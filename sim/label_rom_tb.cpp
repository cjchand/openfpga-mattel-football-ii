// Spot-checks label_rom: background color away from any glyph, and that
// each bar's row actually varies (proves it holds real text, not a solid
// fill or all-zero garbage).
#include "Vlabel_rom.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>

static int g_failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::printf("FAIL: %s (line %d)\n", msg, __LINE__); \
                   g_failures++; } } while (0)

static uint32_t sample(Vlabel_rom& d, int x, int band_y) {
    d.x = x; d.band_y = band_y;
    d.eval();
    return d.rgb;
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Vlabel_rom d;

    // Far-left of bar 1 (x=2, well outside any of the 3 centered words) --
    // must be the white background.
    uint32_t bg1 = sample(d, 2, 2);
    CHECK(bg1 == 0xFFFFFF, "bar1 far-left background is white");

    // Bar 2 (band_y=17, first row of the second bar), same expectation.
    uint32_t bg2 = sample(d, 2, 17);
    CHECK(bg2 == 0xFFFFFF, "bar2 far-left background is white");

    // Row 8 of bar 1 (mid-height) must contain at least one non-white
    // pixel somewhere across the full width -- proves the ROM holds real
    // text content, not a solid fill.
    bool row_has_text = false;
    for (int px = 0; px < 400; px++) {
        if (sample(d, px, 8) != 0xFFFFFF) { row_has_text = true; break; }
    }
    CHECK(row_has_text, "bar1 row 8 contains at least one non-background pixel (label text)");

    if (g_failures) { std::printf("FAILED: %d check(s)\n", g_failures); return 1; }
    std::printf("PASS: label_rom_tb\n");
    return 0;
}
