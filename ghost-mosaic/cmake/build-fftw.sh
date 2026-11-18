#!/bin/bash
# Builds FFTW 3.3.10 (GPL-2.0-or-later, https://www.fftw.org) from the official release tarball:
# static, double precision (what Samplebrain's fft.cpp uses), thread-safe planner combined into
# libfftw3.a, position-independent (it goes into plug-in bundles). On a Mac it is built universal.
#   cmake/build-fftw.sh <work dir> <install prefix>
set -euo pipefail
WORK=${1:?work dir}; PREFIX=${2:?install prefix}
URL=https://www.fftw.org/fftw-3.3.10.tar.gz
SHA=56c932549852cddcfafdab3820b0200c7742675be92179e59e6215b340e26467
mkdir -p "$WORK"; cd "$WORK"
[ -f fftw-3.3.10.tar.gz ] || curl -fsSL -o fftw-3.3.10.tar.gz "$URL"
if command -v sha256sum >/dev/null; then echo "$SHA  fftw-3.3.10.tar.gz" | sha256sum -c -; else echo "$SHA  fftw-3.3.10.tar.gz" | shasum -a 256 -c -; fi
rm -rf fftw-3.3.10 && tar xzf fftw-3.3.10.tar.gz
EXTRA=()
if [ "$(uname)" = "Darwin" ]; then EXTRA+=(-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0); fi
cmake -S fftw-3.3.10 -B fftw-build -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTS=OFF \
  -DENABLE_THREADS=ON -DWITH_COMBINED_THREADS=ON -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_POLICY_VERSION_MINIMUM=3.5 "${EXTRA[@]}"
cmake --build fftw-build --config Release -j 4
cmake --install fftw-build --config Release
