#pragma once
// Ghost Box 3 units, part 2 (GPFX_BOX builds only): GHOST SPECTRA (STFT freeze / smear / scatter / shift / ghost),
// BASS LAB (3-band bass tool with sub, growl, edge and TRANSLATE), DOORWAY (morphing-spaces reverb; internal key "portal"),
// GHOST FORMANT (talking vowel filter) and the GHOST GATE pump (rhythmic / drawn / sidechain ducking).
// Original designs. Audio-thread rule: no allocation, no locks inside process().
#include "FxCore.h"
#include "FxSpaceDsp.h"
#include "FxColour.h"
#include "FxRhythm.h"
#include "BendLane.h"
#include "SpectralCore.h"

#if GPFX_BOX
namespace gpfx {

inline const juce::StringArray& portalShapeNames() { static juce::StringArray s { "METALLIC", "ORGANIC", "HOLLOW", "DIFFUSE", "CRYSTALLINE" }; return s; }
inline const juce::StringArray& portalMoveNames() { static juce::StringArray s { "STATIC", "DRIFT", "ROTATE", "EXPAND" }; return s; }
inline const juce::StringArray& formantMoveNames() { static juce::StringArray s { "STILL", "LFO", "DRAWN" }; return s; }
inline const juce::StringArray& formantLfoNames() { static juce::StringArray s { "SINE", "TRIANGLE", "SAW", "SQUARE", "S&H", "ENV" }; return s; }
inline const juce::StringArray& pumpModeNames() { static juce::StringArray s { "OFF", "4 ON FLOOR", "HALF-TIME", "FAST PULSE", "DRAWN", "SIDECHAIN" }; return s; }
inline const juce::StringArray& pumpLenNames() { static juce::StringArray s { "1/2 BEAT", "1 BEAT", "2 BEATS", "1 BAR" }; return s; }
inline double pumpLenBeats (int i) { static const double b[] { 0.5, 1, 2, 4 }; return b[juce::jlimit (0, 3, i)]; }
enum PumpMode { pmOff, pmFloor, pmHalf, pmFast, pmDrawn, pmSidechain };

// latency switch that crossfades the dry delay (same idea as WARP): units that only add latency while they are on
struct LatentDry
{
    std::array<Ring, 2> ring; int cur = 0, from = 0, xf = 0, xfLen = 256;
    void clear() noexcept { for (auto& r : ring) r.clear(); from = cur; xf = 0; }   // empty the line (keeps the current tap, no crossfade)
    void prepare (int maxLat, double sr) { for (auto& r : ring) r.prepare (maxLat + kSub + 16); xfLen = std::max (32, (int) (sr * 0.005)); cur = from = xf = 0; }
    void process (float* L, float* R, int n, int d) noexcept
    {
        if (d != cur && xf == 0) { from = cur; cur = d; xf = xfLen; }   // a change during a crossfade waits for it to end (no jump)
        for (int i = 0; i < n; ++i)
        {
            ring[0].push (L[i]); ring[1].push (R[i]);
            float l = d > 0 ? ring[0].at (d) : L[i], r = d > 0 ? ring[1].at (d) : R[i];
            if (xf > 0)
            {
                const float w = 0.5f - 0.5f * std::cos (kPi * (float) xf / (float) xfLen);
                l += w * ((from > 0 ? ring[0].at (from) : L[i]) - l); r += w * ((from > 0 ? ring[1].at (from) : R[i]) - r); --xf;
            }
            L[i] = l; R[i] = r;
        }
    }
};

// ================================================================ GHOST SPECTRA
inline const juce::StringArray& spectraSyncNames() { static juce::StringArray s { "OFF", "1/16", "1/8", "1/4", "1/2", "1 BAR", "2 BARS" }; return s; }
inline double spectraSyncBeats (int i) noexcept { static const double b[] { 0, 0.25, 0.5, 1, 2, 4, 8 }; return b[juce::jlimit (0, 6, i)]; }
constexpr int kSpectraBands = 64;
// the band layout shared by the display, the painted mask and the audio: 64 log-spaced bands, 30 Hz .. 20 kHz
inline double spectraBandLo (int b) noexcept { return 30.0 * std::pow (666.0, b / 64.0); }

// Painted per-band freeze mask (message thread writes, audio thread reads; no locks).
struct SpectraMask
{
    std::array<std::atomic<float>, kSpectraBands> v {};
    std::atomic<uint32_t> ver { 0 };
    float get (int b) const noexcept { return v[(size_t) juce::jlimit (0, kSpectraBands - 1, b)].load (std::memory_order_relaxed); }
    void set (int b, float x) { v[(size_t) juce::jlimit (0, kSpectraBands - 1, b)].store (clampf (x, 0.0f, 1.0f)); ver.fetch_add (1); }
    void clear() { for (auto& a : v) a.store (0.0f); ver.fetch_add (1); }
    bool any() const { for (auto& a : v) if (a.load() > 0.001f) return true; return false; }
    juce::String toString() const { juce::StringArray s; for (auto& a : v) s.add (juce::String (a.load(), 3)); return s.joinIntoString (","); }
    void fromString (const juce::String& str)
    {
        auto t = juce::StringArray::fromTokens (str, ",", "");
        for (int b = 0; b < kSpectraBands; ++b) v[(size_t) b].store (b < t.size() ? clampf (t[b].getFloatValue(), 0.0f, 1.0f) : 0.0f);
        ver.fetch_add (1);
    }
};

class SpectraUnit : public UnitDsp
{
public:
    Telemetry* tm = nullptr;
    const std::atomic<bool>* offline = nullptr;   // the host is bouncing (non-realtime): HQ overlap if asked for
    const SpectraMask* mask = nullptr;            // painted per-band freeze
    void attach (juce::AudioProcessorValueTreeState& s) override
    {
        auto b = [&s] (P& p, const char* id, float d) { p.bind (s, pid (uSpectra, id), d); };
        b (freeze, "freeze", 0); b (smear, "smear", 0.3f); b (scatter, "scatter", 0); b (shift, "shift", 0); b (ghost, "ghost", 0.5f);
        b (motion, "motion", 0); b (mdiv, "mdiv", 9); b (tilt, "tilt", 0); b (hq, "hq", 0); b (fsync, "fsync", 0); b (thresh, "thresh", -90);
        hostOn = s.getRawParameterValue (pid (uSpectra, "on"));   // the host value itself: latency is reported the moment the switch flips
    }
    void prepare (double sr_, int) override
    {
        sr = sr_;
        order = sr >= 88200.0 ? 12 : 11;   // ~43 ms frames at every rate (same smear / freeze character at 44.1, 48, 96 kHz)
        N = 1 << order; bins = N / 2 + 1;
        stft.prepare (order, 8); dry.prepare (N, sr);
        for (auto* v : { &mag, &prevMag, &smearMag, &frozen, &lastPh[0], &lastPh[1], &sumPh[0], &sumPh[1], &newMag, &newFreq, &binAmt })
            v->assign ((size_t) bins, 0.0f);
        binBand.assign ((size_t) bins, 0);
        for (int k = 0; k < bins; ++k)
        {
            const double f = k * sr / N; int b = 0;
            while (b + 1 < kSpectraBands && spectraBandLo (b + 1) <= f) ++b;
            binBand[(size_t) k] = (uint8_t) b;
        }
        hist.assign ((size_t) N, 0.0f); histR.assign ((size_t) N, 0.0f);
        reset();
    }
    void reset() override
    {
        stft.reset(); primed = false; freezeAmt = 0; trans = 0; rng = 0x1234567u; lfoPh = 0; retrig = false; lastSyncCycle = -1; wasFrozen = false;
        for (auto* v : { &prevMag, &smearMag, &frozen, &lastPh[0], &lastPh[1], &sumPh[0], &sumPh[1] }) std::fill (v->begin(), v->end(), 0.0f);
    }
    int frameSize() const noexcept { return N; }
    // reported as soon as the unit is switched on (before its first process call), so the host's PDC moves with the switch
    int latency() const override { return latent.load (std::memory_order_relaxed) || (hostOn != nullptr && hostOn->load() > 0.5f) ? N : 0; }
    int maxLatency() const noexcept { return N; }
    void delayDry (float* L, float* R, int n) override { dry.process (L, R, n, latency()); }
    void clearLines() override { dry.clear(); }
    void idle (const float*, const float*, int) override { latent.store (false, std::memory_order_relaxed); primed = false; }
    void process (float* L, float* R, int n, const BlockCtx& c) override
    {
        latent.store (true, std::memory_order_relaxed);
        if (! primed)
        {   // just switched on: start from the audio the dry ring already has, so sound comes back after one hop
            const int have = std::min (N, dry.ring[0].size() - 8);
            std::fill (hist.begin(), hist.end(), 0.0f); std::fill (histR.begin(), histR.end(), 0.0f);
            for (int i = 0; i < have; ++i) { hist[(size_t) (N - have + i)] = dry.ring[0].at (have - 1 - i); histR[(size_t) (N - have + i)] = dry.ring[1].at (have - 1 - i); }
            stft.prime (hist.data(), histR.data(), N); primed = true;
        }
        stft.setOverlap (hq.b() && offline != nullptr && offline->load (std::memory_order_relaxed) ? 8 : 4);
        // tempo-synced motion (triangle) on SHIFT and SCATTER
        const auto& d = echoDivs(); const double beats = d[(size_t) juce::jlimit (0, (int) d.size() - 1, mdiv.i())].beats;
        lfoPh = positiveMod (c.ppq, beats) / beats;
        const float tri = (float) (lfoPh < 0.5 ? 4.0 * lfoPh - 1.0 : 3.0 - 4.0 * lfoPh);
        pShift = clampf (shift(), -24.0f, 24.0f) + clampf (motion(), 0.0f, 1.0f) * tri * 7.0f;
        pScatter = clampf (scatter() + clampf (motion(), 0.0f, 1.0f) * std::max (0.0f, tri) * 0.5f, 0.0f, 1.0f);
        pSmear = clampf (smear(), 0.0f, 1.0f); pGhost = clampf (ghost(), 0.0f, 1.0f); pFreeze = freeze.b(); pTilt = clampf (tilt(), -1.0f, 1.0f);
        pThr = clampf (thresh(), -90.0f, 0.0f);
        // FREEZE SYNC: while frozen, re-capture the spectrum at every division boundary
        const double syncBeats = spectraSyncBeats (fsync.i());
        if (pFreeze && syncBeats > 0)
        {   // the boundary is looked at one frame late, so the re-captured frame holds the audio AFTER the beat, not the 43 ms before it
            const double lag = (double) N / c.samplesPerBeat;
            const long cycEnd = (long) std::floor ((c.ppq - lag + (double) n / c.samplesPerBeat) / syncBeats);
            if (lastSyncCycle >= 0 && cycEnd != lastSyncCycle) retrig = true;
            lastSyncCycle = cycEnd;
        }
        else lastSyncCycle = -1;
        if (mask != nullptr) for (int b = 0; b < kSpectraBands; ++b) bandAmt[(size_t) b] = mask->get (b);
        stft.process (L, R, n, [this] (spectral::Cpx* a, spectral::Cpx* b2, int nb) { frame (a, b2, nb); });
        if (tm) { float pk = 0; for (int i = 0; i < n; i += 4) pk = std::max (pk, std::abs (L[i])); tm->spectraLevel.store (pk); tm->spectraTrans.store (trans); }
    }

private:
    void frame (spectral::Cpx* X0, spectral::Cpx* X1, int nb)
    {
        const float hop = (float) stft.hop(), twoPi = juce::MathConstants<float>::twoPi;
        // every per-frame recursion is normalised to the hop, so an HQ bounce (overlap 8) has the same time constants as playback (overlap 4)
        const float hs = 4.0f / (float) stft.overlap();
        // transient detector on the mid magnitude (GHOST keeps transient frames dry-ish)
        float frameMax = 0;
        for (int k = 0; k < nb; ++k) { mag[(size_t) k] = 0.5f * (std::abs (X0[k]) + std::abs (X1[k])); frameMax = std::max (frameMax, mag[(size_t) k]); }
        const float fl = spectral::flux (mag.data(), prevMag.data(), nb) / hs;
        if (tm && ++telCount % 2 == 0)
        {
            std::array<float, kSpectraBands> bm {};
            for (int k = 1; k < nb; ++k) bm[binBand[(size_t) k]] = std::max (bm[binBand[(size_t) k]], mag[(size_t) k]);
            for (int b = 0; b < kSpectraBands; ++b) tm->spectrum[(size_t) b].store (bm[(size_t) b] * 4.0f / (float) N, std::memory_order_relaxed);
        }
        const float tTarget = clampf ((fl - 0.12f) * 4.0f, 0.0f, 1.0f);
        trans = tTarget > trans ? tTarget : trans * std::pow (0.55f, hs);
        const float hold = pGhost * trans;   // 1 = leave this frame as it was
        const float fzCoef = 1.0f - std::pow (0.7f, hs);
        freezeAmt += ((pFreeze ? 1.0f : 0.0f) - freezeAmt) * fzCoef;
        if (! pFreeze && freezeAmt < 1.0e-3f) freezeAmt = 0.0f;
        // per-bin freeze amount: the global FREEZE or the painted band mask; bins that are not held keep capturing, so a band
        // (or the whole spectrum) freezes on the moment it is switched on. FREEZE SYNC re-captures everything on the beat.
        bool anyFrozen = freezeAmt > 0.0f;
        for (int k = 0; k < nb; ++k)
        {
            const float a = std::max (freezeAmt, bandAmt[binBand[(size_t) k]]);
            binAmt[(size_t) k] = a;
            if (a <= 0.0f || retrig) frozen[(size_t) k] = mag[(size_t) k];
            anyFrozen = anyFrozen || a > 0.0f;
        }
        retrig = false;
        const float ratio = std::exp2 (pShift / 12.0f);
        const bool shifting = std::abs (pShift) > 0.01f;
        const float sa = 1.0f - std::pow (0.97f * pSmear, hs);
        const bool gating = pThr > -89.0f;
        const float thrLin = frameMax * dbToGain (pThr);
        for (int c = 0; c < 2; ++c)
        {
            spectral::Cpx* X = c == 0 ? X0 : X1;
            auto& lp = lastPh[c]; auto& sp = sumPh[c];
            std::fill (newMag.begin(), newMag.begin() + nb, 0.0f); std::fill (newFreq.begin(), newFreq.begin() + nb, 0.0f);
            for (int k = 0; k < nb; ++k)
            {
                const float m = std::abs (X[k]), ph = std::arg (X[k]);
                // true frequency of the bin (in bins), from the phase advance since the last frame
                const float expected = twoPi * (float) k * hop / (float) N;
                const float dev = spectral::wrapPi (ph - lp[(size_t) k] - expected);
                lp[(size_t) k] = ph;
                const float fBins = (float) k + dev * (float) N / (twoPi * hop);
                float mm = m;
                if (c == 0) smearMag[(size_t) k] += sa * (m - smearMag[(size_t) k]);
                if (pSmear > 0.001f) mm = m + pSmear * ((c == 0 ? smearMag[(size_t) k] : smearMag[(size_t) k] * (m + 1e-9f) / (mag[(size_t) k] + 1e-9f)) - m);
                if (gating) mm *= clampf ((mag[(size_t) k] / (thrLin + 1.0e-12f) - 0.5f) * 2.0f, 0.0f, 1.0f);   // spectral gate: quiet bins drop out
                const float fa = binAmt[(size_t) k];
                if (fa > 0.0f) mm += fa * (frozen[(size_t) k] - mm);
                const int j = shifting ? (int) std::lround ((float) k * ratio) : k;
                if (j >= 0 && j < nb) { newMag[(size_t) j] += mm; newFreq[(size_t) j] = fBins * (shifting ? ratio : 1.0f); }
            }
            // spectral tilt (brighter / darker) and scatter (bin jitter + phase noise)
            const bool resynth = shifting || anyFrozen || pScatter > 0.001f || pSmear > 0.05f;
            for (int k = 0; k < nb; ++k)
            {
                float m = newMag[(size_t) k];
                if (std::abs (pTilt) > 0.001f) m *= std::exp2 (pTilt * (std::log2 ((float) (k + 1)) - 6.0f) * 0.25f);   // +-1.5 dB per octave around ~1 kHz
                float phase;
                if (resynth)
                {
                    float f = newFreq[(size_t) k];
                    const float fa = binAmt[(size_t) k];
                    if (fa > 0.0f) f += fa * ((float) k - f);   // frozen: every bin rings at its own centre
                    sp[(size_t) k] = spectral::wrapPi (sp[(size_t) k] + twoPi * f * hop / (float) N);
                    phase = sp[(size_t) k];
                    if (pScatter > 0.001f) { rng = rng * 1664525u + 1013904223u; phase += pScatter * hs * juce::MathConstants<float>::pi * ((float) (rng >> 9) / 4194304.0f - 1.0f); }
                }
                else { phase = std::arg (X[k]); sp[(size_t) k] = phase; }
                if (pScatter > 0.001f && k > 2 && k + 2 < nb) { rng = rng * 1664525u + 1013904223u; if ((float) (rng >> 28) < pScatter * 8.0f * hs) m = newMag[(size_t) (k + (int) ((rng >> 16) % 5) - 2)]; }
                const spectral::Cpx y = std::polar (m, phase);
                X[k] = hold > 0.0f ? hold * X[k] + (1.0f - hold) * y : y;
            }
        }
    }
    P freeze, smear, scatter, shift, ghost, motion, mdiv, tilt, hq, fsync, thresh; std::atomic<float>* hostOn = nullptr;
    spectral::Stft stft; LatentDry dry; std::atomic<bool> latent { false }; bool primed = false, retrig = false, wasFrozen = false;
    std::vector<float> mag, prevMag, smearMag, frozen, lastPh[2], sumPh[2], newMag, newFreq, binAmt, hist, histR;
    std::vector<uint8_t> binBand; std::array<float, kSpectraBands> bandAmt {};
    int order = 11, N = 2048, bins = 1025; long lastSyncCycle = -1;
    double sr = 48000, lfoPh = 0; float freezeAmt = 0, trans = 0, pShift = 0, pScatter = 0, pSmear = 0, pGhost = 0, pTilt = 0, pThr = -90; bool pFreeze = false; uint32_t rng = 1; int telCount = 0;
};

// ================================================================ BASS LAB
class BassLabUnit : public UnitDsp
{
public:
    Telemetry* tm = nullptr;
    void attach (juce::AudioProcessorValueTreeState& s) override
    {
        auto b = [&s] (P& p, const char* id, float d) { p.bind (s, pid (uBassLab, id), d); };
        b (xlo, "xlo", 120); b (xhi, "xhi", 2200); b (lowWidth, "lowwidth", 1); b (sub, "sub", 0); b (growl, "growl", 0); b (edge, "edge", 0);
        b (translate, "translate", 0); b (lowG, "low", 0); b (midG, "mid", 0); b (highG, "high", 0); b (autoG, "auto", 1); b (out, "out", 0);
        b (gfreq, "gfreq", 750); b (gmove, "gmove", 0); b (gdiv, "gdiv", 6);
    }
    void prepare (double sr_, int) override
    {
        sr = sr_; ag.prepare (sr, 400.0f); lastLo = lastHi = -1;
        for (auto& s : { &gSm, &oSm, &lgSm, &mgSm, &hgSm, &subSm, &trSm, &grSm, &edSm, &wSm }) s->setTime (sr, 25.0f);
        subLp.setLP (sr, 90.0); subLp2.setLP (sr, 90.0); trHp.setLP (sr, 180.0); trLp.setLP (sr, 2600.0); env.set (sr, 2.0f, 80.0f); subEnv.set (sr, 5.0f, 120.0f);
        growlEnv.set (sr, 8.0f, 150.0f); lastBell = -1; f0Sm = 0;
        reset();
    }
    void reset() override
    {
        for (auto& b : bq) b.reset();
        ag.reset(); subLp.reset(); subLp2.reset(); trHp.reset(); trLp.reset(); env.reset(); subEnv.reset(); growlEnv.reset(); flip = 1.0f; armed = true; sTrack.reset(); sinceFlip = 0; f0Inst = 0; f0Sm = 0;
        gSm.snap (1); oSm.snap (dbToGain (out())); lgSm.snap (dbToGain (lowG())); mgSm.snap (dbToGain (midG())); hgSm.snap (dbToGain (highG()));
        subSm.snap (sub()); trSm.snap (translate()); grSm.snap (growl()); edSm.snap (edge()); wSm.snap (lowWidth());
        midBell.reset(); midBell2.reset();
    }
    void process (float* L, float* R, int n, const BlockCtx& c) override
    {
        const float flo = clampf (xlo(), 30.0f, 500.0f), fhi = clampf (xhi(), 600.0f, 10000.0f);
        if (changed (flo, lastLo) || changed (fhi, lastHi))
        {
            lastLo = flo; lastHi = fhi;
            lp1 = rbj (rbjLP, sr, flo, 0.70710678); hp1 = rbj (rbjHP, sr, flo, 0.70710678);
            lp2 = rbj (rbjLP, sr, fhi, 0.70710678); hp2 = rbj (rbjHP, sr, fhi, 0.70710678);
            sTrack.setLP (sr, flo * 0.8);
        }
        // GROWL centre: GROWL FREQ moved by a tempo-synced sine and by the mid band's own envelope (GROWL MOVE = depth, up to +1.5 octaves)
        {
            const float mv = clampf (gmove(), 0.0f, 1.0f);
            const auto& dv = echoDivs(); const double beats = dv[(size_t) juce::jlimit (0, (int) dv.size() - 1, gdiv.i())].beats;
            const float lfo = 0.5f + 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * positiveMod (c.ppq, beats) / beats);
            const float e01 = clampf (growlEnv.env * 6.0f, 0.0f, 1.0f);
            const float hz = juce::jlimit (200.0f, 4000.0f, clampf (gfreq(), 300.0f, 1500.0f) * std::exp2 (mv * 1.5f * (0.6f * lfo + 0.4f * e01)));
            if (std::abs (hz / (lastBell > 0 ? lastBell : 1.0f) - 1.0f) > 0.01f) { lastBell = hz; bell = rbj (rbjBell, sr, hz, 0.9, 6.0); if (tm) tm->bassGrowlHz.store (hz); }
        }
        oSm.target = dbToGain (out()); lgSm.target = dbToGain (lowG()); mgSm.target = dbToGain (midG()); hgSm.target = dbToGain (highG());
        subSm.target = clampf (sub(), 0, 1); trSm.target = clampf (translate(), 0, 1); grSm.target = clampf (growl(), 0, 1); edSm.target = clampf (edge(), 0, 1); wSm.target = clampf (lowWidth(), 0, 1);
        const bool comp = autoG.b();
        float pk[3] {};
        for (int i = 0; i < n; ++i)
        {
            const float xl = L[i], xr = R[i];
            if (comp) ag.feedIn (xl, xr);
            float lo[2], mi[2], hi[2];
            for (int ch = 0; ch < 2; ++ch)
            {
                const float x = ch == 0 ? xl : xr; Biquad* b = &bq[(size_t) ch * 12];
                // Linkwitz-Riley 4th-order splits (two cascaded Butterworth stages each); the low band also passes the
                // upper split's allpass so the three bands sum back flat
                float l = b[1].process (b[0].process (x, lp1), lp1);
                const float h = b[3].process (b[2].process (x, hp1), hp1);
                const float m = b[5].process (b[4].process (h, lp2), lp2);
                const float hh = b[7].process (b[6].process (h, hp2), hp2);
                l = b[9].process (b[8].process (l, lp2), lp2) + b[11].process (b[10].process (l, hp2), hp2);
                lo[ch] = l; mi[ch] = m; hi[ch] = hh;
            }
            // LOW: width (0 = mono), sub octave, TRANSLATE harmonics
            const float w = wSm.next(), mono = 0.5f * (lo[0] + lo[1]);
            lo[0] = mono + w * (lo[0] - mono); lo[1] = mono + w * (lo[1] - mono);
            const float sb = subSm.next(), tr = trSm.next();
            float subS = 0, trS = 0;
            {   // the fundamental tracker always runs (it feeds the SUB flip-flop and the measured-f0 display)
                const float e = env.process (mono) + 1.0e-6f;
                const float tr0 = sTrack.lp (mono);
                if (armed && tr0 > 0.02f * e) { flip = -flip; armed = false; if (sinceFlip > 0 && sinceFlip < (int) sr) { const float f = (float) sr / (float) sinceFlip; if (f > 15.0f && f < 400.0f) f0Inst = f; } sinceFlip = 0; }
                else if (tr0 < -0.02f * e) armed = true;
                ++sinceFlip;
                if (sb > 1.0e-4f)   // octave-down: a flip-flop on the low band's upward zero crossings, shaped by its envelope, smoothed to a sine-ish sub
                    subS = subLp2.lp (subLp.lp (flip * subEnv.process (mono))) * 2.2f * sb;
                else subEnv.process (mono);
                if (tr > 1.0e-4f)
                {   // harmonics 2-4 of the low band (level independent: the shape works on the normalised waveform), band-passed to the
                    // range small speakers can play; the fundamental itself is untouched
                    const float u = clampf (mono / (e * 1.15f), -1.0f, 1.0f), u2 = u * u;
                    const float h2 = 2 * u2 - 1, h3 = 4 * u2 * u - 3 * u, h4 = 8 * u2 * u2 - 8 * u2 + 1;
                    trS = trLp.lp (trHp.hp ((0.6f * h2 + 0.45f * h3 + 0.25f * h4) * (e - 1.0e-6f))) * 1.6f * tr;   // (the guard epsilon is not signal: silence in = silence out)
                }
            }
            // MID: growl (bell push + saturation), HIGH: edge (exciter)
            const float gr = grSm.next(), ed = edSm.next();
            growlEnv.process (0.5f * (mi[0] + mi[1]));
            for (int ch = 0; ch < 2; ++ch)
            {
                float m = mi[ch];
                if (gr > 1.0e-4f)
                {
                    const float pushed = (ch == 0 ? midBell : midBell2).process (m, bell);
                    const float d = 1.0f + 18.0f * gr;
                    m = m + gr * (fastTanh ((m + gr * (pushed - m)) * d) / std::sqrt (d) * 1.4f - m);
                }
                float h = hi[ch];
                if (ed > 1.0e-4f) { const float d = 1.0f + 10.0f * ed; h = h + ed * (fastTanh (h * d) / std::sqrt (d) * 1.6f - h); }
                mi[ch] = m; hi[ch] = h;
            }
            const float lg = lgSm.next(), mg = mgSm.next(), hg = hgSm.next(), og = oSm.next();
            float yl = (lo[0] + subS) * lg + (mi[0] + trS) * mg + hi[0] * hg;
            float yr = (lo[1] + subS) * lg + (mi[1] + trS) * mg + hi[1] * hg;
            if (comp) { const float g = ag.next (yl, yr); yl *= g; yr *= g; }
            L[i] = yl * og; R[i] = yr * og;
            pk[0] = std::max (pk[0], std::abs (lo[0] + subS)); pk[1] = std::max (pk[1], std::abs (mi[0] + trS)); pk[2] = std::max (pk[2], std::abs (hi[0]));
        }
        // measured fundamental: the period between the tracked low band's upward zero crossings
        if (env.env > 1.0e-3f && f0Inst > 0) f0Sm += 0.2f * (f0Inst - f0Sm); else if (env.env <= 1.0e-3f) f0Sm *= 0.9f;
        if (tm) { for (int k = 0; k < 3; ++k) tm->bassBand[(size_t) k].store (pk[k]); tm->bassF0.store (f0Sm); }
    }
private:
    P xlo, xhi, lowWidth, sub, growl, edge, translate, lowG, midG, highG, autoG, out, gfreq, gmove, gdiv;
    std::array<Biquad, 24> bq; BiquadCoefs lp1, hp1, lp2, hp2, bell; Biquad midBell, midBell2;
    OnePole subLp, subLp2, trHp, trLp, sTrack; EnvFollow env, subEnv, growlEnv; AutoGain ag;
    ExpSmooth gSm, oSm, lgSm, mgSm, hgSm, subSm, trSm, grSm, edSm, wSm;
    double sr = 48000; float lastLo = -1, lastHi = -1, flip = 1.0f, lastBell = -1, f0Sm = 0, f0Inst = 0; bool armed = true; int sinceFlip = 0;
};

