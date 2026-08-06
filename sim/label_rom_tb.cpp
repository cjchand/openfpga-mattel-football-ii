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

    // Every word must land inside its own digit window. The window
    // x-ranges match video_renderer.v's digit-window boxes. Checking all
    // three windows in both bars (6 checks) is what catches a word being
    // rendered off-canvas or into the wrong window -- a single
    // "somewhere in this row there is ink" check does not.
    struct Window { int x0, x1; const char* name; };
    const Window windows[3] = {
        {41, 140, "window1"}, {141, 259, "window2"}, {260, 359, "window3"},
    };
    const int bar_rows[2] = {8, 24}; // mid-height row of bar1 / bar2
    const char* bar_names[2] = {"bar1", "bar2"};

    for (int b = 0; b < 2; b++) {
        for (int w = 0; w < 3; w++) {
            bool has_text = false;
            // Scan the whole band, not one row: glyph ink does not cover
            // every row of a 16px bar.
            for (int by = bar_rows[b] - 8; by < bar_rows[b] + 8 && !has_text; by++) {
                for (int px = windows[w].x0; px < windows[w].x1; px++) {
                    if (sample(d, px, by) != 0xFFFFFF) { has_text = true; break; }
                }
            }
            char msg[128];
            std::snprintf(msg, sizeof(msg),
                          "%s %s (x%d-%d) contains label text (non-background pixels)",
                          bar_names[b], windows[w].name, windows[w].x0, windows[w].x1 - 1);
            CHECK(has_text, msg);
        }
    }

    // Nothing may be drawn outside a window -- proves no word overflows into
    // a neighbour or off the plaque. This used to sample two points in the
    // wide white gaps between windows; those gaps are now single-pixel
    // dividers (x=140 and x=259), so the whole outside is checked instead:
    // the left/right margins plus the two divider columns. With only 1px
    // between neighbouring words this is the check that has to be exact.
    for (int b = 0; b < 2; b++) {
        for (int x = 0; x < 400; x++) {
            bool inside = false;
            for (int w = 0; w < 3; w++)
                if (x >= windows[w].x0 && x < windows[w].x1) inside = true;
            if (inside) continue;
            for (int by = bar_rows[b] - 8; by < bar_rows[b] + 8; by++) {
                if (sample(d, x, by) != 0xFFFFFF) {
                    char msg[128];
                    std::snprintf(msg, sizeof(msg),
                                  "%s has ink at x=%d, outside every digit window",
                                  bar_names[b], x);
                    CHECK(false, msg);
                    break;
                }
            }
        }
    }

    if (g_failures) { std::printf("FAILED: %d check(s)\n", g_failures); return 1; }
    std::printf("PASS: label_rom_tb\n");
    return 0;
}
