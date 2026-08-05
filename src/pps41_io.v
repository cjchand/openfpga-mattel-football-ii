// src/pps41_io.v
module pps41_io (
    input  wire        clk,
    input  wire        rst_n,

    input  wire        sos_fire,
    input  wire        ros_fire,
    input  wire        ioa_fire,
    input  wire         ox_fire,
    input  wire [6:0]   ram_addr,
    input  wire [3:0]    a_in,
    input  wire            c_in,
    input  wire [3:0]        a_out_for_ioa,

    input  wire [11:0]        d_input,   // external drivers on the D bus
    input  wire [7:0]         dbg_p_set,
    input  wire                p_set_en,

    output wire [9:0]           r_output,
    output wire [11:0]           d_output,
    output wire                    skisl_skip,
    output wire [3:0]               i2c_a,
    output wire [3:0]                ioa_a_result
);
    reg [9:0]  r_out_reg;
    reg [11:0] d_out_reg;
    reg [7:0]  p_input;

    assign r_output   = r_out_reg;
    assign d_output   = d_out_reg;
    assign i2c_a      = (~p_input[7:4]) & 4'hF;
    assign ioa_a_result = a_out_for_ioa;

    wire b7_high = ram_addr[6];
    wire [3:0] bl = ram_addr[3:0];
    wire       bl_valid = (bl < 4'hC);

    // SKISL reads the pin, which is d_output OR whatever is driving it
    // externally -- MAME: !BIT((m_d_output | m_read_d()) & m_d_mask, bl).
    // The ROM releases DIO10 with ROS and then tests it here to read the
    // PRO 1 / PRO 2 difficulty switch.
    wire [11:0] d_pin_state = d_out_reg | d_input;
    assign skisl_skip = (!b7_high && bl_valid) ? ~d_pin_state[bl] : 1'b0;

    // This block is deliberately NOT qualified by the core's ce, unlike
    // pps41_tone's (see the ios_fire comment in pps41_core.v). On the device
    // `op` -- and therefore each *_fire strobe -- is stable for the whole
    // ~129-clock ce period, so every assignment below is applied ~129 times
    // per instruction. That is harmless here and only here, because every one
    // of them is idempotent: they set or clear a D bit, or load R from A/C.
    // Applying "d_out_reg[bl] <= 1" 129 times is the same as applying it once.
    //
    // Anything added to this block that is NOT idempotent -- a shift, a
    // toggle, an increment -- must be qualified by ce, or it will silently
    // work in every ce=1 testbench and misbehave on hardware in a way that
    // depends on ce_gen's fractional 129/130 period.
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            r_out_reg <= 10'h3FF;
            d_out_reg <= 12'h000;
            p_input   <= 8'h00;
        end else begin
            if (p_set_en) p_input <= dbg_p_set;

            if (sos_fire && !b7_high && bl_valid) d_out_reg[bl] <= 1'b1;
            if (ros_fire && !b7_high && bl_valid) d_out_reg[bl] <= 1'b0;

            if (ioa_fire) r_out_reg[4:0]  <= {c_in, a_in};
            if (ox_fire)  r_out_reg[9:5]  <= {c_in, a_in};
        end
    end
endmodule
