#include "Vpps41_display_pwm.h"
#include "verilated.h"
#include <cstdio>

static void tick(Vpps41_display_pwm* dut) {
    dut->clk = 0; dut->eval();
    dut->clk = 1; dut->eval();
}

static const int WINDOW = 1583, DIM_MIN = 24, BRIGHT_MIN = 317;

static void drive_window(Vpps41_display_pwm* dut, int on, uint16_t rowsel, uint16_t rowdata) {
    for (int i = 0; i < WINDOW; i++) {
        bool active = i < on;
        dut->rowsel = active ? rowsel : 0;
        dut->rowdata = active ? rowdata : 0;
        tick(dut);
    }
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
    Vpps41_display_pwm* dut = new Vpps41_display_pwm;
    dut->rst_n = 0; dut->rowsel = 0; dut->rowdata = 0;
    tick(dut);
    dut->rst_n = 1;

    drive_window(dut, BRIGHT_MIN, 1u << 2, 1u << 3);
    CHECK(level(dut, 2, 3) == 2);

    drive_window(dut, DIM_MIN, 1u << 2, 1u << 3);
    CHECK(level(dut, 2, 3) == 1);

    drive_window(dut, DIM_MIN - 1, 1u << 2, 1u << 3);
    CHECK(level(dut, 2, 3) == 0);

    // Test: BRIGHT_MIN - 1 should still be dim (one tick short of bright)
    drive_window(dut, BRIGHT_MIN - 1, 1u << 2, 1u << 3);
    CHECK(level(dut, 2, 3) == 1);

    // Test: levels hold steady mid-window (not reset until next window boundary)
    drive_window(dut, WINDOW, 1u << 0, 1u << 0);  // fully lit for whole window
    CHECK(level(dut, 0, 0) == 2);                 // should be bright
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

    // Test: no coincidence, no accumulation (row and col bits alternate, never both set)
    for (int i = 0; i < WINDOW; i++) {
        bool odd = i & 1;
        dut->rowsel = odd ? (1u << 4) : 0;
        dut->rowdata = odd ? 0 : (1u << 6);
        tick(dut);
    }
    CHECK(level(dut, 4, 6) == 0);  // should accumulate nothing

    // The tests above don't all run in exact multiples of WINDOW cycles
    // (the "levels hold steady mid-window" test deliberately advances only
    // WINDOW/2 cycles past its boundary), so window_pos may currently be
    // mid-window rather than freshly reset to 0. drive_window_tail below
    // depends on knowing exactly where WIN_LAST falls, so resync to a
    // window boundary first by idling until window_tick fires.
    while (!dut->window_tick) {
        dut->rowsel = 0;
        dut->rowdata = 0;
        tick(dut);
    }

    // Test: BRIGHT_MIN on-cycles placed at the END of the window (through
    // and including the WIN_LAST boundary cycle) must still classify as
    // bright -- the boundary-cycle classification must include that same
    // cycle's own increment, not the stale pre-increment count.
    drive_window_tail(dut, BRIGHT_MIN, 1u << 5, 1u << 7);
    CHECK(level(dut, 5, 7) == 2);

    // Test: exactly DIM_MIN on-cycles at the end of the window (boundary
    // cycle active) must land dim, not off -- same boundary-inclusion
    // property at the other threshold.
    drive_window_tail(dut, DIM_MIN, 1u << 5, 1u << 7);
    CHECK(level(dut, 5, 7) == 1);

    delete dut;
    if (failures == 0) { std::printf("PASS\n"); return 0; }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
