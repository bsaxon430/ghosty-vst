// Ghosty Spectrum ARRANGE page tests: sample-exact starts, reverse, pitch keep-length, fade shapes, stems null, morph endpoints,
// loop / jump transport, offline == realtime, state round trip + size, 64-clip CPU, NaN safety, MIDI, Shared Sounds, background
// render, undo; then screenshots (light + dark) and two MP3 demos of synthesised arrangements.
#include "ArrangeEngine.h"
#include "ArrangePage.h"
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <iostream>
#include <random>
#include <array>
#include <set>

using namespace arr;
static int fails = 0, passes = 0;
static void check(bool ok, const juce::String& what, const juce::String& detail = {}) {
    if (ok) ++passes; else ++fails;
    std::cout << (ok ? "  PASS  " : "  FAIL  ") << what << (detail.isNotEmpty() ? "   [" + detail + "]" : juce::String()) << std::endl;
}
static constexpr double SR = 48000.0;
static double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }
static double db(double x) { return 20.0 * std::log10(std::max(1e-30, x)); }
static float peakOf(const juce::AudioBuffer<float>& b) { float p = 0; for (int c = 0; c < b.getNumChannels(); ++c) p = std::max(p, b.getMagnitude(c, 0, b.getNumSamples())); return p; }
static bool finite(const juce::AudioBuffer<float>& b) { for (int c = 0; c < b.getNumChannels(); ++c) { auto* d = b.getReadPointer(c); for (int i = 0; i < b.getNumSamples(); ++i) if (!std::isfinite(d[i])) return false; } return true; }
static double peakHz(const float* x, int n, double sr) {   // 65536-point windowed spectrum peak, parabolic interpolation
    constexpr int order = 16, N = 1 << order; juce::dsp::FFT fft(order); std::vector<float> f((size_t) 2 * N, 0.0f);
    const int m = std::min(n, N); for (int i = 0; i < m; ++i) f[(size_t) i] = x[i] * (float) (0.5 - 0.5 * std::cos(2 * juce::MathConstants<double>::pi * i / m));
    fft.performFrequencyOnlyForwardTransform(f.data()); int k = 1; for (int i = 2; i < N / 2 - 1; ++i) if (f[(size_t) i] > f[(size_t) k]) k = i;
    const double a = f[(size_t) k - 1], b = f[(size_t) k], c = f[(size_t) k + 1], d = 0.5 * (a - c) / (a - 2 * b + c);
    return (k + d) * sr / N;
}
// ---- synthesised material (all original)
static juce::AudioBuffer<float> mono(int n) { juce::AudioBuffer<float> b(1, n); b.clear(); return b; }
static juce::AudioBuffer<float> sine(double hz, double secs, float amp = 0.5f) { auto b = mono((int) (secs * SR)); for (int i = 0; i < b.getNumSamples(); ++i) b.setSample(0, i, amp * (float) std::sin(2 * juce::MathConstants<double>::pi * hz * i / SR)); return b; }
static juce::AudioBuffer<float> noise(double secs, int seed, float amp = 0.4f) { auto b = mono((int) (secs * SR)); juce::Random r(seed); for (int i = 0; i < b.getNumSamples(); ++i) b.setSample(0, i, amp * (r.nextFloat() * 2 - 1)); return b; }
static juce::AudioBuffer<float> dc(double secs, float v) { auto b = mono((int) (secs * SR)); for (int i = 0; i < b.getNumSamples(); ++i) b.setSample(0, i, v); return b; }
static PayloadPtr P(const juce::AudioBuffer<float>& b, const juce::String& name, std::vector<Note> notes = {}) { return makePayload(b, SR, name, std::move(notes), { "Test", name, {} }); }
static Arrangement fresh(double bpm = 120) { Arrangement a; a.bpm = bpm; a.snapOn = false; return a; }
static juce::AudioBuffer<float> render(const Arrangement& a, Cache& c, bool stems = false, std::shared_ptr<RenderResult>* out = nullptr) {
    auto r = renderArrangement(a, SR, c, stems); if (out) *out = r; return r->mix;
}


static double mtof(double m) { return 440.0 * std::pow(2.0, (m - 69) / 12.0); }
static juce::AudioBuffer<float> stereo(int n) { juce::AudioBuffer<float> b(2, n); b.clear(); return b; }
static juce::AudioBuffer<float> padChord(std::vector<int> notes, double secs, int seed) {   // detuned saw-ish additive pad, slow swell
    auto b = stereo((int) (secs * SR)); juce::Random r(seed);
    for (int note : notes) for (int v = 0; v < 3; ++v) {
        const double f = mtof(note) * (1.0 + (v - 1) * 0.004); double ph = r.nextDouble(); const float pan = (v - 1) * 0.6f;
        for (int i = 0; i < b.getNumSamples(); ++i) {
            ph += f / SR; ph -= std::floor(ph); const double t = i / SR;
            double s = 0; for (int h = 1; h <= 9; ++h) s += std::sin(2 * juce::MathConstants<double>::pi * ph * h) / h * std::exp(-h * 0.18);
            const float env = (float) (std::min(1.0, t / (secs * 0.35)) * std::min(1.0, (secs - t) / (secs * 0.3)));
            const float y = (float) s * 0.035f * env; b.addSample(0, i, y * panLeft(pan)); b.addSample(1, i, y * panRight(pan));
        }
    }
    return b;
}
static juce::AudioBuffer<float> bell(int note, double secs) {   // two-operator FM bell
    auto b = stereo((int) (secs * SR)); const double f = mtof(note);
    for (int i = 0; i < b.getNumSamples(); ++i) { const double t = i / SR; const double m = std::sin(2 * juce::MathConstants<double>::pi * f * 3.5 * t) * 2.2 * std::exp(-t * 3);
        const float y = (float) (std::sin(2 * juce::MathConstants<double>::pi * f * t + m) * std::exp(-t * 2.2) * 0.32); b.setSample(0, i, y); b.setSample(1, i, y * 0.92f); }
    return b;
}
static juce::AudioBuffer<float> kick() { auto b = stereo((int) (SR * 0.4)); double ph = 0; for (int i = 0; i < b.getNumSamples(); ++i) { const double t = i / SR; ph += 2 * juce::MathConstants<double>::pi * (46 + 150 * std::exp(-t * 32)) / SR; const float y = (float) (0.9 * std::sin(ph) * std::exp(-t * 7)); b.setSample(0, i, y); b.setSample(1, i, y); } return b; }
static juce::AudioBuffer<float> hat(int seed) { auto b = stereo((int) (SR * 0.07)); juce::Random r(seed); float lp = 0; for (int i = 0; i < b.getNumSamples(); ++i) { const float n = r.nextFloat() * 2 - 1; lp += 0.85f * (n - lp); const float y = (n - lp) * 0.3f * (float) std::exp(-(i / SR) * 60); b.setSample(0, i, y); b.setSample(1, i, y * 0.8f); } return b; }
static juce::AudioBuffer<float> snare() { auto b = stereo((int) (SR * 0.25)); juce::Random r(5); for (int i = 0; i < b.getNumSamples(); ++i) { const double t = i / SR; const float y = (float) ((r.nextFloat() * 2 - 1) * 0.35 * std::exp(-t * 18) + 0.3 * std::sin(2 * juce::MathConstants<double>::pi * 190 * t) * std::exp(-t * 25)); b.setSample(0, i, y); b.setSample(1, i, y); } return b; }
static juce::AudioBuffer<float> beatLoop(double bpm, int bars) {   // one-bar-pattern drum loop rendered as a sound (like a Beats send)
    const double spb = SR * 60 / bpm; auto b = stereo((int) (bars * 4 * spb)); auto K = kick(), S = snare();
    auto add = [&](const juce::AudioBuffer<float>& x, double beat, float g) { const int at = (int) std::llround(beat * spb); for (int c = 0; c < 2; ++c) for (int i = 0; i < x.getNumSamples() && at + i < b.getNumSamples(); ++i) b.addSample(c, at + i, x.getSample(c, i) * g); };
    for (int bar = 0; bar < bars; ++bar) { const double o = bar * 4; add(K, o, 1); add(K, o + 2.5, 0.8f); add(S, o + 1, 0.8f); add(S, o + 3, 0.8f); for (int h = 0; h < 8; ++h) { auto H = hat(h + bar * 8); add(H, o + h * 0.5, h % 2 ? 0.5f : 0.8f); } }
    return b;
}
static juce::AudioBuffer<float> bassLine(double bpm, std::vector<int> notes, std::vector<double> lens, int seed) {   // acid-ish filtered saw, notes in beats
    double total = 0; for (double l : lens) total += l; const double spb = SR * 60 / bpm; auto b = stereo((int) (total * spb)); juce::ignoreUnused(seed);
    double pos = 0; float z1 = 0, z2 = 0;
    for (size_t k = 0; k < notes.size(); ++k) {
        const int a0 = (int) (pos * spb), a1 = std::min(b.getNumSamples(), (int) ((pos + lens[k]) * spb)); const double f = mtof(notes[k]); double ph = 0;
        for (int i = a0; i < a1; ++i) { const double t = (i - a0) / SR; ph += f / SR; ph -= std::floor(ph); const float saw = (float) (2 * ph - 1);
            const float cut = (float) (0.04 + 0.25 * std::exp(-t * 9)); z1 += cut * (saw - z1); z2 += cut * (z1 - z2);
            const float env = (float) (std::min(1.0, t * 400) * std::min(1.0, (a1 - i) / (SR * 0.01))); const float y = std::tanh(z2 * 2.2f) * 0.45f * env; b.setSample(0, i, y); b.setSample(1, i, y); }
        pos += lens[k];
    }
    return b;
}
static juce::AudioBuffer<float> sweepTexture(double secs, int seed) {   // band-passed noise sweep (a painted "spectral" texture stand-in)
    auto b = stereo((int) (secs * SR)); juce::Random r(seed); float lp[2] = { 0, 0 }, bp[2] = { 0, 0 };
    for (int i = 0; i < b.getNumSamples(); ++i) { const double t = i / secs / SR; const double fc = 300 * std::pow(20.0, 0.5 - 0.5 * std::cos(2 * juce::MathConstants<double>::pi * t));
        const float g = (float) (2 * std::sin(juce::MathConstants<double>::pi * fc / SR));
        for (int c = 0; c < 2; ++c) { const float n = r.nextFloat() * 2 - 1; const float hp = n - lp[c] - 0.15f * bp[c]; bp[c] += g * hp; lp[c] += g * bp[c]; b.setSample(c, i, bp[c] * 0.22f * (float) std::sin(juce::MathConstants<double>::pi * t)); } }
    return b;
}
static PayloadPtr PS(const juce::AudioBuffer<float>& b, const juce::String& name, const juce::String& page = "Test", std::vector<Note> notes = {}) { return makePayload(b, SR, name, std::move(notes), { page, name, {} }); }

// ------------------------------------------------------------------ engine tests

// ------------------------------------------------------------------ helpers
static juce::AudioBuffer<float> playBlocks(Session& s, int total, std::function<HostPos(juce::int64 done)> host, int block = 512) {
    juce::AudioBuffer<float> out(2, total); out.clear();
    for (int p = 0; p < total;) { const int n = std::min(block, total - p); juce::AudioBuffer<float> b(2, n); b.clear(); s.process(b, host ? host(p) : HostPos()); for (int c = 0; c < 2; ++c) out.copyFrom(c, p, b, c, 0, n); p += n; }
    return out;
}
static double maxDiff(const juce::AudioBuffer<float>& a, int a0, const juce::AudioBuffer<float>& b, int b0, int n) {
    double e = 0; for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) { if (a0 + i >= a.getNumSamples() || b0 + i >= b.getNumSamples()) return 1e9; e = std::max(e, (double) std::abs(a.getSample(c, a0 + i) - b.getSample(c, b0 + i))); }
    return e;
}
static double maxStep(const juce::AudioBuffer<float>& y, int from, int to) { double m = 0; for (int i = std::max(1, from); i < to && i < y.getNumSamples(); ++i) m = std::max(m, (double) std::abs(y.getSample(0, i) - y.getSample(0, i - 1))); return m; }
static void pump(int ms) { const double until = nowMs() + ms; while (nowMs() < until) juce::MessageManager::getInstance()->runDispatchLoopUntil(5); }
template <typename F> static bool pumpUntil(F&& f, int ms = 10000) { const double until = nowMs() + ms; while (nowMs() < until) { if (f()) return true; juce::MessageManager::getInstance()->runDispatchLoopUntil(5); } return f(); }
static juce::File testDir() { auto d = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("ghosty-arrange-tests"); d.createDirectory(); return d; }

