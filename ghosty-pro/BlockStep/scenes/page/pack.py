#!/usr/bin/env python3
"""Turns the painter's output (out/*.png + out/*.json) into the pictures and numbers the page carries (out/art.json, out/meta.json)."""
import json, os, sys
from PIL import Image
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'out')
arts, meta = {}, {}
def jpg(src, dst, q=84, sub=0):
    im = Image.open(os.path.join(OUT, src)).convert('RGB'); im.save(os.path.join(OUT, dst), 'JPEG', quality=q, subsampling=sub, optimize=True, progressive=True)
    return os.path.getsize(os.path.join(OUT, dst))
def png(src, dst, colors=None):
    im = Image.open(os.path.join(OUT, src)).convert('RGBA')
    if colors: im = im.quantize(colors=colors, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.FLOYDSTEINBERG)
    im.save(os.path.join(OUT, dst), 'PNG', optimize=True); return os.path.getsize(os.path.join(OUT, dst))
r2 = lambda v: round(v, 1)
def plate(name, key, q=84):
    if not os.path.exists(os.path.join(OUT, name + '.png')): return
    n = jpg(name + '.png', key + '.jpg', q); arts[key] = {'file': key + '.jpg', 'mime': 'image/jpeg'}
    m = json.load(open(os.path.join(OUT, name + '.json'))) if os.path.exists(os.path.join(OUT, name + '.json')) else {}
    keep = {k: m[k] for k in ('W', 'H', 'hero', 'vp') if k in m}
    for k in ('lamps', 'wisps', 'puddles', 'spots', 'drums', 'holo', 'parts', 'mill', 'light', 'drum', 'birds'):
        if k in m: keep[k] = [[r2(v) if isinstance(v, (int, float)) else v for v in row] for row in m[k]] if isinstance(m[k], list) and m[k] and isinstance(m[k][0], list) else m[k]
    meta[key] = keep; print(f'{key}: {n / 1e3:.0f} kB')
plate('city', 'cityL'); plate('city-p', 'cityP'); plate('anime', 'animeL'); plate('anime-p', 'animeP')
for f in sorted(os.listdir(OUT)):
    if f.endswith('.o.png'): continue
    if f.startswith('spr-') and f.endswith('.png'):
        key = f[4:-4]; a = f[:-4] + '.o.png'; n = png(f, a); arts[key] = {'file': a, 'mime': 'image/png'}; print(f'{key}: {n / 1e3:.0f} kB')
    if f.startswith('add-') and f.endswith('.png'):   # additive light sprites: black background, no alpha needed
        key = f[4:-4]; a = f[:-4] + '.jpg'; n = jpg(f, a, 82, 0); arts[key] = {'file': a, 'mime': 'image/jpeg'}; print(f'{key}: {n / 1e3:.0f} kB')
if os.path.exists(os.path.join(OUT, 'holo.json')): meta['holo'] = json.load(open(os.path.join(OUT, 'holo.json')))
if os.path.exists(os.path.join(OUT, 'animeSpr.json')): meta['animeSpr'] = json.load(open(os.path.join(OUT, 'animeSpr.json')))
json.dump(arts, open(os.path.join(OUT, 'art.json'), 'w')); json.dump(meta, open(os.path.join(OUT, 'meta.json'), 'w'))
print('total', sum(os.path.getsize(os.path.join(OUT, a['file'])) for a in arts.values()) // 1000, 'kB')
