// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST ATLAS - the things every part shares: where files live, the eight worlds, the nineteen categories, small helpers.
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace atlas
{
constexpr int kLayers = 12;      // the layers of a sound (eight, Quetzalcoatl's layout, until 0.1.10)
// How many layer slots the screen shows: the eight there always were, and past the seventh layer one more than the
// last in use, so a sound of a few layers looks as it always did and the row only grows when it is needed.
inline int layerSlotsShown (int lastUsed) { return lastUsed + 2 < 8 ? (kLayers < 8 ? kLayers : 8) : (lastUsed + 2 > kLayers ? kLayers : lastUsed + 2); }
constexpr int kSends = 3;        // A (motion), B (echo), C (space)
constexpr int kInserts = 2;      // effect slots on every layer
constexpr int kMasterSlots = 3;  // tone, glue, limiter
constexpr int kMacros = 4;
constexpr int kWorlds = 8;

// ---------------------------------------------------------------- where things live
// <home>/Music/GhostBlock/Ghost Atlas  (GHOST_ATLAS_HOME moves it: the checks and the build machine use that)
inline juce::File atlasRoot()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("GHOST_ATLAS_HOME", {});
    if (env.isNotEmpty()) return juce::File (env);
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("GhostBlock").getChildFile ("Ghost Atlas");
}
inline juce::File libraryDir() { return atlasRoot().getChildFile ("Library"); }
inline juce::File userPresetDir() { return atlasRoot().getChildFile ("Presets"); }
inline juce::File databaseFile() { return atlasRoot().getChildFile ("library.json"); }
inline juce::File settingsFile() { return atlasRoot().getChildFile ("settings.json"); }
inline juce::File reportsDir() { return atlasRoot().getChildFile ("Reports"); }
inline juce::File mappingsDir() { return atlasRoot().getChildFile ("Mappings"); }   // which key each loose recording sits on, where the owner has said so

// ---------------------------------------------------------------- the eight worlds (from the film) + the harbor
struct WorldDef
{
    const char* key;         // stable, saved in presets
    const char* name;
    const char* line;        // one line under the name
    const char* art;         // embedded picture (hero)
    const char* art2;        // embedded picture (second view)
    juce::uint32 accent;     // the world's light
    juce::uint32 accent2;    // its second colour
    juce::uint32 deep;       // its shadow (panel tint)
    float focusX, focusY;    // the point the hero picture is framed round (0..1). It is where the boat's lantern was
                             // before the boat was painted out, so every picture keeps the framing it had.
    float focus2X, focus2Y;  // and in the second view
    int weather;             // which drifting things are drawn over it
};
enum Weather { wMotes, wEmbers, wPetals, wLanterns, wRain, wRings, wSnow, wStars, wMist };
enum { kHarbor = 8 };        // index of the ninth picture (the library page)

inline const WorldDef& world (int i)
{
    static const WorldDef defs[kWorlds + 1] = {
        { "amber",   "Amber Marsh",     "Linen and low sun: pianos and keys",               "s01a_jpg", "s01b_jpg", 0xffffc56b, 0xffff8f3d, 0xff3a2410, 0.566f, 0.670f, 0.575f, 0.849f, wMotes },
        { "crimson", "Crimson Loom",    "Dye works at dusk: analog synths, leads, plucks",  "s02a_jpg", "s02b_jpg", 0xffff7a66, 0xffb48aff, 0xff3a1220, 0.605f, 0.735f, 0.836f, 0.884f, wEmbers },
        { "blossom", "Blossom Flood",   "The drowned orchard: choirs and vocal pads",       "s03b_jpg", "s03a_jpg", 0xffdcc8ff, 0xffffb38a, 0xff1b2540, 0.560f, 0.665f, 0.828f, 0.771f, wPetals },
        { "lantern", "Lantern Sanctum", "A thousand lights: bells, chops, vocal textures",  "s04b_jpg", "s04a_jpg", 0xffffa27e, 0xffb99cff, 0xff22183a, 0.548f, 0.621f, 0.524f, 0.536f, wLanterns },
        { "opera",   "Emerald Opera",   "Rain in the house: strings and cinematic",         "s05a_jpg", "s05b_jpg", 0xffffc46a, 0xff5fdcb0, 0xff0e2a26, 0.783f, 0.590f, 0.410f, 0.828f, wRain },
        { "glow",    "Glow Tide",       "Rings on dark water: experimental and granular",   "s06c_jpg", "s06b_jpg", 0xff6ff2e6, 0xff8a9cff, 0xff0b1a33, 0.267f, 0.499f, 0.608f, 0.783f, wRings },
        { "snow",    "Snow Orchard",    "The quiet: ambient and soundscapes",               "s07b_jpg", "s07a_jpg", 0xffffd9e6, 0xffffc068, 0xff2a2030, 0.578f, 0.618f, 0.804f, 0.595f, wSnow },
        { "aurora",  "Aurora Lake",     "The sky comes down: pads, digital synths, hybrids","s08a_jpg", "s08b_jpg", 0xff86ffb8, 0xffa4aaff, 0xff14183f, 0.781f, 0.593f, 0.587f, 0.751f, wStars },
        { "harbor",  "Dawn Harbor",     "Where new sounds come ashore",                     "s09a_jpg", "s09b_jpg", 0xffffd7a0, 0xffb9a8ff, 0xff2a2a52, 0.727f, 0.770f, 0.641f, 0.670f, wMist },
    };
    return defs[juce::jlimit (0, kWorlds, i)];
}
inline int worldIndex (const juce::String& key)
{
    for (int i = 0; i < kWorlds; ++i) if (key == world (i).key) return i;
    return 0;
}

