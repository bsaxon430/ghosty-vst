// Ghost Lab - AGPL-3.0-only (see LICENSE, NOTICE.md). SPECTRE, the real-time resynthesis room (Spectre.h).
#include "Spectre.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace glab::spectre
{
static const ParamInfo kInfo[kParams] = {
    { "phase",     "PHASE",     "left: every partial locked together, a robot buzz. Right: scattered, a whisper", -1.0f, 1.0f, 0.0f, "", true },
    { "cepstrum",  "CEPSTRUM",  "up: only the body of the sound (its vowel). Down: only the fine detail, bright and raw", -1.0f, 1.0f, 0.0f, "", true },
    { "shift",     "SHIFT",     "slides every partial by the same Hz: goes metallic", -1000.0f, 1000.0f, 0.0f, "Hz", true },
    { "transpose", "TRANSPOSE", "moves the notes up or down, keeping the body", -24.0f, 24.0f, 0.0f, "st", true },
    { "cloud",     "CLOUD",     "partials ring on and drift apart into a wash", 0.0f, 1.0f, 0.0f, "", false },
    { "smooth",    "SMOOTH",    "every change glides instead of jumping", 0.0f, 1.0f, 0.0f, "", false },
    { "mid",       "MID",       "how much of the middle is rebuilt", 0.0f, 1.0f, 1.0f, "", false },
    { "side",      "SIDE",      "how much of the stereo sides is rebuilt", 0.0f, 1.0f, 1.0f, "", false },
    { "inf",       "INF",       "hold: at the top the sound freezes forever", 0.0f, 1.0f, 0.0f, "", false },
    { "partials",  "PARTIALS",  "how many sine waves rebuild the sound", 16.0f, 1024.0f, 512.0f, "", false },
    { "output",    "OUTPUT",    "how loud it comes out", -24.0f, 12.0f, 0.0f, "dB", true },
    { "noise",     "NOISE",     "up: hiss, breath and drums stay noise. Down: everything becomes sine waves (glassy, tuned)", 0.0f, 1.0f, 0.7f, "", false },
    { "transient", "TRANSIENT", "lets the first few milliseconds of every hit through as they were, so drums keep their punch", 0.0f, 1.0f, 0.7f, "", false },
};

const ParamInfo& param (int p) { return kInfo[juce::jlimit (0, kParams - 1, p)]; }
int paramIndex (const juce::String& key)
{
    for (int i = 0; i < kParams; ++i) if (key == kInfo[i].key) return i;
    return -1;
}
Params::Params() { for (int i = 0; i < kParams; ++i) v[(size_t) i] = kInfo[i].def; }

float toNorm (int p, float v)
{
    const auto& in = param (p);
    if (p == Shift) { const float t = juce::jlimit (-1.0f, 1.0f, v / in.hi); return 0.5f + 0.5f * (t < 0 ? -std::cbrt (-t) : std::cbrt (t)); }
    if (p == Partials) return juce::jlimit (0.0f, 1.0f, std::log (juce::jmax (in.lo, v) / in.lo) / std::log (in.hi / in.lo));
    return juce::jlimit (0.0f, 1.0f, (v - in.lo) / (in.hi - in.lo));
}
float fromNorm (int p, float t)
{
    const auto& in = param (p);
    t = juce::jlimit (0.0f, 1.0f, t);
    if (p == Shift) { const float u = 2.0f * t - 1.0f; return in.hi * u * u * u; }
    if (p == Partials) return in.lo * std::pow (in.hi / in.lo, t);
    return in.lo + t * (in.hi - in.lo);
}
juce::String display (int p, float v)
{
    switch (p)
    {
        case Shift:     return (v > 0.5f ? "+" : "") + juce::String (juce::roundToInt (v)) + " Hz";
        case Transpose: return (v > 0.05f ? "+" : "") + (std::abs (v - std::round (v)) < 0.01f ? juce::String (juce::roundToInt (v)) : juce::String (v, 2)) + " st";
        case Partials:  return juce::String (juce::roundToInt (v));
        case Output:    return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB";
        case Inf:       return v >= 0.999f ? "INF" : juce::String (juce::roundToInt (v * 100)) + "%";
        case PhaseX: case CepstrumY: return (v > 0.005f ? "+" : "") + juce::String (juce::roundToInt (v * 100));
        default:        return juce::String (juce::roundToInt (v * 100)) + "%";
    }
}

static constexpr float kTwoPi = 6.283185307179586f, kPi = 3.14159265358979f;
static inline float wrapPi (float x) { x = std::fmod (x + kPi, kTwoPi); if (x < 0) x += kTwoPi; return x - kPi; }

// ================================================================================================ the wavetables
void Wavetable::build (const std::vector<float>& cycle)
{
    sine = true;
    if ((int) cycle.size() < kTableSize) return;
    juce::dsp::FFT f (11);
    std::vector<float> spec ((size_t) kTableSize * 2, 0.0f), tmp ((size_t) kTableSize * 2, 0.0f);
    for (int i = 0; i < kTableSize; ++i) spec[(size_t) i] = cycle[(size_t) i];
    f.performRealOnlyForwardTransform (spec.data(), true);
    spec[0] = spec[1] = 0.0f;                          // no DC
    float gain = 0.0f;
    for (int L = 0; L < kTableLevels; ++L)
    {
        const int top = juce::jmin (kTableSize / 2 - 1, (kTableSize / 2) >> L);   // harmonics kept at this level
        std::fill (tmp.begin(), tmp.end(), 0.0f);
        for (int h = 1; h <= top; ++h) { tmp[(size_t) (2 * h)] = spec[(size_t) (2 * h)]; tmp[(size_t) (2 * h + 1)] = spec[(size_t) (2 * h + 1)]; }
        f.performRealOnlyInverseTransform (tmp.data());
        if (L == 0)
        {
            double e = 0.0; for (int i = 0; i < kTableSize; ++i) e += (double) tmp[(size_t) i] * tmp[(size_t) i];
            const double r = std::sqrt (e / kTableSize);
            if (r < 1e-6) return;                      // silent: stays a sine
            gain = (float) (0.70710678 / r);           // as loud as a sine
        }
        auto& lv = level[(size_t) L];
        for (int i = 0; i < kTableSize; ++i) lv[(size_t) i] = tmp[(size_t) i] * gain;
        lv[kTableSize] = lv[0];
    }
    sine = false;
}

const juce::StringArray& tableNames()
{
    static const juce::StringArray n { "SINE", "SAW", "SQUARE", "TRIANGLE", "PULSE", "ORGAN", "GLASS", "GHOST", "FM" };
    return n;
}

std::vector<float> tableCycle (int index)
{
    if (index <= 0 || index >= tableNames().size()) return {};
    std::vector<float> c ((size_t) kTableSize, 0.0f);
    auto add = [&] (int h, float a, float ph = 0.0f) { for (int i = 0; i < kTableSize; ++i) c[(size_t) i] += a * std::sin (kTwoPi * (float) h * (float) i / kTableSize + ph); };
    switch (index)
    {
        case 1: for (int h = 1; h < 512; ++h) add (h, 1.0f / (float) h); break;                                   // SAW
        case 2: for (int h = 1; h < 512; h += 2) add (h, 1.0f / (float) h); break;                                // SQUARE
        case 3: for (int h = 1; h < 512; h += 2) add (h, ((h / 2) % 2 ? -1.0f : 1.0f) / (float) (h * h)); break;  // TRIANGLE
        case 4: for (int h = 1; h < 512; ++h) add (h, std::sin (kPi * (float) h * 0.25f) / (float) h, kPi * 0.5f); break;   // PULSE (25%)
        case 5: { const int hs[] = { 1, 2, 3, 4, 6, 8 }; const float as[] = { 1.0f, 0.7f, 0.5f, 0.4f, 0.3f, 0.2f }; for (int k = 0; k < 6; ++k) add (hs[k], as[k]); } break;   // ORGAN
        case 6: for (int h = 1; h <= 15; h += 2) add (h, 1.0f / std::sqrt ((float) h), 0.7f * (float) h); break;   // GLASS
        case 7: for (int h = 1; h < 40; ++h)                                                                       // GHOST: a hollow "oo" vowel
                    add (h, 0.25f / (float) h + std::exp (-0.5f * juce::square (((float) h - 3.0f) / 1.2f)) + 0.6f * std::exp (-0.5f * juce::square (((float) h - 9.0f) / 2.0f)));
                break;
        case 8: for (int i = 0; i < kTableSize; ++i) { const float x = kTwoPi * (float) i / kTableSize; c[(size_t) i] = std::sin (x + 2.0f * std::sin (x)); } break;   // FM
        default: break;
    }
    return c;
}

std::vector<float> cycleFromFile (const juce::AudioBuffer<float>& b)
{
    const int n = b.getNumSamples(), ch = b.getNumChannels();
    if (n < 16 || ch < 1) return {};
    std::vector<float> mono ((size_t) n, 0.0f);
    for (int c = 0; c < ch; ++c) for (int i = 0; i < n; ++i) mono[(size_t) i] += b.getSample (c, i) / (float) ch;
    std::vector<float> out ((size_t) kTableSize);
    if (n >= kTableSize) std::copy (mono.begin(), mono.begin() + kTableSize, out.begin());   // a wavetable file: its first frame
    else for (int i = 0; i < kTableSize; ++i)                                                 // one short cycle: stretched to 2048
    {
        const float x = (float) i * (float) n / kTableSize; const int i0 = (int) x; const float fr = x - (float) i0;
        out[(size_t) i] = mono[(size_t) i0] + (mono[(size_t) ((i0 + 1) % n)] - mono[(size_t) i0]) * fr;
    }
    return out;
}

juce::String tableToText (const std::vector<float>& cycle)
{
    if (cycle.empty()) return {};
    juce::MemoryBlock mb (cycle.data(), cycle.size() * sizeof (float));
    return mb.toBase64Encoding();
}
std::vector<float> tableFromText (const juce::String& text)
{
    juce::MemoryBlock mb;
    if (text.isEmpty() || ! mb.fromBase64Encoding (text) || mb.getSize() != (size_t) kTableSize * sizeof (float)) return {};
    std::vector<float> c ((size_t) kTableSize);
    std::memcpy (c.data(), mb.getData(), mb.getSize());
    for (auto& v : c) if (! std::isfinite (v)) v = 0.0f;
    return c;
}

// ================================================================================================ one voice
void Voice::prepare (double sampleRate)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    order = sr > 60000.0 ? 12 : 11;          // ~43 ms frames at 44.1 / 48 k, the same at 88.2 / 96 k
    fftSize = 1 << order;
    hop = fftSize / 4;
    const int half = fftSize / 2;
    fft = std::make_unique<juce::dsp::FFT> (order);
    ring.assign ((size_t) fftSize, 0.0f);
    window.resize ((size_t) fftSize);
    winSum = 0.0f;
    for (int i = 0; i < fftSize; ++i) { window[(size_t) i] = 0.5f - 0.5f * std::cos (kTwoPi * (float) i / (float) fftSize); winSum += window[(size_t) i]; }
    for (auto* v : { &work, &cep, &noiseBuf }) v->assign ((size_t) fftSize * 2, 0.0f);
    for (auto* v : { &logMag, &prevLog, &env, &resid, &noiseEnv }) v->assign ((size_t) half + 1, 0.0f);
    synth.assign ((size_t) hop, 0.0f);
    outRing.assign ((size_t) fftSize * 4, 0.0f);    // (a power of two: the output is built ahead of where it plays)
    gainRing.assign ((size_t) fftSize * 4, 0.0f);
    peaks.reserve ((size_t) half);
    order_.reserve ((size_t) half);
    freeList.reserve (kMaxOscs);
    sortedActive.reserve (kMaxOscs);
    reset();
}

