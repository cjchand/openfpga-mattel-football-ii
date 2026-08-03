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
