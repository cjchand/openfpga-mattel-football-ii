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

// Drives `windows` consecutive windows each with the same `on` on-cycle
// count, letting the alpha=1/8 smoothing filter converge to its fixed
// point (on-count itself, for a constant input). 80 windows is far more
// than enough for integer-rounded convergence at these on-counts.
static void drive_steady(DisplayPwmState& st, int windows, int on, uint16_t rowsel, uint16_t rowdata) {
    for (int w = 0; w < windows; w++) drive_window(st, on, rowsel, rowdata);
}

// Same as drive_window, but the `on` cycles are the LAST `on` cycles of the
// window (including the boundary cycle itself) rather than the first --
// regression coverage for the off-by-one where classification read the
// stale pre-increment cnt[i], silently dropping the boundary cycle's own
// contribution when that cycle happened to be active.
static void drive_window_tail(DisplayPwmState& st, int on, uint16_t rowsel, uint16_t rowdata) {
    for (int i = 0; i < kDisplayWindow; i++) {
        bool active = i >= (kDisplayWindow - on);
        display_pwm_step(st, active ? rowsel : 0, active ? rowdata : 0);
    }
}

static void drive_steady_tail(DisplayPwmState& st, int windows, int on, uint16_t rowsel, uint16_t rowdata) {
    for (int w = 0; w < windows; w++) drive_window_tail(st, on, rowsel, rowdata);
}

// Sustained (steady-state) thresholds: once the alpha=1/8 smoothing filter
// has converged (many consecutive windows of the same on-count), the
// classification matches the original single-window thresholds exactly --
// smoothing only changes how fast a cell ramps up/down, not where the
// eventual steady-state cutoffs fall.
static void test_steady_state_thresholds() {
    DisplayPwmState st;
    // row 2, col 3: BRIGHT_MIN=317 on-cycles/window sustained -> bright
    drive_steady(st, 80, kDisplayBrightMin, 1u << 2, 1u << 3);
    CHECK(st.levels[2 * 11 + 3] == 2);

    DisplayPwmState st2;
    drive_steady(st2, 80, kDisplayDimMin, 1u << 2, 1u << 3);
    CHECK(st2.levels[2 * 11 + 3] == 1);

    DisplayPwmState st3;
    drive_steady(st3, 80, kDisplayDimMin - 1, 1u << 2, 1u << 3);
    CHECK(st3.levels[2 * 11 + 3] == 0);

    DisplayPwmState st4;
    drive_steady(st4, 80, kDisplayBrightMin - 1, 1u << 2, 1u << 3);
    CHECK(st4.levels[2 * 11 + 3] == 1); // still dim, one tick short of bright
}

// The core anti-flicker property this smoothing filter exists for: a cell
// that's been solidly lit for a while does NOT snap to fully off after a
// single window's worth of dropout (e.g. an idle-loop scan cycle that
// briefly doesn't re-strobe it) -- it decays gradually instead. This is
// what real hardware testing showed was missing: the previous
// classify-from-raw-cnt-only design reclassified from scratch every single
// window, so any transient dip in a cell's duty cycle was visible as a
// full on/off flash on real 60fps video, even though it never showed up in
// simulation's single static screenshots.
static void test_single_window_dropout_does_not_blank() {
    DisplayPwmState st;
    // Converge close to fully-lit (smooth approaches kDisplayWindow).
    drive_steady(st, 80, kDisplayWindow, 1u << 1, 1u << 9);
    CHECK(st.levels[1 * 11 + 9] == 2);

    // One window of complete silence: smooth only drops by ~1/8 (still
    // far above BRIGHT_MIN), so the cell must still read bright, not off.
    drive_window(st, 0, 1u << 1, 1u << 9);
    CHECK(st.levels[1 * 11 + 9] == 2);

    // But it's not stuck on forever -- enough sustained silence eventually
    // decays it to off.
    drive_steady(st, 80, 0, 1u << 1, 1u << 9);
    CHECK(st.levels[1 * 11 + 9] == 0);
}

static void test_levels_hold_mid_window() {
    DisplayPwmState st;
    drive_steady(st, 3, kDisplayWindow, 1u << 0, 1u << 0); // converge to fully lit
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

static void test_boundary_cycle_activity_counts() {
    DisplayPwmState st;
    drive_steady_tail(st, 80, kDisplayBrightMin, 1u << 5, 1u << 7);
    CHECK(st.levels[5 * 11 + 7] == 2);

    DisplayPwmState st2;
    drive_steady_tail(st2, 80, kDisplayDimMin, 1u << 5, 1u << 7);
    CHECK(st2.levels[5 * 11 + 7] == 1);
}

static void test_no_coincidence_no_accumulation() {
    DisplayPwmState st;
    // row bit and col bit alternate, never both present in the same call
    for (int w = 0; w < 3; w++) {
        for (int i = 0; i < kDisplayWindow; i++) {
            bool odd = i & 1;
            display_pwm_step(st, odd ? (1u << 4) : 0, odd ? 0 : (1u << 6));
        }
    }
    CHECK(st.levels[4 * 11 + 6] == 0);
}

int main() {
    test_steady_state_thresholds();
    test_single_window_dropout_does_not_blank();
    test_levels_hold_mid_window();
    test_window_tick_fires_once_per_window();
    test_boundary_cycle_activity_counts();
    test_no_coincidence_no_accumulation();
    if (failures == 0) { std::printf("PASS\n"); return 0; }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
