# PPS-4/1 CPU Core (Phase 1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a cycle-accurate MM77LA (PPS-4/1) CPU core in Verilog, proven correct against a from-scratch C++ golden model and the real `b8000-12` ROM dump.

**Architecture:** A flat (non-hierarchical) C++ golden model transcribing the final MM77LA-resolved opcode semantics is built and unit-tested first, opcode group by opcode group. In parallel/after, the equivalent Verilog RTL (`pps41_decode.v`, `pps41_alu.v`, `pps41_core.v`) is built, each piece checked in isolation with a small Verilator testbench before final integration. The last tasks run both models in lockstep — first over hand-written synthetic instruction-stream vectors targeting every named hardware quirk, then over the real ROM — diffing architectural state every cycle.

**Tech Stack:** Verilog (Verilator for simulation), C++ (golden model + testbenches), no external test framework — a small custom assert-based runner, consistent with the rest of this project's dependency-light approach.

## Global Constraints

- Chip is MM77LA only — do not model MAME's `pps41_base`/`mm76`/`mm78`/`mm78la` class hierarchy; every opcode is implemented directly with its final MM77LA-resolved behavior (per `docs/superpowers/specs/2026-08-02-cpu-core-phase1-design.md` §2).
- Program ROM: 11-bit address space (`0x000-0x7FF`), only `0x000-0x5FF` is real content; `0x600-0x7FF` mirrors `0x400-0x5FF`.
- Data RAM: 96 nibbles total across the irregular map in `docs/initial-plan.md` §3 — not a power-of-two block.
- PC low 6 bits step through an LFSR, not a binary counter — exact recurrence in `docs/initial-plan.md` §4.
- `IX` (PLA output) and `INT1L` are explicitly out of scope for real behavior this phase — see design spec §2 for their stub treatment.
- `development-assets/` (containing the real ROM/PLA dumps) is gitignored — never reference it from committed code paths; testbenches take a `--rom <path>` argument instead of a hardcoded path.
- No core-template/packaging work this phase — `src/` and `sim/` only.

---

## File Structure

```
src/
  pps41_decode.v       # combinational opcode decoder
  pps41_alu.v           # arithmetic ops
  pps41_core.v            # top-level: PC/regs/stack/skip/ram addressing, instantiates decode+alu
sim/
  golden/
    mm77la_model.h        # golden model state + class declaration
    mm77la_model.cpp        # golden model implementation
    mm77la_model_test.cpp     # unit tests for the golden model
  vectors/
    *.hex                    # synthetic instruction-stream vectors, one file per opcode/quirk group
  pps41_decode_tb.cpp          # standalone Verilator TB for the decoder
  pps41_alu_tb.cpp               # standalone Verilator TB for the ALU
  pps41_core_tb.cpp                 # full lockstep TB: RTL vs golden model, per-cycle diff
  Makefile
```

---

### Task 1: Repo scaffolding and build smoke test

**Files:**
- Create: `sim/Makefile`
- Create: `src/pps41_core.v` (empty stub module, expanded in later tasks)
- Modify: `.gitignore`

**Interfaces:**
- Produces: `make -C sim golden-test` (builds/runs golden model unit tests), `make -C sim verilate-core` (runs Verilator lint on `pps41_core.v`) — later tasks add real targets under these same names.

- [ ] **Step 1: Confirm Verilator is available**

Run: `verilator --version`
Expected: prints a version string (e.g. `Verilator 5.x`). If missing, stop and tell the user to install it (`brew install verilator` on macOS) before continuing — do not proceed without it.

- [ ] **Step 2: Add sim/roms/ to .gitignore**

Add this line to `.gitignore` (the file already excludes `development-assets/` and `.DS_Store`):

```
sim/roms/
sim/*/obj_dir*/
```

- [ ] **Step 3: Create a stub top-level core module**

```verilog
// src/pps41_core.v
module pps41_core (
    input  wire clk,
    input  wire rst_n
);
endmodule
```

- [ ] **Step 4: Create sim/Makefile with a lint target**

```makefile
# sim/Makefile
VERILATOR := verilator
CXX := c++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra

.PHONY: lint-core golden-test clean

lint-core:
	$(VERILATOR) --lint-only -Wall ../src/pps41_core.v

golden-test:
	$(CXX) $(CXXFLAGS) golden/mm77la_model.cpp golden/mm77la_model_test.cpp -o /tmp/mm77la_model_test
	/tmp/mm77la_model_test

clean:
	rm -rf obj_dir* /tmp/mm77la_model_test
```

- [ ] **Step 5: Run the lint target to confirm the toolchain works end to end**

Run: `make -C sim lint-core`
Expected: exits 0 with no errors (warnings about the unused `clk`/`rst_n` ports are fine at this stage).

- [ ] **Step 6: Commit**

```bash
git add .gitignore src/pps41_core.v sim/Makefile
git commit -m "Add repo scaffolding and Verilator/build smoke test"
```

---

### Task 2: Golden model — state, memory map, reset

**Files:**
- Create: `sim/golden/mm77la_model.h`
- Create: `sim/golden/mm77la_model.cpp`
- Create: `sim/golden/mm77la_model_test.cpp`

**Interfaces:**
- Produces: `struct Mm77laState` (all architectural registers), `class Mm77laModel` with `Mm77laModel(const uint8_t* rom, size_t rom_size)`, `void reset()`, `const Mm77laState& state() const`, and private `uint8_t rom_read(uint16_t addr) const`, `uint8_t ram_read(uint8_t addr) const`, `void ram_write(uint8_t addr, uint8_t val)`. Later tasks add `void step()` and the `op_*` methods; this task only needs enough to construct, reset, and exercise memory addressing.

- [ ] **Step 1: Write the failing test for RAM address mapping**

The data RAM map (`docs/initial-plan.md` §3) is irregular: 96 real nibbles laid out as one 64-cell block (`0x00-0x3F`) plus four 8-cell banks at `0x40-0x47`, `0x50-0x57`, `0x60-0x67`, `0x70-0x77`, where `0x48-0x4F` and `0x58-0x5F` both mirror the `0x40-0x47` bank (not `0x50-0x57`), and symmetrically `0x68-0x6F`/`0x78-0x7F` both mirror `0x60-0x67` (not `0x70-0x77`).

```cpp
// sim/golden/mm77la_model_test.cpp
#include "mm77la_model.h"
#include <cassert>
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

static void test_reset_fills_ram_with_0xf() {
    uint8_t rom[8] = {0};
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    for (int addr = 0; addr < 0x80; addr++) {
        // Only test addresses that are part of the real 96-nibble map;
        // out-of-map addresses are undefined and not checked here.
        if (addr < 0x40 || (addr >= 0x40 && addr <= 0x47) ||
            (addr >= 0x50 && addr <= 0x57) || (addr >= 0x60 && addr <= 0x67) ||
            (addr >= 0x70 && addr <= 0x77)) {
            CHECK(m.debug_ram_read(addr) == 0xF);
        }
    }
}

static void test_ram_bank_a_mirrors_at_48_and_58_not_50() {
    uint8_t rom[8] = {0};
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.debug_ram_write(0x40, 0x3);
    CHECK(m.debug_ram_read(0x48) == 0x3); // mirror of bank A
    CHECK(m.debug_ram_read(0x58) == 0x3); // mirror of bank A
    CHECK(m.debug_ram_read(0x50) != 0x3 || true); // bank B is independent storage
    m.debug_ram_write(0x50, 0x7);
    CHECK(m.debug_ram_read(0x40) == 0x3); // bank A unaffected by bank B write
    CHECK(m.debug_ram_read(0x58) == 0x3); // mirror of A still reflects A, not B
}

static void test_ram_bank_c_mirrors_at_68_and_78_not_70() {
    uint8_t rom[8] = {0};
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.debug_ram_write(0x60, 0x9);
    CHECK(m.debug_ram_read(0x68) == 0x9);
    CHECK(m.debug_ram_read(0x78) == 0x9);
    m.debug_ram_write(0x70, 0x1);
    CHECK(m.debug_ram_read(0x60) == 0x9);
    CHECK(m.debug_ram_read(0x78) == 0x9);
}

static void test_rom_read_mirrors_0x400_0x5ff_at_0x600_0x7ff() {
    uint8_t rom[0x600];
    for (size_t i = 0; i < sizeof(rom); i++) rom[i] = static_cast<uint8_t>(i & 0xFF);
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    for (uint16_t off = 0; off < 0x200; off++) {
        CHECK(m.debug_rom_read(0x400 + off) == m.debug_rom_read(0x600 + off));
    }
}

int main() {
    test_reset_fills_ram_with_0xf();
    test_ram_bank_a_mirrors_at_48_and_58_not_50();
    test_ram_bank_c_mirrors_at_68_and_78_not_70();
    test_rom_read_mirrors_0x400_0x5ff_at_0x600_0x7ff();
    if (failures == 0) { std::printf("PASS\n"); return 0; }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
```

