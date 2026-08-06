# Follow-ups

Deferred work, with the measurements behind each so it can be picked up
without re-deriving anything. Nothing here is blocking; the core plays the
game (see `sim/golden/gameplay_test.cpp`).

---

## 1. ROM/bitmap memories are built from logic, not block RAM

**`rom_loader` is DONE** (2026-08-04). Registering its read took the design
from **54% to 28%** ALM utilisation: the entity went from 4,959 ALMs to
**10.5**, with its 12,288 bits now in 2 M10Ks (Quartus inferred an
`altsyncram`). Timing stayed clean in every corner. **Not yet confirmed on
hardware** — roll back by reverting that commit, or on the SD card by copying
`/_ff2_rollback/bitstream.rbf_r.known-good` over the core's `bitstream.rbf_r`.

**`field_rom`/`label_rom` are still open** — see the note at the end of this
section for why they are the riskier half.

The original measurement and reasoning follow.

### The measurement

From `src/fpga/output_files/ap_core.fit.rpt` (Fitter Resource Utilization
by Entity), for the 5CEBA4F23C8:

| Entity | ALMs | Share |
|---|---:|---:|
| `rom_loader:u_rom_loader` | 4,959 | 49% of the core |
| `video_renderer` (incl. `field_rom` 880 + `label_rom` 996) | 2,265 | 22% |
| `pps41_display_pwm:u_display_pwm` | 1,803 | 18% |
| `pps41_core:u_pps41_core` (the actual CPU) | 430 | 4% |
| **Total** | **10,081 / 18,480** | **55%** |

Meanwhile: **block memory 8,192 / 3,153,920 bits (<1%)**, 2 of 308 RAM
blocks — and those two are the APF datatable, not ours.

So the largest consumer in the design is a 12,288-bit ROM buffer
(`reg [31:0] mem [0:383]` = 1,536 bytes, the whole game ROM), and the
device's memory blocks are essentially unused. Two M10Ks would hold it.

### Why

`rom_loader` is written on `clk_74a` and read **combinationally**
(`assign rom_data = mem[rd_word_idx][...]`). Quartus cannot infer an M10K
for an asynchronous read, so it lands in MLAB/logic. `field_rom` and
`label_rom` have the same shape (`$readmemh` array, combinational read).

### The proposed change (rom_loader first)

Register the read on `clk_core_12288`. This is the low-risk one because
the CPU only samples `rom_data` on a `ce` pulse, and `ce` fires roughly
every 130 core clocks (~95 kHz from 12.288 MHz). `pc_reg` changes on the
`ce` edge, so a registered read has ~129 clocks of margin before the next
`ce` needs the data — the latency is absorbed with enormous headroom.

Expected saving ~4,959 ALMs, taking utilization from 55% to roughly 28%.

Existing coverage that would catch a mistake: `core-top-test`'s ROM fetch
check (200,000 consecutive CPU fetches compared against the file),
`vectors-test`'s RTL-vs-golden lockstep, and `gameplay-test`.

**`field_rom`/`label_rom` are a separate, riskier step** (~1,900 ALMs):
`video_renderer` reads them once per pixel, so registering them needs a
compensating pipeline delay or the image shifts one pixel horizontally —
the kind of defect that is easy to miss in simulation and obvious on
hardware.

---

## 2. ~~`mame-parity-test` cannot catch a `c_in` behavioural bug~~ — FIXED

**Resolved.** `step()`'s carry commit moved from the top of the following
step to the end of the current one, matching MAME's `execute_run()` tail.
`state().c_in` is now, immediately after `step()` returns, exactly the
value the next instruction will observe, so the parity tracer reads it
directly instead of recomputing it from `c`/`prev_c`.

Equivalence was checked by the digests: they are **unchanged**, which is
what proves the refactor did not alter behaviour. And the blind spot is
genuinely closed — removing the carry delay now fails **all 9** parity
scenarios, where it previously passed silently.

`gameplay-test` still does not catch it, which is expected and recorded in
that file: a wrong carry does not happen to break this particular opening
sequence.

---

## 3. `INT1L` (opcode `0x04`) is a flag-only no-op

Both implementations treat it as a no-op that only sets a testbench flag.
MAME's base tier implements it as a real skip (`m_skip = !m_int_line[1]`),
though the MM78LA tier leaves it as `op_todo()`.

Not urgent: measured zero occurrences of byte `0x04` in the ROM image and
zero dispatches over a 4,000,000-cycle run. It cannot affect this title.
See `docs/initial-plan.md` §9 risk 2.

---

## 4. When does the game read the PRO 1 / PRO 2 switch?

The pin is wired and honoured — `pro-switch-test` drives the ROM's own
difficulty block and shows DIO10 selecting between `LAI 3` and `LAI 4`.

What is unknown is which game state reaches that block. Its entry point
(`0x35E`) is never hit by idle, by any of the eight buttons, or by
randomised fuzzing (40 trials x 3M steps) in the golden model. The entry is
reached only via a `TR`-prefixed long jump, which unidasm's static per-page
dump cannot resolve — so finding it needs either a stateful disassembly
pass or interactive play in MAME.

