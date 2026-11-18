// Poltergeist VOCAL CHOP test harness.
//   PoltergeistTests            -> runs every check, prints numbers, exit code = failures
//   PoltergeistTests shots DIR  -> PNG screenshots of the FM page's ADD SAMPLE slot, all three themes (needs a display / Xvfb)
//   PoltergeistTests demo FILE  -> 15 s WAV of a chopped vocal-style melody
//   PoltergeistTests demogate   -> the demo export gate: every audio / MIDI export path refused locked, working unlocked
//   PoltergeistTests savegate   -> demo saves (project, user sound, snapshot, board) leave out audio Poltergeist made
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <atomic>
#include <cstdio>
#include <new>

// ---- allocation counter (real-time check: processBlock must not allocate)
static std::atomic<long> gAllocs { 0 };
static thread_local bool gCount = false;
void* operator new(std::size_t n) { if (gCount) ++gAllocs; if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n) { if (gCount) ++gAllocs; if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

static int failures = 0;
static void check(bool ok, const juce::String& what) { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8()); if (!ok) ++failures; }

// ---- a synthetic "vocal": harmonic tone -> 3 formant resonators, syllables with gaps
struct Biquad { double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void bp(double f, double q, double fs) { const double w = 2 * juce::MathConstants<double>::pi * f / fs, al = std::sin(w) / (2 * q), a0 = 1 + al;
        b0 = al / a0; b1 = 0; b2 = -al / a0; a1 = -2 * std::cos(w) / a0; a2 = (1 - al) / a0; }
    double tick(double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; } };

static std::vector<float> makeVocal(double fs, const std::vector<double>& midi, std::vector<int>& onsets, double lead = 0.15, double syl = 0.32, double gap = 0.11) {
    const int n = (int) ((lead + midi.size() * (syl + gap) + 0.2) * fs);
    std::vector<float> y((size_t) n, 0.0f);
    Biquad F[3]; const double fr[3] = { 800, 1150, 2500 }, fg[3] = { 1.0, 0.35, 0.12 };
    for (int k = 0; k < 3; ++k) F[k].bp(fr[k], 6.0, fs);
    double ph = 0; onsets.clear();
    for (size_t s = 0; s < midi.size(); ++s) {
        const int a = (int) ((lead + s * (syl + gap)) * fs), len = (int) (syl * fs); onsets.push_back(a);
        const double f0 = 440.0 * std::pow(2.0, (midi[s] - 69) / 12.0);
        for (int i = 0; i < len; ++i) {
            const double t = i / fs; ph += f0 * (1 + 0.002 * std::sin(2 * juce::MathConstants<double>::pi * 5.0 * t)) / fs;
            double src = 0; for (int h = 1; h * f0 < 7000; ++h) src += std::sin(2 * juce::MathConstants<double>::pi * h * ph) / h;
            const double env = juce::jmin(1.0, t / 0.012) * juce::jmin(1.0, (syl - t) / 0.04);
            double o = 0; for (int k = 0; k < 3; ++k) o += F[k].tick(src * env) * fg[k];
            y[(size_t) (a + i)] = (float) o;
        }
    }
    float pk = 1e-6f; for (float v : y) pk = juce::jmax(pk, std::abs(v));
    for (float& v : y) v *= 0.9f / pk;
    return y;
}
static std::shared_ptr<gs::SampleData> asSample(const std::vector<float>& v, double fs, const char* name) {
    auto s = std::make_shared<gs::SampleData>(); s->data = v; s->rate = fs; s->name = name; s->buildFrames(); return s;
}
// spectral envelope peak (Hz): average magnitude spectrum, smoothed over +-450 Hz so single harmonics vanish
static double envelopePeak(const float* x, int n, double fs) {
    constexpr int order = 12, N = 1 << order; juce::dsp::FFT fft(order);
    std::vector<double> acc(N / 2 + 1, 0.0); std::vector<float> buf(2 * N);
    for (int a = 0; a + N <= n; a += N / 4) {
        std::fill(buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < N; ++i) buf[(size_t) i] = x[a + i] * (0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * i / N));
        fft.performFrequencyOnlyForwardTransform(buf.data(), true);
        for (int b = 0; b <= N / 2; ++b) acc[(size_t) b] += buf[(size_t) b];
    }
    const int w = (int) (450.0 / (fs / N)); double best = 0, bf = 0;
    for (int b = (int) (300 / (fs / N)); b < (int) (3500 / (fs / N)); ++b) { double s = 0; for (int k = -w; k <= w; ++k) s += acc[(size_t) juce::jlimit(0, N / 2, b + k)]; if (s > best) { best = s; bf = b * fs / N; } }
    return bf;
}
static float maxJump(const std::vector<float>& y, int a, int b) { float m = 0; for (int i = juce::jmax(1, a); i < juce::jmin((int) y.size(), b); ++i) m = juce::jmax(m, std::abs(y[(size_t) i] - y[(size_t) i - 1])); return m; }
static juce::String xmlOf(const juce::ValueTree& t) { return t.toXmlString(); }

