#!/usr/bin/env python3
"""
build_logo.py - GS MINER chrome nameplate branding assets.

Generates standalone PNGs for README / GitHub release, plus an in-app preview
composite. Direction: brushed-metal plate, chiseled red "GS" + "MINER", gold
coin, SHA-256d badge — matching branding/nameplate_idea_01_chrome.png.

Usage:
    python3 viz/build_logo.py
"""
from __future__ import annotations

import math
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

import font5x7 as F

ROOT = Path(__file__).resolve().parent.parent
BRANDING = ROOT / "branding"
VIZ = Path(__file__).resolve().parent

# ---- palette (matches spec.py header band) ----
BLACK = (0, 0, 0)
SHADOW = (18, 18, 22)
SILVER = (210, 214, 222)
WHITE = (245, 245, 250)
RED = (225, 45, 45)
RED_DK = (140, 18, 18)
GOLD = (235, 185, 45)
GOLD_D = (150, 110, 20)
LABEL = (28, 28, 32)
PLATE_HI = (228, 230, 236)
PLATE_LO = (180, 184, 192)
METAL_HI = (148, 152, 158)
METAL_LO = (72, 74, 80)


def metal_bg(w: int, h: int, seed: int = 7) -> np.ndarray:
    """Brushed-metal gradient with subtle horizontal grain."""
    rng = np.random.default_rng(seed)
    arr = np.zeros((h, w, 3), np.uint8)
    for y in range(h):
        t = y / max(h - 1, 1)
        base = np.array([int(METAL_HI[i] * (1 - t) + METAL_LO[i] * t) for i in range(3)])
        grain = int(rng.integers(-6, 7))
        arr[y, :] = np.clip(base + grain, 0, 255)
    # horizontal brush streaks
    for y in range(0, h, 2):
        arr[y, :] = np.clip(arr[y, :].astype(int) + 4, 0, 255)
    return arr


def bevel_rect(arr: np.ndarray, x: int, y: int, w: int, h: int,
               hi: tuple, lo: tuple, fill: tuple | None = None) -> None:
    if fill:
        arr[y:y + h, x:x + w] = fill
    arr[y, x:x + w] = hi
    arr[y:y + h, x] = hi
    arr[y + h - 1, x:x + w] = lo
    arr[y:y + h, x + w - 1] = lo


def screw(arr: np.ndarray, cx: int, cy: int) -> None:
    H, W = arr.shape[:2]
    for j in range(-2, 3):
        for i in range(-2, 3):
            if i * i + j * j <= 5:
                py, px = cy + j, cx + i
                if 0 <= py < H and 0 <= px < W:
                    arr[py, px] = (90, 92, 98)
    for dx in (-1, 0, 1):
        px, py = cx + dx, cy
        if 0 <= py < H and 0 <= px < W:
            arr[py, px] = (40, 42, 48)


def chiseled(arr: np.ndarray, x: int, y: int, s: str, rgb: tuple,
             scale: int = 2, sp: int = 1) -> int:
    """Shadow + face + thin silver top highlight (chrome emboss). Returns right edge."""
    F.draw_text(arr, x + scale, y + scale, s, SHADOW, scale=scale, sp=sp)
    end = F.draw_text(arr, x, y, s, rgb, scale=scale, sp=sp)
    # top-edge highlight: one white pixel per lit top row cell
    H, W = arr.shape[:2]
    cx = x
    for ch in s:
        bmp = F._BMP.get(ch if ch in F._BMP else ch.upper(), F._BMP[" "])
        for gx in range(F.GW):
            if bmp[0, gx]:
                px = cx + gx * scale
                py = y - 1 if y > 0 else y
                if 0 <= py < H and 0 <= px < W:
                    arr[py, px] = SILVER
        cx += F.GW * scale + sp
    return end


def coin(arr: np.ndarray, cx: int, cy: int, r: int) -> None:
    H, W = arr.shape[:2]
    for y in range(cy - r, cy + r + 1):
        for x in range(cx - r, cx + r + 1):
            d2 = (x - cx) ** 2 + (y - cy) ** 2
            if d2 <= r * r and 0 <= y < H and 0 <= x < W:
                arr[y, x] = GOLD if d2 <= (r - 1) ** 2 else GOLD_D
    F.draw_text(arr, cx - 2, cy - 3, "B", BLACK)


