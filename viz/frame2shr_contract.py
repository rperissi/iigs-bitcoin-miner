#!/usr/bin/env python3
"""
frame2shr_contract.py - convert the locked v3 frame plate to SHR using the FIXED
palette contract (spec.py). SCB per row = its band palette id; rows are quantised
only against that band's 16 colours. Palette slots 0..3 hold the contract; 4..15
mirror slot 0 (unused). Output blob is byte-identical in layout to png2shr.py so
miner/viz.c loads it unchanged.

Usage: python3 frame2shr_contract.py INPUT.png OUTPUT.shr
"""
import sys
import numpy as np
from PIL import Image
import spec as S

W, H = 320, 200
NPAL, NCOL = 16, 16


def main():
    src, out = sys.argv[1], sys.argv[2]
    # procedural plate is already final/clean -> no SAT/blur, just quantise
    im = Image.open(src).convert("RGB")
    if im.size != (W, H):
        im = im.resize((W, H), Image.LANCZOS)
    arr = np.array(im, np.uint8)

    # 16-zone hybrid palettes built from the frame's local gradients
    pals, scb = S.build_zone_palettes(arr)
    preview, idx = S.quantize(arr, pals, scb)

    pix = np.zeros(H * 160, np.uint8)
    for y in range(H):
        row = idx[y]
        pix[y * 160:(y + 1) * 160] = (row[0::2] << 4) | row[1::2]

    # all 16 palette slots are real zones now
    palbytes = np.zeros(NPAL * NCOL * 2, np.uint8)
    for i in range(NPAL):
        for c in range(NCOL):
            r, g, b = pals[i][c]
            off = (i * NCOL + c) * 2
            palbytes[off] = (g << 4) | b
            palbytes[off + 1] = r

    blob = bytes(pix) + bytes(scb) + bytes(palbytes)
    assert len(blob) == 32000 + 200 + 512, len(blob)
    with open(out, "wb") as f:
        f.write(blob)
    Image.fromarray(preview, "RGB").resize((W * 2, H * 2), Image.NEAREST).save(out + "_preview2x.png")
    print(f"wrote {out} ({len(blob)} bytes); SCB palettes used: {sorted(set(scb.tolist()))}")


if __name__ == "__main__":
    main()
