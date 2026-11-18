# Third-party code in the GhostBlock plug-ins

Pinned versions of everything fetched by `CMakeLists.txt`, and the licence of each. No GPL code is linked into any plug-in.
Licence texts that must ship with the binaries are in `packaging/third_party/` and are copied into the installers.

| Dependency | Version / pin | Licence | Used by | Shipped notice |
|---|---|---|---|---|
| JUCE | 8.0.4 (tag, zip) | JUCE 8 licence (dual: JUCE commercial/personal licence, or AGPLv3) | all plug-ins | n/a (JUCE licence terms) |
| Signalsmith Stretch | 1.4.0 (tag, zip; commit `a670068`. Its header still reports `version = {1,3,2}`: upstream did not bump the constant for 1.4.0, the code is the 1.4.0 tag) | MIT (Copyright 2022 Geraint Luff / Signalsmith Audio Ltd.) | several plug-ins (see `CMakeLists.txt`); Ghost Slice's LIVE + STUDIO stretch / pitch / formant engine | Ghost Slice bundles: `packaging/third_party/SIGNALSMITH-MIT-LICENSE.txt` |
| signalsmith-linear | 0.6.4 (pulled by Stretch; commit `de55e6a`) | MIT (Copyright 2025 Signalsmith Audio) | as above | same file |
| WORLD | 1.0.1 | modified BSD (clause 2: text with every binary) | Ghosty Spectrum VOICE | `packaging/third_party/WORLD-LICENSE.txt` |
| clap-juce-extensions | main commit `7adee3a1bd4684d4caa5601100e364abccff4b4d` (2026-10-06; newest tag 0.26.0 targets pre-1.0 CLAP) | MIT (Copyright 2019-2020 Paul Walker) | every `<Target>_CLAP` | `packaging/third_party/CLAP-LICENSES.txt` |
| CLAP SDK (free-audio/clap) | 1.2.7, commit `29ffcc273be7c7c651f6c9953b99e69700e2387a` (submodule of clap-juce-extensions) | MIT (Copyright 2021 Alexandre Bique) | every `<Target>_CLAP` | same file |
| clap-helpers | commit `a61bcdf0ecc2c8db1e80bfe8bf9cb7e8d9fd2bbc` (2025-11-28, submodule of clap-juce-extensions) | MIT (Copyright 2021 Alexandre Bique) | every `<Target>_CLAP` | same file |
| r8brain-free-src | tag `version-6.5`, commit `3c930bf6825c0cfea4a813210c83b1a650c820b5` (2023-12-07; vendored unmodified in `third_party/r8brain-free-src`, core files only, see its `PINNED.txt`) | MIT (Copyright 2013-2023 Aleksey Vaneev); includes Takuya Ooura's FFT (`fft4g.h`, free for any use incl. commercial) | Ghost Slice: the working copy's sample-rate conversion (source rate -> host rate; CDSPResampler24, linear phase) | Ghost Slice bundles: `packaging/third_party/R8BRAIN-MIT-LICENSE.txt` |
| Open303 (TB-303 emulation, DSPCode only) | github.com/scheffle/open303 commit `850c98186db28829f9ff6c8254966cdb6bf334e6` (vendored in `third_party/open303-mit`, one GHOSTY PATCH: allocation-free gateOn / gateOff, see its `PINNED.txt`); includes Takuya Ooura's FFT (`fft4g.c`, free for any use incl. commercial) | MIT (Copyright (c) 2009 Robin Schmidt) | Block Step + Ghost House SYNTH page (`Shared/GhostSynth.cpp`): the 303 voice | `packaging/third_party/OPEN303-MIT-LICENSE.txt`; also shown in the page's SYNTH panel (Licenses) |
| eurorack (STM32 module code: plaits / clouds / rings) + stmlib | eurorack `f4cfa7697095a28d292bff758beb51deb49b5a86`, stmlib `e3bd7c9cc00e4364166f9905c0509b6ffd0535ec` (vendored in `third_party/eurorack-mit`) | MIT (Copyright 2012-2021 Emilie Gillet) | Ghosty Pro oscillator ENGINES | `packaging/third_party/EURORACK-MIT-LICENSE.txt` |

