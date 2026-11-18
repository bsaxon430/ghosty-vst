#!/usr/bin/env python3
"""Ghost Bass build 8 (PURE SURGE) demos: Surge XT's own bass patches played straight (Surge's sound, exactly),
then the same patches moved by our features (which only move Surge's own parameters).
Pop-free rendering: every take gets its own engine, warmed up for 0.5 s with no notes, then 0.3 s of
silence before the first note; takes are joined with silence; the file gets a 5 ms fade-in on its first
samples only, and one fixed gain for the whole file when Surge's level would clip it (nothing else is
faded or processed). 48 kHz float WAV, then MP3 (ffmpeg).
  SURGE_DATA_HOME=<data> python3 -I b8_demo.py <surgepy dir> <kind> <out.wav> <patch.fxp> [<patch.fxp> ...]
kinds:
  straight   each patch plays the bassline as Surge plays it
  sculpt     each patch: straight, then the same bassline while SCULPT is pulled, twisted and stretched by hand
  terrain    each patch: straight, then with a TERRAIN shape (SPIRE FOREST, CANYON ...) on it
  terrain-drag   (build 9) each patch: straight, then a mountain dragged across the land (and a valley the other way) while it plays,
                 MOVEMENT 60% wandering the controls under it
  movement       (build 9) each patch: the same land with MOVEMENT 0, 50%, 100%
  sculpt-glitch  (build 9) each patch: straight, then a big hand on the SCULPT shape with SHAKE bursts (glitches)
  pend4      MORPH of the first two patches, PENDULUM at 1/4, smooth swing
  pend1bar   MORPH of the first two patches, PENDULUM at 1 bar, smooth swing
  draw       MORPH of the first two patches, a drawn curve over 2 bars (Surge's MSEG engine plays it)"""
import os, sys
import numpy as np

sys.path.insert(0, sys.argv[1])
import surgepy  # noqa: E402

SR = 48000
KIND, OUT, PATCHES = sys.argv[2], sys.argv[3], sys.argv[4:]
BPM = 124.0
# a two-bar bassline, played twice (note, start beat, length beats, velocity)
LINE = [(33, 0, 0.75, 110), (33, 1, 0.5, 90), (45, 1.5, 0.25, 80), (36, 2, 0.75, 105), (31, 3, 0.75, 100),
        (33, 4, 0.75, 110), (40, 5, 0.5, 95), (38, 5.5, 0.5, 90), (36, 6, 1.5, 105)]
LINE = LINE + [(n, s + 8, l, v) for n, s, l, v in LINE]
HELD = [(33, 0, 7.8, 105), (36, 8, 7.8, 105), (31, 16, 7.8, 105), (28, 24, 7.8, 105)]
PRE, TAIL, GAP = 0.3, 1.2, 0.5


