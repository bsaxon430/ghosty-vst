#!/usr/bin/env python3
# Phantom FX: pack one painted look from the art export into assets/looks/<look>/ (what the plugin embeds).
#   python3 tools/pack_look.py /path/to/export/neon            -> assets/looks/neon/
# What it does (PaintedLook.h's loader reads every form):
#   - keeps the @2x set only (the plugin scales it once to the window and caches that); soft pictures (light overlays, glows,
#     blurred backgrounds) are kept at 1x
#   - opaque pictures become JPEG (quality from look.json "pack.quality", default 90)
#   - pictures with transparency become a JPEG for the colour plus a small 8-bit PNG for the transparency ("name@2x.jpg" +
#     "name@2x.a.png", 8 bits with a grey palette) when that is at least a quarter smaller than the PNG; the colour is bled outwards under the transparent
#     part first, so no dark fringe appears at the edges. Full resolution is kept (sharp on Retina).
#   - look.json "pack.drop": name patterns (fnmatch on "folder/name") of pictures this look never draws
#   - look.json "pack.scale": 1.5 stores "@1.5x" sprites instead of @2x (smaller, a little softer on Retina); off by default
# A look.json (name, order, layout numbers, 9-slice insets, anchors) is written from the CEL one if the look has none yet.
import os, sys, shutil, json, io, fnmatch
import numpy as np
from PIL import Image
from scipy.ndimage import gaussian_filter
src = sys.argv[1].rstrip('/'); look = os.path.basename(src)
root = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'assets', 'looks')
dst = os.path.join(root, look); total = 0
lj = os.path.join(dst, 'look.json')
if not os.path.exists(lj):
    os.makedirs(dst, exist_ok=True)
    j = json.load(open(os.path.join(root, 'cel', 'look.json'))); j['name'] = look.upper().replace('-', ' '); j['order'] = 99
    json.dump(j, open(lj, 'w'), indent=1)
pack = json.load(open(lj)).get('pack', {})
quality = int(pack.get('quality', 90)); drop = pack.get('drop', []); scale = float(pack.get('scale', 2))
for d, _, fs in os.walk(dst):
    for f in fs:
        if f.endswith(('.png', '.jpg')): os.remove(os.path.join(d, f))   # a re-export replaces the old sprites (look.json and fonts stay)

def bleed(im):
    # fill the colour under transparent pixels with the nearby visible colour (repeated blur of the premultiplied picture)
    a = np.asarray(im.getchannel('A'), dtype=np.float32) / 255.0
    rgb = np.asarray(im.convert('RGB'), dtype=np.float32)
    out = rgb.copy(); known = (a > 0.02).astype(np.float32); cur = rgb * known[..., None]; w = known.copy()
    for radius in (2, 4, 8, 16):
        bc = np.stack([gaussian_filter(cur[..., c], radius) for c in range(3)], -1)
        bw = gaussian_filter(w, radius)
        fill = (w < 0.5) & (bw > 1e-3)
        est = bc / np.maximum(bw, 1e-6)[..., None]
        out[fill] = est[fill]; cur[fill] = est[fill]; w[fill] = 1.0
    return Image.fromarray(np.clip(out, 0, 255).astype(np.uint8), 'RGB')

kept = dropped = 0
for d, _, fs in os.walk(src):
    rel = os.path.relpath(d, src)
    for f in sorted(fs):
        if not f.endswith(('.png', '.jpg')): continue
        soft = rel.startswith('overlay') or rel.startswith('glow') or '-glow' in f or '-blur' in f   # soft pictures: 1x is enough
        if soft == ('@2x' in f): continue
        if f.endswith('.jpg'):   # the export already made it a JPEG (backgrounds, blurs): take it as it is
            name = (rel + '/' + f[:-4].replace('@2x', '')).lstrip('./')
            if any(fnmatch.fnmatch(name, pat) for pat in drop): dropped += 1; continue
            os.makedirs(os.path.join(dst, rel), exist_ok=True); shutil.copyfile(os.path.join(d, f), os.path.join(dst, rel, f)); total += os.path.getsize(os.path.join(d, f)); kept += 1; continue
        if os.path.exists(os.path.join(d, f[:-4] + '.jpg')): continue
        name = (rel + '/' + f[:-4].replace('@2x', '')).lstrip('./')
        if any(fnmatch.fnmatch(name, pat) for pat in drop): dropped += 1; continue
        p = os.path.join(d, f); os.makedirs(os.path.join(dst, rel), exist_ok=True)
        im = Image.open(p); stem = f[:-4]
        if not soft and scale != 2:
            im = im.convert('RGBA').resize((max(1, round(im.width * scale / 2)), max(1, round(im.height * scale / 2))), Image.LANCZOS)
            stem = stem.replace('@2x', '@%gx' % scale)
        opaque = im.mode in ('RGB', 'L') or (im.mode == 'RGBA' and im.getchannel('A').getextrema()[0] == 255)
        png = io.BytesIO(); im.save(png, 'PNG', optimize=True); png = png.getvalue(); outs = [(stem + '.png', png)]
        if opaque:
            jb = io.BytesIO(); im.convert('RGB').save(jb, 'JPEG', quality=quality, subsampling=0)
            if len(jb.getvalue()) < len(png) * 0.75: outs = [(stem + '.jpg', jb.getvalue())]
        elif im.mode in ('RGBA', 'LA', 'P') and len(png) > 6000:
            im = im.convert('RGBA'); jb = io.BytesIO(); bleed(im).save(jb, 'JPEG', quality=quality, subsampling=0)
            ab = io.BytesIO(); mk = im.getchannel('A').convert('P'); mk.putpalette([v for i in range(256) for v in (i, i, i)]); mk.save(ab, 'PNG', optimize=True)   # (a 256-grey palette, not a greyscale PNG: a system loader that colour-manages greyscale leaves it alone)
            if len(jb.getvalue()) + len(ab.getvalue()) < len(png) * 0.75: outs = [(stem + '.jpg', jb.getvalue()), (stem + '.a.png', ab.getvalue())]
        for n, data in outs:
            open(os.path.join(dst, rel, n), 'wb').write(data); total += len(data)
        kept += 1
print(look, 'packed:', kept, 'pictures,', dropped, 'dropped,', round(total / 1e6, 2), 'MB')
