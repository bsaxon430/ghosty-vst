// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX SPATIAL (Phase 6b, SPATIAL.md): the routing around Steam Audio.
// Each placed unit stays where it is in its chain and is fed from the signal at that place. Its slot computes a latency-aligned dry
// and the unit's WET part (trim x effect x mix; the mix knob is the wet level). In SPATIAL mode the chain carries on with the dry, as
// if the unit were bypassed (so the units after it still see the signal at that place), and the wet goes to a bus, delayed so it lines up with the end of the chain,
// placed in 3D by Steam Audio, and summed back in before GHOST LIMITER, which stays last. Dry stays centre.
// A unit at distance 0 (on the head, the default) is not placed: its wet stays in the chain and nothing here runs for it. Between
// 0 and kPlaceRamp metres the wet crossfades from the chain to the bus, so dragging off the head never clicks.
// Steam Audio runs on fixed 128-sample frames: SPATIAL adds exactly 128 samples of latency while it is switched on (reported to the
// host); everything else is bit-exact.
#pragma once
#include "SteamVoice.h"
#include "../dsp/DelayLine.h"
#include "../dsp/Smoothing.h"
#include "../effects/Catalog.h"
#include <array>
#include <cmath>

namespace phantom
{
// GHOST SQUEEZE and GHOST LIMITER work on the whole mix (the master stage): they have no tile
inline bool spatialEligible (int u) noexcept { return u >= 0 && u < kNumUnits && u != uLim && u != uOtt; }
constexpr float kSpatialMaxDistance = 20.0f;   // metres at the sphere's edge
constexpr float kPlaceRamp = 0.5f;             // metres: 0 = in the chain, kPlaceRamp and beyond = fully placed

// One 2 : 1 stage of the SPATIAL stage's rate change (0.1.10): a linear-phase half-band FIR whose coefficients come from JUCE's own
// equiripple half-band designer (juce::dsp::FilterDesign); down by two, or up by two. Each has a whole-sample delay of
// (taps - 1) / 2 at the higher rate, so the stage's latency is exact.
struct HalfBandTaps
{
    std::vector<float> h; std::vector<int> nz;   // the taps, and which of them are not zero (every second one is, but for the middle)
    void design (float transition, float dB)
    {
        auto c = juce::dsp::FilterDesign<float>::designFIRLowpassHalfBandEquirippleMethod (transition, dB);
        const float* raw = c->getRawCoefficients(); const int n = (int) c->getFilterOrder() + 1; h.assign (raw, raw + n);
        if ((n & 1) == 0) h.push_back (0.0f);   // (an odd count: a whole-sample delay)
        nz.clear(); for (int i = 0; i < (int) h.size(); ++i) if (std::abs (h[(size_t) i]) > 1.0e-9f) nz.push_back (i);
    }
    int taps() const noexcept { return (int) h.size(); }
    int delay() const noexcept { return (taps() - 1) / 2; }   // in samples of the higher rate
};
struct HalfDown
{
    const HalfBandTaps* t = nullptr; std::vector<float> x;
    void prepare (const HalfBandTaps& taps, int maxIn) { t = &taps; x.assign ((size_t) (taps.taps() - 1 + maxIn), 0.0f); }
    void reset() noexcept { std::fill (x.begin(), x.end(), 0.0f); }
    void process (const float* in, int nIn, float* out) noexcept   // nIn even -> nIn / 2 samples
    {
        const int N = t->taps(); std::copy (in, in + nIn, x.begin() + (N - 1));
        for (int k = 0; k < nIn / 2; ++k) { const float* p = x.data() + (N - 1) + 2 * k; float acc = 0.0f; for (int j : t->nz) acc += t->h[(size_t) j] * p[-j]; out[k] = acc; }
        std::copy (x.begin() + nIn, x.begin() + nIn + (N - 1), x.begin());
    }
};
struct HalfUp
{
    const HalfBandTaps* t = nullptr; std::vector<float> x; int hist = 0;
    void prepare (const HalfBandTaps& taps, int maxIn) { t = &taps; hist = taps.taps() / 2 + 1; x.assign ((size_t) (hist + maxIn), 0.0f); }
    void reset() noexcept { std::fill (x.begin(), x.end(), 0.0f); }
    void process (const float* in, int nIn, float* out) noexcept   // nIn -> 2 nIn samples
    {
        std::copy (in, in + nIn, x.begin() + hist);
        for (int m = 0; m < 2 * nIn; ++m)
        {   // y[m] = 2 * sum over taps j of the same parity as m of h[j] * in[(m - j) / 2]
            float acc = 0.0f; for (int j : t->nz) if (((m - j) & 1) == 0) acc += t->h[(size_t) j] * x[(size_t) (hist + (m - j) / 2)];   // (m - j may be negative and even: the shift floors it, as wanted)
            out[m] = 2.0f * acc;
        }
        std::copy (x.begin() + nIn, x.begin() + nIn + hist, x.begin());
    }
};

class SpatialEngine
{
public:
    static constexpr int kFrame = 128;   // the renderer's frame at ITS rate (44.1 or 48 kHz)
    // the whole stage's latency in host samples: one frame, plus the rate change's filters when the project runs at 2x or 4x the
    // renderer's rate (88.2 / 96 / 176.4 / 192 kHz)
    int latency() const noexcept { return hostFrame + resampleDelay; }
    int rateRatio() const noexcept { return ratio; }

