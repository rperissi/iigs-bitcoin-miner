#!/usr/bin/env python3
"""render_config.py - Mac proof of the GS Miner CONFIG page (demo mode layout)."""
import numpy as np
from PIL import Image
import spec as S
import build_config as C
import font4x6 as F

W, H = 320, 200


def c8(t):
    return tuple(int(v * 17) for v in t)


BLK, WHT = (0, 0, 0), (255, 255, 255)
CYAN, GREEN, RED, AMBER = c8(S.CYAN), c8(S.GREEN), c8(S.RED), c8(S.AMBER)
BLUE = c8(S.BLUE)

FRAME = np.array(Image.open("config_frame.png").convert("RGB"))
A = FRAME.copy()


def put(x, y, s, rgb):
    F.draw_text(A, x, y, s, rgb)


def put_c(x, w, y, s, rgb):
    F.draw_text(A, x + (w - F.text_width(s)) // 2, y, s, rgb)


def rect(x, y, w, h, rgb):
    A[y:y + h, x:x + w] = rgb


def button(x, y, w, h, fill, label, lcol):
    rect(x, y, w, h, fill)
    put_c(x, w, y + (h - F.GH) // 2, label, lcol)


def lbl_y(y):
    return y + (C.ROW_H - F.GH) // 2


put_c(10, 300, 13, "DEMO MODE - SET TCP/POOL/WALLET FOR LIVE", CYAN)

LABELS = ["WORKER", "WALLET", "POOL", "BACKUP"]
VALUES = ["GSMINER", "3CfSNGtkdpGMyKWx57Vr93MdHyP2UQgKao",
          "192.168.2.1", "POOL2.EX.COM"]
for i, y in enumerate(C.ROWS_Y):
    put(C.LBL_X + 4, lbl_y(y), LABELS[i], BLK)
    put(C.VAL_X + 4, lbl_y(y), VALUES[i], CYAN)

put(C.PORT_LBL_X + 3, lbl_y(C.ROWS_Y[2]), "PORT", BLK)
put(C.PORT_VAL_X + 4, lbl_y(C.ROWS_Y[2]), "3333", CYAN)
put(C.PORT_LBL_X + 3, lbl_y(C.ROWS_Y[3]), "PORT", BLK)
put(C.PORT_VAL_X + 4, lbl_y(C.ROWS_Y[3]), "3333", CYAN)

cx = C.VAL_X + 4 + F.text_width(VALUES[0]) + 1
A[C.ROWS_Y[0] + 1:C.ROWS_Y[0] + C.ROW_H - 1, cx:cx + 1] = CYAN

for y, line in [
    (106, "DEMO: LOCAL SHA-256 + NO POOL/PAYOUT"),
    (115, "LIVE: MARINETTI + TCP/IP REQUIRED"),
    (133, "CLICK LIVE OR DEMO TO TOGGLE"),
]:
    put(12, y, line, AMBER)

for i, (k, d) in enumerate([
    ("TAB", "NEXT FIELD"), ("RETURN", "SAVE + EXIT"),
    ("ESC", "QUIT"), ("DELETE", "ERASE CHAR"),
]):
    put(12, 151 + i * 9, k, AMBER)
    put(68, 151 + i * 9, d, AMBER)

GREY = c8((5, 5, 5))
button(C.BTN_X, C.SAVE_Y, C.BTN_W, C.BTN_H, GREEN, "SAVE", BLK)
button(C.BTN_X, C.DEF_Y, C.BTN_W, C.BTN_H, GREY, "DEFAULTS", BLK)
button(C.BTN_X, C.MODE_Y, C.MODE_W, C.BTN_H, BLUE, "LIVE", WHT)
button(C.BTN_X + 52, C.MODE_Y, C.MODE_W, C.BTN_H, AMBER, "DEMO", BLK)
button(C.BTN_X, C.ACT_Y, C.MODE_W, C.BTN_H, GREY, "CANCEL", WHT)
button(C.BTN_X + 52, C.ACT_Y, C.MODE_W, C.BTN_H, RED, "QUIT", WHT)

pals, scb = S.build_uniform_palettes(FRAME)
out, _ = S.quantize(A, pals, scb, frame=FRAME)
Image.fromarray(out).resize((W * 2, H * 2), Image.NEAREST).save("config_full2x.png")
print("wrote config_full2x.png")
