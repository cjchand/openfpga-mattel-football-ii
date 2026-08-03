# core-template vendoring notes

- Vendored from: https://github.com/open-fpga/core-template (commit:
  `da3a021b1eaf742604d86d8dc9b33a6666263e6a`, tag `v1.3.0`)
- License: upstream ships no LICENSE file (`"license": null` per GitHub's
  repo API, confirmed 2026-08-02); Analogue's developer program terms
  govern instead. `docs/CORE_TEMPLATE_LICENSE` was not created.
- Quartus project file: `src/fpga/ap_core.qpf` (matching `src/fpga/ap_core.qsf`)
- RBF output after compile: `src/fpga/output_files/ap_core.rbf`
  (786,964 bytes as produced by this task's `make bitstream` run, dated
  2026-08-03 03:36 UTC inside the container)
  - Note: the vendored template ships this file **pre-built and tracked in
    git** (along with `ap_core.sof` and `ap_core.jdi`) — `src/fpga/output_files/.gitignore`
    contains `!*.sof` / `!*.rbf` allow-rules specifically so these placeholder
    build products are committed upstream. `make bitstream` regenerates and
    overwrites them in place. This task's rebuild reproduced essentially the
    same placeholder core (0 errors throughout: Analysis & Synthesis 159
    warnings, Fitter 16 warnings, Assembler 2 warnings, Timing Analyzer 15
    warnings; full compile 192 warnings total, all pre-existing template
    warnings unrelated to this vendoring).
- dist/ layout: top-level metadata files (`core.json`, `audio.json`,
  `data.json`, `info.txt`, `input.json`, `interact.json`, `variants.json`,
  `video.json`, `README.md`) live at the **template repo root**, not under
  `dist/` — confirms FB1's finding is still true for this commit. The
  `dist/` directory itself (vendored here) contains only:
  - `dist/assets/` (currently just a `.keep` placeholder)
  - `dist/icon.bin`
  - `dist/platforms/ex_platform.json` and `dist/platforms/_images/ex_platform.bin`
    (example/placeholder platform definition)
  The repo-root metadata files (`core.json` etc.) were **not** vendored by
  this task — only `src/fpga/` and `dist/` were copied per the brief's Step 1.
  This will need to be revisited when Task 3 (packaging) needs those
  metadata files.
- Cores/ staging directory: the packager constructs
  `dist/Cores/<author>.<shortname>/` at package time; it is not shipped
  pre-built by the template. Confirmed absent from the vendored tree.

## Task 3 update: root-level metadata files vendored

Task 3 (packaging) needed `core.json` to determine the exact
`<author>.<shortname>` folder name and RBF filename (Global Constraint:
byte-for-byte match or the core fails to boot). Those files were never
vendored by Task 2 (see above), so Task 3 fetched the same 8 root-level
metadata files directly from the exact vendored commit/tag recorded above
(`da3a021b1eaf742604d86d8dc9b33a6666263e6a`, tag `v1.3.0`) via
`raw.githubusercontent.com`, and placed them under
`dist/Cores/Developer.Core Template/` (alongside where `bitstream.rbf_r`
is staged), rather than at the repo root — this matches the openFPGA SD
card layout, where `core.json` and its sibling manifests live inside the
core's own `Cores/<author>.<shortname>/` directory, not the project root.

Files fetched and placed in `dist/Cores/Developer.Core Template/`:
`core.json`, `audio.json`, `data.json`, `info.txt`, `input.json`,
`interact.json`, `variants.json`, `video.json`.

From `core.json`, the values that determine the SD-card folder/filename:
- `metadata.author` = `"Developer"`
- `metadata.shortname` = `"Core Template"`
- folder name = `Developer.Core Template` (note: contains a literal
  space — copied verbatim from `shortname`, per the exact-match
  constraint; do not "clean up" this space)
- `cores[0].filename` = `"bitstream.rbf_r"` — this is the RBF_R
  destination filename inside that folder.

These are still the upstream template's placeholder identity (author
"Developer", shortname "Core Template") — a later task should update
`core.json`'s `metadata.author`/`metadata.shortname` to the real project
identity once one is chosen, and the `Makefile`'s `RBF_R_DEST` and the
`dist/Cores/...` directory name must be renamed to match, in lockstep,
or the core will fail to boot.

Do not upgrade Quartus device settings in the .qsf — preconfigured for the
Pocket's Cyclone V.

## Actual layout observed (Step 1 inspection, matches brief's expectation)

`src/fpga/` contains exactly one `.qpf` (`ap_core.qpf`), a matching `.qsf`,
an `apf/` framework directory, and a `core/` directory — matching the
brief's expected layout exactly. No deviation requiring a stop-and-record
detour was needed.

## Hardware boot test findings (2026-08-03)

- **Bitstream tested:** `dist/Cores/Developer.Core Template/bitstream.rbf_r`
  (786,964 bytes, reversed RBF produced by `make package` in Task 3, sourced
  from `src/fpga/output_files/ap_core.rbf` compiled in Task 2).
- **Hardware:** Analogue Pocket running firmware 2.6.
- **Test procedure:** The packaged bitstream was placed on SD card at the
  correct folder path (`/Cores/Developer.Core Template/bitstream.rbf_r`,
  matching the `Developer.Core Template` folder name from `core.json`) and
  booted on real Analogue Pocket hardware.
- **Result:** Clean boot, no error dialog or "Load error" message. Screen
  displayed a solid gray fill (RGB 60,60,60) as expected for the placeholder
  template core.
