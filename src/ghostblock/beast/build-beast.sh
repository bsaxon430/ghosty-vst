#!/bin/bash
# SEANCE (GhostBlock) - builds BEAST: Surge XT's engine + beast.cpp, linked into ONE object that shows only beast_*.
#   build-beast.sh <surge checkout> <build dir> [cmake args...]      ->  <build dir>/seance-beast.o
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"; SURGE="$1"; B="$2"; shift 2
EXTRA=()
# GCC marks some inline statics "unique", which cannot be made local afterwards: ask it not to (Linux only; the Mac has none)
if [ "$(uname)" != Darwin ]; then EXTRA+=("-DCMAKE_CXX_FLAGS=-fno-gnu-unique"); fi
cmake -S "$SURGE" -B "$B" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PROJECT_INCLUDE="$HERE/beast-inject.cmake" \
  -DSURGE_SKIP_JUCE_FOR_RACK=TRUE -DSURGE_SKIP_LUA=TRUE -DSURGE_SKIP_VST3=TRUE -DSURGE_SKIP_STANDALONE=TRUE -DSURGE_SKIP_ALSA=TRUE \
  -DSURGE_SKIP_JACK=TRUE -DSURGE_SKIP_ODDSOUND_MTS=TRUE -DSURGE_SKIP_WERROR=TRUE -DSURGE_COMPILE_BLOCK_SIZE=8 -DSURGE_BUILD_CLAP=FALSE \
  -DSURGE_BUILD_XT=FALSE -DSURGE_BUILD_FX=FALSE -DSURGE_BUILD_TESTRUNNER=OFF -DSURGE_RELIABLE_VERSION_INFO=OFF -DENABLE_LTO=OFF \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_CXX_VISIBILITY_PRESET=hidden -DCMAKE_C_VISIBILITY_PRESET=hidden -DCMAKE_VISIBILITY_INLINES_HIDDEN=ON ${EXTRA[@]+"${EXTRA[@]}"} "$@"
cmake --build "$B" --target beast beast-check --parallel "${BEAST_JOBS:-$(getconf _NPROCESSORS_ONLN)}"
CHK=$(find "$B" -type f -name beast-check -perm -u+x | head -n 1)
# BEAST_ARCH=x86_64|arm64 builds the object for that architecture (a universal SEANCE joins both); its check runs only natively
if [ -z "${BEAST_ARCH:-}" ] || [ "$BEAST_ARCH" = "$(uname -m)" ]; then "$CHK"; fi
# every static library the build made, linked into one relocatable object; only beast_* stays visible
LIBS=$(find "$B" -name "*.a" | sort)
OUT="$B/seance-beast.o"
if [ "$(uname)" = Darwin ]; then
  printf '_beast_*\n' > "$B/beast-exports.txt"
  # -all_load: take every member (static initialisers included); what nothing uses is dropped by the plugin's own dead-strip
  ld -r -arch "${BEAST_ARCH:-$(uname -m)}" -o "$OUT" -exported_symbols_list "$B/beast-exports.txt" -all_load $LIBS
  # " C " = common symbols (two zero-initialised C globals inside Surge's libraries): the export list cannot hide
  # them and this Mac's linker has no -d; they hold no code and cannot collide with a function, so they are allowed
  nm -g "$OUT" | grep -v " U " | grep -v " C " | grep -v "_beast_" | head -n 5 > "$B/leaks.txt" || true
else
  BL=$(find "$B" -name "libbeast.a" | head -n 1)
  ld -r --force-group-allocation -o "$B/seance-beast-all.o" --whole-archive "$BL" --no-whole-archive --start-group $LIBS --end-group
  nm -g --defined-only "$B/seance-beast-all.o" | awk '{print $3}' | grep '^beast_' > "$B/beast-exports.txt"
  objcopy --keep-global-symbols="$B/beast-exports.txt" "$B/seance-beast-all.o" "$OUT"
  nm -g --defined-only "$OUT" | grep -v " beast_" | head -n 5 > "$B/leaks.txt" || true
fi
if [ -s "$B/leaks.txt" ]; then echo "BEAST: names other than beast_* are visible:"; cat "$B/leaks.txt"; exit 1; fi
ls -la "$OUT"
