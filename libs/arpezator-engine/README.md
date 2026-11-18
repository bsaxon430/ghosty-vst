# ArpEZator engine (GhostBlock's own code)

The JUCE-free composing engine of Professor Bernstein's ArpEZator 3.2.2, copied unchanged from this repository's
`arpezator-mutant` branch at commit ef6b8e3 (`ghosty-pro/ArpEZator/`): chords (ArpCore), the arp / melody / bass
composers (Composer, Recipe), the idea pipeline (Idea) and what it links against (HookLab, UrbanMoods, Mutant,
CyberpopTable + CyberpopData.inc). `RecipeJson.h` (needs juce_core) reads `recipe.json`, which Seance embeds as
`resources/ghostblock/seance/arpez/recipe.json`.

Seance uses it on the main thread only, to write MIDNIGHT and DUSK ideas into its STEPS page
(`src/ghostblock/seance/seance-editor.cpp`, `StepsPage`). Do not edit these files here: change ArpEZator, then copy.
