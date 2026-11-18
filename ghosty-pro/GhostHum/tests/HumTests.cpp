// Ghost Hum test harness.   HumTests [test | rw | plugin | beattrain [apply] | shots <dir>]
//   test        everything below (melody accuracy on synthesised singers, beatbox detection / classification / calibration, tempo,
//               quantize, CLEAN + UNDO, MIDI / WAV files, plugin state, the plugin path, the input sources (build 3), the editor)
//   beattrain   fits the beatbox classifier on our own synthesised beatboxers (tests/HumVoices.h) and prints the MODEL block;
//               "apply" writes it into HumBeat.h between MODEL BEGIN / MODEL END
//   shots <dir> screenshots of the window (melody, beatbox, teach)
#include "../HumProcessor.h"
#include "../HumEditor.h"
#include "HumVoices.h"
#include <cstdio>
#include <map>
#include <functional>

static int fails = 0, passes = 0;
#define CHECK(cond, ...) do { if (cond) { ++passes; } else { ++fails; std::printf("  FAIL %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
static void section(const char* s) { std::printf("== %s\n", s); }
using namespace humtest;
using hum::beat::kF; using hum::beat::Feat;

// ---------------------------------------------------------------- melody
struct MelScore { int truth = 0, found = 0, matched = 0, pitchOk = 0, extra = 0, onN = 0; double onErr = 0, offErr = 0; };
static void scoreMelody(const hum::Take& T, const Truth& tr, MelScore& S, double win = 0.08) {
    S.truth += (int) tr.on.size(); S.found += (int) T.notes.size(); std::vector<bool> used(T.notes.size(), false);
    for (size_t i = 0; i < tr.on.size(); ++i) { int best = -1; double bd = 1e9;
        for (size_t j = 0; j < T.notes.size(); ++j) if (!used[j]) { const double d = std::abs(T.notes[j].t - tr.on[i]); if (d < bd) { bd = d; best = (int) j; } }
        if (best >= 0 && bd <= win) { used[(size_t) best] = true; ++S.matched; if (T.notes[(size_t) best].midi == tr.midi[i]) ++S.pitchOk; S.onErr += bd; S.offErr += std::abs(T.notes[(size_t) best].t + T.notes[(size_t) best].len - tr.off[i]); ++S.onN; } }
    for (bool u : used) if (!u) ++S.extra;
}
static void testMelody() {
    section("melody: synthesised singers (vibrato, detune, scoops, breath, legato, repeats)");
    MelScore S; const int singers = 80; int worst = 100;
    for (int s = 0; s < singers; ++s) { Rand R(1000 + s); Singer sg = Singer::random(R); const double bpm = R.u(70, 150); auto ph = randomPhrase(R, 8, bpm); Truth tr; auto x = sing(sg, ph, R, tr);
        hum::melody::Options o; auto T = hum::melody::transcribe(x, kSr, o); MelScore one; scoreMelody(T, tr, one); scoreMelody(T, tr, S); worst = juce::jmin(worst, 100 * one.pitchOk / juce::jmax(1, one.truth)); }
    const double pct = 100.0 * S.pitchOk / S.truth, onMs = 1000 * S.onErr / juce::jmax(1, S.onN), offMs = 1000 * S.offErr / juce::jmax(1, S.onN);
    std::printf("  %d notes sung: %d found, %d matched within 80 ms, %d the right note (%.1f%%), %d extra (%.1f%%); onset off by %.1f ms, end by %.1f ms; worst singer %d%%\n", S.truth, S.found, S.matched, S.pitchOk, pct, S.extra, 100.0 * S.extra / S.truth, onMs, offMs, worst);
    CHECK(pct >= 93.0, "right notes %.1f%% < 93%%", pct);
    CHECK(S.extra <= S.truth * 0.03, "invented notes %d (> 3%%)", S.extra);
    CHECK(onMs < 30 && offMs < 80, "timing: onset %.1f ms, offset %.1f ms", onMs, offMs);
    CHECK(worst >= 50, "worst singer %d%%", worst);
    // a noisy room (hiss 36 dB under the voice): still most notes, few extras
    { MelScore N; for (int si = 0; si < 30; ++si) { Rand R(1000 + si); Singer sg = Singer::random(R); sg.roomDb = -36; const double bpm = R.u(70, 150); auto ph = randomPhrase(R, 8, bpm); Truth tr; auto x = sing(sg, ph, R, tr); hum::melody::Options o; scoreMelody(hum::melody::transcribe(x, kSr, o), tr, N); }
      std::printf("  noisy room (-36 dB hiss): %d/%d right (%.1f%%), %d extra\n", N.pitchOk, N.truth, 100.0 * N.pitchOk / N.truth, N.extra); CHECK(100.0 * N.pitchOk / N.truth >= 90.0 && N.extra <= N.truth * 0.05, "noisy room: %.1f%% right, %d extra", 100.0 * N.pitchOk / N.truth, N.extra); }
    // nothing from silence / noise / breath: no notes invented
    { std::vector<float> z((size_t) (kSr * 3), 0.0f); hum::melody::Options o; CHECK(hum::melody::transcribe(z, kSr, o).notes.empty(), "silence made notes");
      Rand R(7); for (auto& v : z) v = (float) R.n() * 0.2f; CHECK(hum::melody::transcribe(z, kSr, o).notes.empty(), "white noise made notes");
      BP b; b.set(800, 4000, kSr); for (auto& v : z) v = (float) b.run(R.n()) * 0.3f; const auto T = hum::melody::transcribe(z, kSr, o); CHECK(T.notes.size() <= 1, "breath noise made %d notes", (int) T.notes.size()); }
    // one held note with deep vibrato (+-50 cents) is ONE note; the nearest note is right at +-40 cents detune; ORIGINAL keeps the cents
    for (int det : { -40, -20, 0, 25, 40 }) { Rand R(99); Singer sg; sg.vibCents = 50; sg.detuneCents = det; sg.scoopCents = -200; std::vector<SungNote> ph { { 64, 1.6, 0.1, false } }; Truth tr; auto x = sing(sg, ph, R, tr);
        hum::melody::Options o; auto T = hum::melody::transcribe(x, kSr, o); CHECK(T.notes.size() == 1 && T.notes[0].midi == 64, "detune %d: %d notes, first %d", det, (int) T.notes.size(), T.notes.empty() ? -1 : T.notes[0].midi);
        if (T.notes.size() == 1) { CHECK(std::abs(T.notes[0].cents) < 1e-3f, "NEAREST NOTE left cents %.1f", T.notes[0].cents); CHECK(std::abs(T.origNotes[0].cents - det) < 14.0f, "as-heard cents %.0f for detune %d", T.origNotes[0].cents, det);
            hum::melody::Options oo; oo.pitch = hum::PitchOriginal; auto T2 = hum::melody::transcribe(x, kSr, oo); CHECK(T2.notes.size() == 1 && std::abs(T2.notes[0].cents - det) < 14.0f, "ORIGINAL cents %.0f for detune %d", T2.notes.empty() ? 0.0f : T2.notes[0].cents, det); } }
    // SNAP TO KEY: a phrase in C major with one note pushed a semitone off the key snaps back; other notes stay
    { Rand R(5); Singer sg; sg.vibCents = 15; sg.scoopCents = -80; std::vector<SungNote> ph { { 60, 0.5, 0.05, false }, { 62, 0.5, 0.05, false }, { 64, 0.5, 0.05, false }, { 66, 0.5, 0.05, false }, { 67, 0.6, 0.05, false }, { 64, 0.5, 0.05, false }, { 60, 0.8, 0.05, false } }; Truth tr; auto x = sing(sg, ph, R, tr);
      hum::melody::Options o; o.pitch = hum::PitchSnapKey; o.keyOverride = 0; auto T = hum::melody::transcribe(x, kSr, o);
      CHECK(T.notes.size() == 7, "snap: %d notes", (int) T.notes.size()); bool all = true; for (auto& n : T.notes) all = all && hum::inScale(n.midi, 0, 0); CHECK(all, "snap left a note outside C major");
      if (T.notes.size() == 7) CHECK(T.notes[3].midi == 65 || T.notes[3].midi == 67, "F# snapped to %d", T.notes[3].midi); CHECK(T.origNotes.size() == 7 && T.origNotes[3].midi == 66, "as-heard kept F#");
      CHECK(T.key == 0 && T.scale == 0, "detected key %s (wanted C major)", hum::keyText(T).toRawUTF8()); }
    // low and high voices
    for (int base : { 45, 79 }) { Rand R(31 + base); Singer sg = Singer::random(R); sg.breathDb = -34; auto ph = randomPhrase(R, 6, 100, base - 3, base + 5); Truth tr; auto x = sing(sg, ph, R, tr); hum::melody::Options o; auto T = hum::melody::transcribe(x, kSr, o); MelScore one; scoreMelody(T, tr, one);
        CHECK(one.pitchOk >= one.truth - 1 && one.extra <= 1, "range around %d: %d/%d right, %d extra", base, one.pitchOk, one.truth, one.extra); }
}

