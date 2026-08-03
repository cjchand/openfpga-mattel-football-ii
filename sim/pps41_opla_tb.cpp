#include "Vpps41_opla.h"
#include "verilated.h"
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Vpps41_opla* dut = new Vpps41_opla;
    dut->a = 0x0; dut->eval();
    CHECK(dut->r_out == 0x03F);
    dut->a = 0xF; dut->eval();
    CHECK(dut->r_out == 0x000);
    dut->a = 0x1; dut->eval();
    CHECK(dut->r_out == 0x006);
    dut->a = 0xC; dut->eval();
    CHECK(dut->r_out == 0x200);
    delete dut;
    if (failures == 0) { std::printf("PASS\n"); return 0; }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
