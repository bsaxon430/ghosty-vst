#pragma once
// Ghost FX rack: rhythm-unit data shared by the audio thread and the editor.
//   GHOST GATE  - 16 patterns: a drawn volume curve (points + per-segment tension) and a 16-step time lane.
//   GHOST GRID  - 8 patterns of 10 effect rows x up to 32 steps (bit masks).
//   HAUNT EDIT  - 16 performance pads (gesture type, size, motion).
// The editor edits plain data on the message thread; the audio thread only reads lock-free atomics baked from it.
#include "FxCore.h"

namespace gpfx {

// ================================================================ GHOST GATE
constexpr int kGatePatterns = 16, kGateSteps = 16, kGateTab = 512;
enum TimeMode { tmNormal, tmRepeat, tmStutter, tmHalf, tmReverse, tmScratch, tmStop, kNumTimeModes };
inline const char* timeModeName (int m) { static const char* n[] { "-", "REP", "STUT", "HALF", "REV", "SCR", "STOP" }; return n[juce::jlimit (0, kNumTimeModes - 1, m)]; }
inline juce::uint32 timeModeColour (int m)
{
    static const juce::uint32 c[] { 0xff2a2e3e, 0xff45E6FF, 0xffFF4FD8, 0xffA06BFF, 0xffFFC35A, 0xff3CFF8F, 0xffFF5A6E };
    return c[juce::jlimit (0, kNumTimeModes - 1, m)];
}

struct GatePoint { float x = 0, y = 1, t = 0; };   // t = tension of the segment to the next point (-1..1)
struct GatePattern
{
    juce::String name;
    std::vector<GatePoint> pts { { 0, 1, 0 }, { 1, 1, 0 } };
    std::array<uint8_t, kGateSteps> steps {};
};

inline float segShape (float u, float t) noexcept
{
    if (std::abs (t) < 1.0e-3f) return u;
    return std::pow (u, std::pow (5.0f, t));
}
inline float evalGate (const GatePattern& p, float x)
{
    const auto& v = p.pts;
    if (v.empty()) return 1.0f;
    if (x <= v.front().x) return v.front().y;
    for (size_t i = 0; i + 1 < v.size(); ++i)
        if (x <= v[i + 1].x)
        {
            const float w = v[i + 1].x - v[i].x;
            const float u = w > 1.0e-6f ? (x - v[i].x) / w : 1.0f;
            return v[i].y + (v[i + 1].y - v[i].y) * segShape (u, v[i].t);
        }
    return v.back().y;
}

namespace detail {
inline void addPulse (GatePattern& p, float x0, float x1, float lo, float hi, float edge = 0.0025f)
{
    p.pts.push_back ({ x0, lo, 0 }); p.pts.push_back ({ x0 + edge, hi, 0 });
    p.pts.push_back ({ x1 - edge, hi, 0 }); p.pts.push_back ({ x1, lo, 0 });
}
inline GatePattern pulses (const char* name, int n, float duty, const char* onMask = nullptr, float lo = 0.0f)
{
    GatePattern p; p.name = name; p.pts.clear();
    p.pts.push_back ({ 0, lo, 0 });
    for (int k = 0; k < n; ++k)
    {
        if (onMask != nullptr && onMask[k] == '0') continue;
        const float hi = (onMask != nullptr && onMask[k] == '2') ? 0.6f : 1.0f;
        const float x0 = (float) k / (float) n + 0.0001f;
        addPulse (p, x0, x0 + duty / (float) n, lo, hi);
    }
    p.pts.push_back ({ 1, lo, 0 });
    return p;
}
inline void setSteps (GatePattern& p, std::initializer_list<int> idx, int mode) { for (int i : idx) p.steps[(size_t) i] = (uint8_t) mode; }
}

inline GatePattern factoryGatePattern (int i)
{
    using namespace detail;
    GatePattern p;
    switch (i)
    {
        case 0: p.name = "FULL"; break;
        case 1: p = pulses ("8TH GATE", 8, 0.5f); break;
        case 2: p = pulses ("16TH GATE", 16, 0.5f); break;
        case 3: p = pulses ("TRANCE", 16, 0.62f, "1011011010110111"); break;
        case 4:
        {
            p.name = "PUMP"; p.pts.clear();
            for (int b = 0; b < 4; ++b)
            {
                const float x = (float) b / 4.0f;
                p.pts.push_back ({ x + (b == 0 ? 0.0f : 0.002f), 0.05f, -0.55f });
                p.pts.push_back ({ x + 0.17f, 1.0f, 0 });
            }
            p.pts.push_back ({ 1.0f, 1.0f, 0 });
            break;
        }
        case 5: p.name = "HALF-TIME"; for (auto& s : p.steps) s = tmHalf; break;
        case 6: p.name = "REV TAIL"; setSteps (p, { 12, 13, 14, 15 }, tmReverse); break;
        case 7: p.name = "STUTTER FILL"; setSteps (p, { 12, 13 }, tmRepeat); setSteps (p, { 14, 15 }, tmStutter); break;
        case 8:
        {
            p.name = "TAPE STOP"; setSteps (p, { 12, 13, 14, 15 }, tmStop);
            p.pts = { { 0, 1, 0 }, { 0.75f, 1, 0.4f }, { 1.0f, 0.2f, 0 } };
            break;
        }
        case 9: p.name = "SCRATCH"; setSteps (p, { 3, 7, 11, 14, 15 }, tmScratch); break;
        case 10: p = pulses ("DOTTED", 16, 1.6f, "1001001001001000"); break;
        case 11:
        {
            p.name = "SWING"; p.pts.clear(); p.pts.push_back ({ 0, 0, 0 });
            for (int k = 0; k < 8; ++k)
            {
                const float x0 = (float) k / 8.0f + (k % 2 ? 0.045f : 0.0005f);
                addPulse (p, x0, x0 + (k % 2 ? 0.045f : 0.07f), 0.0f, k % 2 ? 0.75f : 1.0f);
            }
            p.pts.push_back ({ 1, 0, 0 });
            break;
        }
        case 12:
        {
            p.name = "SWELL"; p.pts.clear();
            for (int b = 0; b < 2; ++b)
            {
                const float x = (float) b * 0.5f;
                p.pts.push_back ({ x + (b == 0 ? 0.0f : 0.003f), 0.0f, 0.6f });
                p.pts.push_back ({ x + 0.497f, 1.0f, 0 });
            }
            p.pts.push_back ({ 1.0f, 1.0f, 0 });
            break;
        }
        case 13: p.name = "HALF + REV"; setSteps (p, { 0, 1, 2, 3, 4, 5, 6, 7 }, tmHalf); setSteps (p, { 12, 13, 14, 15 }, tmReverse); break;
        case 14:
        {
            p = pulses ("GLITCH", 16, 0.7f, "1101101111011010");
            const int m[16] { 0, 0, 2, 0, 4, 0, 1, 1, 0, 3, 3, 0, 5, 2, 4, 6 };
            for (int k = 0; k < 16; ++k) p.steps[(size_t) k] = (uint8_t) m[k];
            break;
        }
        default:
        {
            p.name = "BREATH"; p.pts.clear();
            for (int k = 0; k <= 8; ++k)
            {
                const float x = (float) k / 8.0f;
                p.pts.push_back ({ x, k % 2 ? 0.25f : 1.0f, k % 2 ? 0.45f : -0.45f });
            }
            break;
        }
    }
    return p;
}

// Lock-free view the audio thread reads.
struct GateStore
{
    std::array<std::array<std::atomic<float>, kGateTab + 1>, kGatePatterns> table;
    std::array<std::array<std::atomic<uint8_t>, kGateSteps>, kGatePatterns> steps;
    GateStore()
    {
        for (auto& t : table) for (auto& v : t) v.store (1.0f);
        for (auto& s : steps) for (auto& v : s) v.store (0);
    }
    void bake (int i, const GatePattern& p)
    {
        auto& t = table[(size_t) i];
        for (int k = 0; k <= kGateTab; ++k) t[(size_t) k].store (clampf (evalGate (p, (float) k / (float) kGateTab), 0.0f, 1.0f), std::memory_order_relaxed);
        for (int k = 0; k < kGateSteps; ++k) steps[(size_t) i][(size_t) k].store (p.steps[(size_t) k], std::memory_order_relaxed);
    }
    float vol (int i, double pos) const noexcept
    {
        const double x = pos * kGateTab; int k = (int) x; const float f = (float) (x - k);
        k = juce::jlimit (0, kGateTab - 1, k);
        const auto& t = table[(size_t) i];
        const float a = t[(size_t) k].load (std::memory_order_relaxed), b = t[(size_t) k + 1].load (std::memory_order_relaxed);
        return a + f * (b - a);
    }
};

// Delay (samples back from "now") for a time-manipulation mode.
//   oRun = samples since the start of the run of identical steps, runLen = run length, stepLen = one step, oStep = since step start.
inline double timeModeDelay (int mode, double oRun, double runLen, double stepLen, double oStep, float amount = 0.5f) noexcept
{
    switch (mode)
    {
        case tmRepeat:  { const double L = std::max (16.0, stepLen); return oRun - std::fmod (oRun, L); }
        case tmStutter: { const double L = std::max (16.0, stepLen * (0.5 - 0.375 * amount)); return oRun - std::fmod (oRun, L); }
        case tmHalf:    return oRun * 0.5;
        case tmReverse: return 2.0 * oRun;
        case tmScratch: return stepLen * (0.08 + 0.14 * amount) * (1.0 - std::cos (4.0 * juce::MathConstants<double>::pi * oStep / std::max (1.0, stepLen)));
        case tmStop:    { const double e = 1.0 + 1.5 * amount; const double u = std::min (1.0, oRun / std::max (1.0, runLen));
                          // speed = (1-u)^e ; delay = o - integral(speed)
                          return oRun - runLen * (1.0 - std::pow (1.0 - u, e + 1.0)) / (e + 1.0); }
        default:        return 0.0;
    }
}

// ================================================================ GHOST GRID
constexpr int kGridPatterns = 8, kGridRows = 10, kGridMaxSteps = 32;
enum GridRow { grLoop, grStutter, grScratch, grReverse, grTapeStop, grFilter, grCrush, grDelay, grVerb, grTonal };
struct GridRowInfo { const char* key; const char* name; const char* knob; juce::uint32 colour; float def; };
inline const GridRowInfo& gridRow (int r)
{
    static const GridRowInfo k[kGridRows] {
        { "loop",    "LOOP",      "LENGTH",  0xff45E6FF, 0.35f },
        { "stutter", "STUTTER",   "RATE",    0xffFF4FD8, 0.5f },
        { "scratch", "SCRATCH",   "DEPTH",   0xff3CFF8F, 0.5f },
        { "reverse", "REVERSE",   "AMOUNT",  0xffFFC35A, 1.0f },
        { "tape",    "TAPE STOP", "SPEED",   0xffFF5A6E, 0.4f },
        { "filter",  "FILTER",    "DEPTH",   0xffA06BFF, 0.6f },
        { "crush",   "CRUSH",     "AMOUNT",  0xffFF9E5A, 0.55f },
        { "delay",   "DELAY",     "FEEDBACK",0xff7FD8FF, 0.5f },
        { "verb",    "VERB",      "SIZE",    0xffE6B8FF, 0.6f },
        { "tonal",   "TONAL",     "PITCH",   0xffB6FF3C, 0.5f },
    };
    return k[juce::jlimit (0, kGridRows - 1, r)];
}
struct GridPattern { juce::String name; std::array<uint32_t, kGridRows> rows {}; };
inline GridPattern factoryGridPattern (int i)
{
    GridPattern p;
    auto set = [&p] (int row, std::initializer_list<int> steps) { for (int s : steps) p.rows[(size_t) row] |= (1u << s); };
    switch (i)
    {
        case 0: p.name = "BREAKDOWN"; set (grLoop, { 12, 13, 14, 15 }); set (grFilter, { 8, 9, 10, 11, 12, 13, 14, 15 }); set (grDelay, { 14 }); break;
        case 1: p.name = "ROLL"; set (grStutter, { 12, 13, 14, 15 }); set (grReverse, { 7 }); set (grVerb, { 15 }); break;
        case 2: p.name = "SCRATCHY"; set (grScratch, { 2, 3, 10, 11 }); set (grCrush, { 6, 14 }); break;
        case 3: p.name = "TAPE OUT"; set (grTapeStop, { 12, 13, 14, 15 }); set (grDelay, { 15 }); break;
        case 4: p.name = "REVERSO"; set (grReverse, { 4, 5, 6, 7, 12, 13, 14, 15 }); set (grVerb, { 7 }); break;
        case 5: p.name = "DUB THROWS"; set (grDelay, { 3, 7, 11, 15 }); set (grVerb, { 15 }); set (grFilter, { 0, 1, 2, 3 }); break;
        case 6: p.name = "TONAL"; set (grTonal, { 0, 1, 8, 9 }); set (grCrush, { 4, 5, 12, 13 }); break;
        default: p.name = "CHAOS"; set (grLoop, { 6, 7 }); set (grStutter, { 14, 15 }); set (grScratch, { 3 }); set (grReverse, { 10, 11 });
                 set (grCrush, { 1, 5, 9 }); set (grDelay, { 7 }); set (grTonal, { 12, 13 }); set (grFilter, { 8, 9, 10, 11 }); break;
    }
    return p;
}
struct GridStore
{
    std::array<std::array<std::atomic<uint32_t>, kGridRows>, kGridPatterns> rows;
    GridStore() { for (auto& p : rows) for (auto& r : p) r.store (0); }
    void bake (int i, const GridPattern& p) { for (int r = 0; r < kGridRows; ++r) rows[(size_t) i][(size_t) r].store (p.rows[(size_t) r]); }
    bool cell (int pat, int row, int step) const noexcept { return (rows[(size_t) pat][(size_t) row].load (std::memory_order_relaxed) >> step) & 1u; }
};

// ================================================================ HAUNT EDIT
constexpr int kPads = 16;
enum HauntType { htStutter, htBuild, htGate, htGlitch, htReverse, htTapeStop, htLpSweep, htHpSweep, htCrush, htPitchDrop, kNumHauntTypes };
inline const juce::StringArray& hauntTypeNames()
{
    static juce::StringArray s { "STUTTER", "BUILD", "GATE", "GLITCH", "REVERSE", "TAPE STOP", "LP SWEEP", "HP SWEEP", "CRUSH", "PITCH DROP" };
    return s;
}
inline juce::uint32 hauntTypeColour (int t)
{
    static const juce::uint32 c[] { 0xffFF4FD8, 0xffFF7AE6, 0xff3CFF8F, 0xff45E6FF, 0xffFFC35A, 0xffFF5A6E, 0xffA06BFF, 0xff9C8BFF, 0xffFF9E5A, 0xffB6FF3C };
    return c[juce::jlimit (0, kNumHauntTypes - 1, t)];
}
struct PadDef { int type = htStutter, div = 7; float motion = 0; };
inline PadDef factoryPad (int i)
{
    static const PadDef d[kPads] {
        { htStutter, 7, 0.0f }, { htStutter, 5, 0.0f }, { htStutter, 3, 0.0f }, { htStutter, 1, 0.0f },
        { htBuild, 7, 0.0f }, { htGate, 5, 0.5f }, { htGate, 3, 0.5f }, { htGate, 2, 0.4f },
        { htGlitch, 3, 0.6f }, { htGlitch, 1, 0.75f }, { htReverse, 7, 0.0f }, { htPitchDrop, 5, 0.5f },
        { htTapeStop, 8, 0.3f }, { htLpSweep, 9, 0.5f }, { htHpSweep, 9, 0.5f }, { htCrush, 8, 0.5f } };
    return d[juce::jlimit (0, kPads - 1, i)];
}
struct PadStore
{
    std::array<std::atomic<int>, kPads> type, div; std::array<std::atomic<float>, kPads> motion;
    PadStore() { for (int i = 0; i < kPads; ++i) set (i, factoryPad (i)); }
    void set (int i, const PadDef& d) { type[(size_t) i].store (d.type); div[(size_t) i].store (d.div); motion[(size_t) i].store (d.motion); }
    PadDef get (int i) const { return { type[(size_t) i].load(), div[(size_t) i].load(), motion[(size_t) i].load() }; }
};

} // namespace gpfx
