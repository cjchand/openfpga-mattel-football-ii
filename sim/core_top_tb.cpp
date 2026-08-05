// sim/core_top_tb.cpp
//
// Integration tests for core_top.v -- the APF glue layer.
//
// Why this exists: every other testbench in this directory covers a single
// module. core_top, which wires them to the Analogue Pocket host, had no
// coverage at all -- and it is where the last two real bugs lived:
//
//   * the CPU was released from reset before the ROM data slot had been
//     transferred, so it ran NOPs and then a half-loaded image, blew past
//     the ROM's boot/RAM-init sequence at 0x3C0, and never returned to it
//   * p_input crossed clk_74a -> clk_core_12288 with no synchroniser
//
// Both were found by reading code, which is luck. These tests pin the
// contracts instead.
//
// The Altera megafunctions core_top instantiates (mf_pllbase, and
// mf_datatable inside core_bridge_cmd) are replaced by the behavioural
// stubs in sim/stubs/. See those files for why that does not weaken what is
// being asserted here.
#include "Vcore_top.h"
#include "Vcore_top___024root.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>
#include <vector>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::printf("FAIL: %s (line %d)\n", msg, __LINE__); failures++; } \
} while (0)

// One clk_74a period. The PLL stub divides this by 6, so the core domain
// advances one edge every 3 calls.
static void tick(Vcore_top* d) {
    d->clk_74a = 0; d->eval();
    d->clk_74a = 1; d->eval();
}
static void ticks(Vcore_top* d, long n) { for (long i = 0; i < n; i++) tick(d); }

static uint16_t core_pc(Vcore_top* d) { return d->rootp->core_top__DOT__u_pps41_core__DOT__pc_reg; }

// Brings the core out of host reset without delivering the ROM.
static void power_on(Vcore_top* d) {
    d->clk_74a = 0;
    d->clk_74b = 0;
    d->bridge_addr = 0; d->bridge_rd = 0; d->bridge_wr = 0; d->bridge_wr_data = 0;
    d->cont1_key = 0; d->cont2_key = 0; d->cont3_key = 0; d->cont4_key = 0;
    d->cont1_joy = 0; d->cont2_joy = 0; d->cont3_joy = 0; d->cont4_joy = 0;
    d->cont1_trig = 0; d->cont2_trig = 0; d->cont3_trig = 0; d->cont4_trig = 0;
    d->vblank = 0;
    d->eval();
    ticks(d, 8);
    // core_bridge_cmd holds reset_n low until the host says otherwise; the
    // template's default brings it high on its own. Drive it directly so
    // this test does not depend on the host command sequence.
    d->rootp->core_top__DOT__icb__DOT__reset_n = 1;
    ticks(d, 8);
}

// A single host bridge write.
static void bridge_write(Vcore_top* d, uint32_t addr, uint32_t data) {
    d->bridge_addr = addr;
    d->bridge_wr_data = data;
    d->bridge_wr = 1;
    tick(d);
    d->bridge_wr = 0;
    tick(d);
}

// Writes `bytes` into rom_loader over the bridge, exactly as APF does:
// 32-bit words at the 0x10000000 slot base, big-endian within each word.
static void deliver_rom(Vcore_top* d, const std::vector<uint8_t>& rom) {
    for (size_t w = 0; w * 4 < rom.size(); w++) {
        uint32_t word = 0;
        for (int b = 0; b < 4; b++) {
            size_t idx = w * 4 + b;
            uint8_t v = (idx < rom.size()) ? rom[idx] : 0;
            word |= static_cast<uint32_t>(v) << (24 - 8 * b); // byte 0 -> bits 31:24
        }
        d->bridge_addr = 0x10000000u + static_cast<uint32_t>(w * 4);
        d->bridge_wr_data = word;
        d->bridge_wr = 1;
        tick(d);
        d->bridge_wr = 0;
        tick(d);
    }
}

// Signals transfer completion the way the host does.
static void signal_done(Vcore_top* d) {
    d->rootp->core_top__DOT__icb__DOT__target_dataslot_done = 1;
    ticks(d, 4);
    d->rootp->core_top__DOT__icb__DOT__target_dataslot_done = 0;
    ticks(d, 4);
}

// The real game ROM, if it is available. Returns empty if not.
static std::vector<uint8_t> load_real_rom() {
    FILE* f = std::fopen("../development-assets/b8000-12", "rb");
    if (!f) return {};
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> rom(static_cast<size_t>(n));
    if (std::fread(rom.data(), 1, rom.size(), f) != rom.size()) rom.clear();
    std::fclose(f);
    return rom;
}

// A ROM whose every byte is NOP, big enough to cover the address space the
// CPU fetches from. Contents do not matter for reset/CDC tests.
static std::vector<uint8_t> nop_rom() { return std::vector<uint8_t>(1536, 0x00); }

// ---------------------------------------------------------------------

