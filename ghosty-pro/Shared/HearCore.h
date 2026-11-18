#pragma once
// HearCore: the GhostBlock audio-analysis core shared by Ghosty Beats (RE-CREATE AS MIDI, BeatsX/AudioToMidi.h) and Ghost Hum (GhostHum/).
// Moved here from BeatsX/AudioToMidi.h unchanged (same namespace gb::a2m, same numbers): RBJ biquads, YIN pitch, the five-band envelope
// analysis, band-wise onset events with their per-hit features, and the event feature vector. Pure analysis, no UI, no external libraries.
// Ghosty Beats' drum classifier (DrumModel, stages, writers) stays in AudioToMidi.h; Ghost Hum's beatbox classifier is GhostHum/HumBeat.h.
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace gb::a2m {

enum Band : int { BSub = 0, BLow, BMid, BNoise, BHigh, kBands };
struct DrumEvent {
    double sec = 0;
    std::array<float, kBands> pre {}, post {}, delta {}, rise {}, t12 {}, postFull {};   // (postFull: the peak over 40 ms, past the next event)
    float subHz = 0, midHz = 0, hiDrop = 0, gap = 1, midDepth = 0, hiLate = 0; int bursts = 0, midBursts = 0;
    uint32_t roles = 0; std::array<float, 10> vel {}; int cluster = -1;
    bool has(int r) const { return (roles >> r) & 1u; }
};
constexpr int kFeat = 32;   // the per-event feature vector (eventFeatures)

// ---------------------------------------------------------------- small DSP helpers
struct Biquad {   // RBJ cookbook, transposed direct form II
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    static Biquad make(bool high, double f, double sr, double q = 0.7071) {
        Biquad B; f = juce::jlimit(10.0, sr * 0.45, f); const double w = juce::MathConstants<double>::twoPi * f / sr, c = std::cos(w), al = std::sin(w) / (2 * q), a0 = 1 + al;
        if (high) { B.b0 = (1 + c) / 2 / a0; B.b1 = -(1 + c) / a0; B.b2 = (1 + c) / 2 / a0; } else { B.b0 = (1 - c) / 2 / a0; B.b1 = (1 - c) / a0; B.b2 = (1 - c) / 2 / a0; }
        B.a1 = -2 * c / a0; B.a2 = (1 - al) / a0; return B;
    }
    float run(float x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return (float) y; }
};
inline std::vector<float> filtered(const std::vector<float>& x, std::initializer_list<Biquad> chain) {
    std::vector<float> y(x); for (auto B : chain) for (auto& v : y) v = B.run(v); return y;
}

// YIN on a mono buffer at rate fs: f0 (Hz, 0 = none) and the aperiodicity (min of the CMNDF; low = clearly periodic)
inline float yin(const float* x, int n, double fs, float& aperiodicity, double fmin = 30.0, double fmax = 1000.0) {
    aperiodicity = 1; const int tMin = juce::jmax(2, (int) (fs / fmax)), tMax = (int) (fs / fmin), W = juce::jmin(n - tMax - 1, (int) (fs * 0.05));
    if (W < tMax / 2 || W < 16) return 0;
    std::vector<float> d((size_t) tMax + 2, 0.0f);
    for (int t = 1; t <= tMax + 1; ++t) { double s = 0; for (int j = 0; j < W; ++j) { const float e = x[j] - x[j + t]; s += (double) e * e; } d[(size_t) t] = (float) s; }
    std::vector<float> c((size_t) tMax + 2, 1.0f); double run = 0;
    for (int t = 1; t <= tMax + 1; ++t) { run += d[(size_t) t]; c[(size_t) t] = run > 0 ? (float) (d[(size_t) t] * t / run) : 1.0f; }
    int best = -1; for (int t = tMin; t <= tMax; ++t) if (c[(size_t) t] < 0.15f) { while (t + 1 <= tMax && c[(size_t) (t + 1)] < c[(size_t) t]) ++t; best = t; break; }
    if (best < 0) { best = tMin; for (int t = tMin; t <= tMax; ++t) if (c[(size_t) t] < c[(size_t) best]) best = t; }
    aperiodicity = c[(size_t) best];
    double tau = best; const double a = c[(size_t) (best - 1)], b = c[(size_t) best], cc = c[(size_t) (best + 1)], den = a - 2 * b + cc;
    if (best > 1 && std::abs(den) > 1e-12) tau += 0.5 * (a - cc) / den;
    return tau > 0 ? (float) (fs / tau) : 0.0f;
}
inline int midiOf(double hz) { return (int) std::lround(69.0 + 12.0 * std::log2(hz / 440.0)); }

// ---------------------------------------------------------------- DRUMS 1: five-band envelopes, band-wise onsets, per-hit features
// Bands: SUB < 120 Hz, LOW 120..300, MID 300..2 k, NOISE 2 k..8 k, HIGH > 8 k (4th-order RBJ sections). Mean square per 0.5 ms
// sub-frame; envelopes per 2.5 ms frame over centred windows of 20 / 10 / 5 / 5 / 5 ms (longer than a low band's waveform period).
// ONSETS per band: the rise (dB) of the envelope over the minimum of the window before it, local maxima >= 5..6 dB (and >= the
// local median + 3 dB: noisy loops), the band within 50 dB of its loudest. Band onsets within 12 ms (sub: 20 ms) make one event, so a
// hat over a kick, or a kick under a held bass, is found by its own band even when the full-band level hardly moves.
// FEATURES of each event (its first 30..40 ms, never past the next event): per band the energy just before and the peak after, the
// NEW energy (after - before, so a ringing 808 or a tail is not counted again) and its rise; per band the time to fall 12 dB (until
// that band is hit again, extrapolated past it); the low end's frequency (zero crossings < 300 Hz, 15..80 ms in); the mid band's
// zero-crossing frequency (noise vs tone); clap bursts (peaks of the 300 Hz..8 kHz envelope in the first 45 ms, each out of a dip).
struct BandEnv {
    static constexpr int kSub = 5;                               // sub-frames per frame
    double sr = 48000, off = 0; int subLen = 24, frames = 0;     // off: the silence put before the loop (so a hit at 0 is an onset too)
    std::array<int, kBands> hw { 20, 10, 5, 5, 5 };              // half windows, in sub-frames
    std::array<std::vector<float>, kBands> se;                    // mean square per sub-frame
    std::array<std::vector<double>, kBands> cum;                  // their running sums
    std::array<std::vector<float>, kBands> db;                    // per frame, dB
    std::vector<float> lo, mid;                                   // < 300 Hz, 300 Hz..2 kHz (zero crossings)
    double frameSec() const { return kSub * subLen / sr; }
    double centreSec(int f) const { return (f * kSub + 0.5 * kSub) * subLen / sr - off; }
    float energy(int b, double fromSec, double toSec) const {     // mean square over [from, to)
        const int S = (int) se[(size_t) b].size(); const int a = juce::jlimit(0, S, (int) std::floor((fromSec + off) * sr / subLen)), z = juce::jlimit(0, S, (int) std::ceil((toSec + off) * sr / subLen));
        return z > a ? (float) ((cum[(size_t) b][(size_t) z] - cum[(size_t) b][(size_t) a]) / (z - a)) : 0.0f; }
};
inline void buildBands(const std::vector<float>& x0, double sr, BandEnv& B) {
    const int pad = (int) std::lround(sr * 0.05); std::vector<float> x((size_t) pad, 0.0f); x.insert(x.end(), x0.begin(), x0.end());
    B.sr = sr; B.off = pad / sr; B.subLen = juce::jmax(1, (int) std::lround(sr * 0.0005)); const int n = (int) x.size(), S = n / B.subLen;
    const double hc = juce::jmin(8000.0, sr * 0.4);
    auto LP = [&](double f) { return Biquad::make(false, f, sr); }; auto HP = [&](double f) { return Biquad::make(true, f, sr); };
    for (int b = 0; b < kBands; ++b) {
        std::vector<Biquad> ch;
        switch (b) { case BSub: ch = { LP(120), LP(120), HP(25) }; break; case BLow: ch = { HP(120), HP(120), LP(300), LP(300) }; break;
                     case BMid: ch = { HP(300), HP(300), LP(2000), LP(2000) }; break; case BNoise: ch = { HP(2000), HP(2000), LP(hc), LP(hc) }; break; default: ch = { HP(hc), HP(hc) }; break; }
        auto& out = B.se[(size_t) b]; out.assign((size_t) S, 0.0f); if (b == BMid) B.mid.assign((size_t) n, 0.0f);
        for (int s = 0; s < S; ++s) { double acc = 0; for (int k = s * B.subLen; k < (s + 1) * B.subLen; ++k) { float v = x[(size_t) k]; for (auto& q : ch) v = q.run(v); acc += (double) v * v; if (b == BMid) B.mid[(size_t) k] = v; } out[(size_t) s] = (float) (acc / B.subLen); }
        auto& c = B.cum[(size_t) b]; c.assign((size_t) S + 1, 0.0); for (int s = 0; s < S; ++s) c[(size_t) s + 1] = c[(size_t) s] + out[(size_t) s];
    }
    { B.lo.assign((size_t) n, 0.0f); auto a = LP(300), c = LP(300); for (int k = 0; k < n; ++k) B.lo[(size_t) k] = c.run(a.run(x[(size_t) k])); }
    B.frames = S / BandEnv::kSub; double gmax = 1e-20; std::array<double, kBands> bmax {};
    std::array<std::vector<float>, kBands> lin;
    for (int b = 0; b < kBands; ++b) { auto& v = lin[(size_t) b]; v.assign((size_t) B.frames, 0.0f);
        for (int f = 0; f < B.frames; ++f) { const int cs = f * BandEnv::kSub + BandEnv::kSub / 2; const int a = juce::jmax(0, cs - B.hw[(size_t) b]), z = juce::jmin(S, cs + B.hw[(size_t) b]);
            v[(size_t) f] = z > a ? (float) ((B.cum[(size_t) b][(size_t) z] - B.cum[(size_t) b][(size_t) a]) / (z - a)) : 0.0f; bmax[(size_t) b] = juce::jmax(bmax[(size_t) b], (double) v[(size_t) f]); }
        gmax = juce::jmax(gmax, bmax[(size_t) b]); }
    for (int b = 0; b < kBands; ++b) { const double fl = juce::jmax(bmax[(size_t) b] * 1e-6, gmax * 1e-9, 1e-20); auto& d = B.db[(size_t) b]; d.resize((size_t) B.frames);
        for (int f = 0; f < B.frames; ++f) d[(size_t) f] = (float) (10.0 * std::log10(lin[(size_t) b][(size_t) f] + fl)); }
}
inline std::vector<DrumEvent> drumEvents(const BandEnv& B, double seconds) {
    struct Cand { double t; int band; float rise; };
    std::vector<Cand> cand; const int F = B.frames; if (F < 8) return {};
    static constexpr float kThr[kBands] { 5.0f, 5.0f, 6.0f, 5.0f, 3.0f };
    float gTop = -300; for (int b = 0; b < kBands; ++b) for (auto v : B.db[(size_t) b]) gTop = juce::jmax(gTop, v);
    for (int b = 0; b < kBands; ++b) {
        const auto& d = B.db[(size_t) b]; float top = -300; for (auto v : d) top = juce::jmax(top, v);
        const int lag = (2 * B.hw[(size_t) b] + BandEnv::kSub - 1) / BandEnv::kSub + 1;
        std::vector<float> r((size_t) F, 0.0f);
        for (int f = 1; f < F; ++f) { float m = d[(size_t) f - 1]; for (int k = juce::jmax(0, f - lag); k < f; ++k) m = juce::jmin(m, d[(size_t) k]); r[(size_t) f] = juce::jmax(0.0f, d[(size_t) f] - m); }
        // the local median of the rise (+-250 ms, every 4th frame): what the band's own noise rises by
        const int W = (int) std::lround(0.25 / B.frameSec()); std::vector<float> med((size_t) F, 0.0f), tmp;
        for (int f = 0; f < F; f += 8) { tmp.clear(); for (int k = juce::jmax(0, f - W); k < juce::jmin(F, f + W); k += 4) tmp.push_back(r[(size_t) k]);
            std::nth_element(tmp.begin(), tmp.begin() + (long) tmp.size() / 2, tmp.end()); const float m = tmp[tmp.size() / 2]; for (int k = f; k < juce::jmin(F, f + 8); ++k) med[(size_t) k] = m; }
        for (int f = 1; f + 1 < F; ++f) {
            const float v = r[(size_t) f]; if (v < juce::jmax(kThr[b], med[(size_t) f] + 3.0f)) continue;
            if (d[(size_t) f] < top - 50.0f || d[(size_t) f] < gTop - 75.0f) continue;
            bool mx = true; for (int k = juce::jmax(1, f - 3); k <= juce::jmin(F - 1, f + 3) && mx; ++k) if (r[(size_t) k] > v || (!(r[(size_t) k] < v) && k < f)) mx = false;
            if (!mx) continue;
            // (the window's centre is hw after the onset when the rise peaks)
            const double at = B.centreSec(f) - B.hw[(size_t) b] * B.subLen / B.sr; if (at < -0.004 || at >= seconds) continue;
            cand.push_back({ juce::jmax(0.0, at), b, v });
        }
    }
    std::sort(cand.begin(), cand.end(), [](const Cand& a, const Cand& b) { return a.t < b.t; });
    if (std::getenv("A2M_DEBUG")) for (auto& c : cand) std::printf("    cand %.4f band %d rise %.1f\n", c.t, c.band, c.rise);
    std::vector<DrumEvent> ev; std::vector<bool> used(cand.size(), false);
    for (size_t i = 0; i < cand.size(); ++i) {
        if (used[i]) continue;
        const double t0 = cand[i].t; double best = cand[i].band != BSub ? t0 : 1e9; bool any = cand[i].band != BSub; used[i] = true;
        // (the sub band joins up to 45 ms late: a kick's sweep only reaches it after 20..40 ms)
        for (size_t j = i + 1; j < cand.size() && cand[j].t - t0 <= 0.045; ++j) {
            if (used[j]) continue; const bool sub = cand[j].band == BSub;
            if (cand[j].t - t0 <= (sub ? (any ? 0.045 : 0.020) : 0.012)) { used[j] = true; if (!sub) { best = juce::jmin(best, cand[j].t); any = true; } }
        }
        DrumEvent e; e.sec = any ? best : t0; ev.push_back(e);
    }
    // features (twice: the events that bring nothing new in any band are dropped, then the rest measured with their true gaps)
    const double fr = B.frameSec();
    auto measure = [&] {
        for (size_t i = 0; i < ev.size(); ++i) {
            auto& e = ev[i]; const double t = e.sec, tn = i + 1 < ev.size() ? ev[i + 1].sec : seconds; e.gap = (float) (tn - t);
            for (int b = 0; b < kBands; ++b) {
                const double h = B.hw[(size_t) b] * B.subLen / B.sr;
                // before: the mean square over the 10..25 ms that end 1 ms before the onset (a ringing tone's own wobble is averaged out);
                // after: the peak of the band's window over the first 30 / 40 ms
                static constexpr double kPre[kBands] { 0.025, 0.015, 0.012, 0.010, 0.010 };
                const float pre = B.energy(b, t - 0.001 - kPre[b], t - 0.001);
                // (the windows never reach into the next event)
                const double wEnd = t + juce::jmin(b <= BLow ? 0.040 : 0.030, e.gap - h - 0.001);
                float post = 0; double pkC = t + h; for (double c = t + h - 0.5 * fr; c <= wEnd + 1e-9 || c <= t + h; c += 0.5 * fr) { const float v = B.energy(b, c - h, c + h); if (v > post) { post = v; pkC = c; } }
                float full = 0; for (double c = t + h - 0.5 * fr; c <= t + 0.040; c += 0.5 * fr) full = juce::jmax(full, B.energy(b, c - h, c + h));
                e.postFull[(size_t) b] = juce::jmax(full, post);
                e.pre[(size_t) b] = pre; e.post[(size_t) b] = post; e.delta[(size_t) b] = juce::jmax(0.0f, post - pre);
                // the rise: over the LOUDEST window of the 25 ms before (a waveform's own per-cycle bursts, a clap's later claps, never rise)
                // (HIGH counts from 2 dB: a closed hat over a ringing open hat adds only a few)
                static constexpr double kMaxWin[kBands] { 0.025, 0.025, 0.025, 0.025, 0.025 };
                float preMax = pre; for (double c = t - 0.001 - h; kMaxWin[b] > 0 && c >= t - 0.001 - kMaxWin[b] + h - 1e-9; c -= 0.5 * fr) preMax = juce::jmax(preMax, B.energy(b, c - h, c + h));
                e.rise[(size_t) b] = (float) (10.0 * std::log10((post + 1e-20) / (preMax + 1e-20)));
                if (b == BHigh) {   // a CHOKE: the ringing (open) hat stops (-3 dB in the 10 ms before) and a closed hat starts (+3 dB over that)
                    const float justBefore = B.energy(b, t - 0.003, t - 0.0005), earlier = B.energy(b, t - 0.015, t - 0.008);
                    const float steady = juce::jmin(B.energy(b, t - 0.015, t - 0.0125), B.energy(b, t - 0.0125, t - 0.010), B.energy(b, t - 0.010, t - 0.008));   // (it rang: not a click)
                    if (justBefore < 0.5f * earlier && steady >= 0.3f * earlier && post > 2.0f * justBefore) e.rise[(size_t) b] = juce::jmax(e.rise[(size_t) b], (float) (10.0 * std::log10((post + 1e-20) / (justBefore + 1e-20))) - 1.0f);
                }
                e.t12[(size_t) b] = (float) pkC;   // (the peak's time for now; the decay pass below)
            }
        }
    };
    measure();
    {   // keep an event only if some band rises >= 4..5 dB with new energy >= 0.3 % of that band's biggest and within 45 dB of its loudest
        static constexpr float kV[kBands] { 4.0f, 4.0f, 5.0f, 4.0f, 2.0f };
        std::array<float, kBands> dMax {}, pMax {}; for (auto& e : ev) for (int b = 0; b < kBands; ++b) { dMax[(size_t) b] = juce::jmax(dMax[(size_t) b], e.delta[(size_t) b]); pMax[(size_t) b] = juce::jmax(pMax[(size_t) b], e.post[(size_t) b]); }
        // (within 45 ms of the last event its low end may still be building up: SUB / LOW count only 3 dB over that event's own peak)
        std::vector<DrumEvent> keep; for (size_t i = 0; i < ev.size(); ++i) { const auto& e = ev[i]; bool ok = false;
            for (int b = 0; b < kBands && !ok; ++b) { ok = e.rise[(size_t) b] >= kV[b] && e.delta[(size_t) b] >= 0.01f * dMax[(size_t) b] && e.post[(size_t) b] >= 3.2e-5f * pMax[(size_t) b];
                const bool near = !keep.empty() && e.sec - keep.back().sec < 0.045;
                if (ok && near && b <= BLow && e.post[(size_t) b] < 2.0f * keep.back().postFull[(size_t) b]) ok = false; }
            if (ok) keep.push_back(e); }
        ev.swap(keep); measure();
        if (std::getenv("A2M_DEBUG")) for (auto& e : ev) std::printf("    event %.4f rise %.1f %.1f %.1f %.1f %.1f  delta %.2g %.2g %.2g %.2g %.2g\n", e.sec, e.rise[0], e.rise[1], e.rise[2], e.rise[3], e.rise[4], e.delta[0], e.delta[1], e.delta[2], e.delta[3], e.delta[4]);
    }
    // decay per band: from the peak until it falls 12 dB, until that band is struck again (rises >= 4 dB with >= 30 % of this one's
    // level), extrapolated from the slope when it is struck again before; capped at 3 s
    for (size_t i = 0; i < ev.size(); ++i) {
        auto& e = ev[i];
        for (int b = 0; b < kBands; ++b) {
            const double pk = e.t12[(size_t) b]; double end = juce::jmin(seconds, e.sec + 3.0);
            for (size_t j = i + 1; j < ev.size(); ++j) { if (ev[j].sec > end) break; if (ev[j].rise[(size_t) b] >= 4.0f && ev[j].post[(size_t) b] >= 0.3f * e.post[(size_t) b]) { end = ev[j].sec; break; } }
            const int f0 = juce::jlimit(0, F - 1, (int) std::floor((pk + B.off) / fr)); const float top = B.db[(size_t) b][(size_t) f0];
            double t12 = -1; int f = f0; for (; f < F && B.centreSec(f) <= end; ++f) if (B.db[(size_t) b][(size_t) f] <= top - 12.0f) { t12 = B.centreSec(f) - e.sec; break; }
            if (t12 < 0) { const int fl = juce::jlimit(f0, F - 1, f - 1); const double span = juce::jmax(fr, B.centreSec(fl) - pk); const double drop = top - B.db[(size_t) b][(size_t) fl];
                t12 = drop > 1.0 ? (pk - e.sec) + span * 12.0 / drop : 3.0; }
            e.t12[(size_t) b] = (float) juce::jlimit(0.0, 3.0, t12);
            if (b == BHigh) { const int f40 = juce::jmin(F - 1, f0 + (int) std::lround(0.04 / fr)); const double lim = juce::jmin(end, e.sec + (double) e.gap);
                e.hiDrop = B.centreSec(f40) <= lim ? top - B.db[(size_t) b][(size_t) f40] : (float) (12.0 * 0.04 / juce::jmax(0.005, (double) e.t12[(size_t) b])); }
        }
        // zero crossings: the low end 15..80 ms in, the mid band over the first 30 ms
        auto zc = [&](const std::vector<float>& s, double a, double z) { const int ia = juce::jlimit(0, (int) s.size(), (int) ((a + B.off) * B.sr)), iz = juce::jlimit(0, (int) s.size(), (int) ((z + B.off) * B.sr)); if (iz - ia < (int) (0.01 * B.sr)) return 0.0f;
            int c = 0; for (int k = ia + 1; k < iz; ++k) if ((s[(size_t) k] >= 0) != (s[(size_t) k - 1] >= 0)) ++c; return (float) (c * B.sr / 2.0 / (iz - ia)); };
        e.subHz = zc(B.lo, e.sec + 0.015, e.sec + juce::jmin(0.08, (double) e.gap));
        e.hiLate = juce::jmax(0.0f, B.energy(BHigh, e.sec + 0.004, e.sec + juce::jlimit(0.008, 0.020, (double) e.gap - 0.001)) - e.pre[BHigh]);
        e.midHz = zc(B.mid, e.sec, e.sec + juce::jmin(0.03, (double) e.gap));
        // clap bursts: 1.5 ms mean square of MID + NOISE, peaks >= 40 % of the loudest, >= 5 ms apart, each out of a dip <= 40 % of it
        { const int S = (int) B.se[0].size(); const int a = juce::jlimit(0, S, (int) ((e.sec - 0.002 + B.off) * B.sr / B.subLen)), z = juce::jlimit(0, S, (int) ((e.sec + juce::jmin(0.045, (double) e.gap) + B.off) * B.sr / B.subLen));
          std::vector<float> v; for (int s = a; s < z; ++s) { double m = 0; int c = 0; for (int k = juce::jmax(0, s - 1); k <= juce::jmin(S - 1, s + 1); ++k) { m += B.se[BMid][(size_t) k] + B.se[BNoise][(size_t) k]; ++c; } v.push_back((float) (m / c)); }
          float mx = 0; for (auto q : v) mx = juce::jmax(mx, q); int cnt = 0, last = -100; float dip = 0;
          const int gapS = (int) std::lround(0.005 * B.sr / B.subLen);
          for (int k = 1; k + 1 < (int) v.size(); ++k) { dip = cnt == 0 ? 0.0f : juce::jmin(dip, v[(size_t) k]);
              if (v[(size_t) k] >= v[(size_t) k - 1] && v[(size_t) k] > v[(size_t) k + 1] && v[(size_t) k] >= 0.4f * mx && k - last >= gapS && (cnt == 0 || dip <= 0.4f * v[(size_t) k])) { ++cnt; last = k; dip = v[(size_t) k]; } }
          e.bursts = cnt;
          // the same on MID alone (a hat over a clap hides its bursts in NOISE), and how deep MID's envelope dips between its peaks (dB)
          std::vector<float> m; for (int s2 = a; s2 < z; ++s2) { double q = 0; int c = 0; for (int k = juce::jmax(0, s2 - 1); k <= juce::jmin(S - 1, s2 + 1); ++k) { q += B.se[BMid][(size_t) k]; ++c; } m.push_back((float) (q / c)); }
          float mm = 0; for (auto q : m) mm = juce::jmax(mm, q); int c2 = 0, l2 = -100; float d2 = 0, deepest = 0;
          for (int k = 1; k + 1 < (int) m.size(); ++k) { d2 = c2 == 0 ? 0.0f : juce::jmin(d2, m[(size_t) k]);
              if (m[(size_t) k] >= m[(size_t) k - 1] && m[(size_t) k] > m[(size_t) k + 1] && m[(size_t) k] >= 0.4f * mm && k - l2 >= gapS) {
                  if (c2 > 0) deepest = juce::jmax(deepest, (float) (10.0 * std::log10((m[(size_t) k] + 1e-20) / (d2 + 1e-20))));
                  if (c2 == 0 || d2 <= 0.4f * m[(size_t) k]) ++c2; l2 = k; d2 = m[(size_t) k]; } }
          e.midBursts = c2; e.midDepth = deepest; }
    }
    return ev;
}

// ---------------------------------------------------------------- per-event features (the classifier input)
struct LoopStats { std::array<float, kBands> dMax {}; float eMax = 1e-20f; };   // per loop: the biggest new energy per band / in all
inline LoopStats loopStats(const std::vector<DrumEvent>& ev) {
    LoopStats S; for (auto& e : ev) { float E = 0; for (int b = 0; b < kBands; ++b) { S.dMax[(size_t) b] = juce::jmax(S.dMax[(size_t) b], e.delta[(size_t) b]); E += e.delta[(size_t) b]; } S.eMax = juce::jmax(S.eMax, E); }
    return S;
}
inline std::array<float, kFeat> eventFeatures(const DrumEvent& e, const LoopStats& S) {
    std::array<float, kFeat> f {}; float E = 0; for (auto d : e.delta) E += d; E = juce::jmax(E, 1e-20f);
    auto l10 = [](double v) { return (float) std::log10(v); };
    for (int b = 0; b < kBands; ++b) {
        const auto B = (size_t) b;
        f[B] = l10(e.delta[B] / E + 1e-4) / 4;
        f[5 + B] = juce::jlimit(-12.0f, 36.0f, e.rise[B]) / 24.0f;
        f[10 + B] = l10(juce::jlimit(0.005, 3.0, (double) e.t12[B])) / 2;
        f[15 + B] = l10(e.delta[B] / juce::jmax(1e-20f, S.dMax[B]) + 1e-4) / 4;
    }
    f[20] = l10(E / S.eMax + 1e-6) / 6;
    f[21] = (float) std::log2(juce::jmax(20.0, (double) e.subHz) / 60.0) / 2;
    f[22] = (float) std::log2(juce::jmax(100.0, (double) e.midHz) / 800.0) / 2;
    f[23] = juce::jlimit(0.0f, 40.0f, e.hiDrop) / 20.0f;
    f[24] = (float) juce::jmin(6, e.bursts) / 3.0f;
    f[25] = l10(juce::jlimit(0.01, 2.0, (double) e.gap));
    f[26] = juce::jlimit(-40.0f, 10.0f, (float) (10.0 * std::log10((e.pre[BSub] + 1e-20) / (e.post[BSub] + 1e-20)))) / 20.0f;
    f[27] = juce::jlimit(-40.0f, 10.0f, (float) (10.0 * std::log10((e.pre[BHigh] + 1e-20) / (e.post[BHigh] + 1e-20)))) / 20.0f;
    f[28] = (float) juce::jmin(6, e.midBursts) / 3.0f;
    f[29] = juce::jlimit(0.0f, 30.0f, e.midDepth) / 15.0f;
    f[30] = l10(S.dMax[BHigh] / (S.dMax[BHigh] + S.dMax[BNoise] + 1e-20f) + 1e-3) / 3;   // the loop's own top end (lo-fi loops: none)
    f[31] = l10(S.dMax[BSub] / (S.dMax[BSub] + S.dMax[BMid] + 1e-20f) + 1e-3) / 3;
    return f;
}
inline float sigmoid(float x) { return 1.0f / (1.0f + std::exp(-juce::jlimit(-30.0f, 30.0f, x))); }
inline float logit(float p) { p = juce::jlimit(1e-4f, 1.0f - 1e-4f, p); return std::log(p / (1.0f - p)); }
template <size_t N> inline float dot(const std::array<float, N + 1>& w, const std::array<float, N>& x) { float s = w[N]; for (size_t i = 0; i < N; ++i) s += w[i] * x[i]; return s; }

} // namespace gb::a2m
