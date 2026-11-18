import numpy as np
import soundfile as sf
import pytest
from ghostvoice.enrollment import segment_audio, enroll_references


class FakeEncoder:
    def encode(self, audio, sr):
        # Deterministic nonzero vector derived from the segment.
        return np.array([1.0, float(np.mean(audio)) + 0.1, 0.25], dtype=np.float32)


def test_segment_audio_overlap_and_short_clip():
    x = np.ones(8 * 16000, dtype=np.float32) * 0.05
    chunks = segment_audio(x, 16000, window_seconds=3, overlap_seconds=0.5)
    assert len(chunks) >= 3
    assert all(len(c) >= 1.5 * 16000 for c in chunks)


def test_rejects_too_short_audio():
    with pytest.raises(ValueError):
        segment_audio(np.ones(1000, dtype=np.float32), 16000)


def test_multi_reference_enrollment(tmp_path):
    paths = []
    for i, amp in enumerate((0.04, 0.05)):
        path = tmp_path / f"take{i}.wav"
        sf.write(path, np.ones(5 * 16000, dtype=np.float32) * amp, 16000)
        paths.append(str(path))

    def loader(path):
        audio, sr = sf.read(path, dtype="float32")
        return audio, sr

    identity, report = enroll_references(paths, FakeEncoder(), loader)
    assert identity.shape == (3,)
    assert np.isclose(np.linalg.norm(identity), 1.0)
    assert report["reference_files"] == 2
    assert report["segments_kept"] >= 2


def test_all_silent_references_rejected(tmp_path):
    path = tmp_path / "silent.wav"
    sf.write(path, np.zeros(3 * 16000, dtype=np.float32), 16000)
    def loader(p):
        audio, sr = sf.read(p, dtype="float32")
        return audio, sr
    with pytest.raises(ValueError, match="No usable"):
        enroll_references([str(path)], FakeEncoder(), loader)
