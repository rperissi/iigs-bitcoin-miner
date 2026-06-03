#!/usr/bin/env python3
"""
png2shr.py - convert a PNG mockup into an Apple IIgs Super Hi-Res image.

Output is a raw 32712-byte SHR blob in our own simple layout:
    [0      .. 31999]  pixel data   (320x200, 4bpp, 2 px/byte, hi nibble = left)
    [32000  .. 32199]  SCBs         (one per scanline; low nibble = palette 0..15)
    [32200  .. 32711]  palettes     (16 palettes x 16 colours x 2 bytes)

Palette colour word is $0RGB (4 bits/chan), little-endian:
    byte0 = (g<<4)|b ,  byte1 = r        (r,g,b in 0..15)

SHR gives 16 colours per scanline, with each line picking one of 16 palettes via
its SCB. We generate 16 candidate palettes from 16 horizontal bands of the image,
then assign every row its best-fitting palette and quantise to it.

Also writes <out>_preview.png: the image decoded back through the 4-bit palettes,
i.e. a faithful simulation of what the GS will actually display.

Usage: python3 png2shr.py INPUT.png OUTPUT.shr [--dither]
"""
import sys
import numpy as np
from PIL import Image, ImageEnhance, ImageFilter

W, H = 320, 200
NPAL = 16          # palettes available (SCB low nibble)
NCOL = 16          # colours per palette (320 mode)

# punch defaults (compensate for 4-bit/16-colour dullness, high-contrast snap)
SAT = 1.45         # saturation boost (punchier red logo / blue LCD / green LED)
CON = 1.10         # contrast boost
SHARP = 0          # unsharp-mask percent (0=off) - OFF: was amplifying metal grain
SMOOTH = 0.4       # gaussian blur radius to tame high-freq grain (lower = sharper)
GAMMA = 1.0        # neutral: tone driven by the SOURCE art, not the converter


def median_cut_palette(pixels, ncol=NCOL):
    """pixels: (N,3) uint8 -> (<=ncol,3) uint8 palette via PIL median cut."""
    if len(pixels) == 0:
        return np.zeros((1, 3), np.uint8)
    im = Image.fromarray(pixels.reshape(-1, 1, 3).astype(np.uint8), "RGB")
    q = im.quantize(colors=ncol, method=Image.MEDIANCUT)
    pal = np.array(q.getpalette()[: ncol * 3], np.uint8).reshape(-1, 3)
    # how many colours actually used
    used = len(set(q.getdata()))
    return pal[:max(used, 1)]