def sha_badge(arr: np.ndarray, x: int, y: int, w: int, h: int, label: str = "SHA-256d") -> None:
    bevel_rect(arr, x, y, w, h, PLATE_HI, PLATE_LO, PLATE_LO)
    bevel_rect(arr, x + 1, y + 1, w - 2, h - 2, (240, 242, 248), (160, 164, 172), PLATE_HI)
    screw(arr, x + 2, y + 2)
    screw(arr, x + w - 3, y + 2)
    screw(arr, x + 2, y + h - 3)
    screw(arr, x + w - 3, y + h - 3)
    F.draw_text(arr, x + 4, y + (h - F.GH) // 2, label, LABEL)


def draw_nameplate_cluster(arr: np.ndarray, ox: int = 0, oy: int = 0,
                           gs_scale: int = 3, show_badge: bool = True) -> None:
    """Logo cluster: large GS + MINER, coin, optional SHA-256d plate."""
    chiseled(arr, ox + 14, oy + 10, "GS", RED, scale=gs_scale, sp=2)
    miner_x = ox + 14 + F.text_width("GS", sp=2, scale=gs_scale) + 2
    chiseled(arr, miner_x, oy + 16, "MINER", RED, scale=2, sp=1)
    coin(arr, ox + 22, oy + 44, 7)
    if show_badge:
        sha_badge(arr, ox + 38, oy + 38, 66, 14)


def compose_nameplate(w: int = 320, h: int = 72) -> Image.Image:
    arr = metal_bg(w, h)
    bevel_rect(arr, 0, 0, w, h, (170, 174, 182), (48, 50, 56))
    bevel_rect(arr, 2, 2, w - 4, h - 4, (120, 124, 130), (200, 204, 212))
    screw(arr, 6, 6)
    draw_nameplate_cluster(arr, gs_scale=3)
    return Image.fromarray(arr)


def compose_banner(w: int = 640, h: int = 160) -> Image.Image:
    arr = metal_bg(w, h, seed=11)
    bevel_rect(arr, 8, 8, w - 16, h - 16, (170, 174, 182), (48, 50, 56))
    bevel_rect(arr, 12, 12, w - 24, h - 24, (120, 124, 130), (200, 204, 212))
    for sx in (24, w - 24):
        for sy in (24, h - 24):
            screw(arr, sx, sy)
    chiseled(arr, 40, 36, "GS", RED, scale=5, sp=3)
    mx = 40 + F.text_width("GS", sp=3, scale=5) + 8
    chiseled(arr, mx, 52, "MINER", RED, scale=3, sp=2)
    coin(arr, 56, h - 52, 14)
    sha_badge(arr, 88, h - 58, 120, 22, "SHA-256d")
    F.draw_text(arr, w - 180, h - 36, "Apple IIgs  ·  Stratum  ·  SHA-256d", (60, 62, 68), sp=1)
    return Image.fromarray(arr)


def compose_icon_mark(size: int = 128) -> Image.Image:
    """Square mark for README / social (coin-forward)."""
    arr = metal_bg(size, size, seed=3)
    bevel_rect(arr, 4, 4, size - 8, size - 8, (170, 174, 182), (48, 50, 56))
    r = size // 5
    coin(arr, size // 2, size // 2 + 4, r)
    chiseled(arr, size // 2 - 28, 18, "GS", RED, scale=2, sp=1)
    return Image.fromarray(arr)


def save_png(im: Image.Image, path: Path, scale: int = 1) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if scale > 1:
        im = im.resize((im.width * scale, im.height * scale), Image.NEAREST)
    im.save(path)
    print(f"  {path}  ({im.width}x{im.height})")


def main() -> None:
    print("=== GS MINER logo assets ===")
    save_png(compose_nameplate(), BRANDING / "logo_nameplate_320.png", scale=2)
    save_png(compose_banner(), BRANDING / "logo_banner_github.png", scale=1)
    save_png(compose_banner(), BRANDING / "logo_banner_github_2x.png", scale=2)
    save_png(compose_icon_mark(), BRANDING / "logo_mark_128.png", scale=2)
    save_png(compose_nameplate(), VIZ / "logo_proof_chrome.png", scale=2)
    print("OK.")


if __name__ == "__main__":
    main()
