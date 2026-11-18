"""Seed-VC-native style embedding extraction and two-reference fusion.

This module deliberately uses the exact CAMPPlus/fbank frontend used by the
Seed-VC V1 inference path. It produces a 192-D style tensor suitable for the
Seed-VC CFM style argument. It does NOT claim that linear interpolation is a
trained or perceptually guaranteed new identity; that must be tested by actual
singing conversion and listening.
"""
from __future__ import annotations

from pathlib import Path
from typing import Iterable, Sequence
import json
import numpy as np
import torch
import torch.nn.functional as F
import torchaudio

STYLE_DIM = 192
STYLE_SAMPLE_RATE = 16_000
MAX_REFERENCE_SECONDS = 25.0  # Seed-VC's stock inference truncates its reference to 25 s.
MIN_REFERENCE_SECONDS = 1.5


def _model_device(model: torch.nn.Module, fallback: str | torch.device = "cpu") -> torch.device:
    try:
        return next(model.parameters()).device
    except (StopIteration, AttributeError):
        return torch.device(fallback)


def _load_mono_16k(path: str | Path, device: str | torch.device) -> torch.Tensor:
    path = Path(path)
    if not path.is_file():
        raise FileNotFoundError(path)
    waveform, sample_rate = torchaudio.load(str(path))
    if waveform.ndim != 2 or waveform.shape[0] < 1:
        raise ValueError(f"Could not decode audio: {path}")
    waveform = waveform.mean(dim=0, keepdim=True).float()
    if sample_rate != STYLE_SAMPLE_RATE:
        waveform = torchaudio.functional.resample(waveform, sample_rate, STYLE_SAMPLE_RATE)
    waveform = waveform.to(device)
    if waveform.shape[-1] < int(MIN_REFERENCE_SECONDS * STYLE_SAMPLE_RATE):
        raise ValueError(f"Reference must be at least {MIN_REFERENCE_SECONDS:.1f} seconds: {path}")
    peak = waveform.abs().max().item()
    rms = waveform.square().mean().sqrt().item()
    if peak < 1e-5 or rms < 1e-4:
        raise ValueError(f"Reference is silent or nearly silent: {path}")
    # Match Seed-VC's reference window length. Do not loudness-normalize: its
    # original inference path does not do so before Kaldi fbank extraction.
    max_samples = int(MAX_REFERENCE_SECONDS * STYLE_SAMPLE_RATE)
    return waveform[:, :max_samples]


@torch.inference_mode()
def encode_campplus_reference(path: str | Path, campplus_model: torch.nn.Module,
                              device: str | torch.device | None = None) -> tuple[torch.Tensor, dict]:
    """Encode one reference using Seed-VC's own CAMPPlus frontend.

    The feature operations intentionally mirror upstream inference.py:
      torchaudio.compliance.kaldi.fbank(..., num_mel_bins=80, dither=0,
                                        sample_frequency=16000)
      feat = feat - feat.mean(dim=0, keepdim=True)
      campplus_model(feat.unsqueeze(0))
    """
    target_device = _model_device(campplus_model, device or "cpu") if device is None else torch.device(device)
    campplus_model.eval()
    waveform = _load_mono_16k(path, target_device)
    feat = torchaudio.compliance.kaldi.fbank(
        waveform,
        num_mel_bins=80,
        dither=0,
        sample_frequency=STYLE_SAMPLE_RATE,
    )
    if feat.ndim != 2 or feat.shape[-1] != 80 or feat.shape[0] < 2:
        raise RuntimeError(f"Unexpected Kaldi fbank shape for {path}: {tuple(feat.shape)}")
    feat = feat - feat.mean(dim=0, keepdim=True)
    embedding = campplus_model(feat.unsqueeze(0))
    if not isinstance(embedding, torch.Tensor):
        raise TypeError("CAMPPlus must return a torch.Tensor")
    if embedding.ndim == 1:
        embedding = embedding.unsqueeze(0)
    if embedding.shape != (1, STYLE_DIM):
        raise ValueError(f"Expected CAMPPlus output shape (1, {STYLE_DIM}), got {tuple(embedding.shape)}")
    if not torch.isfinite(embedding).all():
        raise ValueError(f"CAMPPlus produced non-finite values for {path}")
    diagnostics = {
        "file": str(path),
        "duration_seconds": round(waveform.shape[-1] / STYLE_SAMPLE_RATE, 3),
        "fbank_frames": int(feat.shape[0]),
        "embedding_norm": round(float(embedding.norm().item()), 6),
    }
    return embedding.detach().float(), diagnostics


