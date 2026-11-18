// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX 21 GHOST SPECTRA: a spectral engine on a short-time Fourier transform (JUCE's FFT: Apple's vDSP on the Mac).
// 2048-point frames, sqrt-Hann analysis and synthesis windows, 75 % overlap (87.5 % with HQ BOUNCE), overlap-add; latency exactly 2048
// samples, reported. At its neutral setting the overlap-add rebuilds the input exactly (checked).
// Per bin: freeze (a phase-vocoder hold: each bin keeps its own frequency), smear (magnitudes blurred over time and across neighbouring
// bins), scatter (bins swapped with random neighbours and slowly re-dealt: SpectralSuite's Bin Scrambler idea, public domain), shift and
// pitch (bins moved by a ratio; phases follow each bin's frequency), bin shift (a linear move in Hz), phase randomise, tilt, a threshold
// gate, motion (a host-synced sweep of smear and tilt), and GHOST, which lets transient frames (hits) through less processed.
// The brief's engines: SpectralSuite's processors are a few lines each on top of its own threaded FFT plugin shell (the wrapper would be
// larger than the algorithms); PaulXStretch's spectral functions are GPL-2.0-only by their file header, so cannot be combined with this
// AGPL-3 build. This is the brief's fallback, "own STFT on shared FFT" (DECISIONS.md 23).
#pragma once
#include "SpaceCommon.h"
#include <random>

namespace phantom
{
class GhostSpectraFx final : public CatalogEffect
{
public:
    GhostSpectraFx() : CatalogEffect (uSpectra) {}
    static constexpr int kOrder = 11, kN = 1 << kOrder, kBins = kN / 2 + 1;
    int getLatencySamples() const override { return kN; }
    double tailSeconds() const override { return 4.0; }
    void neutralise (std::vector<float>& v) const override
    {
        for (auto* k : { "smear", "scatter", "shift", "ghost", "motion", "tilt", "pitch", "phase", "binshift" }) v[(size_t) id (k)] = 0.0f;
        v[(size_t) id ("thresh")] = -90.0f; v[(size_t) id ("freeze")] = 0.0f;
    }
protected:
    void prepareEffect (double, int, int) override
    {
        pFreeze = id ("freeze"); pSmear = id ("smear"); pScatter = id ("scatter"); pShift = id ("shift"); pGhost = id ("ghost"); pMotion = id ("motion"); pMdiv = id ("mdiv");
        pTilt = id ("tilt"); pHq = id ("hq"); pFsync = id ("fsync"); pThresh = id ("thresh"); pPitch = id ("pitch"); pPhase = id ("phase"); pBinshift = id ("binshift");
        for (int i = 0; i < kN; ++i) win[(size_t) i] = std::sqrt (0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) kN));   // periodic sqrt-Hann
        for (int k = 0; k < kBins; ++k) { scatA[(size_t) k] = scatB[(size_t) k] = k; }
        rng.seed (4242); resetEffect();
    }
    void resetEffect() override
    {
        for (auto& c : ch) { c.in.fill (0.0f); c.out.fill (0.0f); c.prevMag.fill (0.0f); c.smMag.fill (0.0f); c.frzMag.fill (0.0f); c.frx.fill (1.0f); c.fry.fill (0.0f); c.pux.fill (1.0f); c.puy.fill (0.0f); c.opx.fill (1.0f); c.opy.fill (0.0f); }
        inPos = 0; hopCount = 0; frozenNow = false; prevFlux = 0.0f; scatPhase = 0; lastBar = -1.0;
    }
public:
    void process (juce::dsp::AudioBlock<float> block, const ModulationContext& ctx) noexcept override
    {
        beginBlock (ctx);
        const int n = (int) block.getNumSamples(); auto* L = block.getChannelPointer (0); auto* R = block.getNumChannels() > 1 ? block.getChannelPointer (1) : L;
        hop = on (pHq) ? kN / 8 : kN / 4;
        olaGain = on (pHq) ? 1.0f / 4.0f : 1.0f / 2.0f;   // sum of sqrt-Hann^2 (= Hann) at 87.5 % / 75 % overlap is 4 / 2
        tempoPpq = ctx.tempo.ppqPosition; tempoSpb = ctx.tempo.samplesPerBeat();
        for (int i = 0; i < n; ++i)
        {
            ch[0].in[(size_t) inPos] = L[i]; ch[1].in[(size_t) inPos] = R[i];
            L[i] = ch[0].out[(size_t) inPos]; R[i] = ch[1].out[(size_t) inPos];
            ch[0].out[(size_t) inPos] = 0.0f; ch[1].out[(size_t) inPos] = 0.0f;
            if (++inPos == kN) inPos = 0;
            if (++hopCount >= hop) { hopCount = 0; frame (i, n); }
            for (int k : { pSmear, pScatter, pShift, pGhost, pMotion, pTilt, pThresh, pPitch, pPhase, pBinshift }) p (k).next();
        }
    }