    void prepare (double sampleRate, int maxBlockSize)
    {
        sr = sampleRate;
        // the HRTF set has 44.1 and 48 kHz: a project at twice or four times one of those renders at that rate, each placed
        // unit's wet brought down and the summed result brought back up through half-band filters
        const int isr = (int) std::lround (sampleRate); ratio = (isr == 88200 || isr == 96000) ? 2 : (isr == 176400 || isr == 192000) ? 4 : 1;
        hostFrame = kFrame * ratio; resampleDelay = 0;
        renderer.prepare (isr / ratio, kFrame, kNumUnits);
        if (ratio > 1)
        {
            // stage A works at the host rate (keeps 0-20 kHz, rejects what would fold onto it), stage B (4x only) between 2x and 1x
            const float base = (float) (isr / ratio), top = 20000.0f;
            const float trOuter = 2.0f * (0.25f - top / (float) isr), trInner = 2.0f * (0.25f - top / (2.0f * base));
            if (ratio == 2) { tapsA.design (trInner, -96.0f); resampleDelay = 2 * tapsA.delay(); }
            else { tapsA.design (juce::jmin (0.4f, trOuter), -96.0f); tapsB.design (trInner, -96.0f); resampleDelay = 2 * tapsA.delay() + 4 * tapsB.delay(); }
            for (int u = 0; u < kNumUnits; ++u) for (int c = 0; c < 2; ++c) { downA[(size_t) u][c].prepare (tapsA, hostFrame); if (ratio == 4) downB[(size_t) u][c].prepare (tapsB, hostFrame / 2); }
            for (int c = 0; c < 2; ++c) { if (ratio == 4) { upB[c].prepare (tapsB, kFrame); upA[c].prepare (tapsA, 2 * kFrame); } else upA[c].prepare (tapsA, kFrame); }
            baseIn.setSize (2, kFrame); baseOut.setSize (2, kFrame); mid.setSize (2, hostFrame);
        }
        for (int u = 0; u < kNumUnits; ++u)
        {
            bus[(size_t) u].setSize (2, juce::jmax (1, maxBlockSize)); inFifo[(size_t) u].setSize (2, hostFrame);
            place[(size_t) u].prepare (sampleRate, 30.0f, placeTarget (target[(size_t) u].dist), false);
            if (cap[(size_t) u] == 0) { busDelay[(size_t) u].prepare (4096, 2); cap[(size_t) u] = 4096; }
        }
        outFifo.setSize (2, hostFrame); mainDelay.prepare (hostFrame + resampleDelay, 2); mainDelay.setDelay (hostFrame + resampleDelay);
        smoothA = 1.0f - std::exp (-(float) hostFrame / (float) (sampleRate * 0.025));
        reset();
    }
    bool ready() const noexcept { return renderer.ready(); }
    void reset() noexcept
    {
        for (int u = 0; u < kNumUnits; ++u) { inFifo[(size_t) u].clear(); busDelay[(size_t) u].reset(); live[(size_t) u] = 0; renderer.resetVoice (u); snapped[(size_t) u] = false; }
        outFifo.clear(); mainDelay.reset(); pos = 0; anyOut = false; upTail = 0;
        if (ratio > 1) { for (auto& v : downA) for (auto& d : v) d.reset(); for (auto& v : downB) for (auto& d : v) d.reset(); for (auto& d : upA) d.reset(); for (auto& d : upB) d.reset(); }
    }

    // message thread under the callback lock (applyTopology): the delay that lines unit u's wet up with the end of the chain
    void setBusDelay (int u, int samples)
    {
        if (samples + 1 > cap[(size_t) u]) { int c = 4096; while (c < samples + 1) c <<= 1; busDelay[(size_t) u].prepare (c, 2); cap[(size_t) u] = c; }
        busDelay[(size_t) u].setDelay (samples);
    }

