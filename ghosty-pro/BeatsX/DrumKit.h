#pragma once
// Ghosty Beats 2: the built-in drum library, synthesised at the engine's sample rate (no files needed),
// plus user samples dropped onto tracks. Every sound is a mono one-shot normalised to about -1 dB.
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <functional>
#include <memory>
#include <random>
#include <vector>

namespace gb {

struct Sound {
    juce::String name, category;
    std::vector<float> data;   // mono, or the left channel
    std::vector<float> right;  // the right channel of a stereo sample (empty = mono)
    double rate = 44100;
    double beats = 0;          // loops (Ghosty Beats breaks): length in beats, 0 = one-shot
    float trim = 1.0f;         // playback gain that loudness-matches the built-in sounds within their folder (data stays peak-normalised)
    bool stereo() const { return right.size() == data.size() && !right.empty(); }
};
using SoundPtr = std::shared_ptr<const Sound>;

class DrumLibrary {
public:
    static const juce::StringArray& categories() {
        static juce::StringArray c { "Kick", "808", "Snare", "Clap", "Hat", "Open Hat", "Perc", "Tom", "Cymbal", "FX" };
        return c;
    }
    const std::vector<SoundPtr>& sounds() const { return all; }
    int indexOf(const juce::String& name) const { for (int i = 0; i < (int) all.size(); ++i) if (all[(size_t) i]->name == name) return i; return -1; }
    SoundPtr byName(const juce::String& name) const { const int i = indexOf(name); return i >= 0 ? all[(size_t) i] : nullptr; }
    void append(SoundPtr s) { all.push_back(std::move(s)); }   // Ghosty Beats adds its big library after build()

