#!/usr/bin/env python3
"""Ghost Bass build 8 (PURE SURGE) - null test, one side.
Renders every patch in a list with the given surgepy build (Ghost Bass's, or upstream Surge XT's at the same
commit) and prints one line per patch: a SHA-256 of the exact float32 output, its peak and length. Run it once
with each build and compare the two files (null_compare.py): Ghost Bass with its features at rest must give
bit-identical audio for every patch.
The take, the same for every patch: 0.1 s warm-up, then a low note, a chord, a high note, mod wheel moves and a
pitch bend, held and released - 3.0 s in all, 48 kHz, blocks of 32.
  SURGE_DATA_HOME=<data> [NULL_FRESH=1] python3 -I null_test.py <surgepy dir> <list.txt (patch paths)> <out.tsv> [part k/n]
(NULL_FRESH=1: each patch rendered in a fresh process)"""
import hashlib, os, sys
import numpy as np

sys.path.insert(0, sys.argv[1])
import surgepy  # noqa: E402

SR = 48000
LIST, OUT = sys.argv[2], sys.argv[3]
PART = sys.argv[4] if len(sys.argv) > 4 else "0/1"
K, N = (int(v) for v in PART.split("/"))
# (time s, kind, a, b): note on/off, controller, pitch bend
EV = [(0.00, "on", 36, 110), (0.60, "off", 36, 0),
      (0.70, "on", 48, 90), (0.70, "on", 55, 90), (0.70, "on", 60, 90),
      (1.00, "cc", 1, 100), (1.30, "cc", 1, 0), (1.50, "bend", 12000, 0),
      (1.70, "off", 48, 0), (1.70, "off", 55, 0), (1.70, "off", 60, 0), (1.70, "bend", 8192, 0),
      (1.80, "on", 72, 70), (2.40, "off", 72, 0)]
TOTAL = 3.0


def take(path):
    s = surgepy.createSurge(SR)
    s.loadPatch(path)
    bs = s.getBlockSize()
    for _ in range(int(0.1 * SR) // bs):
        s.process()
    nb = int(TOTAL * SR) // bs
    out = s.createMultiBlock(nb)
    ev = sorted(EV, key=lambda e: e[0])
    k = 0
    for b in range(nb):
        t = b * bs / SR
        while k < len(ev) and ev[k][0] <= t:
            _, kind, a, v = ev[k]
            if kind == "on":
                s.playNote(0, a, v, 0)
            elif kind == "off":
                s.releaseNote(0, a, 0)
            elif kind == "cc":
                s.channelController(0, a, v)
            else:
                s.pitchBend(0, a)
            k += 1
        s.processMultiBlock(out, b, 1)
    return np.ascontiguousarray(out, dtype=np.float32)


def main():
    paths = [l.rstrip("\n") for l in open(LIST) if l.strip()]
    if os.environ.get("NULL_FRESH") == "1" and len(paths) > 1:
        # every patch in a process of its own: what one patch draws from Surge's random generators can
        # then never shift what the next one gets (both sides are run the same way)
        import subprocess, tempfile
        env = dict(os.environ, NULL_FRESH="0")
        for i, p in enumerate(paths):
            if i % N != K:
                continue
            with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as t:
                t.write(p + "\n")
            subprocess.run([sys.executable, "-I", os.path.abspath(__file__), sys.argv[1], t.name, OUT], env=env,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            os.unlink(t.name)
        return
    with open(OUT, "a") as f:
        for i, p in enumerate(paths):
            if i % N != K:
                continue
            try:
                x = take(p)
                h = hashlib.sha256(x.tobytes()).hexdigest()[:24]
                f.write("%s\t%s\t%.6f\t%d\n" % (p, h, float(np.abs(x).max()), x.shape[1]))
            except Exception as e:  # noqa: BLE001
                f.write("%s\tERROR\t%s\t0\n" % (p, str(e).replace("\t", " ")[:80]))
            f.flush()


if __name__ == "__main__":
    main()
