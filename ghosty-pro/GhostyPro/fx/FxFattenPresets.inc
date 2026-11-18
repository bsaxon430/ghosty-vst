// FATTEN factory presets (included by FxPresets.cpp inside its anonymous namespace, both racks: directly in Ghosty Pro,
// through FxBoxPresets.inc in Ghost Box). Each preset lists the module switches it needs; anything not listed goes back to its default.
std::vector<UnitPreset> makeFatten()
{
    return { { "Gentle Thick",   V { { "fat", 0.25f }, { "crunch", 0.05f } } },
             { "Fat Bass",       V { { "fat", 0.6f }, { "color", -0.3f }, { "crunch", 0.15f } } },
             { "Fat Drums",      V { { "fat", 0.7f }, { "crunch", 0.4f } } },
             { "Big Vocal",      V { { "fat", 0.4f }, { "color", 0.25f }, { "crunch", 0.1f }, { "mix", 0.7f } } },
             { "Crunchy Synth",  V { { "fat", 0.55f }, { "crunch", 0.7f }, { "color", 0.2f } } },
             { "Maximum Chonk",  V { { "fat", 1.0f }, { "crunch", 0.5f } } },
             { "Parallel Puff",  V { { "fat", 0.9f }, { "crunch", 0.3f }, { "mix", 0.4f } } },
             { "Dark Pillow",    V { { "fat", 0.6f }, { "color", -0.7f }, { "crunch", 0.0f } } },
             // the modes (2026-10-23; appended so older preset indices stay put)
             { "Sausage",        V { { "mode", fmSausage }, { "fat", 0.85f }, { "crunch", 0.35f }, { "drive", 3.0f } } },
             { "Sub Bomb",       V { { "mode", fmSub }, { "fat", 0.8f }, { "crunch", 0.1f }, { "punch", 1 }, { "lowclean", 1 }, { "color", -0.3f } } },
             { "Thick Body",     V { { "mode", fmBody }, { "fat", 0.55f }, { "crunch", 0.15f }, { "punch", 1 } } },
             { "Crunch Drive",   V { { "mode", fmSaturate }, { "fat", 0.75f }, { "crunch", 0.7f }, { "drive", 6.0f }, { "color", 0.2f } } },
             { "Smash Glue",     V { { "mode", fmSmash }, { "fat", 0.4f } } },
             { "Wide Stack",     V { { "mode", fmStack }, { "fat", 0.6f }, { "crunch", 0.1f } } },
             { "Round Tail",     V { { "mode", fmTransient }, { "fat", 0.6f }, { "lowclean", 1 } } } };
}
