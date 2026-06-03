#!/usr/bin/env python3
"""
make_mod.py - generate an original ProTracker .MOD for GS Miner.

Why a generator (not a static file): the SoundSmith/tracker vibe we're after
(Modulae / Nucleus) is all about a tight, looping, catchy hook over a driving
beat WITH AN ARC - a slow build, then a payoff. Having the whole song as data
lets us iterate fast: tweak a bassline, swap a hook, restructure the arrangement,
re-emit. Audition the .mod on the Mac (VLC) before we ever touch the GS, then
convert to NTP (sound/mod2ntp.py) and play it on iron via NinjaTracker Plus.

v2 changes (per direction):
  - section 2 is now a PERCUSSIVE BUILD: a driving 16th-note plucked bass that
    ratchets tension before the payoff.
  - PAYOFF/chorus: catchy lead melody + a layered second melody/harmony on top.
  - tightened the "warbly" voice: dropped the harsh 25%-duty pulse + the 0xy
    arpeggio EFFECT (which wobbles pitch); the arp now plays DISCRETE notes on a
    clean, edge-smoothed sample.
  - longer loop with a real arc: intro -> build -> payoff(A) -> payoff(B) -> breakdown.

Output: standard 4-channel, 31-instrument "M.K." ProTracker module (what NTP eats).
Looped melodic instruments are power-of-2 sized so NTP can free-run them on the
DOC with no per-loop interrupt (see ntpsources/file_format.txt).
"""
import math, struct, argparse

# ---- ProTracker Amiga period table (finetune 0), octaves 1..3 ----
NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
PERIODS = {
    1: [856,808,762,720,678,640,604,570,538,508,480,453],
    2: [428,404,381,360,339,320,302,285,269,254,240,226],
    3: [214,202,190,180,170,160,151,143,135,127,120,113],
}
def period(name, octave):
    return PERIODS[octave][NOTE_NAMES.index(name)]

# ---- sample synthesis: signed 8-bit PCM, -127..127 ----
def _q(x):
    return max(-127, min(127, int(round(x)))) & 0xFF

def clean_pulse(length=256, duty=0.5, amp=112, ramp=6):
    """Pulse with short linear edges -> tighter, less buzzy/warbly than a hard square."""
    out = []
    hi = int(length * duty)
    for i in range(length):
        # base level
        v = amp if i < hi else -amp
        # smooth the two transitions over `ramp` samples
        d_up = i                      # distance past the rising edge (at 0)
        d_dn = i - hi                 # distance past the falling edge
        if 0 <= d_dn < ramp:
            v = amp - (2 * amp) * (d_dn / ramp)
        elif 0 <= d_up < ramp:
            v = -amp + (2 * amp) * (d_up / ramp)
        out.append(_q(v))
    return bytes(out)

def soft_wave(length=256, amp=96):
    """Rounded sine+triangle blend: a warm, tight arp/pad voice (no warble)."""
    out = []
    for i in range(length):
        p = i / length
        s = math.sin(2 * math.pi * p)
        tri = (4 * p - 1) if p < 0.5 else (3 - 4 * p)
        out.append(_q(amp * (0.7 * s + 0.3 * tri)))
    return bytes(out)

def tri_cycle(length=128, amp=118):
    out = []
    for i in range(length):
        p = i / length
        v = (4 * p - 1) if p < 0.5 else (3 - 4 * p)
        out.append(_q(amp * v))
    return bytes(out)

_seed = [0x1234]
def _rng():
    _seed[0] = (_seed[0] * 1103515245 + 12345) & 0x7fffffff
    return (_seed[0] >> 8) / 0x7fffff - 1.0

def pluck_bass(length=512, amp=120):
    """Short, sharp-attack decaying tone (one-shot) -> percussive driving bass."""
    out = []
    cycles = 3.0
    for i in range(length):
        t = i / length
        env = math.exp(-5.5 * t)
        # blend triangle + a little square bite for punch
        ph = (cycles * i / length) % 1.0
        tri = (4 * ph - 1) if ph < 0.5 else (3 - 4 * ph)
        sq = 0.5 if ph < 0.5 else -0.5
        out.append(_q(amp * env * (0.75 * tri + 0.25 * sq)))
    return bytes(out)

