// Ghost Box factory presets (included by FxPresets.cpp inside its anonymous namespace, GPFX_BOX builds only).
// Each preset lists the module switches it needs; anything not listed goes back to its default.
std::vector<UnitPreset> makeWarp()
{   // the bend lane's factory shapes: each sets the curve plus its length and range (mode back to PITCH)
    std::vector<UnitPreset> v;
    const auto& f = bendlane::factory();
    for (int i = 0; i < (int) f.size(); ++i)
        v.push_back ({ f[(size_t) i].name, V { { "length", (float) f[(size_t) i].lenIndex }, { "range", (float) f[(size_t) i].rangeIndex } },
                       [i] (Rack::Impl& r) { r.warpShape.set (bendlane::factory()[(size_t) i].shape); } });
    return v;
}
std::vector<UnitPreset> makeRetro()
{   // ntype 0 VINYL 1 DUST 2 HISS 3 WHITE 4 PINK 5 HUM50 6 HUM60 7 RADIO; dtype 0 TAPE 1 TUBE 2 CONSOLE 3 TRANSISTOR 4 FUZZ
    return { { "Dusty Vinyl",         V { { "noise", 0.45f }, { "ntype", 0 }, { "wobble", 0.25f }, { "dist", 0.25f }, { "tone", -0.2f } } },
             { "Old Cassette",        V { { "noise", 0.35f }, { "ntype", 2 }, { "wobble", 0.45f }, { "flutter", 0.5f }, { "dist", 0.3f }, { "mag_on", 1 }, { "mag", 0.45f }, { "tone", -0.1f } } },
             { "Broken VHS",          V { { "noise", 0.3f }, { "ntype", 2 }, { "wobble", 0.7f }, { "flutter", 0.3f }, { "mag_on", 1 }, { "mag", 0.7f }, { "digi_on", 1 }, { "digi", 0.35f }, { "dist", 0.2f }, { "tone", -0.3f } } },
             { "Lo-Fi Beat Tape",     V { { "noise", 0.3f }, { "ntype", 1 }, { "wobble", 0.3f }, { "dist", 0.4f }, { "dtype", 1 }, { "digi_on", 1 }, { "digi", 0.45f }, { "space_on", 1 }, { "space", 0.25f } } },
             { "Radio 1938",          V { { "noise", 0.5f }, { "ntype", 7 }, { "dist", 0.5f }, { "dtype", 3 }, { "wobble", 0.15f }, { "mag_on", 1 }, { "mag", 0.3f }, { "tone", 0.25f } } },
             { "Warm Tube Glow",      V { { "noise_on", 0 }, { "wobble", 0.15f }, { "dist", 0.5f }, { "dtype", 1 }, { "tone", -0.1f }, { "space_on", 1 }, { "space", 0.2f } } },
             { "Bitcrushed Memories", V { { "noise", 0.2f }, { "ntype", 3 }, { "ntone", -0.5f }, { "wobble", 0.2f }, { "dist", 0.2f }, { "digi_on", 1 }, { "digi", 0.7f } } },
             { "Haunted Hum",         V { { "noise", 0.4f }, { "ntype", 6 }, { "wobble", 0.3f }, { "dist_on", 0 }, { "mag_on", 1 }, { "mag", 0.5f }, { "space_on", 1 }, { "space", 0.4f } } },
             { "Grandpa's Turntable", V { { "noise", 0.55f }, { "ntype", 0 }, { "wobble", 0.35f }, { "flutter", 0.15f }, { "dist", 0.2f }, { "space_on", 1 }, { "space", 0.2f }, { "tone", -0.45f } } },
             { "Ghost Signal",        V { { "noise", 0.4f }, { "ntype", 7 }, { "nfollow", 1 }, { "wobble", 0.5f }, { "digi_on", 1 }, { "digi", 0.3f }, { "mag_on", 1 }, { "mag", 0.4f }, { "magic", 0.65f } } } };
}
#include "FxFattenPresets.inc"   // makeFatten() (shared with Ghosty Pro)
std::vector<UnitPreset> makeCrackle()
{   // bands 0 = 1 band, 1 = 2 bands, 2 = 3 bands; curves: see crackleCurveNames(); boxes: see crackleBoxNames()
    return { { "Cracked Sidewalk", V { { "bands", 2 }, { "b1_curve", ccTube }, { "b1_drive", 0.3f }, { "b2_curve", ccCracked }, { "b2_drive", 0.6f }, { "b3_curve", ccBitcrush }, { "b3_drive", 0.5f }, { "b3_mix", 0.6f } } },
             { "Busted Speaker",   V { { "b1_curve", ccBroken }, { "b1_drive", 0.6f }, { "box", cbSmallSpeaker } } },
             { "Garage Fuzz Box",  V { { "bands", 1 }, { "xlo", 300 }, { "b1_curve", ccTube }, { "b1_drive", 0.3f }, { "b2_curve", ccFuzz }, { "b2_drive", 0.7f }, { "box", cbCab }, { "boxmix", 0.8f } } },
             { "Tin Can Phone",    V { { "b1_curve", ccDiode }, { "b1_drive", 0.5f }, { "box", cbTinCan }, { "prehp", 300 } } },
             { "Rectified Rats",   V { { "bands", 1 }, { "b1_curve", ccHard }, { "b1_drive", 0.2f }, { "b2_curve", ccRectify }, { "b2_drive", 0.6f }, { "b2_mix", 0.7f } } },
             { "Dying Battery",    V { { "b1_curve", ccBattery }, { "b1_drive", 0.6f }, { "box", cbWalkie }, { "boxmix", 0.6f } } },
             { "Folded Metal",     V { { "bands", 2 }, { "b1_curve", ccTube }, { "b1_drive", 0.2f }, { "b2_curve", ccFold }, { "b2_drive", 0.6f }, { "b3_curve", ccSineFold }, { "b3_drive", 0.5f } } },
             { "8-Bit Gutter",     V { { "bands", 1 }, { "b1_curve", ccBitcrush }, { "b1_drive", 0.7f }, { "b2_curve", ccDownsample }, { "b2_drive", 0.5f } } },
             { "AM Static Ghost",  V { { "b1_curve", ccDiode }, { "b1_drive", 0.4f }, { "box", cbRadio }, { "posthp", 200 }, { "postlp", 5000 } } },
             { "Splatter Drums",   V { { "bands", 2 }, { "b1_curve", ccHard }, { "b1_drive", 0.4f }, { "b2_curve", ccCracked }, { "b2_drive", 0.7f }, { "b3_curve", ccHalfRect }, { "b3_drive", 0.5f }, { "b3_mix", 0.7f } } } };
}
std::vector<UnitPreset> makeHalls()
{
    return { { "Tight Room",       V { { "mode", hmRoom }, { "size", 0.25f }, { "decay", 0.5f }, { "predelay", 5 }, { "early", 0.7f }, { "mix", 0.25f } } },
             { "Drum Room",        V { { "mode", hmRoom }, { "size", 0.4f }, { "decay", 0.9f }, { "diffusion", 0.8f }, { "early", 0.8f }, { "lowcut", 120 }, { "mix", 0.3f } } },
             { "Concert Hall",     V { { "mode", hmHall }, { "size", 0.7f }, { "decay", 2.8f }, { "predelay", 25 }, { "mix", 0.3f } } },
             { "Big Hall",         V { { "mode", hmHall }, { "size", 0.9f }, { "decay", 5.0f }, { "predelay", 40 }, { "mod", 0.5f }, { "mix", 0.3f } } },
             { "Bright Plate",     V { { "mode", hmPlate }, { "size", 0.5f }, { "decay", 1.8f }, { "damp", 0.15f }, { "mix", 0.25f } } },
             { "Vocal Plate",      V { { "mode", hmPlate }, { "size", 0.45f }, { "decay", 1.4f }, { "predelay", 30 }, { "lowcut", 200 }, { "damp", 0.3f }, { "mix", 0.25f } } },
             { "Stone Chamber",    V { { "mode", hmChamber }, { "size", 0.55f }, { "decay", 2.2f }, { "damp", 0.5f }, { "mix", 0.3f } } },
             { "Haunted Basement", V { { "mode", hmChamber }, { "size", 0.35f }, { "decay", 1.5f }, { "damp", 0.75f }, { "lowcut", 40 }, { "mix", 0.35f } } },
             { "Cathedral",        V { { "mode", hmCathedral }, { "size", 0.9f }, { "decay", 8.0f }, { "predelay", 60 }, { "damp", 0.45f }, { "mix", 0.35f } } },
             { "Cloud Nine",       V { { "mode", hmCloud }, { "size", 0.85f }, { "decay", 9.0f }, { "mod", 0.7f }, { "diffusion", 1.0f }, { "mix", 0.4f } } },
             { "Galaxy Shimmer",   V { { "mode", hmGalaxy }, { "size", 1.0f }, { "decay", 20.0f }, { "shimmer", 0.45f }, { "mod", 0.8f }, { "mix", 0.35f } } },
             { "Endless Galaxy",   V { { "mode", hmGalaxy }, { "size", 1.0f }, { "decay", 60.0f }, { "mod", 1.0f }, { "mix", 0.35f } } },
             { "Reverse Spirit",   V { { "mode", hmReverse }, { "size", 0.6f }, { "decay", 3.0f }, { "mix", 0.4f } } },
             { "80s Gated Snare",  V { { "mode", hmGated }, { "size", 0.35f }, { "decay", 2.5f }, { "early", 0.6f }, { "mix", 0.35f } } },
             { "Nonlinear Smash",  V { { "mode", hmNonlinear }, { "size", 0.5f }, { "decay", 1.0f }, { "mix", 0.35f } } },
             { "Nonlinear Swell",  V { { "mode", hmNonlinear }, { "size", 0.7f }, { "decay", 5.0f }, { "mix", 0.35f } } },
             { "Dark Tape Verb",   V { { "mode", hmDarkTape }, { "size", 0.6f }, { "decay", 3.5f }, { "damp", 0.5f }, { "mix", 0.35f } } },
             { "Frozen Ghost",     V { { "mode", hmHall }, { "size", 0.9f }, { "decay", 30.0f }, { "freeze", 1 }, { "mix", 0.5f } } } };
}
std::vector<UnitPreset> makeSplit()
{   // a_ = ATTACK path, b_ = BODY path
    return { { "Tail-Only Reverb",   V { { "sens", 0.6f }, { "xover", 45.0f }, { "b_verb", 0.75f }, { "vsize", 0.8f }, { "vdecay", 4.5f } } },
             { "Attack-Only Reverb", V { { "sens", 0.7f }, { "xover", 30.0f }, { "a_verb", 0.9f }, { "vsize", 0.55f }, { "vdecay", 2.2f } } },
             { "Clean Hit, Huge Tail", V { { "sens", 0.65f }, { "xover", 40.0f }, { "attack", 0.25f }, { "b_verb", 1.0f }, { "b_tilt", -0.3f }, { "vsize", 0.95f }, { "vdecay", 7.0f } } },
             { "Punch Up",           V { { "sens", 0.6f }, { "xover", 35.0f }, { "attack", 0.45f }, { "sustain", -0.15f } } },
             { "Soften Hits",        V { { "sens", 0.6f }, { "xover", 25.0f }, { "attack", -0.5f }, { "a_tilt", -0.4f } } },
             { "Crunchy Attack",     V { { "sens", 0.55f }, { "xover", 30.0f }, { "a_drive", 0.7f }, { "a_tilt", 0.3f } } },
             { "Wide Body",          V { { "sens", 0.5f }, { "xover", 60.0f }, { "b_width", 1.8f }, { "a_width", 0.4f } } },
             { "Dub Body Echo",      V { { "sens", 0.55f }, { "xover", 50.0f }, { "b_echo", 0.6f }, { "efb", 0.55f }, { "ediv", 7 } } },
             { "Snappy Dry Tail",    V { { "sens", 0.7f }, { "xover", 20.0f }, { "attack", 0.3f }, { "sustain", -0.45f } } },
             { "Dark Body, Bright Hit", V { { "sens", 0.55f }, { "xover", 40.0f }, { "a_tilt", 0.6f }, { "b_tilt", -0.7f } } } };
}
std::vector<UnitPreset> makeSpectra()
{
    return { { "Frozen Pad",     V { { "freeze", 1 }, { "smear", 0.5f }, { "ghost", 0.0f } } },
             { "Ghost Smear",    V { { "smear", 0.85f }, { "ghost", 0.9f } } },
             { "Scatter Dust",   V { { "scatter", 0.6f }, { "smear", 0.3f }, { "ghost", 0.6f } } },
             { "Octave Ghost",   V { { "shift", 12 }, { "smear", 0.2f }, { "ghost", 0.85f } } },
             { "Sub Shadow",     V { { "shift", -12 }, { "smear", 0.4f }, { "tilt", -0.4f } } },
             { "Fifth Shimmer",  V { { "shift", 7 }, { "smear", 0.7f }, { "tilt", 0.4f } } },
             { "Tempo Wobble",   V { { "motion", 0.7f }, { "mdiv", 12 }, { "smear", 0.4f } } },
             { "Glitch Spray",   V { { "scatter", 0.9f }, { "motion", 0.5f }, { "mdiv", 6 }, { "ghost", 0.4f } } },
             { "Dark Freeze",    V { { "freeze", 1 }, { "tilt", -0.6f }, { "smear", 0.6f } } } };
}
std::vector<UnitPreset> makeBassLab()
{
    return { { "Phone Speaker Fix", V { { "translate", 0.7f }, { "lowwidth", 0.0f } } },
             { "Sub Boost",         V { { "sub", 0.5f }, { "lowwidth", 0.0f } } },
             { "Growl Bass",        V { { "growl", 0.7f }, { "mid", 2.0f }, { "lowwidth", 0.0f } } },
             { "Edge Bite",         V { { "edge", 0.6f }, { "high", 2.0f } } },
             { "Tight Mono Low",    V { { "lowwidth", 0.0f }, { "xlo", 150.0f } } },
             { "Grit Stack",        V { { "growl", 0.5f }, { "edge", 0.3f }, { "translate", 0.3f }, { "lowwidth", 0.0f } } },
             { "Big Sub Weight",    V { { "sub", 0.35f }, { "translate", 0.45f }, { "xlo", 100.0f }, { "lowwidth", 0.0f } } },
             { "Clean Wide Top",    V { { "lowwidth", 0.0f }, { "high", 1.5f }, { "edge", 0.15f } } } };
}
std::vector<UnitPreset> makePortal()
{
    return { { "Metal Box",          V { { "shape", 0 }, { "size", 0.4f }, { "decay", 2.5f }, { "move", 0 } } },
             { "Living Room",        V { { "shape", 1 }, { "size", 0.35f }, { "decay", 1.2f }, { "density", 0.8f } } },
             { "Hollow Tube",        V { { "shape", 2 }, { "size", 0.5f }, { "decay", 2.0f } } },
             { "Diffuse Cloud",      V { { "shape", 3 }, { "size", 0.85f }, { "decay", 7.0f }, { "density", 1.0f } } },
             { "Crystal Cave",       V { { "shape", 4 }, { "size", 0.8f }, { "decay", 6.0f }, { "tone", 0.3f } } },
             { "Rotating Temple",    V { { "shape", 3 }, { "move", 2 }, { "size", 0.9f }, { "decay", 9.0f } } },
             { "Expanding Universe", V { { "shape", 1 }, { "move", 3 }, { "transform", 0.6f }, { "size", 0.8f }, { "decay", 10.0f } } },
             { "Frozen Doorway",     V { { "shape", 3 }, { "freeze", 1 }, { "size", 0.9f }, { "decay", 20.0f } } } };
}
std::vector<UnitPreset> makeFormant()
{
    return { { "Talking Bass",   V { { "move", 1 }, { "mdiv", 6 }, { "mdepth", 0.8f }, { "reso", 0.6f }, { "drive", 0.4f }, { "body", 0.7f } } },
             { "Vowel Wobble",   V { { "move", 1 }, { "mdiv", 9 }, { "mdepth", 1.0f }, { "reso", 0.5f } } },
             { "Robot Mouth",    V { { "move", 2 }, { "mlen", 1 }, { "mdepth", 1.0f }, { "reso", 0.7f }, { "drive", 0.3f } } },
             { "Open Wide A",    V { { "vowel", 0.0f }, { "mouth", 0.7f }, { "reso", 0.45f } } },
             { "Ooh Pad",        V { { "vowel", 4.0f }, { "mouth", 0.4f }, { "reso", 0.35f }, { "body", 0.4f } } },
             { "Formant Up",     V { { "vowel", 1.0f }, { "shift", 7.0f }, { "reso", 0.5f } } },
             { "Growl Talk",     V { { "move", 1 }, { "mdiv", 3 }, { "mdepth", 0.7f }, { "drive", 0.75f }, { "reso", 0.65f }, { "body", 0.6f } } },
             { "Slow Speech",    V { { "move", 1 }, { "mdiv", 14 }, { "mdepth", 1.0f }, { "reso", 0.55f } } } };
}
