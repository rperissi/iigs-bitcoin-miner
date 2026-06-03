#!/usr/bin/env python3
"""
make_mod_v1.py - FROZEN snapshot of the GS Miner theme, version 1.
The very first pass: Am-F-C-G groove, arpeggiated 25%-duty pulse hook, bouncing
bass, kick/snare/hat. Kept verbatim so we can A/B against later versions.
Output: sound/gsminer_theme_v1.mod  (do not edit; see make_mod.py for the latest).
"""
import math, struct, argparse

NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
PERIODS = {
    1: [856,808,762,720,678,640,604,570,538,508,480,453],
    2: [428,404,381,360,339,320,302,285,269,254,240,226],
    3: [214,202,190,180,170,160,151,143,135,127,120,113],
}
def period(name, octave):
    return PERIODS[octave][NOTE_NAMES.index(name)]

def _q(x):
    return max(-127, min(127, int(round(x))))

def pulse_cycle(length=128, duty=0.5, amp=110):
    return bytes((_q(amp if (i / length) < duty else -amp) & 0xFF) for i in range(length))

def saw_cycle(length=128, amp=104):
    return bytes((_q(amp * (1 - 2 * (i / length))) & 0xFF) for i in range(length))

def tri_cycle(length=128, amp=112):
    out = []
    for i in range(length):
        p = i / length
        v = (4 * p - 1) if p < 0.5 else (3 - 4 * p)
        out.append(_q(amp * v) & 0xFF)
    return bytes(out)

_seed = [0x1234]
def _rng():
    _seed[0] = (_seed[0] * 1103515245 + 12345) & 0x7fffffff
    return (_seed[0] >> 8) / 0x7fffff - 1.0

def kick(length=700, amp=125):
    out = []
    for i in range(length):
        t = i / length
        f = 150 * (1 - t) + 45
        env = math.exp(-4.0 * t)
        out.append(_q(amp * env * math.sin(2 * math.pi * f * i / 8000.0)) & 0xFF)
    return bytes(out)

def snare(length=640, amp=96):
    out = []
    for i in range(length):
        t = i / length
        env = math.exp(-7.0 * t)
        tone = 0.4 * math.sin(2 * math.pi * 180 * i / 8000.0)
        out.append(_q(amp * env * (0.6 * _rng() + tone)) & 0xFF)
    return bytes(out)

def hat(length=256, amp=60):
    out = []
    for i in range(length):
        env = math.exp(-22.0 * (i / length))
        out.append(_q(amp * env * _rng()) & 0xFF)
    return bytes(out)

class Ins:
    def __init__(self, name, data, vol, loop):
        self.name = name; self.data = data; self.vol = vol; self.loop = loop

INSTRUMENTS = [
    Ins('lead.pulse',  pulse_cycle(128, 0.5, 110), 40, True),
    Ins('arp.pulse',   pulse_cycle(128, 0.25, 100), 34, True),
    Ins('bass.tri',    tri_cycle(128, 118),         50, True),
    Ins('pad.saw',     saw_cycle(128, 80),          26, True),
    Ins('kick',        kick(700),                   64, False),
    Ins('snare',       snare(640),                  46, False),
    Ins('hat',         hat(256),                    32, False),
]

EMPTY = (0, None, 0, 0, 0)
NCH = 4

def cell(p, name=None, octv=0, eff=0, par=0):
    return (p, name, octv, eff, par)

def blank_pattern():
    return [[EMPTY for _ in range(NCH)] for _ in range(64)]

def put(pat, row, ch, c):
    pat[row][ch] = c

CHORDS = {
    'Am': [('A', 1), ('C', 2), ('E', 2)],
    'F':  [('F', 1), ('A', 1), ('C', 2)],
    'C':  [('C', 2), ('E', 2), ('G', 2)],
    'G':  [('G', 1), ('B', 1), ('D', 2)],
}
BASSNOTE = {'Am': ('A', 1), 'F': ('F', 1), 'C': ('C', 1), 'G': ('G', 1)}

