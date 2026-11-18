# Ghost Voice CHOP tools (AGPL-3.0-only, like Ghost Voice; see ../NOTICE.md). Build 9.
#   chop_tools.py phrases WAV OUT.json   -> PHRASES: OpenVPI's audio-slicer (slicer2.py, MIT, untouched copy in
#                                           third_party/openvpi-audio-slicer) splits a vocal at its silences
#   chop_tools.py words WAV OUT.json     -> WORDS: WhisperX (m-bain/whisperX 3.3.1, BSD-2) finds every word and when it starts / ends
#   chop_tools.py status                 -> which of the two can run here
# OUT.json: {"ok": true, "chops": [{"a": seconds, "b": seconds, "text": "..."}], "language": ".."} or {"ok": false, "error": ".."}
# The WAV is read with Python's own wave module (PCM 16 / 24 / 32-bit); nothing else touches the sound.
import json
import os
import sys
import wave

HERE = os.path.dirname(os.path.abspath(__file__))


def read_wav(path):
    import numpy as np
    with wave.open(path, "rb") as w:
        ch, width, sr, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    if width == 3:
        b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3)
        x = (b[:, 0].astype(np.int32) | (b[:, 1].astype(np.int32) << 8) | (b[:, 2].astype(np.int32) << 16))
        x = np.where(x >= 1 << 23, x - (1 << 24), x).astype(np.float32) / float(1 << 23)
    elif width == 2:
        x = np.frombuffer(raw, dtype="<i2").astype(np.float32) / 32768.0
    elif width == 4:
        x = np.frombuffer(raw, dtype="<i4").astype(np.float32) / float(1 << 31)
    else:
        raise ValueError("unsupported WAV sample size %d" % width)
    x = x.reshape(-1, ch).mean(axis=1) if ch > 1 else x
    return np.ascontiguousarray(x, dtype=np.float32), sr


def phrases(path, db=-40.0, min_length=500, min_interval=150, hop=10, max_sil_kept=200):
    sys.path.insert(0, os.path.join(HERE, "third_party", "openvpi-audio-slicer"))
    from slicer2 import Slicer  # OpenVPI, untouched
    x, sr = read_wav(path)
    s = Slicer(sr=sr, threshold=db, min_length=min_length, min_interval=min_interval, hop_size=hop, max_sil_kept=max_sil_kept)
    chunks = s.slice(x)
    base = x.__array_interface__["data"][0]
    out = []
    for c in chunks:  # every chunk is a view into x: where it starts is where its memory starts
        a = (c.__array_interface__["data"][0] - base) // x.itemsize
        out.append({"a": a / sr, "b": (a + c.shape[0]) / sr, "text": ""})
    return {"ok": True, "chops": out}


def words(path, model_name="small", language=None):
    import numpy as np
    import torch
    import torchaudio
    import whisperx
    x, sr = read_wav(path)
    audio = torchaudio.functional.resample(torch.from_numpy(x), sr, 16000).numpy().astype(np.float32)
    model = whisperx.load_model(model_name, "cpu", compute_type="int8", language=language)
    result = model.transcribe(audio, batch_size=4)
    lang = result.get("language", language or "en")
    align_model, meta = whisperx.load_align_model(language_code=lang, device="cpu")
    aligned = whisperx.align(result["segments"], align_model, meta, audio, "cpu", return_char_alignments=False)
    out = []
    for w in aligned.get("word_segments", []):
        if "start" in w and "end" in w and w["end"] > w["start"]:
            out.append({"a": float(w["start"]), "b": float(w["end"]), "text": str(w.get("word", "")).strip()})
    return {"ok": True, "chops": out, "language": lang}


def status():
    have = {}
    try:
        import numpy  # noqa: F401
        have["phrases"] = True
    except Exception:
        have["phrases"] = False
    try:
        import whisperx  # noqa: F401
        have["words"] = True
    except Exception:
        have["words"] = False
    return have


def main():
    if len(sys.argv) >= 2 and sys.argv[1] == "status":
        print(json.dumps(status()))
        return 0
    if len(sys.argv) < 4 or sys.argv[1] not in ("phrases", "words"):
        print(__doc__ or "usage: chop_tools.py phrases|words WAV OUT.json")
        return 2
    cmd, wav, out = sys.argv[1], sys.argv[2], sys.argv[3]
    try:
        print("GV_STEP 1/2 listening", flush=True)
        r = phrases(wav) if cmd == "phrases" else words(wav, os.environ.get("GHOSTVOICE_WORDS_MODEL", "small"),
                                                          os.environ.get("GHOSTVOICE_WORDS_LANGUAGE") or None)
    except Exception as e:  # noqa: BLE001
        r = {"ok": False, "error": "%s: %s" % (type(e).__name__, e)}
    with open(out, "w") as f:
        json.dump(r, f)
    print("GV_STEP 2/2 done", flush=True)
    print("GV_DONE" if r.get("ok") else "GV_FAILED " + r.get("error", ""), flush=True)
    return 0 if r.get("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