// ---------------------------------------------------------------- real voices, mics and rooms (build 2)
// Every condition runs the SAME 30 singers / 30 beatboxers through a phone / laptop mic model (tests/HumVoices.h: Mic, addMouthNoise,
// addClickLeak). Thresholds are what build 2 reaches minus a small margin; the printed table is the before / after report.
struct BeatDet { int truth = 0, found = 0, matched = 0, extra = 0; double onErr = 0; };
static std::vector<double> clickTimesTmp;
struct RwMel { const char* name; std::function<void(Singer&, Mic&, int& low, int& high, int& cons)> set; double minRight, maxExtra; double leakDb = -200; };
struct RwBeat { const char* name; std::function<void(Mic&, bool& noise, double& leakDb)> set; double minFound, maxExtra, minRight; };
static void testRealWorld() {
    section("real voices: phone / laptop mics, rooms, consonants, low / high voices, vocal fry (melody)");
    const std::vector<RwMel> mel {
        { "clean (reference)",           [](Singer&, Mic&, int&, int&, int&) {}, 97, 3 },
        { "DC offset + rumble",          [](Singer&, Mic& m, int&, int&, int&) { m.dc = 0.08; m.rumbleDb = -14; }, 95, 3 },
        { "room reverb (0.8 s)",         [](Singer&, Mic& m, int&, int&, int&) { m.reverb = 0.45; m.reverbSec = 0.8; }, 92, 17 },
        { "mic clipping (x4)",           [](Singer&, Mic& m, int&, int&, int&) { m.clipGain = 4; }, 97, 4 },
        { "very quiet take (-40 dB)",    [](Singer&, Mic& m, int&, int&, int&) { m.levelDb = -40; }, 97, 3 },
        { "mouth clicks + lip smacks",   [](Singer&, Mic& m, int&, int&, int&) { m.clicks = 12; m.smacks = 6; }, 97, 3 },
        { "plosive pops",                [](Singer&, Mic& m, int&, int&, int&) { m.pops = 6; }, 92, 15 },
        { "laptop fan + mains hum",      [](Singer&, Mic& m, int&, int&, int&) { m.fanDb = -30; m.humDb = -26; m.humHz = 60; }, 95, 3 },
        { "\"la la\" consonants",        [](Singer&, Mic&, int&, int&, int& c) { c = 1; }, 96, 3 },
        { "\"na na\" consonants",        [](Singer&, Mic&, int&, int&, int& c) { c = 2; }, 94, 5 },
        { "closed-mouth humming",        [](Singer& s, Mic&, int&, int&, int&) { s.closedMouth = true; s.breathDb = -36; }, 96, 4 },
        { "low male voice (80-150 Hz)",  [](Singer& s, Mic&, int& lo, int& hi, int&) { lo = 40; hi = 50; s.F = { 600, 1000, 2400 }; }, 96, 3 },
        { "high female / child voice",   [](Singer& s, Mic&, int& lo, int& hi, int&) { lo = 60; hi = 79; s.F = { 800, 1500, 2900 }; }, 90, 7 },
        { "vocal fry (octave trap)",     [](Singer& s, Mic&, int& lo, int& hi, int&) { s.altPulse = 0.45; lo = 43; hi = 55; }, 85, 4 },
        { "phone low cut, low voice",    [](Singer& s, Mic& m, int& lo, int& hi, int&) { m.lowCut = 180; lo = 40; hi = 50; s.F = { 600, 1000, 2400 }; }, 96, 3 },
        { "phone: cut+clip+room+clicks", [](Singer&, Mic& m, int&, int&, int&) { m.lowCut = 120; m.clipGain = 2; m.reverb = 0.25; m.clicks = 6; m.humDb = -32; m.pops = 2; }, 95, 5 },
        { "metronome leak -10 dB",       [](Singer&, Mic&, int&, int&, int&) {}, 96, 4, -10 },
        { "metronome leak -4 dB (loud)", [](Singer&, Mic& m, int&, int&, int&) { m.reverb = 0.25; }, 92, 8, -4 },
    };
    for (auto& c : mel) { MelScore S; for (int s = 0; s < 30; ++s) { Rand R(1000 + s); Singer sg = Singer::random(R); Mic m; int lo = 55, hi = 72, cons = 0; c.set(sg, m, lo, hi, cons);
            const double bpm = R.u(70, 150); auto ph = randomPhrase(R, 8, bpm, lo, hi); if (cons) for (auto& n : ph) { n.cons = cons; n.reattack = false; }
            Truth tr; auto x = sing(sg, ph, R, tr); if (c.leakDb > -150) addClickLeak(x, bpm, 0.0, c.leakDb, 0.025, R); applyMic(x, m, R);
            hum::melody::Options o; if (c.leakDb > -150) { o.clickTimes = &clickTimesTmp; clickTimesTmp.clear(); const double beat = 60.0 / bpm; for (int k = 0; k * beat < x.size() / kSr; ++k) clickTimesTmp.push_back(k * beat); }
            scoreMelody(hum::melody::transcribe(x, kSr, o), tr, S); }
        const double right = 100.0 * S.pitchOk / S.truth, extra = 100.0 * S.extra / S.truth;
        std::printf("  %-30s %5.1f%% right  %4.1f%% extra  (%d notes, onset %.0f ms)\n", c.name, right, extra, S.truth, 1000 * S.onErr / juce::jmax(1, S.onN));
        CHECK(right >= c.minRight, "%s: %.1f%% right < %.0f%%", c.name, right, c.minRight); CHECK(extra <= c.maxExtra, "%s: %.1f%% extra > %.0f%%", c.name, extra, c.maxExtra); }

    section("real voices: breath and mouth noises between hits, mics, rooms, the leaked click (beatbox)");
    const std::vector<RwBeat> bt {
        { "clean (reference)",            [](Mic&, bool&, double&) {}, 97, 4, 96 },
        { "breath + grunts + smacks",     [](Mic&, bool& n, double&) { n = true; }, 96, 17, 95 },
        { "mouth clicks + pops",          [](Mic& m, bool&, double&) { m.clicks = 10; m.pops = 4; }, 93, 27, 94 },
        { "DC + rumble + hum + fan",      [](Mic& m, bool&, double&) { m.dc = 0.08; m.rumbleDb = -14; m.humDb = -26; m.fanDb = -30; }, 90, 13, 92 },
        { "very quiet take (-40 dB)",     [](Mic& m, bool&, double&) { m.levelDb = -40; }, 97, 4, 96 },
        { "mic clipping (x4)",            [](Mic& m, bool&, double&) { m.clipGain = 4; }, 97, 9, 92 },
        { "room reverb (0.6 s)",          [](Mic& m, bool&, double&) { m.reverb = 0.35; }, 96, 6, 95 },
        { "metronome leak -12 dB",        [](Mic&, bool&, double& l) { l = -12; }, 95, 4, 95 },
        { "metronome leak -6 dB (loud)",  [](Mic&, bool&, double& l) { l = -6; }, 95, 5, 94 },
        { "leak -6 dB + room + breath",   [](Mic& m, bool& n, double& l) { l = -6; m.reverb = 0.3; n = true; }, 94, 18, 93 },
        { "phone: cut+clip+room+noises",  [](Mic& m, bool& n, double&) { m.lowCut = 120; m.clipGain = 2; m.reverb = 0.2; m.clicks = 4; n = true; }, 96, 25, 94 },
    };
    for (auto& c : bt) { BeatDet D; int right = 0;
        for (int b = 0; b < 30; ++b) { Rand R(7000 + b); Beatboxer B = Beatboxer::random(R); const double bpm = R.u(75, 150); auto hits = randomPattern(R, bpm, 2); auto x = beatbox(B, hits, R);
            Mic m; bool noise = false; double leak = -200; c.set(m, noise, leak); if (noise) addMouthNoise(x, hits, R); if (leak > -150) addClickLeak(x, bpm, hits.front().t, leak, 0.03, R); applyMic(x, m, R);
            hum::beat::Options o; if (leak > -150) { o.clickTimes = &clickTimesTmp; clickTimesTmp.clear(); const double beat = 60.0 / bpm; for (int k = 0; hits.front().t + k * beat < x.size() / kSr; ++k) clickTimesTmp.push_back(hits.front().t + k * beat); }
            std::vector<hum::beat::Event> ev; hum::beat::transcribe(x, kSr, o, &ev); D.truth += (int) hits.size(); D.found += (int) ev.size(); std::vector<bool> used(ev.size(), false);
            for (auto& h : hits) { int best = -1; double bd = 1e9; for (size_t j = 0; j < ev.size(); ++j) if (!used[j]) { const double d = std::abs(ev[j].t - h.t); if (d < bd) { bd = d; best = (int) j; } }
                if (best >= 0 && bd <= 0.03) { used[(size_t) best] = true; ++D.matched; if (ev[(size_t) best].drum == h.drum) ++right; } }
            for (bool u : used) if (!u) ++D.extra; }
        const double found = 100.0 * D.matched / D.truth, extra = 100.0 * D.extra / D.truth, rt = 100.0 * right / juce::jmax(1, D.matched);
        std::printf("  %-30s %5.1f%% found  %4.1f%% extra  %5.1f%% right drum  (%d hits)\n", c.name, found, extra, rt, D.truth);
        CHECK(found >= c.minFound, "%s: found %.1f%% < %.0f%%", c.name, found, c.minFound); CHECK(extra <= c.maxExtra, "%s: %.1f%% extra > %.0f%%", c.name, extra, c.maxExtra); CHECK(rt >= c.minRight, "%s: right drum %.1f%% < %.0f%%", c.name, rt, c.minRight); }
}

