#!/bin/bash
# shellcheck disable=SC2016,SC2034,SC2015  # check bodies + stub bodies are single-quoted on purpose (eval / run later)
# Linux dry run of the Mac signing guard (no macOS needed). Stubs security / codesign / productsign / productbuild / pkgbuild /
# xcrun / spctl / pkgutil / ditto / strip / file / plutil / curl, builds a fake CMake artefact tree, and checks:
#   1. unsigned path: build-mac-pkg.sh without MAC_* env makes exactly the same calls as the origin/main version
#   2. workflow: without secrets the mac job has the same steps as before (new steps are guarded by HAS_APPLE), STATUS.txt
#      keeps the commit line first
#   3. signed path: keychain -> identities -> nested code -> bundles -> productbuild -> productsign -> notarytool -> stapler ->
#      spctl -> pkgutil, in that order; no --deep; every signature verified; temp keychain / key files removed; no secret printed
#   4. failure paths (missing secret, bad p12, codesign error, notarization Invalid, staple error, spctl reject, step killed):
#      exit 0, the unsigned pkg stays, build-mac.log says SIGNING FAILED + NOTARIZED: no
#
#   bash ghosty-pro/packaging/tests/sign-dryrun.sh        (exit 0 = all checks passed)
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
PK="$(cd "$HERE/.." && pwd)"
REPO="$(cd "$PK/../.." && pwd)"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
pass=0; failn=0
check() { if eval "$2"; then pass=$((pass + 1)); echo "  ok   $1"; else failn=$((failn + 1)); echo "  FAIL $1"; fi; }

# ---------- stubs ----------
S="$T/stubs"; mkdir -p "$S"
export CALLS="$T/calls.log"
mk() { printf '#!/bin/bash\n%s\n' "$2" > "$S/$1"; chmod +x "$S/$1"; }
LOGC='printf "%s" "$(basename "$0")" >> "$CALLS"; for a in "$@"; do printf " [%s]" "$a" >> "$CALLS"; done; echo >> "$CALLS"'
mk codesign "$LOGC"'
case " $* " in *" --verify "*) exit 0;; esac
[ "${STUB_FAIL:-}" = codesign ] && { echo "codesign: errSecInternalComponent" >&2; exit 1; }
exit 0'
mk productbuild "$LOGC"'
out="${!#}"; echo "PKG" > "$out"'
mk pkgbuild "$LOGC"'
out="${!#}"; echo "COMPONENT" > "$out"'
mk productsign "$LOGC"'
n=$#; in="${@:n-1:1}"; out="${!#}"; cp "$in" "$out"; echo "SIGNED" >> "$out"'
mk pkgutil "$LOGC"'
f="${!#}"
if grep -q SIGNED "$f" 2>/dev/null; then printf "Package \"x\":\n   Status: signed by a developer certificate issued by Apple for distribution\n   Signed with a trusted timestamp on: 2026-10-26\n   1. Developer ID Installer: Test Owner (TEAM123)\n"; exit 0; fi
echo "Status: no signature"; exit 1'
mk ditto "$LOGC"'
mkdir -p "$(dirname "$2")"; cp -R "$1" "$2"'
mk strip "$LOGC"
mk lipo "$LOGC"
mk file 'if head -c5 "$1" 2>/dev/null | grep -q MACHO; then echo "$1: Mach-O universal binary"; else echo "$1: ASCII text"; fi'
mk plutil 'grep -o "<exe>[^<]*</exe>" "${!#}" | sed "s/<[^>]*>//g"'
mk curl 'exit 7'
mk spctl "$LOGC"'
[ "${STUB_FAIL:-}" = spctl ] && { echo "rejected"; exit 3; }
echo "${!#}: accepted"; echo "source=Notarized Developer ID"'
mk security "$LOGC"'
case "$1" in
  import) [ "${STUB_FAIL:-}" = import ] && { echo "security: SecKeychainItemImport: MAC verification failed (wrong password?)" >&2; exit 1; }; exit 0;;
  create-keychain) touch "${!#}";;
  delete-keychain) rm -f "${!#}";;
  find-identity)
    case " $* " in *" -p codesigning "*) echo "  1) AAAA \"Developer ID Application: Test Owner (TEAM123)\"";;
      *) echo "  1) AAAA \"Developer ID Application: Test Owner (TEAM123)\""; echo "  2) BBBB \"Developer ID Installer: Test Owner (TEAM123)\"";; esac
    echo "     2 valid identities found";;
  list-keychains) [ "$#" -le 2 ] && echo "    \"/Users/runner/Library/Keychains/login.keychain-db\"";;
