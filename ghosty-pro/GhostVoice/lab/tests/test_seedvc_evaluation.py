import numpy as np
import soundfile as sf
import torch
from ghostvoice.seedvc_evaluation import evaluate_voice_c
from ghostvoice.seedvc_style import STYLE_DIM


class FakeCampPlus(torch.nn.Module):
    def __init__(self):
        super().__init__()
        self.proj = torch.nn.Linear(80, STYLE_DIM, bias=False)
        with torch.no_grad():
            torch.manual_seed(11)
            self.proj.weight.copy_(torch.randn_like(self.proj.weight))
    def forward(self, x):
        return self.proj(x.mean(dim=1))


def test_evaluator_reports_all_three_pairwise_scores(tmp_path):
    sr = 16000
    t = np.arange(sr * 3, dtype=np.float32) / sr
    files = {}
    for name, freq in [("a", 180), ("b", 280), ("c", 220)]:
        path = tmp_path / f"{name}.wav"
        sf.write(path, 0.1 * np.sin(2 * np.pi * freq * t), sr)
        files[name] = str(path)
    report = evaluate_voice_c([files["a"]], [files["b"]], [files["c"]], FakeCampPlus(), "cpu")
    assert set(report["cosine_similarity"]) == {"a_to_b", "a_to_c", "b_to_c"}
    assert report["nearest_source_in_embedding_space"] in {"A", "B", "equidistant"}
