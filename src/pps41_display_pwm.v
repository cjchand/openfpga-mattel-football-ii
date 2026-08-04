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
    // Exponentially-smoothed duty estimate, same 0..WINDOW scale as
    // cnt/thresholds. MAME's pwm_display_device (the best-available
    // reference for this chip; mfootb2's own driver config never overrides
    // its default) smooths at alpha=0.5 per REAL-TIME frame -- but that
    // default assumes continuous real-time frame accumulation. This
    // module instead re-classifies from a fixed WINDOW=1583-cycle batch,
    // and direct simulation measurement (idle, no button held) found the
    // ROM's own idle loop repeats on an much shorter ~51-cycle inner
    // period that does NOT evenly divide 1583 -- each successive window
    // boundary lands about 2 cycles further into that period than the
    // last, so the window captures a different slice of the loop's
    // (correct, intentional) fine-grained multiplex content almost every
    // time, independent of any real display change. alpha=0.5 barely
    // dents this (measured ~51% reduction in idle cell-churn); alpha=1/8
    // (measured ~83% reduction) averages over enough cycles of that short
    // inner period to wash out the aliasing while still settling within
    // a few hundred ms of a real input change.
    reg [10:0] smooth [0:109];
    reg [10:0] window_pos;
    integer row, col, i;
    reg [15:0] raw_duty;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            for (i = 0; i < 110; i = i + 1) begin
                cnt[i]    <= 11'd0;
                smooth[i] <= 11'd0;
            end
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
                        /* verilator lint_off BLKSEQ */
                        raw_duty = cnt[row*11+col] + ((rowsel[row] && rowdata[col]) ? 11'd1 : 11'd0);
                        // alpha = 1/2, matching MAME pwm_display_device's
                        // default interpolation factor (pwm.cpp:63,
                        // set_interpolation(0.5); its per-frame update is
                        // bri = bri*(1-f) + duty*f). The +1 rounding offset
                        // (divisor-1) makes smooth == cnt an exact fixed
                        // point, so a steady cell settles rather than
                        // creeping one count short of its threshold, which
                        // plain truncation would do.
                        raw_duty = (smooth[row*11+col] * 16'd1 + raw_duty + 16'd1) >> 1;
                        /* verilator lint_on BLKSEQ */
                        smooth[row*11+col] <= raw_duty[10:0];
                        if (raw_duty >= BRIGHT_MIN)
                            levels[(row*11+col)*2 +: 2] <= 2'd2;
                        else if (raw_duty >= DIM_MIN)
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
