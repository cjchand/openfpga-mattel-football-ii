// src/pps41_core.v
module pps41_core (
    input  wire        clk,
    input  wire        rst_n,
    output wire [10:0] rom_addr,
    input  wire [7:0]  rom_data
);
    reg [10:0] pc;

    wire lfsr_feed_seed = (pc[5:1] == 5'b0);
    wire lfsr_feed = lfsr_feed_seed ^ (pc[1] ^ pc[0]);
    wire [10:0] pc_next = {pc[10:6], lfsr_feed, pc[5:1]};

    assign rom_addr = pc;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            pc <= 11'h0;
        end else begin
            pc <= pc_next;
        end
    end
endmodule
