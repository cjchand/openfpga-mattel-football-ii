# Phase 3 Design: Display Pipeline (PWM matrix + brightness)

Companion spec to `docs/initial-plan.md`, `docs/superpowers/specs/2026-08-02-cpu-core-phase1-design.md`,
and `docs/superpowers/specs/2026-08-02-io-peripherals-phase2-design.md`. Third
of several planned sub-projects for the Mattel Football II core (CPU core →
I/O peripherals → **display pipeline** → APF/openFPGA integration →
bezel/packaging). See `docs/initial-plan.md` §7 for the driver's I/O wiring
and `docs/superpowers/specs/2026-08-02-io-peripherals-phase2-design.md` for
the `D`/`R` port registers this phase consumes.

## Scope

Reconstruct Football II's multiplexed PWM display matrix from the `D`/`R`
port registers Phase 2 built, and integrate each of the resulting 110 matrix
cells' on-time into a flicker-free, discrete brightness level — matching
MAME's `pwm_display_device` (`src/devices/video/pwm.cpp`/`pwm.h`) semantics,
verified the same golden-model/RTL lockstep way as Phases 1-2.

**Explicit non-goal:** actual pixel/segment shapes, screen geometry,
HDMI/video timing generation, bezel art, or any openFPGA scaffolding. This
phase's deliverable is a settled, per-frame snapshot of "which of the 110
matrix cells are lit, at which of 3 discrete levels (off/dim/bright)" — the
concrete data later phases (APF/openFPGA integration) render into pixels.

**Explicit reuse, not a fresh derivation:** the sibling `cjchand/openfpga-mattel-football`
(Football I) project already solved the "integrate strobed on-time into a
flicker-free discrete brightness level" problem in `src/led_capture.v`,
targeting the same MAME `pwm_display` semantics for its own (simpler,
digit-less) 9×11 matrix. This phase adapts that module's structure —
parameterized window, per-cell counter array, threshold-classify-on-boundary
— rather than re-deriving the algorithm from scratch. What genuinely has no
FB1 analog and must be built fresh is the **matrix reconstruction** step
(turning `D`/`R` register writes into a `rowsel`/`rowdata` pair) — FB1's
B6100 drives discrete `str`/`seg` output lines directly; FB2's MM77LA
combines `D` and `R` through a specific combinational formula (see §2) that
is genuine, chip-specific silicon wiring with no equivalent on FB1.

## 1. Repo layout

```
src/
  pps41_core.v               # unchanged this phase
  pps41_display_mux.v         # D/R -> (rowsel, rowdata) matrix reconstruction
  pps41_display_pwm.v          # per-cell on-time accumulation + window-boundary
                                # threshold classification (adapted from FB1's
                                # led_capture.v — see design note above)
sim/
  golden/
    mm77la_display_mux.h/.cpp    # golden matrix reconstruction
    mm77la_display_pwm.h/.cpp     # golden brightness/window integration
  pps41_display_mux_tb.cpp         # standalone Verilator TB
  pps41_display_pwm_tb.cpp          # standalone Verilator TB
  pps41_core_tb.cpp                   # lockstep TB extended to diff the
                                       # settled per-window display snapshot,
                                       # in addition to Phase 1/2's existing
                                       # architectural/PLA/tone/port state
  vectors/                              # Phase 1/2's existing vectors, plus
                                         # known-strobe-pattern -> known-level
                                         # vectors for the new modules
docs/
  initial-plan.md
  superpowers/specs/2026-08-02-cpu-core-phase1-design.md
  superpowers/specs/2026-08-02-io-peripherals-phase2-design.md
  superpowers/specs/2026-08-02-display-pipeline-phase3-design.md   # this file
```

## 2. Matrix reconstruction (`pps41_display_mux`)

Purely combinational, recomputed whenever `D` (12-bit) or `R` (10-bit)
changes — transcribed exactly from `mfootb2_state::update_display()`:

```cpp
void mfootb2_state::update_display() {
    m_display->matrix(m_d, (m_r << 1 & 0x700) | (m_d >> 4 & 0x80) | (m_r & 0x7f));
}
```

which decomposes into:

