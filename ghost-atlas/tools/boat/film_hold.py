#!/usr/bin/env python3
"""Keeps the part of a film's painted-in background that does not move (a bank, a wall, trees) the same from frame to
frame. Painted one frame at a time, the network invents that part anew in every frame, and it seethes when the film
plays (and once in a while it invents an object for one frame). Here that part is taken from the film's first painted
frame instead - the painting's own frame - moved as the background beside it has moved since, so what was
invented once stays as it was. Where the boat has moved on to cover new ground, that sliver is still painted frame by frame.
Water is left as it was painted frame by frame: it moves, and held it would freeze.
   film_hold.py [world ...]      the worlds that have an entry in HOLD; the frames as painted are kept in film_lama/"""
import os, sys, shutil, cv2, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import where
from film_masks import frames_of, mask_of

SRC, OUT, KEEP = where.film() + '/src', where.work() + '/film', where.work() + '/film_lama'
# world: in frame 00, the still part behind the boat (a polygon; only what lies inside a frame's mask is replaced)
HOLD = {'snow': [(620, 338), (910, 338), (910, 445), (620, 445)]}

# The background is not one flat picture: the camera turns and climbs while it follows the boat, so the far trees slide
# one way and the bank in front of them hardly at all. What is behind the boat at some height is taken to move as the
# real picture beside the hole does at that height: the median motion of corners there, in bands of rows (BANDS: each a
# first and last row in frame 00), joined smoothly from band to band.
BANDS = {'snow': [(150, 250), (250, 330), (330, 400)]}

def moved(g0, gi, pts, hole, bands):
    """[(row, dx, dy), ...]: how far the still background has moved between two frames, at the height of each band."""
    nxt, st, _ = cv2.calcOpticalFlowPyrLK(g0, gi, pts, None, winSize=(31, 31), maxLevel=4)
    back, st2, _ = cv2.calcOpticalFlowPyrLK(gi, g0, nxt, None, winSize=(31, 31), maxLevel=4)
    ok = (st.ravel() == 1) & (st2.ravel() == 1) & (np.abs(back - pts).reshape(-1, 2).max(axis=1) < 1.0)
    p, q = pts.reshape(-1, 2), nxt.reshape(-1, 2)
    inside = np.array([hole[min(hole.shape[0] - 1, max(0, int(round(y)))), min(hole.shape[1] - 1, max(0, int(round(x))))] > 0 for x, y in q])
    ok &= ~inside                                # (corners that land where the boat is in the later frame are left out)
    out = []
    for a, b in bands:
        sel = ok & (p[:, 1] >= a) & (p[:, 1] < b)
        if sel.sum() < 8: return None
        d = (q - p)[sel]
        out.append((0.5 * (a + b), float(np.median(d[:, 0])), float(np.median(d[:, 1]))))
    return out

def carried(P0, field):
    """Frame 00's painted picture with every row moved as its height moves (field: [(row, dx, dy), ...])."""
    H, W = P0.shape[:2]
    rows = np.arange(H, dtype=np.float32)
    ys = np.array([r for r, _, _ in field], np.float32)
    dx = np.interp(rows, ys, [v for _, v, _ in field]).astype(np.float32)
    dy = np.interp(rows, ys, [v for _, _, v in field]).astype(np.float32)
    to = rows + dy                               # where each row of frame 00 lands; np.interp needs it rising, and it is (the picture is never folded)
    to = np.maximum.accumulate(to + np.arange(H, dtype=np.float32) * 1e-4)
    src_row = np.interp(rows, to, rows).astype(np.float32)          # for each row of the new frame: the row of frame 00 that lands there
    src_dx = np.interp(src_row, rows, dx).astype(np.float32)
    mapx = (np.arange(W, dtype=np.float32)[None, :] - src_dx[:, None]).astype(np.float32)
    mapy = np.repeat(src_row[:, None], W, axis=1).astype(np.float32)
    return cv2.remap(P0, mapx, mapy, cv2.INTER_CUBIC, borderMode=cv2.BORDER_REPLICATE), mapx, mapy

