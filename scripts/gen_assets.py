#!/usr/bin/env python3
"""Generate placeholder PNG assets required by the VPK build.

Run this script once before building (or let CI run it automatically):
    python3 scripts/gen_assets.py

The PNGs use a simple dark-blue palette matching the shell colour scheme.
They are intentionally minimal solid-colour images — replace them with
real artwork before a public release.
"""

import os
import struct
import zlib


def _make_png(width: int, height: int, r: int, g: int, b: int) -> bytes:
    """Return raw bytes of a valid solid-colour RGB PNG."""
    def _chunk(tag: bytes, data: bytes) -> bytes:
        size = struct.pack(">I", len(data))
        crc  = struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
        return size + tag + data + crc

    signature = b"\x89PNG\r\n\x1a\n"
    ihdr_data = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    ihdr      = _chunk(b"IHDR", ihdr_data)

    # Build raw scanlines: filter byte 0x00 then RGB triples
    row   = b"\x00" + bytes([r, g, b]) * width
    idat  = _chunk(b"IDAT", zlib.compress(row * height, level=9))
    iend  = _chunk(b"IEND", b"")

    return signature + ihdr + idat + iend


# (output_path, width, height, R, G, B)
ASSETS = [
    ("sce_sys/icon0.png",                       128, 128, 0x1E, 0x1E, 0x3C),
    ("sce_sys/livearea/contents/bg.png",         840, 500, 0x10, 0x10, 0x18),
    ("sce_sys/livearea/contents/startup.png",    280, 158, 0x1E, 0x1E, 0x3C),
]


def main() -> None:
    script_dir  = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.dirname(script_dir)

    for rel_path, w, h, r, g, b in ASSETS:
        out_path = os.path.join(project_dir, rel_path)
        os.makedirs(os.path.dirname(out_path), exist_ok=True)
        data = _make_png(w, h, r, g, b)
        with open(out_path, "wb") as fh:
            fh.write(data)
        print(f"  generated  {rel_path}  ({w}×{h})")


if __name__ == "__main__":
    main()
