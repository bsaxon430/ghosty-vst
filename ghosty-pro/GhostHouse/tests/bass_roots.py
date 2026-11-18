# Measures the root note of each of the 17 Block Step basses (YIN, offline, at build time) and writes the table the engines
# and the page use: Shared/BassRoots.h (C++) + bass_roots.json. Usage: bass_roots.py <dir with bass-01.mp3 .. bass-17.mp3> <out dir>
# The root is the median YIN pitch over the body of the sound (after the attack, while it is loud and periodic), as a MIDI note
# with cents (a float). Each value is printed with its spread, so an odd one is easy to spot.
import sys, json, subprocess, pathlib
import numpy as np

SR = 44100
def decode(p):
    raw = subprocess.run(['ffmpeg', '-v', 'quiet', '-i', str(p), '-f', 'f32le', '-ac', '1', '-ar', str(SR), '-'], capture_output=True, check=True).stdout
    return np.frombuffer(raw, dtype=np.float32).astype(np.float64)

def yin(frame, fmin=25.0, fmax=700.0, thr=0.12):
    W = len(frame) // 2; tmax = int(SR / fmin); tmin = int(SR / fmax); tmax = min(tmax, W - 1)
    x = frame
    # difference function d(tau) = sum (x[j] - x[j+tau])^2, j < W (via cumulative sums + correlation)
    e = np.cumsum(np.concatenate([[0.0], x * x]))
    r = np.correlate(x[:2 * W], x[:W], mode='valid')[:tmax + 1]   # r[tau] = sum x[j] x[j+tau]
    d = np.array([e[W] - e[0] + e[t + W] - e[t] - 2 * r[t] for t in range(tmax + 1)])
    cm = np.ones_like(d); run = np.cumsum(d[1:]); cm[1:] = d[1:] * np.arange(1, tmax + 1) / np.maximum(run, 1e-12)
    t = tmin
    while t < tmax and cm[t] >= thr: t += 1
    if t >= tmax: t = tmin + int(np.argmin(cm[tmin:tmax]));
    while t + 1 < tmax and cm[t + 1] < cm[t]: t += 1
    ap = cm[t]
    if 1 <= t < tmax:   # parabolic interpolation on d
        a, b, c = d[t - 1], d[t], d[t + 1]; den = a - 2 * b + c; off = 0.5 * (a - c) / den if abs(den) > 1e-12 else 0.0
    else: off = 0.0
    return SR / (t + off), ap

def root(x):
    n = len(x); win = 4096; hop = 512
    env = np.array([np.sqrt(np.mean(x[i:i + win] ** 2)) for i in range(0, max(1, n - win), hop)]) if n > win else np.array([np.sqrt(np.mean(x ** 2))])
    pk = env.max() if len(env) else 0
    est = []
    start = int(0.03 * SR)   # after the attack
    for k, i in enumerate(range(0, max(1, n - win), hop)):
        if i < start or env[k] < pk * 0.25: continue
        f, ap = yin(x[i:i + win])
        if ap < 0.2: est.append(12 * np.log2(f / 440.0) + 69)
    if not est: return None, None, 0
    est = np.array(est); med = np.median(est)
    est = est[np.abs(est - med) < 0.75]   # drop octave slips
    return float(np.median(est)), float(np.std(est)), len(est)

if __name__ == '__main__':
    src = pathlib.Path(sys.argv[1]); out = pathlib.Path(sys.argv[2]); out.mkdir(parents=True, exist_ok=True)
    roots = []
    for k in range(1, 18):
        p = src / f'bass-{k:02d}.mp3'; x = decode(p); r, sd, n = root(x)
        names = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
        nm = f"{names[int(round(r)) % 12]}{int(round(r)) // 12 - 1}{(r - round(r)) * 100:+.0f}c" if r is not None else '?'
        print(f'bass {k:2d}: root {r if r is None else round(r, 2)} ({nm}, {440 * 2 ** ((r - 69) / 12) if r else 0:.1f} Hz), spread {sd if sd is None else round(sd, 3)} st over {n} frames, {len(x) / SR:.2f} s')
        roots.append(round(r, 3) if r is not None else 36.0)
    (out / 'bass_roots.json').write_text(json.dumps(roots))
    (out / 'BassRoots.h').write_text(
        '#pragma once\n// The root note (MIDI, with cents) of each of the 17 Block Step basses (kit bass-01 .. bass-17), measured offline\n'
        '// with YIN (/home/claude/shared/bass_roots.py: median pitch over the body of the sound). The sample bass of the ACID page\n'
        '// plays each note at 2^((note - root) / 12) times the sample\'s speed.\n'
        'namespace ghosty::bassroots {\nconstexpr int kCount = 17;\nconstexpr float kRoot[kCount] = { ' + ', '.join(f'{v:.3f}f' for v in roots) + ' };\n}\n')
    print(json.dumps(roots))
