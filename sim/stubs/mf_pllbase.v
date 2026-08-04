// sim/stubs/mf_pllbase.v
//
// Simulation stub for the Altera PLL IP that core_top instantiates.
// The real megafunction cannot be elaborated by this simulator, and the
// logic under test does not care about the exact output frequency -- only
// that there is a second, slower clock domain to cross into.
//
// Divide-by-6 gives ~12.375MHz from 74.25MHz rather than the real
// 12.288MHz. That is deliberate: the ratio only sets the CPU's instruction
// rate (via ce_gen), and every property this testbench asserts -- reset
// gating, clock-domain crossing, datatable reads, ROM fetch -- is
// independent of it. Keeping the divider an integer keeps the test
// deterministic.
`default_nettype none
module mf_pllbase (
    input  wire refclk,
    input  wire rst,
    output wire outclk_0,
    output wire outclk_1,
    output wire outclk_2,
    output wire outclk_3,
    output wire outclk_4,
    output wire locked
);
    reg [2:0] div = 3'd0;
    reg       clk_slow = 1'b0;
    always @(posedge refclk) begin
        if (rst) begin
            div      <= 3'd0;
            clk_slow <= 1'b0;
        end else if (div == 3'd2) begin
            div      <= 3'd0;
            clk_slow <= ~clk_slow;
        end else begin
            div <= div + 3'd1;
        end
    end
    assign outclk_0 = clk_slow;
    assign outclk_1 = clk_slow; // 90deg phase is irrelevant to these tests
    assign outclk_2 = 1'b0;
    assign outclk_3 = 1'b0;
    assign outclk_4 = 1'b0;
    assign locked   = 1'b1;
endmodule
