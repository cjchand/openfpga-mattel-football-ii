# Follow-ups

Deferred work, with the measurements behind each so it can be picked up
without re-deriving anything. Nothing here is blocking; the core plays the
game (see `sim/golden/gameplay_test.cpp`).

---

## 1. ROM/bitmap memories are built from logic, not block RAM

**Deferred deliberately** — the core works, timing is clean, and this
changes memory read timing, which is not something to ship without a
hardware test.

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

So the largest consumer in the design is a 16 Kbit ROM buffer, and the
device's memory blocks are essentially unused.

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

## 2. `mame-parity-test` cannot catch a `c_in` behavioural bug

`golden/mame_parity_test.cpp` builds its trace line by recomputing
`c_in` as `s.c_delay ? s.prev_c : s.c`, because the model publishes `c_in`
at the *top* of the following `step()`. That means the logged value is
derived by the test rather than read from the model.

Consequence, verified by reintroducing the bug: replacing the carry commit
with `st_.c_in = st_.c;` (no delay at all) leaves **`mame-parity-test`
passing** and `gameplay-test` passing. Only the unit tests in
`mm77la_model_test.cpp` catch it
(`test_ac_carry_is_not_visible_until_two_instructions_later`,
`test_back_to_back_ac_uses_the_older_carry`).

A wrong `c_in` that is never consumed before being corrected does not
change the instruction stream, so the digest is unchanged.

**Fix, if wanted:** move `step()`'s carry commit to the end of the step,
as MAME's `execute_run()` does. Then `state().c_in` after `step()` is
exactly what the next instruction will observe, the tracer can log it
directly, and the parity digest genuinely covers carry semantics.

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
(`0x35E`) is never hit by idle, any of the eight buttons, or randomised
fuzzing (40 trials x 3M steps), and MAME with the switch genuinely flipped
produces byte-identical traces over 30s of play. The entry is reached only
via a `TR`-prefixed long jump, which unidasm's static per-page dump cannot
resolve — so finding it needs either a stateful disassembly pass or
interactive play in MAME.

---

## 5. Post-reception lockup (blocked on hardware)

Not reproducible in simulation by any means tried. `src/debug_probe.v` is
in the shipped bitstream and will latch the PC and paint it across the top
of the screen if the tone sticks for ~2s or the CPU halts. Needs a photo of
that strip from a real device. Decode is in the probe's header comment.
