#pragma once
// GHOSTPRINT (Ghost Box): drag in a short audio reference and take its GESTURE, never its sound. Our own analysis (no third-party models):
//   tempo        a user BPM, or BARS (the file is that many bars), or detected: the autocorrelation of an onset / rise novelty curve
//                (70 .. 180 BPM, a soft preference around 120), snapped to the BPM that makes the file a whole number of bars when that is
//                within 4 % (loops are usually cut to the bar)
//   envelope     RMS per 1/16 note
//   accents      spectral-flux onsets (log-magnitude rise over a 2048-point frame every 5 ms, median + 3 x MAD threshold, local maxima at
//                least 50 ms apart), quantised to the nearest 1/16 step
//   brightness   the spectral centroid (log frequency) per step, energy-weighted
//   stereo       the side / (mid + side) energy ratio per step (width) and the L / R balance per step (pan), where it is measurable
//   loop length  1, 2 or 4 bars: the shortest lag whose correlation of the envelope (or, when it is flat, of the curves that move)
//                with itself is >= 0.85;
//                every repetition in the file is then averaged into one loop
// The result is an editable pattern of GHOST MOTION timeline lanes (FxBoxTimeline.h): CURVE lanes (one point per 1/16) for loops of
// 1 or 2 bars, 64-STEP lanes for 4 bars, events for accents. INTENSITY = the lane depth, SMOOTH = a circular moving average,
// INVERT flips the curves, RHYTHM-ONLY turns the volume lane into a gate that opens on the accents. APPLY writes the lanes (one UNDO step).
// Analysis runs off the audio thread (the processor's worker); applying is message-thread work like any timeline edit.
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include "FxBoxTimeline.h"
#include "BendLane.h"
#include <atomic>
#include <memory>
#include <vector>

namespace boxprint {

constexpr double kMaxSecs = 60.0, kMinSecs = 0.25;
enum Curve { cEnv, cAccent, cBright, cStereo, kCurves };
inline const char* curveName (int c) { static const char* n[] { "ENVELOPE", "ACCENTS", "BRIGHTNESS", "STEREO" }; return n[juce::jlimit (0, 3, c)]; }
// the destinations each curve offers (index 0 = OFF); ids are rack settings a timeline lane can move
struct Dest { const char* name; const char* id; const char* unitOn; };
inline const std::vector<Dest>& dests (int curve)
{
    static const std::vector<Dest> d[kCurves] {
        { { "OFF", "", "" }, { "VOLUME", "fx_print_vol", "" }, { "FILTER CUTOFF", "fx_filter_cutoff", "fx_filter_on" }, { "SPACE DISTANCE", "fx_gspace_y", "fx_gspace_on" } },
        { { "OFF", "", "" }, { "ECHO THROWS", "fx_echo_mix", "fx_echo_on" }, { "REVERB THROWS", "fx_space_mix", "fx_space_on" }, { "FILTER CUTOFF", "fx_filter_cutoff", "fx_filter_on" } },
        { { "OFF", "", "" }, { "TONE", "fx_print_tone", "" }, { "FILTER CUTOFF", "fx_filter_cutoff", "fx_filter_on" } },
        { { "OFF", "", "" }, { "WIDTH", "fx_print_width", "" }, { "SPACE PAN", "fx_gspace_x", "fx_gspace_on" } } };
    return d[juce::jlimit (0, kCurves - 1, curve)];
}
struct Options { double bpm = 0.0; int bars = 0; double beatsPerBar = 4.0; };   // bpm 0 = detect, bars 0 = AUTO
struct Analysis
{
    bool ok = false; juce::String message, source;
    double sr = 48000, secs = 0, bpm = 120; bool bpmDetected = false, truncated = false; int fileSteps = 0, stepsPerBar = 16;
    int loopBars = 1, steps = 16; float loopCorr = 0.0f;
    std::vector<float> env, envDb, accent, bright, side, bal;   // one value per loop step: env 0..1 (1 = loudest), accent 0..1 (0 = none), bright / side 0..1, bal -1..1
    float envRangeDb = 0, brightOct = 0, sideRange = 0, balRange = 0; bool brightMoves = false, stereoMeasurable = false, panMoves = false;
    double brightLoHz = 0, brightHiHz = 0;
};
struct Settings { float intensity = 1.0f, smooth = 0.0f; bool rhythmOnly = false, invert = false; std::array<int, kCurves> dest { 1, 0, 1, 1 }; };

// ---------------------------------------------------------------- reading a reference (any thread but the audio thread)
inline bool readFile (const juce::File& f, juce::AudioBuffer<float>& out, double& sr, juce::String& why)
{
    if (! f.existsAsFile()) { why = "File not found: " + f.getFileName(); return false; }
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr || r->sampleRate <= 0.0 || r->lengthInSamples <= 0 || r->numChannels <= 0)
    { why = "Could not read " + f.getFileName() + ": not an audio file Ghost Box can open (WAV, AIFF, FLAC, MP3, OGG)"; return false; }
    sr = r->sampleRate;
    const juce::int64 n = std::min<juce::int64> (r->lengthInSamples, (juce::int64) (kMaxSecs * sr) + 1);
    const int ch = (int) std::min<unsigned int> (2u, r->numChannels);
    out.setSize (ch, (int) n, false, true, false);
    if (! r->read (&out, 0, (int) n, 0, true, ch > 1)) { why = "Could not read the audio of " + f.getFileName(); return false; }
    for (int c = 0; c < out.getNumChannels(); ++c)   // (a damaged file must never feed NaN into the analysis)
    {
        auto* p = out.getWritePointer (c);
        for (int i = 0; i < out.getNumSamples(); ++i) if (! std::isfinite (p[i])) p[i] = 0.0f;
    }
    if (r->lengthInSamples > n) why = "only the first " + juce::String ((int) kMaxSecs) + " s are used";
    return true;
}

