#!/bin/sh
# Checks that the vendored upstream files are byte-identical to the pinned upstream commits.
#   ./verify.sh            compare against PROVENANCE.sha256 (no network)
#   ./verify.sh --clone    also clone the upstream repos at the pinned commits and compare file by file
#   ./verify.sh --write    regenerate PROVENANCE.sha256 from the files here
set -e
cd "$(dirname "$0")"
BZ_URL=https://github.com/hollance/bombaz;   BZ_SHA=434d8b6eaf228338b0b2d2f56ad470c1f8929ddd
SH_URL=https://github.com/ardura/Subhoofer;  SH_SHA=19ea26b500fe0f3fc0516bde02b565af80501ae6
sum() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$@"; else shasum -a 256 "$@"; fi; }
list() { find bombaz subhoofer -type f | LC_ALL=C sort; }
if [ "$1" = "--write" ]; then list | while read -r f; do sum "$f"; done > PROVENANCE.sha256; echo "wrote PROVENANCE.sha256 ($(wc -l < PROVENANCE.sha256) files)"; exit 0; fi
list | while read -r f; do sum "$f"; done > /tmp/p2sub-provenance.$$
if diff -q /tmp/p2sub-provenance.$$ PROVENANCE.sha256 >/dev/null; then echo "ok: $(wc -l < PROVENANCE.sha256) vendored files match PROVENANCE.sha256"; else echo "MISMATCH against PROVENANCE.sha256:"; diff /tmp/p2sub-provenance.$$ PROVENANCE.sha256 || true; rm -f /tmp/p2sub-provenance.$$; exit 1; fi
rm -f /tmp/p2sub-provenance.$$
[ "$1" = "--clone" ] || exit 0
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
get() { git init -q "$T/$1" && git -C "$T/$1" remote add origin "$2" && git -C "$T/$1" fetch -q --depth 1 origin "$3" && git -C "$T/$1" checkout -q FETCH_HEAD; }
get bz "$BZ_URL" "$BZ_SHA"; get sh "$SH_URL" "$SH_SHA"
bad=0
cmpf() { if cmp -s "$1" "$2"; then :; else echo "DIFFERS: $1"; bad=1; fi; }
for f in LICENSE.txt README.md; do cmpf "bombaz/$f" "$T/bz/$f"; done
for f in $(cd bombaz/src && find . -type f | LC_ALL=C sort); do cmpf "bombaz/src/$f" "$T/bz/src/$f"; done
for f in $(cd subhoofer && find . -type f | LC_ALL=C sort); do cmpf "subhoofer/$f" "$T/sh/$f"; done
[ "$bad" = 0 ] && echo "ok: every vendored file is byte-identical to its pinned upstream commit" || exit 1
