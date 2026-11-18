#pragma once
// GHOST HUM - BEATBOX: "boom tss pah tss" -> kick / snare / hi-hat / open hat / clap hits.
//
// 1. HITS      Ghosty Beats' five-band onset analysis (Shared/HearCore.h: SUB / LOW / MID / NOISE / HIGH envelopes, band-wise rises,
//              events within 12 ms joined, the NEW energy per band, decays, clap bursts, low-end frequency...). Beatbox is one mouth:
//              one drum per hit.
//              Build 2: the take is high-passed at 40 Hz (DC, rumble) and its level brought up first (prepared()).
// 2. FEATURES  13 numbers per hit (features()): the share of its energy in each band, how long it rings (to -20 dB), how fast the top
//              decays, the overall zero-crossing frequency (bright vs dark), the mid band's noisiness, whether the low end is a voiced
//              "boom" (YIN periodicity 50..400 Hz), clap-like bursts, the top end's drop over 40 ms, the low-end frequency, the attack
//              time (a drum peaks in a few ms; a breath or a grunt takes tens of ms) and its loudness against the take's loudest hit.
// 3. WHICH DRUM softmax regression on the standardised features (one linear score per drum PLUS one for "not a drum", fitted by HumTests
//              "beattrain" on our own synthesised beatboxers only: tests/HumVoices.h, hundreds of random mouths, half of them through a
//              phone / laptop mic model with breaths, grunts, lip smacks, clicks, pops and a leaked metronome click between the hits;
//              nothing recorded, no datasets; held-out mouths score it). The scores become probabilities; what is likelier "not a drum"
//              than any drum is dropped (so is anything near a known metronome click that is not clearly a drum); the gap between the
//              best drum and the runner-up is the hit's confidence (the lanes draw an unsure hit fainter). With TEACH GHOST YOUR SOUNDS
//              the owner's own examples (3 per drum) join in as nearest-neighbour evidence in the same space and outweigh the built-in
//              score for the drums they cover (an untaught drum stays possible, at a penalty).
// 4. VELOCITY  each hit's energy against the loudest hit (cube root).
// Tempo / swing / double hits: HumTempo.h.
#include "HumModel.h"
#include "HearCore.h"
#include "HumClicks.h"