static void testOnsets() {
    const double fs = 44100; std::vector<int> on;
    auto v = makeVocal(fs, { 57, 60, 55, 62, 58 }, on);
    for (int mode : { 0, 2 }) {
        gs::ChopState c; c.audio = asSample(v, fs, "vocal"); c.slicesMode = mode; c.autoChop();
        double worst = 0; const bool five = c.count() == 5;
        if (five) for (int i = 0; i < 5; ++i) worst = juce::jmax(worst, std::abs(c.slices[(size_t) i].start - on[(size_t) i]) / fs * 1000.0);
        juce::String L; for (auto& sl : c.slices) L << juce::String(sl.start / fs * 1000.0, 1) << " "; juce::String T; for (int o : on) T << juce::String(o / fs * 1000.0, 1) << " ";
        std::printf("    found %s| true %s\n", L.toRawUTF8(), T.toRawUTF8());
        check(five && worst <= 15.0, juce::String("AUTO CHOP (") + (mode == 0 ? "AUTO" : "5") + "): " + juce::String(c.count()) + " slices, worst onset error " + juce::String(worst, 2) + " ms (limit 15)");
    }
    {   // legato: 35 ms dips between syllables, plus a noise floor at -40 dB
        std::vector<int> on2; auto w = makeVocal(fs, { 57, 60, 55, 62, 58 }, on2, 0.15, 0.30, 0.035);
        juce::Random r(3); for (auto& x : w) x += (r.nextFloat() * 2 - 1) * 0.009f;
        gs::ChopState c; c.audio = asSample(w, fs, "legato"); c.slicesMode = 0; c.autoChop();
        double worst = 0; const bool five = c.count() == 5;
        if (five) for (int i = 0; i < 5; ++i) worst = juce::jmax(worst, std::abs(c.slices[(size_t) i].start - on2[(size_t) i]) / fs * 1000.0);
        check(five && worst <= 15.0, "AUTO CHOP legato + noise: " + juce::String(c.count()) + " slices, worst " + juce::String(worst, 2) + " ms");
    }
    // the same vocal much quieter still finds 5
    auto q = v; for (auto& x : q) x *= 0.05f;
    gs::ChopState c; c.audio = asSample(q, fs, "quiet"); c.slicesMode = 0; c.autoChop();
    check(c.count() == 5, "AUTO CHOP on a -26 dB copy: " + juce::String(c.count()) + " slices");
}

