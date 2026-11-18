// ARCS page test harness: measured checks, one regression test per review item, screenshots and demos.
// usage: ArcsTests [outDir (default: current directory)] [--no-media]
#include "ArcsPage.h"
#include "ArcsFactory.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <thread>
#include <cstdio>
#include <fstream>

using namespace arcs;

static int failures = 0, passes = 0;
static void check(bool ok, const juce::String& what) { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8()); std::fflush(stdout); if (!ok) ++failures; else ++passes; }

// ---------------------------------------------------------------- helpers
static constexpr double SR = 48000.0;
static Arc mkArc(std::vector<Pt> pts, int shape = ShapeSine, float gain = 0.8f) { Arc a; a.pts = std::move(pts); a.setWave(waveShape(shape)); a.gain = gain; return a; }
static int addArc(Page& p, Arc a) { auto* n = p.add(std::move(a)); return n ? n->id : -1; }
static Page secPage(double secs) { Page p; p.tempoSync = false; p.lengthSeconds = secs; p.master = 0.5f; return p; }
static void render(const Page& p, std::vector<float>& L, std::vector<float>& R, double bpm = 120.0, double sr = SR) {
    SnapCache c; auto s = buildSnapshot(p, c); Engine::render(*s, sr, bpm, L, R);
}
static std::vector<float> mono(const std::vector<float>& L, const std::vector<float>& R) { std::vector<float> m(L.size()); for (size_t i = 0; i < L.size(); ++i) m[i] = L[i] + R[i]; return m; }
static std::vector<float> spectrum(const std::vector<float>& x, size_t start, int order) {   // Blackman-Harris magnitude
    const int N = 1 << order; juce::dsp::FFT fft(order); std::vector<float> b((size_t) N * 2, 0.0f);
    for (int i = 0; i < N; ++i) { const double t = 2.0 * juce::MathConstants<double>::pi * i / (N - 1);
        const double w = 0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t);
        b[(size_t) i] = start + (size_t) i < x.size() ? (float) (x[start + (size_t) i] * w) : 0.0f; }
    fft.performFrequencyOnlyForwardTransform(b.data());
    b.resize((size_t) N / 2); return b;
}
static double peakHz(const std::vector<float>& mag, double sr, int N, double lo = 20, double hi = 20000) {
    size_t k = 1; float best = 0;
    for (size_t i = (size_t) (lo * N / sr); i < mag.size() - 1 && i < (size_t) (hi * N / sr); ++i) if (mag[i] > best) { best = mag[i]; k = i; }
    const double a = std::log(mag[k - 1] + 1e-20), b = std::log(mag[k] + 1e-20), c = std::log(mag[k + 1] + 1e-20);
    return ((double) k + 0.5 * (a - c) / (a - 2 * b + c)) * sr / N;
}
static double levelDbAt(const std::vector<float>& mag, double sr, int N, double hz, int halfWidth = 6) {
    const int k = (int) std::round(hz * N / sr); float m = 0; for (int i = juce::jmax(0, k - halfWidth); i <= juce::jmin((int) mag.size() - 1, k + halfWidth); ++i) m = juce::jmax(m, mag[(size_t) i]);
    return juce::Decibels::gainToDecibels((double) m, -300.0);
}
static double zcHz(const std::vector<float>& x, size_t a, size_t b, double sr) {
    double first = -1, last = -1; int n = 0;
    for (size_t i = a + 1; i < b && i < x.size(); ++i) if (x[i - 1] <= 0 && x[i] > 0) { const double t = (double) (i - 1) + x[i - 1] / (double) (x[i - 1] - x[i]); if (first < 0) first = t; last = t; ++n; }
    return n >= 2 ? (n - 1) * sr / (last - first) : 0.0;
}
static double rms(const std::vector<float>& x, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b && i < x.size(); ++i) s += (double) x[i] * x[i]; return std::sqrt(s / (double) juce::jmax<size_t>(1, b - a)); }
static bool allFinite(const std::vector<float>& x, float& peak) { peak = 0; for (float v : x) { if (!std::isfinite(v)) return false; peak = juce::jmax(peak, std::abs(v)); } return true; }
static double hfRatioDb(const std::vector<float>& x, double hz) {
    int order = 10; while ((1u << order) < x.size() && order < 20) ++order;
    const int N = 1 << order; juce::dsp::FFT fft(order); std::vector<float> b((size_t) N * 2, 0.0f); const size_t len = juce::jmin(x.size(), (size_t) N);
    for (size_t i = 0; i < len; ++i) { const double t = 2.0 * juce::MathConstants<double>::pi * (double) i / (double) (len - 1); b[i] = (float) (x[i] * (0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t))); }
    fft.performFrequencyOnlyForwardTransform(b.data());
    double tot = 0, hf = 0; for (int i = 1; i < N / 2; ++i) { const double e = (double) b[(size_t) i] * b[(size_t) i]; tot += e; if (i * SR / N >= hz) hf += e; }
    return 10.0 * std::log10(hf / juce::jmax(1e-30, tot) + 1e-30);
}
// worst spectral component that is not a harmonic of f0, relative to the strongest component
static double worstNonHarmonic(const std::vector<float>& m, double f0, double sr, double from = 0.3) {
    const int order = 16, N = 1 << order; const auto mag = spectrum(m, (size_t) (from * sr), order);
    float ref = 0; for (size_t i = 4; i < mag.size(); ++i) ref = juce::jmax(ref, mag[i]);
    double worst = -300;
    for (size_t i = (size_t) (30.0 * N / sr); i < mag.size(); ++i) {
        const double hz = i * sr / N; const double h = hz / f0; if (std::abs(h - std::round(h)) * f0 < 14 * sr / N) continue;
        worst = juce::jmax(worst, juce::Decibels::gainToDecibels((double) mag[i] / ref, -300.0));
    }
    return worst;
}
static size_t vmHwmKb() { std::ifstream f("/proc/self/status"); std::string l; while (std::getline(f, l)) if (l.rfind("VmHWM:", 0) == 0) return (size_t) std::atol(l.c_str() + 6); return 0; }
template <typename F> static void pump(F&& f, int ms) { for (int i = 0; i < ms / 10; ++i) { if (f()) return; juce::MessageManager::getInstance()->runDispatchLoopUntil(10); } }

// ================================================================ measured core
static void test440() {
    Page p = secPage(4.0); addArc(p, mkArc({ { 0.05f, 69.0f }, { 0.95f, 69.0f } }));
    std::vector<float> L, R; render(p, L, R);
    const auto m = mono(L, R); const double f = peakHz(spectrum(m, (size_t) SR, 17), SR, 1 << 17); const double zc = zcHz(m, (size_t) (0.5 * SR), (size_t) (3.5 * SR), SR);
    check(std::abs(f - 440.0) <= 0.5 && std::abs(zc - 440.0) <= 0.5, "drawn line at A4 plays 440 Hz: FFT " + juce::String(f, 3) + " Hz, zero-crossing " + juce::String(zc, 3) + " Hz");
    for (double sr2 : { 44100.0, 96000.0 }) { std::vector<float> l2, r2; render(p, l2, r2, 120, sr2); const double z2 = zcHz(mono(l2, r2), (size_t) (0.5 * sr2), (size_t) (3.5 * sr2), sr2);
        check(std::abs(z2 - 440.0) <= 0.5 && std::abs((double) l2.size() / sr2 - 4.1) < 0.01, "any sample rate (" + juce::String(sr2 / 1000.0, 1) + " kHz): 440 Hz line plays " + juce::String(z2, 3) + " Hz, length " + juce::String(l2.size() / sr2, 3) + " s"); }
}
static double trackError(const std::vector<float>& m, const std::function<float(double)>& wantMidi, double t0, double t1, double& worstAt) {
    double worst = 0; const size_t win = 1200, hop = 480;
    for (size_t a = (size_t) (t0 * SR); a + win < (size_t) (t1 * SR); a += hop) {
        const double hz = zcHz(m, a, a + win, SR); if (hz <= 0) continue;
        const double tc = (a + win / 2) / SR; const double err = std::abs(12.0 * std::log2(hz / midiToHz(wantMidi(tc))));
        if (err > worst) { worst = err; worstAt = tc; }
    }
    return worst;
}
static void testGlide() {
    Page p = secPage(4.0); Arc a = mkArc({ { 0.0f, 57.0f }, { 0.5f, 81.0f }, { 1.0f, 64.0f } }); addArc(p, a);
    std::vector<float> L, R; render(p, L, R); const auto m = mono(L, R);
    double at = 0; const double err = trackError(m, [&](double t) { return a.pitchAt((float) (t / 4.0)); }, 0.1, 3.9, at);
    check(err < 0.06, "glide follows the drawn line (220 -> 880 -> 330 Hz): worst f0 error " + juce::String(err * 100.0, 2) + " cents (at " + juce::String(at, 2) + " s)");
    Page q = p; q.speedOn = true; q.speed = { { 0.0f, 1.0f }, { 0.4999f, 1.0f }, { 0.5f, -1.0f }, { 1.0f, -1.0f } };
    SnapCache c; auto s = buildSnapshot(q, c); std::vector<float> L2, R2; Engine::render(*s, SR, 120, L2, R2); const auto m2 = mono(L2, R2);
    const double D = 4.0 * s->total; double at2 = 0; const double err2 = trackError(m2, [&](double t) { return a.pitchAt(s->posAtTime((float) (t / 4.0))); }, 0.1, D - 0.1, at2);
    check(std::abs(D - 5.0) < 0.02 && err2 < 0.08, "speed lane (x2 then x1/2): page lasts " + juce::String(D, 3) + " s (want 5.000), worst f0 error " + juce::String(err2 * 100.0, 2) + " cents");
}
static void testReverse() {
    Page p = secPage(3.0);
    Arc a = mkArc({ { 0.05f, 60.0f }, { 0.7f, 72.0f } }); a.env = { { 0, 0.1f }, { 0.3f, 1.0f }, { 1, 0.3f } }; addArc(p, a);
    Arc b = mkArc({ { 0.5f, 79.0f }, { 0.95f, 67.0f } }, ShapeSine, 0.5f); b.env = { { 0, 1 }, { 1, 0.2f } }; addArc(p, b);
    std::vector<float> L, R, L2, R2; render(p, L, R); Page q = p; q.reverse = true; render(q, L2, R2);
    const auto f = mono(L, R), r = mono(L2, R2); const size_t n = (size_t) (3.0 * SR);
    std::vector<double> ef, er; const size_t fr = 480;
    for (size_t k = 0; k + fr <= n; k += fr) { ef.push_back(rms(f, k, k + fr)); er.push_back(rms(r, n - k - fr, n - k)); }
    double sxy = 0, sxx = 0, syy = 0, mx = 0, my = 0; for (size_t i = 0; i < ef.size(); ++i) { mx += ef[i]; my += er[i]; } mx /= (double) ef.size(); my /= (double) er.size();
    for (size_t i = 0; i < ef.size(); ++i) { sxy += (ef[i] - mx) * (er[i] - my); sxx += (ef[i] - mx) * (ef[i] - mx); syy += (er[i] - my) * (er[i] - my); }
    const double corr = sxy / std::sqrt(sxx * syy);
    double num = 0, den = 0;
    for (double t = 0.2; t < 2.8; t += 0.1) { const auto A = spectrum(f, (size_t) (t * SR) - 2048, 12); const auto B = spectrum(r, n - (size_t) (t * SR) - 2048, 12);
        for (size_t i = 0; i < A.size(); ++i) { num += (A[i] - B[i]) * (A[i] - B[i]); den += A[i] * A[i]; } }
    const double specDb = 10.0 * std::log10(num / den + 1e-30); double worst = 0;
    for (double t = 0.3; t < 1.4; t += 0.025) { const double hf = zcHz(f, (size_t) (t * SR), (size_t) (t * SR) + 1200, SR), hr = zcHz(r, n - (size_t) (t * SR) - 1200, n - (size_t) (t * SR), SR);
        if (hf > 0 && hr > 0) worst = juce::jmax(worst, std::abs(12.0 * std::log2(hf / hr))); }
    check(corr > 0.995 && specDb < -20.0 && worst < 0.05, "reverse = time-reversed output: envelope correlation " + juce::String(corr, 5) + ", spectrogram difference " + juce::String(specDb, 1) + " dB, pitch track difference " + juce::String(worst * 100.0, 2) + " cents");
}
static Page fmPage(float carrierMidi, float modMidi, float depth, int carrierShape, double secs = 2.0) {
    Page p = secPage(secs); const int c = addArc(p, mkArc({ { 0.0f, carrierMidi }, { 1.0f, carrierMidi } }, carrierShape));
    Arc m = mkArc({ { 0.0f, modMidi }, { 1.0f, modMidi } }); m.modOnly = true; const int mid = addArc(p, m); p.find(c)->fmSource = mid; p.find(c)->fmDepth = depth; return p;
}
static void testFMTheory() {
    juce::String txt; double worstDev = 0;
    for (float depth : { 0.2f, 0.3f, 0.4f }) {
        std::vector<float> L, R; render(fmPage(69, 45, depth, ShapeSine, 3.0), L, R); auto mag = spectrum(mono(L, R), (size_t) SR, 16); const int N = 1 << 16;
        const double car = levelDbAt(mag, SR, N, 440), s1 = 0.5 * (levelDbAt(mag, SR, N, 550) + levelDbAt(mag, SR, N, 330)) - car, s2 = 0.5 * (levelDbAt(mag, SR, N, 660) + levelDbAt(mag, SR, N, 220)) - car;
        const double beta = 2.0 * juce::MathConstants<double>::pi * depth * depth * 2.0 * 0.8;
        const double t1 = 20 * std::log10(std::abs(std::cyl_bessel_j(1.0, beta) / std::cyl_bessel_j(0.0, beta))), t2 = 20 * std::log10(std::abs(std::cyl_bessel_j(2.0, beta) / std::cyl_bessel_j(0.0, beta)));
        worstDev = juce::jmax(worstDev, std::abs(s1 - t1), std::abs(s2 - t2));
        txt << "depth " << juce::String(depth, 1) << ": 1st " << juce::String(s1, 1) << " dB (theory " << juce::String(t1, 1) << "), 2nd " << juce::String(s2, 1) << " (" << juce::String(t2, 1) << "); ";
    }
    std::vector<float> L, R; render(fmPage(69, 45, 0.0f, ShapeSine, 3.0), L, R); auto mag0 = spectrum(mono(L, R), (size_t) SR, 16); const double off = levelDbAt(mag0, SR, 1 << 16, 550) - levelDbAt(mag0, SR, 1 << 16, 440);
    check(worstDev < 1.0 && off < -90, "FM sidebands measurable and on theory: " + txt + "depth 0: " + juce::String(off, 1) + " dB");
}
static void testQuantise() {
    struct C { float drawn, want; } cs[] = { { 61.3f, 62.0f }, { 63.8f, 64.0f }, { 66.2f, 67.0f }, { 70.4f, 71.0f }, { 69.9f, 69.0f }, { 59.6f, 60.0f } };
    double worst = 0;
    for (auto& c : cs) { Page p = secPage(2.0); p.quantise = true; addArc(p, mkArc({ { 0, c.drawn }, { 1, c.drawn } }));
        std::vector<float> L, R; render(p, L, R); const double f = zcHz(mono(L, R), (size_t) (0.3 * SR), (size_t) (1.7 * SR), SR); worst = juce::jmax(worst, std::abs(12.0 * std::log2(f / midiToHz(c.want)))); }
    Page g = secPage(4.0); g.quantise = true; addArc(g, mkArc({ { 0, 60.0f }, { 1, 72.0f } }));
    std::vector<float> L, R; render(g, L, R); const auto m = mono(L, R); int frames = 0, onKey = 0;
    for (size_t a = (size_t) (0.1 * SR); a + 1200 < (size_t) (3.9 * SR); a += 1200) { const double hz = zcHz(m, a, a + 1200, SR); if (hz <= 0) continue; const double mm = hzToMidi((float) hz); ++frames;
        if (std::abs(mm - std::round(mm)) < 0.03 && inScale((int) std::round(mm), 0, 0)) ++onKey; }
    check(worst < 0.01 && onKey >= frames * 9 / 10, "quantise lands on the key (C major): steady lines off by at most " + juce::String(worst * 100, 2) + " cents; glide frames on a scale note " + juce::String(onKey) + "/" + juce::String(frames));
}
static void testAliasing() {
    juce::String txt; bool ok = true;
    for (double f0 : { 8000.0, 3520.0, 12000.0 }) for (int shape : { (int) ShapeSaw, (int) ShapeSquare }) {
        Page p = secPage(2.0); addArc(p, mkArc({ { 0, hzToMidi((float) f0) }, { 1, hzToMidi((float) f0) } }, shape));
        std::vector<float> L, R; render(p, L, R); const double w = worstNonHarmonic(mono(L, R), midiToHz(hzToMidi((float) f0)), SR); ok = ok && w < -70.0;
        txt << waveShapeNames()[shape] << " " << juce::String(f0 / 1000.0, 2) << " kHz " << juce::String(w, 1) << " dB; ";
    }
    Page g = secPage(3.0); addArc(g, mkArc({ { 0, hzToMidi(2000) }, { 1, hzToMidi(14000) } }, ShapeSaw));
    std::vector<float> L, R; render(g, L, R); const auto m = mono(L, R); double worstLow = -300;
    for (double t = 0.3; t < 2.7; t += 0.2) { const auto mag = spectrum(m, (size_t) (t * SR), 12); const double f0 = midiToHz(g.arcs[0].pitchAt((float) ((t + 2048 / SR) / 3.0)));
        const double ref = levelDbAt(mag, SR, 4096, f0, 3); for (size_t i = 8; i < (size_t) ((f0 * 0.8) * 4096 / SR); ++i) worstLow = juce::jmax(worstLow, juce::Decibels::gainToDecibels((double) mag[i], -300.0) - ref); }
    check(ok && worstLow < -70.0, "band-limited plain lines: worst non-harmonic spur " + txt + "saw sweep 2-14 kHz below-fundamental spur " + juce::String(worstLow, 1) + " dB");
}
static void testZipper() {
    Page p = secPage(2.0); Arc a = mkArc({ { 0, 55.0f }, { 1, 55.0f } }); for (int k = 0; k < 8; ++k) { a.env.push_back({ k / 8.0f, (k % 2) ? 1.0f : 0.15f }); a.env.push_back({ (k + 1) / 8.0f - 0.0001f, (k % 2) ? 1.0f : 0.15f }); } addArc(p, a);
    std::vector<float> L, R; render(p, L, R); auto m = mono(L, R); m.erase(m.begin(), m.begin() + (std::ptrdiff_t) (0.05 * SR)); m.resize((size_t) (1.9 * SR));
    const double envHf = hfRatioDb(m, 3000.0);
    Page q = secPage(2.0); Arc b = mkArc({ { 0, 55.0f }, { 1, 55.0f } }); for (int k = 0; k < 8; ++k) { b.pan.push_back({ k / 8.0f, (k % 2) ? 1.0f : -1.0f }); b.pan.push_back({ (k + 1) / 8.0f - 0.0001f, (k % 2) ? 1.0f : -1.0f }); } addArc(q, b);
    render(q, L, R); std::vector<float> l(L.begin() + (std::ptrdiff_t) (0.05 * SR), L.begin() + (std::ptrdiff_t) (1.95 * SR)); const double panHf = hfRatioDb(l, 3000.0);
    check(envHf < -70.0 && panHf < -70.0, "no zipper: energy above 3 kHz from envelope steps " + juce::String(envHf, 1) + " dB, pan flips " + juce::String(panHf, 1) + " dB");
    Engine e; e.prepare(SR, 256); SnapCache c; Page s = secPage(4.0); addArc(s, mkArc({ { 0, 55.0f }, { 1, 55.0f } })); e.publish(buildSnapshot(s, c)); e.internalPlay = true; e.followHost = false;
    std::vector<float> oL((size_t) SR * 2), oR((size_t) SR * 2); HostInfo h; h.bpm = 0;
    for (size_t pos = 0; pos < oL.size(); pos += 256) { if (pos == (size_t) SR) { s.arcs[0].gain = 0.1f; e.publish(buildSnapshot(s, c)); } e.process(oL.data() + pos, oR.data() + pos, 256, h, false); }
    std::vector<float> seg(oL.begin() + (std::ptrdiff_t) (0.5 * SR), oL.begin() + (std::ptrdiff_t) (1.5 * SR));
    check(hfRatioDb(seg, 3000.0) < -70.0, "no zipper on a live level edit (new snapshot mid-note): " + juce::String(hfRatioDb(seg, 3000.0), 1) + " dB above 3 kHz");
}

