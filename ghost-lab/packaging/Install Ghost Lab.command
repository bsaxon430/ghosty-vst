#!/bin/bash
cd "$(dirname "$0")"
echo "Installing Ghost Lab..."
xattr -dr com.apple.quarantine . 2>/dev/null
V=~/Library/Audio/Plug-Ins/VST3; C=~/Library/Audio/Plug-Ins/Components
mkdir -p "$V" "$C"
rm -rf "$V/Ghost Lab.vst3" "$C/Ghost Lab.component" "$V/Ghost Lab FX.vst3" "$C/Ghost Lab FX.component"
# an older copy in /Library (for all users) would win over this one in FL / Logic: take it out (asks for your Mac password once)
OLD=()
for f in "/Library/Audio/Plug-Ins/VST3/Ghost Lab.vst3" "/Library/Audio/Plug-Ins/Components/Ghost Lab.component" \
         "/Library/Audio/Plug-Ins/VST3/Ghost Lab FX.vst3" "/Library/Audio/Plug-Ins/Components/Ghost Lab FX.component" \
         "/Applications/GhostBlock/Ghost Lab.app"; do
  [ -e "$f" ] && OLD+=("$f")
done
if [ ${#OLD[@]} -gt 0 ]; then
  echo "Removing the old Ghost Lab (type your Mac password if asked; nothing shows while you type):"
  for f in "${OLD[@]}"; do echo "  - $f"; done
  rm -rf "${OLD[@]}" 2>/dev/null || sudo rm -rf "${OLD[@]}"
fi
cp -R "Plugins/Ghost Lab.vst3" "Plugins/Ghost Lab FX.vst3" "$V"/
# the Audio Units are in the download only when it was built with them (FL uses the VST3s)
for c in "Plugins/Ghost Lab.component" "Plugins/Ghost Lab FX.component"; do [ -d "$c" ] && cp -R "$c" "$C"/; done
for p in "$V/Ghost Lab.vst3" "$C/Ghost Lab.component" "$V/Ghost Lab FX.vst3" "$C/Ghost Lab FX.component"; do
  [ -d "$p" ] || continue
  xattr -dr com.apple.quarantine "$p" 2>/dev/null
  for x in "$p"/Contents/Helpers/cdp/*; do file "$x" | grep -q Mach-O && codesign --force --sign - "$x" 2>/dev/null; done
  codesign --force --deep --sign - "$p" 2>/dev/null
  echo "  + $(basename "$p")"
done
if [ -d "Ghost Lab.app" ]; then rm -rf "/Applications/Ghost Lab.app"; cp -R "Ghost Lab.app" /Applications/ 2>/dev/null && xattr -dr com.apple.quarantine "/Applications/Ghost Lab.app" 2>/dev/null && echo "  + Ghost Lab.app (in Applications)"; fi
# fresh plug-in list (so FL / Logic see the new version)
killall -9 AudioComponentRegistrar 2>/dev/null
rm -f ~/Library/Caches/AudioUnitCache/com.apple.audiounits.cache 2>/dev/null
rm -rf ~/Library/Caches/GhostLab/work 2>/dev/null
echo ""
echo "Done! In FL Studio: Options > Manage plugins > Find more plugins."
echo "  Ghost Lab    = an instrument (Channel rack > + > Ghost Lab). Drag a sound from FL's browser onto it."
echo "  Ghost Lab FX = an effect (Mixer insert). Press CAPTURE, play your track, press it again."
echo "Or open Ghost Lab from Applications."
echo ""
read -n 1 -s -r -p "Press any key to close."
