#pragma once
// VintageCharacter: one-click "old sampler" colour for any audio (Poltergeist samples, Ghosty Beats tracks, ...).
// Original models, measured and tested (Poltergeist/tests/BassTests.cpp "vintage" checks):
//
//   mode 1  8-BIT HOME COMPUTER     8.4 kHz, 8 bit (rounded, no dither), NO anti-alias filter (the aliasing is the point),
//                                   zero-order hold, soft 2-pole output filter at 4.6 kHz, noise floor -62 dBFS
//   mode 2  12-BIT CLASSIC SAMPLER  26.04 kHz, 12 bit (no dither), 4-pole input filter at 10.9 kHz (some aliasing gets
//                                   through above it), zero-order hold, 4-pole output filter at 12 kHz with a little
//                                   resonance, noise floor -78 dBFS
//   mode 3  EARLY-90s LO-FI         22.05 kHz, 10 bit with TPDF dither, 4-pole input filter at 9.5 kHz, zero-order hold,
//                                   dull 2-pole output filter at 7.5 kHz, noise floor -61.5 dBFS (mostly the dither)
//
// API (header-only, real-time safe: no allocation, no locks in process; per-channel state for up to kMaxCh channels):
//   vc::Character c;
//   c.prepare(sampleRate, maxBlock);     // any time before process (message thread); also resets
//   c.setMode(m);                        // 0 = off, 1..3 above (vc::kModeNames)
//   c.setAmount(a);                      // 0..1: 0 = bit-exact bypass, 1 = the full model; in between the rate, bits,
//                                        //        filters and noise move smoothly from clean to the model
//   c.setDrive(d);                       // 0..1 extra input saturation (0 = none beyond the model's own)
//   c.process(channels, numChannels, numSamples);   // in place
//   c.reset();                           // clear the filters / hold / noise state (e.g. on a new note or a seek)
// setMode / setAmount / setDrive only recompute a few coefficients: safe to call per block from the audio thread.
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace vc {

constexpr int kMaxCh = 8;
static const char* const kModeNames[] = { "Off", "8-bit Home Computer", "12-bit Classic Sampler", "Early-90s Lo-Fi" };
constexpr int kNumModes = 4;

// one mode's model (also what the tests measure against)
struct Spec {
    double rate = 0;        // held sample rate (Hz)
    double bits = 24;       // word length
    int dither = 0;         // 0 none, 1 TPDF, 2 TPDF + first-order noise shaping
    double preCut = 0;      // input (anti-alias) filter, 4-pole, Hz (0 = none: full aliasing)
    double postCut = 0;     // output (reconstruction) filter, Hz
    int postPoles = 2;      // 2 or 4
    double postQ = 0.707;   // resonance of each output stage
    double noiseDb = -200;  // analog noise of the output stage, dBFS RMS
    double floorDb = -200;  // the whole noise floor with silence in (analog + dither), as measured
    double drive = 0;       // the model's own input saturation (0..1)
};
inline Spec spec(int mode) {
    Spec s;
    switch (mode) {
        case 1: s.rate = 8400; s.bits = 8; s.dither = 0; s.preCut = 0; s.postCut = 4600; s.postPoles = 2; s.postQ = 0.6; s.noiseDb = -62; s.floorDb = -62; s.drive = 0.0; break;
        case 2: s.rate = 26040; s.bits = 12; s.dither = 0; s.preCut = 10900; s.postCut = 12000; s.postPoles = 4; s.postQ = 0.85; s.noiseDb = -78; s.floorDb = -78; s.drive = 0.0; break;
        case 3: s.rate = 22050; s.bits = 10; s.dither = 1; s.preCut = 9500; s.postCut = 7500; s.postPoles = 2; s.postQ = 0.707; s.noiseDb = -70; s.floorDb = -61.5; s.drive = 0.0; break;
        default: break;
    }
    return s;
}

class Character {
public:
    void prepare(double sampleRate, int /*maxBlock*/) { sr = sampleRate > 0 ? sampleRate : 44100.0; reset(); update(); }
    void reset() {
        for (auto& c : ch) c = Chan();
        rng = 0x2545F491u;
    }
    void setMode(int m) { m = std::clamp(m, 0, kNumModes - 1); if (m != mode) { mode = m; reset(); update(); } }
    void setAmount(float a) { a = std::clamp(a, 0.0f, 1.0f); if (a != amount) { amount = a; update(); } }
    void setDrive(float d) { d = std::clamp(d, 0.0f, 1.0f); if (d != drive) { drive = d; update(); } }
    int getMode() const { return mode; }
    float getAmount() const { return amount; }
    bool active() const { return mode > 0 && amount > 0.0f; }

    // measurement switches (tests only): leave the quantiser / the noise out
    bool testNoQuantise = false, testNoNoise = false, testNoFilters = false;
    // what the model is running right now (after AMOUNT)
    double heldRate() const { return rateNow; }
    double bitsNow() const { return bitsEff; }

    void process(float* const* io, int numCh, int n) {
        if (!active() || io == nullptr) return;   // bit-exact bypass
        numCh = std::min(numCh, kMaxCh);
        for (int c = 0; c < numCh; ++c) {
            float* x = io[c]; if (x == nullptr) continue;
            Chan& s = ch[c];
            for (int i = 0; i < n; ++i) {
                double v = x[i];
                if (!std::isfinite(v)) v = 0.0;
                if (driveG > 1.0001) v = std::tanh(v * driveG) * driveNorm;          // input stage
                if (usePre && !testNoFilters) { v = pre[0].tick(s.pre[0], v); v = pre[1].tick(s.pre[1], v); }   // anti-alias filter
                // sample & hold at the model's rate (fractional phase: the average rate is exact)
                s.ph += rateStep;
                if (s.ph >= 1.0) { s.ph -= 1.0; s.held = quantise(s, v); }
                double y = s.held;
                if (!testNoFilters) { y = post[0].tick(s.post[0], y); if (postPoles == 4) y = post[1].tick(s.post[1], y); }   // reconstruction filter
                if (noiseG > 0 && !testNoNoise) y += noiseG * gauss();                                  // output noise floor
                x[i] = (float) y;
            }
        }
    }

private:
    struct Bq {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        struct St { double z1 = 0, z2 = 0; };
        inline double tick(St& s, double x) const { const double y = b0 * x + s.z1; s.z1 = b1 * x - a1 * y + s.z2; s.z2 = b2 * x - a2 * y; return y; }
        static Bq lowpass(double f, double fs, double q) {
            Bq b; f = std::clamp(f, 10.0, fs * 0.49);
            const double w = 2.0 * 3.141592653589793 * f / fs, al = std::sin(w) / (2.0 * q), c = std::cos(w), a0 = 1.0 + al;
            b.b0 = (1.0 - c) / 2.0 / a0; b.b1 = (1.0 - c) / a0; b.b2 = b.b0; b.a1 = -2.0 * c / a0; b.a2 = (1.0 - al) / a0; return b;
        }
    };
    struct Chan { Bq::St pre[2], post[2]; double ph = 1.0, held = 0.0, err = 0.0; };

    double sr = 44100; int mode = 0; float amount = 1.0f, drive = 0.0f;
    Chan ch[kMaxCh];
    Bq pre[2], post[2]; bool usePre = false; int postPoles = 2;
    double rateNow = 44100, rateStep = 1, bitsEff = 24, q = 8388608, noiseG = 0, driveG = 1, driveNorm = 1; int ditherType = 0;
    uint32_t rng = 0x2545F491u;

    inline double uni() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (double) rng * (1.0 / 4294967296.0); }   // [0, 1)
    inline double gauss() { return (uni() + uni() + uni() + uni() - 2.0) * 1.7320508; }   // ~N(0, 1)
    inline double quantise(Chan& s, double v) {
        if (testNoQuantise) return v;
        double u = v * q;
        if (ditherType >= 1) u += uni() - uni();                         // TPDF, +-1 LSB
        if (ditherType == 2) u -= s.err;                                  // first-order noise shaping
        double r = std::floor(u + 0.5);
        r = std::clamp(r, -q, q - 1.0);
        if (ditherType == 2) s.err = r - u;
        return r / q;
    }
    // AMOUNT morphs from clean (0) to the full model (1)
    void update() {
        const Spec s = spec(mode); const double a = amount;
        const double nyq = sr * 0.45;
        rateNow = s.rate > 0 && s.rate < sr ? sr * std::pow(s.rate / sr, a) : sr;
        rateStep = rateNow / sr;
        bitsEff = 24.0 - (24.0 - s.bits) * a;
        q = std::pow(2.0, bitsEff - 1.0);
        ditherType = a > 0.0 ? s.dither : 0;
        auto morph = [&](double f) { return f > 0 ? f * std::pow(nyq / f, 1.0 - a) : nyq; };
        usePre = s.preCut > 0;
        if (usePre) { const double f = std::min(morph(s.preCut), nyq); pre[0] = Bq::lowpass(f, sr, 0.5412); pre[1] = Bq::lowpass(f, sr, 1.3066); }   // 4-pole Butterworth
        postPoles = s.postPoles;
        const double pf = std::min(morph(s.postCut), nyq);
        post[0] = Bq::lowpass(pf, sr, s.postQ); post[1] = Bq::lowpass(pf, sr, s.postQ);
        noiseG = s.noiseDb > -190 ? std::pow(10.0, s.noiseDb / 20.0) * a : 0.0;
        const double d = std::clamp(s.drive * a + (double) drive, 0.0, 1.0);
        driveG = 1.0 + 6.0 * d; driveNorm = 1.0 / std::sqrt(driveG);   // DRIVE: up to +8 dB on quiet parts, peaks pushed into tanh
    }
};

} // namespace vc
