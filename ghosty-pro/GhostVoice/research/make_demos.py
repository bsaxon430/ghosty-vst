#!/usr/bin/env python3
"""Demo mp3s + the blind listening pages for the A/B proof.

  make_demos.py --voices VOICES --results RESULTS_DIR --out DEMOS_DIR

Writes:
  DEMOS/clips/<clip>.mp3                every render as rendered (level untouched), plus source / A / B references
  DEMOS/side_by_side_<mix>.mp3          the source, then Seed-VC, NATURAL, CLASSIC at that blend, 1 s apart, each at -18 LUFS
                                        (level-matched so the louder one does not win)
  DEMOS/blind_engines/index.html        the lab's blind page idea extended to the engine question: per blend the three engines
                                        shuffled as X / Y / Z, rated for naturalness, artifacts and "how much like B"
  DEMOS/PRIVATE_engine_answer_key.json  which letter is which (outside the shared folder)
The lab's own blind page (A anchor / B anchor / 50 % C, Seed-VC only) is written by the lab's benchmark into its run folder.
"""
import argparse
import json
import random
import shutil
import subprocess
from pathlib import Path

import numpy as np
from scipy import signal
import soundfile as sf
import importlib.util

HERE = Path(__file__).resolve().parent
_spec = importlib.util.spec_from_file_location('gvm', str(HERE.parents[1] / 'docs/research/tools/ghostvoice_measure.py'))
gvm = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(gvm)

FS = 44100
ENGINES = [('seedvc', 'Seed-VC (neural)'), ('natural', 'NATURAL (our DSP)'), ('classic', 'CLASSIC (our DSP)')]
MIXES = [('anchor_a', 'A anchor (0 % B)'), ('fusion_025', '25 % B'), ('fusion_050', '50 % B'), ('fusion_075', '75 % B'), ('anchor_b', 'B anchor (100 % B)')]