// ================================================================ DOORWAY (internal key "portal")
class PortalUnit : public UnitDsp
{
public:
    Telemetry* tm = nullptr;
    void attach (juce::AudioProcessorValueTreeState& s) override
    {
        auto b = [&s] (P& p, const char* id, float d) { p.bind (s, pid (uPortal, id), d); };
        b (size, "size", 0.6f); b (shape, "shape", 1); b (move, "move", 1); b (density, "density", 0.7f); b (decay, "decay", 4.0f);
        b (transform, "transform", 0); b (freeze, "freeze", 0); b (predelay, "predelay", 20); b (tone, "tone", 0); b (early, "early", 0.5f);
    }
    void prepare (double sr_, int) override
    {
        sr = sr_; fdn.prepare (sr, 2.6f);
        const float apMs[6] { 5.3f, 7.9f, 11.7f, 3.1f, 13.9f, 9.7f };
        for (int k = 0; k < 6; ++k) { apL[(size_t) k].prepare ((int) (apMs[k] * 0.001 * sr), 0.6f); apR[(size_t) k].prepare ((int) (apMs[k] * 1.09f * 0.001 * sr), 0.6f); }
        for (auto& r : pre) r.prepare ((int) (sr * 0.75) + 16);   // pre-delay (250 ms) + the longest early reflection at full size
        for (auto& p : shim) p.prepare (sr, 70.0f);
        for (auto& p : shim) p.setSemis (12.0f);
        preSm.setTime (sr, 80.0f); shapeX.setTime (sr, 150.0f); erSm.setTime (sr, 40.0f); erScaleSm.setTime (sr, 30.0f);
        for (auto& s : hollow) s.set (sr, 650.0, 2.2);
        reset();
    }
    void reset() override
    {
        fdn.clear(); for (auto& a : apL) a.clear(); for (auto& a : apR) a.clear(); for (auto& r : pre) r.clear(); for (auto& p : shim) p.clear();
        for (auto& s : hollow) s.reset();
        preSm.snap (predelay() * 0.001f * (float) sr); erSm.snap (clampf (early(), 0, 1)); fbL = fbR = 0; rot = 0; wander = 0; wanderT = 0; noise = 0x51ed27u; ramp.start ((int) (sr * 0.01)); erScaleInit = false;
    }
    void jumpSmoothing() override { snapPre = true; }
    // early-reflection pattern per shape: 8 taps (ms at size 0.5, gain). Original tables: METALLIC = tight flutter, ORGANIC = irregular
    // room, HOLLOW = sparse long tube slaps, DIFFUSE = dense cluster, CRYSTALLINE = sparse bright glints.
    struct ErTap { float ms, g; };
    static const std::array<ErTap, 8>& erPattern (int shape)
    {
        static const std::array<std::array<ErTap, 8>, 5> t {{
            {{ { 1.1f, 0.90f }, { 2.3f, 0.80f }, { 3.1f, 0.72f }, { 4.7f, 0.66f }, { 5.9f, 0.60f }, { 7.3f, 0.54f }, { 8.9f, 0.48f }, { 11.0f, 0.42f } }},
            {{ { 7.0f, 0.85f }, { 13.0f, 0.70f }, { 19.0f, 0.62f }, { 27.0f, 0.55f }, { 34.0f, 0.46f }, { 43.0f, 0.40f }, { 55.0f, 0.33f }, { 68.0f, 0.27f } }},
            {{ { 15.0f, 0.80f }, { 33.0f, 0.72f }, { 61.0f, 0.60f }, { 95.0f, 0.50f }, { 130.0f, 0.42f }, { 170.0f, 0.34f }, { 215.0f, 0.27f }, { 260.0f, 0.20f } }},
            {{ { 5.0f, 0.70f }, { 9.0f, 0.68f }, { 14.0f, 0.64f }, { 20.0f, 0.60f }, { 27.0f, 0.55f }, { 35.0f, 0.50f }, { 44.0f, 0.45f }, { 54.0f, 0.40f } }},
            {{ { 3.0f, 0.75f }, { 8.0f, 0.55f }, { 17.0f, 0.65f }, { 29.0f, 0.40f }, { 47.0f, 0.50f }, { 71.0f, 0.30f }, { 101.0f, 0.36f }, { 139.0f, 0.22f } }} }};
        return t[(size_t) juce::jlimit (0, 4, shape)];
    }
    void process (float* L, float* R, int n, const BlockCtx& c) override
    {
        const int sh = juce::jlimit (0, 4, shape.i()), mv = juce::jlimit (0, 3, move.i());
        const float dens = clampf (density(), 0, 1), tf = clampf (transform(), 0, 1);
        // TRANSFORM: over an 8-bar cycle the space breathes (size, decay, damping) so the room keeps changing shape
        const double cyc = positiveMod (c.ppq, 32.0) / 32.0;
        const float tSin = (float) std::sin (2.0 * juce::MathConstants<double>::pi * cyc);
        float sz = clampf (size(), 0, 1) * (1.0f + 0.35f * tf * tSin);
        float dec = clampf (decay(), 0.2f, 60.0f) * (1.0f + 0.5f * tf * (float) std::sin (2.0 * juce::MathConstants<double>::pi * cyc + 1.7));
        // MOVEMENT
        float mod = 0.06f;
        if (mv == 1) { mod = 0.65f; if ((wanderT -= n) <= 0) { wanderT = (int) (sr * 0.7); noise = hash32 (noise + 1); wanderTgt = ((float) (noise & 0xffff) / 65535.0f - 0.5f) * 0.12f; } wander += 0.02f * (wanderTgt - wander); sz *= 1.0f + wander; }
        if (mv == 3) sz *= 0.6f + 0.6f * (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * positiveMod (c.ppq, 16.0) / 16.0));
        // SHAPE
        float damp = 7000.0f * std::exp2 (clampf (tone(), -1, 1) * 1.5f);
        float scale = sz; bool diffuse = true; float shimAmt = 0;
        switch (sh)
        {
            case 0: scale = 0.05f + 0.25f * sz; diffuse = false; damp *= 2.2f; break;     // METALLIC: short tuned lines ring like metal
            case 1: damp *= 0.6f; mod = std::max (mod, 0.45f); break;                       // ORGANIC
            case 2: damp *= 0.8f; break;                                                     // HOLLOW (band-passed input)
            case 3: break;                                                                   // DIFFUSE
            default: damp *= 1.6f; shimAmt = 0.18f + 0.25f * dens; break;                    // CRYSTALLINE: octave shimmer in the loop
        }
        damp *= 1.0f + 0.6f * tf * tSin;
        fdn.diffuse = diffuse;
        fdn.set (clampf (scale, 0.0f, 1.0f), dec, clampf (damp, 800.0f, 20000.0f), mod, freeze.b());
        preSm.target = clampf (predelay(), 0.0f, 250.0f) * 0.001f * (float) sr;
        if (snapPre) { preSm.snap (preSm.target); snapPre = false; erScaleInit = false; }
        erSm.target = clampf (early(), 0.0f, 1.0f) * (1.0f - (freeze.b() ? 1.0f : 0.0f));
        const auto& erp = erPattern (sh);
        erScaleSm.target = (0.4f + 1.2f * clampf (sz, 0.0f, 1.0f)) * 0.001f * (float) sr;   // the pattern stretches with SIZE (glided: no tap jumps)
        if (! erScaleInit) { erScaleSm.snap (erScaleSm.target); erScaleInit = true; }
        const int stages = 2 + (int) std::lround (dens * 4.0f);
        const float apg = 0.45f + 0.25f * dens;
        for (auto& a : apL) a.g = apg;
        for (auto& a : apR) a.g = apg;
        const double rotRate = c.samplesPerBeat > 0 ? 1.0 / (8.0 * c.samplesPerBeat) : 0.0;   // ROTATE: one turn every 2 bars
        float pk = 0;
        for (int i = 0; i < n; ++i)
        {
            const float rin = ramp.next();
            pre[0].push (L[i] * rin); pre[1].push (R[i] * rin);
            const double d = preSm.next();
            float il = d < 1.0 ? pre[0].at (0) : pre[0].read (d), ir = d < 1.0 ? pre[1].at (0) : pre[1].read (d);
            // early reflections from the pre-delayed signal: the front wall / first slaps before the tail (mirrored taps left / right)
            float eL = 0, eR = 0; const float er = erSm.next(); const float erScale = erScaleSm.next();
            if (er > 0.001f)
                for (int k = 0; k < 8; ++k)
                {
                    const double t = d + erp[(size_t) k].ms * erScale;
                    eL += pre[0].readLinear (t) * erp[(size_t) k].g; eR += pre[1].readLinear (t * 1.06 + 11.0) * erp[(size_t) (7 - k)].g;
                }
            eL *= er * 0.5f; eR *= er * 0.5f;
            il += 0.35f * eL; ir += 0.35f * eR;   // the reflections also excite the tail, so tail and room belong together
            if (sh == 2) { float lp, bp, hp; hollow[0].tick (il, lp, bp, hp); il = bp * 1.6f; hollow[1].tick (ir, lp, bp, hp); ir = bp * 1.6f; }
            for (int k = 0; k < stages; ++k) { il = apL[(size_t) k].process (il); ir = apR[(size_t) k].process (ir); }
            float inject = 0;
            if (shimAmt > 0) inject = shimAmt * 0.5f * (shim[0].process (fbL) + shim[1].process (fbR));
            float wl, wr; fdn.process (il, ir, wl, wr, inject);
            fbL = wl; fbR = wr;
            if (mv == 2)
            {
                rot += rotRate; if (rot >= 1.0) rot -= 1.0;
                const float a = (float) (2.0 * juce::MathConstants<double>::pi * rot), cs = std::cos (a), sn = std::sin (a);
                const float m = 0.5f * (wl + wr), s = 0.5f * (wl - wr);
                const float s2 = s * cs + m * sn * 0.35f; wl = m + s2; wr = m - s2;
            }
            L[i] = wl + eL; R[i] = wr + eR; pk = std::max (pk, std::abs (L[i]));
        }
        if (tm) { tm->portalLevel.store (pk); tm->portalShape.store ((float) sh); }
    }
