#!/bin/bash
# Developer ID signing + Apple notarization for the GhostBlock Mac pkg (CI only; see SIGNING.md).
#
#   mac-sign.sh run <cmake build dir> <final pkg path> <version>
#   mac-sign.sh cleanup
#
# The workflow runs this ONLY when at least one APPLE_* secret exists. The unsigned pkg at <final pkg path> has already been
# built by build-mac-pkg.sh (exactly as without secrets). This script builds a second, signed pkg in $RUNNER_TEMP, notarizes,
# staples and verifies it, and only when every check passes moves it over <final pkg path>. Any failure leaves the unsigned
# pkg in place, writes "SIGNING FAILED: <reason>" + "NOTARIZED: no" into build-mac.log, and exits 0 (fail-soft: signing can
# never block a release).
#
# Env (from GitHub secrets, all base64 where noted): APPLE_DEV_ID_APP_P12 (b64), APPLE_DEV_ID_INSTALLER_P12 (b64),
# APPLE_CERT_PASSWORD, APPLE_TEAM_ID, APPLE_NOTARY_KEY_P8 (b64), APPLE_NOTARY_KEY_ID, APPLE_NOTARY_ISSUER_ID.
# Optional: SIGN_LOG (default build-mac.log), SIGN_STATUS (default sign-status.txt, one line for STATUS.txt),
# SIGN_BUILD_LOG (default sign-build.log). Never run with `set -x`: secrets pass through here.
# macOS ships bash 3.2: no mapfile, no ${x,,}, no associative arrays.
set -u
set +x

HERE="$(cd "$(dirname "$0")" && pwd)"
TMPROOT="${RUNNER_TEMP:-${TMPDIR:-/tmp}}"
W="$TMPROOT/ghostblock-sign"
KC="$TMPROOT/ghostblock-signing.keychain-db"
LOG="${SIGN_LOG:-build-mac.log}"
STATUS="${SIGN_STATUS:-sign-status.txt}"
BLOG="${SIGN_BUILD_LOG:-sign-build.log}"

say() { echo "$*"; echo "$*" >> "$LOG"; }

cleanup() {
  if [ -e "$KC" ]; then security delete-keychain "$KC" >/dev/null 2>&1 || rm -f "$KC"; fi
  rm -rf "$W"
}

