#!/usr/bin/env python3
"""The eighteen paintings without the boat, at the size the plug-in keeps them: each painted-out source frame is enlarged
the way the paintings always were (Real-ESRGAN general x4v3, then down to 2640 x 1440 by area, JPEG quality 88), and the
nine small pictures of the worlds are made from the first view of each."""
import os, sys, time, cv2, numpy as np, importlib.util
HERO = {'amber': 's01a', 'crimson': 's02a', 'blossom': 's03b', 'lantern': 's04b', 'opera': 's05a', 'glow': 's06c', 'snow': 's07b', 'aurora': 's08a'}
FIRST = ['s01a', 's02a', 's03b', 's04b', 's05a', 's06c', 's07b', 's08a', 's09a']
ALL = ['s01a','s01b','s02a','s02b','s03a','s03b','s04a','s04b','s05a','s05b','s06b','s06c','s07a','s07b','s08a','s08b','s09a','s09b']
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import where
SR = where.sr()
spec = importlib.util.spec_from_file_location('sr', f'{SR}/sr.py'); sr = importlib.util.module_from_spec(spec); spec.loader.exec_module(sr)
import torch; torch.set_num_threads(2)
model = sr.load(f'{SR}/realesr-general-x4v3.pth')
dest = sys.argv[1]; os.makedirs(dest, exist_ok=True)
film_of = {v: k for k, v in HERO.items()}
for n in (sys.argv[2:] or ALL):
    t = time.time()
    src = f'{where.work()}/film/{film_of[n]}/00.png' if n in film_of else f'{where.work()}/out/{n}.png'      # (a painting that has a film is its film's first frame)
    up = sr.run(model, cv2.imread(src))
    big = cv2.resize(up, (2640, 1440), interpolation=cv2.INTER_AREA)
    cv2.imwrite(f'{dest}/{n}.jpg', big, [cv2.IMWRITE_JPEG_QUALITY, 88])
    if n in FIRST: cv2.imwrite(f'{dest}/t{FIRST.index(n)}.jpg', cv2.resize(big, (352, 192), interpolation=cv2.INTER_AREA), [cv2.IMWRITE_JPEG_QUALITY, 86])
    print(n, 'from', os.path.relpath(src, where.work()), round(time.time() - t, 1), 's', flush=True)
