#!/usr/bin/env python3
"""
logo_depth_proof.py - chiseled GS MINER wordmark proof (depth / shadow / bevel).

Renders at the exact SHR logo-well footprint from build_frame.py:
  recess(14, 6, 82, 30)  ->  82 x 30 pixels @ 320x200

Usage:
    python3 viz/logo_depth_proof.py
    python3 viz/logo_depth_proof.py --compare
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

import font4x6 as F4
import font5x7 as F5
import spec

ROOT = Path(__file__).resolve().parent.parent
BRANDING = ROOT / "branding"
VIZ = Path(__file__).resolve().parent
VIZ_C = ROOT / "miner" / "viz.c"

# ---- frame contract (build_frame.py) ----
LOGO_X, LOGO_Y, LOGO_W, LOGO_H = 14, 6, 85, 30   # well widened 82->85 (right edge 99)
SCREEN_W, SCREEN_H = 320, 200

REF_CANDIDATES = [
    BRANDING / "reference_gs_miner_wordmark.png",
    BRANDING / "nameplate_idea_01_chrome.png",
]

RGB = {
    "shadow": (0, 0, 0),
    "outline": (0, 0, 0),
    "bevel_hi": (200, 202, 208),   # light grey top bevel (was white)
    "bevel_lo": (110, 112, 118),
    "red": (225, 45, 45),
    "red_lo": (120, 20, 20),
    "highlight": (200, 202, 208),  # light grey cap glint (was white)
    "ver": (108, 110, 114),        # subtle dark grey version stamp
}

PAL = {
    "shadow": 0,
    "outline": 0,
    "bevel_hi": 7,       # light grey from metal ramp
    "bevel_lo": 5,
    "red": 13,
    "red_lo": 0,
    "highlight": 7,      # light grey cap glint
    "ver": 4,            # dark grey version stamp
}

HEADER_PAL_4 = [
    (0, 0, 0), (2, 2, 2), (3, 3, 3), (4, 4, 4), (5, 5, 5), (6, 6, 6),
    (7, 7, 7), (8, 8, 8), (9, 9, 9),
    spec.GREEN_DK, spec.BLUE, spec.CYAN, spec.GREEN, spec.RED, spec.GOLD, spec.WHITE,
]


def pal_rgb(idx: int) -> tuple[int, int, int]:
    return tuple(min(255, int(x) * 17) for x in HEADER_PAL_4[idx])


def colors_for(mode: str) -> dict:
    if mode == "rgb":
        return dict(RGB)
    return {k: pal_rgb(v) for k, v in PAL.items()}


def gold_colors(base: dict) -> dict:
    """GS palette in Bitcoin-coin gold/amber (ties the wordmark to the coin badge)."""
    g = dict(base)
    g["red"] = (238, 176, 34)
    g["red_lo"] = (150, 104, 16)
    g["highlight"] = (248, 224, 150)
    return g


# ---- in-app (SHR header-band) palette indices, for the C blit (miner/logo_gs.h) ----
# Header band palette (spec.py BANDS["H"] / miner/contract.h):
#   0=black  1..8=grey ramp  9=green_dk 10=blue 11=cyan 12=green 13=RED 14=GOLD 15=white
# No dark-red/dark-gold exists here, so depth comes from the black outline + grey bevel
# (the body is flat). 255 = transparent (the panel recess shows through).
IDX_TRANSPARENT = 255
IDX_RED = {                      # red MINER (and red GS in the all-red variant)
    "shadow": 0, "outline": 0, "bevel_hi": 8, "bevel_lo": 5,
    "red": 13, "red_lo": 2, "highlight": 8, "ver": 6,
}
IDX_GOLD = {                     # gold coin GS
    "shadow": 0, "outline": 0, "bevel_hi": 8, "bevel_lo": 5,
    "red": 14, "red_lo": 14, "highlight": 8, "ver": 6,
}
IDX_MINER = {                    # red MINER: solid red body, single subtle grey edge,
    "shadow": 0, "outline": 0,   # NO internal top/bottom grey bands (was too striped)
    "bevel_hi": 6, "bevel_lo": 6,
    "red": 13, "red_lo": 13, "highlight": 13, "ver": 6,
}


def render_well_idx(gold_gs: bool = True) -> np.ndarray:
    """
    (H,W) uint8 palette-index image of the wordmark for the in-app blit. Transparent
    background (255), NO version stamp (viz.c draws the dynamic APPVER). Geometry is
    identical to render_well() so it matches the approved proof pixel-for-pixel.
    """
    w, h = LOGO_W, LOGO_H
    arr = np.full((h, w), IDX_TRANSPARENT, np.uint8)
    miner_colors = IDX_MINER if gold_gs else IDX_RED
    gs_colors = IDX_GOLD if gold_gs else IDX_RED
    # nudge the whole wordmark a few px down so it sits centred in the well
    y0 = 3
    # natural spacing (sp=1, gap=-2); the widened 85px well gives MINER's "R" room
    embossed_word(
        arr, 2, y0, "MINER", miner_colors,
        gs_scale=3, gs_slant=1, gs_gap=None,
        miner_scale=2, miner_sp=1, shadow_dx=1, shadow_dy=1,
        gs_miner_gap=-2, gs_colors=gs_colors,
    )
    return arr


def _band_idx_rgb(idx: int) -> tuple[int, int, int]:
    """Approximate header-band palette index -> RGB (preview only)."""
    if idx == IDX_TRANSPARENT:
        return (48, 48, 52)
    if idx == 0:
        return (0, 0, 0)
    if idx == 15:
        return (255, 255, 255)
    if idx == 13:
        return (255, 34, 34)
    if idx == 14:
        return (255, 187, 34)
    if idx == 9:
        return (17, 136, 34)
    if 1 <= idx <= 8:
        ramp = np.linspace(48, 210, 8)
        v = int(ramp[idx - 1])
        return (v, v, v)
    return (40, 40, 44)


def band_preview(arr_idx: np.ndarray) -> Image.Image:
    h, w = arr_idx.shape
    out = np.zeros((h, w, 3), np.uint8)
    for y in range(h):
        for x in range(w):
            out[y, x] = _band_idx_rgb(int(arr_idx[y, x]))
    return Image.fromarray(out)


def emit_c_header(arr_idx: np.ndarray, path: Path, gold_gs: bool) -> None:
    h, w = arr_idx.shape
    lines = []
    lines.append("/* logo_gs.h - GENERATED by viz/logo_depth_proof.py (do not hand-edit).")
    lines.append(" * Locked GS MINER wordmark as SHR header-band palette indices for the")
    lines.append(" * in-app logo well. %s GS + red MINER." % ("gold" if gold_gs else "red"))
    lines.append(" * 0=black 1..8=grey 13=red 14=gold 15=white; 255 = transparent.")
    lines.append(" * Version is drawn dynamically by viz.c (APPVER), NOT baked here. */")
    lines.append("#ifndef LOGO_GS_H")
    lines.append("#define LOGO_GS_H")
    lines.append("#define LOGO_GS_X    %d" % LOGO_X)
    lines.append("#define LOGO_GS_Y    %d" % LOGO_Y)
    lines.append("#define LOGO_GS_W    %d" % w)
    lines.append("#define LOGO_GS_H_PX %d" % h)
    lines.append("#define LOGO_GS_NONE 255")
    lines.append("static const unsigned char LOGO_GS[%d] = {" % (w * h))
    flat = arr_idx.reshape(-1)
    for i in range(0, len(flat), 20):
        chunk = ",".join("%3d" % int(v) for v in flat[i:i + 20])
        lines.append("    " + chunk + ",")
    lines.append("};")
    lines.append("#endif /* LOGO_GS_H */")
    path.write_text("\n".join(lines) + "\n")
    print(f"  {path}  ({w}x{h} indices, {'gold' if gold_gs else 'red'} GS)")


def read_appver() -> str:
    """#define APPVER from miner/viz.c (same source inject_gsminer.sh uses)."""
    if VIZ_C.exists():
        m = re.search(r'#define\s+APPVER\s+"([^"]+)"', VIZ_C.read_text())
        if m:
            return m.group(1)
    return "V0.93"