void Voice::reset()
{
    std::fill (ring.begin(), ring.end(), 0.0f);
    std::fill (synth.begin(), synth.end(), 0.0f);
    std::fill (outRing.begin(), outRing.end(), 0.0f);
    std::fill (gainRing.begin(), gainRing.end(), 0.0f);
    std::fill (noiseEnv.begin(), noiseEnv.end(), 0.0f);
    std::fill (prevLog.begin(), prevLog.end(), -30.0f);
    ringPos = synthPos = ringOut = 0;
    prevFlux = 0.0f; onsetNow = false;
    freeList.clear();
    for (int o = kMaxOscs - 1; o >= 0; --o)
    {
        active[(size_t) o] = false;
        aCur[(size_t) o] = aTgt[(size_t) o] = 0.0f;
        re[(size_t) o] = 1.0f; im[(size_t) o] = 0.0f;
        detune[(size_t) o] = rng.nextFloat() * 2.0f - 1.0f;
        freeList.push_back (o);
    }
    showCount.store (0); activeCount.store (0); level.store (0.0f);
}

void Voice::process (const float* in, float* out, int n, const Params& p, const Wavetable* table)
{
    if (fft == nullptr) { if (out != in) std::fill (out, out + n, 0.0f); return; }
    const int mask = (int) outRing.size() - 1;
    for (int i = 0; i < n; ++i)
    {
        const float x = in[i];
        const float dry = ring[(size_t) ringPos];          // the input from latency() samples ago (an attack plays as it was)
        const float o = outRing[(size_t) ringOut], g = gainRing[(size_t) ringOut];
        outRing[(size_t) ringOut] = 0.0f; gainRing[(size_t) ringOut] = 0.0f;
        ringOut = (ringOut + 1) & mask;
        out[i] = g > 0.0f ? o * (1.0f - g * g) + dry * (1.0f - (1.0f - g) * (1.0f - g)) : o;   // (about equal power: the two are not in phase)
        ring[(size_t) ringPos] = x;
        if (++ringPos == fftSize) ringPos = 0;
        if (++synthPos == hop)
        {
            synthPos = 0;
            analyse (p);
            renderHop (p, table);
            renderNoise (p);
        }
    }
}

