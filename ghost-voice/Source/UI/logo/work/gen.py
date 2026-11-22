# Ghost Voice logo, round 3 ("lettered", refined for THE GHOST CHOIR). Own drawing, AGPL-3.0 with the plug-in.
# Makes the singing-ghost mark, the brush-lettered GHOST VOICE wordmark and the lockup, each in a DARK and a LIGHT version,
# as SVG, and the PNGs (rsvg-convert). Run:  python3 gen.py   (needs numpy + shapely, and rsvg-convert on the PATH)
import sys, os, math, subprocess, numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
from letters import catmull, lines, nib_stroke, wisp_stroke, to_path, resample
from shapely.geometry import Point, Polygon
from shapely import affinity
from shapely import affinity
from shapely.ops import unary_union
OUT = os.path.dirname(HERE)

# ------------------------------------------------------------------ colours
# (polish 3) DARK glows: a wide ectoplasm aura (glow) under a tighter haze, and the trailing wisp behind the letters is lit.
# LIGHT stays crisp: only a whisper of haze, the trail a pale violet under the ink.
DARK = dict(ground="#120d19", letter0="#f6f0ff", letter1="#cbb8f2", line="#efe6ff", body0="#f2ecfd", body1="#b9a2ee", ink="#160d20",
            wisp="#b9a2ee", haze="#9f82ee", hazeA=0.40, glow="#b49cff", glowA=0.30, glowR=15, trail="#c9b6ff", trailA=0.55, trailGlow=0.45)
LIGHT = dict(ground="#f3ecdf", letter0="#2a1636", letter1="#4a2a7a", line="#2a1636", body0="#ffffff", body1="#d9ccf2", ink="#2a1636",
             wisp="#6d3cc4", haze="#7b52d4", hazeA=0.10, glow="#7b52d4", glowA=0.0, glowR=0, trail="#8a63d8", trailA=0.42, trailGlow=0.0)

def smooth(g, r=6):
    return g.buffer(r, quad_segs=12).buffer(-2 * r, quad_segs=12).buffer(r, quad_segs=12)

# ------------------------------------------------------------------ THE HAND (polish 2): a pointed pen. Thin on the up- and side-strokes,
# swelling only where the pen pulls down (and down-left, the way a right-handed pen leans), tapering in and out. Every line of the
# wordmark and of the ghost is drawn with it, so they read as one hand.
def pen(pts, wmin, wmax, n=22, taper=(0.10, 0.14), closed=False):
    P = catmull(pts, n) if not closed else catmull(pts + pts[:3], n)[n:-n * 2 + 1]
    P = resample(np.array(P), 1.0); m = len(P)
    d = np.diff(P, axis=0); L = np.linalg.norm(d, axis=1) + 1e-9
    pull = np.clip((d[:, 1] - 0.30 * d[:, 0]) / L, 0, 1) ** 1.4
    k = 11; pull = np.convolve(np.r_[np.full(k, pull[0]), pull, np.full(k, pull[-1])], np.ones(2 * k + 1) / (2 * k + 1), 'same')[k:-k]
    polys = []
    for i in range(m - 1):
        t = i / max(1, m - 2)
        f = 1.0
        if not closed:
            if taper[0] > 0 and t < taper[0]: f = 0.25 + 0.75 * math.sin(t / taper[0] * math.pi / 2)
            if taper[1] > 0 and t > 1 - taper[1]: f = 0.25 + 0.75 * math.sin((1 - t) / taper[1] * math.pi / 2)
        w = (wmin + (wmax - wmin) * pull[i]) * f / 2
        nn = np.array([-d[i][1], d[i][0]]) / L[i]
        polys.append(Polygon([P[i] + nn * w, P[i + 1] + nn * w, P[i + 1] - nn * w, P[i] - nn * w]))
    return unary_union(polys).buffer(0.5, quad_segs=6)

def wisp(pts, r0, r1=0.25, n=22):
    return wisp_stroke(catmull(pts, n), r0, r1, curve=lambda t: t ** 0.7)

# ------------------------------------------------------------------ the letters: flowing hand-drawn capitals, cap height 200 (y down)
WMIN, WMAX = 3.6, 17.0
def L_G(): return 176, [[(150, 46), (158, 26), (140, 6), (96, 2), (48, 24), (18, 92), (30, 164), (78, 200), (130, 192), (156, 152), (156, 108)],
                        [(118, 114), (150, 106), (178, 108)]]
