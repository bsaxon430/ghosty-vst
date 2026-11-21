# 👻 Ghost Graph · v1 beta

**Music written as equations.** Every voice is a line on a graph: `x` is time (in beats) and `y` is pitch (in octaves). The plugin draws the lines as they play, with glowing dots on the playheads, and you can type in the math or drag the notes and the sound changes as you go.

Part of the free [GhostBlock plugins](https://github.com/bsaxon430/ghosty-vst). VST3 for FL Studio and Ableton Live on Mac.

- **TRACKS**: six ready-made songs, each a few scenes of equations
- **GENERATE**: writes a new song in equations (Drift, Riff house, Breaks, Cosine song or Acid)
- **DRAG**: the song split into its parts (kick, snare, hi-hat, bass, lead, pad ...), each one a WAV and a MIDI to drag into your DAW, plus the whole mix
- **Tempo** on the main page, with SYNC to follow your DAW
- **EDIT**: type in the math, or drag notes on the graph to move, stretch or tilt them; every drag rewrites the voice's own formula in place. Click a note to see where it is in the math, right-click to copy, paste or delete, Cmd+drag a box (or Cmd+click) to pick several and move them together
- **LOOK**: PAPER, CHROMA, INK, WINE and TI 84
- Six waves (sine, triangle, saw, square, soft saw, round), a KIT that turns voices named hats / snare / clap into drums, and TUNE, GLIDE, TONE, SPACE, WIDTH and VOLUME

## Build

```
cmake -S ghost-graph -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target GhostGraph_VST3
```

JUCE 8 is fetched by CMake. Add `-DGHOSTGRAPH_TESTS=ON` and build `GhostGraphTests` for the checks.

## Licence

GNU AGPLv3 (see `LICENSE`). Credits are in `ghost-graph/packaging/CREDITS.txt`.
