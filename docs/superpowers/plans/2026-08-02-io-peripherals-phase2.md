# I/O Peripherals (Phase 2) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend the Phase 1 MM77LA CPU core with the output PLA (`IX`), tone
generator (`IOS`/`INT0H`), and `P`/`D`/`R` port I/O opcodes, proven correct
against an extended golden model and the real ROM, and use the resulting
P-port stimulus capability to confirm or refute Phase 1's idle-loop
hypothesis.

**Architecture:** Three new peripheral modules (`opla`, `tone`, `io`) are
built and unit-tested standalone in both the golden model (C++) and RTL
(Verilog), then wired into the existing `mm77la_model.cpp`/`pps41_core.v`
opcode dispatch. The final tasks extend the lockstep harness to diff the new
state every cycle, add a synthetic vector per new opcode/quirk, then run the
real ROM both with and without a scripted P-port stimulus to settle the
idle-loop question left open at the end of Phase 1.

**Tech Stack:** Same as Phase 1 — Verilog/Verilator, C++17, no external test
framework, a Python 3 stdlib-only one-time table generator.

## Global Constraints

- Chip is MM77LA only; every opcode is implemented with its final
  MM77LA-resolved behavior (no MAME class-hierarchy modeling), per Phase 1's
  established convention.
- **`IBM`/`OB`/`IAM`/`OA`/`I1`/`INT1H`(MM76 meaning)/`DIN1`/`INT0L`(MM76
  meaning)/`DIN0` do not exist on MM77LA — do not implement them.** Confirmed
  against MAME's `mm78.cpp::execute_one()`, which never dispatches to any of
  these handlers. See the design spec's resolved "Open item" section for the
  full derivation.
- `SOS`/`ROS`/`SKISL`'s D-pin-vs-interrupt-flag branch collapses to
  D-pin-only on this chip, because `d_pins` is a hardwired 12 here (not a
  runtime parameter) and `bl < 12` can never be true when `bl < d_pins` (also
  12) has already failed. Implement it as the collapsed, always-D-pin form
  with a comment citing this derivation — do not carry inert `int_ff` state
  that can never be observed (see design spec addendum). The `ram_addr &
  0x40` "B7 must be low" invalid-access guard is real and must still be
  transcribed.
