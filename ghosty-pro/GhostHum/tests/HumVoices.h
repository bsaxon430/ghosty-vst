#pragma once
// GHOST HUM tests - our own synthesised voices. Nothing recorded, no datasets, no songs: every phrase and every beat here is
// generated from numbers, so the melody tests know every note and the beatbox classifier is trained only on audio we made.
//
//   SUNG PHRASES  a glottal pulse source (soft sawtooth pulses, low-passed) through three vowel formants, with vibrato (ramps in),
//                 a scoop into each note, detune, slow pitch drift and jitter, breath noise, consonant bursts for "da da da" re-attacks,
//                 attack / release shaping, legato glides or gaps, plus a quiet room / mic noise floor. Every "singer" has their own
//                 formants, vibrato, breathiness and tuning habits.
//   BEATBOX HITS  mouth drums: kick ("b" / "boom": lip burst + a pitched body sweeping down, sometimes a voiced hum tail), snare ("pf" /
//                 "psh" / "ka": a plosive click + a band of noise), closed hat ("t" / "ts": a few tens of ms of bright noise), open hat
//                 ("tss": the same, ringing on), clap ("k" / "pah" / a tongue click: mid noise in two to four quick sub-bursts, little tail). Every "beatboxer" has their own
//                 mouth resonance, brightness tilt, kick pitch, lengths and loudness balance, so the classifier is tested on people it
//                 never heard. hitTruth: the ground truth of a pattern.
#include <juce_core/juce_core.h>
#include <vector>
#include <array>
#include <cmath>
#include <random>

