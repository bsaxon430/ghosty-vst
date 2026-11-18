"""Experimental identity-space fusion utilities.

These functions operate only in the embedding space supplied by one encoder.
They do not create audio and do not make embeddings compatible with another model.
"""
from __future__ import annotations
import hashlib
import numpy as np


def _unit(x: np.ndarray, eps: float = 1e-10) -> np.ndarray:
    x = np.asarray(x, dtype=np.float32).reshape(-1)
    n = float(np.linalg.norm(x))
    if n < eps:
        raise ValueError("Identity vector has near-zero magnitude")
    return x / n


def cosine(a: np.ndarray, b: np.ndarray) -> float:
    return float(np.clip(np.dot(_unit(a), _unit(b)), -1.0, 1.0))


def _stable_orthogonal_direction(a: np.ndarray, b: np.ndarray, seed: str) -> np.ndarray:
    """Create a repeatable direction orthogonal to the A/B span.

    This is a controlled *novelty probe*, not a learned human identity direction.
    A learned singing-identity manifold is required for perceptually meaningful use.
    """
    a, b = _unit(a), _unit(b)
    digest = hashlib.sha256(seed.encode("utf-8")).digest()
    rng = np.random.default_rng(int.from_bytes(digest[:8], "little"))
    for _ in range(32):
        v = rng.normal(size=a.shape[0]).astype(np.float32)
        v -= np.dot(v, a) * a
        # Remove the component along B after accounting for its A component.
        b_perp = b - np.dot(b, a) * a
        denom = float(np.dot(b_perp, b_perp))
        if denom > 1e-8:
            v -= (np.dot(v, b_perp) / denom) * b_perp
        n = float(np.linalg.norm(v))
        if n > 1e-5:
            return v / n
    raise ValueError("Could not find an orthogonal novelty direction")


def fuse_identity(a: np.ndarray, b: np.ndarray, weight_b: float = 0.5,
                  novelty: float = 0.0, seed: str = "ghost-voice") -> np.ndarray:
    """Fuse two normalized embeddings and optionally add a novelty probe.

    weight_b: 0=A, 1=B.
    novelty: 0..0.5; experimental orthogonal displacement magnitude.
    The output remains in the same encoder's vector space, but is not guaranteed
    to be on the learned manifold or accepted by any singing renderer.
    """
    if not 0.0 <= weight_b <= 1.0:
        raise ValueError("weight_b must be between 0 and 1")
    if not 0.0 <= novelty <= 0.5:
        raise ValueError("novelty must be between 0 and 0.5")
    a, b = _unit(a), _unit(b)
    if a.shape != b.shape:
        raise ValueError("A and B embeddings must have identical dimensions")
    base = _unit((1.0 - weight_b) * a + weight_b * b)
    if novelty == 0.0:
        return base
    direction = _stable_orthogonal_direction(a, b, seed)
    # Tangent displacement followed by normalization keeps the vector unit length.
    return _unit(base + novelty * direction)


def diagnostics(a: np.ndarray, b: np.ndarray, c: np.ndarray) -> dict:
    return {
        "cosine_a_b": round(cosine(a, b), 6),
        "cosine_a_c": round(cosine(a, c), 6),
        "cosine_b_c": round(cosine(b, c), 6),
        "distance_a_c": round(1.0 - cosine(a, c), 6),
        "distance_b_c": round(1.0 - cosine(b, c), 6),
        "warning": "Embedding distances are diagnostics, not proof of a perceptually new singer.",
    }
