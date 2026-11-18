#!/usr/bin/env python3
"""CYBERPOP: learn the style table from a folder of MIDI packs (standard library only).

  python3 -I learn_pack.py <folder or .mid files> [...] [--out ../CyberpopData.inc] [--json table.json] [--label "pack set 1"]
                           [--only "file#track" ...] [--skip "file#track" ...] [--min-notes 8] [--chords "file#track" ...]
  --only / --skip pick tracks by a substring of "<file name>#<track name>" (case-insensitive), after the melodic filter; use them to
  keep leads / bells / chords and drop 808s, drum maps and fragments. The tracks used are printed, so the choice is on record.
  --chords marks a chord / pad track (its top line is learned; the melodic filter and the note floor do not apply to it, its notes
  still weigh by count); --min-notes is the floor for an ordinary line (default 8).

What it keeps (aggregate, low-order statistics only; the generator in Mutant.cpp samples from these):
  - rhythm: the probability of an onset at each 1/16 of the bar, notes per bar, note-length shares, swing,
  - pitch motion: the distribution of moves between successive notes (-12..+12 semitones, larger moves folded into +-12),
    leap / octave / repeated-note shares, the post-leap reversal rate,
  - tonality: pitch-class weights relative to the detected key (minor = 0 is the tonic), the share of onsets on the key's
    tonic-triad tones on strong vs weak steps (our stand-in for chord tones: packs carry no chord track), the in-scale share,
  - dynamics: the mean velocity by 1/16 position and its spread, the share and beat position of ratchets (sub-1/16 repeats),
  - form: the phrase / repeat period (1, 2 or 4 bars), how often a bar repeats an earlier bar's rhythm (and its pitches), how
    many notes change between a bar and its repeat, the median range and register.
What it never keeps: note sequences, phrases, n-grams, file or track names, or anything an artist could be identified by. The
table is written as a C++ initializer (CyberpopData.inc) the plug-in compiles in; the packs stay where they are.
The 4-note no-copy check lives next to this: midi_ngram_check.py --ref <packs> --gen <our generated .mid> (run it after
every table update: ArpEZatorTests --dump-mid <dir> writes the generated files).
"""
import os, sys, json, math, time, importlib.util
from collections import Counter

here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("midi_stats", os.path.join(here, "midi_stats.py")); ms = importlib.util.module_from_spec(spec); spec.loader.exec_module(ms)

MINOR = [6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17]   # Krumhansl-Kessler key profiles
MAJOR = [6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88]
MINOR_SCALE = {0, 2, 3, 5, 7, 8, 10, 11}   # natural + harmonic (the raised 7th counts as in-scale)
MAJOR_SCALE = {0, 2, 4, 5, 7, 9, 11}

def detect_key(on):
    w = [0.0] * 12
    for t, p, d, v in on: w[p % 12] += max(1, d)
    best = (-2, 0, True)
    for minor in (True, False):
        prof = MINOR if minor else MAJOR
        for k in range(12):
            x = [w[(k + i) % 12] for i in range(12)]
            mx, mp = sum(x) / 12, sum(prof) / 12
            num = sum((a - mx) * (b - mp) for a, b in zip(x, prof)); den = math.sqrt(sum((a - mx) ** 2 for a in x) * sum((b - mp) ** 2 for b in prof)) or 1
            r = num / den
            if r > best[0]: best = (r, k, minor)
    return best[1], best[2]

def monophonic(notes):
    notes = sorted(notes); on = []
    for n in notes:
        if not on or on[-1][0] != n[0]: on.append(list(n))
        elif n[1] > on[-1][1]: on[-1] = list(n)   # the top note of a chord onset
    return on