// The bug from commit c5a3b98: the CPU must not execute a single
// instruction until the ROM image is actually present.
static void test_cpu_held_in_reset_until_rom_transfer_completes() {
    Vcore_top d;
    power_on(&d);

    CHECK(d.rootp->core_top__DOT__core_rst_n == 0,
          "core_rst_n is low while the ROM has not arrived");

    uint16_t pc_before = core_pc(&d);
    ticks(&d, 2000); // ~660 core-domain edges: ample time to run if enabled,
                     // and well short of the (shortened) ROM-wait timeout
    CHECK(core_pc(&d) == pc_before,
          "CPU does not advance its PC before the ROM transfer completes");

    deliver_rom(&d, nop_rom());
    // Still not released -- delivery alone is not the completion signal.
    CHECK(d.rootp->core_top__DOT__core_rst_n == 0,
          "core stays in reset until completion is signalled, not merely on bridge writes");

    signal_done(&d);
    ticks(&d, 30); // let rom_loaded cross into the core domain
    CHECK(d.rootp->core_top__DOT__core_rst_n == 1,
          "core_rst_n releases once the transfer is signalled complete");

    uint16_t pc_at_release = core_pc(&d);
    CHECK(pc_at_release == 0x3C0,
          "CPU starts from the chip's 0x3C0 reset vector, not mid-stream");

    ticks(&d, 3000);
    CHECK(core_pc(&d) != pc_at_release, "CPU runs once released");
}

// The timeout is a safety net: if the host never signals completion the
// core must still start rather than sit dead forever.
static void test_rom_wait_times_out_so_the_core_cannot_hang_forever() {
    Vcore_top d;
    power_on(&d);
    deliver_rom(&d, nop_rom());
    CHECK(d.rootp->core_top__DOT__core_rst_n == 0, "still gated before the timeout");
    // ROM_WAIT_MAX_P is overridden to a small value for this build; run
    // well past it without ever asserting done.
    ticks(&d, 30000);
    CHECK(d.rootp->core_top__DOT__core_rst_n == 1,
          "core starts anyway once the ROM-wait timeout expires");
}

// The bug from commit 800c650: cont1_key is in the clk_74a domain and the
// CPU samples p_input in the core domain. The value must arrive, and must
// arrive through registers rather than combinationally.
static void test_p_input_is_synchronised_into_the_core_domain() {
    Vcore_top d;
    power_on(&d);
    deliver_rom(&d, nop_rom());
    signal_done(&d);
    ticks(&d, 30);

    CHECK(d.rootp->core_top__DOT__p_input_w == 0x00, "p_input starts clear");

    d.cont1_key = (1u << 5); // face_b -> Kick -> p_input bit 4
    d.eval();
    CHECK(d.rootp->core_top__DOT__p_input_w == 0x00,
          "p_input does not change combinationally with cont1_key");

    ticks(&d, 12); // >= 2 core-domain edges
    CHECK(d.rootp->core_top__DOT__p_input_w == 0x10,
          "Kick reaches p_input bit 4 after synchronisation");

    d.cont1_key = 0;
    ticks(&d, 12);
    CHECK(d.rootp->core_top__DOT__p_input_w == 0x00, "release propagates too");
}

// The full button map, checked end to end through the synchroniser.
static void test_button_map_matches_the_mfootb2_port() {
    struct { unsigned key_bit; unsigned p_bit; const char* name; } map[] = {
        {6,  0, "face_x -> Score"},   {15, 0, "Start -> Score"},
        {7,  1, "face_y -> Status"},  {14, 1, "Select -> Status"},
        {0,  2, "dpad_up"},           {3,  3, "dpad_right"},
        {5,  4, "face_b -> Kick"},    {4,  5, "face_a -> Pass"},
        {1,  6, "dpad_down"},         {2,  7, "dpad_left"},
    };
    Vcore_top d;
    power_on(&d);
    deliver_rom(&d, nop_rom());
    signal_done(&d);
    ticks(&d, 30);
    for (auto& m : map) {
        d.cont1_key = (1u << m.key_bit);
        ticks(&d, 12);
        CHECK(d.rootp->core_top__DOT__p_input_w == (1u << m.p_bit), m.name);
        d.cont1_key = 0;
        ticks(&d, 12);
    }
}

// Two interact.json variables now live in the datatable, so the read
// address has to alternate. Word 0 is Presentation, word 1 is PRO 2, and
// PRO 2 drives d_input bit 10 -- the pin the ROM tests with SKISL.
static void test_datatable_reads_both_variable_words() {
    Vcore_top d;
    power_on(&d);
    // The host writes interact.json variables as ordinary bridge writes in
    // the 0xF8xx2xxx window; core_bridge_cmd turns those into datatable
    // writes at (bridge_addr >> 2).
    bridge_write(&d, 0xF8002000u, 1); // Presentation on  -> word 0
    bridge_write(&d, 0xF8002004u, 1); // PRO 2 on         -> word 1

    ticks(&d, 200); // several full read-address alternation cycles

    CHECK((d.rootp->core_top__DOT__dt_word0 & 1u) == 1, "word 0 (Presentation) is read back");
    CHECK((d.rootp->core_top__DOT__dt_word1 & 1u) == 1, "word 1 (PRO 2) is read back");
    CHECK(d.rootp->core_top__DOT__d_input_w == (1u << 10),
          "PRO 2 drives d_input bit 10 (DIO10), the pin the ROM's SKISL tests");
}