namespace hum::beat {

constexpr int kF = 15;                 // (build 2: + attack time, + loudness against the take's loudest hit, + how long its level took to climb, above 300 Hz)
constexpr int kJunk = kDrums, kClasses = kDrums + 1;   // class 5 = NOT A DRUM: a breath, a grunt, a mouth click, a lip smack, a plosive pop, the metronome leaking in
using Feat = std::array<float, kF>;
inline const char* featName(int i) { static const char* n[] { "low share", "mid share", "noise share", "high share", "ring", "top decay", "brightness", "mid noise", "voiced boom", "bursts", "top drop", "low pitch", "attack", "loudness", "climb" }; return i >= 0 && i < kF ? n[i] : "?"; }
struct Example { int drum = Kick; Feat f {}; int nf = kF; };   // nf: how many features were recorded (a build 1 lesson has 12; the rest are skipped)
struct Calibration {   // TEACH GHOST YOUR SOUNDS
    std::vector<Example> ex;
    int countFor(int d) const { int n = 0; for (auto& e : ex) if (e.drum == d) ++n; return n; }
    bool any() const { return !ex.empty(); }
    void clearDrum(int d) { ex.erase(std::remove_if(ex.begin(), ex.end(), [d](const Example& e) { return e.drum == d; }), ex.end()); }
    juce::var toVar() const { juce::Array<juce::var> a; for (auto& e : ex) { juce::Array<juce::var> q; q.add(e.drum); for (int i = 0; i < e.nf; ++i) q.add((double) e.f[(size_t) i]); a.add(q); } return a; }
    static Calibration fromVar(const juce::var& v) { Calibration c; if (auto* a = v.getArray()) for (auto& e : *a) if (auto* q = e.getArray(); q && q->size() >= 13 && q->size() <= kF + 1) { Example x; x.drum = juce::jlimit(0, kDrums - 1, (int) (*q)[0]); x.nf = q->size() - 1; for (int i = 0; i < x.nf; ++i) x.f[(size_t) i] = (float) (double) (*q)[i + 1]; c.ex.push_back(x); } return c; }
};
struct Model {   // the built-in classifier: features standardised (mu, sg), then one linear score per drum (softmax regression; bias last)
    Feat mu {}, sg {}, wsd {}; std::array<std::array<float, kF + 1>, kClasses> W {};   // wsd: how much each (standardised) feature wanders within one drum (TEACH weighs steady features more)
    Feat z(const Feat& f) const { Feat o; for (int i = 0; i < kF; ++i) o[(size_t) i] = (f[(size_t) i] - mu[(size_t) i]) / juce::jmax(0.05f, sg[(size_t) i]); return o; }
};
inline const Model& defaultModel();

struct Event { double t = 0; float vel = 1, energy = 0; Feat f {}; int drum = -1; float conf = 0, junk = 0; std::array<float, kDrums> score {}; bool nearClick = false; };   // junk: how much it looked like NOT a drum (0..1); drum = -1 = dropped
struct Options { const Calibration* cal = nullptr; const Model* model = nullptr; const std::vector<double>* clickTimes = nullptr; };   // clickTimes: when the metronome clicked during the take (seconds), so a click leaking from the speakers is not a hit

// ---------------------------------------------------------------- features of one hit
struct Audio { const std::vector<float>& x; double sr; std::vector<float> low8k; double fs8 = 8000; std::vector<float> hi; };   // hi: above 300 Hz (a kick's tail does not hide the next sound's start)
inline Audio prepare(const std::vector<float>& x, double sr) {
    Audio A { x, sr, {}, 8000, {} }; const int D = juce::jmax(1, (int) std::floor(sr / 8000.0)); A.fs8 = sr / D;
    A.hi = gb::a2m::filtered(x, { gb::a2m::Biquad::make(true, 300.0, sr), gb::a2m::Biquad::make(true, 300.0, sr) });
    // (40 .. 600 Hz: a "boom" and its first harmonics live there; the mouth's ring on a hat or clap mostly does not)
    auto lp = gb::a2m::filtered(x, { gb::a2m::Biquad::make(false, 600.0, sr), gb::a2m::Biquad::make(false, 600.0, sr), gb::a2m::Biquad::make(false, 600.0, sr), gb::a2m::Biquad::make(true, 40.0, sr) });
    A.low8k.resize(lp.size() / (size_t) D); for (size_t i = 0; i < A.low8k.size(); ++i) A.low8k[i] = lp[i * (size_t) D];
    return A;
}
inline Feat features(const gb::a2m::DrumEvent& e, const gb::a2m::LoopStats& S, const Audio& A) {
    using namespace gb::a2m; Feat f {}; float E = 0; for (auto d : e.delta) E += d; E = juce::jmax(E, 1e-20f);
    auto l10 = [](double v) { return (float) std::log10(v); };
    f[0] = l10((e.delta[BSub] + e.delta[BLow]) / E + 1e-3); f[1] = l10(e.delta[BMid] / E + 1e-3); f[2] = l10(e.delta[BNoise] / E + 1e-3); f[3] = l10(e.delta[BHigh] / E + 1e-3);
    // ring: the full-band 2 ms RMS from the peak until -20 dB (bounded by the next hit)
    const int n = (int) A.x.size(), a = juce::jlimit(0, n, (int) (e.sec * A.sr)), z = juce::jlimit(a, n, (int) ((e.sec + juce::jmin(0.6, (double) e.gap)) * A.sr)), w = juce::jmax(1, (int) (A.sr * 0.002));
    std::vector<float> env; for (int s = a; s + w <= z; s += w) { double q = 0; for (int k = s; k < s + w; ++k) q += (double) A.x[(size_t) k] * A.x[(size_t) k]; env.push_back((float) (q / w)); }
    float pk = 0; int pi = 0; for (int i = 0; i < (int) env.size(); ++i) if (env[(size_t) i] > pk) { pk = env[(size_t) i]; pi = i; }
    // (down to -20 dB, or to twice the room's level just before the hit when the hit is quiet: a soft hit's tail ends where the room begins)
    double room = 0; { const int ra = juce::jmax(0, a - (int) (A.sr * 0.02)); for (int k = ra; k < a; ++k) room += (double) A.x[(size_t) k] * A.x[(size_t) k]; room /= juce::jmax(1, a - ra); }
    const float stop = juce::jmax(pk * 0.01f, (float) room * 2.0f);
    int end = (int) env.size(); for (int i = pi; i < (int) env.size(); ++i) if (env[(size_t) i] < stop) { end = i; break; }
    f[4] = l10(juce::jlimit(0.004, 0.6, (end - pi) * (double) w / A.sr));
    { int sb = 0; for (int b = 1; b < kBands; ++b) if (e.delta[(size_t) b] > e.delta[(size_t) sb]) sb = b; f[5] = l10(juce::jlimit(0.005, 3.0, (double) e.t12[BHigh] * 0.5 + (double) e.t12[(size_t) sb] * 0.5)); }
    { const int zz = juce::jlimit(a, n, a + (int) (A.sr * 0.04)); int c = 0; for (int k = a + 1; k < zz; ++k) if ((A.x[(size_t) k] >= 0) != (A.x[(size_t) k - 1] >= 0)) ++c; f[6] = (float) std::log2(juce::jmax(100.0, c * A.sr / 2.0 / juce::jmax(1, zz - a)) / 1000.0); }
    f[7] = (float) std::log2(juce::jmax(100.0, (double) e.midHz) / 800.0);
    { const int a8 = juce::jlimit(0, (int) A.low8k.size(), (int) ((e.sec + 0.008) * A.fs8)), need = (int) (A.fs8 * 0.05) + (int) (A.fs8 / 50.0) + 2; float ap = 1;
      // (only when the low end carries a real share of the hit's energy, a fifth or more: a hat's low band is the room and the mouth's ring, not a "boom")
      if (a8 + need < (int) A.low8k.size() && 0.008 + need / A.fs8 <= (double) e.gap - 0.005) {   // (never into the next hit)
          double lo = 0; for (int k = a8; k < a8 + need; ++k) lo += (double) A.low8k[(size_t) k] * A.low8k[(size_t) k]; lo /= need;
          double full = 0; const int zz = juce::jmin(n, a + (int) (A.sr * 0.078)); for (int k = a; k < zz; ++k) full += (double) A.x[(size_t) k] * A.x[(size_t) k]; full /= juce::jmax(1, zz - a);
          if (lo > 1e-8 && lo > 0.2 * full) yin(A.low8k.data() + a8, need, A.fs8, ap, 50.0, 400.0); }
      f[8] = juce::jlimit(0.0f, 1.0f, 1.0f - ap); }
    f[9] = (float) juce::jmin(6, e.bursts) / 3.0f;
    f[10] = juce::jlimit(0.0f, 40.0f, e.hiDrop) / 20.0f;
    f[11] = (float) std::log2(juce::jmax(25.0, (double) e.subHz) / 60.0);
    f[12] = l10(juce::jlimit(0.001, 0.1, (pi + 0.5) * (double) w / A.sr));   // attack: from the onset to the loudest 2 ms (a drum: a few ms; a breath or a grunt: tens of ms)
    {   // the climb: above 300 Hz, 1 ms mean squares from 50 ms before the onset to 20 ms after; from the peak back to the last point under -10 dB of it
        const int w1 = juce::jmax(1, (int) (A.sr * 0.001)), a0 = juce::jmax(0, a - (int) (A.sr * 0.05)), z1 = juce::jmin(n, a + (int) (A.sr * 0.02)); std::vector<float> h;
        for (int s2 = a0; s2 + w1 <= z1; s2 += w1) { double q = 0; for (int k = s2; k < s2 + w1; ++k) q += (double) A.hi[(size_t) k] * A.hi[(size_t) k]; h.push_back((float) (q / w1)); }
        int p2 = 0; float hp = 0; const int from = (a - a0) / w1; for (int i = juce::jmax(0, from - 3); i < (int) h.size(); ++i) if (h[(size_t) i] > hp) { hp = h[(size_t) i]; p2 = i; }
        int s0 = p2; while (s0 > 0 && h[(size_t) s0 - 1] >= hp * 0.1f) --s0;
        f[14] = l10(juce::jlimit(0.001, 0.05, (p2 - s0 + 0.5) * (double) w1 / A.sr)); }   // a drum: 1..3 ms; a breath or a grunt: 20..50 ms
    juce::ignoreUnused(S); return f;
}

// ---------------------------------------------------------------- which drum
constexpr int kLoud = 13;   // the loudness feature: left out of the TEACH distance (a lesson is made loud on purpose; the beat's hats are soft)
inline void classify(Event& ev, const Model& M, const Calibration* cal) {
    const Feat z = M.z(ev.f); const bool taught = cal && cal->any();
    std::array<float, kClasses> s {};
    for (int d = 0; d < kClasses; ++d) { float v = M.W[(size_t) d][kF]; for (int i = 0; i < kF; ++i) v += M.W[(size_t) d][(size_t) i] * z[(size_t) i]; s[(size_t) d] = v; }
    auto soft = [](std::array<float, kClasses>& q) { float mx = -1e30f; for (float v : q) mx = juce::jmax(mx, v); float sum = 0; for (auto& v : q) { v = std::exp(juce::jmax(-60.0f, v - mx)); sum += v; } for (auto& v : q) v /= juce::jmax(1e-20f, sum); };
    // the built-in ears alone: how much this was "not a drum"
    { auto p = s; soft(p); ev.junk = p[kJunk]; }
    if (taught) {   // TEACH GHOST YOUR SOUNDS: the nearest of the owner's own examples (in the same standardised space) weighs more than the built-in score
        float nearest = 1e9f;
        for (int d = 0; d < kDrums; ++d) { s[(size_t) d] *= 0.2f;
            if (cal->countFor(d) > 0) { float best = 1e9f; for (auto& x : cal->ex) if (x.drum == d) { const Feat zx = M.z(x.f); float q = 0; int n = 0; for (int i = 0; i < kF && i < x.nf; ++i) { if (i == kLoud) continue; const float e = (z[(size_t) i] - zx[(size_t) i]) / juce::jmax(0.2f, M.wsd[(size_t) i]); q += e * e; ++n; } best = juce::jmin(best, q / juce::jmax(1, n)); }
                s[(size_t) d] += -5.0f * best; nearest = juce::jmin(nearest, best); }
            else s[(size_t) d] += -8.0f; }   // (a drum nobody taught is still possible, at a penalty)
        // what sounds like one of the owner's own sounds IS a drum; otherwise only what the built-in ears are very sure about is dropped
        ev.junk = nearest < 2.0f ? 0.0f : (ev.junk > 0.8f ? ev.junk : 0.0f);
    }
    s[kJunk] = -1e9f; soft(s); for (int d = 0; d < kDrums; ++d) ev.score[(size_t) d] = s[(size_t) d];   // probabilities over the drums
    ev.drum = 0; for (int d = 1; d < kDrums; ++d) if (ev.score[(size_t) d] > ev.score[(size_t) ev.drum]) ev.drum = d;
    float second = 0; for (int d = 0; d < kDrums; ++d) if (d != ev.drum) second = juce::jmax(second, ev.score[(size_t) d]);
    ev.conf = juce::jlimit(0.0f, 1.0f, ev.score[(size_t) ev.drum] - second);   // sure = the best clearly ahead of the runner-up
    // not a drum: "not a drum" is the likeliest of all, or it is near a metronome click and not clearly a drum. (Never for a loud one, within
    // 10 dB of the take's loudest hit: whatever it sounded like, the owner meant it; a wrong drum can be dragged, a dropped hit is gone.)
    if ((ev.junk > 0.5f || (ev.nearClick && ev.junk > 0.3f)) && ev.f[kLoud] < -1.0f) ev.drum = -1;
    if (ev.nearClick && ev.f[kLoud] < -2.2f) ev.drum = -1;   // right on a metronome click and 1/160th of the loudest hit's energy: the click's leftovers
}

// every hit heard, with its features (also what TEACH GHOST YOUR SOUNDS records)
// the mic's take made fit to listen to: the metronome's leaked clicks subtracted and notched (HumClicks.h) when the click times are known,
// DC and rumble under 45 Hz removed (6th order), the level brought up (a quiet take is as good as a loud one)
inline std::vector<float> prepared(const std::vector<float>& x0, double sr, const std::vector<double>* clicks = nullptr) {
    auto x = gb::a2m::filtered(clicks::removed(x0, sr, clicks), { gb::a2m::Biquad::make(true, 45.0, sr), gb::a2m::Biquad::make(true, 45.0, sr), gb::a2m::Biquad::make(true, 45.0, sr) });
    float pk = 1e-9f; for (auto v : x) pk = juce::jmax(pk, std::abs(v)); if (pk > 1e-5f) for (auto& v : x) v *= 0.7f / pk;
    return x;
}
inline std::vector<Event> hear(const std::vector<float>& x0, double sr, const std::vector<double>* clicks = nullptr) {
    std::vector<Event> out; if (x0.size() < (size_t) (sr * 0.05)) return out;
    float peak = 0; for (auto v : x0) peak = juce::jmax(peak, std::abs(v)); if (peak < 1e-5f) return out;
    const auto x = prepared(x0, sr, clicks);
    gb::a2m::BandEnv B; gb::a2m::buildBands(x, sr, B); auto ev = gb::a2m::drumEvents(B, x.size() / sr);
    // one mouth: two sounds cannot start within 50 ms of each other (a clap's later bursts, a kick's late low end, a "ts" after a "k" are
    // one hit); the first onset stays, the louder's features and energy count. (A much louder second one, 3x, IS the hit: the first was a
    // mouth click or a breath before it, and the hit's time is the second one's.)
    { std::vector<gb::a2m::DrumEvent> one; for (auto& e : ev) { if (!one.empty() && e.sec - one.back().sec < 0.05) { auto& p = one.back(); float Ep = 0, Ee = 0; for (int b = 0; b < gb::a2m::kBands; ++b) { Ep += p.delta[(size_t) b]; Ee += e.delta[(size_t) b]; }
            if (Ee > 3 * Ep) p = e; else if (Ee > Ep) { const double t0 = p.sec; p = e; p.sec = t0; } continue; } one.push_back(e); }
      for (size_t i = 0; i < one.size(); ++i) one[i].gap = (float) ((i + 1 < one.size() ? one[i + 1].sec : x.size() / sr) - one[i].sec); ev.swap(one); }
    const auto S = gb::a2m::loopStats(ev); const auto A = prepare(x, sr);
    float eMax = 1e-20f; for (auto& e : ev) { float E = 0; for (auto d : e.delta) E += d; eMax = juce::jmax(eMax, E); }
    for (auto& e : ev) { Event o; o.t = e.sec; float E = 0; for (auto d : e.delta) E += d; o.energy = E; if (E < eMax * 2e-4f) continue;   // (-37 dB: breath, not a hit)
        o.vel = juce::jlimit(0.15f, 1.0f, std::cbrt(E / eMax)); o.f = features(e, S, A); o.f[13] = (float) std::log10(E / eMax + 1e-4); out.push_back(o); }
    return out;
}
inline Take transcribe(const std::vector<float>& x, double sr, const Options& o, std::vector<Event>* diag = nullptr) {
    Take T; T.mode = Beatbox; T.seconds = x.size() / sr; auto ev = hear(x, sr, o.clickTimes); const Model& M = o.model ? *o.model : defaultModel();
    clicks::Mask mask; mask.times = o.clickTimes;
    for (auto& e : ev) { e.nearClick = mask.near(e.t);   // (the click reaches the mic a little late; inside its window an onset must prove itself a drum)
        classify(e, M, o.cal); }
    ev.erase(std::remove_if(ev.begin(), ev.end(), [](const Event& e) { return e.drum < 0; }), ev.end());   // not drums
    for (auto& e : ev) { Hit h; h.t = e.t; h.drum = e.drum; h.vel = e.vel; h.conf = e.conf; T.hits.push_back(h); }
    T.origHits = T.hits; if (diag) *diag = ev; return T;
}

// the built-in numbers (HumTests "beattrain" prints this block from our own synthesised beatbox sounds; see docs/GHOST-HUM.md)
inline const Model& defaultModel() {
    static const Model M = [] { Model m;
        // MODEL BEGIN (HumTests beattrain)
        m.mu = { -1.24249f, -1.19752f, -1.27705f, -1.66552f, -1.65873f, -0.72033f, 0.56938f, 0.08365f, 0.45852f, 0.82753f, 0.36497f, 1.04315f, -2.04317f, -1.01702f, -2.35083f };
        m.sg = { 1.17989f, 0.80570f, 1.07939f, 1.07029f, 0.66497f, 0.61251f, 1.95935f, 0.81486f, 0.39892f, 0.49574f, 0.29920f, 0.61295f, 0.53473f, 0.75539f, 0.49999f };
        m.wsd = { 0.49462f, 0.45529f, 0.31006f, 0.25497f, 0.87299f, 0.72201f, 0.38520f, 0.49338f, 0.86894f, 0.77215f, 0.68340f, 1.05048f, 0.80033f, 0.49197f, 0.69689f };
        m.W[0] = { 0.92874f, 0.51872f, -1.21748f, -0.40994f, 0.70126f, 0.08129f, -0.53582f, -1.26985f, 0.08819f, -0.47365f, 0.83616f, 0.30097f, 0.08217f, 1.27569f, -0.12650f, -0.44205f };   // KICK
        m.W[1] = { 0.91708f, 1.64860f, 0.77946f, 0.87129f, 0.64389f, -0.43588f, -0.19551f, 0.63001f, 0.37778f, 0.63249f, -1.01474f, 0.09864f, -0.59398f, 0.58338f, 0.03477f, -0.28317f };   // SNARE
        m.W[2] = { -0.27426f, -1.61684f, -0.20141f, 1.50771f, -0.46796f, -0.44743f, 0.78646f, 0.06541f, 0.25539f, -0.46091f, 0.68105f, -0.12931f, -0.33017f, -0.89545f, 0.30340f, 0.26171f };   // HI-HAT
        m.W[3] = { -0.83201f, -1.68689f, 0.47184f, 1.38520f, 0.30339f, 1.09383f, 0.71644f, 0.13940f, 0.10788f, 0.22349f, -1.20786f, -0.16598f, 0.46326f, -0.20137f, 0.06210f, -0.93360f };   // OPEN HAT
        m.W[4] = { -0.44809f, 1.79645f, 0.26589f, -2.63102f, 0.18358f, -0.13491f, -0.38967f, 0.57185f, -0.19958f, 0.30190f, 0.64110f, -0.26452f, 0.40085f, 0.33299f, -0.51718f, -0.30724f };   // CLAP
        m.W[5] = { -0.29146f, -0.66004f, -0.09829f, -0.72325f, -1.36416f, -0.15690f, -0.38191f, -0.13682f, -0.62967f, -0.22331f, 0.06428f, 0.16020f, -0.02213f, -1.09525f, 0.24342f, 1.70435f };   // NOT A DRUM
        // MODEL END
        return m; }();
    return M;
}

} // namespace hum::beat
