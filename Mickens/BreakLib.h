#pragma once
// Mr. Mickens: the built-in break library. 20 classic break RHYTHMS (re-created, not sampled: drum patterns
// aren't copyrightable, recordings are) x 5 variations = 100 breaks, played on an original vintage break kit and
// run through "old record" processing (tape crunch, room, top-end roll-off, a little vinyl).
// Every break is 2 bars of audio at its own tempo with 32 equal slices (one per 16th), so it chops on the grid.
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <memory>
#include <random>
#include <vector>

namespace mk {

struct Break {
    juce::String name, family;
    std::vector<float> data;   // mono
    double rate = 48000, bpm = 100;
    int slices = 32;           // equal slices = 16ths
    bool user = false;
    int sliceLen() const { return juce::jmax(1, (int) data.size() / slices); }
};
using BreakPtr = std::shared_ptr<const Break>;

// lanes: K kick, S snare, G ghost snare, H closed hat, O open hat, R ride, C crash. 32 steps (2 bars of 16ths).
struct Rhythm { const char* name; double bpm; const char *K, *S, *G, *H, *O, *R, *C; };
inline const std::vector<Rhythm>& rhythms() {
    static const std::vector<Rhythm> r {
        { "Amen",            136, "x.x.......xx....x.x.......x.....", "....x..x.x..x..x....x..x.x....x.", "...............................x", "", "", "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.", "" },
        { "Amen End",        136, "x.x.......xx......xx......x.....", "....x..x.x..x..x.x..x..x.x....x.", "", "", "", "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x...", "..........................x....." },
        { "Think",           110, "x......x.x.x....x......x.x......", "....x.......x.......x.......x.x.", "..x.......x.......x....x........", "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x...", "..............................x.", "", "" },
        { "Apache",          118, "x.....x...x.....x.....x.x.......", "....x.......x.......x.......x...", "......x.......x.......x...x.....", "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", "", "", "" },
        { "Funky Drummer",   102, "x.x...x...x..x..x.x...x...x..x..", "....x..x.x.x...x....x..x.x.x...x", ".x....x......x...x....x......x..", "xxxxxxxxxxxxx.xxxxxxxxxxxxxxx.xx", ".............x...............x..", "", "" },
        { "Hot Pants",       115, "x..x..x...x..x..x..x..x...x..x..", "....x.......x..x....x.......x..x", "", "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", "", "", "" },
        { "Cold Sweat",      120, "x.....x.x.......x.....x.x.......", "....x..x....x.......x..x....x...", "", "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.", "", "", "" },
        { "Impeach",         96,  "x......xx.x.....x......x..x.....", "....x.......x.......x.......x...", "", "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.", "......x...............x.........", "", "" },
        { "Skull Snaps",     92,  "x...x..x..x.....x...x..x..x.....", "....x.......x.x.....x.......x.x.", "", "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.", "", "", "" },
        { "Roachclip",       100, "x.x...x....x....x.x...x....x....", "....x..x.x..x.......x..x.x..x...", "", "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", "", "", "" },
        { "Substitution",    104, "x..x..x...x.....x..x..x...x.....", "....x.......x..x....x.......x..x", "", "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.", "", "", "" },
        { "Soul Pride",      112, "x.....x.x..x....x.....x.x..x....", "....x.......x.......x.......x...", "..x...........x...x...........x.", "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", "", "", "" },
        { "Tighten Up",      108, "x.......x.x.....x.......x.x.....", "....x.......x.......x.......x...", "", "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.", "", "", "" },
        { "Mule",            98,  "x.x.....x.x..x..x.x.....x.x.....", "....x.......x.......x.......x..x", ".......x........x......x........", "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", "", "", "" },
        { "Ride Break",      124, "x.....x...x.....x.....x...x.....", "....x.......x.......x.......x...", "", "", "", "x.xxx.xxx.xxx.xxx.xxx.xxx.xxx.xx", "" },
        { "Jungle Roller",   170, "x.........x.....x.........x.....", "....x.......x.......x.......x...", "..x....x...x..x...x....x...x..x.", "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.", "", "", "" },
        { "Two Step",        172, "x.........x.......x.......x.....", "....x.......x.......x.......x...", ".......x.x.......x.....x.x......", "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", "", "", "" },
        { "Halftime",        86,  "x.........x.....x.....x...x.....", "........x...............x.......", "......x.......x.x.....x.......x.", "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.", "", "", "" },
        { "Rolling",         174, "x.....x.x.......x.....x.x.......", "....x.......x.......x.......x...", ".x..x.....x..x...x..x.....x..x..", "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", "", "", "" },
        { "Dub Break",       90,  "x.....x....x....x.....x....x....", "....x.......x.......x.......x...", "", "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.", "...........x...............x....", "", "" },
    };
    return r;
}
inline juce::StringArray variationNames() { return { "Original", "Ghosts", "Fill", "Dirty", "Room" }; }

class BreakLibrary {
public:
    const std::vector<BreakPtr>& breaks() const { return all; }

