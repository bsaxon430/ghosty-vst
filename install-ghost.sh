#!/bin/bash
# GHOST: one-step install on a Mac. Open Terminal and paste:
#   curl -fsSL https://raw.githubusercontent.com/bsaxon430/ghosty-vst/builds/install-ghost.sh | bash
# Puts GHOST.vst3 in ~/Library/Audio/Plug-Ins/VST3 (an older copy is moved aside, never deleted).
set -euo pipefail
URL="https://github.com/bsaxon430/ghosty-vst/releases/download/test-ghost/GHOST-mac-universal.zip"
DEST="$HOME/Library/Audio/Plug-Ins/VST3"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
echo "Downloading GHOST..."
curl -fL --progress-bar "$URL" -o "$TMP/g.zip"
ditto -x -k "$TMP/g.zip" "$TMP/x"
mkdir -p "$DEST"
if [ -e "$DEST/GHOST.vst3" ]; then OLD="$HOME/Library/Audio/Plug-Ins/VST3-replaced-$(date +%Y%m%d-%H%M%S)"; mkdir -p "$OLD"; mv "$DEST/GHOST.vst3" "$OLD/"; echo "older GHOST moved to $OLD"; fi
ditto "$TMP/x/GHOST.vst3" "$DEST/GHOST.vst3"
xattr -dr com.apple.quarantine "$DEST/GHOST.vst3" 2>/dev/null || true
chmod 755 "$DEST/GHOST.vst3/Contents/MacOS/"* 2>/dev/null || true
echo "Done: GHOST is in $DEST"
echo "FL Studio: Options > Manage plugins > Find more plugins, then GHOST is under GhostBlock."