// ------------------------------------------------------------------ engine tests (the original suite)
static Arrangement makeBig() {
    Arrangement a = fresh(128); for (int t = 0; t < 5; ++t) a.addTrack();
    auto pad = PS(padChord({ 57, 60, 64 }, 4.0, 1), "pad"), bl = PS(bell(72, 2.0), "bell"), lp = PS(beatLoop(128, 1), "beat"), tx = PS(sweepTexture(3.0, 2), "tex");
    for (int i = 0; i < 4; ++i) { auto* c = a.addClip(0, lp, i * 4.0); c->gainDb = -2; }
    { auto* c = a.addClip(1, pad, 0.0, 8.0); c->fadeIn = 2; c->fadeOut = 1; c->fadeOutCurve = Curve::SCurve; c->pan = -0.4f; c->morph = true; a.addClip(1, tx, 6.0, 6.0)->pan = 0.3f; }
    { auto* c = a.addClip(2, bl, 1.0); c->pitch = 7; c->reverse = true; auto* d = a.addClip(2, bl, 5.0); d->pitch = -5; d->keepLength = false; d->stretch = 1.5f; }
    a.tracks[1].gainDb = -3; a.tracks[2].pan = 0.5f; a.tracks[3].gainDb = 4; a.addClip(3, tx, 8.0)->reverse = true;
    a.tracks[3].volEnv = { { 8.0, 0.0f }, { 12.0, -12.0f } }; a.tracks[0].panEnv = { { 0.0, -0.5f }, { 16.0, 0.5f } };   // automation lanes ride along
    return a;
}
static void coreTests() {
    std::cout << "-- core" << std::endl;
    Cache cache;
    for (double bpm : { 120.0, 133.0 }) {   // a clip at bar 2 starts sample-exact
        auto imp = mono(4800); imp.setSample(0, 0, 0.75f); imp.setSample(0, 100, -0.5f);
        auto a = fresh(bpm); a.addClip(0, P(imp, "click"), 4.0);
        auto y = render(a, cache); const juce::int64 want = std::llround(4.0 * 60.0 / bpm * SR);
        int first = -1; for (int i = 0; i < y.getNumSamples(); ++i) if (y.getSample(0, i) != 0.0f) { first = i; break; }
        check(first == want && y.getSample(0, (int) want) == 0.75f && y.getSample(1, (int) want + 100) == -0.5f, "clip at bar 2 starts sample-exact at " + juce::String(bpm, 0) + " BPM", "first " + juce::String(first) + " want " + juce::String(want));
    }
    { auto src = noise(1.0, 3); auto a = fresh(); auto* c = a.addClip(0, P(src, "n"), 0.0); c->reverse = true;
      auto y = render(a, cache); const int n = src.getNumSamples(); src.makeCopyOf(a.tracks[0].clips[0].source->audio); double err = 0;
      for (int i = 0; i < n; ++i) err = std::max(err, (double) std::abs(y.getSample(0, i) - src.getSample(0, n - 1 - i)));
      check(err == 0.0, "reverse plays the sound time-reversed (exact)", "max err " + juce::String(err)); }
    { auto src = sine(220.0, 2.0); Payload p; p.audio.makeCopyOf(src); p.sampleRate = SR;
      auto k = processSound(p, 12.0f, true, 1.0f, false, SR); auto r = processSound(p, 12.0f, false, 1.0f, false, SR);
      const double fk = peakHz(k.getReadPointer(0) + 12000, k.getNumSamples() - 24000, SR), fr = peakHz(r.getReadPointer(0) + 4000, r.getNumSamples() - 8000, SR);
      check(k.getNumSamples() == src.getNumSamples() && std::abs(fk - 440.0) < 2.0, "pitch +12 keep-length = octave up, same length", juce::String(k.getNumSamples()) + " samples, " + juce::String(fk, 2) + " Hz");
      check(std::abs(r.getNumSamples() - src.getNumSamples() / 2) <= 1 && std::abs(fr - 440.0) < 1.0, "pitch +12 tape = octave up, half length", juce::String(r.getNumSamples()) + " samples, " + juce::String(fr, 2) + " Hz");
      const float rmsK = k.getRMSLevel(0, 12000, k.getNumSamples() - 24000), rmsS = src.getRMSLevel(0, 0, src.getNumSamples());
      check(std::abs(db(rmsK / rmsS)) < 1.5, "pitch shift keeps the level", juce::String(db(rmsK / rmsS), 2) + " dB");
      auto st = processSound(p, 0.0f, true, 2.0f, false, SR); const double fs = peakHz(st.getReadPointer(0) + 12000, st.getNumSamples() - 24000, SR);
      check(st.getNumSamples() == 2 * src.getNumSamples() && std::abs(fs - 220.0) < 1.5, "stretch x2 = twice as long, same pitch", juce::String(fs, 2) + " Hz");
      auto rs = processSound(p, 0.0f, true, 1.0f, false, 44100.0);
      check(std::abs(rs.getNumSamples() - (int) std::floor(src.getNumSamples() * 44100.0 / SR)) <= 1 && std::abs(peakHz(rs.getReadPointer(0) + 4000, rs.getNumSamples() - 8000, 44100.0) - 220.0) < 0.5, "a 48k sound plays at the right pitch in a 44.1k session"); }
    { bool ok = true; double worst = 0;
      for (int ci = 0; ci < (int) Curve::Count; ++ci) {
          auto a = fresh(); auto* c = a.addClip(0, P(dc(4.0, 0.5f), "dc"), 0.0, 4.0); c->fadeIn = 1.0; c->fadeOut = 2.0; c->fadeInCurve = c->fadeOutCurve = (Curve) ci;
          auto y = render(a, cache); const int fi = (int) (0.5 * SR), fo = (int) (1.0 * SR), L = (int) (2.0 * SR);
          for (int k = 0; k < L; ++k) { float g = 1; if (k < fi) g = fadeInGain((Curve) ci, (double) k / fi); if (k >= L - fo) g *= fadeInGain((Curve) ci, (double) (L - k) / fo); worst = std::max(worst, (double) std::abs(y.getSample(0, k) - 0.5f * g)); }
          ok = ok && worst < 1e-7;
      }
      check(ok, "fade in / out shapes are exact for all 4 curves", "max err " + juce::String(worst, 10));
      const bool shapes = std::abs(fadeInGain(Curve::EqualPower, 0.5) - std::sqrt(0.5f)) < 1e-6 && fadeInGain(Curve::Linear, 0.25) == 0.25f && std::abs(fadeInGain(Curve::SCurve, 0.5) - 0.5f) < 1e-7 && std::abs(fadeInGain(Curve::Exponential, 0.5) - 0.125f) < 1e-7;
      check(shapes, "fade curves: equal-power -3 dB at half, S-curve symmetric, exponential slow start"); }
    auto big = makeBig();
    { std::shared_ptr<RenderResult> r; auto mix = render(big, cache, true, &r);
      juce::AudioBuffer<float> sum(2, mix.getNumSamples()); sum.clear(); for (auto& s : r->stems) for (int c = 0; c < 2; ++c) sum.addFrom(c, 0, s, c, 0, mix.getNumSamples());
      check(r->stems.size() == 4 && maxDiff(sum, 0, mix, 0, mix.getNumSamples()) == 0.0, "stems sum to the mix exactly (no master processing; automation included)", juce::String((int) r->stems.size()) + " stems");
      auto a2 = big; a2.tracks[2].mute = true; std::shared_ptr<RenderResult> r2; render(a2, cache, true, &r2); check(r2->stems.size() == 3, "muted tracks leave the mix and the stems");
      auto a3 = big; a3.tracks[1].solo = true; std::shared_ptr<RenderResult> r3; render(a3, cache, true, &r3); check(r3->stems.size() == 1 && r3->stemNames[0] == big.tracks[1].name, "solo plays only the soloed track");
      check(finite(mix) && peakOf(mix) > 0.05f, "arrangement renders (finite, not silent)", "peak " + juce::String(peakOf(mix), 3)); }
    { auto A = noise(4.0, 11, 0.3f), B = sine(330, 4.0, 0.4f);
      auto a = fresh(); auto pa = P(A, "A"), pb = P(B, "B"); auto* ca = a.addClip(0, pa, 0.0, 6.0); ca->morph = true; a.addClip(0, pb, 4.0, 6.0);
      auto y = render(a, cache);
      auto alone = [&](bool f) { auto b = fresh(); if (f) b.addClip(0, pa, 0.0, 6.0); else b.addClip(0, pb, 4.0, 6.0); return render(b, cache); };
      auto ya = alone(true), yb = alone(false);
      const int o0 = (int) (2.0 * SR), o1 = (int) (3.0 * SR), edge = (int) (0.01 * SR);
      double eA = 0, eB = 0; for (int i = 0; i < edge; ++i) { eA = std::max(eA, (double) std::abs(y.getSample(0, o0 + i) - ya.getSample(0, o0 + i))); eB = std::max(eB, (double) std::abs(y.getSample(0, o1 - 1 - i) - yb.getSample(0, o1 - 1 - i))); }
      check(eA < 1e-5 && eB < 1e-5, "morph endpoints equal the clips (start = A, end = B)", "err A " + juce::String(db(eA), 1) + " dB, B " + juce::String(db(eB), 1) + " dB");
      check(maxDiff(y, 0, ya, 0, o0) == 0 && maxDiff(y, o1, yb, o1, (int) (2 * SR)) == 0, "outside the overlap the clips play untouched");
      const int mid = (o0 + o1) / 2, w = 4096; double dA = 0, dB = 0, e = 0;
      for (int i = mid - w; i < mid + w; ++i) { dA += std::pow(y.getSample(0, i) - ya.getSample(0, i), 2); dB += std::pow(y.getSample(0, i) - yb.getSample(0, i), 2); e += std::pow(y.getSample(0, i), 2); }
      check(dA > 0.1 * e && dB > 0.1 * e && finite(y) && e > 0, "morph middle is a blend of both");
      check(std::abs(peakHz(y.getReadPointer(0) + mid - 4096, 8192, SR) - 330) < 3, "morph keeps B's partial audible half-way"); }
    { Session s; s.arrangement = big; s.changed(); check(s.waitUntilUpToDate(), "builder thread publishes the edit");
      auto off = renderArrangement(big, SR, s.cache, false)->mix;
      std::mt19937 rng(7); s.setPlayheadBeats(0); s.play(); juce::AudioBuffer<float> rt(2, off.getNumSamples()); rt.clear();
      for (int p = 0; p < rt.getNumSamples();) { const int n = std::min(rt.getNumSamples() - p, 1 + (int) (rng() % 1500)); juce::AudioBuffer<float> blk(2, n); blk.clear(); s.process(blk, {}); for (int c = 0; c < 2; ++c) rt.copyFrom(c, p, blk, c, 0, n); p += n; }
      check(maxDiff(rt, 0, off, 0, off.getNumSamples()) == 0.0, "offline render == realtime playback (bit exact, random blocks)");
      s.stop(); const juce::int64 jumps[] = { 0, 30000, 120000, 50000 }; juce::AudioBuffer<float> hb(2, 48000); hb.clear(); int w = 0;
      for (auto j : jumps) for (int b = 0; b < 24; ++b) { juce::AudioBuffer<float> blk(2, 500); blk.clear(); HostPos h; h.valid = h.playing = true; h.bpm = big.bpm; h.timeSamples = j + b * 500; s.process(blk, h); for (int c = 0; c < 2; ++c) hb.copyFrom(c, w, blk, c, 0, 500); w += 500; }
      double ej = 0; w = 0; for (auto j : jumps) { ej = std::max(ej, maxDiff(hb, w, off, (int) j, 12000)); w += 12000; }
      check(ej == 0.0, "host transport jumps play the right samples (exact)");
      auto lp = big; lp.loopOn = true; lp.loopStart = 4; lp.loopEnd = 6; s.arrangement = lp; s.changed(); s.waitUntilUpToDate();
      const int L0 = (int) std::llround(4 * 60.0 / big.bpm * SR), L1 = (int) std::llround(6 * 60.0 / big.bpm * SR), len = L1 - L0;
      s.setPlayheadBeats(3.0); s.play(); const int start = (int) std::llround(3 * 60.0 / big.bpm * SR), total = len * 3;
      juce::AudioBuffer<float> lb(2, total); lb.clear(); for (int p = 0; p < total;) { const int n = std::min(total - p, 1 + (int) (rng() % 900)); juce::AudioBuffer<float> blk(2, n); blk.clear(); s.process(blk, {}); for (int c = 0; c < 2; ++c) lb.copyFrom(c, p, blk, c, 0, n); p += n; }
      double el = 0; for (int i = 0; i < total; ++i) { const juce::int64 t = start + i; const juce::int64 m = t < L1 ? t : L0 + (t - L0) % len; for (int c = 0; c < 2; ++c) el = std::max(el, (double) std::abs(lb.getSample(c, i) - off.getSample(c, (int) m))); }
      check(el == 0.0, "loop region wraps sample-exact (internal transport)"); s.stop(); }
    { // MIDI
      std::vector<Note> notes = { { 60, 1.0f, 0.0, 0.25 }, { 64, 0.8f, 0.5, 0.25 }, { 67, 0.6f, 1.0, 0.5 } };
      auto a = fresh(120); auto* c = a.addClip(0, P(sine(261.6, 2.0), "melody", notes), 8.0); c->pitch = 2; c->stretch = 2.0f; c->length = 8;
      a.addClip(1, P(noise(1.0, 1), "texture"), 0.0);
      auto m = arrangementMidi(a); std::vector<std::pair<double, int>> ons;
      for (int t = 0; t < m.getNumTracks(); ++t) for (auto* e : *m.getTrack(t)) if (e->message.isNoteOn()) ons.push_back({ e->message.getTimeStamp() / 960.0, e->message.getNoteNumber() });
      std::sort(ons.begin(), ons.end());
      check(ons.size() == 3 && std::abs(ons[0].first - 8.0) < 1e-6 && ons[0].second == 62 && std::abs(ons[1].first - 10.0) < 1e-6 && ons[1].second == 66 && std::abs(ons[2].first - 12.0) < 1e-6 && ons[2].second == 69, "MIDI drag-out: note clips only, at the clip's time, shifted and stretched");
      c->reverse = true; c->stretch = 1.0f; c->length = 4; auto m2 = arrangementMidi(a); double firstOn = 1e9; for (int t = 0; t < m2.getNumTracks(); ++t) for (auto* e : *m2.getTrack(t)) if (e->message.isNoteOn()) firstOn = std::min(firstOn, e->message.getTimeStamp() / 960.0);
      check(std::abs(firstOn - 9.0) < 1e-6, "reversed note clip: notes mirror inside the clip");
      c->reverse = false; c->loop = true; c->length = 12; check(midiNoteCount(arrangementMidi(a)) == 9, "looped note clip repeats its notes", juce::String(midiNoteCount(arrangementMidi(a)))); }
    { // state round trip + undo
      auto a = big; a.loopOn = true; a.loopStart = 2; a.loopEnd = 10; a.snapBeats = 0.5; a.tracks[3].name = "TEXTURES"; a.tracks[2].collapsed = true; a.tracks[3].showAuto = true; a.tracks[3].autoLane = 1;
      a.markers = { { 0, "INTRO" }, { 8, "DROP" } }; a.tracks[0].clips[0].gainEnv = { { 0, -12 }, { 2, 0 } }; a.tracks[0].clips[1].loop = true; a.tracks[1].clips[0].morphShape = MorphShape::Late; a.tracks[1].clips[0].morphSmear = 0.4f;
      auto mb = saveState(a); Arrangement b; juce::String err; const bool ok = loadState(mb.getData(), mb.getSize(), b, &err);
      bool same = ok && b.tracks.size() == a.tracks.size() && b.bpm == a.bpm && b.loopOn && b.loopStart == 2 && b.loopEnd == 10 && b.snapBeats == 0.5 && b.tracks[3].name == "TEXTURES" && b.tracks[2].collapsed && b.tracks[3].showAuto && b.tracks[3].autoLane == 1
          && b.markers.size() == 2 && b.markers[1].name == "DROP" && b.tracks[3].volEnv.size() == 2 && b.tracks[0].panEnv.size() == 2;
      for (size_t t = 0; same && t < a.tracks.size(); ++t) { same = same && a.tracks[t].clips.size() == b.tracks[t].clips.size() && a.tracks[t].gainDb == b.tracks[t].gainDb && a.tracks[t].pan == b.tracks[t].pan;
          for (size_t k = 0; same && k < a.tracks[t].clips.size(); ++k) { auto& x = a.tracks[t].clips[k]; auto& y = b.tracks[t].clips[k];
              same = x.id == y.id && x.start == y.start && x.length == y.length && x.offset == y.offset && x.pitch == y.pitch && x.keepLength == y.keepLength && x.stretch == y.stretch && x.reverse == y.reverse && x.warp == y.warp && x.loop == y.loop
                  && x.fadeIn == y.fadeIn && x.fadeOut == y.fadeOut && x.fadeInCurve == y.fadeInCurve && x.fadeOutCurve == y.fadeOutCurve && x.gainDb == y.gainDb && x.pan == y.pan && x.morph == y.morph && x.name == y.name
                  && x.gainEnv.size() == y.gainEnv.size() && x.morphShape == y.morphShape && x.morphSmear == y.morphSmear; } }
      std::set<const Payload*> distinct; for (auto& t : b.tracks) for (auto& c : t.clips) distinct.insert(c.source.get());
      check(same && distinct.size() == 4, "state round trip keeps every field (envelopes, lanes, markers, loop / warp, morph shape); duplicates share one sound", juce::String((int) mb.getSize() / 1024) + " KB");
      Cache c2; check(maxDiff(render(a, c2), 0, render(b, c2), 0, render(a, c2).getNumSamples()) == 0.0, "reloaded arrangement renders bit-identically (lossless 24-bit FLAC)");
      Arrangement z; check(!loadState("junk", 4, z, &err) && z.tracks.size() == 4, "garbage state is rejected, arrangement untouched");
      Session s; s.arrangement = a; const auto before = s.arrangement; s.arrangement.tracks[0].clips[0].gainDb = -12; s.commit(before);
      const auto b2 = s.arrangement; s.arrangement.splitClip(s.arrangement.tracks[1].clips[0].id, 3.0); s.commit(b2);
      const bool u1 = s.undo() && s.arrangement.tracks[1].clips.size() == a.tracks[1].clips.size(), u2 = s.undo() && s.arrangement.tracks[0].clips[0].gainDb == a.tracks[0].clips[0].gainDb;
      const bool r1 = s.redo() && s.arrangement.tracks[0].clips[0].gainDb == -12 && s.redo() && s.arrangement.tracks[1].clips.size() == a.tracks[1].clips.size() + 1;
      check(u1 && u2 && r1 && !s.redo(), "undo / redo (edit, split)"); }
    { auto a = fresh(); auto* c = a.addClip(0, P(noise(2.0, 9), "n"), 0.0, 4.0); const auto id = c->id; auto y0 = render(a, cache);
      a.splitClip(id, 1.5); a.splitClip(a.tracks[0].clips[1].id, 2.75); auto y1 = render(a, cache);
      check(a.tracks[0].clips.size() == 3 && maxDiff(y0, 0, y1, 0, y0.getNumSamples()) == 0.0, "split into 3 plays exactly like the whole clip (no declick at seamless joins)");
      auto b = fresh(); auto* d = b.addClip(0, P(noise(1.0, 2), "n2"), 0.0); b.duplicateClip(d->id, 8.0); auto y = render(b, cache);
      check(b.tracks[0].clips.size() == 2 && maxDiff(y, 0, y, (int) (4 * SR), (int) SR) == 0.0, "duplicate plays the same sound at its new place"); }
    { // 64 clips: realtime CPU
      auto a = fresh(140); for (int t = 0; t < 7; ++t) a.addTrack(); std::vector<PayloadPtr> snd;
      for (int k = 0; k < 8; ++k) snd.push_back(PS(k % 2 ? sweepTexture(3.0, k) : padChord({ 48 + k, 55 + k }, 3.0, k), "s" + juce::String(k)));
      for (int k = 0; k < 64; ++k) { auto* c = a.addClip(k % 8, snd[(size_t) (k % 8)], (k / 8) * 2.0); c->fadeIn = 0.5; c->fadeOut = 1.0; c->pan = (k % 5 - 2) * 0.3f; c->reverse = k % 3 == 0; c->pitch = (float) (k % 4 - 2); if (k % 5 == 0) c->gainEnv = { { 0, -6 }, { 1, 0 } }; }
      a.tracks[0].volEnv = { { 0, 0 }, { 16, -6 } };
      Session s; s.arrangement = a; const double tb = nowMs(); s.changed(); s.waitUntilUpToDate(60000); const double buildMs = nowMs() - tb;
      s.setPlayheadBeats(0); s.play(); const int blk = 256, nBlocks = (int) (12.0 * SR / blk); juce::AudioBuffer<float> b(2, blk);
      int overlapMax = 0; s.withSnapshot([&](const Snapshot* cur) { if (cur) for (juce::int64 t = 0; t < cur->endSample; t += 4800) { int o = 0; for (auto& it : cur->items) if (it.start <= t && it.end > t) ++o; overlapMax = std::max(overlapMax, o); } });
      const double t0 = nowMs(); for (int i = 0; i < nBlocks; ++i) { b.clear(); s.process(b, {}); } const double ms = nowMs() - t0;
      const double cpu = ms / (nBlocks * blk / SR * 1000.0) * 100.0;
      check(a.clipCount() == 64 && cpu < 5.0, "64 clips play at " + juce::String(cpu, 2) + "% of one core (" + juce::String(overlapMax) + " items at once, with envelopes + meters)", "first build " + juce::String(buildMs / 1000.0, 2) + " s, blocks of 256");
      auto c2 = a; c2.tracks[3].clips[2].start += 0.5; s.arrangement = c2; const double tm = nowMs(); s.changed(); s.waitUntilUpToDate(); const double moveMs = nowMs() - tm;
      check(moveMs < 300, "moving a clip re-publishes in " + juce::String(moveMs, 1) + " ms (processed sounds cached)"); }
    { // no NaN
      auto bad = noise(1.0, 3); bad.setSample(0, 100, std::numeric_limits<float>::quiet_NaN()); bad.setSample(0, 200, std::numeric_limits<float>::infinity());
      Payload raw; raw.audio.makeCopyOf(bad); raw.sampleRate = 22050; auto praw = std::make_shared<const Payload>(std::move(raw));
      auto a = fresh(400); a.addClip(0, P(bad, "bad"), 0.0); auto* c = a.addClip(0, praw, 0.01); c->pitch = 24; c->keepLength = false; c->stretch = 0.25f; c->morph = true;
      auto* d = a.addClip(0, P(sine(5000, 0.01), "tiny"), 0.02, kMinClipBeats); d->pitch = -24; d->stretch = 4; d->fadeIn = 10; d->fadeOut = 10; d->morph = true; d->loop = true;
      auto* e = a.addClip(1, P(dc(0.5, 1.0f), "dc"), 0.0, 64.0); e->gainDb = 24; e->pan = 1; e->loop = true; a.tracks[1].gainDb = 12; a.addClip(1, P(noise(0.001, 2), "1ms"), 0.0)->reverse = true;
      auto y = render(a, cache); check(finite(y), "no NaN / Inf from broken input or extreme settings");
      auto mb = saveState(a); Arrangement b; check(loadState(mb.getData(), mb.getSize(), b) && finite(render(b, cache)), "state with broken input saves and reloads finite"); }
    { // 5-minute state size
      auto a = fresh(124); for (int t = 0; t < 4; ++t) a.addTrack();
      std::vector<PayloadPtr> snd = { PS(beatLoop(124, 2), "beat"), PS(bassLine(124, { 36, 36, 48, 39, 36, 43 }, { 1, 0.5, 0.5, 1, 0.5, 0.5 }, 1), "bass"), PS(padChord({ 60, 63, 67, 70 }, 7.74, 2), "pad"),
                                      PS(bell(79, 3.0), "bell"), PS(sweepTexture(7.74, 4), "sweep"), PS(padChord({ 55, 58, 62 }, 7.74, 3), "pad 2") };
      double rawSec = 0; for (auto& p : snd) rawSec += p->seconds();
      for (int bar = 0; bar < 155; bar += 2) { a.addClip(0, snd[0], bar * 4.0); a.addClip(1, snd[1], bar * 4.0)->pitch = (float) ((bar / 8) % 3 == 1 ? 5 : 0);
          if (bar % 4 == 0) { auto* c = a.addClip(2, snd[bar % 8 == 0 ? 2 : 5], bar * 4.0, 16.0); c->fadeIn = 2; c->fadeOut = 4; }
          if (bar % 8 == 4) a.addClip(3, snd[3], bar * 4.0 + 2)->reverse = bar % 16 == 4; if (bar % 16 == 0) a.addClip(4, snd[4], bar * 4.0, 16.0); }
      const double minutes = a.beatsToSeconds(a.lengthBeats()) / 60.0; auto mb = saveState(a); const double rawFloatB64 = rawSec * SR * 2 * 4 * 4.0 / 3.0;
      check(minutes > 4.9 && mb.getSize() < 6u * 1024 * 1024 && mb.getSize() < rawFloatB64 * 0.5, "5-minute arrangement (" + juce::String(a.clipCount()) + " clips) saves in " + juce::String(mb.getSize() / 1024.0 / 1024.0, 2) + " MB", "raw-float base64 of the sounds: " + juce::String(rawFloatB64 / 1048576.0, 1) + " MB"); }
}

