// FX RACK (round 3) factory presets: DISTORTION, COMPRESSOR, SPLITTER (Ghosty Pro; included by FxPresets.cpp inside its anonymous
// namespace). Anything not listed goes back to its default. Mode numbers: see distModeNames / compModeNames / bandsModeNames.
std::vector<UnitPreset> makeDist()
{
    return { { "Warm Tube",        V { { "mode", (float) dmTube }, { "drive", 9.0f } } },
             { "Overdrive",        V { { "mode", (float) dmSoft }, { "drive", 18.0f }, { "fpos", 2.0f }, { "freq", 7000.0f } } },
             { "Hard Edge",        V { { "mode", (float) dmHard }, { "drive", 15.0f }, { "fpos", 1.0f }, { "ftype", 1.0f }, { "freq", 180.0f } } },
             { "Diode Fuzz",       V { { "mode", (float) dmDiode }, { "drive", 26.0f }, { "fpos", 2.0f }, { "freq", 5000.0f } } },
             { "Metal Fold",       V { { "mode", (float) dmLinFold }, { "drive", 20.0f }, { "mix", 0.6f } } },
             { "Vocal Fold",       V { { "mode", (float) dmSinFold }, { "drive", 16.0f }, { "fpos", 1.0f }, { "ftype", 0.5f }, { "freq", 900.0f }, { "q", 2.0f }, { "fkey", 1.0f } } },
             { "Broken Speaker",   V { { "mode", (float) dmZeroSq }, { "drive", 8.0f }, { "fpos", 2.0f }, { "ftype", 0.5f }, { "freq", 1500.0f } } },
             { "Bit Grit",         V { { "mode", (float) dmDownsample }, { "drive", 20.0f }, { "mix", 0.5f } } },
             { "Thick Asym",       V { { "mode", (float) dmAsym }, { "drive", 14.0f } } },
             { "Band Drive",       V { { "mode", (float) dmSoft }, { "drive", 22.0f }, { "fpos", 1.0f }, { "ftype", 0.5f }, { "freq", 800.0f }, { "q", 1.2f } } },
             { "Drawn Morph",      V { { "mode", (float) dmXShaper }, { "drive", 18.0f } } } };
}
std::vector<UnitPreset> makeComp()
{
    return { { "Gentle Glue",      V { { "thresh", -18.0f }, { "ratio", 2.0f }, { "attack", 20.0f }, { "release", 200.0f }, { "makeup", 2.0f }, { "knee", 6.0f } } },
             { "Punchy",           V { { "thresh", -22.0f }, { "ratio", 4.0f }, { "attack", 25.0f }, { "release", 120.0f }, { "makeup", 4.0f } } },
             { "Squash",           V { { "thresh", -30.0f }, { "ratio", 10.0f }, { "attack", 2.0f }, { "release", 80.0f }, { "makeup", 9.0f } } },
             { "Parallel Smash",   V { { "thresh", -36.0f }, { "ratio", 12.0f }, { "attack", 1.0f }, { "release", 100.0f }, { "makeup", 14.0f }, { "mix", 0.4f } } },
             { "Duck To Playing",  V { { "thresh", -30.0f }, { "ratio", 6.0f }, { "attack", 5.0f }, { "release", 250.0f }, { "key", 1.0f } } },
             { "3-Band Even",      V { { "mode", 1.0f }, { "thresh", -24.0f }, { "ratio", 3.0f }, { "attack", 10.0f }, { "release", 150.0f }, { "makeup", 3.0f } } },
             { "Up + Down",        V { { "mode", 1.0f }, { "thresh", -26.0f }, { "ratio", 4.0f }, { "b1_up", 0.4f }, { "b2_up", 0.5f }, { "b3_up", 0.6f }, { "makeup", 2.0f } } },
             { "Air Lift",         V { { "mode", 1.0f }, { "thresh", -30.0f }, { "ratio", 3.0f }, { "b1_down", 0.0f }, { "b2_down", 0.3f }, { "b3_up", 0.8f }, { "b3_thr", 6.0f } } },
             { "Limit -1 dB",      V { { "ratio", kCompRatioMax }, { "thresh", -1.0f }, { "attack", 2.0f }, { "release", 80.0f } } },
             { "Limit Loud",       V { { "ratio", kCompRatioMax }, { "thresh", -0.5f }, { "attack", 1.0f }, { "release", 50.0f }, { "makeup", 8.0f } } } };
}
std::vector<UnitPreset> makeBands()
{
    return { { "Low / High 200",   V { { "mode", 0.0f }, { "xlo", 200.0f } } },
             { "Low / High 1k",    V { { "mode", 0.0f }, { "xlo", 1000.0f } } },
             { "3 Bands",          V { { "mode", 1.0f }, { "xlo", 200.0f }, { "xhi", 3000.0f } } },
             { "3 Bands Wide Mid", V { { "mode", 1.0f }, { "xlo", 120.0f }, { "xhi", 6000.0f } } },
             { "Mid / Side",       V { { "mode", 2.0f } } } };
}
std::vector<UnitPreset> makeEq3()
{
    return { { "Warmer",           V { { "b1_gain", 3.0f }, { "b1_freq", 140.0f }, { "b3_gain", -2.0f }, { "b3_freq", 8000.0f } } },
             { "Brighter",         V { { "tilt", 4.0f } } },
             { "Darker",           V { { "tilt", -5.0f } } },
             { "Less Mud",         V { { "b1_gain", -4.0f }, { "b1_freq", 280.0f }, { "b1_q", 1.2f } } },
             { "Presence",         V { { "b2_gain", 3.5f }, { "b2_freq", 3200.0f }, { "b2_q", 1.0f } } },
             { "Air",              V { { "b3_gain", 4.0f }, { "b3_freq", 12000.0f }, { "b3_q", 0.6f } } },
             { "Telephone",        V { { "b1_gain", -18.0f }, { "b1_freq", 300.0f }, { "b1_q", 0.4f }, { "b2_gain", 8.0f }, { "b2_freq", 1500.0f }, { "b3_gain", -18.0f }, { "b3_freq", 5000.0f }, { "b3_q", 0.4f } } },
             { "Smile",            V { { "b1_gain", 4.0f }, { "b1_freq", 90.0f }, { "b2_gain", -3.0f }, { "b2_freq", 700.0f }, { "b2_q", 0.7f }, { "b3_gain", 4.0f }, { "b3_freq", 9000.0f } } } };
}
