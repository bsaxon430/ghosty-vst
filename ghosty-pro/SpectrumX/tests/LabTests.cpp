// Ghosty Spectrum SPECTRAL LAB tests.
//   SpectrumLabTests            -> engine + state tests (exit code = failures)
//   SpectrumLabTests shots DIR  -> PNG screenshots of the lab tab (needs a display / Xvfb)
//   SpectrumLabTests demogate   -> the demo export gate: every audio / MIDI export path refused when locked, working when unlocked
//   SpectrumLabTests savegate   -> locked project saves keep the user's imported sounds but no plugin-made audio (demogate runs it too)
//   SpectrumLabTests demos DIR  -> WAV demos (painted melody, image ghost face, evolving snare, split rhythms)
#include <map>
#include "../SpecSliders.h"
#include "../SpecGuide.h"
#include "WaveRegion.h"
#include "../SpecProcessor.h"
#include "../SpecEditor.h"
#include "../LabPanel.h"
#include "../SpecTabs.h"
#include "../SpecTheme.h"
#include "../ArrangePage.h"
#include "../ArcsPage.h"
#include "../LabProcessPage.h"
#include "../SpecVoiceTab.h"
#include "../ArcsFactory.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <iostream>

using namespace lab;
static int fails = 0, passes = 0;
static void check(bool ok, const juce::String& what, const juce::String& detail = {}) {
    std::cout << (ok ? "  PASS  " : "  FAIL  ") << what << (detail.isNotEmpty() ? "  (" + detail + ")" : juce::String()) << std::endl;
    (ok ? passes : fails)++;
}
static double snrDb(const std::vector<float>& ref, const std::vector<float>& y, int skip = 0) {
    double s = 0, e = 0; const size_t n = std::min(ref.size(), y.size());
    for (size_t i = (size_t) skip; i < n; ++i) { s += (double) ref[i] * ref[i]; const double d = (double) ref[i] - y[i]; e += d * d; }
    return 10.0 * std::log10(s / std::max(1e-30, e));
}
static double corr(const std::vector<float>& a, const std::vector<float>& b) {
    const size_t n = std::min(a.size(), b.size()); double ma = 0, mb = 0; for (size_t i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; } ma /= (double) n; mb /= (double) n;
    double sab = 0, saa = 0, sbb = 0; for (size_t i = 0; i < n; ++i) { const double x = a[i] - ma, y = b[i] - mb; sab += x * y; saa += x * x; sbb += y * y; }
    return sab / std::sqrt(std::max(1e-30, saa * sbb));
}
static double peakHz(const std::vector<float>& x, size_t start = 0) {   // 65536-point spectrum peak with parabolic interpolation
    constexpr int order = 16, M = 1 << order; juce::dsp::FFT fft(order); std::vector<float> b((size_t) M * 2, 0.0f);
    for (int i = 0; i < M && start + (size_t) i < x.size(); ++i) b[(size_t) i] = x[start + (size_t) i] * (float) (0.5 - 0.5 * std::cos(2 * juce::MathConstants<double>::pi * i / M));
    fft.performFrequencyOnlyForwardTransform(b.data(), true);
    int k = 2; for (int i = 2; i < M / 2 - 1; ++i) if (b[(size_t) i] > b[(size_t) k]) k = i;
    const double a = b[(size_t) k - 1], c = b[(size_t) k], d = b[(size_t) k + 1]; const double off = 0.5 * (a - d) / (a - 2 * c + d);
    return (k + off) * SR / M;
}
static bool finite(const std::vector<float>& x) { for (float v : x) if (!std::isfinite(v)) return false; return true; }
static bool finite(const Canvas& c) { for (auto& e : c.cells) if (!std::isfinite(e.mag) || !std::isfinite(e.freq) || !std::isfinite(e.phase)) return false; for (float v : c.amp) if (!std::isfinite(v)) return false; return true; }
static std::vector<float> testSignal(double secs) {
    std::vector<float> x((size_t) (secs * SR)); juce::Random r(3);
    for (size_t i = 0; i < x.size(); ++i) {
        const double t = i / SR; double v = 0;
        for (int h = 1; h <= 6; ++h) v += 0.25 / h * std::sin(2 * juce::MathConstants<double>::pi * 220.0 * h * t + h);
        v += 0.12 * std::sin(2 * juce::MathConstants<double>::pi * (300.0 * t + 400.0 * t * t));   // chirp
        if (t > 0.6 && t < 0.7) v += 0.3 * (r.nextFloat() * 2 - 1);                                   // noise burst
        x[i] = (float) (v * std::min(1.0, t * 50.0));
    }
    return x;
}
juce::Image ghostFace(int w, int h) {   // a white ghost with dark eyes and mouth on black
    juce::Image img(juce::Image::RGB, w, h, true); juce::Graphics g(img); g.fillAll(juce::Colours::black);
    juce::Path p; const float cx = w * 0.5f, top = h * 0.1f, bw = w * 0.56f, bot = h * 0.9f;
    p.startNewSubPath(cx - bw / 2, bot); p.lineTo(cx - bw / 2, top + bw / 2); p.addCentredArc(cx, top + bw / 2, bw / 2, bw / 2, 0, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi);
    p.lineTo(cx + bw / 2, bot); for (int i = 0; i < 4; ++i) { const float x0 = cx + bw / 2 - (i + 0.5f) * bw / 4; p.lineTo(x0, bot - h * 0.08f); p.lineTo(x0 - bw / 8, bot); }
    p.closeSubPath();
    g.setGradientFill(juce::ColourGradient(juce::Colours::white, cx, top, juce::Colour(0xffb0b0b0), cx, bot, false)); g.fillPath(p);
    g.setColour(juce::Colours::black); g.fillEllipse(cx - bw * 0.28f, h * 0.36f, bw * 0.17f, h * 0.17f); g.fillEllipse(cx + bw * 0.11f, h * 0.36f, bw * 0.17f, h * 0.17f);
    g.fillEllipse(cx - bw * 0.12f, h * 0.6f, bw * 0.24f, h * 0.12f);
    return img;
}
static std::vector<float> readWavMono(const juce::File& f) {
    juce::AudioFormatManager fm; fm.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f)); if (!r) return {};
    juce::AudioBuffer<float> b((int) r->numChannels, (int) r->lengthInSamples); r->read(&b, 0, b.getNumSamples(), 0, true, true);
    return std::vector<float>(b.getReadPointer(0), b.getReadPointer(0) + b.getNumSamples());
}

// ---------------------------------------------------------------- engine tests
static void engineTests() {
    std::cout << "ENGINE" << std::endl;
    { // resynthesis of an unedited sound
        const auto x = testSignal(2.0); Canvas c; analyse(x, c);
        const auto yp = render(c, Quality::Preview), yh = render(c, Quality::HQ);
        check(snrDb(x, yp) >= 30.0 && snrDb(x, yh) >= 30.0, "unedited resynthesis SNR >= 30 dB", "preview " + juce::String(snrDb(x, yp), 1) + " dB, HQ " + juce::String(snrDb(x, yh), 1) + " dB");
        // saved + loaded canvas still resynthesises the same
        Canvas c2; check(load(save(c), c2) && snrDb(x, render(c2, Quality::HQ)) >= 30.0, "canvas save/load keeps the sound");
    }
    { // painted line at 1 kHz
        Canvas c; c.resize(framesFor(2.0)); Brush b; b.size = 1; b.level = 0.5f;
        for (int t = 0; t < c.T; ++t) paintAt(c, (float) t, (float) hzRow(1000.0), b, 1000.0);
        const auto y = render(c, Quality::HQ); const double f = peakHz(y, 4096);
        check(std::abs(f - 1000.0) < 2.0, "painted 1 kHz line -> output peak at 1 kHz", juce::String(f, 2) + " Hz");
        // unsnapped painting lands within a third of a semitone
        Canvas d; d.resize(framesFor(2.0)); for (int t = 0; t < d.T; ++t) paintAt(d, (float) t, (float) hzRow(1000.0), b);
        const double f2 = peakHz(render(d, Quality::Preview), 4096); check(std::abs(12 * std::log2(f2 / 1000.0)) < 0.34, "free paint at the 1 kHz row", juce::String(f2, 1) + " Hz");
        // SHIFT +12 on the painted line
        Sel s; s.t0 = 0; s.t1 = c.T; s.r0 = 0; s.r1 = R - 1; shift(c, s, 12.0);
        const double f3 = peakHz(render(c, Quality::HQ), 4096); check(std::abs(f3 - 2000.0) < 3.0, "SHIFT +12 moves a painted partial x2", juce::String(f3, 2) + " Hz");
    }
    { // SHIFT +12 on an analysed sound: a 440 Hz sine (STFT plane, phase-locked region move)
        std::vector<float> x((size_t) SR * 2); for (size_t i = 0; i < x.size(); ++i) x[i] = 0.5f * (float) std::sin(2 * juce::MathConstants<double>::pi * 440.0 * i / SR);
        Canvas c; analyse(x, c); Sel s; s.t0 = 0; s.t1 = c.T; s.r0 = 0; s.r1 = R - 1; shift(c, s, 12.0);
        const auto y = render(c, Quality::HQ); const double f = peakHz(y, 8192);
        check(std::abs(f - 880.0) < 2.0, "SHIFT +12 moves an imported partial x2", juce::String(f, 2) + " Hz");
        // ...and leaves a clean tone (energy within +-30 Hz of 880 Hz)
        int T = 0; auto X = stftFrames(y, T); double in = 0, all = 0;
        for (int t = 4; t < T - 4; ++t) for (int k = 1; k < K; ++k) { const double e = std::norm(X[(size_t) t * K + (size_t) k]); all += e; if (std::abs(k * BIN_HZ - 880.0) < 60) in += e; }
        check(in / all > 0.97, "shifted tone stays clean", juce::String(100.0 * in / all, 2) + "% of energy at 880 Hz");
        // two-octave stretch of harmonics + freeze + smear: finite
        Canvas d; analyse(testSignal(1.0), d); stretchHarmonics(d, s, 1.3); Sel f2 = s; f2.t0 = 10; f2.t1 = 60; freeze(d, f2); Brush b; b.size = 8; smearAt(d, 30, 150, b); eraseAt(d, 40, 200, b);
        check(finite(d) && finite(render(d, Quality::HQ)), "stretch / freeze / smear / erase stay finite");
    }
    { // image to sound: the spectrogram of the output looks like the image
        const auto img = ghostFace(256, 256); ImageMap m; m.seconds = 4.0; m.loHz = 150; m.hiHz = 12000;
        Canvas c; imageToCanvas(img, m, c); const auto y = render(c, Quality::HQ);
        Canvas a; analyse(y, a);
        const int ra = (int) std::ceil(hzRow(m.loHz)), rb = (int) std::floor(hzRow(m.hiHz));
        const auto small = img.rescaled(64, 64, juce::Graphics::highResamplingQuality);
        std::vector<float> I, S; float mx = 1e-9f;
        for (int t = 0; t < a.T; ++t) for (int r = ra; r <= rb; ++r) mx = std::max(mx, a.level(t, r));
        for (int x = 0; x < 64; ++x) for (int yy = 0; yy < 64; ++yy) {
            I.push_back(small.getPixelAt(x, yy).getBrightness());
            const int t0 = x * a.T / 64, t1 = std::max(t0 + 1, (x + 1) * a.T / 64); const int r1 = rb - yy * (rb - ra) / 64, r0 = std::max(ra, rb - (yy + 1) * (rb - ra) / 64);
            double s = 0; int n = 0; for (int t = t0; t < t1; ++t) for (int r = r0; r <= r1; ++r) { s += juce::jlimit(0.0, 1.0, (20 * std::log10(std::max(1e-9f, a.level(t, r) / mx)) + 60) / 60.0); ++n; }
            S.push_back((float) (s / std::max(1, n)));
        }
        const double cr = corr(I, S); check(cr > 0.8, "image-to-sound: spectrogram matches the image", "corr " + juce::String(cr, 3));
        check(finite(y) && peakOf(y) < 1.0f && peakOf(y) > 0.05f, "image render level is sane", "peak " + juce::String(peakOf(y), 3));
    }
    { // split bands sum back to the input
        const auto x = testSignal(2.0);
        for (int nb : { 2, 4, 6 }) { SplitSetup s; s.bands = nb; auto bands = splitBands(x, s); std::vector<float> sum(x.size(), 0.0f); for (auto& b : bands) for (size_t i = 0; i < x.size(); ++i) sum[i] += b[i];
            const double cr = corr(x, sum); check(cr > 0.99, "SPLIT " + juce::String(nb) + " bands sum to the input", "corr " + juce::String(cr, 6) + ", SNR " + juce::String(snrDb(x, sum), 1) + " dB"); }
        SplitSetup s; auto bands = splitBands(x, s); auto y = renderSplit(bands, s, 120, (int) x.size());
        check(corr(x, y) > 0.99, "SPLIT sequencer at neutral = the input", "corr " + juce::String(corr(x, y), 6));
        s.seq[1].steps.fill(0); for (int i = 0; i < kSteps; i += 2) s.seq[1].steps[(size_t) i] = 1; s.seq[2].retrig = true; s.seq[2].rateDiv = 32; s.seq[3].gate = 0.3f;
        auto z = renderSplit(bands, s, 120, (int) x.size()); check(finite(z) && corr(x, z) < 0.99, "SPLIT patterns change the sound");
    }
    { // morph 0 / 1 = A / B
        std::vector<float> a((size_t) SR), b((size_t) (SR * 1.3));
        for (size_t i = 0; i < a.size(); ++i) a[i] = 0.4f * (float) (std::sin(2 * juce::MathConstants<double>::pi * 220 * i / SR) + 0.5 * std::sin(2 * juce::MathConstants<double>::pi * 440 * i / SR));
        juce::Random r(9); for (size_t i = 0; i < b.size(); ++i) b[i] = 0.3f * (float) std::sin(2 * juce::MathConstants<double>::pi * 330 * i / SR) * (float) std::exp(-2.0 * i / SR) + 0.05f * (r.nextFloat() * 2 - 1);
        auto m = morphPrepare(a, b); Canvas c0, c1, ch; morph(*m, 0.0f, c0); morph(*m, 1.0f, c1); morph(*m, 0.5f, ch);
        const auto y0 = render(c0, Quality::HQ), y1 = render(c1, Quality::HQ), yh = render(ch, Quality::HQ);
        check(snrDb(a, y0) > 30 && snrDb(b, y1) > 30, "MORPH 0 = A and 1 = B", juce::String(snrDb(a, y0), 1) + " / " + juce::String(snrDb(b, y1), 1) + " dB");
        const double fh = peakHz(yh, 4096);
        check(finite(yh) && peakOf(yh) > 0.01f && fh > 230 && fh < 320, "MORPH 0.5 moves the partial between A and B", juce::String(fh, 1) + " Hz (A 220, B 330)");
        check(m->path.front() == std::make_pair(0, 0) && m->path.back() == std::make_pair(m->A.T - 1, m->B.T - 1), "MORPH time alignment spans both sounds");
    }
    { // seeds are deterministic, ghost + chaos
        Canvas c; c.resize(framesFor(3.0)); ChaosParams p; p.seed = 42; Canvas a = c, b = c, d = c;
        chaosPaint(a, p, {}, nullptr); chaosPaint(b, p, {}, nullptr); p.seed = 43; chaosPaint(d, p, {}, nullptr);
        check(a.amp == b.amp && a.fq == b.fq && a.amp != d.amp, "CHAOS: same seed = same picture, new seed = new picture");
        // locks survive a regenerate
        Sel lk; lk.t0 = 10; lk.t1 = 40; lk.r0 = 100; lk.r1 = 200; Canvas e = a; p.seed = 77; chaosPaint(e, p, { lk }, nullptr);
        bool kept = true; for (int t = lk.t0; t < lk.t1; ++t) for (int r = 100; r <= 200; ++r) if (e.A(t, r) != a.A(t, r)) kept = false;
        check(kept && e.amp != a.amp, "CHAOS: locked region is kept, the rest regenerates");
        for (float h : { 0.1f, 0.5f, 0.9f }) { ChaosParams q; q.harmony = h; q.chaos = 1; q.density = 1; q.motion = 1; Canvas f = c; chaosPaint(f, q, {}, &a); check(finite(f) && finite(render(f, Quality::Preview)), "CHAOS extremes stay finite (harmony " + juce::String(h) + ")"); }
        Canvas s; analyse(testSignal(1.0), s); GhostParams g; g.seed = 5; g.count = 8;
        auto s1 = ghostSet(s, g), s2 = ghostSet(s, g); bool same = s1.size() == 8; for (size_t i = 0; i < s1.size() && same; ++i) same = render(s1[i], Quality::Preview, 5) == render(s2[i], Quality::Preview, 5);
        check(same, "GHOST: same seed = same variations");
        bool fin = true; double minDiff = 1; for (size_t i = 0; i < s1.size(); ++i) { auto y = render(s1[i], Quality::Preview, 5); fin = fin && finite(y); if (i > 0 && i < 7) minDiff = std::min(minDiff, corr(render(s1[0], Quality::Preview, 5), y)); }
        check(fin && minDiff < 0.95, "GHOST: variations differ from the original and stay finite", "lowest corr to the original " + juce::String(minDiff, 3));
        g.evolve = 1; g.memory = 1; g.decay = 1; g.count = 16; auto s3 = ghostSet(s, g); bool f3 = true; for (auto& v : s3) f3 = f3 && finite(v); check(f3, "GHOST extremes stay finite");
    }
    { // undo 60 strokes
        Canvas c; c.resize(framesFor(3.0)); const Canvas orig = c; Undo u; Brush b; b.size = 5;
        for (int i = 0; i < 60; ++i) { const Canvas before = c; u.begin(c, i * 2, i * 2 + 12); paintAt(c, i * 2.0f + 5, 50.0f + i * 3, b); }
        const Canvas painted = c; int n = 0; while (u.undo(c)) ++n;
        check(n == 60 && c.amp == orig.amp, "UNDO: 60 strokes undo back to the blank canvas", juce::String(n) + " steps");
        while (u.redo(c)) {} check(c.amp == painted.amp, "REDO brings them all back");
    }
}

// ---------------------------------------------------------------- plugin tests (player, state)
static void pluginTests() {
    std::cout << "PLUGIN" << std::endl;
    { // MIDI keys play the canvas, pitch-tracked
        SpecProcessor p; p.prepareToPlay(48000, 512); auto& L = p.lab; L.open = true; L.playKeys = true; L.rootNote = 60;
        L.canvas.resize(framesFor(2.0)); Brush b; b.size = 1; b.level = 0.5f; for (int t = 0; t < L.canvas.T; ++t) paintAt(L.canvas, (float) t, (float) hzRow(1000.0), b, 1000.0);
        { const int v0 = L.bankVersion.load(); L.edited(); L.requestPreview(); for (int i = 0; i < 1500 && (L.bankVersion.load() <= v0 || L.busy.load()); ++i) juce::Thread::sleep(20); }
        juce::AudioBuffer<float> buf(2, 512); std::vector<float> out; juce::MidiBuffer m;
        for (int blk = 0; blk < 150; ++blk) { buf.clear(); m.clear(); if (blk == 2) m.addEvent(juce::MidiMessage::noteOn(1, 72, (juce::uint8) 110), 0); p.processBlock(buf, m); out.insert(out.end(), buf.getReadPointer(0), buf.getReadPointer(0) + 512); }
        const double f = peakHz(out, 4096); check(std::abs(f - 2000.0) < 6.0, "MIDI key C5 plays the 1 kHz canvas an octave up", juce::String(f, 1) + " Hz");
        check(finite(out) && peakOf(out) > 0.05f, "player output is finite and audible");
    }
    { // existing Spectrum state loads unchanged (a state from before the lab: no SPECTRALLAB, no labMorph)
        SpecProcessor a; a.model.pads.clear(); sp::PadDot d; d.pitch = 64; d.voice = 1; a.model.pads.push_back(d); d.pitch = 71; a.model.pads.push_back(d); a.model.root = 4; a.model.scale = 0; a.publish();
        auto old = a.apvts.copyState(); old.appendChild(a.toTree(), nullptr);
        for (int i = old.getNumChildren(); --i >= 0;) if (old.getChild(i).getProperty("id").toString().startsWith("lab")) old.removeChild(i, nullptr);   // no lab parameters back then
        juce::MemoryBlock mb; { auto xml = old.createXml(); juce::AudioProcessor::copyXmlToBinary(*xml, mb); }
        SpecProcessor b; b.setStateInformation(mb.getData(), (int) mb.getSize());
        const auto xa = a.toTree().createXml()->toString(), xb = b.toTree().createXml()->toString();
        check(xa == xb, "old state (no lab) loads exactly as before");
        check(!b.lab.open.load() && b.lab.output.load() == OutCanvas, "no lab data -> lab starts closed and blank");
        // new state round trip: lab canvas, settings and the old part
        a.lab.open = true; a.lab.output = OutSplit; a.lab.split.bands = 5; a.lab.ghost.seed = 99; Brush br; for (int t = 0; t < 40; ++t) paintAt(a.lab.canvas, (float) t, 120.0f, br); a.lab.edited();
        juce::MemoryBlock nb; a.getStateInformation(nb); SpecProcessor c; c.setStateInformation(nb.getData(), (int) nb.getSize());
        check(c.toTree().createXml()->toString() == xa && c.lab.open.load() && c.lab.output.load() == OutSplit && c.lab.split.bands == 5 && c.lab.ghost.seed == 99 && c.lab.canvas.amp == a.lab.canvas.amp, "lab state round-trips next to the old state");
    }
    { // SEND TO BEATS writes into Documents/Ghosty/Shared Sounds
        SpecProcessor p; Brush br; for (int t = 0; t < 60; ++t) paintAt(p.lab.canvas, (float) t, 150.0f, br, 440.0);
        juce::String sendErr; auto files = p.lab.sendToBeatsNow(120, sendErr); bool ok = files.size() == 1 && juce::File(files[0]).existsAsFile() && juce::File(files[0]).getParentDirectory() == LabHost::sharedSoundsDir();
        check(ok, "SEND TO BEATS saves a WAV in Shared Sounds", files.joinIntoString(";"));
        if (ok) { const auto y = readWavMono(juce::File(files[0])); check(std::abs(peakHz(y, 2048) - 440.0) < 3.0, "the sent WAV is the canvas sound"); juce::File(files[0]).deleteFile(); }
    }
}