    void build(double sr) {
        all.clear(); rate = sr;
        const double T = juce::MathConstants<double>::twoPi;
        std::mt19937 rng(4242);
        std::uniform_real_distribution<float> U(-1.0f, 1.0f);
        auto noise = [&] { return U(rng); };
        auto hp = [](std::vector<float>& b, float a) { float y = 0, px = 0; for (auto& x : b) { y = a * (y + x - px); px = x; x = y; } };
        auto lp = [](std::vector<float>& b, float a) { float y = 0; for (auto& x : b) { y += a * (x - y); x = y; } };
        auto bp = [&](std::vector<float>& b, double f, double q) {   // resonant band-pass (TPT SVF)
            const double g = std::tan(juce::MathConstants<double>::pi * juce::jmin(f, sr * 0.45) / sr), k = 1.0 / q, a1 = 1 / (1 + g * (g + k)), a2 = g * a1, a3 = g * a2;
            double s1 = 0, s2 = 0;
            for (auto& x : b) { const double v3 = x - s2, v1 = a1 * s1 + a2 * v3, v2 = s2 + a2 * s1 + a3 * v3; s1 = 2 * v1 - s1; s2 = 2 * v2 - s2; x = (float) v1; }
        };
        auto metal = [&](double t, double mul) {   // 6 square partials, the classic 808 hat/cymbal recipe
            static const double fs[] { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
            double v = 0; for (auto f : fs) v += std::sin(T * f * 1.7 * mul * t) > 0 ? 1.0 : -1.0; return v / 6.0;
        };
        auto add = [&](const char* cat, const char* nm, double len, const std::function<void(std::vector<float>&, int)>& fn) {
            auto s = std::make_shared<Sound>(); s->name = nm; s->category = cat; s->rate = sr;
            const int n = juce::jmax(64, (int) (sr * len)); s->data.assign((size_t) n, 0.0f); fn(s->data, n);
            float mx = 1e-6f; for (auto v : s->data) mx = std::max(mx, std::abs(v));
            for (auto& v : s->data) v *= 0.89f / mx;
            for (int i = 0; i < 96 && i < n; ++i) s->data[(size_t) (n - 1 - i)] *= (float) i / 96.0f;   // click-free tail
            all.push_back(s);
        };
        // kick: pitch sweep f1 -> f0, amp decay, click, saturation
        auto kick = [&](std::vector<float>& b, int n, double f0, double f1, double pk, double ak, double click, double sat, double glide = 0) {
            double ph = 0;
            for (int i = 0; i < n; ++i) {
                const double t = i / sr, f = f0 * (1 - glide * juce::jmin(1.0, t / 0.35)) + f1 * std::exp(-t * pk);
                ph += T * f / sr;
                b[(size_t) i] = (float) (std::tanh(sat * std::sin(ph) * std::exp(-t * ak)) + noise() * std::exp(-t * 900) * click);
            }
        };
        auto snare = [&](std::vector<float>& b, int n, double tone, double tk, double nk, double hpA, double mixTone) {
            std::vector<float> no((size_t) n); for (auto& x : no) x = noise(); hp(no, (float) hpA);
            for (int i = 0; i < n; ++i) { const double t = i / sr; b[(size_t) i] = (float) ((std::sin(T * tone * t) + 0.55 * std::sin(T * tone * 1.78 * t)) * std::exp(-t * tk) * mixTone + no[(size_t) i] * std::exp(-t * nk)); }
        };
        auto clap = [&](std::vector<float>& b, int n, double spread, double tail, int hits) {
            std::vector<float> no((size_t) n); for (auto& x : no) x = noise(); bp(no, 1200, 0.9);
            for (int i = 0; i < n; ++i) {
                const double t = i / sr; double e = 0;
                for (int h = 0; h < hits; ++h) { const double o = h * spread; if (t >= o) e = std::max(e, std::exp(-(t - o) * 160)); }
                const double last = (hits - 1) * spread; if (t > last) e = std::max(e, std::exp(-(t - last) * tail) * 0.6);
                b[(size_t) i] = (float) (no[(size_t) i] * e);
            }
        };
        auto hat = [&](std::vector<float>& b, int n, double mul, double k, double noiseMix, float hpA) {
            for (int i = 0; i < n; ++i) { const double t = i / sr; b[(size_t) i] = (float) ((metal(t, mul) * (1 - noiseMix) + noise() * noiseMix) * std::exp(-t * k)); }
            hp(b, hpA); hp(b, hpA);
        };
        auto tone = [&](std::vector<float>& b, int n, double f, double k, double bendTo = 0, double bendK = 30) {
            double ph = 0; for (int i = 0; i < n; ++i) { const double t = i / sr; ph += T * (f + bendTo * std::exp(-t * bendK)) / sr; b[(size_t) i] = (float) (std::sin(ph) * std::exp(-t * k)); }
        };

        // ---------------- KICK
        add("Kick", "Punch Kick", 0.45, [&](auto& b, int n) { kick(b, n, 52, 190, 48, 9, 0.6, 3); });
        add("Kick", "Boom Bap Kick", 0.5, [&](auto& b, int n) { kick(b, n, 48, 140, 35, 8, 0.8, 2.2); lp(b, 0.35f); });
        add("Kick", "House Kick", 0.5, [&](auto& b, int n) { kick(b, n, 50, 230, 55, 7, 0.35, 2.4); });
        add("Kick", "Techno Kick", 0.6, [&](auto& b, int n) { kick(b, n, 46, 260, 60, 6, 0.3, 4.5); });
        add("Kick", "Lofi Kick", 0.45, [&](auto& b, int n) { kick(b, n, 50, 120, 30, 9, 0.5, 2); lp(b, 0.12f); });
        add("Kick", "Hard Kick", 0.5, [&](auto& b, int n) { kick(b, n, 55, 320, 70, 8, 0.9, 6); });
        add("Kick", "Soft Kick", 0.4, [&](auto& b, int n) { kick(b, n, 55, 90, 30, 11, 0.1, 1.2); });
        add("Kick", "Sub Kick", 0.8, [&](auto& b, int n) { kick(b, n, 42, 70, 25, 4, 0.05, 1.3); });
        // ---------------- 808
        add("808", "808 Long", 1.6, [&](auto& b, int n) { kick(b, n, 45, 110, 28, 1.6, 0.4, 1.6); });
        add("808", "808 Short", 0.7, [&](auto& b, int n) { kick(b, n, 45, 110, 28, 5, 0.4, 1.6); });
        add("808", "808 Dirty", 1.4, [&](auto& b, int n) { kick(b, n, 46, 130, 30, 1.8, 0.5, 5.5); });
        add("808", "808 Glide", 1.6, [&](auto& b, int n) { kick(b, n, 55, 100, 25, 1.7, 0.3, 2, 0.35); });
        add("808", "808 Boom", 1.8, [&](auto& b, int n) { kick(b, n, 55, 60, 40, 1.5, 0, 2.2); });
        add("808", "808 Punchy", 1.0, [&](auto& b, int n) { kick(b, n, 48, 260, 60, 2.6, 0.7, 2.8); });
        // ---------------- SNARE
        add("Snare", "Trap Snare", 0.35, [&](auto& b, int n) { snare(b, n, 200, 26, 16, 0.88, 0.5); });
        add("Snare", "Boom Bap Snare", 0.45, [&](auto& b, int n) { snare(b, n, 180, 20, 11, 0.8, 0.7); lp(b, 0.45f); });
        add("Snare", "Drill Snare", 0.3, [&](auto& b, int n) { snare(b, n, 240, 30, 20, 0.9, 0.45); });
        add("Snare", "Lofi Snare", 0.4, [&](auto& b, int n) { snare(b, n, 190, 22, 13, 0.82, 0.6); lp(b, 0.18f); });
        add("Snare", "Tight Snare", 0.2, [&](auto& b, int n) { snare(b, n, 260, 40, 34, 0.9, 0.4); });
        add("Snare", "Big Snare", 0.7, [&](auto& b, int n) { snare(b, n, 170, 14, 6, 0.75, 0.6); });
        add("Snare", "Rim Snare", 0.25, [&](auto& b, int n) { snare(b, n, 420, 45, 30, 0.92, 0.9); });
        add("Snare", "Rim", 0.12, [&](auto& b, int n) { tone(b, n, 1700, 60); for (int i = 0; i < n; ++i) b[(size_t) i] += (float) (noise() * std::exp(-i / sr * 120) * 0.4); bp(b, 1800, 2); });
        // ---------------- CLAP
        add("Clap", "Clap", 0.5, [&](auto& b, int n) { clap(b, n, 0.011, 14, 3); });
        add("Clap", "Trap Clap", 0.45, [&](auto& b, int n) { clap(b, n, 0.009, 18, 4); });
        add("Clap", "Big Clap", 0.8, [&](auto& b, int n) { clap(b, n, 0.014, 7, 4); });
        add("Clap", "Snap", 0.2, [&](auto& b, int n) { clap(b, n, 0.004, 40, 2); bp(b, 2400, 1.5); });
        // ---------------- HAT
        add("Hat", "Hat", 0.12, [&](auto& b, int n) { hat(b, n, 1, 55, 0.4, 0.95f); });
        add("Hat", "Trap Hat", 0.07, [&](auto& b, int n) { hat(b, n, 1.25, 85, 0.35, 0.96f); });
        add("Hat", "Tight Hat", 0.05, [&](auto& b, int n) { hat(b, n, 1.4, 130, 0.3, 0.97f); });
        add("Hat", "Lofi Hat", 0.1, [&](auto& b, int n) { hat(b, n, 0.9, 60, 0.6, 0.93f); lp(b, 0.3f); });
        add("Hat", "Shaker Hat", 0.12, [&](auto& b, int n) { for (int i = 0; i < n; ++i) { const double t = i / sr; b[(size_t) i] = (float) (noise() * std::min(1.0, t * 200) * std::exp(-t * 35)); } hp(b, 0.96f); });
        add("Hat", "Pedal Hat", 0.16, [&](auto& b, int n) { hat(b, n, 0.95, 32, 0.45, 0.94f); });
        // ---------------- OPEN HAT
        add("Open Hat", "Open Hat", 0.7, [&](auto& b, int n) { hat(b, n, 1, 6, 0.4, 0.95f); });
        add("Open Hat", "Short Open Hat", 0.35, [&](auto& b, int n) { hat(b, n, 1.1, 12, 0.4, 0.95f); });
        add("Open Hat", "Long Open Hat", 1.2, [&](auto& b, int n) { hat(b, n, 0.95, 3.2, 0.45, 0.95f); });
        // ---------------- PERC
        add("Perc", "Cowbell", 0.4, [&](auto& b, int n) { for (int i = 0; i < n; ++i) { const double t = i / sr; b[(size_t) i] = (float) (((std::sin(T * 540 * t) > 0 ? 1 : -1) + (std::sin(T * 800 * t) > 0 ? 1 : -1)) * std::exp(-t * 9) * 0.5); } bp(b, 700, 1.4); });
        add("Perc", "Shaker", 0.18, [&](auto& b, int n) { for (int i = 0; i < n; ++i) { const double t = i / sr; b[(size_t) i] = (float) (noise() * std::sin(juce::MathConstants<double>::pi * juce::jmin(1.0, t / 0.18))); } hp(b, 0.97f); });
        add("Perc", "Tambourine", 0.3, [&](auto& b, int n) { for (int i = 0; i < n; ++i) { const double t = i / sr; b[(size_t) i] = (float) ((metal(t, 2.2) * 0.5 + noise() * 0.5) * std::exp(-t * 14)); } hp(b, 0.97f); });
        add("Perc", "Conga High", 0.3, [&](auto& b, int n) { tone(b, n, 340, 14, 90); });
        add("Perc", "Conga Low", 0.4, [&](auto& b, int n) { tone(b, n, 210, 11, 70); });
        add("Perc", "Bongo", 0.2, [&](auto& b, int n) { tone(b, n, 480, 22, 160, 60); });
        add("Perc", "Wood Block", 0.12, [&](auto& b, int n) { tone(b, n, 1100, 45); bp(b, 1100, 6); });
        add("Perc", "Clave", 0.1, [&](auto& b, int n) { tone(b, n, 2500, 50); });
        add("Perc", "Triangle", 0.9, [&](auto& b, int n) { for (int i = 0; i < n; ++i) { const double t = i / sr; b[(size_t) i] = (float) ((std::sin(T * 4100 * t) + 0.5 * std::sin(T * 6240 * t)) * std::exp(-t * 4)); } });
        add("Perc", "Perc Pop", 0.15, [&](auto& b, int n) { tone(b, n, 700, 30, 900, 90); });
        add("Perc", "Tick", 0.04, [&](auto& b, int n) { for (int i = 0; i < n; ++i) b[(size_t) i] = (float) (noise() * std::exp(-i / sr * 250)); bp(b, 5000, 2); });
        add("Perc", "Water Drop", 0.2, [&](auto& b, int n) { tone(b, n, 900, 18, -700, 25); });
        // ---------------- TOM
        add("Tom", "Tom High", 0.4, [&](auto& b, int n) { tone(b, n, 240, 9, 120, 25); });
        add("Tom", "Tom Mid", 0.5, [&](auto& b, int n) { tone(b, n, 170, 7.5, 90, 22); });
        add("Tom", "Tom Low", 0.7, [&](auto& b, int n) { tone(b, n, 110, 6, 60, 20); });
        // ---------------- CYMBAL
        add("Cymbal", "Crash", 1.8, [&](auto& b, int n) { for (int i = 0; i < n; ++i) { const double t = i / sr; b[(size_t) i] = (float) ((metal(t, 0.73) * 0.4 + noise() * 0.6) * std::exp(-t * 1.9)); } hp(b, 0.9f); });
        add("Cymbal", "Ride", 1.5, [&](auto& b, int n) { for (int i = 0; i < n; ++i) { const double t = i / sr; b[(size_t) i] = (float) ((metal(t, 1.9) * 0.7 + noise() * 0.15) * std::exp(-t * 2.4) + std::sin(T * 3200 * t) * std::exp(-t * 6) * 0.3); } hp(b, 0.92f); });
        add("Cymbal", "Reverse Crash", 1.8, [&](auto& b, int n) { for (int i = 0; i < n; ++i) { const double t = (n - 1 - i) / sr; b[(size_t) i] = (float) ((metal(t, 0.73) * 0.4 + noise() * 0.6) * std::exp(-t * 1.9)); } hp(b, 0.9f); });
        // ---------------- FX
        add("FX", "Zap", 0.3, [&](auto& b, int n) { tone(b, n, 120, 9, 2400, 18); });
        add("FX", "Laser", 0.4, [&](auto& b, int n) { double ph = 0; for (int i = 0; i < n; ++i) { const double t = i / sr; ph += T * (3000 * std::exp(-t * 10) + 200) / sr; b[(size_t) i] = (float) ((std::sin(ph) > 0 ? 1 : -1) * std::exp(-t * 6) * 0.6); } lp(b, 0.4f); });
        add("FX", "Blip", 0.12, [&](auto& b, int n) { tone(b, n, 1320, 30); });
        add("FX", "Glitch", 0.25, [&](auto& b, int n) { float hold = 0; for (int i = 0; i < n; ++i) { if (i % 300 == 0) hold = noise(); b[(size_t) i] = hold * (float) std::exp(-i / sr * 10); } });
        add("FX", "Swell", 1.2, [&](auto& b, int n) { std::vector<float> no((size_t) n); for (auto& x : no) x = noise(); bp(no, 900, 1.5); for (int i = 0; i < n; ++i) { const double t = (double) i / n; b[(size_t) i] = no[(size_t) i] * (float) (t * t); } });
        add("FX", "Riser", 2.0, [&](auto& b, int n) { double ph = 0; for (int i = 0; i < n; ++i) { const double t = (double) i / n; ph += T * (200 + 1800 * t * t) / sr; b[(size_t) i] = (float) ((std::sin(ph) * 0.5 + noise() * 0.3 * t) * t); } });
        add("FX", "Vinyl Crackle", 1.0, [&](auto& b, int n) { std::uniform_real_distribution<float> P(0, 1); for (int i = 0; i < n; ++i) b[(size_t) i] = (P(rng) < 0.0015f ? noise() : 0.0f) + noise() * 0.02f; lp(b, 0.5f); });
        add("FX", "Ghost Vox", 0.35, [&](auto& b, int n) {   // a short "hey"-like formant chop
            double ph = 0; for (int i = 0; i < n; ++i) { const double t = i / sr; ph += T * (180 + 20 * std::sin(t * 30)) / sr; b[(size_t) i] = (float) ((2 * (ph / T - std::floor(ph / T)) - 1) * std::min(1.0, t * 80) * std::exp(-t * 7)); }
            std::vector<float> a = b, c = b; bp(a, 700, 6); bp(c, 1700, 8); for (int i = 0; i < n; ++i) b[(size_t) i] = a[(size_t) i] + c[(size_t) i] * 0.6f; });
    }

    // a dropped audio file -> a Sound at the library's rate (mono, trimmed, normalised, max 12 s)
    static SoundPtr fromFile(const juce::File& f, double rate, juce::String& err, bool normalise = true) {
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f));
        if (!r) { err = "Can't read that file (try WAV, AIFF, MP3 or FLAC)."; return nullptr; }
        const int n = (int) std::min<juce::int64>(r->lengthInSamples, (juce::int64) (r->sampleRate * 12));
        juce::AudioBuffer<float> b((int) juce::jmin(2u, r->numChannels), juce::jmax(1, n)); r->read(&b, 0, n, 0, true, true);
        std::vector<float> left((size_t) n, 0.0f), rightCh;
        for (int i = 0; i < n; ++i) left[(size_t) i] = b.getSample(0, i);
        if (b.getNumChannels() == 2) { rightCh.resize((size_t) n); bool same = true; for (int i = 0; i < n; ++i) { rightCh[(size_t) i] = b.getSample(1, i); if (same && std::abs(left[(size_t) i] - rightCh[(size_t) i]) > 1e-4f) same = false; } if (same) rightCh.clear(); }   // dual mono stays mono
        else if (b.getNumChannels() > 2) for (int c = 1; c < b.getNumChannels(); ++c) for (int i = 0; i < n; ++i) left[(size_t) i] += b.getSample(c, i) / b.getNumChannels();
        return fromMono(left, r->sampleRate, rate, f.getFileNameWithoutExtension(), normalise, rightCh.empty() ? nullptr : &rightCh);
    }
    // mono (or L + R: both channels trimmed / resampled / normalised together, so they stay aligned)
    static SoundPtr fromMono(const std::vector<float>& mono, double srcRate, double rate, const juce::String& name, bool normalise = true, const std::vector<float>* rightIn = nullptr) {
        const std::vector<float>* chs[2] { &mono, rightIn && rightIn->size() == mono.size() ? rightIn : nullptr };
        int a = 0, z = (int) mono.size() - 1;
        auto loud = [&](int i) { float m = std::abs(mono[(size_t) i]); if (chs[1]) m = std::max(m, std::abs((*chs[1])[(size_t) i])); return m; };
        while (a < z && loud(a) < 0.002f) ++a;
        while (z > a && loud(z) < 0.002f) --z;
        if (z - a < 32) return nullptr;
        auto s = std::make_shared<Sound>(); s->name = name; s->category = "Sample"; s->rate = rate;
        const double ratio = srcRate / rate; const int n = (int) ((z - a + 1) / ratio);
        float mx = 1e-6f;
        for (int c = 0; c < 2; ++c) { if (!chs[c]) continue; auto& dst = c == 0 ? s->data : s->right; const auto& src = *chs[c]; dst.resize((size_t) n);
            for (int i = 0; i < n; ++i) { const double p = a + i * ratio; const int k = (int) p; const float t = (float) (p - k); const float x0 = src[(size_t) k], x1 = src[(size_t) juce::jmin(k + 1, z)]; dst[(size_t) i] = x0 + (x1 - x0) * t; }
            for (auto v : dst) mx = std::max(mx, std::abs(v)); }
        for (int c = 0; c < 2; ++c) { if (!chs[c]) continue; auto& dst = c == 0 ? s->data : s->right;
            if (normalise) for (auto& v : dst) v *= 0.89f / mx; else if (mx > 1.0f) for (auto& v : dst) v /= mx;
            for (int i = 0; i < 64 && i < n; ++i) { dst[(size_t) i] *= (float) i / 64; dst[(size_t) (n - 1 - i)] *= (float) i / 64; } }
        return s;
    }

private:
    std::vector<SoundPtr> all;
    double rate = 44100;
};

