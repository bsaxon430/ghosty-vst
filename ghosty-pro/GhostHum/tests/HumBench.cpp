// Ghost Hum benchmark runner: one wav in, what the engines heard out (JSON), so the Python scorer (tests/bench/score.py) can put the
// engines next to the reference transcribers (Basic Pitch, CREPE, pYIN) on real recordings with ground truth.
//   HumBench melody <in.wav> <out.json> [engine]    notes exactly as heard (PITCH = ORIGINAL: midi + cents) and the f0 frames
//   HumBench beat   <in.wav> <out.json> [engine] [net.bin]   hits (t, drum, vel, conf); net.bin: other numbers for the new ears (train_beat.py's
//                                                   held-out folds, so every AVP participant is scored by a net that never heard them)
// engine: "old" (the build 3 engines) or "new" (build 4); default new.
#include "../HumProcessor.h"
#include "HumVoices.h"
#include "../HumFiles.h"
#include <cstdio>

static std::vector<float> readWav(const juce::File& f, double& sr) {
    juce::AudioFormatManager fm; fm.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f));
    if (!r) { std::fprintf(stderr, "cannot read %s\n", f.getFullPathName().toRawUTF8()); return {}; }
    sr = r->sampleRate; juce::AudioBuffer<float> b((int) r->numChannels, (int) r->lengthInSamples); r->read(&b, 0, (int) r->lengthInSamples, 0, true, true);
    std::vector<float> x((size_t) b.getNumSamples(), 0.0f); for (int c = 0; c < b.getNumChannels(); ++c) for (int i = 0; i < b.getNumSamples(); ++i) x[(size_t) i] += b.getSample(c, i) / (float) b.getNumChannels();
    return x;
}

// the first detected onset more than 2 frames after f (-1 = none): what the patch must not look past
static int nextOnset(const std::vector<int>& on, int f) { for (int g : on) if (g > f + 2) return g; return -1; }
// AVP labels: "time,class,..." lines (some run together); returns (time, drum) with kd sd hhc hho -> 0 1 2 3
static std::vector<std::pair<double, int>> readLabels(const juce::File& f) {
    std::vector<std::pair<double, int>> out; auto s = f.loadFileAsString(); int i = 0;
    while (i < s.length()) { int j = i; while (j < s.length() && (juce::CharacterFunctions::isDigit(s[j]) || s[j] == '.' || s[j] == 'e' || s[j] == '-')) ++j;
        if (j > i && j < s.length() && s[j] == ',') { const double t = s.substring(i, j).getDoubleValue(); int k = j + 1; while (k < s.length() && s[k] == ' ') ++k; const auto rest = s.substring(k, k + 3);
            int d = rest.startsWith("hhc") ? 2 : rest.startsWith("hho") ? 3 : rest.startsWith("kd") ? 0 : rest.startsWith("sd") ? 1 : -1; if (d >= 0) out.push_back({ t, d }); i = k + 2; } else i = j > i ? j : i + 1; }
    std::sort(out.begin(), out.end()); return out;
}
// training rows for the beatbox net: every labelled hit's patch at the detected onset nearest it (<= 50 ms, AVP's labels drift by tens of ms; else at the label), jittered
// copies a frame early / late, and every detected onset > 60 ms from any label as NOT A DRUM. Rows of kIn + 1 floats (the label last),
// appended to out.bin. Prints the onset numbers of this file: labels, found, extra.
static int beatFeatures(const std::vector<float>& x, double sr, const juce::File& labels, const juce::File& outBin, float margin) {
    using namespace hum::beat2; const auto S = spectrogram(x, sr); const auto on = onsets(S, margin); const auto ref = readLabels(labels);
    std::vector<float> rows; std::vector<bool> used(on.size(), false); int found = 0; auto push = [&](int f, int label) { auto p = patch(S, f, nextOnset(on, f)); rows.insert(rows.end(), p.begin(), p.end()); rows.push_back((float) label); };
    for (auto& r : ref) { const int fr = (int) std::lround(r.first / S.hop); int best = -1; double bd = 1e9; for (size_t j = 0; j < on.size(); ++j) if (!used[j]) { const double d = std::abs(on[j] - fr) * S.hop; if (d < bd) { bd = d; best = (int) j; } }
        const int at = best >= 0 && bd <= 0.05 ? on[(size_t) best] : fr; if (best >= 0 && bd <= 0.05) { used[(size_t) best] = true; ++found; }
        push(at, r.second); push(at - 1, r.second); push(at + 1, r.second); }
    int extra = 0; for (size_t j = 0; j < on.size(); ++j) { if (used[j]) continue; ++extra; double near = 1e9; for (auto& r : ref) near = juce::jmin(near, std::abs(on[j] * S.hop - r.first)); if (near > 0.06) push(on[j], kJunk); }
    outBin.deleteFile(); juce::FileOutputStream os(outBin); if (!os.openedOk()) return 1;   // (a FileOutputStream appends) os.write(rows.data(), rows.size() * sizeof(float)); os.flush();
    std::printf("%d %d %d\n", (int) ref.size(), found, extra); return 0;
}

