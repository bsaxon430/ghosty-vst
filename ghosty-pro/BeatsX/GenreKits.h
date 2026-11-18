#pragma once
// Ghosty Beats: 20 genre kits built only from the built-in (synthesised) library. Each kit: 16 sounds,
// a starter pattern (A) and a variation (B), a processing style, and swing for the hats.
#include "BeatModel.h"
#include "DrillStyle.h"
#include "LatinStyle.h"
#include "TrapClassicStyle.h"
#include <map>

namespace gb {

// the classic-character kits' mixes (16 per kit; the kit trim below brings each to -14 LUFS)
inline const std::array<float, kTracks>& classicVols(int kit) {
    static const std::array<std::array<float, kTracks>, 9> v { {
        { 1.05f, 0.8f, 0.45f, 0.4f, 0.21f, 0.3f, 0.2f, 0.16f, 0.45f, 0.45f, 0.3f, 0.14f, 0.15f, 0.22f, 0.22f, 0.45f },   // ANALOG BOOM
        { 0.9f, 0.8f, 0.32f, 0.5f, 0.24f, 0.34f, 0.18f, 0.3f, 0.5f, 0.5f, 0.28f, 0.12f, 0.5f, 0.35f, 0.4f, 0.35f },   // ANALOG PUNCH
        { 1.0f, 0.3f, 0.5f, 0.22f, 0.16f, 0.2f, 0.25f, 0.18f, 0.26f, 0.26f, 0.3f, 0.22f, 0.12f, 0.2f, 0.25f, 0.26f },  // DIGITAL EIGHTIES
        { 1.0f, 0.5f, 0.5f, 0.4f, 0.25f, 0.3f, 0.3f, 0.25f, 0.4f, 0.35f, 0.3f, 0.25f, 0.22f, 0.3f, 0.3f, 0.35f },    // CRUNCH TWELVE
        { 1.0f, 0.45f, 0.5f, 0.3f, 0.2f, 0.28f, 0.12f, 0.14f, 0.3f, 0.3f, 0.3f, 0.25f, 0.3f, 0.3f, 0.3f, 0.35f },    // SWING TWELVE
        { 0.66f, 0.62f, 0.4f, 0.4f, 0.14f, 0.22f, 0.25f, 0.22f, 0.66f, 0.66f, 0.18f, 0.66f, 0.25f, 0.3f, 0.3f, 0.3f },  // TRAP GOLD
        { 0.58f, 0.9f, 0.24f, 0.3f, 0.13f, 0.2f, 0.26f, 0.15f, 0.4f, 0.4f, 0.3f, 0.25f, 0.25f, 0.3f, 0.3f, 0.35f },  // TEXAS BOUNCE
        { 1.0f, 0.5f, 0.5f, 0.35f, 0.22f, 0.26f, 0.25f, 0.2f, 0.4f, 0.3f, 0.3f, 0.25f, 0.22f, 0.3f, 0.3f, 0.3f },    // BOOM BAP CLASSIC
        { 0.9f, 0.5f, 0.45f, 0.35f, 0.2f, 0.26f, 0.18f, 0.38f, 0.3f, 0.3f, 0.3f, 0.25f, 0.3f, 0.3f, 0.35f, 0.3f },  // POP SHINE
    } };
    return v[(size_t) juce::jlimit(0, (int) v.size() - 1, kit)];
}

inline const std::vector<KitDef>& genreKits() {
    using A = std::array<const char*, kTracks>;
    const std::array<int, kTracks> HC { 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };   // closed + open hats choke
    static const std::vector<KitDef> k {
        { "Club Progressive", 126,
          A { "Club Kick Punch", "Sub Kick Floor", "Clap Snare Glue", "Club Clap Wide", "Closed Hat Crisp", "Open Hat Sizzle", "Shaker Egg", "Rim Click", "Tom Deep", "Tom Top", "Crash Bright", "Ride Ping", "Riser Noise", "Impact Boom", "Perc Bubble", "Reverse Cymbal Swell" },
          A { "x...x...x...x...", "", "", "....x.......x...", "xo.oxo.oxo.oxo.o", "..x...x...x...x.", "o.oxo.oxo.oxo.ox", "", "", "", "", "", "", "", "", "" },
          A { "x...x...x...x...", "", "............x...", "....x.......x...", "xo.oxo.oxo.oxo.o", "..x...x...x...x.", "o.oxo.oxo.oxo.ox", "...x......x..x..", "", "", "x...............", "", "", "", "..........o.....", "" },
          HC, 1, 0.0f },
        { "Big Room", 128,
          A { "Big Room Kick Arena", "Sub Kick Pressure", "Big Room Snare Huge", "Big Clap Stadium", "Closed Hat Bright", "Open Hat Club", "Shaker Fast", "Snap Bright", "Tom Floor", "Tom Rack", "Crash Wide", "Ride Bell", "Riser Tension", "Impact Slam", "Sweep White Up", "Downlifter Fall" },
          A { "X...x...x...x...", "", "", "....x.......x...", "..x...x...x...x.", "", "gggggggggggggggg", "", "", "", "", "", "", "", "", "" },
          A { "X...x...x...x...", "", "....x.......x.x.", "....x.......x...", "xx.xxx.xxx.xxx.x", "..x...x...x...x.", "gggggggggggggggg", "", "", "", "x...............", "", "", "", "", "" },
          HC, 5, 0.0f },
        { "Dubstep Heavy", 140,
          A { "Dubstep Kick Slam", "Sub Kick Rumble", "Dubstep Snare Crack", "Big Clap Plate", "Closed Hat Tight", "Open Hat Trash", "Rim Snappy", "Metal Anvil", "Tom Electro Low", "Tom Electro High", "Crash Dark", "China Trashy", "Riser Saw", "Impact Thunder", "Zap Blaster", "Reverse Snare" },
          A { "X.........x.....", "", "........X.......", "........x.......", "x.x.x.x.x.x.x.x.", "", "", "", "", "", "x...............", "", "", "", "", "" },
          A { "X.....x...x..x..", "", "........X.....o.", "........x.......", "x.x.x.xxx.x.x4x.", "......x.........", "", "...........x....", "", "", "", "", "", "", "", "" },
          HC, 2, 0.0f },
        { "Riddim", 140,
          A { "Dubstep Kick Knuckle", "808 Kick Bounce", "Dubstep Snare Shot", "Tight Clap Hard", "Closed Hat Dry", "Open Hat Breath", "Rim Tight", "Wood Knock", "Tom Electro Mid", "Tom Electro High", "Crash Short", "Metal Tin", "Riser Tension", "Impact Sub Drop", "Zap Laser Down", "Sweep Phaser" },
          A { "X..x......x.....", "", "........X.......", "........x.......", "..x...x...x...x.", "", "......x.......x.", "", "", "", "", "", "", "", "", "" },
          A { "X..x..x...x..x..", "", "........X.......", "........x.......", "x.x.x.x.x.x.x.x.", "", "...x..x...x...x.", "............x...", "", "", "", "", "", "", "", "" },
          HC, 2, 0.0f },
        { "Brostep", 145,
          A { "Distorted Kick Torn", "Dubstep Kick Crunch", "Dubstep Snare Blast", "Big Clap Hall", "Closed Hat Metal", "Open Hat Trash", "Rim Hollow", "Metal Pipe", "Tom Electro Low", "Tom Electro Mid", "Crash Bright", "China Gong", "Riser Saw", "Impact Slam", "Zap Rise", "Reverse Clap" },
          A { "X.........x.....", "", "........X.......", "........x.......", "x.xxx.xxx.xxx.xx", "", "", "", "", "", "x...............", "", "", "", "", "" },
          A { "X..x......x..x..", "", "........X...o...", "........x.......", "x.xxx.xxx3x.x6x.", "", "", "", "............x.x.", ".............x.x", "", "", "", "", "", "" },
          HC, 2, 0.0f },
        { "Electro House", 128,
          A { "Club Kick Driven", "Sub Kick Under", "Clap Snare Fat", "Club Clap Smack", "Closed Hat Sizzle", "Open Hat Bright", "Shaker Seed", "Rim Stick", "Tom Electro Low", "Tom Electro High", "Crash Bright", "Ride Wash", "Riser Saw", "Impact Boom", "Zap Chirp", "Sweep White Up" },
          A { "X...x...x...x...", "", "", "....x.......x...", "oxo.oxo.oxo.oxo.", "...x...x...x...x", "", "", "", "", "", "", "", "", "", "" },
          A { "X...x...x...x...", "", "....x.......x..o", "....x.......x...", "oxo.oxo.oxo.oxo.", "...x...x...x...x", "oooooooooooooooo", "..x.....x.x.....", "", "", "x...............", "", "", "", "..............x.", "" },
          HC, 1, 0.0f },
        { "Tech House", 125,
          A { "Tech Kick Thump", "Sub Kick Floor", "Tech Snare Clipped", "Club Clap Dry", "Closed Hat Tick", "Open Hat Loose", "Shaker Sand", "Rim Click", "Conga Low Open", "Conga High Open", "Crash Short", "Ride Ping", "Wood Block", "Perc Plop", "Metal Bell", "Sweep Wind" },
          A { "x...x...x...x...", "", "", "....x.......x...", "xx.xxx.xxx.xxx.x", "..x...x...x...x.", "oxoooxoooxoooxoo", "...x..x....x..x.", "", "", "", "", "", "", "", "" },
          A { "x...x...x...x...", "", "", "....x.......x...", "xx.xxx.xxx.xxx.x", "..x...x...x...x.", "oxoooxoooxoooxoo", "...x..x....x..x.", "......x.......x.", "...x.......x....", "", "", "", "", "", "" },
          HC, 1, 0.25f },
        { "Melodic Techno", 122,
          A { "Progressive Kick Velvet", "Sub Kick Rumble", "Tech Snare Short", "Club Clap Crisp", "Closed Hat Glass", "Open Hat Silver", "Shaker Long", "Rim Hollow", "Tom Electro Low", "Metal Chime", "Crash Dark", "Ride Wash", "Riser Noise", "Impact Cinematic", "Perc Drip", "Reverse Cymbal Suck" },
          A { "x...x...x...x...", "..o...o...o...o.", "", "....x.......x...", "oo.ooo.ooo.ooo.o", "..x...x...x...x.", "L12:x..o..x..o..", "L12:..x....x.x..", "", "", "", "", "", "", "", "" },
          A { "x...x...x...x...", "..o...o...o...o.", "", "....x.......x...", "oo.ooo.ooo.ooo.o", "..x...x...x...x.", "L12:x..o..x..o..", "L12:..x....x.x..", "", "L10:x.........", "", "x.o.x.o.x.o.x.o.", "", "", "L14:......x.....", "" },
          HC, 1, 0.0f },
        { "Future Bass", 150,
          A { "Club Kick Round", "808 Kick Long", "Clap Snare Wide", "Big Clap Cloud", "Closed Hat Soft", "Open Hat Swish", "Snap Finger", "Rim Snappy", "Tom Body", "Tom Top", "Crash Wide", "Ride Bell", "Riser Lift", "Impact Cinematic", "Perc Blip", "Reverse Snare" },
          A { "X.........x.....", "", "........X.......", "........x.......", "x.x.x.x.x.x.x3x.", "", "....x.......x...", "", "", "", "x...............", "", "", "", "", "" },
          A { "X.....x...x.....", "", "........X.......", "........x.......", "x.x.x.x.x.x6x.x4", "..............x.", "....x.......x...", "", "", "", "", "", "", "", "", "" },
          HC, 4, 0.0f },
        { "Drum & Bass", 174,
          A { "Tech Kick Knock", "Sub Kick Pressure", "Dubstep Snare Snap", "Tight Clap Pinch", "Closed Hat Sharp", "Open Hat Breath", "Shaker Fast", "Rim Tight", "Tom Electro Mid", "Tom High", "Crash Bright", "Ride Jazz", "Riser Noise", "Impact Boom", "Zap Laser Down", "Reverse Kick" },
          A { "X.........x.....", "", "....X.......X...", "", "x.x.x.x.x.x.x.x.", "", "gggggggggggggggg", "", "", "", "x...............", "", "", "", "", "" },
          A { "X......x..x.....", "", "....X..o....X..o", "", "x.x.x.x.x.x.x.x.", "..............x.", "gggggggggggggggg", "", "", "", "", "o.o.o.o.o.o.o.o.", "", "", "", "" },
          HC, 2, 0.0f },
        { "Trap Hard", 145,
          A { "808 Long C", "Dubstep Kick Hit", "Trap Snare Pop", "Clap Snare Layer", "Closed Hat Chip", "Open Hat Club", "Rim Click", "Perc Bubble", "Tom Deep", "Tom Top", "Crash Short", "Metal Bell", "Shaker Egg", "Snap Dry", "Zap Chirp", "Riser Lift" },
          A { "x......x..x.....", "x......x..x.....", "........X.......", "........x.......", "x.xxx.x.x3x.x.x4", ".............x..", "", "", "", "", "", "", "", "", "", "" },
          A { "x..x...x..x...x.", "x..x...x..x...x.", "........X.....o.", "........x.......", "x.x.x6x.x.x.x3x8", "", "", "", "", "", "x...............", "", "", "", "", "" },
          HC, 3, 0.0f },
        { "Drill Dark", 142,
          A { "808 Slide Down", "Dubstep Kick Bone", "Trap Snare Hollow", "Tight Clap Flat", "Closed Hat Edge", "Open Hat Dark", "Rimshot Wood", "Perc Drip", "Tom Floor", "Tom Rack", "Crash Dark", "Metal Pipe", "Shaker Brush", "Snap Room", "Riser Tension", "Reverse Cymbal Suck" },
          A { "x.....x...x.....", "x.....x...x.....", "......X.......X.", "......x.......x.", "x..x..x.x..x.x3.", "", "...x.......x....", "", "", "", "", "", "", "", "", "" },
          A { "x.....x..x..x...", "x.....x..x..x...", "......X.......X.", "......x.......x.", "x..x3.x.x..x.x6.", "", "...x.......x...x", "", "", "", "", "", "", "", "", "" },
          HC, 3, 0.0f },
        { "Phonk Drift", 130,
          A { "808 Distorted C", "Distorted Kick Gritty", "Lofi Snare Tape", "Dusty Clap Vinyl", "Closed Hat Dark", "Open Hat Disco", "Cowbell", "Rim Stick", "Tom Deep", "Tom Top", "Crash Short", "Metal Tin", "Shaker Seed", "Snap Finger", "Impact Sub Drop", "Reverse Clap" },
          A { "x..x..x...x.....", "x..x..x...x.....", "....x.......x...", "....x.......x...", "x.x.x.x.x.x.x.x.", "", "x.xx.x.xx.x.x.x.", "", "", "", "", "", "", "", "", "" },
          A { "x..x..x...x..x..", "x..x..x...x..x..", "....x.......x...", "....x.......x...", "x.x.x.x.x3x.x4x.", "", "x.xx.x.xx.xxx.xx", "", "", "", "", "", "", "", "", "" },
          HC, 3, 0.0f },
        { "Lo-fi Tape", 80,
          A { "Lofi Kick", "Progressive Kick Soft", "Lofi Snare Dusty", "Snap Finger", "Closed Hat Soft", "Open Hat Breath", "Rimshot Wood", "Shaker Brush", "Tom Body", "Conga Muted", "Ride Jazz", "Wood Log", "Vinyl Crackle", "Dusty Clap Worn", "Perc Drip", "Reverse Snare" },
          A { "x......x.x......", "", "....x.......x...", "", "x.o.x.o.x.o.x.o.", "", "", "", "", "", "", "", "x...............", "", "", "" },
          A { "x......x.x....x.", "", "....x.......x..o", "", "x.o.x.oxx.o.x.o.", "", "...o......o.....", "o..o............", "", "", "", "", "x...............", "", "", "" },
          HC, 3, 0.35f },
        { "Boom Bap Dusty", 92,
          A { "Boom Bap Kick", "Distorted Kick Fuzz", "Lofi Snare Paper", "Dusty Clap Old", "Closed Hat Hiss", "Open Hat Loose", "Rimshot Cross", "Shaker Sand", "Tom Floor", "Conga High Open", "Crash Dark", "Ride Ping", "Vinyl Crackle", "Snap Dry", "Metal Bell", "Reverse Kick" },
          A { "x.......xx......", "", "....x.......x...", "", "x.x.x.x.x.x.x.x.", "", "", "", "", "", "", "", "x...............", "", "", "" },
          A { "x.....x.xx...x..", "", "....x..o....x...", "", "x.x.x.xxx.x.x.x.", "..............x.", "", "", "", "", "", "", "x...............", "", "", "" },
          HC, 3, 0.3f },
        { "Afro House", 122,
          A { "Club Kick Warm", "Sub Kick Floor", "Rimshot Ring", "Club Clap Classic", "Closed Hat Air", "Open Hat Wash", "Shaker Seed", "Conga Low Open", "Conga High Open", "Conga Slap", "Crash Short", "Ride Jazz", "Wood Clave", "Metal Bell", "Perc Plop", "Tom Body" },
          A { "x...x...x...x...", "", "...x..x....x..x.", "....x.......x...", "..x...x...x...x.", "", "oxoooxoooxoooxoo", "x.....x...x.....", "..x.x....x.x....", "", "", "", "x..x..x...x.x...", "", "", "" },
          A { "x...x...x...x...", "", "...x..x....x..x.", "....x.......x...", "..x...x...x...x.", "", "oxoooxoooxoooxoo", "x.....x...x...x.", "..x.x....x.x..x.", "...........x....", "", "x.o.x.o.x.o.x.o.", "x..x..x...x.x...", "", "", "" },
          HC, 1, 0.2f },
        { "Garage", 132,
          A { "Club Kick Snap", "Sub Kick Under", "Clap Snare Stack", "Tight Clap Short", "Closed Hat Shimmer", "Open Hat Silver", "Shaker Fast", "Rim Click", "Tom Top", "Perc Blip", "Crash Short", "Ride Ping", "Snap Bright", "Wood Stick", "Metal Tin", "Reverse Snare" },
          A { "x......x..x.....", "", "....x.......x...", "....x.......x...", "x.xxx.xxx.xxx.xx", "", "o.oxo.oxo.oxo.ox", "", "", "", "", "", "", "", "", "" },
          A { "x......x..x..x..", "", "....x.......x...", "....x.......x...", "x.xxx.xxx.xxx.x.", "..............x.", "o.oxo.oxo.oxo.ox", "...x.......x....", "", "", "", "", "", "", "", "" },
          HC, 1, 0.5f },
        { "Hardstyle", 150,
          A { "Hardstyle Kick Euphoric", "Hardstyle Kick Screech", "Big Room Snare Clash", "Big Clap Stadium", "Closed Hat Steel", "Open Hat Crash", "Shaker Fast", "Snap Bright", "Tom Electro Low", "Tom Electro High", "Crash Bright", "Ride Bell", "Riser Saw", "Impact Slam", "Zap Rise", "Reverse Cymbal Swell" },
          A { "X...x...x...x...", "", "", "....x.......x...", "..x...x...x...x.", "", "", "", "", "", "x...............", "", "", "", "", "" },
          A { "X...x...x...x.x.", "", "", "....x.......x...", "xx.xxx.xxx.xxx.x", "..x...x...x...x.", "gggggggggggggggg", "", "", "", "", "", "", "", "", "" },
          HC, 5, 0.0f },
        { "Pop", 105,
          A { "Club Kick Classic", "808 Kick Round", "Clap Snare Glue", "Club Clap Party", "Closed Hat Crisp", "Open Hat Bright", "Shaker Egg", "Snap Finger", "Tom Body", "Tom Top", "Crash Wide", "Ride Ping", "Riser Lift", "Impact Boom", "Perc Blip", "Reverse Snare" },
          A { "x.......x.x.....", "", "....x.......x...", "....x.......x...", "x.x.x.x.x.x.x.x.", "", "oooooooooooooooo", "", "", "", "", "", "", "", "", "" },
          A { "x.......x.x..x..", "", "....x.......x...", "....x.......x...", "x.x.x.x.x.x.x.x.", "..............x.", "oooooooooooooooo", "....x.......x...", "", "", "x...............", "", "", "", "", "" },
          HC, 4, 0.1f },
        { "IDM Glitch", 160,
          A { "Glitch Kick Bit", "Sub Kick Pressure", "Glitch Snare Crunch", "Glitch Click Hard", "Glitch Hat Grain", "Open Hat Breath", "Glitch Blip Formant", "Glitch Tick Metal", "Glitch Burst Noise", "Glitch Crush Sine High", "Glitch Grain Tail", "Glitch Zip", "Glitch Click Soft", "Glitch Blip Vowel", "Glitch Grain Shimmer", "Glitch Sub Pulse" },
          A { "x.....x...x.....", "x...............", "....x.......x...", "....x......4x...", "x.x.xx3.x.x.x.6.", "", "..x.......x...x.", "x..x..x..x..x..x", "......x.........", "...........x....", "", "", "", "", "", "x.......x......." },
          A { "x.....x.x.x...x.", "x...............", "....x.......x..3", "....4.......x.x.", "x.x3x.x.xx3.x.x8", "..............x.", "..x.......x.x.x.", "x..x..x..x..x..x", "......x.......x.", ".......x...x....", "..............x.", "............x...", "x...x...x...x...", "", "........x.......", "x.......x......." },
          HC, 5, 0.0f },
        { "Cinematic", 90,
          A { "Sub Kick Pressure", "Big Room Kick Tok", "Big Room Snare Hall", "Big Clap Cathedral", "Closed Hat Dark", "Open Hat Wash", "Tom Floor", "Tom Deep", "Tom Rack", "Metal Gong Hit", "Crash Dark", "China Gong", "Riser Tension", "Impact Thunder", "Downlifter Sink", "Reverse Cymbal Swell" },
          A { "x.......x.......", "", "........x.......", "", "", "", "x..x..x.x..x..x.", "..x.....x.x.....", "", "x...............", "x...............", "", "", "", "", "" },
          A { "x.......x.......", "", "........x.......", "........x.......", "x.x.x.x.x.x.x.x.", "", "x..x..x.x..x..x.", "..x.....x.x...xx", "............x.xx", "", "x...............", "", "", "", "", "" },
          HC, 4, 0.0f },
        // DRILL (appended last, so every older kit keeps its index): the DRILL pack, 2-bar starter beats from the DRILL generator
        { "DRILL", 142,
          A { "DRILL 808 SLIDE", "DRILL KICK", "DRILL SNARE", "DRILL CLAP", "DRILL HAT", "DRILL OPEN HAT", "DRILL METAL PERC", "Tom Low", "Crash", "DRILL 808 LONG", "DRILL 808 KNOCK", "Reverse Crash", "Riser", "Swell", "Zap", "Vinyl Crackle" },
          A { "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "" },
          A { "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "" },
          { 2, 0, 0, 0, 1, 1, 0, 0, 0, 2, 2, 0, 0, 0, 0, 0 }, 6, 0.0f, &drill::starter, drill::kitVols().data() },
        // LATIN (appended after DRILL): the LATIN pack, 4-bar dembow starter beats from the REGGAETON generator
        { "LATIN", 95,
          A { "REGGAETON KICK", "808 Long", "DEMBOW SNARE", "LATIN RIMSHOT", "Hat", "LATIN SHAKER", "LATIN CLAVE", "LATIN CONGA HI", "LATIN CONGA LO", "LATIN BONGO HI", "LATIN BONGO LO", "TIMBALE HI", "TIMBALE LO", "TIMBALE RIM", "LATIN COWBELL", "BRIGHT CLAP" },
          A { "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "" },
          A { "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "" },
          { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, 7, 0.0f, &latin::starter, latin::kitVols().data() },
        // ---- the CLASSIC-CHARACTER kits (appended after LATIN; ClassicPacks.h, docs/research/DRUM-SOUNDS-STUDY.md)
        // ANALOG BOOM: the 8-series character (resonator kick + long boom, two-resonator snare, burst clap, six-oscillator metal)
        { "ANALOG BOOM", 112,
          A { "ANALOG BOOM KICK", "ANALOG BOOM LONG", "ANALOG BOOM SNARE", "ANALOG BOOM CLAP", "ANALOG BOOM CLOSED HAT", "ANALOG BOOM OPEN HAT", "ANALOG BOOM RIMSHOT", "ANALOG BOOM COWBELL",
              "ANALOG BOOM TOM LO", "ANALOG BOOM TOM MID", "ANALOG BOOM CYMBAL", "ANALOG BOOM CLAVES", "ANALOG BOOM MARACAS", "ANALOG BOOM CONGA HI", "ANALOG BOOM CONGA LO", "ANALOG BOOM TOM HI" },
          A { "x.....x...x.....", "", "....x.......x...", "....x.......x...", "x.x.o.x.x.x.o.x.", "..............x.", "", "", "", "", "", "", "", "", "", "" },
          A { "x.....x...x..x..", "", "....x.......x..o", "....x.......x...", "x.x.o.x.x.x.o.x.", "......x.......x.", "...x......x.....", "x..x..x...x.x...", "", "", "", "..x....x..x....x", "oxoooxoooxoooxoo", "..........x.....", "...x.......x....", "" },
          HC, 8, 0.0f, nullptr, classicVols(0).data() },
        // ANALOG PUNCH: the 9-series character (punchy swept kick, two-partial snare, 6-bit metal), four on the floor
        { "ANALOG PUNCH", 124,
          A { "ANALOG PUNCH KICK", "ANALOG PUNCH KICK LONG", "ANALOG PUNCH SNARE", "ANALOG PUNCH CLAP", "ANALOG PUNCH CLOSED HAT", "ANALOG PUNCH OPEN HAT", "ANALOG PUNCH RIMSHOT", "Shaker Egg",
              "ANALOG PUNCH TOM LO", "ANALOG PUNCH TOM MID", "ANALOG PUNCH CRASH", "ANALOG PUNCH RIDE", "ANALOG PUNCH TOM HI", "Riser Noise", "Impact Boom", "Reverse Cymbal Swell" },
          A { "x...x...x...x...", "", "", "....x.......x...", "xx.xox.xxx.xox.x", "..x...x...x...x.", "", "", "", "", "", "", "", "", "", "" },
          A { "x...x...x...x...", "", "............x..o", "....x.......x...", "xx.xox.xxx.xox.x", "..x...x...x...x.", "...x......x.....", "", "", "", "x...............", "x.o.x.o.x.o.x.o.", "", "", "", "" },
          HC, 8, 0.0f, nullptr, classicVols(1).data() },
        // DIGITAL EIGHTIES: the first sampled "real drum" machines (8-bit mu-law, 28-35 kHz): tight, dry, close
        { "DIGITAL EIGHTIES", 104,
          A { "DIGITAL EIGHTIES KICK", "DIGITAL EIGHTIES CONGA LO", "DIGITAL EIGHTIES SNARE", "DIGITAL EIGHTIES CLAP", "DIGITAL EIGHTIES CLOSED HAT", "DIGITAL EIGHTIES OPEN HAT", "DIGITAL EIGHTIES RIM", "DIGITAL EIGHTIES CABASA",
              "DIGITAL EIGHTIES TOM LO", "DIGITAL EIGHTIES TOM MID", "Crash Bright", "DIGITAL EIGHTIES COWBELL", "DIGITAL EIGHTIES TAMBOURINE", "DIGITAL EIGHTIES CLAVES", "DIGITAL EIGHTIES CONGA HI", "DIGITAL EIGHTIES TOM HI" },
          A { "x.......x.x.....", "", "....x.......x...", "", "x.x.o.x.x.x.o.x.", "", "", "oxoooxoooxoooxoo", "", "", "", "", "", "", "", "" },
          A { "x.......x.x..x..", "", "....x.......x...", "....x.......x...", "x.x.o.x.x.x.o.x.", "..............x.", "", "oxoooxoooxoooxoo", "..............xx", ".............x..", "", "", "....x.......x...", "", "", "............x..." },
          HC, 8, 0.0f, nullptr, classicVols(2).data() },
        // CRUNCH TWELVE: the 12-bit 26 kHz sampler (dropped-sample pitch, no reconstruction filter, swept / fixed / open channels): gritty hip hop
        { "CRUNCH TWELVE", 92,
          A { "CRUNCH TWELVE KICK", "CRUNCH TWELVE KICK OPEN", "CRUNCH TWELVE SNARE", "CRUNCH TWELVE CLAP", "CRUNCH TWELVE HAT", "CRUNCH TWELVE OPEN HAT", "CRUNCH TWELVE RIM", "CRUNCH TWELVE SHAKER",
              "CRUNCH TWELVE TOM", "CRUNCH TWELVE SNARE FILTERED", "Crash Dark", "Ride Ping", "Vinyl Crackle", "Snap Dry", "Conga High Open", "Reverse Kick" },
          A { "x.........x..x..", "", "....x.......x...", "", "x.x.o.x.x.x.o.xo", "", "", "", "", "", "", "", "x...............", "", "", "" },
          A { "x.........x..x..", ".......x........", "....x.......x...", "............x...", "x.x.o.x.x.x.o.xo", "..............x.", "", "", "", ".......o.o......", "", "", "x...............", "", "", "" },
          HC, 8, 0.32f, nullptr, classicVols(3).data() },
        // SWING TWELVE: the 12-bit 40 kHz production centre (~18 kHz bandwidth, a hot converter): punchy and warm, swung at 61 %
        { "SWING TWELVE", 94,
          A { "SWING TWELVE KICK", "SWING TWELVE TOM", "SWING TWELVE SNARE", "SWING TWELVE CLAP", "SWING TWELVE HAT", "SWING TWELVE OPEN HAT", "SWING TWELVE RIM", "SWING TWELVE SHAKER",
              "SWING TWELVE CONGA", "Snap Finger", "Crash Short", "Ride Jazz", "Vinyl Crackle", "Wood Block", "Metal Bell", "Reverse Snare" },
          A { "x......x..x.....", "", "....x.......x...", "", "x.x.o.x.x.x.o.x.", "", "", "", "", "", "", "", "", "", "", "" },
          A { "x......x..x..x..", "", "....x..o....x...", "............x...", "x.xxo.x.x.xxo.x.", "..............x.", "..x.......x.....", "oooooooooooooooo", "", "", "", "", "", "", "", "" },
          HC, 8, 0.44f, nullptr, classicVols(4).data() },
        // TRAP GOLD: modern processed trap (a click-led kick over a long tuned 808 family, crisp snare / clap stack, tiny bright hats)
        { "TRAP GOLD", 140,
          A { "TRAP GOLD 808 F", "TRAP GOLD KICK", "TRAP GOLD SNARE", "TRAP GOLD CLAP", "TRAP GOLD CLOSED HAT", "TRAP GOLD OPEN HAT", "TRAP GOLD RIMSHOT", "TRAP GOLD BELL",
              "TRAP GOLD 808 D", "TRAP GOLD 808 G", "Crash Bright", "TRAP GOLD 808 A", "Shaker Egg", "Snap Dry", "Riser Lift", "Reverse Cymbal Swell" },
          A { "x......x..x.....", "x......x..x.....", "........x.......", "........x.......", "x.xxx.x.o3x.x.x4", ".............x..", "", "", "", "", "", "", "", "", "", "" },
          A { "x......x..x..x..", "x......x..x..x..", "........x.....o.", "........x.......", "x.x.x6x.o.x.x3x8", "", "", "..x.......x.....", "", "", "x...............", "", "", "", "", "" },
          { 2, 0, 0, 0, 1, 1, 0, 0, 2, 2, 0, 2, 0, 0, 0, 0 }, 9, 0.0f, nullptr, classicVols(5).data() },
        // TEXAS BOUNCE: up-tempo and straight (not half time), short punchy drums, a roomy clap on 2 and 4, short 808 notes on the kick
        { "TEXAS BOUNCE", 115,
          A { "TEXAS BOUNCE KICK", "TEXAS BOUNCE 808", "TEXAS BOUNCE SNARE", "TEXAS BOUNCE CLAP", "TEXAS BOUNCE HAT", "TEXAS BOUNCE OPEN HAT", "TEXAS BOUNCE SNAP", "TEXAS BOUNCE COWBELL",
              "Tom Body", "Tom Top", "Crash Short", "Ride Ping", "Shaker Egg", "Rim Click", "Riser Lift", "Impact Boom" },
          A { "x..x..x...x..x..", "x..x..x...x..x..", "....x.......x...", "....x.......x...", "ooxoooxoxoxoooxo", "", "...........x...o", "", "", "", "", "", "", "", "", "" },
          A { "x..x..x...x..x..", "x..x..x...x..x..", "....x.......x..o", "....x.......x...", "ooxoooxoxoxoooxo", "......x.......x.", "...........x...o", ".x.......x.....x", "", "", "", "", "", "", "", "" },
          { 0, 2, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, 9, 0.0f, nullptr, classicVols(6).data() },
        // BOOM BAP CLASSIC: dusty, saturated, a hair flat through the 12-bit sampler; swung 58 %
        { "BOOM BAP CLASSIC", 90,
          A { "BOOM BAP CLASSIC KICK", "Boom Bap Kick", "BOOM BAP CLASSIC SNARE", "Dusty Clap Old", "BOOM BAP CLASSIC HAT", "BOOM BAP CLASSIC OPEN HAT", "BOOM BAP CLASSIC RIM", "BOOM BAP CLASSIC SHAKER",
              "Tom Floor", "Conga High Open", "Crash Dark", "Ride Ping", "Vinyl Crackle", "Snap Dry", "Metal Bell", "Reverse Kick" },
          A { "x.........x..x..", "", "....x.......x...", "", "x.x.o.x.x.x.o.xo", "", "", "", "", "", "", "", "x...............", "", "", "" },
          A { "x.........x..x..", "", "....x.......x...", "............x...", "x.x.o.x.x.x.o.xo", "..............x.", ".......o.o......", "", "", "", "", "", "x...............", "", "", "" },
          HC, 8, 0.32f, nullptr, classicVols(7).data() },
        // POP SHINE: clean pop drums (a big clean kick, a bright snare + clap stack with a plate, crisp shaker); B = the Eighties gated snare
        { "POP SHINE", 110,
          A { "POP SHINE KICK", "Sub Kick Under", "POP SHINE SNARE", "POP SHINE CLAP", "POP SHINE HAT", "POP SHINE OPEN HAT", "POP SHINE SHAKER", "POP SHINE SNARE GATED",
              "DIGITAL EIGHTIES TOM LO", "DIGITAL EIGHTIES TOM HI", "Crash Wide", "Ride Ping", "Snap Finger", "Riser Lift", "Impact Boom", "Reverse Snare" },
          A { "x.......x.x.....", "", "....x.......x...", "....x.......x...", "x.x.o.x.x.x.o.x.", "", "oooooooooooooooo", "", "", "", "", "", "", "", "", "" },
          A { "x.......x.x..x..", "", "", "....x.......x...", "x.x.o.x.x.x.o.x.", "..............x.", "oooooooooooooooo", "....x.......x...", "............x...", "..............x.", "", "", "", "", "", "" },
          HC, 8, 0.0f, nullptr, classicVols(8).data() },
        // TRAP CLASSIC (appended last): the early-2010s orchestral trap kit, 4-bar starter beats from the TRAP CLASSIC generator
        { "TRAP CLASSIC", 150,
          A { "BOOM 808", "TRAP KICK", "TRAP CLASSIC SNARE", "TRAP CLASSIC CLAP", "TRAP HAT CLOSED", "TRAP HAT OPEN", "BRASS HIT", "TIMP TOM",
              "TRAP CLASSIC CRASH", "TRAP GOLD BELL", "TRAP GOLD RIMSHOT", "Riser Lift", "Reverse Cymbal Swell", "Snap Dry", "Impact Boom", "Shaker Egg" },
          A { "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "" },
          A { "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "" },
          HC, 9, 0.0f, &trapc::starter, trapc::kitVols().data() },
    };
    return k;
}

// measured with the test harness (BeatsTests levels): pattern A at default settings -> -14 LUFS
inline float kitTrimDb(const juce::String& kit) {
    static const std::map<juce::String, float> t {
        { "Trap", -1.7f }, { "Drill", -0.1f }, { "Boom Bap", 9.1f }, { "Lofi", 11.2f }, { "House", -3.4f }, { "Dubstep", -0.4f },
        { "Afrobeats", -5.3f }, { "Phonk", -5.3f }, { "Reggaeton", 5.6f }, { "Jersey Club", -1.5f }, { "Club Progressive", -4.3f }, { "Big Room", -4.5f },
        { "Dubstep Heavy", -8.0f }, { "Riddim", -8.0f }, { "Brostep", -9.7f }, { "Electro House", -2.6f }, { "Tech House", -4.3f }, { "Melodic Techno", -4.7f },
        { "Future Bass", -4.5f }, { "Drum & Bass", -9.2f }, { "Trap Hard", -3.8f }, { "Drill Dark", -5.3f }, { "Phonk Drift", -6.3f }, { "Lo-fi Tape", 5.5f },
        { "Boom Bap Dusty", 2.7f }, { "Afro House", -4.8f }, { "Garage", -5.1f }, { "Hardstyle", -8.9f }, { "Pop", -3.0f }, { "Cinematic", -2.2f },
        { "IDM Glitch", -1.2f }, { "DRILL", -1.25f }, { "LATIN", 0.9f },
        { "ANALOG BOOM", 1.1f }, { "ANALOG PUNCH", -0.4f },
        { "DIGITAL EIGHTIES", 2.4f }, { "CRUNCH TWELVE", 1.2f }, { "SWING TWELVE", 1.2f },
        { "TRAP GOLD", 1.4f }, { "TEXAS BOUNCE", 2.0f }, { "BOOM BAP CLASSIC", 1.8f }, { "POP SHINE", 2.0f }, { "TRAP CLASSIC", 0.4f } };
    auto it = t.find(kit); return it == t.end() ? 0.0f : it->second;
}

} // namespace gb
