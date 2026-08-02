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

**PLA row/bit ordering, confirmed against MAME's `pla_device::read()`/
`pla_parse()` (`src/devices/machine/pla.cpp`, `src/lib/util/plaparse.cpp`):**
rows in the `.pla` file are **not** in binary-counting order (they're a
Gray-code-like enumeration: `0000,1000,1100,0100,0010,...`) — a lookup table
generator must match each row by its literal input pattern, not by file
line order. Within a row, the **leftmost input character is bit 0 (LSB) of
A**, not the MSB (e.g. row `"1000 ..."` is A=1, not A=8) — confirmed by
tracing `plaparse.cpp`'s per-column fuse emission into `pla.cpp`'s
`(term->and_mask | inputs) == input_mask` match against `read(input)`'s bit-`i`-of-`input`
extraction. Output columns map directly, left-to-right = bit 0..9 (no
inversion — `plaparse.cpp`'s double negation on the OR-matrix fuses cancels
out). `tools/gen_opla_table.py` (§2 below) must implement exactly this:
build a 16-entry `A -> raw_10bit` dict keyed by literal input value, not by
row position. Verified end-to-end against `development-assets/mm77la_mfootb2_output.pla`:
applying this parse, then `IX`'s `~raw & 0x3FF` and
`bitswap<10>(out, 9,7,5,3,1,0,2,4,6,8)`, gives `A=0x0 -> r_output=0x03F`
(`0b0111111`, the standard 7-segment "0" pattern) and `A=0xF -> r_output=0x000`
(blank/all-off, consistent with `TAB`/other code using `A=0xF` as a "blank
digit" sentinel elsewhere in the ISA) — both sane, corroborating results,
not just an unverified transcription.

`IX`'s exact behavior (`docs/initial-plan.md` MM77LA-tier override),
transcribed literally including the non-obvious bitswap (bit 8's position is
intentionally reused, not a typo — do not "clean up" the pattern):

```cpp
u16 out = ~opla_read(A) & 0x3FF;
r_output = bitswap<10>(out, 9,7,5,3,1,0,2,4,6,8);
```

## 3. Port I/O and interrupts

`pps41_io.v`/`mm77la_io.*` model:
- **P** (8-bit input: buttons/D-pad) — read via `I1SK`/`I2C`. (Correction:
  plain `I1` doesn't exist on MM77LA per the resolved byte-map above — `I1SK`
  at opcode `0x60` exactly, `mm78op.cpp::op_i1sk()`: `A += P_input & 0xF; skip
  if no overflow`, is the chip's only P-port-reading opcode. Phase 1 stubbed
  `0x60`-exact as a no-op pending this phase; Phase 2 implements it for
  real. Since it's the *only* reachable P-port read, it's almost certainly
  the ROM's actual button-polling mechanism — directly relevant to the
  idle-loop investigation below.)
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

**Resolved during plan-writing (2026-08-02), against MAME master
`src/devices/cpu/pps41/{mm76,mm76op,mm78,mm78op,mm78la,mm78laop,pps41base}.cpp`:**

`IBM`/`OB`/`IAM`/`OA`/`I1`/`INT1H`(old MM76 meaning)/`DIN1`/`INT0L`(old MM76
meaning)/`DIN0` are **not part of MM77LA's opcode map at all**. MM76's
`execute_one()` dispatch tree calls these handlers directly by opcode byte,
but MM78's `execute_one()` (which MM77LA inherits wholesale, per
`docs/initial-plan.md` §2) has a completely different, non-overlapping
fully-decoded table — it never calls `op_ibm`/`op_ob`/`op_iam`/`op_oa`/`op_i1`/
`op_int1h`/`op_din1`/`op_int0l`/`op_din0` anywhere, at any byte value. These
are MM76-only opcodes, unreachable once the ISA moves to the MM78-tier
dispatch tree. Do not implement them; there is no byte value to assign.