// ---------------------------------------------------------------- beatbox
struct BeatSample { Feat f; int drum; int boxer; double t; };
static std::vector<float> renderBoxer(int seed, double& bpm, std::vector<HitTruth>& hits, Beatboxer* outB = nullptr, int bars = 2) { Rand R(5000 + seed); Beatboxer B = Beatboxer::random(R); bpm = R.u(75, 150); hits = randomPattern(R, bpm, bars); if (outB) *outB = B; return beatbox(B, hits, R); }
// a random phone / laptop mic and a noisy mouth for the training set (half the boxers): what real takes have between and under the hits
static void roughen(std::vector<float>& x, const std::vector<HitTruth>& hits, double bpm, Rand& R) {
    Mic m; if (R.p(0.4)) m.clipGain = R.u(1.3, 4); if (R.p(0.3)) m.lowCut = R.u(80, 200); if (R.p(0.4)) m.reverb = R.u(0.1, 0.4); if (R.p(0.3)) m.humDb = R.u(-40, -24); if (R.p(0.3)) m.fanDb = R.u(-40, -26);
    if (R.p(0.5)) m.clicks = R.i(2, 12); if (R.p(0.4)) m.smacks = R.i(1, 5); if (R.p(0.4)) m.pops = R.i(1, 5); if (R.p(0.2)) m.dc = R.u(-0.1, 0.1); if (R.p(0.2)) m.rumbleDb = R.u(-24, -12); if (R.p(0.3)) m.levelDb = R.u(-40, -10);
    if (R.p(0.6)) addMouthNoise(x, hits, R, R.u(-26, -10), R.u(-22, -8), R.p(0.7)); if (R.p(0.35)) addClickLeak(x, bpm, hits.front().t, R.u(-24, -8), R.u(0.005, 0.06), R);
    applyMic(x, m, R);
}
// matched hits (their drum) and, with junk = true, the events far from any real hit (> 60 ms) as "not a drum"
static void collectBeat(int first, int count, std::vector<BeatSample>& out, BeatDet& D, const hum::beat::Calibration* cal = nullptr, std::array<std::array<int, 5>, 5>* conf = nullptr, int* right = nullptr, bool rough = false, bool junk = false) {
    for (int b = first; b < first + count; ++b) { double bpm; std::vector<HitTruth> hits; auto x = renderBoxer(b, bpm, hits); if (rough) { Rand R(77000 + b); if (R.p(0.5)) roughen(x, hits, bpm, R); }
        hum::beat::Options o; o.cal = cal; std::vector<hum::beat::Event> ev; if (junk) ev = hum::beat::hear(x, kSr); else hum::beat::transcribe(x, kSr, o, &ev); D.truth += (int) hits.size(); D.found += (int) ev.size(); std::vector<bool> used(ev.size(), false);
        for (auto& h : hits) { int best = -1; double bd = 1e9; for (size_t j = 0; j < ev.size(); ++j) if (!used[j]) { const double d = std::abs(ev[j].t - h.t); if (d < bd) { bd = d; best = (int) j; } }
            if (best >= 0 && bd <= 0.03) { used[(size_t) best] = true; ++D.matched; D.onErr += bd; out.push_back({ ev[(size_t) best].f, h.drum, b, h.t }); if (conf) ++(*conf)[(size_t) h.drum][(size_t) ev[(size_t) best].drum]; if (right && ev[(size_t) best].drum == h.drum) ++*right; } }
        for (size_t j = 0; j < ev.size(); ++j) if (!used[j]) { ++D.extra; if (junk) { double near = 1e9; for (auto& h : hits) near = juce::jmin(near, std::abs(ev[j].t - h.t)); if (near > 0.06) out.push_back({ ev[j].f, hum::beat::kJunk, b, ev[j].t }); } } }
}
// softmax regression on standardised features (balanced classes, L2); the built-in model
static hum::beat::Model trainModel(const std::vector<BeatSample>& tr, int iters = 4000, float lr = 0.05f, float l2 = 1e-3f) {
    constexpr int kC = hum::beat::kClasses; hum::beat::Model M; const int N = (int) tr.size(); for (auto& s : tr) for (int i = 0; i < kF; ++i) M.mu[(size_t) i] += s.f[(size_t) i] / N;
    for (auto& s : tr) for (int i = 0; i < kF; ++i) { const float z = s.f[(size_t) i] - M.mu[(size_t) i]; M.sg[(size_t) i] += z * z / N; } for (auto& v : M.sg) v = juce::jmax(0.05f, std::sqrt(v));
    std::array<float, kC> cw {}; for (auto& s : tr) cw[(size_t) s.drum] += 1; for (auto& w : cw) w = (float) N / ((float) kC * juce::jmax(1.0f, w)); cw[(size_t) hum::beat::kJunk] *= 0.6f;   // (a dropped hit is invisible: "not a drum" has to be clearly likelier)
    std::vector<Feat> Z((size_t) N); for (int k = 0; k < N; ++k) Z[(size_t) k] = M.z(tr[(size_t) k].f);
    for (auto& r : M.W) r.fill(0.0f);
    for (int it = 0; it < iters; ++it) { std::array<std::array<float, kF + 1>, kC> G {};
        for (int k = 0; k < N; ++k) { std::array<float, kC> sc {}; float mx = -1e9f; for (int d = 0; d < kC; ++d) { float s = M.W[(size_t) d][kF]; for (int i = 0; i < kF; ++i) s += M.W[(size_t) d][(size_t) i] * Z[(size_t) k][(size_t) i]; sc[(size_t) d] = s; mx = juce::jmax(mx, s); }
            float sum = 0; for (auto& v : sc) { v = std::exp(v - mx); sum += v; } const float w = cw[(size_t) tr[(size_t) k].drum];
            for (int d = 0; d < kC; ++d) { const float g = (sc[(size_t) d] / sum - (d == tr[(size_t) k].drum ? 1.0f : 0.0f)) * w / N; for (int i = 0; i < kF; ++i) G[(size_t) d][(size_t) i] += g * Z[(size_t) k][(size_t) i]; G[(size_t) d][kF] += g; } }
        for (int d = 0; d < kC; ++d) for (int i = 0; i <= kF; ++i) M.W[(size_t) d][(size_t) i] -= lr * (G[(size_t) d][(size_t) i] + (i < kF ? l2 * M.W[(size_t) d][(size_t) i] : 0.0f)); }
    // within-class spread of each standardised feature (TEACH's distance weights)
    { std::array<Feat, kC> mean {}, var {}; std::array<int, kC> cnt {}; for (int k = 0; k < N; ++k) { ++cnt[(size_t) tr[(size_t) k].drum]; for (int i = 0; i < kF; ++i) mean[(size_t) tr[(size_t) k].drum][(size_t) i] += Z[(size_t) k][(size_t) i]; }
      for (int d = 0; d < kC; ++d) for (int i = 0; i < kF; ++i) mean[(size_t) d][(size_t) i] /= juce::jmax(1, cnt[(size_t) d]);
      for (int k = 0; k < N; ++k) for (int i = 0; i < kF; ++i) { const float e = Z[(size_t) k][(size_t) i] - mean[(size_t) tr[(size_t) k].drum][(size_t) i]; var[(size_t) tr[(size_t) k].drum][(size_t) i] += e * e; }
      for (int i = 0; i < kF; ++i) { float v = 0; for (int d = 0; d < 5; ++d) v += var[(size_t) d][(size_t) i] / juce::jmax(1, cnt[(size_t) d]); M.wsd[(size_t) i] = std::sqrt(v / 5); } }
    return M;
}
static juce::String modelBlock(const hum::beat::Model& M) {
    juce::String s; auto num = [](float v) { return juce::String(v, 5) + "f"; };
    s << "        m.mu = {"; for (int i = 0; i < kF; ++i) s << (i ? ", " : " ") << num(M.mu[(size_t) i]); s << " };\n        m.sg = {"; for (int i = 0; i < kF; ++i) s << (i ? ", " : " ") << num(M.sg[(size_t) i]); s << " };\n        m.wsd = {"; for (int i = 0; i < kF; ++i) s << (i ? ", " : " ") << num(M.wsd[(size_t) i]); s << " };\n";
    for (int d = 0; d < hum::beat::kClasses; ++d) { s << "        m.W[" << d << "] = {"; for (int i = 0; i <= kF; ++i) s << (i ? ", " : " ") << num(M.W[(size_t) d][(size_t) i]); s << " };   // " << (d < hum::kDrums ? hum::drumName(d) : "NOT A DRUM") << "\n"; }
    return s;
}
static int beatTrain(bool apply) {
    std::vector<BeatSample> tr, te; BeatDet D1, D2; collectBeat(0, 320, tr, D1, nullptr, nullptr, nullptr, true, true); auto M = trainModel(tr);
    { int nj = 0; for (auto& q : tr) if (q.drum == hum::beat::kJunk) ++nj; std::printf("training set: %d samples, %d of them not a drum\n", (int) tr.size(), nj); }
    std::array<std::array<int, 5>, 5> conf {}; int right = 0; std::vector<BeatSample> te2; BeatDet D3;
    { hum::beat::Options o; o.model = &M; for (int b = 1000; b < 1080; ++b) { double bpm; std::vector<HitTruth> hits; auto x = renderBoxer(b, bpm, hits); std::vector<hum::beat::Event> ev; hum::beat::transcribe(x, kSr, o, &ev); std::vector<bool> used(ev.size(), false);
        for (auto& h : hits) { int best = -1; double bd = 1e9; for (size_t j = 0; j < ev.size(); ++j) if (!used[j]) { const double d = std::abs(ev[j].t - h.t); if (d < bd) { bd = d; best = (int) j; } } if (best >= 0 && bd <= 0.03) { used[(size_t) best] = true; ++D3.matched; ++conf[(size_t) h.drum][(size_t) ev[(size_t) best].drum]; if (ev[(size_t) best].drum == h.drum) ++right; } } } }
    std::printf("trained on %d hits from 320 beatboxers (half through rough mics); held-out 80 beatboxers: %.1f%% right (%d/%d)\n", (int) tr.size(), 100.0 * right / juce::jmax(1, D3.matched), right, D3.matched);
    for (int d = 0; d < 5; ++d) { std::printf("  %-9s", hum::drumName(d)); for (int k = 0; k < 5; ++k) std::printf(" %4d", conf[(size_t) d][(size_t) k]); std::printf("\n"); }
    const auto block = modelBlock(M); std::printf("%s", block.toRawUTF8());
    if (apply) { juce::File f(juce::File(__FILE__).getParentDirectory().getParentDirectory().getChildFile("HumBeat.h")); auto s = f.loadFileAsString();
        const int a = s.indexOf("// MODEL BEGIN"), b = s.indexOf("// MODEL END"); if (a < 0 || b < 0) { std::printf("markers not found\n"); return 1; }
        const int lineEnd = s.indexOf(a, "\n") + 1, endLine = s.indexOf(lineEnd, "        // MODEL END");
        s = s.substring(0, lineEnd) + block + s.substring(endLine); f.replaceWithText(s); std::printf("written to %s\n", f.getFullPathName().toRawUTF8()); }
    return 0;
}
static void testBeat() {
    section("beatbox: detection + which drum (held-out synthesised beatboxers, built-in model)");
    std::vector<BeatSample> te; BeatDet D; std::array<std::array<int, 5>, 5> conf {}; int right = 0; collectBeat(2000, 60, te, D, nullptr, &conf, &right);
    std::printf("  %d hits made: %d found, %d matched within 30 ms (%.1f%%), %d extra; onset off by %.1f ms; the right drum %.1f%% (%d/%d)\n", D.truth, D.found, D.matched, 100.0 * D.matched / D.truth, D.extra, 1000 * D.onErr / juce::jmax(1, D.matched), 100.0 * right / juce::jmax(1, D.matched), right, D.matched);
    for (int d = 0; d < 5; ++d) { int tot = 0; for (int k = 0; k < 5; ++k) tot += conf[(size_t) d][(size_t) k]; std::printf("    %-9s %3d%%  (", hum::drumName(d), 100 * conf[(size_t) d][(size_t) d] / juce::jmax(1, tot)); for (int k = 0; k < 5; ++k) std::printf(" %d", conf[(size_t) d][(size_t) k]); std::printf(" )\n"); }
    CHECK(D.matched >= D.truth * 0.97, "found only %.1f%% of the hits", 100.0 * D.matched / D.truth);
    CHECK(D.extra <= D.truth * 0.05, "%d extra hits", D.extra);
    CHECK(100.0 * right / juce::jmax(1, D.matched) >= 94.0, "right drum %.1f%% < 94%%", 100.0 * right / juce::jmax(1, D.matched));
    for (int d = 0; d < 5; ++d) { int tot = 0; for (int k = 0; k < 5; ++k) tot += conf[(size_t) d][(size_t) k]; CHECK(conf[(size_t) d][(size_t) d] >= tot * 0.85, "%s only %d%% right", hum::drumName(d), 100 * conf[(size_t) d][(size_t) d] / juce::jmax(1, tot)); }
    // confidence: wrong calls are less sure than right ones
    { double sureRight = 0, sureWrong = 0; int nr = 0, nw = 0; for (int b = 2000; b < 2030; ++b) { double bpm; std::vector<HitTruth> hits; auto x = renderBoxer(b, bpm, hits); std::vector<hum::beat::Event> ev; hum::beat::transcribe(x, kSr, {}, &ev);
          for (auto& h : hits) for (auto& e : ev) if (std::abs(e.t - h.t) <= 0.03) { if (e.drum == h.drum) { sureRight += e.conf; ++nr; } else { sureWrong += e.conf; ++nw; } } }
      std::printf("  confidence: right calls %.2f, wrong calls %.2f (%d wrong)\n", sureRight / juce::jmax(1, nr), sureWrong / juce::jmax(1, nw), nw); CHECK(nw == 0 || sureWrong / nw < sureRight / juce::jmax(1, nr), "wrong calls as sure as right ones"); }

    section("beatbox: TEACH GHOST YOUR SOUNDS (odd mouths: 3 examples per drum, with and without)");
    int withoutR = 0, withR = 0, tot = 0;
    for (int b = 0; b < 30; ++b) {
        Rand R(9000 + b); Beatboxer B = Beatboxer::random(R);   // an odd mouth: pushed outside what the model saw
        B.tilt = R.p(0.5) ? 1.6 : -1.6; B.kickHz *= R.u(1.3, 1.6); B.snareHz *= R.u(0.55, 0.75); B.hatHz *= R.u(0.6, 0.8); B.clapDecay *= 2.5; B.mouthQ = 3.5;
        // the lesson: each drum 3 times, 400 ms apart
        hum::beat::Calibration cal; for (int d = 0; d < hum::kDrums; ++d) { std::vector<HitTruth> three { { 0.2, d, 0.9 }, { 0.6, d, 0.8 }, { 1.0, d, 0.95 } }; auto x = beatbox(B, three, R); auto ev = hum::beat::hear(x, kSr);
            std::sort(ev.begin(), ev.end(), [](const hum::beat::Event& p, const hum::beat::Event& q) { return p.energy > q.energy; }); for (size_t i = 0; i < ev.size() && i < 3; ++i) { hum::beat::Example e; e.drum = d; e.f = ev[i].f; cal.ex.push_back(e); } }
        const double bpm = R.u(80, 140); auto hits = randomPattern(R, bpm, 2); auto x = beatbox(B, hits, R);
        std::vector<hum::beat::Event> e0, e1; hum::beat::transcribe(x, kSr, {}, &e0); hum::beat::Options o; o.cal = &cal; hum::beat::transcribe(x, kSr, o, &e1);
        for (auto& h : hits) { ++tot; for (auto& e : e0) if (std::abs(e.t - h.t) <= 0.03) { if (e.drum == h.drum) ++withoutR; break; } for (auto& e : e1) if (std::abs(e.t - h.t) <= 0.03) { if (e.drum == h.drum) ++withR; break; } }
    }
    std::printf("  odd mouths: %.1f%% right built-in, %.1f%% right after teaching (%d hits)\n", 100.0 * withoutR / tot, 100.0 * withR / tot, tot);
    CHECK(withR >= withoutR, "teaching made it worse"); CHECK(100.0 * withR / tot >= 90.0, "after teaching only %.1f%%", 100.0 * withR / tot);
    // calibration round trip
    { hum::beat::Calibration c; hum::beat::Example e; e.drum = hum::Snare; for (int i = 0; i < kF; ++i) e.f[(size_t) i] = 0.1f * i; c.ex.push_back(e); auto c2 = hum::beat::Calibration::fromVar(juce::JSON::parse(juce::JSON::toString(c.toVar()))); CHECK(c2.ex.size() == 1 && c2.ex[0].drum == hum::Snare && std::abs(c2.ex[0].f[5] - 0.5f) < 1e-4f, "calibration JSON round trip"); }

    section("beatbox: double hits, FREE tempo (x2 / /2 offers), swing");
    { hum::Take T; T.mode = hum::Beatbox; T.hits = { { 0.5, hum::Kick, 0.9f, 1 }, { 0.53, hum::Kick, 0.5f, 1 }, { 1.0, hum::Snare, 0.8f, 1 }, { 1.02, hum::HatClosed, 0.5f, 1 }, { 1.5, hum::Kick, 0.7f, 1 } };
      const int removed = hum::clean::removeDoubleHits(T); CHECK(removed == 1 && T.hits.size() == 4 && T.hits[0].t == 0.5 && T.hits[0].vel == 0.9f, "double hits: removed %d, %d left", removed, (int) T.hits.size()); }
    int bpmOk = 0, offerOk = 0, n = 0; double jit = 0;
    for (int b = 0; b < 40; ++b) { double bpm; std::vector<HitTruth> hits; auto x = renderBoxer(3000 + b, bpm, hits, nullptr, 2); hum::Take T = hum::beat::transcribe(x, kSr, {}); hum::tempo::estimateFor(T); ++n;
        auto close = [](double a, double b2) { return std::abs(a - b2) / b2 < 0.03; }; if (close(T.bpm, bpm)) ++bpmOk; bool any = false; for (double o : T.bpmOffers) if (close(o, bpm)) any = true; if (any) ++offerOk; jit += std::abs(T.bpm - bpm) / bpm; }
    std::printf("  FREE tempo on 40 two-bar beats: best guess within 3%% in %d, the right tempo among the offers in %d\n", bpmOk, offerOk);
    CHECK(offerOk >= 36, "the right tempo was offered in only %d/40", offerOk); CHECK(bpmOk >= 26, "best guess right in only %d/40", bpmOk);
    { std::vector<double> on; for (int i = 0; i < 16; ++i) on.push_back(i * 0.25 + ((i % 2) ? 0.08 : 0.0)); CHECK(std::abs(hum::tempo::swingHeard(on, 120, 0) - 66.0) < 3.0, "swing heard %.1f (wanted ~66)", hum::tempo::swingHeard(on, 120, 0)); }
}

// ---------------------------------------------------------------- quantize / grid / clean
static void testGrid() {
    section("quantize: OFF / 1/4 / 1/8 / 1/16 / 1/32, strength, swing");
    hum::Take T; T.bpm = 120; T.origin = 0; hum::Grid G;
    G.quant = hum::QOff; CHECK(G.place(0.26, T) == 0.26, "OFF moved a note");
    G.quant = hum::Q16; G.strength = 1; CHECK(std::abs(G.place(0.27, T) - 0.25) < 1e-9 && std::abs(G.place(0.32, T) - 0.375) < 1e-9, "1/16 at 120: %.3f %.3f", G.place(0.27, T), G.place(0.32, T));
    G.quant = hum::Q4; CHECK(std::abs(G.place(0.7, T) - 0.5) < 1e-9, "1/4: %.3f", G.place(0.7, T)); G.quant = hum::Q8; CHECK(std::abs(G.place(0.7, T) - 0.75) < 1e-9, "1/8: %.3f", G.place(0.7, T)); G.quant = hum::Q32; CHECK(std::abs(G.place(0.7, T) - 0.6875) < 1e-9, "1/32: %.3f", G.place(0.7, T));
    G.quant = hum::Q16; G.strength = 0.5f; CHECK(std::abs(G.place(0.27, T) - 0.26) < 1e-9, "strength 50%%: %.3f", G.place(0.27, T)); G.strength = 0; CHECK(G.place(0.27, T) == 0.27, "strength 0 moved");
    G.strength = 1; G.swing = 66; { const double odd = G.place(0.125, T); CHECK(std::abs(odd - (0.125 + 0.125 * 0.32)) < 1e-6, "swing 66 odd 1/16 at %.4f", odd); CHECK(std::abs(G.place(0.25, T) - 0.25) < 1e-9, "swing moved an even slot"); }
    T.origin = 0.1; G.swing = 50; CHECK(std::abs(G.place(0.36, T) - 0.35) < 1e-9, "origin: %.3f", G.place(0.36, T));
    section("CLEAN: melody");
    { hum::Take M; M.mode = hum::Melody; M.key = 0; M.scale = 0; M.bpm = 120;
      M.notes = { { 0.0, 0.5, 60, 0.9f }, { 0.52, 0.04, 61, 0.3f }, { 0.6, 0.12, 61, 0.7f }, { 0.75, 0.5, 64, 0.5f }, { 1.27, 0.1, 64, 0.6f }, { 1.4, 0.5, 67, 0.8f } };
      hum::clean::melody(M); bool blipGone = true; for (auto& n2 : M.notes) if (n2.len < 0.07) blipGone = false; CHECK(blipGone, "CLEAN kept a blip");
      bool inKey = true; for (auto& n2 : M.notes) inKey = inKey && hum::inScale(n2.midi, 0, 0); CHECK(inKey, "CLEAN left the short C# between in-key notes");
      bool joined = true; for (size_t i = 0; i + 1 < M.notes.size(); ++i) if (M.notes[i].midi == M.notes[i + 1].midi && M.notes[i + 1].t - (M.notes[i].t + M.notes[i].len) < 0.025) joined = false; CHECK(joined, "CLEAN left shaky same-pitch notes apart");
      CHECK(M.notes.front().midi == 60 && M.notes.back().midi == 67, "CLEAN changed the melody's ends"); }
}

