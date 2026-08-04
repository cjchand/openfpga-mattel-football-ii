// sim/golden/pro_switch_test.cpp
//
// Executes the real ROM's difficulty-read block and checks that the
// PRO 1 / PRO 2 pin actually selects between its two constants.
//
// Found by disassembly at 0x35E-0x37C (LFSR execution order):
//
//   0x35E  LB    10     select DIO10
//   0x36F  EOB   2
//   0x357  L     0      A = RAM[0x2A]
//   0x34B  AISK  15     skips the next instruction unless A overflows
//   0x345  T     $3A    (skipped, so control falls through)
//   0x362  ROS          release DIO10 so it reads as an input
//   0x371  SKISL        test the pin -- the ROM's ONLY SKISL
//   0x378  LAI   3      PRO 2 constant
//   0x35C  LAI   4      PRO 1 constant
//
// Pin high -> SKISL does not skip -> LAI 3 runs -> A=3, and the following
// LAI 4 is suppressed by successive-LAI coalescing.
// Pin low  -> SKISL skips -> LAI 3 is skipped -> LAI 4 runs -> A=4.
//
// Note what the low case depends on: a SKIPPED LAI must not count as a
// preceding LAI for the coalescing rule. That is the skipped-byte
// prev_op behaviour (MAME substitutes a fake NOP). With that reverted this
// test yields A=F instead of A=4 -- verified directly -- so this doubles as
// a guard on that rule in the exact place the ROM depends on it.
//
// Caveat, stated plainly: this block is not reached by any input sequence
// simulated so far -- idle, each of the eight buttons held or tapped, and
// randomised fuzzing (40 trials x 3,000,000 steps). So this proves the pin
// is wired and honoured, not that ordinary play uses it. The entry point
// 0x35E is reached only via a TR-prefixed long jump, which the static
// disassembler cannot resolve.
//
// A MAME cross-check was attempted and is NOT strong evidence: MAME's Lua
// machine-frame notifier silently stops firing after ~175 frames (~2.9s),
// so a script scheduling button presses past that point does nothing at
// all. The comparison that appeared to cover "30s of rich play" in fact
// only delivered its first two presses. Drive MAME inputs within the first
// ~175 frames, or verify the presses actually landed.
#include "mm77la_model.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::printf("FAIL: %s (line %d)\n", msg, __LINE__); failures++; } \
} while (0)

static uint8_t run_block(const std::vector<uint8_t>& rom, uint16_t d_input) {
    Mm77laModel m(rom.data(), rom.size());
    m.reset();
    m.debug_set_pc(0x35E);
    m.debug_ram_write(0x2A, 0); // so AISK 15 does not overflow and the T is skipped
    for (int i = 0; i < 9; i++) {
        m.debug_set_d_input(d_input); // hold the switch for the whole block
        m.step();
    }
    return m.state().a;
}

int main(int argc, char** argv) {
    if (argc != 2) { std::fprintf(stderr, "usage: %s <rom-file>\n", argv[0]); return 2; }
    FILE* f = std::fopen(argv[1], "rb");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> rom(static_cast<size_t>(size));
    if (std::fread(rom.data(), 1, rom.size(), f) != rom.size()) { std::fclose(f); return 2; }
    std::fclose(f);

    uint8_t pro1 = run_block(rom, 0x000);
    uint8_t pro2 = run_block(rom, 0x400);

    CHECK(pro1 == 0x4, "PRO 1 (DIO10 low) selects the LAI 4 constant");
    CHECK(pro2 == 0x3, "PRO 2 (DIO10 high) selects the LAI 3 constant");
    CHECK(pro1 != pro2, "the difficulty pin actually changes the ROM's result");

    // An input on an unrelated D pin must not affect this branch.
    CHECK(run_block(rom, 0x001) == 0x4, "an input on DIO0 does not disturb the difficulty read");

    if (failures == 0) {
        std::printf("PASS: pro_switch_test (PRO 1 -> A=%X, PRO 2 -> A=%X)\n", pro1, pro2);
        return 0;
    }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
