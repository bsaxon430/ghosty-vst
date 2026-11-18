#pragma once
// Mr. Mickens 2: BREAK DNA. Reads a break and finds, for every 16th (slice): is there a hit, what it is
// (kick / snare / ghost snare / hat / cymbal, from spectral band features), how loud it is (the accent map),
// and how far off the grid it lands (micro-timing; the odd 16ths give the swing). It also sums a break up into
// "feel" descriptors for BREAK SEARCH / BREAK MATCH. Pure analysis, message thread only, deterministic.
#include "BreakLib.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace mk {

enum DnaBits : uint8_t { kK = 1, kS = 2, kG = 4, kH = 8, kC = 16 };
constexpr int kMaxSteps = 128;
// classifier thresholds for breaks from files (tuned blind on the 100 built-in breaks against the events they were rendered with:
// hat 72 % / cymbal 33 % event agreement, 81 % / 88 % per step; kicks 99.7 %, snares 97 %). Built-ins use their exact events.
inline float kKickT = 0.1f, kKickMidT = 0.3f, kKickNoiseMax = 0.3f, kSnareT = 0.3f, kSnareMidT = 0.12f, kHatT = 0.005f, kHatAdapt = 0.0f, kCymTail = 0.45f, kCymTailLong = 9.0f, kGhostT = 0.06f, kGhostRel = 0.45f;

struct BreakDNA {
    int steps = 0;
    std::array<uint8_t, kMaxSteps> cls {};        // DnaBits per step
    std::array<float, kMaxSteps> accent {};       // 0..1 hit level per step (0 = nothing starts here)
    std::array<float, kMaxSteps> offset {};       // onset position relative to the grid, in steps (-0.5..0.5)
    float swing = 0;                              // mean late-ness of the odd 16ths minus the even ones, in steps
    // feel descriptors (0..1 unless noted)
    float bpm = 0, ghostDensity = 0, snareAccent = 0, kickSparse = 0, hfBusy = 0, bright = 0, synco = 0, density = 0;
    float bandShape[4] {};                         // low / mid / noise / high energy share (spectrum fingerprint)
    // the slices of each class, for substitution
    std::array<int16_t, kMaxSteps> byCls[6] {};   // 0 kick 1 snare 2 ghost 3 hat 4 cymbal 5 empty
    int nCls[6] {};
    bool has(int step, uint8_t bits) const { return steps > 0 && (cls[(size_t) (((step % steps) + steps) % steps)] & bits) != 0; }
    static int group(uint8_t c) { return c & kK ? 0 : c & kS ? 1 : c & kG ? 2 : c & kH ? 3 : c & kC ? 4 : 5; }
    void index() {
        for (auto& n : nCls) n = 0;
        for (int i = 0; i < steps; ++i) { const int g = group(cls[(size_t) i]); byCls[g][(size_t) nCls[g]++] = (int16_t) i; }
    }
    juce::String toString() const {   // compact, for FREEZE DNA in the session
        juce::String s; s << steps << ";" << juce::String(swing, 4) << ";";
        for (int i = 0; i < steps; ++i) s << (int) cls[(size_t) i] << "," << juce::String(accent[(size_t) i], 3) << "," << juce::String(offset[(size_t) i], 3) << " ";
        return s;
    }
    static bool fromString(const juce::String& str, BreakDNA& d) {
        auto parts = juce::StringArray::fromTokens(str, ";", ""); if (parts.size() < 3) return false;
        d = {}; d.steps = juce::jlimit(0, kMaxSteps, parts[0].getIntValue()); d.swing = parts[1].getFloatValue();
        auto st = juce::StringArray::fromTokens(parts[2], " ", ""); st.removeEmptyStrings();
        if (st.size() < d.steps || d.steps == 0) return false;
        d.swing = juce::jlimit(-0.5f, 0.5f, d.swing);
        for (int i = 0; i < d.steps; ++i) { auto f = juce::StringArray::fromTokens(st[i], ",", ""); if (f.size() < 3) return false;   // session data: clamped, never trusted
            d.cls[(size_t) i] = (uint8_t) (f[0].getIntValue() & 31); d.accent[(size_t) i] = juce::jlimit(0.0f, 1.0f, f[1].getFloatValue()); d.offset[(size_t) i] = juce::jlimit(-0.5f, 0.5f, f[2].getFloatValue()); }
        d.index(); return true;
    }
};

// per-step band features (exposed for the tests / tuning)
struct StepFeat { float low = 0, mid = 0, noise = 0, high = 0, full = 0, hiTail = 0, hiTailLong = 0, lowLvl = 0; int onset = 0; bool hit = false; };