def kick(length=720, amp=125):
    out = []
    for i in range(length):
        t = i / length
        f = 150 * (1 - t) + 45
        env = math.exp(-4.0 * t)
        out.append(_q(amp * env * math.sin(2 * math.pi * f * i / 8000.0)))
    return bytes(out)

def snare(length=640, amp=98):
    out = []
    for i in range(length):
        t = i / length
        env = math.exp(-7.0 * t)
        tone = 0.4 * math.sin(2 * math.pi * 180 * i / 8000.0)
        out.append(_q(amp * env * (0.6 * _rng() + tone)))
    return bytes(out)

def hat(length=256, amp=58):
    out = []
    for i in range(length):
        env = math.exp(-22.0 * (i / length))
        out.append(_q(amp * env * _rng()))
    return bytes(out)

def tom(length=560, amp=110):
    out = []
    for i in range(length):
        t = i / length
        f = 220 * (1 - 0.5 * t)
        env = math.exp(-5.0 * t)
        out.append(_q(amp * env * math.sin(2 * math.pi * f * i / 8000.0)))
    return bytes(out)

class Ins:
    def __init__(self, name, data, vol, loop):
        self.name = name; self.data = data; self.vol = vol; self.loop = loop

# instrument numbers (1-based) used in cells below
I_LEAD, I_HARM, I_ARP, I_BASS, I_PLUCK, I_KICK, I_SNARE, I_HAT, I_TOM = range(1, 10)
INSTRUMENTS = [
    Ins('lead.pulse', clean_pulse(256, 0.5, 112, 6), 44, True),  # 1 catchy lead
    Ins('harm.pulse', clean_pulse(256, 0.5, 92, 8),  32, True),  # 2 layered harmony
    Ins('arp.soft',   soft_wave(256, 96),            30, True),  # 3 tight arp/pad shimmer
    Ins('bass.tri',   tri_cycle(128, 118),           52, True),  # 4 sustained groove bass
    Ins('bass.pluck', pluck_bass(512, 120),          60, False), # 5 percussive build bass
    Ins('kick',       kick(720),                     64, False), # 6
    Ins('snare',      snare(640),                    48, False), # 7
    Ins('hat',        hat(256),                      30, False), # 8
    Ins('tom',        tom(560),                      52, False), # 9
]

# ---- pattern model: cell = (sample#, note, octave, effect, param) ----
EMPTY = (0, None, 0, 0, 0)
NCH = 4
ROWS = 64

def cell(p, name=None, octv=0, eff=0, par=0):
    return (p, name, octv, eff, par)

def blank():
    return [[EMPTY for _ in range(NCH)] for _ in range(ROWS)]

def put(pat, row, ch, c):
    if 0 <= row < ROWS:
        pat[row][ch] = c

PROG = ['Am', 'F', 'C', 'G']
CHORDS = {
    'Am': [('A', 2), ('C', 3), ('E', 3)],
    'F':  [('F', 2), ('A', 2), ('C', 3)],
    'C':  [('C', 3), ('E', 3), ('G', 3)],
    'G':  [('G', 2), ('B', 2), ('D', 3)],
}
ROOT = {'Am': ('A', 1), 'F': ('F', 1), 'C': ('C', 2), 'G': ('G', 1)}
THIRD = {'Am': ('C', 2), 'F': ('A', 1), 'C': ('E', 2), 'G': ('B', 1)}

# channel map: 0=lead/arp  1=bass  2=kick/snare(+hat on odd)  3=harmony/hat

