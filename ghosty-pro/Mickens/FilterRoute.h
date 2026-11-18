#pragma once
// Mr. Mickens 2: FILTER ROUTING for the breaks bus.
//   PARALLEL  every filter next to the dry break, each with its own MIX
//   SERIAL    LP -> HP -> BP -> NOTCH -> COMB -> FORMANT, each MIX = how much of that stage you hear
//   MORPH     crossfades (equal power) through the filters that are switched on (MIX > 0), MORPH picks where
//   AUTO GAIN keeps the filtered (wet) part as loud as the input: a peak-ish follower (5 ms attack, 250 ms
//             release), +/-6 dB, only on the wet sum in PARALLEL (the DRY knob is never touched), bypassed when
//             no filter is on
//   COMB      FREE pitch, KEY (tuned to a note) or TEMPO (a power-of-two multiple of the beat), +/- feedback;
//             the delay time glides (20 ms) so pitch moves don't zipper
//   HIT SWEEP each kick / snare / any hit fires a short resonant sweep with its own envelope, starting on the hit's sample
//   PHRASE    a tempo-synced sweep over 1..8 bars (rise / fall / arch) that snaps back at the phrase start
// Nothing switches instantly: the parallel/morph bank and the serial bank both run all the time, and a change of
// ROUTE, of the MORPH active set (a MIX crossing on/off) or of AUTO GAIN crossfades the output over 10 ms.
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <vector>

namespace mk {

enum FilterRouteMode { kParallel = 0, kSerial = 1, kMorphRoute = 2 };
struct FilterHit { int at; uint8_t cls; };

struct FilterParams {
    int route = kParallel; float morph = 0; bool autoGain = true;
    float dry = 1, cut[4] { 0.45f, 0.55f, 0.5f, 0.5f }, mix[6] {}, res = 0.5f;   // cut: LP HP BP NOTCH; mix: LP HP BP NOTCH COMB FORMANT
    float combCut = 0.4f, combFb = 0.75f; int combTune = 0, combNote = 48; float formant = 0.3f;
    int trigSrc = 0; float trigAmt = 2, trigDec = 0.12f, trigRes = 0.3f;    // trigSrc: 0 off 1 kick 2 snare 3 any hit
    int phrBars = 0; float phrAmt = 3; int phrShape = 0;                      // phrBars 0 off else 1/2/4/8
};

class FilterRouter {
public:
    void prepare(double rate) {
        sr = rate; for (auto& b : bank) { for (auto& c : b.comb) c.assign((size_t) (rate / 25.0) + 8, 0.0f); b.combPos = 0; for (auto& f : b.flt) f = TPT(); for (auto& f : b.form) f = TPT(); }
        envIn = envWet = 1e-6f; gain = 1; gainSm = 1; trigEnv = 0; hitLog.n = 0; clock = 0;
        curRoute = -1; prevRoute = -1; routeFade = 0; agW = 0; agPos = 0; agPosTarget = 0; for (auto& a : active) a = 0; for (auto& a : prevActive) a = 0; nActive = nPrevActive = 0; setFade = 0; cdSm = -1;
        fadeLen = juce::jmax(16, (int) (0.010 * rate));
    }
    // audio-thread reset (BYPASS engaged): states and tails to zero, no allocation
    void reset() { for (auto& b : bank) { for (auto& c : b.comb) std::fill(c.begin(), c.end(), 0.0f); b.combPos = 0; for (auto& f : b.flt) f = TPT(); for (auto& f : b.form) f = TPT(); } envIn = envWet = 1e-6f; gain = gainSm = 1; trigEnv = 0; routeFade = setFade = 0; }
    struct Log { long long at[512]; int n = 0; } hitLog;   // samples (since prepare) where a hit sweep started; tests read it
    long long clock = 0;

    static float cutHz(float v) { return 30.0f * std::pow(600.0f, juce::jlimit(0.0f, 1.0f, v)); }
    static double noteHz(int note) { return 440.0 * std::exp2((note - 69) / 12.0); }
    double combHz(const FilterParams& P, double bpm) const {
        double f = 40.0 * std::pow(40.0, (double) P.combCut);
        if (P.combTune == 1) f = noteHz(P.combNote);
        else if (P.combTune == 2) { const double beat = juce::jmax(40.0, bpm) / 60.0; f = beat * std::exp2(std::round(std::log2(f / beat))); while (f < 26.0) f *= 2; }
        return juce::jlimit(26.0, sr * 0.45, f);
    }
    bool anyActive(const FilterParams& P) const { for (int j = 0; j < 6; ++j) if (P.mix[j] > 0.01f) return true; return false; }

