#!/bin/bash
# SEANCE (GhostBlock) - BEAST: gets Surge XT at the commit this plugin is built against, with only the libraries its
# engine needs (no JUCE, no Lua).   fetch-surge.sh <dir>
set -euo pipefail
SHA=529bb52fe6fd79777ed4ed9287aeb1b575465c61
D="$1"
if [ "$(git -C "$D" rev-parse HEAD 2>/dev/null || true)" != "$SHA" ]; then
  mkdir -p "$D"; cd "$D"
  [ -d .git ] || git init -q
  git remote remove origin 2>/dev/null || true
  git remote add origin https://github.com/surge-synthesizer/surge.git
  git fetch -q --depth 1 origin "$SHA"
  git checkout -q -f FETCH_HEAD
fi
cd "$D"
git submodule update --init --depth 1 libs/eurorack/eurorack libs/fmt libs/simde libs/tuning-library libs/PEGTL libs/pffft \
  libs/sst/sst-basic-blocks libs/sst/sst-cpputils libs/sst/sst-effects libs/sst/sst-filters libs/sst/sst-plugininfra \
  libs/sst/sst-waveshapers libs/sst/sst-cmake libs/r8brain-free-src libs/zstd libs/oddsound-mts/MTS-ESP
(cd libs/sst/sst-plugininfra && git submodule update --init --depth 1 --recursive)
echo "Surge XT at $(git rev-parse --short HEAD) in $D"
