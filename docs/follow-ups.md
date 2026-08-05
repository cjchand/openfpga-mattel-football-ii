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

## 3. The SD card's core folder name does not match `dist/`

`make package` stages into `dist/Cores/cjchand.Mattel Football II/`, which
matches `core.json`'s `author` + `shortname` and is what the README tells a
new user to copy. But the folder that actually boots on the development
Pocket is `Cores/cjchand.Mattel_Football_II/` (underscores), created by an
earlier session, and that is where every recent bitstream has been hand
copied.

Both spellings appear to work — the sibling FB1 core boots fine from
`Cores/cjchand.Mattel Football` with a space — so this is a tidiness and
reproducibility problem, not a functional one. It does mean a `dist/` copy
onto that card creates a SECOND core entry rather than updating the one in
use.

Not fixed unattended because renaming the live folder while the owner is
away from the device would leave them unable to boot if the assumption is
wrong. Worth resolving before any release: pick one spelling, make
`make package` and the SD card agree, and delete the other.
