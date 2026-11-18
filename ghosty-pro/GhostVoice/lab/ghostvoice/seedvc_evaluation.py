"""Same-backend diagnostics for a rendered Voice C singing result.

These scores are useful for spotting obvious source dominance, but CAMPPlus is
not a validated singing-only identity evaluator. Do not use them as a pass/fail
claim that Voice C is a new person.
"""
from __future__ import annotations
from pathlib import Path
from typing import Sequence
import torch
import torch.nn.functional as F
from .seedvc_style import enroll_campplus_identity


def _cos(a: torch.Tensor, b: torch.Tensor) -> float:
    return float(F.cosine_similarity(a.float(), b.float(), dim=-1).item())


def evaluate_voice_c(paths_a: Sequence[str | Path], paths_b: Sequence[str | Path],
                     paths_c: Sequence[str | Path], campplus_model: torch.nn.Module,
                     device: str | torch.device | None = None) -> dict:
    """Compare a rendered C against enrolled A/B reference sets."""
    if not paths_c:
        raise ValueError("Provide at least one rendered Voice C file")
    a, report_a = enroll_campplus_identity(paths_a, campplus_model, device)
    b, report_b = enroll_campplus_identity(paths_b, campplus_model, device)
    c, report_c = enroll_campplus_identity(paths_c, campplus_model, device)
    ab = _cos(a, b)
    ac = _cos(a, c)
    bc = _cos(b, c)
    closer = "A" if ac > bc else "B" if bc > ac else "equidistant"
    return {
        "metric_encoder": "Seed-VC CAMPPlus 192-D",
        "voice_a": report_a,
        "voice_b": report_b,
        "voice_c_rendered": report_c,
        "cosine_similarity": {
            "a_to_b": round(ab, 6),
            "a_to_c": round(ac, 6),
            "b_to_c": round(bc, 6),
        },
        "nearest_source_in_embedding_space": closer,
        "source_similarity_imbalance": round(abs(ac - bc), 6),
        "interpretation": (
            "Diagnostic only. A lower C-to-A/B similarity may indicate separation, but this is the same CAMPPlus family used for conditioning and is not an independent singing identity test. "
            "Use a second evaluator and blinded listening before claiming C is perceptually distinct."
        ),
    }