// the same rows from our own synthesised beatboxers (tests/HumVoices.h): `count` random mouths, half through a rough phone / laptop mic with
// breaths, grunts, smacks, clicks and pops between the hits (those onsets are NOT A DRUM). The claps come only from here (AVP has none).
static int synthFeatures(int count, int seed, const juce::File& outBin) {
    using namespace hum::beat2; using namespace humtest; std::vector<float> rows; int hits = 0, junk = 0;
    for (int b = 0; b < count; ++b) { Rand R((uint32_t) (seed + b)); Beatboxer B = Beatboxer::random(R); const double bpm = R.u(75, 150); auto truth = randomPattern(R, bpm, 2); auto x = beatbox(B, truth, R);
        if (R.p(0.5)) { Mic m; if (R.p(0.4)) m.clipGain = R.u(1.3, 4); if (R.p(0.3)) m.lowCut = R.u(80, 200); if (R.p(0.4)) m.reverb = R.u(0.1, 0.4); if (R.p(0.3)) m.humDb = R.u(-40, -24); if (R.p(0.3)) m.fanDb = R.u(-40, -26);
            if (R.p(0.5)) m.clicks = R.i(2, 12); if (R.p(0.4)) m.smacks = R.i(1, 5); if (R.p(0.4)) m.pops = R.i(1, 5); if (R.p(0.3)) m.levelDb = R.u(-40, -10); if (R.p(0.6)) addMouthNoise(x, truth, R, R.u(-26, -10), R.u(-22, -8), R.p(0.7)); applyMic(x, m, R); }
        // (a leaked metronome click, in a third of the takes: its onsets that are not a hit are NOT A DRUM too)
        if (R.p(0.33)) addClickLeak(x, bpm, truth.front().t, R.u(-24, -6), R.u(0.005, 0.06), R);
        const auto S = spectrogram(x, kSr); const auto on = onsets(S); std::vector<bool> used(on.size(), false);
        auto push = [&](int f, int label) { auto p = patch(S, f, nextOnset(on, f)); rows.insert(rows.end(), p.begin(), p.end()); rows.push_back((float) label); };
        for (auto& h : truth) { const int fr = (int) std::lround(h.t / S.hop); int best = -1; double bd = 1e9; for (size_t j = 0; j < on.size(); ++j) if (!used[j]) { const double d = std::abs(on[j] - fr) * S.hop; if (d < bd) { bd = d; best = (int) j; } }
            if (best < 0 || bd > 0.03) continue; used[(size_t) best] = true; push(on[(size_t) best], h.drum); ++hits; }
        for (size_t j = 0; j < on.size(); ++j) { if (used[j]) continue; double near = 1e9; for (auto& h : truth) near = juce::jmin(near, std::abs(on[j] * S.hop - h.t)); if (near > 0.06) { push(on[j], kJunk); ++junk; } } }
    outBin.deleteFile(); juce::FileOutputStream os(outBin); if (!os.openedOk()) return 1;   // (a FileOutputStream appends) os.write(rows.data(), rows.size() * sizeof(float)); os.flush();
    std::printf("%d mouths: %d hits, %d not a drum\n", count, hits, junk); return 0;
}

