#!/usr/bin/env python3
"""
config2shr.py - convert the procedural CONFIG chassis (config_frame.png) to an SHR
blob, byte-identical in layout to png2shr.py / frame2shr_contract.py so miner/viz.c
loads it unchanged (32000 pix + 200 scb + 512 pal).

Difference from frame2shr_contract.py: the config page needs UNIFORM ink in every
scanline (blue LCD / cyan / green / red / amber usable at any row of the form), so
it quantises through spec.build_uniform_palettes (CFG_INK) instead of the main
panel's per-band zone contract. Mirror the ink slots in miner/contract_cfg.h.

Usage: python3 config2shr.py [INPUT.png] [OUTPUT.shr]   (defaults: config_frame.png config.shr)
"""
import sys
import numpy as np
from PIL import Image
import spec as S

W, H = 320, 200
NPAL, NCOL = 16, 16


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else "config_frame.png"
    out = sys.argv[2] if len(sys.argv) > 2 else "config.shr"
    im = Image.open(src).convert("RGB")
    if im.size != (W, H):
        im = im.resize((W, H), Image.LANCZOS)
    arr = np.array(im, np.uint8)

    pals, scb = S.build_uniform_palettes(arr)
    preview, idx = S.quantize(arr, pals, scb)

    pix = np.zeros(H * 160, np.uint8)
    for y in range(H):
        row = idx[y]
        pix[y * 160:(y + 1) * 160] = (row[0::2] << 4) | row[1::2]

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
    print(f"wrote {out} ({len(blob)} bytes); SCB zones: {sorted(set(scb.tolist()))}")


if __name__ == "__main__":
    main()
