#!/usr/bin/env python3
"""Reference transcribers for the Ghost Hum benchmark (run outside the plugin; nothing here ships).

  refs.py basicpitch <wav> <out.json>     Spotify Basic Pitch 0.4.0 (Apache-2.0 code + weights), ONNX model, default thresholds
  refs.py pyin       <wav> <out.json>     librosa.pyin (ISC), f0 frames; notes from a plain median segmenter (frames only are fair)
  refs.py crepe      <wav> <out.json>     torchcrepe "full" (or CREPE_MODEL=tiny; MIT; weights MIT, in the package), 10 ms hop, Viterbi decode

Output format = HumBench's: {"notes": [[t, dur, midi, cents, vel]...], "frames": [[t, f0_voiced, f0_raw, flag]...], "ms": ...}.
"""
import sys, os, json, time
import numpy as np
import soundfile as sf

def load(path, sr=None):
    x, fs = sf.read(path, dtype="float32", always_2d=True); x = x.mean(axis=1)
    if sr and fs != sr:
        import librosa; x = librosa.resample(x, orig_sr=fs, target_sr=sr); fs = sr
    return x, fs

def hz_to_midi(f):
    return 69.0 + 12.0 * np.log2(np.maximum(f, 1e-9) / 440.0)

def segment_f0(times, f0, voiced, min_len=0.06, split_cents=80.0):
    """A plain note segmenter for a frame-level tracker: voiced runs, split where the median pitch jumps > split_cents; pitch = median."""
    notes = []; i = 0; n = len(times)
    while i < n:
        if not voiced[i]: i += 1; continue
        j = i
        while j < n and voiced[j]: j += 1
        seg = np.arange(i, j); i = j
        # split by pitch jumps between short medians
        cuts = [seg[0]]; k = seg[0] + 3
        while k < seg[-1] - 3:
            a = np.median(hz_to_midi(f0[k - 3:k])); b = np.median(hz_to_midi(f0[k:k + 3]))
            if abs(a - b) * 100 > split_cents and k - cuts[-1] >= 3: cuts.append(k); k += 3
            else: k += 1
        cuts.append(seg[-1] + 1)
        for a, b in zip(cuts[:-1], cuts[1:]):
            dur = times[min(b, n - 1)] - times[a]
            if dur < min_len: continue
            m = np.median(hz_to_midi(f0[a:b])); midi = int(round(m))
            notes.append([float(times[a]), float(dur), midi, float((m - midi) * 100), 0.8])
    return notes

def run_basicpitch(wav):
    from basic_pitch.inference import predict
    from basic_pitch import ICASSP_2022_MODEL_PATH
    from basic_pitch.constants import AUDIO_SAMPLE_RATE, FFT_HOP, ANNOTATIONS_BASE_FREQUENCY, CONTOURS_BINS_PER_SEMITONE
    t0 = time.time()
    model_output, midi, note_events = predict(wav, ICASSP_2022_MODEL_PATH)
    ms = (time.time() - t0) * 1000
    notes = []
    for start, end, pitch, amp, bends in note_events:
        cents = 0.0
        if bends:   # Basic Pitch's bends: contour bins (1/3 semitone) relative to the note, per frame; the median is the note's lean
            cents = float(np.median(bends)) * (100.0 / CONTOURS_BINS_PER_SEMITONE)
        notes.append([float(start), float(end - start), int(pitch), cents, float(amp)])
    notes.sort()
    contour = model_output["contour"]  # (frames, 264)
    hop = FFT_HOP / AUDIO_SAMPLE_RATE
    frames = []
    for i in range(contour.shape[0]):
        b = int(np.argmax(contour[i])); p = float(contour[i, b])
        f = ANNOTATIONS_BASE_FREQUENCY * 2 ** (b / (12.0 * CONTOURS_BINS_PER_SEMITONE))
        frames.append([round(i * hop, 4), round(f if p > 0.3 else 0.0, 2), round(f, 2), 1 if p > 0.3 else 0])
    return {"notes": notes, "frames": frames, "ms": ms, "tool": "basic-pitch 0.4.0 onnx"}

def run_pyin(wav):
    import librosa
    x, fs = load(wav, 22050); t0 = time.time()
    f0, vflag, vprob = librosa.pyin(x, fmin=60, fmax=1200, sr=fs, frame_length=2048, hop_length=256)
    ms = (time.time() - t0) * 1000
    times = librosa.times_like(f0, sr=fs, hop_length=256)
    f0 = np.nan_to_num(f0, nan=0.0); voiced = vflag.astype(bool)
    frames = [[round(float(t), 4), round(float(f if v else 0.0), 2), round(float(f), 2), int(v)] for t, f, v in zip(times, f0, voiced)]
    return {"notes": segment_f0(times, f0, voiced), "frames": frames, "ms": ms, "tool": "librosa.pyin " + librosa.__version__}

def run_crepe(wav):
    import torch, torchcrepe
    x, fs = load(wav, 16000); t0 = time.time()
    audio = torch.tensor(x)[None]
    hop = 160
    model = os.environ.get("CREPE_MODEL", "full")
    f0, per = torchcrepe.predict(audio, fs, hop, 60.0, 1200.0, model, return_periodicity=True, batch_size=512, device="cpu", decoder=torchcrepe.decode.viterbi)
    per = torchcrepe.filter.median(per, 3); f0 = torchcrepe.filter.mean(f0, 3)
    ms = (time.time() - t0) * 1000
    f0 = f0[0].numpy(); per = per[0].numpy(); times = np.arange(len(f0)) * hop / fs
    voiced = per > 0.21   # torchcrepe's documented default-ish threshold for voicing
    frames = [[round(float(t), 4), round(float(f if v else 0.0), 2), round(float(f), 2), int(v)] for t, f, v in zip(times, f0, voiced)]
    return {"notes": segment_f0(times, f0, voiced), "frames": frames, "ms": ms, "tool": "torchcrepe " + model}

if __name__ == "__main__":
    which, wav, out = sys.argv[1], sys.argv[2], sys.argv[3]
    r = {"basicpitch": run_basicpitch, "pyin": run_pyin, "crepe": run_crepe}[which](wav)
    with open(out, "w") as f: json.dump(r, f)
