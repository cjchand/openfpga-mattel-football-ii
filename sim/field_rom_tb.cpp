// Spot-checks field_rom against known regions: green background, an
// endzone, a black field cell, and the two hash-tick rows -- coordinates
// chosen safely inside each region's flat-color area (not on a
// divider/border edge), so exact color is unambiguous regardless of
// quantization.
//
// Geometry here must track tools/gen_bezel_bitmaps.py:
//   MARGIN_X=7, EZ_W=23, FIELD_X0=30, COL_PITCH=34, DIV_W=2, BORDER_W=4
//   strip is 108px tall, occupying field_y 4-111 of a 116-row bitmap
//   hash rows at strip-thirds -> field_y 40 and 76
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
static int R(uint32_t c) { return (c >> 16) & 0xFF; }
static int G(uint32_t c) { return (c >> 8) & 0xFF; }
static int B(uint32_t c) { return c & 0xFF; }

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Vfield_rom d;

    // Green background, in the left margin (x=1, left of the border which
    // starts at MARGIN_X-BORDER_W = 3). FB1's green is (14,138,3), so this
    // asserts green dominance rather than a high absolute level.
    uint32_t green = sample(d, 1, 60);
    CHECK(G(green) > 100, "green background has a strong green channel");
    CHECK(G(green) > 3 * R(green), "green background is dominated by green, not red");
    CHECK(G(green) > 3 * B(green), "green background is dominated by green, not blue");

    // Endzone spans x=7..29; x=15 is safely mid-endzone. It must be BLUE
    // (13,98,188) and not cyan -- blue clearly above green is the check
    // that would have caught the cyan endzone this core shipped with.
    uint32_t endzone = sample(d, 15, 60);
    CHECK(B(endzone) > 150, "endzone has a strong blue channel");
    CHECK(B(endzone) > G(endzone) + 50, "endzone is blue, not cyan");

    // Field cell 0 spans x=32..63 (next divider at FIELD_X0+COL_PITCH=64).
    uint32_t cell = sample(d, 40, 60);
    CHECK(cell < 0x202020, "field cell is near-black");

    // The strip is 108px tall plus a 4px border, so field_y 4..111 is field
    // art. Check near the bottom that cells and endzones run the full height.
    CHECK(sample(d, 40, 108) < 0x202020, "field cell is still near-black near the bottom of the strip");
    CHECK(B(sample(d, 15, 108)) > 150, "endzone runs the full height of the strip");

    // Two hash-tick rows, at field_y 40 and 76, straddling the internal
    // divider at x=64 (ticks reach 4px either side, so x=61 is on a tick).
    // A single-row field -- which is what this core shipped with -- fails
    // the second of these.
    uint32_t hash_top = sample(d, 61, 40);
    uint32_t hash_bot = sample(d, 61, 76);
    CHECK(R(hash_top) > 150 && G(hash_top) > 150 && B(hash_top) > 150,
          "upper hash tick is light-coloured");
    CHECK(R(hash_bot) > 150 && G(hash_bot) > 150 && B(hash_bot) > 150,
          "lower hash tick is light-coloured (two hash rows, as on FB1)");
    // Between the two rows, the same column is plain black field.
    CHECK(sample(d, 61, 58) < 0x202020, "no hash tick between the two rows");

    // Goal lines carry half-length ticks that reach ONLY into the black
    // playfield, never into the endzone. Left goal line is the divider at
    // FIELD_X0=30, with the endzone to its left; right goal line is at
    // FIELD_X0+COL_PITCH*10 = 370, with the endzone to its right.
    // HASH_REACH is 4, DIV_W is 2.
    for (int hy : {40, 76}) {
        // Left goal line: tick present on the black side (x just right of
        // the divider), absent on the endzone side.
        uint32_t left_in  = sample(d, 33, hy); // 30..31 divider, 32..35 reach
        uint32_t left_out = sample(d, 27, hy); // inside the endzone
        CHECK(R(left_in) > 150 && G(left_in) > 150 && B(left_in) > 150,
              "left goal line tick reaches into the black playfield");
        CHECK(B(left_out) > 150 && B(left_out) > G(left_out) + 50,
              "left goal line tick does NOT reach into the endzone");

        // Right goal line: mirror image.
        uint32_t right_in  = sample(d, 368, hy); // 366..369 reach, 370..371 divider
        uint32_t right_out = sample(d, 376, hy); // inside the endzone
        CHECK(R(right_in) > 150 && G(right_in) > 150 && B(right_in) > 150,
              "right goal line tick reaches into the black playfield");
        CHECK(B(right_out) > 150 && B(right_out) > G(right_out) + 50,
              "right goal line tick does NOT reach into the endzone");
    }

    if (g_failures) { std::printf("FAILED: %d check(s)\n", g_failures); return 1; }
    std::printf("PASS: field_rom_tb\n");
    return 0;
}
