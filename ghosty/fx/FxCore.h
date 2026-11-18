#pragma once
// (Copied from Ghosty Pro's own rack into this GPL-3 fork; only GHOST GATE and HAUNT are used here.)
// Ghost FX rack: shared DSP building blocks, unit list, parameter helpers and the unit interface.
// Audio-thread rule for everything here: no allocation, no locks, no strings inside process().
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "ParamText.h"
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

#ifndef GPFX_BOX
 #define GPFX_BOX 0   // 1 = Ghost Box build (extra units WARP / RETRO / FATTEN / CRACKLE / HAUNTED HALLS); Ghosty Pro builds with 0
#endif

namespace gpfx {

constexpr float kPi = 3.14159265358979f, kTwoPi = 6.28318530717959f;
constexpr int kSub = 256;   // the rack always works in sub-blocks of at most this many samples

// ---------------------------------------------------------------- the units (enum order = default processing order)
enum Unit { uGate, uGrid, uHaunt,
#if GPFX_BOX
            uWarp, uCrackle,
#endif
            uWreck, uTube, uCrush,
#if GPFX_BOX
            uRetro, uFatten, uSplit, uBassLab, uFormant,
#endif
            uEq, uFilter, uChorus, uCrystal, uEcho, uSpace, uShimmer,
#if GPFX_BOX
            uHalls, uSpectra, uPortal,
#endif
            uOtt,
#if ! GPFX_BOX
            uFatten,   // Ghosty Pro (2026-10-23): FATTEN as the last unit before the limiter (state keys and parameter ids are names, not these numbers)
            uHyper,    // Ghosty Pro (2026-10-24): HYPER / DIMENSION, likewise appended (parameters in addLateParameters)
            uDist,     // FX RACK (round 3, 2026-10-25): DISTORTION
            uComp,     // FX RACK (round 3): COMPRESSOR
            uBands,    // FX RACK (round 3): SPLITTER (L/H, L/M/H, M/S with a mini rack per band)
            uEq3,      // FX RACK (round 3): TONE EQ (3 bells + tilt)
#endif
            uLim, kNumUnits };
enum Group { gRhythm, gColour, gSpace, gMaster };
#if ! GPFX_BOX
// Ghosty Pro: the units appended after the original 15, in unit order (their parameters come from addLateParameters, and the saved
// order leaves each one out while it sits at its default place, so older builds read newer states unchanged)
inline const std::vector<int>& lateUnits() { static const std::vector<int> v { uFatten, uHyper, uDist, uComp, uBands, uEq3 }; return v; }
#endif

struct UnitInfo { const char* key; const char* title; const char* shortName; const char* blurb; int group; juce::uint32 accent; int look; bool hasMix; };
inline const UnitInfo& unitInfo (int u)
{
    static const UnitInfo k[kNumUnits] {
        { "gate",    "GHOST GATE",     "GATE",    "16 drawable volume + time patterns per bar",        gRhythm, 0xff3CFF8F, 3,  true },
        { "grid",    "GHOST GRID",     "GRID",    "paint effects onto a 1-bar step grid",              gRhythm, 0xff45E6FF, 7,  true },
        { "haunt",   "HAUNT EDIT",     "HAUNT",   "hold a pad for tempo-synced stutter gestures",      gRhythm, 0xffFF4FD8, 11, true },
#if GPFX_BOX
        { "warp",    "WARP",           "WARP",    "draw a tempo-synced pitch bend: PITCH keeps time, TAPE bends time too", gRhythm, 0xffC6FF4F, 15, true },
        { "crackle", "CRACKLE",        "CRACKLE", "multiband destroyer: 14 curves, speaker boxes",     gColour, 0xffFF6A3D, 6,  true },
#endif
        { "wreck",   "WRECKER",        "WRECK",   "analog saturation from warm to slammed",           gColour, 0xffFFC35A, 5,  true },
        { "tube",    "TUBE GLOW",      "TUBE",    "vintage tube console warmth",                       gColour, 0xffFF9E5A, 9,  true },
        { "crush",   "SOUL CRUSH",     "CRUSH",   "level-smashing grit compressor",                    gColour, 0xffFF5A6E, 13, true },
#if GPFX_BOX
        { "retro",   "RETRO",          "RETRO",   "vintage colour: noise, wobble, dirt, dust and age", gColour, 0xffE8C07A, 8,  true },
        { "fatten",  "FATTEN",         "FATTEN",  "one big knob: turn it and it gets fatter",          gColour, 0xffFF8FB1, 4,  true },
        { "split",   "GHOST SPLIT",    "SPLIT",   "split hits into ATTACK and BODY, then treat each its own way", gColour, 0xff5AF0C8, 10, true },
        { "basslab", "BASS LAB",       "BASS",    "low / mid / high bass tool: mono sub, growl, edge, TRANSLATE for small speakers", gColour, 0xffFF7A3D, 12, true },
        { "formant", "GHOST FORMANT",  "FORMANT", "talking vowel filter: A-E-I-O-U, mouth size, drawn or synced movement", gColour, 0xffFF5AC8, 14, true },
#endif
        { "eq",      "SPIRIT EQ",      "EQ",      "precision dynamic EQ over a live analyser",         gColour, 0xff45E6FF, 1,  true },
        { "filter",  "PHANTOM FILTER", "FILTER",  "resonant analog multimode filter with rhythm LFO",  gColour, 0xffA06BFF, 17, true },
        { "chorus",  "CHORUS",         "CHORUS",  "three-voice ghost chorus",                          gSpace,  0xff7FD8FF, 19, true },
        { "crystal", "CRYSTAL",        "CRYSTAL", "reverse + pitched granular splices",               gSpace,  0xffB8F4FF, 21, true },
        { "echo",    "ECTO ECHO",      "ECHO",    "tape / analog / BBD / digital character delay",    gSpace,  0xff3CFF8F, 23, true },
        { "space",   "SPECTRAL SPACE", "SPACE",   "blendable ambience with glimmering tails",          gSpace,  0xff9C8BFF, 25, true },
        { "shimmer", "SHIMMER",        "SHIMMER", "lush octave-shimmer reverb with freeze",            gSpace,  0xffE6B8FF, 27, true },
#if GPFX_BOX
        { "halls",   "HAUNTED HALLS",  "HALLS",   "16-line reverb, 11 rooms from tight to endless",    gSpace,  0xff8FA8FF, 2,  true },
        { "spectra", "GHOST SPECTRA",  "SPECTRA", "spectral freeze, smear, scatter and shift; GHOST keeps the hits", gSpace, 0xff6BFFE6, 16, true },
        { "portal",  "DOORWAY",        "DOOR",    "morphing-spaces reverb: shapes, early reflections, movement, transform, freeze", gSpace, 0xffB06BFF, 18, true },
#endif
        { "ott",     "GHOST SQUEEZE",  "SQUEEZE", "3-band upward + downward compression",              gMaster, 0xffFF4FD8, 29, true },
#if ! GPFX_BOX
        { "fatten",  "FATTEN",         "FATTEN",  "one big knob: turn it and it gets fatter",          gColour, 0xffFF8FB1, 4,  true },
        { "hyper",   "HYPER",          "HYPER",   "1-7 micro-delay chorus voices (big unison without the CPU) or DIMENSION: 4 moving lines", gSpace, 0xff7FFFB0, 20, true },
        { "dist",    "DISTORTION",     "DIST",    "10 curves incl. two drawn ones, 4x oversampled + anti-aliased, filter before or after", gColour, 0xffFF7A4F, 22, true },
        { "comp",    "COMPRESSOR",     "COMP",    "single or 3-band up / down compression, RATIO max = true-peak limiter", gMaster, 0xffFFD24F, 24, true },
        { "bands",   "SPLITTER",       "SPLIT",   "split into L/H, L/M/H or M/S and give every band its own mini rack of up to 3 units", gColour, 0xff5AF0C8, 26, true },
        { "eq3",     "TONE EQ",        "TONE",    "3 quick parametric bells and a tilt: shape the tone in seconds", gColour, 0xff8FD8FF, 28, true },
#endif
        { "lim",     "GHOST LIMITER",  "LIMIT",   "transparent lookahead limiter, no overs",           gMaster, 0xffF2F4F5, 31, false },
    };
    return k[juce::jlimit (0, kNumUnits - 1, u)];
}
inline const char* groupName (int g) { static const char* n[] { "RHYTHM", "COLOUR", "SPACE", "MASTER" }; return n[juce::jlimit (0, 3, g)]; }
inline juce::String pid (int u, const char* s) { return juce::String ("fx_") + unitInfo (u).key + "_" + s; }

// ---------------------------------------------------------------- tempo divisions
struct Division { const char* name; double beats; };
inline const std::vector<Division>& echoDivs()
{
    static const std::vector<Division> d { { "1/64", 0.0625 }, { "1/32", 0.125 }, { "1/16T", 1.0 / 6.0 }, { "1/16", 0.25 }, { "1/16D", 0.375 },
                                           { "1/8T", 1.0 / 3.0 }, { "1/8", 0.5 }, { "1/8D", 0.75 }, { "1/4T", 2.0 / 3.0 }, { "1/4", 1.0 },
                                           { "1/4D", 1.5 }, { "1/2T", 4.0 / 3.0 }, { "1/2", 2.0 }, { "1/2D", 3.0 }, { "1/1", 4.0 }, { "2/1", 8.0 } };
    return d;
}
inline const std::vector<Division>& padDivs()
{
    static const std::vector<Division> d { { "1/64", 0.0625 }, { "1/32", 0.125 }, { "1/16T", 1.0 / 6.0 }, { "1/16", 0.25 }, { "1/8T", 1.0 / 3.0 },
                                           { "1/8", 0.5 }, { "1/4T", 2.0 / 3.0 }, { "1/4", 1.0 }, { "1/2", 2.0 }, { "1 BAR", 4.0 }, { "2 BARS", 8.0 } };
    return d;
}
inline juce::StringArray divNames (const std::vector<Division>& d) { juce::StringArray s; for (auto& x : d) s.add (x.name); return s; }

// ---------------------------------------------------------------- parameter helpers
using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
inline juce::NormalisableRange<float> skewRange (float lo, float hi, float centre)
{
    juce::NormalisableRange<float> r (lo, hi);
    if (centre > lo && centre < hi) r.setSkewForCentre (centre);
    return r;
}
#if JUCE_MAJOR_VERSION >= 7   // Ghosty Pro on Vital: Vital ships JUCE 6, which has none of these (the units below never need them)
inline void addFloat (Layout& l, const juce::String& id, const juce::String& name, juce::NormalisableRange<float> r, float def,
                      const juce::String& suffix = {}, int decimals = 2)
{
    auto attrs = juce::AudioParameterFloatAttributes().withLabel (suffix)
        .withStringFromValueFunction ([decimals, suffix] (float v, int) {
            if (suffix == "Hz" && v >= 1000.0f) return juce::String (v / 1000.0f, 2) + " kHz";
            return (decimals <= 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, decimals)) + (suffix.isEmpty() ? "" : " " + suffix); })
        .withValueFromStringFunction ([suffix] (const juce::String& t) {
            auto s = t.trim().toLowerCase(); double v = s.getDoubleValue();
            if (suffix == "Hz" && s.containsChar ('k')) v *= 1000.0;
            return (float) v; });
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, r, def, attrs));
}
inline void addPct (Layout& l, const juce::String& id, const juce::String& name, float def)
{
    auto attrs = juce::AudioParameterFloatAttributes().withLabel ("%")
        .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; })
        .withValueFromStringFunction ([] (const juce::String& t) { return (float) t.getDoubleValue() / 100.0f; });
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (0.0f, 1.0f), def, attrs));
}
inline void addBool (Layout& l, const juce::String& id, const juce::String& name, bool def)
{
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def));
}
inline void addChoice (Layout& l, const juce::String& id, const juce::String& name, const juce::StringArray& items, int def)
{
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, items, def,
               juce::AudioParameterChoiceAttributes().withValueFromStringFunction ([items] (const juce::String& t) { return ghosty::ptext::choiceIndex (items, t); })));
}
inline void addInt (Layout& l, const juce::String& id, const juce::String& name, int lo, int hi, int def)
{
    l.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { id, 1 }, name, lo, hi, def));
}