def load44(path):
    x, fs = sf.read(str(path), always_2d=True, dtype='float64')
    x = x.mean(axis=1)
    if fs != FS:
        g = np.gcd(fs, FS)
        x = signal.resample_poly(x, FS // g, fs // g)
    return x


def to_lufs(x, target=-18.0):
    l = gvm.lufs(x, FS)
    if np.isfinite(l):
        x = x * 10 ** ((target - l) / 20)
    pk = np.max(np.abs(x)) + 1e-12
    return x * (0.97 / pk) if pk > 0.97 else x


def mp3(x, path):
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix('.tmp.wav')
    sf.write(str(tmp), np.clip(x, -1, 1), FS, subtype='PCM_16')
    subprocess.run(['lame', '--quiet', '-b', '192', str(tmp), str(path)], check=True)
    tmp.unlink()


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--voices', type=Path, required=True)
    p.add_argument('--results', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    res = json.loads((a.results / 'results.json').read_text())
    clips = {k: v['file'] for k, v in res['clips'].items()}
    out = a.out
    out.mkdir(parents=True, exist_ok=True)
    for ref in ('source', 'A1', 'B1'):
        mp3(load44(a.voices / f'{ref}.wav'), out / 'clips' / f'ref_{ref}.mp3')
    for k, f in clips.items():
        mp3(load44(f), out / 'clips' / f'{k}.mp3')
    gap = np.zeros(FS)
    src = to_lufs(load44(a.voices / 'source.wav'))
    order = {}
    for mix, label in MIXES:
        parts, names = [src, gap], ['source performance']
        for eng, elabel in ENGINES:
            k = f'{eng}_{mix}'
            if k in clips:
                parts += [to_lufs(load44(clips[k])), gap]
                names.append(elabel)
        mp3(np.concatenate(parts), out / f'side_by_side_{mix}.mp3')
        order[f'side_by_side_{mix}.mp3'] = names
    (out / 'side_by_side_order.json').write_text(json.dumps(order, indent=2) + '\n')

    # blind engine page (mp3, level-matched, letters shuffled per blend with a fixed seed)
    blind = out / 'blind_engines'
    if blind.exists():
        shutil.rmtree(blind)
    blind.mkdir()
    rng = random.Random(20261105)
    key, groups = {}, []
    mp3(src, blind / 'source.mp3')
    mp3(to_lufs(load44(a.voices / 'A1.wav')), blind / 'voice_A.mp3')
    mp3(to_lufs(load44(a.voices / 'B1.wav')), blind / 'voice_B.mp3')
    for mix, label in MIXES:
        engs = [e for e, _ in ENGINES if f'{e}_{mix}' in clips]
        rng.shuffle(engs)
        items = []
        for letter, eng in zip('XYZ', engs):
            name = f'{mix}_{letter}.mp3'
            mp3(to_lufs(load44(clips[f'{eng}_{mix}'])), blind / name)
            key[name] = eng
            items.append({'file': name, 'letter': letter})
        groups.append({'mix': mix, 'label': label, 'items': items})
    (out / 'PRIVATE_engine_answer_key.json').write_text(json.dumps({'seed': 20261105, 'assignment': key}, indent=2) + '\n')
    page = PAGE.replace('__GROUPS__', json.dumps(groups))
    (blind / 'index.html').write_text(page)
    print('demos in', out)


PAGE = r'''<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Ghost Voice Blind Test</title>
<style>
:root{--bg:#0f1115;--card:#171a21;--ink:#ecebe6;--dim:#9a9ca3;--line:#2a2e38;--acc:#b8f5c0}
@media (prefers-color-scheme: light){:root{--bg:#f4f3ef;--card:#fff;--ink:#1b1c1f;--dim:#5e6068;--line:#dcdbd5;--acc:#226b33}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.45 system-ui,sans-serif}
main{max-width:760px;margin:0 auto;padding:24px 16px 60px}h1{font-size:24px;margin:0 0 6px}p{color:var(--dim)}
section{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:16px;margin:18px 0}
h2{font-size:17px;margin:0 0 10px}.clip{border-top:1px solid var(--line);padding:12px 0}.clip:first-of-type{border-top:0}
audio{width:100%;margin:6px 0}label{display:grid;grid-template-columns:1fr 2fr 2.2em;gap:8px;align-items:center;font-size:14px;color:var(--dim)}
input[type=range]{width:100%}output{text-align:right;color:var(--ink)}textarea{width:100%;height:80px;background:var(--bg);color:var(--ink);border:1px solid var(--line);border-radius:8px;padding:8px}
button{background:var(--acc);color:#0b0d10;border:0;border-radius:8px;padding:12px 18px;font-weight:600;cursor:pointer}
.refs{display:grid;gap:4px}.refs span{font-size:13px;color:var(--dim)}
</style>
<main>
<h1>Ghost Voice blind test</h1>
<p>Every clip sings the same source melody and words. At each blend, X, Y and Z are three different engines in a shuffled order (all at the same loudness). Use headphones, rate each clip on its own, then download your ratings. Do not open the answer key until you are done.</p>
<section class="refs"><h2>References</h2>
<span>Source performance (what is being re-sung)</span><audio controls preload="none" src="source.mp3"></audio>
<span>Voice A</span><audio controls preload="none" src="voice_A.mp3"></audio>
<span>Voice B</span><audio controls preload="none" src="voice_B.mp3"></audio></section>
<form id="f"><div id="groups"></div>
<section><h2>Comments</h2><textarea name="comments"></textarea></section>
<button type="submit">Download my ratings</button></form>
<p style="font-size:13px">Singers: VocalSet (Wilkins, Seetharaman, Wahl &amp; Pardo, ISMIR 2018, CC BY 4.0). Research test only.</p>
</main>
<script>
const G=__GROUPS__;const root=document.getElementById('groups');
const slider=(n,t,min,max,v)=>`<label>${t}<input type="range" name="${n}" min="${min}" max="${max}" value="${v}" oninput="this.nextElementSibling.value=this.value"><output>${v}</output></label>`;
for(const g of G){const s=document.createElement('section');let h=`<h2>${g.label}</h2>`;
 for(const it of g.items){const id=it.file.replace('.mp3','');h+=`<div class="clip"><b>${it.letter}</b><audio controls preload="none" src="${it.file}"></audio>`+
  slider(id+'_natural','Natural (1 poor, 5 great)',1,5,3)+slider(id+'_artifacts','Artifacts (1 none, 5 bad)',1,5,3)+slider(id+'_like_b','Sounds like B (0 = A, 100 = B)',0,100,50)+`</div>`;}
 s.innerHTML=h;root.appendChild(s);}
document.getElementById('f').onsubmit=e=>{e.preventDefault();const d=Object.fromEntries(new FormData(e.target));d.date=new Date().toISOString();
 const b=new Blob([JSON.stringify(d,null,2)],{type:'application/json'});const a=document.createElement('a');a.href=URL.createObjectURL(b);a.download='ghost_voice_blind_ratings.json';a.click();setTimeout(()=>URL.revokeObjectURL(a.href),1000)};
</script></html>
'''

if __name__ == '__main__':
    main()
