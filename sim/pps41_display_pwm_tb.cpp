#include "Vpps41_display_pwm.h"
#include "verilated.h"
#include <cstdio>

static void tick(Vpps41_display_pwm* dut) {
    dut->clk = 0; dut->eval();
    dut->clk = 1; dut->eval();
}

static const int WINDOW = 1583, DIM_MIN = 24, BRIGHT_MIN = 317;

static Vpps41_display_pwm* make_dut() {
    Vpps41_display_pwm* dut = new Vpps41_display_pwm;
    dut->rst_n = 0; dut->rowsel = 0; dut->rowdata = 0;
    dut->ce = 1;
    tick(dut);
    dut->rst_n = 1;
    return dut;
}

static void drive_window(Vpps41_display_pwm* dut, int on, uint16_t rowsel, uint16_t rowdata) {
    for (int i = 0; i < WINDOW; i++) {
        bool active = i < on;
        dut->rowsel = active ? rowsel : 0;
        dut->rowdata = active ? rowdata : 0;
        tick(dut);
    }
}

// Drives `windows` consecutive windows at the same on-count, letting the
// alpha=1/8 smoothing filter converge to its fixed point (the on-count
// itself, for a constant input, when approaching from a cold/zeroed start
// -- see pps41_display_pwm_tb's per-test fresh dut instances, which avoid
// the narrow edge case where a value that's already converged to one
// target gets pinned for a step when the target then drops by exactly 1).
static void drive_steady(Vpps41_display_pwm* dut, int windows, int on, uint16_t rowsel, uint16_t rowdata) {
    for (int w = 0; w < windows; w++) drive_window(dut, on, rowsel, rowdata);
}

// Same as drive_window, but the `on` cycles are the LAST `on` cycles of the
// window (including the WIN_LAST boundary cycle itself) rather than the
// first -- regression test for the off-by-one where the RTL's boundary
// classification read the stale pre-increment cnt[i], silently dropping
// the WIN_LAST cycle's own contribution when that cycle happened to be
// active.
static void drive_window_tail(Vpps41_display_pwm* dut, int on, uint16_t rowsel, uint16_t rowdata) {
    for (int i = 0; i < WINDOW; i++) {
        bool active = i >= (WINDOW - on);
        dut->rowsel = active ? rowsel : 0;
        dut->rowdata = active ? rowdata : 0;
        tick(dut);
    }
}

static void drive_steady_tail(Vpps41_display_pwm* dut, int windows, int on, uint16_t rowsel, uint16_t rowdata) {
    for (int w = 0; w < windows; w++) drive_window_tail(dut, on, rowsel, rowdata);
}

