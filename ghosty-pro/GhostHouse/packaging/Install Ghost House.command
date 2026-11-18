#!/bin/bash
cd "$(dirname "$0")"
echo "Installing Ghost House..."
xattr -dr com.apple.quarantine . 2>/dev/null
V=~/Library/Audio/Plug-Ins/VST3; C=~/Library/Audio/Plug-Ins/Components
mkdir -p "$V" "$C"
rm -rf "$V/Ghost House.vst3"; [ -d "Plugins/Ghost House.component" ] && rm -rf "$C/Ghost House.component"
cp -R "Plugins/Ghost House.vst3" "$V"/
[ -d "Plugins/Ghost House.component" ] && cp -R "Plugins/Ghost House.component" "$C"/   # (builds from Brandon's Mac are VST3 only)
for p in "$V/Ghost House.vst3" "$C/Ghost House.component"; do
  [ -d "$p" ] || continue
  xattr -dr com.apple.quarantine "$p" 2>/dev/null
  codesign --force --deep --sign - "$p" 2>/dev/null
  echo "  + $(basename "$p")"
done
if [ -d "Ghost House.app" ]; then rm -rf "/Applications/Ghost House.app"; cp -R "Ghost House.app" /Applications/ 2>/dev/null && xattr -dr com.apple.quarantine "/Applications/Ghost House.app" 2>/dev/null && echo "  + Ghost House.app (in Applications)"; fi
killall -9 AudioComponentRegistrar 2>/dev/null
echo ""
echo "Done! Now in FL Studio: Options > Manage plugins > Find more plugins,"
echo "then add Ghost House from the Channel Rack (+) as a generator."
echo ""
read -n 1 -s -r -p "Press any key to close."
