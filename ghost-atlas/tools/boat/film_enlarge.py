#!/usr/bin/env python3
"""The film's enlarged frames (1320 x 720) without the boat. Only the part that was painted in is enlarged again: the
window round the mask goes through the same model as before, is set down in an otherwise empty picture four times the
source's size, that picture is brought to 1320 x 720 exactly as the whole frame was (by area), and the part inside the
mask's box is laid over the old enlarged frame with a soft edge. Everything else in the frame stays as it was.
   film_enlarge.py [world or world:first-last ...]"""
import os, sys, time, cv2, numpy as np, importlib.util
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import where
from film_masks import HERO, frames_of, which
SR = where.sr()
spec = importlib.util.spec_from_file_location('sr', f'{SR}/sr.py'); sr = importlib.util.module_from_spec(spec); spec.loader.exec_module(sr)
import torch; torch.set_num_threads(2)
model = sr.load(f'{SR}/realesr-general-x4v3.pth')
CLEAN, OLD, NEW, KEEP = where.work() + '/film', where.film() + '/big', where.work() + '/big', (1320, 720)      # (NEW then takes the place of FILM_WORK/big for make_film.py pack)
for spec in (sys.argv[1:] or HERO):
    w, wanted = which(spec)
    os.makedirs(f'{NEW}/{w}', exist_ok=True); t = time.time()
    for n in [frames_of(w)[i] for i in wanted]:
        clean = cv2.imread(f'{CLEAN}/{w}/{n}'); mask = cv2.imread(f'{CLEAN}/{w}/{n[:-4]}_mask.png', 0); old = cv2.imread(f'{OLD}/{w}/{n}')
        H, W = mask.shape
        ys, xs = np.where(mask > 0)
        rx0, rx1, ry0, ry1 = max(0, xs.min() - 16), min(W, xs.max() + 17), max(0, ys.min() - 16), min(H, ys.max() + 17)       # what is replaced
        cx0, cx1, cy0, cy1 = max(0, rx0 - 48), min(W, rx1 + 48), max(0, ry0 - 48), min(H, ry1 + 48)                           # what is enlarged
        up = sr.run(model, clean[cy0:cy1, cx0:cx1])
        canvas = np.zeros((H * 4, W * 4, 3), np.uint8)
        canvas[cy0 * 4:cy1 * 4, cx0 * 4:cx1 * 4] = up
        small = cv2.resize(canvas, KEEP, interpolation=cv2.INTER_AREA)
        sx, sy = KEEP[0] / W, KEEP[1] / H
        box = np.zeros((KEEP[1], KEEP[0]), np.uint8)
        box[int(np.ceil(ry0 * sy)):int(np.floor(ry1 * sy)), int(np.ceil(rx0 * sx)):int(np.floor(rx1 * sx))] = 255
        a = np.clip(cv2.distanceTransform(box, cv2.DIST_L2, 5) / 6.0, 0, 1)[..., None].astype(np.float32)
        # (where the box reaches the frame's own edge nothing is blended there: the distance is to the box's other sides)
        out = (old * (1 - a) + small * a + 0.5).astype(np.uint8)
        cv2.imwrite(f'{NEW}/{w}/{n}', out)
    print(w, len(wanted), 'frames', round(time.time() - t, 1), 's', flush=True)
