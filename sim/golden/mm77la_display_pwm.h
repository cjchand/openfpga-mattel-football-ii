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
    std::array<uint8_t, kDisplayCells> levels{}; // 0/1/2, settled once per window
    bool window_tick = false; // true only on the step() call that just settled a new window
};

// Called once per CPU step(), after that step's rowsel/rowdata are known.
// Accumulates on-time for cells where (rowsel bit for row) AND (rowdata bit
// for col) are both set, then classifies+resets every kDisplayWindow calls.
void display_pwm_step(DisplayPwmState& st, uint16_t rowsel, uint16_t rowdata);
