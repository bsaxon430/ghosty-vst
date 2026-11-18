#!/usr/bin/env python3
# Phantom FX: pack the unit visuals' parts from the art export into the painted looks (what the plugin embeds).
#   python3 tools/pack_visuals.py /home/claude/work/phantom-anime/visuals/export
# For each look folder in the export (dawn, cel, neon):
#   - copies export/<look>/<folder>/*.png|*.jpg|manifest.json to assets/looks/<look>/visual/<folder>/ (as exported: the masks are
#     small white + alpha PNGs, the two opaque backdrops JPEG; nothing is re-encoded, so what the painter made is what ships)
#   - merges export/<look>/look-visuals.json ("visual": stage rectangle, what lies under and over the picture, the frame) into
#     assets/looks/<look>/look.json
# Re-running it replaces the visual folder of each look; nothing else in the look is touched.
import os, sys, shutil, json
src = sys.argv[1].rstrip('/')
root = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'assets', 'looks')
total = 0
for look in sorted(os.listdir(src)):
    ls = os.path.join(src, look); ld = os.path.join(root, look)
    if not os.path.isdir(ls) or not os.path.isdir(ld): continue
    vd = os.path.join(ld, 'visual')
    if os.path.isdir(vd): shutil.rmtree(vd)
    n = 0; size = 0
    for folder in sorted(os.listdir(ls)):
        fs = os.path.join(ls, folder)
        if not os.path.isdir(fs): continue
        os.makedirs(os.path.join(vd, folder), exist_ok=True)
        for f in sorted(os.listdir(fs)):
            if f.endswith(('.png', '.jpg', 'manifest.json')):
                shutil.copy2(os.path.join(fs, f), os.path.join(vd, folder, f)); n += 1; size += os.path.getsize(os.path.join(fs, f))
    lv = os.path.join(ls, 'look-visuals.json')
    if os.path.exists(lv):
        lj = os.path.join(ld, 'look.json'); j = json.load(open(lj)); j['visual'] = json.load(open(lv))['visual']
        json.dump(j, open(lj, 'w'), indent=1)
    total += size
    print(f'{look}: {n} files, {size / 1e6:.2f} MB')
print(f'all looks: {total / 1e6:.2f} MB of visual parts')
