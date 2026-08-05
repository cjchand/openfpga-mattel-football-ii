// sim/pps41_core_tb.cpp
#include "Vpps41_core.h"
#include "Vpps41_core___024root.h"
#include "Vpps41_display_mux.h"
#include "Vpps41_display_pwm.h"
#include "Vpps41_display_pwm___024root.h"
#include "verilated.h"
#include "golden/mm77la_model.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>
#include "stimulus.h"

static void tick(Vpps41_core* dut) {
    dut->clk = 0; dut->eval();
    dut->clk = 1; dut->eval();
}

// Advances the core by exactly one instruction.
//
// With ce_period == 1 this is a plain tick with ce held high, which is how
// this testbench ran for its whole life -- and that is precisely the problem
// it was blind to. On the real device ce_gen pulses ce once every ~129.35
// core clocks, so `op` sits stable on the ROM bus for ~129 clocks while the
// instruction's combinational "fire" strobes stay asserted the whole time.
// Any downstream register that acts on a fire signal without qualifying it
// with ce therefore applies that instruction ~129 times instead of once.
// pps41_tone did exactly that: IOS shifted tone_freq and advanced ios_state
// on every one of those clocks. At ce=1 the two are indistinguishable, which
// is why 1.2M cycles of ROM lockstep passed while the hardware produced
// wrong tone pitches and, when ios_state landed out of phase with the ROM's
// own RAM-bit mirror of it, a tone that never stopped.
//
// ce_period alternates 129/130 to reproduce ce_gen's fractional accumulator,
// so a bug whose effect depends on (period mod 3) -- as this one's did --
// cannot hide behind an exact divider.
static void advance(Vpps41_core* dut, int ce_period, long instr) {
    if (ce_period <= 1) { dut->ce = 1; tick(dut); return; }
    int period = ce_period + (instr % 3 == 0 ? 1 : 0);
    dut->ce = 0;
    for (int k = 0; k < period - 1; k++) tick(dut);
    dut->ce = 1;
    tick(dut);
}

static uint8_t ram_phys_index(uint8_t addr) {
    addr &= 0x7F;
    if (addr < 0x40) return addr;
    if (addr <= 0x4F || (addr >= 0x58 && addr <= 0x5F)) return 64 + (addr & 0x07);
    if (addr <= 0x57) return 72 + (addr & 0x07);
    if (addr <= 0x6F || (addr >= 0x78 && addr <= 0x7F)) return 80 + (addr & 0x07);
    return 88 + (addr & 0x07);
}

// load_stimulus lives in stimulus.h -- see there for why.

