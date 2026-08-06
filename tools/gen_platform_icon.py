#!/usr/bin/env python3
"""Generates dist/platforms/_images/mattel_fb_ii.bin -- the Pocket's
platform-browser icon, replacing the openFPGA template placeholder.

Run with no arguments to (re)generate the review preview PNG; run with
--write to also produce the final .bin.

Original artwork, and a deliberate sibling to the Football I core's icon
(tools/gen_platform_icon.py in cjchand/openfpga-mattel-football): black
background, white 7-segment/LED-style wordmark, mid-gray yard-line ticks.
Nothing here is traced from or derived from Mattel packaging, labelling or
any other source material -- the segment font is a from-scratch
interpretation, and T and B use a shorter "lowercase-style" form (no top
segment) because uppercase T/B have no clean, unambiguous 7-segment
representation, exactly the limitation real 7-segment alphanumeric displays
have.

Differences from FB1's icon, so the two are distinguishable at a glance in
the platform browser:
  * a "II" numeral, rendered as two bare vertical segment pairs -- how a
    real 7-segment digit forms a 1
  * a slightly smaller wordmark to make room for it
  * ten tick divisions rather than five, matching this game's ten field
    columns (FB1 has nine)

NOTE: the icon format is 8-bit GRAYSCALE (see tools/platform_icon.py --
low byte is the grey level, high byte is always 0). The core's green field
and blue endzones cannot appear here; the art has to work in grey.
"""
import argparse
import sys
from pathlib import Path

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))
from platform_icon import LANDSCAPE_SIZE, encode_landscape_to_bin

REPO_ROOT = Path(__file__).resolve().parent.parent
OUT_BIN = REPO_ROOT / "dist" / "platforms" / "_images" / "mattel_fb_ii.bin"
PREVIEW_PNG = REPO_ROOT / "tools" / "platform_icon_preview.png"

BG = 0
FG = 255
TICK_GRAY = 90

# Smaller than FB1's 55x100 cell to leave room for the "II".
CHAR_W = 44
CHAR_H = 80
SEG_T = 8
CHAR_GAP = 6

# Gap between the wordmark and the numeral, and between the two I bars.
NUM_GAP = 20
BAR_GAP = 9

FONT = {
    "F": {"a", "f", "g", "e"},
    "O": {"a", "b", "c", "d", "e", "f"},
    "T": {"d", "e", "f", "g"},
    "B": {"f", "g", "e", "c", "d"},
    "A": {"a", "b", "c", "e", "f", "g"},
    "L": {"d", "e", "f"},
}

WORD = "FOOTBALL"


def segment_rects(w: int, h: int, t: int) -> dict:
    half = h // 2
    return {
        "a": (0, 0, w, t),
        "g": (0, half - t // 2, w, half + t // 2),
        "d": (0, h - t, w, h),
        "f": (0, 0, t, half),
        "b": (w - t, 0, w, half),
        "e": (0, half, t, h),
        "c": (w - t, half, w, h),
    }


def draw_char(draw: ImageDraw.ImageDraw, x0: int, y0: int, ch: str) -> None:
    rects = segment_rects(CHAR_W, CHAR_H, SEG_T)
    for seg in FONT[ch]:
        x1, y1, x2, y2 = rects[seg]
        draw.rectangle([x0 + x1, y0 + y1, x0 + x2, y0 + y2], fill=FG)


def render() -> Image.Image:
    img = Image.new("L", LANDSCAPE_SIZE, BG)
    draw = ImageDraw.Draw(img)

    word_w = len(WORD) * CHAR_W + (len(WORD) - 1) * CHAR_GAP
    numeral_w = 2 * SEG_T + BAR_GAP
    total_w = word_w + NUM_GAP + numeral_w

    x0 = (LANDSCAPE_SIZE[0] - total_w) // 2
    y0 = (LANDSCAPE_SIZE[1] - CHAR_H) // 2

    for i, ch in enumerate(WORD):
        draw_char(draw, x0 + i * (CHAR_W + CHAR_GAP), y0, ch)

    # "II" -- each bar is the b+c segment pair of a 7-segment cell, i.e. the
    # same stroke a real display uses to show a 1.
    nx = x0 + word_w + NUM_GAP
    for i in range(2):
        bx = nx + i * (SEG_T + BAR_GAP)
        draw.rectangle([bx, y0, bx + SEG_T, y0 + CHAR_H], fill=FG)

    # Yard-line ticks above and below, echoing the in-game field's markers
    # without reproducing any specific device's field art. Ten divisions
    # here against FB1's five, matching this game's ten field columns.
    for tick_y in (y0 - 14, y0 + CHAR_H + 14):
        for i in range(11):
            tx = x0 + round(i * total_w / 10)
            draw.line([(tx, tick_y - 4), (tx, tick_y + 4)], fill=TICK_GRAY, width=2)

    return img


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--write", action="store_true",
                        help="write the final .bin (default: preview PNG only)")
    args = parser.parse_args()

    img = render()
    img.save(PREVIEW_PNG)
    print(f"Preview written to {PREVIEW_PNG}")

    if args.write:
        data = encode_landscape_to_bin(img)
        OUT_BIN.write_bytes(data)
        print(f"Wrote {OUT_BIN} ({len(data)} bytes)")
    else:
        print("Review the preview, then re-run with --write to produce the .bin.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
