#!/bin/bash
# Checks a GhostBlock Mac installer on a Mac: is the pkg signed (Developer ID Installer), notarized + stapled, accepted by
# Gatekeeper, and is every plug-in / app inside it signed with Developer ID + hardened runtime + secure timestamp?
#
#   ghosty-pro/packaging/sign-check.sh ~/Downloads/GhostBlock-Plugins-Mac.pkg
#
# Read-only: it expands the pkg into a temp folder (deleted afterwards) and installs nothing. Exit 0 = all good.
set -u
PKG="${1:-}"
if [ -z "$PKG" ] || [ ! -f "$PKG" ]; then echo "usage: $0 <path to .pkg>" >&2; exit 2; fi
if [ "$(uname -s)" != Darwin ]; then echo "run this on a Mac (it needs pkgutil, spctl, stapler, codesign)" >&2; exit 2; fi
bad=0
ok()   { echo "  OK    $*"; }
nope() { echo "  FAIL  $*"; bad=1; }

echo "== $PKG"
echo "-- pkg signature (pkgutil --check-signature)"
sig=$(pkgutil --check-signature "$PKG" 2>&1)
echo "$sig" | sed 's/^/        /'
if echo "$sig" | grep -q "Developer ID Installer"; then ok "signed with a Developer ID Installer certificate"; else nope "not signed with Developer ID Installer"; fi
if echo "$sig" | grep -qi "trusted timestamp"; then ok "has a trusted timestamp"; else nope "no trusted timestamp"; fi

echo "-- notarization ticket (xcrun stapler validate)"
if xcrun stapler validate "$PKG" >/dev/null 2>&1; then ok "notarization ticket stapled"; else nope "no stapled notarization ticket"; fi

echo "-- Gatekeeper (spctl --assess --type install)"
sp=$(spctl --assess --type install -vv "$PKG" 2>&1); rc=$?
echo "$sp" | sed 's/^/        /'
if [ $rc -eq 0 ] && echo "$sp" | grep -q "Notarized Developer ID"; then ok "Gatekeeper accepts it as Notarized Developer ID (opens with a normal double-click)"
elif [ $rc -eq 0 ]; then nope "accepted, but not as 'Notarized Developer ID' (Gatekeeper may be off on this Mac: spctl --status)"
else nope "Gatekeeper rejects it (users would need right-click > Open / System Settings > Open Anyway)"; fi

echo "-- bundles inside the pkg (codesign)"
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
if pkgutil --expand-full "$PKG" "$tmp/x" >/dev/null 2>&1; then
  n=0
  while IFS= read -r b; do
    n=$((n + 1)); name="${b#"$tmp"/x/}"
    if ! codesign --verify --strict --verbose=2 "$b" >/dev/null 2>&1; then nope "$name: codesign --verify --strict fails"; continue; fi
    info=$(codesign -dvv "$b" 2>&1)
    probs=""
    echo "$info" | grep -q "Authority=Developer ID Application" || probs="$probs not-Developer-ID"
    echo "$info" | grep -q "flags=.*runtime" || probs="$probs no-hardened-runtime"
    echo "$info" | grep -q "^Timestamp=" || probs="$probs no-secure-timestamp"
    if [ -z "$probs" ]; then ok "$name"; else nope "$name:$probs"; fi
  done < <(find "$tmp/x" -type d \( -name '*.vst3' -o -name '*.component' -o -name '*.clap' -o -name '*.app' \) -prune)
  [ $n -gt 0 ] || nope "no .vst3 / .component / .clap / .app found inside the pkg"
else
  nope "pkgutil --expand-full failed"
fi

echo
if [ $bad -eq 0 ]; then echo "ALL GOOD: signed, notarized, stapled, Gatekeeper-approved."; else echo "PROBLEMS FOUND (see FAIL lines above)."; fi
exit $bad