int main(int argc, char** argv) {
    // Positional args, plus an optional --ce-period=N anywhere after them.
    std::vector<char*> pos;
    int ce_period = 1;
    for (int i = 1; i < argc; i++) {
        if (std::strncmp(argv[i], "--ce-period=", 12) == 0)
            ce_period = std::atoi(argv[i] + 12);
        else if (std::strncmp(argv[i], "--", 2) != 0)
            pos.push_back(argv[i]);
    }
    if (pos.size() != 2 && pos.size() != 3) {
        std::fprintf(stderr,
            "usage: %s <rom-file> <cycle-count> [stimulus-file] [--ce-period=N]\n", argv[0]);
        return 2;
    }
    Verilated::commandArgs(argc, argv);

    FILE* f = std::fopen(pos[0], "rb");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", pos[0]); return 2; }
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> rom(static_cast<size_t>(size));
    if (std::fread(rom.data(), 1, rom.size(), f) != rom.size()) { std::fclose(f); return 2; }
    std::fclose(f);

    long cycles = std::strtol(pos[1], nullptr, 10);
    auto stimulus = load_stimulus(pos.size() == 3 ? pos[2] : nullptr);
    uint8_t current_p = 0x00;

    Mm77laModel golden(rom.data(), rom.size());
    golden.reset();

    Vpps41_core* dut = new Vpps41_core;
    dut->rst_n = 0; dut->dbg_b_set = 0; dut->dbg_sag_set = 0; dut->dbg_ram_wr = 0; dut->p_input = 0; dut->d_input = 0;
    dut->ce = 1;
    dut->rom_data = rom.empty() ? 0 : rom[0];
    tick(dut);
    dut->rst_n = 1;

    Vpps41_display_mux* dmux = new Vpps41_display_mux;
    Vpps41_display_pwm* dpwm = new Vpps41_display_pwm;
    dpwm->rst_n = 0; dpwm->rowsel = 0; dpwm->rowdata = 0;
    dpwm->ce = 1;
    dpwm->clk = 0; dpwm->eval();
    dpwm->clk = 1; dpwm->eval();  // actually clock the reset, don't rely on Verilator's zero-init
    dpwm->rst_n = 1;

    long ix_hit_count = 0;
    bool int1l_ever_hit = false;
    bool unimpl_ever_hit = false;

    for (long i = 0; i < cycles; i++) {
        auto it = stimulus.find(i);
        if (it != stimulus.end()) current_p = it->second;
        dut->p_input = current_p;
        golden.debug_set_p(current_p);

        uint16_t addr = dut->rom_addr;
        addr &= 0x7FF;
        if (addr >= 0x600) addr -= 0x200;
        dut->rom_data = (addr < rom.size()) ? rom[addr] : 0;
        advance(dut, ce_period, i);

        dmux->d = dut->d_output_out;
        dmux->r = dut->r_output_out;
        dmux->eval();
        dpwm->rowsel = dmux->rowsel;
        dpwm->rowdata = dmux->rowdata;
        dpwm->clk = 0; dpwm->eval();
        dpwm->clk = 1; dpwm->eval();

        golden.step();
        const auto& g = golden.state();

        if (g.ix_executed) ix_hit_count++;
        if (g.int1l_hit) int1l_ever_hit = true;
        if (g.unimpl_hit) unimpl_ever_hit = true;

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
        if (dut->r_output_out != g.io.r_output) { std::printf("cycle %ld: r_output mismatch rtl=%03x golden=%03x\n", i, dut->r_output_out, g.io.r_output); mismatch = true; }
        if (dut->d_output_out != g.io.d_output) { std::printf("cycle %ld: d_output mismatch rtl=%03x golden=%03x\n", i, dut->d_output_out, g.io.d_output); mismatch = true; }
        if (dut->tone_on_result != (g.tone.tone_on ? 1 : 0)) { std::printf("cycle %ld: tone_on mismatch rtl=%d golden=%d\n", i, dut->tone_on_result, g.tone.tone_on); mismatch = true; }
        if (dut->tone_freq_result != g.tone.tone_freq) { std::printf("cycle %ld: tone_freq mismatch rtl=%02x golden=%02x\n", i, dut->tone_freq_result, g.tone.tone_freq); mismatch = true; }
        if (dut->spk_output_result != g.tone.spk_output) { std::printf("cycle %ld: spk_output mismatch rtl=%d golden=%d\n", i, dut->spk_output_result, g.tone.spk_output); mismatch = true; }
        if (dut->ios_state_result != g.tone.ios_state) { std::printf("cycle %ld: ios_state mismatch rtl=%d golden=%d\n", i, dut->ios_state_result, g.tone.ios_state); mismatch = true; }
        if (dut->unimpl_hit_out != (g.unimpl_hit ? 1 : 0)) { std::printf("cycle %ld: unimpl_hit mismatch rtl=%d golden=%d\n", i, dut->unimpl_hit_out, g.unimpl_hit); mismatch = true; }
        if (dut->x_out != g.x) { std::printf("cycle %ld: x mismatch rtl=%x golden=%x\n", i, dut->x_out, g.x); mismatch = true; }
        if (dut->s_out != g.s) { std::printf("cycle %ld: s mismatch rtl=%x golden=%x\n", i, dut->s_out, g.s); mismatch = true; }

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

        // Settled per-window levels, diffed every cycle (cheap: these only
        // change at window boundaries, but diffing every cycle catches a
        // stale/incorrectly-timed update just as well as a wrong value).
        for (int cell = 0; cell < 110; cell++) {
            int rtl_level = (dpwm->levels[(cell * 2) / 32] >> ((cell * 2) % 32)) & 3;
            int golden_level = g.display.levels[cell];
            if (rtl_level != golden_level) {
                std::printf("cycle %ld: display cell %d level mismatch rtl=%d golden=%d\n", i, cell, rtl_level, golden_level);
                mismatch = true;
            }
        }

        // Per-cell accumulator state, diffed every cycle too -- not just
        // the settled snapshot above. This is the "don't just check the
        // final answer" discipline Phase 1's RAM-comparison fix
        // established: the settled `levels` diff above is 0==0 for every
        // cell across an entire idle run where nothing ever lights up, so
        // it alone provides no verification signal for the accumulator's
        // internal correctness (this is exactly the gap that let the
        // window-boundary off-by-one bug go undetected -- see
        // pps41_display_pwm.v's WIN_LAST classification fix).
        for (int cell = 0; cell < 110 && !mismatch; cell++) {
            uint16_t rtl_cnt = dpwm->rootp->pps41_display_pwm__DOT__cnt[cell];
            uint16_t golden_cnt = g.display.cnt[cell];
            if (rtl_cnt != golden_cnt) {
                std::printf("cycle %ld: display cell %d cnt mismatch rtl=%d golden=%d\n", i, cell, rtl_cnt, golden_cnt);
                mismatch = true;
            }
        }

        if (mismatch) { delete dut; delete dmux; delete dpwm; return 1; }
    }

    if (ce_period > 1)
        std::printf("PASS: %ld cycles at ce-period ~%d, no mismatches\n", cycles, ce_period);
    else
        std::printf("PASS: %ld cycles, no mismatches\n", cycles);
    std::printf("INT1L observed: %s; IX executed: %ld time(s)\n",
                 int1l_ever_hit ? "yes" : "no", ix_hit_count);
    std::printf("Unimplemented opcode dispatched: %s\n", unimpl_ever_hit ? "yes" : "no");
    delete dut;
    delete dmux;
    delete dpwm;
    return 0;
}
