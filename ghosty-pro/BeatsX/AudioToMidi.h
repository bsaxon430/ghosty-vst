#pragma once
// RE-CREATE AS MIDI (Ghosty Beats): an audio loop (drums / break / bass / any rhythm) -> an editable pattern (DRUMS) or a tracker
// bassline (PITCHED). Pure analysis + writers, no UI; runs on a background thread (never the audio thread). No external libraries.
//
// 1. READ      up to 120 s of the file (longer files: the first 120 s; an 8-hour file is never loaded whole), mixed to mono, any rate.
// 2. ONSETS    gb::rhythm::chop (3-band energy flux, 2.5 ms frames, min gap 30 ms, sample-accurate rise point): the tempo search;
//              DRUMS use their own five-band onsets (4).
// 3. TEMPO     in this order: a BPM typed by the user > a BPM in the file name ("... 140bpm ...", what Mr. Mickens and Beats drag out) >
//              the loop length (1/2/4/8/16 bars) if the onsets agree with it (comb score >= 90 % of the best) > the best comb score over
//              80..160 BPM (onsets weighted by strength, Gaussian distance to the 1/16 grid, sigma 10 ms). x2 / /2 in the UI.
//              BARS = the loop length in bars (a loop) or the last onset rounded up, snapped to 1 / 2 / 4 / 8.
// 4. CLASSIFY  DRUMS (DRUMS 1 + 2 below; docs/research/RECREATE-DRUMS.md): onsets found per band (SUB / LOW / MID / NOISE / HIGH), so a
//              hat over a kick or a kick under an 808 is its own find; per event the NEW energy per band, rises, decays, low-end pitch,
//              noisiness, clap bursts...; a small network scores every drum separately (one event can be kick + hat + 808), then the
//              loop teaches itself (its sounds clustered, repeated bars vote, the hat grid) and a threshold per drum decides.
//              Drums: KICK, SNARE, CLAP, HAT, OPEN HAT, TOM, PERC, CYMBAL, 808.
//              VELOCITY  from each drum's own part of the event against the loudest of that drum (cube root: the engine's vel^1.5).
// 5. QUANTIZE  to the grid (1/8, 1/16, 1/32) with a strength (100 % = on the grid; less keeps that much of the feel as step NUDGE).
//              Swing is measured (the off-beat 8ths' average delay) and reported.
// 6. WRITE     onto the kit's tracks by role (Kick, Snare / Clap, Hat, Open Hat, Tom / Perc, Cymbal, 808 ...; a missing role falls back to
//              the closest: clap <-> snare, open hat -> hat, tom -> perc, perc -> tom -> hat, cymbal -> open hat -> hat, 808 -> kick). Locked
//              tracks are left alone; the panel can leave whole drums out. Optional: the loop's own strongest hit per role becomes that
//              track's sound (a sample-accurate re-creation).
// PITCHED: the same onsets + YIN (cumulative mean normalised difference, threshold 0.15, parabolic refinement) on ~11 kHz audio, a few
//              frames inside each note; consecutive frames with the same rounded MIDI note make one note (legato pitch changes split).
//              AUTO picks PITCHED when >= 55 % of the loud 20 ms frames are clearly periodic (aperiodicity < 0.2, 30..1000 Hz), unless the
//              hits bring top end like drums do (>= 30 % of them >= 6 % of their new energy above 2 kHz: a tonal kick / 808 loop stays DRUMS).
//              Notes go onto the MELODY tracker (channel 1, 1/16 rows) of the current pattern.
#include "Rhythm.h"
#include "MelodyTracker.h"
#include <atomic>
#include <functional>

namespace gb::a2m {

enum DrumRole : int { Kick = 0, Snare, Clap, HatClosed, HatOpen, Tom, Perc, Cymbal, Bass, kNumRoles };
inline const char* roleName(int r) { static const char* n[] { "KICK", "SNARE", "CLAP", "HAT", "OPEN HAT", "TOM", "PERC", "CYMBAL", "808" }; return r >= 0 && r < kNumRoles ? n[r] : "?"; }

struct Loop { std::vector<float> x; double sr = 48000; bool truncated = false; juce::String name; double seconds() const { return x.size() / sr; } };
struct Hit { double sec = 0; int role = Perc; float vel = 1; };
enum Band : int { BSub = 0, BLow, BMid, BNoise, BHigh, kBands };
struct DrumEvent {
    double sec = 0;
    std::array<float, kBands> pre {}, post {}, delta {}, rise {}, t12 {}, postFull {};   // (postFull: the peak over 40 ms, past the next event)
    float subHz = 0, midHz = 0, hiDrop = 0, gap = 1, midDepth = 0, hiLate = 0; int bursts = 0, midBursts = 0;
    uint32_t roles = 0; std::array<float, 10> vel {}; int cluster = -1;
    bool has(int r) const { return (roles >> r) & 1u; }
};

struct Note { double sec = 0, len = 0; int midi = 60; float vel = 1; };
struct Result {
    bool ok = false, pitched = false; float voicedRatio = 0;
    double bpm = 120; juce::String bpmFrom;   // "typed", "file name", "loop length", "onsets"
    int bars = 1; double origin = 0, swing = 50; int onsets = 0;
    std::vector<Hit> hits; std::vector<Note> notes;
    std::array<int, kNumRoles> perRole {};
    std::vector<DrumEvent> events;   // DRUMS: every event heard, with its features (tests / diagnostics)
};
constexpr int kFeat = 32, kHidden = 24, kCtx = 3 * kNumRoles + 2;
struct DrumModel {                                                // DRUMS: the classifier's numbers (see DRUMS 2)
    std::array<std::array<float, kFeat + 1>, kHidden> h1 {};     // stage 1: event features -> 24 tanh units (bias last)
    std::array<std::array<float, kHidden + 1>, kNumRoles> o1 {}; //          -> each drum (logistic)
    std::array<std::array<float, kCtx + 1>, kNumRoles> w2 {};    // stage 2: + the loop's context -> each drum
    std::array<float, kNumRoles> thr {};                          // write a drum when its stage-2 score >= this
};
inline const DrumModel& defaultDrumModel();
struct Options {
    int mode = -1;          // -1 auto, 0 drums, 1 pitched
    double bpm = 0;         // > 0: use this tempo
    double hintBpm = 0;     // the project tempo (only used for the message)
    const DrumModel* model = nullptr;   // (tests: another classifier; null = the built-in one)
};

constexpr double kMaxSeconds = 120.0;

// ---------------------------------------------------------------- reading
inline bool readFile(const juce::File& f, Loop& L, juce::String& err, double maxSeconds = kMaxSeconds) {
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f));
    if (!r || r->sampleRate < 4000 || r->sampleRate > 768000 || r->numChannels == 0) { err = "Can't read that file (try WAV, AIFF, FLAC, OGG or MP3)."; return false; }
    const juce::int64 total = r->lengthInSamples, lim = (juce::int64) (r->sampleRate * maxSeconds);
    const int n = (int) juce::jlimit<juce::int64>(0, lim, total);
    if (n < (int) (r->sampleRate * 0.05)) { err = "That file is too short to find a rhythm in."; return false; }
    L.sr = r->sampleRate; L.truncated = total > lim; L.name = f.getFileNameWithoutExtension(); L.x.assign((size_t) n, 0.0f);
    const int ch = (int) juce::jmin(8u, r->numChannels), chunk = 1 << 16; juce::AudioBuffer<float> b(ch, chunk);
    for (int pos = 0; pos < n; pos += chunk) {   // in chunks: never a giant temporary buffer
        const int k = juce::jmin(chunk, n - pos); r->read(&b, 0, k, pos, true, true);
        for (int c = 0; c < ch; ++c) { const float* s = b.getReadPointer(c); for (int i = 0; i < k; ++i) L.x[(size_t) (pos + i)] += s[i] / (float) ch; }
    }
    for (auto& v : L.x) if (!std::isfinite(v)) v = 0;
    return true;
}
// "Mr Mickens 140bpm ...", "Ghosty Beat Pattern 92bpm", "loop_128 BPM" -> the tempo (0 = none)
inline double bpmFromName(const juce::String& name) {
    const auto lo = name.toLowerCase(); int at = lo.indexOf("bpm");
    while (at > 0) {
        int e = at; while (e > 0 && lo[e - 1] == ' ') --e; int s = e; while (s > 0 && (juce::CharacterFunctions::isDigit(lo[s - 1]) || lo[s - 1] == '.')) --s;
        if (e > s) { const double b = lo.substring(s, e).getDoubleValue(); if (b >= 40 && b <= 300) return b; }
        at = lo.indexOf(at + 3, "bpm");
    }
    return 0;
}

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