esac
exit 0'
mk xcrun "$LOGC"'
case "$1 $2" in
  "notarytool submit")
    [ "${STUB_FAIL:-}" = hang ] && sleep 8
    if [ "${STUB_FAIL:-}" = notary ]; then echo "{\"id\":\"sub-123\",\"status\":\"Invalid\",\"message\":\"Processing complete\"}"; exit 1; fi
    echo "Conducting pre-submission checks..." >&2
    echo "{\"id\":\"sub-123\",\"status\":\"Accepted\",\"message\":\"Processing complete\"}";;
  "notarytool log") echo "{\"issues\":[{\"severity\":\"error\",\"message\":\"The binary is not signed with a valid Developer ID certificate.\"}]}";;
  "stapler staple") [ "${STUB_FAIL:-}" = staple ] && { echo "CloudKit query failed" >&2; exit 65; }; echo "STAPLED" >> "${!#}"; echo "The staple and validate action worked!";;
  "stapler validate") grep -q STAPLED "${!#}" || exit 65; echo "The validate action worked!";;
esac
exit 0'
export PATH="$S:$PATH"

# ---------- fake CMake artefacts ----------
B="$T/build"
for t in "MrMickens:Mr Mickens" "Poltergeist:Poltergeist" "GhostyBeats:Ghosty Beats" "GhostySpectrum:Ghosty Spectrum" "GhostyPro:Ghosty Pro" "GhostBox:Ghost Box" "ArpEZator:ArpEZator" "GhostSlice:Ghost Slice"; do
  TG="${t%%:*}"; N="${t#*:}"; R="$B/${TG}_artefacts/Release"
  for d in "VST3/$N.vst3" "AU/$N.component" "Standalone/$N.app" "CLAP/$N.clap"; do
    mkdir -p "$R/$d/Contents/MacOS"; echo "MACHO $N" > "$R/$d/Contents/MacOS/$N"; chmod +x "$R/$d/Contents/MacOS/$N"
    echo "<plist><exe>$N</exe></plist>" > "$R/$d/Contents/Info.plist"
  done
done
# nested code in one app: a loose dylib, a helper binary next to the main one, and a framework (must be signed BEFORE the app)
A="$B/GhostyPro_artefacts/Release/Standalone/Ghosty Pro.app/Contents"
mkdir -p "$A/Frameworks/Foo.framework/Versions/A" "$A/Resources"
echo "MACHO dylib" > "$A/Frameworks/libextra.dylib"
echo "MACHO helper" > "$A/MacOS/helper"; chmod +x "$A/MacOS/helper"
echo "MACHO fw" > "$A/Frameworks/Foo.framework/Versions/A/Foo"; chmod +x "$A/Frameworks/Foo.framework/Versions/A/Foo"
echo "not code" > "$A/Resources/readme.txt"; chmod +x "$A/Resources/readme.txt"

norm() { sed -e "s#/tmp/tmp\.[A-Za-z0-9]*#ROOT#g" -e "s#${TMPDIR:-/tmp}/tmp\.[A-Za-z0-9]*#ROOT#g" -e "s#$1#PKGDIR#g" "$2"; }

echo "== 1. unsigned path identical to origin/main"
if OLD=$(git -C "$REPO" show origin/main:ghosty-pro/packaging/build-mac-pkg.sh 2>/dev/null) && echo "$OLD" | grep -q 'timestamp=none --sign "$ID"'; then
  OP="$T/oldpk"; mkdir -p "$OP"; cp -R "$PK/third_party" "$PK/EULA.txt" "$OP/"; echo "$OLD" > "$OP/build-mac-pkg.sh"; chmod +x "$OP/build-mac-pkg.sh"
  : > "$CALLS"; env -u MAC_APP_IDENTITY -u MAC_INSTALLER_IDENTITY bash "$OP/build-mac-pkg.sh" "$B" "$T/same.pkg" 3.1.0 > "$T/old.out" 2>&1; orc=$?; norm "$OP" "$CALLS" > "$T/old.calls"
  : > "$CALLS"; env -u MAC_APP_IDENTITY -u MAC_INSTALLER_IDENTITY bash "$PK/build-mac-pkg.sh" "$B" "$T/same.pkg" 3.1.0 > "$T/new.out" 2>&1; nrc=$?; norm "$PK" "$CALLS" > "$T/new.calls"
  check "old and new both succeed (exit $orc / $nrc)" '[ $orc -eq 0 ] && [ $nrc -eq 0 ]'
  check "identical call sequence ($(wc -l < "$T/new.calls") calls)" 'diff "$T/old.calls" "$T/new.calls" > "$T/calls.diff"'
  check "identical script output" 'diff <(norm "$OP" "$T/old.out") <(norm "$PK" "$T/new.out") >/dev/null'
  check "unsigned: ad-hoc --deep signing, no productsign, no timestamp" '! grep -q "^productsign" "$T/new.calls" && grep -q "\[--deep\]" "$T/new.calls" && ! grep -q "\[--timestamp\]" "$T/new.calls"'
