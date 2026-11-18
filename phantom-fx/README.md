# PHANTOM FX

GhostBlock's open-source 24-effect rack: the successor to Ghost Box, rebuilt from the ground up on open-source DSP.
One JUCE 8 / C++20 plugin (VST3 + CLAP + Standalone; AU behind a switch), AGPL-3.0-or-later.

- `AUDIT.md` - every one of the 24 effects' controls and behaviour (from the Ghost Box code + manual) and what each DSP foundation really contains.
- `THIRD_PARTY.md` - every outside project, its exact licence (read in the clone), commit and files taken. `third_party/licenses/` holds the texts.
- `DECISIONS.md` - every departure from the rebuild brief, with the reason.
- `src/core/PhantomEffect.h` - the one interface every effect implements. `src/dsp/` - the shared layer (smoothing, oversampling policy,
  modulators, delay lines, crossovers). `src/rack/` - the rack. `src/effects/` - the effect modules. `src/plugin/` - the host shell. `src/ui/` - the look.
- `tests/PhantomTests.cpp` - the quality-bar check table, run in CI on every commit (exit code = failures).

## Build
```
cmake -S phantom-fx -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target PhantomFX_VST3 PhantomFX_Standalone PhantomFX_CLAP PhantomTests --parallel
build/PhantomTests_artefacts/Release/PhantomTests
```
Dependencies are fetched at pinned commits (see CMakeLists.txt). `-DPHANTOM_JUCE_DIR=<path>` uses a local JUCE 8.0.4;
`-DFETCHCONTENT_SOURCE_DIR_<NAME>=<path>` uses local copies of the others.

## Phases
0 Audit (done) - 1 Skeleton (done: interface, shared DSP layer, test harness, CI on the Mac, pluginval 10, installed) - 2 Rack - 3 Colour - 4 Space - 5 Rhythm - 6 Master - 7 Release.
Phases 2-6b are done (0.1.8 is the installed build). 0.1.9 adds the painted looks (branch `phantom-fx-looks`, waiting for its Mac run).
0.1.10 (branch `phantom-fx-p7`) works through the leftover list: the limiter's gain-reduction meter, drawn patterns in scenes / A-B /
presets, SPIRIT EQ's linear-phase modes and spectrum display, RETRO's tape-bias aliasing, SPATIAL at 88.2-192 kHz, the first factory
bank. Still open for Phase 7: a signed / notarised macOS build and a GitHub release (both need the Mac and Brandon's say-so), the manual.
