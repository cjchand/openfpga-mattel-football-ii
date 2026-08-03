#include "Vpps41_tone.h"
#include "verilated.h"
#include <cstdio>

static void tick(Vpps41_tone* dut) {
    dut->clk = 0; dut->eval();
    dut->clk = 1; dut->eval();
}

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Vpps41_tone* dut = new Vpps41_tone;
    dut->rst_n = 0; dut->ios_fire = 0; dut->int0h_fire = 0; dut->cycle_en = 0; dut->ios_a = 0;
    tick(dut);
    dut->rst_n = 1;

    CHECK(dut->tone_on_out == 0);
    CHECK(dut->spk_output_out == 2);
    CHECK(dut->ios_state_out == 0);

    dut->ios_fire = 1; dut->cycle_en = 1; dut->ios_a = 0x5;
    tick(dut);
    CHECK(dut->tone_on_out == 0);
    CHECK(dut->ios_state_out == 1);

    dut->ios_a = 0x3;
    tick(dut);
    CHECK(dut->tone_on_out == 1);
    CHECK(dut->ios_state_out == 2);
    CHECK(dut->tone_freq_out == 0x35);

    dut->ios_fire = 0;

    dut->cycle_en = 1;
    for (int i = 0; i < 0x35 - 1; i++) tick(dut);
    CHECK(dut->spk_output_out == 1);

    delete dut;
    if (failures == 0) { std::printf("PASS\n"); return 0; }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