float Voice::envAt (float hz) const
{
    const float k = juce::jlimit (0.0f, (float) (fftSize / 2) - 1.001f, hz * (float) fftSize / (float) sr);
    const int k0 = (int) k;
    const float fr = k - (float) k0;
    return env[(size_t) k0] + (env[(size_t) k0 + 1] - env[(size_t) k0]) * fr;
}

// INF / CLOUD / SMOOTH for one frame: how much of the new frame gets in, how fast things let go, how fast they glide
struct Moves { float u, rel, glide; };
static Moves movesFor (const Params& p, float hopSec)
{
    const float inf = juce::jlimit (0.0f, 1.0f, p[Inf]);
    const float cloud = juce::jlimit (0.0f, 1.0f, p[Cloud]), smooth = juce::jlimit (0.0f, 1.0f, p[Smooth]);
    return { inf >= 0.999f ? 0.0f : std::pow (1.0f - inf, 1.6f),
             cloud < 0.001f ? 1.0f : 1.0f - std::exp (-hopSec / (6.0f * cloud * cloud + 0.01f)),
             smooth < 0.001f ? 1.0f : 1.0f - std::exp (-hopSec / (1.2f * smooth * smooth + 0.005f)) };
}

void Voice::analyse (const Params& p)
{
    const int N = fftSize, half = N / 2;
    // ---- the frame, windowed, and its spectrum
    for (int j = 0; j < N; ++j) work[(size_t) j] = ring[(size_t) ((ringPos + j) % N)] * window[(size_t) j];
    std::fill (work.begin() + N, work.end(), 0.0f);
    fft->performRealOnlyForwardTransform (work.data(), true);
    float maxLog = -60.0f;
    for (int k = 0; k <= half; ++k)
    {
        const float a = work[(size_t) (2 * k)], b = work[(size_t) (2 * k + 1)];
        logMag[(size_t) k] = 0.5f * std::log (a * a + b * b + 1e-24f);
        maxLog = juce::jmax (maxLog, logMag[(size_t) k]);
    }
    const float toAmp = std::log (2.0f / winSum);   // log |X| -> log amplitude of a sinusoid

    // ---- an attack? (the spectrum suddenly louder across the board than the frame before)
    {
        float flux = 0.0f; int cnt = 0;
        const float lo = maxLog - 6.9f;   // the top 60 dB
        for (int k = 1; k <= half; ++k)
            if (logMag[(size_t) k] > lo) { flux += juce::jmax (0.0f, logMag[(size_t) k] - prevLog[(size_t) k]); ++cnt; }
        const float d = cnt > 0 ? flux / (float) cnt : 0.0f;
        onsetNow = d > 0.9f && d > 1.5f * prevFlux && maxLog + toAmp > std::log (3e-4f);
        if (onsetNow) onsets.fetch_add (1, std::memory_order_relaxed);
        prevFlux = d;
        std::copy (logMag.begin(), logMag.end(), prevLog.begin());
    }

    // ---- the cepstrum: the log spectrum's slow shape (the body) apart from its fast ripple (the harmonics)
    std::fill (cep.begin(), cep.end(), 0.0f);
    for (int k = 0; k <= half; ++k) cep[(size_t) (2 * k)] = logMag[(size_t) k];
    fft->performRealOnlyInverseTransform (cep.data());
    const int lifter = juce::jlimit (8, N / 4, juce::roundToInt (sr / 900.0));   // quefrencies under ~1.1 ms: the formants, not the pitch
    for (int q = lifter + 1; q < N - lifter; ++q) cep[(size_t) q] = 0.0f;
    std::fill (cep.begin() + N, cep.end(), 0.0f);
    fft->performRealOnlyForwardTransform (cep.data(), true);
    for (int k = 0; k <= half; ++k) env[(size_t) k] = cep[(size_t) (2 * k)] + toAmp;

    // ---- the peaks (each one a partial), placed between bins by a parabola through the log magnitudes. NOISE decides
    // what counts: a peak must stand this far above its neighbourhood to be a sine wave rather than part of the noise.
    peaks.clear();
    const float floorLog = juce::jmax (maxLog - 8.06f /* -70 dB */, std::log (1e-5f) - toAmp);
    const float prominence = 1.4f * juce::jlimit (0.0f, 1.0f, p[Noise]);   // up to ~12 dB
    const float binHz = (float) sr / (float) N;
    for (int k = 2; k < half - 1; ++k)
    {
        const float b = logMag[(size_t) k];
        if (b < floorLog || b <= logMag[(size_t) k - 1] || b < logMag[(size_t) k + 1]) continue;
        if (prominence > 0.0f)
        {
            float s = 0.0f; int c = 0;
            for (int j = juce::jmax (1, k - 10); j <= juce::jmin (half, k + 10); ++j) if (std::abs (j - k) > 2) { s += logMag[(size_t) j]; ++c; }
            if (c > 0 && b - s / (float) c < prominence) continue;
        }
        const float a = logMag[(size_t) k - 1], c = logMag[(size_t) k + 1];
        const float den = a - 2.0f * b + c;
        const float off = std::abs (den) > 1e-9f ? juce::jlimit (-0.5f, 0.5f, 0.5f * (a - c) / den) : 0.0f;
        const float pk = b - 0.25f * (a - c) * off;
        const float f = ((float) k + off) * binHz;
        if (f < 20.0f) continue;
        peaks.push_back ({ f, std::exp (pk + toAmp), 0.0f, 0.0f, (float) k + off });
    }
    const int want = juce::jlimit (16, kMaxOscs, juce::roundToInt (p[Partials]));
    if ((int) peaks.size() > want)
    {
        std::nth_element (peaks.begin(), peaks.begin() + want, peaks.end(), [] (const Peak& x, const Peak& y) { return x.amp > y.amp; });
        peaks.resize ((size_t) want);
    }
    const auto mv = movesFor (p, (float) hop / (float) sr);

    // ---- what is left once the partials are out: the noise (its smooth shape, holes filled from the neighbours)
    {
        for (int k = 0; k <= half; ++k) resid[(size_t) k] = std::exp (logMag[(size_t) k]);
        for (auto& pk : peaks)
        {
            const int b = juce::roundToInt (pk.bin);
            for (int j = juce::jmax (0, b - 2); j <= juce::jmin (half, b + 2); ++j) resid[(size_t) j] = -1.0f;
        }
        double s = 0.0; int c = 0;
        constexpr int W = 4;
        for (int j = 0; j <= juce::jmin (half, W - 1); ++j) if (resid[(size_t) j] >= 0) { s += (double) resid[(size_t) j] * resid[(size_t) j]; ++c; }
        for (int k = 0; k <= half; ++k)
        {
            const int add = k + W, drop = k - W - 1;
            if (add <= half && resid[(size_t) add] >= 0) { s += (double) resid[(size_t) add] * resid[(size_t) add]; ++c; }
            if (drop >= 0 && resid[(size_t) drop] >= 0) { s -= (double) resid[(size_t) drop] * resid[(size_t) drop]; --c; }
            const float target = c > 0 ? (float) std::sqrt (juce::jmax (0.0, s) / c) : 0.0f;
            float& e = noiseEnv[(size_t) k];
            float e1 = e + mv.u * (target - e);
            if (e1 < e) e1 = e + (e1 - e) * mv.rel;
            e = e + (e1 - e) * mv.glide;
        }
    }

    // ---- where each partial goes, and how loud
    const float ratio = std::exp2 (p[Transpose] / 12.0f), shiftHz = p[Shift];
    const float y = p[CepstrumY], wF = 1.0f - juce::jmax (0.0f, y), wE = 1.0f - juce::jmax (0.0f, -y);
    const float top = (float) sr * 0.47f;
    double eIn = 0.0, eOut = 0.0;
    for (auto& pk : peaks)
    {
        eIn += (double) pk.amp * pk.amp;
        pk.fOut = pk.fSrc * ratio + shiftHz;
        if (pk.fOut < 20.0f || pk.fOut > top) { pk.aOut = 0.0f; continue; }
        const float logA = wE * envAt (pk.fOut) + wF * (std::log (pk.amp) - envAt (pk.fSrc));
        pk.aOut = std::exp (juce::jlimit (-30.0f, 6.0f, logA));
        eOut += (double) pk.aOut * pk.aOut;
    }
    const float norm = eOut > 1e-20 ? (float) juce::jmin (100.0, std::sqrt (eIn / eOut)) : 0.0f;   // the same energy as the frame had
    for (auto& pk : peaks) pk.aOut *= norm;
    level.store ((float) std::sqrt (eIn));

    // ---- tracking: each peak continues the oscillator nearest to it (by its frequency in the sound), or starts one
    order_.clear();
    for (int i = 0; i < (int) peaks.size(); ++i) order_.push_back (i);
    std::sort (order_.begin(), order_.end(), [this] (int a, int b) { return peaks[(size_t) a].amp > peaks[(size_t) b].amp; });
    sortedActive.clear();
    for (int o = 0; o < kMaxOscs; ++o) { matched[(size_t) o] = false; if (active[(size_t) o]) sortedActive.push_back (o); }
    std::sort (sortedActive.begin(), sortedActive.end(), [this] (int a, int b) { return fSrc[(size_t) a] < fSrc[(size_t) b]; });

    const float u = mv.u, rel = mv.rel, glide = mv.glide;
    const float spread = juce::jlimit (0.0f, 1.0f, p[Cloud]) * 0.3f / 12.0f;   // CLOUD: up to 30 cents apart

    auto update = [&] (int o, float A, float F)
    {
        const size_t s = (size_t) o;
        F *= std::exp2 (detune[s] * spread);
        float a1 = aTgt[s] + u * (A - aTgt[s]);
        float f1 = fTgt[s] + u * (F - fTgt[s]);
        if (a1 < aTgt[s]) a1 = aTgt[s] + (a1 - aTgt[s]) * rel;
        aTgt[s] = aTgt[s] + (a1 - aTgt[s]) * glide;
        fTgt[s] = juce::jlimit (1.0f, top, fTgt[s] + (f1 - fTgt[s]) * glide);
    };

    for (int pi : order_)
    {
        const auto& pk = peaks[(size_t) pi];
        const float tol = juce::jmax (2.0f * binHz, 0.025f * pk.fSrc);
        auto it = std::lower_bound (sortedActive.begin(), sortedActive.end(), pk.fSrc, [this] (int o, float f) { return fSrc[(size_t) o] < f; });
        int best = -1; float bestD = tol;
        for (auto j = it; j != sortedActive.end(); ++j)   // upward
        {
            const float d = fSrc[(size_t) *j] - pk.fSrc;
            if (d > bestD) break;
            if (! matched[(size_t) *j]) { bestD = d; best = *j; break; }
        }
        for (auto j = it; j != sortedActive.begin();)     // downward
        {
            --j;
            const float d = pk.fSrc - fSrc[(size_t) *j];
            if (d > bestD) break;
            if (! matched[(size_t) *j]) { bestD = d; best = *j; break; }
        }
        if (best >= 0)
        {
            matched[(size_t) best] = true;
            fSrc[(size_t) best] += u * (pk.fSrc - fSrc[(size_t) best]);
            update (best, pk.aOut, pk.fOut);
        }
        else if (u > 0.001f && ! freeList.empty() && pk.aOut > 0.0f)
        {
            const int o = freeList.back(); freeList.pop_back();
            const size_t s = (size_t) o;
            active[s] = true; matched[s] = true;
            fSrc[s] = pk.fSrc;
            fTgt[s] = fCur[s] = juce::jlimit (1.0f, top, pk.fOut * std::exp2 (detune[s] * spread));
            aCur[s] = 0.0f;
            aTgt[s] = pk.aOut * u;
            const float th = rng.nextFloat() * kTwoPi;
            re[s] = std::cos (th); im[s] = std::sin (th);
        }
    }
    int count = 0, shown = 0;
    for (int o = 0; o < kMaxOscs; ++o)
    {
        const size_t s = (size_t) o;
        if (! active[s]) continue;
        if (! matched[s]) update (o, 0.0f, fTgt[s] / std::exp2 (detune[s] * spread));   // (no new frame: it lets go where it is)
        if (! matched[s] && aTgt[s] < 1e-7f && aCur[s] < 1e-7f) { active[s] = false; freeList.push_back (o); continue; }
        ++count;
        if (shown < kShow && aTgt[s] > 1e-6f) { showFreq[(size_t) shown].store (fTgt[s], std::memory_order_relaxed); showAmp[(size_t) shown].store (aTgt[s], std::memory_order_relaxed); ++shown; }
    }
    showCount.store (shown, std::memory_order_relaxed);
    activeCount.store (count, std::memory_order_relaxed);
}