namespace humtest {

constexpr double kSr = 48000.0;
constexpr double TAU = 6.283185307179586;

struct Rand {
    std::mt19937 g; explicit Rand(uint32_t s) : g(s) {}
    double u(double a = 0, double b = 1) { return std::uniform_real_distribution<double>(a, b)(g); }
    double n(double m = 0, double s = 1) { return std::normal_distribution<double>(m, s)(g); }
    int i(int a, int b) { return std::uniform_int_distribution<int>(a, b)(g); }
    bool p(double prob) { return u() < prob; }
};

// two-pole resonator (formant) and simple one-pole filters
struct Reso { double a1 = 0, a2 = 0, g = 1, y1 = 0, y2 = 0;
    void set(double f, double bw, double sr) { const double r = std::exp(-M_PI * bw / sr); a1 = -2 * r * std::cos(TAU * f / sr); a2 = r * r; g = (1 - r) * std::sqrt(1 - 2 * r * std::cos(2 * TAU * f / sr) + r * r); }
    double run(double x) { const double y = g * x - a1 * y1 - a2 * y2; y2 = y1; y1 = y; return y; } };
struct LP1 { double z = 0, a = 1; void set(double hz, double sr) { a = 1 - std::exp(-TAU * hz / sr); } double run(double x) { z += a * (x - z); return z; } };
struct HP1 { LP1 l; void set(double hz, double sr) { l.set(hz, sr); } double run(double x) { return x - l.run(x); } };
struct BP { HP1 h; LP1 l; void set(double lo, double hi, double sr) { h.set(lo, sr); l.set(hi, sr); } double run(double x) { return l.run(h.run(x)); } };

inline void addNoiseFloor(std::vector<float>& x, Rand& R, double db = -52) { const double g = std::pow(10.0, db / 20.0); LP1 lp; lp.set(6000, kSr); for (auto& v : x) v += (float) (lp.run(R.n()) * g); }
inline void normalise(std::vector<float>& x, double peak = 0.7) { float pk = 1e-9f; for (auto v : x) pk = std::max(pk, std::abs(v)); for (auto& v : x) v = (float) (v * peak / pk); }

// ---------------------------------------------------------------- sung phrases
struct Singer {
    std::array<double, 3> F { 700, 1220, 2600 };   // vowel formants (Hz)
    double altPulse = 0;                                   // vocal fry / period doubling: every other glottal pulse this much weaker (0..1) -> an octave trap for pitch trackers
    bool closedMouth = false;                              // humming with the mouth closed: nasal murmur (formants ~250 / 1000 / 2500, dull, quiet)
    double vibHz = 5.5, vibCents = 30, vibDelay = 0.18;   // vibrato
    double scoopCents = -150, scoopSec = 0.08;             // the slide into each note
    double detuneCents = 0;                                // the singer's habit (flat / sharp)
    double driftCents = 8, jitterCents = 3;                // slow wander, fast wobble
    double breathDb = -30;                                 // breath noise under the tone
    double attack = 0.03, release = 0.06;
    double glide = 0.05;                                   // legato: the time a pitch change takes
    double brightness = 1.0;                               // source spectrum (1 = normal)
    double gain = 0.6;
    double roomDb = -52;                                   // the mic / room noise floor under everything
    static Singer random(Rand& R) {
        Singer s; static const double vowels[4][3] { { 700, 1220, 2600 }, { 300, 870, 2250 }, { 400, 2000, 2550 }, { 500, 1000, 2500 } };   // ah, oo, ee, hum-ish
        const int v = R.i(0, 3); for (int k = 0; k < 3; ++k) s.F[(size_t) k] = vowels[v][k] * R.u(0.9, 1.1);
        s.vibHz = R.u(4.5, 6.8); s.vibCents = R.u(5, 50); s.vibDelay = R.u(0.08, 0.3); s.scoopCents = R.p(0.7) ? R.u(-300, -60) : 0; s.scoopSec = R.u(0.04, 0.13);
        s.detuneCents = R.u(-40, 40); s.driftCents = R.u(2, 15); s.jitterCents = R.u(1, 6); s.breathDb = R.u(-40, -22); s.attack = R.u(0.01, 0.06); s.release = R.u(0.03, 0.12);
        s.glide = R.u(0.02, 0.09); s.brightness = R.u(0.6, 1.5); s.gain = R.u(0.3, 0.8); return s;
    }
};
// cons: the consonant that starts the note. 0 none (or the "d" when reattack), 1 "la" (a voiced "l": the tongue dims the vowel for
// 40..70 ms, no silence, the level dips ~8 dB), 2 "na" (a nasal murmur: voiced, dull, ~10 dB down for 50..80 ms, no silence)
struct SungNote { int midi; double len; double gap; bool reattack; int cons = 0; };   // len seconds, gap of silence after it (0 = legato into the next), reattack = a "d" consonant at its start
struct Truth { std::vector<double> on, off; std::vector<int> midi; };

inline std::vector<float> sing(const Singer& S, const std::vector<SungNote>& notes, Rand& R, Truth& truth, double lead = 0.3, double tail = 0.5) {
    double total = lead + tail; for (auto& n : notes) total += n.len + n.gap;
    const int N = (int) (total * kSr); std::vector<float> x((size_t) N, 0.0f);
    std::array<double, 3> F = S.F; if (S.closedMouth) F = { 250, 1000, 2500 };
    std::array<Reso, 3> fm; for (int k = 0; k < 3; ++k) fm[(size_t) k].set(F[(size_t) k], (S.closedMouth ? 90 : 60) + 40 * k, kSr);
    std::array<Reso, 3> fmC; fmC[0].set(320, 80, kSr); fmC[1].set(1100, 120, kSr); fmC[2].set(2500, 200, kSr);   // the "l" / "n" colour
    LP1 src; src.set(1800 * S.brightness * (S.closedMouth ? 0.35 : 1.0), kSr); BP breath; breath.set(800, 4000, kSr); LP1 driftLp; driftLp.set(0.7, kSr); LP1 nasalLp; nasalLp.set(900, kSr);
    double ph = 0, t = lead, drift = 0; truth = {}; int pulse = 0;
    double prevF = 0;
    for (size_t i = 0; i < notes.size(); ++i) {
        const auto& n = notes[i]; const double f0 = 440.0 * std::pow(2.0, (n.midi - 69) / 12.0);
        const bool legatoIn = i > 0 && notes[i - 1].gap <= 0 && !n.reattack && n.cons == 0;
        // "da": the tongue closes the mouth for 25..50 ms before the next note when it re-attacks with no gap
        const double closure = i + 1 < notes.size() && n.gap <= 0 && notes[i + 1].reattack ? R.u(0.025, 0.05) : 0.0;
        const double consSec = n.cons == 1 ? R.u(0.04, 0.07) : n.cons == 2 ? R.u(0.05, 0.08) : 0.0;
        truth.on.push_back(t); truth.off.push_back(t + n.len - closure); truth.midi.push_back(n.midi);
        const int a = (int) (t * kSr), z = (int) ((t + n.len - closure) * kSr);
        for (int k = a; k < z && k < N; ++k) {
            const double tt = (k - a) / kSr, tl = n.len - closure - tt;
            // pitch: scoop or glide in, vibrato ramping in, drift + jitter, the singer's detune
            double cents = S.detuneCents;
            if (legatoIn && prevF > 0 && tt < S.glide) { const double w = tt / S.glide; cents += 1200.0 * std::log2(prevF / f0) * (1 - w); }
            else if (!legatoIn && tt < S.scoopSec) cents += S.scoopCents * (1 - tt / S.scoopSec);
            const double vibAmt = std::min(1.0, std::max(0.0, (tt - S.vibDelay) / 0.25)); cents += S.vibCents * vibAmt * std::sin(TAU * S.vibHz * tt + (double) i);
            drift = driftLp.run(R.n() * 40.0); cents += drift * S.driftCents / 8.0 + R.n() * S.jitterCents * 0.3;
            const double f = f0 * std::pow(2.0, cents / 1200.0); ph += f / kSr; if (ph >= 1) { ph -= 1; ++pulse; }
            // source: a soft pulse (saw shaped, low-passed), plus breath; vocal fry weakens every other pulse
            const double saw = 2 * ph - 1; double s = src.run((saw * saw * saw * 0.8 + saw * 0.2) * ((pulse & 1) ? 1.0 - S.altPulse : 1.0));
            double env = std::min(1.0, tt / S.attack) * std::min(1.0, std::max(0.0, tl / S.release));
            if (legatoIn) env = std::min(1.0, std::max(0.0, tl / S.release)); if (i + 1 < notes.size() && n.gap <= 0 && closure <= 0) env = std::min(1.0, tt / S.attack);   // legato: no dip at the seam
            if (n.reattack && tt < 0.012) env *= tt / 0.012;   // the "d": the tone starts from nothing
            double v = 0;
            if (n.cons != 0 && tt < consSec) {   // "l" / "n": voiced through the consonant, dimmer and duller, blending into the vowel over its last 15 ms
                const double w = std::min(1.0, std::max(0.0, (consSec - tt) / 0.015)); double c = 0; for (auto& r : fmC) c += r.run(s); c *= 0.5; if (n.cons == 2) c = nasalLp.run(c) * 1.3;
                double vo = 0; for (auto& r : fm) vo += r.run(s); vo *= 0.5; v = c * (n.cons == 1 ? 0.4 : 0.32) * w + vo * (1 - w); }
            else { for (auto& r : fm) v += r.run(s); v *= 0.5; }
            v += breath.run(R.n()) * std::pow(10.0, S.breathDb / 20.0);
            if (n.reattack && tt < 0.015) v += R.n() * 0.15 * (1 - tt / 0.015);   // the consonant burst
            x[(size_t) k] += (float) (v * env * S.gain * (0.85 + 0.15 * std::sin(TAU * 0.6 * tt)));
        }
        prevF = f0; t += n.len + n.gap;
    }
    addNoiseFloor(x, R, S.roomDb); normalise(x, 0.6); return x;
}

// phrases: a random walk in a key, note lengths from 8ths to half notes at a tempo, some legato, some "da da" repeats
inline std::vector<SungNote> randomPhrase(Rand& R, int count, double bpm, int low = 55, int high = 72, bool allowRepeats = true) {
    static const int major[] { 0, 2, 4, 5, 7, 9, 11 }; const int key = R.i(0, 11);
    std::vector<int> scale; for (int m = low - 12; m <= high + 12; ++m) { const int d = ((m - key) % 12 + 12) % 12; for (int q : major) if (q == d) scale.push_back(m); }
    std::vector<SungNote> out; int idx = (int) scale.size() / 2; const double beat = 60.0 / bpm;
    for (int i = 0; i < count; ++i) {
        const int step = R.p(0.25) ? 0 : R.i(-3, 3); idx = std::min((int) scale.size() - 1, std::max(0, idx + step));
        if (step == 0 && !allowRepeats) idx = std::min((int) scale.size() - 1, idx + 1);
        while (scale[(size_t) idx] < low) ++idx; while (scale[(size_t) idx] > high) --idx;
        static const double lens[] { 0.5, 0.5, 1, 1, 1, 1.5, 2 }; SungNote n; n.midi = scale[(size_t) idx]; n.len = lens[R.i(0, 6)] * beat * R.u(0.85, 1.0);
        n.gap = R.p(0.45) ? 0 : R.u(0.04, 0.25); n.reattack = (!out.empty() && out.back().midi == scale[(size_t) idx]) || R.p(0.3); out.push_back(n);
    }
    return out;
}

// ---------------------------------------------------------------- beatbox hits
enum Drum { Kick = 0, Snare, HatClosed, HatOpen, Clap, kDrums };
struct Beatboxer {
    double mouthHz = 1100, mouthQ = 1.5;       // the mouth's own resonance colouring every sound
    double tilt = 0;                           // brightness tilt (dB per octave-ish, -1..1 -> dark..bright)
    double kickHz = 90, kickSweep = 2.0, kickDecay = 0.12, kickClick = 0.6, kickHum = 0.0;
    double snareHz = 2200, snareBw = 1.6, snareDecay = 0.09, snareThump = 0.3, snareClick = 0.5;
    double hatHz = 7500, hatDecay = 0.03, openDecay = 0.25;
    double clapHz = 1500, clapDecay = 0.07; int clapBursts = 3;
    std::array<double, kDrums> level { 1.0, 0.8, 0.45, 0.5, 0.7 };
    double micLp = 14000;
    static Beatboxer random(Rand& R) {
        Beatboxer b; b.mouthHz = R.u(700, 1800); b.mouthQ = R.u(0.8, 2.5); b.tilt = R.u(-1, 1);
        b.kickHz = R.u(60, 140); b.kickSweep = R.u(1.3, 3.0); b.kickDecay = R.u(0.06, 0.22); b.kickClick = R.u(0.2, 1.0); b.kickHum = R.p(0.4) ? R.u(0.2, 0.8) : 0;
        b.snareHz = R.u(1400, 3500); b.snareBw = R.u(1.2, 2.2); b.snareDecay = R.u(0.05, 0.16); b.snareThump = R.u(0, 0.6); b.snareClick = R.u(0.2, 1.0);
        b.hatHz = R.u(5000, 9500); b.hatDecay = R.u(0.015, 0.05); b.openDecay = R.u(0.14, 0.45);
        b.clapHz = R.u(900, 2800); b.clapDecay = R.u(0.015, 0.06); b.clapBursts = R.i(2, 4);   // (a mouth clap is a short "k" / tongue click: bursts, little tail)
        for (auto& l : b.level) l *= R.u(0.6, 1.3); b.micLp = R.u(9000, 18000); return b;
    }
};
// one hit rendered into x at sample a (adds); vel 0..1
inline void hit(std::vector<float>& x, int a, int drum, double vel, const Beatboxer& B, Rand& R) {
    const int N = (int) x.size(); const double lenS = drum == Kick ? B.kickDecay * 4 + 0.05 : drum == Snare ? B.snareDecay * 4 + 0.03 : drum == HatClosed ? B.hatDecay * 5 + 0.02 : drum == HatOpen ? B.openDecay * 4 : B.clapDecay * 4 + 0.05;
    const int n = std::min(N - a, (int) (lenS * kSr)); if (n <= 0) return;
    std::vector<double> y((size_t) n, 0.0);
    const double g = vel * B.level[(size_t) drum] * R.u(0.9, 1.1);
    switch (drum) {
        case Kick: { double ph = 0; LP1 lp; lp.set(3000, kSr); const double hz = B.kickHz * R.u(0.92, 1.08), hum = B.kickHum;
            for (int i = 0; i < n; ++i) { const double t = i / kSr, f = hz * (1 + (B.kickSweep - 1) * std::exp(-t / 0.025)); ph += f / kSr;
                const double body = std::tanh(2.0 * std::sin(TAU * ph)) * std::exp(-t / B.kickDecay) * std::min(1.0, t / 0.002);
                const double click = lp.run(R.n()) * std::exp(-t / 0.003) * B.kickClick;
                const double humm = hum > 0 ? std::sin(TAU * ph) * std::exp(-t / (B.kickDecay * 2.5)) * hum * 0.5 : 0;
                y[(size_t) i] = body * 0.9 + click * 0.5 + humm; } break; }
        case Snare: { Reso r; r.set(B.snareHz * R.u(0.95, 1.05), B.snareHz / B.snareBw, kSr); HP1 hp; hp.set(600, kSr); double ph = 0; const double hz = R.u(150, 260);
            for (int i = 0; i < n; ++i) { const double t = i / kSr; ph += hz * (1 + 0.5 * std::exp(-t / 0.01)) / kSr;
                const double ns = (r.run(R.n()) * 3.0 + hp.run(R.n()) * 0.25) * std::exp(-t / B.snareDecay) * std::min(1.0, t / 0.0015);
                const double click = R.n() * std::exp(-t / 0.002) * B.snareClick, thump = std::sin(TAU * ph) * std::exp(-t / 0.03) * B.snareThump;
                y[(size_t) i] = ns + click * 0.6 + thump; } break; }
        case HatClosed: case HatOpen: { const double dec = drum == HatClosed ? B.hatDecay : B.openDecay; Reso r; r.set(B.hatHz * R.u(0.93, 1.07), B.hatHz / 1.2, kSr); HP1 hp, hp2, hp3; hp.set(3500, kSr); hp2.set(3000, kSr); hp3.set(2500, kSr);
            for (int i = 0; i < n; ++i) { const double t = i / kSr; const double e = drum == HatOpen ? std::exp(-t / dec) * (t < 0.01 ? t / 0.01 : 1.0) : std::exp(-t / dec) * std::min(1.0, t / 0.001);
                y[(size_t) i] = hp3.run(hp2.run((r.run(R.n()) * 2.0 + hp.run(R.n()) * 0.5) * e + (t < 0.002 ? R.n() * 0.4 * (1 - t / 0.002) : 0))); } break; }
        default: { Reso r; r.set(B.clapHz * R.u(0.9, 1.1), B.clapHz / 1.1, kSr); LP1 lp; lp.set(5000, kSr); std::vector<double> on; double o = 0; for (int b = 0; b < B.clapBursts; ++b) { on.push_back(o); o += R.u(0.005, 0.011); }
            for (int i = 0; i < n; ++i) { const double t = i / kSr; double e = 0; for (size_t b = 0; b < on.size(); ++b) if (t >= on[b]) e = std::max(e, std::exp(-(t - on[b]) / (b + 1 == on.size() ? B.clapDecay : 0.006)));
                y[(size_t) i] = lp.run(r.run(R.n()) * 2.5 + R.n() * 0.2) * e; } break; }
    }
    // the mouth, the tilt, the mic
    Reso mouth; mouth.set(B.mouthHz, B.mouthHz / B.mouthQ, kSr); HP1 tiltHp; tiltHp.set(1500, kSr); LP1 mic; mic.set(B.micLp, kSr);
    double pk = 1e-9; for (int i = 0; i < n; ++i) { double v = y[(size_t) i]; v = v * 0.7 + mouth.run(v) * 0.6; const double hi = tiltHp.run(v); v += hi * B.tilt * 0.7; v = mic.run(v); y[(size_t) i] = v; pk = std::max(pk, std::abs(v)); }
    const double scale = g * 0.5 / pk; for (int i = 0; i < n; ++i) x[(size_t) (a + i)] += (float) (y[(size_t) i] * scale);
}
struct HitTruth { double t; int drum; double vel; };
// a pattern at a tempo: 1/8 / 1/16 positions with human timing error; returns the audio; truth filled
inline std::vector<float> beatbox(const Beatboxer& B, const std::vector<HitTruth>& hits, Rand& R, double tail = 0.6) {
    double end = 0; for (auto& h : hits) end = std::max(end, h.t); const int N = (int) ((end + tail) * kSr); std::vector<float> x((size_t) N, 0.0f);
    for (auto& h : hits) hit(x, (int) (h.t * kSr), h.drum, h.vel, B, R);
    addNoiseFloor(x, R, -50); normalise(x, 0.7); return x;
}
// a typical beatbox bar pattern (random style), n bars, with timing jitter (ms sd) and velocity variety
inline std::vector<HitTruth> randomPattern(Rand& R, double bpm, int bars, double jitterMs = 8, double lead = 0.2, bool withClap = true, bool withOpen = true) {
    std::vector<HitTruth> out; const double s16 = 60.0 / bpm / 4; const int steps = 16 * bars;
    std::array<int, 16> lane {}; lane.fill(-1);
    lane[0] = Kick; lane[4] = withClap && R.p(0.4) ? Clap : Snare; lane[8] = Kick; lane[12] = withClap && R.p(0.4) ? Clap : Snare;
    for (int s = 0; s < 16; ++s) if (lane[(size_t) s] < 0) { if (s % 2 == 0 && R.p(0.75)) lane[(size_t) s] = HatClosed; else if (R.p(0.2)) lane[(size_t) s] = R.p(0.5) ? HatClosed : Kick; }
    if (withOpen) { const int o = R.p(0.5) ? 14 : 6; lane[(size_t) o] = HatOpen; }
    if (R.p(0.3)) lane[10] = Kick; if (R.p(0.3)) lane[7] = Kick;
    for (int s = 0; s < steps; ++s) { const int d = lane[(size_t) (s % 16)]; if (d < 0) continue; if (R.p(0.08)) continue;   // a dropped hit now and then
        HitTruth h; h.t = lead + s * s16 + R.n() * jitterMs / 1000.0; h.drum = d; h.vel = d == HatClosed ? R.u(0.4, 0.8) : R.u(0.6, 1.0); if (s % 4 == 0) h.vel = std::min(1.0, h.vel + 0.15); out.push_back(h); }
    std::sort(out.begin(), out.end(), [](const HitTruth& a, const HitTruth& b) { return a.t < b.t; });
    // no two hits closer than 70 ms (one mouth)
    std::vector<HitTruth> clean; for (auto& h : out) if (clean.empty() || h.t - clean.back().t >= 0.07) clean.push_back(h);
    return clean;
}

// ---------------------------------------------------------------- real microphones, rooms and mouths (build 2)
// What a phone or laptop mic adds to a take. Applied AFTER the voice is made, so every condition is tested on the same singers / beatboxers.
struct Mic {
    double dc = 0;              // a DC offset (fraction of full scale)
    double rumbleDb = -200;     // low rumble under 30 Hz (traffic, handling)
    double reverb = 0;          // the room's reverb: wet share 0..1 (a small Schroeder room)
    double reverbSec = 0.6;     // its decay (RT60-ish)
    double clipGain = 1;        // > 1: the mic clips (hard clip after this gain)
    double levelDb = 0;         // a very quiet take: -40
    int clicks = 0;             // mouth clicks (1..3 ms bright ticks)
    int smacks = 0;             // lip smacks (two ticks 8..15 ms apart)
    int pops = 0;               // plosive pops (a 30..80 ms thump at 30..90 Hz, loud)
    double fanDb = -200;        // a laptop fan: steady dull noise
    double humDb = -200;        // mains hum: 50 / 60 Hz plus harmonics
    double humHz = 60;
    double lowCut = 0;          // a phone's low cut (Hz, 0 = none): the fundamental of a low voice goes missing
};
inline void addClick(std::vector<float>& x, int at, double amp, Rand& R, double lenS, double hz) {
    const int n = std::min((int) x.size() - at, std::max(8, (int) (lenS * kSr))); if (n <= 0) return; Reso r; r.set(hz, hz / 2.0, kSr);
    for (int i = 0; i < n; ++i) { const double t = i / kSr; x[(size_t) (at + i)] += (float) (r.run(R.n()) * 2.0 * std::exp(-t / (lenS * 0.35)) * amp); }
}
inline void addPop(std::vector<float>& x, int at, double amp, Rand& R) {
    const double len = R.u(0.03, 0.08), hz = R.u(30, 90); const int n = std::min((int) x.size() - at, (int) (len * kSr)); if (n <= 0) return;
    for (int i = 0; i < n; ++i) { const double t = i / kSr; const double e = std::sin(M_PI * t / len); x[(size_t) (at + i)] += (float) (std::sin(TAU * hz * t) * e * e * amp); }
}
inline void applyMic(std::vector<float>& x, const Mic& M, Rand& R) {
    const int N = (int) x.size(); if (N == 0) return; float pk = 1e-9f; for (auto v : x) pk = std::max(pk, std::abs(v));
    // the room first (it is in the air before the mic)
    if (M.reverb > 0) { static const double d[4] { 0.0297, 0.0371, 0.0411, 0.0437 }; std::vector<float> wet((size_t) N, 0.0f);
        for (int c = 0; c < 4; ++c) { const int D = (int) (d[c] * kSr); const double fb = std::pow(10.0, -3.0 * d[c] / M.reverbSec); std::vector<float> buf((size_t) D, 0.0f); int p = 0; LP1 damp; damp.set(4500, kSr);
            for (int i = 0; i < N; ++i) { const float out = buf[(size_t) p]; buf[(size_t) p] = (float) (x[(size_t) i] + damp.run(out) * fb); if (++p >= D) p = 0; wet[(size_t) i] += out * 0.25f; } }
        for (int ap = 0; ap < 2; ++ap) { const int D = (int) ((ap ? 0.0017 : 0.005) * kSr); std::vector<float> buf((size_t) D, 0.0f); int p = 0; const float g = 0.7f;
            for (int i = 0; i < N; ++i) { const float b = buf[(size_t) p], in = wet[(size_t) i] + g * b; buf[(size_t) p] = in; wet[(size_t) i] = b - g * in; if (++p >= D) p = 0; } }
        for (int i = 0; i < N; ++i) x[(size_t) i] = (float) (x[(size_t) i] * (1 - M.reverb * 0.5) + wet[(size_t) i] * M.reverb); }
    // the mouth's own noises: clicks, smacks, pops at random places
    for (int k = 0; k < M.clicks; ++k) addClick(x, R.i(0, N - 2000), pk * std::pow(10.0, R.u(-18, -4) / 20.0), R, R.u(0.001, 0.003), R.u(2000, 6000));
    for (int k = 0; k < M.smacks; ++k) { const int at = R.i(0, N - 4000); const double a = pk * std::pow(10.0, R.u(-16, -4) / 20.0); addClick(x, at, a, R, R.u(0.002, 0.004), R.u(1200, 3500)); addClick(x, at + (int) (R.u(0.008, 0.015) * kSr), a * 0.8, R, R.u(0.002, 0.005), R.u(900, 2500)); }
    for (int k = 0; k < M.pops; ++k) addPop(x, R.i(0, N - 6000), pk * std::pow(10.0, R.u(-8, 2) / 20.0), R);
    // the room's steady sounds
    if (M.fanDb > -150) { const double g = pk * std::pow(10.0, M.fanDb / 20.0); LP1 a, b; a.set(400, kSr); b.set(1500, kSr); for (auto& v : x) { const double w = R.n(); v += (float) ((a.run(w) * 3.0 + b.run(w) * 0.6) * g); } }
    if (M.humDb > -150) { const double g = pk * std::pow(10.0, M.humDb / 20.0); for (int i = 0; i < N; ++i) { const double t = i / kSr; x[(size_t) i] += (float) (g * (std::sin(TAU * M.humHz * t) + 0.5 * std::sin(TAU * 2 * M.humHz * t + 1) + 0.35 * std::sin(TAU * 3 * M.humHz * t + 2) + 0.2 * std::sin(TAU * 4 * M.humHz * t))); } }
    if (M.rumbleDb > -150) { const double g = pk * std::pow(10.0, M.rumbleDb / 20.0); LP1 a, b; a.set(25, kSr); b.set(25, kSr); for (auto& v : x) v += (float) (b.run(a.run(R.n())) * 40.0 * g); }
    // the mic
    if (M.lowCut > 0) { HP1 h1, h2; h1.set(M.lowCut, kSr); h2.set(M.lowCut, kSr); for (auto& v : x) v = (float) h2.run(h1.run(v)); }
    if (M.clipGain != 1) for (auto& v : x) v = (float) std::max(-1.0, std::min(1.0, v * M.clipGain / std::max(1e-6f, pk) * 0.9));
    if (M.levelDb != 0) { const double g = std::pow(10.0, M.levelDb / 20.0); for (auto& v : x) v = (float) (v * g); }
    if (M.dc != 0) for (auto& v : x) v += (float) M.dc;
}
// a beatboxer's extras between hits: breaths ("hh": 100..300 ms of soft noise with a slow start), voiced grunts ("uh": 80..200 ms at
// 90..160 Hz, slow start) and lip smacks, dropped into the gaps of >= 300 ms. None of them is a hit: anything found there is an extra.
inline void addMouthNoise(std::vector<float>& x, const std::vector<HitTruth>& hits, Rand& R, double breathDb = -18, double gruntDb = -14, bool smacks = true) {
    float pk = 1e-9f; for (auto v : x) pk = std::max(pk, std::abs(v)); const int N = (int) x.size();
    for (size_t i = 0; i + 1 < hits.size(); ++i) { const double gap = hits[i + 1].t - hits[i].t; if (gap < 0.3) continue;
        const double at = hits[i].t + 0.09 + R.u(0, std::max(0.0, gap - 0.3)); const int a = (int) (at * kSr); const int kind = R.i(0, 2);
        if (kind == 0) { const double len = R.u(0.1, std::min(0.3, gap - 0.2)), rise = R.u(0.03, 0.06); BP bp; bp.set(500, 4000, kSr); const double g = pk * std::pow(10.0, breathDb / 20.0);
            for (int k = 0; k < (int) (len * kSr) && a + k < N; ++k) { const double t = k / kSr; const double e = std::min(1.0, t / rise) * std::min(1.0, (len - t) / 0.04); x[(size_t) (a + k)] += (float) (bp.run(R.n()) * 3.0 * e * g); } }
        else if (kind == 1) { const double len = R.u(0.08, std::min(0.2, gap - 0.2)), hz = R.u(90, 160), rise = R.u(0.025, 0.05); Reso f1, f2; f1.set(500, 80, kSr); f2.set(1100, 120, kSr); LP1 src; src.set(900, kSr); double ph = 0; const double g = pk * std::pow(10.0, gruntDb / 20.0);
            for (int k = 0; k < (int) (len * kSr) && a + k < N; ++k) { const double t = k / kSr; ph += hz / kSr; if (ph >= 1) ph -= 1; const double saw = 2 * ph - 1, s = src.run(saw * saw * saw); const double e = std::min(1.0, t / rise) * std::min(1.0, (len - t) / 0.04);
                x[(size_t) (a + k)] += (float) ((f1.run(s) + f2.run(s)) * 1.5 * e * g); } }
        else if (smacks) { const double g = pk * std::pow(10.0, R.u(-14, -6) / 20.0); addClick(x, a, g, R, R.u(0.002, 0.004), R.u(1200, 3500)); addClick(x, a + (int) (R.u(0.008, 0.015) * kSr), g * 0.8, R, R.u(0.002, 0.005), R.u(900, 2500)); }
    }
}
// the metronome click leaking from the speakers into the mic (the same click Ghost Hum plays: 30 ms sine at 2000 / 1400 Hz), a little
// late (the speaker + air + input latency) and coloured by the laptop speaker
inline void addClickLeak(std::vector<float>& x, double bpm, double origin, double db, double latencyS, Rand& R) {
    float pk = 1e-9f; for (auto v : x) pk = std::max(pk, std::abs(v)); const double g = pk * std::pow(10.0, db / 20.0); const int N = (int) x.size(); const double beat = 60.0 / bpm;
    for (int b = 0; origin + b * beat < N / kSr; ++b) { const bool accent = b % 4 == 0; const double f = accent ? 2000 : 1400; const int a = (int) ((origin + b * beat + latencyS) * kSr); HP1 sp; sp.set(300, kSr);
        for (int i = 0; i < (int) (0.03 * kSr) && a + i < N; ++i) { const double t = i / kSr; const double env = std::min(1.0, t / 0.0005) * (t < 0.002 ? 1.0 : std::exp(-(t - 0.002) / 0.008));
            x[(size_t) (a + i)] += (float) (sp.run(std::sin(TAU * f * t) * env * (accent ? 0.6 : 0.4) * g * (1 + 0.1 * R.n()))); } }
}

} // namespace humtest
