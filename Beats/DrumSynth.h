#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <cmath>
#include <functional>

// The Ghosty drum sounds, synthesised at the host sample rate (same recipes as the web app).
namespace ghosty
{
struct DrumSound
{
    juce::String name;
    std::vector<float> fwd, rev;
    float gain = 1.0f;
};

class DrumBank
{
public:
    static const juce::StringArray& names()
    {
        static juce::StringArray n { "808 Kick", "Punch Kick", "808 Boom", "Snare", "Clap", "Hat", "Open Hat", "Tom",
                                     "Rim", "Cowbell", "Shaker", "Zap", "Blip", "Glitch", "Crash", "Swell" };
        return n;
    }

    void build (double sr)
    {
        if (std::abs (sr - builtRate) < 1.0 && ! sounds.empty()) return;
        builtRate = sr;
        sounds.clear();
        const double T = juce::MathConstants<double>::twoPi;
        juce::Random rnd (1234);
        auto noise = [&] { return rnd.nextFloat() * 2.0f - 1.0f; };
        auto metal = [&] (double t, double mul)
        {
            static const double fs[] { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
            double v = 0; for (auto f : fs) v += std::sin (T * f * 1.7 * mul * t) > 0 ? 1.0 : -1.0; return v / 6.0;
        };
        auto hp = [] (std::vector<float>& b, float a) { float y = 0, px = 0; for (auto& x : b) { y = a * (y + x - px); px = x; x = y; } };
        auto lp = [] (std::vector<float>& b, float a) { float y = 0; for (auto& x : b) { y += a * (x - y); x = y; } };
        auto make = [&] (const char* nm, double len, float gain, const std::function<void (std::vector<float>&, int)>& fn)
        {
            DrumSound s; s.name = nm; s.gain = gain;
            const int n = (int) (sr * len); s.fwd.assign ((size_t) n, 0.0f); fn (s.fwd, n);
            float mx = 1e-6f; for (auto v : s.fwd) mx = std::max (mx, std::abs (v));
            for (auto& v : s.fwd) v /= mx * 1.05f;
            for (int i = 0; i < 64 && i < n; ++i) s.fwd[(size_t) (n - 1 - i)] *= (float) i / 64.0f;   // click-free tail
            s.rev.assign (s.fwd.rbegin(), s.fwd.rend());
            sounds.push_back (std::move (s));
        };
        make ("808 Kick", 0.9, 1.1f, [&] (auto& b, int n) { double ph = 0; for (int i = 0; i < n; ++i) { const double t = i / sr, f = 45 + 110 * std::exp (-t * 28); ph += T * f / sr; b[(size_t) i] = (float) (std::tanh (1.6 * std::sin (ph) * std::exp (-t * 3.4)) + noise() * std::exp (-t * 900) * 0.4); } });
        make ("Punch Kick", 0.45, 1.0f, [&] (auto& b, int n) { double ph = 0; for (int i = 0; i < n; ++i) { const double t = i / sr, f = 52 + 190 * std::exp (-t * 48); ph += T * f / sr; b[(size_t) i] = (float) (std::tanh (3 * std::sin (ph) * std::exp (-t * 9)) + noise() * std::exp (-t * 600) * 0.6); } });
        make ("808 Boom", 1.8, 1.0f, [&] (auto& b, int n) { double ph = 0; for (int i = 0; i < n; ++i) { const double t = i / sr, f = 55 + 60 * std::exp (-t * 40); ph += T * f / sr; b[(size_t) i] = (float) (std::tanh (2.2 * std::sin (ph)) * std::exp (-t * 1.5)); } });
        make ("Snare", 0.4, 0.9f, [&] (auto& b, int n) { std::vector<float> no ((size_t) n); for (auto& x : no) x = noise(); hp (no, 0.85f);
            for (int i = 0; i < n; ++i) { const double t = i / sr; b[(size_t) i] = (float) ((std::sin (T * 185 * t) + 0.6 * std::sin (T * 330 * t)) * std::exp (-t * 22) * 0.6 + no[(size_t) i] * std::exp (-t * 13)); } });
        make ("Clap", 0.5, 0.9f, [&] (auto& b, int n) { std::vector<float> no ((size_t) n); for (auto& x : no) x = noise(); hp (no, 0.7f); lp (no, 0.5f);
            for (int i = 0; i < n; ++i) { const double t = i / sr; double e = 0; for (double o : { 0.0, 0.011, 0.022 }) if (t >= o) e = std::max (e, std::exp (-(t - o) * 180)); if (t > 0.022) e = std::max (e, std::exp (-(t - 0.022) * 14) * 0.55); b[(size_t) i] = (float) (no[(size_t) i] * e); } });
        make ("Hat", 0.12, 0.55f, [&] (auto& b, int n) { for (int i = 0; i < n; ++i) { const double t = i / sr; b[(size_t) i] = (float) ((metal (t, 1) * 0.6 + noise() * 0.4) * std::exp (-t * 55)); } hp (b, 0.95f); });
        make ("Open Hat", 0.7, 0.5f, [&] (auto& b, int n) { for (int i = 0; i < n; ++i) { const double t = i / sr; b[(size_t) i] = (float) ((metal (t, 1) * 0.6 + noise() * 0.4) * std::exp (-t * 6)); } hp (b, 0.95f); });
        make ("Tom", 0.6, 0.9f, [&] (auto& b, int n) { double ph = 0; for (int i = 0; i < n; ++i) { const double t = i / sr, f = 110 + 70 * std::exp (-t * 20); ph += T * f / sr; b[(size_t) i] = (float) (std::sin (ph) * std::exp (-t * 6)); } });
        make ("Rim", 0.08, 0.6f, [&] (auto& b, int n) { for (int i = 0; i < n; ++i) { const double t = i / sr; b[(size_t) i] = (float) ((std::sin (T * 1700 * t) + noise() * 0.5) * std::exp (-t * 80)); } });
        make ("Cowbell", 0.4, 0.45f, [&] (auto& b, int n) { for (int i = 0; i < n; ++i) { const double t = i / sr; b[(size_t) i] = (float) (((std::sin (T * 540 * t) > 0 ? 1 : -1) + (std::sin (T * 800 * t) > 0 ? 1 : -1)) * std::exp (-t * 10)); } lp (b, 0.25f); });
        make ("Shaker", 0.15, 0.45f, [&] (auto& b, int n) { for (int i = 0; i < n; ++i) { const double t = i / sr; b[(size_t) i] = (float) (noise() * std::min (1.0, t * 120) * std::exp (-t * 30)); } hp (b, 0.9f); });
        make ("Zap", 0.3, 0.7f, [&] (auto& b, int n) { double ph = 0; for (int i = 0; i < n; ++i) { const double t = i / sr, f = 80 + 2400 * std::exp (-t * 35); ph += T * f / sr; b[(size_t) i] = (float) (std::sin (ph) * std::exp (-t * 10)); } });
        make ("Blip", 0.2, 0.6f, [&] (auto& b, int n) { for (int i = 0; i < n; ++i) { const double t = i / sr; b[(size_t) i] = (float) (std::sin (T * 880 * t) * std::exp (-t * 25)); } });
        make ("Glitch", 0.18, 0.5f, [&] (auto& b, int n) { for (int i = 0; i < n; ++i) { const double t = i / sr; const int step = ((int) (t * 60)) % 3; b[(size_t) i] = (float) (std::round ((noise() * 0.6 + std::sin (T * 220 * t * (1 + step))) * 3) / 3 * std::exp (-t * 18)); } });
        make ("Crash", 2.0, 0.45f, [&] (auto& b, int n) { for (int i = 0; i < n; ++i) { const double t = i / sr; b[(size_t) i] = (float) ((metal (t, 1.3) * 0.5 + noise() * 0.7) * std::exp (-t * 2.2)); } hp (b, 0.93f); });
        make ("Swell", 1.2, 0.45f, [&] (auto& b, int n) { for (int i = 0; i < n; ++i) { const double t = i / sr; b[(size_t) i] = (float) (noise() * std::pow (t / 1.2, 3)); } hp (b, 0.9f); });
    }

    const DrumSound& get (int i) const { return sounds[(size_t) juce::jlimit (0, (int) sounds.size() - 1, i)]; }
    bool ready() const { return ! sounds.empty(); }

private:
    std::vector<DrumSound> sounds;
    double builtRate = 0;
};
} // namespace ghosty
