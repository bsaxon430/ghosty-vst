#!/usr/bin/env python3
"""Ghosty Pro art under the resource names Vital's interface reads (vital_ring / vital_v / vital_word / vital_word_ring).
Vital's logo is its trademark, so our ghost takes its place. Vital's Paths::vitalV() reads vital_v with vital_ring's
byte size, so those two files are padded to the same length. The word mark is the text 'ghosty pro' outlined from
Poppins SemiBold (SIL Open Font License 1.1). Usage: make_brand_svgs.py <Poppins-SemiBold.ttf> <out dir>"""
import sys, os
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

font_path, out = sys.argv[1], sys.argv[2]
L = 1701.0

def ghost_body(cx, top, w, h, wav=4):
    # a ghost: round head, straight sides, wavy hem (clockwise)
    r = w / 2; x0, x1 = cx - r, cx + r; yb = top + h
    d = f"M{x0:.1f} {top + r:.1f} A{r:.1f} {r:.1f} 0 0 1 {x1:.1f} {top + r:.1f} L{x1:.1f} {yb:.1f} "
    seg = w / wav
    for i in range(wav):
        xa = x1 - seg * i; xm = xa - seg / 2; xb = xa - seg
        d += f"Q{xa - seg * 0.25:.1f} {yb - seg * 0.55:.1f} {xm:.1f} {yb - seg * 0.3:.1f} Q{xb + seg * 0.25:.1f} {yb - seg * 0.05:.1f} {xb:.1f} {yb:.1f} "
    return d + "Z"

def circle(cx, cy, r, ccw=False):
    s = 0 if ccw else 1
    return f"M{cx - r:.1f} {cy:.1f} A{r:.1f} {r:.1f} 0 1 {s} {cx + r:.1f} {cy:.1f} A{r:.1f} {r:.1f} 0 1 {s} {cx - r:.1f} {cy:.1f} Z"

def svg(view, d, pad_to=None):
    s = (f'<?xml version="1.0" encoding="UTF-8"?>\n<svg xmlns="http://www.w3.org/2000/svg" viewBox="{view}">'
         f'<path fill-rule="evenodd" d="{d}"/></svg>\n')
    if pad_to is not None:
        assert len(s) <= pad_to, (len(s), pad_to)
        s = s[:-7] + "<!--" + " " * (pad_to - len(s) - 7) + "-->" + s[-7:]
        assert len(s) == pad_to
    return s

c = L / 2
ring = circle(c, c, 800) + " " + circle(c, c, 690, ccw=True)
ghost = ghost_body(c, 330, 860, 1050) + " " + circle(c - 170, 700, 70, ccw=True) + " " + circle(c + 170, 700, 70, ccw=True)
size = max(len(svg(f"0 0 {L:.0f} {L:.0f}", ring)), len(svg(f"0 0 {L:.0f} {L:.0f}", ghost))) + 40

def word_path(text, scale, x, base):
    f = TTFont(font_path); gs = f.getGlyphSet(); cmap = f.getBestCmap(); hmtx = f["hmtx"]
    pen = SVGPathPen(gs)
    for ch in text:
        g = cmap.get(ord(ch))
        if g is None: continue
        tp = TransformPen(pen, (scale, 0, 0, -scale, x, base))
        gs[g].draw(tp)
        x += hmtx[g][0] * scale
    return pen.getCommands(), x

word, _ = word_path("ghosty pro", 0.16, 20, 175)
os.makedirs(out, exist_ok=True)
open(os.path.join(out, "vital_ring.svg"), "w").write(svg(f"0 0 {L:.0f} {L:.0f}", ring, size))
open(os.path.join(out, "vital_v.svg"), "w").write(svg(f"0 0 {L:.0f} {L:.0f}", ghost, size))
open(os.path.join(out, "vital_word.svg"), "w").write(svg("0 0 936 246", word))
open(os.path.join(out, "vital_word_ring.svg"), "w").write(svg("0 0 936 246", word))
print("ok", size)
