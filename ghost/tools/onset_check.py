#!/usr/bin/env python3
"""Ghost Bass - no pop at note start (build 7).
For each preset: load it, warm the engine up, render 0.25 s of silence (nothing may sound before a note:
a Stretch layer, a Flues voice or a MORPH scene must stay quiet), then play one note (A1, C2, A2) and compare
its first 10 ms with the same audio given a 5 ms fade-in. A sound that starts smoothly barely changes; a
sound that jumps in at full level (a pop) loses a lot of high end under the fade.
  POP   = high band (> 1 kHz) peak of the first 10 ms, as played vs with the 5 ms ramp, in dB (<= 6 dB passes)
  STEP  = the largest single-sample jump in the first 2 ms, against the largest in the next 100 ms (dB)
  PRE   = the loudest sample before any note (dBFS; <= -80 passes)
  SURGE_DATA_HOME=<data> python3 -I onset_check.py <surgepy dir> <presets dir> [regex]"""
import os, re, sys
import numpy as np
from scipy.signal import butter, sosfilt

sys.path.insert(0, sys.argv[1])
import surgepy  # noqa: E402

SR = 48000
ROOT = sys.argv[2]
RX = re.compile(sys.argv[3] if len(sys.argv) > 3 else ".")
NOTES = (33, 36, 45)
LIMIT_POP, LIMIT_PRE = 6.0, -80.0


def ramp(n):
    r = np.ones(n)
    k = int(0.005 * SR)
    r[:k] = 0.5 - 0.5 * np.cos(np.pi * np.arange(k) / k)
    return r


def onset(path, note):
    s = surgepy.createSurge(SR)
    s.loadPatch(path)
    bs = s.getBlockSize()
    for _ in range(int(0.1 * SR) // bs):  # warm up (preset load, smoothing)
        s.process()
    pre_n, post_n = int(0.25 * SR) // bs, int(0.4 * SR) // bs
    out = s.createMultiBlock(pre_n + post_n)
    for b in range(pre_n + post_n):
        if b == pre_n:
            s.playNote(0, note, 100, 0)
        s.processMultiBlock(out, b, 1)
    x = out.T.astype(np.float64)
    t0 = pre_n * bs
    pre = 20 * np.log10(np.abs(x[:t0]).max() + 1e-12)
    m = x[t0:].mean(1)
    if np.abs(m).max() < 1e-4:
        return pre, -99.0, -99.0
    sos = butter(4, 1000, "highpass", fs=SR, output="sos")
    w = int(0.010 * SR)
    a = np.abs(sosfilt(sos, m))[:w].max()
    b = np.abs(sosfilt(sos, m * ramp(len(m))))[:w].max()
    pop = 20 * np.log10((a + 1e-9) / (b + 1e-9))
    d = np.abs(np.diff(np.concatenate([[0.0], m])))
    step = 20 * np.log10((d[:int(0.002 * SR)].max() + 1e-12) / (d[int(0.002 * SR):int(0.1 * SR)].max() + 1e-9))
    return pre, pop, step


def main():
    bad = 0
    n = 0
    for cat in sorted(os.listdir(ROOT)):
        d = os.path.join(ROOT, cat)
        if not os.path.isdir(d):
            continue
        for f in sorted(os.listdir(d)):
            p = os.path.join(d, f)
            if not f.endswith(".fxp") or not RX.search(p):
                continue
            rs = [onset(p, k) for k in NOTES]
            pre = max(r[0] for r in rs)
            pop = max(r[1] for r in rs)
            step = max(r[2] for r in rs)
            why = []
            if pop > LIMIT_POP:
                why.append("POP")
            if pre > LIMIT_PRE:
                why.append("PRE")
            n += 1
            bad += bool(why)
            print("%-18s %-24s POP %5.1f dB  STEP %5.1f dB  PRE %6.1f dBFS  %s" % (
                cat, f[:-4], pop, step, pre, ("FAIL " + ",".join(why)) if why else "PASS"), flush=True)
    print("onset: %d presets, %d fail" % (n, bad))


if __name__ == "__main__":
    main()