// ------------------------------------------------------------------ punch-list regressions (one per item)
static void punchTests() {
    std::cout << "-- punch list" << std::endl;
    Cache cache;
    // 1: host position in beats
    { Session s; auto a = fresh(120); auto* c = a.addClip(0, P(noise(2.0, 21), "late"), 256.0); juce::ignoreUnused(c); s.arrangement = a; s.changed(); s.waitUntilUpToDate();
      HostPos h; h.valid = true; h.playing = false; h.bpm = 128; juce::AudioBuffer<float> blk(2, 64); s.process(blk, h);
      bool retimed = false; for (int i = 0; i < 400 && !retimed; ++i) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); s.withSnapshot([&](const Snapshot* x) { retimed = x && std::abs(x->bpm - 128) < 1e-9; }); }
      auto a128 = a; a128.bpm = 128; auto off = renderArrangement(a128, SR, s.cache, false)->mix;
      // the host played 64 beats at 120, then switched to 128: timeInSamples says beat 273.07 at 128, ppq says 256
      const double ppq0 = 256.0; const juce::int64 tSamples = (juce::int64) (64 * 60.0 / 120 * SR + (ppq0 - 64) * 60.0 / 128 * SR);
      auto y = playBlocks(s, 24000, [&](juce::int64 d) { HostPos q; q.valid = q.playing = true; q.bpm = 128; q.hasPpq = true; q.ppq = ppq0 + d / (60.0 / 128 * SR); q.timeSamples = tSamples + d; return q; });
      const int at = (int) std::llround(ppq0 * 60.0 / 128 * SR);
      check(retimed && maxDiff(y, 0, off, at, 24000) < 1e-6 && peakOf(y) > 0.1f, "1 BLOCKER: host position in beats (ppq): after a 120 -> 128 change the clip at beat 256 plays on time", "diff " + juce::String(maxDiff(y, 0, off, at, 24000))); }
    // 2: sample-rate change is not silent
    { Session s; auto a = fresh(120); auto* c = a.addClip(0, P(sine(300, 6.0), "pitched"), 0.0); c->pitch = 5; s.arrangement = a; s.changed(); s.waitUntilUpToDate(30000);
      const double t0 = nowMs(); s.prepare(96000, 512); const double prepMs = nowMs() - t0; s.setPlayheadBeats(0); s.play();
      auto y = playBlocks(s, 9600, {}); auto off = renderArrangement(a, 96000, s.cache, false)->mix;
      check(peakOf(y) > 0.1f && maxDiff(y, 0, off, 0, 9600) == 0.0, "2 BLOCKER: prepare(96000) then play at once: no silence, exact 96 k audio", "prepare took " + juce::String(prepMs, 0) + " ms");
      s.stop(); HostPos h; h.valid = h.playing = true; h.nonRealtime = true; h.bpm = 120; s.prepare(44100, 512); auto y2 = playBlocks(s, 4410, [&](juce::int64 d) { HostPos q = h; q.timeSamples = d; return q; });
      check(peakOf(y2) > 0.1f, "2: an offline bounce at 44.1 k starts with audio"); }
    // 3: automatic declick at cut edges
    { auto src = sine(200, 2.0, 0.8f); auto a = fresh(); auto* c = a.addClip(0, P(src, "sine"), 1.0, 1.0); c->offset = 1.0 / 800.0;   // starts at a sine peak, ends mid-wave
      auto y = render(a, cache); const int S = (int) (0.5 * SR), E = S + (int) (0.5 * SR);
      const double stepIn = maxStep(y, S - 2, S + 400), stepOut = maxStep(y, E - 400, E + 2), smooth = 2 * 3.1416 * 200 / SR * 0.8;
      check(stepIn < smooth * 1.6 && stepOut < smooth * 1.6, "3: cut clip edges get a 3 ms declick (no jump)", "steps " + juce::String(stepIn, 4) + " / " + juce::String(stepOut, 4) + " vs signal " + juce::String(smooth, 4));
      auto a2 = fresh(); auto t2 = P(noise(2.0, 4), "n3"); auto* tc = a2.addClip(0, t2, 2.0, 2.0); tc->offset = 0.25; auto yt = render(a2, cache); const auto& sa = tc->source->audio; const int X = (int) std::llround(kMicroFadeSeconds * SR);
      double et = 0; for (int i = X; i < (int) SR - X; ++i) et = std::max(et, (double) std::abs(yt.getSample(0, (int) SR + i) - sa.getSample(0, i + (int) (0.25 * SR))));
      check(et == 0.0 && std::abs(yt.getSample(0, (int) SR)) < 1e-6f, "3: a trimmed clip plays its window exactly between the declicks"); }
    // 4: LOOP does not wrap host time
    { Session s; auto a = makeBig(); a.loopOn = true; a.loopStart = 4; a.loopEnd = 6; s.arrangement = a; s.changed(); s.waitUntilUpToDate();
      auto off = renderArrangement(a, SR, s.cache, false)->mix; const int L1 = (int) std::llround(6 * 60.0 / a.bpm * SR);
      auto y = playBlocks(s, 12000, [&](juce::int64 d) { HostPos q; q.valid = q.playing = true; q.bpm = a.bpm; q.timeSamples = L1 - 3000 + d; return q; });
      check(maxDiff(y, 0, off, L1 - 3000, 12000) == 0.0, "4: while the host plays, the ARRANGE loop is ignored (Arrange follows the DAW)"); }
    // 5: huge positions are capped
    { auto a = fresh(); auto* c = a.addClip(0, P(noise(0.5, 1), "far"), 1e9, 1e12); auto t = toTree(a); t.getChild(1).getChild(0).setProperty("start", 1e18, nullptr);
      Arrangement b; const bool ok = fromTree(t, b); const auto* bc = ok && !b.tracks.empty() && !b.tracks[0].clips.empty() ? &b.tracks[0].clips[0] : nullptr;
      check(c && c->start <= kMaxBeats && c->length <= kMaxBeats && bc && bc->start <= kMaxBeats && bc->length <= kMaxBeats, "5: clip positions / lengths are capped (UI edits and loaded state)");
      a.moveClip(c->id, 0, 1e300); check(a.tracks[0].clips[0].start <= kMaxBeats, "5: moving a clip past the cap is clamped");
      Arrangement far = fresh(); far.addClip(0, P(noise(0.2, 2), "x"), kMaxBeats - 4); far.loopOn = true; far.loopStart = kMaxBeats - 4; far.loopEnd = kMaxBeats;
      auto r = renderArrangement(far, SR, cache, false, 0, nullptr, {}, true); check(!r->failed && r->mix.getNumSamples() <= (int) (8 * 60.0 / 120 * SR) + 2 && peakOf(r->mix) > 0.01f, "5: a loop render at the far end renders just the loop (no giant buffers)"); }
    // 6: forged FLAC lengths
    { auto snd = noise(0.2, 5); auto mb = encodeFlac(snd, SR, 0); auto patch = [&](juce::uint64 total) { juce::MemoryBlock m(mb); auto* d = static_cast<juce::uint8*>(m.getData());
          juce::uint64 v = 0; for (int i = 0; i < 8; ++i) v = (v << 8) | d[18 + i]; v = (v & ~((1ULL << 36) - 1)) | (total & ((1ULL << 36) - 1)); for (int i = 7; i >= 0; --i) { d[18 + i] = (juce::uint8) (v & 0xff); v >>= 8; } return m; };
      juce::AudioBuffer<float> out; double rate = 0; const bool okReal = decodeFlac(mb, out, rate, 0) && out.getNumSamples() == snd.getNumSamples();
      const double t0 = nowMs(); const bool a1 = decodeFlac(patch(1ULL << 31), out, rate, 0), a2 = decodeFlac(patch(1ULL << 30), out, rate, 0); const double ms = nowMs() - t0;
      check(okReal && !a1 && !a2 && ms < 500, "6: forged FLAC lengths (2^31, 2^30) are rejected without allocating", juce::String(ms, 0) + " ms");
      auto a = fresh(); a.addClip(0, P(snd, "s"), 0); auto t = toTree(a); auto sd = t.getChildWithName("sounds").getChild(0); sd.setProperty("flac", juce::var(patch(1ULL << 31)), nullptr);
      Arrangement b; juce::MemoryBlock st; { juce::MemoryOutputStream os(st, false); t.writeToStream(os); } check(loadState(st.getData(), st.getSize(), b) && b.clipCount() == 0, "6: a state with a forged sound loads (that sound dropped), no crash"); }
    // 7: render memory: streamed to files, stems only when asked
    { auto a = makeBig(); RenderRequest rq; rq.dir = testDir().getChildFile("r7"); rq.dir.deleteRecursively(); auto r = renderToFiles(a, SR, cache, rq);
      auto mem = renderArrangement(a, SR, cache, false)->mix; juce::AudioFormatManager fm; fm.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> rd(fm.createReaderFor(r->mixFile));
      juce::AudioBuffer<float> back(2, mem.getNumSamples()); if (rd) rd->read(&back, 0, mem.getNumSamples(), 0, true, true);
      check(r->mixFile.existsAsFile() && r->stemFiles.isEmpty() && r->mix.getNumSamples() == 0 && rd && maxDiff(back, 0, mem, 0, mem.getNumSamples()) == 0.0, "7: RENDER streams the mix to a float WAV (no stems unless asked, nothing kept in RAM)");
      rq.stems = true; rq.mix = false; auto r2 = renderToFiles(a, SR, cache, rq); check(r2->stemFiles.size() == 4 && !r2->mixFile.exists(), "7: stems render to one file each when asked"); rq.dir.deleteRecursively(); }
    // 8: NaN in the state
    { auto a = fresh(); auto* c = a.addClip(0, P(noise(0.5, 3), "n"), 2.0); c->gainEnv = { { 0, 0 } }; a.markers = { { 1, "M" } };
      auto t = toTree(a); const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
      t.setProperty("bpm", nan, nullptr); t.setProperty("loopStart", inf, nullptr); t.setProperty("snapBeats", nan, nullptr);
      auto tr = t.getChild(2); tr.setProperty("gainDb", nan, nullptr); tr.setProperty("pan", inf, nullptr);
      auto ck = tr.getChild(0); for (auto* p : { "start", "length", "offset", "pitch", "stretch", "fadeIn", "fadeOut", "gainDb", "pan", "morphAmount", "morphSmear" }) ck.setProperty(p, nan, nullptr);
      t.getChildWithName("markers").getChild(0).setProperty("beat", nan, nullptr);
      Arrangement b; const bool ok = fromTree(t, b); bool fin = ok && std::isfinite(b.bpm) && std::isfinite(b.loopStart) && std::isfinite(b.snapBeats) && b.markers.empty();
      for (auto& x : b.tracks) { fin = fin && std::isfinite(x.gainDb) && std::isfinite(x.pan); for (auto& k : x.clips) fin = fin && std::isfinite(k.start) && std::isfinite(k.length) && std::isfinite(k.offset) && std::isfinite(k.pitch) && std::isfinite(k.stretch) && std::isfinite(k.fadeIn) && std::isfinite(k.gainDb) && std::isfinite(k.pan) && std::isfinite(k.morphAmount); }
      check(fin && b.clipCount() == 1 && finite(render(b, cache)), "8: NaN / Inf in every state field load as safe defaults"); }
    // 9: per-instance re-render registry
    { int ca = 0, cb = 0; auto s1 = std::make_unique<Session>(); Session s2;
      auto r1 = s1->registerSource("ARCS", [&](const SourceRef& r) { ++ca; return makePayload(sine(440, 0.3), SR, "A", {}, r); });
      auto r2 = s2.registerSource("ARCS", [&](const SourceRef& r) { ++cb; return makePayload(sine(550, 0.3), SR, "B", {}, r); });
      SourceRef ref; ref.page = "ARCS";
      const bool sep = s1->rerender(ref) && s2.rerender(ref) && ca == 1 && cb == 1;
      r1.reset(); const bool gone = s1->rerender(ref) == nullptr && s2.rerender(ref) != nullptr;
      auto r3 = s1->registerSource("VOICE", [&](const SourceRef&) { return PayloadPtr(); }); s1.reset(); r3.reset();   // the token outlives its session: no crash
      check(sep && gone, "9: re-render registry is per instance; a dropped token unregisters only there"); }
    // 10: state from host threads
    { Session s; s.arrangement = makeBig(); s.changed(); s.waitUntilUpToDate();
      std::atomic<bool> stop { false }; std::atomic<int> saves { 0 }; bool allOk = true;
      std::thread host([&] { while (!stop.load()) { auto mb = s.getState(); Arrangement x; if (!loadState(mb.getData(), mb.getSize(), x)) allOk = false; ++saves; } });
      for (int i = 0; i < 60; ++i) { const auto b = s.arrangement; s.arrangement.tracks[0].clips[0].gainDb = (float) -(i % 10); s.arrangement.moveClip(s.arrangement.tracks[2].clips[0].id, 2, 1.0 + (i % 4)); s.commit(b); pump(2); }
      stop.store(true); host.join();
      auto blob = saveState(fresh(97)); bool setOk = false; std::thread loader([&] { setOk = s.setState(blob.getData(), blob.getSize()); }); loader.join();
      const bool audioFirst = s.waitUntilUpToDate(5000); bool snapAt97 = false; s.withSnapshot([&](const Snapshot* x) { snapAt97 = x && x->numTracks == 4; });
      const bool docLater = s.arrangement.bpm != 97; pump(50);
      check(allOk && saves.load() >= 2 && setOk && audioFirst && snapAt97 && docLater && s.arrangement.bpm == 97, "10: getState / setState are safe from a host thread (applied on the message thread)", juce::String(saves.load()) + " saves during edits"); }
    // 11: send / import off the message thread
    { Session::setSharedSoundsDir(testDir().getChildFile("shared11")); Session s; s.arrangement = makeBig(); s.changed(); s.waitUntilUpToDate();
      bool done = false; juce::Array<juce::File> files; const double t0 = nowMs(); s.sendToBeatsAsync(0, [&](juce::Array<juce::File> f, juce::String) { files = f; done = true; }); const double callMs = nowMs() - t0;
      pumpUntil([&] { return done; }, 30000);
      std::vector<PayloadPtr> got; bool loaded = false; if (!files.isEmpty()) s.loadSoundsAsync({ files[0].getFullPathName() }, "Shared", [&](std::vector<PayloadPtr> p, juce::StringArray) { got = p; loaded = true; });
      pumpUntil([&] { return loaded; }, 30000);
      check(done && files.size() == 1 && callMs < 20 && loaded && got.size() == 1, "11: SEND TO BEATS and import run on a worker (the call returns at once)", juce::String(callMs, 1) + " ms on the message thread");
      testDir().getChildFile("shared11").deleteRecursively(); Session::setSharedSoundsDir({}); }
    // 12: morph with B inside A
    { auto a = fresh(); auto pA = P(sine(220, 6.0, 0.5f), "A"); auto* ca = a.addClip(0, pA, 0.0, 12.0); ca->morph = true; a.addClip(0, P(noise(1.5, 6, 0.3f), "B"), 3.0, 3.0);
      auto y = render(a, cache); const int bEnd = (int) (3.0 * SR); const double step = maxStep(y, bEnd - 2000, bEnd + 2000);
      auto alone = fresh(); alone.addClip(0, pA, 0.0, 12.0); auto ya = render(alone, cache);
      check(step < 0.08 && maxDiff(y, bEnd + 10, ya, bEnd + 10, (int) SR) < 1e-6, "12: MORPH with the next clip inside: morphs in and back out, A resumes without a step", "max step " + juce::String(step, 3)); }
    // 13: tempo-tagged loops WARP
    { auto loop = PS(beatLoop(120, 1), "loop120"); auto a = fresh(140); auto* c = a.addClip(0, makePayload(beatLoop(120, 1), SR, "loop", {}, { "Shared", "loop", {} }, 120.0), 0.0);
      Buf b = c ? cache.get(*c, SR, 140) : nullptr; const int want = (int) std::llround(4 * 60.0 / 140 * SR);
      check(c && c->warp && std::abs(c->length - 4.0) < 1e-6 && b && std::abs(b->getNumSamples() - want) <= 2, "13: a 1-bar loop made at 120 stays 4 beats long at 140 (WARP)", juce::String(c ? c->length : 0, 4) + " beats, " + juce::String(b ? b->getNumSamples() : 0) + " samples");
      Session s; s.arrangement = a; s.changed(); s.waitUntilUpToDate(); HostPos h; h.valid = true; h.bpm = 100; juce::AudioBuffer<float> blk(2, 64); s.process(blk, h);
      bool ok = false; for (int i = 0; i < 600 && !ok; ++i) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); s.withSnapshot([&](const Snapshot* x) { ok = x && std::abs(x->bpm - 100) < 1e-9 && !x->stale && !x->items.empty() && std::abs(x->items[0].buf->getNumSamples() - (int) std::llround(4 * 60.0 / 100 * SR)) <= 2; }); }
      juce::ignoreUnused(loop); check(ok, "13: the warped loop follows a host tempo change (100 BPM)"); }
    // 14: clip gain envelope + track automation
    { auto a = fresh(); auto* c = a.addClip(0, P(dc(2.0, 0.5f), "dc"), 0.0, 4.0); c->gainEnv = { { 0.0, -60.0f }, { 2.0, 0.0f } };
      auto y = render(a, cache); const float g0 = juce::Decibels::decibelsToGain(-60.0f); const double mid = 0.5f * (g0 + (1 - g0) * 0.5);
      check(std::abs(y.getSample(0, (int) (0.5 * SR)) - mid) < 1e-4 && std::abs(y.getSample(0, (int) (1.5 * SR)) - 0.5f) < 1e-6, "14: clip gain envelope (linear between points, holds after the last)");
      auto t = fresh(); t.addClip(0, P(dc(2.0, 0.5f), "dc"), 0.0, 4.0); t.tracks[0].volEnv = { { 0.0, 0.0f }, { 4.0, -6.0f } }; t.tracks[0].panEnv = { { 0.0, -1.0f }, { 4.0, 1.0f } };
      auto yt = render(t, cache); const float g6 = juce::Decibels::decibelsToGain(-6.0f); const float gm = 0.5f * (1 + g6);
      check(std::abs(yt.getSample(0, (int) SR) - 0.5f * gm * panLeft(0)) < 1e-4 && yt.getSample(1, 10) < 1e-3f && yt.getSample(0, (int) (2 * SR) - 10) < 1e-3f, "14: track VOLUME and PAN automation lanes"); }
    // 15: bounce in place
    { Session s; s.arrangement = fresh(); auto& a = s.arrangement; auto* c1 = a.addClip(0, P(noise(1.0, 7), "n1"), 0.0); c1->gainDb = -3; c1->fadeOut = 0.5; const auto i1 = c1->id; auto* c2 = a.addClip(0, P(sine(500, 1.0), "s1"), 2.0); c2->reverse = true; const auto i2 = c2->id;
      s.changed(); auto before = renderArrangement(a, SR, s.cache, false)->mix; const auto id = s.bounce({ i1, i2 }); auto after = renderArrangement(s.arrangement, SR, s.cache, false)->mix;
      check(id && s.arrangement.clipCount() == 1 && maxDiff(before, 0, after, 0, std::min(before.getNumSamples(), after.getNumSamples())) < 1e-6, "15: bounce in place: one clip, same sound", "id " + juce::String((int) id) + " clips " + juce::String(s.arrangement.clipCount()) + " diff " + juce::String(maxDiff(before, 0, after, 0, std::min(before.getNumSamples(), after.getNumSamples()))) + " len " + juce::String(before.getNumSamples()) + "/" + juce::String(after.getNumSamples()));
      s.undo(); check(s.arrangement.clipCount() == 2, "15: bounce undoes"); }
    // 16: clip content loop
    { auto src = noise(0.5, 8); auto a = fresh(); auto* c = a.addClip(0, P(src, "loop"), 0.0, 4.0); c->loop = true; auto y = render(a, cache);
      const int L = (int) (0.5 * SR), X = (int) std::llround(kMicroFadeSeconds * SR);
      check(y.getNumSamples() >= 4 * L && maxDiff(y, L + X, y, 2 * L + X, L - 2 * X) == 0.0 && std::abs(y.getSample(0, 2 * L)) < 1e-6f && peakOf(y) > 0.1f, "16: LOOP repeats the sound to fill the clip, declicked at each wrap"); }
    // 17: render staleness: only sound-changing edits matter
    { auto a = makeBig(); const auto h0 = audioHash(a, SR, a.bpm); auto b = a; b.tracks[2].collapsed = true; b.tracks[1].showAuto = true; b.markers.push_back({ 4, "X" }); b.snapBeats = 0.25; b.loopStart = 2; b.loopEnd = 3;
      auto c = a; c.tracks[0].clips[0].gainDb += 1;
      check(audioHash(b, SR, a.bpm) == h0 && audioHash(c, SR, a.bpm) != h0 && audioHash(a, SR, a.bpm + 1) != h0, "17: the drag-out render goes stale only when the sound changes"); }
    // 18: undo keeps the host tempo
    { Session s; HostPos h; h.valid = true; h.bpm = 100; juce::AudioBuffer<float> blk(2, 32); s.process(blk, h); auto b = s.arrangement; s.arrangement.addClip(0, P(noise(0.2, 1), "x"), 0); s.commit(b);
      s.undo(); check(s.arrangement.bpm == 100 && s.effectiveBpm() == 100, "18: undo doesn't bring back an old document tempo while the host sets it"); }
    // 19: host start stops the internal transport and takes the playhead
    { Session s; s.setPlayheadBeats(2); s.play(); juce::AudioBuffer<float> blk(2, 256); s.process(blk, {}); HostPos h; h.valid = h.playing = true; h.bpm = 120; h.hasPpq = true; h.ppq = 16; s.process(blk, h);
      const bool stopped = !s.isPlaying(); h.playing = false; s.process(blk, h); check(stopped && std::abs(s.playheadBeats() - 16.0 - 256 / (60.0 / 120 * SR)) < 1e-6, "19: when the host starts, the internal PLAY stops and the playhead follows the host"); }
    // 20: seeking while playing is never lost
    { Session s; s.arrangement = makeBig(); s.changed(); s.waitUntilUpToDate(); auto off = renderArrangement(s.arrangement, SR, s.cache, false)->mix;
      s.play(); juce::AudioBuffer<float> blk(2, 300); s.process(blk, {}); s.setPlayheadBeats(8); blk.clear(); s.process(blk, {});
      const int at = (int) std::llround(8 * 60.0 / 128 * SR); check(maxDiff(blk, 0, off, at, 300) == 0.0, "20: a seek while playing lands exactly (applied by the audio thread)"); }
    // 22: memory budgets
    { check(Cache::kDefaultBudget <= ((size_t) 256 << 20), "22: processed-sound cache budget is 256 MB");
      History h; h.maxBytes = (size_t) 20 << 20; Arrangement a = fresh(); for (int i = 0; i < 12; ++i) { a.tracks[0].clips.clear(); a.addClip(0, P(noise(30.0, i), "big"), 0); h.push(a); }
      check(h.payloadBytesHeld() <= h.maxBytes && h.depth() < 12, "22: undo history is capped by the audio it keeps alive", juce::String((double) h.payloadBytesHeld() / 1048576.0, 0) + " MB, " + juce::String(h.depth()) + " steps"); }
    // 24: a later clip covers an earlier one (with a short crossfade)
    { auto a = fresh(); a.addClip(0, P(sine(220, 4.0), "A"), 0.0, 8.0); auto* b = a.addClip(0, P(noise(1.0, 9, 0.3f), "B"), 2.0, 2.0); auto y = render(a, cache);
      auto alone = fresh(); alone.addClip(0, b->source, 2.0, 2.0); auto yb = render(alone, cache); const int XO = (int) std::llround(kOverlapFadeSeconds * SR);
      check(maxDiff(y, (int) SR + XO, yb, (int) SR + XO, (int) SR - 2 * XO) == 0.0 && maxStep(y, (int) (2 * SR) + 1, (int) (2 * SR) + 1000) < 0.05, "24: overlapping clips: the later one wins (crossfaded), no hidden layering"); }
    // 26: long files are cut and the user is told
    { auto f = testDir().getChildFile("long.wav"); { juce::AudioBuffer<float> b(1, (int) (8000 * (kMaxSoundSeconds + 30))); b.clear(); b.setSample(0, 10, 0.5f); writeWav(f, b, 8000, 16); }
      juce::String note; auto p = Session::loadSound(f, "File", &note); f.deleteFile();
      check(p && std::abs(p->seconds() - kMaxSoundSeconds) < 0.01 && note.contains("20 minutes"), "26: files over 20 minutes are cut, with a message"); }
    // 27: old drag-out files are cleaned at start-up
    { auto old = Session::dragDir().getChildFile("old-test-folder"); old.createDirectory(); old.getChildFile("x.wav").replaceWithText("x"); old.setLastModificationTime(juce::Time::getCurrentTime() - juce::RelativeTime::days(3));
      { Session s; pump(200); } check(!old.exists(), "27: drag-out files older than a day are deleted"); }
    // 28 + 33: Shared Sounds listing (any case), tests use their own folder
    { auto dir = testDir().getChildFile("shared28"); dir.deleteRecursively(); dir.createDirectory(); Session::setSharedSoundsDir(dir);
      auto realDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Ghosty").getChildFile("Shared Sounds"); const int realBefore = realDir.getNumberOfChildFiles(juce::File::findFiles);
      juce::AudioBuffer<float> b(2, 100); b.clear(); writeWav(dir.getChildFile("KICK.WAV"), b, SR); writeWav(dir.getChildFile("snare.wav"), b, SR); dir.getChildFile("notes.txt").replaceWithText("x");
      auto l = Session::listShared(); Session s; s.arrangement.addClip(0, P(noise(0.1, 1), "n"), 0); juce::String err; auto sent = s.sendToBeats(s.arrangement.tracks[0].clips[0].id, err);
      check(l.size() == 2 && sent.size() == 1 && sent[0].getParentDirectory() == dir && realDir.getNumberOfChildFiles(juce::File::findFiles) == realBefore, "28/33: Shared Sounds lists any extension case; tests never touch the real folder", juce::String(l.size()) + " listed, sent " + juce::String(sent.size()) + (sent.isEmpty() ? err : sent[0].getFullPathName()) + " real " + juce::String(realBefore) + "->" + juce::String(realDir.getNumberOfChildFiles(juce::File::findFiles)));
      dir.deleteRecursively(); Session::setSharedSoundsDir({}); }
    // 29: float WAV keeps overs
    { auto f = testDir().getChildFile("over.wav"); juce::AudioBuffer<float> b(2, 10); b.clear(); b.setSample(0, 3, 1.75f); writeWav(f, b, SR);
      juce::AudioFormatManager fm; fm.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f)); juce::AudioBuffer<float> x(2, 10); if (r) r->read(&x, 0, 10, 0, true, true); r.reset(); f.deleteFile();
      check(std::abs(x.getSample(0, 3) - 1.75f) < 1e-6f, "29: bounces / drag-outs are 32-bit float WAV (a hot mix is not clipped)"); }
    // 30: cancelling a render stops a long Signalsmith job quickly
    { Session s; auto a = fresh(); auto* c = a.addClip(0, P(noise(60.0, 2), "long"), 0); c->pitch = 3; c->stretch = 3.0f; s.arrangement = a;
      bool done = false, wasCancelled = false; s.startRender(false, [&](std::shared_ptr<RenderResult> r) { done = true; wasCancelled = r->cancelled; }); pump(150);
      const double t0 = nowMs(); s.cancelRender(); pumpUntil([&] { return done; }, 20000); const double ms = nowMs() - t0;
      check(done && wasCancelled && ms < 1500, "30: RENDER cancel stops even a long stretch job", juce::String(ms, 0) + " ms"); }
    // 31: snap values from a state are legal
    { auto a = fresh(); auto t = toTree(a); t.setProperty("snapBeats", -3.0, nullptr); Arrangement b; fromTree(t, b); t.setProperty("snapBeats", 1e-300, nullptr); Arrangement c; fromTree(t, c); t.setProperty("snapBeats", 0.25, nullptr); Arrangement d; fromTree(t, d);
      check(b.snapBeats == 1.0 && c.snapBeats == 1.0 && d.snapBeats == 0.25, "31: grid values from a state snap to the offered grid"); }
    // 34: morph shape / amount / smear
    { std::vector<float> A((size_t) 48000), B((size_t) 48000), o1(A.size()), o2(A.size()), o3(A.size()), o4(A.size()); juce::Random r(3);
      for (size_t i = 0; i < A.size(); ++i) { A[i] = r.nextFloat() - 0.5f; B[i] = 0.4f * (float) std::sin(i * 0.05); }
      spectralMorph(A.data(), B.data(), o1.data(), 48000, MorphShape::Early); spectralMorph(A.data(), B.data(), o2.data(), 48000, MorphShape::Late);
      spectralMorph(A.data(), B.data(), o3.data(), 48000, MorphShape::Linear, 0.0f); spectralMorph(A.data(), B.data(), o4.data(), 48000, MorphShape::SCurve, 1.0f, 1.0f);
      double d12 = 0, e3 = 0, ends = 0; for (size_t i = 0; i < A.size(); ++i) { d12 = std::max(d12, (double) std::abs(o1[i] - o2[i])); const double x = morphCurve(MorphShape::Linear, i / 47999.0); e3 = std::max(e3, std::abs(o3[i] - (A[i] * std::cos(x * 1.5707963267948966) + B[i] * std::sin(x * 1.5707963267948966)))); }
      for (size_t i = 0; i < 400; ++i) ends = std::max(ends, std::max((double) std::abs(o4[i] - A[i]), (double) std::abs(o4[A.size() - 1 - i] - B[A.size() - 1 - i])));
      check(d12 > 0.05 && e3 < 1e-5 && ends < 1e-5, "34: morph SHAPE changes the blend, AMOUNT 0 = plain crossfade, SMEAR keeps the exact ends"); }
    // 38 / 40 / 42: position text, meters, markers
    { check(positionString(34.25, 4, 120).startsWith("9.3.2"), "38: position reads bar.beat.16th (9.3.2), with min:sec", positionString(34.25, 4, 120));
      Session s; s.arrangement = makeBig(); s.changed(); s.waitUntilUpToDate(); s.play(); juce::AudioBuffer<float> blk(2, 4096); for (int i = 0; i < 30; ++i) { blk.clear(); s.process(blk, {}); }
      check(s.takeTrackPeak(0) > 0.01f && s.takeMasterPeak() > 0.01f && s.takeTrackPeak(0) == 0.0f, "40: per-track and master peak meters (read-and-reset)"); }
}