def file_stats(on, ppq, floor=8):
    """one melodic line -> its aggregate numbers (all shares / histograms, no sequences)"""
    if len(on) < max(2, floor): return None
    six = ppq / 4.0; bar = ppq * 4; beat = ppq
    key, minor = detect_key(on)
    scale = MINOR_SCALE if minor else MAJOR_SCALE; triad = {0, 3, 7} if minor else {0, 4, 7}
    t0 = (on[0][0] // bar) * bar
    span = on[-1][0] + on[-1][2] - t0; bars = max(1.0, span / bar)
    S = dict(notes=len(on), bars=bars)
    # rhythm
    onset = [0.0] * 16; durs = [0.0] * 6; swing = []
    for i, (t, p, d, v) in enumerate(on):
        pos = ((t - t0) % bar) / six; k = int(round(pos)) % 16; onset[k] += 1
        if abs(pos - round(pos)) > 0.1 and int(round(pos)) % 2 == 1: swing.append(pos - math.floor(pos))
        # the note's rhythmic value: its spacing to the next onset when it runs into it (a staccato 1/16 is still a 1/16, a
        # sub-1/16 spacing is a ratchet), its own length when a rest follows
        ioi = (on[i + 1][0] - t) if i + 1 < len(on) else d
        val = ioi if (ioi <= 2 * six or d >= 0.6 * ioi) else d
        q = val / six
        durs[0 if q < 0.75 else 1 if q < 1.5 else 2 if q < 3 else 3 if q < 6 else 4 if q < 12 else 5] += 1
    nb = max(1.0, math.ceil(bars))
    S['onset'] = [min(1.0, x / nb) for x in onset]; S['dur'] = [x / len(on) for x in durs]; S['perBar'] = len(on) / bars
    S['swing'] = (sum(swing) / len(swing)) if len(swing) >= 4 else 0.5
    # pitch motion
    ivs = [on[k + 1][1] - on[k][1] for k in range(len(on) - 1)]
    hist = [0.0] * 25
    for v in ivs: hist[max(-12, min(12, v)) + 12] += 1
    S['interval'] = [x / max(1, len(ivs)) for x in hist]
    S['leap'] = sum(1 for v in ivs if abs(v) >= 7) / max(1, len(ivs)); S['oct'] = sum(1 for v in ivs if abs(v) in (12, 24)) / max(1, len(ivs))
    S['repeat'] = sum(1 for v in ivs if v == 0) / max(1, len(ivs)); S['step'] = sum(1 for v in ivs if 1 <= abs(v) <= 2) / max(1, len(ivs))
    rev = foll = 0
    for k in range(1, len(ivs)):
        if abs(ivs[k - 1]) >= 7 and ivs[k] != 0: foll += 1; rev += (ivs[k] > 0) != (ivs[k - 1] > 0)
    S['reversal'] = rev / foll if foll else 0.75
    # tonality (relative to the detected key)
    deg = [0.0] * 12; strong = weak = sS = sW = inScale = 0
    for t, p, d, v in on:
        r = (p - key) % 12; deg[r] += 1
        st = ((t - t0) % beat) < six * 0.5
        if st: strong += 1; sS += r in triad
        else: weak += 1; sW += r in triad
        inScale += r in scale
    S['degree'] = [x / len(on) for x in deg]; S['chordStrong'] = sS / max(1, strong); S['chordWeak'] = sW / max(1, weak); S['scale'] = inScale / len(on)
    S['minor'] = 1.0 if minor else 0.0
    # dynamics
    vp = [[] for _ in range(16)]
    for t, p, d, v in on: vp[int(round(((t - t0) % bar) / six)) % 16].append(v / 127.0)
    allv = [v / 127.0 for t, p, d, v in on]; mean = sum(allv) / len(allv)
    S['velPos'] = [(sum(x) / len(x)) if x else mean for x in vp]; S['velSd'] = math.sqrt(sum((x - mean) ** 2 for x in allv) / len(allv))
    # ratchets: onsets closer than 3/4 of a 1/16 to the one before, and where in the beat they fall
    gaps = [on[k + 1][0] - on[k][0] for k in range(len(on) - 1)]
    rp = [0.0] * 4; rn = 0
    for k, g in enumerate(gaps):
        if g < six * 0.75: rn += 1; rp[int(((on[k + 1][0] - t0) % beat) / six) % 4] += 1
    S['ratchet'] = rn / max(1, len(gaps)); S['ratchetPos'] = [x / rn for x in rp] if rn else [0.25] * 4
    # form: bar rhythm signatures (onset steps) and pitch signatures
    barsR = {}; barsP = {}
    for t, p, d, v in on:
        b = (t - t0) // bar; barsR.setdefault(b, []).append(int(round(((t - t0) % bar) / six)) % 16); barsP.setdefault(b, []).append(p)
    keys = sorted(barsR); repR = repP = 0; period = Counter(); changed = []
    for i, b in enumerate(keys):
        if i == 0: continue
        prevs = [q for q in keys[:i] if barsR[q] == barsR[b]]
        if prevs:
            repR += 1; q = prevs[-1]
            diff = sum(1 for a, c in zip(barsP[b], barsP[q]) if a != c) / max(1, len(barsP[b])); changed.append(diff)
            if barsP[b] == barsP[q]: repP += 1
            per = b - q; period[1 if per == 1 else 2 if per == 2 else 4] += 1
    n1 = max(1, len(keys) - 1)
    S['barRepeat'] = repR / n1; S['barRepeatExact'] = repP / n1; S['variation'] = (sum(changed) / len(changed)) if changed else 0.3
    tot = sum(period.values()) or 1; S['motifBars'] = [period[1] / tot, period[2] / tot, period[4] / tot] if period else [0.3, 0.5, 0.2]
    S['range'] = max(p for t, p, d, v in on) - min(p for t, p, d, v in on)
    ps = sorted(p for t, p, d, v in on); S['reg'] = ps[len(ps) // 2]
    return S

def aggregate(stats):
    """note-weighted mean of every field (medians for range / register)"""
    W = sum(s['notes'] for s in stats); A = {}
    def mean(f): return sum(s[f] * s['notes'] for s in stats) / W
    def meanv(f): n = len(stats[0][f]); return [sum(s[f][i] * s['notes'] for s in stats) / W for i in range(n)]
    for f in ('perBar', 'swing', 'leap', 'oct', 'repeat', 'step', 'reversal', 'chordStrong', 'chordWeak', 'scale', 'minor', 'velSd', 'ratchet', 'barRepeat', 'barRepeatExact', 'variation'): A[f] = mean(f)
    for f in ('onset', 'dur', 'interval', 'degree', 'velPos', 'ratchetPos', 'motifBars'): A[f] = meanv(f)
    sd = math.sqrt(sum(s['notes'] * (s['perBar'] - A['perBar']) ** 2 for s in stats) / W); A['perBarSd'] = sd
    rg = sorted(s['range'] for s in stats); A['range'] = rg[len(rg) // 2]
    rs = sorted(s['reg'] for s in stats); A['reg'] = rs[len(rs) // 2]
    A['files'] = len(stats); A['notes'] = W
    return A

def fmt(v): return ', '.join(f"{x:.3f}" for x in v)

def emit_inc(A, label):
    L = []
    L.append("// CYBERPOP style table: aggregate statistics learned by tools/learn_pack.py (see its header for what is and is not kept).")
    L.append(f"// source: {label}; {A['files']} melodic tracks, {A['notes']} notes; written {time.strftime('%Y-%m-%d')}. Regenerate with learn_pack.py; do not edit by hand.")
    L.append("{")
    L.append(f'    "{label}", {A["files"]}, {A["notes"]},')
    L.append(f"    {A['perBar']:.3f}, {A['perBarSd']:.3f}, {A['swing']:.3f},")
    L.append(f"    {{ {fmt(A['onset'])} }},")
    L.append(f"    {{ {fmt(A['dur'])} }},")
    L.append(f"    {{ {fmt(A['interval'])} }},")
    L.append(f"    {A['leap']:.3f}, {A['oct']:.3f}, {A['reversal']:.3f}, {A['repeat']:.3f}, {A['step']:.3f},")
    L.append(f"    {A['chordStrong']:.3f}, {A['chordWeak']:.3f}, {A['scale']:.3f}, {A['minor']:.3f},")
    L.append(f"    {{ {fmt(A['degree'])} }},")
    L.append(f"    {{ {fmt(A['velPos'])} }}, {A['velSd']:.3f},")
    L.append(f"    {A['ratchet']:.3f}, {{ {fmt(A['ratchetPos'])} }},")
    L.append(f"    {{ {fmt(A['motifBars'])} }}, {A['barRepeat']:.3f}, {A['barRepeatExact']:.3f}, {A['variation']:.3f},")
    L.append(f"    {int(A['range'])}, {int(A['reg'])}")
    L.append("}")
    return "\n".join(L) + "\n"

def main():
    args = sys.argv[1:]; out = None; js = None; label = "MIDI packs"; only = []; skip = []; chords = []; floor = 8
    paths = []
    i = 0
    while i < len(args):
        a = args[i]
        if a == '--out': out = args[i + 1]; i += 2
        elif a == '--json': js = args[i + 1]; i += 2
        elif a == '--label': label = args[i + 1]; i += 2
        elif a == '--only': only.append(args[i + 1].lower()); i += 2
        elif a == '--skip': skip.append(args[i + 1].lower()); i += 2
        elif a == '--chords': chords.append(args[i + 1].lower()); i += 2
        elif a == '--min-notes': floor = int(args[i + 1]); i += 2
        else: paths.append(a); i += 1
    if not paths: print(__doc__); return 1
    stats = []; skipped = 0
    for f in ms.walk(paths):
        if not f.lower().endswith(('.mid', '.midi')): continue
        r = ms.smf_tracks(f)
        if r is None: skipped += 1; continue
        ppq, tracks = r
        for name, notes in tracks:
            key = (os.path.basename(f) + "#" + name).lower()
            pad = any(c in key for c in chords)
            if not pad and not ms.melodic(notes): continue
            if not pad and only and not any(o in key for o in only): continue
            if any(k in key for k in skip): continue
            s = file_stats(monophonic(notes), ppq, 2 if pad else floor)
            if s: stats.append(s); print(f"  using {os.path.basename(f)} # {name}{' (chord track, top line)' if pad else ''}: {s['notes']} notes over {s['bars']:.1f} bars")
    if not stats: print("no melodic tracks found"); return 1
    A = aggregate(stats)
    print(f"{A['files']} melodic tracks, {A['notes']} notes ({skipped} unreadable files)")
    print(f"notes/bar {A['perBar']:.1f} (sd {A['perBarSd']:.1f})  steps {A['step']:.2f} leaps {A['leap']:.2f} octaves {A['oct']:.2f} repeats {A['repeat']:.2f} reversal {A['reversal']:.2f}")
    print(f"tonic-triad share strong {A['chordStrong']:.2f} weak {A['chordWeak']:.2f}, in scale {A['scale']:.2f}, minor {A['minor']:.2f}; ratchets {A['ratchet']:.2f}; bar repeats {A['barRepeat']:.2f} (exact {A['barRepeatExact']:.2f}), {A['variation']:.2f} of notes change; period 1/2/4 bars {fmt(A['motifBars'])}")
    print(f"range {A['range']} st, register median MIDI {A['reg']}; onset grid {' '.join(f'{x:.2f}' for x in A['onset'])}")
    print(f"durations <1/16 1/16 1/8 1/4 1/2 longer: {fmt(A['dur'])}")
    if js: json.dump(A, open(js, 'w'), indent=1)
    inc = emit_inc(A, label)
    if out: open(out, 'w').write(inc); print(f"wrote {out}")
    else: print(inc)
    return 0

if __name__ == '__main__': sys.exit(main())