void Voice::renderHop (const Params& p, const Wavetable* table)
{
    // pack the live oscillators, with PHASE's pull on each one's phase over this hop
    const float x = juce::jlimit (-1.0f, 1.0f, p[PhaseX]);
    const float lock = juce::jmax (0.0f, -x), scatter = juce::jmax (0.0f, x);
    const float invSr = 1.0f / (float) sr, H = (float) hop;
    int m = 0;
    for (int o = 0; o < kMaxOscs; ++o)
    {
        const size_t s = (size_t) o;
        if (! active[s]) continue;
        const size_t k = (size_t) m++;
        pIdx[k] = o;
        pRe[k] = re[s]; pIm[k] = im[s];
        pAmp[k] = aCur[s]; pDa[k] = (aTgt[s] - aCur[s]) / H;
        pF0[k] = fCur[s]; pF1[k] = fTgt[s];
        float dw = 0.0f;
        const float ph = std::atan2 (im[s], re[s]);
        if (lock > 0.0f || scatter > 0.0f)
        {
            const float phEnd = ph + kPi * (fCur[s] + fTgt[s]) * invSr * H;
            const float toZero = wrapPi (-phEnd);
            const float jump = (rng.nextFloat() * 2.0f - 1.0f) * kPi;
            dw = (lock * toZero + scatter * jump) / H;
        }
        pDw[k] = dw;
        if (table != nullptr) { float c = ph / kTwoPi; if (c < 0) c += 1.0f; pPh[k] = c; }
    }
    const int m8 = (m + 7) & ~7;
    for (int k = m; k < m8; ++k) { pRe[(size_t) k] = pIm[(size_t) k] = pAmp[(size_t) k] = pDa[(size_t) k] = 0.0f; pC[(size_t) k] = 1.0f; pS[(size_t) k] = 0.0f; pF0[(size_t) k] = pF1[(size_t) k] = 0.0f; pDw[(size_t) k] = 0.0f; }

    constexpr int S = 32;
    const int nsub = hop / S;
    float* out = synth.data();
    for (int sb = 0; sb < nsub; ++sb)
    {
        const float t = ((float) sb + 0.5f) / (float) nsub;
        if (table == nullptr)
        {
            for (int k = 0; k < m; ++k)
            {
                const float f = pF0[(size_t) k] + (pF1[(size_t) k] - pF0[(size_t) k]) * t;
                const float w = kTwoPi * f * invSr + pDw[(size_t) k];
                pC[(size_t) k] = std::cos (w); pS[(size_t) k] = std::sin (w);
            }
            float* RE = pRe.data(); float* IM = pIm.data(); float* AM = pAmp.data();
            const float* DA = pDa.data(); const float* C = pC.data(); const float* SN = pS.data();
            for (int i = 0; i < S; ++i)
            {
                float acc[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
                for (int k = 0; k < m8; k += 8)
                    for (int j = 0; j < 8; ++j)
                    {
                        const int q = k + j;
                        const float r = RE[q], v = IM[q];
                        acc[j] += AM[q] * r;
                        RE[q] = r * C[q] - v * SN[q];
                        IM[q] = r * SN[q] + v * C[q];
                        AM[q] += DA[q];
                    }
                out[sb * S + i] = ((acc[0] + acc[1]) + (acc[2] + acc[3])) + ((acc[4] + acc[5]) + (acc[6] + acc[7]));
            }
            for (int k = 0; k < m; ++k)   // keep every rotator on the unit circle
            {
                const float g = 1.5f - 0.5f * (RE[k] * RE[k] + IM[k] * IM[k]);
                RE[k] *= g; IM[k] *= g;
            }
        }
        else
        {
            // WAVETABLE: each partial reads the table copy that has no harmonic past Nyquist at its pitch
            for (int k = 0; k < m; ++k)
            {
                const float f = juce::jmax (1.0f, pF0[(size_t) k] + (pF1[(size_t) k] - pF0[(size_t) k]) * t);
                pInc[(size_t) k] = f * invSr + pDw[(size_t) k] / kTwoPi;
                const float maxH = 0.45f * (float) sr / f;
                const int L = maxH >= (float) (kTableSize / 2) ? 0 : juce::jlimit (0, kTableLevels - 1, (int) std::ceil (std::log2 ((float) (kTableSize / 2) / juce::jmax (1.0f, maxH))));
                pTab[(size_t) k] = table->level[(size_t) L].data();
            }
            for (int i = 0; i < S; ++i)
            {
                float acc = 0.0f;
                for (int k = 0; k < m; ++k)
                {
                    float ph = pPh[(size_t) k];
                    const float xx = ph * (float) kTableSize;
                    const int i0 = juce::jlimit (0, kTableSize - 1, (int) xx);
                    const float fr = xx - (float) i0;
                    const float* tb = pTab[(size_t) k];
                    acc += pAmp[(size_t) k] * (tb[i0] + (tb[i0 + 1] - tb[i0]) * fr);
                    ph += pInc[(size_t) k];
                    if (ph >= 1.0f) ph -= 1.0f; else if (ph < 0.0f) ph += 1.0f;
                    pPh[(size_t) k] = ph;
                    pAmp[(size_t) k] += pDa[(size_t) k];
                }
                out[sb * S + i] = acc;
            }
        }
    }
    for (int k = 0; k < m; ++k)
    {
        const size_t s = (size_t) pIdx[(size_t) k];
        if (table != nullptr) { re[s] = std::cos (kTwoPi * pPh[(size_t) k]); im[s] = std::sin (kTwoPi * pPh[(size_t) k]); }
        else { re[s] = pRe[(size_t) k]; im[s] = pIm[(size_t) k]; }
        aCur[s] = aTgt[s]; fCur[s] = fTgt[s];
    }
    // the partials play one hop later than they are made, so they line up with the noise frame made now
    const int mask = (int) outRing.size() - 1;
    for (int i = 0; i < hop; ++i) outRing[(size_t) ((ringOut + hop + i) & mask)] += synth[(size_t) i];
}

void Voice::renderNoise (const Params& p)
{
    const int N = fftSize, half = N / 2, mask = (int) outRing.size() - 1;
    const float amt = juce::jmin (1.0f, 3.0f * juce::jlimit (0.0f, 1.0f, p[Noise]));
    if (amt > 0.001f)
    {
        // the noise frame: its shape with random phases, windowed and overlapped (4/3: a Hann at 75% overlap, on both ends)
        std::fill (noiseBuf.begin(), noiseBuf.end(), 0.0f);
        for (int k = 1; k < half; ++k)
        {
            const float m = noiseEnv[(size_t) k] * amt, th = rng.nextFloat() * kTwoPi;
            noiseBuf[(size_t) (2 * k)] = m * std::cos (th);
            noiseBuf[(size_t) (2 * k + 1)] = m * std::sin (th);
        }
        fft->performRealOnlyInverseTransform (noiseBuf.data());
        for (int j = 0; j < N; ++j) outRing[(size_t) ((ringOut + j) & mask)] += noiseBuf[(size_t) j] * window[(size_t) j] * (4.0f / 3.0f);
    }
    // TRANSIENT: an attack in the newest part of this frame plays as it was. It lands latency() after it came in: three
    // to four hops from now. A quick fade in just before, held through the hop, then a 35 ms fade back.
    const float tr = juce::jlimit (0.0f, 1.0f, p[Transient]) * (p[Inf] >= 0.999f ? 0.0f : 1.0f);
    if (onsetNow && tr > 0.001f)
    {
        const int pre = hop / 4, start = 3 * hop - pre, hold = 4 * hop, decay = (int) (0.035 * sr);
        const int end = juce::jmin ((int) outRing.size() - 1, hold + decay);
        for (int k = start; k < end; ++k)
        {
            const float e = k < 3 * hop ? (float) (k - start) / (float) pre : (k < hold ? 1.0f : std::exp (-3.0f * (float) (k - hold) / (float) decay));
            float& g = gainRing[(size_t) ((ringOut + k) & mask)];
            g = juce::jmax (g, tr * e);
        }
    }
}

// ================================================================================================ the stereo engine
Engine::Engine() : tables (new std::array<Wavetable, 3>()) {}

void Engine::setTable (const std::vector<float>& cycle)
{
    const int now = tableNow.load(), used = tableInUse.load();
    int s = 0;
    while (s == now || s == used) ++s;
    (*tables)[(size_t) s].build (cycle);
    tableNow.store (s);
}

void Engine::prepare (double sampleRate, int maxBlock)
{
    mid.prepare (sampleRate);
    side.prepare (sampleRate);
    const size_t b = (size_t) juce::jmax (64, maxBlock);
    for (auto* v : { &inM, &inS, &wetM, &wetS }) v->assign (b, 0.0f);
    delayM.assign ((size_t) latency(), 0.0f);
    delayS.assign ((size_t) latency(), 0.0f);
    limRelease = (float) std::exp (-1.0 / (0.08 * sampleRate));   // lets go over ~80 ms
    isPrepared = true;
    reset();
}

void Engine::reset()
{
    mid.reset(); side.reset();
    std::fill (delayM.begin(), delayM.end(), 0.0f);
    std::fill (delayS.begin(), delayS.end(), 0.0f);
    delayPos = 0;
    limEnv = 0.0f;
    for (auto& s : scope) s.store (0.0f, std::memory_order_relaxed);
    sideLevel.store (0.0f);
}

void Engine::process (float* left, float* right, int n, const Params& p)
{
    if (! isPrepared) return;
    int ti = tableNow.load();
    tableInUse.store (ti);
    if (tableNow.load() != ti) { ti = tableNow.load(); tableInUse.store (ti); }   // (it moved while we looked: take the new one)
    const Wavetable* table = (*tables)[(size_t) ti].sine ? nullptr : &(*tables)[(size_t) ti];
    const float mixM = juce::jlimit (0.0f, 1.0f, p[Mid]), mixS = juce::jlimit (0.0f, 1.0f, p[Side]);
    const float gainWant = juce::Decibels::decibelsToGain (p[Output]);
    const int chunk = (int) inM.size(), L = (int) delayM.size();
    for (int off = 0; off < n; off += chunk)
    {
        const int len = juce::jmin (chunk, n - off);
        float* l = left + off; float* r = right + off;
        double eM = 0.0, eS = 0.0;
        for (int i = 0; i < len; ++i)
        {
            inM[(size_t) i] = 0.5f * (l[i] + r[i]); inS[(size_t) i] = 0.5f * (l[i] - r[i]);
            eM += (double) inM[(size_t) i] * inM[(size_t) i]; eS += (double) inS[(size_t) i] * inS[(size_t) i];
        }
        const float ratio = eM + eS > 1e-12 ? (float) std::sqrt (eS / (eM + eS)) : 0.0f;
        sideLevel.store (juce::jmax (ratio, sideLevel.load (std::memory_order_relaxed) * 0.995f), std::memory_order_relaxed);
        mid.process (inM.data(), wetM.data(), len, p, table);
        side.process (inS.data(), wetS.data(), len, p, table);
        int sp = scopePos.load (std::memory_order_relaxed);
        for (int i = 0; i < len; ++i)
        {
            const float dm = delayM[(size_t) delayPos], ds = delayS[(size_t) delayPos];
            delayM[(size_t) delayPos] = inM[(size_t) i]; delayS[(size_t) delayPos] = inS[(size_t) i];
            if (++delayPos == L) delayPos = 0;
            gainNow += (gainWant - gainNow) * 0.002f;
            const float M = (dm + (wetM[(size_t) i] - dm) * mixM) * gainNow;
            const float Sd = (ds + (wetS[(size_t) i] - ds) * mixS) * gainNow;
            // a safety limiter: PHASE's robot lines every partial up into one spike, and OUTPUT can add 12 dB on top
            const float pk = juce::jmax (std::abs (M + Sd), std::abs (M - Sd));
            limEnv = juce::jmax (pk, limEnv * limRelease);
            const float lg = limEnv > 0.977f ? 0.977f / limEnv : 1.0f;
            l[i] = (M + Sd) * lg; r[i] = (M - Sd) * lg;
            scope[(size_t) sp].store (wetM[(size_t) i], std::memory_order_relaxed);
            if (++sp == kScope) sp = 0;
        }
        scopePos.store (sp, std::memory_order_relaxed);
    }
}

juce::AudioBuffer<float> renderOffline (const juce::AudioBuffer<float>& in, double sampleRate, const Params& p, const std::vector<float>& table, const std::atomic<bool>* cancel)
{
    auto e = std::make_unique<Engine>();
    constexpr int block = 4096;
    e->prepare (sampleRate, block);
    e->setTable (table);
    const int lat = e->latency();
    const float cloud = p[Cloud], inf = p[Inf];
    const double ringSec = 0.2 + 6.0 * cloud * cloud * 2.5 + 1.5 * p[Smooth] + (inf > 0.5f ? 4.0 * inf : 0.0);
    const int len = in.getNumSamples(), total = len + lat + (int) (ringSec * sampleRate);
    juce::AudioBuffer<float> buf (2, total);
    buf.clear();
    for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, in, juce::jmin (c, in.getNumChannels() - 1), 0, len);
    for (int i = 0; i < total; i += block)
    {
        if (cancel != nullptr && cancel->load()) return {};
        e->process (buf.getWritePointer (0) + i, buf.getWritePointer (1) + i, juce::jmin (block, total - i), p);
    }
    // take the latency off the front; trim the silent end (keeping 50 ms)
    int end = total - lat;
    const float floor = juce::jmax (1e-5f, buf.getMagnitude (0, total) * 0.0005f);
    while (end > len && juce::jmax (std::abs (buf.getSample (0, lat + end - 1)), std::abs (buf.getSample (1, lat + end - 1))) < floor) --end;
    end = juce::jmin (total - lat, end + (int) (0.05 * sampleRate));
    juce::AudioBuffer<float> out (2, juce::jmax (1, end));
    for (int c = 0; c < 2; ++c) out.copyFrom (c, 0, buf, c, lat, out.getNumSamples());
    return out;
}
}