def L_H(): return 172, [[(20, 22), (36, 8), (40, 60), (36, 140), (30, 198)], [(146, 2), (142, 90), (140, 196), (152, 206)],
                        [(14, 112), (60, 100), (110, 106), (164, 94)]]
def L_O(): return 176, [[(98, 6), (40, 28), (14, 104), (36, 176), (90, 202), (146, 180), (166, 106), (146, 30), (98, 4), (70, 12)]]
def L_S(): return 140, [[(128, 34), (110, 6), (66, 2), (30, 28), (36, 74), (86, 104), (124, 140), (118, 184), (74, 202), (32, 192), (12, 166)]]
def L_T(): return 170, [[(2, 26), (40, 10), (96, 16), (140, 12), (172, 2)], [(92, 14), (88, 110), (84, 194), (72, 206), (60, 198)]]
def L_V(): return 172, [[(0, 4), (10, 2), (46, 100), (86, 200)], [(86, 200), (126, 90), (166, 4), (178, -6)]]
def L_I(): return 58, [[(18, 10), (32, 4), (30, 100), (26, 198), (38, 206)]]
def L_C(): return 160, [[(150, 44), (132, 8), (86, 2), (36, 28), (14, 100), (32, 172), (84, 202), (132, 190), (158, 158)]]
def L_E(): return 146, [[(30, 6), (28, 100), (24, 196)], [(20, 10), (74, 2), (136, 6)], [(28, 102), (66, 96), (104, 100)], [(18, 198), (70, 194), (132, 196)]]
LETTERS = {'G': L_G, 'H': L_H, 'O': L_O, 'S': L_S, 'T': L_T, 'V': L_V, 'I': L_I, 'C': L_C, 'E': L_E}
SHEAR = 0.16   # a slight forward lean, as written

def wordgeom():
    x = 0; parts = []; boxes = []
    for i, ch in enumerate("GHOST VOICE"):
        if ch == ' ': x += 50; continue
        w, strokes = LETTERS[ch]()
        g = unary_union([pen(s, WMIN, WMAX) for s in strokes])
        dy = 2.5 * math.sin(i * 1.3 + 0.4)          # a hand's small rise and fall along the line
        g = affinity.translate(affinity.rotate(g, [-1, 0.6, -0.4, 1, 0, -0.6, 0.8, -0.8, 0.5, -0.5, 0.7][i], origin=(w / 2, 100)), x, dy)
        parts.append(g); boxes.append((ch, x, w, dy)); x += w + 16
    W = x - 16
    letters = unary_union(parts)
    # VOICE's O is a ghost's face: two eyes and a singing "O", drawn small in the same hand
    _, ox, ow, ody = [b for b in boxes if b[0] == 'O'][1]
    cx, cy = ox + 90, 104 + ody
    face = unary_union([affinity.scale(Point(cx - 24, cy - 26).buffer(8.5), 0.72, 1.3), affinity.scale(Point(cx + 22, cy - 27).buffer(8.5), 0.72, 1.3),
                        affinity.scale(Point(cx, cy + 26).buffer(14).difference(Point(cx, cy + 26).buffer(8.5)), 0.8, 1.15)])
    # the wisp: E's foot does not stop, it trails off as smoke, curling up and back and thinning to nothing
    _, ex, ew, edy = boxes[-1]
    trail = wisp([(ex + 128, 196 + edy), (ex + 168, 188 + edy), (ex + 198, 160 + edy), (ex + 208, 122 + edy), (ex + 196, 96 + edy), (ex + 176, 100 + edy), (ex + 174, 118 + edy)], 3.4, 0.2)
    # (polish 3) the trailing wisp: a ribbon of ectoplasm that drifts along behind the letters, low through the word, swelling in the
    # middle and thinning to nothing at both ends, the way a ghost leaves a trail. It runs into E's curl of smoke
    rib_pts = [(-70, 112), (-58, 150), (-20, 182), (60, 206), (150, 196), (230, 170), (320, 186), (400, 212), (500, 200), (590, 176),
               (680, 190), (770, 212), (870, 196), (970, 176), (1070, 192), (1170, 212), (1270, 196), (1370, 176), (1470, 194), (ex + 30, 210), (ex + 128, 196)]
    rib = wisp_stroke(catmull(rib_pts, 22), 0.3, 4.6, curve=lambda t: math.sin(math.pi * min(1.0, t * 1.03)) ** 0.8)
    # a second, finer strand of the same smoke, a little behind and out of step with the first
    rib2_pts = [(-40, 168), (40, 196), (140, 182), (250, 200), (360, 178), (470, 208), (580, 190), (700, 174), (820, 204), (940, 190), (1060, 208), (1180, 184), (1300, 206), (1420, 186), (ex + 20, 206)]
    rib = unary_union([rib.buffer(0), wisp_stroke(catmull(rib2_pts, 22), 0.2, 1.8, curve=lambda t: math.sin(math.pi * t) ** 1.2).buffer(0)]).buffer(0)
    # a little condensed (the header is a narrow place) and leaning forward, as written
    sh = lambda g: affinity.skew(affinity.scale(g, 0.80, 1.0, origin=(0, 200)), xs=-math.degrees(math.atan(SHEAR)), origin=(0, 200))
    return sh(letters), sh(face), sh(trail), W, sh(rib)