else
  echo "  (skipped: origin/main version not available)"
fi

echo "== 2. workflow guard"
python3 - "$REPO" <<'PY' && pass=$((pass + 1)) && echo "  ok   workflow: old steps unchanged, new steps guarded, STATUS first line unchanged" || { failn=$((failn + 1)); echo "  FAIL workflow check"; }
import subprocess, sys, yaml
repo = sys.argv[1]
new = yaml.safe_load(open(f"{repo}/.github/workflows/ghosty-pro.yml"))
try:
    old = yaml.safe_load(subprocess.run(["git", "-C", repo, "show", "origin/main:.github/workflows/ghosty-pro.yml"], capture_output=True, text=True, check=True).stdout)
except Exception:
    print("  (origin/main workflow unavailable: only checking the new one)"); old = None
ns = new["jobs"]["mac"]["steps"]
guarded = [s for s in ns if "HAS_APPLE" in str(s.get("if", ""))]
assert [s["name"] for s in guarded] == ["Sign + notarize (only when the Apple secrets exist)", "Delete the signing keychain"], guarded
assert guarded[0].get("continue-on-error") is True and guarded[0].get("timeout-minutes")
assert "always()" in guarded[1]["if"]
assert "secrets." in new["jobs"]["mac"]["env"]["HAS_APPLE"]
for s in ns:  # secrets only reach the signing step
    if s not in guarded[:1]: assert "APPLE_" not in str(s.get("env", "")), s
rest = [s for s in ns if s not in guarded]
if old:
    os_ = old["jobs"]["mac"]["steps"]
    assert len(os_) == len(rest)
    for a, b in zip(os_, rest):
        if a.get("name") == "Status":
            al, bl = a["run"].splitlines(), b["run"].splitlines()
            removed = [l for l in al if l not in bl]
            assert not removed, removed
        else:
            assert a == b, (a, b)
    assert old["jobs"]["windows"] == new["jobs"]["windows"]
    assert old.get(True, old.get("on")) == new.get(True, new.get("on"))
st = [s for s in ns if s.get("name") == "Status"][0]["run"].splitlines()
i = [n for n, l in enumerate(st) if "> STATUS.txt" in l and ">> STATUS.txt" not in l]
assert len(i) == 1 and st[i[0]].strip().startswith('echo "commit ${GITHUB_SHA} result'), st[i[0]]
assert all(">> STATUS.txt" in l for n, l in enumerate(st) if "STATUS.txt" in l and n > i[0])  # later lines only append
assert "104000000" in "\n".join(st)
PY

# ---------- mac-sign.sh scenarios ----------
SECRET_PW="pw-$RANDOM-secret"
setsecrets() {
  export APPLE_DEV_ID_APP_P12; APPLE_DEV_ID_APP_P12=$(printf 'fake app p12' | base64 -w0)
  export APPLE_DEV_ID_INSTALLER_P12; APPLE_DEV_ID_INSTALLER_P12=$(printf 'fake installer p12' | base64 -w0)
  export APPLE_CERT_PASSWORD="$SECRET_PW" APPLE_TEAM_ID=TEAM123 APPLE_NOTARY_KEY_ID=KEY4567 APPLE_NOTARY_ISSUER_ID=issuer-uuid
  export APPLE_NOTARY_KEY_P8; APPLE_NOTARY_KEY_P8=$(printf -- '-----BEGIN PRIVATE KEY-----\nabc\n-----END PRIVATE KEY-----\n' | base64 -w0)
}
run_sign() {  # $1 = scenario dir; runs like the workflow step does (cwd = workspace)
  local D="$1"; rm -rf "$D"; mkdir -p "$D/out" "$D/rt"
  echo "UNSIGNED" > "$D/out/GhostBlock-Plugins-Mac.pkg"; echo "build exit 0" > "$D/build-mac.log"
  : > "$CALLS"
  ( cd "$D" && RUNNER_TEMP="$D/rt" ${RUNWRAP:-} "$PK/mac-sign.sh" run "$B" out/GhostBlock-Plugins-Mac.pkg 3.1.0 > "$D/stdout" 2>&1 ); RC=$?
  cp "$CALLS" "$D/calls"
}
line() { grep -n -m1 -- "$2" "$1/calls" | cut -d: -f1; }   # first call line matching
before() { local a b; a=$(line "$1" "$2"); b=$(line "$1" "$3"); [ -n "$a" ] && [ -n "$b" ] && [ "$a" -lt "$b" ]; }
soft() {  # common checks for every failure path
  local D="$1"
  check "$2: exit 0" '[ $RC -eq 0 ]'
  check "$2: unsigned pkg untouched" '[ "$(cat "$D/out/GhostBlock-Plugins-Mac.pkg")" = UNSIGNED ]'
  check "$2: log says SIGNING FAILED + NOTARIZED: no" 'grep -q "^SIGNING FAILED: " "$D/build-mac.log" && grep -q "^NOTARIZED: no" "$D/build-mac.log"'
  check "$2: status line says signed: no notarized: no" 'grep -q "^signed: no notarized: no (SIGNING FAILED" "$D/sign-status.txt"'
  check "$2: temp keychain + key files removed" '[ ! -e "$D/rt/ghostblock-signing.keychain-db" ] && [ ! -e "$D/rt/ghostblock-sign" ]'
  check "$2: no secret value printed" '! grep -q -- "$SECRET_PW" "$D/stdout" "$D/build-mac.log"'
}

