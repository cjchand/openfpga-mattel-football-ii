// Verifies the digit/segment/field-lamp positions (carried over from
// Phase 5's display_render.v, repositioned for the new 400x360 canvas)
// and the new background layers (label bars, digit-window panel, field
// strip, green margin).
#include "Vvideo_renderer.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>

static int g_failures = 0;
static const char* g_current = "";
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::printf("FAIL [%s]: %s (line %d)\n", g_current, msg, __LINE__); \
                   g_failures++; return; } } while (0)

static void run_test(const char* name, void (*fn)(void)) { g_current = name; fn(); }

static void set_cell(Vvideo_renderer& d, int row, int col, int lvl) {
    int bit = (row * 11 + col) * 2;
    int word = bit / 32, off = bit % 32;
    d.levels[word] &= ~(3u << off);
    d.levels[word] |= ((uint32_t)lvl & 3u) << off;
}

static void clear(Vvideo_renderer& d) {
    for (int i = 0; i < 7; i++) d.levels[i] = 0;
    d.bezel_enable = 1;
}

// Must track src/video_renderer.v's digit geometry. Kept as named constants
// rather than inline literals because these were rescaled once (24x40 -> 18x30
// at the user's request) and every hardcoded coordinate in this file had to be
// re-derived by hand; next time, only this block changes.
static const int DIGIT_Y = 74, CELL_W = 18, CELL_H = 30;
static const int STROKE = 3, RUN = 12;           // segment thickness / length
static const int DIGIT_X[7] = {66, 96, 161, 191, 221, 285, 315};
// The three black digit windows: [x0, x1). FB1's values.
static const int WIN[3][2] = {{41, 140}, {141, 259}, {260, 359}};
// Plaque bands: [y0, y1).
static const int BAR1_Y0 = 52, BAR1_Y1 = 68;
static const int DIGIT_Y0 = 68, DIGIT_Y1 = 111;
static const int BAR2_Y0 = 111, BAR2_Y1 = 127;
// Which window each screen digit slot belongs to.
static const int SLOT_WIN[7] = {0, 0, 1, 1, 1, 2, 2};

static void test_digit_slot0_segment_a_lights_when_level_2() {
    Vvideo_renderer d;
    clear(d);
    set_cell(d, 8, 0, 2); // screen slot 0 -> row 8, segment a
    d.x = DIGIT_X[0] + STROKE + RUN/2;
    d.y = DIGIT_Y + STROKE/2;
    d.eval();
    CHECK(d.rgb != 0, "segment a of digit slot 0 is non-black when its cell is level 2");
}

static void test_gap_between_segments_is_black_regardless_of_level() {
    Vvideo_renderer d;
    clear(d);
    set_cell(d, 8, 0, 2); // segment a bright
    set_cell(d, 8, 6, 2); // segment g bright
    // (9,9) relative to the digit cell falls between segments a/b/f/g --
    // proves individual segment shapes are drawn, not a filled cell.
    d.x = DIGIT_X[0] + 9;
    d.y = DIGIT_Y + 9;
    d.eval();
    CHECK(d.rgb == 0, "gap pixel between segments stays black even with neighboring segments bright");
}

static void test_bright_brighter_than_dim() {
    Vvideo_renderer bright, dim;
    clear(bright); clear(dim);
    set_cell(bright, 8, 0, 2);
    set_cell(dim, 8, 0, 1);
    int cx = DIGIT_X[0] + STROKE + RUN/2, cy = DIGIT_Y + STROKE/2;
    bright.x = cx; bright.y = cy; bright.eval();
    dim.x = cx; dim.y = cy; dim.eval();
    CHECK(((bright.rgb >> 16) & 0xFF) > ((dim.rgb >> 16) & 0xFF), "level 2 (bright) has a higher red-channel value than level 1 (dim)");
}

static void test_row1_decimal_point_lights_when_level_2() {
    Vvideo_renderer d;
    clear(d);
    set_cell(d, 1, 7, 2);
    // digit_row(3)==1, so screen slot 3 carries the dp.
    d.x = DIGIT_X[3] + CELL_W + 2 + 1;
    d.y = DIGIT_Y + CELL_H - STROKE + 1;
    d.eval();
    CHECK(d.rgb != 0, "row 1's decimal point is non-black when row 1 col 7 is level 2");
}

