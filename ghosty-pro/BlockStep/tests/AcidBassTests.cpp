// ACID BASS SAMPLES tests (console, local harness like EngineTests): the 17 Block Step basses playing the acid line.
// Build: g++ -DENGINE_HEADER='"../<Plugin>Engine.h"' -DENGINE_NS=<bstep|ghouse> -DSYN_SOUND=<0|5> ... ; run with the plugin's data folder.
#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include ENGINE_HEADER
#include <cstdio>
#include <algorithm>

using namespace ENGINE_NS;
static int fails = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("  ok   %s\n", juce::String(msg).toRawUTF8()); else { std::printf("  FAIL %s\n", juce::String(msg).toRawUTF8()); ++fails; } } while (0)

static juce::File dataDir;
static std::vector<std::unique_ptr<juce::MemoryBlock>> keep;
static const void* lookup(const juce::String& f, int& size) {
    juce::File file = f == "allman.json" ? dataDir.getChildFile(f) : dataDir.getChildFile("sounds").getChildFile(f);
    auto mb = std::make_unique<juce::MemoryBlock>(); if (! file.loadFileAsData(*mb)) { size = 0; return nullptr; }
    keep.push_back(std::move(mb)); size = (int) keep.back()->getSize(); return keep.back()->getData();
}
// YIN (the same method as the offline root script): the median pitch (MIDI) of the loud, periodic frames in [a, b)
static double yinMidi(const float* x, int n, double sr, int a, int b) {
    const int W = 4096 / 2 * (sr > 60000 ? 2 : 1), tmax = juce::jmin(W - 1, (int) (sr / 25.0)), tmin = (int) (sr / 700.0); std::vector<double> est; double pk = 0;
    for (int i = a; i + 2 * W < juce::jmin(n, b); i += 512) { double e = 0; for (int k = 0; k < 2 * W; ++k) e += x[i + k] * x[i + k]; pk = juce::jmax(pk, e); }
    for (int i = a; i + 2 * W < juce::jmin(n, b); i += 512) {
        double e0 = 0; for (int k = 0; k < 2 * W; ++k) e0 += x[i + k] * x[i + k]; if (e0 < pk * 0.0625) continue;
        std::vector<double> d((size_t) tmax + 1, 0.0), cm((size_t) tmax + 1, 1.0);
        for (int t = 1; t <= tmax; ++t) { double s = 0; for (int j = 0; j < W; ++j) { const double q = x[i + j] - x[i + j + t]; s += q * q; } d[(size_t) t] = s; }
        double run = 0; for (int t = 1; t <= tmax; ++t) { run += d[(size_t) t]; cm[(size_t) t] = d[(size_t) t] * t / juce::jmax(run, 1e-12); }
        int t = tmin; while (t < tmax && cm[(size_t) t] >= 0.12) ++t; if (t >= tmax) t = (int) (std::min_element(cm.begin() + tmin, cm.begin() + tmax) - cm.begin());
        while (t + 1 < tmax && cm[(size_t) t + 1] < cm[(size_t) t]) ++t; if (cm[(size_t) t] >= 0.2) continue;
        const double A = d[(size_t) t - 1], B = d[(size_t) t], C = d[(size_t) t + 1], den = A - 2 * B + C, off = std::abs(den) > 1e-12 ? 0.5 * (A - C) / den : 0.0;
        est.push_back(12.0 * std::log2(sr / (t + off) / 440.0) + 69.0); }
    if (est.empty()) return -1; std::sort(est.begin(), est.end()); const double med = est[est.size() / 2];
    std::vector<double> k2; for (double v : est) if (std::abs(v - med) < 0.75) k2.push_back(v); std::sort(k2.begin(), k2.end()); return k2[k2.size() / 2];
}
static juce::String cellS(int p, int v, double l, bool sl = false, bool ac = false) {
    return "{\"i\":11,\"s\":" + juce::String(SYN_SOUND) + ",\"v\":" + juce::String(v) + ",\"fx\":\"\",\"pr\":4,\"p\":" + juce::String(p) + ",\"l\":" + juce::String(l) + (sl ? ",\"sl\":true" : "") + (ac ? ",\"ac\":true" : "") + "}";
}
static juce::String stateS(const std::vector<juce::String>& col0, double bpm, const juce::String& acid, double synMix = 1.0) {
    juce::String r = "["; for (size_t i = 0; i < col0.size(); ++i) r += juce::String(i ? "," : "") + "[" + (col0[i].isEmpty() ? juce::String("null") : col0[i]) + ",null,null,null,null]"; r += "]";
    const juce::String pat = "{\"bars\":" + juce::String((int) col0.size() / 16) + ",\"ch\":5,\"rows\":" + r + ",\"loop\":null}", empty = "{\"bars\":1,\"ch\":5,\"rows\":[],\"loop\":null}";
    return "{\"pats\":[" + pat + "," + empty + "," + empty + "," + empty + "],\"cur\":0,\"bpm\":" + juce::String(bpm) + ",\"bank\":\"block\",\"mix\":[1,1,1,1,1,1,1,1,1,1,1," + juce::String(synMix) + "]"
           + (acid.isNotEmpty() ? ",\"acid\":" + acid : juce::String()) + ",\"sndB\":{\"block\":[6,3,0,1,0,0,7,-1,0,0,0," + juce::String(SYN_SOUND) + "]}}";
}
static std::vector<float> run(SampleStore& st, const SynthKit& sk, const juce::String& state, double sr, double secs) {
    Engine e(st, sk); e.prepare(sr); e.setPattern(Pattern::fromState(juce::JSON::parse(state))); e.setInternalPlay(true);
    std::vector<float> L; juce::AudioBuffer<float> b(2, 512); Engine::Host h;
    for (int done = 0; done < (int) (sr * secs); done += 512) { b.clear(); e.process(b, 512, h); for (int i = 0; i < 512; ++i) L.push_back(b.getSample(0, i)); }
    return L;
}
static double rms(const std::vector<float>& x, int a, int b) { double s = 0; b = juce::jmin(b, (int) x.size()); for (int i = a; i < b; ++i) s += x[(size_t) i] * x[(size_t) i]; return std::sqrt(s / juce::jmax(1, b - a)); }
static double d2peak(const std::vector<float>& x, int a, int b) { double m = 0; a = juce::jmax(1, a); b = juce::jmin((int) x.size() - 1, b); for (int i = a; i < b; ++i) m = juce::jmax(m, (double) std::abs(x[(size_t) i + 1] - 2 * x[(size_t) i] + x[(size_t) i - 1])); return m; }

