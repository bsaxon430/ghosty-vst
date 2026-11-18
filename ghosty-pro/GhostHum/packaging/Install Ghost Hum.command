#!/bin/bash
cd "$(dirname "$0")"
echo "Installing Ghost Hum..."
xattr -dr com.apple.quarantine . 2>/dev/null
V=~/Library/Audio/Plug-Ins/VST3; C=~/Library/Audio/Plug-Ins/Components
mkdir -p "$V" "$C"
rm -rf "$V/Ghost Hum.vst3" "$C/Ghost Hum.component"
cp -R "Plugins/Ghost Hum.vst3" "$V"/
cp -R "Plugins/Ghost Hum.component" "$C"/
for p in "$V/Ghost Hum.vst3" "$C/Ghost Hum.component"; do
  xattr -dr com.apple.quarantine "$p" 2>/dev/null
  codesign --force --deep --sign - "$p" 2>/dev/null
  echo "  + $(basename "$p")"
done
if [ -d "Ghost Hum.app" ]; then rm -rf "/Applications/Ghost Hum.app"; cp -R "Ghost Hum.app" /Applications/ 2>/dev/null && xattr -dr com.apple.quarantine "/Applications/Ghost Hum.app" 2>/dev/null && echo "  + Ghost Hum.app (in Applications)"; fi
killall -9 AudioComponentRegistrar 2>/dev/null
echo ""
echo "Done! Easiest: open the Ghost Hum app from Applications, tap the big button, sing."
echo "In FL Studio: Options > Manage plugins > Find more plugins, then put Ghost Hum on the"
echo "mixer insert your microphone comes in on (it is an effect that listens, then plays MIDI)."
echo ""
read -n 1 -s -r -p "Press any key to close."
