#include "mm77la_display_pwm.h"
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

// Drives `on` cycles of (rowsel,rowdata) then idle for the rest of one window.
static void drive_window(DisplayPwmState& st, int on, uint16_t rowsel, uint16_t rowdata) {
    for (int i = 0; i < kDisplayWindow; i++) {
        bool active = i < on;
        display_pwm_step(st, active ? rowsel : 0, active ? rowdata : 0);
    }
}

static void test_thresholds() {
    DisplayPwmState st;
    // row 2, col 3: BRIGHT_MIN=317 ticks -> 317/1583=20.03% -> bright
    drive_window(st, kDisplayBrightMin, 1u << 2, 1u << 3);
    CHECK(st.levels[2 * 11 + 3] == 2);

    drive_window(st, kDisplayDimMin, 1u << 2, 1u << 3);
    CHECK(st.levels[2 * 11 + 3] == 1);

    drive_window(st, kDisplayDimMin - 1, 1u << 2, 1u << 3);
    CHECK(st.levels[2 * 11 + 3] == 0);

    drive_window(st, kDisplayBrightMin - 1, 1u << 2, 1u << 3);
    CHECK(st.levels[2 * 11 + 3] == 1); // still dim, one tick short of bright
}

static void test_levels_hold_mid_window() {
    DisplayPwmState st;
    drive_window(st, kDisplayWindow, 1u << 0, 1u << 0); // fully lit whole window
    CHECK(st.levels[0] == 2);
    for (int i = 0; i < kDisplayWindow / 2; i++) display_pwm_step(st, 0, 0);
    CHECK(st.levels[0] == 2); // holds steady mid-window even though currently idle
}

static void test_window_tick_fires_once_per_window() {
    DisplayPwmState st;
    int ticks = 0;
    for (int i = 0; i < kDisplayWindow * 3; i++) {
        display_pwm_step(st, 0, 0);
        if (st.window_tick) ticks++;
    }
    CHECK(ticks == 3);
}

// Drives `on` cycles at the END of the window (through and including the
// boundary/classification cycle), not the start. The golden model already
// increments cnt[cell] before checking the boundary condition every call
// (see display_pwm_step), so it has always included the boundary cycle's
// own contribution -- this test documents that property and gives the RTL
// side (whose classification previously read the stale pre-increment
// value on this exact cycle) a matching golden reference to lockstep
// against.
static void drive_window_tail(DisplayPwmState& st, int on, uint16_t rowsel, uint16_t rowdata) {
    for (int i = 0; i < kDisplayWindow; i++) {
        bool active = i >= (kDisplayWindow - on);
        display_pwm_step(st, active ? rowsel : 0, active ? rowdata : 0);
    }
}

static void test_boundary_cycle_activity_counts() {
    DisplayPwmState st;
    drive_window_tail(st, kDisplayBrightMin, 1u << 5, 1u << 7);
    CHECK(st.levels[5 * 11 + 7] == 2);

    DisplayPwmState st2;
    drive_window_tail(st2, kDisplayDimMin, 1u << 5, 1u << 7);
    CHECK(st2.levels[5 * 11 + 7] == 1);
}

static void test_no_coincidence_no_accumulation() {
    DisplayPwmState st;
    // row bit and col bit alternate, never both present in the same call
    for (int i = 0; i < kDisplayWindow; i++) {
        bool odd = i & 1;
        display_pwm_step(st, odd ? (1u << 4) : 0, odd ? 0 : (1u << 6));
    }
    CHECK(st.levels[4 * 11 + 6] == 0);
}

int main() {
    test_thresholds();
    test_levels_hold_mid_window();
    test_window_tick_fires_once_per_window();
    test_boundary_cycle_activity_counts();
    test_no_coincidence_no_accumulation();
    if (failures == 0) { std::printf("PASS\n"); return 0; }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
