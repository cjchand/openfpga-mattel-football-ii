# Phase 2 Design: I/O Peripherals — PLA, Tone Generator, Port I/O

Companion spec to `docs/initial-plan.md` and
`docs/superpowers/specs/2026-08-02-cpu-core-phase1-design.md`. Second of
several planned sub-projects for the Mattel Football II core (CPU core →
**I/O peripherals** → display pipeline → APF/openFPGA integration →
bezel/packaging). See `docs/initial-plan.md` for full ISA/architecture
detail; this document scopes and locks down decisions specific to Phase 2.

## Scope

Extend the Phase 1 CPU core with the opcodes and registers Phase 1
deliberately excluded: the `IX`/output-PLA opcode, the `IOS` tone-generator
state machine (including `INT0H`'s repurposed speaker-toggle behavior), `P`/
`D`/`R` port I/O (`IBM`/`OB`/`IAM`/`OA`/`I1`/`I2C`/`IOA`/`OX`), and the
interrupt-flag opcodes (`INT0L`/`INT1H`/`DIN0`/`DIN1`/`SOS`/`ROS`/`SKISL`).
All verified in simulation via the same golden-model/RTL lockstep approach
as Phase 1 — no real display rendering, no openFPGA/APF integration, no
bezel work yet. Those become their own later phases, mirroring how the
sibling Football I project split CPU → display pipeline → APF integration →
bezel into separate, independently-verified plans.

**Explicit validation goal:** Phase 1's final whole-branch review found that
the real ROM, once its jump/call decode bug was fixed, settles into a small
idle loop (113 unique PCs over 200,000 cycles) — flagged as an unconfirmed
hypothesis, possibly the ROM polling for button input via the P port, which
Phase 1 has no way to supply. Once P-port input exists in this phase, drive
a synthetic button-press stimulus through the real ROM's lockstep run and
check whether it breaks the loop. Confirm or refute this explicitly, with
evidence — don't leave it speculative.

**Out of scope for Phase 2** (deferred to later specs): real display
rendering/bezel art, openFPGA core-template vendoring/packaging, real
Analogue Pocket button/joystick wiring (P/D inputs stay testbench-driven
registers this phase), actual PWM/I2S audio-DAC output (the tone generator's
internal state machine and single-bit speaker toggle are modeled; hardware
audio output is a later phase's concern).

## 1. Repo layout

New peripheral modules plug into the existing Phase 1 core rather than
growing it in place — same pattern as `pps41_decode.v`/`pps41_alu.v` being
split out from `pps41_core.v` in Phase 1, each independently unit-tested
before wiring in.

```
src/
  pps41_core.v          # Phase 1 CPU core, extended to dispatch the new
                         # opcodes into the peripheral modules below
  pps41_opla.v           # PLA lookup: A -> 10-bit R output (IX opcode)
  pps41_opla_table.vh    # generated from development-assets/
                          # mm77la_mfootb2_output.pla — see §2
  pps41_tone.v            # IOS 3-state arming FSM + free-running tone
                           # counter + speaker toggle output
  pps41_io.v               # P/D/R port registers, INT0/INT1 lines and
                            # their DIN0/DIN1 test-and-set flip-flops
sim/
  golden/
    mm77la_model.h/.cpp     # Phase 1 golden model, extended to dispatch
                             # the same new opcodes
    mm77la_opla.h/.cpp       # golden PLA lookup
    mm77la_opla_table.h      # generated from the same .pla file as above
    mm77la_tone.h/.cpp        # golden tone-generator FSM
    mm77la_io.h/.cpp           # golden port/interrupt registers
  pps41_core_tb.cpp             # extended lockstep TB: diffs PLA/tone/
                                  # port/interrupt state in addition to
                                  # Phase 1's 9 architectural fields, plus
                                  # accepts a scripted input-stimulus file
  vectors/                       # Phase 1's existing vectors, plus one
                                  # new vector per new opcode/quirk (§3)
  stimulus/                      # scripted P/D/INT-line input sequences
                                  # for the idle-loop investigation (§3)
tools/
  gen_opla_table.py               # one-time conversion script: .pla ->
                                   # pps41_opla_table.vh + mm77la_opla_table.h
docs/
  initial-plan.md
  superpowers/specs/2026-08-02-cpu-core-phase1-design.md
  superpowers/specs/2026-08-02-io-peripherals-phase2-design.md   # this file
```

## 2. PLA representation

`development-assets/mm77la_mfootb2_output.pla` (Berkeley PLA text format) is
a fully-specified 16-entry table — all 16 possible 4-bit inputs are present,
no don't-cares, so no logic minimization is needed. It is, functionally,
just a 4-in/10-out lookup table.

`tools/gen_opla_table.py` reads the `.pla` file once and generates:
- `src/pps41_opla_table.vh` — a Verilog case statement or ROM initial block
- `sim/golden/mm77la_opla_table.h` — a `static constexpr` C++ array

Both generated from the same script run and checked into the repo as
generated source (same pattern as Football I's `field_bitmap.mem`/
`label_bitmap.mem`). The `.pla` file remains the source of truth; nobody
hand-edits the generated files. `pps41_opla.v`/`mm77la_opla.*` consume the
generated table, not the raw `.pla` text — no runtime Berkeley-PLA parsing
in either model.

`IX`'s exact behavior (`docs/initial-plan.md` MM77LA-tier override),
transcribed literally including the non-obvious bitswap (bit 8's position is
intentionally reused, not a typo — do not "clean up" the pattern):

```cpp
u16 out = ~opla_read(A) & 0x3FF;
r_output = bitswap<10>(out, 9,7,5,3,1,0,2,4,6,8);
```

## 3. Port I/O and interrupts

`pps41_io.v`/`mm77la_io.*` model:
- **P** (8-bit input: buttons/D-pad) — read via `I1`/`I2C`.
- **D** (12-bit: digit-select output + difficulty-switch input) — written
  via the `write_d` path's opcodes, read back for the difficulty switch.
- **R** (10-bit output, driven either directly via `IOA`/`OX` or through the
  PLA via `IX`) — Football II's final MM77LA override means `IX` is the
  opcode actually exercised; `IOA`/`OX` are implemented per their MM78LA-
  tier semantics for completeness/correctness even if the real ROM may not
  exercise them (confirm against ROM trace during implementation).
- **INT0**/**INT1** lines and their **DIN0**/**DIN1** test-and-set flip-flops.

All are plain testbench-drivable/observable registers in Phase 2 — not
wired to real Analogue Pocket inputs yet. `sim/stimulus/` holds scripted
input sequences (e.g. "hold Score button from cycle N") that
`pps41_core_tb.cpp` can apply during a lockstep run, used for the idle-loop
investigation (see Scope above and §5).

**Open item to resolve during plan-writing, not here:** `docs/initial-plan.md`'s
final fully-decoded MM78-tier opcode table (the one with exact byte values,
e.g. `0x00`=NOP, `0x2D`=IOS, `0x70`=SOS) does not show explicit byte
assignments for the base MM76-tier ops `IBM`/`OB`/`IAM`/`OA`/`I1`/`INT1H`/
`DIN1`/`INT0L`/`DIN0` — only their semantics are given. This needs pinning
down against the doc's fuller opcode-map section or MAME's
`rw5000op.cpp`/`mm78.cpp` source directly (the same discipline Phase 1
repeatedly used) when the implementation plan's task briefs are written —
not guessed at here.

## 4. Tone generator

`pps41_tone.v`/`mm77la_tone.*` model, per `docs/initial-plan.md`'s MM78LA-
tier override:
- `tone_freq`: built across two `IOS` calls (`tone_freq = (tone_freq >> 4) |
  (A << 4)`).
- `m_ios_state`: a 3-state arming sequence (0→1→2). Transitioning 1→2 sets
  `tone_on = true` and resets the tone counter; any other transition sets
  `tone_on = false`. This means `IOS` must be executed a specific number of
  times in sequence to actually arm the generator — a real hardware state
  machine, not a simple register write. Test this heavily; it's exactly the
  kind of sharp edge Phase 1's carry-delay and skip/coalescing quirks were.
- A free-running `tone_count` that increments every internal cycle; when
  `tone_on` and `tone_count == tone_freq`, toggle the speaker output and
  reset the counter.
- `INT0H` (repurposed at the final MM78LA tier from a skip-test to directly
  toggling the speaker output) lives here too, since it's the same
  subsystem.

Only the internal state machine and a single-bit speaker toggle output are
modeled this phase. Real PWM/I2S audio-DAC conversion for Analogue Pocket
hardware output is deferred to a later integration phase.

## 5. Test harness & vectors

Same lockstep discipline as Phase 1: golden model and RTL stepped cycle by
cycle, diffing state every cycle, extended to cover the new PLA/tone/port/
interrupt registers alongside Phase 1's existing 9 architectural fields.

**Synthetic vectors**, one per new opcode/quirk:
- `IX`/PLA lookup, including the exact bitswap pattern
- Each port I/O opcode (`IBM`/`OB`/`IAM`/`OA`/`I1`/`I2C`/`IOA`/`OX`)
- The `IOS` 3-state arming sequence, including a vector proving the "must
  fire a specific number of times in sequence" quirk actually gates arming
  (not just that the state variable holds the right value)
- `INT0H`'s post-arming speaker toggle
- `DIN0`/`DIN1` test-and-set semantics (skip-if-clear-then-set)
- `SOS`/`ROS`/`SKISL`'s branching between D-output-pin and interrupt-flag
  targets at the `Bl < 12` boundary described in `docs/initial-plan.md`

**Real-ROM idle-loop investigation:** extend `pps41_core_tb.cpp` to accept a
`sim/stimulus/` script, re-run the 200,000-cycle real-ROM lockstep run with
a synthetic button-press stimulus applied, and record whether the ROM now
visits substantially more than 113 unique PCs. Document the result — either
confirmation or refutation — in this spec's open-risks section once known;
don't leave it as a standing guess.

## 6. Completion criteria

- Every new opcode has golden-model unit-test coverage and a lockstep RTL
  vector.
- The extended lockstep harness (now diffing PLA/tone/port/interrupt state,
  not just Phase 1's 9 fields) passes zero-mismatch on the real ROM run,
  both with and without the input-stimulus script applied.
- The idle-loop hypothesis from Phase 1's final review is either confirmed
  or refuted with concrete evidence from the stimulus-driven run.

## Open risks carried over from Phase 1 and initial-plan.md §9

- `INT1L` remains genuinely unknown/unimplemented real-hardware behavior —
  unchanged from Phase 1 (MAME's own model doesn't resolve this either).
  Still a no-op-but-flagged in both models.
- The still-undetermined MM76-tier opcode byte assignments noted in §3.
- Phase 1's final review identified that lockstep-only verification (golden
  vs. RTL) cannot catch bugs shared identically by both models — it found
  three such bugs across Phase 1 (a carry-timing bug, a `ram_delay`
  addressing bug, and the jump/call decode-range bug). Adding a third
  independent oracle (e.g. a captured MAME debugger trace) was deliberately
  deferred rather than added in Phase 1; this risk category applies equally
  to Phase 2's new opcodes and has not been mitigated. Worth revisiting
  whether to add the oracle before or during this phase, given the growing
  number of shared-derivation bugs found.
- No confirmed datasheet for MM77LA/B8000 exists — MAME's C++ model remains
  the best-available reference, not gospel silicon truth (carried over from
  Phase 1/`initial-plan.md`).