// ---------------------------------------------------------------- DRUMS 2: what each event is (soft, several drums per event)
// STAGE 1  each event's features (below: the share of its new energy per band, each band's rise, decay, its level against the loop's
//          biggest of that band, the low end's frequency, the mid band's noisiness, the high band's drop over 40 ms, clap bursts, the
//          gap to the next hit, what was ringing before, the loop's own top end) -> a small network (24 tanh units) -> one score per
//          drum. One event can be a kick AND a hat AND an 808.
// STAGE 2  the loop teaches itself: the events are clustered by timbre (k-means, k <= 6: the loop's own "sounds"); each event then also
//          sees its cluster's mean scores, the mean scores of the events on the same 1/16 of the other bars (a repeated part votes),
//          and whether it sits on the loop's hat grid (8ths or 16ths, >= 50 % of its slots) -> a second logistic score per drum.
//          A drum is written when its score passes that drum's threshold.
// The weights were fitted offline on our own synthesised loops (all factory kits x trap / boom bap / house / break / drill, clean,
//          reverb, noise and lo-fi; BeatsTests "recreatetrain", docs/research/RECREATE-DRUMS.md) and are plain numbers below.
// VELOCITY each drum's own band energy in the event (see velocities()) against the loudest of that drum in the loop, cube root (the
//          engine plays velocity^1.5 as level).
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

inline std::array<float, kNumRoles> stage1(const DrumModel& M, const std::array<float, kFeat>& x) {
    std::array<float, kHidden> h {}; for (int j = 0; j < kHidden; ++j) h[(size_t) j] = std::tanh(dot<kFeat>(M.h1[(size_t) j], x));
    std::array<float, kNumRoles> p {}; for (int r = 0; r < kNumRoles; ++r) p[(size_t) r] = sigmoid(dot<kHidden>(M.o1[(size_t) r], h));
    return p;
}
struct DrumStages {
    std::vector<std::array<float, kFeat>> X; std::vector<std::array<float, kNumRoles>> p1, p2; std::vector<int> cluster; std::vector<std::array<float, kCtx>> C;
    int k = 0; float gridStrength = 0; int gridPer = 0;
};
// k-means on the timbre features (shares, decays, low / mid frequency, bursts): farthest-point start from the loudest event, k grows
// while the spread falls by >= 30 % (and each cluster keeps >= 2 events on average)
inline std::vector<int> clusterEvents(const std::vector<std::array<float, kFeat>>& X, const std::vector<float>& loud, int& kOut) {
    static constexpr int dims[] { 0, 1, 2, 3, 4, 10, 11, 12, 13, 14, 21, 22, 24 };
    const int n = (int) X.size(); std::vector<int> best((size_t) n, 0); kOut = 1; if (n < 4) return best;
    auto d2 = [&](const std::array<float, kFeat>& a, const std::array<float, kFeat>& b) { float s = 0; for (int d : dims) { const float q = a[(size_t) d] - b[(size_t) d]; s += q * q; } return s; };
    auto run = [&](int k, std::vector<int>& lab) {
        std::vector<std::array<float, kFeat>> c; int first = (int) (std::max_element(loud.begin(), loud.end()) - loud.begin()); c.push_back(X[(size_t) first]);
        while ((int) c.size() < k) { int far = 0; float fd = -1; for (int i = 0; i < n; ++i) { float m = 1e30f; for (auto& q : c) m = juce::jmin(m, d2(X[(size_t) i], q)); if (m > fd) { fd = m; far = i; } } c.push_back(X[(size_t) far]); }
        lab.assign((size_t) n, 0); float sse = 0;
        for (int it = 0; it < 25; ++it) {
            bool moved = false; sse = 0;
            for (int i = 0; i < n; ++i) { int bi = 0; float bd = 1e30f; for (int j = 0; j < k; ++j) { const float d = d2(X[(size_t) i], c[(size_t) j]); if (d < bd) { bd = d; bi = j; } } if (lab[(size_t) i] != bi) moved = true; lab[(size_t) i] = bi; sse += bd; }
            std::vector<std::array<float, kFeat>> s((size_t) k); std::vector<int> cnt((size_t) k, 0); for (auto& q : s) q.fill(0);
            for (int i = 0; i < n; ++i) { ++cnt[(size_t) lab[(size_t) i]]; for (int d : dims) s[(size_t) lab[(size_t) i]][(size_t) d] += X[(size_t) i][(size_t) d]; }
            for (int j = 0; j < k; ++j) if (cnt[(size_t) j]) for (int d : dims) c[(size_t) j][(size_t) d] = s[(size_t) j][(size_t) d] / (float) cnt[(size_t) j];
            if (!moved && it > 0) break;
        }
        return sse;
    };
    float sse = run(1, best);
    for (int k = 2; k <= juce::jmin(6, n / 2); ++k) { std::vector<int> lab; const float s = run(k, lab); if (s > 0.7f * sse) break; sse = s; best = lab; kOut = k; }
    return best;
}
inline void runStages(const std::vector<DrumEvent>& ev, const Result& R, const DrumModel& M, DrumStages& S, bool stage2 = true) {
    const int n = (int) ev.size(); const auto st = loopStats(ev);
    S.X.resize((size_t) n); S.p1.resize((size_t) n); std::vector<float> loud((size_t) n);
    for (int i = 0; i < n; ++i) { S.X[(size_t) i] = eventFeatures(ev[(size_t) i], st); float E = 0; for (auto d : ev[(size_t) i].delta) E += d; loud[(size_t) i] = E;
        S.p1[(size_t) i] = stage1(M, S.X[(size_t) i]); }
    S.cluster = clusterEvents(S.X, loud, S.k);
    // the loop's hat grid (from stage 1's hats)
    const double beat = 60.0 / juce::jmax(20.0, R.bpm); S.gridStrength = 0; S.gridPer = 0;
    for (int per : { 4, 2 }) {
        const int slots = juce::jmax(1, R.bars * 4 * per); std::vector<bool> took((size_t) slots, false); int got = 0;
        for (int i = 0; i < n; ++i) if (S.p1[(size_t) i][HatClosed] >= 0.5f) { const double p = (ev[(size_t) i].sec - R.origin) / beat * per; const long s = std::lround(p);
            if (std::abs(p - (double) s) < 0.25) { const int q = (int) (((s % slots) + slots) % slots); if (!took[(size_t) q]) { took[(size_t) q] = true; ++got; } } }
        const float g = (float) got / (float) slots; if (g >= 0.5f) { S.gridStrength = g; S.gridPer = per; break; } S.gridStrength = juce::jmax(S.gridStrength, g);
    }
    S.C.resize((size_t) n); S.p2.resize((size_t) n);
    for (int i = 0; i < n; ++i) {
        std::array<float, kNumRoles> cm {}, sm {}; int cc = 0, sc = 0; const double pi = (ev[(size_t) i].sec - R.origin) / beat * 4.0;
        for (int j = 0; j < n; ++j) {
            if (S.cluster[(size_t) j] == S.cluster[(size_t) i]) { ++cc; for (int r = 0; r < kNumRoles; ++r) cm[(size_t) r] += S.p1[(size_t) j][(size_t) r]; }
            if (j == i || R.bars < 2) continue;
            const double pj = (ev[(size_t) j].sec - R.origin) / beat * 4.0; double dq = std::fmod(std::abs(pi - pj), 16.0); dq = juce::jmin(dq, 16.0 - dq);
            if (dq < 0.3 && std::abs(pi - pj) > 8.0) { ++sc; for (int r = 0; r < kNumRoles; ++r) sm[(size_t) r] += S.p1[(size_t) j][(size_t) r]; }
        }
        auto& c = S.C[(size_t) i];
        for (int r = 0; r < kNumRoles; ++r) { c[(size_t) r] = logit(S.p1[(size_t) i][(size_t) r]) / 4; c[(size_t) (kNumRoles + r)] = logit(cm[(size_t) r] / (float) juce::jmax(1, cc)) / 4;
                                              c[(size_t) (2 * kNumRoles + r)] = logit(sc ? sm[(size_t) r] / (float) sc : S.p1[(size_t) i][(size_t) r]) / 4; }
        float on = 0; if (S.gridPer > 0) { const double p = (ev[(size_t) i].sec - R.origin) / beat * S.gridPer; on = std::abs(p - std::round(p)) < 0.2 ? 1.0f : 0.0f; }
        c[(size_t) (3 * kNumRoles)] = S.gridStrength; c[(size_t) (3 * kNumRoles + 1)] = on * S.gridStrength;
        for (int r = 0; r < kNumRoles; ++r) S.p2[(size_t) i][(size_t) r] = stage2 ? sigmoid(dot<kCtx>(M.w2[(size_t) r], c)) : S.p1[(size_t) i][(size_t) r];
    }
}
constexpr float kBothLookAlikes = 0.85f;
struct DrumTranscriber {
    const Loop& L; Result& R; std::vector<DrumEvent>& ev; const DrumModel& M; std::atomic<bool>* cancel;
    void run() {
        if (ev.empty()) return;
        DrumStages S; runStages(ev, R, M, S);
        for (size_t i = 0; i < ev.size(); ++i) { auto& e = ev[i]; e.roles = 0; e.cluster = S.cluster[i];
            for (int r = 0; r < kNumRoles; ++r) if (S.p2[i][(size_t) r] >= M.thr[(size_t) r]) e.roles |= 1u << r;
            // the look-alikes (snare / clap, closed / open hat, open hat / cymbal) are both written only when both are clear
            static constexpr int pairs[][2] { { Snare, Clap }, { HatClosed, HatOpen }, { HatOpen, Cymbal } };
            for (auto& pr : pairs) if (e.has(pr[0]) && e.has(pr[1]) && juce::jmin(S.p2[i][(size_t) pr[0]], S.p2[i][(size_t) pr[1]]) < kBothLookAlikes)
                e.roles &= ~(1u << (S.p2[i][(size_t) pr[0]] < S.p2[i][(size_t) pr[1]] ? pr[0] : pr[1])); }
        velocities();
        for (auto& e : ev) for (int r = 0; r < kNumRoles; ++r) if (e.has(r)) { R.hits.push_back({ e.sec, r, e.vel[(size_t) r] }); ++R.perRole[(size_t) r]; }
        if (std::getenv("A2M_DEBUG")) for (size_t i = 0; i < ev.size(); ++i) { std::printf("    ev %.3f k%d", ev[i].sec, S.cluster[i]); for (int r = 0; r < kNumRoles; ++r) std::printf(" %s %.2f/%.2f", roleName(r), S.p1[i][(size_t) r], S.p2[i][(size_t) r]); std::printf("\n"); }
        R.events = ev;
    }
    // each drum's own part of the event: kick = SUB + LOW, 808 = SUB, tom = LOW + MID, snare / clap / perc = MID + NOISE (new energy);
    // hats / cymbal = HIGH from 4 to 20 ms in (a kick's click under the hat is over by then)
    void velocities() {
        auto part = [](const DrumEvent& e, int r) -> float {
            switch (r) { case Kick: return e.delta[BSub] + e.delta[BLow]; case Bass: return e.delta[BSub]; case Tom: return e.delta[BLow] + e.delta[BMid];
                         case HatClosed: case HatOpen: case Cymbal: return e.hiLate; default: return e.delta[BMid] + e.delta[BNoise]; } };
        // (a hat / cymbal heard together with others is never louder than its loudest hit on its own, when it has >= 3 of those: a snare's
        //  or a clap's top end is not the hat's)
        std::array<float, kNumRoles> mx {}, alone {}; std::array<int, kNumRoles> nAlone {};
        for (auto& e : ev) for (int r = 0; r < kNumRoles; ++r) if (e.roles == (1u << r)) { alone[(size_t) r] = juce::jmax(alone[(size_t) r], part(e, r)); ++nAlone[(size_t) r]; }
        auto partOf = [&](const DrumEvent& e, int r) { const float v = part(e, r); const bool top = r == HatClosed || r == HatOpen || r == Cymbal; return top && e.roles != (1u << r) && nAlone[(size_t) r] >= 3 ? juce::jmin(v, alone[(size_t) r]) : v; };
        for (auto& e : ev) for (int r = 0; r < kNumRoles; ++r) if (e.has(r)) mx[(size_t) r] = juce::jmax(mx[(size_t) r], partOf(e, r));
        for (auto& e : ev) for (int r = 0; r < kNumRoles; ++r) if (e.has(r)) e.vel[(size_t) r] = juce::jlimit(0.12f, 1.0f, (float) std::cbrt(partOf(e, r) / juce::jmax(1e-20f, mx[(size_t) r])));
    }
};

