#!/bin/bash
# Builds the "Ghost Bass" data folder (what the installer puts in ~/Library/Application Support/Ghost Bass,
# or GhostBassData next to a portable Linux build).
# Build 8 (PURE SURGE): the library is Surge XT's own, unchanged - its factory patches, wavetables, FX and
# modulator presets, impulses and tunings from this tree (the upstream import), and its third-party patches
# and wavetables from upstream Surge XT at the same commit (they were left out of the import for size).
# Our own presets (ghost/extras/GHOST EXTRAS) are not part of the library: hidden by default.
#   ghost/tools/make_data.sh <repo root> <output folder> [<upstream Surge XT resources/data> for the 3rd-party sets]
set -e
SRC="$1"; OUT="$2"; UP="${3:-}"
D="$SRC/resources/data"
rm -rf "$OUT"; mkdir -p "$OUT"
for d in patches_factory wavetables fx_presets modulator_presets tuning_library skins impulses_factory; do
  [ -d "$D/$d" ] && cp -R "$D/$d" "$OUT/"
done
if [ -n "$UP" ]; then
  for d in patches_3rdparty wavetables_3rdparty impulses_3rdparty; do
    [ -d "$UP/$d" ] && cp -R "$UP/$d" "$OUT/"
  done
fi
cp "$SRC/LICENSE" "$OUT/LICENSE-GPL3.txt"
cp "$SRC/ghost/docs/CREDITS.txt" "$OUT/CREDITS.txt"
echo "Ghost Bass data -> $OUT ($(du -sh "$OUT" | cut -f1); $(find "$OUT" -name '*.fxp' | wc -l) patches)"
