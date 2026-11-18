#!/usr/bin/env python3
# Ghost Voice demos: the same blend on every engine, one after another (SEED-VC, then NATURAL, then CLASSIC, 0.8 s apart),
# as mp3s for listening side by side. Levels are left exactly as each engine made them.
#   side_by_side.py E2E_DIR DEMO_DIR      (needs numpy, soundfile and lame)
import subprocess, sys
from pathlib import Path
import numpy as np
import soundfile as sf

src, out = Path(sys.argv[1]), Path(sys.argv[2]); out.mkdir(parents=True, exist_ok=True)
for b in ('000', '025', '050', '075', '100'):
    parts = [src / f'{e}_{b}.wav' for e in ('seedvc', 'natural', 'classic')]
    if not all(p.is_file() for p in parts):
        print('skip', b); continue
    clips, fs = [], None
    for p in parts:
        x, r = sf.read(p, always_2d=True); x = x.mean(axis=1)
        if fs is None: fs = r
        if r != fs:
            idx = np.arange(int(len(x) * fs / r)) * r / fs
            x = np.interp(idx, np.arange(len(x)), x)
        clips += [x, np.zeros(int(0.8 * fs))]
    y = np.concatenate(clips[:-1])
    wav = out / f'compare_{b}_seedvc-natural-classic.wav'
    sf.write(wav, y.astype(np.float32), fs, subtype='FLOAT')
    subprocess.run(['lame', '--quiet', '-b', '192', str(wav), str(wav.with_suffix('.mp3'))], check=False)
    wav.unlink()
    print('wrote', wav.with_suffix('.mp3').name)
