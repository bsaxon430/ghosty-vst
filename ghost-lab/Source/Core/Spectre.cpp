// Ghost Lab - AGPL-3.0-only (see LICENSE, NOTICE.md). SPECTRE, the real-time resynthesis room (Spectre.h).
#include "Spectre.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace glab::spectre
{
static const ParamInfo kInfo[kParams] = {
    { "phasemix",  "PHASE",       "left: your sound's own phase. Right: the rebuilt sound's phase", 0.0f, 1.0f, 1.0f, "", false },
    { "cepmix",    "CEPSTRUM",    "down: your sound's own tone. Up: the rebuilt sound's tone", 0.0f, 1.0f, 1.0f, "", false },
    { "mode",      "MODE",        "IF: clean, each oscillator stays in its own bin. PEAK: the loudest peaks swap oscillators (sparkly with SMOOTH). TRACK: each peak followed by one oscillator", 0.0f, 2.0f, 0.0f, "", false },
    { "formant",   "SHIFT",       "moves the formants (the body of the sound) up or down without changing the notes", -12.0f, 12.0f, 0.0f, "st", true },
    { "transpose", "TRANSPOSE",   "moves the oscillators' notes up or down in semitones", -12.0f, 12.0f, 0.0f, "st", true },
    { "locd",      "LOCD",        "locks every phase to a sawtooth at the note's own pitch: solid and buzzy, best on bass", 0.0f, 1.0f, 0.0f, "", false },
    { "smooth",    "SMOOTH",      "each partial glides from where it was instead of jumping", 0.0f, 1.0f, 0.0f, "", false },
    { "cloud",     "CLOUD",       "partials ring on and drift apart into a wash", 0.0f, 1.0f, 0.0f, "", false },
    { "inf",       "INF",         "hold: at the top the sound freezes forever", 0.0f, 1.0f, 0.0f, "", false },
    { "midoscs",   "MID",         "how many oscillators rebuild the middle (0: the middle is left as it was)", 0.0f, 512.0f, 512.0f, "", false },
    { "sideoscs",  "SIDE",        "how many oscillators rebuild the stereo sides (0: the sides are left as they were)", 0.0f, 512.0f, 512.0f, "", false },
    { "link",      "LINK",        "MID and SIDE move together", 0.0f, 1.0f, 1.0f, "", false },
    { "rate",      "UPDATE RATE", "how often the oscillators take a new picture of the sound: slower is steppier", 0.0f, 1000.0f, 0.0f, "ms", false },
    { "sync",      "SNAPPED",     "UPDATE RATE follows the song's tempo (FREE: in milliseconds)", 0.0f, 1.0f, 0.0f, "", false },
    { "division",  "DIVISION",    "UPDATE RATE in note lengths of the song", 0.0f, 6.0f, 3.0f, "", false },
    { "noise",     "NOISE",       "up: hiss, breath and drums stay noise. Down: everything becomes oscillators (glassy, tuned)", 0.0f, 1.0f, 0.7f, "", false },
    { "transient", "TRANSIENT",   "lets the first few milliseconds of every hit through as they were, so drums keep their punch", 0.0f, 1.0f, 0.7f, "", false },
    { "output",    "OUTPUT",      "how loud it comes out", -24.0f, 12.0f, 0.0f, "dB", true },
};

const ParamInfo& param (int p) { return kInfo[juce::jlimit (0, kParams - 1, p)]; }
int paramIndex (const juce::String& key)
{
    for (int i = 0; i < kParams; ++i) if (key == kInfo[i].key) return i;
    return -1;
}
Params::Params() { for (int i = 0; i < kParams; ++i) v[(size_t) i] = kInfo[i].def; }
const juce::StringArray& modeNames() { static const juce::StringArray n { "IF", "PEAK", "TRACK" }; return n; }
const juce::StringArray& divisionNames() { static const juce::StringArray n { "1/64", "1/32", "1/16", "1/8", "1/4", "1/2", "1 BAR" }; return n; }
bool isStepped (int p) { return p == Mode || p == Transpose || p == MidOscs || p == SideOscs || p == Link || p == Sync || p == Division; }

float toNorm (int p, float v)
{
    const auto& in = param (p);
    if (p == Rate) return std::sqrt (juce::jlimit (0.0f, 1.0f, v / in.hi));   // (fine near the fast end)
    return juce::jlimit (0.0f, 1.0f, (v - in.lo) / (in.hi - in.lo));
}
float fromNorm (int p, float t)
{
    const auto& in = param (p);
    t = juce::jlimit (0.0f, 1.0f, t);
    if (p == Rate) return in.hi * t * t;
    const float v = in.lo + t * (in.hi - in.lo);
    return isStepped (p) ? (float) juce::roundToInt (v) : v;
}
juce::String display (int p, float v)
{
    switch (p)
    {
        case PhaseX: case CepstrumY: return v <= 0.005f ? "DRY" : (v >= 0.995f ? "REBUILT" : juce::String (juce::roundToInt (v * 100)) + "%");
        case Mode:      return modeNames()[juce::jlimit (0, 2, juce::roundToInt (v))];
        case Shift:     return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " st";
        case Transpose: return (v > 0.5f ? "+" : "") + juce::String (juce::roundToInt (v)) + " st";
        case MidOscs: case SideOscs: return v < 0.5f ? "OFF" : juce::String (juce::roundToInt (v));
        case Link: case Sync: return v >= 0.5f ? "ON" : "OFF";
        case Rate:      return v < 12.0f ? "EVERY 11 MS" : juce::String (juce::roundToInt (v)) + " ms";
        case Division:  return divisionNames()[juce::jlimit (0, 6, juce::roundToInt (v))];
        case Output:    return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB";
        case Inf:       return v >= 0.999f ? "INF" : juce::String (juce::roundToInt (v * 100)) + "%";
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
    for (auto* v : { &logMag, &phase, &prevPhase, &prevLog, &env, &resid, &noiseEnv }) v->assign ((size_t) half + 1, 0.0f);
    synth.assign ((size_t) hop, 0.0f);
    outRing.assign ((size_t) fftSize * 4, 0.0f);
    gainRing.assign ((size_t) fftSize * 4, 0.0f);
    peaks.reserve ((size_t) half);
    order_.reserve ((size_t) half);
    freeList.reserve (kMaxOscs);
    sortedActive.reserve (kMaxOscs);
    reset();
}

void Voice::freeAll()
{
    freeList.clear();
    for (int o = kMaxOscs - 1; o >= 0; --o)
    {
        active[(size_t) o] = false; hasTgt[(size_t) o] = false;
        aCur[(size_t) o] = aTgt[(size_t) o] = 0.0f;
        re[(size_t) o] = 1.0f; im[(size_t) o] = 0.0f;
        freeList.push_back (o);
    }
}

void Voice::reset()
{
    std::fill (ring.begin(), ring.end(), 0.0f);
    std::fill (synth.begin(), synth.end(), 0.0f);
    std::fill (outRing.begin(), outRing.end(), 0.0f);
    std::fill (gainRing.begin(), gainRing.end(), 0.0f);
    std::fill (noiseEnv.begin(), noiseEnv.end(), 0.0f);
    std::fill (prevLog.begin(), prevLog.end(), -30.0f);
    std::fill (prevPhase.begin(), prevPhase.end(), 0.0f);
    ringPos = synthPos = ringOut = holdCount = 0;
    prevFlux = 0.0f; onsetNow = false; freshFrame = true;
    for (int o = 0; o < kMaxOscs; ++o) detune[(size_t) o] = rng.nextFloat() * 2.0f - 1.0f;
    freeAll();
    showCount.store (0); activeCount.store (0); level.store (0.0f);
}

void Voice::process (const float* in, float* out, int n, const Params& p, const Wavetable* table, int oscs, double bpm)
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
        out[i] = g > 0.0f ? o * (1.0f - g * g) + dry * (1.0f - (1.0f - g) * (1.0f - g)) : o;   // (about equal power)
        ring[(size_t) ringPos] = x;
        if (++ringPos == fftSize) ringPos = 0;
        if (++synthPos == hop)
        {
            synthPos = 0;
            analyse (p, oscs, bpm);
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

void Voice::analyse (const Params& p, int oscs, double bpm)
{
    const int N = fftSize, half = N / 2;
    const float binHz = (float) sr / (float) N;
    // ---- the frame, windowed and centred (so each bin's phase is the phase at the middle of the frame)
    for (int j = 0; j < N; ++j) work[(size_t) ((j + half) % N)] = ring[(size_t) ((ringPos + j) % N)] * window[(size_t) j];
    std::fill (work.begin() + N, work.end(), 0.0f);
    fft->performRealOnlyForwardTransform (work.data(), true);
    float maxLog = -60.0f;
    for (int k = 0; k <= half; ++k)
    {
        const float a = work[(size_t) (2 * k)], b = work[(size_t) (2 * k + 1)];
        logMag[(size_t) k] = 0.5f * std::log (a * a + b * b + 1e-24f);
        phase[(size_t) k] = std::atan2 (b, a);
        maxLog = juce::jmax (maxLog, logMag[(size_t) k]);
    }
    const float toAmp = std::log (2.0f / winSum);

    // ---- an attack? (the spectrum suddenly louder across the board than the frame before)
    {
        float flux = 0.0f; int cnt = 0;
        const float lo = maxLog - 6.9f;
        for (int k = 1; k <= half; ++k)
            if (logMag[(size_t) k] > lo) { flux += juce::jmax (0.0f, logMag[(size_t) k] - prevLog[(size_t) k]); ++cnt; }
        const float d = cnt > 0 ? flux / (float) cnt : 0.0f;
        onsetNow = d > 0.9f && d > 1.5f * prevFlux && maxLog + toAmp > std::log (3e-4f);
        if (onsetNow) onsets.fetch_add (1, std::memory_order_relaxed);
        prevFlux = d;
        std::copy (logMag.begin(), logMag.end(), prevLog.begin());
    }

    // ---- UPDATE RATE: between pictures the oscillators keep what they had (and run free)
    {
        double holdSec = (double) p[Rate] / 1000.0;
        if (p[Sync] >= 0.5f)
        {
            static const double beats[] = { 1.0 / 16.0, 1.0 / 8.0, 0.25, 0.5, 1.0, 2.0, 4.0 };
            holdSec = beats[juce::jlimit (0, 6, juce::roundToInt (p[Division]))] * 60.0 / juce::jmax (20.0, bpm);
        }
        const int holdHops = juce::jmax (1, (int) std::lround (holdSec * sr / hop));
        if (++holdCount < holdHops)
        {
            freshFrame = false;
            std::copy (phase.begin(), phase.end(), prevPhase.begin());
            return;
        }
        holdCount = 0;
        freshFrame = true;
    }
    const int mode = juce::jlimit (0, 2, juce::roundToInt (p[Mode]));
    if (mode != lastMode) { freeAll(); lastMode = mode; }   // (each mode numbers its oscillators its own way)

    // ---- the cepstral envelope (for NOISE's peak test and the energy of the noise)
    std::fill (cep.begin(), cep.end(), 0.0f);
    for (int k = 0; k <= half; ++k) cep[(size_t) (2 * k)] = logMag[(size_t) k];
    fft->performRealOnlyInverseTransform (cep.data());
    const int lifter = juce::jlimit (8, N / 4, juce::roundToInt (sr / 900.0));
    for (int q = lifter + 1; q < N - lifter; ++q) cep[(size_t) q] = 0.0f;
    std::fill (cep.begin() + N, cep.end(), 0.0f);
    fft->performRealOnlyForwardTransform (cep.data(), true);
    for (int k = 0; k <= half; ++k) env[(size_t) k] = cep[(size_t) (2 * k)] + toAmp;

    // ---- the peaks. NOISE decides what counts: a peak must stand this far above its neighbourhood to be an oscillator.
    peaks.clear();
    const float floorLog = juce::jmax (maxLog - 8.06f /* -70 dB */, std::log (1e-5f) - toAmp);
    const float prominence = 1.4f * juce::jlimit (0.0f, 1.0f, p[Noise]);
    const float expect = kTwoPi * (float) hop / (float) N;   // a bin's phase advance per hop, per bin index
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
        float f = ((float) k + off) * binHz;
        if (mode == ModeIF)
        {
            // the bin's instantaneous frequency (from how far its phase moved since the last frame), kept inside its bin
            const float dev = wrapPi (phase[(size_t) k] - prevPhase[(size_t) k] - expect * (float) k);
            f = juce::jlimit (((float) k - 1.0f) * binHz, ((float) k + 1.0f) * binHz, ((float) k + dev / expect) * binHz);
        }
        if (f < 20.0f) continue;
        peaks.push_back ({ f, std::exp (pk + toAmp), 0.0f, 0.0f, (float) k, phase[(size_t) k] });
    }
    std::copy (phase.begin(), phase.end(), prevPhase.begin());
    const int want = juce::jlimit (0, 512, oscs);
    std::sort (peaks.begin(), peaks.end(), [] (const Peak& x, const Peak& y) { return x.amp > y.amp; });   // loudest first
    if ((int) peaks.size() > want) peaks.resize ((size_t) want);

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
            float target = c > 0 ? (float) std::sqrt (juce::jmax (0.0, s) / c) : 0.0f;
            // never above the spectrum's own floor here (+6 dB): between the harmonics of a clean note there is no
            // noise to add, only the skirts of the harmonics themselves
            float floorHere = logMag[(size_t) k];
            for (int j = juce::jmax (0, k - 6); j <= juce::jmin (half, k + 6); ++j) floorHere = juce::jmin (floorHere, logMag[(size_t) j]);
            target = juce::jmin (target, 4.5f * std::exp (floorHere));   // (+13 dB: plain noise keeps its level)
            float& e = noiseEnv[(size_t) k];
            float e1 = e + mv.u * (target - e);
            if (e1 < e) e1 = e + (e1 - e) * mv.rel;
            e = e + (e1 - e) * mv.glide;
        }
    }

    // ---- where each partial goes (TRANSPOSE moves the oscillators; the tone is the morph's business)
    const float tr = (float) juce::roundToInt (p[Transpose]);
    const float ratio = std::exp2 (tr / 12.0f);
    const float top = (float) sr * 0.47f;
    double eIn = 0.0;
    for (auto& pk : peaks)
    {
        eIn += (double) pk.amp * pk.amp;
        pk.fOut = pk.fSrc * ratio;
        pk.aOut = pk.fOut < 20.0f || pk.fOut > top ? 0.0f : pk.amp;
    }
    level.store ((float) std::sqrt (eIn));
    // the oscillators land on each partial's own phase (so the rebuilt sound lines up with yours) - unless something is
    // meant to move them off it (transposed, gliding, ringing, held)
    const bool landOnPhase = tr == 0.0f && p[Smooth] < 0.02f && p[Cloud] < 0.02f && p[Inf] < 0.02f;

    const float u = mv.u, rel = mv.rel, glide = mv.glide;
    const float spread = juce::jlimit (0.0f, 1.0f, p[Cloud]) * 0.3f / 12.0f;
    const float hopSecs = (float) hop / (float) sr;
    for (int o = 0; o < kMaxOscs; ++o) { matched[(size_t) o] = false; hasTgt[(size_t) o] = false; }

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
    auto start = [&] (int o, const Peak& pk)
    {
        const size_t s = (size_t) o;
        active[s] = true; matched[s] = true;
        fSrc[s] = pk.fSrc;
        fTgt[s] = fCur[s] = juce::jlimit (1.0f, top, pk.fOut * std::exp2 (detune[s] * spread));
        aCur[s] = 0.0f;
        aTgt[s] = pk.aOut * u;
        // begin where it will be on its own phase at the end of this hop
        const float th = landOnPhase ? pk.ph - kTwoPi * fCur[s] * hopSecs : rng.nextFloat() * kTwoPi;
        re[s] = std::cos (th); im[s] = std::sin (th);
    };
    auto aim = [&] (int o, const Peak& pk) { if (landOnPhase) { tgtPh[(size_t) o] = pk.ph; hasTgt[(size_t) o] = true; } };

    if (mode == ModeIF || mode == ModePeak)
    {
        // IF: oscillator = bin. PEAK: oscillator n = the n-th loudest peak of this frame (so peaks swap oscillators).
        for (int i = 0; i < (int) peaks.size(); ++i)
        {
            const auto& pk = peaks[(size_t) i];
            const int o = mode == ModeIF ? (int) pk.bin : i;
            if (o < 0 || o >= kMaxOscs) continue;
            if (active[(size_t) o]) { matched[(size_t) o] = true; fSrc[(size_t) o] = pk.fSrc; update (o, pk.aOut, pk.fOut); }
            else if (u > 0.001f && pk.aOut > 0.0f) start (o, pk);
            aim (o, pk);
        }
    }
    else
    {
        // TRACK: each peak continues the oscillator nearest to it (by its frequency in the sound), or starts one
        sortedActive.clear();
        for (int o = 0; o < kMaxOscs; ++o) if (active[(size_t) o]) sortedActive.push_back (o);
        std::sort (sortedActive.begin(), sortedActive.end(), [this] (int a, int b) { return fSrc[(size_t) a] < fSrc[(size_t) b]; });
        for (const auto& pk : peaks)
        {
            const float tol = juce::jmax (2.0f * binHz, 0.025f * pk.fSrc);
            auto it = std::lower_bound (sortedActive.begin(), sortedActive.end(), pk.fSrc, [this] (int o, float f) { return fSrc[(size_t) o] < f; });
            int best = -1; float bestD = tol;
            for (auto j = it; j != sortedActive.end(); ++j)
            {
                const float d = fSrc[(size_t) *j] - pk.fSrc;
                if (d > bestD) break;
                if (! matched[(size_t) *j]) { bestD = d; best = *j; break; }
            }
            for (auto j = it; j != sortedActive.begin();)
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
                aim (best, pk);
            }
            else if (u > 0.001f && ! freeList.empty() && pk.aOut > 0.0f)
            {
                const int o = freeList.back(); freeList.pop_back();
                start (o, pk); aim (o, pk);
            }
        }
    }
    int count = 0, shown = 0;
    for (int o = 0; o < kMaxOscs; ++o)
    {
        const size_t s = (size_t) o;
        if (! active[s]) continue;
        if (! matched[s]) update (o, 0.0f, fTgt[s] / std::exp2 (detune[s] * spread));   // (gone from this frame: it lets go)
        if (! matched[s] && aTgt[s] < 1e-7f && aCur[s] < 1e-7f)
        {
            active[s] = false;
            if (mode == ModeTrack) freeList.push_back (o);
            continue;
        }
        ++count;
        if (shown < kShow && aTgt[s] > 1e-6f) { showFreq[(size_t) shown].store (fTgt[s], std::memory_order_relaxed); showAmp[(size_t) shown].store (aTgt[s], std::memory_order_relaxed); ++shown; }
    }
    showCount.store (shown, std::memory_order_relaxed);
    activeCount.store (count, std::memory_order_relaxed);
}

