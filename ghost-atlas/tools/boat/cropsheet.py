#!/usr/bin/env python3
"""For looking at the result: a world's painted film frames, cut to the part round the masks, side by side.
   cropsheet.py <world> <outfile.jpg> [every how many frames]"""
import sys, os, cv2, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import where
from film_masks import frames_of, mask_of
w, outfile = sys.argv[1], sys.argv[2]
step = int(sys.argv[3]) if len(sys.argv) > 3 else 3
names = frames_of(w)
union = np.zeros((658, 1206), np.uint8)
for i in range(len(names)): union = np.maximum(union, mask_of(w, i))
ys, xs = np.where(union > 0)
x0, x1, y0, y1 = max(0, xs.min() - 70), min(1206, xs.max() + 70), max(0, ys.min() - 70), min(658, ys.max() + 70)
tiles = []
for i in list(range(0, len(names), step)) + ([len(names) - 1] if (len(names) - 1) % step else []):
    img = cv2.imread(f'{where.work()}/film/{w}/{names[i]}')[y0:y1, x0:x1].copy()
    cv2.putText(img, names[i][:2], (6, 24), cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 0, 0), 4); cv2.putText(img, names[i][:2], (6, 24), cv2.FONT_HERSHEY_SIMPLEX, 0.8, (255, 255, 255), 2)
    tiles.append(img)
cols = 3 if (x1 - x0) > 500 else 4
s = min(1.0, 1560 / (cols * (x1 - x0)))
tiles = [cv2.resize(t, None, fx=s, fy=s, interpolation=cv2.INTER_AREA) for t in tiles]
while len(tiles) % cols: tiles.append(np.zeros_like(tiles[0]))
cv2.imwrite(outfile, np.vstack([np.hstack(tiles[i:i + cols]) for i in range(0, len(tiles), cols)]), [cv2.IMWRITE_JPEG_QUALITY, 86])
print(w, 'crop', (x0, y0, x1, y1), 'scale', round(s, 2), len(tiles), 'tiles')
