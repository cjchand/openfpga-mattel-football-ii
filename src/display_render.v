// src/display_render.v
//
// Renders pps41_display_pwm's already-reconstructed 10x11 PWM matrix
// (levels[219:0], cell = row*11+col, 2-bit brightness) on a 320x240 canvas.
//
// Row/column roles and screen ordering below are sourced directly from a
// local MAME checkout (~/Projects/mame), not guessed:
//   - src/mame/handheld/hh_pps41.cpp, mfootb2_state::update_display() and
//     the mfootb2() machine-config: set_segmask(0x3c7, 0x7f) and
//     set_segmask(0x002, 0xff) mark rows {0,1,2,6,7,8,9} as the 7
//     seven-segment digits (row 1 alone also gets the decimal point, bit
//     7); rows {3,4,5} are otherwise unused/unwired (no set_segmask
//     covers them) -- confirming what docs/initial-plan.md's transcribed
//     driver comments already said.
//   - src/mame/layout/mfootb2.lay's explicit element bounds settle the two
//     things the driver comments alone don't: screen order. The digit
//     panel group places digit8/digit9/digit0/digit1/digit2/digit6/digit7
//     left-to-right in that exact row-index order (x=21,37,69,85,101,133,
//     149) -- NOT ascending row-index order. And the field/LED view places
//     elements named "8.8/8.9/8.10", "9.8/9.9/9.10", "0.8/0.9/0.10", ...,
//     "7.8/7.9/7.10" left-to-right in that same row-index order (x=7,27,
//     47,...,187) -- i.e. EVERY row 0-9 (not just {3,4,5}) has 3 field
//     lamps, at column-bits {8,9,10} specifically (top/mid/bottom), for
//     10*3=30 lamps total. The digit rows and field-lamp rows overlap
//     (rows {0,1,2,6,7,8,9} carry both a digit at columns 0-6/0-7 AND 3
//     field lamps at columns 8-10 on the same row-select line) rather
//     than being disjoint row ranges, matching a real multiplexed board
//     where the field's 10 lamp-columns and the 7 digit tubes share
//     digit-select lines but only 7 of the 10 have a digit soldered on.
//   - src/emu/rendlay.cpp's led7seg_component::draw_aligned() gives the
//     segment-to-bit convention MAME's own <led7seg> layout element uses:
//     bit0=top(a), bit1=upper-right(b), bit2=lower-right(c), bit3=bottom
//     (d), bit4=lower-left(e), bit5=upper-left(f), bit6=middle(g),
//     bit7=decimal point. That's what seg_rect() below implements.
//
// What's still NOT sourced (no FBII photo/hardware reference exists yet,
// unlike FB1's photo-calibrated bezel): the actual pixel geometry --
// sizes, spacing, margins below are this project's own procedural choice,
// not measured off a real device. Per the design spec's approved "simple
// procedural shapes" fidelity, this still isn't attempting photo-accurate
// bezel art -- that stays deferred to the dedicated bezel phase.
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
    // Order per mfootb2.lay's digit panel group: digit8,9,0,1,2,6,7.
    function [3:0] digit_row(input [2:0] d);
        case (d)
            3'd0: digit_row = 4'd8;
            3'd1: digit_row = 4'd9;
            3'd2: digit_row = 4'd0;
            3'd3: digit_row = 4'd1;
            3'd4: digit_row = 4'd2;
            3'd5: digit_row = 4'd6;
            default: digit_row = 4'd7; // d == 6
        endcase
    endfunction

    localparam DIGIT_MARGIN_X = 20, DIGIT_PITCH = 40, DIGIT_Y = 20;
    localparam DIGIT_CELL_W = 24, DIGIT_CELL_H = 40;

    function [9:0] digit_x0(input [2:0] d);
        digit_x0 = DIGIT_MARGIN_X + d * DIGIT_PITCH;
    endfunction

    // Segment rects within a digit cell, {x0, y0, w, h}, matching MAME's
    // led7seg_component bit order (see header comment).
    function [39:0] seg_rect(input [2:0] s);
        case (s)
            3'd0: seg_rect = {10'd4,  10'd0,  10'd16, 10'd4};  // bit0 - top (a)
            3'd1: seg_rect = {10'd20, 10'd4,  10'd4,  10'd16}; // bit1 - upper right (b)
            3'd2: seg_rect = {10'd20, 10'd20, 10'd4,  10'd16}; // bit2 - lower right (c)
            3'd3: seg_rect = {10'd4,  10'd36, 10'd16, 10'd4};  // bit3 - bottom (d)
            3'd4: seg_rect = {10'd0,  10'd20, 10'd4,  10'd16}; // bit4 - lower left (e)
            3'd5: seg_rect = {10'd0,  10'd4,  10'd4,  10'd16}; // bit5 - upper left (f)
            3'd6: seg_rect = {10'd4,  10'd18, 10'd16, 10'd4};  // bit6 - middle (g)
            default: seg_rect = 40'd0;
        endcase
    endfunction

    // Field lamps: EVERY levels[] row (0-9) has 3 lamps at columns 8/9/10
    // (top/mid/bottom respectively, per mfootb2.lay's y=62/79/96). Screen
    // left-to-right column order per mfootb2.lay's field view element
    // x-coords: rows 8,9,0,1,2,3,4,5,6,7 (10 positions).
    function [3:0] field_row(input [3:0] c);
        case (c)
            4'd0: field_row = 4'd8;
            4'd1: field_row = 4'd9;
            4'd2: field_row = 4'd0;
            4'd3: field_row = 4'd1;
            4'd4: field_row = 4'd2;
            4'd5: field_row = 4'd3;
            4'd6: field_row = 4'd4;
            4'd7: field_row = 4'd5;
            4'd8: field_row = 4'd6;
            default: field_row = 4'd7; // c == 9
        endcase
    endfunction

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
            // Decimal point: row 1 only (segmask 0x002 -> ff), bit 7,
            // wherever row 1 lands in the screen digit order.
            if (digit_row(d[2:0]) == 4'd1) begin
                rx0 = digit_x0(d[2:0]) + DIGIT_CELL_W + 2;
                ry0 = DIGIT_Y + DIGIT_CELL_H - 4;
                if (x >= rx0 && x < rx0 + 4 && y >= ry0 && y < ry0 + 4) begin
                    cell_idx = 4'd1 * 11 + 7;
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
                    cell_idx = field_row(fcol[3:0]) * 11 + (8 + frow);
                    lvl = levels[cell_idx*2 +: 2];
                    rgb = level_color(lvl);
                end
            end
        end
    end
endmodule
