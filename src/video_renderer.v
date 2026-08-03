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
    localparam [23:0] C_DIM    = 24'h552200;
    localparam [23:0] C_BRIGHT = 24'hFF8800;
    localparam [23:0] C_WHITE  = 24'hFFFFFF;
    localparam [23:0] C_GREEN  = 24'h12CA7D; // matches field_rom's GREEN_RGB

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

    localparam DIGIT_Y = 18, DIGIT_CELL_W = 24, DIGIT_CELL_H = 40;

    function [8:0] digit_x(input [2:0] d);
        case (d)
            3'd0: digit_x = 9'd20;   3'd1: digit_x = 9'd60;                    // window 1
            3'd2: digit_x = 9'd148;  3'd3: digit_x = 9'd188; 3'd4: digit_x = 9'd228; // window 2
            3'd5: digit_x = 9'd316;  3'd6: digit_x = 9'd356;                   // window 3
            default: digit_x = 9'd0;
        endcase
    endfunction

    // Segment rects within a digit cell -- unchanged from Phase 5.
    function [39:0] seg_rect(input [2:0] s);
        case (s)
            3'd0: seg_rect = {9'd4,  9'd0,  9'd16, 9'd4};  // a
            3'd1: seg_rect = {9'd20, 9'd4,  9'd4,  9'd16}; // b
            3'd2: seg_rect = {9'd20, 9'd20, 9'd4,  9'd16}; // c
            3'd3: seg_rect = {9'd4,  9'd36, 9'd16, 9'd4};  // d
            3'd4: seg_rect = {9'd0,  9'd20, 9'd4,  9'd16}; // e
            3'd5: seg_rect = {9'd0,  9'd4,  9'd4,  9'd16}; // f
            3'd6: seg_rect = {9'd4,  9'd18, 9'd16, 9'd4};  // g
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

    localparam FIELD_X0 = 25, COL_PITCH = 35;
    // STRIP_H must match gen_bezel_bitmaps.py's STRIP_Y1 - STRIP_Y0.
    // The three lamp rows are spread across the whole strip (rows at
    // strip-relative y8-35, y68-95, y128-155) so the strip does not read
    // as three lamps crowded at the top over an empty black field.
    localparam STRIP_Y0 = 90, STRIP_H = 180;
    localparam LAMP_Y_OFF = 8, ROW_PITCH = 60, LAMP_W = 20, LAMP_H = 28;
    localparam LAMP_Y0 = STRIP_Y0 + LAMP_Y_OFF;
    localparam BORDER_W = 4;

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
    wire [8:0]  label_band_y = (y < 9'd16) ? y : (y - 9'd43); // bar2 starts at y=59, band_y 16 there
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
        end else if (y < 9'd16 || (y >= 9'd59 && y < 9'd75)) begin
            rgb = label_rgb; // label bars
        end else if (y >= 9'd16 && y < 9'd59) begin
            // digit-window band: corner accents + 3 digit-window boxes
            // are black; everything else (including the gaps between
            // windows) is white, matching the photo's one continuous
            // white scoreboard plaque.
            if ((x < 9'd6) || (x >= 9'd12 && x < 9'd92) ||
                (x >= 9'd140 && x < 9'd260) || (x >= 9'd308 && x < 9'd388) ||
                (x >= 9'd394))
                rgb = C_BG;
            else
                rgb = C_WHITE;
        end else if (y >= (STRIP_Y0 - BORDER_W) && y < (STRIP_Y0 + STRIP_H + BORDER_W)) begin
            rgb = field_rgb; // field strip (border + 10-column art)
        end else begin
            rgb = C_GREEN; // green margin above/below the field strip
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
                ry0 = DIGIT_Y + DIGIT_CELL_H - 4;
                if (x >= rx0 && x < rx0 + 4 && y >= ry0 && y < ry0 + 4) begin
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
