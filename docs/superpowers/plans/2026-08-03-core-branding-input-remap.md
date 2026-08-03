# Core identity, branding, and default input remap Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rebrand this core from the upstream `open-fpga/core-template` placeholder identity to its real Football II identity, and fix the default face-button mapping to match the real device's layout.

**Architecture:** Pure config/rename work (openFPGA manifest JSON, a packaging folder rename, one Makefile variable) plus one combinational rewiring in `core_top.v`. No new RTL modules, no new CPU semantics, no new ports.

**Tech Stack:** openFPGA core manifest JSON (`core.json`/`platform.json`), GNU Make, Verilog (`core_top.v`), Verilator test suite (existing, unchanged).

## Global Constraints

- The SD-card folder name (`dist/Cores/<author>.<shortname>/`) must exactly match `core.json`'s `metadata.author` + `.` + `metadata.shortname`, including any literal spaces — a mismatch means the Pocket shows "Load error in 'core': General Error" (per this repo's own `docs/template-notes.md`, confirmed independently on the FB1 sibling project).
- Mirror the FB1 sibling project's (`cjchand/openfpga-mattel-football`) established branding convention exactly: author `cjchand`, same JSON key structure, same license-section framing. Do not invent a different convention.
- Do not touch `input.json`, `variants.json`, or `audio.json` — they already match FB1's convention (empty/default) and are out of scope.
- Do not touch bezel/overlay rendering, PRO1/PRO2 difficulty switch, or any "Original Controls" toggle — all explicitly deferred to future phases per the spec (`docs/superpowers/specs/2026-08-03-core-branding-input-remap-design.md`).
- Do not touch `dist/icon.bin` or customize `dist/platforms/_images/*.bin` beyond renaming the file — content customization is out of scope (FB1 doesn't customize it either).
- After the `core_top.v` change, `make test` (the full Verilator/golden-model suite) must still pass unmodified — the remap only changes which physical Pocket button maps to which `p_input` bit, not the testbench's direct bit-level `p_input` driving.

---

### Task 1: Core identity/branding rename

**Files:**
- Rename: `dist/Cores/Developer.Core Template/` → `dist/Cores/cjchand.Mattel Football II/` (git mv, all 7 files inside move with it)
- Modify: `dist/Cores/cjchand.Mattel Football II/core.json`
- Modify: `dist/Cores/cjchand.Mattel Football II/info.txt`
- Rename: `dist/platforms/ex_platform.json` → `dist/platforms/mattel_football_ii.json`
- Modify: `dist/platforms/mattel_football_ii.json`
- Rename: `dist/platforms/_images/ex_platform.bin` → `dist/platforms/_images/mattel_football_ii.bin` (content unchanged)
- Modify: `Makefile:10`

**Interfaces:**
- Consumes: nothing from other tasks.
- Produces: the renamed `dist/Cores/cjchand.Mattel Football II/` folder and `dist/platforms/mattel_football_ii.json` that Task 3's build verification checks.

- [ ] **Step 1: Rename the Cores folder and platform files with `git mv`**

```bash
git mv "dist/Cores/Developer.Core Template" "dist/Cores/cjchand.Mattel Football II"
git mv dist/platforms/ex_platform.json dist/platforms/mattel_football_ii.json
git mv dist/platforms/_images/ex_platform.bin dist/platforms/_images/mattel_football_ii.bin
git status
```

Expected: `git status` shows the three renames (as renames, not delete+add, confirming git tracked them as moves).

- [ ] **Step 2: Update `core.json`'s metadata block**

Open `dist/Cores/cjchand.Mattel Football II/core.json`. Replace the
`"metadata"` object (keep `"framework"` and `"cores"` blocks byte-for-byte
unchanged — they already match Task 6/8's hardware requirements) with:

```json
        "metadata": {
            "platform_ids": ["mattel_football_ii"],
            "shortname": "Mattel Football II",
            "description": "Mattel Electronic Football II (1978) for the Analogue Pocket.",
            "author": "cjchand",
            "url": "https://github.com/cjchand/openfpga-mattel-football-ii",
            "version": "1.0.0",
            "date_release": "2026-08-03"
        },
```

- [ ] **Step 3: Update `info.txt`**

Replace the entire contents of
`dist/Cores/cjchand.Mattel Football II/info.txt` with:

```
Mattel Football II

Mattel Electronic Football II (1978) for the Analogue Pocket.

Controls: D-pad moves. Face buttons: top = Score, bottom = Kick,
left = Status, right = Pass (Select/Start also work for Status/Score).
```

- [ ] **Step 4: Update `dist/platforms/mattel_football_ii.json`**

Replace its entire contents with:

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

- [ ] **Step 5: Update the Makefile's `RBF_R_DEST`**

In `Makefile:10`, change:

```makefile
RBF_R_DEST ?= dist/Cores/Developer.Core Template/bitstream.rbf_r
```

to:

```makefile
RBF_R_DEST ?= dist/Cores/cjchand.Mattel Football II/bitstream.rbf_r
```

- [ ] **Step 6: Rename the local (gitignored) Assets ROM folder to match**

The dataslot loader resolves ROM paths as `dist/Assets/<platform_id>/common/<filename>`
(per `data.json`'s dataslot definition). `dist/Assets/` is entirely
gitignored (confirmed via `git check-ignore -v dist/Assets/ex_platform/common/b8000-12.bin`),
so this local folder from a prior task's hardware-prep work won't be
caught by `git mv` and must be renamed directly to match the new
`mattel_football_ii` platform id from Step 4, or the core will fail to
find the ROM at runtime despite everything else being correctly renamed:

```bash
if [ -d dist/Assets/ex_platform ]; then
  mv dist/Assets/ex_platform dist/Assets/mattel_football_ii
fi
ls dist/Assets/
```

Expected: if the folder existed, it's now `dist/Assets/mattel_football_ii/`
containing `common/b8000-12.bin`. If it didn't exist (fresh checkout with
no ROM staged locally yet), the `if` guard skips this harmlessly — that's
fine, the folder gets created at the new name whenever a ROM is staged.

- [ ] **Step 7: Repo-wide grep for any remaining stray references**

Run:

```bash
grep -rn "Developer.Core Template\|ex_platform\|Example Platform\|Example Manufacturer\|Example Core" \
  --include="*.md" --include="*.json" --include="Makefile" --include="*.py" --include="*.sh" --include="*.v" . \
  | grep -v "^docs/superpowers/plans/2026-08-0[23]-.*\.md:" \
  | grep -v "^docs/template-notes.md:"
```

Expected: no output. (The excluded paths are historical planning/notes
docs that correctly describe what was true *at the time they were
written* — do not edit those; they're a record, not current state.) If
anything else shows up, fix it before moving on.

- [ ] **Step 8: Commit**

```bash
git add -A dist Makefile
git commit -m "$(cat <<'EOF'
Rebrand core identity from upstream template placeholder to Mattel Football II

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 2: Default face-button remap

**Files:**
- Modify: `src/fpga/core/core_top.v` (the `p_input_w` assignment, currently at approximately lines 519-528)

**Interfaces:**
- Consumes: nothing from Task 1.
- Produces: nothing consumed by later tasks — this is a leaf change. Task 3's `make test` run verifies it didn't break the existing suite.

- [ ] **Step 1: Locate and read the current mapping**

```bash
grep -n "p_input_w" src/fpga/core/core_top.v
```

Confirm the block reads (comment wording may vary slightly, match on
structure):

```verilog
    // p_input bit mapping, per docs/initial-plan.md §7's IN.0 table and the
    // template's own cont1_key bit comment (this file, ~line 187-202).
    wire [7:0] p_input_w = {
        cont1_key[2],  // bit7: Left     = dpad_left
        cont1_key[1],  // bit6: Down     = dpad_down
        cont1_key[5],  // bit5: Pass     = face_b
        cont1_key[4],  // bit4: Kick     = face_a
        cont1_key[3],  // bit3: Right    = dpad_right
        cont1_key[0],  // bit2: Up       = dpad_up
        cont1_key[14], // bit1: Status   = face_select
        cont1_key[15]  // bit0: Score    = face_start
    };
```

- [ ] **Step 2: Replace it with the new mapping**

```verilog
    // p_input bit mapping, per docs/initial-plan.md §7's IN.0 table.
    // Face buttons match the real device's layout (Phase 5 remap):
    // top=Score, bottom=Kick, left=Status, right=Pass. Select/Start are
    // kept as redundant alternates for Status/Score.
    wire [7:0] p_input_w = {
        cont1_key[2],                  // bit7: Left     = dpad_left
        cont1_key[1],                  // bit6: Down     = dpad_down
        cont1_key[4],                  // bit5: Pass     = face_a (right)
        cont1_key[5],                  // bit4: Kick     = face_b (bottom)
        cont1_key[3],                  // bit3: Right    = dpad_right
        cont1_key[0],                  // bit2: Up       = dpad_up
        cont1_key[7] | cont1_key[14],  // bit1: Status   = face_y (left) | Select
        cont1_key[6] | cont1_key[15]   // bit0: Score    = face_x (top)  | Start
    };
```

- [ ] **Step 3: Verify the file still parses as valid Verilog**

```bash
grep -c "p_input_w" src/fpga/core/core_top.v
```

Expected: `2` (the declaration line plus its one consumer in the
`pps41_core` instantiation) — confirms no stray duplicate/syntax break.
This is a quick sanity check; Task 3's `make test` and `make bitstream`
are the real verification.

- [ ] **Step 4: Commit**

```bash
git add src/fpga/core/core_top.v
git commit -m "$(cat <<'EOF'
Remap default face buttons to match the real device's button layout

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: README, build verification, and final commit

**Files:**
- Create: `README.md` (repo root)
- No other files modified — this task only verifies Tasks 1-2's changes build correctly.

**Interfaces:**
- Consumes: the renamed folders from Task 1 and the remapped `core_top.v` from Task 2.
- Produces: nothing further — this is the phase's completion gate.

- [ ] **Step 1: Write `README.md`**

Create `README.md` at the repo root with the following content:

```markdown
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
2. Copy the dump to `dist/Assets/mattel_football_ii/common/b8000-12.bin`
   (per this core's `data.json` dataslot definition, filename
   `b8000-12.bin`, under the renamed platform folder from Task 1).
3. Copy the entire contents of `dist/` onto your Analogue Pocket's SD
   card root.
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
```

- [ ] **Step 2: Commit the README**

```bash
git add README.md
git commit -m "$(cat <<'EOF'
Add project README

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 3: Run the existing Verilator/golden-model test suite**

```bash
make test
```

Expected: all tests pass (same pass/fail status as before Task 2's
change — the remap only changes which `cont1_key` bit feeds which
`p_input` bit at the `core_top.v` level; the testbench drives `p_input`
directly and is unaffected).

- [ ] **Step 4: Recompile the bitstream in Docker**

```bash
make bitstream
```

Expected: Quartus flow completes with 0 errors. Compare the warning count
to the last known-good baseline (200 warnings, per this project's own
`docs/template-notes.md` / `.superpowers/sdd/2026-08-03-apf-integration/progress.md`
Task 8 entry) — flag any *new* warning category introduced by the
`core_top.v` remap for review; don't silently wave through anything new.

- [ ] **Step 5: Package for the SD card and confirm the renamed folder**

```bash
make package
ls "dist/Cores/cjchand.Mattel Football II/"
```

Expected: `make package` succeeds and prints the "copy dist/ to SD card"
message; `ls` shows `bitstream.rbf_r` alongside the other manifest files
(`core.json`, `audio.json`, `info.txt`, `input.json`, `interact.json`,
`variants.json`, `video.json`) all inside the correctly-renamed folder.

- [ ] **Step 6: Record findings in `docs/template-notes.md`**

Append a dated section noting: the rename (old folder name → new), the
button remap, and the `make bitstream`/`make package` result (error/warning
counts) from Step 4-5 above.

- [ ] **Step 7: Final commit**

```bash
git add docs/template-notes.md
git commit -m "$(cat <<'EOF'
Record Phase 5 branding rename and button remap build verification

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```
