// src/pps41_display_pwm.v
module pps41_display_pwm (
    input  wire         clk,
    input  wire         rst_n,
    input  wire         ce,
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
        end else if (ce) begin
            window_tick <= 1'b0;

            for (row = 0; row < 10; row = row + 1)
                for (col = 0; col < 11; col = col + 1)
                    if (rowsel[row] && rowdata[col])
                        cnt[row * 11 + col] <= cnt[row * 11 + col] + 11'd1;

            if (window_pos == WIN_LAST) begin
                window_pos  <= 11'd0;
                window_tick <= 1'b1;
                // This is the same clock edge that just issued this cycle's
                // per-cell increments above (nonblocking assignments to the
                // same cnt[i] registers). Verilog resolves multiple
                // nonblocking assignments to one signal within a single
                // always-block invocation by letting the last one issued in
                // procedural order win -- so a bare `cnt[i]` read here would
                // see the STALE pre-increment value (the increment loop's
                // effect is only visible starting next edge, and this same
                // block's `cnt[i] <= 0` below would clobber it anyway).
                // The golden model increments cnt[cell] in plain C++ (takes
                // effect immediately) and THEN classifies from the
                // post-increment value on the boundary cycle. To match that,
                // explicitly fold this cycle's own would-be increment into
                // the classification comparison rather than reading cnt[i]
                // bare.
                for (row = 0; row < 10; row = row + 1)
                    for (col = 0; col < 11; col = col + 1) begin
                        /* verilator lint_off WIDTHEXPAND */
                        if ((cnt[row*11+col] + ((rowsel[row] && rowdata[col]) ? 11'd1 : 11'd0)) >= BRIGHT_MIN)
                            levels[(row*11+col)*2 +: 2] <= 2'd2;
                        else if ((cnt[row*11+col] + ((rowsel[row] && rowdata[col]) ? 11'd1 : 11'd0)) >= DIM_MIN)
                            levels[(row*11+col)*2 +: 2] <= 2'd1;
                        else
                            levels[(row*11+col)*2 +: 2] <= 2'd0;
                        /* verilator lint_on WIDTHEXPAND */
                        cnt[row*11+col] <= 11'd0;
                    end
            end else
                window_pos <= window_pos + 11'd1;
        end
    end
endmodule