static void testSlicePitch() {
    const double fs = 44100; std::vector<int> on;
    auto v = makeVocal(fs, { 57, 60, 55, 62, 58 }, on);
    gs::ChopState c; c.audio = asSample(v, fs, "vocal"); c.slicesMode = 2; c.autoChop();
    if (c.count() != 5) { check(false, "slice pitch: chop failed"); return; }
    const int i = 2; const int a = c.slices[(size_t) i].start, b = c.endOf(i);
    const float before = gs::yinMidi(v.data() + a, b - a, fs);
    gs::ChopSlice s = c.slices[(size_t) i]; s.pitch = 7; s.formant = true;
    auto keep = gs::ChopState::renderSlice(*c.audio, a, b, s, 0.0f);
    s.formant = false; auto chip = gs::ChopState::renderSlice(*c.audio, a, b, s, 0.0f);
    const float after = gs::yinMidi(keep.data(), (int) keep.size(), fs);
    const double cents = (after - before - 7.0) * 100.0;
    check(std::abs(cents) <= 15.0, "slice +7: detected " + juce::String(before, 2) + " -> " + juce::String(after, 2) + " (error " + juce::String(cents, 1) + " cents, limit 15)");
    check((int) keep.size() == b - a, "slice +7: duration unchanged (" + juce::String((int) keep.size()) + " vs " + juce::String(b - a) + " samples)");
    const double e0 = envelopePeak(v.data() + a, b - a, fs), e1 = envelopePeak(keep.data(), (int) keep.size(), fs), e2 = envelopePeak(chip.data(), (int) chip.size(), fs);
    check(std::abs(e1 / e0 - 1.0) <= 0.10, "FORMANT KEEP: envelope peak " + juce::String(e0, 0) + " Hz -> " + juce::String(e1, 0) + " Hz (" + juce::String((e1 / e0 - 1) * 100, 1) + " %, limit 10 %); CHIPMUNK moves it to " + juce::String(e2, 0) + " Hz (" + juce::String((e2 / e0 - 1) * 100, 1) + " %)");
    bool finite = true; for (float x : keep) finite = finite && std::isfinite(x);
    check(finite, "slice render: no NaN / inf");
    // a very short slice (25 ms) still renders (padded stretcher)
    gs::ChopSlice t; t.pitch = 5; auto shortR = gs::ChopState::renderSlice(*c.audio, a, a + (int) (0.025 * fs), t, 0.0f);
    float pk = 0; for (float x : shortR) pk = juce::jmax(pk, std::abs(x));
    check(pk > 0.05f, "25 ms slice pitched +5 renders (peak " + juce::String(pk, 3) + ")");
    gs::ChopSlice cr; cr.pitch = 3; auto crunch = gs::ChopState::renderSlice(*c.audio, a, b, cr, 0.5f);
    const float cm = gs::yinMidi(crunch.data(), (int) crunch.size(), fs);
    check(std::abs(cm - before - 3) < 0.6f, "CRUNCH 0.5, +3 st: still pitched right (" + juce::String(cm - before, 2) + " st)");
}

static void testLabSection() {
    const double fs = gs::Lab::kRate;
    std::vector<int> on; auto v = makeVocal(fs, { 57 }, on, 0.0, 3.0, 0.0);
    gs::Lab lab; lab.bpm = 120; const int src = lab.addSource(asSample(v, fs, "tone"));
    const int id = lab.addClip(src, 0.0, 3.0, 0.0); auto* c = lab.clip(id);
    for (int pass = 0; pass < 2; ++pass) {
        c->marks = { { 1.0, pass ? 1.3 : 1.0 }, { 2.0, 2.0 } };
        *lab.sectionPitch(*c, 1) = 5.0f;
        auto y = lab.renderClip(*c);
        const double b1 = pass ? 1.3 : 1.0, b2 = 2.0;
        auto pitchIn = [&](double t0, double t1) { const int a = (int) (t0 * fs), b = (int) (t1 * fs); return gs::yinMidi(y.data() + a, b - a, fs); };
        const float p0 = pitchIn(0.15, b1 - 0.15), p1 = pitchIn(b1 + 0.15, b2 - 0.15), p2 = pitchIn(b2 + 0.15, 2.85);
        const bool ok = std::abs(p0 - 57) < 0.15f && std::abs(p1 - 62) < 0.15f && std::abs(p2 - 57) < 0.15f;
        check(ok, juce::String(pass ? "Lab warp (retimed 1.0->1.3 s) " : "Lab warp ") + "section +5: before " + juce::String(p0 - 57, 2) + ", section " + juce::String(p1 - 57, 2) + ", after " + juce::String(p2 - 57, 2) + " st");
        const float ref = juce::jmax(maxJump(y, (int) (0.3 * fs), (int) ((b1 - 0.1) * fs)), maxJump(y, (int) ((b1 + 0.1) * fs), (int) ((b2 - 0.1) * fs)));
        const float j1 = maxJump(y, (int) ((b1 - 0.08) * fs), (int) ((b1 + 0.08) * fs)), j2 = maxJump(y, (int) ((b2 - 0.08) * fs), (int) ((b2 + 0.08) * fs));
        check(j1 <= ref * 1.25f && j2 <= ref * 1.25f, "  no clicks at the section edges: max sample jump " + juce::String(j1, 4) + " / " + juce::String(j2, 4) + " vs steady " + juce::String(ref, 4) + " (limit x1.25)");
        check((int) y.size() == (int) std::round(c->len * lab.beatSec() * fs), "  length = clip length (" + juce::String((int) y.size()) + ")");
    }
}

