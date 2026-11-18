// GHOST VOICE stage renders: the engine taken apart stage by stage, so each stage can be measured on its own
// (tools/ghostvoice_measure.py reads the folder). No judgement here, only renders.
//   GhostVoiceStages DIR A.wav B.wav [engine]     engine: classic (default) | clear (= NATURAL) | both
// Writes (fs = the working rate of A):
//   00_A.wav                     A as analysed (mono, working rate)
//   01_A_world.wav               A through WORLD analysis + synthesis only (the VOICE page path, nothing changed): the vocoder floor
//   02_A_fusion_0.wav            A through the fusion engine at 0 % (grid, LPC formants, level window: the fusion pipeline's own cost)
//   03_B.wav, 04_B_world.wav     the same for B (so B's vocoder floor is known)
//   10_blend_25.wav .. 12_blend_75.wav, 13_blend_100.wav     the blends (B %), default settings
//   20_blend_50_nosmooth.wav     50 % with the identity-view smoothing bypassed (via a 1-frame identity: n/a) -> left out
//   21_blend_50_noconsonants.wav 50 % with KEEP MY CONSONANTS off
//   22_blend_50_notransients.wav 50 % with the attack splice off
//   23_blend_50_nonorm.wav       50 % without the peak normalise (gain as rendered)
//   30_ident_B_frames.txt        the identity: frames, formants, weights (what the fingerprint holds)
// With engine "clear" / "both" the same numbers are written with a c_ prefix for the new engine.
#include "VoiceFusion.h"
#include "VoiceFusionHost.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>

