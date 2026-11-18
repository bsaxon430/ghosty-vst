#!/bin/bash
# Builds GhostBlock-Plugins-Mac.pkg from the CMake artefacts: VST3 -> /Library/Audio/Plug-Ins/VST3,
# AU -> /Library/Audio/Plug-Ins/Components, CLAP -> /Library/Audio/Plug-Ins/CLAP (when built: GHOSTY_BUILD_CLAP, default ON), apps -> /Applications/GhostBlock.
# Signing:
#   - MAC_APP_IDENTITY unset (the normal CI build): every bundle is signed ad-hoc exactly as before, and the pkg is unsigned.
#   - MAC_APP_IDENTITY="Developer ID Application: ... (TEAMID)" (set by mac-sign.sh when the Apple secrets exist): nested code
#     first, then each bundle, with hardened runtime + secure timestamp, no --deep, each one verified (--strict).
#   - MAC_INSTALLER_IDENTITY="Developer ID Installer: ... (TEAMID)": the finished pkg is signed with productsign.
#   - MAC_KEYCHAIN (optional): the keychain holding those identities (mac-sign.sh's temporary keychain).
# Notarization / stapling is NOT done here: see mac-sign.sh and SIGNING.md.
set -euo pipefail
B="$1"; OUT="$2"; VER="${3:-3.1.0}"
root=$(mktemp -d)
mkdir -p "$root/vst3" "$root/au" "$root/app" "$root/clap"
for t in "MrMickens:Mr Mickens" "Poltergeist:Poltergeist" "GhostyBeats:Ghosty Beats" "GhostySpectrum:Ghosty Spectrum" "GhostyPro:Ghosty Pro" "GhostBox:Ghost Box" "ArpEZator:ArpEZator" "GhostSlice:Ghost Slice"; do
  T="${t%%:*}"; N="${t#*:}"; R="$B/${T}_artefacts/Release"
  ditto "$R/VST3/$N.vst3" "$root/vst3/$N.vst3"
  ditto "$R/AU/$N.component" "$root/au/$N.component"
  ditto "$R/Standalone/$N.app" "$root/app/$N.app"
  if [ -d "$R/CLAP/$N.clap" ]; then ditto "$R/CLAP/$N.clap" "$root/clap/$N.clap"; else echo "WARNING: CLAP missing for $N (left out of the installer)"; fi
done
# third-party licence that must ship with Ghosty Spectrum (VOICE page uses WORLD, modified BSD: clause 2 needs the text with every binary distribution)
TP="$(cd "$(dirname "$0")" && pwd)/third_party"
for b in "$root/vst3/Ghosty Spectrum.vst3" "$root/au/Ghosty Spectrum.component" "$root/app/Ghosty Spectrum.app" "$root/clap/Ghosty Spectrum.clap"; do
  [ -e "$b" ] || continue; mkdir -p "$b/Contents/Resources" && cp "$TP/WORLD-LICENSE.txt" "$b/Contents/Resources/"
