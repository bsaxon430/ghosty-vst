from pathlib import Path
import numpy as np
import soundfile as sf
import librosa

TARGET_SR = 16000

def load_audio(path: str | Path) -> tuple[np.ndarray, int]:
    path = Path(path)
    if not path.exists():
        raise FileNotFoundError(path)
    audio, sr = sf.read(str(path), always_2d=True, dtype="float32")
    if audio.size == 0:
        raise ValueError(f"Empty audio file: {path}")
    mono = np.mean(audio, axis=1)
    peak = float(np.max(np.abs(mono)))
    if peak < 1e-5:
        raise ValueError(f"Audio is silent or nearly silent: {path}")
    # Avoid clipping while preserving relative dynamics.
    if peak > 1.0:
        mono = mono / peak
    if sr != TARGET_SR:
        mono = librosa.resample(mono, orig_sr=sr, target_sr=TARGET_SR)
        sr = TARGET_SR
    return mono.astype(np.float32), sr

def audio_summary(audio: np.ndarray, sr: int) -> dict:
    rms = float(np.sqrt(np.mean(np.square(audio)) + 1e-12))
    duration = float(len(audio) / sr)
    return {
        "duration_seconds": round(duration, 3),
        "sample_rate": int(sr),
        "rms": round(rms, 6),
        "peak": round(float(np.max(np.abs(audio))), 6),
    }