def drums(pat, busy=False, hats_on_ch3=False, fill_last=False):
    for bar in range(4):
        b = bar * 16
        put(pat, b + 0, 2, cell(I_KICK, 'C', 2))
        put(pat, b + 8, 2, cell(I_KICK, 'C', 2))
        put(pat, b + 4, 2, cell(I_SNARE, 'C', 2))
        put(pat, b + 12, 2, cell(I_SNARE, 'C', 2))
        if busy:
            put(pat, b + 14, 2, cell(I_KICK, 'C', 2))   # pickup
        hat_ch = 3 if hats_on_ch3 else 2
        step = 1 if (busy or hats_on_ch3) else 2
        for r in range(0, 16, step):
            row = b + r
            if hat_ch == 2 and (pat[row][2] != EMPTY):
                continue                                 # don't stomp kick/snare
            put(pat, row, hat_ch, cell(I_HAT, 'C', 3))
    if fill_last:                                        # tom roll into the next section
        for r in (58, 60, 61, 62, 63):
            put(pat, r, 2, cell(I_TOM, 'C', 2))

def arp_layer(pat, ch=0, ins=I_ARP, step=4, octshift=0):
    """Discrete arpeggio (no pitch-wobble effect): chord tones cycling."""
    for bar, ch_name in enumerate(PROG):
        b = bar * 16
        tones = CHORDS[ch_name]
        seq = [tones[0], tones[1], tones[2], tones[1]]
        for k, r in enumerate(range(0, 16, step)):
            n, o = seq[k % len(seq)]
            put(pat, b + r, ch, cell(ins, n, o + octshift))

def groove_bass(pat, ch=1):
    for bar, ch_name in enumerate(PROG):
        b = bar * 16
        rn, ro = ROOT[ch_name]
        put(pat, b + 0, ch, cell(I_BASS, rn, ro))
        put(pat, b + 8, ch, cell(I_BASS, rn, ro + 1))    # octave bounce on beat 3

