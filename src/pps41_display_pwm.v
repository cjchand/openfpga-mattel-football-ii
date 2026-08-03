// src/pps41_display_pwm.v
module pps41_display_pwm (
    input  wire         clk,
    input  wire         rst_n,
    input  wire [9:0]   rowsel,
    input  wire [10:0]  rowdata,
    output reg  [219:0] levels,    // 110 cells x 2 bits, cell = row*11 + col
    output reg           window_tick
);
    localparam integer WINDOW     = 1583;
    localparam integer DIM_MIN    = 24;
    localparam integer BRIGHT_MIN = 317;
    /* verilator lint_off WIDTHTRUNC */
    localparam [10:0]  WIN_LAST   = WINDOW - 1;
    /* verilator lint_on WIDTHTRUNC */

    reg [10:0] cnt [0:109];
    reg [10:0] window_pos;
    integer row, col, i;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            for (i = 0; i < 110; i = i + 1) cnt[i] <= 11'd0;
            window_pos  <= 11'd0;
            levels      <= 220'd0;
            window_tick <= 1'b0;
        end else begin
            window_tick <= 1'b0;

            for (row = 0; row < 10; row = row + 1)
                for (col = 0; col < 11; col = col + 1)
                    if (rowsel[row] && rowdata[col])
                        cnt[row * 11 + col] <= cnt[row * 11 + col] + 11'd1;

            if (window_pos == WIN_LAST) begin
                window_pos  <= 11'd0;
                window_tick <= 1'b1;
                for (i = 0; i < 110; i = i + 1) begin
                    /* verilator lint_off WIDTHEXPAND */
                    if (cnt[i] >= BRIGHT_MIN)
                        levels[i*2 +: 2] <= 2'd2;
                    else if (cnt[i] >= DIM_MIN)
                        levels[i*2 +: 2] <= 2'd1;
                    else
                        levels[i*2 +: 2] <= 2'd0;
                    /* verilator lint_on WIDTHEXPAND */
                    cnt[i] <= 11'd0;
                end
            end else
                window_pos <= window_pos + 11'd1;
        end
    end
endmodule