// ---------------------------------------------------------------- analysis
struct Analyzer {
    const Loop& L; Options opt; std::atomic<bool>* cancel; std::atomic<float>* progress;
    std::vector<double> on; std::vector<float> strength;            // onsets (seconds) + their strength (peak of the first 30 ms)
    std::vector<float> ds; double fs = 11025;                        // ~11 kHz copy for YIN
    BandEnv bands; std::vector<DrumEvent> ev;                         // DRUMS: the five-band analysis and its events
    bool cancelled() const { return cancel && cancel->load(); }
    void prog(float p) const { if (progress) progress->store(p); }

    Result run() {
        Result R; const int n = (int) L.x.size(); const double sr = L.sr; prog(0.02f);
        float peak = 0; for (auto v : L.x) peak = juce::jmax(peak, std::abs(v));
        if (n < (int) (sr * 0.05) || peak < 1.0e-4f) { R.ok = true; return R; }   // silence / nothing: an empty result, no crash
        // onsets
        Sound snd; snd.rate = sr; snd.data = L.x;
        auto sl = rhythm::chop(snd, 0.55f, 1, 100000); if (cancelled()) return R; prog(0.35f);
        for (size_t i = 0; i < sl.size(); ++i) {
            const int s = sl[i]; float pk = 0; for (int k = s; k < juce::jmin(n, s + (int) (sr * 0.03)); ++k) pk = juce::jmax(pk, std::abs(L.x[(size_t) k]));
            if (pk < peak * 0.03f) continue;                         // (chop always returns 0 first: only if something starts there)
            if (i == 0) { float pre = 0; for (int k = 0; k < juce::jmin(n, (int) (sr * 0.004)); ++k) pre = juce::jmax(pre, std::abs(L.x[(size_t) k])); if (pre < pk * 0.05f && pk < peak * 0.1f) continue; }
            on.push_back(s / sr); strength.push_back(pk);
        }
        R.onsets = (int) on.size();
        if (on.empty()) { R.ok = true; return R; }
        // ~11 kHz copy for pitch work
        const int D = juce::jmax(1, (int) std::floor(sr / 11025.0)); fs = sr / D;
        { auto lp = filtered(L.x, { Biquad::make(false, fs * 0.42, sr), Biquad::make(false, fs * 0.42, sr) }); ds.resize((size_t) (n / D)); for (size_t i = 0; i < ds.size(); ++i) ds[i] = lp[i * (size_t) D]; }
        if (cancelled()) return R; prog(0.45f);
        R.voicedRatio = voicedRatio(peak);
        if (opt.mode != 1) { buildBands(L.x, sr, bands); if (cancelled()) return R; ev = drumEvents(bands, L.seconds()); }
        R.pitched = opt.mode == 1 || (opt.mode < 0 && R.voicedRatio >= 0.55f && !drumLike());
        if (R.pitched) { levelOnsets(); R.onsets = (int) on.size(); if (on.empty()) { R.ok = true; return R; } }
        tempo(R); if (cancelled()) return R; prog(0.6f);
        if (R.pitched) notes(R); else drums(R);
        if (cancelled()) return R;
        measureSwing(R); prog(1.0f); R.ok = true; return R;
    }

    // AUTO: a loop whose hits bring top end (hats, snares, claps: >= 6 % of a hit's new energy above 2 kHz) in >= 30 % of
    // them is drums, however tonal its kick / 808 is (a bassline's harmonics up there are a few per cent)
    bool drumLike() const {
        int n = 0, bright = 0; double top = 0; for (auto& e : ev) { double s = 0; for (auto d : e.delta) s += d; top = juce::jmax(top, s); }
        for (auto& e : ev) { double s = 0; for (auto d : e.delta) s += d; if (s < 1e-4 * top) continue; ++n; if ((e.delta[BNoise] + e.delta[BHigh]) >= 0.06 * s) ++bright; }
        if (std::getenv("A2M_DEBUG")) std::printf("    drumLike: %d of %d events bright\n", bright, n);
        return n >= 4 && bright >= 0.3 * n;
    }
    float voicedRatio(float peak) const {
        const int hop = (int) (fs * 0.02), W = (int) (fs * 0.085); int act = 0, voiced = 0; const float thr = peak * 0.02f;
        const int maxFrames = 600, total = juce::jmax(1, ((int) ds.size() - W) / juce::jmax(1, hop)), stride = juce::jmax(1, total / maxFrames);
        for (int f = 0; f < total; f += stride) {
            const int a = f * hop; float rms = 0; for (int k = a; k < a + W && k < (int) ds.size(); ++k) rms = juce::jmax(rms, std::abs(ds[(size_t) k]));
            if (rms < thr * 10) continue; ++act;
            float ap = 1; const float f0 = yin(ds.data() + a, juce::jmin(W, (int) ds.size() - a), fs, ap);
            if (f0 > 0 && ap < 0.2f) ++voiced;
            if (cancelled()) break;
        }
        return act > 0 ? (float) voiced / (float) act : 0.0f;
    }

