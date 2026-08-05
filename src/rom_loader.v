// src/rom_loader.v
//
// APF bridge-loaded ROM for the Football II core. The SD-card-supplied
// file (development-assets/b8000-12 locally; the same 1536-byte dense
// dump gets copied to Assets/<platform>/common/<filename> on the Pocket)
// is written verbatim, one 32-bit word at a time, starting at bridge
// address SLOT_BASE. The CPU's 11-bit address space mirrors 0x600-0x7FF
// onto 0x400-0x5FF (docs/initial-plan.md §3) -- the file itself has no
// hole, so only reads need the fold, not writes.
// The read side is REGISTERED, on its own clock (rd_clk = the core clock),
// so that Quartus can infer an M10K block for `mem` instead of building it
// out of logic. A combinational read cannot map to an M10K -- the block's
// read address is registered inside the hardware -- so the array landed in
// ALMs, and the 384-to-1 word multiplexer needed to answer instantly cost
// 4,959 of them: about half the entire core, to hold 12,288 bits, while
// 307 of the device's 308 memory blocks sat unused.
//
// The one cycle of read latency this introduces is free here. rom_addr comes
// from pps41_core's pc_reg, which only moves on a ce pulse, and ce fires
// every ~129 core clocks; rom_data is registered one core clock after the
// address changes and is not consumed until the following ce edge. That is
// ~128 clocks of margin.
module rom_loader #(
    parameter [31:0] SLOT_BASE = 32'h10000000
) (
    input  wire        clk,      // write clock (APF bridge, clk_74a)
    input  wire        bridge_wr,
    input  wire [31:0] bridge_addr,
    input  wire [31:0] bridge_wr_data,
    input  wire        rd_clk,   // read clock (core clock)
    input  wire [10:0] rom_addr,
    output wire [7:0]  rom_data
);
    // 1536 bytes = 384 32-bit words, addressed 0..383 (9 bits)
    reg [31:0] mem [0:383];

    wire in_slot = (bridge_addr[31:24] == SLOT_BASE[31:24]);
    wire [8:0] wr_word_idx = bridge_addr[10:2];

    always @(posedge clk)
        if (bridge_wr && in_slot)
            mem[wr_word_idx] <= bridge_wr_data;

    // mirror fold: CPU addresses 0x600-0x7FF read the same bytes as
    // 0x400-0x5FF (real ROM content only exists in 0x000-0x5FF)
    wire [10:0] dense_addr = (rom_addr >= 11'h600) ? (rom_addr - 11'h200) : rom_addr;
    wire [8:0] rd_word_idx = dense_addr[10:2];
    wire [1:0] byte_sel = dense_addr[1:0];

    // APF packs each bridge write big-endian (file byte 0 of a 4-byte
    // group lands in bits[31:24], not bits[7:0]) -- confirmed on real
    // hardware in the sibling FB1 project via a debug readback: byte_sel==0
    // was returning file byte 3 instead of file byte 0. Select from the
    // high end to match.
    wire [1:0] byte_sel_rev = ~byte_sel;

    // Only the WORD read is registered -- that is the part that has to be
    // synchronous for M10K inference. The byte select is registered alongside
    // it (so it stays aligned with the word it belongs to) and applied
    // combinationally afterwards: a 4-to-1 byte mux costs almost nothing,
    // whereas folding the byte select into the memory address would have
    // meant a 1536-deep x 8-bit array instead of 384 x 32.
    reg [31:0] rd_word;
    reg [1:0]  byte_sel_rev_q;
    always @(posedge rd_clk) begin
        rd_word        <= mem[rd_word_idx];
        byte_sel_rev_q <= byte_sel_rev;
    end

    assign rom_data = rd_word[byte_sel_rev_q*8 +: 8];
endmodule
