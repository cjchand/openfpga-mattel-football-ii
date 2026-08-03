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

static void test_digit_slot0_segment_a_lights_when_level_2() {
    Vvideo_renderer d;
    clear(d);
    set_cell(d, 8, 0, 2); // screen slot 0 -> row 8, segment a
    d.x = 20 + 4 + 16/2;  // digit_x(0)=20
    d.y = 18 + 4/2;       // DIGIT_Y=18
    d.eval();
    CHECK(d.rgb != 0, "segment a of digit slot 0 is non-black when its cell is level 2");
}

static void test_gap_between_segments_is_black_regardless_of_level() {
    Vvideo_renderer d;
    clear(d);
    set_cell(d, 8, 0, 2); // segment a bright
    set_cell(d, 8, 6, 2); // segment g bright
    // (12,12) relative to the digit cell falls between segments a/b/f/g --
    // proves individual segment shapes are drawn, not a filled cell.
    d.x = 20 + 12; // digit_x(0)=20
    d.y = 18 + 12; // DIGIT_Y=18
    d.eval();
    CHECK(d.rgb == 0, "gap pixel between segments stays black even with neighboring segments bright");
}

static void test_bright_brighter_than_dim() {
    Vvideo_renderer bright, dim;
    clear(bright); clear(dim);
    set_cell(bright, 8, 0, 2);
    set_cell(dim, 8, 0, 1);
    int cx = 20 + 4 + 16/2, cy = 18 + 4/2; // digit_x(0)=20, DIGIT_Y=18
    bright.x = cx; bright.y = cy; bright.eval();
    dim.x = cx; dim.y = cy; dim.eval();
    CHECK(((bright.rgb >> 16) & 0xFF) > ((dim.rgb >> 16) & 0xFF), "level 2 (bright) has a higher red-channel value than level 1 (dim)");
}

static void test_row1_decimal_point_lights_when_level_2() {
    Vvideo_renderer d;
    clear(d);
    set_cell(d, 1, 7, 2);
    // digit_row(3)==1, so screen slot 3 (digit_x(3)=188) carries the dp.
    d.x = 188 + 24 + 2 + 2;
    d.y = 18 + 40 - 4 + 2;
    d.eval();
    CHECK(d.rgb != 0, "row 1's decimal point is non-black when row 1 col 7 is level 2");
}

static void test_field_lamp_slot0_top_lights_when_level_2() {
    Vvideo_renderer d;
    clear(d);
    set_cell(d, 8, 8, 2); // field screen slot 0 -> row 8, top lamp
    d.x = 25 + (35-20)/2 + 20/2;  // FIELD_X0=25, COL_PITCH=35, LAMP_W=20
    d.y = 90 + 8 + 28/2;          // STRIP_Y0=90, LAMP_Y_OFF=8, LAMP_H=28
    d.eval();
    CHECK(d.rgb != 0, "field lamp (slot 0 -> row 8, top) lights up when its cell is level 2");
}

static void test_field_lamp_slot9_bottom_last_position_reachable() {
    Vvideo_renderer d;
    clear(d);
    set_cell(d, 7, 10, 2); // field screen slot 9 -> row 7 (field_row(9)), bottom lamp -> col 10
    // FIELD_X0=25, COL_PITCH=35, LAMP_W=20, STRIP_Y0=90, LAMP_Y_OFF=8, ROW_PITCH=60, LAMP_H=28
    int cx = 25 + 9 * 35 + (35 - 20) / 2 + 20 / 2;
    int cy = 90 + 8 + 2 * 60 + 28 / 2;
    d.x = cx; d.y = cy;
    d.eval();
    CHECK(d.rgb != 0, "last field lamp (slot 9 -> row 7, bottom) is independently addressable and lights up");
}

static void test_bezel_disabled_is_plain_black() {
    Vvideo_renderer d;
    clear(d);
    d.bezel_enable = 0;
    d.x = 200; d.y = 100; // inside where the field strip would be
    d.eval();
    CHECK(d.rgb == 0, "bezel_enable=0 shows plain black everywhere outside lit LEDs");
}

static void test_bezel_enabled_shows_label_bar_background() {
    Vvideo_renderer d;
    clear(d);
    // x=120 is the gap between digit window 1 (x12-91) and window 2
    // (x140-259), so no label text belongs here. (x=300 would NOT work:
    // it is close enough to window 3 (x308-387, "YARDS TO GO") that
    // asserting white there would be asserting text is absent where text
    // is legitimately drawn.)
    d.x = 120; d.y = 5;
    d.eval();
    CHECK(d.rgb == 0xFFFFFF, "label bar background is white when bezel_enable=1");
}

static void test_field_strip_extends_below_the_lamp_rows() {
    // The strip is 180px tall (STRIP_Y0=90 .. y269 plus border), so a
    // point well below the last lamp row (y218-245) is still field art,
    // not the green margin -- this is what keeps the lower canvas from
    // being a large empty green band.
    Vvideo_renderer d;
    clear(d);
    d.x = 30; d.y = 260; // inside field cell 0, below all three lamp rows
    d.eval();
    CHECK(d.rgb != 0x12CA7D, "field strip still covers y=260 rather than falling back to green margin");
}

static void test_bezel_enabled_shows_green_margin() {
    Vvideo_renderer d;
    clear(d);
    d.x = 200; d.y = 350; // bottom green filler area
    d.eval();
    CHECK(d.rgb == 0x12CA7D, "bottom green margin shows field green when bezel_enable=1");
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
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