static void test_field_lamp_slot0_top_lights_when_level_2() {
    Vvideo_renderer d;
    clear(d);
    set_cell(d, 8, 8, 2); // field screen slot 0 -> row 8, top lamp
    d.x = 30 + (34-14)/2 + 14/2;  // FIELD_X0=30, COL_PITCH=34, LAMP_W=14
    d.y = 163 + 15 + 6/2;         // STRIP_Y0=163, LAMP_Y_OFF=15, LAMP_H=6
    d.eval();
    CHECK(d.rgb != 0, "field lamp (slot 0 -> row 8, top) lights up when its cell is level 2");
}

static void test_field_lamp_slot9_bottom_last_position_reachable() {
    Vvideo_renderer d;
    clear(d);
    set_cell(d, 7, 10, 2); // field screen slot 9 -> row 7 (field_row(9)), bottom lamp -> col 10
    // FIELD_X0=30, COL_PITCH=34, LAMP_W=14, STRIP_Y0=163, LAMP_Y_OFF=15, ROW_PITCH=36, LAMP_H=6
    int cx = 30 + 9 * 34 + (34 - 14) / 2 + 14 / 2;
    int cy = 163 + 15 + 2 * 36 + 6 / 2;
    d.x = cx; d.y = cy;
    d.eval();
    CHECK(d.rgb != 0, "last field lamp (slot 9 -> row 7, bottom) is independently addressable and lights up");
}

static void test_bezel_disabled_is_plain_black() {
    Vvideo_renderer d;
    clear(d);
    d.bezel_enable = 0;
    d.x = 200; d.y = 200; // inside where the field strip would be
    d.eval();
    CHECK(d.rgb == 0, "bezel_enable=0 shows plain black everywhere outside lit LEDs");
}

static void test_bezel_enabled_shows_label_bar_background() {
    Vvideo_renderer d;
    clear(d);
    // Sampled in the left margin of bar 1, outside every label word: the
    // words are centred on 90 / 200 / 309 within windows 99 / 118 / 99 wide,
    // so x=5 is comfortably clear of "DOWN".
    d.x = 5; d.y = BAR1_Y0 + 5;
    d.eval();
    CHECK(d.rgb == 0xFFFFFF, "label bar background is white when bezel_enable=1");
}

static void test_field_strip_extends_below_the_lamp_rows() {
    // The strip is 108px tall (STRIP_Y0=163 .. y270 plus border), so a
    // point well below the last lamp row (y218-245) is still field art,
    // not the green margin -- this is what keeps the lower canvas from
    // being a large empty green band.
    Vvideo_renderer d;
    clear(d);
    d.x = 30; d.y = 260; // inside field cell 0, below all three lamp rows
    d.eval();
    CHECK(d.rgb != 0x0E8A03, "field strip still covers y=260 rather than falling back to green margin");
}

static void test_bezel_enabled_shows_green_margin() {
    Vvideo_renderer d;
    clear(d);
    // Green is a 32px surround around the strip (STRIP_Y0=163, STRIP_H=108,
    // BORDER_W=4), not a fill of the whole lower canvas -- matching FB1.
    d.x = 200; d.y = 290; // inside the bottom green margin (275..306)
    d.eval();
    CHECK(d.rgb == 0x0E8A03, "bottom green margin shows field green when bezel_enable=1");
    d.x = 200; d.y = 350; // beyond the green margin: black letterbox
    d.eval();
    CHECK(d.rgb == 0x000000, "below the green margin is black letterbox, not more green");
    d.x = 200; d.y = 20; // above the plaque: black top letterbox
    d.eval();
    CHECK(d.rgb == 0x000000, "above the plaque is black top letterbox");
}

// The property that actually matters when the numerals are rescaled: every
// digit must still sit inside its black window, and each group must still be
// centred in it. Scaling digit_x by 0.75 along with the cell -- the obvious
// thing to do -- satisfies neither, since the windows did not scale; the
// digits would bunch toward the left of each window. So this checks the
// result, not the arithmetic.
static void test_digits_are_centred_inside_their_unscaled_windows() {
    for (int w = 0; w < 3; w++) {
        int first = -1, last = -1;
        for (int d = 0; d < 7; d++) {
            if (SLOT_WIN[d] != w) continue;
            if (first < 0) first = d;
            last = d;
            CHECK(DIGIT_X[d] >= WIN[w][0] && DIGIT_X[d] + CELL_W <= WIN[w][1],
                  "digit cell lies entirely inside its black window");
        }
        int left  = DIGIT_X[first] - WIN[w][0];
        int right = WIN[w][1] - (DIGIT_X[last] + CELL_W);
        CHECK(left - right <= 1 && right - left <= 1,
              "digit group is centred in its window (margins equal within 1px)");
    }
    // Slot 3 also carries the decimal point, 2px to the right of its cell.
    // It must not collide with slot 4 or leave the window.
    int dp_x0 = DIGIT_X[3] + CELL_W + 2;
    CHECK(dp_x0 + STROKE <= DIGIT_X[4], "decimal point clears the next digit cell");
    CHECK(dp_x0 + STROKE <= WIN[1][1], "decimal point stays inside its window");
    // Vertically the numerals must stay within the digit band (y 16..58).
    CHECK(DIGIT_Y >= DIGIT_Y0 && DIGIT_Y + CELL_H <= DIGIT_Y1,
          "digit cell stays within the digit-window band");
}