```
rowsel  = D[9:0]                                     // 10 rows, a BITMASK
                                                       // (>1 row can be
                                                       // simultaneously
                                                       // selected — real
                                                       // hardware likely
                                                       // strobes one at a
                                                       // time in practice,
                                                       // but nothing in the
                                                       // silicon forces
                                                       // that; model the
                                                       // general case)
rowdata = { R[9:7], D[11], R[6:0] }                   // 11-bit column value
                                                       // R[9:7] -> bits 10:8
                                                       // D[11]  -> bit 7
                                                       // R[6:0] -> bits 6:0
```

`PWM_DISPLAY(config, m_display).set_size(10, 11)` in the MAME driver's
`mfootb2()` machine config confirms these exact dimensions (the `set_size`
signature is `(height, width)`, i.e. 10 rows / 11 columns).

**Resolved finding (this phase's own source-reading, closing an open
question standing since `initial-plan.md` §9 risk #6):** `D[11]` (DIO11) —
previously flagged as possibly-unused, since `mm78la_device` sets 12 D-pins
but the driver's `write_d` comment only documents DIO0-DIO10 — is in fact
real and load-bearing: it is the **sole source of column-bit-7**, a position
`R`'s own 10 bits never reach (`R[9:7]` lands at columns 8-10, `R[6:0]` at
columns 0-6, leaving column 7 as a gap `D[11]` exactly fills). `D[10]` (the
pin the driver comment actually labels "4th digit DP") is excluded from
`rowsel` entirely and is not otherwise consumed by `update_display()` — the
decimal point's live data bit is `D[11]`, sampled once per row-1 strobe (see
§3), not `D[10]`.

**Row semantics**, derived from the driver's segmask calls
(`set_segmask(0x3c7, 0x7f)` then `set_segmask(0x002, 0xff)` — a `digits`
bitmask selecting which rows get which per-row segment mask):

| Rows | Role |
|---|---|
| 0, 1, 2, 6, 7, 8, 9 (7 rows) | Seven-segment digits, columns 0-6 = segments a-g |
| 1 (also) | Column 7 (i.e. `D[11]`) is an 8th segment — the decimal point |
| 3, 4, 5 (3 rows) | The 30 discrete field-position LEDs — no segment grouping, each raw column bit (up to 11 per row) is one independent LED |

7 digit rows matches `initial-plan.md` §1's "7 seven-segment digits"
exactly; 3 LED rows × up to 10-11 real columns each is consistent with "30
discrete LEDs."

## 3. Brightness/window integration (`pps41_display_pwm`)

Adapted directly from `led_capture.v`'s structure (see Scope's reuse note),
with three concrete changes for Football II:

