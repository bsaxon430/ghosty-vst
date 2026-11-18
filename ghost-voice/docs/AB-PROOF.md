# Ghost Voice: neural (Seed-VC) vs our DSP (NATURAL / CLASSIC): the A/B proof

Run: GitHub Actions `ghostvoice-neural`, run 41997317850 (2026-11-05), Apple Silicon runner (macos-14, "Apple M1 (Virtual)").
Everything is in the artifact **GhostVoice-AB-proof**: `demos/` (mp3s, the blind page), `results/` (all numbers), the lab's own
blind page, the answer keys (kept apart).

## What was compared
- **Voices** (VocalSet, CC BY 4.0, research singers): voice A = singer F1, voice B = singer F2 (two takes each: "caro mio ben" and
  a scale, 20 s each). The performance that is re-sung = singer F3 singing "caro mio ben" (the first 12 s). No famous voices.
- **Seed-VC** (Plachtaa/seed-vc @ 51383ef, GPL-3.0, its singing model), run by Brandon's Identity Lab v0.6 unchanged: its patch,
  its benchmark (A anchor, B anchor, C at 25 / 50 / 75 % B, all from the same performance), its YIN melody check, its blind page.
  Every clip starts from the same noise (fixed seed). Also two "stock" renders: plain Seed-VC with its normal reference prompt
  (how Seed-VC is meant to be used), for A and for B.
- **Our current Ghost Voice**, CLASSIC and NATURAL, on the same layout (the performer's own weight at 0, identity 1 = A, identity
  2 = B), same blends.

## The numbers (12 s clip)
CAMPPlus = Seed-VC's own identity encoder (cosine, 1 = the same voice). "A..B" = where the clip sits between voice A (0) and voice
B (1). Second opinion = Resemblyzer (a speech encoder; it hears these two sopranos as nearly the same voice: A-B 0.93).

| | melody (median cents off / within a semitone) | to A | to B | to the performer | A..B | clicks / s | loudness | true peak | render (CPU) |
|---|---|---|---|---|---|---|---|---|---|
| performance (as recorded) | 0 / 100 % | 0.70 | 0.66 | 1.00 | | 0.17 | -25.3 LUFS | -7.4 | |
| **Seed-VC** A anchor | 7 / 98 % | **0.81** | 0.70 | 0.65 | 0.38 | 0.00 | -14.9 | -1.6 | 145 s |
| **Seed-VC** 25 % B | 7 / 99 % | 0.81 | 0.73 | 0.66 | 0.42 | 0.00 | -14.0 | -1.0 | 127 s |
| **Seed-VC** 50 % B | 7 / 99 % | 0.80 | 0.77 | 0.67 | 0.46 | 0.00 | -13.1 | -0.4 | 145 s |
| **Seed-VC** 75 % B | 7 / 99 % | 0.78 | 0.79 | 0.67 | 0.51 | 0.00 | -12.2 | 0.0 | 157 s |
| **Seed-VC** B anchor | 7 / 99 % | 0.75 | **0.81** | 0.67 | 0.57 | 0.00 | -11.3 | **+0.1** | 133 s |
| Seed-VC stock (prompted) A / B | 8 / 99 %, 7 / 99 % | 0.81 / 0.70 | 0.66 / 0.82 | 0.61 / 0.66 | 0.36 / 0.63 | 0.08 / 0.17 | -28 / -26 | -14 | 639 s / 528 s |
| NATURAL A .. B anchor | 0 / 100 % | 0.77 .. 0.74 | 0.66 .. 0.72 | **0.73 .. 0.84** | 0.41 .. 0.49 | 0.00 | -18.0 | -1.0 | 1.6 s |
| CLASSIC A .. B anchor | 0-1 / 100 % | 0.65 .. 0.59 | 0.64 .. 0.66 | 0.60 .. 0.61 | 0.50 .. 0.55 | 0.25 | -19.5 .. -18.8 | -1.0 | 1.0 s |

Other measures (`results/results.md`): buzz 0 dB for all (none); HNR about 10 dB for all (the same as the recording); Seed-VC has
clearer formants (peak-to-valley 27-28 dB vs 22 dB in the recording, 20-24 dB for NATURAL) and more air above 8 kHz (-36 dB vs
-44 dB).

## In plain words
- **Only Seed-VC really changes who is singing.** Its A anchor is as close to singer A as the B anchor is to singer B (0.81 each),
  and its blend walks from A towards B as the slider goes up (A..B 0.38 -> 0.46 -> 0.57). The performer fades out (0.65-0.67).
- **NATURAL keeps the performer.** It moves only a little (A..B 0.41 -> 0.49), and the further it goes towards "B" the MORE it
  sounds like the performer (0.73 -> 0.84): it reshapes the performer's own audio, so the performer stays.
- **CLASSIC moves away from everyone.** It is about equally far from A, B and the performer (0.59-0.66): a re-synthesised,
  more generic voice, with clicks (0.25 / s).