// ---------------- the DRILL pack: appended after the whole Beats library (never cached, never part of the classic 61 or the
// cached set), so every older sound, trim and index stays exactly as it was. Names all start with "DRILL ".
inline bool isDrillPackSound(const juce::String& name) { return name.startsWith("DRILL "); }
inline const juce::StringArray& drillPackNames() {
    static juce::StringArray n { "DRILL 808 LONG", "DRILL 808 SLIDE", "DRILL 808 KNOCK", "DRILL KICK", "DRILL SNARE", "DRILL CLAP", "DRILL HAT", "DRILL OPEN HAT", "DRILL METAL PERC", "DRILL 808 DIRTY", "DRILL SNARE DIRTY" };
    return n;
}
inline void appendDrillSounds(DrumLibrary& lib, double sr) {
    const double T = juce::MathConstants<double>::twoPi;
    uint32_t rs = 0x2b5d1e37u;
    auto noise = [&] { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return (float) (rs & 0xffffff) / 8388608.0f - 1.0f; };
    struct Svf { double s1 = 0, s2 = 0, a1 = 0, a2 = 0, a3 = 0, k = 1;   // TPT state-variable filter: 0 low-pass, 1 band-pass, 2 high-pass
        void set(double f, double q, double rate) { const double g = std::tan(juce::MathConstants<double>::pi * juce::jlimit(10.0, rate * 0.45, f) / rate); k = 1.0 / q; a1 = 1 / (1 + g * (g + k)); a2 = g * a1; a3 = g * a2; }
        double tick(double x, int type) { const double v3 = x - s2, v1 = a1 * s1 + a2 * v3, v2 = s2 + a2 * s1 + a3 * v3; s1 = 2 * v1 - s1; s2 = 2 * v2 - s2; return type == 0 ? v2 : (type == 1 ? v1 : x - k * v1 - v2); } };
    auto filt = [&](std::vector<float>& b, int type, double f, double q = 0.707) { Svf s; s.set(f, q, sr); for (auto& x : b) x = (float) s.tick(x, type); };
    auto room = [&](std::vector<float>& b, double mix) {   // a short, tight room: four early reflections, darkened
        static const double tap[] { 0.0071, 0.0113, 0.0167, 0.0239 }, g[] { 0.42, 0.3, 0.21, 0.13 };
        std::vector<float> dry = b, wet(b.size(), 0.0f);
        for (int k = 0; k < 4; ++k) { const int d = (int) (tap[k] * sr); for (size_t i = (size_t) d; i < b.size(); ++i) wet[i] += dry[i - (size_t) d] * (float) g[k] * ((k & 1) ? -1.0f : 1.0f); }
        filt(wet, 0, 5200); for (size_t i = 0; i < b.size(); ++i) b[i] = dry[i] + wet[i] * (float) mix;
    };
    auto metal = [&](double t, double mul) { static const double fs[] { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 }; double v = 0; for (auto f : fs) v += std::sin(T * f * 1.7 * mul * t) > 0 ? 1.0 : -1.0; return v / 6.0; };
    auto add = [&](const char* cat, const char* nm, double len, const std::function<void(std::vector<float>&, int)>& fn) {
        if (lib.indexOf(nm) >= 0) return;
        auto s = std::make_shared<Sound>(); s->name = nm; s->category = cat; s->rate = sr;
        const int n = juce::jmax(64, (int) (sr * len)); s->data.assign((size_t) n, 0.0f); fn(s->data, n);
        { const float c = (float) std::exp(-T * 10.0 / sr); float px = 0, py = 0; for (auto& v : s->data) { const float y = v - px + c * py; px = v; py = y; v = y; } }   // DC block
        float mx = 1e-6f; for (auto& v : s->data) { if (!std::isfinite(v)) v = 0; mx = std::max(mx, std::abs(v)); }
        for (auto& v : s->data) v *= 0.89f / mx;
        const int fade = juce::jmin(n / 4, (int) (sr * 0.006)); for (int i = 0; i < fade; ++i) s->data[(size_t) (n - 1 - i)] *= (float) i / (float) fade;   // click-free end
        lib.append(s);
    };
    // The drill 808: a G1 sine (+ a little 2nd harmonic so it reads on small speakers), pitch envelope +semis falling over ~22 ms,
    // a long exponential decay, a short click, then multiband drive (the sub below 90 Hz stays clean, the band above is driven
    // with a soft clip + a touch of tube asymmetry), a 12 dB/oct low-pass after the drive, and a 28 Hz high-pass.
    struct B8 { double f0 = 49.0, penv = 9, penvMs = 22, decay = 2.6, h2 = 0.18, click = 0.12, drive = 0.38, lp = 1600, growl = 0; };   // growl: the DIRTY 808's extra low-mid band (0 = off)
    auto bass808 = [&](std::vector<float>& b, int n, const B8& P) {
        double ph = 0; const double tau = P.penvMs * 0.001 / 2.3, dk = 6.9 / P.decay;   // pitch env ~90 % gone after penvMs; amp -60 dB at `decay`
        std::vector<float> cl((size_t) n, 0.0f); for (int i = 0; i < juce::jmin(n, (int) (0.004 * sr)); ++i) cl[(size_t) i] = noise() * (float) std::exp(-i / sr * 900.0);
        filt(cl, 2, 1800);
        for (int i = 0; i < n; ++i) {
            const double t = i / sr, f = P.f0 * std::exp2(P.penv / 12.0 * std::exp(-t / tau));
            ph += T * f / sr;
            const double amp = std::exp(-t * dk) * juce::jmin(1.0, t / 0.0012);
            b[(size_t) i] = (float) ((std::sin(ph) + P.h2 * std::sin(2 * ph)) * amp + cl[(size_t) i] * P.click);
        }
        std::vector<float> lo = b; filt(lo, 0, 90, 0.6);
        const double dg = 1.0 + P.drive * 6.0;
        for (int i = 0; i < n; ++i) {
            const double x = b[(size_t) i] * dg, y = x >= 0 ? std::tanh(x) : std::tanh(x * 0.8) / 0.8;   // soft clip + tube asymmetry
            b[(size_t) i] = (float) (y / std::tanh(dg));
        }
        filt(b, 2, 90, 0.6);
        if (P.growl > 0) {   // a second, harder stage on 120-700 Hz only (the low-mid growl); the sub below 90 Hz never sees it
            std::vector<float> gw = b; filt(gw, 2, 120, 0.7); filt(gw, 0, 700, 0.7); const double g2 = 1.0 + P.growl * 8.0;
            for (auto& x : gw) { const double y = x * g2; x = (float) ((y >= 0 ? std::tanh(y) : std::tanh(y * 0.7) / 0.7) / std::tanh(g2) * P.growl); }
            for (int i = 0; i < n; ++i) b[(size_t) i] += gw[(size_t) i];
        }
        for (int i = 0; i < n; ++i) b[(size_t) i] = b[(size_t) i] * (float) (0.3 + P.drive * 0.9) + lo[(size_t) i];   // the growl over the clean sub
        filt(b, 0, P.lp, 0.707); filt(b, 2, 28, 0.707);
    };
    add("808", "DRILL 808 LONG", 3.0, [&](auto& b, int n) { bass808(b, n, B8 {}); });
    add("808", "DRILL 808 SLIDE", 3.6, [&](auto& b, int n) { B8 P; P.penv = 7; P.penvMs = 20; P.decay = 3.2; P.h2 = 0.22; P.click = 0.08; P.drive = 0.42; P.lp = 1500; bass808(b, n, P); });
    add("808", "DRILL 808 KNOCK", 2.2, [&](auto& b, int n) { B8 P; P.penv = 12; P.penvMs = 26; P.decay = 1.8; P.click = 0.4; P.drive = 0.4; P.lp = 2400;   // 808-as-kick: the click at about -8 dB
                                                               bass808(b, n, P); });
    // DRILL KICK: a short knock that sits ABOVE the 808 (high-passed at 110 Hz, two poles), so the two never fight in the sub
    add("Kick", "DRILL KICK", 0.3, [&](auto& b, int n) {
        double ph = 0;
        for (int i = 0; i < n; ++i) { const double t = i / sr, f = 96 + 250 * std::exp(-t / 0.009); ph += T * f / sr;
            b[(size_t) i] = (float) (std::tanh(2.2 * std::sin(ph) * std::exp(-t * 15)) + noise() * std::exp(-t * 1500) * 0.3); }
        filt(b, 2, 115, 0.75); filt(b, 2, 115, 0.6); filt(b, 2, 90, 0.7); filt(b, 0, 9000);
        float mx = 1e-6f; for (auto v : b) mx = std::max(mx, std::abs(v)); for (auto& v : b) v = (float) (std::tanh(3.0 * v / mx) / std::tanh(3.0));   // a knock with body, not one spike
    });
    // DRILL SNARE: tight and bright, a short room
    add("Snare", "DRILL SNARE", 0.34, [&](auto& b, int n) {
        std::vector<float> no((size_t) n); for (auto& x : no) x = noise(); filt(no, 2, 1900, 0.8); filt(no, 1, 6500, 0.5);
        for (int i = 0; i < n; ++i) { const double t = i / sr;
            b[(size_t) i] = (float) ((std::sin(T * 228 * t) + 0.5 * std::sin(T * 372 * t)) * std::exp(-t * 24) * 0.6 + no[(size_t) i] * (std::exp(-t * 15) * 0.9 + std::exp(-t * 260) * 0.25)); }
        room(b, 0.35);
        float mx = 1e-6f; for (auto v : b) mx = std::max(mx, std::abs(v)); for (auto& v : b) v = (float) (std::tanh(2.5 * v / mx) / std::tanh(2.5));   // tight: the crack glued to the body
        filt(b, 0, 14000, 0.6);
    });
    // DRILL CLAP: three quick bursts, band-limited, the same short room
    add("Clap", "DRILL CLAP", 0.42, [&](auto& b, int n) {
        std::vector<float> no((size_t) n); for (auto& x : no) x = noise(); filt(no, 1, 1450, 1.1); filt(no, 2, 700);
        for (int i = 0; i < n; ++i) { const double t = i / sr; double e = 0;
            for (int h = 0; h < 3; ++h) { const double o = h * 0.0085; if (t >= o) e = std::max(e, std::exp(-(t - o) * 190)); }
            if (t > 0.017) e = std::max(e, std::exp(-(t - 0.017) * 17) * 0.62);
            b[(size_t) i] = (float) (no[(size_t) i] * e); }
        room(b, 0.3); filt(b, 0, 14000, 0.6);
    });
    // DRILL HAT closed / open: crisp metal + air, high-passed twice
    add("Hat", "DRILL HAT", 0.09, [&](auto& b, int n) {
        for (int i = 0; i < n; ++i) { const double t = i / sr; b[(size_t) i] = (float) ((metal(t, 1.36) * 0.6 + noise() * 0.4) * std::exp(-t * 72) * juce::jmin(1.0, t / 0.0004)); }
        filt(b, 2, 7200, 0.7); filt(b, 2, 6000, 0.6); filt(b, 0, 15000, 0.6); filt(b, 0, 15000, 0.6);   // crisp, but band-limited (no inter-sample overs)
    });
    add("Open Hat", "DRILL OPEN HAT", 0.62, [&](auto& b, int n) {
        for (int i = 0; i < n; ++i) { const double t = i / sr; b[(size_t) i] = (float) ((metal(t, 1.31) * 0.55 + noise() * 0.45) * std::exp(-t * 7.5) * juce::jmin(1.0, t / 0.0006)); }
        filt(b, 2, 6800, 0.7); filt(b, 2, 5600, 0.6); filt(b, 0, 15000, 0.6); filt(b, 0, 15000, 0.6);
    });
    // DRILL METAL PERC: an inharmonic clank (struck-metal partials, each with its own decay) and a tick
    add("Perc", "DRILL METAL PERC", 0.45, [&](auto& b, int n) {
        static const double fr[] { 612, 947, 1361, 1874, 2531 }, dk[] { 11, 15, 19, 26, 34 }, gn[] { 1.0, 0.7, 0.55, 0.4, 0.28 };
        for (int i = 0; i < n; ++i) { const double t = i / sr; double v = 0; for (int k = 0; k < 5; ++k) v += std::sin(T * fr[k] * t + k) * std::exp(-t * dk[k]) * gn[k];
            b[(size_t) i] = (float) (v + noise() * std::exp(-t * 600) * 0.6); }
        filt(b, 2, 380);
    });
    // DRILL 808 DIRTY (GRIT): the same G1 sub, kept clean below 90 Hz, but driven much harder above it plus a low-mid growl stage,
    // a brighter post-drive low-pass and a longer tail (4 s to -60 dB)
    add("808", "DRILL 808 DIRTY", 4.2, [&](auto& b, int n) { B8 P; P.penv = 10; P.penvMs = 24; P.decay = 4.0; P.h2 = 0.16; P.click = 0.1; P.drive = 0.5; P.lp = 2000; P.growl = 0.22; bass808(b, n, P); });
    // DRILL SNARE DIRTY (GRIT): the snare and a clap layered in one hit, a short BRIGHT room, a touch of asymmetric distortion
    add("Snare", "DRILL SNARE DIRTY", 0.42, [&](auto& b, int n) {
        std::vector<float> no((size_t) n), cl((size_t) n); for (auto& x : no) x = noise(); for (auto& x : cl) x = noise();
        filt(no, 2, 1700, 0.8); filt(no, 1, 6000, 0.5); filt(cl, 1, 1350, 1.0); filt(cl, 2, 650);
        for (int i = 0; i < n; ++i) { const double t = i / sr; double e = 0;
            for (int h = 0; h < 3; ++h) { const double o = 0.002 + h * 0.008; if (t >= o) e = std::max(e, std::exp(-(t - o) * 180)); }
            if (t > 0.018) e = std::max(e, std::exp(-(t - 0.018) * 15) * 0.6);
            b[(size_t) i] = (float) ((std::sin(T * 214 * t) + 0.55 * std::sin(T * 351 * t)) * std::exp(-t * 22) * 0.65 + no[(size_t) i] * (std::exp(-t * 13) * 0.85 + std::exp(-t * 240) * 0.3) + cl[(size_t) i] * e * 0.8); }
        {   // the short bright room: five early reflections, opened up to 9 kHz
            static const double tap[] { 0.0053, 0.0089, 0.0131, 0.0187, 0.0263 }, gn[] { 0.45, 0.34, 0.25, 0.17, 0.1 };
            std::vector<float> dry = b, wet(b.size(), 0.0f);
            for (int k = 0; k < 5; ++k) { const int d = (int) (tap[k] * sr); for (size_t i = (size_t) d; i < b.size(); ++i) wet[i] += dry[i - (size_t) d] * (float) gn[k] * ((k & 1) ? -1.0f : 1.0f); }
            filt(wet, 0, 9000); filt(wet, 2, 400); for (size_t i = 0; i < b.size(); ++i) b[i] = dry[i] + wet[i] * 0.45f;
        }
        float mx = 1e-6f; for (auto v : b) mx = std::max(mx, std::abs(v));
        for (auto& v : b) { const double x = 3.2 * v / mx; v = (float) ((x >= 0 ? std::tanh(x) : std::tanh(x * 0.75) / 0.75) / std::tanh(3.2)); }   // the dirt: asymmetric soft clip
        filt(b, 0, 13000, 0.6); filt(b, 0, 15000, 0.6);
    });
}