// ================================================================ review items
// 1  BLOCKER: non-finite input never reaches the audio
static void t01_nan() {
    const float nan = std::nanf(""), inf = std::numeric_limits<float>::infinity();
    juce::ValueTree t("ARCSPAGE"); t.setProperty("lengthBeats", std::nan(""), nullptr); t.setProperty("master", inf, nullptr); t.setProperty("loopA", std::nan(""), nullptr); t.setProperty("speedOn", true, nullptr); t.setProperty("tempoSync", false, nullptr);
    float spd[] = { 0.1f, nan, nan, 1.0f, 0.5f, inf, 0.7f, -0.5f }; t.setProperty("speed", juce::MemoryBlock(spd, sizeof(spd)), nullptr);
    for (int k = 0; k < 3; ++k) {
        juce::ValueTree a("ARC"); a.setProperty("id", k + 1, nullptr);
        float pts[] = { 0.0f, 60.0f, 0.3f, nan, nan, 64.0f, 0.6f, inf, 0.9f, 67.0f, inf, 70.0f };
        float env[] = { 0.0f, nan, 0.5f, 1.0f, nan, 0.2f }; float pan[] = { 0.0f, inf, 1.0f, -inf }; float cut[] = { nan, nan, 0.5f, 0.5f };
        a.setProperty("pts", juce::MemoryBlock(pts, sizeof(pts)), nullptr); a.setProperty("env", juce::MemoryBlock(env, sizeof(env)), nullptr);
        a.setProperty("pan", juce::MemoryBlock(pan, sizeof(pan)), nullptr); a.setProperty("cutoff", juce::MemoryBlock(cut, sizeof(cut)), nullptr);
        a.setProperty("gain", std::nan(""), nullptr); a.setProperty("fmDepth", inf, nullptr); a.setProperty("drive", std::nan(""), nullptr); a.setProperty("rate", std::nan(""), nullptr); a.setProperty("filter", 1, nullptr);
        float wave[kWaveLen]; for (int i = 0; i < kWaveLen; ++i) wave[i] = i % 7 == 0 ? nan : std::sin(i * 0.1f); a.setProperty("wave", juce::MemoryBlock(wave, sizeof(wave)), nullptr);
        a.setProperty("fmSource", k == 0 ? 2 : -1, nullptr); t.appendChild(a, nullptr);
    }
    bool fin = true; int finite = 0, total = 0;
    auto scan = [&](const std::vector<float>& x) { for (float v : x) { ++total; if (std::isfinite(v)) ++finite; else fin = false; } };
    Page p = Page::fromTree(t); std::vector<float> L, R; render(p, L, R); scan(L); scan(R);
    ArcsHost h; h.setState(t); h.prepare(SR, 512); h.engine.followHost = false; h.engine.internalPlay = true; std::vector<float> l(512), r(512);
    for (int b = 0; b < 200; ++b) { HostInfo hi; hi.bpm = b % 3 == 0 ? std::nan("") : 120; h.process(l.data(), r.data(), 512, hi, false); scan(l); scan(r); }
    const auto f = juce::File::createTempFile(".ghostarcs"); if (auto x = t.createXml()) x->writeTo(f); ArcsHost h2; const bool loaded = h2.loadFromFile(f); f.deleteFile();
    std::vector<float> L3, R3; render(h2.page(), L3, R3); scan(L3); scan(R3);
    check(fin && loaded && p.arcs.size() == 3, "1 hostile NaN/Inf in points / lanes / speed / page fields (tree, host state, file, NaN host tempo): " + juce::String(finite) + " of " + juce::String(total) + " samples finite");
}
// 2  BLOCKER: FM (and drive) do not alias
static void t02_fmAlias() {
    juce::String txt; double worst = -300;
    for (double f : { 2000.0, 3700.0, 5000.0, 8000.0 }) for (int shape : { (int) ShapeSine, (int) ShapeSaw }) for (float depth : { 0.5f, 1.0f }) {
        const float m = hzToMidi((float) f); std::vector<float> L, R; render(fmPage(m, m, depth, shape), L, R);
        const double w = worstNonHarmonic(mono(L, R), midiToHz(m), SR); worst = juce::jmax(worst, w);
        if (std::abs(f - 3700.0) < 1.0) txt << (shape == ShapeSine ? "sine" : "saw") << " d" << juce::String(depth, 1) << " " << juce::String(w, 1) << " dB; ";
    }
    Page d = secPage(2.0); Arc a = mkArc({ { 0, hzToMidi(3700) }, { 1, hzToMidi(3700) } }, ShapeSaw); a.drive = 1.0f; addArc(d, a); std::vector<float> L, R; render(d, L, R); const double wd = worstNonHarmonic(mono(L, R), 3700.0, SR);
    check(worst <= -60.0 && wd <= -60.0, "2 FM carrier = modulator 2-8 kHz, sine + saw, depth 0.5 / 1.0: worst non-harmonic spur " + juce::String(worst, 1) + " dB (3.7 kHz: " + txt + "); saw 3.7 kHz drive 1.0: " + juce::String(wd, 1) + " dB");
}
// 3  BLOCKER: no historic name in this page's code, file names, CMake names or its CMakeLists line
static void t03_names() {
    const juce::File here = juce::File(__FILE__).getParentDirectory().getParentDirectory();   // SpectrumX
    const juce::String bad = juce::String("u") + "pic"; int hits = 0, files = 0; juce::StringArray where;
    juce::Array<juce::File> mine = here.findChildFiles(juce::File::findFiles, false, "Arcs*"); mine.add(here.getChildFile("tests/ArcsTests.cpp"));
    for (auto& f : mine) { ++files; if (f.loadFileAsString().toLowerCase().contains(bad) || f.getFileName().toLowerCase().contains(bad)) { ++hits; where.add(f.getFileName()); } }
    for (auto& f : here.findChildFiles(juce::File::findFiles, true, "*")) if (f.getFileName().toLowerCase().contains(bad)) { ++hits; where.add(f.getFileName()); }
    juce::StringArray lines; lines.addLines(here.getParentDirectory().getChildFile("CMakeLists.txt").loadFileAsString());
    bool included = false; for (auto& l : lines) { if (l.containsIgnoreCase("Arcs.cmake")) { included = true; if (l.toLowerCase().contains(bad)) ++hits; } }
    check(hits == 0 && files >= 9 && included, "3 no historic name in the page's " + juce::String(files) + " files, any file name under SpectrumX, or its CMakeLists include line" + (hits ? ": " + where.joinIntoString(", ") : juce::String()));
}
// 4  MAJOR: free-time page follows tempo changes without jumping
static void t04_tempo() {
    SnapCache c; Page p = secPage(8.0); addArc(p, mkArc({ { 0, 60 }, { 1, 72 } }));
    double worstWithSec = 0, worstNoSec = 0;
    for (bool withSec : { true, false }) {
        Engine e; e.prepare(SR, 512); e.publish(buildSnapshot(p, c)); std::vector<float> L(512), R(512);
        double ppq = 64, secs = 32.0, bpm = 120; float last = -1;
        for (int b = 0; b < 200; ++b) {
            if (b == 100) { bpm = 121; } if (b == 150) { bpm = 90; }
            HostInfo h; h.bpm = bpm; h.ppq = ppq; h.playing = true; if (withSec) h.timeSec = secs;
            e.process(L.data(), R.data(), 512, h, false); const float pp = e.playPos.load();
            if (last >= 0 && b > 1) { double d = pp - last; if (d < -0.5) d += 1.0; const double expect = 512.0 / SR / 8.0; (withSec ? worstWithSec : worstNoSec) = juce::jmax(withSec ? worstWithSec : worstNoSec, std::abs(d - expect) * 8.0 * 1000.0); }
            last = pp; ppq += 512.0 / SR * bpm / 60.0; secs += 512.0 / SR;
        }
    }
    check(worstWithSec < 1.0 && worstNoSec < 1.0, "4 free-time page through tempo changes 120 -> 121 -> 90: worst playhead step error " + juce::String(worstWithSec, 3) + " ms (host seconds), " + juce::String(worstNoSec, 3) + " ms (ppq only)");
}
// 5  MAJOR: count-in is silent
static void t05_countIn() {
    SnapCache c; Page p; p.lengthBeats = 4; addArc(p, mkArc({ { 0, 60 }, { 1, 72 } })); Engine e; e.prepare(SR, 512); e.publish(buildSnapshot(p, c));
    std::vector<float> L(512), R(512); float peak = 0;
    for (int b = 0; b < 60; ++b) { HostInfo h; h.bpm = 120; h.ppq = -2.0 + b * 512.0 / SR * 2.0; h.playing = true; if (h.ppq >= -0.02) break; e.process(L.data(), R.data(), 512, h, false); for (float v : L) peak = juce::jmax(peak, std::abs(v)); }
    HostInfo h; h.bpm = 120; h.ppq = 0.5; h.playing = true; float after = 0; for (int b = 0; b < 20; ++b) { e.process(L.data(), R.data(), 512, h, false); for (float v : L) after = juce::jmax(after, std::abs(v)); h.ppq += 512.0 / SR * 2.0; }
    check(peak < 1.0e-12f && after > 0.05f, "5 host count-in (ppq < 0) is silent: peak " + juce::String(peak, 4) + "; from beat 0.5 it plays (peak " + juce::String(after, 2) + ")");
}
// MIDI parsing helper
struct MNote { int track, ch, key; double t0, t1; };
static std::vector<MNote> notesOf(const juce::MidiFile& mf, std::vector<const juce::MidiMessageSequence*>* tracks = nullptr) {
    std::vector<MNote> out;
    for (int t = 0; t < mf.getNumTracks(); ++t) { auto* tr = mf.getTrack(t); if (tracks) tracks->push_back(tr);
        for (int i = 0; i < tr->getNumEvents(); ++i) { auto* ev = tr->getEventPointer(i); if (ev->message.isNoteOn() && ev->noteOffObject) out.push_back({ t, ev->message.getChannel(), ev->message.getNoteNumber(), ev->message.getTimeStamp(), ev->noteOffObject->message.getTimeStamp() }); } }
    return out;
}
static juce::MidiFile roundTrip(const juce::MidiFile& m) { juce::MemoryOutputStream mo; m.writeTo(mo, 1); juce::MemoryInputStream mi(mo.getData(), mo.getDataSize(), false); juce::MidiFile in; in.readFrom(mi); in.convertTimestampTicksToSeconds(); return in; }
static double heardAt(const juce::MidiMessageSequence& tr, int ch, int key, double t, int range) {
    int b = 8192; for (int i = 0; i < tr.getNumEvents(); ++i) { const auto& m = tr.getEventPointer(i)->message; if (m.getTimeStamp() > t + 1e-9) break; if (m.isPitchWheel() && m.getChannel() == ch) b = m.getPitchWheelValue(); }
    return key + (b - 8192) / 8192.0 * range;
}
static void testMidiBasic() {
    ArcsHost h; auto& p = h.edit(); p.tempoSync = true; p.lengthBeats = 16; p.ownBpm = 120;
    addArc(p, mkArc({ { 0.0f, 60.0f }, { 0.25f, 60.0f } })); addArc(p, mkArc({ { 0.1f, 64.0f }, { 0.4f, 71.5f }, { 0.6f, 62.0f } })); addArc(p, mkArc({ { 0.5f, 48.0f }, { 1.0f, 36.0f } }));
    Arc mod = mkArc({ { 0.0f, 80.0f }, { 1.0f, 80.0f } }); mod.modOnly = true; addArc(p, mod); h.commit("test");
    SnapCache c; auto s = buildSnapshot(h.page(), c); const double D = s->seconds(120.0);
    for (bool mpe : { false, true }) {
        const auto in = roundTrip(h.makeMidi(mpe, 120.0)); std::vector<const juce::MidiMessageSequence*> trs; const auto ns = notesOf(in, &trs); const int range = h.bendRange(mpe);
        double worstPitch = 0, worstTime = 0;
        for (auto& n : ns) { int best = -1; double bd = 1e9; for (int k = 0; k < (int) s->arcs.size(); ++k) if (s->arcs[(size_t) k]->audible) { const double d = std::abs(s->timeAtPos(s->arcs[(size_t) k]->x0) * D - n.t0); if (d < bd) { bd = d; best = k; } }
            const auto& a = *s->arcs[(size_t) best]; worstTime = juce::jmax(worstTime, bd, std::abs(s->timeAtPos(a.x1) * D - n.t1));
            for (double f = 0.02; f < 0.99; f += 0.07) { const double t = n.t0 + f * (n.t1 - n.t0); int seg = 0; const double want = s->heardMidi(a, s->posAtTime((float) (t / D)), seg);
                worstPitch = juce::jmax(worstPitch, std::abs(heardAt(*trs[(size_t) n.track], n.ch, n.key, t, range) - want)); } }
        check(ns.size() == 3 && worstPitch < 0.05 && worstTime < 0.002, juce::String(mpe ? "MPE" : "standard") + " MIDI export matches the lines: 3 notes, worst pitch error " + juce::String(worstPitch * 100.0, 2) + " cents, worst timing " + juce::String(worstTime * 1000.0, 2) + " ms");
    }
}
// 6  MAJOR: a dive wider than the bend range survives (split into notes); bend range configurable
static void t06_dive() {
    ArcsHost h; auto& p = h.edit(); p.lengthBeats = 8; addArc(p, mkArc({ { 0.0f, 100.0f }, { 0.9f, 30.0f } })); h.commit("dive");
    SnapCache c; auto s = buildSnapshot(h.page(), c); const double D = s->seconds(120.0); const auto& a = *s->arcs[0];
    juce::String txt; bool ok = true;
    for (int br : { 0, 1, 2 }) for (bool mpe : { false, true }) {
        h.edit().bendRange = br; ArcsHost::MidiReport rep; const auto in = roundTrip(h.makeMidi(mpe, 120.0, &rep)); std::vector<const juce::MidiMessageSequence*> trs; auto ns = notesOf(in, &trs);
        const int range = h.bendRange(mpe); double worst = 0, covered = 0;
        for (auto& n : ns) { covered += n.t1 - n.t0; for (double f = 0.05; f < 0.96; f += 0.1) { const double t = n.t0 + f * (n.t1 - n.t0); int seg = 0;
            worst = juce::jmax(worst, std::abs(heardAt(*trs[(size_t) n.track], n.ch, n.key, t, range) - (double) s->heardMidi(a, s->posAtTime((float) (t / D)), seg))); } }
        // the RPN in the file says the same range
        bool rpnOk = false; for (int i = 0; i < trs[0]->getNumEvents(); ++i) { const auto& m = trs[0]->getEventPointer(i)->message; if (m.isController() && m.getControllerNumber() == 6 && m.getControllerValue() == range) rpnOk = true; }
        ok = ok && worst < 0.1 && std::abs(covered - a.span * D) < 0.01 && rpnOk && (range >= 48 || rep.splits > 0);
        txt << (mpe ? "MPE " : "") << "+-" << range << ": " << rep.notes << " notes, err " << juce::String(worst * 100, 1) << " c" << (rpnOk ? "" : " NO-RPN") << (std::abs(covered - a.span * D) < 0.01 ? "" : " cover " + juce::String(covered, 3) + "/" + juce::String(a.span * D, 3)) << "; ";
    }
    check(ok, "6 a 100 -> 30 dive exports whole (split into notes when wider than the bend range; range 12/24/48, MPE 24/48/96 written as RPN): " + txt);
}
// 7  MAJOR: overlapping lines never share a channel; never channel 10
static void t07_overflow() {
    ArcsHost h; auto& p = h.edit(); p.lengthBeats = 16; for (int k = 0; k < 20; ++k) addArc(p, mkArc({ { 0.0f, 60.0f }, { 0.5f, 60.0f } })); h.commit("20");
    bool ok = true; juce::String txt;
    for (bool mpe : { false, true }) {
        ArcsHost::MidiReport rep; const auto mf = h.makeMidi(mpe, 120.0, &rep); const auto ns = notesOf(mf);   // ticks
        double shortest = 1e9; bool ch10 = false, shared = false;
        for (auto& n : ns) { shortest = juce::jmin(shortest, n.t1 - n.t0); if (!mpe && n.ch == 10) ch10 = true; if (mpe && n.ch == 1) ch10 = true; }
        for (size_t i = 0; i < ns.size(); ++i) for (size_t j = i + 1; j < ns.size(); ++j) if (ns[i].track == ns[j].track && ns[i].ch == ns[j].ch && ns[i].t0 < ns[j].t1 - 1 && ns[j].t0 < ns[i].t1 - 1) shared = true;
        ok = ok && ns.size() == 20 && std::abs(shortest - 7680) < 2 && !ch10 && !shared && rep.tracks == 2;
        txt << (mpe ? "MPE" : "standard") << ": " << (int) ns.size() << " notes on " << rep.tracks << " tracks, shortest " << (int) shortest << " ticks; ";
    }
    check(ok, "7 20 overlapping C4 lines: " + txt + "no shared channel, never channel 10 (standard) / 1 (MPE)");
}
// 8  MAJOR: MPE sends what the header says
static void t08_mpeCC() {
    ArcsHost h; auto& p = h.edit(); Arc a = mkArc({ { 0, 60 }, { 0.5f, 62 } }); a.pan = { { 0, -1 }, { 1, 1 } }; a.env = { { 0, 0.2f }, { 1, 1 } }; addArc(p, a); h.commit("x");
    int cc74 = 0, cc10 = 0, press = 0, cc11 = 0; const auto mf = h.makeMidi(true, 120);
    for (int t = 0; t < mf.getNumTracks(); ++t) for (auto* ev : *mf.getTrack(t)) { const auto& m = ev->message; if (m.isController() && m.getControllerNumber() == 74) ++cc74; if (m.isController() && m.getControllerNumber() == 10) ++cc10; if (m.isChannelPressure()) ++press; if (m.isController() && m.getControllerNumber() == 11) ++cc11; }
    int s10 = 0, s11 = 0; const auto ms = h.makeMidi(false, 120); for (int t = 0; t < ms.getNumTracks(); ++t) for (auto* ev : *ms.getTrack(t)) { const auto& m = ev->message; if (m.isController() && m.getControllerNumber() == 10) ++s10; if (m.isController() && m.getControllerNumber() == 11) ++s11; }
    check(cc74 > 5 && cc10 == 0 && press > 5 && cc11 == 0 && s10 > 5 && s11 > 5, "8 MPE: pan on CC74 (" + juce::String(cc74) + " events), envelope as pressure (" + juce::String(press) + "), no CC10/CC11; standard: CC10 pan (" + juce::String(s10) + "), CC11 envelope (" + juce::String(s11) + ")");
}
// 9  MAJOR: offline render streams (flat memory), any sample rate, errors reported
static void t09_render() {
    Page p = secPage(600.0); addArc(p, mkArc({ { 0, 57 }, { 1, 69 } }));   // 10 minutes
    SnapCache c; auto s = buildSnapshot(p, c); const size_t before = vmHwmKb(); juce::int64 n = 0; double sum = 0;
    const bool ok = Engine::render(*s, 44100.0, 120, [&](const float* l, const float*, int k) { n += k; for (int i = 0; i < k; i += 97) sum += std::abs(l[i]); return true; });
    const size_t grow = (vmHwmKb() - before) / 1024;
    ArcsHost h; h.prepare(96000, 512); h.edit() = secPage(2.0); addArc(h.edit(), mkArc({ { 0, 69 }, { 1, 69 } })); h.commit("r");
    const auto f = juce::File::createTempFile(".wav"); h.startRender(f); pump([&] { return !h.rendering(); }, 20000);
    double fileSr = 0; juce::int64 len = 0; { juce::WavAudioFormat wav; if (auto* r = wav.createReaderFor(new juce::FileInputStream(f), true)) { fileSr = r->sampleRate; len = r->lengthInSamples; delete r; } }
    ArcsHost h2; h2.edit() = secPage(2.0); addArc(h2.edit(), mkArc({ { 0, 69 }, { 1, 69 } })); h2.commit("r"); const auto bad = juce::File("/proc/nope/cannot.wav"); h2.startRender(bad); pump([&] { return !h2.rendering(); }, 5000);
    f.deleteFile();
    check(ok && std::abs((double) n / 44100.0 - 600.1) < 0.01 && sum > 0 && grow < 64 && std::abs(fileSr - 96000.0) < 1.0 && std::abs((double) len / 96000.0 - 2.1) < 0.01 && !h2.renderReady() && h2.renderError().isNotEmpty(),
          "9 a 10-minute page renders streamed at 44.1 kHz: " + juce::String((double) n / 44100.0, 1) + " s, peak memory grew " + juce::String((int) grow) + " MB (whole-page buffers would be 203 MB); host render writes the engine's 96 kHz ("
          + juce::String(fileSr / 1000.0, 1) + " kHz WAV); an unwritable target reports \"" + h2.renderError() + "\"");
}
// 10 MAJOR: no silent line loss over the limit
static void t10_limit() {
    Page p; for (int k = 0; k < 250; ++k) addArc(p, mkArc({ { (k % 50) / 60.0f, 40.0f + k % 40 }, { (k % 50) / 60.0f + 0.01f, 40.0f + k % 40 } }));
    std::vector<int> ids; for (int k = 0; k < 20; ++k) ids.push_back(p.arcs[(size_t) k].id);
    int skipped = 0; const auto n = copyArcs(p, ids, 0.0f, &skipped); p.clampAll();
    Page full; for (int k = 0; k < kMaxArcs; ++k) addArc(full, mkArc({ { 0.0f, 40.0f + (k % 60) }, { 1.0f, 40.0f + (k % 60) } }));
    bool refused = false; const size_t before = full.arcs.size(); eraseAt(full, 0.5f, 40.0f, 0.02f, 0.4f, &refused);
    const bool addRefused = full.add(mkArc({ { 0, 60 }, { 1, 60 } })) == nullptr;
    juce::ValueTree t("ARCSPAGE"); for (int k = 0; k < 300; ++k) t.appendChild(arcToTree(mkArc({ { 0, 60 }, { 1, 61 } })), nullptr);
    for (int k = 0; k < 300; ++k) t.getChild(k).setProperty("id", k + 1, nullptr);
    LoadInfo li; auto lp = Page::fromTree(t, &li);
    check(n.size() == 6 && skipped == 14 && p.arcs.size() == 256 && refused && full.arcs.size() == before && addRefused && lp.arcs.size() == 256 && li.truncatedLines == 44 && li.message().contains("44"),
          "10 copy 20 lines onto a 250-line page: " + juce::String((int) n.size()) + " copied, " + juce::String(skipped) + " reported as not fitting; a split on a full page is refused; load of 300 lines says \"" + li.message() + "\"");
}
// 11 MAJOR: a live edit rebuilds one line
static void t11_incremental() {
    ArcsHost h; auto& p = h.edit(); juce::Random r(9);
    for (int k = 0; k < 256; ++k) { Arc a = mkArc({ { 0.0f, 40.0f + k % 50 }, { 0.5f, 50.0f + k % 30 }, { 1.0f, 45.0f + k % 40 } }); std::array<float, kWaveLen> w {}; for (auto& v : w) v = r.nextFloat() * 2 - 1; a.setWave(w); addArc(p, a); }
    const double t0 = juce::Time::getMillisecondCounterHiRes(); h.commit("load"); const double first = juce::Time::getMillisecondCounterHiRes() - t0;
    double worst = 0; int rebuilt = 0;
    for (int i = 0; i < 30; ++i) { h.edit().arcs[(size_t) (i * 7 % 256)].pts[1].y += 0.5f; h.changed(); worst = juce::jmax(worst, h.lastPublishMs()); rebuilt = juce::jmax(rebuilt, h.lastRebuiltLines()); }
    check(rebuilt == 1 && worst < 4.0, "11 256 distinct lines: first build " + juce::String(first, 1) + " ms (wavetables built in parallel); a live edit rebuilds " + juce::String(rebuilt) + " line, worst publish " + juce::String(worst, 2) + " ms");
}
// 12 MAJOR: pitch detail does not depend on the page length
static void t12_resolution() {
    Page p; p.tempoSync = true; p.lengthBeats = 512; std::vector<Pt> pts; const float x0 = 0.25f;
    for (int k = 0; k <= 64; ++k) pts.push_back({ x0 + k * (1.0f / 16) / 512.0f, 93.0f + (k % 2 ? 2.0f : -2.0f) });   // 1/16-beat zig-zag (31.25 ms per step at 120)
    addArc(p, mkArc(pts)); SnapCache c;
    Engine e; e.prepare(SR, 512); e.publish(buildSnapshot(p, c)); e.followHost = false; e.internalPlay = true; e.jumpTo = x0 - 0.0001f; e.jumpQuantBeats = 0;
    std::vector<float> L((size_t) (2.2 * SR)), R(L.size()); HostInfo h; h.bpm = 120;
    for (size_t pos = 0; pos < L.size(); pos += 512) e.process(L.data() + pos, R.data() + pos, (int) juce::jmin((size_t) 512, L.size() - pos), h, false);
    double worst = 0; const double D = 256.0, seg = 0.03125, lead = 0.0001 * D; juce::String dbg;
    for (int k = 1; k < 60; ++k) { const double tMid = lead + (k + 0.5) * seg; const size_t a = (size_t) ((tMid - 0.004) * SR) + (size_t) Engine::kLatency;
        const double hz = zcHz(L, a, a + (size_t) (0.008 * SR), SR); const double want = p.arcs[0].pitchAt((float) (x0 - 0.0001 + tMid / D));
        const double err = hz > 0 ? std::abs(12.0 * std::log2(hz / midiToHz((float) want))) : 99.0;
        if (err > worst) { worst = err; dbg = "step " + juce::String(k); } }
    check(worst < 0.15, "12 512-beat page, 1/16-beat zig-zag +-2 semitones: worst heard-vs-drawn error " + juce::String(worst, 3) + " semitones over 59 steps (was 3.99 with a page-length table)");
}
// 13 MAJOR: setState from another thread is applied on the message thread
static void t13_threadState() {
    ArcsHost h; Page p; addArc(p, mkArc({ { 0, 60 }, { 1, 62 } })); addArc(p, mkArc({ { 0, 64 }, { 1, 66 } })); const auto t = p.toTree();
    std::thread th([&] { h.setState(t); }); th.join();
    const bool notYet = h.page().arcs.empty(); pump([&] { return h.page().arcs.size() == 2; }, 2000);
    check(notYet && h.page().arcs.size() == 2, "13 setState from a loader thread is queued and applied on the message thread (" + juce::String((int) h.page().arcs.size()) + " lines after the queue ran)");
}
// 14 MINOR: retired snapshots are freed after a grace period, editor closed
static void t14_grace() {
    Exchange<int> ex; for (int i = 0; i < 5; ++i) { ex.publish(std::make_unique<int>(i)); (void) ex.get(); }
    const double now = juce::Time::getMillisecondCounterHiRes(); ex.collect(now, 1000.0); const size_t held = ex.graveSize(); ex.collect(now + 1500.0, 1000.0); const size_t after = ex.graveSize();
    ArcsHost h; for (int i = 0; i < 20; ++i) { h.edit().lengthBeats = 4 + i; h.changed(); } std::vector<float> L(64), R(64); HostInfo hi; h.process(L.data(), R.data(), 64, hi, false);
    check(held >= 1 && after == 0, "14 a retired snapshot waits its 1 s grace on the message thread, then is freed (" + juce::String((int) held) + " held, " + juce::String((int) after) + " after); the host's own timer collects with no editor");
}
// 15 MINOR: a deleted line fades on its own sound
static void t15_orphan() {
    SnapCache c; Page p = secPage(4.0); Arc a = mkArc({ { 0, 57 }, { 1, 57 } }, ShapeSaw); a.pan = { { 0, 0.8f }, { 1, 0.8f } }; addArc(p, a);
    Engine e; e.prepare(SR, 64); e.publish(buildSnapshot(p, c)); e.followHost = false; e.internalPlay = true; std::vector<float> L(64), R(64); HostInfo h; h.bpm = 0;
    for (int b = 0; b < 300; ++b) e.process(L.data(), R.data(), 64, h, false);
    Page empty = p; empty.arcs.clear(); e.publish(buildSnapshot(empty, c));
    std::vector<float> fl, fr; for (int b = 0; b < 4; ++b) { e.process(L.data(), R.data(), 64, h, false); fl.insert(fl.end(), L.begin(), L.end()); fr.insert(fr.end(), R.begin(), R.end()); }
    // the fade (after the 36-sample latency) keeps the saw's 2nd harmonic and the right-heavy pan
    double h2 = 0, h1 = 0; for (size_t i = 40; i < 200; ++i) { const double t = i / SR; h1 += fr[i] * std::sin(2 * juce::MathConstants<double>::pi * 220 * t); h2 += fr[i] * std::sin(2 * juce::MathConstants<double>::pi * 440 * t); }
    const double rl = rms(fr, 40, 200) / juce::jmax(1e-9, rms(fl, 40, 200));
    float tail = 0; for (int b = 0; b < 100; ++b) { e.process(L.data(), R.data(), 64, h, false); } for (float v : L) tail = juce::jmax(tail, std::abs(v));
    check(rl > 2.0 && tail < 1e-4f, "15 a deleted line fades out on its own wavetable with its pan kept (right/left " + juce::String(rl, 2) + "), then silence (" + juce::String(tail, 6) + ")");
}
// 16 MINOR: a host loop seam does not re-glide
static void t16_seam() {
    SnapCache c; Page p; p.lengthBeats = 4; addArc(p, mkArc({ { 0, 81 }, { 1, 93 } })); Engine e; e.prepare(SR, 128); e.publish(buildSnapshot(p, c));
    std::vector<float> L(128), R(128), out; HostInfo h; h.bpm = 120; h.playing = true; h.ppq = 3.0;
    for (int b = 0; b < 160; ++b) { e.process(L.data(), R.data(), 128, h, false); h.ppq += 128.0 / SR * 2.0; }   // up to near the end (~1720 Hz)
    h.ppq = 0.0; for (int b = 0; b < 8; ++b) { e.process(L.data(), R.data(), 128, h, false); out.insert(out.end(), L.begin(), L.end()); h.ppq += 128.0 / SR * 2.0; }
    const double f = zcHz(out, (size_t) Engine::kLatency + 16, (size_t) Engine::kLatency + 16 + 240, SR);   // first 5 ms after the seam
    check(std::abs(12 * std::log2(f / 880.0)) < 0.5, "16 host loop seam: the first 5 ms after the jump play " + juce::String(f, 1) + " Hz (the line's start, 880 Hz), no glide from 1760 Hz");
}
// 17 MINOR: the edge fade is in real time under the speed lane
static void t17_fade() {
    auto onsetRise = [](bool fast) { Page p = secPage(4.0); if (fast) { p.speedOn = true; p.speed = { { 0, 2.0f }, { 1, 2.0f } }; } Arc a = mkArc({ { 0.2f, 60 }, { 0.8f, 60 } }); addArc(p, a);
        SnapCache c; auto s = buildSnapshot(p, c); std::vector<float> L, R; Engine::render(*s, SR, 120, L, R); const auto m = mono(L, R);
        size_t start = 0; for (size_t i = 0; i < m.size(); ++i) if (std::abs(m[i]) > 1e-4f) { start = i; break; }
        float peak = 0; for (size_t i = start; i < start + 4800 && i < m.size(); ++i) peak = juce::jmax(peak, std::abs(m[i]));
        size_t t90 = start; for (size_t i = start; i < m.size(); ++i) if (std::abs(m[i]) > 0.9f * peak) { t90 = i; break; } return (t90 - start) / SR * 1000.0; };
    const double slow = onsetRise(false), fast = onsetRise(true);
    check(fast > 2.0 && std::abs(fast - slow) < 1.5, "17 line onset under speed x4: rise " + juce::String(fast, 2) + " ms vs " + juce::String(slow, 2) + " ms at x1 (the 4 ms edge fade stays 4 ms of real time)");
}
// 18 MINOR: a mono bus is as loud as stereo
static void t18_mono() {
    ArcsHost h; h.prepare(SR, 512); addArc(h.edit(), mkArc({ { 0, 60 }, { 1, 60 } })); h.commit("m"); h.engine.followHost = false; h.engine.internalPlay = true;
    juce::AudioBuffer<float> st(2, 512), mo(1, 512); HostInfo hi; double ps = 0, pm = 0;
    for (int b = 0; b < 100; ++b) { st.clear(); h.process(st, hi, true); if (b > 20) ps += std::pow(st.getRMSLevel(0, 0, 512), 2) + std::pow(st.getRMSLevel(1, 0, 512), 2); }
    h.engine.stopRequest = true;
    for (int b = 0; b < 100; ++b) { mo.clear(); h.process(mo, hi, true); if (b > 20) pm += std::pow(mo.getRMSLevel(0, 0, 512), 2); }
    const double d = 10 * std::log10(pm / ps);
    check(std::abs(d) < 0.2, "18 mono bus power vs stereo L+R power: " + juce::String(d, 2) + " dB (was +3 dB)");
}
// 19 MINOR: duplicate ids, point cap, newer versions
static void t19_hostile() {
    juce::ValueTree t("ARCSPAGE"); for (int k = 0; k < 3; ++k) { auto a = arcToTree(mkArc({ { 0, 60.0f + k }, { 1, 61.0f + k } })); a.setProperty("id", k == 2 ? -4 : 1, nullptr); t.appendChild(a, nullptr); }
    std::vector<Pt> many; for (int k = 0; k < 50000; ++k) many.push_back({ k / 50000.0f, 60.0f + (k % 7) }); auto big = arcToTree(mkArc(many)); big.setProperty("id", 9, nullptr); t.appendChild(big, nullptr);
    LoadInfo li; auto p = Page::fromTree(t, &li); std::vector<int> ids; for (auto& a : p.arcs) ids.push_back(a.id);
    bool unique = true; for (size_t i = 0; i < ids.size(); ++i) for (size_t j = i + 1; j < ids.size(); ++j) if (ids[i] == ids[j]) unique = false; for (int id : ids) unique = unique && id > 0;
    juce::ValueTree fut("ARCSPAGE"); fut.setProperty("version", kStateVersion + 1, nullptr); LoadInfo lf; Page::fromTree(fut, &lf);
    ArcsHost h; addArc(h.edit(), mkArc({ { 0, 60 }, { 1, 60 } })); h.commit("x"); h.setState(fut); const auto msg = h.takeMessage();
    check(unique && li.renumbered == 2 && p.find(9)->pts.size() <= (size_t) kMaxPts && lf.futureVersion && h.page().arcs.size() == 1 && msg.contains("newer"),
          "19 duplicate / negative ids renumbered (" + juce::String(li.renumbered) + "), 50000 points capped to " + juce::String((int) p.find(9)->pts.size()) + ", a newer-version page is refused: \"" + msg + "\"");
}
// 20 MINOR: a knob reset (double-click, wheel, keys) is its own undo step
static juce::Slider* findSlider(juce::Component& root, const juce::String& tipStart) {
    for (auto* c : root.getChildren()) { if (auto* s = dynamic_cast<juce::Slider*>(c)) if (s->getTooltip().startsWith(tipStart)) return s; if (auto* r = findSlider(*c, tipStart)) return r; } return nullptr; }