for w in (sys.argv[1:] or HOLD):
    names = frames_of(w)
    os.makedirs(f'{KEEP}/{w}', exist_ok=True)
    for n in names:
        if not os.path.exists(f'{KEEP}/{w}/{n}'): shutil.copy(f'{OUT}/{w}/{n}', f'{KEEP}/{w}/{n}')      # (the frames as painted, once)
    P0 = cv2.imread(f'{KEEP}/{w}/{names[0]}')
    H, W = P0.shape[:2]
    k = lambda r: cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (2 * r + 1, 2 * r + 1))
    # what is carried is what was painted in frame 00 and nothing else: the real picture round it holds nearer things
    # too (here a fallen tree, which crosses the frame far faster than the bank behind it), and they must not be laid
    # down a second time as background
    region0 = np.zeros((H, W), np.uint8); cv2.fillPoly(region0, [np.array(HOLD[w], np.int32)], 255)
    region0 = cv2.bitwise_and(region0, cv2.dilate(mask_of(w, 0), k(2)))
    # corners are looked for on real, still picture beside and above the held part: not in frame 00's hole, not on the water
    hole0 = cv2.dilate(mask_of(w, 0), k(6))
    x0, x1 = min(x for x, _ in HOLD[w]) - 140, max(x for x, _ in HOLD[w])
    band = np.zeros((H, W), np.uint8); band[BANDS[w][0][0]:BANDS[w][-1][1], max(0, x0):x1] = 255
    band[hole0 > 0] = 0
    g0 = cv2.cvtColor(cv2.imread(f'{SRC}/{w}/{names[0]}'), cv2.COLOR_BGR2GRAY)
    pts = cv2.goodFeaturesToTrack(g0, 700, 0.004, 5, mask=band)
    # every frame's motion first, then each band's path through the film is evened out (a corner here and there is a
    # snowflake, and a held picture that trembles by a pixel is seen): a running mean over five frames
    fields = [[(0.5 * (a + b), 0.0, 0.0) for a, b in BANDS[w]]]
    for i, n in enumerate(names):
        if i == 0: continue
        gi = cv2.cvtColor(cv2.imread(f'{SRC}/{w}/{n}'), cv2.COLOR_BGR2GRAY)
        fields.append(moved(g0, gi, pts, cv2.dilate(mask_of(w, i), k(4)), BANDS[w]) or fields[-1])
    raw = np.array(fields, np.float32)                 # frames x bands x (row, dx, dy)
    even = raw.copy()
    for i in range(1, len(names)):
        lo, hi = max(0, i - 2), min(len(names), i + 3)
        even[i, :, 1:] = raw[lo:hi, :, 1:].mean(axis=0)
    for i, n in enumerate(names):
        if i == 0: continue
        m = mask_of(w, i)
        field = [tuple(float(v) for v in row) for row in even[i]]
        ref, mapx, mapy = carried(P0, field)
        # soft where the held part ends inside the hole (at the water), and over two pixels at the hole's own edge, where
        # the frame's real picture and the held one show the same still background
        poly = cv2.remap(region0, mapx, mapy, cv2.INTER_NEAREST, borderMode=cv2.BORDER_CONSTANT, borderValue=0)
        a = np.minimum(np.clip(cv2.distanceTransform(poly, cv2.DIST_L2, 5) / 6.0, 0, 1), np.clip(cv2.distanceTransform(m, cv2.DIST_L2, 5) / 2.0, 0, 1))[..., None].astype(np.float32)
        cur = cv2.imread(f'{KEEP}/{w}/{n}').astype(np.float32)
        cv2.imwrite(f'{OUT}/{w}/{n}', np.clip(cur * (1 - a) + ref.astype(np.float32) * a + 0.5, 0, 255).astype(np.uint8))
        if i % 5 == 0 or i == len(names) - 1: print(f'{w} {n[:2]}: since frame 00 the background has moved ' + '; '.join(f'{dx:+.1f}, {dy:+.1f} at row {r:.0f}' for r, dx, dy in field), flush=True)
