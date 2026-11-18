#pragma once
// GHOST FOCUS (Ghost Box): pick an intention (FAT, WIDER, DARKER, DREAMY, DIRTY, MOVE, WEIRD, PUNCH) and get 4 starting chains built from the
// rack's own units and their FACTORY PRESETS (each chain is a list of "unit + factory preset + mix", tagged with its intention). No AI:
// the table below is the whole recipe book, so the same intention always gives the same 4 chains. The processor builds a chain on a private
// rack (BoxLayers.h LayerHost) as a normal sound (the SNAPSHOT format), matches its loudness to what played before (the DNA matcher), and
// loads it through the click-free live path. Every chain keeps the GHOST LIMITER on (peak safety).
#include <juce_audio_processors/juce_audio_processors.h>
#include "FxCore.h"
#include <vector>

namespace boxfocus {

constexpr int kIntents = 8, kChains = 4;
inline const juce::StringArray& intentNames() { static juce::StringArray s { "FAT", "WIDER", "DARKER", "DREAMY", "DIRTY", "MOVE", "WEIRD", "PUNCH" }; return s; }
inline const juce::StringArray& intentTips()
{
    static juce::StringArray s { "FAT: thicker, heavier, more weight in the low end", "WIDER: a bigger stereo picture", "DARKER: less top, more shadow",
                                 "DREAMY: soft, floating, blurred into space", "DIRTY: grit, drive and crunch", "MOVE: motion in time with the song",
                                 "WEIRD: strange, otherworldly colours", "PUNCH: harder hits, more snap" };
    return s;
}

// one unit of a chain: a factory preset of that unit (by name; "" = the unit's defaults), its MIX (< 0 = as the preset says) and overrides
struct Step { int unit; const char* preset; float mix; std::vector<std::pair<const char*, float>> set; };
struct Chain { const char* name; const char* blurb; std::vector<Step> steps; };

inline const std::array<std::array<Chain, kChains>, kIntents>& recipes()
{
    using namespace gpfx;
    static const std::array<std::array<Chain, kChains>, kIntents> r {{
        {{ // FAT
            { "Fat Bass Glow", "FATTEN's bass weight into warm tube colour", { { uFatten, "Fat Bass", -1, {} }, { uTube, "Fat Bottom", 0.7f, {} } } },
            { "Sub Bomb", "a heavier sub with the low end kept tight and mono", { { uBassLab, "Big Sub Weight", -1, {} }, { uFatten, "Sub Bomb", -1, {} } } },
            { "Thick Drums", "fatter hits glued together", { { uFatten, "Fat Drums", -1, {} }, { uCrush, "Glue", 0.35f, {} } } },
            { "Warm Chonk", "a thick body with vintage warmth", { { uRetro, "Warm Tube Glow", 0.8f, {} }, { uFatten, "Thick Body", -1, {} } } } }},
        {{ // WIDER
            { "Subtle Width", "a gentle chorus spread with a touch of room", { { uChorus, "Subtle Width", -1, {} }, { uSpace, "Small Room", 0.15f, {} } } },
            { "Lush Ensemble", "a slow, wide ensemble", { { uChorus, "Slow Ensemble", -1, {} } } },
            { "Wide Top, Mono Low", "the top spread wide, the low end kept centred", { { uBassLab, "Clean Wide Top", -1, {} }, { uChorus, "Classic Chorus", 0.3f, {} } } },
            { "Wide Body", "the body of every hit spread, the attacks stay focused", { { uSplit, "Wide Body", -1, {} }, { uSpace, "Plate", 0.15f, {} } } } }},
        {{ // DARKER
            { "Dark Lowpass", "the top rolled off with a resonant low-pass", { { uFilter, "Dark Lowpass", -1, {} } } },
            { "Dark Chamber", "a warm bus into a dark room", { { uTube, "Warm Bus", -1, {} }, { uSpace, "Dark Chamber", 0.25f, {} } } },
            { "Dark Tape Echo", "dark analog echoes under a low-pass", { { uFilter, "Dark Lowpass", 0.7f, {} }, { uEcho, "BBD Dark", 0.25f, {} } } },
            { "Dark Pillow", "soft, dark weight", { { uFatten, "Dark Pillow", -1, {} }, { uHalls, "Dark Tape Verb", 0.2f, {} } } } }},
        {{ // DREAMY
            { "Halo", "a soft shimmer halo over a slow ensemble", { { uChorus, "Slow Ensemble", 0.3f, {} }, { uShimmer, "Subtle Halo", -1, {} } } },
            { "Cloud Nine", "a huge, diffuse cloud", { { uHalls, "Cloud Nine", 0.3f, {} } } },
            { "Reverse Swells", "reversed splices blooming into glimmering tails", { { uCrystal, "Reverse Swells", 0.3f, {} }, { uSpace, "Glimmer Tails", 0.25f, {} } } },
            { "Dusty Dream", "old vinyl drifting into a concert hall", { { uRetro, "Dusty Vinyl", 0.6f, {} }, { uHalls, "Concert Hall", 0.25f, {} } } } }},
        {{ // DIRTY
            { "Tube Drive", "an overdriven tube, warm and gritty", { { uTube, "Overdriven", -1, {} } } },
            { "Cracked", "multiband destruction with a cracked top", { { uCrackle, "Cracked Sidewalk", 0.7f, {} } } },
            { "Lo-Fi Beat Tape", "crunchy tape, dust and crushed bits", { { uRetro, "Lo-Fi Beat Tape", -1, {} } } },
            { "Crunchy Attack", "driven attacks, parallel grit", { { uSplit, "Crunchy Attack", -1, {} }, { uCrush, "Parallel Grit", -1, {} } } } }},
        {{ // MOVE
            { "Slow Sweep", "a tempo-synced filter sweep", { { uFilter, "Slow Sweep", -1, {} } } },
            { "Ping Pong", "dotted echoes bouncing left and right", { { uEcho, "Ping Pong 1/8d", 0.3f, {} } } },
            { "Vowel Wobble", "a talking vowel filter in time", { { uFormant, "Vowel Wobble", 0.8f, {} } } },
            { "Dotted Groove", "a gated groove pattern", { { uGate, "Dotted Groove", -1, {} } } } }},
        {{ // WEIRD
            { "Glitch Spray", "spectral scatter and glitches", { { uSpectra, "Glitch Spray", 0.6f, {} } } },
            { "Octave Down Ghost", "a ghost an octave below", { { uCrystal, "Octave Down Ghost", 0.4f, {} } } },
            { "Robot Mouth", "a robotic talking filter", { { uFormant, "Robot Mouth", 0.8f, {} } } },
            { "Rotating Temple", "a morphing, rotating space", { { uPortal, "Rotating Temple", 0.35f, {} } } } }},
        {{ // PUNCH
            { "Punch Up", "louder attacks, tighter tails", { { uSplit, "Punch Up", -1, {} } } },
            { "Drum Smash", "a smashed drum bus, blended in", { { uCrush, "Drum Smash", 0.45f, {} } } },
            { "Fast Squash", "fast 3-band squeeze, blended in", { { uOtt, "Fast Squash", 0.5f, { { "out", 6.0f } } } } },
            { "Snappy Glue", "snappy hits glued together", { { uSplit, "Snappy Dry Tail", -1, {} }, { uFatten, "Smash Glue", 0.5f, {} } } } }}
    }};
    return r;
}
inline const Chain& chain (int intent, int k) { return recipes()[(size_t) juce::jlimit (0, kIntents - 1, intent)][(size_t) juce::jlimit (0, kChains - 1, k)]; }
inline juce::String unitsText (const Chain& c)
{
    juce::StringArray s; for (auto& st : c.steps) s.add (juce::String (gpfx::unitInfo (st.unit).shortName) + (st.preset != nullptr && *st.preset ? juce::String (" ") + st.preset : juce::String()));
    return s.joinIntoString ("  +  ");
}

} // namespace boxfocus