    // comb score: how well the onsets sit on the 1/16 grid of this tempo (origin = the first onset or 0)
    // (minus what randomly placed onsets would score, so a finer grid at a faster tempo gets no free points)
    // Swung loops: the off-beat 8th may sit late (sw beats), so the grid per beat is { 0, 1/4, 1/2 (+sw), 3/4 }; the best swing counts.
    double score(double bpm, double origin) const {
        const double beat = 60.0 / bpm, sig = 0.010; double best = -1;
        for (double sw : { 0.0, 0.04, 0.08, 0.12, 0.167 }) {
            const double pts[] { 0.0, 0.25, 0.5 + sw, 0.75, 1.0 }; double s = 0, w = 0;
            for (size_t i = 0; i < on.size(); ++i) { const double b = (on[i] - origin) / beat; const double fr = b - std::floor(b); double d = 1;
                for (double q : pts) d = juce::jmin(d, std::abs(fr - q)); d *= beat; s += strength[i] * std::exp(-(d * d) / (sig * sig)); w += strength[i]; }
            const double sc = w > 0 ? s / w - 4.0 * sig * std::sqrt(juce::MathConstants<double>::pi) / beat : 0;
            best = juce::jmax(best, sc);
        }
        return best;
    }
    // pitched material: onsets from the level (5 ms RMS, smoothed): a rise of >= 6 dB within 20 ms out of a dip, min 60 ms apart
    void levelOnsets() {
        on.clear(); strength.clear(); const int n = (int) L.x.size(); const double sr = L.sr; const int fr = juce::jmax(1, (int) (sr * 0.005)), F = n / fr; if (F < 4) return;
        // RMS over the 25 ms ending at each 5 ms frame (longer than a low note's period, so the waveform itself never reads as onsets)
        std::vector<double> cum((size_t) n + 1, 0.0); for (int i = 0; i < n; ++i) cum[(size_t) i + 1] = cum[(size_t) i] + (double) L.x[(size_t) i] * L.x[(size_t) i];
        const int win = juce::jmax(fr, (int) (sr * 0.025));
        std::vector<float> db((size_t) F); float pk = 1e-9f;
        for (int f = 0; f < F; ++f) { const int z = juce::jmin(n, (f + 1) * fr), a = juce::jmax(0, z - win); const float r = (float) std::sqrt((cum[(size_t) z] - cum[(size_t) a]) / juce::jmax(1, z - a)); pk = juce::jmax(pk, r); db[(size_t) f] = 20.0f * std::log10(r + 1e-9f); }
        const float floorDb = 20.0f * std::log10(pk) - 40.0f; int last = -1000;
        if (db[0] >= floorDb + 20) { on.push_back(0.0); float m = 0; for (int k = 0; k < juce::jmin(n, (int) (sr * 0.03)); ++k) m = juce::jmax(m, std::abs(L.x[(size_t) k])); strength.push_back(m); last = 0; }   // it starts sounding right away
        for (int f = 1; f < F; ++f) {
            if (db[(size_t) f] < floorDb + 6) continue;
            float lo = db[(size_t) f]; for (int k = juce::jmax(0, f - 6); k < f; ++k) lo = juce::jmin(lo, juce::jmax(db[(size_t) k], floorDb));
            if (db[(size_t) f] - lo < 6.0f || db[(size_t) f] < db[(size_t) (f - 1)]) continue;
            if (f - last < (int) (0.06 / 0.005)) continue;
            int s0 = juce::jmax(0, (f - 6) * fr); { int mn = s0; float mv = 1e9f; for (int k = juce::jmax(0, f - 6); k <= f; ++k) if (db[(size_t) k] < mv) { mv = db[(size_t) k]; mn = k * fr; } s0 = mn; }   // the quietest frame before the rise
            { float lvl = 0; for (int k = s0; k < juce::jmin(n, (f + 1) * fr); ++k) lvl = juce::jmax(lvl, std::abs(L.x[(size_t) k])); int k = s0; while (k < (f + 1) * fr - 1 && std::abs(L.x[(size_t) k]) < lvl * 0.2f) ++k; s0 = k; }   // the first sample that's clearly sounding
            on.push_back(s0 / sr); float m = 0; for (int k = s0; k < juce::jmin(n, s0 + (int) (sr * 0.03)); ++k) m = juce::jmax(m, std::abs(L.x[(size_t) k])); strength.push_back(m); last = f;
        }
    }
    void tempo(Result& R) {
        const double len = L.seconds(); const double named = bpmFromName(L.name);
        R.origin = on.front() < 0.05 ? 0.0 : on.front();
        if (opt.bpm > 0) { R.bpm = juce::jlimit(20.0, 999.0, opt.bpm); R.bpmFrom = "typed"; }
        else if (named > 0) { R.bpm = named; R.bpmFrom = "file name"; R.origin = 0; }
        else {
            double best = 80, bs = -1;
            for (double b = 80; b < 160; b += 0.05) { const double s = score(b, R.origin); if (s > bs) { bs = s; best = b; } }
            for (double b = best - 0.06; b <= best + 0.06; b += 0.002) { const double s = score(b, R.origin); if (s > bs) { bs = s; best = b; } }
            R.bpm = best; R.bpmFrom = "onsets";
            // a loop of whole bars (1..16) whose tempo the onsets agree with wins: its tempo is exact
            double lb = 0, ls = -1;
            for (int bars : { 1, 2, 4, 8, 16 }) { double b = bars * 240.0 / len; while (b < 80) b *= 2; while (b >= 160) b /= 2; const double s = score(b, 0.0); if (s > ls) { ls = s; lb = b; } }
            if (lb > 0 && ls >= 0.8 * bs) { R.bpm = lb; R.bpmFrom = "loop length"; R.origin = 0; }
        }
        // bars: a loop -> its length; otherwise up to the last onset
        const double beatSec = 60.0 / R.bpm, loopBeats = (len - R.origin) / beatSec, lastBeat = (on.back() - R.origin) / beatSec;
        double bars = std::abs(loopBeats / 4.0 - std::round(loopBeats / 4.0)) < 0.06 && R.bpmFrom != "file name" ? std::round(loopBeats / 4.0) : std::floor(lastBeat / 4.0 + 1e-6) + 1;
        if (R.bpmFrom == "file name") { const double lb = loopBeats / 4.0; if (std::abs(lb - std::round(lb)) < 0.03) bars = std::round(lb); }   // an exact loop of that tempo (no tail)
        int b = 1; while (b < 8 && b < bars - 0.25) b *= 2; R.bars = b;
    }

    void drums(Result& R) { DrumTranscriber T { L, R, ev, opt.model ? *opt.model : defaultDrumModel(), cancel }; T.run(); }

