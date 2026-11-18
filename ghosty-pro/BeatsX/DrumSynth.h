#pragma once
// Ghosty Beats: the big built-in library. Every sound here is OUR OWN: synthesised from scratch by the
// models below (layered transient + body + tail, pitch envelopes, shaped noise, saturation, multiband
// transient design, a small room). No samples, no third-party one-shots.
// The 61 classic sounds come from DrumLibrary::build() unchanged (Ghosty Spectrum / Mr Mickens use only those);
// buildBeatsLibrary() appends 260+ more and caches the render on disk so startup stays fast.
#include <cstdlib>
#include "DrumKit.h"
#include <cstring>

namespace gb {

// browser folders (the categories stay the classic names so old projects and Spectrum see the same thing)
inline juce::String folderOf(const juce::String& cat) {
    if (cat == "Kick") return "KICKS";
    if (cat == "Snare") return "SNARES";
    if (cat == "Clap") return "CLAPS";
    if (cat == "Hat" || cat == "Open Hat" || cat == "Pedal Hat") return "HATS";
    if (cat == "Perc" || cat == "Tom") return "PERC";
    if (cat == "Cymbal") return "CYMBALS";
    if (cat == "FX") return "FX";
    if (cat == "808") return "808s";
    if (cat == "Break") return "BREAKS";
    if (cat == "Glitch") return "GLITCH";
    if (cat == "Shared") return "SHARED";   // Documents/Ghosty/Shared Sounds: what Ghosty Spectrum's SEND TO BEATS (and anything else) drops there
    return "SAMPLES";
}
inline const juce::StringArray& browserFolders() { static juce::StringArray f { "KICKS", "SNARES", "CLAPS", "HATS", "PERC", "CYMBALS", "FX", "808s", "GLITCH", "BREAKS", "SHARED" }; return f; }

namespace synth {
using Buf = std::vector<float>;
constexpr double TAU = 6.283185307179586;

struct Rng { uint32_t s; explicit Rng(uint32_t seed) : s(seed * 2654435761u + 12345u) {} float u() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (s & 0xffffff) / 8388608.0f - 1.0f; } };
inline double V(int i, int k) { const double x = (i + 1) * 0.6180339887 + k * 0.3141592; return x - std::floor(x); }   // well-spread 0..1 per variant
inline double lerp(double a, double b, double t) { return a + (b - a) * t; }

// TPT state-variable filter: type 0 low-pass, 1 band-pass, 2 high-pass
struct SVF {
    double s1 = 0, s2 = 0, a1 = 0, a2 = 0, a3 = 0, k = 1;
    void set(double f, double q, double sr) { const double g = std::tan(juce::MathConstants<double>::pi * juce::jlimit(10.0, sr * 0.45, f) / sr); k = 1.0 / juce::jmax(0.05, q); a1 = 1 / (1 + g * (g + k)); a2 = g * a1; a3 = g * a2; }
    double tick(double x, int type) { const double v3 = x - s2, v1 = a1 * s1 + a2 * v3, v2 = s2 + a2 * s1 + a3 * v3; s1 = 2 * v1 - s1; s2 = 2 * v2 - s2; return type == 0 ? v2 : (type == 1 ? v1 : x - k * v1 - v2); }
};
inline void filt(Buf& b, double sr, int type, double f, double q = 0.707) { SVF s; s.set(f, q, sr); for (auto& x : b) x = (float) s.tick(x, type); }
template <class F> inline void filtMod(Buf& b, double sr, int type, double q, F freqAt) { SVF s; for (size_t i = 0; i < b.size(); ++i) { if ((i & 15) == 0) s.set(freqAt((double) i / sr), q, sr); b[i] = (float) s.tick(b[i], type); } }
inline void sat(Buf& b, int type, double drive) {
    if (drive <= 1.001 && type == 0) return;
    const double d = juce::jmax(1.0, drive), norm = type == 0 ? 1.0 / std::tanh(d) : 1.0;
    for (auto& x : b) {
        const double v = x * d; double y;
        switch (type) { case 1: y = juce::jlimit(-1.0, 1.0, v); break;                         // hard clip
                        case 2: y = std::sin(juce::jlimit(-6.0, 6.0, v) * 1.2);  break;        // fold
                        case 3: y = v >= 0 ? std::tanh(v) : std::tanh(v * 0.6) / 0.6 * 0.8; break; // asymmetric tube
                        default: y = std::tanh(v) * norm; }
        x = (float) y;
    }
}
// transient designer: fast vs slow envelope -> gain (attack > 0 = more snap, sustain > 0 = more body)
inline void shape(Buf& b, double sr, double attack, double sustain) {
    const double af = std::exp(-1.0 / (0.0007 * sr)), as = std::exp(-1.0 / (0.02 * sr)), rl = std::exp(-1.0 / (0.06 * sr));
    double ef = 0, es = 0;
    for (auto& x : b) { const double a = std::abs(x); ef = a > ef ? af * ef + (1 - af) * a : rl * ef + (1 - rl) * a; es = a > es ? as * es + (1 - as) * a : rl * es + (1 - rl) * a;
        const double diff = (ef - es) / (ef + 1e-5); const double g = 1 + attack * juce::jmax(0.0, diff) * 3 + sustain * juce::jmax(0.0, -diff) * 2.5; x = (float) (x * juce::jlimit(0.1, 6.0, g)); }
}
inline void mbTransient(Buf& b, double sr, double split, double lowAmt, double highAmt) {   // two-band transient design
    Buf lo = b; filt(lo, sr, 0, split, 0.6); Buf hi(b.size()); for (size_t i = 0; i < b.size(); ++i) hi[i] = b[i] - lo[i];
    if (lowAmt != 0) shape(lo, sr, lowAmt, 0); if (highAmt != 0) shape(hi, sr, highAmt, 0);
    for (size_t i = 0; i < b.size(); ++i) b[i] = lo[i] + hi[i];
}
inline void room(Buf& b, double sr, double size, double decay, double mix) {   // small mono room (4 combs + 2 all-passes)
    if (mix <= 0) return;
    static const int cd[] = { 1116, 1188, 1277, 1356 }, ad[] = { 556, 441 };
    const double sc = sr / 44100.0 * (0.35 + size * 0.9);
    std::vector<Buf> c(4), a(2); int ci[4] {}, ai[2] {}; double lp[4] {};
    for (int k = 0; k < 4; ++k) c[(size_t) k].assign((size_t) juce::jmax(8, (int) (cd[k] * sc)), 0.0f);
    for (int k = 0; k < 2; ++k) a[(size_t) k].assign((size_t) juce::jmax(8, (int) (ad[k] * sc)), 0.0f);
    const double fb = juce::jlimit(0.3, 0.93, 0.6 + decay * 0.33);
    for (auto& x : b) {
        double in = x * 0.25, out = 0;
        for (int k = 0; k < 4; ++k) { auto& d = c[(size_t) k]; const double y = d[(size_t) ci[k]]; lp[k] = y * 0.6 + lp[k] * 0.4; d[(size_t) ci[k]] = (float) (in + lp[k] * fb); ci[k] = (ci[k] + 1) % (int) d.size(); out += y; }
        for (int k = 0; k < 2; ++k) { auto& d = a[(size_t) k]; const double y = d[(size_t) ai[k]]; d[(size_t) ai[k]] = (float) (out + y * 0.5); out = y - out * 0.5; ai[k] = (ai[k] + 1) % (int) d.size(); }
        x = (float) (x + out * mix);
    }
}
inline double env(double t, double att, double hold, double dec) { const double a = att > 0 ? juce::jmin(1.0, t / att) : 1.0; return a * (t < hold ? 1.0 : std::exp(-(t - hold) / juce::jmax(1e-4, dec))); }
inline double sq(double ph) { return std::sin(ph) >= 0 ? 1.0 : -1.0; }
inline double metal(double t, int set, double mul) {   // inharmonic square-partial clusters
    static const double S0[] { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
    static const double S1[] { 263.0, 400.0, 421.0, 531.0, 588.0, 714.0 };
    static const double S2[] { 317.0, 351.0, 460.0, 583.0, 671.0, 902.0 };
    const double* s = set == 1 ? S1 : (set == 2 ? S2 : S0);
    double v = 0; for (int k = 0; k < 6; ++k) v += sq(TAU * s[k] * 1.7 * mul * t); return v / 6.0;
}

// ---------------- models
struct KickP { double f0 = 50, f1 = 200, pt = 0.03, bend2 = 0, len = 0.5, att = 0.0004, hold = 0.03, dec = 0.18, harm = 0, click = 0.3, clickHz = 3200, clickDec = 0.004,
               sub = 0, subDec = 0.35, drive = 2, satType = 0, noiseTail = 0, trans = 0.4, lp = 0, roomMix = 0, glide = 0, postDrive = 0; };
inline void kick(Buf& b, double sr, const KickP& p, Rng& r) {
    const int n = (int) b.size(); double ph = 0;
    for (int i = 0; i < n; ++i) {
        const double t = i / sr;
        double f = p.f0 + (p.f1 - p.f0) * std::exp(-t / p.pt) + p.f0 * p.bend2 * std::exp(-t / 0.09);
        if (p.glide != 0) f *= std::pow(2.0, p.glide * juce::jmin(1.0, t / juce::jmax(0.05, p.len * 0.7)) / 12.0);
        ph += TAU * f / sr;
        b[(size_t) i] = (float) ((std::sin(ph) + p.harm * std::sin(2 * ph + 0.3)) * env(t, p.att, p.hold, p.dec));
    }
    sat(b, p.satType, p.drive);
    if (p.click > 0) {
        Buf c((size_t) n); SVF h; h.set(p.clickHz, 0.9, sr);
        for (int i = 0; i < n; ++i) { const double t = i / sr, e = std::exp(-t / p.clickDec); c[(size_t) i] = (float) (h.tick(r.u(), 1) * e * 1.6 + std::sin(TAU * p.clickHz * 0.5 * t) * std::exp(-t / (p.clickDec * 0.8)) * 0.6); }
        for (int i = 0; i < n; ++i) b[(size_t) i] += (float) (c[(size_t) i] * p.click);
    }
    if (p.sub > 0) { double sp = 0; for (int i = 0; i < n; ++i) { const double t = i / sr; sp += TAU * p.f0 / sr; b[(size_t) i] += (float) (p.sub * std::sin(sp) * juce::jmin(1.0, t / 0.006) * std::exp(-t / p.subDec)); } }
    if (p.noiseTail > 0) { Buf nz((size_t) n); for (auto& x : nz) x = r.u(); filt(nz, sr, 0, 180, 0.7); for (int i = 0; i < n; ++i) b[(size_t) i] += (float) (nz[(size_t) i] * 6 * p.noiseTail * env(i / sr, 0.002, p.hold, p.dec * 1.3)); }
    if (p.trans != 0) mbTransient(b, sr, 220, p.trans * 0.3, p.trans);
    if (p.postDrive > 0) sat(b, 1, 1 + p.postDrive);
    if (p.lp > 0) filt(b, sr, 0, p.lp, 0.6);
    room(b, sr, 0.3, 0.3, p.roomMix);
}

struct SnareP { double tone = 190, ratio = 1.62, bend = 0.25, toneDec = 0.06, toneMix = 0.55, nBp = 4500, nQ = 0.6, nHp = 900, nDec = 0.12, nMix = 1, click = 0.25,
                drive = 1, satType = 0, roomMix = 0, roomSize = 0.4, clapMix = 0, lp = 0, crack = 0; };
inline void snare(Buf& b, double sr, const SnareP& p, Rng& r) {
    const int n = (int) b.size(); Buf nz((size_t) n); for (auto& x : nz) x = r.u();
    filt(nz, sr, 2, p.nHp, 0.7); if (p.nBp > 0) { Buf bp = nz; filt(bp, sr, 1, p.nBp, p.nQ); for (int i = 0; i < n; ++i) nz[(size_t) i] = nz[(size_t) i] * 0.45f + bp[(size_t) i] * 1.2f; }
    double p1 = 0, p2 = 0;
    for (int i = 0; i < n; ++i) {
        const double t = i / sr, bend = 1 + p.bend * std::exp(-t / 0.012);
        p1 += TAU * p.tone * bend / sr; p2 += TAU * p.tone * p.ratio * bend / sr;
        const double body = (std::sin(p1) + 0.5 * std::sin(p2)) * env(t, 0.0003, 0.004, p.toneDec) * p.toneMix;
        const double ns = nz[(size_t) i] * (env(t, 0.0005, 0.002, p.nDec) * p.nMix + p.crack * std::exp(-t / 0.008));
        const double ck = r.u() * std::exp(-t / 0.0015) * p.click;
        b[(size_t) i] = (float) (body + ns + ck);
    }
    if (p.clapMix > 0) {   // clap layer
        Buf c((size_t) n); for (auto& x : c) x = r.u(); filt(c, sr, 1, 1300, 1.0);
        for (int i = 0; i < n; ++i) { const double t = i / sr; double e = 0; for (int h = 0; h < 3; ++h) { const double o = h * 0.009; if (t >= o) e = juce::jmax(e, std::exp(-(t - o) / 0.006)); } if (t > 0.018) e = juce::jmax(e, std::exp(-(t - 0.018) / 0.07) * 0.6); b[(size_t) i] += (float) (c[(size_t) i] * e * p.clapMix * 2.2); }
    }
    sat(b, p.satType, p.drive);
    if (p.lp > 0) filt(b, sr, 0, p.lp, 0.6);
    room(b, sr, p.roomSize, 0.4, p.roomMix);
}

struct ClapP { int hits = 4; double spread = 0.010, jitter = 0.002, bp = 1200, q = 0.9, hitDec = 0.006, tail = 0.12, tailMix = 0.6, hp = 400, lp = 0, drive = 1, roomMix = 0, roomSize = 0.5, bright = 0; };
inline void clap(Buf& b, double sr, const ClapP& p, Rng& r) {
    const int n = (int) b.size(); Buf nz((size_t) n); for (auto& x : nz) x = r.u();
    Buf bp = nz; filt(bp, sr, 1, p.bp, p.q); filt(nz, sr, 2, p.hp, 0.7); for (int i = 0; i < n; ++i) nz[(size_t) i] = bp[(size_t) i] * 1.6f + nz[(size_t) i] * (float) (0.15 + p.bright);
    std::vector<double> on; double o = 0; for (int h = 0; h < p.hits; ++h) { on.push_back(o); o += p.spread + r.u() * p.jitter; }
    const double last = on.back();
    for (int i = 0; i < n; ++i) {
        const double t = i / sr; double e = 0;
        for (size_t h = 0; h < on.size(); ++h) if (t >= on[h]) e = juce::jmax(e, std::exp(-(t - on[h]) / p.hitDec) * (0.8 + 0.2 * (double) h / on.size()));
        if (t > last) e = juce::jmax(e, std::exp(-(t - last) / p.tail) * p.tailMix);
        b[(size_t) i] = (float) (nz[(size_t) i] * e);
    }
    sat(b, 0, p.drive);
    if (p.lp > 0) filt(b, sr, 0, p.lp, 0.6);
    room(b, sr, p.roomSize, 0.45, p.roomMix);
}

struct HatP { int set = 0; double mul = 1, noise = 0.4, hp = 7000, bp = 0, bpQ = 1.2, att = 0, hold = 0, dec = 0.05, drive = 1, lp = 0, ring = 0; };
inline void hat(Buf& b, double sr, const HatP& p, Rng& r) {
    const int n = (int) b.size();
    for (int i = 0; i < n; ++i) { const double t = i / sr; b[(size_t) i] = (float) ((metal(t, p.set, p.mul) * (1 - p.noise) + r.u() * p.noise) * env(t, p.att, p.hold, p.dec)); }
    filt(b, sr, 2, p.hp, 0.7); filt(b, sr, 2, p.hp * 0.8, 0.6);
    if (p.bp > 0) { Buf e = b; filt(e, sr, 1, p.bp, p.bpQ); for (int i = 0; i < n; ++i) b[(size_t) i] += e[(size_t) i] * 1.5f; }
    if (p.ring > 0) for (int i = 0; i < n; ++i) b[(size_t) i] += (float) (p.ring * std::sin(TAU * 9200 * p.mul * i / sr) * env(i / sr, 0.0005, 0, p.dec * 0.8) * 0.3);
    sat(b, 0, p.drive);
    if (p.lp > 0) filt(b, sr, 0, p.lp, 0.6);
}

struct CymP { int set = 0; double mul = 0.73, noise = 0.6, hp = 3500, dec = 1.6, att = 0.0005, bell = 0, bellHz = 3200, drive = 1, shimmer = 0, lp = 0, reverse = 0; };
inline void cymbal(Buf& b, double sr, const CymP& p, Rng& r) {
    const int n = (int) b.size();
    for (int i = 0; i < n; ++i) { const double t = i / sr; const double am = 1 + p.shimmer * std::sin(TAU * 5.3 * t) * 0.3;
        b[(size_t) i] = (float) (((metal(t, p.set, p.mul) * (1 - p.noise) + r.u() * p.noise) * env(t, p.att, 0, p.dec) + p.bell * std::sin(TAU * p.bellHz * t) * std::exp(-t / (p.dec * 0.35))) * am); }
    filt(b, sr, 2, p.hp, 0.7);
    sat(b, 0, p.drive);
    if (p.lp > 0) filt(b, sr, 0, p.lp, 0.6);
    if (p.reverse > 0) std::reverse(b.begin(), b.end());
}

struct DrumP { double f = 200, bend = 0.5, bendT = 0.02, dec = 0.2, m2 = 1.5, m2Mix = 0.3, noise = 0.2, nHz = 3000, nDec = 0.01, drive = 1, att = 0.0003, lp = 0; };
inline void membrane(Buf& b, double sr, const DrumP& p, Rng& r) {
    const int n = (int) b.size(); double p1 = 0, p2 = 0; SVF s; s.set(p.nHz, 1.0, sr);
    for (int i = 0; i < n; ++i) { const double t = i / sr, bend = 1 + p.bend * std::exp(-t / p.bendT);
        p1 += TAU * p.f * bend / sr; p2 += TAU * p.f * p.m2 * bend / sr;
        b[(size_t) i] = (float) ((std::sin(p1) + p.m2Mix * std::sin(p2) * std::exp(-t / (p.dec * 0.4))) * env(t, p.att, 0.002, p.dec) + s.tick(r.u(), 1) * std::exp(-t / p.nDec) * p.noise * 2); }
    sat(b, 0, p.drive); if (p.lp > 0) filt(b, sr, 0, p.lp, 0.6);
}
inline void modal(Buf& b, double sr, double f, const std::vector<std::pair<double, double>>& modes, double dec, double strike, Rng& r) {   // ratio, amp
    const int n = (int) b.size();
    for (int i = 0; i < n; ++i) { const double t = i / sr; double v = 0; int k = 0;
        for (auto& m : modes) { v += m.second * std::sin(TAU * f * m.first * t) * std::exp(-t / (dec / (1 + 0.6 * k))); ++k; }
        b[(size_t) i] = (float) (v * juce::jmin(1.0, t / 0.0004) + r.u() * std::exp(-t / 0.002) * strike); }
}
inline void shaker(Buf& b, double sr, double att, double dec, double hp, double grain, Rng& r) {
    const int n = (int) b.size(); for (int i = 0; i < n; ++i) { const double t = i / sr; const double g = 1 - grain + grain * (r.u() > 0.6f ? 1 : 0.3);
        b[(size_t) i] = (float) (r.u() * g * env(t, att, 0, dec)); }
    filt(b, sr, 2, hp, 0.8); filt(b, sr, 1, hp * 1.4, 0.8);
}

// ================= building blocks for the classic-character packs (docs/research/DRUM-SOUNDS-STUDY.md 2.0). New code only:
// nothing above calls them, so every older sound stays bit-exact.

// brickwall(): a linear-phase windowed-sinc low-pass (Blackman, 2 * half + 1 taps), offline so it adds no delay. -6 dB at fc,
// transition about 5.5 * sr / taps wide (half 96 at 48 kHz: ~1.4 kHz), stop band below -70 dB. The "careful sampler" anti-alias /
// reconstruction filter.
inline void brickwall(Buf& b, double sr, double fc, int half = 96) {
    if (fc <= 0 || fc >= sr * 0.5 || b.empty()) return;
    std::vector<double> h((size_t) (2 * half + 1)); const double wc = fc / sr, pi = juce::MathConstants<double>::pi; double sum = 0;
    for (int k = -half; k <= half; ++k) { const double s = k == 0 ? 2 * wc : std::sin(TAU * wc * k) / (pi * k), w = 0.42 + 0.5 * std::cos(pi * k / half) + 0.08 * std::cos(2 * pi * k / half);
                                          h[(size_t) (k + half)] = s * w; sum += s * w; }
    for (auto& v : h) v /= sum;
    const int n = (int) b.size(); Buf y((size_t) n);
    for (int i = 0; i < n; ++i) { double a = 0; const int k0 = juce::jmax(-half, i - (n - 1)), k1 = juce::jmin(half, i); for (int k = k0; k <= k1; ++k) a += h[(size_t) (k + half)] * b[(size_t) (i - k)]; y[(size_t) i] = (float) a; }
    b.swap(y);
}

// era(): the digital-era stage. Order: pre-filter -> the converter clock (zero-order hold: the newest input sample is held, no
// interpolation, so aliasing / imaging grit stays) -> converter clip -> quantiser (linear or mu-law) -> playback (re-pitched by
// dropping / repeating stored samples, or by moving the clock) -> the amplitude envelope AFTER the DAC (when `env` is given:
// "quantise before the envelope", the decay of early ROM machines, so the tail stays clean) -> the output filter.
// Reference SNR of an ideal N-bit quantiser on a full-scale sine: 6.02 N + 1.76 dB (8-bit 50, 12-bit 74).
struct EraP {
    double rate = 0;        // converter clock, Hz (0 = no rate stage: the quantiser alone)
    int bits = 0;           // quantiser bits (0 = off)
    int law = 0;            // 0 linear, 1 mu-law (mu = 255: 8 bits that keep ~38 dB SNR down the level range)
    double semis = 0;       // re-pitch of the stored sound, semitones
    int pitchMode = 0;      // 0 drop-sample (fixed clock, the read index skips / repeats samples), 1 clock (varispeed: brightness and aliasing move with the tuning)
    double preLP = 0;       // anti-alias filter before the converter, Hz (0 = none)
    int preType = 0;        // 0 a 2-pole SVF, 1 brickwall
    double clip = 0;        // converter drive into a hard clip at full scale (0 = none; 1.4 = a hot input)
    double postLP = 0;      // output filter, Hz (0 = none: the images stay)
    int postType = 0;       // 0 4-pole analogue (two SVFs), 1 brickwall, 2 2-pole SVF
    double postEnd = 0, sweepT = 0.12;   // a swept output filter: postLP falls to postEnd with time constant sweepT (0 = fixed)
};
inline float eraQuant(float x, int bits, int law) {
    if (bits <= 0) return x;
    const double q = std::ldexp(1.0, bits - 1); double v = juce::jlimit(-1.0, 1.0, (double) x);
    constexpr double mu = 255.0; const double lmu = std::log1p(mu);
    if (law == 1) v = (v < 0 ? -1.0 : 1.0) * std::log1p(mu * std::abs(v)) / lmu;
    v = juce::jlimit(-q, q - 1, std::round(v * q)) / q;
    if (law == 1) v = (v < 0 ? -1.0 : 1.0) * std::expm1(std::abs(v) * lmu) / mu;
    return (float) v;
}
inline void era(Buf& b, double sr, const EraP& p, const Buf* env = nullptr) {
    if (b.empty()) return;
    if (p.preLP > 0) { if (p.preType == 1) brickwall(b, sr, p.preLP); else filt(b, sr, 0, p.preLP, 0.707); }
    const double rate = p.rate > 0 ? juce::jmin(p.rate, sr) : sr;
    // 1. the converter: one stored sample per clock tick, the newest input sample (zero-order hold, no smoothing)
    Buf s; s.reserve((size_t) ((double) b.size() * rate / sr) + 2);
    for (int k = 0;; ++k) { const auto i = (size_t) std::floor(k * sr / rate + 1e-9); if (i >= b.size()) break; float v = b[i];
        if (p.clip > 0) v = (float) juce::jlimit(-1.0, 1.0, v * p.clip);
        s.push_back(eraQuant(v, p.bits, p.law)); }
    // 2. playback at the output rate: the held stored sample (re-pitched by the read index, or by the clock)
    const double ratio = std::pow(2.0, p.semis / 12.0);
    const size_t outN = (size_t) juce::jmax(1.0, std::floor((double) b.size() / ratio));
    Buf y(outN, 0.0f);
    for (size_t i = 0; i < outN; ++i) {
        size_t k;
        if (p.pitchMode == 1) k = (size_t) std::floor((double) i * rate * ratio / sr + 1e-9);
        else k = (size_t) std::floor(std::floor((double) i * rate / sr + 1e-9) * ratio + 1e-9);
        y[i] = k < s.size() ? s[k] : 0.0f;
    }
    if (env && !env->empty()) for (size_t i = 0; i < outN; ++i) y[i] *= (*env)[juce::jmin(i, env->size() - 1)];
    if (p.postLP > 0) {
        if (p.postEnd > 0 && p.sweepT > 0) { auto at = [&](double t) { return p.postEnd + (p.postLP - p.postEnd) * std::exp(-t / p.sweepT); }; filtMod(y, sr, 0, 0.54, at); filtMod(y, sr, 0, 1.31, at); }
        else if (p.postType == 1) brickwall(y, sr, p.postLP);
        else if (p.postType == 2) filt(y, sr, 0, p.postLP, 0.707);
        else { filt(y, sr, 0, p.postLP, 0.54); filt(y, sr, 0, p.postLP, 1.31); }
    }
    b.swap(y);
}

// bridgedT(): the analogue resonator voice (8-series kick, toms, congas, claves, rimshot): a 2-pole band-pass at f0 rung by a
// 1 ms pulse instead of an oscillator. Q from the decay: tau = Q / (pi f0). The first ~6 ms ring more than an octave higher
// (the attack shift: punch, not a heard sweep), then a smaller pulse refills it at f0; the pitch 'sighs' down a few % through the
// note; a passive low-pass sets the tone; the pulse leaks through as the click.
struct BridgedP {
    double f0 = 52;            // Hz
    double dec = 0.5;          // the decay knob: time to -40 dB, seconds (clamped 0.01..4 s: "too much feedback makes the decay infinite")
    double attackMul = 2.2, attackMs = 6, refill = 0.4;   // the attack shift and the refill pulse at its end
    double sigh = 0.10, sighT = 0.15;                     // pitch sigh: f = f0 * (1 + sigh * e^-t/sighT): ~3.5 % down between 20 and 150 ms (as the ring settles)
    double drop = 0, dropT = 0.04;                        // an extra pitch drop for toms / congas (fraction above f0 at the strike)
    double pulseMs = 1, click = 0.12;                     // the trigger pulse and how much of it leaks through (x the ring's peak)
    double tone = 0;           // passive low-pass after the resonator, Hz (0 = off)
    double drive = 0;          // soft clip (0 = off; 1 = the voice driven hot)
    double noise = 0, noiseHz = 2500, noiseDec = 0.02;    // a band of noise on the strike (toms)
};
inline void bridgedT(Buf& b, double sr, const BridgedP& p, Rng& r) {
    const int n = (int) b.size(); if (n == 0) return;
    const double tau = juce::jlimit(0.01, 4.0, p.dec) / std::log(100.0);
    const int pw = juce::jmax(1, (int) std::lround(p.pulseMs * sr / 1000.0)), a6 = (int) std::lround(p.attackMs * sr / 1000.0);
    SVF s; Buf pulse((size_t) n, 0.0f); float ringPk = 1e-9f;
    for (int i = 0; i < n; ++i) {
        const double t = i / sr; double f = p.f0 * (1 + p.sigh * std::exp(-t / p.sighT)) * (1 + p.drop * std::exp(-t / juce::jmax(1e-4, p.dropT)));
        if (i < a6) f *= p.attackMul;
        s.set(f, juce::jmax(0.6, juce::MathConstants<double>::pi * f * tau), sr);
        const double x = (i < pw ? 1.0 : 0.0) + (i >= a6 && i < a6 + pw && a6 > 0 ? p.refill : 0.0);
        pulse[(size_t) i] = (float) x;
        b[(size_t) i] = (float) (s.tick(x, 1) * s.k);   // (k * band = the unity-peak band-pass)
        ringPk = juce::jmax(ringPk, std::abs(b[(size_t) i]));
    }
    if (p.click > 0) for (int i = 0; i < n; ++i) b[(size_t) i] += pulse[(size_t) i] * (float) (p.click * ringPk);
    if (p.noise > 0) { SVF h; h.set(p.noiseHz, 0.9, sr); for (int i = 0; i < n; ++i) b[(size_t) i] += (float) (h.tick(r.u(), 1) * std::exp(-(i / sr) / p.noiseDec) * p.noise * ringPk * 2.0); }
    if (p.tone > 0) filt(b, sr, 0, p.tone, 0.6);
    if (p.drive > 0) { const double d = 1 + p.drive * 3; float pk = 1e-9f; for (auto v : b) pk = juce::jmax(pk, std::abs(v)); for (auto& v : b) v = (float) (std::tanh(d * v / pk) / std::tanh(d) * pk); }
    { const double c = std::exp(-TAU * 8.0 / sr); double px = 0, py = 0; for (auto& v : b) { const double y = v - px + c * py; px = v; py = y; v = (float) y; } }   // DC block (the leaked pulse is one-sided)
}
} // namespace synth

// Append the new sounds to `lib` (call after lib.build(), which makes the classic 61). Deterministic.
inline void buildExtendedSounds(DrumLibrary& lib, double sr) {
    using namespace synth;
    auto add = [&](const char* cat, const juce::String& nm, double len, const std::function<void(Buf&, Rng&)>& fn) {
        if (lib.indexOf(nm) >= 0) return;   // names are unique
        auto s = std::make_shared<Sound>(); s->name = nm; s->category = cat; s->rate = sr;
        const int n = juce::jmax(64, (int) (sr * len)); s->data.assign((size_t) n, 0.0f);
        Rng r((uint32_t) nm.hashCode()); fn(s->data, r);
        { const float c = (float) std::exp(-2.0 * juce::MathConstants<double>::pi * 12.0 / sr); float px = 0, py = 0;   // DC block (asymmetric saturation leaves an offset)
          for (auto& v : s->data) { const float y = v - px + c * py; px = v; py = y; v = y; } }
        const int fade = juce::jmin(n / 4, (int) (sr * 0.004)); for (int i = 0; i < fade; ++i) s->data[(size_t) (n - 1 - i)] *= (float) i / (float) fade;   // click-free end
        float mx = 1e-6f; for (auto& v : s->data) { if (!std::isfinite(v)) v = 0; mx = std::max(mx, std::abs(v)); }
        for (auto& v : s->data) v *= 0.89f / mx;
        lib.append(s);
    };
    auto nm2 = [](const char* fam, const char* suf) { return juce::String(fam) + " " + suf; };

    // ================= KICKS (56 new)
    { const char* suf[] = { "Tight", "Warm", "Punch", "Deep", "Snap", "Round", "Driven", "Classic" };
      for (int i = 0; i < 8; ++i) add("Kick", nm2("Club Kick", suf[i]), 0.5, [&, i](Buf& b, Rng& r) { KickP p; p.f0 = lerp(46, 56, V(i, 0)); p.f1 = lerp(170, 280, V(i, 1)); p.pt = lerp(0.022, 0.04, V(i, 2)); p.hold = 0.035; p.dec = lerp(0.13, 0.24, V(i, 3));
          p.click = lerp(0.12, 0.4, V(i, 4)); p.clickHz = lerp(2500, 4500, V(i, 5)); p.drive = lerp(1.5, 3.2, V(i, 6)); p.sub = 0.15; p.trans = 0.5; kick(b, sr, p, r); }); }
    { const char* suf[] = { "Soft", "Wide", "Tall", "Velvet", "Bloom", "Glass", "Pulse", "Driver" };
      for (int i = 0; i < 8; ++i) add("Kick", nm2("Progressive Kick", suf[i]), 0.6, [&, i](Buf& b, Rng& r) { KickP p; p.f0 = lerp(44, 52, V(i, 1)); p.f1 = lerp(150, 230, V(i, 2)); p.pt = lerp(0.03, 0.05, V(i, 3)); p.hold = 0.05; p.dec = lerp(0.18, 0.3, V(i, 4));
          p.click = lerp(0.08, 0.25, V(i, 5)); p.clickHz = lerp(1800, 3200, V(i, 6)); p.drive = lerp(1.4, 2.4, V(i, 0)); p.sub = lerp(0.2, 0.4, V(i, 7)); p.subDec = 0.3; p.trans = 0.35; p.lp = 9000; kick(b, sr, p, r); }); }
    { const char* suf[] = { "Click", "Dry", "Short", "Rubber", "Thump", "Knock", "Steel", "Low" };
      for (int i = 0; i < 8; ++i) add("Kick", nm2("Tech Kick", suf[i]), 0.4, [&, i](Buf& b, Rng& r) { KickP p; p.f0 = lerp(48, 60, V(i, 2)); p.f1 = lerp(200, 340, V(i, 3)); p.pt = lerp(0.012, 0.025, V(i, 4)); p.hold = 0.02; p.dec = lerp(0.08, 0.16, V(i, 5));
          p.click = lerp(0.3, 0.7, V(i, 6)); p.clickHz = lerp(3500, 6500, V(i, 7)); p.clickDec = 0.003; p.drive = lerp(2, 4, V(i, 1)); p.satType = i % 3 == 0 ? 3 : 0; p.trans = 0.8; kick(b, sr, p, r); }); }
    { const char* suf[] = { "Arena", "Stadium", "Festival", "Tok", "Main Stage", "Anthem" };
      for (int i = 0; i < 6; ++i) add("Kick", nm2("Big Room Kick", suf[i]), 0.75, [&, i](Buf& b, Rng& r) { KickP p; p.f0 = lerp(45, 55, V(i, 3)); p.f1 = lerp(320, 520, V(i, 4)); p.pt = lerp(0.008, 0.016, V(i, 5)); p.bend2 = 0.4; p.hold = 0.06; p.dec = lerp(0.25, 0.4, V(i, 6));
          p.harm = 0.25; p.click = 0.5; p.clickHz = 2800; p.drive = lerp(3, 5, V(i, 7)); p.satType = 0; p.noiseTail = lerp(0.05, 0.15, V(i, 1)); p.trans = 0.6; p.roomMix = 0.12; kick(b, sr, p, r); }); }
    { const char* suf[] = { "Slam", "Crunch", "Hit", "Bully", "Rattle", "Knuckle", "Bone", "Concrete" };
      for (int i = 0; i < 8; ++i) add("Kick", nm2("Dubstep Kick", suf[i]), 0.45, [&, i](Buf& b, Rng& r) { KickP p; p.f0 = lerp(50, 62, V(i, 4)); p.f1 = lerp(260, 420, V(i, 5)); p.pt = lerp(0.012, 0.022, V(i, 6)); p.hold = 0.03; p.dec = lerp(0.1, 0.18, V(i, 7));
          p.click = lerp(0.5, 0.9, V(i, 0)); p.clickHz = lerp(3000, 5500, V(i, 1)); p.clickDec = 0.005; p.drive = lerp(3.5, 6, V(i, 2)); p.satType = i % 2 ? 3 : 0; p.trans = 1.0; p.postDrive = lerp(0.2, 0.8, V(i, 3)); kick(b, sr, p, r); }); }
    { const char* suf[] = { "Gritty", "Torn", "Fuzz", "Blown", "Melt", "Rage" };
      for (int i = 0; i < 6; ++i) add("Kick", nm2("Distorted Kick", suf[i]), 0.55, [&, i](Buf& b, Rng& r) { KickP p; p.f0 = lerp(48, 58, V(i, 5)); p.f1 = lerp(220, 360, V(i, 6)); p.pt = 0.02; p.hold = 0.06; p.dec = lerp(0.15, 0.26, V(i, 7));
          p.harm = 0.3; p.click = 0.4; p.drive = lerp(6, 12, V(i, 0)); p.satType = i % 3 == 0 ? 1 : (i % 3 == 1 ? 2 : 3); p.trans = 0.5; p.lp = lerp(5000, 9000, V(i, 1)); kick(b, sr, p, r); }); }
    { const char* suf[] = { "Boom", "Round", "Long", "Bounce" };
      for (int i = 0; i < 4; ++i) add("Kick", nm2("808 Kick", suf[i]), 0.9, [&, i](Buf& b, Rng& r) { KickP p; p.f0 = lerp(44, 54, V(i, 6)); p.f1 = lerp(110, 170, V(i, 7)); p.pt = 0.03; p.hold = 0.05; p.dec = lerp(0.3, 0.55, V(i, 0)); p.click = 0.15; p.drive = 1.6; p.trans = 0.3; kick(b, sr, p, r); }); }
    { const char* suf[] = { "Floor", "Rumble", "Pressure", "Under" };
      for (int i = 0; i < 4; ++i) add("Kick", nm2("Sub Kick", suf[i]), 0.9, [&, i](Buf& b, Rng& r) { KickP p; p.f0 = lerp(38, 46, V(i, 7)); p.f1 = lerp(70, 110, V(i, 0)); p.pt = 0.04; p.hold = 0.08; p.dec = lerp(0.3, 0.5, V(i, 1)); p.click = 0.05; p.drive = 1.3; p.trans = 0; p.lp = 2500; kick(b, sr, p, r); }); }
    { const char* suf[] = { "Screech", "Pitch", "Gated", "Euphoric" };
      for (int i = 0; i < 4; ++i) add("Kick", nm2("Hardstyle Kick", suf[i]), 0.6, [&, i](Buf& b, Rng& r) { KickP p; p.f0 = lerp(52, 60, V(i, 0)); p.f1 = lerp(500, 800, V(i, 1)); p.pt = 0.012; p.bend2 = 0.8; p.hold = 0.18; p.dec = 0.12;
          p.harm = 0.5; p.click = 0.5; p.drive = lerp(10, 18, V(i, 2)); p.satType = 2; p.trans = 0.6; p.lp = 7000; kick(b, sr, p, r); }); }

    // ================= SNARES (36 new)
    { const char* suf[] = { "Crack", "Whip", "Splat", "Shot", "Snap", "Blast", "Break", "Gunfire" };
      for (int i = 0; i < 8; ++i) add("Snare", nm2("Dubstep Snare", suf[i]), 0.45, [&, i](Buf& b, Rng& r) { SnareP p; p.tone = lerp(180, 240, V(i, 0)); p.toneDec = lerp(0.05, 0.09, V(i, 1)); p.toneMix = 0.7; p.nBp = lerp(3000, 6000, V(i, 2)); p.nDec = lerp(0.09, 0.16, V(i, 3));
          p.crack = 0.8; p.click = 0.5; p.drive = lerp(3, 6, V(i, 4)); p.satType = i % 2 ? 3 : 0; p.roomMix = lerp(0.1, 0.3, V(i, 5)); p.roomSize = 0.5; p.clapMix = i % 3 == 0 ? 0.4 : 0; snare(b, sr, p, r); }); }
    { const char* suf[] = { "Tight", "Bright", "Pop", "Flick", "Hollow", "Thin" };
      for (int i = 0; i < 6; ++i) add("Snare", nm2("Trap Snare", suf[i]), 0.35, [&, i](Buf& b, Rng& r) { SnareP p; p.tone = lerp(200, 280, V(i, 1)); p.toneDec = 0.04; p.toneMix = 0.5; p.nBp = lerp(5000, 8000, V(i, 2)); p.nHp = 1500; p.nDec = lerp(0.06, 0.11, V(i, 3)); p.click = 0.4; p.drive = 1.8; snare(b, sr, p, r); }); }
    { const char* suf[] = { "Layer", "Stack", "Glue", "Wide", "Fat", "Room" };
      for (int i = 0; i < 6; ++i) add("Snare", nm2("Clap Snare", suf[i]), 0.5, [&, i](Buf& b, Rng& r) { SnareP p; p.tone = lerp(170, 230, V(i, 2)); p.toneMix = 0.45; p.nDec = lerp(0.08, 0.14, V(i, 3)); p.clapMix = lerp(0.5, 0.9, V(i, 4)); p.drive = 2; p.roomMix = i == 5 ? 0.35 : 0.08; snare(b, sr, p, r); }); }
    { const char* suf[] = { "Wood", "Click", "Ring", "Cross" };
      for (int i = 0; i < 4; ++i) add("Snare", nm2("Rimshot", suf[i]), 0.2, [&, i](Buf& b, Rng& r) { DrumP p; p.f = lerp(380, 520, V(i, 3)); p.bend = 0.2; p.dec = lerp(0.02, 0.05, V(i, 4)); p.m2 = 2.3; p.m2Mix = 0.6; p.noise = 0.5; p.nHz = 3500; p.nDec = 0.006; p.drive = 2; membrane(b, sr, p, r); filt(b, sr, 2, 300); }); }
    { const char* suf[] = { "Tick", "Clipped", "Short", "Metal" };
      for (int i = 0; i < 4; ++i) add("Snare", nm2("Tech Snare", suf[i]), 0.25, [&, i](Buf& b, Rng& r) { SnareP p; p.tone = lerp(220, 300, V(i, 4)); p.toneDec = 0.03; p.nDec = lerp(0.04, 0.07, V(i, 5)); p.nBp = 7000; p.click = 0.6; p.drive = 2.5; p.satType = 1; snare(b, sr, p, r); }); }
    { const char* suf[] = { "Huge", "Hall", "Build", "Clash" };
      for (int i = 0; i < 4; ++i) add("Snare", nm2("Big Room Snare", suf[i]), 0.9, [&, i](Buf& b, Rng& r) { SnareP p; p.tone = lerp(170, 210, V(i, 5)); p.toneMix = 0.6; p.nDec = lerp(0.18, 0.3, V(i, 6)); p.clapMix = 0.5; p.drive = 2.5; p.roomMix = 0.5; p.roomSize = 0.8; snare(b, sr, p, r); }); }
    { const char* suf[] = { "Dusty", "Tape", "Paper", "Muffled" };
      for (int i = 0; i < 4; ++i) add("Snare", nm2("Lofi Snare", suf[i]), 0.4, [&, i](Buf& b, Rng& r) { SnareP p; p.tone = lerp(170, 210, V(i, 6)); p.nDec = lerp(0.1, 0.16, V(i, 7)); p.lp = lerp(3000, 5000, V(i, 0)); p.drive = 2.2; p.satType = 3; snare(b, sr, p, r); }); }

    // ================= CLAPS (28 new)
    { const char* suf[] = { "Club", "Dry", "Bright", "Wide", "Classic", "Crisp", "Party", "Smack" };
      for (int i = 0; i < 8; ++i) add("Clap", nm2("Club Clap", suf[i]), 0.5, [&, i](Buf& b, Rng& r) { ClapP p; p.hits = 3 + i % 2; p.spread = lerp(0.008, 0.013, V(i, 0)); p.bp = lerp(1000, 1700, V(i, 1)); p.q = lerp(0.7, 1.4, V(i, 2)); p.tail = lerp(0.08, 0.16, V(i, 3)); p.drive = 1.6; p.roomMix = lerp(0, 0.2, V(i, 4)); clap(b, sr, p, r); }); }
    { const char* suf[] = { "Tight", "Short", "Pinch", "Micro", "Hard", "Flat" };
      for (int i = 0; i < 6; ++i) add("Clap", nm2("Tight Clap", suf[i]), 0.3, [&, i](Buf& b, Rng& r) { ClapP p; p.hits = 3; p.spread = lerp(0.005, 0.008, V(i, 1)); p.bp = lerp(1400, 2200, V(i, 2)); p.tail = lerp(0.04, 0.07, V(i, 3)); p.drive = 2; clap(b, sr, p, r); }); }
    { const char* suf[] = { "Hall", "Cathedral", "Plate", "Stadium", "Cloud", "Echo" };
      for (int i = 0; i < 6; ++i) add("Clap", nm2("Big Clap", suf[i]), 1.0, [&, i](Buf& b, Rng& r) { ClapP p; p.hits = 4; p.spread = 0.012; p.bp = lerp(900, 1400, V(i, 3)); p.tail = lerp(0.15, 0.25, V(i, 4)); p.roomMix = lerp(0.35, 0.6, V(i, 5)); p.roomSize = lerp(0.6, 1.0, V(i, 6)); clap(b, sr, p, r); }); }
    { const char* suf[] = { "Dusty", "Vinyl", "Old", "Worn" };
      for (int i = 0; i < 4; ++i) add("Clap", nm2("Dusty Clap", suf[i]), 0.5, [&, i](Buf& b, Rng& r) { ClapP p; p.hits = 3; p.bp = lerp(900, 1200, V(i, 4)); p.lp = lerp(3000, 5000, V(i, 5)); p.drive = 2.5; p.tail = 0.11; clap(b, sr, p, r); }); }
    { const char* suf[] = { "Finger", "Dry", "Room", "Bright" };
      for (int i = 0; i < 4; ++i) add("Clap", nm2("Snap", suf[i]), 0.25, [&, i](Buf& b, Rng& r) { ClapP p; p.hits = 2; p.spread = 0.003; p.bp = lerp(2200, 3200, V(i, 5)); p.q = 1.6; p.hitDec = 0.004; p.tail = 0.03; p.tailMix = 0.4; p.roomMix = i == 2 ? 0.3 : 0; clap(b, sr, p, r); }); }

    // ================= HATS (44 new)
    { const char* suf[] = { "Crisp", "Dry", "Silver", "Shimmer", "Tick", "Sizzle", "Glass", "Sharp", "Soft", "Dark", "Bright", "Metal", "Hiss", "Tight", "Short", "Pop", "Chip", "Steel", "Air", "Edge" };
      for (int i = 0; i < 20; ++i) add("Hat", nm2("Closed Hat", suf[i]), 0.12, [&, i](Buf& b, Rng& r) { HatP p; p.set = i % 3; p.mul = lerp(0.85, 1.4, V(i, 0)); p.noise = lerp(0.2, 0.7, V(i, 1)); p.hp = lerp(5500, 9500, V(i, 2)); p.dec = lerp(0.018, 0.05, V(i, 3));
          p.bp = V(i, 4) > 0.5 ? lerp(8000, 12000, V(i, 5)) : 0; p.lp = suf[i][0] == 'D' || suf[i][1] == 'o' ? 9000 : 0; p.ring = V(i, 6) > 0.7 ? 0.5 : 0; hat(b, sr, p, r); }); }
    { const char* suf[] = { "Wash", "Sizzle", "Long", "Breath", "Loose", "Bright", "Dark", "Silver", "Club", "Disco", "Crash", "Splash", "Trash", "Swish" };
      for (int i = 0; i < 14; ++i) add("Open Hat", nm2("Open Hat", suf[i]), 0.9, [&, i](Buf& b, Rng& r) { HatP p; p.set = i % 3; p.mul = lerp(0.85, 1.3, V(i, 1)); p.noise = lerp(0.3, 0.6, V(i, 2)); p.hp = lerp(5000, 8500, V(i, 3)); p.att = 0.001; p.hold = 0.01; p.dec = lerp(0.15, 0.45, V(i, 4)); p.bp = lerp(7000, 11000, V(i, 5)); hat(b, sr, p, r); }); }
    { const char* suf[] = { "Chick", "Foot", "Soft", "Closed", "Half", "Tap" };
      for (int i = 0; i < 6; ++i) add("Pedal Hat", nm2("Pedal Hat", suf[i]), 0.2, [&, i](Buf& b, Rng& r) { HatP p; p.set = i % 3; p.mul = lerp(0.8, 1.1, V(i, 2)); p.noise = 0.5; p.hp = lerp(4500, 6500, V(i, 3)); p.att = 0.004; p.hold = 0.01; p.dec = lerp(0.03, 0.06, V(i, 4)); hat(b, sr, p, r); }); }
    { const char* suf[] = { "Roll", "Needle", "Spark", "Grain" };
      for (int i = 0; i < 4; ++i) add("Hat", nm2("Tick Hat", suf[i]), 0.05, [&, i](Buf& b, Rng& r) { HatP p; p.set = i % 3; p.mul = 1.5; p.noise = 0.5; p.hp = 9000; p.dec = lerp(0.008, 0.015, V(i, 3)); hat(b, sr, p, r); }); }

    // ================= PERC (40 new)
    { const char* suf[] = { "Floor", "Deep", "Body", "Top", "Rack", "Electro Low", "Electro Mid", "Electro High" };
      const double fq[] = { 85, 105, 140, 190, 230, 95, 150, 220 };
      for (int i = 0; i < 8; ++i) add("Tom", nm2("Tom", suf[i]), 0.7, [&, i](Buf& b, Rng& r) { DrumP p; p.f = fq[i]; p.bend = i >= 5 ? 1.2 : 0.45; p.bendT = i >= 5 ? 0.06 : 0.03; p.dec = lerp(0.2, 0.35, V(i, 0)); p.m2 = 1.6; p.m2Mix = i >= 5 ? 0.0 : 0.35; p.noise = 0.25; p.nHz = 2500; p.drive = i >= 5 ? 1.5 : 1.8; membrane(b, sr, p, r); }); }
    { const char* suf[] = { "Egg", "Sand", "Seed", "Fast", "Long", "Brush" };
      for (int i = 0; i < 6; ++i) add("Perc", nm2("Shaker", suf[i]), 0.25, [&, i](Buf& b, Rng& r) { shaker(b, sr, lerp(0.004, 0.03, V(i, 1)), lerp(0.03, 0.09, V(i, 2)), lerp(4000, 7500, V(i, 3)), lerp(0.1, 0.6, V(i, 4)), r); }); }
    { const char* suf[] = { "Click", "Stick", "Hollow", "Tight", "Snappy" };
      for (int i = 0; i < 5; ++i) add("Perc", nm2("Rim", suf[i]), 0.12, [&, i](Buf& b, Rng& r) { DrumP p; p.f = lerp(1200, 2000, V(i, 2)); p.bend = 0.1; p.dec = lerp(0.012, 0.03, V(i, 3)); p.m2 = 2.7; p.m2Mix = 0.5; p.noise = 0.6; p.nHz = 4500; p.nDec = 0.003; membrane(b, sr, p, r); filt(b, sr, 1, p.f * 1.1, 2.5); }); }
    { const char* suf[] = { "Low Open", "High Open", "Slap", "Muted", "Tumba", "Quinto" };
      const double fq[] = { 200, 310, 360, 290, 170, 420 };
      for (int i = 0; i < 6; ++i) add("Perc", nm2("Conga", suf[i]), 0.45, [&, i](Buf& b, Rng& r) { DrumP p; p.f = fq[i]; p.bend = 0.25; p.dec = (i == 3 ? 0.04 : lerp(0.1, 0.2, V(i, 3))); p.m2 = 1.58; p.m2Mix = 0.4; p.noise = i == 2 ? 0.8 : 0.25; p.nHz = i == 2 ? 2500 : 1800; p.nDec = i == 2 ? 0.012 : 0.006; membrane(b, sr, p, r); }); }
    { const char* suf[] = { "Block", "Log", "Knock", "Stick", "Clave" };
      const double fq[] = { 950, 620, 780, 1350, 2400 };
      for (int i = 0; i < 5; ++i) add("Perc", nm2("Wood", suf[i]), 0.15, [&, i](Buf& b, Rng& r) { modal(b, sr, fq[i], { { 1, 1 }, { 2.57, 0.4 }, { 4.1, 0.15 } }, lerp(0.02, 0.05, V(i, 4)), 0.3, r); filt(b, sr, 1, fq[i], 2); }); }
    { const char* suf[] = { "Anvil", "Bell", "Pipe", "Tin", "Chime", "Gong Hit" };
      const double fq[] = { 620, 880, 410, 1500, 1760, 140 };
      for (int i = 0; i < 6; ++i) add("Perc", nm2("Metal", suf[i]), i == 5 ? 1.6 : 0.6, [&, i](Buf& b, Rng& r) { modal(b, sr, fq[i], { { 1, 1 }, { 2.76, 0.6 }, { 5.40, 0.4 }, { 8.93, 0.25 }, { 1.51, 0.3 } }, i == 5 ? 0.9 : lerp(0.12, 0.35, V(i, 5)), 0.4, r); sat(b, 0, 1.5); }); }
    { const char* suf[] = { "Plop", "Bubble", "Drip", "Blip" };
      for (int i = 0; i < 4; ++i) add("Perc", nm2("Perc", suf[i]), 0.2, [&, i](Buf& b, Rng& r) { DrumP p; p.f = lerp(500, 1100, V(i, 6)); p.bend = i == 2 ? -0.5 : 1.5; p.bendT = 0.015; p.dec = lerp(0.03, 0.07, V(i, 7)); p.m2Mix = 0; p.noise = 0.05; membrane(b, sr, p, r); }); }

    // ================= CYMBALS (14 new)
    { const char* suf[] = { "Bright", "Dark", "Wide", "Short" };
      for (int i = 0; i < 4; ++i) add("Cymbal", nm2("Crash", suf[i]), i == 3 ? 1.0 : 2.2, [&, i](Buf& b, Rng& r) { CymP p; p.set = i % 3; p.mul = lerp(0.65, 0.85, V(i, 0)); p.noise = lerp(0.5, 0.7, V(i, 1)); p.hp = lerp(2500, 4500, V(i, 2)); p.dec = i == 3 ? 0.35 : lerp(0.8, 1.4, V(i, 3)); p.lp = suf[i][0] == 'D' ? 8000 : 0; p.shimmer = 0.5; cymbal(b, sr, p, r); }); }
    { const char* suf[] = { "Ping", "Bell", "Wash", "Jazz" };
      for (int i = 0; i < 4; ++i) add("Cymbal", nm2("Ride", suf[i]), 1.6, [&, i](Buf& b, Rng& r) { CymP p; p.set = (i + 1) % 3; p.mul = lerp(1.6, 2.1, V(i, 1)); p.noise = lerp(0.1, 0.3, V(i, 2)); p.hp = 3000; p.dec = lerp(0.6, 1.0, V(i, 3)); p.bell = i == 1 ? 0.9 : 0.3; p.bellHz = lerp(2800, 3600, V(i, 4)); cymbal(b, sr, p, r); }); }
    { const char* suf[] = { "Quick", "Tiny" };
      for (int i = 0; i < 2; ++i) add("Cymbal", nm2("Splash", suf[i]), 0.8, [&, i](Buf& b, Rng& r) { CymP p; p.set = 2; p.mul = 1.1; p.noise = 0.55; p.hp = 5000; p.dec = lerp(0.2, 0.3, V(i, 2)); cymbal(b, sr, p, r); }); }
    { const char* suf[] = { "Trashy", "Gong" };
      for (int i = 0; i < 2; ++i) add("Cymbal", nm2("China", suf[i]), 1.6, [&, i](Buf& b, Rng& r) { CymP p; p.set = 1; p.mul = 0.6; p.noise = 0.4; p.hp = 1800; p.dec = lerp(0.5, 0.9, V(i, 3)); p.drive = 3; cymbal(b, sr, p, r); filt(b, sr, 1, 4000, 0.8); }); }
    { const char* suf[] = { "Swell", "Suck" };
      for (int i = 0; i < 2; ++i) add("Cymbal", nm2("Reverse Cymbal", suf[i]), i ? 1.0 : 2.0, [&, i](Buf& b, Rng& r) { CymP p; p.set = i; p.mul = 0.75; p.noise = 0.6; p.hp = 3000; p.dec = i ? 0.35 : 0.8; p.reverse = 1; cymbal(b, sr, p, r); }); }

    // ================= FX (26 new)
    for (int i = 0; i < 5; ++i) { const char* suf[] = { "Noise", "Tension", "Saw", "Sweep Up", "Lift" };
        add("FX", nm2("Riser", suf[i]), lerp(2.0, 4.0, V(i, 0)), [&, i](Buf& b, Rng& r) { const int n = (int) b.size(); double ph = 0, ph2 = 0;
            for (int k = 0; k < n; ++k) { const double t = (double) k / n; const double f = lerp(150, 1600, t * t) * (1 + i * 0.1); ph += TAU * f / sr; ph2 += TAU * f * 1.01 / sr;
                const double saw = (ph / TAU - std::floor(ph / TAU)) * 2 - 1, saw2 = (ph2 / TAU - std::floor(ph2 / TAU)) * 2 - 1;
                b[(size_t) k] = (float) ((r.u() * (i == 2 ? 0.25 : 0.9) + (saw + saw2) * (i == 0 ? 0.05 : 0.35)) * t * t); }
            filtMod(b, sr, 1, 1.2, [&](double tt) { return lerp(300, 9000, juce::jmin(1.0, tt / (b.size() / sr)) * juce::jmin(1.0, tt / (b.size() / sr))); }); }); }
    for (int i = 0; i < 4; ++i) { const char* suf[] = { "Fall", "Drop", "Sink", "Exhale" };
        add("FX", nm2("Downlifter", suf[i]), lerp(1.5, 3.0, V(i, 1)), [&, i](Buf& b, Rng& r) { const int n = (int) b.size(); double ph = 0;
            for (int k = 0; k < n; ++k) { const double t = (double) k / n; ph += TAU * lerp(900, 40, std::sqrt(t)) / sr; b[(size_t) k] = (float) ((r.u() * 0.7 + std::sin(ph) * (i % 2 ? 0.6 : 0.2)) * (1 - t) * (1 - t)); }
            filtMod(b, sr, 1, 1.0, [&](double tt) { return lerp(8000, 200, juce::jmin(1.0, tt / (b.size() / sr))); }); }); }
    for (int i = 0; i < 5; ++i) { const char* suf[] = { "Boom", "Slam", "Thunder", "Cinematic", "Sub Drop" };
        add("FX", nm2("Impact", suf[i]), 2.5, [&, i](Buf& b, Rng& r) { KickP p; p.f0 = i == 4 ? 30 : 38; p.f1 = i == 4 ? 90 : 160; p.pt = i == 4 ? 0.4 : 0.06; p.hold = 0.1; p.dec = 0.9; p.click = 0.8; p.drive = 2.5; p.noiseTail = i == 2 ? 0.3 : 0.12; p.trans = 0.6; kick(b, sr, p, r);
            Buf nz(b.size()); for (auto& x : nz) x = r.u(); filt(nz, sr, 0, 3000, 0.7); for (size_t k = 0; k < b.size(); ++k) b[k] += (float) (nz[k] * std::exp(-(k / sr) / (i == 3 ? 0.6 : 0.25)) * (i == 4 ? 0.1 : 0.5));
            room(b, sr, 1.0, 0.95, i == 3 ? 0.8 : 0.4); }); }
    for (int i = 0; i < 4; ++i) { const char* suf[] = { "White Up", "White Down", "Phaser", "Wind" };
        add("FX", nm2("Sweep", suf[i]), 2.0, [&, i](Buf& b, Rng& r) { const int n = (int) b.size(); for (int k = 0; k < n; ++k) { const double t = (double) k / n; b[(size_t) k] = (float) (r.u() * std::sin(juce::MathConstants<double>::pi * t)); }
            const double L = n / sr; filtMod(b, sr, i == 3 ? 1 : 1, i == 2 ? 4.0 : 1.5, [&](double tt) { const double x = tt / L; return i == 0 ? lerp(200, 12000, x * x) : (i == 1 ? lerp(12000, 200, std::sqrt(x)) : (i == 2 ? 800 + 4000 * (0.5 + 0.5 * std::sin(TAU * x * 3)) : 500 + 1500 * (0.5 + 0.5 * std::sin(TAU * x * 1.3)))); }); }); }
    for (int i = 0; i < 4; ++i) { const char* suf[] = { "Snare", "Clap", "Kick", "Tom" };
        add("FX", nm2("Reverse", suf[i]), 1.0, [&, i](Buf& b, Rng& r) {
            if (i == 0) { SnareP p; p.nDec = 0.2; p.roomMix = 0.6; p.roomSize = 0.9; p.drive = 2; snare(b, sr, p, r); }
            else if (i == 1) { ClapP p; p.tail = 0.2; p.roomMix = 0.6; p.roomSize = 0.9; clap(b, sr, p, r); }
            else if (i == 2) { KickP p; p.dec = 0.3; p.roomMix = 0.4; kick(b, sr, p, r); }
            else { DrumP p; p.f = 110; p.dec = 0.35; p.drive = 1.6; membrane(b, sr, p, r); room(b, sr, 0.9, 0.8, 0.6); }
            std::reverse(b.begin(), b.end()); }); }
    for (int i = 0; i < 4; ++i) { const char* suf[] = { "Laser Down", "Rise", "Blaster", "Chirp" };
        add("FX", nm2("Zap", suf[i]), 0.4, [&, i](Buf& b, Rng&) { double ph = 0; for (size_t k = 0; k < b.size(); ++k) { const double t = k / sr;
            const double f = i == 1 ? 200 + 4000 * (1 - std::exp(-t * 12)) : (i == 3 ? 2000 + 1500 * std::sin(t * 90) : 4000 * std::exp(-t * (i == 2 ? 25 : 12)) + 120);
            ph += TAU * f / sr; b[k] = (float) ((i == 2 ? sq(ph) * 0.6 : std::sin(ph)) * std::exp(-t * 8)); } }); }

    // ================= 808s (18 new)
    { const char* note[] = { "C", "D", "E", "F", "G", "A" }; const double hz[] = { 32.70, 36.71, 41.20, 43.65, 49.00, 55.00 };
      for (int i = 0; i < 6; ++i) add("808", juce::String("808 Long ") + note[i], 1.8, [&, i](Buf& b, Rng& r) { KickP p; p.f0 = hz[i]; p.f1 = hz[i] * 3.2; p.pt = 0.035; p.hold = 0.1; p.dec = 0.7; p.click = 0.2; p.drive = 1.8; p.trans = 0.3; p.harm = 0.05; kick(b, sr, p, r); });
      for (int i = 0; i < 4; ++i) add("808", juce::String("808 Distorted ") + note[i], 1.4, [&, i](Buf& b, Rng& r) { KickP p; p.f0 = hz[i]; p.f1 = hz[i] * 4; p.pt = 0.03; p.hold = 0.12; p.dec = 0.5; p.click = 0.3; p.drive = lerp(5, 9, V(i, 2)); p.satType = i % 2 ? 3 : 2; p.lp = 6000; kick(b, sr, p, r); });
      for (int i = 0; i < 4; ++i) add("808", juce::String("808 Short ") + note[i + 2], 0.55, [&, i](Buf& b, Rng& r) { KickP p; p.f0 = hz[i + 2]; p.f1 = hz[i + 2] * 4; p.pt = 0.025; p.hold = 0.04; p.dec = 0.16; p.click = 0.35; p.drive = 2.6; p.trans = 0.6; kick(b, sr, p, r); });
      add("808", "808 Slide Down", 1.6, [&](Buf& b, Rng& r) { KickP p; p.f0 = 49; p.f1 = 150; p.hold = 0.15; p.dec = 0.6; p.glide = -7; p.drive = 2.2; kick(b, sr, p, r); });
      add("808", "808 Slide Up", 1.6, [&](Buf& b, Rng& r) { KickP p; p.f0 = 36.7; p.f1 = 120; p.hold = 0.15; p.dec = 0.6; p.glide = 5; p.drive = 2.2; kick(b, sr, p, r); });
      add("808", "808 Growl", 1.4, [&](Buf& b, Rng& r) { KickP p; p.f0 = 41.2; p.f1 = 160; p.hold = 0.1; p.dec = 0.55; p.harm = 0.6; p.drive = 7; p.satType = 2; p.lp = 3500; kick(b, sr, p, r); });
      add("808", "808 Saw", 1.4, [&](Buf& b, Rng&) { double ph = 0; for (size_t k = 0; k < b.size(); ++k) { const double t = k / sr; ph += TAU * (43.65 + 90 * std::exp(-t / 0.03)) / sr; const double s = 2 * (ph / TAU - std::floor(ph / TAU)) - 1;
          b[k] = (float) ((s * 0.4 + std::sin(ph)) * env(t, 0.0005, 0.1, 0.55)); } filt(b, sr, 0, 900, 0.8); sat(b, 0, 2.5); });
    }

    // ================= GLITCH (16 IDM sounds: clicks, crushed sines, formant blips, noise bursts, granular tails)
    {
        auto crush = [](Buf& b, int bits, int hold) { const float q = (float) (1 << (bits - 1)); float h = 0; for (size_t i = 0; i < b.size(); ++i) { if (hold <= 1 || i % (size_t) hold == 0) h = std::round(b[i] * q) / q; b[i] = h; } };
        add("Glitch", "Glitch Kick Bit", 0.3, [&](Buf& b, Rng& r) { KickP p; p.f0 = 45; p.f1 = 180; p.pt = 0.02; p.hold = 0.02; p.dec = 0.12; p.click = 0.5; p.drive = 2.5; kick(b, sr, p, r); crush(b, 7, 5); filt(b, sr, 0, 6000, 0.7); });
        add("Glitch", "Glitch Snare Crunch", 0.18, [&](Buf& b, Rng& r) { for (size_t i = 0; i < b.size(); ++i) { const double t = i / sr; b[i] = (float) (r.u() * env(t, 0.0005, 0.004, 0.035) * (std::fmod(t, 0.02) < 0.012 ? 1.0 : 0.25)); } filt(b, sr, 1, 2200, 0.9); sat(b, 1, 4.0); });
        add("Glitch", "Glitch Hat Grain", 0.09, [&](Buf& b, Rng& r) { for (int g = 0; g < 30; ++g) { const int at = (int) (sr * (g * 0.0026 + std::abs(r.u()) * 0.001)), gl = (int) (sr * 0.0018); for (int k = 0; k < gl && at + k < (int) b.size(); ++k) b[(size_t) (at + k)] += r.u() * (float) std::sin(juce::MathConstants<double>::pi * k / gl) * (float) std::exp(-g * 0.08); } filt(b, sr, 2, 7000, 0.7); });
        add("Glitch", "Glitch Click Soft", 0.03, [&](Buf& b, Rng&) { const int n = (int) (sr * 0.0015); for (int i = 0; i < n && i < (int) b.size(); ++i) b[(size_t) i] = (float) (std::sin(TAU * 3000 * i / sr) * 0.5 * (1 - std::cos(TAU * i / n))); });
        add("Glitch", "Glitch Click Hard", 0.03, [&](Buf& b, Rng& r) { SVF f; f.set(6000, 6.0, sr); for (size_t i = 0; i < b.size(); ++i) b[i] = (float) f.tick(i < (size_t) (sr * 0.0005) ? r.u() : 0.0, 1); });
        add("Glitch", "Glitch Blip Formant", 0.06, [&](Buf& b, Rng&) { double ph = 0; for (size_t i = 0; i < b.size(); ++i) { const double t = i / sr; ph += TAU * 140 / sr; b[i] = (float) ((std::fmod(ph, TAU) < 0.6 ? 1.0 : -0.1) * env(t, 0.0005, 0.01, 0.02)); } Buf a = b, c = b; filt(a, sr, 1, 700, 6); filt(c, sr, 1, 1200, 6); for (size_t i = 0; i < b.size(); ++i) b[i] = a[i] + 0.7f * c[i]; });
        add("Glitch", "Glitch Blip Vowel", 0.09, [&](Buf& b, Rng&) { double ph = 0; for (size_t i = 0; i < b.size(); ++i) { const double t = i / sr; ph += TAU * 110 / sr; b[i] = (float) ((std::fmod(ph, TAU) < 0.5 ? 1.0 : -0.1) * env(t, 0.0005, 0.02, 0.03)); }
            filtMod(b, sr, 1, 5.0, [&](double tt) { return lerp(300, 900, juce::jmin(1.0, tt / 0.07)); }); });
        add("Glitch", "Glitch Burst Noise", 0.03, [&](Buf& b, Rng& r) { const int n = (int) (sr * 0.025), f = (int) (sr * 0.002); for (int i = 0; i < n && i < (int) b.size(); ++i) b[(size_t) i] = r.u() * (float) juce::jmin(1.0, juce::jmin(i, n - i) / (double) f); });
        add("Glitch", "Glitch Burst Pink", 0.07, [&](Buf& b, Rng& r) { const int n = (int) (sr * 0.06); for (int i = 0; i < n && i < (int) b.size(); ++i) b[(size_t) i] = r.u() * (float) juce::jmin(1.0, (n - i) / (double) n); filt(b, sr, 0, 2500, 0.6); filt(b, sr, 2, 150, 0.7); });
        add("Glitch", "Glitch Crush Sine Low", 0.14, [&](Buf& b, Rng&) { for (size_t i = 0; i < b.size(); ++i) { const double t = i / sr; b[i] = (float) (std::sin(TAU * 110 * t) * env(t, 0.0005, 0.03, 0.04)); } crush(b, 3, 1); filt(b, sr, 0, 3000, 0.7); });
        add("Glitch", "Glitch Crush Sine High", 0.08, [&](Buf& b, Rng&) { for (size_t i = 0; i < b.size(); ++i) { const double t = i / sr; b[i] = (float) (std::sin(TAU * 1200 * t) * env(t, 0.0003, 0.01, 0.025)); } crush(b, 4, 4); });
        add("Glitch", "Glitch Grain Tail", 0.32, [&](Buf& b, Rng& r) { for (int g = 0; g < 60; ++g) { const double f = 500 * std::pow(2.0, r.u()); const int at = (int) (sr * (g * 0.005 + std::abs(r.u()) * 0.002)), gl = (int) (sr * 0.01); for (int k = 0; k < gl && at + k < (int) b.size(); ++k) b[(size_t) (at + k)] += (float) (std::sin(TAU * f * k / sr) * 0.5 * (1 - std::cos(TAU * k / gl)) * std::exp(-g * 0.05)); } filt(b, sr, 0, 5000, 0.7); });
        add("Glitch", "Glitch Grain Shimmer", 0.3, [&](Buf& b, Rng& r) { for (int g = 0; g < 60; ++g) { const double f = 2000 * std::pow(2.0, r.u() * 0.5); const int at = (int) (sr * (g * 0.0045 + std::abs(r.u()) * 0.002)), gl = (int) (sr * 0.008); for (int k = 0; k < gl && at + k < (int) b.size(); ++k) b[(size_t) (at + k)] += (float) (std::sin(TAU * f * k / sr) * 0.5 * (1 - std::cos(TAU * k / gl)) * (0.2 + 0.8 * g / 60.0)); } filt(b, sr, 2, 1000, 0.7); });
        add("Glitch", "Glitch Tick Metal", 0.02, [&](Buf& b, Rng& r) { modal(b, sr, 3100, { { 1, 1 }, { 1.52, 0.6 }, { 2.31, 0.3 } }, 0.004, 0.3, r); });
        add("Glitch", "Glitch Zip", 0.07, [&](Buf& b, Rng&) { double ph = 0; for (size_t i = 0; i < b.size(); ++i) { const double t = i / sr; ph += TAU * (200 + 7800 * std::exp(-t * 60)) / sr; b[i] = (float) (std::sin(ph) * env(t, 0.0003, 0.01, 0.02)); } });
        add("Glitch", "Glitch Sub Pulse", 0.12, [&](Buf& b, Rng& r) { for (size_t i = 0; i < b.size(); ++i) { const double t = i / sr; b[i] = (float) (sq(TAU * 55 * t) * env(t, 0.0005, 0.04, 0.03) + r.u() * std::exp(-t / 0.001) * 0.4); } filt(b, sr, 0, 300, 0.8); });
    }

    // ================= BREAKS (8 original 2-bar breakbeats, played by our own drums through our own room)
    struct BreakDef { const char* name; double bpm, swing; const char *kick, *snare, *ghost, *hat, *open; const char *kSnd, *sSnd, *hSnd, *oSnd; };
    static const BreakDef breaks[] {
        { "Break Ghost Funk", 104, 0.12, "x.........x.....x.x.......x.....", "....x.......x.......x.......x...", ".......o.o....o........o.o..o..o", "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.", "..............x...............x.", "Club Kick Warm", "Lofi Snare Paper", "Closed Hat Soft", "Open Hat Loose" },
        { "Break Rolling", 172, 0.0, "x.........x.......x.......x.....", "....x.......x.......x..x....x...", "..o....o.o...o.o..o....o..o..o.o", "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.", "......................x.........", "Tech Kick Knock", "Dubstep Snare Snap", "Closed Hat Crisp", "Open Hat Breath" },
        { "Break Dusty Swing", 90, 0.3, "x.......x.x.....x.....x...x.....", "....x.......x.......x.......x...", "...o......o..o.....o..o....o..o.", "x.xxx.x.x.xxx.x.x.xxx.x.x.xxx.x.", "..............x...............x.", "Boom Bap Kick", "Lofi Snare Dusty", "Closed Hat Hiss", "Open Hat Dark" },
        { "Break Tight Machine", 128, 0.0, "x.....x...x.....x.....x...x..x..", "....x.......x.......x.......x...", "", "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", "..x...x...x...x...x...x...x...x.", "Tech Kick Click", "Tech Snare Clipped", "Closed Hat Tick", "Open Hat Silver" },
        { "Break Shuffle", 120, 0.4, "x.....x...x.....x.....x.x.......", "....x.......x.......x.......x...", ".o.....o.o....o..o.....o.o..o..o", "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.", "", "Club Kick Round", "Clap Snare Glue", "Closed Hat Glass", "Open Hat Swish" },
        { "Break Halftime", 140, 0.0, "x.....x...x.......x.......x.....", "........x...............x.......", "..o....o.....o.o...o.......o..o.", "x.x.x.x.x.x.x.x.x.x.x.x.x.x.xxxx", "..............x.................", "Dubstep Kick Hit", "Dubstep Snare Crack", "Closed Hat Tight", "Open Hat Trash" },
        { "Break Glitch Hop", 110, 0.15, "x..x......x..x..x.....x...x.....", "....x.......x.......x.......x.x.", ".......o.......o...o...o......o.", "x.x.x.x.x.x.x.x.x.xxx.x.x.x.xxxx", "......x...............x.........", "Distorted Kick Fuzz", "Trap Snare Hollow", "Closed Hat Chip", "Open Hat Club" },
        { "Break Breakbeat", 135, 0.05, "x.x.......x.......x.x.....x.....", "....x.......x.......x.......x...", ".......o...o.o.........o..o..o.o", "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.", "..........x...............x.....", "Club Kick Punch", "Clap Snare Fat", "Closed Hat Sharp", "Open Hat Bright" },
    };
    for (const auto& B : breaks) {
        const double beat = 60.0 / B.bpm, stepT = beat / 4.0, len = beat * 8.0;
        auto s = std::make_shared<Sound>(); s->name = B.name; s->category = "Break"; s->rate = sr; s->beats = 8;
        const int n = (int) (sr * (len + 0.02)); s->data.assign((size_t) n, 0.0f);
        Rng r((uint32_t) juce::String(B.name).hashCode());
        auto place = [&](const char* pat, const char* snd, float gain, bool choke) {
            if (!pat || !*pat) return; const auto src = lib.byName(snd); if (!src) return;
            for (int i = 0; i < 32 && pat[i]; ++i) {
                const char c = pat[i]; if (c == '.') continue;
                const float v = (c == 'o' ? 0.32f : (c == 'X' ? 1.0f : 0.8f)) * gain * (0.92f + 0.08f * r.u());
                const double t0 = i * stepT + ((i & 1) ? B.swing * stepT * 0.5 : 0.0) + r.u() * 0.002;
                const int at = juce::jmax(0, (int) (t0 * sr)); int m = (int) src->data.size();
                if (choke) { int nx = i + 1; while (nx < 32 && pat[nx] == '.') ++nx; m = juce::jmin(m, (int) (((nx - i) * stepT + 0.01) * sr)); }
                for (int k = 0; k < m && at + k < n; ++k) s->data[(size_t) (at + k)] += src->data[(size_t) k] * v * (choke && k > m - 200 ? (float) (m - k) / 200.0f : 1.0f);
            }
        };
        place(B.kick, B.kSnd, 1.0f, false); place(B.snare, B.sSnd, 0.85f, false); place(B.ghost, B.sSnd, 0.85f, false);
        place(B.hat, B.hSnd, 0.35f, true); place(B.open, B.oSnd, 0.3f, false);
        Buf& d = s->data; sat(d, 0, 1.6); room(d, sr, 0.35, 0.3, 0.12);
        float mx = 1e-6f; for (auto& v : d) { if (!std::isfinite(v)) v = 0; mx = std::max(mx, std::abs(v)); } for (auto& v : d) v *= 0.89f / mx;
        lib.append(s);
    }
}

// Per-sound playback trims: within a browser folder every sound's loudest 200 ms sits within +-6 dB of the folder median, so
// swapping a sound in a kit keeps the level. Data stays peak-normalised (Spectrum / Mickens read the data only); v1 projects ignore trims.
inline float loudest200ms(const Sound& s) {
    const int n = (int) s.data.size(), w = juce::jmax(1, (int) (s.rate * 0.2)), hop = juce::jmax(1, (int) (s.rate * 0.05)); double best = 0;
    for (int a = 0; a < n; a += hop) { double e = 0; int c = 0; for (int i = a; i < a + w && i < n; ++i) { e += (double) s.data[(size_t) i] * s.data[(size_t) i]; ++c; } if (c > 0) best = juce::jmax(best, e / c); }
    return 20.0f * std::log10((float) std::sqrt(best) + 1e-9f);
}
// a pack sound that reads louder than its folder at the same 200 ms level (the DIRTY 808's growl: about 1.5 dB more LUFS) sits that much lower
inline float packLevelOffsetDb(const juce::String& name) { return name == "DRILL 808 DIRTY" ? -1.5f : 0.0f; }
inline void applyTrims(DrumLibrary& lib) {
    std::map<juce::String, std::vector<float>> byFolder;
    for (auto& s : lib.sounds()) if (s->category != "Break" && !isPackSound(s->name)) byFolder[folderOf(s->category)].push_back(loudest200ms(*s));   // (the DRILL / LATIN / classic packs are levelled TO the medians, never move them: older trims stay bit-exact)
    std::map<juce::String, float> median; for (auto& kv : byFolder) { auto v = kv.second; std::sort(v.begin(), v.end()); median[kv.first] = v[v.size() / 2]; }
    for (auto& sp : lib.sounds()) { if (sp->category == "Break") continue; auto* s = const_cast<Sound*>(sp.get());   // freshly built, exclusively ours
        const float lim = isClassicPackSound(s->name) ? 9.0f : 6.0f;   // (the classic packs' short machine hits may need a little more than the older +-6 dB)
        s->trim = juce::Decibels::decibelsToGain(juce::jlimit(-lim, lim, median[folderOf(s->category)] - loudest200ms(*s) + packLevelOffsetDb(s->name))); }
}

} // namespace gb
#include "ClassicPacks.h"   // the classic-character packs (needs synth:: above)
namespace gb {

// The library a Beats instance uses: cached render on disk (Documents/Ghosty/Ghosty Beats/Cache), else built now.
// The cache is not a free sample folder: after the 8-byte header (magic, version) the whole payload (names, lengths, the
// 16-bit PCM) is XORed with a rolling key stream (xorshift32 seeded from the version + rate). Not cryptography - just not
// something a "raw PCM import" or an audio editor opens. Reading undoes it, so the sound is bit-exact to the plain v7 cache.
inline juce::File beatsCacheDir() {
    if (const char* e = std::getenv("GHOSTY_BEATS_CACHE_DIR")) if (*e) { juce::File d (juce::String::fromUTF8 (e)); d.createDirectory(); return d; }   // tests / parallel builds: a private cache
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Ghosty").getChildFile("Ghosty Beats").getChildFile("Cache");
}
inline void beatsCacheScramble(void* data, size_t n, int version, int rate) {   // its own inverse
    uint32_t x = (uint32_t) juce::String("ghostblock|beats-cache|" + juce::String(version) + "|" + juce::String(rate)).hashCode() ^ 0x9e3779b9u;
    if (x == 0) x = 0x6d2b79f5u;
    auto* p = static_cast<uint8_t*>(data);
    for (size_t i = 0; i < n; ++i) {
        if ((i & 3) == 0) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; }
        p[i] ^= (uint8_t) (x >> (8 * (i & 3)));
    }
}
inline std::shared_ptr<DrumLibrary> loadBeatsLibrary(double sr, bool useCache = true) {
    static constexpr int kVersion = 8;   // bump whenever a recipe changes (8 = the payload is scrambled; same sounds as 7)
    auto lib = std::make_shared<DrumLibrary>(); lib->build(sr);   // the classic 61, bit-identical to before
    const int classic = (int) lib->sounds().size();
    const auto f = beatsCacheDir().getChildFile("library-v" + juce::String(kVersion) + "-" + juce::String((int) sr) + ".bin");
    auto q16 = [](float v) { return (int16_t) juce::jlimit(-32767.0f, 32767.0f, std::round(v * 32767.0f)); };
    if (useCache && f.existsAsFile()) {
        juce::MemoryBlock raw; std::vector<SoundPtr> got; bool ok = false;
        if (f.loadFileAsData(raw) && raw.getSize() > 12) {
            juce::MemoryInputStream head(raw.getData(), 8, false);
            if (head.readInt() == 0x47424c42 && head.readInt() == kVersion) {
                beatsCacheScramble(static_cast<uint8_t*>(raw.getData()) + 8, raw.getSize() - 8, kVersion, (int) sr);
                juce::MemoryInputStream in(static_cast<const uint8_t*>(raw.getData()) + 8, raw.getSize() - 8, false);
                const int count = in.readInt(); ok = count > 0 && count < 5000;
                for (int i = 0; ok && i < count; ++i) {
                    auto s = std::make_shared<Sound>(); s->name = in.readString(); s->category = in.readString(); s->rate = sr; s->beats = in.readDouble();
                    const int n = in.readInt(); if (n <= 0 || n > (int) (sr * 30)) { ok = false; break; }
                    s->data.resize((size_t) n); std::vector<int16_t> d((size_t) n);
                    if (in.read(d.data(), n * 2) != n * 2) { ok = false; break; }
                    for (int k = 0; k < n; ++k) s->data[(size_t) k] = d[(size_t) k] / 32767.0f;
                    got.push_back(s);
                }
            }
        }
        if (ok) { for (auto& s : got) lib->append(s); appendDrillSounds(*lib, sr); appendLatinSounds(*lib, sr); appendClassicSounds(*lib, sr); applyTrims(*lib); return lib; }
    }
    DrumLibrary ext; buildExtendedSounds(ext, sr);
    // the cache holds 16-bit copies: quantise now too, so a fresh build sounds exactly like a cached load
    std::vector<SoundPtr> news;
    for (auto& s : ext.sounds()) { if (lib->indexOf(s->name) >= 0) continue; auto c = std::make_shared<Sound>(*s); for (auto& v : c->data) v = q16(v) / 32767.0f; news.push_back(c); lib->append(c); }
    juce::ignoreUnused(classic);
    if (useCache) {
        const auto dir = beatsCacheDir(); dir.createDirectory();
        for (auto& old : dir.findChildFiles(juce::File::findFiles, false, "library-v*.bin"))   // older cache versions (v7 = plain PCM): gone
            if (old.getFileName() != f.getFileName() && old.getFileName().fromFirstOccurrenceOf("library-v", false, false).upToFirstOccurrenceOf("-", false, false).getIntValue() < kVersion) old.deleteFile();
        juce::MemoryOutputStream body;
        body.writeInt((int) news.size());
        for (auto& s : news) { body.writeString(s->name); body.writeString(s->category); body.writeDouble(s->beats); body.writeInt((int) s->data.size());
            std::vector<int16_t> d(s->data.size()); for (size_t k = 0; k < d.size(); ++k) d[k] = q16(s->data[k]); body.write(d.data(), d.size() * 2); }
        juce::MemoryBlock payload = body.getMemoryBlock(); beatsCacheScramble(payload.getData(), payload.getSize(), kVersion, (int) sr);
        const auto tmp = f.getSiblingFile(f.getFileName() + ".tmp"); tmp.deleteFile();
        { juce::FileOutputStream out(tmp);
          if (out.openedOk()) { out.writeInt(0x47424c42); out.writeInt(kVersion); out.write(payload.getData(), payload.getSize()); } }   // NOT-EXPORT: the instrument's own sound cache, scrambled (not a WAV / PCM folder)
        if (tmp.getSize() > 16) tmp.moveFileTo(f); else tmp.deleteFile();
    }
    appendDrillSounds(*lib, sr);   // after the cache is written: the DRILL pack is always synthesised (fast), never cached
    appendLatinSounds(*lib, sr);   // ... and the LATIN pack after it
    appendClassicSounds(*lib, sr); // ... then the classic-character packs (ClassicPacks.h)
    applyTrims(*lib);
    return lib;
}

} // namespace gb
