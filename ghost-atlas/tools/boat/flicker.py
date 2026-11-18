#!/usr/bin/env python3
"""How much the painted-in part of a world's film changes from frame to frame, against how much the picture round it does.
The background slides under the hole (the camera follows the boat), so each pair of frames is first lined up by the motion
of the ring of real picture round the hole.   flicker.py <folder of frames> <world> ..."""
import os, sys, cv2, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from film_masks import frames_of, mask_of

def shift_between(a, b, ring):
    """(dx, dy) that moves frame a's surroundings onto frame b's: the median motion of corners in the ring."""
    ga, gb = cv2.cvtColor(a, cv2.COLOR_BGR2GRAY), cv2.cvtColor(b, cv2.COLOR_BGR2GRAY)
    pts = cv2.goodFeaturesToTrack(ga, 400, 0.005, 6, mask=ring)
    if pts is None or len(pts) < 8: return 0.0, 0.0
    nxt, st, _ = cv2.calcOpticalFlowPyrLK(ga, gb, pts, None, winSize=(25, 25), maxLevel=3)
    ok = st.ravel() == 1
    if ok.sum() < 8: return 0.0, 0.0
    d = (nxt - pts).reshape(-1, 2)[ok]
    return float(np.median(d[:, 0])), float(np.median(d[:, 1]))

def rings(mask):
    k = lambda r: cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (2 * r + 1, 2 * r + 1))
    near = cv2.dilate(mask, k(60)); ring = cv2.subtract(near, cv2.dilate(mask, k(4)))
    inner = cv2.erode(mask, k(6))
    return ring, inner

def measure(folder, w):
    names = frames_of(w)
    frames = [cv2.imread(f'{folder}/{w}/{n}') for n in names]
    hole_d, ring_d, shifts = [], [], []
    for i in range(len(frames) - 1):
        m = mask_of(w, i); ring, inner = rings(m)
        dx, dy = shift_between(frames[i], frames[i + 1], ring)
        shifts.append((dx, dy))
        M = np.float32([[1, 0, dx], [0, 1, dy]])
        moved = cv2.warpAffine(frames[i], M, (frames[i].shape[1], frames[i].shape[0]), flags=cv2.INTER_LINEAR, borderMode=cv2.BORDER_REPLICATE)
        d = np.abs(moved.astype(np.float32) - frames[i + 1].astype(np.float32)).mean(axis=2)
        hole_d.append(float(d[inner > 0].mean())); ring_d.append(float(d[ring > 0].mean()))
    return np.array(hole_d), np.array(ring_d), shifts

if __name__ == '__main__':
    folder = sys.argv[1]
    for w in sys.argv[2:]:
        h, r, s = measure(folder, w)
        sx = np.array(s)
        print(f'{w:8s} frame to frame: the ring changes by {r.mean():.2f} (worst {r.max():.2f}), the painted-in part by {h.mean():.2f} (worst {h.max():.2f}); '
              f'ratio {h.mean() / max(1e-6, r.mean()):.2f}; the background moves {np.abs(sx[:, 0]).mean():.1f}, {np.abs(sx[:, 1]).mean():.1f} px a frame', flush=True)
