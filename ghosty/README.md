# Ghosty Pro 2 on Vital's engine (free, GPL-3)

Ghosty Pro 2: Ghosty Pro rebuilt on Vital's open-source synth engine (mtytel/vital @ 636ca0e, GPL-3). Mac: VST3, AU, standalone.
Its own plug-in ("Ghosty Pro 2", id io.ghostblock.ghostypro2, AU Ghbk/Gpr2, version 2.0.16), installed next to the old Ghosty Pro 3.x.

## What is Vital's (unchanged)
Everything that makes sound or decides how the synth works:
- the wavetable oscillators, sampler, filters, envelopes, LFOs, random sources, voices, MPE, tuning;
- the modulation system (the matrix and drag-to-modulate on the SYNTH page);
- the nine effects (chorus, compressor, delay, distortion, EQ, filter, flanger, phaser, reverb);
- the `.vital` preset format, saving and loading (`SynthBase`, `LoadSave`), bank import steps;
- Vital's whole editor: it IS the SYNTH page (oscillators and wavetable editor, filters, envelopes, LFOs, modulation matrix,
  effects, voice settings), drawn in Ghosty Pro's looks through Vital's own skin system.

All of it lives in `src/`, `third_party/`, `fonts/`, `icons/`, `tunings/`, compiled through Vital's own unity build files.
The only build glue: `CMakeLists.txt`, one renamed symbol (`createPluginFilter` in Vital's plugin file, by a compile
definition) and our ghost art in place of Vital's logo files (its trademark). Vital's own factory presets are not here.

## What is Ghosty Pro's (ours, in `ghosty/`)
- the Ghosty Pro window: header (the preset picker, save / load, looks, bypass) and tabs SYNTH / MORPH / EXTRAS;
- TEMPO 1/2x | 1x | 2x: our synced parts get host tempo x the multiple, and Vital's engine gets it through its normal host transport (tempo and beat position both scaled); at 1x the engine sees the host's transport untouched;
- the preset picker: every preset on the computer by TYPE (from its own preset_style tag, then name and folder words), each type one flat A-Z list, a search, < > through the type;
- the looks as Vital skins (2.0.16: GHOST, BLACK, LIGHT) (`resources/skins`, made by `tools/make_skins.py` from Vital's default skin), our ghost art;
- LIGHT (2.0.15 SKETCH, renamed in 2.0.16; PURPLE and the old flat LIGHT removed): a hand-drawn watercolor face:
  rice paper, pencil lines that run past the corners, soft washes that bleed and pool at the edges (`shared/Watercolor.h`), a
  watercolor ghost; Vital's pages get it as a skin (`ghosty_sketch.vitalskin`: paper, graphite, one wash colour per section);
- MORPH / SPIRIT BOARD: ghosts hold sounds' knob settings, the puck blends them; MOTION moves the puck by itself (STILL,
  CIRCLE, EIGHT, WANDER, GLIDE ghost to ghost, STEP one ghost at a time, DRAWN path), locked to the DAW's tempo at
  1/16 triplet .. 8 bars or free in Hz (off by default);
- GHOSTY EXTRAS: GHOST GATE and HAUNT from Ghosty Pro's rack, the effects Vital has no version of (off by default);
- the preset finder: finds the `.vital` presets already on the computer (read: file name + type tag) and imports `.vitalbank` banks;
- 34 Ghosty Pro presets, written from scratch (`tools/make_presets.py`) and level-fitted on their own volume knob.

While MORPH and the extras are off, Ghosty Pro's audio is exactly Vital's: the tests check our `processBlock` is
bit-identical to Vital's `SynthPlugin::processBlock`, and `GhostyProTool nulltest` checks presets render bit-identically
to Vital's own headless build (`src/headless/main.cpp`, unchanged).

## Build and test
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target GhostyPro_VST3 GhostyProTool UpstreamHeadless
    build/GhostyProTool_artefacts/Release/GhostyProTool test
    build/GhostyProTool_artefacts/Release/GhostyProTool nulltest build/UpstreamHeadless_artefacts/Release/UpstreamHeadless nt <folder of .vital>
Mac builds run in CI (`.github/workflows/ghostypro-vital-mac.yml`) when `ghosty/BUILD` changes.

## Presets and the study
`docs/STUDY-PRESETS.md` holds aggregate numbers from four free preset packs (no patches). Our presets are checked to be
far from every studied preset by `tools/check_distance.py` (`docs/PRESET-DISTANCE.md`). The packs themselves are never in
this repository.
