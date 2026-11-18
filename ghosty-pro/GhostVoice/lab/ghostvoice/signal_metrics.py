"""Conservative objective *signal* checks, not voice-identity proof.

Pure waveform metrics detect broken renders. Pitch checks use YIN as a rough
proxy for melody preservation and are not robust on every kind of singing.
"""
from __future__ import annotations
from pathlib import Path
import hashlib
import numpy as np
import soundfile as sf


def analyze_wav(path: str | Path, *, min_seconds: float = 1.5) -> dict:
    path = Path(path)
    if not path.is_file():
        raise FileNotFoundError(path)
    data, sr = sf.read(path, always_2d=True, dtype='float32')
    if data.size == 0 or sr < 8000:
        raise ValueError(f'Unreadable or invalid audio: {path}')
    length = data.shape[0] / sr
    if length < min_seconds:
        raise ValueError(f'Audio is only {length:.2f}s, needs {min_seconds:.2f}s: {path}')
    mono = data.mean(axis=1)
    if not np.isfinite(mono).all():
        raise ValueError(f'Nonfinite audio in {path}')
    rms = float(np.sqrt(np.mean(mono.astype(np.float64)**2)))
    peak = float(np.max(np.abs(data)))
    if rms < 1e-4:
        raise ValueError(f'Silent or near-silent audio: {path}')
    clipped_fraction = float(np.mean(np.max(np.abs(data), axis=1) >= 0.999))
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024*1024), b''):
            digest.update(block)
    return {
        'name': path.name,
        'sha256': digest.hexdigest(),
        'duration_seconds': round(length, 3),
        'sample_rate': int(sr),
        'channels': int(data.shape[1]),
        'peak': round(peak, 6),
        'rms': round(rms, 6),
        'clipped_fraction': round(clipped_fraction, 6),
        'warnings': (['potential_clipping'] if clipped_fraction > 0.001 else []),
    }


def pitch_similarity(source: str | Path, output: str | Path, *, max_seconds: float = 20.0) -> dict:
    """Basic same-timing F0 agreement with explicit confidence guardrails.

    Stereo downmixes and silent regions can distort pitch; this number is never
    a proxy for singer identity or musical quality. Only returns when enough
    frames are voiced and the two performances overlap in time.
    """
    import librosa
    from scipy.ndimage import median_filter

    sr = 16000
    f0_tracks = []
    durations = []
    for path in (source, output):
        signal, _ = librosa.load(str(path), sr=sr, mono=True, duration=max_seconds)
        durations.append(len(signal) / sr)
        if len(signal) < sr:
            return {'status': 'insufficient_audio'}
        hop = 320
        f0 = librosa.yin(signal, fmin=70, fmax=1100, sr=sr, frame_length=2048, hop_length=hop)
        energies = librosa.feature.rms(y=signal, frame_length=2048, hop_length=hop)[0]
        n = min(len(f0), len(energies))
        voiced = energies[:n] > max(0.006, float(np.quantile(energies[:n], 0.30)))
        f0_tracks.append((f0[:n], voiced))
    n = min(len(f0_tracks[0][0]), len(f0_tracks[1][0]))
    a, va = f0_tracks[0]
    b, vb = f0_tracks[1]
    mask = va[:n] & vb[:n] & (a[:n] >= 70) & (b[:n] >= 70)
    if int(mask.sum()) < 40:
        return {'status': 'insufficient_voiced_overlap', 'voiced_frames': int(mask.sum())}
    delta_cents = 1200 * np.log2(np.maximum(b[:n][mask], 1e-6) / np.maximum(a[:n][mask], 1e-6))
    return {
        'status': 'ok',
        'method': 'YIN energy-gated, time-aligned proxy; octave errors and timing offsets possible',
        'voiced_frames': int(mask.sum()),
        'median_absolute_cents': round(float(np.median(np.abs(delta_cents))), 2),
        'within_100_cents_fraction': round(float(np.mean(np.abs(delta_cents) <= 100)), 3),
        'source_seconds': round(durations[0], 3),
        'output_seconds': round(durations[1], 3),
    }
