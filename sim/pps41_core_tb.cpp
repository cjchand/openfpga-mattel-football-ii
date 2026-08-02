// sim/pps41_core_tb.cpp
#include "Vpps41_core.h"
#include "verilated.h"
#include "golden/mm77la_model.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

static void tick(Vpps41_core* dut) {
    dut->clk = 0; dut->eval();
    dut->clk = 1; dut->eval();
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s <rom-file> <cycle-count>\n", argv[0]);
        return 2;
    }
    Verilated::commandArgs(argc, argv);

    FILE* f = std::fopen(argv[1], "rb");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> rom(static_cast<size_t>(size));
    if (std::fread(rom.data(), 1, rom.size(), f) != rom.size()) { std::fclose(f); return 2; }
    std::fclose(f);

    long cycles = std::strtol(argv[2], nullptr, 10);

    Mm77laModel golden(rom.data(), rom.size());
    golden.reset();

    Vpps41_core* dut = new Vpps41_core;
    dut->rst_n = 0; dut->dbg_b_set = 0; dut->dbg_sag_set = 0; dut->dbg_ram_wr = 0;
    dut->rom_data = rom.empty() ? 0 : rom[0];
    tick(dut);
    dut->rst_n = 1;

    for (long i = 0; i < cycles; i++) {
        // ROM is combinationally addressed: present this cycle's byte before the edge.
        uint16_t addr = dut->rom_addr;
        addr &= 0x7FF;
        if (addr >= 0x600) addr -= 0x200;
        dut->rom_data = (addr < rom.size()) ? rom[addr] : 0;
        tick(dut);

        golden.step();
        const auto& g = golden.state();

        bool mismatch = false;
        if (dut->pc != g.pc) { std::printf("cycle %ld: pc mismatch rtl=%03x golden=%03x\n", i, dut->pc, g.pc); mismatch = true; }
        if (dut->a_out != g.a) { std::printf("cycle %ld: a mismatch rtl=%x golden=%x\n", i, dut->a_out, g.a); mismatch = true; }
        if (dut->b_out != g.b) { std::printf("cycle %ld: b mismatch rtl=%02x golden=%02x\n", i, dut->b_out, g.b); mismatch = true; }
        if (dut->skip_out != (g.skip ? 1 : 0)) { std::printf("cycle %ld: skip mismatch rtl=%d golden=%d\n", i, dut->skip_out, g.skip); mismatch = true; }
        if (dut->c_out != (g.c ? 1 : 0)) { std::printf("cycle %ld: c mismatch rtl=%d golden=%d\n", i, dut->c_out, g.c); mismatch = true; }
        if (dut->stack0_out != g.stack[0]) { std::printf("cycle %ld: stack0 mismatch rtl=%03x golden=%03x\n", i, dut->stack0_out, g.stack[0]); mismatch = true; }
        if (dut->stack1_out != g.stack[1]) { std::printf("cycle %ld: stack1 mismatch rtl=%03x golden=%03x\n", i, dut->stack1_out, g.stack[1]); mismatch = true; }
        if (dut->skip_count_out != g.skip_count) { std::printf("cycle %ld: skip_count mismatch rtl=%x golden=%x\n", i, dut->skip_count_out, g.skip_count); mismatch = true; }
        if (dut->int1l_hit_out != (g.int1l_hit ? 1 : 0)) { std::printf("cycle %ld: int1l_hit mismatch rtl=%d golden=%d\n", i, dut->int1l_hit_out, g.int1l_hit); mismatch = true; }
        if (mismatch) { delete dut; return 1; }
    }

    std::printf("PASS: %ld cycles, no mismatches\n", cycles);
    delete dut;
    return 0;
}
