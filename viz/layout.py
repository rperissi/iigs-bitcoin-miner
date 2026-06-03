#!/usr/bin/env python3
"""
layout.py - text draw coordinates shared by render_panel.py and miner/viz.c.

Well geometry matches build_frame.py recess/plate rects.  Text is inset past the
2px bevel (INSET=3) and vertically centred inside each well/plate.
Labels use font4x6 ALL CAPS on bright nameplates; same font4x6 for LCD/LED values.
"""
import font4x6 as FV

INSET = 3
LBL_GH = FV.GH
VAL_GH = FV.GH


def tx(wx):
    return wx + INSET


def ty(wy, wh, gh):
    return wy + (wh - gh) // 2


def inner_w(ww):
    return ww - 2 * INSET


# ---- well / plate geometry (x, y, w, h) ----
FLD_PLATE = (101, 9, 48, 10)
FLD_LCD   = (149, 9, 97, 11)
FLD_DY    = 12

WAL_PLATE = (8, 52, 46, 10)
WAL_LCD   = (56, 52, 248, 10)

RO_LED    = (9, 68, 68, 10)
RO_PLATE  = (9, 79, 68, 9)
RO_DY     = 21

HASH_PLATE = (8, 156, 50, 10)
HASH_LCD   = (60, 155, 176, 11)

ODDS_PLATE = (8, 173, 28, 10)
ODDS_LCD   = (38, 172, 45, 10)   # trimmed 48->45 to slide the ticker left
ETA_PLATE  = (8, 184, 28, 10)
ETA_LCD    = (38, 183, 45, 10)

TICKER_LCD = (84, 174, 151, 17)  # left edge 87->84 (right edge unchanged at 235)

# ---- derived draw coords (mirror as #defines in miner/viz.c) ----
FLD_LX = tx(FLD_PLATE[0])
FLD_LW = inner_w(FLD_PLATE[2])
FLD_VX = tx(FLD_LCD[0])
FLD_VW = inner_w(FLD_LCD[2])
FLD_LY0 = ty(FLD_PLATE[1], FLD_PLATE[3], LBL_GH)
FLD_VY0 = ty(FLD_LCD[1], FLD_LCD[3], VAL_GH)

WAL_LX = tx(WAL_PLATE[0]) + 4   # clear screw at x=9
WAL_LW = inner_w(WAL_PLATE[2]) - 4
WAL_VX = tx(WAL_LCD[0])
WAL_VW = inner_w(WAL_LCD[2])
WAL_LY = ty(WAL_PLATE[1], WAL_PLATE[3], LBL_GH)
WAL_VY = ty(WAL_LCD[1], WAL_LCD[3], VAL_GH)

RO_LX = tx(RO_LED[0])
RO_RX = tx(243)
RO_VW = inner_w(RO_LED[2])
RO_LW = inner_w(RO_PLATE[2])


def ro_val_y(i):
    return ty(68 + i * RO_DY, RO_LED[3], VAL_GH)


def ro_lbl_y(i):
    return ty(79 + i * RO_DY, RO_PLATE[3], LBL_GH)


RO_VY0 = ro_val_y(0)
RO_LY0 = ro_lbl_y(0)

HASH_LX = tx(HASH_PLATE[0])
HASH_LW = inner_w(HASH_PLATE[2])
HASH_VX = tx(HASH_LCD[0])
HASH_VW = inner_w(HASH_LCD[2])
HASH_LY = ty(HASH_PLATE[1], HASH_PLATE[3], LBL_GH)
HASH_VY = ty(HASH_LCD[1], HASH_LCD[3], VAL_GH)

ODDS_LX = tx(ODDS_PLATE[0]) + 2   # aligned with BEST below, centered on the plate
ODDS_LW = inner_w(ODDS_PLATE[2])
ODDS_VX = tx(ODDS_LCD[0])
ODDS_VW = inner_w(ODDS_LCD[2])
ODDS_LY = ty(ODDS_PLATE[1], ODDS_PLATE[3], LBL_GH)
ODDS_VY = ty(ODDS_LCD[1], ODDS_LCD[3], VAL_GH)

ETA_LX = tx(ETA_PLATE[0]) + 2   # past the screw (cx=9), aligned under ODDS
ETA_LW = inner_w(ETA_PLATE[2])
ETA_VX = tx(ETA_LCD[0])
ETA_VW = inner_w(ETA_LCD[2])
ETA_LY = ty(ETA_PLATE[1], ETA_PLATE[3], LBL_GH)
ETA_VY = ty(ETA_LCD[1], ETA_LCD[3], VAL_GH)

TICKER_X = tx(TICKER_LCD[0])
TICKER_W = inner_w(TICKER_LCD[2])
TICKER_Y = ty(TICKER_LCD[1], TICKER_LCD[3], VAL_GH)

# ---- scope axis (4x6 in 10px left margin: groove x=84, black recess x=94) ----
SCOPE_IN  = (94, 71, 138, 70)
SCOPE_ML  = 84                             # groove left edge
SCOPE_AXIS_LX = SCOPE_ML + 1               # x=85, 9px "FF" ends at 93 (1px gap)
SCOPE_FF_Y    = SCOPE_IN[1] + 2           # y=73
SCOPE_00_Y    = SCOPE_IN[1] + SCOPE_IN[3] - 8   # y=133
SCOPE_CAP_X   = SCOPE_IN[0]
SCOPE_CAP_Y   = SCOPE_IN[1] + SCOPE_IN[3] + 2
SCOPE_CAP_W   = SCOPE_IN[2]

# ---- header buttons + TCP/IP lamp (groove 250,5,56,44) ----
BTN_RUN   = (253, 9, 24, 13)
BTN_STOP  = (279, 9, 24, 13)
BTN_CFG   = (253, 25, 50, 13)
TCP_PLATE = (253, 39, 36, 8)
TCP_LAMP  = (291, 39, 11, 8)
TCP_LX = tx(TCP_PLATE[0])
TCP_LW = inner_w(TCP_PLATE[2])
TCP_LY = ty(TCP_PLATE[1], TCP_PLATE[3], LBL_GH)
TCP_LAMP_X = TCP_LAMP[0]
TCP_LAMP_Y = TCP_LAMP[1]
TCP_LAMP_D = min(TCP_LAMP[2], TCP_LAMP[3]) - 2

# ---- VU hashrate 60s (groove 240,153,74,42; well 243,163,68,25) ----
VU_GRO   = (240, 153, 74, 42)
VU_WELL  = (243, 163, 68, 25)
VU_LBL_Y = ty(154, 8, LBL_GH)
VU_5S_X  = 246
VU_AXIS_Y = ty(188, 7, VAL_GH)          # margin below well (well ends y=188)
VU_60S_RX = VU_GRO[0] + VU_GRO[2] - 3   # right-align in groove
