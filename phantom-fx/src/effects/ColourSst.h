// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX Colour effects built on sst-filters / sst-waveshapers (GPL-3.0): PHANTOM FILTER, GHOST FORMANT, CRACKLE, WRECKER.
#pragma once
#include "Catalog.h"
#include "../dsp/Smoothing.h"
#include "../dsp/SstWrap.h"
#include "../dsp/Modulators.h"
#include "../dsp/DelayLine.h"
#include <chowdsp_filters/chowdsp_filters.h>
#include "../dsp/FilterModels.h"

namespace phantom
{
// A module that carries a catalog unit's identity and declared controls (unitParams) and adds a real engine.
class CatalogEffect : public EffectBase
{
public:
    explicit CatalogEffect (int unit) : u (unit) { const auto ps = unitParams (unit); for (size_t i = 0; i < ps.size(); ++i) index[ps[i].id] = (int) i; }
    const char* key() const override { return unitDef (u).key; }
    const char* title() const override { return unitDef (u).title; }
    // the drive-type controls of a nonlinear module glide over 60 ms: their harmonics would otherwise carry the host's automation steps
    std::vector<ParamSpec> parameters() const override
    {
        auto ps = unitParams (u);
        if (isNonlinear()) for (auto& q : ps) if (q.isContinuous() && (q.id.find ("drive") != std::string::npos || q.id == "fat" || q.id == "crush")) q.smoothMs = juce::jmax (q.smoothMs, 60.0f);
        return ps;
    }
protected:
    int id (const char* s) const { auto it = index.find (s); return it == index.end() ? -1 : it->second; }   // parameter index by id (prepare time)
    int u; std::map<std::string, int> index;
};

// ================================================================ 14 PHANTOM FILTER: the real Surge filters + LFO + envelope
class PhantomFilterFx final : public CatalogEffect
{
public:
    PhantomFilterFx() : CatalogEffect (uFilter) {}
    bool isNonlinear() const override { return true; }     // the ladder / diode / warp models are nonlinear
    int oversamplingFactor() const override { return 2; }
    bool isRhythmic() const override { return true; }
    bool hasNullSetting() const override { return false; }   // a filter always filters
    bool changesLevelOnPurpose() const override { return true; }   // a low-pass at 1.2 kHz (its default) removes the highs: the level drop is the effect
    void neutralise (std::vector<float>& v) const override { v[(size_t) id ("cutoff")] = 20000.0f; v[(size_t) id ("reso")] = 0.0f; v[(size_t) id ("drive")] = 0.0f; v[(size_t) id ("lfoamt")] = 0.0f; v[(size_t) id ("envamt")] = 0.0f; }
    void prepareEffect (double sampleRate, int, int) override
    {
        for (auto& f : f1) f.prepare (sampleRate); for (auto& f : f2) f.prepare (sampleRate);
        x1.prepare (sampleRate, kModelChunk); x2.prepare (sampleRate, kModelChunk);
        lfo.prepare (sampleRate); env.prepare (sampleRate); dc.prepare (sampleRate);
        pType = id ("type"); pPoles = id ("poles"); pRouting = id ("routing"); pCut = id ("cutoff"); pRes = id ("reso"); pSpread = id ("spread"); pDrive = id ("drive");
        pLfoAmt = id ("lfoamt"); pLfoShape = id ("lfoshape"); pLfoSync = id ("lfosync"); pLfoDiv = id ("lfodiv"); pLfoRate = id ("lforate"); pEnvAmt = id ("envamt"); pEnvRel = id ("envrel"); pEnvAtk = id ("envatk"); pSafe = id ("safe");
    }
    void resetEffect() override { for (auto& f : f1) f.reset(); for (auto& f : f2) f.reset(); x1.reset(); x2.reset(); lfo.reset(); env.reset(); dc.reset(); }
    static constexpr int kSurgeTypes = 31, kModelChunk = 32;   // TYPE 0..30: the Surge models; 31 on: FilterModels (dsp/FilterModels.h)
    void process (juce::dsp::AudioBlock<float> block, const ModulationContext& ctx) noexcept override
    {
        beginBlock (ctx);
        const int n = (int) block.getNumSamples(), ch = (int) block.getNumChannels();
        auto* L = block.getChannelPointer (0); auto* R = ch > 1 ? block.getChannelPointer (1) : L;
        // type / slope -> Surge filter + subtype (the 4 Ghost Box types first, then the rest of the 31)
        const bool p24 = choice (pPoles) == 1; const int t = choice (pType);
        auto pick = [&] (int which, sst::filters::FilterType& ft, sst::filters::FilterSubType& st)
        {
            using namespace sst::filters; st = st_Standard;
            switch (which)
            {
                case 0: ft = p24 ? fut_lp24 : fut_lp12; break;      case 1: ft = p24 ? fut_bp24 : fut_bp12; break;
                case 2: ft = p24 ? fut_hp24 : fut_hp12; break;      case 3: ft = p24 ? fut_notch24 : fut_notch12; break;
                case 4: ft = fut_lpmoog; st = p24 ? st_lpmoog_24dB : st_lpmoog_12dB; break;
                case 5: ft = fut_hp24; st = st_Driven; break;       case 6: ft = fut_bp24; st = st_Driven; break;
                case 7: ft = p24 ? fut_obxd_4pole : fut_obxd_2pole_lp; break; case 8: ft = fut_obxd_2pole_hp; break; case 9: ft = fut_obxd_2pole_bp; break; case 10: ft = fut_obxd_2pole_n; break;
                case 11: ft = fut_k35_lp; break;                    case 12: ft = fut_k35_hp; break;
                case 13: ft = fut_diode; st = p24 ? st_diode_24dB : st_diode_12dB; break;
                case 14: ft = fut_comb_pos; break;                  case 15: ft = fut_comb_neg; break;
                case 16: ft = fut_tripole; break;
                case 17: ft = fut_vintageladder; st = st_vintage_type1; break; case 18: ft = fut_vintageladder; st = st_vintage_type2; break; case 19: ft = fut_vintageladder; st = st_vintage_type3; break;
                case 20: ft = fut_cutoffwarp_lp; st = st_cutoffwarp_tanh2; break; case 21: ft = fut_cutoffwarp_hp; st = st_cutoffwarp_tanh2; break; case 22: ft = fut_cutoffwarp_bp; st = st_cutoffwarp_tanh2; break; case 23: ft = fut_cutoffwarp_n; st = st_cutoffwarp_tanh2; break;
                case 24: ft = fut_resonancewarp_lp; st = st_resonancewarp_tanh1; break; case 25: ft = fut_resonancewarp_hp; st = st_resonancewarp_tanh1; break; case 26: ft = fut_resonancewarp_bp; st = st_resonancewarp_tanh1; break; case 27: ft = fut_resonancewarp_n; st = st_resonancewarp_tanh1; break;
                case 28: ft = fut_apf; break;                       case 29: ft = fut_cytomic_svf; break;
                default: ft = fut_notch24; break;
            }
        };
        if (t >= kSurgeTypes) { processModels (L, R, n, ch, t - kSurgeTypes, p24, ctx); return; }
        sst::filters::FilterType ft; sst::filters::FilterSubType st; pick (t, ft, st);
        for (auto& f : f1) f.setType (ft, st); for (auto& f : f2) f.setType (ft, st);
        const int routing = choice (pRouting);
        // modulation sources once per block (the smoothers glide the result): LFO (host-locked) + input envelope
        lfo.setShape ((LfoShape) choice (pLfoShape)); lfo.setSync (on (pLfoSync), choice (pLfoDiv)); lfo.setRateHz (target (pLfoRate)); lfo.beginBlock (ctx.tempo);
        env.setTimes (target (pEnvAtk), target (pEnvRel));
        const float safeRes = on (pSafe) ? 0.92f : 1.0f;
        float lastL = 0, lastR = 0;
        for (int i = 0; i < n; ++i)
        {
            const float lv = lfo.next(); float e = 0; { const float m = juce::jmax (std::abs (L[i]), std::abs (R[i])); e = env.next (m); }
            if ((i & 31) == 0)
            {   // coefficient update every 32 samples (the maker interpolates in between)
                const float cut = p (pCut).skip (32), res = juce::jmin (safeRes, p (pRes).skip (32)), spread = p (pSpread).skip (32);
                const float oct = p (pLfoAmt).skip (32) * lv * 4.0f + p (pEnvAmt).skip (32) * juce::jlimit (0.0f, 1.0f, e * 3.0f) * 4.0f;
                const float c1 = juce::jlimit (20.0f, 20000.0f, cut * std::pow (2.0f, oct)), c2 = juce::jlimit (20.0f, 20000.0f, c1 * std::pow (2.0f, spread));
                f1[0].set (c1, res); f2[0].set (c2, res);
            }
            const float drive = 1.0f + p (pDrive).next() * 7.0f;
            float l = L[i] * drive, r = R[i] * drive;
            if (routing == 0) { f1[0].process (l, r); }
            else if (routing == 1) { f1[0].process (l, r); f2[0].process (l, r); }
            else { float l2 = l, r2 = r; f1[0].process (l, r); f2[0].process (l2, r2); l = 0.5f * (l + l2); r = 0.5f * (r + r2); }
            l /= drive; r /= drive;
            dc.process (l, r);
            // safety: a self-oscillating filter never runs away
            l = juce::jlimit (-4.0f, 4.0f, l); r = juce::jlimit (-4.0f, 4.0f, r);
            L[i] = l; if (ch > 1) R[i] = r; lastL = l; lastR = r;
        }
        juce::ignoreUnused (lastL, lastR);
    }
private:
    // the extra models: the same controls, in chunks of up to 32 samples (one coefficient set per chunk, as the Surge models)
    void processModels (float* L, float* R, int n, int ch, int model, bool p24, const ModulationContext& ctx) noexcept
    {
        x1.setModel (model); x2.setModel (model);
        const int routing = choice (pRouting);
        lfo.setShape ((LfoShape) choice (pLfoShape)); lfo.setSync (on (pLfoSync), choice (pLfoDiv)); lfo.setRateHz (target (pLfoRate)); lfo.beginBlock (ctx.tempo);
        env.setTimes (target (pEnvAtk), target (pEnvRel));
        const bool safe = on (pSafe); const float safeRes = safe ? 0.92f : 1.0f;
        float drv[kModelChunk], al[kModelChunk], ar[kModelChunk], bl[kModelChunk], br[kModelChunk];
        for (int i0 = 0; i0 < n; i0 += kModelChunk)
        {
            const int k = juce::jmin (kModelChunk, n - i0); float lv0 = 0, e0 = 0;
            for (int j = 0; j < k; ++j)
            {
                const float lv = lfo.next(), e = env.next (juce::jmax (std::abs (L[i0 + j]), std::abs (R[i0 + j])));
                if (j == 0) { lv0 = lv; e0 = e; }
                drv[j] = 1.0f + p (pDrive).next() * 7.0f; al[j] = L[i0 + j] * drv[j]; ar[j] = R[i0 + j] * drv[j];
            }
            const float cut = p (pCut).skip (k), res = juce::jmin (safeRes, p (pRes).skip (k)), spread = p (pSpread).skip (k);
            const float oct = p (pLfoAmt).skip (k) * lv0 * 4.0f + p (pEnvAmt).skip (k) * juce::jlimit (0.0f, 1.0f, e0 * 3.0f) * 4.0f;
            const float c1 = juce::jlimit (20.0f, 20000.0f, cut * std::pow (2.0f, oct)), c2 = juce::jlimit (20.0f, 20000.0f, c1 * std::pow (2.0f, spread));
            if (routing == 0) x1.process (al, ar, k, c1, res, p24, safe);
            else if (routing == 1) { x1.process (al, ar, k, c1, res, p24, safe); x2.process (al, ar, k, c2, res, p24, safe); }
            else
            {
                std::copy (al, al + k, bl); std::copy (ar, ar + k, br);
                x1.process (al, ar, k, c1, res, p24, safe); x2.process (bl, br, k, c2, res, p24, safe);
                for (int j = 0; j < k; ++j) { al[j] = 0.5f * (al[j] + bl[j]); ar[j] = 0.5f * (ar[j] + br[j]); }
            }
            for (int j = 0; j < k; ++j)
            {
                float l = al[j] / drv[j], r = ar[j] / drv[j];
                dc.process (l, r);
                if (! std::isfinite (l) || ! std::isfinite (r)) { l = r = 0.0f; x1.reset(); x2.reset(); }   // never a NaN out of a model
                L[i0 + j] = juce::jlimit (-4.0f, 4.0f, l); if (ch > 1) R[i0 + j] = juce::jlimit (-4.0f, 4.0f, r);
            }
        }
    }
    std::array<SstFilter, 1> f1, f2; FilterModels x1, x2; Lfo lfo; EnvelopeFollower env; DcBlock dc;
    int pType = 0, pPoles = 0, pRouting = 0, pCut = 0, pRes = 0, pSpread = 0, pDrive = 0, pLfoAmt = 0, pLfoShape = 0, pLfoSync = 0, pLfoDiv = 0, pLfoRate = 0, pEnvAmt = 0, pEnvRel = 0, pEnvAtk = 0, pSafe = 0;
};

// ================================================================ 12 GHOST FORMANT: a vowel bank of 4 resonators (Cytomic SVF bandpasses) per side
class GhostFormantFx final : public CatalogEffect
{
public:
    GhostFormantFx() : CatalogEffect (uFormant) {}
    bool isNonlinear() const override { return true; }   // DRIVE saturates
    int oversamplingFactor() const override { return 2; }   // a gentle drive into a linear vowel bank: 2x keeps it alias-free
    bool hasNullSetting() const override { return false; }   // a vowel bank always shapes
    int getLatencySamples() const override { return Shaper::latencySamples; }
    void neutralise (std::vector<float>& v) const override { v[(size_t) id ("drive")] = 0.0f; v[(size_t) id ("reso")] = 0.0f; v[(size_t) id ("body")] = 1.0f; v[(size_t) id ("move")] = 0.0f; }
    void prepareEffect (double sampleRate, int, int chans) override
    {
        drive.prepare (chans); drive.setKind (Shaper::Kind::Tanh);
        for (auto& b : bank) b.prepare (sampleRate); lfo.prepare (sampleRate); env.prepare (sampleRate); env.setTimes (5.0f, 150.0f); dc.prepare (sampleRate); sr = sampleRate;
        pVowel = id ("vowel"); pMouth = id ("mouth"); pReso = id ("reso"); pShift = id ("shift"); pDrive = id ("drive"); pMove = id ("move"); pDiv = id ("mdiv"); pDepth = id ("mdepth"); pBody = id ("body"); pShape = id ("mshape"); pOffset = id ("offset");
        buildLevelTable();
    }
    void resetEffect() override { for (auto& b : bank) b.reset(); lfo.reset(); env.reset(); dc.reset(); drive.reset(); }
    void process (juce::dsp::AudioBlock<float> block, const ModulationContext& ctx) noexcept override
    {
        beginBlock (ctx);
        const int n = (int) block.getNumSamples(), ch = (int) block.getNumChannels();
        auto* L = block.getChannelPointer (0); auto* R = ch > 1 ? block.getChannelPointer (1) : L;
        const int move = choice (pMove); const int shape = choice (pShape);
        lfo.setShape (shape == 5 ? LfoShape::Sine : (LfoShape) juce::jmin (shape, 5)); lfo.setSync (true, choice (pDiv)); lfo.beginBlock (ctx.tempo);
        for (int i = 0; i < n; ++i)
        {
            const float lv = lfo.next(); const float e = env.next (juce::jmax (std::abs (L[i]), std::abs (R[i])));
            if ((i & 15) == 0)
            {
                float v = p (pVowel).skip (16);
                if (move == 1) v += p (pDepth).skip (16) * 2.0f * (shape == 5 ? juce::jlimit (0.0f, 1.0f, e * 3.0f) * 2.0f - 1.0f : lv);
                else if (move == 2) v += p (pDepth).skip (16) * 2.0f * lv;   // DRAWN: the lane editor arrives with Phase 5; until then the LFO shape drives it
                v = juce::jlimit (0.0f, 4.0f, v);
                const float scale = (0.6f + p (pMouth).skip (16) * 0.8f) * std::pow (2.0f, p (pShift).skip (16) / 12.0f), res = p (pReso).skip (16), off = p (pOffset).skip (16) * 0.08f;
                setBank (bank.data(), amp, v, scale, res, off);
                bodyNow = p (pBody).skip (16);
                levelTarget = level (v, res, bodyNow, scale);   // keeps the vowel bank at the input's level whatever the vowel, mouth or resonance
            }
            const float dr = p (pDrive).next(); levelNow += 0.002f * (levelTarget - levelNow);
            float xl = L[i], xr = R[i];
            if (dr > 0.0005f) { const float g = 1.0f + dr * 6.0f; drive.process (xl, xr, g, 0.0f); xl /= g; xr /= g; } else drive.pass (xl, xr);   // unity small-signal gain: DRIVE rounds the peaks, it does not turn it up
            float yl = 0, yr = 0;
            for (int k = 0; k < 4; ++k) { float l = xl, r = xr; bank[(size_t) k].process (l, r); yl += l * amp[k]; yr += r * amp[k]; }
            yl = (yl * 0.8f + xl * bodyNow) * levelNow; yr = (yr * 0.8f + xr * bodyNow) * levelNow;
            dc.process (yl, yr);
            L[i] = yl; if (ch > 1) R[i] = yr;
        }
    }
private:
    // classic vowel formant table (Hz) and relative amplitudes: A E I O U, four formants each
    static constexpr float F[5][4] { { 730, 1090, 2440, 3400 }, { 530, 1840, 2480, 3500 }, { 270, 2290, 3010, 3600 }, { 570, 840, 2410, 3400 }, { 300, 870, 2240, 3500 } };
    static constexpr float A[5][4] { { 1.0f, 0.5f, 0.25f, 0.12f }, { 1.0f, 0.35f, 0.3f, 0.12f }, { 1.0f, 0.2f, 0.3f, 0.15f }, { 1.0f, 0.45f, 0.2f, 0.1f }, { 1.0f, 0.3f, 0.15f, 0.08f } };
    static void setBank (Svf* bk, float* am, float v, float scale, float reso, float off) noexcept
    {
        const int a = juce::jmin (3, (int) v); const float fr = v - (float) a, res = 0.6f + reso * 0.38f;
        for (int k = 0; k < 4; ++k)
        {
            const float f = (F[a][k] + (F[a + 1][k] - F[a][k]) * fr) * scale; am[k] = A[a][k] + (A[a + 1][k] - A[a][k]) * fr;
            bk[k].f.setCoeff (sst::filters::CytomicSVF::Mode::Bandpass, juce::jlimit (40.0f, 16000.0f, f * (1.0f - off)), juce::jlimit (40.0f, 16000.0f, f * (1.0f + off)), res, res, bk[k].srInv, 1.0f, 1.0f);
        }
    }
    // level compensation measured once at prepare: white noise through the same bank on a grid of vowel x resonance x body x mouth/shift,
    // stored as 1 / (output RMS / input RMS); read back with linear interpolation
    static constexpr int nV = 9, nR = 5, nB = 3, nS = 5; static constexpr float sLo = 0.3f, sHi = 2.8f;
    float table[nV][nR][nB][nS] {};
    void buildLevelTable()
    {
        // white noise limited to the host's audio band (the module runs oversampled; only 0..20 kHz ever reaches it): brick-wall in the
        // FFT domain, circular, so a second pass through each bank is a steady-state measurement
        constexpr int order = 12, N = 1 << order; std::vector<float> noiseBuf ((size_t) N);
        {
            juce::dsp::FFT fft (order); std::vector<juce::dsp::Complex<float>> x ((size_t) N), y ((size_t) N); unsigned seed = 1;
            for (auto& v : x) { seed = seed * 1664525u + 1013904223u; v = { (float) (seed >> 8) / 8388608.0f - 1.0f, 0.0f }; }
            fft.perform (x.data(), y.data(), false);
            const int top = (int) (juce::jmin (20000.0, 0.45 * sr) * N / sr); for (int k = top; k <= N - top; ++k) y[(size_t) k] = {}; y[0] = {};
            fft.perform (y.data(), x.data(), true); for (int i = 0; i < N; ++i) noiseBuf[(size_t) i] = x[(size_t) i].real();
        }
        std::array<Svf, 4> bk; float am[4];
        for (int iv = 0; iv < nV; ++iv) for (int ir = 0; ir < nR; ++ir) for (int ib = 0; ib < nB; ++ib) for (int is = 0; is < nS; ++is)
        {
            for (auto& b : bk) b.prepare (sr);
            const float scale = sLo * std::pow (sHi / sLo, is / (float) (nS - 1)), body = ib / (float) (nB - 1);
            setBank (bk.data(), am, iv * 4.0f / (nV - 1), scale, ir / (float) (nR - 1), 0.0f);
            double si = 0, so = 0;
            for (int pass = 0; pass < 2; ++pass) for (int i = 0; i < N; ++i)
            {
                const float x = noiseBuf[(size_t) i];
                float y = 0; for (int k = 0; k < 4; ++k) { float l = x, r = x; bk[(size_t) k].process (l, r); y += l * am[k]; }
                y = y * 0.8f + x * body; if (pass == 1) { si += (double) x * x; so += (double) y * y; }
            }
            table[iv][ir][ib][is] = so > 1.0e-12 ? (float) std::sqrt (si / so) : 1.0f;
        }
        levelNow = levelTarget = level (target (pVowel), target (pReso), target (pBody), (0.6f + target (pMouth) * 0.8f) * std::pow (2.0f, target (pShift) / 12.0f));
    }
    float level (float v, float res, float body, float scale) const noexcept
    {
        auto idx = [] (float x, int n, int& i0, float& f) { x = juce::jlimit (0.0f, (float) (n - 1), x); i0 = juce::jmin (n - 2, (int) x); f = x - (float) i0; };
        int a, b, c, d; float fa, fb, fc, fd;
        idx (v * (nV - 1) / 4.0f, nV, a, fa); idx (res * (nR - 1), nR, b, fb); idx (body * (nB - 1), nB, c, fc);
        idx (std::log (juce::jlimit (sLo, sHi, scale) / sLo) / std::log (sHi / sLo) * (nS - 1), nS, d, fd);
        float out = 0;
        for (int i = 0; i < 16; ++i)
        {
            const int da = i & 1, db = (i >> 1) & 1, dc2 = (i >> 2) & 1, dd = (i >> 3) & 1;
            const float w = (da ? fa : 1 - fa) * (db ? fb : 1 - fb) * (dc2 ? fc : 1 - fc) * (dd ? fd : 1 - fd);
            out += w * table[a + da][b + db][c + dc2][d + dd];
        }
        return out;
    }
    std::array<Svf, 4> bank; float amp[4] { 1, 0.5f, 0.25f, 0.12f }; Lfo lfo; EnvelopeFollower env; DcBlock dc; Shaper drive; double sr = 48000.0;
    float levelNow = 1.0f, levelTarget = 1.0f, bodyNow = 0.3f;
    int pVowel = 0, pMouth = 0, pReso = 0, pShift = 0, pDrive = 0, pMove = 0, pDiv = 0, pDepth = 0, pBody = 0, pShape = 0, pOffset = 0;
};

// ================================================================ shared: Ghost Box curve / style names -> the saturation stage
inline Shaper::Kind crackleCurveKind (int c)
{
    using K = Shaper::Kind;   // TUBE, TAPE, DIODE, HARD CLIP, FUZZ, RECTIFY, HALF RECT, FOLD, SINE FOLD, BITCRUSH, DOWNSAMPLE, BROKEN SPEAKER, CRACKED, DYING BATTERY
    static const K m[14] { K::Tanh, K::Tape, K::Diode, K::Hard, K::Hard, K::Rect, K::HalfRect, K::Sine, K::Sine, K::None, K::None, K::Soft5, K::Soft3, K::Hard };
    return m[juce::jlimit (0, 13, c)];
}
inline Shaper::Kind wreckStyleKind (int s)
{
    using K = Shaper::Kind;   // TAPE, TUBE, CONSOLE, TRANSISTOR, PENTODE, FUZZ, FOLD, CRUSH, DECIMATE
    static const K m[9] { K::Tape, K::Tanh, K::Console, K::Transistor, K::Soft5, K::Hard, K::Sine, K::None, K::None };
    return m[juce::jlimit (0, 8, s)];
}

// bitcrush / downsample stage for the digital curves (own glue)
struct Decimator
{
    float holdL = 0, holdR = 0, phase = 0;
    inline void process (float& l, float& r, float amount, bool crushBits) noexcept
    {
        if (amount <= 0.001f) return;
        if (crushBits) { const float steps = std::pow (2.0f, 16.0f - amount * 13.0f); l = std::round (l * steps) / steps; r = std::round (r * steps) / steps; }
        else { const float ratio = 1.0f + amount * 31.0f; phase += 1.0f; if (phase >= ratio) { phase -= ratio; holdL = l; holdR = r; } l = holdL; r = holdR; }
    }
};

// "keeps the level steady": slow RMS followers before and after a stage, the ratio applied as gain (bounded, glided)
struct AutoGain
{
    float inSq = 0, outSq = 0, c = 0.001f, gc = 0.0005f, g = 1.0f, target = 1.0f; int countdown = 0;
    // 200 ms RMS followers; the gain itself glides with a 25 ms time constant (both in seconds, so the same at every oversampling rate)
    void prepare (double sr) noexcept { c = 1.0f - std::exp (-1.0f / (float) (sr * 0.2)); gc = 1.0f - std::exp (-1.0f / (float) (sr * 0.025)); reset(); }
    void reset() noexcept { inSq = outSq = 0; g = target = 1.0f; countdown = 0; }
    inline void feedIn (float l, float r) noexcept { inSq += c * (0.5f * (l * l + r * r) - inSq); }
    inline float gain (float l, float r) noexcept
    {
        outSq += c * (0.5f * (l * l + r * r) - outSq);
        if (--countdown <= 0)   // the ratio of two 200 ms followers moves slowly: its square root is refreshed every 16 samples
        { countdown = 16; target = outSq > 1.0e-10f && inSq > 1.0e-10f ? juce::jlimit (0.05f, 20.0f, std::sqrt (inSq / outSq)) : 1.0f; }
        g += gc * (target - g); return g;
    }
};

// ================================================================ 5 CRACKLE: true 3-band multiband destroyer (LR4 split, per-band curve / drive / mix / level, speaker boxes)
class CrackleFx final : public CatalogEffect
{
public:
    CrackleFx() : CatalogEffect (uCrackle) {}
    bool isNonlinear() const override { return true; }
    int oversamplingFactor() const override { return 8; }
    int getLatencySamples() const override { return Shaper::latencySamples; }
    void neutralise (std::vector<float>& v) const override { for (int k = 0; k < 3; ++k) v[(size_t) id (("b" + std::to_string (k + 1) + "_drive").c_str())] = 0.0f; }
    void prepareEffect (double sampleRate, int maxBlock, int ch) override
    {
        sr = sampleRate; xo.prepare ({ sampleRate, (juce::uint32) maxBlock, (juce::uint32) ch }); for (auto& d : dcs) d.prepare (sampleRate); for (auto& a : ag) a.prepare (sampleRate);
        for (auto& sh : shaper) sh.prepare (ch);
        for (auto& f : pre) f.prepare (sampleRate); for (auto& f : post) f.prepare (sampleRate); for (auto& b : box) b.prepare (sampleRate); batt.prepare (sampleRate); batt.setTimes (30.0f, 400.0f);
        pBands = id ("bands"); pXlo = id ("xlo"); pXhi = id ("xhi"); for (int k = 0; k < 3; ++k) { const auto b = "b" + std::to_string (k + 1) + "_"; pCurve[k] = id ((b + "curve").c_str()); pDrive[k] = id ((b + "drive").c_str()); pMix[k] = id ((b + "mix").c_str()); pLevel[k] = id ((b + "level").c_str()); }
        pPreHp = id ("prehp"); pPreLp = id ("prelp"); pPostHp = id ("posthp"); pPostLp = id ("postlp"); pBox = id ("box"); pBoxMix = id ("boxmix"); pAuto = id ("auto"); pOut = id ("out");
    }
    void resetEffect() override { xo.reset(); for (auto& d : dcs) d.reset(); for (auto& a : ag) a.reset(); for (auto& sh : shaper) sh.reset(); for (auto& f : pre) f.reset(); for (auto& f : post) f.reset(); for (auto& b : box) b.reset(); for (auto& d : dec) d = {}; batt.reset(); firstBlock = true; }
    void process (juce::dsp::AudioBlock<float> block, const ModulationContext& ctx) noexcept override
    {
        beginBlock (ctx);
        const int n = (int) block.getNumSamples(), ch = (int) block.getNumChannels();
        auto* L = block.getChannelPointer (0); auto* R = ch > 1 ? block.getChannelPointer (1) : L;
        const int bands = choice (pBands) + 1;
        for (int k = 0; k < 3; ++k) shaper[(size_t) k].setKind (crackleCurveKind (choice (pCurve[k])));
        xo.setFrequencies (target (pXlo), target (pXhi));
        using M = sst::filters::CytomicSVF::Mode;
        setBox (choice (pBox));
        const bool autoGain = on (pAuto);
        // the cuts glide per sample (coefficients recomputed only while a knob moves) and fade out of the path over their last 500 Hz / 2 Hz, so
        // at their ends the signal is untouched and nothing switches abruptly
        auto cutSet = [&] (Svf& f, M m, ParamSmoother& ps) -> float { const float v = ps.next(); if (ps.isSmoothing() || firstBlock) f.set (m, v, 0.0f); return m == M::Highpass ? juce::jlimit (0.0f, 1.0f, (v - 10.0f) * 0.5f) : juce::jlimit (0.0f, 1.0f, (22000.0f - v) * 0.002f); };
        auto cut = [] (Svf& f, float& l, float& r, float amt) { if (amt <= 0.0f) return; float fl = l, fr = r; f.process (fl, fr); l += (fl - l) * amt; r += (fr - r) * amt; };
        for (int i = 0; i < n; ++i) { float l = L[i], r = R[i]; cut (pre[0], l, r, cutSet (pre[0], M::Highpass, p (pPreHp))); cut (pre[1], l, r, cutSet (pre[1], M::Lowpass, p (pPreLp))); L[i] = l; R[i] = r; }
        juce::dsp::AudioBlock<const float> in (block);
        if (bands > 1) xo.split (in);
        const int rc = juce::jmin (1, ch - 1);
        for (int i = 0; i < n; ++i)
        {
            float outL = 0, outR = 0;
            const float battery = batt.next (juce::jmax (std::abs (L[i]), std::abs (R[i])));
            for (int k = 0; k < bands; ++k)
            {
                float l, r;
                if (bands == 1) { l = L[i]; r = R[i]; }
                else if (bands == 2) { l = k == 0 ? xo.lowBuf.getSample (0, i) : xo.midBuf.getSample (0, i) + xo.highBuf.getSample (0, i); r = k == 0 ? xo.lowBuf.getSample (rc, i) : xo.midBuf.getSample (rc, i) + xo.highBuf.getSample (rc, i); }
                else { auto& b = k == 0 ? xo.lowBuf : k == 1 ? xo.midBuf : xo.highBuf; l = b.getSample (0, i); r = b.getSample (rc, i); }
                const float dryL = l, dryR = r, drive = p (pDrive[k]).next(), mix = p (pMix[k]).next(), lvl = p (pLevel[k]).nextGain();
                const int c = choice (pCurve[k]);
                ag[(size_t) k].feedIn (l, r);
                if (c == 9 || c == 10) { shaper[(size_t) k].pass (l, r); dec[(size_t) k].process (l, r, drive, c == 9); }
                else if (drive > 0.0005f)
                {
                    float g = 1.0f + drive * drive * (c == 4 ? 60.0f : 24.0f), bias = c == 12 ? drive * 0.3f : c == 4 ? drive * 0.15f : 0.0f;   // FUZZ hits harder and leans; CRACKED leans
                    if (c == 13) { g *= 1.0f - juce::jlimit (0.0f, 0.7f, battery * 2.0f) * drive; bias += drive * 0.4f * juce::jlimit (0.0f, 1.0f, battery * 3.0f); }   // DYING BATTERY: starves on loud passages
                    shaper[(size_t) k].process (l, r, g, bias);
                    if (c == 11) { l = l - 0.6f * dryL * drive; r = r - 0.6f * dryR * drive; }   // BROKEN SPEAKER: a torn-cone cancellation
                    if (autoGain) { const float ga = ag[(size_t) k].gain (l, r); l *= ga; r *= ga; }
                    dcs[(size_t) k].process (l, r);
                }
                else shaper[(size_t) k].pass (l, r);   // drive 0: the stage is out of the way (its pad keeps the latency)
                outL += (dryL + (l - dryL) * mix) * lvl; outR += (dryR + (r - dryR) * mix) * lvl;
            }
            float l = outL, r = outR;
            if (boxKind > 0) { float bl = l, br = r; box[0].process (bl, br); box[1].process (bl, br); box[2].process (bl, br); const float bm = p (pBoxMix).next(); l += (bl - l) * bm; r += (br - r) * bm; } else p (pBoxMix).next();
            cut (post[0], l, r, cutSet (post[0], M::Highpass, p (pPostHp))); cut (post[1], l, r, cutSet (post[1], M::Lowpass, p (pPostLp)));
            const float og = p (pOut).nextGain();
            L[i] = l * og; if (ch > 1) R[i] = r * og;
        }
        firstBlock = false;
    }
private:
    bool firstBlock = true;
    void setBox (int kind)
    {
        boxKind = kind; if (kind == 0) return;
        using M = sst::filters::CytomicSVF::Mode;
        static const float hp[9] { 180, 70, 300, 200, 500, 90, 400, 350, 450 }, lp[9] { 6000, 4500, 3400, 4000, 5000, 2500, 3000, 7000, 2800 }, bump[9] { 1200, 2000, 1800, 1500, 2800, 400, 1500, 2500, 2000 }, q[9] { 0.5f, 0.6f, 0.7f, 0.5f, 0.85f, 0.6f, 0.8f, 0.5f, 0.8f };
        const int k = juce::jlimit (0, 8, kind - 1);
        box[0].set (M::Highpass, hp[k], 0.2f); box[1].set (M::Lowpass, lp[k], 0.2f); box[2].set (M::Bell, bump[k], q[k], 2.5f);
    }
    Crossover3 xo; std::array<Shaper, 3> shaper; std::array<DcBlock, 3> dcs; std::array<AutoGain, 3> ag; std::array<Decimator, 3> dec; std::array<Svf, 2> pre, post; std::array<Svf, 3> box; EnvelopeFollower batt; int boxKind = 0; double sr = 48000.0;
    int pBands = 0, pXlo = 0, pXhi = 0, pCurve[3] {}, pDrive[3] {}, pMix[3] {}, pLevel[3] {}, pPreHp = 0, pPreLp = 0, pPostHp = 0, pPostLp = 0, pBox = 0, pBoxMix = 0, pAuto = 0, pOut = 0;
};

// ================================================================ 6 WRECKER: analog-style saturation with bias and sag, tone, cuts, THUMP / STEEP / PUNISH
class WreckerFx final : public CatalogEffect
{
public:
    WreckerFx() : CatalogEffect (uWreck) {}
    bool isNonlinear() const override { return true; }
    int oversamplingFactor() const override { return 8; }
    int getLatencySamples() const override { return Shaper::latencySamples; }
    void neutralise (std::vector<float>& v) const override { v[(size_t) id ("drive")] = 0.0f; }
    void prepareEffect (double sampleRate, int, int ch) override
    {
        dc.prepare (sampleRate); for (auto& f : filt) f.prepare (sampleRate); sagEnv.prepare (sampleRate); sagEnv.setTimes (10.0f, 250.0f); ag.prepare (sampleRate); shaper.prepare (ch);
        pStyle = id ("style"); pDrive = id ("drive"); pTone = id ("tone"); pLow = id ("lowcut"); pHigh = id ("highcut"); pThump = id ("thump"); pSteep = id ("steep"); pPunish = id ("punish"); pBias = id ("bias"); pSag = id ("sag"); pAuto = id ("auto"); pOut = id ("out");
    }
    void resetEffect() override { dc.reset(); shaper.reset(); for (auto& f : filt) f.reset(); sagEnv.reset(); ag.reset(); dec = {}; firstBlock = true; }
    void process (juce::dsp::AudioBlock<float> block, const ModulationContext& ctx) noexcept override
    {
        beginBlock (ctx);
        const int n = (int) block.getNumSamples(), ch = (int) block.getNumChannels();
        auto* L = block.getChannelPointer (0); auto* R = ch > 1 ? block.getChannelPointer (1) : L;
        const int style = choice (pStyle); shaper.setKind (wreckStyleKind (style));
        using M = sst::filters::CytomicSVF::Mode;
        const bool thump = on (pThump), steep = on (pSteep), punish = on (pPunish), autoGain = on (pAuto);
        if (thump) filt[4].set (M::Bell, 90.0f, 0.5f, dbToGain (4.0f));
        const bool toneOn = std::abs (target (pTone)) > 0.001f; const float res = steep ? 0.5f : 0.0f;
        auto cut = [] (Svf& f, float& l, float& r, float amt) { if (amt <= 0.0f) return; float fl = l, fr = r; f.process (fl, fr); l += (fl - l) * amt; r += (fr - r) * amt; };
        for (int i = 0; i < n; ++i)
        {
            float l = L[i], r = R[i];
            // cuts and tone glide per sample (coefficients recomputed only while a knob moves); the cuts fade out of the path at their ends
            const float lowV = p (pLow).next(), highV = p (pHigh).next();
            if (p (pLow).isSmoothing() || firstBlock) filt[0].set (M::Highpass, lowV, res);
            if (p (pHigh).isSmoothing() || firstBlock) filt[1].set (M::Lowpass, highV, res);
            if (p (pTone).isSmoothing() || firstBlock) { const float tone = p (pTone).next(); filt[2].set (M::LowShelf, 250.0f, 0.3f, dbToGain (-tone * 6.0f)); filt[3].set (M::HighShelf, 3000.0f, 0.3f, dbToGain (tone * 6.0f)); } else p (pTone).next();
            const float lowAmt = juce::jlimit (0.0f, 1.0f, (lowV - 10.0f) * 0.5f), highAmt = juce::jlimit (0.0f, 1.0f, (22000.0f - highV) * 0.002f);
            cut (filt[0], l, r, lowAmt); if (thump) filt[4].process (l, r);
            const float drive = p (pDrive).next(), bias = p (pBias).next() * 0.5f, sag = p (pSag).next();
            const float env = sagEnv.next (juce::jmax (std::abs (l), std::abs (r)));
            float g = 1.0f + drive * drive * (punish ? 60.0f : 20.0f);
            g *= 1.0f - sag * juce::jlimit (0.0f, 0.8f, env * 1.5f);   // supply sag: the harder it is hit, the less headroom
            ag.feedIn (l, r);
            const bool shaping = drive > 0.0005f || std::abs (bias) > 0.0005f;
            if (style == 7) { shaper.pass (l, r); dec.process (l, r, drive, true); } else if (style == 8) { shaper.pass (l, r); dec.process (l, r, drive, false); }
            else if (shaping) { shaper.process (l, r, g, bias); if (autoGain) { const float ga = ag.gain (l, r); l *= ga; r *= ga; } dc.process (l, r); }
            else shaper.pass (l, r);
            if (toneOn) { filt[2].process (l, r); filt[3].process (l, r); }
            cut (filt[1], l, r, highAmt);
            const float og = p (pOut).nextGain();
            L[i] = l * og; if (ch > 1) R[i] = r * og;
        }
        firstBlock = false;
    }
private:
    bool firstBlock = true;
    Shaper shaper; Decimator dec; DcBlock dc; std::array<Svf, 5> filt; EnvelopeFollower sagEnv; AutoGain ag;
    int pStyle = 0, pDrive = 0, pTone = 0, pLow = 0, pHigh = 0, pThump = 0, pSteep = 0, pPunish = 0, pBias = 0, pSag = 0, pAuto = 0, pOut = 0;
};
} // namespace phantom
