// Verifies the grid layout (which pixel maps to which of the 110 PWM
// cells) and the level-to-color mapping.
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

// Layout constants, mirrored from src/display_render.v -- kept in sync by
// the two lint/compile steps below (a mismatch here would show up as
// every test failing, not a silent pass).
static const int MARGIN_X = 6, MARGIN_Y = 10, CELL_W = 28, CELL_H = 22, GAP = 3;

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

static void test_cell_0_0_center_pixel_is_bright_when_level_2() {
    Vdisplay_render d;
    for (int i = 0; i < 7; i++) d.levels[i] = 0;
    set_cell(d, 0, 0, 2); // row 0, col 0 -> bright
    int cx = MARGIN_X + CELL_W/2, cy = MARGIN_Y + CELL_H/2; // center of cell (0,0)
    d.x = cx; d.y = cy;
    d.eval();
    CHECK(d.rgb != 0, "center of a bright cell is non-black");
}

static void test_off_cell_center_pixel_is_black() {
    Vdisplay_render d;
    for (int i = 0; i < 7; i++) d.levels[i] = 0; // every cell level 0 (off)
    int cx = MARGIN_X + CELL_W/2, cy = MARGIN_Y + CELL_H/2;
    d.x = cx; d.y = cy;
    d.eval();
    CHECK(d.rgb == 0, "center of an off cell (level 0) is black");
}

static void test_gap_pixel_between_cells_is_black_regardless_of_level() {
    Vdisplay_render d;
    for (int i = 0; i < 7; i++) d.levels[i] = 0;
    set_cell(d, 0, 0, 2); // bright
    // pixel right at the cell's left edge (within the GAP inset) must stay
    // black even though the cell itself is bright -- proves the grid-line
    // gap is actually rendered, not just a level==0 check.
    d.x = MARGIN_X; d.y = MARGIN_Y + CELL_H/2;
    d.eval();
    CHECK(d.rgb == 0, "gap pixel at a bright cell's edge is still black");
}

static void test_bright_brighter_than_dim() {
    Vdisplay_render bright, dim;
    for (int i = 0; i < 7; i++) { bright.levels[i] = 0; dim.levels[i] = 0; }
    set_cell(bright, 0, 0, 2);
    set_cell(dim, 0, 0, 1);
    int cx = MARGIN_X + CELL_W/2, cy = MARGIN_Y + CELL_H/2;
    bright.x = cx; bright.y = cy; bright.eval();
    dim.x = cx; dim.y = cy; dim.eval();
    CHECK(((bright.rgb >> 16) & 0xFF) > ((dim.rgb >> 16) & 0xFF), "level 2 (bright) has a higher red-channel value than level 1 (dim)");
}

static void test_row_9_col_10_last_cell_reachable() {
    Vdisplay_render d;
    for (int i = 0; i < 7; i++) d.levels[i] = 0;
    set_cell(d, 9, 10, 2); // last row, last col of the 10x11 grid
    int cx = MARGIN_X + 10*CELL_W + CELL_W/2;
    int cy = MARGIN_Y + 9*CELL_H + CELL_H/2;
    d.x = cx; d.y = cy;
    d.eval();
    CHECK(d.rgb != 0, "last grid cell (row 9, col 10) is independently addressable and lights up");
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    run_test("cell_0_0_center_pixel_is_bright_when_level_2", test_cell_0_0_center_pixel_is_bright_when_level_2);
    run_test("off_cell_center_pixel_is_black", test_off_cell_center_pixel_is_black);
    run_test("gap_pixel_between_cells_is_black_regardless_of_level", test_gap_pixel_between_cells_is_black_regardless_of_level);
    run_test("bright_brighter_than_dim", test_bright_brighter_than_dim);
    run_test("row_9_col_10_last_cell_reachable", test_row_9_col_10_last_cell_reachable);
    if (g_failures) { std::printf("FAILED: %d check(s)\n", g_failures); return 1; }
    std::printf("PASS: display_render_tb\n");
    return 0;
}
