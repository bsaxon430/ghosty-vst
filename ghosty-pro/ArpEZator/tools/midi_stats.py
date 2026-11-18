#!/usr/bin/env python3
"""Reference-pack statistics for the MUTANT ARP research (standard library only).

Reads .mid files (SMF 0/1) and FL Studio .flp projects (pattern notes), keeps only melodic tracks / patterns, and prints
the numbers RESEARCH.md compares the generator against: notes per bar, interval shares (steps / leaps / octaves),
post-leap reversal rate, repeated-fragment share, pitch range, velocity spread, ratchet (sub-1/16 gap) share.
Nothing is written out: the packs stay where they are and no note data is copied anywhere.

  python3 -I midi_stats.py <file or folder> [...]
"""
import os, struct, sys
from collections import Counter

# ---------------------------------------------------------------- SMF
def read_varlen(b, i):   # SMF: big-endian 7-bit groups
    v = 0
    while True:
        c = b[i]; i += 1; v = (v << 7) | (c & 0x7F)
        if not (c & 0x80): return v, i

def read_varlen_le(b, i):   # FLP: little-endian 7-bit groups
    v = 0; k = 0
    while True:
        c = b[i]; i += 1; v |= (c & 0x7F) << (7 * k); k += 1
        if not (c & 0x80): return v, i

def smf_tracks(path):
    """-> (ppq, [(name, [(tick, pitch, dur, vel)])])"""
    b = open(path, 'rb').read()
    if b[:4] != b'MThd': return None
    ppq = struct.unpack('>H', b[12:14])[0]
    ntr = struct.unpack('>H', b[10:12])[0]
    i = 14; out = []
    for _ in range(ntr):
        if b[i:i+4] != b'MTrk': break
        ln = struct.unpack('>I', b[i+4:i+8])[0]; d = b[i+8:i+8+ln]; i += 8 + ln
        j = 0; t = 0; st = 0; name = ''; notes = []; on = {}
        while j < len(d):
            dt, j = read_varlen(d, j); t += dt
            s = d[j]
            if s == 0xFF:
                typ = d[j+1]; l, j2 = read_varlen(d, j+2); data = d[j2:j2+l]; j = j2 + l
                if typ == 3: name = data.decode('latin1', 'ignore')
                continue
            if s in (0xF0, 0xF7):
                l, j2 = read_varlen(d, j+1); j = j2 + l; continue
            if s & 0x80: st = s; j += 1
            hi = st & 0xF0
            if hi in (0x80, 0x90, 0xA0, 0xB0, 0xE0):
                a, c = d[j], d[j+1]; j += 2
                if hi == 0x90 and c > 0: on.setdefault(a, []).append((t, c))
                elif hi in (0x80, 0x90) and a in on and on[a]:
                    t0, v = on[a].pop(0); notes.append((t0, a, max(1, t - t0), v))
            else: j += 1
        out.append((name, notes))
    return ppq, out

# ---------------------------------------------------------------- FLP (pattern notes only)
def flp_patterns(path):
    b = open(path, 'rb').read()
    if b[:4] != b'FLhd': return None
    ppq = struct.unpack('<H', b[12:14])[0]
    i = 8 + struct.unpack('<I', b[4:8])[0]
    assert b[i:i+4] == b'FLdt'; ln = struct.unpack('<I', b[i+4:i+8])[0]; d = b[i+8:i+8+ln]
    j = 0; pat = 0; pats = {}; names = {}; chan_names = []; cur_chan = -1
    while j < len(d):
        ev = d[j]; j += 1
        if ev < 64: j += 1; continue
        if ev < 128:
            if ev == 65: pat = struct.unpack('<H', d[j:j+2])[0]
            j += 2; continue
        if ev < 192: j += 4; continue
        l, j = read_varlen_le(d, j); data = d[j:j+l]; j += l
        if ev == 192: chan_names.append(data.decode('utf-16le', 'ignore').rstrip('\0'))
        elif ev == 224:   # pattern notes: 24-byte records
            notes = []
            for k in range(0, len(data) - 23, 24):
                pos, flags, rack, length, key = struct.unpack('<IHHIH', data[k:k+14])
                vel = data[k+21]
                notes.append((pos, key, max(1, length), vel, rack))
            pats.setdefault(pat, []).extend(notes)
        elif ev == 193: names[pat] = data.decode('utf-16le', 'ignore').rstrip('\0')   # pattern name
    return ppq, pats, names, chan_names