// ---------------------------------------------------------------- FINAL GATE punch list: regression tests
static std::vector<float> richTone(double f0, double secs) { std::vector<float> x((size_t) (secs * SR)); for (size_t i = 0; i < x.size(); ++i) { double v = 0; for (int h = 1; h <= 8; ++h) v += std::sin(2 * juce::MathConstants<double>::pi * f0 * h * i / SR) / (h * 1.3); x[i] = (float) (0.15 * v * (0.5 + 0.5 * std::sin(i / SR * 0.7))); } return x; }
static std::vector<float> sineAt(double hz, double secs, double amp = 0.4, double rate = SR) { std::vector<float> x((size_t) (secs * rate)); for (size_t i = 0; i < x.size(); ++i) x[i] = (float) (amp * std::sin(2 * juce::MathConstants<double>::pi * hz * i / rate)); return x; }
static double rmsDb(const std::vector<float>& x, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b && i < x.size(); ++i) s += (double) x[i] * x[i]; return 10 * std::log10(std::max(1e-30, s / (double) std::max<size_t>(1, b - a))); }
static double secsSince(double t0) { return juce::Time::getMillisecondCounterHiRes() * 0.001 - t0; }
static double nowS() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }
template <typename F> static bool waitFor(F&& f, int ms = 8000) { for (int i = 0; i < ms / 10; ++i) { if (f()) return true; juce::MessageManager::getInstance()->runDispatchLoopUntil(10); } return f(); }
static int countPeaks(const std::vector<float>& x, size_t start, double lo, double hi, double thrDb) {
    constexpr int order = 16, M = 1 << order; juce::dsp::FFT fft(order); std::vector<float> b((size_t) M * 2, 0.0f);
    for (int i = 0; i < M && start + (size_t) i < x.size(); ++i) b[(size_t) i] = x[start + (size_t) i] * (float) (0.5 - 0.5 * std::cos(2 * juce::MathConstants<double>::pi * i / M));
    fft.performFrequencyOnlyForwardTransform(b.data(), true); float mx = 1e-12f; for (int i = 1; i < M / 2; ++i) mx = std::max(mx, b[(size_t) i]);
    int n = 0; for (int i = 2; i < M / 2 - 1; ++i) { const double hz = i * SR / M; if (hz < lo || hz > hi) continue; if (20 * std::log10(b[(size_t) i] / mx) > thrDb && b[(size_t) i] > b[(size_t) i - 1] && b[(size_t) i] >= b[(size_t) i + 1]) ++n; }
    return n;
}
static void playBlocks(SpecProcessor& p, int blocks, std::vector<float>& L, std::vector<float>& Rr, std::function<void(int, juce::MidiBuffer&)> midi = {}) {
    juce::AudioBuffer<float> buf(2, 512); juce::MidiBuffer m;
    for (int b = 0; b < blocks; ++b) { buf.clear(); m.clear(); if (midi) midi(b, m); p.processBlock(buf, m); L.insert(L.end(), buf.getReadPointer(0), buf.getReadPointer(0) + 512); Rr.insert(Rr.end(), buf.getReadPointer(1), buf.getReadPointer(1) + 512); }
}
static std::vector<float> drumLoop(double bpm, int bars, double ratio = 1, bool tonal = false);
static std::vector<float> synthKick(double ratio); static std::vector<float> synthHat(double ratio); static std::vector<float> synthSnare(double ratio);
static void punchTests() {
    std::cout << "FINAL GATE FIXES" << std::endl;
    { // 1. state size + time with a 30 s import and A / B loaded
        auto pHeap = std::make_unique<SpecProcessor>(); auto& p = *pHeap; auto x = richTone(110.0, 30.0); auto& L = p.lab; analyse(x, L.canvas); L.sourceAudio = x; L.audioA = x; L.audioB = richTone(165.0, 30.0); L.edited();
        juce::MemoryBlock st; double t0 = nowS(); p.getStateInformation(st); const double tFirst = secsSince(t0);
        t0 = nowS(); juce::MemoryBlock st2; p.getStateInformation(st2); const double tAgain = secsSince(t0);
        check(st.getSize() < (size_t) 12 << 20 && tAgain < 0.25, "30 s import + A + B: small, quick state", juce::String(st.getSize() / 1048576.0, 1) + " MB, first save " + juce::String(tFirst, 2) + " s, next " + juce::String(tAgain * 1000, 0) + " ms (was 63 MB / 3.96 s)");
        auto qHeap = std::make_unique<SpecProcessor>(); auto& q = *qHeap; t0 = nowS(); q.setStateInformation(st.getData(), (int) st.getSize()); const double tLoad = secsSince(t0);
        check(snrDb(x, render(q.lab.canvas, Quality::HQ)) > 60 && q.lab.audioB.size() == L.audioB.size() && tLoad < 2.0, "...and it loads back exact", juce::String(tLoad, 2) + " s, SNR " + juce::String(snrDb(x, render(q.lab.canvas, Quality::HQ)), 1) + " dB");
        // an edited part is kept exactly (only those frames are stored)
        Sel s; s.t0 = 100; s.t1 = 300; s.r0 = 0; s.r1 = R - 1; shift(L.canvas, s, 7); L.edited(); juce::MemoryBlock st3; p.getStateInformation(st3);
        auto rHeap = std::make_unique<SpecProcessor>(); auto& r = *rHeap; r.setStateInformation(st3.getData(), (int) st3.getSize());
        const auto ya = render(L.canvas, Quality::HQ), yb = render(r.lab.canvas, Quality::HQ);
        check(snrDb(ya, yb) > 40, "edited frames survive save / load", juce::String(snrDb(ya, yb), 1) + " dB, " + juce::String(st3.getSize() / 1048576.0, 1) + " MB");
        // a canvas saved by the first lab build (full float blocks) still loads
        Canvas c; analyse(sineAt(440, 1.0), c); juce::MemoryBlock v1; { juce::MemoryOutputStream raw(v1, false); juce::GZIPCompressorOutputStream z(raw, 6); z.writeInt(0x474c4231); z.writeInt(c.T); z.writeInt(c.srcLen); z.writeInt(K); z.writeInt(R);
            z.write(c.cells.data(), c.cells.size() * sizeof(Cell)); z.write(c.amp.data(), c.amp.size() * 4); z.write(c.fq.data(), c.fq.size() * 4); z.write(c.noise.data(), c.noise.size() * 4); z.flush(); }
        Canvas d; check(load(v1, d) && d.T == c.T && snrDb(render(c, Quality::HQ), render(d, Quality::HQ)) > 100, "first-format lab canvases still load");
        juce::MemoryBlock oldAudio; { juce::MemoryOutputStream raw(oldAudio, false); juce::GZIPCompressorOutputStream z(raw, 6); auto a = sineAt(220, 0.5); z.writeInt((int) a.size()); z.write(a.data(), a.size() * 4); z.flush(); }
        check(unpackAudio(oldAudio).size() == (size_t) (0.5 * SR), "first-format audio blocks still load");
    }
    { // 2. undo keeps to its RAM budget; IMAGE knob drags are one step each
        Undo u; Canvas c10; c10.resize(framesFor(10.0)); for (int i = 0; i < 60; ++i) u.beginAll(c10);
        check(u.bytes() <= Undo::kBudget + c10.bytes() + 4096, "undo: 60 whole-canvas steps stay inside the budget", juce::String((double) u.bytes() / 1048576.0, 0) + " MB, " + juce::String(u.depth()) + " steps (was 50 steps, +955 MB)");
        LabHost L; L.imageMap.seconds = 30; juce::Image im(juce::Image::RGB, 200, 100, true); { juce::Graphics g(im); g.fillAll(juce::Colours::white); }
        L.setImage(im); const int d0 = L.undo.depth(); const double t0 = nowS(); for (int i = 0; i < 40; ++i) { L.imageMap.seconds = 29.0 + i * 0.025; L.remapImage(false); }
        check(L.undo.depth() == d0, "IMAGE knob ticks add no undo steps", juce::String(L.undo.depth() - d0) + " extra steps, " + juce::String(secsSince(t0), 2) + " s for 40 ticks");
    }
    { // 3. brush power + the limiter
        Canvas cq; cq.resize(framesFor(4.0)); Brush b; b.size = 12; for (int t = 0; t < cq.T; ++t) paintAt(cq, (float) t, (float) hzRow(440.0), b);
        const auto y = render(cq, Quality::HQ); check(peakOf(y) < 1.3f, "SIZE 12 stroke at default BRIGHT stays near full scale", "peak " + juce::String(peakOf(y), 2) + " (was 6.04)");
        auto pHeap = std::make_unique<SpecProcessor>(); auto& p = *pHeap; p.prepareToPlay(48000, 512); auto& L = p.lab; L.open = true; L.playKeys = false; L.autoLevel = false; L.canvas.resize(framesFor(2.0)); b.level = 1.0f; b.size = 24;
        for (int t = 0; t < L.canvas.T; ++t) for (int r = 60; r < 300; r += 30) paintAt(L.canvas, (float) t, (float) r, b);
        for (int i = 0; i < 6; ++i) gainRegion(L.canvas, Sel { 0, L.canvas.T, 0, R - 1 }, 1.41f);
        { const int v0 = L.bankVersion.load(); L.edited(); L.requestPreview(); waitFor([&] { return L.bankVersion.load() > v0 && !L.busy.load(); }); }
        if (auto* lv = p.apvts.getParameter("labLevel")) lv->setValueNotifyingHost(1.0f);
        L.previewPlaying = true; std::vector<float> lo, ro; playBlocks(p, 120, lo, ro);
        check(std::max(peakOf(lo), peakOf(ro)) < 0.97f && L.clipCount.load() > 0, "limiter holds a far-too-loud canvas under 0 dBFS and lights the clip LED", "peak " + juce::String(std::max(peakOf(lo), peakOf(ro)), 3));
    }
    { // 4. ghosts are built one at a time
        Canvas c; analyse(richTone(110.0, 3.0), c); GhostParams g; g.count = 16; int n = 0; size_t mx = 0;
        ghostEach(c, g, [&](int, const Canvas& v) { ++n; mx = std::max(mx, v.bytes()); return true; });
        check(n == 16, "GHOST: 16 variations generated lazily", juce::String(n));
        Canvas l; analyse(richTone(110.0, 12.0), l); check(trimmed(l, framesFor(kGhostMaxSeconds)).T == framesFor(kGhostMaxSeconds), "GHOST uses at most the first 8 s");
    }
    { // 5. a held pad key is let go when the lab takes the keys
        auto pHeap = std::make_unique<SpecProcessor>(); auto& p = *pHeap; p.prepareToPlay(48000, 256); if (auto* ph = p.apvts.getParameter("padHold")) ph->setValueNotifyingHost(0.0f);
        juce::AudioBuffer<float> buf(2, 256); juce::MidiBuffer m; m.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0); buf.clear(); p.processBlock(buf, m);
        const bool g1 = p.engine.padMidiGate; p.lab.open = true; p.lab.playKeys = true;
        m.clear(); m.addEvent(juce::MidiMessage::noteOff(1, 60), 0); buf.clear(); p.processBlock(buf, m); m.clear(); buf.clear(); p.processBlock(buf, m);
        check(g1 && !p.engine.padMidiGate && p.engine.padStopped.load(), "pad key released while the lab has the keys -> no stuck pad");
    }
    { // 6. PLAY stops when the lab is closed
        auto pHeap = std::make_unique<SpecProcessor>(); auto& p = *pHeap; p.prepareToPlay(48000, 512); auto& L = p.lab; L.open = true; L.playKeys = false; Brush b; b.size = 1; L.canvas.resize(framesFor(1.0)); for (int t = 0; t < L.canvas.T; ++t) paintAt(L.canvas, (float) t, (float) hzRow(1000.0), b, 1000.0);
        { const int v0 = L.bankVersion.load(); L.edited(); L.requestPreview(); waitFor([&] { return L.bankVersion.load() > v0 && !L.busy.load(); }); }
        L.previewPlaying = true; std::vector<float> a, bb; playBlocks(p, 20, a, bb); L.open = false; std::vector<float> c, d; playBlocks(p, 20, c, d);
        check(peakOf(a) > 0.05f && peakOf(c) < 1e-6f, "the lab is silent once its tab is closed", "open " + juce::String(peakOf(a), 2) + ", closed " + juce::String(peakOf(c), 4));
    }
    { // 10. hostile session values are clamped
        auto pHeap = std::make_unique<SpecProcessor>(); auto& p = *pHeap; juce::ValueTree t("SPECTRALLAB"); t.setProperty("ghost", "100000,5,-3,9,1", nullptr); t.setProperty("level", 50.0, nullptr); t.setProperty("image", "0,-5,nan,0", nullptr); t.setProperty("split", "4,nan,100,50,20,10", nullptr);
        p.lab.fromTree(t); auto& L = p.lab; double sum = 0; for (int b = 0; b < L.split.bands; ++b) sum += splitMask(L.split, b, 1000.0);
        check(L.ghost.count <= kMaxGhosts && L.level.load() <= 1.0f && L.imageMap.seconds >= 0.25 && std::isfinite(L.split.xover[0]) && std::abs(sum - 1.0) < 1e-3, "hostile session values are clamped",
              "count " + juce::String(L.ghost.count) + ", level " + juce::String(L.level.load()) + ", image " + juce::String(L.imageMap.seconds) + " s, bands sum " + juce::String(sum, 3));
    }
    { // 11. two sends in one second never overwrite; odd names become legal
        auto pHeap = std::make_unique<SpecProcessor>(); auto& p = *pHeap; Brush br; for (int t = 0; t < 60; ++t) paintAt(p.lab.canvas, (float) t, 150.0f, br, 440.0);
        juce::String e; auto f1 = p.lab.sendToBeatsNow(120, e), f2 = p.lab.sendToBeatsNow(120, e); p.lab.sourceName = "a/b:c*d?"; auto f3 = p.lab.sendToBeatsNow(120, e);
        check(f1.size() == 1 && f2.size() == 1 && f1[0] != f2[0] && f3.size() == 1 && juce::File(f3[0]).getParentDirectory() == LabHost::sharedSoundsDir(), "SEND TO BEATS: unique, legal file names", juce::File(f3[0]).getFileName());
        for (auto* fs : { &f1, &f2, &f3 }) for (auto& f : *fs) juce::File(f).deleteFile();
    }
    { // 12. MORPH plays the exact morph, not a crossfade
        auto pHeap = std::make_unique<SpecProcessor>(); auto& p = *pHeap; p.prepareToPlay(48000, 512); auto& L = p.lab; L.open = true; L.playKeys = false; L.audioA = sineAt(220, 1.0); L.audioB = sineAt(330, 1.0);
        if (auto* o = p.apvts.getParameter("labOutput")) o->setValueNotifyingHost(o->convertTo0to1(3.0f));
        if (auto* m = p.apvts.getParameter("labMorph")) m->setValueNotifyingHost(0.4375f);
        L.output = OutMorph; L.requestMorph(); std::vector<float> a, b; playBlocks(p, 2, a, b);
        const bool ok = waitFor([&] { std::vector<float> x, y; playBlocks(p, 1, x, y); auto bk = L.latestBank(); return bk && bk->morphExact && std::abs(bk->morphExactT - 0.4375f) < 0.01f; }, 15000);
        L.audition.store(-1); std::vector<float> lo, ro; playBlocks(p, 120, lo, ro);
        // a crossfade of the 0.375 and 0.5 renders beats at ~4 Hz (deep dips); the exact morph is steady
        double mn = 1e9, mx = -1e9; for (size_t a0 = (size_t) (0.1 * SR); a0 + 2400 < (size_t) (0.9 * SR); a0 += 2400) { const double r = rmsDb(lo, a0, a0 + 2400); mn = std::min(mn, r); mx = std::max(mx, r); }
        const double f = peakHz(lo, 8192);
        check(ok && std::abs(f - 262.9) < 3.0 && mx - mn < 2.0, "MORPH 0.4375 plays the exact morph (one steady partial, no beating)", juce::String(f, 1) + " Hz, level swing " + juce::String(mx - mn, 1) + " dB");
    }
    { // 13. KEEP TRANSIENTS: a shifted click keeps its snap
        std::vector<float> ck((size_t) SR, 0.0f); const size_t at = (size_t) (SR * 0.5); ck[at] = 0.9f; Canvas cc, co; analyse(ck, cc); analyse(ck, co); Sel s; s.t0 = 0; s.t1 = cc.T; s.r0 = 0; s.r1 = R - 1;
        shift(cc, s, 12.0, true); shift(co, s, 12.0, false);
        RenderOpts keep; keep.side = false; RenderOpts loose = keep; loose.keepTransients = false;
        const auto y1 = renderStereo(cc, Quality::HQ, keep).m, y0 = renderStereo(co, Quality::HQ, loose).m;
        auto pre = [&](const std::vector<float>& y) { return rmsDb(y, at - (size_t) (SR * 0.025), at - 48) - rmsDb(y, at - 48, at + 48); };
        check(pre(y1) < -25.0 && peakOf(y1) > 0.3f, "SHIFT +12 on a click: little pre-echo with KEEP TRANSIENTS", juce::String(pre(y1), 1) + " dB, peak " + juce::String(peakOf(y1), 2) + " (off: " + juce::String(pre(y0), 1) + " dB, peak " + juce::String(peakOf(y0), 2) + ")");
        { // drums shifted +5 with KEEP TRANSIENTS: the attacks keep their true level - not sharper (the first version was +2.3 dB crest)
            const double ratio = std::exp2(5.0 / 12.0);
            auto crest = [](const std::vector<float>& v) { return 20 * std::log10(peakOf(v) / std::max(1e-9f, rmsOf(v))); };
            auto shifted = [](const std::vector<float>& x, bool keep) { Canvas a; analyse(x, a); Sel al; al.t1 = a.T; shift(a, al, 5.0, keep); RenderOpts o; o.side = false; o.keepTransients = keep; return renderStereo(a, Quality::HQ, o).m; };
            // pitched drums (kick + tonal snare): the exact +5 version exists, so its attack level is the reference
            const auto tx = drumLoop(120, 2, 1, true), tideal = drumLoop(120, 2, ratio, true), ty = shifted(tx, true), tz = shifted(tx, false);
            check(std::abs(20 * std::log10(peakOf(ty) / peakOf(tideal))) < 0.5, "KEEP TRANSIENTS: pitched drums' attacks land at their true level (the exact +5 version)",
                  "peak " + juce::String(peakOf(ty), 2) + " vs " + juce::String(peakOf(tideal), 2) + "; body rms " + juce::String(20 * std::log10(rmsOf(ty) / rmsOf(tz)), 1) + " dB vs the same shift without it");
            // the full loop (noisy snares + hats): the crest the first version over-sharpened (16.1 dB vs 13.8 dry)
            const auto x = drumLoop(120, 2), y = shifted(x, true), z = shifted(x, false);
            check(crest(y) - crest(z) < 0.3 && crest(y) - crest(x) < 0.6, "KEEP TRANSIENTS: drum loop crest like the dry loop, nothing added over a plain shift",
                  "keep " + juce::String(crest(y), 2) + " dB, plain shift " + juce::String(crest(z), 2) + " dB, dry " + juce::String(crest(x), 2) + " dB (was 16.1)");
        }
        auto rt = richTone(110.0, 2.0); Canvas cr; analyse(rt, cr); Sel a2 = s; a2.t1 = cr.T; shift(cr, a2, 7.0); const auto yr = render(cr, Quality::HQ);
        check(countPeaks(yr, 24000, 140, 1400, -40) >= 8, "...and a sustained tone shifted +7 keeps its harmonics", juce::String(countPeaks(yr, 24000, 140, 1400, -40)) + " partials");
    }
    { // 14. stereo: width spreads, the low end and the mid stay put
        Canvas c; c.resize(framesFor(2.0)); Brush b; b.size = 1; for (int t = 0; t < c.T; ++t) { paintAt(c, (float) t, 0, b, 80.0); for (double f : { 600.0, 1300.0, 2900.0, 6100.0 }) paintAt(c, (float) t, 0, b, f); }
        RenderOpts o; auto st = renderStereo(c, Quality::HQ, o); const auto mid = render(c, Quality::HQ);
        double es = 0, em = 0; for (size_t i = 0; i < st.size(); ++i) { es += (double) st.s[i] * st.s[i]; em += (double) st.m[i] * st.m[i]; }
        Canvas lowc; lowc.resize(framesFor(1.0)); for (int t = 0; t < lowc.T; ++t) paintAt(lowc, (float) t, 0, b, 70.0); auto lows = renderStereo(lowc, Quality::HQ, o);
        check(snrDb(mid, st.m) > 100 && es > 0.05 * em && peakOf(lows.s) < 1e-4f, "stereo side carries width, nothing below ~120 Hz", "side/mid " + juce::String(10 * std::log10(es / em), 1) + " dB, low side " + juce::String(peakOf(lows.s), 5));
    }
    { // 25. imported 44.1 kHz files land on time
        auto x44 = sineAt(1000.0, 1.0, 0.5, 44100.0); x44[22050] += 0.4f; auto r = resampleTo48k(x44.data(), (int) x44.size(), 44100.0);
        size_t best = 0; float bv = 0; for (size_t i = 23000; i < 25000; ++i) { const float d = std::abs(r[i] - (float) (0.5 * std::sin(2 * juce::MathConstants<double>::pi * 1000.0 * i / SR))); if (d > bv) { bv = d; best = i; } }
        check(std::abs((int) best - 24000) <= 2, "44.1 kHz import: no resampler delay", juce::String((int) best - 24000) + " samples (was 109)");
    }
    { // 28 / 30: image edge cases
        ImageMap m; m.loHz = 5000; m.hiHz = 1000; juce::Image im(juce::Image::RGB, 64, 64, true); { juce::Graphics g(im); g.fillAll(juce::Colours::white); } Canvas c; imageToCanvas(im, m, c);
        juce::Image gr(juce::Image::SingleChannel, 64, 64, true); { juce::Image::BitmapData bd(gr, juce::Image::BitmapData::writeOnly); for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) bd.setPixelColour(x, y, juce::Colours::white); }
        Canvas c2; imageToCanvas(gr, ImageMap(), c2);
        check(*std::max_element(c.amp.begin(), c.amp.end()) > 0.001f && *std::max_element(c2.amp.begin(), c2.amp.end()) > 0.01f, "IMAGE: LOW above HIGH and grey images still sing");
    }
    { // 29. partials above the top are reported
        Canvas c; c.resize(framesFor(1.0)); Brush b; for (int t = 0; t < c.T; ++t) paintAt(c, (float) t, 0, b, 440.0); Sel s; s.t0 = 0; s.t1 = c.T; s.r0 = 0; s.r1 = R - 1; int lost = 0; for (int i = 0; i < 6; ++i) lost += shift(c, s, 12.0);
        check(lost > 0, "SHIFT past 23 kHz reports the lost partials");
    }
    { // 22. SPLIT: polymeter, chance, swing, pitch
        BandSeq a; a.length = 3; BandSeq b; b.length = 4; check(evalBand(a, 3 * 0.25).step == 0 && evalBand(b, 3 * 0.25).step == 3, "SPLIT polymeter: 3-step and 4-step bands drift apart");
        BandSeq z; z.prob.fill(0); bool any = false; for (int i = 0; i < 64; ++i) any = any || evalBand(z, i * 0.25 + 0.01).gain > 0; check(!any, "SPLIT chance 0 % never plays");
        BandSeq h; h.prob.fill(50); int on = 0; for (int i = 0; i < 400; ++i) on += evalBand(h, i * 0.25 + 0.01, 0, 0, 7).gain > 0; check(on > 140 && on < 260, "SPLIT chance 50 % plays about half the time", juce::String(on) + " / 400");
        BandSeq sw; check(evalBand(sw, 0.25 + 0.05, 0, 0.4f).step == 0 && evalBand(sw, 0.25 + 0.15, 0, 0.4f).step == 1, "SPLIT swing delays every second step");
        BandSeq pt; pt.pitch.fill(12); const auto e = evalBand(pt, 0.1); check(std::abs(e.srcBeats - 0.2) < 1e-9, "SPLIT step pitch +12 reads the band twice as fast");
        SplitSetup s; s.xover[0] = 2000; s.xover[1] = 500; s.sanitise(); double sum = 0; for (int i = 0; i < s.bands; ++i) sum += splitMask(s, i, 1000.0); check(std::abs(sum - 1.0) < 1e-6, "SPLIT crossovers are kept in order (bands sum to 1)");
    }
    { // 24. SPECTRAL keys: same length at another pitch
        auto pHeap = std::make_unique<SpecProcessor>(); auto& p = *pHeap; p.prepareToPlay(48000, 512); auto& L = p.lab; L.open = true; L.playKeys = true; L.rootNote = 60; L.keysPitch = PitchSpectral;
        L.canvas.resize(framesFor(1.0)); Brush b; b.size = 1; for (int t = 0; t < L.canvas.T; ++t) paintAt(L.canvas, (float) t, 0, b, 500.0);
        { const int v0 = L.bankVersion.load(); L.edited(); L.requestPreview(); waitFor([&] { return L.bankVersion.load() > v0 && !L.busy.load(); }); }
        std::vector<float> a, bb; playBlocks(p, 4, a, bb, [](int blk, juce::MidiBuffer& m) { if (blk == 0) m.addEvent(juce::MidiMessage::noteOn(1, 72, (juce::uint8) 100), 0); if (blk == 2) m.addEvent(juce::MidiMessage::noteOff(1, 72), 0); });
        const bool ready = waitFor([&] { std::vector<float> x, y; playBlocks(p, 1, x, y); auto bk = L.latestBank(); return bk && bk->pitched[(size_t) (kPitchRange + 12)] != nullptr; }, 10000);
        std::vector<float> lo, ro; playBlocks(p, 160, lo, ro, [](int blk, juce::MidiBuffer& m) { if (blk == 1) m.addEvent(juce::MidiMessage::noteOn(1, 72, (juce::uint8) 100), 0); });
        size_t last = 0; for (size_t i = 0; i < lo.size(); ++i) if (std::abs(lo[i]) > 0.01f) last = i;
        const double len = (last - 512) / SR;
        check(ready && std::abs(peakHz(lo, 2048) - 1000.0) < 4.0 && std::abs(len - L.canvas.seconds()) < 0.08, "KEYS SPECTRAL: an octave up, same length", juce::String(peakHz(lo, 2048), 1) + " Hz, " + juce::String(len, 2) + " s vs " + juce::String(L.canvas.seconds(), 2) + " s");
    }
    { // 42. a host block larger than prepared
        auto pHeap = std::make_unique<SpecProcessor>(); auto& p = *pHeap; p.prepareToPlay(48000, 64); juce::AudioBuffer<float> b(2, 8192); juce::MidiBuffer m; b.clear(); p.processBlock(b, m);
        bool fin = true; for (int c = 0; c < 2; ++c) for (int i = 0; i < 8192; ++i) fin = fin && std::isfinite(b.getSample(c, i)); check(fin, "pad engine survives a block 128x larger than prepared");
    }
    { // 27. split / locks / lane edits undo
        LabHost L; L.undo.beginSide(); L.split.bands = 6; L.locks.push_back(Sel { 1, 5, 10, 20 }); L.morphLane[3].store(0.9f); L.sideEdited();
        L.undo.undo(L.canvas); check(L.split.bands == 4 && L.locks.empty() && std::abs(L.morphLane[3].load() - 0.45f) < 1e-3f && L.undo.redoDepth() == 1, "UNDO covers split, locks and the morph lane");
    }
}

