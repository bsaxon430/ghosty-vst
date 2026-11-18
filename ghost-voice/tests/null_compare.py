#!/usr/bin/env python3
# Ghost Voice null test: every WAV Ghosty Spectrum's own renderer wrote (GhostVoiceABC) against the one Ghost Voice wrote for
# the same job (GhostVoiceTests dsp), sample for sample, as the 24-bit integers in the files. Any difference fails.
#   null_compare.py SPECTRUM_DIR GHOSTVOICE_DIR
import sys
from pathlib import Path
import numpy as np
import soundfile as sf

spec, gv = Path(sys.argv[1]), Path(sys.argv[2])
files = sorted(p.name for p in spec.glob('*.wav'))
bad = 0
for n in files:
    a, fa = sf.read(spec / n, dtype='int32', always_2d=True)
    if not (gv / n).is_file():
        print(f'MISSING  {n}'); bad += 1; continue
    b, fb = sf.read(gv / n, dtype='int32', always_2d=True)
    if fa != fb or a.shape != b.shape:
        print(f'DIFFER   {n}: {a.shape}@{fa} vs {b.shape}@{fb}'); bad += 1; continue
    d = int(np.max(np.abs(a.astype(np.int64) - b.astype(np.int64)))) if a.size else 0
    print(f'{"NULL    " if d == 0 else "DIFFER  "} {n}: {a.shape[0]} samples @ {fa} Hz, max difference {d} (24-bit steps)')
    bad += d != 0
print(f'{len(files) - bad} / {len(files)} files null' if files else 'no files')
sys.exit(1 if bad or not files else 0)