    // audio thread: where unit u is wanted this block (azimuth / elevation in degrees, distance in metres)
    void setTarget (int u, float azDeg, float elDeg, float distM) noexcept
    {
        auto& t = target[(size_t) u]; t.az = azDeg; t.el = elDeg; t.dist = juce::jlimit (0.0f, kSpatialMaxDistance, distM);
        place[(size_t) u].setTarget (placeTarget (t.dist));
    }
    void setSpeakers (bool s) noexcept { speakers = s; }
    bool wants (int u) const noexcept { const auto& p = place[(size_t) u]; return p.current != 0.0f || p.target != 0.0f || p.mid != 0.0f; }
    void skip (int u, int n) noexcept { place[(size_t) u].skip (n); }

    // audio thread: the buffer unit u's slot writes its wet into (EffectSlot::setWetTap) this block
    float* tapPtr (int u, int c) noexcept { return bus[(size_t) u].getWritePointer (c); }
    // audio thread: after unit u ran in place on `out` (its aligned dry in `dry`, its wet in the tap): by the placement weight p the chain
    // goes on with the dry (as if the unit were bypassed) and the wet goes to the bus. p = 0 leaves the chain exactly as it was.
    void tap (int u, juce::dsp::AudioBlock<float> out, juce::dsp::AudioBlock<float> dry, int n, float laneGain) noexcept
    {
        auto& b = bus[(size_t) u]; auto& sm = place[(size_t) u];
        float* bl = b.getWritePointer (0); float* br = b.getWritePointer (1);
        float* ol = out.getChannelPointer (0); float* orr = out.getChannelPointer (1);
        const float* dl = dry.getChannelPointer (0); const float* dr = dry.getChannelPointer (1);
        float pk = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float p = sm.next();
            ol[i] += (dl[i] - ol[i]) * p; orr[i] += (dr[i] - orr[i]) * p;
            bl[i] *= p * laneGain; br[i] *= p * laneGain;
            pk = juce::jmax (pk, std::abs (bl[i]), std::abs (br[i]));
        }
        juce::dsp::AudioBlock<float> bb (b); busDelay[(size_t) u].process (bb.getSubBlock (0, (size_t) n));
        used[(size_t) u] = true; if (pk > kQuiet) heard[(size_t) u] = true;
    }

    // audio thread, once per block before the limiter: delay the chain by one frame, run the frames, add the placed wets
    void finish (juce::dsp::AudioBlock<float> block, int n) noexcept
    {
        mainDelay.process (block);
        float* L = block.getChannelPointer (0); float* R = block.getChannelPointer (1);
        for (int u = 0; u < kNumUnits; ++u) if (heard[(size_t) u]) live[(size_t) u] = kLiveFrames + 1;   // + the frame in progress
        int i = 0;
        while (i < n)
        {
            const int c = juce::jmin (n - i, hostFrame - pos);
            for (int u = 0; u < kNumUnits; ++u)
            {
                if (live[(size_t) u] <= 0) continue;
                auto& f = inFifo[(size_t) u];
                if (used[(size_t) u]) { f.copyFrom (0, pos, bus[(size_t) u], 0, i, c); f.copyFrom (1, pos, bus[(size_t) u], 1, i, c); }
                else { f.clear (0, pos, c); f.clear (1, pos, c); }
            }
            if (anyOut) { const float* ol = outFifo.getReadPointer (0); const float* orr = outFifo.getReadPointer (1); for (int k = 0; k < c; ++k) { L[i + k] += ol[pos + k]; R[i + k] += orr[pos + k]; } }
            pos += c; i += c;
            if (pos == hostFrame) { renderFrame(); pos = 0; }
        }
        for (int u = 0; u < kNumUnits; ++u) { used[(size_t) u] = false; heard[(size_t) u] = false; }
    }
    // for the UI / tests: how many voices rendered in the last frame
    int voicesLastFrame() const noexcept { return lastVoices; }

private:
    static float placeTarget (float d) noexcept { return d <= 0.0f ? 0.0f : juce::jmin (1.0f, d / kPlaceRamp); }
    static constexpr float kQuiet = 1.0e-7f;   // -140 dBFS
    static constexpr int kLiveFrames = 3;      // a voice keeps rendering 3 frames after its wet went silent (the HRTF tail), then sleeps
    struct Pos { float az = 0, el = 0, dist = 0; };
    struct Cur { float x = 0, y = 0, z = -1, d = 0; };

