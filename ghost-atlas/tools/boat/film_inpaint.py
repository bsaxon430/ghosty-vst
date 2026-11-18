#!/usr/bin/env python3
"""Paints the boat out of every film frame (source size), one frame at a time, in a window round the mask.
   film_inpaint.py [world or world:first-last ...]"""
import os, sys, time, cv2, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import where, lama
from film_masks import HERO, frames_of, mask_of, which
SRC, OUT = where.film() + '/src', where.work() + '/film'
for spec in (sys.argv[1:] or HERO):
    w, wanted = which(spec)
    os.makedirs(f'{OUT}/{w}', exist_ok=True)
    names = frames_of(w); t = time.time()
    for i in wanted:
        n = names[i]
        img = cv2.imread(f'{SRC}/{w}/{n}'); m = mask_of(w, i)
        cv2.imwrite(f'{OUT}/{w}/{n[:-4]}_mask.png', m)
        cv2.imwrite(f'{OUT}/{w}/{n}', lama.inpaint(img, m, whole=False))
    print(w, len(wanted), 'frames', round(time.time() - t, 1), 's', flush=True)
