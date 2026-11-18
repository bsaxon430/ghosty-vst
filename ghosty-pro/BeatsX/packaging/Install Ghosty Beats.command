#!/bin/bash
cd "$(dirname "$0")"
echo "Installing Ghosty Beats..."
xattr -dr com.apple.quarantine . 2>/dev/null
V=~/Library/Audio/Plug-Ins/VST3; C=~/Library/Audio/Plug-Ins/Components
mkdir -p "$V" "$C"
rm -rf "$V/Ghosty Beats.vst3" "$C/Ghosty Beats.component"
cp -R "Plugins/Ghosty Beats.vst3" "$V"/
cp -R "Plugins/Ghosty Beats.component" "$C"/
for p in "$V/Ghosty Beats.vst3" "$C/Ghosty Beats.component"; do
  xattr -dr com.apple.quarantine "$p" 2>/dev/null
  codesign --force --deep --sign - "$p" 2>/dev/null
  echo "  + $(basename "$p")"
done
if [ -d "Ghosty Beats.app" ]; then rm -rf "/Applications/Ghosty Beats.app"; cp -R "Ghosty Beats.app" /Applications/ 2>/dev/null && xattr -dr com.apple.quarantine "/Applications/Ghosty Beats.app" 2>/dev/null && echo "  + Ghosty Beats.app (in Applications)"; fi
killall -9 AudioComponentRegistrar 2>/dev/null
echo ""
echo "Done! Now in FL Studio: Options > Manage plugins > Find more plugins,"
echo "then add Ghosty Beats from the Channel Rack (+) as a generator."
echo ""
read -n 1 -s -r -p "Press any key to close."
