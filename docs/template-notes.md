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

Do not upgrade Quartus device settings in the .qsf — preconfigured for the
Pocket's Cyclone V.

## Actual layout observed (Step 1 inspection, matches brief's expectation)

`src/fpga/` contains exactly one `.qpf` (`ap_core.qpf`), a matching `.qsf`,
an `apf/` framework directory, and a `core/` directory — matching the
brief's expected layout exactly. No deviation requiring a stop-and-record
detour was needed.
