Third-party licences that must ship with the installers.

EURORACK-MIT-LICENSE.txt  MIT notice of the eurorack STM32 DSP code the Ghosty Pro oscillator ENGINES are built on
                   (see ghosty-pro/THIRD_PARTY_LICENSES.md). Copied into each Ghosty Pro bundle and the licences folder.

R8BRAIN-MIT-LICENSE.txt  MIT notice of r8brain-free-src (+ the Ooura FFT notice), Ghost Slice's sample-rate converter.
                   Copied into each Ghost Slice bundle and the licences folder.

WORLD-LICENSE.txt  WORLD v1.0.1 (M. Morise, modified BSD), used by the Ghosty Spectrum VOICE page.
                   Clause 2 requires the copyright + licence text with every binary distribution.
                   It is also shown inside the plugin (VOICE page, "i" button).

Lines for the owner to add once the VOICE page is in the Ghosty Spectrum build (the packaging scripts were not edited):

  packaging/GhostBlock.iss, [Files]:
    Source: "third_party\WORLD-LICENSE.txt"; DestDir: "{app}\licenses"; Components: spectrum; Flags: ignoreversion

  packaging/build-mac-pkg.sh, before productbuild (the apps payload lands in /Applications/GhostBlock):
    mkdir -p "$root/app/licenses" && cp "$(dirname "$0")/third_party/WORLD-LICENSE.txt" "$root/app/licenses/"
    (and copy it into each Ghosty Spectrum bundle's Contents/Resources if you prefer it next to the plugin)

Release checklist: WORLD licence present in the installer + the VOICE about panel.
