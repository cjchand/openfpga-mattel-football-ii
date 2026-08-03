# Display Pipeline (Phase 3) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reconstruct Football II's multiplexed PWM display matrix from the
`D`/`R` port registers (Phase 2), and integrate each of the 110 matrix
cells' on-time into a flicker-free, discrete brightness level (off/dim/
bright), proven correct via golden-model/RTL lockstep the same way as
Phases 1-2.

**Architecture:** Two new modules, each built golden-model-first then RTL,
each standalone-unit-tested before integration: `pps41_display_mux` (pure
combinational `D`/`R` → `rowsel`/`rowdata` reconstruction, transcribed from
MAME's `mfootb2_state::update_display()`) and `pps41_display_pwm`
(per-cell on-time accumulation + window-boundary threshold classification,
adapted directly from the sibling FB1 project's `led_capture.v`, not
re-derived). Both are fed by the existing `pps41_core`'s `d_output_out`/
`r_output_out` ports as siblings, not instantiated inside `pps41_core.v`
itself — this phase doesn't touch CPU execution semantics at all. The final
tasks extend `pps41_core_tb.cpp`'s lockstep harness to diff the new
per-cycle accumulator state and per-window settled snapshot, add a vector
per named quirk, then re-run the real-ROM sustained run to confirm the
matrix-reconstruction/row-grouping findings against real gameplay-derived
I/O.

**Tech Stack:** Same as Phases 1-2 — Verilog/Verilator, C++17, no external
test framework.

## Global Constraints

- This phase does not modify `pps41_core.v`, `pps41_opla.v`, `pps41_tone.v`,
  or `pps41_io.v` — it only consumes `pps41_core`'s existing `d_output_out`
  (12-bit) / `r_output_out` (10-bit) ports. If any of Phase 1/2's existing
  vectors or the real-ROM lockstep run regress, that's this phase's bug to
  fix, not license to touch CPU-core files.
- Matrix reconstruction formula (verbatim from
  `docs/superpowers/specs/2026-08-02-display-pipeline-phase3-design.md` §2):
  `rowsel = D[9:0]`, `rowdata = {R[9:7], D[11], R[6:0]}` (11 bits). `D[11]`
  is the sole source of column-bit-7 — do not substitute any `R` bit there.
- Matrix is 10 rows × 11 columns = 110 cells, indexed `row*11 + col` (row
  0-9, col 0-10).
- Row grouping (design spec §2): rows {0,1,2,6,7,8,9} are 7-segment digits
  (columns 0-6 = segments a-g); row 1 additionally treats column 7 as an
  8th segment (the decimal point); rows {3,4,5} are 30 individual LEDs, no
  segment grouping.
- `WINDOW = 1583` cycles (`round(380000 / 4 / 60)`), `DIM_MIN = 24`
  (`(1583*15)/1000 + 1`), `BRIGHT_MIN = 317` (`1583/5 + 1`) — copy these
  literal constants, don't recompute with different rounding.
- No cross-window brightness smoothing (explicit design decision, diverging
  from MAME's own `pwm.cpp`): classify directly from each window's raw
  count, reset every window boundary. Mirrors FB1's `led_capture.v`
  structure — read that file for the exact pattern before writing
  `pps41_display_pwm.v`, don't re-derive independently.
- A cell increments each cycle where its row bit is set in `rowsel` AND its
  column bit is set in `rowdata` (AND-of-two-bitmasks "collision implies
  powered-on", per `pwm.cpp`'s own header comment).
- `development-assets/` stays gitignored; the real-ROM task takes a file
  path argument, never hardcoded.

---

## File Structure

```
src/
  pps41_display_mux.v        # combinational D/R -> (rowsel, rowdata)
  pps41_display_pwm.v          # per-cell accumulation + window threshold
sim/
  golden/
    mm77la_display_mux.h/.cpp    # golden matrix reconstruction
    mm77la_display_pwm.h/.cpp     # golden brightness/window integration
    mm77la_model.h/.cpp             # extended: DisplayPwmState member,
                                     # updated once per step()
  pps41_display_mux_tb.cpp           # standalone Verilator TB
  pps41_display_pwm_tb.cpp            # standalone Verilator TB
  pps41_core_tb.cpp                     # extended: instantiates both new
                                         # RTL modules alongside Vpps41_core,
                                         # diffs accumulator + settled state
  vectors/                                # existing Phase 1/2 vectors, plus
                                           # new display-quirk vectors
```

---

### Task 1: Golden model — matrix reconstruction

**Files:**
- Create: `sim/golden/mm77la_display_mux.h`, `sim/golden/mm77la_display_mux.cpp`
- Test: `sim/golden/mm77la_display_mux_test.cpp`
- Modify: `sim/Makefile` (add `display-mux-test` target)

**Interfaces:**
- Produces: `void display_mux(uint16_t d, uint16_t r, uint16_t& rowsel, uint16_t& rowdata)` — Task 3 wires this into `Mm77laModel::step()`.

- [ ] **Step 1: Write the matrix reconstruction function**

```cpp
// sim/golden/mm77la_display_mux.h
#pragma once
#include <cstdint>

// Transcribed from MAME's mfootb2_state::update_display():
//   m_display->matrix(m_d, (m_r << 1 & 0x700) | (m_d >> 4 & 0x80) | (m_r & 0x7f));
// rowsel: 10-bit bitmask (D bits 0-9) -- which of the 10 matrix rows are
//   currently strobed. Not necessarily one-hot; model the general case.
// rowdata: 11-bit column value. R[9:7] -> bits 10:8, D[11] -> bit 7 (the
//   ONLY source of column-bit-7 -- R's 10 bits never reach it), R[6:0] ->
//   bits 6:0. D[10] (the pin the driver labels "4th digit DP") is NOT
//   consumed here at all -- D[11] carries the real DP data bit.
void display_mux(uint16_t d, uint16_t r, uint16_t& rowsel, uint16_t& rowdata);
```

```cpp
// sim/golden/mm77la_display_mux.cpp
#include "mm77la_display_mux.h"

void display_mux(uint16_t d, uint16_t r, uint16_t& rowsel, uint16_t& rowdata) {
    rowsel = d & 0x3FF;
    rowdata = static_cast<uint16_t>(((r << 1) & 0x700) | ((d >> 4) & 0x80) | (r & 0x7F));
}
```

- [ ] **Step 2: Write unit tests, including the D[11]-is-the-DP-bit case**

```cpp
// sim/golden/mm77la_display_mux_test.cpp
#include "mm77la_display_mux.h"
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

static void test_rowsel_is_d_low_10_bits() {
    uint16_t rowsel, rowdata;
    display_mux(0xFFF, 0x000, rowsel, rowdata);
    CHECK(rowsel == 0x3FF); // D bits 10/11 excluded from rowsel
}

static void test_rowdata_r_low_7_bits_direct() {
    uint16_t rowsel, rowdata;
    display_mux(0x000, 0x07F, rowsel, rowdata);
    CHECK(rowdata == 0x07F);
}

static void test_rowdata_r_high_3_bits_shift_to_8_10() {
    uint16_t rowsel, rowdata;
    display_mux(0x000, 0x380, rowsel, rowdata); // R bits 7,8,9 set
    CHECK(rowdata == 0x700); // land at rowdata bits 8,9,10
}

static void test_d11_is_sole_source_of_column_bit_7() {
    uint16_t rowsel, rowdata;
    // D[11] set, R fully clear -- column bit 7 must still be set
    display_mux(0x800, 0x000, rowsel, rowdata);
    CHECK((rowdata & 0x80) == 0x80);
    // R's bits (0-9) can NEVER set column bit 7 on their own
    display_mux(0x000, 0x3FF, rowsel, rowdata);
    CHECK((rowdata & 0x80) == 0x00);
}

static void test_d10_dp_pin_not_consumed() {
    uint16_t rowsel, rowdata;
    display_mux(0x400, 0x000, rowsel, rowdata); // only D[10] set
    CHECK(rowsel == 0x000); // excluded from rowsel (bit 10 is above the mask)
    CHECK(rowdata == 0x000); // not folded into rowdata either
}

int main() {
    test_rowsel_is_d_low_10_bits();
    test_rowdata_r_low_7_bits_direct();
    test_rowdata_r_high_3_bits_shift_to_8_10();
    test_d11_is_sole_source_of_column_bit_7();
    test_d10_dp_pin_not_consumed();
    if (failures == 0) { std::printf("PASS\n"); return 0; }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
```

- [ ] **Step 3: Add Makefile target and run**

```makefile
display-mux-test:
	$(CXX) $(CXXFLAGS) golden/mm77la_display_mux.cpp golden/mm77la_display_mux_test.cpp -o /tmp/mm77la_display_mux_test
	/tmp/mm77la_display_mux_test
```

Run: `make -C sim display-mux-test`
Expected: `PASS`.

- [ ] **Step 4: Commit**

```bash
git add sim/golden/mm77la_display_mux.h sim/golden/mm77la_display_mux.cpp \
        sim/golden/mm77la_display_mux_test.cpp sim/Makefile
git commit -m "Add golden matrix reconstruction, confirming D[11] is the sole DP data source"
```

---

### Task 2: RTL — pps41_display_mux.v

**Files:**
- Create: `src/pps41_display_mux.v`
- Test: `sim/pps41_display_mux_tb.cpp`
- Modify: `sim/Makefile` (add `display-mux-rtl-test` target)

**Interfaces:**
- Produces: `pps41_display_mux` module, `input [11:0] d, input [9:0] r, output [9:0] rowsel, output [10:0] rowdata` — Task 5 instantiates this alongside `Vpps41_core`.

- [ ] **Step 1: Write the module**

```verilog
// src/pps41_display_mux.v
module pps41_display_mux (
    input  wire [11:0] d,
    input  wire [9:0]  r,
    output wire [9:0]  rowsel,
    output wire [10:0] rowdata
);
    assign rowsel  = d[9:0];
    assign rowdata = {r[9:7], d[11], r[6:0]};
endmodule
```

- [ ] **Step 2: Write the standalone Verilator TB, same cases as Task 1's golden tests**

```cpp
// sim/pps41_display_mux_tb.cpp
#include "Vpps41_display_mux.h"
#include "verilated.h"
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Vpps41_display_mux* dut = new Vpps41_display_mux;

    dut->d = 0xFFF; dut->r = 0x000; dut->eval();
    CHECK(dut->rowsel == 0x3FF);

    dut->d = 0x000; dut->r = 0x07F; dut->eval();
    CHECK(dut->rowdata == 0x07F);

    dut->d = 0x000; dut->r = 0x380; dut->eval();
    CHECK(dut->rowdata == 0x700);

    dut->d = 0x800; dut->r = 0x000; dut->eval();
    CHECK((dut->rowdata & 0x80) == 0x80);
    dut->d = 0x000; dut->r = 0x3FF; dut->eval();
    CHECK((dut->rowdata & 0x80) == 0x00);

    dut->d = 0x400; dut->r = 0x000; dut->eval();
    CHECK(dut->rowsel == 0x000);
    CHECK(dut->rowdata == 0x000);

    delete dut;
    if (failures == 0) { std::printf("PASS\n"); return 0; }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
```

- [ ] **Step 3: Add Makefile target and run**

```makefile
display-mux-rtl-test:
	$(VERILATOR) --cc ../src/pps41_display_mux.v --exe pps41_display_mux_tb.cpp \
		--Mdir obj_dir_display_mux -Wall
	$(MAKE) -C obj_dir_display_mux -f Vpps41_display_mux.mk
	./obj_dir_display_mux/Vpps41_display_mux
```

Run: `make -C sim display-mux-rtl-test`
Expected: `PASS`.

- [ ] **Step 4: Commit**

```bash
git add src/pps41_display_mux.v sim/pps41_display_mux_tb.cpp sim/Makefile
git commit -m "Add pps41_display_mux.v RTL, cross-checked against golden model"
```

---

### Task 3: Golden model — brightness/window integration

**Files:**
- Create: `sim/golden/mm77la_display_pwm.h`, `sim/golden/mm77la_display_pwm.cpp`
- Test: `sim/golden/mm77la_display_pwm_test.cpp`
- Modify: `sim/Makefile` (add `display-pwm-test` target)

**Interfaces:**
- Consumes: nothing from Tasks 1-2 directly (takes `rowsel`/`rowdata` as
  plain arguments, same as FB1's `led_capture` taking `str`/`seg`).
- Produces: `struct DisplayPwmState { uint16_t window_pos = 0; uint16_t
  cnt[110] = {}; uint8_t levels[110] = {}; bool window_tick = false; }` and
  `void display_pwm_step(DisplayPwmState&, uint16_t rowsel, uint16_t
  rowdata)` — Task 5 folds a `DisplayPwmState` member into `Mm77laState`
  and calls this once per `step()`, after `display_mux()`.

Before writing this task, read
`/Users/chandler/Projects/analogue-pocket/cores/mattel-football/src/led_capture.v`
in full — this task adapts its exact structure (parameterized window,
per-cell counter array, threshold-classify-on-boundary), not a fresh
derivation. The only real differences: 110 cells instead of 99, `WINDOW`/
`DIM_MIN`/`BRIGHT_MIN` recalculated for FB2 (see Global Constraints), no
separate `dp_in` input (FB2's DP is already folded into `rowdata` bit 7 by
Task 1's `display_mux`, unlike FB1 where it's a distinct signal), no `ce`
gate (FB2's core steps every cycle unconditionally, unlike FB1).

- [ ] **Step 1: Write the state + step function**

```cpp
// sim/golden/mm77la_display_pwm.h
#pragma once
#include <cstdint>
#include <array>

constexpr int kDisplayCells = 110; // 10 rows x 11 cols
constexpr uint16_t kDisplayWindow = 1583;   // round(380000 / 4 / 60)
constexpr uint16_t kDisplayDimMin = 24;     // (1583*15)/1000 + 1
constexpr uint16_t kDisplayBrightMin = 317; // 1583/5 + 1

struct DisplayPwmState {
    uint16_t window_pos = 0;
    std::array<uint16_t, kDisplayCells> cnt{};
    std::array<uint8_t, kDisplayCells> levels{}; // 0/1/2, settled once per window
    bool window_tick = false; // true only on the step() call that just settled a new window
};

// Called once per CPU step(), after that step's rowsel/rowdata are known.
// Accumulates on-time for cells where (rowsel bit for row) AND (rowdata bit
// for col) are both set, then classifies+resets every kDisplayWindow calls.
void display_pwm_step(DisplayPwmState& st, uint16_t rowsel, uint16_t rowdata);
```

```cpp
// sim/golden/mm77la_display_pwm.cpp
#include "mm77la_display_pwm.h"

void display_pwm_step(DisplayPwmState& st, uint16_t rowsel, uint16_t rowdata) {
    st.window_tick = false;

    for (int row = 0; row < 10; row++) {
        if (!((rowsel >> row) & 1)) continue;
        for (int col = 0; col < 11; col++) {
            if ((rowdata >> col) & 1) {
                st.cnt[row * 11 + col]++;
            }
        }
    }

    if (st.window_pos == kDisplayWindow - 1) {
        st.window_pos = 0;
        st.window_tick = true;
        for (int cell = 0; cell < kDisplayCells; cell++) {
            if (st.cnt[cell] >= kDisplayBrightMin) st.levels[cell] = 2;
            else if (st.cnt[cell] >= kDisplayDimMin) st.levels[cell] = 1;
            else st.levels[cell] = 0;
            st.cnt[cell] = 0;
        }
    } else {
        st.window_pos++;
    }
}
```

- [ ] **Step 2: Write unit tests, mirroring FB1's `led_capture_tb.cpp` cases (thresholds, hold-between-windows), adapted for the new constants and no DP-specific input**

```cpp
// sim/golden/mm77la_display_pwm_test.cpp
#include "mm77la_display_pwm.h"
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

// Drives `on` cycles of (rowsel,rowdata) then idle for the rest of one window.
static void drive_window(DisplayPwmState& st, int on, uint16_t rowsel, uint16_t rowdata) {
    for (int i = 0; i < kDisplayWindow; i++) {
        bool active = i < on;
        display_pwm_step(st, active ? rowsel : 0, active ? rowdata : 0);
    }
}

static void test_thresholds() {
    DisplayPwmState st;
    // row 2, col 3: BRIGHT_MIN=317 ticks -> 317/1583=20.03% -> bright
    drive_window(st, kDisplayBrightMin, 1u << 2, 1u << 3);
    CHECK(st.levels[2 * 11 + 3] == 2);

    drive_window(st, kDisplayDimMin, 1u << 2, 1u << 3);
    CHECK(st.levels[2 * 11 + 3] == 1);

    drive_window(st, kDisplayDimMin - 1, 1u << 2, 1u << 3);
    CHECK(st.levels[2 * 11 + 3] == 0);

    drive_window(st, kDisplayBrightMin - 1, 1u << 2, 1u << 3);
    CHECK(st.levels[2 * 11 + 3] == 1); // still dim, one tick short of bright
}

static void test_levels_hold_mid_window() {
    DisplayPwmState st;
    drive_window(st, kDisplayWindow, 1u << 0, 1u << 0); // fully lit whole window
    CHECK(st.levels[0] == 2);
    for (int i = 0; i < kDisplayWindow / 2; i++) display_pwm_step(st, 0, 0);
    CHECK(st.levels[0] == 2); // holds steady mid-window even though currently idle
}

static void test_window_tick_fires_once_per_window() {
    DisplayPwmState st;
    int ticks = 0;
    for (int i = 0; i < kDisplayWindow * 3; i++) {
        display_pwm_step(st, 0, 0);
        if (st.window_tick) ticks++;
    }
    CHECK(ticks == 3);
}

static void test_no_coincidence_no_accumulation() {
    DisplayPwmState st;
    // row bit and col bit alternate, never both present in the same call
    for (int i = 0; i < kDisplayWindow; i++) {
        bool odd = i & 1;
        display_pwm_step(st, odd ? (1u << 4) : 0, odd ? 0 : (1u << 6));
    }
    CHECK(st.levels[4 * 11 + 6] == 0);
}

int main() {
    test_thresholds();
    test_levels_hold_mid_window();
    test_window_tick_fires_once_per_window();
    test_no_coincidence_no_accumulation();
    if (failures == 0) { std::printf("PASS\n"); return 0; }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
```

- [ ] **Step 3: Add Makefile target and run**

```makefile
display-pwm-test:
	$(CXX) $(CXXFLAGS) golden/mm77la_display_pwm.cpp golden/mm77la_display_pwm_test.cpp -o /tmp/mm77la_display_pwm_test
	/tmp/mm77la_display_pwm_test
```

Run: `make -C sim display-pwm-test`
Expected: `PASS`.

- [ ] **Step 4: Commit**

```bash
git add sim/golden/mm77la_display_pwm.h sim/golden/mm77la_display_pwm.cpp \
        sim/golden/mm77la_display_pwm_test.cpp sim/Makefile
git commit -m "Add golden brightness/window integration, adapted from FB1's led_capture.v"
```

---

### Task 4: RTL — pps41_display_pwm.v

**Files:**
- Create: `src/pps41_display_pwm.v`
- Test: `sim/pps41_display_pwm_tb.cpp`
- Modify: `sim/Makefile` (add `display-pwm-rtl-test` target)

**Interfaces:**
- Produces: `pps41_display_pwm` module — `input [9:0] rowsel, input [10:0]
  rowdata, output [219:0] levels` (110 cells × 2 bits, `levels[cell*2 +: 2]`),
  `output window_tick`. Task 5 instantiates this alongside `Vpps41_core` and
  `Vpps41_display_mux`.

Adapt `led_capture.v` directly (same file referenced in Task 3) rather than
writing from scratch — the counter-array/threshold-classify structure
carries over almost verbatim, just re-sized and re-indexed.

- [ ] **Step 1: Write the module**

```verilog
// src/pps41_display_pwm.v
module pps41_display_pwm (
    input  wire         clk,
    input  wire         rst_n,
    input  wire [9:0]   rowsel,
    input  wire [10:0]  rowdata,
    output reg  [219:0] levels,    // 110 cells x 2 bits, cell = row*11 + col
    output reg           window_tick
);
    localparam integer WINDOW     = 1583;
    localparam integer DIM_MIN    = 24;
    localparam integer BRIGHT_MIN = 317;
    localparam [10:0]  WIN_LAST   = WINDOW - 1;

    reg [10:0] cnt [0:109];
    reg [10:0] window_pos;
    integer row, col, cell;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            for (cell = 0; cell < 110; cell = cell + 1) cnt[cell] <= 11'd0;
            window_pos  <= 11'd0;
            levels      <= 220'd0;
            window_tick <= 1'b0;
        end else begin
            window_tick <= 1'b0;

            for (row = 0; row < 10; row = row + 1)
                for (col = 0; col < 11; col = col + 1)
                    if (rowsel[row] && rowdata[col])
                        cnt[row * 11 + col] <= cnt[row * 11 + col] + 11'd1;

            if (window_pos == WIN_LAST) begin
                window_pos  <= 11'd0;
                window_tick <= 1'b1;
                for (cell = 0; cell < 110; cell = cell + 1) begin
                    /* verilator lint_off WIDTHEXPAND */
                    if (cnt[cell] >= BRIGHT_MIN)
                        levels[cell*2 +: 2] <= 2'd2;
                    else if (cnt[cell] >= DIM_MIN)
                        levels[cell*2 +: 2] <= 2'd1;
                    else
                        levels[cell*2 +: 2] <= 2'd0;
                    /* verilator lint_on WIDTHEXPAND */
                    cnt[cell] <= 11'd0;
                end
            end else
                window_pos <= window_pos + 11'd1;
        end
    end
endmodule
```

- [ ] **Step 2: Write the standalone Verilator TB, same cases as Task 3's golden tests**

```cpp
// sim/pps41_display_pwm_tb.cpp
#include "Vpps41_display_pwm.h"
#include "verilated.h"
#include <cstdio>

static void tick(Vpps41_display_pwm* dut) {
    dut->clk = 0; dut->eval();
    dut->clk = 1; dut->eval();
}

static const int WINDOW = 1583, DIM_MIN = 24, BRIGHT_MIN = 317;

static void drive_window(Vpps41_display_pwm* dut, int on, uint16_t rowsel, uint16_t rowdata) {
    for (int i = 0; i < WINDOW; i++) {
        bool active = i < on;
        dut->rowsel = active ? rowsel : 0;
        dut->rowdata = active ? rowdata : 0;
        tick(dut);
    }
}

static int level(Vpps41_display_pwm* dut, int row, int col) {
    int cell = row * 11 + col;
    return (dut->levels[(cell * 2) / 32] >> ((cell * 2) % 32)) & 3;
}

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Vpps41_display_pwm* dut = new Vpps41_display_pwm;
    dut->rst_n = 0; dut->rowsel = 0; dut->rowdata = 0;
    tick(dut);
    dut->rst_n = 1;

    drive_window(dut, BRIGHT_MIN, 1u << 2, 1u << 3);
    CHECK(level(dut, 2, 3) == 2);

    drive_window(dut, DIM_MIN, 1u << 2, 1u << 3);
    CHECK(level(dut, 2, 3) == 1);

    drive_window(dut, DIM_MIN - 1, 1u << 2, 1u << 3);
    CHECK(level(dut, 2, 3) == 0);

    delete dut;
    if (failures == 0) { std::printf("PASS\n"); return 0; }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
```

- [ ] **Step 3: Add Makefile target and run**

```makefile
display-pwm-rtl-test:
	$(VERILATOR) --cc ../src/pps41_display_pwm.v --exe pps41_display_pwm_tb.cpp \
		--Mdir obj_dir_display_pwm -Wall --Wno-UNUSEDSIGNAL
	$(MAKE) -C obj_dir_display_pwm -f Vpps41_display_pwm.mk
	./obj_dir_display_pwm/Vpps41_display_pwm
```

Run: `make -C sim display-pwm-rtl-test`
Expected: `PASS`.

- [ ] **Step 4: Commit**

```bash
git add src/pps41_display_pwm.v sim/pps41_display_pwm_tb.cpp sim/Makefile
git commit -m "Add pps41_display_pwm.v RTL, adapted from FB1's led_capture.v"
```

---

### Task 5: Wire into the golden model and extend the lockstep testbench

**Files:**
- Modify: `sim/golden/mm77la_model.h`, `sim/golden/mm77la_model.cpp`
- Modify: `sim/pps41_core_tb.cpp`
- Modify: `sim/Makefile` (`core-test` target's file list)

**Interfaces:**
- Consumes: `display_mux`/`mm77la_display_mux.h` (Task 1),
  `DisplayPwmState`/`display_pwm_step`/`mm77la_display_pwm.h` (Task 3),
  `pps41_display_mux`/`pps41_display_pwm` (Tasks 2, 4).
- Produces: `Mm77laState` gains a `DisplayPwmState display` field, readable
  via the existing `state()` accessor.

- [ ] **Step 1: Fold display state into the golden model**

In `sim/golden/mm77la_model.h`, add:
```cpp
#include "mm77la_display_mux.h"
#include "mm77la_display_pwm.h"
```
Add to `Mm77laState`:
```cpp
    DisplayPwmState display;
```

- [ ] **Step 2: Call display_mux + display_pwm_step once per step(), after the opcode dispatch commits this cycle's D/R**

In `sim/golden/mm77la_model.cpp`, add the includes:
```cpp
#include "mm77la_display_mux.h"
#include "mm77la_display_pwm.h"
```

At the very end of `step()` (after the `st_.prev3_op = st_.prev2_op; ...`
lines, so this cycle's `D`/`R` writes -- if this cycle's opcode was `SOS`/
`ROS`/`IOA`/`OX`/`IX` -- are already committed into `st_.io`), add:
```cpp
    uint16_t rowsel, rowdata;
    display_mux(st_.io.d_output, st_.io.r_output, rowsel, rowdata);
    display_pwm_step(st_.display, rowsel, rowdata);
```

Update `reset()` to also reset the new sub-state:
```cpp
void Mm77laModel::reset() {
    st_ = Mm77laState{};
    ram_.fill(0xF);
    tone_reset(st_.tone);
    io_reset(st_.io);
    st_.display = DisplayPwmState{};
}
```

- [ ] **Step 3: Extend `pps41_core_tb.cpp` to instantiate both new RTL modules and diff their state every cycle**

```cpp
#include "Vpps41_display_mux.h"
#include "Vpps41_display_pwm.h"
```

After constructing `dut` (`Vpps41_core* dut = new Vpps41_core;`), add:
```cpp
    Vpps41_display_mux* dmux = new Vpps41_display_mux;
    Vpps41_display_pwm* dpwm = new Vpps41_display_pwm;
    dpwm->rst_n = 0; dpwm->rowsel = 0; dpwm->rowdata = 0;
```

Inside the per-cycle loop, immediately after `tick(dut);` (so `dut`'s
`d_output_out`/`r_output_out` reflect this cycle's committed values) and
before `golden.step();`, add:
```cpp
        dmux->d = dut->d_output_out;
        dmux->r = dut->r_output_out;
        dmux->eval();
        dpwm->rowsel = dmux->rowsel;
        dpwm->rowdata = dmux->rowdata;
        dpwm->rst_n = 1;
        dpwm->clk = 0; dpwm->eval();
        dpwm->clk = 1; dpwm->eval();
```

After `golden.step();` and its existing state extraction, add the display
diff (alongside the existing per-field `if (dut->X != g.Y) {...}` checks):
```cpp
        // Settled per-window levels are diffed, not raw counters -- the RTL
        // module doesn't expose its internal cnt[] array.
        for (int cell = 0; cell < 110; cell++) {
            int rtl_level = (dpwm->levels[(cell * 2) / 32] >> ((cell * 2) % 32)) & 3;
            int golden_level = g.display.levels[cell];
            if (rtl_level != golden_level) {
                std::printf("cycle %ld: display cell %d level mismatch rtl=%d golden=%d\n", i, cell, rtl_level, golden_level);
                mismatch = true;
            }
        }
```

Free the new DUTs alongside the existing `delete dut;` at the end of `main()`:
```cpp
    delete dmux;
    delete dpwm;
```

- [ ] **Step 4: Update the Makefile's `core-test` target to include the new source/golden files**

```makefile
core-test:
	$(VERILATOR) --cc ../src/pps41_core.v ../src/pps41_decode.v ../src/pps41_alu.v ../src/pps41_opla.v ../src/pps41_tone.v ../src/pps41_io.v ../src/pps41_display_mux.v ../src/pps41_display_pwm.v \
		--exe pps41_core_tb.cpp golden/mm77la_model.cpp golden/mm77la_opla.cpp golden/mm77la_tone.cpp golden/mm77la_io.cpp golden/mm77la_display_mux.cpp golden/mm77la_display_pwm.cpp \
		--Mdir obj_dir_core -Wall --Wno-UNUSEDSIGNAL -Wno-WIDTH -CFLAGS "-I.."
	$(MAKE) -C obj_dir_core -f Vpps41_core.mk
```

- [ ] **Step 5: Build and run against existing vectors to confirm no regression**

Run: `make -C sim core-test vectors-test`
Expected: builds clean, every existing `sim/vectors/*.bin` (Phase 1's and
Phase 2's) still reports `PASS` -- none of them specifically exercise
display timing, so this is a pure regression check. Since `WINDOW=1583`
cycles is much longer than the existing vectors' 30-cycle runs, no window
boundary will be crossed by any of them; the per-cycle *accumulator* count
isn't diffed directly (only the settled `levels`, which both start at all
zero and won't have changed within 30 cycles) -- verify this by confirming
both `dpwm->levels` and `g.display.levels` are all-zero at the end of a
30-cycle vector run (add a temporary check, then remove it once confirmed).

- [ ] **Step 6: If any mismatch appears, root-cause against the design spec before patching**

Same discipline as every prior phase: re-read the exact clause (design spec
§2/§3), check both models independently, fix whichever is wrong, re-run
every vector plus the full suite (a fix touching `D`/`R` timing could, in
principle, interact with Phase 2's I/O opcodes if the wiring point in Step 2
is wrong -- e.g. reading `st_.io.d_output` before vs. after this cycle's own
`SOS`/`ROS` committed would be exactly this class of off-by-one).

- [ ] **Step 7: Commit**

```bash
git add sim/golden/mm77la_model.h sim/golden/mm77la_model.cpp sim/pps41_core_tb.cpp sim/Makefile
git commit -m "Wire display_mux/display_pwm into golden model and lockstep TB"
```

---

### Task 6: Synthetic vectors for named quirks

**Files:**
- Create: `sim/vectors/display_dp_via_d11.bin`, `sim/vectors/display_multi_row_select.bin`
- Modify: `src/pps41_display_mux.v`, `src/pps41_display_pwm.v`, and/or golden
  model files (bugfixes only, as found)

**Interfaces:**
- Consumes: `Vpps41_core` extended with `Vpps41_display_mux`/
  `Vpps41_display_pwm` (Task 5)

Phase 1/2's existing vectors run 30 cycles -- far short of one `WINDOW`
(1583 cycles), so no vector so far ever observes a settled (non-zero)
`levels` value. This task adds vectors that run long enough to cross at
least one window boundary and checks the settled result, plus the two
quirks not otherwise covered: `D[11]`-drives-the-DP and multi-row `rowsel`.

- [ ] **Step 1: Write each vector (same LFSR-PC-address placement convention as every prior vector -- see `docs/superpowers/plans/2026-08-02-io-peripherals-phase2.md` Task 10's note on why sequential byte arrays don't work)**

```bash
python3 - <<'EOF'
def lfsr_next(pc):
    feed = 1 if (pc & 0x3e) == 0 else 0
    feed ^= (pc >> 1 ^ pc) & 1
    return (pc & ~0x3f) | (pc >> 1 & 0x1f) | (feed << 5)

def assemble(instrs, size=2048):
    buf = bytearray(size)
    pc = 0
    for b in instrs:
        buf[pc] = b
        pc = lfsr_next(pc)
    return bytes(buf)

# D[11]-drives-the-DP: Phase 2 only exposes D through SOS/ROS's per-bit
# set/clear (bl 0-11, see docs/superpowers/specs/2026-08-02-io-peripherals-phase2-design.md
# section 3). LB 0xB sets Bl=11, then SOS sets D bit 11 -- the display_mux
# formula folds that bit directly into column 7 regardless of which row is
# currently selected, so no particular R value or row selection is needed
# to exercise the path; the lockstep diff (both models process the same D
# history) is what confirms RTL and golden agree on the result.
rom = [0x1B, 0x70]  # LB 0xB (bl=11, Bu=0); SOS -- sets D bit 11
open('sim/vectors/display_dp_via_d11.bin', 'wb').write(assemble(rom))

# Multi-row select: IX writes R (via the real PLA table, not a synthetic
# value) while D independently has 2 row-select bits set at once (e.g. via
# two SOS calls to different bl values before this point) -- confirms
# rowsel is treated as a genuine bitmask, not collapsed to one row.
rom2 = [0x10, 0x70, 0x11, 0x70]  # LB0;SOS(bl=0); LB1;SOS(bl=1) -- D bits 0 and 1 both set
open('sim/vectors/display_multi_row_select.bin', 'wb').write(assemble(rom2))
EOF
```

- [ ] **Step 2: Run each vector for a full window's worth of cycles**

Run: `./sim/obj_dir_core/Vpps41_core sim/vectors/display_dp_via_d11.bin 1600`
and the same for `display_multi_row_select.bin`.
Expected: `PASS: 1600 cycles, no mismatches` for both -- since both models
run the exact same D/R history, a settled non-zero level in the golden
model implies the RTL produced the identical settled level (that's what the
lockstep diff in Task 5 checks every cycle, not just at the end).

- [ ] **Step 3: For any FAIL, root-cause against the design spec before patching, then re-run the full suite**

Same discipline as every prior task.

- [ ] **Step 4: Commit**

```bash
git add sim/vectors/display_dp_via_d11.bin sim/vectors/display_multi_row_select.bin \
        src/pps41_display_mux.v src/pps41_display_pwm.v sim/golden/mm77la_model.cpp
git commit -m "Add display-pipeline synthetic vectors for D[11]-DP and multi-row select"
```

---

### Task 7: Real-ROM sustained run — Phase 3 completion

**Files:**
- Modify: `docs/superpowers/specs/2026-08-02-display-pipeline-phase3-design.md`

**Interfaces:**
- Consumes: `Vpps41_core` extended with the display pipeline (Task 5),
  `development-assets/b8000-12` (gitignored, real ROM).

- [ ] **Step 1: Run the full 200,000-cycle real-ROM lockstep run**

Run: `./sim/obj_dir_core/Vpps41_core development-assets/b8000-12 200000`
Expected: `PASS: 200000 cycles, no mismatches`.

- [ ] **Step 2: Instrument (temporarily) which of the 110 cells are ever observed at level 1 or 2, and confirm the row-grouping table**

Add a temporary `std::array<bool,110> ever_lit{}` in `pps41_core_tb.cpp`'s
main loop, set `ever_lit[cell] = true` whenever `g.display.levels[cell] >
0`, and print which rows/columns were ever active after the run. Cross-check
against `docs/superpowers/specs/2026-08-02-display-pipeline-phase3-design.md`
§2's row table: rows {0,1,2,6,7,8,9} should show columns 0-6 activity (and
row 1 possibly column 7, the DP); rows {3,4,5} should show activity spread
across columns without the 7-segment-shaped pattern. Remove the
instrumentation once the finding is recorded (don't leave ad-hoc debug code
committed).

- [ ] **Step 3: Record the result in the design spec**

Edit `docs/superpowers/specs/2026-08-02-display-pipeline-phase3-design.md`'s
"Completion criteria" section: state which rows/columns were observed
active during the real 200,000-cycle run, whether this matches the
predicted digit/LED row grouping, and whether `rowsel` was ever observed
with more than one bit set simultaneously in real gameplay-derived I/O (this
also settles §6's "confirmed or refuted empirically" open item -- write
whichever the evidence shows, don't leave it open if the run answers it).

- [ ] **Step 4: Commit**

```bash
git add docs/superpowers/specs/2026-08-02-display-pipeline-phase3-design.md
git commit -m "Complete Phase 3: real-ROM display-pipeline run confirms row/DP findings"
```

This is Phase 3's completion criterion -- once this task's commit lands,
the display-pipeline sub-project is done, and the next spec (APF/openFPGA
integration) can be brainstormed against a working, display-complete core.

---

## Self-Review Notes

- **Spec coverage:** design spec §1 (repo layout) → Tasks 1-5's file
  structure; §2 (matrix reconstruction, including the D[11]-DP finding) →
  Tasks 1-2, 6; §3 (brightness/window integration, FB1 reuse) → Tasks 3-4;
  §4 (output interface) → Task 5's `levels` exposure; §5 (test harness) →
  Tasks 5-6; §6 (completion criteria) → Task 7.
- **FB1 reuse honored, not silently re-derived:** Tasks 3-4 explicitly point
  at `led_capture.v`/`led_capture_tb.cpp` as the pattern to adapt, with the
  concrete deltas (110 vs 99 cells, recalculated constants, no `dp_in`/`ce`)
  spelled out rather than left implicit.
- **No placeholders:** every step has literal code or a literal shell
  command; Task 6/7's root-cause-before-patching and real-ROM-evidence steps
  are the same irreducible "can't pre-solve a hardware investigation"
  category every prior phase's plan also had, not an unstated TODO.
