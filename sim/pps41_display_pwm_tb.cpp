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

    delete dut;
    if (failures == 0) { std::printf("PASS\n"); return 0; }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