// ---------------------------------------------------------------- small helpers
inline float pearson (const float* a, const float* b, int n)
{
    if (n < 4) return 0.0f;
    double ma = 0, mb = 0; for (int i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; } ma /= n; mb /= n;
    double c = 0, va = 0, vb = 0; for (int i = 0; i < n; ++i) { const double x = a[i] - ma, y = b[i] - mb; c += x * y; va += x * x; vb += y * y; }
    return va > 1.0e-18 && vb > 1.0e-18 ? (float) (c / std::sqrt (va * vb)) : 0.0f;
}
inline std::vector<float> circularSmooth (const std::vector<float>& v, int radius)
{
    if (radius <= 0 || v.size() < 3) return v;
    const int n = (int) v.size(); std::vector<float> o ((size_t) n, 0.0f);
    for (int i = 0; i < n; ++i)
    {   // triangular weights
        double s = 0, w = 0;
        for (int k = -radius; k <= radius; ++k) { const double wk = radius + 1 - std::abs (k); s += wk * v[(size_t) (((i + k) % n + n) % n)]; w += wk; }
        o[(size_t) i] = (float) (s / w);
    }
    return o;
}
inline float medianOf (std::vector<float> v) { if (v.empty()) return 0.0f; std::nth_element (v.begin(), v.begin() + (long) v.size() / 2, v.end()); return v[v.size() / 2]; }