using namespace voice;
static void wr(const juce::File& dir, const juce::String& name, const Rendered& r) { const auto f = dir.getChildFile(name); fusion::FusionHost::writeWav(f, r); std::cout << "  wrote " << f.getFileName() << std::endl; }
static void wr(const juce::File& dir, const juce::String& name, const std::vector<float>& x, int fs) { Rendered r; r.L = x; r.fs = fs; wr(dir, name, r); }
static AudioRef refOf(const std::vector<float>& x, int fs) { AudioRef r; r.ch.push_back(x.data()); r.n = (int) x.size(); r.fs = fs; return r; }
static bool load(const juce::File& f, std::vector<float>& x, int& fs) {
    juce::String err; if (!fusion::FusionHost::decodeFile(f, x, fs, err)) { std::cerr << err << std::endl; return false; }
    const int wrate = voice::workingRate(fs); if (wrate != fs) { x = voice::resample(x, fs, wrate); fs = wrate; }
    float pk = 0; for (float v : x) pk = std::max(pk, std::abs(v)); if (pk > 1e-6f) for (auto& v : x) v *= 0.7f / pk;   // the same input level for every file
    return true;
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 4) { std::cerr << "usage: GhostVoiceStages DIR A.wav B.wav [classic|clear|both]" << std::endl; return 2; }
    const juce::File dir(argv[1]); dir.createDirectory(); const juce::String eng = argc > 4 ? argv[4] : "classic";
    std::vector<float> xa, xb; int fsA = 0, fsB = 0;
    if (!load(juce::File(argv[2]), xa, fsA) || !load(juce::File(argv[3]), xb, fsB)) return 1;
    if (fsB != fsA) { xb = voice::resample(xb, fsB, fsA); fsB = fsA; }
    const int fs = fsA; std::cout << "fs " << fs << "  A " << xa.size() / (double) fs << " s  B " << xb.size() / (double) fs << " s" << std::endl;
    Analysis aa, ab; AnalysisOptions o; const auto tA0 = juce::Time::getMillisecondCounterHiRes();
    analyse(xa.data(), (int) xa.size(), fs, o, aa); analyse(xb.data(), (int) xb.size(), fs, o, ab);
    std::cout << "  analysis " << (juce::Time::getMillisecondCounterHiRes() - tA0) << " ms" << std::endl;
    wr(dir, "00_A.wav", xa, fs); wr(dir, "03_B.wav", xb, fs);
    { RenderParams p; Rendered y; voice::render(aa, refOf(xa, fs), nullptr, p, y); wr(dir, "01_A_world.wav", y); }
    { RenderParams p; Rendered y; voice::render(ab, refOf(xb, fs), nullptr, p, y); wr(dir, "04_B_world.wav", y); }
    const auto idB = fusion::extractIdentity(ab);
    { juce::String s; s << "frames " << idB.frames.size() << "  medianF0 " << idB.medianF0 << "  tilt " << idB.tilt << "  breath " << idB.breath << "  scale " << idB.formantScale << "\n";
      for (auto& fr : idB.frames) { s << "  w " << juce::String(fr.weight, 3) << " f0 " << (int) fr.f0 << " F"; for (auto& f : fr.F) s << " " << (int) f.hz << "/" << (int) f.bwHz; s << "  ap"; for (float a : fr.ap) s << " " << juce::String(a, 2); s << "\n"; }
      dir.getChildFile("30_ident_B_frames.txt").replaceWithText(s);
      juce::String csv; for (int i = 0; i < fusion::kGrid; ++i) csv << (i ? "," : "") << juce::String(fusion::gridHz(i), 1); csv << "\n";
      for (auto& fr : idB.frames) { for (int i = 0; i < fusion::kGrid; ++i) csv << (i ? "," : "") << juce::String(fr.logEnv[(size_t) i] * 4.342944819, 2); csv << "\n"; }
      dir.getChildFile("31_ident_B_env_db.csv").replaceWithText(csv); }
    auto run = [&](const juce::String& name, fusion::Params p) {
        const auto t0 = juce::Time::getMillisecondCounterHiRes(); Rendered y; fusion::render(aa, refOf(xa, fs), &idB, nullptr, p, y);
        std::cout << "  " << name << "  " << (int) (juce::Time::getMillisecondCounterHiRes() - t0) << " ms"; wr(dir, name, y); };
    if (eng == "classic" || eng == "both") {
        { fusion::Params p; p.wB = 0; run("02_A_fusion_0.wav", p); }
        for (int w : { 25, 50, 75, 100 }) { fusion::Params p; p.wB = w / 100.0f; run("1" + juce::String(w == 25 ? 0 : w == 50 ? 1 : w == 75 ? 2 : 3) + "_blend_" + juce::String(w) + ".wav", p); }
        { fusion::Params p; p.wB = 0.5f; p.keepConsonants = false; run("21_blend_50_noconsonants.wav", p); }
        { fusion::Params p; p.wB = 0.5f; p.transients = false; run("22_blend_50_notransients.wav", p); }
        { fusion::Params p; p.wB = 0.5f; p.normalise = false; run("23_blend_50_nonorm.wav", p); }
        { fusion::Params p; p.wB = 0.5f; p.octave = -1; run("24_blend_50_octdown.wav", p); }
        { fusion::Params p; p.wB = 0.5f; p.semis = 4; run("25_blend_50_up4.wav", p); }
        { fusion::Params p; p.wB = 0.5f; p.voices = 3; p.octDown = 0.4f; run("26_blend_50_stack3_oct.wav", p); }
        { fusion::Params p; p.wB = 0.5f; p.breath = 0.5f; p.grit = 0.4f; run("27_blend_50_breath_grit.wav", p); }
    }
    if (eng == "clear" || eng == "both") {
        auto runc = [&](const juce::String& name, fusion::Params p) { p.engine = 1; run(name, p); };
        { fusion::Params p; p.wB = 0; runc("c02_A_fusion_0.wav", p); }
        for (int w : { 25, 50, 75, 100 }) { fusion::Params p; p.wB = w / 100.0f; runc("c1" + juce::String(w == 25 ? 0 : w == 50 ? 1 : w == 75 ? 2 : 3) + "_blend_" + juce::String(w) + ".wav", p); }
        { fusion::Params p; p.wB = 0.5f; p.keepConsonants = false; runc("c21_blend_50_noconsonants.wav", p); }
        { fusion::Params p; p.wB = 0.5f; p.octave = -1; runc("c24_blend_50_octdown.wav", p); }
        { fusion::Params p; p.wB = 0.5f; p.semis = 4; runc("c25_blend_50_up4.wav", p); }
        { fusion::Params p; p.wB = 0.5f; p.voices = 3; p.octDown = 0.4f; runc("c26_blend_50_stack3_oct.wav", p); }
        { fusion::Params p; p.wB = 0.5f; p.breath = 0.5f; p.grit = 0.4f; runc("c27_blend_50_breath_grit.wav", p); }
    }
    std::cout << "done" << std::endl; return 0;
}
