#!/bin/bash
# Makes MainMenu.nib for the standalone app on a Mac without Xcode: takes the compiled menu nib out of the official Six
# Sines Mac release (same clap-wrapper menu, compiled by Xcode's ibtool there), renames "Six Sines" -> "Poltergeist 2"
# in it (plutil, part of macOS), and writes it to $1. Needs the network (GitHub's API + release download).
set -e
OUT="$1"; [ -n "$OUT" ] || { echo "usage: get-nib.sh <out.nib>"; exit 1; }
W=$(mktemp -d); trap 'hdiutil detach -quiet "$W/mnt" 2>/dev/null || true; rm -rf "$W"' EXIT
curl -sSL https://api.github.com/repos/baconpaul/six-sines/releases/latest -o "$W/rel.json"
URL=$(python3 -c 'import json,sys; a=json.load(open(sys.argv[1]))["assets"]; m=[x["browser_download_url"] for x in a if ("mac" in x["name"].lower() or "darwin" in x["name"].lower()) and x["name"].lower().endswith((".dmg",".zip",".pkg"))]; print(m[0] if m else "")' "$W/rel.json")
[ -n "$URL" ] || { echo "get-nib: no Mac asset in the latest Six Sines release"; exit 1; }
echo "get-nib: $URL"; curl -sSL "$URL" -o "$W/asset"
mkdir -p "$W/x"
case "$URL" in
  *.dmg) hdiutil attach -quiet -nobrowse -readonly -mountpoint "$W/mnt" "$W/asset"; cp -R "$W/mnt/." "$W/x/" ;;
  *.zip) ditto -x -k "$W/asset" "$W/x" ;;
  *.pkg) cp "$W/asset" "$W/x/a.pkg" ;;
esac
for p in $(find "$W/x" -name "*.pkg" -maxdepth 4); do pkgutil --expand-full "$p" "$W/x/exp-$(basename "$p" .pkg)" >/dev/null 2>&1 || true; done
N=$(find "$W/x" -path "*.app/Contents/Resources/MainMenu.nib" | head -n 1)
[ -n "$N" ] || { echo "get-nib: no MainMenu.nib in the release"; find "$W/x" -maxdepth 6 | head -n 40; exit 1; }
rm -rf "$OUT"; cp -R "$N" "$OUT"
for f in $( [ -d "$OUT" ] && find "$OUT" -type f || echo "$OUT" ); do
  if plutil -convert xml1 -o "$W/n.xml" "$f" 2>/dev/null; then
    sed -i '' 's/Six Sines/Poltergeist 2/g' "$W/n.xml"; plutil -convert binary1 -o "$f" "$W/n.xml"
  fi
done
echo "get-nib: $OUT ready"; ls -la "$OUT"
