#!/usr/bin/env python3
"""Where the boat is in each film frame, as a shift from where it is in frame 00 (the painting's frame), measured two ways:
  track.json           the median motion, frame to frame, of corners on its hull (the first shape of the painting's mask), added up
  track_template.json  the hull's picture from frame 00, looked for in each frame (the best match within 90 pixels)
Neither is right everywhere (a dark hull on dark water has few corners; the hull's picture changes as the boat turns),
so film_masks.py covers both places."""
import os, sys, json, cv2, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import where
from masks import SHAPES, mask_for
HERO = {'amber': 's01a', 'crimson': 's02a', 'blossom': 's03b', 'lantern': 's04b', 'opera': 's05a', 'glow': 's06c', 'snow': 's07b', 'aurora': 's08a'}
def frames_of(w): return sorted(f for f in os.listdir(f'{where.film()}/src/{w}') if f.endswith('.png'))

def track(w):
    names = frames_of(w)
    hull = mask_for(HERO[w], 0, [SHAPES[HERO[w]][0]])
    hull = cv2.erode(hull, np.ones((5, 5), np.uint8))
    out = [(0.0, 0.0)]
    SRC = where.film() + '/src'
    prev = cv2.cvtColor(cv2.imread(f'{SRC}/{w}/{names[0]}'), cv2.COLOR_BGR2GRAY)
    x = y = 0.0
    for n in names[1:]:
        cur = cv2.cvtColor(cv2.imread(f'{SRC}/{w}/{n}'), cv2.COLOR_BGR2GRAY)
        M = np.float32([[1, 0, x], [0, 1, y]])
        spot = cv2.warpAffine(hull, M, (hull.shape[1], hull.shape[0]), flags=cv2.INTER_NEAREST)      # the hull where it was in the frame before
        pts = cv2.goodFeaturesToTrack(prev, 300, 0.005, 4, mask=spot)
        dx = dy = 0.0
        if pts is not None and len(pts) >= 6:
            nxt, st, _ = cv2.calcOpticalFlowPyrLK(prev, cur, pts, None, winSize=(21, 21), maxLevel=3)
            back, st2, _ = cv2.calcOpticalFlowPyrLK(cur, prev, nxt, None, winSize=(21, 21), maxLevel=3)
            ok = (st.ravel() == 1) & (st2.ravel() == 1) & (np.abs(back - pts).reshape(-1, 2).max(axis=1) < 1.0)
            if ok.sum() >= 6:
                d = (nxt - pts).reshape(-1, 2)[ok]
                dx, dy = float(np.median(d[:, 0])), float(np.median(d[:, 1]))
        x += dx; y += dy
        out.append((round(x, 2), round(y, 2)))
        prev = cur
    return out

def match(w):
    """(dx, dy, score) for each frame: where the hull's picture from frame 00 fits best."""
    SRC = where.film() + '/src'
    names = frames_of(w)
    hull = mask_for(HERO[w], 0, [SHAPES[HERO[w]][0]])
    ys, xs = np.where(hull > 0)
    x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
    f0 = cv2.imread(f'{SRC}/{w}/{names[0]}')
    tmpl, tm, R, res = f0[y0:y1, x0:x1], hull[y0:y1, x0:x1], 90, []
    for n in names:
        f = cv2.imread(f'{SRC}/{w}/{n}')
        sx0, sy0 = max(0, x0 - R), max(0, y0 - R); sx1, sy1 = min(f.shape[1], x1 + R), min(f.shape[0], y1 + R)
        r = cv2.matchTemplate(f[sy0:sy1, sx0:sx1], tmpl, cv2.TM_CCORR_NORMED, mask=cv2.merge([tm, tm, tm]))
        r[~np.isfinite(r)] = -1
        _, score, _, loc = cv2.minMaxLoc(r)
        res.append((float(loc[0] + sx0 - x0), float(loc[1] + sy0 - y0), round(float(score), 3)))
    return res


if __name__ == '__main__':
    json.dump({w: match(w) for w in HERO}, open(where.work() + '/track_template.json', 'w'))
    res = {w: track(w) for w in HERO}
    json.dump(res, open(where.work() + '/track.json', 'w'))
    for w, t in res.items():
        a = np.array(t); print(f'{w:8s} the boat ends {a[-1][0]:+.1f}, {a[-1][1]:+.1f} px from where it began; furthest {np.abs(a[:, 0]).max():.1f}, {np.abs(a[:, 1]).max():.1f}')