if [ "${1:-}" = cleanup ]; then cleanup; echo "signing keychain + temp files removed"; exit 0; fi
if [ "${1:-}" != run ] || [ $# -ne 4 ]; then echo "usage: $0 run <build dir> <final pkg> <version> | $0 cleanup" >&2; exit 2; fi
BUILD="$2"; FINAL="$3"; VER="$4"

# first thing: a status that says "did not finish", so a timeout / crash mid-way is still reported by the Status step
echo "signed: no notarized: no (SIGNING FAILED: signing step did not finish - timeout or crash; shipped pkg is unsigned)" > "$STATUS"

fail() {
  say "SIGNING FAILED: $1"
  say "NOTARIZED: no"
  say "shipped pkg: UNSIGNED (the signing problem did not block the build)"
  echo "signed: no notarized: no (SIGNING FAILED: $1; shipped pkg is unsigned)" > "$STATUS"
  cleanup
  exit 0
}
trap 'cleanup' EXIT

say "=== Developer ID signing + notarization ==="

# 1) every secret present?
missing=""
for v in APPLE_DEV_ID_APP_P12 APPLE_DEV_ID_INSTALLER_P12 APPLE_CERT_PASSWORD APPLE_TEAM_ID APPLE_NOTARY_KEY_P8 APPLE_NOTARY_KEY_ID APPLE_NOTARY_ISSUER_ID; do
  eval "val=\${$v:-}"
  [ -n "$val" ] || missing="$missing $v"
done
unset val
[ -z "$missing" ] || fail "missing secrets:$missing"
for t in security codesign productsign productbuild pkgbuild xcrun pkgutil spctl python3 openssl; do
  command -v "$t" >/dev/null 2>&1 || fail "tool not found: $t"
done
[ -f "$FINAL" ] || fail "unsigned pkg not found at $FINAL"
echo "::add-mask::$APPLE_TEAM_ID"
echo "::add-mask::$APPLE_NOTARY_KEY_ID"
echo "::add-mask::$APPLE_NOTARY_ISSUER_ID"

rm -rf "$W"; mkdir -p "$W/out"; chmod 700 "$W"

# 2) temporary keychain with both identities
printf '%s' "$APPLE_DEV_ID_APP_P12" | base64 --decode > "$W/app.p12" 2>/dev/null && [ -s "$W/app.p12" ] || fail "APPLE_DEV_ID_APP_P12 is not valid base64"
printf '%s' "$APPLE_DEV_ID_INSTALLER_P12" | base64 --decode > "$W/inst.p12" 2>/dev/null && [ -s "$W/inst.p12" ] || fail "APPLE_DEV_ID_INSTALLER_P12 is not valid base64"
KCPW=$(openssl rand -hex 24) || fail "could not make a keychain password"
echo "::add-mask::$KCPW"
security delete-keychain "$KC" >/dev/null 2>&1 || true
security create-keychain -p "$KCPW" "$KC" >/dev/null || fail "security create-keychain failed"
security set-keychain-settings -lut 21600 "$KC" >/dev/null || fail "security set-keychain-settings failed"
security unlock-keychain -p "$KCPW" "$KC" >/dev/null || fail "security unlock-keychain failed"
security import "$W/app.p12" -k "$KC" -f pkcs12 -P "$APPLE_CERT_PASSWORD" -T /usr/bin/codesign -T /usr/bin/productsign -T /usr/bin/productbuild >/dev/null 2>&1 \
  || fail "importing the Developer ID Application .p12 failed (wrong APPLE_CERT_PASSWORD or not a .p12 with its private key?)"
security import "$W/inst.p12" -k "$KC" -f pkcs12 -P "$APPLE_CERT_PASSWORD" -T /usr/bin/codesign -T /usr/bin/productsign -T /usr/bin/productbuild >/dev/null 2>&1 \
  || fail "importing the Developer ID Installer .p12 failed (wrong APPLE_CERT_PASSWORD or not a .p12 with its private key?)"
rm -f "$W/app.p12" "$W/inst.p12"
# Apple's Developer ID intermediate (G2), in case the .p12 was exported without it (best effort: runners usually have it)
if curl -fsSL --max-time 30 -o "$W/DeveloperIDG2CA.cer" https://www.apple.com/certificateauthority/DeveloperIDG2CA.cer 2>/dev/null; then
  security import "$W/DeveloperIDG2CA.cer" -k "$KC" >/dev/null 2>&1 || true
fi
# let codesign / productsign use the keys without a GUI prompt
security set-key-partition-list -S apple-tool:,apple:,codesign: -s -k "$KCPW" "$KC" >/dev/null 2>&1 || fail "security set-key-partition-list failed"
# put the keychain first in the search list, keep the existing ones
old=()
while IFS= read -r l; do
  l="${l#"${l%%[![:space:]]*}"}"; l="${l%\"}"; l="${l#\"}"
  [ -n "$l" ] && [ "$l" != "$KC" ] && old+=("$l")
done < <(security list-keychains -d user 2>/dev/null)
if [ ${#old[@]} -gt 0 ]; then security list-keychains -d user -s "$KC" "${old[@]}"; else security list-keychains -d user -s "$KC"; fi \
  || fail "security list-keychains failed"

# 3) find both identities for APPLE_TEAM_ID
find_id() {  # $1 = "Developer ID Application" | "Developer ID Installer"; $2 = extra find-identity args
  local out
  # shellcheck disable=SC2086
  out=$(security find-identity -v $2 "$KC" 2>/dev/null | grep -o "\"$1: [^\"]*($APPLE_TEAM_ID)\"" | head -1 | tr -d '"')
  # shellcheck disable=SC2086
  [ -n "$out" ] || out=$(security find-identity $2 "$KC" 2>/dev/null | grep -o "\"$1: [^\"]*($APPLE_TEAM_ID)\"" | head -1 | tr -d '"')
  printf '%s' "$out"
}
APP_ID=$(find_id "Developer ID Application" "-p codesigning")
INST_ID=$(find_id "Developer ID Installer" "")
[ -n "$APP_ID" ] || fail "no 'Developer ID Application: ... ($APPLE_TEAM_ID)' identity in the .p12 (wrong certificate type or team id?)"
[ -n "$INST_ID" ] || fail "no 'Developer ID Installer: ... ($APPLE_TEAM_ID)' identity in the .p12 (wrong certificate type or team id?)"
echo "::add-mask::$APP_ID"
echo "::add-mask::$INST_ID"
say "identities: Developer ID Application + Developer ID Installer found"

# 4) signed build (same artefacts, same steps as the unsigned pkg; only the signing differs)
PKG="$W/out/$(basename "$FINAL")"
if ! MAC_APP_IDENTITY="$APP_ID" MAC_INSTALLER_IDENTITY="$INST_ID" MAC_KEYCHAIN="$KC" "$HERE/build-mac-pkg.sh" "$BUILD" "$PKG" "$VER" > "$BLOG" 2>&1; then
  say "--- signed build log (last 40 lines) ---"; tail -n 40 "$BLOG" | cut -c1-400 >> "$LOG"
  fail "signed build failed (codesign / productsign; see the lines above and sign-build-tail.log)"
fi
[ -f "$PKG" ] || fail "signed build produced no pkg"
sig=$(pkgutil --check-signature "$PKG" 2>&1) || { echo "$sig" >> "$LOG"; fail "pkgutil --check-signature rejects the signed pkg"; }
echo "$sig" | grep -q "Developer ID Installer" || { echo "$sig" >> "$LOG"; fail "pkg is not signed with a Developer ID Installer certificate"; }
say "signed: yes (bundles: Developer ID Application, hardened runtime, timestamp; pkg: Developer ID Installer)"

# 5) notarize (App Store Connect API key), wait up to 60 min
P8="$W/AuthKey_notary.p8"
( umask 077; printf '%s' "$APPLE_NOTARY_KEY_P8" | base64 --decode > "$P8" 2>/dev/null ) && [ -s "$P8" ] || fail "APPLE_NOTARY_KEY_P8 is not valid base64"
grep -q "PRIVATE KEY" "$P8" || fail "APPLE_NOTARY_KEY_P8 does not decode to a .p8 private key"
say "notarization: submitting $(basename "$PKG") ($(du -h "$PKG" | cut -f1)) and waiting (up to 60 min)..."
xcrun notarytool submit "$PKG" --key "$P8" --key-id "$APPLE_NOTARY_KEY_ID" --issuer "$APPLE_NOTARY_ISSUER_ID" \
  --wait --timeout 60m --output-format json > "$W/notary.json" 2> "$W/notary.err"
nrc=$?
jget() {  # the final JSON object of `notarytool --output-format json` (whole file, else the last line that parses)
  python3 -c 'import json,sys
t = open(sys.argv[1]).read()
cands = [t] + t.splitlines()[::-1]
v = ""
for c in cands:
    try:
        d = json.loads(c)
    except Exception:
        continue
    if isinstance(d, dict) and sys.argv[2] in d:
        v = d[sys.argv[2]]
        break
print(v)' "$W/notary.json" "$1" 2>/dev/null
}
SUB_ID=$(jget id); NSTATUS=$(jget status)
say "notarization: submission ${SUB_ID:-?} status ${NSTATUS:-?} (notarytool exit $nrc)"
if [ "$NSTATUS" != "Accepted" ]; then
  head -c 2000 "$W/notary.err" >> "$LOG" 2>/dev/null
  if [ -n "$SUB_ID" ]; then
    say "--- notarytool log $SUB_ID ---"
    xcrun notarytool log "$SUB_ID" --key "$P8" --key-id "$APPLE_NOTARY_KEY_ID" --issuer "$APPLE_NOTARY_ISSUER_ID" 2>&1 | head -n 200 >> "$LOG"
  fi
  rm -f "$P8"
  fail "notarization not accepted (status ${NSTATUS:-none}, notarytool exit $nrc)"
fi
rm -f "$P8"

# 6) staple + verify everything; any failure = ship the unsigned pkg
xcrun stapler staple "$PKG" >> "$LOG" 2>&1 || fail "xcrun stapler staple failed"
xcrun stapler validate "$PKG" >> "$LOG" 2>&1 || fail "xcrun stapler validate failed"
sp=$(spctl --assess --type install -vv "$PKG" 2>&1); src=$?
echo "$sp" >> "$LOG"
[ $src -eq 0 ] || fail "spctl --assess --type install rejects the pkg"
pkgutil --check-signature "$PKG" >> "$LOG" 2>&1 || fail "pkgutil --check-signature failed after stapling"

mv -f "$PKG" "$FINAL" || fail "could not move the signed pkg into place"
say "NOTARIZED: yes (ticket stapled, Gatekeeper accepts it)"
say "shipped pkg: SIGNED + NOTARIZED"
echo "signed: yes notarized: yes" > "$STATUS"
exit 0
