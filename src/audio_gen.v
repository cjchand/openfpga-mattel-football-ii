// src/audio_gen.v
//
// Real I2S audio from pps41_tone's 2-bit speaker level, reusing the APF
// template's own MCLK/SCLK/LRCK timing (core_top.v's audgen_* generator,
// which normally drives silence) and adding actual sample shifting. The
// 2-bit level encodes docs/initial-plan.md §7's speaker_levels table
// ({0.0, +1.0, -1.0, 0.0} for level values 0..3). No DC-blocking filter or
// volume scaling -- deferred polish, this targets clear audibility only.
module audio_gen (
    input  wire       clk_74a,
    input  wire [1:0] level,
    output wire       audio_mclk,
    output wire       audio_sclk,
    output reg        audio_lrck,
    output reg        audio_dac
);
    // MCLK ~= 12.288MHz via fractional accumulator (identical constants to
    // the template's own silence generator in core_top.v)
    reg  [21:0] audgen_accum;
    reg         audgen_mclk_r;
    localparam [21:0] CYCLE_48KHZ = 22'd122880 * 2;
    always @(posedge clk_74a) begin
        audgen_accum <= audgen_accum + CYCLE_48KHZ;
        if (audgen_accum >= 22'd742500) begin
            audgen_mclk_r <= ~audgen_mclk_r;
            audgen_accum <= audgen_accum - 22'd742500 + CYCLE_48KHZ;
        end
    end
    assign audio_mclk = audgen_mclk_r;

    // SCLK = MCLK/4
    reg [1:0] mclk_divider;
    always @(posedge audgen_mclk_r) mclk_divider <= mclk_divider + 1'b1;
    assign audio_sclk = mclk_divider[1];

    // level -> signed 16-bit sample: {0.0, +1.0, -1.0, 0.0} per
    // initial-plan.md's speaker_levels table
    wire signed [15:0] sample = (level == 2'd1) ? 16'sd12000
                               : (level == 2'd2) ? -16'sd12000
                               : 16'sd0;

    reg [4:0]  lrck_cnt;
    reg [15:0] shift;

    always @(negedge audio_sclk) begin
        audio_dac <= shift[15];
        shift <= {shift[14:0], 1'b0};
        lrck_cnt <= lrck_cnt + 1'b1;
        if (lrck_cnt == 5'd31) begin
            audio_lrck <= ~audio_lrck;
        end
        if (lrck_cnt == 5'd0)
            shift <= sample;
    end
endmodule
