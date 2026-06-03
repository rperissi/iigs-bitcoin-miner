#!/usr/bin/env python3
"""
spec.py - PALETTE CONTRACT v3 (Tier 1: 16-zone hybrid).

The SHR SCB picks one of 16 palettes per scanline. v2 wasted that by using only 4
(one per band) -> the whole band was literally 16 colours -> flat "8-bit" look.

v3 uses all 16 palettes as ZONES stacked down the screen. Within each zone:
  * a small set of band-fixed INK indices hold constant colours (so the C code can
    draw text/values/bars and always know "index 11 = cyan" anywhere in that band);
  * the remaining indices hold a GREY RAMP recomputed from THAT zone's local metal
    gradient -> ~10 fresh greys per ~12 lines -> hundreds of greys down the panel ->
    smooth metal + bevels, while ink stays code-addressable.

Colours are 4-bit (0..15)/channel. build_zone_palettes() reads the frame to build
the ramps; quantize() snaps an image to the per-line palettes. Both the converter
and the Mac render-proof import these so the preview equals real GS output.

C ink constants (mirror in miner/contract.h via gen_contract_c.py):
  all bands : C_BLACK=0  C_WHITE=15
  header(0) : H_BLUE=10 H_CYAN=11 H_GREEN=12 H_RED=13 H_GOLD=14
  wallet(1) : W_BLUE=11 W_CYAN=12
  mid(2)    : M_LED=11  M_RB0=8 (spectrum 8..14, 7 colours; LED reuses green)
  bottom(3) : B_AMBER=11 B_VUG=12 B_VUY=13 B_VUR=14   (hash green = B_VUG)
"""
import numpy as np

W, H = 320, 200

# ---- ink colours (4-bit) ----
BLACK = (0, 0, 0)
WHITE = (15, 15, 15)
BLUE = (1, 1, 7)
CYAN = (6, 14, 15)
GREEN = (2, 15, 4)
GREEN_DK = (1, 8, 2)   # darker green for the TCP-active lamp pulse (header band idx 9)
RED = (15, 2, 2)
GOLD = (15, 11, 2)
LED = (1, 11, 2)
AMBER = (15, 11, 2)
VUG = (2, 15, 4)
VUY = (15, 15, 2)
VUR = (15, 3, 2)
# 7-colour SETI@home-style thermal spectrum, cool (low) -> hot (high).
# GREY IS THE PRIORITY: the mid band keeps the FULL 7-step FTA metal ramp, and the
# green LED reuses spectrum index 11 so the rainbow costs no extra grey slots.
SPECTRA = [
    (1, 1, 9),     # 8  deep blue
    (2, 8, 15),    # 9  blue
    (2, 13, 15),   # 10 cyan
    (4, 15, 4),    # 11 green  (= M_LED)
    (14, 15, 2),   # 12 yellow
    (15, 8, 1),    # 13 orange
    (15, 2, 2),    # 14 red
]
RB6 = SPECTRA          # back-compat alias for older renderers

# ---- per-band ink layout: {index: colour} fixed + list of grey indices ----
_RB = {8 + i: SPECTRA[i] for i in range(len(SPECTRA))}   # spectrum at 8..14
BANDS = {
    "H": ({0: BLACK, 9: GREEN_DK, 10: BLUE, 11: CYAN, 12: GREEN, 13: RED, 14: GOLD, 15: WHITE},
          [1, 2, 3, 4, 5, 6, 7, 8]),   # idx 9 reassigned grey->dark green (TCP lamp pulse)
    "W": ({0: BLACK, 11: BLUE, 12: CYAN, 15: WHITE},
          [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 13, 14]),
    "M": ({0: BLACK, 15: WHITE, **_RB},      # M_LED = spectrum green (index 11)
          [1, 2, 3, 4, 5, 6, 7]),            # full 7-step FTA metal ramp restored
    "B": ({0: BLACK, 11: AMBER, 12: VUG, 13: VUY, 14: VUR, 15: WHITE},
          [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]),
}


# ---- CONFIG page: uniform ink in EVERY zone (a form needs blue LCDs / cyan /
# green / red / amber at any row, not the main page's band-by-row ink). Greys are
# still recomputed per zone so the metal stays smooth. Mirror in contract_cfg.h.
CFG_INK = {0: BLACK, 10: BLUE, 11: CYAN, 12: GREEN, 13: RED, 14: AMBER, 15: WHITE}
CFG_GREYS = [1, 2, 3, 4, 5, 6, 7, 8, 9]


def build_uniform_palettes(frame, ink=None, greys=None, nz=16):
    """16 equal horizontal zones, IDENTICAL ink in each, per-zone grey ramp from
    the local metal gradient. -> (pals[nz][16] 4-bit, scb[H]). Same ramp logic as
    build_zone_palettes (reserve true min/max, pack middle by population)."""
    if ink is None:
        ink = CFG_INK
    if greys is None:
        greys = CFG_GREYS
    f = frame.astype(np.int32)
    lum = f @ np.array([0.299, 0.587, 0.114])
    sat = f.max(2) - f.min(2)
    is_metal = (sat < 26) & (lum >= 30)
    edges = np.linspace(0, H, nz + 1).round().astype(int)
    pals, scb = [], np.zeros(H, np.uint8)
    n = len(greys)
    for zid in range(nz):
        y0, y1 = int(edges[zid]), int(edges[zid + 1])
        m = lum[y0:y1][is_metal[y0:y1]]
        if m.size >= 8:
            inner = np.percentile(m, np.linspace(14, 86, max(n - 2, 1)))
            ramp = np.sort(np.concatenate(([m.min()], inner, [m.max()])))[:n]
        else:
            ramp = np.linspace(48.0, 210.0, n)
        pal = [(0, 0, 0)] * 16
        for idx, col in ink.items():
            pal[idx] = col
        for k, idx in enumerate(greys):
            v = int(round(float(ramp[k]) / 17.0))
            pal[idx] = (v, v, v)
        pals.append(pal)
        scb[y0:y1] = zid
    return pals, scb