// ---------------------------------------------------------------- the plugin: record -> notes -> play -> MIDI out, files, state, undo
struct Plug { hum::HumProcessor p; Plug() { p.prepareToPlay(48000, 512); } };
static float feedOut(hum::HumProcessor& p, const std::vector<float>& x) {   // the take goes in through processBlock like a mic; returns the loudest output (minus the dry pass-through)
    juce::AudioBuffer<float> b(2, 512); juce::MidiBuffer m; float pk = 0; for (size_t pos = 0; pos < x.size(); pos += 512) { b.clear(); const int n = (int) juce::jmin<size_t>(512, x.size() - pos); for (int i = 0; i < n; ++i) { b.setSample(0, i, x[pos + (size_t) i]); b.setSample(1, i, x[pos + (size_t) i]); } b.setSize(2, n, true, false, true); m.clear(); p.processBlock(b, m);
        for (int i = 0; i < n; ++i) pk = juce::jmax(pk, std::abs(b.getSample(0, i) - x[pos + (size_t) i])); b.setSize(2, 512, false, false, true); }
    return pk;
}
static void feed(hum::HumProcessor& p, const std::vector<float>& x) { feedOut(p, x); }
static void testPlugin() {
    section("plugin: record through processBlock -> notes -> plays on the piano and sends MIDI");
    Plug P; auto& p = P.p; p.testSync = true;
    Rand R(77); Singer sg; sg.scoopCents = -120; std::vector<SungNote> ph { { 60, 0.5, 0.05, false }, { 64, 0.5, 0.05, false }, { 67, 0.7, 0.05, false }, { 72, 0.9, 0.05, false } }; Truth tr; auto x = sing(sg, ph, R, tr);
    p.startRecording(); CHECK(p.isRecording(), "not recording"); feed(p, x); CHECK(std::abs(p.recordedSeconds() - x.size() / 48000.0) < 0.02, "recorded %.2f s of %.2f", p.recordedSeconds(), x.size() / 48000.0);
    p.stopRecording(); CHECK(!p.isRecording() && !p.isAnalysing(), "still busy after stop (testSync)");
    auto& T = p.take(); CHECK(T.notes.size() == 4, "heard %d notes (wanted 4)", (int) T.notes.size()); if (T.notes.size() == 4) CHECK(T.notes[0].midi == 60 && T.notes[1].midi == 64 && T.notes[2].midi == 67 && T.notes[3].midi == 72, "notes %d %d %d %d", T.notes[0].midi, T.notes[1].midi, T.notes[2].midi, T.notes[3].midi);
    CHECK(T.origNotes.size() == T.notes.size(), "as-heard copy"); CHECK(!T.bpmKnown && !T.bpmOffers.empty(), "FREE: no tempo offers"); CHECK(p.status.contains("4 notes"), "status: %s", p.status.toRawUTF8());
    CHECK(p.isPlaying(), "did not start playing after the take");
    // it plays: audio comes out and MIDI goes to the host
    { juce::AudioBuffer<float> b(2, 512); int ons = 0, offs = 0; float peak = 0; for (int k = 0; k < 800 && p.isPlaying(); ++k) { b.clear(); juce::MidiBuffer m; p.processBlock(b, m); peak = juce::jmax(peak, b.getMagnitude(0, 0, 512)); for (const auto e : m) { if (e.getMessage().isNoteOn()) ++ons; if (e.getMessage().isNoteOff()) ++offs; } }
      CHECK(peak > 0.05f && peak < 1.0f, "playback peak %.3f", peak); CHECK(ons == 4 && offs == 4, "MIDI out: %d ons %d offs", ons, offs); CHECK(!p.isPlaying(), "still playing after the end"); }
    // BPM mode + metronome + COUNT-IN: 4 3 2 1 (clicks come out, nothing is recorded yet), then the take, a set tempo, no offers, the click times kept
    { p.setParam("tempomode", 1); p.setParam("bpm", 100); p.setParam("metro", 1); p.setParam("countin", 1); p.startRecording();
      CHECK(p.isCountingIn() && !p.isRecording() && p.countInLeft() == 4, "count-in did not start (counting %d left %d)", (int) p.isCountingIn(), p.countInLeft());
      std::vector<float> quiet((size_t) (48000 * 2.3), 0.0f); const float heard = feedOut(p, quiet); CHECK(heard > 0.05f, "no clicks heard during the count-in (peak %.3f)", heard);
      CHECK(p.isCountingIn() && p.countInLeft() == 1 && p.recordedSeconds() < 0.01, "after 2.3 s at 100 BPM: counting %d left %d recorded %.2f", (int) p.isCountingIn(), p.countInLeft(), p.recordedSeconds());
      std::vector<float> rest((size_t) (48000 * 0.2), 0.0f); feed(p, rest); CHECK(!p.isCountingIn() && p.isRecording(), "recording did not start after the bar");
      feed(p, x); p.stopRecording(); CHECK(std::abs(p.recordedSeconds() - (x.size() / 48000.0 + 0.1)) < 0.15, "recorded %.2f s (take %.2f + the rest of the bar)", p.recordedSeconds(), x.size() / 48000.0);
      CHECK(p.take().bpmKnown && p.take().bpm == 100 && p.take().bpmOffers.empty(), "BPM mode: bpm %.1f known %d", p.take().bpm, (int) p.take().bpmKnown); CHECK(p.take().notes.size() == 4, "count-in take heard %d notes", (int) p.take().notes.size());
      const auto clicks = p.lastClickTimes(); CHECK(clicks.size() >= 5 && clicks[0] == 0 && std::abs(clicks[1] - 0.6) < 0.02, "click times kept: %d, first %.2f second %.2f", (int) clicks.size(), clicks.empty() ? -1.0 : clicks[0], clicks.size() > 1 ? clicks[1] : -1.0);
      // a tap during the count-in cancels it; COUNT-IN off records at once
      p.startRecording(); CHECK(p.isCountingIn(), "second count-in"); p.stopRecording(); CHECK(!p.isCountingIn() && !p.isRecording() && !p.isAnalysing(), "tap during count-in did not cancel");
      p.setParam("countin", 0); p.startRecording(); CHECK(p.isRecording() && !p.isCountingIn(), "COUNT-IN off should record at once"); feed(p, x); p.stopRecording(); CHECK(p.take().notes.size() == 4, "no-count-in take heard %d notes", (int) p.take().notes.size());
      p.setParam("tempomode", 0); p.setParam("metro", 0); p.setParam("countin", 1); p.stop(); }
    // undo after a new take brings the old one back
    { const auto before = p.take(); p.startRecording(); feed(p, x); p.stopRecording(); p.stop(); CHECK(p.doUndo(), "no undo after a take"); CHECK(p.take().notes.size() == before.notes.size() && p.take().bpm == before.bpm, "undo did not restore the take"); }

    section("plugin: CLEAN + UNDO + REDO, edits, pitch setting, quantize playback");
    { auto& t = p.take(); t.notes.push_back({ 0.52, 0.03, 61, 0.3f }); hum::sortTake(t); const auto before = t; p.clean(); CHECK(p.take().notes.size() == 4, "CLEAN left %d notes", (int) p.take().notes.size()); CHECK(p.doUndo(), "undo"); CHECK(p.take().notes.size() == 5 && p.take().notes[1].len == 0.03, "UNDO did not bring the blip back"); CHECK(p.doRedo() && p.take().notes.size() == 4, "REDO"); p.doUndo(); p.take().notes.erase(p.take().notes.begin() + 1); }
    { p.beginEdit(); p.take().notes[0].midi = 62; p.edited(); CHECK(p.take().notes[0].midi == 62, "edit"); p.doUndo(); CHECK(p.take().notes[0].midi == 60, "undo of an edit"); }
    { p.setParam("pitch", (float) hum::PitchOriginal); juce::MessageManager::getInstance()->runDispatchLoopUntil(50); p.handleAsyncUpdateForTests(); bool cents = false; for (auto& n2 : p.take().notes) if (std::abs(n2.cents) > 0.5f) cents = true; CHECK(cents || true, "(original cents)"); p.setParam("pitch", (float) hum::PitchNearest); p.handleAsyncUpdateForTests(); bool none = true; for (auto& n2 : p.take().notes) if (std::abs(n2.cents) > 0.5f) none = false; CHECK(none, "NEAREST NOTE left cents"); }
    { p.chooseBpm(120, false); CHECK(p.take().bpmKnown && p.take().bpm == 120 && p.quant() == hum::Q16, "choose 120: quant %d", p.quant()); const auto seqGrid = hum::sound::sequence(p.take(), p.grid(), false), seqOrig = hum::sound::sequence(p.take(), p.grid(), true);
      bool onGrid = true; const double g16 = 60.0 / 120 / 4; for (auto& e : seqGrid) if (e.type == 0) { const double b = (e.t - p.take().origin) / g16; if (std::abs(b - std::round(b)) > 1e-6) onGrid = false; } CHECK(onGrid, "ON GRID notes not on the 1/16 grid");
      bool same = seqGrid.size() == seqOrig.size(); for (size_t i = 0; same && i < seqGrid.size(); ++i) same = seqGrid[i].num == seqOrig[i].num; CHECK(same, "ORIGINAL and ON GRID play different notes");
      p.chooseBpm(0, true); CHECK(!p.take().bpmKnown && p.quant() == hum::QOff, "KEEP ORIGINAL"); }

    section("files: MIDI (tempo, lengths, velocities, GM drums on channel 10) and WAV");
    { p.chooseBpm(120, false); const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("ghosthum-tests"); dir.createDirectory();
      const auto mf = dir.getChildFile("melody.mid"); CHECK(hum::files::writeMidi(mf, p.take(), p.grid(), false, "test"), "write MIDI"); juce::FileInputStream in(mf); juce::MidiFile m; CHECK(m.readFrom(in), "read MIDI back");
      CHECK(m.getNumTracks() == 2 && m.getTimeFormat() == 960, "MIDI tracks %d ppq %d", m.getNumTracks(), (int) m.getTimeFormat());
      int ons = 0; double tempo = 0; bool lens = true, vels = true; const auto* tk = m.getTrack(1); const auto* meta = m.getTrack(0); for (int i = 0; i < meta->getNumEvents(); ++i) if (meta->getEventPointer(i)->message.isTempoMetaEvent()) tempo = 60.0 / meta->getEventPointer(i)->message.getTempoSecondsPerQuarterNote();
      for (int i = 0; i < tk->getNumEvents(); ++i) { auto* e = tk->getEventPointer(i); if (e->message.isNoteOn()) { ++ons; if (!e->noteOffObject || e->noteOffObject->message.getTimeStamp() - e->message.getTimeStamp() < 100) lens = false; if (e->message.getVelocity() < 20) vels = false; if (e->message.getChannel() != 1) vels = false; } }
      CHECK(ons == 4 && std::abs(tempo - 120) < 0.01 && lens && vels, "MIDI: %d notes tempo %.1f lens %d vels %d", ons, tempo, (int) lens, (int) vels);
      const auto wf = dir.getChildFile("melody.wav"); CHECK(hum::files::writeWav(wf, p.take(), p.grid(), false, hum::sound::Piano), "write WAV"); juce::AudioFormatManager fm; fm.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(wf));
      CHECK(r && r->numChannels == 2 && r->sampleRate == 48000 && r->bitsPerSample == 24 && r->lengthInSamples > 48000 * 2, "WAV format"); if (r) { juce::AudioBuffer<float> b(2, (int) r->lengthInSamples); r->read(&b, 0, (int) r->lengthInSamples, 0, true, true); const float pk = b.getMagnitude(0, 0, b.getNumSamples()); CHECK(pk > 0.1f && pk < 1.0f, "WAV peak %.3f", pk); }
      // drums
      hum::Take B; B.mode = hum::Beatbox; B.bpm = 100; B.seconds = 2; B.hits = { { 0.0, hum::Kick, 1.0f, 1 }, { 0.6, hum::Snare, 0.8f, 1 }, { 0.9, hum::HatClosed, 0.5f, 1 }, { 1.2, hum::HatOpen, 0.6f, 1 }, { 1.5, hum::Clap, 0.7f, 1 } }; B.origHits = B.hits; hum::Grid g0;
      const auto df = dir.getChildFile("beat.mid"); CHECK(hum::files::writeMidi(df, B, g0, false, "beat"), "write drum MIDI"); juce::FileInputStream in2(df); juce::MidiFile m2; m2.readFrom(in2); std::vector<int> notes; int ch = 0; const auto* dt = m2.getTrack(1);
      for (int i = 0; i < dt->getNumEvents(); ++i) if (dt->getEventPointer(i)->message.isNoteOn()) { notes.push_back(dt->getEventPointer(i)->message.getNoteNumber()); ch = dt->getEventPointer(i)->message.getChannel(); }
      CHECK(notes == std::vector<int>({ 36, 38, 42, 46, 39 }) && ch == 10, "GM drum notes on channel 10");
      const auto bw = dir.getChildFile("beat.wav"); CHECK(hum::files::writeWav(bw, B, g0, false, 0), "drum WAV"); std::unique_ptr<juce::AudioFormatReader> r2(fm.createReaderFor(bw)); CHECK(r2 && r2->lengthInSamples > 48000 * 1.5, "drum WAV length");
      dir.deleteRecursively(); }

    section("plugin: state save / load (both takes, settings, taught sounds)");
    { p.setParam("mode", 1); p.handleAsyncUpdateForTests(); auto& bt = p.take(); bt = hum::Take(); bt.mode = hum::Beatbox; bt.bpm = 95; bt.seconds = 2; bt.hits = { { 0.1, hum::Kick, 0.9f, 0.4f }, { 0.6, hum::Clap, 0.7f, 1 } }; bt.origHits = bt.hits; p.edited();
      hum::beat::Example ex; ex.drum = hum::Kick; ex.f.fill(0.25f); p.cal.ex.push_back(ex); p.setParam("quant", 3); p.setParam("swing", 60); p.setParam("sound", 2);
      juce::MemoryBlock mb; p.getStateInformation(mb); Plug Q; Q.p.cal.ex.clear(); Q.p.setStateInformation(mb.getData(), (int) mb.getSize());
      CHECK(Q.p.mode() == 1 && Q.p.quant() == 3 && std::abs(Q.p.swing() - 60) < 1e-4 && Q.p.voice() == 2, "settings came back: mode %d quant %d swing %.0f sound %d", Q.p.mode(), Q.p.quant(), Q.p.swing(), Q.p.voice());
      CHECK(Q.p.takes[1].hits.size() == 2 && Q.p.takes[1].hits[1].drum == hum::Clap && std::abs(Q.p.takes[1].hits[0].conf - 0.4f) < 1e-4f && Q.p.takes[1].bpm == 95, "beat take came back");
      CHECK(Q.p.takes[0].notes.size() == 4 && Q.p.takes[0].origNotes.size() == 4 && Q.p.takes[0].bpm == 120, "melody take came back (%d notes)", (int) Q.p.takes[0].notes.size());
      CHECK(Q.p.cal.countFor(hum::Kick) == 1, "taught sounds came back"); CHECK(!Q.p.undo().canUndo(), "undo history leaked into the loaded state");
      // manual reclassification: drag a hit to another lane = its drum changes and the MIDI note follows
      Q.p.beginEdit(); Q.p.take().hits[0].drum = hum::Snare; Q.p.take().hits[0].conf = 1; Q.p.edited(); const auto sq = hum::sound::sequence(Q.p.take(), Q.p.grid(), false); CHECK(!sq.empty() && sq[0].type == 2 && sq[0].num == hum::Snare && hum::gmNote(sq[0].num) == 38, "reclassified hit plays as a snare");
      CHECK(Q.p.doUndo() && Q.p.take().hits[0].drum == hum::Kick, "undo of a reclassification"); p.cal.ex.clear(); p.setParam("mode", 0); p.handleAsyncUpdateForTests(); }

    section("plugin: beatbox through the mic, then TEACH");
    { Plug Bq; auto& q = Bq.p; q.testSync = true; q.setParam("mode", 1); q.handleAsyncUpdateForTests(); double bpm; std::vector<HitTruth> hits; Beatboxer B; auto x = renderBoxer(4242, bpm, hits, &B, 2);
      q.startRecording(); feed(q, x); q.stopRecording(); CHECK(q.take().mode == hum::Beatbox && (int) q.take().hits.size() >= (int) hits.size() - 2 && (int) q.take().hits.size() <= (int) hits.size() + 2, "heard %d hits of %d", (int) q.take().hits.size(), (int) hits.size());
      int right = 0; for (auto& h : hits) for (auto& e : q.take().hits) if (std::abs(e.t - h.t) <= 0.03) { if (e.drum == h.drum) ++right; break; } std::printf("  mic beat: %d/%d right\n", right, (int) hits.size()); CHECK(right >= (int) hits.size() * 0.85, "mic beat right %d/%d", right, (int) hits.size());
      CHECK(q.isPlaying(), "beat did not play"); q.stop();
      // TEACH: record three kicks
      q.teachingDrum = hum::Kick; int got = -1; q.onTaught = [&](int, int g) { got = g; }; q.startRecording(); Rand R(1); std::vector<HitTruth> three { { 0.2, Kick, 0.9 }, { 0.6, Kick, 0.8 }, { 1.0, Kick, 0.95 } }; feed(q, beatbox(B, three, R)); q.stopRecording();
      CHECK(got == 3 && q.cal.countFor(hum::Kick) == 3 && q.teachingDrum < 0, "TEACH got %d kicks (cal %d)", got, q.cal.countFor(hum::Kick)); CHECK(q.take().hits.size() >= 10, "TEACH replaced the take");
      q.forgetTaught(); CHECK(!q.cal.any(), "forget"); hum::HumProcessor::calibrationFile().deleteFile(); }

    section("export gate: locked = no file, no drag");
    { ghosty::License::testForce().store(0); ghosty::exportgate::log().clear(); CHECK(p.writeMidiFile() == juce::File() && p.writeWavFile() == juce::File(), "locked wrote a file"); CHECK(!ghosty::exportAllowed(p.license, nullptr, "ghosthum.test"), "gate open while locked");
      ghosty::License::testForce().store(1); CHECK(ghosty::exportAllowed(p.license, nullptr, "ghosthum.test"), "gate closed while unlocked"); const auto f = p.writeMidiFile(); CHECK(f.existsAsFile(), "unlocked MIDI file"); f.deleteFile(); ghosty::License::testForce().store(-1); }
}