- **110 cells**, not 99 (10×11 vs FB1's 9×11).
- **`WINDOW` recalculated for FB2's clock.** FB1's `WINDOW=1167` comes from
  `280000 (Hz) / 4 (phases per instruction) / 60 (Hz target)`. FB2's PPS-4/1
  core is also 4-phases-per-cycle (`pps41_base_device::execute_clocks_to_cycles`:
  `(clocks+4-1)/4`), so by the same derivation at FB2's ~380kHz approximate
  clock: `WINDOW = round(380000 / 4 / 60) = 1583`. Like FB1's constant, this
  inherits the RC-oscillator clock's inherent approximateness
  (`initial-plan.md` §9 risk #4) — not a new source of imprecision, the same
  one Phase 1/2 already carry.
- **Thresholds from Football II's actual driver config**, not FB1's:
  `m_display->set_bri_levels(0.015, 0.2)` (1.5% / 20%, vs FB1's 2%/20%).
  Following FB1's `(WINDOW * pct) / 100 + 1` strictly-greater-than pattern:
  `DIM_MIN = (1583 * 15) / 1000 + 1 = 24`, `BRIGHT_MIN = 1583 / 5 + 1 = 317`.
- **No cross-window smoothing** (per the explicit decision this phase makes,
  diverging from MAME's exact `bri = bri*0.5 + duty*0.5` interpolation):
  classify directly from each window's raw count, reset every window
  boundary. Matches FB1's proven, shipped approach; avoids carrying
  smoothed floating-point-ish brightness state through hardware for a
  difference unlikely to be visually distinguishable.

```verilog
// per-cell classification at each window boundary (WINDOW=1583, DIM_MIN=24, BRIGHT_MIN=317)
if (cnt[cell] >= BRIGHT_MIN)      level = 2'd2; // bright (typically ball-position LEDs)
else if (cnt[cell] >= DIM_MIN)    level = 2'd1; // dim (typically digit segments)
else                               level = 2'd0; // off
```

Which cells land in which level is an *emergent* property of the ROM's own
strobe timing, not hardcoded per-LED-type — matches `initial-plan.md` §7's
"preserve this relative brightness" note without special-casing digit vs.
LED rows in the classification logic itself (only in which rows get
segment-grouped for later consumption, per §2's row table).

Cell increments each cycle where the cell's row bit is set in `rowsel` AND
its column bit is set in `rowdata` (the AND-of-two-bitmasks "collision
implies powered-on" rule from `pwm.cpp`'s own header comment).

## 4. Output interface for later phases

Once per window boundary, a settled snapshot: 110 cells × 2-bit level,
addressable as `(row, col)`, `row` 0-9 and `col` 0-10. This is the concrete
handoff point to the eventual APF/openFPGA integration phase's renderer — it
consumes "cell (row, col) is at level L this frame," not `D`/`R` register
history, duty cycles, or window mechanics. Row 0-2/6-9 + col 0-6 are digit
segments (row 1's col 7 additionally the DP); rows 3-5 are individual LEDs.

## 5. Test harness & vectors

Same lockstep discipline as Phases 1-2: golden model and RTL stepped in
lockstep, diffing the settled per-window snapshot every window boundary (not
every cycle, since the snapshot is only meaningful once settled — but the
per-cell accumulator state itself should also be diffed every cycle, the
same "don't just check the final answer" discipline Phase 1's RAM-comparison
fix established).

**Synthetic vectors**, one per named quirk:
- A column driven every single cycle for a full window → must land at level
  2 (`>= BRIGHT_MIN`).
- A column driven for exactly `DIM_MIN` cycles out of the window → level 1.
- A column driven for `DIM_MIN - 1` cycles → level 0 (confirms the
  strictly-greater-than boundary, not off-by-one).
- A cell in row 1 at column 7, driven via `D[11]` toggling (not `R`) → proves
  the DP's data source really is `D[11]`, not any `R` bit.
- `rowsel` with multiple simultaneous row bits set → confirms the general
  bitmask case, not just single-row strobing.
- A window boundary mid-multi-byte-instruction (confirms the window counter
  is a pure free-running cycle counter, independent of instruction
  boundaries — matching how `tone_count` free-runs unconditionally in
  Phase 2).

**Real-ROM run:** extend the existing 200,000-cycle lockstep run to also
diff the display-pipeline state throughout, and report which of the 110
cells are ever observed at level 1 or level 2 — a sanity check that the
reconstructed matrix stays within the expected digit/LED row groupings (no
activity ever observed on the "always invalid" row/column combinations)
across real gameplay-derived I/O, not just synthetic vectors.

## 6. Completion criteria

- Every named quirk in §5 has golden-model unit-test coverage and a
  lockstep RTL vector.
- The extended lockstep harness (now diffing per-cell accumulator state
  every cycle and the settled snapshot every window) passes zero-mismatch
  on the real ROM run.
- ~~The `D[11]`-is-the-DP-data-bit finding and the row/LED grouping table in
  §2 are confirmed against real-ROM `D`/`R` activity, not left as
  spec-derived-only claims.~~ **Not met by Task 7's real-ROM run:** see the
  findings paragraph below — `D[11]` was never set once and no cell ever
  reached even the dim threshold across the full 200,000 cycles, so neither
  the DP-data-bit finding nor the row/LED grouping table has real-ROM
  evidence for or against it. Both remain spec-derived-only claims,
  unconfirmed by this run.

**Real-ROM run results (Task 7, `./sim/obj_dir_core/Vpps41_core
development-assets/b8000-12 200000`, no button-press stimulus, `p_input`
held at 0 throughout):** `PASS: 200000 cycles, no mismatches` — the extended
lockstep harness (per-cell accumulator diffed every cycle, settled snapshot
diffed every window boundary) is zero-mismatch against the real ROM.

Temporary instrumentation (added and removed within this task; not part of
the committed harness) recorded, additionally:

- **None of the 110 display cells ever reached level 1 or 2** during these
  200,000 cycles. `rowsel` (`D[9:0]`) was non-zero on 199,793 of 200,000
  cycles (rows are almost always selected), but `rowdata` was non-zero on
  only 204 of 200,000 cycles — `R` (which supplies `rowdata[6:0]` and, via
  `R[9:7]`, `rowdata[10:8]`) was non-zero on only 204 cycles total, and
  `D[11]` (the sole source of `rowdata[7]`, the row-1 DP bit) was never set
  even once. With `WINDOW=1583` and `DIM_MIN=24`, 204 on-cycles spread
  across ~126 windows (≈1.6 on-cycles/window on average, concentrated
  wherever they happen to land) never comes close to lighting any single
  cell to even the dim threshold. **This means the §2 digit/LED row-grouping
  table could not be confirmed or contradicted by this specific run** — the
  run simply never drives enough real column data to light anything,
  regardless of which rows/columns the table predicts. This is consistent
  with, and further corroborates, the open risk below (Phase 1's idle-loop
  finding, carried into this spec's last open-risk bullet): with no button
  press, the ROM appears to spend this entire 200,000-cycle window in a
  loop that strobes `D` (selecting rows) without writing meaningful `R`
  content, i.e. genuinely idling rather than rendering a static screen.
  Confirming the row-grouping table against real column data requires a run
  with button-press stimulus (or simply more cycles) to reach code paths
  that actually populate `R` — out of scope for this task, which is
  constrained to the specific 200,000-cycle, no-stimulus command above.
- **`rowsel` was never observed multi-bit.** Across all 199,793 cycles where
  `rowsel` was non-zero, exactly one bit was set each time (checked via
  `rowsel & (rowsel - 1) == 0` every cycle) — this **refutes** the
  "genuinely multi-bit in practice" possibility for this real-ROM run: real
  gameplay-derived (well, idle-loop-derived) I/O strobes rows one at a time,
  never several simultaneously, even though §2's `rowsel = D[9:0]` bitmask
  formula permits the general multi-bit case in principle. This settles the
  open-risk bullet below — no longer an open question for this data set.

## Open risks carried over from Phases 1-2 and initial-plan.md §9

- No confirmed Rockwell datasheet for MM77LA/B8000 exists; MAME's C++ model
  (here, its generic `pwm_display_device`, not chip-specific code) remains
  the best-available reference.
- Clock frequency (~380kHz) is an RC-oscillator approximation; `WINDOW=1583`
  inherits that same imprecision, same category as Phase 1/2's carried-over
  risk, not a new one.
- Lockstep only proves RTL ≡ golden model, not agreement with real silicon
  or MAME itself — same standing risk category as Phases 1-2's three
  previously-found shared-derivation bugs (most recently Phase 2's `EOB`
  dispatch gap). Read §5's vectors with that limitation in mind.
- ~~Whether `rowsel` is ever genuinely multi-bit in the real ROM (vs. always
  effectively one-hot in practice) is confirmed or refuted empirically via
  §5's real-ROM run, not assumed either way going in.~~ **Refuted by Task
  7's real-ROM run:** `rowsel` was observed non-zero on 199,793/200,000
  cycles and was single-bit every single time — never multi-bit — over the
  full 200,000-cycle no-stimulus run. See §6 for the detailed figures.
- Phase 1's idle-loop investigation (refuted in Phase 2 — the loop the ROM
  currently settles into doesn't poll the P port) remains unexplained.
  Worth revisiting once this phase's reconstructed display state is
  available — cross-referencing what the loop is plausibly rendering during
  that time may shed light on what it's actually waiting for. **Task 7
  update:** the reconstructed display state is now available, and it
  corroborates rather than explains the idle-loop finding — during the full
  200,000-cycle no-stimulus run, `R` (column data) is non-zero on only 204
  cycles total and no display cell ever reaches even the dim threshold,
  consistent with the CPU spending this entire window in a loop that
  strobes rows without rendering real content. What that loop is *waiting
  for* (button press being the obvious candidate, given `p_input` is held
  at 0 throughout this run) is still not directly proven — would need a
  run with button-press stimulus to confirm the loop exits and starts
  driving real column data once input arrives. Left open for a future
  task/phase, since driving synthetic button-press stimulus through the
  lockstep harness is outside Task 7's scope (which is pinned to the
  specific no-stimulus 200,000-cycle command).
