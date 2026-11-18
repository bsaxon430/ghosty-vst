// Granite — stereo look-ahead brickwall limiter engine.
// Clean-room implementation from public, generic techniques: per-sample required gain, a sliding
// minimum over the look-ahead window followed by a moving average (so gain changes are smooth yet
// never late), a one-pole release stage, separate channel linking for the transient and release
// stages, windowed-sinc halfband FIR oversampling (linear phase, 2x to 32x), a true-peak stage that
// limits the 4x-interpolated output, TPDF dither with error-feedback noise shaping (coefficients after
// Wannamaker's 1992 weighted designs), an external sidechain, and ITU-R BS.1770 loudness metering.
// No third-party product code, assets or disassembly were used. License: MIT.

#pragma once
#include "common/Halfband.hpp"
#include "common/Loudness.hpp"
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <vector>

namespace granite {

static constexpr float kPi = 3.14159265358979f;
inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float dbToGain(float db) { return std::pow(10.f, db / 20.f); }
inline float gainToDb(float g) { return 20.f * std::log10(std::max(g, 1e-9f)); }

using epsilon::HbUp;
using epsilon::OversamplerN;

// ------------------------------------------------------------------ params (append-only)
enum Param : uint32_t {
    kGain,          // dB, 0 .. 30: drive into the limiter
    kCeiling,       // dB, -30 .. 0: output level
    kStyle,         // Clear, Punchy, Dynamic, Balanced, Aggressive, Modern, Bus, Safe
    kLookahead,     // ms, 0 .. 10
    kAttack,        // ms, 0.1 .. 10 (limited to the look-ahead)
    kRelease,       // ms, 1 .. 2000
    kLinkTransient, // 0 .. 1
    kLinkRelease,   // 0 .. 1
    kOversampling,  // off, 2x, 4x, 8x, 16x, 32x
    kTruePeak,      // true-peak (inter-sample) metering
    kDither,        // off, 16, 20, 24 bit
    kShaping,       // flat, basic, optimized, weighted
    kMeterScale,    // dBFS, K-20, K-14, K-12 (display only)
    kDisplayRange,  // 12, 24, 48 dB (display only)
    kMidiOn,
    // outputs (read-only, for the UI)
    kOutLearn, kOutInL, kOutInR, kOutOutL, kOutOutR, kOutGr,
    // ---- 2.0
    kTruePeakLimit, // limit the 4x-interpolated (true) peak of the output, not only the samples
    kSidechain,     // internal, external
    kDcFilter,      // remove DC before limiting
    kUnityGain,     // monitoring: hear the result turned down by the gain (view)
    kAudition,      // monitoring: hear what the limiter removes (view)
    kLoudTarget,    // off, EBU R128 -23, ATSC A/85 -24, streaming -14, podcast -16 (view)
    kDisplaySpeed,  // normal, fast, slow, off (view)
    kCompact,       // compact layout (view)
    kOutLoudM, kOutLoudS, kOutLoudI, kOutLoudRange, kOutTruePeakMax,
    kParamCount
};
enum Style { kClear, kPunchy, kDynamic, kBalanced, kAggressive, kModern, kBus, kSafe, kNumStyles };
static constexpr int kNumOversampling = 6;
static constexpr int kNumLoudTargets = 5;
inline float loudTargetLufs(int k) { static const float t[kNumLoudTargets] = { 0, -23, -24, -14, -16 }; return t[std::max(0, std::min(kNumLoudTargets - 1, k))]; }

// ------------------------------------------------------------------ sliding minimum (monotonic deque)
struct SlidingMin {
    std::vector<float> val; std::vector<int64_t> idx; int head = 0, tail = 0, cap = 1;
    void init(int capacity) { cap = capacity + 2; val.assign(cap, 1.f); idx.assign(cap, 0); head = tail = 0; }
    void reset() { head = tail = 0; }
    // push value v at time t, drop entries older than t - window + 1, return the minimum
    inline float push(float v, int64_t t, int window) {
        while (tail != head) { const int last = tail == 0 ? cap - 1 : tail - 1; if (val[last] >= v) tail = last; else break; }
        val[tail] = v; idx[tail] = t; if (++tail == cap) tail = 0;
        while (idx[head] <= t - window) if (++head == cap) head = 0;
        return val[head];
    }
};

// ------------------------------------------------------------------ true-peak stage
// Interpolates the output 4x (24 samples of delay), works out the gain each sample needs so that no
// interpolated point crosses the ceiling, and applies it with the same sliding-minimum + moving-average
// scheme as the main limiter over a short window, then a release. Latency: 24 + window samples.
struct TruePeakStage {
    static constexpr int kInterpDelay = 24;               // epsilon::OversamplerN::upLatency(4)
    HbUp a, b; float q[2] = { 0, 0 }; SlidingMin mins; std::vector<float> ring, box; double boxSum = 0; int pos = 0, bpos = 0, win = 8; int64_t t = 0; float rel = 1;
    void init(int window) { win = std::max(2, window); mins.init(win + 4); ring.assign(kInterpDelay + win + 4, 0.f); box.assign(win + 1, 1.f); reset(); }
    void reset() { a.reset(); b.reset(); q[0] = q[1] = 0; mins.reset(); std::fill(ring.begin(), ring.end(), 0.f); std::fill(box.begin(), box.end(), 1.f); boxSum = win + 1; pos = bpos = 0; t = 0; rel = 1; }
    int latency() const { return kInterpDelay + win; }
    // x: output sample; ceil: linear ceiling; on: limit (else only delay); relCoef: release per sample
    // margin: headroom kept below the ceiling for what the interpolation can't see
    inline float process(float x, float ceil, bool on, float relCoef, float margin, float& gainOut) {
        float a0, a1, y[6]; y[0] = q[0]; y[1] = q[1];
        a.process(x, a0, a1); b.process(a0, y[2], y[3]); b.process(a1, y[4], y[5]);
        // the 4x points can miss a peak between them by up to 0.7 dB near Nyquist: refine each local extreme
        // with a parabola through it and its neighbours
        float e = 0;
        for (int k = 1; k < 5; ++k) {
            const float l = y[k - 1], m = y[k], r = y[k + 1], am = std::fabs(m);
            float pk = am;
            if (am >= std::fabs(l) && am >= std::fabs(r)) { const float den = l - 2 * m + r; if (std::fabs(den) > 1e-12f) pk = std::min(am * 1.2f, std::fabs(m - (l - r) * (l - r) / (8 * den))); }
            e = std::max(e, pk);
        }
        q[0] = y[4]; q[1] = y[5];
        const float lim = ceil * margin;
        const float req = on && e > lim ? lim / e : 1.f;
        const float m = mins.push(req, t++, win + 1);
        boxSum += m - box[bpos]; box[bpos] = m; bpos = (bpos + 1) % (win + 1);
        const float env = (float)(boxSum / (win + 1));
        rel = env < rel ? env : rel + (env - rel) * relCoef;
        const int n = (int)ring.size();
        ring[pos] = x; const float d = ring[(pos - (kInterpDelay + win) + 2 * n) % n]; pos = (pos + 1) % n;
        gainOut = rel;
        return d * rel;
    }
};

// ------------------------------------------------------------------ engine
class Engine {
public:
    float p[kParamCount];