static void t20_knob() {
    ArcsHost h; const int id = addArc(h.edit(), mkArc({ { 0, 60 }, { 1, 60 } })); h.commit("x"); ArcsPage page(h); page.setSelection({ id });
    auto* s = findSlider(page, "The line's level"); const bool found = s != nullptr;
    if (s) s->setValue(0.3, juce::sendNotificationSync);
    const float after = h.page().find(id)->gain; const bool und = h.undo(); const float back = h.page().find(id)->gain;
    check(found && std::abs(after - 0.3f) < 1e-4f && und && std::abs(back - 0.8f) < 1e-4f, "20 a knob change without a drag (double-click reset) commits at once: level 0.3, one UNDO brings back " + juce::String(back, 2));
}
// 21 MINOR: stamps move the lanes too
static void t21_stamps() {
    Page p; Arc a = mkArc({ { 0.1f, 60 }, { 0.5f, 64 } }); a.env = { { 0, 1 }, { 1, 0 } }; a.pan = { { 0, -1 }, { 1, 1 } }; const int id = addArc(p, a);
    mirrorArcs(p, { id }); const bool envRev = std::abs(p.find(id)->envAt(0.0f)) < 0.01f && std::abs(p.find(id)->envAt(1.0f) - 1) < 0.01f && p.find(id)->panAt(0) > 0.9f;
    Page q; const int i2 = addArc(q, mkArc({ { 0.6f, 60 }, { 0.7f, 61 }, { 0.8f, 62 }, { 0.9f, 63 } })); stretchArcs(q, { i2 }, 3.0f, 1.0f); const bool kept = q.find(i2)->pts.size() == 4 && q.find(i2)->x1() <= 1.0f;
    Page e; Arc b = mkArc({ { 0, 60 }, { 0.25f, 60 }, { 0.5f, 60 }, { 0.75f, 60 }, { 1, 60 } }); b.env = { { 0, 0 }, { 1, 1 } }; const int i3 = addArc(e, b); eraseAt(e, 0.5f, 60, 0.05f, 0.5f);
    const Arc* left = e.find(i3); const Arc* right = nullptr; for (auto& x : e.arcs) if (x.id != i3) right = &x;
    const bool cropped = left && right && std::abs(left->envAt(1.0f) - left->x1()) < 0.03f && std::abs(right->envAt(0.0f) - right->x0()) < 0.03f && left->x1() < 0.5f && right->x0() > 0.5f;
    check(envRev && kept && cropped, "21 mirror reverses envelope + pan, stretch keeps every point on the page, an erased fragment keeps its own part of the envelope");
}
// 22 MINOR: one tooltip window (the host editor can switch the page's own off)
static void t22_tooltips() {
    ArcsHost h; ArcsPage a(h), b(h, false); auto count = [](juce::Component& c) { int n = 0; for (auto* k : c.getChildren()) if (dynamic_cast<juce::TooltipWindow*>(k)) ++n; return n; };
    const int def = count(a); a.setOwnsTooltips(false); const int off = count(a); a.setOwnsTooltips(true); const int back = count(a);
    check(def == 1 && off == 0 && back == 1 && count(b) == 0 && !b.ownsTooltips(), "22 tooltips: the page owns one TooltipWindow by default (" + juce::String(def) + "); setOwnsTooltips(false) / the constructor flag leave none (" + juce::String(off) + ", " + juce::String(count(b)) + ") so Spectrum's own shows each tip once");
}
// 24 MAJOR: tone shaping: unison, sub, drive, filter
static void t24_shaping() {
    auto spec = [](Arc a) { Page p = secPage(2.0); addArc(p, a); std::vector<float> L, R; render(p, L, R); return std::make_pair(spectrum(mono(L, R), (size_t) (0.5 * SR), 16), std::make_pair(L, R)); };
    const int N = 1 << 16;
    Arc base = mkArc({ { 0, 57 }, { 1, 57 } }, ShapeSaw);
    Arc u = base; u.unison = 7; u.detune = 1.0f; u.spread = 1.0f; auto su = spec(u);
    // 7 detuned voices show as separate partials around the 10th harmonic (2.2 kHz, spaced ~20 Hz)
    int peaks = 0; { const int k0 = (int) (2200.0 * 0.97 * N / SR), k1 = (int) (2200.0 * 1.03 * N / SR); float top = 0; for (int k = k0; k <= k1; ++k) top = juce::jmax(top, su.first[(size_t) k]);
        for (int k = k0 + 4; k <= k1 - 4; ++k) { const float v = su.first[(size_t) k]; bool mx = v > top * 0.1f; for (int d = 1; d <= 4 && mx; ++d) mx = v >= su.first[(size_t) (k - d)] && v >= su.first[(size_t) (k + d)]; if (mx) ++peaks; } }
    double corr = 0, a2 = 0, b2 = 0; const auto& L = su.second.first; const auto& R = su.second.second; for (size_t i = 24000; i < 72000; ++i) { corr += L[i] * R[i]; a2 += L[i] * L[i]; b2 += R[i] * R[i]; } corr /= std::sqrt(a2 * b2);
    Arc sb = mkArc({ { 0, 57 }, { 1, 57 } }); sb.sub = 1.0f; auto ss = spec(sb); const double subDb = levelDbAt(ss.first, SR, N, 110) - levelDbAt(ss.first, SR, N, 220);
    Arc dr = mkArc({ { 0, 57 }, { 1, 57 } }); dr.drive = 1.0f; auto sd = spec(dr); const double h3 = levelDbAt(sd.first, SR, N, 660) - levelDbAt(sd.first, SR, N, 220);
    auto plain = spec(base); Arc lp = base; lp.filter = FiltLP; lp.cutoff = { { 0, std::log2(1000.0f / 20.0f) / 10.0f } }; lp.res = 0; auto sl = spec(lp);
    const double cut = (levelDbAt(plain.first, SR, N, 4400) - levelDbAt(plain.first, SR, N, 220)) - (levelDbAt(sl.first, SR, N, 4400) - levelDbAt(sl.first, SR, N, 220));
    check(peaks >= 5 && corr < 0.9 && subDb > -6 && h3 > -20 && cut > 20, "24 unison 7: " + juce::String(peaks) + " detuned partials around 220 Hz, L/R correlation " + juce::String(corr, 2) + "; sub at 110 Hz " + juce::String(subDb, 1)
          + " dB; drive raises the 3rd harmonic to " + juce::String(h3, 1) + " dB; LP at 1 kHz cuts the 20th harmonic by " + juce::String(cut, 1) + " dB more");
}
// 25 MAJOR: factory pages + patches
static void t25_factory() {
    int ok = 0, total = 0; float worst = 0; juce::String bad;
    for (auto& d : factory::pages()) { ++total; Page p = d.make(); SnapCache c; auto s = buildSnapshot(p, c); std::vector<float> L; juce::int64 n = 0; float pk = 0; bool fin = true;
        Engine::render(*s, SR, 120, [&](const float* l, const float* r, int k) { for (int i = 0; i < k; ++i) { fin = fin && std::isfinite(l[i]) && std::isfinite(r[i]); pk = juce::jmax(pk, std::abs(l[i]), std::abs(r[i])); } n += k; return n < (juce::int64) (SR * 6); });
        if (fin && pk > 0.01f && pk <= 1.0f && !p.arcs.empty()) ++ok; else bad << d.name << " "; worst = juce::jmax(worst, pk); }
    int pOk = 0; for (auto& pt : factory::patches()) { Arc a = mkArc({ { 0, 60 }, { 1, 60 } }); pt.apply(a); a.clampAll(); Page p = secPage(0.5); addArc(p, a); std::vector<float> L, R; render(p, L, R); float pk = 0; if (allFinite(L, pk) && pk > 0.001f) ++pOk; }
    check(ok == total && total >= 20 && pOk == (int) factory::patches().size() && pOk >= 16, "25 " + juce::String(total) + " factory pages all play (finite, audible, peak <= 1) and " + juce::String(pOk) + " line patches work" + (bad.isNotEmpty() ? " - bad: " + bad : juce::String()));
}
// 26 MAJOR: MIDI in
static void t26_midiIn() {
    SnapCache c; Page p = secPage(4.0); p.midiIn = true; addArc(p, mkArc({ { 0, 60 }, { 1, 60 } })); Page pb = p; pb.arcs.clear(); addArc(pb, mkArc({ { 0, 72 }, { 1, 72 } }));
    Engine e; e.prepare(SR, 512); e.publish(buildSnapshot(p, c), 0); SnapCache c2; e.publish(buildSnapshot(pb, c2), 1);
    std::vector<float> L(512), R(512), out; HostInfo h; h.bpm = 120; juce::MidiBuffer mb;
    auto run = [&](int blocks, juce::MidiBuffer* m) { out.clear(); for (int b = 0; b < blocks; ++b) { e.process(L.data(), R.data(), 512, h, false, b == 0 ? m : nullptr); out.insert(out.end(), L.begin(), L.end()); } };
    run(10, nullptr); float silent = 0; for (float v : out) silent = juce::jmax(silent, std::abs(v));
    mb.addEvent(juce::MidiMessage::noteOn(1, 62, (juce::uint8) 100), 0); run(40, &mb); const double f = zcHz(out, 4800, 19000, SR);
    juce::MidiBuffer off; off.addEvent(juce::MidiMessage::noteOff(1, 62), 0); run(40, &off); float tail = 0; for (size_t i = 9600; i < out.size(); ++i) tail = juce::jmax(tail, std::abs(out[i]));
    juce::MidiBuffer sw; sw.addEvent(juce::MidiMessage::noteOn(1, 1, (juce::uint8) 100), 0); sw.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 100); run(2, &sw); run(30, nullptr);
    const double f2 = zcHz(out, 4800, 14000, SR);
    check(silent < 1.0e-12f && std::abs(12 * std::log2(f / midiToHz(62))) < 0.05 && tail < 1e-4f && e.playingSlot.load() == 1 && std::abs(12 * std::log2(f2 / midiToHz(72))) < 0.1,
          "26 MIDI in: silent until a note; D4 plays the page +2 semitones (" + juce::String(f, 1) + " Hz), release stops it; note C#-1 switches to page B (" + juce::String(f2, 1) + " Hz)");
}
// 27 MINOR: auto gain + limiter indicator
static void t27_autoGain() {
    SnapCache c; Page p = secPage(2.0); for (int k = 0; k < 64; ++k) addArc(p, mkArc({ { 0, 48.0f + k % 24 }, { 1, 48.0f + k % 24 } }, ShapeSaw, 0.5f)); p.master = 0.8f;
    auto run = [&](bool ag, bool& knee) { Page q = p; q.autoGain = ag; Engine e; e.prepare(SR, 512); e.publish(buildSnapshot(q, c)); e.followHost = false; e.internalPlay = true; std::vector<float> L(512), R(512); HostInfo h; h.bpm = 0; float pk = 0; knee = false;
        for (int b = 0; b < 100; ++b) { e.process(L.data(), R.data(), 512, h, false); if (b > 20) { for (float v : L) pk = juce::jmax(pk, std::abs(v)); knee = knee || e.kneeActive.load(); } } return pk; };
    bool k1, k2; const float off = run(false, k1), on = run(true, k2);
    check(k1 && off > 0.8f && on < off, "27 64 saw lines: without AUTO the limiter works (OUT meter amber, peak " + juce::String(off, 2) + "); with AUTO GAIN peak " + juce::String(on, 2) + (k2 ? " (still limiting)" : " (no limiting)"));
}
// 28 MAJOR: per-line rate / repeat = polyrhythm
static void t28_rate() {
    Page p; p.tempoSync = true; p.lengthBeats = 12; Arc a = mkArc({ { 0, 69 }, { 1.0f / 12 * 0.9f, 69 } }); a.env = { { 0, 1 }, { 0.3f, 0.0f }, { 1, 0 } }; a.repeat = true; a.rate = 0.75f; addArc(p, a);   // a 0.9-beat blip every 1.2 beats
    std::vector<float> L, R; render(p, L, R); const auto m = mono(L, R);
    std::vector<double> onsets; bool on = false; for (size_t i = 0; i + 240 < m.size(); i += 240) { const double r = rms(m, i, i + 240); if (!on && r > 0.05) { onsets.push_back(i / SR); on = true; } else if (on && r < 0.005) on = false; }
    double worst = 0; for (size_t k = 1; k < onsets.size(); ++k) worst = juce::jmax(worst, std::abs((onsets[k] - onsets[k - 1]) - 1.2 * 0.5));
    ArcsHost h; h.edit() = p; h.commit("r"); const auto ns = notesOf(roundTrip(h.makeMidi(false, 120)));
    juce::String onList; for (auto t : onsets) onList << juce::String(t, 3) << " ";
    check(onsets.size() == 10 && worst < 0.006 && ns.size() == 10, "28 a line with RATE x3/4 + REPEAT on a 12-beat page: " + juce::String((int) onsets.size()) + " onsets every 1.2 beats (worst " + juce::String(worst * 1000, 1) + " ms off), MIDI export " + juce::String((int) ns.size()) + " notes" + (onsets.size() != 10 ? " [" + onList + "]" : juce::String()));
}
// 29 MAJOR: odd grids
static void t29_grid() {
    const bool vals = std::abs(gridBeats(7) - 2.0 / 3) < 1e-9 && std::abs(gridBeats(8) - 1.0 / 3) < 1e-9 && std::abs(gridBeats(10) - 0.2) < 1e-9 && std::abs(gridBeats(11) - 1.0 / 7) < 1e-9 && std::abs(gridBeats(12)) < 1e-12 && gridBeats(0) < 0;
    ArcsHost h; h.edit().grid = 8; h.edit().lengthBeats = 4; h.commit("g"); ArcsPage page(h); page.selectTool(ArcsPage::Line);
    page.testGesture({ { 0.30f, 60 }, { 0.45f, 62 }, { 0.71f, 64 } });   // 1.2 and 2.84 beats -> 1 1/3 and 2 2/3 (1/8 triplets)
    const auto& a = h.page().arcs.back(); const double b0 = a.x0() * 4, b1 = a.x1() * 4;
    const bool snapped = std::abs(b0 * 3 - std::round(b0 * 3)) < 1e-3 && std::abs(b1 * 3 - std::round(b1 * 3)) < 1e-3;
    check(vals && snapped && Page::fromTree(h.page().toTree()).grid == 8, "29 grids include triplets / 1/5 / 1/7 / free; a LINE on the 1/8 T grid lands on " + juce::String(b0, 3) + " and " + juce::String(b1, 3) + " beats; saved with the page");
}
// 30 MAJOR: clipboard + page bank
static void t30_clipBank() {
    Page a; const int m = addArc(a, mkArc({ { 0.1f, 60 }, { 0.2f, 60 } })); const int c = addArc(a, mkArc({ { 0.1f, 72 }, { 0.2f, 72 } })); a.find(c)->fmSource = m; a.find(c)->fmDepth = 0.5f;
    const auto text = linesToText(a, { m, c }); Page b; addArc(b, mkArc({ { 0, 40 }, { 1, 40 } })); const auto ids = pasteLines(b, text, 0.5f);
    const bool pasted = ids.size() == 2 && b.find(ids[1])->fmSource == ids[0] && std::abs(b.find(ids[0])->x0() - 0.5f) < 1e-5f;
    ArcsHost h; addArc(h.edit(), mkArc({ { 0, 60 }, { 1, 60 } })); h.commit("A"); h.selectSlot(2); addArc(h.edit(), mkArc({ { 0, 70 }, { 1, 70 } })); addArc(h.edit(), mkArc({ { 0, 71 }, { 1, 71 } })); h.commit("C");
    const auto st = h.getState(); ArcsHost h2; h2.setState(st); const bool bankOk = h2.slot() == 2 && h2.page().arcs.size() == 2 && !h2.slotEmpty(0);
    h.selectSlot(0); const bool undoPer = h.canUndo() && h.undo() && h.page().arcs.empty(); h.selectSlot(2); const bool otherKept = h.page().arcs.size() == 2;
    check(pasted && bankOk && undoPer && otherKept, "30 copy / paste lines between pages (FM link kept inside the group, pasted at the mouse); page bank A..H saved with the state, each page with its own undo");
}
// 31 MINOR: randomise
static void t31_random() {
    Page p; const int id = addArc(p, mkArc({ { 0.2f, 60 }, { 0.5f, 62 }, { 0.8f, 64 } })); Page a = p, b = p, c = p;
    randomiseArcs(a, { id }, 0.5f, 42); randomiseArcs(b, { id }, 0.5f, 42); randomiseArcs(c, { id }, 0.5f, 43);
    const bool same = a.toTree().isEquivalentTo(b.toTree()), diff = !a.toTree().isEquivalentTo(c.toTree()), changed = !a.toTree().isEquivalentTo(p.toTree());
    ArcsHost h; h.edit() = p; h.commit("x"); randomiseArcs(h.edit(), { id }, 1.0f, 7); h.commit("rnd"); const bool und = h.undo() && h.page().toTree().isEquivalentTo(p.toTree());
    check(same && diff && changed && und, "31 RANDOMISE: the same seed gives the same result, a new seed a new one, and UNDO restores the line");
}
// 32 MINOR: stable line numbers
static void t32_ids() {
    Page p; for (int k = 0; k < 5; ++k) addArc(p, mkArc({ { 0, 60.0f + k }, { 1, 60.0f + k } })); const int fmTarget = p.arcs[4].id; p.arcs[3].fmSource = fmTarget; p.remove(p.arcs[1].id);
    check(p.arcs[2].id == 4 && p.arcs[2].fmSource == fmTarget && p.arcs[3].id == 5, "32 lines are numbered by a stable id: deleting line 2 leaves lines 4 and 5 (and an 'FM from line 5' link) as they were");
}
// 33 MAJOR: smoothing + curve tool + key snap at draw
static void t33_ink() {
    std::vector<Pt> raw; juce::Random r(2); for (int k = 0; k < 200; ++k) raw.push_back({ k / 200.0f, 60.0f + k * 0.05f + (r.nextFloat() - 0.5f) * 0.8f });
    auto jitter = [](const std::vector<Pt>& v) { double s = 0; for (size_t i = 1; i + 1 < v.size(); ++i) s += std::abs(v[i + 1].y - 2 * v[i].y + v[i - 1].y); return s / (double) v.size(); };
    auto sm = raw; smoothStroke(sm, 1.0f); const double j0 = jitter(raw), j1 = jitter(sm);
    bool curves = true; for (int s = 0; s < 3; ++s) { const auto cv = makeCurve({ 0.1f, 50 }, { 0.9f, 74 }, s); curves = curves && std::abs(cv.front().y - 50) < 1e-4f && std::abs(cv.back().y - 74) < 1e-4f;
        for (size_t i = 1; i < cv.size(); ++i) curves = curves && cv[i].y >= cv[i - 1].y - 1e-4f; }
    const float midExp = makeCurve({ 0, 50 }, { 1, 74 }, 0)[24].y, midLog = makeCurve({ 0, 50 }, { 1, 74 }, 2)[24].y;
    ArcsHost h; h.edit().quantise = true; h.edit().root = 0; h.edit().scale = 0; h.commit("q"); ArcsPage page(h); page.selectTool(ArcsPage::Curve); page.testGesture({ { 0.1f, 61.4f }, { 0.5f, 70.0f }, { 0.9f, 77.6f } });
    const auto& a = h.page().arcs.back(); const bool keyEnds = std::abs(a.pts.front().y - 62) < 1e-3f && std::abs(a.pts.back().y - 77) < 1e-3f && a.pts.size() > 10;
    check(j1 < j0 * 0.35 && curves && midExp < 62 && midLog > 62 && keyEnds, "33 INK smoothing cuts stroke wobble to " + juce::String(100 * j1 / j0, 0) + "%; CURVE EXP / S / LOG risers are monotonic (mid " + juce::String(midExp, 1) + " / " + juce::String(midLog, 1) + "); with QUANTISE the ends land on the key");
}
// 34 MAJOR: quantised jump keeps the page on the bar grid
static void t34_jump() {
    SnapCache c; Page p; p.lengthBeats = 16; addArc(p, mkArc({ { 0, 60 }, { 1, 72 } })); Engine e; e.prepare(SR, 256); e.publish(buildSnapshot(p, c));
    std::vector<float> L(256), R(256); HostInfo h; h.bpm = 120; h.playing = true; h.ppq = 0;
    for (int b = 0; b < 100; ++b) { e.process(L.data(), R.data(), 256, h, false); h.ppq += 256.0 / SR * 2.0; }   // ~1.07 beats in
    e.jumpQuantBeats = 4.0f; e.jumpTo = 0.5f; double jumpedAt = -1; float before = e.playPos.load();
    for (int b = 0; b < 400 && jumpedAt < 0; ++b) { e.process(L.data(), R.data(), 256, h, false); const float pp = e.playPos.load(); if (pp > before + 0.05f) jumpedAt = h.ppq; before = pp; h.ppq += 256.0 / SR * 2.0; }
    // after the jump the page is a whole number of bars off: ppq 4 lands at page beat 8 (0.5 of 16)
    for (int b = 0; b < 50; ++b) { e.process(L.data(), R.data(), 256, h, false); h.ppq += 256.0 / SR * 2.0; }
    const double pageBeat = e.playPos.load() * 16.0, offset = std::fmod(pageBeat - h.ppq + 64.0, 4.0);
    check(jumpedAt > 3.9 && jumpedAt < 4.05 && (offset < 0.02 || offset > 3.98), "34 a ruler jump while the host plays waits for the next bar (applied at beat " + juce::String(jumpedAt, 3) + ") and keeps the page on the bar grid (offset " + juce::String(std::fmod(offset + 2, 4) - 2, 3) + " beats)");
}
// 35 MAJOR: noise lines follow the pitch through the tracked band-pass
static void t35_noise() {
    auto centroid = [](float midi) { Page p = secPage(2.0); Arc a; a.pts = { { 0, midi }, { 1, midi } }; factory::applyPatch(a, "Noise riser"); a.env.clear(); addArc(p, a);
        std::vector<float> L, R; render(p, L, R); const auto mag = spectrum(mono(L, R), (size_t) (0.5 * SR), 15); double s = 0, w = 0; for (size_t i = 1; i < mag.size(); ++i) { const double e = (double) mag[i] * mag[i]; s += e * i * SR / 32768.0; w += e; } return s / w; };
    const double lo = centroid(60), hi = centroid(96);
    check(hi / lo > 4.0, "35 NOISE line with the tracked band-pass: spectral centroid " + juce::String(lo, 0) + " Hz at C4, " + juce::String(hi, 0) + " Hz at C7 (follows the drawn pitch)");
}
// 36 MINOR: the page stops with a followed host
static void t36_transport() {
    SnapCache c; Page p; addArc(p, mkArc({ { 0, 60 }, { 1, 60 } })); Engine e; e.prepare(SR, 512); e.publish(buildSnapshot(p, c)); e.internalPlay = true;
    std::vector<float> L(512), R(512); HostInfo h; h.bpm = 120; h.playing = true; h.ppq = 0; for (int b = 0; b < 5; ++b) { e.process(L.data(), R.data(), 512, h, false); h.ppq += 0.02; }
    const bool running = e.hostRunning.load(); h.playing = false; e.process(L.data(), R.data(), 512, h, false);
    check(running && !e.internalPlay.load() && !e.isPlaying.load(), "36 when a followed DAW stops, the page stops too (no orphaned PLAY); while it plays PLAY shows HOST");
}
// 38 MINOR: right-to-left drawing works
static void t38_rtl() {
    ArcsHost h; ArcsPage page(h); page.selectTool(ArcsPage::Draw); std::vector<std::pair<float, float>> g; for (int k = 0; k <= 30; ++k) g.push_back({ 0.8f - k * 0.02f, 60.0f + k * 0.3f });
    page.testGesture(g); const bool ok = h.page().arcs.size() == 1 && h.page().arcs[0].x0() < 0.25f && h.page().arcs[0].x1() > 0.75f && h.page().arcs[0].pts.front().y > h.page().arcs[0].pts.back().y;
    check(ok, "38 a stroke drawn right to left becomes a line (played left to right, " + juce::String(h.page().arcs.empty() ? 0.0f : h.page().arcs[0].x0(), 2) + " .. " + juce::String(h.page().arcs.empty() ? 0.0f : h.page().arcs[0].x1(), 2) + ")");
}
// 39 MINOR: keyboard nudge
static void t39_nudge() {
    ArcsHost h; h.edit().grid = 3; h.edit().lengthBeats = 16; const int id = addArc(h.edit(), mkArc({ { 0.25f, 60 }, { 0.5f, 60 } })); h.commit("x"); ArcsPage page(h); page.setSelection({ id });
    page.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)); page.keyPressed(juce::KeyPress(juce::KeyPress::upKey)); page.keyPressed(juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier, 0));
    const auto& a = *h.page().find(id);
    check(std::abs(a.x0() - (0.25f + 1.0f / 16)) < 1e-4f && std::abs(a.pts[0].y - 73) < 1e-4f && h.canUndo(), "39 arrows nudge the selection: right = one grid step (1/4 = 1 beat), up = a semitone, shift+up = an octave; each is an undo step");
}

