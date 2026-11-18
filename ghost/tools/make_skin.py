#!/usr/bin/env python3
"""Ghost Bass b9 (recoloured AKIRA in build 12): builds the GhostBlock skin for the ENGINE (Surge) page from Surge's own "Surge Dark" skin.

Surge's skin system takes a folder (skin.xml + SVG images); we copy Surge's own dark skin and recolour it: every grey becomes a
deep navy-to-lilac ramp, Surge's orange value arcs become violet, its blue modulation colour becomes cyan. Nothing of the layout or
the code changes: it is still Surge's own skin, selectable beside the original skins in Surge's menu.

  python3 ghost/tools/make_skin.py resources/data/skins/dark-mode.surge-skin resources/data/skins/GhostBlock.surge-skin
"""
import colorsys, os, re, shutil, sys

src, dst = sys.argv[1], sys.argv[2]
# build 12 (AKIRA): midnight navy to warm paper white, Kaneda red for Surge's value colour, electric cyan for modulation,
# sodium amber for greens, red kept red
NAVY = (5, 9, 18)
PAPER = (236, 230, 214)
RED = (229, 36, 44)
CYAN = (53, 224, 236)
AMBER = (244, 163, 64)

def lerp(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))

def mapc(r, g, b):
    h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
    hd = h * 360
    if s < 0.16:                     # greys: deep navy through steel to paper white (the mid greys lean steel blue)
        t = (0.299 * r + 0.587 * g + 0.114 * b) / 255
        c = lerp(NAVY, PAPER, t ** 1.15)
        k = t * (1 - t)
        return (c[0] - 14 * k, c[1] + 6 * k, c[2] + 40 * k)
    if 15 <= hd < 55:                # Surge's orange: value arcs, hover, selection -> Kaneda red
        return lerp((70, 10, 14), RED, min(1.0, v * 1.05)) if v < 0.98 else RED
    if 185 <= hd < 260:              # Surge's blue: modulation -> electric cyan
        return lerp((6, 40, 52), CYAN, min(1.0, v * 1.05)) if v < 0.98 else CYAN
    if 80 <= hd < 160:               # greens -> sodium amber
        return lerp((60, 36, 10), AMBER, min(1.0, v))
    if hd >= 330 or hd < 15:         # reds -> Kaneda red
        return lerp((r, g, b), RED, 0.5)
    return (r, g, b)

HEX = re.compile(r'#([0-9a-fA-F]{8}|[0-9a-fA-F]{6}|[0-9a-fA-F]{3})(?![0-9a-zA-Z])')

def repl(m):
    h = m.group(1)
    a = ''
    if len(h) == 3:
        h = ''.join(c * 2 for c in h)
    if len(h) == 8:
        h, a = h[:6], h[6:]
    r, g, b = int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)
    r, g, b = [max(0, min(255, int(round(x)))) for x in mapc(r, g, b)]
    return '#%02X%02X%02X%s' % (r, g, b, a.upper())

if os.path.exists(dst):
    shutil.rmtree(dst)
shutil.copytree(src, dst)
n = 0
for root, _, files in os.walk(dst):
    for f in files:
        if f.endswith(('.svg', '.xml')):
            p = os.path.join(root, f)
            t = open(p, encoding='utf-8').read()
            t2 = HEX.sub(repl, t)
            if f == 'skin.xml':
                t2 = re.sub(r'<surge-skin [^>]*>', '<surge-skin name="GhostBlock" author="Ghost Bass" authorURL="https://github.com/bsaxon430/ghosty-vst" version="2">', t2, count=1)
            open(p, 'w', encoding='utf-8').write(t2)
            n += 1
print('recoloured', n, 'files ->', dst)