    // level history for the display: one point per 20 ms, written by the audio thread
    static constexpr int kHist = 512;
    float hIn[kHist], hOut[kHist], hGr[kHist], hRms[kHist], hLoud[kHist];
    struct AtomicIndex {          // copyable wrapper so the engine itself can be copied (tests)
        std::atomic<uint32_t> v { 0 };
        AtomicIndex() {}
        AtomicIndex(const AtomicIndex& o) : v(o.v.load()) {}
        AtomicIndex& operator=(const AtomicIndex& o) { v.store(o.v.load()); return *this; }
        uint32_t load() const { return v.load(std::memory_order_acquire); }
        void store(uint32_t x) { v.store(x, std::memory_order_release); }
    } histWrite;

    Engine() {
        std::fill(p, p + kParamCount, 0.f);
        p[kGain] = 0; p[kCeiling] = -0.1f; p[kStyle] = kClear; p[kLookahead] = 2; p[kAttack] = 2; p[kRelease] = 80;
        p[kLinkTransient] = 0.75f; p[kLinkRelease] = 1.f; p[kOversampling] = 0; p[kTruePeak] = 1;
        p[kDither] = 0; p[kShaping] = 2; p[kMeterScale] = 0; p[kDisplayRange] = 1; p[kMidiOn] = 1;
        p[kTruePeakLimit] = 0; p[kSidechain] = 0; p[kDcFilter] = 0; p[kUnityGain] = 0; p[kAudition] = 0;
        p[kLoudTarget] = 0; p[kDisplaySpeed] = 0; p[kCompact] = 0;
        for (int i = 0; i < kHist; ++i) { hIn[i] = hOut[i] = hRms[i] = hLoud[i] = -120; hGr[i] = 0; }
    }

