# Mac installer signing + notarization

When the seven Apple secrets below exist in the GitHub repo, the Mac CI build (`.github/workflows/ghosty-pro.yml`, job `mac`):

1. builds the unsigned pkg exactly as before (`build-mac-pkg.sh`), then, in the step **Sign + notarize**, runs `mac-sign.sh`:
2. makes a temporary keychain, imports both certificates, lets `codesign` use them without a prompt;
3. builds the pkg again with every bundle signed by **Developer ID Application** (nested code first, then each `.vst3`,
   `.component`, `.clap` and standalone `.app`; hardened runtime, secure timestamp, no `--deep`; each one checked with
   `codesign --verify --strict`), and signs the pkg with **Developer ID Installer** (`productsign`);
4. sends it to Apple (`xcrun notarytool submit --wait`, up to 60 min), staples the ticket (`xcrun stapler staple`), and checks
   it (`stapler validate`, `spctl --assess --type install`, `pkgutil --check-signature`);
5. only if **all** of that passed, replaces `out/GhostBlock-Plugins-Mac.pkg` with the signed one. The upload, the public
   Release `latest` and the `installer-23` friend link then carry the signed, notarized pkg (same file names).
6. deletes the keychain and the key files (step **Delete the signing keychain**, always runs).

**Fail-soft.** If anything goes wrong (a missing or wrong secret, codesign error, Apple rejects it, timeout), the unsigned pkg
ships exactly as before and the build stays green. You see it in the status branch `ghosty-pro-build-mac`:

- `build-mac.log` gets `SIGNING FAILED: <reason>` and `NOTARIZED: no` (plus Apple's notarization log when Apple rejected
  it), or `NOTARIZED: yes (ticket stapled, Gatekeeper accepts it)` and `shipped pkg: SIGNED + NOTARIZED` when it worked;
- `STATUS.txt` line 1 is unchanged (`commit <sha> result ...`); line 2 is `signed: yes notarized: yes` or
  `signed: no notarized: no` (with the reason in brackets when signing was tried and failed);
- `sign-build-tail.log` holds the end of the signed build's output when signing was tried.

**No secrets = no change.** Without any of the secrets the two signing steps are skipped and the build is the same unsigned
pkg as before (only `STATUS.txt` gains the line `signed: no notarized: no`).

## What the owner must do (once)

You need: a Mac with Xcode (or Keychain Access), the Apple Developer account (the **Account Holder** login: only the
Account Holder can create Developer ID certificates), and admin access to the GitHub repo.

### 1. Create the two Developer ID certificates

Easiest, in Xcode: **Xcode > Settings > Accounts** > select your Apple ID > select the team > **Manage Certificates...** >
**+** > **Developer ID Application**, then **+** again > **Developer ID Installer**.

Or in the browser: <https://developer.apple.com/account/resources/certificates/list> > **+** > under *Software* choose
**Developer ID Application** (G2 Sub-CA) > upload a CSR (Keychain Access > Certificate Assistant > *Request a Certificate From
a Certificate Authority...* > your email, *Saved to disk*) > download the `.cer` > double-click it. Repeat for
**Developer ID Installer** (the same CSR is fine).

### 2. Export both as .p12 files (same password for both)

Keychain Access > **login** keychain > **My Certificates**. You should see `Developer ID Application: <Name> (<TEAMID>)` and
`Developer ID Installer: <Name> (<TEAMID>)`, each with a private key under it (click the arrow). For each one:
right-click > **Export...** > format *Personal Information Exchange (.p12)* > save as `DevIDApp.p12` / `DevIDInstaller.p12` >
give it a password. **Use the same password for both** (it goes into one secret, `APPLE_CERT_PASSWORD`).

If there is no private key under a certificate, it was made on another Mac: export it there, or create a new one.

### 3. Your Team ID

<https://developer.apple.com/account> > **Membership details** > *Team ID* (10 characters, e.g. `AB12CD34EF`). It is also
the part in brackets in the certificate names.

### 4. An App Store Connect API key for notarization

<https://appstoreconnect.apple.com/access/integrations/api> (Users and Access > Integrations > App Store Connect API).
If asked, click **Request Access** first (Account Holder). Then **Team Keys** > **+** > name `GhostBlock notarization` >
Access **Developer** (enough for notarization) > **Generate**.

- **Download API Key** gives `AuthKey_XXXXXXXXXX.p8`: you can download it only once, keep it safe.
- **Key ID**: the column next to the key (10 characters).
- **Issuer ID**: shown above the key list (a UUID like `69a6de7e-...`).

### 5. Turn the files into text (on the Mac, Terminal)

Each command copies one value to the clipboard; paste it into GitHub right after (step 6) before running the next one.

```sh
base64 -i DevIDApp.p12 | pbcopy              # -> APPLE_DEV_ID_APP_P12
base64 -i DevIDInstaller.p12 | pbcopy        # -> APPLE_DEV_ID_INSTALLER_P12
base64 -i AuthKey_XXXXXXXXXX.p8 | pbcopy     # -> APPLE_NOTARY_KEY_P8
```

