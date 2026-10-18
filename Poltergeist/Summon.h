#pragma once
// SUMMON: type what you want ("dark trap bell", "wobble growl", "808 slide", "haunted pad") and get a patch.
// A word list maps to a base recipe (category) plus modifiers (adjectives); a seed gives a fresh variation each tap.
// Factory presets are just fixed (words, seed) pairs, so they always come out the same.
#include "Params.h"
#include <map>
#include <random>

namespace gs {

using Patch = std::map<juce::String, float>;

struct Recipe {
    juce::String category;
    juce::StringArray matched;
};

class Summoner {
public:
    static juce::StringArray categories() {
        return { "808", "bass", "growl", "wobble", "reese", "bell", "pluck", "keys", "pad", "lead", "brass", "choir", "riser", "arp", "sub" };
    }

    // Everything at its default, then the recipe on top.
    static Patch summon(const juce::String& query, uint32_t seed, juce::String* describedAs = nullptr) {
        std::mt19937 rng(seed);
        auto R = [&](float a, float b) { std::uniform_real_distribution<float> u(a, b); return u(rng); };
        auto pick = [&](std::initializer_list<float> xs) { std::vector<float> v(xs); return v[(size_t) (rng() % v.size())]; };

        const auto words = tokenize(query);
        juce::String cat = categoryFor(words);
        if (cat.isEmpty()) { auto c = categories(); cat = c[(int) (rng() % (uint32_t) c.size())]; }

        Patch p = base();
        auto op = [&](int o, const char* k, float v) { p[opId(o, k)] = v; };
        auto mod = [&](int f, int t, float v) { p[modId(f, t)] = v; };
        auto macro = [&](int m, float v) { p["macro_" + kMacroIds[m]] = juce::jlimit(0.0f, 1.0f, v); };
        auto env = [&](int o, float a, float d, float s, float r) { op(o, "a", a); op(o, "d", d); op(o, "s", s); op(o, "r", r); };

        if (cat == "808" || cat == "sub") {
            op(0, "ratio", 1); op(0, "level", 0.95f); op(0, "wave", cat == "sub" ? 0.0f : R(0.0f, 0.35f));
            env(0, 0.001f, R(1.4f, 3.2f), cat == "sub" ? 0.8f : 0.0f, R(0.15f, 0.4f));
            op(1, "ratio", pick({ 1.0f, 2.0f })); env(1, 0.001f, R(0.08f, 0.3f), 0.0f, 0.2f); mod(1, 0, R(0.15f, 0.4f));
            p["penv_amt"] = cat == "sub" ? 0.0f : R(18, 36); p["penv_dec"] = R(0.03f, 0.08f);
            p["mono"] = 1; p["glide"] = R(0.05f, 0.12f); p["octave"] = 0; p["flt_cut"] = R(1500, 4000);
            macro(2, R(0.15f, 0.45f)); macro(4, 0.0f);
        } else if (cat == "bass") {
            op(0, "ratio", 1); op(0, "level", 0.9f); op(0, "wave", R(1.5f, 2.2f)); env(0, 0.002f, 0.8f, 0.7f, 0.12f);
            op(1, "ratio", pick({ 1.0f, 2.0f, 0.5f })); env(1, 0.001f, R(0.12f, 0.4f), R(0.1f, 0.35f), 0.15f); mod(1, 0, R(0.35f, 0.6f));
            p["mono"] = 1; p["glide"] = R(0.0f, 0.06f); p["octave"] = -1; p["flt_cut"] = R(900, 2500); p["flt_res"] = R(0.1f, 0.35f);
            p["flt_env"] = R(0.2f, 0.45f); p["flt_d"] = R(0.15f, 0.4f);
            macro(0, R(0.2f, 0.4f)); macro(2, R(0.2f, 0.4f)); macro(4, 0.05f);
        } else if (cat == "growl" || cat == "wobble") {
            op(0, "ratio", 1); op(0, "level", 0.9f); op(0, "wave", R(1.6f, 2.4f)); env(0, 0.003f, 1.0f, 1.0f, 0.1f);
            op(1, "ratio", pick({ 1.0f, 2.0f, 1.5f })); op(1, "fb", R(0.3f, 0.7f)); env(1, 0.003f, 1.0f, 0.9f, 0.1f); mod(1, 0, R(0.5f, 0.8f));
            op(2, "ratio", pick({ 0.5f, 3.0f, 0.25f })); env(2, 0.003f, 1.0f, 0.9f, 0.1f); mod(2, 1, R(0.3f, 0.6f));
            op(3, "ratio", 0.5f); op(3, "level", 0.35f); env(3, 0.003f, 1.0f, 1.0f, 0.1f);   // sub under the growl
            p["mono"] = 1; p["octave"] = -1; p["flt_cut"] = R(700, 2000); p["flt_res"] = R(0.25f, 0.55f); p["flt_key"] = 0.5f;
            p["lfo_sync"] = 1; p["lfo_div"] = (float) (cat == "wobble" ? pick({ 4, 3, 5, 7 }) : pick({ 2, 4, 7 }));
            p["lfo_shape"] = (float) (cat == "wobble" ? 0 : pick({ 0, 1, 2 }));
            macro(0, R(0.6f, 0.9f)); macro(1, R(0.15f, 0.35f)); macro(2, R(0.55f, 0.85f)); macro(3, cat == "wobble" ? R(0.75f, 1.0f) : R(0.35f, 0.6f)); macro(4, 0.05f);
        } else if (cat == "reese") {
            op(0, "ratio", 1); op(0, "level", 0.7f); op(0, "wave", 2.0f); env(0, 0.01f, 1.0f, 1.0f, 0.2f);
            op(2, "ratio", 1); op(2, "fine", R(6, 14)); op(2, "level", 0.7f); op(2, "wave", 2.0f); env(2, 0.01f, 1.0f, 1.0f, 0.2f);
            op(1, "ratio", 1); env(1, 0.01f, 1.0f, 1.0f, 0.2f); mod(1, 0, R(0.15f, 0.3f)); mod(1, 2, R(0.1f, 0.25f));
            p["mono"] = 1; p["octave"] = -1; p["flt_cut"] = R(450, 900); p["flt_res"] = 0.2f;
            p["lfo_rate"] = R(0.1f, 0.4f); macro(1, R(0.4f, 0.6f)); macro(2, R(0.3f, 0.5f)); macro(3, R(0.25f, 0.4f)); macro(4, 0.1f);
        } else if (cat == "bell") {
            const float r2 = pick({ 3.5f, 1.4142f, 2.76f, 4.0f, 7.0f });
            op(0, "ratio", 1); op(0, "level", 0.8f); env(0, 0.001f, R(2.0f, 4.5f), 0.0f, R(1.2f, 2.5f));
            op(1, "ratio", r2); env(1, 0.001f, R(0.8f, 2.0f), 0.0f, 1.0f); mod(1, 0, R(0.35f, 0.6f));
            op(2, "ratio", pick({ 2.0f, 4.0f })); op(2, "level", R(0.15f, 0.35f)); env(2, 0.001f, R(1.0f, 2.0f), 0.0f, 1.0f);
            op(3, "ratio", pick({ 7.0f, 9.0f, 11.0f })); env(3, 0.001f, 0.4f, 0.0f, 0.5f); mod(3, 2, R(0.2f, 0.4f));
            macro(4, R(0.3f, 0.5f)); macro(1, R(0.0f, 0.2f));
        } else if (cat == "pluck") {
            op(0, "ratio", 1); op(0, "level", 0.85f); op(0, "wave", R(0.0f, 1.2f)); env(0, 0.001f, R(0.25f, 0.6f), 0.0f, R(0.2f, 0.4f));
            op(1, "ratio", pick({ 2.0f, 3.0f, 4.0f, 1.0f })); env(1, 0.001f, R(0.06f, 0.2f), 0.0f, 0.1f); mod(1, 0, R(0.45f, 0.75f));
            macro(1, R(0.1f, 0.35f)); macro(4, R(0.2f, 0.4f));
        } else if (cat == "keys") {
            op(0, "ratio", 1); op(0, "level", 0.7f); env(0, 0.002f, R(2.0f, 3.5f), 0.25f, 0.5f);
            op(1, "ratio", 14); env(1, 0.001f, 0.35f, 0.0f, 0.3f); mod(1, 0, R(0.12f, 0.22f));
            op(2, "ratio", 1); op(2, "level", 0.6f); env(2, 0.002f, R(2.0f, 3.5f), 0.3f, 0.5f);
            op(3, "ratio", 1); env(3, 0.002f, R(1.0f, 2.0f), 0.2f, 0.4f); mod(3, 2, R(0.3f, 0.5f)); op(3, "vel", 0.9f);
            p["lfo_rate"] = 4.5f; macro(3, R(0.1f, 0.2f)); macro(4, R(0.15f, 0.3f));
        } else if (cat == "pad" || cat == "choir") {
            op(0, "ratio", 1); op(0, "level", 0.6f); env(0, R(0.8f, 2.5f), 2.0f, 0.85f, R(1.5f, 3.5f));
            op(2, "ratio", cat == "choir" ? 3.0f : 1.0f); op(2, "fine", R(3, 9)); op(2, "level", cat == "choir" ? 0.35f : 0.55f); env(2, R(1.0f, 3.0f), 2.0f, 0.85f, R(1.5f, 3.5f));
            op(1, "ratio", pick({ 2.0f, 0.5f, 3.0f })); env(1, R(1.0f, 3.0f), 3.0f, 0.6f, 2.0f); mod(1, 0, R(0.15f, 0.35f));
            op(3, "ratio", cat == "choir" ? 5.0f : 4.0f); env(3, 2.0f, 3.0f, 0.5f, 2.0f); mod(3, 2, R(0.1f, 0.25f));
            p["flt_cut"] = R(2000, 6000); if (cat == "choir") { p["flt_type"] = 1; p["flt_cut"] = R(900, 1600); p["flt_res"] = 0.3f; }
            p["lfo_rate"] = R(0.15f, 0.5f);
            macro(1, R(0.5f, 0.8f)); macro(3, R(0.25f, 0.5f)); macro(4, R(0.55f, 0.85f));
        } else if (cat == "lead") {
            op(0, "ratio", 1); op(0, "level", 0.8f); op(0, "wave", R(1.6f, 2.6f)); env(0, 0.004f, 0.6f, 0.8f, 0.25f);
            op(1, "ratio", pick({ 1.0f, 2.0f })); env(1, 0.004f, 0.4f, 0.5f, 0.2f); mod(1, 0, R(0.3f, 0.55f));
            p["mono"] = 1; p["glide"] = R(0.03f, 0.09f); p["lfo_rate"] = 5.0f;
            macro(1, R(0.25f, 0.45f)); macro(2, R(0.2f, 0.4f)); macro(3, R(0.1f, 0.2f)); macro(4, R(0.2f, 0.35f));
        } else if (cat == "brass") {
            op(0, "ratio", 1); op(0, "level", 0.8f); env(0, R(0.03f, 0.08f), 0.8f, 0.8f, 0.25f);
            op(1, "ratio", 1); env(1, R(0.05f, 0.12f), 0.6f, 0.6f, 0.2f); mod(1, 0, R(0.45f, 0.65f)); op(1, "vel", 0.9f);
            op(2, "ratio", 1); op(2, "fine", 5); op(2, "level", 0.4f); env(2, 0.06f, 0.8f, 0.8f, 0.25f);
            macro(1, R(0.2f, 0.35f)); macro(4, R(0.2f, 0.35f));
        } else if (cat == "riser") {
            op(0, "ratio", 1); op(0, "level", 0.6f); env(0, R(3.0f, 6.0f), 2.0f, 1.0f, 1.5f);
            op(1, "ratio", R(1.0f, 7.0f)); op(1, "fb", R(0.6f, 1.0f)); env(1, R(3.0f, 6.0f), 2.0f, 1.0f, 1.5f); mod(1, 0, R(0.5f, 0.8f));
            p["flt_cut"] = 300; p["flt_env"] = 0.8f; p["flt_a"] = R(3.0f, 6.0f); p["flt_s"] = 1.0f; p["flt_res"] = 0.4f;
            p["lfo_sync"] = 1; p["lfo_div"] = 7; macro(1, 0.7f); macro(3, R(0.2f, 0.5f)); macro(4, 0.7f);
        } else if (cat == "arp") {
            op(0, "ratio", 1); op(0, "level", 0.8f); op(0, "wave", R(0.8f, 2.2f)); env(0, 0.001f, R(0.15f, 0.35f), 0.0f, 0.15f);
            op(1, "ratio", pick({ 2.0f, 3.0f, 1.0f })); env(1, 0.001f, 0.12f, 0.1f, 0.1f); mod(1, 0, R(0.3f, 0.55f));
            macro(1, R(0.2f, 0.4f)); macro(4, R(0.35f, 0.55f));
        }

        // modifiers
        auto has = [&](std::initializer_list<const char*> ws) { for (auto w : ws) if (words.contains(w)) return true; return false; };
        auto mul = [&](const char* k, float f) { p[k] = p[k] * f; };
        auto addM = [&](int m, float a) { macro(m, p["macro_" + kMacroIds[m]] + a); };
        if (has({ "dark", "deep", "evil", "night", "shadow", "crypt", "grave" })) { mul("flt_cut", 0.4f); for (int f = 0; f < 4; ++f) for (int t = 0; t < 4; ++t) if (f != t) p[modId(f, t)] *= 0.8f; }
        if (has({ "bright", "shiny", "crystal", "sparkle", "glass" })) { mul("flt_cut", 2.2f); addM(0, 0.15f); }
        if (has({ "dirty", "distorted", "gritty", "grimy", "crunchy", "nasty" })) addM(2, 0.35f);
        if (has({ "clean", "pure", "smooth" })) { macro(2, 0.0f); macro(5, 0.0f); }
        if (has({ "wide", "huge", "big", "massive", "stereo" })) addM(1, 0.35f);
        if (has({ "narrow", "mono", "tight" })) macro(1, 0.0f);
        if (has({ "haunted", "spooky", "ghost", "ghostly", "creepy", "eerie", "possessed", "cursed" })) { addM(4, 0.3f); addM(3, 0.2f); mod(3, 0, juce::jmax(p[modId(3, 0)], 0.15f)); op(3, "ratio", 1.4142f); }
        if (has({ "glitch", "glitchy", "broken", "bitcrushed", "crushed", "8bit" })) addM(5, 0.55f);
        if (has({ "lofi", "lo-fi", "vintage", "tape", "dusty" })) { addM(5, 0.25f); mul("flt_cut", 0.55f); addM(3, 0.2f); }
        if (has({ "metallic", "metal", "chains", "iron", "clang" })) { op(1, "ratio", 3.5f); op(2, "ratio", 7.07f); addM(0, 0.2f); }
        if (has({ "warm", "soft", "mellow", "gentle", "round" })) { macro(2, p["macro_possess"] * 0.4f); mul("flt_cut", 0.7f); for (int o = 0; o < 4; ++o) p[opId(o, "wave")] *= 0.4f; }
        if (has({ "hard", "aggressive", "heavy", "angry", "brutal", "filthy" })) { addM(0, 0.25f); addM(2, 0.3f); }
        if (has({ "long", "endless", "huge" })) for (int o = 0; o < 4; ++o) p[opId(o, "r")] *= 2.5f;
        if (has({ "short", "tight", "stab", "staccato" })) for (int o = 0; o < 4; ++o) { p[opId(o, "d")] *= 0.4f; p[opId(o, "r")] *= 0.4f; }
        if (has({ "airy", "air", "spacey", "space", "dreamy", "ambient", "cinematic", "ethereal" })) { addM(4, 0.25f); mul("flt_cut", 1.3f); }
        if (has({ "dry" })) macro(4, 0.0f);
        if (has({ "slide", "gliding", "glide" })) { p["mono"] = 1; p["glide"] = juce::jmax(p["glide"], 0.12f); }
        if (has({ "fast" })) p["lfo_rate"] = p["lfo_rate"] * 2.5f;
        if (has({ "slow" })) p["lfo_rate"] = p["lfo_rate"] * 0.4f;
        if (has({ "low", "sub" }) && cat != "808" && cat != "sub") p["octave"] = juce::jmax(-2.0f, p["octave"] - 1);
        if (has({ "high" })) p["octave"] = juce::jmin(2.0f, p["octave"] + 1);

        // loudness balance per sound type (measured with ghost_render, aiming at about the same peak)
        static const std::map<juce::String, float> level { { "808", -1.0f }, { "sub", -1.0f }, { "bass", -7.0f }, { "growl", -6.0f }, { "wobble", -7.0f },
            { "reese", -6.0f }, { "bell", -5.0f }, { "pluck", 0.0f }, { "keys", -10.0f }, { "pad", -7.0f }, { "choir", -1.0f }, { "lead", -7.0f },
            { "brass", -10.0f }, { "riser", 2.0f }, { "arp", 0.0f } };
        p["master"] = level.count(cat) ? level.at(cat) : -6.0f;
        for (auto& kv : p) if (kv.first == "flt_cut") kv.second = juce::jlimit(40.0f, 20000.0f, kv.second);
        if (describedAs) *describedAs = cat;
        return p;
    }

