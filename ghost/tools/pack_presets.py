#!/usr/bin/env python3
# Packs the synths' factory sound files into one file per synth (GHOSTPACK1), so GHOST keeps each sound's folder
# (its category) and exact bytes. Run from ghost/: python3 tools/pack_presets.py
import json, os, glob
HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
def pack(out, items):
    with open(out, 'wb') as f:
        f.write(b'GHOSTPACK1\n')
        for cat, name, data in items:
            f.write(f'{cat}\t{name}\t{len(data)}\n'.encode('utf-8')); f.write(data); f.write(b'\n')
    print(out, len(items))
vit = []
for p in sorted(glob.glob(os.path.join(HERE, 'presets/ghostypro2/*.vital'))):
    data = open(p, 'rb').read(); style = json.loads(data).get('preset_style') or 'Other'
    name = os.path.splitext(os.path.basename(p))[0]
    if len(name) > 4 and name[:3].isdigit() and name[3] == ' ': name = name[4:]
    vit.append((style, name, data))
pack(os.path.join(HERE, 'presets/ghostypro2.pack'), vit)
p2 = []
root = os.path.join(HERE, 'presets/poltergeist2')
for p in sorted(glob.glob(os.path.join(root, '*/*.sxsnp'))):
    p2.append((os.path.basename(os.path.dirname(p)), os.path.splitext(os.path.basename(p))[0], open(p, 'rb').read()))
pack(os.path.join(HERE, 'presets/poltergeist2.pack'), p2)
