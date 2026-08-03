# APF Integration Design: real Football II core on Pocket hardware

Companion spec to `docs/initial-plan.md` and the Phase 1-3 specs
(`2026-08-02-cpu-core-phase1-design.md`, `-io-peripherals-phase2-design.md`,
`-display-pipeline-phase3-design.md`) and the toolchain-scaffold spec
(`2026-08-02-toolchain-scaffold-design.md`). This is the fourth of five
sub-projects on the roadmap those specs name explicitly: **CPU core → I/O
peripherals → display pipeline → APF/openFPGA integration (this spec) →
bezel/packaging**. The first three are done and cross-verified against a
C++ golden model over both synthetic vectors and the real ROM; the
toolchain-scaffold sub-project proved the Docker/Quartus/Pocket pipeline
end-to-end with the vendored template's own do-nothing placeholder core.
This sub-project wires the real, already-verified CPU/display/tone/IO RTL
into `core_top.v` so the actual game boots and plays on physical hardware.

**Explicit reuse, not a fresh derivation:** the sibling
`cjchand/openfpga-mattel-football` (Football I) project already did the
architecturally-equivalent integration in its own
`docs/superpowers/plans/2026-07-25-apf-integration.md` ("Plan 4 of 5") —
`ce_gen` (clock-enable derivation), a bridge-fed ROM loader, and a real I²S
audio path are all directly-adaptable patterns from that plan, reused for
their *shape*, not their content (FB2's CPU/display/audio logic is
unrelated silicon, per `initial-plan.md` §10).

## Scope

Wire `pps41_core.v` (which already internally instantiates
`pps41_opla`/`pps41_tone`/`pps41_io`) and the separately-instantiated
`pps41_display_mux`/`pps41_display_pwm` into the vendored template's
`core_top.v`, replacing its placeholder gray-fill video and silent audio,
so that: the real ROM (`development-assets/b8000-12`, already present
locally, gitignored) loads and executes at the correct instruction rate,
real video output shows the 7-digit/30-LED display, real audio plays
through the speaker, and Pocket controls map to the game's buttons.
Completion criterion is a human playing the real game on physical Analogue
Pocket hardware — the same bar FB1's Plan 4 and this project's own
toolchain-scaffold sub-project both used.

**Explicit non-goals** (deferred to the next, final "bezel/packaging"
sub-project): polished bezel artwork, real branding identity in
`core.json`/platform JSON/icon (stays on the template's placeholder
`Developer`/`Core Template` identity), `input.json` button-label
customization, difficulty-switch (PRO1/PRO2) settings-menu wiring (hardcoded
to PRO1 here, same deferral FB1 made), and high-fidelity seven-segment
glyph rendering (this phase draws the PWM matrix as plain procedural
rectangles — legible and functionally correct, not hand-crafted).

## 1. Required RTL modification (do first, regression-test before anything else)

`pps41_core.v` and `pps41_display_pwm.v` currently run their sequential
logic on every `posedge clk` unconditionally — correct for lockstep
golden-model comparison, where each clock edge *is* one instruction cycle,
but real hardware only has the template's fixed 12.288 MHz `clk_74a`.
Gating an actual clock net (rather than an enable) is bad FPGA practice and
untested by the existing sim harness, so:

- Add a `ce` input to `pps41_core.v`; gate its sequential `always` blocks
  with `if (ce)`. Wire the internal `u_tone` instance's existing
  `cycle_en` port (currently hardcoded `1'b1`) to this new `ce` instead —
  `cycle_en` was already built for exactly this purpose.
- Add the same `ce` input to `pps41_display_pwm.v`. Its `WINDOW = 1583`
  cycle count is instruction cycles, not pixel clocks — at the ~95 kHz
  instruction rate this derives (see §2), `1583` cycles ≈ 60.01 Hz, matching
  the display's implied PWM refresh. It must advance in lockstep with the
  CPU's `ce`, not the raw 12.288 MHz clock.
- `pps41_display_mux.v` stays combinational and untouched (no `clk` port).
- **Regression:** re-run the existing `pps41_core_tb`/`pps41_display_pwm_tb`
  lockstep tests with the new `ce` port tied high, confirming byte-for-byte
  identical results to the current passing baseline. This is a refactor,
  not a behavior change — any diff is a bug in the modification, not an
  acceptable new result.

## 2. `ce_gen.v` — ~95 kHz clock enable from the 12.288 MHz core clock

