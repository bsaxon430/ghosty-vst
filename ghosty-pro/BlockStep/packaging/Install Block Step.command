#!/bin/bash
cd "$(dirname "$0")"
echo "Installing Block Step..."
xattr -dr com.apple.quarantine . 2>/dev/null
V=~/Library/Audio/Plug-Ins/VST3; C=~/Library/Audio/Plug-Ins/Components
mkdir -p "$V" "$C"
rm -rf "$V/Block Step.vst3"; [ -d "Plugins/Block Step.component" ] && rm -rf "$C/Block Step.component"
cp -R "Plugins/Block Step.vst3" "$V"/
[ -d "Plugins/Block Step.component" ] && cp -R "Plugins/Block Step.component" "$C"/   # (builds from Brandon's Mac are VST3 only)
for p in "$V/Block Step.vst3" "$C/Block Step.component"; do
  [ -d "$p" ] || continue
  xattr -dr com.apple.quarantine "$p" 2>/dev/null
  codesign --force --deep --sign - "$p" 2>/dev/null
  echo "  + $(basename "$p")"
done
if [ -d "Block Step.app" ]; then rm -rf "/Applications/Block Step.app"; cp -R "Block Step.app" /Applications/ 2>/dev/null && xattr -dr com.apple.quarantine "/Applications/Block Step.app" 2>/dev/null && echo "  + Block Step.app (in Applications)"; fi
killall -9 AudioComponentRegistrar 2>/dev/null
echo ""
echo "Done! Now in FL Studio: Options > Manage plugins > Find more plugins,"
echo "then add Block Step from the Channel Rack (+) as a generator."
echo ""
read -n 1 -s -r -p "Press any key to close."
