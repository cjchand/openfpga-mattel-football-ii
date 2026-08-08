#!/usr/bin/env python3
"""Guards the shape of what we ship in dist/.

Exists because v0.1.0 and v1.0.0 both shipped a core folder whose name did not
match core.json's shortname, which the Pocket rejects at load time with
"Load error in 'core': General Error". Nothing in the test suite looked at
dist/, so the defect was invisible until a third party tried to install it.
See docs/follow-ups.md section 3.
"""
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CORES = ROOT / "dist" / "Cores"

failures = []


def check(cond, msg):
    if not cond:
        failures.append(msg)


core_dirs = [d for d in CORES.iterdir() if d.is_dir()]
check(len(core_dirs) == 1, f"expected exactly one core folder in dist/Cores, found {[d.name for d in core_dirs]}")

for d in core_dirs:
    meta = json.loads((d / "core.json").read_text())["core"]["metadata"]
    expected = f"{meta['author']}.{meta['shortname']}"
    # The Pocket matches this character for character -- spaces included.
    check(
        d.name == expected,
        f"core folder {d.name!r} must equal author.shortname {expected!r}; "
        f"a mismatch is a load error on real hardware",
    )

    # The bitstream named in core.json has to actually be there.
    for entry in json.loads((d / "core.json").read_text())["core"]["cores"]:
        check((d / entry["filename"]).is_file(), f"{d.name}: missing bitstream {entry['filename']}")

    # Required dataslots are user-supplied. A local dump under dist/assets/ is
    # expected and gitignored -- what must never happen is the ROM landing in
    # the core folder or getting committed, either of which would distribute it.
    for slot in json.loads((d / "data.json").read_text())["data"]["data_slots"]:
        fn = slot.get("filename")
        if not fn:
            continue
        check(not list(d.rglob(fn)), f"ROM {fn} must not be inside the core folder {d.name}")
        tracked = subprocess.run(
            ["git", "ls-files", "--error-unmatch", f"dist/assets/mattel_fb_ii/common/{fn}"],
            cwd=ROOT, capture_output=True,
        )
        check(tracked.returncode != 0, f"ROM {fn} must never be committed to git")

if failures:
    for f in failures:
        print(f"FAIL: {f}", file=sys.stderr)
    sys.exit(1)

print("PASS: test_core_packaging")
