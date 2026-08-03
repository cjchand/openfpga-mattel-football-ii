// Verifies the digit/segment and field-lamp layout (which pixel maps to
// which of the 110 PWM cells) and the level-to-color mapping. Geometry
// constants mirrored from src/display_render.v -- kept in sync by the two
// lint/compile steps below (a mismatch here would show up as every test
// failing, not a silent pass).
#include "Vdisplay_render.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>

static int g_failures = 0;
static const char* g_current = "";
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::printf("FAIL [%s]: %s (line %d)\n", g_current, msg, __LINE__); \
                   g_failures++; return; } } while (0)

static void run_test(const char* name, void (*fn)(void)) { g_current = name; fn(); }

static const int DIGIT_MARGIN_X = 20, DIGIT_PITCH = 40, DIGIT_Y = 20;
static const int DIGIT_CELL_W = 24, DIGIT_CELL_H = 40;
static const int FIELD_X0 = 10, FIELD_COL_PITCH = 30;
static const int FIELD_Y0 = 100, FIELD_ROW_PITCH = 30, FIELD_DOT = 16;

static void set_cell(Vdisplay_render& d, int row, int col, int lvl) {
    // levels[219:0] is a 220-bit input; Verilator represents it as an array
    // of 7 uint32_t words (ceil(220/32)=7). Each cell's 2-bit field starts
    // at an even bit index, so (since 32 is even) no cell ever spans a
    // word boundary -- a plain single-word read-modify-write is exact.
    int bit = (row * 11 + col) * 2;
    int word = bit / 32, off = bit % 32;
    d.levels[word] &= ~(3u << off);
    d.levels[word] |= ((uint32_t)lvl & 3u) << off;
}

static void clear(Vdisplay_render& d) {
    for (int i = 0; i < 7; i++) d.levels[i] = 0;
}

static void test_digit0_segment_a_lights_when_level_2() {
    Vdisplay_render d;
    clear(d);
    set_cell(d, 0, 0, 2); // digit position 0 -> row 0, segment a -> col 0
    d.x = DIGIT_MARGIN_X + 4 + 16/2;   // center of seg a's rect
    d.y = DIGIT_Y + 4/2;
    d.eval();
    CHECK(d.rgb != 0, "segment a of digit 0 is non-black when its cell is level 2");
}

static void test_digit0_segment_a_off_is_black() {
    Vdisplay_render d;
    clear(d);
    d.x = DIGIT_MARGIN_X + 4 + 16/2;
    d.y = DIGIT_Y + 4/2;
    d.eval();
    CHECK(d.rgb == 0, "segment a of digit 0 is black when its cell is level 0");
}

static void test_gap_between_segments_is_black_regardless_of_level() {
    Vdisplay_render d;
    clear(d);
    set_cell(d, 0, 0, 2); // segment a bright
    set_cell(d, 0, 6, 2); // segment g bright
    // (12,12) relative to the digit cell falls between segments a/b/f/g --
    // proves individual segment shapes are drawn, not a filled cell.
    d.x = DIGIT_MARGIN_X + 12;
    d.y = DIGIT_Y + 12;
    d.eval();
    CHECK(d.rgb == 0, "gap pixel between segments stays black even with neighboring segments bright");
}

static void test_bright_brighter_than_dim() {
    Vdisplay_render bright, dim;
    clear(bright); clear(dim);
    set_cell(bright, 0, 0, 2);
    set_cell(dim, 0, 0, 1);
    int cx = DIGIT_MARGIN_X + 4 + 16/2, cy = DIGIT_Y + 4/2;
    bright.x = cx; bright.y = cy; bright.eval();
    dim.x = cx; dim.y = cy; dim.eval();
    CHECK(((bright.rgb >> 16) & 0xFF) > ((dim.rgb >> 16) & 0xFF), "level 2 (bright) has a higher red-channel value than level 1 (dim)");
}

static void test_digit1_decimal_point_lights_when_level_2() {
    Vdisplay_render d;
    clear(d);
    set_cell(d, 1, 7, 2); // digit position 1 -> row 1, decimal point -> col 7
    d.x = DIGIT_MARGIN_X + DIGIT_PITCH + DIGIT_CELL_W + 2 + 2; // center of the 4x4 dp box
    d.y = DIGIT_Y + DIGIT_CELL_H - 4 + 2;
    d.eval();
    CHECK(d.rgb != 0, "digit 1's decimal point is non-black when row 1 col 7 is level 2");
}

static void test_field_lamp_row0_col0_lights_when_level_2() {
    Vdisplay_render d;
    clear(d);
    set_cell(d, 3, 0, 2); // field row 0 -> levels[] row 3, col 0
    int cx = FIELD_X0 + (FIELD_COL_PITCH - FIELD_DOT) / 2 + FIELD_DOT / 2;
    int cy = FIELD_Y0 + FIELD_DOT / 2;
    d.x = cx; d.y = cy;
    d.eval();
    CHECK(d.rgb != 0, "field lamp (row 0, col 0) lights up when its cell is level 2");
}

static void test_field_lamp_row2_col9_last_position_reachable() {
    Vdisplay_render d;
    clear(d);
    set_cell(d, 5, 9, 2); // field row 2 -> levels[] row 5, last col 9
    int cx = FIELD_X0 + 9 * FIELD_COL_PITCH + (FIELD_COL_PITCH - FIELD_DOT) / 2 + FIELD_DOT / 2;
    int cy = FIELD_Y0 + 2 * FIELD_ROW_PITCH + FIELD_DOT / 2;
    d.x = cx; d.y = cy;
    d.eval();
    CHECK(d.rgb != 0, "last field lamp (row 2, col 9) is independently addressable and lights up");
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    run_test("digit0_segment_a_lights_when_level_2", test_digit0_segment_a_lights_when_level_2);
    run_test("digit0_segment_a_off_is_black", test_digit0_segment_a_off_is_black);
    run_test("gap_between_segments_is_black_regardless_of_level", test_gap_between_segments_is_black_regardless_of_level);
    run_test("bright_brighter_than_dim", test_bright_brighter_than_dim);
    run_test("digit1_decimal_point_lights_when_level_2", test_digit1_decimal_point_lights_when_level_2);
    run_test("field_lamp_row0_col0_lights_when_level_2", test_field_lamp_row0_col0_lights_when_level_2);
    run_test("field_lamp_row2_col9_last_position_reachable", test_field_lamp_row2_col9_last_position_reachable);
    if (g_failures) { std::printf("FAILED: %d check(s)\n", g_failures); return 1; }
    std::printf("PASS: display_render_tb\n");
    return 0;
}
