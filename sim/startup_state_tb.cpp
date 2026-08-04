// sim/startup_state_tb.cpp
//
// Regression check for the real, documented startup display state: with
// no button but Score held (pressed slightly after reset, not at cycle 0
// -- see below), the scoreboard reads Home 00 / Time Remaining 15.0 /
// Visitor 00. Confirmed against a real MAME 0.288 mfootb2 run (output
// values for digit0/1/2/6/7/8/9 settle to this exact pattern by video
// frame 3 and stay there) after fixing the CPU's reset PC (was 0x000,
// real chip resets to 0x3C0 -- see pps41_core.v).
//
// Score is held starting at SETTLE_CYCLES rather than cycle 0: MAME only
// samples digital ioport fields once per emulated video frame, so a field
// "held from machine start" via a Lua autoboot script doesn't actually
// read as pressed until the first ~1/60s frame boundary elapses (this is
// a MAME ioport-polling detail, not real hardware behavior -- real
// silicon reads P-input live every cycle, same as this project's RTL).
// Holding Score from cycle 0 produces a different, non-matching digit
// pattern, consistent with this timing sensitivity.
#include "Vpps41_core.h"
#include "Vpps41_display_mux.h"
#include "Vpps41_display_pwm.h"
#include "verilated.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

static void tick(Vpps41_core* dut) {
    dut->clk = 0; dut->eval();
    dut->clk = 1; dut->eval();
}

static const long SETTLE_CYCLES = 6333; // ~1 video frame at 380kHz/60Hz
static const long RUN_CYCLES = 20000;

// Segment bits: bit0=a(top) 1=b(upper-right) 2=c(lower-right) 3=d(bottom)
// 4=e(lower-left) 5=f(upper-left) 6=g(middle) 7=dp -- MAME's led7seg
// convention (see src/video_renderer.v's header comment).
static const int SEG_0  = 0x3F; // a,b,c,d,e,f
static const int SEG_1  = 0x06; // b,c
static const int SEG_5D = 0xED; // a,c,d,f,g,dp

struct Expect { int row; int pattern; const char* label; };
static const Expect kExpected[] = {
    {8, SEG_0,  "Home tens"},
    {9, SEG_0,  "Home ones"},
    {0, SEG_1,  "Time tens-of-minutes"},
    {1, SEG_5D, "Time minutes (with decimal point)"},
    {2, SEG_0,  "Time seconds-ish digit"},
    {6, SEG_0,  "Visitor tens"},
    {7, SEG_0,  "Visitor ones"},
};

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <rom-file>\n", argv[0]);
        return 2;
    }
    FILE* f = std::fopen(argv[1], "rb");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> rom(static_cast<size_t>(size));
    if (std::fread(rom.data(), 1, rom.size(), f) != rom.size()) { std::fclose(f); return 2; }
    std::fclose(f);

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
    dpwm->clk = 1; dpwm->eval();
    dpwm->rst_n = 1;

    for (long i = 0; i < RUN_CYCLES; i++) {
        dut->p_input = (i < SETTLE_CYCLES) ? 0x00 : 0x01; // bit0 = Score

        uint16_t addr = dut->rom_addr;
        addr &= 0x7FF;
        if (addr >= 0x600) addr -= 0x200;
        dut->rom_data = (addr < rom.size()) ? rom[addr] : 0;
        tick(dut);

        dmux->d = dut->d_output_out;
        dmux->r = dut->r_output_out;
        dmux->eval();
        dpwm->rowsel = dmux->rowsel;
        dpwm->rowdata = dmux->rowdata;
        dpwm->clk = 0; dpwm->eval();
        dpwm->clk = 1; dpwm->eval();
    }

    int failures = 0;
    for (const auto& e : kExpected) {
        int actual = 0;
        // Column 7 is a shared line that reads lit on every row regardless
        // of that row's own content (see docs/initial-plan.md's segmask
        // notes) -- only row 1 actually wires it to a real segment (the
        // decimal point), so only include it there.
        int num_cols = (e.row == 1) ? 8 : 7;
        for (int col = 0; col < num_cols; col++) {
            int cell = e.row * 11 + col;
            int lvl = (dpwm->levels[(cell * 2) / 32] >> ((cell * 2) % 32)) & 3;
            if (lvl) actual |= (1 << col);
        }
        if (actual != e.pattern) {
            std::printf("FAIL row=%d (%s): expected segments 0x%02X, got 0x%02X\n",
                         e.row, e.label, e.pattern, actual);
            failures++;
        }
    }

    delete dut; delete dmux; delete dpwm;
    if (failures) { std::printf("FAILED: %d digit(s) mismatched\n", failures); return 1; }
    std::printf("PASS: startup_state_tb (Home 00 / Time 15.0 / Visitor 00)\n");
    return 0;
}
