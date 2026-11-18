"""Small, repeatable evaluation helpers for A/B/C embedding experiments."""
from __future__ import annotations
import numpy as np
from .fusion import cosine


def score_identity_triplet(a: np.ndarray, b: np.ndarray, c: np.ndarray) -> dict:
    ac, bc, ab = cosine(a, c), cosine(b, c), cosine(a, b)
    return {
        "cosine": {"A_vs_B": round(ab, 5), "A_vs_C": round(ac, 5), "B_vs_C": round(bc, 5)},
        "closer_source": "A" if ac > bc else "B" if bc > ac else "tie",
        "distinctness_margin": round(min(1.0 - ac, 1.0 - bc), 5),
        "interpretation": "Use only to compare runs from the same encoder. Human listening and a singing-domain evaluator are still required.",
    }
