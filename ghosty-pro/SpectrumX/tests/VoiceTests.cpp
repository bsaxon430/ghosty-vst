// Ghosty Spectrum VOICE page tests. Every input is synthesised here (glottal pulses through formant resonators at known
// F1..F4, noise, silence); no recordings.
//   VoiceTests              -> engine + host tests (exit code = failures)
//   VoiceTests shots DIR    -> PNG screenshots of the page (light + dark)
//   VoiceTests demos DIR    -> WAV demos (a synth speaking vowels, a pitch-contoured phrase)
#include "VoiceEngine.h"   // Shared
#include "../VoiceHost.h"
#include "../VoicePage.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_dsp/juce_dsp.h>
#include <iostream>
#include "world/synthesis.h"
#include "WaveRegion.h"

using namespace voice;
static int fails = 0, passes = 0;
static void check(bool ok, const juce::String& what, const juce::String& detail = {}) {
    std::cout << (ok ? "  PASS  " : "  FAIL  ") << what << (detail.isNotEmpty() ? "  (" + detail + ")" : juce::String()) << std::endl;
    (ok ? passes : fails)++;
}
static constexpr double kPi = juce::MathConstants<double>::pi;
static double cents(double a, double b) { return 1200.0 * std::log2(std::max(1e-9, a) / std::max(1e-9, b)); }
static double pct(double a, double b) { return 100.0 * std::abs(a - b) / std::max(1e-9, b); }

// ---------------------------------------------------------------------------------------------------------------- signals
// two-pole resonator (unity gain at its centre)
struct Reso { double a1 = 0, a2 = 0, g = 1, y1 = 0, y2 = 0;
    void set(double f, double bw, double fs) { const double r = std::exp(-kPi * bw / fs), th = 2 * kPi * f / fs; a1 = -2 * r * std::cos(th); a2 = r * r; g = (1 - r) * std::sqrt(1 - 2 * r * std::cos(2 * th) + r * r); }
    double tick(double x) { const double y = g * x - a1 * y1 - a2 * y2; y2 = y1; y1 = y; return y; } };

// A sung / spoken vowel: Rosenberg glottal pulses at f0(t) (Hz, 0 = whisper/noise), cascade of 4 formant resonators
// (F given by formants(t)), lip radiation. jitter = tiny natural pitch wobble.
struct VoiceSpec { std::function<double(double)> f0; std::function<std::array<double, 4>(double)> F; double seconds = 1.5, breath = 0.01; };
static std::vector<float> synthVoice(const VoiceSpec& v, int fs, unsigned seed = 1) {
    const int n = (int) (v.seconds * fs); std::vector<float> x((size_t) n); juce::Random rnd((juce::int64) seed);
    std::array<Reso, 4> R; const double BW[4] = { 80, 100, 140, 180 };
    double phase = 0, prev = 0; float peak = 0;
    for (int i = 0; i < n; ++i) {
        const double t = i / (double) fs; const double f = v.f0(t);
        double src = 0;
        if (f > 0) { phase += f / fs; if (phase >= 1) phase -= 1;   // Rosenberg: open 40 % rising, 16 % falling, closed after
            const double op = 0.4, cl = 0.16; src = phase < op ? 0.5 * (1 - std::cos(kPi * phase / op)) : phase < op + cl ? std::cos(0.5 * kPi * (phase - op) / cl) : 0.0; src -= 0.3; }
        src += v.breath * (rnd.nextFloat() * 2 - 1) + (f > 0 ? 0.0 : 0.25 * (rnd.nextFloat() * 2 - 1));
        if ((i & 31) == 0) { const auto F = v.F(t); for (int k = 0; k < 4; ++k) R[(size_t) k].set(F[(size_t) k], BW[k], fs); }
        double y = src; for (auto& r : R) y = r.tick(y);
        const double out = y - 0.97 * prev; prev = y;
        const double env = std::min(1.0, std::min(t, v.seconds - t) * 30.0);
        x[(size_t) i] = (float) (out * env); peak = std::max(peak, std::abs(x[(size_t) i]));
    }
    for (auto& s : x) s *= 0.7f / std::max(1e-6f, peak);
    return x;
}
static std::array<double, 4> vowelF(int i) { const auto& V = vowelTable()[(size_t) i]; return { V.F[0], V.F[1], V.F[2], V.F[3] }; }
static std::vector<float> steadyVowel(int vowel, double f0, int fs, double secs = 1.5) {
    VoiceSpec v; v.seconds = secs; v.f0 = [f0](double t) { return f0 * (1.0 + 0.002 * std::sin(2 * kPi * 5.0 * t)); }; v.F = [vowel](double) { return vowelF(vowel); };
    return synthVoice(v, fs);
}
// a band-limited saw "synth pad" (the carrier for BORROW)
static std::vector<float> synthSaw(double f0, int fs, double secs) {
    std::vector<float> x((size_t) (secs * fs));
    for (size_t i = 0; i < x.size(); ++i) { const double t = i / (double) fs; double s = 0; for (int h = 1; h * f0 < fs * 0.45; ++h) s += std::sin(2 * kPi * f0 * h * t) / h; x[i] = (float) (0.3 * s * std::min(1.0, std::min(t, secs - t) * 30.0)); }
    return x;
}

// ---------------------------------------------------------------------------------------------------------------- measures
// independent f0: harmonic-sum search on a long Hann-windowed FFT around a guess (+-1.5 semitones), parabolic refinement
static double harmonicF0(const std::vector<float>& x, int fs, double guess, double t0, double t1) {
    const int s0 = (int) (t0 * fs), s1 = std::min((int) x.size(), (int) (t1 * fs)); const int len = s1 - s0; if (len < 1024) return 0;
    int order = 1; while ((1 << order) < len * 4) ++order; order = std::min(order, 19); const int M = 1 << order;
    juce::dsp::FFT fft(order); std::vector<float> b((size_t) M * 2, 0.0f);
    for (int i = 0; i < len && i < M; ++i) b[(size_t) i] = x[(size_t) (s0 + i)] * (float) (0.5 - 0.5 * std::cos(2 * kPi * (i + 0.5) / len));
    fft.performFrequencyOnlyForwardTransform(b.data(), true);
    auto magAt = [&](double hz) { const double k = hz * M / fs; const int i = (int) k; if (i < 1 || i >= M / 2 - 1) return 0.0; const double f = k - i; return (double) b[(size_t) i] * (1 - f) + b[(size_t) i + 1] * f; };
    auto score = [&](double f) { double s = 0; for (int h = 1; h <= 8 && h * f < fs * 0.45; ++h) s += std::log(1e-9 + magAt(h * f)); return s; };
    double best = guess, bs = -1e300;
    for (double c = -150; c <= 150; c += 0.5) { const double f = guess * std::exp2(c / 1200.0); const double s = score(f); if (s > bs) { bs = s; best = f; } }
    // refine on the strongest harmonic below 1.5 kHz (a peak, parabolic in log-magnitude)
    int hb = 1; double hm = 0; for (int h = 1; h * best < 1500; ++h) { const double m = magAt(h * best); if (m > hm) { hm = m; hb = h; } }
    const int k = (int) std::lround(hb * best * M / fs); int kk = k; for (int d = -3; d <= 3; ++d) if (b[(size_t) (k + d)] > b[(size_t) kk]) kk = k + d;
    const double a = std::log(1e-12 + b[(size_t) kk - 1]), c = std::log(1e-12 + b[(size_t) kk]), d = std::log(1e-12 + b[(size_t) kk + 1]);
    const double off = 0.5 * (a - d) / (a - 2 * c + d);
    return (kk + off) * fs / (double) M / hb;
}
// envelope formants of a steady region: WORLD analysis of y, median of the per-frame F1/F2 over voiced frames in [t0, t1]
static std::pair<double, double> envFormants(const std::vector<float>& y, int fs, double t0, double t1) {
    Analysis a; AnalysisOptions o; analyse(y.data(), (int) y.size(), fs, o, a);
    std::vector<double> f1, f2; for (int i = a.frameAt(t0); i <= a.frameAt(t1); ++i) if (a.voiced(i)) { f1.push_back(a.formants[(size_t) i][0].hz); f2.push_back(a.formants[(size_t) i][1].hz); }
    if (f1.empty()) return { 0, 0 };
    std::sort(f1.begin(), f1.end()); std::sort(f2.begin(), f2.end()); return { f1[f1.size() / 2], f2[f2.size() / 2] };
}
// the same, but per frame the formant nearest each reference (so a formant that merges / vanishes shows as an error, not an index shift)
static std::pair<double, double> envNear(const std::vector<float>& y, int fs, double t0, double t1, double r1, double r2) {
    Analysis a; AnalysisOptions o; analyse(y.data(), (int) y.size(), fs, o, a);
    std::vector<double> f1, f2;
    auto nearest = [](const Formants& F, double r) { double best = 0, bd = 1e9; for (auto& f : F) if (f.hz > 0 && std::abs(f.hz - r) < bd) { bd = std::abs(f.hz - r); best = f.hz; } return best; };
    for (int i = a.frameAt(t0); i <= a.frameAt(t1); ++i) if (a.voiced(i)) { f1.push_back(nearest(a.formants[(size_t) i], r1)); f2.push_back(nearest(a.formants[(size_t) i], r2)); }
    if (f1.empty()) return { 0, 0 };
    std::sort(f1.begin(), f1.end()); std::sort(f2.begin(), f2.end()); return { f1[f1.size() / 2], f2[f2.size() / 2] };
}
static std::pair<double, double> waveFormants(const std::vector<float>& y, int fs, double t0, double t1) {
    std::vector<double> f1, f2;
    for (double t = t0; t + 0.04 <= t1; t += 0.02) { const auto F = formantsOfWave(y.data() + (size_t) (t * fs), (int) (0.04 * fs), fs); if (F[0].hz > 0 && F[1].hz > 0) { f1.push_back(F[0].hz); f2.push_back(F[1].hz); } }
    if (f1.empty()) return { 0, 0 };
    std::sort(f1.begin(), f1.end()); std::sort(f2.begin(), f2.end()); return { f1[f1.size() / 2], f2[f2.size() / 2] };
}
// rms dB difference between the output's harmonic peaks (k * f0 below 4 kHz) and the input analysis' mean envelope
// at the same frequencies (after removing the overall level offset)
static double harmonicLevelError(const std::vector<float>& y, int fs, double f0, const Analysis& in, double t0, double t1) {
    const int s0 = (int) (t0 * fs), len = (int) ((t1 - t0) * fs); int order = 1; while ((1 << order) < len * 2) ++order; const int M = 1 << order;
    juce::dsp::FFT fft(order); std::vector<float> b((size_t) M * 2, 0.0f);
    for (int i = 0; i < len; ++i) b[(size_t) i] = y[(size_t) (s0 + i)] * (float) (0.5 - 0.5 * std::cos(2 * kPi * (i + 0.5) / len));
    fft.performFrequencyOnlyForwardTransform(b.data(), true);
    std::vector<double> env((size_t) in.bins(), 0.0); int cnt = 0;
    for (int i = in.frameAt(t0); i <= in.frameAt(t1); ++i) if (in.voiced(i)) { { const auto row = in.specFrame(i); for (int k = 0; k < in.bins(); ++k) env[(size_t) k] += row[(size_t) k]; } ++cnt; }
    if (cnt == 0) return 99;
    std::vector<double> d, lev;
    for (int h = 1; h * f0 < 4000; ++h) {
        const int k = (int) std::lround(h * f0 * M / fs); double m = 0; for (int j = k - 4; j <= k + 4; ++j) m = std::max(m, (double) b[(size_t) j]);
        const double eb = h * f0 * in.fftSize / fs; const int e0 = (int) eb; const double ef = eb - e0;
        const double e = env[(size_t) e0] * (1 - ef) + env[(size_t) e0 + 1] * ef;
        d.push_back(20.0 * std::log10(1e-12 + m) - 10.0 * std::log10(1e-30 + e / cnt)); lev.push_back(10.0 * std::log10(1e-30 + e / cnt));
    }
    // only harmonics within 40 dB of the loudest (deep valleys are breath noise in any vocoder)
    const double top = *std::max_element(lev.begin(), lev.end()); std::vector<double> kept; for (size_t i = 0; i < d.size(); ++i) if (lev[i] > top - 40) kept.push_back(d[i]); d = kept;
    double mean = 0; for (double v : d) mean += v; mean /= (double) d.size(); double ss = 0; for (double v : d) ss += (v - mean) * (v - mean);
    return std::sqrt(ss / (double) d.size());
}
static Analysis analysed(const std::vector<float>& x, int fs) { Analysis a; AnalysisOptions o; analyse(x.data(), (int) x.size(), fs, o, a); return a; }
static std::vector<float> rendered(const Analysis& a, const Analysis* b, const RenderParams& p, RenderInfo* info = nullptr) { std::vector<float> y; render(a, b, p, y, {}, info); return y; }

// ---------------------------------------------------------------------------------------------------------------- engine tests
static void testAnalysis(int fs) {
    std::cout << "-- analysis @ " << fs << " Hz" << std::endl;
    const auto x = steadyVowel(0, 130.0, fs);
    const auto a = analysed(x, fs);
    check(finite(a), "analysis finite");
    check(a.voicedFraction() > 0.85f, "steady vowel is voiced", juce::String(a.voicedFraction(), 2));
    const double m = a.medianF0(); check(std::abs(cents(m, 130.0)) < 5.0, "f0 of a 130 Hz vowel", juce::String(m, 2) + " Hz, " + juce::String(cents(m, 130.0), 2) + " c");
    const auto F = envFormants(x, fs, 0.3, 1.2);
    check(pct(F.first, 730) < 8 && pct(F.second, 1090) < 8, "envelope F1/F2 of A near 730/1090", juce::String(F.first, 0) + " / " + juce::String(F.second, 0));
}