private:
    P size, shape, move, density, decay, transform, freeze, predelay, tone, early;
    Fdn8 fdn; std::array<Allpass, 6> apL, apR; std::array<Ring, 2> pre; std::array<PitchShifter, 2> shim; std::array<Svf, 2> hollow;
    ExpSmooth preSm, shapeX, erSm, erScaleSm; InRamp ramp; bool snapPre = false, erScaleInit = false;
    double sr = 48000, rot = 0; float fbL = 0, fbR = 0, wander = 0, wanderTgt = 0; int wanderT = 0; uint32_t noise = 1;
};

// ================================================================ GHOST FORMANT
struct Vowel { float f[3]; float g[3]; };
inline const Vowel& vowelAt (int i)
{
    static const Vowel v[5] { { { 730, 1090, 2440 }, { 1.0f, 0.55f, 0.30f } },    // A
                              { { 530, 1840, 2480 }, { 1.0f, 0.60f, 0.35f } },    // E
                              { { 290, 2290, 3010 }, { 1.0f, 0.45f, 0.35f } },    // I
                              { { 570,  840, 2410 }, { 1.0f, 0.65f, 0.25f } },    // O
                              { { 320,  870, 2240 }, { 1.0f, 0.50f, 0.20f } } };  // U
    return v[juce::jlimit (0, 4, i)];
}
class FormantUnit : public UnitDsp
{
public:
    Telemetry* tm = nullptr;
    const bendlane::SharedShape* shape = nullptr;   // the drawn vowel path (MOVEMENT = DRAWN)
    void attach (juce::AudioProcessorValueTreeState& s) override
    {
        auto b = [&s] (P& p, const char* id, float d) { p.bind (s, pid (uFormant, id), d); };
        b (vowel, "vowel", 0); b (mouth, "mouth", 0.5f); b (reso, "reso", 0.5f); b (shift, "shift", 0); b (drive, "drive", 0.2f);
        b (move, "move", 0); b (mdiv, "mdiv", 9); b (mdepth, "mdepth", 0.5f); b (mlen, "mlen", 1); b (body, "body", 0.3f); b (mshape, "mshape", 0);
    }
    void prepare (double sr_, int) override { sr = sr_; vSm.setTime (sr, 18.0f); dSm.setTime (sr, 40.0f); bSm.setTime (sr, 40.0f); for (auto& b : bodyLp) b.setLP (sr, 150.0); inEnv.set (sr, 5.0f, 120.0f); reset(); }
    void reset() override { for (auto& f : flt) f.reset(); for (auto& b : bodyLp) b.reset(); vSm.snap (vowel()); dSm.snap (drive()); bSm.snap (body()); cnt = 0; dc[0].reset(); dc[1].reset(); dc[0].prepare (sr); dc[1].prepare (sr); }
    void process (float* L, float* R, int n, const BlockCtx& c) override
    {
        if (shape != nullptr) shape->fetch (cur, seen);
        if (cur.n == 0) cur.add (0, 0);
        const int mv = juce::jlimit (0, 2, move.i());
        float pos = clampf (vowel(), 0.0f, 4.0f);
        const float dp = clampf (mdepth(), 0.0f, 1.0f) * 2.0f;
        const float mScale = std::exp2 ((clampf (mouth(), 0, 1) - 0.5f) * 0.9f) * std::exp2 (clampf (shift(), -12.0f, 12.0f) / 12.0f);
        const float q = 2.0f + 14.0f * clampf (reso(), 0, 1);
        dSm.target = clampf (drive(), 0, 1); bSm.target = clampf (body(), 0, 1);
        float pk = 0;
        for (int i = 0; i < n; ++i)
        {
            if ((cnt++ & 15) == 0)
            {
                float v = pos;
                if (mv == 1)
                {   // LFO shapes: sine / triangle / saw / square / sample-and-hold (new value every division) / envelope follower
                    const auto& d = echoDivs(); const double beats = d[(size_t) juce::jlimit (0, (int) d.size() - 1, mdiv.i())].beats;
                    const double ph = positiveMod (c.ppqAt (i), beats) / beats; const int shp = juce::jlimit (0, 5, mshape.i());
                    float w;
                    switch (shp)
                    {
                        case 1: w = (float) (ph < 0.5 ? 4.0 * ph - 1.0 : 3.0 - 4.0 * ph); break;
                        case 2: w = (float) (1.0 - 2.0 * ph); break;
                        case 3: w = ph < 0.5 ? 1.0f : -1.0f; break;
                        case 4: { const long cyc = (long) std::floor (c.ppqAt (i) / beats); if (cyc != shCycle) { shCycle = cyc; shRng = hash32 (shRng + 0x9e37u); shVal = hashFloat (shRng) * 2.0f - 1.0f; } w = shVal; break; }
                        case 5: w = clampf ((gainToDb (inEnv.env) + 40.0f) / 30.0f, 0.0f, 1.0f) * 2.0f - 1.0f; break;   // -40 dB closed .. -10 dB open
                        default: w = (float) std::sin (2.0 * juce::MathConstants<double>::pi * ph); break;
                    }
                    v += dp * w;
                }
                else if (mv == 2)
                {
                    const double beats = bendlane::barsOf (mlen.i()) * 4.0;
                    v += dp * cur.valueAt (positiveMod (c.ppqAt (i), beats) / beats);
                }
                vSm.target = clampf (v, 0.0f, 4.0f);
                const float vv = vSm.cur; const int a = juce::jlimit (0, 3, (int) vv); const float f = vv - (float) a;
                const auto& A = vowelAt (a); const auto& B = vowelAt (a + 1);
                for (int k = 0; k < 3; ++k)
                {
                    const float hz = std::exp (std::log (A.f[k]) + f * (std::log (B.f[k]) - std::log (A.f[k]))) * mScale;
                    gains[(size_t) k] = A.g[k] + f * (B.g[k] - A.g[k]);
                    for (int ch = 0; ch < 2; ++ch) flt[(size_t) (ch * 3 + k)].set (sr, juce::jlimit (60.0, sr * 0.45, (double) hz), q * (k == 0 ? 1.0f : 1.3f));
                }
                curVowel = vv;
            }
            vSm.next();
            inEnv.process (0.5f * (L[i] + R[i]));
            const float d = dSm.next(), bd = bSm.next(), dg = 1.0f + 14.0f * d;
            for (int ch = 0; ch < 2; ++ch)
            {
                float x = ch == 0 ? L[i] : R[i];
                const float dry = x;
                if (d > 1.0e-4f) x = fastTanh (x * dg) / std::sqrt (dg) * (1.0f + d);
                float y = 0;
                for (int k = 0; k < 3; ++k)
                {
                    auto& fl = flt[(size_t) (ch * 3 + k)]; float lp, bp, hp; fl.tick (x, lp, bp, hp);
                    y += gains[(size_t) k] * bp * fl.k;   // k * bp = unity gain at the formant peak
                }
                // BODY keeps the low end under the vowel (the sub of a bass is never thrown away by the formant band-passes):
                // the dry signal below ~150 Hz plus a little of the full dry, both scaled by BODY
                const float lo = bodyLp[(size_t) (ch * 2 + 1)].lp (bodyLp[(size_t) (ch * 2)].lp (dry));
                y = dc[ch].process (y * 2.2f + bd * (1.4f * lo + 0.3f * dry));
                if (ch == 0) L[i] = y; else R[i] = y;
            }
            pk = std::max (pk, std::abs (L[i]));
        }
        if (tm) { tm->formantVowel.store (curVowel); tm->formantLevel.store (pk); }
    }
private:
    P vowel, mouth, reso, shift, drive, move, mdiv, mdepth, mlen, body, mshape;
    std::array<Svf, 6> flt; std::array<float, 3> gains { 1, 0.5f, 0.3f }; std::array<DcBlock, 2> dc; std::array<OnePole, 4> bodyLp; EnvFollow inEnv;
    ExpSmooth vSm, dSm, bSm; bendlane::Shape cur; uint32_t seen = 0xffffffffu; double sr = 48000; int cnt = 0; float curVowel = 0;
    long shCycle = -1; uint32_t shRng = 7u; float shVal = 0;
};