// ---------------------------------------------------------------- the analysis (pure; any thread; 'cancel' may stop it early)
inline Analysis analyse (const juce::AudioBuffer<float>& in, double sr, const Options& opt, const std::atomic<bool>* cancel = nullptr)
{
    Analysis A; A.sr = sr;
    const int N = in.getNumSamples(), ch = in.getNumChannels();
    if (ch <= 0 || N <= 0 || sr <= 0.0) { A.message = "Empty reference"; return A; }
    const int n0 = std::min (N, (int) (kMaxSecs * sr)); A.truncated = n0 < N;
    A.secs = n0 / sr;
    if (A.secs < kMinSecs) { A.message = "The reference is too short (at least a quarter of a second)"; return A; }
    const float* Lp = in.getReadPointer (0); const float* Rp = in.getReadPointer (ch > 1 ? 1 : 0);
    int n = n0;
    float pk = 0; for (int i = 0; i < n; ++i) pk = std::max ({ pk, std::abs (Lp[i]), std::abs (Rp[i]) });
    if (! (pk > 1.0e-4f)) { A.message = "The reference is silent"; return A; }
    {   // a coder's start-up silence (MP3: tens of ms) is dropped so the grid starts on the first sound; a longer rest stays (it is music)
        int first = 0; while (first < n && std::abs (Lp[first]) < pk * 0.01f && std::abs (Rp[first]) < pk * 0.01f) ++first;   // (-40 dB: a coder's pre-echo is not the first sound)
        if (first > 0 && first < (int) (0.06 * sr)) { Lp += first; Rp += first; n -= first; A.secs = n / sr; }
    }
    // ---- frames: a hop every 5 ms; per hop the energies (mono, mid, side, L, R) and, from a Hann-windowed FFT centred on the hop, the
    //      spectral flux (positive log-magnitude rise) and the centroid
    const int hop = std::max (16, (int) std::lround (sr * 0.005));
    int order = 9; while ((1 << order) < (int) (sr * 0.04) && order < 14) ++order;
    const int F = 1 << order, bins = F / 2;
    const int H = n / hop + 1;
    std::vector<float> eM ((size_t) H), eS ((size_t) H), eL ((size_t) H), eR ((size_t) H), flux ((size_t) H), cen ((size_t) H), fe ((size_t) H);
    juce::dsp::FFT fft (order); std::vector<float> buf ((size_t) F * 2), win ((size_t) F), prevMag ((size_t) bins, 0.0f);
    for (int i = 0; i < F; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) F);
    const int kLo = std::max (1, (int) (40.0 * F / sr)), kHi = std::min (bins - 1, (int) (16000.0 * F / sr));
    for (int h = 0; h < H; ++h)
    {
        if (cancel != nullptr && (h & 255) == 0 && cancel->load()) { A.message = "cancelled"; return A; }
        const int a = h * hop, b = std::min (n, a + hop);
        double m = 0, s = 0, l = 0, r = 0;
        for (int i = a; i < b; ++i) { const double x = Lp[i], y = Rp[i]; m += 0.25 * (x + y) * (x + y); s += 0.25 * (x - y) * (x - y); l += x * x; r += y * y; }
        const double cnt = std::max (1, b - a);
        eM[(size_t) h] = (float) (m / cnt); eS[(size_t) h] = (float) (s / cnt); eL[(size_t) h] = (float) (l / cnt); eR[(size_t) h] = (float) (r / cnt);
        const int c0 = a + hop / 2 - F / 2;
        for (int i = 0; i < F; ++i) { const int q = c0 + i; buf[(size_t) i] = q >= 0 && q < n ? 0.5f * (Lp[q] + Rp[q]) * win[(size_t) i] : 0.0f; }
        std::fill (buf.begin() + F, buf.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (buf.data(), true);
        double fl = 0, num = 0, den = 0;
        for (int k = kLo; k <= kHi; ++k)
        {
            const float mag = buf[(size_t) k] * (4.0f / (float) F);   // (about the sine amplitude)
            const float lm = std::log (1.0f + 100.0f * mag), lp = std::log (1.0f + 100.0f * prevMag[(size_t) k]);
            if (lm > lp && h > 0) fl += lm - lp;   // (the first frame has nothing before it)
            prevMag[(size_t) k] = mag;
            const double p = (double) mag * mag; num += p * (k * sr / F); den += p;
        }
        flux[(size_t) h] = (float) fl; cen[(size_t) h] = den > 1.0e-20 ? (float) (num / den) : 0.0f; fe[(size_t) h] = (float) den;
    }
    const double hopSec = (double) hop / sr;
    // ---- tempo
    A.stepsPerBar = (int) std::lround (4.0 * opt.beatsPerBar);
    if (opt.bpm > 0.0) A.bpm = juce::jlimit (30.0, 300.0, opt.bpm);
    else if (opt.bars > 0) A.bpm = juce::jlimit (30.0, 300.0, opt.bars * opt.beatsPerBar * 60.0 / A.secs);
    else
    {   // novelty = normalised flux + the rise of the (dB) envelope; its autocorrelation over 70 .. 180 BPM with a soft preference for 120
        // two z-normalised curves: the spectral flux and the envelope (dB); each one's autocorrelation coefficient at a lag
        std::vector<float> zf (flux), ze ((size_t) H);
        for (int h = 0; h < H; ++h) ze[(size_t) h] = 10.0f * std::log10 (eM[(size_t) h] + 1.0e-9f);
        for (auto* v : { &zf, &ze })
        {
            double m = 0, q = 0; for (float x : *v) m += x; m /= H; for (float x : *v) q += (x - m) * (x - m); const double sd = std::sqrt (q / H) + 1.0e-12;
            for (auto& x : *v) x = (float) ((x - m) / sd);
        }
        auto acf1 = [&] (const std::vector<float>& v, double lag) { const int L = (int) lag; const double f = lag - L; double c = 0; int cnt = 0; for (int h = 0; h + L + 1 < H; ++h, ++cnt) c += v[(size_t) h] * ((1 - f) * v[(size_t) (h + L)] + f * v[(size_t) (h + L + 1)]); return cnt > 0 ? c / cnt : 0.0; };
        auto acf = [&] (double lag) { return acf1 (zf, lag) + acf1 (ze, lag); };
        double best = 120, bestS = -1.0e30;
        for (double bpm = 70.0; bpm <= 180.0; bpm += 0.25)
        {
            const double lag = 60.0 / bpm / hopSec; if (lag * 2 >= H) continue;
            const double pr = std::exp (-0.5 * std::pow (std::log2 (bpm / 120.0) / 0.6, 2.0));
            const double sc = (acf (lag) + 0.5 * acf (lag * 2.0) + 0.25 * acf (lag * 0.5)) * pr;
            if (sc > bestS) { bestS = sc; best = bpm; }
        }
        A.bpm = best; A.bpmDetected = true;
        // a loop is usually cut to the bar: a whole number of bars within 4 % wins (also at double / half the tempo)
        double snap = 0, snapErr = 1.0;
        for (int bars : { 1, 2, 4, 8, 16, 32 })
        {
            const double b = bars * opt.beatsPerBar * 60.0 / A.secs;
            for (double t : { best, best * 2.0, best * 0.5 })
            {
                const double err = std::abs (b / t - 1.0) + (t != best ? 0.03 : 0.0);   // (the detected tempo itself first)
                if (b >= 60.0 && b <= 200.0 && std::abs (b / t - 1.0) < 0.04 && err < snapErr) { snapErr = err; snap = b; }
            }
        }
        if (snap > 0)
        {   // the file is a whole number of bars: allow up to 60 ms of coder padding at the end (MP3), preferring a whole BPM
            const double bars = std::round (snap * A.secs / (60.0 * opt.beatsPerBar));
            double bestB = snap, bestE = 1.0e9;
            for (int padMs = 0; padMs <= 60; ++padMs)
            {
                const double b = bars * opt.beatsPerBar * 60.0 / (A.secs - padMs * 0.001), e = std::abs (b - std::round (b)) + 0.002 * padMs;
                if (e < bestE) { bestE = e; bestB = b; }
            }
            A.bpm = bestB;
        }
        if (std::abs (A.bpm - std::round (A.bpm)) < 0.35) A.bpm = std::round (A.bpm);   // (loops are almost always made at a whole BPM)
    }
    // ---- per 1/16 step over the whole file
    const double stepSec = 60.0 / A.bpm / 4.0;
    A.fileSteps = std::max (1, (int) std::floor (A.secs / stepSec + 0.25));
    const int S = A.fileSteps;
    std::vector<double> sM ((size_t) S, 0), sS ((size_t) S, 0), sL ((size_t) S, 0), sR ((size_t) S, 0), sCn ((size_t) S, 0), sCd ((size_t) S, 0); std::vector<int> sN ((size_t) S, 0);
    for (int h = 0; h < H; ++h)
    {
        const double t = (h + 0.5) * hopSec; const int s = (int) (t / stepSec); if (s < 0 || s >= S) continue;
        sM[(size_t) s] += eM[(size_t) h]; sS[(size_t) s] += eS[(size_t) h]; sL[(size_t) s] += eL[(size_t) h]; sR[(size_t) s] += eR[(size_t) h]; ++sN[(size_t) s];
        if (cen[(size_t) h] > 0) { sCn[(size_t) s] += fe[(size_t) h] * std::log2 (cen[(size_t) h]); sCd[(size_t) s] += fe[(size_t) h]; }
    }
    std::vector<float> fEnv ((size_t) S), fBr ((size_t) S), fSide ((size_t) S), fBal ((size_t) S), fAcc ((size_t) S, 0.0f);
    for (int s = 0; s < S; ++s)
    {
        const double c = std::max (1, sN[(size_t) s]);
        fEnv[(size_t) s] = (float) std::sqrt ((sM[(size_t) s] + sS[(size_t) s]) / c);
        fBr[(size_t) s] = sCd[(size_t) s] > 0 ? (float) (sCn[(size_t) s] / sCd[(size_t) s]) : 0.0f;
        fSide[(size_t) s] = (float) (sS[(size_t) s] / (sM[(size_t) s] + sS[(size_t) s] + 1.0e-20));
        fBal[(size_t) s] = (float) ((sR[(size_t) s] - sL[(size_t) s]) / (sR[(size_t) s] + sL[(size_t) s] + 1.0e-20));
    }
    {   // steps without energy take their brightness from the mean (so silence never reads as "dark")
        double sum = 0, w = 0; for (int s = 0; s < S; ++s) if (sCd[(size_t) s] > 0) { sum += fBr[(size_t) s] * sCd[(size_t) s]; w += sCd[(size_t) s]; }
        const float mean = w > 0 ? (float) (sum / w) : 10.0f; float eMax = 0; for (float v : fEnv) eMax = std::max (eMax, v);
        for (int s = 0; s < S; ++s) if (sCd[(size_t) s] <= 0 || fEnv[(size_t) s] < eMax * 0.003f) { fBr[(size_t) s] = mean; fSide[(size_t) s] = 0.0f; fBal[(size_t) s] = 0.0f; }
    }
    // ---- accents: flux peaks over an adaptive threshold, at least 50 ms apart, quantised to the nearest step
    {
        const float med = medianOf (flux); std::vector<float> dev ((size_t) H); for (int h = 0; h < H; ++h) dev[(size_t) h] = std::abs (flux[(size_t) h] - med);
        float fmx = 0; for (float v : flux) fmx = std::max (fmx, v);
        const float thr = std::max (med + 3.0f * medianOf (dev) * 1.4826f, 0.12f * fmx);
        const int minGap = std::max (1, (int) (0.05 / hopSec)), w = 3;
        int last = -minGap, lastAt = -minGap;
        float eMax = 0; for (float v : eM) eMax = std::max (eMax, v);
        for (int h = 1; h + 1 < H; ++h)
        {
            const float v = flux[(size_t) h]; if (v < thr || h - last < minGap) continue;
            bool isMax = true; for (int k = std::max (0, h - w); k <= std::min (H - 1, h + w); ++k) if (flux[(size_t) k] > v) { isMax = false; break; }
            if (! isMax) continue;
            float after = 0; for (int k = h; k < std::min (H, h + 6); ++k) after = std::max (after, eM[(size_t) k]);
            if (after < eMax * 1.0e-5f) continue;   // (below -50 dB of the loudest: not a hit)
            // the hit itself: the hop near the flux peak where the level jumps most (at least 3 dB in 5 ms); a sound stopping has no jump
            int at = -1; float bestJump = 2.0f;
            for (int k = std::max (4, h - 4); k <= std::min (H - 4, h + 8); ++k)
            {   // 20 ms after against 20 ms before (a steady sound's own beating never doubles that)
                const float a4 = eM[(size_t) k] + eM[(size_t) k + 1] + eM[(size_t) k + 2] + eM[(size_t) k + 3], b4 = eM[(size_t) k - 1] + eM[(size_t) k - 2] + eM[(size_t) k - 3] + eM[(size_t) k - 4];
                const float j = a4 / (b4 + 4.0f * eMax * 1.0e-7f); if (j > bestJump) { bestJump = j; at = k; }
            }
            if (at < 0 || at - lastAt < minGap) continue;
            lastAt = at;
            last = h;
            const int s = (int) std::lround ((at * hopSec) / stepSec);   // (the jump's hop starts at at * hop)
            if (s >= 0 && s < S) fAcc[(size_t) s] = std::max (fAcc[(size_t) s], v / std::max (1.0e-9f, fmx));
        }
    }
    // ---- loop length: 1 / 2 / 4 bars, the shortest that repeats (the curves that move, z-normalised)
    float eMx = 0, eMn = 1.0e9f; for (float v : fEnv) { eMx = std::max (eMx, v); eMn = std::min (eMn, v); }
    auto range = [] (const std::vector<float>& v) { float a = 1.0e9f, b = -1.0e9f; for (float x : v) { a = std::min (a, x); b = std::max (b, x); } return b - a; };
    std::vector<const std::vector<float>*> moving;   // the envelope decides when it moves (the other curves are noisier); otherwise the ones that move
    if (20.0f * std::log10 ((eMx + 1.0e-9f) / (eMn + 1.0e-9f)) > 1.5f) moving.push_back (&fEnv);
    else
    {
        if (range (fBr) > 0.15f) moving.push_back (&fBr);
        if (range (fBal) > 0.1f) moving.push_back (&fBal);
        if (range (fSide) > 0.05f) moving.push_back (&fSide);
    }
    const int bar = A.stepsPerBar;
    int L = 0; float bestCorr = 0.0f;
    for (int bars : { 1, 2, 4 })
    {
        const int lag = bars * bar;
        if (S < 2 * lag || moving.empty()) continue;
        float c = 0; for (auto* f : moving) c += pearson (f->data(), f->data() + lag, S - lag);
        c /= (float) moving.size();
        if (c >= 0.85f) { L = lag; bestCorr = c; break; }
    }
    if (L == 0)
    {   // nothing repeats inside the file: the loop is the file (1 / 2 / 4 bars, the largest that fits; a short file is tiled)
        L = bar; for (int bars : { 2, 4 }) if (S >= bars * bar - bar / 4) L = bars * bar;
    }
    A.loopBars = L / bar; A.steps = L; A.loopCorr = bestCorr;
    // ---- fold every repetition into one loop
    A.env.assign ((size_t) L, 0.0f); A.envDb.assign ((size_t) L, -120.0f); A.accent.assign ((size_t) L, 0.0f); A.bright.assign ((size_t) L, 0.5f); A.side.assign ((size_t) L, 0.5f); A.bal.assign ((size_t) L, 0.0f);
    std::vector<float> br ((size_t) L), sd ((size_t) L), bl ((size_t) L);
    for (int j = 0; j < L; ++j)
    {
        double e = 0, wb = 0, b = 0, s2 = 0, b2 = 0, acc = 0; int reps = 0, hits = 0;
        for (int s = j; s < std::max (S, L); s += L)
        {
            const int q = s % S;   // (a file shorter than the loop is tiled)
            const double w = (double) fEnv[(size_t) q] * fEnv[(size_t) q] + 1.0e-12;
            e += w; b += w * fBr[(size_t) q]; s2 += w * fSide[(size_t) q]; b2 += w * fBal[(size_t) q]; wb += w;
            if (fAcc[(size_t) q] > 0) { acc += fAcc[(size_t) q]; ++hits; }
            ++reps;
            if (s >= S) break;
        }
        A.env[(size_t) j] = (float) std::sqrt (e / reps);
        br[(size_t) j] = (float) (b / wb); sd[(size_t) j] = (float) (s2 / wb); bl[(size_t) j] = (float) (b2 / wb);
        A.accent[(size_t) j] = 2 * hits >= reps ? (float) (acc / hits) : 0.0f;
    }
    float emax = 0, emin = 1.0e9f; for (float v : A.env) { emax = std::max (emax, v); emin = std::min (emin, v); }
    for (int j = 0; j < L; ++j) { A.envDb[(size_t) j] = 20.0f * std::log10 (A.env[(size_t) j] / std::max (1.0e-9f, emax) + 1.0e-9f); A.env[(size_t) j] /= std::max (1.0e-9f, emax); }
    A.envRangeDb = 20.0f * std::log10 ((emax + 1.0e-9f) / (emin + 1.0e-9f));
    {
        float lo = 1.0e9f, hi = -1.0e9f; for (float v : br) { lo = std::min (lo, v); hi = std::max (hi, v); }
        A.brightOct = hi - lo; A.brightMoves = A.brightOct > 0.15f; A.brightLoHz = std::exp2 (lo); A.brightHiHz = std::exp2 (hi);
        for (int j = 0; j < L; ++j) A.bright[(size_t) j] = A.brightMoves ? (br[(size_t) j] - lo) / (hi - lo) : 0.5f;
    }
    {
        float lo = 1.0e9f, hi = -1.0e9f, mean = 0; for (float v : sd) { lo = std::min (lo, v); hi = std::max (hi, v); mean += v; } mean /= (float) L;
        A.sideRange = hi - lo; A.stereoMeasurable = hi > 0.01f && A.sideRange > 0.05f;
        for (int j = 0; j < L; ++j) A.side[(size_t) j] = A.stereoMeasurable ? (sd[(size_t) j] - lo) / (hi - lo) : 0.5f;
        float blo = 1.0e9f, bhi = -1.0e9f; for (float v : bl) { blo = std::min (blo, v); bhi = std::max (bhi, v); }
        A.balRange = bhi - blo; A.panMoves = A.balRange > 0.1f;
        for (int j = 0; j < L; ++j) A.bal[(size_t) j] = bl[(size_t) j];
        juce::ignoreUnused (mean);
    }
    A.ok = true;
    juce::String m; m << juce::String (A.bpm, 1) << " BPM" << (A.bpmDetected ? " (detected)" : "") << ", loop " << A.loopBars << (A.loopBars == 1 ? " bar" : " bars")
                      << ", envelope " << juce::String (A.envRangeDb, 1) << " dB";
    int nAcc = 0; for (float v : A.accent) nAcc += v > 0 ? 1 : 0;
    m << ", " << nAcc << " accents";
    m << (A.brightMoves ? ", brightness moves " + juce::String (A.brightOct, 1) + " oct" : ", brightness steady");
    m << (A.stereoMeasurable || A.panMoves ? ", stereo moves" : ", no stereo movement");
    if (A.truncated) m << " (first " << (int) kMaxSecs << " s)";
    A.message = m;
    return A;
}

