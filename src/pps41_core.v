// src/pps41_core.v
module pps41_core (
    input  wire        clk,
    input  wire        rst_n,
    output wire [10:0] rom_addr,
    /* verilator lint_off UNUSEDSIGNAL */
    input  wire [7:0]  rom_data,
    /* verilator lint_on UNUSEDSIGNAL */

    // Debug-only ports for Task 12's isolated RAM-addressing test; Task 13
    // replaces the driving of b_reg/sag with real instruction execution but
    // keeps these ports for continued unit testing.
    input  wire        dbg_b_set,
    input  wire [6:0]  dbg_b_val,
    input  wire        dbg_sag_set,
    input  wire        dbg_ram_wr,
    input  wire [3:0]  dbg_ram_wdata,
    output wire [6:0]  ram_addr,
    output wire [3:0]  ram_rdata
);
    reg [10:0] pc;
    reg [6:0]  b_reg;
    reg        sag;

    wire lfsr_feed_seed = (pc[5:1] == 5'b0);
    wire lfsr_feed = lfsr_feed_seed ^ (pc[1] ^ pc[0]);
    wire [10:0] pc_next = {pc[10:6], lfsr_feed, pc[5:1]};

    assign rom_addr = pc;
    /* verilator lint_off WIDTHEXPAND */
    assign ram_addr = sag ? {2'b11, b_reg[3:0]} : b_reg;
    /* verilator lint_on WIDTHEXPAND */

    // Sparse 96-nibble RAM map: real storage indices are picked with the
    // same bank logic as sim/golden/mm77la_model.cpp's ram_phys_index --
    // 0x00-0x3F direct (64), 0x40-0x47/0x48-0x4F/0x58-0x5F -> bank A (8),
    // 0x50-0x57 -> bank B (8), 0x60-0x67/0x68-0x6F/0x78-0x7F -> bank C (8),
    // 0x70-0x77 -> bank D (8).
    function [6:0] ram_phys_index(input [6:0] addr);
        if (addr < 7'h40) ram_phys_index = addr;
        else if (addr <= 7'h4F || (addr >= 7'h58 && addr <= 7'h5F)) ram_phys_index = 7'd64 + {3'b0, addr[2:0]};
        else if (addr <= 7'h57) ram_phys_index = 7'd72 + {3'b0, addr[2:0]};
        /* verilator lint_off CMPCONST */
        else if (addr <= 7'h6F || (addr >= 7'h78 && addr <= 7'h7F)) ram_phys_index = 7'd80 + {3'b0, addr[2:0]};
        /* verilator lint_on CMPCONST */
        else ram_phys_index = 7'd88 + {3'b0, addr[2:0]};
    endfunction

    reg [3:0] ram [0:95];
    integer i;

    assign ram_rdata = ram[ram_phys_index(ram_addr)];

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            pc <= 11'h0;
            b_reg <= 7'h0;
            sag <= 1'b0;
            for (i = 0; i < 96; i = i + 1) ram[i] <= 4'hF;
        end else begin
            pc <= pc_next;
            sag <= dbg_sag_set; // one-cycle pulse: set this cycle, visible next cycle, then clears
            if (dbg_b_set) b_reg <= dbg_b_val;
            if (dbg_ram_wr) ram[ram_phys_index(ram_addr)] <= dbg_ram_wdata;
        end
    end
endmodule
