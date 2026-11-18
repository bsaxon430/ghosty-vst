// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST BREAK split sidechain. Each bay is split into LOW / MID / HIGH by LSP DSP Units' Crossover (LGPL-3.0: Linkwitz-Riley splits whose
// low band carries the matching all-pass, so the three bands sum back to the input's exact magnitude); every band has its own ducking
// envelope, driven by the drum lanes routed to it.
//   PUNCH  the envelope fires on the drum hits themselves (sample-accurate: the sequencer knows each hit before it sounds)
//   SMART  the envelope follows the routed drums' own level inside the band, so a band only goes down while the drums really fill it
// Per band: on/off, which drums trigger it, attack, hold, release, release shape. Per bay: the depth of each band. Global: crossover
// points and slope, lookahead (the audio is held back so the duck is already down when the hit lands), mix, SMART threshold.
// Audio thread: no allocation. LSP objects are built in prepare().
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

namespace gbk
{
constexpr int kBands = 3, kLanes = 6, kDuckChunk = 256;
inline const char* bandName (int k) { static const char* n[] { "LOW", "MID", "HIGH" }; return n[juce::jlimit (0, kBands - 1, k)]; }
inline const char* bandPrefix (int k) { static const char* p[] { "dl_", "dm_", "dh_" }; return p[juce::jlimit (0, kBands - 1, k)]; }
inline bool defaultRoute (int band, int lane) { return band == 0 ? lane == 0 : band == 1 ? (lane == 1 || lane == 4) : (lane == 2 || lane == 3 || lane == 5); }   // KICK > low, SNARE + PERC 1 > mid, the hats + PERC 2 > high

struct DuckBand { bool on = true; uint8_t route = 0; float attackMs = 1.5f, holdMs = 0, releaseMs = 120, shape = 0.5f; };
struct DuckP
{
    std::array<DuckBand, kBands> band {};
    float xLow = 160, xHigh = 5000, mix = 1, lookMs = 0, thrDb = -30; int slope = 1; bool smart = false;   // slope 0 LR2, 1 LR4, 2 LR8
    DuckP() { for (int k = 0; k < kBands; ++k) for (int l = 0; l < kLanes; ++l) if (defaultRoute (k, l)) band[(size_t) k].route |= (uint8_t) (1u << l); }
};

// one 3-band LSP split of a mono signal into three buffers (Duck.cpp: LSP's headers stay out of the JUCE translation units)
class Split3
{
public:
    Split3(); ~Split3();
    Split3 (const Split3&) = delete; Split3& operator= (const Split3&) = delete;
    void prepare (double sr);
    void set (float lo, float hi, int slope);   // slope 0 LR2, 1 LR4, 2 LR8
    void reset();
    void process (const float* in, float* const* out, int n);   // n <= kDuckChunk; out[k] gets band k
    static void initDsp();
private:
    struct Impl; Impl* d;
};

// the three envelopes (0 = no duck, 1 = full duck) for one chunk
class DuckEnvelopes
{
public:
    void prepare (double rate)
    {
        Split3::initDsp();
        sr = rate; for (auto& s : src) s.prepare (rate);
        for (auto& e : st) e = State(); for (auto& b : scratch) for (auto& c : b) c.fill (0.0f);
    }
    void reset() { for (auto& s : src) s.reset(); for (auto& e : st) e = State(); }
    // laneHit[l] = the first hit of lane l in this chunk (-1 none); drumAudio[l] = that lane's mono sound in this chunk (SMART)
    void process (const DuckP& P, const int* laneHit, const float* const* drumAudio, float* const* env, int n)
    {
        for (int k = 0; k < kBands; ++k) { src[k].set (P.xLow, P.xHigh, P.slope); }
        if (P.smart)
        {   // each band's detector hears only its routed drums, inside its own band
            for (int k = 0; k < kBands; ++k)
            {
                float* mix = scratch[3][0].data(); juce::FloatVectorOperations::clear (mix, n);
                for (int l = 0; l < kLanes; ++l) if ((P.band[(size_t) k].route >> l) & 1u) juce::FloatVectorOperations::add (mix, drumAudio[l], n);
                float* outs[kBands] { scratch[k][0].data(), scratch[k][1].data(), scratch[k][2].data() };
                src[k].process (mix, outs, n);
            }
        }
        const float thr = juce::Decibels::decibelsToGain (P.thrDb);
        for (int k = 0; k < kBands; ++k)
        {
            const auto& b = P.band[(size_t) k]; auto& s = st[(size_t) k]; float* e = env[k];
            const int atk = juce::jmax (1, (int) (b.attackMs * 0.001f * sr)), hold = (int) (b.holdMs * 0.001f * sr), rel = juce::jmax (1, (int) (b.releaseMs * 0.001f * sr));
            const float expo = std::pow (4.0f, 1.0f - 2.0f * juce::jlimit (0.0f, 1.0f, b.shape));   // 0 = slow-then-fast (4), 0.5 = straight (1), 1 = fast-then-slow (0.25)
            if (! b.on) { s.v = 0; juce::FloatVectorOperations::clear (e, n); continue; }
            if (P.smart)
            {
                const float* x = scratch[k][(size_t) k].data();
                const float ca = 1.0f - std::exp (-1.0f / (float) atk), cr = 1.0f - std::exp (-1.0f / (float) rel);
                for (int i = 0; i < n; ++i)
                {
                    const float a = std::abs (x[i]);
                    s.det += (a > s.det ? 0.25f : 0.0015f) * (a - s.det);                          // peak detector
                    const float over = s.det > thr ? juce::jlimit (0.0f, 1.0f, juce::Decibels::gainToDecibels (s.det / thr) / 12.0f) : 0.0f;   // 0 at the threshold, full 12 dB above it
                    if (over >= s.v) { s.v += ca * (over - s.v); s.holdLeft = hold; }
                    else if (s.holdLeft > 0) --s.holdLeft;
                    else s.v += cr * (over - s.v);
                    e[i] = s.v;
                }
                continue;
            }
            int hitAt = n;
            for (int l = 0; l < kLanes; ++l) if (((b.route >> l) & 1u) && laneHit[l] >= 0) hitAt = juce::jmin (hitAt, laneHit[l]);
            for (int i = 0; i < n; ++i)
            {
                if (i == hitAt) { s.phase = 0; s.from = s.v; s.pos = 0; }   // attack from wherever the envelope is (no jump)
                switch (s.phase)
                {
                    case 0: s.v = s.from + (1.0f - s.from) * (float) (++s.pos) / (float) atk; if (s.pos >= atk) { s.v = 1; s.phase = 1; s.pos = 0; } break;
                    case 1: if (++s.pos >= hold) { s.phase = 2; s.pos = 0; } break;
                    case 2: { const float r = (float) (++s.pos) / (float) rel; s.v = r >= 1.0f ? 0.0f : std::pow (1.0f - r, expo); if (r >= 1.0f) s.phase = 3; break; }
                    default: s.v = 0; break;
                }
                e[i] = s.v;
            }
        }
    }
private:
    struct State { int phase = 3, pos = 0, holdLeft = 0; float v = 0, from = 0, det = 0; };
    double sr = 48000; std::array<State, kBands> st {};
    Split3 src[kBands];
    std::array<std::array<std::array<float, kDuckChunk>, kBands>, 4> scratch {};
};
} // namespace gbk
