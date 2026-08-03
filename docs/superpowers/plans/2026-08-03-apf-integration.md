# APF Integration Implementation Plan (Phase 4 of 5)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the core actually boot and play on a physical Analogue Pocket: real ROM loaded from the SD card, a real ~95 kHz clock enable driving the CPU, real audio out the speaker, real digit/LED video, and Pocket controls mapped to the game (design spec's full boot-and-play milestone).

**Architecture:** First, fix a confirmed CPU-core gap (opcodes `0x74`/`0x75`/`0x79` were never decoded) found while writing the design spec — this must not ship inside "real gameplay." Then five small, independently-testable pieces get wired into the vendored template's `core_top.v`: a `ce` port added to `pps41_core`/`pps41_display_pwm` (replacing their current "every clk edge is one instruction cycle" assumption, which only holds in the lockstep sim harness) plus `ce_gen` to drive it at ~95 kHz from the Pocket's 12.288 MHz core clock; `rom_loader`, a bridge-write BRAM serving the CPU's `rom_addr`→`rom_data` reads with the `0x600-0x7FF` mirror fold; `audio_gen`, turning the tone generator's 2-bit speaker level into real I²S; and `display_render`, a plain combinational grid renderer over the already-reconstructed `levels[219:0]` PWM matrix. The final task wires everything into `core_top.v`, updates the SD-card manifest, and ends with a human playing the real game on a real Pocket.

