# Ghost Mosaic - licence notes and credits

Ghost Mosaic is a fork of **Samplebrain** by **Then Try This** (https://thentrythis.org,
source https://gitlab.com/then-try-this/samplebrain), free software under the
**GNU General Public License, version 2 or (at your option) any later version** (LICENSE in
this folder). Ghost Mosaic is released under the same licence. Thank you to Then Try This and
every Samplebrain contributor: the sound of Ghost Mosaic is theirs.

## What is whose

| Part | Where | Licence |
|---|---|---|
| Samplebrain's engine (brain, block, search, renderer, window, spiralcore sample/stream code), its manual, Qt app and GUI (kept for reference) | `samplebrain/` (git subtree of upstream `production` 30e290d, full history) | GPL-2.0-or-later, copyright Then Try This and contributors (see each file's header) |
| Aquila DSP (MFCC, mel filters, DCT), inside Samplebrain | `samplebrain/brain/src/aquila` | MIT, copyright Zbigniew Siciarz (`packaging/Licenses/AQUILA-MIT-LICENSE.txt`) |
| FFTW 3.3.10, the FFT the engine is written against (built from the official release, static) | `cmake/build-fftw.sh` | GPL-2.0-or-later, copyright Matteo Frigo and MIT (`packaging/Licenses/FFTW-COPYRIGHT.txt`) |
| JUCE 8.0.4 (plug-in formats, UI) | fetched at build time | AGPLv3 / commercial dual licence (`packaging/Licenses/JUCE-AGPLv3-NOTE.txt`) |
| The live view (DOTS) | `Source/Plugin/Swarm.cpp` | port of Bencho's "Particles", MIT (`packaging/Licenses/BENCHO-MIT-LICENSE.txt`); comments kept |
| The wordmark and the HEAT view | `Source/Plugin/HeatWord.cpp`, `Swarm.cpp` | port of Bencho's "Heat map", MIT (same file); comments kept |
| TERRAIN, the landscape control (shared by the GhostBlock plug-ins) | `../ghosty-pro/Shared/Terrain` | GhostBlock's own code, used here under GPL-2.0-or-later as part of this program |
| Poppins font (embedded, unmodified) | `fonts/` | SIL Open Font License 1.1 (`fonts/Poppins-OFL.txt`) |
| Everything else in this folder (plug-in, realtime renderer port, live input, limiter, OSC bridge, UI, built-in sounds, tests) | `Source/`, `compat/`, `tests/` | GPL-2.0-or-later, GhostBlock |

## Changes to Samplebrain's own code

Three, marked in the code:
- "Ghost Mosaic patch 1", `samplebrain/brain/src/block.h` / `block.cpp`: the FFT and MFCC
  scratch objects are one per thread instead of one per process, so several threads and
  plug-in instances can analyse at once. The maths is untouched; the null tests show the
  output is bit-identical to upstream.
- "Ghost Mosaic patch 2" (build 9, the TERRAIN page), `search_params.h` / `block.cpp`: an
  optional weight per FFT band in the matching distance. Unset (the default, and whenever the
  landscape is flat) the original loops run unchanged; the null tests still pass. It is not
  written to session files, which stay upstream's format.
- "Ghost Mosaic patch 3" (build 10), `search_params.h` / `block.cpp`: an optional factor per
  library piece that scales its distance (TERRAIN's darker / brighter preference). Unset, the
  original expression runs unchanged. Not written to session files.

Everything else is new code around the engine: `RtRenderer` is a port of upstream's
`renderer` that never allocates on the audio thread (its output is tested bit-identical to
`renderer` for every search style), and `compat/` stands in for libsndfile, liblo and
PortAudio so the engine compiles as upstream wrote it.

## The sounds

The built-in sounds and targets (`Source/Engine/DemoKits.cpp`) are synthesised by Ghost
Mosaic from scratch - no recordings, no sample packs - and are dedicated to the public domain
(CC0). The demo renders use only these. Upstream's demo session is used by the tests only
and is not shipped.