private:
    struct Chan
    {
        std::array<float, kN> in {}, out {};
        std::array<float, kBins> prevMag {}, smMag {}, frzMag {}, frx {}, fry {}, pux {}, puy {}, opx {}, opy {};
    };
    // one analysis / synthesis frame, both channels (the same random choices for both, so a mono source stays mono)
    void frame (int sampleInBlock, int) noexcept
    {
        const float smear = p (pSmear).current, scatter = p (pScatter).current, ghost = p (pGhost).current, motion = p (pMotion).current;
        const float tilt0 = p (pTilt).current, threshDb = p (pThresh).current, phaseRnd = p (pPhase).current, binshift = p (pBinshift).current;
        const float ratio = std::pow (2.0f, (p (pShift).current + p (pPitch).current) / 12.0f);
        // motion: a host-synced sine on smear and tilt
        float mot = 0.0f;
        if (motion > 0.0f)
        {
            const auto& dv = divisions(); const double beats = dv[(size_t) juce::jlimit (0, (int) dv.size() - 1, choice (pMdiv))].beats;
            mot = motion * (float) std::sin (hostPhase (tempoPpq + sampleInBlock / tempoSpb, beats) * juce::MathConstants<double>::twoPi);
        }
        const float sm = juce::jlimit (0.0f, 0.97f, smear + 0.5f * mot), tilt = juce::jlimit (-1.0f, 1.0f, tilt0 + 0.5f * mot);
        // freeze (with FREEZE SYNC: re-captured at every division of the host clock)
        bool capture = false; const bool wantFreeze = on (pFreeze);
        if (wantFreeze && ! frozenNow) capture = true;
        if (wantFreeze && choice (pFsync) > 0)
        {
            static const double fb[] { 0.0, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0 };
            const double beats = fb[juce::jlimit (0, 6, choice (pFsync))], pos = std::floor ((tempoPpq + sampleInBlock / tempoSpb) / beats);
            if (pos != lastBar) { lastBar = pos; capture = true; }
        }
        frozenNow = wantFreeze;
        const bool identity = ! wantFreeze && sm <= 0.0f && scatter <= 0.0f && ratio == 1.0f && binshift <= 0.0f && phaseRnd <= 0.0f && tilt == 0.0f && threshDb <= -89.9f;
        // scatter: re-deal the bin pairs every 16 frames, crossfading between deals (Bin Scrambler)
        if (scatter > 0.0f && ++scatPhase >= 16)
        {
            scatPhase = 0; scatA = scatB; const int range = 1 + (int) (scatter * 48.0f);
            for (int k = 0; k < kBins; ++k) scatB[(size_t) k] = juce::jlimit (0, kBins - 1, k + std::uniform_int_distribution<int> (-range, range) (rng));
        }
        const float scatT = (float) scatPhase / 16.0f;
        // transient (spectral flux on the mid) for GHOST
        float flux = 0.0f;
        for (int c = 0; c < 2; ++c)
        {
            auto& C = ch[(size_t) c];
            for (int i = 0; i < kN; ++i) { const int k = (inPos + i) % kN; buf[(size_t) i] = C.in[(size_t) k] * win[(size_t) i]; buf[(size_t) (kN + i)] = 0.0f; }   // JUCE: real input in the first N
            fft.performRealOnlyForwardTransform (buf.data(), true);
            if (identity)
            {
                for (int k = 0; k < kBins; ++k) { const float re = buf[(size_t) (2 * k)], im = buf[(size_t) (2 * k + 1)], m = std::sqrt (re * re + im * im); C.prevMag[(size_t) k] = m; C.smMag[(size_t) k] = m; const float x = m > 1.0e-20f ? re / m : 1.0f, y = m > 1.0e-20f ? im / m : 0.0f; C.pux[(size_t) k] = C.opx[(size_t) k] = x; C.puy[(size_t) k] = C.opy[(size_t) k] = y; }
            }
            else
            {
                for (int k = 0; k < kBins; ++k)
                {
                    const float re = buf[(size_t) (2 * k)], im = buf[(size_t) (2 * k + 1)], m = std::sqrt (re * re + im * im);
                    mag[(size_t) k] = m; ux[(size_t) k] = m > 1.0e-20f ? re / m : 1.0f; uy[(size_t) k] = m > 1.0e-20f ? im / m : 0.0f;
                    if (c == 0) flux += juce::jmax (0.0f, m - C.prevMag[(size_t) k]);
                }
                if (c == 0) { const float f = flux; transient = f > 2.5f * prevFlux + 1.0e-3f; prevFlux = 0.7f * prevFlux + 0.3f * f; }
                processBins (C, capture, sm, scatT, ratio, binshift, phaseRnd, tilt, threshDb, ghost);
                // back to complex
                for (int k = 0; k < kBins; ++k) { buf[(size_t) (2 * k)] = omag[(size_t) k] * opx[(size_t) k]; buf[(size_t) (2 * k + 1)] = omag[(size_t) k] * opy[(size_t) k]; }
            }
            fft.performRealOnlyInverseTransform (buf.data());
            for (int i = 0; i < kN; ++i) { const int k = (inPos + i) % kN; C.out[(size_t) k] += buf[(size_t) i] * win[(size_t) i] * olaGain; }
        }
    }
    void processBins (Chan& C, bool capture, float sm, float scatT, float ratio, float binshift, float phaseRnd, float tilt, float threshDb, float ghost) noexcept
    {
        const float expAdv = 2.0f * juce::MathConstants<float>::pi * (float) hop / (float) kN;   // a bin's expected phase advance per hop
        const float thr = std::pow (10.0f, threshDb / 20.0f) * (float) kN * 0.32f;               // a full-scale sine's peak bin
        // each bin's rotation since the previous frame (a unit phasor: its own frequency), and this frame's phasor kept for the next
        for (int k = 0; k < kBins; ++k)
        {
            const float x = ux[(size_t) k], y = uy[(size_t) k], px = C.pux[(size_t) k], py = C.puy[(size_t) k];
            rx[(size_t) k] = x * px + y * py; ry[(size_t) k] = y * px - x * py;   // u * conj (prev)
            C.pux[(size_t) k] = x; C.puy[(size_t) k] = y; C.prevMag[(size_t) k] = mag[(size_t) k];
        }
        if (capture) for (int k = 0; k < kBins; ++k) { C.frzMag[(size_t) k] = mag[(size_t) k]; C.frx[(size_t) k] = rx[(size_t) k]; C.fry[(size_t) k] = ry[(size_t) k]; }
        const bool frozen = frozenNow;
        const float* srcMag = frozen ? C.frzMag.data() : mag.data(); const float* srx = frozen ? C.frx.data() : rx.data(); const float* sry = frozen ? C.fry.data() : ry.data();
        // gate, tilt
        for (int k = 0; k < kBins; ++k)
        {
            float m = srcMag[(size_t) k];
            if (threshDb > -89.9f && m < thr) m = 0.0f;
            if (tilt != 0.0f && k > 0) m *= std::pow ((float) k * 48000.0f / (float) kN / 1000.0f, tilt * 0.5f);
            work[(size_t) k] = m;
        }
        // smear: over time (one pole per bin) and across bins (a two-way blur whose width grows with SMEAR)
        if (sm > 0.0f)
        {
            for (int k = 0; k < kBins; ++k) { C.smMag[(size_t) k] = sm * C.smMag[(size_t) k] + (1.0f - sm) * work[(size_t) k]; work[(size_t) k] = C.smMag[(size_t) k]; }
            const float a = sm * 0.6f;
            for (int k = 1; k < kBins; ++k) work[(size_t) k] = work[(size_t) k] * (1.0f - a) + work[(size_t) (k - 1)] * a;
            for (int k = kBins - 2; k >= 0; --k) work[(size_t) k] = work[(size_t) k] * (1.0f - a) + work[(size_t) (k + 1)] * a;
        }
        else for (int k = 0; k < kBins; ++k) C.smMag[(size_t) k] = work[(size_t) k];
        // scatter
        const bool scattering = scatT > 0.0f || scatA[1] != 1 || scatB[1] != 1;
        if (scattering) for (int k = 0; k < kBins; ++k) tmp[(size_t) k] = work[(size_t) scatA[(size_t) k]] * (1.0f - scatT) + work[(size_t) scatB[(size_t) k]] * scatT;
        else tmp = work;
        const int lin = (int) std::lround (binshift * 2000.0f * (float) kN / 48000.0f);
        const bool moved = frozen || ratio != 1.0f || lin != 0 || sm > 0.0f || scattering;
        if (ratio == 1.0f && lin == 0)
        {
            omag = tmp;
            if (moved) for (int k = 0; k < kBins; ++k)
            {   // the output phasor turns by the bin's own rotation (phase vocoder), renormalised
                const float x = C.opx[(size_t) k] * srx[(size_t) k] - C.opy[(size_t) k] * sry[(size_t) k], y = C.opx[(size_t) k] * sry[(size_t) k] + C.opy[(size_t) k] * srx[(size_t) k];
                const float g = 1.0f / juce::jmax (1.0e-20f, std::sqrt (x * x + y * y)); C.opx[(size_t) k] = x * g; C.opy[(size_t) k] = y * g;
            }
            else for (int k = 0; k < kBins; ++k) { C.opx[(size_t) k] = ux[(size_t) k]; C.opy[(size_t) k] = uy[(size_t) k]; }
        }
        else
        {   // bins move (pitch, shift, bin shift): each target bin turns by its source's frequency, scaled
            std::fill (omag.begin(), omag.end(), 0.0f); std::fill (oadv.begin(), oadv.end(), 0.0f);
            for (int k = 0; k < kBins; ++k)
            {
                const int t = (int) std::lround (k * ratio) + lin;
                if (t < 0 || t >= kBins) continue;
                if (tmp[(size_t) k] >= omag[(size_t) t])
                {
                    float d = std::atan2 (sry[(size_t) k], srx[(size_t) k]) - expAdv * k; d -= 2.0f * juce::MathConstants<float>::pi * std::round (d / (2.0f * juce::MathConstants<float>::pi));
                    oadv[(size_t) t] = (expAdv * k + d) * ratio + (float) lin * expAdv;
                }
                omag[(size_t) t] += tmp[(size_t) k];
            }
            for (int k = 0; k < kBins; ++k)
            {
                const float c = std::cos (oadv[(size_t) k]), sn = std::sin (oadv[(size_t) k]);
                const float x = C.opx[(size_t) k] * c - C.opy[(size_t) k] * sn, y = C.opx[(size_t) k] * sn + C.opy[(size_t) k] * c;
                C.opx[(size_t) k] = x; C.opy[(size_t) k] = y;
            }
        }
        for (int k = 0; k < kBins; ++k) { opx[(size_t) k] = C.opx[(size_t) k]; opy[(size_t) k] = C.opy[(size_t) k]; }
        if (phaseRnd > 0.0f)
        {
            if (&C == &ch[0]) { std::uniform_real_distribution<float> u (-juce::MathConstants<float>::pi, juce::MathConstants<float>::pi); for (int k = 0; k < kBins; ++k) { const float a = phaseRnd * u (rng); rnx[(size_t) k] = std::cos (a); rny[(size_t) k] = std::sin (a); } }
            for (int k = 0; k < kBins; ++k) { const float x = opx[(size_t) k] * rnx[(size_t) k] - opy[(size_t) k] * rny[(size_t) k], y = opx[(size_t) k] * rny[(size_t) k] + opy[(size_t) k] * rnx[(size_t) k]; opx[(size_t) k] = x; opy[(size_t) k] = y; }
        }
        // GHOST keeps the hits: a transient frame passes through less processed (its own phases from half way)
        if (transient && ghost > 0.0f && ! frozen)
            for (int k = 0; k < kBins; ++k) { omag[(size_t) k] = omag[(size_t) k] * (1.0f - ghost) + mag[(size_t) k] * ghost; if (ghost >= 0.5f) { opx[(size_t) k] = ux[(size_t) k]; opy[(size_t) k] = uy[(size_t) k]; } }
    }

    juce::dsp::FFT fft { kOrder };
    std::array<float, kN> win {}; std::array<float, 2 * kN> buf {};
    std::array<float, kBins> mag {}, ux {}, uy {}, rx {}, ry {}, work {}, tmp {}, omag {}, oadv {}, opx {}, opy {}, rnx {}, rny {};
    std::array<int, kBins> scatA {}, scatB {};
    std::array<Chan, 2> ch; int inPos = 0, hopCount = 0, hop = kN / 4, scatPhase = 0; float olaGain = 0.5f, prevFlux = 0.0f;
    bool frozenNow = false, transient = false; double lastBar = -1.0, tempoPpq = 0.0, tempoSpb = 24000.0; std::mt19937 rng;
    int pFreeze = 0, pSmear = 0, pScatter = 0, pShift = 0, pGhost = 0, pMotion = 0, pMdiv = 0, pTilt = 0, pHq = 0, pFsync = 0, pThresh = 0, pPitch = 0, pPhase = 0, pBinshift = 0;
};
} // namespace phantom