def word_svg(C, T=lambda g: g, idp="w", sc=1.0):
    L, F, Wp, W, R = wordgeom()
    L, F, Wp, R = T(L), T(F), T(Wp), T(R)
    b = L.bounds; wb = Wp.bounds; rb = R.bounds
    s = f'<defs><linearGradient id="{idp}g" x1="0" y1="{b[1]:.0f}" x2="0" y2="{b[3]:.0f}" gradientUnits="userSpaceOnUse"><stop offset="0" stop-color="{C["letter0"]}"/>' \
        f'<stop offset="0.6" stop-color="{C["letter0"]}"/><stop offset="1" stop-color="{C["letter1"]}"/></linearGradient>' \
        f'<linearGradient id="{idp}t" x1="{wb[0]:.0f}" y1="0" x2="{wb[2]:.0f}" y2="0" gradientUnits="userSpaceOnUse"><stop offset="0" stop-color="{C["wisp"]}" stop-opacity="0.15"/>' \
        f'<stop offset="0.12" stop-color="{C["wisp"]}" stop-opacity="0.75"/><stop offset="0.85" stop-color="{C["wisp"]}" stop-opacity="0.9"/><stop offset="1" stop-color="{C["wisp"]}" stop-opacity="0.35"/></linearGradient>' \
        f'<linearGradient id="{idp}r" x1="{rb[0]:.0f}" y1="0" x2="{rb[2]:.0f}" y2="0" gradientUnits="userSpaceOnUse"><stop offset="0" stop-color="{C["trail"]}" stop-opacity="0"/>' \
        f'<stop offset="0.25" stop-color="{C["trail"]}" stop-opacity="{C["trailA"]}"/><stop offset="0.75" stop-color="{C["trail"]}" stop-opacity="{C["trailA"] * 0.8:.2f}"/><stop offset="1" stop-color="{C["trail"]}" stop-opacity="{C["trailA"]}"/></linearGradient>' \
        f'<filter id="{idp}f" x="-10%" y="-40%" width="120%" height="180%"><feGaussianBlur stdDeviation="{7 * sc:.1f}"/></filter>' \
        f'<filter id="{idp}o" x="-10%" y="-60%" width="120%" height="220%"><feGaussianBlur stdDeviation="{max(1, C["glowR"]) * sc:.1f}"/></filter>' \
        f'<filter id="{idp}s" x="-10%" y="-100%" width="120%" height="300%"><feGaussianBlur stdDeviation="{5 * sc:.1f}"/></filter></defs>'
    if C["glowA"] > 0: s += f'<path d="{to_path(L)}" fill="{C["glow"]}" opacity="{C["glowA"]}" filter="url(#{idp}o)"/>'
    if C["trailGlow"] > 0: s += f'<path d="{to_path(R)}" fill="{C["trail"]}" opacity="{C["trailGlow"]}" filter="url(#{idp}s)"/>'
    s += f'<path d="{to_path(R)}" fill="url(#{idp}r)"/>'
    s += f'<path d="{to_path(L)}" fill="{C["haze"]}" opacity="{C["hazeA"]}" filter="url(#{idp}f)"/>'
    s += f'<path d="{to_path(Wp)}" fill="url(#{idp}t)"/>'
    s += f'<path d="{to_path(L)}" fill="url(#{idp}g)"/>'
    s += f'<path d="{to_path(F)}" fill="{C["letter0"]}"/>'
    return s, unary_union([L.buffer(14 * sc), Wp.buffer(0), R.buffer(0)]).bounds