    void setSampleRate(float sr) {
        fs = sr;
        const int maxL = (int)std::ceil(0.010 * sr) * OversamplerN::kMax + 8;      // 10 ms at 32x
        for (int c = 0; c < 2; ++c) {
            delay[c].assign(maxL + 8, 0.f); boxRing[c].assign(maxL + 8, 1.f); mins[c].init(maxL + 8);
            tpStage[c].init(std::max(8, (int)std::round(0.0005f * sr)));
            dry[c].assign((size_t)std::ceil(0.010 * sr) + OversamplerN::latency(32) + tpStage[c].latency() + 16, 0.f);
        }
        smooth = 1.f - std::exp(-1.f / (0.02f * sr));
        meterFall = std::exp(-1.f / (0.3f * sr));                       // peak meters fall back over ~300 ms
        rmsCoef = 1.f - std::exp(-1.f / (0.3f * sr));
        dcR = 1.f - 2.f * kPi * 5.f / sr;
        histSamples = std::max(1, (int)(0.02f * sr));
        loud.setSampleRate(sr);
        configure(true);
        fadeGain = 1; fadeState = 0;
    }
    void reset() { configure(true); }
    void resetLoudness() { loudResetPending = true; }

    // samples of delay the host must compensate for (base rate)
    uint32_t latency() const { return (uint32_t)(OversamplerN::latency(os) + lookBase + (tpOn ? tpStage[0].latency() : 0)); }
    int oversampling() const { return os; }
    float meterIn(int c) const { return gainToDb(mIn[c]); }
    float meterOut(int c) const { return gainToDb(mOut[c]); }
    float meterGr() const { return mGr; }
    float loudMomentary() const { return loud.momentary(); }
    float loudShortTerm() const { return loud.shortTerm(); }
    float loudIntegrated() const { return loud.integrated(); }
    float loudRange() const { return loud.range(); }
    float loudTruePeak() const { return loud.truePeakDb(); }

