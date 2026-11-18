import numpy as np
import pytest
import soundfile as sf
import torch

from ghostvoice.seedvc_style import (
    STYLE_DIM,
    blend_campplus_styles,
    encode_campplus_reference,
    build_fused_style_condition,
)


class FakeCampPlus(torch.nn.Module):
    def __init__(self):
        super().__init__()
        self.proj = torch.nn.Linear(80, STYLE_DIM, bias=False)
        with torch.no_grad():
            torch.manual_seed(7)
            self.proj.weight.copy_(torch.randn_like(self.proj.weight) * 0.01)

    def forward(self, features):
        return self.proj(features.mean(dim=1))


def test_raw_blend_endpoints_and_midpoint():
    a = torch.linspace(-1, 1, STYLE_DIM).unsqueeze(0)
    b = torch.linspace(1, -1, STYLE_DIM).unsqueeze(0)
    c0, _ = blend_campplus_styles(a, b, 0.0)
    c1, _ = blend_campplus_styles(a, b, 1.0)
    cm, report = blend_campplus_styles(a, b, 0.5)
    assert torch.allclose(c0, a)
    assert torch.allclose(c1, b)
    assert torch.allclose(cm, (a + b) * 0.5)
    assert report["method"] == "raw_linear_interpolation_in_seedvc_campplus_space"


def test_blend_rejects_bad_shape_and_weight():
    with pytest.raises(ValueError):
        blend_campplus_styles(torch.ones(1, 10), torch.ones(1, 10), 0.5)
    with pytest.raises(ValueError):
        blend_campplus_styles(torch.ones(1, STYLE_DIM), torch.ones(1, STYLE_DIM), 1.1)


def test_exact_frontend_returns_seedvc_style_shape(tmp_path):
    sr = 22050
    t = np.arange(sr * 3, dtype=np.float32) / sr
    wav = 0.12 * np.sin(2 * np.pi * 220 * t)
    path = tmp_path / "voice.wav"
    sf.write(path, wav, sr)
    model = FakeCampPlus()
    embedding, report = encode_campplus_reference(path, model, "cpu")
    assert embedding.shape == (1, STYLE_DIM)
    assert torch.isfinite(embedding).all()
    assert report["fbank_frames"] > 0


def test_build_fused_style_condition_multiple_refs(tmp_path):
    sr = 16000
    t = np.arange(sr * 3, dtype=np.float32) / sr
    paths_a, paths_b = [], []
    for i, freq in enumerate((180, 190)):
        path = tmp_path / f"a{i}.wav"
        sf.write(path, 0.1 * np.sin(2 * np.pi * freq * t), sr)
        paths_a.append(str(path))
    for i, freq in enumerate((260, 280)):
        path = tmp_path / f"b{i}.wav"
        sf.write(path, 0.1 * np.sin(2 * np.pi * freq * t), sr)
        paths_b.append(str(path))
    model = FakeCampPlus()
    fused, report = build_fused_style_condition(paths_a, paths_b, model, "cpu", 0.5, True)
    assert fused.shape == (1, STYLE_DIM)
    assert report["voice_a"]["reference_count"] == 2
    assert report["voice_b"]["reference_count"] == 2
    assert report["fusion"]["weight_b"] == 0.5
