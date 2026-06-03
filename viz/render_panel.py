#!/usr/bin/env python3
"""
render_panel.py - FULL populated Mac proof of the GS Miner panel.
"""
import numpy as np
from PIL import Image
import spec as S
import font4x6 as F
import layout as L

W, H = 320, 200


def c8(t):
    return tuple(int(v * 17) for v in t)


BLK = (0, 0, 0)
WHT = (255, 255, 255)
H_BLUE, H_CYAN, H_GREEN, H_RED, H_GOLD = (
    c8(S.BLUE), c8(S.CYAN), c8(S.GREEN), c8(S.RED), c8(S.GOLD))
M_LED = c8(S.LED)
RB = [c8(c) for c in S.SPECTRA]
B_AMBER, B_VUG, B_VUY, B_VUR = c8(S.AMBER), c8(S.VUG), c8(S.VUY), c8(S.VUR)
LBL = (0, 0, 0)

FRAME = np.array(Image.open("frame_proc.png").convert("RGB"))
A = FRAME.copy()


def put(x, y, s, rgb, maxw=None):
    if maxw is not None:
        n = max(0, (maxw + 1) // (F.GW + 1))
        s = s[:n]
    F.draw_text(A, x, y, s, rgb)


def put_lbl(x, y, s, maxw=None):
    put(x, y, s, LBL, maxw=maxw)


def put_r(x_right, y, s, rgb):
    put(x_right - F.text_width(s), y, s, rgb)


def rect(x, y, w, h, rgb):
    A[y:y + h, x:x + w] = rgb


def coin(x, y, w, h):
    cx, cy = x + w / 2.0, y + h / 2.0
    rx, ry = w / 2.0, h / 2.0
    for j in range(h):
        for i in range(w):
            dx, dy = (x + i - cx) / rx, (y + j - cy) / ry
            if dx * dx + dy * dy <= 1.0:
                A[y + j, x + i] = H_GOLD
    icx = x + w // 2
    A[y, icx - 1] = BLK; A[y, icx] = BLK                     # Bitcoin stem (top)
    A[y + h - 1, icx - 1] = BLK; A[y + h - 1, icx] = BLK     # Bitcoin stem (bottom)
    F.draw_text(A, x + w // 2 - 2, y + h // 2 - 3, "B", BLK)


def button(x, y, w, h, fill, label, lcol, down=False):
    """FTA chiseled cap on raised pad; down = pressed (inverted bevel + nudge)."""
    o = 1 if down else 0
    hi, lo = (BLK, WHT) if down else (WHT, BLK)
    rect(x, y, w, h, fill)
    # outer bevel
    A[y, x:x + w] = hi; A[y:y + h, x] = hi
    A[y + h - 1, x:x + w] = lo; A[y:y + h, x + w - 1] = lo
    # inner chisel
    A[y + 1, x + 1:x + w - 1] = hi; A[y + 1:y + h - 1, x + 1] = hi
    A[y + h - 2, x + 1:x + w - 1] = lo; A[y + 1:y + h - 1, x + w - 2] = lo
    tx = x + (w - F.text_width(label)) // 2 + o
    put(tx, y + (h - F.GH) // 2 + o, label, lcol, maxw=w)


def lamp(x, y, d, state):
    """TCP LED: off=dark, down=red, up=green, act=green+specular flash."""
    if state == "off":
        col = (28, 32, 28)
    elif state == "down":
        col = H_RED
    elif state == "act":
        col = H_GREEN
    else:
        col = H_GREEN
    cx, cy = x + d / 2.0, y + d / 2.0
    rx, ry = d / 2.0, d / 2.0
    for j in range(d):
        for i in range(d):
            dx, dy = (x + i - cx) / rx, (y + j - cy) / ry
            if dx * dx + dy * dy <= 1.0:
                A[y + j, x + i] = col
    if state in ("up", "act"):
        A[y + 1, x + 1] = WHT


F.draw_text(A, 28, 16, "GS MINER", H_RED)
coin(16, 40, 10, 8)
put(33, 41, "SHA-256D", H_CYAN)


def field(i, label, value):
    yl = L.FLD_LY0 + i * L.FLD_DY
    yv = L.FLD_VY0 + i * L.FLD_DY
    put_lbl(L.FLD_LX, yl, label, maxw=L.FLD_LW)
    put(L.FLD_VX, yv, value, H_CYAN, maxw=L.FLD_VW)


field(0, "WORKER", "GSMINER")
field(1, "POOL", "192.168.2.10")
field(2, "FAILOVER", "POOL2.EX.COM")

button(*L.BTN_RUN, H_GREEN, "RUN", BLK, down=True)
button(*L.BTN_STOP, H_RED, "STOP", WHT, down=False)
button(*L.BTN_CFG, (70, 74, 82), "CONFIG", (235, 235, 235))
put_lbl(L.TCP_LX, L.TCP_LY, "TCP/IP", maxw=L.TCP_LW)
lamp(L.TCP_LAMP_X + 1, L.TCP_LAMP_Y + 1, L.TCP_LAMP_D, "off")

put_lbl(L.WAL_LX, L.WAL_LY, "WALLET", maxw=L.WAL_LW)
put(L.WAL_VX, L.WAL_VY, "1A2B3C4D5E6F7G8H9J0KQWERTYUP", H_CYAN, maxw=L.WAL_VW)

LEFT = [("HASHRATE", "92 H/S"), ("SHARES", "1 / 0"),
        ("BEST", "1.8 ZB"), ("UPTIME", "00:12:42")]
RIGHT = [("NONCE", "0x001A4F"), ("HASHES", "1.28M"),
         ("NET DIFF", "88.1 T"), ("BLOCK", "842317")]
for i, (lab, val) in enumerate(LEFT):
    put(L.RO_LX, L.ro_val_y(i), val, M_LED, maxw=L.RO_VW)
    put_lbl(L.RO_LX, L.ro_lbl_y(i), lab, maxw=L.RO_LW)
for i, (lab, val) in enumerate(RIGHT):
    put(L.RO_RX, L.ro_val_y(i), val, M_LED, maxw=L.RO_VW)
    put_lbl(L.RO_RX, L.ro_lbl_y(i), lab, maxw=L.RO_LW)

sx, sy, sw, sh = L.SCOPE_IN
rng = np.random.default_rng(7)
for col in range(sw):
    energy = (0.5 + 0.5 * np.sin(col * 0.18)) * (0.6 + 0.4 * rng.random())
    bars = int(energy * sh)
    for row in range(bars):
        ci = (row * len(S.SPECTRA)) // sh
        A[sy + sh - 1 - row, sx + col] = RB[min(ci, len(RB) - 1)]
put_lbl(L.SCOPE_AXIS_LX, L.SCOPE_FF_Y, "FF")
put_lbl(L.SCOPE_AXIS_LX, L.SCOPE_00_Y, "00")
put_lbl(L.SCOPE_CAP_X, L.SCOPE_CAP_Y, "4096 HASHES", maxw=L.SCOPE_CAP_W)

put_lbl(L.HASH_LX, L.HASH_LY, "SHA-256", maxw=L.HASH_LW)
put(L.HASH_VX, L.HASH_VY, "005B58A41C9E0D7F2BC3914A88E612", B_VUG, maxw=L.HASH_VW)

put_lbl(L.ODDS_LX, L.ODDS_LY, "ODDS", maxw=L.ODDS_LW)
put_lbl(L.ETA_LX, L.ETA_LY, "ETA", maxw=L.ETA_LW)
put(L.ODDS_VX, L.ODDS_VY, "1:4E23", B_AMBER, maxw=L.ODDS_VW)
put(L.ETA_VX, L.ETA_VY, "749T YRS", B_AMBER, maxw=L.ETA_VW)

put(L.TICKER_X, L.TICKER_Y, "NET 642 EH/S  HEIGHT 842317  FEE 4 SAT", B_AMBER, maxw=L.TICKER_W)

vx, vy, vw, vh = 243, 163, 68, 25
put_lbl(245, 155, "HASHRATE 30s", maxw=66)
put_lbl(L.VU_5S_X, L.VU_AXIS_Y, "30s")
put_r(L.VU_60S_RX - 7, L.VU_AXIS_Y, "0s", LBL)
# scrolling hashrate line graph: 66 columns, newest on the right.
import math
gw, gh = vw - 2, vh - 2
gx0, gy0 = vx + 1, vy + 1
# clean line riding ~mid-height with gentle drift (headroom-scaled steady rate)
samples = [0.48 + 0.10 * math.sin(c / 8.0) + 0.04 * math.sin(c / 2.7)
           for c in range(gw)]
gy = [max(0, min(gh - 1, int(s * (gh - 1)))) for s in samples]


def gset(col, rowfb, col_idx):
    if 0 <= rowfb < gh and 0 <= col < gw:
        A[gy0 + gh - 1 - rowfb, gx0 + col] = col_idx


for c in range(gw):
    y0 = gy[c]
    y1 = gy[c - 1] if c > 0 else gy[c]
    lo, hi = min(y0, y1), max(y0, y1)
    f = y0 * 100 // (gh - 1)
    cc = B_VUG if f < 55 else (B_VUY if f < 80 else B_VUR)
    for yy in range(lo, hi + 1):
        gset(c, yy, cc)
    gset(c, y0 + 1, cc)
gset(gw - 1, gy[-1], WHT)
gset(gw - 1, gy[-1] + 1, WHT)

pals, scb = S.build_zone_palettes(FRAME)
out, _ = S.quantize(A, pals, scb, frame=FRAME)

Image.fromarray(out).resize((W * 2, H * 2), Image.NEAREST).save("panel_full2x.png")
print("wrote panel_full2x.png")
