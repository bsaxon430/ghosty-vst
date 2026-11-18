#!/usr/bin/env python3
"""Ghost Bass build 7 MORPH demos (SAFETY LIMITER off = what you hear):
  a held bassline on a MORPH preset; the blend is swung by the PENDULUM at a given rate, or by a drawn curve.
  SURGE_DATA_HOME=<data> python3 -I morph_demo.py <surgepy dir> <out.wav> <preset.fxp> <pend|draw> <rate index> <bars> <bpm>
  (rate index: 0 1/16, 1 1/8, 2 1/4, 3 1/2, 4 1 bar, 5 2 bars ...)"""
import os, sys
import numpy as np

sys.path.insert(0, sys.argv[1])
import surgepy  # noqa: E402

SR = 48000
OUT, PRE, KIND, RATE, BARS, BPM = sys.argv[2], sys.argv[3], sys.argv[4], int(sys.argv[5]), int(sys.argv[6]), float(sys.argv[7])
# two bars a note, low: the swing is plain to hear on held notes
LINE = [(33, 0, 7.8), (36, 8, 7.8), (31, 16, 7.8), (28, 24, 7.8)]

s = surgepy.createSurge(SR)
s.loadPatch(PRE)
s.ghostSetTransport(False, 0.0, BPM)
mode = s.ghostMorphInfo()[0]
if KIND == "pend":
    s.ghostMorphSet(0.0, True, RATE, 0, 0, False, 0.5, 1.0, False, BARS)
else:
    s.ghostMorphSet(0.0, False, RATE, 0, 0, False, 0.5, 1.0, True, BARS)
for _ in range(20):
    s.process()
spb = 60.0 / BPM * SR
total = int(32 * spb + 2.0 * SR)
nb = total // 32
out = s.createMultiBlock(nb)
ev = sorted([(int(st * spb), 1, n) for n, st, ln in LINE] + [(int((st + ln) * spb), 0, n) for n, st, ln in LINE])
k = 0
blends = []
for b in range(nb):
    t = b * 32
    while k < len(ev) and ev[k][0] <= t:
        _, on, n = ev[k]
        s.playNote(0, n, 100, 0) if on else s.releaseNote(0, n, 0)
        k += 1
    s.processMultiBlock(out, b, 1)
    blends.append(s.ghostMorphInfo()[1])
x = out.T.copy()
from scipy.io import wavfile  # noqa: E402
wavfile.write(OUT, SR, np.clip(x, -1, 1).astype(np.float32))
b = np.array(blends)
print("%s: mode %s, blend %.2f .. %.2f, peak %.1f dBFS, %.1f s" % (os.path.basename(PRE)[:-4], {1: "true morph", 2: "scene blend"}.get(mode, "?"),
                                                                    b.min(), b.max(), 20 * np.log10(np.max(np.abs(x)) + 1e-12), len(x) / SR))