// ---------------- the LATIN pack (the REGGAETON style's kit): appended after the DRILL pack, never cached, never part of the classic 61
// or the cached set, levelled TO the folder medians (never moves them). Our own synthesis: damped tuned shells + slap / strike noise.
inline const juce::StringArray& latinPackNames() {
    static juce::StringArray n { "REGGAETON KICK", "DEMBOW SNARE", "LATIN RIMSHOT", "TIMBALE HI", "TIMBALE LO", "TIMBALE RIM", "LATIN CONGA HI", "LATIN CONGA LO",
                                 "LATIN BONGO HI", "LATIN BONGO LO", "LATIN SHAKER", "LATIN CLAVE", "LATIN COWBELL", "BRIGHT CLAP" };
    return n;
}
inline bool isLatinPackSound(const juce::String& name) { return latinPackNames().contains(name); }
// ---------------- the CLASSIC-CHARACTER packs (docs/research/DRUM-SOUNDS-STUDY.md; ClassicPacks.h synthesises them): appended after the
// LATIN pack, in this order, never cached, levelled TO the folder medians. Our own synthesis of a machine's / an era's character,
// generic names only (no brand, model or producer names).
inline const juce::StringArray& classicPackNames() {
    static juce::StringArray n {
        // ANALOG BOOM (8-series character)
        "ANALOG BOOM KICK", "ANALOG BOOM LONG", "ANALOG BOOM SNARE", "ANALOG BOOM CLAP", "ANALOG BOOM CLOSED HAT", "ANALOG BOOM OPEN HAT", "ANALOG BOOM CYMBAL",
        "ANALOG BOOM COWBELL", "ANALOG BOOM TOM LO", "ANALOG BOOM TOM MID", "ANALOG BOOM TOM HI", "ANALOG BOOM CONGA LO", "ANALOG BOOM CONGA MID", "ANALOG BOOM CONGA HI",
        "ANALOG BOOM CLAVES", "ANALOG BOOM RIMSHOT", "ANALOG BOOM MARACAS",
        // ANALOG PUNCH (9-series character)
        "ANALOG PUNCH KICK", "ANALOG PUNCH KICK LONG", "ANALOG PUNCH SNARE", "ANALOG PUNCH CLAP", "ANALOG PUNCH CLOSED HAT", "ANALOG PUNCH OPEN HAT", "ANALOG PUNCH RIDE",
        "ANALOG PUNCH CRASH", "ANALOG PUNCH TOM LO", "ANALOG PUNCH TOM MID", "ANALOG PUNCH TOM HI", "ANALOG PUNCH RIMSHOT",
        // DIGITAL EIGHTIES (8-bit sampled-era character)
        "DIGITAL EIGHTIES KICK", "DIGITAL EIGHTIES SNARE", "DIGITAL EIGHTIES CLAP", "DIGITAL EIGHTIES CLOSED HAT", "DIGITAL EIGHTIES OPEN HAT", "DIGITAL EIGHTIES CABASA",
        "DIGITAL EIGHTIES TAMBOURINE", "DIGITAL EIGHTIES TOM LO", "DIGITAL EIGHTIES TOM MID", "DIGITAL EIGHTIES TOM HI", "DIGITAL EIGHTIES CONGA HI", "DIGITAL EIGHTIES CONGA LO",
        "DIGITAL EIGHTIES COWBELL", "DIGITAL EIGHTIES CLAVES", "DIGITAL EIGHTIES RIM",
        // CRUNCH TWELVE (12-bit 26 kHz sampler character)
        "CRUNCH TWELVE KICK", "CRUNCH TWELVE KICK OPEN", "CRUNCH TWELVE SNARE", "CRUNCH TWELVE SNARE FILTERED", "CRUNCH TWELVE CLAP", "CRUNCH TWELVE HAT",
        "CRUNCH TWELVE OPEN HAT", "CRUNCH TWELVE RIM", "CRUNCH TWELVE SHAKER", "CRUNCH TWELVE TOM",
        // SWING TWELVE (12-bit 40 kHz production-centre character)
        "SWING TWELVE KICK", "SWING TWELVE SNARE", "SWING TWELVE CLAP", "SWING TWELVE HAT", "SWING TWELVE OPEN HAT", "SWING TWELVE RIM",
        "SWING TWELVE SHAKER", "SWING TWELVE TOM", "SWING TWELVE CONGA",
        // TRAP GOLD / TEXAS BOUNCE / BOOM BAP CLASSIC / POP SHINE (modern processed kits)
        "TRAP GOLD KICK", "TRAP GOLD 808 D", "TRAP GOLD 808 F", "TRAP GOLD 808 G", "TRAP GOLD 808 A", "TRAP GOLD SNARE", "TRAP GOLD CLAP", "TRAP GOLD CLOSED HAT", "TRAP GOLD OPEN HAT",
        "TRAP GOLD RIMSHOT", "TRAP GOLD BELL",
        "TEXAS BOUNCE KICK", "TEXAS BOUNCE 808", "TEXAS BOUNCE CLAP", "TEXAS BOUNCE SNARE", "TEXAS BOUNCE HAT", "TEXAS BOUNCE OPEN HAT", "TEXAS BOUNCE SNAP", "TEXAS BOUNCE COWBELL",
        "BOOM BAP CLASSIC KICK", "BOOM BAP CLASSIC SNARE", "BOOM BAP CLASSIC HAT", "BOOM BAP CLASSIC OPEN HAT", "BOOM BAP CLASSIC RIM", "BOOM BAP CLASSIC SHAKER",
        "POP SHINE KICK", "POP SHINE SNARE", "POP SHINE CLAP", "POP SHINE HAT", "POP SHINE OPEN HAT", "POP SHINE SHAKER", "POP SHINE SNARE GATED",
        // TRAP CLASSIC (the early-2010s orchestral trap kit)
        "BOOM 808", "TRAP KICK", "TRAP CLASSIC SNARE", "TRAP CLASSIC CLAP", "TRAP HAT CLOSED", "TRAP HAT OPEN", "TRAP CLASSIC CRASH", "BRASS HIT", "TIMP TOM",
    };
    return n;
}
inline bool isClassicPackSound(const juce::String& name) { return classicPackNames().contains(name); }
inline bool isPackSound(const juce::String& name) { return isDrillPackSound(name) || isLatinPackSound(name) || isClassicPackSound(name); }   // appended packs: levelled to, never part of, the folder medians
inline void appendLatinSounds(DrumLibrary& lib, double sr) {
    const double T = juce::MathConstants<double>::twoPi;
    uint32_t rs = 0x5ea1c0d3u;
    auto noise = [&] { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return (float) (rs & 0xffffff) / 8388608.0f - 1.0f; };
    struct Svf { double s1 = 0, s2 = 0, a1 = 0, a2 = 0, a3 = 0, k = 1;   // TPT state-variable filter: 0 low-pass, 1 band-pass, 2 high-pass
        void set(double f, double q, double rate) { const double g = std::tan(juce::MathConstants<double>::pi * juce::jlimit(10.0, rate * 0.45, f) / rate); k = 1.0 / q; a1 = 1 / (1 + g * (g + k)); a2 = g * a1; a3 = g * a2; }
        double tick(double x, int type) { const double v3 = x - s2, v1 = a1 * s1 + a2 * v3, v2 = s2 + a2 * s1 + a3 * v3; s1 = 2 * v1 - s1; s2 = 2 * v2 - s2; return type == 0 ? v2 : (type == 1 ? v1 : x - k * v1 - v2); } };
    auto filt = [&](std::vector<float>& b, int type, double f, double q = 0.707) { Svf s; s.set(f, q, sr); for (auto& x : b) x = (float) s.tick(x, type); };
    auto room = [&](std::vector<float>& b, double mix, double bright) {   // a short room: four early reflections
        static const double tap[] { 0.0061, 0.0097, 0.0149, 0.0211 }, g[] { 0.4, 0.29, 0.2, 0.12 };
        std::vector<float> dry = b, wet(b.size(), 0.0f);
        for (int k = 0; k < 4; ++k) { const int d = (int) (tap[k] * sr); for (size_t i = (size_t) d; i < b.size(); ++i) wet[i] += dry[i - (size_t) d] * (float) g[k] * ((k & 1) ? -1.0f : 1.0f); }
        filt(wet, 0, bright); for (size_t i = 0; i < b.size(); ++i) b[i] = dry[i] + wet[i] * (float) mix;
    };
    auto add = [&](const char* cat, const char* nm, double len, const std::function<void(std::vector<float>&, int)>& fn) {
        if (lib.indexOf(nm) >= 0) return;
        auto s = std::make_shared<Sound>(); s->name = nm; s->category = cat; s->rate = sr;
        const int n = juce::jmax(64, (int) (sr * len)); s->data.assign((size_t) n, 0.0f); fn(s->data, n);
        filt(s->data, 0, 16000, 0.6); filt(s->data, 0, 16000, 0.6);   // band-limited: no inter-sample overs from the bright strikes
        { const float c = (float) std::exp(-T * 10.0 / sr); float px = 0, py = 0; for (auto& v : s->data) { const float y = v - px + c * py; px = v; py = y; v = y; } }   // DC block
        float mx = 1e-6f; for (auto& v : s->data) { if (!std::isfinite(v)) v = 0; mx = std::max(mx, std::abs(v)); }
        for (auto& v : s->data) v *= 0.89f / mx;
        const int fade = juce::jmin(n / 4, (int) (sr * 0.004)); for (int i = 0; i < fade; ++i) s->data[(size_t) (n - 1 - i)] *= (float) i / (float) fade;   // click-free end
        lib.append(s);
    };
    // a struck hand drum: a damped sine whose pitch drops a little after the strike (the skin settling), a weaker overtone,
    // and the slap: a short band of noise. f0 Hz, k amp decay /s, bend Hz (falls over ~bendMs), slap level + its centre
    auto hand = [&](std::vector<float>& b, int n, double f0, double k, double bend, double bendMs, double over, double slap, double slapHz) {
        std::vector<float> sl((size_t) n, 0.0f); for (int i = 0; i < juce::jmin(n, (int) (0.03 * sr)); ++i) sl[(size_t) i] = noise() * (float) std::exp(-i / sr * 140.0);
        filt(sl, 1, slapHz, 1.1); filt(sl, 2, 900);
        double ph = 0, ph2 = 0;
        for (int i = 0; i < n; ++i) { const double t = i / sr, f = f0 + bend * std::exp(-t * 1000.0 / bendMs); ph += T * f / sr; ph2 += T * f * 1.52 / sr;
            const double a = std::exp(-t * k) * juce::jmin(1.0, t / 0.0008);
            b[(size_t) i] = (float) ((std::sin(ph) + over * std::sin(ph2) * std::exp(-t * k * 0.8)) * a + sl[(size_t) i] * slap); }
    };
    // a timbale: a thin metal-shelled drum: a tuned head with inharmonic shell partials ringing longer, and a stick strike
    auto timbale = [&](std::vector<float>& b, int n, double f0, double k) {
        static const double pr[] { 1.0, 1.59, 2.14, 2.92, 3.5 }, gk[] { 1.0, 0.55, 0.42, 0.3, 0.2 }, dk[] { 1.0, 0.75, 0.9, 1.3, 1.6 };
        std::vector<float> st((size_t) n, 0.0f); for (int i = 0; i < juce::jmin(n, (int) (0.012 * sr)); ++i) st[(size_t) i] = noise() * (float) std::exp(-i / sr * 420.0);
        filt(st, 2, 2500); filt(st, 0, 12000);
        for (int i = 0; i < n; ++i) { const double t = i / sr, fb = f0 * (1.0 + 0.03 * std::exp(-t * 60)); double v = 0;
            for (int p = 0; p < 5; ++p) v += std::sin(T * fb * pr[p] * t + p) * std::exp(-t * k * dk[p]) * gk[p];
            b[(size_t) i] = (float) (v * juce::jmin(1.0, t / 0.0005) + st[(size_t) i] * 0.7); }
        filt(b, 2, f0 * 0.6);
    };
    // REGGAETON KICK: round and punchy, short (a quick sweep into a ~55 Hz body, gently rounded by a soft clip)
    add("Kick", "REGGAETON KICK", 0.34, [&](auto& b, int n) {
        double ph = 0;
        for (int i = 0; i < n; ++i) { const double t = i / sr, f = 54 + 170 * std::exp(-t / 0.011); ph += T * f / sr;
            b[(size_t) i] = (float) (std::tanh(2.6 * std::sin(ph) * std::exp(-t * 12.5)) + noise() * std::exp(-t * 1300) * 0.22); }
        filt(b, 0, 9000); filt(b, 2, 30);
    });
    // DEMBOW SNARE: bright and cracky, short, a small bright room
    add("Snare", "DEMBOW SNARE", 0.28, [&](auto& b, int n) {
        std::vector<float> no((size_t) n); for (auto& x : no) x = noise(); filt(no, 2, 2300, 0.8); filt(no, 1, 7200, 0.6);
        for (int i = 0; i < n; ++i) { const double t = i / sr;
            b[(size_t) i] = (float) ((std::sin(T * 252 * t) + 0.6 * std::sin(T * 418 * t)) * std::exp(-t * 34) * 0.55 + no[(size_t) i] * (std::exp(-t * 24) * 0.8 + std::exp(-t * 380) * 0.45)); }
        room(b, 0.3, 8500);
        float mx = 1e-6f; for (auto v : b) mx = std::max(mx, std::abs(v)); for (auto& v : b) v = (float) (std::tanh(3.4 * v / mx) / std::tanh(3.4));   // cracky, and the peak glued to the body
        filt(b, 0, 15000, 0.6);
    });
    // LATIN RIMSHOT: stick on rim and head together: a hard ping (two shell partials) + a short crack
    add("Snare", "LATIN RIMSHOT", 0.2, [&](auto& b, int n) {
        std::vector<float> no((size_t) n); for (auto& x : no) x = noise(); filt(no, 1, 3600, 1.2);
        for (int i = 0; i < n; ++i) { const double t = i / sr;
            b[(size_t) i] = (float) ((std::sin(T * 1720 * t) * 0.8 + std::sin(T * 530 * t) * 0.7) * std::exp(-t * 40) + std::sin(T * 338 * t) * std::exp(-t * 15) * 0.95 + no[(size_t) i] * (std::exp(-t * 200) * 0.9 + std::exp(-t * 30) * 0.25)); }
        room(b, 0.3, 9000);
        float mx = 1e-6f; for (auto v : b) mx = std::max(mx, std::abs(v)); for (auto& v : b) v = (float) (std::tanh(2.6 * v / mx) / std::tanh(2.6));   // the crack glued to the ring
        filt(b, 2, 280); filt(b, 0, 14000, 0.6);
    });
    add("Perc", "TIMBALE HI", 0.55, [&](auto& b, int n) { timbale(b, n, 545, 8.5); });
    add("Perc", "TIMBALE LO", 0.6, [&](auto& b, int n) { timbale(b, n, 382, 7.5); });
    // TIMBALE RIM: the stick on the metal shell (cascara): high, dry, bright
    add("Perc", "TIMBALE RIM", 0.12, [&](auto& b, int n) {
        static const double fr[] { 2240, 3410, 5130 }, gn[] { 1.0, 0.6, 0.35 };
        for (int i = 0; i < n; ++i) { const double t = i / sr; double v = 0; for (int k = 0; k < 3; ++k) v += std::sin(T * fr[k] * t + k) * gn[k] * std::exp(-t * (60 + 25 * k));
            b[(size_t) i] = (float) (v + noise() * std::exp(-t * 900) * 0.6); }
        filt(b, 2, 1100); filt(b, 0, 15000, 0.6);
    });
    add("Perc", "LATIN CONGA HI", 0.38, [&](auto& b, int n) { hand(b, n, 332, 13, 42, 22, 0.25, 0.32, 2600); });
    add("Perc", "LATIN CONGA LO", 0.46, [&](auto& b, int n) { hand(b, n, 222, 10, 30, 26, 0.22, 0.24, 2100); });
    add("Perc", "LATIN BONGO HI", 0.22, [&](auto& b, int n) { hand(b, n, 640, 24, 85, 14, 0.3, 0.3, 3600); });
    add("Perc", "LATIN BONGO LO", 0.28, [&](auto& b, int n) { hand(b, n, 438, 18, 60, 16, 0.28, 0.26, 3000); });
    // LATIN SHAKER: seeds in a gourd: a soft swell in, a quick fall out, all air
    add("Perc", "LATIN SHAKER", 0.13, [&](auto& b, int n) {
        for (int i = 0; i < n; ++i) { const double t = i / sr; b[(size_t) i] = (float) (noise() * juce::jmin(1.0, t / 0.009) * std::exp(-std::max(0.0, t - 0.009) * 48)); }
        filt(b, 2, 4800, 0.7); filt(b, 1, 8200, 0.6); filt(b, 0, 15000, 0.6);
    });
    // LATIN CLAVE: two hardwood sticks: one bright tuned knock
    add("Perc", "LATIN CLAVE", 0.12, [&](auto& b, int n) {
        for (int i = 0; i < n; ++i) { const double t = i / sr; b[(size_t) i] = (float) ((std::sin(T * 2480 * t) + 0.2 * std::sin(T * 6900 * t) * std::exp(-t * 120)) * std::exp(-t * 46) * juce::jmin(1.0, t / 0.0003) + noise() * std::exp(-t * 2500) * 0.25); }
        filt(b, 2, 900); filt(b, 0, 12000, 0.6);
    });
    // LATIN COWBELL: two square partials through a body band-pass, a quick drop then a short ring
    add("Perc", "LATIN COWBELL", 0.36, [&](auto& b, int n) {
        for (int i = 0; i < n; ++i) { const double t = i / sr; const double sq = (std::sin(T * 562 * t) > 0 ? 1.0 : -1.0) + 0.8 * (std::sin(T * 845 * t) > 0 ? 1.0 : -1.0);
            b[(size_t) i] = (float) (sq * (std::exp(-t * 32) * 0.75 + std::exp(-t * 8) * 0.25)); }
        filt(b, 1, 720, 1.6); filt(b, 1, 1150, 0.9); filt(b, 0, 9000);
    });
    // BRIGHT CLAP: four quick bursts, opened up top, a small bright room
    add("Clap", "BRIGHT CLAP", 0.36, [&](auto& b, int n) {
        std::vector<float> no((size_t) n), air((size_t) n); for (auto& x : no) x = noise(); for (auto& x : air) x = noise();
        filt(no, 1, 1850, 0.9); filt(no, 2, 900); filt(air, 2, 5200, 0.7);
        for (int i = 0; i < n; ++i) { const double t = i / sr; double e = 0;
            for (int h = 0; h < 4; ++h) { const double o = h * 0.0072; if (t >= o) e = std::max(e, std::exp(-(t - o) * 210)); }
            if (t > 0.0216) e = std::max(e, std::exp(-(t - 0.0216) * 21) * 0.6);
            b[(size_t) i] = (float) ((no[(size_t) i] + air[(size_t) i] * 0.35) * e); }
        room(b, 0.28, 9500); filt(b, 0, 15000, 0.6);
    });
}

} // namespace gb