def glyph_bmp(font_mod, ch: str) -> np.ndarray:
    ch = ch.upper() if ch.upper() in font_mod._BMP else " "
    return font_mod._BMP[ch]


def stamp_layer(
    arr: np.ndarray,
    x: int,
    y: int,
    text: str,
    color,
    font_mod,
    scale: int = 1,
    sp: int = 1,
    slant: int = 0,
    dilate: int = 0,
) -> None:
    H, W = arr.shape[:2]
    gw, gh = font_mod.GW, font_mod.GH
    cx = x
    for ch in text:
        bmp = glyph_bmp(font_mod, ch)
        for gy in range(gh):
            shear = slant * (gh - 1 - gy) if slant else 0
            for gx in range(gw):
                if not bmp[gy, gx]:
                    continue
                px0 = cx + gx * scale + shear
                py0 = y + gy * scale
                for dy in range(-dilate, dilate + 1):
                    for dx in range(-dilate, dilate + 1):
                        for sy in range(scale):
                            for sx in range(scale):
                                px, py = px0 + dx + sx, py0 + dy + sy
                                if 0 <= py < H and 0 <= px < W:
                                    arr[py, px] = color
        cx += gw * scale + sp


def collect_mask(
    x: int, y: int, text: str, font_mod, scale: int, sp: int, slant: int,
    w: int, h: int,
) -> np.ndarray:
    mask = np.zeros((h, w), bool)
    gw, gh = font_mod.GW, font_mod.GH
    cx = x
    for ch in text:
        bmp = glyph_bmp(font_mod, ch)
        for gy in range(gh):
            shear = slant * (gh - 1 - gy) if slant else 0
            for gx in range(gw):
                if not bmp[gy, gx]:
                    continue
                px0 = cx + gx * scale + shear
                py0 = y + gy * scale
                for sy in range(scale):
                    for sx in range(scale):
                        px, py = px0 + sx, py0 + sy
                        if 0 <= py < h and 0 <= px < w:
                            mask[py, px] = True
        cx += gw * scale + sp
    return mask