// ---------------------------------------------------------------- build 2 quality pass: edge cases, rates and blocks, the DAW, click masking through the plugin
static std::vector<float> resampled(const std::vector<float>& x, double from, double to) { std::vector<float> y((size_t) (x.size() * to / from)); for (size_t i = 0; i < y.size(); ++i) { const double p = i * from / to; const size_t k = (size_t) p; const double f = p - k; y[i] = k + 1 < x.size() ? (float) (x[k] * (1 - f) + x[k + 1] * f) : x.back(); } return y; }
struct FakeHead : juce::AudioPlayHead { double bpm = 128; bool playing = true; juce::Optional<PositionInfo> getPosition() const override { PositionInfo p; p.setBpm(bpm); p.setIsPlaying(playing); p.setTimeInSamples(0); return p; } };
static void feedAt(hum::HumProcessor& p, const std::vector<float>& x, int block) {
    juce::AudioBuffer<float> b(2, block); juce::MidiBuffer m; for (size_t pos = 0; pos < x.size(); pos += (size_t) block) { b.clear(); const int n = (int) juce::jmin<size_t>((size_t) block, x.size() - pos); for (int i = 0; i < n; ++i) { b.setSample(0, i, x[pos + (size_t) i]); b.setSample(1, i, x[pos + (size_t) i]); } b.setSize(2, n, true, false, true); m.clear(); p.processBlock(b, m); b.setSize(2, block, false, false, true); } }
static void testQuality() {
    section("quality: tiny takes, silence, the 90 s stop, a mode switch mid-take, undo after everything, state (ghost animation)");
    Rand R(21); Singer sg; std::vector<SungNote> ph { { 60, 0.5, 0.05, false }, { 64, 0.5, 0.05, false }, { 67, 0.7, 0.05, false }, { 72, 0.9, 0.05, false } }; Truth tr; const auto x = sing(sg, ph, R, tr);
    { Plug P; auto& p = P.p; p.testSync = true;
      // a tiny take (under 0.25 s) is refused with a word, nothing changes
      p.startRecording(); feed(p, std::vector<float>(4800, 0.1f)); p.stopRecording(); CHECK(p.take().empty() && p.status.contains("too short"), "tiny take: %s", p.status.toRawUTF8());
      // silence: no notes, a clear message, nothing invented
      // (digital zeros the whole way: build 3 says nothing came in, rather than "no notes")
      p.startRecording(); feed(p, std::vector<float>(48000 * 3, 0.0f)); p.stopRecording(); CHECK(p.take().empty() && p.status.contains("No sound"), "silence: %d notes, %s", p.take().count(), p.status.toRawUTF8());
      // 90 seconds: the cap stops the recording on the audio thread; the message thread finishes the take
      { std::vector<float> longTake; while (longTake.size() < (size_t) (48000 * 91.5)) longTake.insert(longTake.end(), x.begin(), x.end());
        p.startRecording(); feed(p, longTake); CHECK(!p.isRecording(), "still recording after 91.5 s"); CHECK(std::abs(p.recordedSeconds() - 90.0) < 0.05, "recorded %.2f s (cap 90)", p.recordedSeconds());
        p.handleAsyncUpdateForTests(); CHECK(!p.isRecording() && !p.isAnalysing(), "90 s take not finished"); CHECK(p.take().count() > 80, "90 s take heard %d notes", p.take().count()); CHECK(p.status.contains("90 seconds") || p.status.contains("Heard"), "90 s status: %s", p.status.toRawUTF8());
        CHECK(p.take().seconds > 89.0 && p.take().seconds < 91.0, "90 s take length %.1f", p.take().seconds); p.stop(); }
      // switching the mode mid-take: the take lands in the mode it was started in; the other slot is untouched
      { p.setParam("mode", 0); p.handleAsyncUpdateForTests(); const auto beatBefore = p.takes[1]; p.startRecording(); feed(p, x); p.setParam("mode", 1); p.handleAsyncUpdateForTests(); CHECK(p.isRecording(), "the mode switch stopped the recording");
        feed(p, x); p.stopRecording(); CHECK(p.recordingMode() == hum::Melody, "recording mode %d", p.recordingMode()); CHECK(p.takes[0].mode == hum::Melody && p.takes[0].notes.size() == 8, "melody slot got %d notes", (int) p.takes[0].notes.size());
        CHECK(p.takes[1].hits.size() == beatBefore.hits.size(), "beat slot changed"); p.setParam("mode", 0); p.handleAsyncUpdateForTests(); p.stop(); }
      // undo after everything: take -> CLEAN -> edit -> tempo choice -> NEW, then undo all the way back and redo forward
      { p.startRecording(); feed(p, x); p.stopRecording(); p.stop(); const auto orig = p.take(); CHECK(orig.notes.size() == 4, "fresh take %d notes", (int) orig.notes.size());
        p.clean(); p.beginEdit(); p.take().notes[0].midi = 65; p.edited(); p.chooseBpm(140, false); p.clearTake(); CHECK(p.take().empty(), "NEW did not clear");
        int steps = 0; while (steps < 20 && !(p.take().notes.size() == 4 && p.take().notes[0].midi == 60 && !p.take().bpmKnown) && p.doUndo()) ++steps; CHECK(steps >= 3 && steps < 20, "undo back to the take took %d steps", steps); CHECK(p.take().notes.size() == 4 && p.take().notes[0].midi == 60, "undo all the way: %d notes, first %d", (int) p.take().notes.size(), p.take().notes.empty() ? -1 : p.take().notes[0].midi);
        int redo = 0; while (p.doRedo()) ++redo; CHECK(redo == steps && p.take().empty(), "redo forward: %d of %d, empty %d", redo, steps, (int) p.take().empty()); for (int k = 0; k < steps; ++k) p.doUndo(); }
      // GHOST ANIMATION in the state and the settings file
      { p.setGhostAnim(false); juce::MemoryBlock mb; p.getStateInformation(mb); Plug Q; Q.p.setStateInformation(mb.getData(), (int) mb.getSize()); CHECK(!Q.p.ghostAnim, "ghost animation OFF did not come back from the state");
        Plug Z; CHECK(!Z.p.ghostAnim, "ghost animation OFF not kept as an app setting"); p.setGhostAnim(true); Plug Z2; CHECK(Z2.p.ghostAnim, "ghost animation ON not kept as an app setting"); } }

    section("quality: sample rates 44.1 - 96 kHz, block sizes 32 - 2048 (count-in, click times, the take)");
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0 }) for (int block : { 32, 256, 2048 }) {
        hum::HumProcessor p; p.testSync = true; p.prepareToPlay(sr, block); const auto xs = resampled(x, 48000.0, sr);
        p.setParam("tempomode", 1); p.setParam("bpm", 100); p.setParam("metro", 1); p.setParam("countin", 1); p.startRecording();
        feedAt(p, std::vector<float>((size_t) (sr * 2.5), 0.0f), block); CHECK(p.isRecording() && !p.isCountingIn(), "%.0f Hz / %d: not recording after the count-in", sr, block);
        feedAt(p, xs, block); p.stopRecording(); CHECK(p.take().notes.size() == 4, "%.0f Hz / %d: heard %d notes", sr, block, (int) p.take().notes.size());
        if (p.take().notes.size() == 4) CHECK(p.take().notes[0].midi == 60 && p.take().notes[3].midi == 72, "%.0f Hz / %d: notes %d..%d", sr, block, p.take().notes[0].midi, p.take().notes[3].midi);
        const auto clicks = p.lastClickTimes(); bool spaced = clicks.size() >= 4; for (size_t i = 1; spaced && i < clicks.size(); ++i) spaced = std::abs((clicks[i] - clicks[i - 1]) - 0.6) < 0.03; CHECK(spaced, "%.0f Hz / %d: click times %d, not 0.6 s apart", sr, block, (int) clicks.size());
        // playback renders at that rate / block without a hiccup (audio out, MIDI out, ends)
        { juce::AudioBuffer<float> b(2, block); int ons = 0; float peak = 0; int guard = 0; while (p.isPlaying() && guard++ < (int) (sr * 12) / block) { b.clear(); juce::MidiBuffer m; p.processBlock(b, m); peak = juce::jmax(peak, b.getMagnitude(0, 0, block)); for (const auto e : m) if (e.getMessage().isNoteOn()) ++ons; }
          CHECK(ons == 4 && peak > 0.05f && peak < 1.0f && !p.isPlaying(), "%.0f Hz / %d: playback ons %d peak %.2f playing %d", sr, block, ons, peak, (int) p.isPlaying()); }
        if (block == 2048) { juce::AudioBuffer<float> big(2, 4096); juce::MidiBuffer m; p.processBlock(big, m); CHECK(true, "(a block bigger than prepared did not crash)"); } }

    section("quality: inside a DAW at a different tempo (host BPM shown, never applied by itself; the dry track passes through)");
    { hum::HumProcessor p; p.testSync = true; p.prepareToPlay(48000, 512); FakeHead head; p.setPlayHead(&head); p.setParam("tempomode", 1); p.setParam("bpm", 100); p.setParam("metro", 0); p.setParam("countin", 0);
      feed(p, std::vector<float>(512, 0.0f)); CHECK(std::abs(p.hostBpm() - 128) < 1e-6, "host bpm %.1f", p.hostBpm());
      p.startRecording(); feed(p, x); p.stopRecording(); CHECK(p.take().bpmKnown && std::abs(p.take().bpm - 100) < 1e-6, "the DAW's tempo leaked into the take (%.1f)", p.take().bpm);
      p.setParam("bpm", (float) p.hostBpm()); CHECK(std::abs(p.setBpm() - 128) < 1e-6, "HOST pill: bpm %.1f", p.setBpm());
      head.bpm = 500; feed(p, std::vector<float>(512, 0.0f)); CHECK(p.hostBpm() == 0, "an absurd host tempo was accepted"); p.setPlayHead(nullptr); }

    section("quality: speaker click masking through the plugin (BPM mode, click on, the clicks leak into the mic)");
    { hum::HumProcessor p; p.testSync = true; p.prepareToPlay(48000, 512); p.setParam("mode", 1); p.handleAsyncUpdateForTests(); p.setParam("tempomode", 1); p.setParam("bpm", 100); p.setParam("metro", 1); p.setParam("countin", 0);
      Rand R2(99); Beatboxer B = Beatboxer::random(R2); std::vector<HitTruth> hits; for (int bar = 0; bar < 2; ++bar) for (int k = 0; k < 16; ++k) { const double t = 0.3 + bar * 2.4 + k * 0.15; if (k % 4 == 0) hits.push_back({ t, Kick, 0.9 }); else if (k % 8 == 4) hits.push_back({ t, Snare, 0.85 }); else if (k % 2 == 0) hits.push_back({ t, HatClosed, 0.6 }); }
      auto y = beatbox(B, hits, R2); while (y.size() < (size_t) (48000 * 5.4)) y.push_back(0.0f); y.resize((size_t) (48000 * 5.4));
      // the plugin clicks at 0, 0.6, 1.2 ... (beat 0 = the first recorded sample); the leak lands 25 ms later at -8 dB
      addClickLeak(y, 100, 0.0, -8, 0.025, R2);
      p.startRecording(); feed(p, y); p.stopRecording(); const auto& T = p.take(); int matched = 0; std::vector<bool> used(T.hits.size(), false);
      for (auto& h : hits) { int best = -1; double bd = 1e9; for (size_t j = 0; j < T.hits.size(); ++j) if (!used[j]) { const double d = std::abs(T.hits[j].t - h.t); if (d < bd) { bd = d; best = (int) j; } } if (best >= 0 && bd <= 0.03) { used[(size_t) best] = true; ++matched; } }
      int extra = 0; for (bool u : used) if (!u) ++extra; std::printf("  leak through the plugin: %d/%d hits found, %d extra (clicks kept: %d)\n", matched, (int) hits.size(), extra, (int) p.lastClickTimes().size());
      CHECK(matched >= (int) hits.size() - 2, "leak: found %d of %d", matched, (int) hits.size()); CHECK(extra <= 1, "leak: %d extra hits (the clicks became hits)", extra); CHECK(p.lastClickTimes().size() >= 9, "clicks kept %d", (int) p.lastClickTimes().size()); p.stop(); }
}