// ================================================================ GHOST GATE + PUMP
struct SideChainFeed { const float* l = nullptr; const float* r = nullptr; int n = 0, off = 0; bool connected = false; };
// the pump's gain target at lane phase ph (0..1) for the rhythmic modes: duck at the pulse, HOLD, then recover over the rest of
// 75 % of the period with PUMP SHAPE as the curve (shared by the audio and the page's curve display)
inline float pumpTargetAt (double ph, float depth, float shapeEx, float holdFrac) noexcept
{
    const float recStart = clampf (holdFrac, 0.0f, 0.6f);
    const float rec = clampf (((float) ph - recStart) / std::max (0.05f, 0.75f - recStart), 0.0f, 1.0f);
    return 1.0f - depth * (1.0f - std::pow (rec, shapeEx));
}
class BoxGateUnit : public GateUnit
{
public:
    const bendlane::SharedShape* pumpShape = nullptr;
    const SideChainFeed* sc = nullptr;
    Telemetry* ptm = nullptr;
    static constexpr double kLookMs = 5.0;
    void attach (juce::AudioProcessorValueTreeState& s) override
    {
        GateUnit::attach (s);
        auto b = [&s] (P& p, const char* id, float d) { p.bind (s, pid (uGate, id), d); };
        b (pmode, "pmode", 0); b (pdepth, "pdepth", 0.7f); b (pshape, "pshape", 0.5f); b (plen, "plen", 1); b (prel, "prel", 180); b (pthr, "pthr", -24); b (ponly, "ponly", 1);
        b (patk, "patk", 4); b (phold, "phold", 0); b (plook, "plook", 0);
        hostOn = s.getRawParameterValue (pid (uGate, "on")); hostMode = s.getRawParameterValue (pid (uGate, "pmode")); hostLook = s.getRawParameterValue (pid (uGate, "plook"));
    }
    void prepare (double sr_, int mb) override
    {
        GateUnit::prepare (sr_, mb); psr = sr_; ga = 1.0f - (float) std::exp (-1.0 / (0.0008 * psr));
        lookSamples = (int) std::lround (kLookMs * 0.001 * psr);
        look.prepare (lookSamples, psr); dryD.prepare (lookSamples, psr);
        resetPump();
    }
    void reset() override { GateUnit::reset(); resetPump(); }
    // SIDECHAIN + LOOKAHEAD: the audio runs 5 ms behind the key, so the duck is already down when the kick lands
    bool lookaheadOn() const noexcept { return hostMode != nullptr && (int) std::lround (hostMode->load()) == pmSidechain && hostLook != nullptr && hostLook->load() > 0.5f; }
    int latency() const override { return lookaheadOn() && hostOn != nullptr && hostOn->load() > 0.5f ? lookSamples : 0; }
    int maxLatency() const noexcept { return lookSamples; }
    void delayDry (float* L, float* R, int n) override { dryD.process (L, R, n, latency()); }
    void clearLines() override { look.clear(); dryD.clear(); }
    void idle (const float*, const float*, int) override { keyHold = 0; }
    void process (float* L, float* R, int n, const BlockCtx& c) override
    {
        const int md = juce::jlimit (0, 5, pmode.i());
        if (md == pmOff || ! ponly.b()) GateUnit::process (L, R, n, c);
        if (md == pmOff) { g1 = g2 = 1.0f; look.process (L, R, n, 0); if (ptm) { ptm->pumpGain.store (1.0f); ptm->pumpKey.store (0); } return; }
        if (pumpShape != nullptr) pumpShape->fetch (cur, seen);
        if (cur.n == 0) cur.add (0, 0);
        const float depth = clampf (pdepth(), 0.0f, 1.0f), ex = std::exp2 ((clampf (pshape(), 0, 1) - 0.5f) * 3.0f);
        const double period = md == pmFloor ? 1.0 : md == pmHalf ? 2.0 : md == pmFast ? 0.5 : pumpLenBeats (plen.i());
        const float holdMs = clampf (phold(), 0.0f, 200.0f);
        const float holdFrac = (float) (holdMs * 0.001 / (period * 60.0 / c.bpm));
        const int holdSamples = (int) (holdMs * 0.001 * psr);
        const float atkA = 1.0f - (float) std::exp (-1.0 / (clampf (patk(), 0.5f, 50.0f) * 0.001 * psr));   // ATTACK: the downward slew
        env.set (psr, 0.8f, clampf (prel(), 10.0f, 1000.0f));
        const float thr = pthr();
        const bool connected = sc != nullptr && sc->connected && sc->l != nullptr && sc->off + n <= sc->n;
        // a connected key that has been silent for a second falls back to the input (and the page says so)
        if (connected)
        {
            float kpk = 0; for (int i = 0; i < n; i += 4) kpk = std::max (kpk, std::abs (sc->l[sc->off + i]));
            if (kpk > 1.0e-3f) keyHold = (int) psr; else keyHold = std::max (0, keyHold - n);
        }
        else keyHold = 0;
        const bool ext = connected && keyHold > 0;
        if (ptm) ptm->pumpKey.store (md == pmSidechain ? (ext ? 2 : connected ? 1 : 0) : 0);
        // lookahead: the audio is delayed, the key is not (nothing to delay in the rhythmic modes: they know the future from the clock)
        look.process (L, R, n, latency());
        float gMin = 1.0f;
        for (int i = 0; i < n; ++i)
        {
            float tgt;
            if (md == pmSidechain)
            {
                const float k = ext ? 0.5f * (std::abs (sc->l[sc->off + i]) + std::abs (sc->r ? sc->r[sc->off + i] : sc->l[sc->off + i])) : 0.5f * (std::abs (L[i]) + std::abs (R[i]));
                const float e = env.process (k);
                const float over = clampf ((gainToDb (e) - thr) / 18.0f, 0.0f, 1.0f);
                tgt = 1.0f - depth * std::pow (over, 1.0f / ex);
                // HOLD: the deepest duck of the last hold time is kept before the release may recover
                if (tgt <= heldTgt || holdLeft <= 0) { heldTgt = tgt; holdLeft = holdSamples; } else { --holdLeft; tgt = heldTgt; }
            }
            else if (md == pmDrawn)
            {
                const double ph = positiveMod (c.ppqAt (i), period) / period;
                tgt = 1.0f - depth * clampf (-cur.valueAt (ph), 0.0f, 1.0f);
            }
            else tgt = pumpTargetAt (positiveMod (c.ppqAt (i), period) / period, depth, ex, holdFrac);
            g1 += (tgt < g1 ? atkA : ga) * (tgt - g1); g2 += ga * (g1 - g2);   // ATTACK down, two 0.8 ms poles on top: never a click
            L[i] *= g2; R[i] *= g2; gMin = std::min (gMin, g2);
        }
        if (ptm) ptm->pumpGain.store (gMin);
    }
private:
    void resetPump() { g1 = g2 = 1.0f; env.reset(); heldTgt = 1.0f; holdLeft = 0; keyHold = 0; }
    P pmode, pdepth, pshape, plen, prel, pthr, ponly, patk, phold, plook; std::atomic<float>* hostOn = nullptr; std::atomic<float>* hostMode = nullptr; std::atomic<float>* hostLook = nullptr;
    EnvFollow env; bendlane::Shape cur; uint32_t seen = 0xffffffffu; double psr = 48000; float g1 = 1, g2 = 1, ga = 0.02f, heldTgt = 1.0f;
    int holdLeft = 0, keyHold = 0, lookSamples = 240; LatentDry look, dryD;
};

