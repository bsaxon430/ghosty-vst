# Changelog

## v0.6.0 — End-to-end benchmark orchestration
- Added recorded-voice file validation and content fingerprinting.
- Added full pretrained renderer invocation with A/B matching anchors and fusion variants.
- Added rendering log capture, failure handling, output WAV checks and manifest.
- Added optional pitch/melody YIN diagnostic; explicitly not an identity test.
- Added offline randomized listening-review page and local end-to-end Gradio UI.
- Added selected VocalSet reference downloader, not executed in offline build.
- **Not yet tested against actual Seed-VC model/checkpoints or real singing audio.**



## v0.5.0 — Seed-VC-native fusion bridge
- Added exact CAMPPlus 192-D style extraction with Seed-VC's Kaldi fbank frontend.
- Added robust multi-reference enrollment and raw-space A/B style interpolation.
- Added Seed-VC CFM style-condition bridge and profile export CLI.
- Added guarded patcher for two-reference, promptless fused-style SVC experiments.
- Added synthetic-audio/fake-encoder tests and patcher integration test.
- Clarified GPL-3.0 and commercial distribution caveats.
- Full pretrained singing inference remains untested; no claim of production quality.

## v0.4.0 — Renderer compatibility gate
- Added a guard against passing incompatible ECAPA embeddings into SoulX-Singer-SVC.
- Documented the model's reference waveform/F0 conditioning contract.

## v0.3.0 — Multi-reference enrollment
- Added segment-level quality checks, medoid outlier rejection and weighted aggregation.

## v0.2.0 — Identity profile experiment
- Added experimental identity-space fusion diagnostics.

## v0.1.0 — Initial research prototype
- Added speech-trained ECAPA embedding extraction and Gradio interface.