// 41 DEMO GATE: every export asks allowExport first
static void t41_exportGate() {
    ArcsHost h; h.prepare(SR, 512); addArc(h.edit(), mkArc({ { 0, 60 }, { 0.5f, 64 } })); h.commit("x"); ArcsPage page(h);
    const auto f = juce::File::createTempFile(".mid"); int asked = 0;
    page.allowExport = [&](juce::Component*, const char*) { ++asked; return false; };
    const bool midiNo = page.testExport(false, f), audioNo = page.testExport(true); const auto msg = page.status();
    const bool noFile = !f.existsAsFile() && !h.rendering() && !h.renderReady();
    page.allowExport = [&](juce::Component*, const char*) { ++asked; return true; };
    const bool midiYes = page.testExport(false, f) && f.existsAsFile(); const bool audioYes = page.testExport(true); pump([&] { return !h.rendering(); }, 20000);
    page.allowExport = nullptr; const bool nullOk = !page.testExport(false, f);   // fail closed: no gate wired = no export
    f.deleteFile();
    check(!midiNo && !audioNo && noFile && msg.contains("full version") && midiYes && audioYes && h.renderReady() && nullOk && asked == 4,
          "41 export gate: allowExport() false refuses the MIDI save and the audio render (\"" + msg + "\", no file); true lets both through; no hook = refused (fail closed)");
}
// 42 SEND TO ARRANGE: render to memory on a worker, with the notes
static void t42_renderBuffer() {
    ArcsHost h; h.edit().lengthBeats = 4; addArc(h.edit(), mkArc({ { 0.0f, 69 }, { 0.5f, 69 } })); Arc r = mkArc({ { 0.5f, 72 }, { 0.75f, 72 } }); r.repeat = true; addArc(h.edit(), r);
    Arc m = mkArc({ { 0, 40 }, { 1, 40 } }); m.modOnly = true; addArc(h.edit(), m); h.commit("x");
    const auto req = h.prepareRender(); juce::AudioBuffer<float> buf; std::vector<RenderNote> notes; bool ok = false; std::atomic<int> prog { 0 };
    std::thread worker([&] { ok = ArcsHost::renderToBuffer(req, buf, 44100.0, &notes, nullptr, &prog); }); worker.join();
    std::vector<float> L(buf.getReadPointer(0), buf.getReadPointer(0) + buf.getNumSamples());
    const double f = zcHz(L, 4410, 40000, 44100.0);
    const bool notesOk = notes.size() == 3 && notes[0].pitch == 69 && std::abs(notes[0].start) < 1e-3 && std::abs(notes[0].length - 1.0) < 2e-3 && notes[1].pitch == 72 && std::abs(notes[1].start - 1.0) < 2e-3 && std::abs(notes[2].start - 1.5) < 2e-3;
    std::atomic<bool> abortFlag { true }; juce::AudioBuffer<float> b2; const bool aborted = !ArcsHost::renderToBuffer(req, b2, 48000.0, nullptr, &abortFlag);
    check(ok && buf.getNumChannels() == 2 && std::abs(buf.getNumSamples() / 44100.0 - 2.1) < 0.01 && std::abs(f - 440.0) < 0.5 && notesOk && prog.load() == 100 && aborted,
          "42 render to memory on a worker (no file): " + juce::String(buf.getNumSamples() / 44100.0, 3) + " s stereo at 44.1 kHz, 440 Hz line plays " + juce::String(f, 2) + " Hz; notes " + juce::String((int) notes.size())
          + " (line + 2 repeats, modulator skipped) with pitch / start / length in seconds; an abort flag stops it");
}