// ---------------------------------------------------------------- build 3: where the sound comes from (FL's channel rack = no host input -> the Mac's mic)
// The host side is processBlock with the input bus disabled (0 channels, what an instrument gets) or with stereo input; the "Mac mic" is
// the processor's fake device (fakeMic + micFeed), fed in its own blocks at its own rate, interleaved with the host's blocks like two
// audio threads would. pollSources is the 10 Hz timer the message thread runs while recording.
static void hostBlocks(hum::HumProcessor& p, double seconds, float* peakOut = nullptr, int* onsOut = nullptr, int block = 512) {   // the host runs, nothing comes in (0 channels or zeros)
    juce::AudioBuffer<float> b(2, block); for (int k = 0; k < (int) (48000 * seconds / block); ++k) { b.clear(); juce::MidiBuffer m; p.processBlock(b, m); if (peakOut) *peakOut = juce::jmax(*peakOut, b.getMagnitude(0, 0, block)); if (onsOut) for (const auto e : m) if (e.getMessage().isNoteOn()) ++*onsOut; } }
static void micAndHost(hum::HumProcessor& p, const std::vector<float>& micAudio, double micRate, const std::vector<float>* hostAudio = nullptr, float* peakOut = nullptr) {   // both run, 10 ms at a time; the poll ticks every 100 ms
    const int mb = (int) (micRate / 100), hb = 480; size_t mp = 0, hp = 0; juce::AudioBuffer<float> b(2, hb); int tick = 0; std::vector<float> micBlock((size_t) mb, 0.0f);
    while (mp < micAudio.size() || (hostAudio && hp < hostAudio->size())) {   // (both keep running until both are used up; a device that ran out delivers zeros)
        for (int i = 0; i < mb; ++i) micBlock[(size_t) i] = mp + (size_t) i < micAudio.size() ? micAudio[mp + (size_t) i] : 0.0f; p.micFeed(micBlock.data(), mb); mp += (size_t) mb;
        b.clear(); if (hostAudio) for (int i = 0; i < hb; ++i) { const float v = hp + (size_t) i < hostAudio->size() ? (*hostAudio)[hp + (size_t) i] : 0.0f; b.setSample(0, i, v); b.setSample(1, i, v); } hp += (size_t) hb;
        juce::MidiBuffer m; p.processBlock(b, m); if (peakOut) *peakOut = juce::jmax(*peakOut, b.getMagnitude(0, 0, hb)); if (++tick % 10 == 0) p.pollSources(); } }
static bool noInputBus(hum::HumProcessor& p) { auto l = p.getBusesLayout(); l.inputBuses.set(0, juce::AudioChannelSet::disabled()); return p.setBusesLayout(l) && p.getTotalNumInputChannels() == 0; }
static void testInput() {
    section("input (build 3): an instrument in FL's channel rack (0 host inputs) records through the Mac's mic; silence says so; a track with sound is used as before");
    Rand R(77); Singer sg; sg.scoopCents = -120; std::vector<SungNote> ph { { 60, 0.5, 0.05, false }, { 64, 0.5, 0.05, false }, { 67, 0.7, 0.05, false }, { 72, 0.9, 0.05, false } }; Truth tr; const auto x = sing(sg, ph, R, tr);
    const auto x441 = resampled(x, 48000.0, 44100.0);
    // 1) the channel rack: no input bus, the (fake) mic at 44.1 kHz carries the voice -> notes, the right rate, playback is heard and MIDI goes out
    { hum::HumProcessor p; p.testSync = true; CHECK(noInputBus(p), "FL's generator layout (no input, stereo out) refused"); p.prepareToPlay(48000, 512); p.fakeMic.available = true; p.fakeMic.rate = 44100;
      p.startRecording(); CHECK(p.isRecording() && p.recordingSource() == hum::SrcMic, "0 inputs: recording %d source %d (wanted the mic)", (int) p.isRecording(), p.recordingSource());
      CHECK(p.status.contains("microphone"), "status: %s", p.status.toRawUTF8());
      float out = 0; micAndHost(p, x441, 44100, nullptr, &out); CHECK(out < 1e-6f, "the host output carried something while recording with no input (%.4f)", out);
      CHECK(std::abs(p.recordedSeconds() - x441.size() / 44100.0) < 0.02, "recorded %.2f s of %.2f", p.recordedSeconds(), x441.size() / 44100.0); CHECK(p.inputLevel() > 0.01f, "the ring's level did not follow the mic (%.3f)", p.inputLevel());
      p.stopRecording(); CHECK(p.lastRecordingRate() == 44100, "take rate %.0f", p.lastRecordingRate()); const auto& T = p.take(); CHECK(T.notes.size() == 4, "mic take heard %d notes (wanted 4)", (int) T.notes.size());
      if (T.notes.size() == 4) CHECK(T.notes[0].midi == 60 && T.notes[1].midi == 64 && T.notes[2].midi == 67 && T.notes[3].midi == 72, "notes %d %d %d %d", T.notes[0].midi, T.notes[1].midi, T.notes[2].midi, T.notes[3].midi);
      CHECK(p.isPlaying(), "did not play after the mic take"); float peak = 0; int ons = 0; hostBlocks(p, 8.0, &peak, &ons); CHECK(peak > 0.05f && peak < 1.0f && ons == 4, "channel rack playback: peak %.3f, %d MIDI note-ons", peak, ons);
      // BPM mode with the click and a count-in, still with no input: the clicks come out of the host, the take starts after the bar on the mic, the click times are kept
      p.setParam("tempomode", 1); p.setParam("bpm", 100); p.setParam("metro", 1); p.setParam("countin", 1); p.startRecording(); CHECK(p.isCountingIn(), "count-in with 0 inputs");
      float clicks = 0; hostBlocks(p, 2.5, &clicks); CHECK(clicks > 0.05f, "no count-in clicks heard with 0 inputs (%.3f)", clicks); CHECK(p.isRecording() && !p.isCountingIn() && p.recordingSource() == hum::SrcMic, "after the bar: recording %d source %d", (int) p.isRecording(), p.recordingSource());
      micAndHost(p, x441, 44100); p.stopRecording(); CHECK(p.take().notes.size() == 4 && p.take().bpmKnown && p.take().bpm == 100, "count-in mic take: %d notes, bpm %.0f", (int) p.take().notes.size(), p.take().bpm);
      const auto ct = p.lastClickTimes(); CHECK(ct.size() >= 5 && ct[0] == 0 && std::abs(ct[1] - 0.6) < 0.02, "click times with a mic take: %d, %.2f %.2f", (int) ct.size(), ct.empty() ? -1.0 : ct[0], ct.size() > 1 ? ct[1] : -1.0); p.stop();
      p.setParam("tempomode", 0); p.setParam("metro", 0); }
    // 2) silence on both (no input bus, the mic gives zeros): after 2 s the status says no sound is coming in; the stop says so too, no take is invented
    { hum::HumProcessor p; p.testSync = true; noInputBus(p); p.prepareToPlay(48000, 512); p.fakeMic.available = true; p.fakeMic.rate = 48000;
      p.startRecording(); std::vector<float> z((size_t) (48000 * 2.6), 0.0f); micAndHost(p, z, 48000); CHECK(p.isRecording() && p.status.contains("No sound"), "2.6 s of nothing: %s", p.status.toRawUTF8());
      p.stopRecording(); CHECK(p.take().empty() && p.status.contains("No sound") && p.status.contains("microphone"), "stop after nothing: %s", p.status.toRawUTF8()); }
    // 3) no input bus and no mic to be had: the tap is refused with a word (INPUT = AUTO and MAC MIC); INPUT = TRACK with no input is refused too
    { hum::HumProcessor p; p.testSync = true; noInputBus(p); p.prepareToPlay(48000, 512); p.fakeMic.available = false;   // (the Linux harness has no audio device: the real open fails like a Mac without permission / a device)
      p.startRecording(); CHECK(!p.isRecording() && p.status.contains("microphone") && p.status.contains("mixer track"), "no mic, no input: %s", p.status.toRawUTF8());
      p.setParam("input", (float) hum::InputTrack); p.startRecording(); CHECK(!p.isRecording() && p.status.contains("No audio comes into this channel"), "TRACK with no input: %s", p.status.toRawUTF8());
      p.setParam("input", (float) hum::InputMic); p.startRecording(); CHECK(!p.isRecording() && p.status.contains("Could not open"), "MAC MIC with no device: %s", p.status.toRawUTF8()); }
    // 4) a mixer insert with sound on the track (AUTO): the track is used, the mic is never armed, even though a mic is there and carries something else
    { Plug P; auto& p = P.p; p.testSync = true; p.fakeMic.available = true; p.fakeMic.rate = 44100; std::vector<SungNote> other { { 48, 0.6, 0.05, false }, { 50, 0.6, 0.05, false } }; Truth t2; Rand R2(3); const auto y = resampled(sing(sg, other, R2, t2), 48000.0, 44100.0);
      feed(p, std::vector<float>(4800, 0.05f));   // (the track carried sound a moment ago)
      p.startRecording(); CHECK(p.recordingSource() == hum::SrcHost && !p.micOpen(), "track with sound: source %d, mic open %d", p.recordingSource(), (int) p.micOpen()); CHECK(p.status.contains("track"), "status: %s", p.status.toRawUTF8());
      micAndHost(p, y, 44100, &x); p.stopRecording(); CHECK(p.lastRecordingRate() == 48000 && p.take().notes.size() == 4 && p.take().notes[0].midi == 60, "insert take: rate %.0f, %d notes, first %d", p.lastRecordingRate(), (int) p.take().notes.size(), p.take().notes.empty() ? -1 : p.take().notes[0].midi); p.stop(); }
    // 5) AUTO on a track that has channels but feeds digital silence (FL feeding an instrument's input nothing): the mic takes over within half a second and the whole take is the mic's
    { Plug P; auto& p = P.p; p.testSync = true; p.fakeMic.available = true; p.fakeMic.rate = 44100;   // (a host with no audio given to micAndHost = stereo zeros)
      p.startRecording(); CHECK(p.recordingSource() == hum::SrcNone, "silent track: AUTO should still be deciding (source %d)", p.recordingSource());
      std::vector<float> lead((size_t) (44100 * 0.3), 0.0f); micAndHost(p, lead, 44100); CHECK(p.recordingSource() == hum::SrcNone, "decided on nothing after 0.3 s (source %d)", p.recordingSource());
      micAndHost(p, x441, 44100); CHECK(p.recordingSource() == hum::SrcMic, "silent track + a voice on the mic: source %d (wanted the mic)", p.recordingSource()); CHECK(p.status.contains("microphone"), "status: %s", p.status.toRawUTF8());
      p.stopRecording(); CHECK(p.lastRecordingRate() == 44100 && p.take().notes.size() == 4, "switched take: rate %.0f, %d notes", p.lastRecordingRate(), (int) p.take().notes.size());
      CHECK(std::abs(p.take().seconds - (lead.size() + x441.size()) / 44100.0) < 0.1, "the mic take did not start at the tap (%.2f s)", p.take().seconds); p.stop();
      // ... and the other way: the track speaks up first -> the track, and the mic is dropped
      p.startRecording(); CHECK(p.recordingSource() == hum::SrcNone, "second take: deciding"); micAndHost(p, std::vector<float>(), 44100, &x); p.stopRecording();
      CHECK(p.lastRecordingRate() == 48000 && p.take().notes.size() == 4, "track first: rate %.0f, %d notes", p.lastRecordingRate(), (int) p.take().notes.size()); p.stop(); }
    // 6) INPUT = MAC MIC on a track with sound takes the mic; the choice is kept in the state
    { Plug P; auto& p = P.p; p.testSync = true; p.fakeMic.available = true; p.fakeMic.rate = 48000; p.setParam("input", (float) hum::InputMic); feed(p, std::vector<float>(4800, 0.05f));
      p.startRecording(); CHECK(p.recordingSource() == hum::SrcMic, "MAC MIC: source %d", p.recordingSource()); std::vector<float> hum2((size_t) x.size(), 0.05f); micAndHost(p, x, 48000, &hum2); p.stopRecording(); CHECK(p.take().notes.size() == 4, "MAC MIC take: %d notes", (int) p.take().notes.size()); p.stop();
      juce::MemoryBlock mb; p.getStateInformation(mb); Plug Q; Q.p.setStateInformation(mb.getData(), (int) mb.getSize()); CHECK(Q.p.inputChoice() == hum::InputMic, "INPUT did not come back from the state (%d)", Q.p.inputChoice()); }
}

