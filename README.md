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
   `Assets/mattel_football_ii/common/b8000-12.bin` (create the
   `mattel_football_ii` and `common` folders if they don't already exist;
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

## Gameplay

Mattel Electronic Football II is a two-player (or single-player vs. a
simple AI) football game played entirely on a 7-digit seven-segment +
30-LED display. Move your player along the field, Kick to punt/kickoff,
Pass to throw, and use Score/Status to check the scoreboard and
down-and-distance.

The core renders the original LED segments/lamps over a bitmap
scoreboard-and-field bezel (label bars, digit windows, and a 10-column
field strip with endzones) that approximates the real handheld's plastic
overlay. This is purely cosmetic — the underlying LED state is unchanged.
A **Presentation** toggle in the core's settings menu (on by default) lets
you switch it off, which falls back to the plain black background used
before the bezel was added; the LED digits and field lamps keep working
identically either way.

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