# ---------------------------------------------------------------- statistics
def stats(notes, ppq):
    """notes: list of (tick, pitch, dur, vel) for one melodic line"""
    notes = sorted(notes)
    if len(notes) < 8: return None
    on = []
    for n in notes:
        if not on or on[-1][0] != n[0]: on.append(n)
        elif n[1] > on[-1][1]: on[-1] = n   # top note of a chord onset
    bar = ppq * 4; sixteenth = ppq / 4
    span = max(1, (on[-1][0] + on[-1][2] - on[0][0]))
    bars = max(1.0, span / bar)
    ivs = [on[k+1][1] - on[k][1] for k in range(len(on) - 1)]
    nz = [v for v in ivs if v != 0] or [0]
    leaps = sum(1 for v in ivs if abs(v) >= 7); octs = sum(1 for v in ivs if abs(v) in (12, 24)); steps = sum(1 for v in ivs if 1 <= abs(v) <= 2); reps = sum(1 for v in ivs if v == 0)
    rev = foll = 0
    for k in range(1, len(ivs)):
        if abs(ivs[k-1]) >= 7 and ivs[k] != 0:
            foll += 1; rev += (ivs[k] > 0) != (ivs[k-1] > 0)
    gaps = [on[k+1][0] - on[k][0] for k in range(len(on) - 1)]
    ratch = sum(1 for g in gaps if g < sixteenth * 0.75)
    grid = Counter()
    for g in gaps:
        q = g / sixteenth
        grid['1/32-' if q < 0.75 else '1/16' if q < 1.5 else '1/8' if q < 3 else '1/4+' ] += 1
    vels = [n[3] for n in on]; vspread = (max(vels) - min(vels)) / 127 if vels else 0
    # repeated 3-note fragments (two intervals + two gaps, quantised to 1/16)
    frags = Counter()
    for k in range(len(on) - 2):
        frags[(ivs[k], ivs[k+1], round(gaps[k] / sixteenth), round(gaps[k+1] / sixteenth))] += 1
    rep3 = sum(c for c in frags.values() if c >= 2) / max(1, len(on) - 2)
    pcs = Counter(n[1] % 12 for n in on)
    return dict(notes=len(on), bars=round(bars, 1), per_bar=len(on) / bars, step=steps / max(1, len(ivs)), leap=leaps / max(1, len(ivs)), oct=octs / max(1, len(ivs)),
                rep=reps / max(1, len(ivs)), rev=rev / foll if foll else float('nan'), ratchet=ratch / max(1, len(gaps)), vspread=vspread,
                range=max(n[1] for n in on) - min(n[1] for n in on), rep3=rep3, lo=min(n[1] for n in on), hi=max(n[1] for n in on), npc=len(pcs),
                grid={k: round(v / max(1, len(gaps)), 2) for k, v in grid.items()})

def melodic(notes):
    """a line, not a drum / chord track: several pitches, mostly one note per onset, mid/high register"""
    if len(notes) < 8: return False
    pitches = set(n[1] for n in notes)
    if len(pitches) < 4: return False
    onsets = Counter(n[0] for n in notes); poly = sum(1 for c in onsets.values() if c >= 3) / len(onsets)
    if poly > 0.5: return False
    return sum(n[1] for n in notes) / len(notes) >= 48

def report(label, st):
    if st is None: return
    print(f"{label[:44]:44s} n/bar {st['per_bar']:5.1f} step {st['step']:.2f} leap {st['leap']:.2f} oct {st['oct']:.2f} rep {st['rep']:.2f} rev {st['rev']:.2f} "
          f"ratch {st['ratchet']:.2f} vel {st['vspread']:.2f} rng {st['range']:2d} ({st['lo']}-{st['hi']}) frag {st['rep3']:.2f} pcs {st['npc']} grid {st['grid']}")

def walk(paths):
    for p in paths:
        if os.path.isdir(p):
            for root, _, files in os.walk(p):
                for f in sorted(files):
                    if f.lower().endswith(('.mid', '.midi', '.flp')): yield os.path.join(root, f)
        else: yield p

def main():
    agg = []
    for f in walk(sys.argv[1:]):
        try:
            if f.lower().endswith('.flp'):
                r = flp_patterns(f)
                if r is None: continue
                ppq, pats, names, chans = r
                print(f"== {os.path.basename(f)} (FLP, ppq {ppq}, {len(pats)} patterns, channels: {', '.join(chans[:12])})")
                for k, notes in sorted(pats.items()):
                    by_rack = {}
                    for n in notes: by_rack.setdefault(n[4], []).append(n[:4])
                    for rack, nn in sorted(by_rack.items()):
                        cn = chans[rack] if rack < len(chans) else str(rack)
                        if melodic(nn):
                            st = stats(nn, ppq); report(f"  pat {k} {names.get(k, '')} / {cn}", st)
                            if st: agg.append(st)
            else:
                r = smf_tracks(f)
                if r is None: continue
                ppq, tracks = r
                for name, notes in tracks:
                    if melodic(notes):
                        st = stats(notes, ppq); report(f"{os.path.basename(f)[:30]} / {name}", st)
                        if st: agg.append(st)
        except Exception as e:
            print(f"!! {f}: {e}")
    if agg:
        keys = ['per_bar', 'step', 'leap', 'oct', 'rep', 'rev', 'ratchet', 'vspread', 'range', 'rep3']
        import math
        print(f"== MEAN over {len(agg)} melodic lines:", ' '.join(f"{k} {sum(a[k] for a in agg if not math.isnan(a[k])) / max(1, sum(1 for a in agg if not math.isnan(a[k]))):.2f}" for k in keys))

if __name__ == '__main__': main()