def text_width(font_mod, text: str, scale: int, sp: int = 1) -> int:
    if not text:
        return 0
    gw = font_mod.GW
    return len(text) * (gw * scale + sp) - sp


def draw_ver_stamp(arr: np.ndarray, x_right: int, y: int, ver: str, color) -> None:
    """Right-aligned version tag (bottom-right of logo well)."""
    tw = text_width(F4, ver, 1, 1)
    stamp_layer(arr, x_right - tw, y, ver, color, F4, 1, 1, 0, 0)


def embossed_part(
    arr: np.ndarray,
    tx: int,
    ty: int,
    text: str,
    font_mod,
    colors: dict,
    scale: int,
    sp: int,
    slant: int,
    shdx: int,
    shdy: int,
    hi_rows: int = 2,
    lo_rows: int = 2,
) -> None:
    """
    hi_rows / lo_rows: thickness (in glyph rows) of the top highlight band and
    bottom shadow band. The big chunky GS uses 2/2; small text (MINER) reads far
    cleaner with a thin 1-row band so most of each letter stays solid colour
    instead of being sliced into grey/red/dark stripes.
    """
    H, W = arr.shape[:2]
    stamp_layer(arr, tx + shdx, ty + shdy, text, colors["shadow"],
                font_mod, scale, sp, slant, dilate=1)
    stamp_layer(arr, tx, ty, text, colors["outline"],
                font_mod, scale, sp, slant, dilate=1)
    outer = collect_mask(tx, ty, text, font_mod, scale, sp, slant, W, H)
    core = collect_mask(tx + 1, ty + 1, text, font_mod, scale, sp, slant, W, H)
    ring = outer & ~core
    arr[ring] = colors["bevel_lo"]
    arr[core] = colors["red"]
    for py in range(H):
        for px in range(W):
            if not core[py, px]:
                continue
            rel_y = py - ty
            if rel_y < scale * hi_rows:
                arr[py, px] = colors["highlight"]
            elif rel_y >= scale * (font_mod.GH - lo_rows):
                arr[py, px] = colors["red_lo"]
    for py in range(H):
        for px in range(W):
            if ring[py, px] and py > 0 and outer[py - 1, px]:
                arr[py, px] = colors["bevel_hi"]