    void build(double sr = 48000.0) {
        all.clear();
        const auto& R = rhythms();
        for (int r = 0; r < (int) R.size(); ++r)
            for (int v = 0; v < 5; ++v) all.push_back(render(R[(size_t) r], v, sr, (uint32_t) (r * 31 + v * 7 + 1)));
    }

    static BreakPtr fromFile(const juce::File& f, double hostBpm, juce::String& err) {
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f));
        if (!r) { err = "Can't read that file (WAV, AIFF, MP3, FLAC)."; return nullptr; }
        const int n = (int) std::min<juce::int64>(r->lengthInSamples, (juce::int64) (r->sampleRate * 30));
        juce::AudioBuffer<float> b((int) juce::jmin(2u, r->numChannels), juce::jmax(1, n)); r->read(&b, 0, n, 0, true, true);
        auto br = std::make_shared<Break>(); br->name = f.getFileNameWithoutExtension(); br->family = "Your breaks"; br->user = true; br->rate = r->sampleRate;
        br->data.assign((size_t) n, 0.0f);
        for (int c = 0; c < b.getNumChannels(); ++c) for (int i = 0; i < n; ++i) br->data[(size_t) i] += b.getSample(c, i) / b.getNumChannels();
        float pk = 1e-6f; for (float x : br->data) pk = juce::jmax(pk, std::abs(x)); for (float& x : br->data) x *= 0.89f / pk;
        // guess how many bars: the count that puts the tempo nearest a normal break tempo (or the host's)
        const double secs = n / r->sampleRate; int bestBars = 1; double bestErr = 1e9;
        for (int bars : { 1, 2, 4, 8 }) { const double bpm = bars * 4 * 60.0 / secs; const double target = hostBpm > 60 ? hostBpm : 140.0;
            const double e = std::abs(std::log(bpm / target)); if (bpm >= 70 && bpm <= 190 && e < bestErr) { bestErr = e; bestBars = bars; } }
        br->bpm = bestBars * 4 * 60.0 / secs; br->slices = bestBars * 16;
        return br;
    }