// ---------------------------------------------------------------- demos + screenshots
static void writeDemo(const juce::File& dir, const juce::String& name, std::vector<float> x) {
    normalise(x, 0.89f); writeWav(dir.getChildFile(name + ".wav"), x); std::cout << "  wrote " << name << ".wav (" << juce::String(x.size() / SR, 1) << " s)" << std::endl;
}
static void writeDemoSt(const juce::File& dir, const juce::String& name, const Stereo& x, float width) {
    std::vector<float> L(x.m.size()), Rr(x.m.size()); for (size_t i = 0; i < L.size(); ++i) { const float sd = i < x.s.size() ? x.s[i] * width : 0.0f; L[i] = x.m[i] + sd; Rr[i] = x.m[i] - sd; }
    const float pk = std::max(peakOf(L), peakOf(Rr)); if (pk > 0) { const float g = 0.89f / pk; for (auto& v : L) v *= g; for (auto& v : Rr) v *= g; }
    writeWav(dir.getChildFile(name + ".wav"), L, Rr); std::cout << "  wrote " << name << ".wav (" << juce::String(L.size() / SR, 1) << " s, stereo)" << std::endl;
}
static float onePole(float a, double ratio) { const double fc = std::min(23000.0, -std::log(1.0 - a) * SR / (2 * juce::MathConstants<double>::pi) * ratio); return (float) (1.0 - std::exp(-2 * juce::MathConstants<double>::pi * fc / SR)); }   // a high-pass corner moved by ratio
static std::vector<float> synthKick(double ratio = 1) {
    std::vector<float> x((size_t) (SR * 0.35)); double ph = 0;
    for (size_t i = 0; i < x.size(); ++i) { const double t = i / SR; ph += 2 * juce::MathConstants<double>::pi * ratio * (48 + 140 * std::exp(-t * 30)) / SR; x[i] = (float) (0.9 * std::sin(ph) * std::exp(-t * 9)); }
    return x;
}
static std::vector<float> synthHat(double ratio = 1) { std::vector<float> x((size_t) (SR * 0.06)); juce::Random r(9); float lp = 0; const float a = onePole(0.9f, ratio); for (size_t i = 0; i < x.size(); ++i) { const float n = r.nextFloat() * 2 - 1; lp += a * (n - lp); x[i] = (n - lp) * 0.35f * (float) std::exp(-(i / SR) * 70); } return x; }
static std::vector<float> synthSnare(double ratio = 1);
static std::vector<float> drumLoop(double bpm, int bars, double ratio, bool tonal) {   // kick / snare / hats, 16ths
    const double s16 = 60.0 / bpm / 4 * SR; std::vector<float> y((size_t) (bars * 16 * s16 + SR * 0.5), 0.0f);
    auto put = [&](const std::vector<float>& x, double at, float g) { for (size_t i = 0; i < x.size() && (size_t) at + i < y.size(); ++i) y[(size_t) at + i] += g * x[i]; };
    const auto k = synthKick(ratio), sn = synthSnare(ratio), h = synthHat(ratio);
    for (int b = 0; b < bars; ++b) for (int st = 0; st < 16; ++st) { const double at = (b * 16 + st) * s16;
        if (st == 0 || st == 6 || st == 10) put(k, at, 1.0f);
        if (st == 4 || st == 12) { if (tonal) { std::vector<float> tn(sn.size()); for (size_t i = 0; i < tn.size(); ++i) { const double t = i / SR; tn[i] = (float) (0.55 * std::sin(2 * juce::MathConstants<double>::pi * ratio * (185 * t + 30 * (1 - std::exp(-t * 40)) / 40)) * std::exp(-t * 22)); } put(tn, at, 0.9f); } else put(sn, at, 0.9f); }
        if (!tonal) put(h, at, st % 2 ? 0.5f : 0.8f); }
    y.resize((size_t) (bars * 16 * s16)); return y;
}
static std::vector<float> synthSnare(double ratio) {
    std::vector<float> x((size_t) (SR * 0.45)); juce::Random r(4); float lp = 0; const float a = onePole(0.6f, ratio);
    for (size_t i = 0; i < x.size(); ++i) { const double t = i / SR; const float n = r.nextFloat() * 2 - 1; lp += a * (n - lp);
        x[i] = (float) (0.55 * std::sin(2 * juce::MathConstants<double>::pi * ratio * (185 * t + 30 * (1 - std::exp(-t * 40)) / 40)) * std::exp(-t * 22) + 0.5 * (n - lp) * std::exp(-t * 11)); }
    return x;
}
static void demos(const juce::File& dir) {
    dir.createDirectory(); const double bpm = 120;
    { // 1. painted melody: snapped lines in A minor with a soft noise breath and harmonics
        Canvas c; c.resize(framesFor(8.0)); Brush b; b.size = 1; b.hardness = 0.3f;
        const int mel[] = { 57, 60, 64, 62, 60, 64, 67, 69, 67, 64, 62, 60, 59, 60, 57, 57 };
        const double spb = 60.0 / bpm; const double fps = SR / HOP;
        for (int i = 0; i < 16; ++i) {
            const double f = midiHz((float) mel[i]); const int t0 = (int) (i * spb / 2 * fps), t1 = (int) ((i + 0.9) * spb / 2 * fps);
            for (int t = t0; t < t1; ++t) { const float env = std::exp(-2.0f * (t - t0) / (float) (t1 - t0)); for (int h = 1; h <= 5; ++h) { b.level = 0.3f * env / h; paintAt(c, (float) t, 0, b, f * h); } }
            b.level = 0.1f; b.noise = 1; for (int t = t0; t < t0 + 3; ++t) paintAt(c, (float) t, (float) hzRow(f * 6), b); b.noise = 0;
        }
        for (int t = 0; t < c.T; ++t) { b.level = 0.12f; paintAt(c, (float) t, 0, b, midiHz(45)); paintAt(c, (float) t, 0, b, midiHz(52)); }
        writeDemoSt(dir, "1 painted melody", renderStereo(c, Quality::HQ, RenderOpts()), 0.6f);
    }
    { // 2. image to sound: the ghost face
        ImageMap m; m.seconds = 6; m.loHz = 120; m.hiHz = 9000; Canvas c; imageToCanvas(ghostFace(512, 512), m, c); writeDemoSt(dir, "2 image to sound ghost face", renderStereo(c, Quality::HQ, RenderOpts()), 0.7f);
    }
    { // 3. evolving snare: SPECTRAL GHOST over 16 hits on 8ths
        Canvas c; analyse(synthSnare(), c); GhostParams g; g.count = 16; g.evolve = 0.85f; g.memory = 0.55f; g.decay = 0.15f; g.seed = 7;
        auto set = ghostSet(c, g); const int step = (int) (60.0 / bpm / 2 * SR); std::vector<float> y((size_t) step * 16 + (size_t) SR, 0.0f);
        for (int rep = 0; rep < 2; ++rep) for (size_t i = 0; i < set.size(); ++i) { (void) rep; }
        for (size_t i = 0; i < set.size(); ++i) { auto a = render(set[i], Quality::HQ, 7); for (size_t k = 0; k < a.size() && i * (size_t) step + k < y.size(); ++k) y[i * (size_t) step + k] += a[k]; }
        writeDemo(dir, "3 evolving snare spectral ghost", y);
    }
    { // 4. split rhythms: a chord pad split into 4 bands with their own patterns
        Canvas c; c.resize(framesFor(8.0)); Brush b; b.size = 1; b.hardness = 0.2f;
        const int chord[] = { 45, 57, 60, 64, 69, 72, 76 };
        for (int t = 0; t < c.T; ++t) for (int n : chord) for (int h = 1; h <= 6; ++h) { b.level = 0.12f / h; paintAt(c, (float) t, 0, b, midiHz((float) n) * h * (1.0 + 0.002 * h)); }
        b.noise = 1; b.size = 6; b.level = 0.05f; for (int t = 0; t < c.T; ++t) paintAt(c, (float) t, (float) hzRow(7000), b);
        auto x = render(c, Quality::HQ); SplitSetup s; s.bands = 4; s.xover = { 200, 800, 3000, 9000, 14000 };
        const char* pats[] = { "1000100010001010", "0010001000100011", "1011011010110110", "1111111111111111" };
        for (int i = 0; i < 4; ++i) for (int k = 0; k < 16; ++k) s.seq[(size_t) i].steps[(size_t) k] = pats[i][k] == '1';
        s.seq[0].gate = 0.9f; s.seq[1].gate = 0.5f; s.seq[2].rateDiv = 16; s.seq[2].gate = 0.35f; s.seq[3].rateDiv = 32; s.seq[3].retrig = true; s.seq[3].gate = 0.25f; s.seq[1].rateDiv = 16;
        writeDemo(dir, "4 split rhythms", renderSplit(splitBands(x, s), s, bpm, (int) x.size()));
    }
    { // 5. chaos paint: two seeds in A minor, the second keeping a locked box from the first
        Canvas c; c.resize(framesFor(8.0)); ChaosParams q; q.seed = 5; q.chaos = 0.55f; q.density = 0.55f; q.harmony = 0.75f; q.motion = 0.45f; q.root = 9; q.scale = 1; q.bpm = bpm;
        chaosPaint(c, q, {}, nullptr); auto y = render(c, Quality::HQ);
        Sel lk; lk.t0 = 0; lk.t1 = c.T / 2; lk.r0 = 0; lk.r1 = R - 1; q.seed = 11; q.chaos = 0.9f; chaosPaint(c, q, { lk }, nullptr);
        auto y2 = render(c, Quality::HQ); y.insert(y.end(), y2.begin(), y2.end());
        writeDemo(dir, "5 chaos paint two seeds", y);
    }
    { // 6. spectral DNA: a bright pluck chord morphs into the evolving snare, 5 steps (0, 25, 50, 75, 100 %)
        std::vector<float> a((size_t) (SR * 1.2)); const double fr[] = { 220.0, 277.18, 329.63, 440.0 };
        for (size_t i = 0; i < a.size(); ++i) { const double t = i / SR; double v = 0; for (double f : fr) for (int h = 1; h <= 8; ++h) v += std::sin(2 * juce::MathConstants<double>::pi * f * h * t) / (h * 1.6); a[i] = (float) (0.08 * v * std::exp(-t * 2.5)); }
        auto snare = synthSnare(); auto m = morphPrepare(a, snare); std::vector<float> y;
        for (int k = 0; k <= 4; ++k) { Canvas c; morph(*m, k / 4.0f, c); auto z = render(c, Quality::HQ); normalise(z, 0.8f); z.resize((size_t) (SR * 1.25), 0.0f); y.insert(y.end(), z.begin(), z.end()); }
        writeDemo(dir, "6 spectral dna morph pluck to snare", y);
    }
    { // 7. a drum loop shifted +5 semitones: KEEP TRANSIENTS off, then on (the snap comes back)
        const auto x = drumLoop(bpm, 2); Sel all; Canvas a, b; analyse(x, a); analyse(x, b); all.t1 = a.T;
        shift(a, all, 5.0, false); shift(b, all, 5.0, true); RenderOpts off; off.keepTransients = false; RenderOpts on;
        auto ya = renderStereo(a, Quality::HQ, off), yb = renderStereo(b, Quality::HQ, on); Stereo y; y.m = x; y.s.assign(x.size(), 0.0f);
        for (auto* z : { &ya, &yb }) { y.m.insert(y.m.end(), z->m.begin(), z->m.end()); y.s.insert(y.s.end(), z->s.begin(), z->s.end()); }
        writeDemoSt(dir, "7 drums dry then shift +5 smeared then shift +5 keep transients", y, 0.4f);
    }
    { // 8. SPLIT for the IDM lens: polymeter (5 / 7 / 16 / 3 steps), chance, swing, pitched steps, retrig on the top band
        const auto x = drumLoop(bpm, 4); SplitSetup s; s.bands = 4; s.xover = { 150, 700, 4000, 9000, 14000 }; s.swing = 0.22f; s.seed = 5;
        const char* pats[] = { "10110", "1011010", "1111111111111111", "101" }; const int lens[] = { 5, 7, 16, 3 };
        for (int i = 0; i < 4; ++i) { auto& b = s.seq[(size_t) i]; b.length = lens[i]; for (int k = 0; k < lens[i]; ++k) b.steps[(size_t) k] = pats[i][k] == '1'; }
        s.seq[1].pitch[2] = 7; s.seq[1].pitch[5] = -5; s.seq[2].prob.fill(70); s.seq[2].rateDiv = 32; s.seq[2].gate = 0.4f; s.seq[3].retrig = true; s.seq[3].rateDiv = 32; s.seq[3].pitch[1] = 12;
        writeDemo(dir, "8 split polymeter chance swing pitched steps", renderSplit(splitBands(x, s), s, bpm, (int) x.size()));
    }
}

static void pump(int ms);
static juce::Component* findRec(juce::Component* c, const juce::String& id);
// ---------------------------------------------------------------- ARRANGE + ARCS wiring, BYPASS
static int clipCount(SpecProcessor& p) { int n = 0; for (auto& t : p.arrange.arrangement.tracks) n += (int) t.clips.size(); return n; }
static void pagesAndBypassTests() {
    std::cout << "ARRANGE / ARCS / BYPASS" << std::endl;
    { // an empty arrangement and an empty ARCS page add exactly nothing (old sessions sound bit-identical)
        SpecProcessor p; p.prepareToPlay(48000, 512); juce::AudioBuffer<float> b(2, 512); juce::Random r(3);
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) b.setSample(c, i, r.nextFloat() - 0.5f);
        juce::AudioBuffer<float> ref(b); bool same = true;
        for (int k = 0; k < 20; ++k) { p.arrange.process(b, arr::HostPos()); p.arcsPage.process(b, arcs::HostInfo(), true, nullptr); }
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) same = same && b.getSample(c, i) == ref.getSample(c, i);
        check(same, "empty ARRANGE + empty ARCS add exactly nothing to the output");
    }
    { // state: the arrangement (binary, after the XML) and the ARCS page round-trip; old sessions load empty
        SpecProcessor a; a.prepareToPlay(48000, 512);
        juce::AudioBuffer<float> tone(2, 48000); for (int i = 0; i < 48000; ++i) { const float v = 0.3f * (float) std::sin(2 * juce::MathConstants<double>::pi * 440 * i / 48000.0); tone.setSample(0, i, v); tone.setSample(1, i, v); }
        a.arrange.receive(arr::makePayload(tone, 48000, "tone"), 0, 0); a.arrange.receive(arr::makePayload(tone, 48000, "tone 2"), 1, 4);
        a.arcsPage.edit() = arcs::factory::pages()[1].make(); a.arcsPage.commit();
        juce::MemoryBlock st; a.getStateInformation(st);
        SpecProcessor b; b.setStateInformation(st.getData(), (int) st.getSize()); b.arrange.applyPendingState();
        check(clipCount(b) == 2 && b.arcsPage.page().arcs.size() == a.arcsPage.page().arcs.size() && !b.arcsPage.page().arcs.empty(), "ARRANGE clips and the ARCS page round-trip in the session",
              juce::String(clipCount(b)) + " clips, " + juce::String((int) b.arcsPage.page().arcs.size()) + " lines, state " + juce::String((int) (st.getSize() / 1024)) + " KB");
        auto xml = juce::AudioProcessor::getXmlFromBinary(st.getData(), (int) st.getSize());
        check(xml != nullptr && xml->getChildByName("ARCSPAGE") != nullptr, "an older build still reads the session's XML part (the arrangement sits after it)");
        // a session saved before these pages: no blob, no ARCSPAGE -> empty arrangement, default page (even on an instance that had both)
        auto old = a.apvts.copyState(); old.appendChild(a.toTree(), nullptr); juce::MemoryBlock ob; { auto x = old.createXml(); juce::AudioProcessor::copyXmlToBinary(*x, ob); }
        b.setStateInformation(ob.getData(), (int) ob.getSize()); b.arrange.applyPendingState();
        check(clipCount(b) == 0 && b.arcsPage.page().arcs.empty(), "an old session loads with an empty ARRANGE and an empty ARCS page");
    }
    { // SEND TO ARRANGE: what the Lab plays becomes a clip (with the snapped lines as MIDI notes) that ARRANGE can re-render
        SpecProcessor p; p.prepareToPlay(48000, 512); auto& L = p.lab; L.canvas.resize(framesFor(2.0)); Brush b; b.size = 1; b.level = 0.5f;
        for (int t = 10; t < 80; ++t) paintAt(L.canvas, (float) t, 0, b, midiHz(60)); for (int t = 90; t < 160; ++t) paintAt(L.canvas, (float) t, 0, b, midiHz(67));
        L.edited(); std::shared_ptr<LabHost::ExportSound> got;
        L.renderExport(120, [&](std::shared_ptr<LabHost::ExportSound> e) { got = e; }); waitFor([&] { return got != nullptr; }, 10000);
        juce::uint32 id = got ? p.arrange.receive(SpecProcessor::toPayload(*got)) : 0; const arr::Clip* c = p.arrange.arrangement.findClip(id);
        check(id != 0 && c && c->source && c->source->notes.size() == 2 && c->source->notes[0].pitch == 60 && c->source->notes[1].pitch == 67 && std::abs(c->source->seconds() - L.canvas.seconds()) < 0.05,
              "SEND TO ARRANGE: the Lab sound lands on the timeline with its notes", c && c->source ? juce::String((int) c->source->notes.size()) + " notes, " + juce::String(c->source->seconds(), 2) + " s" : juce::String("no clip"));
        auto again = c ? p.arrange.rerender(c->source->source) : nullptr; double snr = 0;
        if (again && c) { const auto& A = c->source->audio; const auto& B2 = again->audio; double se = 0, ee = 0; for (int i = 0; i < std::min(A.getNumSamples(), B2.getNumSamples()); ++i) { const double x = A.getSample(0, i), y = B2.getSample(0, i); se += x * x; ee += (x - y) * (x - y); } snr = 10 * std::log10(se / std::max(1e-30, ee)); }
        check(again != nullptr && snr > 60, "ARRANGE can re-render a Lab clip from its saved state", juce::String(snr, 1) + " dB");
    }
    { // BYPASS: the last parameter, the host's bypass, automatable, off by default, saved, old sessions load off
        SpecProcessor p; auto params = p.getParameters();
        check(p.getBypassParameter() != nullptr && params[params.size() - 2] == p.getBypassParameter() && p.getBypassParameter()->isAutomatable() && p.getBypassParameter()->getName(32) == "Bypass" && !p.bypassParam->get(),
              "BYPASS is the host's bypass, after every sound parameter (only PLAY follows it), automatable, off by default");
        p.bypassParam->setValueNotifyingHost(1.0f); juce::MemoryBlock st; p.getStateInformation(st);
        SpecProcessor q; q.setStateInformation(st.getData(), (int) st.getSize()); const bool on = q.bypassParam->get();
        auto old = p.apvts.copyState(); for (int i = old.getNumChildren(); --i >= 0;) if (old.getChild(i).getProperty("id").toString() == "bypass") old.removeChild(i, nullptr);
        juce::MemoryBlock ob; { auto x = old.createXml(); juce::AudioProcessor::copyXmlToBinary(*x, ob); }
        SpecProcessor r2; r2.bypassParam->setValueNotifyingHost(1.0f); r2.setStateInformation(ob.getData(), (int) ob.getSize());
        check(on && !r2.bypassParam->get(), "BYPASS is saved with the session; an older session loads with it off");
    }
    { // BYPASS off = bit-identical: two identical instances, one with BYPASS explicitly off, play the same
        auto run = [](bool touch) { SpecProcessor p; p.prepareToPlay(48000, 512); if (touch) p.bypassParam->setValueNotifyingHost(0.0f);
            auto& L = p.lab; L.open = true; L.playKeys = false; L.canvas.resize(framesFor(1.0)); Brush b; b.size = 1; for (int t = 0; t < L.canvas.T; ++t) paintAt(L.canvas, (float) t, 0, b, 440.0);
            { const int v0 = L.bankVersion.load(); L.edited(); L.requestPreview(); waitFor([&] { return L.bankVersion.load() > v0 && !L.busy.load(); }); }
            L.previewPlaying = true; std::vector<float> lo, ro; playBlocks(p, 40, lo, ro); return lo; };
        const auto a = run(false), b = run(true); check(a == b && peakOf(a) > 0.01f, "BYPASS off: the output is bit-identical");
        juce::AudioBuffer<float> x(2, 256); juce::Random r(5); for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i) x.setSample(c, i, r.nextFloat()); juce::AudioBuffer<float> y(x);
        ghosty::BypassFader f; f.prepare(48000); f.set(false); f.applyMute(y); bool same = true; for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i) same = same && x.getSample(c, i) == y.getSample(c, i);
        check(same, "BYPASS off: the fader leaves every sample untouched");
    }
    { // BYPASS on with the Lab, ARRANGE and ARCS all playing: silent within 20 ms, no click, no stuck notes, MIDI ignored
        SpecProcessor p; p.prepareToPlay(48000, 128); auto& L = p.lab; L.open = true; L.playKeys = false; L.canvas.resize(framesFor(1.0)); Brush b; b.size = 1; for (int t = 0; t < L.canvas.T; ++t) paintAt(L.canvas, (float) t, 0, b, 330.0);
        { const int v0 = L.bankVersion.load(); L.edited(); L.requestPreview(); waitFor([&] { return L.bankVersion.load() > v0 && !L.busy.load(); }); }
        L.previewPlaying = true;
        juce::AudioBuffer<float> tone(2, 96000); for (int i = 0; i < 96000; ++i) { const float v = 0.3f * (float) std::sin(2 * juce::MathConstants<double>::pi * 220 * i / 48000.0); tone.setSample(0, i, v); tone.setSample(1, i, v); }
        p.arrange.receive(arr::makePayload(tone, 48000, "tone"), 0, 0); p.arrange.waitUntilUpToDate(); p.arrange.play();
        p.arcsPage.edit() = arcs::factory::pages()[1].make(); p.arcsPage.commit(); p.arcsPage.engine.internalPlay.store(true);
        std::vector<float> pre, preR; for (int k = 0; k < 120; ++k) { juce::AudioBuffer<float> buf(2, 128); buf.clear(); juce::MidiBuffer m; p.processBlock(buf, m); pre.insert(pre.end(), buf.getReadPointer(0), buf.getReadPointer(0) + 128); }
        const bool allPlaying = L.previewPlaying.load() && p.arrange.isPlaying() && p.arcsPage.engine.isPlaying.load();
        p.bypassParam->setValueNotifyingHost(1.0f);
        std::vector<float> out; for (int k = 0; k < 200; ++k) { juce::AudioBuffer<float> buf(2, 128); buf.clear(); juce::MidiBuffer m; if (k == 50) m.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0); p.processBlock(buf, m); out.insert(out.end(), buf.getReadPointer(0), buf.getReadPointer(0) + 128); }
        float after = 0; for (size_t i = 960; i < out.size(); ++i) after = std::max(after, std::abs(out[i]));
        float jumpPre = 0, jumpFade = 0; for (size_t i = pre.size() - 2000; i + 1 < pre.size(); ++i) jumpPre = std::max(jumpPre, std::abs(pre[i + 1] - pre[i]));
        { float prev = pre.back(); for (size_t i = 0; i < 960; ++i) { jumpFade = std::max(jumpFade, std::abs(out[i] - prev)); prev = out[i]; } }
        check(allPlaying && peakOf(pre) > 0.05f && (after < 1e-6f), "BYPASS: silent (< -120 dBFS) within 20 ms with the Lab, ARRANGE and ARCS playing",
              "before " + juce::String(20 * std::log10(peakOf(pre)), 1) + " dBFS, after 20 ms " + (after > 0 ? juce::String(20 * std::log10(after), 1) + " dBFS" : juce::String("digital silence")));
        check(jumpFade <= jumpPre * 1.05f + 1e-6f, "BYPASS: the fade adds no click (no bigger sample step than the music itself)", "largest step " + juce::String(jumpFade, 4) + " vs " + juce::String(jumpPre, 4));
        check(!L.previewPlaying.load() && !p.arrange.isPlaying() && !p.arcsPage.engine.internalPlay.load() && p.engine.padStopped.load(), "BYPASS stops the Lab player, ARRANGE's and ARCS' own PLAY and the pad");
        p.bypassParam->setValueNotifyingHost(0.0f); std::vector<float> rel; for (int k = 0; k < 1500; ++k) { juce::AudioBuffer<float> buf(2, 128); buf.clear(); juce::MidiBuffer m; p.processBlock(buf, m); rel.insert(rel.end(), buf.getReadPointer(0), buf.getReadPointer(0) + 128); }
        float tail = 0; for (size_t i = rel.size() - 24000; i < rel.size(); ++i) tail = std::max(tail, std::abs(rel[i]));
        check(tail < 1e-4f, "BYPASS released: nothing is left ringing and the note sent while bypassed never plays (no stuck notes)", "last 0.5 s peak " + juce::String(tail > 0 ? 20 * std::log10(tail) : -200.0f, 1) + " dBFS");
    }
}
static void pagesUiTests() {
    std::cout << "TABS (events under Xvfb)" << std::endl;
    SpecProcessor p; p.prepareToPlay(48000, 512); std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor()); ed->setVisible(true); pump(200);
    bool allTabs = true; for (auto& d : sp::tabs()) allTabs = allTabs && dynamic_cast<juce::Button*>(findRec(ed.get(), d.id)) != nullptr;
    check(allTabs && ed->getHeight() == 790 + SpecEditor::kBarH, "the tab bar is built from the tab table (every tab present)");
    auto click = [&](const char* id) { if (auto* b = dynamic_cast<juce::Button*>(findRec(ed.get(), id))) b->triggerClick(); pump(60); };
    auto shown = [&](const char* id) { auto* c = findRec(ed.get(), id); for (auto* x = c; x != nullptr && x != ed.get(); x = x->getParentComponent()) if (!x->isVisible()) return false; return c != nullptr; };
    auto* by = findRec(ed.get(), "bypassButton");
    click("tab_arrange"); const bool arr1 = shown("arrangePage"), byA = shown("bypassButton");
    click("tab_arcs"); const bool arc1 = shown("arcsPage") && !shown("arrangePage"), byC = shown("bypassButton");
    click("labButton"); const bool lab1 = shown("spectralLab") && !shown("arcsPage") && p.lab.open.load(), byL = shown("bypassButton");
    click("tab_loop"); const bool loop1 = !shown("spectralLab") && p.model.mode == 1 && !p.lab.open.load();
    check(arr1 && arc1 && lab1 && loop1, "tabs switch between PAINT, SPECTRAL LAB, ARCS and ARRANGE");
    check(byA && byC && byL && by && by->getWidth() == 84 && by->getHeight() == 26, "BYPASS (84 x 26) is in the bar on every tab");
    if (auto* bb = dynamic_cast<juce::Button*>(by)) { bb->triggerClick(); pump(60); } const bool onNow = p.bypassParam->get();
    if (auto* bb = dynamic_cast<juce::Button*>(by)) { bb->triggerClick(); pump(60); }
    check(onNow && !p.bypassParam->get(), "clicking BYPASS toggles the host parameter");
    ed.reset(); pump(50);
}

// ---------------------------------------------------------------- LOOK: owner's report (see-through strip, mixed palettes, lime frame, doubled headings)
static juce::Image snapshotOf(juce::Component& c, float scale = 1.0f) { return c.createComponentSnapshot(c.getLocalBounds(), true, scale); }
static int colourDist(juce::Colour a, juce::Colour b) { return std::abs(a.getRed() - b.getRed()) + std::abs(a.getGreen() - b.getGreen()) + std::abs(a.getBlue() - b.getBlue()); }
static void labLookTests() {
    std::cout << "LOOK (themes x pages x sizes, under Xvfb)" << std::endl;
    SpecProcessor p; p.prepareToPlay(48000, 512);
    std::unique_ptr<juce::AudioProcessorEditor> edp(p.createEditor()); auto* ed = dynamic_cast<SpecEditor*>(edp.get()); ed->setVisible(true); pump(150);
    if (auto* b = dynamic_cast<juce::Button*>(findRec(ed, "labButton"))) b->triggerClick(); pump(80);
    auto* panel = dynamic_cast<LabPanel*>(findRec(ed, "spectralLab")); if (!panel) { check(false, "LOOK: lab panel"); return; }
    { auto& L = p.lab; analyse(testSignal(3.0), L.canvas); L.edited(); L.requestPreview(); waitFor([&] { return !L.busy.load(); }); }
    const juce::Rectangle<int> strip { 244, 568, 940, 214 };
    int seeThrough = 0, stripBad = 0, lanesBad = 0, lime = 0, combos = 0; juce::String worst;
    for (int dark = 1; dark >= 0; --dark) {
        ed->setTheme(dark == 1); pump(40);
        for (int pg = 0; pg < 6; ++pg) {
            panel->showPage(pg); pump(60);
            const juce::String tag = juce::String(dark ? "dark" : "light") + " page " + juce::String(pg);
            // 1. the panel covers its whole area: painted alone onto a fully transparent image, no pixel may stay see-through
            juce::Image img(juce::Image::ARGB, panel->getWidth(), panel->getHeight(), true);
            { juce::Graphics g(img); panel->paintEntireComponent(g, true); }
            int holes = 0; for (int y = 0; y < img.getHeight(); y += 2) for (int x = 0; x < img.getWidth(); x += 2) if (img.getPixelAt(x, y).getAlpha() < 255) ++holes;
            if (holes) { ++seeThrough; worst << tag << ": " << holes << " see-through px; "; }
            // 2. under each strip: wherever no control sits, the pixels are exactly the panel colour (no leftover window rectangle)
            juce::RectangleList<int> kids; for (auto* c : panel->getChildren()) if (c->isVisible() && strip.intersects(c->getBounds())) kids.add(c->getBounds().expanded(3));
            const auto panelCol = sp::pal().panel; int off = 0;
            for (int y = strip.getY() + 44; y < strip.getBottom() - 4; y += 3) for (int x = strip.getX() + 10; x < strip.getRight() - 10; x += 3) {
                if (kids.containsPoint({ x, y })) continue;
                if (colourDist(img.getPixelAt(x, y), panelCol) > 6) ++off; }
            if (off) { ++stripBad; worst << tag << ": " << off << " strip px off-colour; "; }
            // 3. title + help lanes (rows 1 and 2 of the strip) are free: only the status label may sit there
            for (auto* c : panel->getChildren()) if (c->isVisible() && c->getBounds().intersects(strip) && c->getY() < strip.getY() + 42 && c->getProperties()["role"].toString() != "status") { ++lanesBad; worst << tag << ": " << c->getName() << c->getComponentID() << " in the heading lanes; "; }
        }
        // 4. no lime frame: the outer 14 px of the whole window, at 100 / 125 / 200 %
        for (float sc : { 1.0f, 1.25f, 2.0f }) {
            ed->setSize(juce::roundToInt(1200 * sc), juce::roundToInt((790 + SpecEditor::kBarH) * sc)); pump(40);
            // a frame would light up a whole edge: count lime per edge band (the logo's own green motion lines at the top-left are excluded)
            auto shot = snapshotOf(*ed); const int band = juce::jmax(2, juce::roundToInt(5 * sc)); const auto logo = juce::Rectangle<int>(0, 0, juce::roundToInt(80 * sc), juce::roundToInt((SpecEditor::kBarH + 80) * sc));
            int edge[4] = { 0, 0, 0, 0 }; const int Wd = shot.getWidth(), Ht = shot.getHeight();
            auto isLime = [&](int x, int y) { return !logo.contains(x, y) && colourDist(shot.getPixelAt(x, y), juce::Colour(0xff3cff8f)) < 60; };
            for (int x = 0; x < Wd; ++x) for (int y = 0; y < band; ++y) { edge[0] += isLime(x, y); edge[1] += isLime(x, Ht - 1 - y); }
            for (int y = 0; y < Ht; ++y) for (int x = 0; x < band; ++x) { edge[2] += isLime(x, y); edge[3] += isLime(Wd - 1 - x, y); }
            const bool frame = edge[0] > Wd * band / 10 || edge[1] > Wd * band / 10 || edge[2] > Ht * band / 10 || edge[3] > Ht * band / 10;   // > 10 % of an edge
            if (frame) { ++lime; worst << (dark ? "dark" : "light") << " x" << sc << ": lime on the frame top " << edge[0] << " bottom " << edge[1] << " left " << edge[2] << " right " << edge[3] << "; "; }
            int holes = 0; for (int y = 0; y < shot.getHeight(); y += 3) for (int x = 0; x < shot.getWidth(); x += 3) if (shot.getPixelAt(x, y).getAlpha() < 255) ++holes;
            if (holes) { ++seeThrough; worst << "editor x" << sc << " see-through; "; }
        }
        ed->setSize(1200, 790 + SpecEditor::kBarH); pump(30);
        (void) combos;
    }
    check(seeThrough == 0, "LOOK: every Lab page covers its whole area (no see-through pixel), both themes, 100 / 125 / 200 %", worst.substring(0, 300));
    check(stripBad == 0, "LOOK: under every strip the free space is exactly the panel colour (no leftover window-coloured rectangle)", worst.substring(0, 300));
    check(lanesBad == 0, "LOOK: page title, help line and status have their own lanes (no control under them)", worst.substring(0, 300));
    check(lime == 0, "LOOK: no lime-green frame around the plugin, both themes, 100 / 125 / 200 %", worst.substring(0, 300));
    // 5. one palette per theme: in LIGHT, the panel, the strip and the tab bar are all light; in DARK all dark
    for (int dark = 1; dark >= 0; --dark) {
        ed->setTheme(dark == 1); pump(40); panel->showPage(1); pump(40);
        auto shot = snapshotOf(*ed); const int bar = SpecEditor::kBarH;
        const float side = shot.getPixelAt(30, bar + 300).getBrightness(), stripB = shot.getPixelAt(1150, bar + 770).getBrightness(), top = shot.getPixelAt(600, 4).getBrightness(), win = shot.getPixelAt(5, bar + 400).getBrightness();
        const bool ok = dark ? (side < 0.25f && stripB < 0.25f && top < 0.25f && win < 0.25f) : (side > 0.8f && stripB > 0.8f && top > 0.8f && win > 0.8f);
        check(ok, juce::String("LOOK: one coherent ") + (dark ? "DARK" : "LIGHT") + " palette (tools column, strip, tab bar, window)", "brightness side " + juce::String(side, 2) + " strip " + juce::String(stripB, 2) + " bar " + juce::String(top, 2) + " window " + juce::String(win, 2));
    }
    ed->setTheme(true); edp.reset(); pump(50);
}
static void fixShots(const juce::File& dir) {
    dir.createDirectory();
    SpecProcessor p; p.guideSeen = 0x7fffffff; p.prepareToPlay(48000, 512);   // (no first-run bubbles in these shots)
    std::unique_ptr<juce::AudioProcessorEditor> edp(p.createEditor()); auto* ed = dynamic_cast<SpecEditor*>(edp.get()); ed->setVisible(true); pump(150);
    auto& L = p.lab; analyse(testSignal(3.0), L.canvas); L.edited(); L.requestPreview(); L.audioA = sineAt(220, 1.0); L.audioB = synthSnare(1.0); L.nameA = "Tone"; L.nameB = "Snare"; L.requestMorph();
    juce::AudioBuffer<float> tone(2, 96000); for (int i = 0; i < 96000; ++i) { const float v = 0.3f * (float) std::sin(2 * juce::MathConstants<double>::pi * 220 * i / 48000.0) * (float) std::exp(-i / 30000.0); tone.setSample(0, i, v); tone.setSample(1, i, v); }
    p.arrange.receive(arr::makePayload(tone, 48000, "Lab Canvas"), 0, 0); p.arrange.receive(arr::makePayload(tone, 48000, "Ghost set"), 1, 4);
    p.arcsPage.edit() = arcs::factory::pages()[2].make(); p.arcsPage.commit();
    auto click = [&](const char* id) { if (auto* b = dynamic_cast<juce::Button*>(findRec(ed, id))) b->triggerClick(); pump(120); };
    auto save = [&](const juce::String& name) { waitFor([&] { return !L.busy.load(); }, 5000); pump(250); auto img = snapshotOf(*ed); auto f = dir.getChildFile(name); f.deleteFile(); juce::FileOutputStream os(f); juce::PNGImageFormat png; png.writeImageToStream(img, os); std::cout << "  shot " << name << std::endl; };
    static const char* pages[] = { "paint", "image", "ghost", "split", "chaos", "dna" };
    for (int dark = 1; dark >= 0; --dark) for (float sc : { 1.0f, 1.25f, 2.0f }) {
        ed->setTheme(dark == 1); ed->setSize(juce::roundToInt(1200 * sc), juce::roundToInt((790 + SpecEditor::kBarH) * sc)); pump(60);
        const juce::String suf = juce::String(dark ? "dark" : "light") + "-" + juce::String(juce::roundToInt(sc * 100)) + ".png";
        click("tab_pads"); save("lab-fix-pads-" + suf);
        if (sc == 1.0f) { click("tab_loop"); save("lab-fix-loop-" + suf); }
        click("labButton"); auto* panel = dynamic_cast<LabPanel*>(findRec(ed, "spectralLab"));
        for (int pg = 0; pg < 6; ++pg) { if (sc != 1.0f && pg != 1 && pg != 5 && pg != 2) continue; if (pg == 2) { L.ghost.count = 8; L.requestGhost(); } panel->showPage(pg); save("lab-fix-" + juce::String(pages[pg]) + "-" + suf); }
        if (sc == 1.0f) { click("tab_arrange"); save("lab-fix-arrange-" + suf); click("tab_arcs"); save("lab-fix-arcs-" + suf); if (findRec(ed, "tab_process")) { click("tab_process"); save("process-" + suf); } if (findRec(ed, "tab_voice")) { click("tab_voice"); save("voice-tab-" + suf); } }
    }
    ed->setTheme(true); edp.reset();
}

