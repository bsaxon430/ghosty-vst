#!/usr/bin/env python3
"""No open-source project names in the visible UI (Brandon 2026-11-06 08:56).
Scans every string literal in the code that draws the UI (Vital's editor in src/interface, our ghosty/src and ghosty/shared)
for "Vital", "Vial", "Tytel". Literals that never reach the screen (file extensions, folder names on disk, thread names,
settings keys, skin resource ids, file metadata) are listed below with the reason; anything new fails the build.
The About / Credits text (GhostyEditor::ghostyAboutText) is the one allowed place (licence law)."""
import re, sys, pathlib

ROOT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ".")
DIRS = ["src/interface", "src/common", "src/plugin", "ghosty/src", "ghosty/shared"]
WORD = re.compile(r"vital|vial|tytel", re.I)
LIT = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
NOT_SHOWN = {
    "vital", "vitalbank", "vitalskin", "vitaltable", "vitallfo", "vitalsample",   # file extensions
    "*.vital", "*.vitalbank", "*.vital;*.vitalbank", ".vital", "Vital.config", "Vial.config",
    "vital/Vital.config", "vial/Vial.config", ".vital/Vital.config", "Music/Vital", "Music/Vial",
    "Library/Audio/Presets/Vital", "Vital", "Vial", "vial", "~/.local/share/vital/",
    ".local/share/vital", ".local/share/vial",                                   # folders on disk (where presets live)
    "vitalFolders",                                                              # a settings key
    "default_vitalskin", "ghosty_black_vitalskin", "ghosty_sketch_vitalskin",   # resource ids
    "Vial Download Thread", "Vial Install Thread", "Vial Auth Init Thread",      # thread names
    "vital_versions.txt", "<!>2048 20000000 wavetable (vital.audio)",          # a temp file, wav metadata
    "https://github.com/mtytel/vital",                                           # in About / Credits
}
ABOUT = "ghostyAboutText"
bad = []
for d in DIRS:
    for f in sorted((ROOT / d).rglob("*")):
        if f.suffix not in (".cpp", ".h", ".mm"):
            continue
        in_about = False
        for n, line in enumerate(f.read_text(errors="replace").splitlines(), 1):
            s = line.strip()
            if ABOUT in s and "String GhostyEditor::" in s:
                in_about = True
            if in_about:
                if s.startswith("}"):
                    in_about = False
                continue
            if s.startswith(("#include", "//", "/*", "*")):
                continue
            code = line.split("//")[0] if '"' not in line.split("//")[0][-1:] else line
            for m in LIT.finditer(code):
                t = m.group(1)
                if WORD.search(t) and t not in NOT_SHOWN and not re.fullmatch(r"[\w./*;~-]*\.(vital\w*)", t):
                    bad.append(f"{f.relative_to(ROOT)}:{n}: \"{t}\"")
for b in bad:
    print("UPSTREAM NAME IN UI TEXT:", b)
print(f"names check: {len(bad)} upstream names in UI text")
sys.exit(1 if bad else 0)
