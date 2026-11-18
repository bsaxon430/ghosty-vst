#!/bin/bash
cd "$(dirname "$0")"
echo "Installing Ghost Box..."
xattr -dr com.apple.quarantine . 2>/dev/null
V=~/Library/Audio/Plug-Ins/VST3; C=~/Library/Audio/Plug-Ins/Components
mkdir -p "$V" "$C"
rm -rf "$V/Ghost Box.vst3" "$C/Ghost Box.component"
# an older Ghost Box from the GhostBlock collection installer (in /Library, for all users) wins over this one in FL / Logic:
# take it out (asks for your Mac password once)
OLD=()
for f in "/Library/Audio/Plug-Ins/VST3/Ghost Box.vst3" "/Library/Audio/Plug-Ins/Components/Ghost Box.component" "/Library/Audio/Plug-Ins/CLAP/Ghost Box.clap" "/Applications/GhostBlock/Ghost Box.app" "$HOME/Library/Audio/Plug-Ins/CLAP/Ghost Box.clap"; do
  [ -e "$f" ] && OLD+=("$f")
done
if [ ${#OLD[@]} -gt 0 ]; then
  echo "Removing the old Ghost Box (type your Mac password if asked; nothing shows while you type):"
  for f in "${OLD[@]}"; do echo "  - $f"; done
  rm -rf "${OLD[@]}" 2>/dev/null || sudo rm -rf "${OLD[@]}"
fi
cp -R "Plugins/Ghost Box.vst3" "$V"/
[ -d "Plugins/Ghost Box.component" ] && cp -R "Plugins/Ghost Box.component" "$C"/
for p in "$V/Ghost Box.vst3" "$C/Ghost Box.component"; do
  [ -e "$p" ] || continue
  xattr -dr com.apple.quarantine "$p" 2>/dev/null
  codesign --force --deep --sign - "$p" 2>/dev/null
  echo "  + $(basename "$p")"
done
if [ -d "Ghost Box.app" ]; then rm -rf "/Applications/Ghost Box.app"; cp -R "Ghost Box.app" /Applications/ 2>/dev/null && xattr -dr com.apple.quarantine "/Applications/Ghost Box.app" 2>/dev/null && echo "  + Ghost Box.app (in Applications)"; fi
killall -9 AudioComponentRegistrar 2>/dev/null
rm -f ~/Library/Caches/AudioUnitCache/com.apple.audiounits.cache 2>/dev/null
echo ""
echo "Done! In FL Studio: Options > Manage plugins > Find more plugins, then put Ghost Box"
echo "on a mixer insert (it is an effect). Old projects open with their sound."
echo ""
read -n 1 -s -r -p "Press any key to close."
