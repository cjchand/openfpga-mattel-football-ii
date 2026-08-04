// sim/debug_probe_tb.cpp
//
// The debug probe only earns its place if it fires exactly when a real
// fault happens and never during normal play -- if it is wrong, a hardware
// test session produces no usable information. Small parameters are used
// here so the tests stay fast; the real thresholds are set in core_top.
#include "Vdebug_probe.h"
#include "verilated.h"
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

static void tick(Vdebug_probe* d) {
    d->clk = 0; d->eval();
    d->clk = 1; d->eval();
}

static Vdebug_probe* make() {
    Vdebug_probe* d = new Vdebug_probe;
    d->rst_n = 0; d->ce = 0; d->pc = 0;
    d->tone_on = 0; d->unimpl_hit = 0; d->int1l_hit = 0;
    tick(d);
    d->rst_n = 1;
    return d;
}

// Runs `n` enabled cycles, advancing pc each one unless `freeze_pc`.
static void run(Vdebug_probe* d, int n, bool tone, bool freeze_pc) {
    for (int i = 0; i < n; i++) {
        d->ce = 1;
        d->tone_on = tone;
        if (!freeze_pc) d->pc = (d->pc + 1) & 0x7FF;
        tick(d);
    }
    d->ce = 0;
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);

    // A tone of normal length, with the CPU running, must not trigger.
    {
        Vdebug_probe* d = make();
        run(d, 500, true, false);
        run(d, 200, false, false);
        run(d, 500, true, false);
        CHECK(d->trig == 0);
        delete d;
    }

    // A tone held past TONE_STUCK_CE triggers and reports the tone cause.
    {
        Vdebug_probe* d = make();
        // PC frozen would trip the pc cause first (48000 < 190000), so keep
        // the PC moving and only hold the tone.
        run(d, 189000, true, false);
        CHECK(d->trig == 0); // not yet -- must not fire on ordinary sounds
        run(d, 2000, true, false);
        CHECK(d->trig == 1);
        CHECK(d->cause_tone == 1);
        CHECK(d->cause_pc == 0);
        delete d;
    }

    // A halted CPU triggers on the pc cause even with no tone at all.
    {
        Vdebug_probe* d = make();
        d->pc = 0x123;
        run(d, 47000, false, true);
        CHECK(d->trig == 0); // not yet
        run(d, 2000, false, true);
        CHECK(d->trig == 1);
        CHECK(d->cause_pc == 1);
        CHECK(d->cause_tone == 0);
        CHECK(d->pc_latched == 0x123);
        delete d;
    }

    // The first capture is held: a later fault must not overwrite the PC,
    // so what is on screen is the original fault, not the newest one.
    {
        Vdebug_probe* d = make();
        d->pc = 0x0AB;
        run(d, 48010, false, true);
        CHECK(d->trig == 1);
        unsigned first = d->pc_latched;
        d->pc = 0x555;
        run(d, 200000, true, true);
        CHECK(d->pc_latched == first);
        delete d;
    }

    // unimpl/int1l are sticky and independent of trig -- a single dispatch
    // anywhere in the run must still be visible afterwards.
    {
        Vdebug_probe* d = make();
        run(d, 10, false, false);
        d->unimpl_hit = 1; d->ce = 1; tick(d); d->unimpl_hit = 0;
        run(d, 100, false, false);
        CHECK(d->unimpl_seen == 1);
        CHECK(d->int1l_seen == 0);
        CHECK(d->trig == 0); // sticky flags alone must not arm the capture
        delete d;
    }

    if (failures == 0) { std::printf("PASS: debug_probe_tb\n"); return 0; }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
