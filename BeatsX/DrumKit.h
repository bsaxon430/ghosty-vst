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
    std::vector<float> data;   // mono
    double rate = 44100;
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
    static SoundPtr fromFile(const juce::File& f, double rate, juce::String& err) {
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f));
        if (!r) { err = "Can't read that file (try WAV, AIFF, MP3 or FLAC)."; return nullptr; }
        const int n = (int) std::min<juce::int64>(r->lengthInSamples, (juce::int64) (r->sampleRate * 12));
        juce::AudioBuffer<float> b((int) juce::jmin(2u, r->numChannels), juce::jmax(1, n)); r->read(&b, 0, n, 0, true, true);
        std::vector<float> mono((size_t) n, 0.0f);
        for (int c = 0; c < b.getNumChannels(); ++c) for (int i = 0; i < n; ++i) mono[(size_t) i] += b.getSample(c, i) / b.getNumChannels();
        return fromMono(mono, r->sampleRate, rate, f.getFileNameWithoutExtension());
    }
    static SoundPtr fromMono(const std::vector<float>& mono, double srcRate, double rate, const juce::String& name) {
        int a = 0, z = (int) mono.size() - 1;
        while (a < z && std::abs(mono[(size_t) a]) < 0.002f) ++a;
        while (z > a && std::abs(mono[(size_t) z]) < 0.002f) --z;
        if (z - a < 32) return nullptr;
        auto s = std::make_shared<Sound>(); s->name = name; s->category = "Sample"; s->rate = rate;
        const double ratio = srcRate / rate; const int n = (int) ((z - a + 1) / ratio);
        s->data.resize((size_t) n);
        for (int i = 0; i < n; ++i) { const double p = a + i * ratio; const int k = (int) p; const float t = (float) (p - k); const float x0 = mono[(size_t) k], x1 = mono[(size_t) juce::jmin(k + 1, z)]; s->data[(size_t) i] = x0 + (x1 - x0) * t; }
        float mx = 1e-6f; for (auto v : s->data) mx = std::max(mx, std::abs(v));
        for (auto& v : s->data) v *= 0.89f / mx;
        for (int i = 0; i < 64 && i < n; ++i) { s->data[(size_t) i] *= (float) i / 64; s->data[(size_t) (n - 1 - i)] *= (float) i / 64; }
        return s;
    }

private:
    std::vector<SoundPtr> all;
    double rate = 44100;
};

} // namespace gb
