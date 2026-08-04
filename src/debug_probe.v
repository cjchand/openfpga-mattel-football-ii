// src/debug_probe.v
//
// On-screen flight recorder for faults that only reproduce on real
// hardware.
//
// The post-kickoff lockup (tone sticks on, game stops responding) has never
// been reproduced in simulation: the CPU core matches MAME instruction for
// instruction over millions of instructions across every input scenario and
// press timing tried, so there is nothing left to inspect on this side. This
// module captures the state that would identify the fault at the moment it
// happens on the device, and paints it on screen so it can be photographed.
//
// It arms on either of two conditions:
//
//   * tone_on asserted continuously for TONE_STUCK_CE core cycles. No real
//     sound effect in this game comes close -- the longest measured in
//     simulation is ~0.72s (68k cycles).
//   * the program counter not changing for PC_STUCK_CE core cycles, which
//     means the CPU has genuinely halted rather than looping.
//
// On the first trigger it latches the PC and the sticky flags and holds
// them forever (until reset), so the display is stable to photograph rather
// than flickering through a loop.
//
// Nothing is drawn until a trigger fires, so this is inert during normal
// play and safe to leave enabled.
module debug_probe #(
    // ~2.0s and ~0.5s at the ~95kHz instruction rate.
    parameter TONE_STUCK_CE = 32'd190000,
    parameter PC_STUCK_CE   = 32'd48000
) (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        ce,
    input  wire [10:0] pc,
    input  wire        tone_on,
    input  wire        unimpl_hit,
    input  wire        int1l_hit,

    output reg         trig,        // a fault has been captured
    output reg  [10:0] pc_latched,
    output reg         cause_tone,  // which condition fired
    output reg         cause_pc,
    output reg         unimpl_seen, // sticky, independent of trig
    output reg         int1l_seen
);
    reg [31:0] tone_ce_count;
    reg [31:0] pc_ce_count;
    reg [10:0] pc_prev;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            trig          <= 1'b0;
            pc_latched    <= 11'd0;
            cause_tone    <= 1'b0;
            cause_pc      <= 1'b0;
            unimpl_seen   <= 1'b0;
            int1l_seen    <= 1'b0;
            tone_ce_count <= 32'd0;
            pc_ce_count   <= 32'd0;
            pc_prev       <= 11'd0;
        end else begin
            if (unimpl_hit) unimpl_seen <= 1'b1;
            if (int1l_hit)  int1l_seen  <= 1'b1;

            if (ce) begin
                pc_prev <= pc;

                // Saturating counters: once a cause has fired we stop
                // counting it, so a later, unrelated long tone cannot
                // overwrite the first capture.
                if (!tone_on) tone_ce_count <= 32'd0;
                else if (tone_ce_count < TONE_STUCK_CE) tone_ce_count <= tone_ce_count + 32'd1;

                if (pc != pc_prev) pc_ce_count <= 32'd0;
                else if (pc_ce_count < PC_STUCK_CE) pc_ce_count <= pc_ce_count + 32'd1;

                if (!trig) begin
                    if (tone_on && tone_ce_count == TONE_STUCK_CE - 32'd1) begin
                        trig       <= 1'b1;
                        cause_tone <= 1'b1;
                        pc_latched <= pc;
                    end else if (pc_ce_count == PC_STUCK_CE - 32'd1) begin
                        trig       <= 1'b1;
                        cause_pc   <= 1'b1;
                        pc_latched <= pc;
                    end
                end
            end
        end
    end
endmodule