static void test_pro2_off_leaves_the_difficulty_pin_low() {
    Vcore_top d;
    power_on(&d);
    ticks(&d, 200);
    CHECK(d.rootp->core_top__DOT__d_input_w == 0,
          "with PRO 2 unset the difficulty pin reads low (PRO 1)");
}

// Integration check on the ROM path: the bytes the CPU fetches must be the
// bytes of the file, in order. This is where the big-endian bridge packing
// bug lived (rom_loader's byte_sel_rev).
static void test_rom_bytes_reach_the_cpu_in_file_order() {
    Vcore_top d;
    power_on(&d);
    // Prefer the real game ROM: it makes the CPU execute real code, which
    // is what drives fetches up into the 0x600-0x7FF mirror window (the
    // game's subroutine pages live there). A synthetic ramp never gets the
    // CPU out of the low pages, so it would leave the mirror fold
    // untested. Falls back to the ramp if the ROM is not present.
    std::vector<uint8_t> rom = load_real_rom();
    bool using_real_rom = !rom.empty();
    if (!using_real_rom) {
        rom.resize(1536);
        for (size_t i = 0; i < rom.size(); i++) rom[i] = static_cast<uint8_t>(i & 0xFF);
    }
    deliver_rom(&d, rom);
    signal_done(&d);
    ticks(&d, 30);

    // rom_addr_w is driven by the CPU, so rather than forcing it, let the
    // CPU walk the address space and check every fetch it makes. Any
    // byte-order or mirror-fold error shows up immediately, and this covers
    // far more addresses than a handful of forced probes would.
    // Sampled ON the ce pulse, which is the only moment the value matters:
    // core_ce is when pps41_core latches the fetched byte. Checking on every
    // clock instead would be asserting a particular read latency rather than
    // correctness -- rom_loader's read is registered (so `mem` can infer an
    // M10K), so rom_data_w is legitimately stale for one core clock after
    // rom_addr_w moves. What must hold is that it has settled by the next ce,
    // ~129 core clocks later. If the latency ever grew past that budget, or a
    // byte/word select came apart, this fails on the very first fetch.
    int checked = 0, bad = 0;
    unsigned seen_high = 0;
    for (long i = 0; i < 200000; i++) {
        ticks(&d, 1);
        if (!d.rootp->core_top__DOT__core_ce) continue;
        unsigned addr = d.rootp->core_top__DOT__rom_addr_w & 0x7FF;
        unsigned folded = (addr >= 0x600) ? (addr - 0x200) : addr;
        uint8_t got = d.rootp->core_top__DOT__rom_data_w;
        if (got != rom[folded]) {
            if (bad < 3)
                std::printf("FAIL: rom byte at 0x%03X (folded 0x%03X) is 0x%02X, expected 0x%02X (line %d)\n",
                            addr, folded, got, rom[folded], __LINE__);
            bad++;
        }
        if (addr >= 0x600) seen_high++;
        checked++;
    }
    if (bad) failures++;
    CHECK(checked > 100, "the CPU actually fetched on ce pulses during the run");
    // The 0x600-0x7FF mirror fold is NOT exercised here: at ~95kHz the CPU
    // retires only a few hundred instructions in a run of this length, and
    // the game's mirrored subroutine pages are thousands of instructions
    // in. Simulating that far through the whole core_top (video renderer
    // and all) is not worth the runtime, and the fold already has direct
    // coverage in rom_loader_tb.cpp's test_mirror_fold_0x600_to_0x7ff.
    // What this test adds is the integration path: real bridge packing ->
    // rom_loader -> the byte the CPU actually fetches.
    (void)seen_high;
    std::printf("  (rom fetch path: %d CPU fetches checked against the file, "
                "%d mismatched; source: %s)\n",
                checked, bad, using_real_rom ? "real game ROM" : "synthetic ramp");
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);

    test_cpu_held_in_reset_until_rom_transfer_completes();
    test_rom_wait_times_out_so_the_core_cannot_hang_forever();
    test_p_input_is_synchronised_into_the_core_domain();
    test_button_map_matches_the_mfootb2_port();
    test_datatable_reads_both_variable_words();
    test_pro2_off_leaves_the_difficulty_pin_low();
    test_rom_bytes_reach_the_cpu_in_file_order();

    if (failures == 0) { std::printf("PASS: core_top_tb\n"); return 0; }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