def take(path, events, setup=None, move=None):
    s = surgepy.createSurge(SR)
    s.loadPatch(path)
    s.ghostSetTransport(False, 0.0, BPM)
    if setup:
        setup(s)
    bs = s.getBlockSize()
    for _ in range(int(0.5 * SR) // bs):  # warm-up: the patch load settles, nothing is recorded
        s.process()
    spb = 60.0 / BPM * SR
    pre = int(PRE * SR)
    total = pre + int(max(st + ln for _, st, ln, _ in events) * spb + TAIL * SR)
    nb = total // bs
    out = s.createMultiBlock(nb)
    ev = sorted([(pre + int(st * spb), 1, n, v) for n, st, ln, v in events] + [(pre + int((st + ln) * spb), 0, n, 0) for n, st, ln, v in events])
    k = 0
    for b in range(nb):
        t = b * bs
        if move:
            move(s, max(0.0, (t - pre) / SR))
        while k < len(ev) and ev[k][0] <= t:
            _, on, n, v = ev[k]
            s.playNote(0, n, v, 0) if on else s.releaseNote(0, n, 0)
            k += 1
        s.processMultiBlock(out, b, 1)
    return out.T.copy()


def hand_sculpt(s, t):
    # a hand on the shape: pull up over two bars, twist back and forth, stretch at the end
    s.ghostSetAxes(float(np.clip(np.sin(t * 0.8), -1, 1)), float(0.7 * np.sin(t * 1.9)), float(np.clip(t / 8.0 - 0.3, -1, 1)))


def drag_land(s, t):
    # a hand dragging a mountain from the sub up to the air while a valley goes the other way (the page hands the land over ~25x a second)
    f = min(1.0, t / 14.0)
    hz = 90.0 * (7000.0 / 90.0) ** f
    hz2 = 4000.0 * (110.0 / 4000.0) ** f
    h = 0.45 + 0.45 * np.sin(t * 1.3)
    peaks = [(hz, float(h), 3.0, 0.3, 0.35, 0.1), (hz2, -0.55, 2.5, 0.5, 0.3, 0.0), (900.0, 0.5 * np.sin(t * 0.7), 4.0, 0.7, 0.25, 0.2)]
    s.ghostSetTerrain(s.ghostMakeTerrain(peaks, 0.1, 0.0, False, 0.0, 7))


class Glitcher:
    def __init__(self):
        self.shook = set()

    def __call__(self, s, t):
        s.ghostSetAxes(float(np.clip(1.1 * np.sin(t * 0.9), -1, 1)), float(np.sin(t * 2.3)), float(np.clip(1.2 * np.sin(t * 0.5 + 1.0), -1, 1)))
        for k, tt in enumerate((1.5, 5.0, 9.0, 12.0)):
            if t >= tt and k not in self.shook:
                self.shook.add(k)
                s.ghostShake(1.0, 100 + k)


def main():
    name = lambda p: os.path.basename(p)[:-4]
    parts = []
    gap = np.zeros((int(GAP * SR), 2), np.float32)

    def add(x, label):
        pk = 20 * np.log10(np.max(np.abs(x)) + 1e-12)
        print("%-46s peak %6.1f dBFS  %5.1f s" % (label, pk, len(x) / SR), flush=True)
        parts.extend([x, gap])

    if KIND in ("straight", "sculpt", "terrain", "terrain-drag", "movement", "sculpt-glitch"):
        for p in PATCHES:
            add(take(p, LINE), name(p) + " (Surge, straight)")
            if KIND == "sculpt":
                add(take(p, LINE, move=hand_sculpt), name(p) + " + SCULPT")
            elif KIND == "terrain-drag":
                def setup(s):
                    s.ghostSetMovement(0.6, 2, 0.5)

                def mv(s, t):
                    if int(t * 25) != getattr(mv, "last", -1):
                        mv.last = int(t * 25)
                        drag_land(s, t)

                add(take(p, LINE, setup=setup, move=mv), name(p) + " + TERRAIN dragged")
            elif KIND == "movement":
                land = surgepy.createSurge(SR).ghostShapeTerrain("ARCHIPELAGO", 3)
                for m in (0.0, 0.5, 1.0):
                    add(take(p, LINE, setup=lambda s, m=m: (s.ghostSetTerrain(land), s.ghostSetMovement(m, 2, 0.5))),
                        name(p) + " + TERRAIN ARCHIPELAGO, MOVEMENT %d%%" % int(m * 100))
            elif KIND == "sculpt-glitch":
                add(take(p, LINE, move=Glitcher()), name(p) + " + SCULPT + SHAKE glitches")
            elif KIND == "terrain":
                shape = surgepy.createSurge(SR).ghostShapeTerrain("CANYON", 3)
                add(take(p, LINE, setup=lambda s: s.ghostSetTerrain(shape)), name(p) + " + TERRAIN (CANYON)")
    else:
        a, b = PATCHES[0], PATCHES[1]
        tmp = os.path.join(os.path.dirname(os.path.abspath(OUT)), "b8_morph.fxp")
        mode, why = surgepy.createSurge(SR).ghostMorphMake(a, b, name(a), name(b), tmp)
        if not mode:
            sys.exit("no morph: " + why)
        print("MORPH %s > %s: %s" % (name(a), name(b), {1: "true morph", 2: "scene blend"}.get(mode, "?")))
        if KIND == "draw":
            def setup(s):
                s.ghostMorphDraw([(0.0, 0.0), (0.2, 0.15), (0.4, 0.9), (0.5, 1.0), (0.7, 0.3), (0.85, 0.65), (1.0, 0.0)])
                s.ghostMorphSet(0.0, False, 0, 0, 0, False, 1.0, 1.0, True, 2)
        else:
            rate = 2 if KIND == "pend4" else 4  # 1/4, 1 bar
            setup = lambda s: s.ghostMorphSet(0.0, True, rate, 0, 0, False, 1.0, 1.0, False, 1)
        add(take(a, HELD), name(a) + " (A, Surge straight)")
        add(take(b, HELD), name(b) + " (B, Surge straight)")
        add(take(tmp, HELD, setup=setup), "MORPH " + KIND)
    y = np.concatenate(parts[:-1]).astype(np.float64)
    k = int(0.005 * SR)  # the file's first 5 ms only
    y[:k] *= (0.5 - 0.5 * np.cos(np.pi * np.arange(k) / k))[:, None]
    # Surge plays some patches above 0 dBFS: the whole file (every take alike) is turned down by one fixed
    # amount so the file never clips - no per-take gain, no limiter
    pk = np.max(np.abs(y))
    g = min(1.0, 10 ** (-1.0 / 20) / max(pk, 1e-9))
    y *= g
    print("file gain %.1f dB (one fixed gain for the whole file)" % (20 * np.log10(g)))
    from scipy.io import wavfile
    wavfile.write(OUT, SR, y.astype(np.float32))
    os.system('ffmpeg -loglevel error -y -i "%s" -b:a 256k "%s"' % (OUT, OUT[:-4] + ".mp3"))
    print("wrote", OUT[:-4] + ".mp3", "%.1f s" % (len(y) / SR))


if __name__ == "__main__":
    main()