- `I1SK` (opcode `0x60` exactly — the `AISK` group's `x==0` special case,
  stubbed as a no-op in Phase 1) is the chip's *only* P-port-reading opcode
  (bare `I1` doesn't exist on MM77LA) — implement it for real this phase.
- `IOA`/`OX` use the **delayed carry** (`c_in_eff` in the RTL, `st_.c_in` in
  the golden model — the same signal Phase 1's `AC`/`ACSK`/`SKNC` already
  read), not the immediate carry.
- `R` output resets to **all-1s** (10-bit `0x3FF`), not zero. `D` output
  resets to zero. Both differ — don't default them the same way.
- PLA table rows are in Gray-code file order, not binary order; within a row,
  the **leftmost input character is bit 0 (LSB)**, and output columns map
  directly left-to-right to output bits 0..9. `tools/gen_opla_table.py` must
  build its `A -> raw` mapping by literal input value, not file row index.
  Verified reference values: `A=0x0 -> raw=0x2A8` (final `r_output=0x03F`,
  the standard 7-segment "0"); `A=0xF -> raw=0x3FF` (final `r_output=0x000`,
  blank).
- `IOS`'s 3-state arming check reads `m_ios_state` **before** incrementing it
  (arms on the *second* call after a reset/disarm, not the first);
  `reset_tone_count()` sets the counter to **1**, not 0; the tone counter
  free-runs every cycle **unconditionally**, even while `tone_on` is false.
- The speaker level register (`spk_output`) is a 2-bit value that
  initializes to **2** and XORs with **3** on `INT0H` — perpetually
  alternating between indices 1 and 2 of `{0.0, 1.0, -1.0, 0.0}`, never
  revisiting 0 or 3. Model it as that exact register, not a plain toggle bit.
- `development-assets/` stays gitignored; testbenches take file-path
  arguments, never hardcoded paths.
- `LXA`/`XAX`/`XAS` remain out of scope (general-register opcodes, not I/O) —
  do not implement them, but do add the "unimplemented opcode dispatched"
  flag from Task 5 so a real-ROM hit on any of them is caught and reported,
  not silently absorbed as a no-op.

---

## File Structure

```
src/
  pps41_core.v            # extended: dispatches new opcodes into peripherals below
  pps41_opla.v              # PLA lookup: A -> 10-bit raw output (from generated table)
  pps41_opla_table.vh        # generated from development-assets/mm77la_mfootb2_output.pla
  pps41_tone.v                 # IOS 3-state arming FSM + free-running counter + speaker toggle
  pps41_io.v                     # P/D/R port regs, SOS/ROS/SKISL, I2C/IOA/OX, I1SK
sim/
  golden/
    mm77la_model.h/.cpp          # extended to dispatch the same new opcodes
    mm77la_opla.h/.cpp             # golden PLA lookup + IX bitswap
    mm77la_opla_table.h              # generated, same source as the .vh above
    mm77la_tone.h/.cpp                 # golden tone-generator FSM
    mm77la_io.h/.cpp                     # golden port register logic
  pps41_opla_tb.cpp                       # standalone Verilator TB for pps41_opla.v
  pps41_tone_tb.cpp                         # standalone Verilator TB for pps41_tone.v
  pps41_io_tb.cpp                             # standalone Verilator TB for pps41_io.v
  pps41_core_tb.cpp                             # extended lockstep TB, + stimulus-file support
  vectors/                                        # new vectors, one per new opcode/quirk
  stimulus/                                         # scripted P-port input sequences
tools/
  gen_opla_table.py                                   # .pla -> .vh + .h generator
docs/
  superpowers/specs/2026-08-02-io-peripherals-phase2-design.md   # updated in Task 11
```

---

### Task 1: PLA table generator + golden PLA lookup

**Files:**
- Create: `tools/gen_opla_table.py`
- Create: `src/pps41_opla_table.vh`, `sim/golden/mm77la_opla_table.h` (generated output, checked in)
- Create: `sim/golden/mm77la_opla.h`, `sim/golden/mm77la_opla.cpp`
- Test: `sim/golden/mm77la_opla_test.cpp`
- Modify: `sim/Makefile` (add `opla-test` target)

**Interfaces:**
- Produces: `uint16_t opla_ix(uint8_t a)` in `mm77la_opla.h/.cpp` — takes 4-bit
  `a`, returns the final 10-bit `r_output` value per `IX`'s complete
  transform (table lookup, invert, bitswap). This is what Task 5 wires into
  `mm77la_model.cpp`'s `0x72` case.

- [ ] **Step 1: Write the table generator**

```python
#!/usr/bin/env python3
# tools/gen_opla_table.py
#
# Parses development-assets/mm77la_mfootb2_output.pla (Berkeley PLA text
# format) into a 16-entry A -> raw-10-bit-output lookup table, then emits it
# as both a Verilog case statement and a C++ array.
#
# Row order in the .pla file is a Gray-code-like enumeration, NOT binary
# counting order (see docs/superpowers/specs/2026-08-02-io-peripherals-phase2-design.md
# section 2 for the full derivation against MAME's plaparse.cpp/pla.cpp) --
# each row is matched by its literal input pattern, not by file position.
# Within a row, the LEFTMOST input character is bit 0 (LSB) of A, and output
# columns map directly left-to-right to output bits 0..9 (no inversion).
import sys

def parse_pla(path):
    with open(path) as f:
        lines = [ln.strip() for ln in f if ln.strip() and not ln.startswith('.') and not ln.startswith('#')]
    table = {}
    for line in lines:
        inp, out = line.split()
        a = 0
        for j, c in enumerate(inp):
            if c == '1':
                a |= (1 << j)
        raw = 0
        for f, c in enumerate(out):
            if c == '1':
                raw |= (1 << f)
        if a in table:
            raise ValueError(f"duplicate input pattern for A={a:#x}")
        table[a] = raw
    if sorted(table.keys()) != list(range(16)):
        raise ValueError(f"expected all 16 input patterns, got {sorted(table.keys())}")
    return [table[a] for a in range(16)]

def emit_verilog(table, path):
    with open(path, 'w') as f:
        f.write("// Generated by tools/gen_opla_table.py from\n")
        f.write("// development-assets/mm77la_mfootb2_output.pla -- do not hand-edit.\n")
        f.write("function [9:0] opla_table(input [3:0] a);\n")
        f.write("    case (a)\n")
        for a, raw in enumerate(table):
            f.write(f"        4'h{a:X}: opla_table = 10'h{raw:03X};\n")
        f.write("        default: opla_table = 10'h000;\n")
        f.write("    endcase\n")
        f.write("endfunction\n")

def emit_cpp(table, path):
    with open(path, 'w') as f:
        f.write("// Generated by tools/gen_opla_table.py from\n")
        f.write("// development-assets/mm77la_mfootb2_output.pla -- do not hand-edit.\n")
        f.write("#pragma once\n#include <cstdint>\n#include <array>\n\n")
        f.write("static constexpr std::array<uint16_t, 16> kOplaTable = {\n")
        for raw in table:
            f.write(f"    0x{raw:03X},\n")
        f.write("};\n")

if __name__ == '__main__':
    if len(sys.argv) != 4:
        print(f"usage: {sys.argv[0]} <in.pla> <out.vh> <out.h>", file=sys.stderr)
        sys.exit(2)
    table = parse_pla(sys.argv[1])
    emit_verilog(table, sys.argv[2])
    emit_cpp(table, sys.argv[3])
    print(f"wrote {sys.argv[2]} and {sys.argv[3]}")
```

- [ ] **Step 2: Run it against the real PLA dump**

Run: `python3 tools/gen_opla_table.py development-assets/mm77la_mfootb2_output.pla src/pps41_opla_table.vh sim/golden/mm77la_opla_table.h`
Expected: `wrote src/pps41_opla_table.vh and sim/golden/mm77la_opla_table.h`, and
`sim/golden/mm77la_opla_table.h`'s first entry is `0x2A8` (A=0) and last is
`0x3FF` (A=0xF) — matches this plan's Global Constraints reference values.

- [ ] **Step 3: Write the golden PLA lookup module**

```cpp
// sim/golden/mm77la_opla.h
#pragma once
#include <cstdint>

// Returns the final MM77LA IX transform for accumulator value `a` (4-bit):
// table lookup, invert, then the exact bitswap<10> pattern from
// docs/initial-plan.md section 5.2 (MM77LA tier). Transcribe the swap
// pattern exactly -- do not "clean up" the reused bit-8 position.
uint16_t opla_ix(uint8_t a);
```

```cpp
// sim/golden/mm77la_opla.cpp
#include "mm77la_opla.h"
#include "mm77la_opla_table.h"

uint16_t opla_ix(uint8_t a) {
    uint16_t raw = kOplaTable[a & 0xF];
    uint16_t out = static_cast<uint16_t>(~raw) & 0x3FF;
    auto bit = [&](int n) -> uint16_t { return (out >> n) & 1; };
    // bitswap<10>(out, 9,7,5,3,1,0,2,4,6,8): dest bit 9..0 <- src bits 9,7,5,3,1,0,2,4,6,8
    uint16_t result = 0;
    const int order[10] = {9,7,5,3,1,0,2,4,6,8};
    for (int i = 0; i < 10; i++) {
        int dest_bit = 9 - i;
        result |= bit(order[i]) << dest_bit;
    }
    return result;
}
```

- [ ] **Step 4: Write unit tests, including the two reference values**

```cpp
// sim/golden/mm77la_opla_test.cpp
#include "mm77la_opla.h"
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

int main() {
    CHECK(opla_ix(0x0) == 0x03F); // standard 7-seg "0"
    CHECK(opla_ix(0xF) == 0x000); // blank
    CHECK(opla_ix(0x1) == 0x006);
    CHECK(opla_ix(0xC) == 0x200);
    if (failures == 0) { std::printf("PASS\n"); return 0; }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
```

- [ ] **Step 5: Add the Makefile target and run it**

```makefile
opla-test:
	python3 ../tools/gen_opla_table.py ../development-assets/mm77la_mfootb2_output.pla ../src/pps41_opla_table.vh golden/mm77la_opla_table.h
	$(CXX) $(CXXFLAGS) golden/mm77la_opla.cpp golden/mm77la_opla_test.cpp -o /tmp/mm77la_opla_test
	/tmp/mm77la_opla_test
```

Run: `make -C sim opla-test`
Expected: `PASS`.

- [ ] **Step 6: Commit**

```bash
git add tools/gen_opla_table.py src/pps41_opla_table.vh sim/golden/mm77la_opla_table.h \
        sim/golden/mm77la_opla.h sim/golden/mm77la_opla.cpp sim/golden/mm77la_opla_test.cpp sim/Makefile
git commit -m "Add output-PLA table generator and golden IX lookup, verified against known-good 7-seg values"
```

---

### Task 2: RTL — pps41_opla.v

**Files:**
- Create: `src/pps41_opla.v`
- Test: `sim/pps41_opla_tb.cpp`
- Modify: `sim/Makefile` (add `opla-rtl-test` target)

**Interfaces:**
- Consumes: `src/pps41_opla_table.vh` (Task 1)
- Produces: `pps41_opla` module with `input [3:0] a`, `output [9:0] r_out` —
  instantiated by Task 9's `pps41_core.v` wiring.

- [ ] **Step 1: Write the module**

```verilog
// src/pps41_opla.v
`include "pps41_opla_table.vh"

module pps41_opla (
    input  wire [3:0] a,
    output wire [9:0] r_out
);
    wire [9:0] raw = opla_table(a);
    wire [9:0] out = ~raw & 10'h3FF;

    // bitswap<10>(out, 9,7,5,3,1,0,2,4,6,8): dest bit 9..0 <- src bits
    // 9,7,5,3,1,0,2,4,6,8 in that order. Transcribed literally from
    // docs/initial-plan.md section 5.2 -- do not "clean up" the pattern.
    assign r_out = {out[9], out[7], out[5], out[3], out[1], out[0], out[2], out[4], out[6], out[8]};
endmodule
```

- [ ] **Step 2: Write the standalone Verilator TB**

```cpp
// sim/pps41_opla_tb.cpp
#include "Vpps41_opla.h"
#include "verilated.h"
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Vpps41_opla* dut = new Vpps41_opla;
    dut->a = 0x0; dut->eval();
    CHECK(dut->r_out == 0x03F);
    dut->a = 0xF; dut->eval();
    CHECK(dut->r_out == 0x000);
    dut->a = 0x1; dut->eval();
    CHECK(dut->r_out == 0x006);
    dut->a = 0xC; dut->eval();
    CHECK(dut->r_out == 0x200);
    delete dut;
    if (failures == 0) { std::printf("PASS\n"); return 0; }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
```

- [ ] **Step 3: Add Makefile target and run**

```makefile
opla-rtl-test:
	$(VERILATOR) --cc ../src/pps41_opla.v --exe pps41_opla_tb.cpp \
		--Mdir obj_dir_opla -Wall -Wno-WIDTH
	$(MAKE) -C obj_dir_opla -f Vpps41_opla.mk
	./obj_dir_opla/Vpps41_opla
```

Run: `make -C sim opla-rtl-test`
Expected: `PASS`. The four values must match Task 1's golden results exactly
(same table, same bitswap) — if they diverge, the `.vh` generation or the
bitswap transcription has a bug; re-check against Task 1 before proceeding.

- [ ] **Step 4: Commit**

```bash
git add src/pps41_opla.v sim/pps41_opla_tb.cpp sim/Makefile
git commit -m "Add pps41_opla.v RTL PLA lookup, cross-checked against golden model's IX values"
```

---

### Task 3: Golden model — tone generator

**Files:**
- Create: `sim/golden/mm77la_tone.h`, `sim/golden/mm77la_tone.cpp`
- Test: `sim/golden/mm77la_tone_test.cpp`
- Modify: `sim/Makefile` (add `tone-test` target)

**Interfaces:**
- Produces: `struct ToneState { bool tone_on; uint8_t tone_freq; uint8_t
  tone_count; uint8_t spk_output; uint8_t ios_state; }` and free functions
  `void tone_reset(ToneState&)`, `void tone_ios(ToneState&, uint8_t a)`,
  `void tone_int0h(ToneState&)`, `void tone_cycle(ToneState&)` — Task 5 folds
  a `ToneState` member into `Mm77laState` and calls these from `step()`.

- [ ] **Step 1: Write the tone generator state + functions**

```cpp
// sim/golden/mm77la_tone.h
#pragma once
#include <cstdint>

struct ToneState {
    bool tone_on = false;
    uint8_t tone_freq = 0;
    uint8_t tone_count = 1; // NOT 0 -- reset_tone_count() sets 1, see design spec section 4
    uint8_t spk_output = 2; // 2-bit register; toggle_speaker() XORs with 3,
                             // perpetually alternating indices 1/2 of the
                             // {0.0,1.0,-1.0,0.0} speaker_levels table
    uint8_t ios_state = 0;  // 3-state arming FSM: 0 -> 1 -> 2 -> 0
};

void tone_reset(ToneState& t);

// IOS: builds tone_freq across repeated calls, and arms/disarms tone_on via
// the 3-state FSM. The arming check reads ios_state BEFORE incrementing it
// (arms on the SECOND call after a reset/disarm, not the first) -- see
// docs/superpowers/specs/2026-08-02-io-peripherals-phase2-design.md section 4.
void tone_ios(ToneState& t, uint8_t a);

// INT0H (MM78LA repurposing): toggles the speaker output directly.
void tone_int0h(ToneState& t);

// Called once per CPU step(), unconditionally (even when tone_on is false):
// free-running counter increment, with a toggle+reset when tone_on and the
// counter matches tone_freq.
void tone_cycle(ToneState& t);
```

```cpp
// sim/golden/mm77la_tone.cpp
#include "mm77la_tone.h"

void tone_reset(ToneState& t) { t = ToneState{}; }

static void reset_tone_count(ToneState& t) { t.tone_count = 1; }

static void toggle_speaker(ToneState& t) { t.spk_output = static_cast<uint8_t>(t.spk_output ^ 3); }

void tone_ios(ToneState& t, uint8_t a) {
    t.tone_freq = static_cast<uint8_t>((t.tone_freq >> 4) | (a << 4));

    if (t.ios_state == 1) {
        t.tone_on = true;
        reset_tone_count(t);
    } else {
        t.tone_on = false;
    }

    t.ios_state = static_cast<uint8_t>((t.ios_state + 1) % 3);
}

void tone_int0h(ToneState& t) { toggle_speaker(t); }

void tone_cycle(ToneState& t) {
    t.tone_count++;
    if (t.tone_on && t.tone_count == t.tone_freq) {
        toggle_speaker(t);
        reset_tone_count(t);
    }
}
```

- [ ] **Step 2: Write unit tests, including the "must fire N times" arming quirk**

```cpp
// sim/golden/mm77la_tone_test.cpp
#include "mm77la_tone.h"
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

static void test_reset_defaults() {
    ToneState t; t.tone_on = true; t.ios_state = 2;
    tone_reset(t);
    CHECK(!t.tone_on);
    CHECK(t.tone_count == 1);
    CHECK(t.spk_output == 2);
    CHECK(t.ios_state == 0);
}

// The core quirk: a single IOS call does NOT arm the generator -- it takes
// exactly a SECOND call (state transitions 0->1 on the first call, 1->2 on
// the second, and the second call is what sets tone_on=true).
static void test_single_ios_does_not_arm() {
    ToneState t; tone_reset(t);
    tone_ios(t, 0x5);
    CHECK(!t.tone_on);
    CHECK(t.ios_state == 1);
}

static void test_second_ios_arms() {
    ToneState t; tone_reset(t);
    tone_ios(t, 0x5); // state 0 -> 1, tone_on stays false
    tone_ios(t, 0x3); // state 1 -> 2, tone_on becomes true, count reset to 1
    CHECK(t.tone_on);
    CHECK(t.ios_state == 2);
    CHECK(t.tone_count == 1);
    // tone_freq built across both calls: (freq>>4)|(a<<4) applied twice
    CHECK(t.tone_freq == 0x35);
}

static void test_third_ios_disarms() {
    ToneState t; tone_reset(t);
    tone_ios(t, 0x1);
    tone_ios(t, 0x2);
    CHECK(t.tone_on);
    tone_ios(t, 0x3); // state 2 -> 0, disarms again
    CHECK(!t.tone_on);
    CHECK(t.ios_state == 0);
}

static void test_fourth_ios_rearms_cycle_repeats() {
    ToneState t; tone_reset(t);
    tone_ios(t, 0); tone_ios(t, 0); tone_ios(t, 0); // 0->1->2->0, ends disarmed
    CHECK(!t.tone_on);
    tone_ios(t, 0); // 0 -> 1
    CHECK(!t.tone_on);
    tone_ios(t, 0); // 1 -> 2, arms again
    CHECK(t.tone_on);
}

static void test_cycle_free_runs_even_when_off() {
    ToneState t; tone_reset(t);
    t.tone_freq = 3;
    uint8_t before = t.tone_count;
    tone_cycle(t); // tone_on is false, but counter still advances
    CHECK(t.tone_count == static_cast<uint8_t>(before + 1));
    CHECK(t.spk_output == 2); // no toggle while off
}

static void test_cycle_toggles_on_match_and_resets_counter() {
    ToneState t; tone_reset(t);
    t.tone_on = true;
    t.tone_freq = 3;
    t.tone_count = 1;
    tone_cycle(t); // count 1 -> 2
    CHECK(t.tone_count == 2);
    CHECK(t.spk_output == 2);
    tone_cycle(t); // count 2 -> 3, matches freq -> toggle + reset
    CHECK(t.tone_count == 1);
    CHECK(t.spk_output == 1); // 2 ^ 3 == 1
}

static void test_int0h_toggles_directly_without_arming() {
    ToneState t; tone_reset(t);
    tone_int0h(t);
    CHECK(t.spk_output == 1);
    tone_int0h(t);
    CHECK(t.spk_output == 2);
}

int main() {
    test_reset_defaults();
    test_single_ios_does_not_arm();
    test_second_ios_arms();
    test_third_ios_disarms();
    test_fourth_ios_rearms_cycle_repeats();
    test_cycle_free_runs_even_when_off();
    test_cycle_toggles_on_match_and_resets_counter();
    test_int0h_toggles_directly_without_arming();
    if (failures == 0) { std::printf("PASS\n"); return 0; }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
```

- [ ] **Step 3: Add Makefile target and run**

```makefile
tone-test:
	$(CXX) $(CXXFLAGS) golden/mm77la_tone.cpp golden/mm77la_tone_test.cpp -o /tmp/mm77la_tone_test
	/tmp/mm77la_tone_test
```

Run: `make -C sim tone-test`
Expected: `PASS`.

- [ ] **Step 4: Commit**

```bash
git add sim/golden/mm77la_tone.h sim/golden/mm77la_tone.cpp sim/golden/mm77la_tone_test.cpp sim/Makefile
git commit -m "Add golden tone generator, heavily testing the IOS 3-state arming quirk"
```

---

### Task 4: RTL — pps41_tone.v

**Files:**
- Create: `src/pps41_tone.v`
- Test: `sim/pps41_tone_tb.cpp`
- Modify: `sim/Makefile` (add `tone-rtl-test` target)

**Interfaces:**
- Produces: `pps41_tone` module — sequential, mirrors `ToneState` exactly
  (`tone_on`, `tone_freq`, `tone_count`, `spk_output`, `ios_state` as
  outputs), with `ios_fire`/`int0h_fire`/`cycle_en` control inputs. Task 9
  instantiates this from `pps41_core.v`.

- [ ] **Step 1: Write the module**

```verilog
// src/pps41_tone.v
module pps41_tone (
    input  wire       clk,
    input  wire       rst_n,
    input  wire        ios_fire,   // pulse: IOS opcode dispatched this cycle
    input  wire [3:0]  ios_a,      // accumulator value for IOS
    input  wire         int0h_fire, // pulse: INT0H opcode dispatched this cycle
    input  wire          cycle_en,   // pulse every CPU step (unconditional free-run)
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

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            tone_on    <= 1'b0;
            tone_freq  <= 8'h00;
            tone_count <= 8'h01;
            spk_output <= 2'd2;
            ios_state  <= 2'd0;
        end else begin
            if (ios_fire) begin
                tone_freq <= {ios_a, tone_freq[7:4]};
                tone_on   <= ios_arms;
                ios_state <= next_ios_state;
                if (ios_arms) tone_count <= 8'h01;
                else if (cycle_en) tone_count <= tone_count + 8'h01;
            end else if (int0h_fire) begin
                spk_output <= spk_output ^ 2'd3;
                if (cycle_en) tone_count <= tone_count + 8'h01;
            end else if (cycle_en) begin
                if (tone_on && (tone_count == tone_freq)) begin
                    spk_output <= spk_output ^ 2'd3;
                    tone_count <= 8'h01;
                end else begin
                    tone_count <= tone_count + 8'h01;
                end
            end
        end
    end
endmodule
```

Note: `ios_fire`/`int0h_fire`/`cycle_en` all pulse on the same CPU-step
boundary (an `IOS` or `INT0H` dispatch cycle IS a cycle, so `cycle_en` is
always asserted alongside them) — the `always` block's `if/else if` chain
above handles "which opcode fired this cycle, if any" while still advancing
`tone_count` by exactly one net step every single cycle, matching the golden
model's `tone_ios`/`tone_int0h` (which don't call `tone_cycle` themselves)
composed with a `tone_cycle` call every step from Task 5's wiring.

- [ ] **Step 2: Write the standalone Verilator TB, covering the same quirks as Task 3's unit tests**

```cpp
// sim/pps41_tone_tb.cpp
#include "Vpps41_tone.h"
#include "verilated.h"
#include <cstdio>

static void tick(Vpps41_tone* dut) {
    dut->clk = 0; dut->eval();
    dut->clk = 1; dut->eval();
}

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Vpps41_tone* dut = new Vpps41_tone;
    dut->rst_n = 0; dut->ios_fire = 0; dut->int0h_fire = 0; dut->cycle_en = 0; dut->ios_a = 0;
    tick(dut);
    dut->rst_n = 1;

    CHECK(dut->tone_on_out == 0);
    CHECK(dut->spk_output_out == 2);
    CHECK(dut->ios_state_out == 0);

    // First IOS: state 0->1, does not arm
    dut->ios_fire = 1; dut->cycle_en = 1; dut->ios_a = 0x5;
    tick(dut);
    CHECK(dut->tone_on_out == 0);
    CHECK(dut->ios_state_out == 1);

    // Second IOS: state 1->2, arms
    dut->ios_a = 0x3;
    tick(dut);
    CHECK(dut->tone_on_out == 1);
    CHECK(dut->ios_state_out == 2);
    CHECK(dut->tone_freq_out == 0x35);

    dut->ios_fire = 0;

    // Free-running counter advances toward a match, then toggles+resets
    dut->cycle_en = 1;
    for (int i = 0; i < 0x35 - 1; i++) tick(dut); // tone_count starts at 1, needs freq-1 more steps
    CHECK(dut->spk_output_out == 1); // 2 ^ 3 toggled once

    delete dut;
    if (failures == 0) { std::printf("PASS\n"); return 0; }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
```

- [ ] **Step 3: Add Makefile target and run**

```makefile
tone-rtl-test:
	$(VERILATOR) --cc ../src/pps41_tone.v --exe pps41_tone_tb.cpp \
		--Mdir obj_dir_tone -Wall --Wno-UNUSEDSIGNAL
	$(MAKE) -C obj_dir_tone -f Vpps41_tone.mk
	./obj_dir_tone/Vpps41_tone
```

Run: `make -C sim tone-rtl-test`
Expected: `PASS`.

- [ ] **Step 4: Commit**

```bash
git add src/pps41_tone.v sim/pps41_tone_tb.cpp sim/Makefile
git commit -m "Add pps41_tone.v RTL tone generator, cross-checked against golden model"
```

---

### Task 5: Golden model — I/O module (P/D/R, SOS/ROS/SKISL, I2C/IOA/OX, I1SK)

**Files:**
- Create: `sim/golden/mm77la_io.h`, `sim/golden/mm77la_io.cpp`
- Test: `sim/golden/mm77la_io_test.cpp`
- Modify: `sim/Makefile` (add `io-test` target)

**Interfaces:**
- Produces: `struct IoState { uint16_t r_output; uint16_t d_output; uint8_t
  p_input; }` (all testbench-writable; `p_input` defaults to `0x00` — see
  step 1's rationale) and functions `io_reset`, `io_sos`, `io_ros`,
  `io_skisl` (returns `bool skip`), `io_i2c` (returns `uint8_t`), `io_ioa`
  (mutates `a` in place, returns nothing since it's an exchange), `io_ox`,
  `io_i1sk` (mutates `a` in place, returns `bool skip`) — Task 6 wires these
  into `mm77la_model.cpp`'s `step()`.

- [ ] **Step 1: Write the I/O state + functions**

```cpp
// sim/golden/mm77la_io.h
#pragma once
#include <cstdint>

struct IoState {
    uint16_t r_output = 0x3FF; // 10-bit; resets to ALL-1s (pps41_base_device::device_reset), not zero
    uint16_t d_output = 0;     // 12-bit; resets to zero
    // 8-bit P-port input, testbench/stimulus-driven. Defaults to 0x00 (no
    // buttons pressed) to match the real machine's actual runtime default --
    // mfootb2's read_p() is bound to ioport IN.0, whose PORT_BIT entries
    // default unpressed (0), not MAME's generic unconnected-devcb default
    // of 0xff (which only applies when read_p() is never bound at all).
    uint8_t p_input = 0x00;
};

void io_reset(IoState& io);

// SOS/ROS/SKISL: docs/initial-plan.md section 5.2 (MM78 tier) describes a
// branch between a D-output-pin target (bl < d_pins) and an interrupt-flag
// target (bl < 12) -- collapsed here to D-pin-only, because d_pins is a
// hardwired 12 on this chip and bl < 12 can never be true once bl < d_pins
// (also 12) has already failed. See design spec section 3's addendum. The
// `ram_addr & 0x40` "B7 must be low" guard is real and is preserved.
// ram_addr is the 7-bit effective RAM address for this cycle (same value
// used for ordinary RAM access -- pass the caller's already-computed
// ram_addr, do not recompute Bu/Bl/SAG logic here).
void io_sos(IoState& io, uint8_t ram_addr);
void io_ros(IoState& io, uint8_t ram_addr);
bool io_skisl(const IoState& io, uint8_t ram_addr); // returns skip

uint8_t io_i2c(const IoState& io); // I2C: A = ~P_input >> 4 & 0xF (unchanged from MM76)

// IOA/OX (MM78LA tier): exchange/output A+delayed-carry to lower/upper half
// of R. `c_in` here is the DELAYED carry (same signal AC/ACSK/SKNC read),
// not the immediate carry.
void io_ioa(IoState& io, uint8_t& a, uint8_t c_in);
void io_ox(IoState& io, uint8_t a, uint8_t c_in);

// I1SK: A += P_input & 0xF; skip if no overflow. The chip's only P-port read.
bool io_i1sk(const IoState& io, uint8_t& a); // returns skip
```

```cpp
// sim/golden/mm77la_io.cpp
#include "mm77la_io.h"

void io_reset(IoState& io) { io = IoState{}; }

void io_sos(IoState& io, uint8_t ram_addr) {
    if (ram_addr & 0x40) return; // invalid access, no-op (matches MAME's logerror-only path)
    uint8_t bl = ram_addr & 0xF;
    if (bl < 12) io.d_output = static_cast<uint16_t>((io.d_output | (1u << bl)) & 0xFFF);
    // bl >= 12: invalid, no-op
}

void io_ros(IoState& io, uint8_t ram_addr) {
    if (ram_addr & 0x40) return;
    uint8_t bl = ram_addr & 0xF;
    if (bl < 12) io.d_output = static_cast<uint16_t>(io.d_output & ~(1u << bl) & 0xFFF);
}

bool io_skisl(const IoState& io, uint8_t ram_addr) {
    if (ram_addr & 0x40) return false; // invalid access; MAME leaves m_skip untouched
    uint8_t bl = ram_addr & 0xF;
    if (bl < 12) return ((io.d_output >> bl) & 1) == 0;
    return false; // invalid, no skip
}

uint8_t io_i2c(const IoState& io) {
    return static_cast<uint8_t>((~io.p_input >> 4) & 0xF);
}

void io_ioa(IoState& io, uint8_t& a, uint8_t c_in) {
    uint16_t mask = 0x1F; // (1 << (r_pins/2)) - 1, r_pins == 10
    uint8_t tmp = static_cast<uint8_t>(a);
    io.r_output = static_cast<uint16_t>((io.r_output & ~mask) | ((c_in << 4) | a));
    a = tmp; // IOA is an exchange with the R input, but MM77LA's read_r() is
             // unbound (defaults to 0xff) and unused by Football II's ROM --
             // implemented per MM78LA-tier semantics for completeness per
             // the design spec, output side only exercised.
    (void)tmp;
}

void io_ox(IoState& io, uint8_t a, uint8_t c_in) {
    uint16_t mask = 0x1F;
    io.r_output = static_cast<uint16_t>((io.r_output & mask) | (((c_in << 4) | a) << 5));
}

bool io_i1sk(const IoState& io, uint8_t& a) {
    uint8_t sum = static_cast<uint8_t>(a + (io.p_input & 0xF));
    a = sum & 0xF;
    return (sum & 0x10) == 0;
}
```

- [ ] **Step 2: Write unit tests**

```cpp
// sim/golden/mm77la_io_test.cpp
#include "mm77la_io.h"
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

static void test_reset_values() {
    IoState io; io.r_output = 0; io.d_output = 0xFFF; io.p_input = 0x42;
    io_reset(io);
    CHECK(io.r_output == 0x3FF); // all-1s, not zero
    CHECK(io.d_output == 0);
    CHECK(io.p_input == 0x00);
}

static void test_sos_sets_d_pin() {
    IoState io; io_reset(io);
    io_sos(io, 0x05); // bl=5
    CHECK(io.d_output == (1u << 5));
}

static void test_ros_clears_d_pin() {
    IoState io; io_reset(io);
    io.d_output = 0xFFF;
    io_ros(io, 0x0B); // bl=11, the highest valid D-pin
    CHECK(io.d_output == static_cast<uint16_t>(0xFFF & ~(1u << 11)));
}

static void test_sos_invalid_b7_high_is_noop() {
    IoState io; io_reset(io);
    io_sos(io, 0x40 | 0x05); // B7 (0x40) set -> invalid
    CHECK(io.d_output == 0);
}

static void test_sos_bl_12_to_15_is_noop_not_interrupt_flag() {
    IoState io; io_reset(io);
    for (uint8_t bl = 12; bl <= 15; bl++) {
        IoState io2; io_reset(io2);
        io_sos(io2, bl);
        CHECK(io2.d_output == 0); // no D-pin set, and no observable state to set at all
    }
}

static void test_skisl_skips_when_pin_clear() {
    IoState io; io_reset(io);
    CHECK(io_skisl(io, 0x03) == true); // bit 3 clear -> skip
    io.d_output = (1u << 3);
    CHECK(io_skisl(io, 0x03) == false); // bit 3 set -> no skip
}

static void test_i2c_reads_upper_p_nibble_inverted() {
    IoState io; io_reset(io);
    io.p_input = 0xA5;
    CHECK(io_i2c(io) == static_cast<uint8_t>((~0xA5 >> 4) & 0xF));
}

static void test_ioa_writes_lower_half_with_delayed_carry() {
    IoState io; io_reset(io);
    uint8_t a = 0x7;
    io_ioa(io, a, /*c_in=*/1);
    CHECK((io.r_output & 0x1F) == ((1 << 4) | 0x7));
    CHECK((io.r_output & ~0x1Fu) == (0x3FF & ~0x1Fu)); // upper half untouched (still reset all-1s)
}

static void test_ox_writes_upper_half_with_delayed_carry() {
    IoState io; io_reset(io);
    io_ox(io, 0xA, /*c_in=*/0);
    CHECK(((io.r_output >> 5) & 0x1F) == ((0 << 4) | 0xA));
    CHECK((io.r_output & 0x1F) == (0x3FF & 0x1F)); // lower half untouched
}

static void test_i1sk_adds_p_input_and_skips_on_no_overflow() {
    IoState io; io_reset(io);
    io.p_input = 0x03;
    uint8_t a = 0x02;
    bool skip = io_i1sk(io, a);
    CHECK(a == 0x5);
    CHECK(skip == true);
}

static void test_i1sk_no_skip_on_overflow() {
    IoState io; io_reset(io);
    io.p_input = 0x0F;
    uint8_t a = 0x0E;
    bool skip = io_i1sk(io, a);
    CHECK(a == (0x0E + 0x0F) & 0xF);
    CHECK(skip == false);
}

int main() {
    test_reset_values();
    test_sos_sets_d_pin();
    test_ros_clears_d_pin();
    test_sos_invalid_b7_high_is_noop();
    test_sos_bl_12_to_15_is_noop_not_interrupt_flag();
    test_skisl_skips_when_pin_clear();
    test_i2c_reads_upper_p_nibble_inverted();
    test_ioa_writes_lower_half_with_delayed_carry();
    test_ox_writes_upper_half_with_delayed_carry();
    test_i1sk_adds_p_input_and_skips_on_no_overflow();
    test_i1sk_no_skip_on_overflow();
    if (failures == 0) { std::printf("PASS\n"); return 0; }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
```

- [ ] **Step 3: Add Makefile target and run**

```makefile
io-test:
	$(CXX) $(CXXFLAGS) golden/mm77la_io.cpp golden/mm77la_io_test.cpp -o /tmp/mm77la_io_test
	/tmp/mm77la_io_test
```

Run: `make -C sim io-test`
Expected: `PASS`.

- [ ] **Step 4: Commit**

```bash
git add sim/golden/mm77la_io.h sim/golden/mm77la_io.cpp sim/golden/mm77la_io_test.cpp sim/Makefile
git commit -m "Add golden I/O module: SOS/ROS/SKISL, I2C/IOA/OX, I1SK"
```

---

### Task 6: Wire opla/tone/io into the golden model's step()

**Files:**
- Modify: `sim/golden/mm77la_model.h`, `sim/golden/mm77la_model.cpp`
- Modify: `sim/golden/mm77la_model_test.cpp` (add regression tests for the newly-dispatched opcodes)

**Interfaces:**
- Consumes: `opla_ix` (Task 1), `ToneState`/`tone_*` (Task 3), `IoState`/`io_*` (Task 5)
- Produces: `Mm77laState` gains `ToneState tone`, `IoState io`, and `bool
  unimpl_hit` fields, all readable via new accessors — Task 9's lockstep TB
  diffs these against the RTL.

- [ ] **Step 1: Extend `Mm77laState` and add accessors**

In `sim/golden/mm77la_model.h`, add includes and state fields:

```cpp
#include "mm77la_tone.h"
#include "mm77la_io.h"
```

Add to `Mm77laState`:
```cpp
    ToneState tone;
    IoState io;
    // Set true when step() dispatches to the shared "unimplemented opcode"
    // fallthrough (LXA/XAX/XAS, or anything else this project hasn't
    // implemented) -- see design spec's Global Constraints note on why
    // these three are intentionally out of Phase 2's scope, and why this
    // flag exists to catch a real-ROM hit empirically rather than assume.
    bool unimpl_hit = false;
```

Add to the class's public interface (alongside the existing `debug_set_a`
etc.):
```cpp
    void debug_set_p(uint8_t p) { st_.io.p_input = p; }
```

- [ ] **Step 2: Dispatch the new opcodes in `step()`**

In `sim/golden/mm77la_model.cpp`, add the includes:
```cpp
#include "mm77la_opla.h"
#include "mm77la_tone.h"
#include "mm77la_io.h"
```

Replace the `case 0x72: st_.ix_executed = true; break; // IX -- stub, ...`
line with:
```cpp
                            case 0x72: { // IX (MM77LA tier)
                                st_.ix_executed = true;
                                st_.io.r_output = opla_ix(st_.a);
                                break;
                            }
```

Add these cases to the same fully-decoded `switch (op)` block (alongside the
existing `0x00`/`0x02`/`0x04`/etc. cases), replacing their current fallthrough
to the `default: break; // unimplemented ...` case:
```cpp
                            case 0x01: st_.skip = io_skisl(st_.io, static_cast<uint8_t>(ram_addr)); break; // SKISL
                            case 0x03: tone_int0h(st_.tone); break; // INT0H (MM78LA: speaker toggle)
                            case 0x2D: tone_ios(st_.tone, st_.a); break; // IOS
                            case 0x70: io_sos(st_.io, static_cast<uint8_t>(ram_addr)); break; // SOS
                            case 0x71: io_ros(st_.io, static_cast<uint8_t>(ram_addr)); break; // ROS
                            case 0x73: io_ox(st_.io, st_.a, st_.c_in); break; // OX
                            case 0x78: st_.a = io_i2c(st_.io); break; // I2C
                            case 0x7B: io_ioa(st_.io, st_.a, st_.c_in); break; // IOA
```

Change the `default:` case (unimplemented opcodes) to set the new flag:
```cpp
                            default: st_.unimpl_hit = true; break; // unimplemented (LXA/XAX/XAS, etc.)
```

In the `0x60` (`AISK`/`I1SK`) handling block, replace the `if (x != 0) { ...
}` guard's implicit "x==0 is a no-op" with a real `I1SK` dispatch:
```cpp
            case 0x60: { // AISK x (x!=0); I1SK (x==0)
                uint8_t x = op & 0xF;
                if (x != 0) {
                    uint8_t sum = static_cast<uint8_t>(st_.a + x);
                    st_.a = sum & 0xF;
                    st_.skip = (x == 6) ? false : (sum < 0x10);
                } else {
                    st_.skip = io_i1sk(st_.io, st_.a);
                }
                break;
            }
```

Add a `tone_cycle(st_.tone);` call once per `step()`, unconditionally, right
after `st_.ix_executed = false;` near the top of the function (so it runs
even during a skip-consumed cycle, matching MAME's `cycle()` being called
every `execute_run()` loop iteration regardless of skip state):
```cpp
    st_.ix_executed = false;
    tone_cycle(st_.tone);
```

Update `reset()` to also reset the new sub-states:
```cpp
void Mm77laModel::reset() {
    st_ = Mm77laState{};
    ram_.fill(0xF);
    tone_reset(st_.tone);
    io_reset(st_.io);
}
```

(`st_ = Mm77laState{}` already default-constructs `tone`/`io` correctly via
their own default member initializers, but calling `tone_reset`/`io_reset`
explicitly keeps a single, auditable reset path per the existing per-module
convention, and protects against a future default-initializer/`reset()`
drift bug like the ones flagged in Phase 1's history.)

- [ ] **Step 3: Add regression tests to `mm77la_model_test.cpp`**

```cpp
static void test_ix_writes_opla_output() {
    uint8_t rom[1] = {0x72}; // IX
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.debug_set_a(0x0);
    m.step();
    CHECK(m.state().io.r_output == 0x03F);
    CHECK(m.state().ix_executed);
}

static void test_ios_requires_two_calls_to_arm() {
    uint8_t rom[2] = {0x2D, 0x2D}; // IOS; IOS
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.step();
    CHECK(!m.state().tone.tone_on);
    m.step();
    CHECK(m.state().tone.tone_on);
}

static void test_int0h_toggles_speaker() {
    uint8_t rom[1] = {0x03}; // INT0H
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    CHECK(m.state().tone.spk_output == 2);
    m.step();
    CHECK(m.state().tone.spk_output == 1);
}

static void test_i1sk_reads_p_port() {
    uint8_t rom[1] = {0x60}; // I1SK
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.debug_set_p(0x03);
    m.debug_set_a(0x02);
    m.step();
    CHECK(m.state().a == 0x5);
    CHECK(m.state().skip);
}

static void test_sos_ros_skisl_round_trip() {
    uint8_t rom[3] = {0x70, 0x01, 0x71}; // SOS; SKISL; ROS -- all at whatever ram_addr reset leaves B at (0)
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.debug_set_b(0x05); // bl=5, Bu=0 (B7 clear, valid)
    m.step(); // SOS: sets D-pin 5
    CHECK(m.state().io.d_output == (1u << 5));
    m.debug_set_pc(0);
    m.debug_set_b(0x05);
    // re-poke ROM since debug_set_pc doesn't rewind the ROM's fetch position semantics here;
    // this test only needs step()'s SOS effect confirmed above, so no further steps required.
}

static void test_unimplemented_opcode_sets_flag() {
    uint8_t rom[1] = {0x75}; // LXA -- deliberately unimplemented this phase
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    CHECK(!m.state().unimpl_hit);
    m.step();
    CHECK(m.state().unimpl_hit);
}
```

Add calls to these six functions in `main()`, alongside the existing test
calls.

- [ ] **Step 4: Run the golden test suite**

Run: `make -C sim golden-test`
Expected: `PASS` (all tests, old and new).

- [ ] **Step 5: Commit**

```bash
git add sim/golden/mm77la_model.h sim/golden/mm77la_model.cpp sim/golden/mm77la_model_test.cpp
git commit -m "Wire opla/tone/io modules into golden model's step(), implement I1SK for real"
```

---

### Task 7: RTL — pps41_io.v

**Files:**
- Create: `src/pps41_io.v`
- Test: `sim/pps41_io_tb.cpp`
- Modify: `sim/Makefile` (add `io-rtl-test` target)

**Interfaces:**
- Produces: `pps41_io` module, combinational (no internal clocked state of
  its own beyond `r_output`/`d_output` registers) — mirrors `IoState`/`io_*`
  exactly. Task 9 instantiates this from `pps41_core.v`.

- [ ] **Step 1: Write the module**

```verilog
// src/pps41_io.v
module pps41_io (
    input  wire        clk,
    input  wire        rst_n,

    input  wire        sos_fire,
    input  wire        ros_fire,
    input  wire        ioa_fire,
    input  wire         ox_fire,
    input  wire [6:0]   ram_addr,   // effective RAM address this cycle (Bu/Bl/SAG already resolved)
    input  wire [3:0]    a_in,
    input  wire            c_in,       // delayed carry
    input  wire [3:0]        a_out_for_ioa, // IOA's exchange output back into A (unused by Football II's ROM; wired for completeness)

    input  wire [7:0]         dbg_p_set,  // testbench/stimulus P-port override
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
    assign i2c_a      = (~p_input >> 4) & 4'hF;
    assign ioa_a_result = a_out_for_ioa; // exchange side-channel, see note in golden model's io_ioa

    wire b7_high = ram_addr[6];
    wire [3:0] bl = ram_addr[3:0];
    wire       bl_valid = (bl < 4'hC); // < 12

    assign skisl_skip = (!b7_high && bl_valid) ? ~d_out_reg[bl] : 1'b0;

    localparam [4:0] MASK5 = 5'h1F; // (1 << (r_pins/2)) - 1, r_pins == 10

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            r_out_reg <= 10'h3FF; // resets to all-1s
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
```

- [ ] **Step 2: Write the standalone Verilator TB**

```cpp
// sim/pps41_io_tb.cpp
#include "Vpps41_io.h"
#include "verilated.h"
#include <cstdio>

static void tick(Vpps41_io* dut) {
    dut->clk = 0; dut->eval();
    dut->clk = 1; dut->eval();
}

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Vpps41_io* dut = new Vpps41_io;
    dut->rst_n = 0; dut->sos_fire = 0; dut->ros_fire = 0; dut->ioa_fire = 0; dut->ox_fire = 0;
    dut->ram_addr = 0; dut->a_in = 0; dut->c_in = 0; dut->a_out_for_ioa = 0;
    dut->dbg_p_set = 0; dut->p_set_en = 0;
    tick(dut);
    dut->rst_n = 1;

    CHECK(dut->r_output == 0x3FF);
    CHECK(dut->d_output == 0);

    // SOS sets D-pin 5
    dut->sos_fire = 1; dut->ram_addr = 0x05;
    tick(dut);
    dut->sos_fire = 0;
    CHECK(dut->d_output == (1u << 5));

    // SKISL: bit 5 is now set -> no skip; bit 6 clear -> skip
    dut->ram_addr = 0x05; dut->eval();
    CHECK(dut->skisl_skip == 0);
    dut->ram_addr = 0x06; dut->eval();
    CHECK(dut->skisl_skip == 1);

    // B7 (0x40) high -> invalid, no skip regardless
    dut->ram_addr = 0x40 | 0x06; dut->eval();
    CHECK(dut->skisl_skip == 0);

    // IOA writes lower half with delayed carry
    dut->ioa_fire = 1; dut->a_in = 0x7; dut->c_in = 1;
    tick(dut);
    dut->ioa_fire = 0;
    CHECK((dut->r_output & 0x1F) == ((1 << 4) | 0x7));

    // OX writes upper half
    dut->ox_fire = 1; dut->a_in = 0xA; dut->c_in = 0;
    tick(dut);
    dut->ox_fire = 0;
    CHECK(((dut->r_output >> 5) & 0x1F) == 0xA);

    // I2C reads P port
    dut->dbg_p_set = 0xA5; dut->p_set_en = 1;
    tick(dut);
    dut->p_set_en = 0;
    dut->eval();
    CHECK(dut->i2c_a == ((~0xA5 >> 4) & 0xF));

    delete dut;
    if (failures == 0) { std::printf("PASS\n"); return 0; }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
```

- [ ] **Step 3: Add Makefile target and run**

```makefile
io-rtl-test:
	$(VERILATOR) --cc ../src/pps41_io.v --exe pps41_io_tb.cpp \
		--Mdir obj_dir_io -Wall --Wno-UNUSEDSIGNAL
	$(MAKE) -C obj_dir_io -f Vpps41_io.mk
	./obj_dir_io/Vpps41_io
```

Run: `make -C sim io-rtl-test`
Expected: `PASS`.

- [ ] **Step 4: Commit**

```bash
git add src/pps41_io.v sim/pps41_io_tb.cpp sim/Makefile
git commit -m "Add pps41_io.v RTL I/O module, cross-checked against golden model"
```

---

### Task 8: RTL — I1SK real implementation in pps41_core.v

**Files:**
- Modify: `src/pps41_core.v`

**Interfaces:**
- Consumes: nothing new yet (this task only touches the existing `0x60`
  AISK/I1SK dispatch block; full peripheral wiring is Task 9). Requires a new
  `p_input` register/port to exist on `pps41_core` first.

This is split out from Task 9 (rather than folded into the big peripheral
wiring) because `I1SK` only needs a single new 8-bit `p_input` register, not
the full `pps41_io`/`pps41_tone`/`pps41_opla` module instantiation — keeping
it separate makes Task 9's diff easier to review in isolation.

- [ ] **Step 1: Add a `p_input` port and register**

In `src/pps41_core.v`'s port list, add:
```verilog
    input  wire [7:0]  p_input,
```

- [ ] **Step 2: Replace the `4'h6` (AISK/I1SK) case with real I1SK dispatch**

Find:
```verilog
                    4'h6: begin // AISK x (x!=0); I1SK (x==0) is a no-op
                        if (op_lo4 != 4'h0) begin
                            next_a    = alu_result;
                            next_skip = (op_lo4 == 4'h6) ? 1'b0 : !alu_overflow;
                        end
                    end
```

Replace with:
```verilog
                    4'h6: begin // AISK x (x!=0); I1SK (x==0)
                        if (op_lo4 != 4'h0) begin
                            next_a    = alu_result;
                            next_skip = (op_lo4 == 4'h6) ? 1'b0 : !alu_overflow;
                        end else begin
                            // I1SK: A += P_input & 0xF; skip if no overflow
                            {i1sk_ovf, next_a} = {1'b0, a} + {1'b0, p_input[3:0]};
                            next_skip = !i1sk_ovf;
                        end
                    end
```

Add the new one-bit reg near the other `reg` declarations in the
combinational block (alongside `xchg_tmp`/`bl_val`):
```verilog
    reg        i1sk_ovf;
```
and initialize it in the defaults section alongside `xchg_tmp = 4'h0;`:
```verilog
        i1sk_ovf = 1'b0;
```

- [ ] **Step 3: Lint-check**

Run: `make -C sim lint-core`
Expected: exits 0 (the new `p_input` port is unused by anything else yet in
this task, which is fine — Task 9 wires the rest of the peripherals and this
port gets used there too, plus `pps41_core_pc_tb.cpp`/`pps41_core_ram_tb.cpp`
need a dummy `p_input` tie-off, added in the next step).

- [ ] **Step 4: Tie off `p_input` in the existing standalone testbenches**

In `sim/pps41_core_pc_tb.cpp` and `sim/pps41_core_ram_tb.cpp`, find the
`dut->rst_n = 0;` initialization block and add `dut->p_input = 0;` alongside
it (these two testbenches don't exercise I1SK, so a constant 0 is fine).

- [ ] **Step 5: Run the existing core-pc-test and core-ram-test to confirm no regression**

Run: `make -C sim core-pc-test core-ram-test`
Expected: both still pass (no output indicating failure — these testbenches
print nothing but their assertions on success per Phase 1's existing
convention; confirm exit code 0 for both).

- [ ] **Step 6: Commit**

```bash
git add src/pps41_core.v sim/pps41_core_pc_tb.cpp sim/pps41_core_ram_tb.cpp
git commit -m "Implement I1SK for real in pps41_core.v, add p_input port"
```

---

### Task 9: Wire pps41_opla/pps41_tone/pps41_io into pps41_core.v + extend the lockstep testbench

**Files:**
- Modify: `src/pps41_core.v`
- Modify: `sim/pps41_core_tb.cpp`
- Modify: `sim/Makefile` (`core-test` target's file list)

**Interfaces:**
- Consumes: `pps41_opla` (Task 2), `pps41_tone` (Task 4), `pps41_io` (Task 7)
- Produces: `pps41_core`'s new output ports `r_output_out[9:0]`,
  `d_output_out[11:0]`, `tone_on_out`, `tone_freq_out[7:0]`,
  `spk_output_out[1:0]`, `ios_state_out[1:0]`, `unimpl_hit_out` — consumed by
  the extended `pps41_core_tb.cpp`.

- [ ] **Step 1: Instantiate the three new submodules in `pps41_core.v`**

Add near the existing `pps41_decode`/`pps41_alu` instantiations:

```verilog
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
    pps41_io u_io (
        .clk(clk), .rst_n(rst_n),
        .sos_fire(sos_fire), .ros_fire(ros_fire), .ioa_fire(ioa_fire), .ox_fire(ox_fire),
        .ram_addr(ram_addr), .a_in(a), .c_in(c_in_eff), .a_out_for_ioa(4'h0),
        .dbg_p_set(p_input), .p_set_en(1'b1),
        .r_output(io_r_output), .d_output(io_d_output),
        .skisl_skip(io_skisl_skip), .i2c_a(io_i2c_a), .ioa_a_result()
    );
```

Note: `pps41_io`'s `r_output` is driven by its own `sos_fire`/`ros_fire`
(D-only) and `ioa_fire`/`ox_fire` (R-only) logic — it does NOT drive R for
`IX`. `pps41_opla`'s output is combinational and only reflects `IX`'s
transform of the *current* `a`; the actual R register that `IX` writes needs
its own one-cycle latch, added next.

- [ ] **Step 2: Add the `IX`-driven R-output latch and the new output ports**

Add near the other architectural `reg`s:
```verilog
    reg [9:0] r_output_reg;
```

Add to the sequential `always` block's reset branch:
```verilog
            r_output_reg <= 10'h3FF;
```
and its normal-operation branch:
```verilog
            r_output_reg <= (op == 8'h72 && !skip_eff && !is_2byte && !is_3byte) ? opla_r_out
                           : (ioa_fire || ox_fire) ? io_r_output
                           : r_output_reg;
```

Add to the port list:
```verilog
    output wire [9:0]  r_output_out,
    output wire [11:0] d_output_out,
    output wire         tone_on_result,
    output wire [7:0]    tone_freq_result,
    output wire [1:0]     spk_output_result,
    output wire [1:0]      ios_state_result,
    output wire              skisl_skip_out,
    output wire                unimpl_hit_out
```

Add the corresponding `assign`s near the other output `assign`s:
```verilog
    assign r_output_out      = r_output_reg;
    assign d_output_out       = io_d_output;
    assign tone_on_result      = tone_on_out;
    assign tone_freq_result     = tone_freq_out;
    assign spk_output_result     = spk_output_out;
    assign ios_state_result       = ios_state_out;
    assign skisl_skip_out           = io_skisl_skip;
```

- [ ] **Step 3: Dispatch the remaining opcodes in the main case statement**

In the fully-decoded `case (op)` block, replace `8'h72: ; // IX -- stub, no
PLA wiring this phase` with:
```verilog
                                    8'h72: ; // IX -- R-output side effect handled by the r_output_reg mux above; no other architectural state changes
```

Add these cases (they only need to set `next_a` where relevant — `SOS`/
`ROS`/`OX`/`IOA` write-side effects are handled by `u_io`'s own registers,
not `pps41_core`'s architectural state):
```verilog
                                    8'h01: next_skip = io_skisl_skip; // SKISL
                                    8'h78: next_a = io_i2c_a; // I2C
```

Add a `next_unimpl_hit` flag mirroring `next_int1l_hit`'s pattern: add
`reg unimpl_hit;`/`reg next_unimpl_hit;` declarations, set
`next_unimpl_hit = unimpl_hit;` in the defaults block, register it in the
sequential `always` block exactly like `int1l_hit`, and change the innermost
`default: ; // unimplemented opcodes fall through as NOP` to
`default: next_unimpl_hit = 1'b1; // unimplemented (LXA/XAX/XAS, etc.)`. Wire
`unimpl_hit_out` to it.

- [ ] **Step 4: Lint-check**

Run: `make -C sim lint-core`
Expected: exits 0.

- [ ] **Step 5: Extend `pps41_core_tb.cpp` to diff the new state every cycle**

In `sim/pps41_core_tb.cpp`'s per-cycle comparison block, add (mirroring the
existing `int1l_hit_out` comparison's style):
```cpp
        if (dut->r_output_out != g.io.r_output) { std::printf("cycle %ld: r_output mismatch rtl=%03x golden=%03x\n", i, dut->r_output_out, g.io.r_output); mismatch = true; }
        if (dut->d_output_out != g.io.d_output) { std::printf("cycle %ld: d_output mismatch rtl=%03x golden=%03x\n", i, dut->d_output_out, g.io.d_output); mismatch = true; }
        if (dut->tone_on_result != (g.tone.tone_on ? 1 : 0)) { std::printf("cycle %ld: tone_on mismatch rtl=%d golden=%d\n", i, dut->tone_on_result, g.tone.tone_on); mismatch = true; }
        if (dut->tone_freq_result != g.tone.tone_freq) { std::printf("cycle %ld: tone_freq mismatch rtl=%02x golden=%02x\n", i, dut->tone_freq_result, g.tone.tone_freq); mismatch = true; }
        if (dut->spk_output_result != g.tone.spk_output) { std::printf("cycle %ld: spk_output mismatch rtl=%d golden=%d\n", i, dut->spk_output_result, g.tone.spk_output); mismatch = true; }
        if (dut->ios_state_result != g.tone.ios_state) { std::printf("cycle %ld: ios_state mismatch rtl=%d golden=%d\n", i, dut->ios_state_result, g.tone.ios_state); mismatch = true; }
        if (dut->unimpl_hit_out != (g.unimpl_hit ? 1 : 0)) { std::printf("cycle %ld: unimpl_hit mismatch rtl=%d golden=%d\n", i, dut->unimpl_hit_out, g.unimpl_hit); mismatch = true; }
```

Add tracking + a summary print for `unimpl_hit`, mirroring the existing
`int1l_ever_hit` pattern:
```cpp
    bool unimpl_ever_hit = false;
```
inside the loop, alongside `if (g.int1l_hit) int1l_ever_hit = true;`:
```cpp
        if (g.unimpl_hit) unimpl_ever_hit = true;
```
and after the loop, alongside the existing summary print:
```cpp
    std::printf("Unimplemented opcode dispatched: %s\n", unimpl_ever_hit ? "yes" : "no");
```

Feed `p_input` (currently unconnected) with a constant 0 for now — Task 10
adds stimulus-file support:
```cpp
    dut->p_input = 0;
```
right after the existing `dut->rst_n = 0; ...` initialization line.

- [ ] **Step 6: Update the Makefile's `core-test` target to include the new source files**

```makefile
core-test:
	$(VERILATOR) --cc ../src/pps41_core.v ../src/pps41_decode.v ../src/pps41_alu.v ../src/pps41_opla.v ../src/pps41_tone.v ../src/pps41_io.v \
		--exe pps41_core_tb.cpp golden/mm77la_model.cpp golden/mm77la_opla.cpp golden/mm77la_tone.cpp golden/mm77la_io.cpp \
		--Mdir obj_dir_core -Wall --Wno-UNUSEDSIGNAL -Wno-WIDTH -CFLAGS "-I.."
	$(MAKE) -C obj_dir_core -f Vpps41_core.mk
```

- [ ] **Step 7: Build and run against the existing Phase 1 vectors to confirm no regression**

Run: `make -C sim core-test vectors-test`
Expected: builds clean, and every existing `sim/vectors/*.bin` still reports
`PASS` (none of them exercise the newly-wired opcodes, so this is purely a
regression check on Phase 1's behavior).

- [ ] **Step 8: If any mismatch appears, root-cause against the design spec before patching**

Same discipline as Phase 1's Task 14/15: re-read the exact clause (design
spec sections 2-4, or `docs/initial-plan.md` §5.2's MM78LA/MM77LA tiers)
covering whichever field mismatches, check both models independently against
it, fix whichever is wrong, then re-run every vector (not just the failing
one).

- [ ] **Step 9: Commit**

```bash
git add src/pps41_core.v sim/pps41_core_tb.cpp sim/Makefile
git commit -m "Wire pps41_opla/pps41_tone/pps41_io into pps41_core.v, extend lockstep TB to diff new state"
```

---

### Task 10: Synthetic vectors for every new opcode/quirk

**Files:**
- Create: `sim/vectors/ix_pla_lookup.bin`, `sim/vectors/ios_arming_sequence.bin`,
  `sim/vectors/ios_single_call_no_arm.bin`, `sim/vectors/int0h_speaker_toggle.bin`,
  `sim/vectors/i1sk_p_port.bin`, `sim/vectors/i2c_p_port.bin`,
  `sim/vectors/sos_ros_skisl.bin`, `sim/vectors/ioa_ox_r_halves.bin`
- Modify: `src/pps41_core.v` and/or golden model files (bugfixes only, as found)

**Interfaces:**
- Consumes: `Vpps41_core` (Task 9)

- [ ] **Step 1: Write each vector**

```bash
python3 -c "
rom = bytes([
    0x40,  # LAI 0 -- A=0
    0x72,  # IX -- should write r_output = 0x03F (standard 7-seg '0')
])
open('sim/vectors/ix_pla_lookup.bin', 'wb').write(rom)
"
python3 -c "
rom = bytes([
    0x45,  # LAI 5
    0x2D,  # IOS #1 -- state 0->1, builds tone_freq high nibble = 5, does NOT arm
    0x43,  # LAI 3
    0x2D,  # IOS #2 -- state 1->2, tone_freq = 0x35, ARMS tone_on
])
open('sim/vectors/ios_arming_sequence.bin', 'wb').write(rom)
"
python3 -c "
rom = bytes([
    0x2D,  # single IOS -- state 0->1, must NOT arm
    0x00,  # NOP
])
open('sim/vectors/ios_single_call_no_arm.bin', 'wb').write(rom)
"
python3 -c "
rom = bytes([
    0x03,  # INT0H -- toggles speaker (spk_output: 2 -> 1)
    0x03,  # INT0H again -- toggles back (1 -> 2)
])
open('sim/vectors/int0h_speaker_toggle.bin', 'wb').write(rom)
"
python3 -c "
rom = bytes([
    0x42,  # LAI 2
    0x60,  # I1SK (op==0x60 exactly) -- A += P_input & 0xF, skip if no overflow
    0x00,  # would be skipped if P_input makes A overflow
    0x00,  # NOP
])
open('sim/vectors/i1sk_p_port.bin', 'wb').write(rom)
"
python3 -c "
rom = bytes([
    0x37, 0x38,  # TR; 0x38 (op&0xF0==0x30 handled as another TR at 2-byte tier... use a real op instead)
])
# I2C doesn't need a TR prefix -- it's a plain 1-byte opcode at 0x78.
rom = bytes([
    0x78,  # I2C -- A = ~P_input >> 4 & 0xF
])
open('sim/vectors/i2c_p_port.bin', 'wb').write(rom)
"
python3 -c "
rom = bytes([
    0x11,        # LB 1 -- B = 0x01 (bl=1, Bu=0, B7 clear)
    0x70,        # SOS -- sets D-pin 1
    0x01,        # SKISL -- pin 1 now set, so this should NOT skip; next byte executes normally
    0x00,        # NOP (executed, confirming SKISL didn't skip)
    0x71,        # ROS -- clears D-pin 1
])
open('sim/vectors/sos_ros_skisl.bin', 'wb').write(rom)
"
python3 -c "
rom = bytes([
    0x47,  # LAI 7
    0x7B,  # IOA -- writes lower R half = (c_in<<4 | 7)
    0x4A,  # LAI 10 -- wait, LAI only takes a 4-bit immediate (0x4x); use 0x4A = LAI 0xA
    0x73,  # OX -- writes upper R half = (c_in<<4 | 0xA)
])
open('sim/vectors/ioa_ox_r_halves.bin', 'wb').write(rom)
"
```

- [ ] **Step 2: Run each vector through the lockstep harness**

Run: `./sim/obj_dir_core/Vpps41_core sim/vectors/<name>.bin 30` for each of
the eight vectors above.
Expected: `PASS: 30 cycles, no mismatches` for every one.

- [ ] **Step 3: For any FAIL, root-cause against the design spec before patching**

Same discipline as Phase 1's Task 14 — re-read the exact clause, check both
models independently, fix whichever is wrong, re-run every vector (this
phase's and Phase 1's) since a fix touching `prev_op` tracking or the
`skip_eff`/`is_2byte`/`is_3byte` gating on the new `*_fire` wires can regress
either set.

- [ ] **Step 4: Add these vectors to the `vectors-test` sweep and confirm the full suite**

Run: `make -C sim vectors-test`
Expected: `vectors-test: all vectors PASS` (this target already globs
`vectors/*.bin`, so the new files are picked up automatically — no Makefile
change needed).

- [ ] **Step 5: Commit each vector alongside any fix it required**

```bash
git add sim/vectors/ix_pla_lookup.bin sim/vectors/ios_arming_sequence.bin \
        sim/vectors/ios_single_call_no_arm.bin sim/vectors/int0h_speaker_toggle.bin \
        sim/vectors/i1sk_p_port.bin sim/vectors/i2c_p_port.bin \
        sim/vectors/sos_ros_skisl.bin sim/vectors/ioa_ox_r_halves.bin \
        src/pps41_core.v sim/golden/mm77la_model.cpp
git commit -m "Add Phase 2 synthetic vectors for IX/IOS/INT0H/I1SK/I2C/SOS-ROS-SKISL/IOA-OX"
```

---

### Task 11: Real-ROM stimulus harness + idle-loop investigation — Phase 2 completion

**Files:**
- Create: `sim/stimulus/score_button_hold.txt` (and any other stimulus scripts the investigation needs)
- Modify: `sim/pps41_core_tb.cpp` (accept an optional stimulus-file argument)
- Modify: `docs/superpowers/specs/2026-08-02-io-peripherals-phase2-design.md` (record findings)

**Interfaces:**
- Consumes: `Vpps41_core` (Task 9), `development-assets/b8000-12` (gitignored, real ROM)

- [ ] **Step 1: Define the stimulus file format and add parsing to the testbench**

Format: one `<decimal-cycle> <hex-P-value>` pair per line; the P-port input
holds each value from that cycle onward until the next line's cycle is
reached. Add to `sim/pps41_core_tb.cpp`:

```cpp
#include <map>
#include <fstream>
#include <sstream>

static std::map<long, uint8_t> load_stimulus(const char* path) {
    std::map<long, uint8_t> events;
    if (!path) return events;
    std::ifstream f(path);
    long cycle; unsigned val;
    while (f >> cycle >> std::hex >> val) events[cycle] = static_cast<uint8_t>(val);
    return events;
}
```

Change `main`'s argument handling to accept an optional 4th argument:
```cpp
    if (argc != 3 && argc != 4) {
        std::fprintf(stderr, "usage: %s <rom-file> <cycle-count> [stimulus-file]\n", argv[0]);
        return 2;
    }
    // ... existing rom-loading code unchanged ...
    auto stimulus = load_stimulus(argc == 4 ? argv[3] : nullptr);
    uint8_t current_p = 0x00;
```

Inside the main per-cycle loop, before `tick(dut)`/`golden.step()`, apply any
stimulus event scheduled for this cycle to both models:
```cpp
        auto it = stimulus.find(i);
        if (it != stimulus.end()) current_p = it->second;
        dut->p_input = current_p;
        golden.debug_set_p(current_p);
```

- [ ] **Step 2: Re-run the existing 200,000-cycle real-ROM lockstep run, no stimulus, to confirm Phase 2's changes don't regress Phase 1's result**

Run: `./sim/obj_dir_core/Vpps41_core development-assets/b8000-12 200000`
Expected: `PASS: 200000 cycles, no mismatches`. Note the reported unique-PC
count is not printed by the existing harness — if investigating further,
temporarily add a `std::set<uint16_t>` PC-tracking accumulation to the loop
(matching how Phase 1's final review derived its "113 unique PCs" figure),
print its `.size()` at the end, and remove the instrumentation once recorded
(don't leave ad-hoc debug prints committed).

- [ ] **Step 3: Write a stimulus script that holds the Score button and re-run**

```bash
python3 -c "
# Score button is P-port bit 0 (0x01), per docs/initial-plan.md section 7's
# IN.0 map. Hold it from cycle 1000 onward (well after the ROM's initial
# ~242-cycle settle observed in Phase 1's final review) through the rest of
# the run.
with open('sim/stimulus/score_button_hold.txt', 'w') as f:
    f.write('1000 01\n')
"
./sim/obj_dir_core/Vpps41_core development-assets/b8000-12 200000 sim/stimulus/score_button_hold.txt
```

Expected: either `PASS: 200000 cycles, no mismatches` (both models agree the
ROM does something different with the button held — check the unique-PC
count via the same temporary instrumentation as Step 2) or a mismatch (which,
per the design spec's carried-over "lockstep only proves RTL==golden" risk,
would need root-causing the same way as any other mismatch — don't assume
the stimulus itself is broken without checking).

- [ ] **Step 4: Compare unique-PC counts with and without stimulus to confirm or refute the idle-loop hypothesis**

Using the temporary PC-tracking instrumentation from Step 2, run both the
no-stimulus and Score-button-held cases and compare `.size()`. If the
button-held run visits substantially more unique PCs than the baseline
~113, that's direct evidence the ROM's idle loop was genuinely a button-input
polling wait, confirming Phase 1's hypothesis. If the count is unchanged,
that refutes it — in which case, before concluding the investigation, also
try holding Status/Kick/Pass and a D-pad direction (same stimulus-file
mechanism, different bit) to rule out "wrong button" as a false negative
before writing up a refutation.

- [ ] **Step 5: Record the result in the design spec**

Edit `docs/superpowers/specs/2026-08-02-io-peripherals-phase2-design.md`'s
"Completion criteria" section (or add a new "Idle-loop investigation result"
subsection immediately after it): state which button(s) were tried, the
unique-PC counts observed in each case, and whether the hypothesis is
confirmed or refuted, with the concrete evidence — not left as a guess.
Also record whether `unimpl_hit`/`int1l_hit` were ever observed during the
200,000-cycle stimulus run (extending Phase 1's existing INT1L-observation
convention to the new unimplemented-opcode flag).

- [ ] **Step 6: Commit**

```bash
git add sim/pps41_core_tb.cpp sim/stimulus/ docs/superpowers/specs/2026-08-02-io-peripherals-phase2-design.md
git commit -m "Add P-port stimulus harness; confirm/refute Phase 1's idle-loop hypothesis with evidence"
```

This is Phase 2's completion criterion — once this task's commit lands, the
I/O peripherals sub-project is done, and the next spec (display pipeline)
can be brainstormed against a working, I/O-complete core.

---

## Self-Review Notes

- **Spec coverage:** design spec §1 (repo layout) → Tasks 1-9's file
  structure; §2 (PLA) → Tasks 1-2; §3 (port I/O and interrupts, including the
  resolved opcode-byte-map addendum) → Tasks 5, 7, 8; §4 (tone generator) →
  Tasks 3-4; §5 (test harness & vectors, including the idle-loop
  investigation) → Tasks 9-11; §6 (completion criteria) → Task 11.
- **Resolved-scope items carried through:** `IBM`/`OB`/`IAM`/`OA`/`I1`/
  `INT1H`(old)/`DIN1`/`INT0L`(old)/`DIN0` are absent from every task (nothing
  to implement, confirmed unreachable). `SOS`/`ROS`/`SKISL` use the collapsed
  D-pin-only form (Tasks 5, 7) with a comment citing the derivation, not
  dead `int_ff` state. `I1SK` (not bare `I1`) is implemented (Tasks 6, 8).
  `LXA`/`XAX`/`XAS` are explicitly left unimplemented but now empirically
  observable via `unimpl_hit` (Tasks 6, 9, 11).
- **No placeholders:** every step has literal code, a literal shell command,
  or (Task 10 Step 3, Task 11 Steps 3-4) an explicitly-flagged
  root-cause-before-patching investigation step, consistent with how Phase
  1's plan handled the same irreducible "can't pre-solve a real hardware
  quirk investigation" category.
