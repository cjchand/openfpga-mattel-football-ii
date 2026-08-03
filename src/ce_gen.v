// src/ce_gen.v
//
// Derives a clock enable averaging CE_HZ from a clk running at CLK_HZ,
// using the same fractional-accumulator technique the APF template itself
// uses for its 48kHz audio MCLK (core_top.v's audgen_accum). With the
// defaults (12.288MHz core clock, ~95kHz instruction rate = 380kHz MM77LA
// oscillator / 4 phases per cycle, per docs/initial-plan.md §1), the ratio
// 95000/12288000 does NOT divide the accumulator's overflow period exactly
// -- expect a small long-run rate error (a few Hz), not zero error.
module ce_gen #(
    parameter CLK_HZ = 12288000,
    parameter CE_HZ  = 95000
) (
    input  wire clk,
    input  wire rst_n,
    output reg  ce
);
    localparam ACC_W = $clog2(CLK_HZ + CE_HZ + 1);

    reg [ACC_W-1:0] accum;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            accum <= {ACC_W{1'b0}};
            ce <= 1'b0;
        end else begin
            accum <= accum + CE_HZ[ACC_W-1:0];
            if (accum + CE_HZ[ACC_W-1:0] >= CLK_HZ[ACC_W-1:0]) begin
                accum <= accum + CE_HZ[ACC_W-1:0] - CLK_HZ[ACC_W-1:0];
                ce <= 1'b1;
            end else begin
                ce <= 1'b0;
            end
        end
    end
endmodule
