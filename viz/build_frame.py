#!/usr/bin/env python3
"""
build_frame.py - PROCEDURAL GS Miner chassis, native 320x200, "one sheet" look.

Design notes (matching the concept mockup + chiseled-metal finish):
  * ONE metal sheet: recessed wells/screens cut into it, screws ONLY in the four
    outer corners (no per-panel rivets) -> single-panel feel.
  * Side readouts are a CONTIGUOUS stack of cells (no vertical gaps), which frees
    room for a taller hash bar.
  * CLEAN metal: smooth gradients snapped to a single NEUTRAL grey ramp (no
    dithering) -> contiguous greys, sleek 16-bit look, not scattered 8-bit noise.
  * Softer 2-tone bevels + chamfered corners; engraved grooves group the header
    fields / buttons without looking like separate riveted plates.

Layout coords here are the source of truth for spec.py + viz.c.
Outputs frame_proc.png (320x200) and frame_proc_2x.png (preview).
"""
import numpy as np
from PIL import Image

W, H = 320, 200
MOCK = ("/Users/robertperissi/.cursor/projects/Users-robertperissi-Downloads-IIGS/"
        "assets/gsminer1-9f06d922-f6d4-4051-8442-3f40fc2e5844.png")

BASE = np.array([111, 114, 121], float)   # bumped lighter
BLACK = np.array([6, 6, 8], float)
BLUE = np.array([16, 20, 92], float)
A = np.zeros((H, W, 3), float)


def g(mul):
    return BASE * mul


def clipset(x, y, c):
    if 0 <= x < W and 0 <= y < H:
        A[y, x] = c


def vgrad(x, y, w, h, top, bot):
    for j in range(h):
        t = j / max(h - 1, 1)
        A[y + j, x:x + w] = top * (1 - t) + bot * t


def hline(x, y, w, c): A[max(y, 0), x:x + w] = c
def vline(x, y, h, c): A[y:y + h, max(x, 0)] = c


def sheen(x, y, w, h, amp=30.0):
    """bright specular band near the top of a raised surface."""
    for j in range(h):
        s = np.exp(-((j - h * 0.22) ** 2) / (2 * (h * 0.26) ** 2)) * amp
        A[y + j, x:x + w] = np.clip(A[y + j, x:x + w] + s, 0, 255)


def chamf(x, y, w, h, c):
    for cx, cy in ((x, y), (x + w - 1, y), (x, y + h - 1), (x + w - 1, y + h - 1)):
        clipset(cx, cy, c)


def bevel(x, y, w, h, lt, dk):
    """1px 2-tone bevel, corners left to caller."""
    hline(x + 1, y, w - 2, lt); vline(x, y + 1, h - 2, lt)
    hline(x + 1, y + h - 1, w - 2, dk); vline(x + w - 1, y + 1, h - 2, dk)


def bevel2(x, y, w, h, lt, mid_lt, dk, mid_dk):
    """thick 2px chiseled bevel: bright highlight + dark shadow = 3D pop."""
    bevel(x, y, w, h, lt, dk)
    bevel(x + 1, y + 1, w - 2, h - 2, mid_lt, mid_dk)


def rtile(x, y, w, h):
    """raised metal cell with strong relief (high-contrast chiaroscuro)."""
    vgrad(x, y, w, h, g(1.34), g(0.66)); sheen(x, y, w, h, 34.0)
    bevel2(x, y, w, h, g(1.85), g(1.42), g(0.40), g(0.62))
    chamf(x, y, w, h, g(0.40))


def recess(x, y, w, h, color):
    """display inset cut into the sheet: dark top/left, bright lit far rim."""
    A[y:y + h, x:x + w] = color
    if not np.array_equal(color, BLACK):
        vgrad(x + 1, y + 1, w - 2, max(h - 2, 1), color * 1.6 + 16, color)
    bevel2(x, y, w, h, g(0.34), g(0.58), g(1.78), g(1.34))
    chamf(x, y, w, h, g(0.32))


def groove(x, y, w, h):
    """engraved recessed outline on the sheet (groups without rivets)."""
    hline(x + 1, y, w - 2, g(0.52)); vline(x, y + 1, h - 2, g(0.52))
    hline(x + 1, y + h - 1, w - 2, g(1.62)); vline(x + w - 1, y + 1, h - 2, g(1.62))


def plate(x, y, w, h):
    """bright near-white nameplate for crisp BLACK labels (bright X-AXIS look)."""
    vgrad(x, y, w, h, g(1.82), g(1.55))
    bevel(x, y, w, h, g(1.98), g(1.30))
    chamf(x, y, w, h, g(1.30))


