# Third-party code in the GhostBlock plug-ins

Pinned versions of everything fetched by `CMakeLists.txt`, and the licence of each. No GPL code is linked into any plug-in.
Licence texts that must ship with the binaries are in `packaging/third_party/` and are copied into the installers.

| Dependency | Version / pin | Licence | Used by | Shipped notice |
|---|---|---|---|---|
| JUCE | 8.0.4 (tag, zip) | JUCE 8 licence (dual: JUCE commercial/personal licence, or AGPLv3) | all plug-ins | n/a (JUCE licence terms) |
| Signalsmith Stretch | 1.4.0 (tag, zip; commit `a670068`. Its header still reports `version = {1,3,2}`: upstream did not bump the constant for 1.4.0, the code is the 1.4.0 tag) | MIT (Copyright 2022 Geraint Luff / Signalsmith Audio Ltd.) | several plug-ins (see `CMakeLists.txt`); Ghost Slice's phase-vocoder pitching (CHOPS / TEXTURE) | Ghost Slice bundles: `packaging/third_party/SIGNALSMITH-MIT-LICENSE.txt` |
| signalsmith-linear | 0.6.4 (pulled by Stretch; commit `de55e6a`) | MIT (Copyright 2025 Signalsmith Audio) | as above | same file |
| WORLD | 1.0.1 (tag zip, SHA-256 `408d2429...d1fb24d1`) | modified BSD = 3-clause BSD (Copyright 2010 M. Morise; clause 2: text with every binary) | Ghosty Spectrum VOICE + GHOST VOICE (analysis + synthesis only); Ghost Slice build 6: StoneMask / CheapTrick / D4C analysis for INSTRUMENT / PAD | `packaging/third_party/WORLD-LICENSE.txt` (Ghost Slice ships it in Licenses/) |
| POP909 n-gram models | `brain_pop.json` from Block Step / Ghost House (copied to `GhostSlice/data/`, embedded gzip'd) | MIT (POP909 dataset, Music X Lab) | Ghost Slice build 6 POP BRAIN: statistics only (what chord / note / rhythm usually comes next); no song or MIDI is included | `packaging/third_party/POP909-MIT-LICENSE.txt` |
| Aicd 303 model | `brain_acid.json` from Block Step / Ghost House (copied to `GhostSlice/data/`, embedded gzip'd) | MIT (Aicd, Michael Ganss) | Ghost Slice build 6 ACID BRAIN: an n-gram of 303 pattern steps, sampled anew | `packaging/third_party/AICD-MIT-LICENSE.txt` |
| clap-juce-extensions | main commit `7adee3a1bd4684d4caa5601100e364abccff4b4d` (2026-10-06; newest tag 0.26.0 targets pre-1.0 CLAP) | MIT (Copyright 2019-2020 Paul Walker) | every `<Target>_CLAP` | `packaging/third_party/CLAP-LICENSES.txt` |
| CLAP SDK (free-audio/clap) | 1.2.7, commit `29ffcc273be7c7c651f6c9953b99e69700e2387a` (submodule of clap-juce-extensions) | MIT (Copyright 2021 Alexandre Bique) | every `<Target>_CLAP` | same file |
| clap-helpers | commit `a61bcdf0ecc2c8db1e80bfe8bf9cb7e8d9fd2bbc` (2025-11-28, submodule of clap-juce-extensions) | MIT (Copyright 2021 Alexandre Bique) | every `<Target>_CLAP` | same file |
| r8brain-free-src | tag `version-6.5`, commit `3c930bf6825c0cfea4a813210c83b1a650c820b5` (2023-12-07; vendored unmodified in `third_party/r8brain-free-src`, core files only, see its `PINNED.txt`) | MIT (Copyright 2013-2023 Aleksey Vaneev); includes Takuya Ooura's FFT (`fft4g.h`, free for any use incl. commercial) | Ghost Slice: the sound's sample-rate conversion (source rate -> 48 kHz for GENERATE; CDSPResampler24, linear phase) | Ghost Slice bundles: `packaging/third_party/R8BRAIN-MIT-LICENSE.txt` |
| eurorack (STM32 module code: plaits / clouds / rings) + stmlib | eurorack `f4cfa7697095a28d292bff758beb51deb49b5a86`, stmlib `e3bd7c9cc00e4364166f9905c0509b6ffd0535ec` (vendored in `third_party/eurorack-mit`) | MIT (Copyright 2012-2021 Emilie Gillet) | Ghosty Pro oscillator ENGINES | `packaging/third_party/EURORACK-MIT-LICENSE.txt` |

MP3 reading: the plug-ins that read MP3 files (Ghosty Pro, Poltergeist, Ghosty Spectrum, Ghost Box, Ghost Slice, ArpEZator) use JUCE's own
decoder (`JUCE_USE_MP3AUDIOFORMAT=1`, part of JUCE and covered by the JUCE row above; decode only, no encoder). The MP3 patents have expired
(2017), which is the condition JUCE's disclaimer for this flag asks the user to check. No other MP3 code is linked.

Ghost Slice (GhostSlice/, GENERATE since 2026-11-02) links only JUCE, Signalsmith Stretch + signalsmith-linear (MIT: the phase-vocoder
pitch shift with formant handling for CHOPS / TEXTURE), r8brain-free-src (MIT: the rate conversion) and the shared GhostBlock headers,
including Ghost Verb (`GhostyPro/ProVerb.h`, our own reverb: SPACE) and the CleanStage true-peak ceiling. Everything else is our own code:
the analysis (onsets, pitch, notes, key, chords, syllables), the voice engine (TD-PSOLA after Moulines & Charpentier 1990: the epoch marker
`Psola.cpp` with the LPC residual `Lpc.h` after Makhoul 1975, grain alignment, formant ratio by grain resampling), the granular clouds
(`GenDsp.cpp`: written from Roads, "Microsound", MIT Press 2001 and Bencina, "Implementing Real-Time Granular Synthesis", 2001; the
parameter set - density, size, spray, pitch, pan - follows Mutable Instruments Clouds, MIT, already vendored in `third_party/eurorack-mit`;
no Clouds code is compiled into Ghost Slice), the spectral freeze / blur (`Hold.cpp`: Paulstretch-style FREEZE written from Paul Nasca's
published description, and a phase-locked TEXTURE after Laroche & Dolson 1999), the loudness meter (ITU-R BS.1770-4, our own; libebur128,
MIT, was looked at as the reference implementation, nothing copied) and the music (scales, progressions, voice leading, phrases).
Research pass 2026-11-02 (granular / spectral freeze / phase vocoder / vocal chops): Paulstretch (paulstretch_python, public domain;
the C++ Paulstretch is GPL - study only), Signalsmith Stretch (MIT, used), Mutable Instruments Clouds (MIT, design reference), libebur128
(MIT, reference), pyo granulator examples (LGPL - study only), the Glasgow gslib granular library (study only). Nothing GPL / AGPL / NC / ND
is in the tree. Rubber Band, aubio, zita-* and SoundTouch were studied only (docs/research/ghost-slice/); none of their code is in the tree.
Test sounds: every voice in the tests and the example renders is synthesised by our own formant synthesiser (`tests/TestVoices.h`, after
the published structure of Klatt 1980); no recording, no dataset.

Ghost Slice build 6 (version 1.5.0, 2026-11-03) adds: WORLD (row above: modified BSD), signalsmith-linear's FFT (MIT, part of the
Signalsmith Stretch row: the phase-locked vocoder's FFT), ArpEZator's own engine (our code, ArpEZator/), the POP909 and Aicd n-gram models
(rows above, MIT) and our own code: the VOICE morph (`VoiceMorph.cpp`: sinusoidal + residual after Serra & Smith 1990 / McAulay & Quatieri
1986, shape-invariant re-pitch), the harmonic + noise synthesis (`Vocoder.cpp`), the identity phase-locked vocoder (`PhaseLock.cpp`, after
Laroche & Dolson 1999) and the melody brain (`Melody.cpp`, `Brain.cpp`: ports of Block Step / Ghost House's own generators, rules only).
Left out on purpose: the Satie piano brain (`brain_satie.json`): one of its 12 Mutopia sources (Gnossienne No. 2) is CC BY-SA 4.0, so it
is not clearly permissive; the owner decides. Test voice: `pd-voice.wav` (LibriVox, Public Domain Mark 1.0) is used for measurements and
the A/B demos only, it is not in the repo.

Ghost Hum (GhostHum/) links only JUCE and the shared GhostBlock headers: no model files, no third-party DSP, no datasets. Everything it
hears with is our own code: the pitch tracker (YIN's cumulative mean normalised difference, several dips per frame, a Viterbi note path -
the multi-candidate idea is pYIN's, de Cheveigne & Kawahara 2002 / Mauch & Dixon 2014, written from the papers), the five-band onset
analysis shared with Ghosty Beats (`Shared/HearCore.h`), and the beatbox classifier (softmax regression on 12 designed features, fitted by
`HumTests beattrain` ONLY on audio we synthesise ourselves in `GhostHum/tests/HumVoices.h`; nothing recorded, no downloaded datasets, no
NC / ND material). Its piano and drum sounds are synthesised (`HumSounds.h`). Build 2 adds only our own code: the look (`HumLook.h`:
glass, backdrop, ring - juce::Graphics drawing, no bitmaps, no fonts beyond the system's), the roll (`HumRoll.h`) and the speaker click
masking (`HumClicks.h`: template matching and subtraction of our own click, notches); the key-command set follows Block Step's, which is
the owner's own app. Studied for ideas, no code copied and nothing bundled:
spotify/basic-pitch (Apache-2.0 code and model card; a polyphonic neural transcriber - not used: it needs an ML runtime, and a monophonic
voice is better served by a pitch tracker), DamRsn/NeuralNote (Apache-2.0 code, but it ships model weights reported as CC BY-NC 4.0: never
bundled, never used), aubio (GPL), HachiTune (AGPL), Essentia (AGPL / commercial) - studied only, no code; beatbox2drums / beatbox_midi /
beatrice (no clear licence) - not used in any way.

Ghosty Spectrum GHOST VOICE (Shared/VoiceFusion*, Shared/VoiceFusionHost*, SpectrumX/GhostVoicePage*) adds only our own code on top of
WORLD (the analysis and the pulse + noise synthesis it already uses for VOICE). The voice-fingerprint extraction (vowel clusters of the
log spectral envelope on a log-frequency grid), the formant-anchored log-spectral morphing, the designed voices and the tests are written
from the published methods, no code copied: Abe, Nakamura, Shikano & Kuwabara 1988 (codebook mapping voice conversion), Slaney, Covell &
Lassiter 1996 (audio morphing), Kawahara & Matsui 2003 (STRAIGHT morphing: interpolation in a log-spectral domain), Pfitzinger 2004
(dynamic frequency warping of spectral envelopes for morphing), Shechtman & Sorin 2010 (frequency warping on mapped formant parameters),
Paliwal 1995 (which domains interpolate spectra perceptually well). The designed voices start from textbook average vowel formants (as the
VOICE page's vowel tables already do); no recording of any singer is in the tree, the tests or the presets, and no dataset is used.
Studied for reference only, nothing copied or bundled: pyworld / World forks (MIT / modified BSD), RMVPE (a neural pitch tracker; not
used: the tests measure pitch with WORLD's Harvest and our own YIN-style tracker instead).

Test-only tools (not shipped, not linked):

| Tool | Version | Licence | Use |
|---|---|---|---|
| clap-validator | 0.4.1 release binary (`clap-validator-0.4.1-127-g152b982-ubuntu-22.04`) | MIT | CLAP conformance check on Linux |

## Ghosty Pro oscillator ENGINES: Mutable Instruments eurorack code (STM32 modules, MIT)

The Ghosty Pro oscillator engines (VA, WAVESHAPE, FM 2-OP, ADDITIVE, GRAIN FORMANT, SWARM, STRING, MODAL, CHORD, SPEECH,
PARTICLE, TEXTURE, RESONATOR) are built on DSP code by Emilie Gillet from the Mutable Instruments eurorack repository.

- Source: https://github.com/pichenettes/eurorack, commit f4cfa7697095a28d292bff758beb51deb49b5a86 (2026-10-12)
  and its submodule https://github.com/pichenettes/stmlib, commit e3bd7c9cc00e4364166f9905c0509b6ffd0535ec (2023-05-30).
- Used: only the STM32 module code, which the repository licenses under MIT ("Code (STM32F projects): MIT license"):
  `plaits/dsp` (macro-oscillator engines), `clouds/dsp` (granular processor parts), `rings/dsp` (resonator), `stmlib/dsp`,
  `stmlib/utils`. Every vendored file carries the MIT notice below in its header. None of the repository's AVR projects (GPL 3.0),
  nor `stmlib/programming` (GPL), nor any hardware file (cc-by-sa) is used. The LPC speech word data (extracted from TI ROMs) is
  NOT included; the SPEECH engine uses the formant and phoneme synthesisers only.
- Where: `ghosty-pro/third_party/eurorack-mit/` (pinned copy, see PINNED.txt; local modifications are listed in PATCHES.md and
  marked `GHOSTY PATCH` in the files).
- Name use: per the repository's guidelines for derivative works, the product and the engines do not use the Mutable Instruments
  name or the original module names; the engines are named for what they do.

Licence text (stmlib/LICENSE and the per-file notice of the eurorack STM32 code):

```
Copyright 2012-2021 Emilie Gillet (emilie.o.gillet@gmail.com) (each file carries its own year)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

The same text ships with the installers as `packaging/third_party/EURORACK-MIT-LICENSE.txt` (Ghosty Pro bundles and the
licences folder).