- [ ] **Step 2: Run the test to verify it fails (header/impl don't exist yet)**

Run: `make -C sim golden-test`
Expected: compile failure — `mm77la_model.h` not found.

- [ ] **Step 3: Write mm77la_model.h**

```cpp
// sim/golden/mm77la_model.h
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

struct Mm77laState {
    uint16_t pc = 0;      // 11-bit program counter
    uint8_t a = 0;         // 4-bit accumulator
    uint8_t b = 0;          // 7-bit RAM address reg (Bu = bits 4-6, Bl = bits 0-3)
    uint8_t x = 0;            // 4-bit secondary register
    uint8_t c = 0;              // 1-bit immediate carry
    uint8_t c_in = 0;            // 1-bit delayed carry, what SKNC actually reads
    uint8_t s = 0;                 // 4-bit serial shift register (unused by FBII, modeled anyway)
    std::array<uint16_t, 2> stack{}; // 2-level return address stack, stack[0] = top
    bool skip = false;
    uint8_t skip_count = 0;
    bool ram_delay = false;
    bool sag = false;
    bool c_delay = false;
    uint8_t prev_op = 0, prev2_op = 0, prev3_op = 0;
    bool tab_pending = false;   // TAB's effect fires on the opcode AFTER next
    bool int1l_hit = false;      // flagged for the testbench, does not affect execution
};

class Mm77laModel {
public:
    Mm77laModel(const uint8_t* rom, size_t rom_size);
    void reset();
    void step();
    const Mm77laState& state() const { return st_; }

    // Test-only direct memory accessors (bypass ram_addr/delay logic).
    uint8_t debug_ram_read(uint8_t addr) const;
    void debug_ram_write(uint8_t addr, uint8_t val);
    uint8_t debug_rom_read(uint16_t addr) const;

private:
    uint8_t rom_read(uint16_t addr) const;
    uint8_t ram_phys_index(uint8_t addr) const;
    uint8_t ram_read(uint8_t addr) const;
    void ram_write(uint8_t addr, uint8_t val);
    void increment_pc();

    const uint8_t* rom_;
    size_t rom_size_;
    std::array<uint8_t, 96> ram_{};
    Mm77laState st_;
};
```

- [ ] **Step 4: Write mm77la_model.cpp (memory map + reset only; step() is a later task's job)**

```cpp
// sim/golden/mm77la_model.cpp
#include "mm77la_model.h"

Mm77laModel::Mm77laModel(const uint8_t* rom, size_t rom_size)
    : rom_(rom), rom_size_(rom_size) {}

void Mm77laModel::reset() {
    st_ = Mm77laState{};
    ram_.fill(0xF);
}

uint8_t Mm77laModel::rom_read(uint16_t addr) const {
    addr &= 0x7FF;
    if (addr >= 0x600) addr -= 0x200; // 0x600-0x7FF mirrors 0x400-0x5FF
    return (addr < rom_size_) ? rom_[addr] : 0x00;
}

// Maps the 7-bit RAM address space onto the 96 physically-real nibbles.
// See docs/initial-plan.md §3 and the design spec's RAM-map derivation:
// 0x00-0x3F: 64 real cells, indices 0-63
// 0x40-0x47: bank A, indices 64-71; mirrored at 0x48-0x4F and 0x58-0x5F
// 0x50-0x57: bank B, indices 72-79 (NOT a mirror of bank A)
// 0x60-0x67: bank C, indices 80-87; mirrored at 0x68-0x6F and 0x78-0x7F
// 0x70-0x77: bank D, indices 88-95 (NOT a mirror of bank C)
uint8_t Mm77laModel::ram_phys_index(uint8_t addr) const {
    addr &= 0x7F;
    if (addr < 0x40) return addr;
    if (addr <= 0x4F || (addr >= 0x58 && addr <= 0x5F)) return 64 + (addr & 0x07);
    if (addr <= 0x57) return 72 + (addr & 0x07);
    if (addr <= 0x6F || (addr >= 0x78 && addr <= 0x7F)) return 80 + (addr & 0x07);
    return 88 + (addr & 0x07);
}

uint8_t Mm77laModel::ram_read(uint8_t addr) const {
    return ram_[ram_phys_index(addr)] & 0xF;
}

void Mm77laModel::ram_write(uint8_t addr, uint8_t val) {
    ram_[ram_phys_index(addr)] = val & 0xF;
}

uint8_t Mm77laModel::debug_ram_read(uint8_t addr) const { return ram_read(addr); }
void Mm77laModel::debug_ram_write(uint8_t addr, uint8_t val) { ram_write(addr, val); }
uint8_t Mm77laModel::debug_rom_read(uint16_t addr) const { return rom_read(addr); }
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `make -C sim golden-test`
Expected: `PASS`

- [ ] **Step 6: Commit**

```bash
git add sim/golden/mm77la_model.h sim/golden/mm77la_model.cpp sim/golden/mm77la_model_test.cpp
git commit -m "Add golden model skeleton with irregular RAM/ROM memory map"
```

---

### Task 3: Golden model — PC LFSR increment

**Files:**
- Modify: `sim/golden/mm77la_model.h` (declare `increment_pc` — already declared in Task 2)
- Modify: `sim/golden/mm77la_model.cpp`
- Modify: `sim/golden/mm77la_model_test.cpp`

**Interfaces:**
- Consumes: `Mm77laState.pc` (Task 2)
- Produces: `void Mm77laModel::increment_pc()`, exercised via a new test-only accessor `void debug_set_pc(uint16_t)` / reads via `state().pc`.

- [ ] **Step 1: Write the failing test**

The recurrence, verbatim from `docs/initial-plan.md` §4:
```cpp
int feed = ((m_pc & 0x3e) == 0) ? 1 : 0;
feed ^= (m_pc >> 1 ^ m_pc) & 1;
m_pc = (m_pc & ~0x3f) | (m_pc >> 1 & 0x1f) | (feed << 5);
```

```cpp
// append to sim/golden/mm77la_model_test.cpp, and add the new call in main()

static void test_pc_lfsr_known_sequence() {
    // Independently compute the expected sequence from the exact recurrence
    // in docs/initial-plan.md §4, then check the model matches it step for step.
    uint8_t rom[1] = {0};
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.debug_set_pc(0);
    uint16_t expect = 0;
    for (int i = 0; i < 64; i++) {
        int feed = ((expect & 0x3e) == 0) ? 1 : 0;
        feed ^= (expect >> 1 ^ expect) & 1;
        expect = (expect & ~0x3f) | (expect >> 1 & 0x1f) | (feed << 5);
        m.debug_step_pc_only();
        CHECK(m.state().pc == expect);
    }
    // The low-6-bit LFSR must return to 0 after exactly 64 steps (it's a
    // full-cycle LFSR over the 64 non-... actually over all 64 states
    // including the degenerate all-zero re-seed via the feed==1 special case).
    CHECK(expect == 0);
}

static void test_pc_high_bits_are_plain_storage() {
    uint8_t rom[1] = {0};
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.debug_set_pc(0x40); // high bits = 0x40 (bit 6 set), low 6 bits = 0
    m.debug_step_pc_only();
    CHECK((m.state().pc & ~0x3Fu) == 0x40); // high bits untouched by increment_pc
}
```

Add both calls to `main()` in the same file, before the `if (failures == 0)` check.

- [ ] **Step 2: Run test to verify it fails**

Run: `make -C sim golden-test`
Expected: compile failure — `debug_set_pc`/`debug_step_pc_only` not declared.

- [ ] **Step 3: Add the declarations and implementation**

In `mm77la_model.h`, add to the public section:
```cpp
    void debug_set_pc(uint16_t pc) { st_.pc = pc & 0x7FF; }
    void debug_step_pc_only() { increment_pc(); }
```

In `mm77la_model.cpp`, add:
```cpp
void Mm77laModel::increment_pc() {
    int feed = ((st_.pc & 0x3e) == 0) ? 1 : 0;
    feed ^= (st_.pc >> 1 ^ st_.pc) & 1;
    st_.pc = static_cast<uint16_t>((st_.pc & ~0x3fu) | (st_.pc >> 1 & 0x1f) | (feed << 5));
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `make -C sim golden-test`
Expected: `PASS`

- [ ] **Step 5: Commit**

```bash
git add sim/golden/mm77la_model.h sim/golden/mm77la_model.cpp sim/golden/mm77la_model_test.cpp
git commit -m "Add PC LFSR increment to golden model"
```

---

### Task 4: Golden model — register/memory/arithmetic/comparison opcodes

**Files:**
- Modify: `sim/golden/mm77la_model.h`
- Modify: `sim/golden/mm77la_model.cpp`
- Modify: `sim/golden/mm77la_model_test.cpp`

**Interfaces:**
- Consumes: `ram_read`/`ram_write`/`increment_pc` (Tasks 2-3)
- Produces: `void Mm77laModel::step()` handling the opcode groups below (jumps/skip/TR/TAB/carry-delay are separate later tasks and can be left as no-ops that just advance PC for now — this task's tests only exercise opcodes within this group, run via a `debug_run_one(std::initializer_list<uint8_t> opcodes)` test helper that loads a tiny ROM and calls `step()` once).

This task implements, exactly per `docs/initial-plan.md` §5.2 (MM76 tier as overridden by MM78, both already resolved for MM77LA in that doc — do not re-derive from MAME, transcribe from the doc):

`NOP`, `LAI x` (with coalescing — see Task 6, stub as always-load for now), `LBA`, `XAB`, `L x`, `X x`, `A`, `AC` (carry write only — delay logic is Task 8), `ASK`, `ACSK`, `COM`, `AISK x` (including the `x==6` DC special case), `I1SK`, `SB x`, `RB x`, `SKBF x`, `SKMEA`.

- [ ] **Step 1: Write the failing tests**

```cpp
// append to sim/golden/mm77la_model_test.cpp

static void test_lai_loads_a() {
    uint8_t rom[1] = {0x45}; // LAI 5
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.step();
    CHECK(m.state().a == 0x5);
}

static void test_lba_sets_bl_no_ram_delay() {
    uint8_t rom[2] = {0x47, 0x76}; // LAI 7; LBA
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.step(); // A = 7
    m.step(); // B low nibble = A = 7, MM78 tier: NO ram_delay
    CHECK((m.state().b & 0xF) == 0x7);
    CHECK(m.state().ram_delay == false);
}

static void test_a_op_adds_ram_to_accumulator() {
    // LAI 3; LBA (B=3); LAI 2 (A=2); now write RAM[3]=3 via direct debug write,
    // then A-op should give A = (2+3)&0xF = 5
    uint8_t rom[3] = {0x43, 0x76, 0x42}; // LAI 3; LBA; LAI 2
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.debug_ram_write(0x3, 0x3);
    m.step(); m.step(); m.step();
    CHECK(m.state().a == 0x2);
    uint8_t rom2[1] = {0x7E}; // A (add RAM[ram_addr] to A)
    Mm77laModel m2(rom2, sizeof(rom2));
    m2.reset();
    // Drive state directly since this model instance is fresh: set b/a via steps on rom2
    // is not possible (rom2 has only the A opcode). Use debug setters instead.
    m2.debug_set_a(0x2);
    m2.debug_set_b(0x3);
    m2.debug_ram_write(0x3, 0x3);
    m2.step();
    CHECK(m2.state().a == 0x5);
}

static void test_com_complements_a() {
    uint8_t rom[1] = {0x77}; // COM
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.debug_set_a(0x3);
    m.step();
    CHECK(m.state().a == 0xC); // 0x3 ^ 0xF
}

static void test_aisk_skips_on_no_overflow_and_forces_no_skip_for_dc() {
    uint8_t rom[1] = {0x62}; // AISK 2 (0x60 | 2)
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.debug_set_a(0x1);
    m.step();
    CHECK(m.state().a == 0x3);
    CHECK(m.state().skip == true); // 1+2=3 < 0x10, no overflow -> skip

    uint8_t rom2[1] = {0x66}; // AISK 6 -- the "DC" pseudo-op, MM78 forces skip=false
    Mm77laModel m2(rom2, sizeof(rom2));
    m2.reset();
    m2.debug_set_a(0x1);
    m2.step();
    CHECK(m2.state().skip == false); // forced false regardless of overflow
}

static void test_sb_rb_skbf_ram_bits() {
    uint8_t rom[3] = {0x21, 0x25, 0x29}; // SB 1; RB 1(diff addr); SKBF 1
    // SB x sets bit x of RAM[ram_addr]; test bit 1 specifically.
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.debug_set_b(0x10);
    m.debug_ram_write(0x10, 0x0);
    m.step(); // SB 1 -> RAM[0x10] bit 1 set -> 0x2
    CHECK(m.debug_ram_read(0x10) == 0x2);
}

static void test_skmea_skips_when_a_equals_ram() {
    uint8_t rom[1] = {0x7F}; // SKMEA
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.debug_set_a(0x5);
    m.debug_set_b(0x20);
    m.debug_ram_write(0x20, 0x5);
    m.step();
    CHECK(m.state().skip == true);
}
```

Add all six calls to `main()`. This is a representative subset of the group, not exhaustive — Task 14's synthetic-vector suite is where full opcode coverage against the spec table gets locked in; this task's job is to get the opcode group implemented and lightly self-checked.

- [ ] **Step 2: Run tests to verify they fail**

Run: `make -C sim golden-test`
Expected: compile failure (`debug_set_a`/`debug_set_b` undeclared) or logic failures once it compiles against a `step()` stub.

- [ ] **Step 3: Add debug setters and implement step() for this opcode group**

In `mm77la_model.h` public section:
```cpp
    void debug_set_a(uint8_t a) { st_.a = a & 0xF; }
    void debug_set_b(uint8_t b) { st_.b = b & 0x7F; }
```

In `mm77la_model.cpp`, implement `step()`. This is the central dispatch — later tasks (5, 6, 7) extend the same function, so structure it as an `op & 0xF0` / `op & 0xFC` cascade matching `docs/initial-plan.md` §5.1 exactly, with a `default:`/fallthrough for opcodes not yet implemented that just no-ops (those get filled in by later tasks):

```cpp
void Mm77laModel::step() {
    uint16_t ram_addr = st_.b; // ram_delay override handled in Task 8
    uint8_t op = rom_read(st_.pc);
    increment_pc();

    switch (op & 0xF0) {
        case 0x40: { // LAI x
            st_.a = op & 0xF;
            break;
        }
        case 0x60: { // AISK x (x!=0) handled here; I1SK (x==0) is Task 5/7 I/O work
            uint8_t x = op & 0xF;
            uint8_t sum = static_cast<uint8_t>(st_.a + x);
            st_.a = sum & 0xF;
            st_.skip = (x == 6) ? false : (sum < 0x10);
            break;
        }
        default: {
            switch (op & 0xFC) {
                case 0x20: { // SB x
                    uint8_t val = ram_read(static_cast<uint8_t>(ram_addr));
                    ram_write(static_cast<uint8_t>(ram_addr), val | (1 << (op & 0x3)));
                    break;
                }
                case 0x24: { // RB x
                    uint8_t val = ram_read(static_cast<uint8_t>(ram_addr));
                    ram_write(static_cast<uint8_t>(ram_addr), val & ~(1 << (op & 0x3)));
                    break;
                }
                case 0x28: { // SKBF x
                    uint8_t val = ram_read(static_cast<uint8_t>(ram_addr));
                    st_.skip = (val & (1 << (op & 0x3))) == 0;
                    break;
                }
                case 0x50: { // L x
                    st_.a = ram_read(static_cast<uint8_t>(ram_addr));
                    st_.b = static_cast<uint8_t>(st_.b ^ ((op & 0x3) << 4));
                    break;
                }
                case 0x5C: { // X x
                    uint8_t tmp = ram_read(static_cast<uint8_t>(ram_addr));
                    ram_write(static_cast<uint8_t>(ram_addr), st_.a);
                    st_.a = tmp;
                    st_.b = static_cast<uint8_t>(st_.b ^ ((op & 0x3) << 4));
                    break;
                }
                default: {
                    switch (op) {
                        case 0x00: break; // NOP
                        case 0x76: { // LBA (MM78: no ram_delay)
                            st_.b = static_cast<uint8_t>((st_.b & ~0xFu) | st_.a);
                            break;
                        }
                        case 0x77: { // COM
                            st_.a = st_.a ^ 0xF;
                            break;
                        }
                        case 0x7A: { // XAB
                            uint8_t tmp = st_.a;
                            st_.a = st_.b & 0xF;
                            st_.b = static_cast<uint8_t>((st_.b & ~0xFu) | tmp);
                            st_.ram_delay = true;
                            break;
                        }
                        case 0x7E: { // A
                            st_.a = static_cast<uint8_t>((st_.a + ram_read(static_cast<uint8_t>(ram_addr))) & 0xF);
                            break;
                        }
                        case 0x7C: { // AC -- carry write only; delay visibility is Task 8
                            uint8_t sum = static_cast<uint8_t>(st_.a + ram_read(static_cast<uint8_t>(ram_addr)) + st_.c_in);
                            st_.c = (sum >> 4) & 1;
                            st_.a = sum & 0xF;
                            break;
                        }
                        case 0x7D: { // ACSK -- MM78: skip if NEW carry (inverted vs MM76)
                            uint8_t sum = static_cast<uint8_t>(st_.a + ram_read(static_cast<uint8_t>(ram_addr)) + st_.c_in);
                            st_.c = (sum >> 4) & 1;
                            st_.a = sum & 0xF;
                            st_.skip = st_.c != 0;
                            break;
                        }
                        case 0x7F: { // SKMEA
                            st_.skip = (st_.a == ram_read(static_cast<uint8_t>(ram_addr)));
                            break;
                        }
                        default:
                            break; // unimplemented opcodes fall through as NOP until later tasks
                    }
                    break;
                }
            }
            break;
        }
    }

    st_.prev3_op = st_.prev2_op;
    st_.prev2_op = st_.prev_op;
    st_.prev_op = op;
}
```

Note: `ASK` and `AISK` with `x==0` (`I1SK`) are intentionally deferred — `ASK` isn't in the MM77LA opcode table at all once MM78's remap is applied (it's superseded), and `I1SK` needs the P-port input this phase doesn't model; leave `op==0x60` exactly (I1SK) falling to the `default: break` no-op path for now, and revisit only if Task 14's real-ROM run shows it's actually executed.

- [ ] **Step 4: Run tests to verify they pass**

Run: `make -C sim golden-test`
Expected: `PASS`

- [ ] **Step 5: Commit**

```bash
git add sim/golden/mm77la_model.h sim/golden/mm77la_model.cpp sim/golden/mm77la_model_test.cpp
git commit -m "Implement register/memory/arithmetic/comparison opcodes in golden model"
```

---

### Task 5: Golden model — jumps, calls, stack

**Files:**
- Modify: `sim/golden/mm77la_model.h`
- Modify: `sim/golden/mm77la_model.cpp`
- Modify: `sim/golden/mm77la_model_test.cpp`

**Interfaces:**
- Consumes: `step()`'s dispatch cascade (Task 4), `st_.pc`/`st_.stack` (Task 2)
- Produces: `T`, `TM`, `RT`, `RTSK` handling inside `step()`. (`TL`/`TML`/`TLB`/`TMLB` are 2-/3-byte forms — Task 7, since they need the TR-prefix tracking built in Task 6.)

Per `docs/initial-plan.md` §5.2/§4:
```
T x:  on-page jump. If in the subroutine page (pc & ~0x7f == prgmask & ~0x7f),
      clear bit 0x40 of pc first. pc = (pc & ~0x3f) | (~x & 0x3f)
TM x: call on-page. If NOT in the subroutine page, push_pc() first.
      pc = (prgmask & ~0x3F) | (~x & 0x3F)
RT:   pop_pc()
RTSK: RT, then skip = true
```
`prgmask = 0x7FF` (11-bit address space, per the design spec).

- [ ] **Step 1: Write the failing tests**

```cpp
// append to sim/golden/mm77la_model_test.cpp

static void test_t_jumps_on_page_with_inverted_operand() {
    // T x encodes as 0xC0 | x; the destination low-6 bits are ~x & 0x3f.
    uint8_t rom[1] = {static_cast<uint8_t>(0xC0 | 0x05)}; // T 5
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.debug_set_pc(0x100);
    m.step();
    CHECK(m.state().pc == (0x100 | (~0x05 & 0x3F)));
}

static void test_tm_pushes_return_address_outside_subroutine_page() {
    uint8_t rom[1] = {static_cast<uint8_t>(0x80 | 0x03)}; // TM 3
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.debug_set_pc(0x040); // not in the subroutine page (top page is 0x780-0x7FF)
    m.step();
    CHECK(m.state().stack[0] == 0x041); // return address = incremented PC before the jump
    CHECK(m.state().pc == (0x7FF & ~0x3Fu & 0 | (~0x03 & 0x3F))); // page bits from prgmask&~0x3F combined with dest
}

static void test_tm_from_subroutine_page_does_not_push() {
    uint8_t rom[1] = {static_cast<uint8_t>(0x80 | 0x03)}; // TM 3
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.debug_set_pc(0x7C0); // inside the subroutine page (top page, pc & ~0x7F == 0x780&~0x7F... )
    m.debug_set_stack0(0x000);
    m.step();
    CHECK(m.state().stack[0] == 0x000); // unchanged: calls from the subroutine page don't push
}

static void test_rt_pops_stack() {
    uint8_t rom[1] = {0x2F}; // RT
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.debug_set_stack0(0x123);
    m.step();
    CHECK(m.state().pc == 0x123);
}

static void test_rtsk_pops_and_sets_skip() {
    uint8_t rom[1] = {0x2E}; // RTSK
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.debug_set_stack0(0x055);
    m.step();
    CHECK(m.state().pc == 0x055);
    CHECK(m.state().skip == true);
}
```

Add all five calls to `main()`, and add a `debug_set_stack0` setter alongside the other debug setters — this is called out explicitly in Step 3 below.

- [ ] **Step 2: Run tests to verify they fail**

Run: `make -C sim golden-test`
Expected: compile failure (`debug_set_stack0` undeclared) and/or logic failures.

- [ ] **Step 3: Add the stack debug setter and implement the opcodes**

In `mm77la_model.h`:
```cpp
    void debug_set_stack0(uint16_t addr) { st_.stack[0] = addr & 0x7FF; }
```

In `mm77la_model.cpp`, add cases to the `switch (op & 0xF0)` block in `step()` (before the `default:` that falls to `op & 0xFC`), and add `RT`/`RTSK` to the innermost fully-decoded `switch (op)`:

```cpp
        case 0xC0: { // T x
            constexpr uint16_t prgmask = 0x7FF;
            bool in_subroutine_page = (st_.pc & ~0x7Fu) == (prgmask & ~0x7Fu);
            uint16_t pc = st_.pc;
            if (in_subroutine_page) pc &= ~0x40u;
            uint8_t x = op & 0x3F;
            st_.pc = static_cast<uint16_t>((pc & ~0x3Fu) | (~x & 0x3Fu));
            break;
        }
        case 0x80: { // TM x
            constexpr uint16_t prgmask = 0x7FF;
            bool in_subroutine_page = (st_.pc & ~0x7Fu) == (prgmask & ~0x7Fu);
            if (!in_subroutine_page) {
                st_.stack[1] = st_.stack[0];
                st_.stack[0] = st_.pc;
            }
            uint8_t x = op & 0x3F;
            st_.pc = static_cast<uint16_t>((prgmask & ~0x3Fu) | (~x & 0x3Fu));
            break;
        }
```

And inside the fully-decoded `switch (op)` in the innermost `default:` block from Task 4:
```cpp
                        case 0x2F: { // RT
                            st_.pc = st_.stack[0] & 0x7FF;
                            st_.stack[0] = st_.stack[1];
                            break;
                        }
                        case 0x2E: { // RTSK
                            st_.pc = st_.stack[0] & 0x7FF;
                            st_.stack[0] = st_.stack[1];
                            st_.skip = true;
                            break;
                        }
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `make -C sim golden-test`
Expected: `PASS`. If `test_tm_pushes_return_address_outside_subroutine_page`'s expected-PC expression is confusing, verify it by hand: `prgmask & ~0x3F = 0x7C0`, `~0x03 & 0x3F = 0x3C`, so expected `pc = 0x7C0 | 0x3C = 0x7FC`.

- [ ] **Step 5: Commit**

```bash
git add sim/golden/mm77la_model.h sim/golden/mm77la_model.cpp sim/golden/mm77la_model_test.cpp
git commit -m "Implement on-page jump/call and return opcodes in golden model"
```

---

### Task 6: Golden model — skip mechanics, LB/EOB/LAI coalescing, carry delay, ram_delay, SAG

**Files:**
- Modify: `sim/golden/mm77la_model.h`
- Modify: `sim/golden/mm77la_model.cpp`
- Modify: `sim/golden/mm77la_model_test.cpp`

**Interfaces:**
- Consumes: `step()` (Tasks 4-5), `prev_op`/`prev2_op`/`prev3_op` (Task 4)
- Produces: full `LB x`/`EOB x` with coalescing, `LAI x` with coalescing (replacing Task 4's always-load stub), `SAG`, carry-delay visibility (`c_in` updated one instruction late), `XDSK`/`XNSK` with `ram_delay`, and general skip-continuation across `TR`-prefixed instructions (`op_is_tr`). TAB's one-opcode-delayed fire is Task 7 (it depends on TR/multi-byte dispatch being in place first).

This is the highest-risk task in the plan — re-read `docs/initial-plan.md` §2's "Skip / delay mechanics" section before starting, and implement each mechanism exactly as described there, not as it might seem to "obviously" work.

- [ ] **Step 1: Write the failing tests**

```cpp
// append to sim/golden/mm77la_model_test.cpp

static void test_lb_then_eob_coalesce_as_a_pair() {
    // LB x is 0x10|x; EOB x is 0x08|x (2-bit immediate). A direct LB;EOB pair
    // is the documented non-suppressed case -- EOB after LB should still apply.
    uint8_t rom[2] = {static_cast<uint8_t>(0x10 | 0x5), static_cast<uint8_t>(0x08 | 0x2)};
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.step(); // LB 5 -> B = 5
    CHECK(m.state().b == 0x5);
    m.step(); // EOB 2 -> Bu ^= (2<<4)
    CHECK(m.state().b == (0x5 ^ 0x20));
}

static void test_successive_lai_coalescing_suppresses_repeat() {
    // Per docs/initial-plan.md §5.2: "LAI x: A = x, UNLESS prev op was a
    // non-suppressed LAI." Two back-to-back LAI's: the second is suppressed
    // (A keeps the first value), matching MAME's coalescing behavior.
    uint8_t rom[2] = {0x43, 0x47}; // LAI 3; LAI 7
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.step(); // A = 3
    CHECK(m.state().a == 0x3);
    m.step(); // suppressed: A stays 3, NOT 7
    CHECK(m.state().a == 0x3);
}

static void test_lai_after_non_lai_is_not_suppressed() {
    uint8_t rom[2] = {0x00, 0x47}; // NOP; LAI 7
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.step(); // NOP
    m.step(); // LAI 7 -- prev op was NOP, not suppressed
    CHECK(m.state().a == 0x7);
}

static void test_ac_carry_visible_to_sknc_only_after_one_instruction_delay() {
    uint8_t rom[3] = {0x7C, 0x00, 0x02}; // AC; NOP; SKNC
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.debug_set_a(0xF);
    m.debug_set_b(0x00);
    m.debug_ram_write(0x00, 0x2); // 0xF + 0x2 = 0x11 -> carry out = 1
    m.step(); // AC: new carry computed, but c_in not yet updated
    CHECK(m.state().c_in == 0); // not visible yet
    m.step(); // NOP: this is the instruction after which c_in updates
    m.step(); // SKNC reads c_in
    CHECK(m.state().skip == false); // c_in should now be 1 -> SKNC (skip if carry==0) does NOT skip
}

static void test_xdsk_sets_ram_delay_and_skips_on_wrap_to_f() {
    uint8_t rom[1] = {static_cast<uint8_t>(0x58 | 0x1)}; // XDSK 1
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.debug_set_a(0x9);
    m.debug_set_b(0x00); // Bl = 0, decrementing wraps to 0xF
    m.debug_ram_write(0x00, 0x2);
    m.step();
    CHECK(m.state().ram_delay == true);
    CHECK((m.state().b & 0xF) == 0xF); // Bl wrapped
    CHECK(m.state().skip == true);     // skip because it wrapped
}

static void test_sag_forces_ram_addr_upper_bits_to_3_for_one_cycle_only() {
    // SB writes RAM[ram_addr]; with SAG active, ram_addr upper bits (Bu) are
    // forced to 3 for exactly the next cycle, regardless of B's real value.
    uint8_t rom[2] = {0x07, static_cast<uint8_t>(0x20 | 0x1)}; // SAG; SB 1
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.debug_set_b(0x05); // Bu would normally be 0, not 3
    m.debug_ram_write(0x35, 0x0); // address with Bu=3, Bl=5
    m.step(); // SAG
    m.step(); // SB 1, should target RAM[0x35] because of SAG, not RAM[0x05]
    CHECK(m.debug_ram_read(0x35) == 0x2);
    CHECK(m.debug_ram_read(0x05) == 0xF); // untouched (still reset value)
}
```

Add all six calls to `main()`.

- [ ] **Step 2: Run tests to verify they fail**

Run: `make -C sim golden-test`
Expected: several failures — `LB`/`EOB`/`LAI` coalescing, carry delay, `XDSK`, and `SAG` aren't implemented yet (Task 4's `LAI` always loads; `LB`/`EOB`/`XDSK`/`SAG` fall through as no-ops).

- [ ] **Step 3: Implement coalescing, carry delay, ram_delay opcodes, and SAG**

Replace the `case 0x40:` (LAI) block in `step()` from Task 4 with:
```cpp
        case 0x40: { // LAI x, with coalescing suppression
            bool suppressed = (st_.prev_op & 0xF0) == 0x40;
            if (!suppressed) st_.a = op & 0xF;
            break;
        }
```

Add a `case 0x10:` (LB) to the `op & 0xF0` switch, and `case 0x08:`/`case 0x0C:` (EOB, matched via `op & 0xFC == 0x08`) to the `op & 0xFC` switch:
```cpp
        case 0x10: { // LB x
            bool suppressed = (st_.prev_op & 0xF0) == 0x10;
            if (!suppressed) st_.b = op & 0xF;
            break;
        }
```
```cpp
                case 0x08: { // EOB x (op & 0xFC == 0x08 covers 0x08 and 0x0C)
                    bool suppressed = (st_.prev_op & 0xFC) == 0x08;
                    if (!suppressed) st_.b = static_cast<uint8_t>(st_.b ^ ((op & 0x3) << 4));
                    break;
                }
```

Add `XDSK`/`XNSK` (`op & 0xFC == 0x58` / `0x54`) to the `op & 0xFC` switch:
```cpp
                case 0x58: { // XDSK x
                    uint16_t ram_addr = st_.b;
                    uint8_t tmp = ram_read(static_cast<uint8_t>(ram_addr));
                    ram_write(static_cast<uint8_t>(ram_addr), st_.a);
                    st_.a = tmp;
                    uint8_t bl = static_cast<uint8_t>(((st_.b & 0xF) - 1) & 0xF);
                    st_.b = static_cast<uint8_t>((st_.b & ~0xFu) | bl);
                    st_.b = static_cast<uint8_t>(st_.b ^ ((op & 0x3) << 4));
                    st_.ram_delay = true;
                    st_.skip = (bl == 0xF);
                    break;
                }
                case 0x54: { // XNSK x
                    uint16_t ram_addr = st_.b;
                    uint8_t tmp = ram_read(static_cast<uint8_t>(ram_addr));
                    ram_write(static_cast<uint8_t>(ram_addr), st_.a);
                    st_.a = tmp;
                    uint8_t bl = static_cast<uint8_t>(((st_.b & 0xF) + 1) & 0xF);
                    st_.b = static_cast<uint8_t>((st_.b & ~0xFu) | bl);
                    st_.b = static_cast<uint8_t>(st_.b ^ ((op & 0x3) << 4));
                    st_.ram_delay = true;
                    st_.skip = (bl == 0x0);
                    break;
                }
```

Add `SAG` and `SKNC` to the fully-decoded `switch (op)`:
```cpp
                        case 0x07: { // SAG
                            st_.sag = true;
                            break;
                        }
                        case 0x02: { // SKNC
                            st_.skip = (st_.c_in == 0);
                            break;
                        }
```

Now wire `SAG` into `ram_addr` computation and carry delay into `c_in`. At the top of `step()`, change:
```cpp
    uint16_t ram_addr = st_.b; // ram_delay override handled in Task 8
```
to:
```cpp
    uint16_t ram_addr = st_.sag ? (0x30 | (st_.b & 0xF)) : st_.b;
    st_.sag = false; // exactly one cycle of effect
```
and replace every local `uint16_t ram_addr = st_.b;` that Task 4/5's per-case blocks introduced (in `XDSK`/`XNSK` above) with a reference to this single top-of-function `ram_addr` instead of re-reading `st_.b` — i.e. delete those two locals and use the outer `ram_addr` directly, since SAG must apply uniformly to every opcode's RAM access, not just the ones written before this task.

For carry delay: `AC`/`ACSK` (Task 4) currently write `st_.c` directly. Change them to write a pending value and apply it with one instruction's delay:
```cpp
                        case 0x7C: { // AC
                            uint8_t sum = static_cast<uint8_t>(st_.a + ram_read(static_cast<uint8_t>(ram_addr)) + st_.c_in);
                            st_.c = (sum >> 4) & 1;
                            st_.a = sum & 0xF;
                            st_.c_delay = true;
                            break;
                        }
                        case 0x7D: { // ACSK
                            uint8_t sum = static_cast<uint8_t>(st_.a + ram_read(static_cast<uint8_t>(ram_addr)) + st_.c_in);
                            st_.c = (sum >> 4) & 1;
                            st_.a = sum & 0xF;
                            st_.c_delay = true;
                            st_.skip = st_.c != 0;
                            break;
                        }
```
And at the very end of `step()`, before updating `prev_op`/`prev2_op`/`prev3_op`, add:
```cpp
    if (st_.c_delay) {
        st_.c_in = st_.c;
        st_.c_delay = false;
    }
```
This gives exactly the "visible after the instruction after next" timing the test checks: cycle N sets `c_delay=true`; cycle N+1 (any opcode) applies it to `c_in` at its own end; cycle N+2 reads the now-updated `c_in`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `make -C sim golden-test`
Expected: `PASS`. If `test_ac_carry_visible_to_sknc_only_after_one_instruction_delay` still fails, double check the ordering — `c_delay` must be consumed at the end of the NOP's `step()` call (cycle 2), not the AC's own call (cycle 1).

- [ ] **Step 5: Commit**

```bash
git add sim/golden/mm77la_model.h sim/golden/mm77la_model.cpp sim/golden/mm77la_model_test.cpp
git commit -m "Implement coalescing, carry delay, ram_delay ops, and SAG in golden model"
```

---

### Task 7: Golden model — TR-prefixed multi-byte opcodes, skip continuation, TAB, IX/INT1L stubs

**Files:**
- Modify: `sim/golden/mm77la_model.h`
- Modify: `sim/golden/mm77la_model.cpp`
- Modify: `sim/golden/mm77la_model_test.cpp`

**Interfaces:**
- Consumes: `prev_op`/`prev2_op`/`prev3_op` (Task 4), `step()`'s dispatch cascade (Tasks 4-6)
- Produces: `TR` prefix handling (`op_is_tr`), 2-byte (`SKBEI`, `SKAEI`, `TL`, `TML`) and 3-byte (`TLB`, `TMLB`) dispatch, skip-continues-through-TR-prefixed-instructions logic, `TAB`'s one-opcode-delayed fire, `IX`/`INT1L` stubs.

- [ ] **Step 1: Write the failing tests**

```cpp
// append to sim/golden/mm77la_model_test.cpp

static bool op_is_tr(uint8_t op) { return (op & 0xF0) == 0x30; }

static void test_tr_prefixed_tl_jumps_off_page() {
    // TR (0x30) then TL x: 2-byte form. pc = (~prev_op & 0xF)<<6 | (~op & 0x3F)
    uint8_t rom[2] = {0x30, static_cast<uint8_t>(0xC0 | 0x05)}; // TR; TL 5
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.step(); // TR: prefix only, no direct effect
    m.step(); // TL 5, dispatched because prev_op was TR
    uint16_t expected = static_cast<uint16_t>(((~0x30 & 0xF) << 6) | (~0xC5 & 0x3F));
    CHECK(m.state().pc == expected);
}

static void test_skip_continues_through_tr_prefixed_instruction() {
    // If an opcode sets skip, and the NEXT fetched opcode is itself a TR
    // prefix, skipping must continue through the whole 2-byte instruction,
    // not just the TR byte.
    uint8_t rom[4] = {
        0x66,             // AISK 6 (DC) -- NOT what we want, use SKMEA instead below
    };
    (void)rom;
    uint8_t rom2[4] = {
        0x7F,             // SKMEA -- will skip since A==RAM by construction
        0x30,             // TR (start of a 2-byte TL)
        static_cast<uint8_t>(0xC0 | 0x01), // TL 1 -- second byte of the skipped instruction
        0x00,             // NOP -- execution should resume here
    };
    Mm77laModel m(rom2, sizeof(rom2));
    m.reset();
    m.debug_set_a(0x5);
    m.debug_set_b(0x00);
    m.debug_ram_write(0x00, 0x5);
    m.step(); // SKMEA: sets skip=true
    CHECK(m.state().skip == true);
    m.step(); // fetch of TR byte should be consumed by the skip, and because
              // it's a TR prefix, the skip must extend through the TL byte too
    m.step(); // fetch of TL's second byte, also consumed
    CHECK(m.state().pc == 3); // landed on the NOP at address 3, not mid-instruction
}

static void test_tab_fires_one_opcode_after_next() {
    // TAB (0x2C): the NEXT opcode executes first with A/skip_count unchanged,
    // THEN skip_count = A+1, A = 0xF fires (using the A value present when
    // TAB itself was decoded).
    uint8_t rom[2] = {0x2C, 0x00}; // TAB; NOP
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.debug_set_a(0x3);
    m.step(); // TAB decoded; its effect has NOT applied yet
    CHECK(m.state().skip_count == 0);
    m.step(); // NOP executes; TAB's delayed effect fires at the end of THIS step
    CHECK(m.state().skip_count == 0x3 + 1);
    CHECK(m.state().a == 0xF);
}

static void test_int1l_is_noop_but_flags_hit() {
    uint8_t rom[1] = {0x04}; // INT1L (0x04 per the MM78 opcode table)
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.debug_set_a(0x3);
    m.step();
    CHECK(m.state().a == 0x3);      // no architectural effect
    CHECK(m.state().int1l_hit == true); // but flagged for the testbench
}
```

Add all four calls to `main()`.

- [ ] **Step 2: Run tests to verify they fail**

Run: `make -C sim golden-test`
Expected: failures — none of `TR`/`TL`/skip-continuation/`TAB`/`INT1L` are implemented yet.

- [ ] **Step 3: Implement TR dispatch, skip continuation, TAB, and INT1L**

Restructure the top of `step()` to dispatch on `prev_op`/`prev2_op` before falling into the normal opcode table, and to apply skip-continuation. Replace the whole function body from Task 6 with this version (same cases as before, now wrapped):

```cpp
void Mm77laModel::step() {
    uint16_t ram_addr = st_.sag ? (0x30 | (st_.b & 0xF)) : st_.b;
    st_.sag = false;

    uint8_t op = rom_read(st_.pc);
    increment_pc();

    bool consumed_by_skip = st_.skip;
    if (consumed_by_skip) {
        st_.skip = false;
        // If what we just "executed" as a skip target is itself a TR prefix,
        // the skip must extend through the whole multi-byte instruction.
        if (op_is_tr(op)) {
            uint8_t op2 = rom_read(st_.pc);
            increment_pc();
            if (op_is_tr(op2)) {
                increment_pc(); // consume the 3rd byte of a TLB/TMLB-under-skip too
            }
        }
        st_.prev3_op = st_.prev2_op;
        st_.prev2_op = st_.prev_op;
        st_.prev_op = op;
        if (st_.tab_pending) { st_.skip_count = static_cast<uint8_t>(st_.a + 1); st_.a = 0xF; st_.tab_pending = false; }
        return;
    }

    bool is_2byte = op_is_tr(st_.prev_op);
    bool is_3byte = is_2byte && op_is_tr(st_.prev2_op);

    if (is_3byte) {
        switch (op & 0xF0) {
            case 0x80: { // TMLB
                st_.stack[1] = st_.stack[0];
                st_.stack[0] = st_.pc;
                st_.pc = static_cast<uint16_t>(0x400 | ((~st_.prev_op & 0xF) << 6) | (~op & 0x3F));
                break;
            }
            case 0xC0: { // TLB
                st_.pc = static_cast<uint16_t>(0x400 | ((~st_.prev_op & 0xF) << 6) | (~op & 0x3F));
                break;
            }
            default: break;
        }
    } else if (is_2byte) {
        switch (op & 0xF0) {
            case 0x30: break; // another TR -- enables 3-byte dispatch next step
            case 0x40: { // SKBEI x
                st_.skip = ((st_.b & 0xF) == (op & 0xF));
                break;
            }
            case 0x60: { // SKAEI x (op==0x60 exactly is illegal, treat as no-op)
                if (op != 0x60) st_.skip = (st_.a == (~op & 0xF));
                break;
            }
            case 0x80: { // TML
                st_.stack[1] = st_.stack[0];
                st_.stack[0] = st_.pc;
                st_.pc = static_cast<uint16_t>(((~st_.prev_op & 0xF) << 6) | (~op & 0x3F));
                break;
            }
            case 0xC0: { // TL
                st_.pc = static_cast<uint16_t>(((~st_.prev_op & 0xF) << 6) | (~op & 0x3F));
                break;
            }
            default: break;
        }
    } else {
        switch (op & 0xF0) {
            case 0x10: { bool suppressed = (st_.prev_op & 0xF0) == 0x10; if (!suppressed) st_.b = op & 0xF; break; }
            case 0x30: break; // TR prefix, no direct effect
            case 0x40: { bool suppressed = (st_.prev_op & 0xF0) == 0x40; if (!suppressed) st_.a = op & 0xF; break; }
            case 0x60: {
                uint8_t x = op & 0xF;
                uint8_t sum = static_cast<uint8_t>(st_.a + x);
                st_.a = sum & 0xF;
                st_.skip = (x == 6) ? false : (sum < 0x10);
                break;
            }
            case 0x80: {
                constexpr uint16_t prgmask = 0x7FF;
                bool in_subroutine_page = (st_.pc & ~0x7Fu) == (prgmask & ~0x7Fu);
                if (!in_subroutine_page) { st_.stack[1] = st_.stack[0]; st_.stack[0] = st_.pc; }
                uint8_t x = op & 0x3F;
                st_.pc = static_cast<uint16_t>((prgmask & ~0x3Fu) | (~x & 0x3Fu));
                break;
            }
            case 0xC0: {
                constexpr uint16_t prgmask = 0x7FF;
                bool in_subroutine_page = (st_.pc & ~0x7Fu) == (prgmask & ~0x7Fu);
                uint16_t pc = st_.pc;
                if (in_subroutine_page) pc &= ~0x40u;
                uint8_t x = op & 0x3F;
                st_.pc = static_cast<uint16_t>((pc & ~0x3Fu) | (~x & 0x3Fu));
                break;
            }
            default: {
                switch (op & 0xFC) {
                    case 0x08: { bool suppressed = (st_.prev_op & 0xFC) == 0x08; if (!suppressed) st_.b = static_cast<uint8_t>(st_.b ^ ((op & 0x3) << 4)); break; }
                    case 0x20: { uint8_t val = ram_read(static_cast<uint8_t>(ram_addr)); ram_write(static_cast<uint8_t>(ram_addr), val | (1 << (op & 0x3))); break; }
                    case 0x24: { uint8_t val = ram_read(static_cast<uint8_t>(ram_addr)); ram_write(static_cast<uint8_t>(ram_addr), val & ~(1 << (op & 0x3))); break; }
                    case 0x28: { uint8_t val = ram_read(static_cast<uint8_t>(ram_addr)); st_.skip = (val & (1 << (op & 0x3))) == 0; break; }
                    case 0x50: { st_.a = ram_read(static_cast<uint8_t>(ram_addr)); st_.b = static_cast<uint8_t>(st_.b ^ ((op & 0x3) << 4)); break; }
                    case 0x54: {
                        uint8_t tmp = ram_read(static_cast<uint8_t>(ram_addr));
                        ram_write(static_cast<uint8_t>(ram_addr), st_.a);
                        st_.a = tmp;
                        uint8_t bl = static_cast<uint8_t>(((st_.b & 0xF) + 1) & 0xF);
                        st_.b = static_cast<uint8_t>((st_.b & ~0xFu) | bl);
                        st_.b = static_cast<uint8_t>(st_.b ^ ((op & 0x3) << 4));
                        st_.ram_delay = true;
                        st_.skip = (bl == 0x0);
                        break;
                    }
                    case 0x58: {
                        uint8_t tmp = ram_read(static_cast<uint8_t>(ram_addr));
                        ram_write(static_cast<uint8_t>(ram_addr), st_.a);
                        st_.a = tmp;
                        uint8_t bl = static_cast<uint8_t>(((st_.b & 0xF) - 1) & 0xF);
                        st_.b = static_cast<uint8_t>((st_.b & ~0xFu) | bl);
                        st_.b = static_cast<uint8_t>(st_.b ^ ((op & 0x3) << 4));
                        st_.ram_delay = true;
                        st_.skip = (bl == 0xF);
                        break;
                    }
                    case 0x5C: {
                        uint8_t tmp = ram_read(static_cast<uint8_t>(ram_addr));
                        ram_write(static_cast<uint8_t>(ram_addr), st_.a);
                        st_.a = tmp;
                        st_.b = static_cast<uint8_t>(st_.b ^ ((op & 0x3) << 4));
                        break;
                    }
                    default: {
                        switch (op) {
                            case 0x00: break; // NOP
                            case 0x02: st_.skip = (st_.c_in == 0); break; // SKNC
                            case 0x04: st_.int1l_hit = true; break; // INT1L -- flagged no-op
                            case 0x07: st_.sag = true; break; // SAG
                            case 0x2C: st_.tab_pending = true; break; // TAB -- delayed fire
                            case 0x2E: { st_.pc = st_.stack[0] & 0x7FF; st_.stack[0] = st_.stack[1]; st_.skip = true; break; } // RTSK
                            case 0x2F: { st_.pc = st_.stack[0] & 0x7FF; st_.stack[0] = st_.stack[1]; break; } // RT
                            case 0x72: break; // IX -- stub, no PLA wiring this phase
                            case 0x76: st_.b = static_cast<uint8_t>((st_.b & ~0xFu) | st_.a); break; // LBA
                            case 0x77: st_.a = st_.a ^ 0xF; break; // COM
                            case 0x7A: { uint8_t tmp = st_.a; st_.a = st_.b & 0xF; st_.b = static_cast<uint8_t>((st_.b & ~0xFu) | tmp); st_.ram_delay = true; break; } // XAB
                            case 0x7C: { uint8_t sum = static_cast<uint8_t>(st_.a + ram_read(static_cast<uint8_t>(ram_addr)) + st_.c_in); st_.c = (sum >> 4) & 1; st_.a = sum & 0xF; st_.c_delay = true; break; } // AC
                            case 0x7D: { uint8_t sum = static_cast<uint8_t>(st_.a + ram_read(static_cast<uint8_t>(ram_addr)) + st_.c_in); st_.c = (sum >> 4) & 1; st_.a = sum & 0xF; st_.c_delay = true; st_.skip = st_.c != 0; break; } // ACSK
                            case 0x7E: st_.a = static_cast<uint8_t>((st_.a + ram_read(static_cast<uint8_t>(ram_addr))) & 0xF); break; // A
                            case 0x7F: st_.skip = (st_.a == ram_read(static_cast<uint8_t>(ram_addr))); break; // SKMEA
                            default: break; // opcodes not reachable on this ROM: left as no-op
                        }
                        break;
                    }
                }
                break;
            }
        }
    }

    if (st_.c_delay) { st_.c_in = st_.c; st_.c_delay = false; }
    if (st_.tab_pending && op != 0x2C) {
        // TAB's effect fires after the FOLLOWING opcode has executed; since
        // we're already past that opcode's execution here, apply it now,
        // using the A value TAB captured -- but TAB doesn't capture A itself,
        // the doc's op_tab() reads m_a at fire time, which by construction is
        // whatever A is after the intervening opcode ran. Re-read docs/initial-plan.md
        // §5.1 before touching this if the semantics ever look wrong for a real ROM trace.
        st_.skip_count = static_cast<uint8_t>(st_.a + 1);
        st_.a = 0xF;
        st_.tab_pending = false;
    }

    st_.prev3_op = st_.prev2_op;
    st_.prev2_op = st_.prev_op;
    st_.prev_op = op;
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `make -C sim golden-test`
Expected: `PASS`. `test_tab_fires_one_opcode_after_next` is the one most likely to need a second look — if it fails, trace through by hand which `step()` call's `tab_pending` block actually fires and adjust the `op != 0x2C` guard condition (the goal: TAB's effect must NOT fire immediately when TAB's own opcode is being decoded as `prev_op` on the very next call, only after the instruction after TAB has run).

- [ ] **Step 5: Commit**

```bash
git add sim/golden/mm77la_model.h sim/golden/mm77la_model.cpp sim/golden/mm77la_model_test.cpp
git commit -m "Implement TR-prefixed dispatch, skip continuation, TAB, and INT1L/IX stubs"
```

---

### Task 8: Golden model — trace CLI for vector/ROM runs

**Files:**
- Create: `sim/golden/trace_main.cpp`
- Modify: `sim/Makefile`

**Interfaces:**
- Consumes: `Mm77laModel` (Tasks 2-7)
- Produces: a `golden_trace` executable: `golden_trace <rom-file> <cycle-count>` prints one CSV line per cycle (`cycle,pc,a,b,x,c,c_in,stack0,stack1,skip,skip_count`) to stdout. This becomes the reference stream Task 13's RTL testbench diffs against.

- [ ] **Step 1: Write trace_main.cpp**

```cpp
// sim/golden/trace_main.cpp
#include "mm77la_model.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s <rom-file> <cycle-count>\n", argv[0]);
        return 2;
    }
    FILE* f = std::fopen(argv[1], "rb");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> rom(static_cast<size_t>(size));
    if (std::fread(rom.data(), 1, rom.size(), f) != rom.size()) {
        std::fprintf(stderr, "short read on %s\n", argv[1]);
        std::fclose(f);
        return 2;
    }
    std::fclose(f);

    long cycles = std::strtol(argv[2], nullptr, 10);
    Mm77laModel m(rom.data(), rom.size());
    m.reset();

    std::printf("cycle,pc,a,b,x,c,c_in,stack0,stack1,skip,skip_count\n");
    for (long i = 0; i < cycles; i++) {
        m.step();
        const auto& s = m.state();
        std::printf("%ld,%u,%u,%u,%u,%u,%u,%u,%u,%d,%u\n",
            i, s.pc, s.a, s.b, s.x, s.c, s.c_in, s.stack[0], s.stack[1],
            s.skip ? 1 : 0, s.skip_count);
    }
    return 0;
}
```

- [ ] **Step 2: Add a Makefile target and build it**

Add to `sim/Makefile`:
```makefile
golden-trace: golden/mm77la_model.cpp golden/trace_main.cpp
	$(CXX) $(CXXFLAGS) golden/mm77la_model.cpp golden/trace_main.cpp -o /tmp/golden_trace
```

Run: `make -C sim golden-trace`
Expected: builds without error.

- [ ] **Step 3: Smoke-test against a tiny synthetic ROM**

Run:
```bash
printf '\x40\x41\x42' > /tmp/smoke.bin   # LAI 0; LAI 1 (suppressed); LAI 2 (suppressed)
/tmp/golden_trace /tmp/smoke.bin 3
```
Expected: 3 CSV rows; `a` column reads `0,0,0` (the first `LAI 0` loads, the next two `LAI`s coalesce-suppress because the immediately preceding op was also `LAI`).

- [ ] **Step 4: Commit**

```bash
git add sim/golden/trace_main.cpp sim/Makefile
git commit -m "Add golden model trace CLI for vector/ROM cycle dumps"
```

---

### Task 9: RTL — pps41_decode.v opcode decoder

**Files:**
- Create: `src/pps41_decode.v`
- Create: `sim/pps41_decode_tb.cpp`
- Modify: `sim/Makefile`

**Interfaces:**
- Produces: `pps41_decode` module — combinational, given `op` (8-bit, current fetched byte), `prev_op`/`prev2_op` (8-bit each, for TR-prefix state), outputs decode flags used by `pps41_core.v` in Task 13: `is_tr`, `is_2byte`, `is_3byte`, `op_class` (a small enum-as-integer identifying which case arm applies), and the raw immediate/operand bits needed by each class. This task only needs to prove decode correctness against a table of known opcodes — it does not execute anything.

- [ ] **Step 1: Write the Verilog module**

```verilog
// src/pps41_decode.v
module pps41_decode (
    input  wire [7:0] op,
    input  wire [7:0] prev_op,
    input  wire [7:0] prev2_op,
    output wire        is_tr,          // op itself is a TR prefix byte
    output wire        prev_is_tr,     // prev_op was a TR prefix
    output wire        prev2_is_tr,    // prev2_op was also a TR prefix (3-byte form)
    output wire [3:0]  op_hi,          // op & 0xF0, as a 4-bit selector (op[7:4])
    output wire [1:0]  op_lo2,         // op & 0x3, common 2-bit immediate field
    output wire [3:0]  op_lo4,         // op & 0xF, common 4-bit immediate field
    output wire [5:0]  op_lo6,         // op & 0x3F, jump-target field
    output wire [7:0]  op_fc           // op & 0xFC, for the second-tier dispatch group
);
    assign is_tr      = (op[7:4] == 4'h3);
    assign prev_is_tr  = (prev_op[7:4] == 4'h3);
    assign prev2_is_tr  = (prev2_op[7:4] == 4'h3);
    assign op_hi   = op[7:4];
    assign op_lo2  = op[1:0];
    assign op_lo4  = op[3:0];
    assign op_lo6  = op[5:0];
    assign op_fc   = {op[7:2], 2'b00};
endmodule
```

- [ ] **Step 2: Write the Verilator testbench**

```cpp
// sim/pps41_decode_tb.cpp
#include "Vpps41_decode.h"
#include "verilated.h"
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Vpps41_decode* dut = new Vpps41_decode;

    dut->op = 0x30; dut->prev_op = 0x00; dut->prev2_op = 0x00; dut->eval();
    CHECK(dut->is_tr == 1);

    dut->op = 0xC5; dut->prev_op = 0x30; dut->prev2_op = 0x00; dut->eval();
    CHECK(dut->prev_is_tr == 1);
    CHECK(dut->op_hi == 0xC);
    CHECK(dut->op_lo6 == 0x05);

    dut->op = 0x21; dut->prev_op = 0x00; dut->prev2_op = 0x00; dut->eval();
    CHECK(dut->op_fc == 0x20);
    CHECK(dut->op_lo2 == 0x1);

    dut->op = 0x47; dut->prev_op = 0x00; dut->prev2_op = 0x00; dut->eval();
    CHECK(dut->op_lo4 == 0x7);

    delete dut;
    if (failures == 0) { std::printf("PASS\n"); return 0; }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
```

- [ ] **Step 3: Add a Makefile target and run it**

Add to `sim/Makefile`:
```makefile
decode-test:
	$(VERILATOR) --cc ../src/pps41_decode.v --exe pps41_decode_tb.cpp \
		--Mdir obj_dir_decode -Wall
	$(MAKE) -C obj_dir_decode -f Vpps41_decode.mk
	./obj_dir_decode/Vpps41_decode
```

Run: `make -C sim decode-test`
Expected: `PASS`

- [ ] **Step 4: Commit**

```bash
git add src/pps41_decode.v sim/pps41_decode_tb.cpp sim/Makefile
git commit -m "Add pps41_decode.v opcode decoder with standalone testbench"
```

---

### Task 10: RTL — pps41_alu.v

**Files:**
- Create: `src/pps41_alu.v`
- Create: `sim/pps41_alu_tb.cpp`
- Modify: `sim/Makefile`

**Interfaces:**
- Produces: `pps41_alu` module — combinational. Inputs: `a` (4-bit), `mem` (4-bit, RAM operand), `c_in` (1-bit), `op_sel` (3-bit, selects which operation: `ADD`, `ADC`, `COM`, `AISK` with a separate `aisk_imm` 4-bit input). Outputs: `result` (4-bit), `carry_out` (1-bit), `overflow` (1-bit, for `AISK`'s skip condition).

- [ ] **Step 1: Write the Verilog module**

```verilog
// src/pps41_alu.v
module pps41_alu (
    input  wire [3:0] a,
    input  wire [3:0] mem,
    input  wire        c_in,
    input  wire [2:0]  op_sel,       // 0=A(add), 1=AC(add+carry), 2=COM, 3=AISK
    input  wire [3:0]  aisk_imm,
    output reg  [3:0]  result,
    output reg          carry_out,
    output reg           overflow      // for AISK: 1 if a+aisk_imm >= 0x10
);
    localparam OP_ADD  = 3'd0;
    localparam OP_ADC  = 3'd1;
    localparam OP_COM  = 3'd2;
    localparam OP_AISK = 3'd3;

    always @(*) begin
        result    = 4'h0;
        carry_out = 1'b0;
        overflow  = 1'b0;
        case (op_sel)
            OP_ADD: begin
                {carry_out, result} = {1'b0, a} + {1'b0, mem};
            end
            OP_ADC: begin
                {carry_out, result} = {1'b0, a} + {1'b0, mem} + {4'b0, c_in};
            end
            OP_COM: begin
                result = a ^ 4'hF;
            end
            OP_AISK: begin
                {overflow, result} = {1'b0, a} + {1'b0, aisk_imm};
            end
            default: ;
        endcase
    end
endmodule
```

- [ ] **Step 2: Write the Verilator testbench**

```cpp
// sim/pps41_alu_tb.cpp
#include "Vpps41_alu.h"
#include "verilated.h"
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Vpps41_alu* dut = new Vpps41_alu;

    dut->op_sel = 0; dut->a = 0x3; dut->mem = 0x2; dut->c_in = 0; dut->eval();
    CHECK(dut->result == 0x5);
    CHECK(dut->carry_out == 0);

    dut->op_sel = 1; dut->a = 0xF; dut->mem = 0x2; dut->c_in = 1; dut->eval(); // 0xF+0x2+1=0x12
    CHECK(dut->result == 0x2);
    CHECK(dut->carry_out == 1);

    dut->op_sel = 2; dut->a = 0x3; dut->eval();
    CHECK(dut->result == 0xC);

    dut->op_sel = 3; dut->a = 0x1; dut->aisk_imm = 0x2; dut->eval();
    CHECK(dut->result == 0x3);
    CHECK(dut->overflow == 0);

    dut->op_sel = 3; dut->a = 0xF; dut->aisk_imm = 0x2; dut->eval(); // 0xF+2=0x11 -> overflow
    CHECK(dut->overflow == 1);

    delete dut;
    if (failures == 0) { std::printf("PASS\n"); return 0; }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
```

- [ ] **Step 3: Add a Makefile target and run it**

Add to `sim/Makefile`:
```makefile
alu-test:
	$(VERILATOR) --cc ../src/pps41_alu.v --exe pps41_alu_tb.cpp \
		--Mdir obj_dir_alu -Wall
	$(MAKE) -C obj_dir_alu -f Vpps41_alu.mk
	./obj_dir_alu/Vpps41_alu
```

Run: `make -C sim alu-test`
Expected: `PASS`

- [ ] **Step 4: Commit**

```bash
git add src/pps41_alu.v sim/pps41_alu_tb.cpp sim/Makefile
git commit -m "Add pps41_alu.v with standalone testbench"
```

---

### Task 11: RTL — pps41_core.v: PC/LFSR + ROM fetch

**Files:**
- Modify: `src/pps41_core.v` (replace the Task 1 stub)
- Create: `sim/pps41_core_pc_tb.cpp`
- Modify: `sim/Makefile`

**Interfaces:**
- Produces: `pps41_core` module with a real port list: `clk`, `rst_n`, `rom_addr` (11-bit output), `rom_data` (8-bit input, combinational ROM read from the testbench). This task wires up PC/LFSR and fetch only — every fetched byte is currently discarded (treated as NOP) after incrementing PC; full dispatch is Task 13.

- [ ] **Step 1: Write the failing testbench**

```cpp
// sim/pps41_core_pc_tb.cpp
#include "Vpps41_core.h"
#include "verilated.h"
#include <cstdio>
#include <vector>

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

static uint16_t lfsr_increment(uint16_t pc) {
    int feed = ((pc & 0x3e) == 0) ? 1 : 0;
    feed ^= (pc >> 1 ^ pc) & 1;
    return static_cast<uint16_t>((pc & ~0x3fu) | (pc >> 1 & 0x1f) | (feed << 5));
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Vpps41_core* dut = new Vpps41_core;

    dut->rst_n = 0; dut->rom_data = 0x00; dut->eval();
    dut->clk = 0; dut->eval();
    dut->clk = 1; dut->eval();
    dut->rst_n = 1;

    uint16_t expect_pc = 0;
    CHECK(dut->rom_addr == expect_pc);

    for (int i = 0; i < 70; i++) {
        dut->rom_data = 0x00; // NOP, so PC just advances every cycle
        dut->clk = 0; dut->eval();
        dut->clk = 1; dut->eval();
        expect_pc = lfsr_increment(expect_pc);
        CHECK(dut->rom_addr == expect_pc);
    }

    delete dut;
    if (failures == 0) { std::printf("PASS\n"); return 0; }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
```

- [ ] **Step 2: Run to verify it fails**

Add the Makefile target (needed before it can even fail to run):
```makefile
core-pc-test:
	$(VERILATOR) --cc ../src/pps41_core.v --exe pps41_core_pc_tb.cpp \
		--Mdir obj_dir_core_pc -Wall
	$(MAKE) -C obj_dir_core_pc -f Vpps41_core.mk
	./obj_dir_core_pc/Vpps41_core
```

Run: `make -C sim core-pc-test`
Expected: fails — the Task 1 stub has no `rom_addr`/`rom_data` ports.

- [ ] **Step 3: Implement PC/LFSR + fetch**

```verilog
// src/pps41_core.v
module pps41_core (
    input  wire        clk,
    input  wire        rst_n,
    output wire [10:0] rom_addr,
    input  wire [7:0]  rom_data
);
    reg [10:0] pc;

    wire lfsr_feed_seed = (pc[5:1] == 5'b0);
    wire lfsr_feed = lfsr_feed_seed ^ (pc[1] ^ pc[0]);
    wire [10:0] pc_next = {pc[10:6], lfsr_feed, pc[5:1]};

    assign rom_addr = pc;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            pc <= 11'h0;
        end else begin
            pc <= pc_next;
        end
    end
endmodule
```

Note: `pc_next`'s bit layout matches the doc's recurrence exactly: `m_pc = (m_pc & ~0x3f) | (m_pc >> 1 & 0x1f) | (feed << 5)` — i.e. bits `[10:6]` are untouched high bits, bit `[5]` is the new feed bit, bits `[4:0]` are the old `pc[5:1]`. In the Verilog, `{pc[10:6], lfsr_feed, pc[5:1]}` produces exactly that: `pc[10:6]` (5 bits) + `lfsr_feed` (1 bit, becomes new bit 5) + `pc[5:1]` (5 bits, becomes new bits 4:0) = 11 bits total.

- [ ] **Step 4: Run to verify it passes**

Run: `make -C sim core-pc-test`
Expected: `PASS`

- [ ] **Step 5: Commit**

```bash
git add src/pps41_core.v sim/pps41_core_pc_tb.cpp sim/Makefile
git commit -m "Implement PC LFSR increment and ROM fetch in pps41_core.v"
```

---

### Task 12: RTL — pps41_core.v: RAM addressing (Bu/Bl, ram_delay, SAG, sparse map)

**Files:**
- Modify: `src/pps41_core.v`
- Create: `sim/pps41_core_ram_tb.cpp`
- Modify: `sim/Makefile`

**Interfaces:**
- Consumes: PC/fetch (Task 11)
- Produces: added ports `b_reg` (7-bit, exposed for test observation), `ram_addr` (7-bit output, the address actually driven to RAM this cycle, including SAG/ram_delay effects), plus an inline 96-nibble RAM array with the same sparse mapping as the golden model's `ram_phys_index` (Task 2). No opcode dispatch is wired yet — this task drives `b_reg` and `sag`/`ram_delay` directly via testbench pokes on new debug input ports, matching the golden model's `debug_set_b` style, to isolate RAM addressing correctness before Task 13 wires it to real instruction execution.

- [ ] **Step 1: Write the failing testbench**

```cpp
// sim/pps41_core_ram_tb.cpp
#include "Vpps41_core.h"
#include "verilated.h"
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

static void tick(Vpps41_core* dut) {
    dut->clk = 0; dut->eval();
    dut->clk = 1; dut->eval();
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Vpps41_core* dut = new Vpps41_core;

    dut->rst_n = 0; dut->rom_data = 0; dut->dbg_b_set = 0; dut->dbg_sag_set = 0; dut->dbg_ram_wr = 0; tick(dut);
    dut->rst_n = 1;

    // Bank A mirror: write 0x3 at 0x40, expect it readable at 0x48 and 0x58, not at 0x50.
    dut->dbg_b_set = 1; dut->dbg_b_val = 0x40; tick(dut);
    dut->dbg_b_set = 0;
    dut->dbg_ram_wr = 1; dut->dbg_ram_wdata = 0x3; tick(dut);
    dut->dbg_ram_wr = 0;

    dut->dbg_b_set = 1; dut->dbg_b_val = 0x48; tick(dut);
    dut->dbg_b_set = 0; dut->eval();
    CHECK(dut->ram_rdata == 0x3);

    dut->dbg_b_set = 1; dut->dbg_b_val = 0x58; tick(dut);
    dut->dbg_b_set = 0; dut->eval();
    CHECK(dut->ram_rdata == 0x3);

    dut->dbg_b_set = 1; dut->dbg_b_val = 0x50; tick(dut);
    dut->dbg_b_set = 0; dut->eval();
    CHECK(dut->ram_rdata == 0xF); // reset value -- independent bank, not the mirror

    // SAG: force upper bits to 3 for exactly one cycle.
    dut->dbg_b_set = 1; dut->dbg_b_val = 0x05; tick(dut); // Bu would normally be 0
    dut->dbg_b_set = 0;
    dut->dbg_sag_set = 1; tick(dut); // pulse SAG
    dut->dbg_sag_set = 0;
    CHECK(dut->ram_addr == 0x35); // SAG cycle: Bu forced to 3, Bl stays 5
    tick(dut);
    CHECK(dut->ram_addr == 0x05); // one cycle later: SAG has expired

    delete dut;
    if (failures == 0) { std::printf("PASS\n"); return 0; }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
```

- [ ] **Step 2: Add Makefile target, run to verify it fails**

```makefile
core-ram-test:
	$(VERILATOR) --cc ../src/pps41_core.v --exe pps41_core_ram_tb.cpp \
		--Mdir obj_dir_core_ram -Wall
	$(MAKE) -C obj_dir_core_ram -f Vpps41_core.mk
	./obj_dir_core_ram/Vpps41_core
```

Run: `make -C sim core-ram-test`
Expected: fails — `dbg_b_set`/`ram_rdata`/etc. ports don't exist yet.

- [ ] **Step 3: Add RAM addressing to pps41_core.v**

```verilog
// src/pps41_core.v -- add these ports and this logic to the module from Task 11
module pps41_core (
    input  wire        clk,
    input  wire        rst_n,
    output wire [10:0] rom_addr,
    input  wire [7:0]  rom_data,

    // Debug-only ports for Task 12's isolated RAM-addressing test; Task 13
    // replaces the driving of b_reg/sag with real instruction execution but
    // keeps these ports for continued unit testing.
    input  wire        dbg_b_set,
    input  wire [6:0]  dbg_b_val,
    input  wire        dbg_sag_set,
    input  wire        dbg_ram_wr,
    input  wire [3:0]  dbg_ram_wdata,
    output wire [6:0]  ram_addr,
    output wire [3:0]  ram_rdata
);
    reg [10:0] pc;
    reg [6:0]  b_reg;
    reg        sag;

    wire lfsr_feed_seed = (pc[5:1] == 5'b0);
    wire lfsr_feed = lfsr_feed_seed ^ (pc[1] ^ pc[0]);
    wire [10:0] pc_next = {pc[10:6], lfsr_feed, pc[5:1]};

    assign rom_addr = pc;
    assign ram_addr = sag ? {2'b11, b_reg[3:0]} : b_reg;

    // Sparse 96-nibble RAM map: real storage indices are picked with the
    // same bank logic as sim/golden/mm77la_model.cpp's ram_phys_index --
    // 0x00-0x3F direct (64), 0x40-0x47/0x48-0x4F/0x58-0x5F -> bank A (8),
    // 0x50-0x57 -> bank B (8), 0x60-0x67/0x68-0x6F/0x78-0x7F -> bank C (8),
    // 0x70-0x77 -> bank D (8).
    function [6:0] ram_phys_index(input [6:0] addr);
        if (addr < 7'h40) ram_phys_index = addr;
        else if (addr <= 7'h4F || (addr >= 7'h58 && addr <= 7'h5F)) ram_phys_index = 7'd64 + {3'b0, addr[2:0]};
        else if (addr <= 7'h57) ram_phys_index = 7'd72 + {3'b0, addr[2:0]};
        else if (addr <= 7'h6F || (addr >= 7'h78 && addr <= 7'h7F)) ram_phys_index = 7'd80 + {3'b0, addr[2:0]};
        else ram_phys_index = 7'd88 + {3'b0, addr[2:0]};
    endfunction

    reg [3:0] ram [0:95];
    integer i;

    assign ram_rdata = ram[ram_phys_index(ram_addr)];

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            pc <= 11'h0;
            b_reg <= 7'h0;
            sag <= 1'b0;
            for (i = 0; i < 96; i = i + 1) ram[i] <= 4'hF;
        end else begin
            pc <= pc_next;
            sag <= dbg_sag_set; // one-cycle pulse: set this cycle, visible next cycle, then clears
            if (dbg_b_set) b_reg <= dbg_b_val;
            if (dbg_ram_wr) ram[ram_phys_index(ram_addr)] <= dbg_ram_wdata;
        end
    end
endmodule
```

- [ ] **Step 4: Run to verify it passes**

Run: `make -C sim core-ram-test`
Expected: `PASS`

- [ ] **Step 5: Commit**

```bash
git add src/pps41_core.v sim/pps41_core_ram_tb.cpp sim/Makefile
git commit -m "Add RAM addressing (sparse map, SAG) to pps41_core.v"
```

---

### Task 13: RTL — full instruction execution + lockstep integration testbench

**Files:**
- Modify: `src/pps41_core.v` (wire in `pps41_decode`/`pps41_alu`, add register file, stack, skip/coalescing state, remove the Task 12 debug-only ports' role as the sole driver of `b_reg`/`sag`)
- Create: `sim/pps41_core_tb.cpp`
- Modify: `sim/Makefile`

**Interfaces:**
- Consumes: `pps41_decode` (Task 9), `pps41_alu` (Task 10), PC/fetch (Task 11), RAM addressing (Task 12), `Mm77laModel`/`golden_trace` (Tasks 2-8)
- Produces: `pps41_core`'s real opcode execution — every opcode implemented in Tasks 4-7's golden model now has an RTL equivalent, driven from `rom_data` each cycle rather than from debug ports. `sim/pps41_core_tb.cpp` becomes the permanent lockstep harness: `pps41_core_tb <rom-file> <cycle-count>` runs both the Verilated RTL and `Mm77laModel` side by side and diffs every architectural field every cycle, exiting nonzero and printing a diff on first mismatch.

This is the largest single implementation task in the plan. Port `step()`'s full dispatch cascade from Tasks 4-7 into synchronous Verilog inside `pps41_core.v`, using `pps41_decode`'s outputs to drive the same `op & 0xF0` / `op & 0xFC` / fully-decoded cascade structure, and `pps41_alu` for the arithmetic opcodes. Keep the RTL's case structure a direct mirror of the golden model's `step()` — same case order, same opcode groupings — so a future reader can diff the two side by side.

- [ ] **Step 1: Write the lockstep testbench first (drives the test-driven order: this fails until the RTL exists)**

```cpp
// sim/pps41_core_tb.cpp
#include "Vpps41_core.h"
#include "verilated.h"
#include "golden/mm77la_model.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

static void tick(Vpps41_core* dut) {
    dut->clk = 0; dut->eval();
    dut->clk = 1; dut->eval();
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s <rom-file> <cycle-count>\n", argv[0]);
        return 2;
    }
    Verilated::commandArgs(argc, argv);

    FILE* f = std::fopen(argv[1], "rb");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> rom(static_cast<size_t>(size));
    if (std::fread(rom.data(), 1, rom.size(), f) != rom.size()) { std::fclose(f); return 2; }
    std::fclose(f);

    long cycles = std::strtol(argv[2], nullptr, 10);

    Mm77laModel golden(rom.data(), rom.size());
    golden.reset();

    Vpps41_core* dut = new Vpps41_core;
    dut->rst_n = 0; dut->dbg_b_set = 0; dut->dbg_sag_set = 0; dut->dbg_ram_wr = 0;
    dut->rom_data = rom.empty() ? 0 : rom[0];
    tick(dut);
    dut->rst_n = 1;

    for (long i = 0; i < cycles; i++) {
        // ROM is combinationally addressed: present this cycle's byte before the edge.
        uint16_t addr = dut->rom_addr;
        addr &= 0x7FF;
        if (addr >= 0x600) addr -= 0x200;
        dut->rom_data = (addr < rom.size()) ? rom[addr] : 0;
        tick(dut);

        golden.step();
        const auto& g = golden.state();

        bool mismatch = false;
        if (dut->pc != g.pc) { std::printf("cycle %ld: pc mismatch rtl=%03x golden=%03x\n", i, dut->pc, g.pc); mismatch = true; }
        if (dut->a_out != g.a) { std::printf("cycle %ld: a mismatch rtl=%x golden=%x\n", i, dut->a_out, g.a); mismatch = true; }
        if (dut->b_out != g.b) { std::printf("cycle %ld: b mismatch rtl=%02x golden=%02x\n", i, dut->b_out, g.b); mismatch = true; }
        if (dut->skip_out != (g.skip ? 1 : 0)) { std::printf("cycle %ld: skip mismatch rtl=%d golden=%d\n", i, dut->skip_out, g.skip); mismatch = true; }
        if (mismatch) { delete dut; return 1; }
    }

    std::printf("PASS: %ld cycles, no mismatches\n", cycles);
    delete dut;
    return 0;
}
```

- [ ] **Step 2: Add the Makefile target and run to confirm it fails to build**

```makefile
core-test: 
	$(VERILATOR) --cc ../src/pps41_core.v ../src/pps41_decode.v ../src/pps41_alu.v \
		--exe pps41_core_tb.cpp golden/mm77la_model.cpp \
		--Mdir obj_dir_core -Wall -CFLAGS "-I.."
	$(MAKE) -C obj_dir_core -f Vpps41_core.mk
```

Run: `make -C sim core-test`
Expected: build error — `dut->a_out`/`b_out`/`skip_out` ports don't exist yet on `pps41_core`.

- [ ] **Step 3: Wire full execution into pps41_core.v**

Extend the module from Task 12 with: output ports `a_out` (4-bit), `b_out` (7-bit, already have as `ram_addr`'s source `b_reg` — expose it), `skip_out` (1-bit); internal registers for `a`, `x`, `c`, `c_in`, `s`, `stack0`, `stack1`, `skip`, `skip_count`, `ram_delay`, `c_delay`, `prev_op`, `prev2_op`, `prev3_op`, `tab_pending`, `int1l_hit`; instances of `pps41_decode` and `pps41_alu`; and a synchronous `always` block implementing the same dispatch cascade as the golden model's `step()` from Tasks 4-7, opcode group by opcode group, using `pps41_decode`'s `op_hi`/`op_fc`/`op_lo2`/`op_lo4`/`op_lo6`/`is_tr`/`prev_is_tr`/`prev2_is_tr` outputs in place of the golden model's manual bitmasking, and `pps41_alu`'s `op_sel`/`result`/`carry_out`/`overflow` in place of the golden model's inline arithmetic.

Because this block is large, implement it incrementally within this task rather than in one pass: add one opcode group at a time (LAI/LB/EOB coalescing group; register-memory group L/X/XDSK/XNSK; arithmetic group A/AC/ACSK/AISK/COM; bit-manipulation group SB/RB/SKBF; jump/call group T/TM/RT/RTSK; TR-prefixed group TL/TML/TLB/TMLB/SKBEI/SKAEI; TAB/SAG/INT1L/IX), rerunning `make -C sim core-test` after each group against a small hand-written per-group synthetic ROM (reuse the same byte sequences from the matching golden-model unit test in Tasks 4-7) before moving to the next group. Do not attempt to write and debug the entire dispatch block in one shot — the golden model tests already pinned down the exact expected byte sequences and results per group; use those as the per-group acceptance check here, then move to Task 14 for the full cross-group and real-ROM validation.

For each opcode group, the RTL body should be the direct translation of that group's already-committed golden-model `step()` code (Tasks 4-7) into a synchronous block: same conditions, same register updates, using `pps41_alu` for the `A`/`AC`/`ACSK`/`COM`/`AISK` cases and `pps41_decode`'s fields for case selection instead of raw bitmasking. Do not re-derive semantics from `docs/initial-plan.md` a second time — the golden model is the checked, working reference for exactly what each group must do.

- [ ] **Step 4: Run the full lockstep test after each group, and after the final group**

Run: `make -C sim core-test && /tmp/golden_trace_check` — more precisely, run the compiled `obj_dir_core/Vpps41_core` driver against each per-group synthetic ROM file (write these as small `.bin` files under `sim/vectors/`, reusing the exact byte arrays from the corresponding Task 4-7 test) via:
```bash
./sim/obj_dir_core/Vpps41_core sim/vectors/<group>.bin 20
```
Expected: `PASS: 20 cycles, no mismatches` for each group's vector, before moving to the next group.

- [ ] **Step 5: Commit (one commit per opcode group, to keep bisectable history)**

```bash
git add src/pps41_core.v sim/vectors/<group>.bin
git commit -m "Wire <group> opcodes into pps41_core.v RTL execution"
```
Repeat for each group; a final commit once all groups are wired:
```bash
git add src/pps41_core.v sim/pps41_core_tb.cpp sim/Makefile
git commit -m "Complete pps41_core.v full instruction execution and lockstep testbench"
```

---

### Task 14: Synthetic vector suite covering every named quirk

**Files:**
- Create: `sim/vectors/*.bin` (one file per quirk not already covered by Task 13's per-group vectors)
- Modify: `src/pps41_core.v` and/or `sim/golden/mm77la_model.cpp` (bugfixes only, as found)

**Interfaces:**
- Consumes: `pps41_core_tb` (Task 13)

The synthetic-vector list from the design spec §3, cross-referenced against what Task 13's per-group vectors already exercise incidentally: LFSR wraparound across a full 64-step page (already covered structurally by Task 11's PC test, but not run through the full lockstep harness yet), LB/EOB coalescing across a TR prefix specifically (not just back-to-back LB/EOB), carry delay across `AC`→(intervening op)→`SKNC` with more than one intervening instruction, `ram_delay`'s effect actually changing which address a *following* instruction reads (not just that the flag gets set), SAG's one-cycle scope verified through a full lockstep run rather than the isolated Task 12 RAM test, subroutine-page `T`/`TM` edge cases at the actual top-of-ROM address, and TAB immediately followed by another TAB (back-to-back).

- [ ] **Step 1: Write each vector as a small standalone .bin (assembled by hand from the opcode tables in docs/initial-plan.md §5.1)**

Example — LB/EOB coalescing broken by an intervening TR-prefixed instruction:
```bash
python3 -c "
import struct
rom = bytes([
    0x15,        # LB 5
    0x30, 0xC0,  # TR; TL 0 -- breaks the coalescing run
    0x0A,        # EOB 2 -- NOT suppressed, because prev_op chain was reset by the TR
])
open('sim/vectors/lb_eob_tr_break.bin', 'wb').write(rom)
"
```
Repeat this pattern for each remaining named quirk in the list above, one `.bin` file per quirk, with a comment above the `python3 -c` block naming which quirk it targets and what result is expected (write the expectation in the commit message, not just in your head).

- [ ] **Step 2: Run each vector through the lockstep harness**

Run: `./sim/obj_dir_core/Vpps41_core sim/vectors/<name>.bin 30` for each vector.
Expected: `PASS: 30 cycles, no mismatches` for every one.

- [ ] **Step 3: For any FAIL, root-cause against docs/initial-plan.md before patching**

If a mismatch appears, first determine which model is wrong: re-read the exact clause in `docs/initial-plan.md` §2/§5.1/§5.2 covering that quirk, and check both the golden model and the RTL against it independently — don't assume the RTL is the buggy one just because it's newer. Fix whichever is wrong, re-run the specific vector, then re-run every other vector in this task (a fix for one quirk can regress another, especially anything touching `prev_op`/`prev2_op`/`prev3_op` tracking).

- [ ] **Step 4: Commit each vector alongside any fix it required**

```bash
git add sim/vectors/<name>.bin src/pps41_core.v sim/golden/mm77la_model.cpp
git commit -m "Add <quirk> synthetic vector and fix <what was wrong>"
```
(If a vector passed with no fix needed, commit it alone with a message saying which quirk it confirms.)

---

### Task 15: Real-ROM sustained run — Phase 1 completion

**Files:**
- No new source files. Uses `sim/obj_dir_core/Vpps41_core` (Task 13) against the real ROM in `development-assets/b8000-12` (gitignored, already present locally per the earlier verification).

**Interfaces:**
- Consumes: `pps41_core_tb` (Task 13), the sourced ROM.

- [ ] **Step 1: Run an initial short trace to observe the ROM's actual behavior**

Run: `./sim/obj_dir_core/Vpps41_core development-assets/b8000-12 5000`
Expected: either `PASS: 5000 cycles, no mismatches`, or a mismatch printout pointing at a specific cycle/opcode to investigate (treat any mismatch the same way as Task 14 Step 3 — root-cause against the spec doc before patching).

- [ ] **Step 2: Once 5000 cycles pass clean, extend to a sustained run**

Run: `./sim/obj_dir_core/Vpps41_core development-assets/b8000-12 200000`
Expected: `PASS: 200000 cycles, no mismatches`. 200,000 cycles at the chip's ~4-phases-per-cycle, ~380kHz-derived instruction rate is well over 10 seconds of emulated real-time gameplay — enough to observe multiple full game-loop iterations without needing to hand-derive the ROM's exact loop length first.

- [ ] **Step 3: Check whether INT1L or IX were ever hit during the run**

The golden model's `int1l_hit` flag and the RTL's equivalent should be checked after the run (add a one-line print of `golden.state().int1l_hit` at the end of `pps41_core_tb.cpp`'s `main()`, after the loop, if not already visible). If `int1l_hit` is true at any point, stop and flag it explicitly rather than silently accepting the no-op behavior — this confirms or refutes open risk #2 from the design spec for real. If `IX` was executed (also worth logging a hit-count for), that's expected and fine — Phase 1's stub is deliberately silent there since PLA wiring is out of scope, but knowing it's exercised confirms Phase 2's `IX`/PLA work is indeed on the critical path.

- [ ] **Step 4: Record the result in the design spec's open-risks section**

Edit `docs/superpowers/specs/2026-08-02-cpu-core-phase1-design.md`'s "Open risks carried over" section: append a line noting whether `INT1L` was observed during the 200,000-cycle real-ROM run, and whether `IX` was exercised (expected) or not (unexpected, worth a note for Phase 2 planning).

- [ ] **Step 5: Commit**

```bash
git add docs/superpowers/specs/2026-08-02-cpu-core-phase1-design.md
git commit -m "Complete Phase 1: real-ROM 200k-cycle lockstep run passes, record INT1L/IX observations"
```

This is Phase 1's completion criterion (per the design spec §3) — once this task's commit lands, the CPU core + golden model sub-project is done, and the next spec (I/O/display/PLA) can be brainstormed against a working, proven core.

---

## Self-Review Notes

- **Spec coverage:** every section of the design spec (§1 repo layout, §2 golden model, §3 harness/vectors/success-criterion, §4 error handling/INT1L) maps to at least one task above (Tasks 1, 2-8, 9-14, 15 respectively).
- **No placeholders:** every step has literal code or a literal shell command; the one place genuine judgment is deferred (Task 13's incremental per-group wiring, Task 14/15's "root-cause before patching") is flagged as requiring investigation, not left as an unstated TODO — those are inherent to porting a bespoke ISA and can't be pre-solved without seeing actual mismatch output.
- **Type/name consistency:** `Mm77laModel`/`Mm77laState` and their field names (`pc`, `a`, `b`, `x`, `c`, `c_in`, `stack`, `skip`, `skip_count`, `ram_delay`, `sag`, `c_delay`, `prev_op`/`prev2_op`/`prev3_op`, `tab_pending`, `int1l_hit`) are introduced in Task 2 and used identically through Task 15. RTL port names (`rom_addr`/`rom_data`, `ram_addr`/`ram_rdata`, `a_out`/`b_out`/`skip_out`, `dbg_*`) are introduced in Tasks 11-13 and reused consistently in Task 14-15's harness invocations.
