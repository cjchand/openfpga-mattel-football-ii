// Spot-checks field_rom against known regions: green background, an
// endzone, and a black field cell -- coordinates chosen safely inside
// each region's flat-color area (not on a divider/border edge), so exact
// color is unambiguous regardless of quantization.
#include "Vfield_rom.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>

static int g_failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::printf("FAIL: %s (line %d)\n", msg, __LINE__); \
                   g_failures++; } } while (0)

static uint32_t sample(Vfield_rom& d, int x, int field_y) {
    d.x = x; d.field_y = field_y;
    d.eval();
    return d.rgb;
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Vfield_rom d;

    // Green background, in the left margin (x=1, left of the border which starts at x=2).
    uint32_t green = sample(d, 1, 60);
    CHECK(((green >> 8) & 0xFF) > 150, "green background has a strong green channel");
    CHECK(((green >> 16) & 0xFF) < 100, "green background has a low red channel");

    // Endzone: x=12 is inside MARGIN_X(6)+EZ_W(19) = x6-24, safely mid-endzone.
    uint32_t endzone = sample(d, 12, 60);
    CHECK((endzone & 0xFF) > 150, "endzone has a strong blue channel");

    // Field cell 0: just past FIELD_X0(25)+DIV_W(2), safely inside the
    // first black cell before the next divider at 25+35=60.
    uint32_t cell = sample(d, 30, 60);
    CHECK(cell < 0x202020, "field cell is near-black");

    // The strip is 180px tall (plus 4px border top/bottom), so field_y
    // near the bottom of the bitmap is still field art, not out of
    // range: x=30 stays inside cell 0 all the way down.
    uint32_t deep_cell = sample(d, 30, 170);
    CHECK(deep_cell < 0x202020, "field cell is still near-black near the bottom of the 180px strip");
    uint32_t deep_endzone = sample(d, 12, 170);
    CHECK((deep_endzone & 0xFF) > 150, "endzone runs the full height of the 180px strip");

    if (g_failures) { std::printf("FAILED: %d check(s)\n", g_failures); return 1; }
    std::printf("PASS: field_rom_tb\n");
    return 0;
}