private:
    std::vector<BreakPtr> all;

    // ---- the vintage break kit (original synthesis)
    static void kick(std::vector<float>& b, int at, float vel, double sr, double tune, std::mt19937& rng) {
        std::uniform_real_distribution<float> U(-1, 1); const int n = (int) (sr * 0.32); double ph = 0;
        for (int i = 0; i < n && at + i < (int) b.size(); ++i) { const double t = i / sr, f = (52 + 10 * tune) + 95 * std::exp(-t * 38); ph += juce::MathConstants<double>::twoPi * f / sr;
            b[(size_t) (at + i)] += vel * (float) (std::tanh(2.2 * std::sin(ph) * std::exp(-t * 11)) + U(rng) * std::exp(-t * 700) * 0.5); }
    }
    static void snare(std::vector<float>& b, int at, float vel, double sr, double tune, std::mt19937& rng) {
        std::uniform_real_distribution<float> U(-1, 1); const int n = (int) (sr * 0.28); float bp1 = 0, bp2 = 0, hp = 0, px = 0;
        const float f = (float) (2600 + 400 * tune), g = std::tan(juce::MathConstants<float>::pi * f / (float) sr), k = 1.2f, a1 = 1 / (1 + g * (g + k)), a2 = g * a1, a3 = g * a2;
        for (int i = 0; i < n && at + i < (int) b.size(); ++i) {
            const double t = i / sr; const float x = U(rng);
            const float v3 = x - bp2, v1 = a1 * bp1 + a2 * v3, v2 = bp2 + a2 * bp1 + a3 * v3; bp1 = 2 * v1 - bp1; bp2 = 2 * v2 - bp2;   // rattle band
            hp = 0.9f * (hp + x - px); px = x;
            const double body = (std::sin(juce::MathConstants<double>::twoPi * (190 + 20 * tune) * t) + 0.5 * std::sin(juce::MathConstants<double>::twoPi * 335 * t)) * std::exp(-t * 26);
            b[(size_t) (at + i)] += vel * (float) (body * 0.55 + v1 * 1.6 * std::exp(-t * 14) + hp * 0.5 * std::exp(-t * 60));
        }
    }
    static double metal(double t, double mul) { static const double fs[] { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 }; double v = 0; for (auto f : fs) v += std::sin(juce::MathConstants<double>::twoPi * f * 1.7 * mul * t) > 0 ? 1.0 : -1.0; return v / 6.0; }
    static void cym(std::vector<float>& b, int at, float vel, double sr, double len, double decay, double mul, double noiseMix, std::mt19937& rng) {
        std::uniform_real_distribution<float> U(-1, 1); const int n = (int) (sr * len); float hp = 0, px = 0;
        for (int i = 0; i < n && at + i < (int) b.size(); ++i) { const double t = i / sr; const float x = (float) ((metal(t, mul) * (1 - noiseMix) + U(rng) * noiseMix) * std::exp(-t * decay));
            hp = 0.94f * (hp + x - px); px = x; b[(size_t) (at + i)] += vel * hp * 0.6f; }
    }

    static BreakPtr render(const Rhythm& r, int variation, double sr, uint32_t seed) {
        std::mt19937 rng(seed); std::uniform_real_distribution<float> U(0, 1);
        auto br = std::make_shared<Break>(); br->family = r.name; br->name = juce::String(r.name) + " - " + variationNames()[variation];
        br->rate = sr; br->bpm = r.bpm; br->slices = 32;
        const double step = 60.0 / r.bpm / 4.0 * sr; const int n = (int) std::round(step * 32);
        std::vector<float> d((size_t) n + (int) (sr * 0.5), 0.0f);
        const double tune = (U(rng) - 0.5) * 2.0;
        auto lane = [&](const char* s, int i) { return s && (int) strlen(s) > i && s[i] != '.'; };
        for (int i = 0; i < 32; ++i) {
            const int jit = (int) ((U(rng) - 0.5f) * sr * 0.006);   // human timing
            const int at = juce::jmax(0, (int) std::round(i * step) + jit);
            const float vv = 0.85f + U(rng) * 0.15f;
            if (lane(r.K, i)) kick(d, at, vv, sr, tune, rng);
            if (lane(r.S, i)) snare(d, at, vv, sr, tune, rng);
            bool ghost = lane(r.G, i);
            if (variation == 1 && !lane(r.S, i) && !lane(r.K, i) && (i % 2 == 1) && U(rng) < 0.35f) ghost = true;
            if (ghost) snare(d, at, 0.28f, sr, tune, rng);
            if (variation == 2 && i >= 28) snare(d, at, 0.55f + 0.1f * (i - 28), sr, tune, rng);   // fill into the loop
            if (lane(r.H, i)) cym(d, at, (i % 2 ? 0.45f : 0.7f) * vv, sr, 0.09, 45, 1.0, 0.35, rng);
            if (lane(r.O, i) || (variation == 4 && i % 8 == 6)) cym(d, at, 0.55f, sr, 0.45, 7, 1.0, 0.4, rng);
            if (lane(r.R, i)) cym(d, at, (i % 2 ? 0.4f : 0.6f), sr, 0.6, 5, 1.9, 0.12, rng);
            if (lane(r.C, i) || (variation == 2 && i == 0)) cym(d, at, 0.7f, sr, 1.4, 2.2, 0.73, 0.6, rng);
        }
        // "old record": room reflections, tape saturation, top-end roll-off, a little vinyl
        const float room = variation == 4 ? 0.35f : 0.14f;
        std::vector<float> wet(d.size(), 0.0f);
        const int taps[] = { (int) (sr * 0.011), (int) (sr * 0.019), (int) (sr * 0.027), (int) (sr * 0.041), (int) (sr * 0.063) };
        const float tg[] = { 0.5f, 0.38f, 0.3f, 0.2f, 0.12f };
        for (size_t i = 0; i < d.size(); ++i) for (int k = 0; k < 5; ++k) if (i >= (size_t) taps[k]) wet[i] += d[i - (size_t) taps[k]] * tg[k];
        const float drive = variation == 3 ? 3.2f : 1.5f, lpA = variation == 3 ? 0.35f : 0.6f;
        float lp = 0, lp2 = 0;
        for (size_t i = 0; i < d.size(); ++i) {
            float x = d[i] + wet[i] * room;
            x = std::tanh(x * drive) / std::tanh(drive);
            lp += lpA * (x - lp); lp2 += lpA * (lp - lp2); x = lp2;
            if (U(rng) < 0.0004f) x += (U(rng) - 0.5f) * 0.25f;   // crackle
            x += (U(rng) - 0.5f) * 0.004f;                      // hiss
            if (variation == 3) x = std::round(x * 48.0f) / 48.0f;
            d[i] = x;
        }
        // fold the tail of bar 2 onto the start so the loop rings into itself, then cut to exactly 2 bars
        for (size_t i = (size_t) n; i < d.size(); ++i) d[i - (size_t) n] += d[i] * 0.6f;
        d.resize((size_t) n);
        float pk = 1e-6f; for (float x : d) pk = juce::jmax(pk, std::abs(x)); for (float& x : d) x *= 0.89f / pk;
        br->data = std::move(d);
        return br;
    }
};

} // namespace mk