    void notes(Result& R) {
        if (std::getenv("A2M_DEBUG")) { std::printf("    onsets:"); for (double t : on) std::printf(" %.3f", t); std::printf("\n"); }
        const int n = (int) L.x.size(); const double sr = L.sr; float gpk = 0; for (auto v : L.x) gpk = juce::jmax(gpk, std::abs(v));
        const int W = (int) (fs * 0.085), hop = juce::jmax(1, (int) (fs * 0.015));
        for (size_t i = 0; i < on.size(); ++i) {
            if (cancelled()) return; prog(0.6f + 0.4f * (float) i / (float) on.size());
            const double t0 = on[i], t1 = i + 1 < on.size() ? on[i + 1] : n / sr;
            // the note's end: the level falls 30 dB under its peak (or the next onset)
            const int a = (int) (t0 * sr), z = (int) (t1 * sr); float pk = 0; for (int k = a; k < juce::jmin(z, a + (int) (sr * 0.05)); ++k) pk = juce::jmax(pk, std::abs(L.x[(size_t) k]));
            int end = z; { const int fr = juce::jmax(1, (int) (sr * 0.005)); for (int k = a + (int) (sr * 0.03); k + fr <= z; k += fr) { float m = 0; for (int j = 0; j < fr; ++j) m = juce::jmax(m, std::abs(L.x[(size_t) (k + j)])); if (m < pk * 0.0316f) { end = k; break; } } }
            // pitch frames inside the note; consecutive frames on the same note = one note (a legato change starts a new one)
            int curMidi = -1; double curStart = t0;
            auto flush = [&](double at) { if (curMidi >= 0 && at - curStart > 0.02) R.notes.push_back({ curStart, at - curStart, curMidi, juce::jlimit(0.05f, 1.0f, pk / juce::jmax(1e-6f, gpk)) }); };
            for (double ft = t0 + 0.02; ft + W / fs <= end / sr + 0.03 && ft < t1; ft += hop / fs) {
                const int fa = (int) (ft * fs); if (fa + W >= (int) ds.size()) break;
                float ap = 1; const float f0 = yin(ds.data() + fa, W + (int) (fs / 30.0) + 2 < (int) ds.size() - fa ? W + (int) (fs / 30.0) + 2 : (int) ds.size() - fa, fs, ap);
                if (f0 <= 0 || ap >= 0.25f) continue;
                const int m = juce::jlimit(0, 127, midiOf(f0));
                if (m != curMidi) {
                    // a change needs a second frame to agree (one odd frame is a glitch)
                    float ap2 = 1; const int fb = fa + hop; if (fb + W >= (int) ds.size()) break;
                    const float f1 = yin(ds.data() + fb, juce::jmin((int) ds.size() - fb, W + (int) (fs / 30.0) + 2), fs, ap2);
                    if (f1 <= 0 || ap2 >= 0.25f || juce::jlimit(0, 127, midiOf(f1)) != m) continue;
                    if (curMidi >= 0) flush(ft); curMidi = m; curStart = ft - t0 < 0.05 ? t0 : ft;   // the first pitch of a note starts at its onset
                }
            }
            flush(end / sr);
        }
    }

    void measureSwing(Result& R) const {
        // the off-beat 8ths' average delay, as the usual swing percentage (50 = straight, 66 = triplet)
        const double beatSec = 60.0 / R.bpm; double sum = 0; int k = 0;
        for (double t : on) { const double p = (t - R.origin) / beatSec * 2.0; const double frac = p - std::floor(p); const long idx = (long) std::floor(p);
            if ((idx & 1) == 0 && frac > 0.95) continue; if ((idx & 1) == 1 || frac > 0.5) { const double off = std::fmod((t - R.origin) / beatSec, 1.0); if (off > 0.4 && off < 0.75) { sum += off; ++k; } } }
        R.swing = k >= 2 ? juce::jlimit(50.0, 75.0, 100.0 * sum / k) : 50.0;
    }
};

inline Result analyse(const Loop& L, const Options& o, std::atomic<bool>* cancel = nullptr, std::atomic<float>* progress = nullptr) {
    Analyzer A { L, o, cancel, progress, {}, {}, {}, 11025, {}, {} }; return A.run();
}