// ---------------------------------------------------------------- the nineteen categories, each living in one world
struct CategoryDef { const char* name; int world; };
inline const std::vector<CategoryDef>& categories()
{
    static const std::vector<CategoryDef> c = {
        { "Pianos", 0 }, { "Keys", 0 },
        { "Analog Synths", 1 }, { "Leads", 1 }, { "Plucks", 1 },
        { "Choirs", 2 }, { "Vocal Pads", 2 },
        { "Bells", 3 }, { "Vocal Chops", 3 }, { "Vocal Textures", 3 },
        { "Strings", 4 }, { "Cinematic", 4 },
        { "Experimental", 5 }, { "Granular", 5 },
        { "Ambient", 6 }, { "Soundscapes", 6 },
        { "Pads", 7 }, { "Digital Synths", 7 }, { "Hybrid Instruments", 7 },
    };
    return c;
}
inline int worldOfCategory (const juce::String& category)
{
    for (const auto& c : categories()) if (category.equalsIgnoreCase (c.name)) return c.world;
    return 7;
}

// ---------------------------------------------------------------- small helpers
inline float dbToGain (float db) { return db <= -90.0f ? 0.0f : std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) { return g <= 1.0e-9f ? -180.0f : 20.0f * std::log10 (g); }
inline float lerp (float a, float b, float t) { return a + (b - a) * t; }
inline float clamp01 (float v) { return juce::jlimit (0.0f, 1.0f, v); }
inline juce::String noteName (int midi) { return juce::MidiMessage::getMidiNoteName (midi, true, true, 4); }

inline juce::var parseJson (const juce::String& text) { juce::var v; if (juce::JSON::parse (text, v).failed()) return {}; return v; }
inline juce::var readJson (const juce::File& f) { return f.existsAsFile() ? parseJson (f.loadFileAsString()) : juce::var(); }
inline bool writeJson (const juce::File& f, const juce::var& v)
{
    f.getParentDirectory().createDirectory();
    juce::TemporaryFile tmp (f);
    if (! tmp.getFile().replaceWithText (juce::JSON::toString (v, false))) return false;
    return tmp.overwriteTargetFileWithTemporary();
}
inline juce::var prop (const juce::var& o, const char* name, const juce::var& fallback = {})
{
    if (auto* d = o.getDynamicObject()) if (d->hasProperty (name)) return d->getProperty (name);
    return fallback;
}
inline juce::DynamicObject::Ptr newObject() { return new juce::DynamicObject(); }
inline juce::StringArray toStrings (const juce::var& v)
{
    juce::StringArray out;
    if (auto* a = v.getArray()) for (const auto& e : *a) out.add (e.toString());
    return out;
}
inline juce::var fromStrings (const juce::StringArray& s) { juce::Array<juce::var> a; for (const auto& e : s) a.add (e); return a; }

// a file name that is safe everywhere
inline juce::String safeName (const juce::String& s)
{
    auto t = s.retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-+#.,()&'").trim();
    return t.isEmpty() ? juce::String ("untitled") : t.substring (0, 96);
}
} // namespace atlas
