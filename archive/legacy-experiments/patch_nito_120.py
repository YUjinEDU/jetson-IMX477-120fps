#!/usr/bin/env python3
"""Create an experimental IMX477 NITO variant for the 1332x990@120 mode.

WARNING: This byte-level patch is not a valid JetPack 7.2 NITO authoring
workflow. The resulting file is rejected by Argus as corrupt because NITO
contains integrity/schema data that this script cannot regenerate. Keep this
script only as a record of the failed experiment; do not install its output.

The stock JetPack 7.2 IMX477 NITO contains two mode records.  The second
record is the 1920x1080@60 tuning entry.  This tool changes only that mode
record's matching metadata and leaves the tuning payload untouched.
"""

from pathlib import Path
import struct
import sys


MODE_TABLE_OFFSET = 0x140
SECOND_MODE_OFFSET = 0x15C
MODE_RECORD_SIZE = 24


def u32(data: bytearray, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def f32(data: bytearray, offset: int) -> float:
    return struct.unpack_from("<f", data, offset)[0]


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} INPUT.nito OUTPUT.nito", file=sys.stderr)
        return 2

    source = Path(sys.argv[1])
    output = Path(sys.argv[2])
    data = bytearray(source.read_bytes())

    count = u32(data, MODE_TABLE_OFFSET)
    old = (
        u32(data, SECOND_MODE_OFFSET),
        u32(data, SECOND_MODE_OFFSET + 4),
        u32(data, SECOND_MODE_OFFSET + 8),
        u32(data, SECOND_MODE_OFFSET + 12),
        f32(data, SECOND_MODE_OFFSET + 16),
    )
    expected = (1920, 1080, 4, 10, 60.0)
    if count != 2 or old[:4] != expected[:4] or abs(old[4] - expected[4]) > 0.01:
        raise RuntimeError(f"unexpected stock NITO layout: count={count}, second={old}")

    struct.pack_into("<I", data, MODE_TABLE_OFFSET, 2)
    struct.pack_into("<II", data, SECOND_MODE_OFFSET, 1332, 990)
    struct.pack_into("<II", data, SECOND_MODE_OFFSET + 8, 4, 10)
    struct.pack_into("<f", data, SECOND_MODE_OFFSET + 16, 120.0)
    struct.pack_into("<I", data, SECOND_MODE_OFFSET + 20, 0)

    output.write_bytes(data)
    print(f"created {output} ({len(data)} bytes)")
    print(f"changed second NITO mode: {old} -> (1332, 990, 4, 10, 120.0)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