done
# CLAP builds include clap-juce-extensions + the CLAP SDK + clap-helpers (all MIT: the notices go with the binaries)
HAS_CLAP=0
if [ -n "$(ls -A "$root/clap")" ]; then
  HAS_CLAP=1
  for b in "$root"/clap/*.clap; do mkdir -p "$b/Contents/Resources" && cp "$TP/CLAP-LICENSES.txt" "$b/Contents/Resources/"; done
else
  echo "WARNING: no .clap bundles found under $B/*_artefacts/Release/CLAP - the installer has no CLAP choice"
fi
# Ghosty Pro ENGINES (round 3): the MIT notice of the eurorack DSP it is built on, inside each Ghosty Pro bundle
for b in "$root/vst3/Ghosty Pro.vst3" "$root/au/Ghosty Pro.component" "$root/app/Ghosty Pro.app" "$root/clap/Ghosty Pro.clap"; do
  [ -e "$b" ] || continue
  mkdir -p "$b/Contents/Resources" && cp "$TP/EURORACK-MIT-LICENSE.txt" "$b/Contents/Resources/"
done
# Ghost Slice: the MIT notices of Signalsmith Stretch + signalsmith-linear (its stretch / pitch engine) and r8brain-free-src (its
# sample-rate converter), inside each Ghost Slice bundle
for b in "$root/vst3/Ghost Slice.vst3" "$root/au/Ghost Slice.component" "$root/app/Ghost Slice.app" "$root/clap/Ghost Slice.clap"; do
  [ -e "$b" ] || continue
  mkdir -p "$b/Contents/Resources" && cp "$TP/SIGNALSMITH-MIT-LICENSE.txt" "$TP/R8BRAIN-MIT-LICENSE.txt" "$b/Contents/Resources/"
done
# strip local symbols from every Mach-O binary (keeps exported plugin entry points; GitHub refuses files over 100 MB)
find "$root" -type f -perm -u+x | while read -r f; do if file "$f" | grep -q Mach-O; then strip -x "$f" 2>/dev/null || true; fi; done
du -sh "$root"/vst3 "$root"/au "$root"/app "$root"/clap || true
ID="${MAC_APP_IDENTITY:--}"
if [ "$ID" = "-" ]; then
  # unsigned build (no Apple secrets): ad-hoc, unchanged
  for b in "$root"/vst3/* "$root"/au/* "$root"/app/* "$root"/clap/*; do [ -e "$b" ] || continue; codesign --force --deep --options runtime --timestamp=none --sign "$ID" "$b"; done
else
  # Developer ID signing. Entitlements: NONE, on purpose.
  #  - Plug-ins (.vst3 / .component / .clap) run inside the host's process, so the host's entitlements apply; a plug-in's own
  #    entitlements are ignored. Hardened runtime + no entitlements is what notarization needs.
  #  - Standalone apps: JUCE does not JIT (no allow-unsigned-executable-memory / allow-jit) and our apps do not load other
  #    vendors' plug-ins or dylibs (no disable-library-validation: everything they load is inside their own signed bundle).
  #    Audio input (com.apple.security.device.audio-input) is NOT added: the apps carry no NSMicrophoneUsageDescription today
  #    (CMake MICROPHONE_PERMISSION_ENABLED is off), so the entitlement alone would change nothing; if a standalone ever needs
  #    the mic (e.g. Ghost Box as an effect app), turn both on together.
  sign_one() {
    if [ -n "${MAC_KEYCHAIN:-}" ]; then
      codesign --force --timestamp --options runtime --keychain "$MAC_KEYCHAIN" --sign "$ID" "$1"
    else
      codesign --force --timestamp --options runtime --sign "$ID" "$1"
    fi
    codesign --verify --strict --verbose=2 "$1"
  }
  # deepest path first (bash 3.2 safe: no mapfile)
  deepest_first() { awk -F/ '{ print NF "\t" $0 }' | sort -rn | cut -f2-; }
  for b in "$root"/vst3/* "$root"/au/* "$root"/app/* "$root"/clap/*; do
    [ -e "$b" ] || continue
    # 1) loose Mach-O code (dylibs, helper tools, extra binaries) that is not the main executable of its bundle and not inside a
    #    framework (frameworks are signed whole in step 2)
    find "$b/Contents" -type f \( -perm -u+x -o -name '*.dylib' -o -name '*.so' \) -not -path '*.framework/*' 2>/dev/null | deepest_first | while IFS= read -r f; do
      case "$(file "$f")" in *Mach-O*) ;; *) continue ;; esac   # (no `file | grep -q` here: pipefail + SIGPIPE could skip a binary)
      d=$(dirname "$f")
      if [ "$(basename "$d")" = MacOS ] && [ -f "$d/../Info.plist" ]; then
        main=$(plutil -extract CFBundleExecutable raw -o - "$d/../Info.plist" 2>/dev/null || true)
        [ "$(basename "$f")" = "$main" ] && continue   # signed with its bundle below
      fi
      sign_one "$f"
    done
    # 2) nested bundles (frameworks, helper apps, xpc, plain bundles), deepest first
    find "$b/Contents" -mindepth 1 -type d \( -name '*.framework' -o -name '*.app' -o -name '*.bundle' -o -name '*.xpc' -o -name '*.appex' -o -name '*.plugin' \) 2>/dev/null | deepest_first | while IFS= read -r n; do
      sign_one "$n"
    done
    # 3) the bundle itself (seals Contents/MacOS/<main>, Info.plist and Resources incl. the licence files copied in above)
    sign_one "$b"
  done
fi
# the plain licences folder (not a bundle, so it must be added AFTER the signing loop above)
mkdir -p "$root/app/licenses" && cp "$TP/WORLD-LICENSE.txt" "$TP/EURORACK-MIT-LICENSE.txt" "$TP/SIGNALSMITH-MIT-LICENSE.txt" "$TP/R8BRAIN-MIT-LICENSE.txt" "$root/app/licenses/"
if [ "$HAS_CLAP" = 1 ]; then cp "$TP/CLAP-LICENSES.txt" "$root/app/licenses/"; fi
pkgbuild --root "$root/vst3" --install-location "/Library/Audio/Plug-Ins/VST3" --identifier io.ghostblock.plugins.vst3 --version "$VER" "$root/vst3.pkg"
pkgbuild --root "$root/au"   --install-location "/Library/Audio/Plug-Ins/Components" --identifier io.ghostblock.plugins.au --version "$VER" "$root/au.pkg"
if [ "$HAS_CLAP" = 1 ]; then pkgbuild --root "$root/clap" --install-location "/Library/Audio/Plug-Ins/CLAP" --identifier io.ghostblock.plugins.clap --version "$VER" "$root/clap.pkg"; fi
pkgbuild --root "$root/app"  --install-location "/Applications/GhostBlock" --identifier io.ghostblock.plugins.apps --version "$VER" "$root/apps.pkg"
CLAP_LINE=""; CLAP_CHOICE=""; CLAP_REF=""
if [ "$HAS_CLAP" = 1 ]; then
  CLAP_LINE='<line choice="clap"/>'
  CLAP_CHOICE='<choice id="clap" title="CLAP plug-ins" description="For Bitwig, Reaper, FL Studio 2024+, Studio One..."><pkg-ref id="io.ghostblock.plugins.clap"/></choice>'
  CLAP_REF="<pkg-ref id=\"io.ghostblock.plugins.clap\" version=\"$VER\">clap.pkg</pkg-ref>"
fi
cat > "$root/dist.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>GhostBlock Plugins</title>
  <license file="EULA.txt"/>
  <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
  <choices-outline><line choice="vst3"/><line choice="au"/>$CLAP_LINE<line choice="apps"/></choices-outline>
  <choice id="vst3" title="VST3 plug-ins" description="For FL Studio, Ableton, Bitwig, Reaper, Cubase..."><pkg-ref id="io.ghostblock.plugins.vst3"/></choice>
  <choice id="au" title="Audio Units" description="For Logic, GarageBand (and FL/Ableton on Mac)"><pkg-ref id="io.ghostblock.plugins.au"/></choice>
  $CLAP_CHOICE
  <choice id="apps" title="Standalone apps" description="Play without a DAW"><pkg-ref id="io.ghostblock.plugins.apps"/></choice>
  <pkg-ref id="io.ghostblock.plugins.vst3" version="$VER">vst3.pkg</pkg-ref>
  <pkg-ref id="io.ghostblock.plugins.au" version="$VER">au.pkg</pkg-ref>
  <pkg-ref id="io.ghostblock.plugins.apps" version="$VER">apps.pkg</pkg-ref>
  $CLAP_REF
</installer-gui-script>
XML
mkdir -p "$root/res" && cp "$(dirname "$0")/EULA.txt" "$root/res/"
# (no empty-array expansion: macOS ships bash 3.2, where "${arr[@]}" of an empty array fails under set -u)
if [ -n "${MAC_INSTALLER_IDENTITY:-}" ]; then
  productbuild --distribution "$root/dist.xml" --package-path "$root" --resources "$root/res" "$root/unsigned-product.pkg"
  rm -f "$OUT"
  if [ -n "${MAC_KEYCHAIN:-}" ]; then
    productsign --sign "$MAC_INSTALLER_IDENTITY" --timestamp --keychain "$MAC_KEYCHAIN" "$root/unsigned-product.pkg" "$OUT"
  else
    productsign --sign "$MAC_INSTALLER_IDENTITY" --timestamp "$root/unsigned-product.pkg" "$OUT"
  fi
  pkgutil --check-signature "$OUT"
else
  productbuild --distribution "$root/dist.xml" --package-path "$root" --resources "$root/res" "$OUT"
fi
ls -la "$OUT"
