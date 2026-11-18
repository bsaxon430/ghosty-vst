#!/usr/bin/env python3
"""Ghost Bass build 8 (PURE SURGE) - our features move only Surge's parameters, smoothly: the click test.
For each patch, a held low note is rendered with every feature moved FAST (much faster than a hand):
  SCULPT     the three axes flicked between the extremes every 150 ms (and back to rest)
  SHAKE      full-strength shakes every 200 ms, then cleared
  TERRITORY  the pad thrown corner to corner every 150 ms
  TERRAIN    a different shape preset landed every 250 ms, then flat
  MORPH      the patch morphed with the next one, PENDULUM at 1/16 with the JUMP (square) shape, and
             the BLEND slider thrown 0 <-> 1 every 120 ms
A click is a burst of high frequencies (> 5 kHz, 2 ms windows) that is in the moving take but in neither
of the still takes at the two ends of the move (the sound at rest, and held at the far end of the move).
  CLICK = loudest high band of the moving take over the louder of the still takes, in dB (<= 6 dB passes;
          a high band under -76 dBFS counts as silence, so a burst has to reach -70 dBFS to fail)
(At rest the features change nothing at all: that is the null test against upstream Surge XT, null_test.py.)
  SURGE_DATA_HOME=<data> python3 -I feature_clicks.py <surgepy dir> <list.txt of patches> [out.txt]"""
import os, sys
import numpy as np
from scipy.signal import butter, sosfilt

sys.path.insert(0, sys.argv[1])
import surgepy  # noqa: E402

SR = 48000
LIST = sys.argv[2]
OUT = sys.argv[3] if len(sys.argv) > 3 else None
SECONDS, T0, T1 = 3.0, 0.6, 2.4  # moves happen between T0 and T1 into the note
LIMIT = 6.0
SHAPES = None


def render(path, setup=None, move=None, note=36, sculpt=True):
    s = surgepy.createSurge(SR)
    if not sculpt:
        s.ghostSculptEnable(False)
    s.loadPatch(path)
    bs = s.getBlockSize()
    if setup:
        setup(s)
    for _ in range(int(0.15 * SR) // bs):
        s.process()
    nb = int(SECONDS * SR) // bs
    out = s.createMultiBlock(nb)
    s.playNote(0, note, 100, 0)
    for b in range(nb):
        if move:
            move(s, b * bs / SR)
        s.processMultiBlock(out, b, 1)
    return out.T.astype(np.float64)


SOS = butter(4, 5000, "highpass", fs=SR, output="sos")


def hf(x):
    h = sosfilt(SOS, x.mean(1))
    w = int(0.002 * SR)
    e = np.sqrt(np.convolve(h ** 2, np.ones(w) / w, mode="same") + 1e-24)
    return e[int(T0 * SR) - w:int(T1 * SR) + int(0.15 * SR)]


def every(period, fn):
    state = {"k": -1}

    def mv(s, t):
        if T0 <= t < T1:
            k = int((t - T0) / period)
            if k != state["k"]:
                state["k"] = k
                fn(s, k)
    return mv


def features(path, nxt, tmp):
    """(name, moving take, [still takes at the ends of the move])"""
    r = []
    ax = [(1, 1, 1), (-1, -1, -1), (1, -1, 1), (-1, 1, -1)]
    r.append(("SCULPT", render(path, move=every(0.15, lambda s, k: s.ghostSetAxes(*ax[k % 4]) if k < 11 else s.ghostSetAxes(0, 0, 0))),
              [render(path)] + [render(path, setup=lambda s, a=a: s.ghostSetAxes(*a)) for a in ax]))
    r.append(("SHAKE", render(path, move=every(0.2, lambda s, k: s.ghostShake(1.0, 7 + k) if k < 8 else s.ghostClearSculpt())),
              [render(path)] + [render(path, setup=lambda s, k=k: s.ghostShake(1.0, 7 + k)) for k in range(8)]))
    corners = [(0.86, 0.82), (0.14, 0.18), (0.86, 0.18), (0.14, 0.82)]
    r.append(("TERRITORY", render(path, move=every(0.15, lambda s, k: s.ghostSetTerritory(*corners[k % 4]))),
              [render(path)] + [render(path, setup=lambda s, c=c: s.ghostSetTerritory(*c)) for c in corners]))
    lands = [s for s in SHAPES[:6]]
    probe = surgepy.createSurge(SR)
    texts = [probe.ghostShapeTerrain(n, 3) for n in lands]
    r.append(("TERRAIN", render(path, move=every(0.25, lambda s, k: s.ghostSetTerrain(texts[k % len(texts)] if k < 6 else ""))),
              [render(path)] + [render(path, setup=lambda s, t=t: s.ghostSetTerrain(t)) for t in texts]))
    if nxt:
        mk = surgepy.createSurge(SR)
        mode, why = mk.ghostMorphMake(path, nxt, "A", "B", tmp)
        if mode:
            def pend(s):
                s.ghostSetTransport(True, 0.0, 128.0)
                s.ghostMorphSet(0.0, True, 0, 0, 2, False, 1.0, 1.0, False, 1)
            r.append(("MORPH PENDULUM 1/16 JUMP", render(tmp, setup=pend),
                      [render(tmp, setup=lambda s: s.ghostMorphSet(0.0, False, 0, 0, 0, False, 1.0, 1.0, False, 1)),
                       render(tmp, setup=lambda s: s.ghostMorphSet(1.0, False, 0, 0, 0, False, 1.0, 1.0, False, 1))]))
            r.append(("MORPH BLEND 0<->1", render(tmp, move=every(0.12, lambda s, k: s.ghostMorphSet(float(k % 2), False, 0, 0, 0, False, 1.0, 1.0, False, 1))),
                      [render(tmp, setup=lambda s: s.ghostMorphSet(0.0, False, 0, 0, 0, False, 1.0, 1.0, False, 1)),
                       render(tmp, setup=lambda s: s.ghostMorphSet(1.0, False, 0, 0, 0, False, 1.0, 1.0, False, 1))]))
    return r


def main():
    global SHAPES
    SHAPES = surgepy.createSurge(SR).ghostShapeNames()
    paths = [l.rstrip("\n") for l in open(LIST) if l.strip()]
    tmp = os.path.join(os.path.dirname(os.path.abspath(OUT or LIST)), "fc_morph.fxp")
    lines, fails = [], 0
    for i, p in enumerate(paths):
        nxt = paths[(i + 1) % len(paths)] if len(paths) > 1 else None
        name = os.path.basename(p)[:-4]
        worst, parts = -99.0, []
        for fname, mv, still in features(p, nxt, tmp):
            ref = max(hf(x).max() for x in still)
            pk = hf(mv).max()
            # (the band above 5 kHz under -76 dBFS counts as silence: a burst must reach -70 dBFS to fail)
            c = -99.0 if pk < 10 ** (-90 / 20) else 20 * np.log10(pk / max(ref, 10 ** (-76 / 20)))
            parts.append("%s %+.1f (%.0f dBFS)" % (fname, c, 20 * np.log10(pk + 1e-12)))
            worst = max(worst, c)
        ok = worst <= LIMIT
        fails += not ok
        line = "%-34s worst %+5.1f dB  %s  [%s]" % (name[:34], worst, "PASS" if ok else "FAIL", ", ".join(parts))
        print(line, flush=True)
        lines.append(line)
    summ = "feature click test: %d patches, %d fail (limit %.0f dB)" % (len(paths), fails, LIMIT)
    print(summ)
    if OUT:
        with open(OUT, "w") as f:
            f.write("\n".join(lines + [summ]) + "\n")


if __name__ == "__main__":
    main()