    // scL / scR: the external sidechain (may be null)
    void process(const float* inL, const float* inR, float* outL, float* outR, uint32_t n, const float* scL = nullptr, const float* scR = nullptr) {
        if (loudResetPending) { loudResetPending = false; loud.resetIntegrated(); }
        // structural changes (oversampling, look-ahead length, true-peak stage) switch over a short fade
        const int wantOs = osOf(p[kOversampling]), wantLook = lookOf(p[kLookahead]); const bool wantTp = p[kTruePeakLimit] > 0.5f;
        if ((wantOs != os || wantLook != lookBase || wantTp != tpOn) && fadeState == 0) fadeState = 1;
        const int style = (int)clampf(std::round(p[kStyle]), 0, kNumStyles - 1);
        static const float attackFactors[kNumStyles] = { 1.f, 0.35f, 0.7f, 1.f, 0.2f, 0.6f, 1.f, 1.f };
        const int hi = (int)std::round(clampf(p[kAttack], 0.1f, 10.f) * 0.001f * fs * os * attackFactors[style]);
        attackHi = style == kSafe ? lookHi + 1 : std::max(1, std::min(hi, lookHi + 1));
        if (attackHi != lastAttack) refreshBox();
        static const float relFactors[kNumStyles] = { 1.f, 0.5f, 1.f, 1.f, 0.4f, 1.f, 1.5f, 1.f };
        float relMs = clampf(p[kRelease], 1.f, 2000.f) * relFactors[style];
        if (style == kSafe) relMs = std::max(relMs, 40.f);
        const float lt = clampf(p[kLinkTransient], 0.f, 1.f), lr = clampf(p[kLinkRelease], 0.f, 1.f);
        const float tGain = dbToGain(clampf(p[kGain], -12.f, 30.f)), tCeil = dbToGain(clampf(p[kCeiling], -30.f, 0.f));
        const int bits = ditherBits(), shaping = (int)clampf(std::round(p[kShaping]), 0, 3);
        const bool truePeak = p[kTruePeak] > 0.5f;
        const bool external = p[kSidechain] > 0.5f && scL && scR, dcOn = p[kDcFilter] > 0.5f;
        const bool unity = p[kUnityGain] > 0.5f, audition = p[kAudition] > 0.5f;
        const float fsHi = fs * os, susCoef = 1.f - std::exp(-1.f / (0.3f * fsHi));
        const float rcTol = os >= 8 ? 2e-3f : 0.f;            // at 8x and up, reuse the release coefficient while it barely moves
        const float tpRel = 1.f - std::exp(-1.f / (std::max(relMs, 5.f) * 0.001f * fs));
        // without oversampling the limiter's own sample clamp leaves energy right up to Nyquist, where a 4x
        // interpolation is least exact: keep 0.1 dB more headroom there
        const float tpMargin = os == 1 ? 0.9874f : 0.9985f;

        for (uint32_t i = 0; i < n; ++i) {
            sGain += (tGain - sGain) * smooth; sCeil += (tCeil - sCeil) * smooth;
            float xin[2] = { inL[i], inR[i] };
            if (dcOn) for (int c = 0; c < 2; ++c) { const float y = xin[c] - dcX[c] + dcR * dcY[c]; dcX[c] = xin[c]; dcY[c] = y; xin[c] = y; }
            const float x[2] = { xin[0] * sGain, xin[1] * sGain };
            float up[2][OversamplerN::kMax], y[2][OversamplerN::kMax], kup[2][OversamplerN::kMax];
            for (int c = 0; c < 2; ++c) { mIn[c] = std::max(mIn[c] * meterFall, std::fabs(x[c])); ovs[c].up(x[c], os, up[c]); }
            if (external) { const float k[2] = { scL[i] * sGain, scR[i] * sGain }; for (int c = 0; c < 2; ++c) scOvs[c].up(k[c], os, kup[c]); }
            float grMin = 1.f;
            for (int k = 0; k < os; ++k) {
                // required gain per channel (Balanced and Aggressive soften the peaks first)
                float v[2], req[2];
                for (int c = 0; c < 2; ++c) {
                    v[c] = up[c][k];
                    if (style == kBalanced) v[c] = 0.6f * v[c] + 0.4f * soften(v[c], sCeil);
                    else if (style == kAggressive) v[c] = 0.3f * v[c] + 0.7f * soften(v[c], sCeil, 0.35f);
                    const float a = std::fabs(external ? kup[c][k] : v[c]);
                    req[c] = a > sCeil ? sCeil / a : 1.f;
                }
                const float rmin = std::min(req[0], req[1]);
                float env[2];
                for (int c = 0; c < 2; ++c) {
                    const float r = req[c] + (rmin - req[c]) * lt;                       // transient link
                    const float m = mins[c].push(r, tHi, lookHi + 1);                   // worst gain needed ahead
                    lastMin[c] = m;
                    boxSum[c] += m - boxRing[c][boxPos];                                // moving average over the attack
                    boxRing[c][boxPos] = m;
                    env[c] = (float)(boxSum[c] / attackHi);
                    // release stage: follow down at once, recover at the release rate
                    float rel = relMs;
                    const float grDb = -gainToDb(rel_g[c]);
                    switch (style) {
                    case kClear: rel *= 1.f + 2.f * clampf(sus[c] / 4.f, 0.f, 1.f); break;          // sustained limiting: slower, less pumping
                    case kDynamic: rel /= 1.f + grDb / 2.f; break;                                   // deep, short peaks recover fast
                    case kModern: rel *= (1.f + 1.5f * clampf(sus[c] / 4.f, 0.f, 1.f)) / (1.f + std::max(0.f, grDb - sus[c]) / 2.f); break;
                    case kBus: rel *= 1.f + 2.f * clampf(sus[c] / 3.f, 0.f, 1.f); break;
                    case kSafe: rel *= 1.f + 3.f * clampf(sus[c] / 4.f, 0.f, 1.f); break;
                    default: break;
                    }
                    float rc;
                    if (rcTol > 0 && std::fabs(rel - rcRel[c]) <= rcTol * rel) rc = rcVal[c];
                    else { rc = 1.f - std::exp(-1.f / (std::max(rel, 0.5f) * 0.001f * fsHi)); rcRel[c] = rel; rcVal[c] = rc; }
                    rel_g[c] = env[c] < rel_g[c] ? env[c] : rel_g[c] + (env[c] - rel_g[c]) * rc;
                    sus[c] += (grDb - sus[c]) * susCoef;
                }
                if (++boxPos >= attackHi) boxPos = 0;
                const float gmin = std::min(rel_g[0], rel_g[1]);
                for (int c = 0; c < 2; ++c) {
                    const float g = rel_g[c] + (gmin - rel_g[c]) * lr;                    // release link
                    // delayed signal (look-ahead)
                    std::vector<float>& d = delay[c];
                    d[dPos] = v[c];
                    int rd = dPos - lookHi; if (rd < 0) rd += (int)d.size();
                    y[c][k] = external ? d[rd] * g : clampf(d[rd] * g, -sCeil, sCeil);     // the clamp is a safety net only
                    grMin = std::min(grMin, g);
                }
                if (++dPos == (int)delay[0].size()) dPos = 0;
                ++tHi;
            }
            float o[2], pre[2], tpg = 1.f;
            for (int c = 0; c < 2; ++c) {
                o[c] = ovs[c].down(y[c], os);
                if (!external) o[c] = clampf(o[c], -sCeil, sCeil);                        // filter ripple stays under the ceiling
                if (tpOn) { float g; o[c] = tpStage[c].process(o[c], sCeil, !external, tpRel, tpMargin, g); tpg = std::min(tpg, g); if (!external) o[c] = clampf(o[c], -sCeil, sCeil); }
                if (bits > 0) {                     // dither, then keep the result on the grid and under the ceiling
                    const float q = std::ldexp(1.f, bits - 1), top = std::floor(sCeil * q) / q;
                    o[c] = external ? dither(c, o[c], bits, shaping) : clampf(dither(c, o[c], bits, shaping), -top, top);
                }
                o[c] *= fadeGain;
                pre[c] = o[c];
                const float pk = truePeak ? truePeakOf(c, o[c]) : std::fabs(o[c]);
                mOut[c] = std::max(mOut[c] * meterFall, pk);
                ms[c] += (o[c] * o[c] - ms[c]) * rmsCoef;
            }
            loudL[loudN] = pre[0]; loudR[loudN] = pre[1];
            if (++loudN == kLoudBlock) { loud.process(loudL, loudR, kLoudBlock); loudN = 0; }
            // monitoring: what the limiter removed, and/or the result at unity gain
            if (audition || unity) {
                const int dn = (int)dry[0].size(), lat = (int)latency();
                for (int c = 0; c < 2; ++c) {
                    dry[c][dryPos] = x[c];
                    if (audition) o[c] = dry[c][(dryPos - lat + 2 * dn) % dn] - o[c];
                    if (unity) o[c] /= std::max(sGain, 1e-6f);
                }
                dryPos = (dryPos + 1) % dn;
            } else if (dryUsed) { for (int c = 0; c < 2; ++c) std::fill(dry[c].begin(), dry[c].end(), 0.f); }
            dryUsed = audition || unity;
            outL[i] = o[0]; outR[i] = o[1];
            const float grDb = -gainToDb(grMin * tpg);
            mGr = std::max(mGr * 0.9995f, grDb);
            // display history
            hPkIn = std::max(hPkIn, std::max(std::fabs(x[0]), std::fabs(x[1])));
            hPkOut = std::max(hPkOut, std::max(std::fabs(pre[0]), std::fabs(pre[1])));
            hGrMax = std::max(hGrMax, grDb);
            if (++hCount >= histSamples) {
                const uint32_t w = histWrite.load() % kHist;
                hIn[w] = gainToDb(hPkIn); hOut[w] = gainToDb(hPkOut); hGr[w] = hGrMax; hRms[w] = 10.f * std::log10(std::max(0.5f * (ms[0] + ms[1]), 1e-12f));
                hLoud[w] = loud.momentary();
                histWrite.store(histWrite.load() + 1);
                hPkIn = hPkOut = 0; hGrMax = 0; hCount = 0;
            }
            // fade handling for structural changes
            if (fadeState == 1) { fadeGain -= 1.f / 128.f; if (fadeGain <= 0) { fadeGain = 0; configure(false); fadeState = 2; } }
            else if (fadeState == 2) { fadeGain += 1.f / 128.f; if (fadeGain >= 1) { fadeGain = 1; fadeState = 0; } }
        }
        for (int c = 0; c < 2; ++c) {
            if (!(std::fabs(ms[c]) < 1e6f)) ms[c] = 0;
            if (!(rel_g[c] >= 0 && rel_g[c] <= 1.0001f)) rel_g[c] = 1;
            if (!(std::fabs(dcY[c]) < 1e6f)) dcX[c] = dcY[c] = 0;
        }
    }

