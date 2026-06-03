#!/usr/bin/env python3
"""
logo_proof.py - prototype crisp CODE-DRAWN logo, coin, button text and a few
labels on the smooth plate, to compare vs the baked (downscaled) logo/coin.
"""
import numpy as np
from PIL import Image, ImageEnhance
import font5x7 as F

W, H = 320, 200
FRAME = "/Users/robertperissi/.cursor/projects/Users-robertperissi-Downloads-IIGS/assets/viz_frame_smooth.png"

RED = (225, 45, 45)
WHITE = (240, 240, 245)
DARK = (28, 28, 32)
GOLD = (235, 185, 45)
GOLD_D = (150, 110, 20)
LABEL = (218, 220, 226)
GREEN = (70, 255, 95)
CYAN = (135, 235, 255)


def coin(arr, cx, cy, r):
    for y in range(cy - r, cy + r + 1):
        for x in range(cx - r, cx + r + 1):
            d = (x - cx) ** 2 + (y - cy) ** 2
            if d <= r * r:
                arr[y, x] = GOLD if d <= (r - 1) ** 2 else GOLD_D
    # B + vertical strokes (bitcoin-ish)
    F.draw_text(arr, cx - 2, cy - 3, "B", DARK)


def main():
    im = Image.open(FRAME).convert("RGB").resize((W, H), Image.LANCZOS)
    im = ImageEnhance.Contrast(im).enhance(1.10)
    lut = [int(((i / 255.0) ** 0.85) * 255 + 0.5) for i in range(256)] * 3
    im = im.point(lut)
    arr = np.array(im, np.uint8)

    # logo "GS MINER": shadow + red, scale 3
    F.draw_text(arr, 14, 15, "GS MINER", DARK, scale=3, sp=2)
    F.draw_text(arr, 13, 14, "GS MINER", RED, scale=3, sp=2)
    # tiny white top highlight pass (1px up)
    F.draw_text(arr, 13, 13, "GS MINER", WHITE, scale=1, sp=8)

    # coin + SHA-256d on the badge plaque
    coin(arr, 26, 44, 7)
    F.draw_text(arr, 40, 41, "SHA-256d", LABEL)

    # button text
    F.draw_text(arr, 254, 16, "START", DARK)
    F.draw_text(arr, 253, 15, "START", WHITE)
    F.draw_text(arr, 287, 16, "STOP", DARK)
    F.draw_text(arr, 286, 15, "STOP", WHITE)
    F.draw_text(arr, 268, 47, "CONFIG", LABEL)

    # status lamp labels
    F.draw_text(arr, 232, 20, "MINING", LABEL)
    F.draw_text(arr, 232, 44, "POOL OK", LABEL)

    # header field labels
    F.draw_text(arr, 92, 11, "WORKER", LABEL)
    F.draw_text(arr, 100, 23, "POOL", LABEL)
    F.draw_text(arr, 84, 35, "FAILOVER", LABEL)

    # left/right readout labels (above wells)
    for x, y, t in [(11, 64, "HASHRATE"), (11, 84, "SHARES"), (11, 105, "BEST"), (11, 125, "UPTIME")]:
        F.draw_text(arr, x, y, t, LABEL)
    for x, y, t in [(243, 64, "NONCE"), (243, 84, "HASHES"), (243, 105, "NET DIFF"), (243, 125, "BLOCK")]:
        F.draw_text(arr, x, y, t, LABEL)

    # a couple sample values
    F.draw_text(arr, 20, 73, "90", GREEN, scale=2)
    F.draw_text(arr, 243, 73, "0000001A", GREEN)
    F.draw_text(arr, 155, 53, "bc1q9x7k4m2v8h3r5t6y7u8i9o0p1a2", CYAN)

    out = Image.fromarray(arr)
    out.save("logo_proof.png")
    out.resize((W * 2, H * 2), Image.NEAREST).save("logo_proof2x.png")
    print("wrote logo_proof.png / logo_proof2x.png")


if __name__ == "__main__":
    main()