def groove_pattern(progression, lead_motif=True):
    pat = blank_pattern()
    for ci, chord in enumerate(progression):
        base = ci * 16
        bnote, boct = BASSNOTE[chord]
        notes = CHORDS[chord]
        for r in range(16):
            row = base + r
            if r in (0, 8):
                put(pat, row, 2, cell(5, *( ('C',2) ), 0, 0))
            elif r in (4, 12):
                put(pat, row, 2, cell(6, 'C', 2))
            elif r % 2 == 1:
                put(pat, row, 2, cell(7, 'C', 3))
            if r % 4 == 0:
                put(pat, row, 1, cell(3, bnote, boct))
            elif r % 4 == 2:
                put(pat, row, 1, cell(3, bnote, boct + 1))
            if lead_motif and r % 2 == 0:
                n, o = notes[(r // 2) % 3]
                put(pat, row, 0, cell(2, n, o + 1, 0x0, 0x37))
            if r == 0:
                put(pat, row, 3, cell(4, notes[0][0], notes[0][1] + 1))
    return pat

def hook_pattern(progression):
    pat = groove_pattern(progression, lead_motif=False)
    melody = [
        (0,'A',2),(2,'C',3),(4,'E',3),(6,'D',3),(8,'C',3),(10,'A',2),(12,'B',2),(14,'C',3),
        (16,'F',2),(18,'A',2),(20,'C',3),(22,'A',2),(24,'G',2),(26,'F',2),(28,'E',2),(30,'F',2),
        (32,'E',2),(34,'G',2),(36,'C',3),(38,'B',2),(40,'G',2),(42,'E',2),(44,'D',2),(46,'E',2),
        (48,'D',2),(50,'G',2),(52,'B',2),(54,'A',2),(56,'G',2),(58,'D',2),(60,'E',2),(62,'G',2),
    ]
    for (row, n, o) in melody:
        put(pat, row, 0, cell(1, n, o))
    return pat

def encode_cell(c):
    s, name, octv, eff, par = c
    per = period(name, octv) if name else 0
    b0 = (s & 0xF0) | ((per >> 8) & 0x0F)
    b1 = per & 0xFF
    b2 = ((s & 0x0F) << 4) | (eff & 0x0F)
    b3 = par & 0xFF
    return bytes((b0, b1, b2, b3))

def write_mod(path, title, patterns, order):
    out = bytearray()
    out += title.encode('ascii', 'ignore')[:20].ljust(20, b'\0')
    for ins in INSTRUMENTS:
        words = len(ins.data) // 2
        rep_len = words if ins.loop else 1
        out += ins.name.encode('ascii', 'ignore')[:22].ljust(22, b'\0')
        out += struct.pack('>H', words)
        out += bytes((0,))
        out += bytes((max(0, min(64, ins.vol)),))
        out += struct.pack('>H', 0)
        out += struct.pack('>H', rep_len)
    for _ in range(len(INSTRUMENTS), 31):
        out += b'\0' * 22 + struct.pack('>H', 0) + bytes((0, 0)) + struct.pack('>H', 0) + struct.pack('>H', 1)
    out += bytes((len(order),))
    out += bytes((127,))
    out += bytes(order) + b'\0' * (128 - len(order))
    out += b'M.K.'
    for pi in range(max(order) + 1):
        pat = patterns[pi]
        for row in range(64):
            for ch in range(NCH):
                out += encode_cell(pat[row][ch])
    for ins in INSTRUMENTS:
        out += ins.data
    with open(path, 'wb') as f:
        f.write(out)
    return len(out)

def build(args):
    prog = ['Am', 'F', 'C', 'G']
    patterns = [groove_pattern(prog), hook_pattern(prog), groove_pattern(['Am', 'F', 'G', 'G'])]
    order = [0, 0, 1, 2]
    size = write_mod(args.out, 'GSMINER THEME v1', patterns, order)
    print(f"wrote {args.out}  ({size} bytes) - frozen v1")

if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default='sound/gsminer_theme_v1.mod')
    build(ap.parse_args())