static void testPitchShift(int fs) {
    std::cout << "-- pitch shift +5 st @ " << fs << " Hz" << std::endl;
    // A and E: F1 well above the shifted f0. I (F1 ~270 Hz sits below the 2nd harmonic of a 160 Hz voice, where no
    // measurement can place it to 5 %) is checked with a looser bound and by its harmonic levels.
    for (int vowel : { 0, 1, 2 }) {
        const bool lowF1 = vowel == 2;
        const auto x = steadyVowel(vowel, 120.0, fs, 1.6); const auto a = analysed(x, fs);
        const auto Fin = envFormants(x, fs, 0.3, 1.3); const auto Win = waveFormants(x, fs, 0.3, 1.3);
        RenderParams p; p.mode = RenderParams::Pitch; p.pitchSemis = 5; p.keepFormants = true;
        const auto y = rendered(a, nullptr, p);
        check(finite(y) && (int) y.size() >= (int) x.size() - fs / 50, "output finite + same length");
        const double want = 120.0 * std::exp2(5.0 / 12.0), got = harmonicF0(y, fs, want, 0.3, 1.3);
        check(std::abs(cents(got, want)) < 5.0, juce::String("f0 +5 st within 5 cents (vowel ") + vowelTable()[(size_t) vowel].name + ")", juce::String(got, 2) + " Hz vs " + juce::String(want, 2) + ", " + juce::String(cents(got, want), 2) + " c");
        const auto Fo = envNear(y, fs, 0.3, 1.3, Fin.first, Fin.second); const auto Wo = waveFormants(y, fs, 0.3, 1.3);
        if (lowF1) check(pct(Fo.second, Fin.second) < 5, "keep formants (I): F2 within 5 % (F1 is below the 2nd harmonic: judged by harmonic levels)",
                         juce::String(Fin.second, 0) + " -> " + juce::String(Fo.second, 0) + "  (" + juce::String(pct(Fo.second, Fin.second), 1) + " %)");
        else check(pct(Fo.first, Fin.first) < 5 && pct(Fo.second, Fin.second) < 5, "keep formants: envelope F1/F2 within 5 %",
              juce::String(Fin.first, 0) + "/" + juce::String(Fin.second, 0) + " -> " + juce::String(Fo.first, 0) + "/" + juce::String(Fo.second, 0)
              + "  (" + juce::String(pct(Fo.first, Fin.first), 1) + " %, " + juce::String(pct(Fo.second, Fin.second), 1) + " %)");
        if (!lowF1) check(pct(Wo.first, Win.first) < 8 && pct(Wo.second, Win.second) < 8, "keep formants: waveform-LPC F1/F2 agree (independent path, 8 %)",
              juce::String(Win.first, 0) + "/" + juce::String(Win.second, 0) + " -> " + juce::String(Wo.first, 0) + "/" + juce::String(Wo.second, 0));
        { // the output's harmonic levels (independent long FFT) follow the input's envelope at those frequencies
            const double hd = harmonicLevelError(y, fs, got, a, 0.3, 1.3);
            check(hd < 3.0, "keep formants: output harmonics follow the input envelope (rms dB error < 3, 0..4 kHz, top 40 dB)", juce::String(hd, 2) + " dB");
        }
        p.keepFormants = false; const auto z = rendered(a, nullptr, p); const double ratio = std::exp2(5.0 / 12.0);
        const auto Fz = envNear(z, fs, 0.3, 1.3, Fin.first * ratio, Fin.second * ratio);
        check(pct(Fz.first, Fin.first * ratio) < 8 && pct(Fz.second, Fin.second * ratio) < 8, "without keep: F1/F2 move with the pitch (x1.335)",
              juce::String(Fz.first, 0) + "/" + juce::String(Fz.second, 0) + " vs " + juce::String(Fin.first * ratio, 0) + "/" + juce::String(Fin.second * ratio, 0));
        const double gz = harmonicF0(z, fs, want, 0.3, 1.3); check(std::abs(cents(gz, want)) < 5.0, "without keep: f0 still +5 st", juce::String(cents(gz, want), 2) + " c");
    }
}

static void testBorrow(int fs) {
    std::cout << "-- formant borrow (synth speaks) @ " << fs << " Hz" << std::endl;
    for (int vowel : { 0, 1, 2, 4 }) {
        const auto voiceB = steadyVowel(vowel, 140.0, fs, 1.4); const auto saw = synthSaw(110.0, fs, 1.4);
        const auto a = analysed(saw, fs), b = analysed(voiceB, fs);
        RenderParams p; p.mode = RenderParams::Borrow; p.timeMode = RenderParams::FollowB; p.borrowMix = 1; p.dynamics = 0.7f;
        const auto y = rendered(a, &b, p);
        const auto Fb = envFormants(voiceB, fs, 0.3, 1.1), Fy = envNear(y, fs, 0.3, 1.1, Fb.first, Fb.second);
        // F1 above the 3rd harmonic of the 110 Hz synth: 5 %; a low F1 (I, U ~ 270 Hz, between harmonics 2 and 3) can only be
        // located to about half a harmonic spacing: 10 %, plus the harmonic-level check below
        const double tol = Fb.first > 330 ? 5.0 : 10.0;
        check(pct(Fy.first, Fb.first) < tol && pct(Fy.second, Fb.second) < 5, juce::String("synth envelope F1 within ") + juce::String(tol, 0) + " % / F2 within 5 % of the voice's (" + vowelTable()[(size_t) vowel].name + ")",
              "voice " + juce::String(Fb.first, 0) + "/" + juce::String(Fb.second, 0) + "  synth " + juce::String(Fy.first, 0) + "/" + juce::String(Fy.second, 0));
        const double f = harmonicF0(y, fs, 110.0, 0.3, 1.1); check(std::abs(cents(f, 110.0)) < 5.0, "synth keeps its own pitch (110 Hz)", juce::String(cents(f, 110.0), 2) + " c");
        const double hd = harmonicLevelError(y, fs, f, b, 0.3, 1.1); check(hd < 3.0, "synth harmonics follow the voice's envelope (rms dB < 3)", juce::String(hd, 2) + " dB");
        if (vowel == 0) { const auto Fs = envFormants(saw, fs, 0.3, 1.1); check(pct(Fs.first, Fb.first) > 15 || pct(Fs.second, Fb.second) > 15, "(the raw saw's envelope was different)", juce::String(Fs.first, 0) + "/" + juce::String(Fs.second, 0)); }
    }
}

static void testContour(int fs) {
    std::cout << "-- drawn contour @ " << fs << " Hz" << std::endl;
    const auto x = steadyVowel(1, 150.0, fs, 2.0); const auto a = analysed(x, fs);
    RenderParams p; p.mode = RenderParams::Contour;
    // a phrase: 57 (A3) -> glide to 62 -> hold -> 55, plus a bend; the curve is midi vs seconds
    p.contour.add(0.0, 57); p.contour.add(0.5, 57); p.contour.add(0.8, 62); p.contour.add(1.3, 62); p.contour.add(1.35, 55); p.contour.add(2.0, 55.4f);
    RenderInfo info; const auto y = rendered(a, nullptr, p, &info);
    std::vector<double> f0; AnalysisOptions o; trackF0(y.data(), (int) y.size(), fs, o, f0);
    double worst = 0, sum = 0; int cnt = 0;
    for (int i = 0; i < (int) f0.size(); ++i) {
        const double t = i * o.framePeriodMs / 1000.0; if (t < 0.1 || t > 1.9 || f0[(size_t) i] <= 0) continue;
        if (std::abs(t - 1.325) < 0.06) continue;   // the 50 ms jump itself (an instantaneous 7-st step; the tracker smooths it)
        const double c = std::abs(cents(f0[(size_t) i], midiHz(p.contour.at(t)))); worst = std::max(worst, c); sum += c; ++cnt;
    }
    check(cnt > 200, "contour output voiced", juce::String(cnt) + " frames");
    check(sum / std::max(1, cnt) < 10.0, "drawn contour followed: mean error < 10 cents", juce::String(sum / std::max(1, cnt), 2) + " c mean, " + juce::String(worst, 1) + " c worst");
    const double hold = harmonicF0(y, fs, midiHz(62), 0.9, 1.25); check(std::abs(cents(hold, midiHz(62))) < 10, "held note (independent FFT) within 10 cents", juce::String(cents(hold, midiHz(62)), 2) + " c");
    // snap to key: a curve at 61.6 in C major snaps to 62 (D), at 60.4 -> 60
    check(juce::exactlyEqual(snapMidi(61.6f, 0, 1), 62.0f) && juce::exactlyEqual(snapMidi(60.4f, 0, 1), 60.0f) && juce::exactlyEqual(snapMidi(63.4f, 0, 1), 64.0f), "snap to key (C major)");
    check(snapMidi(61.0f, 1, 0) == 61.0f && snapMidi(66.0f, 2, 2) != 66.0f, "snap: chromatic keeps, D minor drops F#");
    RenderParams ps = p; ps.contourSnap = true; ps.root = 0; ps.scale = 1; ps.contour.clear(); ps.contour.add(0, 61.4f); ps.contour.add(2.0, 61.4f);
    const auto ys = rendered(a, nullptr, ps); const double fsn = harmonicF0(ys, fs, midiHz(61), 0.4, 1.6);
    check(std::abs(cents(fsn, midiHz(61))) < 10 || std::abs(cents(fsn, midiHz(62))) < 10, "snapped curve lands on a scale note", juce::String(hzMidi(fsn), 2));
}

static void testVowel(int fs) {
    std::cout << "-- vowel morph @ " << fs << " Hz" << std::endl;
    const auto x = steadyVowel(1, 120.0, fs, 2.0); const auto a = analysed(x, fs);
    RenderParams p; p.mode = RenderParams::VowelMorph; p.vowelAmount = 1; p.lane.add(0.0, 0); p.lane.add(0.6, 0); p.lane.add(1.4, 4); p.lane.add(2.0, 4);
    const auto y = rendered(a, nullptr, p);
    const auto FA = envFormants(y, fs, 0.15, 0.55), FU = envFormants(y, fs, 1.45, 1.85);
    const auto& A = vowelTable()[0]; const auto& U = vowelTable()[4];
    check(pct(FA.first, A.F[0]) < 8 && pct(FA.second, A.F[1]) < 8, "morph start = A", juce::String(FA.first, 0) + "/" + juce::String(FA.second, 0) + " vs " + juce::String(A.F[0], 0) + "/" + juce::String(A.F[1], 0));
    check(pct(FU.first, U.F[0]) < 10 && pct(FU.second, U.F[1]) < 10, "morph end = U", juce::String(FU.first, 0) + "/" + juce::String(FU.second, 0) + " vs " + juce::String(U.F[0], 0) + "/" + juce::String(U.F[1], 0));
    const double f = harmonicF0(y, fs, 120, 0.2, 1.8); check(std::abs(cents(f, 120)) < 5, "vowel morph keeps pitch", juce::String(cents(f, 120), 2) + " c");
    // vowel table envelope -> its own formants
    std::vector<double> env(1025); for (int v = 0; v < kVowels; ++v) { vowelEnvelope(vowelTable()[(size_t) v], 1025, fs, env.data()); const auto F = formantsOf(env.data(), 1025, fs);
        check(pct(F[0].hz, vowelTable()[(size_t) v].F[0]) < 6 && pct(F[1].hz, vowelTable()[(size_t) v].F[1]) < 6, juce::String("vowel table ") + vowelTable()[(size_t) v].name + " reads back", juce::String(F[0].hz, 0) + "/" + juce::String(F[1].hz, 0)); }
}

static void testGarbage(int fs) {
    std::cout << "-- silence / noise / unvoiced @ " << fs << " Hz" << std::endl;
    juce::Random r(7);
    std::vector<float> silence((size_t) fs, 0.0f), noise((size_t) fs), hiss((size_t) fs), dc((size_t) fs, 0.3f), tiny((size_t) (fs / 100), 0.1f), clicks((size_t) fs, 0.0f), bad((size_t) fs, 0.0f);
    for (auto& v : noise) v = 0.5f * (r.nextFloat() * 2 - 1);
    { Reso h; h.set(5000, 2000, fs); for (auto& v : hiss) v = (float) h.tick(0.6 * (r.nextFloat() * 2 - 1)); }
    for (size_t i = 0; i < clicks.size(); i += (size_t) (fs / 3)) clicks[i] = 0.9f;
    for (size_t i = 0; i < bad.size(); ++i) bad[i] = i % 100 == 0 ? std::numeric_limits<float>::quiet_NaN() : (i % 101 == 0 ? std::numeric_limits<float>::infinity() : 0.1f * std::sin((float) i * 0.05f));
    struct C { const char* name; std::vector<float>* x; float maxVoiced; } cases[] = { { "silence", &silence, 0.0f }, { "white noise", &noise, 0.1f }, { "hiss (s)", &hiss, 0.1f },
                                                                                     { "DC", &dc, 0.0f }, { "10 ms blip", &tiny, 0.0f }, { "clicks", &clicks, 0.1f }, { "NaN / inf in a tone", &bad, 1.0f } };
    for (auto& c : cases) {
        const auto a = analysed(*c.x, fs); bool sane = finite(a); for (double f : a.f0) if (f != 0 && (f < 60 || f > 820)) sane = false;
        check(sane && a.voicedFraction() <= c.maxVoiced, juce::String(c.name) + ": finite, no garbage pitch", "voiced " + juce::String(a.voicedFraction(), 3));
        for (int mode = 0; mode < 5; ++mode) { RenderParams p; p.mode = mode; p.pitchSemis = 7; p.contour.add(0, 60); p.lane.add(0, 2); p.forceVoiced = true;
            const auto y = rendered(a, &a, p); if (!finite(y)) check(false, juce::String(c.name) + " render mode " + juce::String(mode) + " finite"); }
    }
    check(true, "every mode renders finite output on every garbage input");
}

static void testCancel() {
    std::cout << "-- progress / cancel" << std::endl;
    const int fs = 44100; const auto x = steadyVowel(0, 120.0, fs, 9.0);
    std::atomic<int> pc { 0 }; std::atomic<bool> cancel { true };
    Analysis a; const bool ok = analyse(x.data(), (int) x.size(), fs, {}, a, { &pc, &cancel });
    check(!ok && a.empty(), "cancelled analysis returns false + empty");
    cancel = false; const bool ok2 = analyse(x.data(), (int) x.size(), fs, {}, a, { &pc, &cancel }); check(ok2 && pc.load() == 100, "progress reaches 100");
    // segmented f0 (9 s > one 4 s piece) matches across the seams
    check(std::abs(cents(a.medianF0(), 120.0)) < 5 && a.voicedFraction() > 0.9f, "segmented tracking seamless", juce::String(a.voicedFraction(), 3));
}

static void testCpu() {
    std::cout << "-- CPU" << std::endl;
    for (int fs : { 44100, 96000 }) {
        const double secs = 10.0; const auto x = steadyVowel(0, 120.0, fs, secs);
        for (int method : { 0, 1 }) {
            AnalysisOptions o; o.method = method; Analysis a; const auto t0 = juce::Time::getMillisecondCounterHiRes();
            analyse(x.data(), (int) x.size(), fs, o, a); const auto t1 = juce::Time::getMillisecondCounterHiRes();
            RenderParams p; p.pitchSemis = 3; std::vector<float> y; render(a, nullptr, p, y); const auto t2 = juce::Time::getMillisecondCounterHiRes();
            const double an = (t1 - t0) / 1000.0 / secs, rn = (t2 - t1) / 1000.0 / secs;
            check(an < 1.5 && rn < 0.5, juce::String("CPU ") + (method ? "FAST" : "QUALITY") + " @ " + juce::String(fs) + ": analyse x" + juce::String(an, 3) + " realtime, render x" + juce::String(rn, 3));
        }
    }
}

static void testCurveState() {
    std::cout << "-- curves" << std::endl;
    Curve c; c.add(1.0, 60); c.add(0.5, 62); c.add(2.0, 55); c.add(1.0, 61, 0.01);
    check(c.pts.size() == 3 && c.pts[0].t == 0.5 && c.at(1.0) == 61.0f && std::abs(c.at(1.5) - 58.0f) < 1e-4f && c.at(9) == 55.0f, "curve add / merge / interpolate");
    Curve d; d.load(c.save()); check(d.pts.size() == 3 && std::abs(d.at(1.5) - 58.0f) < 1e-3f, "curve save / load");
    c.erase(0.9, 1.1); check(c.pts.size() == 2, "curve erase");
}


