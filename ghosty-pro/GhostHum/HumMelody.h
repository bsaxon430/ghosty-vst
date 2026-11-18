#pragma once
// GHOST HUM - MELODY: a sung / hummed recording -> the notes you meant.
//
// 1. PREPARE   mono, DC removed, brought to ~16 kHz (RBJ low-pass + decimation, Shared/HearCore.h), peak normalised.
// 2. PITCH     every 8 ms: YIN (HearCore yin: cumulative mean normalised difference, threshold 0.15, parabolic refinement; 60..1200 Hz,
//              35 ms window), its aperiodicity, the RMS. A frame is VOICED when it is clearly periodic (aperiodicity < 0.35) and loud
//              enough (>= -32 dB of the take's loudest frame and 4x the noise floor). Octave slips of one frame are put back.
// 3. NOTES     a hidden-Markov path (Viterbi) over { silence, every MIDI note 36..96 }: a voiced frame "fits" a note by how many cents
//              it is away (sigma 55 cents, so vibrato of +-50 cents stays ONE note), changing note costs more than a few frames of
//              misfit (a real semitone change wins after ~40 ms; a wobble never does), an unvoiced frame inside a note costs a little
//              (a 20 ms glitch is kept; a 40 ms gap splits). Nothing is invented: a note needs voiced frames to exist.
// 4. REPEATS   "da da da" on the same pitch: the level's own re-attacks (RMS rises >= 5 dB out of a dip, 80 ms apart) split a note.
// 5. PITCH     each note's pitch = the median of its frames past the scoop (the first 20 % / 60 ms of a long note is skipped): the
//              nearest semitone and how far the voice leaned from it (cents; PITCH = ORIGINAL bends to them).
// 6. TIDY      octave twins put back (a piece > 9 semitones from the phrase's middle whose frames also hold its octave / twelfth twin on
//              the middle's side: vocal fry, a phone's missing fundamental), scoops / glitches < 70 ms glued to the longer neighbour they
//              touch, a note that drifted across the semitone line (medians < 65 cents apart, no re-attack) made one, blips < 50 ms,
//              whispers < -32 dB and plosive pops (short, low, nothing above 300 Hz) dropped, touching notes of the same pitch with no
//              re-attack between them joined. Velocity from each note's peak level.
// 7. KEY       Krumhansl-Kessler key profiles over the notes' pitch classes weighted by duration -> "C major" etc. (3+ pitch classes).
//              PITCH = ORIGINAL (as sung: the notes carry their cents, playback bends to them) | NEAREST NOTE (default) | SNAP TO KEY
//              (the detected key, or one the owner picks). The as-heard notes are kept beside the working copy; quantize is a third,
//              derived view on top (HumModel.h Grid), so detection, pitch and timing are separate steps and nothing is overwritten.
// pYIN / Tony (GPL) and Basic Pitch (Apache-2.0) were studied for the ideas (HMM note tracking); no code from them is here.
#include "HumModel.h"
#include "HearCore.h"
#include "HumClicks.h"

