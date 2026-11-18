#!/usr/bin/env python3
"""Looks for things that are in one painted frame only (the network inventing an object once): for every frame, where it
differs from BOTH its neighbours while the neighbours agree with each other, inside the painted part.
   blips.py <folder of painted frames> <world> ...      prints the frames that stand out most"""
import os, sys, cv2, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from film_masks import frames_of, mask_of
from flicker import shift_between, rings

def onto(a, b, ring):
    dx, dy = shift_between(a, b, ring)
    M = np.float32([[1, 0, dx], [0, 1, dy]])
    return cv2.warpAffine(a, M, (a.shape[1], a.shape[0]), flags=cv2.INTER_LINEAR, borderMode=cv2.BORDER_REPLICATE).astype(np.float32)

def scan(folder, w):
    names = frames_of(w)
    f = [cv2.imread(f'{folder}/{w}/{n}') for n in names]
    out = []
    for i in range(1, len(f) - 1):
        m = mask_of(w, i); ring, inner = rings(m)
        cur = f[i].astype(np.float32)
        before, after = onto(f[i - 1], f[i], ring), onto(f[i + 1], f[i], ring)
        d1 = np.abs(cur - before).mean(axis=2); d2 = np.abs(cur - after).mean(axis=2); d3 = np.abs(before - after).mean(axis=2)
        s = cv2.blur(np.clip(np.minimum(d1, d2) - d3, 0, None), (21, 21))
        s[inner == 0] = 0
        y, x = np.unravel_index(int(s.argmax()), s.shape)
        out.append((float(s.max()), i, int(x), int(y)))
    return out

if __name__ == '__main__':
    for w in sys.argv[2:]:
        r = scan(sys.argv[1], w)
        vals = np.array([v[0] for v in r]); med = float(np.median(vals))
        top = sorted(r, reverse=True)[:4]
        print(f'{w:8s} usual {med:5.1f}; stand out: ' + ', '.join(f'frame {i:02d} {v:5.1f} at ({x},{y})' for v, i, x, y in top), flush=True)
