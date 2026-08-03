// src/display_render.v
//
// Renders pps41_display_pwm's already-reconstructed 10x11 PWM matrix
// (levels[219:0], cell = row*11+col, 2-bit brightness) on a 320x240 canvas.
//
// Row roles are sourced from docs/initial-plan.md's MAME-driver comments for
// mfootb2 (segmask 0x3c7 -> 7f, segmask 0x002 -> ff, DIO0-2/DIO6-9 = digit
// select, DIO3-5 = led select): rows {0,1,2,6,7,8,9} are the 7 seven-segment
// digits, row 1 alone also carries the decimal point, and rows {3,4,5} are
// the 30-lamp field display (3 rows x 10 columns) -- the same 3-row
// dash-field shape the sibling FB1 project uses for its own field LEDs.
// That row/column *classification* is real, sourced data, not a guess.
//
// Two things below are NOT sourced from hardware (no FBII photo/layout
// reference exists yet, unlike FB1's photo-calibrated bezel): which bit of
// a digit row's data is segment a vs. b vs. ... vs. g (assumed standard
// a-g bit-order 0-6), and which screen position each of the 7 digit rows
// occupies left-to-right (assumed ascending row-index order). Both are
// placeholder conventions, disclosed here so a future hardware-verified
// bezel phase knows exactly what to confirm or correct -- per the design
// spec's approved "simple procedural shapes" fidelity, this still isn't
// attempting photo-accurate bezel art.
module display_render (
    input  wire [219:0] levels,
    input  wire [9:0]   x,   // pixel x within the 320-wide active video region
    input  wire [9:0]   y,   // pixel y within the 240-tall active video region
    output reg  [23:0]  rgb
);
    localparam [23:0] C_BG     = 24'h000000;
    localparam [23:0] C_DIM    = 24'h552200;
    localparam [23:0] C_BRIGHT = 24'hFF8800;

    // Digit position (0-6, left-to-right on screen) -> levels[] row index.
    function [3:0] digit_row(input [2:0] d);
        case (d)
            3'd0: digit_row = 4'd0;
            3'd1: digit_row = 4'd1;
            3'd2: digit_row = 4'd2;
            3'd3: digit_row = 4'd6;
            3'd4: digit_row = 4'd7;
            3'd5: digit_row = 4'd8;
            default: digit_row = 4'd9;
        endcase
    endfunction

    localparam DIGIT_MARGIN_X = 20, DIGIT_PITCH = 40, DIGIT_Y = 20;
    localparam DIGIT_CELL_W = 24, DIGIT_CELL_H = 40;

    function [9:0] digit_x0(input [2:0] d);
        digit_x0 = DIGIT_MARGIN_X + d * DIGIT_PITCH;
    endfunction

    // Segment rects within a digit cell, standard 7-seg a-g layout,
    // {x0, y0, w, h}.
    function [39:0] seg_rect(input [2:0] s);
        case (s)
            3'd0: seg_rect = {10'd4,  10'd0,  10'd16, 10'd4};  // a - top
            3'd1: seg_rect = {10'd20, 10'd4,  10'd4,  10'd16}; // b - upper right
            3'd2: seg_rect = {10'd20, 10'd20, 10'd4,  10'd16}; // c - lower right
            3'd3: seg_rect = {10'd4,  10'd36, 10'd16, 10'd4};  // d - bottom
            3'd4: seg_rect = {10'd0,  10'd20, 10'd4,  10'd16}; // e - lower left
            3'd5: seg_rect = {10'd0,  10'd4,  10'd4,  10'd16}; // f - upper left
            3'd6: seg_rect = {10'd4,  10'd18, 10'd16, 10'd4};  // g - middle
            default: seg_rect = 40'd0;
        endcase
    endfunction

    // Field lamps: rows {3,4,5} (loop index row = 0-2, actual levels[] row
    // = 3+row), columns 0-9.
    localparam FIELD_X0 = 10, FIELD_COL_PITCH = 30;
    localparam FIELD_Y0 = 100, FIELD_ROW_PITCH = 30, FIELD_DOT = 16;

    function [23:0] level_color(input [1:0] lvl);
        case (lvl)
            2'd0: level_color = C_BG;
            2'd1: level_color = C_DIM;
            default: level_color = C_BRIGHT;
        endcase
    endfunction

    integer d, s, frow, fcol;
    reg [9:0] rx0, ry0, rw, rh;
    reg [1:0] lvl;
    reg [6:0] cell_idx;

    always @* begin
        rgb = C_BG;
        lvl = 2'd0;
        cell_idx = 7'd0;

        for (d = 0; d < 7; d = d + 1) begin
            for (s = 0; s < 7; s = s + 1) begin
                {rx0, ry0, rw, rh} = seg_rect(s[2:0]);
                rx0 = rx0 + digit_x0(d[2:0]);
                ry0 = ry0 + DIGIT_Y;
                if (x >= rx0 && x < rx0 + rw && y >= ry0 && y < ry0 + rh) begin
                    cell_idx = digit_row(d[2:0]) * 11 + s[2:0];
                    lvl = levels[cell_idx*2 +: 2];
                    rgb = level_color(lvl);
                end
            end
            // Decimal point: digit position 1 only, row 1's column 7 (the
            // "segmask 0x002 -> ff" row), drawn just right of that cell.
            if (d == 1) begin
                rx0 = digit_x0(d[2:0]) + DIGIT_CELL_W + 2;
                ry0 = DIGIT_Y + DIGIT_CELL_H - 4;
                if (x >= rx0 && x < rx0 + 4 && y >= ry0 && y < ry0 + 4) begin
                    cell_idx = digit_row(d[2:0]) * 11 + 7;
                    lvl = levels[cell_idx*2 +: 2];
                    rgb = level_color(lvl);
                end
            end
        end

        for (frow = 0; frow < 3; frow = frow + 1) begin
            for (fcol = 0; fcol < 10; fcol = fcol + 1) begin
                rx0 = FIELD_X0 + fcol * FIELD_COL_PITCH + (FIELD_COL_PITCH - FIELD_DOT) / 2;
                ry0 = FIELD_Y0 + frow * FIELD_ROW_PITCH;
                if (x >= rx0 && x < rx0 + FIELD_DOT && y >= ry0 && y < ry0 + FIELD_DOT) begin
                    cell_idx = (3 + frow) * 11 + fcol;
                    lvl = levels[cell_idx*2 +: 2];
                    rgb = level_color(lvl);
                end
            end
        end
    end
endmodule
