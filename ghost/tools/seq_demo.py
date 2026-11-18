#!/usr/bin/env python3
"""The bass sequencer, rendered: generated patterns in five styles with their grooves, then CHORD
mode following a chord progression. Output guard on (what you hear).
  SURGE_DATA_HOME=<data> python3 -I seq_demo.py <surgepy dir> <presets dir> <out.wav>"""
import os, sys
import numpy as np
from scipy.io import wavfile
from scipy.signal import resample_poly
sys.path.insert(0, sys.argv[1])
import surgepy  # noqa: E402

PRE, OUT = sys.argv[2], sys.argv[3]
SR = 48000
# (preset, style, scale, groove, amount, bpm, root, length, seed, chords)
#   styles: 0 OFFBEAT 1 ROLLING 2 SYNCOPATED 3 HALFTIME 4 ACID
#   grooves: 0 STRAIGHT 1 SWING16 2 SHUFFLE8 3 PUSH 4 LAZY;  scales: 0 MINOR 1 PHRYGIAN 2 DORIAN ...
SEGS = [
    ("HOUSE/SIDECHAIN PULSE", 0, 0, 1, 0.5, 124, 33, 16, 7, None),
    ("TECHNO & ACID/ACID SQUELCH", 4, 1, 3, 0.6, 132, 36, 16, 21, None),
    ("REESE & DNB/MOVING REESE", 1, 0, 0, 0.0, 172, 29, 32, 5, None),
    ("DUBSTEP & GROWL/VOWEL WOBBLE", 3, 0, 4, 0.5, 140, 31, 32, 3, None),
    ("BLOCK STEP BASSES/BS BASS 05", 2, 2, 2, 0.7, 120, 33, 32, 11, None),
    # CHORD mode: hold Am, F, C, G - the same pattern follows the chords
    ("ANALOG/LADDER DRIVE", 2, 0, 1, 0.4, 124, 33, 16, 4, [(45, 48, 52), (41, 45, 48), (36, 40, 43), (43, 47, 50)]),
]


def render(seg):
    name, style, scale, groove, amt, bpm, root, length, seed, chords = seg
    s = surgepy.createSurge(SR)
    s.loadPatch(os.path.join(PRE, name + ".fxp"))
    for _ in range(20):
        s.process()
    s.ghostSeqGenerate(scale, 0.65, seed, style, length, root)
    s.ghostSeqConfig(groove, amt, 0.55, chords is not None)
    s.ghostSeqPlay(True)
    bars = 4
    beats = bars * 4
    pps = bpm / (60.0 * SR)
    nb = int((beats / pps + 0.8 * SR) / 32)
    out = s.createMultiBlock(nb)
    held = []
    ppq = 0.0
    for b in range(nb):
        if chords is not None:
            bar = int(ppq // 4)
            want = list(chords[bar % len(chords)]) if ppq < beats else []
            if want != held:
                for n in held:
                    s.ghostSeqHold(n, False)
                for n in want:
                    s.ghostSeqHold(n, True)
                held = want
        if ppq >= beats:
            s.ghostSeqPlay(False)
        s.ghostSeqTick(ppq, bpm)
        s.processMultiBlock(out, b, 1)
        ppq += pps * 32
    return out.T.copy()


def main():
    parts = []
    gap = np.zeros((int(0.25 * SR), 2), np.float32)
    for seg in SEGS:
        x = render(seg)
        up = np.vstack([resample_poly(x[:, c], 4, 1) for c in range(2)]).T
        print("%-34s %-11s %3d bpm  TP %.1f dBTP" % (seg[0], "CHORD" if seg[9] else "", seg[5],
                                                     20 * np.log10(np.abs(up).max() + 1e-12)), flush=True)
        parts += [x, gap]
    y = np.concatenate(parts)
    os.makedirs(os.path.dirname(OUT) or ".", exist_ok=True)
    wavfile.write(OUT, SR, np.clip(y, -1, 1).astype(np.float32))
    os.system('ffmpeg -loglevel error -y -i "%s" -b:a 256k "%s"' % (OUT, OUT[:-4] + ".mp3"))
    print("wrote", OUT[:-4] + ".mp3", "%.1f s" % (len(y) / SR))


if __name__ == "__main__":
    main()