// ---------------------------------------------------------------------------------------------------------------- generators
static std::vector<float> speechLike(int fs, double& secs) {
    struct Seg { double dur; int vowel; bool unvoiced; };
    const Seg segs[] = { { 0.12, 0, true }, { 0.34, 0, false }, { 0.08, 1, true }, { 0.30, 1, false }, { 0.16, 2, false }, { 0.10, 2, true }, { 0.36, 3, false }, { 0.22, 4, false }, { 0.25, 4, true },
                         { 0.10, 2, true }, { 0.30, 2, false }, { 0.30, 0, false }, { 0.08, 3, true }, { 0.40, 3, false }, { 0.40, 4, false }, { 0.30, 0, true } };
    std::vector<double> t0; double t = 0; for (auto& s : segs) { t0.push_back(t); t += s.dur; } secs = t;
    auto segAt = [&](double x) { int i = 0; while (i + 1 < (int) t0.size() && t0[(size_t) i + 1] <= x) ++i; return i; };
    VoiceSpec v; v.seconds = secs; v.breath = 0.02;
    v.f0 = [&](double x) { const int i = segAt(x); if (segs[i].unvoiced) return 0.0; const double phrase = x < secs * 0.55 ? x / (secs * 0.55) : (x - secs * 0.55) / (secs * 0.45); return 150.0 * std::exp2(-0.45 * phrase + 0.05 * std::sin(2 * kPi * 4.0 * x)); };
    v.F = [&](double x) { const int i = segAt(x); const double f = juce::jlimit(0.0, 1.0, (x - t0[(size_t) i]) / 0.06); const int pv = i > 0 ? segs[i - 1].vowel : segs[i].vowel;
                          const auto a = vowelF(pv), b = vowelF(segs[i].vowel); std::array<double, 4> o; for (int k = 0; k < 4; ++k) o[(size_t) k] = a[(size_t) k] + (b[(size_t) k] - a[(size_t) k]) * f; return o; };
    return synthVoice(v, fs, 11);
}
static std::vector<float> supersawLine(int fs, double secs) {   // detuned saws playing a little minor line (the carrier)
    const int notes[] = { 45, 45, 48, 52, 50, 48, 47, 45 }; std::vector<float> x((size_t) (secs * fs)); std::array<double, 7> ph {}; juce::Random r(5); for (auto& p : ph) p = r.nextDouble();
    const double det[7] = { -0.19, -0.11, -0.04, 0, 0.05, 0.12, 0.2 };
    for (size_t i = 0; i < x.size(); ++i) {
        const double t = i / (double) fs; const int n = notes[(int) (t / secs * 8) % 8]; double s = 0;
        for (int k = 0; k < 7; ++k) { const double f = midiHz(n + det[k] * 0.5); ph[(size_t) k] += f / fs; if (ph[(size_t) k] >= 1) ph[(size_t) k] -= 1;
            double v = 2 * ph[(size_t) k] - 1; const double dt = f / fs; const double p = ph[(size_t) k];   // polyBLEP
            if (p < dt) { const double q = p / dt; v -= q + q - q * q - 1; } else if (p > 1 - dt) { const double q = (p - 1) / dt; v -= q * q + q + q + 1; }
            s += v / 7; }
        x[i] = (float) (0.5 * s * std::min(1.0, std::min(t, secs - t) * 20.0));
    }
    return x;
}
// ================================================================================================================ helpers for the punch-list regressions
static std::vector<float> glideVoice(int fs, double secs) {   // a vowel gliding 110 -> 180 Hz and back (for TRACE)
    VoiceSpec v; v.seconds = secs; v.f0 = [secs](double t) { return 110.0 * std::exp2(0.7 * std::sin(kPi * t / secs)); }; v.F = [](double) { return vowelF(0); };
    return synthVoice(v, fs);
}
static AudioRef refOf(const std::vector<float>& x, int fs) { AudioRef r; r.ch.push_back(x.data()); r.n = (int) x.size(); r.fs = fs; return r; }
static AudioRef refOf2(const std::vector<float>& l, const std::vector<float>& r, int fs) { AudioRef a; a.ch.push_back(l.data()); a.ch.push_back(r.data()); a.n = (int) std::min(l.size(), r.size()); a.fs = fs; return a; }
static Rendered rr(const Analysis& a, const std::vector<float>& x, const Analysis* b, const RenderParams& p, RenderInfo* info = nullptr) { Rendered o; render(a, refOf(x, a.fs), b, p, o, {}, info); return o; }
static long rssKB() { juce::File f("/proc/self/status"); for (auto& l : juce::StringArray::fromLines(f.loadFileAsString())) if (l.startsWith("VmRSS:")) return l.substring(6).trim().getLargeIntValue(); return 0; }
static double rmsOf(const std::vector<float>& x, int a, int b) { double e = 0; a = std::max(0, a); b = std::min((int) x.size(), b); for (int i = a; i < b; ++i) e += (double) x[(size_t) i] * x[(size_t) i]; return std::sqrt(e / std::max(1, b - a)); }
static double db(double v) { return 20.0 * std::log10(std::max(1e-12, v)); }
static std::vector<float> fftMag(const std::vector<float>& x, int s0, int len, int& M) {
    int order = 1; while ((1 << order) < len * 2) ++order; M = 1 << order; juce::dsp::FFT fft(order); std::vector<float> b((size_t) M * 2, 0.0f);
    for (int i = 0; i < len && s0 + i < (int) x.size(); ++i) b[(size_t) i] = x[(size_t) (s0 + i)] * (float) (0.5 - 0.5 * std::cos(2 * kPi * (i + 0.5) / len));
    fft.performFrequencyOnlyForwardTransform(b.data(), true); b.resize((size_t) M / 2 + 1); return b;
}
static double bandEnergy(const std::vector<float>& x, int fs, double t0, double t1, double f0, double f1) {
    int M; const auto m = fftMag(x, (int) (t0 * fs), (int) ((t1 - t0) * fs), M); double e = 0;
    for (int k = (int) (f0 * M / fs); k <= (int) (f1 * M / fs) && k < (int) m.size(); ++k) e += (double) m[(size_t) k] * m[(size_t) k];
    return e;
}
// harmonic-to-interharmonic ratio (dB) of a steady region in a band
static double hnr(const std::vector<float>& x, int fs, double f0, double t0, double t1, double b0, double b1) {
    int M; const auto m = fftMag(x, (int) (t0 * fs), (int) ((t1 - t0) * fs), M); const double binHz = fs / (double) M;
    double H = 0, N = 0; int hb = 0, nbins = 0;
    for (int h = 1; h * f0 < b1; ++h) { if (h * f0 < b0) continue;
        const int c = (int) std::lround(h * f0 / binHz), w = std::max(1, (int) (0.08 * f0 / binHz));
        for (int k = c - w; k <= c + w; ++k) { H += (double) m[(size_t) k] * m[(size_t) k]; ++hb; }
        for (int k = (int) ((h + 0.35) * f0 / binHz); k <= (int) ((h + 0.65) * f0 / binHz); ++k) { N += (double) m[(size_t) k] * m[(size_t) k]; ++nbins; } }
    return 10.0 * std::log10(std::max(1e-30, H) / std::max(1e-30, N / std::max(1, nbins) * hb));
}
static std::vector<float> kickPattern(int fs, double secs = 2.0) {
    std::vector<float> x((size_t) (fs * secs)); juce::Random r(3);
    for (int hit = 0; hit < (int) (secs * 4); ++hit) { const int s0 = hit * fs / 4; for (int i = 0; i < fs / 5 && s0 + i < (int) x.size(); ++i) { const double t = i / (double) fs;
        x[(size_t) (s0 + i)] += (float) (0.8 * std::exp(-t * 25) * std::sin(2 * kPi * (50 + 120 * std::exp(-t * 40)) * t) + (hit % 2 ? 0.3 * std::exp(-t * 80) * (r.nextFloat() * 2 - 1) : 0.0)); } }
    return x;
}
static std::pair<double, double> attackOf(const std::vector<float>& v, int fs, int s0) {   // 10 %-to-peak ms, peak
    float pk = 0; int at = s0; for (int i = s0; i < s0 + fs / 20; ++i) if (std::abs(v[(size_t) i]) > pk) { pk = std::abs(v[(size_t) i]); at = i; }
    int k = s0 - fs / 50; while (k < at && std::abs(v[(size_t) k]) < 0.1f * pk) ++k; return { (at - k) * 1000.0 / fs, pk };
}
static std::vector<float> breathyVowel(int fs, double secs, double breath) {
    VoiceSpec v; v.seconds = secs; v.breath = breath; v.f0 = [](double) { return 140.0; }; v.F = [](double) { return vowelF(0); }; return synthVoice(v, fs, 21);
}
static std::vector<float> triad(int fs, double secs) {
    std::vector<float> A((size_t) (fs * secs)); const double f[3] = { midiHz(48), midiHz(52), midiHz(55) };
    for (size_t i = 0; i < A.size(); ++i) { double s = 0; for (double fr : f) for (int h = 1; h * fr < 8000; ++h) s += std::sin(2 * kPi * fr * h * i / fs + h) / h; A[i] = (float) (0.12 * s); }
    return A;
}
static void supersawStereo(int fs, double secs, std::vector<float>& L, std::vector<float>& R) {
    L.assign((size_t) (fs * secs), 0.0f); R = L; const double det[7] = { -0.19, -0.11, -0.04, 0, 0.05, 0.12, 0.2 }; juce::Random rnd(9); double ph[7]; for (auto& p : ph) p = rnd.nextDouble();
    for (size_t i = 0; i < L.size(); ++i) { double l = 0, r = 0; for (int k = 0; k < 7; ++k) { ph[k] += midiHz(45 + det[k]) / fs; if (ph[k] >= 1) ph[k] -= 1; const double v = 2 * ph[k] - 1; const double pan = (k - 3) / 3.0; l += v * (1 - pan) / 14; r += v * (1 + pan) / 14; }
        L[i] = (float) (0.5 * l); R[i] = (float) (0.5 * r); }
}
static double corr(const std::vector<float>& a, const std::vector<float>& b) {
    const size_t n = std::min(a.size(), b.size()); double ab = 0, aa = 0, bb = 0; for (size_t i = 0; i < n; ++i) { ab += (double) a[i] * b[i]; aa += (double) a[i] * a[i]; bb += (double) b[i] * b[i]; }
    return ab / std::sqrt(std::max(1e-30, aa * bb));
}
static std::vector<float> mixdown(const Rendered& r) { if (!r.stereo()) return r.L; std::vector<float> m(r.L.size()); for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (r.L[i] + r.R[i]); return m; }

// ---------------------------------------------------------------------------------------------------------------- streaming synthesis = batch synthesis
static void testStreamingSynth() {
    std::cout << "-- streamed synthesis (flat memory) vs WORLD's batch synthesis" << std::endl;
    const int fs = 44100; const auto x = steadyVowel(1, 130, fs, 2.0); const auto a = analysed(x, fs);
    RenderParams p; p.normalise = false; p.transients = false; p.breath = 0; const auto y = rr(a, x, nullptr, p);
    // the same frames through Synthesis() (all in memory)
    const int F = a.frames(), bins = a.bins(); std::vector<double> f0 = effectiveF0(a, p), sp((size_t) F * bins), ap((size_t) F * bins); std::vector<double*> rs((size_t) F), as((size_t) F);
    for (int i = 0; i < F; ++i) { rs[(size_t) i] = sp.data() + (size_t) i * bins; as[(size_t) i] = ap.data() + (size_t) i * bins; a.specRow(i, rs[(size_t) i]); a.apRow(i, as[(size_t) i]);
        calibrateAperiodicity(as[(size_t) i], bins, fs, a.periodicity[(size_t) i]); }
    const int yl = (int) ((F - 1) * a.framePeriod / 1000.0 * fs) + 1; std::vector<double> z((size_t) yl);
    Synthesis(f0.data(), F, rs.data(), as.data(), a.fftSize, a.framePeriod, fs, yl, z.data());
    std::vector<float> zf(z.begin(), z.end());
    const double c = corr(std::vector<float>(zf.begin() + fs / 4, zf.end() - fs / 4), std::vector<float>(y.L.begin() + fs / 4, y.L.begin() + (long) zf.size() - fs / 4));
    check(finite(y.L) && (int) y.L.size() == yl, "streamed render: finite, same length as batch", juce::String((int) y.L.size()) + " vs " + juce::String(yl));
    check(c > 0.95, "streamed = batch synthesis (aligned, no lag)", "corr " + juce::String(c, 4));
    check(std::abs(db(rmsOf(y.L, fs / 4, fs * 7 / 4) / rmsOf(x, fs / 4, fs * 7 / 4))) < 2.0, "level kept at 0 st", juce::String(db(rmsOf(y.L, fs / 4, fs * 7 / 4) / rmsOf(x, fs / 4, fs * 7 / 4)), 2) + " dB");
}

// ---------------------------------------------------------------------------------------------------------------- 1 double analysis
static void test01DoubleAnalysis() {
    std::cout << "-- [1] one analysis per sound (double ANALYSE / undo / redo)" << std::endl;
    const int fs = 44100; VoiceHost h; h.autoRender = false;
    h.setAudio(SlotA, steadyVowel(0, 120, fs, 6.0), fs, "A", true); juce::Thread::sleep(100); h.cancel(); h.waitIdle(20000);
    auto s = h.source(SlotA); check(s && !s->ready(), "analysis cancelled");
    h.reanalyse(); h.reanalyse(); h.reanalyse();   // three presses while the first is queued / running
    std::shared_ptr<const Analysis> first; int replaced = 0; const auto end = juce::Time::getMillisecondCounter() + 60000;
    while (juce::Time::getMillisecondCounter() < end) { auto a = s->analysis(); if (a && !first) first = a; if (a && first && a != first) ++replaced; if (h.waitIdle(1) && first) break; }
    juce::Thread::sleep(200); auto last = s->analysis();
    check(first && last == first && replaced == 0, "ANALYSE x3: the analysis is published once and never replaced under readers", "replaced " + juce::String(replaced));
    const auto before = s->analysis(); h.undo(); h.redo(); h.waitIdle(20000);
    check(h.source(SlotA) == s && s->analysis() == before, "undo / redo keep the same analysis (no re-analysis)");
}

// ---------------------------------------------------------------------------------------------------------------- 2 source time
static void test02SourceTime() {
    std::cout << "-- [2] CONTOUR / VOWEL / TRACE in source time" << std::endl;
    const int fs = 44100; VoiceHost h; h.autoRender = false;
    VoiceSpec v; v.seconds = 3.0; v.f0 = [](double t) { return 110.0 * std::exp2(t / 3.0); }; v.F = [](double) { return vowelF(0); };
    const auto x = synthVoice(v, fs); h.setAudio(SlotA, x, fs, "glide", false);
    for (float sp : { 0.5f, 2.0f }) {
        auto p = h.params(); p.mode = RenderParams::Contour; p.contour = h.traceContour(); p.speed = sp; h.setParams(p); juce::String e; h.renderNow(e); auto r = h.result();
        double sum = 0, worst = 0; int n = 0;
        for (size_t j = 0; j < r->info.f0.size(); ++j) { const double ts = r->info.tSrc[j]; if (r->info.f0[j] <= 0 || ts < 0.1 || ts > 2.9) continue; const double c = std::abs(cents(r->info.f0[j], 110.0 * std::exp2(ts / 3.0))); sum += c; worst = std::max(worst, c); ++n; }
        check(n > 50 && sum / n < 10, "TRACE + SPEED " + juce::String(sp, 1) + ": result follows the source's own pitch", juce::String(sum / std::max(1, n), 2) + " c mean, worst " + juce::String(worst, 1));
    }
    // the vowel lane at SPEED 0.5: A at the start of the source, U at its end
    h.setAudio(SlotA, steadyVowel(1, 115, fs, 3.0), fs, "E", false);
    auto p = h.params(); p.mode = RenderParams::VowelMorph; p.speed = 0.5f; p.contour.clear(); p.lane.clear(); p.lane.add(0.0, 0); p.lane.add(1.0, 0); p.lane.add(2.0, 4); p.lane.add(3.0, 4); h.setParams(p);
    juce::String e; h.renderNow(e); auto r = h.result(); const auto y = mixdown(r->audio);
    const auto FA = envFormants(y, fs, 0.4, 1.6), FU = envFormants(y, fs, 4.6, 5.6);   // output seconds = source x 2
    check(pct(FA.first, 730) < 10 && pct(FU.first, 300) < 15, "vowel lane at SPEED 0.5 lands on the same syllables", juce::String(FA.first, 0) + " / " + juce::String(FU.first, 0));
}

