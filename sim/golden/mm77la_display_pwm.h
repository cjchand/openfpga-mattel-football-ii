#pragma once
#include <cstdint>
#include <array>

constexpr int kDisplayCells = 110; // 10 rows x 11 cols
constexpr uint16_t kDisplayWindow = 1583;   // round(380000 / 4 / 60)
constexpr uint16_t kDisplayDimMin = 24;     // (1583*15)/1000 + 1
constexpr uint16_t kDisplayBrightMin = 317; // 1583/5 + 1

struct DisplayPwmState {
    uint16_t window_pos = 0;
    std::array<uint16_t, kDisplayCells> cnt{};
    // Exponentially-smoothed duty estimate (alpha=1/2), same 0..kDisplayWindow
    // scale as cnt/thresholds. This matches MAME pwm_display_device's default
    // interpolation factor of 0.5 (src/devices/video/pwm.cpp).
    //
    // This was alpha=1/8 for a while, to suppress what looked like broad
    // idle flicker across the field. That flicker was mostly a symptom of
    // the CPU's EOB bug (RAM banks 4-7 were unreachable, so the ROM's
    // display state was never built correctly). With the CPU fixed, idle is
    // stable except for two cells that genuinely blink -- measured directly:
    // one alternates cleanly between duty 0 and ~492 on a ~4-frame period.
    // alpha=1/8 averaged that real blink into a steady dim glow, and its
    // ~133ms time constant smeared moving objects into visible motion
    // trails (the ball left a tail of ~2.7 extra lit cells while in flight).
    // alpha=1/2 is both the reference behaviour and visibly correct here.
    std::array<uint16_t, kDisplayCells> smooth{};
    std::array<uint8_t, kDisplayCells> levels{}; // 0/1/2, settled once per window
    bool window_tick = false; // true only on the step() call that just settled a new window
};

// Called once per CPU step(), after that step's rowsel/rowdata are known.
// Accumulates on-time for cells where (rowsel bit for row) AND (rowdata bit
// for col) are both set, then classifies+resets every kDisplayWindow calls.
void display_pwm_step(DisplayPwmState& st, uint16_t rowsel, uint16_t rowdata);
