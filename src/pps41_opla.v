// src/pps41_opla.v
module pps41_opla (
    input  wire [3:0] a,
    output wire [9:0] r_out
);
    /* verilator lint_off VARHIDDEN */
    `include "pps41_opla_table.vh"
    /* verilator lint_on VARHIDDEN */
    wire [9:0] raw = opla_table(a);
    wire [9:0] out = ~raw & 10'h3FF;

    // bitswap<10>(out, 9,7,5,3,1,0,2,4,6,8): dest bit 9..0 <- src bits
    // 9,7,5,3,1,0,2,4,6,8 in that order. Transcribed literally from
    // docs/initial-plan.md section 5.2 -- do not "clean up" the pattern.
    assign r_out = {out[9], out[7], out[5], out[3], out[1], out[0], out[2], out[4], out[6], out[8]};
endmodule