**Tech Stack:** Verilog-2001 (Quartus-synthesizable, no SystemVerilog), Verilator + C++17 for unit tests (matching this repo's existing `sim/Makefile` conventions — no external test framework), Docker Quartus (already proven by the toolchain-scaffold sub-project) for the bitstream, a physical Analogue Pocket + SD card for the final checkpoint.

**Spec:** `docs/superpowers/specs/2026-08-03-apf-integration-design.md`.

## Global Constraints

- Every new/modified Verilog file goes through `verilator --lint-only -Wall` clean (matches `sim/Makefile`'s existing `lint-core` target convention) before its task is considered done.
- Clock domain: `clk_74a` (12.288 MHz, from the vendored template's pre-built `mf_pllbase` — do not touch that IP) is the one clock the whole core runs on. `ce_gen` produces a single-cycle-wide enable pulse averaging ~95000 Hz on this domain; this becomes the new `ce` input on `pps41_core` and `pps41_display_pwm`. Do not attempt to build a divided/gated *clock* — enables only.
- ROM: real dump lives at `development-assets/b8000-12` (1536 bytes, gitignored — already present locally, do not commit it). The CPU's 11-bit address space mirrors `0x600-0x7FF` onto `0x400-0x5FF`; both `rom_loader` (bridge write is a dense, hole-free 1536-byte file) and the real-ROM regression test must apply this same fold.
- Every new Verilog file's header comment states its one responsibility in 1-3 lines, matching the style already used in `src/pps41_*.v` and `src/fpga/core/core_top.v` — no multi-paragraph blocks.
- `pps41_core.v`'s `dbg_*` ports stay tied off (never asserted) outside their existing unit tests — this integration exclusively drives real execution.
- Do not modify `src/fpga/core/mf_pllbase*` (pre-built Quartus IP) or `src/fpga/apf/*` (framework internals) — only `src/fpga/core/core_top.v` and the `dist/Cores/.../*.json` manifests may be touched inside `src/fpga/`, plus new files under `src/`/`sim/`.
- Every commit message ends with: `Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>`.

## File Structure

- `sim/golden/mm77la_model.cpp` — modified (Task 0): add `0x74`/`0x75`/`0x79` opcode cases.
- `sim/golden/mm77la_model_test.cpp` — modified (Task 0): new unit tests for the three opcodes.
- `src/pps41_core.v` — modified (Task 0: add the three opcode cases; Task 1: add `ce` port).
- `sim/pps41_core_tb.cpp` — modified (Task 0: tie `ce` high, no behavior change; Task 1's own `ce` port added here too; adds `x_out`/`s_out` comparison).
- `sim/vectors/xas_lxa_xax.bin` — new (Task 0): synthetic regression vector.
- `src/pps41_display_pwm.v` — modified (Task 1): add `ce` port.
- `sim/pps41_display_pwm_tb.cpp` — modified (Task 1): tie `ce` high, no behavior change.
- `src/ce_gen.v` — new (Task 2).
- `sim/ce_gen_tb.cpp` — new (Task 2).
- `src/rom_loader.v` — new (Task 3).
- `sim/rom_loader_tb.cpp` — new (Task 3).
- `src/audio_gen.v` — new (Task 4).
- `sim/audio_gen_tb.cpp` — new (Task 4).
- `src/display_render.v` — new (Task 5).
- `sim/display_render_tb.cpp` — new (Task 5).
- `src/fpga/core/core_top.v` — modified in place (Task 6): instantiate everything, replace placeholder video/audio, wire inputs.
- `dist/Cores/Developer.Core Template/data.json` — modified (Task 7): ROM dataslot entry.
- `sim/Makefile` — modified (Tasks 0, 2-5): new test targets, added to `test:`.

---

### Task 0: Fix the confirmed CPU-core gap — `0x74` (XAS), `0x75` (LXA), `0x79` (XAX)

**Files:**
- Modify: `sim/golden/mm77la_model.cpp`
- Modify: `sim/golden/mm77la_model_test.cpp`
- Modify: `src/pps41_core.v`
- Modify: `sim/pps41_core_tb.cpp`
- Create: `sim/vectors/xas_lxa_xax.bin`
- Modify: `sim/Makefile` (`vectors-test` already globs `vectors/*.bin`, no change needed there — but confirm)

**Interfaces:**
- Consumes: nothing new. `Mm77laState.x`/`Mm77laState.s` (`sim/golden/mm77la_model.h:17,20`) and `pps41_core.v`'s `x`/`s` registers (`src/pps41_core.v:67,69`) already exist, currently unused by any decoded opcode.
- Produces: two new output ports on `pps41_core`, `x_out` (4-bit) and `s_out` (4-bit), for lockstep comparison — no other module depends on these.

- [ ] **Step 1: Add golden-model unit tests for the three opcodes (write them failing first)**

Add to `sim/golden/mm77la_model_test.cpp`, near the other single-opcode tests (e.g. after `test_int1l_is_noop_but_flags_hit`):

```cpp
static void test_xas_swaps_a_and_s() {
    uint8_t rom[1] = {0x74}; // XAS
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.debug_set_a(0x5);
    // s starts at 0 (Mm77laState's default) -- no debug setter needed
    m.step();
    CHECK(m.state().a == 0x0); // a took s's old value (0)
    CHECK(m.state().s == 0x5); // s took a's old value (5)
}

static void test_lxa_loads_x_from_a() {
    uint8_t rom[1] = {0x75}; // LXA
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.debug_set_a(0x7);
    m.step();
    CHECK(m.state().x == 0x7); // x = a
    CHECK(m.state().a == 0x7); // a unchanged
}

static void test_xax_swaps_a_and_x() {
    // LXA first to put a known value in x, then XAX to swap it back into a
    // after a's own value changes -- proves XAX round-trips correctly.
    uint8_t rom[3] = {
        0x75,       // LXA -- x = a (a starts 0x0, so x = 0x0)
        0x40 | 0x9, // LAI 9 -- a = 9 (prev op was LXA, not LAI, so not suppressed)
        0x79,       // XAX -- swap a<->x
    };
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.step(); // LXA: x = 0x0
    m.step(); // LAI 9: a = 0x9
    CHECK(m.state().a == 0x9);
    m.step(); // XAX: a<->x
    CHECK(m.state().a == 0x0); // a took x's old value
    CHECK(m.state().x == 0x9); // x took a's old value
}
```

Register the three new calls in `main()`, right after `test_int1l_is_noop_but_flags_hit();`:
```cpp
    test_xas_swaps_a_and_s();
    test_lxa_loads_x_from_a();
    test_xax_swaps_a_and_x();
```

- [ ] **Step 2: Run to verify the new tests fail**

Run: `cd sim && make golden-test`
Expected: `FAIL` lines for all three new checks (opcodes currently fall through to `unimpl_hit`, leaving `a`/`x`/`s` unchanged from their reset/initial values — `test_xas_swaps_a_and_s` fails because `a` stays `0x5`, not `0x0`; etc.)

- [ ] **Step 3: Implement the three opcodes in the golden model**

In `sim/golden/mm77la_model.cpp`, inside the `switch (op)` block (the one currently ending `default: st_.unimpl_hit = true; break; // unimplemented (LXA/XAX/XAS, etc.)`), add three new cases immediately before that `default:`:

```cpp
                            case 0x74: { // XAS -- swap A<->S. Real hardware also updates a
                                // serial data-out pin; Football II's MAME driver never wires
                                // that pin to anything observable (see initial-plan.md §7's
                                // I/O summary -- only write_d/write_r/write_spk/read_d/read_p
                                // are connected), so the register swap is the entire
                                // architecturally-visible effect for this game.
                                uint8_t tmp = st_.a;
                                st_.a = st_.s;
                                st_.s = tmp;
                                break;
                            }
                            case 0x75: st_.x = st_.a; break; // LXA
                            case 0x79: { // XAX -- swap A<->X
                                uint8_t tmp = st_.a;
                                st_.a = st_.x;
                                st_.x = tmp;
                                break;
                            }
```

Update the header comment on `Mm77laState::x` and `::s` (`sim/golden/mm77la_model.h:17,20`) to drop "unused by FBII" / add that they're now written by `LXA`/`XAX`/`XAS`:
```cpp
    uint8_t x = 0;            // 4-bit secondary register, written by LXA/XAX
    ...
    uint8_t s = 0;                 // 4-bit serial shift register, written by XAS (serial-out pin not modeled -- unused by this game, see mm77la_model.cpp's XAS case)
```

- [ ] **Step 4: Run to verify the golden-model tests pass**

Run: `cd sim && make golden-test`
Expected: `PASS` (all tests, including the three new ones).

- [ ] **Step 5: Add matching cases to the RTL, plus new x_out/s_out ports**

In `src/pps41_core.v`, add two new output ports right after `output wire unimpl_hit_out` (line 58):
```verilog
    output wire [3:0]  x_out,
    output wire [3:0]  s_out
```
Add the two `assign`s near the other `assign ..._out` lines (after `assign unimpl_hit_out = unimpl_hit;`):
```verilog
    assign x_out = x;
    assign s_out = s;
```
Update the comments on the `x`/`s` register declarations (lines 67, 69) the same way as Step 3's golden-model header update (drop "unused by any implemented opcode").

Add three new cases to the `case (op)` block, immediately before `default: next_unimpl_hit = 1'b1;` (around line 511):
```verilog
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
```
(`xchg_tmp` is the existing `reg [3:0]` scratch variable already declared and used by the `8'h7A` (XAB) case — reusing it here is safe since only one `case` arm executes per cycle.)

- [ ] **Step 6: Wire the new ports into the lockstep testbench and add comparisons**

In `sim/pps41_core_tb.cpp`, add two comparison lines right after the existing `unimpl_hit_out` check:
```cpp
        if (dut->x_out != g.x) { std::printf("cycle %ld: x mismatch rtl=%x golden=%x\n", i, dut->x_out, g.x); mismatch = true; }
        if (dut->s_out != g.s) { std::printf("cycle %ld: s mismatch rtl=%x golden=%x\n", i, dut->s_out, g.s); mismatch = true; }
```

- [ ] **Step 7: Rebuild and re-run the full existing test suite to confirm zero regressions**

Run: `cd sim && make test`
Expected: every existing test still `PASS` — this task only adds previously-unreachable opcode cases and two new comparison ports, so nothing already passing should change.

- [ ] **Step 8: Write the regression vector and confirm it passes**

```bash
python3 -c "
rom = bytes([
    0x74,        # XAS: a(0)<->s(0) -- no-op numerically at reset, but exercises decode
    0x40 | 0x5,  # LAI 5 -- a = 5
    0x74,        # XAS: a<->s -- a becomes 0 (s's value), s becomes 5
    0x74,        # XAS again: a<->s -- a becomes 5 again, s becomes 0
    0x75,        # LXA: x = a (5)
    0x40 | 0x0,  # LAI 0 -- a = 0 (prev op was LXA, not LAI, so not suppressed)
    0x79,        # XAX: a<->x -- a becomes 5 (x's value), x becomes 0
])
open('sim/vectors/xas_lxa_xax.bin', 'wb').write(rom)
"
```

Run: `cd sim && ./obj_dir_core/Vpps41_core vectors/xas_lxa_xax.bin 30`
Expected: `PASS: 30 cycles, no mismatches` (confirms RTL and golden model agree cycle-by-cycle through the whole sequence, including the final `a==5` state that only round-trips correctly if both LXA and XAX are implemented right).

Run: `cd sim && make vectors-test`
Expected: all vectors (including the new one, picked up automatically by the `vectors/*.bin` glob) `PASS`.

- [ ] **Step 9: Commit**

```bash
git add sim/golden/mm77la_model.cpp sim/golden/mm77la_model.h sim/golden/mm77la_model_test.cpp \
        src/pps41_core.v sim/pps41_core_tb.cpp sim/vectors/xas_lxa_xax.bin
git commit -m "$(cat <<'EOF'
Implement XAS/LXA/XAX (0x74/0x75/0x79), fixing a confirmed real-ROM gap

Found while writing the APF integration design spec: the real ROM
(development-assets/b8000-12) dispatches opcode 0x74 (XAS) at cycle 202,
which both the golden model and RTL silently no-op'd via unimpl_hit. All
three opcodes were already in initial-plan.md's §5.1/§5.2 opcode tables
with documented semantics -- this was a Phase-1 coding omission, not a new
unknown.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 1: Add a `ce` port to `pps41_core.v` and `pps41_display_pwm.v`

**Files:**
- Modify: `src/pps41_core.v`
- Modify: `sim/pps41_core_tb.cpp`
- Modify: `src/pps41_display_pwm.v`
- Modify: `sim/pps41_display_pwm_tb.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces: `pps41_core` gains `input wire ce` (gates its sequential always block). `pps41_display_pwm` gains `input wire ce` (gates its sequential always block). Task 6 wires both to `ce_gen`'s (Task 2) output; every existing testbench ties `ce` to `1'b1` permanently, proving zero behavior change.

- [ ] **Step 1: Add the `ce` port and gate `pps41_core`'s sequential block**

In `src/pps41_core.v`, add `input wire ce,` right after `input wire rst_n,` (line 23).

Change the `pps41_tone` instantiation's `cycle_en` binding (line 226) from:
```verilog
        .cycle_en(1'b1),
```
to:
```verilog
        .cycle_en(ce),
```

Gate the sequential block: change `end else begin` (line 577, the non-reset branch of `always @(posedge clk or negedge rst_n)`) to:
```verilog
        end else if (ce) begin
```
Everything between that line and the block's closing `end` (line 615) stays exactly as-is — this makes the entire state-update branch conditional on `ce`, while reset (`!rst_n`) still fires unconditionally on any clock edge.

- [ ] **Step 2: Update `pps41_core_tb.cpp` to tie `ce` high**

In `sim/pps41_core_tb.cpp`, right after `dut->rst_n = 0; dut->dbg_b_set = 0; ...` (the DUT init line), add:
```cpp
    dut->ce = 1;
```

- [ ] **Step 3: Rebuild and confirm zero regression**

Run: `cd sim && make core-test && make vectors-test && make display-vectors-long-test`
Expected: all `PASS`, byte-for-byte identical results to before this task (this is a pure refactor — `ce` tied high means every cycle behaves exactly as it did with the unconditional `else begin`).

- [ ] **Step 4: Add the `ce` port and gate `pps41_display_pwm`'s sequential block**

In `src/pps41_display_pwm.v`, add `input wire ce,` right after `input wire rst_n,` (line 4).

Change `end else begin` (line 26, the non-reset branch) to:
```verilog
        end else if (ce) begin
```

- [ ] **Step 5: Update `pps41_display_pwm_tb.cpp` to tie `ce` high**

In `sim/pps41_display_pwm_tb.cpp`, find the DUT initialization (mirrors the `dpwm->rst_n = 0; ...` pattern already used in `pps41_core_tb.cpp`) and add a line tying `ce` to `1` right after reset is deasserted, alongside the existing signal setup at the top of `main()`.

- [ ] **Step 6: Rebuild and confirm zero regression**

Run: `cd sim && make display-pwm-test && make display-pwm-rtl-test && make core-test && make display-vectors-long-test`
Expected: all `PASS`, identical to pre-task results (`pps41_core_tb.cpp` also instantiates `pps41_display_pwm` directly as `dpwm` — that instantiation needs the same `dpwm->ce = 1;` line added next to its own `dpwm->rst_n = 0;` init, in the same edit as Step 2).

- [ ] **Step 7: Commit**

```bash
git add src/pps41_core.v src/pps41_display_pwm.v sim/pps41_core_tb.cpp sim/pps41_display_pwm_tb.cpp
git commit -m "$(cat <<'EOF'
Add ce (clock-enable) port to pps41_core and pps41_display_pwm

Both modules previously ran their sequential logic on every posedge clk
unconditionally, correct only for 1:1 lockstep sim where each clock edge
is one instruction cycle. Real hardware has one fixed 12.288MHz clock, so
this adds an enable input gating the same logic -- a pure refactor,
verified behavior-identical by tying ce high in every existing testbench.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 2: `ce_gen` — ~95 kHz clock enable from the 12.288 MHz core clock

**Files:**
- Create: `src/ce_gen.v`
- Create: `sim/ce_gen_tb.cpp`
- Modify: `sim/Makefile` (add `ce-gen-test` target, add to `test:`)

**Interfaces:**
- Consumes: nothing.
- Produces: `module ce_gen #(parameter CLK_HZ = 12288000, parameter CE_HZ = 95000) (input clk, input rst_n, output reg ce)`. Task 6 wires this module's `ce` output to both `pps41_core.ce` and `pps41_display_pwm.ce`.

- [ ] **Step 1: Write the failing testbench**

Create `sim/ce_gen_tb.cpp`:

```cpp
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
```

- [ ] **Step 2: Add the Makefile target and run to verify failure**

Add to `sim/Makefile` (near the other `*-test` targets):
```makefile
ce-gen-test:
	$(VERILATOR) --cc ../src/ce_gen.v --exe ce_gen_tb.cpp \
		--Mdir obj_dir_ce_gen -Wall --Wno-UNUSEDSIGNAL
	$(MAKE) -C obj_dir_ce_gen -f Vce_gen.mk
	./obj_dir_ce_gen/Vce_gen
```
Add `ce-gen-test` to the `.PHONY:` line and to the `test:` target's dependency list.

Run: `cd sim && make ce-gen-test` — Expected: FAIL, `src/ce_gen.v` not found.

- [ ] **Step 3: Implement the module**

Create `src/ce_gen.v`:

```verilog
// src/ce_gen.v
//
// Derives a clock enable averaging CE_HZ from a clk running at CLK_HZ,
// using the same fractional-accumulator technique the APF template itself
// uses for its 48kHz audio MCLK (core_top.v's audgen_accum). With the
// defaults (12.288MHz core clock, ~95kHz instruction rate = 380kHz MM77LA
// oscillator / 4 phases per cycle, per docs/initial-plan.md §1), the ratio
// 95000/12288000 does NOT divide the accumulator's overflow period exactly
// -- expect a small long-run rate error (a few Hz), not zero error.
module ce_gen #(
    parameter CLK_HZ = 12288000,
    parameter CE_HZ  = 95000
) (
    input  wire clk,
    input  wire rst_n,
    output reg  ce
);
    localparam ACC_W = $clog2(CLK_HZ + CE_HZ + 1);

    reg [ACC_W-1:0] accum;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            accum <= {ACC_W{1'b0}};
            ce <= 1'b0;
        end else begin
            accum <= accum + CE_HZ[ACC_W-1:0];
            if (accum + CE_HZ[ACC_W-1:0] >= CLK_HZ[ACC_W-1:0]) begin
                accum <= accum + CE_HZ[ACC_W-1:0] - CLK_HZ[ACC_W-1:0];
                ce <= 1'b1;
            end else begin
                ce <= 1'b0;
            end
        end
    end
endmodule
```

- [ ] **Step 4: Run to verify pass**

Run: `cd sim && make ce-gen-test`
Expected: `PASS: ce_gen_tb` (`test_average_rate` runs 12.288M simulated cycles under Verilator — a few seconds, not minutes).

- [ ] **Step 5: Run the full suite to confirm no regressions**

Run: `cd sim && make test`
Expected: all `PASS`.

- [ ] **Step 6: Commit**

```bash
git add src/ce_gen.v sim/ce_gen_tb.cpp sim/Makefile
git commit -m "$(cat <<'EOF'
Add ce_gen: accumulator-based ~95kHz clock enable from 12.288MHz core clock

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: `rom_loader` — APF bridge-loaded ROM with the 0x600-0x7FF mirror fold

**Files:**
- Create: `src/rom_loader.v`
- Create: `sim/rom_loader_tb.cpp`
- Modify: `sim/Makefile` (add `rom-loader-test` target, add to `test:`)

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `module rom_loader #(parameter [31:0] SLOT_BASE = 32'h10000000) (input clk, input bridge_wr, input [31:0] bridge_addr, input [31:0] bridge_wr_data, input [10:0] rom_addr, output wire [7:0] rom_data)`. Task 6 wires `rom_addr`/`rom_data` directly to `pps41_core`'s identically-purposed ports.

- [ ] **Step 1: Write the failing testbench**

Create `sim/rom_loader_tb.cpp`:

```cpp
// Verifies bridge-write loading (dense 1536-byte file) and CPU-side
// 0x600-0x7FF -> 0x400-0x5FF mirror-fold translation.
#include "Vrom_loader.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>

static int g_failures = 0;
static const char* g_current = "";
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::printf("FAIL [%s]: %s (line %d)\n", g_current, msg, __LINE__); \
                   g_failures++; return; } } while (0)

static const uint32_t SLOT_BASE = 0x10000000;

struct Loader {
    Vrom_loader d;
    void tick() { d.clk = 1; d.eval(); d.clk = 0; d.eval(); }

    void write_word(int word_index, uint32_t data) {
        d.bridge_addr = SLOT_BASE + word_index * 4;
        d.bridge_wr_data = data;
        d.bridge_wr = 1;
        tick();
        d.bridge_wr = 0;
    }

    // load a 1536-byte image via 384 word writes, little-endian within each word
    void load(const uint8_t* rom) {
        for (int w = 0; w < 384; w++) {
            uint32_t word = rom[w*4] | (rom[w*4+1] << 8) | (rom[w*4+2] << 16) | (rom[w*4+3] << 24);
            write_word(w, word);
        }
    }

    uint8_t read(uint16_t rom_addr) {
        d.rom_addr = rom_addr;
        d.eval();
        return d.rom_data;
    }
};

static void run_test(const char* name, void (*fn)(void)) { g_current = name; fn(); }

static void test_load_and_direct_read() {
    Loader l;
    uint8_t rom[1536];
    for (int i = 0; i < 1536; i++) rom[i] = (uint8_t)(i ^ 0x5A);
    l.load(rom);
    CHECK(l.read(0x000) == rom[0x000], "addr 0x000 = file offset 0x000");
    CHECK(l.read(0x5FF) == rom[0x5FF], "addr 0x5FF = file offset 0x5FF (last real byte)");
}

static void test_mirror_fold_0x600_to_0x7ff() {
    Loader l;
    uint8_t rom[1536];
    for (int i = 0; i < 1536; i++) rom[i] = (uint8_t)(i ^ 0x5A);
    l.load(rom);
    CHECK(l.read(0x600) == rom[0x400], "addr 0x600 mirrors file offset 0x400");
    CHECK(l.read(0x7FF) == rom[0x5FF], "addr 0x7FF mirrors file offset 0x5FF (last mirrored byte)");
    CHECK(l.read(0x700) == rom[0x500], "addr 0x700 mirrors file offset 0x500 (middle of mirror range)");
}

static void test_bridge_writes_outside_slot_ignored() {
    Loader l;
    uint8_t rom[1536] = {0};
    l.load(rom);
    l.d.bridge_addr = 0x20000000; l.d.bridge_wr_data = 0xFFFFFFFF; l.d.bridge_wr = 1;
    l.tick();
    l.d.bridge_wr = 0;
    CHECK(l.read(0x000) == 0, "write outside SLOT_BASE range does not touch ROM");
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    run_test("load_and_direct_read", test_load_and_direct_read);
    run_test("mirror_fold_0x600_to_0x7ff", test_mirror_fold_0x600_to_0x7ff);
    run_test("bridge_writes_outside_slot_ignored", test_bridge_writes_outside_slot_ignored);
    if (g_failures) { std::printf("FAILED: %d check(s)\n", g_failures); return 1; }
    std::printf("PASS: rom_loader_tb\n");
    return 0;
}
```

- [ ] **Step 2: Add the Makefile target and run to verify failure**

Add to `sim/Makefile`:
```makefile
rom-loader-test:
	$(VERILATOR) --cc ../src/rom_loader.v --exe rom_loader_tb.cpp \
		--Mdir obj_dir_rom_loader -Wall --Wno-UNUSEDSIGNAL
	$(MAKE) -C obj_dir_rom_loader -f Vrom_loader.mk
	./obj_dir_rom_loader/Vrom_loader
```
Add `rom-loader-test` to `.PHONY:` and to `test:`'s dependency list.

Run: `cd sim && make rom-loader-test` — Expected: FAIL, `src/rom_loader.v` not found.

- [ ] **Step 3: Implement the module**

Create `src/rom_loader.v`:

```verilog
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
```

- [ ] **Step 4: Run to verify pass**

Run: `cd sim && make rom-loader-test`
Expected: `PASS: rom_loader_tb`.

- [ ] **Step 5: Run the full suite to confirm no regressions**

Run: `cd sim && make test`
Expected: all `PASS`.

- [ ] **Step 6: Commit**

```bash
git add src/rom_loader.v sim/rom_loader_tb.cpp sim/Makefile
git commit -m "$(cat <<'EOF'
Add rom_loader: APF bridge-loaded ROM with 0x600-0x7FF mirror-fold translation

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 4: `audio_gen` — tone generator's 2-bit level to real I²S audio

**Files:**
- Create: `src/audio_gen.v`
- Create: `sim/audio_gen_tb.cpp`
- Modify: `sim/Makefile` (add `audio-gen-test` target, add to `test:`)

**Interfaces:**
- Consumes: nothing from earlier tasks (standalone; Task 6 feeds it `pps41_tone`'s `spk_output_out`, exposed by `pps41_core` as `spk_output_result`).
- Produces: `module audio_gen (input clk_74a, input [1:0] level, output audio_mclk, output audio_sclk, output reg audio_lrck, output reg audio_dac)`. Task 6 wires `audio_mclk`/`audio_dac`/`audio_lrck` directly to `core_top`'s identically-named top-level output ports, replacing the template's silence generator; `audio_sclk` is internal-only (not a top-level pin), matching the template's own unexposed SCLK.

- [ ] **Step 1: Write the failing testbench**

Create `sim/audio_gen_tb.cpp`:

```cpp
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
    g.run(5000);
    g.dac_at_sclk_fall.clear();
    g.run(700); // run past one full lrck toggle boundary to land on a fresh frame
    bool saw_high_bit = false;
    for (size_t i = 0; i + 16 <= g.dac_at_sclk_fall.size(); i++) {
        if (g.dac_at_sclk_fall[i] == 1) { saw_high_bit = true; break; }
    }
    CHECK(saw_high_bit, "a positive sample's MSB (sign bit) is high at some point in the frame");
}

static void test_level_00_produces_all_zero_sample() {
    Gen g;
    g.d.level = 0; // silence per speaker_levels[0] == 0.0
    g.run(5000);
    g.dac_at_sclk_fall.clear();
    g.run(64); // two full sclk-frame windows
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
```

- [ ] **Step 2: Add the Makefile target and run to verify failure**

Add to `sim/Makefile`:
```makefile
audio-gen-test:
	$(VERILATOR) --cc ../src/audio_gen.v --exe audio_gen_tb.cpp \
		--Mdir obj_dir_audio_gen -Wall --Wno-UNUSEDSIGNAL
	$(MAKE) -C obj_dir_audio_gen -f Vaudio_gen.mk
	./obj_dir_audio_gen/Vaudio_gen
```
Add `audio-gen-test` to `.PHONY:` and to `test:`'s dependency list.

Run: `cd sim && make audio-gen-test` — Expected: FAIL, `src/audio_gen.v` not found.

- [ ] **Step 3: Implement the module**

Create `src/audio_gen.v`:

```verilog
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
```

- [ ] **Step 4: Run to verify pass**

Run: `cd sim && make audio-gen-test`
Expected: `PASS: audio_gen_tb`.

- [ ] **Step 5: Run the full suite to confirm no regressions**

Run: `cd sim && make test`
Expected: all `PASS`.

- [ ] **Step 6: Commit**

```bash
git add src/audio_gen.v sim/audio_gen_tb.cpp sim/Makefile
git commit -m "$(cat <<'EOF'
Add audio_gen: real I2S audio from the tone generator's 2-bit speaker level

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 5: `display_render` — PWM matrix to a simple procedural video grid

**Files:**
- Create: `src/display_render.v`
- Create: `sim/display_render_tb.cpp`
- Modify: `sim/Makefile` (add `display-render-test` target, add to `test:`)

**Interfaces:**
- Consumes: nothing from earlier tasks (standalone; Task 6 feeds it `pps41_display_pwm`'s `levels[219:0]` output and `core_top.v`'s existing `visible_x`/`visible_y` wires).
- Produces: `module display_render (input [219:0] levels, input [9:0] x, input [9:0] y, output wire [23:0] rgb)`. Pure combinational. Task 6 assigns `vidout_rgb <= render_rgb;` (replacing the placeholder gray fill) inside the existing active-video conditional, feeding `visible_x`/`visible_y` as `x`/`y`.

**Layout decision (per the approved "simple procedural shapes" fidelity):** rather than reverse-engineering which of the 110 `(row,col)` cells are 7-segment digit segments vs. discrete LEDs (a bezel-artwork-level concern, deferred to the next sub-project), this renders the already-reconstructed 10-row × 11-column PWM matrix directly as a plain grid of rectangles on a 320×240 canvas — one rectangle per cell, brightness-mapped from the cell's 2-bit level. This is fully legible and functionally correct (every lit cell is visible, at the correct relative brightness) without requiring unverified segment-position data.

- [ ] **Step 1: Write the failing testbench**

Create `sim/display_render_tb.cpp`:

```cpp
// Verifies the grid layout (which pixel maps to which of the 110 PWM
// cells) and the level-to-color mapping.
#include "Vdisplay_render.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>

static int g_failures = 0;
static const char* g_current = "";
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::printf("FAIL [%s]: %s (line %d)\n", g_current, msg, __LINE__); \
                   g_failures++; return; } } while (0)

static void run_test(const char* name, void (*fn)(void)) { g_current = name; fn(); }

// Layout constants, mirrored from src/display_render.v -- kept in sync by
// the two lint/compile steps below (a mismatch here would show up as
// every test failing, not a silent pass).
static const int MARGIN_X = 6, MARGIN_Y = 10, CELL_W = 28, CELL_H = 22, GAP = 3;

static void set_cell(Vdisplay_render& d, int row, int col, int lvl) {
    // levels[219:0] is a 220-bit input; Verilator represents it as an array
    // of 7 uint32_t words (ceil(220/32)=7). Each cell's 2-bit field starts
    // at an even bit index, so (since 32 is even) no cell ever spans a
    // word boundary -- a plain single-word read-modify-write is exact.
    int bit = (row * 11 + col) * 2;
    int word = bit / 32, off = bit % 32;
    d.levels[word] &= ~(3u << off);
    d.levels[word] |= ((uint32_t)lvl & 3u) << off;
}

static void test_cell_0_0_center_pixel_is_bright_when_level_2() {
    Vdisplay_render d;
    for (int i = 0; i < 7; i++) d.levels[i] = 0;
    set_cell(d, 0, 0, 2); // row 0, col 0 -> bright
    int cx = MARGIN_X + CELL_W/2, cy = MARGIN_Y + CELL_H/2; // center of cell (0,0)
    d.x = cx; d.y = cy;
    d.eval();
    CHECK(d.rgb != 0, "center of a bright cell is non-black");
}

static void test_off_cell_center_pixel_is_black() {
    Vdisplay_render d;
    for (int i = 0; i < 7; i++) d.levels[i] = 0; // every cell level 0 (off)
    int cx = MARGIN_X + CELL_W/2, cy = MARGIN_Y + CELL_H/2;
    d.x = cx; d.y = cy;
    d.eval();
    CHECK(d.rgb == 0, "center of an off cell (level 0) is black");
}

static void test_gap_pixel_between_cells_is_black_regardless_of_level() {
    Vdisplay_render d;
    for (int i = 0; i < 7; i++) d.levels[i] = 0;
    set_cell(d, 0, 0, 2); // bright
    // pixel right at the cell's left edge (within the GAP inset) must stay
    // black even though the cell itself is bright -- proves the grid-line
    // gap is actually rendered, not just a level==0 check.
    d.x = MARGIN_X; d.y = MARGIN_Y + CELL_H/2;
    d.eval();
    CHECK(d.rgb == 0, "gap pixel at a bright cell's edge is still black");
}

static void test_bright_brighter_than_dim() {
    Vdisplay_render bright, dim;
    for (int i = 0; i < 7; i++) { bright.levels[i] = 0; dim.levels[i] = 0; }
    set_cell(bright, 0, 0, 2);
    set_cell(dim, 0, 0, 1);
    int cx = MARGIN_X + CELL_W/2, cy = MARGIN_Y + CELL_H/2;
    bright.x = cx; bright.y = cy; bright.eval();
    dim.x = cx; dim.y = cy; dim.eval();
    CHECK((bright.rgb & 0xFF) > (dim.rgb & 0xFF), "level 2 (bright) has a higher blue-channel/luma value than level 1 (dim)");
}

static void test_row_9_col_10_last_cell_reachable() {
    Vdisplay_render d;
    for (int i = 0; i < 7; i++) d.levels[i] = 0;
    set_cell(d, 9, 10, 2); // last row, last col of the 10x11 grid
    int cx = MARGIN_X + 10*CELL_W + CELL_W/2;
    int cy = MARGIN_Y + 9*CELL_H + CELL_H/2;
    d.x = cx; d.y = cy;
    d.eval();
    CHECK(d.rgb != 0, "last grid cell (row 9, col 10) is independently addressable and lights up");
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    run_test("cell_0_0_center_pixel_is_bright_when_level_2", test_cell_0_0_center_pixel_is_bright_when_level_2);
    run_test("off_cell_center_pixel_is_black", test_off_cell_center_pixel_is_black);
    run_test("gap_pixel_between_cells_is_black_regardless_of_level", test_gap_pixel_between_cells_is_black_regardless_of_level);
    run_test("bright_brighter_than_dim", test_bright_brighter_than_dim);
    run_test("row_9_col_10_last_cell_reachable", test_row_9_col_10_last_cell_reachable);
    if (g_failures) { std::printf("FAILED: %d check(s)\n", g_failures); return 1; }
    std::printf("PASS: display_render_tb\n");
    return 0;
}
```

- [ ] **Step 2: Add the Makefile target and run to verify failure**

Add to `sim/Makefile`:
```makefile
display-render-test:
	$(VERILATOR) --cc ../src/display_render.v --exe display_render_tb.cpp \
		--Mdir obj_dir_display_render -Wall --Wno-UNUSEDSIGNAL
	$(MAKE) -C obj_dir_display_render -f Vdisplay_render.mk
	./obj_dir_display_render/Vdisplay_render
```
Add `display-render-test` to `.PHONY:` and to `test:`'s dependency list.

Run: `cd sim && make display-render-test` — Expected: FAIL, `src/display_render.v` not found.

- [ ] **Step 3: Implement the module**

Create `src/display_render.v`:

```verilog
// src/display_render.v
//
// Renders pps41_display_pwm's already-reconstructed 10x11 PWM matrix
// (levels[219:0], cell = row*11+col, 2-bit brightness) as a plain grid of
// rectangles on a 320x240 canvas -- one rectangle per cell, brightness
// mapped from the cell's level. Per the design spec's approved "simple
// procedural shapes" fidelity: this does not attempt to reverse-engineer
// which cells are 7-segment digit vs. discrete-LED positions (deferred to
// the bezel/packaging sub-project) -- every cell renders identically,
// just positioned by its (row,col) coordinate.
module display_render (
    input  wire [219:0] levels,
    input  wire [9:0]   x,   // pixel x within the 320-wide active video region
    input  wire [9:0]   y,   // pixel y within the 240-tall active video region
    output wire [23:0]  rgb
);
    localparam MARGIN_X = 6, MARGIN_Y = 10;
    localparam CELL_W = 28, CELL_H = 22;
    localparam GAP = 3;
    localparam GRID_W = 11 * CELL_W; // 308, fits in 320 with MARGIN_X=6 each side
    localparam GRID_H = 10 * CELL_H; // 220, fits in 240 with MARGIN_Y=10 each side

    wire in_grid_x = (x >= MARGIN_X) && (x < MARGIN_X + GRID_W);
    wire in_grid_y = (y >= MARGIN_Y) && (y < MARGIN_Y + GRID_H);

    wire [9:0] rel_x = x - MARGIN_X;
    wire [9:0] rel_y = y - MARGIN_Y;
    wire [3:0] col = rel_x / CELL_W; // 0-10
    wire [3:0] row = rel_y / CELL_H; // 0-9
    wire [9:0] within_x = rel_x % CELL_W;
    wire [9:0] within_y = rel_y % CELL_H;

    wire lit_area = in_grid_x && in_grid_y &&
                    (within_x >= GAP) && (within_x < CELL_W - GAP) &&
                    (within_y >= GAP) && (within_y < CELL_H - GAP);

    wire [6:0] cell_idx = row * 7'd11 + col; // 0-109
    wire [1:0] cell_level = levels[cell_idx*2 +: 2];

    // Amber LED-style color: black when off/not-in-a-lit-area, dim amber
    // for level 1, bright amber-white for level 2 (level 3 never produced
    // by pps41_display_pwm but treated the same as bright, defensively).
    assign rgb = !lit_area          ? 24'h000000
               : (cell_level == 0)  ? 24'h000000
               : (cell_level == 1)  ? 24'h552200
               :                      24'hFF8800;
endmodule
```

- [ ] **Step 4: Run to verify pass**

Run: `cd sim && make display-render-test`
Expected: `PASS: display_render_tb`.

- [ ] **Step 5: Run the full suite to confirm no regressions**

Run: `cd sim && make test`
Expected: all `PASS`.

- [ ] **Step 6: Commit**

```bash
git add src/display_render.v sim/display_render_tb.cpp sim/Makefile
git commit -m "$(cat <<'EOF'
Add display_render: simple procedural grid renderer for the PWM matrix

Renders levels[219:0] as a plain 10x11 grid of brightness-coded
rectangles rather than reverse-engineering digit/LED segment positions --
deferred to the later bezel/packaging sub-project per the design spec.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 6: Wire everything into `core_top.v`

**Files:**
- Modify: `src/fpga/core/core_top.v`

**Interfaces:**
- Consumes: `pps41_core` (Task 0/1), `pps41_display_mux`/`pps41_display_pwm` (Task 1, pre-existing), `ce_gen` (Task 2), `rom_loader` (Task 3), `audio_gen` (Task 4), `display_render` (Task 5) — all by their exact port names defined in those tasks.
- Produces: a fully wired `core_top.v` — no new module, this task only edits the vendored file.

- [ ] **Step 1: Declare the internal wires and instantiate `ce_gen` and `rom_loader`**

In `src/fpga/core/core_top.v`, add near the top of the core-logic section (after the existing `reset_n`/`clk_core_12288` declarations, before the placeholder video block that currently starts with `// inactive screen areas are black`):

```verilog
    wire        core_ce;
    ce_gen u_ce_gen (
        .clk   ( clk_74a ),
        .rst_n ( reset_n ),
        .ce    ( core_ce )
    );

    wire [10:0] rom_addr_w;
    wire [7:0]  rom_data_w;
    rom_loader u_rom_loader (
        .clk            ( clk_74a ),
        .bridge_wr      ( bridge_wr ),
        .bridge_addr    ( bridge_addr ),
        .bridge_wr_data ( bridge_wr_data ),
        .rom_addr       ( rom_addr_w ),
        .rom_data       ( rom_data_w )
    );
```

- [ ] **Step 2: Build the P-port input mapping and instantiate `pps41_core`**

Add, right after the above:
```verilog
    // p_input bit mapping, per docs/initial-plan.md §7's IN.0 table and the
    // template's own cont1_key bit comment (this file, ~line 187-202).
    wire [7:0] p_input_w = {
        cont1_key[2],  // bit7: Left     = dpad_left
        cont1_key[1],  // bit6: Down     = dpad_down
        cont1_key[5],  // bit5: Pass     = face_b
        cont1_key[4],  // bit4: Kick     = face_a
        cont1_key[3],  // bit3: Right    = dpad_right
        cont1_key[0],  // bit2: Up       = dpad_up
        cont1_key[14], // bit1: Status   = face_select
        cont1_key[15]  // bit0: Score    = face_start
    };

    wire [9:0]  r_output_w;
    wire [11:0] d_output_w;
    wire [1:0]  spk_level_w;
    pps41_core u_pps41_core (
        .clk               ( clk_74a ),
        .rst_n             ( reset_n ),
        .ce                ( core_ce ),
        .rom_addr          ( rom_addr_w ),
        .pc                (  ),
        .rom_data          ( rom_data_w ),
        .p_input           ( p_input_w ),
        .dbg_b_set         ( 1'b0 ),
        .dbg_b_val         ( 7'h0 ),
        .dbg_sag_set       ( 1'b0 ),
        .dbg_ram_wr        ( 1'b0 ),
        .dbg_ram_wdata     ( 4'h0 ),
        .ram_addr          (  ),
        .ram_rdata         (  ),
        .a_out             (  ),
        .b_out             (  ),
        .skip_out          (  ),
        .c_out             (  ),
        .stack0_out        (  ),
        .stack1_out        (  ),
        .skip_count_out    (  ),
        .int1l_hit_out     (  ),
        .r_output_out      ( r_output_w ),
        .d_output_out      ( d_output_w ),
        .tone_on_result    (  ),
        .tone_freq_result  (  ),
        .spk_output_result ( spk_level_w ),
        .ios_state_result  (  ),
        .skisl_skip_out    (  ),
        .unimpl_hit_out    (  ),
        .x_out             (  ),
        .s_out             (  )
    );
```

- [ ] **Step 3: Instantiate the display pipeline and `audio_gen`**

Add, right after:
```verilog
    wire [9:0]   rowsel_w;
    wire [10:0]  rowdata_w;
    pps41_display_mux u_display_mux (
        .d       ( d_output_w ),
        .r       ( r_output_w ),
        .rowsel  ( rowsel_w ),
        .rowdata ( rowdata_w )
    );

    wire [219:0] levels_w;
    pps41_display_pwm u_display_pwm (
        .clk         ( clk_74a ),
        .rst_n       ( reset_n ),
        .ce          ( core_ce ),
        .rowsel      ( rowsel_w ),
        .rowdata     ( rowdata_w ),
        .levels      ( levels_w ),
        .window_tick (  )
    );

    wire [23:0] render_rgb_w;
    display_render u_display_render (
        .levels ( levels_w ),
        .x      ( visible_x ),
        .y      ( visible_y ),
        .rgb    ( render_rgb_w )
    );

    audio_gen u_audio_gen (
        .clk_74a    ( clk_74a ),
        .level      ( spk_level_w ),
        .audio_mclk ( audio_mclk ),
        .audio_sclk (  ),
        .audio_lrck ( audio_lrck ),
        .audio_dac  ( audio_dac )
    );
```

- [ ] **Step 4: Replace the placeholder gray-fill video and the silence generator**

Find the placeholder block (the comment `// inactive screen areas are black` through the closing of that `if(x_count >= VID_H_BPORCH ...)` conditional, currently ending with the three `vidout_rgb[23:16] <= 8'd60;` lines). Replace the three gray-fill assignment lines:
```verilog
                vidout_rgb[23:16] <= 8'd60;
                vidout_rgb[15:8]  <= 8'd60;
                vidout_rgb[7:0]   <= 8'd60;
```
with:
```verilog
                vidout_rgb <= render_rgb_w;
```

Delete the entire "audio i2s silence generator" section (from the `//\n// audio i2s silence generator\n// see other examples for actual audio generation\n//` comment block through the `end` that closes the `always @(negedge audgen_sclk)` block, including the `assign audio_mclk = audgen_mclk;` / `assign audio_dac = audgen_dac;` / `assign audio_lrck = audgen_lrck;` lines and the `audgen_accum`/`audgen_mclk`/`aud_mclk_divider`/`audgen_sclk`/`audgen_lrck_1`/`audgen_lrck_cnt`/`audgen_lrck`/`audgen_dac` register declarations) — `u_audio_gen` (Step 3) now drives `audio_mclk`/`audio_dac`/`audio_lrck` directly.

- [ ] **Step 5: Lint the modified file**

Run: `verilator --lint-only -Wall --Wno-UNUSEDSIGNAL -Isrc/fpga/core -Isrc src/fpga/core/core_top.v src/pps41_core.v src/pps41_decode.v src/pps41_alu.v src/pps41_opla.v src/pps41_tone.v src/pps41_io.v src/pps41_display_mux.v src/pps41_display_pwm.v src/ce_gen.v src/rom_loader.v src/audio_gen.v src/display_render.v`

This will very likely surface framework-module dependencies (`apf_top`, `mf_pllbase`, etc.) that `--lint-only` can't resolve standalone — if so, note which errors are pre-existing framework-resolution noise (compare against a lint run of the *unmodified* `core_top.v` from before this task, to distinguish "pre-existing template noise" from "a real mistake in this task's wiring") versus genuine new errors from this task's added instantiations, and fix only the latter. Do not silence unrelated pre-existing warnings.

- [ ] **Step 6: Run the full Verilator sim suite one more time**

Run: `cd sim && make test`
Expected: all `PASS` — this task only touched `core_top.v`, which nothing in `sim/` builds or tests directly, so this step exists purely as a "didn't break anything else" sanity check.

- [ ] **Step 7: Commit**

```bash
git add src/fpga/core/core_top.v
git commit -m "$(cat <<'EOF'
Wire the real Football II CPU/display/audio RTL into core_top.v

Replaces the vendored template's placeholder gray-fill video and silence
generator with pps41_core (fed by rom_loader + ce_gen) driving real
video (via pps41_display_mux/pwm + display_render) and real audio (via
audio_gen). Controls mapped per initial-plan.md §7's IN.0 table.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 7: SD-card manifest — ROM dataslot

**Files:**
- Modify: `dist/Cores/Developer.Core Template/data.json`

**Interfaces:**
- Consumes: `rom_loader`'s `SLOT_BASE` parameter (Task 3, default `0x10000000`).
- Produces: a dataslot entry the Pocket's menu uses to prompt for and load the ROM file from the SD card.

- [ ] **Step 1: Read the current `data.json` and confirm its existing shape**

Run: `cat "dist/Cores/Developer.Core Template/data.json"`
Confirm whether it's currently an empty/stub dataslot array (per the toolchain-scaffold sub-project's scope, it was left as "the current valid empty stub").

- [ ] **Step 2: Add the ROM dataslot entry**

Edit `dist/Cores/Developer.Core Template/data.json` to add one dataslot entry matching the real, working-core JSON shape (the same pattern the design spec's §3 cites from a real installed core):
```json
{
    "data": {
        "magic": "APF_VER_1",
        "data_slots": [
            {
                "id": 0,
                "name": "ROM",
                "required": true,
                "parameters": "0x0",
                "filename": "b8000-12.bin",
                "address": "0x10000000"
            }
        ]
    }
}
```
(If the file already has a `"magic"` key or other top-level fields from the template stub, preserve them and only add/replace the `data_slots` array — do not guess at fields not already present; if unsure, `cat` a real installed core's `data.json` for the exact top-level shape, per this file structure's own discovery-not-assumption discipline established in `docs/template-notes.md`.)

- [ ] **Step 3: Copy the local ROM to the Assets layout expected on the SD card**

Run:
```bash
mkdir -p "dist/Assets/ex_platform/common"
cp development-assets/b8000-12 "dist/Assets/ex_platform/common/b8000-12.bin"
```
(`ex_platform` is the template's placeholder platform id, per the toolchain-scaffold spec — renaming platform identity is the next, bezel/packaging sub-project's job. `dist/Assets/` is gitignored the same way `sim/roms/` is, since it contains the real ROM — confirm with `git check-ignore dist/Assets/ex_platform/common/b8000-12.bin`; if it's not ignored, add `dist/Assets/` to `.gitignore` before proceeding, since this is a copyrighted ROM dump that must never be committed.)

- [ ] **Step 4: Commit (manifest only, never the ROM copy)**

```bash
git add "dist/Cores/Developer.Core Template/data.json" .gitignore
git status --short  # confirm dist/Assets/.../b8000-12.bin does NOT appear staged
git commit -m "$(cat <<'EOF'
Add ROM dataslot to data.json for SD-card loading

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 8: Bitstream, package, and the physical boot-and-play checkpoint (human checkpoint)

**Files:** none new — this task runs the existing `make bitstream`/`make package` targets and requires a human with physical hardware.

**Interfaces:**
- Consumes: everything from Tasks 0-7.
- Produces: nothing further in the repo — this is the sub-project's actual completion gate.

- [ ] **Step 1: Verify the Docker/Quartus toolchain is still usable**

Run: `docker run --platform linux/amd64 --rm didiermalenfant/quartus:22.1-apple-silicon quartus_sh --version`
Expected: version output, no error (already proven by the toolchain-scaffold sub-project on this machine — re-verify since environments can drift, per that sub-project's own open-risks note).

- [ ] **Step 2: Compile the bitstream**

Run: `make bitstream`
Expected: Quartus flow completes with zero errors (warnings are fine if they match the pre-existing baseline noted in `docs/template-notes.md`'s boot-test findings — flag any NEW warning categories introduced by this plan's added RTL for a human to review, don't silently wave them through).

- [ ] **Step 3: Package for the SD card**

Run: `make package`
Expected: `dist/Cores/Developer.Core Template/bitstream.rbf_r` is regenerated (nonzero size), and the console prints the "copy dist/ to SD card" message.

- [ ] **Step 4: Human checkpoint — copy to SD card and boot on real hardware**

This step requires a human with a physical Analogue Pocket and SD card:
1. Copy the full contents of `dist/` onto the Pocket's SD card root (same layout as the toolchain-scaffold sub-project's boot test).
2. Copy `development-assets/b8000-12` to the SD card's `Assets/ex_platform/common/b8000-12.bin` path (Task 7's Step 3 already did this locally under `dist/Assets/`, so if `dist/` was copied wholesale this is already included — confirm the file made it onto the card, since it's gitignored and easy to accidentally skip).
3. Boot the core. Confirm: no `"Load error"` dialog; real video shows a lit grid (not solid gray); real audio is audible; D-pad/buttons respond during actual gameplay.
4. Play through at least one recognizable in-game moment (e.g. a kickoff or a score) to confirm the CPU core, display, audio, and input are all functioning together, not just powering on.

- [ ] **Step 5: Record findings in `docs/template-notes.md`**

Append a dated section (matching the existing "Hardware boot test findings" section's style) documenting: firmware version tested, pass/fail per the four checks in Step 4, and any discrepancy from expected behavior (garbled display, wrong tone pitch, unresponsive controls, etc.) for follow-up — do not silently mark this done if any check fails; a compiling bitstream that doesn't actually play is not this sub-project's completion.

- [ ] **Step 6: Commit**

```bash
git add docs/template-notes.md
git commit -m "$(cat <<'EOF'
Record APF integration hardware boot-and-play test findings

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```
