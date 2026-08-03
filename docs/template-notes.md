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
- **Hardware:** Analogue Pocket running firmware 2.5.
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
