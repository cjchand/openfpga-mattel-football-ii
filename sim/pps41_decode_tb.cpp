// sim/pps41_decode_tb.cpp
#include "Vpps41_decode.h"
#include "verilated.h"
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Vpps41_decode* dut = new Vpps41_decode;

    dut->op = 0x30; dut->prev_op = 0x00; dut->prev2_op = 0x00; dut->eval();
    CHECK(dut->is_tr == 1);

    dut->op = 0xC5; dut->prev_op = 0x30; dut->prev2_op = 0x00; dut->eval();
    CHECK(dut->prev_is_tr == 1);
    CHECK(dut->op_hi == 0xC);
    CHECK(dut->op_lo6 == 0x05);

    dut->op = 0x21; dut->prev_op = 0x00; dut->prev2_op = 0x00; dut->eval();
    CHECK(dut->op_fc == 0x20);
    CHECK(dut->op_lo2 == 0x1);

    dut->op = 0x47; dut->prev_op = 0x00; dut->prev2_op = 0x00; dut->eval();
    CHECK(dut->op_lo4 == 0x7);

    delete dut;
    if (failures == 0) { std::printf("PASS\n"); return 0; }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
