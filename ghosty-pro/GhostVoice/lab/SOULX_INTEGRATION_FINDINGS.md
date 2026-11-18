# SoulX-Singer integration findings (2026-11-05)

## What the official SVC inference code actually consumes

The official `SoulXSingerSVC.infer(...)` interface accepts:

- `pt_wav`: one prompt/reference waveform
- `gt_wav`: the singing performance to convert
- `pt_f0`: prompt pitch track
- `gt_f0`: target pitch track
- inference controls (`pitch_shift`, `n_steps`, `cfg`, etc.)

Inside `infer_segment`, the model computes a prompt mel representation from `pt_wav`, extracts prompt and target content features with its Whisper encoder, combines those content features with pitch embeddings, and conditions the CFM decoder using the prompt mel and prompt/target decoder features.

Official source inspected:
- https://github.com/Soul-AILab/SoulX-Singer/blob/main/soulxsinger/models/soulxsinger_svc.py
- `infer` and `infer_segment` methods; source lines around 1139-1391 in the current GitHub view.

## Consequence for Ghost Voice

SoulX-Singer-SVC is a strong candidate renderer for singing voice conversion, but its public inference interface does **not** expose:

- two independent singer-reference inputs,
- a speaker-identity embedding input,
- a documented identity-fusion control.

Therefore, the existing SpeechBrain ECAPA profile cannot be passed to SoulX-Singer-SVC. Concatenating A and B reference waveforms, alternating chunks, or averaging ECAPA vectors would not constitute validated identity fusion and could cause unstable or source-dominated results.

The integration gate in `ghostvoice/renderer_contract.py` intentionally rejects this unsupported path.

## Correct next engineering steps

1. Reproduce the official SoulX-Singer-SVC baseline with one permitted reference singer and one held-out target performance.
2. Instrument the model to expose and log the exact prompt conditioning tensors at the decoder boundary.
3. Determine whether prompt mel conditioning can be replaced by a learned identity token/latent without breaking content, pitch, alignment, or decoder behavior.
4. Build a multi-reference identity encoder in the renderer's own training space, not in ECAPA's unrelated speech embedding space.
5. Train a fusion network using a properly licensed/consented multi-singer singing corpus and explicit held-out singer/song splits.
6. Evaluate whether generated C is distinct from both A and B using blinded listening plus singing-domain identity metrics; also measure melody/content preservation and artifacts.
7. Only after these gates pass, expose `Create Voice C` and build the JUCE plugin bridge.

## Acceptance criteria (not yet met)

- C is consistently judged distinct from A and B in blinded comparisons.
- C retains a stable identity across different songs, vowels, registers, and recording conditions.
- The converted/generated performance preserves target lyrics/content, timing, and pitch within defined tolerances.
- No severe artifacts, metallic/grainy timbre, unstable consonants, or phrase-boundary identity drift.
- A saved C profile reproduces the same identity across separate sessions.
- All model code, checkpoints, datasets, and distribution rights are reviewed before commercial release.

This document records an integration finding. It is not a completed fusion model or audio renderer.
