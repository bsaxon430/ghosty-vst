from __future__ import annotations
import numpy as np
import torch

class SpeakerEncoder:
    """SpeechBrain ECAPA baseline encoder.

    Note: this checkpoint is speech-trained. Singing may reduce identity accuracy;
    this module is a measurable baseline, not the final singing identity encoder.
    """
    def __init__(self, device: str | None = None):
        from speechbrain.inference.speaker import EncoderClassifier
        if device is None:
            device = "cuda" if torch.cuda.is_available() else "cpu"
        self.device = device
        self.model = EncoderClassifier.from_hparams(
            source="speechbrain/spkrec-ecapa-voxceleb",
            savedir="pretrained_models/spkrec-ecapa-voxceleb",
            run_opts={"device": device},
        )

    @torch.inference_mode()
    def encode(self, audio: np.ndarray, sr: int = 16000) -> np.ndarray:
        if sr != 16000:
            raise ValueError("SpeakerEncoder expects 16 kHz audio.")
        wav = torch.tensor(audio, dtype=torch.float32, device=self.device).unsqueeze(0)
        lengths = torch.ones(1, device=self.device)
        emb = self.model.encode_batch(wav, lengths=lengths)
        vec = emb.squeeze().detach().float().cpu().numpy().astype(np.float32)
        return normalize(vec)

def normalize(v: np.ndarray, eps: float = 1e-12) -> np.ndarray:
    v = np.asarray(v, dtype=np.float32).reshape(-1)
    n = float(np.linalg.norm(v))
    if n < eps:
        raise ValueError("Cannot normalize a zero-length embedding.")
    return v / n

def cosine(a: np.ndarray, b: np.ndarray) -> float:
    a, b = normalize(a), normalize(b)
    return float(np.clip(np.dot(a, b), -1.0, 1.0))

def slerp(a: np.ndarray, b: np.ndarray, weight_b: float) -> np.ndarray:
    """Spherical interpolation between two normalized embeddings.

    weight_b=0 returns A; weight_b=1 returns B.
    """
    if not 0.0 <= weight_b <= 1.0:
        raise ValueError("weight_b must be between 0 and 1.")
    a, b = normalize(a), normalize(b)
    dot = float(np.clip(np.dot(a, b), -1.0, 1.0))
    # For nearly parallel vectors, normalized linear interpolation is stable.
    if dot > 0.9995:
        return normalize((1.0 - weight_b) * a + weight_b * b)
    # Handle near-antipodal vectors with a stable orthogonal direction.
    if dot < -0.9995:
        basis = np.zeros_like(a)
        basis[int(np.argmin(np.abs(a)))] = 1.0
        orth = normalize(basis - np.dot(basis, a) * a)
        theta = np.pi * weight_b
        return normalize(np.cos(theta) * a + np.sin(theta) * orth)
    theta = np.arccos(dot)
    sin_theta = np.sin(theta)
    out = (
        np.sin((1.0 - weight_b) * theta) / sin_theta * a
        + np.sin(weight_b * theta) / sin_theta * b
    )
    return normalize(out)
