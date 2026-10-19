#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>
#include <memory>

// Wavetable frames (the waves you draw) and band-limited mip tables built from them.
namespace ghosty
{
constexpr int FRAME = 256;
constexpr int TABLE = 2048;
constexpr int MIPS  = 11;                  // one table per octave from 20 Hz up
using Frame = std::array<float, FRAME>;

inline const juce::StringArray& shapeNames()
{
    static juce::StringArray s { "Sine", "Triangle", "Saw", "Square", "Pulse", "Organ", "Glass", "Growl", "Digital", "Vox", "Ahh", "Ooh", "Sync", "Fold", "Hollow", "Bright" };
    return s;
}

inline void normaliseFrame (Frame& f)
{
    float mean = 0; for (auto v : f) mean += v; mean /= (float) FRAME;
    float mx = 1e-9f; for (auto& v : f) { v -= mean; mx = std::max (mx, std::abs (v)); }
    for (auto& v : f) v /= mx;
}

inline Frame makeShape (int shape)
{
    Frame f {};
    const double tau = juce::MathConstants<double>::twoPi;
    for (int i = 0; i < FRAME; ++i)
    {
        const double p = (double) i / FRAME;
        double v = 0;
        switch (shape)
        {
            case 0: v = std::sin (tau * p); break;
            case 1: v = 1.0 - 4.0 * std::abs (std::fmod (p + 0.25, 1.0) - 0.5); break;
            case 2: v = 2.0 * p - 1.0; break;
            case 3: v = p < 0.5 ? 1.0 : -1.0; break;
            case 4: v = p < 0.18 ? 1.0 : -1.0; break;
            case 5: v = std::sin (tau * p) + 0.5 * std::sin (2 * tau * p) + 0.32 * std::sin (3 * tau * p) + 0.2 * std::sin (4 * tau * p) + 0.14 * std::sin (8 * tau * p); break;
            case 6: v = std::sin (tau * p) + 0.5 * std::sin (5 * tau * p) + 0.32 * std::sin (9 * tau * p) + 0.2 * std::sin (13 * tau * p); break;
            case 7: v = std::sin (tau * p + 2.6 * std::sin (2 * tau * p)); break;
            case 8: v = std::round (std::sin (tau * p) * 3.0) / 3.0; break;
            case 9:
                for (int k = 1; k <= 48; ++k)
                {
                    const double fr = k * 130.0;
                    const double a = std::exp (-std::pow ((fr - 650) / 220, 2)) + 0.7 * std::exp (-std::pow ((fr - 1100) / 260, 2))
                                   + 0.35 * std::exp (-std::pow ((fr - 2500) / 420, 2)) + 0.12 / k;
                    v += a * std::sin (tau * k * p);
                }
                break;
            case 10: case 11:
            {
                const double f1 = shape == 10 ? 800 : 300, f2 = shape == 10 ? 1150 : 870, f3 = shape == 10 ? 2900 : 2240;
                for (int k = 1; k <= 48; ++k)
                {
                    const double fr = k * 130.0;
                    const double a = std::exp (-std::pow ((fr - f1) / 180, 2)) + 0.6 * std::exp (-std::pow ((fr - f2) / 220, 2))
                                   + 0.25 * std::exp (-std::pow ((fr - f3) / 380, 2)) + 0.08 / k;
                    v += a * std::sin (tau * k * p);
                }
                break;
            }
            case 12: { const double q = std::fmod (p * 2.6, 1.0); v = 2.0 * q - 1.0; break; }
            case 13: v = std::sin (3.2 * std::sin (tau * p)); break;
            case 14: for (int k = 1; k <= 15; k += 2) v += std::sin (tau * k * p) / (double) (k * k); v += 0.35 * std::sin (2 * tau * p); break;
            case 15: for (int k = 1; k <= 40; ++k) v += std::sin (tau * k * p) / std::sqrt ((double) k); break;
            default: break;
        }
        f[(size_t) i] = (float) v;
    }
    normaliseFrame (f);
    return f;
}

struct TrigTables { std::vector<double> c, s; };
inline const TrigTables& trig()
{
    static TrigTables t = []
    {
        TrigTables x; x.c.resize (TABLE); x.s.resize (TABLE);
        for (int n = 0; n < TABLE; ++n) { const double ph = juce::MathConstants<double>::twoPi * n / TABLE; x.c[(size_t) n] = std::cos (ph); x.s[(size_t) n] = std::sin (ph); }
        return x;
    }();
    return t;
}

// One frame -> MIPS band-limited tables (harmonics above Nyquist removed per octave).
struct MipSet
{
    std::array<std::vector<float>, MIPS> t;
    void build (const Frame& f, double sampleRate)
    {
        constexpr int H = 128;
        std::array<double, H + 1> re {}, im {};
        const double tau = juce::MathConstants<double>::twoPi;
        for (int k = 1; k <= H; ++k)
        {
            double r = 0, s = 0;
            for (int n = 0; n < FRAME; ++n) { r += f[(size_t) n] * std::cos (tau * k * n / FRAME); s += f[(size_t) n] * std::sin (tau * k * n / FRAME); }
            re[(size_t) k] = r * 2.0 / FRAME; im[(size_t) k] = s * 2.0 / FRAME;
        }
        for (int m = 0; m < MIPS; ++m)
        {
            const double topFreq = 40.0 * std::pow (2.0, m);              // highest note this table serves
            const int maxH = juce::jlimit (1, H, (int) (sampleRate * 0.45 / topFreq));
            auto& tab = t[(size_t) m];
            tab.assign (TABLE + 1, 0.0f);
            for (int k = 1; k <= maxH; ++k)
            {
                const double a = re[(size_t) k], b = im[(size_t) k];
                if (std::abs (a) + std::abs (b) < 1e-6) continue;
                const auto& cs = trig();
                for (int n = 0; n < TABLE; ++n)
                {
                    const int idx = (k * n) & (TABLE - 1);
                    tab[(size_t) n] += (float) (a * cs.c[(size_t) idx] + b * cs.s[(size_t) idx]);
                }
            }
            tab[TABLE] = tab[0];
        }
    }
    static int mipFor (double freq) { return juce::jlimit (0, MIPS - 1, (int) std::floor (std::log2 (std::max (20.0, freq) / 20.0))); }
    float read (int mip, double phase) const
    {
        const auto& tab = t[(size_t) mip];
        const double x = phase * TABLE; const int i = (int) x; const float fr = (float) (x - i);
        return tab[(size_t) i] + (tab[(size_t) i + 1] - tab[(size_t) i]) * fr;
    }
};

// Legacy double buffer, kept only so older code that includes this header still compiles.
// Ghosty Synth no longer uses it: two rebuilds close together could overwrite the table the audio
// thread is still reading. Use WaveSet + WaveMailbox below instead.
struct WaveBank
{
    MipSet a[2], b[2];
    std::atomic<int> live { 0 };
    void rebuild (const Frame& fa, const Frame& fb, double sr)
    {
        const int next = 1 - live.load();
        a[next].build (fa, sr); b[next].build (fb, sr);
        live.store (next);
    }
};

// An immutable pair of band-limited tables (frame A and frame B). Built off the audio thread, never changed after.
struct WaveSet
{
    MipSet a, b;
    double sampleRate = 0;
    static std::unique_ptr<WaveSet> make (const Frame& fa, const Frame& fb, double sr)
    {
        auto s = std::make_unique<WaveSet>();
        s->a.build (fa, sr); s->b.build (fb, sr); s->sampleRate = sr;
        return s;
    }
};

// Hands finished WaveSets from ONE builder thread (the message thread) to the audio thread.
// Audio side: no locks, no allocation, no frees. The set the audio thread drops goes into a
// one-slot "trash" that the builder thread empties (deletes) on its next post() / collect().
class WaveMailbox
{
public:
    WaveMailbox() = default;
    ~WaveMailbox() { delete pending.exchange (nullptr); delete trash.exchange (nullptr); delete active; }

    // builder thread
    void post (std::unique_ptr<WaveSet> s) { delete pending.exchange (s.release()); collect(); }
    void collect()                         { delete trash.exchange (nullptr); }

    // audio thread, once at the start of each block; the pointer stays valid until the next acquire()
    const WaveSet* acquire() noexcept
    {
        if (pending.load (std::memory_order_acquire) != nullptr && trash.load (std::memory_order_acquire) == nullptr)
            if (auto* p = pending.exchange (nullptr, std::memory_order_acq_rel))
            {
                trash.store (active, std::memory_order_release);
                active = p;
            }
        return active;
    }

private:
    std::atomic<WaveSet*> pending { nullptr }, trash { nullptr };
    WaveSet* active = nullptr;      // audio thread only
    JUCE_DECLARE_NON_COPYABLE (WaveMailbox)
};
} // namespace ghosty