// ---------------------------------------------------------------- PLAY: ALWAYS | ON NOTES (ROADMAP 16)
struct FakeHead : juce::AudioPlayHead {
    double ppq = 0, bpm = 120; bool playing = true; juce::int64 t = 0;
    juce::Optional<PositionInfo> getPosition() const override { PositionInfo p; p.setBpm(bpm); p.setPpqPosition(ppq); p.setIsPlaying(playing); p.setTimeInSamples(t); return p; }
    void advance(int n) { if (playing) { ppq += n / 48000.0 * bpm / 60.0; t += n; } }
};
struct GateRig {
    SpecProcessor p; FakeHead head; std::vector<float> out;
    explicit GateRig(bool onNotes) {
        if (auto* pm = p.apvts.getParameter("play_mode")) pm->setValueNotifyingHost(onNotes ? 1.0f : 0.0f);   // as a project load would: the mode is known before playback
        p.prepareToPlay(48000, 512); p.setPlayHead(&head);
        juce::AudioBuffer<float> tone(2, 48000 * 8); for (int i = 0; i < tone.getNumSamples(); ++i) { const float v = 0.25f * (float) std::sin(2 * juce::MathConstants<double>::pi * 220 * i / 48000.0); tone.setSample(0, i, v); tone.setSample(1, i, v); }
        p.arrange.receive(arr::makePayload(tone, 48000, "tone"), 0, 0); p.arrange.waitUntilUpToDate();
        p.arcsPage.edit() = arcs::factory::pages()[1].make(); p.arcsPage.commit();
        auto& L = p.lab; L.open = true; L.playKeys = false; L.canvas.resize(framesFor(2.0)); Brush b; b.size = 1; for (int t = 0; t < L.canvas.T; ++t) paintAt(L.canvas, (float) t, 0, b, 440.0);
        const int v0 = L.bankVersion.load(); L.edited(); L.requestPreview(); waitFor([&] { return L.bankVersion.load() > v0 && !L.busy.load(); });
    }
    // events: (sample within the whole run, note, on)
    void run(int blocks, std::vector<std::tuple<int, int, bool>> ev = {}, std::function<void(int)> before = {}) {
        for (int k = 0; k < blocks; ++k) {
            if (before) before(k);
            const int base = (int) out.size(); juce::AudioBuffer<float> buf(2, 512); buf.clear(); juce::MidiBuffer m;
            for (auto& [at, nn, on] : ev) if (at >= base && at < base + 512) m.addEvent(on ? juce::MidiMessage::noteOn(1, nn, (juce::uint8) 100) : juce::MidiMessage::noteOff(1, nn), at - base);
            p.processBlock(buf, m); out.insert(out.end(), buf.getReadPointer(0), buf.getReadPointer(0) + 512); head.advance(512);
        }
    }
};
static int firstNonZero(const std::vector<float>& x, int from = 0) { for (int i = from; i < (int) x.size(); ++i) if (x[(size_t) i] != 0.0f) return i; return -1; }
static bool allZero(const std::vector<float>& x, int a, int b) { for (int i = a; i < b && i < (int) x.size(); ++i) if (x[(size_t) i] != 0.0f) return false; return true; }
static void playModeTests() {
    std::cout << "PLAY: ALWAYS | ON NOTES" << std::endl;
    { auto ap = std::make_unique<GateRig>(false), bp = std::make_unique<GateRig>(false); auto& a = *ap; auto& b = *bp; b.p.apvts.getParameter("play_mode")->setValueNotifyingHost(0.0f); a.run(60); b.run(60);
      check(a.out == b.out && peakOf(a.out) > 0.05f, "ALWAYS: today's behaviour, bit-identical (Lab host loop + ARRANGE + ARCS playing)", "peak " + juce::String(peakOf(a.out), 2)); }
    auto refp = std::make_unique<GateRig>(false); auto& ref = *refp; ref.run(120);   // what ALWAYS plays at every song position
    { auto gp = std::make_unique<GateRig>(true); auto& g = *gp; g.run(40); check(allZero(g.out, 0, (int) g.out.size()), "ON NOTES, no note held: exact digital silence while the song plays"); }
    { auto gp = std::make_unique<GateRig>(false); auto& g = *gp; g.run(40, {}, [&](int k) { if (k == 20) g.p.apvts.getParameter("play_mode")->setValueNotifyingHost(1.0f); });
      check(!allZero(g.out, 19 * 512, 20 * 512) && allZero(g.out, 20 * 512 + 300, 40 * 512), "switching to ON NOTES mid-song with no key held fades out (~6 ms) and stays silent"); }
    { auto gp = std::make_unique<GateRig>(true); auto& g = *gp; const int on = 10 * 512 + 300, off = 70 * 512 + 100;
      g.run(120, { { on, 60, true }, { off, 60, false } });
      const int first = firstNonZero(g.out); double maxErr = 0; for (int i = on; i < off; ++i) maxErr = std::max(maxErr, (double) std::abs(g.out[(size_t) i] - ref.out[(size_t) i]));
      check(first == on && maxErr < 1e-4, "ON NOTES: a held note starts them at its own sample, in time with the song (same audio as ALWAYS there)", "first sound at sample " + juce::String(first) + " (note at " + juce::String(on) + "), largest difference from ALWAYS " + juce::String(maxErr, 7));
      float jumpMusic = 0, jumpFade = 0; for (int i = off - 2000; i < off; ++i) jumpMusic = std::max(jumpMusic, std::abs(g.out[(size_t) i + 1] - g.out[(size_t) i]));
      for (int i = off; i < off + 400; ++i) jumpFade = std::max(jumpFade, std::abs(g.out[(size_t) i + 1] - g.out[(size_t) i]));
      const int fadeEnd = off + (int) (0.0062 * 48000);
      check(allZero(g.out, fadeEnd, (int) g.out.size()) && !allZero(g.out, off, off + 100) && jumpFade <= jumpMusic * 1.05f, "ON NOTES: note-off fades out in ~6 ms (no click), then exact silence",
            "silent from sample " + juce::String(firstNonZero(std::vector<float>(g.out.rbegin(), g.out.rend())) >= 0 ? (int) g.out.size() - firstNonZero(std::vector<float>(g.out.rbegin(), g.out.rend())) : -1) + " (note-off at " + juce::String(off) + "), largest step " + juce::String(jumpFade, 4) + " vs music " + juce::String(jumpMusic, 4)); }
    { auto gp = std::make_unique<GateRig>(true); auto& g = *gp; g.run(100, { { 5 * 512, 60, true }, { 20 * 512, 64, true }, { 40 * 512, 60, false }, { 70 * 512, 64, false } });
      check(!allZero(g.out, 40 * 512, 69 * 512) && allZero(g.out, 70 * 512 + 400, 100 * 512), "ON NOTES: overlapping notes keep it playing until the last key lets go"); }
    { auto gp = std::make_unique<GateRig>(true); auto& g = *gp; g.run(100, { { 5 * 512, 60, true }, { 80 * 512, 60, true } }, [&](int k) { g.head.playing = !(k >= 30 && k < 50); });
      check(!allZero(g.out, 6 * 512, 29 * 512) && allZero(g.out, 31 * 512, 80 * 512) && !allZero(g.out, 80 * 512, 100 * 512), "ON NOTES: a host stop with a key held forgets it (no stuck gate); the next note starts it again"); }
    { auto gp = std::make_unique<GateRig>(true); auto& g = *gp; g.run(80, { { 5 * 512, 60, true } }, [&](int k) { if (k == 30) g.p.bypassParam->setValueNotifyingHost(1.0f); if (k == 50) g.p.bypassParam->setValueNotifyingHost(0.0f); });
      check(!allZero(g.out, 6 * 512, 29 * 512) && allZero(g.out, 31 * 512, 80 * 512), "ON NOTES: BYPASS wins (silent), and after it the gate waits for a new note"); }
    { auto ap2 = std::make_unique<SpecProcessor>(); auto& a = *ap2; a.apvts.getParameter("play_mode")->setValueNotifyingHost(1.0f); juce::MemoryBlock st; a.getStateInformation(st);
      auto bp2 = std::make_unique<SpecProcessor>(); auto& b = *bp2; b.setStateInformation(st.getData(), (int) st.getSize()); const bool kept = b.playModeP->load() > 0.5f;
      auto old = a.apvts.copyState(); for (int i = old.getNumChildren(); --i >= 0;) if (old.getChild(i).getProperty("id").toString() == "play_mode") old.removeChild(i, nullptr);
      juce::MemoryBlock ob; { auto x = old.createXml(); juce::AudioProcessor::copyXmlToBinary(*x, ob); }
      auto cp2 = std::make_unique<SpecProcessor>(); auto& c = *cp2; c.apvts.getParameter("play_mode")->setValueNotifyingHost(1.0f); c.setStateInformation(ob.getData(), (int) ob.getSize());
      auto params = a.getParameters();
      check(kept && c.playModeP->load() < 0.5f && params.getLast()->getName(32) == "Play", "PLAY is saved with the project; an older project loads ALWAYS; it is the last parameter (after BYPASS)"); }
}

// ---------------------------------------------------------------- PROCESS page (the shared PROCESS LAB on the Lab's sound)
static void processPageTests() {
#if GHOSTY_PROCESS_LAB
    std::cout << "PROCESS (events under Xvfb)" << std::endl;
    SpecProcessor p; p.prepareToPlay(48000, 512); auto& L = p.lab;
    std::vector<float> x((size_t) (SR * 1.0)); for (size_t i = 0; i < x.size(); ++i) x[i] = (float) ((0.3 + 0.4 * i / x.size()) * std::sin(2 * juce::MathConstants<double>::pi * 330 * i / SR));   // a rising tone: reverse is audible
    analyse(x, L.canvas); L.sourceAudio = x; L.sourceName = "rise"; L.edited(); L.requestPreview();
    std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor()); ed->setVisible(true); pump(120);
    if (auto* b = dynamic_cast<juce::Button*>(findRec(ed.get(), "tab_process"))) b->triggerClick(); pump(200);
    auto* page = dynamic_cast<LabProcessPage*>(findRec(ed.get(), "processPage")); if (!page) { check(false, "PROCESS: page found"); return; }
    auto& lab = page->panel().lab();
    { bool vis = page->panel().isVisible() && page->panel().getWidth() > 600 && page->panel().getHeight() > 400; bool themed = true;
      for (int dark = 1; dark >= 0; --dark) { dynamic_cast<SpecEditor*>(ed.get())->setTheme(dark == 1); pump(60); auto img = ed->createComponentSnapshot(ed->getLocalBounds());
          const float b = img.getPixelAt(300, SpecEditor::kBarH + 700).getBrightness(); themed = themed && (dark ? b < 0.4f : b > 0.6f); }
      dynamic_cast<SpecEditor*>(ed.get())->setTheme(true); pump(40);
      check(vis && themed, "PROCESS: the panel is shown and follows Spectrum's DARK / LIGHT (its palette hook)"); }
    check(lab.audio.channels() == 1 && lab.audio.rate == SR && lab.audio.ch[0] == x, "PROCESS: the panel gets the Lab's sound, bit-exact (48 kHz mono)");
    lab.chain.ops = { plab::Op(plab::OpType::Reverse), plab::Op(plab::OpType::Gain) }; lab.chain.ops[1].p[0] = -6.0f; lab.chainChanged();
    lab.setRegion(plab::Region::all(lab.audio)); lab.apply();
    waitFor([&] { return !lab.busy() && L.sourceAudio != x; }, 20000); pump(100);
    const auto& y = L.sourceAudio; const float g = std::pow(10.0f, -6.0f / 20.0f); double err = 0;
    for (size_t i = 0; i < y.size() && i < x.size(); ++i) err = std::max(err, (double) std::abs(y[i] - x[x.size() - 1 - i] * g));
    check(y.size() == x.size() && err < 1e-5 && std::abs(peakOf(y) / peakOf(x) - g) < 1e-3, "PROCESS: REVERSE + GAIN -6 dB -> the Lab's sound is the reversed tone at half level", "length " + juce::String((int) y.size()) + ", max error " + juce::String(err, 8) + ", level " + juce::String(peakOf(y) / peakOf(x), 4));
    check(snrDb(y, render(L.canvas, Quality::HQ)) > 100, "PROCESS: the Lab plays the processed sound (canvas = the result)", juce::String(snrDb(y, render(L.canvas, Quality::HQ)), 1) + " dB");
    std::shared_ptr<LabHost::ExportSound> got; L.renderExport(120, [&](std::shared_ptr<LabHost::ExportSound> e) { got = e; }); waitFor([&] { return got != nullptr; }, 10000);
    const auto pl = got ? SpecProcessor::toPayload(*got) : nullptr; const juce::uint32 clip = pl ? p.arrange.receive(pl) : 0;
    double c = 0; if (pl) { std::vector<float> a(pl->audio.getReadPointer(0), pl->audio.getReadPointer(0) + pl->audio.getNumSamples()); c = corr(a, y); }
    check(clip != 0 && pl && std::abs(pl->seconds() - y.size() / SR) < 0.01 && c > 0.99, "PROCESS: SEND TO ARRANGE takes the processed sound", "corr " + juce::String(c, 4));
    lab.undoStep(); pump(100);
    check(L.sourceAudio == x, "PROCESS: UNDO restores the Lab's sound bit-exact");
    juce::TextButton* bn = nullptr; std::function<void(juce::Component&)> find = [&](juce::Component& cc) { if (auto* b = dynamic_cast<juce::TextButton*>(&cc)) if (b->getButtonText() == "BOUNCE NEW") bn = b; for (auto* ch : cc.getChildren()) find(*ch); };
    find(page->panel()); if (bn) { if (!lab.previewValid()) { lab.preview(); waitFor([&] { return lab.previewValid() && !lab.busy(); }, 20000); } bn->triggerClick(); waitFor([&] { return L.kept.size() == 1; }, 20000); pump(100); }   // BOUNCE NEW takes the preview
    check(bn != nullptr && L.kept.size() == 1 && L.kept[0].audio == x && L.sourceName.endsWith("(processed)") && L.sourceAudio.size() == x.size(), "PROCESS: BOUNCE NEW makes the result the Lab's sound and keeps the original");
    juce::MemoryBlock st; p.getStateInformation(st); SpecProcessor q; q.setStateInformation(st.getData(), (int) st.getSize());
    const auto a = p.plabState(), b2 = q.plabState();
    check(a.isValid() && a.toXmlString() == b2.toXmlString() && a.toXmlString().contains("reverse") && st.getSize() < (size_t) 2 << 20, "PROCESS: the chain round-trips with the project (audio only through the Lab's source)", juce::String((int) (st.getSize() / 1024)) + " KB state");
    ed.reset(); pump(50);
#endif
}

// ---------------------------------------------------------------- VOICE tab
#if GHOSTY_VOICE
static std::vector<float> synthVoice(double f0, double secs) {   // glottal-ish pulses through three formant resonances (a sung "ah")
    const int n = (int) (secs * SR); std::vector<float> x((size_t) n, 0.0f); double ph = 0;
    struct Res { double f, bw, a; double y1 = 0, y2 = 0; } R3[] = { { 730, 90, 1.0 }, { 1090, 110, 0.6 }, { 2440, 160, 0.3 } };
    for (int i = 0; i < n; ++i) {
        ph += f0 * (1.0 + 0.002 * std::sin(i / SR * 2 * juce::MathConstants<double>::pi * 5)) / SR; double src = 0; if (ph >= 1.0) { ph -= 1.0; src = 1.0; }
        double y = 0; for (auto& r : R3) { const double w = 2 * juce::MathConstants<double>::pi * r.f / SR, rr = std::exp(-juce::MathConstants<double>::pi * r.bw / SR);
            const double v = src * r.a + 2 * rr * std::cos(w) * r.y1 - rr * rr * r.y2; r.y2 = r.y1; r.y1 = v; y += v; }
        x[(size_t) i] = (float) (0.05 * y * std::min(1.0, i / (0.02 * SR)) * std::min(1.0, (n - i) / (0.02 * SR)));
    }
    return x;
}
static double f0Of(const std::vector<float>& x, int fs) {   // median of per-frame autocorrelation peaks (parabolic), middle half
    std::vector<double> est; const int W = fs / 20, lo = fs / 500, hi = fs / 70;
    for (int s0 = (int) x.size() / 4; s0 + W + hi < (int) x.size() * 3 / 4; s0 += W / 2) {
        int best = lo; double bv = -1e30; std::vector<double> ac((size_t) hi + 2, 0.0);
        for (int lag = lo; lag <= hi + 1; ++lag) { double a = 0; for (int i = 0; i < W; ++i) a += (double) x[(size_t) (s0 + i)] * x[(size_t) (s0 + i + lag)]; ac[(size_t) lag] = a; if (lag <= hi && a > bv) { bv = a; best = lag; } }
        const double y0 = ac[(size_t) best - 1], y1 = ac[(size_t) best], y2 = ac[(size_t) best + 1]; const double off = 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2);
        est.push_back(fs / (best + (std::isfinite(off) ? off : 0.0)));
    }
    if (est.empty()) return 0; std::sort(est.begin(), est.end()); return est[est.size() / 2];
}
#endif
static void voiceTabTests() {
#if GHOSTY_VOICE
    std::cout << "VOICE (events under Xvfb)" << std::endl;
    auto pp = std::make_unique<SpecProcessor>(); auto& p = *pp; p.prepareToPlay(48000, 512);
    std::unique_ptr<juce::AudioProcessorEditor> edp(p.createEditor()); auto* ed = dynamic_cast<SpecEditor*>(edp.get()); ed->setVisible(true); pump(150);
    if (auto* b = dynamic_cast<juce::Button*>(findRec(ed, "tab_voice"))) b->triggerClick(); pump(300);
    auto* tab = dynamic_cast<SpecVoiceTab*>(findRec(ed, "voiceTab")); if (!tab) { check(false, "VOICE: tab found"); return; }
    bool themes = true; for (int dark = 1; dark >= 0; --dark) { ed->setTheme(dark == 1); pump(80); auto img = ed->createComponentSnapshot(ed->getLocalBounds());
        const float bright = img.getPixelAt(600, SpecEditor::kBarH + 400).getBrightness(); themes = themes && tab->page().isDark() == (dark == 1) && img.isValid() && (dark ? bright < 0.5f : bright > 0.5f); }
    check(themes, "VOICE: the tab renders and follows Spectrum's DARK / LIGHT toggle");
    ed->setTheme(true); pump(40);
    const auto x = synthVoice(150.0, 1.5); juce::String err;
    const bool loaded = p.voice.setAudio(0, x, 48000, "ah", false, &err); p.voice.waitIdle(60000);
    auto prm = p.voice.params(); prm.mode = voice::RenderParams::Pitch; prm.pitchSemis = 5; prm.keepFormants = true; p.voice.setParams(prm);
    const bool rendered = p.voice.renderNow(err); auto res = p.voice.result();
    const double fIn = f0Of(x, 48000), fOut = res ? f0Of(res->audio.L, res->audio.fs) : 0, want = fIn * std::exp2(5.0 / 12.0), cents = 1200 * std::log2(fOut / want);
    check(loaded && rendered && std::abs(cents) < 5.0, "VOICE: a synthetic voice up 5 semitones with formants kept -> f0 within 5 cents", juce::String(fIn, 2) + " Hz -> " + juce::String(fOut, 2) + " Hz (want " + juce::String(want, 2) + ", " + juce::String(cents, 2) + " cents)" + (err.isNotEmpty() ? "  " + err : juce::String()));
    tab->sendToArrange(); const arr::Clip* c = nullptr; for (auto& t : p.arrange.arrangement.tracks) for (auto& cl : t.clips) if (cl.source && cl.source->source.page == "VOICE") c = &cl;
    auto again = c ? p.arrange.rerender(c->source->source) : nullptr; double cr = 0;
    if (c && again) { std::vector<float> a(c->source->audio.getReadPointer(0), c->source->audio.getReadPointer(0) + c->source->audio.getNumSamples()), b(again->audio.getReadPointer(0), again->audio.getReadPointer(0) + again->audio.getNumSamples()); cr = corr(a, b); }
    check(c != nullptr && std::abs(c->source->seconds() - res->seconds()) < 0.01 && again != nullptr && cr > 0.99, "VOICE: SEND TO ARRANGE puts the result on the timeline; ARRANGE can re-render it", "re-render corr " + juce::String(cr, 4));
    { juce::MemoryBlock st; p.getStateInformation(st); auto q = std::make_unique<SpecProcessor>(); q->setStateInformation(st.getData(), (int) st.getSize()); q->voice.waitIdle(60000);
      auto s0 = q->voice.source(0); check(s0 && s0->pcm && std::abs(s0->seconds() - 1.5) < 0.01 && std::abs(q->voice.params().pitchSemis - 5.0f) < 1e-4, "VOICE: the voice and its settings round-trip with the project");
      auto old = p.apvts.copyState(); old.appendChild(p.toTree(), nullptr); juce::MemoryBlock ob; { auto xm = old.createXml(); juce::AudioProcessor::copyXmlToBinary(*xm, ob); }
      q->setStateInformation(ob.getData(), (int) ob.getSize()); q->voice.waitIdle(60000); check(q->voice.source(0) == nullptr, "VOICE: an older project loads with an empty VOICE page"); }
    { p.voice.play(voice::PlayResult); std::vector<float> a, b; for (int k = 0; k < 30; ++k) { juce::AudioBuffer<float> buf(2, 512); buf.clear(); juce::MidiBuffer m; p.processBlock(buf, m); a.insert(a.end(), buf.getReadPointer(0), buf.getReadPointer(0) + 512); }
      p.bypassParam->setValueNotifyingHost(1.0f); for (int k = 0; k < 30; ++k) { juce::AudioBuffer<float> buf(2, 512); buf.clear(); juce::MidiBuffer m; p.processBlock(buf, m); b.insert(b.end(), buf.getReadPointer(0), buf.getReadPointer(0) + 512); }
      float after = 0; for (size_t i = 960; i < b.size(); ++i) after = std::max(after, std::abs(b[i]));
      check(peakOf(a) > 0.01f && after == 0.0f, "VOICE: BYPASS mutes the VOICE preview (digital silence within 20 ms)", "preview peak " + juce::String(peakOf(a), 3));
      p.bypassParam->setValueNotifyingHost(0.0f); p.apvts.getParameter("play_mode")->setValueNotifyingHost(1.0f); p.voice.play(voice::PlayResult);
      std::vector<float> c2; for (int k = 0; k < 30; ++k) { juce::AudioBuffer<float> buf(2, 512); buf.clear(); juce::MidiBuffer m; p.processBlock(buf, m); c2.insert(c2.end(), buf.getReadPointer(0), buf.getReadPointer(0) + 512); }
      float late = 0; for (size_t i = 960; i < c2.size(); ++i) late = std::max(late, std::abs(c2[i]));
      check(late == 0.0f, "VOICE: PLAY ON NOTES gates the VOICE preview too (no note -> silence)"); p.apvts.getParameter("play_mode")->setValueNotifyingHost(0.0f); p.voice.stop(); }
    edp.reset(); pump(50);
#endif
}

