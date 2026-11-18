The CURVES engine's sets: `bdd.py` reads the presets (JUCE ValueTree binary), `export.py <presets dir> <out>` writes each preset's
curves (zlib-compressed float tables decoded, ordered by their "[+N]" steps; equation presets rendered at 9 morph points),
`pack.py <out> ../../assets/curves` writes the files the plugin embeds.