# ------------------------------------------------------------------ the mark: the same hand draws a ghost singing, its hem trailing off
# a hooded head, shoulders that fall away, and a hem of three soft lobes that drifts to the left: the last lobe becomes the wisp
GH = [(262, 36), (322, 46), (366, 92), (382, 170), (380, 260), (392, 350), (414, 430), (432, 500), (414, 524), (388, 500), (362, 530),
      (334, 506), (304, 540), (276, 516), (244, 552), (214, 534), (186, 500), (164, 430), (150, 340), (144, 250), (150, 160), (178, 84), (218, 46)]
def ghost(pen_scale):
    body = smooth(Polygon(catmull(GH + GH[:1], 16)).buffer(0), 4)
    big = pen_scale > 5   # the 32 px mark: one even, heavier line so it holds at that size
    outline = pen(GH + [GH[0], GH[1], (330, 58)], WMIN * pen_scale * (1.6 if big else 1.0), WMAX * pen_scale * (0.45 if big else 0.75), n=16, taper=(0.02, 0.06))
    tail = wisp([(244, 552), (214, 584), (172, 600), (124, 594), (92, 566), (92, 536)], 2.4 * pen_scale, 0.3, 16)   # the hem trails off as smoke
    er = 22 if big else 16
    eyes = unary_union([affinity.rotate(affinity.scale(Point(234, 186).buffer(er), 0.72, 1.3), 10), affinity.rotate(affinity.scale(Point(310, 182).buffer(er), 0.72, 1.3), -8)])
    mouth = affinity.scale(Point(274, 282).buffer(34 if big else 28), 0.78, 1.18) if big else \
            affinity.scale(Point(274, 282).buffer(28).difference(Point(274, 282).buffer(15)), 0.78, 1.18)   # singing "O"
    return body, outline, tail, unary_union([eyes, mouth])

def mark_svg(C, T=lambda g: g, idp="m", small=False, sc=1.0, pen_scale=3.2):
    ps = pen_scale * (2.4 if small else 1.0)
    body, outline, wisps, feats = ghost(ps)
    body, outline, wisps, feats = T(body), T(outline), T(wisps), T(feats)
    b = body.bounds
    s = f'<defs><linearGradient id="{idp}b" x1="0" y1="{b[1]:.0f}" x2="0" y2="{b[3]:.0f}" gradientUnits="userSpaceOnUse">' \
        f'<stop offset="0" stop-color="{C["body0"]}" stop-opacity="0.95"/><stop offset="0.6" stop-color="{C["body0"]}" stop-opacity="0.85"/>' \
        f'<stop offset="1" stop-color="{C["body1"]}" stop-opacity="{0.9 if small else 0.25}"/></linearGradient></defs>'
    if not small and C.get("glowA", 0) > 0:   # (polish 3) DARK: the ghost glows like its letters
        s = s.replace('</defs>', f'<filter id="{idp}o" x="-30%" y="-30%" width="160%" height="160%"><feGaussianBlur stdDeviation="{C["glowR"] * 1.6 * sc:.1f}"/></filter></defs>')
        s += f'<path d="{to_path(body)}" fill="{C["glow"]}" opacity="{C["glowA"] * 1.1:.2f}" filter="url(#{idp}o)"/>'
    s += f'<path d="{to_path(body)}" fill="url(#{idp}b)"/>'
    if not small: s += f'<path d="{to_path(wisps)}" fill="{C["wisp"]}" opacity="0.85"/>'
    s += f'<path d="{to_path(outline)}" fill="{C["line"]}"/>'
    s += f'<path d="{to_path(feats)}" fill="{C["ink"]}"/>'
    return s, unary_union([body, outline] + ([] if small else [wisps])).bounds

def ghost_small(): return None
def wrap(body, bb, pad, bg=None, square=False):
    x0, y0, x1, y1 = bb; w = x1 - x0; h = y1 - y0
    if square: m = max(w, h); x0 -= (m - w) / 2; y0 -= (m - h) / 2; w = h = m
    x0 -= pad; y0 -= pad; w += 2 * pad; h += 2 * pad
    r = f'<rect x="{x0:.0f}" y="{y0:.0f}" width="{w:.0f}" height="{h:.0f}" fill="{bg}"/>' if bg else ''
    return f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{x0:.1f} {y0:.1f} {w:.1f} {h:.1f}">{r}{body}</svg>', w, h

def save(name, svg):
    open(os.path.join(OUT, name), 'w').write(svg)

