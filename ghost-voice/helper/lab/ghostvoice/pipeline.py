from __future__ import annotations
from pathlib import Path
import json
import numpy as np
from .audio import load_audio, audio_summary
from .embedding import SpeakerEncoder
from .fusion import fuse_identity, diagnostics
from .traits import analyze_signal
from .enrollment import enroll_references


def build_identity(path_a, path_b, mix_b: float, output_prefix: str,
                   encoder: SpeakerEncoder | None = None, novelty: float = 0.0,
                   seed: str = "ghost-voice") -> dict:
    if not 0.0 <= mix_b <= 1.0:
        raise ValueError("Mix must be between 0 and 1.")
    if not 0.0 <= novelty <= 0.5:
        raise ValueError("Novelty must be between 0 and 0.5.")
    paths_a = [path_a] if isinstance(path_a, (str, bytes)) else list(path_a)
    paths_b = [path_b] if isinstance(path_b, (str, bytes)) else list(path_b)
    if encoder is None:
        encoder = SpeakerEncoder()
    emb_a, enrollment_a = enroll_references(paths_a, encoder, load_audio)
    emb_b, enrollment_b = enroll_references(paths_b, encoder, load_audio)
    a_audio, sr_a = load_audio(paths_a[0])
    b_audio, sr_b = load_audio(paths_b[0])
    emb_c = fuse_identity(emb_a, emb_b, mix_b, novelty, seed)

    out = Path(output_prefix)
    out.parent.mkdir(parents=True, exist_ok=True)
    npz_path = out.with_suffix(".npz")
    json_path = out.with_suffix(".json")
    np.savez_compressed(
        npz_path,
        identity=emb_c,
        source_a_embedding=emb_a,
        source_b_embedding=emb_b,
        mix_b=np.float32(mix_b),
        novelty=np.float32(novelty),
        schema_version=np.int32(2),
    )

    report = {
        "product": "Ghost Voice Identity Lab",
        "prototype_version": "0.3.0",
        "status": "identity_latent_experiment_no_audio_renderer",
        "mix": {"vocal_a": round(1.0 - mix_b, 4), "vocal_b": round(mix_b, 4)},
        "novelty_probe": {
            "amount": round(novelty, 4),
            "seed": seed,
            "warning": "Orthogonal latent displacement is an experiment, not a learned new-singer direction.",
        },
        "encoder": "SpeechBrain ECAPA-TDNN / spkrec-ecapa-voxceleb",
        "encoder_domain_warning": "Speech-trained baseline; singing identity quality is unvalidated.",
        "multi_reference_enrollment": {"vocal_a": enrollment_a, "vocal_b": enrollment_b},
        "diagnostics": diagnostics(emb_a, emb_b, emb_c),
        "inputs": {
            "vocal_a": {"file": str(path_a), **audio_summary(a_audio, sr_a), "signal_traits": analyze_signal(a_audio, sr_a)},
            "vocal_b": {"file": str(path_b), **audio_summary(b_audio, sr_b), "signal_traits": analyze_signal(b_audio, sr_b)},
        },
        "outputs": {"identity_profile": str(npz_path), "report": str(json_path)},
        "next_step": "Inspect SoulX-Singer/SVC conditioning and build a model-specific singing-identity adapter; this ECAPA vector cannot be passed to it as-is.",
    }
    json_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    return report
