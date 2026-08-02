// src/pps41_decode.v
module pps41_decode (
    input  wire [7:0] op,
    input  wire [7:0] prev_op,
    input  wire [7:0] prev2_op,
    output wire        is_tr,          // op itself is a TR prefix byte
    output wire        prev_is_tr,     // prev_op was a TR prefix
    output wire        prev2_is_tr,    // prev2_op was also a TR prefix (3-byte form)
    output wire [3:0]  op_hi,          // op & 0xF0, as a 4-bit selector (op[7:4])
    output wire [1:0]  op_lo2,         // op & 0x3, common 2-bit immediate field
    output wire [3:0]  op_lo4,         // op & 0xF, common 4-bit immediate field
    output wire [5:0]  op_lo6,         // op & 0x3F, jump-target field
    output wire [7:0]  op_fc           // op & 0xFC, for the second-tier dispatch group
);
    assign is_tr      = (op[7:4] == 4'h3);
    assign prev_is_tr  = (prev_op[7:4] == 4'h3);
    assign prev2_is_tr  = (prev2_op[7:4] == 4'h3);
    assign op_hi   = op[7:4];
    assign op_lo2  = op[1:0];
    assign op_lo4  = op[3:0];
    assign op_lo6  = op[5:0];
    assign op_fc   = {op[7:2], 2'b00};
endmodule
