// sim/pps41_display_mux_tb.cpp
#include "Vpps41_display_mux.h"
#include "verilated.h"
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Vpps41_display_mux* dut = new Vpps41_display_mux;

    dut->d = 0xFFF; dut->r = 0x000; dut->eval();
    CHECK(dut->rowsel == 0x3FF);

    dut->d = 0x000; dut->r = 0x07F; dut->eval();
    CHECK(dut->rowdata == 0x07F);

    dut->d = 0x000; dut->r = 0x380; dut->eval();
    CHECK(dut->rowdata == 0x700);

    dut->d = 0x800; dut->r = 0x000; dut->eval();
    CHECK((dut->rowdata & 0x80) == 0x80);
    dut->d = 0x000; dut->r = 0x3FF; dut->eval();
    CHECK((dut->rowdata & 0x80) == 0x00);

    dut->d = 0x400; dut->r = 0x000; dut->eval();
    CHECK(dut->rowsel == 0x000);
    CHECK(dut->rowdata == 0x000);

    delete dut;
    if (failures == 0) { std::printf("PASS\n"); return 0; }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
