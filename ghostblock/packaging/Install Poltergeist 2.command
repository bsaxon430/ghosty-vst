#!/bin/bash
# Installs Poltergeist 2 for this Mac user (the old copy is moved to ~/GhostBlock-Backups first)
cd "$(dirname "$0")"
STAMP=$(date +%Y-%m-%d_%H%M); BK="$HOME/GhostBlock-Backups/$STAMP Poltergeist 2"; mkdir -p "$BK"
put() { src="$1"; dst="$2"; mkdir -p "$dst"; n=$(basename "$src"); [ -e "$dst/$n" ] && mv "$dst/$n" "$BK/"; ditto "$src" "$dst/$n"; xattr -dr com.apple.quarantine "$dst/$n" 2>/dev/null; echo "installed $dst/$n"; }
for b in Plugins/*.vst3; do put "$b" "$HOME/Library/Audio/Plug-Ins/VST3"; done
for b in Plugins/*.clap; do [ -e "$b" ] && put "$b" "$HOME/Library/Audio/Plug-Ins/CLAP"; done
for b in Plugins/*.component; do [ -e "$b" ] && put "$b" "$HOME/Library/Audio/Plug-Ins/Components"; done
D=/Applications; [ -w "$D" ] || { D="$HOME/Applications"; mkdir -p "$D"; }
for a in *.app; do [ -e "$a" ] && put "$a" "$D"; done
rmdir "$BK" 2>/dev/null
echo "Done. Rescan plugins in FL Studio."
