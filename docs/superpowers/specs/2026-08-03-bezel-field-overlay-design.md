# Phase 6: Bezel/field overlay

## Context

Phase 5 shipped the core's real branding and button mapping and
established, via `display_render.v`, a correctly-classified digit/field
layout sourced directly from a local MAME checkout (`~/Projects/mame`):
which of the CPU's 10 display rows are 7-segment digits vs. 30-lamp field
positions, and their real left-to-right screen order. That work — plus a
real CPU bug fix (the reset PC was wrong, `docs`'s own earlier research
never caught it) and a MAME-golden-trace-confirmed regression test — is
recorded in this session's commits on `main` (`7bfd015`, `565f173`,
`7ec223d`, `13769bb`).

What `display_render.v` still draws is placeholder geometry: generic
7-segment glyphs and square lamps on a plain black background, at
arbitrary size/spacing. The sibling FB1 project solved this properly for
its own device: a photo-calibrated "bezel" — procedural field art plus
real label text, baked into ROM bitmaps by an offline Python tool
(`tools/gen_bezel_bitmaps.py`), composited at runtime by `video_renderer.v`.
This phase ports that same pattern to FB2, using a real photo of the
actual Mattel Electronics Football II handheld
(`https://www.handheldmuseum.com/Mattel/Mattel-FootballII.jpg`) as the
measurement reference — the same role FB1's own reference photo played
for its bezel.

## Goals

1. Render the field as photo-calibrated procedural art: green background,
   10 field columns (not FB1's 9) with dividers and hash-tick marks, and
   endzones — colors and proportions measured from the real photo, not
   assumed or copied from FB1's device (a different physical unit with
   different colors).