def percussive_bass(pat, ch=1, density=4):
    """Driving plucked bass; density=4 -> 16th notes (every row)."""
    for bar, ch_name in enumerate(PROG):
        b = bar * 16
        rn, ro = ROOT[ch_name]
        fifth = CHORDS[ch_name][2]                       # the 5th for movement
        for r in range(0, 16, max(1, 4 // density)):
            row = b + r
            if r % 8 == 0:
                put(pat, row, ch, cell(I_PLUCK, rn, ro))           # root accents
            elif r % 4 == 0:
                put(pat, row, ch, cell(I_PLUCK, rn, ro + 1))       # octave
            else:
                put(pat, row, ch, cell(I_PLUCK, fifth[0], fifth[1] - 1))  # passing 5th
    return pat

def melody_line(pat, ch, ins, notes):
    for (row, n, o) in notes:
        put(pat, row, ch, cell(ins, n, o))

# --- the two catchy payoff melodies (lead, ch0) ---
MELODY_A = [
    (0,'E',3),(4,'A',2),(6,'C',3),(8,'B',2),(10,'C',3),(12,'D',3),(14,'E',3),
    (16,'F',2),(20,'A',2),(22,'C',3),(24,'A',2),(28,'G',2),(30,'A',2),
    (32,'E',3),(36,'G',2),(38,'E',3),(40,'C',3),(42,'E',3),(44,'G',3),(46,'E',3),
    (48,'D',3),(52,'G',2),(54,'B',2),(56,'D',3),(58,'B',2),(60,'G',2),(62,'B',2),
]
MELODY_B = [   # higher, more syncopated - the "lift"
    (0,'A',3),(2,'G',3),(4,'E',3),(8,'A',3),(10,'B',3),(12,'A',3),(14,'G',3),
    (16,'A',3),(20,'F',3),(22,'A',2),(24,'C',3),(26,'F',3),(30,'C',3),
    (32,'G',3),(34,'E',3),(36,'C',3),(40,'E',3),(42,'G',3),(44,'C',3),(46,'G',3),
    (48,'B',3),(52,'D',3),(54,'G',3),(56,'B',3),(60,'D',3),(62,'G',3),
]
# layered harmony (harm, ch3): held thirds under the lead -> thickens the payoff
def harmony_pad(pat, ch=3):
    for bar, ch_name in enumerate(PROG):
        b = bar * 16
        tn, to = THIRD[ch_name]
        put(pat, b + 0, ch, cell(I_HARM, tn, to + 1))
        put(pat, b + 8, ch, cell(I_HARM, CHORDS[ch_name][2][0], CHORDS[ch_name][2][1]))

# ---- section builders ----
def pat_intro():
    p = blank(); drums(p, busy=False); groove_bass(p); arp_layer(p, ch=0, step=4); return p

def pat_intro_busy():
    p = blank(); drums(p, busy=True, hats_on_ch3=True); groove_bass(p); arp_layer(p, ch=0, step=2); return p

def pat_build():
    p = blank(); drums(p, busy=True, hats_on_ch3=True, fill_last=True)
    percussive_bass(p, density=4); arp_layer(p, ch=0, step=2); return p

def pat_payoff(melody):
    p = blank(); drums(p, busy=True, hats_on_ch3=False)
    # driving but musical bass: roots + octaves on 8ths
    for bar, ch_name in enumerate(PROG):
        b = bar * 16; rn, ro = ROOT[ch_name]
        for r in (0, 4, 8, 12):
            put(p, b + r, 1, cell(I_PLUCK, rn, ro if r % 8 == 0 else ro + 1))
    melody_line(p, 0, I_LEAD, melody)
    harmony_pad(p, ch=3)
    return p

def pat_break():
    p = blank()
    # breakdown: strip drums to soft hats, sustain bass + arp, tension before the loop
    for bar, ch_name in enumerate(PROG):
        b = bar * 16; rn, ro = ROOT[ch_name]
        put(p, b + 0, 1, cell(I_BASS, rn, ro))
        for r in range(0, 16, 4):
            put(p, b + r, 3, cell(I_HAT, 'C', 3))
    arp_layer(p, ch=0, step=4)
    for r in (60, 61, 62, 63):                           # snare roll back into intro
        put(p, r, 2, cell(I_SNARE, 'C', 2))
    return p

# ---- MOD writer ----
def encode_cell(c):
    s, name, octv, eff, par = c
    per = period(name, octv) if name else 0
    return bytes(((s & 0xF0) | ((per >> 8) & 0x0F), per & 0xFF,
                  ((s & 0x0F) << 4) | (eff & 0x0F), par & 0xFF))

def write_mod(path, title, patterns, order, restart=0):
    out = bytearray()
    out += title.encode('ascii', 'ignore')[:20].ljust(20, b'\0')
    for ins in INSTRUMENTS:
        words = len(ins.data) // 2
        rep_len = words if ins.loop else 1
        out += ins.name.encode('ascii', 'ignore')[:22].ljust(22, b'\0')
        out += struct.pack('>H', words) + bytes((0, max(0, min(64, ins.vol))))
        out += struct.pack('>H', 0) + struct.pack('>H', rep_len)
    for _ in range(len(INSTRUMENTS), 31):
        out += b'\0' * 22 + struct.pack('>H', 0) + bytes((0, 0)) + struct.pack('>H', 0) + struct.pack('>H', 1)
    out += bytes((len(order), restart)) + bytes(order) + b'\0' * (128 - len(order)) + b'M.K.'
    for pi in range(max(order) + 1):
        for row in range(ROWS):
            for ch in range(NCH):
                out += encode_cell(patterns[pi][row][ch])
    for ins in INSTRUMENTS:
        out += ins.data
    open(path, 'wb').write(out)
    return len(out)

def build(args):
    patterns = [pat_intro(), pat_intro_busy(), pat_build(),
                pat_payoff(MELODY_A), pat_payoff(MELODY_B), pat_break()]
    #          intro  intro+  build  build  payoffA payoffB payoffA  break
    order = [0, 1, 2, 2, 3, 4, 3, 5]
    # loop the ARC (skip the soft intro on repeats) -> restart at the build
    size = write_mod(args.out, 'GSMINER THEME v2', patterns, order, restart=2)
    print(f"wrote {args.out}  ({size} bytes, {len(INSTRUMENTS)} instruments, "
          f"{len(order)} positions, {max(order)+1} patterns, ~{len(order)*64*6/50:.0f}s/loop @spd6)")
    print("audition: open in VLC.  arc = intro -> build(x2) -> payoffA -> payoffB -> payoffA -> breakdown -> (loop to build)")

if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default='sound/gsminer_theme.mod')
    build(ap.parse_args())
