# Ghosty VST — notes for Claude Code

JUCE 8 (fetched by CMake FetchContent, tag 8.0.4) project producing two VST3 + Standalone instruments for Ableton on Windows 11 x64:

- `GhostySynth` → "Ghosty" (Synth/*, wavetable synth, MIDI in)
- `GhostyBeats` → "Ghosty Beats" (Beats/*, 8x16 drum sequencer synced to host ppq/tempo, MIDI notes 36–43 trigger tracks)
- `Shared/` header-only: `GhostArt.h` (ghost vector art via `juce::Drawable::parseSVGPath`, palette, NeonRain, wordmark), `GhostUI.h` (LookAndFeel, GhostKnob, HoldButton, XYGhostPad bound to two parameters per ghost), `Fx.h` (drive/crush/DJ filter/chorus/phaser/echo/haunt/reverb).

Build: `INSTALL-GHOSTY.bat` (self-elevates, installs VS 2022 Build Tools via vs_BuildTools.exe if missing, uses the CMake bundled with VS, Visual Studio 17 2022, x64, Release, copies VST3s to Common Files\VST3). JUCE comes from a zip URL, so Git is not required. Output: `build/<Target>_artefacts/Release/VST3/*.vst3`.

This code was written without access to a compiler. If it fails to build, fix the errors with minimal changes and keep behaviour/look the same. Likely spots: JUCE 8 API changes (Font/FontOptions, GlyphArrangement), braced-init narrowing, missing includes, MSVC warnings treated as errors. Real-time rules: no allocation or locks in `processBlock`/`renderNextBlock`.

Owner doesn't code — after fixing, rerun `INSTALL-GHOSTY.bat` and tell him to Rescan plug-ins in Ableton.