def screw(cx, cy):
    for j in range(-2, 3):
        for i in range(-2, 3):
            if i * i + j * j <= 4:
                clipset(cx + i, cy + j, g(0.42))
    clipset(cx - 1, cy - 1, g(1.85)); clipset(cx, cy, g(1.5))
    hline(cx - 1, cy, 3, g(0.34))


def main():
    # ---- one metal sheet (wider gradient + bright outer rim) ----
    vgrad(0, 0, W, H, g(1.30), g(0.70))
    bevel2(0, 0, W, H, g(1.95), g(1.5), g(0.42), g(0.66))
    bevel(3, 3, W - 6, H - 6, g(0.62), g(1.35))

    # ---- HEADER ----
    recess(14, 6, 85, 30, BLACK)                      # LOGO WELL (load/draw later); widened 82->85 to give MINER right-edge room
    recess(14, 38, 14, 11, BLACK)                     # coin well (code draws coin)
    groove(30, 39, 69, 10)                            # SHA-256d badge, aligned to logo well right edge (99)
    # field cluster: lamps removed -> expand right to the button cluster so the
    # WORKER / POOL / FAILOVER values get full width.
    groove(100, 5, 148, 43)
    for i in range(3):
        plate(101, 9 + i * 12, 48, 10)          # bright label nameplate
        recess(149, 9 + i * 12, 97, 11, BLUE)   # wide value LCD (149..246)
    # button cluster: raised pads + CONFIG; TCP/IP lamp row below CONFIG
    groove(250, 5, 56, 44)
    rtile(252, 8, 26, 15)                             # RUN pad (code draws cap)
    rtile(278, 8, 26, 15)                             # STOP pad
    rtile(252, 24, 52, 14)                            # CONFIG pad
    plate(253, 39, 36, 8)                             # TCP/IP label
    recess(291, 39, 11, 8, BLACK)                     # TCP status LED well

    # ---- WALLET (gap below the header so they don't run together) ----
    groove(5, 50, 310, 14)
    plate(8, 52, 46, 10)
    recess(56, 52, 248, 10, BLUE)

    # ---- MAIN ROW: contiguous readout stacks + scope ----
    ys, cellh = 67, 21
    for i in range(4):
        cy = ys + i * cellh
        # LED cell: black LED screen (green digits) on top + BRIGHT nameplate below
        # for a bold BLACK label -> high contrast, razor clear.
        rtile(6, cy, 74, cellh)
        recess(9, cy + 1, 68, 10, BLACK); plate(9, cy + 12, 68, 9)       # left
        rtile(240, cy, 74, cellh)
        recess(243, cy + 1, 68, 10, BLACK); plate(243, cy + 12, 68, 9)   # right
    # scope: EQUAL left (BYTE) and bottom (time) axis margins = 10px each
    groove(84, 67, 154, 84)
    recess(94, 71, 138, 70, BLACK)

    # ---- HASH BAR ----
    groove(5, 153, 233, 15)
    plate(8, 156, 50, 10)
    recess(60, 155, 176, 11, BLACK)

    # ---- ODDS / ETA (value wells trimmed 48->45 to give the TICKER 3px more) ----
    groove(6, 170, 74, 25)
    plate(8, 173, 28, 10); recess(38, 172, 45, 10, BLACK)
    plate(8, 184, 28, 10); recess(38, 183, 45, 10, BLACK)

    # ---- TICKER (slid 3px left: groove 84->81, recess 87->84; right edges fixed) ----
    groove(81, 170, 157, 25)
    recess(84, 174, 151, 17, BLACK)

    # ---- VU (tall, right) - expanded right to align with the column above ----
    groove(240, 153, 74, 42)
    plate(243, 154, 68, 8)
    recess(243, 163, 68, 25, BLACK)

    # ---- screws: corners + wallet ends, vertically aligned (x9 / x310) ----
    for cx, cy in ((9, 9), (310, 9), (9, 191), (310, 191), (9, 57), (310, 57)):
        screw(cx, cy)

    # ---- SMOOTH source: keep the continuous metal gradient (NO pre-snap).
    # The SHR converter (spec.py) owns colour reduction: per-zone equal-population
    # grey ramps + fine dithering recover smoothness the hard snap used to destroy.
    img = Image.fromarray(np.clip(A, 0, 255).astype(np.uint8))
    img.save("frame_proc.png")
    img.resize((W * 2, H * 2), Image.NEAREST).save("frame_proc_2x.png")
    print("wrote frame_proc.png / frame_proc_2x.png")


if __name__ == "__main__":
    main()