static void testTune() {
    const double fs = 44100; std::vector<int> on;
    const std::vector<double> off { 57.4, 59.3, 61.6, 64.45, 66.3 };
    auto v = makeVocal(fs, off, on);
    gs::ChopState c; c.audio = asSample(v, fs, "detuned"); c.slicesMode = 2; c.key = 0; c.scale = 1; c.autoChop();
    const int n = c.tuneToKey();
    gs::ChopState::Cache cache; auto kit = c.buildKit(cache);
    const auto sc = gs::MidiFx::scale(1); double worst = 0; juce::String list;
    for (int i = 0; i < kit->n; ++i) {
        const float m = gs::yinMidi(kit->slot[i].d, kit->slot[i].len, fs); const float tgt = gs::ChopState::nearestInScale(m, 0, sc);
        worst = juce::jmax(worst, (double) std::abs(m - tgt) * 100); list << juce::String(c.slices[(size_t) i].det, 2) << "->" << juce::String(m, 2) << " ";
    }
    check(n == 5 && worst <= 15, "TUNE TO KEY (C minor): " + list + " worst " + juce::String(worst, 1) + " cents off the scale (limit 15)");
}

static void testProcessor() {
    const double fs = 44100; std::vector<int> on;
    auto v = makeVocal(fs, { 57, 60, 55, 62, 58 }, on);
    PoltergeistProcessor p1;
    p1.chopLoad(asSample(v, fs, "vocal"));
    p1.chopBegin(); p1.chop.slices[2].pitch = 7; p1.chop.slices[1].reverse = true; p1.chop.slices[3].formant = false; p1.chop.crunch = 0.2f;
    p1.chop.makeMelody(); p1.chopRenderNow();
    juce::MemoryBlock mb; p1.getStateInformation(mb);
    PoltergeistProcessor p2; p2.restoreState(mb.getData(), (int) mb.getSize()); p2.chopRenderNow();
    const auto a = xmlOf(p1.chop.toTree(true)), b = xmlOf(p2.chop.toTree(true));
    check(a == b, "state round trip: chop tree identical (" + juce::String(a.length()) + " chars)");
    juce::MemoryBlock mb2; p2.getStateInformation(mb2);
    check(mb == mb2, "state round trip: saved state byte-identical (" + juce::String((int) mb.getSize()) + " bytes)");

    // undo / redo: 35 edits, back and forth, byte-identical
    PoltergeistProcessor& p = p1; std::vector<juce::String> st { xmlOf(p.chop.toTree(false)) };
    juce::Random r(7);
    for (int e = 0; e < 35; ++e) {
        p.chopBegin();
        switch (e % 5) {
            case 0: p.chop.slices[(size_t) r.nextInt(p.chop.count())].pitch = (float) r.nextInt(25) - 12; break;
            case 1: p.chop.addLine(r.nextInt(p.chop.size())); break;
            case 2: if (p.chop.count() > 2) p.chop.deleteLine(1 + r.nextInt(p.chop.count() - 1)); break;
            case 3: p.chop.moveLine(1, p.chop.slices[1].start + 300); p.chop.slices[0].gainDb = -3; break;
            default: p.chop.makeMelody(); p.chop.crunch = r.nextFloat(); break;
        }
        st.push_back(xmlOf(p.chop.toTree(false)));
    }
    bool ok = true; int depth = 0;
    for (int e = 35; e > 0; --e) { ok = ok && p.chopUndo(); ok = ok && xmlOf(p.chop.toTree(false)) == st[(size_t) e - 1]; if (ok) ++depth; }
    for (int e = 1; e <= 35; ++e) { ok = ok && p.chopRedo(); ok = ok && xmlOf(p.chop.toTree(false)) == st[(size_t) e]; }
    check(ok, "UNDO / REDO: " + juce::String(depth) + " steps back and all the way forward, byte-identical");
    p.chopBegin(); p.chop.slices[0].pitch = 3; const auto pre = xmlOf(p.chop.toTree(false));
    p.chopRenderNow(); const int lenBefore = p.chop.size() - p.chop.slices[0].start; p.chopBounce(); p.chopRenderNow();
    check(p.chop.size() == lenBefore && p.chop.slices[0].start == 0 && p.chop.slices[0].pitch == 0, "BOUNCE: new sample " + juce::String(p.chop.size()) + " samples, lines kept (" + juce::String(p.chop.count()) + " slices), pitches printed");
    p.chopUndo(); check(xmlOf(p.chop.toTree(false)) == pre, "BOUNCE undo restores the sample and pitches");

    // audio: CHOP mode playing notes + CHOP MELODY, 10 s at 48 kHz; no allocations, no NaN
    PoltergeistProcessor q; q.chopLoad(asSample(v, fs, "vocal")); q.chop.slices[2].pitch = 7; q.chop.makeMelody(); q.chopRenderNow();
    q.apvts.getParameter("smp_mode")->setValueNotifyingHost(q.apvts.getParameter("smp_mode")->convertTo0to1(4));
    q.apvts.getParameter("smp_level")->setValueNotifyingHost(0.9f);
    q.setRateAndBufferSizeDetails(48000, 512); q.prepareToPlay(48000, 512);
    juce::AudioBuffer<float> buf(2, 512); juce::MidiBuffer midi; midi.ensureSize(4096);
    bool finite = true; double energy = 0; long allocs = 0; q.chopPlay = true;
    for (int blk = 0; blk < 48000 * 10 / 512; ++blk) {
        midi.clear();
        if (blk % 40 == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 60 + (blk / 40) % 7, 0.9f), 10);
        if (blk % 40 == 20) midi.addEvent(juce::MidiMessage::noteOff(1, 60 + (blk / 40) % 7), 5);
        gAllocs = 0; gCount = true; q.processBlock(buf, midi); gCount = false; allocs += gAllocs.load();
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 512; ++i) { const float x = buf.getSample(ch, i); finite = finite && std::isfinite(x); energy += x * x; }
    }
    check(finite && energy > 1.0, "audio thread: 10 s of chop notes + CHOP MELODY, finite, energy " + juce::String(energy, 1));
    check(allocs == 0, "audio thread: " + juce::String(allocs) + " heap allocations inside processBlock (rendering is on a background thread)");
}

