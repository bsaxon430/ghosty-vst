#!/bin/bash
# ibtool stand-in for a Mac with only the command line tools (no Xcode): the standalone app's menu (MainMenu.nib) was
# compiled once by Xcode's ibtool on GitHub's Mac (workflow poltergeist2-nib) and lives next to this script. Copy it.
out=""
while [ $# -gt 0 ]; do case "$1" in --compile) out="$2"; shift 2 ;; *) shift ;; esac; done
src="${P2_NIB:-$(cd "$(dirname "$0")" && pwd)/MainMenu.nib}"
[ -e "$src" ] || { echo "ibtool-shim: no prebuilt $src yet (run the poltergeist2-nib workflow)"; exit 1; }
[ -n "$out" ] || { echo "ibtool-shim: no --compile target"; exit 1; }
mkdir -p "$(dirname "$out")"; rm -rf "$out"; cp -R "$src" "$out"; echo "ibtool-shim: $out"
