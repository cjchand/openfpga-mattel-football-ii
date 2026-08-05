// sim/golden/tone_stuck_fuzz.cpp
//
// Searches for the stuck-tone lockup seen on hardware.
//
// What the device reported (debug_probe strip, photographed 2026-08-04):
// cause_tone set, cause_pc clear, latched PC = 0x3BA. So the CPU was still
// executing -- it had simply held tone_on continuously for 2 seconds, which
// no legitimate sound effect in this game does.
//
// The mechanism that can produce exactly that is visible in the ROM. IOS is
// a three-state machine in the chip (state 1 turns the tone ON, states 0 and
// 2 turn it OFF), and the game keeps its OWN mirror of that state in a RAM
// bit, e.g. at page 0b:
//
//     0b:2b SKBF 4     ; if the "tone running" flag is clear, skip...
//     0b:2a IOS        ; ...this state-consuming IOS
//     0b:35 RB   4     ; clear the flag
//     ...
//     0b:3f LAI 15 / 0b:1f IOS / 0b:0f LAI 5 / 0b:07 A / 0b:03 IOS
//
// If the chip's ios_state and the ROM's RAM-bit mirror ever disagree by one,
// the IOS the ROM intends as "turn off" lands on chip state 1 and turns the
// tone ON instead -- permanently, until some later IOS, with the CPU running
// normally throughout. That is the observed symptom precisely.
//
// So this fuzzes the model with randomised, human-plausible button traffic
// and asserts the invariant the probe checks on hardware: tone_on is never
// continuously high for TONE_STUCK_CE cycles. A violation is a reproducible
// case for the RTL, and the seed that produced it is printed so it can be
// replayed.
#include "mm77la_model.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

namespace {

// Matches src/debug_probe.v's TONE_STUCK_CE: ~2.0s at the 95kHz cycle rate.
constexpr long kToneStuckCe = 190000;

// MAME's mfootb2 IN.0 bit assignment.
const uint8_t kButtons[8] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80};

struct Result {
    bool     stuck = false;
    long     step_at = 0;      // cycle the 2s threshold was crossed
    long     tone_started = 0; // cycle tone_on last went high
    uint16_t pc = 0;
    uint8_t  ios_state = 0;
    uint8_t  tone_freq = 0;
    uint8_t  p_at_stuck = 0;
};

Result run(const std::vector<uint8_t>& rom, uint32_t seed, long steps, bool verbose) {
    Mm77laModel m(rom.data(), rom.size());
    m.reset();

    std::mt19937 rng(seed);
    auto uni = [&](int lo, int hi) {
        return std::uniform_int_distribution<int>(lo, hi)(rng);
    };

    Result res;
    uint8_t  p = 0;
    long     next_change = 20000; // let the boot sequence settle first
    bool     holding = false;
    long     tone_run = 0;
    long     tone_started = 0;

    for (long i = 0; i < steps; i++) {
        if (i >= next_change) {
            if (holding) {
                p = 0;
                holding = false;
                // Gap between presses: 50ms .. 2.5s.
                next_change = i + uni(5000, 240000);
            } else {
                p = kButtons[uni(0, 7)];
                // 1-in-8 chance of a second button held at the same time,
                // which a real player's thumbs can produce and a scripted
                // stimulus never does.
                if (uni(0, 7) == 0) p |= kButtons[uni(0, 7)];
                holding = true;
                // Hold: 30ms .. 400ms.
                next_change = i + uni(3000, 38000);
            }
        }
        m.debug_set_p(p);
        m.step();

        const auto& s = m.state();
        if (s.tone.tone_on) {
            if (tone_run == 0) tone_started = i;
            tone_run++;
            if (tone_run >= kToneStuckCe) {
                res.stuck        = true;
                res.step_at      = i;
                res.tone_started = tone_started;
                res.pc           = s.pc & 0x7FF;
                res.ios_state    = s.tone.ios_state;
                res.tone_freq    = s.tone.tone_freq;
                res.p_at_stuck   = p;
                return res;
            }
        } else {
            if (verbose && tone_run > 40000) {
                std::printf("  seed %u: long-but-legal tone %ld cycles (%.2fs) ending at step %ld\n",
                            seed, tone_run, tone_run / 95000.0, i);
            }
            tone_run = 0;
        }
    }
    return res;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
            "usage: %s <rom-file> [trials] [steps-per-trial] [--verbose]\n", argv[0]);
        return 2;
    }
    long trials = (argc > 2) ? std::strtol(argv[2], nullptr, 0) : 40;
    long steps  = (argc > 3) ? std::strtol(argv[3], nullptr, 0) : 20000000;
    bool verbose = false;
    for (int i = 1; i < argc; i++)
        if (std::strcmp(argv[i], "--verbose") == 0) verbose = true;

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

    std::printf("fuzzing %ld trials x %ld cycles (%.0fs of chip time each)\n",
                trials, steps, steps / 95000.0);
    for (long t = 0; t < trials; t++) {
        Result r = run(rom, static_cast<uint32_t>(t), steps, verbose);
        if (r.stuck) {
            std::printf("STUCK TONE: seed %ld, tone on from cycle %ld, still on at %ld "
                        "(%.2fs), pc=%03X ios_state=%d tone_freq=%02X p=%02X\n",
                        t, r.tone_started, r.step_at,
                        (r.step_at - r.tone_started) / 95000.0,
                        r.pc, r.ios_state, r.tone_freq, r.p_at_stuck);
            return 1;
        }
        std::printf("  seed %ld: clean\n", t);
        std::fflush(stdout);
    }
    std::printf("PASS: no stuck tone in %ld trials\n", trials);
    return 0;
}