// ---------------------------------------------------------------- the editable pattern: timeline lanes
struct PLane
{
    int curve = cEnv; juce::String id, unitOn, name;
    bool steps = false;                  // STEPS (64) or CURVE (one point per step)
    std::vector<float> vals;             // -1 .. 1 per loop step (the lane base)
    float depth = 1.0f;
    std::vector<gpfx::TlEvent> events;   // (beats from the loop start)
};
struct Pattern { int loopBars = 1, steps = 16; double beatsPerBar = 4.0; std::vector<PLane> lanes; std::array<std::vector<float>, kCurves> preview; };

inline Pattern makePattern (const Analysis& A, const Settings& s)
{
    Pattern P; if (! A.ok) return P;
    P.loopBars = A.loopBars; P.steps = A.steps; P.beatsPerBar = A.stepsPerBar / 4.0;
    const int n = A.steps, rad = juce::roundToInt (juce::jlimit (0.0f, 1.0f, s.smooth) * 3.0f);
    const float I = juce::jlimit (0.0f, 1.0f, s.intensity);
    const double stepBeats = 0.25;
    auto inv = [&] (std::vector<float> v) { if (s.invert) for (auto& x : v) x = 1.0f - x; return v; };
    const auto env = inv (circularSmooth (A.env, rad)), bright = inv (circularSmooth (A.bright, rad)), side = inv (circularSmooth (A.side, rad));
    auto bal = circularSmooth (A.bal, rad); if (s.invert) for (auto& x : bal) x = -x;
    P.preview[cEnv] = env; P.preview[cBright] = bright; P.preview[cStereo] = A.stereoMeasurable ? side : (A.panMoves ? bal : side);
    P.preview[cAccent] = A.accent;
    std::vector<int> acc; for (int j = 0; j < n; ++j) if (A.accent[(size_t) j] > 0) acc.push_back (j);
    auto lane = [&] (int curve) -> PLane& {
        const auto& d = dests (curve)[(size_t) juce::jlimit (0, (int) dests (curve).size() - 1, s.dest[(size_t) curve])];
        for (auto& l : P.lanes) if (l.id == d.id) return l;   // two curves on one setting share its lane (base from the first, events from both)
        PLane l; l.curve = curve; l.id = d.id; l.unitOn = d.unitOn; l.name = juce::String (curveName (curve)) + " -> " + d.name; l.steps = n > bendlane::kMaxPoints; l.vals.assign ((size_t) n, 0.0f); l.depth = I;
        P.lanes.push_back (l); return P.lanes.back();
    };
    auto active = [&] (int curve) { return s.dest[(size_t) curve] > 0; };
    // ENVELOPE (or, RHYTHM-ONLY, a gate that opens on the accents: closed = 1 - INTENSITY)
    if (active (cEnv))
    {
        auto& l = lane (cEnv);
        if (! s.rhythmOnly) for (int j = 0; j < n; ++j) l.vals[(size_t) j] = -(1.0f - env[(size_t) j]);
        else
        {
            std::vector<int> open = acc;
            if (open.empty()) for (int j = 0; j < n; ++j) if (env[(size_t) j] > 0.5f) open.push_back (j);   // (no accents: the loud steps)
            for (int j = 0; j < n; ++j) l.vals[(size_t) j] = -1.0f;
            for (size_t k = 0; k < open.size(); ++k)
            {
                const int a = open[k], b = k + 1 < open.size() ? open[k + 1] : open[0] + n;
                const double len = std::min (2.0, (double) (b - a)) * stepBeats;
                l.events.push_back ({ a * stepBeats, len, s.invert ? -1.0f : 1.0f, false });
                if (s.invert) l.vals[(size_t) a] = 0.0f;
            }
            if (s.invert) for (auto& v : l.vals) v = 0.0f;
        }
    }
    // ACCENTS: one-step bumps (throws) on their own setting
    if (active (cAccent) && ! acc.empty())
    {
        auto& l = lane (cAccent);
        for (int a : acc) l.events.push_back ({ a * stepBeats, stepBeats, (s.invert ? -1.0f : 1.0f) * juce::jlimit (0.3f, 1.0f, A.accent[(size_t) a] * 1.5f), false });
    }
    // BRIGHTNESS: open at the brightest step
    if (active (cBright))
    {
        auto& l = lane (cBright);
        if (l.curve == cBright) for (int j = 0; j < n; ++j) l.vals[(size_t) j] = -(1.0f - bright[(size_t) j]);
    }
    // STEREO: WIDTH follows the side ratio (around 100 %), SPACE PAN follows the balance
    if (active (cStereo) && (A.stereoMeasurable || A.panMoves))
    {
        auto& l = lane (cStereo);
        const bool pan = juce::String (l.id) == "fx_gspace_x";
        if (l.curve == cStereo) for (int j = 0; j < n; ++j) l.vals[(size_t) j] = pan ? 0.5f * bal[(size_t) j] : side[(size_t) j] - 0.5f;
    }
    for (auto& l : P.lanes) if (l.events.size() > (size_t) gpfx::kTlEvents) l.events.resize ((size_t) gpfx::kTlEvents);
    return P;
}
// the lane's base as the timeline stores it
inline bendlane::Shape curveShape (const PLane& l)
{
    bendlane::Shape sh; sh.snap = 0; sh.grid = 2;
    const int n = (int) l.vals.size();
    for (int j = 0; j < n && j < bendlane::kMaxPoints; ++j) sh.add ((float) ((j + 0.5) / n), juce::jlimit (-1.0f, 1.0f, l.vals[(size_t) j]));
    if (n == 0) sh.add (0, 0);
    sh.sanitise();
    return sh;
}
inline int stepsIdxFor (int n) { return n <= 8 ? 0 : n <= 16 ? 1 : n <= 32 ? 2 : 3; }
// the value a lane holds at a loop phase (0..1), before DEPTH: what the timeline plays (tests, preview)
inline float laneValueAt (const PLane& l, double ph, double loopBeats)
{
    const int n = (int) l.vals.size(); if (n == 0) return 0.0f;
    float v;
    if (l.steps) v = gpfx::tlStepsAt (l.vals.data(), n, ph);
    else v = curveShape (l).valueAt (ph);
    v += gpfx::tlEventsAt (l.events.data(), (int) l.events.size(), ph * loopBeats, loopBeats, 0.0);
    return juce::jlimit (-1.0f, 1.0f, v);
}

} // namespace boxprint
