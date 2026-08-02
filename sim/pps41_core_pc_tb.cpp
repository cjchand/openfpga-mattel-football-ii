// sim/pps41_core_pc_tb.cpp
#include "Vpps41_core.h"
#include "verilated.h"
#include <cstdio>
#include <vector>

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

static uint16_t lfsr_increment(uint16_t pc) {
    int feed = ((pc & 0x3e) == 0) ? 1 : 0;
    feed ^= (pc >> 1 ^ pc) & 1;
    return static_cast<uint16_t>((pc & ~0x3fu) | (pc >> 1 & 0x1f) | (feed << 5));
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Vpps41_core* dut = new Vpps41_core;

    dut->rst_n = 0; dut->rom_data = 0x00; dut->eval();
    dut->clk = 0; dut->eval();
    dut->clk = 1; dut->eval();
    dut->rst_n = 1;

    uint16_t expect_pc = 0;
    CHECK(dut->rom_addr == expect_pc);

    for (int i = 0; i < 70; i++) {
        dut->rom_data = 0x00; // NOP, so PC just advances every cycle
        dut->clk = 0; dut->eval();
        dut->clk = 1; dut->eval();
        expect_pc = lfsr_increment(expect_pc);
        CHECK(dut->rom_addr == expect_pc);
    }

    delete dut;
    if (failures == 0) { std::printf("PASS\n"); return 0; }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