**Correction:** an earlier MAME cross-check described as "30s of rich play"
was not. See the MAME tooling limitation below — the script's later presses
never fired, so it exercised only Status and Kick.

## 4a. MAME Lua gotcha: frame notifiers stop after ~175 frames

`emu.add_machine_frame_notifier` stops being called after frame ~175
(~2.9s of emulated time), regardless of `-video none`/`-video soft` and
regardless of `-seconds_to_run`. The machine keeps running — a 14s run
still reports 13 seconds and keeps retiring instructions — but no further
frame callbacks arrive, silently.

Consequences for anyone scripting MAME here:
- **Drive all inputs within the first ~175 frames**, or verify the presses
  actually landed rather than assuming.
- The long CPU-parity traces are unaffected: they press at frame 60 and
  are captured through the *debugger* (`-debugscript` + `trace`), which
  keeps working for the full run.
- Also beware `set_value()` on `PORT_CONFNAME` fields (e.g. Difficulty):
  it silently does nothing. Use `field.user_value = <value>` and read it
  back to confirm.

---

## 5. Post-reception lockup (blocked on hardware)

Not reproducible in simulation by any means tried. `src/debug_probe.v` is
in the shipped bitstream and will latch the PC and paint it across the top
of the screen if the tone sticks for ~2s or the CPU halts. Needs a photo of
that strip from a real device. Decode is in the probe's header comment.

---

## 3. ~~The SD card's core folder name does not match `dist/`~~ — FIXED

**Resolved 2026-08-05.** `make package` now stages into
`dist/Cores/cjchand.Mattel_Football_II/`, matching the folder that boots on
the device. Underscores were chosen because that is the spelling already in
use on the card and the convention across other cores.

`core.json` is deliberately left alone: its `shortname` is still
`Mattel Football II` with spaces, which is what the Pocket displays in the
core list. That combination -- underscore folder, spaced shortname -- is the
exact one that has been booting and playing on the device, so it is the
proven configuration rather than a tidier-looking guess.

---

## 4. "Hold KICK at power-on to change the speed" — partially traced

User-reported feature on the real device: holding KICK while powering on,
with PRO 1 or PRO 2, alters the game speed. Mechanism and magnitude unknown.
An overnight investigation against the golden model established the
following. **Nothing here changes the core yet** — it is a map for whoever
picks this up.

### Found: a real boot-time latch

Booting with a button held diverges from an idle boot **at step 311**, only
~300 instructions after the reset vector, at PC `0x3CE`:

```
0f:38  I2C
0f:1c  SKMEA          ; skip the next instruction if RAM == A
0f:0e  T $12          ; -> 0x3D2 when taken; skipped -> 0x3E7
```

Diffing RAM between the two boot paths at 400k / 800k / 1.2M cycles, exactly
one cell is persistently different at every checkpoint:

**RAM `0x1A` == `0x0` after an idle boot, `0xD` after booting with a button
held.** Everything else that differs (`0x1F`, `0x3A`, `0x70`-`0x75`) drifts
chaotically and is not stable across checkpoints.

### Not found: any effect on speed

- Pace was measured as the multiset of intervals between display changes
  during a kickoff, for all 8 buttons held at boot, crossed with PRO 1/2.
  All intervals stay multiples of the same 1583-cycle display window in
  near-identical proportions; the ~2% differences are phase, not rate.
- Forcing `0x1A` to each of its 16 values mid-run changes nothing
  systematic: span is 25.19-25.23s across all 16, with no trend.

So `0x1A` is latched at boot but was **not** shown to be a speed control. It
may be read only at a moment earlier than the force point, or be something
else entirely.

### Also established (a firm negative)

The PRO pin does nothing, in anything tested:

- All **12** D-input pins swept individually: zero trace divergence.
- PRO 1 vs PRO 2 compared under **all 256** boot-held button combinations,
  and under holds of 0.5s / 2s / the entire run: **identical in every case**.

This matches MAME, which likewise never reads it in play. Note MAME's own
port comment says `DIO11` while the bit it defines is `0x400` (bit 10); this
core uses bit 10, i.e. it matches MAME's *bit*, which is what matters.

### The one caveat worth chasing first

The latch responds to the **nibble**, not the button: KICK / PASS / DOWN /
LEFT (bits 4-7) all produce identical results, as do SCORE / STATUS / UP /
RIGHT (bits 0-3). If the real feature is specific to KICK, then either it is
really "any of those four", or the boot code reads P in a way this model
conflates. Worth resolving before building anything on `0x1A`.

### What would actually settle it

Ground truth from the device. Specifically: **what** visibly changes (ball
travel speed? game-clock rate? opponent advance rate?), **how much**, and
whether it is KICK alone or any of KICK/PASS/DOWN/LEFT. With one concrete
observable, the golden model can be pointed straight at it — every tool used
above is in the job scratch dir and takes minutes to re-run.