def snap4(rgb):
    """8-bit rgb -> 4-bit (0..15) snapped, returned as 4-bit ints."""
    return np.clip((rgb.astype(np.int32) + 8) // 17, 0, 15).astype(np.uint8)


def expand4(rgb4):
    """4-bit (0..15) -> 8-bit for preview/distance."""
    return (rgb4.astype(np.int32) * 17).astype(np.uint8)


def _pad16(pal):
    if len(pal) < NCOL:
        pal = np.vstack([pal, np.tile(pal[-1], (NCOL - len(pal), 1))])
    return pal[:NCOL]


def _row_feature(arr, y):
    """sorted (by luma) 16-colour palette of one row, flattened to (48,)."""
    pal = median_cut_palette(arr[y], NCOL).astype(np.float32)
    luma = pal @ np.array([0.299, 0.587, 0.114], np.float32)
    pal = pal[np.argsort(luma)]
    return _pad16(pal).reshape(-1)


def _kmeans(feats, k, iters=24, seed=0):
    rng = np.random.default_rng(seed)
    cent = feats[rng.choice(len(feats), k, replace=False)].copy()
    assign = np.zeros(len(feats), int)
    for _ in range(iters):
        d = ((feats[:, None, :] - cent[None, :, :]) ** 2).sum(2)
        new = d.argmin(1)
        if np.array_equal(new, assign):
            break
        assign = new
        for c in range(k):
            m = assign == c
            if m.any():
                cent[c] = feats[m].mean(0)
    return assign


def build_palettes(arr):
    """Content-aware: per-row palettes -> k-means(16) -> palette per cluster."""
    feats = np.stack([_row_feature(arr, y) for y in range(H)])
    assign = _kmeans(feats, NPAL)
    palettes = []
    for c in range(NPAL):
        rows = np.where(assign == c)[0]
        if len(rows) == 0:
            palettes.append(snap4(median_cut_palette(arr.reshape(-1, 3), NCOL)))
            palettes[-1] = _pad16(palettes[-1])
            continue
        px = arr[rows].reshape(-1, 3)
        palettes.append(_pad16(snap4(median_cut_palette(px, NCOL))))
    return palettes  # list of (16,3) 4-bit


def quantize_row(row8, pal4, dither):
    """row8:(W,3) uint8 ; pal4:(16,3) 4-bit -> indices (W,) and decoded (W,3) 8-bit."""
    pal8 = expand4(pal4).astype(np.int32)        # (16,3)
    work = row8.astype(np.float32).copy()
    idx = np.zeros(W, np.uint8)
    dec = np.zeros((W, 3), np.uint8)
    err = np.zeros(3, np.float32)
    for x in range(W):
        p = work[x] + (err if dither else 0)
        d = ((pal8 - p) ** 2).sum(1)
        k = int(d.argmin())
        idx[x] = k
        dec[x] = pal8[k]
        if dither:
            err = (p - pal8[k]) * 0.5    # simple serpentine-ish row carry
    return idx, dec


def best_palette_for_row(row8, palettes):
    """pick palette index with lowest nearest-colour error for this row."""
    best_i, best_e = 0, None
    for i, pal4 in enumerate(palettes):
        pal8 = expand4(pal4).astype(np.int32)
        # distance of each pixel to nearest palette colour
        d = ((row8.astype(np.int32)[:, None, :] - pal8[None, :, :]) ** 2).sum(2)
        e = d.min(1).sum()
        if best_e is None or e < best_e:
            best_i, best_e = i, e
    return best_i


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    src, out = sys.argv[1], sys.argv[2]
    dither = "--dither" in sys.argv[3:]

    im = Image.open(src).convert("RGB").resize((W, H), Image.LANCZOS)
    if SMOOTH > 0:
        im = im.filter(ImageFilter.GaussianBlur(SMOOTH))
    if SHARP > 0:
        im = im.filter(ImageFilter.UnsharpMask(radius=1.2, percent=SHARP, threshold=2))
    im = ImageEnhance.Color(im).enhance(SAT)
    im = ImageEnhance.Contrast(im).enhance(CON)
    if GAMMA != 1.0:
        lut = [int(((i / 255.0) ** GAMMA) * 255 + 0.5) for i in range(256)] * 3
        im = im.point(lut)
    arr = np.array(im, np.uint8)

    palettes = build_palettes(arr)

    pix = np.zeros(H * 160, np.uint8)
    scb = np.zeros(H, np.uint8)
    preview = np.zeros((H, W, 3), np.uint8)

    for y in range(H):
        row = arr[y]
        pi = best_palette_for_row(row, palettes)
        scb[y] = pi
        idx, dec = quantize_row(row, palettes[pi], dither)
        preview[y] = dec
        # pack 2 px/byte, hi nibble = left pixel
        hi = idx[0::2]
        lo = idx[1::2]
        pix[y * 160:(y + 1) * 160] = (hi << 4) | lo

    # palette bytes: 16 pal x 16 col x 2 bytes, little-endian $0RGB
    palbytes = np.zeros(NPAL * NCOL * 2, np.uint8)
    for i, pal4 in enumerate(palettes):
        for c in range(NCOL):
            r, g, b = int(pal4[c][0]), int(pal4[c][1]), int(pal4[c][2])
            off = (i * NCOL + c) * 2
            palbytes[off] = (g << 4) | b
            palbytes[off + 1] = r

    blob = bytes(pix) + bytes(scb) + bytes(palbytes)
    assert len(blob) == 32000 + 200 + 512, len(blob)
    with open(out, "wb") as f:
        f.write(blob)

    Image.fromarray(preview, "RGB").save(out + "_preview.png")
    # also a 2x preview for easier eyeballing
    Image.fromarray(preview, "RGB").resize((W * 2, H * 2), Image.NEAREST).save(out + "_preview2x.png")
    print(f"wrote {out} ({len(blob)} bytes), palettes used by rows: "
          f"{sorted(set(scb.tolist()))}")
    print(f"preview: {out}_preview.png  (and _preview2x.png)")


if __name__ == "__main__":
    main()