// ---------------------------------------------------------------- the built-in classifier (BeatsTests "recreatetrain" prints this block)
inline const DrumModel& defaultDrumModel() {
    // MODEL BEGIN
    static const DrumModel M = [] { DrumModel m;
        m.h1 = {{
            {{ 0.2706f, 0.7098f, -0.0553f, -0.0926f, 0.1981f, -0.0187f, 0.5506f, 0.1029f, -0.4583f, 0.0356f, 0.1710f, -0.2575f, 0.3450f, -0.2231f, 0.0279f, 0.0238f, 0.1895f, -0.2538f, -0.2002f, 0.1398f, -0.3399f, -0.0635f, -0.4194f, 0.2760f, 0.7964f, -0.3585f, 0.0118f, -0.0353f, 0.4772f, 0.5511f, -0.0034f, 0.2980f, 0.0028f }},
            {{ 0.7346f, 0.5544f, 0.0976f, -0.3842f, -0.5063f, -0.5003f, 0.1035f, -0.2753f, -0.0108f, 0.1728f, 0.5987f, 0.6513f, -0.1146f, -0.2190f, 0.0004f, 0.1352f, -0.0907f, -0.2605f, -0.5879f, -0.5198f, -0.3477f, 0.3343f, -0.2011f, 0.1595f, -0.2117f, 0.0892f, 0.6329f, 0.0451f, -0.0381f, 0.3864f, 0.0159f, 0.4884f, -0.4541f }},
            {{ 0.7530f, 0.2223f, -0.2069f, 0.5299f, 0.0440f, 0.1705f, -0.0013f, -0.1711f, -0.1129f, -0.0135f, 0.4997f, 0.1449f, -0.3972f, 0.5277f, 0.1285f, 0.6540f, 0.0932f, -0.5091f, 0.1550f, 0.3200f, 0.1787f, 0.3850f, 0.0096f, -0.6439f, 0.1488f, 0.3100f, -0.3634f, -0.3124f, -0.0773f, 0.2279f, -0.4595f, 0.1974f, -0.2820f }},
            {{ -0.2504f, -0.2209f, -0.3466f, 0.0428f, -0.6116f, -0.0731f, 0.4508f, -0.1486f, 0.1086f, 0.2521f, -0.2553f, -0.0112f, 0.1218f, 0.0608f, 0.5647f, 0.4999f, 0.4558f, 0.0742f, 0.1762f, -0.9974f, 0.3367f, -0.2206f, 0.5300f, 2.4319f, -0.1852f, -0.5923f, -0.0398f, 0.5306f, -0.0804f, 0.4122f, 0.3848f, 0.1430f, -0.0254f }},
            {{ -0.0981f, -0.1841f, 1.6272f, -0.3969f, -0.6245f, -0.0743f, 0.2665f, 0.1557f, 0.2063f, -0.0110f, -0.3584f, -0.1600f, 0.4665f, 0.1096f, -0.2500f, -0.2454f, 0.0722f, 1.6502f, 0.3598f, -0.0570f, 0.6662f, 0.2339f, 0.2877f, -0.2351f, -0.0020f, -0.0206f, 0.1785f, 0.0236f, -0.0634f, -0.0036f, 0.4194f, 0.2220f, -0.0305f }},
            {{ -0.1135f, 0.5746f, -0.0318f, -0.0157f, 0.0621f, -0.4376f, -0.0870f, 0.2744f, -0.0894f, -0.2277f, 0.2578f, 0.2562f, -0.1266f, -0.2376f, -0.2590f, -0.3227f, 0.6386f, 0.0477f, -0.0679f, 0.3864f, 0.2761f, 0.2070f, -0.1732f, -0.1384f, 0.2780f, 0.0984f, 0.5270f, -0.0269f, -0.0085f, 0.0405f, 0.0866f, 0.1117f, -0.3416f }},
            {{ 0.5844f, -0.1779f, -1.1766f, 0.5566f, 0.2578f, -0.0186f, 0.1784f, -0.3193f, 0.4901f, -0.0949f, -0.3766f, -0.2500f, -0.1089f, 0.0049f, 0.0132f, -0.3923f, -1.1195f, -1.7575f, 0.4793f, -0.1555f, -0.5439f, -0.2143f, 0.7944f, 0.1093f, -0.3390f, 0.0254f, 0.0632f, 0.0781f, 0.1709f, -0.0116f, -0.0287f, 0.1778f, -0.0044f }},
            {{ -0.2759f, -1.4998f, 0.0717f, 0.4836f, -0.1184f, 0.1730f, -1.1126f, -0.3370f, 0.0072f, -0.0137f, -0.0810f, 0.3962f, 0.0555f, 0.3433f, -0.1250f, 0.1775f, -1.2269f, -0.1864f, -0.1310f, 0.2688f, -0.0998f, -0.0880f, 1.1437f, 0.0660f, -0.1349f, -0.4577f, -0.3075f, -0.0011f, -0.2241f, 0.5142f, -0.5257f, -0.3134f, -0.5944f }},
            {{ 0.1935f, 1.5757f, -0.0735f, 0.9453f, 0.0732f, 0.1500f, -0.0266f, 0.0959f, 0.3269f, 0.1215f, 0.1763f, 0.6346f, 0.0955f, 0.1199f, -0.3591f, -0.0756f, 1.0654f, -0.1984f, -0.0980f, -1.3781f, -0.2174f, 0.2617f, 0.1447f, -0.2530f, 0.0021f, -0.1100f, -0.1912f, -0.0094f, 0.0895f, 0.0922f, 0.2265f, 0.2783f, -0.2680f }},
            {{ -0.1060f, -0.6909f, 0.1814f, -0.2573f, 0.4235f, 0.1449f, 0.2322f, 0.1155f, -0.5416f, -0.0389f, -0.9953f, -0.6480f, 0.1103f, -0.0587f, 0.3392f, 0.0131f, -0.7184f, 0.1216f, -0.1963f, 0.2657f, -0.2232f, 0.1552f, 0.2073f, 0.4890f, -0.1830f, 0.7281f, -0.3045f, -0.1289f, 0.5083f, 0.1903f, -0.1176f, -0.2032f, -0.1072f }},
            {{ -0.0640f, -0.0039f, 0.0931f, 0.1307f, 1.0707f, 0.0120f, -0.0944f, 0.2227f, -0.1885f, 0.1391f, -0.2370f, -0.2966f, -0.5555f, 0.1516f, 0.3373f, -0.2885f, -0.1955f, -0.1308f, -0.5502f, 0.6997f, -0.0566f, 0.1697f, -0.0996f, -1.2570f, -0.0599f, 0.2198f, 0.0034f, -0.7563f, 0.0446f, 0.0312f, -0.3064f, -0.3843f, 0.7740f }},
            {{ 0.1444f, -0.0246f, 0.3659f, -0.5211f, -0.5551f, -0.0243f, 0.5271f, 0.7664f, -0.3383f, -0.0944f, -0.2087f, 0.5224f, 0.2698f, -0.3212f, 0.0490f, 0.0514f, 0.0040f, 0.3117f, -0.4123f, -0.5116f, -0.1834f, 0.0357f, -1.1347f, 0.5292f, 0.1165f, 0.5774f, -0.1269f, 0.0052f, -0.1050f, -0.1969f, 0.0542f, 0.1223f, -0.5610f }},
            {{ 0.5219f, 0.1987f, 0.2360f, -0.5252f, -0.5804f, -0.1124f, 0.0581f, -0.3269f, 0.1532f, 0.0105f, 0.5044f, 0.0991f, 0.1100f, -0.3677f, -0.0131f, -0.3438f, -0.0802f, 0.0713f, 0.7936f, 0.9445f, 0.3040f, -0.4930f, 0.2301f, -0.0287f, -0.0679f, -1.4493f, 0.1272f, -0.1773f, -0.0726f, 0.0185f, 0.2562f, 0.3507f, -0.5398f }},
            {{ 1.0462f, 0.0426f, -0.3969f, -0.5990f, -0.4411f, 0.1360f, 0.3455f, 0.1671f, -0.1446f, 0.1382f, 0.3837f, -1.3023f, -0.1372f, -0.1632f, -0.0207f, 1.6416f, 0.2956f, 0.1417f, 0.4530f, 0.5935f, 0.8176f, -0.3669f, -0.0506f, 0.0264f, -0.0286f, 0.4262f, -0.0497f, 0.2060f, 0.0203f, -0.1186f, 0.0648f, 0.4822f, -0.1424f }},
            {{ 0.0615f, 0.6449f, -0.2626f, -0.2450f, 0.2634f, -0.3412f, 0.3940f, 0.2122f, 0.0887f, -0.1906f, 0.4262f, 0.0737f, 0.0201f, -0.9061f, -0.5148f, 0.1564f, 0.7115f, 0.0915f, -0.5009f, -0.0036f, -0.0096f, -0.0740f, -0.1322f, 0.8520f, -0.0578f, -0.3949f, 0.4071f, -0.1263f, 0.0635f, -0.3644f, -0.1214f, -0.1652f, -0.1460f }},
            {{ 0.1087f, -0.0990f, 1.1284f, 0.0296f, -0.4121f, -0.0120f, -0.1947f, -0.0106f, 0.2569f, 0.1367f, 0.4234f, -0.0591f, 0.1279f, -0.1186f, -0.4883f, -0.1307f, 0.2261f, 1.2835f, 1.6409f, 1.3926f, 1.1240f, 0.1015f, 0.8224f, 0.1124f, 0.2585f, -0.0393f, -0.0222f, 0.0703f, 0.0884f, 0.5524f, 0.5743f, 0.5531f, -0.4746f }},
            {{ 0.2104f, 0.3571f, -0.3241f, -0.0466f, -0.1846f, 0.3169f, -0.0029f, -0.3098f, -0.0221f, 0.0906f, 0.3369f, -0.4434f, 0.0282f, 0.2522f, 0.4394f, 0.3605f, 0.1325f, -0.2767f, 0.2856f, 0.3543f, 0.6777f, -0.4982f, -0.1524f, -0.4962f, -0.1505f, -0.6747f, -0.2509f, 0.0914f, -0.1116f, -0.3766f, 0.1029f, 0.2717f, 0.2743f }},
            {{ -0.2279f, -0.0348f, -0.0903f, 0.8216f, 0.4502f, 0.0452f, 0.5219f, 0.0396f, 0.3729f, -0.0008f, -0.3058f, -0.0272f, -0.4174f, 1.0919f, 0.3708f, 0.4229f, 0.7124f, 0.6772f, 1.4353f, 0.7735f, 0.8958f, -0.4379f, 0.0726f, -0.7153f, 0.0651f, -0.2995f, 0.0474f, -0.1907f, 0.0291f, 0.1655f, 0.1387f, -0.1917f, 0.3186f }},
            {{ -0.1065f, -0.0709f, -0.1142f, -0.0014f, -0.2964f, -0.0870f, 0.2658f, -0.0057f, -0.1087f, -0.0213f, -0.1141f, -1.0039f, -0.5521f, -0.1557f, -0.0971f, -0.0705f, -0.0890f, -0.1649f, 0.1574f, 0.0116f, -0.3186f, 0.7919f, -0.2102f, 0.0649f, 0.3714f, 0.4740f, 0.0700f, 0.0634f, 0.3679f, -0.0666f, -0.2203f, 0.1251f, 0.4802f }},
            {{ 0.2592f, 0.3726f, 0.1288f, -0.1133f, 0.0006f, 0.0773f, -0.1486f, -0.0304f, -0.1789f, -0.0916f, 0.0377f, 0.2201f, -0.0680f, 0.3727f, 1.4464f, -0.3593f, -0.4965f, -0.1655f, -0.4827f, 0.0125f, -0.5432f, -0.0072f, -0.2591f, -2.4554f, -0.1106f, -1.2765f, -0.1622f, -0.4453f, 0.0096f, -0.2486f, -0.2548f, 0.1897f, 0.2218f }},
            {{ -0.0788f, -0.0781f, 0.0992f, 0.2166f, 0.8638f, 0.1162f, -0.1168f, -0.2695f, 0.2730f, -0.0434f, -0.0216f, 0.2145f, 0.0395f, 0.0923f, 0.0828f, 0.0611f, -0.4329f, -0.0702f, -0.0121f, 1.7879f, 0.1376f, -0.2338f, 0.4993f, -0.1133f, -0.2552f, -0.5928f, -0.1221f, -0.6109f, -0.1167f, 0.0667f, -0.5181f, -0.3391f, -0.1788f }},
            {{ 0.3362f, 0.4691f, 0.0252f, 0.5032f, 0.1744f, 0.0307f, 0.3560f, -0.0017f, 0.0568f, -0.4777f, -0.3529f, -0.1188f, -0.1351f, 0.2989f, -0.5701f, -0.5489f, -0.4831f, -0.0956f, 0.7527f, 0.5991f, -0.1192f, 0.2786f, 0.0091f, -1.7628f, 0.0501f, 0.3219f, 0.1565f, -0.2672f, 0.1638f, 0.0030f, -0.2337f, 0.0549f, 0.0943f }},
            {{ -0.1584f, 0.3614f, -0.5557f, -0.2808f, -0.2005f, -0.0755f, -0.3870f, 0.4538f, 0.4273f, -0.3054f, 0.6741f, 0.3068f, -0.7735f, -0.3337f, 0.0542f, 0.3467f, 1.1668f, -0.0569f, -0.0539f, -0.3230f, 0.3361f, 0.1107f, -0.6123f, 0.3562f, 0.1372f, 1.0604f, -0.0359f, -0.1259f, -0.2649f, 0.2771f, 0.3477f, -0.3873f, -0.4365f }},
            {{ -0.1914f, -0.2170f, -0.0379f, -0.0220f, -0.0565f, 0.3534f, 0.6014f, -0.1871f, 0.2254f, 0.2875f, -0.0856f, 0.2470f, 0.0334f, -0.0729f, 0.2273f, 0.0483f, 0.1569f, 0.1249f, 0.0562f, -0.3373f, 0.2272f, 0.5688f, -0.2522f, 1.8759f, -0.2131f, -0.5289f, -0.4697f, 0.3351f, -0.0277f, 0.2579f, 0.1266f, -0.0428f, -0.1336f }},
        }};
        m.o1 = {{
            {{ 0.5020f, -1.7653f, 0.2207f, 0.6855f, 0.8850f, 0.3835f, -1.2900f, -0.5789f, 0.2035f, -0.2592f, -0.0510f, -0.4765f, 0.5891f, 2.3009f, -0.2299f, 0.8994f, 1.3331f, -0.8159f, -1.2928f, -0.1915f, -0.7989f, -0.9314f, 0.6795f, 0.2212f, 0.4480f }},
            {{ -1.1350f, -0.4909f, 0.5595f, -0.3755f, 0.3236f, -0.0702f, 1.7567f, -2.1172f, 1.1715f, -0.3743f, 0.5352f, -0.1469f, 0.1480f, -1.5482f, -0.0545f, -0.8793f, 0.3875f, 0.9484f, -1.4974f, -0.0247f, -1.3806f, 1.3267f, -0.5313f, 0.3960f, -0.4494f }},
            {{ 0.4263f, -0.1769f, 0.3890f, 0.3170f, 1.4940f, -0.0841f, 0.5405f, 0.7453f, -0.4190f, -1.4032f, -0.4985f, -1.1426f, 0.5867f, 1.0727f, -1.5988f, 2.6411f, 1.1416f, 0.9155f, -0.3240f, -1.1803f, 0.3372f, 0.9186f, -1.0941f, 0.5543f, -0.1539f }},
            {{ -0.2585f, -0.1127f, 0.5710f, 1.3203f, 0.0166f, 0.6899f, -0.4280f, 0.5368f, -1.2553f, 0.2062f, 1.4119f, -0.8202f, -0.7502f, 0.1397f, 1.0883f, 0.2213f, 0.6631f, 1.5248f, -0.6318f, -2.0210f, 0.7288f, -0.8661f, -0.7326f, 1.0779f, -0.3118f }},
            {{ 0.2357f, -0.2726f, 0.8540f, -1.8685f, 0.2552f, 0.8485f, -1.0577f, 1.1231f, -0.5190f, 0.2976f, 0.2097f, 0.6025f, 0.8963f, -0.0594f, 0.4123f, -1.0417f, -0.2995f, -1.4100f, 0.2345f, 1.3081f, 1.3688f, 1.8111f, -0.0428f, -1.5757f, -0.8472f }},
            {{ -0.4064f, 1.1881f, 0.9570f, -0.0937f, -0.8988f, 1.2605f, 0.2002f, -0.9996f, 1.8304f, -1.8452f, 0.0019f, 0.1057f, 0.1209f, -0.7008f, 0.6949f, -0.7002f, -0.0541f, 0.0596f, -0.4141f, 0.7075f, -0.6025f, 0.2132f, 0.8032f, -0.5340f, -0.6403f }},
            {{ -1.8356f, -0.3635f, -0.0321f, -2.0776f, 1.9196f, 0.3476f, -1.3520f, -0.0555f, -0.0510f, 0.4382f, -1.7257f, 1.0429f, 0.8950f, -0.2564f, 0.1894f, -0.0519f, -0.2588f, -0.9091f, 0.2305f, 0.5690f, -0.4283f, -0.3681f, -1.8660f, -0.7402f, -0.1272f }},
            {{ -0.5063f, 0.1309f, 1.9656f, -0.1262f, -0.0248f, 0.1950f, -0.6706f, 0.0526f, 0.5688f, -0.0339f, -0.0373f, 0.4651f, -0.2524f, 0.9218f, -0.8165f, 0.1215f, 0.6743f, 1.1096f, -0.2988f, 0.8581f, 0.1303f, 0.1308f, 0.4152f, -0.4080f, -0.6409f }},
            {{ -0.1833f, 0.8993f, 0.3507f, -0.3838f, -0.2210f, 0.9926f, 0.6619f, -1.3052f, -0.9829f, 0.1846f, -0.3399f, 1.6744f, 1.8294f, 0.6557f, -0.3159f, 1.5001f, 0.3053f, -0.9014f, -0.7591f, -0.1814f, 0.3917f, -0.4363f, 0.5961f, -0.5676f, -0.8256f }},
        }};
        m.w2 = {{
            {{ 3.6029f, -0.3024f, 0.5888f, 0.2695f, -0.3315f, -0.2851f, 0.4510f, 0.5042f, 0.6879f, 0.8867f, 1.1198f, -0.3917f, -0.7865f, 1.0138f, 0.4182f, 0.0828f, 0.3762f, -0.5721f, 1.2601f, -0.7096f, -0.2466f, 0.7517f, -0.3403f, 0.5685f, -0.0083f, 0.0224f, -0.8160f, -0.0145f, 0.4828f, 1.4625f }},
            {{ -0.3404f, 3.0752f, 0.5409f, 0.6096f, 0.3141f, -0.1905f, -0.0203f, 0.2040f, -0.1195f, -0.4961f, 2.0538f, -1.0196f, -0.0393f, -1.0504f, 0.1482f, 0.3659f, -0.2061f, 0.6287f, 0.1456f, 2.0802f, -0.0089f, -0.4443f, -0.7542f, 0.0056f, 0.5763f, -0.3767f, 0.5618f, 1.9371f, -1.2584f, -0.2969f }},
            {{ 0.5726f, 0.1927f, 3.9175f, 0.1157f, 0.0866f, 0.0828f, -0.2573f, -0.0817f, -0.1131f, -0.1744f, 0.0662f, 0.9654f, -0.1863f, 0.0739f, -0.4195f, 0.9377f, -0.8740f, 0.2217f, -0.3654f, 0.0736f, 1.8485f, 1.3233f, 0.2164f, 0.1917f, -0.7082f, 0.3280f, 0.0611f, -1.2075f, -1.3119f, 0.9830f }},
            {{ 0.0519f, 0.2241f, 0.2616f, 3.9074f, -0.5300f, -0.1499f, -0.2182f, -0.1731f, -0.3442f, 0.1258f, -0.6669f, -0.0329f, -0.1738f, 1.3670f, 0.6599f, 0.4031f, 0.2115f, -0.2456f, -0.1909f, -0.1793f, -0.3237f, 1.6070f, -0.3997f, 0.0909f, -0.0261f, 0.4153f, -0.2093f, -0.9683f, 2.1854f, -0.6512f }},
            {{ 0.0773f, 0.2515f, 0.1728f, -0.7688f, 3.4197f, 0.0311f, 0.3410f, 0.4324f, -0.0236f, 0.3473f, -0.7591f, 0.4405f, 0.4456f, 0.4170f, -0.2720f, 0.1430f, -0.4953f, 0.6334f, -0.2910f, -0.5449f, -0.8366f, 0.4367f, 1.3325f, -0.0030f, 0.3419f, -0.1322f, -0.1725f, -0.6932f, 2.3416f, -0.9834f }},
            {{ -0.3727f, -0.0695f, -1.1594f, -0.6686f, -0.3073f, 3.5516f, -0.3147f, -0.2558f, 0.8130f, -0.0660f, 1.0043f, -0.5288f, -0.6191f, -0.1595f, 1.3141f, -0.1449f, 0.6995f, -0.3357f, 0.2223f, 0.4828f, 1.3602f, 0.4400f, 0.4173f, 0.0195f, 0.7046f, -0.1746f, -0.2863f, 0.1193f, 0.4809f, 0.1447f }},
            {{ 0.4039f, 0.2579f, -0.4699f, -0.8965f, -0.8666f, -0.3104f, 3.3798f, -0.0722f, -0.0836f, 0.4948f, -1.6454f, 0.9578f, -0.0942f, 0.5389f, -0.2018f, 0.5595f, -0.2967f, -0.0583f, -0.1848f, 0.0453f, 0.1874f, 1.0914f, 1.1635f, 0.4468f, 2.4340f, 0.3025f, -0.1417f, -0.2589f, 1.0683f, 0.3498f }},
            {{ 0.7897f, 0.4697f, 0.8495f, 0.1128f, 0.5812f, 0.1447f, -0.6401f, 2.7248f, -0.2592f, -0.5436f, 0.9734f, -0.8299f, -0.6770f, -0.2505f, 1.5914f, 0.0983f, 0.2360f, -0.0874f, 0.0895f, -0.8203f, -0.3141f, 0.3732f, -0.0106f, 0.0938f, 0.9676f, -0.7190f, 0.9227f, -0.3606f, 1.0685f, -0.5229f }},
            {{ 1.2007f, 0.7485f, -0.1181f, -0.5871f, 0.0682f, 0.0989f, 0.1317f, 0.4984f, 2.6631f, -1.6877f, -2.1823f, 1.3783f, 0.1763f, -0.8313f, 0.8056f, 0.4682f, -1.6517f, 2.0032f, -0.7001f, 0.3754f, -0.4113f, -0.0202f, 0.2128f, 0.0056f, -0.6792f, -0.2400f, 1.8148f, -0.7422f, 1.3218f, -1.1140f }},
        }};
        m.thr = {{ 0.575f, 0.675f, 0.600f, 0.550f, 0.700f, 0.675f, 0.650f, 0.700f, 0.675f }};
        return m; }();
    // MODEL END
    return M;
}

