#!/usr/bin/env python3
"""Fetch the lab's five VocalSet clips (pinned mirror revision) and cut them short for a CPU / Apple Silicon run.

  prepare_voices.py OUTDIR [--source-seconds 12] [--ref-seconds 20]

VocalSet: Wilkins, Seetharaman, Wahl & Pardo, "VocalSet: A Singing Voice Dataset", ISMIR 2018, CC BY 4.0
(https://zenodo.org/records/1193957). Research singers; no celebrity or real-artist voices. Writes OUTDIR/raw (as downloaded),
OUTDIR/voices/{source,A1,A2,B1,B2}.wav (mono, original rate, cut) and OUTDIR/voices/voices.json.
"""
import argparse
import json
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'lab'))
sys.path.insert(0, str(HERE.parent / 'lab' / 'scripts'))
import download_vocalset_test as dl  # noqa: E402  (the lab's own downloader)

MIRROR_REV = 'b81c42274e64c21e97aeddffa3551672ee393a7d'   # zhu3000/squillo-vocal-quality, checked 2026-11-05
dl.ROOT = f'https://huggingface.co/datasets/zhu3000/squillo-vocal-quality/resolve/{MIRROR_REV}/vocalset/'

ROLES = {'source': 'source_performance', 'A1': 'voice_a', 'A2': 'voice_a_additional', 'B1': 'voice_b', 'B2': 'voice_b_additional'}


def first_sung(x, fs, thr_db=-35.0):
    """index of the first 50 ms block within thr_db of the loudest block (skips the silence before the singer starts)"""
    hop = int(0.05 * fs)
    n = len(x) // hop
    if n < 2:
        return 0
    e = np.array([np.sqrt(np.mean(x[i * hop:(i + 1) * hop] ** 2)) + 1e-9 for i in range(n)])
    db = 20 * np.log10(e / e.max())
    k = int(np.argmax(db > thr_db))
    return max(0, (k - 2) * hop)


def cut(src, dst, seconds):
    x, fs = sf.read(str(src), always_2d=True, dtype='float32')
    x = x.mean(axis=1)
    s = first_sung(x, fs)
    y = x[s:s + int(seconds * fs)]
    fade = int(0.02 * fs)
    y[-fade:] *= np.linspace(1, 0, fade)
    sf.write(str(dst), y, fs, subtype='PCM_16')
    return {'file': dst.name, 'from': src.name, 'start_seconds': round(s / fs, 3), 'seconds': round(len(y) / fs, 3), 'sample_rate': fs}


def main():
    p = argparse.ArgumentParser()
    p.add_argument('out', type=Path)
    p.add_argument('--source-seconds', type=float, default=12.0)
    p.add_argument('--ref-seconds', type=float, default=20.0)
    a = p.parse_args()
    raw = a.out / 'raw'
    man = dl.download(raw)
    if man['errors']:
        raise SystemExit(f"download failed: {man['errors']}")
    voices = a.out / 'voices'
    voices.mkdir(parents=True, exist_ok=True)
    info = {'dataset': man['dataset'], 'license': man['license'], 'original_source': man['original_source'],
            'mirror': dl.ROOT, 'credit': 'VocalSet by Wilkins, Seetharaman, Wahl & Pardo (ISMIR 2018), CC BY 4.0',
            'roles': {'source': 'the performance that is re-sung (singer F3)', 'A': 'voice A (singer F1)', 'B': 'voice B (singer F2)'},
            'files': {}}
    for name, role in ROLES.items():
        src = Path(man['files'][role]['path'])
        info['files'][name] = cut(src, voices / f'{name}.wav', a.source_seconds if name == 'source' else a.ref_seconds)
    (voices / 'voices.json').write_text(json.dumps(info, indent=2) + '\n')
    print(json.dumps(info, indent=2))


if __name__ == '__main__':
    main()