static Page demoPageForState() {
    Page p; p.tempoSync = true; p.lengthBeats = 8; p.ownBpm = 97; p.reverse = true; p.speedOn = true; p.speed = { { 0, 0.4f }, { 1, -0.4f } }; p.quantise = true; p.root = 2; p.scale = 1;
    Arc a = mkArc({ { 0.0f, 62 }, { 0.06f, 65 }, { 0.12f, 69 } }, ShapeSaw, 0.4f); a.repeat = true; a.rate = 1.5f; a.unison = 5; a.filter = FiltLP; a.track = true; a.cutoff = { { 0, 0.4f }, { 1, 0.8f } }; addArc(p, a);
    Arc n; factory::applyPatch(n, "Noise riser"); n.pts = makeCurve({ 0.3f, 50 }, { 0.9f, 90 }, 1); n.gain = 0.3f; addArc(p, n);
    Arc b = mkArc({ { 0.2f, 45 }, { 0.8f, 40 } }, ShapeSquare, 0.5f); b.sub = 0.6f; b.drive = 0.4f; const int bid = addArc(p, b);
    Arc m = mkArc({ { 0.2f, 57 }, { 0.8f, 64 } }); m.modOnly = true; const int mid = addArc(p, m); p.find(bid)->fmSource = mid; p.find(bid)->fmDepth = 0.5f;
    p.clampAll(); return p;
}
// 43 RE-RENDER from a saved state blob: identical to the live page, any thread, hostile blobs safe, abort works
static void t43_fromState() {
    ArcsHost h; h.edit() = factory::pages()[2].make(); h.commit("fm growl");   // FM + drive (oversampled path) + a modulator
    h.selectSlot(3); h.edit() = demoPageForState(); h.commit("rich");          // the current slot: repeat, unison, filter, noise, speed lane, reverse
    const auto live = h.prepareRender(); const auto blob = h.renderBlob(); const auto tree = h.getState();
    juce::AudioBuffer<float> a, b, c; std::vector<RenderNote> na, nb; bool okA = false, okB = false, okC = false;
    std::thread w1([&] { okA = ArcsHost::renderToBuffer(live, a, 48000.0, &na); }); w1.join();
    std::thread w2([&] { okB = ArcsHost::renderToBuffer(ArcsHost::fromState(blob), b, 48000.0, &nb); }); w2.join();   // built + rendered on the worker
    std::thread w3([&] { okC = ArcsHost::renderToBuffer(ArcsHost::fromState(tree), c, 48000.0); }); w3.join();
    auto same = [](const juce::AudioBuffer<float>& x, const juce::AudioBuffer<float>& y) { if (x.getNumSamples() != y.getNumSamples() || x.getNumChannels() != y.getNumChannels()) return false;
        for (int ch = 0; ch < x.getNumChannels(); ++ch) { if (std::memcmp(x.getReadPointer(ch), y.getReadPointer(ch), sizeof(float) * (size_t) x.getNumSamples()) != 0) return false; } return true; };
    bool notesSame = na.size() == nb.size(); for (size_t i = 0; notesSame && i < na.size(); ++i) notesSame = na[i].pitch == nb[i].pitch && std::abs(na[i].start - nb[i].start) < 1e-12 && std::abs(na[i].length - nb[i].length) < 1e-12;
    // hostile / old / foreign blobs: valid request, empty page, silence
    juce::Random r(5); juce::MemoryBlock junk(4000); for (size_t i = 0; i < junk.getSize(); ++i) junk[i] = (char) r.nextInt(256);
    juce::ValueTree fut("ARCSPAGE"); fut.setProperty("version", kStateVersion + 1, nullptr); fut.appendChild(arcToTree(mkArc({ { 0, 60 }, { 1, 60 } })), nullptr);
    juce::MemoryOutputStream fo; fut.writeToStream(fo);
    bool hostileOk = true; float hostilePeak = 0;
    for (const auto& bl : { junk, juce::MemoryBlock(), fo.getMemoryBlock(), juce::MemoryBlock("not a page", 10) }) {
        juce::AudioBuffer<float> x; const auto rq = ArcsHost::fromState(bl); std::thread w([&] { hostileOk = hostileOk && rq.valid() && ArcsHost::renderToBuffer(rq, x, 48000.0); }); w.join();
        hostilePeak = juce::jmax(hostilePeak, x.getMagnitude(0, 0, x.getNumSamples()), x.getMagnitude(1, 0, x.getNumSamples())); }
    // an XML blob works too; abort stops a render
    const auto xmlBlob = juce::MemoryBlock(tree.toXmlString().toRawUTF8(), tree.toXmlString().getNumBytesAsUTF8()); juce::AudioBuffer<float> d; const bool okD = ArcsHost::renderToBuffer(ArcsHost::fromState(xmlBlob, live.bpm), d, 48000.0);
    std::atomic<bool> stop { true }; juce::AudioBuffer<float> e; const bool aborted = !ArcsHost::renderToBuffer(ArcsHost::fromState(blob), e, 48000.0, nullptr, &stop);
    check(okA && okB && okC && okD && same(a, b) && same(a, c) && same(a, d) && notesSame && hostileOk && hostilePeak < 1.0e-12f && aborted && a.getMagnitude(0, 0, a.getNumSamples()) > 0.01f,
          "43 re-render from the saved blob on a worker is bit-identical to the live page (" + juce::String(a.getNumSamples() / 48000.0, 2) + " s, " + juce::String((int) na.size()) + " notes; ValueTree and XML too); junk / empty / newer-version blobs render silence; abort works");
}

