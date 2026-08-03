// src/pps41_display_mux.v
module pps41_display_mux (
    input  wire [11:0] d,
    input  wire [9:0]  r,
    output wire [9:0]  rowsel,
    output wire [10:0] rowdata
);
    assign rowsel  = d[9:0];
    assign rowdata = {r[9:7], d[11], r[6:0]};
endmodule
