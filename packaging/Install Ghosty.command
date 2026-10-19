#!/bin/bash
cd "$(dirname "$0")"
echo "Installing Ghosty, Ghosty Beats, Ghosty Spectrum, Poltergeist and Mr. Mickens..."
xattr -dr com.apple.quarantine . 2>/dev/null
V=~/Library/Audio/Plug-Ins/VST3; C=~/Library/Audio/Plug-Ins/Components
mkdir -p "$V" "$C"
rm -rf "$V"/Ghosty*.vst3 "$C"/Ghosty*.component "$V"/Poltergeist.vst3 "$C"/Poltergeist.component "$V/Mr Mickens.vst3" "$C/Mr Mickens.component"
cp -R Plugins/*.vst3 "$V"/
cp -R Plugins/*.component "$C"/
for p in "$V"/Ghosty*.vst3 "$C"/Ghosty*.component "$V"/Poltergeist.vst3 "$C"/Poltergeist.component "$V/Mr Mickens.vst3" "$C/Mr Mickens.component"; do
  xattr -dr com.apple.quarantine "$p" 2>/dev/null
  codesign --force --deep --sign - "$p" 2>/dev/null
  echo "  + $(basename "$p")"
done
killall -9 AudioComponentRegistrar 2>/dev/null
echo ""
echo "Done! Now rescan plug-ins in your DAW:"
echo "  FL Studio: Options > Manage plugins > Find more plugins"
echo "  Ableton:   Settings > Plug-Ins > Rescan"
echo "  Logic:     just reopen Logic (AU)"
echo ""
read -n 1 -s -r -p "Press any key to close."