Same accumulator technique as FB1's `ce_gen.v` (and the template's own
`audgen_accum` for its audio MCLK). Target rate: 380 kHz (the MM77LA's
approximate real-hardware oscillator, `initial-plan.md` §1) ÷ 4 clock
phases per instruction cycle = **95000 Hz**. `clk_74a` (12.288 MHz) is the
one clock the whole core runs on; `ce_gen` produces a single-cycle-wide
pulse averaging 95000 Hz, feeding the `ce` ports added in §1.

```
module ce_gen #(parameter CLK_HZ = 12288000, parameter CE_HZ = 95000)
              (input clk, input rst_n, output reg ce);
```

Note this is an *approximation* of real silicon (open risk #4 in
`initial-plan.md` §9 — the RC oscillator isn't crystal-locked on real
units either) — unlike FB1's 70000/12288000 ratio, 95000/12288000 does not
divide the accumulator's overflow period exactly every `CLK_HZ` clocks, so
expect (and test for) a small long-run rate error rather than assuming
zero error by construction; document the actual measured rate in the unit
test rather than asserting an exact integer pulse count over one second.

## 3. `rom_loader.v` — SD-card ROM into the CPU's address space

A bridge-write BRAM sized for the real ROM's dense storage (1536 bytes),
filled via the APF dataslot mechanism (same `bridge_addr`/`bridge_wr`/
`bridge_wr_data` pattern `core_top.v` already exposes and FB1's
`rom_loader.v` used). Reads from `pps41_core.rom_addr[10:0]` translate
through the `0x600-0x7FF → mirrors 0x400-0x5FF` fold (per
`docs/superpowers/plans/2026-08-02-cpu-core-phase1.md`'s Global
Constraints) before indexing the dense array — the same fold the existing
sim harness already performs when loading `development-assets/b8000-12`,
so the translation logic can be ported directly rather than re-derived.
`data.json` gets one dataslot entry (`filename`, `address`, `size: 1536`)
following the same real-core JSON shape FB1's plan read from an installed
Pocket core (`{"id":0,"name":"ROM","required":true,...}`).

## 4. `audio_gen.v` — tone generator output to real I²S

