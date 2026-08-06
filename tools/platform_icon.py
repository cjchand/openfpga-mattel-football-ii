"""Encode/decode for Analogue Pocket platform-browser icons
(dist/platforms/_images/<platform_id>.bin).

Format confirmed empirically against a real, populated Pocket SD card
(see docs/superpowers/specs/2026-07-31-platform-icon-design.md):
stored portrait (165w x 521h, matching the Pocket panel's native
orientation), row-major, 2 bytes/pixel little-endian where the low byte
is an 8-bit grayscale value and the high byte is always 0. Art is
authored/reviewed in the human-friendly landscape orientation (521x165,
matching how the icon actually reads on-screen) and rotated for
storage.
"""
from PIL import Image

LANDSCAPE_SIZE = (521, 165)
PORTRAIT_SIZE = (165, 521)


# IMPORTANT: Rotation direction (ROTATE_90 encode / ROTATE_270 decode) was
# empirically pinned by comparing against a real reference .bin/.png pair from
# a populated Pocket SD card (methodology in
# docs/superpowers/specs/2026-07-31-platform-icon-design.md), then confirmed
# correct by a human checking the real device. DO NOT change these constants
# without re-validating on hardware: the round-trip test (sim/test_platform_icon.py)
# is direction-blind. Flipping BOTH ROTATE_90→ROTATE_270 and ROTATE_270→ROTATE_90
# still round-trips losslessly and passes the test, silently shipping a 180°
# rotated icon. Only hardware verification can catch this trap.
def encode_landscape_to_bin(img: Image.Image) -> bytes:
    if img.size != LANDSCAPE_SIZE:
        raise ValueError(f"expected {LANDSCAPE_SIZE}, got {img.size}")
    portrait = img.convert("L").transpose(Image.Transpose.ROTATE_90)
    gray = portrait.tobytes()
    out = bytearray(len(gray) * 2)
    out[0::2] = gray
    return bytes(out)


def decode_bin_to_landscape(data: bytes) -> Image.Image:
    expected_len = PORTRAIT_SIZE[0] * PORTRAIT_SIZE[1] * 2
    if len(data) != expected_len:
        raise ValueError(f"expected {expected_len} bytes, got {len(data)}")
    gray = data[0::2]
    portrait = Image.frombytes("L", PORTRAIT_SIZE, bytes(gray))
    return portrait.transpose(Image.Transpose.ROTATE_270)
