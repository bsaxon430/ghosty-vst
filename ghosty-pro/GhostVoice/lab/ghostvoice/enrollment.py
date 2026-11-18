"""Multi-reference enrollment with segment-level quality checks.

This improves reference consistency only. The encoder remains speech-trained ECAPA,
so these vectors are experimental and are not a singing renderer conditioning format.
"""
from __future__ import annotations
import numpy as np
from .embedding import normalize


def segment_audio(audio: np.ndarray, sr: int, window_seconds: float = 3.0,
                  overlap_seconds: float = 0.5, min_seconds: float = 1.5):
    x = np.asarray(audio, dtype=np.float32).reshape(-1)
    if sr <= 0:
        raise ValueError("Sample rate must be positive")
    if x.size < int(min_seconds * sr):
        raise ValueError(f"Reference must be at least {min_seconds:.1f} seconds")
    win = max(1, int(window_seconds * sr))
    hop = max(1, int((window_seconds - overlap_seconds) * sr))
    if x.size <= win:
        return [x]
    starts = list(range(0, x.size - win + 1, hop))
    if starts[-1] + win < x.size:
        starts.append(x.size - win)
    return [x[s:s + win] for s in starts]


def _quality_weight(segment: np.ndarray) -> float:
    x = np.asarray(segment, dtype=np.float32)
    rms = float(np.sqrt(np.mean(x * x) + 1e-12))
    peak = float(np.max(np.abs(x)))
    if rms < 0.003:  # reject near-silence
        return 0.0
    if peak > 0.999 and np.mean(np.abs(x) >= 0.999) > 0.002:
        return 0.0  # likely clipped reference
    # Softly prefer healthy levels without letting loudness dominate identity.
    return float(np.clip((rms / 0.05) ** 0.25, 0.5, 1.5))


def enroll_references(paths, encoder, load_audio_fn):
    """Encode multiple takes, reject poor segments, robustly aggregate one identity.

    Returns (identity_vector, diagnostics). Uses a cosine-medoid outlier guard,
    then a quality-weighted normalized mean of the inliers.
    """
    if isinstance(paths, (str, bytes)):
        paths = [paths]
    paths = list(paths or [])
    if not paths:
        raise ValueError("Provide at least one reference recording")
    vectors, weights, labels = [], [], []
    rejected = []
    for path in paths:
        audio, sr = load_audio_fn(path)
        chunks = segment_audio(audio, sr)
        accepted_for_file = 0
        for idx, chunk in enumerate(chunks):
            weight = _quality_weight(chunk)
            if weight <= 0:
                rejected.append({"file": str(path), "segment": idx, "reason": "silence_or_clipping"})
                continue
            vec = normalize(encoder.encode(chunk, sr))
            vectors.append(vec)
            weights.append(weight)
            labels.append(str(path))
            accepted_for_file += 1
        if accepted_for_file == 0:
            rejected.append({"file": str(path), "reason": "no_usable_segments"})
    if not vectors:
        raise ValueError("No usable vocal segments. Use clean, dry, unclipped recordings.")
    matrix = np.stack(vectors)
    # Cosine medoid is robust to one bad take; reject only strong outliers.
    similarity = matrix @ matrix.T
    centrality = np.median(similarity, axis=1)
    medoid_idx = int(np.argmax(centrality))
    threshold = max(0.20, float(centrality[medoid_idx]) - 0.25)
    inliers = centrality >= threshold
    if not np.any(inliers):
        inliers[medoid_idx] = True
    inlier_matrix = matrix[inliers]
    inlier_weights = np.asarray(weights, dtype=np.float32)[inliers]
    identity = normalize(np.sum(inlier_matrix * inlier_weights[:, None], axis=0))
    per_file = {}
    for label, keep in zip(labels, inliers):
        per_file[label] = per_file.get(label, 0) + int(bool(keep))
    return identity, {
        "reference_files": len(paths),
        "segments_encoded": len(vectors),
        "segments_kept": int(np.sum(inliers)),
        "segments_rejected": len(rejected) + int(np.sum(~inliers)),
        "kept_segments_by_file": per_file,
        "rejections": rejected,
        "aggregation": "cosine-medoid outlier guard + quality-weighted normalized mean",
        "warning": "Reference consistency diagnostics only; speech-trained encoder is not validated for singing identity.",
    }