// ---------------------------------------------------------------- writers
struct WriteOpts { int res = 1; float quantize = 1.0f; bool useHits = false; uint32_t roles = 0xFFFFFFFFu; };   // roles: the drums to write (bit per DrumRole; the panel's on / off)
// role -> track, from the kit's tracks (roleOf + the sound's category); -1 = nothing fits
inline std::array<int, kNumRoles> mapRoles(const std::array<Track, kTracks>& tracks, const std::function<juce::String(const juce::String&)>& categoryOf) {
    std::array<int, kNumRoles> first; first.fill(-1); int kick = -1, snare = -1, clap = -1, hat = -1, open = -1, tom = -1, perc = -1, bass = -1, cym = -1;
    for (int t = 0; t < kTracks; ++t) {
        const auto& tr = tracks[(size_t) t]; if (tr.sound.isEmpty()) continue; const auto cat = categoryOf ? categoryOf(tr.sound) : juce::String(); const auto nm = tr.sound.toLowerCase();
        const auto r = roleOf(cat, tr.sound);
        auto take = [&](int& v) { if (v < 0) v = t; };
        if (cat == "Clap" || nm.contains("clap")) take(clap); else if (r == RoleSnare) take(snare);
        else if (r == RoleKick) take(kick); else if (r == RoleOpenHat || nm.contains("open hat")) take(open);
        else if (cat == "Cymbal" || nm.contains("crash") || nm.contains("ride") || nm.contains("china") || nm.contains("cymbal")) take(cym);
        else if (r == RoleHat || nm.contains("shaker")) take(hat);
        else if (cat == "Tom" || nm.contains("tom")) take(tom); else if (r == RolePerc || nm.contains("perc") || nm.contains("rim") || nm.contains("cowbell")) take(perc);
        else if (r == Role808) take(bass);
    }
    auto pick = [](std::initializer_list<int> c) { for (int v : c) if (v >= 0) return v; return -1; };
    first[Kick] = pick({ kick, bass, tom, perc }); first[Snare] = pick({ snare, clap, perc }); first[Clap] = pick({ clap, snare, perc });
    first[HatClosed] = pick({ hat, open, perc }); first[HatOpen] = pick({ open, hat, perc }); first[Tom] = pick({ tom, perc, snare }); first[Perc] = pick({ perc, tom, hat });
    first[Cymbal] = pick({ cym, open, hat }); first[Bass] = pick({ bass, kick, tom });
    return first;
}
// the drum result onto pattern P (its tracks cleared first, locked tracks untouched). Returns hits written.
inline int writeDrums(Pattern& P, const std::array<Track, kTracks>& tracks, const Result& R, const std::array<int, kNumRoles>& map, const WriteOpts& w) {
    const int res = w.res == 0 || w.res == 2 ? w.res : 1; const double sb = stepBeats(res);
    const int steps = juce::jlimit(1, kMaxSteps, (int) std::lround(R.bars * 4.0 / sb));
    for (int t = 0; t < kTracks; ++t) if (!tracks[(size_t) t].lock) { for (auto& st : P.steps[(size_t) t]) st = Step(); P.trackLen[(size_t) t] = 0; }
    P.length = steps; P.res = res; P.evolve = 0; P.fillEvery = 0;
    const double beatSec = 60.0 / R.bpm; int written = 0;
    for (const auto& h : R.hits) {
        const int t = map[(size_t) h.role]; if (t < 0 || tracks[(size_t) t].lock || !((w.roles >> h.role) & 1u)) continue;
        const double p = (h.sec - R.origin) / beatSec / sb; long s = std::lround(p); const double resid = p - (double) s;
        if (s < 0) s = 0; if (s >= steps) { if (s == steps && resid < 0) s = 0; else continue; }   // a hit just before the loop end belongs on step 1
        auto& st = P.steps[(size_t) t][(size_t) s];
        const int vel = juce::jlimit(16, 127, (int) std::lround(127 * h.vel));   // (h.vel = the step velocity / 127)
        if (st.vel && st.vel >= vel) continue;
        st = Step(); st.vel = (uint8_t) vel; st.decay = 100;
        st.nudge = (int8_t) juce::jlimit(-50, 50, (int) std::lround((1.0f - juce::jlimit(0.0f, 1.0f, w.quantize)) * resid * 100.0));
        ++written;
    }
    return written;
}
// the loop's own strongest hit of each used role -> a sound (sample-accurate re-creation)
inline std::array<SoundPtr, kNumRoles> hitSounds(const Loop& L, const Result& R) {
    std::array<SoundPtr, kNumRoles> out {}; std::array<float, kNumRoles> best {}; std::array<double, kNumRoles> at {}; best.fill(-1);
    for (const auto& h : R.hits) if (h.vel > best[(size_t) h.role]) { best[(size_t) h.role] = h.vel; at[(size_t) h.role] = h.sec; }
    std::vector<double> starts; for (const auto& h : R.hits) starts.push_back(h.sec); std::sort(starts.begin(), starts.end());
    for (int r = 0; r < kNumRoles; ++r) {
        if (best[(size_t) r] < 0) continue;
        const double t0 = at[(size_t) r]; double t1 = t0 + 0.5; for (double s : starts) if (s > t0 + 0.02) { t1 = juce::jmin(t1, s); break; }
        const int a = (int) (t0 * L.sr), z = juce::jmin((int) L.x.size(), (int) (t1 * L.sr)); if (z - a < 64) continue;
        auto s = std::make_shared<Sound>(); s->rate = L.sr; s->category = "Sample"; s->name = L.name + " " + juce::String(roleName(r)).toLowerCase();
        s->data.assign(L.x.begin() + a, L.x.begin() + z); float mx = 1e-6f; for (auto v : s->data) mx = juce::jmax(mx, std::abs(v)); for (auto& v : s->data) v *= 0.89f / mx;
        const int fade = juce::jmin((int) s->data.size() / 4, (int) (L.sr * 0.005)); for (int i = 0; i < fade; ++i) s->data[s->data.size() - 1 - (size_t) i] *= (float) i / (float) fade;
        out[(size_t) r] = s;
    }
    return out;
}
// the pitched result onto the melody pattern (channel 1, 1/16 rows); returns notes written
inline int writeNotes(mel::Pattern& P, const Result& R, int inst = 1) {
    for (auto& c : P.cells[0]) c = mel::Cell();
    const int rows = juce::jlimit(1, mel::kRows, R.bars * 16); P.rows = juce::jmax(P.rows, rows); P.rows = rows;
    const double beatSec = 60.0 / R.bpm; int written = 0; int lastRow = -1;
    for (size_t i = 0; i < R.notes.size(); ++i) {
        const auto& nt = R.notes[i]; const long row = std::lround((nt.sec - R.origin) / beatSec * 4.0); if (row < 0 || row >= rows || row == lastRow) continue;
        auto& c = P.cells[0][(size_t) row]; c.note = (uint8_t) (juce::jlimit(0, 127, nt.midi) + 1); c.inst = (uint8_t) inst; c.vol = nt.vel >= 0.95f ? mel::kNoVol : (uint8_t) juce::jlimit(8, 64, (int) std::lround(nt.vel * 64)); c.fx = 0; c.param = 0;
        lastRow = (int) row; ++written;
        const long offRow = std::lround((nt.sec + nt.len - R.origin) / beatSec * 4.0);
        const long nextRow = i + 1 < R.notes.size() ? std::lround((R.notes[i + 1].sec - R.origin) / beatSec * 4.0) : rows;
        if (offRow > row && offRow < nextRow && offRow < rows) { auto& o = P.cells[0][(size_t) offRow]; if (o.empty()) o.note = mel::kNoteOff; }
    }
    return written;
}

} // namespace gb::a2m
