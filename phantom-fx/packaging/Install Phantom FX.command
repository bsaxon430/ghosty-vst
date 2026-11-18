#!/bin/bash
# Installs Phantom FX for the current user. Old copies are moved to ~/GhostBlock-Backups/<stamp> (never deleted).
cd "$(dirname "$0")" || exit 1
STAMP=$(date +%Y-%m-%d_%H%M); BK="$HOME/GhostBlock-Backups/$STAMP Phantom FX"; mkdir -p "$BK"
V="$HOME/Library/Audio/Plug-Ins/VST3"; C="$HOME/Library/Audio/Plug-Ins/CLAP"; mkdir -p "$V" "$C"
if [ -d "Plugins/Phantom FX.vst3" ]; then [ -e "$V/Phantom FX.vst3" ] && mv "$V/Phantom FX.vst3" "$BK/"; ditto "Plugins/Phantom FX.vst3" "$V/Phantom FX.vst3"; xattr -dr com.apple.quarantine "$V/Phantom FX.vst3" 2>/dev/null; echo "installed VST3"; fi
if [ -d "Plugins/Phantom FX.clap" ]; then [ -e "$C/Phantom FX.clap" ] && mv "$C/Phantom FX.clap" "$BK/"; ditto "Plugins/Phantom FX.clap" "$C/Phantom FX.clap"; xattr -dr com.apple.quarantine "$C/Phantom FX.clap" 2>/dev/null; echo "installed CLAP"; fi
if [ -d "Plugins/Phantom FX.component" ]; then A="$HOME/Library/Audio/Plug-Ins/Components"; mkdir -p "$A"; [ -e "$A/Phantom FX.component" ] && mv "$A/Phantom FX.component" "$BK/"; ditto "Plugins/Phantom FX.component" "$A/Phantom FX.component"; xattr -dr com.apple.quarantine "$A/Phantom FX.component" 2>/dev/null; echo "installed AU"; fi
if [ -d "Phantom FX.app" ]; then D=/Applications; [ -w /Applications ] || { D="$HOME/Applications"; mkdir -p "$D"; }; [ -e "$D/Phantom FX.app" ] && mv "$D/Phantom FX.app" "$BK/"; ditto "Phantom FX.app" "$D/Phantom FX.app"; xattr -dr com.apple.quarantine "$D/Phantom FX.app" 2>/dev/null; echo "installed app to $D"; fi
rmdir "$BK" 2>/dev/null
echo "Done. Rescan plugins in FL Studio (Options > Manage plugins > Find more plugins)."