2. Render the scoreboard's real label text: `DOWN / FIELD POSITION /
   YARDS TO GO` above the 3 digit windows, `HOME / TIME REMAINING /
   VISITORS` below (confirmed wording and plural "VISITORS" from both
   the photo and MAME's `layout/mfootb2.lay`) — both labels are
   permanently printed flanking one shared set of digits, which the real
   CPU multiplexes between the two meanings (confirmed via a MAME
   golden-output comparison this session: holding Status vs. Score
   produces different digit content in the same window).
3. Grow the video canvas to fit this legibly, reusing FB1's exact proven
   timing numbers (no new pixel-clock math needed).
4. Port FB1's `bezel_enable`/"Presentation" on-off toggle for parity.

## Non-goals

- Physical cabinet appearance: buttons, branding, speaker grille, the
  case's rounded/oval outline and decorative concentric-groove texture
  around the screen. Only the video content behind the Pocket's own
  screen matters; the field+scoreboard rectangle is what gets rendered,
  same scope boundary FB1 drew for its own bezel.
- Pixel-perfect photo tracing. Per the existing design precedent
  (Phase 5's "simple procedural shapes" fidelity, and FB1's own
  procedurally-drawn field), this measures real proportions and colors
  but draws them as clean procedural shapes, not a traced/cropped photo
  underlay.
- PRO1/PRO2 difficulty switch and the unimplemented D-bus read — still a
  separate, already-deferred phase.
- Resolving exactly what the currently-nonstandard field-lamp segment
  patterns mean gameplay-wise (e.g. the non-digit pattern seen on the
  middle window under "Status" held) — out of scope; the renderer draws
  whatever the real CPU outputs, faithfully, using the same lamp
  positions regardless of which meaning is active.

## Design

### 1. Measured geometry (from the real device photo)

Sampled directly from `handheldmuseum.com`'s photo (see this
conversation's history for the exact crop/sample coordinates; final
precise pixel values get tuned during implementation the same way FB1's
own constants were, this is the starting reference):

- **Field background green:** a brighter teal-green, ~`(18,202,125)` —
  distinctly different from FB1's darker forest green `(14,138,3)`. Do
  not reuse FB1's color.
- **Endzone color:** bright cyan/sky-blue, ~`(26,230,255)` — distinctly
  different from FB1's darker blue `(13,98,188)`. Do not reuse FB1's
  color.
- **Field columns:** 10 dark (near-black) cells divided by white/light
  divider lines, each divider crossed by a **single** small hash-tick
  near vertical center — FB1's field has **two** hash-tick rows splitting
  the strip into thirds; FB2's real photo shows only one. Do not carry
  FB1's two-row convention over.
- **Endzones:** one on each side of the 10 field columns (12 columns
  total in the background bitmap, matching FB1's "11 columns, not 9 with
  the outer two recolored" precedent scaled up by one field column).
- **Scoreboard label bar:** white background with blue text — FB1's is
  gray background with blue text. Do not reuse FB1's gray.
- **Label wording**, confirmed from both the photo and MAME's
  `layout/mfootb2.lay` (`text_down`/`text_home`/`text_field`/
  `text_time`/`text_yards`/`text_visitors` elements): top bar `DOWN`,
  `FIELD POSITION`, `YARDS TO GO`; bottom bar `HOME`, `TIME REMAINING`,
  `VISITORS` (plural — FB1's own equivalent bar says singular
  `VISITOR`; that's a real difference between the two physical devices,
  not a typo to reconcile).
- **Digit window grouping:** 2 digits / 3 digits / 2 digits
  (`DOWN`+`HOME` window = screen slots 0-1 = CPU rows 8,9;
  `FIELD POSITION`+`TIME REMAINING` window = screen slots 2-4 = CPU rows
  0,1,2; `YARDS TO GO`+`VISITORS` window = screen slots 5-6 = CPU rows
  6,7) — same 2/3/2 shape FB1 uses, already established in Phase 5's
  `display_render.v` (`digit_row()`/screen-slot order).
- **Field lamp columns:** 10, in the same screen order as the digit rows
  extend to (CPU rows 8,9,0,1,2,3,4,5,6,7 left to right), already
  established in Phase 5's `display_render.v` (`field_row()`).

### 2. Architecture — port FB1's pipeline structure

Mirrors FB1's file layout and division of labor exactly, adapted for the
above deltas:

- **`tools/gen_bezel_bitmaps.py`** (new, adapted from FB1's script of the
  same name): generates two ROM bitmaps —
  - **Field bitmap:** drawn procedurally (green background, 10 field
    columns + 2 endzones, single-row hash ticks, all colors/proportions
    per §1) — same approach as FB1's `build_field_image()`, new
    constants.
  - **Label bitmap:** FB1 crops its label text from a pre-made overlay
    PNG asset (`assets/bezel/overlay_400x360.png`) that already contained
    real anti-aliased text from an earlier, unspecified source. FB2 has
    no equivalent pre-made overlay asset. Instead, this tool renders the
    label text directly using `PIL.ImageFont` (a bundled/system
    TrueType font) at high resolution, then downscales with LANCZOS —
    same anti-aliasing goal as FB1's approach, different source (font
    rendering instead of cropping), since there's no source image to
    crop from. Pick a clean, legible sans-serif; exact font choice and
    any bold/contrast tuning (FB1 needed both, see its script's
    `bolden()`/`punch_up_contrast()`) happens during implementation
    against a rendered preview, not decided in advance here.
  - Outputs (`field_bitmap.mem`/`field_palette.mem`/
    `label_bitmap.mem`/`label_palette.mem`) are committed, same as FB1's
    (and this project's own `tools/reverse_rbf.py` build-output
    precedent) — no Python dependency at FPGA build time.
- **`src/field_rom.v`** / **`src/label_rom.v`** (new): near-verbatim ports
  of FB1's modules of the same name — indexed-bitmap ROM lookups via
  `$readmemh`, re-sized for FB2's canvas/column count.
- **`src/video_renderer.v`** (new, replaces `display_render.v`'s role):
  layered compositor like FB1's — LED segments/lamps (procedural,
  reusing Phase 5's exact digit/field cell-to-position mapping) over the
  label bitmap over the field bitmap over background. Digit segment
  shapes, decimal point (row 1 only), and field lamp positions carry over
  unchanged from Phase 5's `display_render.v` logic; only the canvas
  layout (label bars now real, field now has real background art instead
  of solid black) changes.
- **`src/fpga/core/core_top.v`:** canvas grown to `VID_H_ACTIVE=400,
  VID_V_ACTIVE=360, VID_H_TOTAL=512, VID_V_TOTAL=400` — FB1's exact
  final numbers (`H_TOTAL*V_TOTAL=204800`, `204800*60=12,288,000Hz`,
  so 60.000Hz holds exactly on the existing fixed 12.288MHz PLL, no PLL
  changes needed). `display_render` module instantiation replaced with
  `video_renderer`.
- **`interact.json`** (new "Presentation" toggle) and the
  `bezel_enable_74a`/`synch_2`/`datatable` wiring in `core_top.v`: ported
  from FB1's `docs/superpowers/plans/2026-07-26-bezel-overlay.md`
  Task list for parity — toggling it off falls back to the plain
  black-background digit/lamp view (Phase 5's current look), same
  fallback behavior FB1 has.

### 3. Testing

- `display_render_tb.cpp`'s existing segment/lamp-position tests
  (Phase 5) get ported/renamed to `video_renderer_tb.cpp` and continue to
  hold — the digit/lamp cell-to-screen-position mapping itself doesn't
  change, only what's drawn behind/around it.
- New tests for `field_rom`/`label_rom`: verify the bitmap ROMs produce
  the expected background colors at representative sampled coordinates
  (same style as FB1's `field_rom`/`label_rom` — no dedicated testbenches
  exist for FB1's, so this may be light-touch, e.g. folded into
  `video_renderer_tb.cpp`'s own checks rather than separate files).
- Manual verification: run `make screenshot` (already exists from Phase
  5) against the real ROM and visually confirm the rendered frame reads
  as a legible scoreboard + field, cross-checked against the real photo
  and against MAME's own `-video none`-disabled-but-output-watched runs
  from this session (the `watch_outputs.lua` technique) for at least the
  already-confirmed "Score held → Home 00 / Time 15.0 / Visitor 00"
  scenario.
- `make sim` (full existing suite) must stay green throughout — no CPU/
  display-pipeline RTL changes in this phase, only the rendering layer.

## Risks / open questions

- Font choice for the label text (§1): default to **DejaVu Sans Bold**
  (SIL Open Font License, permissively licensed, and already present on
  most systems including inside common Python/matplotlib installs — no
  new font file needs to be committed to the repo, matching how FB1
  never committed its source photo either). If unavailable in the build
  environment, fall back to PIL's built-in bitmap font rather than
  blocking on sourcing a new font file.
- Exact pixel geometry (column pitch, label bar height, margins) is
  approximate from the measurements in §1 and will get refined visually
  during implementation, same iterative-tuning process FB1's own script
  comments show it went through (e.g. its `BAR_OUT_H`/`FIELD_H`
  adjustment history).
- The real device's dual-meaning digit windows (down/home,
  field-position/time, yards-to-go/visitors) are handled here only as a
  rendering concern (draw whatever's lit, using the correct label-bar
  art) — no attempt is made to detect or expose which meaning is
  currently active (e.g. no indicator light), matching what the real
  physical bezel itself does (both labels are always visibly printed;
  the player infers which applies from context).