// ---------------------------------------------------------------------------------------------------------------- 3 vocode carrier
static void test03Vocode() {
    std::cout << "-- [3] BORROW keeps chords / pads / detune (VOCODE carrier)" << std::endl;
    const int fs = 48000; const auto A = triad(fs, 2.0); const auto B = steadyVowel(0, 120, fs, 2.0);
    const auto aA = analysed(A, fs), aB = analysed(B, fs);
    RenderParams p; p.mode = RenderParams::Borrow; RenderInfo info; const auto y = rr(aA, A, &aB, p, &info); const auto m = mixdown(y);
    auto peak = [&](const std::vector<float>& v, double hz) { return bandEnergy(v, fs, 0.4, 1.6, hz - 3, hz + 3); };
    const double eIn = 10 * std::log10(peak(A, midiHz(52)) / peak(A, midiHz(48))), eOut = 10 * std::log10(peak(m, midiHz(52)) / peak(m, midiHz(48)));
    const double gIn = 10 * std::log10(peak(A, midiHz(55)) / peak(A, midiHz(48))), gOut = 10 * std::log10(peak(m, midiHz(55)) / peak(m, midiHz(48)));
    check(info.vocoded, "a chord as A picks the VOCODE carrier automatically", "A voiced " + juce::String(aA.voicedFraction(), 2));
    // every note of the chord is still a tone (a sharp peak), not noise: peak vs the gaps beside it
    auto tonal = [&](const std::vector<float>& v, double hz) { return 10 * std::log10(peak(v, hz) / std::max(1e-30, 0.5 * (bandEnergy(v, fs, 0.4, 1.6, hz - 14, hz - 8) + bandEnergy(v, fs, 0.4, 1.6, hz + 8, hz + 14)))); };
    const double tC = tonal(m, midiHz(48)), tE = tonal(m, midiHz(52)), tG = tonal(m, midiHz(55));
    RenderParams pr = p; pr.carrier = RenderParams::CarrierResynth; const auto yr = mixdown(rr(aA, A, &aB, pr)); const double rE = tonal(yr, midiHz(52));
    check(tC > 20 && tE > 20 && tG > 20, "triad: C, E and G are all still tones (peak over gaps > 20 dB)",
          juce::String(tC, 0) + " / " + juce::String(tE, 0) + " / " + juce::String(tG, 0) + " dB; RESYNTH carrier E " + juce::String(rE, 0) + " dB; level E/C " + juce::String(eIn, 1) + " -> " + juce::String(eOut, 1) + ", G/C " + juce::String(gIn, 1) + " -> " + juce::String(gOut, 1));
    // the chord takes on the voice's envelope: its spectrum peaks move to the voice's F1 / F2 region
    const double lowRatioIn = 10 * std::log10(bandEnergy(A, fs, 0.4, 1.6, 600, 900) / bandEnergy(A, fs, 0.4, 1.6, 1500, 2200));
    const double lowRatioOut = 10 * std::log10(bandEnergy(m, fs, 0.4, 1.6, 600, 900) / bandEnergy(m, fs, 0.4, 1.6, 1500, 2200));
    const double lowRatioB = 10 * std::log10(bandEnergy(B, fs, 0.4, 1.6, 600, 900) / bandEnergy(B, fs, 0.4, 1.6, 1500, 2200));
    check(std::abs(lowRatioOut - lowRatioB) < 4.0, "vocoded chord's F1 / mid balance matches the voice (A " + juce::String(lowRatioIn, 1) + " dB)", "out " + juce::String(lowRatioOut, 1) + " dB, voice " + juce::String(lowRatioB, 1) + " dB");
    // a stereo supersaw keeps its width; a single voice as A still resynthesises
    std::vector<float> L, R; supersawStereo(fs, 2.0, L, R); std::vector<float> mono(L.size()); for (size_t i = 0; i < L.size(); ++i) mono[i] = 0.5f * (L[i] + R[i]);
    const auto aS = analysed(mono, fs); Rendered ys; RenderParams pv = p; pv.carrier = RenderParams::CarrierVocode; render(aS, refOf2(L, R, fs), &aB, pv, ys);
    check(preferVocode(aS, refOf2(L, R, fs), 0.45f), "AUTO picks VOCODE for a wide stereo supersaw (one pitch, many voices)", "A voiced " + juce::String(aS.voicedFraction(), 2));
    check(ys.stereo() && corr(ys.L, ys.R) < 0.95 && corr(L, R) < 0.95, "stereo supersaw keeps its width through VOCODE", "L/R corr in " + juce::String(corr(L, R), 2) + ", out " + juce::String(corr(ys.L, ys.R), 2));
    const auto V = steadyVowel(2, 150, fs, 2.0); const auto aV = analysed(V, fs); RenderInfo i2; rr(aV, V, &aB, p, &i2);
    check(!i2.vocoded, "a single voice as A keeps the RESYNTH carrier");
}

// ---------------------------------------------------------------------------------------------------------------- 4 rates + memory guard
static void test04Rates() {
    std::cout << "-- [4] every way in is rate-normalised and capped; the worker survives bad_alloc" << std::endl;
    VoiceHost h; h.autoRender = false;
    for (int fs : { 8000, 192000, 655350 }) {
        std::vector<float> x((size_t) (fs * 0.5)); for (size_t i = 0; i < x.size(); ++i) x[i] = 0.3f * (float) std::sin(2 * kPi * 220 * i / fs);
        h.setAudio(SlotA, x, fs, "r", false); const int got = h.source(SlotA)->fs();
        check(got == 48000, "setAudio at " + juce::String(fs) + " Hz -> working rate 48000", juce::String(got));
    }
    { std::vector<float> x((size_t) (16000 * 0.5), 0.1f); h.setAudio(SlotA, x, 16000, "16k", false); check(h.source(SlotA)->fs() == 16000, "[34] 16 kHz stays native (no 3x upsampling)"); }
    // state carrying 8 kHz / 192 kHz audio
    for (int fs : { 8000, 192000 }) {
        juce::MemoryBlock mb; { juce::FlacAudioFormat flac; auto* os = new juce::MemoryOutputStream(mb, false); std::unique_ptr<juce::AudioFormatWriter> w(flac.createWriterFor(os, fs, 1, 16, {}, 0));
            std::vector<float> x((size_t) fs / 2); for (size_t i = 0; i < x.size(); ++i) x[i] = 0.3f * (float) std::sin(2 * kPi * 220 * i / fs); const float* c[1] = { x.data() }; w->writeFromFloatArrays(c, 1, (int) x.size()); }
        juce::ValueTree t("VOICE"); juce::ValueTree s("SLOT"); s.setProperty("index", 0, nullptr); s.setProperty("flac", mb.toBase64Encoding(), nullptr); t.appendChild(s, nullptr);
        VoiceHost g; g.autoRender = false; g.fromTree(t); g.waitIdle(30000);
        check(g.source(SlotA) && g.source(SlotA)->fs() == 48000 && g.source(SlotA)->ready(), "state with " + juce::String(fs) + " Hz audio is normalised to 48 kHz");
    }
    h.runOnWorker([] { throw std::bad_alloc(); }); h.waitIdle(5000);
    juce::String e; check(h.status().contains("Out of memory") && h.renderNow(e), "a bad_alloc on the worker becomes a status line (no terminate); the host still works", h.status());
}

// ---------------------------------------------------------------------------------------------------------------- 5 compact analysis + undo memory
static void test05Memory() {
    std::cout << "-- [5] compact analyses; the undo history holds audio, not analyses" << std::endl;
    const int fs = 48000; const auto x = steadyVowel(0, 120, fs, 60.0); Analysis a; analyse(x.data(), (int) x.size(), fs, {}, a);
    const double oldMB = (double) a.frames() * a.bins() * 2 * sizeof(double) / 1048576.0, newMB = a.memoryBytes() / 1048576.0;
    check(newMB < oldMB / 4, "60 s @ 48 kHz analysis: " + juce::String(oldMB, 0) + " MB (double spec + ap) -> " + juce::String(newMB, 1) + " MB");
    // envelope precision of the 16-bit code
    { std::vector<double> e((size_t) a.bins()); a.specRow(600, e.data()); const auto F = formantsOf(e.data(), a.bins(), fs, 120); check(pct(F[0].hz, 730) < 8 && pct(F[1].hz, 1090) < 8, "decoded envelope keeps its formants", juce::String(F[0].hz, 0) + "/" + juce::String(F[1].hz, 0)); }
    VoiceHost h; h.autoRender = false; const long rss0 = rssKB();
    for (int i = 0; i < 10; ++i) h.setAudio(i % 2, steadyVowel(i % 5, 110 + 5 * i, fs, 60.0), fs, "take " + juce::String(i), false);
    const double liveMB = h.liveAnalysisBytes() / 1048576.0, rssMB = (rssKB() - rss0) / 1024.0;
    check(liveMB < 4.5 * newMB, "10 loads of 60 s: analyses kept for the 2 slots + 2 recent only", juce::String(liveMB, 1) + " MB");
    check(rssMB < 900, "RSS growth for 10 x 60 s loads stays bounded", juce::String(rssMB, 0) + " MB (audio in undo " + juce::String(h.undoAudioBytes() / 1048576.0, 0) + " MB)");
    check(h.undo() && h.undo() && h.waitIdle(120000) && h.source(SlotA)->ready(), "an undone step whose analysis was dropped analyses again on demand");
}

// ---------------------------------------------------------------------------------------------------------------- 6 + 7 long files, speed, obsolete analyses
static void test06LongAndFast() {
    std::cout << "-- [6][7] 5-minute cap, parallel analysis, obsolete analyses cancelled, time left" << std::endl;
    const int fs = 44100; VoiceHost h; h.autoRender = false;
    { std::vector<float> x((size_t) (fs * 310.0), 0.0f); for (size_t i = 0; i < x.size(); ++i) x[i] = 0.2f * (float) std::sin(2 * kPi * 150 * i / fs);
      h.setAudio(SlotA, x, fs, "long", true); check(h.source(SlotA)->seconds() <= 300.0 + 1e-6 && h.trimmedNote(SlotA).contains("trimmed"), "310 s -> trimmed to 5:00 with a banner", h.trimmedNote(SlotA));
      juce::Thread::sleep(1500); const double left = h.secondsLeft(); check(left > 0, "time left is reported while analysing", juce::String(left, 1) + " s");
      // replacing A cancels its obsolete analysis right away
      const auto old = h.source(SlotA); const auto t0 = juce::Time::getMillisecondCounterHiRes();
      h.setAudio(SlotA, steadyVowel(0, 120, fs, 2.0), fs, "short", true); h.waitIdle(60000); const double took = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
      check(old->failed.load() && !old->ready() && h.source(SlotA)->ready() && took < 10, "a replaced source's analysis is cancelled (new one ready in " + juce::String(took, 1) + " s)"); }
    const auto x = steadyVowel(1, 130, fs, 30.0);
    AnalysisOptions o1; o1.threads = 1; AnalysisOptions oN;
    Analysis a1, aN; auto t = juce::Time::getMillisecondCounterHiRes(); analyse(x.data(), (int) x.size(), fs, o1, a1); const double s1 = (juce::Time::getMillisecondCounterHiRes() - t) / 1000.0;
    t = juce::Time::getMillisecondCounterHiRes(); analyse(x.data(), (int) x.size(), fs, oN, aN); const double sN = (juce::Time::getMillisecondCounterHiRes() - t) / 1000.0;
    bool same = a1.frames() == aN.frames(); double md = 0; for (int i = 0; same && i < a1.frames(); ++i) md = std::max(md, std::abs(a1.f0[(size_t) i] - aN.f0[(size_t) i]));
    check(sN < s1 / 1.6, "parallel analysis: 30 s in " + juce::String(sN, 1) + " s (1 thread " + juce::String(s1, 1) + " s, x" + juce::String(30.0 / sN, 1) + " realtime)");
    check(same && md < 1e-6, "parallel result = single-threaded result", "max f0 diff " + juce::String(md, 8));
    { const auto y = steadyVowel(0, 120, fs, 300.0); Analysis a; t = juce::Time::getMillisecondCounterHiRes(); analyse(y.data(), (int) y.size(), fs, {}, a);
      const double s = (juce::Time::getMillisecondCounterHiRes() - t) / 1000.0; RenderParams p; p.pitchSemis = 3; std::vector<float> out; const long r0 = rssKB(); t = juce::Time::getMillisecondCounterHiRes(); render(a, nullptr, p, out);
      const double sr = (juce::Time::getMillisecondCounterHiRes() - t) / 1000.0; const double mem = (rssKB() - r0) / 1024.0;
      check(a.voicedFraction() > 0.9 && (int) out.size() > fs * 299 && s < 120, "5 min source: analysed in " + juce::String(s, 1) + " s, rendered in " + juce::String(sr, 1) + " s", "analysis " + juce::String(a.memoryBytes() / 1048576.0, 0) + " MB, render RSS +" + juce::String(mem, 0) + " MB"); }
}

// ---------------------------------------------------------------------------------------------------------------- 8 transients
static void test08Transients() {
    std::cout << "-- [8] percussive attacks + no pre-echo" << std::endl;
    const int fs = 48000; const auto x = kickPattern(fs); const auto a = analysed(x, fs);
    RenderParams p; p.normalise = false; const auto y = rr(a, x, nullptr, p); p.transients = false; const auto z = rr(a, x, nullptr, p);
    const auto ai = attackOf(x, fs, fs / 2), ao = attackOf(y.L, fs, fs / 2), az = attackOf(z.L, fs, fs / 2);
    const double preIn = db(rmsOf(x, fs / 2 - fs / 100, fs / 2 - fs / 1000)), preOut = db(rmsOf(y.L, fs / 2 - fs / 100, fs / 2 - fs / 1000)), preOff = db(rmsOf(z.L, fs / 2 - fs / 100, fs / 2 - fs / 1000));
    check(a.onsets.size() >= 7, "onsets found", juce::String((int) a.onsets.size()));
    check(ao.first < 2.5 && preOut < -60, "TRANSIENTS on: attack " + juce::String(ao.first, 2) + " ms (in " + juce::String(ai.first, 2) + "), pre-echo " + juce::String(preOut, 0) + " dB",
          "off: attack " + juce::String(az.first, 2) + " ms, pre-echo " + juce::String(preOff, 0) + " dB");
    RenderParams q; q.normalise = false; q.pitchSemis = 5; const auto w = rr(a, x, nullptr, q); q.transients = false; const auto wo = rr(a, x, nullptr, q);
    const double hfOn = db(std::sqrt(bandEnergy(w.L, fs, 0.49, 0.499, 2500, 16000))), hfOff = db(std::sqrt(bandEnergy(wo.L, fs, 0.49, 0.499, 2500, 16000)));
    check(hfOn < hfOff - 10, "+5 st: high-band pre-echo removed too", juce::String(hfOff, 0) + " -> " + juce::String(hfOn, 0) + " dB");
    // a sung vowel at 0 st is untouched by TRANSIENTS (no stray onsets in a steady note)
    const auto v = steadyVowel(0, 130, fs, 2.0); const auto av = analysed(v, fs); int inside = 0; for (auto& o : av.onsets) if (o.t > 0.2 && o.t < 1.8 && o.strength >= 0.5f) ++inside;
    check(inside == 0, "no false onsets inside a steady vowel", juce::String(inside));
}