// PLAY: ON NOTES also covers the PAINT LOOP sequencer and a HOLD pad (they play by themselves); a pad played by a held key keeps its own release
struct PadRig {
    SpecProcessor p; FakeHead head; std::vector<float> out;
    PadRig(bool onNotes, bool loop, bool pad, bool hold) {
        if (auto* pm = p.apvts.getParameter("play_mode")) pm->setValueNotifyingHost(onNotes ? 1.0f : 0.0f);
        if (auto* ph = p.apvts.getParameter("padHold")) ph->setValueNotifyingHost(hold ? 1.0f : 0.0f);
        p.prepareToPlay(48000, 512); p.setPlayHead(&head);
        if (loop) { p.addLayer(0, "lead", "soft bright pluck", 3); for (int k = 0; k < 8; ++k) { sp::Note nt; nt.layer = 0; nt.start = k * 0.5; nt.len = 0.4; nt.pitch = 60 + (k % 4) * 3; p.model.notes.push_back(nt); } p.publish(); }
        if (pad) { sp::PadDot d; d.pitch = 64; d.voice = 2; p.model.pads.push_back(d); d.pitch = 71; p.model.pads.push_back(d); p.syncPads(); }
    }
    void run(int blocks, std::vector<std::tuple<int, int, bool>> ev = {}) {
        for (int k = 0; k < blocks; ++k) { const int base = (int) out.size(); juce::AudioBuffer<float> buf(2, 512); buf.clear(); juce::MidiBuffer m;
            for (auto& [at, nn, on] : ev) if (at >= base && at < base + 512) m.addEvent(on ? juce::MidiMessage::noteOn(1, nn, (juce::uint8) 100) : juce::MidiMessage::noteOff(1, nn), at - base);
            p.processBlock(buf, m); out.insert(out.end(), buf.getReadPointer(0), buf.getReadPointer(0) + 512); head.advance(512); }
    }
};
static void playModePadTests() {
    std::cout << "PLAY: ON NOTES - PAINT LOOP + pad" << std::endl;
    { auto a = std::make_unique<PadRig>(false, true, true, true); a->run(80); auto b = std::make_unique<PadRig>(true, true, true, true); b->run(80);
      check(peakOf(a->out) > 0.02f && allZero(b->out, 0, (int) b->out.size()), "ON NOTES: the PAINT LOOP and a HOLD pad are silent without a note (ALWAYS plays them)", "ALWAYS peak " + juce::String(peakOf(a->out), 3)); }
    { auto g = std::make_unique<PadRig>(true, true, false, false); const int on = 20 * 512 + 100, off = 120 * 512;
      g->run(600, { { on, 60, true }, { off, 60, false } });   // 600 blocks: the 4 s window after note-off fits
      auto peakIn = [&](int a, int b) { float m = 0; for (int i = a; i < b; ++i) m = std::max(m, std::abs(g->out[(size_t) i])); return m; };
      const float music = peakIn(on, off), t2 = peakIn(off + 96000, off + 120000), t4 = peakIn(off + 192000, off + 216000); const float tail = t2;
      check(firstNonZero(g->out) >= on && music > 0.02f && t2 < music * 0.01f && t4 < t2 * 0.3f, "ON NOTES: a held note plays the PAINT LOOP, note-off stops it (only the reverb tail rings out)",
            "first sound " + juce::String(firstNonZero(g->out)) + " (note " + juce::String(on) + "), reverb tail 2 s after note-off " + juce::String(20 * std::log10(tail / music), 1) + " dB below the music, 4 s " + juce::String(20 * std::log10(std::max(1e-9f, t4) / music), 1) + " dB"); }
    { auto a = std::make_unique<PadRig>(false, false, true, false), b = std::make_unique<PadRig>(true, false, true, false); const int on = 10 * 512, off = 60 * 512;
      a->run(150, { { on, 64, true }, { off, 64, false } }); b->run(150, { { on, 64, true }, { off, 64, false } });
      check(a->out == b->out && peakOf(std::vector<float>(b->out.begin() + off, b->out.begin() + off + 4800)) > 1e-3f, "ON NOTES: a pad played by a held key (HOLD off) is untouched - its release still rings out exactly as in ALWAYS"); }
}

// ---------------------------------------------------------------- SYNC: DAW | OWN
static void setSync(SpecProcessor& p, bool own) { p.sync.setMode(own ? ghosty::SyncTransport::Mode::Own : ghosty::SyncTransport::Mode::Daw); }
static void addLoop(SpecProcessor& p) { p.addLayer(0, "lead", "soft bright pluck", 3); for (int k = 0; k < 8; ++k) { sp::Note nt; nt.layer = 0; nt.start = k * 0.5; nt.len = 0.4; nt.pitch = 60 + (k % 4) * 3; p.model.notes.push_back(nt); } p.publish(); }
static float maxStep(const std::vector<float>& x, int a, int b) { float m = 0; for (int i = juce::jmax(0, a); i + 1 < juce::jmin(b, (int) x.size()); ++i) m = std::max(m, std::abs(x[(size_t) i + 1] - x[(size_t) i])); return m; }
struct SyncRig : GateRig {   // the Lab host loop + ARRANGE + ARCS (GateRig) + the PAINT LOOP
    explicit SyncRig(bool own, bool loopOnly = false) : GateRig(false) {
        if (loopOnly) { p.arrange.setState(p.defaultArrange.getData(), p.defaultArrange.getSize()); p.arrange.waitUntilUpToDate(); p.arcsPage.setState(p.defaultArcs); p.lab.open = false; }
        addLoop(p); setSync(p, own);
    }
};
static void syncTests() {
    std::cout << "SYNC: DAW | OWN" << std::endl;
    { auto a = std::make_unique<SyncRig>(false), b = std::make_unique<SyncRig>(false);
      juce::MemoryBlock st; a->p.getStateInformation(st); { auto x = juce::AudioProcessor::getXmlFromBinary(st.getData(), (int) st.getSize()); jassert(x); }
      setSync(b->p, true); setSync(b->p, false);   // touched and set back before playback
      a->run(80); b->run(80);
      check(a->out == b->out && peakOf(a->out) > 0.05f && !a->p.ownClock(), "DAW (default): today's behaviour, bit-identical (PAINT LOOP + Lab host loop + ARRANGE + ARCS follow the host)", "peak " + juce::String(peakOf(a->out), 2)); }
    { auto a = std::make_unique<SyncRig>(true), b = std::make_unique<SyncRig>(true);
      a->head.bpm = 90; a->head.ppq = 37.25; a->head.playing = true; b->head.bpm = 140; b->head.ppq = 3; b->head.playing = false;
      a->p.toggleOwnPlay(); b->p.toggleOwnPlay();
      a->run(40, {}, [&](int k) { if (k == 20) { a->head.ppq = 2.0; a->head.bpm = 171; } }); b->run(40, {}, [&](int k) { if (k == 12) b->head.playing = true; });
      check(a->out == b->out && peakOf(a->out) > 0.05f, "OWN: the output ignores the DAW's transport (two different host transports, locate + tempo jump -> identical audio)", "peak " + juce::String(peakOf(a->out), 2)); }
    { auto o = std::make_unique<SyncRig>(true, true), d = std::make_unique<SyncRig>(false, true); o->p.sync.setOwnBpm(120); d->head.bpm = 120; d->head.ppq = 0; d->head.playing = false;
      o->head.playing = true; o->head.ppq = 13.7; o->head.bpm = 95;   // the DAW plays something else: OWN doesn't start with it
      o->run(5); const bool quiet = allZero(o->out, 0, (int) o->out.size()); d->run(5);
      o->p.toggleOwnPlay(); d->head.playing = true; o->run(60); d->run(60);
      double err = 0; for (int i = 5 * 512; i < 65 * 512; ++i) err = std::max(err, (double) std::abs(o->out[(size_t) i] - d->out[(size_t) i]));
      check(quiet && firstNonZero(o->out) >= 5 * 512 && firstNonZero(o->out) < 5 * 512 + 64 && err < 1e-6 && peakOf(o->out) > 0.02f, "OWN: the DAW's play doesn't start it; own PLAY starts on the grid at bar 1 (same audio as a DAW starting at ppq 0)",
            "first sound " + juce::String(firstNonZero(o->out)) + " (PLAY at " + juce::String(5 * 512) + "), largest difference " + juce::String(err, 8)); }
    { auto o = std::make_unique<SyncRig>(true, true); o->p.toggleOwnPlay(); o->run(120, {}, [&](int k) { if (k == 60) o->p.sync.setOwnBpm(97); });
      const int at = 60 * 512; const float music = maxStep(o->out, 0, at), sw = maxStep(o->out, at - 64, at + 2048);
      // the next notes come at the new tempo: 4 beats at 97 bpm after the change
      check(sw <= music * 1.05f && o->p.tempo() == 97.0, "OWN: a tempo change applies from the next block with no click (the clock keeps its position)", "largest step at the change " + juce::String(sw, 4) + " vs music " + juce::String(music, 4)); }
    for (int dir = 0; dir < 2; ++dir) {   // 0: DAW -> OWN (own stopped), 1: OWN (playing) -> DAW (host playing)
        auto r = std::make_unique<SyncRig>(dir == 1); if (dir == 1) r->p.toggleOwnPlay();
        const int swb = 50; r->run(110, {}, [&](int k) { if (k == swb) setSync(r->p, dir == 0); });
        const int at = swb * 512; const float music = std::max(maxStep(r->out, 0, at), maxStep(r->out, at + 4000, 110 * 512)), sw = maxStep(r->out, at - 64, at + 1200);   // the switch: fade out, swap at the next block, fade in
        const bool own = r->p.ownClock();
        check(sw <= music * 1.05f && own == (dir == 0) && (dir == 1 || !r->p.sync.isOwnPlaying()), dir == 0 ? "switching DAW -> OWN mid-song: a ~6 ms fade, no click; OWN waits for its own PLAY" : "switching OWN -> DAW mid-play: a ~6 ms fade, then the DAW's song position (the own clock stops)",
              "largest step " + juce::String(sw, 4) + " vs music " + juce::String(music, 4) + [&] { int w = 0; float m = 0; for (int i = at - 64; i < at + 1200; ++i) { const float d = std::abs(r->out[(size_t) i + 1] - r->out[(size_t) i]); if (d > m) { m = d; w = i - at; } } return juce::String(" at +") + juce::String(w) + " v=" + juce::String(r->out[(size_t) (at + w)], 3) + "->" + juce::String(r->out[(size_t) (at + w + 1)], 3); }()); }
    { auto r = std::make_unique<SyncRig>(true); r->p.toggleOwnPlay(); r->run(20); r->p.bypassParam->setValueNotifyingHost(1.0f); r->run(20);
      const bool byp = allZero(r->out, 20 * 512 + 400, 40 * 512); r->p.bypassParam->setValueNotifyingHost(0.0f);
      auto g = std::make_unique<SyncRig>(true); g->p.apvts.getParameter("play_mode")->setValueNotifyingHost(1.0f); g->p.prepareToPlay(48000, 512); g->p.toggleOwnPlay(); g->run(30); const bool gated = allZero(g->out, 0, (int) g->out.size());
      g->run(40, { { 30 * 512 + 100, 60, true } }); const int first = firstNonZero(g->out);
      check(byp && gated && first == 30 * 512 + 100, "OWN: BYPASS wins; PLAY: ON NOTES gates the own clock's players too (a held note opens it at its sample)", "first sound " + juce::String(first)); }
    { SpecProcessor a; setSync(a, true); a.sync.setOwnBpm(133.5); juce::MemoryBlock st; a.getStateInformation(st);
      SpecProcessor b; b.setStateInformation(st.getData(), (int) st.getSize());
      auto x = juce::AudioProcessor::getXmlFromBinary(st.getData(), (int) st.getSize()); auto t = juce::ValueTree::fromXml(*x);
      auto u = t.getChildWithName("SPECTRUMUI"); u.removeProperty("sync", nullptr); u.removeProperty("own_bpm", nullptr);
      if (auto pb = t.getChildWithProperty("id", "bpm"); pb.isValid()) pb.setProperty("value", 101.0, nullptr);
      juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary(*t.createXml(), old);
      SpecProcessor c; setSync(c, true); c.setStateInformation(old.getData(), (int) old.getSize());
      const bool presetFree = !a.toTree().toXmlString().contains("own_bpm") && !a.lab.toTree().toXmlString().contains("own_bpm");
      check(b.sync.getMode() == ghosty::SyncTransport::Mode::Own && b.sync.getOwnBpm() == 133.5 && c.sync.getMode() == ghosty::SyncTransport::Mode::Daw && c.sync.getOwnBpm() == 101.0 && presetFree,
            "SYNC mode + own tempo are saved with the project (not in patterns / Lab state); an older project loads SYNC: DAW (own tempo = its tempo)"); }
}
// SPACE and the header cluster with a real window: OWN plays / stops on SPACE (a focused text box keeps its SPACE); DAW leaves SPACE to the DAW
static void syncUiTests() {
    std::cout << "SYNC: UI" << std::endl;
    SpecProcessor p; FakeHead head; head.playing = false; p.prepareToPlay(48000, 512); p.setPlayHead(&head);
    auto block = [&] { juce::AudioBuffer<float> b(2, 512); juce::MidiBuffer m; p.processBlock(b, m); };
    block();
    std::unique_ptr<juce::AudioProcessorEditor> edp(p.createEditor()); auto* ed = dynamic_cast<SpecEditor*>(edp.get()); ed->setVisible(true); pump(150);
    auto* bar = findRec(ed, "syncButton"); auto vis = [&](const char* id) { auto* c = findRec(ed, id); return c != nullptr && c->isVisible(); };
    const bool dawUi = bar != nullptr && vis("dawTempo") && !vis("ownBpm") && !vis("ownPlay");
    const juce::KeyPress space(juce::KeyPress::spaceKey, {}, ' ');
    // JUCE's key routing (ComponentPeer::handleKeyPress): from the focused component up through its parents; at each one its key listeners
    // first, then its keyPressed. Spectrum's pages carry the SYNC listener (ids below); everything else is the real component code.
    auto press = [&](juce::Component* target) { bool used = false;
        for (auto* c = target; c != nullptr && !used; c = c->getParentComponent()) { if ((bool) c->getProperties().getWithDefault("ownSpace", false) && ed->ownSpaceKeys()->keyPressed(space, c)) used = true; else used = c->keyPressed(space); }
        block(); pump(20); return used; };
    const bool dawFree = !press(ed) && !p.sync.isOwnPlaying();
    if (auto* b = dynamic_cast<juce::Button*>(bar)) b->triggerClick(); pump(80); block(); block(); block(); pump(60);   // the switch fades over ~2 blocks
    const bool ownUi = p.ownClock() && vis("ownBpm") && vis("ownPlay") && vis("tapButton") && !vis("dawTempo");
    const bool s1 = press(ed) && p.sync.isOwnPlaying(); const bool s2 = press(ed) && !p.sync.isOwnPlaying();
    if (auto* b = dynamic_cast<juce::Button*>(findRec(ed, "tab_arrange"))) b->triggerClick(); pump(80);
    const bool sArr = press(findRec(ed, "arrangePage")) && p.sync.isOwnPlaying() && !p.arrange.isPlaying(); press(findRec(ed, "arrangePage"));
    if (auto* b = dynamic_cast<juce::Button*>(findRec(ed, "labButton"))) b->triggerClick(); else if (auto* t = dynamic_cast<juce::Button*>(findRec(ed, "tab_lab"))) t->triggerClick(); pump(120);
    juce::TextEditor* te = nullptr; std::function<void(juce::Component*)> findTe = [&](juce::Component* c) { for (auto* ch : c->getChildren()) { if (te) return; if (auto* x = dynamic_cast<juce::TextEditor*>(ch); x && x->isVisible()) { te = x; return; } findTe(ch); } };
    if (auto* lab = dynamic_cast<LabPanel*>(findRec(ed, "spectralLab"))) { lab->showPage(0); pump(60); findTe(lab); }
    bool textKeeps = false; if (te) { te->setText("ab", false); te->moveCaretToEnd(); const bool wasOwn = p.sync.isOwnPlaying(); press(te); textKeeps = te->getText() == "ab " && p.sync.isOwnPlaying() == wasOwn; }
    bool innerOk = true; juce::String innerWhy;   // pages with their own SPACE preview inside (VOICE, PROCESS): in OWN the one clock wins
    for (auto [tab, page] : { std::pair<const char*, const char*> { "tab_voice", "voiceTab" }, { "tab_process", "processPage" } }) {
        auto* tb = dynamic_cast<juce::Button*>(findRec(ed, tab)); if (!tb) continue; tb->triggerClick(); pump(150);
        auto* pg = findRec(ed, page); juce::Component* inner = pg && pg->getNumChildComponents() > 0 ? pg->getChildComponent(0) : pg;
        const bool was = p.sync.isOwnPlaying(); const bool used = press(inner); const bool now = p.sync.isOwnPlaying(); press(inner);
        if (!(used && now != was)) { innerOk = false; innerWhy << page << " "; }
    }
    auto* bpm = dynamic_cast<juce::Label*>(findRec(ed, "ownBpm")); if (bpm) { bpm->setText("150", juce::sendNotificationSync); } block();
    check(dawUi && dawFree, "SYNC: DAW shows the DAW's tempo read-only, no own PLAY, and leaves SPACE to the DAW");
    check(ownUi && s1 && s2, "SYNC: OWN shows the own BPM box + TAP + PLAY; SPACE plays / stops the own clock", juce::String((int) p.ownClock()) + juce::String((int) vis("ownBpm")) + juce::String((int) vis("ownPlay")) + juce::String((int) vis("tapButton")) + juce::String((int) vis("dawTempo")) + " s " + juce::String((int) s1) + juce::String((int) s2));
    check(sArr, "SYNC: OWN on ARRANGE: SPACE drives the one own clock (not a separate ARRANGE transport)");
    check(te != nullptr && textKeeps, "SYNC: OWN: a focused Lab text box keeps its SPACE (types a space, the clock doesn't toggle)", te ? "text [" + te->getText() + "]" : juce::String("no text box"));
    check(innerOk, "SYNC: OWN on VOICE / PROCESS: SPACE drives the own clock (their previews keep their PLAY buttons)", innerWhy);
    check(p.sync.getOwnBpm() == 150.0 && p.tempo() == 150.0, "SYNC: OWN: typing in the BPM box sets the own tempo");
    // ARRANGE's transport view: its PLAY / SPACE drive the own clock in OWN, its PLAY lights with the running clock, its tempo label is read-only
    { if (p.sync.isOwnPlaying()) { p.toggleOwnPlay(); block(); }
      if (auto* b = dynamic_cast<juce::Button*>(findRec(ed, "tab_arrange"))) b->triggerClick(); pump(120);
      auto* pg = dynamic_cast<arr::ArrangePage*>(findRec(ed, "arrangePage"));
      juce::TextButton* play = nullptr; juce::Label* tempo = nullptr;
      if (pg) for (auto* c : pg->getChildren()) { if (auto* tb = dynamic_cast<juce::TextButton*>(c); tb && tb->getButtonText() == "PLAY") play = tb; if (auto* l = dynamic_cast<juce::Label*>(c); l && l->getText().contains("BPM")) tempo = l; }
      auto settle = [&] { pump(60); block(); block(); pump(120); };   // triggerClick is asynchronous
      bool ownOk = false, ownKey = false, dawOk = false, dawPlay = false; juce::String why;
      if (pg && play && tempo) {
          play->triggerClick(); settle();
          const bool on1 = p.sync.isOwnPlaying() && !p.arrange.isPlaying() && play->getToggleState() && tempo->getText().startsWith("OWN 150") && !tempo->isEditableOnDoubleClick();
          play->triggerClick(); settle(); const bool off1 = !p.sync.isOwnPlaying() && !play->getToggleState() && !p.arrange.isPlaying();
          ownOk = on1 && off1; why << "own " << (int) on1 << (int) off1 << " [" << tempo->getText() << "] ";
          pg->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)); settle(); const bool k1 = p.sync.isOwnPlaying() && !p.arrange.isPlaying();
          pg->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)); settle(); ownKey = k1 && !p.sync.isOwnPlaying();
          setSync(p, false); head.bpm = 128.5; head.playing = true; for (int k = 0; k < 4; ++k) block(); pump(120);
          dawOk = !p.ownClock() && tempo->getText().startsWith("DAW 128.5") && play->getToggleState() && !tempo->isEditableOnDoubleClick(); why << "daw [" << tempo->getText() << "] lit " << (int) play->getToggleState();
          head.playing = false; for (int k = 0; k < 2; ++k) block(); pump(120);
          play->triggerClick(); settle(); dawPlay = p.arrange.isPlaying() && !p.sync.isOwnPlaying() && play->getToggleState();   // DAW, host stopped: Arrange's own preview as before
          play->triggerClick(); settle(); dawPlay = dawPlay && !p.arrange.isPlaying();
      } else why = "page/button/label not found";
      check(ownOk && ownKey, "SYNC: OWN on ARRANGE: its PLAY and SPACE start / stop the one own clock (Arrange's internal transport stays off); PLAY lights while it runs; label 'OWN 150 BPM', read-only", why);
      check(dawOk && dawPlay, "SYNC: DAW on ARRANGE: label 'DAW 128.5 BPM' read-only, PLAY lights while the DAW plays; with the DAW stopped PLAY is Arrange's own preview as before", why); }
    edp.reset(); pump(50);
}
static void syncShots(const juce::File& dir) {
    dir.createDirectory();
    SpecProcessor p; p.guideSeen = 0x7fffffff; FakeHead head; head.bpm = 128.5; head.playing = true; p.prepareToPlay(48000, 512); p.setPlayHead(&head);
    auto block = [&] { juce::AudioBuffer<float> b(2, 512); juce::MidiBuffer m; p.processBlock(b, m); head.advance(512); };
    block(); addLoop(p);
    { juce::AudioBuffer<float> tone(2, 96000); for (int i = 0; i < 96000; ++i) { const float v = 0.3f * (float) std::sin(2 * juce::MathConstants<double>::pi * 220 * i / 48000.0) * (float) std::exp(-i / 30000.0); tone.setSample(0, i, v); tone.setSample(1, i, v); }
      p.arrange.receive(arr::makePayload(tone, 48000, "Loop bounce"), 0, 0); p.arrange.receive(arr::makePayload(tone, 48000, "Ghost set"), 1, 4); p.arrange.waitUntilUpToDate(); }
    std::unique_ptr<juce::AudioProcessorEditor> edp(p.createEditor()); auto* ed = dynamic_cast<SpecEditor*>(edp.get()); ed->setVisible(true); pump(150);
    if (auto* b = dynamic_cast<juce::Button*>(findRec(ed, "tab_loop"))) b->triggerClick(); pump(100);
    for (int own = 0; own < 2; ++own) for (int dark = 1; dark >= 0; --dark) {
        setSync(p, own == 1); for (int k = 0; k < 3; ++k) block(); if (own && !p.sync.isOwnPlaying()) p.toggleOwnPlay(); for (int k = 0; k < 3; ++k) block(); ed->setTheme(dark == 1); pump(200);
        for (int arrPage = 0; arrPage < 2; ++arrPage) {
            if (auto* b = dynamic_cast<juce::Button*>(findRec(ed, arrPage ? "tab_arrange" : "tab_loop"))) b->triggerClick(); pump(200);
            auto img = snapshotOf(*ed); const juce::String name = juce::String(arrPage ? "sync-arrange-" : "sync-") + (own ? "own-" : "daw-") + (dark ? "dark" : "light") + ".png";
            auto f = dir.getChildFile(name); f.deleteFile(); juce::FileOutputStream os(f); juce::PNGImageFormat png; png.writeImageToStream(img, os); std::cout << "  shot " << name << std::endl;
        }
    }
    ed->setTheme(true); edp.reset(); pump(50);
}

