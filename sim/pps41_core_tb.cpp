// sim/pps41_core_tb.cpp
#include "Vpps41_core.h"
#include "Vpps41_core___024root.h" // Important #10: needed for dut->rootp->pps41_core__DOT__ram[] access
#include "verilated.h"
#include "golden/mm77la_model.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

static void tick(Vpps41_core* dut) {
    dut->clk = 0; dut->eval();
    dut->clk = 1; dut->eval();
}

// Maps the 7-bit RAM address space onto the 96 physically-real nibbles.
// Mirrors sim/golden/mm77la_model.cpp's ram_phys_index() / src/pps41_core.v's
// ram_phys_index() exactly (Important #10). Used here only to index into the
// RTL's raw internal `ram` array (accessed via Verilator's rootp escape
// hatch below), which is stored by physical index, not raw address.
static uint8_t ram_phys_index(uint8_t addr) {
    addr &= 0x7F;
    if (addr < 0x40) return addr;
    if (addr <= 0x4F || (addr >= 0x58 && addr <= 0x5F)) return 64 + (addr & 0x07);
    if (addr <= 0x57) return 72 + (addr & 0x07);
    if (addr <= 0x6F || (addr >= 0x78 && addr <= 0x7F)) return 80 + (addr & 0x07);
    return 88 + (addr & 0x07);
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

    long ix_hit_count = 0;
    bool int1l_ever_hit = false;

    for (long i = 0; i < cycles; i++) {
        // ROM is combinationally addressed: present this cycle's byte before the edge.
        uint16_t addr = dut->rom_addr;
        addr &= 0x7FF;
        if (addr >= 0x600) addr -= 0x200;
        dut->rom_data = (addr < rom.size()) ? rom[addr] : 0;
        tick(dut);

        golden.step();
        const auto& g = golden.state();

        if (g.ix_executed) ix_hit_count++; // Important #11: precise per-step flag, not post-hoc prev_op==0x72 inspection
        if (g.int1l_hit) int1l_ever_hit = true;

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

        // Important #10: full 96-nibble RAM content comparison every cycle,
        // not just the 9 scalar architectural fields above. Reads the RTL's
        // raw internal `ram` array (physical-index-addressed) via
        // Verilator's rootp escape hatch, and the golden model's RAM via its
        // existing debug_ram_read() accessor (which applies the identical
        // address-to-physical-index mapping internally). Sweeping all 128
        // raw 7-bit addresses (not just the 96 physically distinct ones)
        // also exercises that both models' mirror-aliasing agrees.
        for (int addr = 0; addr < 0x80 && !mismatch; addr++) {
            uint8_t idx = ram_phys_index(static_cast<uint8_t>(addr));
            uint8_t rtl_val = dut->rootp->pps41_core__DOT__ram[idx] & 0xF;
            uint8_t golden_val = golden.debug_ram_read(static_cast<uint8_t>(addr)) & 0xF;
            if (rtl_val != golden_val) {
                std::printf("cycle %ld: ram[addr=%02x,phys=%d] mismatch rtl=%x golden=%x\n",
                            i, addr, idx, rtl_val, golden_val);
                mismatch = true;
            }
        }

        if (mismatch) { delete dut; return 1; }
    }

    std::printf("PASS: %ld cycles, no mismatches\n", cycles);
    std::printf("INT1L observed: %s; IX executed: %ld time(s)\n",
                 int1l_ever_hit ? "yes" : "no", ix_hit_count);
    delete dut;
    return 0;
}
