# Ghost Voice Identity Lab — v0.6

> **STATUS: Experimental, NOT finished or audio-quality validated.** v0.6 adds an end-to-end render/verification harness and a local studio interface. A working Seed-VC GPU installation is still needed to hear a Voice C. No pretrained model is bundled and no actual voice C has been produced in this environment.

## What v0.6 actually does

- Checks Voice A, Voice B and source performance WAVs for bad input, clipping, duplication and sample rates.
- Builds **A anchor**, **B anchor** and **fused-C variants**, all sung using the *same source performance* and the same model settings. The anchor tests are essential: otherwise changes in lyrics/melody could be mistaken for voice identity differences.
- Launches actual patched Seed-VC **singing inference**, captures logs and verifies output WAV existence, duration and signal health.
- Produces a reproducible experiment manifest and an offline randomized, blind listening page, with the answer key **separate** from shared clips.
- Adds an optional YIN-based melody-preservation proxy (not singer identity testing).
- Includes a **Ghost Voice Studio** Gradio interface with one experimental CREATE & RENDER VOICE C action.
- Includes a downloader for the selected five VocalSet WAV examples (the selected mirror URLs must be reachable; they weren't downloadable in this sandbox).

### Quick start on a computer with a supported Seed-VC installation

1. Install Seed-VC's pinned dependencies, model checkpoints and singing voice conversion backend from the upstream research repository: https://github.com/Plachtaa/seed-vc
2. Use a **separate research clone**, then run `python patches/apply_seedvc_fusion_patch.py /path/to/seed-vc` once. This backs up upstream `inference.py`. You need this project's `ghostvoice/` Python package available in the Seed-VC environment (the runner sets PYTHONPATH).
3. Use the five selected VocalSet research WAVs (or permitted recordings):

```bash
python scripts/download_vocalset_test.py --output ./vocalset
```

4. Verify the experiment plan first:

```bash
python -m ghostvoice.benchmark_cli \
  --a "vocalset/F1 caro mio ben.wav" --a "vocalset/F1 scale.wav" \
  --b "vocalset/F2 caro mio ben.wav" --b "vocalset/F2 scale.wav" \
  --source "vocalset/F3 caro mio ben.wav" \
  --seed-vc-root /path/to/seed-vc --output ./outputs/run1 \
  --rights-confirmed --dry-run
```

5. Run the **real model** by removing `--dry-run` (ideally on an NVIDIA GPU). This generates five WAVs: A anchor, B anchor, C at 25%, C at 50%, and C at 75% B. The benchmark writes `experiment.json`, individual logs and `blind_listening/index.html`. Different weights are **candidates**, not proven distinct singers.

Alternatively run `python ghost_voice_studio.py` for the local Gradio UI (needs the same installed backend).

## Need to understand

- Current fusion is a raw CAMPPlus embedding interpolation, **not a learned new-identity generator**. A successful render is not proof of novel singing identity.
- The voice generation needs a full Seed-VC SVC model + vocoder + checkpoints; the CAMPPlus-only profile tool does not generate audio.
- The full pretrained model **has not been executed or auditioned here**. This environment has CPU-only PyTorch and cannot retrieve the large pretrained checkpoints.
- Keep identity references and performance different. A and B render anchors are designed to match the source performance, making the comparison fairer.
- VocalSet is CC BY 4.0. Credit Wilkins, Seetharaman, Wahl and Pardo, *VocalSet: A Singing Voice Dataset* (ISMIR 2018). That doesn't automatically grant rights to sell commercial clones of a participant's voice.
- Seed-VC's upstream code is GPL-3.0. This is a **research adapter**, not a cleared closed-source commercial plugin. Verify every relevant weight/dependency license before distribution.

---

## v0.5 renderer-compatible fusion details

A research prototype for creating a new singing-voice identity from two permitted vocal references. It is separate from Ghost Voice Classic.

## The important v0.5 change

The earlier prototype extracted SpeechBrain ECAPA speech embeddings. Those vectors are **not** compatible with a singing renderer. v0.5 adds a second, model-native path based on the 192-dimensional CAMPPlus style embedding used directly by Seed-VC V1.

Seed-VC's public SVC inference code extracts CAMPPlus from 80-bin Kaldi filterbanks at 16 kHz and passes the resulting `style` vector separately to its conditional-flow-matching generator. The upstream evaluation code also contains a `remove_prompt` mode that removes the reference mel/content prompt while retaining the style vector. Ghost Voice now has:

- Exact Seed-VC-compatible CAMPPlus/fbank reference encoding.
- Multi-take enrollment for Voice A and Voice B.
- Cosine-centrality outlier rejection across takes.
- Raw-space interpolation of A/B vectors in the **same 192-D CAMPPlus space** that Seed-VC consumes. The vector is deliberately not L2-normalized because Seed-VC uses the raw CAMPPlus output.
- A bridge function that returns a `[1, 192]` tensor for `model.cfm.inference(..., style, ...)`.
- A guarded patcher for Seed-VC `inference.py` that adds two-reference inputs and a promptless fused-style experiment.
- Tests for feature extraction, interpolation, multi-reference enrollment and patch generation.

## Honest status

**This is the first renderer-compatible identity-fusion experiment, not a finished or proven new singer.** The style vector now comes from the same CAMPPlus encoder used by Seed-VC, unlike the unrelated ECAPA profile. However, linear interpolation is still a baseline, not a learned fusion network. A new-sounding identity is not guaranteed. The actual Seed-VC SVC model must be run on a GPU with real permitted vocals, and the result must pass blinded listening and independent identity/content/pitch tests.

No pretrained model weights are included. We did not run full neural inference in this environment. The automated tests use a deterministic fake CAMPPlus network for code-path verification; they do not measure singing quality.

## v0.5 install

Use Python 3.10 or 3.11 for the Seed-VC environment. Install Seed-VC according to its own instructions, including its matched `torch`/`torchaudio` versions. In the Ghost Voice project environment:

```bash
pip install -r requirements.txt
pip install -r requirements-seedvc.txt
```

Seed-VC checkout: https://github.com/Plachtaa/seed-vc

The CAMPPlus checkpoint used by Seed-VC is `funasr/campplus` / `campplus_cn_common.bin`. Download it from the official Hugging Face model repository and keep it outside this source archive.

## Optional Gradio profile builder

```bash
python seedvc_app.py
```

Open the local Gradio page, upload multiple takes for A and B, point it to the Seed-VC checkout and CAMPPlus checkpoint, and create the model-native profile. This still creates a profile only; rendering is done by the patched Seed-VC inference path below.

## Build a model-native Voice C style profile

```bash
python -m ghostvoice.seedvc_cli \
  --seed-vc-root /path/to/seed-vc \
  --campplus-checkpoint /path/to/campplus_cn_common.bin \
  --a voice_a_take1.wav --a voice_a_take2.wav \
  --b voice_b_take1.wav --b voice_b_take2.wav \
  --mix-b 0.50 \
  --device cuda \
  --out outputs/ghost_voice_c_seedvc
```

This writes:
- `outputs/ghost_voice_c_seedvc.npz` — the fused 192-D style vector and source vectors.
- `outputs/ghost_voice_c_seedvc.json` — enrollment and fusion diagnostics.

The profile is a Seed-VC-native conditioning vector. It is not an audio file.

## Patch Seed-VC to run the singing experiment

1. Make a separate research clone of Seed-VC.
2. Copy this project's `ghostvoice/` folder into the Seed-VC repository root (or make this project importable from that environment).
3. From this project, run:

```bash
python patches/apply_seedvc_fusion_patch.py /path/to/seed-vc
```

The patcher checks exact source anchors, refuses to patch twice, and creates `inference.py.ghostvoice-backup` before editing. If the upstream file has changed, it aborts rather than guessing.

Then run Seed-VC's **singing** model (`--f0-condition true`) with the source performance and both identity reference sets:

```bash
cd /path/to/seed-vc
python inference.py \
  --source /path/to/target_singing_performance.wav \
  --target /path/to/voice_a_take1.wav \
  --output /path/to/ghost_voice_output \
  --f0-condition true \
  --identity-a /path/to/voice_a_take1.wav \
  --identity-a /path/to/voice_a_take2.wav \
  --identity-b /path/to/voice_b_take1.wav \
  --identity-b /path/to/voice_b_take2.wav \
  --identity-mix-b 0.50 \
  --fused-identity-no-prompt true \
  --diffusion-steps 40 \
  --fp16 true
```

`--target` is still read by the upstream script to initialize its normal reference path, but the fused-identity mode removes that reference mel prompt before CFM generation by default. This is intended to reduce A/B anchor leakage and test the fused CAMPPlus style vector on its own. The promptless path exists in Seed-VC's upstream evaluation code; its perceptual quality with the F0-conditioned SVC checkpoint has **not** been validated yet. If it sounds weak or unstable, the next task is model fine-tuning with a proper multi-singer singing dataset, not increasing the interpolation slider.

The patched `inference.py` prints diagnostics for the A/B fusion. It writes converted audio through the normal Seed-VC output path.

## Evaluate a rendered Voice C

After generating one or more C WAVs, run the same-backend diagnostic:

```bash
python -m ghostvoice.seedvc_eval_cli \
  --seed-vc-root /path/to/seed-vc \
  --campplus-checkpoint /path/to/campplus_cn_common.bin \
  --a voice_a_take1.wav --a voice_a_take2.wav \
  --b voice_b_take1.wav --b voice_b_take2.wav \
  --c /path/to/rendered_voice_c.wav \
  --device cuda \
  --out outputs/voice_c_evaluation.json
```

This flags obvious source dominance in CAMPPlus space. It is not independent proof of a new singer; the same encoder family is used by the renderer.

## What to evaluate

For each A/B pair, render the same held-out target performance at several mixes (e.g. 0.25, 0.50, 0.75). Keep the same source performance and inference seed/settings. Compare:

1. Voice C vs A and Voice C vs B using a separate singing-capable identity evaluator (not only the same CAMPPlus encoder used to condition the model).
2. Melody/F0 preservation against the source performance.
3. Lyric/content intelligibility.
4. Naturalness, consonants, vibrato, breath, register transitions and artifact rate.
5. Whether C stays stable on a second song and a different register.
6. Blinded human judgments: listeners must identify C as distinct from both A and B, not simply say it is a crossfade or a degraded version.

Do not claim a successful new identity based on cosine distances alone.

## Licensing and commercial warning

- This project's original adapter/prototype code is MIT.
- Upstream Seed-VC code is **GPL-3.0** and its repository was archived on 2025-11-21. A commercial proprietary Ghost Voice distribution that incorporates or bundles it needs legal review; do not assume it can be included unchanged in a closed-source plugin.
- The `funasr/campplus` model repository declares Apache-2.0, but model weights and every other dependency/checkpoint must be reviewed separately.
- The Seed-VC model checkpoint's license and redistribution terms must be checked independently before shipping.
- For a proprietary commercial release, either comply with all applicable copyleft obligations or replace this research backend with a compatible implementation/checkpoint after legal and technical review.

## Legacy ECAPA experiment

The original `pipeline.py` still exists for historical comparison. It uses SpeechBrain ECAPA, a speech-trained encoder. Its `.npz` profiles are **not** compatible with Seed-VC or SoulX-Singer and must not be used as singing renderer inputs.

## Responsible use

Use only voices you own or have explicit permission to process and generate. Do not create deceptive impersonations.
