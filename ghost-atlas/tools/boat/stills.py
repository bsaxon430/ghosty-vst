"""Paints the boat out of the paintings' source frames (the whole frame in the network's view), into BOAT_WORK/out.
The eight paintings that have a film are taken from their film's first frame instead (finish_stills.py)."""
import os, sys, time, cv2, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import where, lama
from masks import SHAPES, TOUCH, mask_for
names = sys.argv[1:] or sorted(SHAPES)
for n in names:
    t = time.time()
    img = cv2.imread(f'{where.stills()}/{n}.png'); m = mask_for(n)
    out = lama.inpaint(img, m)
    if n in TOUCH: out = lama.inpaint(out, mask_for(n, 2, TOUCH[n]))
    os.makedirs(f'{where.work()}/out', exist_ok=True)
    cv2.imwrite(f'{where.work()}/out/{n}.png', out); cv2.imwrite(f'{where.work()}/out/{n}_mask.png', m)
    print(n, round(time.time() - t, 1), 's', flush=True)