// 44 RULER GESTURES (one set in every product): left click / drag never makes a loop or moves the cursor; right-click plays
// from there; right-drag makes a loop; left-drag edits an existing loop's ends / middle
static void t44_rulerGestures() {
    ArcsHost h; h.prepare(SR, 512); h.edit().lengthBeats = 16; h.edit().grid = 3; h.edit().jumpQuant = 0; addArc(h.edit(), mkArc({ { 0, 60 }, { 1, 60 } })); h.commit("x"); ArcsPage page(h); page.fitView();
    std::vector<float> L(512), R(512); HostInfo hi; hi.bpm = 0; auto run = [&](int n) { for (int b = 0; b < n; ++b) h.process(L.data(), R.data(), 512, hi, false); };
    page.testRuler(0.3f, 0.3f, false); run(2); const bool clickNothing = !h.page().loopOn && !h.engine.isPlaying.load();
    page.testRuler(0.2f, 0.6f, false); const bool leftDragNothing = !h.page().loopOn;
    page.testRuler(0.5f, 0.5f, true); run(2); const float pos = h.engine.playPos.load(); const bool rightClickPlays = h.engine.isPlaying.load() && std::abs(pos - 0.5f) < 0.02f && !h.page().loopOn;
    page.testRuler(0.25f, 0.75f, true); const bool rightDragLoop = h.page().loopOn && std::abs(h.page().loopA - 0.25f) < 1e-4f && std::abs(h.page().loopB - 0.75f) < 1e-4f;
    const bool undoable = h.canUndo();
    page.testRuler(0.75f, 0.875f, false); const bool resized = std::abs(h.page().loopB - 0.875f) < 1e-4f && std::abs(h.page().loopA - 0.25f) < 1e-4f;
    page.testRuler(0.5f, 0.4375f, false); const bool moved = std::abs(h.page().loopA - 0.1875f) < 1e-4f && std::abs(h.page().loopB - 0.8125f) < 1e-4f;
    page.testRuler(0.95f, 0.95f, false); const bool stillLoop = h.page().loopOn && std::abs(h.page().loopA - 0.1875f) < 1e-4f;
    page.testRuler(0.6f, 0.602f, true); const bool tinyIsClick = std::abs(h.page().loopA - 0.1875f) < 1e-4f;   // a shaky right-click (< 6 px) is a click, not a loop
    // old saved state still loads its loop
    juce::ValueTree old("ARCSPAGE"); old.setProperty("version", 1, nullptr); old.setProperty("loopOn", true, nullptr); old.setProperty("loopA", 0.25, nullptr); old.setProperty("loopB", 0.5, nullptr);
    const auto op = Page::fromTree(old); const bool oldOk = op.loopOn && std::abs(op.loopA - 0.25f) < 1e-6f;
    check(clickNothing && leftDragNothing && rightClickPlays && rightDragLoop && undoable && resized && moved && stillLoop && tinyIsClick && oldOk,
          "44 ruler: left click / left drag make no loop and move nothing; right-click plays from there (" + juce::String(pos, 3) + "); right-drag makes a loop (0.25..0.75, undoable); left-drag resizes / moves it; a shaky right-click stays a click; old loops load");
}