def _cosine_matrix(vectors: torch.Tensor) -> torch.Tensor:
    normalized = F.normalize(vectors.float(), dim=-1, eps=1e-8)
    return normalized @ normalized.transpose(0, 1)


def enroll_campplus_identity(paths: Sequence[str | Path], campplus_model: torch.nn.Module,
                             device: str | torch.device | None = None) -> tuple[torch.Tensor, dict]:
    """Create one raw CAMPPlus identity style from multiple reference takes.

    Outlier rejection uses cosine centrality, but aggregation stays in the raw
    CAMPPlus output scale because Seed-VC feeds raw CAMPPlus outputs to its CFM.
    """
    paths = list(paths or [])
    if not paths:
        raise ValueError("Provide at least one reference WAV for each identity")
    vectors, per_file = [], []
    for path in paths:
        vector, diag = encode_campplus_reference(path, campplus_model, device)
        vectors.append(vector.squeeze(0))
        per_file.append(diag)
    matrix = torch.stack(vectors, dim=0)
    if len(vectors) == 1:
        keep = torch.ones(1, dtype=torch.bool, device=matrix.device)
        centrality = torch.ones(1, device=matrix.device)
    else:
        similarities = _cosine_matrix(matrix)
        centrality = similarities.median(dim=1).values
        medoid = int(torch.argmax(centrality).item())
        threshold = max(0.20, float(centrality[medoid].item()) - 0.25)
        keep = centrality >= threshold
        if not bool(keep.any()):
            keep[medoid] = True
    # Equal take weighting prevents a single long file dominating the profile.
    identity = matrix[keep].mean(dim=0, keepdim=True)
    if identity.shape != (1, STYLE_DIM):
        raise RuntimeError("Internal CAMPPlus identity shape error")
    report = {
        "encoder": "Seed-VC V1 CAMPPlus (192-D)",
        "frontend": "Kaldi fbank 80-bin, 16 kHz, dither=0, per-bin mean subtraction",
        "aggregation": "equal-weight raw embedding mean after cosine-centrality outlier guard",
        "reference_count": len(paths),
        "kept_reference_count": int(keep.sum().item()),
        "rejected_reference_count": int((~keep).sum().item()),
        "references": [
            {**diag, "kept": bool(keep[i].item()), "cosine_centrality": round(float(centrality[i].item()), 6)}
            for i, diag in enumerate(per_file)
        ],
        "raw_embedding_norm": round(float(identity.norm().item()), 6),
    }
    return identity, report


def blend_campplus_styles(style_a: torch.Tensor, style_b: torch.Tensor,
                          weight_b: float = 0.5) -> tuple[torch.Tensor, dict]:
    """Interpolate two Seed-VC-native CAMPPlus style vectors in raw model space.

    No L2 normalization is applied: upstream Seed-VC passes the raw CAMPPlus
    output to its style-conditioned CFM. This is a baseline interpolation, not
    a learned identity-fusion network.
    """
    if not 0.0 <= float(weight_b) <= 1.0:
        raise ValueError("weight_b must be between 0 and 1")
    a = torch.as_tensor(style_a).detach().float()
    b = torch.as_tensor(style_b, device=a.device).detach().float()
    if a.ndim == 1:
        a = a.unsqueeze(0)
    if b.ndim == 1:
        b = b.unsqueeze(0)
    if a.shape != (1, STYLE_DIM) or b.shape != (1, STYLE_DIM):
        raise ValueError(f"Both style vectors must have shape (1, {STYLE_DIM})")
    if not torch.isfinite(a).all() or not torch.isfinite(b).all():
        raise ValueError("Style vectors must contain only finite values")
    fused = (1.0 - float(weight_b)) * a + float(weight_b) * b
    cosine = float(F.cosine_similarity(a, b, dim=-1).item())
    cosine_a = float(F.cosine_similarity(a, fused, dim=-1).item())
    cosine_b = float(F.cosine_similarity(b, fused, dim=-1).item())
    report = {
        "method": "raw_linear_interpolation_in_seedvc_campplus_space",
        "weight_a": round(1.0 - float(weight_b), 4),
        "weight_b": round(float(weight_b), 4),
        "cosine_a_b": round(cosine, 6),
        "cosine_a_fused": round(cosine_a, 6),
        "cosine_b_fused": round(cosine_b, 6),
        "norm_a": round(float(a.norm().item()), 6),
        "norm_b": round(float(b.norm().item()), 6),
        "norm_fused": round(float(fused.norm().item()), 6),
        "warning": "Embedding-space distance is not proof of a perceptually new singer. Verify with singing inference and blinded listening.",
    }
    return fused, report


