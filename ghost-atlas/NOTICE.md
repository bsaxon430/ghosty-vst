# Ghost Atlas: what it is made of

Ghost Atlas is a personal instrument, built for one person and not distributed. It stands on other people's work, and
this file says whose, under what terms, and where each piece is.

## The instrument

| Part | What it does here | Licence | Where |
|---|---|---|---|
| [JUCE 8.0.4](https://juce.com) | Plug-in formats, windows, drawing | AGPL-3.0 | fetched at build |
| [sfizz 1.2.3](https://sfz.tools/sfizz/) | Plays every layer: one sfizz engine per layer, unmodified | BSD-2-Clause | fetched at build; `third_party/licenses/SFIZZ-BSD-2-LICENSE.txt` |
| [dusk-fizz](https://github.com/dusk-audio/dusk-fizz) `abd338d` | Not in the build as shipped. sfizz 1.2.3 with portability fixes; takes sfizz's place only when built with `-DGHOSTATLAS_SFIZZ=dusk` | BSD-2-Clause | fetched at build when asked for; its own `LICENSE` |
| [Samplebrain](https://gitlab.com/then-try-this/samplebrain) (Then Try This) | Timbre analysis (its blocks, FFT, MFCC and distance) for MUTATE's shortlist; its brain and renderer for REIMAGINE | GPL-2.0-or-later | `third_party/samplebrain/` (compiled as upstream wrote it; Ghost Mosaic's three marked patches) |
| [Aquila](https://aquila-dsp.org) | The MFCC inside Samplebrain | MIT | inside `third_party/samplebrain/` |
| [FFTW 3.3.10](https://www.fftw.org) | The FFT Samplebrain is written against | GPL-2.0-or-later | built once by `cmake/build-fftw.sh` |
| [Signalsmith Stretch](https://signalsmith-audio.co.uk/code/stretch/) | VOICE SHIFT: pitch and formant shifting, through its documented API | MIT | fetched at build |
| [TinySoundFont](https://github.com/schellingb/TinySoundFont) | Reads SoundFonts for the importer | MIT | `third_party/tsf/` |
| Phantom FX effect units | The 24 effect units on layers, sends and master | AGPL-3.0 (own work, with its own third parties) | `../phantom-fx/`, licences in `../phantom-fx/third_party/licenses/` |
| [Spectral Corruptor](https://github.com/Dirty-Octopus/spectral-corruptor-plugin) v0.2.6, commit `81d39c7` (Dirty Octopus) | SPECTRAL FX's chain: its 38 effect modules, its STFT and Partial engines and its dry/wet mixer, unmodified. Not its interface, presets, licensing or update code | MPL-2.0 | `third_party/spectral/spectral-corruptor/` (with its `LICENSE`, `README.md` and `docs/LICENSING.md`); `third_party/licenses/SPECTRAL-CORRUPTOR-MPL-2.0-LICENSE.txt` |
| [WarpCore](https://github.com/ManasWolrd/WarpCore) v0.0.12, commit `d133060` (ManasWolrd) | SPECTRAL FX's WARP CORE: its spectral inversion, unmodified | GPL-3.0 | `third_party/spectral/warpcore/` (with its `LICENSE`); `third_party/licenses/WARPCORE-GPL-3.0-LICENSE.txt` |
| Poltergeist 2's spectral rack (GhostBlock: this repository's `poltergeist-2` branch, commit `cae613d`) | Hosts the two above: the rack of four places and WARP CORE, unmodified | own work, GPL-3; its `sc-engine.cpp` is MPL-2.0 (it adapts lines of Spectral Corruptor) | `third_party/spectral/rack/` |
| [simde](https://github.com/simd-everywhere/simde), commit `60a3a24` | WarpCore's SIMD code is written through it | MIT | fetched at build; `third_party/licenses/SIMDE-MIT-LICENSE.txt` |
| [sst-cpputils](https://github.com/surge-synthesizer/sst-cpputils), commit `67601c8` | A ring buffer inside the rack | MIT | fetched at build; `third_party/licenses/SST-CPPUTILS-MIT-LICENSE.txt` |
| [CLAP](https://github.com/free-audio/clap) headers, commit `195b42a` | Two type names in the rack's header | MIT | fetched at build; `third_party/licenses/CLAP-MIT-LICENSE.txt` |
| IM Fell English (Igino Marini) | The display typeface | SIL OFL 1.1 | `assets/fonts/`, `third_party/licenses/IM-FELL-OFL.txt` |
| Atkinson Hyperlegible (Braille Institute) | The typeface of controls and values | SIL OFL 1.1 | `assets/fonts/`, `third_party/licenses/AtkinsonHyperlegible-OFL.txt` |

Ghost Atlas's own code is AGPL-3.0-or-later, which is what JUCE's open-source terms and the GPL parts above ask for.
The licence texts ride along in `third_party/licenses/` and are copied into the build's `Licenses` folder.

### SPECTRAL FX: what was taken, and under what terms

SPECTRAL FX is Poltergeist 2's FX page as one more effect a slot can hold. Its three parts are in
`third_party/spectral/`, each exactly as it is in Poltergeist 2; `PROVENANCE.sha256` there lists the checksum of every
upstream file, and `verify.sh --clone` compares them with fresh clones of the upstream repositories at the pinned
commits (run on 2026-11-14: all 75 files byte-identical). What follows is the reading these terms were given; it is
not legal advice.

- **Spectral Corruptor is sold by its author**, who also publishes its source under MPL-2.0, all but the official
  licensing backend, its keys, the official binaries and the Factory Presets. None of those is here, and none is used.
  Only `Source/dsp`, `Source/modules` and `Source/parameters` were taken: the effects and their engine. The plug-in
  shell that holds its activation was not, so no activation is switched off or gone round; upstream's own
  `docs/LICENSING.md` (kept beside the code) says the default limitation belongs to its placeholder backend and does
  not narrow the rights MPL-2.0 gives. MPL-2.0 goes file by file: those files stay MPL-2.0, unchanged, with their
  notices, and any change to one would have to be marked and published.
- **WarpCore** is GPL-3.0. One of its headers, `pluginshared/simd.hpp`, comes from a repository of the same author
  that carries no licence file; it is treated as GPL-3.0 with the rest, for the reasons Poltergeist 2's notes give
  (`third_party/spectral/README.md`). That is the one point here that rests on inference.
- **Together with the rest**: MPL-2.0 files may sit in a larger work under the AGPL (MPL-2.0 section 3.3 names it a
  Secondary License, and upstream attached no notice against that), and GPL-3.0 code may be combined with AGPL-3.0
  code (GPL-3.0 section 13). Ghost Atlas is not distributed; if it ever were, these parts would have to go with their
  source and notices.
- Where an effect starts when it is chosen here is Ghost Atlas's own (`src/engine/SpectralRack.cpp`): upstream starts
  most effects at an amount of nothing. No preset of either project is used.

The eight painted worlds behind the screen are frames from the owner's own video clip, enlarged for the screen, and
the worlds' film (`film/*.gafilm`, installed beside the library) is a short run of frames from the same clip for each
world. (They were enlarged with Real-ESRGAN, BSD-3-Clause, used as a tool: none of it is in the build.)
The boat and the figure in it were painted out of every painting and every film frame with LaMa
([advimman/lama](https://github.com/advimman/lama), Apache-2.0), through its TorchScript export `big-lama.pt`
([enesmsahin/simple-lama-inpainting](https://github.com/enesmsahin/simple-lama-inpainting) v0.1.0, Apache-2.0), again
used as a tool: the model is not in the build and is not kept in this repository. The masks that say where the boat is
were drawn by hand (`tools/boat/`).

## The sounds

No sound is inside the plug-in. The starter library is downloaded onto the computer by `library/fetch_library.py`,
from each author's or host's own public download, and nothing in it sits behind a login, a paywall or a "name your
price" page. `library/manifest.json` records, for every library: its name, where it came from, who made it, its
licence, any restrictions, and (once fetched) the checksum or commit of what was fetched. The same record is written
beside the files (`ATLAS-SOURCE.json`), and the Library page shows it for every instrument ("Where the sounds come from").

Libraries marked "by hand" are ones whose pages ask for an account or an agreement: they are listed so they can be
found, but the fetch script does not touch them. Download them yourself and drop the folder on the Library page.

| Library | By | Licence | Fetched |
|---|---|---|---|
| [Glasses of water](https://freepats.zenvoid.org/ChromaticPercussion/glass.html) | FreePats project and its contributors (see credit) | CC0-1.0 | starter set |
| [Hang tuned in D minor](https://freepats.zenvoid.org/ChromaticPercussion/hang.html) | FreePats project and its contributors (see credit) | CC0-1.0 | starter set |
| [Tubular Bells](https://freepats.zenvoid.org/ChromaticPercussion/tubular-bells.html) | FreePats project and its contributors (see credit) | CC0-1.0 | starter set |
| [FM Synthesized Piano #1](https://freepats.zenvoid.org/ElectricPiano/synthesized-piano.html) | FreePats project and its contributors (see credit) | CC0-1.0 | starter set |
| [FM Synthesized Piano #2](https://freepats.zenvoid.org/ElectricPiano/synthesized-piano.html) | FreePats project and its contributors (see credit) | CC0-1.0 | starter set |
| [Kalimba](https://freepats.zenvoid.org/Ethnic/kalimba.html) | FreePats project and its contributors (see credit) | CC0-1.0 | starter set |
| [Concert Harp](https://freepats.zenvoid.org/OrchestralStrings/harp.html) | FreePats project and its contributors (see credit) | CC0-1.0 | starter set |
| [Drawbar organ emulation](https://freepats.zenvoid.org/Organ/electric-organ.html) | FreePats project and its contributors (see credit) | CC0-1.0 | starter set |
| [Church Organ Emulation](https://freepats.zenvoid.org/Organ/pipe-organ.html) | FreePats project and its contributors (see credit) | CC0-1.0 | starter set |
| [Upright Piano KW](https://freepats.zenvoid.org/Piano/acoustic-grand-piano.html) | FreePats project and its contributors (see credit) | CC0-1.0 | starter set |
| [Salamander Grand Piano](https://freepats.zenvoid.org/Piano/acoustic-grand-piano.html) | Alexander Holm | CC-BY-3.0 | starter set |
| [Lately Bass](https://freepats.zenvoid.org/Synthesizer/synth-bass.html) | FreePats project and its contributors (see credit) | CC0-1.0 | starter set |
| [Synth Bass #1](https://freepats.zenvoid.org/Synthesizer/synth-bass.html) | FreePats project and its contributors (see credit) | CC0-1.0 | starter set |
| [Synth Bass #2](https://freepats.zenvoid.org/Synthesizer/synth-bass.html) | FreePats project and its contributors (see credit) | CC0-1.0 | starter set |
| [Synth Brass #1](https://freepats.zenvoid.org/Synthesizer/synth-brass.html) | FreePats project and its contributors (see credit) | CC0-1.0 | starter set |
| [Synth Brass #2](https://freepats.zenvoid.org/Synthesizer/synth-brass.html) | FreePats project and its contributors (see credit) | CC0-1.0 | starter set |
| [Synth Goblins](https://freepats.zenvoid.org/Synthesizer/synth-effects.html) | FreePats project and its contributors (see credit) | CC0-1.0 | starter set |
| [Synth Soundtrack](https://freepats.zenvoid.org/Synthesizer/synth-effects.html) | FreePats project and its contributors (see credit) | CC0-1.0 | starter set |
| [Synth Sci-Fi](https://freepats.zenvoid.org/Synthesizer/synth-effects.html) | FreePats project and its contributors (see credit) | CC0-1.0 | starter set |
| [Synth Crystal](https://freepats.zenvoid.org/Synthesizer/synth-effects.html) | FreePats project and its contributors (see credit) | CC0-1.0 | starter set |
| [Synth Bass & Lead](https://freepats.zenvoid.org/Synthesizer/synth-lead.html) | FreePats project and its contributors (see credit) | CC0-1.0 | starter set |
| [Synth Fifths](https://freepats.zenvoid.org/Synthesizer/synth-lead.html) | FreePats project and its contributors (see credit) | CC0-1.0 | starter set |
| [Synth Lead Square](https://freepats.zenvoid.org/Synthesizer/synth-lead.html) | FreePats project and its contributors (see credit) | CC0-1.0 | starter set |
| [Synth Lead Calliope](https://freepats.zenvoid.org/Synthesizer/synth-lead.html) | FreePats project and its contributors (see credit) | CC0-1.0 | starter set |
| [Synth Pad Choir](https://freepats.zenvoid.org/Synthesizer/synth-pad.html) | FreePats project and its contributors (see credit) | CC0-1.0 | starter set |
| [Sweep Pad](https://freepats.zenvoid.org/Synthesizer/synth-pad.html) | FreePats project and its contributors (see credit) | CC0-1.0 | starter set |
| [New Age](https://freepats.zenvoid.org/Synthesizer/synth-pad.html) | FreePats project and its contributors (see credit) | CC0-1.0 | starter set |
| [Synth Pad Bowed](https://freepats.zenvoid.org/Synthesizer/synth-pad.html) | FreePats project and its contributors (see credit) | CC0-1.0 | starter set |
| [Synth Strings #1](https://freepats.zenvoid.org/Synthesizer/synth-strings.html) | FreePats project and its contributors (see credit) | CC0-1.0 | starter set |
| [Synth Strings #2](https://freepats.zenvoid.org/Synthesizer/synth-strings.html) | FreePats project and its contributors (see credit) | CC0-1.0 | starter set |
| [Wooden Recorder](https://freepats.zenvoid.org/Wind/recorder.html) | FreePats project and its contributors (see credit) | CC0-1.0 | optional |
| [String Cyborgs](https://shop.karoryfer.com/pages/free-string-cyborgs) | Karoryfer Samples | CC0-1.0 | starter set |
| [Caveman Cosmonaut](https://shop.karoryfer.com/pages/free-caveman-cosmonaut) | Karoryfer Samples | CC0-1.0 | optional |
| [Cowsynth](https://shop.karoryfer.com/pages/free-cowsynth) | Karoryfer Samples | CC0-1.0 | starter set |
| [272 Merry Orks](https://shop.karoryfer.com/pages/free-272-merry-orks) | Karoryfer Samples | CC0-1.0 | starter set |
| [Karoryfer x bigcat Cello](https://shop.karoryfer.com/pages/free-karoryfer-x-bigcat-cello) | Karoryfer Samples | CC0-1.0 | starter set |
| [Scarypiano](https://shop.karoryfer.com/pages/free-scarypiano) | Karoryfer Samples | CC0-1.0 | starter set |
| [Weresax](https://shop.karoryfer.com/pages/free-weresax) | Karoryfer Samples | CC0-1.0 | starter set |
| [Meatbass](https://shop.karoryfer.com/pages/free-meatbass) | Karoryfer Samples | CC0-1.0 | optional |
| [Sonatina Symphonic Orchestra](https://github.com/peastman/sso) | Mattias Westlund; SFZ and maintenance by Peter Eastman | CC-Sampling-Plus-1.0 | starter set |
| [Versilian Community Sample Library (selection)](https://github.com/sgossner/VCSL) | Versilian Studios LLC and contributors | CC0-1.0 | starter set |
| [VSCO 2 Community Edition (strings and harp)](https://github.com/sgossner/VSCO-2-CE) | Versilian Studios LLC (Sam Gossner, Simon Dalzell) and contr | CC0-1.0 | starter set |
| [Adventure Kid Waveforms (AKWF)](https://github.com/KristofferKarlAxelEkstrand/AKWF-FREE) | Kristoffer Ekstrand (Adventure Kid) | CC0-1.0 | starter set |
| [Splendid Grand Piano](https://github.com/sfzinstruments/SplendidGrandPiano) | AKAI (public-domain sample CD); SFZ by SFZ Instruments | Public-Domain | starter set |
| [Greg Sullivan E-Pianos](https://github.com/sfzinstruments/GregSullivan.E-Pianos) | Greg Sullivan | CC-BY-3.0 | starter set |
| [MuseScore General](https://musescore.org/en/handbook/3/soundfonts-and-sfz-files) | S. Christian Collins (MuseScore General); Michael Cowgill (F | MIT | starter set |
| [jRhodes3d (1977 Rhodes Mark I Stage 73)](https://github.com/sfzinstruments/jlearman.jRhodes3d) | Jeffrey Learman | CC-BY-NC-4.0 (the samples); CC0-1.0 (the SFZ files) | starter set |
| [Sonic Pi ambient samples](https://github.com/sonic-pi-net/sonic-pi/tree/dev/etc/samples) | Freesound contributors, The Black Dog and others (each one i | CC0-1.0 | starter set |
| [G-Town Church Sampling Project](https://github.com/sfzinstruments/GTownChurchSamplingProject) | Tobias Marberger; SFZ version by S. Christian Collins | CC-Sampling-Plus-1.0 | starter set |
| [Emilyguitar](https://github.com/sfzinstruments/karoryfer.emilyguitar) | Karoryfer Samples | CC0-1.0 | starter set |
| [D. Smolken's Rubner double bass](https://github.com/sfzinstruments/dsmolken.double-bass) | D. Smolken (Karoryfer Samples) | CC0-1.0 | starter set |
| [Shinyguitar](https://github.com/sfzinstruments/karoryfer.shinyguitar) | Karoryfer Samples | CC0-1.0 | optional |
| [Bear Sax](https://github.com/sfzinstruments/karoryfer.bear-sax) | Karoryfer Samples | CC0-1.0 | starter set |
| [Sneakybass](https://github.com/sfzinstruments/karoryfer.sneakybass) | Karoryfer Samples | CC0-1.0 | optional |
| [RJS Vocal Pads](https://www.pianobook.co.uk/packs/rjs-vocal-pads/) | RJS (Pianobook) | Pianobook terms | by hand |
| [Pianobook pads and synths](https://www.pianobook.co.uk/instrument/pads-synths/) | Pianobook contributors | Pianobook terms | by hand |
| [Pianobook choirs and vocals](https://www.pianobook.co.uk/instrument/choirs/) | Pianobook contributors | Pianobook terms | by hand |
| [Minifreak Pads](https://drolez.com/blog/music/ableton-free-sound-packs.php#minifreak) | SHLD Music | CC0-1.0 | by hand |
| [Wavestate Pads](https://drolez.com/blog/music/ableton-free-sound-packs.php#wavestate) | SHLD Music | CC0-1.0 | by hand |
| [Quetzalcoatl factory library](https://github.com/publicsamples/Quetzalcoatl/releases) | modularsamples / publicsamples | unstated for the samples (program: GPL-3.0) | by hand |
| [KBH Real and Swelling Choirs](https://musical-artifacts.com/artifacts/387) | KBH | reported as CC-BY-4.0 (not confirmed here) | by hand |

Things to know about the licences above:

- **CC-BY** libraries (Salamander Grand Piano, Greg Sullivan's E-Pianos) ask for credit when music made with them is
  published. The credit line is in the manifest and on the Library page.
- **Sonatina Symphonic Orchestra** and the **G-Town Church Sampling Project** are under Creative Commons Sampling
  Plus 1.0: their sounds may be used in music, including music that is sold, but not in advertising, and the
  libraries themselves may only be passed on non-commercially, with credit.
- **jRhodes3d** is free to play in any music. Its samples themselves are CC BY-NC 4.0: they may be passed on only
  with credit to Jeffrey Learman and not commercially.
- **MuseScore General** is MIT: its copyright and permission notice (fetched beside the SoundFont) stays with any
  copy. Only 44 of its 309 presets are listed, the ones the library had nothing like.
- **KBH Real and Swelling Choirs** is listed "by hand" because its licence is only reported here (as CC BY 4.0, on
  the MuseScore forum): its own page could not be opened to confirm it, so it is not fetched.

Instruments made by REIMAGINE are rebuilt from library sounds. Each one's notes list what it was made from and those
sources' licences; treat it as carrying all of them.

Commercial sample libraries, ROM content and other people's presets are not included and were not used.
