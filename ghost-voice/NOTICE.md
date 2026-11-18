# Ghost Voice - notices

Ghost Voice is free and open-source software by GhostBlock, released under the **GNU Affero General Public License, version 3
only** (`LICENSE`; JUCE 8 is used under its AGPLv3 option). Every build ships its complete source (`Licenses/Ghost Voice
source.zip`). None of this code may be copied into GhostBlock's paid plug-ins.

## What is inside, and under which licence

| Part | Where | Licence / author |
|---|---|---|
| **Seed-VC** (Plachtaa/seed-vc @ 51383ef): the singing voice conversion itself (`inference.py`, its models code). NOT inside the plug-in: the optional installer downloads it into the engine folder and Ghost Voice starts it as a separate helper process | downloaded by `helper/install-engine.sh` | GPL-3.0, Plachtaa (`packaging/licenses/SEED-VC-GPL-3.0.txt`) |
| **Ghost Voice Identity Lab v0.6** (Brandon's lab): the CAMPPlus enrolment, the A / B voice-vector blend and the guarded patch that adds it to Seed-VC's `inference.py` | `helper/lab` | MIT, Ghost Voice Identity Lab contributors (`packaging/licenses/GHOST-VOICE-LAB-MIT.txt`) |
| The helper (`ghostvoice_engine.py`: runs Seed-VC's own script, loads the models once per job, NOVELTY, the CAMPPlus measurements) and the installer | `helper/` | GhostBlock, AGPL-3.0 |
| Models the engine downloads (never shipped by us): Seed-VC singing DiT `DiT_seed_v2_uvit_whisper_base_f0_44k_bigvgan_pruned_ft_ema_v2` (Plachta/Seed-VC, GPL-3.0), BigVGAN v2 44 kHz 128-band (NVIDIA, MIT), Whisper small (OpenAI, Apache-2.0), RMVPE (lj1995/VoiceConversionWebUI, MIT), CAMPPlus `campplus_cn_common.bin` (funasr, Apache-2.0) | the engine folder | as listed |
| **NATURAL and CLASSIC** (build 2): GhostBlock's own voice-fusion engines, copied byte for byte from Ghosty Spectrum (`ghosty-pro/Shared/VoiceEngine*`, `VoiceFusion*`; the tests check the copies match and CI null-tests the renders against Spectrum's own renderer) | `Source/Fusion` | GhostBlock's own code, released here under the AGPL-3.0 |
| **WORLD v1.0.1** (M. Morise): the analysis (Harvest, CheapTrick, D4C) and synthesis under NATURAL and CLASSIC; fetched at build time from its pinned zip (SHA256 checked) and compiled in | fetched at build time | modified BSD (`packaging/licenses/WORLD-BSD.txt`; the licence must ship with every build) |
| uv 0.5.11 (the installer's private Python 3.10) | downloaded by the installer | MIT or Apache-2.0, Astral |
| **INTERSECT** (tucktuckg00se/INTERSECT @ b40c2fb, 2026-11-11): CHOP's sound engine (loading the clip, the slices, MIDI playback, its stretch engines). An untouched copy; Ghost Voice drives it through its own command API. Built without its stand-alone app and without ONNX Runtime (stem separation off; it has no Intel Mac package) | `third_party/intersect` | GPL-3.0, tucktuckg00se (`packaging/licenses/INTERSECT-GPL-3.0.txt`) |
| Signalsmith Stretch, Signalsmith Linear (INTERSECT's stretch engine) | `third_party/intersect/signalsmith-*` | MIT, Signalsmith Audio |
| Bungee (INTERSECT's other stretch engine), with Eigen (headers) and PFFFT | `third_party/intersect/bungee` | MPL-2.0 (Bungee, Eigen), BSD-like (PFFFT, Julien Pommier) |
| VelociLoops (INTERSECT's REX2 import) | `third_party/intersect/third_party/velociloops` | public domain (Unlicense), kunitoki |
| **Flayr Chop's analysis engine** (FlayrLabs/flayr-chop @ c7baf83, `Source/Analysis.h`): CHOP's tempo, key and hit finder and AUTO CHOP's HITS / BEATS / 2 BEATS / BAR / EVEN cue placement. An untouched copy | `third_party/flayr-chop` | AGPL-3.0, FlayrLabs (`packaging/licenses/FLAYR-CHOP-AGPL-3.0.txt`) |
| SPLASH (build 9): the look in the palette of Brandon's painting, the gooey panels, the sketched outlines, the hair logo | `Source/UI/SplashArt.h` | GhostBlock's own code, AGPL-3.0 |
| CHOP's page, its clip editor, AUTO CHOP's RANDOM mode, the KEYS modes, CONVERT VOICE (build 8); the tools and keys are Block Step's (GhostBlock) | `Source/Chop` | GhostBlock's own code, released here under the AGPL-3.0 |
| **OpenVPI audio-slicer** (openvpi/audio-slicer @ 9958eed, `slicer2.py`): CHOP's PHRASES (a vocal split at its silences). An untouched copy, run by `helper/chop_tools.py` in a separate Python process | `helper/third_party/openvpi-audio-slicer` | MIT, Team OpenVPI (`packaging/licenses/OPENVPI-AUDIO-SLICER-MIT.txt`) |
| **WhisperX 3.3.1** (m-bain/whisperX) with faster-whisper "small" and the wav2vec2 word aligner: CHOP's WORDS. NOT inside the plug-in: the optional installer downloads it into the chop-tools folder and Ghost Voice runs it as a separate helper | downloaded by `helper/install-chop-tools.sh` | BSD-2-Clause (WhisperX), MIT (faster-whisper, Whisper), torchaudio's wav2vec2 model (MIT, fairseq) |
| JUCE 8.0.14 (build 8; INTERSECT's pin) | fetched at build time | AGPLv3 option |
| TERRAIN, the Spirit Board, the ghost art, the themes (snapshots of GhostBlock's shared code, see `Source/*/SNAPSHOT.txt`), the window, MOTION, everything else | `Source/`, `tests/` | GhostBlock's own code, released here under the AGPL-3.0 |

## Ideas we learned from
- **Bencho's liquid toggle** (MIT, https://bencho.dev/licence), via the Ghost Voice React adaptation Brandon supplied: the switch's
  proportions, spring and stretch rule are re-created natively in `Source/UI/LiquidToggle.h` (no code from it runs).

- **ReactDrops "Bubble UI"** (soap-bubble): Brandon liked its look for presses. Its licence (ReactDrops Component License 1.0)
  forbids redistributing its source as part of anything offered for reuse, and Ghost Voice ships its full source, so none of its
  code is used: `Source/UI/BubbleFx.h` is our own effect, written from the physics of thin-film colour, in our palette.

## Voices
Ghost Voice ships no voices and no presets of anyone's voice. It renders only after the user ticks "I have the rights to these
voices". The demos use VocalSet (Wilkins, Seetharaman, Wahl & Pardo, "VocalSet: A Singing Voice Dataset", ISMIR 2018, CC BY 4.0).