    // Base patch: every parameter at its layout default.
    static Patch base() {
        static Patch b = [] {
            Patch out;
            auto layout = createLayout();
            struct Probe : juce::AudioProcessor {
                Probe(juce::AudioProcessorValueTreeState::ParameterLayout l) : apvts(*this, nullptr, "P", std::move(l)) {}
                const juce::String getName() const override { return "probe"; }
                void prepareToPlay(double, int) override {} void releaseResources() override {}
                void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
                double getTailLengthSeconds() const override { return 0; } bool acceptsMidi() const override { return true; }
                bool producesMidi() const override { return false; } juce::AudioProcessorEditor* createEditor() override { return nullptr; }
                bool hasEditor() const override { return false; } int getNumPrograms() override { return 1; } int getCurrentProgram() override { return 0; }
                void setCurrentProgram(int) override {} const juce::String getProgramName(int) override { return {}; } void changeProgramName(int, const juce::String&) override {}
                void getStateInformation(juce::MemoryBlock&) override {} void setStateInformation(const void*, int) override {}
                juce::AudioProcessorValueTreeState apvts;
            } probe(std::move(layout));
            for (auto* prm : probe.getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(prm))
                    out[rp->getParameterID()] = rp->convertFrom0to1(rp->getDefaultValue());
            // a neutral start: only ghost 1 sounds, no routes
            for (int f = 0; f < kOps; ++f) for (int t = 0; t < kOps; ++t) if (f != t) out[modId(f, t)] = 0.0f;
            for (int o = 1; o < kOps; ++o) out[opId(o, "level")] = 0.0f;
            out["macro_veil"] = 0.0f;
            return out;
        }();
        return b;
    }