// ---------------------------------------------------------------- UI tests (real mouse / wheel / key / drag events; need a display - run under Xvfb)
static void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }
static juce::MouseEvent mev(juce::Component& c, juce::Point<float> p, juce::ModifierKeys mods = {}, int clicks = 1, juce::Point<float> down = { -1.0e9f, -1.0e9f }) {
    auto src = juce::Desktop::getInstance().getMainMouseSource(); const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent(src, p, mods, juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation,
                            juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY, &c, &c, now, down.x < -1.0e8f ? p : down, now, clicks, false);
}
static void drag(juce::Component& c, juce::Point<float> a, juce::Point<float> b, juce::ModifierKeys mods = {}) {
    c.mouseDown(mev(c, a, mods)); for (int i = 1; i <= 6; ++i) c.mouseDrag(mev(c, a + (b - a) * (i / 6.0f), mods, 1, a)); c.mouseUp(mev(c, b, mods, 1, a));
}
// ---------------------------------------------------------------- final polish: demo export gate, one tooltip window, SEND TO ARRANGE everywhere, slider convention, version
static const arr::Clip* findClip(SpecProcessor& p, juce::uint32 id) { for (auto& t : p.arrange.arrangement.tracks) for (auto& c : t.clips) if (c.id == id) return &c; return nullptr; }
static void polishTests() {
    std::cout << "POLISH (export gate, tooltips, SEND TO ARRANGE, sliders)" << std::endl;
    SpecProcessor p; FakeHead head; head.playing = false; head.bpm = 124; p.prepareToPlay(48000, 512); p.setPlayHead(&head);
    auto block = [&] { juce::AudioBuffer<float> b(2, 512); juce::MidiBuffer m; p.processBlock(b, m); };
    block(); addLoop(p);
    std::unique_ptr<juce::AudioProcessorEditor> edp(p.createEditor()); auto* ed = dynamic_cast<SpecEditor*>(edp.get()); ed->setVisible(true); pump(150);
    auto click = [&](const char* id) { if (auto* b = dynamic_cast<juce::Button*>(findRec(ed, id))) b->triggerClick(); pump(150); };
    for (auto* t : { "tab_arrange", "tab_arcs", "tab_process", "tab_voice", "tab_lab", "tab_loop" }) click(t);
    click("labButton");
    // 1) every export asks the licence
    auto* arrPg = dynamic_cast<arr::ArrangePage*>(findRec(ed, "arrangePage")); auto* arcsPg = dynamic_cast<arcs::ArcsPage*>(findRec(ed, "arcsPage"));
    auto* procPg = dynamic_cast<LabProcessPage*>(findRec(ed, "processPage"));
#if GHOSTY_VOICE
    auto* voiceTab = dynamic_cast<SpecVoiceTab*>(findRec(ed, "voiceTab"));
#endif
    auto exportsAllowed = [&](int& asked) {   // each path: true = allowed; counts how many paths answered
        int ok = 0; asked = 0;
        auto one = [&](bool v) { ++asked; ok += v ? 1 : 0; };
        one(p.bounce().existsAsFile()); one(!p.bounceStems().isEmpty());
        one(arrPg && arrPg->exportAllowed("spec.arrange.render", false)); one(arcsPg && arcsPg->allowExport && arcsPg->allowExport(arcsPg, "spec.arcs.midi-save")); one(procPg && procPg->exportHost().allowExport(procPg));
#if GHOSTY_VOICE
        one(voiceTab && voiceTab->page().allowExport && voiceTab->page().allowExport(voiceTab, "spec.voice.wav-drag"));
#endif
        return ok; };
    p.exportLockForTest = 1; const int r0 = p.exportRefusals.load(); int asked = 0; const int okLocked = exportsAllowed(asked);
    // the Lab's own: DRAG WAV OUT + SEND TO BEATS (real controls)
    if (auto* w = findRec(ed, "labWavOut")) drag(*w, { 20, 10 }, { 120, 10 }); pump(30);
    if (auto* b = dynamic_cast<juce::Button*>(findRec(ed, "labToBeats"))) { b->triggerClick(); pump(150); }
    const int refused = p.exportRefusals.load() - r0;
    check(okLocked == 0 && refused == asked + 2, "DEMO: every export is refused (pattern WAV + stems, Lab DRAG WAV + SEND TO BEATS, ARRANGE, ARCS, PROCESS, VOICE): every path asks the one gate, which shows the licence message in the real demo",
          juce::String(asked + 2) + " export paths, " + juce::String(refused) + " refused");
    p.exportLockForTest = 0; const int okOpen = exportsAllowed(asked);
    check(okOpen == asked, "LICENSED: every export is allowed", juce::String(okOpen) + " / " + juce::String(asked));
    p.exportLockForTest = -1;
    // 2) one tooltip window for the whole editor
    int tipWins = 0; std::function<void(juce::Component*)> countTips = [&](juce::Component* c) { if (dynamic_cast<juce::TooltipWindow*>(c)) ++tipWins; for (auto* ch : c->getChildren()) countTips(ch); };
    countTips(ed);
#if GHOSTY_VOICE
    { bool themeBtnHidden = true; if (voiceTab) { std::function<void(juce::Component*)> f = [&](juce::Component* c) { if (auto* b = dynamic_cast<juce::TextButton*>(c); b && b->isVisible() && (b->getButtonText() == "DARK" || b->getButtonText() == "LIGHT")) themeBtnHidden = false; for (auto* ch : c->getChildren()) f(ch); }; f(&voiceTab->page()); }
      check(voiceTab && themeBtnHidden, "VOICE: its own DARK / LIGHT button is hidden (Spectrum's toggle is the one)"); }
#endif
    check(tipWins == 1, "ONE tooltip window for every page (the pages make none of their own: no doubled tips)", juce::String(tipWins) + " found");
    // 3) SEND TO ARRANGE from PAINT LOOP / PADS and from ARCS (rendered on a worker, tempo-tagged)
    click("tab_loop");
    juce::uint32 idLoop = 0; p.sendPatternToArrange([&](juce::uint32 id) { idLoop = id; }); waitFor([&] { return idLoop != 0; }, 20000);
    arr::PayloadPtr cl; if (const auto* c = findClip(p, idLoop)) cl = c->source;   // keep the sound (the clip vector moves on the next receive)
    const bool loopOk = cl && cl->bpm == 124.0 && cl->audio.getMagnitude(0, cl->lengthSamples()) > 0.01f;
    const int clipsBefore = [&] { int n = 0; for (auto& t : p.arrange.arrangement.tracks) n += (int) t.clips.size(); return n; }();
    click("patternToArrange"); waitFor([&] { int n = 0; for (auto& t : p.arrange.arrangement.tracks) n += (int) t.clips.size(); return n > clipsBefore; }, 20000);
    const int clipsAfter = [&] { int n = 0; for (auto& t : p.arrange.arrangement.tracks) n += (int) t.clips.size(); return n; }();
    check(loopOk && clipsAfter == clipsBefore + 1, "SEND TO ARRANGE on PAINT LOOP / PADS: what you hear becomes an ARRANGE clip, tagged with the tempo", cl ? "bpm " + juce::String(cl->bpm) + ", " + juce::String(cl->seconds(), 2) + " s" : juce::String("no clip"));
    p.arcsPage.edit() = arcs::factory::pages()[1].make(); p.arcsPage.commit(); click("tab_arcs");
    auto* arcsBtn = findRec(ed, "arcsToArrange"); const bool arcsBtnShown = arcsBtn && arcsBtn->isVisible();
    juce::uint32 idArcs = 0; p.sendArcsToArrange([&](juce::uint32 id) { idArcs = id; }); waitFor([&] { return idArcs != 0; }, 30000);
    arr::PayloadPtr ca; if (const auto* c = findClip(p, idArcs)) ca = c->source;
    check(arcsBtnShown && ca && !ca->notes.empty() && ca->audio.getMagnitude(0, ca->lengthSamples()) > 0.01f, "SEND TO ARRANGE on ARCS: the page (one pass) becomes a clip with its lines as MIDI notes",
          ca ? juce::String(ca->notes.size()) + " notes, " + juce::String(ca->seconds(), 2) + " s" : juce::String("no clip"));
    { bool rrOk = false; juce::String rw;   // ARRANGE can re-render the ARCS clip from its blob: the same sound
      if (ca && ca->source.page == "ARCS" && ca->source.state.getSize() > 0) {
          auto again = p.arrange.rerender(ca->source); double err = 1;
          if (again && again->lengthSamples() == ca->lengthSamples() && again->audio.getNumChannels() == ca->audio.getNumChannels()) { err = 0;
              for (int c = 0; c < ca->audio.getNumChannels(); ++c) for (int i = 0; i < ca->lengthSamples(); ++i) err = std::max(err, (double) std::abs(again->audio.getSample(c, i) - ca->audio.getSample(c, i))); }
          rrOk = again && err < 1e-5 && again->notes.size() == ca->notes.size() && again->bpm == ca->bpm; rw << "largest difference " << (err > 0 ? juce::String(20 * std::log10(err), 1) : juce::String("-inf")) << " dB";
      } else rw = "no ARCS source on the clip";
      juce::Random rnd(7); juce::MemoryBlock junk(4096); for (size_t i = 0; i < junk.getSize(); ++i) static_cast<char*>(junk.getData())[i] = (char) rnd.nextInt(256);
      const bool hostileOk = SpecProcessor::arcsPayload({ "ARCS", "junk", junk }) == nullptr && SpecProcessor::arcsPayload({ "ARCS", "empty", {} }) == nullptr
                          && p.arrange.rerender({ "ARCS", "junk", junk }) == nullptr;
      if (ca) { auto cut = SpecProcessor::arcsPayload({ "ARCS", "cut", juce::MemoryBlock(ca->source.state.getData(), ca->source.state.getSize() / 2) }); (void) cut; }   // must not crash (whatever it reads)
      check(rrOk, "ARCS clips can be re-rendered by ARRANGE (same sound within -100 dB, same notes and tempo)", rw);
      check(hostileOk, "a broken ARCS blob (random bytes, empty) is refused - no clip; a cut blob doesn't crash"); }
    click("tab_loop"); const bool arcsBtnHidden = arcsBtn && !arcsBtn->isVisible();
    check(arcsBtnHidden, "the ARCS SEND TO ARRANGE button only shows on ARCS");
    // 4) slider convention: the bpm bar
    click("tab_loop"); head.bpm = 0;   // (the bpm bar shows when there is no host tempo)
    sp::BarSlider* bar = nullptr; std::function<void(juce::Component*)> findBar = [&](juce::Component* c) { if (!bar) if (auto* b = dynamic_cast<sp::BarSlider*>(c)) bar = b; for (auto* ch : c->getChildren()) findBar(ch); }; findBar(ed);
    bool barOk = false; juce::String barWhy;
    if (bar) {
        const float w = (float) (bar->getWidth() - bar->getTextBoxWidth()), y = bar->getHeight() * 0.5f;
        bar->mouseDown(mev(*bar, { w * 0.75f, y })); bar->mouseUp(mev(*bar, { w * 0.75f, y })); const double vClick = bar->getValue();
        drag(*bar, { w * 0.5f, y }, { w * 0.6f, y }); const double vScrub = bar->getValue();   // from the click value, +10 % of the bar
        const double vBeforeFine = bar->getValue(); drag(*bar, { w * 0.9f, y }, { w * 0.4f, y }, juce::ModifierKeys::shiftModifier); const double vFine = bar->getValue();   // fine: half the bar = 5 %
        juce::MouseWheelDetails wd {}; wd.deltaY = 0.2f; bar->mouseWheelMove(mev(*bar, { 10, y }), wd); const double vWheel = bar->getValue();
        bar->mouseDoubleClick(mev(*bar, { 10, y }, {}, 2)); const double vReset = bar->getValue();
        const double expectClick = std::round(60 + 0.75 * 140), expectScrub = std::round(60 + 0.6 * 140), expectFine = vBeforeFine - 0.05 * 140;
        barOk = std::abs(vClick - expectClick) <= 1 && std::abs(vScrub - expectScrub) <= 1 && std::abs(vFine - expectFine) <= 1.01
             && vWheel == vFine + 1 && vReset == 120 && (int) p.apvts.getRawParameterValue("bpm")->load() == 120;
        barWhy << "click " << vClick << " (" << expectClick << "), scrub " << vScrub << " (" << expectScrub << "), fine " << vBeforeFine << "->" << vFine << " (" << expectFine << "), wheel " << vWheel << ", dbl " << vReset;
    }
    check(barOk, "SLIDERS: the bpm bar - click = the value under the mouse, drag scrubs, Shift = fine, wheel = 1 bpm, double-click = default", barWhy);
    // SPLIT step bars + the morph lane (Lab)
    click("labButton"); auto* panel = dynamic_cast<LabPanel*>(findRec(ed, "spectralLab")); if (panel) panel->showPage(3); pump(80);
    auto& L = p.lab; bool splitOk = false, laneOk = false; juce::String sw, lw;
    if (auto* grid = findRec(ed, "splitGrid")) {
        click("splitMode1"); const int rh = std::max(16, (grid->getHeight() - 4) / std::max(2, L.split.bands)); const float sx = 56 + 3, row = (float) rh - 5;
        auto stepY = [&](float f) { return 2.0f + (1.0f - f) * row; };
        const int u0 = L.undo.depth();
        grid->mouseDown(mev(*grid, { sx, stepY(0.3f) })); grid->mouseUp(mev(*grid, { sx, stepY(0.3f) })); const int pClick = L.split.seq[0].prob[0];
        drag(*grid, { sx, stepY(0.3f) }, { sx, stepY(0.8f) }); const int pScrub = L.split.seq[0].prob[0];
        drag(*grid, { sx, stepY(0.8f) }, { sx, stepY(0.8f) - 20 }, juce::ModifierKeys::shiftModifier); const int pFine = L.split.seq[0].prob[0];
        juce::MouseWheelDetails wd {}; wd.deltaY = 0.2f; grid->mouseWheelMove(mev(*grid, { sx, stepY(0.5f) }), wd); const int pWheel = L.split.seq[0].prob[0];
        grid->mouseDoubleClick(mev(*grid, { sx, stepY(0.5f) }, {}, 2)); const int pReset = L.split.seq[0].prob[0];
        const int undoSteps = L.undo.depth() - u0;
        click("splitMode2"); grid->mouseDown(mev(*grid, { sx, stepY(1.0f) })); grid->mouseUp(mev(*grid, { sx, stepY(1.0f) })); const int pitchTop = L.split.seq[0].pitch[0];
        grid->mouseDoubleClick(mev(*grid, { sx, stepY(0.5f) }, {}, 2)); const int pitchReset = L.split.seq[0].pitch[0];
        splitOk = std::abs(pClick - 30) <= 2 && std::abs(pScrub - 80) <= 2 && pFine > pScrub && pFine <= pScrub + 4 && pWheel == pFine + 1 && pReset == 100 && pitchTop == 12 && pitchReset == 0 && undoSteps == 5;
        sw << "chance click " << pClick << ", scrub " << pScrub << ", fine " << pFine << ", wheel " << pWheel << ", dbl " << pReset << "; pitch top " << pitchTop << ", dbl " << pitchReset << "; undo steps " << undoSteps;
        click("splitMode0");
    }
    if (panel) panel->showPage(5); pump(80);
    if (auto* lane = findRec(ed, "morphLane")) {
        const float bw = lane->getWidth() / 8.0f, hh = (float) lane->getHeight();
        lane->mouseDown(mev(*lane, { bw * 2.5f, hh * 0.25f })); lane->mouseUp(mev(*lane, { bw * 2.5f, hh * 0.25f })); const float v1 = L.morphLane[2].load();
        drag(*lane, { bw * 2.5f, hh * 0.5f }, { bw * 2.5f, hh * 0.5f - 20 }, juce::ModifierKeys::shiftModifier); const float vf = L.morphLane[2].load();
        juce::MouseWheelDetails wd {}; wd.deltaY = -0.2f; lane->mouseWheelMove(mev(*lane, { bw * 2.5f, 5 }), wd); const float vw = L.morphLane[2].load();
        lane->mouseDoubleClick(mev(*lane, { bw * 2.5f, 5 }, {}, 2)); const float vr = L.morphLane[2].load();
        laneOk = std::abs(v1 - 0.75f) < 0.02f && vf > v1 && vf < v1 + 20.0f / (10.0f * hh) + 0.01f && std::abs(vw - (vf - 0.01f)) < 1e-4f && vr == 0.3f;
        lw << "click " << v1 << ", fine " << vf << ", wheel " << vw << ", dbl " << vr;
    }
    check(splitOk, "SLIDERS: SPLIT step bars - click sets the value at that height, drag scrubs, Shift = fine, wheel = one step, double-click = default, one undo step each", sw);
    check(laneOk, "SLIDERS: the MORPH lane bars - click = value, Shift = fine, wheel = 1 %, double-click = its default", lw);
    // 6) no product / brand names in the sound lists Spectrum shows (SYNTHS, DRUMS groups, ARCS factory)
    { const juce::StringArray brands { "OTT", "Serum", "Massive", "Melodyne", "Omnisphere", "Kontakt", "Sylenth", "Nexus", "MPC", "303", "Juno", "Moog", "Roland", "Korg", "Rhodes", "Wurli", "Ableton", "FL Studio", "FabFilter", "Gross Beat" };
      juce::StringArray hits; for (const auto& b : sp::synthBank()) for (const auto& w : brands) if ((" " + b.name + " ").containsIgnoreCase(" " + w + " ")) hits.add(b.name);
      for (const auto& g : gb::DrumLibrary::categories()) for (const auto& w : brands) if (g.containsIgnoreCase(w)) hits.add(g);
      check(hits.isEmpty(), "BRANDS: no product names in the SYNTHS / DRUMS lists (e.g. the old 'OTT Phantom' now reads 'Squeeze Phantom')", hits.joinIntoString(", ")); }
    // ARRANGE inside Spectrum: no type-in of its own (the editor's one covers it) -> right-click opens one box; the typed value lands
    { click("tab_arrange"); auto* pg = dynamic_cast<arr::ArrangePage*>(findRec(ed, "arrangePage"));
      juce::Slider* pitch = nullptr; auto findPitch = [&] { pitch = nullptr; std::function<void(juce::Component*)> f = [&](juce::Component* c) { if (auto* sl = dynamic_cast<juce::Slider*>(c); sl && sl->isVisible() && !pitch && sl->getTooltip().startsWith("Pitch")) pitch = sl; for (auto* ch : c->getChildren()) f(ch); }; if (pg) f(pg); };
      findPitch(); if (!pitch && pg) { pg->selectAll(); pump(120); findPitch(); }
      bool lands = false; juce::String why;
      if (pitch) { pitch->setValue(pitch->getValueFromText("3"), juce::sendNotificationSync); pump(60);   // what the editor's TypeIn does on Enter
          for (auto& t : p.arrange.arrangement.tracks) for (auto& c : t.clips) if (std::abs(c.pitch - 3.0f) < 1e-4f) lands = true; why << "pitch bar " << pitch->getValue(); }
      else why = "no pitch bar";
      check(pg && !pg->hasTypeIn() && pitch && ed->isParentOf(pitch) && lands, "ARRANGE in Spectrum: one type-in (the editor's covers its bars; the page makes none) and a typed value lands on the clip", why); }
    // 5) version
    juce::String tip; if (auto* tb = dynamic_cast<juce::SettableTooltipClient*>(findRec(ed, "syncButton")->getParentComponent())) tip = tb->getTooltip();
    check(tip.contains("3.0.0"), "the version shows (tab bar tooltip + header): " + tip);
    edp.reset(); pump(80);
}