int main(int argc, char** argv) {
    dataDir = juce::File(argc > 1 ? argv[1] : "../data");
    SampleStore store; SynthKit synth; store.init(dataDir.getChildFile("allman.json").loadFileAsString(), lookup);
    namespace br = ghosty::bassroots;

    std::printf("the 17 basses and their roots\n");
    int found = 0; for (int b = 0; b < br::kCount; ++b) found += Engine::findBass(store, b) >= 0 ? 1 : 0;
    CHECK(found == 17, "all 17 basses are in this plugin's sound data (" + juce::String(found) + ")");
    static const bool steady[17] = { 0,1,0,0,1,1,0,1,1,1,1,1,1,1,1,1,0 };   // (the 808-style ones bend: their root is the median of a glide)
    for (int b = 0; b < br::kCount; ++b) {
        const int ix = Engine::findBass(store, b); const bool ok = ix >= 0 && store.decodeNow(Engine::kCatBass, ix); const Sample* s = ok ? store.get(Engine::kCatBass, ix) : nullptr;
        const double m = s ? yinMidi(s->buf.getReadPointer(0), s->buf.getNumSamples(), s->sr, (int) (0.03 * s->sr), s->buf.getNumSamples()) : -1;
        const bool plaus = br::kRoot[b] >= 20.0f && br::kRoot[b] <= 48.0f;
        CHECK(plaus && (! steady[b] || std::abs(m - br::kRoot[b]) < 0.3), "bass " + juce::String(b + 1) + ": root " + juce::String(br::kRoot[b], 2) + " (" + juce::String(440.0 * std::exp2((br::kRoot[b] - 69) / 12.0), 1) + " Hz), measured here " + juce::String(m, 2) + (steady[b] ? "" : " (bends; plausible range only)"));
    }

    std::printf("a note lands on its pitch (within 10 cents)\n");
    const int B = 5;   // bass 6: a steady C1, 9 s long
    const int ix = Engine::findBass(store, B); store.decodeNow(Engine::kCatBass, ix); const Sample* smp = store.get(Engine::kCatBass, ix);
    const double own = yinMidi(smp->buf.getReadPointer(0), smp->buf.getNumSamples(), smp->sr, (int) (0.3 * smp->sr), (int) (1.5 * smp->sr));
    const juce::String acid6 = "{\"snd\":\"bass\",\"b\":" + juce::String(B) + "}";
    for (double sr : { 44100.0, 48000.0 }) for (int p : { -5, 0, 7, 12, 19 }) {
        std::vector<juce::String> col(16); col[0] = cellS(p, 100, 16.0);
        const auto L = run(store, synth, stateS(col, 120, acid6), sr, 2.2);
        const double got = yinMidi(L.data(), (int) L.size(), sr, (int) (0.3 * sr), (int) (1.5 * sr)), want = own + (36 + p - br::kRoot[B]);
        CHECK(std::abs(got - want) * 100 < 10, "note " + juce::String(36 + p) + " at " + juce::String(sr, 0) + " Hz: " + juce::String((got - want) * 100, 1) + " cents off");
    }

    std::printf("no clicks at note changes\n");
    {   // a 16th-note line: new attacks, a slide (glide), gate ends; the 2nd difference at every change vs. inside the notes
        const double sr = 48000, bpm = 150, step = 60.0 / bpm / 4.0; std::vector<juce::String> col(32);
        const int ps[8] = { 0, 12, 3, 3, 7, -2, 10, 0 }; for (int k = 0; k < 8; ++k) col[(size_t) k * 2] = cellS(ps[k], 90, 1.4, k == 3, k == 5);
        const auto L = run(store, synth, stateS(col, bpm, acid6), sr, 32 * step);
        double edge = 0, body = 0;
        for (int k = 0; k < 8; ++k) { const int t = (int) std::lround(k * 2 * step * sr), off = (int) std::lround((k * 2 + 1.4) * step * sr);
            edge = juce::jmax(edge, d2peak(L, t - (int) (0.001 * sr), t + (int) (0.0005 * sr))); edge = juce::jmax(edge, d2peak(L, off - 48, off + (int) (0.013 * sr)));
            body = juce::jmax(body, d2peak(L, t + (int) (0.004 * sr), off - (int) (0.002 * sr))); }
        CHECK(body > 0 && edge <= body * 1.5, "2nd-difference peak at the changes " + juce::String(edge, 5) + " vs inside the notes " + juce::String(body, 5));
        CHECK(rms(L, (int) ((14 + 1.4) * step * sr) + (int) (0.02 * sr), (int) (31 * step * sr)) < 1e-4, "silent after the last gate (no hanging note)");
        std::vector<juce::String> c2(16); c2[0] = cellS(0, 90, 2.5, true); c2[2] = cellS(12, 90, 2);
        const auto G = run(store, synth, stateS(c2, bpm, acid6), sr, 1.2); const int t2 = (int) std::lround(2 * step * sr);
        const double gl = yinMidi(G.data(), (int) G.size(), sr, t2 + (int) (0.15 * sr), t2 + (int) (0.15 * sr) + 9000);
        CHECK(d2peak(G, t2 - 96, t2 + 96) <= d2peak(G, t2 - 2400, t2 - 200) * 1.5 + 1e-6 && std::abs(gl - (own + 48 - br::kRoot[B])) < 0.15, "SLIDE: no new attack, it glides up to the next note (" + juce::String(gl - own - 48 + br::kRoot[B], 2) + " st off after 150 ms)");
    }

    std::printf("MIX and the state\n");
    {   const double sr = 48000; std::vector<juce::String> col(16); col[0] = cellS(0, 100, 8.0);
        const double on = rms(run(store, synth, stateS(col, 120, acid6, 1.0), sr, 0.8), 2400, 36000), half = rms(run(store, synth, stateS(col, 120, acid6, 0.5), sr, 0.8), 2400, 36000), off = rms(run(store, synth, stateS(col, 120, acid6, 0.0), sr, 0.8), 0, 38000);
        CHECK(on > 0.01 && std::abs(half / on - 0.5) < 0.02, "ACID MIX volume is a gain (" + juce::String(half / on, 3) + " at 50%)");
        CHECK(off < 1e-6, "ACID MIX mute silences the sample bass (rms " + juce::String(off, 8) + ")");
        auto P1 = Pattern::fromState(juce::JSON::parse(stateS(col, 120, "{\"snd\":\"bass\",\"b\":7}"))), P0 = Pattern::fromState(juce::JSON::parse(stateS(col, 120, ""))), P3 = Pattern::fromState(juce::JSON::parse(stateS(col, 120, "{\"snd\":\"303\",\"b\":4}")));
        CHECK(P1->acidBass && P1->acidB == 7 && ! P0->acidBass && ! P3->acidBass, "state: {snd: bass, b: 7} -> bass 8; no \"acid\" (older projects) and {snd: 303} -> the synth");
        auto Pb = Pattern::fromState(juce::JSON::parse(stateS(col, 120, "{\"snd\":\"bass\",\"b\":99}"))); CHECK(Pb->acidBass && Pb->acidB == 16, "a bass number out of range is clamped");
        const auto syn = run(store, synth, stateS(col, 120, ""), sr, 0.8); const auto bas = run(store, synth, stateS(col, 120, acid6), sr, 0.8);
        double diff = 0; for (size_t i = 0; i < syn.size(); ++i) diff += std::abs(syn[i] - bas[i]); CHECK(diff > 1.0, "the switch changes the sound (303 vs bass)");
        auto wav = Engine::bounce(store, synth, *Pattern::fromState(juce::JSON::parse(stateS(col, 120, acid6))), sr, 120, -1);
        CHECK(wav.getNumSamples() > 0 && wav.getRMSLevel(0, 0, juce::jmin(wav.getNumSamples(), 36000)) > 0.01, "the ⇣ WAV bounce has the sample bass in it");
        Engine e(store, synth); e.prepare(sr); e.setPattern(Pattern::fromState(juce::JSON::parse(stateS(std::vector<juce::String>(16), 120, "{\"snd\":\"303\",\"b\":0}"))));
        Cell a; a.i = 11; a.s = (int16_t) SYN_SOUND; a.v = 90; a.ab = 3; e.audition(a); juce::AudioBuffer<float> blk(2, 4096); Engine::Host h; e.process(blk, 4096, h);
        CHECK(blk.getRMSLevel(0, 0, 4096) > 0.005 && e.bassActive(), "an audition naming a bass plays it right away (before the new state arrives; rms " + juce::String(blk.getRMSLevel(0, 0, 4096), 4) + ")");
    }
    std::printf(fails ? "\n%d FAILED\n" : "\nALL PASSED\n", fails); return fails ? 1 : 0;
}