// ---- screenshots: the FM page with the ADD SAMPLE slot (empty, then a loaded "vocal"), in all three themes
static void shots(const juce::File& dir) {
    dir.createDirectory();
    const double fs = 44100; std::vector<int> on;
    auto v = makeVocal(fs, { 45, 45, 48, 45, 43 }, on, 0.05, 0.4, 0.06);
    for (auto t : { ghosty::themes::Theme::Dark, ghosty::themes::Theme::Purple, ghosty::themes::Theme::Light }) {
        const auto tn = ghosty::themes::displayName(t).toLowerCase();
        PoltergeistProcessor p; p.loadPreset(PoltergeistProcessor::numLegacyPresets());   // (a bass preset)
        std::unique_ptr<PoltergeistEditor> ed(dynamic_cast<PoltergeistEditor*>(p.createEditor()));
        ed->setSize(1040, 744); ed->testTheme(t);
        auto snap = [&](const juce::String& name) {
            juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
            auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
            juce::PNGImageFormat png; juce::File f = dir.getChildFile("polt-" + tn + "-" + name + ".png"); f.deleteFile();
            if (auto os = std::unique_ptr<juce::FileOutputStream>(f.createOutputStream())) png.writeImageToStream(img, *os);
            std::printf("wrote %s\n", f.getFullPathName().toRawUTF8());
        };
        ed->testShow(0, 0, 0); ed->selectGhost(0); snap("fm-page-empty-slot");
        ed->selectGhost(7); snap("add-sample-panel-empty");
        auto s = asSample(v, fs, "monster vocal"); p.addSample(s); ed->sampleAdded();
        auto* to = p.apvts.getParameter("smp_fm_to"); to->setValueNotifyingHost(to->convertTo0to1(1.0f));   // haunts ghost 2
        auto* wv = p.apvts.getParameter("smp_wave_to"); wv->setValueNotifyingHost(wv->convertTo0to1(3.0f));   // ghost 3 plays it as its wave
        p.apvts.getParameter("smp_lst")->setValueNotifyingHost(0.25f); p.apvts.getParameter("smp_lend")->setValueNotifyingHost(0.6f);
        ed->testRefresh(); snap("add-sample-loaded");
        ed->selectGhost(2); snap("ghost3-plays-sample-wave");
    }
}