// ------------------------------------------------------------------ page tests (UI layer)
static juce::MouseEvent mouseAt(juce::Component& c, juce::Point<float> p, juce::ModifierKeys mods, int clicks = 1, bool dragged = false, juce::Point<float> down = { -1, -1 }) {
    auto src = juce::Desktop::getInstance().getMainMouseSource();
    return juce::MouseEvent(src, p, mods, 0, 0, 0, 0, 0, &c, &c, juce::Time::getCurrentTime(), down.x < 0 ? p : down, juce::Time::getCurrentTime(), clicks, dragged);
}
static juce::Slider* findSlider(juce::Component& root, double lo, double hi) {
    for (auto* ch : root.getChildren()) { if (auto* s = dynamic_cast<juce::Slider*>(ch)) if (s->getMinimum() == lo && s->getMaximum() == hi) return s; if (auto* f = findSlider(*ch, lo, hi)) return f; }
    return nullptr;
}
static void pageTests() {
    std::cout << "-- page" << std::endl;
    Session s; s.arrangement = fresh(120); s.arrangement.snapOn = true; for (int i = 0; i < 3; ++i) s.arrangement.addClip(i % 2, PS(noise(1.0, i), "c" + juce::String(i)), i * 4.0); s.changed(); s.waitUntilUpToDate();
    ArrangePage page(s); page.setSize(1200, 700);
    const auto licensed = [](juce::Component*, const char*) { return true; };   // the page as a licensed plugin wires it (the demo gate: Shared/ExportGate.h)
    page.allowExport = licensed; page.exportOpen = [] { return true; }; pump(50);
    auto& tl = *page.getChildComponent(0);
    // 15: multi-select, copy / paste, nudge, quantise, mute, repeat, bounce
    page.selectAll(); const int n0 = s.arrangement.clipCount(); page.copySelected(); s.setPlayheadBeats(16); s.selection.clear(); s.selected = 0; page.paste();
    const bool pasted = s.arrangement.clipCount() == 2 * n0 && s.selection.size() == (size_t) n0;
    double minStart = 1e9; for (auto id : s.selection) minStart = std::min(minStart, s.arrangement.findClip(id)->start);
    page.nudge(1.0, 1); double m2 = 1e9; for (auto id : s.selection) m2 = std::min(m2, s.arrangement.findClip(id)->start);
    for (auto id : s.selection) s.arrangement.findClip(id)->start += 0.3; page.quantiseSelected(); bool onGrid = true; for (auto id : s.selection) onGrid = onGrid && std::abs(std::fmod(s.arrangement.findClip(id)->start, 1.0)) < 1e-9;
    page.muteSelected(); bool muted = true; for (auto id : s.selection) muted = muted && s.arrangement.findClip(id)->mute; page.muteSelected();
    page.repeatSelected(); const bool repeated = s.arrangement.clipCount() == 3 * n0;
    page.bounceSelected(); const bool bounced = s.arrangement.clipCount() == 2 * n0 + 1;
    check(pasted && std::abs(minStart - 16) < 1e-9 && std::abs(m2 - 17) < 1e-9 && onGrid && muted && repeated && bounced, "15: select all, copy / paste at the playhead, nudge, quantise, mute, repeat, bounce");
    for (int i = 0; i < 4; ++i) s.undo(); page.refresh();
    // 23: Alt-click without moving makes no copy
    { auto& c = s.arrangement.tracks[0].clips[0]; const int count = s.arrangement.clipCount(); auto* T = &tl; const float x = (float) (178 + (c.start + 1.0) * 24), y = 42 + 50;   // the clip body (away from the fade handles)
      page.setZoom(24, 0); T->mouseDown(mouseAt(*T, { x, (float) y }, juce::ModifierKeys(juce::ModifierKeys::altModifier | juce::ModifierKeys::leftButtonModifier))); T->mouseUp(mouseAt(*T, { x, (float) y }, juce::ModifierKeys(juce::ModifierKeys::altModifier)));
      const int after1 = s.arrangement.clipCount();
      T->mouseDown(mouseAt(*T, { x, (float) y }, juce::ModifierKeys(juce::ModifierKeys::altModifier | juce::ModifierKeys::leftButtonModifier))); T->mouseDrag(mouseAt(*T, { x + 48, (float) y }, juce::ModifierKeys(juce::ModifierKeys::altModifier | juce::ModifierKeys::leftButtonModifier), 1, true, { x, (float) y }));
      T->mouseUp(mouseAt(*T, { x + 48, (float) y }, juce::ModifierKeys(juce::ModifierKeys::altModifier), 1, true, { x, (float) y }));
      check(after1 == count && s.arrangement.clipCount() == count + 1, "23: Alt-click leaves no stacked copy; Alt-drag copies", juce::String(count) + " " + juce::String(after1) + " " + juce::String(s.arrangement.clipCount())); s.undo(); page.refresh(); }
    // 21: pitch drag is throttled, an unchanged drag adds no undo step
    { s.selected = s.arrangement.tracks[0].clips[0].id; s.selection = { s.selected }; page.refresh(); auto* sl = findSlider(page, -24, 24);
      const auto rev0 = s.revision(); const int d0 = s.history.depth();
      if (sl) { sl->onDragStart(); for (int i = 1; i <= 40; ++i) sl->setValue(i * 0.05, juce::sendNotificationSync); const auto revs = s.revision() - rev0; sl->setValue(0.0, juce::sendNotificationSync); sl->onDragEnd();
          check(revs <= 3 && s.history.depth() == d0, "21: dragging PITCH re-processes at most every 150 ms; a drag back to the start adds no undo step", juce::String((int) revs) + " rebuilds for 40 moves"); }
      else check(false, "21: pitch slider found"); }
    // 25: no mp3 promise
    check(!page.isInterestedInFileDrag({ "/x/a.mp3" }) && page.isInterestedInFileDrag({ "/x/a.WAV" }), "25: drop accepts what can be read (no mp3), any extension case");
    // 42: markers
    page.addMarkerAt(8); page.addMarkerAt(0); check(s.arrangement.markers.size() == 2 && s.arrangement.markers[0].beat == 0 && s.arrangement.markers[0].name == "BUILD", "42: markers: added sorted, named INTRO / BUILD / DROP...");
    // 41: collapse + track height
    s.arrangement.tracks[1].collapsed = true; s.arrangement.tracks[0].showAuto = true; page.refresh(); check(true, "41: tracks collapse and show automation lanes (see screenshots)");
    // SYNC view (Spectrum's DAW | OWN): PLAY lights with the real clock, the tempo label names the clock, OWN PLAY / SPACE call back
    { juce::TextButton* play = nullptr; juce::Label* tempo = nullptr;
      for (auto* ch : page.getChildren()) { if (auto* b = dynamic_cast<juce::TextButton*>(ch)) if (b->getButtonText() == "PLAY") play = b; if (auto* l = dynamic_cast<juce::Label*>(ch)) if (l->getText().contains("BPM")) tempo = l; }
      int toggles = 0; page.onOwnPlayToggle = [&] { ++toggles; };
      const bool def = play && tempo && !play->getToggleState() && tempo->getText().startsWith("120.0 BPM");
      page.setTransportView({ true, true, true, 133 }); pump(80);
      const bool ownOn = play && play->getToggleState() && tempo->getText() == "OWN 133 BPM" && !tempo->isEditableOnDoubleClick();
      page.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)); if (play) play->triggerClick(); pump(60);
      const bool ownPress = toggles == 2 && !s.isPlaying();
      page.setTransportView({ true, true, false, 133 }); pump(60); const bool ownOff = play && !play->getToggleState();
      page.setTransportView({ true, false, true, 128.5 }); pump(60); const bool daw = play->getToggleState() && tempo->getText() == "DAW 128.5 BPM" && !tempo->isEditableOnDoubleClick();
      page.setTransportView({ true, false, false, 128 }); page.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)); pump(60); const bool dawPlay = s.isPlaying() && play->getToggleState() && toggles == 2;
      page.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)); page.setTransportView({}); pump(60);
      const bool back = !s.isPlaying() && !play->getToggleState() && tempo->getText().endsWith("BPM") && tempo->isEditableOnDoubleClick();
      page.onOwnPlayToggle = nullptr;
      check(def, "SYNC view: default (no view) keeps today's PLAY / tempo behaviour");
      check(ownOn && ownOff, "SYNC view OWN: PLAY lights while the own clock runs, tempo reads OWN 133 BPM (read-only)");
      check(ownPress, "SYNC view OWN: PLAY / SPACE call onOwnPlayToggle (Arrange's internal transport stays off)");
      check(daw && dawPlay, "SYNC view DAW: tempo reads DAW 128.5 BPM (read-only), PLAY lights with the host, SPACE still plays Arrange when the host is stopped");
      check(back, "SYNC view cleared: back to the classic editable tempo"); }
    // DEMO GATE: every export asks allowExport first
    { juce::TextButton* send = nullptr; juce::TextButton* rend = nullptr;
      for (auto* ch : page.getChildren()) if (auto* b = dynamic_cast<juce::TextButton*>(ch)) { if (b->getButtonText() == "SEND TO BEATS") send = b; if (b->getButtonText().startsWith("RENDER")) rend = b; }
      auto dir = testDir().getChildFile("sharedGate"); dir.deleteRecursively(); dir.createDirectory(); Session::setSharedSoundsDir(dir);
      pumpUntil([&] { return !s.rendering(); }, 20000);
      s.selected = s.arrangement.tracks[0].clips[0].id; s.selection = { s.selected }; page.refresh();
      int asked = 0; page.allowExport = [&](juce::Component*, const char*) { ++asked; return false; };
      if (send) send->triggerClick(); pump(300); const bool noSend = Session::listShared().isEmpty() && page.statusMessage().contains("full version");
      if (rend) rend->triggerClick(); pump(50); const bool noRender = !s.rendering() && page.statusMessage().contains("full version");
      const bool refused = !page.exportAllowed("spec.arrange.render", false) && asked >= 3;
      page.allowExport = [](juce::Component*, const char*) { return true; };
      if (send) send->triggerClick(); const bool sent = pumpUntil([&] { return Session::listShared().size() == 1; }, 20000);
      const bool okNow = page.exportAllowed("spec.arrange.render", false); page.allowExport = nullptr; const bool nullOk = !page.exportAllowed("spec.arrange.render", false); page.allowExport = licensed;   // fail closed
      check(send && rend && noSend && noRender && refused, "DEMO GATE: allowExport false refuses SEND TO BEATS / RENDER / drags with 'Exporting is part of the full version'");
      check(sent && okNow && nullOk, "DEMO GATE: allowExport true exports as before; unset = refused (fail closed)");
      pumpUntil([&] { return !s.rendering(); }, 20000); dir.deleteRecursively(); Session::setSharedSoundsDir({}); }
    // SLIDER convention: bars set the value under the mouse, drag scrubs, Shift = fine, wheel = one step, double-click = default, type-in
    { s.selected = s.arrangement.tracks[0].clips[0].id; s.selection = { s.selected }; page.refresh(); pump(30);
      auto* g = findSlider(page, -60, 24); auto* pn = findSlider(page, -1, 1); bool ok = g && pn;
      if (ok) {
          const int bw = g->getWidth() - g->getTextBoxWidth(); const float y = g->getHeight() / 2.0f; const auto L = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier);
          auto near = [&](double v, double want) { return std::abs(v - want) <= g->getInterval() * 0.51 + 1e-9; };
          bool clicks = true; int d0 = s.history.depth();
          for (double p : { 0.25, 0.5, 0.75 }) { const float x = (float) (p * bw); g->mouseDown(mouseAt(*g, { x, y }, L)); g->mouseUp(mouseAt(*g, { x, y }, {})); clicks = clicks && near(g->getValue(), g->proportionOfLengthToValue(p)) && near(s.arrangement.findClip(s.selected)->gainDb, g->getValue()); }
          const bool clickUndo = s.history.depth() == d0 + 3;
          d0 = s.history.depth(); g->mouseDown(mouseAt(*g, { 0.25f * bw, y }, L)); for (int k = 1; k <= 10; ++k) g->mouseDrag(mouseAt(*g, { (0.25f + 0.025f * k) * bw, y }, L, 1, true, { 0.25f * bw, y }));
          g->mouseUp(mouseAt(*g, { 0.5f * bw, y }, {}, 1, true, { 0.25f * bw, y })); const bool drag = near(g->getValue(), g->proportionOfLengthToValue(0.5)) && s.history.depth() == d0 + 1;
          const auto S = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier); const double p0 = g->valueToProportionOfLength(g->getValue());
          g->mouseDown(mouseAt(*g, { 0.9f * bw, y }, S)); const bool noJump = std::abs(g->valueToProportionOfLength(g->getValue()) - p0) < 1e-9;
          g->mouseDrag(mouseAt(*g, { 0.9f * bw - 0.5f * bw, y }, S, 1, true, { 0.9f * bw, y })); g->mouseUp(mouseAt(*g, { 0.4f * bw, y }, {}, 1, true, { 0.9f * bw, y }));
          const bool shift = noJump && std::abs(g->valueToProportionOfLength(g->getValue()) - (p0 - 0.05)) < 0.004;
          d0 = s.history.depth(); const double v0 = g->getValue(); juce::MouseWheelDetails w {}; w.deltaY = 0.1f;
          g->mouseWheelMove(mouseAt(*g, { 10, y }, {}), w); const bool wheel = near(g->getValue(), v0 + g->getInterval()) && s.history.depth() == d0 + 1;
          g->mouseDown(mouseAt(*g, { 0.9f * bw, y }, L, 2)); g->mouseUp(mouseAt(*g, { 0.9f * bw, y }, {}, 2)); g->mouseDoubleClick(mouseAt(*g, { 0.9f * bw, y }, {}, 2)); pump(30);
          const bool reset = g->getValue() == 0.0 && s.arrangement.findClip(s.selected)->gainDb == 0.0f;
          pn->mouseDoubleClick(mouseAt(*pn, { 5, y }, {}, 2)); pump(30);
          const bool parse = g->getValueFromText("-6 dB") == -6.0 && g->getValueFromText("-inf") == -60.0 && pn->getValueFromText("30 L") == -0.3 && pn->getValueFromText("C") == 0.0 && pn->getValueFromText("45 R") == 0.45
              && pn->getTextFromValue(-0.3) == "30 L" && g->getTextFromValue(-6) == "-6.0 dB" && parseDbText("-inf", -60, 12) == -60 && parsePanText("centre") == 0.0 && std::abs(parsePanText("-25") + 0.25) < 1e-12;
          check(clicks && clickUndo, "SLIDERS: a click at 25 / 50 / 75 % sets that value (one undo step per click)");
          check(drag, "SLIDERS: press-drag scrubs from the click, one undo step for the gesture");
          check(shift, "SLIDERS: Shift = fine (1/10), and pressing Shift doesn't jump");
          check(wheel, "SLIDERS: the wheel moves one step (one undo step)");
          check(reset && pn->getValue() == 0.0, "SLIDERS: double-click resets (gain 0 dB, pan centre)");
          check(parse && g->getTooltip().startsWith("Clip gain: 0.0 dB"), "SLIDERS: type-in / hover text in real units (dB with -inf, pan C / 30 L / 45 R, %)", g->getTooltip().substring(0, 24));
          // header bars (track gain / pan) follow the same convention
          auto& T = tl; page.setZoom(24, 0); page.refresh(); const int ty = 42 + 38; const int d1 = s.history.depth();
          bool hdr = true; for (int px : { 14 + 21, 14 + 43, 14 + 64 }) { T.mouseDown(mouseAt(T, { (float) px, (float) ty }, L)); T.mouseUp(mouseAt(T, { (float) px, (float) ty }, {}));
              const double u = (px - 14) / 86.0; hdr = hdr && std::abs(s.arrangement.tracks[0].gainDb - juce::jmap(std::sqrt(u), 0.0, 1.0, -60.0, 12.0)) < 1e-4; }
          T.mouseDown(mouseAt(T, { 120.0f, (float) ty }, L)); T.mouseUp(mouseAt(T, { 120.0f, (float) ty }, {})); const bool hpan = std::abs(s.arrangement.tracks[0].pan - (12.0 / 50.0 * 2 - 1)) < 1e-5;
          const bool hundo = s.history.depth() == d1 + 4;
          T.mouseWheelMove(mouseAt(T, { 30.0f, (float) ty }, {}), w); const double gw = s.arrangement.tracks[0].gainDb;
          T.mouseDoubleClick(mouseAt(T, { 30.0f, (float) ty }, {}, 2)); const bool hreset = s.arrangement.tracks[0].gainDb == 0.0f;
          check(hdr && hpan && hundo && std::abs(gw - (juce::jmap(std::sqrt(64 / 86.0), 0.0, 1.0, -60.0, 12.0) + 0.5)) < 1e-4 && hreset, "SLIDERS: track gain / pan bars: click = value, wheel = 0.5 dB, double-click = 0 dB, one undo step each");
      } else check(false, "SLIDERS: inspector bars found");
    }
    // RULER gestures (one set everywhere): left never makes a loop; right-click = play from here; right-drag = loop; grips drag it out
    { page.setZoom(24, 0); auto& T = tl; auto& a = s.arrangement; a.loopOn = false; a.loopStart = 0; a.loopEnd = 0; s.changed(); page.refresh();
      const auto L = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), R = juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier);
      auto X = [](double beat) { return (float) (178 + beat * 24); }; const float y = 42 - 8;
      T.mouseDown(mouseAt(T, { X(8), y }, L)); T.mouseUp(mouseAt(T, { X(8), y }, {})); const bool clickNoLoop = a.loopEnd == 0 && !a.loopOn && std::abs(s.playheadBeats() - 8) < 1e-6;
      T.mouseDown(mouseAt(T, { X(2), y }, L)); T.mouseDrag(mouseAt(T, { X(10), y }, L, 1, true, { X(2), y })); T.mouseUp(mouseAt(T, { X(10), y }, {}, 1, true, { X(2), y }));
      const bool dragNoLoop = a.loopEnd == 0 && !a.loopOn;
      const int d0 = s.history.depth();
      T.mouseDown(mouseAt(T, { X(12), y }, R)); T.mouseUp(mouseAt(T, { X(12), y }, R)); const bool rightCursor = std::abs(s.playheadBeats() - 12) < 1e-6 && a.loopEnd == 0 && s.history.depth() == d0;
      T.mouseDown(mouseAt(T, { X(4), y }, R)); T.mouseDrag(mouseAt(T, { X(4) + 4, y }, R, 1, true, { X(4), y })); const bool smallNoLoop = a.loopEnd == 0;
      T.mouseDrag(mouseAt(T, { X(12), y }, R, 1, true, { X(4), y })); T.mouseUp(mouseAt(T, { X(12), y }, R, 1, true, { X(4), y }));
      const bool rightLoop = a.loopStart == 4 && a.loopEnd == 12 && a.loopOn && s.history.depth() == d0 + 1;
      T.mouseDown(mouseAt(T, { X(12), y }, L)); T.mouseDrag(mouseAt(T, { X(16), y }, L, 1, true, { X(12), y })); T.mouseUp(mouseAt(T, { X(16), y }, {}, 1, true, { X(12), y }));
      const bool resize = a.loopStart == 4 && a.loopEnd == 16;
      T.mouseDown(mouseAt(T, { X(6), y }, L)); T.mouseDrag(mouseAt(T, { X(8), y }, L, 1, true, { X(6), y })); T.mouseUp(mouseAt(T, { X(8), y }, {}, 1, true, { X(6), y }));
      const bool move = a.loopStart == 6 && a.loopEnd == 18;
      check(clickNoLoop && dragNoLoop, "RULER: a plain left click / drag places the playhead and never makes a loop");
      check(rightCursor && smallNoLoop, "RULER: right-click = play from here (no loop, no undo step); under 6 px is still a click");
      check(rightLoop, "RULER: right-drag makes the loop (snapped, LOOP on, one undo step)");
      check(resize && move, "RULER: left drags an existing loop's end to resize it, its middle to move it");
      // the loop grip: export-gated; MIDI of the loop section
      int asked = 0; page.allowExport = [&](juce::Component*, const char*) { ++asked; return false; };
      const bool refused = !page.dragLoop(true) && !page.dragLoop(false) && asked == 2 && page.statusMessage().contains("full version");
      page.allowExport = licensed;
      auto m = fresh(120); m.addClip(0, P(sine(261.6, 4.0), "mel", { { 60, 1, 0.0, 0.5 }, { 64, 1, 1.0, 0.5 }, { 67, 1, 3.0, 0.5 } }), 0.0); m.loopStart = 2; m.loopEnd = 6;   // notes at beats 0, 2, 6
      const auto keep = s.arrangement; s.arrangement = m; auto f = page.writeLoopMidi(testDir().getChildFile("loopmidi")); int n = 0; double first = -1;
      if (f.existsAsFile()) { juce::FileInputStream in(f); juce::MidiFile mf; mf.readFrom(in); for (int t = 0; t < mf.getNumTracks(); ++t) for (auto* e : *mf.getTrack(t)) if (e->message.isNoteOn()) { ++n; first = e->message.getTimeStamp() / 960.0; } }
      s.arrangement = keep; s.changed(); page.refresh();
      check(refused, "RULER: the loop's WAV / MIDI grips are refused in the demo ('Exporting is part of the full version')");
      check(n == 1 && std::abs(first) < 1e-9, "RULER: the MIDI grip exports just the loop's notes, from the loop start", juce::String(n) + " notes"); }
    // TOOLTIPS: the page owns a TooltipWindow only when asked to
    { auto countTips = [](juce::Component& c) { int n = 0; for (auto* ch : c.getChildren()) if (dynamic_cast<juce::TooltipWindow*>(ch)) ++n; return n; };
      juce::Component host; host.setSize(1200, 700);
      ArrangePage own(s); host.addAndMakeVisible(own); const int a1 = countTips(own);
      ArrangePage shared(s); shared.setOwnsTooltips(false); host.addAndMakeVisible(shared); const int a2 = countTips(shared);
      own.setOwnsTooltips(false); const int a3 = countTips(own);
      check(a1 == 1 && a2 == 0 && a3 == 0, "TOOLTIPS: ownsTooltips = false -> no second TooltipWindow (default keeps its own)", juce::String(a1) + juce::String(a2) + juce::String(a3));
      ArrangePage noType(s); noType.setOwnsTypeIn(false); host.addAndMakeVisible(noType); const bool t1 = own.hasTypeIn(), t2 = !noType.hasTypeIn(); own.setOwnsTypeIn(false);
      check(t1 && t2 && !own.hasTypeIn(), "TYPE-IN: the page has its own right-click type-in unless the host editor owns one (ownsTypeIn = false)"); }
    // 17: background MIX render becomes ready after edits settle
    { const bool ready = pumpUntil([&] { return s.lastRender && s.lastRender->revision == s.audioHashNow(s.arrangement.loopOn) && s.lastRender->mixFile.existsAsFile(); }, 20000);
      check(ready, "17: a MIX render is kept ready in the background (drag-out never waits)"); }
}