void Voice::renderHop (const Params&, const Wavetable* table)
{
    const float invSr = 1.0f / (float) sr, H = (float) hop;
    int m = 0;
    for (int o = 0; o < kMaxOscs; ++o)
    {
        const size_t s = (size_t) o;
        if (! active[s]) continue;
        const size_t k = (size_t) m++;
        pIdx[k] = o;
        pRe[k] = re[s]; pIm[k] = im[s];
        pAmp[k] = aCur[s]; pDa[k] = freshFrame ? (aTgt[s] - aCur[s]) / H : 0.0f;
        pF0[k] = fCur[s]; pF1[k] = freshFrame ? fTgt[s] : fCur[s];
        const float ph = std::atan2 (im[s], re[s]);
        float dw = 0.0f;
        if (freshFrame && hasTgt[s])   // land exactly on the partial's own phase at the end of this hop
        {
            const float phEnd = ph + kPi * (pF0[k] + pF1[k]) * invSr * H;
            dw = wrapPi (tgtPh[s] - phEnd) / H;
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
            for (int k = 0; k < m; ++k)
            {
                const float g = 1.5f - 0.5f * (RE[k] * RE[k] + IM[k] * IM[k]);
                RE[k] *= g; IM[k] *= g;
            }
        }
        else
        {
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
        if (freshFrame) { aCur[s] = aTgt[s]; fCur[s] = fTgt[s]; }
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
    const float tr = juce::jlimit (0.0f, 1.0f, p[Transient]) * (p[Inf] >= 0.999f ? 0.0f : 1.0f);
    if (onsetNow && tr > 0.001f)
    {
        const int pre = hop / 4, startK = 3 * hop - pre, hold = 4 * hop, decay = (int) (0.035 * sr);
        const int end = juce::jmin ((int) outRing.size() - 1, hold + decay);
        for (int k = startK; k < end; ++k)
        {
            const float e = k < 3 * hop ? (float) (k - startK) / (float) pre : (k < hold ? 1.0f : std::exp (-3.0f * (float) (k - hold) / (float) decay));
            float& g = gainRing[(size_t) ((ringOut + k) & mask)];
            g = juce::jmax (g, tr * e);
        }
    }
}

// ================================================================================================ the morph
void Morph::prepare (double sampleRate, int fftOrder)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    fftSize = 1 << fftOrder;
    hop = fftSize / 4;
    fft = std::make_unique<juce::dsp::FFT> (fftOrder);
    const int N = fftSize, half = N / 2;
    dryRing.assign ((size_t) N, 0.0f); wetRing.assign ((size_t) N, 0.0f);
    window.resize ((size_t) N);
    for (int i = 0; i < N; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (kTwoPi * (float) i / (float) N);
    for (auto* v : { &D, &W, &cep }) v->assign ((size_t) N * 2, 0.0f);
    for (auto* v : { &lm, &phs, &env }) v->assign ((size_t) half + 1, 0.0f);
    outRing.assign ((size_t) N * 2, 0.0f);
    reset();
}

void Morph::reset()
{
    std::fill (dryRing.begin(), dryRing.end(), 0.0f);
    std::fill (wetRing.begin(), wetRing.end(), 0.0f);
    std::fill (outRing.begin(), outRing.end(), 0.0f);
    pos = count = outPos = 0;
    f0Phase = 0.0;
}

void Morph::process (const float* dry, const float* wet, float* out, int n, const Params& p)
{
    if (fft == nullptr) return;
    const int mask = (int) outRing.size() - 1;
    for (int i = 0; i < n; ++i)
    {
        dryRing[(size_t) pos] = dry[i]; wetRing[(size_t) pos] = wet[i];
        if (++pos == fftSize) pos = 0;
        out[i] = outRing[(size_t) outPos];
        outRing[(size_t) outPos] = 0.0f;
        outPos = (outPos + 1) & mask;
        if (++count == hop) { count = 0; frame (p); }
    }
}

void Morph::frame (const Params& p)
{
    const int N = fftSize, half = N / 2, mask = (int) outRing.size() - 1;
    for (int j = 0; j < N; ++j)
    {
        const size_t src = (size_t) ((pos + j) % N), dst = (size_t) ((j + half) % N);
        D[dst] = dryRing[src] * window[(size_t) j];
        W[dst] = wetRing[src] * window[(size_t) j];
    }
    std::fill (D.begin() + N, D.end(), 0.0f); std::fill (W.begin() + N, W.end(), 0.0f);
    fft->performRealOnlyForwardTransform (D.data(), true);
    fft->performRealOnlyForwardTransform (W.data(), true);
    const float x = juce::jlimit (0.0f, 1.0f, p[PhaseX]), y = juce::jlimit (0.0f, 1.0f, p[CepstrumY]);
    const float shift = p[Shift], locd = juce::jlimit (0.0f, 1.0f, p[Locd]);
    float* S = W.data();   // what goes out
    const bool plain = std::abs (shift) < 0.01f && locd < 0.001f;
    if (! (plain && x >= 0.999f && y >= 0.999f))
    {
        if (plain && x <= 0.001f && y <= 0.001f) S = D.data();   // (all your sound)
        else
        {
            // the morph: tone (log magnitude = the cepstrum, all of it) and phase, each blended on its own
            for (int k = 0; k <= half; ++k)
            {
                const float dr = D[(size_t) (2 * k)], di = D[(size_t) (2 * k + 1)], wr = W[(size_t) (2 * k)], wi = W[(size_t) (2 * k + 1)];
                const float lD = 0.5f * std::log (dr * dr + di * di + 1e-24f), lW = 0.5f * std::log (wr * wr + wi * wi + 1e-24f);
                lm[(size_t) k] = lD + y * (lW - lD);
                const float pD = std::atan2 (di, dr), pW = std::atan2 (wi, wr);
                phs[(size_t) k] = pD + x * wrapPi (pW - pD);
            }
            if (std::abs (shift) >= 0.01f)
            {
                // SHIFT: the cepstral envelope (the formants) moved, the fine detail (the notes) left where it is
                std::fill (cep.begin(), cep.end(), 0.0f);
                for (int k = 0; k <= half; ++k) cep[(size_t) (2 * k)] = lm[(size_t) k];
                fft->performRealOnlyInverseTransform (cep.data());
                const int lifter = juce::jlimit (8, N / 4, juce::roundToInt (sr / 900.0));
                for (int q = lifter + 1; q < N - lifter; ++q) cep[(size_t) q] = 0.0f;
                std::fill (cep.begin() + N, cep.end(), 0.0f);
                fft->performRealOnlyForwardTransform (cep.data(), true);
                for (int k = 0; k <= half; ++k) env[(size_t) k] = cep[(size_t) (2 * k)];
                const float r = std::exp2 (shift / 12.0f);
                for (int k = 0; k <= half; ++k)
                {
                    const float src = juce::jlimit (0.0f, (float) half, (float) k / r);
                    const int k0 = juce::jmin (half - 1, (int) src); const float fr = src - (float) k0;
                    const float moved = env[(size_t) k0] + (env[(size_t) k0 + 1] - env[(size_t) k0]) * fr;
                    cep[(size_t) (2 * k + 1)] = moved;   // (scratch: the moved envelope)
                }
                for (int k = 0; k <= half; ++k) lm[(size_t) k] += cep[(size_t) (2 * k + 1)] - env[(size_t) k];
            }
            if (locd >= 0.001f)
            {
                // LOCD: the note's pitch (autocorrelation of your sound), then every bin's phase pulled to a sawtooth's
                std::fill (cep.begin(), cep.end(), 0.0f);
                for (int k = 0; k <= half; ++k) { const float dr = D[(size_t) (2 * k)], di = D[(size_t) (2 * k + 1)]; cep[(size_t) (2 * k)] = dr * dr + di * di; }
                fft->performRealOnlyInverseTransform (cep.data());
                const float r0 = cep[0];
                const int lo = juce::jmax (2, (int) (sr / 1000.0)), hi = juce::jmin (N / 2 - 2, (int) (sr / 40.0));
                int best = -1; float bv = 0.0f;
                for (int l = lo; l <= hi; ++l) if (cep[(size_t) l] > bv && cep[(size_t) l] >= cep[(size_t) l - 1] && cep[(size_t) l] >= cep[(size_t) l + 1]) { bv = cep[(size_t) l]; best = l; }
                if (best > 0 && r0 > 1e-12f && bv / r0 > 0.3f)
                {
                    const float a = cep[(size_t) best - 1], b = cep[(size_t) best], c = cep[(size_t) best + 1];
                    const float den = a - 2.0f * b + c;
                    const float lag = (float) best + (std::abs (den) > 1e-12f ? juce::jlimit (-0.5f, 0.5f, 0.5f * (a - c) / den) : 0.0f);
                    const double f0 = sr / lag;
                    f0Phase = std::fmod (f0Phase + 2.0 * juce::MathConstants<double>::pi * f0 * hop / sr, 2.0 * juce::MathConstants<double>::pi);
                    const float binHz = (float) sr / (float) N;
                    for (int k = 1; k <= half; ++k)
                    {
                        const int h = juce::jmax (1, juce::roundToInt ((float) k * binHz / (float) f0));
                        const float target = (float) std::fmod ((double) h * f0Phase, 2.0 * juce::MathConstants<double>::pi) - kPi * 0.5f;
                        phs[(size_t) k] += locd * wrapPi (target - phs[(size_t) k]);
                    }
                }
            }
            for (int k = 0; k <= half; ++k)
            {
                const float m = std::exp (lm[(size_t) k]);
                D[(size_t) (2 * k)] = m * std::cos (phs[(size_t) k]);
                D[(size_t) (2 * k + 1)] = m * std::sin (phs[(size_t) k]);
            }
            S = D.data();
        }
    }
    fft->performRealOnlyInverseTransform (S);
    const float norm = 1.0f / 1.5f;   // (Hann in, Hann out, at 75% overlap)
    for (int j = 0; j < N; ++j)
        outRing[(size_t) ((outPos + j) & mask)] += S[(size_t) ((j + half) % N)] * window[(size_t) j] * norm;
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
    const int order = mid.fftSize == 4096 ? 12 : 11;
    morphM.prepare (sampleRate, order);
    morphS.prepare (sampleRate, order);
    const size_t b = (size_t) juce::jmax (64, maxBlock);
    for (auto* v : { &inM, &inS, &wetM, &wetS, &dryM, &dryS }) v->assign (b, 0.0f);
    delayM.assign ((size_t) mid.latency(), 0.0f);
    delayS.assign ((size_t) mid.latency(), 0.0f);
    limRelease = (float) std::exp (-1.0 / (0.08 * sampleRate));
    isPrepared = true;
    reset();
}

void Engine::reset()
{
    mid.reset(); side.reset(); morphM.reset(); morphS.reset();
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
    if (tableNow.load() != ti) { ti = tableNow.load(); tableInUse.store (ti); }
    const Wavetable* table = (*tables)[(size_t) ti].sine ? nullptr : &(*tables)[(size_t) ti];
    const int oscM = juce::jlimit (0, 512, juce::roundToInt (p[MidOscs]));
    const int oscS = juce::jlimit (0, 512, juce::roundToInt (p[p[Link] >= 0.5f ? MidOscs : SideOscs]));
    const double bpm = tempo.load();
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
            // your sound, delayed to where the rebuilt one comes out (the morph needs them lined up)
            dryM[(size_t) i] = delayM[(size_t) delayPos]; dryS[(size_t) i] = delayS[(size_t) delayPos];
            delayM[(size_t) delayPos] = inM[(size_t) i]; delayS[(size_t) delayPos] = inS[(size_t) i];
            if (++delayPos == L) delayPos = 0;
        }
        const float ratio = eM + eS > 1e-12 ? (float) std::sqrt (eS / (eM + eS)) : 0.0f;
        sideLevel.store (juce::jmax (ratio, sideLevel.load (std::memory_order_relaxed) * 0.995f), std::memory_order_relaxed);
        mid.process (inM.data(), wetM.data(), len, p, table, oscM, bpm);
        side.process (inS.data(), wetS.data(), len, p, table, oscS, bpm);
        if (oscM == 0) std::copy (dryM.begin(), dryM.begin() + len, wetM.begin());   // (no oscillators: left as it was)
        if (oscS == 0) std::copy (dryS.begin(), dryS.begin() + len, wetS.begin());
        morphM.process (dryM.data(), wetM.data(), wetM.data(), len, p);
        morphS.process (dryS.data(), wetS.data(), wetS.data(), len, p);
        int sp = scopePos.load (std::memory_order_relaxed);
        for (int i = 0; i < len; ++i)
        {
            gainNow += (gainWant - gainNow) * 0.002f;
            const float M = wetM[(size_t) i] * gainNow, Sd = wetS[(size_t) i] * gainNow;
            const float pk = juce::jmax (std::abs (M + Sd), std::abs (M - Sd));
            limEnv = juce::jmax (pk, limEnv * limRelease);
            const float lg = limEnv > 0.977f ? 0.977f / limEnv : 1.0f;
            l[i] = (M + Sd) * lg; r[i] = (M - Sd) * lg;
            scope[(size_t) sp].store (M, std::memory_order_relaxed);
            if (++sp == kScope) sp = 0;
        }
        scopePos.store (sp, std::memory_order_relaxed);
    }
}

juce::AudioBuffer<float> renderOffline (const juce::AudioBuffer<float>& in, double sampleRate, const Params& p, const std::vector<float>& table, const std::atomic<bool>* cancel, double bpm)
{
    auto e = std::make_unique<Engine>();
    constexpr int block = 4096;
    e->prepare (sampleRate, block);
    e->setTable (table);
    e->setTempo (bpm);
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
    int end = total - lat;
    const float floor = juce::jmax (1e-5f, buf.getMagnitude (0, total) * 0.0005f);
    while (end > len && juce::jmax (std::abs (buf.getSample (0, lat + end - 1)), std::abs (buf.getSample (1, lat + end - 1))) < floor) --end;
    end = juce::jmin (total - lat, end + (int) (0.05 * sampleRate));
    juce::AudioBuffer<float> out (2, juce::jmax (1, end));
    for (int c = 0; c < 2; ++c) out.copyFrom (c, 0, buf, c, lat, out.getNumSamples());
    return out;
}
}