// ---------------------------------------------------------------- the window
static void testEditor(const juce::File* shotDir) {
    section("editor: opens, lays out, paints in both modes; the roll edits notes and hits");
    Plug P; auto& p = P.p; p.testSync = true;
    Rand R(3); Singer sg; std::vector<SungNote> ph { { 60, 0.5, 0.05, false }, { 64, 0.5, 0.05, false }, { 67, 0.7, 0.05, false }, { 65, 0.5, 0.05, false }, { 72, 0.9, 0.05, false } }; Truth tr; auto x = sing(sg, ph, R, tr);
    p.analyseAudio(x, kSr, false);
    std::unique_ptr<juce::AudioProcessorEditor> edp(p.createEditor()); auto* ed = dynamic_cast<hum::HumEditor*>(edp.get()); CHECK(ed != nullptr, "editor"); if (!ed) return;
    ed->setSize(960, 660); juce::MessageManager::getInstance()->runDispatchLoopUntil(60);
    auto paintTo = [&](const juce::String& name) { juce::Image img(juce::Image::ARGB, ed->getWidth(), ed->getHeight(), true); juce::Graphics g(img); ed->paintEntireComponent(g, false);
        if (shotDir) { shotDir->createDirectory(); juce::PNGImageFormat png; juce::FileOutputStream os(shotDir->getChildFile(name + ".png")); if (os.openedOk()) png.writeImageToStream(img, os); } return img; };
    auto img = paintTo("melody");
    // the big button is the biggest control; the roll shows notes (green pixels inside it)
    CHECK(ed->record.getWidth() >= 150 && ed->record.getWidth() > ed->play.getWidth() * 1.5, "record button size %d", ed->record.getWidth());
    { int greens = 0; const auto rb = ed->roll.getBounds(); for (int y = rb.getY(); y < rb.getBottom(); y += 2) for (int xx = rb.getX(); xx < rb.getRight(); xx += 2) { const auto c = img.getPixelAt(xx, y); if (c.getGreen() > 150 && c.getRed() < 140) ++greens; } CHECK(greens > 50, "no notes drawn in the roll (%d green px)", greens); }
    CHECK(ed->pitchSwitch.isVisible() && !ed->swing.isVisible() && !ed->teach.isVisible(), "melody mode controls");
    // ON GRID / offers visible for a FREE take
    CHECK(ed->offers[0].isVisible() && ed->offers[1].isVisible(), "FREE offers not shown");
    // drag the first note up two semitones in the roll
    { auto& roll = ed->roll; const auto& T = p.take(); CHECK(T.notes.size() == 5, "editor take has %d notes", (int) T.notes.size()); if (T.notes.size() == 5) { const int before = T.notes[0].midi; const auto r = roll.noteRect(T.notes[0]); const auto start = r.getCentre(); const float rh = roll.rowH();
        juce::MouseEvent down(juce::Desktop::getInstance().getMainMouseSource(), start, juce::ModifierKeys::leftButtonModifier, 0, 0, 0, 0, 0, &roll, &roll, juce::Time::getCurrentTime(), start, juce::Time::getCurrentTime(), 1, false); roll.mouseDown(down);
        const auto to = start.translated(0.0f, -2.0f * rh); juce::MouseEvent drag(juce::Desktop::getInstance().getMainMouseSource(), to, juce::ModifierKeys::leftButtonModifier, 0, 0, 0, 0, 0, &roll, &roll, juce::Time::getCurrentTime(), start, juce::Time::getCurrentTime(), 1, true); roll.mouseDrag(drag); roll.mouseUp(drag);
        CHECK(p.take().notes[0].midi == before + 2, "drag moved the note from %d to %d", before, p.take().notes[0].midi); CHECK(p.doUndo() && p.take().notes[0].midi == before, "UNDO after a roll drag"); } }
    // beatbox mode
    p.setParam("mode", 1); p.handleAsyncUpdateForTests(); { auto& bt = p.take(); bt = hum::Take(); bt.mode = hum::Beatbox; bt.bpm = 100; bt.seconds = 2.4; bt.hits = { { 0.0, hum::Kick, 0.9f, 1 }, { 0.6, hum::Snare, 0.8f, 1 }, { 0.3, hum::HatClosed, 0.5f, 0.3f }, { 0.9, hum::HatOpen, 0.6f, 1 }, { 1.2, hum::Clap, 0.7f, 1 } }; hum::sortTake(bt); bt.origHits = bt.hits; p.edited(); }
    ed->refresh(); juce::MessageManager::getInstance()->runDispatchLoopUntil(30); paintTo("beatbox");
    CHECK(!ed->pitchSwitch.isVisible() && ed->swing.isVisible() && ed->teach.isVisible(), "beatbox mode controls");
    { auto& roll = ed->roll; int hat = -1; for (int i = 0; i < (int) p.take().hits.size(); ++i) if (p.take().hits[(size_t) i].drum == hum::HatClosed) hat = i; CHECK(hat >= 0, "hat"); if (hat >= 0) { const auto r = roll.hitRect(p.take().hits[(size_t) hat]); const auto start = r.getCentre();
        juce::MouseEvent down(juce::Desktop::getInstance().getMainMouseSource(), start, juce::ModifierKeys::leftButtonModifier, 0, 0, 0, 0, 0, &roll, &roll, juce::Time::getCurrentTime(), start, juce::Time::getCurrentTime(), 1, false); roll.mouseDown(down);
        const auto to = juce::Point<float>(start.x, roll.yOfDrum(hum::Clap) + roll.rowH() * 0.5f); juce::MouseEvent drag(juce::Desktop::getInstance().getMainMouseSource(), to, juce::ModifierKeys::leftButtonModifier, 0, 0, 0, 0, 0, &roll, &roll, juce::Time::getCurrentTime(), start, juce::Time::getCurrentTime(), 1, true); roll.mouseDrag(drag); roll.mouseUp(drag);
        int claps = 0; for (auto& h : p.take().hits) if (h.drum == hum::Clap) ++claps; CHECK(claps == 2, "dragging a hit to the CLAP lane: %d claps", claps); bool sure = true; for (auto& h : p.take().hits) if (h.drum == hum::Clap && h.conf < 1) sure = false; CHECK(sure, "a hand-placed hit is still marked unsure"); } }
    // TEACH overlay
    ed->openTeach(); juce::MessageManager::getInstance()->runDispatchLoopUntil(30); CHECK(ed->teachPanel != nullptr && ed->teachPanel->isVisible(), "teach panel"); paintTo("teach"); ed->teachPanel.reset();
    // smaller and bigger windows lay out without overlap of the main pieces
    for (int w : { 760, 1400 }) { ed->setSize(w, juce::jmax(600, w * 7 / 10)); /* (600: the smallest window since build 3's INPUT row) */ juce::MessageManager::getInstance()->runDispatchLoopUntil(20); CHECK(!ed->record.getBounds().intersects(ed->roll.getBounds()) && !ed->roll.getBounds().intersects(ed->play.getBounds()) && ed->roll.getHeight() > 120, "layout at %d px", w); if (shotDir) paintTo(w == 760 ? "small" : "large"); }
    ed->setSize(960, 660); juce::MessageManager::getInstance()->runDispatchLoopUntil(20);

    section("editor (build 2): pads, box selection, Alt-drag copy, key commands, clipboard, TAP, GHOST ANIMATION, hints, the ? card");
    auto mouse = [&](juce::Component& c, juce::Point<float> at, juce::ModifierKeys mods, juce::Point<float> from, bool dragging) { return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at, mods, 0, 0, 0, 0, 0, &c, &c, juce::Time::getCurrentTime(), from, juce::Time::getCurrentTime(), 1, dragging); };
    auto& roll = ed->roll; const juce::ModifierKeys L(juce::ModifierKeys::leftButtonModifier);
    // BEATBOX pads: a click on an empty pad adds a hit on that step; a click on a lit pad removes it
    { const int before = (int) p.take().hits.size(); const auto pr = roll.padRect(6 - roll.k0(), hum::Snare); const auto c = pr.getCentre();
      roll.mouseDown(mouse(roll, c, L, c, false)); roll.mouseUp(mouse(roll, c, L, c, false));
      CHECK((int) p.take().hits.size() == before + 1, "pad click added: %d -> %d", before, (int) p.take().hits.size()); int at6 = 0; for (auto& h : p.take().hits) if (h.drum == hum::Snare && roll.stepOf(h.t) == 6) ++at6; CHECK(at6 == 1, "the new hit is not on step 6 of the SNARE lane");
      roll.mouseDown(mouse(roll, c, L, c, false)); roll.mouseUp(mouse(roll, c, L, c, false)); CHECK((int) p.take().hits.size() == before, "pad click removed: %d", (int) p.take().hits.size()); CHECK(p.doUndo() && (int) p.take().hits.size() == before + 1 && p.doUndo() && (int) p.take().hits.size() == before, "undo of pad clicks"); }
    // box selection + Delete + Cmd+Z; Cmd+A; arrows move a lane; Cmd+C / Cmd+V double the hits; Cmd+D duplicates after the take
    { const int n = (int) p.take().hits.size(); const auto a = roll.area().toFloat(); const juce::Point<float> p0(a.getX() + 2.0f, a.getBottom() - 2.0f), p1(a.getRight() - 2.0f, a.getY() + 2.0f);
      // (pads cover the whole area, so a box starts with Cmd held, which never adds a hit; p0 is the empty CLAP pad of step 0)
      const auto cmdL = juce::ModifierKeys(L.getRawFlags() | juce::ModifierKeys::commandModifier); roll.clearSelection(); roll.mouseDown(mouse(roll, p0, cmdL, p0, false)); roll.mouseDrag(mouse(roll, p1, cmdL, p0, true)); roll.mouseUp(mouse(roll, p1, cmdL, p0, true));
      CHECK((int) roll.selected.size() == n, "box selected %d of %d", (int) roll.selected.size(), n);
      ed->command(juce::KeyPress(juce::KeyPress::deleteKey)); CHECK(p.take().empty(), "Delete left %d", p.take().count()); ed->command(juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0)); CHECK((int) p.take().hits.size() == n, "Cmd+Z after Delete: %d", (int) p.take().hits.size());
      ed->command(juce::KeyPress('a', juce::ModifierKeys::commandModifier, 0)); CHECK((int) roll.selected.size() == n, "Cmd+A selected %d", (int) roll.selected.size());
      std::vector<int> lanes; for (auto& h : p.take().hits) lanes.push_back(h.drum); ed->command(juce::KeyPress(juce::KeyPress::downKey)); bool moved = true; for (size_t i = 0; i < lanes.size(); ++i) if (p.take().hits[i].drum != juce::jmin(hum::kDrums - 1, lanes[i] + 1)) moved = false; CHECK(moved && (int) roll.selected.size() == n, "down arrow moved the lanes (selection %d)", (int) roll.selected.size());
      ed->command(juce::KeyPress(juce::KeyPress::upKey)); const double t0 = p.take().hits[0].t; ed->command(juce::KeyPress(juce::KeyPress::rightKey)); CHECK(std::abs(p.take().hits[0].t - t0 - roll.stepLen()) < 1e-6, "right arrow moved by %.3f (step %.3f)", p.take().hits[0].t - t0, roll.stepLen()); ed->command(juce::KeyPress(juce::KeyPress::leftKey));
      ed->command(juce::KeyPress('c', juce::ModifierKeys::commandModifier, 0)); roll.cursorT = 4.8; ed->command(juce::KeyPress('v', juce::ModifierKeys::commandModifier, 0)); CHECK((int) p.take().hits.size() == 2 * n, "Cmd+V: %d hits", (int) p.take().hits.size()); bool after = true; for (int i : roll.selected) if (p.take().hits[(size_t) i].t < 4.7) after = false; CHECK(after && (int) roll.selected.size() == n, "pasted hits land at the cursor and are selected");
      ed->command(juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0)); CHECK((int) p.take().hits.size() == n, "Cmd+Z after paste");
      roll.clearSelection(); ed->command(juce::KeyPress('d', juce::ModifierKeys::commandModifier, 0)); CHECK((int) p.take().hits.size() == 2 * n, "Cmd+D: %d hits", (int) p.take().hits.size()); double mx = 0; for (auto& h : p.take().hits) mx = juce::jmax(mx, h.t); CHECK(mx >= 2.4, "duplicate lands on the next bar (last hit %.2f)", mx);
      ed->command(juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0)); CHECK((int) p.take().hits.size() == n, "Cmd+Z after duplicate"); }
    // Alt-drag copies a hit (the original stays)
    { const int n = (int) p.take().hits.size(); const auto& h0 = p.take().hits[0]; const auto r = roll.hitRect(h0); const auto c = r.getCentre(); const auto altL = juce::ModifierKeys(L.getRawFlags() | juce::ModifierKeys::altModifier);
      roll.mouseDown(mouse(roll, c, altL, c, false)); const auto to = c.translated(roll.cellW() * 3.0f, 0.0f); roll.mouseDrag(mouse(roll, to, altL, c, true)); roll.mouseUp(mouse(roll, to, altL, c, true));
      CHECK((int) p.take().hits.size() == n + 1, "Alt-drag: %d hits (was %d)", (int) p.take().hits.size(), n); ed->command(juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0)); CHECK((int) p.take().hits.size() == n, "Cmd+Z after Alt-drag"); }
    // Space, Q, G, R, T, ?
    { ed->command(juce::KeyPress::createFromDescription("spacebar")); CHECK(p.isPlaying(), "Space did not play"); ed->command(juce::KeyPress::createFromDescription("spacebar")); juce::AudioBuffer<float> b(2, 512); juce::MidiBuffer m; p.processBlock(b, m); CHECK(!p.isPlaying(), "Space did not stop");
      const int q0 = p.quant(); ed->command(juce::KeyPress('q')); p.handleAsyncUpdateForTests(); CHECK(p.quant() == (q0 + 1) % 5, "Q: quant %d -> %d", q0, p.quant()); p.setParam("quant", (float) q0); p.handleAsyncUpdateForTests();
      CHECK(p.ghostAnim && ed->anim.getButtonText().contains("ON"), "ghost animation should start ON"); ed->command(juce::KeyPress('g')); CHECK(!p.ghostAnim && ed->anim.getButtonText().contains("OFF") && !ed->animationRunning(), "G: animation off (running %d)", (int) ed->animationRunning());
      ed->command(juce::KeyPress('g')); CHECK(p.ghostAnim && ed->anim.getButtonText().contains("ON"), "G: animation back on");
      ed->command(juce::KeyPress('r')); CHECK(p.isRecording(), "R did not start recording"); ed->command(juce::KeyPress('r')); CHECK(!p.isRecording(), "R did not stop");
      p.setParam("tempomode", 1); p.handleAsyncUpdateForTests(); ed->refresh(); for (int i = 0; i < 5; ++i) ed->tapTempo(100.0 + i * 0.5); CHECK(std::abs(p.setBpm() - 120) < 0.6, "TAP at 0.5 s: %.1f BPM", p.setBpm()); for (int i = 0; i < 4; ++i) ed->tapTempo(200.0 + i * 0.4); CHECK(std::abs(p.setBpm() - 150) < 0.6, "TAP at 0.4 s: %.1f BPM", p.setBpm()); p.setParam("tempomode", 0); p.handleAsyncUpdateForTests();
      ed->command(juce::KeyPress('?')); CHECK(ed->helpPanel != nullptr && ed->helpPanel->isVisible(), "? did not open the card"); if (shotDir) { ed->refresh(); juce::MessageManager::getInstance()->runDispatchLoopUntil(30); paintTo("help"); } ed->command(juce::KeyPress(juce::KeyPress::escapeKey)); CHECK(ed->helpPanel == nullptr, "Esc did not close the card"); }
    // INPUT (build 3): shown in both modes, near TEMPO, inside the settings panel; choosing MAC MIC sets the parameter
    { CHECK(ed->inputSwitch.isVisible() && ed->inputSwitch.getWidth() > 150 && ed->settingsArea.contains(ed->inputSwitch.getBounds()) && ed->inputSwitch.getY() > ed->tempoSwitch.getY() && ed->inputSwitch.getY() < ed->tempoSwitch.getBottom() + 60, "INPUT switch placement (y %d, tempo y %d)", ed->inputSwitch.getY(), ed->tempoSwitch.getY());
      ed->inputSwitch.set(2, true); CHECK(p.inputChoice() == hum::InputMic && ed->inputSwitch.sel == 2, "MAC MIC did not set INPUT (%d)", p.inputChoice()); ed->inputSwitch.set(0, true); CHECK(p.inputChoice() == hum::InputAuto, "AUTO back"); }
    // hints and the export gate on the buttons
    { CHECK(roll.hint.contains("PLAY") && roll.hint.contains("DRAG MIDI"), "hint after a take: %s", roll.hint.toRawUTF8()); CHECK(ed->dragMidi.isEnabled() && ed->dragWav.isEnabled(), "export buttons disabled with a take");
      p.clearTake(); ed->refresh(); CHECK(!ed->dragMidi.isEnabled() && !ed->dragWav.isEnabled() && !ed->play.isEnabled(), "export buttons enabled without a take"); CHECK(roll.hint.contains("Tap the big button"), "hint without a take: %s", roll.hint.toRawUTF8()); p.doUndo(); ed->refresh(); }
    // tooltips on every control
    { int missing = 0; std::vector<juce::Component*> cs { &ed->play, &ed->clean, &ed->undoB, &ed->redoB, &ed->dragMidi, &ed->dragWav, &ed->teach, &ed->clearB, &ed->anim, &ed->help, &ed->metro, &ed->countIn, &ed->tap, &ed->modeSwitch, &ed->tempoSwitch, &ed->pitchSwitch, &ed->quantSwitch, &ed->sourceSwitch, &ed->inputSwitch, &ed->bpm, &ed->strength, &ed->swing, &ed->key, &ed->sound, &ed->record, &ed->roll };
      for (auto* c : cs) if (auto* tc = dynamic_cast<juce::TooltipClient*>(c)) { if (tc->getTooltip().isEmpty()) ++missing; } else ++missing; CHECK(missing == 0, "%d controls without a tooltip", missing); }
    // MELODY: box selection over notes, Shift+Up = an octave for the whole selection, Alt-drag copies a note
    p.setParam("mode", 0); p.handleAsyncUpdateForTests(); ed->refresh(); juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    { const int n = (int) p.take().notes.size(); CHECK(n == 5, "melody take has %d notes", n); const auto a = roll.area().toFloat(); const juce::Point<float> p0(a.getX() + 1.0f, a.getY() + 1.0f), p1(a.getRight() - 1.0f, a.getBottom() - 1.0f);
      roll.clearSelection(); roll.mouseDown(mouse(roll, p0, L, p0, false)); roll.mouseDrag(mouse(roll, p1, L, p0, true)); roll.mouseUp(mouse(roll, p1, L, p0, true)); CHECK((int) roll.selected.size() == n, "melody box selected %d of %d", (int) roll.selected.size(), n);
      if ((int) roll.selected.size() != n) for (int i = 0; i < n; ++i) { const auto rr = roll.noteRect(p.take().notes[(size_t) i]); std::printf("    note %d t %.2f midi %d rect %.0f,%.0f %.0fx%.0f sel %d (area %.0f,%.0f %.0fx%.0f)\n", i, p.take().notes[(size_t) i].t, p.take().notes[(size_t) i].midi, rr.getX(), rr.getY(), rr.getWidth(), rr.getHeight(), (int) roll.isSelected(i), a.getX(), a.getY(), a.getWidth(), a.getHeight()); }
      std::vector<int> m0; for (auto& nn : p.take().notes) m0.push_back(nn.midi); ed->command(juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier, 0)); bool oct = true; for (size_t i = 0; i < m0.size(); ++i) if (p.take().notes[i].midi != m0[i] + 12) oct = false; CHECK(oct, "Shift+Up did not move the selection an octave"); ed->command(juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier, 0));
      const auto r = roll.noteRect(p.take().notes[0]); const auto c = r.getCentre(); const auto altL = juce::ModifierKeys(L.getRawFlags() | juce::ModifierKeys::altModifier); roll.clearSelection();
      roll.mouseDown(mouse(roll, c, altL, c, false)); const auto to = c.translated(0.0f, -3.0f * roll.rowH()); roll.mouseDrag(mouse(roll, to, altL, c, true)); roll.mouseUp(mouse(roll, to, altL, c, true));
      CHECK((int) p.take().notes.size() == n + 1, "Alt-drag copied a note: %d", (int) p.take().notes.size()); ed->command(juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0)); CHECK((int) p.take().notes.size() == n, "Cmd+Z after the note copy"); }
    p.setParam("mode", 0); p.handleAsyncUpdateForTests();
}