// ------------------------------------------------------------------ demos
static void writeMp3(const juce::File& dir, const juce::String& name, const juce::AudioBuffer<float>& y) {
    juce::AudioBuffer<float> b; b.makeCopyOf(y); const float pk = peakOf(b); if (pk > 0) b.applyGain(0.89f / pk);
    auto wav = dir.getChildFile(name + ".wav"); writeWav(wav, b, SR, 24);
    auto mp3 = dir.getChildFile(name + ".mp3"); mp3.deleteFile();
    juce::ChildProcess p; p.start(juce::StringArray { "ffmpeg", "-loglevel", "error", "-y", "-i", wav.getFullPathName(), "-codec:a", "libmp3lame", "-b:a", "256k", mp3.getFullPathName() }); p.waitForProcessToFinish(120000);
    if (mp3.existsAsFile()) wav.deleteFile();
    std::cout << "  wrote " << mp3.getFullPathName() << " (" << juce::String(y.getNumSamples() / SR, 1) << " s)" << std::endl;
}
static Arrangement demoA() {   // "painted sounds into a piece": pad morphs, bells, a warped Beats loop, bass, automation build, markers
    auto a = fresh(122); a.snapOn = true; for (int t = 0; t < 2; ++t) a.addTrack();
    a.tracks[0].name = "PAINTED PADS"; a.tracks[1].name = "TEXTURES"; a.tracks[2].name = "BELLS"; a.tracks[3].name = "BEAT"; a.tracks[4].name = "BASS"; a.tracks[5].name = "VOICE";
    const double bar = 4; const double spbS = 60.0 / 122;
    auto padA = PS(padChord({ 57, 60, 64, 67 }, 8 * bar * spbS, 1), "pad Am9", "SPECTRAL LAB"), padB = PS(padChord({ 53, 57, 60, 64 }, 8 * bar * spbS, 2), "pad Fmaj7", "SPECTRAL LAB");
    auto sweep = PS(sweepTexture(8 * bar * spbS, 3), "breath sweep", "ARCS"), b1 = PS(bell(76, 3.0), "glass bell", "ARCS", { { 76, 0.9f, 0, 1.0 } });
    auto beat = makePayload(beatLoop(128, 2), SR, "beat 2 bars @128", {}, { "Shared", "beat", {} }, 128.0);   // a Beats loop made at 128: WARPs to 122
    auto bass = PS(bassLine(122, { 45, 45, 57, 43, 45, 52, 41, 43 }, { 1, 0.5, 0.5, 1, 0.5, 0.5, 0.5, 0.5 }, 1), "acid line", "VOICE",
        { { 45, 1, 0, 0.24 }, { 45, 1, 0.49, 0.12 }, { 57, 1, 0.74, 0.12 }, { 43, 1, 0.98, 0.24 }, { 45, 1, 1.48, 0.12 }, { 52, 1, 1.72, 0.12 }, { 41, 1, 1.97, 0.12 }, { 43, 1, 2.21, 0.12 } });
    { auto* c = a.addClip(0, padA, 0, 8 * bar); c->fadeIn = 8; c->fadeInCurve = Curve::SCurve; c->morph = true; auto* d = a.addClip(0, padB, 6 * bar, 8 * bar); d->morph = true; d->morphShape = MorphShape::Late; auto* e = a.addClip(0, padA, 12 * bar, 8 * bar); e->pitch = 5; e->fadeOut = 8; }
    { auto* c = a.addClip(1, sweep, 2 * bar, 8 * bar); c->gainDb = -4; c->pan = -0.3f; c->gainEnv = { { 0, -24 }, { 16, 0 }, { 32, -6 } }; auto* d = a.addClip(1, sweep, 12 * bar, 8 * bar); d->reverse = true; d->pan = 0.3f; d->gainDb = -4; }
    for (int k = 0; k < 8; ++k) { auto* c = a.addClip(2, b1, 4 * bar + k * 2 * bar + (k % 2) * 1.5); c->pitch = (float) std::array<int, 8> { 0, 3, 7, 5, 0, -2, 3, 10 }[(size_t) k]; c->pan = (k % 2 ? 0.5f : -0.5f); c->gainDb = -5; c->reverse = k == 3 || k == 7; }
    { auto* c = a.addClip(3, beat, 4 * bar, 14 * bar); c->loop = true; c->gainDb = -3; }   // one warped loop, LOOPed across 14 bars
    { auto* c = a.addClip(4, bass, 8 * bar, 10 * bar); c->loop = true; c->gainDb = -4; }
    { auto* c = a.addClip(5, padB, 10 * bar, 4 * bar); c->pitch = 12; c->stretch = 2.0f; c->gainDb = -9; c->fadeIn = 4; c->fadeOut = 6; c->pan = 0.6f; }
    a.tracks[3].volEnv = { { 4 * bar, -18 }, { 8 * bar, 0 }, { 15 * bar, 0 }, { 16 * bar, -30 }, { 16.25 * bar, 0 } };   // beat build + a pre-drop dip
    a.tracks[2].panEnv = { { 4 * bar, -0.6f }, { 20 * bar, 0.6f } };
    a.tracks[4].showAuto = false; a.tracks[3].showAuto = true;
    a.markers = { { 0, "INTRO" }, { 4 * bar, "BUILD" }, { 8 * bar, "DROP" }, { 16 * bar, "BREAK" } };
    a.tracks[3].gainDb = -2; a.loopStart = 8 * bar; a.loopEnd = 12 * bar;
    return a;
}
static Arrangement demoB() {   // "spectral transitions": a reversed swell into a stretched, pitched collage with shaped / smeared morphs
    auto a = fresh(96); a.snapOn = true; a.tracks[0].name = "MORPH CHAIN"; a.tracks[1].name = "STRETCH"; a.tracks[2].name = "PULSE"; a.tracks[3].name = "DUST";
    auto bellLong = PS(bell(64, 6.0), "low bell"), pad = PS(padChord({ 50, 57, 62, 65 }, 12 * 60.0 / 96 * 4, 7), "drone Dm"), sw = PS(sweepTexture(10.0, 8), "storm");
    auto beat = makePayload(beatLoop(120, 1), SR, "slow beat @120", {}, { "Shared", "beat", {} }, 120.0);
    { auto* c = a.addClip(0, bellLong, 0, 16); c->reverse = true; c->stretch = 2.0f; c->morph = true; c->fadeIn = 4; c->morphSmear = 0.6f;
      auto* d = a.addClip(0, pad, 12, 24); d->morph = true; d->morphShape = MorphShape::Early; auto* e = a.addClip(0, sw, 32, 16); e->morph = true; e->pitch = -5; auto* f = a.addClip(0, pad, 44, 20); f->pitch = 3; f->fadeOut = 8; }
    for (int k = 0; k < 6; ++k) { auto* c = a.addClip(1, bellLong, 8 + k * 8.0, 8); c->stretch = 0.5f + 0.25f * (k % 4); c->pitch = (float) (k * 2 - 5); c->keepLength = k % 2 == 0; c->pan = (k % 3 - 1) * 0.6f; c->gainDb = -8; c->fadeOut = 2; }
    { auto* c = a.addClip(2, beat, 16, 24); c->loop = true; c->gainDb = -5; auto* d = a.addClip(2, beat, 40, 16); d->loop = true; d->pitch = -12; d->gainDb = -5; }
    { auto* c = a.addClip(3, sw, 0, 64); c->gainDb = -16; c->stretch = 4.0f; c->pan = -0.2f; }
    a.tracks[0].panEnv = { { 0, -0.5f }, { 64, 0.5f } }; a.markers = { { 0, "SWELL" }, { 16, "PULSE" }, { 40, "DOWN" } };
    return a;
}
static void demos() {
    auto dir = juce::File("/root/work/arrange/demos"); dir.createDirectory(); Cache c;
    std::cout << "demos" << std::endl;
    writeMp3(dir, "1 painted sounds into a piece", render(demoA(), c));
    writeMp3(dir, "2 spectral transitions", render(demoB(), c));
}