#endif

#if GPFX_BOX
// Ghost Box modulation layer (MORPH / MOTION / MACROS): while the rack attaches its units, every P is bound to a
// shadow value that the rack refreshes from the host parameter (plus modulation) before each processing chunk.
struct ShadowBinder
{
    virtual ~ShadowBinder() = default;
    virtual std::atomic<float>* shadowFor (const juce::String& id, std::atomic<float>* host) = 0;
    static ShadowBinder*& current() { static thread_local ShadowBinder* c = nullptr; return c; }
};
#endif

// Raw parameter pointer with a safe default when missing.
struct P
{
    std::atomic<float>* p = nullptr; float def = 0;
    void bind (juce::AudioProcessorValueTreeState& s, const juce::String& id, float fallback = 0)
    {
        p = s.getRawParameterValue (id); def = fallback;
        jassert (p != nullptr);
#if GPFX_BOX
        if (p != nullptr && ShadowBinder::current() != nullptr) p = ShadowBinder::current()->shadowFor (id, p);
#endif
    }
    float operator()() const noexcept { return p ? p->load (std::memory_order_relaxed) : def; }
    int i() const noexcept { return (int) std::lround ((*this)()); }
    bool b() const noexcept { return (*this)() > 0.5f; }
};

// ---------------------------------------------------------------- small math
inline float dbToGain (float db) noexcept { return std::exp (db * 0.11512925465f); }
inline float gainToDb (float g) noexcept { return 8.68588963807f * std::log (std::max (g, 1.0e-9f)); }
inline bool changed (float a, float b) noexcept { return std::abs (a - b) > 1.0e-6f; }
inline float clampf (float x, float lo, float hi) noexcept { return x < lo ? lo : (x > hi ? hi : x); }
inline float softClip (float x) noexcept { return x / (1.0f + std::abs (x)); }
inline float fastTanh (float x) noexcept
{
    x = clampf (x, -4.97f, 4.97f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
inline uint32_t hash32 (uint32_t x) noexcept
{
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x;
}
inline float hashFloat (uint32_t x) noexcept { return (float) (hash32 (x) & 0xffffff) / 16777215.0f; }
inline double positiveMod (double a, double b) noexcept { double r = std::fmod (a, b); return r < 0 ? r + b : r; }

// ---------------------------------------------------------------- smoothers
struct LinSmooth
{
    float cur = 0, target = 0, step = 0; int left = 0, len = 1;
    void setLen (int n) { len = std::max (1, n); }
    void setTarget (float t) noexcept { if (std::abs (t - target) > 1.0e-9f) { target = t; left = len; step = (target - cur) / (float) len; } }
    void snap (float v) noexcept { cur = target = v; left = 0; }
    float next() noexcept { if (left > 0) { cur += step; if (--left == 0) cur = target; } return cur; }
    bool idle() const noexcept { return left == 0; }
};
struct ExpSmooth
{
    float cur = 0, target = 0, a = 1;
    void setTime (double sr, float ms) { a = 1.0f - (float) std::exp (-1.0 / std::max (1.0, ms * 0.001 * sr)); }
    float next() noexcept { cur += a * (target - cur); return cur; }
    void snap (float v) noexcept { cur = target = v; }
};

// ---------------------------------------------------------------- ring buffer, read by delay (0 = newest sample)
struct Ring
{
    std::vector<float> buf; int mask = 0, w = 0;
    void prepare (int minSize) { int n = 16; while (n < minSize) n <<= 1; buf.assign ((size_t) n, 0.0f); mask = n - 1; w = 0; }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    int size() const noexcept { return mask + 1; }
    void push (float x) noexcept { buf[(size_t) w] = x; w = (w + 1) & mask; }
    float at (int back) const noexcept { return buf[(size_t) ((w - 1 - back) & mask)]; }
    double maxDelay() const noexcept { return (double) (size() - 8); }
    float readLinear (double d) const noexcept
    {
        d = d < 0 ? 0 : (d > maxDelay() ? maxDelay() : d);
        const int i = (int) d; const float f = (float) (d - i);
        const float a = at (i), b = at (i + 1);
        return a + f * (b - a);
    }
    float read (double d) const noexcept   // cubic Hermite; never touches samples newer than the newest
    {
        d = d < 0 ? 0 : (d > maxDelay() ? maxDelay() : d);
        const int i = (int) d; const float f = (float) (d - i);
        const float y0 = at (i), y1 = at (i + 1);
        if (i == 0) return y0 + f * (y1 - y0);
        const float ym1 = at (i - 1), y2 = at (i + 2);
        const float c1 = 0.5f * (y1 - ym1), c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2, c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * f + c2) * f + c1) * f + y0;
    }
};

