#!/usr/bin/env python3
"""Run an upstream Seed-VC script unchanged, with a fixed random seed (and optionally with the Apple GPU hidden).

  seeded_python.py inference.py --source ... (the script's own arguments)

Seed-VC draws the flow-matching start noise with torch.randn and never seeds it, so two runs of the same settings differ.
Seeding here (outside the GPL code) makes the A anchor, B anchor and every blend start from the same noise, so the clips
differ only by the voice. GV_SEED (default 1234) sets the seed; GV_FORCE_CPU=1 hides MPS so the upstream device pick
falls back to the CPU (used when the GitHub Mac's virtual GPU cannot run the model).
"""
import os
import runpy
import sys

import torch

if os.environ.get('GV_FORCE_CPU') == '1':
    torch.backends.mps.is_available = lambda: False  # upstream picks cuda > mps > cpu at import time
seed = int(os.environ.get('GV_SEED', '1234'))
torch.manual_seed(seed)
try:
    import numpy as np
    import random
    np.random.seed(seed)
    random.seed(seed)
except Exception:
    pass

script = sys.argv[1]
sys.argv = sys.argv[1:]
sys.path.insert(0, os.path.dirname(os.path.abspath(script)))
runpy.run_path(script, run_name='__main__')