def gs_bottom_seam_pixels(tx: int, ty: int, scale: int, slant: int) -> list[tuple[int, int]]:
    """
    Black wedge that splits the fused G|S bottom bar into two separate feet
    (matches the user's ref edit3). The cut sits at the G|S glyph boundary
    (G = glyph cols 0-4, S = 5-9 -> boundary px = tx + 5*scale), is `scale`
    px wide, and runs the full depth of the bottom (gy=6) block plus the +1
    shadow row so it reaches the baseline outline. The top row is inset 1 px
    to follow the italic slant.
    """
    junction = tx + F5.GW * scale          # G|S boundary at the bottom (shear=0 there)
    top = ty + (F5.GH - 1) * scale         # start of the gy=6 block
    bot = ty + F5.GH * scale               # include the +1 shadow row -> baseline
    pts = []
    for y in range(top, bot + 1):
        inset = (slant if y < top + scale else 0)   # slight slant at the very top
        for x in range(junction + inset, junction + scale):
            pts.append((y, x))
    # Forward-slant shave on the S-foot top-left: trim a small right-leaning
    # triangle so the top of the S's bottom stroke follows the italic slant
    # (user ref tweak). Deepest at the top row, tapering down over `scale` rows.
    foot_left = junction + scale - 1
    for k in range(scale):
        y = top + k
        for x in range(foot_left, foot_left + (scale - k)):
            pts.append((y, x))
    return pts


def carve_gs_bottom_notch(
    arr: np.ndarray,
    colors: dict,
    tx: int,
    ty: int,
    scale: int,
    slant: int,
) -> None:
    """
    Split the bottom junction between G and S into two feet by blacking out
    the seam pixels with the outline colour (the user blacked these out in
    their edit). G+S stay fused above; only the shared bottom bar is cut.
    """
    H, W = arr.shape[:2]
    for py, px in gs_bottom_seam_pixels(tx, ty, scale, slant):
        if 0 <= py < H and 0 <= px < W:
            arr[py, px] = colors["outline"]


def embossed_gs_mono(
    arr: np.ndarray,
    x: int,
    y: int,
    colors: dict,
    scale: int,
    slant: int,
    shdx: int,
    shdy: int,
) -> int:
    """Monolithic italic GS with bottom-center notch (drawn-example layout)."""
    embossed_part(arr, x, y, "GS", F5, colors, scale, 0, slant, shdx, shdy)
    carve_gs_bottom_notch(arr, colors, x, y, scale, slant)
    return text_width(F5, "GS", scale, 0) + slant * (F5.GH - 1)


def embossed_gs_pair(
    arr: np.ndarray,
    x: int,
    y: int,
    colors: dict,
    scale: int,
    slant: int,
    gap: int,
    shdx: int,
    shdy: int,
) -> int:
    """Large italic G + S with a visible gap between the letterforms."""
    g_w = text_width(F5, "G", scale, 0) + slant * (F5.GH - 1)
    embossed_part(arr, x, y, "G", F5, colors, scale, 0, slant, shdx, shdy)
    sx = x + g_w + gap
    embossed_part(arr, sx, y, "S", F5, colors, scale, 0, slant, shdx, shdy)
    s_w = text_width(F5, "S", scale, 0) + slant * (F5.GH - 1)
    return sx + s_w