// synthetic ground truth (tests/HumVoices.h): `count` random singers / beatboxers, half through a rough mic, both engines, signed timing.
// Prints one line per engine: notes / hits, right %, extra %, signed median onset and offset error (ms), fragments per note.
static void tune(hum::melody::Options& o) {   // (tuning sweeps: HUM_RISE, HUM_GLUE, HUM_GAP, HUM_SMOOTH)
    if (auto* v = std::getenv("HUM_RISE")) o.riseDb = (float) atof(v); if (auto* v = std::getenv("HUM_GLUE")) o.glueCents = (float) atof(v);
    if (auto* v = std::getenv("HUM_GAP")) o.gapCut = atoi(v); if (auto* v = std::getenv("HUM_SMOOTH")) o.smooth = (float) atof(v); if (auto* v = std::getenv("HUM_BLIP")) o.blipFrames = atoi(v); }
static double median(std::vector<double> v) { if (v.empty()) return 0; std::nth_element(v.begin(), v.begin() + (long) v.size() / 2, v.end()); return v[v.size() / 2]; }
static int synthBench(const juce::String& what, int count, int seed) {
    using namespace humtest;
    for (int eng = 0; eng < 2; ++eng) {
        int truthN = 0, right = 0, matched = 0, extra = 0, frag = 0; std::vector<double> on, off, absOn; double ms = 0, sec = 0;
        for (int s = 0; s < count; ++s) { Rand R((uint32_t) (seed + s));
            if (what == "synthmel") { Singer sg = Singer::random(R); const double bpm = R.u(70, 150); auto ph = randomPhrase(R, 8, bpm); Truth tr; auto x = sing(sg, ph, R, tr);
                if (s % 2) { Mic m; m.lowCut = R.p(0.5) ? R.u(80, 180) : 0; m.reverb = R.p(0.5) ? R.u(0.1, 0.35) : 0; m.clicks = R.i(0, 6); m.humDb = R.p(0.3) ? -32 : -200; applyMic(x, m, R); }
                hum::melody::Options o; o.pitch = hum::PitchOriginal; o.engine = eng; tune(o); const auto t0 = juce::Time::getMillisecondCounterHiRes(); auto T = hum::melody::transcribe(x, kSr, o); ms += juce::Time::getMillisecondCounterHiRes() - t0; sec += x.size() / kSr;
                std::vector<bool> used(T.notes.size(), false); truthN += (int) tr.on.size();
                for (size_t i = 0; i < tr.on.size(); ++i) { int best = -1; double bd = 1e9; for (size_t j = 0; j < T.notes.size(); ++j) if (!used[j]) { const double d = std::abs(T.notes[j].t - tr.on[i]); if (d < bd) { bd = d; best = (int) j; } }
                    if (best >= 0 && bd <= 0.08) { used[(size_t) best] = true; ++matched; if (T.notes[(size_t) best].midi == tr.midi[i]) ++right; on.push_back(1000 * (T.notes[(size_t) best].t - tr.on[i])); absOn.push_back(std::abs(on.back())); off.push_back(1000 * (T.notes[(size_t) best].t + T.notes[(size_t) best].len - tr.off[i])); } }
                for (size_t j = 0; j < T.notes.size(); ++j) if (!used[j]) { ++extra; for (size_t i = 0; i < tr.on.size(); ++i) if (T.notes[j].t > tr.on[i] + 0.05 && T.notes[j].t < tr.off[i] - 0.03) { ++frag; break; } } }
            else { Beatboxer B = Beatboxer::random(R); const double bpm = R.u(75, 150); auto hits = randomPattern(R, bpm, 2); auto x = beatbox(B, hits, R);
                if (s % 2) { Mic m; m.lowCut = R.p(0.5) ? R.u(80, 180) : 0; m.reverb = R.p(0.5) ? R.u(0.1, 0.35) : 0; m.clicks = R.i(0, 6); m.clipGain = R.p(0.3) ? 2 : 1; addMouthNoise(x, hits, R); applyMic(x, m, R); }
                hum::beat::Options o; o.engine = eng; const auto t0 = juce::Time::getMillisecondCounterHiRes(); auto T = hum::beat::transcribe(x, kSr, o); ms += juce::Time::getMillisecondCounterHiRes() - t0; sec += x.size() / kSr;
                std::vector<bool> used(T.hits.size(), false); truthN += (int) hits.size();
                for (auto& h : hits) { int best = -1; double bd = 1e9; for (size_t j = 0; j < T.hits.size(); ++j) if (!used[j]) { const double d = std::abs(T.hits[j].t - h.t); if (d < bd) { bd = d; best = (int) j; } }
                    if (best >= 0 && bd <= 0.03) { used[(size_t) best] = true; ++matched; if (T.hits[(size_t) best].drum == h.drum) ++right; on.push_back(1000 * (T.hits[(size_t) best].t - h.t)); absOn.push_back(std::abs(on.back())); } }
                for (bool u : used) if (!u) ++extra; } }
        std::printf("%s %-4s truth %d matched %.1f%% right %.1f%% (of truth) extra %.1f%% frag/note %.3f onset signed %.1f ms |%.1f| offset signed %.1f ms  %.1f ms/s\n", what.toRawUTF8(), eng ? "new" : "old", truthN, 100.0 * matched / truthN, 100.0 * right / truthN, 100.0 * extra / truthN, (double) frag / truthN, median(on), median(absOn), median(off), ms / juce::jmax(1e-9, sec));
    }
    return 0;
}

