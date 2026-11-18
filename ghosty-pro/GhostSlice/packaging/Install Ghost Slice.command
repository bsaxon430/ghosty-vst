#!/bin/bash
cd "$(dirname "$0")"
echo "Installing Ghost Slice..."
xattr -dr com.apple.quarantine . 2>/dev/null
V=~/Library/Audio/Plug-Ins/VST3; C=~/Library/Audio/Plug-Ins/Components
mkdir -p "$V" "$C"
rm -rf "$V/Ghost Slice.vst3" "$C/Ghost Slice.component"
# an older Ghost Slice from the GhostBlock collection installer (in /Library, for all users) wins over this one in FL / Logic:
# take it out (asks for your Mac password once)
OLD=()
for f in "/Library/Audio/Plug-Ins/VST3/Ghost Slice.vst3" "/Library/Audio/Plug-Ins/Components/Ghost Slice.component" "/Library/Audio/Plug-Ins/CLAP/Ghost Slice.clap" "/Applications/GhostBlock/Ghost Slice.app" "$HOME/Library/Audio/Plug-Ins/CLAP/Ghost Slice.clap"; do
  [ -e "$f" ] && OLD+=("$f")
done
if [ ${#OLD[@]} -gt 0 ]; then
  echo "Removing the old Ghost Slice (type your Mac password if asked; nothing shows while you type):"
  for f in "${OLD[@]}"; do echo "  - $f"; done
  rm -rf "${OLD[@]}" 2>/dev/null || sudo rm -rf "${OLD[@]}"
fi
cp -R "Plugins/Ghost Slice.vst3" "$V"/
cp -R "Plugins/Ghost Slice.component" "$C"/
for p in "$V/Ghost Slice.vst3" "$C/Ghost Slice.component"; do
  xattr -dr com.apple.quarantine "$p" 2>/dev/null
  codesign --force --deep --sign - "$p" 2>/dev/null
  echo "  + $(basename "$p")"
done
if [ -d "Ghost Slice.app" ]; then rm -rf "/Applications/Ghost Slice.app"; cp -R "Ghost Slice.app" /Applications/ 2>/dev/null && xattr -dr com.apple.quarantine "/Applications/Ghost Slice.app" 2>/dev/null && echo "  + Ghost Slice.app (in Applications)"; fi
killall -9 AudioComponentRegistrar 2>/dev/null
rm -f ~/Library/Caches/AudioUnitCache/com.apple.audiounits.cache 2>/dev/null
echo ""
echo "Done! In FL Studio: Options > Manage plugins > Find more plugins, then add Ghost Slice"
echo "as an instrument (Channel rack > + > Ghost Slice). Drop a vocal on it and press GENERATE."
echo ""
read -n 1 -s -r -p "Press any key to close."
