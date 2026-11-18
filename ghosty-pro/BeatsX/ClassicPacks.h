#pragma once
// Ghosty Beats: the CLASSIC-CHARACTER packs (docs/research/DRUM-SOUNDS-STUDY.md sections 2-3). Every sound is OUR OWN synthesis of a
// machine's or an era's character (resonator circuits, oscillator banks, shaped noise, the era() converter stage). No samples of
// anything, ever. Product names are generic (classicPackNames() in DrumKit.h): no brand, model or producer names.
// Appended after the LATIN pack in classicPackNames() order, never cached, levelled TO the folder medians (applyTrims), so every
// older sound, trim and index stays exactly as it was.
#include "DrumSynth.h"
#include <atomic>
#include <thread>

namespace gb::classic {
using synth::Buf; using synth::Rng; using synth::TAU;

struct Ctx { double sr = 48000; bool bypassBits = false; };   // bypassBits: era() without its quantiser (tests: the noise floor)
using Fn = void (*)(Buf&, Rng&, const Ctx&);
struct Def { const char* cat; const char* name; double len; Fn fn; };

inline void E(Buf& b, const Ctx& c, synth::EraP p, const Buf* env = nullptr) { if (c.bypassBits) p.bits = 0; synth::era(b, c.sr, p, env); }
inline void normTo(Buf& b, float pk = 0.99f) { float m = 1e-9f; for (auto v : b) m = std::max(m, std::abs(v)); for (auto& v : b) v *= pk / m; }
inline void glue(Buf& b, double d) { float m = 1e-9f; for (auto v : b) m = std::max(m, std::abs(v)); for (auto& v : b) v = (float) (std::tanh(d * v / m) / std::tanh(d)); }   // the peak glued to the body (lower crest)
inline Buf expEnv(size_t n, double sr, double att, double hold, double dec) { Buf e(n); for (size_t i = 0; i < n; ++i) e[i] = (float) synth::env(i / sr, att, hold, dec); return e; }

// the AUTHENTIC 8-series metal: six square oscillators at the published frequencies (205.3, 304.4, 369.6, 522.7, 540, 800 Hz) x mul.
// A NEW set: synth::metal() keeps its x1.7 tuning for every older hat (bit-exact); this one has none (mul 1 = the circuit's tuning).
inline double metal8(double t, double mul) { static const double S[] { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 }; double v = 0; for (double f : S) v += synth::sq(TAU * f * mul * t); return v / 6.0; }
// the 8-series metal voice: the six squares summed into two band-passes (~3440 + ~7100 Hz, Q ~2) that lift the upper overtones
inline void metal8Voice(Buf& b, double sr, double mul, double noise, Rng& r) {
    for (size_t i = 0; i < b.size(); ++i) b[i] = (float) (metal8(i / sr, mul) * (1 - noise) + r.u() * noise);
    Buf a = b, c = b; synth::filt(a, sr, 1, 3440, 2.0); synth::filt(c, sr, 1, 7100, 2.0); for (size_t i = 0; i < b.size(); ++i) b[i] = a[i] + c[i] * 1.2f;
}
// a resonator ring (no pulse click, no attack shift) for snares / rims / claves
inline Buf ring(size_t n, double sr, double f0, double t40, Rng& r, double attackMul = 1, double sigh = 0) {
    Buf b(n, 0.0f); synth::BridgedP p; p.f0 = f0; p.dec = t40; p.attackMul = attackMul; p.sigh = sigh; p.click = 0; synth::bridgedT(b, sr, p, r); normTo(b, 1.0f); return b;
}

// ================= ANALOG BOOM (8-series character, study 2.1): bridged-T kick + toms / congas / claves / rim, two-resonator snare,
// burst clap, the authentic six-oscillator metal for hats / cymbal / cowbell
inline void abKick(Buf& b, Rng& r, const Ctx& c) { synth::BridgedP p; p.f0 = 50; p.dec = 0.8; p.tone = 3200; p.click = 0.14; synth::bridgedT(b, c.sr, p, r); }
inline void abLong(Buf& b, Rng& r, const Ctx& c) { synth::BridgedP p; p.f0 = 47; p.dec = 3.4; p.tone = 2200; p.click = 0.08; p.sigh = 0.12; p.drive = 0.25; synth::bridgedT(b, c.sr, p, r); }
inline void abSnare(Buf& b, Rng& r, const Ctx& c) {
    const double sr = c.sr; const size_t n = b.size();
    const Buf r1 = ring(n, sr, 200, 0.24, r, 1.0, 0.02), r2 = ring(n, sr, 350, 0.17, r, 1.0, 0.02);
    Buf nz(n); for (auto& v : nz) v = r.u(); synth::filt(nz, sr, 2, 1800, 0.7); synth::filt(nz, sr, 2, 1800, 0.7); synth::filt(nz, sr, 0, 5200, 0.7); synth::filt(nz, sr, 0, 7000, 0.7);   // the snappy: high-passed noise
    for (size_t i = 0; i < n; ++i) { const double t = i / sr; b[i] = (float) ((r1[i] + 0.6 * r2[i]) * 0.6 + nz[i] * 1.6 * synth::env(t, 0.0004, 0.003, 0.045)); }
    glue(b, 2.6);
}
inline void abClap(Buf& b, Rng& r, const Ctx& c) { synth::ClapP p; p.hits = 4; p.spread = 0.0105; p.jitter = 0; p.bp = 1000; p.q = 1.0; p.hitDec = 0.0042; p.tail = 0.17; p.tailMix = 0.42; p.hp = 600; synth::clap(b, c.sr, p, r); }
inline void abHat(Buf& b, Rng& r, const Ctx& c, double dec) {
    metal8Voice(b, c.sr, 1.0, 0.05, r); synth::filt(b, c.sr, 2, 6500, 0.7); synth::filt(b, c.sr, 2, 6500, 0.6); synth::filt(b, c.sr, 0, 12500, 0.7);
    for (size_t i = 0; i < b.size(); ++i) b[i] *= (float) synth::env(i / c.sr, 0.0003, 0.0, dec);
}
inline void abClosedHat(Buf& b, Rng& r, const Ctx& c) { abHat(b, r, c, 0.0125); }
inline void abOpenHat(Buf& b, Rng& r, const Ctx& c) { abHat(b, r, c, 0.095); }
inline void abCymbal(Buf& b, Rng& r, const Ctx& c) {   // three bands, each with its own envelope: 3440 (0.4 s), 7100 (0.9 s), a resonant 10.5 kHz high band (1.4 s)
    const double sr = c.sr; const size_t n = b.size(); Buf src(n); metal8Voice(src, sr, 1.0, 0.1, r);
    Buf lo = src, mid = src, hi = src; synth::filt(lo, sr, 1, 3440, 1.6); synth::filt(mid, sr, 1, 7100, 1.6); synth::filt(hi, sr, 2, 10500, 2.2); synth::filt(hi, sr, 2, 8000, 0.7);
    for (size_t i = 0; i < n; ++i) { const double t = i / sr; b[i] = (float) (lo[i] * 0.55 * std::exp(-t / 0.09) + mid[i] * 0.8 * std::exp(-t / 0.2) + hi[i] * 1.0 * std::exp(-t / 0.31)); }
    synth::filt(b, sr, 2, 2500, 0.7);
}
inline void abCowbell(Buf& b, Rng& r, const Ctx& c) {   // squares at 540 + 800 Hz on one two-stage contour, a body band-pass, a breath of noise
    const double sr = c.sr;
    for (size_t i = 0; i < b.size(); ++i) { const double t = i / sr; const double e = t < 0.015 ? 1.0 - 0.7 * t / 0.015 : 0.3 * std::exp(-(t - 0.015) / 0.12);
        b[i] = (float) ((synth::sq(TAU * 540 * t) + synth::sq(TAU * 800 * t) * 0.9 + r.u() * 0.04) * e * juce::jmin(1.0, t / 0.0005)); }
    Buf a = b; synth::filt(a, sr, 1, 1150, 1.1); Buf h = b; synth::filt(h, sr, 1, 2600, 1.5); for (size_t i = 0; i < b.size(); ++i) b[i] = a[i] + h[i] * 0.35f;
    synth::filt(b, sr, 0, 5000, 0.7); glue(b, 1.8);
}
inline void abDrum(Buf& b, Rng& r, const Ctx& c, double f0, double t40, double drop, double dropT, double noise) {
    synth::BridgedP p; p.f0 = f0; p.dec = t40; p.attackMul = 1.35; p.attackMs = 3; p.refill = 0.3; p.sigh = 0.03; p.drop = drop; p.dropT = dropT; p.click = 0.05; p.tone = 6000; p.noise = noise; p.noiseHz = 2200; p.noiseDec = 0.015;
    synth::bridgedT(b, c.sr, p, r);
}
inline void abTomLo(Buf& b, Rng& r, const Ctx& c) { abDrum(b, r, c, 90, 0.75, 0.25, 0.04, 0.1); }
inline void abTomMid(Buf& b, Rng& r, const Ctx& c) { abDrum(b, r, c, 130, 0.65, 0.25, 0.04, 0.1); }
inline void abTomHi(Buf& b, Rng& r, const Ctx& c) { abDrum(b, r, c, 180, 0.55, 0.25, 0.04, 0.1); }
inline void abCongaLo(Buf& b, Rng& r, const Ctx& c) { abDrum(b, r, c, 220, 0.6, 0.1, 0.015, 0); }
inline void abCongaMid(Buf& b, Rng& r, const Ctx& c) { abDrum(b, r, c, 330, 0.5, 0.1, 0.015, 0); }
inline void abCongaHi(Buf& b, Rng& r, const Ctx& c) { abDrum(b, r, c, 470, 0.42, 0.1, 0.015, 0); }
inline void abClaves(Buf& b, Rng& r, const Ctx& c) { b = ring(b.size(), c.sr, 2500, 0.115, r); }
inline void abRimshot(Buf& b, Rng& r, const Ctx& c) {   // two resonators (~480 Hz + ~1.7 kHz, short) + a 1 ms high-passed noise tick, then HP 300 Hz
    const double sr = c.sr; const size_t n = b.size(); const Buf a = ring(n, sr, 480, 0.085, r), h = ring(n, sr, 1700, 0.06, r);
    Buf tk(n, 0.0f); for (size_t i = 0; i < n && i < (size_t) (0.001 * sr); ++i) tk[i] = r.u(); synth::filt(tk, sr, 2, 3000, 0.7);
    for (size_t i = 0; i < n; ++i) b[i] = a[i] * 0.8f + h[i] * 0.7f + tk[i] * 1.2f;
    synth::filt(b, sr, 2, 300, 0.7); glue(b, 2.4);
}
inline void abMaracas(Buf& b, Rng& r, const Ctx& c) { synth::shaker(b, c.sr, 0.002, 0.036, 7000, 0.4, r); }

// ================= ANALOG PUNCH (9-series character, study 2.2): swept-sine kick with click + saturation, two-partial snare with a
// snappy and a plain noise branch, toms, rimshot; hats / ride / crash = metallic partials + noise through a 6-bit ~32 kHz converter
// with the envelope after the DAC (the grain sits on the loud part, the tail stays clean)
inline void apKickWith(Buf& b, Rng& r, const Ctx& c, double dec) { synth::KickP p; p.f0 = 54; p.f1 = 240; p.pt = 0.02; p.hold = 0.02; p.dec = dec; p.harm = 0.1; p.click = 0.35; p.clickHz = 3500; p.clickDec = 0.003;
    p.noiseTail = 0.05; p.drive = 2.2; p.satType = 0; p.trans = 0.5; synth::kick(b, c.sr, p, r); }
inline void apKick(Buf& b, Rng& r, const Ctx& c) { apKickWith(b, r, c, 0.2); }
inline void apKickLong(Buf& b, Rng& r, const Ctx& c) { apKickWith(b, r, c, 0.42); }
inline void apSnare(Buf& b, Rng& r, const Ctx& c) {
    synth::SnareP p; p.tone = 180; p.ratio = 1.833; p.bend = 0.15; p.toneDec = 0.08; p.toneMix = 0.7; p.nBp = 0; p.nHp = 2500; p.nDec = 0.075; p.nMix = 0.9; p.click = 0.2; p.drive = 1.6; p.lp = 11000;
    synth::snare(b, c.sr, p, r);
    Buf plain(b.size()); for (auto& v : plain) v = r.u(); synth::filt(plain, c.sr, 0, 9000, 0.7);   // the unfiltered noise branch on its own short contour
    for (size_t i = 0; i < b.size(); ++i) b[i] += (float) (plain[i] * 0.35 * std::exp(-(i / c.sr) / 0.012));
}
inline void apClap(Buf& b, Rng& r, const Ctx& c) { synth::ClapP p; p.hits = 4; p.spread = 0.008; p.jitter = 0; p.bp = 1300; p.q = 1.0; p.hitDec = 0.0035; p.tail = 0.12; p.tailMix = 0.42; p.hp = 900; synth::clap(b, c.sr, p, r); }
// metallic partials + noise, high-passed, rendered SUSTAINED, then the 6-bit converter with the envelope after the DAC
inline void apMetal(Buf& b, Rng& r, const Ctx& c, int set, double mul, double noise, double hp, double bp, double att, double dec, double rate, double post) {
    const double sr = c.sr; const size_t n = b.size();
    for (size_t i = 0; i < n; ++i) b[i] = (float) (synth::metal(i / sr, set, mul) * (1 - noise) + r.u() * noise);
    synth::filt(b, sr, 2, hp, 0.7); synth::filt(b, sr, 2, hp * 0.8, 0.6); if (bp > 0) { Buf e = b; synth::filt(e, sr, 1, bp, 0.9); for (size_t i = 0; i < n; ++i) b[i] += e[i] * 1.5f; }
    normTo(b); const Buf en = expEnv(n, sr, att, 0.0, dec);
    synth::EraP E6; E6.rate = rate; E6.bits = 6; E6.postLP = post; E(b, c, E6, &en);
}
inline void apClosedHat(Buf& b, Rng& r, const Ctx& c) { apMetal(b, r, c, 2, 1.25, 0.45, 7500, 10000, 0.0003, 0.021, 32000, 14000); }
inline void apOpenHat(Buf& b, Rng& r, const Ctx& c) { apMetal(b, r, c, 2, 1.25, 0.45, 7000, 9500, 0.0005, 0.11, 32000, 14000); }
inline void apRide(Buf& b, Rng& r, const Ctx& c) { apMetal(b, r, c, 1, 1.9, 0.25, 3200, 6500, 0.0005, 0.42, 32000, 13000); }
inline void apCrash(Buf& b, Rng& r, const Ctx& c) { apMetal(b, r, c, 0, 0.8, 0.6, 3000, 7000, 0.001, 0.45, 32000, 13000); }
inline void apTom(Buf& b, Rng& r, const Ctx& c, double f) { synth::DrumP p; p.f = f; p.bend = 0.45; p.bendT = 0.035; p.dec = 0.13; p.m2 = 1.5; p.m2Mix = 0.2; p.noise = 0.25; p.nHz = 2200; p.nDec = 0.01; p.drive = 1.6; synth::membrane(b, c.sr, p, r); }
inline void apTomLo(Buf& b, Rng& r, const Ctx& c) { apTom(b, r, c, 100); }
inline void apTomMid(Buf& b, Rng& r, const Ctx& c) { apTom(b, r, c, 140); }
inline void apTomHi(Buf& b, Rng& r, const Ctx& c) { apTom(b, r, c, 190); }
inline void apRimshot(Buf& b, Rng& r, const Ctx& c) {
    const double sr = c.sr; for (size_t i = 0; i < b.size(); ++i) { const double t = i / sr; b[i] = (float) ((std::sin(TAU * 500 * t) * 0.8 + std::sin(TAU * 1720 * t) * 0.7) * std::exp(-t / 0.012) + r.u() * std::exp(-t / 0.0012) * 0.9); }
    synth::filt(b, sr, 2, 350, 0.7); synth::sat(b, 0, 1.8);
}

// ================= DIGITAL EIGHTIES (study 2.3-2.4: the first sampled "real drum" machines, 8-bit at ~28-35 kHz with companding):
// our own acoustic-drum MODELS through an 8-bit mu-law converter; tight, dry, close; toms tuned by the clock (a lower tom is darker)
inline synth::EraP d80(double rate = 28000, bool mu = true) { synth::EraP p; p.rate = rate; p.bits = 8; p.law = mu ? 1 : 0; p.preLP = rate * 0.45; p.postLP = 12000; return p; }
inline void d8Kick(Buf& b, Rng& r, const Ctx& c) { synth::KickP p; p.f0 = 58; p.f1 = 110; p.pt = 0.012; p.hold = 0.012; p.dec = 0.036; p.harm = 0.2; p.click = 0.45; p.clickHz = 2200; p.noiseTail = 0.1; p.drive = 1.8; p.trans = 0.6; p.lp = 9000;
    synth::kick(b, c.sr, p, r); glue(b, 2.2); normTo(b); E(b, c, d80()); }
inline void d8Snare(Buf& b, Rng& r, const Ctx& c) { synth::SnareP p; p.tone = 185; p.ratio = 1.5; p.toneDec = 0.06; p.nBp = 3500; p.nQ = 0.7; p.nHp = 700; p.nDec = 0.058; p.crack = 0.45; p.click = 0.35; p.drive = 2.2; p.roomMix = 0.08;
    synth::snare(b, c.sr, p, r); glue(b, 2.4); normTo(b); E(b, c, d80()); }
inline void d8Clap(Buf& b, Rng& r, const Ctx& c) { synth::ClapP p; p.hits = 5; p.spread = 0.0078; p.jitter = 0.0005; p.bp = 1400; p.q = 1.0; p.hitDec = 0.0028; p.tail = 0.08; p.tailMix = 0.3; p.hp = 700; p.drive = 1.4;   // a group of people, not a machine
    synth::clap(b, c.sr, p, r); normTo(b); E(b, c, d80()); }
// hats / cabasa / tambourine: rendered sustained, the 8-bit (linear, ROM-style) converter at 35 kHz, the decay after the DAC (a clean tail)
inline void d8Metal(Buf& b, Rng& r, const Ctx& c, double dec, double hp) {
    const double sr = c.sr; for (size_t i = 0; i < b.size(); ++i) b[i] = (float) (synth::metal(i / sr, 2, 1.3) * 0.3 + r.u() * 0.7);
    synth::filt(b, sr, 2, hp, 0.7); synth::filt(b, sr, 2, hp * 0.85, 0.6); normTo(b); const Buf en = expEnv(b.size(), sr, 0.0003, 0.0, dec); E(b, c, d80(35000, false), &en); }
inline void d8ClosedHat(Buf& b, Rng& r, const Ctx& c) { d8Metal(b, r, c, 0.014, 6000); }
inline void d8OpenHat(Buf& b, Rng& r, const Ctx& c) { d8Metal(b, r, c, 0.1, 5800); }
inline void d8Cabasa(Buf& b, Rng& r, const Ctx& c) { const double sr = c.sr; for (size_t i = 0; i < b.size(); ++i) { const double g = 0.4 + 0.6 * (r.u() > 0.2f ? 1 : 0.3); b[i] = (float) (r.u() * g); }
    synth::filt(b, sr, 2, 5000, 0.8); synth::filt(b, sr, 1, 7000, 0.8); normTo(b); const Buf en = expEnv(b.size(), sr, 0.003, 0.004, 0.05); E(b, c, d80(35000, false), &en); }
inline void d8Tambourine(Buf& b, Rng& r, const Ctx& c) { const double sr = c.sr; Buf j(b.size()); synth::modal(j, sr, 5500, { { 1, 1 }, { 1.41, 0.7 }, { 1.93, 0.5 } }, 0.12, 0.2, r);
    for (size_t i = 0; i < b.size(); ++i) b[i] = (float) (r.u() * 0.5 + j[i] * 0.6); synth::filt(b, sr, 2, 4500, 0.7); normTo(b);
    const Buf en = expEnv(b.size(), sr, 0.001, 0.01, 0.085); E(b, c, d80(35000, false), &en); }
// toms: one drum model, tuned by the converter clock (varispeed): the low tom is the same sample played slower and darker
inline void d8Tom(Buf& b, Rng& r, const Ctx& c, double semis) { const double ratio = std::pow(2.0, semis / 12.0);   // (the source rendered at the length the re-pitch needs)
    Buf src((size_t) (b.size() * ratio)); synth::DrumP p; p.f = 130; p.bend = 0.15; p.bendT = 0.05; p.dec = 0.16; p.m2 = 1.48; p.m2Mix = 0.35; p.noise = 0.12; p.nHz = 2500; p.nDec = 0.012; p.drive = 1.4;
    synth::membrane(src, c.sr, p, r); normTo(src); synth::EraP e = d80(); e.semis = semis; e.pitchMode = 1; E(src, c, e); b.assign(src.begin(), src.begin() + (long) juce::jmin(src.size(), b.size())); b.resize(b.size(), 0.0f); }
inline void d8TomLo(Buf& b, Rng& r, const Ctx& c) { d8Tom(b, r, c, -5); }
inline void d8TomMid(Buf& b, Rng& r, const Ctx& c) { d8Tom(b, r, c, 0); }
inline void d8TomHi(Buf& b, Rng& r, const Ctx& c) { d8Tom(b, r, c, 4); }
inline void d8Conga(Buf& b, Rng& r, const Ctx& c, double f) { synth::DrumP p; p.f = f; p.bend = 0.05; p.bendT = 0.01; p.dec = 0.12; p.m2 = 1.58; p.m2Mix = 0.3; p.noise = 0.25; p.nHz = 2600; p.nDec = 0.006; synth::membrane(b, c.sr, p, r); normTo(b); E(b, c, d80()); }
inline void d8CongaHi(Buf& b, Rng& r, const Ctx& c) { d8Conga(b, r, c, 340); }
inline void d8CongaLo(Buf& b, Rng& r, const Ctx& c) { d8Conga(b, r, c, 240); }
inline void d8Cowbell(Buf& b, Rng& r, const Ctx& c) { const double sr = c.sr; for (size_t i = 0; i < b.size(); ++i) { const double t = i / sr; b[i] = (float) ((synth::sq(TAU * 600 * t) + synth::sq(TAU * 880 * t) * 0.85) * (t < 0.012 ? 1 - 0.6 * t / 0.012 : 0.4 * std::exp(-(t - 0.012) / 0.09)) + r.u() * 0.02); }
    synth::filt(b, sr, 1, 1200, 1.0); glue(b, 1.8); normTo(b); E(b, c, d80()); }
inline void d8Claves(Buf& b, Rng& r, const Ctx& c) { synth::modal(b, c.sr, 2300, { { 1, 1 }, { 2.7, 0.15 } }, 0.04, 0.15, r); glue(b, 1.6); normTo(b); E(b, c, d80()); }
inline void d8Rim(Buf& b, Rng& r, const Ctx& c) { synth::modal(b, c.sr, 520, { { 1, 1 }, { 2.6, 0.6 } }, 0.03, 0.6, r); synth::filt(b, c.sr, 2, 300, 0.7); glue(b, 2.2); normTo(b); E(b, c, d80()); }

// ================= CRUNCH TWELVE (study 2.5: the 12-bit 26.04 kHz sampling drum machine): our dusty source drums rendered "fast" (+5
// semitones, the 45-rpm trick), stored at 26.04 kHz / 12 bits with a hot converter, played back 5 semitones down by DROPPING samples
// (no interpolation) with no reconstruction filter (the images stay, so it is brighter), out through per-channel filters:
// channels "1-2" swept (the kick), "3-6" fixed, "7-8" open
inline synth::EraP c12(int channel) { synth::EraP p; p.rate = 26040; p.bits = 12; p.semis = -5; p.pitchMode = 0; p.clip = 1.3;
    if (channel == 0) { p.postLP = 9000; p.postEnd = 2500; p.sweepT = 0.06; } else if (channel == 1) p.postLP = 6000; else if (channel == 2) p.postLP = 8000; return p; }   // 3 = open
inline void c12Render(Buf& b, Rng& r, const Ctx& c, int channel, void (*src)(Buf&, Rng&, double)) {
    const double up = std::pow(2.0, 5 / 12.0); Buf x((size_t) (b.size() / up) + 1, 0.0f); src(x, r, c.sr / up);   // rendered at 1/up of the rate = played back +5 st
    normTo(x); E(x, c, c12(channel)); b.assign(x.begin(), x.begin() + (long) juce::jmin(x.size(), b.size())); b.resize(b.size(), 0.0f); }
inline void c12KickSrc(Buf& b, Rng& r, double sr) { synth::KickP p; p.f0 = 52; p.f1 = 150; p.pt = 0.02; p.hold = 0.025; p.dec = 0.2; p.click = 0.3; p.drive = 2.5; p.trans = 0.4; synth::kick(b, sr, p, r);
    for (size_t i = 0; i < b.size(); ++i) b[i] += r.u() * 0.01f; }   // the record's noise floor (-40 dB)
inline void c12SnareSrc(Buf& b, Rng& r, double sr) { synth::SnareP p; p.tone = 200; p.ratio = 1.62; p.toneDec = 0.07; p.nBp = 2500; p.nQ = 0.6; p.nDec = 0.1; p.crack = 0.35; p.drive = 2.0; p.roomMix = 0.2; synth::snare(b, sr, p, r); glue(b, 2.2);
    for (size_t i = 0; i < b.size(); ++i) b[i] += r.u() * 0.008f; }
inline void c12ClapSrc(Buf& b, Rng& r, double sr) { synth::ClapP p; p.hits = 3; p.spread = 0.009; p.bp = 1100; p.tail = 0.1; p.drive = 2; synth::clap(b, sr, p, r); }
inline void c12HatSrc(Buf& b, Rng& r, double sr) { synth::HatP p; p.set = 0; p.mul = 1; p.noise = 0.6; p.hp = 5000; p.dec = 0.021; p.lp = 9000; synth::hat(b, sr, p, r); }
inline void c12OpenSrc(Buf& b, Rng& r, double sr) { synth::HatP p; p.set = 0; p.mul = 1; p.noise = 0.6; p.hp = 4800; p.att = 0.001; p.dec = 0.11; p.lp = 9000; synth::hat(b, sr, p, r); }
inline void c12RimSrc(Buf& b, Rng& r, double sr) { synth::modal(b, sr, 520, { { 1, 1 }, { 2.7, 0.5 } }, 0.035, 0.5, r); synth::filt(b, sr, 2, 300, 0.7); glue(b, 2.0); }
inline void c12ShakerSrc(Buf& b, Rng& r, double sr) { synth::shaker(b, sr, 0.006, 0.05, 5000, 0.5, r); }
inline void c12TomSrc(Buf& b, Rng& r, double sr) { synth::DrumP p; p.f = 120; p.bend = 0.3; p.bendT = 0.03; p.dec = 0.13; p.m2Mix = 0.3; p.noise = 0.2; p.drive = 1.8; synth::membrane(b, sr, p, r); }
inline void c12Kick(Buf& b, Rng& r, const Ctx& c) { c12Render(b, r, c, 0, c12KickSrc); }
inline void c12KickOpen(Buf& b, Rng& r, const Ctx& c) { c12Render(b, r, c, 3, c12KickSrc); }
inline void c12Snare(Buf& b, Rng& r, const Ctx& c) { c12Render(b, r, c, 3, c12SnareSrc); }
inline void c12SnareFiltered(Buf& b, Rng& r, const Ctx& c) { c12Render(b, r, c, 1, c12SnareSrc); }
inline void c12Clap(Buf& b, Rng& r, const Ctx& c) { c12Render(b, r, c, 2, c12ClapSrc); }
inline void c12Hat(Buf& b, Rng& r, const Ctx& c) { c12Render(b, r, c, 2, c12HatSrc); }
inline void c12OpenHat(Buf& b, Rng& r, const Ctx& c) { c12Render(b, r, c, 2, c12OpenSrc); }
inline void c12Rim(Buf& b, Rng& r, const Ctx& c) { c12Render(b, r, c, 3, c12RimSrc); }
inline void c12Shaker(Buf& b, Rng& r, const Ctx& c) { c12Render(b, r, c, 2, c12ShakerSrc); }
inline void c12Tom(Buf& b, Rng& r, const Ctx& c) { c12Render(b, r, c, 1, c12TomSrc); }

// ================= SWING TWELVE (study 2.6: the 12-bit 40 kHz production centre): rounder, warmer sources; a careful converter
// (anti-alias + reconstruction brickwalls at ~18 kHz bandwidth) driven hot for "warmth and loudness"
inline synth::EraP s12(double semis = 0) { synth::EraP p; p.rate = 40000; p.bits = 12; p.semis = semis; p.pitchMode = 1; p.preLP = 17600; p.preType = 1; p.postLP = 17600; p.postType = 1; p.clip = 1.5; return p; }
inline void s12Kick(Buf& b, Rng& r, const Ctx& c) { synth::KickP p; p.f0 = 50; p.f1 = 170; p.pt = 0.022; p.hold = 0.03; p.dec = 0.26; p.click = 0.3; p.clickHz = 2600; p.harm = 0.12; p.drive = 2; p.trans = 0.5; p.lp = 8000;
    synth::kick(b, c.sr, p, r); normTo(b); E(b, c, s12()); }
inline void s12Snare(Buf& b, Rng& r, const Ctx& c) { synth::SnareP p; p.tone = 195; p.ratio = 1.6; p.toneDec = 0.09; p.toneMix = 0.65; p.nBp = 2000; p.nQ = 0.6; p.nHp = 600; p.nDec = 0.11; p.crack = 0.3; p.drive = 1.8; p.roomMix = 0.15; p.lp = 11000;
    synth::snare(b, c.sr, p, r); glue(b, 2.0); normTo(b); E(b, c, s12()); }
inline void s12Clap(Buf& b, Rng& r, const Ctx& c) { synth::ClapP p; p.hits = 4; p.spread = 0.009; p.bp = 1250; p.tail = 0.12; p.drive = 1.6; p.lp = 10000; synth::clap(b, c.sr, p, r); normTo(b); E(b, c, s12()); }
inline void s12Hat(Buf& b, Rng& r, const Ctx& c) { synth::HatP p; p.set = 1; p.mul = 1.15; p.noise = 0.55; p.hp = 6000; p.dec = 0.018; p.lp = 12000; synth::hat(b, c.sr, p, r); normTo(b); E(b, c, s12()); }
inline void s12OpenHat(Buf& b, Rng& r, const Ctx& c) { synth::HatP p; p.set = 1; p.mul = 1.15; p.noise = 0.55; p.hp = 5800; p.att = 0.001; p.dec = 0.12; p.lp = 12000; synth::hat(b, c.sr, p, r); normTo(b); E(b, c, s12()); }
inline void s12Rim(Buf& b, Rng& r, const Ctx& c) { synth::modal(b, c.sr, 480, { { 1, 1 }, { 2.65, 0.55 } }, 0.04, 0.45, r); synth::filt(b, c.sr, 2, 300, 0.7); glue(b, 2.0); normTo(b); E(b, c, s12()); }
inline void s12Shaker(Buf& b, Rng& r, const Ctx& c) { synth::shaker(b, c.sr, 0.008, 0.05, 5200, 0.35, r); normTo(b); E(b, c, s12()); }
inline void s12Tom(Buf& b, Rng& r, const Ctx& c) { synth::DrumP p; p.f = 110; p.bend = 0.25; p.bendT = 0.04; p.dec = 0.13; p.m2Mix = 0.3; p.noise = 0.15; p.drive = 1.6; synth::membrane(b, c.sr, p, r); normTo(b); E(b, c, s12()); }
inline void s12Conga(Buf& b, Rng& r, const Ctx& c) { synth::DrumP p; p.f = 300; p.bend = 0.08; p.bendT = 0.012; p.dec = 0.14; p.m2 = 1.6; p.m2Mix = 0.25; p.noise = 0.3; p.nHz = 2400; p.nDec = 0.006; synth::membrane(b, c.sr, p, r); normTo(b); E(b, c, s12()); }

// ================= modern processed kits (study 3.2-3.6): layering, saturation, transient shaping, short rooms
// a long tuned 808 (TRAP-HIPHOP-STYLE 3: the pitch envelope +semis falling over ~35 ms, a long decay, a short click, a warm
// multiband drive: the sub below 90 Hz stays clean, the band above is soft-clipped)
inline void bass808(Buf& b, Rng& r, double sr, double f0, double penv, double penvMs, double t60, double drive, double click, double h2 = 0.12, double atk = 0.0012) {
    const size_t n = b.size(); double ph = 0; const double tau = penvMs * 0.001 / 2.3, dk = 6.9 / t60;
    Buf cl(n, 0.0f); for (size_t i = 0; i < n && i < (size_t) (0.004 * sr); ++i) cl[i] = (float) (r.u() * std::exp(-(double) i / sr * 900.0)); synth::filt(cl, sr, 2, 1800, 0.7);
    for (size_t i = 0; i < n; ++i) { const double t = i / sr, f = f0 * std::exp2(penv / 12.0 * std::exp(-t / tau)); ph += TAU * f / sr;
        b[i] = (float) ((std::sin(ph) + h2 * std::sin(2 * ph)) * std::exp(-t * dk) * juce::jmin(1.0, t / atk) + cl[i] * click); }
    Buf lo = b; synth::filt(lo, sr, 0, 90, 0.6); Buf hi = b; synth::filt(hi, sr, 2, 90, 0.6); const double dg = 1 + drive * 6;
    for (size_t i = 0; i < n; ++i) { const double x = hi[i] * dg, y = x >= 0 ? std::tanh(x) : std::tanh(x * 0.8) / 0.8; b[i] = (float) (lo[i] + y / std::tanh(dg) * (0.3 + drive * 0.9)); }
    synth::filt(b, sr, 0, 1800, 0.707); synth::filt(b, sr, 2, 28, 0.707);
}
// ---- TRAP GOLD (study 3.2): a short hard click-led kick that sits on top of a long tuned 808 family, a crisp clipped snare and a clap
// with a short room (the beat-3 hero), tiny bright hats so 1/32 rolls stay clear, a tubular-bell perc
inline void tgKick(Buf& b, Rng& r, const Ctx& c) { synth::KickP p; p.f0 = 55; p.f1 = 260; p.pt = 0.012; p.hold = 0.015; p.dec = 0.035; p.click = 1.7; p.clickHz = 4200; p.clickDec = 0.005; p.drive = 3; p.trans = 0.9;
    synth::kick(b, c.sr, p, r); Buf layer(b.size()); synth::KickP q; q.f0 = 62; q.f1 = 400; q.pt = 0.006; q.hold = 0.004; q.dec = 0.018; q.click = 0.6; q.clickHz = 6500; q.drive = 2; synth::kick(layer, c.sr, q, r);   // the top layer
    for (size_t i = 0; i < b.size(); ++i) b[i] += layer[i] * 0.7f; synth::filt(b, c.sr, 2, 150, 0.7); synth::filt(b, c.sr, 2, 120, 0.6); glue(b, 3.2); }   // (high-passed: it sits above the 808)
inline void tg808(Buf& b, Rng& r, const Ctx& c, double f0) { bass808(b, r, c.sr, f0, 14, 35, 2.4, 0.35, 0.12); }
inline void tg808D(Buf& b, Rng& r, const Ctx& c) { tg808(b, r, c, 36.71); }
inline void tg808F(Buf& b, Rng& r, const Ctx& c) { tg808(b, r, c, 43.65); }
inline void tg808G(Buf& b, Rng& r, const Ctx& c) { tg808(b, r, c, 49.0); }
inline void tg808A(Buf& b, Rng& r, const Ctx& c) { tg808(b, r, c, 55.0); }
inline void tgSnare(Buf& b, Rng& r, const Ctx& c) { synth::SnareP p; p.tone = 210; p.ratio = 1.7; p.toneDec = 0.05; p.nBp = 4800; p.nQ = 0.7; p.nHp = 1500; p.nDec = 0.07; p.click = 0.35; p.drive = 2.5; p.satType = 1; p.clapMix = 0.35;
    synth::snare(b, c.sr, p, r); synth::filt(b, c.sr, 0, 14000, 0.6); }
inline void tgClap(Buf& b, Rng& r, const Ctx& c) { synth::ClapP p; p.hits = 4; p.spread = 0.008; p.bp = 1500; p.q = 1.1; p.hitDec = 0.004; p.tail = 0.16; p.tailMix = 0.55; p.bright = 0.04; p.roomMix = 0.15; p.lp = 5200;
    synth::clap(b, c.sr, p, r); }
inline void tgHat(Buf& b, Rng& r, const Ctx& c, double dec, double att) { synth::HatP p; p.set = 1; p.mul = 1.4; p.noise = 0.35; p.hp = 8500; p.att = att; p.dec = dec; synth::hat(b, c.sr, p, r); }
inline void tgClosedHat(Buf& b, Rng& r, const Ctx& c) { tgHat(b, r, c, 0.0072, 0); }
inline void tgOpenHat(Buf& b, Rng& r, const Ctx& c) { tgHat(b, r, c, 0.05, 0.0005); }
inline void tgRim(Buf& b, Rng& r, const Ctx& c) { synth::modal(b, c.sr, 1800, { { 1, 1 }, { 1.6, 0.5 }, { 2.4, 0.25 } }, 0.03, 0.7, r); synth::filt(b, c.sr, 2, 500, 0.7); glue(b, 2.2); }
inline void tgBell(Buf& b, Rng& r, const Ctx& c) { synth::modal(b, c.sr, 520, { { 1, 1 }, { 2.76, 0.55 }, { 5.4, 0.3 }, { 8.93, 0.12 } }, 1.2, 0.15, r); }

// ---- TEXAS BOUNCE (study 3.4): up-tempo and straight, short and punchy everything; a roomy clap (+ a snare layer) is the big part
inline void txKick(Buf& b, Rng& r, const Ctx& c) { synth::KickP p; p.f0 = 54; p.f1 = 240; p.pt = 0.014; p.hold = 0.02; p.dec = 0.03; p.click = 0.45; p.drive = 2.8; p.trans = 0.7; synth::kick(b, c.sr, p, r); synth::filt(b, c.sr, 2, 75, 0.7); glue(b, 2.4); }   // (a little above the 808)
inline void tx808(Buf& b, Rng& r, const Ctx& c) { bass808(b, r, c.sr, 49.0, 12, 30, 0.42, 0.3, 0.05, 0.12, 0.007); for (auto& v : b) v = -v; }   // (a 7 ms swell and the polarity flipped: on the kick's steps the two never stack into one peak)
inline void txClap(Buf& b, Rng& r, const Ctx& c) { synth::ClapP p; p.hits = 4; p.spread = 0.009; p.bp = 1300; p.tail = 0.04; p.tailMix = 0.5; p.roomMix = 0.24; p.roomSize = 0.6; p.drive = 1.4; synth::clap(b, c.sr, p, r); }
inline void txSnare(Buf& b, Rng& r, const Ctx& c) { synth::SnareP p; p.tone = 205; p.ratio = 1.65; p.toneDec = 0.05; p.nBp = 4200; p.nDec = 0.045; p.click = 0.4; p.drive = 2.2; p.roomMix = 0.2; p.roomSize = 0.55; synth::snare(b, c.sr, p, r); glue(b, 2.0); }
inline void txHat(Buf& b, Rng& r, const Ctx& c) { synth::HatP p; p.set = 1; p.mul = 1.3; p.noise = 0.4; p.hp = 8000; p.dec = 0.009; synth::hat(b, c.sr, p, r); }
inline void txOpenHat(Buf& b, Rng& r, const Ctx& c) { synth::HatP p; p.set = 1; p.mul = 1.3; p.noise = 0.4; p.hp = 7500; p.att = 0.0005; p.dec = 0.045; synth::hat(b, c.sr, p, r); }
inline void txSnap(Buf& b, Rng& r, const Ctx& c) { synth::ClapP p; p.hits = 2; p.spread = 0.003; p.bp = 2800; p.q = 1.6; p.hitDec = 0.004; p.tail = 0.035; p.tailMix = 0.4; p.roomMix = 0.15; synth::clap(b, c.sr, p, r); }
inline void txCowbell(Buf& b, Rng& r, const Ctx& c) { const double sr = c.sr;   // the 8-series cowbell tuned up 1.2x
    for (size_t i = 0; i < b.size(); ++i) { const double t = i / sr; const double e = t < 0.012 ? 1.0 - 0.7 * t / 0.012 : 0.3 * std::exp(-(t - 0.012) / 0.08);
        b[i] = (float) ((synth::sq(TAU * 648 * t) + synth::sq(TAU * 960 * t) * 0.9 + r.u() * 0.04) * e * juce::jmin(1.0, t / 0.0005)); }
    Buf a = b; synth::filt(a, sr, 1, 1350, 1.1); for (size_t i = 0; i < b.size(); ++i) b[i] = a[i]; synth::filt(b, sr, 0, 6000, 0.7); glue(b, 1.8); }

// ---- BOOM BAP CLASSIC (study 3.5): dusty sources through the 12-bit 26 kHz sampler, saturated, a hair flat (detuned), swung 58 %
inline void bbRender(Buf& b, Rng& r, const Ctx& c, int channel, void (*src)(Buf&, Rng&, double)) {
    const double up = std::pow(2.0, 4.65 / 12.0); Buf x((size_t) (b.size() / std::pow(2.0, 5 / 12.0)) + 1, 0.0f); src(x, r, c.sr / up);   // +4.65 st recorded, -5 st played: 35 cents flat
    normTo(x, 0.7f); synth::sat(x, 3, 1.6); normTo(x); E(x, c, c12(channel)); b.assign(x.begin(), x.begin() + (long) juce::jmin(x.size(), b.size())); b.resize(b.size(), 0.0f); }   // (saturated on the way in)
inline void bbKickSrc(Buf& b, Rng& r, double sr) { synth::KickP p; p.f0 = 55; p.f1 = 140; p.pt = 0.02; p.hold = 0.02; p.dec = 0.07; p.harm = 0.15; p.click = 0.3; p.noiseTail = 0.12; p.drive = 2; synth::kick(b, sr, p, r);
    for (size_t i = 0; i < b.size(); ++i) b[i] += r.u() * 0.012f; }
inline void bbSnareSrc(Buf& b, Rng& r, double sr) { synth::SnareP p; p.tone = 190; p.ratio = 1.55; p.toneDec = 0.08; p.nBp = 2500; p.nDec = 0.075; p.crack = 0.3; p.roomMix = 0.25; p.drive = 1.6; synth::snare(b, sr, p, r); glue(b, 2.0);
    for (size_t i = 0; i < b.size(); ++i) b[i] += r.u() * 0.01f; }
inline void bbHatSrc(Buf& b, Rng& r, double sr) { synth::HatP p; p.set = 0; p.mul = 0.9; p.noise = 0.7; p.hp = 5500; p.dec = 0.011; p.lp = 9000; synth::hat(b, sr, p, r); }
inline void bbOpenSrc(Buf& b, Rng& r, double sr) { synth::HatP p; p.set = 0; p.mul = 0.9; p.noise = 0.7; p.hp = 5200; p.att = 0.001; p.dec = 0.08; p.lp = 9000; synth::hat(b, sr, p, r); }
inline void bbRimSrc(Buf& b, Rng& r, double sr) { synth::modal(b, sr, 500, { { 1, 1 }, { 2.7, 0.5 } }, 0.04, 0.5, r); synth::filt(b, sr, 2, 300, 0.7); glue(b, 2.0); }
inline void bbShakerSrc(Buf& b, Rng& r, double sr) { synth::shaker(b, sr, 0.006, 0.05, 5000, 0.5, r); }
inline void bbKick(Buf& b, Rng& r, const Ctx& c) { bbRender(b, r, c, 1, bbKickSrc); }
inline void bbSnare(Buf& b, Rng& r, const Ctx& c) { bbRender(b, r, c, 3, bbSnareSrc); }
inline void bbHat(Buf& b, Rng& r, const Ctx& c) { bbRender(b, r, c, 2, bbHatSrc); }
inline void bbOpenHat(Buf& b, Rng& r, const Ctx& c) { bbRender(b, r, c, 2, bbOpenSrc); }
inline void bbRim(Buf& b, Rng& r, const Ctx& c) { bbRender(b, r, c, 3, bbRimSrc); }
inline void bbShaker(Buf& b, Rng& r, const Ctx& c) { bbRender(b, r, c, 2, bbShakerSrc); }

// ---- POP SHINE (study 3.6): a big clean kick with a defined click, a wide bright snare + clap stack with a plate tail, a crisp
// shaker; loud but not distorted. The "Eighties" option: the snare into a big room, gated at 180 ms
inline void psKick(Buf& b, Rng& r, const Ctx& c) { synth::KickP p; p.f0 = 52; p.f1 = 200; p.pt = 0.02; p.hold = 0.03; p.dec = 0.06; p.click = 0.35; p.sub = 0.2; p.subDec = 0.12; p.drive = 1.6; p.trans = 0.5; synth::kick(b, c.sr, p, r); glue(b, 1.4); }
inline void psSnare(Buf& b, Rng& r, const Ctx& c) { synth::SnareP p; p.tone = 200; p.ratio = 1.62; p.toneDec = 0.07; p.nBp = 4500; p.nDec = 0.04; p.clapMix = 0.5; p.roomMix = 0.3; p.roomSize = 0.7; p.lp = 13000; synth::snare(b, c.sr, p, r);
    synth::room(b, c.sr, 0.8, 0.55, 0.28); glue(b, 1.6); }   // the plate tail
inline void psClap(Buf& b, Rng& r, const Ctx& c) { synth::ClapP p; p.hits = 4; p.spread = 0.01; p.bp = 1600; p.bright = 0.35; p.tail = 0.1; p.roomMix = 0.3; p.lp = 13000; synth::clap(b, c.sr, p, r); }
inline void psHat(Buf& b, Rng& r, const Ctx& c) { synth::HatP p; p.set = 2; p.mul = 1.35; p.noise = 0.4; p.hp = 9000; p.dec = 0.012; p.lp = 15000; synth::hat(b, c.sr, p, r); }
inline void psOpenHat(Buf& b, Rng& r, const Ctx& c) { synth::HatP p; p.set = 2; p.mul = 1.35; p.noise = 0.4; p.hp = 8500; p.att = 0.0005; p.dec = 0.06; p.lp = 15000; synth::hat(b, c.sr, p, r); }
inline void psShaker(Buf& b, Rng& r, const Ctx& c) { synth::shaker(b, c.sr, 0.006, 0.04, 6000, 0.3, r); }
inline void psSnareGated(Buf& b, Rng& r, const Ctx& c) { synth::SnareP p; p.tone = 195; p.ratio = 1.6; p.toneDec = 0.08; p.nBp = 3800; p.nDec = 0.07; p.clapMix = 0.3; p.drive = 1.4; synth::snare(b, c.sr, p, r);
    synth::room(b, c.sr, 0.8, 0.7, 0.5); const double sr = c.sr;
    for (size_t i = 0; i < b.size(); ++i) { const double t = i / sr; b[i] *= (float) (t < 0.18 ? 1.0 : std::exp(-(t - 0.18) / 0.0029)); } glue(b, 1.5); }   // the gate: shut at 180 ms (20 ms release to -60 dB)

// ---- TRAP CLASSIC (the early-2010s southern "orchestral trap" kit; the study's Trap Gold recipe + the owner's slots): a booming long
// 808, a clean punchy kick, a snare and a clap with reverb (layered on the 3), bright short hats for the rolls, a crash, and the
// orchestral perc: a synth BRASS HIT and a timpani-like TIMP TOM
inline void tcBoom808(Buf& b, Rng& r, const Ctx& c) { bass808(b, r, c.sr, 49.0, 14, 35, 3.6, 0.3, 0.1); }
inline void tcKick(Buf& b, Rng& r, const Ctx& c) { synth::KickP p; p.f0 = 55; p.f1 = 230; p.pt = 0.015; p.hold = 0.012; p.dec = 0.035; p.click = 0.5; p.clickHz = 4500; p.drive = 1.8; p.trans = 0.6;
    synth::kick(b, c.sr, p, r); synth::filt(b, c.sr, 2, 70, 0.7); glue(b, 2.4); }   // (sits a little above the 808)
inline void tcSnare(Buf& b, Rng& r, const Ctx& c) { synth::SnareP p; p.tone = 230; p.ratio = 1.7; p.toneDec = 0.04; p.nBp = 5000; p.nDec = 0.05; p.click = 0.4; p.drive = 1.8; p.lp = 13000; synth::snare(b, c.sr, p, r);
    synth::room(b, c.sr, 0.75, 0.6, 0.3); glue(b, 1.8); }
inline void tcClap(Buf& b, Rng& r, const Ctx& c) { synth::ClapP p; p.hits = 3; p.spread = 0.007; p.bp = 1500; p.bright = 0.2; p.tail = 0.07; p.lp = 11000; synth::clap(b, c.sr, p, r); synth::room(b, c.sr, 0.85, 0.7, 0.4); }
inline void tcHatClosed(Buf& b, Rng& r, const Ctx& c) { synth::HatP p; p.set = 1; p.mul = 1.4; p.noise = 0.35; p.hp = 8500; p.dec = 0.007; synth::hat(b, c.sr, p, r); }
inline void tcHatOpen(Buf& b, Rng& r, const Ctx& c) { synth::HatP p; p.set = 1; p.mul = 1.4; p.noise = 0.35; p.hp = 8000; p.att = 0.0005; p.dec = 0.045; synth::hat(b, c.sr, p, r); }
inline void tcCrash(Buf& b, Rng& r, const Ctx& c) { synth::CymP p; p.set = 0; p.mul = 0.75; p.noise = 0.55; p.hp = 3500; p.dec = 0.6; p.shimmer = 0.4; synth::cymbal(b, c.sr, p, r); }
inline void tcBrass(Buf& b, Rng&, const Ctx& c) {   // a synth brass stab: a minor chord of detuned saws (G3, Bb3, D4, G4) scooping up into pitch, the filter opening and closing
    const double sr = c.sr; static const double notes[] { 196.0, 233.08, 293.66, 392.0 }, det[] { -0.004, 0.0, 0.005 };
    std::vector<double> ph(12, 0.0);
    for (size_t i = 0; i < b.size(); ++i) { const double t = i / sr, scoop = 1.0 - 0.03 * std::exp(-t / 0.025); double v = 0; int k = 0;
        for (double f : notes) for (double d : det) { auto& q = ph[(size_t) k++]; q += f * (1 + d) * scoop / sr; q -= std::floor(q); v += 2 * q - 1; }
        b[i] = (float) (v / 12.0 * synth::env(t, 0.008, 0.06, 0.16)); }
    synth::filtMod(b, sr, 0, 1.1, [](double t) { return 900 + 4200 * std::exp(-t / 0.07) * juce::jmin(1.0, t / 0.012); });
    synth::filt(b, sr, 2, 120, 0.7); synth::room(b, sr, 0.9, 0.7, 0.35); glue(b, 1.6); }
inline void tcTimp(Buf& b, Rng& r, const Ctx& c) {   // a timpani-like tom: a low tuned head (G2) with its near-harmonic modes (1, 1.5, 1.99), a felt mallet, a slight drop
    const double sr = c.sr; static const double m[] { 1.0, 1.504, 1.987, 2.44 }, g[] { 1.0, 0.5, 0.32, 0.15 }, dk[] { 1.0, 1.3, 1.6, 2.2 };
    for (size_t i = 0; i < b.size(); ++i) { const double t = i / sr, f = 98.0 * (1 + 0.04 * std::exp(-t / 0.05)); double v = 0;
        for (int k = 0; k < 4; ++k) v += std::sin(TAU * f * m[k] * t + k) * g[k] * std::exp(-t * 2.4 * dk[k]);
        b[i] = (float) (v * juce::jmin(1.0, t / 0.002) + r.u() * std::exp(-t / 0.006) * 0.25); }
    synth::filt(b, sr, 0, 3000, 0.7); synth::room(b, sr, 0.9, 0.6, 0.25); glue(b, 1.6); }

// ================= the table (classicPackNames() order)
inline const std::vector<Def>& defs() {
    static const std::vector<Def> d {
        { "Kick", "ANALOG BOOM KICK", 1.1, abKick }, { "808", "ANALOG BOOM LONG", 3.8, abLong }, { "Snare", "ANALOG BOOM SNARE", 0.45, abSnare }, { "Clap", "ANALOG BOOM CLAP", 0.6, abClap },
        { "Hat", "ANALOG BOOM CLOSED HAT", 0.15, abClosedHat }, { "Open Hat", "ANALOG BOOM OPEN HAT", 0.9, abOpenHat }, { "Cymbal", "ANALOG BOOM CYMBAL", 2.0, abCymbal },
        { "Perc", "ANALOG BOOM COWBELL", 0.7, abCowbell }, { "Tom", "ANALOG BOOM TOM LO", 1.0, abTomLo }, { "Tom", "ANALOG BOOM TOM MID", 0.9, abTomMid }, { "Tom", "ANALOG BOOM TOM HI", 0.8, abTomHi },
        { "Perc", "ANALOG BOOM CONGA LO", 0.8, abCongaLo }, { "Perc", "ANALOG BOOM CONGA MID", 0.7, abCongaMid }, { "Perc", "ANALOG BOOM CONGA HI", 0.6, abCongaHi },
        { "Perc", "ANALOG BOOM CLAVES", 0.2, abClaves }, { "Snare", "ANALOG BOOM RIMSHOT", 0.15, abRimshot }, { "Perc", "ANALOG BOOM MARACAS", 0.12, abMaracas },
        { "Kick", "ANALOG PUNCH KICK", 0.6, apKick }, { "Kick", "ANALOG PUNCH KICK LONG", 1.0, apKickLong }, { "Snare", "ANALOG PUNCH SNARE", 0.45, apSnare }, { "Clap", "ANALOG PUNCH CLAP", 0.5, apClap },
        { "Hat", "ANALOG PUNCH CLOSED HAT", 0.2, apClosedHat }, { "Open Hat", "ANALOG PUNCH OPEN HAT", 0.9, apOpenHat }, { "Cymbal", "ANALOG PUNCH RIDE", 2.2, apRide },
        { "Cymbal", "ANALOG PUNCH CRASH", 2.4, apCrash }, { "Tom", "ANALOG PUNCH TOM LO", 0.9, apTomLo }, { "Tom", "ANALOG PUNCH TOM MID", 0.8, apTomMid }, { "Tom", "ANALOG PUNCH TOM HI", 0.7, apTomHi },
        { "Snare", "ANALOG PUNCH RIMSHOT", 0.12, apRimshot },
        { "Kick", "DIGITAL EIGHTIES KICK", 0.4, d8Kick }, { "Snare", "DIGITAL EIGHTIES SNARE", 0.4, d8Snare }, { "Clap", "DIGITAL EIGHTIES CLAP", 0.4, d8Clap },
        { "Hat", "DIGITAL EIGHTIES CLOSED HAT", 0.15, d8ClosedHat }, { "Open Hat", "DIGITAL EIGHTIES OPEN HAT", 0.8, d8OpenHat }, { "Perc", "DIGITAL EIGHTIES CABASA", 0.18, d8Cabasa },
        { "Perc", "DIGITAL EIGHTIES TAMBOURINE", 0.45, d8Tambourine }, { "Tom", "DIGITAL EIGHTIES TOM LO", 0.9, d8TomLo }, { "Tom", "DIGITAL EIGHTIES TOM MID", 0.8, d8TomMid },
        { "Tom", "DIGITAL EIGHTIES TOM HI", 0.7, d8TomHi }, { "Perc", "DIGITAL EIGHTIES CONGA HI", 0.45, d8CongaHi }, { "Perc", "DIGITAL EIGHTIES CONGA LO", 0.5, d8CongaLo },
        { "Perc", "DIGITAL EIGHTIES COWBELL", 0.45, d8Cowbell }, { "Perc", "DIGITAL EIGHTIES CLAVES", 0.15, d8Claves }, { "Snare", "DIGITAL EIGHTIES RIM", 0.15, d8Rim },
        { "Kick", "CRUNCH TWELVE KICK", 0.7, c12Kick }, { "Kick", "CRUNCH TWELVE KICK OPEN", 0.7, c12KickOpen }, { "Snare", "CRUNCH TWELVE SNARE", 0.5, c12Snare },
        { "Snare", "CRUNCH TWELVE SNARE FILTERED", 0.5, c12SnareFiltered }, { "Clap", "CRUNCH TWELVE CLAP", 0.45, c12Clap }, { "Hat", "CRUNCH TWELVE HAT", 0.15, c12Hat },
        { "Open Hat", "CRUNCH TWELVE OPEN HAT", 0.8, c12OpenHat }, { "Snare", "CRUNCH TWELVE RIM", 0.2, c12Rim }, { "Perc", "CRUNCH TWELVE SHAKER", 0.25, c12Shaker },
        { "Tom", "CRUNCH TWELVE TOM", 0.8, c12Tom },
        { "Kick", "SWING TWELVE KICK", 0.7, s12Kick }, { "Snare", "SWING TWELVE SNARE", 0.5, s12Snare }, { "Clap", "SWING TWELVE CLAP", 0.5, s12Clap },
        { "Hat", "SWING TWELVE HAT", 0.15, s12Hat }, { "Open Hat", "SWING TWELVE OPEN HAT", 0.8, s12OpenHat }, { "Snare", "SWING TWELVE RIM", 0.2, s12Rim },
        { "Perc", "SWING TWELVE SHAKER", 0.25, s12Shaker }, { "Tom", "SWING TWELVE TOM", 0.8, s12Tom }, { "Perc", "SWING TWELVE CONGA", 0.45, s12Conga },
        { "Kick", "TRAP GOLD KICK", 0.3, tgKick }, { "808", "TRAP GOLD 808 D", 3.0, tg808D }, { "808", "TRAP GOLD 808 F", 3.0, tg808F }, { "808", "TRAP GOLD 808 G", 3.0, tg808G },
        { "808", "TRAP GOLD 808 A", 3.0, tg808A }, { "Snare", "TRAP GOLD SNARE", 0.35, tgSnare }, { "Clap", "TRAP GOLD CLAP", 0.6, tgClap }, { "Hat", "TRAP GOLD CLOSED HAT", 0.08, tgClosedHat },
        { "Open Hat", "TRAP GOLD OPEN HAT", 0.5, tgOpenHat }, { "Snare", "TRAP GOLD RIMSHOT", 0.15, tgRim }, { "Perc", "TRAP GOLD BELL", 2.5, tgBell },
        { "Kick", "TEXAS BOUNCE KICK", 0.35, txKick }, { "808", "TEXAS BOUNCE 808", 0.8, tx808 }, { "Clap", "TEXAS BOUNCE CLAP", 0.6, txClap }, { "Snare", "TEXAS BOUNCE SNARE", 0.4, txSnare },
        { "Hat", "TEXAS BOUNCE HAT", 0.1, txHat }, { "Open Hat", "TEXAS BOUNCE OPEN HAT", 0.45, txOpenHat }, { "Clap", "TEXAS BOUNCE SNAP", 0.25, txSnap }, { "Perc", "TEXAS BOUNCE COWBELL", 0.4, txCowbell },
        { "Kick", "BOOM BAP CLASSIC KICK", 0.6, bbKick }, { "Snare", "BOOM BAP CLASSIC SNARE", 0.5, bbSnare }, { "Hat", "BOOM BAP CLASSIC HAT", 0.15, bbHat },
        { "Open Hat", "BOOM BAP CLASSIC OPEN HAT", 0.6, bbOpenHat }, { "Snare", "BOOM BAP CLASSIC RIM", 0.2, bbRim }, { "Perc", "BOOM BAP CLASSIC SHAKER", 0.25, bbShaker },
        { "Kick", "POP SHINE KICK", 0.5, psKick }, { "Snare", "POP SHINE SNARE", 0.9, psSnare }, { "Clap", "POP SHINE CLAP", 0.7, psClap }, { "Hat", "POP SHINE HAT", 0.1, psHat },
        { "Open Hat", "POP SHINE OPEN HAT", 0.6, psOpenHat }, { "Perc", "POP SHINE SHAKER", 0.25, psShaker }, { "Snare", "POP SHINE SNARE GATED", 0.5, psSnareGated },
        { "808", "BOOM 808", 4.2, tcBoom808 }, { "Kick", "TRAP KICK", 0.35, tcKick }, { "Snare", "TRAP CLASSIC SNARE", 0.7, tcSnare }, { "Clap", "TRAP CLASSIC CLAP", 0.8, tcClap },
        { "Hat", "TRAP HAT CLOSED", 0.08, tcHatClosed }, { "Open Hat", "TRAP HAT OPEN", 0.5, tcHatOpen }, { "Cymbal", "TRAP CLASSIC CRASH", 2.6, tcCrash },
        { "Perc", "BRASS HIT", 1.2, tcBrass }, { "Tom", "TIMP TOM", 1.6, tcTimp },
    };
    return d;
}
inline const Def* find(const juce::String& name) { for (auto& d : defs()) if (name == d.name) return &d; return nullptr; }
// one pack sound as rendered by its recipe (before the DC block / normalising), optionally with era()'s quantiser bypassed
inline Buf renderRaw(const Def& d, double sr, bool bypassBits = false) {
    Buf b((size_t) juce::jmax(64, (int) (sr * d.len)), 0.0f); Rng r((uint32_t) juce::String(d.name).hashCode()); Ctx c; c.sr = sr; c.bypassBits = bypassBits; d.fn(b, r, c); return b;
}
} // namespace gb::classic

