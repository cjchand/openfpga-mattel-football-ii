// src/display_render.v
//
// Renders pps41_display_pwm's already-reconstructed 10x11 PWM matrix
// (levels[219:0], cell = row*11+col, 2-bit brightness) as a plain grid of
// rectangles on a 320x240 canvas -- one rectangle per cell, brightness
// mapped from the cell's level. Per the design spec's approved "simple
// procedural shapes" fidelity: this does not attempt to reverse-engineer
// which cells are 7-segment digit vs. discrete-LED positions (deferred to
// the bezel/packaging sub-project) -- every cell renders identically,
// just positioned by its (row,col) coordinate.
module display_render (
    input  wire [219:0] levels,
    input  wire [9:0]   x,   // pixel x within the 320-wide active video region
    input  wire [9:0]   y,   // pixel y within the 240-tall active video region
    output wire [23:0]  rgb
);
    localparam MARGIN_X = 6, MARGIN_Y = 10;
    localparam CELL_W = 28, CELL_H = 22;
    localparam GAP = 3;
    localparam GRID_W = 11 * CELL_W; // 308, fits in 320 with MARGIN_X=6 each side
    localparam GRID_H = 10 * CELL_H; // 220, fits in 240 with MARGIN_Y=10 each side

    wire in_grid_x = (x >= MARGIN_X) && (x < MARGIN_X + GRID_W);
    wire in_grid_y = (y >= MARGIN_Y) && (y < MARGIN_Y + GRID_H);

    wire [9:0] rel_x = x - MARGIN_X;
    wire [9:0] rel_y = y - MARGIN_Y;
    wire [3:0] col = rel_x / CELL_W; // 0-10
    wire [3:0] row = rel_y / CELL_H; // 0-9
    wire [9:0] within_x = rel_x % CELL_W;
    wire [9:0] within_y = rel_y % CELL_H;

    wire lit_area = in_grid_x && in_grid_y &&
                    (within_x >= GAP) && (within_x < CELL_W - GAP) &&
                    (within_y >= GAP) && (within_y < CELL_H - GAP);

    wire [6:0] cell_idx = row * 7'd11 + col; // 0-109
    wire [1:0] cell_level = levels[cell_idx*2 +: 2];

    // Amber LED-style color: black when off/not-in-a-lit-area, dim amber
    // for level 1, bright amber-white for level 2 (level 3 never produced
    // by pps41_display_pwm but treated the same as bright, defensively).
    assign rgb = !lit_area          ? 24'h000000
               : (cell_level == 0)  ? 24'h000000
               : (cell_level == 1)  ? 24'h552200
               :                      24'hFF8800;
endmodule
