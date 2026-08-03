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

    delete dut;
    if (failures == 0) { std::printf("PASS\n"); return 0; }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