inline void addBoxUnits2Parameters (Layout& l)
{
    auto T = [] (int u, const char* s) { return juce::String ("FX ") + unitInfo (u).title + " " + s; };
    // GHOST SPECTRA
    addBool (l, pid (uSpectra, "freeze"), T (uSpectra, "Freeze"), false);
    addPct (l, pid (uSpectra, "smear"), T (uSpectra, "Smear"), 0.3f);
    addPct (l, pid (uSpectra, "scatter"), T (uSpectra, "Scatter"), 0.0f);
    addFloat (l, pid (uSpectra, "shift"), T (uSpectra, "Shift"), juce::NormalisableRange<float> (-24.0f, 24.0f), 0.0f, "st", 1);
    addPct (l, pid (uSpectra, "ghost"), T (uSpectra, "Ghost"), 0.5f);
    addPct (l, pid (uSpectra, "motion"), T (uSpectra, "Motion"), 0.0f);
    addChoice (l, pid (uSpectra, "mdiv"), T (uSpectra, "Motion Division"), divNames (echoDivs()), 14);
    addFloat (l, pid (uSpectra, "tilt"), T (uSpectra, "Tilt"), juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f, "", 2);
    addBool (l, pid (uSpectra, "hq"), T (uSpectra, "HQ Bounce"), false);
    addChoice (l, pid (uSpectra, "fsync"), T (uSpectra, "Freeze Sync"), spectraSyncNames(), 0);
    addFloat (l, pid (uSpectra, "thresh"), T (uSpectra, "Threshold"), juce::NormalisableRange<float> (-90.0f, 0.0f), -90.0f, "dB", 0);
    // BASS LAB
    addFloat (l, pid (uBassLab, "xlo"), T (uBassLab, "Low Split"), skewRange (40.0f, 400.0f, 120.0f), 120.0f, "Hz", 0);
    addFloat (l, pid (uBassLab, "xhi"), T (uBassLab, "High Split"), skewRange (700.0f, 8000.0f, 2200.0f), 2200.0f, "Hz", 0);
    addPct (l, pid (uBassLab, "lowwidth"), T (uBassLab, "Low Width"), 1.0f);
    addPct (l, pid (uBassLab, "sub"), T (uBassLab, "Sub"), 0.0f);
    addPct (l, pid (uBassLab, "growl"), T (uBassLab, "Growl"), 0.0f);
    addPct (l, pid (uBassLab, "edge"), T (uBassLab, "Edge"), 0.0f);
    addPct (l, pid (uBassLab, "translate"), T (uBassLab, "Translate"), 0.0f);
    addFloat (l, pid (uBassLab, "low"), T (uBassLab, "Low Level"), juce::NormalisableRange<float> (-18.0f, 12.0f), 0.0f, "dB", 1);
    addFloat (l, pid (uBassLab, "mid"), T (uBassLab, "Mid Level"), juce::NormalisableRange<float> (-18.0f, 12.0f), 0.0f, "dB", 1);
    addFloat (l, pid (uBassLab, "high"), T (uBassLab, "High Level"), juce::NormalisableRange<float> (-18.0f, 12.0f), 0.0f, "dB", 1);
    addBool (l, pid (uBassLab, "auto"), T (uBassLab, "Gain Compensation"), true);
    addFloat (l, pid (uBassLab, "out"), T (uBassLab, "Output"), juce::NormalisableRange<float> (-24.0f, 12.0f), 0.0f, "dB", 1);
    addFloat (l, pid (uBassLab, "gfreq"), T (uBassLab, "Growl Frequency"), skewRange (300.0f, 1500.0f, 750.0f), 750.0f, "Hz", 0);
    addPct (l, pid (uBassLab, "gmove"), T (uBassLab, "Growl Move"), 0.0f);
    addChoice (l, pid (uBassLab, "gdiv"), T (uBassLab, "Growl Sync"), divNames (echoDivs()), 6);
    // DOORWAY (internal key "portal")
    addPct (l, pid (uPortal, "size"), T (uPortal, "Size"), 0.6f);
    addChoice (l, pid (uPortal, "shape"), T (uPortal, "Shape"), portalShapeNames(), 1);
    addChoice (l, pid (uPortal, "move"), T (uPortal, "Movement"), portalMoveNames(), 1);
    addPct (l, pid (uPortal, "density"), T (uPortal, "Density"), 0.7f);
    addFloat (l, pid (uPortal, "decay"), T (uPortal, "Decay"), skewRange (0.3f, 30.0f, 4.0f), 4.0f, "s", 2);
    addPct (l, pid (uPortal, "transform"), T (uPortal, "Transform"), 0.0f);
    addBool (l, pid (uPortal, "freeze"), T (uPortal, "Freeze"), false);
    addFloat (l, pid (uPortal, "predelay"), T (uPortal, "Pre-delay"), skewRange (0.0f, 250.0f, 40.0f), 20.0f, "ms", 0);
    addFloat (l, pid (uPortal, "tone"), T (uPortal, "Tone"), juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f, "", 2);
    addPct (l, pid (uPortal, "early"), T (uPortal, "Early Reflections"), 0.5f);
    // GHOST FORMANT
    {   // VOWEL reads as letters: "A", "A>E 40%", "I" ...
        auto attrs = juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { static const char* vw[] { "A", "E", "I", "O", "U" }; const int a = juce::jlimit (0, 3, (int) v); const int pc = juce::roundToInt ((v - (float) a) * 100);
                                                               if (pc < 2 || v >= 4.0f) return juce::String (vw[juce::jlimit (0, 4, (int) std::lround (v))]);
                                                               if (pc >= 100) return juce::String (vw[a + 1]);
                                                               return juce::String (vw[a]) + ">" + vw[a + 1] + " " + juce::String (pc) + "%"; })
            .withValueFromStringFunction ([] (const juce::String& t) { const auto s = t.trim().toUpperCase(); const juce::String vw ("AEIOU"); if (s.length() == 1 && vw.containsChar (s[0])) return (float) vw.indexOfChar (s[0]);
                                                                       if (s.length() > 2 && s[1] == '>' && vw.containsChar (s[0]))   // "A>E 40%" (what the text shows)
                                                                           return juce::jlimit (0.0f, 4.0f, (float) vw.indexOfChar (s[0]) + s.fromFirstOccurrenceOf (" ", false, false).retainCharacters ("0123456789.").getFloatValue() / 100.0f);
                                                                       return s.getFloatValue(); });
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { pid (uFormant, "vowel"), 1 }, T (uFormant, "Vowel"), juce::NormalisableRange<float> (0.0f, 4.0f), 0.0f, attrs));
    }
    addPct (l, pid (uFormant, "mouth"), T (uFormant, "Mouth Size"), 0.5f);
    addPct (l, pid (uFormant, "reso"), T (uFormant, "Resonance"), 0.5f);
    addFloat (l, pid (uFormant, "shift"), T (uFormant, "Formant Shift"), juce::NormalisableRange<float> (-12.0f, 12.0f), 0.0f, "st", 1);
    addPct (l, pid (uFormant, "drive"), T (uFormant, "Drive"), 0.2f);
    addChoice (l, pid (uFormant, "move"), T (uFormant, "Movement"), formantMoveNames(), 0);
    addChoice (l, pid (uFormant, "mdiv"), T (uFormant, "Movement Division"), divNames (echoDivs()), 9);
    addPct (l, pid (uFormant, "mdepth"), T (uFormant, "Movement Depth"), 0.5f);
    addChoice (l, pid (uFormant, "mlen"), T (uFormant, "Drawn Length"), bendlane::lengthNames(), 1);
    addPct (l, pid (uFormant, "body"), T (uFormant, "Body"), 0.3f);
    addChoice (l, pid (uFormant, "mshape"), T (uFormant, "LFO Shape"), formantLfoNames(), 0);
    // GHOST GATE pump (Box only)
    addChoice (l, pid (uGate, "pmode"), T (uGate, "Pump Mode"), pumpModeNames(), 0);
    addPct (l, pid (uGate, "pdepth"), T (uGate, "Pump Depth"), 0.7f);
    addPct (l, pid (uGate, "pshape"), T (uGate, "Pump Shape"), 0.5f);
    addChoice (l, pid (uGate, "plen"), T (uGate, "Pump Length"), pumpLenNames(), 1);
    addFloat (l, pid (uGate, "prel"), T (uGate, "Pump Release"), skewRange (10.0f, 1000.0f, 180.0f), 180.0f, "ms", 0);
    addFloat (l, pid (uGate, "pthr"), T (uGate, "Sidechain Threshold"), juce::NormalisableRange<float> (-60.0f, 0.0f), -24.0f, "dB", 1);
    addBool (l, pid (uGate, "ponly"), T (uGate, "Pump Only"), true);
    addFloat (l, pid (uGate, "patk"), T (uGate, "Pump Attack"), skewRange (0.5f, 50.0f, 4.0f), 4.0f, "ms", 1);
    addFloat (l, pid (uGate, "phold"), T (uGate, "Pump Hold"), juce::NormalisableRange<float> (0.0f, 200.0f), 0.0f, "ms", 0);
    addBool (l, pid (uGate, "plook"), T (uGate, "Sidechain Lookahead"), false);
}

} // namespace gpfx
#endif
