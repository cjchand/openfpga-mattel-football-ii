"""Platform icon encode/decode must round-trip losslessly and match the
confirmed on-disk format (see tools/platform_icon.py)."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
from PIL import Image
from platform_icon import (
    LANDSCAPE_SIZE,
    PORTRAIT_SIZE,
    decode_bin_to_landscape,
    encode_landscape_to_bin,
)


def main() -> int:
    # Deterministic synthetic gradient, not a blank/solid image -- a
    # solid-color test would pass even with the width/height transposed,
    # since every pixel is identical either way.
    img = Image.new("L", LANDSCAPE_SIZE)
    px = img.load()
    for x in range(LANDSCAPE_SIZE[0]):
        for y in range(LANDSCAPE_SIZE[1]):
            px[x, y] = (x + y * 3) % 256

    data = encode_landscape_to_bin(img)
    expected_len = PORTRAIT_SIZE[0] * PORTRAIT_SIZE[1] * 2
    assert len(data) == expected_len, f"size: {len(data)} != {expected_len}"

    assert set(data[1::2]) == {0}, "high byte must always be 0"

    decoded = decode_bin_to_landscape(data)
    assert decoded.size == LANDSCAPE_SIZE, decoded.size
    assert list(decoded.getdata()) == list(img.getdata()), "round trip must be lossless"

    # The shipped .bin must match what the generator currently produces, so
    # the checked-in artwork cannot silently drift from the code that made
    # it -- and so the openFPGA template placeholder this core started with
    # cannot survive unnoticed.
    #
    # An earlier version of this also tried "the right third must contain
    # bright pixels, or it is still the placeholder". That check was useless:
    # the placeholder is black-on-WHITE, so its right third is bright and the
    # assertion passed vacuously. The comparison below subsumes it anyway.
    bin_path = (Path(__file__).resolve().parent.parent
                / "dist" / "platforms" / "_images" / "mattel_fb_ii.bin")
    if bin_path.exists():
        shipped = decode_bin_to_landscape(bin_path.read_bytes())
        assert shipped.size == LANDSCAPE_SIZE, shipped.size
        sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
        import gen_platform_icon
        assert list(shipped.getdata()) == list(gen_platform_icon.render().getdata()), \
            "dist/.bin does not match tools/gen_platform_icon.py -- re-run it with --write"

    print("PASS: test_platform_icon")
    return 0


if __name__ == "__main__":
    sys.exit(main())