static bool loadNet(const juce::File& f, hum::beat2::Net& N) {
    using namespace hum::beat2; juce::MemoryBlock mb; if (!f.loadFileAsData(mb)) return false; const size_t n = mb.getSize() / sizeof(float); const float* p = (const float*) mb.getData();
    const size_t need = (size_t) kH1 * kIn + kH1 + (size_t) kH2 * kH1 + kH2 + (size_t) kOut * kH2 + kOut; if (n != need) { std::fprintf(stderr, "net %s: %d floats, wanted %d\n", f.getFullPathName().toRawUTF8(), (int) n, (int) need); return false; }
    auto take = [&](std::vector<float>& v, size_t k) { v.assign(p, p + k); p += k; };
    take(N.W1, (size_t) kH1 * kIn); take(N.b1, kH1); take(N.W2, (size_t) kH2 * kH1); take(N.b2, kH2); take(N.W3, (size_t) kOut * kH2); take(N.b3, kOut); return N.ready();
}

int main(int argc, char** argv) {
    if (auto* v = std::getenv("HUM_LAG")) hum::beat::kOnsetLag2 = atof(v); if (auto* v = std::getenv("HUM_REL")) hum::beat::kRel2 = (float) atof(v); if (auto* v = std::getenv("HUM_MARGIN")) hum::beat::kMargin2 = (float) atof(v); if (auto* v = std::getenv("HUM_TNET")) hum::beat::kTeachNet = (float) atof(v); if (auto* v = std::getenv("HUM_TSIM")) hum::beat::kTeachSim = (float) atof(v); if (auto* v = std::getenv("HUM_TOLD")) hum::beat::kTeachOld = (float) atof(v);
    if (argc < 4) { std::printf("HumBench melody|beat in.wav out.json [old|new]\n         HumBench beatfeat in.wav labels.csv out.bin [margin]\n         HumBench synthfeat count seed out.bin\n"); return 2; }
    juce::ScopedJuceInitialiser_GUI gui; const juce::String what = argv[1]; const juce::File in { juce::String(argv[2]) }, out { juce::String(argv[3]) }; const juce::String engine = argc > 4 ? argv[4] : "new";
    if (what == "render" || what == "rendercsv") {
        // HumBench render <in.wav> <outprefix> melody|beat old|new [original|nearest]   -> outprefix.mid + outprefix.wav (the take on the built-in sounds, ORIGINAL timing)
        // HumBench rendercsv <notes.csv> <outprefix> <seconds>                          -> the same from a ground-truth note list (onset, Hz, duration)
        hum::Take T; const juce::String pre = argv[3];
        if (what == "render") { if (argc < 6) return 2; double sr = 0; auto x = readWav(in, sr); if (x.empty()) return 1; const bool mel = juce::String(argv[4]) == "melody"; const bool old = juce::String(argv[5]) == "old";
            if (mel) { hum::melody::Options o; o.engine = old ? hum::melody::EngineOld : hum::melody::EngineNew; o.pitch = argc > 6 && juce::String(argv[6]) == "original" ? hum::PitchOriginal : hum::PitchNearest; T = hum::melody::transcribe(x, sr, o); }
            else { hum::beat::Options o; o.engine = old ? hum::beat::EngineOld : hum::beat::EngineNew; T = hum::beat::transcribe(x, sr, o); } T.seconds = x.size() / sr; }
        else { T.mode = hum::Melody; T.seconds = atof(argv[4]); for (auto& l : juce::StringArray::fromLines(in.loadFileAsString())) { auto q = juce::StringArray::fromTokens(l, ",", ""); if (q.size() < 3) continue;
                hum::Note n; n.t = q[0].getDoubleValue(); const double hz = q[1].getDoubleValue(); n.len = q[2].getDoubleValue(); const double m = 69 + 12 * std::log2(hz / 440.0); n.midi = (int) std::lround(m); n.vel = 0.8f; T.notes.push_back(n); } T.origNotes = T.notes; }
        T.bpm = 120; T.origin = 0; hum::Grid g; const bool ok = hum::files::writeMidi(juce::File(pre + ".mid"), T, g, true, "Ghost Hum bench") && hum::files::writeWav(juce::File(pre + ".wav"), T, g, true, T.mode == hum::Melody ? hum::sound::SoftKeys : 0);
        std::printf("%d %s\n", T.count(), T.mode == hum::Melody ? "notes" : "hits"); return ok ? 0 : 1; }
    if (what == "beatteach") {   // HumBench beatteach <improv.wav> <out.json> <kick.wav> <snare.wav> <hhc.wav> <hho.wav> [net.bin] [engine]: TEACH on real takes
        // (AVP: each participant also recorded every sound alone; the 3 clearest of each become the lesson, as the plug-in's TEACH does, then the
        // improvisation is heard with it)
        if (argc < 8) return 2; hum::beat::Calibration cal; const juce::String eng = argc > 9 ? argv[9] : "new";
        hum::beat::Options o; hum::beat2::Net N; if (argc > 8 && juce::String(argv[8]) != "-") { if (!loadNet(juce::File { juce::String(argv[8]) }, N)) return 1; o.net = &N; }
        for (int d = 0; d < 4; ++d) { double s2 = 0; auto y = readWav(juce::File { juce::String(argv[4 + d]) }, s2); if (y.empty()) return 1; for (auto& e : hum::beat::lessons(y, s2, d, nullptr, 3, o.net)) cal.ex.push_back(e); }
        double sr = 0; auto x = readWav(in, sr); if (x.empty()) return 1; o.cal = &cal; o.engine = eng == "old" ? hum::beat::EngineOld : hum::beat::EngineNew;
        if (eng == "old") for (auto& e : cal.ex) e.emb.clear();
        const auto t0 = juce::Time::getMillisecondCounterHiRes(); auto T = hum::beat::transcribe(x, sr, o); juce::String j = "{\n\"seconds\": "; j << x.size() / sr << ", \"ms\": " << (juce::Time::getMillisecondCounterHiRes() - t0) << ",\n\"hits\": [";
        for (size_t i = 0; i < T.hits.size(); ++i) { auto& h = T.hits[i]; j << (i ? ",\n" : "\n") << "[" << juce::String(h.t, 4) << ", " << h.drum << ", " << juce::String(h.vel, 3) << ", " << juce::String(h.conf, 3) << "]"; }
        j << "]\n}\n"; out.replaceWithText(j); return 0; }
    if (what == "synthmel" || what == "synthbeat") return synthBench(what, atoi(argv[2]), atoi(argv[3]));
    if (what == "synthfeat") return synthFeatures(atoi(argv[2]), atoi(argv[3]), juce::File { juce::String(argv[4]) });
    double sr = 0; auto x = readWav(in, sr); if (x.empty()) return 1;
    if (what == "beatfeat") return beatFeatures(x, sr, out, juce::File { juce::String(argv[4]) }, argc > 5 ? (float) atof(argv[5]) : 4.0f);
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    juce::String j = "{\n";
    if (what == "melody") {
        hum::melody::Options o; o.pitch = hum::PitchOriginal; o.engine = engine == "old" ? hum::melody::EngineOld : hum::melody::EngineNew; hum::melody::Analysis A; tune(o);
        auto T = hum::melody::transcribe(x, sr, o, &A);
        j << "\"seconds\": " << x.size() / sr << ", \"ms\": " << (juce::Time::getMillisecondCounterHiRes() - t0) << ",\n\"notes\": [";
        for (size_t i = 0; i < T.origNotes.size(); ++i) { auto& n = T.origNotes[i]; j << (i ? ",\n" : "\n") << "[" << juce::String(n.t, 4) << ", " << juce::String(n.len, 4) << ", " << n.midi << ", " << juce::String(n.cents, 1) << ", " << juce::String(n.vel, 3) << "]"; }
        j << "],\n\"frames\": [";
        for (size_t i = 0; i < A.frames.size(); ++i) { auto& f = A.frames[i];   // (build 4: the final pitch line, A.track, when the engine gives one)
            const bool tr = A.track.size() == A.frames.size(); const float hz = tr ? A.track[i] : (f.voiced ? f.f0 : 0.0f);
            j << (i ? ",\n" : "\n") << "[" << juce::String(f.t, 4) << ", " << juce::String(hz, 2) << ", " << juce::String(tr && hz > 0 ? hz : f.f0, 2) << ", " << (hz > 0 ? 1 : f.weak ? 2 : 0) << "]"; }
        j << "],\n\"key\": \"" << hum::keyText(T) << "\"\n";
    } else {
        hum::beat::Options o; o.engine = engine == "old" ? hum::beat::EngineOld : hum::beat::EngineNew; std::vector<hum::beat::Event> ev; hum::beat2::Net N;
        if (argc > 5 && loadNet(juce::File { juce::String(argv[5]) }, N)) o.net = &N; else if (argc > 5) return 1;
        if (engine == "new1") o.overlaps = false;
        auto T = hum::beat::transcribe(x, sr, o, &ev);
        j << "\"seconds\": " << x.size() / sr << ", \"ms\": " << (juce::Time::getMillisecondCounterHiRes() - t0) << ",\n\"hits\": [";
        for (size_t i = 0; i < T.hits.size(); ++i) { auto& h = T.hits[i]; j << (i ? ",\n" : "\n") << "[" << juce::String(h.t, 4) << ", " << h.drum << ", " << juce::String(h.vel, 3) << ", " << juce::String(h.conf, 3) << "]"; }
        j << "],\n\"events\": [";   // every onset heard, dropped ones included (drum -1), with the class scores
        for (size_t i = 0; i < ev.size(); ++i) { auto& e = ev[i]; j << (i ? ",\n" : "\n") << "[" << juce::String(e.t, 4) << ", " << e.drum << ", " << juce::String(e.junk, 3); for (int d = 0; d < hum::kDrums; ++d) j << ", " << juce::String(e.score[(size_t) d], 3); j << "]"; }
        j << "]\n";
    }
    j << "}\n"; out.replaceWithText(j); return 0;
}
