#!/bin/bash
# Ghost Bass installer for Mac: double-click it.
# Installs into your own user folders (no admin needed for that part), removes older Ghost Bass
# copies first, and clears the Audio Unit cache so Logic / FL Studio see the new build.
# It never touches Surge XT: Ghost Bass has its own name, plugin IDs and data folder.
cd "$(dirname "$0")"
echo "Installing Ghost Bass..."
xattr -dr com.apple.quarantine . 2>/dev/null
V=~/Library/Audio/Plug-Ins/VST3; C=~/Library/Audio/Plug-Ins/Components
DATA=~/Library/Application\ Support/Ghost\ Bass
mkdir -p "$V" "$C"
rm -rf "$V/Ghost Bass.vst3" "$C/Ghost Bass.component"
# older Ghost Bass copies in the shared /Library folders win over this one in some hosts: take them out
OLD=()
for f in "/Library/Audio/Plug-Ins/VST3/Ghost Bass.vst3" "/Library/Audio/Plug-Ins/Components/Ghost Bass.component" \
         "/Library/Audio/Plug-Ins/CLAP/Ghost Bass.clap" "/Library/Application Support/Ghost Bass" \
         "$HOME/Library/Audio/Plug-Ins/CLAP/Ghost Bass.clap"; do
  [ -e "$f" ] && OLD+=("$f")
done
if [ ${#OLD[@]} -gt 0 ]; then
  echo "Removing an older Ghost Bass (type your Mac password if asked; nothing shows while you type):"
  for f in "${OLD[@]}"; do echo "  - $f"; done
  rm -rf "${OLD[@]}" 2>/dev/null || sudo rm -rf "${OLD[@]}"
fi
# factory sounds, wavetables and the Block Step basses
rm -rf "$DATA"; mkdir -p "$DATA"
cp -R "Ghost Bass Data/." "$DATA"/ && echo "  + Surge XT's patches and wavetables (in ~/Library/Application Support/Ghost Bass)"
cp -R "Plugins/Ghost Bass.vst3" "$V"/
[ -d "Plugins/Ghost Bass.component" ] && cp -R "Plugins/Ghost Bass.component" "$C"/   # (builds from Brandon's Mac are VST3 only)
for p in "$V/Ghost Bass.vst3" "$C/Ghost Bass.component"; do
  [ -e "$p" ] || continue
  xattr -dr com.apple.quarantine "$p" 2>/dev/null
  codesign --force --deep --sign - "$p" 2>/dev/null
  echo "  + $(basename "$p")"
done
if [ -d "Ghost Bass.app" ]; then rm -rf "/Applications/Ghost Bass.app"; cp -R "Ghost Bass.app" /Applications/ 2>/dev/null && xattr -dr com.apple.quarantine "/Applications/Ghost Bass.app" 2>/dev/null && echo "  + Ghost Bass.app (in Applications)"; fi
killall -9 AudioComponentRegistrar 2>/dev/null
rm -f ~/Library/Caches/AudioUnitCache/com.apple.audiounits.cache 2>/dev/null
echo ""
echo "Done! In FL Studio: Options > Manage plugins > Find more plugins, then add Ghost Bass"
echo "as an instrument (Channel rack > + > Ghost Bass). Pick a sound on the left and pull the shape."
echo ""
read -n 1 -s -r -p "Press any key to close."