// Fixed integer delay (used to align dry paths with latent units).
struct FixedDelay
{
    std::array<Ring, 2> r; int d = 0;
    void prepare (int delay) { d = std::max (0, delay); for (auto& x : r) x.prepare (d + 4); }
    void clear() { for (auto& x : r) x.clear(); }
    void process (float* L, float* R, int n) noexcept
    {
        if (d == 0) return;
        for (int i = 0; i < n; ++i)
        {
            r[0].push (L[i]); r[1].push (R[i]);
            L[i] = r[0].at (d); R[i] = r[1].at (d);
        }
    }
};

// Reads a ring at a moving delay; when the delay jumps (loop restart, step change) it crossfades from the old read
// head (which keeps running at its previous speed) to the new one, so time effects never click.
struct DelayReader
{
    double prevD = 0, dD = 0, oldD = 0, oldStep = 0; int xf = 0, xfLen = 128; float wNew = 1, wOld = 0; double curD = 0;
    void prepare (double sr) { xfLen = std::max (16, (int) (sr * 0.004)); reset(); }
    void reset() { prevD = dD = oldD = oldStep = curD = 0; xf = 0; wNew = 1; wOld = 0; }
    void advance (double D) noexcept
    {
        const double pred = prevD + dD;
        if (std::abs (D - pred) > 2.0)
        {
            oldD = pred; oldStep = juce::jlimit (-1.0, 3.0, dD); xf = xfLen; dD = 0;
        }
        else dD = D - prevD;
        prevD = D; curD = D;
        if (xf > 0)
        {
            oldD = std::max (0.0, oldD + oldStep);
            const float a = (float) xf / (float) xfLen;              // 1 -> 0
            wOld = 0.5f - 0.5f * std::cos (kPi * a); wNew = 1.0f - wOld; --xf;
        }
        else { wNew = 1; wOld = 0; }
    }
    float tap (const Ring& r) const noexcept
    {
        float y = wNew * (curD < 0.001 ? r.at (0) : r.read (curD));
        if (wOld > 0) y += wOld * r.read (oldD);
        return y;
    }
};

