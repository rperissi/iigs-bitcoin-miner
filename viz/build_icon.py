#!/usr/bin/env python3
"""
build_icon.py - generate a GS/OS Finder icon file ($CA) for GS MINER.

Mirrors the in-app gold Bitcoin coin (viz.c coin(): gold disc, black B + stems).

Outputs:
  viz/gsminer.icons   - the $CA Finder Icons file (header + 1 IconData record:
                        big + small Icon records), ready to inject as type ICN.
  viz/icon_proof.png  - an upscaled, aspect-corrected proof so the coin can be
                        eyeballed on the Mac before it ever touches the GS.

Format authority:
  - $CA layout: Apple II File Type Note for $CA (Finder Icons File), M.Deatherage.
      header(26): iBlkNext(L=0) iBlkID(W=$0001) iBlkPath(L=0) iBlkName[16 pstr]
      IconData:   iDataLen(W) iDataBoss[64 pstr] iDataName[16 pstr]
                  iDataType(W) iDataAux(W) bigIcon smallIcon ; list ends with len=0
  - Icon record: QuickDraw II Aux -> imType(W,bit15=color) imSize(W) imHeight(W)
      imWidth(W) image[imSize] mask[imSize]  (4bpp 320-mode: hi nibble = left px)
  - Colours are the fixed IIGS default 16-colour palette (icons carry no CLUT):
      0 black  8 brown  9 orange  D yellow  A ltgrey  F white
"""
import struct, zlib, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))

# QuickDraw II default palette (index -> RGB) - the table the Finder uses to draw
# colour icons. $0RGB 4-bit channels expanded to 8-bit. Index 6 = orange ($0F70).
def _x(v): return ((v & 0xF) * 17)
def _rgb(w): return (_x(w >> 8), _x(w >> 4), _x(w))
_QD = [0x000, 0x777, 0x841, 0x72C, 0x00F, 0x080, 0xF70, 0xD00,
       0xFA9, 0xFF0, 0x0E0, 0x4DF, 0xDAF, 0x78F, 0xCCC, 0xFFF]
PAL = {i: _rgb(_QD[i]) for i in range(16)}

# in-app-coin look: flat gold disc with a black Bitcoin glyph. The Finder renders
# color icons through the 640-mode desktop palette (QuickDraw II default); measured
# on a real System 6 desktop, index 6 ($0F70) is the clean orange and 0 is black.
C_TRANSP = 0x0    # masked out (outside the disc)
C_DISC   = 0x6    # orange disc (QuickDraw-default index 6, verified on-desktop)
C_SYM    = 0x0    # black Bitcoin glyph

# bold "B" for the small icon: narrow top bowl, wider bottom bowl (reads B, not 8).
B_SMALL = [
    "1111100",
    "1100110",
    "1100110",
    "1100100",
    "1111110",
    "1100011",
    "1100011",
    "1100011",
    "1111111",
]
# chunky "B" for the large disc (kept short so the icon can't clip the window top;
# bottom bowl/foot one column wider than the top so it reads B, not 8).
B_BIG = [
    "11111111100",
    "11111111100",
    "11100001100",
    "11100001100",
    "11111111100",
    "11111111100",
    "11100000110",
    "11100000110",
    "11100000110",
    "11111111110",
    "11111111110",
]


def draw_logo(W, H, glyph, stem_cols, stem_w, stem_h):
    """Flat disc (mask = disc) with a centred black B + Bitcoin stems."""
    idx  = [[C_TRANSP]*W for _ in range(H)]
    mask = [[0]*W for _ in range(H)]
    cx, cy = (W-1)/2.0, (H-1)/2.0
    rx, ry = (W/2.0)-0.5, (H/2.0)-0.5
    for y in range(H):
        for x in range(W):
            nx, ny = (x-cx)/rx, (y-cy)/ry
            if nx*nx + ny*ny <= 1.0:
                mask[y][x] = 1
                idx[y][x] = C_DISC
    gh, gw = len(glyph), len(glyph[0])
    gx0 = int(round(cx - gw/2.0))
    gy0 = int(round(cy - gh/2.0))
    def put(px, py):
        if 0 <= px < W and 0 <= py < H and mask[py][px]:
            idx[py][px] = C_SYM
    for r, row in enumerate(glyph):       # body
        for c, ch in enumerate(row):
            if ch == '1':
                put(gx0+c, gy0+r)
    for col in stem_cols:                 # stems poke above/below the body
        for dx in range(stem_w):
            for k in range(1, stem_h+1):
                put(gx0+col+dx, gy0-k)
                put(gx0+col+dx, gy0+gh-1+k)
    return idx, mask


def pack_4bpp(grid):
    """Pack an index grid to 320-mode 4bpp bytes (hi nibble = left pixel)."""
    H, W = len(grid), len(grid[0])
    bpr = (W + 1)//2
    out = bytearray()
    for y in range(H):
        for bx in range(bpr):
            x = bx*2
            hi = grid[y][x] & 0xF
            lo = grid[y][x+1] & 0xF if x+1 < W else 0
            out.append((hi << 4) | lo)
    return bytes(out)


def mask_4bpp(mask):
    """Pack a 0/1 mask to 4bpp where opaque=$F, transparent=$0."""
    g = [[0xF if v else 0x0 for v in row] for row in mask]
    return pack_4bpp(g)


def icon_record(idx, mask):
    H, W = len(idx), len(idx[0])
    img = pack_4bpp(idx)
    msk = mask_4bpp(mask)
    assert len(img) == len(msk)
    hdr = struct.pack('<HHHH', 0x8000, len(img), H, W)   # color icon
    return hdr + img + msk


