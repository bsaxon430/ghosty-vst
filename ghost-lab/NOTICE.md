# Ghost Lab - notices

From build 12, Ghost Lab is free and open-source software by GhostBlock, released under the **GNU Affero General Public
License, version 3 only** (`LICENSE`). Every build ships its complete source (`Licenses/Ghost Lab source.zip`). Builds 1
to 11 were not released.

Build 12 moved the engines of Ghost Matter (GhostBlock's AGPL instrument, now retired) into Ghost Lab as its rooms:
STRETCH, REBORN and SPACE (build 12 also had a GROW room; build 13 took its page out, and laid each room's page out like
the original plug-in's own window, with the original's own control names). One of them (PaulXStretch) is GPL-3.0, which is why the whole of Ghost Lab is now
open source. None of this code may be copied into GhostBlock's paid plug-ins.

## What is inside, and under which licence

| Part | Where | Licence / author |
|---|---|---|
| **CDP8** (ComposersDesktop/CDP8 @ 28bc42c): THE LAB's engine. Its programs pvoc, stretch, strange, focus, blur, modify, combine, morph, repitch, formants, envel and cdparams, separate executables inside each bundle (`Contents/Helpers/cdp`), unmodified | `cdp/` (build script) | LGPL-2.1-or-later (`packaging/licenses/CDP-LGPL-2.1.txt`); its full source ships as `Licenses/CDP8-source.zip` |
| **Flues** (danja/flues @ 2eae800): compiled in (build 12's GROW room; no page since build 13). PM-Synth, Chatterbox, Disyn: the header-only engines and their own LV2 plug-in files (`lv2/*_plugin.cpp`, `.ttl`), unmodified; Ghost Lab is a small LV2 host for them | `third_party/flues` | MIT, Danny Ayers (`packaging/licenses/FLUES-MIT-LICENSE.txt`) |
| **PaulXStretch** (essej/paulxstretch @ b6175e9): STRETCH. Stretch, ProcessedStretch, StretchSource, BinauralBeats, FreeEdit, Input, globals, jcdp_envelope, its optimizebufsize, the WDL resampler it uses and its envelope editor, unmodified apart from two marked one-line JUCE 8 build shims and four marked lines that let the envelope editor's colours be set | `third_party/paulxstretch` | GPL-3.0 (Nasca Octavian Paul, Xenakios, Jesse Chappell). The files' headers carry the older "GPL v2" text; the project is released under GPL-3 (its README) (`packaging/licenses/PAULXSTRETCH-GPL-3.0.txt`) |
| **PFFFT** + FFTPACK (PaulXStretch's FFT) | `third_party/pffft` | BSD-like, (C) 2013 Julien Pommier; FFTPACK (C) 2004 UCAR (licence text in `pffft.c`) |
| **DDSP-VST** (magenta/ddsp-vst @ f2996e9): REBORN. Its own processor, inference pipeline, synthesizers and model library, unmodified, run offline on the sound. Its 11 instrument models (Flute, Violin, Trumpet, Saxophone, Bassoon, Clarinet, Melodica, Sitar, Trombone, Tuba, Vowels) and its pitch model are built in | `third_party/ddsp-vst` | Apache-2.0, (C) 2022 Google LLC / the DDSP-VST authors; models Apache-2.0 (`packaging/licenses/DDSP-VST-APACHE-2.0.txt`) |
| **TensorFlow Lite** (tensorflow/tensorflow @ d8ce9f9, the commit DDSP-VST pins) and the libraries its CMake fetches (Abseil, Eigen, FlatBuffers, gemmlowp, ruy, farmhash, fft2d, cpuinfo, FP16, NEON_2_SSE): DDSP's model runner | fetched at build time | Apache-2.0, (C) the TensorFlow authors (`packaging/licenses/TENSORFLOW-APACHE-2.0.txt`); Eigen MPL-2.0; the others Apache-2.0 / BSD / MIT |
| **Pertylizer** (pertyjons/pertylizer @ 3726360): SPACE. Its own Rust crates (Spatial Panner, Reverb; Euclidean and Turing Machine are linked but not on a page yet), built by cargo and linked through our small C interface | `third_party/pertylizer-ffi` | MIT, (C) 2026 Per Jonsson (`packaging/licenses/PERTYLIZER-MIT-LICENSE.txt`) |
| **Box2D** v3.1.1 (erincatto/box2d), unmodified: compiled in for the BOUNCE toy that came with Ghost Matter (not on a page yet) | `third_party/box2d-3.1.1` | MIT, (C) 2022 Erin Catto (`third_party/box2d-3.1.1/LICENSE`) |
| SoundThread's process help (some of UNDER THE HOOD's longer descriptions) | `cdp/soundthread` | MIT, (C) 2025 Jonathan Higgins (`packaging/licenses/SoundThread-MIT.txt`) |
| JUCE 8.0.4 | fetched at build time | used under its AGPLv3 option |
| Poppins font (embedded, unmodified) | `fonts/` | SIL Open Font License 1.1 |
| The lab, THE LIVING SOUND, TERRAIN, the rooms' pages and pictures, the glue to each engine (`Source/`, `tests/`), and the banner art | `Source/`, `art/` | GhostBlock's own code and Brandon's own artwork, released here under the AGPL-3.0 |

## Ours, on top of the engines (they only set the engines' own controls)
- GROW's LOUD / BRIGHT / ROUGH / SHAPE, STRETCH's START FROM chips, SPACE's pad and chips, REBORN's AUTO (it sets DDSP's own
  Pitch Shift by whole octaves), CHAOS / SWAP / FEED BACK (they move DDSP's own controls, switch its model, or feed its
  output back in; at 0 it plays as DDSP-VST plays it).
- REBORN's tune line is our own pitch tracker for the picture and for AUTO only (the YIN method, de Cheveigne & Kawahara
  2002; our code). The sound itself is tracked by DDSP's own model.
- Every result goes through the lab's finishing stage (edge fades, DC removal, level matched to the sound before, true
  peak held under -1 dBTP), as every lab result always has.

## Where the source code is
- Ghost Lab (this build, complete): Licenses/Ghost Lab source.zip, next to the plug-ins
- CDP8: https://github.com/ComposersDesktop/CDP8 (and Licenses/CDP8-source.zip)
- Flues: https://github.com/danja/flues
- PaulXStretch: https://github.com/essej/paulxstretch
- PFFFT: https://bitbucket.org/jpommier/pffft
- DDSP-VST: https://github.com/magenta/ddsp-vst
- TensorFlow Lite: https://github.com/tensorflow/tensorflow/tree/d8ce9f9c301d021a69953134185ab728c1c248d3/tensorflow/lite
- Pertylizer: https://github.com/pertyjons/pertylizer
- Box2D: https://github.com/erincatto/box2d
- SoundThread: https://github.com/j-p-higgins/SoundThread
- JUCE: https://github.com/juce-framework/JUCE

## Sounds
Every sound GROW makes is made by the Flues engines inside Ghost Lab (no samples). REBORN's models were trained by
Magenta on their own recordings and ship with DDSP-VST. No samples, songs or MIDI from anyone else are included.
