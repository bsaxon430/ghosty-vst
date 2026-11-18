#pragma once
// Ghost FX rack: reverb building blocks shared by SHIMMER, SPECTRAL SPACE and GHOST GRID's verb throw.
#include "FxCore.h"

namespace gpfx {

// Schroeder allpass diffuser with a fixed delay.
struct Allpass
{
    Ring r; int d = 1; float g = 0.6f;
    void prepare (int delay, float gain) { d = std::max (1, delay); g = gain; r.prepare (d + 4); }
    void clear() { r.clear(); }
    float process (float x) noexcept
    {
        const float z = r.at (d - 1);
        const float v = x + g * z;
        r.push (v);
        return z - g * v;
    }
};

// Two-tap granular pitch shifter (delay-line based, sin^2 crossfade keeps the level constant).
struct PitchShifter
{
    Ring buf; double ph = 0; float win = 2048; float ratio = 2.0f;
    void prepare (double sr, float windowMs = 60.0f) { win = (float) (sr * windowMs * 0.001); buf.prepare ((int) win * 2 + 16); ph = 0; }
    void clear() { buf.clear(); ph = 0; }
    void setSemis (float st) { ratio = std::pow (2.0f, st / 12.0f); }
    float process (float x) noexcept
    {
        buf.push (x);
        const double step = (1.0 - (double) ratio) / (double) win;   // delay change per sample, in window units
        ph += step; ph -= std::floor (ph);
        const double p2 = ph + 0.5 - std::floor (ph + 0.5);
        const float d1 = (float) (ph * win) + 1.0f, d2 = (float) (p2 * win) + 1.0f;
        // sin^2(pi p) == hann(p): the two taps' windows always sum to 1
        return buf.readLinear (d1) * hannWin ((float) ph) + buf.readLinear (d2) * hannWin ((float) p2);
    }
};

// 8-line feedback delay network with Hadamard mixing, per-line damping and modulation, plus freeze.
struct Fdn8
{
    static constexpr int N = 8;
    std::array<Ring, N> lines; std::array<OnePole, N> damp; std::array<float, N> g {}; std::array<Phasor, N> lfo;
    std::array<Allpass, 4> diffL, diffR;
    double sr = 48000; float maxScale = 2.4f;
    ExpSmooth sizeSm, freezeSm;
    float t60 = 2.0f, dampHz = 8000, modDepth = 0, modRate = 0.3f;
    bool diffuse = true;

    static float baseMs (int i) { static const float b[N] { 29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 59.9f, 67.1f, 73.3f }; return b[i]; }

    void prepare (double s, float maxSizeScale = 2.4f)
    {
        sr = s; maxScale = maxSizeScale;
        for (int i = 0; i < N; ++i) { lines[(size_t) i].prepare ((int) (baseMs (i) * maxScale * 0.001 * sr) + 256); lfo[(size_t) i].setPhase (i * 0.137); }
        const float apMs[4] { 4.77f, 3.59f, 12.73f, 9.31f };
        for (int k = 0; k < 4; ++k)
        {
            diffL[(size_t) k].prepare ((int) (apMs[k] * 0.001 * sr), 0.62f);
            diffR[(size_t) k].prepare ((int) (apMs[k] * 1.13f * 0.001 * sr), 0.62f);
        }
        sizeSm.setTime (sr, 250.0f); sizeSm.snap (1.0f);
        freezeSm.setTime (sr, 60.0f); freezeSm.snap (0.0f);
        clear();
    }
    void clear()
    {
        for (auto& l : lines) l.clear();
        for (auto& d : damp) d.reset();
        for (auto& a : diffL) a.clear();
        for (auto& a : diffR) a.clear();
    }
    // size 0..1 maps to 0.25..maxScale; decaySec = T60; damping cutoff in Hz; mod 0..1; freeze bool
    void set (float size01, float decaySec, float dampingHz, float mod, bool freeze)
    {
        sizeSm.target = 0.25f + clampf (size01, 0.0f, 1.0f) * (maxScale - 0.25f);
        t60 = std::max (0.05f, decaySec); dampHz = dampingHz; modDepth = mod;
        freezeSm.target = freeze ? 1.0f : 0.0f;
        for (int i = 0; i < N; ++i) damp[(size_t) i].setLP (sr, dampHz);
        updateGains (sizeSm.cur);   // the feedback gains always match the decay (units may call updateGains again per block)
    }
    void updateGains (float scale)
    {
        for (int i = 0; i < N; ++i) { lfo[(size_t) i].setFreq (modRate * (1.0 + 0.13 * i), sr); lfo[(size_t) i].normalise(); }
        for (int i = 0; i < N; ++i)
        {
            const double len = baseMs (i) * scale * 0.001;   // seconds
            g[(size_t) i] = (float) std::pow (10.0, -3.0 * len / t60);
        }
    }
    // inject: extra mono signal added into every line (shimmer feedback). Outputs are the wet signal only.
    void process (float inL, float inR, float& outL, float& outR, float inject = 0.0f) noexcept
    {
        const float scale = sizeSm.next(), fz = freezeSm.next();
        if (diffuse) for (int k = 0; k < 4; ++k) { inL = diffL[(size_t) k].process (inL); inR = diffR[(size_t) k].process (inR); }
        std::array<float, N> v;
        const double modS = modDepth * sr * 0.0012, msr = scale * 0.001 * sr;
        for (int i = 0; i < N; ++i)
        {
            auto& o = lfo[(size_t) i]; o.step();
            const double d = baseMs (i) * msr + modS * o.s + 2.0;
            float x = lines[(size_t) i].readLinear (d);
            const float dl = damp[(size_t) i].lp (x);
            x = dl + fz * (x - dl);   // no damping while frozen
            v[(size_t) i] = x;
        }
        outL = (v[0] - v[2] + v[4] - v[6]) * 0.5f + (v[1] - v[5]) * 0.25f;
        outR = (v[1] - v[3] + v[5] - v[7]) * 0.5f + (v[2] - v[6]) * 0.25f;
        std::array<float, N> u;
        for (int i = 0; i < N; ++i) { const float gi = g[(size_t) i]; u[(size_t) i] = v[(size_t) i] * (gi + fz * (1.0f - gi)); }
        for (int len = 1; len < N; len <<= 1)
            for (int j = 0; j < N; j += len << 1)
                for (int k = j; k < j + len; ++k) { const float a = u[(size_t) k], b = u[(size_t) (k + len)]; u[(size_t) k] = a + b; u[(size_t) (k + len)] = a - b; }
        const float norm = 0.35355339f, ing = 1.0f - fz;
        for (int i = 0; i < N; ++i)
        {
            const float in = ((i & 1) ? inR : inL) * ((i & 2) ? -1.0f : 1.0f) * 0.5f + inject * 0.35f;
            lines[(size_t) i].push (u[(size_t) i] * norm + in * ing);
        }
    }
};

} // namespace gpfx
