// sim/golden/display_parity_test.cpp
//
// Whole-display parity against MAME, for the one part of the chain the CPU
// parity test does not reach: display_mux's matrix reconstruction and
// display_pwm's duty accumulation and brightness thresholds.
//
// Those were previously verified only against this project's own golden
// model and hand-derived expectations -- the same shared-assumption
// situation that let three CPU bugs survive (see
// docs/kick-tone-lockup-investigation.md). MAME's pwm_display is directly
// comparable: mfootb2 configures set_bri_levels(0.015, 0.2), so the level
// it writes to each "y.x" output is a 0/1/2 index against exactly the
// thresholds this model uses (24/1583 = 0.0152, 317/1583 = 0.200).
//
// == Capturing the reference ==
//
//   pps41 mfootb2 -rompath . -video none -sound none -nothrottle \
//         -seconds_to_run 4 -skip_gameinfo -autoboot_script dispdump.lua
//
// where dispdump.lua, at frame 170 (~2.83s, idle, no buttons), prints
//   for y = 0,9: for x = 0,10: manager.machine.output:get_value("y.x")
//
// Frame 170 corresponds to step 170 * 1583 = 269110 here, since this
// model's display window is 1583 cycles -- one 60Hz frame at the ~95kHz
// instruction rate.
#include "mm77la_model.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

static int failures = 0;

// Captured from MAME 0.281-era hh_pps41 running mfootb2, idle, frame 170.
static const uint8_t kMame[10][11] = {
    {0,1,1,0,0,0,0,1,0,0,0},
    {1,0,1,1,0,1,1,1,0,0,0},
    {1,1,1,1,1,1,0,1,0,0,0},
    {0,0,0,0,0,0,0,0,0,2,0},
    {0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,1,0,1},
    {0,0,0,0,0,0,0,0,0,1,0},
};

// Cell (3,9) is the one genuinely time-varying element on an idle screen:
// its raw duty alternates between 0 and ~492 on a ~4-frame period, so its
// level legitimately reads dim or bright depending on which frame you
// sample. MAME's 60Hz real-time frame boundary and this model's
// 1583-cycle window are not phase-locked, so the two can disagree on this
// one cell while agreeing everywhere else. It is checked separately below
// rather than pinned to a value.
static const int kBlinkY = 3, kBlinkX = 9;

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

    Mm77laModel m(rom.data(), rom.size());
    m.reset();
    for (long i = 0; i < 269110; i++) { m.debug_set_p(0); m.step(); }

    const auto& L = m.state().display.levels;
    int mismatches = 0;
    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 11; x++) {
            if (y == kBlinkY && x == kBlinkX) continue;
            uint8_t got = L[y * 11 + x];
            if (got != kMame[y][x]) {
                if (mismatches < 6)
                    std::printf("FAIL: cell (%d,%d) is %u, MAME has %u\n", y, x, got, kMame[y][x]);
                mismatches++;
            }
        }
    }
    if (mismatches) { std::printf("FAIL: %d of 109 static cells differ from MAME\n", mismatches); failures++; }

    // The blinking cell must be lit at all, and must actually still blink --
    // if it ever froze to one level (which is what over-smoothing did) that
    // is a real regression this test should catch.
    uint8_t blink_now = L[kBlinkY * 11 + kBlinkX];
    if (blink_now != 1 && blink_now != 2) {
        std::printf("FAIL: blinking cell (%d,%d) is %u, expected dim or bright\n",
                    kBlinkY, kBlinkX, blink_now);
        failures++;
    }
    int seen[3] = {0, 0, 0};
    for (long i = 0; i < 200000; i++) {
        m.debug_set_p(0);
        m.step();
        if (m.state().display.window_tick) seen[L[kBlinkY * 11 + kBlinkX]]++;
    }
    if (seen[1] == 0 || seen[2] == 0) {
        std::printf("FAIL: cell (%d,%d) no longer alternates dim/bright "
                    "(off=%d dim=%d bright=%d) -- display smoothing may be over-damped\n",
                    kBlinkY, kBlinkX, seen[0], seen[1], seen[2]);
        failures++;
    }

    if (failures == 0) {
        std::printf("PASS: display parity with MAME (109/109 static cells; "
                    "blinking cell alternates dim=%d bright=%d)\n", seen[1], seen[2]);
        return 0;
    }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
