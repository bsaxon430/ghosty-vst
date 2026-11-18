#!/usr/bin/env python3
"""Puts repainted pictures (out/art.json, out/meta.json from pack.py) into an existing Block Step page, in place.
Only the picture blocks (<script type="text/plain" id="art-...">) and the scene numbers (/*SMETA*/.../*END*/) change.
Usage: python3 page/swap_art.py [path/to/index.html]"""
import base64, json, os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__)); WORK = os.path.dirname(HERE)
page = sys.argv[1] if len(sys.argv) > 1 else os.path.join(WORK, '..', 'data', 'index.html')
s = open(page, encoding='utf-8').read()
arts = json.load(open(os.path.join(WORK, 'out', 'art.json')))
for aid, a in arts.items():
    b = base64.b64encode(open(os.path.join(WORK, 'out', a['file']), 'rb').read()).decode('ascii')
    pat = re.compile(r'<script type="text/plain" id="art-' + re.escape(aid) + r'" data-mime="[^"]*">[A-Za-z0-9+/=]*</script>')
    assert len(pat.findall(s)) == 1, f'picture block art-{aid} not found exactly once'
    s = pat.sub(lambda m: f'<script type="text/plain" id="art-{aid}" data-mime="{a["mime"]}">{b}</script>', s)
meta = json.load(open(os.path.join(WORK, 'out', 'meta.json')))
pat = re.compile(r'/\*SMETA\*/.*?/\*END\*/', re.S); assert len(pat.findall(s)) == 1, 'scene numbers not found exactly once'
s = pat.sub(lambda m: '/*SMETA*/' + json.dumps(meta, separators=(',', ':')) + '/*END*/', s)
open(page, 'w', encoding='utf-8').write(s); print(f'wrote {page}: {len(s.encode()) / 1e6:.2f} MB, {len(arts)} pictures')