namespace dna_detail {
struct SVF { float s1 = 0, s2 = 0, a1 = 0, a2 = 0, a3 = 0, k = 1;
    void setup(float f, float q, float sr) { const float g = std::tan(juce::MathConstants<float>::pi * juce::jlimit(10.0f, sr * 0.45f, f) / sr); k = 1.0f / q; a1 = 1 / (1 + g * (g + k)); a2 = g * a1; a3 = g * a2; }
    float lp(float x) { tick(x); return v2; } float bp(float x) { tick(x); return v1; } float hp(float x) { tick(x); return x - k * v1 - v2; }
    float v1 = 0, v2 = 0;
    void tick(float x) { const float v3 = x - s2; v1 = a1 * s1 + a2 * v3; v2 = s2 + a2 * s1 + a3 * v3; s1 = 2 * v1 - s1; s2 = 2 * v2 - s2; } };
}

inline std::vector<StepFeat> dnaFeatures(const Break& br) {
    using namespace dna_detail;
    const int n = (int) br.data.size(), steps = juce::jlimit(1, kMaxSteps, br.slices); const float sr = (float) br.rate;
    std::vector<StepFeat> F((size_t) steps); if (n < 1024) return F;
    // band energies on a 64-sample frame grid
    constexpr int hop = 64; const int nf = n / hop + 1;
    std::vector<float> E[5]; for (auto& e : E) e.assign((size_t) nf, 0.0f);
    SVF l1, l2, m1, z1, h1, h2; l1.setup(100, 0.7f, sr); l2.setup(100, 0.7f, sr); m1.setup(230, 1.4f, sr); z1.setup(2600, 0.9f, sr); h1.setup(7000, 0.7f, sr); h2.setup(7000, 0.7f, sr);
    // run the loop twice so the filters are warm when the second pass (the one we keep) starts
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < n; ++i) {
            const float x = br.data[(size_t) i];
            const float lo = l2.lp(l1.lp(x)), mi = m1.bp(x), no = z1.bp(x), hi = h2.hp(h1.hp(x));
            if (pass) { const size_t f = (size_t) (i / hop); E[0][f] += lo * lo; E[1][f] += mi * mi; E[2][f] += no * no; E[3][f] += hi * hi; E[4][f] += x * x; }
        }
    const double stepLen = n / (double) steps;
    auto sum = [&](int band, int f0, int f1) { float s = 0; for (int f = f0; f < f1; ++f) s += E[band][(size_t) (((f % nf) + nf) % nf)]; return s; };
    const int post = juce::jmax(2, (int) std::round(22 * sr / 1000.0f / hop)), pre = post, gap = juce::jmax(1, (int) std::round(3 * sr / 1000.0f / hop));
    for (int s = 0; s < steps; ++s) {
        const int g = (int) std::round(s * stepLen / hop);
        // onset: the strongest broadband rise within -8 .. +14 ms of the grid line
        const int lo = g - (int) std::round(8 * sr / 1000.0f / hop), hi = g + (int) std::round(14 * sr / 1000.0f / hop);
        int best = g; float bestRise = -1;
        for (int f = lo; f <= hi; ++f) { const float r = sum(4, f, f + 3) - sum(4, f - 4, f - 1); if (r > bestRise) { bestRise = r; best = f; } }
        auto& o = F[(size_t) s]; o.onset = best * hop;
        auto rise = [&](int band) { const float a = sum(band, best, best + post), b = sum(band, best - pre - gap, best - gap); return std::max(0.0f, a - b * 1.05f); };
        o.low = rise(0); o.mid = rise(1); o.noise = rise(2); o.high = rise(3); o.full = rise(4);
        o.lowLvl = sum(0, best, best + post);
        // top-end tail: how much of the hit's high band is still there 60 ms and 200 ms later (hat: gone; cymbal / ride: ringing)
        const float h0 = sum(3, best, best + post); const int t60 = (int) std::round(60 * sr / 1000.0f / hop), t200 = (int) std::round(200 * sr / 1000.0f / hop);
        const float hl = sum(3, best + t60, best + t60 + post), hl2 = sum(3, best + t200, best + t200 + post);
        o.hiTail = h0 > 1e-9f ? hl / h0 : 0; o.hiTailLong = h0 > 1e-9f ? hl2 / h0 : 0;
    }
    return F;
}

inline float pct(std::vector<float> v, float q) { if (v.empty()) return 0; std::sort(v.begin(), v.end()); return v[(size_t) juce::jlimit(0, (int) v.size() - 1, (int) std::round(q * (v.size() - 1)))]; }

