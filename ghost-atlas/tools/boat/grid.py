#!/usr/bin/env python3
"""Crops of the source frames with a coordinate grid, for drawing masks by eye.  grid.py <out folder> name:x0,y0,x1,y1[:path] ..."""
import os, sys, cv2, numpy as np
out = sys.argv[1]
for spec in sys.argv[2:]:
    parts = spec.split(':')
    name = parts[0]; x0, y0, x1, y1 = [int(v) for v in parts[1].split(',')]
    path = parts[2] if len(parts) > 2 else os.path.join(os.environ.get('STILLS_SRC', '.'), name + '.png')
    img = cv2.imread(path)
    h, w = img.shape[:2]
    x0, y0, x1, y1 = max(0, x0), max(0, y0), min(w, x1), min(h, y1)
    crop = img[y0:y1, x0:x1]
    s = min(1500 / (x1 - x0), 1000 / (y1 - y0), 4.0)
    big = cv2.resize(crop, None, fx=s, fy=s, interpolation=cv2.INTER_CUBIC)
    step = 20 if s >= 1.6 else 50
    for gx in range((x0 // step + 1) * step, x1, step):
        X = int(round((gx - x0) * s)); strong = gx % (step * 5 if step == 20 else 100) == 0
        cv2.line(big, (X, 0), (X, big.shape[0]), (255, 255, 255) if strong else (200, 200, 200), 1, cv2.LINE_AA) if strong else None
        if not strong:
            for Y in range(0, big.shape[0], 6): big[Y:Y+2, X] = (big[Y:Y+2, X] * 0.5 + 110).astype(np.uint8)
        if gx % (40 if step == 20 else 100) == 0:
            cv2.putText(big, str(gx), (X + 2, 12), cv2.FONT_HERSHEY_SIMPLEX, 0.4, (0, 0, 0), 3, cv2.LINE_AA); cv2.putText(big, str(gx), (X + 2, 12), cv2.FONT_HERSHEY_SIMPLEX, 0.4, (255, 255, 255), 1, cv2.LINE_AA)
    for gy in range((y0 // step + 1) * step, y1, step):
        Y = int(round((gy - y0) * s)); strong = gy % (step * 5 if step == 20 else 100) == 0
        if strong: cv2.line(big, (0, Y), (big.shape[1], Y), (255, 255, 255), 1, cv2.LINE_AA)
        else:
            for X in range(0, big.shape[1], 6): big[Y, X:X+2] = (big[Y, X:X+2] * 0.5 + 110).astype(np.uint8)
        if gy % (40 if step == 20 else 100) == 0:
            cv2.putText(big, str(gy), (2, Y - 3), cv2.FONT_HERSHEY_SIMPLEX, 0.4, (0, 0, 0), 3, cv2.LINE_AA); cv2.putText(big, str(gy), (2, Y - 3), cv2.FONT_HERSHEY_SIMPLEX, 0.4, (255, 255, 255), 1, cv2.LINE_AA)
    cv2.imwrite(f'{out}/{name}.jpg', big, [cv2.IMWRITE_JPEG_QUALITY, 88])
    print(name, (x0, y0, x1, y1), 'scale', round(s, 2), big.shape[1], 'x', big.shape[0])
