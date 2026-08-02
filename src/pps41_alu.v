// src/pps41_alu.v
module pps41_alu (
    input  wire [3:0] a,
    input  wire [3:0] mem,
    input  wire        c_in,
    input  wire [2:0]  op_sel,       // 0=A(add), 1=AC(add+carry), 2=COM, 3=AISK
    input  wire [3:0]  aisk_imm,
    output reg  [3:0]  result,
    output reg          carry_out,
    output reg           overflow      // for AISK: 1 if a+aisk_imm >= 0x10
);
    localparam OP_ADD  = 3'd0;
    localparam OP_ADC  = 3'd1;
    localparam OP_COM  = 3'd2;
    localparam OP_AISK = 3'd3;

    always @(*) begin
        result    = 4'h0;
        carry_out = 1'b0;
        overflow  = 1'b0;
        case (op_sel)
            OP_ADD: begin
                {carry_out, result} = {1'b0, a} + {1'b0, mem};
            end
            OP_ADC: begin
                {carry_out, result} = {1'b0, a} + {1'b0, mem} + {4'b0, c_in};
            end
            OP_COM: begin
                result = a ^ 4'hF;
            end
            OP_AISK: begin
                {overflow, result} = {1'b0, a} + {1'b0, aisk_imm};
            end
            default: ;
        endcase
    end
endmodule
