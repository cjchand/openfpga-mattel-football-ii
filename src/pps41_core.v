// src/pps41_core.v
//
// Full synchronous instruction execution. This is a direct, line-by-line
// port of sim/golden/mm77la_model.cpp's step() (Tasks 4-7) into a single
// combinational "next-state" block, mirrored by a simple sequential apply
// block. Case order and case grouping intentionally match step() so the two
// can be diffed side by side.
//
// Both this RTL and the golden model (sim/golden/mm77la_model.cpp) now
// fetch exactly one ROM byte per cycle/step() call, including through a
// skip that lands on a TR-prefixed multi-byte instruction: each cycle's
// fetched byte is discarded while `skip` is set, and `skip` stays asserted
// for one more cycle whenever the discarded byte is itself a TR prefix
// (op_hi==4'h3), stopping on the first non-TR byte. Because prev_op is
// updated to `op` every single cycle (see the bottom of this block), this
// naturally ends up holding the LAST byte actually consumed by the time
// skip clears. (Prior to commit 03a6cab, the golden model's step() could
// fetch 2-3 ROM bytes in one call when a skip landed on a TR prefix, which
// was a structural difference from this RTL; that was fixed to match this
// module's one-byte-per-cycle structure, so no divergence remains here.)
module pps41_core (
    input  wire        clk,
    input  wire        rst_n,
    output wire [10:0] rom_addr,
    output wire [10:0] pc,
    input  wire [7:0]  rom_data,
    input  wire [7:0]  p_input,

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
    output wire        int1l_hit_out,
    output wire [9:0]  r_output_out,
    output wire [11:0] d_output_out,
    output wire         tone_on_result,
    output wire [7:0]    tone_freq_result,
    output wire [1:0]     spk_output_result,
    output wire [1:0]      ios_state_result,
    output wire              skisl_skip_out,
    output wire                unimpl_hit_out,
    output wire [3:0]  x_out,
    output wire [3:0]  s_out
);
    reg [10:0] pc_reg;
    reg [6:0]  b_reg;
    reg        sag;
    reg [6:0]  ram_addr_reg; // delayed-address latch; see ram_addr_eff below

    // Architectural state (Task 13): mirrors Mm77laState.
    reg [3:0]  a;
    reg [3:0]  x;           // written by LXA/XAX; carried for parity with golden state
    reg        c, c_in, c_delay;
    reg [3:0]  s;            // written by XAS (serial-out pin not modeled); carried for parity with golden state
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
    reg        unimpl_hit;
    reg [9:0]  r_output_reg;

    wire lfsr_feed_seed = (pc_reg[5:1] == 5'b0);
    wire lfsr_feed = lfsr_feed_seed ^ (pc_reg[1] ^ pc_reg[0]);
    wire [10:0] pc_next = {pc_reg[10:6], lfsr_feed, pc_reg[5:1]};

    assign rom_addr = pc_reg;
    assign pc       = pc_reg;

    // RAM address delay (m_ram_delay, docs/initial-plan.md section 2/section
    // 3): XAB/XDSK/XNSK change b_reg, but the address actually used for RAM
    // ops doesn't catch up to the new b_reg until the instruction after the
    // one immediately following -- the single instruction immediately after
    // XAB/XDSK/XNSK still reads/writes at the STALE (pre-change) address.
    // Mirrors the golden model's step()-top ram_delay consumption exactly:
    // ram_addr_reg normally resyncs to b_reg every cycle; when ram_delay is
    // pending (set by the previous instruction), that resync is skipped
    // exactly once, holding the prior address for this cycle, then the flag
    // clears (see next_ram_delay's default below) so normal resync resumes
    // next cycle. This is a pure function of registered state only (no
    // combinational loop). LBA does NOT set ram_delay (MM78 override), so
    // it has no delayed-address effect -- the very next instruction already
    // sees LBA's new b_reg via the else branch below.
    wire [6:0] ram_addr_eff = ram_delay ? ram_addr_reg : b_reg;
    /* verilator lint_off WIDTHEXPAND */
    assign ram_addr = sag ? {2'b11, b_reg[3:0]} : ram_addr_eff;
    /* verilator lint_on WIDTHEXPAND */

    assign a_out          = a;
    assign b_out          = b_reg;
    assign skip_out       = skip;
    assign c_out          = c;
    assign stack0_out     = stack0;
    assign stack1_out     = stack1;
    assign skip_count_out = skip_count;
    assign int1l_hit_out  = int1l_hit;
    assign r_output_out      = r_output_reg;
    assign d_output_out       = io_d_output;
    assign tone_on_result      = tone_on_out;
    assign tone_freq_result     = tone_freq_out;
    assign spk_output_result     = spk_output_out;
    assign ios_state_result       = ios_state_out;
    assign skisl_skip_out           = io_skisl_skip;
    assign unimpl_hit_out           = unimpl_hit;
    assign x_out = x;
    assign s_out = s;

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

    wire is_3byte = prev_is_tr && prev2_is_tr;
    wire is_2byte = prev_is_tr && !is_3byte;
    wire skip_count_active = !skip && (skip_count != 4'h0);
    wire skip_eff = skip || skip_count_active;

    wire [9:0] opla_r_out;
    pps41_opla u_opla (
        .a(a),
        .r_out(opla_r_out)
    );

    wire        ios_fire   = (op == 8'h2D) && !skip_eff && !is_2byte && !is_3byte;
    wire        int0h_fire = (op == 8'h03) && !skip_eff && !is_2byte && !is_3byte;
    wire [7:0]  tone_freq_out;
    wire        tone_on_out;
    wire [1:0]  spk_output_out;
    wire [1:0]  ios_state_out;
    pps41_tone u_tone (
        .clk(clk), .rst_n(rst_n),
        .ios_fire(ios_fire), .ios_a(a),
        .int0h_fire(int0h_fire),
        .cycle_en(1'b1),
        .tone_freq_out(tone_freq_out),
        .tone_on_out(tone_on_out),
        .spk_output_out(spk_output_out),
        .ios_state_out(ios_state_out)
    );

    wire        sos_fire = (op == 8'h70) && !skip_eff && !is_2byte && !is_3byte;
    wire        ros_fire = (op == 8'h71) && !skip_eff && !is_2byte && !is_3byte;
    wire        ioa_fire = (op == 8'h7B) && !skip_eff && !is_2byte && !is_3byte;
    wire        ox_fire  = (op == 8'h73) && !skip_eff && !is_2byte && !is_3byte;
    wire [9:0]  io_r_output;
    wire [11:0] io_d_output;
    wire        io_skisl_skip;
    wire [3:0]  io_i2c_a;
    wire [3:0]  io_ioa_a_unused;
    pps41_io u_io (
        .clk(clk), .rst_n(rst_n),
        .sos_fire(sos_fire), .ros_fire(ros_fire), .ioa_fire(ioa_fire), .ox_fire(ox_fire),
        .ram_addr(ram_addr), .a_in(a), .c_in(c_in_eff), .a_out_for_ioa(4'h0),
        .dbg_p_set(p_input), .p_set_en(1'b1),
        .r_output(io_r_output), .d_output(io_d_output),
        .skisl_skip(io_skisl_skip), .i2c_a(io_i2c_a), .ioa_a_result(io_ioa_a_unused)
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
    reg [6:0]  next_ram_addr_reg;
    reg [7:0]  next_prev_op, next_prev2_op, next_prev3_op;
    reg        next_tab_pending;
    reg        next_int1l_hit;
    reg        next_unimpl_hit;
    reg        next_ram_wr_en;
    reg [3:0]  next_ram_wr_data;

    // TAB's skip_count (docs/initial-plan.md's m_skip_count: "skips A+1
    // instructions going forward, used for jump tables"): consume one skip
    // credit per FRESH instruction fetch by reusing the exact same `skip` /
    // TR-continuation machinery below (mirrors the golden model's step()
    // change). skip_count_active is asserted only when we are not already
    // mid a skip, so a multi-byte (TR-prefixed) instruction inside the
    // skipped range counts as ONE credit (skip_count is decremented only on
    // entry; the TR-continuation re-asserts `skip` without touching
    // skip_count again). This arming check and a skip_count freshly
    // assigned by THIS SAME cycle's TAB-delayed-fire cannot race, since
    // skip_count_active reads the registered (pre-cycle) skip_count.
    //
    // skip_count and tab_pending CAN legitimately be simultaneously nonzero
    // across a cycle boundary -- e.g. back-to-back TAB;TAB: the second
    // TAB's own dispatch re-arms tab_pending for its OWN future fire, in
    // the very same cycle where the first TAB's tab_pending fire (see
    // below) sets a fresh nonzero skip_count. When that happens, the
    // instruction immediately after the second TAB gets treated as a skip
    // by skip_eff (consuming a skip_count credit) before it can reach the
    // normal dispatch path -- but the `if (skip_eff)` block already
    // independently fires a pending tab_pending in that situation (see its
    // own `if (tab_pending)` branch inside that block), so the second
    // TAB's delayed effect still fires correctly, just via that branch
    // instead of the bottom-of-cycle one. Mirrors the golden model's
    // equivalent comment and test_tab_back_to_back_fires_twice.
    reg        in_subroutine_page;
    /* verilator lint_off UNUSEDSIGNAL */
    reg [10:0] local_pc; // only bits [10:6] are read back out (T x's page/high bits)
    /* verilator lint_on UNUSEDSIGNAL */
    reg [3:0]  xchg_tmp;
    reg [3:0]  bl_val;
    reg        continue_skip;
    reg        i1sk_ovf;

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
        next_ram_delay   = 1'b0; // one-shot: consumed every cycle unless this instruction re-sets it below
        next_ram_addr_reg = ram_addr_eff; // resync-or-hold, per ram_addr_eff's definition above
        next_tab_pending = tab_pending;
        next_int1l_hit   = int1l_hit;
        next_unimpl_hit  = unimpl_hit;
        next_ram_wr_en   = 1'b0;
        next_ram_wr_data = 4'h0;

        in_subroutine_page = (pc_next[10:7] == 4'hF);
        local_pc = pc_next;
        xchg_tmp = 4'h0;
        bl_val   = 4'h0;
        continue_skip = 1'b0;
        i1sk_ovf = 1'b0;

        if (skip_eff) begin
            // ---- consumed_by_skip: discard this byte; extend through any
            // TR-prefixed continuation byte(s), spread over cycles. ----
            // If skip_count_active is what got us in here (rather than the
            // ordinary single-shot `skip`), consume exactly one credit on
            // this, the entry cycle only.
            if (skip_count_active) next_skip_count = skip_count - 4'h1;
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
                    4'h8, 4'h9, 4'hA, 4'hB: begin // TMLB
                        next_stack1 = stack0;
                        next_stack0 = pc_next;
                        next_pc = {1'b1, ~prev_op[3:0], ~op[5:0]};
                    end
                    4'hC, 4'hD, 4'hE, 4'hF: begin // TLB
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
                    4'h8, 4'h9, 4'hA, 4'hB: begin // TML
                        next_stack1 = stack0;
                        next_stack0 = pc_next;
                        next_pc = {1'b0, ~prev_op[3:0], ~op[5:0]};
                    end
                    4'hC, 4'hD, 4'hE, 4'hF: begin // TL
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
                    4'h6: begin // AISK x (x!=0); I1SK (x==0)
                        if (op_lo4 != 4'h0) begin
                            next_a    = alu_result;
                            next_skip = (op_lo4 == 4'h6) ? 1'b0 : !alu_overflow;
                        end else begin
                            {i1sk_ovf, next_a} = {1'b0, a} + {1'b0, p_input[3:0]};
                            next_skip = !i1sk_ovf;
                        end
                    end
                    4'h8, 4'h9, 4'hA, 4'hB: begin // TM x
                        if (!in_subroutine_page) begin
                            next_stack1 = stack0;
                            next_stack0 = pc_next;
                        end
                        next_pc = {5'b11111, ~op[5:0]};
                    end
                    4'hC, 4'hD, 4'hE, 4'hF: begin // T x
                        if (in_subroutine_page) local_pc[6] = 1'b0;
                        next_pc = {local_pc[10:6], ~op[5:0]};
                    end
                    default: begin
                        case (op_fc)
                            8'h08, 8'h0C: begin // EOB x -- reachable via BOTH the 0x08-0x0B and
                                // 0x0C-0x0F byte ranges (MAME mm78.cpp: `case 0x08: case 0x0c:
                                // op_eob();`), both using the low 2 bits as the immediate.
                                // Phase 1 only handled 0x08; the real ROM hits 0x0C-range EOB
                                // bytes within its first ~150 cycles (found via Phase 2's
                                // unimpl_hit instrumentation) -- this was a genuine bug, not a
                                // Phase-2-scope opcode. Suppression covers the whole 0x08-0x0F
                                // range (prev_op[7:3]==5'b00001), not just the 0x08 sub-group.
                                if (prev_op[7:3] != 5'b00001) next_b = b_reg ^ {1'b0, op_lo2, 4'b0};
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
                                    8'h01: next_skip = io_skisl_skip; // SKISL
                                    8'h02: next_skip = (c_in_eff == 1'b0); // SKNC
                                    8'h04: next_int1l_hit = 1'b1; // INT1L -- flagged no-op
                                    8'h05: begin next_c = 1'b0; next_c_in = 1'b0; end // RC -- immediate (no c_delay), docs/initial-plan.md: "RC: carry = 0"
                                    8'h06: begin next_c = 1'b1; next_c_in = 1'b1; end // SC -- immediate (no c_delay), docs/initial-plan.md: "SC: carry = 1"
                                    8'h07: next_sag = 1'b1; // SAG
                                    8'h2C: next_tab_pending = 1'b1; // TAB -- delayed fire
                                    8'h2D: ; // IOS -- side effect handled entirely by u_tone (ios_fire)
                                    8'h03: ; // INT0H -- side effect handled entirely by u_tone (int0h_fire)
                                    8'h70: ; // SOS -- side effect handled entirely by u_io (sos_fire)
                                    8'h71: ; // ROS -- side effect handled entirely by u_io (ros_fire)
                                    8'h73: ; // OX -- side effect handled entirely by u_io (ox_fire) + r_output_reg mux
                                    8'h2E: begin // RTSK
                                        next_pc     = stack0;
                                        next_stack0 = stack1;
                                        next_skip   = 1'b1;
                                    end
                                    8'h2F: begin // RT
                                        next_pc     = stack0;
                                        next_stack0 = stack1;
                                    end
                                    8'h72: ; // IX -- R-output side effect handled by r_output_reg mux
                                    8'h76: next_b = {b_reg[6:4], a}; // LBA (MM78: no ram_delay)
                                    8'h77: next_a = alu_result; // COM
                                    8'h78: next_a = io_i2c_a; // I2C
                                    8'h7B: ; // IOA -- side effect handled entirely by u_io (ioa_fire) + r_output_reg mux
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
                                    8'h74: begin // XAS -- swap A<->S (serial-out pin not modeled, unused by this game)
                                        xchg_tmp = a;
                                        next_a   = s;
                                        next_s   = xchg_tmp;
                                    end
                                    8'h75: next_x = a; // LXA
                                    8'h79: begin // XAX -- swap A<->X
                                        xchg_tmp = a;
                                        next_a   = x;
                                        next_x   = xchg_tmp;
                                    end
                                    default: next_unimpl_hit = 1'b1; // unimplemented (LXA/XAX/XAS, etc.)
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
            //
            // Gate on the REGISTERED `tab_pending` (its value from before
            // this cycle's own dispatch ran) rather than requiring
            // `op != 8'h2C` (Important #9 fix): the reference (MAME)
            // semantics fire based on "was the PREVIOUS opcode TAB",
            // checked unconditionally every cycle -- not "is the CURRENT
            // opcode something other than TAB". The old `op != 8'h2C` guard
            // suppressed firing whenever this cycle's own opcode was itself
            // a fresh TAB, so back-to-back TAB;TAB;NOP only fired once
            // instead of the correct twice. `tab_pending` here still holds
            // its pre-cycle value (combinational block reads registers, not
            // this cycle's `next_*` writes), so a fresh TAB this cycle
            // (which sets next_tab_pending=1 in the 8'h2C case above) no
            // longer masks a still-pending PRIOR TAB's fire.
            if (tab_pending) begin
                next_skip_count  = next_a + 4'h1;
                next_a           = 4'hF;
                // Only clear if this cycle's own opcode didn't just re-arm
                // it (op==8'h2C already set next_tab_pending=1 above; leave
                // that armed for ITS delayed fire next cycle).
                if (op != 8'h2C) next_tab_pending = 1'b0;
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
            ram_addr_reg <= 7'h0;
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
            unimpl_hit  <= 1'b0;
            r_output_reg <= 10'h3FF;
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
            ram_addr_reg <= next_ram_addr_reg;
            prev_op     <= next_prev_op;
            prev2_op    <= next_prev2_op;
            prev3_op    <= next_prev3_op;
            tab_pending <= next_tab_pending;
            int1l_hit   <= next_int1l_hit;
            unimpl_hit  <= next_unimpl_hit;
            // IOA/OX are computed directly here (not routed through u_io's
            // own r_out_reg, which is a SEPARATE register only used for
            // u_io's standalone unit test) -- chaining io_r_output (a wire
            // tied to another module's register) into this register on the
            // same posedge would introduce a spurious extra cycle of delay
            // (the classic double-register cascade), since u_io's own
            // nonblocking update hasn't taken effect yet within this same
            // clock edge. r_output must be a single authoritative register;
            // IX fully replaces it, IOA/OX replace one 5-bit half each.
            r_output_reg <= (op == 8'h72 && !skip_eff && !is_2byte && !is_3byte) ? opla_r_out
                           : ioa_fire ? {r_output_reg[9:5], c_in_eff, a}
                           : ox_fire  ? {c_in_eff, a, r_output_reg[4:0]}
                           : r_output_reg;
            if (dbg_ram_wr) ram[ram_phys_index(ram_addr)] <= dbg_ram_wdata;
            else if (next_ram_wr_en) ram[ram_phys_index(ram_addr)] <= next_ram_wr_data;
        end
    end
endmodule