// ---------------------------------------------------------------------------------------------------------------- 8a breathiness
static void diagBreath() {
    const int fs = 48000;
    for (double br : { 0.25, 0.08, 0.01 }) { const auto x = breathyVowel(fs, 2.0, br); const auto a = analysed(x, fs); double per = 0, a15 = 0, a3 = 0, a6 = 0; int n = 0;
        for (int i = a.frameAt(0.4); i < a.frameAt(1.6); ++i) { if (!a.voiced(i)) continue; const auto ap = a.apFrame(i); const double bh = fs / (2.0 * (a.bins() - 1)); per += a.periodicity[(size_t) i]; a15 += ap[(size_t) (1500 / bh)]; a3 += ap[(size_t) (3000 / bh)]; a6 += ap[(size_t) (6000 / bh)]; ++n; }
        for (float b : { -0.5f, 0.0f, 0.25f, 0.5f, 0.75f }) { RenderParams p; p.normalise = false; p.transients = false; p.breath = b; const auto y = rr(a, x, nullptr, p); std::cout << "   BREATH " << b << " -> HNR1-4k " << hnr(y.L, fs, 140, 0.4, 1.6, 1000, 4000) << " 4-8k " << hnr(y.L, fs, 140, 0.4, 1.6, 4000, 8000) << std::endl; }
        std::cout << "breath " << br << ": periodicity " << per / n << " ap1.5k " << a15 / n << " ap3k " << a3 / n << " ap6k " << a6 / n << " HNR1-4k " << hnr(x, fs, 140, 0.4, 1.6, 1000, 4000) << " HNR4-8k " << hnr(x, fs, 140, 0.4, 1.6, 4000, 8000) << std::endl; }
}
static void test08aBreath() {
    std::cout << "-- [8a] breathiness kept (HNR at 0 st)" << std::endl;
    const int fs = 48000;
    for (double br : { 0.25, 0.08, 0.01 }) {
        const auto x = breathyVowel(fs, 2.0, br); const auto a = analysed(x, fs); RenderParams p; p.normalise = false; p.transients = false; const auto y = rr(a, x, nullptr, p);
        const double l1 = hnr(x, fs, 140, 0.4, 1.6, 1000, 4000), l2 = hnr(y.L, fs, 140, 0.4, 1.6, 1000, 4000), h1 = hnr(x, fs, 140, 0.4, 1.6, 4000, 8000), h2 = hnr(y.L, fs, 140, 0.4, 1.6, 4000, 8000);
        check(std::abs(l2 - l1) < 3.0 && std::abs(h2 - h1) < 3.0, juce::String(br > 0.1 ? "breathy" : br > 0.05 ? "slightly breathy" : "clean") + " vowel HNR within 3 dB (voiced " + juce::String(a.voicedFraction(), 2) + ")",
              "1-4k " + juce::String(l1, 1) + " -> " + juce::String(l2, 1) + ", 4-8k " + juce::String(h1, 1) + " -> " + juce::String(h2, 1));
        if (br > 0.1) { RenderParams q = p; q.breath = -1; const auto ya = rr(a, x, nullptr, q); q.breath = 1; const auto yb = rr(a, x, nullptr, q);
            const double ha = hnr(ya.L, fs, 140, 0.4, 1.6, 1000, 4000), hb = hnr(yb.L, fs, 140, 0.4, 1.6, 1000, 4000);
            check(ha > l2 + 4 && hb < l2 - 4, "BREATH is bipolar around the natural setting", "-1: " + juce::String(ha, 1) + " dB, +1: " + juce::String(hb, 1) + " dB"); }
    }
}

// ---------------------------------------------------------------------------------------------------------------- 9 sub bass
static void test09Sub() {
    std::cout << "-- [9] sub / 808 down to 25 Hz" << std::endl;
    const int fs = 48000; for (double f : { 27.5, 32.7 }) {
        const auto x = synthSaw(f, fs, 2.0); AnalysisOptions o; o.f0Floor = 25; o.f0Ceil = 400; Analysis a; analyse(x.data(), (int) x.size(), fs, o, a);
        check(a.voicedFraction() > 0.8 && std::abs(cents(a.medianF0(), f)) < 10, "tracks " + juce::String(f, 1) + " Hz with the Sub range", juce::String(a.voicedFraction(), 2) + ", " + juce::String(a.medianF0(), 2) + " Hz");
        RenderParams p; p.pitchSemis = 12; p.transients = false; const auto y = rr(a, x, nullptr, p); const double g = harmonicF0(y.L, fs, 2 * f, 0.4, 1.6);
        check(std::abs(cents(g, 2 * f)) < 10, "  +12 st render lands on " + juce::String(2 * f, 1) + " Hz", juce::String(cents(g, 2 * f), 2) + " c");
    }
    VoiceHost h; auto o = h.analysisOptions(); o.f0Floor = 25; h.setAnalysisOptions(o); check(std::abs(h.analysisOptions().f0Floor - 25) < 1e-9, "host keeps a 25 Hz floor end to end");
}

// ---------------------------------------------------------------------------------------------------------------- 10 resampler
static void test10Resampler() {
    std::cout << "-- [10] anti-aliased resampling" << std::endl;
    const int from = 192000; std::vector<float> x((size_t) from), y((size_t) from);
    for (int i = 0; i < from; ++i) { x[(size_t) i] = 0.5f * (float) std::sin(2 * kPi * 30000.0 * i / from); y[(size_t) i] = 0.5f * (float) std::sin(2 * kPi * 1000.0 * i / from); }
    const auto xo = resample(x, from, 48000), yo = resample(y, from, 48000);
    const double alias = 10 * std::log10(bandEnergy(xo, 48000, 0.2, 0.8, 17000, 19000) / bandEnergy(yo, 48000, 0.2, 0.8, 900, 1100));
    check(alias < -90, "30 kHz at 192 kHz -> 48 kHz: alias at 18 kHz " + juce::String(alias, 1) + " dB");
    check(std::abs(db(rmsOf(yo, 9600, 38400) / rmsOf(y, 38400, 153600))) < 0.05, "1 kHz passes at unity", juce::String(db(rmsOf(yo, 9600, 38400) / rmsOf(y, 38400, 153600)), 3) + " dB");
}

// ---------------------------------------------------------------------------------------------------------------- 11 gain
static void test11Gain() {
    std::cout << "-- [11] GAIN after NORMALISE" << std::endl;
    const int fs = 44100; const auto x = steadyVowel(0, 120, fs, 1.0); const auto a = analysed(x, fs);
    RenderParams p; p.gainDb = -12; const auto y = rr(a, x, nullptr, p); float pk = 0; for (float v : y.L) pk = std::max(pk, std::abs(v));
    check(std::abs(pk - 0.89f * 0.2512f) < 0.01f, "NORMALISE + GAIN -12 dB -> peak " + juce::String(pk, 3) + " (0.224)");
    p.gainDb = 12; const auto z = rr(a, x, nullptr, p); pk = 0; for (float v : z.L) pk = std::max(pk, std::abs(v)); check(pk <= 1.0f && pk > 0.95f, "+12 dB is soft-limited at 0 dBFS", juce::String(pk, 3));
}

// ---------------------------------------------------------------------------------------------------------------- 13 stale results
static void test13Stale() {
    std::cout << "-- [13] out-of-date results" << std::endl;
    const int fs = 44100; VoiceHost h; h.autoRender = false; h.setAudio(SlotA, steadyVowel(0, 120, fs, 1.0), fs, "a", false);
    juce::String e; h.renderNow(e); check(!h.resultStale(), "fresh after a render");
    auto p = h.params(); p.pitchSemis = 7; h.setParams(p); check(h.resultStale(), "settings changed -> OUT OF DATE");
    juce::String why; auto f = h.writeResultWav(why); check(f.existsAsFile() && !h.resultStale() && std::abs(h.result()->params.pitchSemis - 7) < 1e-6f && f.getFileName().contains("+7st"), "drag-out renders the current settings first", f.getFileName());
    f.deleteFile(); h.clearSlot(SlotA); check(h.result() == nullptr && !h.writeResultWav(why).exists(), "clearing A drops the result (nothing old to drag)");
}

// ---------------------------------------------------------------------------------------------------------------- 15 TUNE
static void test15Tune() {
    std::cout << "-- [15] TUNE: retune speed, flatten, vibrato kept" << std::endl;
    const int fs = 44100;
    // a phrase of off-key notes (C+30c, D-40c, E+20c, ...) with 5.5 Hz vibrato of +-30 cents
    const double notes[] = { 60.3, 61.6, 64.2, 65.35, 67.4 };
    VoiceSpec v; v.seconds = 2.5; v.f0 = [&](double t) { const int i = std::min(4, (int) (t / 0.5)); return midiHz(notes[i] + 0.3 * std::sin(2 * kPi * 5.5 * t)); }; v.F = [](double) { return vowelF(1); };
    const auto x = synthVoice(v, fs); const auto a = analysed(x, fs);
    RenderParams p; p.mode = RenderParams::Tune; p.root = 0; p.scale = 1; p.retuneMs = 40; p.flatten = 0; p.transients = false; RenderInfo info; rr(a, x, nullptr, p, &info);
    int jumps = 0; double centreErr = 0; int centres = 0; double depthIn = 0, depthOut = 0;
    for (size_t j = 1; j < info.f0.size(); ++j) if (info.f0[j] > 0 && info.f0[j - 1] > 0 && std::abs(12 * std::log2(info.f0[j] / info.f0[j - 1])) >= 1.0) ++jumps;
    for (int n = 0; n < 5; ++n) { double s = 0, s2 = 0, si = 0; int c = 0; for (size_t j = 0; j < info.f0.size(); ++j) { const double t = info.tSrc[j]; if (t < n * 0.5 + 0.12 || t > n * 0.5 + 0.38 || info.f0[j] <= 0) continue;
            const double m = hzMidi(info.f0[j]); s += m; s2 += m * m; si += hzMidi(a.f0[(size_t) a.frameAt(t)]) * hzMidi(a.f0[(size_t) a.frameAt(t)]) - 0; ++c; }
        if (c < 10) continue; const double mean = s / c; centreErr += std::abs(100 * (mean - snapMidi((float) notes[n], 0, 1))); ++centres; depthOut += std::sqrt(std::max(0.0, s2 / c - mean * mean)); }
    for (int n = 0; n < 5; ++n) { double s = 0, s2 = 0; int c = 0; for (int i = 0; i < a.frames(); ++i) { const double t = a.frameTime(i); if (t < n * 0.5 + 0.12 || t > n * 0.5 + 0.38 || !a.voiced(i)) continue; const double m = hzMidi(a.f0[(size_t) i]); s += m; s2 += m * m; ++c; }
        if (c > 10) depthIn += std::sqrt(std::max(0.0, s2 / c - (s / c) * (s / c))); }
    check(jumps == 0, "retune 40 ms: no instantaneous >= 1 st jumps", juce::String(jumps));
    check(centres == 5 && centreErr / 5 < 10, "note centres land on the key (C major)", juce::String(centreErr / std::max(1, centres), 1) + " c mean");
    check(depthOut > 0.8 * depthIn && depthOut < 1.25 * depthIn, "FLATTEN 0 keeps the vibrato", "rms depth " + juce::String(depthIn * 100, 1) + " c -> " + juce::String(depthOut * 100, 1) + " c");
    p.flatten = 1; RenderInfo i2; rr(a, x, nullptr, p, &i2); double d2 = 0; for (int n = 0; n < 5; ++n) { double s = 0, s2 = 0; int c = 0; for (size_t j = 0; j < i2.f0.size(); ++j) { const double t = i2.tSrc[j]; if (t < n * 0.5 + 0.15 || t > n * 0.5 + 0.38 || i2.f0[j] <= 0) continue; const double m = hzMidi(i2.f0[j]); s += m; s2 += m * m; ++c; }
        if (c > 10) d2 += std::sqrt(std::max(0.0, s2 / c - (s / c) * (s / c))); }
    check(d2 < 0.2 * depthIn, "FLATTEN 1 makes notes flat", juce::String(d2 * 100, 1) + " c");
    // CONTOUR SNAP glides (retune) instead of stair-stepping
    RenderParams c; c.mode = RenderParams::Contour; c.contourSnap = true; c.retuneMs = 60; c.contour.add(0, 60); c.contour.add(2.5, 67); c.transients = false; RenderInfo i3; rr(a, x, nullptr, c, &i3);
    int cj = 0; for (size_t j = 1; j < i3.f0.size(); ++j) if (i3.f0[j] > 0 && i3.f0[j - 1] > 0 && std::abs(12 * std::log2(i3.f0[j] / i3.f0[j - 1])) >= 0.5) ++cj;
    check(cj == 0, "CONTOUR SNAP with retune glides between notes", juce::String(cj) + " steps >= 0.5 st per frame");
}

// ---------------------------------------------------------------------------------------------------------------- 16 tracking repairs
static void test16Fixes() {
    std::cout << "-- [16] repair the pitch track: octave fix, voicing, forced voicing" << std::endl;
    const int fs = 44100; const auto x = steadyVowel(0, 150, fs, 2.0); const auto a = analysed(x, fs);
    RenderParams p; p.fixes.push_back({ 0.5, 1.0, -12, 0 }); const auto ef = effectiveF0(a, p);
    check(std::abs(cents(ef[(size_t) a.frameAt(0.75)], 75)) < 5 && std::abs(cents(ef[(size_t) a.frameAt(1.5)], 150)) < 5, "octave fix moves only its region");
    const auto bv = breathyVowel(fs, 2.0, 0.25); const auto ab = analysed(bv, fs);
    RenderParams q; q.voicing = 0.8f; int v = 0; for (double f : effectiveF0(ab, q)) v += f > 0; RenderParams r; int w = 0; for (double f : effectiveF0(ab, r)) w += f > 0;
    check(v < w / 2, "VOICING (render-time) changes the voiced frames without analysing again", juce::String(w) + " -> " + juce::String(v));
    RenderParams u; u.fixes.push_back({ 0.5, 1.0, 0, -1 }); int un = 0; const auto eu = effectiveF0(a, u); for (int i = a.frameAt(0.5); i <= a.frameAt(1.0); ++i) un += eu[(size_t) i] > 0;
    check(un == 0, "paint UNVOICED clears a region");
    std::vector<float> g = x; for (int i = (int) (0.8 * fs); i < (int) (1.0 * fs); ++i) g[(size_t) i] = 0.05f * (float) (juce::Random::getSystemRandom().nextFloat() * 2 - 1);
    const auto ag = analysed(g, fs); RenderParams f; f.fixes.push_back({ 0.82, 0.98, 0, 1 }); const auto ev = effectiveF0(ag, f); int vv = 0; for (int i = ag.frameAt(0.83); i <= ag.frameAt(0.97); ++i) vv += ev[(size_t) i] > 0;
    check(vv > 25, "paint VOICED bridges a noisy gap", juce::String(vv) + " frames");
}

