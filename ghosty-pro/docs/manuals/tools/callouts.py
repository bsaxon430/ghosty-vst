#!/usr/bin/env python3
"""Numbered callouts on screenshots with ImageMagick (no Python deps).

Usage: python3 callouts.py spec.json [only-out-name ...]
spec.json = list of jobs:
 {"src": "shot.png", "out": "fig.png", "scale": 2,          # src pixels per design unit
  "crop": [x, y, w, h],                                      # design units (optional)
  "pad": [top, right, bottom, left],                         # design units of margin for the circles
  "items": [[n, tx, ty, "t|b|l|r"], ...],                    # target in design units (absolute, before crop)
  "resize": 1600}                                            # optional final width in px
Circles sit in the margin on the given side, spread apart so they never overlap,
with a leader line to a dot on the target. The margin is white (the page colour).
"""
import json, subprocess, sys, os

R, FS, GAP = 17, 19, 40          # circle radius, font size, min circle spacing (design units)
COL, OUT_COL, BG = '#e8186d', '#ffffff', '#ffffff'

def spread(vals, lo, hi, gap):
    """keep order, push apart to >= gap, stay inside [lo, hi]"""
    idx = sorted(range(len(vals)), key=lambda i: vals[i]); v = [vals[i] for i in idx]
    for k in range(1, len(v)): v[k] = max(v[k], v[k-1] + gap)
    if v and v[-1] > hi:
        v[-1] = hi
        for k in range(len(v) - 2, -1, -1): v[k] = min(v[k], v[k+1] - gap)
    if v and v[0] < lo:
        v[0] = lo
        for k in range(1, len(v)): v[k] = max(v[k], v[k-1] + gap)
    out = [0] * len(vals)
    for k, i in enumerate(idx): out[i] = v[k]
    return out

def job(j, base):
    s = j.get('scale', 2); src = os.path.join(base, j['src'])
    cx, cy, cw, ch = j.get('crop') or [0, 0, None, None]
    if cw is None:
        w, h = map(int, subprocess.check_output(['identify', '-format', '%w %h', src]).split()); cw, ch = w / s, h / s
    pt, pr, pb, pl = j.get('pad', [0, 0, 0, 0])
    W, H = cw + pl + pr, ch + pt + pb
    items = j.get('items', [])
    pos = {}
    for side in 'tblr':
        its = [it for it in items if it[3] == side]
        if not its: continue
        if side in 'tb':
            xs = spread([it[1] - cx + pl for it in its], R + 4, W - R - 4, j.get('gap', GAP))
            y = pt / 2 if side == 't' else pt + ch + pb / 2
            for it, x in zip(its, xs): pos[it[0]] = (x, y)
        else:
            ys = spread([it[2] - cy + pt for it in its], R + 4, H - R - 4, j.get('gap', GAP))
            x = pl / 2 if side == 'l' else pl + cw + pr / 2
            for it, y in zip(its, ys): pos[it[0]] = (x, y)
    S = lambda v: v * s
    cmd = ['convert', src]
    if j.get('crop'): cmd += ['-crop', f'{int(S(cw))}x{int(S(ch))}+{int(S(cx))}+{int(S(cy))}', '+repage']
    cmd += ['-background', BG, '-gravity', 'northwest', '-splice', f'{int(S(pl))}x{int(S(pt))}',
            '-gravity', 'southeast', '-splice', f'{int(S(pr))}x{int(S(pb))}', '-gravity', 'northwest']
    lines_w, lines_c, dots, circ, txt = [], [], [], [], []
    for n, tx, ty, side in items:
        X, Y = S(tx - cx + pl), S(ty - cy + pt); px, py = map(S, pos[n])
        lines_w.append(f'line {px:.0f},{py:.0f} {X:.0f},{Y:.0f}')
        dots.append(f'circle {X:.0f},{Y:.0f} {X + S(4.5):.0f},{Y:.0f}')
        circ.append(f'circle {px:.0f},{py:.0f} {px + S(R):.0f},{py:.0f}')
        txt.append((px, py, str(n)))
    cmd += ['-fill', 'none', '-stroke', OUT_COL, '-strokewidth', str(S(4.2))] + sum([['-draw', l] for l in lines_w], [])
    cmd += ['-stroke', COL, '-strokewidth', str(S(2))] + sum([['-draw', l] for l in lines_w], [])
    cmd += ['-fill', COL, '-stroke', OUT_COL, '-strokewidth', str(S(1.5))] + sum([['-draw', d] for d in dots + circ], [])
    cmd += ['-stroke', 'none', '-fill', '#ffffff', '-font', 'DejaVu-Sans-Bold', '-pointsize', str(S(FS if True else 0))]
    for px, py, t in txt:
        fs = S(FS if len(t) == 1 else FS * 0.86)
        cmd += ['-pointsize', f'{fs:.0f}', '-gravity', 'northwest', '-annotate',
                f'+{px - fs * 0.31 * len(t):.0f}+{py - fs * 0.56:.0f}', t]
    if j.get('resize'): cmd += ['-resize', f"{j['resize']}x"]
    out = os.path.join(base, j['out']); cmd += [out]
    subprocess.check_call(cmd); print('wrote', out)

if __name__ == '__main__':
    spec = sys.argv[1]; base = os.path.dirname(os.path.abspath(spec))
    jobs = json.load(open(spec)); only = set(sys.argv[2:])
    for j in jobs:
        if not only or j['out'] in only: job(j, base)