- **Expected behavior confirmed:** The gray output is correct and expected.
  The template's placeholder core hardcodes a constant gray output during
  active video: `src/fpga/core/core_top.v` lines 594–596 set
  `vidout_rgb[23:16] <= 8'd60; vidout_rgb[15:8] <= 8'd60; vidout_rgb[7:0] <= 8'd60;`
  for the entire active video region. This is the stock placeholder behavior,
  not a malfunction. The successful boot confirms end-to-end toolchain
  viability: Docker Quartus compilation, RBF-to-RBF_R reversal, correct
  SD-card folder naming, and Pocket hardware loading all functional.

## Phase 5: branding rename and button remap build verification (2026-08-03)

- **Rename (Task 1, already merged):** `dist/Cores/Developer.Core Template`
  -> `dist/Cores/cjchand.Mattel Football II`; `dist/platforms/ex_platform.json`
  -> `dist/platforms/mattel_football_ii.json`; `Makefile`'s `RBF_R_DEST`
  updated in lockstep to `dist/Cores/cjchand.Mattel Football II/bitstream.rbf_r`.
- **Button remap (Task 2, already merged):** `src/fpga/core/core_top.v`'s
  `p_input_w` default face-button mapping rewired to Score=top face button,
  Kick=bottom, Status=left, Pass=right, plus D-pad; Select/Start still work
  as alternates for Status/Score.
- **`make sim` (Verilator/golden-model test suite):** all 50 test vectors
  PASS, 0 mismatches, exit code 0. The remap only changes which `cont1_key`
  bit feeds which `p_input` bit at the `core_top.v` level; the testbench
  drives `p_input` directly and was unaffected, as expected.
- **`make bitstream` (Quartus compile in Docker):** 0 errors, 200 warnings
  total (Analysis & Synthesis 167, Fitter 16, Assembler 2, Timing Analyzer
  15). This matches the last known-good baseline (0 errors, 200 warnings,
  recorded above and in `.superpowers/sdd/2026-08-03-apf-integration/progress.md`
  Task 8) exactly, both in total count and in the set of warning ID
  categories observed. No new warning category was introduced by the
  `core_top.v` remap.
- **`make package`:** succeeded; `bitstream.rbf_r` regenerated (1,052,284
  bytes) at `dist/Cores/cjchand.Mattel Football II/bitstream.rbf_r`. `ls`
  of that folder confirms all expected manifest files present alongside
  it: `core.json`, `audio.json`, `data.json`, `info.txt`, `input.json`,
  `interact.json`, `variants.json`, `video.json`.

## Bezel/field overlay bring-up (2026-08-03)

- **Canvas:** grew the active video region from the original LED-only
  frame to 400x360, reusing FB1's already-proven 400x360/60Hz timing
  budget (same pixel clock and blanking intervals as the sibling
  Football I project) rather than deriving a new one from scratch — the
  goal was a bitmap-friendly canvas large enough for label bars, digit
  windows, and a 10-column field strip, without risking a new timing
  regression.
- **Layering:** `src/video_renderer.v` composites, back-to-front: plain
  black background -> green field margin -> field strip bitmap
  (`field_rom.v`, 10 columns + endzones + borders, `$readmemh`-loaded
  from `field_bitmap.mem`/`field_palette.mem`) -> label-bar bitmap
  (`label_rom.v`, `label_bitmap.mem`/`label_palette.mem`) -> the
  original procedural 7-segment digits and field lamps (unchanged from
  Phase 5's `display_render.v` logic, now inlined into
  `video_renderer.v`), which are drawn unconditionally on top regardless
  of what's under them. Bitmaps were generated from a photo of the real
  device by `tools/gen_bezel_bitmaps.py`.
- **`Presentation` toggle:** wired as `interact.json`'s `"Presentation"`
  checkbox variable (id 1, default on), read into `core_top.v` through
  the standard datatable/`synch_2` path as `bezel_enable`, and passed
  into `video_renderer`. Only the *background* layer selection depends
  on `bezel_enable` (`!bezel_enable` forces plain black); the digit
  segments and field lamps are drawn unconditionally either way, so
  toggling Presentation off reproduces the pre-bezel plain-black display
  with LEDs/lamps unaffected.
- **Verification performed (simulation only):** `make screenshot` against
  the real ROM (`b8000-12.bin`), holding Score from ~1 video frame after
  reset (stimulus `6333 01`, not cycle 0, to let startup settle first),
  produced a 400x360 PPM. Programmatic pixel sampling (PIL, no human
  eyeball) confirmed: label-bar background is white (`0xFFFFFF`) with
  colored label-bitmap content, digit-window gaps are white with black
  corner accents, the field strip shows green margins (`0x12CA7D`),
  cyan endzones, black column dividers, and light-gray divider lines,
  and lit digit segments render at the exact `C_DIM`/`C_BRIGHT` levels
  (`0x552200`/`0xFF8800`) driven by `levels[]`. `make startup-state-test`
  still passes unchanged (`Home 00 / Time 15.0 / Visitor 00`), confirming
  the CPU/display-pipeline wiring itself was untouched by the rendering
  work.
- **Real-hardware confirmation:** still **BLOCKED-on-human** as of this
  writing (see Task 3's report under
  `.superpowers/sdd/2026-08-03-bezel-field-overlay/task-3-report.md`,
  Step 10) — no agent has physical access to a real Analogue Pocket to
  boot the packaged bitstream and visually confirm the bezel on real
  hardware, or confirm the Presentation-off fallback there. Simulation
  results above are the only verification performed to date; this
  should be closed out by a human with hardware access before treating
  the feature as fully signed off.