echo "== 3. signed path (all secrets, everything succeeds)"
setsecrets; D="$T/ok"; run_sign "$D"
check "exit 0" '[ $RC -eq 0 ]'
check "final pkg is the signed + stapled one" 'grep -q SIGNED "$D/out/GhostBlock-Plugins-Mac.pkg" && grep -q STAPLED "$D/out/GhostBlock-Plugins-Mac.pkg"'
check "STATUS line: signed: yes notarized: yes" '[ "$(cat "$D/sign-status.txt")" = "signed: yes notarized: yes" ]'
check "log: NOTARIZED: yes, no SIGNING FAILED" 'grep -q "^NOTARIZED: yes" "$D/build-mac.log" && ! grep -q "SIGNING FAILED" "$D/build-mac.log"'
check "order: create-keychain < import < partition-list < find-identity" 'before "$D" "^security \[create-keychain\]" "^security \[import\]" && before "$D" "^security \[import\]" "set-key-partition-list" && before "$D" "set-key-partition-list" "find-identity"'
check "order: find-identity < first codesign < productbuild < productsign" 'before "$D" find-identity "^codesign" && before "$D" "^codesign" "^productbuild" && before "$D" "^productbuild" "^productsign"'
check "order: productsign < notarytool submit < stapler staple < stapler validate < spctl" 'before "$D" "^productsign" "notarytool\] \[submit" && before "$D" "notarytool\] \[submit" "stapler\] \[staple" && before "$D" "stapler\] \[staple" "stapler\] \[validate" && before "$D" "stapler\] \[validate" "^spctl"'
check "no --deep and no ad-hoc in the signed build" '! grep "^codesign" "$D/calls" | grep -q -- "\[--deep\]" && ! grep "^codesign" "$D/calls" | grep -q "\[--sign\] \[-\]"'
check "every signature: --force --timestamp --options runtime --keychain + Developer ID Application" '! grep "^codesign \[--force\]" "$D/calls" | grep -v -q "\[--force\] \[--timestamp\] \[--options\] \[runtime\] \[--keychain\] \[[^]]*\] \[--sign\] \[Developer ID Application: Test Owner (TEAM123)\]"'
nsign=$(grep -c "^codesign \[--force\]" "$D/calls"); nver=$(grep -c "^codesign \[--verify\] \[--strict\] \[--verbose=2\]" "$D/calls")
check "every signature is verified --strict ($nsign signed, $nver verified)" '[ "$nsign" -eq "$nver" ] && [ "$nsign" -gt 0 ]'
check "32 plug-in/app bundles + 3 nested items signed" '[ "$nsign" -eq 35 ]'
check "nested dylib, helper and framework signed before their app; text file and main binary not signed alone" \
  'a=$(grep -n "^codesign \[--force\].*libextra.dylib\]" "$D/calls" | cut -d: -f1); h=$(grep -n "^codesign \[--force\].*MacOS/helper\]" "$D/calls" | cut -d: -f1); f=$(grep -n "^codesign \[--force\].*Foo.framework\]" "$D/calls" | cut -d: -f1); p=$(grep -n "^codesign \[--force\].*/app/Ghosty Pro.app\]$" "$D/calls" | cut -d: -f1); [ -n "$a" ] && [ -n "$h" ] && [ -n "$f" ] && [ -n "$p" ] && [ "$a" -lt "$p" ] && [ "$h" -lt "$p" ] && [ "$f" -lt "$p" ] && ! grep -q "^codesign \[--force\].*readme.txt" "$D/calls" && ! grep -q "^codesign \[--force\].*MacOS/Ghosty Pro\]" "$D/calls"'
