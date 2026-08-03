# Phase 5: Core identity, branding, and default input remap

## Context

The APF integration sub-project (`docs/superpowers/plans/2026-08-03-apf-integration.md`,
tracked in `.superpowers/sdd/2026-08-03-apf-integration/`) got the core to a
compiling, packaged bitstream that boots on real hardware (Task 8, steps
1-3 complete; step 4, the physical boot-and-play checkpoint, is blocked on
the user having a physical Pocket + SD card available).

While that hardware step is pending, this phase closes a different gap:
the core is still branded as the upstream `open-fpga/core-template`
placeholder ("Developer" / "Core Template" / "Displays gray test screen"),
and the default button mapping doesn't reflect the game's real button
layout. Neither of these blocks compiling or booting, but both block this
being an actually-identifiable, correctly-playable Football II core.

This phase is scoped narrowly on purpose. Two related gaps were identified
during brainstorming and explicitly deferred to their own future phases,
because bundling them here would couple fast, low-risk config/rewiring
work to open-ended research or a large subsystem port:

- **Bezel/overlay art.** The sibling FB1 project's "bezel" is a full
  procedural-rendering subsystem (a Python geometry-measurement tool, ROM
  bitmap generation, and a ~250-line RTL compositor), validated against
  real hardware after an earlier simpler approach (relying on the Pocket's
  scaler to stretch a mismatched-aspect buffer) caused visible distortion
  — see FB1's `docs/verification.md`, "Bezel overlay hardware bring-up."
  Porting that pattern to FB2's different segment/matrix layout (10
  segments vs FB1's 9, plus an added yard-marker column) is real,
  self-contained RTL work deferred to its own phase.
- **PRO1/PRO2 difficulty switch + "Original Controls" toggle.** MAME's
  model reads the difficulty switch via a `read_d()` callback on `IN.1`
  (D-bus bit 0x400 / DIO11), but `pps41_core.v` has no D-bus-read path
  modeled at all today (only D-bus *output*, for digit/LED select) — this
  was flagged as open risk #6 in `docs/initial-plan.md` and never
  resolved. Wiring PRO1/PRO2 means first researching MAME's
  `pps41`/`pps41base`/`mm78la`/`mm77la` device source to find the
  opcode/mechanism involved (not visible in the base PPS-4/1 class), which
  is open-ended research, not config work. Separately, FB2's `IN.0` input
  map has no overloaded buttons the way FB1's original hardware did (every
  function — Up/Down/Left/Right/Kick/Pass/Status/Score — already gets its
  own dedicated line), so there's no clear "Original Controls" remap to
  build in the first place; that determination is folded into the same
  future phase alongside the PRO1/PRO2 work.

## Goals

1. Rebrand the core's identity (openFPGA manifests + packaging paths) from
   the upstream template placeholder to this project's real identity,
   mirroring the sibling FB1 project's established convention exactly.
2. Fix the default face-button mapping to reflect the real device's
   button layout, per user direction during brainstorming.
3. Add a project README.

## Non-goals (deferred to future phases)

- Bezel/overlay rendering.
- PRO1/PRO2 difficulty switch (requires new D-bus-read RTL + MAME
  research).
