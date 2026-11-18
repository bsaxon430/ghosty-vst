#pragma once
// GHOST SLICE tests - our own synthesised voices (nothing recorded, no datasets, no copyrighted audio). A small Klatt-style formant
// synthesiser written for these tests (after D. Klatt, "Software for a cascade/parallel formant synthesizer", JASA 67 (1980): the
// published structure, our own code):
//   source     a glottal pulse train (Rosenberg-like opening / closing shape, spectral tilt), jitter + shimmer, aspiration noise,
//              a pitch contour per syllable (speech: falling declination with accents; singing: notes with scoop, vibrato, drift)
//   tract      five formant resonators in cascade, their targets interpolated through every vowel / consonant (coarticulation)
//   consonants plosives (silence + a burst + aspiration), fricatives (band noise: s, sh, f, h), nasals (m / n murmur), liquids (l / r)
//   room       a quiet noise floor, a few breaths
// spokenSentence(): about 3.5 s of speech ("hello there, how are you feeling today" in made-up phonemes, two phrases).
// sungPhrase():     about 5 s of a singer ("la la ooh, da da aah") on an A minor melody, with vibrato.
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>

namespace slicetest
{
constexpr double kTau = 6.283185307179586;

struct VRng
{
    uint64_t s;
    explicit VRng (uint64_t seed) : s (seed * 0x9E3779B97F4A7C15ull + 1) {}
    uint64_t next() { uint64_t z = (s += 0x9E3779B97F4A7C15ull); z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; return z ^ (z >> 31); }
    double u() { return (double) (next() >> 11) * (1.0 / 9007199254740992.0); }
    double sym() { return 2.0 * u() - 1.0; }
    double gauss() { double a = 0; for (int i = 0; i < 6; ++i) a += u(); return (a - 3.0) * 1.41; }
};

struct Reso
{
    double a1 = 0, a2 = 0, g = 1, y1 = 0, y2 = 0;
    void set (double f, double bw, double sr) { const double r = std::exp (-M_PI * bw / sr); a1 = -2 * r * std::cos (kTau * f / sr); a2 = r * r; g = 1 + a1 + a2; }
    double run (double x) { const double y = g * x - a1 * y1 - a2 * y2; y2 = y1; y1 = y; return y; }
};

// phoneme targets: F1..F5 (Hz), bandwidths, voicing 0..1, noise (fricative) level, noise band centre, nasal, closure
struct Phone { char id; double F[5]; double voice; double fric; double fricHz; double fricBw; bool plosive; bool nasal; double dur; };
inline const Phone& phone (char c)
{
    static const Phone P[] {
        { 'a', { 750, 1220, 2600, 3500, 4200 }, 1, 0, 0, 0, false, false, 0.16 },   // "ah"
        { 'e', { 520, 1850, 2550, 3500, 4200 }, 1, 0, 0, 0, false, false, 0.13 },   // "eh"
        { 'i', { 300, 2250, 2950, 3600, 4300 }, 1, 0, 0, 0, false, false, 0.12 },   // "ee"
        { 'o', { 480, 850, 2450, 3400, 4200 }, 1, 0, 0, 0, false, false, 0.15 },    // "oh"
        { 'u', { 330, 800, 2300, 3300, 4100 }, 1, 0, 0, 0, false, false, 0.14 },    // "oo"
        { 'y', { 400, 1600, 2500, 3400, 4200 }, 1, 0, 0, 0, false, false, 0.10 },   // a schwa-ish glide
        { 'l', { 380, 1100, 2700, 3400, 4200 }, 0.9, 0, 0, 0, false, false, 0.06 },
        { 'r', { 420, 1300, 1700, 3200, 4100 }, 0.9, 0, 0, 0, false, false, 0.06 },
        { 'm', { 280, 1000, 2300, 3300, 4100 }, 0.7, 0, 0, 0, false, true, 0.07 },
        { 'n', { 280, 1500, 2500, 3300, 4100 }, 0.7, 0, 0, 0, false, true, 0.06 },
        { 'h', { 600, 1500, 2500, 3400, 4200 }, 0, 0.5, 1800, 2500, false, false, 0.07 },
        { 's', { 400, 1600, 2600, 3400, 4200 }, 0, 0.9, 6500, 3000, false, false, 0.10 },
        { 'S', { 400, 1600, 2600, 3400, 4200 }, 0, 0.85, 3200, 1800, false, false, 0.10 },   // "sh"
        { 'f', { 400, 1400, 2600, 3400, 4200 }, 0, 0.35, 5000, 6000, false, false, 0.08 },
        { 'd', { 400, 1700, 2600, 3400, 4200 }, 0.5, 0.4, 3500, 3000, true, false, 0.05 },
        { 't', { 400, 1700, 2600, 3400, 4200 }, 0, 0.7, 4500, 3500, true, false, 0.06 },
        { 'k', { 400, 1700, 2400, 3400, 4200 }, 0, 0.6, 2200, 1500, true, false, 0.07 },
        { 'b', { 300, 900, 2300, 3400, 4200 }, 0.5, 0.3, 1200, 1500, true, false, 0.05 },
        { 'w', { 300, 700, 2300, 3300, 4100 }, 0.9, 0, 0, 0, false, false, 0.06 },
        { ' ', { 500, 1500, 2500, 3500, 4200 }, 0, 0, 0, 0, false, false, 0.12 },   // a pause
    };
    for (auto& p : P) if (p.id == c) return p;
    return P[0];
}

struct Seg { char ph; double dur; double f0; double f0End; double amp; double vib = 0; };   // f0 in Hz (start -> end)

// render a list of segments (each a phone with its own duration and pitch glide)
inline juce::AudioBuffer<float> synth (const std::vector<Seg>& segs, double sr, uint64_t seed, double breathy = 0.05, double formantScale = 1.0)
{
    VRng R (seed);
    double total = 0.25; for (auto& s : segs) total += s.dur;
    const int n = (int) (total * sr);
    std::vector<double> y ((size_t) n, 0.0);
    Reso F[5]; Reso fric; Reso nasal; nasal.set (250, 100, sr);
    double phase = 0, lp = 0, tiltZ = 0;
    int idx = 0; double tSeg = 0;
    double F0 = segs.empty() ? 150 : segs[0].f0;
    double curF[5]; for (int k = 0; k < 5; ++k) curF[k] = phone (segs.empty() ? 'a' : segs[0].ph).F[k] * formantScale;
    double voiceAmt = 0, fricAmt = 0, nasalAmt = 0, amp = 0;
    double jitter = 0, shimmer = 1;
    const int start = (int) (0.1 * sr);
    double vibPh = R.u() * kTau;
    for (int i = 0; i < n; ++i)
    {
        double out = 0;
        if (i >= start && idx < (int) segs.size())
        {
            const auto& S = segs[(size_t) idx]; const auto& P = phone (S.ph);
            const double x = tSeg / S.dur;
            // targets glide (coarticulation: one-pole smoothing towards the phone's formants)
            const double glide = 1.0 - std::exp (-1.0 / (0.018 * sr));
            for (int k = 0; k < 5; ++k) curF[k] += glide * (P.F[k] * formantScale - curF[k]);
            const double vT = P.voice, fT = P.fric * (P.plosive ? (x > 0.45 ? std::exp (-(x - 0.45) * 12.0) : 0.0) : 1.0), nT = P.nasal ? 1.0 : 0.0;
            const double aT = S.amp * (P.plosive && x < 0.45 ? 0.0 : 1.0);
            const double sm = 1.0 - std::exp (-1.0 / (0.006 * sr));
            voiceAmt += sm * (vT - voiceAmt); fricAmt += sm * (fT - fricAmt); nasalAmt += sm * (nT - nasalAmt); amp += sm * (aT - amp);
            const double target = S.f0 + (S.f0End - S.f0) * x;
            F0 += (1.0 - std::exp (-1.0 / (0.025 * sr))) * (target - F0);
            const double vib = S.vib > 0 ? std::exp2 (S.vib / 1200.0 * std::sin (vibPh)) : 1.0;
            vibPh += kTau * 5.4 / sr;
            const double f = F0 * vib * (1.0 + jitter);
            // the glottal pulse (Rosenberg-like): opening 0..0.4, closing 0.4..0.6, closed after
            phase += f / sr;
            if (phase >= 1.0) { phase -= 1.0; jitter = 0.004 * R.gauss(); shimmer = 1.0 + 0.04 * R.gauss(); }
            double g = 0;
            if (phase < 0.4) g = 0.5 - 0.5 * std::cos (M_PI * phase / 0.4);
            else if (phase < 0.6) g = std::cos (M_PI * 0.5 * (phase - 0.4) / 0.2);
            const double dg = g - lp; lp = g;   // the flow derivative excites the tract
            tiltZ += 0.35 * (dg - tiltZ);
            const double src = tiltZ * 6.0 * shimmer * voiceAmt + breathy * R.gauss() * voiceAmt * (0.3 + g);
            double v = src;
            for (int k = 0; k < 5; ++k) { F[k].set (curF[k], 60.0 + 25.0 * k, sr); v = F[k].run (v); }
            v = v * (1.0 - 0.6 * nasalAmt) + nasalAmt * nasal.run (src) * 4.0;
            double nz = 0;
            if (fricAmt > 1e-4) { fric.set (P.fricHz > 0 ? P.fricHz : 2000, P.fricBw > 0 ? P.fricBw : 2000, sr); nz = fric.run (R.gauss()) * fricAmt * 0.25; }
            out = amp * (v * 0.6 + nz);
            tSeg += 1.0 / sr;
            if (tSeg >= S.dur) { tSeg = 0; ++idx; }
        }
        y[(size_t) i] = out + 0.0008 * R.gauss();   // the room
    }
    double pk = 1e-9; for (double v : y) pk = std::max (pk, std::abs (v));
    juce::AudioBuffer<float> b (1, n);
    for (int i = 0; i < n; ++i) b.setSample (0, i, (float) (y[(size_t) i] * 0.7 / pk));
    return b;
}

// a spoken sentence: two phrases, speech intonation (declination + accents), a breath between
inline juce::AudioBuffer<float> spokenSentence (double sr = 48000.0, uint64_t seed = 7, double baseHz = 128.0)
{
    std::vector<Seg> s;
    auto syl = [&] (const char* phones, double f0a, double f0b, double amp, double vowelLen)
    {
        for (const char* p = phones; *p; ++p)
        {
            const auto& P = phone (*p);
            const bool vowel = std::string ("aeiouy").find (*p) != std::string::npos;
            s.push_back ({ *p, vowel ? vowelLen : P.dur, f0a, vowel ? f0b : f0a, amp });
        }
    };
    const double b = baseHz;
    // "hello there, how are you"  /  "feeling so good today"
    syl ("he", b * 1.15, b * 1.1, 0.8, 0.10); syl ("lo", b * 1.25, b * 1.05, 1.0, 0.24); syl (" ", b, b, 0, 0.05);
    syl ("de", b * 1.1, b * 1.0, 0.9, 0.12); syl ("ra", b * 1.0, b * 0.92, 0.8, 0.20); syl (" ", b, b, 0, 0.12);
    syl ("ha", b * 1.2, b * 1.3, 1.0, 0.18); syl ("wa", b * 1.1, b * 1.05, 0.85, 0.12); syl ("yu", b * 1.05, b * 0.95, 0.8, 0.22);
    syl (" ", b, b, 0, 0.30); syl ("h", b, b, 0.3, 0.12);   // a breath
    syl ("fi", b * 1.15, b * 1.12, 0.9, 0.16); syl ("lin", b * 1.08, b * 1.0, 0.85, 0.12); syl ("so", b * 1.25, b * 1.2, 1.0, 0.26);
    syl ("gu", b * 1.05, b * 1.0, 0.9, 0.14); syl ("d", b, b, 0.6, 0.05); syl ("tu", b * 0.98, b * 0.95, 0.8, 0.10); syl ("de", b * 0.95, b * 0.8, 0.85, 0.30);
    return synth (s, sr, seed, 0.06);
}

// a sung phrase in A minor: "la la ooh, da da aah" with vibrato and scoops
inline juce::AudioBuffer<float> sungPhrase (double sr = 48000.0, uint64_t seed = 11, double formantScale = 1.08)
{
    std::vector<Seg> s;
    auto hz = [] (int midi) { return 440.0 * std::pow (2.0, (midi - 69) / 12.0); };
    auto note = [&] (char c, char v, int midi, double len, double vib)
    {
        if (c != 0) s.push_back ({ c, phone (c).dur, hz (midi) * 0.97, hz (midi) * 0.99, 0.9 });
        s.push_back ({ v, len, hz (midi) * 0.985, hz (midi), 1.0, vib });
    };
    note ('l', 'a', 69, 0.45, 18); note ('l', 'a', 72, 0.45, 20); note (0, 'u', 76, 0.9, 30); s.push_back ({ ' ', 0.18, hz (74), hz (74), 0 });
    note ('d', 'a', 74, 0.4, 18); note ('d', 'a', 72, 0.4, 20); note (0, 'a', 69, 1.3, 35); s.push_back ({ ' ', 0.2, hz (69), hz (69), 0 });
    return synth (s, sr, seed, 0.035, formantScale);
}
} // namespace slicetest