@torch.inference_mode()
def build_fused_style_condition(paths_a: Sequence[str | Path], paths_b: Sequence[str | Path],
                                campplus_model: torch.nn.Module,
                                device: str | torch.device | None = None,
                                weight_b: float = 0.5,
                                return_report: bool = False):
    """Return [1,192] fused style tensor ready for Seed-VC's CFM `style` input."""
    identity_a, report_a = enroll_campplus_identity(paths_a, campplus_model, device)
    identity_b, report_b = enroll_campplus_identity(paths_b, campplus_model, device)
    fused, fusion_report = blend_campplus_styles(identity_a, identity_b, weight_b)
    target_device = _model_device(campplus_model, device or "cpu") if device is None else torch.device(device)
    fused = fused.to(target_device)
    report = {
        "backend": "Seed-VC V1 SVC / CAMPPlus style-conditioning path",
        "voice_a": report_a,
        "voice_b": report_b,
        "fusion": fusion_report,
        "integration_note": "Pass this tensor as the `style` argument to model.cfm.inference. For a clean fusion experiment, remove prompt mel/content conditioning as in Seed-VC eval.py --remove-prompt.",
    }
    return (fused, report) if return_report else fused


def save_style_profile(path: str | Path, fused: torch.Tensor,
                        report: dict, identity_a: torch.Tensor | None = None,
                        identity_b: torch.Tensor | None = None) -> tuple[Path, Path]:
    """Save a model-native style vector and its human-readable diagnostics."""
    out = Path(path)
    if out.suffix:
        out = out.with_suffix("")
    out.parent.mkdir(parents=True, exist_ok=True)
    arrays = {"style_embedding": fused.detach().float().cpu().numpy(), "schema_version": np.int32(1)}
    if identity_a is not None:
        arrays["identity_a"] = identity_a.detach().float().cpu().numpy()
    if identity_b is not None:
        arrays["identity_b"] = identity_b.detach().float().cpu().numpy()
    np.savez_compressed(out.with_suffix(".npz"), **arrays)
    out.with_suffix(".json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    return out.with_suffix(".npz"), out.with_suffix(".json")


def load_seedvc_campplus(seed_vc_root: str | Path, checkpoint_path: str | Path,
                         device: str | torch.device = "cpu") -> torch.nn.Module:
    """Load the CAMPPlus class from a local Seed-VC checkout and its checkpoint.

    Seed-VC must be installed/cloned separately. This function does not vendor
    or redistribute Seed-VC's GPL-3.0 code or any pretrained weights.
    """
    import sys
    root = Path(seed_vc_root).resolve()
    checkpoint = Path(checkpoint_path).resolve()
    if not (root / "modules" / "campplus" / "DTDNN.py").is_file():
        raise FileNotFoundError(f"Seed-VC source tree not found under {root}")
    if not checkpoint.is_file():
        raise FileNotFoundError(checkpoint)
    root_str = str(root)
    if root_str not in sys.path:
        sys.path.insert(0, root_str)
    from modules.campplus.DTDNN import CAMPPlus
    model = CAMPPlus(feat_dim=80, embedding_size=STYLE_DIM)
    state = torch.load(str(checkpoint), map_location="cpu", weights_only=True)
    if isinstance(state, dict) and "state_dict" in state and isinstance(state["state_dict"], dict):
        state = state["state_dict"]
    model.load_state_dict(state)
    model.eval().to(torch.device(device))
    return model