    // noise-shaping filters (error-feedback coefficients)
    static const float* shapingCoefs(int s, int& n) {
        static const float basic[1] = { 1.f };
        static const float optimized[3] = { 1.623f, -0.982f, 0.109f };                       // after Wannamaker, 3-tap E-weighted
        static const float weighted[9] = { 2.412f, -3.370f, 3.937f, -4.174f, 3.353f, -2.205f, 1.281f, -0.569f, 0.0847f };  // after Wannamaker, 9-tap F-weighted
        switch (s) { case 1: n = 1; return basic; case 2: n = 3; return optimized; case 3: n = 9; return weighted; default: n = 0; return nullptr; }
    }
    static int osOf(float v) { const int k = (int)clampf(std::round(v), 0, kNumOversampling - 1); return 1 << k; }

private:
    int lookOf(float ms) const { return (int)std::round(clampf(ms, 0.f, 10.f) * 0.001f * fs); }
    int ditherBits() const { const int k = (int)clampf(std::round(p[kDither]), 0, 3); return k == 0 ? 0 : (k == 1 ? 16 : (k == 2 ? 20 : 24)); }
    static float soften(float x, float ceil, float knee = 0.5f) {           // smooth saturation that approaches the ceiling
        const float k = knee * ceil, a = std::fabs(x);
        if (a <= k) return x;
        return std::copysign(k + (ceil - k) * std::tanh((a - k) / (ceil - k)), x);
    }

