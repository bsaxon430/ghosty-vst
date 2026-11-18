#pragma once
// Bend lane: a drawable, tempo-synced pitch curve. Shared by Ghosty Pro's BEND LANE (synth master pitch) and
// Ghost Box's WARP unit (pitch / tape on the incoming audio). Model + evaluation + factory shapes; no JUCE GUI here.
// Real-time rule: the audio thread only calls SharedShape::fetch() and Shape::valueAt() (no locks, no allocation).
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace bendlane {

constexpr int kMaxPoints = 32;

// x = position in the lane (0..1), y = -1..1 (times the range in semitones), tension -1..1 (0 = straight,
// > 0 bulges up, < 0 bulges down), step = hold y until the next point (a jump instead of a curve).
struct Point { float x = 0, y = 0, tension = 0; bool step = false; };

inline const juce::StringArray& lengthNames() { static juce::StringArray s { "1/2 BAR", "1 BAR", "2 BARS", "4 BARS" }; return s; }
inline const juce::StringArray& rangeNames()
{
    static juce::StringArray s { juce::String::fromUTF8 ("\xc2\xb1" "2 ST"), juce::String::fromUTF8 ("\xc2\xb1" "7 ST"),
                                 juce::String::fromUTF8 ("\xc2\xb1" "12 ST"), juce::String::fromUTF8 ("\xc2\xb1" "24 ST") };
    return s;
}
inline const juce::StringArray& snapNames() { static juce::StringArray s { "SNAP OFF", "SEMITONE", "5TH + OCT", "OCTAVE" }; return s; }
inline const juce::StringArray& gridNames() { static juce::StringArray s { "GRID 1/4", "GRID 1/8", "GRID 1/16", "GRID 1/32" }; return s; }
inline double barsOf (int lenIndex) noexcept { static const double b[] { 0.5, 1.0, 2.0, 4.0 }; return b[lenIndex < 0 ? 0 : (lenIndex > 3 ? 3 : lenIndex)]; }
inline float rangeOf (int rangeIndex) noexcept { static const float r[] { 2.0f, 7.0f, 12.0f, 24.0f }; return r[rangeIndex < 0 ? 0 : (rangeIndex > 3 ? 3 : rangeIndex)]; }
inline int gridPerBeat (int gridIndex) noexcept { return 1 << (gridIndex < 0 ? 0 : (gridIndex > 3 ? 3 : gridIndex)); }   // 1/4 .. 1/32

// Snap a semitone value: 0 off, 1 semitone, 2 = 0 / 5ths / octaves, 3 octaves.
inline float snapSemis (float s, int mode) noexcept
{
    switch (mode)
    {
        case 1: return std::round (s);
        case 2:
        {
            static const float k[] { -24, -19, -17, -12, -7, -5, 0, 5, 7, 12, 17, 19, 24 };
            float best = 0, d = 1.0e9f;
            for (float v : k) if (std::abs (v - s) < d) { d = std::abs (v - s); best = v; }
            return best;
        }
        case 3: return 12.0f * std::round (s / 12.0f);
        default: return s;
    }
}

// Segment easing: t in 0..1 -> 0..1. Positive tension bulges the segment up (whichever way it goes).
inline float ease (float t, float tension, bool rising) noexcept
{
    if (std::abs (tension) < 1.0e-4f) return t;
    const float e = std::exp2 (3.0f * std::abs (tension));
    const bool fastStart = (tension > 0) == rising;
    return fastStart ? 1.0f - std::pow (1.0f - t, e) : std::pow (t, e);
}

struct Shape
{
    int n = 0;
    std::array<Point, kMaxPoints> p {};
    int snap = 1, grid = 2;   // editor settings saved with the curve: snap SEMITONE, grid 1/16

    void add (float x, float y, float t = 0, bool st = false) { if (n < kMaxPoints) p[(size_t) n++] = { x, y, t, st }; }
    void sanitise()
    {
        n = juce::jlimit (0, kMaxPoints, n);
        for (int i = 0; i < n; ++i)
        {
            auto& q = p[(size_t) i];
            if (! std::isfinite (q.x)) q.x = 0;
            if (! std::isfinite (q.y)) q.y = 0;
            if (! std::isfinite (q.tension)) q.tension = 0;
            q.x = juce::jlimit (0.0f, 0.9999f, q.x); q.y = juce::jlimit (-1.0f, 1.0f, q.y); q.tension = juce::jlimit (-1.0f, 1.0f, q.tension);
        }
        std::sort (p.begin(), p.begin() + n, [] (const Point& a, const Point& b) { return a.x < b.x; });
        if (n == 0) add (0, 0);
        snap = juce::jlimit (0, 3, snap); grid = juce::jlimit (0, 3, grid);
    }
    bool sameAs (const Shape& o) const
    {
        if (n != o.n || snap != o.snap || grid != o.grid) return false;
        for (int i = 0; i < n; ++i)
        {
            const auto& a = p[(size_t) i]; const auto& b = o.p[(size_t) i];
            if (std::abs (a.x - b.x) > 1.0e-4f || std::abs (a.y - b.y) > 1.0e-4f || std::abs (a.tension - b.tension) > 1.0e-4f || a.step != b.step) return false;
        }
        return true;
    }
    // index of the segment that contains phase (the segment starting at point i; the last one wraps to point 0)
    int segmentAt (double ph) const noexcept
    {
        if (n <= 0) return -1;
        if (ph < p[0].x) return n - 1;
        int i = 0;
        while (i + 1 < n && p[(size_t) i + 1].x <= ph) ++i;
        return i;
    }
    float valueAt (double ph) const noexcept
    {
        if (n <= 0) return 0.0f;
        if (n == 1) return p[0].y;
        ph -= std::floor (ph);
        const int i = segmentAt (ph);
        const auto& a = p[(size_t) i];
        const bool wrap = i == n - 1;
        const auto& b = p[(size_t) (wrap ? 0 : i + 1)];
        double ax = a.x, bx = b.x + (wrap ? 1.0 : 0.0);
        double x = ph; if (wrap && x < ax) x += 1.0;
        if (a.step) return a.y;
        const double span = bx - ax;
        if (span <= 1.0e-7) return b.y;
        const float t = (float) juce::jlimit (0.0, 1.0, (x - ax) / span);
        return a.y + (b.y - a.y) * ease (t, a.tension, b.y > a.y);
    }

    juce::String toString() const
    {
        juce::StringArray s;
        for (int i = 0; i < n; ++i) { const auto& q = p[(size_t) i]; s.add (juce::String (q.x, 5) + "," + juce::String (q.y, 5) + "," + juce::String (q.tension, 4) + "," + (q.step ? "1" : "0")); }
        return s.joinIntoString (";");
    }
    static Shape fromString (const juce::String& str, int snapMode = 1, int gridMode = 2)
    {
        Shape sh; sh.snap = snapMode; sh.grid = gridMode;
        for (auto& tok : juce::StringArray::fromTokens (str, ";", ""))
        {
            auto c = juce::StringArray::fromTokens (tok, ",", "");
            if (c.size() >= 2) sh.add (c[0].getFloatValue(), c[1].getFloatValue(), c.size() > 2 ? c[2].getFloatValue() : 0.0f, c.size() > 3 && c[3].getIntValue() != 0);
        }
        sh.sanitise();
        return sh;
    }
};

// ---------------------------------------------------------------- factory shapes
struct Factory { const char* name; int lenIndex, rangeIndex; Shape shape; };
inline const std::vector<Factory>& factory()
{
    static const std::vector<Factory> f = []
    {
        std::vector<Factory> v;
        auto mk = [&v] (const char* nm, int len, int rng, std::initializer_list<Point> pts)
        {
            Factory x { nm, len, rng, {} };
            for (auto& q : pts) x.shape.add (q.x, q.y, q.tension, q.step);
            x.shape.sanitise(); v.push_back (x);
        };
        // 1 bar, +-12: quick up an octave, hold, back down, rest (the default)
        mk ("Up & back",       1, 2, { { 0.0f, 0.0f, 0.4f }, { 0.0625f, 1.0f }, { 0.1875f, 1.0f, 0.4f }, { 0.25f, 0.0f } });
        // the trap-beat move: up and back, up and back, then down and back, rest
        mk ("Up-up-down",      1, 2, { { 0.0f, 0.0f, 0.4f }, { 0.0625f, 1.0f }, { 0.125f, 1.0f, 0.4f }, { 0.1875f, 0.0f },
                                       { 0.25f, 0.0f, 0.4f }, { 0.3125f, 1.0f }, { 0.375f, 1.0f, 0.4f }, { 0.4375f, 0.0f },
                                       { 0.5f, 0.0f, -0.4f }, { 0.5625f, -1.0f }, { 0.6875f, -1.0f, -0.4f }, { 0.75f, 0.0f } });
        // half bar: jump up an octave and fall straight back
        mk ("Octave flick",    0, 2, { { 0.0f, 1.0f, -0.6f }, { 0.25f, 0.0f, 0.0f, true } });
        mk ("Drop & return",   1, 2, { { 0.0f, 0.0f, -0.3f }, { 0.125f, -1.0f }, { 0.5f, -1.0f, 0.5f }, { 0.75f, 0.0f } });
        {   // +-2: 1/8-note wobble
            std::vector<Point> w; for (int k = 0; k < 16; ++k) w.push_back ({ k / 16.0f, (k % 2) ? 0.5f : -0.5f, 0.0f, false });
            Factory x { "Wobble 1/8", 1, 0, {} }; for (auto& q : w) x.shape.add (q.x, q.y, q.tension, q.step); x.shape.sanitise(); v.push_back (x);
        }
        mk ("Rise 4 bars",     3, 2, { { 0.0f, 0.0f, -0.5f }, { 0.96f, 1.0f, 0.0f, true } });
        mk ("Dive end of bar", 1, 3, { { 0.0f, 0.0f }, { 0.75f, 0.0f, 0.5f }, { 0.98f, -1.0f, 0.0f, true } });
        {   // +-2: whole-tone trill in 1/32 notes
            Factory x { "Trill", 1, 0, {} }; for (int k = 0; k < 32; ++k) x.shape.add (k / 32.0f, (k % 2) ? 1.0f : 0.0f, 0.0f, true); x.shape.sanitise(); v.push_back (x);
        }
        mk ("Slide down",      1, 2, { { 0.0f, 0.0f }, { 0.5f, 0.0f, 0.2f }, { 0.875f, -1.0f, 0.0f, true } });
        mk ("Stair up",        1, 2, { { 0.0f, 0.0f, 0.0f, true }, { 0.25f, 4.0f / 12.0f, 0.0f, true }, { 0.5f, 7.0f / 12.0f, 0.0f, true }, { 0.75f, 1.0f, 0.0f, true } });
        return v;
    }();
    return f;
}
inline juce::StringArray factoryNames() { juce::StringArray s; for (auto& f : factory()) s.add (f.name); return s; }
inline Shape defaultShape() { return factory()[0].shape; }

// ---------------------------------------------------------------- lock-free hand-over (message thread -> audio thread)
class SharedShape
{
public:
    SharedShape() { set (defaultShape()); }
    void set (Shape s)
    {
        s.sanitise();
        const juce::SpinLock::ScopedLockType sl (lock);
        ui = s;
        seq.fetch_add (1, std::memory_order_relaxed);               // odd: writing
        std::atomic_thread_fence (std::memory_order_release);
        count.store (s.n, std::memory_order_relaxed);
        for (int i = 0; i < kMaxPoints; ++i) packed[(size_t) i].store (i < s.n ? pack (s.p[(size_t) i]) : 0, std::memory_order_relaxed);
        seq.fetch_add (1, std::memory_order_release);               // even: done
    }
    Shape get() const { const juce::SpinLock::ScopedLockType sl (lock); return ui; }
    uint32_t version() const noexcept { return seq.load (std::memory_order_acquire); }
    bool isDefault() const { return get().sameAs (defaultShape()); }

    // audio thread: copies the newest curve into `out` if it changed since `seen`; never blocks
    bool fetch (Shape& out, uint32_t& seen) const noexcept
    {
        const uint32_t s1 = seq.load (std::memory_order_acquire);
        if (s1 == seen || (s1 & 1u) != 0) return false;
        Shape t; t.n = juce::jlimit (0, kMaxPoints, count.load (std::memory_order_relaxed));
        for (int i = 0; i < t.n; ++i) t.p[(size_t) i] = unpack (packed[(size_t) i].load (std::memory_order_relaxed));
        std::atomic_thread_fence (std::memory_order_acquire);
        if (seq.load (std::memory_order_relaxed) != s1) return false;
        if (t.n == 0) t.add (0, 0);
        out = t; seen = s1;
        return true;
    }

private:
    mutable juce::SpinLock lock;
    Shape ui;
    std::atomic<uint32_t> seq { 0 };
    std::atomic<int> count { 0 };
    std::array<std::atomic<uint64_t>, kMaxPoints> packed {};

    static uint64_t pack (const Point& q) noexcept
    {
        const auto x = (uint64_t) (uint16_t) juce::jlimit (0, 65535, (int) std::lround (q.x * 65536.0f));
        const auto y = (uint64_t) (uint16_t) (int16_t) juce::jlimit (-32767, 32767, (int) std::lround (q.y * 32767.0f));
        const auto t = (uint64_t) (uint16_t) (int16_t) juce::jlimit (-32767, 32767, (int) std::lround (q.tension * 32767.0f));
        return x | (y << 16) | (t << 32) | ((uint64_t) (q.step ? 1 : 0) << 48);
    }
    static Point unpack (uint64_t v) noexcept
    {
        Point q;
        q.x = (float) (v & 0xffff) / 65536.0f;
        q.y = (float) (int16_t) (uint16_t) ((v >> 16) & 0xffff) / 32767.0f;
        q.tension = (float) (int16_t) (uint16_t) ((v >> 32) & 0xffff) / 32767.0f;
        q.step = ((v >> 48) & 1) != 0;
        return q;
    }
};

// ---------------------------------------------------------------- TAPE helper: read-head offset over one lane cycle
// O(phase) = integral of (speed - 1) in lane cycles, speed = 2^(semitones / 12). The tape delay is
// D = headroom - O(phase) * cycleSamples, which depends only on the phase: it is the same at every lane start
// (no drift), and headroom = max(O) keeps it from ever reading ahead of the input.
struct TapeTable
{
    static constexpr int K = 1024;
    std::array<float, K + 1> o {};
    float maxO = 0, endO = 0;
    void build (const Shape& s, float semisScale) noexcept
    {
        double acc = 0; o[0] = 0; maxO = 0;
        constexpr int sub = 4;
        auto speed = [&] (double ph) { return std::exp2 ((double) s.valueAt (ph) * semisScale / 12.0) - 1.0; };
        double prev = speed (0.0);
        for (int k = 1; k <= K; ++k)
        {
            for (int j = 1; j <= sub; ++j)
            {
                const double ph = ((k - 1) * sub + j) / (double) (K * sub);
                const double cur = speed (ph >= 1.0 ? 0.9999999 : ph);
                acc += 0.5 * (prev + cur) / (double) (K * sub); prev = cur;
            }
            o[(size_t) k] = (float) acc; maxO = std::max (maxO, (float) acc);
        }
        endO = (float) acc;
    }
    float at (double ph) const noexcept
    {
        const double x = juce::jlimit (0.0, 1.0, ph) * K; const int i = std::min (K - 1, (int) x); const float f = (float) (x - i);
        return o[(size_t) i] + f * (o[(size_t) i + 1] - o[(size_t) i]);
    }
};

} // namespace bendlane
