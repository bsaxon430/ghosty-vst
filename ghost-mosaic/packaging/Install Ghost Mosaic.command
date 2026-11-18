#!/bin/bash
cd "$(dirname "$0")"
echo "Installing Ghost Mosaic..."
xattr -dr com.apple.quarantine . 2>/dev/null
V=~/Library/Audio/Plug-Ins/VST3; C=~/Library/Audio/Plug-Ins/Components
mkdir -p "$V" "$C"
for n in "Ghost Mosaic" "Ghost Mosaic FX"; do
  rm -rf "$V/$n.vst3" "$C/$n.component"
  cp -R "Plugins/$n.vst3" "$V"/
  [ -d "Plugins/$n.component" ] && cp -R "Plugins/$n.component" "$C"/   # (builds from Brandon's Mac are VST3 only)
  for p in "$V/$n.vst3" "$C/$n.component"; do
    [ -e "$p" ] || continue
    xattr -dr com.apple.quarantine "$p" 2>/dev/null
    codesign --force --deep --sign - "$p" 2>/dev/null
    echo "  + $(basename "$p")"
  done
done
for a in "Ghost Mosaic.app" "Ghost Mosaic FX.app"; do
  if [ -d "$a" ]; then rm -rf "/Applications/$a"; cp -R "$a" /Applications/ 2>/dev/null && xattr -dr com.apple.quarantine "/Applications/$a" 2>/dev/null && echo "  + $a (in Applications)"; fi
done
killall -9 AudioComponentRegistrar 2>/dev/null
rm -f ~/Library/Caches/AudioUnitCache/com.apple.audiounits.cache 2>/dev/null
echo ""
echo "Done! In FL Studio: Options > Manage plugins > Find more plugins."
echo "  Ghost Mosaic    = an INSTRUMENT (Channel rack > + > Ghost Mosaic)"
echo "  Ghost Mosaic FX = an EFFECT (Mixer insert > Ghost Mosaic FX): your track becomes the target, live"
echo ""
read -n 1 -s -r -p "Press any key to close."
