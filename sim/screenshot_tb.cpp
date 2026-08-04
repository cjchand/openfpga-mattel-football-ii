// sim/screenshot_tb.cpp
//
// Runs the real CPU/display pipeline (pps41_core -> pps41_display_mux ->
// pps41_display_pwm -> video_renderer) for a fixed number of cycles against
// a real ROM image, then sweeps the full 400x360 active video region
// through video_renderer and dumps the result as a binary PPM. This is a
// debugging/verification aid (e.g. "does the score digit actually light up
// after a kick"), not a correctness test -- see pps41_core_tb.cpp and
// video_renderer_tb.cpp for those.
#include "Vpps41_core.h"
#include "Vpps41_display_mux.h"
#include "Vpps41_display_pwm.h"
#include "Vvideo_renderer.h"
#include "verilated.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <vector>

static void tick(Vpps41_core* dut) {
    dut->clk = 0; dut->eval();
    dut->clk = 1; dut->eval();
}

static std::map<long, uint8_t> load_stimulus(const char* path) {
    std::map<long, uint8_t> events;
    if (!path) return events;
    std::ifstream f(path);
    long cycle; unsigned val;
    while (f >> cycle >> std::hex >> val) events[cycle] = static_cast<uint8_t>(val);
    return events;
}

int main(int argc, char** argv) {
    if (argc != 4 && argc != 5) {
        std::fprintf(stderr, "usage: %s <rom-file> <cycle-count> <out.ppm> [stimulus-file]\n", argv[0]);
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
    const char* out_path = argv[3];
    auto stimulus = load_stimulus(argc == 5 ? argv[4] : nullptr);
    uint8_t current_p = 0x00;

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
    dpwm->clk = 1; dpwm->eval();  // clock the reset in, don't rely on zero-init
    dpwm->rst_n = 1;

    std::set<int> visited_pc;
    bool unimpl_ever = false;
    std::set<uint32_t> r_values, d_values;
    for (long i = 0; i < cycles; i++) {
        auto it = stimulus.find(i);
        if (it != stimulus.end()) current_p = it->second;
        dut->p_input = current_p;

        uint16_t addr = dut->rom_addr;
        addr &= 0x7FF;
        if (addr >= 0x600) addr -= 0x200;
        dut->rom_data = (addr < rom.size()) ? rom[addr] : 0;
        tick(dut);

        visited_pc.insert(dut->pc);
        if (dut->unimpl_hit_out) unimpl_ever = true;
        r_values.insert(dut->r_output_out);
        d_values.insert(dut->d_output_out);

        dmux->d = dut->d_output_out;
        dmux->r = dut->r_output_out;
        dmux->eval();
        dpwm->rowsel = dmux->rowsel;
        dpwm->rowdata = dmux->rowdata;
        dpwm->clk = 0; dpwm->eval();
        dpwm->clk = 1; dpwm->eval();
    }
    std::fprintf(stderr, "debug: distinct PCs visited=%zu (last=%03x), unimpl_ever=%d, distinct r_output=%zu, distinct d_output=%zu\n",
                 visited_pc.size(), dut->pc, unimpl_ever ? 1 : 0, r_values.size(), d_values.size());

    // Raw cell dump on stderr: independent of video_renderer's screen
    // layout/mapping, useful for checking which levels[] cells are
    // actually lit without relying on the renderer being right.
    for (int row = 0; row < 10; row++) {
        for (int col = 0; col < 11; col++) {
            int cell = row * 11 + col;
            int lvl = (dpwm->levels[(cell * 2) / 32] >> ((cell * 2) % 32)) & 3;
            if (lvl) std::fprintf(stderr, "cell row=%d col=%d level=%d\n", row, col, lvl);
        }
    }

    Vvideo_renderer* render = new Vvideo_renderer;
    for (int w = 0; w < 7; w++) render->levels[w] = dpwm->levels[w];
    render->bezel_enable = 1;

    static const int WIDTH = 400, HEIGHT = 360;
    std::vector<unsigned char> pixels(static_cast<size_t>(WIDTH) * HEIGHT * 3);
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            render->x = x;
            render->y = y;
            render->eval();
            size_t idx = (static_cast<size_t>(y) * WIDTH + x) * 3;
            pixels[idx + 0] = (render->rgb >> 16) & 0xFF;
            pixels[idx + 1] = (render->rgb >> 8) & 0xFF;
            pixels[idx + 2] = render->rgb & 0xFF;
        }
    }

    FILE* out = std::fopen(out_path, "wb");
    if (!out) { std::fprintf(stderr, "cannot open %s for writing\n", out_path); return 2; }
    std::fprintf(out, "P6\n%d %d\n255\n", WIDTH, HEIGHT);
    std::fwrite(pixels.data(), 1, pixels.size(), out);
    std::fclose(out);

    std::printf("wrote %s (%d cycles run)\n", out_path, static_cast<int>(cycles));

    delete dut; delete dmux; delete dpwm; delete render;
    return 0;
}