def png(svgname, pngname, width=None, height=None):
    a = ['rsvg-convert', os.path.join(OUT, svgname), '-o', os.path.join(OUT, pngname)]
    if width: a += ['-w', str(width)]
    if height: a += ['-h', str(height)]
    subprocess.check_call(a)

def lockup(C, idp):
    ws, wbb = word_svg(C, idp=idp + "w")
    wh = wbb[3] - wbb[1]
    _, mbb = mark_svg(C)
    sc = wh * 1.10 / (mbb[3] - mbb[1])
    T0 = lambda g: affinity.scale(g, sc, sc, origin=(0, 0))
    _, mbb2 = mark_svg(C, T0, pen_scale=1.0 / sc)
    dx = wbb[0] - mbb2[2] - 44; dy = (wbb[1] + wbb[3]) / 2 - (mbb2[1] + mbb2[3]) / 2
    T = lambda g: affinity.translate(T0(g), dx, dy)
    ms, mbb3 = mark_svg(C, T, idp + "m", sc=sc, pen_scale=1.0 / sc)
    return ms + ws, (min(mbb3[0], wbb[0]), min(mbb3[1], wbb[1]), max(mbb3[2], wbb[2]), max(mbb3[3], wbb[3]))

if __name__ == '__main__':
    for nm, C in (("dark", DARK), ("light", LIGHT)):
        ms, mbb = mark_svg(C, idp="m" + nm)
        svg, w, h = wrap(ms, mbb, 24, square=True); save(f"mark-{nm}.svg", svg)
        save(f"_mark-{nm}-ground.svg", wrap(ms, mbb, 90, C["ground"], square=True)[0])
        png(f"mark-{nm}.svg", f"mark-{nm}.png", 2048)
        png(f"_mark-{nm}-ground.svg", f"mark-on-{nm}.png", 2048)
        ss, sbb = mark_svg(C, idp="s" + nm, small=True)
        save(f"mark-small-{nm}.svg", wrap(ss, sbb, 30, square=True)[0])
        png(f"mark-small-{nm}.svg", f"mark-32-{nm}.png", 32)
        save(f"_mark32-{nm}-ground.svg", wrap(ss, sbb, 60, C["ground"], square=True)[0]); png(f"_mark32-{nm}-ground.svg", f"mark-32-on-{nm}.png", 32)
        ws, wbb = word_svg(C, idp="w" + nm)
        svg, w, h = wrap(ws, wbb, 20); save(f"wordmark-{nm}.svg", svg)
        save(f"_word-{nm}-ground.svg", wrap(ws, wbb, 80, C["ground"])[0])
        png(f"wordmark-{nm}.svg", f"wordmark-{nm}.png", 2048); png(f"_word-{nm}-ground.svg", f"wordmark-on-{nm}.png", 2048)
        ls, lbb = lockup(C, "l" + nm)
        svg, w, h = wrap(ls, lbb, 20); save(f"lockup-{nm}.svg", svg)
        save(f"_lockup-{nm}-ground.svg", wrap(ls, lbb, 90, C["ground"])[0])
        png(f"lockup-{nm}.svg", f"lockup-{nm}.png", 2048); png(f"_lockup-{nm}-ground.svg", f"lockup-on-{nm}.png", 2048)
        # the header art the plug-in embeds (2x of its 64 px tall slot)
        png(f"lockup-{nm}.svg", f"header-{nm}.png", height=128)
    for f in os.listdir(OUT):
        if f.startswith('_'): os.remove(os.path.join(OUT, f))
    print("ok")

# ------------------------------------------------------------------ embed the header art in the plug-in (Source/UI/LogoData.h)
def embed():
    out = ["// Ghost Voice - AGPL-3.0-only. GENERATED by Source/UI/logo/work/gen.py: the GHOST VOICE lockup for the header, DARK and LIGHT",
           "// (PNG, 128 px tall = 2x the header slot). Decoded once into a juce::Image (Editor.cpp), never parsed per frame.", "#pragma once", "namespace gv::logodata", "{"]
    for nm in ("dark", "light"):
        b = open(os.path.join(OUT, f"header-{nm}.png"), 'rb').read()
        rows = [", ".join(str(x) for x in b[i:i + 40]) for i in range(0, len(b), 40)]
        out.append(f"inline const unsigned char header_{nm}[] = {{\n" + ",\n".join(rows) + "\n};")
    out.append("}")
    open(os.path.join(os.path.dirname(OUT), "LogoData.h"), 'w').write("\n".join(out) + "\n")

if __name__ == '__main__':
    embed()
