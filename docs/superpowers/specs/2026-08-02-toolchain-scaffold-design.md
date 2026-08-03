# Toolchain Scaffold Design: openFPGA core-template vendoring

Companion spec to `docs/initial-plan.md` and the Phase 1-3 specs. First of
two sub-projects making up Phase 4 (CPU core → I/O peripherals → display
pipeline → **APF/openFPGA integration** → bezel/packaging). This sub-project
proves the Quartus/Docker/Pocket toolchain works end-to-end with a
do-nothing placeholder core; the following sub-project ("APF integration")
wires in the real Football II CPU, display, and audio logic on top of the
scaffold this one builds.

**Explicit reuse, not a fresh derivation:** the sibling
`cjchand/openfpga-mattel-football` (Football I) project already vendored
`open-fpga/core-template` and proved this exact Docker/Quartus/Pocket
pipeline in its own `toolchain-scaffold` plan
(`../mattel-football/docs/superpowers/plans/2026-07-25-toolchain-scaffold.md`).
This sub-project follows that plan's Tasks 2-4 structure directly (Task 1,
a Verilator toolchain proof, is skipped — Verilator has already been proven
extensively across FB2's Phases 1-3). Per `docs/initial-plan.md` §10's own
guidance, the openFPGA scaffolding (core-template, Makefile conventions,
dist/ packaging) is the one thing that legitimately carries over mechanically
between the two sibling projects, unlike CPU/display logic which does not.

## Scope

Vendor `open-fpga/core-template` into this repo, prove the Docker-hosted
Quartus build produces a working bitstream from the template's own
placeholder core (no Football II logic), reverse and package it per
Analogue's SD-card layout, and boot-test it on real Analogue Pocket
hardware. Completion criterion is a real, physical boot — the strongest
verification available, and the one FB1 used for the same milestone.

**Explicit non-goal:** any Football-II-specific HDL (`ce_gen`, `rom_loader`,
audio/video/button wiring, `core_top.v` integration with `pps41_core.v`).
This sub-project's own "core" is the vendored template's stock placeholder —
proving the pipe works before any real logic is at stake. That real
integration work is the next sub-project's job.

## 1. Repo layout

```
src/
  fpga/                    # vendored from open-fpga/core-template's src/fpga/
    <name>.qpf               # Quartus project file (exact name discovered at vendor time)
    <name>.qsf                # Quartus settings file, preconfigured for the Pocket's Cyclone V
    apf/                        # openFPGA APF framework directory (vendored, not hand-written)
    core/                         # where all later Football-II HDL gets instantiated
    output_files/                  # Quartus build output, .rbf lands here
dist/                       # vendored from the template's own dist/ (APF JSON manifests)
  Cores/<author>.<shortname>/       # constructed at package time, not shipped by the template
tools/
  reverse_rbf.py             # RBF byte-reversal for the Pocket's .rbf_r loader format
                              # (write fresh for FB2, or adapt FB1's if simple/generic enough
                              # to reuse verbatim — check before rewriting from scratch)
docs/
  template-notes.md            # single source of truth for vendored paths/facts (see §2)
Makefile
  bitstream                      # runs quartus_sh --flow compile in Docker
  package                          # stages dist/ into an SD-card-ready layout
```

## 2. Vendoring and fact-recording discipline

Same discipline FB1 established: the template's actual on-disk layout
(exact `.qpf` filename, RBF output path, `dist/` manifest locations) is
**discovered by inspection at vendor time**, not assumed from FB1's own
values — `open-fpga/core-template` may have changed since FB1 vendored it,
and even if unchanged, guessing invites exactly the kind of `dist/`
root-vs-manifest confusion FB1's own Task 4 had to correct (see FB1's
`docs/template-notes.md`: the template keeps `core.json`/`video.json`/etc.
at the **repository root**, not under `dist/` — there is no `Cores/` folder
shipped upstream at all; that staging directory is something the packager
constructs).

`docs/template-notes.md` records, filled with real observed values, not
placeholders: vendored commit hash, Quartus project file path, RBF output
path, the full set of root-level manifest files and where they actually
live upstream, and the exact SD-card folder-name requirement (see §4).

## 3. Docker/Quartus toolchain

Reuse FB1's already-proven, already-pulled image:
`didiermalenfant/quartus:22.1-apple-silicon` (confirmed present locally).
Verify it still runs (`quartus_sh --version`) before vendoring — image
staleness or a host Docker/Rosetta config change since FB1's original proof
is exactly the kind of thing to check empirically rather than assume still
works. If the image has become unusable, FB1's plan names a fallback
(`raetro/quartus:18.1`, same CLI) to fall back to.

## 4. Packaging and the SD-card folder-name requirement

**Non-obvious, hardware-confirmed detail carried over from FB1's own boot
testing** (`../mattel-football/docs/template-notes.md`'s "Hardware boot
test findings"): the SD-card folder under `/Cores/` must be named **exactly**
`<author>.<shortname>` from `core.json`, byte-for-byte — a mismatch produces
`"Load error in 'core': General Error"` on real hardware, not a clear
diagnostic. Get this exact from the start; don't discover it via a failed
boot.

Real Football II branding (author/shortname identity in `core.json`,
platform JSON, icon) is **deliberately deferred** to a later
artwork/packaging phase, mirroring FB1's own "Plan 5" deferral — this
sub-project keeps the template's stock placeholder identity
(`Developer`/`Core Template` or whatever the current upstream template
ships) through to a successful boot, then hands off. Renaming identity is a
trivial, low-risk change to make once the harder toolchain-proof work is
done; doing it now would just be extra churn if anything about the vendor
step needs re-doing.

## 5. Test harness & completion criteria

No golden-model/RTL lockstep verification in this sub-project — it's pure
toolchain plumbing, not CPU logic. Completion criteria, in order:

1. `quartus_sh --version` succeeds inside the Docker container.
2. `open-fpga/core-template` vendored, `docs/template-notes.md` filled with
   real observed facts.
3. `make bitstream` completes a full Quartus flow (synthesis, fit,
   assembler) against the template's own placeholder core with zero errors,
   producing a nonzero-size `.rbf`.
4. The `.rbf` is reversed to `.rbf_r`, staged into
   `dist/Cores/<author>.<shortname>/` per `core.json`'s declared filename,
   and `make package` produces an SD-card-ready `dist/` layout.
5. **Real boot test on physical Analogue Pocket hardware**: the packaged
   core loads without error. This is the sub-project's actual completion
   criterion, not the bitstream compiling — a bitstream that compiles but
   fails to boot (wrong folder name, wrong manifest, bad RBF reversal) is
   not done.

## Open risks

- No confirmed guarantee `open-fpga/core-template`'s current HEAD matches
  the layout FB1 vendored (§2's discovery-not-assumption discipline exists
  specifically because of this).
- Docker/Quartus-on-Apple-Silicon-via-Rosetta is inherently a slightly
  fragile toolchain (FB1's own plan already names a fallback image) —
  treat any segfault/hang as an environment issue to diagnose, not
  necessarily a project bug.
- Real hardware/firmware behavior (the exact folder-naming requirement,
  boot error messages) is confirmed only against the firmware version FB1
  tested against (2.5) — worth re-confirming if boot behavior looks
  different on a different firmware version.