inline BreakDNA analyzeBreak(const Break& br) {
    BreakDNA d; d.steps = juce::jlimit(1, kMaxSteps, br.slices); d.bpm = (float) br.bpm;
    auto F = dnaFeatures(br); if ((int) F.size() < d.steps) { d.index(); return d; }
    const int S = d.steps;
    std::vector<float> lo, mi, no, hi, fu; for (auto& f : F) { lo.push_back(f.low); mi.push_back(f.mid); no.push_back(f.noise); hi.push_back(f.high); fu.push_back(f.full); }
    const float nLo = std::max(1e-9f, pct(lo, 1.0f)), nMi = std::max(1e-9f, pct(mi, 1.0f)), nNo = std::max(1e-9f, pct(no, 1.0f)), nHi = std::max(1e-9f, pct(hi, 1.0f)), nFu = std::max(1e-9f, pct(fu, 1.0f));
    std::vector<float> snareLvl;
    const double stepLen = br.data.size() / (double) S;
    // the built-in breaks know exactly what was played on every 16th (Break::truth): use that, measure only accents + timing.
    // the classifier is for breaks from files.
    const bool truth = (int) br.truth.size() == S;
    // the hat threshold adapts to the break: a fifth of the median top-band energy of its hits
    float hatT = kHatT; { std::vector<float> hs; for (auto& f : F) if (f.high / nHi > kHatT) hs.push_back(f.high / nHi); if (hs.size() >= 4) hatT = juce::jmax(kHatT, kHatAdapt * pct(hs, 0.5f)); }
    for (int s = 0; s < S; ++s) {
        auto& f = F[(size_t) s]; const float L = f.low / nLo, M = f.mid / nMi, N = f.noise / nNo, H = f.high / nHi, U = f.full / nFu;
        uint8_t c = 0;
        if (truth) c = (uint8_t) (br.truth[(size_t) s] & 31);
        else {
            if (L > kKickT || (M > kKickMidT && N < kKickNoiseMax)) c |= kK;           // low thump, or a re-struck kick inside the last one's tail (its pitch sweep)
            if (N > kSnareT && M > kSnareMidT) c |= kS;                               // rattle band + body
            if (!(c & (kK | kS)) && N > kGhostT && M > 0.02f) c |= kG;                            // a quiet snare: ghost
            const bool ring = f.hiTail >= kCymTail || f.hiTailLong >= kCymTailLong;
            if (H > hatT && !ring) c |= kH;                                           // short top end: hat
            if (H > hatT && ring) c |= kC;                                            // top end that keeps ringing: cymbal / ride
        }
        d.cls[(size_t) s] = c; d.accent[(size_t) s] = c ? juce::jlimit(0.0f, 1.0f, juce::jmax(0.05f, std::sqrt(U))) : 0.0f;
        d.offset[(size_t) s] = c ? (float) juce::jlimit(-0.5, 0.5, (f.onset - s * stepLen) / stepLen) : 0.0f;
        if (c & kS) snareLvl.push_back(N);
    }
    // a "snare" much quieter than the break's snares is a ghost note
    const float sMed = pct(snareLvl, 0.5f);
    if (!truth) for (int s = 0; s < S; ++s) if ((d.cls[(size_t) s] & kS) && F[(size_t) s].noise / nNo < kGhostRel * sMed) { d.cls[(size_t) s] = (uint8_t) ((d.cls[(size_t) s] & ~kS) | kG); }
    // swing: the odd 16ths' average offset minus the even ones'
    float od = 0, ev = 0; int no_ = 0, ne = 0;
    for (int s = 0; s < S; ++s) if (d.cls[(size_t) s]) { if (s & 1) { od += d.offset[(size_t) s]; ++no_; } else { ev += d.offset[(size_t) s]; ++ne; } }
    d.swing = (no_ ? od / no_ : 0) - (ne ? ev / ne : 0);
    d.index();
    // feel descriptors
    int nk = 0, ns = 0, ng = 0, nh = 0, nhit = 0, syn = 0, synW = 0; float sAcc = 0, oAcc = 0; int sN = 0, oN = 0;
    for (int s = 0; s < S; ++s) { const auto c = d.cls[(size_t) s]; if (!c) continue; ++nhit;
        nk += (c & kK) != 0; ns += (c & kS) != 0; ng += (c & kG) != 0; nh += (c & (kH | kC)) != 0;
        if (c & kS) { sAcc += d.accent[(size_t) s]; ++sN; } else { oAcc += d.accent[(size_t) s]; ++oN; }
        if (c & (kK | kS)) { ++synW; if (s % 4 != 0) ++syn; } }
    d.density = nhit / (float) S; d.ghostDensity = ng / (float) S; d.kickSparse = 1.0f - juce::jmin(1.0f, nk / (S * 0.375f));
    d.hfBusy = nh / (float) S; d.synco = synW ? syn / (float) synW : 0;
    d.snareAccent = sN ? juce::jlimit(0.0f, 1.0f, (sAcc / sN) - (oN ? oAcc / oN : 0) + 0.5f) : 0;
    double eb[4] {}; for (auto& f : F) { eb[0] += f.lowLvl; eb[1] += f.mid; eb[2] += f.noise; eb[3] += f.high; }
    const double tot = std::max(1e-12, eb[0] + eb[1] + eb[2] + eb[3]); for (int i = 0; i < 4; ++i) d.bandShape[i] = (float) (eb[i] / tot);
    d.bright = juce::jlimit(0.0f, 1.0f, (float) ((eb[2] + eb[3] * 2) / tot));
    return d;
}

} // namespace mk
