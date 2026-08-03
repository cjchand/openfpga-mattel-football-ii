// sim/pps41_core_ram_tb.cpp
#include "Vpps41_core.h"
#include "verilated.h"
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

static void tick(Vpps41_core* dut) {
    dut->clk = 0; dut->eval();
    dut->clk = 1; dut->eval();
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Vpps41_core* dut = new Vpps41_core;

    dut->rst_n = 0; dut->rom_data = 0; dut->p_input = 0; dut->dbg_b_set = 0; dut->dbg_sag_set = 0; dut->dbg_ram_wr = 0; dut->ce = 1; tick(dut);
    dut->rst_n = 1;

    // Bank A mirror: write 0x3 at 0x40, expect it readable at 0x48 and 0x58, not at 0x50.
    dut->dbg_b_set = 1; dut->dbg_b_val = 0x40; tick(dut);
    dut->dbg_b_set = 0;
    dut->dbg_ram_wr = 1; dut->dbg_ram_wdata = 0x3; tick(dut);
    dut->dbg_ram_wr = 0;

    dut->dbg_b_set = 1; dut->dbg_b_val = 0x48; tick(dut);
    dut->dbg_b_set = 0; dut->eval();
    CHECK(dut->ram_rdata == 0x3);

    dut->dbg_b_set = 1; dut->dbg_b_val = 0x58; tick(dut);
    dut->dbg_b_set = 0; dut->eval();
    CHECK(dut->ram_rdata == 0x3);

    dut->dbg_b_set = 1; dut->dbg_b_val = 0x50; tick(dut);
    dut->dbg_b_set = 0; dut->eval();
    CHECK(dut->ram_rdata == 0xF); // reset value -- independent bank, not the mirror

    // SAG: force upper bits to 3 for exactly one cycle.
    dut->dbg_b_set = 1; dut->dbg_b_val = 0x05; tick(dut); // Bu would normally be 0
    dut->dbg_b_set = 0;
    dut->dbg_sag_set = 1; tick(dut); // pulse SAG
    dut->dbg_sag_set = 0;
    CHECK(dut->ram_addr == 0x35); // SAG cycle: Bu forced to 3, Bl stays 5
    tick(dut);
    CHECK(dut->ram_addr == 0x05); // one cycle later: SAG has expired

    delete dut;
    if (failures == 0) { std::printf("PASS\n"); return 0; }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
