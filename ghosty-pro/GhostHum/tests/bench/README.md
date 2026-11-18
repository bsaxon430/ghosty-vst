# Ghost Hum benchmark (build 4)

Real recordings with ground truth, kept OUTSIDE the repo (both CC BY 4.0; only numbers learned from AVP ship, in HumBeatNetWeights.h):

- **vocadito** (Bittner et al. 2021, https://zenodo.org/record/5578807): 40 solo singing excerpts, F0 + notes by two annotators. Melody.
- **AVP, Amateur Vocal Percussion** (Delgado et al. 2019, https://zenodo.org/record/5036529): 28 amateur beatboxers, kick / snare /
  closed hat / open hat, each sound alone and an improvisation, "Personal" (their own sounds) and "Fixed" (given sounds) takes. Beatbox.

Reference transcribers (benchmark only, nothing ships): Spotify Basic Pitch 0.4.0 (Apache-2.0 code + weights, ONNX), librosa pYIN
(ISC), torchcrepe (MIT code + weights). Omnizart was not run (TensorFlow, and its vocal checkpoints are trained on datasets without a
commercial licence, so it could not ship in Ghost Hum anyway).

## Run

```
HumBench melody <wav> <out.json> old|new          # one take, as heard (ORIGINAL pitch), plus the f0 / pitch line
HumBench beat   <wav> <out.json> old|new [net.bin] # hits; net.bin = a held-out fold net from train_beat.py
HumBench beatteach <improv.wav> <out.json> <kick.wav> <snare.wav> <hhc.wav> <hho.wav> [net.bin] [old|new]   # TEACH from real takes
HumBench synthmel|synthbeat <count> <seed>        # our synthesised singers / beatboxers, both engines, signed timing
HumBench beatfeat <wav> <labels.csv> <out.bin>    # training rows for the beatbox net (AVP)
HumBench synthfeat <count> <seed> <out.bin>       # training rows from synthesised mouths (claps, mouth noise, leaked clicks)
HumBench render <wav> <prefix> melody|beat old|new [original|nearest]   # MIDI + WAV of what an engine heard
run.sh <system> <out_root> <vocadito> <avp> [HumBench] [python]          # all files; systems old | new | basicpitch | pyin | crepe
train_beat.py <feat_dir> <out_dir> [--epochs 40] [--synth-x 2]           # 4-fold by participant + the shipped net
score.py melody <vocadito> <system_dir>...  /  score.py beat <avp> <system_dir>...
```

Every AVP participant is scored by a net trained without them (the four `fold<k>.bin`, participant n in fold (n - 1) % 4).
