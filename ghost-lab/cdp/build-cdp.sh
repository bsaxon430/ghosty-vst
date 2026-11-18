#!/bin/bash
# Builds the CDP8 programs Ghost Lab runs, from the official repo (ComposersDesktop/CDP8, LGPL-2.1-or-later), at a pinned
# commit, following its building.txt (CMake; portaudio is only for CDP's play/record programs, which Ghost Lab doesn't use,
# so it is switched off). Patches in cdp/patches/*.patch (our LGPL changes to CDP files, if any) are applied first.
#   build-cdp.sh <out-dir> [src-dir]
# On a Mac it builds each architecture separately and joins them with lipo (universal: Apple Silicon + Intel).
# CDP_ARCHS="x86_64" builds one architecture only (Brandon's Intel Mac runner, which has no use for the arm64 half).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$1"; SRC="${2:-$PWD/cdp8-src}"
COMMIT="$(grep -E '^commit' "$HERE/CDP-PINNED.txt" | awk '{print $2}')"
PROGS="pvoc stretch strange focus blur modify combine morph repitch formants envel cdparams"
if [ ! -d "$SRC/.git" ]; then
  git clone -q https://github.com/ComposersDesktop/CDP8.git "$SRC"
fi
git -C "$SRC" fetch -q origin "$COMMIT" 2>/dev/null || true
git -C "$SRC" checkout -q "$COMMIT"
for p in "$HERE"/patches/*.patch; do [ -f "$p" ] && git -C "$SRC" apply "$p"; done
mkdir -p "$OUT"
build_one () {  # build_one <build-dir> [extra cmake args...]
  local B="$1"; shift
  rm -rf "$SRC/NewRelease"
  cmake -S "$SRC" -B "$B" -DUSE_LOCAL_PORTAUDIO=OFF -DCMAKE_BUILD_TYPE= "$@" > "$B.cfg.log" 2>&1 || { tail -40 "$B.cfg.log"; exit 1; }
  cmake --build "$B" -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)" --target $PROGS > "$B.build.log" 2>&1 || { grep -E "error" "$B.build.log" | head -40; exit 1; }
}
if [ "$(uname)" = "Darwin" ]; then
  ARCHS="${CDP_ARCHS:-arm64 x86_64}"
  for arch in $ARCHS; do
    build_one "$SRC/build-$arch" -DCMAKE_OSX_ARCHITECTURES=$arch -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 -DUSE_COMPILER_OPTIMIZATIONS=OFF -DCMAKE_C_FLAGS="-O2 -arch $arch" 
    mkdir -p "$SRC/out-$arch"; for p in $PROGS; do cp "$SRC/NewRelease/$p" "$SRC/out-$arch/"; done
  done
  for p in $PROGS; do lipo -create $(for arch in $ARCHS; do echo "$SRC/out-$arch/$p"; done) -output "$OUT/$p"; done
else
  build_one "$SRC/build-ghostlab"
  for p in $PROGS; do cp "$SRC/NewRelease/$p" "$OUT/"; done
fi
chmod +x "$OUT"/*
ls -la "$OUT"
