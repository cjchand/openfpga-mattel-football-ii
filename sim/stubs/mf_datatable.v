// sim/stubs/mf_datatable.v
//
// Simulation stub for the Altera altsyncram-based datatable that
// core_bridge_cmd instantiates. The real megafunction cannot be
// elaborated by this simulator. This is a plain true-dual-port RAM with
// registered read, which is the behaviour the core depends on: the host
// writes interact.json variables through port A, and the core reads them
// back through port B one word at a time.
`default_nettype none
module mf_datatable (
    input  wire [7:0]  address_a,
    input  wire [7:0]  address_b,
    input  wire        clock_a,
    input  wire        clock_b,
    input  wire [31:0] data_a,
    input  wire [31:0] data_b,
    input  wire        wren_a,
    input  wire        wren_b,
    output reg  [31:0] q_a,
    output reg  [31:0] q_b
);
    reg [31:0] mem [0:255];
    integer i;
    initial for (i = 0; i < 256; i = i + 1) mem[i] = 32'd0;

    always @(posedge clock_a) begin
        if (wren_a) mem[address_a] <= data_a;
        q_a <= mem[address_a];
    end
    always @(posedge clock_b) begin
        if (wren_b) mem[address_b] <= data_b;
        q_b <= mem[address_b];
    end
endmodule
