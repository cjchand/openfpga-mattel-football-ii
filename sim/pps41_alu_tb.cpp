// sim/pps41_alu_tb.cpp
#include "Vpps41_alu.h"
#include "verilated.h"
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Vpps41_alu* dut = new Vpps41_alu;

    dut->op_sel = 0; dut->a = 0x3; dut->mem = 0x2; dut->c_in = 0; dut->eval();
    CHECK(dut->result == 0x5);
    CHECK(dut->carry_out == 0);

    dut->op_sel = 1; dut->a = 0xF; dut->mem = 0x2; dut->c_in = 1; dut->eval(); // 0xF+0x2+1=0x12
    CHECK(dut->result == 0x2);
    CHECK(dut->carry_out == 1);

    dut->op_sel = 2; dut->a = 0x3; dut->eval();
    CHECK(dut->result == 0xC);

    dut->op_sel = 3; dut->a = 0x1; dut->aisk_imm = 0x2; dut->eval();
    CHECK(dut->result == 0x3);
    CHECK(dut->overflow == 0);

    dut->op_sel = 3; dut->a = 0xF; dut->aisk_imm = 0x2; dut->eval(); // 0xF+2=0x11 -> overflow
    CHECK(dut->overflow == 1);

    delete dut;
    if (failures == 0) { std::printf("PASS\n"); return 0; }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