This also resolves a related, non-obvious consequence for `SOS`/`ROS`/
`SKISL`'s "D-output-pin vs interrupt-flag" branch (`docs/initial-plan.md`
§5.2, MM78 tier): the branch is `if (bl < d_pins) -> D-pin; else if (bl < 12)
-> interrupt flip-flop; else -> invalid`. MM77LA's `d_pins` is 12 (inherited
from `mm78la_device::device_start()`'s `set_d_pins(12)`, never overridden by
`mm77la_device`) — meaning `bl < d_pins` (12) already covers every `bl` value
that could also satisfy `bl < 12`. **The interrupt-flip-flop branch is dead
code on this specific chip**: `SOS`/`ROS`/`SKISL` can only ever target D-pins
(`bl` 0-11) or hit the invalid case (`bl` 12-15), never the flip-flop path.
Combined with `DIN0`/`DIN1` not existing (above) and MM78LA's tier overriding
`INT0H` to a speaker toggle (no longer a skip-test) and `INT1L` to
`op_todo()` (unimplemented no-op) — **and `mfootb2`'s machine config in
`hh_pps41.cpp` never wires up `PPS41_INPUT_LINE_INT0`/`INT1` to anything** —
the `m_int_ff`/`m_int_line` interrupt-flag machinery is entirely inert on
Football II. There is no functional interrupt-flag skip-test opcode on this
chip at all. Implement `SOS`/`ROS`/`SKISL`'s full branch faithfully anyway
(matching MAME source exactly, including the unreachable branch and the
`ram_addr & 0x40` "B7 must be low" invalid-access guard) rather than
special-casing it away — cheap to transcribe, and it documents the dead path
instead of silently deleting it. Practical upshot: the Phase 1 idle-loop
hypothesis's "escape via interrupts" reading is now ruled out with evidence;
if anything unsticks the ROM's loop, it has to be P-port polling
(`AISK`/`I1SK` reading channel 1, or `I2C` reading channel 2), which is
exactly what this phase's stimulus-driven investigation (§5) tests.

Byte values actually reachable and in this phase's scope, confirmed against
MAME's `mm78.cpp::execute_one()` fully-decoded switch: `SKISL`=`0x01`,
`INT0H`=`0x03` (repurposed to speaker toggle at the MM78LA tier — see §4),
`IOS`=`0x2D`, `SOS`=`0x70`, `ROS`=`0x71`, `IX`=`0x72`, `OX`=`0x73`,
`I2C`=`0x78` (unchanged from MM76: `A = ~read_p() >> 4 & 0xF`, reads the P
port, not R), `IOA`=`0x7B`. `INT1L`=`0x04` was already correctly handled as a
flagged no-op in Phase 1 (matches MM78LA's `op_todo()`) — no change needed.

One adjacent, genuinely out-of-scope gap surfaced by this same source read:
`LXA`(`0x75`)/`XAX`(`0x79`)/`XAS`(`0x74`) are general MM78-tier register
opcodes (not I/O), currently unimplemented (silently fall through as NOP) in
both Phase 1's golden model and RTL. They're not part of this phase's I/O
scope per this spec's own Scope section, so Phase 2 does not implement them —
but Phase 2's real-ROM lockstep run adds an explicit "unimplemented opcode
dispatched" flag (mirroring the existing `int1l_hit` pattern) so this gap is
empirically confirmed hit-or-not-hit rather than silently assumed harmless.
If the real ROM does hit one, that's a finding for a follow-up task, not
something to silently patch here.

Confirmed against MAME source: `IOA`/`OX` (MM78LA tier, `mm78laop.cpp`) use
the **delayed carry `m_c_in`**, not the immediate carry, in their
`(carry<<4 | A)` field — same `c_in_eff` value already threaded through
`src/pps41_core.v`'s ALU path in Phase 1, not a new signal. `R` output resets
to **all-1s** (`m_r_output = m_r_mask` in `pps41_base_device::device_reset()`),
not zero — get this reset value right, it differs from `D`'s reset-to-zero.

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

Confirmed exact details against `mm78laop.cpp`/`mm78la.h`/`mm78la.cpp` (all
fields are 8-bit `u8`, not wider): the arming check reads `m_ios_state`
**before** incrementing it (`if (m_ios_state == 1) { arm } else { disarm };
m_ios_state = (m_ios_state + 1) % 3`) — i.e. arming happens on the *second*
`IOS` call after a reset/disarm, not the first. `reset_tone_count()` sets the
counter to **1**, not 0. `m_tone_count` free-runs (increments) **every
single cycle unconditionally**, even while `tone_on` is false — the `tone_on`
gate only controls whether a counter-match triggers `toggle_speaker()`, not
whether the counter itself advances. The speaker level register
(`m_spk_output`) initializes to **2** (`device_start`) and `toggle_speaker()`
does `m_spk_output ^= 3`, which — starting from 2 — perpetually alternates
between index 2 (level `-1.0`) and index 1 (level `+1.0`) in the
`{0.0, 1.0, -1.0, 0.0}` levels table; it never revisits index 0 or 3. Model
this as a 2-bit register with that exact init value and XOR, not a plain
toggle bit.

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