- **Melody:** our DSP keeps the pitch exactly (it never re-sings). Seed-VC keeps it within a few cents (median 7 cents, 98-99 % of
  the sung frames within a semitone): the same melody, re-sung.
- **Artifacts:** Seed-VC (as the lab runs it, without a prompt) measured no clicks and no buzz. The prompted stock Seed-VC has a few
  clicks, about as many as the recording itself.
- **Loudness:** Seed-VC comes out loud (-15 to -11 LUFS) and its B anchor peaks just past 0 dBFS. The new Ghost Voice writes its takes
  as 32-bit float WAVs so nothing clips, and adds no limiter of ours.
- **Does it sound clearly better?** The measurements say Seed-VC is the only engine that does the job (a new voice between A and B,
  with no clicks, clearer formants and more air). Nobody has listened yet (the build machines can't hear). Brandon: play `demos/side_by_side_*.mp3`
  (each: the performance, then Seed-VC, NATURAL, CLASSIC at that blend, level-matched) and do the blind page
  (`demos/blind_engines/index.html`).
- **These two singers are close** (CAMPPlus 0.80, Resemblyzer 0.93: two classical sopranos), so every engine's A..B range is small. A
  clearer test needs two very different voices (a low male and a high female), which is how the new Ghost Voice will be tried.

- **So the new Ghost Voice is built on Seed-VC, and CLASSIC / NATURAL are left out** (Brandon's rule: keep them only if they sound
  better somewhere). They stay in Ghosty Spectrum untouched. If the listening test says NATURAL wins somewhere (it is 100x faster and
  keeps the performer), it can come back as a quick preview.

## Speed
- Apple Silicon **CPU** (GitHub's virtual M1, 3 cores): Seed-VC sings at **10-12x real time** (12 s took 2-2.5 minutes, model
  loading included), the prompted stock path 43-50x (its 20 s reference prompt makes the context 3x longer). Our DSP: 0.1x.
- **Apple GPU: not measurable on GitHub.** The virtual Macs report the GPU but fail on the first allocation (`MPS backend out of
  memory ... allocated 32 MB`), so the run fell back to the CPU. A real M-series Mac uses its GPU; the new app shows the real time
  of every render ("sung in N s"), so Brandon's first render gives the true number.

## What shipping it takes (now built as its own product: `ghost-voice/`)
- **Licence:** Seed-VC (code and the singing model) is GPL-3.0; the other models are MIT / Apache-2.0. Brandon chose a separate free
  open-source Ghost Voice, so it is AGPL-3.0 (JUCE 8's open-source option) with its full source in every build, and Seed-VC is
  not inside the plug-in at all: it runs as a separate helper process.
  Other options, for the record: keep Ghosty Spectrum paid and ship this only as a free GPL add-on (what was built), or replace
  Seed-VC with an Apache / MIT engine. SoulX-Singer-SVC (Apache-2.0, code and weights) is the strongest such candidate, but it has
  no voice-vector input, so it cannot blend A and B without new training. RVC (MIT), so-vits-svc-fork (MIT) and DDSP-SVC (MIT) all
  need a model TRAINED per singer, so they cannot take two dropped-in recordings either. svc-develop-team/so-vits-svc is AGPL.
- **Helper + download:** the plug-in installs the engine on request into one folder (`~/Library/Application Support/GhostBlock/Ghost
  Voice/engine`): a private Python 3.10 (via uv), Seed-VC at the pinned commit, its pinned packages (torch 2.5.1 ...), and the models,
  **about 2.5 GB download** (the singing model 821 MB, Whisper small 967 MB, BigVGAN 489 MB, RMVPE 181 MB, CAMPPlus 28 MB).
- **GPU / CPU:** Apple GPU when it works, CPU otherwise (the helper checks). Offline render, not real-time.

## Pins and licences checked
| part | pin | licence |
|---|---|---|
| Seed-VC code | Plachtaa/seed-vc @ 51383efd921027683c89e5348211d93ff12ac2a8 (2025-04-20; repository archived 2025-11-21) | GPL-3.0 |
| Seed-VC singing model `DiT_seed_v2_uvit_whisper_base_f0_44k_bigvgan_pruned_ft_ema_v2.pth` | Plachta/Seed-VC @ 257283f | GPL-3.0 (model card) |
| BigVGAN v2 44 kHz 128-band (vocoder) | nvidia/bigvgan_v2_44khz_128band_512x @ 95a9d1d | MIT |
| Whisper small (content encoder) | openai/whisper-small @ 973afd2 | Apache-2.0 |
| RMVPE (pitch) | lj1995/VoiceConversionWebUI @ e6d0c1a | MIT |
| CAMPPlus (identity) | funasr/campplus @ e4b6ede | Apache-2.0 |
| Identity Lab v0.6 | Brandon's lab (ghosty-pro/GhostVoice/lab, ghost-voice/helper/lab) | MIT |
| VocalSet test voices | mirror zhu3000/squillo-vocal-quality @ b81c422 (original: zenodo 1193957) | CC BY 4.0 |
Seed-VC's own code downloads the models from each repository's main branch; the revisions above are what main was on 2026-11-05.