// ================================================================ older checks kept
static void testState() {
    Page p; p.tempoSync = true; p.lengthBeats = 32; p.loopOn = true; p.loopA = 0.25f; p.loopB = 0.75f; p.reverse = true; p.speedOn = true; p.speed = { { 0, 0.5f }, { 1, -0.5f } };
    p.quantise = true; p.root = 9; p.scale = 1; p.master = 0.42f; p.name = "Round trip"; p.grid = 8; p.midiIn = true; p.autoGain = true;
    Arc a = mkArc({ { 0.1f, 60 }, { 0.3f, 67.25f }, { 0.4f, 64 } }, ShapeSaw, 0.6f); a.env = { { 0, 0 }, { 0.2f, 1 }, { 1, 0.1f } }; a.unison = 5; a.drive = 0.3f; a.filter = FiltBP; a.cutoff = { { 0, 0.3f }, { 1, 0.8f } }; a.rate = 1.5f; a.repeat = true;
    const int ia = addArc(p, a); Arc b = mkArc({ { 0.2f, 40 }, { 0.9f, 52 } }, ShapeSquare, 0.3f); b.fmSource = ia; b.fmDepth = 0.4f; b.source = SrcNoise; addArc(p, b);
    const auto t = p.toTree(); auto back = Page::fromTree(juce::ValueTree::fromXml(t.toXmlString()));
    juce::MemoryOutputStream bin; t.writeToStream(bin); auto back2 = Page::fromTree(juce::ValueTree::readFromData(bin.getData(), bin.getDataSize()));
    const bool same = back.toTree().isEquivalentTo(t) && back2.toTree().isEquivalentTo(t);
    std::vector<float> L1, R1, L2, R2; p.midiIn = false; back.midiIn = false; render(p, L1, R1); render(back, L2, R2);
    bool ident = L1.size() == L2.size(); for (size_t i = 0; ident && i < L1.size(); ++i) ident = std::memcmp(&L1[i], &L2[i], sizeof(float)) == 0 && std::memcmp(&R1[i], &R2[i], sizeof(float)) == 0;
    check(same && ident, "state round trip (XML + binary, every new line / page field): " + juce::String(same ? "identical tree" : "TREE DIFFERS") + ", " + (ident ? "bit-identical render" : "RENDER DIFFERS"));
}
static void testNaNFuzz() {
    juce::Random r(1234); bool fin = true; float worstPeak = 0; int pages = 0;
    for (int it = 0; it < 24; ++it) {
        Page p = secPage(0.5 + r.nextDouble() * 1.5); p.reverse = r.nextBool(); p.loopOn = r.nextBool(); p.loopA = r.nextFloat(); p.loopB = r.nextFloat(); p.quantise = r.nextBool(); p.scale = r.nextInt(7); p.master = 1.0f;
        p.speedOn = r.nextBool(); for (int k = 0; k < 8; ++k) p.speed.push_back({ r.nextFloat(), r.nextFloat() * 8 - 4 });
        const int n = 1 + r.nextInt(40);
        for (int k = 0; k < n; ++k) {
            Arc a; const int np = 2 + r.nextInt(12); for (int j = 0; j < np; ++j) a.pts.push_back({ r.nextFloat(), 5.0f + r.nextFloat() * 140.0f }); sortByX(a.pts);
            std::array<float, kWaveLen> w {}; for (auto& v : w) v = r.nextFloat() * 4 - 2; a.setWave(w);
            for (int j = 0; j < 5; ++j) { a.env.push_back({ r.nextFloat(), r.nextFloat() * 2 - 0.5f }); a.pan.push_back({ r.nextFloat(), r.nextFloat() * 3 - 1.5f }); a.cutoff.push_back({ r.nextFloat(), r.nextFloat() * 2 - 0.5f }); }
            a.gain = r.nextFloat() * 1.5f; a.fmDepth = r.nextFloat(); a.modOnly = r.nextInt(5) == 0; a.mute = r.nextInt(8) == 0; a.unison = r.nextInt(9); a.drive = r.nextFloat(); a.filter = r.nextInt(4); a.res = r.nextFloat() * 1.2f;
            a.sub = r.nextFloat(); a.source = r.nextInt(5) == 0 ? 1 : 0; a.rate = 0.1f + r.nextFloat() * 5; a.repeat = r.nextBool(); a.track = r.nextBool();
            addArc(p, a);
        }
        for (auto& a : p.arcs) if (r.nextBool()) a.fmSource = p.arcs[(size_t) r.nextInt((int) p.arcs.size())].id;
        p.clampAll();
        std::vector<float> L, R; render(p, L, R); float pk = 0; fin = fin && allFinite(L, pk); worstPeak = juce::jmax(worstPeak, pk); fin = fin && allFinite(R, pk); worstPeak = juce::jmax(worstPeak, pk); ++pages;
    }
    check(fin && worstPeak <= 1.0f, "no NaN / Inf over " + juce::String(pages) + " random pages (extreme pitches, FM loops, unison, drive, filters at full resonance, noise, rates, speed lane): peak " + juce::String(worstPeak, 3));
}
static void testTransport() {
    Engine e; e.prepare(SR, 512); SnapCache c; Page p = secPage(2.0); addArc(p, mkArc({ { 0, 60 }, { 1, 72 } })); p.loopOn = true; p.loopA = 0.25f; p.loopB = 0.5f;
    e.publish(buildSnapshot(p, c)); e.followHost = false; e.internalPlay = true; e.stopRequest = true;
    std::vector<float> L(512), R(512); HostInfo h; h.bpm = 0; float lo = 1, hi = 0;
    for (int b = 0; b < 400; ++b) { e.process(L.data(), R.data(), 512, h, false); const float pp = e.playPos.load(); lo = juce::jmin(lo, pp); hi = juce::jmax(hi, pp); }
    p.loopOn = false; e.publish(buildSnapshot(p, c)); e.jumpQuantBeats = 0; e.jumpTo = 0.8f; e.process(L.data(), R.data(), 512, h, false); const float afterJump = e.playPos.load();
    Page q; q.tempoSync = true; q.lengthBeats = 16; addArc(q, mkArc({ { 0, 60 }, { 1, 72 } })); Engine e2; e2.prepare(SR, 512); e2.publish(buildSnapshot(q, c));
    HostInfo hh; hh.bpm = 120; hh.ppq = 8.0; hh.playing = true; e2.process(L.data(), R.data(), 512, hh, false); const float hostPos = e2.playPos.load();
    check(lo >= 0.249f && hi <= 0.501f && std::abs(afterJump - 0.8f) < 0.01f && std::abs(hostPos - 0.5f) < 0.01f, "transport: loop keeps the playhead in " + juce::String(lo, 3) + ".." + juce::String(hi, 3) + ", jump lands at " + juce::String(afterJump, 3) + ", host ppq 8 of 16 beats -> " + juce::String(hostPos, 3));
}
static void testRealtime() {
    Page p = secPage(12.0); juce::Random r(7);
    for (int k = 0; k < 64; ++k) { Arc a = mkArc({ { 0.0f, 40.0f + r.nextFloat() * 50 }, { 0.5f, 40.0f + r.nextFloat() * 50 }, { 1.0f, 40.0f + r.nextFloat() * 50 } }, k % 3 == 0 ? ShapeSaw : ShapeSoft, 0.2f);
        a.env = { { 0, 0.5f }, { 0.5f, 1 }, { 1, 0.4f } }; a.pan = { { 0, -0.8f }, { 1, 0.8f } }; addArc(p, a); }
    for (int k = 1; k < 64; k += 2) { p.arcs[(size_t) k].fmSource = p.arcs[(size_t) k - 1].id; p.arcs[(size_t) k].fmDepth = 0.4f; }
    auto cpu = [&](const Page& pg, int& voices) { Engine e; e.prepare(SR, 512); SnapCache c; e.publish(buildSnapshot(pg, c)); e.followHost = false; e.internalPlay = true;
        std::vector<float> L(512), R(512); HostInfo h; h.bpm = 0; const int blocks = (int) (5.0 * SR / 512); e.process(L.data(), R.data(), 512, h, false);
        const double t0 = juce::Time::getMillisecondCounterHiRes(); for (int b = 0; b < blocks; ++b) e.process(L.data(), R.data(), 512, h, false); voices = e.activeVoices.load(); return 100.0 * (juce::Time::getMillisecondCounterHiRes() - t0) / 5000.0; };
    Page plain = p; for (auto& a : plain.arcs) a.fmSource = -1; int v1 = 0, v2 = 0, v3 = 0;
    const double cPlain = cpu(plain, v1), cFm = cpu(p, v2);
    Page fat = plain; for (auto& a : fat.arcs) { a.unison = 3; a.filter = FiltLP; } const double cFat = cpu(fat, v3);
    check(v1 == 64 && v2 == 64 && cPlain < 30 && cFm < 60, "CPU, 64 lines sounding at 48 kHz: plain " + juce::String(cPlain, 1) + "% of one core; 32 FM pairs (oversampled x4) " + juce::String(cFm, 1) + "%; unison 3 + filter on all " + juce::String(cFat, 1) + "%");
    Engine e; e.prepare(SR, 512); SnapCache c; e.publish(buildSnapshot(p, c)); e.followHost = false; e.internalPlay = true;
    std::atomic<bool> run { true }; std::atomic<int> processed { 0 };
    std::thread audio([&] { std::vector<float> l(256), rr(256); HostInfo hi; hi.bpm = 0; while (run.load()) { e.process(l.data(), rr.data(), 256, hi, false); processed.fetch_add(1); } });
    for (int i = 0; i < 400; ++i) { const size_t k = (size_t) i % p.arcs.size(); p.arcs[k].pts[1].y = 40.0f + (float) (i % 50); if (i % 10 == 0) p.arcs[k].setWave(waveShape(i % 6)); if (i % 50 == 0) p.arcs.erase(p.arcs.begin() + (i % 7));
        e.publish(buildSnapshot(p, c)); e.collect(); std::this_thread::sleep_for(std::chrono::microseconds(1000)); }
    run = false; audio.join(); e.collect();
    check(processed.load() > 50, "lock-free snapshot hand-off under load (400 publishes, lines deleted while sounding): the audio thread ran " + juce::String(processed.load()) + " blocks");
}