namespace gb {
// one finished pack sound (the recipe, band-limited at the top, DC-blocked, peak-normalised to 0.89, a click-free end)
inline std::shared_ptr<Sound> classicSound(const classic::Def& d, double sr) {
    auto s = std::make_shared<Sound>(); s->name = d.name; s->category = d.cat; s->rate = sr; s->data = classic::renderRaw(d, sr);
    synth::filt(s->data, sr, 0, 17000, 0.6); synth::filt(s->data, sr, 0, 17000, 0.6);   // band-limited at the top: no inter-sample overs from the bright strikes
    { const float c = (float) std::exp(-synth::TAU * 10.0 / sr); float px = 0, py = 0; for (auto& v : s->data) { const float y = v - px + c * py; px = v; py = y; v = y; } }   // DC block
    float mx = 1e-6f; for (auto& v : s->data) { if (!std::isfinite(v)) v = 0; mx = std::max(mx, std::abs(v)); }
    for (auto& v : s->data) v *= 0.89f / mx;
    const int n = (int) s->data.size(), fade = juce::jmin(n / 4, (int) (sr * 0.005)); for (int k = 0; k < fade; ++k) s->data[(size_t) (n - 1 - k)] *= (float) k / (float) fade;   // click-free end
    return s;
}
inline void appendClassicSounds(DrumLibrary& lib, double sr) {
    // every sound is rendered on its own (its own Rng, no shared state), so the pack renders in parallel and is appended in order:
    // bit-identical to a serial render (BeatsTests checks it), a fraction of the start-up time
    const auto& D = classic::defs(); std::vector<std::shared_ptr<Sound>> made(D.size()); std::atomic<size_t> next { 0 };
    auto work = [&] { for (size_t i; (i = next.fetch_add(1)) < D.size();) if (lib.indexOf(D[i].name) < 0) made[i] = classicSound(D[i], sr); };
    const int nt = (int) juce::jlimit(1u, 8u, std::thread::hardware_concurrency());
    std::vector<std::thread> pool; for (int t = 1; t < nt; ++t) pool.emplace_back(work);
    work(); for (auto& t : pool) t.join();
    for (auto& s : made) if (s) lib.append(s);
}
} // namespace gb
