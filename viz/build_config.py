#!/usr/bin/env python3
"""
build_config.py - PROCEDURAL GS Miner CONFIG chassis, native 320x200.

Layout coords are the source of truth for render_config.py + miner/viz.c.
Outputs config_frame.png (320x200) and config_frame_2x.png (preview).
"""
import numpy as np
from PIL import Image

W, H = 320, 200
BASE = np.array([111, 114, 121], float)
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


def chamf(x, y, w, h, c):
    for cx, cy in ((x, y), (x + w - 1, y), (x, y + h - 1), (x + w - 1, y + h - 1)):
        clipset(cx, cy, c)


def bevel(x, y, w, h, lt, dk):
    hline(x + 1, y, w - 2, lt); vline(x, y + 1, h - 2, lt)
    hline(x + 1, y + h - 1, w - 2, dk); vline(x + w - 1, y + 1, h - 2, dk)


def bevel2(x, y, w, h, lt, mid_lt, dk, mid_dk):
    bevel(x, y, w, h, lt, dk)
    bevel(x + 1, y + 1, w - 2, h - 2, mid_lt, mid_dk)


def recess(x, y, w, h, color):
    A[y:y + h, x:x + w] = color
    if not np.array_equal(color, BLACK):
        vgrad(x + 1, y + 1, w - 2, max(h - 2, 1), color * 1.6 + 16, color)
    bevel2(x, y, w, h, g(0.34), g(0.58), g(1.78), g(1.34))
    chamf(x, y, w, h, g(0.32))


def groove(x, y, w, h):
    hline(x + 1, y, w - 2, g(0.52)); vline(x, y + 1, h - 2, g(0.52))
    hline(x + 1, y + h - 1, w - 2, g(1.62)); vline(x + w - 1, y + 1, h - 2, g(1.62))


def plate(x, y, w, h):
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


# field row geometry (mirrored in render_config.py + miner/viz.c)
LBL_X, LBL_W = 10, 56
VAL_X = 70
ROW_H = 13
ROWS_Y = [38, 54, 70, 86]  # WORKER / WALLET / POOL / BACKUP
PORT_LBL_X, PORT_LBL_W = 214, 26
PORT_VAL_X, PORT_VAL_W = 244, 64
IP_W = PORT_LBL_X - VAL_X - 4

# bottom panel (flat black help + button wells, no heavy outer grooves)
HELP_X, HELP_Y, HELP_W, HELP_H = 8, 101, 196, 88
BTN_X, BTN_W, BTN_H = 210, 100, 19
BTN_GAP = 4
# button stack aligned with help panel (same top/bottom: 101..189)
SAVE_Y = HELP_Y
DEF_Y = SAVE_Y + BTN_H + BTN_GAP
MODE_Y = DEF_Y + BTN_H + BTN_GAP
ACT_Y = MODE_Y + BTN_H + BTN_GAP
MODE_W = 48
# corner screws: same x insets as the main page (9 / 310); top below the title
# groove (27), bottom just under help+buttons (189) but clear of the 196 bevel
SCREW_TOP_Y = 30
SCREW_BTM_Y = 191
SCREW_LX = 9
SCREW_RX = 310


def main():
    vgrad(0, 0, W, H, g(1.30), g(0.70))
    bevel2(0, 0, W, H, g(1.95), g(1.5), g(0.42), g(0.66))
    bevel(3, 3, W - 6, H - 6, g(0.62), g(1.35))

    groove(5, 5, 310, 22)
    recess(10, 9, 300, 14, BLACK)

    for y in ROWS_Y:
        plate(LBL_X, y, LBL_W, ROW_H)
    recess(VAL_X, ROWS_Y[0], 238, ROW_H, BLUE)
    recess(VAL_X, ROWS_Y[1], 238, ROW_H, BLUE)
    recess(VAL_X, ROWS_Y[2], IP_W, ROW_H, BLUE)
    plate(PORT_LBL_X, ROWS_Y[2], PORT_LBL_W, ROW_H)
    recess(PORT_VAL_X, ROWS_Y[2], PORT_VAL_W, ROW_H, BLUE)
    recess(VAL_X, ROWS_Y[3], IP_W, ROW_H, BLUE)
    plate(PORT_LBL_X, ROWS_Y[3], PORT_LBL_W, ROW_H)
    recess(PORT_VAL_X, ROWS_Y[3], PORT_VAL_W, ROW_H, BLUE)

    recess(HELP_X, HELP_Y, HELP_W, HELP_H, BLACK)
    recess(BTN_X, SAVE_Y, BTN_W, BTN_H, BLACK)
    recess(BTN_X, DEF_Y, BTN_W, BTN_H, BLACK)
    recess(BTN_X, MODE_Y, MODE_W, BTN_H, BLACK)
    recess(BTN_X + 52, MODE_Y, MODE_W, BTN_H, BLACK)
    recess(BTN_X, ACT_Y, MODE_W, BTN_H, BLACK)
    recess(BTN_X + 52, ACT_Y, MODE_W, BTN_H, BLACK)

    for cx, cy in ((SCREW_LX, SCREW_TOP_Y), (SCREW_RX, SCREW_TOP_Y),
                   (SCREW_LX, SCREW_BTM_Y), (SCREW_RX, SCREW_BTM_Y)):
        screw(cx, cy)

    img = Image.fromarray(np.clip(A, 0, 255).astype(np.uint8))
    img.save("config_frame.png")
    img.resize((W * 2, H * 2), Image.NEAREST).save("config_frame_2x.png")
    print("wrote config_frame.png / config_frame_2x.png")


if __name__ == "__main__":
    main()
