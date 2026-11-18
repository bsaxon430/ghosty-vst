// Ghost Matter - AGPL-3.0-only (see LICENSE, NOTICE.md).
#include "Presets.h"

namespace matter
{
namespace
{
// Build 2: every preset starts from the engines' OWN defaults. MAKE's LOUD / BRIGHT / ROUGH / SHAPE stay at their defaults, so
// every Flues control sits at its plug-in's .ttl default; only the engine's own choices change (PM-Synth's interface type,
// Chatterbox's vowel preset, Disyn's algorithm) and the note. STRETCH is PaulXStretch at its plug-in defaults with its own
// "Stretch amount" (and, where said, its "FFT size") set; SPACE is Pertylizer at our page defaults; MORPH is CDP's own
// programs; its NEURAL presets are DDSP-VST at its own control defaults with one of its own instruments.
// One exception, measured (GhostMatterTests flues): PM-Synth's .ttl default DC (breath) level 0.5 sends a DC offset
// through, and on REED, BRASS and DRUM drives the output past full scale (DC up to 1.8, 100k+ clipped samples). Its own
// DC level control at 0 (LOUD 0 here; the body is then excited by its own noise source) is clean on every body we use
// (DC under 0.02, nothing clipped). CRYSTAL and VAPOR keep a DC offset at every setting, so no preset uses them.
// Gain-staging (measured on the demos: one key peaks near -6 dBTP, so chords stay under full scale): first PaulXStretch's own "Main volume" (up to its
// +12 dB), then Ghost Matter's plain output Level fader (up to +6 dB). No compressor, no limiter.
Preset make (const char* name, const char* group, Source src, int body, float secs, int note = kRootNote)
{
    Preset p;
    p.name = name; p.group = group;
    auto& m = p.render.make;
    m.source = src; m.body = body; m.seconds = secs; m.note = note;
    if (src == Source::physical) m.loud = 0.0f;   // PM-Synth's own DC level at 0 (see above)
    return p;
}
}

const std::vector<Preset>& factoryPresets()
{
    static const std::vector<Preset> list = []
    {
        std::vector<Preset> v;
        {   // 0: the first sound on a fresh page: PM-Synth's BELL, stretched by PaulXStretch
            auto p = make ("Glass Bell Cloud", "TEXTURES", Source::physical, 6, 3.0f);
            p.render.stretch.amount = 8.0f; p.render.stretch.volume = 12.0f;
            p.params = { { "attack", 0.4f }, { "release", 2.5f } };
            v.push_back (p);
        }
        {   // 1
            auto p = make ("Breathing Choir", "VOICES", Source::voice, 0, 4.0f);
            p.render.stretch.amount = 8.0f; p.render.stretch.volume = 2.5f;
            p.params = { { "attack", 0.8f }, { "release", 3.0f } };
            v.push_back (p);
        }
        {   // 2
            auto p = make ("Whisper Field", "VOICES", Source::voice, 7, 3.0f);
            p.render.stretch.amount = 4.0f; p.render.stretch.volume = 8.0f;
            p.params = { { "release", 1.5f } };
            v.push_back (p);
        }
        {   // 3
            auto p = make ("Long Reed", "PHYSICAL", Source::physical, 2, 2.0f);
            p.render.stretch.amount = 3.0f; p.render.stretch.volume = 12.0f;
            p.params = { { "release", 1.2f } };
            v.push_back (p);
        }
        {   // 4: PM-Synth as it is, no stretch
            auto p = make ("Plucked Matter", "PHYSICAL", Source::physical, 0, 2.0f);
            p.params = { { "loop", 0.0f }, { "attack", 0.002f }, { "release", 0.5f }, { "level", 6.0f } };
            v.push_back (p);
        }
        {   // 5: Pertylizer's Euclidean pattern
            auto p = make ("Wooden Pulse", "PATTERNS", Source::physical, 1, 1.0f);
            p.params = { { "loop", 0.0f }, { "pattern", 1.0f }, { "steps", 16.0f }, { "hits", 7.0f }, { "rate", 2.0f }, { "mutate", 0.2f },
                         { "range", 1.0f }, { "gate", 0.5f }, { "attack", 0.002f }, { "release", 0.25f }, { "level", 6.0f } };
            v.push_back (p);
        }
        {   // 6: Pertylizer's Euclidean pattern + Turing machine
            auto p = make ("Bell Rain", "PATTERNS", Source::physical, 6, 1.5f, 60);
            p.params = { { "loop", 0.0f }, { "pattern", 1.0f }, { "steps", 12.0f }, { "hits", 5.0f }, { "rate", 3.0f }, { "mutate", 0.5f },
                         { "range", 2.0f }, { "gate", 0.4f }, { "attack", 0.002f }, { "release", 0.6f }, { "level", 6.0f } };
            v.push_back (p);
        }
        {   // 7
            auto p = make ("Plasma Drone", "DRONES", Source::physical, 11, 4.0f, 36);
            p.render.stretch.amount = 12.0f; p.render.stretch.volume = 8.0f;
            p.params = { { "attack", 1.5f }, { "release", 4.0f } };
            p.terrainShape = "RIDGE";
            v.push_back (p);
        }
        {   // 8: CDP combine cross
            auto p = make ("Metal Throat", "MORPHS", Source::voice, 5, 3.0f);
            p.render.morph.timbre = Timbre::metal; p.render.morph.amount = 0.75f;
            p.params = { { "release", 1.5f }, { "level", 6.0f } };
            v.push_back (p);
        }
        {   // 9: CDP combine cross, then PaulXStretch
            auto p = make ("Glass Voice", "MORPHS", Source::voice, 1, 3.0f);
            p.render.morph.timbre = Timbre::glass; p.render.morph.amount = 0.6f;
            p.render.stretch.amount = 5.0f; p.render.stretch.volume = 0.0f;
            p.params = { { "attack", 0.6f }, { "release", 2.5f } };
            v.push_back (p);
        }
        {   // 10: Disyn
            auto p = make ("Buzz Swarm", "NOISE", Source::noise, 7, 3.0f);
            p.render.stretch.amount = 2.0f; p.render.stretch.volume = 10.0f;
            v.push_back (p);
        }
        {   // 11: Disyn, as it is
            auto p = make ("Torn Saw Bass", "NOISE", Source::noise, 4, 2.0f, 36);
            p.params = { { "attack", 0.003f }, { "release", 0.3f }, { "level", 6.0f } };
            v.push_back (p);
        }
        {   // 12: PM-Synth's BOW, PaulXStretch at a big stretch and its largest FFT size, then CDP morph morph into AIR
            auto p = make ("Bowed Hall", "TEXTURES", Source::physical, 5, 3.0f);
            p.render.stretch.amount = 24.0f; p.render.stretch.size = 1.0f;
            p.render.morph.timbre = Timbre::air; p.render.morph.amount = 0.0f; p.render.morph.detail = 0.5f;
            p.render.stretch.volume = 12.0f;
            p.params = { { "attack", 2.0f }, { "release", 5.0f }, { "spaceY", 0.6f }, { "level", 5.0f } };
            v.push_back (p);
        }
        {   // 13
            auto p = make ("Long Flute", "PHYSICAL", Source::physical, 3, 3.0f);
            p.render.stretch.amount = 6.0f; p.render.stretch.volume = 12.0f;
            p.params = { { "release", 1.5f } };
            v.push_back (p);
        }
        {   // 14: Chatterbox's AH, played back by DDSP-VST's own Violin model
            auto p = make ("Ghost Violin", "NEURAL", Source::voice, 0, 3.0f);
            p.render.morph.ddspModel = "Violin"; p.render.morph.neural = 1.0f;
            p.params = { { "attack", 0.2f }, { "release", 1.5f } };
            v.push_back (p);
        }
        {   // 15: Chatterbox's HUM through DDSP's Trumpet, with CHAOS and HOP (its own controls thrown about, its models switched)
            auto p = make ("Broken Brass", "NEURAL", Source::voice, 6, 3.0f);
            p.render.morph.ddspModel = "Trumpet"; p.render.morph.neural = 1.0f; p.render.morph.wobble = 0.6f; p.render.morph.hop = 0.4f;
            p.params = { { "release", 1.0f } };
            v.push_back (p);
        }
        {   // 16: PM-Synth's BOW stretched by PaulXStretch, then three passes through DDSP's Tuba
            auto p = make ("Melted Tuba", "NEURAL", Source::physical, 5, 3.0f, 36);
            p.render.stretch.amount = 4.0f; p.render.stretch.volume = 6.0f;
            p.render.morph.ddspModel = "Tuba"; p.render.morph.neural = 1.0f; p.render.morph.refeed = 0.67f; p.render.morph.wobble = 0.25f;
            p.params = { { "attack", 0.5f }, { "release", 3.0f } };
            v.push_back (p);
        }
        return v;
    }();
    return list;
}
}