// ---------------------------------------------------------------- filters
struct BiquadCoefs { float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
enum RbjType { rbjLP, rbjHP, rbjBP, rbjNotch, rbjBell, rbjLowShelf, rbjHighShelf, rbjAllpass };
inline BiquadCoefs rbj (int type, double sr, double f, double q, double gainDb = 0)
{
    f = juce::jlimit (5.0, sr * 0.49, f); q = std::max (0.025, q);
    const double w0 = 2.0 * juce::MathConstants<double>::pi * f / sr, cw = std::cos (w0), sw = std::sin (w0);
    const double alpha = sw / (2.0 * q), A = std::pow (10.0, gainDb / 40.0);
    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
    switch (type)
    {
        case rbjLP:    b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = b0; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
        case rbjHP:    b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = b0; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
        case rbjBP:    b0 = alpha; b1 = 0; b2 = -alpha; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
        case rbjNotch: b0 = 1; b1 = -2 * cw; b2 = 1; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
        case rbjAllpass: b0 = 1 - alpha; b1 = -2 * cw; b2 = 1 + alpha; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
        case rbjBell:  b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A; a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A; break;
        case rbjLowShelf:
        {
            const double s = 2 * std::sqrt (A) * alpha;
            b0 = A * ((A + 1) - (A - 1) * cw + s); b1 = 2 * A * ((A - 1) - (A + 1) * cw); b2 = A * ((A + 1) - (A - 1) * cw - s);
            a0 = (A + 1) + (A - 1) * cw + s; a1 = -2 * ((A - 1) + (A + 1) * cw); a2 = (A + 1) + (A - 1) * cw - s; break;
        }
        case rbjHighShelf:
        {
            const double s = 2 * std::sqrt (A) * alpha;
            b0 = A * ((A + 1) + (A - 1) * cw + s); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - s);
            a0 = (A + 1) - (A - 1) * cw + s; a1 = 2 * ((A - 1) - (A + 1) * cw); a2 = (A + 1) - (A - 1) * cw - s; break;
        }
        default: break;
    }
    return { (float) (b0 / a0), (float) (b1 / a0), (float) (b2 / a0), (float) (a1 / a0), (float) (a2 / a0) };
}
inline double biquadMag (const BiquadCoefs& c, double f, double sr)
{
    const double w = 2.0 * juce::MathConstants<double>::pi * f / sr;
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
    const auto num = (double) c.b0 + (double) c.b1 * z1 + (double) c.b2 * z2;
    const auto den = 1.0 + (double) c.a1 * z1 + (double) c.a2 * z2;
    return std::abs (num / den);
}
struct Biquad
{
    float z1 = 0, z2 = 0;
    float process (float x, const BiquadCoefs& c) noexcept
    {
        const float y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2; z2 = c.b2 * x - c.a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0; }
    // engage after a pass-through stretch as if the filter had been passing the (slowly moving) signal unchanged: the first output
    // is x itself and the state is consistent with y = x, so a bypassed filter leaving its end stop does not step
    void warm (float x, const BiquadCoefs& c) noexcept { z1 = x * (1.0f - c.b0); z2 = x * (1.0f - c.b0 - c.b1 + c.a1); }
};
inline bool isIdentity (const BiquadCoefs& c) noexcept { return c.b0 == 1.0f && c.b1 == 0.0f && c.b2 == 0.0f && c.a1 == 0.0f && c.a2 == 0.0f; }
struct OnePole
{
    float a = 0, z = 0;
    void setLP (double sr, double hz) { a = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * juce::jlimit (1.0, sr * 0.49, hz) / sr); }
    float lp (float x) noexcept { z = x + a * (z - x); return z; }
    float hp (float x) noexcept { return x - lp (x); }
    void reset() { z = 0; }
};
struct DcBlock
{
    float x1 = 0, y1 = 0, r = 0.995f;
    void prepare (double sr) { r = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * 12.0 / sr); }
    float process (float x) noexcept { const float y = x - x1 + r * y1; x1 = x; y1 = y; return y; }
    void reset() { x1 = y1 = 0; }
};
// Zero-delay-feedback state variable filter (Simper/Cytomic form).
struct Svf
{
    float ic1 = 0, ic2 = 0, g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0;
    void set (double sr, double f, double q)
    {
        g = (float) std::tan (juce::MathConstants<double>::pi * juce::jlimit (8.0, sr * 0.46, f) / sr);
        k = (float) (1.0 / std::max (0.3, q));
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    void tick (float v0, float& lp, float& bp, float& hp) noexcept
    {
        const float v3 = v0 - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
        lp = v2; bp = v1; hp = v0 - k * v1 - v2;
    }
    void reset() { ic1 = ic2 = 0; }
};

// Raised-cosine window 0.5 - 0.5 cos(2 pi u) for u in [0, 1], from a table (no per-sample trig).
inline float hannWin (float u) noexcept
{
    static const std::array<float, 1025> t = [] { std::array<float, 1025> a {}; for (int i = 0; i <= 1024; ++i) a[(size_t) i] = 0.5f - 0.5f * (float) std::cos (2.0 * juce::MathConstants<double>::pi * i / 1024.0); return a; }();
    const float x = clampf (u, 0.0f, 1.0f) * 1024.0f; const int i = std::min (1023, (int) x); const float f = x - (float) i;
    return t[(size_t) i] + f * (t[(size_t) i + 1] - t[(size_t) i]);
}

// Fades the input of a delay line in after a reset, so freshly enabled delays never start with a hard edge.
struct InRamp
{
    int left = 0, len = 1;
    void start (int n) { len = std::max (1, n); left = len; }
    float next() noexcept { if (left <= 0) return 1.0f; return 1.0f - (float) (left--) / (float) len; }
};
// Sine/cosine oscillator by rotation (no per-sample trig).
struct Phasor
{
    double c = 1, s = 0, ci = 1, si = 0;
    void setFreq (double hz, double sr) { const double w = 2.0 * juce::MathConstants<double>::pi * hz / sr; ci = std::cos (w); si = std::sin (w); }
    void setPhase (double cycles) { c = std::cos (2.0 * juce::MathConstants<double>::pi * cycles); s = std::sin (2.0 * juce::MathConstants<double>::pi * cycles); }
    void step() noexcept { const double nc = c * ci - s * si; s = s * ci + c * si; c = nc; }
    void normalise() noexcept { const double m = 1.0 / std::sqrt (c * c + s * s); c *= m; s *= m; }
};

// Envelope follower (peak, separate attack/release).
struct EnvFollow
{
    float env = 0, at = 0, rt = 0;
    void set (double sr, float attMs, float relMs)
    {
        at = 1.0f - (float) std::exp (-1.0 / std::max (1.0, attMs * 0.001 * sr));
        rt = 1.0f - (float) std::exp (-1.0 / std::max (1.0, relMs * 0.001 * sr));
    }
    float process (float x) noexcept { x = std::abs (x); env += (x > env ? at : rt) * (x - env); return env; }
    void reset() { env = 0; }
};

// ---------------------------------------------------------------- per-block context handed to every unit
struct BlockCtx
{
    double sr = 48000, bpm = 120, ppq = 0, samplesPerBeat = 24000; bool playing = false;
    int noteOns = 0;          // note-ons the synth saw in this block (Ghosty Pro; 0 in Ghost Box): HYPER's RETRIG
    int noteOnAt = -1;        // sample offset (within this sub-block) of the first of them, -1 = none here: a sample-exact RETRIG
#if ! GPFX_BOX
    int lastNote = -1;        // FX RACK (round 3): the newest note-on's number so far (Ghosty Pro; -1 = none yet): DISTORTION's KEY TRACK
#else
    double beatsPerBar = 4.0; // Ghost Box: the host's time signature in beats (GHOST MOTION timeline bars)
#endif
    double ppqAt (int i) const noexcept { return ppq + (double) i / samplesPerBeat; }
};

// ---------------------------------------------------------------- the unit interface
// process() turns the buffer into the unit's 100%-wet signal; the rack does on/off fades and dry/wet mix
// (and delays the dry with delayDry() for units that have latency).
struct UnitDsp
{
    virtual ~UnitDsp() = default;
    virtual void attach (juce::AudioProcessorValueTreeState&) = 0;
    virtual void prepare (double sr, int maxBlock) = 0;
    virtual void reset() = 0;
    virtual void process (float* L, float* R, int n, const BlockCtx& ctx) = 0;
    virtual int latency() const { return 0; }
    virtual void delayDry (float*, float*, int) {}
    virtual bool selfMix() const { return false; }   // true: the unit applies its own mix (rack only fades on/off)
    virtual void idle (const float*, const float*, int) {}   // called instead of process() while the unit is fully off
    virtual void jumpSmoothing() {}   // Ghost Box MORPH: a time parameter jumped while the unit was ducked: skip the glide
    // empty every delay / latency line that stays in the signal path while the unit is off (its dry-delay line, lookahead):
    // Rack::reset() calls it so nothing old resumes after a reset. NOT part of reset(): reset() also runs when a unit switches on,
    // and its dry line must keep flowing then. Audio-thread safe (no allocation).
    virtual void clearLines() {}
};

// ---------------------------------------------------------------- audio -> editor telemetry (relaxed atomics, display only)
struct Telemetry
{
    std::atomic<float> gatePos { 0 }, gridPos { 0 }, hauntEnv { 0 }, hauntProgress { 0 };
    std::atomic<int> gatePattern { 0 }, gridPattern { 0 }, hauntPad { -1 }, gridActive { 0 };
    std::atomic<float> filterCut { 1000 }, filterLfo { 0 }, filterEnv { 0 };
    std::atomic<float> wreckIn { 0 }, wreckOut { 0 }, tubeLevel { 0 }, crushGr { 0 };
    std::atomic<float> echoMs { 0 }, echoLevel { 0 }, crystalLevel { 0 }, spaceLevel { 0 }, shimmerLevel { 0 }, chorusLevel { 0 };
    std::array<std::atomic<float>, 3> ottLevel {}, ottGain {};
    std::atomic<float> limGr { 0 }, outPeakL { 0 }, outPeakR { 0 }, inPeak { 0 };
    std::array<std::atomic<float>, kNumUnits> unitLevel {};
    std::atomic<float> echoIn { 0 };   // echo input peak, held until the echo page reads it (its bouncing ghosts)
    std::atomic<float> fatAmount { 0 }, fatGr { 0 }, fatLevel { 0 }, fatHeat { 0 };   // FATTEN (both racks); fatHeat: level into its ceiling (clip LED), held until read
    std::atomic<float> hyperLevel { 0 };                                               // HYPER / DIMENSION (Ghosty Pro)
#if ! GPFX_BOX
    std::atomic<float> distIn { 0 }, distOut { 0 }, distDrive { 0 };                  // FX RACK (round 3): DISTORTION
    std::atomic<float> compGr { 0 }; std::array<std::atomic<float>, 3> compBandGr {}; std::atomic<int> compPath { 0 };   // COMPRESSOR (dB, path 0 single 1 multi 2 limit)
    std::array<std::atomic<float>, 3> bandsLevel {};                                   // SPLITTER band peaks
#endif
#if GPFX_BOX
    std::atomic<float> retroLevel { 0 }, retroNoise { 0 }, retroWow { 0 }, retroDrop { 1 }, retroCrush { 0 };
    std::array<std::atomic<float>, 3> crackleBand {};
    std::atomic<float> crackleOut { 0 };
    std::atomic<float> hallsLevel { 0 }, hallsIn { 0 };
    std::atomic<float> warpPos { -1 }, warpSemis { 0 }, warpLagMs { 0 }, warpLevel { 0 };
    std::atomic<float> splitMask { 0 }, splitAtk { 0 }, splitBody { 0 };
    std::atomic<float> spectraLevel { 0 }, spectraTrans { 0 }, portalLevel { 0 }, portalShape { 0 }, formantVowel { 0 }, formantLevel { 0 }, pumpGain { 1 };
    std::atomic<float> bassF0 { 0 }, bassGrowlHz { 750 };   // measured low-band fundamental, current growl centre
    std::atomic<int> pumpKey { 0 };                         // SIDECHAIN mode: 0 no key connected, 1 connected but silent (input ducks itself), 2 live
    std::array<std::atomic<float>, 3> bassBand {};
    std::array<std::atomic<float>, 64> spectrum {};   // GHOST SPECTRA display (relaxed, drawing only)
#endif
};

} // namespace gpfx
