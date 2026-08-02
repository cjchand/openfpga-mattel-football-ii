# Phase 1 Design: PPS-4/1 CPU Core + Golden Model

Companion spec to `docs/initial-plan.md`. This is the first of several planned
sub-projects for the Mattel Football II core (CPU core → I/O/display/PLA →
bezel/packaging). See `docs/initial-plan.md` for full ISA/architecture detail;
this document scopes and locks down decisions specific to Phase 1.

## Scope

A cycle-accurate MM77LA CPU core: PC (with LFSR low-6 quirk), A/B/X/C/S
registers, stack, skip/skip_count logic, ram_delay/SAG, carry-delay — plus a
standalone golden-model testbench that proves the RTL cycle-accurate against
a real ROM.

This is the highest-risk piece of the whole project (LFSR PC stepping,
skip/coalescing/carry-delay quirks, TAB's one-opcode-delayed fire) and the
foundation every later phase builds on. Get this right in isolation before
adding I/O, display, or PLA complexity on top.

**Out of scope for Phase 1** (deferred to later specs): `IX`/output-PLA
opcode, tone generator/`IOS` state machine, `D`/`R`/`P` port I/O, display mux,
bezel art, openFPGA core-template vendoring/packaging.

## 1. Repo layout

```
src/
  pps41_core.v        # PC (LFSR low-6 + linear high bits), A/B/X/C/S regs,
                       # stack, skip/skip_count, ram_delay/SAG, carry_delay
  pps41_alu.v          # A/AC/ASK/ACSK/COM/AISK/I1SK (inline in pps41_core.v
                        # is also acceptable if it stays small)
  pps41_decode.v        # MM78-tier opcode map baked in directly (1/2/3-byte
                         # dispatch) — no MM76-tier dispatch modeled, per
                         # initial-plan.md §8
sim/
  golden/
    mm77la_model.h       # flat, single-tier C++ transcription of the final
    mm77la_model.cpp      # MM77LA-resolved semantics (not a class hierarchy)
  pps41_core_tb.cpp        # Verilator TB: drives ROM, steps RTL, compares
                            # against golden model every cycle
  vectors/                 # hand-written synthetic instruction streams,
                            # one group per opcode and per named quirk
  roms/                    # gitignored — real b8000-12 dump goes here for
                            # local runs, never committed
docs/
  initial-plan.md
  superpowers/specs/2026-08-02-cpu-core-phase1-design.md   # this file
```

No `core-template`, packaging `Makefile`, `dist/`, or `cfg/` yet. Those are
introduced in the I/O/display phase once there's an actual openFPGA-integrated
core to wire into them — Phase 1 has nothing for that scaffolding to
exercise.

## 2. Golden model

One flat `mm77la_model.cpp` — not a transcription of MAME's
`pps41_base → mm76 → mm78 → mm78la → mm77la` class hierarchy. Every opcode is
implemented directly with its final MM77LA-resolved behavior, since
`initial-plan.md` §5.2 already did the override resolution for us and this
project only ever targets one chip.

State modeled: PC (11-bit, LFSR low-6 per §4), A (4-bit), B (7-bit, Bu/Bl
split), X (4-bit), C (1-bit immediate `c` + delayed `c_in`), 2-level stack,
`skip`, `skip_count`, `ram_delay`, `sag`, `c_delay`, and `prev_op`/`prev2_op`/
`prev3_op` (needed for LB/EOB/LAI coalescing and TR-prefix tracking).

`IX` is stubbed as a no-op in Phase 1: the model records that the opcode
executed (for test-vector bookkeeping) but does not touch PLA lookup or
R-pins — no PLA wiring exists yet, and no R-pin state is modeled at all this
phase.

`INT1L` is a no-op in both RTL and golden model (matching MAME's own
`op_todo()` non-behavior for this tier), but the testbench asserts/flags
loudly if the real ROM ever actually executes it. This is a separate flagged
event, not a cycle-diff mismatch — hitting it isn't itself a golden-model
disagreement, it's a "we need to go find out what this really does" signal.

## 3. Test harness & vectors

Both the golden model and the Verilated RTL are stepped in lockstep, cycle by
cycle, diffing PC / A / B / X / C / S / stack / skip / skip_count after every
cycle (same pattern as FB1's `b6100_cpu_tb.cpp` / `golden.tr` /
`test_trace_diff.py`).

Two vector sources, both required:

- **Synthetic vectors** (`sim/vectors/`): short hand-assembled instruction
  streams, one group per opcode and explicitly one per named quirk from
  `initial-plan.md` §2/§4/§5:
  - LFSR PC wraparound and page-boundary stepping
  - LB/EOB coalescing, including a TR prefix breaking a coalescing run
  - LAI coalescing
  - TAB's one-opcode-delayed fire
  - carry delay across `AC` → `SKNC`
  - `ram_delay` behavior on `XAB`/`XDSK`/`XNSK` (and its absence on MM78's
    `LBA`)
  - `SAG`'s exactly-one-cycle scope
  - subroutine-page special-casing in `T`/`TM`
  - 2-byte (`TR`-prefixed) and 3-byte (`TR`,`TR`-prefixed) instruction forms
  This is where quirks get caught in isolation, with an obvious root cause
  when something's wrong.
- **Real-ROM run**: the full `b8000-12` dump (1536 bytes, verified SHA1
  `4fafc9deb5609b16f09b18b7346ea96ffe8bf9e0` against `initial-plan.md` §1) is
  executed to a sustained run through both models, cycle-diffed throughout.
  This is the integration-level proof — it's what actually tells us the core
  is right, not just individually-tested behaviors composing correctly.

**Success criterion for Phase 1 completion**: the real-ROM run produces zero
register/skip mismatches between RTL and golden model across a sustained run.
"Sustained" means multiple full game-loop iterations; the exact cycle count
gets pinned down once the ROM's loop structure is visible from an initial
trace.

## 4. Error handling

No runtime error handling inside the core itself — this is a faithful
microarchitecture port, not a system with recoverable-error paths. The only
"error" concept is the `INT1L` flag described in §2, surfaced as a testbench
assertion, not core logic.

## Open risks carried over from initial-plan.md §9

- No confirmed datasheet for MM77LA/B8000 exists; MAME's C++ model is the
  best-available reference, not gospel silicon truth.
- `INT1L` real hardware behavior is unknown (see §2/§3 above for how Phase 1
  handles this).
- Clock frequency (~380kHz) is an RC-oscillator approximation, not
  crystal-accurate — irrelevant to Phase 1's cycle-accuracy work (which is
  measured in core cycles, not wall-clock time) but will matter later.
- Task 15 real-ROM observation (200,000-cycle lockstep run against
  `development-assets/b8000-12`, zero register/skip mismatches): `INT1L`
  never fired (0 hits), so the real-hardware-behavior question above remains
  genuinely open/unresolved rather than confirmed either way. `IX` fired
  8,695 times over 200,000 cycles (216 times in just the first 5,000),
  confirming it is a heavily-used, hot-path instruction in the real Football
  II ROM — Phase 2's PLA/`IX` wiring is squarely on the critical path, not an
  edge case.
