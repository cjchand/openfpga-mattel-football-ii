// Verifies ce_gen's long-run average rate and single-cycle pulse width.
// Unlike a ratio that divides CLK_HZ exactly (e.g. FB1's 70000/12288000),
// 95000/12288000 does NOT divide the accumulator's overflow period exactly
// every CLK_HZ clocks -- so this test checks the measured rate is close to
// CE_HZ (within a small tolerance), not an exact integer pulse count.
#include "Vce_gen.h"
#include "verilated.h"
#include <cstdio>
#include <cmath>

static int g_failures = 0;
static const char* g_current = "";
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::printf("FAIL [%s]: %s (line %d)\n", g_current, msg, __LINE__); \
                   g_failures++; return; } } while (0)

struct Gen {
    Vce_gen d;
    void reset() {
        d.rst_n = 0; d.clk = 0; d.eval();
        d.clk = 1; d.eval(); d.clk = 0; d.eval();
        d.rst_n = 1; d.eval();
    }
    bool tick() {
        d.clk = 1; d.eval();
        bool ce = d.ce;
        d.clk = 0; d.eval();
        return ce;
    }
};

static void run_test(const char* name, void (*fn)(void)) { g_current = name; fn(); }

static void test_average_rate() {
    Gen g; g.reset();
    long pulses = 0;
    for (long i = 0; i < 12288000; i++) if (g.tick()) pulses++;
    long expected = 95000;
    long diff = pulses > expected ? pulses - expected : expected - pulses;
    CHECK(diff <= 2, "pulse count within 2 of 95000 over one second of clk time");
}

static void test_single_cycle_width() {
    Gen g; g.reset();
    bool prev = false;
    for (long i = 0; i < 200000; i++) {
        bool ce = g.tick();
        CHECK(!(ce && prev), "ce never high on two consecutive clocks");
        prev = ce;
    }
}

static void test_reset_clears_pulse() {
    Gen g; g.reset();
    CHECK(g.d.ce == 0, "ce low immediately after reset");
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    run_test("average_rate", test_average_rate);
    run_test("single_cycle_width", test_single_cycle_width);
    run_test("reset_clears_pulse", test_reset_clears_pulse);
    if (g_failures) { std::printf("FAILED: %d check(s)\n", g_failures); return 1; }
    std::printf("PASS: ce_gen_tb\n");
    return 0;
}