// ---------------------------------------------------------------- demos
static Page demoMelodic() {
    Page p; p.name = "Drawn melody"; p.tempoSync = true; p.ownBpm = 96; p.lengthBeats = 48; p.root = 9; p.scale = 1; p.master = 0.55f;
    const float PB = 48; using factory::applyPatch;
    const float chords[4][3] = { { 57, 60, 64 }, { 53, 57, 60 }, { 55, 60, 64 }, { 55, 59, 62 } };
    for (int rep = 0; rep < 3; ++rep) for (int c = 0; c < 4; ++c) for (int v = 0; v < 3; ++v) {   // pad chords, super-saw-ish and wide
        const float b0 = rep * 16 + c * 4.0f; Arc a; applyPatch(a, "Pad swell"); a.gain = 0.2f; a.colour = 4; a.pan = { { 0, (v - 1) * 0.5f }, { 1, (1 - v) * 0.4f } };
        a.pts = { { (b0 + 0.02f) / PB, chords[c][v] }, { (b0 + 4.2f) / PB, chords[c][v] + 0.06f } }; addArc(p, a); }
    const float roots[4] = { 45, 41, 48, 43 };
    for (int rep = 0; rep < 3; ++rep) for (int c = 0; c < 4; ++c) for (int h = 0; h < 2; ++h) {   // bass: square + sub through a filter, slides between roots
        const float b0 = rep * 16 + c * 4.0f + h * 2.0f; Arc a; applyPatch(a, "Square bass"); a.gain = 0.5f; a.colour = 5; a.env = { { 0, 0 }, { 0.03f, 1 }, { 0.5f, 0.7f }, { 0.95f, 0.5f }, { 1, 0 } };
        const float m = roots[c] - 12 + (h == 1 && c == 1 ? 7.0f : 0.0f); const float next = h == 1 ? roots[(c + 1) % 4] - 12 : m;
        a.pts = { { b0 / PB, m }, { (b0 + 1.6f) / PB, m }, { (b0 + 1.97f) / PB, next } }; addArc(p, a); }
    std::vector<std::pair<float, float>> mel = { { 16, 69 }, { 17.5f, 72 }, { 18, 71 }, { 19, 69 }, { 20, 65 }, { 22, 67 }, { 23, 69 }, { 24, 72 }, { 26, 76 }, { 27, 74 }, { 28, 71 }, { 30, 67 }, { 31, 0 },
                                                 { 32, 76 }, { 33.5f, 79 }, { 34, 77 }, { 35, 76 }, { 36, 72 }, { 38, 74 }, { 39, 76 }, { 40, 79 }, { 41, 77 }, { 42, 76 }, { 43, 74 }, { 44, 71 }, { 45, 72 }, { 46, 69 }, { 47.6f, 0 } };
    for (size_t i = 0; i < mel.size(); ++i) { if (mel[i].second <= 0) continue; const float b0 = mel[i].first, b1 = mel[i + 1].first; Arc a; applyPatch(a, "Soft lead"); a.gain = 0.4f; a.colour = 0; a.unison = 2; a.detune = 0.12f;
        const int steps = juce::jmax(2, (int) ((b1 - b0) * 8));
        for (int k = 0; k <= steps; ++k) { const float bb = b0 + (b1 - b0 - 0.02f) * k / steps; float y = mel[i].second; if (bb - b0 > 0.5f) y += 0.18f * std::sin((bb - b0) * 6.0f * 3.14159f) * juce::jmin(1.0f, bb - b0 - 0.5f);
            if (mel[i + 1].second > 0 && bb > b1 - 0.25f) { y = mel[i].second + (mel[i + 1].second - mel[i].second) * (bb - (b1 - 0.25f)) / 0.25f; } a.pts.push_back({ bb / PB, y }); }
        addArc(p, a); }
    std::vector<std::pair<float, float>> bell = { { 8, 81 }, { 10, 84 }, { 12, 88 }, { 14, 86 }, { 24, 81 }, { 26, 84 }, { 28, 83 }, { 30, 79 }, { 40, 88 }, { 42, 91 }, { 44, 88 }, { 46, 84 } };
    for (auto& b : bell) { Arc car; car.setWave(waveShape(ShapeSine)); car.gain = 0.28f; car.colour = 2; car.env = { { 0, 1 }, { 0.12f, 0.45f }, { 0.5f, 0.15f }, { 1, 0 } }; car.pan = { { 0, 0.6f }, { 1, -0.4f } };
        car.pts = { { b.first / PB, b.second }, { (b.first + 1.9f) / PB, b.second } }; const int cid = addArc(p, car);
        Arc mod; mod.setWave(waveShape(ShapeSine)); mod.gain = 0.9f; mod.modOnly = true; mod.colour = 2; mod.env = { { 0, 1 }, { 1, 0.1f } }; mod.pts = { { b.first / PB, b.second + 21.69f }, { (b.first + 1.9f) / PB, b.second + 21.69f } };
        const int mid = addArc(p, mod); p.find(cid)->fmSource = mid; p.find(cid)->fmDepth = 0.45f; }
    Arc arp; applyPatch(arp, "Pluck"); arp.gain = 0.22f; arp.colour = 1; arp.repeat = true; arp.rate = 1.0f; arp.pan = { { 0, -0.5f }, { 1, 0.5f } };   // one drawn cell, repeated
    const float cell[] = { 81, 84, 88, 84 }; for (int k = 0; k < 4; ++k) { arp.pts.push_back({ (32 + k * 0.5f) / PB, cell[k] }); arp.pts.push_back({ (32 + k * 0.5f + 0.45f) / PB, cell[k] }); }
    addArc(p, arp);
    p.clampAll(); return p;
}
static Page demoTexture() {
    Page p; p.name = "Glissando cloud"; p.tempoSync = false; p.lengthSeconds = 32; p.master = 0.38f; p.speedOn = true;
    p.speed = { { 0, -0.6f }, { 0.35f, 0.0f }, { 0.6f, 0.5f }, { 0.75f, -0.3f }, { 1, -0.8f } };
    juce::Random r(2026); const float target[] = { 38, 45, 50, 57, 62, 66, 69, 73, 74, 78, 81, 85 };
    for (int k = 0; k < 40; ++k) {
        Arc a; const float t = target[k % 12] + (k >= 24 ? 12.0f : 0.0f); const float s0 = 30 + r.nextFloat() * 70, s1 = 30 + r.nextFloat() * 70;
        const float xa = r.nextFloat() * 0.12f, xm = 0.42f + r.nextFloat() * 0.06f, xh = 0.58f + r.nextFloat() * 0.05f, xb = 0.86f + r.nextFloat() * 0.12f;
        a.pts = { { xa, s0 }, { xm, t }, { xh, t + (r.nextFloat() - 0.5f) * 0.2f }, { xb, s1 } };
        a.setWave(waveShape(k % 4 == 0 ? ShapeSaw : (k % 4 == 1 ? ShapeTriangle : ShapeSoft))); a.gain = 0.15f + r.nextFloat() * 0.08f; a.colour = k % 4 == 0 ? 0 : (k % 4 == 1 ? 1 : 4);
        if (k % 4 == 0) { a.filter = FiltLP; a.track = true; a.cutoff = { { 0, 0.55f }, { 0.5f, 0.8f }, { 1, 0.5f } }; }
        a.env = { { 0, 0 }, { 0.3f, 0.6f }, { 0.48f, 1 }, { 0.6f, 0.8f }, { 1, 0 } };
        const float pp = r.nextFloat() * 2 - 1; a.pan = { { 0, pp }, { 0.5f, -pp * 0.5f }, { 1, -pp } }; addArc(p, a);
    }
    for (int k = 0; k < 8; ++k) {   // FM-linked metallic sweeps (now oversampled: high index stays clean)
        const float x0 = 0.05f + k * 0.1f, x1 = x0 + 0.22f; if (x1 > 1) break;
        Arc car; car.setWave(waveShape(ShapeSine)); car.gain = 0.26f; car.colour = 2; car.env = { { 0, 0 }, { 0.15f, 1 }, { 1, 0 } }; car.pan = { { 0, (k % 2) ? -0.8f : 0.8f }, { 1, (k % 2) ? 0.8f : -0.8f } };
        car.pts = { { x0, 84.0f - k }, { x1, 64.0f - k } }; const int cid = addArc(p, car);
        Arc mod; mod.setWave(waveShape(ShapeSine)); mod.gain = 1.0f; mod.modOnly = true; mod.colour = 2; mod.pts = { { x0, 60.0f + k }, { x1, 88.0f + k } }; const int mid = addArc(p, mod);
        p.find(cid)->fmSource = mid; p.find(cid)->fmDepth = 0.7f;
    }
    Arc dr; factory::applyPatch(dr, "Reese"); dr.gain = 0.35f; dr.colour = 5; dr.env = { { 0, 0 }, { 0.2f, 0.8f }, { 0.8f, 1 }, { 1, 0 } }; dr.pts = { { 0.0f, 26 }, { 0.45f, 26 }, { 0.5f, 33 }, { 1.0f, 31 } };
    const int did = addArc(p, dr); Arc dm; dm.setWave(waveShape(ShapeSine)); dm.modOnly = true; dm.gain = 0.6f; dm.colour = 5; dm.pts = { { 0.0f, 38 }, { 1.0f, 50 } }; const int dmid = addArc(p, dm);
    p.find(did)->fmSource = dmid; p.find(did)->fmDepth = 0.3f;
    Arc nr; factory::applyPatch(nr, "Noise riser"); nr.gain = 0.3f; nr.colour = 3; nr.pts = makeCurve({ 0.5f, 50 }, { 0.75f, 100 }, 0); addArc(p, nr);   // a noise riser into the climax
    for (int k = 0; k < 16; ++k) { Arc a; a.setWave(waveShape(ShapePulse)); a.gain = 0.1f; a.colour = 3; const float x0 = 0.62f + r.nextFloat() * 0.3f; a.pts = { { x0, 96 + r.nextFloat() * 10 }, { x0 + 0.03f + r.nextFloat() * 0.04f, 80 + r.nextFloat() * 8 } };
        a.env = { { 0, 1 }, { 1, 0 } }; const float pp = r.nextFloat() * 2 - 1; a.pan = { { 0, pp }, { 1, pp } }; addArc(p, a); }
    p.clampAll(); return p;
}
static bool writeMp3(const std::vector<float>& L, const std::vector<float>& R, const juce::File& mp3) {
    const auto wavF = mp3.withFileExtension("wav"); wavF.deleteFile();
    { juce::WavAudioFormat wav; std::unique_ptr<juce::OutputStream> os(wavF.createOutputStream().release());
      auto* raw = wav.createWriterFor(os.get(), SR, 2, 24, {}, 0); if (!raw) return false; os.release(); std::unique_ptr<juce::AudioFormatWriter> w(raw);
      const float* ch[2] = { L.data(), R.data() }; w->writeFromFloatArrays(ch, 2, (int) L.size()); }
    mp3.deleteFile();
    juce::ChildProcess cp; if (!cp.start(juce::StringArray { "ffmpeg", "-loglevel", "error", "-y", "-i", wavF.getFullPathName(), "-codec:a", "libmp3lame", "-b:a", "224k", mp3.getFullPathName() })) return false;
    cp.waitForProcessToFinish(120000); wavF.deleteFile(); return mp3.existsAsFile() && mp3.getSize() > 10000;
}
static void makeDemos(const juce::File& dir) {
    dir.createDirectory();
    struct D { Page p; const char* name; double bpm; } ds[] = { { demoMelodic(), "01_arcs_drawn_melody.mp3", 96 }, { demoTexture(), "02_arcs_glissando_fm_cloud.mp3", 120 } };
    for (auto& d : ds) {
        std::vector<float> L, R; render(d.p, L, R, d.bpm); float pk = 0; const bool fin = allFinite(L, pk) && allFinite(R, pk);
        const double sec = L.size() / SR; const bool ok = writeMp3(L, R, dir.getChildFile(d.name));
        check(fin && ok && pk < 1.0f, juce::String("demo ") + d.name + ": " + juce::String((int) d.p.arcs.size()) + " lines, " + juce::String(sec, 1) + " s, peak " + juce::String(juce::Decibels::gainToDecibels(pk), 1) + " dBFS");
    }
}

// ---------------------------------------------------------------- screenshots
static void shot(juce::Component& c, const juce::File& f) {
    juce::MessageManager::getInstance()->runDispatchLoopUntil(120);
    auto img = c.createComponentSnapshot(c.getLocalBounds(), true, 1.0f); f.deleteFile(); juce::FileOutputStream os(f); juce::PNGImageFormat png; png.writeImageToStream(img, os);
    std::printf("  shot %s\n", f.getFullPathName().toRawUTF8());
}
static void makeShots(const juce::File& dir) {
    dir.createDirectory();
    ArcsHost host; host.prepare(SR, 512);
    host.edit() = demoMelodic(); host.commit("demo");
    host.selectSlot(1); host.edit() = factory::pages()[2].make(); host.commit("b"); host.selectSlot(0);
    {
        ArcsPage page(host, true); page.setSize(1200, 790); page.setVisible(true);
        std::vector<int> sel; for (auto& a : host.page().arcs) if (a.pts.front().x >= 16.0f / 48 && a.pts.front().x < 24.0f / 48 && a.colour == 0) sel.push_back(a.id);
        std::vector<float> L(512), R(512); HostInfo h; h.bpm = 0; host.engine.followHost = false; host.engine.internalPlay = true; host.engine.jumpQuantBeats = 0; host.engine.jumpTo = 0.4f;
        for (int b = 0; b < 30; ++b) host.process(L.data(), R.data(), 512, h, false);
        page.refresh(); page.fitView();
        page.selectTool(ArcsPage::Draw); page.setSelection(sel.empty() ? std::vector<int>() : std::vector<int> { sel.front() });
        shot(page, dir.getChildFile("arcs_light_melody.png"));
        page.selectTool(ArcsPage::Select); page.setSelection(sel); shot(page, dir.getChildFile("arcs_light_select_group.png"));
        page.setDark(true); shot(page, dir.getChildFile("arcs_dark_select_group.png"));
        host.edit() = demoTexture(); host.commit("texture"); page.refresh(); page.fitView(); page.setSelection({ host.page().arcs[0].id }); page.selectTool(ArcsPage::Draw);
        shot(page, dir.getChildFile("arcs_dark_glissando_fm.png"));
        page.setDark(false); shot(page, dir.getChildFile("arcs_light_glissando_fm.png"));
        host.edit() = factory::pages()[19].make(); host.commit("house"); page.refresh(); page.fitView(); page.setSelection({ host.page().arcs[0].id }); shot(page, dir.getChildFile("arcs_light_house_arp_repeat.png"));
        host.engine.internalPlay = false;
        host.edit() = Page(); host.commit("empty"); page.refresh(); shot(page, dir.getChildFile("arcs_light_empty.png"));
        page.setDark(true); shot(page, dir.getChildFile("arcs_dark_empty.png")); page.setDark(false);
    }
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    juce::File out = juce::File::getCurrentWorkingDirectory(); bool media = true;
    for (int i = 1; i < argc; ++i) { const juce::String a(argv[i]); if (a == "--no-media") media = false; else if (a.startsWith("/")) out = juce::File(a); }
    test440(); testGlide(); testReverse(); testFMTheory(); testQuantise(); testAliasing(); testZipper(); testMidiBasic(); testState(); testNaNFuzz(); testTransport();
    t01_nan(); t02_fmAlias(); t03_names(); t04_tempo(); t05_countIn(); t06_dive(); t07_overflow(); t08_mpeCC(); t09_render(); t10_limit(); t11_incremental(); t12_resolution(); t13_threadState();
    t14_grace(); t15_orphan(); t16_seam(); t17_fade(); t18_mono(); t19_hostile(); t20_knob(); t21_stamps(); t22_tooltips(); t24_shaping(); t25_factory(); t26_midiIn(); t27_autoGain(); t28_rate();
    t29_grid(); t30_clipBank(); t31_random(); t32_ids(); t33_ink(); t34_jump(); t35_noise(); t36_transport(); t38_rtl(); t39_nudge(); t41_exportGate(); t42_renderBuffer(); t43_fromState(); t44_rulerGestures();
    testRealtime();
    if (media) { makeDemos(out.getChildFile("demos")); makeShots(out.getChildFile("shots")); }
    std::printf("\n%s: %d passed, %d failure(s)\n", failures ? "FAILED" : "ALL PASSED", passes, failures);
    return failures ? 1 : 0;
}
