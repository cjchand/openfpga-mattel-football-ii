// src/pps41_core.v
//
// Full synchronous instruction execution. This is a direct, line-by-line
// port of sim/golden/mm77la_model.cpp's step() (Tasks 4-7) into a single
// combinational "next-state" block, mirrored by a simple sequential apply
// block. Case order and case grouping intentionally match step() so the two
// can be diffed side by side.
//
// One structural difference from the golden model, required by this
// module's single-byte-per-cycle ROM interface (rom_addr/rom_data give
// exactly one opcode byte per clock): the golden model's "consumed_by_skip"
// path can fetch 2-3 ROM bytes within a single step() call when a skip
// lands on a TR-prefixed multi-byte instruction. This RTL instead spreads
// that same skip-continuation over multiple clock cycles: while `skip` is
// set, each cycle's fetched byte is discarded, and `skip` stays asserted
// for one more cycle whenever the discarded byte is itself a TR prefix
// (op_hi==4'h3), stopping on the first non-TR byte. Because prev_op is
// updated to `op` every single cycle (see the bottom of this block), this
// naturally ends up holding the LAST byte actually consumed by the time
// skip clears -- exactly the Task 7 prev_op-not-stale fix, achieved for
// free by the per-cycle structure rather than the golden model's manual
// last_byte bookkeeping.
module pps41_core (
    input  wire        clk,
    input  wire        rst_n,
    output wire [10:0] rom_addr,
    output wire [10:0] pc,
    input  wire [7:0]  rom_data,

    // Debug-only ports for Task 12's isolated RAM-addressing test; still
    // wired for continued unit testing (core-ram-test / core-pc-test), and
    // given priority over opcode-driven updates when asserted. The
    // lockstep harness (sim/pps41_core_tb.cpp) always holds these at 0, so
    // in that context b_reg/sag/ram are driven purely by real execution.
    input  wire        dbg_b_set,
    input  wire [6:0]  dbg_b_val,
    input  wire        dbg_sag_set,
    input  wire        dbg_ram_wr,
    input  wire [3:0]  dbg_ram_wdata,
    output wire [6:0]  ram_addr,
    output wire [3:0]  ram_rdata,

    // Architectural outputs for the lockstep testbench.
    output wire [3:0]  a_out,
    output wire [6:0]  b_out,
    output wire        skip_out,
    output wire        c_out,
    output wire [10:0] stack0_out,
    output wire [10:0] stack1_out,
    output wire [3:0]  skip_count_out,
    output wire        int1l_hit_out
);
    reg [10:0] pc_reg;
    reg [6:0]  b_reg;
    reg        sag;

    // Architectural state (Task 13): mirrors Mm77laState.
    reg [3:0]  a;
    reg [3:0]  x;           // unused by any implemented opcode; carried for parity with golden state
    reg        c, c_in, c_delay;
    reg [3:0]  s;            // unused by any implemented opcode; carried for parity with golden state
    reg [10:0] stack0, stack1;
    reg        skip;
    reg [3:0]  skip_count;
    reg        ram_delay;
    /* verilator lint_off UNUSEDSIGNAL */
    reg [7:0]  prev_op, prev2_op, prev3_op; // prev3_op mirrors golden state but is not
                                             // read by any implemented dispatch tier
    /* verilator lint_on UNUSEDSIGNAL */
    reg        tab_pending;
    reg        int1l_hit;

    wire lfsr_feed_seed = (pc_reg[5:1] == 5'b0);
    wire lfsr_feed = lfsr_feed_seed ^ (pc_reg[1] ^ pc_reg[0]);
    wire [10:0] pc_next = {pc_reg[10:6], lfsr_feed, pc_reg[5:1]};

    assign rom_addr = pc_reg;
    assign pc       = pc_reg;
    /* verilator lint_off WIDTHEXPAND */
    assign ram_addr = sag ? {2'b11, b_reg[3:0]} : b_reg;
    /* verilator lint_on WIDTHEXPAND */

    assign a_out          = a;
    assign b_out          = b_reg;
    assign skip_out       = skip;
    assign c_out          = c;
    assign stack0_out     = stack0;
    assign stack1_out     = stack1;
    assign skip_count_out = skip_count;
    assign int1l_hit_out  = int1l_hit;

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

    // ---- Decode (Task 9) and ALU (Task 10) ----
    wire [7:0] op = rom_data;

    wire        is_tr, prev_is_tr, prev2_is_tr;
    wire [3:0]  op_hi;
    wire [1:0]  op_lo2;
    wire [3:0]  op_lo4;
    /* verilator lint_off UNUSEDSIGNAL */
    wire [5:0]  op_lo6; // exposed by pps41_decode but this cascade uses op[5:0] directly
    /* verilator lint_on UNUSEDSIGNAL */
    wire [7:0]  op_fc;

    pps41_decode u_decode (
        .op(op),
        .prev_op(prev_op),
        .prev2_op(prev2_op),
        .is_tr(is_tr),
        .prev_is_tr(prev_is_tr),
        .prev2_is_tr(prev2_is_tr),
        .op_hi(op_hi),
        .op_lo2(op_lo2),
        .op_lo4(op_lo4),
        .op_lo6(op_lo6),
        .op_fc(op_fc)
    );

    localparam ALU_ADD  = 3'd0;
    localparam ALU_ADC  = 3'd1;
    localparam ALU_COM  = 3'd2;
    localparam ALU_AISK = 3'd3;

    reg [2:0] alu_op_sel;
    always @(*) begin
        if (op == 8'h7C || op == 8'h7D) alu_op_sel = ALU_ADC;
        else if (op == 8'h77)           alu_op_sel = ALU_COM;
        else if (op_hi == 4'h6 && op_lo4 != 4'h0) alu_op_sel = ALU_AISK;
        else alu_op_sel = ALU_ADD;
    end

    // c_in promotion: applied unconditionally at the "top" of every cycle's
    // logic (Task 6/13 fix -- must NOT be gated by which opcode runs this
    // cycle, and must be visible to THIS cycle's own AC/ACSK/SKNC reads).
    wire c_in_eff = c_delay ? c : c_in;

    wire [3:0] alu_result;
    wire       alu_carry_out;
    wire       alu_overflow;

    pps41_alu u_alu (
        .a(a),
        .mem(ram_rdata),
        .c_in(c_in_eff),
        .op_sel(alu_op_sel),
        .aisk_imm(op_lo4),
        .result(alu_result),
        .carry_out(alu_carry_out),
        .overflow(alu_overflow)
    );

    // ---- Next-state combinational block (direct port of step()) ----
    reg [10:0] next_pc;
    reg [3:0]  next_a;
    reg [6:0]  next_b;
    reg [3:0]  next_x;
    reg        next_sag;
    reg        next_c, next_c_in, next_c_delay;
    reg [3:0]  next_s;
    reg [10:0] next_stack0, next_stack1;
    reg        next_skip;
    reg [3:0]  next_skip_count;
    reg        next_ram_delay;
    reg [7:0]  next_prev_op, next_prev2_op, next_prev3_op;
    reg        next_tab_pending;
    reg        next_int1l_hit;
    reg        next_ram_wr_en;
    reg [3:0]  next_ram_wr_data;

    reg        is_3byte, is_2byte;
    reg        in_subroutine_page;
    /* verilator lint_off UNUSEDSIGNAL */
    reg [10:0] local_pc; // only bits [10:6] are read back out (T x's page/high bits)
    /* verilator lint_on UNUSEDSIGNAL */
    reg [3:0]  xchg_tmp;
    reg [3:0]  bl_val;
    reg        continue_skip;

    always @(*) begin
        // ---- defaults: hold current value / no side effects ----
        next_pc          = pc_next;
        next_a           = a;
        next_b           = b_reg;
        next_x           = x;
        next_sag         = 1'b0;
        next_c           = c;
        next_c_in        = c_in_eff;   // promoted every cycle, per the Task 6 fix
        next_c_delay     = 1'b0;
        next_s           = s;
        next_stack0      = stack0;
        next_stack1      = stack1;
        next_skip        = 1'b0;
        next_skip_count  = skip_count;
        next_ram_delay   = ram_delay;
        next_tab_pending = tab_pending;
        next_int1l_hit   = int1l_hit;
        next_ram_wr_en   = 1'b0;
        next_ram_wr_data = 4'h0;

        is_3byte = prev_is_tr && prev2_is_tr;
        is_2byte = prev_is_tr && !is_3byte;

        in_subroutine_page = (pc_next[10:7] == 4'hF);
        local_pc = pc_next;
        xchg_tmp = 4'h0;
        bl_val   = 4'h0;
        continue_skip = 1'b0;

        if (skip) begin
            // ---- consumed_by_skip: discard this byte; extend through any
            // TR-prefixed continuation byte(s), spread over cycles. ----
            continue_skip = is_tr; // op_hi==4'h3
            next_skip = continue_skip;
            if (!continue_skip) begin
                if (tab_pending) begin
                    next_skip_count  = a + 4'h1;
                    next_a           = 4'hF;
                    next_tab_pending = 1'b0;
                end
            end
        end else begin
            if (is_3byte) begin
                case (op_hi)
                    4'h8: begin // TMLB
                        next_stack1 = stack0;
                        next_stack0 = pc_next;
                        next_pc = {1'b1, ~prev_op[3:0], ~op[5:0]};
                    end
                    4'hC: begin // TLB
                        next_pc = {1'b1, ~prev_op[3:0], ~op[5:0]};
                    end
                    default: ; // no-op
                endcase
            end else if (is_2byte) begin
                case (op_hi)
                    4'h3: ; // another TR -- enables 3-byte dispatch next cycle
                    4'h4: begin // SKBEI x
                        next_skip = (b_reg[3:0] == op[3:0]);
                    end
                    4'h6: begin // SKAEI x (op==0x60 exactly is illegal, treated as no-op)
                        if (op != 8'h60) next_skip = (a == ~op[3:0]);
                    end
                    4'h8: begin // TML
                        next_stack1 = stack0;
                        next_stack0 = pc_next;
                        next_pc = {1'b0, ~prev_op[3:0], ~op[5:0]};
                    end
                    4'hC: begin // TL
                        next_pc = {1'b0, ~prev_op[3:0], ~op[5:0]};
                    end
                    default: ; // no-op
                endcase
            end else begin
                case (op_hi)
                    4'h1: begin // LB x
                        if (prev_op[7:4] != 4'h1) next_b = {3'b0, op[3:0]};
                    end
                    4'h3: ; // TR prefix, no direct effect
                    4'h4: begin // LAI x
                        if (prev_op[7:4] != 4'h4) next_a = op[3:0];
                    end
                    4'h6: begin // AISK x (x!=0); I1SK (x==0) is a no-op
                        if (op_lo4 != 4'h0) begin
                            next_a    = alu_result;
                            next_skip = (op_lo4 == 4'h6) ? 1'b0 : !alu_overflow;
                        end
                    end
                    4'h8: begin // TM x
                        if (!in_subroutine_page) begin
                            next_stack1 = stack0;
                            next_stack0 = pc_next;
                        end
                        next_pc = {5'b11111, ~op[5:0]};
                    end
                    4'hC: begin // T x
                        if (in_subroutine_page) local_pc[6] = 1'b0;
                        next_pc = {local_pc[10:6], ~op[5:0]};
                    end
                    default: begin
                        case (op_fc)
                            8'h08: begin // EOB x
                                if (prev_op[7:2] != 6'h02) next_b = b_reg ^ {1'b0, op_lo2, 4'b0};
                            end
                            8'h20: begin // SB x
                                next_ram_wr_en   = 1'b1;
                                next_ram_wr_data = ram_rdata | (4'b0001 << op_lo2);
                            end
                            8'h24: begin // RB x
                                next_ram_wr_en   = 1'b1;
                                next_ram_wr_data = ram_rdata & ~(4'b0001 << op_lo2);
                            end
                            8'h28: begin // SKBF x
                                next_skip = (ram_rdata & (4'b0001 << op_lo2)) == 4'h0;
                            end
                            8'h50: begin // L x
                                next_a = ram_rdata;
                                next_b = b_reg ^ {1'b0, op_lo2, 4'b0};
                            end
                            8'h54: begin // XNSK x
                                next_ram_wr_en   = 1'b1;
                                next_ram_wr_data = a;
                                next_a           = ram_rdata;
                                bl_val           = (b_reg[3:0] + 4'h1) & 4'hF;
                                next_b           = ({b_reg[6:4], bl_val}) ^ {1'b0, op_lo2, 4'b0};
                                next_ram_delay    = 1'b1;
                                next_skip         = (bl_val == 4'h0);
                            end
                            8'h58: begin // XDSK x
                                next_ram_wr_en   = 1'b1;
                                next_ram_wr_data = a;
                                next_a           = ram_rdata;
                                bl_val           = (b_reg[3:0] - 4'h1) & 4'hF;
                                next_b           = ({b_reg[6:4], bl_val}) ^ {1'b0, op_lo2, 4'b0};
                                next_ram_delay    = 1'b1;
                                next_skip         = (bl_val == 4'hF);
                            end
                            8'h5C: begin // X x
                                next_ram_wr_en   = 1'b1;
                                next_ram_wr_data = a;
                                next_a           = ram_rdata;
                                next_b           = b_reg ^ {1'b0, op_lo2, 4'b0};
                            end
                            default: begin
                                case (op)
                                    8'h00: ; // NOP
                                    8'h02: next_skip = (c_in_eff == 1'b0); // SKNC
                                    8'h04: next_int1l_hit = 1'b1; // INT1L -- flagged no-op
                                    8'h07: next_sag = 1'b1; // SAG
                                    8'h2C: next_tab_pending = 1'b1; // TAB -- delayed fire
                                    8'h2E: begin // RTSK
                                        next_pc     = stack0;
                                        next_stack0 = stack1;
                                        next_skip   = 1'b1;
                                    end
                                    8'h2F: begin // RT
                                        next_pc     = stack0;
                                        next_stack0 = stack1;
                                    end
                                    8'h72: ; // IX -- stub, no PLA wiring this phase
                                    8'h76: next_b = {b_reg[6:4], a}; // LBA (MM78: no ram_delay)
                                    8'h77: next_a = alu_result; // COM
                                    8'h7A: begin // XAB
                                        xchg_tmp       = a;
                                        next_a         = b_reg[3:0];
                                        next_b         = {b_reg[6:4], xchg_tmp};
                                        next_ram_delay = 1'b1;
                                    end
                                    8'h7C: begin // AC
                                        next_a       = alu_result;
                                        next_c       = alu_carry_out;
                                        next_c_delay = 1'b1;
                                    end
                                    8'h7D: begin // ACSK
                                        next_a       = alu_result;
                                        next_c       = alu_carry_out;
                                        next_c_delay = 1'b1;
                                        next_skip    = (alu_carry_out != 1'b0);
                                    end
                                    8'h7E: next_a = alu_result; // A
                                    8'h7F: next_skip = (a == ram_rdata); // SKMEA
                                    default: ; // unimplemented opcodes fall through as NOP
                                endcase
                            end
                        endcase
                    end
                endcase
            end

            // TAB's effect fires after the FOLLOWING opcode has executed;
            // applies uniformly regardless of which dispatch tier the
            // following opcode went through, exactly like the golden
            // model's bottom-of-step() check.
            if (tab_pending && op != 8'h2C) begin
                next_skip_count  = next_a + 4'h1;
                next_a           = 4'hF;
                next_tab_pending = 1'b0;
            end
        end

        next_prev_op  = op;
        next_prev2_op = prev_op;
        next_prev3_op = prev2_op;
    end

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            pc_reg          <= 11'h0;
            b_reg       <= 7'h0;
            sag         <= 1'b0;
            a           <= 4'h0;
            x           <= 4'h0;
            c           <= 1'b0;
            c_in        <= 1'b0;
            c_delay     <= 1'b0;
            s           <= 4'h0;
            stack0      <= 11'h0;
            stack1      <= 11'h0;
            skip        <= 1'b0;
            skip_count  <= 4'h0;
            ram_delay   <= 1'b0;
            prev_op     <= 8'h0;
            prev2_op    <= 8'h0;
            prev3_op    <= 8'h0;
            tab_pending <= 1'b0;
            int1l_hit   <= 1'b0;
            for (i = 0; i < 96; i = i + 1) ram[i] <= 4'hF;
        end else begin
            pc_reg          <= next_pc;
            a           <= next_a;
            b_reg       <= dbg_b_set ? dbg_b_val : next_b;
            sag         <= dbg_sag_set ? 1'b1 : next_sag;
            x           <= next_x;
            c           <= next_c;
            c_in        <= next_c_in;
            c_delay     <= next_c_delay;
            s           <= next_s;
            stack0      <= next_stack0;
            stack1      <= next_stack1;
            skip        <= next_skip;
            skip_count  <= next_skip_count;
            ram_delay   <= next_ram_delay;
            prev_op     <= next_prev_op;
            prev2_op    <= next_prev2_op;
            prev3_op    <= next_prev3_op;
            tab_pending <= next_tab_pending;
            int1l_hit   <= next_int1l_hit;
            if (dbg_ram_wr) ram[ram_phys_index(ram_addr)] <= dbg_ram_wdata;
            else if (next_ram_wr_en) ram[ram_phys_index(ram_addr)] <= next_ram_wr_data;
        end
    end
endmodule