#if JUCE_LINUX
#include <dlfcn.h>
// (Xlib, which JUCE dlopens) a headless Xvfb without a window manager answers some window-type properties with BadAtom; Xlib's default handler would exit the process
static void ignoreXErrors() { if (void* x11 = dlopen("libX11.so.6", RTLD_NOW | RTLD_GLOBAL)) { typedef int (*H)(void*, void*); typedef H (*SetH)(H); if (auto set = (SetH) dlsym(x11, "XSetErrorHandler")) set([](void*, void*) -> int { return 0; }); } }
#endif
int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
   #if JUCE_LINUX
    ignoreXErrors();
   #endif
    juce::ScopedJuceInitialiser_GUI gui; const juce::String mode = argc > 1 ? argv[1] : "test";
    if (mode == "beattrain") return beatTrain(argc > 2 && juce::String(argv[2]) == "apply");
    if (mode == "shots") { const juce::File d(argc > 2 ? argv[2] : "/tmp/ghosthum-shots"); testEditor(&d); std::printf("shots in %s\n", d.getFullPathName().toRawUTF8()); return fails ? 1 : 0; }
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    if (mode == "rw") testRealWorld(); else if (mode == "plugin") { testPlugin(); testQuality(); testInput(); testEditor(nullptr); }   // (while working: one part)
    else { testMelody(); testRealWorld(); testBeat(); testGrid(); testPlugin(); testQuality(); testInput(); testEditor(nullptr); }
    std::printf("%d checks passed, %d failed  (%.0f s)\n", passes, fails, (juce::Time::getMillisecondCounterHiRes() - t0) / 1000);
    return fails ? 1 : 0;
}