    void process(float* L, float* R, int n, const FilterParams& P, const FilterHit* hits, int nHits, double b0, double spb, double bpm, bool running) {
        const float trigK = std::exp(-1.0f / (juce::jmax(0.005f, P.trigDec) * (float) sr));
        const float atkK = 1.0f - std::exp(-1.0f / (0.005f * (float) sr)), relK = 1.0f - std::exp(-1.0f / (0.25f * (float) sr)), gK = 1.0f - std::exp(-1.0f / (0.03f * (float) sr)), gDownK = 1.0f - std::exp(-1.0f / (0.003f * (float) sr)), cdK = 1.0f - std::exp(-1.0f / (0.02f * (float) sr));
        auto smooth = [](float t) { t = juce::jlimit(0.0f, 1.0f, t); return t * t * (3 - 2 * t); };   // crossfade law: zero slope at both ends (no kink)
        const int N = (int) bank[0].comb[0].size(); const float cdTarget = (float) juce::jlimit(2.0, (double) N - 3, sr / combHz(P, bpm)); if (cdSm < 0) cdSm = cdTarget;
        // ---- what changed since the last block: route, morph active set, auto gain (each starts a 10 ms crossfade)
        const int route = juce::jlimit(0, 2, P.route);
        if (curRoute < 0) curRoute = route;
        else if (route != curRoute) { prevRoute = curRoute; curRoute = route; routeFade = fadeLen; }
        int act[6]; int na = 0; for (int j = 0; j < 6; ++j) if (P.mix[j] > 0.01f) act[na++] = j;
        if (route == kMorphRoute && na == 0) { for (int j = 0; j < 6; ++j) act[j] = j; na = 6; }
        bool same = na == nActive; for (int a = 0; same && a < na; ++a) same = act[a] == active[a];
        if (!same) { if (nActive > 0 && route == kMorphRoute) { for (int a = 0; a < 6; ++a) prevActive[a] = active[a]; nPrevActive = nActive; setFade = fadeLen; } for (int a = 0; a < na; ++a) active[a] = act[a]; nActive = na; }
        agPosTarget = P.autoGain ? 1.0f : 0.0f; const float agStep = 1.0f / (float) fadeLen;
        const bool wetOnly = route == kParallel; const bool agBypass = na == 0 || (route == kMorphRoute && !anyActive(P));
        int hi = 0;
        for (int i = 0; i < n; ++i, ++clock) {
            bool force = (i & 15) == 0;
            while (hi < nHits && hits[hi].at <= i) {
                const auto c = hits[hi].cls; const bool fire = P.trigSrc == 3 || (P.trigSrc == 1 && (c & 1)) || (P.trigSrc == 2 && (c & 2));
                if (fire && hits[hi].at == i) { trigEnv = 1; force = true; if (hitLog.n < 512) hitLog.at[hitLog.n++] = clock; }
                ++hi;
            }
            if (force) {
                float oct = P.trigSrc ? trigEnv * P.trigAmt : 0.0f;
                if (P.phrBars > 0 && running && std::isfinite(b0)) { const double bars = P.phrBars, ph = std::fmod(b0 + i / spb, bars * 4) / (bars * 4);
                    const float sh = P.phrShape == 0 ? (float) ph : P.phrShape == 1 ? (float) (1 - ph) : (float) (1 - std::abs(2 * ph - 1)); oct += P.phrAmt * sh; }
                const float m = std::exp2(juce::jlimit(-10.0f, 10.0f, oct)), q = juce::jlimit(0.0f, 0.98f, P.res + (P.trigSrc ? P.trigRes * trigEnv : 0.0f));
                for (auto& bk : bank) for (int j = 0; j < 4; ++j) bk.flt[j].setup(juce::jlimit(20.0f, (float) sr * 0.45f, cutHz(P.cut[j]) * m), j == 3 ? q * 0.8f : q, (float) sr);
                static const float FORM[5][3] = { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 300, 2300, 3000 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
                const float fp = juce::jlimit(0.0f, 1.0f, P.formant + oct * 0.12f) * 3.999f; const int fa = juce::jlimit(0, 3, (int) fp); const float ft = juce::jlimit(0.0f, 1.0f, fp - fa);
                for (auto& bk : bank) for (int j = 0; j < 3; ++j) bk.form[j].setup(FORM[fa][j] + (FORM[fa + 1][j] - FORM[fa][j]) * ft, 0.85f, (float) sr);
            }
            trigEnv *= trigK; cdSm += cdK * (cdTarget - cdSm);
            agPos += juce::jlimit(-agStep, agStep, agPosTarget - agPos); agW = smooth(agPos);
            float out[2], meas[2], xin[2];
            for (int c = 0; c < 2; ++c) {
                const float x = c ? R[i] : L[i]; xin[c] = x;
                // bank 0: every filter fed with the dry break (PARALLEL / MORPH); bank 1: the serial chain
                float y[6]; for (int j = 0; j < 6; ++j) y[j] = run(bank[0], j, c, x, P);
                float ser = x; for (int j = 0; j < 6; ++j) { const float v = run(bank[1], j, c, ser, P); if (P.mix[j] > 0.01f) ser += P.mix[j] * (v - ser); }
                float wet = 0; for (int a = 0; a < na; ++a) wet += y[act[a]] * P.mix[act[a]];
                auto morphOut = [&](const int* set, int cnt) -> float {
                    if (cnt <= 0) return x; if (cnt == 1) return x + P.mix[set[0]] * (y[set[0]] - x);
                    const float p = juce::jlimit(0.0f, 1.0f, P.morph) * (cnt - 1); const int a = juce::jmin(cnt - 2, (int) p); const float t = p - a;
                    return std::cos(t * juce::MathConstants<float>::halfPi) * y[set[a]] + std::sin(t * juce::MathConstants<float>::halfPi) * y[set[a + 1]]; };
                float mo = morphOut(active, nActive); if (setFade > 0) { const float t = smooth(setFade / (float) fadeLen); mo = mo * (1 - t) + morphOut(prevActive, nPrevActive) * t; }
                auto routeOut = [&](int r, float wetGain) -> float { return r == kSerial ? ser * wetGain : r == kMorphRoute ? mo * wetGain : x * P.dry + wet * wetGain; };
                const float wg = agBypass ? 1.0f : 1.0f + agW * (gainSm - 1.0f);
                float o = routeOut(curRoute, wg);
                if (routeFade > 0) { const float t = smooth(routeFade / (float) fadeLen); o = o * (1 - t) + routeOut(prevRoute, wg) * t; }
                out[c] = o; meas[c] = wetOnly ? wet : (curRoute == kSerial ? ser : mo);
            }
            if (routeFade > 0) --routeFade;
            if (setFade > 0) --setFade;
            if (P.mix[4] > 0.01f || route == kMorphRoute) for (auto& bk : bank) bk.combPos = (bk.combPos + 1) % N;
            // AUTO GAIN follower: the wet sum (PARALLEL) or the whole output (SERIAL / MORPH) against the input, peak-ish
            { const float xi = 0.5f * (xin[0] * xin[0] + xin[1] * xin[1]), wi = 0.5f * (meas[0] * meas[0] + meas[1] * meas[1]);
              envIn += (xi > envIn ? atkK : relK) * (xi - envIn); envWet += (wi > envWet ? atkK : relK) * (wi - envWet);
              const float tg = agBypass ? 1.0f : (float) juce::jlimit(0.5, 2.0, std::sqrt((envIn + 1e-7) / (envWet + 1e-7)));
              gain = tg; gainSm += (gain < gainSm ? gDownK : gK) * (gain - gainSm); }   // down fast (no bloom on a transient), up slowly
            L[i] = out[0]; R[i] = out[1];
        }
    }

private:
    struct TPT { float s1[2] {}, s2[2] {}, a1 = 0, a2 = 0, a3 = 0, k = 1;
        void setup(float f, float q, float rate) { const float g = std::tan(juce::MathConstants<float>::pi * juce::jlimit(20.0f, rate * 0.45f, f) / rate); k = 2.0f - 1.9f * q; a1 = 1 / (1 + g * (g + k)); a2 = g * a1; a3 = g * a2; }
        float tick(int c, float x, int type) { const float v3 = x - s2[c], v1 = a1 * s1[c] + a2 * v3, v2 = s2[c] + a2 * s1[c] + a3 * v3; s1[c] = 2 * v1 - s1[c]; s2[c] = 2 * v2 - s2[c];
            switch (type) { case 0: return v2; case 1: return v1 * k; case 2: return x - k * v1 - v2; default: return x - k * v1; } } };
    struct Bank { TPT flt[4], form[3]; std::vector<float> comb[2]; int combPos = 0; };
    float run(Bank& B, int j, int c, float in, const FilterParams& P) {
        switch (j) {
            case 0: return B.flt[0].tick(c, in, 0); case 1: return B.flt[1].tick(c, in, 2); case 2: return B.flt[2].tick(c, in, 1); case 3: return B.flt[3].tick(c, in, 3);
            case 4: { auto& D = B.comb[c]; const int N = (int) D.size(); float rp = B.combPos - cdSm; if (rp < 0) rp += N; const int r0 = juce::jlimit(0, N - 1, (int) rp); const float t = rp - r0;
                      const float dd = D[(size_t) r0] + (D[(size_t) ((r0 + 1) % N)] - D[(size_t) r0]) * t; float v = in + dd * P.combFb; v = std::abs(v) < 1.5f ? v : 1.5f * std::tanh(v / 1.5f);
                      D[(size_t) B.combPos] = v; return v * (1.0f - std::abs(P.combFb) * 0.5f); }
            default: { float s = 0; for (int k = 0; k < 3; ++k) s += B.form[k].tick(c, in, 1) * (k == 0 ? 1.0f : (k == 1 ? 0.5f : 0.2f)); return s * 1.5f; }
        } }
    double sr = 48000; Bank bank[2];
    float envIn = 1e-6f, envWet = 1e-6f, gain = 1, gainSm = 1, trigEnv = 0, agW = 0, agPos = 0, agPosTarget = 0, cdSm = -1;
    int curRoute = -1, prevRoute = -1, routeFade = 0, fadeLen = 480, active[6] {}, nActive = 0, prevActive[6] {}, nPrevActive = 0, setFade = 0;
};

} // namespace mk
