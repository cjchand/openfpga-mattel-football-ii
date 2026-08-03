# Toolchain Scaffold Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove the Quartus/Docker/Pocket toolchain end-to-end — vendor the
official openFPGA `core-template`, compile its own placeholder core into a
bitstream via Docker Quartus, package it per Analogue's SD-card layout, and
boot it on real Analogue Pocket hardware.

**Architecture:** The Verilator/C++ lockstep toolchain (native, fast) is
already proven across Phases 1-3 and untouched by this plan. This plan adds
a second, independent toolchain: an x86 Quartus container that compiles the
vendored `open-fpga/core-template` Quartus project into an RBF, which a
small Python tool bit-reverses into the RBF_R format the Pocket requires.
The two toolchains meet at a new root `Makefile` (this repo currently has
none — only `sim/Makefile`), whose `sim` target delegates to the existing
`sim/Makefile` unchanged.

**Tech Stack:** Docker (x86 emulation via Rosetta), Quartus Prime in
`didiermalenfant/quartus:22.1-apple-silicon`, Python 3 for packaging tools,
GNU Make.

**Spec:** `docs/superpowers/specs/2026-08-02-toolchain-scaffold-design.md`

## Global Constraints

- Host is macOS (Apple Silicon); Quartus runs only inside Docker with
  `--platform linux/amd64`.
- Quartus image: `didiermalenfant/quartus:22.1-apple-silicon` (already
  pulled locally, proven working by the sibling FB1 project). Fallback if
  it misbehaves: `raetro/quartus:18.1` (same CLI).
- Do not change FPGA device/part settings in the vendored `.qpf`/`.qsf` —
  preconfigured for the Pocket's Cyclone V.
- The Pocket requires RBF_R: every byte of the RBF bit-reversed.
- The SD-card `/Cores/` folder must be named **exactly**
  `<author>.<shortname>` from `core.json`, byte-for-byte — confirmed by the
  sibling FB1 project's own hardware boot testing; a mismatch produces
  `"Load error in 'core': General Error"` with no clearer diagnostic.
- Real Football II branding (author/shortname identity, artwork) is
  deliberately out of scope — keep the template's stock placeholder
  identity through to a successful boot. Renaming is a later phase's job.
- This plan does not touch any Football-II-specific HDL (`pps41_*.v`,
  `mm77la_*` golden model files) or `sim/Makefile` — it only adds a new
  root `Makefile`, vendors `src/fpga/`/`dist/`, and adds `tools/reverse_rbf.py`.
- No `Co-Authored-By` footer requirement carries over from FB1 (that was
  FB1-specific commit convention) — use this repo's own commit style,
  consistent with Phases 1-3's commits in this repo.

---

## File Structure

```
Makefile                  # NEW: root-level, sim/bitstream/package targets
src/
  fpga/                    # vendored from open-fpga/core-template
    <name>.qpf/.qsf           # exact names discovered at vendor time
    apf/                        # openFPGA framework (vendored, not hand-written)
    core/                         # placeholder this phase; Football II HDL goes here later
    output_files/                  # Quartus build output (mostly gitignored via vendored .gitignore)
dist/                      # vendored from the template's own dist/
  Cores/<author>.<shortname>/  # constructed at package time
tools/
  reverse_rbf.py             # RBF byte-reversal, adapted from FB1's (generic, no FB1-specific content)
docs/
  template-notes.md            # single source of truth for vendored paths/facts
  CORE_TEMPLATE_LICENSE          # vendored license (if the current template ships one)
```

---

### Task 1: Docker Quartus toolchain proof

**Files:**
- Modify: none unless a workaround is needed (then a new `docs/toolchain-notes.md` or similar — see Step 4)

**Interfaces:**
- Consumes: nothing.
- Produces: a verified local `didiermalenfant/quartus:22.1-apple-silicon` image and the exact `docker run` incantation Task 2 reuses.

- [ ] **Step 1: Verify Docker is installed and running**

Run: `docker info --format '{{.Architecture}} {{.OperatingSystem}}'`
Expected: prints architecture + "Docker Desktop" without error. If the
daemon isn't running, start Docker Desktop first. In Docker Desktop
settings, confirm "Use Rosetta for x86_64/amd64 emulation" is enabled
(Settings → General).

- [ ] **Step 2: Confirm the already-pulled Quartus image is present and runnable**