- Any "Original Controls" remap toggle.
- Custom icon/platform artwork (the icon and platform placeholder image
  are unchanged from the vendored template in FB1 too — not part of that
  project's branding convention either, so out of scope here).

## Design

### 1. Identity/branding rename

Mirrors FB1's `dist/Cores/cjchand.Mattel Football/` convention exactly
(confirmed by reading FB1's actual `core.json` on disk).

- **Rename** `dist/Cores/Developer.Core Template/` →
  `dist/Cores/cjchand.Mattel Football II/`. This folder name is
  load-bearing: the Analogue Pocket requires it to exactly match
  `<author>.<shortname>` from `core.json` or the core fails to boot
  ("Load error in 'core': General Error" — see FB1's own
  `template-notes.md`-equivalent finding, and this project's own
  `docs/template-notes.md` warning about exact-match constraints).
- **`core.json`** (inside the renamed folder), only the `metadata` block
  changes:
  - `shortname`: `"Mattel Football II"`
  - `description`: `"Mattel Electronic Football II (1978) for the Analogue Pocket."`
  - `author`: `"cjchand"`
  - `url`: this repo's GitHub URL
  - `version`: `"1.0.0"`
  - `date_release`: today's date at time of implementation
  - `platform_ids`: `["mattel_football_ii"]`
  - Everything else in `core.json` (framework/cores blocks) is unchanged —
    it already matches the real hardware requirements from Task 6/8.
- **`dist/platforms/ex_platform.json`** → renamed to
  `dist/platforms/mattel_football_ii.json`, contents changed to:
  ```json
  {
    "platform": {
      "category": "Handheld",
      "name": "Mattel Football II",
      "year": 1978,
      "manufacturer": "Mattel Electronics"
    }
  }
  ```
- **`dist/platforms/_images/ex_platform.bin`** → renamed to
  `dist/platforms/_images/mattel_football_ii.bin`. Content is unchanged
  (FB1's equivalent file is also just the generic vendored placeholder
  image, same byte size as this project's — customizing this image is not
  actually part of FB1's convention, so it stays out of scope here too).
- **`dist/Cores/cjchand.Mattel Football II/info.txt`**: replace the
  template's placeholder text ("Example Core - Core Template...") with
  real project text (short description + controls summary, consistent
  with the README).
- **`Makefile`**: update `RBF_R_DEST`'s default value to point at the
  renamed folder (`dist/Cores/cjchand.Mattel Football II/bitstream.rbf_r`)
  — this is the only reference in the file (verified via grep).
- **No changes** to `input.json`, `variants.json`, `audio.json` — these
  already match FB1's convention (empty/default) and don't need custom
  controller definitions since input is handled via direct `cont1_key`
  wiring in `core_top.v`, not a declared controller schema.

### 2. Default face-button remap

Per user direction, the real device's button layout (via the top/
bottom/left/right face buttons around Pocket's D-pad) should be:

| Function | Current wiring | New wiring |
|---|---|---|
| Score | `cont1_key[15]` (Start) only | `cont1_key[6]` (face_x, top) `\| cont1_key[15]` (Start) |
| Kick | `cont1_key[4]` (face_a, right) | `cont1_key[5]` (face_b, bottom) |
| Status | `cont1_key[14]` (Select) only | `cont1_key[7]` (face_y, left) `\| cont1_key[14]` (Select) |
| Pass | `cont1_key[5]` (face_b, bottom) | `cont1_key[4]` (face_a, right) |
| D-pad Up/Down/Left/Right | unchanged | unchanged |

Select/Start are kept as redundant alternates for Status/Score
respectively (matches the existing pattern already present in the code,
and FB1's own `din_st`/`din_sc` OR-with-Start/Select precedent).

This only touches the `p_input_w` combinational assignment in
`src/fpga/core/core_top.v` (~line 519-528) — no changes to
`pps41_core.v`, no new CPU semantics, no new ports.

### 3. README.md

New `README.md` at repo root, following FB1's structure: install steps
(copy `dist/` to SD card, source your own ROM dump per the hash in
`docs/initial-plan.md`), controls (the new default mapping from the table
above), gameplay summary, credits (MAME/hap as the architecture reference,
per `docs/initial-plan.md`'s existing attribution), and a license section
(project code license + Analogue developer-program terms for the vendored
`open-fpga/core-template`, matching FB1's license section exactly since
the legal posture is identical).

## Testing

- No RTL testbenches are affected by the button remap in a way that
  requires new tests: the existing Verilator lockstep testbench drives
  `p_input` directly as raw bits, not through `cont1_key` semantics, so
  its coverage is unaffected by which physical Pocket button maps to
  which bit. Confirm by re-running the existing test suite
  (`make -C sim test` or equivalent) after the `core_top.v` change to
  ensure nothing in the build broke.
- Manual verification: `make bitstream && make package` still succeeds
  (folder rename didn't break the packaging path), and the resulting
  `dist/Cores/cjchand.Mattel Football II/` folder contains all the
  expected manifest files with the new content.
- No hardware boot test is required for this phase specifically (it
  doesn't change any RTL behavior beyond a static bit-remap), but the
  next time hardware testing does happen (Task 8 step 4, whenever the
  user has the Pocket available), it will naturally exercise the new
  branding (folder name / on-screen identity, if the Pocket's menu shows
  `shortname`) and the new button mapping.

## Risks / open questions

- None identified that block implementation. The one thing to double
  check during implementation: confirm there are no *other* hardcoded
  references to `"Developer.Core Template"` or `"Example Platform"` /
  `ex_platform` beyond the ones already found (`Makefile`,
  `dist/platforms/ex_platform.json`, the `dist/Cores/Developer.Core
  Template/` tree itself) — a repo-wide grep at implementation time is
  cheap insurance.