namespace hum::melody {

struct Options { int pitch = PitchNearest; int keyOverride = -1; double fmin = 60, fmax = 1200; const std::vector<double>* clickTimes = nullptr; };   // clickTimes: the metronome clicked then (its two tones are notched out first)
struct Cand { float cents = 0, ap = 1, pen = 0; };   // one pitch candidate of a frame: its pitch, how clearly periodic the signal is at it (low = clear), and its cost against the frame's likeliest (YIN's rule: the shortest period that is clearly periodic; sub-octave twins of it cost extra)
struct Frame { double t = 0; float f0 = 0, ap = 1, rms = 0, cents = 0; bool voiced = false, weak = false; std::vector<Cand> cands; };   // f0 / cents / ap = the best candidate; weak = breathy: pitched but not clearly (a run of them still makes a note)
struct Analysis {   // (tests / diagnostics)
    std::vector<Frame> frames; double hop = 0.008; std::vector<double> onsets; float peakRms = 0, floorRms = 0;
};

inline std::vector<float> toMono16k(const std::vector<float>& x, double sr, double& fsOut, const std::vector<double>* clicks = nullptr) {
    const int D = juce::jmax(1, (int) std::floor(sr / 16000.0)); fsOut = sr / D;
    // (the metronome's leaked clicks subtracted and notched first, HumClicks.h)
    auto y = gb::a2m::filtered(clicks::removed(x, sr, clicks), { gb::a2m::Biquad::make(true, 40.0, sr), gb::a2m::Biquad::make(false, fsOut * 0.42, sr), gb::a2m::Biquad::make(false, fsOut * 0.42, sr) });
    std::vector<float> d(y.size() / (size_t) D); for (size_t i = 0; i < d.size(); ++i) d[i] = y[i * (size_t) D];
    float pk = 1e-9f; for (auto v : d) pk = juce::jmax(pk, std::abs(v)); for (auto& v : d) v /= pk;
    return d;
}

// the level's re-attacks: RMS over 12 ms every 5 ms, a rise of >= riseDb within 40 ms out of a dip, >= minGap apart; returns the dip times
// the level's re-attacks: RMS over 12 ms every 5 ms, a rise of >= riseDb within 40 ms out of a dip, >= minGap apart; returns the dip times.
// (Build 2: the dip has to be a real V, the level was >= 2.5 dB higher within the 150 ms before it too, so a room's wobble or a reverb tail
// never splits a note.)
inline std::vector<double> levelOnsets(const std::vector<float>& x, double fs, float riseDb = 5.0f, double minGap = 0.08) {
    std::vector<double> on; const int n = (int) x.size(), fr = juce::jmax(1, (int) (fs * 0.005)), F = n / fr; if (F < 10) return on;
    std::vector<double> cum((size_t) n + 1, 0.0); for (int i = 0; i < n; ++i) cum[(size_t) i + 1] = cum[(size_t) i] + (double) x[(size_t) i] * x[(size_t) i];
    const int win = (int) (fs * 0.012); std::vector<float> db((size_t) F); float pk = -200;
    for (int f = 0; f < F; ++f) { const int z = juce::jmin(n, (f + 1) * fr), a = juce::jmax(0, z - win); db[(size_t) f] = 10.0f * std::log10((float) ((cum[(size_t) z] - cum[(size_t) a]) / juce::jmax(1, z - a)) + 1e-12f); pk = juce::jmax(pk, db[(size_t) f]); }
    const float floorDb = pk - 45.0f; int last = -1000;
    for (int f = 2; f < F; ++f) {
        if (db[(size_t) f] < floorDb + 10 || db[(size_t) f] < db[(size_t) f - 1]) continue;
        float lo = db[(size_t) f]; int at = f; for (int k = juce::jmax(0, f - 8); k < f; ++k) if (db[(size_t) k] < lo) { lo = db[(size_t) k]; at = k; }
        if (db[(size_t) f] - juce::jmax(lo, floorDb) < riseDb) continue;
        if (f - last < (int) (minGap / 0.005)) continue;
        // a real dip: the level was higher before it too (or it came out of the floor: the first note, or after a rest)
        float before = -200; for (int k = juce::jmax(0, at - 30); k < at; ++k) before = juce::jmax(before, db[(size_t) k]);
        if (lo > floorDb + 6 && before - lo < 2.5f) continue;
        on.push_back(at * fr / fs); last = f;
    }
    return on;
}

// YIN's cumulative mean normalised difference on one window, every clear dip (a local minimum below `maxAp`) as a candidate: the voice's
// true period and its octave / sub-octave twins all appear, and the note path (Viterbi below) picks the one that fits the line. The
// candidates are sorted clearest first. (The idea of keeping several YIN dips per frame is pYIN's; this is our own code.)
inline void yinCandidates(const float* x, int n, double fs, double fmin, double fmax, std::vector<Cand>& out, float maxAp = 0.5f) {
    out.clear(); const int tMin = juce::jmax(2, (int) (fs / fmax)), tMax = (int) (fs / fmin), W = juce::jmin(n - tMax - 1, (int) (fs * 0.035));
    if (W < tMax / 2 || W < 16) return;
    std::vector<float> dd((size_t) tMax + 2, 0.0f), c((size_t) tMax + 2, 1.0f);
    for (int t = 1; t <= tMax + 1; ++t) { double s = 0; for (int j = 0; j < W; ++j) { const float e = x[j] - x[j + t]; s += (double) e * e; } dd[(size_t) t] = (float) s; }
    double run = 0; for (int t = 1; t <= tMax + 1; ++t) { run += dd[(size_t) t]; c[(size_t) t] = run > 0 ? (float) (dd[(size_t) t] * t / run) : 1.0f; }
    for (int t = tMin; t <= tMax; ++t) {
        if (c[(size_t) t] >= maxAp || c[(size_t) t] > c[(size_t) t - 1] || c[(size_t) t] > c[(size_t) t + 1]) continue;
        if (t + 1 <= tMax && c[(size_t) t + 1] == c[(size_t) t]) continue;   // flat: take the last
        double tau = t; const double a = c[(size_t) t - 1], b = c[(size_t) t], cc = c[(size_t) t + 1], den = a - 2 * b + cc; if (std::abs(den) > 1e-12) tau += 0.5 * (a - cc) / den;
        Cand k; k.ap = c[(size_t) t]; k.cents = (float) (1200.0 * std::log2(fs / tau / 440.0) + 6900.0); out.push_back(k);
    }
    std::sort(out.begin(), out.end(), [](const Cand& p, const Cand& q) { return p.ap < q.ap; });
    if (out.size() > 8) out.resize(8);
}

inline void pitchFrames(const std::vector<float>& d, double fs, const Options& o, Analysis& A) {
    const int hop = juce::jmax(1, (int) std::lround(fs * A.hop)), W = (int) (fs * 0.035), tMax = (int) (fs / o.fmin), need = W + tMax + 2;
    A.frames.clear(); std::vector<float> rmsAll;
    for (int a = 0; a + need < (int) d.size(); a += hop) {
        Frame f; f.t = (a + W / 2) / fs; double s = 0; for (int k = a; k < a + W; ++k) s += (double) d[(size_t) k] * d[(size_t) k]; f.rms = (float) std::sqrt(s / W);
        yinCandidates(d.data() + a, need, fs, o.fmin, o.fmax, f.cands);
        if (!f.cands.empty()) {
            // YIN's pick: the highest-pitched candidate whose dip is clear (within reach of the clearest); the others pay a little per frame
            // (a sub-octave's dip is as deep as the real period's, often deeper: the shortest clear period is the voice)
            // (build 2: when the clearest period is under ~98 Hz, below where a voice usually sits, an octave-up twin is accepted with a shallower
            // dip, 0.25: vocal fry doubles the period and the clearest dip lies an octave under the note)
            const float apMin = f.cands[0].ap, thr = juce::jmax(f.cands[0].cents < 4300.0f ? 0.25f : 0.12f, apMin * 1.5f + 0.02f); int pick = 0;
            for (size_t k = 1; k < f.cands.size(); ++k) if (f.cands[k].ap <= thr && f.cands[k].cents > f.cands[(size_t) pick].cents) pick = (int) k;
            for (size_t k = 0; k < f.cands.size(); ++k) f.cands[k].pen = (int) k == pick ? 0.0f : 1.5f + 8.0f * juce::jmax(0.0f, f.cands[k].ap - thr);
            std::sort(f.cands.begin(), f.cands.end(), [](const Cand& p, const Cand& q) { return p.pen < q.pen; }); if (f.cands.size() > 5) f.cands.resize(5);   // the pick first
            f.ap = f.cands[(size_t) pick].ap; f.cents = f.cands[(size_t) pick].cents; f.f0 = (float) (440.0 * std::pow(2.0, (f.cents - 6900.0) / 1200.0)); }
        A.frames.push_back(f); rmsAll.push_back(f.rms);
    }
    if (A.frames.empty()) return;
    std::vector<float> srt = rmsAll; std::sort(srt.begin(), srt.end()); A.peakRms = srt.back(); A.floorRms = srt[srt.size() / 10];
    // the level gate: 6 dB over the room's floor, 2.5 % of the loudest frame, and 28 dB under the loudest frame within +-300 ms (a note's own
    // release tail is not the next note); the periodicity decides the rest
    const float gate = juce::jmax(A.peakRms * 0.025f, A.floorRms * 2.0f); const int N = (int) A.frames.size(), half = (int) std::lround(0.3 / A.hop);
    std::vector<float> local((size_t) N, 0.0f); for (int i = 0; i < N; ++i) { float m = 0; for (int k = juce::jmax(0, i - half); k <= juce::jmin(N - 1, i + half); ++k) m = juce::jmax(m, A.frames[(size_t) k].rms); local[(size_t) i] = m; }
    for (int i = 0; i < N; ++i) { auto& f = A.frames[(size_t) i]; const float g = juce::jmax(gate, local[(size_t) i] * 0.04f);
        f.voiced = f.f0 > 0 && f.cands[0].ap < 0.4f && f.rms >= g; f.weak = !f.voiced && f.f0 > 0 && f.cands[0].ap < 0.7f && f.rms >= g * 2; }
}

constexpr int kLo = 36, kHi = 96, kStates = kHi - kLo + 2;   // state 0 = silence, 1.. = midi kLo..kHi
// the note path: per frame the state (0 silence, else the MIDI note)
inline std::vector<int> viterbi(const std::vector<Frame>& fr) {
    const int N = (int) fr.size(); std::vector<int> path((size_t) N, 0); if (N == 0) return path;
    constexpr float sig = 55.0f, kSwitch = -7.0f, kInOut = -4.0f, kUnvoicedInNote = -2.0f, kVoicedInSilence = -6.0f, kTail = -9.0f;
    std::vector<float> cur((size_t) kStates, -1e30f), nxt((size_t) kStates); std::vector<std::vector<int16_t>> from((size_t) N, std::vector<int16_t>((size_t) kStates, 0));
    auto emit = [&](const Frame& f, int s) -> float {
        if (s == 0) return f.voiced ? kVoicedInSilence : f.weak ? -1.5f : 0.0f;
        if (!f.voiced && !f.weak) return kUnvoicedInNote;
        // the best candidate that fits this note: a less clear dip (octave twin) costs by how much shallower it is; the voice's usual
        // range (A2..A5) is a little more likely than the far ends
        const float target = 100.0f * (float) (kLo + s - 1); float best = kTail;
        for (auto& k : f.cands) { const float d = (k.cents - target) / sig; best = juce::jmax(best, juce::jmax(kTail, -0.5f * d * d) - k.pen); }
        if (f.weak) best = 0.5f * best - 1.0f;   // breathy frames: half the say, and a note through them costs a little (a run of them is still a note; noise is not)
        const int midi = kLo + s - 1; return best - (midi < 43 || midi > 84 ? 0.3f : 0.0f); };
    cur[0] = emit(fr[0], 0); for (int s = 1; s < kStates; ++s) cur[(size_t) s] = kInOut + emit(fr[0], s);
    for (int i = 1; i < N; ++i) {
        // the best note state to switch from (any note -> another note costs kSwitch; silence -> note kInOut)
        int bestNote = 1; for (int s = 2; s < kStates; ++s) if (cur[(size_t) s] > cur[(size_t) bestNote]) bestNote = s;
        // silence
        { const float a = cur[0], b = cur[(size_t) bestNote] + kInOut; nxt[0] = juce::jmax(a, b) + emit(fr[(size_t) i], 0); from[(size_t) i][0] = (int16_t) (a >= b ? 0 : bestNote); }
        for (int s = 1; s < kStates; ++s) {
            float best = cur[(size_t) s]; int f = s;
            const float sw = cur[(size_t) bestNote] + kSwitch; if (bestNote != s && sw > best) { best = sw; f = bestNote; }
            const float si = cur[0] + kInOut; if (si > best) { best = si; f = 0; }
            // the second-best note might be s itself: check the runner-up when bestNote == s
            if (bestNote == s) { int second = -1; for (int q = 1; q < kStates; ++q) if (q != s && (second < 0 || cur[(size_t) q] > cur[(size_t) second])) second = q;
                if (second > 0 && cur[(size_t) second] + kSwitch > best) { best = cur[(size_t) second] + kSwitch; f = second; } }
            nxt[(size_t) s] = best + emit(fr[(size_t) i], s); from[(size_t) i][(size_t) s] = (int16_t) f;
        }
        cur.swap(nxt);
    }
    int s = 0; for (int q = 1; q < kStates; ++q) if (cur[(size_t) q] > cur[(size_t) s]) s = q;
    for (int i = N - 1; i >= 0; --i) { path[(size_t) i] = s == 0 ? 0 : kLo + s - 1; s = from[(size_t) i][(size_t) s]; }
    return path;
}

struct Seg { int a = 0, b = 0; int midi = 0; float cents = 0, peak = 0, slope = 0; bool onset = false; };   // frames [a, b); slope = cents of its last third minus its first third (a scoop rises)

inline void keyOf(Take& T) {
    static const double maj[12] { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 }, min[12] { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
    std::array<double, 12> pc {}; int classes = 0; for (auto& n : T.notes) pc[(size_t) (((n.midi % 12) + 12) % 12)] += n.len + 0.05;
    for (double v : pc) if (v > 0) ++classes;
    T.key = -1; T.scale = 0; if (classes < 3) return;
    auto corr = [&](const double* prof, int k) { double mx = 0, my = 0; for (int i = 0; i < 12; ++i) { mx += pc[(size_t) i]; my += prof[i]; } mx /= 12; my /= 12; double sxy = 0, sxx = 0, syy = 0;
        for (int i = 0; i < 12; ++i) { const double x = pc[(size_t) ((i + k) % 12)] - mx, y = prof[i] - my; sxy += x * y; sxx += x * x; syy += y * y; } return sxy / std::sqrt(sxx * syy + 1e-12); };
    double best = -2; for (int k = 0; k < 12; ++k) for (int sc = 0; sc < 2; ++sc) { const double c = corr(sc ? min : maj, k); if (c > best) { best = c; T.key = k; T.scale = sc; } }
}

inline Take transcribe(const std::vector<float>& x, double sr, const Options& o, Analysis* diag = nullptr) {
    Take T; T.mode = Melody; T.seconds = x.size() / sr; Analysis A; double fs = 16000;
    if (x.size() < (size_t) (sr * 0.1)) return T;
    float peak = 0; for (auto v : x) peak = juce::jmax(peak, std::abs(v)); if (peak < 1e-4f) return T;
    auto d = toMono16k(x, sr, fs, o.clickTimes); clicks::Mask clickMask; clickMask.times = o.clickTimes; clickMask.before = 0.012;
    pitchFrames(d, fs, o, A); if (A.frames.empty()) return T;
    A.onsets = levelOnsets(d, fs);
    const auto path = viterbi(A.frames); const int N = (int) A.frames.size(); const double hop = A.hop;
    // segments
    std::vector<Seg> segs;
    for (int i = 0; i < N;) { if (path[(size_t) i] == 0) { ++i; continue; } int j = i; while (j < N && path[(size_t) j] == path[(size_t) i]) ++j; Seg s; s.a = i; s.b = j; s.midi = path[(size_t) i]; segs.push_back(s); i = j; }
    // re-attacks split a note (>= 60 ms on each side)
    { std::vector<Seg> out; for (auto s : segs) { std::vector<int> cuts;
        for (double t : A.onsets) { const int f = (int) std::lround((t - A.frames[0].t) / hop); if (f - s.a >= 8 && s.b - f >= 7) cuts.push_back(f); }
        int a = s.a; for (int c : cuts) { Seg p = s; p.a = a; p.b = c; out.push_back(p); a = c; } Seg p = s; p.a = a; p.onset = !cuts.empty(); out.push_back(p); }
      segs.swap(out); for (size_t i = 1; i < segs.size(); ++i) if (segs[i].a == segs[i - 1].b && segs[i].midi == segs[i - 1].midi) segs[i].onset = true;
      // a piece that starts within 24 ms of a re-attack starts a note of its own (the level dipped there: "da" after "da", even on the same pitch)
      for (auto& s : segs) for (double t : A.onsets) { const int f = (int) std::lround((t - A.frames[0].t) / hop); if (std::abs(f - s.a) <= 3) s.onset = true; } }
    auto measure = [&](Seg& s) {
        std::vector<float> c; const int len = s.b - s.a, skip = len > 18 ? juce::jmin(len / 5, 7) : 0;   // past the scoop (20 % / 60 ms) of a long note
        // (per frame: the pitch candidate nearest the path's note, so an octave twin that happened to be the clearest dip does not pull it)
        auto nearest = [&](const Frame& fr) { float best = 1e9f; for (auto& k : fr.cands) if (std::abs(k.cents - 100.0f * (float) s.midi) < std::abs(best - 100.0f * (float) s.midi)) best = k.cents; return best; };
        s.peak = 0; for (int f = s.a; f < s.b; ++f) { const auto& fr = A.frames[(size_t) f]; s.peak = juce::jmax(s.peak, fr.rms); if (f >= s.a + skip && (fr.voiced || fr.weak)) { const float v = nearest(fr); if (std::abs(v - 100.0f * (float) s.midi) < 250.0f) c.push_back(v); } }
        if (c.empty()) for (int f = s.a; f < s.b; ++f) if (A.frames[(size_t) f].voiced || A.frames[(size_t) f].weak) { const float v = nearest(A.frames[(size_t) f]); if (v < 1e8f) c.push_back(v); }
        if (c.empty()) { s.cents = 100.0f * (float) s.midi; s.slope = 0; return; }
        { std::vector<float> all; for (int f = s.a; f < s.b; ++f) if (A.frames[(size_t) f].voiced || A.frames[(size_t) f].weak) { const float v = nearest(A.frames[(size_t) f]); if (v < 1e8f) all.push_back(v); }
          const size_t n3 = all.size() / 3; if (n3 >= 1) { std::vector<float> lo(all.begin(), all.begin() + (long) n3), hi(all.end() - (long) n3, all.end());
              std::nth_element(lo.begin(), lo.begin() + (long) lo.size() / 2, lo.end()); std::nth_element(hi.begin(), hi.begin() + (long) hi.size() / 2, hi.end()); s.slope = hi[hi.size() / 2] - lo[lo.size() / 2]; } else s.slope = 0; }
        std::nth_element(c.begin(), c.begin() + (long) c.size() / 2, c.end()); s.cents = c[c.size() / 2]; };
    for (auto& s : segs) measure(s);
    // octave twins (build 2): a piece far (> 9 semitones) from the phrase's middle whose frames also hold a candidate at its octave / twelfth /
    // double octave / sixth-partial twin on the side of the middle (most frames) is that twin: vocal fry, a weak fundamental on a phone, the
    // first cycles of a note. (The phrase's middle: the duration-weighted median of the pieces >= 150 ms.)
    { std::vector<std::pair<float, int>> w; for (auto& s : segs) if (s.b - s.a >= 19) w.push_back({ s.cents, s.b - s.a }); std::sort(w.begin(), w.end());
      int tot = 0; for (auto& q : w) tot += q.second; float mid = 0; int acc = 0; for (auto& q : w) { acc += q.second; if (acc * 2 >= tot) { mid = q.first; break; } }
      if (!w.empty()) for (auto& s : segs) { if (std::abs(s.cents - mid) <= 900.0f) continue; const float dir = s.cents < mid ? 1.0f : -1.0f;
          float bestTwin = 0; int bestN = 0;
          for (float tw : { 1200.0f, 1900.0f, 2400.0f, 3100.0f }) { const float target = s.cents + dir * tw; if (std::abs(target - mid) > std::abs(s.cents - mid)) continue;
              int nn = 0, nv = 0; for (int f = s.a; f < s.b; ++f) { const auto& fr = A.frames[(size_t) f]; if (!(fr.voiced || fr.weak)) continue; ++nv; for (auto& k : fr.cands) if (std::abs(k.cents - target) < 70.0f) { ++nn; break; } }
              if (nv > 0 && nn * 2 > nv && nn > bestN) { bestN = nn; bestTwin = target; } }
          if (bestN > 0) { s.midi = (int) std::lround(bestTwin / 100.0f); measure(s); } } }
   #ifdef HUM_DEBUG_SEGS
    for (auto& s : segs) std::printf("  seg %d..%d midi %d cents %.0f slope %.0f onset %d\n", s.a, s.b, s.midi, s.cents, s.slope, (int) s.onset);
   #endif
    // glue: a short piece (< 70 ms) touching a longer neighbour with no re-attack at the seam joins it; touching same-pitch pieces join
    bool changed = true;
    while (changed && segs.size() > 1) { changed = false;
        for (size_t i = 0; i < segs.size(); ++i) { auto& s = segs[i]; const int len = s.b - s.a; const bool shortOne = len < 10;
            const bool prevTouch = i > 0 && s.a - segs[i - 1].b <= 3, nextTouch = i + 1 < segs.size() && segs[i + 1].a - s.b <= 3;
            const int prevMidi = i > 0 ? std::lround(segs[i - 1].cents / 100.0f) : -99, nextMidi = i + 1 < segs.size() ? std::lround(segs[i + 1].cents / 100.0f) : -99;
            int into = -1;
            // a scoop: < 160 ms, rising through itself, sitting 1..6 semitones under the longer note it slides into (no re-attack at the seam)
            const bool scoop = len < 20 && nextTouch && !segs[i + 1].onset && (segs[i + 1].b - segs[i + 1].a) > len && s.slope > 30.0f && segs[i + 1].cents - s.cents > 30.0f && segs[i + 1].cents - s.cents < 650.0f;
            // a fall-off: the voice drops away at the end of a longer note (< 160 ms, falling, 1..6 semitones under it)
            const bool falloff = len < 20 && prevTouch && !s.onset && (segs[i - 1].b - segs[i - 1].a) > len && s.slope < -30.0f && segs[i - 1].cents - s.cents > 30.0f && segs[i - 1].cents - s.cents < 650.0f;
            // an octave twin: a short piece an octave / twelfth / two octaves away from the longer note it touches (the first frames of a note,
            // before the voice settles, or its last) is that note
            auto twin = [](float dc) { dc = std::abs(dc); return std::abs(dc - 1200) < 70 || std::abs(dc - 1900) < 70 || std::abs(dc - 2400) < 70; };
            const bool twinNext = len < 16 && nextTouch && !segs[i + 1].onset && (segs[i + 1].b - segs[i + 1].a) > 2 * len && twin(segs[i + 1].cents - s.cents);
            const bool twinPrev = len < 16 && prevTouch && !s.onset && (segs[i - 1].b - segs[i - 1].a) > 2 * len && twin(segs[i - 1].cents - s.cents);
            // a blip: < 104 ms touching a neighbour at least twice as long with no re-attack at the seam (the first frames of a note before the voice
            // settles, its last as it lets go, or a flicker between two notes) belongs to that neighbour (the nearer in pitch when both touch)
            const bool blipPrev = len < 13 && prevTouch && !s.onset && (segs[i - 1].b - segs[i - 1].a) >= 2 * len;
            const bool blipNext = len < 13 && nextTouch && !segs[i + 1].onset && (segs[i + 1].b - segs[i + 1].a) >= 2 * len;
            // the "n" of "na" / the "l" of "la": a short (< 80 ms) dull piece with no re-attack of its own, ending where the longer note after it
            // re-attacks, within 3.5 semitones of it, is the start of that note (the murmur before the vowel)
            const bool murmur = len < 10 && nextTouch && !s.onset && segs[i + 1].onset && (segs[i + 1].b - segs[i + 1].a) >= 2 * len && std::abs(segs[i + 1].cents - s.cents) < 350.0f;
            if (blipPrev && blipNext) into = std::abs(segs[i - 1].cents - s.cents) <= std::abs(segs[i + 1].cents - s.cents) ? (int) i - 1 : (int) i + 1;
            else if (murmur) into = (int) i + 1;
            else if (blipNext || scoop || twinNext) into = (int) i + 1;
            else if (blipPrev || falloff || twinPrev) into = (int) i - 1;
            else if (shortOne && !s.onset && prevTouch && (segs[i - 1].b - segs[i - 1].a) >= len) into = (int) i - 1;
            else if (shortOne && nextTouch && !segs[i + 1].onset && (segs[i + 1].b - segs[i + 1].a) >= len) into = (int) i + 1;
            else if (prevTouch && !s.onset && prevMidi == std::lround(s.cents / 100.0f)) into = (int) i - 1;
            else if (nextTouch && !segs[i + 1].onset && nextMidi == std::lround(s.cents / 100.0f)) into = (int) i + 1;
            // the same pitch really (medians < 65 cents apart, no re-attack between): a note drifting across the semitone line is one note
            else if (prevTouch && !s.onset && std::abs(segs[i - 1].cents - s.cents) < 65.0f) into = (int) i - 1;
            else if (nextTouch && !segs[i + 1].onset && std::abs(segs[i + 1].cents - s.cents) < 65.0f) into = (int) i + 1;
            if (into < 0) continue;
            auto& o2 = segs[(size_t) into]; o2.a = juce::jmin(o2.a, s.a); o2.b = juce::jmax(o2.b, s.b); if (into > (int) i) o2.onset = s.onset; measure(o2); segs.erase(segs.begin() + (long) i); changed = true; break; } }
    // notes
    float loud = 0; for (auto& s : segs) loud = juce::jmax(loud, s.peak);
    // (plosive pops: a short low thump with nothing above 300 Hz is breath on the mic, not a note; a voice always has harmonics up there)
    std::vector<float> dHi; auto hiShare = [&](const Seg& s) { if (dHi.empty()) dHi = gb::a2m::filtered(d, { gb::a2m::Biquad::make(true, 300.0, fs), gb::a2m::Biquad::make(true, 300.0, fs) });
        const int a = juce::jlimit(0, (int) d.size(), (int) (A.frames[(size_t) s.a].t * fs)), z = juce::jlimit(a, (int) d.size(), (int) ((A.frames[(size_t) (s.b - 1)].t + 0.02) * fs)); double eh = 0, et = 0;
        for (int k = a; k < z; ++k) { eh += (double) dHi[(size_t) k] * dHi[(size_t) k]; et += (double) d[(size_t) k] * d[(size_t) k]; } return et > 0 ? eh / et : 1.0; };
    for (auto& s : segs) {
        const double len = (s.b - s.a) * hop; if (len < 0.05) continue; if (s.peak < loud * 0.025f) continue;   // blips, whispers
        if (len < 0.16 && s.cents < 5000.0f && hiShare(s) < 0.01) continue;   // a pop
        if (len < 0.12 && clickMask.near(A.frames[(size_t) s.a].t)) continue;   // what a leaked metronome click left: a short "note" starting right on a click
        Note n; n.t = juce::jmax(0.0, A.frames[(size_t) s.a].t - 0.035 * 0.5); n.len = len; n.midi = juce::jlimit(0, 127, (int) std::lround(s.cents / 100.0f)); n.cents = juce::jlimit(-50.0f, 50.0f, s.cents - 100.0f * (float) n.midi);
        n.vel = juce::jlimit(0.2f, 1.0f, 0.3f + 0.7f * std::sqrt(s.peak / juce::jmax(1e-9f, loud)));
        T.notes.push_back(n);
    }
    for (size_t i = 0; i + 1 < T.notes.size(); ++i) if (T.notes[i + 1].t - (T.notes[i].t + T.notes[i].len) < 0.03) T.notes[i].len = T.notes[i + 1].t - T.notes[i].t;   // legato: no gap
    keyOf(T);
    T.origNotes = T.notes;                                 // exactly as heard (cents included); never changed afterwards
    applyPitch(T, o.pitch, o.keyOverride);                 // the working copy: ORIGINAL / NEAREST NOTE / SNAP TO KEY
    if (diag) *diag = A;
    return T;
}

} // namespace hum::melody