Run: `docker images | grep quartus`
Expected: `didiermalenfant/quartus  22.1-apple-silicon` present (already
pulled from the sibling FB1 project's earlier work). If absent, pull it:
`docker pull --platform linux/amd64 didiermalenfant/quartus:22.1-apple-silicon`.

- [ ] **Step 3: Smoke-test Quartus inside the container**

Run:
```bash
docker run --platform linux/amd64 --rm didiermalenfant/quartus:22.1-apple-silicon quartus_sh --version
```
Expected: output containing `Quartus Prime Shell` and version `22.1`. If
this segfaults or hangs, re-check Rosetta emulation; as a fallback,
substitute image `raetro/quartus:18.1` here and in later tasks (same CLI).

- [ ] **Step 4: If a workaround was needed, record it**

If Step 1-3 required any non-default configuration change or fallback
image, create `docs/toolchain-notes.md` documenting exactly what was needed
(mirrors the design spec's "record facts, don't assume" discipline). If
nothing was needed, skip — no file, no commit for this task.

```bash
git add docs/toolchain-notes.md   # only if created
git commit -m "docs: note Docker/Quartus toolchain workaround"
```

---

### Task 2: Vendor the official core-template, add the root Makefile, build a bitstream

**Files:**
- Create: `src/fpga/` (vendored from `open-fpga/core-template`)
- Create: `dist/` (vendored from the template's `dist/`)
- Create: `docs/template-notes.md`
- Create: `docs/CORE_TEMPLATE_LICENSE` (if the template ships one — see Step 1)
- Create: `Makefile` (root-level)

**Interfaces:**
- Consumes: Docker image proven in Task 1.
- Produces: `make bitstream` — compiles the Quartus project, leaves an RBF
  under `src/fpga/output_files/`; the exact path recorded in
  `docs/template-notes.md` for Task 3. `make sim` — delegates to the
  existing `sim/Makefile`'s `test` target unchanged, so Phases 1-3's full
  regression suite stays a single command from the repo root. The vendored
  `src/fpga/core/` directory is where all later Football-II HDL gets
  instantiated (out of scope for this plan).

- [ ] **Step 1: Clone and vendor the template**

Run:
```bash
git clone --depth 1 https://github.com/open-fpga/core-template /tmp/core-template-fb2
cp -R /tmp/core-template-fb2/src/fpga src/fpga
cp -R /tmp/core-template-fb2/dist dist
if [ -f /tmp/core-template-fb2/LICENSE ]; then cp /tmp/core-template-fb2/LICENSE docs/CORE_TEMPLATE_LICENSE; fi
ls src/fpga
```
Expected: `src/fpga` contains a Quartus project — exactly one `*.qpf` file,
a matching `.qsf`, an `apf/` framework directory, and a `core/` directory.
Note the sibling FB1 project found the current upstream template has **no
LICENSE file** (`"license": null` per GitHub's API) — if that's still true,
`docs/CORE_TEMPLATE_LICENSE` simply won't be created; don't treat a missing
upstream LICENSE as an error to work around.

**If the template's layout differs from this expectation** (a `.qpf`
filename other than what FB1 saw, a restructured `dist/`, etc.), stop and
record the actual observed layout in `docs/template-notes.md` (Step 2)
before proceeding — every later step in this plan refers to paths by what's
actually discovered here, not by assumption from FB1's prior vendoring.

- [ ] **Step 2: Record template facts**

Create `docs/template-notes.md`, filling every value with what was actually
observed in Step 1 (not copied from FB1's own notes — the template may have
changed since FB1 vendored it):

```markdown
# core-template vendoring notes

- Vendored from: https://github.com/open-fpga/core-template (commit: <fill
  from `git -C /tmp/core-template-fb2 rev-parse HEAD`>)
- License: <"see docs/CORE_TEMPLATE_LICENSE" if one was vendored, otherwise
  "upstream ships no LICENSE file; Analogue's developer program terms
  govern instead">
- Quartus project file: <actual path, e.g. src/fpga/ap_core.qpf>
- RBF output after compile: <actual path under src/fpga/output_files/>
- dist/ layout: <where core.json/video.json/etc. actually live upstream —
  FB1 found these at the template's repo root, not under dist/; confirm
  whether that's still true>
- Cores/ staging directory: <the packager constructs
  dist/Cores/<author>.<shortname>/ at package time; it is not shipped
  pre-built by the template>

Do not upgrade Quartus device settings in the .qsf — preconfigured for the
Pocket's Cyclone V.
```

- [ ] **Step 3: Create the root Makefile**

Create `Makefile` (repo root — this repo currently has none):

```make
# Mattel Football II openFPGA core — build entry points
# make sim       — run the full Phase 1-3 Verilator/golden-model test suite
# make bitstream — compile the Quartus project in Docker (this plan)
# make package   — bit-reverse + stage the bitstream for the Pocket (Task 3)

QUARTUS_IMAGE ?= didiermalenfant/quartus:22.1-apple-silicon
QPF           ?= <fill with the actual .qpf filename from Step 1, e.g. ap_core.qpf>

.PHONY: sim bitstream package clean

sim:
	$(MAKE) -C sim test

bitstream:
	docker run --platform linux/amd64 --rm -t \
		-v $(PWD)/src/fpga:/build -w /build \
		$(QUARTUS_IMAGE) quartus_sh --flow compile $(QPF)

clean:
	$(MAKE) -C sim clean
```

(The `package` target is added in Task 3 — leave the `.PHONY` line and
target list ready for it, or add `package` as a no-op placeholder now and
fill it in Task 3, whichever keeps this Makefile always in a working state.)

- [ ] **Step 4: Confirm `make sim` still works through the new root Makefile**

Run: `make sim`
Expected: runs the exact same output as `make -C sim test` did before this
plan — full Phase 1-3 suite passes, zero regressions. This is a pure
delegation, so any difference here means the delegation itself is broken,
not the underlying suite.

- [ ] **Step 5: Build the bitstream**

Run: `make bitstream`
Expected: Quartus flow runs synthesis/fit/assembler and finishes with
`Quartus Prime Assembler was successful` (and the earlier stage-success
lines: `Analysis & Synthesis was successful`, `Fitter was successful`), 0
errors. An `.rbf` appears under `src/fpga/output_files/`. This is slow
under emulation (tens of minutes) — normal, matches FB1's ~1m28s-to-tens-
of-minutes range depending on host load. Record the exact RBF path and
size in `docs/template-notes.md`.

- [ ] **Step 6: Commit**

```bash
git add src/fpga dist docs/template-notes.md Makefile
git add docs/CORE_TEMPLATE_LICENSE 2>/dev/null || true
git commit -m "Vendor open-fpga core-template; add root Makefile with sim/bitstream targets

make sim delegates to the existing sim/Makefile test target unchanged.
make bitstream compiles the vendored template's placeholder core via
Docker Quartus, proving the toolchain before any Football II HDL is
wired in."
```

---

### Task 3: RBF reversal and packaging

**Files:**
- Create: `tools/reverse_rbf.py` (adapted from FB1's — generic byte-reversal, no FB1-specific content, safe to reuse near-verbatim)
- Create: `tools/test_reverse_rbf.py`
- Modify: `Makefile` (add/fill the `package` target)

**Interfaces:**
- Consumes: RBF path recorded in `docs/template-notes.md` (Task 2).
- Produces: `make package` — stages `dist/` with the reversed bitstream in
  place, ready to copy onto a Pocket SD card. `tools/reverse_rbf.py
  <in.rbf> <out.rbf_r>` — a pure, project-agnostic utility, unlikely to
  need changes again for the life of this project.

- [ ] **Step 1: Write the failing test for the reversal tool**

Create `tools/test_reverse_rbf.py`:

```python
"""Bit-reversal must be an involution and match known byte mappings."""
import subprocess
import sys
import tempfile
from pathlib import Path

TOOL = Path(__file__).resolve().parent / "reverse_rbf.py"

def run(data: bytes) -> bytes:
    with tempfile.TemporaryDirectory() as d:
        src = Path(d) / "in.rbf"
        dst = Path(d) / "out.rbf_r"
        src.write_bytes(data)
        subprocess.run([sys.executable, str(TOOL), str(src), str(dst)], check=True)
        return dst.read_bytes()

def main() -> int:
    # Known mappings: 0x01 -> 0x80, 0xA5 -> 0xA5, 0xF0 -> 0x0F, 0x00 -> 0x00
    assert run(bytes([0x01, 0xA5, 0xF0, 0x00])) == bytes([0x80, 0xA5, 0x0F, 0x00]), "byte map"
    # Involution: reversing twice returns the original
    sample = bytes(range(256))
    assert run(run(sample)) == sample, "involution"
    print("PASS: test_reverse_rbf")
    return 0

if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Run it to verify it fails**

Run: `python3 tools/test_reverse_rbf.py`
Expected: FAIL — `FileNotFoundError` / `CalledProcessError` (tool doesn't exist yet).

- [ ] **Step 3: Write the tool**

Create `tools/reverse_rbf.py`:

```python
#!/usr/bin/env python3
"""Convert an RBF to the Pocket's RBF_R format: bit-reverse every byte.

Usage: reverse_rbf.py <input.rbf> <output.rbf_r>
"""
import sys

TABLE = bytes(int(f"{i:08b}"[::-1], 2) for i in range(256))

def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    with open(sys.argv[1], "rb") as f:
        data = f.read()
    with open(sys.argv[2], "wb") as f:
        f.write(data.translate(TABLE))
    return 0

if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `python3 tools/test_reverse_rbf.py`
Expected: `PASS: test_reverse_rbf`, exit 0.

- [ ] **Step 5: Locate (or construct) the Cores staging directory and add the `package` target**

Determine the exact `<author>.<shortname>` folder name and the RBF
filename `core.json` declares (per the Global Constraints' exact-match
requirement), from the vendored `dist/`'s or template root's `core.json`
(per what Task 2 Step 2 recorded). Create
`dist/Cores/<author>.<shortname>/` if the template doesn't already ship it.

Add to `Makefile`, replacing the placeholders with the real discovered values:

```make
RBF        ?= <path recorded in docs/template-notes.md, e.g. src/fpga/output_files/ap_core.rbf>
RBF_R_DEST ?= <exact path, e.g. dist/Cores/Developer.Core Template/bitstream.rbf_r>

.PHONY: package
package:
	python3 tools/reverse_rbf.py "$(RBF)" "$(RBF_R_DEST)"
	@echo "Staged: $(RBF_R_DEST)"
	@echo "Copy the contents of dist/ onto the Pocket SD card root."
```

Set `RBF_R_DEST` explicitly to the real path (don't derive it with `find`
if the directory doesn't pre-exist with a placeholder file to find).

- [ ] **Step 6: Run `make package` and confirm the staged file**

Run: `make package`
Expected: `Staged: <path>` printed, and `ls -la "<RBF_R_DEST>"` shows a
nonzero-size file whose size matches the source `.rbf` exactly (byte
reversal doesn't change length).

- [ ] **Step 7: Commit**

```bash
git add tools/reverse_rbf.py tools/test_reverse_rbf.py Makefile dist
git commit -m "Add RBF_R packaging: bit-reversal tool + make package target"
```

---

### Task 4: On-Pocket boot test — toolchain scaffold completion

**Files:**
- No new files, unless the boot reveals something to document (then update `docs/template-notes.md`).

**Interfaces:**
- Consumes: `make package`'s staged `dist/` (Task 3).

- [ ] **Step 1: Package and boot on hardware — CHECKPOINT (human required)**

Run: `make package` (if not already fresh from Task 3), then copy `dist/`'s
contents onto the Pocket SD card root (merging `Cores/`, `Platforms/`,
etc. — Pocket folder names are capitalized; the vendored `dist/` uses
lowercase `platforms/` as its local staging name for the same content),
insert into the Pocket, power on.

**This step requires the human partner and the physical device — stop and
ask before proceeding, exactly like FB1's plan did for the same
checkpoint.**

Expected: the template core appears in the Pocket's openFPGA core menu and
launches without an error dialog (a static placeholder screen — exact
content per the vendored template's own README, not Football II content
yet). If it fails with `"Load error in 'core': General Error"`, the most
likely cause (per FB1's own hardware findings) is the `/Cores/` folder name
not exactly matching `core.json`'s `<author>.<shortname>` — re-check that
first before assuming a build problem.

- [ ] **Step 2: Record the boot result**

Append a short "Hardware boot test findings" section to
`docs/template-notes.md` (mirroring FB1's own template-notes.md structure):
firmware version tested against, exact RBF size, confirmation the core
booted cleanly (or, if not, what was wrong and how it was fixed).

- [ ] **Step 3: Commit**

```bash
git add docs/template-notes.md
git commit -m "Confirm toolchain scaffold: template core boots on real Pocket hardware"
```

This is the sub-project's completion criterion — once this commit lands,
the toolchain scaffold is proven end-to-end, and the next sub-project (APF
integration: wiring the real Football II CPU/display/audio into
`core_top.v`) can build on a confirmed-working pipeline.

---

## Self-Review Notes

- **Spec coverage:** design spec §1 (repo layout) → Tasks 2-3's file
  structure; §2 (vendoring/fact-recording discipline) → Task 2 Steps 1-2;
  §3 (Docker/Quartus) → Task 1; §4 (packaging, SD-card folder-name
  requirement) → Task 3 Step 5, Task 4; §5 (completion criteria) → Task 4.
- **No placeholders:** every step has literal code/commands; the
  `<fill with actual observed value>` placeholders in Task 2's
  `docs/template-notes.md` and `Makefile` steps are explicitly flagged as
  "fill from what Step 1 actually discovers," not vague TODOs — this
  mirrors FB1's own plan's identical pattern for the same genuinely
  can't-know-in-advance values (the template's exact file layout).
- **Reuse honored:** Task 3 explicitly points at FB1's `reverse_rbf.py` as
  a near-verbatim reuse candidate (it's genuinely generic, no FB1-specific
  content) rather than re-deriving byte-reversal logic from scratch.