MP3 reading: the plug-ins that read MP3 files (Ghosty Pro, Poltergeist, Ghosty Spectrum, Ghost Box, Ghost Slice, ArpEZator) use JUCE's own
decoder (`JUCE_USE_MP3AUDIOFORMAT=1`, part of JUCE and covered by the JUCE row above; decode only, no encoder). The MP3 patents have expired
(2017), which is the condition JUCE's disclaimer for this flag asks the user to check. No other MP3 code is linked.

Ghost Slice (GhostSlice/) links only JUCE, Signalsmith Stretch + signalsmith-linear, r8brain-free-src and the shared GhostBlock headers. Its onset /
analysis code is our own, and so is the GHOST PITCH VOCAL engine (TD-PSOLA, `Psola.cpp`) and the formant correction / LPC
(`FormantFix.cpp`, `Lpc.h`), written from the papers (Moulines & Charpentier 1990; Makhoul 1975; Roebel & Rodet DAFx-05), and so is the syllable detector
(`Syllables.cpp`, after De Jong & Wempe 2009) and the multi-f0 research prototype (tests only, after Klapuri 2006).
Rubber Band, aubio, zita-* and SoundTouch were studied only (docs/research/ghost-slice/); none of their
code is in the tree. Bungee (MPL-2.0) is not used yet (planned for scrub / zero-speed playback as a separate unmodified module).

Test-only tools (not shipped, not linked):

| Tool | Version | Licence | Use |
|---|---|---|---|
| WORLD | 1.0.1 (modified BSD, the same copy as Ghosty Spectrum VOICE) | modified BSD | linked into GhostSliceTests only: the pitch-engine benchmark (`GhostSliceTests bench`, docs/research/ghost-slice/BENCHMARK.md); not in the Ghost Slice plugin |
| clap-validator | 0.4.1 release binary (`clap-validator-0.4.1-127-g152b982-ubuntu-22.04`) | MIT | CLAP conformance check on Linux (also run by `GhostSliceTests pluginsmoke`) |

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

## Block Step / Ghost House SYNTH page (2026-10-29)

`Shared/GhostSynth.cpp` is our own synth bus built on Open303 (above) and the vendored Plaits engines (virtual analog, FM, swarm; the
eurorack row above, linked through `gpro_eurorack`). Plaits' MIT notice ships in `packaging/third_party/EURORACK-MIT-LICENSE.txt` and is
shown in the page's SYNTH panel. The acid GENERATE is our own code after the idea of the "Endless Acid Banger" by Vitling (David Whiting,
CC BY 4.0, vitling.xyz/toys/acid-banger): credited in the SYNTH panel. AcidBox (copych, MIT) and jc303 (GPL-3.0) were studied only;
none of their code is in the tree (jc303's GPL code was deliberately not used).

## Learned generators: ⚡ GENERATE (Block Step), HOUSE BRAIN (Ghost House) and 🧠 BRAIN (both) (2026-10-29)

The pages embed models (data, no third-party code) learned from music that is free to use commercially:

| Model | Learned from | Licence | Notice |
|---|---|---|---|
| Drum brain (house + breaks) | DrumBrain by Michael Ganss: its LZ model of ~250 real 808/909 patterns (`model.js`, house and breaks parts) | MIT | `packaging/third_party/DRUMBRAIN-MIT-LICENSE.txt` |
| Acid / bass lines | Aicd by Michael Ganss: its n-gram model of real TB-303 patterns (`lm.js`), re-expressed per step | MIT | `packaging/third_party/AICD-MIT-LICENSE.txt` |
| Chords, melodies, accompaniment rhythm | Our own n-gram models trained on POP909 (Music X Lab, 909 pop songs: melody, piano, chord annotations) | MIT | `packaging/third_party/POP909-MIT-LICENSE.txt` |
| Block Step piano (Satie) | Our own n-gram models trained on Erik Satie's piano works (public domain), read from Mutopia Project LilyPond scores marked Public Domain | Public domain | none needed |

The generators only sample what usually comes next (a step, a note, a chord); every take is new.