def embossed_word(
    arr: np.ndarray,
    x: int,
    y: int,
    miner_text: str,
    colors: dict,
    gs_scale: int = 3,
    gs_slant: int = 1,
    gs_gap: int | None = None,
    miner_scale: int = 2,
    miner_sp: int = 1,
    shadow_dx: int = 1,
    shadow_dy: int = 1,
    gs_miner_gap: int = 1,
    gs_colors: dict | None = None,
) -> int:
    """
    Drawn-example layout (gs_gap=None): monolithic italic "GS" + "MINER".
    gs_gap>=0 splits G/S with that many pixels between (for later tweaking).

    miner_sp puts 1 px between MINER letters so each reads distinctly; gs_miner_gap
    tightens (negative) the space between GS and MINER to claw that width back.
    gs_colors lets the GS use a different palette than MINER (e.g. gold coin GS).
    """
    gsc = gs_colors or colors
    if gs_gap is None:
        gs_w = embossed_gs_mono(arr, x, y, gsc, gs_scale, gs_slant, shadow_dx, shadow_dy)
    else:
        gs_w = embossed_gs_pair(arr, x, y, gsc, gs_scale, gs_slant, gs_gap, shadow_dx, shadow_dy)
    mx = x + gs_w + gs_miner_gap
    my = y + max(0, (gs_scale * F5.GH - miner_scale * F4.GH) // 2)

    if miner_text:
        embossed_part(arr, mx, my, miner_text, F4, colors, miner_scale, miner_sp, 0,
                      shadow_dx, shadow_dy, hi_rows=1, lo_rows=1)
        return mx + text_width(F4, miner_text, miner_scale, miner_sp)
    return gs_w


def logo_well_bg(w: int = LOGO_W, h: int = LOGO_H) -> np.ndarray:
    """Dark recess fill matching the frame logo well."""
    arr = np.zeros((h, w, 3), np.uint8)
    for y in range(h):
        t = y / max(h - 1, 1)
        g = int(58 * (1 - t) + 32 * t)
        arr[y, :] = (g, g, g + 2)
    arr[0, :] = (96, 98, 102)
    arr[:, 0] = (96, 98, 102)
    arr[-1, :] = (16, 16, 18)
    arr[:, -1] = (16, 16, 18)
    return arr


def render_well(
    palette_mode: str = "rgb",
    gs_scale: int = 3,
    miner_scale: int = 2,
    gs_slant: int = 1,
    gs_gap: int | None = None,
    miner_sp: int = 1,
    gs_miner_gap: int = -2,
    gold_gs: bool = False,
    ver: str | None = None,
) -> np.ndarray:
    """
    Exact 82x30 logo well @ 1:1 pixels. Wordmark fills width; version bottom-right.

    miner_sp=1 + gs_miner_gap=-2 spaces the MINER letters for legibility while
    still fitting the 82px well. gold_gs renders the GS in coin gold.
    """
    w, h = LOGO_W, LOGO_H
    arr = logo_well_bg(w, h)
    colors = colors_for(palette_mode)
    gs_colors = gold_colors(colors) if gold_gs else None
    ver = ver or read_appver()
    ver_h = F4.GH
    ver_y = h - ver_h - 1
    ver_x_right = w - 2

    # Reserve bottom-right corner for version stamp
    word_h = gs_scale * F5.GH + 2
    y0 = max(1, min(2, (h - ver_h - word_h) // 2))

    embossed_word(
        arr, 2, y0, "MINER", colors,
        gs_scale=gs_scale, gs_slant=gs_slant, gs_gap=gs_gap,
        miner_scale=miner_scale, miner_sp=miner_sp,
        shadow_dx=1, shadow_dy=1,
        gs_miner_gap=gs_miner_gap, gs_colors=gs_colors,
    )
    draw_ver_stamp(arr, ver_x_right, ver_y, ver, colors["ver"])
    return arr


def load_frame_rgb() -> np.ndarray | None:
    """frame_proc.png from build_frame.py if present."""
    for path in (VIZ / "frame_proc.png", ROOT / "viz" / "frame_proc.png"):
        if path.exists():
            im = Image.open(path).convert("RGB")
            if im.size != (SCREEN_W, SCREEN_H):
                im = im.resize((SCREEN_W, SCREEN_H), Image.LANCZOS)
            return np.array(im, np.uint8)
    return None


def ensure_frame() -> np.ndarray | None:
    frame = load_frame_rgb()
    if frame is not None:
        return frame
    proc = VIZ / "frame_proc.png"
    try:
        subprocess.run(
            [sys.executable, str(VIZ / "build_frame.py")],
            cwd=str(VIZ), check=True, capture_output=True, timeout=30,
        )
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired):
        return None
    return load_frame_rgb()


def composite_on_frame(well: np.ndarray) -> Image.Image | None:
    frame = ensure_frame()
    if frame is None:
        return None
    out = frame.copy()
    out[LOGO_Y:LOGO_Y + LOGO_H, LOGO_X:LOGO_X + LOGO_W] = well
    return Image.fromarray(out)


def render_large(palette_mode: str = "rgb", ver: str | None = None) -> Image.Image:
    w, h = 320, 80
    arr = np.zeros((h, w, 3), np.uint8)
    for y in range(h):
        g = 40 + y // 3
        arr[y, :] = (g, g, g + 4)
    colors = colors_for(palette_mode)
    ver = ver or read_appver()
    embossed_word(arr, 16, 12, "MINER", colors, gs_scale=4, gs_slant=2, gs_gap=None,
                  miner_scale=2, miner_sp=0, shadow_dx=2, shadow_dy=2)
    draw_ver_stamp(arr, w - 8, h - 10, ver, colors["ver"])
    return Image.fromarray(arr)


def save_png(im: Image.Image, path: Path, zoom: int = 1) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    out = im.resize((im.width * zoom, im.height * zoom), Image.NEAREST) if zoom > 1 else im
    out.save(path)
    print(f"  {path}  ({out.width}x{out.height}{'  native 1:1' if zoom == 1 else f'  {zoom}x zoom'})")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--compare", action="store_true")
    args = ap.parse_args()

    BRANDING.mkdir(parents=True, exist_ok=True)
    ver = read_appver()

    print("=== GS MINER depth logo proof ===")
    print(f"Logo well: {LOGO_W}x{LOGO_H}px @ screen ({LOGO_X},{LOGO_Y})  APPVER={ver}")

    well = render_well("rgb", ver=ver)
    well_iigs = render_well("iigs", ver=ver)
    well_gold = render_well("rgb", gold_gs=True, ver=ver)

    save_png(Image.fromarray(well), BRANDING / "logo_depth_well_rgb.png", zoom=1)
    save_png(Image.fromarray(well), BRANDING / "logo_depth_well_rgb_4x.png", zoom=4)
    save_png(Image.fromarray(well_iigs), BRANDING / "logo_depth_well_iigs.png", zoom=1)
    save_png(Image.fromarray(well_iigs), BRANDING / "logo_depth_well_iigs_4x.png", zoom=4)
    save_png(Image.fromarray(well_gold), BRANDING / "logo_variant_gold_gs.png", zoom=1)
    save_png(Image.fromarray(well_gold), BRANDING / "logo_variant_gold_gs_4x.png", zoom=4)

    frame_rgb = composite_on_frame(well)
    if frame_rgb:
        save_png(frame_rgb, BRANDING / "logo_depth_frame_rgb.png", zoom=1)
        save_png(frame_rgb, BRANDING / "logo_depth_frame_rgb_2x.png", zoom=2)
    else:
        print("  (frame_proc.png not available — run viz/build_frame.py for frame composite)")

    im_large = render_large("rgb", ver)
    save_png(im_large, BRANDING / "logo_depth_large_rgb.png", zoom=2)

    # ---- in-app C blit: emit miner/logo_gs.h + an in-app palette preview ----
    print("\nIn-app (SHR header-band palette) logo:")
    arr_idx = render_well_idx(gold_gs=True)
    emit_c_header(arr_idx, ROOT / "miner" / "logo_gs.h", gold_gs=True)
    prev = band_preview(arr_idx)
    save_png(prev, BRANDING / "logo_inapp_gold_iigs.png", zoom=1)
    save_png(prev, BRANDING / "logo_inapp_gold_iigs_6x.png", zoom=6)
    print("  branding/logo_inapp_gold_iigs_6x.png (what the GS will actually draw)")

    if args.compare:
        ref = next((p for p in REF_CANDIDATES if p.exists()), None)
        if ref:
            ref_im = Image.open(ref).convert("RGB").resize((LOGO_W * 4, LOGO_H * 4), Image.NEAREST)
            ours = Image.fromarray(well).resize((LOGO_W * 4, LOGO_H * 4), Image.NEAREST)
            combo = Image.new("RGB", (LOGO_W * 8 + 8, LOGO_H * 4), (32, 32, 36))
            combo.paste(ref_im, (0, 0))
            combo.paste(ours, (LOGO_W * 4 + 8, 0))
            combo.save(BRANDING / "logo_depth_well_compare.png")
            print(f"  {BRANDING / 'logo_depth_well_compare.png'}")

    print("\nPrimary review: branding/logo_depth_well_rgb.png (82x30 native)")
    print("Zoom:           branding/logo_depth_well_rgb_4x.png")
    print("On frame:       branding/logo_depth_frame_rgb_2x.png")
    print("No inject until you approve.")


if __name__ == "__main__":
    main()
