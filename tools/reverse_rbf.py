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
