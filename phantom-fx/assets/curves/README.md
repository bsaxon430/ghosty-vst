# Curve sets for the CURVES engine (WRECKER, CRACKLE, TUBE GLOW)

PRIVATE. These 52 sets come from the waveshaper presets Brandon downloaded on 2026-11-10 from Aspen Instruments' community page
(52 `.bddpreset` files). The presets name their source as "Trash 2 Waveshapes" (iZotope Trash 2's curves), and the page states no
terms. They are fine to load in Brandon's own build on his own Mac; they must never be bundled in a public release.

Format (`.gbc`): "GBCV", u32 version (1), u32 curves, u32 points, then curves x points float32 little-endian; curve k is
input -1..+1 (point i at x = -1 + 2 i / (points - 1)) -> output, ordered mild to heavy (the presets' own "[+N]" steps).
Made by `tools/curves/` (the preset reader and exporter) from the original zip; 16384-point tables decimated to 8193 points.
The three equation presets (DOOMCOMP, DOOMCOMPPARALLEL, INFLATORFULL) are rendered from their equations at m = 0, 1/8 .. 1.
