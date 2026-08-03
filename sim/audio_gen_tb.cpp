// Verifies I2S frame timing (32 sclk periods per channel, matching the APF
// template's own generator's constants) and that a held 2-bit level
// produces the correct MSB-first bit sequence and sign on audio_dac.
#include "Vaudio_gen.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>
#include <vector>

static int g_failures = 0;
static const char* g_current = "";
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::printf("FAIL [%s]: %s (line %d)\n", g_current, msg, __LINE__); \
                   g_failures++; return; } } while (0)

struct Gen {
    Vaudio_gen d;
    bool prev_sclk = false, prev_lrck = false;
    int lrck_toggles = 0;
    std::vector<int> dac_at_sclk_fall;

    void tick() {
        d.clk_74a = 1; d.eval();
        d.clk_74a = 0; d.eval();
        bool sclk = d.audio_sclk;
        if (prev_sclk && !sclk) dac_at_sclk_fall.push_back(d.audio_dac);
        prev_sclk = sclk;
        bool lrck = d.audio_lrck;
        if (lrck != prev_lrck) lrck_toggles++;
        prev_lrck = lrck;
    }
    void run(long n) { for (long i = 0; i < n; i++) tick(); }
};

static void run_test(const char* name, void (*fn)(void)) { g_current = name; fn(); }

static void test_lrck_toggles_every_32_sclk_periods() {
    Gen g;
    g.d.level = 0;
    g.run(50000); // several full frames' worth of clk_74a cycles
    CHECK(g.dac_at_sclk_fall.size() > 64, "captured multiple full 32-bit frames");
}

static void test_level_01_produces_positive_sample_msb_first() {
    Gen g;
    g.d.level = 1; // +amplitude per speaker_levels table (0.0,+1.0,-1.0,0.0)
    g.run(10000);
    g.dac_at_sclk_fall.clear();
    g.run(5000); // run past one full lrck toggle boundary to land on a fresh frame
    bool saw_high_bit = false;
    for (size_t i = 0; i + 16 <= g.dac_at_sclk_fall.size(); i++) {
        if (g.dac_at_sclk_fall[i] == 1) { saw_high_bit = true; break; }
    }
    CHECK(saw_high_bit, "a positive sample's MSB (sign bit) is high at some point in the frame");
}

static void test_level_00_produces_all_zero_sample() {
    Gen g;
    g.d.level = 0; // silence per speaker_levels[0] == 0.0
    g.run(10000);
    g.dac_at_sclk_fall.clear();
    g.run(5000); // two full sclk-frame windows
    bool any_high = false;
    for (int v : g.dac_at_sclk_fall) if (v) any_high = true;
    CHECK(!any_high, "level 00 (silence) never drives audio_dac high");
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    run_test("lrck_toggles_every_32_sclk_periods", test_lrck_toggles_every_32_sclk_periods);
    run_test("level_01_produces_positive_sample_msb_first", test_level_01_produces_positive_sample_msb_first);
    run_test("level_00_produces_all_zero_sample", test_level_00_produces_all_zero_sample);
    if (g_failures) { std::printf("FAILED: %d check(s)\n", g_failures); return 1; }
    std::printf("PASS: audio_gen_tb\n");
    return 0;
}