`pps41_tone.spk_output[1:0]` encodes `{0.0, +1.0, -1.0, 0.0}` (per
`initial-plan.md` §7's `speaker_levels` table) — a bipolar level, not
FB1's simpler single-bit beep. Adapt FB1's `audio_i2s.v` shape (serializing
onto `audio_mclk`/`audio_lrck`/`audio_dac`) to map the 2-bit level to a
signed sample value instead of a single bit. No new audio golden model is
needed — `pps41_tone`'s frequency/arming behavior was already fully proven
in Phase 2; this module is a pure level-to-I²S-frame format converter.

## 5. `display_render.v` — PWM matrix to video

Reads `pps41_display_pwm.levels[219:0]` (110 cells × 2-bit brightness,
`cell = row*11 + col`) and `window_tick`, and draws into the video timing
region `core_top.v` already generates. Per the user-approved fidelity
level, this phase renders **simple procedural shapes**, not hand-crafted
seven-segment glyphs: each of the 7 digit positions (a fixed rectangular
block per segment-cell) and 30 discrete LED positions (a fixed-size
square/rect each) drawn as flat-colored regions whose brightness follows
the cell's 2-bit level (off / dim / bright, matching §7's `0.015`/`0.2`
relative-brightness note — the ball-position LEDs render visibly brighter
than the digit segments). Layout mapping (which of the 110 `(row,col)`
cells correspond to which of the 7 digits' segments vs. the 30 LEDs) comes
from the `write_d`/`write_r` bit assignments and `update_display()` formula
already transcribed in `initial-plan.md` §7 and already used to build the
Phase 3 golden model/RTL — no new reverse-engineering needed, just a
screen-position lookup table.

**Video timing:** keep the vendored template's stock 320×240 @ 60 Hz timing
(`core_top.v`'s existing `VID_H_ACTIVE`/`VID_V_ACTIVE`/etc. localparams)
unchanged — unlike FB1, which retimed its PLL-derived canvas to fit a
400×360 renderer, this phase's simple procedural layout has no fixed
canvas-size requirement, so keeping the template's default minimizes risk.
`video.json`'s `scaler_modes` stays whatever the template's default already
declares for 320×240.

## 6. Input mapping (no new module — `core_top.v` wiring only)

`cont1_key` bits map directly to `pps41_core.p_input[7:0]` per
`initial-plan.md` §7's IN.0 table: bit0 Score (START2), bit1 Status
(START1), bit2 Up, bit3 Right, bit4 Kick (BUTTON2), bit5 Pass (BUTTON1),
bit6 Down, bit7 Left — using the template's standard `cont1_key` bit
positions (`[0]`=up,`[1]`=down,`[2]`=left,`[3]`=right,`[4]`=a,`[5]`=b,
`[14]`=select,`[15]`=start, confirmed from `core_top.v`'s own port
comment, same as FB1 read it). Difficulty (PRO1/PRO2, read back on D-bus
bit `0x400`/DIO11) is hardcoded to PRO1 (`din = 4'b0001`-equivalent),
matching FB1's identical deferral of settings-menu wiring.

## 7. Testing & completion criteria

1. `ce_gen`: unit test measuring long-run average rate (expect ~95000 Hz
   ± small accumulator error, not an exact count — see §2) and single-cycle
   pulse width, mirroring FB1's `ce_gen_tb.cpp` shape.
2. `pps41_core`/`pps41_display_pwm` §1 regression: existing lockstep tests
   pass unchanged with `ce` tied high.
3. `rom_loader`: unit test writing the real `development-assets/b8000-12`
   bytes through a simulated bridge write, then reading back through the
   `0x000-0x7FF` address range including the mirror fold, byte-for-byte
   matching the source file.
4. `audio_gen`: unit test on 2-bit-level → I²S frame shape/timing — no new
   behavioral golden model needed (see §4).
5. `display_render`: standalone test feeding known `levels[219:0]` patterns,
   checking pixels land in the correct digit/LED screen regions — layout
   correctness only, no pixel-perfect reference needed given the approved
   simple-shapes fidelity.
6. **Full integration**: `make bitstream` / `make package` produce a
   bootable image with the real ROM loadable via the SD-card dataslot
   mechanism, then a **physical Analogue Pocket boot-and-play checkpoint**:
   real digits/LEDs visible, audio audible, controls responsive to a human
   playing the actual game. This — not compilation — is the sub-project's
   actual completion bar, same as the toolchain-scaffold sub-project and
   FB1's Plan 4 both used.

## Open risks

- **Confirmed CPU-core gap, blocking, found while writing this spec:**
  re-running `sim/pps41_core_tb` and a fresh golden-model-only trace against
  the real `development-assets/b8000-12` ROM shows `unimpl_hit` (the
  golden model's "genuinely unimplemented opcode" latch) firing at cycle
  202 — well inside boot/init, long before real gameplay would start. The
  triggering opcode is **`0x74` (XAS)**; `mm77la_model.cpp`'s big opcode
  switch has cases for `0x76-0x7F` but is missing `0x74` (XAS), `0x75`
  (LXA), and `0x79` (XAX) — all three are in `initial-plan.md`'s own §5.1
  opcode table and have documented semantics in §5.2 (`XAS`: swap
  `A↔s` + update serial-out pin; `LXA`: `X = A`; `XAX`: swap `A↔X`), and
  the state already has `s` and `x` fields to hold them — this looks like
  a straightforward Phase-1 coding omission (the `default: unimpl_hit`
  fallthrough's own comment names exactly these three opcodes), not a new
  unknown. **This must be fixed (golden model + RTL, with a regression
  vector) before or as the first task of this sub-project's implementation
  plan** — real ROM execution is currently running through at least one
  no-op'd opcode from very early in boot, which will silently corrupt
  architectural state (S/X register contents, the XAS serial-out pin) by
  the time real gameplay logic runs. RTL and golden model agree on
  `unimpl_hit` timing (no lockstep mismatch), so this is a shared gap in
  both models, not a golden-vs-RTL divergence.
- **Clock-rate approximation** (§2): 95 kHz is derived from MAME's own
  380 kHz approximation of an RC oscillator, not a measured value — same
  open risk already flagged in `initial-plan.md` §9 item 4, now made
  concrete in an actual `ce_gen` ratio. If real-hardware timing/gameplay
  feel looks off, this ratio is the first place to revisit.
- **`INT1L`**: unlike XAS/LXA/XAX above, a real-ROM run to 2,000,000 cycles
  shows `int1l_hit` never fires — open risk #2 from `initial-plan.md` §9 is
  empirically not exercised by this ROM (at least not within normal
  boot+early execution), so `INT1L`'s no-op stub is not currently a known
  blocker, only a latent gap if some rarely-reached code path uses it.
- **`ce` refactor correctness** (§1): the single highest-risk mechanical
  change, since it touches already-proven-correct RTL. The regression step
  is not optional — treat any lockstep diff as a blocking bug, not
  something to special-case around.
- **Docker/Quartus-on-Apple-Silicon-via-Rosetta fragility**: already
  proven working by the toolchain-scaffold sub-project on this same
  machine, but worth re-verifying (`quartus_sh --version`) before relying
  on it again, per that sub-project's own open-risks note.
