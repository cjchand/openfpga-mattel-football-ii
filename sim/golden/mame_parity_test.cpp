// sim/golden/mame_parity_test.cpp
//
// Whole-ROM parity check against MAME's own MM77LA CPU core.
//
// Every other golden-model test in this directory pins one opcode's
// semantics in isolation. Those are necessary but not sufficient: the bugs
// that made this game unplayable (a 2-bit-instead-of-3-bit EOB immediate, a
// carry delay that published one instruction too early, and skipped bytes
// polluting the prev_op history) each pass any plausible isolated test of
// the *other* opcodes, and only show up as control flow drifting apart
// thousands of instructions later. This test catches exactly that class.
//
// It replays the real game ROM for 200,000 retired instructions per input
// scenario and checks the resulting register trace against a digest taken
// from MAME running the same ROM with the same input. Matching MAME here is
// what "the CPU is correct" actually means for this project -- MAME's core
// is the reference implementation this game is known to run correctly on.
//
// == Regenerating the reference digests ==
//
// The digests below were produced from real MAME traces, not from this
// model. To regenerate them (after a deliberate, understood semantics
// change -- never to paper over an unexplained failure):
//
//  1. Build a MAME cut down to just this driver, which is far faster than a
//     full MAME build:
//         cd <mame>
//         make SUBTARGET=pps41 SOURCES=src/mame/handheld/hh_pps41.cpp \
//              USE_LIBSDL=1 ARCHOPTS=-I/opt/homebrew/include REGENIE=1 -j10
//     On macOS the generated link line omits SDL3; adding
//     `-L/opt/homebrew/lib -lSDL3` to the LIBS line of
//     build/projects/sdl3/mamepps41/gmake-osx-clang/pps41.make (it must come
//     AFTER the .a archives, so LDFLAGS does not work) links it.
//
//  2. Build a romset from development-assets/ -- the two files there already
//     match MAME's expected SHA1s exactly:
//         zip mfootb2.zip b8000-12 mm77la_mfootb2_output.pla
//
//  3. Trace, holding one button via a Lua autoboot script:
//         pps41 mfootb2 -rompath . -debug -video none -sound none \
//               -nothrottle -seconds_to_run 3 -skip_gameinfo \
//               -autoboot_script hold.lua -debugscript dbg.txt
//     where dbg.txt is:
//         trace out.txt,0,noloop,{tracelog "| A=%X B=%02X C=%X S=%X ", a, b, c, s}
//         go
//     Note MAME logs ONLY non-skipped instructions, and logs each one's
//     register state as of BEFORE it executes -- trace_line() below
//     reproduces both properties.
//
//  4. `press_at` is a traced-instruction index, found by diffing the held
//     trace against the idle one: the first differing line is where the
//     button first reaches the ROM. Using an index rather than a wall-clock
//     time is what makes this comparison exact despite MAME pressing the
//     button on a frame boundary.
//
// The D-pad scenarios are included deliberately: at idle the D-pad produces
// no visible reaction, and it is worth having it recorded that MAME does the
// same thing, so that behaviour is not re-investigated as a bug.
#include "mm77la_model.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr long kInstructions = 200000;

struct Scenario {
    const char* name;
    uint8_t p_input;        // button bit held, per MAME's mfootb2 IN.0 port
    long press_at;          // traced-instruction index the press becomes visible
    uint64_t expected_fnv;  // FNV-1a/64 over the trace text, from MAME
};

// Digests captured from MAME 0.281-era src/devices/cpu/pps41 running mfootb2.
const Scenario kScenarios[] = {
    {"idle",   0x00,      0, 0x98f28c00af7c9833ULL},
    {"score",  0x01,  69862, 0xc7aec897844d84b3ULL},
    {"status", 0x02,  69862, 0x63ec5e1712e30eb8ULL},
    {"up",     0x04,  69862, 0x6265ac425c551565ULL},
    {"right",  0x08,  69862, 0x4e741d9d002d1d3cULL},
    {"kick",   0x10,  69864, 0x23c9a712f8ed27adULL},
    {"pass",   0x20,  69864, 0x8f01a0a4fe8e30b7ULL},
    {"down",   0x40,  69864, 0xe0db21f34c2c5b69ULL},
    {"left",   0x80,  69864, 0xaae0b45023cbe499ULL},
};

void fnv1a(uint64_t& h, const char* s) {
    for (; *s; ++s) {
        h ^= static_cast<unsigned char>(*s);
        h *= 0x100000001b3ULL;
    }
}

// Runs one scenario and returns the digest of its trace.
uint64_t run(const std::vector<uint8_t>& rom, const Scenario& sc) {
    Mm77laModel m(rom.data(), rom.size());
    m.reset();

    uint64_t h = 0xcbf29ce484222325ULL;
    // `retired` counts every instruction the chip actually retires, starting
    // at the mandatory NOP on the 0x3C0 reset vector. `press_at` is in these
    // units. MAME's own trace omits that reset NOP (its debugger hook starts
    // one instruction late, at 0x3E0), so the digest below covers retired
    // instructions 1..kInstructions -- index 1 here is MAME's index 0.
    long retired = 0;
    long hashed = 0;
    while (hashed < kInstructions) {
        m.debug_set_p(retired >= sc.press_at ? sc.p_input : 0x00);

        const auto& s = m.state();
        // MAME logs an instruction only when it is not being skipped
        // (execute_run(): `if (!m_skip && !m_skip_count)`).
        bool skipped = s.skip || s.skip_count > 0;
        if (!skipped) {
            if (retired > 0) {
                // Read the model's own c_in rather than recomputing it.
                // step() now publishes it at the END of the step, so this
                // IS the value the instruction about to run will observe.
                // Recomputing it here used to hide c_in bugs from this
                // test entirely -- see docs/follow-ups.md item 2.
                unsigned c_in = s.c_in;
                char line[64];
                std::snprintf(line, sizeof(line), "%03X A=%X B=%02X C=%X S=%X\n",
                              s.pc & 0x7FF, s.a & 0xF, s.b & 0x7F, c_in & 1u, s.s & 0xF);
                fnv1a(h, line);
                hashed++;
            }
            retired++;
        }
        m.step();
    }
    return h;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <rom-file>\n", argv[0]);
        return 2;
    }
    FILE* f = std::fopen(argv[1], "rb");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> rom(static_cast<size_t>(size));
    if (std::fread(rom.data(), 1, rom.size(), f) != rom.size()) {
        std::fprintf(stderr, "short read on %s\n", argv[1]);
        std::fclose(f);
        return 2;
    }
    std::fclose(f);

    int failures = 0;
    for (const auto& sc : kScenarios) {
        uint64_t got = run(rom, sc);
        if (got != sc.expected_fnv) {
            std::fprintf(stderr,
                "FAIL %-6s: diverged from MAME over %ld instructions "
                "(digest %016llx, expected %016llx)\n",
                sc.name, kInstructions,
                static_cast<unsigned long long>(got),
                static_cast<unsigned long long>(sc.expected_fnv));
            failures++;
        }
    }

    if (failures == 0) {
        std::printf("PASS: MAME parity, %ld instructions x %zu input scenarios\n",
                    kInstructions, sizeof(kScenarios) / sizeof(kScenarios[0]));
        return 0;
    }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
