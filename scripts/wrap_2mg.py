#!/usr/bin/env python3
"""wrap_2mg.py - wrap a raw ProDOS-order disk image in a real 2IMG (.2mg) header.

Why this exists
---------------
Our dev/inject pipeline clones a *headerless* 800K ProDOS-order image but names it
"apps.2mg". Lenient emulators (Ample/MAME) auto-detect the format by content/size, so
the file mounts fine there. Real hardware is stricter: a CFFA3000 asked to mount a file
with a ".2mg" extension REQUIRES the 4-byte "2IMG" magic header - without it you get
"invalid image" (exactly what we hit on iron). Boot disks built by other tools already
carry the header (they start with "2IMG"), which is why miner_boot.2mg mounts and
apps.2mg did not.

This tool prepends a correct 64-byte 2IMG header (ProDOS order, format=1) so the same
disk mounts on CFFA *and* in every emulator.

Usage
-----
    python3 wrap_2mg.py INPUT.po OUTPUT.2mg          # explicit
    python3 wrap_2mg.py apps.2mg                     # in-place-ish: -> apps_cffa.2mg
    python3 wrap_2mg.py --check apps.2mg             # just report header status

If INPUT already has a 2IMG header it is reported and (without --force) left untouched.

2IMG spec: http://apple2.org.za/gswv/a2zine/Docs/DiskImage_2MG_Info.txt
"""
import argparse
import struct
import sys
from pathlib import Path

MAGIC = b"2IMG"
CREATOR = b"GSMN"          # 4-byte creator tag (arbitrary; CFFA ignores it)
HDR_LEN = 64
FMT_PRODOS = 1             # 0=DOS3.3 order, 1=ProDOS order, 2=nibble
BLOCK = 512


def has_2img_header(data: bytes) -> bool:
    return data[:4] == MAGIC


def build_header(data_len: int) -> bytes:
    if data_len % BLOCK != 0:
        raise SystemExit(f"refusing to wrap: {data_len} bytes is not a whole number of "
                         f"{BLOCK}-byte blocks")
    num_blocks = data_len // BLOCK
    h = bytearray(HDR_LEN)
    h[0:4]   = MAGIC
    h[4:8]   = CREATOR
    struct.pack_into("<H", h, 8,  HDR_LEN)       # header length
    struct.pack_into("<H", h, 10, 1)             # version
    struct.pack_into("<I", h, 12, FMT_PRODOS)    # image format = ProDOS order
    struct.pack_into("<I", h, 16, 0)             # flags
    struct.pack_into("<I", h, 20, num_blocks)    # # of 512-byte blocks (ProDOS order)
    struct.pack_into("<I", h, 24, HDR_LEN)       # data offset
    struct.pack_into("<I", h, 28, data_len)      # data length
    # comment / creator-data offsets+lengths and the 16 reserved bytes stay zero
    return bytes(h)


def main() -> int:
    ap = argparse.ArgumentParser(description="wrap a raw ProDOS-order image in a 2IMG header")
    ap.add_argument("input", help="raw ProDOS-order image (.po or headerless .2mg)")
    ap.add_argument("output", nargs="?", help="output .2mg (default: <input>_cffa.2mg)")
    ap.add_argument("--check", action="store_true", help="only report header status")
    ap.add_argument("--force", action="store_true", help="re-wrap even if a header exists")
    args = ap.parse_args()

    src = Path(args.input)
    data = src.read_bytes()

    if has_2img_header(data):
        blocks = struct.unpack_from("<I", data, 20)[0]
        print(f"{src.name}: already has a 2IMG header ({blocks} blocks). "
              f"Mounts on CFFA as-is.")
        if not args.force:
            return 0
        print("  --force: stripping old header and re-wrapping")
        off = struct.unpack_from("<I", data, 24)[0]
        ln  = struct.unpack_from("<I", data, 28)[0]
        data = data[off:off + ln]

    if args.check:
        print(f"{src.name}: NO 2IMG header (raw {len(data)} bytes = {len(data)//BLOCK} "
              f"blocks). CFFA will reject a .2mg without a header.")
        return 0

    out = Path(args.output) if args.output else src.with_name(src.stem + "_cffa.2mg")
    header = build_header(len(data))
    out.write_bytes(header + data)
    print(f"wrote {out} ({len(header) + len(data)} bytes = 64-byte 2IMG header + "
          f"{len(data)//BLOCK} ProDOS blocks)")
    print("  -> mountable on CFFA3000 and every emulator.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
