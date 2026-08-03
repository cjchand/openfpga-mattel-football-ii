// src/rom_loader.v
//
// APF bridge-loaded ROM for the Football II core. The SD-card-supplied
// file (development-assets/b8000-12 locally; the same 1536-byte dense
// dump gets copied to Assets/<platform>/common/<filename> on the Pocket)
// is written verbatim, one 32-bit word at a time, starting at bridge
// address SLOT_BASE. The CPU's 11-bit address space mirrors 0x600-0x7FF
// onto 0x400-0x5FF (docs/initial-plan.md §3) -- the file itself has no
// hole, so only reads need the fold, not writes.
module rom_loader #(
    parameter [31:0] SLOT_BASE = 32'h10000000
) (
    input  wire        clk,
    input  wire        bridge_wr,
    input  wire [31:0] bridge_addr,
    input  wire [31:0] bridge_wr_data,
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

    assign rom_data = mem[rd_word_idx][byte_sel*8 +: 8];
endmodule
