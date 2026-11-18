"""Singing-reference measurements for quality control and profile metadata.

These are descriptive measurements, not a disentangled speaker identity model.
"""
from __future__ import annotations
import numpy as np


def analyze_signal(audio: np.ndarray, sr: int) -> dict:
    x = np.asarray(audio, dtype=np.float32).reshape(-1)
    if sr <= 0 or x.size < int(sr * 0.25):
        raise ValueError("Audio must be at least 250 ms long")
    peak = float(np.max(np.abs(x)))
    rms = float(np.sqrt(np.mean(x * x)) + 1e-12)
    # Frame-based measurements avoid a single transient dominating the profile.
    frame = max(256, int(sr * 0.04))
    hop = max(128, int(sr * 0.02))
    centroids, zcr, energies = [], [], []
    window = np.hanning(frame).astype(np.float32)
    for start in range(0, max(1, x.size - frame + 1), hop):
        f = x[start:start + frame]
        if f.size < frame:
            break
        energies.append(float(np.sqrt(np.mean(f * f) + 1e-12)))
        zcr.append(float(np.mean(np.abs(np.diff(np.signbit(f).astype(np.int8))))))
        mag = np.abs(np.fft.rfft(f * window))
        freqs = np.fft.rfftfreq(frame, 1.0 / sr)
        total = float(mag.sum()) + 1e-12
        centroids.append(float(np.sum(freqs * mag) / total))
    return {
        "sample_rate": int(sr),
        "duration_seconds": round(x.size / sr, 3),
        "peak_dbfs": round(20.0 * np.log10(max(peak, 1e-9)), 2),
        "rms_dbfs": round(20.0 * np.log10(rms), 2),
        "spectral_centroid_hz_median": round(float(np.median(centroids)), 1) if centroids else None,
        "zero_crossing_rate_median": round(float(np.median(zcr)), 5) if zcr else None,
        "frame_rms_dbfs_median": round(20.0 * np.log10(max(float(np.median(energies)), 1e-9)), 2) if energies else None,
        "note": "Basic signal descriptors only; no formant, vocal-fold, or singer-identity claims.",
    }
