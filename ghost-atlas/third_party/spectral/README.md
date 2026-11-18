# libs/ghostblock-spectral - the upstream DSP behind Poltergeist 2's SPECTRAL FX

Vendored, UNMODIFIED source files of two open-source projects (byte-identical to the upstream commits below;
`PROVENANCE.sha256` lists every file's SHA-256 and `verify.sh` re-checks them against fresh clones).
Our own code that hosts them lives in `src/ghostblock/spectral/`.

| folder | upstream | commit | licence |
| --- | --- | --- | --- |
| `spectral-corruptor/` | https://github.com/Dirty-Octopus/spectral-corruptor-plugin (tag v0.2.6) | `81d39c709f01ff609d4ce6435f3205f2e32f9b3a` | MPL-2.0 (`spectral-corruptor/LICENSE`) |
| `warpcore/src/dsp`, `warpcore/src/global.hpp` | https://github.com/ManasWolrd/WarpCore (v0.0.12) | `d1330606340a1b19515c7b155876fab3d79b430a` | GPL-3.0 (`warpcore/LICENSE`) |
| `warpcore/src/pluginshared/simd.hpp` | https://github.com/ManasWolrd/pluginshared (WarpCore's submodule `src/pluginshared`) | `6c5bda2423651cc2c9466aaacf9e0fdb5e6ebb79` | see below |

## What was taken, and what was deliberately not

**Spectral Corruptor** (Dirty Octopus): `Source/dsp`, `Source/modules`, `Source/parameters` - the DSP engine, the 38 effect
modules and their parameter definitions - plus `LICENSE`, `THIRD_PARTY_NOTICES.md`, `README.md` and `docs/LICENSING.md`
(kept because they state the terms). Every one of these source files carries `SPDX-License-Identifier: MPL-2.0`.

NOT taken, not called, not emulated: `Source/licensing/` (the placeholder licence backend that blocks audio in upstream's
own plugin), `Source/updates/` (update service, host registration), `Source/gui/` (their interface), `PluginProcessor.*`,
`PluginEditor.*`, `PresetManager.h`, `RandomPreset.h`, `MacroRouting.cpp`, `StandaloneApp.cpp`, `third_party/` (md4c, JUCE 8.0.4),
`tests/`, and the Factory Presets (not in the upstream repo; they belong to the paid edition).
The three folders we took contain no reference to the licence backend (`grep -ri licens Source/{dsp,modules,parameters}`
finds only the SPDX lines). Upstream's README and docs/LICENSING.md say the DSP is public, that anyone may complete and
build it, and that "the default runtime limitation describes the supplied placeholder; it does not add restrictions to the
rights granted by MPL-2.0". We do not use their plugin shell at all, so there is nothing to bypass.

Which of the vendored files the plugin compiles: `Source/modules/*.h`, `Source/dsp/{FFTEngine,PartialProcessor,DryWetMixer}.cpp`
and their headers, `Source/dsp/{FrequencySplit,TripleBuffer,RemapFormulaRuntime}.h`, `Source/parameters/{TransferCurve,RemapFormula}.h`.
The rest of `Source/dsp` and `Source/parameters` (SpectralProcessor, Oversampler, GainStage, ParamIDs, ParameterLayout, ...) is
upstream's own rack engine; only the test program compiles it, as the REFERENCE our hosting is checked against.

**WarpCore** (ManasWolrd): `src/dsp/*` (the spectral-inversion DSP and its SIMD variants) and `src/global.hpp`.
NOT taken: the JUCE plugin shell (`PluginProcessor`, `PluginEditor`, `ui/`), presets, and the submodules
`libs/JUCE`, `libs/AudioFFT` (unused upstream too), `libs/simde` (this repo already has simde, MIT, in `libs/simde`) and
`libs/cpp_simd_detector`.
`src/dsp/dsp_dispatch.cpp` is vendored with the rest of the folder but NOT compiled: it needs `cpp_simd_detector`
(github.com/DeltaPavonis/cpp_simd_detector), which has no licence at all, so we cannot ship it. Choosing the SIMD variant for
the CPU is done by our own `src/ghostblock/spectral/warpcore-dispatch.cpp` (same order: AVX2+FMA, AVX2, AVX, SSE4.1, SSE2; NEON on arm64).

`pluginshared/simd.hpp`: the standalone `pluginshared` repository has no licence file. It is by WarpCore's author, WarpCore
(GPL-3) cannot be built without it, and the same author publishes the same header (a later revision) inside
https://github.com/ManasWolrd/dev-plugins, whose `LICENSE.txt` is GPL-3 and covers it in-tree. We therefore treat it as
GPL-3 like the rest of WarpCore. This is the one licence point that rests on inference rather than on a licence file next
to the code; it is recorded in `ghostblock/packaging/THIRD-PARTY-LICENSES.txt` and in the project notes.

## Licence fit
Poltergeist 2 as a whole is GPL-3 (see `ghostblock/README.md`).
- MPL-2.0 code may be combined into a GPL-3 "Larger Work" (MPL-2.0 section 3.3; upstream attached no Exhibit B
  "Incompatible With Secondary Licenses" notice). MPL is per file: these files stay MPL-2.0, keep their headers, and
  any change to them would have to be marked and published. We have changed none. `src/ghostblock/spectral/sc-engine.cpp`
  adapts a few dozen lines of upstream's `SpectralProcessor.cpp` and is therefore itself MPL-2.0 (its header says which parts).
- GPL-3 (WarpCore) into GPL-3: no conflict.

## Updating
Never edit these files. To move to a newer upstream commit: copy the same paths from a clean clone, update the commit ids
above, run `./verify.sh --write` to regenerate `PROVENANCE.sha256`, rebuild, run the tests (the reference test compares
our hosting against upstream's own engine).