// ---------------------------------------------------------------- the Lab's WaveRegion (START / END / LOOP, the GhostBlock sample gestures)
static void waveRegionTests() {
    std::cout << "LAB WAVE (START / END / LOOP gestures)" << std::endl;
    SpecProcessor p; p.prepareToPlay(48000, 512);
    std::unique_ptr<juce::AudioProcessorEditor> edp(p.createEditor()); auto* ed = dynamic_cast<SpecEditor*>(edp.get()); ed->setVisible(true); pump(150);
    if (auto* b = dynamic_cast<juce::Button*>(findRec(ed, "labButton"))) b->triggerClick(); pump(150);
    auto* panel = dynamic_cast<LabPanel*>(findRec(ed, "spectralLab")); if (panel) panel->showPage(0); pump(60);
    auto& L = p.lab; analyse(testSignal(3.0), L.canvas); const int v0 = L.bankVersion.load(); L.edited(); L.requestPreview(); waitFor([&] { return L.bankVersion.load() > v0 && !L.busy.load(); }); pump(250);
    auto* w = dynamic_cast<ghosty::WaveRegion*>(findRec(ed, "labWave"));
    if (!w) { check(false, "LAB WAVE: the waveform is on the PAINT page"); return; }
    const auto bank = L.latestBank(); const double secs = bank && bank->main ? bank->main->size() / 48000.0 : 0;
    check(w->isVisible() && w->hasWave() && secs > 2.5, "LAB WAVE: the PAINT page shows what you hear as a waveform with START / END / LOOP", juce::String(secs, 2) + " s");
    auto ev = [&](float f, juce::ModifierKeys m = {}, int clicks = 1) { return mev(*w, { w->xOf(f), w->waveArea().getCentreY() }, m, clicks); };
    const auto right = juce::ModifierKeys::rightButtonModifier;
    // a plain left-click: hears it (scrub) and moves nothing
    const auto before = w->region(); w->mouseDown(ev(0.5f)); const bool hearing = L.scrubPos.load() >= 0; w->mouseUp(ev(0.5f));
    check(hearing && L.scrubPos.load() < 0 && w->region() == before && L.playStart.load() == 0.0f, "LAB WAVE: a plain click hears it and never moves START or makes a LOOP");
    // right-click = START here
    w->mouseDown(ev(0.25f, right)); w->mouseUp(ev(0.25f, right));
    check(std::abs(L.playStart.load() - 0.25f) < 0.01f && !L.loopRegion.load(), "LAB WAVE: right-click = START here (the Lab's PLAY starts there)", juce::String(L.playStart.load(), 3));
    // right-drag = a LOOP: the SELECT box's time span + LOOP SEL on
    { auto a = ev(0.4f, right); w->mouseDown(a); for (int i = 1; i <= 6; ++i) w->mouseDrag(mev(*w, { w->xOf(0.4f + 0.05f * i), w->waveArea().getCentreY() }, right, 1, a.position)); w->mouseUp(ev(0.7f, right)); }
    const double rs = L.regionStart.load(), re = L.regionEnd.load(), len = bank->main->size();
    auto* loopSel = [&]() -> juce::Button* { juce::Button* r = nullptr; std::function<void(juce::Component*)> f = [&](juce::Component* c) { if (auto* b = dynamic_cast<juce::TextButton*>(c); b && b->getButtonText() == "LOOP SEL") r = b; for (auto* ch : c->getChildren()) f(ch); }; f(panel); return r; }();
    check(L.loopRegion.load() && std::abs(rs / len - 0.4) < 0.02 && std::abs(re / len - 0.7) < 0.02 && loopSel && loopSel->getToggleState(),
          "LAB WAVE: right-drag = LOOP over that span (the same region HEAR / LOOP SEL use; LOOP SEL lights)", juce::String(rs / len, 3) + " .. " + juce::String(re / len, 3));
    // the LOOP pill turns it off (and LOOP SEL follows)
    { auto pill = w->loopPill().getCentre(); w->mouseDown(mev(*w, pill)); w->mouseUp(mev(*w, pill)); }
    check(!L.loopRegion.load() && loopSel && !loopSel->getToggleState(), "LAB WAVE: the LOOP pill = LOOP ON / OFF (= LOOP SEL)");
    // drag the END flag
    { auto f = w->handleFlag(1).getCentre(); w->mouseDown(mev(*w, f)); for (int i = 1; i <= 6; ++i) w->mouseDrag(mev(*w, f.translated(-10.0f * i, 0), {}, 1, f)); w->mouseUp(mev(*w, f.translated(-60, 0), {}, 1, f)); }
    check(L.playEnd.load() < 0.99f && L.playEnd.load() > 0.8f, "LAB WAVE: drag END (the flag shows the time)", juce::String(L.playEnd.load(), 3));
    // a SELECT box in the spectrogram shows up as the LOOP span
    { ghosty::WaveRegion::Region r = w->region(); check(r.loopEnd > r.loopStart, "LAB WAVE: the box (SELECT) and the LOOP are one region"); }
    // the Lab's PLAY stays between START and END (host stopped)
    { L.previewPlaying = true; float lo = 1, hi = 0; juce::AudioBuffer<float> b(2, 512); juce::MidiBuffer m; lab::LabParams lp;
      for (int k = 0; k < 600; ++k) { b.clear(); L.process(b, m, 120, 0, false, lp); const double pp = L.playPos.load(); if (pp >= 0) { lo = std::min(lo, (float) pp); hi = std::max(hi, (float) pp); } }
      L.previewPlaying = false;
      check(lo >= L.playStart.load() - 0.01f && hi <= L.playEnd.load() + 0.01f && hi > lo + 0.1f, "LAB WAVE: PLAY runs from START to END and wraps there", juce::String(lo, 3) + " .. " + juce::String(hi, 3)); }
    // saved with the Lab; an older session (no START / END) plays the whole sound
    { auto t = L.toTree(); lab::LabHost L2; L2.fromTree(t); pump(50); const bool kept = std::abs(L2.playStart.load() - L.playStart.load()) < 1e-4f && std::abs(L2.playEnd.load() - L.playEnd.load()) < 1e-4f;
      t.removeProperty("play", nullptr); lab::LabHost L3; L3.playStart = 0.3f; L3.fromTree(t); pump(50);
      check(kept && L3.playStart.load() == 0.0f && L3.playEnd.load() == 1.0f, "LAB WAVE: START / END are saved; an older session loads the whole sound (as before)"); }
    // the WAV grip: gated, then the highlighted part as a WAV
    { juce::StringArray dragged; w->performDrag = [&](const juce::StringArray& f) { dragged = f; };
      p.exportLockForTest = 1; const int r0 = p.exportRefusals.load(); w->startExport(); const bool refused = dragged.isEmpty() && p.exportRefusals.load() == r0 + 1;
      p.exportLockForTest = 0; juce::String why; waitFor([&] { auto f = L.dragFileRegion(120, 0, 0.001f, why); const bool ok = f.existsAsFile(); f.deleteFile(); return ok; }, 20000);
      w->startExport(); p.exportLockForTest = -1;
      double got = 0; if (dragged.size() == 1) { juce::AudioFormatManager fm; fm.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> rd(fm.createReaderFor(juce::File(dragged[0]))); if (rd) got = rd->lengthInSamples / rd->sampleRate; }
      const auto [a, b] = w->highlighted(); const double want = (b - a) * secs;
      check(refused && dragged.size() == 1 && std::abs(got - want) < 0.05, "LAB WAVE: the WAV grip drags out the highlighted part (refused in the demo)", juce::String(got, 3) + " s (want " + juce::String(want, 3) + " s)");
      for (auto& f : dragged) juce::File(f).deleteFile(); }
    edp.reset(); pump(80);
}
// ---------------------------------------------------------------- HOW IT WORKS: the guide overlay, TRY IT, first-run hints, tooltips everywhere
static void guideTests(const juce::File& shotDir = {}) {
    std::cout << "HOW IT WORKS (guide, TRY IT, first-run hints, tooltips)" << std::endl;
    SpecProcessor p; FakeHead head; head.playing = false; p.prepareToPlay(48000, 512); p.setPlayHead(&head);
    { juce::AudioBuffer<float> b(2, 512); juce::MidiBuffer m; p.processBlock(b, m); }
    std::unique_ptr<juce::AudioProcessorEditor> edp(p.createEditor()); auto* ed = dynamic_cast<SpecEditor*>(edp.get()); ed->setVisible(true); pump(150);
    auto click = [&](const juce::String& id) { if (auto* b = dynamic_cast<juce::Button*>(findRec(ed, id))) b->triggerClick(); pump(150); };
    auto* g = ed->guidePanel();
    // 1) the '?' opens WHAT IS SPECTRUM; Esc closes; a tab's '?' opens that tab's card; CLOSE closes
    click("howItWorks"); const bool opened = ed->guideOpen() && g->card() == 0;
    g->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)); pump(30); const bool escClosed = !ed->guideOpen();
    click("help_tab_arcs"); const bool arcsCard = ed->guideOpen() && sp::guideCards()[(size_t) g->card()].id == "arcs";
    click("guideClose"); const bool closed = !ed->guideOpen();
    check(opened && escClosed && arcsCard && closed, "GUIDE: '?' opens WHAT IS SPECTRUM, a tab's '?' opens its card, Esc / CLOSE close it");
    // 2) every card renders in light and dark, nothing clipped, also at the smallest window size
    bool allFit = true, allDrawn = true; juce::String bad;
    for (int dark = 1; dark >= 0; --dark) for (int small = 0; small < 2; ++small) {
        ed->setTheme(dark == 1); ed->setSize(small ? 900 : 1200, small ? (790 + SpecEditor::kBarH) * 3 / 4 : 790 + SpecEditor::kBarH); pump(60);
        ed->openGuide(0); pump(30);
        for (int c = 0; c < g->numCards(); ++c) {
            g->showCard(c); pump(20); if (!g->textFits(c)) { allFit = false; bad << sp::guideCards()[(size_t) c].id << " "; }
            auto img = ed->createComponentSnapshot(ed->getLocalBounds()); const auto pc = img.getPixelAt(img.getWidth() / 2, img.getHeight() / 2);
            if (!img.isValid() || pc.getAlpha() == 0) allDrawn = false;
            if (shotDir != juce::File() && !small && (c == 0 || sp::guideCards()[(size_t) c].id == "lab" || sp::guideCards()[(size_t) c].id == "arrange")) {
                auto f = shotDir.getChildFile("guide-" + sp::guideCards()[(size_t) c].id + (dark ? "-dark" : "-light") + ".png"); f.deleteFile(); juce::FileOutputStream os(f); juce::PNGImageFormat png; png.writeImageToStream(img, os); std::cout << "  shot " << f.getFileName() << std::endl; }
        }
        ed->closeGuide();
    }
    ed->setTheme(true); ed->setSize(1200, 790 + SpecEditor::kBarH); pump(60);
    check(allFit && allDrawn, "GUIDE: all " + juce::String(g->numCards()) + " cards draw in light + dark with nothing clipped, also at the smallest window", bad);
    // 3) TRY IT on every card leaves something to hear / see
    juce::StringArray tryBad;
    for (int c = 0; c < g->numCards(); ++c) {
        const auto& cd = sp::guideCards()[(size_t) c]; const int clips0 = [&] { int n = 0; for (auto& t : p.arrange.arrangement.tracks) n += (int) t.clips.size(); return n; }();
        ed->openGuide(c); pump(20); click("guideTry"); pump(200);
        bool ok = !ed->guideOpen();
        const auto k = cd.tab < 0 ? sp::TabKind::Pads : (sp::TabKind) cd.tab;
        switch (k) {
            case sp::TabKind::Pads: ok = ok && !p.model.pads.empty(); break;
            case sp::TabKind::Loop: ok = ok && p.model.notes.size() >= 8; break;
            case sp::TabKind::Lab: case sp::TabKind::Process: ok = ok && waitFor([&] { auto b = p.lab.latestBank(); if (!b || !b->main) return false; float m = 0; for (auto x : b->main->m) m = std::max(m, std::abs(x)); return m > 0.01f; }, 15000); break;
            case sp::TabKind::Arcs: ok = ok && !p.arcsPage.page().arcs.empty(); break;
            case sp::TabKind::Arrange: ok = ok && [&] { int n = 0; for (auto& t : p.arrange.arrangement.tracks) n += (int) t.clips.size(); return n; }() == clips0 + 2; break;
            case sp::TabKind::Voice:
#if GHOSTY_VOICE
                ok = ok && p.voice.source(0) && p.voice.source(0)->seconds() > 2.5;
#endif
                break;
        }
        if (!ok) tryBad.add(cd.id);
    }
    check(tryBad.isEmpty(), "TRY IT: every card's example loads (pads, loop notes, Lab sound, PROCESS source, ARCS lines, 2 ARRANGE clips, VOICE source)", tryBad.joinIntoString(", "));
    // 4) first-run hints: once per tab, saved with the session
    { SpecProcessor q; q.prepareToPlay(48000, 512); std::unique_ptr<juce::AudioProcessorEditor> e2p(q.createEditor()); auto* e2 = dynamic_cast<SpecEditor*>(e2p.get()); e2->setVisible(true); pump(120);
      auto c2 = [&](const juce::String& id) { if (auto* b = dynamic_cast<juce::Button*>(findRec(e2, id))) b->triggerClick(); pump(120); };
      c2("tab_arrange"); auto* h = e2->firstRunHint(); const bool shown = h && h->isVisible() && h->tab() == sp::TabKind::Arrange;
      if (shotDir != juce::File()) for (int dark = 1; dark >= 0; --dark) { e2->setTheme(dark == 1); pump(120); auto img = e2->createComponentSnapshot(e2->getLocalBounds());
          auto f = shotDir.getChildFile(juce::String("first-run-hint-") + (dark ? "dark" : "light") + ".png"); f.deleteFile(); juce::FileOutputStream os(f); juce::PNGImageFormat png; png.writeImageToStream(img, os); std::cout << "  shot " << f.getFileName() << std::endl; }
      c2("hintGotIt"); const bool gone = !h->isVisible() && (q.guideSeen.load() & (1 << (int) sp::TabKind::Arrange));
      c2("tab_loop"); c2("tab_arrange"); const bool notAgain = !h->isVisible();
      juce::MemoryBlock st; q.getStateInformation(st); SpecProcessor q2; q2.setStateInformation(st.getData(), (int) st.getSize());
      const bool kept = (q2.guideSeen.load() & (1 << (int) sp::TabKind::Arrange)) != 0;
      check(shown && gone && notAgain && kept, "FIRST RUN: a tab's hint shows once, GOT IT puts it away, the seen flag is saved with the session"); e2p.reset(); pump(50); }
    // 5) tooltips on every button of Spectrum's own UI (the shared pages report theirs to their owners)
    { for (auto* t : { "tab_pads", "tab_loop", "labButton", "tab_process", "tab_arcs", "tab_arrange", "tab_voice" }) click(t);
      if (auto* lab = dynamic_cast<LabPanel*>(findRec(ed, "spectralLab"))) for (int pg = 0; pg < 6; ++pg) { lab->showPage(pg); pump(20); }
      ed->openGuide(0); pump(20);
      juce::StringArray missing, foreignMissing;
      std::function<void(juce::Component*, bool)> walk = [&](juce::Component* c, bool foreign) {
          foreign = foreign || (bool) c->getProperties().getWithDefault("foreignTheme", false) || c->getComponentID() == "processLab";
          if (auto* b = dynamic_cast<juce::Button*>(c)) if (b->getTooltip().trim().isEmpty()) (foreign ? foreignMissing : missing).add(b->getButtonText().isNotEmpty() ? b->getButtonText() : (b->getComponentID().isNotEmpty() ? b->getComponentID() : juce::String(typeid(*b).name()) + "@" + juce::String(typeid(*b->getParentComponent()).name())));
          for (auto* ch : c->getChildren()) walk(ch, foreign); };
      walk(ed, false); ed->closeGuide();
      check(missing.isEmpty(), "TOOLTIPS: every button on Spectrum's own tabs, the tab bar and the guide has a tooltip in plain words", missing.joinIntoString(", "));
      check(foreignMissing.isEmpty(), "TOOLTIPS: ... and on the ARCS / ARRANGE / VOICE / PROCESS LAB pages", foreignMissing.joinIntoString(", ")); }
    edp.reset(); pump(80);
}
// ---------------------------------------------------------------- the three house looks (BLACK / PURPLE / LIGHT): opaque surfaces + readable text everywhere
static double textContrastAt(const juce::Image& img, juce::Rectangle<int> r) {
    r = r.getIntersection(img.getBounds()); if (r.getWidth() < 4 || r.getHeight() < 4) return 21.0;
    std::map<juce::uint32, int> hist; for (int y = r.getY(); y < r.getBottom(); ++y) for (int x = r.getX(); x < r.getRight(); ++x) { auto c = img.getPixelAt(x, y); hist[(c.getARGB() & 0xfff0f0f0u)]++; }
    juce::uint32 bgKey = 0; int best = -1; for (auto& [k, n] : hist) if (n > best) { best = n; bgKey = k; }
    juce::Colour bg; int nb = 0; double rr = 0, gg = 0, bb = 0;   // the background = the average of the most common colour bucket
    for (int y = r.getY(); y < r.getBottom(); ++y) for (int x = r.getX(); x < r.getRight(); ++x) { auto c = img.getPixelAt(x, y); if ((c.getARGB() & 0xfff0f0f0u) == bgKey) { rr += c.getRed(); gg += c.getGreen(); bb += c.getBlue(); ++nb; } }
    bg = juce::Colour((juce::uint8) (rr / nb), (juce::uint8) (gg / nb), (juce::uint8) (bb / nb));
    double m = 1; for (int y = r.getY(); y < r.getBottom(); ++y) for (int x = r.getX(); x < r.getRight(); ++x) m = std::max(m, ghosty::themes::contrast(img.getPixelAt(x, y).withAlpha(1.0f), bg));
    return m;
}
struct LookIssue { juce::String where; double c; };
static void lookContrastScan(juce::Component& ed, juce::Component& root, const juce::String& where, std::vector<LookIssue>& bad, int& checked, juce::Array<juce::Component*> skip = {}) {
    auto img = ed.createComponentSnapshot(ed.getLocalBounds(), true, 2.0f);   // 2x: thin glyphs ('-', '1') reach their full colour
    std::function<void(juce::Component*)> walk = [&](juce::Component* c) {
        if (!c->isVisible() || skip.contains(c)) return;
        juce::String text; bool isText = false;
        if (auto* l = dynamic_cast<juce::Label*>(c)) { text = l->getText(); isText = l->isEnabled() && l->findColour(juce::Label::textColourId).getAlpha() > 0; }   // (disabled controls are exempt, as in WCAG)
        else if (auto* b = dynamic_cast<juce::TextButton*>(c)) { text = b->getButtonText(); isText = b->isEnabled(); }
        else if (auto* cb = dynamic_cast<juce::ComboBox*>(c)) { text = cb->getText(); isText = cb->isEnabled(); }
        if (isText && text.trim().isNotEmpty() && c->getWidth() >= 14 && c->getHeight() >= 10) {
            auto r = ed.getLocalArea(c, c->getLocalBounds()).reduced(c->getWidth() > 24 ? 4 : 2, c->getHeight() > 16 ? 3 : 1) * 2;
            const double k = textContrastAt(img, r); ++checked;
            if (k < 4.5) bad.push_back({ where + ": '" + text.substring(0, 24) + "'", k });
        }
        if (dynamic_cast<juce::ComboBox*>(c) == nullptr) for (auto* ch : c->getChildren()) walk(ch);
    };
    walk(&root);
}
static void houseLookTests(const juce::File& shotDir = {}) {
    std::cout << "LOOKS (BLACK / PURPLE / LIGHT)" << std::endl;
    SpecProcessor p; p.guideSeen = 0x7fffffff; FakeHead head; head.playing = false; p.prepareToPlay(48000, 512); p.setPlayHead(&head);
    { juce::AudioBuffer<float> b(2, 512); juce::MidiBuffer m; p.processBlock(b, m); }
    std::unique_ptr<juce::AudioProcessorEditor> edp(p.createEditor()); auto* ed = dynamic_cast<SpecEditor*>(edp.get()); ed->setVisible(true); pump(150);
    auto click = [&](const juce::String& id) { if (auto* b = dynamic_cast<juce::Button*>(findRec(ed, id))) b->triggerClick(); pump(150); };
    // a little of everything so the pages show their real controls
    ed->tryIt(sp::guideCardFor(sp::TabKind::Arrange)); ed->tryIt(sp::guideCardFor(sp::TabKind::Arcs)); ed->tryIt(sp::guideCardFor(sp::TabKind::Lab)); ed->tryIt(sp::guideCardFor(sp::TabKind::Loop)); pump(300);
    // 1) the button cycles BLACK -> PURPLE -> LIGHT and names the next look
    ed->setLook(ghosty::themes::Theme::Dark); pump(30); auto* tb = dynamic_cast<juce::TextButton*>(findRec(ed, "themeButton"));
    juce::StringArray seen; for (int i = 0; i < 3; ++i) { seen.add(sp::pal().look == ghosty::themes::Theme::Dark ? "BLACK" : sp::pal().look == ghosty::themes::Theme::Purple ? "PURPLE" : "LIGHT"); seen.add(tb ? tb->getButtonText() : "?"); click("themeButton"); }
    check(seen.joinIntoString(",") == "BLACK,PURPLE,PURPLE,LIGHT,LIGHT,BLACK" && sp::pal().look == ghosty::themes::Theme::Dark, "LOOKS: the button cycles BLACK -> PURPLE -> LIGHT and names the next look", seen.joinIntoString(","));
    // 2) saved as theme_name; older sessions: dark=true -> BLACK, dark=false -> LIGHT, none -> BLACK
    { auto roundTrip = [&](std::function<void(juce::ValueTree&)> edit) { SpecProcessor a; a.uiLook = ghosty::themes::Theme::Purple; juce::MemoryBlock st; a.getStateInformation(st);
          auto x = juce::AudioProcessor::getXmlFromBinary(st.getData(), (int) st.getSize()); auto t = juce::ValueTree::fromXml(*x); auto u = t.getChildWithName("SPECTRUMUI"); edit(u);
          juce::MemoryBlock o; juce::AudioProcessor::copyXmlToBinary(*t.createXml(), o); SpecProcessor b; b.setStateInformation(o.getData(), (int) o.getSize()); return b.uiLook; };
      const auto keep = roundTrip([](juce::ValueTree& u) { (void) u; });
      const auto oldDark = roundTrip([](juce::ValueTree& u) { u.removeProperty("theme_name", nullptr); u.setProperty("dark", true, nullptr); });
      const auto oldLight = roundTrip([](juce::ValueTree& u) { u.removeProperty("theme_name", nullptr); u.setProperty("dark", false, nullptr); });
      const auto none = roundTrip([](juce::ValueTree& u) { u.removeProperty("theme_name", nullptr); u.removeProperty("dark", nullptr); });
      check(keep == ghosty::themes::Theme::Purple && oldDark == ghosty::themes::Theme::Dark && oldLight == ghosty::themes::Theme::Light && none == ghosty::themes::Theme::Dark,
            "LOOKS: saved as theme_name; an older session's dark switch still loads (on = BLACK, off = LIGHT, none = BLACK)"); }
    // 3) every page + the overlays in all three looks: text >= 4.5:1 against what is behind it (pixels), screenshots
    std::vector<LookIssue> bad; int checked = 0;
    struct Pg { const char* tab; int labPage; const char* name; };
    const Pg pages[] = { { "tab_pads", -1, "pads" }, { "tab_loop", -1, "loop" }, { "labButton", 0, "lab-paint" }, { "labButton", 1, "lab-image" }, { "labButton", 2, "lab-ghost" }, { "labButton", 3, "lab-split" },
                         { "labButton", 4, "lab-chaos" }, { "labButton", 5, "lab-dna" }, { "tab_process", -1, "process" }, { "tab_arcs", -1, "arcs" }, { "tab_arrange", -1, "arrange" }, { "tab_voice", -1, "voice" } };
    for (auto look : { ghosty::themes::Theme::Dark, ghosty::themes::Theme::Purple, ghosty::themes::Theme::Light }) {
        ed->setLook(look); pump(80); const juce::String ln = ghosty::themes::toString(look);
        for (auto& pg : pages) {
            if (!findRec(ed, pg.tab)) continue; click(pg.tab);
            auto* lab = dynamic_cast<LabPanel*>(findRec(ed, "spectralLab")); if (pg.labPage >= 0 && lab) { lab->showPage(pg.labPage); pump(120); }
            juce::Component* root = nullptr; juce::Array<juce::Component*> skip;
            const juce::String t = pg.tab;
            if (t == "labButton") root = lab;
            else if (t == "tab_process") root = findRec(ed, "processPage"); else if (t == "tab_arcs") root = findRec(ed, "arcsPage");
            else if (t == "tab_arrange") root = findRec(ed, "arrangePage"); else if (t == "tab_voice") root = findRec(ed, "voiceTab");
            else { root = ed->content_(); for (auto* id : { "spectralLab", "processPage", "arcsPage", "arrangePage", "voiceTab" }) if (auto* c = findRec(ed, id)) skip.add(c); }
            if (root) lookContrastScan(*ed, *root, ln + " " + pg.name, bad, checked, skip);
            for (auto* c : ed->getChildren()) if (c->getComponentID() != "guide" && c->getComponentID() != "firstRunHint" && c != ed->content_()) lookContrastScan(*ed, *c, ln + " bar", bad, checked);   // the tab bar
            if (shotDir != juce::File()) { auto img = ed->createComponentSnapshot(ed->getLocalBounds()); auto f = shotDir.getChildFile(juce::String("look-") + ln + "-" + pg.name + ".png"); f.deleteFile(); juce::FileOutputStream os(f); juce::PNGImageFormat png; png.writeImageToStream(img, os); }
        }
        { auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 2.0f);   // BYPASS + SYNC: the shared buttons in the house look
          for (auto* id : { "bypassButton", "syncButton" }) if (auto* c = findRec(ed, id)) { const double k = textContrastAt(img, ed->getLocalArea(c, c->getLocalBounds()).reduced(6, 4) * 2); ++checked; if (k < 4.5) bad.push_back({ ln + " " + id, k }); } }
        ed->openGuide(sp::guideCardFor(sp::TabKind::Lab)); pump(60); lookContrastScan(*ed, *ed->guidePanel(), ln + " guide", bad, checked);
        if (shotDir != juce::File()) { auto img = ed->createComponentSnapshot(ed->getLocalBounds()); auto f = shotDir.getChildFile("look-" + ln + "-guide.png"); f.deleteFile(); juce::FileOutputStream os(f); juce::PNGImageFormat png; png.writeImageToStream(img, os); }
        ed->closeGuide();
        if (auto* h = ed->firstRunHint()) { h->showFor(sp::TabKind::Arrange); pump(40); lookContrastScan(*ed, *h, ln + " hint", bad, checked);
            if (shotDir != juce::File()) { auto img = ed->createComponentSnapshot(ed->getLocalBounds()); auto f = shotDir.getChildFile("look-" + ln + "-hint.png"); f.deleteFile(); juce::FileOutputStream os(f); juce::PNGImageFormat png; png.writeImageToStream(img, os); }
            h->setVisible(false); }
    }
    juce::String worst; std::sort(bad.begin(), bad.end(), [](auto& a, auto& b) { return a.c < b.c; });
    for (size_t i = 0; i < bad.size() && i < 40; ++i) worst << bad[i].where << " " << juce::String(bad[i].c, 2) << "; ";
    check(bad.empty() && checked > 300, "LOOKS: text reads >= 4.5:1 on every page, the bar, the guide and the hint in all three looks (" + juce::String(checked) + " text items)", juce::String((int) bad.size()) + " below: " + worst);
    // 4) the surfaces behind the pages are opaque tokens: the editor background is exactly the look's window colour
    { bool ok = true; for (auto look : { ghosty::themes::Theme::Dark, ghosty::themes::Theme::Purple, ghosty::themes::Theme::Light }) { ed->setLook(look); pump(40); ok = ok && sp::pal().bg == ghosty::themes::get(look).window && sp::pal().bg.isOpaque() && sp::pal().panel.isOpaque() && sp::pal().panel2.isOpaque() && sp::pal().field.isOpaque(); }
      check(ok, "LOOKS: every surface colour is an opaque house token"); }
    ed->setLook(ghosty::themes::Theme::Dark); edp.reset(); pump(80);
}
// ---------------------------------------------------------------- ARCS: the loop brace's WAV / MIDI grips (only the loop, gated)
static void arcsLoopGripTests() {
    std::cout << "ARCS LOOP GRIPS" << std::endl;
    SpecProcessor p; p.guideSeen = 0x7fffffff; p.prepareToPlay(48000, 512);
    std::unique_ptr<juce::AudioProcessorEditor> edp(p.createEditor()); auto* ed = dynamic_cast<SpecEditor*>(edp.get()); ed->setVisible(true); pump(150);
    ed->tryIt(sp::guideCardFor(sp::TabKind::Arcs)); pump(150);
    auto* pg = dynamic_cast<arcs::ArcsPage*>(findRec(ed, "arcsPage")); if (!pg) { check(false, "ARCS page"); return; }
    auto setLoop = [&](float a, float b) { auto& P = p.arcsPage.edit(); P.loopOn = true; P.loopA = a; P.loopB = b; p.arcsPage.commit("loop"); };
    setLoop(0.0f, 0.5f); const bool notYet = !pg->loopWavReady();
    const bool ready = waitFor([&] { return pg->loopWavReady(); }, 30000);
    auto req = p.arcsPage.prepareRender(); double t0 = 0, t1 = 0; arcs::ArcsHost::loopSeconds(*req.snap, req.bpm, t0, t1);
    juce::StringArray got; auto hook = [&](const juce::StringArray& f) { got = f; };
    p.exportLockForTest = 1; const int r0 = p.exportRefusals.load(); pg->dragLoopOut(false, hook); pg->dragLoopOut(true, hook); const bool refused = got.isEmpty() && p.exportRefusals.load() == r0 + 2;
    p.exportLockForTest = 0; pg->dragLoopOut(false, hook); const auto wavFile = got.isEmpty() ? juce::File() : juce::File(got[0]);
    double wavSecs = 0, err = 1; { juce::AudioFormatManager fm; fm.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> rd(fm.createReaderFor(wavFile));
        if (rd) { wavSecs = rd->lengthInSamples / rd->sampleRate; juce::AudioBuffer<float> a((int) rd->numChannels, (int) rd->lengthInSamples); rd->read(&a, 0, a.getNumSamples(), 0, true, true);
            juce::AudioBuffer<float> ref; arcs::ArcsHost::renderLoopToBuffer(req, ref, 48000.0); err = 0;
            if (ref.getNumSamples() != a.getNumSamples()) err = 1; else for (int c = 0; c < a.getNumChannels(); ++c) for (int i = 0; i < a.getNumSamples(); ++i) err = std::max(err, (double) std::abs(a.getSample(c, i) - ref.getSample(c, i))); } }
    check(notYet && ready && refused && std::abs(wavSecs - (t1 - t0)) < 0.002 && err < 1e-4, "ARCS: the loop's WAV grip drags out only the loop (rendered in the background, one gesture when ready; refused in the demo)",
          juce::String(wavSecs, 3) + " s (loop " + juce::String(t1 - t0, 3) + " s), largest difference " + juce::String(err, 6));
    // MIDI: only the notes that start in the loop, re-timed to its start
    got.clear(); pg->dragLoopOut(true, hook); juce::MidiFile mf; int inLoop = 0, outside = 0, expect = 0;
    if (got.size() == 1) { juce::FileInputStream is { juce::File(got[0]) }; mf.readFrom(is); }
    const double len = (t1 - t0) * req.bpm / 60.0 * arcs::ArcsHost::kPpq;
    for (int t = 0; t < mf.getNumTracks(); ++t) for (auto* ev : *mf.getTrack(t)) if (ev->message.isNoteOn()) { const double ts = ev->message.getTimeStamp(); if (ts >= -0.5 && ts < len) ++inLoop; else ++outside; }
    { const auto full = p.arcsPage.makeMidi(false, req.bpm); const double k0 = t0 * req.bpm / 60.0 * arcs::ArcsHost::kPpq, k1 = t1 * req.bpm / 60.0 * arcs::ArcsHost::kPpq;
      for (int t = 0; t < full.getNumTracks(); ++t) for (auto* ev : *full.getTrack(t)) if (ev->message.isNoteOn() && ev->message.getTimeStamp() >= k0 && ev->message.getTimeStamp() < k1) ++expect; }
    check(got.size() == 1 && expect >= 1 && inLoop == expect && outside == 0, "ARCS: the loop's MIDI grip = the notes that start in the loop, from its start", juce::String(inLoop) + " notes (expected " + juce::String(expect) + "), " + juce::String(outside) + " outside");
    // moving the loop makes a new WAV (stale until then)
    setLoop(0.5f, 0.75f); pump(40); const bool stale = !pg->loopWavReady(); const bool again = waitFor([&] { return pg->loopWavReady(); }, 30000);
    check(stale && again, "ARCS: moving the loop renders its WAV again (WAV.. until it is ready)");
    p.exportLockForTest = -1; for (auto& f : got) juce::File(f).deleteFile();
    edp.reset(); pump(80);
}
static juce::Component* findRec(juce::Component* c, const juce::String& id);
static void uiTests() {
    std::cout << "UI (events under Xvfb)" << std::endl;
    SpecProcessor p; p.prepareToPlay(48000, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor()); ed->setVisible(true); pump(200);
    auto* panel = dynamic_cast<LabPanel*>(findRec(ed.get(), "spectralLab")); if (!panel) { check(false, "UI: lab panel found"); return; }
    if (auto* b = dynamic_cast<juce::Button*>(findRec(ed.get(), "labButton"))) b->triggerClick();
    pump(100);
    auto* view = findRec(panel, "labView"); if (!view) { check(false, "UI: canvas view found"); return; }
    auto& L = p.lab; const auto x30 = richTone(110.0, 30.0);
    analyse(x30, L.canvas); L.sourceAudio = x30; L.edited(); L.requestPreview(); panel->showPage(0);
    waitFor([&] { auto b = L.latestBank(); return b && b->main && b->main->size() > 1000000 && !L.busy.load(); }, 20000); pump(100);
    auto pr = [&](const char* k) { return panel->probe()[k]; };
    const double T = (int) pr("T"); const float W = (float) view->getWidth(), Hh = (float) view->getHeight();
    // ---- zoom / scroll / fit
    panel->keyPressed(juce::KeyPress('0', {}, '0')); pump(20);
    check((double) pr("vt0") == 0 && std::abs((double) pr("vt1") - T) < 0.6, "UI: 0 fits the whole canvas", juce::String((double) pr("vt0")) + ".." + juce::String((double) pr("vt1")) + " of " + juce::String(T));
    juce::MouseWheelDetails w {}; w.deltaY = 0.5f;
    view->mouseWheelMove(mev(*view, { W * 0.5f, Hh * 0.5f }), w); pump(10);
    const double span1 = (double) pr("vt1") - (double) pr("vt0");
    check(span1 < 0.6 * T && span1 > 0.3 * T, "UI: wheel zooms time around the mouse", juce::String(span1 / T, 2) + " of the canvas shown");
    view->mouseWheelMove(mev(*view, { W * 0.5f, Hh * 0.5f }, juce::ModifierKeys::shiftModifier), w); pump(10);
    check((double) pr("vr1") - (double) pr("vr0") < R - 1.5, "UI: SHIFT + wheel zooms pitch", juce::String((double) pr("vr1") - (double) pr("vr0"), 0) + " rows of " + juce::String(R - 1));
    const double a0 = pr("vt0"); juce::MouseWheelDetails hw {}; hw.deltaX = -0.4f; view->mouseWheelMove(mev(*view, { W * 0.5f, Hh * 0.5f }), hw); pump(10);
    check((double) pr("vt0") > a0 + 1, "UI: sideways wheel scrolls time", juce::String(a0, 0) + " -> " + juce::String((double) pr("vt0"), 0));
    { const double b0 = pr("vt0"); const float aw = W - 10; const float thumbX = (float) ((double) pr("vt0") / T * aw + ((double) pr("vt1") - (double) pr("vt0")) / T * aw * 0.5);
      drag(*view, { thumbX, Hh - 5 }, { thumbX - 120, Hh - 5 }); pump(10);
      check((double) pr("vt0") < b0 - 10, "UI: dragging the time scrollbar scrolls", juce::String(b0, 0) + " -> " + juce::String((double) pr("vt0"), 0)); }
    view->mouseDoubleClick(mev(*view, { W * 0.4f, 6 }, {}, 2)); pump(10);
    check((double) pr("vt0") == 0 && std::abs((double) pr("vt1") - T) < 0.6 && (double) pr("vr1") - (double) pr("vr0") >= R - 1.5, "UI: double-click on the ruler fits time and pitch");
    // ---- scrub on the ruler
    view->mouseDown(mev(*view, { 465, 6 })); const double s1 = L.scrubPos.load();
    const double expect = frameCentre((int) std::lround(465.0 / (W - 10) * T));
    { std::vector<float> lo, ro; playBlocks(p, 30, lo, ro); check(s1 >= 0 && std::abs(s1 - expect) < 2 * HOP && peakOf(lo) > 0.01f, "UI: holding the ruler scrubs from that spot (and you hear it)", "at " + juce::String(s1 / SR, 2) + " s, peak " + juce::String(peakOf(lo), 2)); }
    view->mouseDrag(mev(*view, { 700, 6 }, {}, 1, { 465, 6 })); const double s2 = L.scrubPos.load();
    view->mouseUp(mev(*view, { 700, 6 }, {}, 1, { 465, 6 }));
    check(s2 > s1 + SR && L.scrubPos.load() < 0, "UI: dragging moves the scrub, letting go stops it", juce::String(s2 / SR, 2) + " s");
    // ---- region select + hear
    panel->keyPressed(juce::KeyPress('m', {}, 'm')); pump(5); check((int) pr("tool") == 7, "UI: M = SELECT tool");
    drag(*view, { 300, 120 }, { 520, 330 }); pump(5);
    check((bool) pr("haveSel") && L.regionStart.load() >= 0 && L.regionEnd.load() > L.regionStart.load(), "UI: dragging a box selects a region", juce::String(L.regionStart.load() / SR, 2) + ".." + juce::String(L.regionEnd.load() / SR, 2) + " s");
    panel->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)); check(L.audition.load() == -2, "UI: ENTER hears the region"); L.audition.store(-100);
    // ---- shortcuts
    const juce::String keys = "BESFTXLM"; bool toolsOk = true; for (int i = 0; i < keys.length(); ++i) { panel->keyPressed(juce::KeyPress(keys[i], {}, keys[i])); toolsOk = toolsOk && (int) pr("tool") == i; }
    check(toolsOk, "UI: tool keys B E S F T X L M");
    panel->keyPressed(juce::KeyPress('m', {}, 'm')); drag(*view, { 300, 120 }, { 520, 330 });
    const auto sel = Sel { (int) pr("selT0"), (int) pr("selT1"), (float) pr("selR0"), (float) pr("selR1") };
    const int bk0 = (int) std::ceil(rowHz(sel.r0) / BIN_HZ) + 1, bk1 = (int) std::floor(rowHz(sel.r1) / BIN_HZ) - 1;
    const float bs = pr("brushSize"); panel->keyPressed(juce::KeyPress('[', {}, '[')); const float bs2 = pr("brushSize"); panel->keyPressed(juce::KeyPress(']', {}, ']'));
    check(bs2 < bs && std::abs((float) pr("brushSize") - bs) < 0.01f, "UI: [ and ] change the brush size");
    const bool pl0 = L.previewPlaying.load(); panel->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)); pump(20); const bool pl1 = L.previewPlaying.load(); panel->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)); pump(20);
    check(pl1 != pl0 && L.previewPlaying.load() == pl0, "UI: SPACE starts and stops PLAY");
    auto boxEnergy = [&] { double e = 0; for (int t = sel.t0; t < sel.t1; ++t) for (int k = std::max(1, bk0); k <= std::min(K - 2, bk1); ++k) e += (double) L.canvas.col(t)[k].mag * L.canvas.col(t)[k].mag; return e; };
    const double e0 = boxEnergy(); panel->keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)); pump(5); const double e1 = boxEnergy();
    check(e0 > 0 && e1 < e0 * 1e-6 && (bool) pr("undoEnabled"), "UI: DEL cuts the box");
    panel->keyPressed(juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0)); pump(20);
    check(std::abs(boxEnergy() - e0) < 1e-9 * std::max(1.0, e0) && (bool) pr("redoEnabled"), "UI: CMD+Z brings it back (and REDO lights up)");
    panel->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)); check(!(bool) pr("haveSel") && L.regionStart.load() < 0, "UI: ESC clears the box");
    // ---- a stroke never copies the whole canvas (30 s = 2816 frames, ~55 MB)
    { panel->keyPressed(juce::KeyPress('b', {}, 'b')); L.requestPreview(); waitFor([&] { return !L.busy.load(); });
      double tc = nowS(); { Canvas copy = L.canvas; (void) copy; } tc = secsSince(tc);
      const auto before = L.canvas.amp;
      double ts = nowS(); view->mouseDown(mev(*view, { 400, 200 })); ts = secsSince(ts);
      for (int i = 1; i <= 8; ++i) view->mouseDrag(mev(*view, { 400.0f + i * 10, 200 }, {}, 1, { 400, 200 }));
      view->mouseUp(mev(*view, { 480, 200 }, {}, 1, { 400, 200 }));
      L.requestPreview(); const int copied = L.lastPreviewCopyFrames.load();
      check(ts < 0.25 * tc && copied < T / 5, "UI: a brush stroke copies only the frames it touches", "stroke start " + juce::String(ts * 1000, 2) + " ms vs a full copy " + juce::String(tc * 1000, 1) + " ms; preview sent " + juce::String(copied) + " of " + juce::String((int) T) + " frames");
      panel->keyPressed(juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0)); pump(20); check(L.canvas.amp == before, "UI: CMD+Z undoes the stroke exactly"); }
    // ---- DNA drop zones
    panel->showPage(5); pump(10);
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("labUiTest"); dir.createDirectory();
    const auto wa = dir.getChildFile("A tone.wav"), wb = dir.getChildFile("B tone.wav"), png = dir.getChildFile("pic.png");
    writeWav(wa, sineAt(220, 0.8)); writeWav(wb, sineAt(330, 0.8));
    { juce::Image im(juce::Image::RGB, 64, 32, true); { juce::Graphics g(im); g.fillAll(juce::Colours::black); g.setColour(juce::Colours::white); g.fillRect(0, 10, 64, 6); } juce::PNGImageFormat f; juce::FileOutputStream os(png); f.writeImageToStream(im, os); }
    const int py = 640;
    panel->fileDragEnter({ wa.getFullPathName() }, 300, py); const int z0 = pr("dropZone");
    panel->fileDragMove({ wa.getFullPathName() }, 900, py); const int z1 = pr("dropZone");
    panel->fileDragMove({ png.getFullPathName() }, 300, py); const int z2 = pr("dropZone");
    panel->fileDragMove({ wa.getFullPathName() }, 300, py); panel->fileDragExit({ wa.getFullPathName() }); const int z3 = pr("dropZone");
    check(z0 == 0 && z1 == 1 && z2 == -1 && z3 == -1, "UI: DNA shows the A / B drop zones under the mouse (not for pictures)", juce::String(z0) + " " + juce::String(z1) + " " + juce::String(z2) + " " + juce::String(z3));
    panel->filesDropped({ wa.getFullPathName() }, 300, py); panel->filesDropped({ wb.getFullPathName() }, 900, py);
    const bool got = waitFor([&] { return !L.audioA.empty() && !L.audioB.empty(); }, 10000);
    check(got && L.nameA == "A tone" && L.nameB == "B tone", "UI: a sound dropped on the left half is A, on the right half B");
    panel->showPage(0); panel->filesDropped({ png.getFullPathName() }, 600, 300); pump(20);
    check((int) pr("page") == 1 && L.image.isValid(), "UI: a dropped picture opens IMAGE with the picture on the canvas");
    // ---- what you hear follows the page
    panel->showPage(2); check(L.output.load() == OutGhost && (int) p.labOutputP->load() == OutGhost && pr("hear").toString().startsWith("HEARING: GHOST"), "UI: the GHOST page makes you hear the ghosts");
    panel->showPage(0); dir.deleteRecursively(); ed.reset(); pump(50);
}

