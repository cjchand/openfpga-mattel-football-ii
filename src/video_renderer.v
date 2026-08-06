// Layered renderer: LED digit segments/field lamps (procedural,
// positions below; shapes unchanged from Phase 5's display_render.v) >
// label-bar text (label_rom) > field background (field_rom) > plain
// background. Canvas 400x360. Geometry measured from a photo of the
// real FBII device -- see
// docs/superpowers/specs/2026-08-03-bezel-field-overlay-design.md.
module video_renderer (
    input  wire [219:0] levels,
    input  wire [8:0]   x,             // 0-399
    input  wire [8:0]   y,             // 0-359
    input  wire         bezel_enable,
    output reg  [23:0]  rgb
);
    localparam [23:0] C_BG     = 24'h000000;
    // LED colours match the FB1 core exactly -- both devices use the same
    // red LEDs, and the orange used here previously read as clearly wrong
    // next to FB1 on real hardware.
    localparam [23:0] C_DIM    = 24'h801414;
    localparam [23:0] C_BRIGHT = 24'hFF2020;
    localparam [23:0] C_WHITE  = 24'hFFFFFF;
    localparam [23:0] C_GREEN  = 24'h0E8A03; // matches field_rom's GREEN_RGB

    // --- Digit position (0-6, left-to-right on screen) -> levels[] row.
    // Unchanged from Phase 5's display_render.v. ---
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

    // --- Scoreboard plaque geometry ---
    //
    // Taken from FB1, whose values are hardware-tuned, and matching the real
    // FB2's plaque: the white carries all the way to both screen edges, and
    // the three digit windows are separated by a HAIRLINE white divider
    // rather than the wide white gaps this core shipped with.
    //
    //   white 0..40 | win1 41..139 | 140 | win2 141..258 | 259 | win3 260..358 | white 359..399
    //
    // FB1 keeps a 1px black corner column at x=0 and x=399; those are dropped
    // here so the white really does reach the edge.
    localparam WIN1_X0 = 41,  WIN1_X1 = 140;   // [x0, x1)
    localparam WIN2_X0 = 141, WIN2_X1 = 259;
    localparam WIN3_X0 = 260, WIN3_X1 = 359;

    // Vertical: the plaque sits directly on top of the field's green margin,
    // with no black band between them (FB1 does the same -- "carries the
    // field's green up to the score area"). The black that used to fill
    // y 75..126 now sits above the plaque as a top letterbox, balancing the
    // one below the field.
    //
    //   black 0..51 | bar1 52..67 | digits 68..110 | bar2 111..126 | green 127..
    localparam BAR1_Y0  = 52,  BAR1_Y1  = 68;    // [y0, y1), 16 rows
    localparam DIGIT_Y0 = 68,  DIGIT_Y1 = 111;   // 43 rows
    localparam BAR2_Y0  = 111, BAR2_Y1  = 127;   // 16 rows

    // Digit cell was 24x40 through Phase 5, scaled to 75% (18x30, stroke 3,
    // segment run 12, pitch 30, decimal point 3x3) after the first hardware
    // play-through -- the numerals read as oversized for their windows.
    //
    // digit_x centres each group in its own window rather than scaling fixed
    // offsets: window centres are 90 / 200 / 309, and a group of n digits
    // spans 30*(n-1) + 18. DIGIT_Y centres the 30-row cell in the 43-row
    // band: 68 + (43-30)/2.
    localparam DIGIT_Y = 74, DIGIT_CELL_W = 18, DIGIT_CELL_H = 30;

    function [8:0] digit_x(input [2:0] d);
        case (d)
            3'd0: digit_x = 9'd66;   3'd1: digit_x = 9'd96;                    // window 1
            3'd2: digit_x = 9'd161;  3'd3: digit_x = 9'd191; 3'd4: digit_x = 9'd221; // window 2
            3'd5: digit_x = 9'd285;  3'd6: digit_x = 9'd315;                   // window 3
            default: digit_x = 9'd0;
        endcase
    endfunction

    // Segment rects within a digit cell.
    function [39:0] seg_rect(input [2:0] s);
        case (s)
            3'd0: seg_rect = {9'd3,  9'd0,  9'd12, 9'd3};  // a
            3'd1: seg_rect = {9'd15, 9'd3,  9'd3,  9'd12}; // b
            3'd2: seg_rect = {9'd15, 9'd15, 9'd3,  9'd12}; // c
            3'd3: seg_rect = {9'd3,  9'd27, 9'd12, 9'd3};  // d
            3'd4: seg_rect = {9'd0,  9'd15, 9'd3,  9'd12}; // e
            3'd5: seg_rect = {9'd0,  9'd3,  9'd3,  9'd12}; // f
            3'd6: seg_rect = {9'd3,  9'd14, 9'd12, 9'd3};  // g
            default: seg_rect = 40'd0;
        endcase
    endfunction

    // --- Field lamp screen-column (0-9) -> levels[] row. Unchanged from
    // Phase 5. ---
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

    // Field geometry mirrors FB1's, adjusted from 9 to 10 columns. All of
    // these must stay in step with tools/gen_bezel_bitmaps.py, which draws
    // the matching background bitmap.
    localparam FIELD_X0 = 30, COL_PITCH = 34;
    // STRIP_H must match gen_bezel_bitmaps.py's STRIP_Y1 - STRIP_Y0.
    localparam STRIP_H = 108;
    // Unchanged. The plaque was moved down to meet it rather than the field
    // being moved up, which keeps the field exactly where it is on hardware
    // and leaves the reclaimed black as a top letterbox. The identity that
    // must hold is BAR2_Y1 == STRIP_Y0 - BORDER_W - GREEN_MARGIN (127), so
    // the green above the field is the same 32px as the green below it.
    localparam STRIP_Y0 = 163;
    // Lamps are thin dashes, as on the real device and FB1 (which uses
    // 16x6 in a 36px cell). Scaled to this core's 32px cell: 14 wide with a
    // 9px margin each side, same 6px height. The previous 20x28 lamps were
    // nearly five times too tall and read as solid blocks rather than LEDs.
    // Rows sit on the same thirds FB1 uses, leaving a 15px margin at the
    // top and bottom of the strip: 15 + 6 + 30 + 6 + 30 + 6 + 15 = 108.
    localparam LAMP_Y_OFF = 15, ROW_PITCH = 36, LAMP_W = 14, LAMP_H = 6;
    localparam LAMP_Y0 = STRIP_Y0 + LAMP_Y_OFF;
    localparam BORDER_W = 4;
    // Green surround around the field strip, matching FB1's 32px margin.
    localparam GREEN_MARGIN = 32;

    function [23:0] level_color(input [1:0] lvl);
        case (lvl)
            2'd0: level_color = C_BG;
            2'd1: level_color = C_DIM;
            default: level_color = C_BRIGHT;
        endcase
    endfunction

    // --- background layers ---
    wire [23:0] label_rgb;
    /* verilator lint_off WIDTHEXPAND */
    // label_rom holds 32 rows: 0-15 are bar1, 16-31 are bar2.
    wire [8:0]  label_band_y = (y < BAR1_Y1) ? (y - BAR1_Y0) : (y - BAR2_Y0 + 9'd16);
    /* verilator lint_on WIDTHEXPAND */
    label_rom lrom (.x(x), .band_y(label_band_y[4:0]), .rgb(label_rgb));

    wire [23:0] field_rgb;
    /* verilator lint_off WIDTHEXPAND */
    wire [8:0]  field_y = y - (STRIP_Y0 - BORDER_W);
    /* verilator lint_on WIDTHEXPAND */
    field_rom from (.x(x), .field_y(field_y[7:0]), .rgb(field_rgb));

    integer d, s, fcol;
    reg [8:0] rx0, ry0, rw, rh;
    reg [1:0] lvl;
    reg [6:0] cell_idx;

    always @* begin
        lvl = 2'd0;
        cell_idx = 7'd0;

        // --- background (bottom layer) ---
        if (!bezel_enable) begin
            rgb = C_BG;
        end else if ((y >= BAR1_Y0 && y < BAR1_Y1) ||
                     (y >= BAR2_Y0 && y < BAR2_Y1)) begin
            rgb = label_rgb; // label bars
        end else if (y >= DIGIT_Y0 && y < DIGIT_Y1) begin
            // Digit-window band: the three windows are black, everything
            // else is the white plaque -- including both screen edges and
            // the two 1px dividers between windows.
            if ((x >= WIN1_X0 && x < WIN1_X1) ||
                (x >= WIN2_X0 && x < WIN2_X1) ||
                (x >= WIN3_X0 && x < WIN3_X1))
                rgb = C_BG;
            else
                rgb = C_WHITE;
        end else if (y >= (STRIP_Y0 - BORDER_W) && y < (STRIP_Y0 + STRIP_H + BORDER_W)) begin
            rgb = field_rgb; // field strip (border + 10-column art)
        end else if (y >= BAR2_Y1 &&
                     y <  (STRIP_Y0 + STRIP_H + BORDER_W + GREEN_MARGIN)) begin
            // Green margin. Above the field it starts the instant the plaque
            // ends (BAR2_Y1 == STRIP_Y0 - BORDER_W - GREEN_MARGIN, i.e. the
            // same 32px as below) so there is no black band separating the
            // scoreboard from the playfield.
            rgb = C_GREEN;
        end else begin
            // Letterbox. FB1 does the same (a black bar above and below a
            // 32px green margin) -- filling the whole lower canvas with
            // green made the field read as a green screen with a strip on
            // it rather than a field with a surround.
            rgb = C_BG;
        end

        // --- digit segments: unconditional draw ---
        for (d = 0; d < 7; d = d + 1) begin
            for (s = 0; s < 7; s = s + 1) begin
                {rx0, ry0, rw, rh} = seg_rect(s[2:0]);
                rx0 = rx0 + digit_x(d[2:0]);
                ry0 = ry0 + DIGIT_Y;
                if (x >= rx0 && x < rx0 + rw && y >= ry0 && y < ry0 + rh) begin
                    cell_idx = digit_row(d[2:0]) * 11 + s[2:0];
                    lvl = levels[cell_idx*2 +: 2];
                    rgb = level_color(lvl);
                end
            end
            // Decimal point: row 1 only, bit 7.
            if (digit_row(d[2:0]) == 4'd1) begin
                rx0 = digit_x(d[2:0]) + DIGIT_CELL_W + 2;
                ry0 = DIGIT_Y + DIGIT_CELL_H - 3;
                if (x >= rx0 && x < rx0 + 3 && y >= ry0 && y < ry0 + 3) begin
                    cell_idx = 4'd1 * 11 + 7;
                    lvl = levels[cell_idx*2 +: 2];
                    rgb = level_color(lvl);
                end
            end
        end

        // --- field lamps: 3 rows x 10 columns, unconditional draw ---
        for (fcol = 0; fcol < 10; fcol = fcol + 1) begin
            rx0 = FIELD_X0 + fcol * COL_PITCH + (COL_PITCH - LAMP_W) / 2;
            if (x >= rx0 && x < rx0 + LAMP_W) begin
                if (y >= LAMP_Y0 && y < LAMP_Y0 + LAMP_H) begin
                    cell_idx = field_row(fcol[3:0]) * 11 + 8; // top lamp
                    lvl = levels[cell_idx*2 +: 2];
                    rgb = level_color(lvl);
                end else if (y >= LAMP_Y0 + ROW_PITCH && y < LAMP_Y0 + ROW_PITCH + LAMP_H) begin
                    cell_idx = field_row(fcol[3:0]) * 11 + 9; // mid lamp
                    lvl = levels[cell_idx*2 +: 2];
                    rgb = level_color(lvl);
                end else if (y >= LAMP_Y0 + 2*ROW_PITCH && y < LAMP_Y0 + 2*ROW_PITCH + LAMP_H) begin
                    cell_idx = field_row(fcol[3:0]) * 11 + 10; // bottom lamp
                    lvl = levels[cell_idx*2 +: 2];
                    rgb = level_color(lvl);
                end
            end
        end
    end
endmodule
