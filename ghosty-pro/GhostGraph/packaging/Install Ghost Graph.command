#!/bin/bash
cd "$(dirname "$0")"
echo "Installing Ghost Graph..."
xattr -dr com.apple.quarantine . 2>/dev/null
V=~/Library/Audio/Plug-Ins/VST3
mkdir -p "$V"
rm -rf "$V/Ghost Graph.vst3"
cp -R "Plugins/Ghost Graph.vst3" "$V"/
xattr -dr com.apple.quarantine "$V/Ghost Graph.vst3" 2>/dev/null
codesign --force --deep --sign - "$V/Ghost Graph.vst3" 2>/dev/null
echo "  + Ghost Graph.vst3"
if [ -d "Ghost Graph.app" ]; then rm -rf "/Applications/Ghost Graph.app"; cp -R "Ghost Graph.app" /Applications/ 2>/dev/null && xattr -dr com.apple.quarantine "/Applications/Ghost Graph.app" 2>/dev/null && echo "  + Ghost Graph.app (in Applications)"; fi
echo ""
echo "Done! In FL Studio: Options > Manage plugins > Find more plugins, then add Ghost Graph as an instrument and press play."
echo ""
read -n 1 -s -r -p "Press any key to close."