static juce::Component* findRec(juce::Component* c, const juce::String& id) {
    if (c->getComponentID() == id) return c;
    for (auto* ch : c->getChildren()) if (auto* r = findRec(ch, id)) return r;
    return nullptr;
}
// ---------------------------------------------------------------- the manual writers' bug list (2026-10-27)
static void bugfixTests() {
    std::cout << "BUG FIXES (manual writers' list)" << std::endl;
    {   // 1. PAINT LOOP REMOVE: the removed sound's notes leave EVERY pattern; the other notes keep their sound in every pattern
        SpecProcessor p; p.prepareToPlay(48000, 512);
        const juce::String names[] = { "Snd A", "Snd B", "Snd C" };
        for (int l = 0; l < 3; ++l) p.addLayer(0, names[l], "soft bright pluck", (uint32_t) (3 + l));
        for (int k = 0; k < 3; ++k) {
            p.switchPattern(k);
            for (int l = 0; l < 3; ++l) for (int j = 0; j < 2; ++j) { sp::Note nt; nt.layer = l; nt.start = j * 1.0 + k * 0.25; nt.len = 0.5; nt.pitch = (float) (50 + l * 10 + k); p.model.notes.push_back(nt); }
            p.publish();
        }
        p.switchPattern(1); p.removeLayer(1); p.model.storeCurrent();
        bool ok = p.model.layers.size() == 2 && p.model.layers[0].name == "Snd A" && p.model.layers[1].name == "Snd C"; int kept = 0;
        for (int k = 0; k < 3; ++k) for (auto& nt : p.model.patterns[(size_t) k].notes) {
            const int orig = (int) std::floor((nt.pitch - 50.0f) / 10.0f);   // the sound the note was drawn with
            ok = ok && orig != 1 && juce::isPositiveAndBelow(nt.layer, (int) p.model.layers.size()) && p.model.layers[(size_t) nt.layer].name == names[orig]; ++kept;
        }
        check(ok && kept == 3 * 4, "PAINT LOOP REMOVE: 3 patterns, remove sound 2 - its notes leave every pattern, every other note keeps its sound", juce::String(kept) + " notes kept");
        p.switchPattern(0); bool ok0 = true; for (auto& nt : p.model.notes) ok0 = ok0 && p.model.layers[(size_t) nt.layer].name == names[(int) std::floor((nt.pitch - 50.0f) / 10.0f)];
        check(ok0 && p.model.notes.size() == 4, "PAINT LOOP REMOVE: switching to another pattern afterwards plays the right sounds");
    }
    {   // 2. a shorter pattern: notes past its end are kept (longer again brings them back) but never play
        auto a = std::make_unique<SyncRig>(true, true), b = std::make_unique<SyncRig>(true, true), c = std::make_unique<SyncRig>(true, true);
        for (auto* r : { a.get(), b.get(), c.get() }) { r->p.sync.setOwnBpm(120); r->p.model.bars = 1; }
        for (auto* r : { b.get(), c.get() }) for (double at : { 4.25, 6.0, 10.0 }) { sp::Note nt; nt.layer = 0; nt.start = at; nt.len = 0.4; nt.pitch = 79; r->p.model.notes.push_back(nt); }
        c->p.model.bars = 4;
        for (auto* r : { a.get(), b.get(), c.get() }) { r->p.publish(); r->p.toggleOwnPlay(); r->run(400); }
        check(a->out == b->out && peakOf(a->out) > 0.02f, "PAINT LOOP: notes past a shortened pattern's end never play (1-bar pattern with hidden notes = the same audio as without them)");
        check(c->out != b->out && b->p.model.notes.size() == 11, "PAINT LOOP: the hidden notes are kept, and making the pattern longer again plays them");
    }
    {   // 5. HOW IT WORKS: the look button has three looks
        juce::String all; for (auto& c : sp::guideCards()) all << c.what << " " << c.steps.joinIntoString(" ") << " " << c.gestures.joinIntoString(" ") << " " << c.out << " ";
        check(!all.contains("DARK / LIGHT") && all.contains("BLACK / PURPLE / LIGHT"), "HOW IT WORKS: the cards name the three looks (BLACK / PURPLE / LIGHT), never DARK / LIGHT");
    }
}

static void bugfixUiTests() {
    std::cout << "BUG FIXES (UI, under Xvfb)" << std::endl;
    {   // 3. LOAD of a canvas saved on the other PAINT page: the tab highlight follows (no loop under the PAINT PADS tab)
        SpecProcessor p; p.prepareToPlay(48000, 512); addLoop(p); p.model.mode = 1;
        const auto name = "bugfix tab test " + juce::String(juce::Time::currentTimeMillis()); check(p.saveCanvas(name), "LOAD test: a PAINT LOOP canvas saved");
        const auto f = p.userDir().getChildFile(name + ".ghostspec");
        p.model.mode = 0; p.model.notes.clear(); p.publish();
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor()); ed->setVisible(true); pump(150);
        auto tabOn = [&](const char* id) { auto* b = dynamic_cast<juce::Button*>(findRec(ed.get(), id)); return b != nullptr && b->getToggleState(); };
        const bool padsFirst = tabOn("tab_pads") && !tabOn("tab_loop");
        const bool loaded = dynamic_cast<SpecEditor*>(ed.get())->loadCanvasFile(f); pump(60);
        check(padsFirst && loaded && p.model.mode == 1 && tabOn("tab_loop") && !tabOn("tab_pads") && !p.model.notes.empty(), "LOAD: a canvas saved on PAINT LOOP, loaded from PAINT PADS, opens on the PAINT LOOP tab (highlight follows)");
        if (auto* b = dynamic_cast<juce::Button*>(findRec(ed.get(), "tab_pads"))) b->triggerClick(); pump(60);
        p.model.mode = 0; check(p.saveCanvas(name + " pads"), "LOAD test: a PAINT PADS canvas saved"); const auto f2 = p.userDir().getChildFile(name + " pads.ghostspec");
        if (auto* b = dynamic_cast<juce::Button*>(findRec(ed.get(), "tab_loop"))) b->triggerClick(); pump(60);
        dynamic_cast<SpecEditor*>(ed.get())->loadCanvasFile(f2); pump(60);
        check(p.model.mode == 0 && tabOn("tab_pads") && !tabOn("tab_loop"), "LOAD: a canvas saved on PAINT PADS, loaded from PAINT LOOP, opens on the PAINT PADS tab");
        f.deleteFile(); f2.deleteFile(); ed.reset(); pump(50);
    }
    {   // 4. TAP: the tooltip says what the code does (2 taps set the tempo)
        SpecProcessor p; p.prepareToPlay(48000, 512); std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor()); ed->setVisible(true); pump(100);
        auto* tap = dynamic_cast<juce::Button*>(findRec(ed.get(), "tapButton")); const double b0 = p.sync.getOwnBpm();
        if (tap) { tap->triggerClick(); pump(10); juce::Thread::sleep(400); tap->triggerClick(); } pump(20);
        check(tap && tap->getTooltip().contains("tap 2 or more times") && !tap->getTooltip().contains("3 or more") && std::abs(p.sync.getOwnBpm() - 150.0) < 15.0,
              "TAP: 2 taps set the own tempo and the tooltip says 'tap 2 or more times'", "tempo " + juce::String(b0, 1) + " -> " + juce::String(p.sync.getOwnBpm(), 1));
        ed.reset(); pump(50);
    }
    {   // 7. ARRANGE RE-RENDER: off (greyed, the tooltip says why) on a bounced clip; on for a clip whose page can render it again
        SpecProcessor p; p.prepareToPlay(48000, 512);
        juce::AudioBuffer<float> tone(2, 48000); for (int i = 0; i < 48000; ++i) { const float v = 0.3f * (float) std::sin(2 * juce::MathConstants<double>::pi * 220 * i / 48000.0); tone.setSample(0, i, v); tone.setSample(1, i, v); }
        const auto a = p.arrange.receive(arr::makePayload(tone, 48000, "Lab tone", {}, { "LAB", "Lab tone", {} }), 0, 0);
        const auto b = p.arrange.bounce({ a }, false); p.arrange.waitUntilUpToDate();
        const bool engineOk = p.arrange.canRerender(a) && !p.arrange.canRerender(b);
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor()); ed->setVisible(true); pump(120);
        if (auto* t = dynamic_cast<juce::Button*>(findRec(ed.get(), "tab_arrange"))) t->triggerClick(); pump(150);
        auto* pg = dynamic_cast<arr::ArrangePage*>(findRec(ed.get(), "arrangePage"));
        std::function<juce::TextButton*(juce::Component*, const juce::String&)> byText = [&](juce::Component* c, const juce::String& t) -> juce::TextButton* {
            if (auto* x = dynamic_cast<juce::TextButton*>(c)) if (x->getButtonText() == t) return x;
            for (auto* ch : c->getChildren()) if (auto* r = byText(ch, t)) return r; return nullptr; };
        bool bouncedOff = false, sourceOn = false; juce::String tip;
        if (pg) {
            p.arrange.selected = b; p.arrange.selection = { b }; pg->refresh(); pump(60);
            if (auto* rb = byText(pg, "RE-RENDER")) { bouncedOff = rb->isVisible() && !rb->isEnabled(); tip = rb->getTooltip(); }
            p.arrange.selected = a; p.arrange.selection = { a }; pg->refresh(); pump(60);
            if (auto* rb = byText(pg, "RE-RENDER")) sourceOn = rb->isVisible() && rb->isEnabled();
        }
        check(engineOk && bouncedOff && tip.contains("bounced") && sourceOn, "ARRANGE: RE-RENDER is greyed on a bounced clip (tooltip says why) and on for a Lab clip", tip);
        ed.reset(); pump(50);
    }
    {   // 8. ARRANGE ruler: right-click moves the playhead (it doesn't start playback) - the texts say so
        const juce::String hint = arr::ArrangePage::rulerHint();
        juce::GlyphArrangement ga; ga.addLineOfText(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)), hint, 0, 0); const float w = ga.getBoundingBox(0, -1, true).getWidth();
        juce::String card; for (auto& c : sp::guideCards()) if (c.id == "arrange") card = c.gestures.joinIntoString(" ");
        check(hint.contains("move the playhead here") && !hint.contains("play from here") && w <= 404.0f && card.contains("right-click = move the playhead here") && !card.contains("play from here"),
              "ARRANGE ruler: the hint and the HOW IT WORKS card say 'move the playhead here' (the hint fits its 408 px box)", "hint " + juce::String(w, 1) + " px");
    }
    {   // 9. ARRANGE MIDI chip: VOICE clips carry no notes, so the texts don't name VOICE as a source of MIDI
        const auto a = arr::ArrangePage::midiChipHelp(), b = arr::ArrangePage::noMidiClipsText();
        check(a.contains("ARCS") && b.contains("ARCS") && !a.contains("(ARCS, VOICE") && !b.contains("VOICE") && a.contains("VOICE clips and plain audio carry no notes"),
              "ARRANGE MIDI chip: the messages name ARCS as a source of notes and say VOICE clips carry none");
    }
#if GHOSTY_VOICE
    {   // 10. VOICE: the selection bar's X (clear the selection) has a real size, inside the page, over nothing else
        SpecProcessor p; p.prepareToPlay(48000, 512); std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor()); ed->setVisible(true); pump(120);
        if (auto* t = dynamic_cast<juce::Button*>(findRec(ed.get(), "tab_voice"))) t->triggerClick(); pump(150);
        auto* tab = findRec(ed.get(), "voiceTab"); juce::TextButton* x = nullptr; juce::Component* vp = nullptr;
        std::function<void(juce::Component*)> walk = [&](juce::Component* c) { if (auto* b = dynamic_cast<juce::TextButton*>(c)) if (b->getButtonText() == "X" && b->getTooltip().startsWith("Clear the selection")) { x = b; vp = c->getParentComponent(); } for (auto* ch : c->getChildren()) walk(ch); };
        if (tab) walk(tab);
        bool inside = false, overNothing = true; juce::Rectangle<int> xb; int grabW = -1, regionX = -1;
        if (x && vp) { xb = x->getBounds(); inside = vp->getLocalBounds().contains(xb);
            for (auto* c : vp->getChildren()) if (c != x && c->isVisible() && dynamic_cast<juce::Button*>(c) && c->getBounds().intersects(xb)) overNothing = false;
            for (auto* c : vp->getChildren()) if (auto* b = dynamic_cast<juce::TextButton*>(c)) { if (b->getButtonText() == "GRAB VOWEL") grabW = b->getWidth(); if (b->getButtonText() == "PROCESS ONLY THIS") regionX = b->getX(); } }
        check(x && xb.getWidth() >= 22 && xb.getHeight() >= 20 && inside && overNothing && grabW > 40 && regionX > xb.getRight(),
              "VOICE: the selection bar's X has a real size (" + juce::String(xb.getWidth()) + " x " + juce::String(xb.getHeight()) + "), inside the page, over no other button; the other buttons keep their places");
        ed.reset(); pump(50);
    }
#endif
    {   // 11. no third-party library names in the UI text (tooltips, button texts, HOW IT WORKS); the licence credits stay in their own panel / file
        SpecProcessor p; p.prepareToPlay(48000, 512);
        juce::AudioBuffer<float> tone(2, 48000); tone.clear(); const auto id = p.arrange.receive(arr::makePayload(tone, 48000, "Clip"), 0, 0);
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor()); ed->setVisible(true); pump(120);
        const char* names[] = { "Signalsmith", "signalsmith", "JUCE", "r8brain", "Airwindows", "Rubber Band", "kissfft", "pffft", "WORLD v", "Morise" };
        juce::StringArray hits; int seen = 0;
        std::function<void(juce::Component*)> walk = [&](juce::Component* c) {
            if (!c->isVisible()) return;
            juce::String t; if (auto* tc = dynamic_cast<juce::SettableTooltipClient*>(c)) t << tc->getTooltip() << " "; if (auto* b = dynamic_cast<juce::Button*>(c)) t << b->getButtonText();
            if (t.isNotEmpty()) { ++seen; for (auto* n : names) if (t.contains(n)) hits.add(c->getComponentID() + ": " + t.substring(0, 80)); }
            for (auto* ch : c->getChildren()) walk(ch); };
        for (auto& d : sp::tabs()) { if (auto* b = dynamic_cast<juce::Button*>(findRec(ed.get(), d.id))) b->triggerClick(); pump(150);
            if (auto* pg = dynamic_cast<arr::ArrangePage*>(findRec(ed.get(), "arrangePage"))) if (pg->isVisible()) { p.arrange.selected = id; p.arrange.selection = { id }; pg->refresh(); pump(40); }
            walk(ed.get()); }
        for (auto& c : sp::guideCards()) { const juce::String t = c.what + c.steps.joinIntoString(" ") + c.gestures.joinIntoString(" ") + c.out; for (auto* n : names) if (t.contains(n)) hits.add("card " + c.id); }
        check(hits.isEmpty() && seen > 100, "UI text: no third-party library names in tooltips / buttons / HOW IT WORKS on any tab (" + juce::String(seen) + " texts checked)", hits.joinIntoString(" | "));
        ed.reset(); pump(50);
    }
#if GHOSTY_PROCESS_LAB
    {   // 6. PROCESS: PER SLICE does nothing in Spectrum (the Lab's sound has no slices): hidden, its place stays empty (nothing moves)
        SpecProcessor p; p.prepareToPlay(48000, 512); auto& L = p.lab;
        std::vector<float> x((size_t) SR); for (size_t i = 0; i < x.size(); ++i) x[i] = (float) (0.3 * std::sin(2 * juce::MathConstants<double>::pi * 330 * i / SR));
        analyse(x, L.canvas); L.sourceAudio = x; L.sourceName = "tone"; L.edited(); L.requestPreview();
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor()); ed->setVisible(true); pump(120);
        if (auto* b = dynamic_cast<juce::Button*>(findRec(ed.get(), "tab_process"))) b->triggerClick(); pump(200);
        auto* page = dynamic_cast<LabProcessPage*>(findRec(ed.get(), "processPage"));
        juce::Button* slice = nullptr; juce::Rectangle<int> sb; bool overlap = false, othersShown = false;
        if (page) {
            auto& pn = page->panel(); pump(100);
            for (auto* c : pn.getChildren()) if (auto* b = dynamic_cast<juce::TextButton*>(c)) if (b->getButtonText() == "PER SLICE") slice = b;
            if (slice) { sb = slice->getBounds();
                for (auto* c : pn.getChildren()) if (c != slice && c->isVisible() && c->getBounds().intersects(sb)) overlap = true;
                for (auto* c : pn.getChildren()) if (auto* b = dynamic_cast<juce::TextButton*>(c)) if ((b->getButtonText() == "AGAIN" || b->getButtonText() == "APPLY") && b->isVisible()) othersShown = true; }
        }
        check(page && slice && !slice->isVisible() && !sb.isEmpty() && !overlap && othersShown, "PROCESS (Spectrum): PER SLICE is hidden, its place stays empty, AGAIN / APPLY stay where they were");
        ed.reset(); pump(50);
    }
#endif
}

static void shots(const juce::File& dir) {
    dir.createDirectory();
    auto p = std::make_unique<SpecProcessor>(); p->prepareToPlay(48000, 512);
    auto snap = [&](juce::Component& ed, const juce::String& name) {
        for (int i = 0; i < 30; ++i) { juce::MessageManager::getInstance()->runDispatchLoopUntil(40); if (!p->lab.busy.load()) break; }
        juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
        auto img = ed.createComponentSnapshot(ed.getLocalBounds(), true, 2.0f);   // 2x: thin glyphs ('-', '1') reach their full colour juce::PNGImageFormat png; auto f = dir.getChildFile(name); f.deleteFile(); juce::FileOutputStream os(f); png.writeImageToStream(img, os);
        std::cout << "  shot " << name << std::endl;
    };
    std::unique_ptr<juce::AudioProcessorEditor> ed(p->createEditor()); ed->setVisible(true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(200);
    snap(*ed, "0 pads tab (unchanged).png");
    auto* panel = dynamic_cast<LabPanel*>(findRec(ed.get(), "spectralLab")); if (!panel) { std::cout << "  no lab panel found" << std::endl; return; }
    auto* labBtn = dynamic_cast<juce::Button*>(findRec(ed.get(), "labButton")); if (labBtn) labBtn->triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(200);
    { // paint page: an imported test sound plus painted lines
        auto& L = p->lab; analyse(testSignal(3.0), L.canvas); Brush b; b.size = 1; b.level = 0.4f;
        for (int t = 150; t < 260; ++t) { paintAt(L.canvas, (float) t, 0, b, midiHz(76.0f + (float) ((t / 20) % 3) * 2)); paintAt(L.canvas, (float) t, 0, b, midiHz(64)); }
        L.requestPreview(); panel->showPage(0); panel->selectTool(0); snap(*ed, "1 lab paint.png");
    }
    { auto& L = p->lab; L.imageMap.seconds = 6; L.imageMap.loHz = 120; L.imageMap.hiHz = 9000; L.setImage(ghostFace(512, 512)); panel->showPage(1); snap(*ed, "2 lab image ghost face.png"); }
    { auto& L = p->lab; analyse(synthSnare(), L.canvas); L.ghost.count = 12; L.ghost.evolve = 0.8f; L.output = OutGhost; L.requestGhost(); panel->showPage(2); snap(*ed, "3 lab ghost.png"); }
    { auto& L = p->lab; L.output = OutSplit; L.split.bands = 4; const char* pats[] = { "1000100010001010", "0010001000100011", "1011011010110110", "1111111111111111" };
      for (int i = 0; i < 4; ++i) for (int k = 0; k < 16; ++k) L.split.seq[(size_t) i].steps[(size_t) k] = pats[i][k] == '1';
      L.split.seq[3].retrig = true; L.split.seq[3].rateDiv = 32; L.requestSplit(); panel->showPage(3); snap(*ed, "4 lab split.png"); }
    { auto& L = p->lab; L.canvas.resize(framesFor(4.0)); L.chaos.seed = 3; L.chaos.density = 0.6f; Sel lk; lk.t0 = 40; lk.t1 = 90; lk.r0 = 150; lk.r1 = 230; L.locks = { lk }; L.generateChaos(); L.output = OutCanvas; panel->showPage(4); snap(*ed, "5 lab chaos.png"); }
    { auto& L = p->lab; std::vector<float> a((size_t) SR * 2), b2 = synthSnare(); for (size_t i = 0; i < a.size(); ++i) a[i] = 0.3f * (float) (std::sin(2 * juce::MathConstants<double>::pi * 220 * i / SR) + 0.4 * std::sin(2 * juce::MathConstants<double>::pi * 660 * i / SR));
      L.audioA = a; L.nameA = "Tone"; L.audioB = b2; L.nameB = "Snare"; L.requestMorph(); L.output = OutMorph; panel->showPage(5);
      for (int i = 0; i < 100 && L.busy.load(); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
      L.locks.clear(); { std::shared_ptr<MorphPrep> mp = L.getMorphPrep(); if (mp) { morph(*mp, 0.5f, L.canvas); L.requestPreview(); } }
      snap(*ed, "6 lab morph.png"); }
    { // the new tabs, light + dark, with something on them
        juce::AudioBuffer<float> tone(2, 96000); for (int i = 0; i < 96000; ++i) { const float v = 0.3f * (float) std::sin(2 * juce::MathConstants<double>::pi * 220 * i / 48000.0) * (float) std::exp(-i / 30000.0); tone.setSample(0, i, v); tone.setSample(1, i, v); }
        p->arrange.receive(arr::makePayload(tone, 48000, "Lab Canvas"), 0, 0); p->arrange.receive(arr::makePayload(tone, 48000, "Ghost set"), 1, 4); p->arrange.receive(arr::makePayload(tone, 48000, "Split loop"), 2, 8);
        auto click = [&](const char* id) { if (auto* b = dynamic_cast<juce::Button*>(findRec(ed.get(), id))) b->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(150); };
        click("tab_arrange");
        if (auto* pg = dynamic_cast<arr::ArrangePage*>(findRec(ed.get(), "arrangePage"))) { pg->refresh(); pg->zoomToFit(); pg->setDark(false); snap(*ed, "7 arrange light.png"); pg->setDark(true); snap(*ed, "8 arrange dark.png"); }
        p->arcsPage.edit() = arcs::factory::pages()[2].make(); p->arcsPage.commit();
        click("tab_arcs");
        if (auto* pg = dynamic_cast<arcs::ArcsPage*>(findRec(ed.get(), "arcsPage"))) { pg->refresh(); pg->fitView(); pg->setDark(false); snap(*ed, "9 arcs light.png"); pg->setDark(true); snap(*ed, "10 arcs dark.png"); }
        p->bypassParam->setValueNotifyingHost(1.0f); juce::MessageManager::getInstance()->runDispatchLoopUntil(100); snap(*ed, "11 bypass engaged.png"); p->bypassParam->setValueNotifyingHost(0.0f);
        click("tab_pads");
    }
    ed.reset();
}

#include "DemoGateTests.inc"   // `demogate`: the demo export gate on every export path (docs/DEMO-EXPORT-GATE.md)

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::String mode = argc > 1 ? argv[1] : "";
    if (mode == "demogate") { const int f = demoGateTests(); return (f + saveGateTests()) == 0 ? 0 : 1; }
    if (mode == "savegate") return saveGateTests() == 0 ? 0 : 1;
    if (mode == "demos" && argc > 2) { demos(juce::File(argv[2])); return 0; }
    if (mode == "shots" && argc > 2) { shots(juce::File(argv[2])); return 0; }
    if (mode == "fixshots" && argc > 2) { fixShots(juce::File(argv[2])); syncShots(juce::File(argv[2])); guideTests(juce::File(argv[2])); return 0; }
    if (mode == "arcsloop") { arcsLoopGripTests(); std::cout << passes << " passed, " << fails << " failed" << std::endl; return fails; }
    if (mode == "looks") { houseLookTests(argc > 2 ? juce::File(argv[2]) : juce::File()); std::cout << passes << " passed, " << fails << " failed" << std::endl; return fails; }
    if (mode == "guide") { guideTests(argc > 2 ? juce::File(argv[2]) : juce::File()); std::cout << passes << " passed, " << fails << " failed" << std::endl; return fails; }
    if (mode == "wave") { waveRegionTests(); std::cout << passes << " passed, " << fails << " failed" << std::endl; return fails; }
    if (mode == "polish") { polishTests(); std::cout << passes << " passed, " << fails << " failed" << std::endl; return fails; }
    if (mode == "pad") { playModePadTests(); std::cout << passes << " passed, " << fails << " failed" << std::endl; return fails; }
    if (mode == "sync") { syncTests(); syncUiTests(); std::cout << passes << " passed, " << fails << " failed" << std::endl; return fails; }
    if (mode == "bugfix") { bugfixTests(); if (juce::SystemStats::getEnvironmentVariable("DISPLAY", {}).isNotEmpty()) bugfixUiTests(); std::cout << passes << " passed, " << fails << " failed" << std::endl; return fails; }
    if (mode == "voice") { voiceTabTests(); std::cout << passes << " passed, " << fails << " failed" << std::endl; return fails; }
    engineTests(); pluginTests(); punchTests();
    pagesAndBypassTests(); playModeTests(); playModePadTests(); syncTests(); bugfixTests();
    if (mode == "ui" || juce::SystemStats::getEnvironmentVariable("DISPLAY", {}).isNotEmpty()) { uiTests(); pagesUiTests(); labLookTests(); processPageTests(); voiceTabTests(); syncUiTests(); polishTests(); waveRegionTests(); guideTests(); houseLookTests(); arcsLoopGripTests(); bugfixUiTests(); }
    std::cout << passes << " passed, " << fails << " failed" << std::endl;
    return fails;
}