    // a voice's frame at the host rate -> the renderer's rate
    const float* down (int u, int c) noexcept
    {
        const float* in = inFifo[(size_t) u].getReadPointer (c); if (ratio == 1) return in;
        if (ratio == 2) { downA[(size_t) u][c].process (in, hostFrame, baseIn.getWritePointer (c)); return baseIn.getReadPointer (c); }
        downA[(size_t) u][c].process (in, hostFrame, mid.getWritePointer (c)); downB[(size_t) u][c].process (mid.getReadPointer (c), hostFrame / 2, baseIn.getWritePointer (c)); return baseIn.getReadPointer (c);
    }
    void renderFrame() noexcept
    {
        outFifo.clear(); int voices = 0; auto& mix = ratio == 1 ? outFifo : baseOut; if (ratio > 1) baseOut.clear();
        for (int u = 0; u < kNumUnits; ++u)
        {
            if (live[(size_t) u] <= 0) continue;
            const auto& t = target[(size_t) u];
            const float az = juce::degreesToRadians (t.az), el = juce::degreesToRadians (t.el);
            const float tx = std::sin (az) * std::cos (el), ty = std::sin (el), tz = -std::cos (az) * std::cos (el);
            auto& c = cur[(size_t) u];
            if (! snapped[(size_t) u]) { c = { tx, ty, tz, juce::jmax (t.dist, 0.0f) }; snapped[(size_t) u] = true; }
            else { glide (c, tx, ty, tz); c.d += smoothA * (t.dist - c.d); }
            const float* vl = down (u, 0); const float* vr = down (u, 1);
            renderer.render (u, vl, vr, c.x, c.y, c.z, c.d, speakers, mix.getWritePointer (0), mix.getWritePointer (1));
            ++voices;
            if (--live[(size_t) u] == 0) { renderer.resetVoice (u); snapped[(size_t) u] = false; if (ratio > 1) for (int ch = 0; ch < 2; ++ch) { downA[(size_t) u][ch].reset(); downB[(size_t) u][ch].reset(); } }
        }
        if (ratio > 1)
        {   // the summed render back up to the host rate; the up-filters ring on for a few frames after the last voice went quiet
            if (voices > 0) upTail = 1 + (resampleDelay + hostFrame - 1) / hostFrame;
            if (voices > 0 || upTail > 0)
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    if (ratio == 2) upA[ch].process (baseOut.getReadPointer (ch), kFrame, outFifo.getWritePointer (ch));
                    else { upB[ch].process (baseOut.getReadPointer (ch), kFrame, mid.getWritePointer (ch)); upA[ch].process (mid.getReadPointer (ch), 2 * kFrame, outFifo.getWritePointer (ch)); }
                }
                if (voices == 0) --upTail;
                anyOut = true; lastVoices = voices; return;
            }
        }
        anyOut = voices > 0; lastVoices = voices;
    }

    // the direction turns towards its target along the sphere (a share of the angle per frame), never through the head
    void glide (Cur& c, float tx, float ty, float tz) const noexcept
    {
        float dot = juce::jlimit (-1.0f, 1.0f, c.x * tx + c.y * ty + c.z * tz);
        if (dot > 0.99999f) { c.x = tx; c.y = ty; c.z = tz; return; }
        if (dot < -0.999f) { const float ox = -c.z, oz = c.x, n = std::sqrt (ox * ox + oz * oz);   // straight behind: go round by the side
                             if (n > 1.0e-4f) { tx = ox / n; ty = 0.0f; tz = oz / n; } else { tx = 1.0f; ty = 0.0f; tz = 0.0f; } dot = c.x * tx + c.y * ty + c.z * tz; }
        const float th = std::acos (dot), s = std::sin (th), a = std::sin ((1.0f - smoothA) * th) / s, b = std::sin (smoothA * th) / s;
        float x = a * c.x + b * tx, y = a * c.y + b * ty, z = a * c.z + b * tz; const float n = std::sqrt (x * x + y * y + z * z);
        c.x = x / n; c.y = y / n; c.z = z / n;
    }
    spatial::Renderer renderer;
    double sr = 48000.0; float smoothA = 0.1f; bool speakers = false;
    std::array<juce::AudioBuffer<float>, kNumUnits> bus, inFifo;
    std::array<IntegerDelay, kNumUnits> busDelay; std::array<int, kNumUnits> cap {};
    std::array<ParamSmoother, kNumUnits> place;
    std::array<Pos, kNumUnits> target {}; std::array<Cur, kNumUnits> cur {};
    std::array<bool, kNumUnits> used {}, heard {}, snapped {}; std::array<int, kNumUnits> live {};
    juce::AudioBuffer<float> outFifo; IntegerDelay mainDelay; int pos = 0, lastVoices = 0; bool anyOut = false;
    int ratio = 1, hostFrame = kFrame, resampleDelay = 0, upTail = 0; HalfBandTaps tapsA, tapsB;
    std::array<std::array<HalfDown, 2>, kNumUnits> downA, downB; std::array<HalfUp, 2> upA, upB; juce::AudioBuffer<float> baseIn, baseOut, mid;
};
} // namespace phantom