    void configure(bool hard) {
        os = osOf(p[kOversampling]); lookBase = lookOf(p[kLookahead]); lookHi = lookBase * os; tpOn = p[kTruePeakLimit] > 0.5f;
        for (int c = 0; c < 2; ++c) {
            ovs[c].reset(); scOvs[c].reset(); std::fill(delay[c].begin(), delay[c].end(), 0.f); mins[c].reset();
            rel_g[c] = 1; sus[c] = 0; for (float& e : err[c]) e = 0; tp[c].reset(); tpStage[c].reset(); rcRel[c] = -1;
            std::fill(dry[c].begin(), dry[c].end(), 0.f);
        }
        dPos = 0; tHi = 0; lastAttack = -1; attackHi = 1; lastMin[0] = lastMin[1] = 1.f; dryPos = 0;
        if (hard) { sGain = dbToGain(clampf(p[kGain], -12.f, 30.f)); sCeil = dbToGain(clampf(p[kCeiling], -30.f, 0.f));
            for (int c = 0; c < 2; ++c) { mIn[c] = mOut[c] = 0; ms[c] = 0; dcX[c] = dcY[c] = 0; } mGr = 0; loud.reset(); loudN = 0; }
        refreshBox();
    }
    // new attack length: refill the moving average with the latest sliding minimum. That value covers
    // every sample still in the look-ahead delay, so the gain stays safe across the change.
    void refreshBox() {
        for (int c = 0; c < 2; ++c) { std::fill(boxRing[c].begin(), boxRing[c].end(), lastMin[c]); boxSum[c] = (double)lastMin[c] * attackHi; }
        boxPos = 0; lastAttack = attackHi;
    }
    float dither(int c, float x, int bits, int shaping) {
        const float q = std::ldexp(1.f, bits - 1);
        int n = 0; const float* h = shapingCoefs(shaping, n);
        float fb = 0; for (int i = 0; i < n; ++i) fb += h[i] * err[c][i];
        const float v = x * q - fb;
        rng = rng * 1664525u + 1013904223u; const float u1 = (rng >> 8) * (1.f / 16777216.f);
        rng = rng * 1664525u + 1013904223u; const float u2 = (rng >> 8) * (1.f / 16777216.f);
        const float yq = std::round(v + (u1 - u2));
        for (int i = 8; i > 0; --i) err[c][i] = err[c][i - 1];
        err[c][0] = clampf(yq - v, -4.f, 4.f);
        return yq / q;
    }
    struct TruePeak { HbUp a, b; void reset() { a.reset(); b.reset(); } };
    float truePeakOf(int c, float x) {
        float a0, a1, y[4]; tp[c].a.process(x, a0, a1); tp[c].b.process(a0, y[0], y[1]); tp[c].b.process(a1, y[2], y[3]);
        return std::max(std::max(std::fabs(y[0]), std::fabs(y[1])), std::max(std::fabs(y[2]), std::fabs(y[3])));
    }

