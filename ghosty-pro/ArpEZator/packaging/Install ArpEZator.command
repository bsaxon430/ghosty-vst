#!/bin/bash
cd "$(dirname "$0")"
echo "Installing ArpEZator..."
xattr -dr com.apple.quarantine . 2>/dev/null
V=~/Library/Audio/Plug-Ins/VST3; C=~/Library/Audio/Plug-Ins/Components
mkdir -p "$V" "$C"
rm -rf "$V/ArpEZator.vst3" "$C/ArpEZator.component"
# an older ArpEZator from the GhostBlock collection installer (in /Library, for all users) wins over this one in FL / Logic: take it out
OLD=()
for f in "/Library/Audio/Plug-Ins/VST3/ArpEZator.vst3" "/Library/Audio/Plug-Ins/Components/ArpEZator.component" "/Library/Audio/Plug-Ins/CLAP/ArpEZator.clap" "/Applications/GhostBlock/ArpEZator.app" "$HOME/Library/Audio/Plug-Ins/CLAP/ArpEZator.clap"; do
  [ -e "$f" ] && OLD+=("$f")
done
if [ ${#OLD[@]} -gt 0 ]; then
  echo "Removing the old ArpEZator (type your Mac password if asked; nothing shows while you type):"
  for f in "${OLD[@]}"; do echo "  - $f"; done
  rm -rf "${OLD[@]}" 2>/dev/null || sudo rm -rf "${OLD[@]}"
fi
cp -R "Plugins/ArpEZator.vst3" "$V"/
cp -R "Plugins/ArpEZator.component" "$C"/
for p in "$V/ArpEZator.vst3" "$C/ArpEZator.component"; do
  xattr -dr com.apple.quarantine "$p" 2>/dev/null
  codesign --force --deep --sign - "$p" 2>/dev/null
  echo "  + $(basename "$p")"
done
if [ -d "ArpEZator.app" ]; then rm -rf "/Applications/ArpEZator.app"; cp -R "ArpEZator.app" /Applications/ 2>/dev/null && xattr -dr com.apple.quarantine "/Applications/ArpEZator.app" 2>/dev/null && echo "  + ArpEZator.app (in Applications)"; fi
killall -9 AudioComponentRegistrar 2>/dev/null
rm -f ~/Library/Caches/AudioUnitCache/com.apple.audiounits.cache 2>/dev/null
echo ""
echo "Done! In FL Studio: Options > Manage plugins > Find more plugins, then add ArpEZator as an instrument."
echo "Press MUTANT ARP (next to HOOK LAB) for the new melodic sequence generator."
echo ""
read -n 1 -s -r -p "Press any key to close."