def pstr(s, total):
    b = s.encode('ascii')
    assert len(b) <= total-1
    return bytes([len(b)]) + b + b'\x00'*(total-1-len(b))


def build_iconfile():
    big_i, big_m = draw_logo(24, 18, B_BIG,   stem_cols=(1, 5), stem_w=2, stem_h=2)
    sml_i, sml_m = draw_logo(16, 15, B_SMALL, stem_cols=(1, 3), stem_w=1, stem_h=1)
    big = icon_record(big_i, big_m)
    sml = icon_record(sml_i, sml_m)

    # ---- IconData record ----
    boss = pstr("", 64)                 # application icon -> empty boss path
    name = pstr("GSMINE*", 16)          # matches GSMINE88 ... and a future GSMINER
    body = boss + name + struct.pack('<HH', 0x00B3, 0x0000) + big + sml
    rec_len = 2 + len(body)             # iDataLen counts itself
    data = struct.pack('<H', rec_len) + body
    data += struct.pack('<H', 0)        # terminating iDataLen = 0

    # ---- file header (26 bytes) ----
    header  = struct.pack('<L', 0)      # iBlkNext
    header += struct.pack('<H', 0x0001) # iBlkID
    header += struct.pack('<L', 0)      # iBlkPath
    header += pstr("GS.MINER", 16)      # iBlkName
    return header + data, (big_i, big_m), (sml_i, sml_m)


def write_png(path, rows_rgb):
    H = len(rows_rgb); W = len(rows_rgb[0])
    raw = bytearray()
    for row in rows_rgb:
        raw.append(0)
        for (r, g, b) in row:
            raw += bytes((r, g, b))
    def chunk(tag, data):
        c = tag + data
        return struct.pack('>I', len(data)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
    png  = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(bytes(raw), 9))
    png += chunk(b'IEND', b'')
    with open(path, 'wb') as f:
        f.write(png)


def proof(big, sml):
    """Compose a side-by-side proof on a desktop-grey field, aspect-corrected
    (320-mode pixels render ~1.2x taller than wide); checker shows transparency."""
    (bi, bm), (si, sm) = big, sml
    SX, SY = 6, 7                       # ~1.17 tall:wide -> round coin
    BG = (0x88, 0x88, 0x88)            # finder desktop grey-ish
    def blit(idx, mask, ox, oy, canvas):
        H, W = len(idx), len(idx[0])
        for y in range(H):
            for x in range(W):
                rgb = PAL[idx[y][x]] if mask[y][x] else None
                for dy in range(SY):
                    for dx in range(SX):
                        cx, cy = ox + x*SX+dx, oy + y*SY+dy
                        if rgb is None:
                            # checkerboard to show transparent area
                            chk = (((cx//4)+(cy//4)) & 1)
                            canvas[cy][cx] = (0x66,0x66,0x66) if chk else (0x77,0x77,0x77)
                        else:
                            canvas[cy][cx] = rgb
    PADX, PADY, GAP = 10, 10, 16
    bw, bh = len(bi[0])*SX, len(bi)*SY
    sw, sh = len(si[0])*SX, len(si)*SY
    CW = PADX*2 + bw + GAP + sw
    CH = PADY*2 + max(bh, sh)
    canvas = [[BG for _ in range(CW)] for _ in range(CH)]
    blit(bi, bm, PADX, PADY, canvas)
    blit(si, sm, PADX+bw+GAP, PADY + (bh-sh)//2, canvas)
    write_png(os.path.join(HERE, "icon_proof.png"), canvas)


def swatch_cells(W, H, cols, rows):
    """Grid of the 16 palette indices, 1px transparent gutters between cells.
    Cell i = palette index i, laid out left->right, top->bottom."""
    idx  = [[0]*W for _ in range(H)]
    mask = [[0]*W for _ in range(H)]
    cw, ch = W//cols, H//rows
    for r in range(rows):
        for c in range(cols):
            i = min(r*cols + c, 15)
            x0, x1 = c*cw, (W if c == cols-1 else (c+1)*cw)
            y0, y1 = r*ch, (H if r == rows-1 else (r+1)*ch)
            for y in range(y0+1, y1):
                for x in range(x0+1, x1):
                    idx[y][x] = i
                    mask[y][x] = 1
    return idx, mask


def build_swatch():
    """Diagnostic icon: 16 colour cells so a single Finder screenshot reveals
    which 4-bit index renders as gold/orange on the real 640-mode desktop."""
    big = swatch_cells(112, 26, 8, 2)   # top row idx 0..7, bottom 8..15
    sml = swatch_cells(16, 16, 4, 4)
    bigr = icon_record(*big)
    smlr = icon_record(*sml)
    boss = pstr("", 64)
    name = pstr("GSMINE*", 16)
    body = boss + name + struct.pack('<HH', 0x00B3, 0x0000) + bigr + smlr
    data = struct.pack('<H', 2+len(body)) + body + struct.pack('<H', 0)
    header  = struct.pack('<L', 0) + struct.pack('<H', 0x0001)
    header += struct.pack('<L', 0) + pstr("GS.MINER", 16)
    out = os.path.join(HERE, "gsminer_swatch.icons")
    with open(out, 'wb') as f:
        f.write(header + data)
    proof(big, sml)
    print("wrote %s (%d bytes) + icon_proof.png" % (out, len(header+data)))


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "swatch":
        build_swatch()
        return
    blob, big, sml = build_iconfile()
    out = os.path.join(HERE, "gsminer.icons")
    with open(out, 'wb') as f:
        f.write(blob)
    proof(big, sml)
    print("wrote %s (%d bytes)" % (out, len(blob)))
    print("wrote %s" % os.path.join(HERE, "icon_proof.png"))


if __name__ == "__main__":
    main()