check "productsign uses Developer ID Installer + --timestamp + keychain" 'grep -q "^productsign \[--sign\] \[Developer ID Installer: Test Owner (TEAM123)\] \[--timestamp\] \[--keychain\]" "$D/calls"'
check "notarytool: API key auth, --wait --timeout 60m" 'grep "notarytool\] \[submit" "$D/calls" | grep -q "\[--key\] \[.*AuthKey_notary.p8\] \[--key-id\] \[KEY4567\] \[--issuer\] \[issuer-uuid\] \[--wait\] \[--timeout\] \[60m\]"'
check "keychain deleted + temp dir removed" 'grep -q "^security \[delete-keychain\]" "$D/calls" && [ ! -e "$D/rt/ghostblock-signing.keychain-db" ] && [ ! -e "$D/rt/ghostblock-sign" ]'
check "derived values masked (::add-mask::)" 'grep -q "::add-mask::Developer ID Application" "$D/stdout" && grep -q "::add-mask::Developer ID Installer" "$D/stdout"'
check "no secret value printed" '! grep -q -- "$SECRET_PW" "$D/stdout" "$D/build-mac.log" && ! grep -q -- "$APPLE_NOTARY_KEY_P8" "$D/stdout" "$D/build-mac.log"'

echo "== 4. failure paths (fail-soft)"
setsecrets; unset APPLE_NOTARY_KEY_ID; D="$T/missing"; run_sign "$D"; soft "$D" "missing secret"
check "missing secret: names it, no signing attempted" 'grep -q "missing secrets: APPLE_NOTARY_KEY_ID" "$D/build-mac.log" && ! grep -q "^codesign" "$D/calls"'
setsecrets; export APPLE_DEV_ID_APP_P12='!!not base64!!'; D="$T/badb64"; run_sign "$D"; soft "$D" "bad base64"
setsecrets; D="$T/import"; STUB_FAIL=import run_sign "$D"; soft "$D" "bad p12 password"
check "bad p12: no codesign" '! grep -q "^codesign" "$D/calls"'
setsecrets; export APPLE_TEAM_ID=OTHER99; D="$T/team"; run_sign "$D"; soft "$D" "wrong team id"
setsecrets; D="$T/codesign"; STUB_FAIL=codesign run_sign "$D"; soft "$D" "codesign error"
check "codesign error: no notarization submitted" '! grep -q "notarytool" "$D/calls"'
setsecrets; D="$T/notary"; STUB_FAIL=notary run_sign "$D"; soft "$D" "notarization Invalid"
check "notarization Invalid: notarytool log fetched into build-mac.log" 'grep -q "notarytool\] \[log\] \[sub-123\]" "$D/calls" && grep -q "not signed with a valid Developer ID" "$D/build-mac.log" && grep -q "status Invalid" "$D/build-mac.log"'
check "notarization Invalid: no staple" '! grep -q "stapler" "$D/calls"'
setsecrets; D="$T/staple"; STUB_FAIL=staple run_sign "$D"; soft "$D" "staple error"
setsecrets; D="$T/spctl"; STUB_FAIL=spctl run_sign "$D"; soft "$D" "spctl reject"
setsecrets; D="$T/hang"; STUB_FAIL=hang RUNWRAP="timeout -s KILL 3" run_sign "$D"
check "step killed mid-notarization: unsigned pkg untouched" '[ "$(cat "$D/out/GhostBlock-Plugins-Mac.pkg")" = UNSIGNED ]'
check "step killed: status says did not finish (Status step turns it into SIGNING FAILED)" 'grep -q "did not finish" "$D/sign-status.txt"'
check "step killed: cleanup subcommand removes the keychain + temp files" '[ -e "$D/rt/ghostblock-signing.keychain-db" ] && RUNNER_TEMP="$D/rt" "$PK/mac-sign.sh" cleanup >/dev/null && [ ! -e "$D/rt/ghostblock-signing.keychain-db" ] && [ ! -e "$D/rt/ghostblock-sign" ]'

echo
echo "passed $pass, failed $failn"
[ $failn -eq 0 ]