// ---------------------------------------------------------------------------------------------------------------- 17 doubles + harmony
static void test17Voices() {
    std::cout << "-- [17] DOUBLE + HARMONY, stereo out" << std::endl;
    const int fs = 44100; const auto x = steadyVowel(1, midiHz(52), fs, 2.0); const auto a = analysed(x, fs);   // E3
    RenderParams p; p.doubles = 4; p.transients = false; const auto y = rr(a, x, nullptr, p);
    check(y.stereo() && corr(y.L, y.R) < 0.9 && finite(y.L) && finite(y.R), "DOUBLE x4 renders a wide stereo result", "L/R corr " + juce::String(corr(y.L, y.R), 2));
    RenderParams h; h.harmony[0] = { 2, 1.0f, 0.0f }; h.root = 0; h.scale = 1; h.transients = false; const auto z = rr(a, x, nullptr, h); const auto m = mixdown(z);
    const double g3 = bandEnergy(m, fs, 0.4, 1.6, midiHz(55) - 3, midiHz(55) + 3), g3in = bandEnergy(x, fs, 0.4, 1.6, midiHz(55) - 3, midiHz(55) + 3);
    check(10 * std::log10(g3 / g3in) > 20, "HARMONY +2 steps in C major on E adds G (a third up)", juce::String(10 * std::log10(g3 / g3in), 1) + " dB more energy at G3");
}

// ---------------------------------------------------------------------------------------------------------------- 18 lanes + region
static void test18LanesRegion() {
    std::cout << "-- [18] FORMANT / PITCH / BREATH lanes, process a region" << std::endl;
    const int fs = 44100; const auto x = steadyVowel(0, 120, fs, 3.0); const auto a = analysed(x, fs);
    RenderParams p; p.formantLane.add(0, 0); p.formantLane.add(1.4, 0); p.formantLane.add(1.5, 4); p.formantLane.add(3, 4); p.transients = false; const auto y = mixdown(rr(a, x, nullptr, p));
    const auto F0 = envFormants(y, fs, 0.4, 1.2), F1 = envFormants(y, fs, 1.8, 2.6);
    check(pct(F1.first, F0.first * std::exp2(4 / 12.0)) < 8, "FORMANT lane moves F1 by +4 st in its second half", juce::String(F0.first, 0) + " -> " + juce::String(F1.first, 0));
    RenderParams q; q.pitchLane.add(0, 0); q.pitchLane.add(1.4, 0); q.pitchLane.add(1.5, 7); q.pitchLane.add(3, 7); q.transients = false; const auto z = mixdown(rr(a, x, nullptr, q));
    check(std::abs(cents(harmonicF0(z, fs, 120, 0.4, 1.2), 120)) < 5 && std::abs(cents(harmonicF0(z, fs, 120 * std::exp2(7 / 12.0), 1.8, 2.6), 120 * std::exp2(7 / 12.0))) < 5, "PITCH lane: 0 then +7 st");
    RenderParams r; r.regionStart = 1.0; r.regionEnd = 2.0; r.pitchSemis = 5; r.transients = false; RenderInfo info; const auto w = rr(a, x, nullptr, r, &info);
    double outside = 0; for (int i = 0; i < (int) (0.98 * fs); ++i) outside = std::max(outside, (double) std::abs(w.L[(size_t) i] - x[(size_t) i]));
    for (int i = (int) (2.02 * fs); i < (int) x.size() - 10; ++i) outside = std::max(outside, (double) std::abs(w.L[(size_t) i] - x[(size_t) i]));
    const double inside = harmonicF0(w.L, fs, 120 * std::exp2(5 / 12.0), 1.2, 1.8);
    check(outside < 1e-4 && std::abs(cents(inside, 120 * std::exp2(5 / 12.0))) < 5 && w.L.size() == x.size(), "region: outside untouched, inside +5 st (10 ms crossfades)", "max diff outside " + juce::String(outside, 6));
}

// ---------------------------------------------------------------------------------------------------------------- 19 bounce + set
static void test19BounceSet() {
    std::cout << "-- [19] RESULT -> A, RENDER SET" << std::endl;
    const int fs = 44100; VoiceHost h; h.autoRender = false; h.setAudio(SlotA, steadyVowel(0, 120, fs, 1.0), fs, "voc", false);
    auto p = h.params(); p.pitchSemis = 5; h.setParams(p); juce::String e; h.renderNow(e); const auto before = h.source(SlotA);
    h.useResultAs(SlotA, false); const double f = h.source(SlotA)->ready() ? h.source(SlotA)->analysis()->medianF0() : 0;
    check(h.source(SlotA) != before && std::abs(cents(f, 120 * std::exp2(5 / 12.0))) < 10, "RESULT -> A: the bounce is the new A", juce::String(f, 1) + " Hz");
    check(h.undo() && h.source(SlotA) == before, "bounce is undoable");
    h.renderSet({ -2, 0, 3 }, false); h.waitIdle(60000); const auto dir = h.lastSetFolder();
    const auto files = dir.findChildFiles(juce::File::findFiles, false, "*.wav"); int okPitch = 0;
    for (auto& ff : files) { std::vector<std::vector<float>> ch; int r = 0, b = 0; juce::String er, nt; VoiceHost::decodeFile(ff, ch, r, b, er, nt); const int st = ff.getFileName().fromFirstOccurrenceOf("voc ", false, false).getIntValue();
        if (!ch.empty() && std::abs(cents(harmonicF0(ch[0], r, 120 * std::exp2((5 + st) / 12.0), 0.3, 0.8), 120 * std::exp2((5 + st) / 12.0))) < 10) ++okPitch; }
    check(files.size() == 3 && okPitch == 3, "RENDER SET writes one file per pitch, each at its pitch", juce::String(files.size()) + " files, " + juce::String(okPitch) + " in tune");
    dir.deleteRecursively();
}

// ---------------------------------------------------------------------------------------------------------------- 20 A/B + loop
static void test20Compare() {
    std::cout << "-- [20] level-matched A/B at the same position, loop" << std::endl;
    const int fs = 44100; VoiceHost h; h.autoRender = false; auto x = steadyVowel(0, 120, fs, 3.0); for (auto& v : x) v *= 0.1f;   // quiet source, normalised result
    h.setAudio(SlotA, x, fs, "q", false); auto p = h.params(); p.speed = 0.5f; h.setParams(p); juce::String e; h.renderNow(e);
    h.prepare(48000, 512); juce::AudioBuffer<float> b(2, 512);
    h.play(PlayResult, 2.0); double eR = 0; for (int i = 0; i < 40; ++i) { b.clear(); h.process(b); if (i > 4) eR += b.getRMSLevel(0, 0, 512); }
    const double atR = h.playPos.load(); h.toggleAB(); double eA = 0; for (int i = 0; i < 40; ++i) { b.clear(); h.process(b); if (i > 4) eA += b.getRMSLevel(0, 0, 512); }
    const double atA = h.playPos.load() - 35 * 512 / 48000.0;
    check(h.playing.load() == PlayA && std::abs(atA - h.result()->info.outputToSource(atR)) < 0.15, "A/B switches to A at the same source position", juce::String(atR, 2) + " s out -> " + juce::String(atA, 2) + " s source");
    check(std::abs(db(eA / eR)) < 2.0, "A/B is loudness matched", juce::String(db(eA / eR), 2) + " dB");
    h.setLoop(true, 1.0, 1.5); h.play(PlayA, 1.0); double lo = 9, hi = -9; for (int i = 0; i < 400; ++i) { b.clear(); h.process(b); const double pp = h.playPos.load(); if (i > 2) { lo = std::min(lo, pp); hi = std::max(hi, pp); } }
    check(lo >= 0.98 && hi <= 1.52, "LOOP keeps the preview inside the selection", juce::String(lo, 2) + " .. " + juce::String(hi, 2));
}

// ---------------------------------------------------------------------------------------------------------------- 21 licence
static void test21Licence() {
    std::cout << "-- [21] licence notice in the product + the installer" << std::endl;
    const juce::String n = thirdPartyNotice(); check(n.contains("Copyright (c) 2010  M. Morise") && n.contains("Redistributions in binary form"), "notice text present");
    VoiceHost h; VoicePage page(h); page.setSize(1200, 790); check(page.aboutText() == n, "the page's i / ABOUT panel shows it");
    const auto repo = juce::File(__FILE__).getParentDirectory().getParentDirectory().getParentDirectory();
    check(repo.getChildFile("packaging/third_party/WORLD-LICENSE.txt").loadFileAsString().contains("M. Morise"), "packaging/third_party/WORLD-LICENSE.txt ships with the installer sources");
}

// ---------------------------------------------------------------------------------------------------------------- minors
static void testMinors() {
    std::cout << "-- [22-42] minors" << std::endl;
    const int fs = 44100;
    { RenderParams p; juce::ValueTree t = VoiceHost::paramsToTree(p); t.setProperty("contour", "1:60;nan:62;0.5:61;inf:1;0.2:nan;", nullptr); const auto q = VoiceHost::paramsFromTree(t);
      bool sorted = std::is_sorted(q.contour.pts.begin(), q.contour.pts.end(), [](const Pt& a, const Pt& b) { return a.t < b.t; });
      Curve big; juce::String s; for (int i = 0; i < 30000; ++i) s << i * 0.001 << ":60;"; big.load(s);
      check(sorted && q.contour.pts.size() == 2 && (int) big.pts.size() <= Curve::kMaxPoints, "[22] NaN curve points dropped, sorted, capped"); }
    { VoiceHost h1, h2; h1.setUiPrefs("dark=1"); VoicePage p1(h1), p2(h2); p1.setSize(1200, 790); p2.setSize(1200, 790);
      auto i1 = p1.createComponentSnapshot(p1.getLocalBounds()), i2 = p2.createComponentSnapshot(p2.getLocalBounds()), i1b = p1.createComponentSnapshot(p1.getLocalBounds());
      check(i1.getPixelAt(600, 785).getBrightness() < 0.3f && i2.getPixelAt(600, 785).getBrightness() > 0.8f && i1b.getPixelAt(600, 785).getBrightness() < 0.3f && p1.isDark() && !p2.isDark(), "[23] theme is per page (two instances don't recolour each other)"); }
    { VoiceHost h; h.autoRender = false; h.setAudio(SlotA, steadyVowel(0, 120, fs, 2.0), fs, "a", false); juce::String e; h.renderNow(e); h.prepare(44100, 256); juce::AudioBuffer<float> b(2, 256);
      h.play(PlayResult, 0.5); for (int i = 0; i < 20; ++i) { b.clear(); h.process(b); } h.stop(); b.clear(); h.process(b);
      float maxStep = 0; for (int i = 1; i < 256; ++i) maxStep = std::max(maxStep, std::abs(b.getSample(0, i) - b.getSample(0, i - 1))); b.clear(); h.process(b); float after = b.getMagnitude(0, 256);
      check(maxStep < 0.05f && after < 1e-6f, "[24] STOP fades out (no click), then silence", "max step " + juce::String(maxStep, 4)); }
    { const auto x = steadyVowel(0, 120, fs, 2.0); const auto a = analysed(x, fs); RenderParams p; p.mode = RenderParams::VowelMorph; p.lane.add(0, 0); p.lane.add(2, 4); p.speed = 0.25f; p.transients = false; RenderInfo info; rr(a, x, nullptr, p, &info);
      // the lane is smooth, so with interpolated frames F2's frame-to-frame movement has no stair steps every 4th frame
      double big = 0; for (size_t j = 41; j + 1 < info.formants.size() - 40; ++j) big = std::max(big, (double) std::abs(info.formants[j + 1][1].hz - info.formants[j][1].hz));
      check(big < 40, "[25] SPEED 0.25 interpolates the envelope between frames", "max F2 step " + juce::String(big, 1) + " Hz per frame"); }
    { const auto A = synthSaw(110, fs, 0.6), B = steadyVowel(0, 130, fs, 3.0); const auto aA = analysed(A, fs), aB = analysed(B, fs); RenderParams p; p.mode = RenderParams::Borrow; p.timeMode = RenderParams::FollowBLoop; p.carrier = RenderParams::CarrierResynth; p.transients = false;
      const auto y = rr(aA, A, &aB, p); double worst = 0; for (int i = (int) (0.2 * fs); i < (int) (2.8 * fs) - 1; ++i) worst = std::max(worst, (double) std::abs(y.L[(size_t) i + 1] - y.L[(size_t) i]));
      double typical = 0; for (int i = (int) (0.2 * fs); i < (int) (0.5 * fs); ++i) typical = std::max(typical, (double) std::abs(y.L[(size_t) i + 1] - y.L[(size_t) i]));
      check(worst < 1.6 * typical, "[26] A looping under B has no wrap clicks", "max step " + juce::String(worst, 3) + " vs " + juce::String(typical, 3)); }
    { const auto A = synthSaw(110, 48000, 1.5); const auto B = steadyVowel(0, 130, 22050, 1.5); const auto aA = analysed(A, 48000), aB = analysed(B, 22050);
      RenderParams p; p.mode = RenderParams::Borrow; p.carrier = RenderParams::CarrierResynth; p.transients = false; const auto y = rr(aA, A, &aB, p);
      const double hf = 10 * std::log10(bandEnergy(y.L, 48000, 0.3, 1.2, 12500, 20000) / bandEnergy(y.L, 48000, 0.3, 1.2, 100, 5000));
      check(hf < -40, "[27] B at 22 kHz: nothing flat above its Nyquist", juce::String(hf, 1) + " dB"); }
    { VoiceHost h; std::vector<float> x((size_t) (fs * 0.04), 0.3f); juce::String e; const bool ok = h.setAudio(SlotA, x, fs, "blip", false, &e);
      check(!ok && e.contains("too short") && h.status().contains("too short"), "[28] a 40 ms file says 'too short (min 50 ms)'", e); }
    { auto x = steadyVowel(0, 120, fs, 1.5); for (auto& v : x) v *= 0.003f; const auto a = analysed(x, fs); check(a.voicedFraction() > 0.85f, "[29] a quiet take (-50 dBFS) is still voiced (relative gate)", juce::String(a.voicedFraction(), 2)); }
    { VoiceHost h; h.autoRender = false; h.setAudio(SlotA, steadyVowel(0, 120, fs, 1.0), fs, "a", false); const auto s0 = h.source(SlotA);
      h.beginGesture(); auto p = h.params(); p.pitchSemis = 3; h.setParams(p); h.setAudio(SlotA, steadyVowel(1, 120, fs, 1.0), fs, "b", false); p.pitchSemis = 4; h.setParams(p); h.endGesture();
      h.undo(); check(h.source(SlotA) == s0 && std::abs(h.params().pitchSemis - 3) < 1e-6f, "[30] a sound loaded during a drag is its own undo step"); }
    { VoiceHost h; h.autoRender = false; for (int i = 0; i < 10; ++i) { auto p = h.params(); p.pitchSemis = (float) i; h.setParams(p, true, "PITCH"); }
      h.undo(); check(std::abs(h.params().pitchSemis) < 1e-6f && !h.canUndo(), "[31] wheel ticks on one control within 400 ms are one undo step"); }
    { auto old = VoiceHost::exportDir().getChildFile("old-test.wav"); old.replaceWithText("x"); old.setLastModificationTime(juce::Time::getCurrentTime() - juce::RelativeTime::days(2));
      auto fresh = VoiceHost::exportDir().getChildFile("fresh-test.wav"); fresh.replaceWithText("x"); VoiceHost::cleanExportDir();
      check(!old.exists() && fresh.exists(), "[32] temp WAVs older than a day are cleaned up"); fresh.deleteFile();
      VoiceHost h; h.autoRender = false; h.setAudio(SlotA, steadyVowel(0, 120, fs, 1.0), fs, "nm", false); auto p = h.params(); p.pitchSemis = 5; p.formantSemis = 2; p.speed = 0.5f; h.setParams(p);
      juce::String why; auto f = h.writeResultWav(why); check(f.getFileName().contains("+5st") && f.getFileName().contains("F+2") && f.getFileName().contains("x0.50"), "[32] drag-out names carry mode / pitch / formant / speed", f.getFileName()); f.deleteFile(); }
    { auto f = juce::File::createTempFile(".mp3"); f.replaceWithText("not an mp3"); std::vector<std::vector<float>> ch; int r, b; juce::String e, n; const bool ok = VoiceHost::decodeFile(f, ch, r, b, e, n);
      check(!ok && e.contains("MP3"), "[33] an undecodable MP3 gives a clear message", e); f.deleteFile(); }
    { VoiceHost h; h.autoRender = false; std::vector<std::vector<float>> c(1, steadyVowel(0, 120, fs, 2.0)); h.setAudio(SlotA, c, fs, "16", false, nullptr, 16); const auto s16 = h.toTree().toXmlString().length();
      h.setAudio(SlotA, c, fs, "24", false, nullptr, 24); const auto s24 = h.toTree().toXmlString().length(); check(s16 < s24 * 0.85, "[35] 16-bit sources are saved as 16-bit", juce::String(s16 / 1024) + " KB vs " + juce::String(s24 / 1024) + " KB"); }
    { VoiceHost h; h.autoRender = false; h.setAudio(SlotA, synthSaw(110, fs, 1.0), fs, "saw", false); VoicePage page(h); page.setSize(1200, 790); page.setMode(RenderParams::Contour); page.refreshNow();
      check(page.boundsOf("scale").getWidth() >= 100 && page.hintFits(), "[36] CONTOUR controls have room (scale box " + juce::String(page.boundsOf("scale").getWidth()) + " px, hint fits)");
      page.inspectAt(0.5); check(page.formantLabel().contains("ENVELOPE"), "[38] non-voice A (a saw) shows 'envelope peaks', not formants", page.formantLabel() + ", contrast " + juce::String(envelopeContrast(*h.analysis(SlotA)), 1) + " dB");
      VoiceHost hv; hv.autoRender = false; hv.setAudio(SlotA, steadyVowel(0, 120, fs, 1.0), fs, "v", false); VoicePage pv(hv); pv.setSize(1200, 790); pv.refreshNow();
      check(pv.formantLabel() == "FORMANTS", "[38] a voice shows FORMANTS", "contrast " + juce::String(envelopeContrast(*hv.analysis(SlotA)), 1) + " dB");
      const double ms = page.wavePaintMs(30); check(ms < 6.0, "[39] waveform view paints from a cached layer", juce::String(ms, 2) + " ms per paint"); }
    { VoiceHost h; h.autoRender = false; auto big = juce::File::createTempFile(".wav"); std::vector<float> x((size_t) (fs * 120), 0.1f); VoiceHost::writeWav(big, x, fs);
      h.loadFileAsync(SlotA, big); h.cancel(); h.waitIdle(30000); check(h.source(SlotA) == nullptr, "[40] CANCEL during the decode stops the load (no analysis after it)"); big.deleteFile(); }
    { VoiceHost h; h.autoRender = false; h.setAudio(SlotB, std::vector<float>((size_t) fs, 0.0f), fs, "silent", false); h.setAudio(SlotA, steadyVowel(0, 120, fs, 1.0), fs, "a", false);
      auto p = h.params(); p.mode = RenderParams::Borrow; h.setParams(p); juce::String e; h.renderNow(e); const bool had = h.status().isNotEmpty(); p.mode = RenderParams::Pitch; h.setParams(p);
      check(had && h.status().isEmpty(), "[41] status messages clear on a mode change"); }
    { check(std::abs(vowelTable(VowelsFemale)[0].F[0] - 850) < 1 && std::abs(vowelTable(VowelsChild)[2].F[1] - 3200) < 1, "[42] female / child vowel sets");
      VoiceSpec v; v.seconds = 3; v.f0 = [](double) { return 220.0; }; v.F = [](double t) { auto f = vowelF((int) (t / 0.6) % 5); for (auto& q : f) q *= 1.25; return f; };
      const auto x = synthVoice(v, fs); const auto a = analysed(x, fs); const float s = fitVowelScale(a);
      check(std::abs(s - 1.25f) < 0.12f, "[42] AUTO SIZE fits a smaller voice (x1.25)", juce::String(s, 2));
      RenderParams p; p.mode = RenderParams::VowelMorph; p.hasCustomVowel = true; p.customF = { 400, 2000, 2600, 3500 }; p.lane.add(0, 5); p.transients = false;
      const auto y = mixdown(rr(analysed(steadyVowel(0, 120, fs, 1.5), fs), steadyVowel(0, 120, fs, 1.5), nullptr, p)); const auto F = envFormants(y, fs, 0.3, 1.2);
      check(pct(F.first, 400) < 10 && pct(F.second, 2000) < 8, "[42] a grabbed custom vowel sits on lane position 5", juce::String(F.first, 0) + "/" + juce::String(F.second, 0)); }
}