    static juce::StringArray tokenize(const juce::String& q) {
        juce::StringArray w;
        w.addTokens(q.toLowerCase().replaceCharacters(",.!?/-_", "       "), " ", "");
        w.removeEmptyStrings();
        return w;
    }

    static juce::String categoryFor(const juce::StringArray& w) {
        static const std::vector<std::pair<juce::String, juce::StringArray>> map {
            { "808", { "808", "808s", "boom", "trap808" } },
            { "sub", { "sub", "subbass", "sine" } },
            { "wobble", { "wobble", "wub", "wubs", "dubstep", "riddim" } },
            { "growl", { "growl", "growls", "yoi", "talking", "tearout", "neuro", "monster", "screech" } },
            { "reese", { "reese", "dnb", "jungle", "drum&bass" } },
            { "bell", { "bell", "bells", "chime", "chimes", "glock", "music-box", "musicbox", "celesta", "trap" } },
            { "pluck", { "pluck", "plucks", "harp", "guitar", "pizz", "kalimba", "marimba" } },
            { "keys", { "keys", "key", "piano", "ep", "epiano", "rhodes", "organ", "wurli", "rnb", "r&b" } },
            { "pad", { "pad", "pads", "ambient", "cinematic", "texture", "drone", "atmosphere", "atmos", "strings", "string", "warmpad" } },
            { "choir", { "choir", "vox", "voice", "vocal", "ahh", "ooh", "ghost", "ghostly", "spirit", "spirits" } },
            { "lead", { "lead", "leads", "solo", "synthlead", "melody", "pop" } },
            { "brass", { "brass", "horn", "horns", "trumpet", "stab" } },
            { "riser", { "riser", "rise", "sweep", "fx", "uplifter", "transition", "whoosh" } },
            { "arp", { "arp", "arps", "sequence", "seq", "arpeggio" } },
            { "bass", { "bass", "basses", "bassline", "low" } },
        };
        // first word that names a category wins (so "dark trap bell" -> bell, not "trap")
        for (int pass = 0; pass < 2; ++pass)
            for (auto& word : w)
                for (auto& [cat, keys] : map) {
                    if (pass == 0 && (word == "trap" || word == "ghost" || word == "ghostly" || word == "low" || word == "pop")) continue;  // weak words only if nothing else matched
                    if (keys.contains(word)) return cat;
                }
        return {};
    }
};

struct FactoryPreset { const char* name; const char* words; uint32_t seed; };
inline const std::vector<FactoryPreset>& factoryPresets() {
    static const std::vector<FactoryPreset> v {
        { "808 Phantom", "808 dirty", 11 }, { "808 Slide Wraith", "808 slide hard", 23 }, { "Sub Abyss", "sub deep", 5 },
        { "Graveyard Bell", "dark bell haunted", 7 }, { "Ice Chime", "bright bell airy", 42 }, { "Trap Music Box", "trap bell", 3 },
        { "Possessed Growl", "growl heavy dirty", 13 }, { "Wobble Banshee", "wobble haunted", 9 }, { "Riddim Reaper", "riddim hard", 31 },
        { "Reese Specter", "reese dark", 4 }, { "Pluck of the Dead", "pluck haunted", 8 }, { "Ghost Keys", "keys warm", 2 },
        { "Haunted EP", "rhodes lofi", 17 }, { "Ectoplasm Pad", "pad haunted wide", 6 }, { "Cinematic Crypt", "cinematic dark pad long", 21 },
        { "Choir of Shadows", "choir dark", 12 }, { "Lead Wraith", "lead wide", 14 }, { "Brass Spirits", "brass", 15 },
        { "Riser from the Grave", "riser", 16 }, { "Lofi Ouija", "keys lofi dusty", 18 }, { "Glitch Poltergeist", "arp glitch", 19 },
        { "Metal Chains", "bell metallic dark", 20 }, { "Neon Arp", "arp bright", 22 }, { "R&B Velvet", "rnb keys soft", 24 },
        { "Pop Lead Halo", "pop lead bright", 25 }, { "Cursed Bass", "bass glitch dirty", 26 },
    };
    return v;
}

} // namespace gs
