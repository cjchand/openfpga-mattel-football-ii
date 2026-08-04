# Mattel Football II — Analogue Pocket openFPGA Core

A from-scratch Analogue Pocket core for Mattel Electronic Football II
(1978), built on the Rockwell MM77LA (PPS-4/1 family) CPU. Companion
project to [`cjchand/openfpga-mattel-football`](https://github.com/cjchand/openfpga-mattel-football)
(Football I) — this is a new CPU core, not an extension of that one;
Football II runs on fundamentally different silicon. See
`docs/initial-plan.md` for the full architecture writeup.

## Installation

1. You need your own legally-obtained dump of the Football II ROM
   (`b8000-12`, 1536 bytes). This project cannot include or distribute
   one. Verify your dump against the hash in `docs/initial-plan.md` §1.
2. Copy the entire contents of `dist/` onto your Analogue Pocket's SD
   card root. Don't rename anything; the core's folder must stay named
   exactly `Cores/cjchand.Mattel Football II` or the Pocket will fail to
   load it ("Load error in 'core': General Error").
3. Place your ROM dump on the SD card at
   `Assets/mattel_fb_ii/common/b8000-12.bin` (create the
   `mattel_fb_ii` and `common` folders if they don't already exist;
   the filename must be exactly `b8000-12.bin` per this core's
   `data.json` dataslot definition).
4. Boot the core from the Pocket's core list.

## Controls

| Function | Button |
|---|---|
| Move | D-pad (Up/Down/Left/Right) |
| Score | Top face button (or Start) |
| Kick | Bottom face button |
| Status | Left face button (or Select) |
| Pass | Right face button |

## Core settings

Both are in the Pocket's core settings menu and persist between sessions.

| Setting | Default | Effect |
|---|---|---|
| **Presentation** | On | Draws the scoreboard/field bezel behind the LEDs. Off falls back to a plain black background. Purely cosmetic — LED state is identical either way. |
| **PRO 2 (Hard)** | Off | The original handheld's PRO 1 / PRO 2 skill switch. On the real device this is a switch wired straight to a CPU pin (DIO10); the ROM releases that pin and tests it, branching on the result. Off is PRO 1. |

## Gameplay

Mattel Electronic Football II is a two-player (or single-player vs. a
simple AI) football game played entirely on a 7-digit seven-segment +
30-LED display. Move your player along the field, Kick to punt/kickoff,
Pass to throw, and use Score/Status to check the scoreboard and
down-and-distance.

There is no attract mode: the game boots straight to an idle state waiting
for **Kick** to start the kickoff. A typical opening looks like:

| You press | You should see |
|---|---|
| **Score** | `0` / `15.0` / `0` — home score, time remaining, visitor score |
| **Kick** | the kickoff; the ball travels, then the game waits for you to run |
| **D-pad** | the play runs and the clock starts counting down from 15.0 |
| **Status** | `1` / `⌐xx` / `10` — down, field position, yards to go |

The field blanks while Score or Status is held; that is how the original
behaves. Per the manual the RUN keys are only meaningful mid-play, not as a
first action.

The core renders the original LED segments/lamps over a bitmap
scoreboard-and-field bezel (label bars, digit windows, and a 10-column
field strip with endzones) that approximates the real handheld's plastic
overlay. Its palette and proportions are matched to the Football I core,
which was tuned against the real hardware.

## Development

Requires Verilator and a C++17 compiler; the bitstream build additionally
needs Docker (it runs Quartus in a container).

```sh
make -C sim test      # full test suite
make bitstream        # Quartus build, ~7 min
make package          # stage the bitstream into dist/
```

Correctness is anchored to MAME, which is the closest thing to
documentation this chip has. The CPU, the display pipeline, and the
difficulty switch are each checked against it directly rather than against
this project's own assumptions:

| Target | What it checks |
|---|---|
| `mame-parity-test` | 200,000 retired instructions against register traces captured from MAME, across 9 input scenarios |
| `display-parity-test` | all 110 display cells against MAME's `pwm_display` brightness levels |
| `pro-switch-test` | the ROM's own difficulty branch, driven directly |
| `core-top-test` | the APF glue layer: reset gating, clock-domain crossing, ROM delivery, button map |
| `vectors-test` | the RTL against the C++ golden model, cycle by cycle |

The two implementations of the CPU (`src/pps41_core.v` and
`sim/golden/mm77la_model.cpp`) are deliberately kept in lockstep, but note
that they are **not** independent evidence of correctness — the RTL is a
port of the golden model, so they share any mistake by construction. That
is why the MAME comparisons exist. `docs/kick-tone-lockup-investigation.md`
is worth reading before debugging anything here: three CPU bugs hid behind
exactly that false confidence.

## Credits

All ISA/architecture facts in `docs/initial-plan.md` are transcribed from
MAME's `src/devices/cpu/pps41/*` (BSD-3-Clause, copyright hap) and the
`handheld/hh_pps41.cpp` driver's `mfootb2` machine definition. MAME is the
closest thing to authoritative documentation for this chip — no Rockwell
datasheet for the B8000/MM77LA is known to exist.

## License

This project's own code (RTL, testbenches, tooling) is available under
the MIT license. The vendored `open-fpga/core-template` scaffolding
(`src/fpga/`, parts of `dist/`) ships no upstream license file — Analogue's
developer-program terms govern its use instead. See `docs/template-notes.md`
for the exact vendored commit/tag.