    float fs = 48000.f, smooth = 0.01f, meterFall = 0.999f, rmsCoef = 0.0001f, dcR = 0.999f;
    int os = 1, lookBase = 0, lookHi = 0, attackHi = 1, lastAttack = -1, boxPos = 0, dPos = 0, dryPos = 0;
    bool tpOn = false, dryUsed = false, loudResetPending = false;
    int64_t tHi = 0;
    OversamplerN ovs[2], scOvs[2];
    std::vector<float> delay[2], boxRing[2], dry[2];
    SlidingMin mins[2];
    TruePeakStage tpStage[2];
    double boxSum[2] = { 1, 1 };
    float rel_g[2] = { 1, 1 }, sus[2] = {}, sGain = 1, sCeil = 1, lastMin[2] = { 1, 1 }, rcRel[2] = { -1, -1 }, rcVal[2] = { 0, 0 };
    float dcX[2] = {}, dcY[2] = {};
    float err[2][9] = {}; uint32_t rng = 22222;
    TruePeak tp[2];
    epsilon::LoudnessMeter loud;
    static constexpr int kLoudBlock = 32; float loudL[kLoudBlock], loudR[kLoudBlock]; int loudN = 0;
    float mIn[2] = {}, mOut[2] = {}, mGr = 0, ms[2] = {};
    float hPkIn = 0, hPkOut = 0, hGrMax = 0; int hCount = 0, histSamples = 960;
    float fadeGain = 1; int fadeState = 0;
};

} // namespace granite
