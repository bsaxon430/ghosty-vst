#!/bin/bash
# GhostBlock plugins: one-step install on a Mac (Apple Silicon or Intel).
# Open Terminal and paste:
#   curl -fsSL https://raw.githubusercontent.com/bsaxon430/ghosty-vst/builds/install-plugins.sh | bash
# It downloads the newest complete release, puts every plugin in ~/Library/Audio/Plug-Ins/VST3 (any older copy is moved to
# ~/Library/Audio/Plug-Ins/VST3-replaced-<date> first, never deleted), and clears the download flag macOS puts on them.
set -euo pipefail
URL="https://github.com/bsaxon430/ghosty-vst/releases/latest/download/GhostBlock-All-Plugins-mac-universal.zip"
DEST="$HOME/Library/Audio/Plug-Ins/VST3"
OLD="$HOME/Library/Audio/Plug-Ins/VST3-replaced-$(date +%Y%m%d-%H%M%S)"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
echo "Downloading the GhostBlock plugins..."
curl -fL --progress-bar "$URL" -o "$TMP/all.zip"
ditto -x -k "$TMP/all.zip" "$TMP/x"
mkdir -p "$DEST"
n=0
while IFS= read -r -d '' v; do
  name="$(basename "$v")"
  if [ -e "$DEST/$name" ]; then mkdir -p "$OLD"; mv "$DEST/$name" "$OLD/"; fi
  ditto "$v" "$DEST/$name"
  xattr -dr com.apple.quarantine "$DEST/$name" 2>/dev/null || true
  echo "  installed $name"; n=$((n+1))
done < <(find "$TMP/x" -maxdepth 3 -name "*.vst3" -type d -prune -print0)
echo
echo "Done: $n plugins installed in $DEST"
[ -d "$OLD" ] && echo "Older copies were moved to $OLD"
echo "Now open FL Studio (Options > Manage plugins > Find more plugins) or Ableton (Settings > Plug-Ins > Rescan)."