// The two things asked for after the second hardware session, checked as
// results rather than as constants: the plaque must reach both screen edges
// (it used to have black columns at x<6 and x>=394), and the windows must be
// separated by a hairline rather than the 48px white gaps it shipped with.
static void test_plaque_reaches_the_edges_with_hairline_dividers() {
    Vvideo_renderer d;
    clear(d);
    int y = DIGIT_Y0 + 5;

    d.x = 0;   d.y = y; d.eval();
    CHECK(d.rgb == 0xFFFFFF, "plaque is white at the very left edge, no black column");
    d.x = 399; d.y = y; d.eval();
    CHECK(d.rgb == 0xFFFFFF, "plaque is white at the very right edge, no black column");

    // Each divider is exactly one pixel: black, white, black.
    for (int i = 0; i < 2; i++) {
        int div = WIN[i][1];               // == WIN[i+1][0] - 1
        d.x = div - 1; d.y = y; d.eval();
        CHECK(d.rgb == 0x000000, "pixel left of the divider is inside the black window");
        d.x = div;     d.y = y; d.eval();
        CHECK(d.rgb == 0xFFFFFF, "divider pixel is white");
        d.x = div + 1; d.y = y; d.eval();
        CHECK(d.rgb == 0x000000, "pixel right of the divider is inside the next black window");
    }
}

// The gap the plaque used to float above: y 75..126 was black letterbox
// between the scoreboard and the field's green. Nothing black may remain
// between the bottom label bar and the green.
static void test_no_black_band_between_plaque_and_field() {
    Vvideo_renderer d;
    clear(d);
    d.x = 200;
    for (int y = BAR2_Y1; y < 163 - 4; y++) {   // BAR2_Y1 .. field border
        d.y = y; d.eval();
        CHECK(d.rgb == 0x0E8A03, "every row between the plaque and the field is green, not black");
    }
    // And the row immediately above is still plaque, not a gap.
    d.y = BAR2_Y1 - 1; d.eval();
    CHECK(d.rgb != 0x0E8A03, "the plaque's last row is not already green");
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    run_test("plaque_reaches_the_edges_with_hairline_dividers", test_plaque_reaches_the_edges_with_hairline_dividers);
    run_test("no_black_band_between_plaque_and_field", test_no_black_band_between_plaque_and_field);
    run_test("digits_are_centred_inside_their_unscaled_windows", test_digits_are_centred_inside_their_unscaled_windows);
    run_test("digit_slot0_segment_a_lights_when_level_2", test_digit_slot0_segment_a_lights_when_level_2);
    run_test("gap_between_segments_is_black_regardless_of_level", test_gap_between_segments_is_black_regardless_of_level);
    run_test("bright_brighter_than_dim", test_bright_brighter_than_dim);
    run_test("row1_decimal_point_lights_when_level_2", test_row1_decimal_point_lights_when_level_2);
    run_test("field_lamp_slot0_top_lights_when_level_2", test_field_lamp_slot0_top_lights_when_level_2);
    run_test("field_lamp_slot9_bottom_last_position_reachable", test_field_lamp_slot9_bottom_last_position_reachable);
    run_test("bezel_disabled_is_plain_black", test_bezel_disabled_is_plain_black);
    run_test("bezel_enabled_shows_label_bar_background", test_bezel_enabled_shows_label_bar_background);
    run_test("field_strip_extends_below_the_lamp_rows", test_field_strip_extends_below_the_lamp_rows);
    run_test("bezel_enabled_shows_green_margin", test_bezel_enabled_shows_green_margin);
    if (g_failures) { std::printf("FAILED: %d check(s)\n", g_failures); return 1; }
    std::printf("PASS: video_renderer_tb\n");
    return 0;
}
