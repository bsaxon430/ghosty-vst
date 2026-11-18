// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX shared DSP: LFO, envelope follower and step sequencer. Every rhythmic one phase-locks to the host clock (TempoInfo),
// so two modules synced to 1/4 are sample-locked to each other and to the song over any length of time and any tempo change.
#pragma once
#include "../core/PhantomEffect.h"
#include <random>

namespace phantom
{
struct Division { const char* name; double beats; };
// the tempo divisions every rhythmic control shares (same list and order as Ghost Box's ECTO ECHO / LFO divisions)
inline const std::vector<Division>& divisions()
{
    static const std::vector<Division> d { { "1/64", 0.0625 }, { "1/32", 0.125 }, { "1/16T", 1.0 / 6.0 }, { "1/16", 0.25 }, { "1/16D", 0.375 },
                                           { "1/8T", 1.0 / 3.0 }, { "1/8", 0.5 }, { "1/8D", 0.75 }, { "1/4T", 2.0 / 3.0 }, { "1/4", 1.0 },
                                           { "1/4D", 1.5 }, { "1/2T", 4.0 / 3.0 }, { "1/2", 2.0 }, { "1/2D", 3.0 }, { "1/1", 4.0 }, { "2/1", 8.0 } };
    return d;
}
inline std::vector<std::string> divisionNames() { std::vector<std::string> n; for (auto& d : divisions()) n.push_back (d.name); return n; }

// phase 0..1 of a cycle `beats` long at song position ppq (host-locked: no accumulated drift)
inline double hostPhase (double ppq, double beats, double phaseOffset = 0.0) noexcept
{
    const double c = ppq / juce::jmax (1.0e-9, beats) + phaseOffset;
    return c - std::floor (c);
}

enum class LfoShape { Sine, Triangle, SawUp, SawDown, Square, SampleHold, Smooth };
inline const std::vector<std::string>& lfoShapeNames() { static const std::vector<std::string> n { "SINE", "TRI", "SAW UP", "SAW DOWN", "SQUARE", "S&H", "SMOOTH" }; return n; }

class Lfo
{
public:
    void prepare (double sampleRate) noexcept { sr = sampleRate; reset(); }
    void reset() noexcept { freePhase = 0.0; shLast = 0.0f; shNext = (float) rng01(); lastCycle = -1; }
    void setShape (LfoShape s) noexcept { shape = s; }
    void setSync (bool on, int divisionIndex) noexcept { synced = on; div = juce::jlimit (0, (int) divisions().size() - 1, divisionIndex); }
    void setRateHz (float hz) noexcept { rateHz = hz; }

    // call once per block with the host clock; returns the value for sample `i` of the block (-1..1, unipolar via (v+1)/2)
    void beginBlock (const TempoInfo& t) noexcept
    {
        tempo = t;
        if (synced && t.playing) { freePhase = hostPhase (t.ppqPosition, divisions()[(size_t) div].beats); }
        else if (synced) { /* stopped: free-run at the division's rate so it still moves */ }
    }
    inline float next() noexcept
    {
        double ph = freePhase;
        const double inc = synced ? 1.0 / (divisions()[(size_t) div].beats * tempo.samplesPerBeat()) : rateHz / sr;
        freePhase += inc; if (freePhase >= 1.0) freePhase -= std::floor (freePhase);
        const int cycle = (int) std::floor (freePhase - inc) ; juce::ignoreUnused (cycle);
        if (freePhase < ph) { shLast = shNext; shNext = (float) rng01(); }   // wrapped: new random step
        return value (ph);
    }
    float value (double ph) const noexcept
    {
        switch (shape)
        {
            case LfoShape::Sine: return (float) std::sin (ph * juce::MathConstants<double>::twoPi);
            case LfoShape::Triangle: return (float) (1.0 - 4.0 * std::abs (ph - 0.5));
            case LfoShape::SawUp: return (float) (2.0 * ph - 1.0);
            case LfoShape::SawDown: return (float) (1.0 - 2.0 * ph);
            case LfoShape::Square: return ph < 0.5 ? 1.0f : -1.0f;
            case LfoShape::SampleHold: return shNext * 2.0f - 1.0f;
            case LfoShape::Smooth: { const float a = (float) (0.5 - 0.5 * std::cos (ph * juce::MathConstants<double>::pi)); return (shLast + (shNext - shLast) * a) * 2.0f - 1.0f; }
        }
        return 0.0f;
    }
private:
    double rng01() noexcept { return dist (rng); }
    double sr = 48000.0, freePhase = 0.0, rateHz = 1.0; bool synced = true; int div = 9; LfoShape shape = LfoShape::Sine;
    float shLast = 0.0f, shNext = 0.0f; int lastCycle = -1; TempoInfo tempo;
    std::minstd_rand rng { 1234u }; std::uniform_real_distribution<double> dist { 0.0, 1.0 };
};

// Peak / RMS envelope follower with attack and release in ms (one per channel or on the stereo max)
class EnvelopeFollower
{
public:
    void prepare (double sampleRate) noexcept { sr = sampleRate; setTimes (attackMs, releaseMs); reset(); }
    void reset() noexcept { env = 0.0f; }
    void setTimes (float atkMs, float relMs) noexcept
    {
        attackMs = atkMs; releaseMs = relMs;
        a = 1.0f - std::exp (-1.0f / (float) (sr * juce::jmax (0.01f, atkMs) * 0.001));
        r = 1.0f - std::exp (-1.0f / (float) (sr * juce::jmax (0.01f, relMs) * 0.001));
    }
    inline float next (float x) noexcept
    {
        const float v = std::abs (x);
        env += (v > env ? a : r) * (v - env);
        return env;
    }
    float current() const noexcept { return env; }
private:
    double sr = 48000.0; float attackMs = 5.0f, releaseMs = 120.0f, a = 0.1f, r = 0.001f, env = 0.0f;
};

// Step sequencer locked to the host: `steps` values over `beats` beats (default one bar), optional glide between steps
class StepSequencer
{
public:
    static constexpr int kMaxSteps = 64;
    void prepare (double sampleRate) noexcept { sr = sampleRate; reset(); }
    void reset() noexcept { pos = 0.0; cur = 0.0f; }
    void setLength (int numSteps, double lengthBeats) noexcept { steps = juce::jlimit (1, kMaxSteps, numSteps); beats = lengthBeats; }
    void setStep (int i, float v) noexcept { if (i >= 0 && i < kMaxSteps) values[(size_t) i] = v; }
    void setGlideMs (float ms) noexcept { glide = 1.0f - std::exp (-1.0f / (float) (sr * juce::jmax (0.01f, ms) * 0.001)); }
    void beginBlock (const TempoInfo& t) noexcept { tempo = t; if (t.playing) pos = hostPhase (t.ppqPosition, beats); }
    inline float next() noexcept
    {
        const int s = juce::jlimit (0, steps - 1, (int) (pos * steps));
        pos += 1.0 / (beats * tempo.samplesPerBeat()); if (pos >= 1.0) pos -= 1.0;
        cur += glide * (values[(size_t) s] - cur);
        return cur;
    }
    int currentStep() const noexcept { return juce::jlimit (0, steps - 1, (int) (pos * steps)); }
private:
    double sr = 48000.0, pos = 0.0, beats = 4.0; int steps = 16; float cur = 0.0f, glide = 1.0f;
    std::array<float, kMaxSteps> values {}; TempoInfo tempo;
};
} // namespace phantom
