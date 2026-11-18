// GHOST VOICE A/B proof: our current DSP engines (CLASSIC = engine 0, NATURAL = engine 1) rendered on the SAME layout as the
// neural lab benchmark, so the two can be compared clip for clip:
//   the source performance (a third singer) is re-sung as voice A, as voice B, and as blends at 25 / 50 / 75 % B.
// Our engine is "your take + up to two identities": the take's own weight is set to 0, identity B = voice A, identity C = voice B.
//   GhostVoiceABC OUTDIR SOURCE.wav --a A1.wav [A2.wav ...] --b B1.wav [B2.wav ...]
// Writes OUTDIR/{classic,natural}_{anchor_a,anchor_b,fusion_025,fusion_050,fusion_075}.wav, 00_source.wav and timings.json.
// Several takes of one voice are joined (0.3 s of silence between) before the fingerprint is taken.
#include "VoiceFusion.h"
#include "VoiceFusionHost.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>

using namespace voice;
static AudioRef refOf(const std::vector<float>& x, int fs) { AudioRef r; r.ch.push_back(x.data()); r.n = (int) x.size(); r.fs = fs; return r; }
static bool load(const juce::File& f, std::vector<float>& x, int& fs, int wantFs) {
    juce::String err; if (!fusion::FusionHost::decodeFile(f, x, fs, err)) { std::cerr << err << std::endl; return false; }
    const int wrate = wantFs > 0 ? wantFs : voice::workingRate(fs); if (wrate != fs) { x = voice::resample(x, fs, wrate); fs = wrate; }
    float pk = 0; for (float v : x) pk = std::max(pk, std::abs(v)); if (pk > 1e-6f) for (auto& v : x) v *= 0.7f / pk;   // same input level for every file (as GhostVoiceStages)
    return true;
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 7) { std::cerr << "usage: GhostVoiceABC OUTDIR SOURCE.wav --a A1.wav [..] --b B1.wav [..]" << std::endl; return 2; }
    const juce::File dir(argv[1]); dir.createDirectory();
    std::vector<juce::String> as, bs; int mode = 0;
    for (int i = 3; i < argc; ++i) { const juce::String s(argv[i]); if (s == "--a") mode = 1; else if (s == "--b") mode = 2; else if (mode == 1) as.push_back(s); else if (mode == 2) bs.push_back(s); }
    if (as.empty() || bs.empty()) { std::cerr << "need --a and --b files" << std::endl; return 2; }
    std::vector<float> xs; int fs = 0;
    if (!load(juce::File(argv[2]), xs, fs, 0)) return 1;
    auto joined = [&](const std::vector<juce::String>& files, std::vector<float>& out) {
        for (auto& f : files) { std::vector<float> x; int r = 0; if (!load(juce::File(f), x, r, fs)) return false;
            if (!out.empty()) out.insert(out.end(), (size_t) (0.3 * fs), 0.0f); out.insert(out.end(), x.begin(), x.end()); }
        return true; };
    std::vector<float> xa, xb; if (!joined(as, xa) || !joined(bs, xb)) return 1;
    std::cout << "fs " << fs << "  source " << xs.size() / (double) fs << " s  A " << xa.size() / (double) fs << " s  B " << xb.size() / (double) fs << " s" << std::endl;
    AnalysisOptions o; Analysis as_, aa, ab;
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    analyse(xs.data(), (int) xs.size(), fs, o, as_);
    const auto tSrc = juce::Time::getMillisecondCounterHiRes() - t0;
    analyse(xa.data(), (int) xa.size(), fs, o, aa); analyse(xb.data(), (int) xb.size(), fs, o, ab);
    const auto idA = fusion::extractIdentity(aa), idB = fusion::extractIdentity(ab);
    const auto tId = juce::Time::getMillisecondCounterHiRes() - t0 - tSrc;
    std::cout << "  source analysis " << (int) tSrc << " ms, fingerprints " << (int) tId << " ms (A " << idA.frames.size() << " vowels, B " << idB.frames.size() << ")" << std::endl;
    { Rendered r; r.L = xs; r.fs = fs; fusion::FusionHost::writeWav(dir.getChildFile("00_source.wav"), r); }
    juce::DynamicObject::Ptr timings = new juce::DynamicObject();
    timings->setProperty("source_seconds", xs.size() / (double) fs);
    timings->setProperty("source_analysis_ms", tSrc); timings->setProperty("fingerprints_ms", tId);
    struct Job { const char* id; float wA, wB; };
    const Job jobs[] = { { "anchor_a", 1, 0 }, { "anchor_b", 0, 1 }, { "fusion_025", 0.75f, 0.25f }, { "fusion_050", 0.5f, 0.5f }, { "fusion_075", 0.25f, 0.75f } };
    for (int engine : { 0, 1 }) {
        const juce::String en = engine == 0 ? "classic" : "natural";
        for (auto& j : jobs) {
            fusion::Params p; p.engine = engine; p.wB = j.wA; p.wC = j.wB;   // the take's own weight = 1 - wB - wC = 0
            const auto s0 = juce::Time::getMillisecondCounterHiRes(); Rendered y;
            if (!fusion::render(as_, refOf(xs, fs), &idA, &idB, p, y)) { std::cerr << "render failed " << j.id << std::endl; return 1; }
            const double ms = juce::Time::getMillisecondCounterHiRes() - s0;
            const juce::String name = en + "_" + j.id + ".wav";
            fusion::FusionHost::writeWav(dir.getChildFile(name), y);
            timings->setProperty(en + "_" + j.id + "_ms", ms);
            std::cout << "  " << name << "  " << (int) ms << " ms" << std::endl;
        }
    }
    dir.getChildFile("timings.json").replaceWithText(juce::JSON::toString(juce::var(timings.get())));
    std::cout << "done" << std::endl; return 0;
}