// ---- demo: a chopped vocal-style melody, 15 s
static void demo(const juce::File& out) {
    const double fs = 44100; std::vector<int> on;
    // "sung" phrase: 6 syllables, a little off pitch (TUNE TO KEY fixes it)
    auto v = makeVocal(fs, { 62.3, 65.2, 69.4, 67.1, 64.8, 72.2 }, on, 0.05, 0.26, 0.07);
    PoltergeistProcessor p; p.chop.slicesMode = 0; p.chopLoad(asSample(v, fs, "phrase"));
    p.chop.key = 9; p.chop.scale = 1; p.chop.tuneToKey();   // A minor
    if (p.chop.count() > 3) { p.chop.slices[1].pitch += 12; p.chop.slices[3].pitch -= 5; p.chop.slices[2].reverse = true; }
    p.chop.seed = 5; p.chop.makeMelody(); p.chopRenderNow();
    auto set = [&](const char* id, float val) { auto* pr = p.apvts.getParameter(id); pr->setValueNotifyingHost(pr->convertTo0to1(val)); };
    set("smp_mode", 4); set("smp_level", 0.9f); set("macro_veil", 0.3f); set("master", -3.0f); set("smp_r", 0.03f);
    // a sub under it: ghost 1 plays the root an octave down? keep it pure chops; just the melody
    const double sr = 44100; p.setRateAndBufferSizeDetails(sr, 512); p.prepareToPlay(sr, 512);
    juce::AudioBuffer<float> all(2, (int) (sr * 15)); all.clear();
    juce::AudioBuffer<float> buf(2, 512); juce::MidiBuffer midi; midi.ensureSize(4096);
    p.chopPlay = true;
    for (int pos = 0; pos + 512 <= all.getNumSamples(); pos += 512) {
        if (pos == (int) (sr * 7.5) - ((int) (sr * 7.5)) % 512) {   // second half: BOUNCE with CRUNCH, new pattern
            p.chop.crunch = 0.55f; p.chopRenderNow(); p.chopBounce(); p.chop.slices[0].pitch = 7; p.chop.seed = 11; p.chop.makeMelody(); p.chopRenderNow();
        }
        midi.clear(); p.processBlock(buf, midi);
        for (int ch = 0; ch < 2; ++ch) all.copyFrom(ch, pos, buf, ch, 0, 512);
        if (std::getenv("CHOPDBG") && pos % 11264 == 0) std::printf("t=%.2f step=%d sounding=%d slices=%d\n", pos / sr, p.chopStepUi(), p.engine.chopSounding.load(), p.chop.count());
    }
    out.deleteFile();
    juce::WavAudioFormat wav;
    if (auto os = std::unique_ptr<juce::OutputStream>(out.createOutputStream()))
        if (auto* w = wav.createWriterFor(os.get(), sr, 2, 16, {}, 0)) { os.release(); std::unique_ptr<juce::AudioFormatWriter> wr(w); wr->writeFromAudioSampleBuffer(all, 0, all.getNumSamples()); }
    std::printf("wrote %s (peak %.3f)\n", out.getFullPathName().toRawUTF8(), all.getMagnitude(0, all.getNumSamples()));
}

#include "DemoGateTests.inc"
#include "SaveGateTests.inc"

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc > 1 && juce::String(argv[1]) == "demogate") return demogateTests();
    if (argc > 1 && juce::String(argv[1]) == "savegate") return saveGateTests();
    if (argc > 2 && juce::String(argv[1]) == "shots") { shots(juce::File(argv[2])); return 0; }
    if (argc > 2 && juce::String(argv[1]) == "demo") { demo(juce::File(argv[2])); return 0; }
    testOnsets(); testSlicePitch(); testLabSection(); testTune(); testProcessor();
    std::printf("%d failure(s)\n", failures);
    return failures;
}
