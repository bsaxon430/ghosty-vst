#!/usr/bin/env python3
"""Verbatim check: does any 4+ note sequence (pitch intervals + rhythm) from the reference files appear in our generated MIDI?
  python3 -I midi_ngram_check.py --ref <files/folders> --gen <files/folders> [--n 4]
Reference note data is read, hashed into n-grams and discarded; nothing is written."""
import os, sys, importlib.util
here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("midi_stats", os.path.join(here, "midi_stats.py")); ms = importlib.util.module_from_spec(spec); spec.loader.exec_module(ms)

def lines(paths):
    for f in ms.walk(paths):
        if f.lower().endswith('.flp'):
            r = ms.flp_patterns(f)
            if r is None: continue
            ppq, pats, names, chans = r
            for k, notes in pats.items():
                by = {}
                for n in notes: by.setdefault(n[4], []).append(n[:4])
                for rack, nn in by.items(): yield f"{os.path.basename(f)}#pat{k}/{rack}", ppq, nn
        else:
            r = ms.smf_tracks(f)
            if r is None: continue
            ppq, tracks = r
            for name, notes in tracks: yield f"{os.path.basename(f)}/{name}", ppq, notes

def grams(notes, ppq, n):
    notes = sorted(notes); on = []
    for x in notes:
        if not on or on[-1][0] != x[0]: on.append(x)
        elif x[1] > on[-1][1]: on[-1] = x
    out = set(); six = ppq / 4
    for i in range(len(on) - n + 1):
        seg = on[i:i+n]
        ivs = tuple(seg[k+1][1] - seg[k][1] for k in range(n-1)); gaps = tuple(int((seg[k+1][0] - seg[k][0]) / six * 2 + 0.5) for k in range(n-1))   # in 32nds
        out.add((ivs, gaps))
    return out

def main():
    a = sys.argv[1:]; n = 4
    if '--n' in a: n = int(a[a.index('--n') + 1])
    ref = a[a.index('--ref') + 1: a.index('--gen')]; gen = a[a.index('--gen') + 1:]
    gen = [g for g in gen if g != '--n' and not g.isdigit()]
    refg = {}
    for label, ppq, notes in lines(ref):
        if len(notes) >= n: refg[label] = grams(notes, ppq, n)
    allref = set().union(*refg.values()) if refg else set()
    print(f"reference: {len(refg)} lines, {len(allref)} distinct {n}-grams (intervals + 32nd gaps)")
    total = 0; hits = {}
    for label, ppq, notes in lines(gen):
        g = grams(notes, ppq, n); total += len(g)
        for x in g & allref:
            hits.setdefault(x, []).append(label)
    print(f"generated: {total} {n}-grams checked; {len(hits)} shared with the references")
    for x, labs in list(hits.items())[:20]:
        src = [l for l, gs in refg.items() if x in gs]
        print(f"  shared {x}: in {len(labs)} generated lines (e.g. {labs[0]}), from {src[:3]}")
    # trivially common patterns (repeated same note on a steady grid) are reported separately
    trivial = [x for x in hits if set(x[0]) == {0} or (len(set(x[0])) == 1 and len(set(x[1])) == 1)]
    print(f"of which trivial (a single repeated pitch in any rhythm, or one interval on one gap = a plain run): {len(trivial)}; non-trivial shared: {len(hits) - len(trivial)}")
    sys.exit(1 if len(hits) - len(trivial) > 0 else 0)

if __name__ == '__main__': main()
