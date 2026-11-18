// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX factory presets (0.1.10): a first bank that uses every one of the 24 units at least once. Each line is
//   name | tags | unit.control=value; ...        (values in the control's own units; a menu by its position, from 0)
// Loading one starts from INIT (everything off but the limiter, factory patterns, no scenes) and sets what the line names, so a
// preset never depends on what was loaded before. Names follow the house rules: plain words, no artist or product names.
#pragma once
namespace phantom::factory
{
struct Line { const char* name; const char* tags; const char* set; };
inline const Line kBank[] {
    // ---- master
    { "Clean Glue",        "master",           "ott.on=1; ott.time=0.5; ott.up=0.3; ott.down=0.4; ott.mix=0.6; lim.gain=2" },
    { "Loud And Proud",    "master",           "fatten.on=1; fatten.fat=0.45; ott.on=1; ott.up=0.4; ott.down=0.6; ott.mix=0.7; lim.gain=4" },
    // ---- colour
    { "Tape Warmth",       "colour,tape",      "retro.on=1; retro.noise_on=0; retro.wobble=0.15; retro.flutter=0.1; retro.dist=0.25; retro.mag_on=1; retro.mag=0.3" },
    { "Dusty Vinyl",       "colour,lo-fi",     "retro.on=1; retro.ntype=0; retro.noise=0.45; retro.wobble=0.4; retro.dist=0.3; retro.degrade=0.4; retro.tone=-0.2" },
    { "Radio Voice",       "colour,lo-fi",     "crackle.on=1; crackle.box=4; crackle.prehp=300; crackle.prelp=4000; retro.on=1; retro.ntype=7; retro.noise=0.25; retro.wobble_on=0" },
    { "Tube Glow",         "colour",           "tube.on=1; tube.drive=0.5; tube.air=0.3; tube.bass=1.5" },
    { "Wrecked Drums",     "colour,drums",     "wreck.on=1; wreck.style=5; wreck.drive=0.55; wreck.thump=1; crush.on=1; crush.crush=0.6" },
    { "Parallel Smash",    "drums",            "crush.on=1; crush.crush=0.8; crush.blend=0.5; fatten.on=1; fatten.mode=3; fatten.fat=0.5" },
    { "Sub Weight",        "bass",             "basslab.on=1; basslab.sub=0.5; basslab.lowsat=0.3; basslab.lowwidth=0; fatten.on=1; fatten.mode=1; fatten.fat=0.4" },
    { "Growl Bass",        "bass",             "basslab.on=1; basslab.growl=0.6; basslab.gmove=0.4; basslab.edge=0.3; wreck.on=1; wreck.style=1; wreck.drive=0.35" },
    { "Talking Filter",    "colour,movement",  "formant.on=1; formant.vowel=2; formant.move=1; formant.mdepth=0.7; formant.reso=0.6" },
    { "Vowel Pad",         "colour,ambient",   "formant.on=1; formant.vowel=1; formant.mouth=0.6; space.on=1; space.blend=0.5; space.decay=4" },
    { "Bright Air",        "eq",               "eq.on=1; eq.1_on=1; eq.1_type=2; eq.1_freq=9000; eq.1_gain=4; eq.2_on=1; eq.2_type=3; eq.2_freq=35" },
    { "Mid Scoop Linear",  "eq",               "eq.on=1; eq.mode=2; eq.1_on=1; eq.1_type=0; eq.1_freq=500; eq.1_gain=-4; eq.1_q=0.8; eq.2_on=1; eq.2_type=2; eq.2_freq=10000; eq.2_gain=3" },
    { "Wah Sweep",         "filter,movement",  "filter.on=1; filter.type=1; filter.cutoff=900; filter.reso=0.6; filter.lfoamt=0.6; filter.lforate=0.5" },
    { "Dark Ladder",       "filter",           "filter.on=1; filter.type=4; filter.cutoff=1100; filter.reso=0.5; filter.drive=0.4; filter.envamt=0.5" },
    // ---- space
    { "Wide Chorus",       "space,width",      "chorus.on=1; chorus.depth=0.6; chorus.voices=4" },
    { "Ensemble",          "space,width",      "chorus.on=1; chorus.mode=1; chorus.rate=0.6; chorus.depth=0.7; halls.on=1; halls.mix=0.25" },
    { "Crystal Rain",      "space,pitch",      "crystal.on=1; crystal.pitch=12; crystal.fb=0.5; crystal.reverse=0.3; crystal.spread=0.8; crystal.mix=0.4" },
    { "Backwards Crystals","space,pitch",      "crystal.on=1; crystal.pitch=7; crystal.reverse=1; crystal.delay=400; crystal.mix=0.35" },
    { "Tape Slap",         "delay",            "echo.on=1; echo.mode=0; echo.sync=0; echo.time=110; echo.fb=0.2; echo.ping=0; echo.mix=0.3" },
    { "Ping Pong Dub",     "delay",            "echo.on=1; echo.mode=1; echo.fb=0.6; echo.lowcut=250; echo.highcut=3500; echo.duck=0.4; echo.mix=0.35" },
    { "Small Room",        "reverb",           "halls.on=1; halls.mode=0; halls.size=0.3; halls.decay=0.8; halls.mix=0.2" },
    { "Big Hall",          "reverb",           "halls.on=1; halls.mode=1; halls.size=0.8; halls.decay=4.5; halls.predelay=30; halls.mix=0.3" },
    { "Cloud Wash",        "reverb,ambient",   "space.on=1; space.size=0.8; space.blend=0.7; space.decay=7; space.glimmer=0.4; space.mix=0.45" },
    { "Shimmer Sky",       "reverb,ambient",   "shimmer.on=1; shimmer.decay=9; shimmer.shimmer=0.6; shimmer.mix=0.4" },
    { "Frozen Spectrum",   "ambient,strange",  "spectra.on=1; spectra.smear=0.7; spectra.ghost=0.6; spectra.scatter=0.3; spectra.mix=0.5" },
    { "Metal Doorway",     "reverb,strange",   "portal.on=1; portal.shape=0; portal.decay=3; portal.transform=0.3; portal.mix=0.35" },
    // ---- rhythm
    { "Pump It",           "rhythm",           "gate.on=1; gate.pmode=1; gate.pdepth=0.8" },
    { "Half Time Pump",    "rhythm",           "gate.on=1; gate.pmode=2; gate.pdepth=0.7" },
    { "Glitch Grid",       "rhythm,strange",   "grid.on=1; grid.pattern=3; grid.stutter=0.6; grid.crush=0.5" },
    { "Haunted Pads",      "rhythm,performance","haunt.on=1; haunt.motion=0.6" },
    { "Tape Bend",         "rhythm,pitch",     "warp.on=1; warp.mode=1; warp.amount=0.6; warp.range=1" },
    // ---- chains
    { "Lo-Fi Dream",       "lo-fi,ambient",    "retro.on=1; retro.noise=0.3; retro.wobble=0.35; retro.degrade=0.3; filter.on=1; filter.type=0; filter.cutoff=6000; filter.reso=0.2; chorus.on=1; chorus.depth=0.4; space.on=1; space.mix=0.25" },
};
inline constexpr int kCount = (int) (sizeof (kBank) / sizeof (kBank[0]));
} // namespace phantom::factory
