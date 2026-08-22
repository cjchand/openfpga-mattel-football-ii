# Mattel Football II — Analogue Pocket openFPGA Core

A from-scratch Analogue Pocket core for Mattel Electronic Football II
(1978), built on the Rockwell MM77LA (PPS-4/1 family) CPU. Companion
project to [`cjchand/openfpga-mattel-football`](https://github.com/cjchand/openfpga-mattel-football)
(Football I) — this is a new CPU core, not an extension of that one;
Football II runs on fundamentally different silicon. See
`docs/initial-plan.md` for the full architecture writeup.

> Disclosure: Claude was used to implement this project. Also, while I work in the software
> industry, FPGA programming and programming for the Pocket in particular are entirely
> new to me. I would love any feedback on where this project violates best practices, norms,
> or otherwise can be made "better".

## Installing on your Analogue Pocket

No building required — every release ships a ready-to-run core. All you need
is the release zip and your own dump of the game ROM.

1. **Download the core.** Go to the
   [latest release](https://github.com/cjchand/openfpga-mattel-football-ii/releases/latest)
   and download the `cjchand.Mattel_Football_II_v*.zip` file, then unzip it.
   (Every version is listed on the
   [releases page](https://github.com/cjchand/openfpga-mattel-football-ii/releases).)
2. **Copy it onto your Pocket's SD card.** Copy everything inside the
   unzipped folder (`Cores`, `platforms`, `assets`, `icon.bin`) onto the root
   of your Pocket's SD card, merging into the folders that are already there
   (say "merge" / "yes to all" if your OS asks — don't replace the whole
   folder). **Don't rename anything** — the core's folder must stay named
   exactly `Cores/cjchand.Mattel Football II`, spaces and all, or the Pocket
   will refuse to load it ("Load error in 'core': General Error"). The Pocket
   requires that folder name to match `core.json`'s author and shortname
   character for character.
3. **Add the game ROM yourself — it's not included.** This project can't ship
   the Mattel Electronic Football II ROM (it's copyrighted), so you'll need to
   supply your own dump: 1536 bytes, CRC32 `5b65fc38`, SHA1
   `4fafc9deb5609b16f09b18b7346ea96ffe8bf9e0` — the `b8000-12` file from the
   MAME `mfootb2` romset. Rename it to `b8000-12.bin` and place it on the SD
   card at:
   `Assets/mattel_fb_ii/common/b8000-12.bin`
   (create the `mattel_fb_ii` and `common` folders if they don't already
   exist; the filename must be exactly `b8000-12.bin`, per this core's
   `data.json` dataslot definition).
4. **Eject the SD card, put it back in your Pocket, and boot the core** from
   the core list — it appears under the "Mattel Football II" platform,
   category Handheld. See [Controls](#controls) below for the button map, and
   [Core settings](#core-settings) for the presentation and difficulty
   options (long-press the Pocket's menu button while the core is running).

### Local Build

Prefer to build it yourself? See [Development](#development) — but note the
`dist/` folder in this repo is only updated when a bitstream is rebuilt, so
the release zip is the authoritative download.

## Controls

The original handheld has eight keys wired to a single CPU input port. Each
one maps to a Pocket button below; the "CPU bit" column is the bit position
in that port, which matches MAME's `mfootb2` `IN.0` definition and is what
the testbenches and `docs/` refer to.

| Handheld key | Pocket button | CPU bit | What it does |
|---|---|---|---|
| **RUN ←** | D-pad Left | 7 | Runs the ball carrier left |
| **RUN →** | D-pad Right | 3 | Runs the ball carrier right |
| **RUN ↑** | D-pad Up | 2 | Moves up a lane |
| **RUN ↓** | D-pad Down | 6 | Moves down a lane |
| **KICK** | B (bottom face) | 4 | Kickoff, punt, or field-goal attempt, depending on game state |
| **PASS** | A (right face) | 5 | Throws a pass |
| **STATUS** | Y (left face) **or** Select | 1 | Hold to show down / field position / yards to go |
| **SCORE** | X (top face) **or** Start | 0 | Hold to show home score / time remaining / visitor score |

Notes:

- To attempt a field goal kick take two steps back, then press the KICK button
  (yes, totally not intuitive).
- **The field blanks while STATUS or SCORE is held.** That is how the real
  device behaves — the display is a multiplexed matrix and those keys take
  it over — not a bug in the core.
- **The RUN keys only do something mid-play.** Per the original manual they
  are not a valid first action; from idle, the game is waiting for KICK.
- Start/Select are duplicates of SCORE/STATUS, added for comfort. The core
  sees them as exactly the same bit, so holding either works.
- The Pocket's L/R triggers and the analog stick are unused.

## Reporting a lockup

If the game freezes — a tone that will not stop, or the display stuck and
ignoring buttons — **the core will usually tell you why, on screen.**

There is a diagnostic recorder built into the bitstream. It stays completely
invisible during normal play and arms itself only when something has actually
gone wrong: a tone running continuously for about two seconds (no real sound
effect in this game lasts even one), or the CPU's program counter not moving
for half a second. When it fires, it freezes what it caught and paints a row
of black-and-white blocks across the very top of the screen.

**That band is the bug report.** It encodes what went wrong and exactly where
the CPU was, and it is far more useful than any description of the symptoms.

To report it:

1. **Take a photo of the whole screen**, straight on, with the band along the
   top clearly readable. Any phone camera is fine — the blocks just need to
   be distinguishable as light or dark. Don't reset the console first; the
   band holds its capture until you do.
2. **Open an issue** at
   [`cjchand/openfpga-mattel-football-ii/issues`](https://github.com/cjchand/openfpga-mattel-football-ii/issues/new)
   and attach the photo.
3. Add whatever you can of the following. All of it is helpful, none of it is
   required — the photo is the important part:
   - what you were doing when it froze (kickoff, mid-run, after a
     touchdown, holding SCORE, …)
   - roughly how long you had been playing
   - whether a tone was sounding, and whether it was high or low
   - the **PRO 1 / PRO 2** setting, and the core version from `core.json`
   - whether it happens again if you repeat the same sequence

If the screen is blank, garbled, or dead to input with **no** band across the
top, that is a different class of fault — say so explicitly in the issue,
because it points somewhere completely different in the design.

Maintainer note: the band is 16 slots. Slot 0 is a fixed marker (so the
photo's alignment can be checked), slots 1-2 are which condition fired, slots
3-4 are sticky flags, and slots 5-15 are the captured program counter,
most-significant bit first. `src/debug_probe.v` has the authoritative decode,
and `docs/kick-tone-lockup-investigation.md` works through a real example
end to end.

## Core settings

Both are in the Pocket's core settings menu and persist between sessions.

| Setting | Default | Effect |
|---|---|---|
| **Presentation** | On | Draws the scoreboard/field bezel behind the LEDs. Off falls back to a plain black background. Purely cosmetic — LED state is identical either way. |
| **PRO 2 (Hard)** | Off | The original handheld's PRO 1 / PRO 2 skill switch. On the real device this is a switch wired straight to a CPU pin (DIO10); the ROM releases that pin and tests it, branching on the result. Off is PRO 1. |

## Gameplay

Mattel Electronic Football II is a two-player (or single-player vs. a
simple AI) football game played entirely on a 7-digit seven-segment +
30-LED display. See **Controls** above for the key map.

There is no attract mode: the game boots straight to an idle state waiting
for **Kick** to start the kickoff. A typical opening looks like:

| You press | You should see |
|---|---|
| **Score** | `0` / `15.0` / `0` — home score, time remaining, visitor score |
| **Kick** | the kickoff; the ball travels, then the game waits for you to run |
| **D-pad** | the play runs and the clock starts counting down from 15.0 |
| **Status** | `1` / `⌐xx` / `10` — down, field position, yards to go |

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
| `core-rom-lockstep-test` | 1.2M cycles of the real ROM, RTL vs golden — run **twice**, at `ce=1` and at the device's real ~129:1 clock-enable ratio |
| `gameplay-test` | that the game is actually *playable*: the opening sequence, decoded off the display |
| `tone-stuck-fuzz` | randomised, human-plausible button traffic against the invariant that no tone ever runs for 2 seconds |

Two of those deserve a note, because they exist for reasons that were
learned the hard way.

`core-rom-lockstep-test` runs at both clock ratios because **a testbench that
holds `ce` high every clock is not testing the design that gets
synthesised**. At `ce=1`, one clock equals one instruction, so an effect
applied once and an effect applied every clock are indistinguishable. A bug
where each `IOS` was applied ~129 times passed 1.2M cycles at `ce=1` and
shipped; it diverges at cycle 220520 at the real ratio.

`tone-stuck-fuzz` asserts on the golden model the same invariant the
on-hardware `debug_probe` checks, which is what let the two be cross-read:
35 hours of simulated chip time found nothing, which is what established
that the hardware fault was in the RTL's clock-enable handling rather than in
the CPU semantics or the ROM.

### Debugging on hardware

`src/debug_probe.v` is the on-screen flight recorder described under
**Reporting a lockup** above. Its header has the authoritative slot decode;
`docs/kick-tone-lockup-investigation.md` works through a real capture, from
photographed band to root cause.

### FPGA resources

5,227 / 18,480 ALMs (28%), 4 M10K blocks, timing clean in every corner. The
game ROM lives in block RAM; see `docs/follow-ups.md` for the remaining
(deferred) win in the bitmap ROMs.

The two implementations of the CPU (`src/pps41_core.v` and
`sim/golden/mm77la_model.cpp`) are deliberately kept in lockstep, but note
that they are **not** independent evidence of correctness — the RTL is a
port of the golden model, so they share any mistake by construction. That
is why the MAME comparisons exist. `docs/kick-tone-lockup-investigation.md`
is worth reading before debugging anything here: three CPU bugs hid behind
exactly that false confidence.

Deferred work is tracked in `docs/follow-ups.md`.

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