def _zones():
    """16 contiguous zones (band, y0, y1). 3 header + 1 wallet + 8 mid + 4 bottom."""
    z = []
    for band, a, b, n in (("H", 0, 50, 3), ("W", 50, 67, 1),
                          ("M", 67, 153, 8), ("B", 153, 200, 4)):
        edges = np.linspace(a, b, n + 1).round().astype(int)
        for k in range(n):
            z.append((band, int(edges[k]), int(edges[k + 1])))
    return z


ZONES = _zones()
assert len(ZONES) == 16, len(ZONES)


def build_zone_palettes(frame):
    """frame: (H,W,3) uint8 of the clean plate. -> (pals[16][16] 4-bit, scb[H])."""
    f = frame.astype(np.int32)
    lum = f @ np.array([0.299, 0.587, 0.114])
    sat = f.max(2) - f.min(2)
    is_metal = (sat < 26) & (lum >= 30)
    pals, scb = [], np.zeros(H, np.uint8)
    for zid, (band, y0, y1) in enumerate(ZONES):
        ink, greys = BANDS[band]
        m = lum[y0:y1][is_metal[y0:y1]]
        # RESERVE the true min/max (bevel shadow + specular highlight) as the ramp
        # endpoints so high-contrast edges survive; pack the middle by population so
        # the broad faces stay smooth. This is what lets the metal read as 3D.
        n = len(greys)
        if m.size >= 8:
            inner = np.percentile(m, np.linspace(14, 86, max(n - 2, 1)))
            ramp = np.sort(np.concatenate(([m.min()], inner, [m.max()])))[:n]
        else:
            ramp = np.linspace(48.0, 210.0, n)
        pal = [(0, 0, 0)] * 16
        for idx, col in ink.items():
            pal[idx] = col
        for k, idx in enumerate(greys):
            v = int(round(float(ramp[k]) / 17.0))
            pal[idx] = (v, v, v)
        pals.append(pal)
        scb[y0:y1] = zid
    return pals, scb


# 4x4 ordered (Bayer) dither matrix, normalised to (-0.5..+0.5)
_BAYER = (np.array([[0, 8, 2, 10], [12, 4, 14, 6],
                    [3, 11, 1, 9], [15, 7, 13, 5]], float) + 0.5) / 16.0 - 0.5


def quantize(image, pals, scb, frame=None, dither=False):
    """Snap (H,W,3) uint8 to per-line palettes.

    Metal pixels (low-saturation frame areas that the code did NOT overdraw) are
    DITHERED between the two neighbouring grey-ramp entries -> smooth gradients.
    Everything else (inks, text, wells) snaps to the nearest palette entry.
    -> (preview uint8, idx uint8 (H,W)).
    """
    if frame is None:
        frame = image
    pal8 = [np.array([(r * 17, g * 17, b * 17) for (r, g, b) in p], np.int32)
            for p in pals]
    # per-zone neutral (grey) ramp: values + palette indices, sorted by brightness
    ramps = []
    for p in pals:
        ne = [(sum(c) * 17 // 3, i) for i, c in enumerate(p)
              if max(c) - min(c) <= 2]   # near-neutral (steel-tinted) greys
        ne.sort()
        ramps.append((np.array([v for v, _ in ne]),
                      np.array([i for _, i in ne], np.uint8)))
    f = frame.astype(np.int32)
    fl = f @ np.array([0.299, 0.587, 0.114])
    fsat = f.max(2) - f.min(2)
    metal = (fsat < 26) & (fl >= 22) & np.all(image == frame, axis=2)
    img = image.astype(np.int32)
    out = np.zeros_like(image)
    idx = np.zeros((H, W), np.uint8)
    for y in range(H):
        p = pal8[scb[y]]
        d = ((img[y][:, None, :] - p[None, :, :]) ** 2).sum(2)
        ii = d.argmin(1).astype(np.uint8)
        rv, ri = ramps[scb[y]]
        if dither and rv.size >= 2:
            row_metal = np.where(metal[y])[0]
            if row_metal.size:
                L = fl[y, row_metal]
                hi = np.clip(np.searchsorted(rv, L), 1, rv.size - 1)
                lo = hi - 1
                span = np.maximum(rv[hi] - rv[lo], 1)
                frac = (L - rv[lo]) / span
                thr = _BAYER[y & 3, row_metal & 3] + 0.5
                ii[row_metal] = np.where(frac > thr, ri[hi], ri[lo])
        idx[y] = ii
        out[y] = p[ii]
    return out, idx