static int level(Vpps41_display_pwm* dut, int row, int col) {
    int cell = row * 11 + col;
    return (dut->levels[(cell * 2) / 32] >> ((cell * 2) % 32)) & 3;
}

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);

    // Sustained (steady-state) thresholds: once the alpha=1/8 smoothing
    // filter has converged from a cold start (many consecutive windows of
    // the same on-count), classification matches the original
    // single-window thresholds exactly -- smoothing only changes ramp
    // speed, not the eventual cutoffs. Fresh dut per case: this filter's
    // integer rounding has a fixed point exactly at a constant target when
    // approached from a cold start, but can briefly pin one step when an
    // already-converged value's target then drops by exactly 1 -- an edge
    // case that doesn't arise from a cold start and isn't what these cases
    // test.
    {
        Vpps41_display_pwm* dut = make_dut();
        drive_steady(dut, 80, BRIGHT_MIN, 1u << 2, 1u << 3);
        CHECK(level(dut, 2, 3) == 2);
        delete dut;
    }
    {
        Vpps41_display_pwm* dut = make_dut();
        drive_steady(dut, 80, DIM_MIN, 1u << 2, 1u << 3);
        CHECK(level(dut, 2, 3) == 1);
        delete dut;
    }
    {
        Vpps41_display_pwm* dut = make_dut();
        drive_steady(dut, 80, DIM_MIN - 1, 1u << 2, 1u << 3);
        CHECK(level(dut, 2, 3) == 0);
        delete dut;
    }
    {
        // BRIGHT_MIN - 1 sustained should still be dim (one tick short of bright)
        Vpps41_display_pwm* dut = make_dut();
        drive_steady(dut, 80, BRIGHT_MIN - 1, 1u << 2, 1u << 3);
        CHECK(level(dut, 2, 3) == 1);
        delete dut;
    }

    // Test: levels hold steady mid-window (not reset until next window boundary)
    {
        Vpps41_display_pwm* dut = make_dut();
        drive_steady(dut, 3, WINDOW, 1u << 0, 1u << 0);  // converge to fully lit
        CHECK(level(dut, 0, 0) == 2);                    // should be bright
        for (int i = 0; i < WINDOW / 2; i++) {
            dut->rowsel = 0;
            dut->rowdata = 0;
            tick(dut);
        }
        CHECK(level(dut, 0, 0) == 2);  // still bright mid-window, hasn't reset yet

        // Test: window_tick fires exactly once per window
        int window_ticks = 0;
        for (int i = 0; i < WINDOW * 3; i++) {
            dut->rowsel = 0;
            dut->rowdata = 0;
            tick(dut);
            if (dut->window_tick) window_ticks++;
        }
        CHECK(window_ticks == 3);
        delete dut;
    }

    // Test: no coincidence, no accumulation (row and col bits alternate, never both set)
    {
        Vpps41_display_pwm* dut = make_dut();
        for (int w = 0; w < 3; w++) {
            for (int i = 0; i < WINDOW; i++) {
                bool odd = i & 1;
                dut->rowsel = odd ? (1u << 4) : 0;
                dut->rowdata = odd ? 0 : (1u << 6);
                tick(dut);
            }
        }
        CHECK(level(dut, 4, 6) == 0);  // should accumulate nothing
        delete dut;
    }

    // Test: BRIGHT_MIN on-cycles placed at the END of each window (through
    // and including the WIN_LAST boundary cycle), sustained across many
    // windows, must still converge to bright -- the boundary-cycle
    // classification must include that same cycle's own increment, not the
    // stale pre-increment count.
    {
        Vpps41_display_pwm* dut = make_dut();
        drive_steady_tail(dut, 80, BRIGHT_MIN, 1u << 5, 1u << 7);
        CHECK(level(dut, 5, 7) == 2);
        delete dut;
    }

    // Test: exactly DIM_MIN on-cycles at the end of each window, sustained,
    // must converge dim, not off -- same boundary-inclusion property at the
    // other threshold.
    {
        Vpps41_display_pwm* dut = make_dut();
        drive_steady_tail(dut, 80, DIM_MIN, 1u << 5, 1u << 7);
        CHECK(level(dut, 5, 7) == 1);
        delete dut;
    }

    // Test: the core anti-flicker property -- a cell that's been solidly
    // lit for a while does not snap to fully off after a single window's
    // worth of dropout, it decays gradually instead. This is what real
    // hardware testing showed was missing: classifying from raw per-window
    // cnt alone (no cross-window memory) renders any transient dip as a
    // visible on/off flash every frame.
    {
        Vpps41_display_pwm* dut = make_dut();
        drive_steady(dut, 80, WINDOW, 1u << 1, 1u << 9); // converge to fully lit
        CHECK(level(dut, 1, 9) == 2);

        drive_window(dut, 0, 1u << 1, 1u << 9); // one window of complete silence
        CHECK(level(dut, 1, 9) == 2);           // still bright, not blanked

        drive_steady(dut, 80, 0, 1u << 1, 1u << 9); // sustained silence
        CHECK(level(dut, 1, 9) == 0);               // eventually decays to off
        delete dut;
    }

    if (failures == 0) { std::printf("PASS\n"); return 0; }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
