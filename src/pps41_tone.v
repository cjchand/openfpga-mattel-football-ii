// src/pps41_tone.v
module pps41_tone (
    input  wire       clk,
    input  wire       rst_n,
    input  wire        ios_fire,
    input  wire [3:0]  ios_a,
    input  wire         int0h_fire,
    input  wire          cycle_en,
    output wire [7:0]     tone_freq_out,
    output wire            tone_on_out,
    output wire [1:0]       spk_output_out,
    output wire [1:0]        ios_state_out
);
    reg        tone_on;
    reg [7:0]  tone_freq;
    reg [7:0]  tone_count;
    reg [1:0]  spk_output;
    reg [1:0]  ios_state;

    assign tone_freq_out   = tone_freq;
    assign tone_on_out     = tone_on;
    assign spk_output_out  = spk_output;
    assign ios_state_out   = ios_state;

    wire [1:0] next_ios_state = (ios_state == 2'd2) ? 2'd0 : (ios_state + 2'd1);
    wire       ios_arms       = ios_fire && (ios_state == 2'd1);

    // Mirrors the golden model's per-step call order exactly (see
    // sim/golden/mm77la_model.cpp's tone_cycle(st_.tone) call, which always
    // runs BEFORE that step's opcode dispatch): the free-running counter's
    // unconditional post-increment-then-compare always happens first using
    // this cycle's PRE-dispatch state, then IOS's own arm/disarm effect can
    // override tone_count on top, then INT0H's own toggle applies as an
    // ADDITIONAL toggle on the same edge (a genuine, if rare, double-toggle
    // if it happens to coincide with a tone_cycle() match -- not a bug to
    // simplify away). The match check compares the POST-increment count
    // (tone_count + 1) against tone_freq, matching golden's
    // `tone_count++; if (tone_count == tone_freq)` ordering -- comparing
    // the PRE-increment value here would fire the toggle one cycle late.
    wire [7:0] incremented_count = tone_count + 8'h01;
    wire       cycle_match       = cycle_en && tone_on && (incremented_count == tone_freq);
    wire [7:0] cycle_next_count  = cycle_match ? 8'h01 : (cycle_en ? incremented_count : tone_count);
    wire [1:0] cycle_next_spk    = cycle_match ? (spk_output ^ 2'd3) : spk_output;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            tone_on    <= 1'b0;
            tone_freq  <= 8'h00;
            tone_count <= 8'h01;
            spk_output <= 2'd2;
            ios_state  <= 2'd0;
        end else begin
            tone_count <= ios_arms ? 8'h01 : cycle_next_count;
            spk_output <= int0h_fire ? (cycle_next_spk ^ 2'd3) : cycle_next_spk;

            if (ios_fire) begin
                tone_freq <= {ios_a, tone_freq[7:4]};
                tone_on   <= ios_arms;
                ios_state <= next_ios_state;
            end
        end
    end
endmodule