// ---------------------------------------------------------------------------------------------------------------- UI items 12, 14, 37
static void testPageControls() {
    std::cout << "-- [12][14] knobs snap to semitones; time zoom / scroll" << std::endl;
    const int fs = 44100; VoiceHost h; h.autoRender = false; h.setAudio(SlotA, steadyVowel(0, 120, fs, 20.0), fs, "long", false);
    VoicePage page(h); page.setSize(1200, 790); page.refreshNow();
    page.setKnob("PITCH", 4.6, false); check(std::abs(h.params().pitchSemis - 5.0f) < 1e-6f, "[12] PITCH snaps to whole semitones", juce::String(h.params().pitchSemis, 2));
    page.setKnob("PITCH", 4.63, true); check(std::abs(h.params().pitchSemis - 4.63f) < 1e-3f, "[12] fine (Shift / Alt) keeps cents", juce::String(h.params().pitchSemis, 2));
    page.setKnob("FORMANT", -2.4, false); check(std::abs(h.params().formantSemis + 2.0f) < 1e-6f, "[12] FORMANT snaps too");
    check(page.knobText("PITCH").contains("st"), "[12] knob shows the interval + note", page.knobText("PITCH"));
    const auto all = page.visibleTime(); page.zoomTime(0.1, 10.0); const auto z = page.visibleTime();
    check(z.getLength() < all.getLength() * 0.2 && z.contains(10.0), "[14] time zoom around the cursor", juce::String(all.getLength(), 1) + " s -> " + juce::String(z.getLength(), 2) + " s");
    page.scrollTime(5.0); const auto s = page.visibleTime(); check(s.getStart() > z.getStart() + 4.9, "[14] time scroll", juce::String(s.getStart(), 2));
    page.zoomTime(100, 10); check(std::abs(page.visibleTime().getLength() - all.getLength()) < 0.01, "[14] zoom out to the whole sound");
}

// ---------------------------------------------------------------------------------------------------------------- host hooks
static void testHostHooks() {
    std::cout << "-- host hooks: demo export gate, tooltips, theme button" << std::endl;
    const int fs = 44100; VoiceHost h; h.autoRender = false; h.setAudio(SlotA, steadyVowel(0, 120, fs, 1.0), fs, "gate", false); juce::String e; h.renderNow(e);
    VoicePage page(h); page.setSize(1200, 790); page.refreshNow();
    page.allowExport = [](juce::Component*, const char*) { return false; };
    const auto w0 = page.exportForTest(0); const auto msg = page.lastMessage(); const auto s0 = page.exportForTest(2);
    check(w0.isEmpty() && s0.isEmpty() && msg.contains("full version"), "DEMO GATE false: drag WAV + RENDER SET refused", msg);
    check(page.exportForTest(1).isEmpty(), "DEMO GATE false: drag SET refused");
    page.allowExport = [](juce::Component*, const char*) { return true; };
    const auto w1 = page.exportForTest(0); check(w1.size() == 1 && juce::File(w1[0]).existsAsFile(), "DEMO GATE true: drag WAV works", w1.isEmpty() ? juce::String() : juce::File(w1[0]).getFileName());
    for (auto& f : w1) juce::File(f).deleteFile();
    check(page.exportForTest(2).size() == 1 && h.waitIdle(60000) && h.lastSetFolder().isDirectory(), "DEMO GATE true: RENDER SET works"); h.lastSetFolder().deleteRecursively();
    page.allowExport = nullptr; check(page.exportForTest(0).isEmpty(), "no gate wired = refused (fail closed)");
    juce::Component parent; parent.setSize(1200, 790);
    { VoicePage p1(h); parent.addAndMakeVisible(p1); check(p1.hasOwnTooltipWindow(), "default: the page shows its own tooltips"); parent.removeChildComponent(&p1); }
    { VoicePage p2(h); p2.setOwnsTooltips(false); parent.addAndMakeVisible(p2); int tw = 0; for (auto* c : p2.getChildren()) tw += dynamic_cast<juce::TooltipWindow*>(c) != nullptr;
      check(!p2.hasOwnTooltipWindow() && tw == 0, "ownsTooltips = false: no TooltipWindow of its own (no double tips)"); parent.removeChildComponent(&p2); }
    { VoicePage p3(h); p3.setSize(1200, 790); check(p3.themeButtonVisible(), "theme button visible by default"); p3.setThemeButtonVisible(false); p3.setDark(true);
      check(!p3.themeButtonVisible() && p3.isDark(), "host-driven theme: button hidden, setDark still works"); }
}

// ---------------------------------------------------------------------------------------------------------------- START / END / LOOP
static juce::MouseEvent wrEv(juce::Component& c, float x, float y, bool right, float dx = 0) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), { x + dx, y }, right ? juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier) : juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
                            0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, juce::Time::getCurrentTime(), { x, y }, juce::Time::getCurrentTime(), 1, std::abs(dx) > 0.0f);
}
static void wrGesture(ghosty::WaveRegion& w, float x0, float x1, float y, bool right) {
    w.mouseDown(wrEv(w, x0, y, right)); if (std::abs(x1 - x0) > 0.0f) w.mouseDrag(wrEv(w, x0, y, right, x1 - x0)); w.mouseUp(wrEv(w, x0, y, right, x1 - x0));
}
static void testSourceRegion() {
    std::cout << "-- START / END / LOOP on the source (WaveRegion)" << std::endl;
    const int fs = 44100; const auto x = steadyVowel(0, 120, fs, 3.0); const auto a = analysed(x, fs);
    // old sessions: an untouched region is the old path, bit for bit
    { RenderParams p; p.pitchSemis = 3; const auto y0 = rr(a, x, nullptr, p); RenderParams q = p; q.trimStart = 0; q.trimEnd = a.seconds(); const auto y1 = rr(a, x, nullptr, q);
      RenderParams r = p; r.trimStart = 0; r.trimEnd = -1; const auto y2 = rr(a, x, nullptr, r);
      check(y0.L == y1.L && y0.L == y2.L, "untouched START / END (0..end) renders bit-identically to the old path");
      const auto t = VoiceHost::paramsToTree(RenderParams()); check(!t.hasProperty("trim0") && !t.hasProperty("trim1"), "untouched params save exactly as before (no new attributes)");
      juce::ValueTree old("PARAMS"); old.setProperty("pitch", 3.0, nullptr); const auto po = VoiceHost::paramsFromTree(old); check(po.trimStart < 0 && po.trimEnd < 0, "an old session loads with the whole file");
      VoiceHost h; h.fromTree(juce::ValueTree("VOICE")); check(!h.loopEnabled() && h.loopRange().first < 0, "an old session loads with no loop"); }
    // trim: only START..END is processed, everything else keeps its source-time meaning
    { RenderParams p; p.trimStart = 0.5; p.trimEnd = 1.5; p.pitchSemis = 5; p.transients = false; RenderInfo info; const auto y = rr(a, x, nullptr, p, &info);
      const double want = 120 * std::exp2(5 / 12.0); const double f = harmonicF0(y.L, fs, want, 0.2, 0.8);
      check(std::abs((double) y.L.size() / fs - 1.0) < 0.01 && std::abs(cents(f, want)) < 5 && std::abs(info.tSrc.front() - 0.5) < 0.006, "trim 0.5..1.5 s: a 1 s result, +5 st, source times start at 0.5 s",
            juce::String((double) y.L.size() / fs, 3) + " s, " + juce::String(cents(f, want), 2) + " c, tSrc0 " + juce::String(info.tSrc.front(), 3));
      RenderParams c; c.mode = RenderParams::Contour; c.contour.add(0, 57); c.contour.add(0.99, 57); c.contour.add(1.0, 64); c.contour.add(3, 64); c.trimStart = 0.5; c.trimEnd = 1.5; c.transients = false;
      RenderInfo ci; const auto z = rr(a, x, nullptr, c, &ci); const double lo = harmonicF0(z.L, fs, midiHz(57), 0.1, 0.45), hi = harmonicF0(z.L, fs, midiHz(64), 0.55, 0.9);
      check(std::abs(cents(lo, midiHz(57))) < 10 && std::abs(cents(hi, midiHz(64))) < 10, "a contour drawn in source time stays on its syllables inside the trim");
      RenderParams g; g.trimStart = 0.5; g.trimEnd = 2.5; g.regionStart = 1.0; g.regionEnd = 1.5; g.pitchSemis = 4; g.transients = false; const auto w = rr(a, x, nullptr, g);
      double outside = 0; for (int i = 0; i < (int) (0.48 * fs); ++i) outside = std::max(outside, (double) std::abs(w.L[(size_t) i] - x[(size_t) (i + (int) std::lround(0.5 * fs))]));
      check(outside < 1e-4 && std::abs((double) w.L.size() / fs - 2.0) < 0.01, "region inside a trim: the untouched part is A's audio from START", "max diff " + juce::String(outside, 6)); }
    // the page: gestures drive START / END / LOOP + the export gate
    { VoiceHost h; h.autoRender = false; h.setAudio(SlotA, x, fs, "trim me", false); juce::String e; h.renderNow(e);
      VoicePage page(h); page.setSize(1200, 790); page.refreshNow(); auto* w = dynamic_cast<ghosty::WaveRegion*>(&page.sourceWave()); check(w != nullptr && w->hasWave(), "the source waveform is a WaveRegion showing A");
      if (w == nullptr) return;
      const float y = w->waveArea().getCentreY() + 12, T = 3.0f; auto X = [&](float f) { return w->xOf(f); };
      wrGesture(*w, X(0.4f), X(0.4f), y, false); check(h.params().trimStart < 0 && !h.loopEnabled(), "a plain click only auditions (nothing moves)");
      wrGesture(*w, X(0.25f), X(0.25f), y, true); check(std::abs(h.params().trimStart - 0.25 * T) < 0.03, "right-click = START here", juce::String(h.params().trimStart, 3) + " s");
      wrGesture(*w, X(0.5f), X(0.75f), y, true); const auto lp = h.loopRange();
      check(h.loopEnabled() && std::abs(lp.first - 1.5) < 0.03 && std::abs(lp.second - 2.25) < 0.03, "right-drag = LOOP over the span (ON)", juce::String(lp.first, 2) + " .. " + juce::String(lp.second, 2));
      const auto f1 = w->handleFlag(1).getCentre(); wrGesture(*w, f1.x, X(0.9f) - (X(1.0f) - f1.x), f1.y, false);
      check(std::abs(h.params().trimEnd - 0.9 * T) < 0.05, "drag the END flag", juce::String(h.params().trimEnd, 3) + " s");
      const auto pill = w->loopPill().getCentre(); wrGesture(*w, pill.x, pill.x, pill.y, false); check(!h.loopEnabled(), "LOOP pill turns the loop off");
      wrGesture(*w, pill.x, pill.x, pill.y, false); check(h.loopEnabled(), "... and on again");
      juce::String renderErr; h.renderNow(renderErr); check(std::abs(h.result()->seconds() - (0.9 - 0.25) * T) < 0.02, "the render follows START / END", juce::String(h.result()->seconds(), 3) + " s");
      juce::StringArray dragged; w->performDrag = [&](const juce::StringArray& f) { dragged = f; };
      page.allowExport = [](juce::Component*, const char*) { return false; }; const auto g = w->gripRect().getCentre(); wrGesture(*w, g.x, g.x + 30, g.y, false);
      check(dragged.isEmpty() && page.lastMessage().contains("full version"), "WAV grip refused by the demo gate", page.lastMessage());
      page.allowExport = [](juce::Component*, const char*) { return true; }; wrGesture(*w, g.x, g.x + 30, g.y, false);
      std::vector<std::vector<float>> ch; int rr2 = 0, bb = 0; juce::String er, nt; const bool ok = dragged.size() == 1 && VoiceHost::decodeFile(juce::File(dragged[0]), ch, rr2, bb, er, nt);
      check(ok && std::abs((double) ch[0].size() / rr2 - 0.75) < 0.02, "WAV grip drags out the LOOP part (0.75 s)", ok ? juce::String((double) ch[0].size() / rr2, 3) + " s" : er);
      if (dragged.size() == 1) juce::File(dragged[0]).deleteFile();
      const auto xml = h.toTree().toXmlString(); VoiceHost g2; g2.autoRender = false; g2.fromTree(juce::ValueTree::fromXml(xml)); g2.waitIdle(30000);
      check(std::abs(g2.params().trimStart - h.params().trimStart) < 1e-4 && std::abs(g2.params().trimEnd - h.params().trimEnd) < 1e-4 && g2.loopEnabled() && std::abs(g2.loopRange().first - lp.first) < 1e-6, "START / END / LOOP are saved + restored");
      check(h.undo() && std::abs(h.params().trimEnd - 3.0) < 0.01 && std::abs(h.params().trimStart - 0.75) < 0.03, "each START / END gesture is one undo step (undo = END back, START kept)", juce::String(h.params().trimStart, 3) + " .. " + juce::String(h.params().trimEnd, 3)); }
}