// ------------------------------------------------------------------ screenshots
static void shots() {
    auto dir = juce::File("/root/work/arrange/shots"); dir.createDirectory(); for (auto& f : dir.findChildFiles(juce::File::findFiles, false, "*.png")) f.deleteFile();
    std::cout << "screenshots" << std::endl;
    Session s; s.arrangement = demoA(); s.changed(); s.waitUntilUpToDate(60000);
    ArrangePage page(s); page.setSize(1200, 700); page.allowExport = [](juce::Component*, const char*) { return true; }; page.exportOpen = [] { return true; };   // licensed
    auto snap = [&](const juce::String& name) {
        pump(150);
        auto img = page.createComponentSnapshot(page.getLocalBounds(), true, 1.0f); juce::PNGImageFormat png; auto f = dir.getChildFile(name); f.deleteFile(); juce::FileOutputStream os(f); png.writeImageToStream(img, os);
        std::cout << "  wrote " << f.getFullPathName() << std::endl;
    };
    auto playSome = [&](double beat) { s.setPlayheadBeats(beat); s.play(); juce::AudioBuffer<float> b(2, 2048); for (int i = 0; i < 6; ++i) { b.clear(); s.process(b, {}); } s.stop(); };
    page.setDark(false); page.zoomToFit(); playSome(34); snap("1 arrange light (markers, automation lane, meters).png");
    s.selected = s.arrangement.tracks[0].clips[0].id; s.selection = { s.selected, s.arrangement.tracks[0].clips[1].id }; page.refresh(); snap("2 light two clips selected (morph badge).png");
    page.setDark(true); playSome(40); snap("3 arrange dark.png");
    s.arrangement.loopOn = true; s.selected = s.arrangement.tracks[1].clips[0].id; s.selection = { s.selected }; page.setEnvelopeMode(true); page.setZoom(20.0, 4.0); page.refresh(); snap("4 dark gain envelope edit + loop.png");
    page.setEnvelopeMode(false); s.arrangement.tracks[1].collapsed = true; s.arrangement.tracks[5].collapsed = true; s.arrangement.tracks[2].showAuto = true; s.arrangement.tracks[2].autoLane = 1; s.changed(); page.refresh(); snap("5 dark collapsed tracks + pan lane.png");
    { ArrangePage::TransportView v; v.active = true;
      v.own = false; v.playing = true; v.bpm = 122; page.setDark(false); page.setTransportView(v); playSome(36); snap("7 light SYNC DAW (tempo from the DAW).png");
      v.own = true; v.playing = true; v.bpm = 122; page.setTransportView(v); snap("8 light SYNC OWN playing.png");
      page.setDark(true); snap("9 dark SYNC OWN playing.png");
      v.own = false; v.playing = false; page.setTransportView(v); snap("10 dark SYNC DAW stopped.png");
      page.setTransportView({}); }
    s.arrangement.loopOn = true; s.arrangement.loopStart = 32; s.arrangement.loopEnd = 48; s.changed(); page.setDark(false); page.zoomToFit(); page.refresh(); snap("11 light loop brace + WAV MIDI grips.png");
    page.setDark(true); snap("12 dark loop brace + WAV MIDI grips.png");
    page.setDark(false); s.arrangement = demoB(); s.selected = s.arrangement.tracks[1].clips[2].id; s.selection = { s.selected }; s.changed(); s.waitUntilUpToDate(60000); page.zoomToFit(); page.refresh(); playSome(20); snap("6 light spectral transitions.png");
    { const int n0 = s.arrangement.clipCount(); page.duplicateSelected(); const bool dup = s.arrangement.clipCount() == n0 + 1; page.splitSelectedAt(s.arrangement.findClip(s.selected)->start + 1.0);
      const bool split = s.arrangement.clipCount() == n0 + 2; page.deleteSelected(); const bool del = s.arrangement.clipCount() == n0 + 1; s.undo(); s.undo(); s.undo();
      check(dup && split && del && s.arrangement.clipCount() == n0, "page: duplicate, split, delete and their undo"); }
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::StringArray args(argv + 1, argc - 1);
    std::cout << "== ARRANGE" << std::endl;
    coreTests(); punchTests(); pageTests();
    if (!args.contains("--quick")) { shots(); demos(); }
    testDir().deleteRecursively();
    std::cout << "\n" << passes << " passed, " << fails << " failed" << std::endl;
    return fails == 0 ? 0 : 1;
}
