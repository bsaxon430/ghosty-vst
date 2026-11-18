#!/bin/sh
# Checks that the vendored upstream files are byte-identical to the pinned upstream commits.
#   ./verify.sh            compare against PROVENANCE.sha256 (no network)
#   ./verify.sh --clone    also clone the three upstream repos at the pinned commits and compare file by file
#   ./verify.sh --write    regenerate PROVENANCE.sha256 from the files here
set -e
cd "$(dirname "$0")"
SC_URL=https://github.com/Dirty-Octopus/spectral-corruptor-plugin; SC_SHA=81d39c709f01ff609d4ce6435f3205f2e32f9b3a
WC_URL=https://github.com/ManasWolrd/WarpCore;                     WC_SHA=d1330606340a1b19515c7b155876fab3d79b430a
PS_URL=https://github.com/ManasWolrd/pluginshared;                 PS_SHA=6c5bda2423651cc2c9466aaacf9e0fdb5e6ebb79
sum() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$@"; else shasum -a 256 "$@"; fi; }
list() { find spectral-corruptor warpcore -type f | LC_ALL=C sort; }
if [ "$1" = "--write" ]; then list | while read -r f; do sum "$f"; done > PROVENANCE.sha256; echo "wrote PROVENANCE.sha256 ($(wc -l < PROVENANCE.sha256) files)"; exit 0; fi
list | while read -r f; do sum "$f"; done > /tmp/p2-provenance.$$
if diff -q /tmp/p2-provenance.$$ PROVENANCE.sha256 >/dev/null; then echo "ok: $(wc -l < PROVENANCE.sha256) vendored files match PROVENANCE.sha256"; else echo "MISMATCH against PROVENANCE.sha256:"; diff /tmp/p2-provenance.$$ PROVENANCE.sha256 || true; rm -f /tmp/p2-provenance.$$; exit 1; fi
rm -f /tmp/p2-provenance.$$
[ "$1" = "--clone" ] || exit 0
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
get() { git init -q "$T/$1" && git -C "$T/$1" remote add origin "$2" && git -C "$T/$1" fetch -q --depth 1 origin "$3" && git -C "$T/$1" checkout -q FETCH_HEAD; }
get sc "$SC_URL" "$SC_SHA"; get wc "$WC_URL" "$WC_SHA"; get ps "$PS_URL" "$PS_SHA"
bad=0
cmpf() { if cmp -s "$1" "$2"; then :; else echo "DIFFERS: $1"; bad=1; fi; }
for f in $(cd spectral-corruptor && find . -type f | LC_ALL=C sort); do cmpf "spectral-corruptor/$f" "$T/sc/$f"; done
for f in $(cd warpcore && find . -type f ! -path './src/pluginshared/*' | LC_ALL=C sort); do cmpf "warpcore/$f" "$T/wc/$f"; done
cmpf warpcore/src/pluginshared/simd.hpp "$T/ps/simd.hpp"
[ "$bad" = 0 ] && echo "ok: every vendored file is byte-identical to its pinned upstream commit" || exit 1