// ---------------------------------------------------------------------------------------------------------------- UI
static void prepUiHost(VoiceHost& h, int mode) {
    const int fs = 44100; h.autoRender = false;
    VoiceSpec v; v.seconds = 3.0; v.f0 = [](double t) { return 125.0 * std::exp2((-0.25 * t + 0.08 * std::sin(2 * kPi * 0.7 * t))); };
    v.F = [](double t) { const double pos = std::fmod(t * 1.6, 5.0); const int i = (int) pos; const double f = pos - i; const auto a = vowelF(i), b = vowelF((i + 1) % 5);
                         std::array<double, 4> o; for (int k = 0; k < 4; ++k) o[(size_t) k] = a[(size_t) k] * std::pow(b[(size_t) k] / a[(size_t) k], juce::jlimit(0.0, 1.0, (f - 0.6) / 0.4)); return o; };
    if (mode == RenderParams::Borrow) { std::vector<float> L, R; supersawStereo(fs, 3.0, L, R); h.setAudio(SlotA, { L, R }, fs, "supersaw pad", false); h.setAudio(SlotB, synthVoice(v, fs), fs, "spoken vowels", false); }
    else h.setAudio(SlotA, synthVoice(v, fs), fs, "spoken vowels", false);
    auto p = h.params(); p.mode = mode;
    if (mode == RenderParams::Pitch) { p.pitchSemis = 5; p.doubles = 3; }
    if (mode == RenderParams::Contour) { const float m[] = { 57, 60, 64, 62, 60, 59, 57 }; for (int i = 0; i < 7; ++i) { p.contour.add(i * 0.43, m[i]); p.contour.add(i * 0.43 + 0.33, m[i]); } p.contourSnap = true; p.root = 9; p.scale = 2;
                                         p.trimStart = 0.2; p.trimEnd = 2.7; h.setLoop(true, 0.9, 1.6); }
    if (mode == RenderParams::VowelMorph) { for (int i = 0; i < 5; ++i) { p.lane.add(i * 0.6 + 0.1, (float) i); p.lane.add(i * 0.6 + 0.45, (float) i); } }
    if (mode == RenderParams::Tune) { p.root = 9; p.scale = 2; p.retuneMs = 30; p.harmony[0] = { 2, 0.6f, -0.5f }; }
    h.setParams(p); juce::String err; h.renderNow(err);
}
static void testUi(const juce::File& shots) {
    std::cout << "-- page" << std::endl;
    static const char* N[] = { "pitch", "borrow", "contour", "vowel", "tune" };
    for (int mode = 0; mode < 5; ++mode) for (int dark = 0; dark < 2; ++dark) {
        VoiceHost h; prepUiHost(h, mode); h.setUiPrefs(dark ? "dark=1" : "");
        VoicePage page(h); page.setSize(1200, 790); page.refreshNow(); page.inspectAt(1.2);
        if (mode == RenderParams::Contour) page.selectForTest(0.9, 1.6);
        auto img = page.createComponentSnapshot(page.getLocalBounds(), true, 1.0f);
        bool nonBlank = false; const auto c0 = img.getPixelAt(5, 5); for (int y = 60; y < 700 && !nonBlank; y += 7) for (int x = 300; x < 1100; x += 7) if (img.getPixelAt(x, y) != c0) { nonBlank = true; break; }
        check(img.isValid() && nonBlank && page.isDark() == (dark == 1), juce::String("page paints: ") + N[mode] + (dark ? " dark" : " light"));
        if (shots != juce::File()) { shots.createDirectory(); auto f = shots.getChildFile(juce::String("voice_") + N[mode] + (dark ? "_dark" : "_light") + ".png"); f.deleteFile(); juce::FileOutputStream os(f); juce::PNGImageFormat png; png.writeImageToStream(img, os); }
    }
    { VoiceHost h; VoicePage page(h); page.setSize(1200, 790); auto img = page.createComponentSnapshot(page.getLocalBounds(), true, 1.0f); check(img.isValid(), "empty page paints");
      if (shots != juce::File()) { auto f = shots.getChildFile("voice_empty_light.png"); f.deleteFile(); juce::FileOutputStream os(f); juce::PNGImageFormat png; png.writeImageToStream(img, os); }
      page.showAbout(true); auto ab = page.createComponentSnapshot(page.getLocalBounds(), true, 1.0f);
      if (shots != juce::File()) { auto f = shots.getChildFile("voice_about.png"); f.deleteFile(); juce::FileOutputStream os(f); juce::PNGImageFormat png; png.writeImageToStream(ab, os); } }
}

// ---------------------------------------------------------------------------------------------------------------- demos
static void writeDemo(const juce::File& dir, const juce::String& name, const Rendered& x) {
    dir.createDirectory(); auto f = dir.getChildFile(name + ".wav"); VoiceHost::writeWav(f, x); std::cout << "  wrote " << f.getFullPathName() << std::endl;
}
static void writeDemo(const juce::File& dir, const juce::String& name, const std::vector<float>& x, int fs) { Rendered r; r.L = x; r.fs = fs; writeDemo(dir, name, r); }
static void demos(const juce::File& dir) {
    const int fs = 44100; double secs = 0;
    const auto speech = speechLike(fs, secs);
    const auto aV = analysed(speech, fs);
    { // a supersaw chord pad speaking (VOCODE carrier keeps the chord + stereo width)
      std::vector<float> L((size_t) (secs * fs)), R(L.size()); const double ch[4] = { midiHz(45), midiHz(52), midiHz(57), midiHz(60) }; const double det[5] = { -0.12, -0.05, 0, 0.06, 0.13 }; double ph[4][5] {};
      for (size_t i = 0; i < L.size(); ++i) { double l = 0, r = 0; for (int c = 0; c < 4; ++c) for (int k = 0; k < 5; ++k) { ph[c][k] += midiHz(hzMidi(ch[c]) + det[k]) / fs; if (ph[c][k] >= 1) ph[c][k] -= 1;
            const double v = 2 * ph[c][k] - 1; const double pan = (k - 2) / 2.0; l += v * (1 - 0.7 * pan); r += v * (1 + 0.7 * pan); }
          const double t = i / (double) fs; const double env = std::min(1.0, std::min(t, secs - t) * 10); L[i] = (float) (0.03 * l * env); R[i] = (float) (0.03 * r * env); }
      std::vector<float> mono(L.size()); for (size_t i = 0; i < L.size(); ++i) mono[i] = 0.5f * (L[i] + R[i]);
      const auto aP = analysed(mono, fs); RenderParams p; p.mode = RenderParams::Borrow; p.dynamics = 0.85f; Rendered y; render(aP, refOf2(L, R, fs), &aV, p, y);
      writeDemo(dir, "01_synth_speaks_vowels", y); writeDemo(dir, "01_source_voice", speech, fs); }
    { // a flat spoken line sung along a drawn, key-snapped melody (A minor) with a glide, plus a 3rd-up harmony
      RenderParams p; p.mode = RenderParams::Contour; p.contourSnap = true; p.root = 9; p.scale = 2; p.retuneMs = 50; p.harmony[0] = { 2, 0.55f, 0.5f };
      const float m[] = { 57, 59.6f, 60.2f, 64, 62.3f, 60, 59, 57.2f, 55.8f, 57 }; const double step = secs / 10.0;
      for (int i = 0; i < 10; ++i) { p.contour.add(i * step + 0.02, m[i]); p.contour.add(i * step + step * 0.8, m[i] + (i == 3 ? 0.4f : 0.0f)); }
      writeDemo(dir, "02_pitch_contoured_phrase", rr(aV, speech, nullptr, p)); }
    { // a pad talking A-E-I-O-U on the vowel lane
      const auto pad = supersawLine(fs, 4.0); const auto aP = analysed(pad, fs); RenderParams p; p.mode = RenderParams::VowelMorph; p.vowelAmount = 1;
      for (int i = 0; i < 5; ++i) { p.lane.add(i * 0.8 + 0.05, (float) i); p.lane.add(i * 0.8 + 0.55, (float) i); } writeDemo(dir, "03_pad_vowel_morph", rr(aP, pad, nullptr, p)); }
    { // +5 st with and without formant keep, back to back
      RenderParams p; p.pitchSemis = 5; auto y = mixdown(rr(aV, speech, nullptr, p)); y.resize(y.size() + fs / 3, 0.0f); p.keepFormants = false; const auto z = mixdown(rr(aV, speech, nullptr, p));
      y.insert(y.end(), z.begin(), z.end()); writeDemo(dir, "04_plus5_keep_vs_chipmunk", y, fs); }
    { // TUNE: an off-key sung line corrected to A minor with vibrato kept, doubled wide
      const double notes[] = { 57.4, 59.7, 60.35, 64.3, 62.2, 59.6 }; VoiceSpec v; v.seconds = 3.6; v.f0 = [&](double t) { const int i = std::min(5, (int) (t / 0.6)); return midiHz(notes[i] + 0.25 * std::sin(2 * kPi * 5.2 * t)); };
      v.F = [](double t) { return vowelF((int) (t / 0.6) % 5); }; const auto x = synthVoice(v, fs); const auto a = analysed(x, fs);
      RenderParams p; p.mode = RenderParams::Tune; p.root = 9; p.scale = 2; p.retuneMs = 35; p.doubles = 3; p.doubleCents = 9;
      writeDemo(dir, "05_tune_off_key_line_doubled", rr(a, x, nullptr, p)); writeDemo(dir, "05_source_off_key_line", x, fs); }
    { // drums through +7 st with TRANSIENTS (attacks stay sharp)
      const auto k = kickPattern(fs, 2.0); const auto a = analysed(k, fs); RenderParams p; p.pitchSemis = 7; writeDemo(dir, "06_drums_plus7_transients", rr(a, k, nullptr, p)); }
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::String cmd = argc > 1 ? argv[1] : "";
    if (cmd == "shots" && argc > 2) { testUi(juce::File(argv[2])); std::cout << passes << " passed, " << fails << " failed" << std::endl; return fails; }
    if (cmd == "demos" && argc > 2) { demos(juce::File(argv[2])); return 0; }
    std::cout << "VOICE tests" << std::endl;
    const bool quick = cmd == "quick";
    const std::vector<std::pair<const char*, void (*)()>> punchTests { { "01", test01DoubleAnalysis }, { "02", test02SourceTime }, { "03", test03Vocode }, { "04", test04Rates }, { "05", test05Memory },
        { "06", test06LongAndFast }, { "08", test08Transients }, { "08a", test08aBreath }, { "09", test09Sub }, { "10", test10Resampler }, { "11", test11Gain }, { "13", test13Stale }, { "15", test15Tune },
        { "16", test16Fixes }, { "17", test17Voices }, { "18", test18LanesRegion }, { "19", test19BounceSet }, { "20", test20Compare }, { "21", test21Licence }, { "minors", testMinors }, { "page", testPageControls }, { "hooks", testHostHooks }, { "region", testSourceRegion }, { "diag", diagBreath } };
    if (cmd == "only") {   // VoiceTests only 08a 15 ...
        for (int i = 2; i < argc; ++i) for (auto& t : punchTests) if (juce::String(argv[i]) == t.first) t.second();
        std::cout << passes << " passed, " << fails << " failed" << std::endl; return fails;
    }
    testCurveState(); testStreamingSynth();
    for (int fs : { 44100, 48000, 96000 }) { if (quick && fs != 44100) continue; testAnalysis(fs); testPitchShift(fs); testBorrow(fs); testContour(fs); testVowel(fs); testGarbage(fs); }
    if (!quick) { testCancel(); testCpu(); for (auto& t : punchTests) if (juce::String(t.first) != "diag") t.second(); }
    testUi({});
    std::cout << passes << " passed, " << fails << " failed" << std::endl;
    return fails;
}