### 6. Add the repo secrets

GitHub > the repo that runs the `ghosty-pro` workflow > **Settings** > **Secrets and variables** > **Actions** >
**New repository secret**, one per row:

| Secret | Value |
| --- | --- |
| `APPLE_DEV_ID_APP_P12` | base64 of `DevIDApp.p12` (step 5) |
| `APPLE_DEV_ID_INSTALLER_P12` | base64 of `DevIDInstaller.p12` (step 5) |
| `APPLE_CERT_PASSWORD` | the .p12 password (step 2) |
| `APPLE_TEAM_ID` | Team ID (step 3) |
| `APPLE_NOTARY_KEY_P8` | base64 of the `.p8` (step 5) |
| `APPLE_NOTARY_KEY_ID` | Key ID (step 4) |
| `APPLE_NOTARY_ISSUER_ID` | Issuer ID (step 4) |

All seven are needed. If only some are there, the build ships unsigned and `build-mac.log` names the missing ones.
Afterwards, keep the .p12 / .p8 files and the password in a password manager and delete the loose copies.

The next Mac build signs and notarizes by itself (notarization adds about 5-20 minutes).

## Check a signed pkg on your Mac

Download `GhostBlock-Plugins-Mac.pkg` from the Release, then:

- **Double-click it.** The installer opens straight away: no "Apple could not verify..." / "unidentified developer" box,
  no more right-click > Open or System Settings > *Open Anyway*.
- In Terminal:

  ```sh
  spctl --assess --type install -vv ~/Downloads/GhostBlock-Plugins-Mac.pkg
  #   -> accepted   source=Notarized Developer ID
  pkgutil --check-signature ~/Downloads/GhostBlock-Plugins-Mac.pkg
  #   -> Status: signed by a developer certificate issued by Apple for distribution, Developer ID Installer: ... (TEAMID)
  xcrun stapler validate ~/Downloads/GhostBlock-Plugins-Mac.pkg
  #   -> The validate action worked!
  ```

- Or everything at once, including every plug-in and app inside (read-only, installs nothing):

  ```sh
  ghosty-pro/packaging/sign-check.sh ~/Downloads/GhostBlock-Plugins-Mac.pkg
  ```

  It ends with `ALL GOOD: signed, notarized, stapled, Gatekeeper-approved.` or lists each problem as a `FAIL` line.

## When `SIGNING FAILED` shows up

| Message | Usual cause |
| --- | --- |
| `missing secrets: ...` | that secret is not set (or has a typo in its name) |
| `... is not valid base64` | the value was not pasted whole; redo step 5 |
| `importing the ... .p12 failed` | wrong `APPLE_CERT_PASSWORD`, the two .p12 have different passwords, or the export had no private key |
| `no 'Developer ID Application: ... (TEAMID)' identity` | wrong certificate type (e.g. *Apple Development*) or `APPLE_TEAM_ID` does not match |
| `signed build failed` | a codesign / productsign error; see `sign-build-tail.log` and the lines above it |
| `notarization not accepted (status Invalid ...)` | Apple's log is printed right above it in `build-mac.log` (each problem file + reason) |
| `notarization not accepted (status In Progress ...)` | Apple took longer than 60 min; the next build will usually pass |
| `signing step did not finish` | the step hit its 80 min limit or crashed |

## Entitlements (why none)

None are added, on purpose. Plug-ins run inside the host app, so the host's entitlements apply and a plug-in's own are
ignored; hardened runtime without entitlements is what notarization needs. The standalone apps do not JIT (no
`allow-unsigned-executable-memory`) and only load code from inside their own signed bundle (no
`disable-library-validation`). They also do not get `com.apple.security.device.audio-input`: they have no
`NSMicrophoneUsageDescription` today, so the entitlement alone would change nothing. If a standalone ever needs the
microphone, add both together (CMake `MICROPHONE_PERMISSION_ENABLED TRUE` and an entitlements file for the apps only).

## Testing the scripts without a Mac

```sh
bash ghosty-pro/packaging/tests/sign-dryrun.sh
```

It runs on Linux with stub `security` / `codesign` / `productsign` / `xcrun` / ... commands and checks that the unsigned path
makes exactly the same calls as before, that the workflow's old steps are unchanged and the new ones are guarded, that the
signed path runs in the right order (keychain, nested code, bundles, productbuild, productsign, notarytool, stapler, spctl),
and that every failure leaves the unsigned pkg and logs `SIGNING FAILED` / `NOTARIZED: no`. It cannot prove that Apple
accepts the real thing: the first real signed build is the true test (check `build-mac.log` and run `sign-check.sh`).

## Files

- `build-mac-pkg.sh`: builds the pkg; ad-hoc when `MAC_APP_IDENTITY` is unset, Developer ID when set.
- `mac-sign.sh`: keychain, identities, signed build, notarization, stapling, checks, fail-soft (`run` / `cleanup`).
- `sign-check.sh`: checks a pkg on a Mac.
- `tests/sign-dryrun.sh`: the Linux dry run above.
