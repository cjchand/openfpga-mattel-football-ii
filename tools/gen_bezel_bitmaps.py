#!/usr/bin/env python3
"""Generates src/field_bitmap.mem, src/field_palette.mem,
src/label_bitmap.mem, and src/label_palette.mem for the bezel overlay.
Run manually when the geometry/text constants below change; outputs are
committed (same pattern as tools/reverse_rbf.py's build outputs).

Colors and proportions below are measured from a photo of the real
Mattel Electronics Football II handheld
(https://www.handheldmuseum.com/Mattel/Mattel-FootballII.jpg) -- do not
reuse the sibling FB1 project's constants, which are for a different
physical device (FB1's field is darker green/blue with a gray label
background and 9 field columns with two hash-tick rows; FBII's is a
brighter teal-green/cyan with a white label background and 10 field
columns with one hash-tick row).

Unlike FB1's tools/gen_bezel_bitmaps.py (which crops label text from a
pre-existing overlay PNG asset), this project has no such asset, so
label text is rendered directly from a font.
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

REPO_ROOT = Path(__file__).resolve().parent.parent
SRC = REPO_ROOT / "src"

WIDTH = 400  # canvas width (matches VID_H_ACTIVE)

# --- Label bars ---
# Each bar is rendered at BAR_SRC_H (large, for clean anti-aliasing) then
# downscaled to BAR_OUT_H with LANCZOS -- same anti-aliasing goal as
# FB1's crop-then-downscale approach, applied to rendered text instead of
# a cropped image (see module docstring).
BAR_SRC_H = 64
BAR_OUT_H = 16
BAR_BG_RGB = (255, 255, 255)     # label bar background: white (photo-measured)
BAR_TEXT_RGB = (20, 90, 200)     # label text: blue (photo-measured)
FONT_CANDIDATES = [
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    "/opt/homebrew/share/fonts/DejaVuSans-Bold.ttf",
    "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
]
FONT_SIZE = 40

# Digit-window group centers (must match video_renderer.v's digit_x()
# window groupings: window1 = digit_x(0..1), window2 = digit_x(2..4),
# window3 = digit_x(5..6)). See Task 2's geometry table.
# window1 = x[12,92), window2 = x[140,260), window3 = x[308,388).
LABEL_COL_TARGET_CENTERS = [52, 200, 348]
LABEL_COL_WINDOW_W = [80, 120, 80]
BAR1_WORDS = ["DOWN", "FIELD POSITION", "YARDS TO GO"]
BAR2_WORDS = ["HOME", "TIME REMAINING", "VISITORS"]


def load_font():
    for path in FONT_CANDIDATES:
        if Path(path).exists():
            return ImageFont.truetype(path, FONT_SIZE)
    return ImageFont.load_default()


def render_bar(words, font):
    # Render at `scale` times the final size on BOTH axes, then downscale
    # both together. Scaling only the height (and still multiplying the x
    # centers by `scale`) would push words off the right edge of the
    # canvas and squash the surviving glyphs vertically.
    scale = BAR_SRC_H // BAR_OUT_H
    src_w = WIDTH * scale
    bar = Image.new("RGB", (src_w, BAR_SRC_H), BAR_BG_RGB)
    draw = ImageDraw.Draw(bar)
    for word, out_center_x in zip(words, LABEL_COL_TARGET_CENTERS):
        bbox = draw.textbbox((0, 0), word, font=font)
        w = bbox[2] - bbox[0]
        h = bbox[3] - bbox[1]
        cx = out_center_x * scale
        x = cx - w / 2 - bbox[0]
        y = (BAR_SRC_H - h) / 2 - bbox[1]
        draw.text((x, y), word, font=font, fill=BAR_TEXT_RGB)
    return bar.resize((WIDTH, BAR_OUT_H), Image.LANCZOS)


def check_word_fits(draw, word, font, window_w, scale):
    """FONT_SIZE is sized so every word fits inside its digit window at
    final scale; assert it rather than silently letting a word bleed into
    the neighbouring window."""
    bbox = draw.textbbox((0, 0), word, font=font)
    final_w = (bbox[2] - bbox[0]) / scale
    if final_w > window_w * 0.95:
        raise SystemExit(
            f"{word!r} renders {final_w:.1f}px wide at final scale, which "
            f"exceeds 95% of its {window_w}px window -- lower FONT_SIZE"
        )


def build_label_image():
    font = load_font()
    probe = ImageDraw.Draw(Image.new("RGB", (1, 1)))
    for words in (BAR1_WORDS, BAR2_WORDS):
        for word, window_w in zip(words, LABEL_COL_WINDOW_W):
            check_word_fits(probe, word, font, window_w, BAR_SRC_H // BAR_OUT_H)
    bar1 = render_bar(BAR1_WORDS, font)
    bar2 = render_bar(BAR2_WORDS, font)
    combined = Image.new("RGB", (WIDTH, BAR_OUT_H * 2))
    combined.paste(bar1, (0, 0))
    combined.paste(bar2, (0, BAR_OUT_H))
    return combined


# --- Field ---
# Photo-measured colors (see this file's module docstring) -- distinct
# from FB1's darker palette.
GREEN_RGB = (18, 202, 125)     # field-area background
ENDZONE_RGB = (26, 230, 255)   # endzone
BLACK_RGB = (0, 0, 0)          # field cell fill
DIVIDER_RGB = (230, 230, 230)  # column dividers / hash ticks / border

FIELD_W = 400
# Strip height (STRIP_Y1 - STRIP_Y0) must match video_renderer.v's
# STRIP_H. It is tall enough for the three lamp rows to be spread across
# it (rather than crammed into its top third) and to fill most of the
# 360-row canvas below the scoreboard, leaving only a modest green margin.
STRIP_Y0, STRIP_Y1 = 8, 188  # exclusive end; 180px tall strip
MARGIN_X = 6      # green shows on both outer edges
EZ_W = 19         # endzone width
FIELD_X0 = MARGIN_X + EZ_W  # 25: where the 10-column field region starts
COL_PITCH = 35    # 33px cell + 2px divider, x10 columns = 350px
DIV_W = 2
BORDER_W = 4
BORDER_RGB = DIVIDER_RGB
# Single hash-tick row at the strip's vertical center. With the strip at
# 180px and video_renderer.v's LAMP_Y_OFF=8 / ROW_PITCH=60 / LAMP_H=28,
# the lamp rows occupy strip-relative y8-35, y68-95 and y128-155, so this
# center tick (strip-relative y90) lands among them rather than below
# all three.
HASH_Y = STRIP_Y0 + (STRIP_Y1 - STRIP_Y0) // 2
HASH_H = 2
HASH_REACH = 4


def build_field_image():
    """Draws the field band procedurally: green background, a solid
    border framing the strip, 10 black field columns divided by light
    dividers, an endzone column on each side, and a single row of
    hash-mark ticks at each internal divider (FBII's photo shows one tick
    row, not FB1's two)."""
    im = Image.new("RGB", (FIELD_W, STRIP_Y1 - STRIP_Y0 + 2 * BORDER_W), GREEN_RGB)
    draw = ImageDraw.Draw(im)
    # Shift everything down by BORDER_W so the border has room above the
    # strip too, within this cropped field-only image (video_renderer.v
    # positions this whole image starting at canvas y=STRIP_Y0-BORDER_W).
    y0 = BORDER_W

    draw.rectangle(
        [MARGIN_X - BORDER_W, 0, FIELD_W - MARGIN_X - 1 + BORDER_W, y0 + (STRIP_Y1 - STRIP_Y0) - 1 + BORDER_W],
        fill=BORDER_RGB,
    )
    draw.rectangle([MARGIN_X, y0, MARGIN_X + EZ_W - 1, y0 + (STRIP_Y1 - STRIP_Y0) - 1], fill=ENDZONE_RGB)

    divider_xs = [FIELD_X0 + COL_PITCH * i for i in range(11)]
    for i, dx in enumerate(divider_xs):
        draw.rectangle([dx, y0, dx + DIV_W - 1, y0 + (STRIP_Y1 - STRIP_Y0) - 1], fill=DIVIDER_RGB)
        if i < 10:
            cx0 = dx + DIV_W
            cx1 = FIELD_X0 + COL_PITCH * (i + 1) - 1
            draw.rectangle([cx0, y0, cx1, y0 + (STRIP_Y1 - STRIP_Y0) - 1], fill=BLACK_RGB)

    ez2_x0 = divider_xs[10] + DIV_W
    draw.rectangle([ez2_x0, y0, ez2_x0 + (EZ_W - DIV_W) - 1, y0 + (STRIP_Y1 - STRIP_Y0) - 1], fill=ENDZONE_RGB)

    hash_y = y0 + (HASH_Y - STRIP_Y0)
    for dx in divider_xs[1:10]:  # internal dividers only, not the 2 bordering the endzones
        draw.rectangle(
            [dx - HASH_REACH, hash_y - HASH_H // 2, dx + DIV_W - 1 + HASH_REACH, hash_y + HASH_H // 2 - 1],
            fill=DIVIDER_RGB,
        )

    return im


def write_bitmap(im, colors, bitmap_path, palette_path, bits_per_pixel):
    quantized = im.quantize(colors=colors, method=Image.MEDIANCUT)
    palette_bytes = quantized.getpalette()[: colors * 3]
    palette_bytes = palette_bytes + [0] * (colors * 3 - len(palette_bytes))

    with open(palette_path, "w") as f:
        for i in range(colors):
            r, g, b = palette_bytes[i * 3 : i * 3 + 3]
            f.write(f"{(r << 16) | (g << 8) | b:06x}\n")

    pixels = quantized.load()
    hex_digits = (bits_per_pixel + 3) // 4
    with open(bitmap_path, "w") as f:
        for y in range(quantized.height):
            for x in range(quantized.width):
                f.write(f"{pixels[x, y]:0{hex_digits}x}\n")

    print(f"wrote {bitmap_path} ({quantized.width * quantized.height} pixels, {bits_per_pixel}bpp)")
    print(f"wrote {palette_path} ({colors} colors)")


def main():
    labels = build_label_image()
    write_bitmap(labels, 128, SRC / "label_bitmap.mem", SRC / "label_palette.mem", bits_per_pixel=7)

    field = build_field_image()
    write_bitmap(field, 256, SRC / "field_bitmap.mem", SRC / "field_palette.mem", bits_per_pixel=8)


if __name__ == "__main__":
    main()
