/*
 * SEANCE (GhostBlock) - writes sounds (.sxsnp) from recipes in this file.
 *   seance-sounds factory <dir>              the sounds that need nothing but the plugin (go into resources/factory_patches)
 *   seance-sounds vital <Vital folder> <dir> the sounds built on the owner's own Vital factory wavetables: run on his
 *                                            Mac, written to his own sounds folder, never into the repository
 * Every sound is played through the engine before it is written: finite, not silent, peak trimmed to about 0.6,
 * silent after release. Nobody has listened to them: they are starting points made by reasoning about the engines.
 */
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include "synth/synth.h"
#include "synth/matrix_index.h"
#include "ghostblock/seance/steps.h"
#include "ghostblock/seance/step-builtins.h"

using namespace baconpaul::six_sines;
using SN = Patch::SourceNode;

struct Maker
{
    std::unique_ptr<Synth> s;
    Patch &P;
    std::vector<int> heardOps;
    Maker() : s(std::make_unique<Synth>(false)), P(s->patchMain)
    {
        s->setSampleRate(48000);
        P.resetToInit();
        for (auto &m : P.mixerNodes) { m.active = 0; m.level = 0; }
        for (auto &m : P.matrixNodes) { m.active = 0; m.level = 0; }
        for (auto &m : P.selfNodes) { m.active = 0; m.fbLevel = 0; }
        for (auto &m : P.sourceNodes) m.active = 0;
    }
    static void env(Patch::DAHDSRMixin &e, float a, float d, float sus, float r)
    {
        e.envPower = 1; e.delay = 0; e.hold = 0; e.attack = a; e.decay = d; e.sustain = sus; e.release = r;
    }
    // seat numbers are 1 - 12, as on the table
    Maker &spirit(int seat, float ratio, int species = 0)
    {
        auto &n = P.sourceNodes[seat - 1];
        n.active = 1; n.ratio = std::log2(ratio); n.species = (float)species;
        return *this;
    }
    Maker &heard(int seat, float level, float a, float d, float sus, float r, float pan = 0.f)
    {
        auto &m = P.mixerNodes[seat - 1];
        m.active = 1; m.level = level; m.pan = pan;
        env(m, a, d, sus, r);
        heardOps.push_back(seat - 1);
        return *this;
    }
    Maker &link(int from, int to, float depth, float a = 0.f, float d = 0.f, float sus = 1.f, float r = 0.5f)
    {
        auto &m = P.matrixNodes[MatrixIndex::positionForSourceTarget(from - 1, to - 1)];
        m.active = 1; m.level = depth;
        env(m, a, d, sus, r);
        return *this;
    }
    Maker &feedback(int seat, float amt)
    {
        P.selfNodes[seat - 1].active = 1; P.selfNodes[seat - 1].fbLevel = amt;
        return *this;
    }
    Maker &throat(int seat, const char *group, const char *name, float cutoff, float reso, float keytrack = 1.f)
    {
        const auto &cat = ghostblock::seance::throatCatalog();
        for (int k = 1; k < (int)cat.size(); ++k)
            if (cat[k].group.find(group) != std::string::npos && cat[k].name.find(name) != std::string::npos)
            {
                auto &t = P.throatNodes[seat - 1];
                t.kind = (float)k; t.cutoff = cutoff; t.resonance = reso; t.keytrack = keytrack;
                return *this;
            }
        std::printf("   (no filter matching %s / %s)\n", group, name);
        return *this;
    }
    // a shade: table 0-7 of the built-in ones, frame 0-1, morph type 0-9 + amount, warp type 0-6 + amount, unison 1-16
    Maker &shade(int seat, float ratio, int table, float frame, int morph = 0, float morphAmt = 0.5f, int warp = 0, float warpAmt = 0.5f, int unison = 1)
    {
        spirit(seat, ratio, SN::shadeSpecies);
        auto &n = P.sourceNodes[seat - 1];
        const float v[7] = {(float)table / 7.f, frame, (float)morph / 9.f, morphAmt, (float)warp / 6.f, warpAmt, (float)(unison - 1) / 15.f};
        for (int k = 0; k < 7; ++k) n.beastP[k] = v[k];
        n.beastSet = 1;
        return *this;
    }
    // MOVE. rate: LFO speed as a power of two in Hz (0 = 1 Hz, -2 = 0.25 Hz, 2 = 4 Hz); cutoff sweeps in semitones;
    // target 0 FRAME / 1 MORPH / 2 WARP on a shade, depths -1 .. 1; the envelope is the seat's PITCH envelope
    Maker &lfoMove(int seat, float rate, float toCutoff, int target = 0, float toBody = 0.f)
    {
        auto &t = P.throatNodes[seat - 1];
        P.sourceNodes[seat - 1].lfoRate = rate;
        t.lfoToCutoff = toCutoff; t.bodyTarget = (float)target; t.lfoToBody = toBody;
        return *this;
    }
    Maker &envMove(int seat, float a, float d, float sus, float r, float toCutoff, int target = 0, float toBody = 0.f)
    {
        auto &t = P.throatNodes[seat - 1];
        env(P.sourceNodes[seat - 1], a, d, sus, r);
        t.envToCutoff = toCutoff; t.bodyTarget = (float)target; t.envToBody = toBody;
        return *this;
    }
    // FOLLOWS (the engine's modulation slots; slot 0 or 1). what: VELOCITY 5000, MOD WHEEL 201, AFTERTOUCH 100, RANDOM 5201, MACRO n 399 + n
    template <typename N> static void fol(N &n, int slot, int what, int target, float amount)
    {
        n.modsource[slot] = (float)what; n.modtarget[slot] = (float)target; n.moddepth[slot] = amount;
    }
    Maker &volumeFollows(int seat, int what, float amount, int slot = 0) { fol(P.mixerNodes[seat - 1], slot, what, 10, amount); return *this; }
    Maker &feedbackFollows(int seat, int what, float amount, int slot = 0) { fol(P.selfNodes[seat - 1], slot, what, 10, amount); return *this; }
    Maker &pitchFollows(int seat, int what, float amount, int slot = 0) { fol(P.sourceNodes[seat - 1], slot, what, 11, amount); return *this; }
    Maker &depthFollows(int from, int to, int what, float amount, int slot = 0)
    {
        fol(P.matrixNodes[MatrixIndex::positionForSourceTarget(from - 1, to - 1)], slot, what, 10, amount);
        return *this;
    }
    Maker &macroName(int n, const char *name)
    {
        std::snprintf(P.macroNames[n - 1].data(), P.macroNames[n - 1].size(), "%s", name);
        return *this;
    }
    // STEPS switched on with one of Seance's own patterns (index into the lines, or into the held-chord arps)
    Maker &steps(bool heldArp, int index, float swing = 0.f, float length = 1.f, int octave = 0)
    {
        using namespace ghostblock::seance;
        auto pat = std::make_unique<StepPattern>();
        const auto &list = heldArp ? steppat::heldArps() : steppat::lines();
        if (index < 0 || index >= (int)list.size() || !steppat::fromBuiltin(*pat, list[(size_t)index], heldArp, index)) return *this;
        auto q = std::make_unique<StepSeq>();
        q->setPattern(*pat);
        q->on = true; q->swing = swing; q->gate = length; q->octave = octave;
        P.rackSteps = q->toState();
        return *this;
    }
    Maker &tableFile(int seat, const std::string &name, const std::string &json)
    {
        P.setShadeTable((size_t)(seat - 1), name, json);
        return *this;
    }
    // play a chord through the engine; returns peak, sets tail (level 1.5 s after release)
    float play(float &tail, bool &finite)
    {
        s->patch.copyValuesFrom(P);
        s->reconcileWavetables();
        for (int k : {48, 55, 60}) s->voiceManager->processNoteOnEvent(0, 0, k, -1, 0.85f, 0.f);
        float pk = 0; finite = true; size_t bp = 0;
        for (int i = 0; i < 48000 * 2; ++i)
        {
            if (bp == 0) s->process(nullptr);
            for (int c = 0; c < 2; ++c) { const float y = s->output[c][bp]; finite &= std::isfinite(y); pk = std::max(pk, std::fabs(y)); }
            if (++bp == s->blockSize) bp = 0;
        }
        for (int k : {48, 55, 60}) s->voiceManager->processNoteOffEvent(0, 0, k, -1, 0.5f);
        tail = 0;
        for (int i = 0; i < 48000 * 6; ++i)
        {
            if (bp == 0) s->process(nullptr);
            if (i > 48000 * 5) tail = std::max(tail, std::fabs(s->output[0][bp]));
            if (++bp == s->blockSize) bp = 0;
        }
        s->voiceManager->allSoundsOff();
        for (int i = 0; i < 4800; ++i) { if (bp == 0) s->process(nullptr); if (++bp == s->blockSize) bp = 0; }
        return pk;
    }
    bool write(const fs::path &dir, const std::string &name)
    {
        float tail = 0; bool fin = true;
        float pk = play(tail, fin);
        for (int pass = 0; pass < 3 && fin && pk > 1e-4f && (pk > 0.7f || pk < 0.45f); ++pass)
        { // trim what is heard so three notes peak near 0.6
            const float g = 0.6f / pk;
            for (int o : heardOps) P.mixerNodes[o].level = std::min(1.f, P.mixerNodes[o].level.value * g);
            pk = play(tail, fin);
        }
        for (int pass = 0; pass < 6 && fin && pk > 1e-4f && pk < 0.4f; ++pass)
        { // still quiet with every heard spirit at full (a closed filter, a soft table): raise the main volume
            auto &ol = P.output.level;
            if (ol.value >= ol.meta.maxVal - 1e-4f) break;
            ol = std::min(ol.meta.maxVal, ol.value + 0.12f * (ol.meta.maxVal - ol.meta.minVal));
            pk = play(tail, fin);
        }
        const bool ok = fin && pk > 0.1f && pk < 0.98f && tail < 0.02f;
        std::printf("%s %-26s peak %.2f  tail %.4f\n", ok ? "ok:  " : "FAIL:", name.c_str(), pk, tail);
        if (!ok) return false;
        strncpy(P.name, name.c_str(), sizeof(P.name) - 1);
        P.setAuthor("GhostBlock");
        P.dirty = false;
        std::error_code ec;
        fs::create_directories(dir, ec);
        std::ofstream f(dir / (name + ".sxsnp"));
        f << P.toState();
        return f.good();
    }
};

using Recipe = std::function<void(Maker &)>;
struct Sound { const char *folder, *name; Recipe make; const char *vitalTable; /* "Basic/Basic Shapes" or null */ };

static std::vector<Sound> sounds()
{
    // species: 1 Classic 2 Modern 3 String 4 Twist 5 Alias 6 Noise 7 FM2 8 FM3 (Surge), SN::shadeSpecies = wavetable
    // envelopes: attack, decay, sustain, release - times 0 (instant) .. 1 (very long)
    std::vector<Sound> v;
    // ------------------------------------------------------------------ the plugin alone
    v.push_back({"Seance Bass", "Possession Bass", [](Maker &m) {
        m.spirit(1, 1).heard(1, 0.8f, 0.02f, 0.45f, 0.6f, 0.25f).spirit(2, 1).spirit(3, 2);
        m.link(2, 1, 0.55f, 0.f, 0.4f, 0.35f, 0.3f).link(3, 2, 0.3f, 0.f, 0.3f, 0.2f, 0.3f).feedback(2, 0.15f);
        m.throat(1, "Vember Classic", "Lowpass 24 dB Standard", 4, 0.3f).envMove(1, 0.f, 0.45f, 0.15f, 0.3f, 34); }, nullptr});
    v.push_back({"Seance Bass", "Beast Reese", [](Maker &m) {
        m.spirit(1, 1, 1).heard(1, 0.6f, 0.03f, 0.5f, 0.9f, 0.3f, -0.25f).spirit(2, 1.007f, 1).heard(2, 0.6f, 0.03f, 0.5f, 0.9f, 0.3f, 0.25f);
        m.spirit(3, 0.5f).heard(3, 0.5f, 0.02f, 0.5f, 1.f, 0.3f);
        m.throat(1, "OB-Xd", "24", 8, 0.35f).throat(2, "OB-Xd", "24", 10, 0.35f); }, nullptr});
    v.push_back({"Seance Bass", "Ladder Growl", [](Maker &m) {
        m.shade(1, 1, 7, 0.35f, 0, 0.5f, 1, 0.45f).heard(1, 0.8f, 0.02f, 0.5f, 0.8f, 0.25f).spirit(2, 0.5f).link(2, 1, 0.35f);
        m.spirit(3, 0.5f).heard(3, 0.5f, 0.02f, 0.5f, 1.f, 0.25f);
        m.throat(1, "Diode Ladder", "24", 8, 0.6f).lfoMove(1, 1.f, 16, 2, 0.2f); }, nullptr});
    v.push_back({"Seance Bells", "Twelve Bells", [](Maker &m) {
        const float r[6] = {1.f, 2.76f, 5.4f, 8.93f, 13.34f, 18.64f};
        for (int i = 0; i < 6; ++i) m.spirit(i + 1, r[i]).heard(i + 1, 0.7f / (1.f + 0.6f * (float)i), 0.f, 0.62f - 0.05f * (float)i, 0.f, 0.6f - 0.05f * (float)i, (i % 2 ? 0.4f : -0.4f));
        m.spirit(7, 3.5f).link(7, 1, 0.3f, 0.f, 0.35f, 0.f, 0.4f).spirit(8, 7.1f).link(8, 2, 0.25f, 0.f, 0.3f, 0.f, 0.4f); }, nullptr});
    v.push_back({"Seance Bells", "Glass Ring", [](Maker &m) {
        m.spirit(1, 1).heard(1, 0.7f, 0.f, 0.68f, 0.f, 0.66f).spirit(2, 3).heard(2, 0.3f, 0.f, 0.6f, 0.f, 0.6f, 0.5f).spirit(3, 4.99f).heard(3, 0.2f, 0.f, 0.55f, 0.f, 0.55f, -0.5f);
        m.link(1, 2, 0.3f).link(2, 3, 0.3f).link(3, 1, 0.25f, 0.f, 0.5f, 0.f, 0.5f); }, nullptr});
    v.push_back({"Seance Bells", "Metal Tongue", [](Maker &m) {
        m.shade(1, 1, 5, 0.3f).heard(1, 0.8f, 0.f, 0.66f, 0.f, 0.62f).spirit(2, 3.5f).link(2, 1, 0.35f, 0.f, 0.4f, 0.f, 0.4f);
        m.shade(3, 2, 5, 0.8f).heard(3, 0.35f, 0.f, 0.58f, 0.f, 0.58f, 0.4f); }, nullptr});
    v.push_back({"Seance Pads", "Shade Choir", [](Maker &m) {
        m.shade(1, 1, 3, 0.2f, 0, 0.5f, 0, 0.5f, 5).heard(1, 0.6f, 0.55f, 0.6f, 0.85f, 0.62f, -0.5f);
        m.shade(2, 1, 3, 0.55f, 0, 0.5f, 0, 0.5f, 5).heard(2, 0.6f, 0.6f, 0.6f, 0.85f, 0.64f, 0.5f);
        m.shade(3, 2, 3, 0.85f, 0, 0.5f, 0, 0.5f, 3).heard(3, 0.3f, 0.65f, 0.6f, 0.8f, 0.66f);
        m.throat(1, "Vember Classic", "Lowpass 24 dB Standard", 28, 0.15f).throat(2, "Vember Classic", "Lowpass 24 dB Standard", 30, 0.15f);
        m.lfoMove(1, -2.f, 6, 0, 0.25f).lfoMove(2, -1.6f, 6, 0, -0.25f).lfoMove(3, -2.3f, 0, 0, 0.12f); }, nullptr});
    v.push_back({"Seance Pads", "String Wraith", [](Maker &m) {
        m.spirit(1, 1, 3).heard(1, 0.7f, 0.5f, 0.6f, 0.9f, 0.6f, -0.3f).spirit(2, 2, 3).heard(2, 0.35f, 0.55f, 0.6f, 0.9f, 0.62f, 0.3f);
        m.shade(3, 1, 6, 0.4f, 5, 0.6f, 0, 0.5f, 3).heard(3, 0.4f, 0.6f, 0.6f, 0.8f, 0.64f);
        m.throat(3, "Vember Classic", "Lowpass 12 dB Standard", 30, 0.1f).lfoMove(3, -1.5f, 8, 1, 0.3f); }, nullptr});
    v.push_back({"Seance Pads", "Iron Lung", [](Maker &m) {
        m.spirit(1, 1, 6).heard(1, 0.8f, 0.6f, 0.6f, 0.9f, 0.62f).throat(1, "Vember Classic", "Bandpass 24 dB Standard", 12, 0.85f);
        m.spirit(2, 2, 6).heard(2, 0.5f, 0.62f, 0.6f, 0.9f, 0.64f, 0.4f).throat(2, "Vember Classic", "Bandpass 24 dB Standard", 12, 0.85f);
        m.spirit(3, 1).heard(3, 0.25f, 0.6f, 0.6f, 0.8f, 0.62f, -0.4f);
        m.lfoMove(1, -2.f, 10).lfoMove(2, -1.4f, -10); }, nullptr});
    v.push_back({"Seance Leads", "Fold Lead", [](Maker &m) {
        m.shade(1, 1, 1, 0.45f, 0, 0.5f, 1, 0.35f).heard(1, 0.8f, 0.05f, 0.5f, 0.85f, 0.3f).spirit(2, 2).link(2, 1, 0.2f, 0.f, 0.45f, 0.3f, 0.3f);
        m.throat(1, "K35", "Lowpass", 34, 0.3f).envMove(1, 0.f, 0.5f, 0.2f, 0.3f, 0, 0, 0.4f); }, nullptr});
    v.push_back({"Seance Leads", "Hollow Pulse", [](Maker &m) {
        m.shade(1, 1, 4, 0.4f, 0, 0.5f, 0, 0.5f, 3).heard(1, 0.8f, 0.04f, 0.5f, 0.85f, 0.3f).shade(2, 2, 4, 0.7f).heard(2, 0.3f, 0.04f, 0.5f, 0.8f, 0.3f, 0.3f);
        m.throat(1, "Vember Classic", "Notch 24 dB Standard", 22, 0.5f).lfoMove(1, -0.5f, 12, 0, 0.3f); }, nullptr});
    v.push_back({"Seance Leads", "Twist Howl", [](Maker &m) {
        m.spirit(1, 1, 4).heard(1, 0.8f, 0.06f, 0.5f, 0.9f, 0.32f).spirit(2, 1.5f).link(2, 1, 0.3f, 0.2f, 0.5f, 0.6f, 0.3f);
        m.throat(1, "Tri-pole", "", 26, 0.55f); }, nullptr});
    v.push_back({"Seance Keys", "Grit Stab", [](Maker &m) {
        m.shade(1, 1, 7, 0.55f).heard(1, 0.8f, 0.f, 0.5f, 0.f, 0.4f).shade(2, 2, 0, 0.2f).heard(2, 0.4f, 0.f, 0.45f, 0.f, 0.4f, 0.3f);
        m.spirit(3, 1).link(3, 1, 0.3f, 0.f, 0.35f, 0.f, 0.3f);
        m.throat(1, "Vember Classic", "Bandpass 12 dB Standard", 10, 0.45f).envMove(1, 0.f, 0.38f, 0.f, 0.3f, 30); }, nullptr});
    v.push_back({"Seance Keys", "Drawbar Ghost", [](Maker &m) {
        m.shade(1, 1, 6, 0.6f).heard(1, 0.8f, 0.02f, 0.5f, 0.95f, 0.25f).spirit(2, 1).link(2, 1, 0.12f).shade(3, 1, 6, 1.f).heard(3, 0.3f, 0.02f, 0.4f, 0.5f, 0.25f, 0.3f); }, nullptr});
    v.push_back({"Seance Keys", "FM Tines", [](Maker &m) {
        m.spirit(1, 1).heard(1, 0.8f, 0.f, 0.62f, 0.15f, 0.5f).spirit(2, 14).link(2, 1, 0.3f, 0.f, 0.4f, 0.f, 0.4f).spirit(3, 1).link(3, 1, 0.25f, 0.f, 0.55f, 0.1f, 0.5f);
        m.spirit(4, 1.003f).heard(4, 0.5f, 0.f, 0.62f, 0.15f, 0.5f, 0.4f).spirit(5, 1).link(5, 4, 0.3f, 0.f, 0.55f, 0.1f, 0.5f); }, nullptr});
    v.push_back({"Seance Effects", "All Twelve At Once", [](Maker &m) {
        const float r[12] = {1, 2, 3, 4, 5, 6, 0.5f, 1.5f, 2.5f, 3.5f, 7, 9};
        for (int i = 0; i < 12; ++i) m.spirit(i + 1, r[i], i % 4 == 3 ? SN::shadeSpecies : 0);
        for (int i = 0; i < 12; ++i) m.link(i + 1, (i + 1) % 12 + 1, 0.25f, 0.5f, 0.6f, 0.6f, 0.6f);
        for (int i = 0; i < 12; i += 3) m.heard(i + 1, 0.5f, 0.5f, 0.6f, 0.8f, 0.62f, i % 2 ? 0.5f : -0.5f); }, nullptr});
    // ------------------------------------------------------------------ all twelve seats, every one connected
    const int SH = SN::shadeSpecies;
    v.push_back({"Seance Twelve", "The Long Chain", [](Maker &m) { // 12 > 11 > ... > 1, only the first is heard
        const float r[12] = {1, 1, 2, 1, 3, 2, 1, 4, 2, 1, 5, 3};
        for (int i = 0; i < 12; ++i) m.spirit(i + 1, r[i]);
        for (int i = 12; i >= 2; --i) m.link(i, i - 1, 0.16f + 0.012f * (float)(12 - i), 0.f, 0.3f + 0.02f * (float)i, 0.25f, 0.3f);
        m.heard(1, 0.8f, 0.02f, 0.5f, 0.7f, 0.28f, -0.2f).heard(7, 0.5f, 0.02f, 0.5f, 0.7f, 0.28f, 0.2f).feedback(12, 0.2f);
        m.throat(1, "Vember Classic", "Lowpass 24 dB Standard", 40, 0.3f).envMove(1, 0.f, 0.45f, 0.2f, 0.3f, 24); }, nullptr});
    v.push_back({"Seance Twelve", "Six Marriages", [](Maker &m) { // six voices, each a singer and its possessor
        const float mr[6] = {1, 14, 2, 7, 3, 1};
        for (int i = 0; i < 6; ++i)
        {
            const int c = 2 * i + 1, q = 2 * i + 2;
            m.spirit(c, i < 3 ? 1.f + 0.002f * (float)i : 2.f - 0.003f * (float)i).spirit(q, mr[i]);
            m.heard(c, i < 3 ? 0.7f : 0.3f, 0.f, 0.6f, 0.12f, 0.5f, -0.6f + 0.24f * (float)i).link(q, c, 0.26f, 0.f, 0.36f + 0.03f * (float)i, 0.f, 0.4f);
        }
        for (int i = 0; i < 6; ++i) m.link(2 * i + 2, (2 * i + 2) % 12 + 1, 0.06f, 0.f, 0.3f, 0.f, 0.3f); }, nullptr}); // and each possessor touches the next singer
    v.push_back({"Seance Twelve", "Coven Choir", [SH](Maker &m) { // four wavetable singers, each with two whisperers
        for (int g = 0; g < 4; ++g)
        {
            const int c = 3 * g + 1;
            m.shade(c, g == 3 ? 2.f : 1.f, 3, 0.2f + 0.2f * (float)g, 0, 0.5f, 0, 0.5f, 3).heard(c, g == 3 ? 0.3f : 0.6f, 0.55f + 0.03f * (float)g, 0.6f, 0.85f, 0.62f, -0.6f + 0.4f * (float)g);
            m.spirit(c + 1, 1).link(c + 1, c, 0.08f, 0.6f, 0.6f, 0.7f, 0.6f).spirit(c + 2, 2).link(c + 2, c + 1, 0.15f, 0.6f, 0.6f, 0.6f, 0.6f);
            m.throat(c, "Vember Classic", "Lowpass 24 dB Standard", 42, 0.15f).lfoMove(c, -2.2f + 0.3f * (float)g, 6, 0, g % 2 ? -0.25f : 0.25f);
        } }, nullptr});
    v.push_back({"Seance Twelve", "Three Towers", [](Maker &m) { // three stacks of four, spread left, centre, right
        const float r[4] = {1, 1, 2, 3};
        for (int t = 0; t < 3; ++t)
        {
            const int b = 4 * t + 1;
            for (int k = 0; k < 4; ++k) m.spirit(b + k, r[k] * (1.f + 0.004f * (float)(t - 1)));
            m.link(b + 1, b, 0.4f, 0.f, 0.5f, 0.45f, 0.3f).link(b + 2, b + 1, 0.3f, 0.f, 0.42f, 0.3f, 0.3f).link(b + 3, b + 2, 0.22f, 0.f, 0.36f, 0.2f, 0.3f).feedback(b + 3, 0.15f);
            m.heard(b, 0.6f, 0.04f, 0.5f, 0.85f, 0.32f, (float)(t - 1) * 0.6f);
        } }, nullptr});
    v.push_back({"Seance Twelve", "Cathedral", [](Maker &m) { // six bell partials, each struck by its own clapper
        const float pr[6] = {1.f, 2.76f, 5.4f, 8.93f, 0.5f, 13.34f}, cr[6] = {3.5f, 7.1f, 11.f, 1.41f, 2.f, 4.2f};
        for (int i = 0; i < 6; ++i)
        {
            const int c = i + 1, q = i + 7;
            m.spirit(c, pr[i]).heard(c, 0.7f / (1.f + 0.5f * (float)i), 0.f, 0.66f - 0.04f * (float)i, 0.f, 0.62f - 0.04f * (float)i, i % 2 ? 0.45f : -0.45f);
            m.spirit(q, cr[i]).link(q, c, 0.28f, 0.f, 0.34f, 0.f, 0.4f);
        } }, nullptr});
    v.push_back({"Seance Twelve", "Ouroboros", [SH](Maker &m) { // a ring: every spirit possesses the next, the last the first
        const float r[12] = {1, 2, 1, 3, 1, 2, 0.5f, 4, 1, 2, 1.5f, 3};
        for (int i = 0; i < 12; ++i)
        {
            if (i % 3 == 0) m.shade(i + 1, r[i], 2 + i / 3, 0.3f + 0.05f * (float)i, 0, 0.5f, 0, 0.5f, 3); else m.spirit(i + 1, r[i]);
            m.link(i + 1, (i + 1) % 12 + 1, 0.2f, 0.55f, 0.6f, 0.7f, 0.6f);
        }
        for (int i = 0; i < 12; i += 3) m.heard(i + 1, 0.5f, 0.55f, 0.6f, 0.85f, 0.64f, i % 2 ? 0.5f : -0.5f).lfoMove(i + 1, -2.5f + 0.25f * (float)i, 0, 0, 0.3f); }, nullptr});
    v.push_back({"Seance Twelve", "Beast Parliament", [](Maker &m) { // four Surge beasts speak, eight spirits drive them
        const int sp[4] = {1, 2, 3, 4};
        for (int g = 0; g < 4; ++g)
        {
            const int c = 3 * g + 1;
            m.spirit(c, g == 3 ? 0.5f : 1.f + 0.005f * (float)(g - 1), sp[g]).heard(c, 0.6f, 0.03f, 0.5f, 0.9f, 0.3f, -0.45f + 0.3f * (float)g);
            m.spirit(c + 1, 1).link(c + 1, c, 0.22f, 0.f, 0.45f, 0.4f, 0.3f).spirit(c + 2, 2).link(c + 2, c + 1, 0.25f, 0.f, 0.4f, 0.3f, 0.3f);
            if (g == 0) m.throat(c, "OB-Xd", "24", 14, 0.3f).envMove(c, 0.f, 0.5f, 0.3f, 0.3f, 24);
        } }, nullptr});
    v.push_back({"Seance Twelve", "Legion", [](Maker &m) { // ten spirits pour into one voice over a clean sub
        const float r[10] = {1, 2, 3, 4, 5, 6, 7, 8, 0.5f, 1.5f};
        m.spirit(1, 0.5f).heard(1, 0.55f, 0.02f, 0.5f, 1.f, 0.25f).spirit(2, 1).heard(2, 0.8f, 0.02f, 0.5f, 0.8f, 0.25f);
        for (int i = 0; i < 10; ++i) m.spirit(i + 3, r[i]).link(i + 3, 2, 0.085f, 0.f, 0.3f + 0.03f * (float)i, 0.15f, 0.3f);
        m.throat(2, "Diode Ladder", "24", 14, 0.4f).envMove(2, 0.f, 0.5f, 0.15f, 0.3f, 26); }, nullptr});
    v.push_back({"Seance Twelve", "Twelve Drawbars", [](Maker &m) { // an organ of twelve pipes; each leans on its neighbour
        const float r[12] = {0.5f, 1, 1.5f, 2, 3, 4, 5, 6, 8, 10, 12, 16};
        for (int i = 0; i < 12; ++i)
        {
            m.spirit(i + 1, r[i]).heard(i + 1, 0.7f / (1.f + 0.35f * (float)i), 0.02f, 0.5f, 0.95f, 0.26f, i % 2 ? 0.25f : -0.25f);
            if (i > 0) m.link(i + 1, i, 0.05f);
        }
        m.link(1, 12, 0.05f).feedback(2, 0.1f); }, nullptr});
    v.push_back({"Seance Twelve", "Wraith Swarm", [SH](Maker &m) { // twelve slightly out of tune, wavetables and sines in turn
        for (int i = 0; i < 12; ++i)
        {
            const float r = (i >= 8 ? 2.f : 1.f) * (1.f + 0.0035f * (float)(i % 8 - 4));
            if (i % 2 == 0) m.shade(i + 1, r, 6, 0.15f + 0.07f * (float)i, 0, 0.5f, 0, 0.5f, 3).lfoMove(i + 1, -2.6f + 0.2f * (float)i, 0, 0, 0.25f); else m.spirit(i + 1, r);
            m.heard(i + 1, i >= 8 ? 0.25f : 0.5f, 0.6f, 0.6f, 0.85f, 0.64f, -0.8f + 0.145f * (float)i);
            m.link(i + 1, (i + 2) % 12 + 1, 0.07f, 0.6f, 0.6f, 0.7f, 0.6f);
        } }, nullptr});
    // ------------------------------------------------------------------ sounds you play: HANDS, MACROS, STEPS
    enum { VEL = 5000, WHEEL = 201, TOUCH = 100, RANDOM = 5201, MACRO1 = 400, MACRO2 = 401 };
    v.push_back({"Seance Played", "Velvet Keys", [](Maker &m) { // soft = round, hard = bright and loud
        m.spirit(1, 1).spirit(2, 1.003f).spirit(3, 1).spirit(4, 14);
        m.heard(1, 0.7f, 0.f, 0.6f, 0.15f, 0.5f, -0.25f).heard(2, 0.55f, 0.f, 0.6f, 0.15f, 0.5f, 0.25f);
        m.link(3, 1, 0.1f, 0.f, 0.42f, 0.f, 0.4f).link(4, 2, 0.06f, 0.f, 0.34f, 0.f, 0.4f);
        m.volumeFollows(1, VEL, 0.5f).volumeFollows(2, VEL, 0.5f).depthFollows(3, 1, VEL, 0.45f).depthFollows(4, 2, VEL, 0.35f); }, nullptr});
    v.push_back({"Seance Played", "Wheel Growl", [](Maker &m) { // push the mod wheel: the bass tears open
        m.spirit(1, 0.5f).heard(1, 0.5f, 0.02f, 0.5f, 1.f, 0.25f).spirit(2, 1).heard(2, 0.8f, 0.02f, 0.5f, 0.9f, 0.25f);
        m.spirit(3, 1).link(3, 2, 0.1f, 0.f, 0.5f, 0.6f, 0.3f).spirit(4, 2).link(4, 3, 0.08f, 0.f, 0.5f, 0.5f, 0.3f).feedback(4, 0.1f);
        m.depthFollows(3, 2, WHEEL, 0.7f).depthFollows(4, 3, WHEEL, 0.6f).feedbackFollows(4, WHEEL, 0.4f).depthFollows(3, 2, VEL, 0.2f, 1);
        m.throat(2, "Diode Ladder", "24", 30, 0.3f); }, nullptr});
    v.push_back({"Seance Played", "Macro Bloom", [SH](Maker &m) { // MACRO 1 opens the pad, MACRO 2 brings in the octave
        m.shade(1, 1, 3, 0.3f, 0, 0.5f, 0, 0.5f, 4).heard(1, 0.7f, 0.56f, 0.6f, 0.9f, 0.62f, -0.4f);
        m.shade(4, 1.004f, 6, 0.5f, 0, 0.5f, 0, 0.5f, 3).heard(4, 0.6f, 0.58f, 0.6f, 0.9f, 0.62f, 0.4f);
        m.shade(7, 2, 2, 0.4f, 0, 0.5f, 0, 0.5f, 3).heard(7, 0.06f, 0.6f, 0.6f, 0.9f, 0.64f);
        m.spirit(2, 2).link(2, 1, 0.02f, 0.5f, 0.6f, 0.8f, 0.6f).spirit(5, 3).link(5, 4, 0.02f, 0.5f, 0.6f, 0.8f, 0.6f).spirit(8, 1).link(8, 7, 0.02f, 0.5f, 0.6f, 0.8f, 0.6f);
        m.depthFollows(2, 1, MACRO1, 0.45f).depthFollows(5, 4, MACRO1, 0.4f).depthFollows(8, 7, MACRO1, 0.4f).volumeFollows(7, MACRO2, 0.8f);
        m.macroName(1, "Bloom").macroName(2, "Octave");
        m.throat(1, "Vember Classic", "Lowpass 24 dB Standard", 34, 0.15f).lfoMove(1, -2.2f, 5, 0, 0.25f).lfoMove(4, -1.9f, 0, 0, -0.25f); }, nullptr});
    v.push_back({"Seance Played", "Glass Touch", [](Maker &m) { // a bell whose clappers follow how hard you hit, and no two notes quite alike
        const float pr[3] = {1.f, 2.76f, 5.4f}, cr[3] = {3.5f, 7.1f, 1.41f};
        for (int i = 0; i < 3; ++i)
        {
            m.spirit(i + 1, pr[i]).heard(i + 1, 0.7f / (1.f + 0.6f * (float)i), 0.f, 0.66f - 0.04f * (float)i, 0.f, 0.6f, i == 1 ? 0.4f : i == 2 ? -0.4f : 0.f);
            m.spirit(i + 4, cr[i]).link(i + 4, i + 1, 0.12f, 0.f, 0.34f, 0.f, 0.4f).depthFollows(i + 4, i + 1, VEL, 0.5f).depthFollows(i + 4, i + 1, RANDOM, 0.12f, 1);
            m.volumeFollows(i + 1, VEL, 0.45f);
        } }, nullptr});
    v.push_back({"Seance Played", "Acid Rite", [](Maker &m) { // STEPS: the acid crawl; hold one key
        m.spirit(1, 1).feedback(1, 0.5f).heard(1, 0.8f, 0.f, 0.45f, 0.6f, 0.2f).spirit(2, 1).link(2, 1, 0.14f, 0.f, 0.4f, 0.2f, 0.3f).depthFollows(2, 1, VEL, 0.5f); // (feedback: a saw-like edge)
        m.throat(1, "Diode Ladder", "24", 10, 0.55f).envMove(1, 0.f, 0.36f, 0.05f, 0.25f, 30).volumeFollows(1, VEL, 0.4f);
        m.steps(false, 5, 0.f, 0.9f); }, nullptr});
    v.push_back({"Seance Played", "Gate Choir", [SH](Maker &m) { // STEPS: the trance gate over a wavetable choir; hold one key (or change SPECIES of sound under it)
        m.shade(1, 1, 3, 0.35f, 0, 0.5f, 0, 0.5f, 4).heard(1, 0.7f, 0.02f, 0.5f, 0.9f, 0.3f, -0.3f).shade(2, 1.5f, 6, 0.5f, 0, 0.5f, 0, 0.5f, 3).heard(2, 0.4f, 0.02f, 0.5f, 0.9f, 0.3f, 0.3f);
        m.shade(3, 2, 2, 0.4f, 0, 0.5f, 0, 0.5f, 3).heard(3, 0.3f, 0.02f, 0.5f, 0.9f, 0.3f);
        m.throat(1, "Vember Classic", "Lowpass 24 dB Standard", 36, 0.2f).lfoMove(1, -2.f, 8, 0, 0.3f).volumeFollows(1, VEL, 0.4f);
        m.steps(false, 4, 0.f, 0.85f); }, nullptr});
    v.push_back({"Seance Played", "Octave Engine", [](Maker &m) { // STEPS: the octave pump; hold one key low down
        m.spirit(1, 0.5f).heard(1, 0.6f, 0.f, 0.45f, 0.5f, 0.2f).spirit(2, 1, 2).heard(2, 0.7f, 0.f, 0.45f, 0.5f, 0.2f);
        m.spirit(3, 2).link(3, 1, 0.2f, 0.f, 0.36f, 0.f, 0.3f).depthFollows(3, 1, VEL, 0.4f);
        m.throat(2, "OB-Xd", "24", 12, 0.3f).envMove(2, 0.f, 0.4f, 0.1f, 0.3f, 22);
        m.steps(false, 0, 0.f, 0.8f, -1); }, nullptr});
    v.push_back({"Seance Played", "Night Arp", [](Maker &m) { // STEPS: up and down over the chord you hold
        m.spirit(1, 1).heard(1, 0.8f, 0.f, 0.5f, 0.f, 0.42f, -0.2f).spirit(2, 2.002f).heard(2, 0.3f, 0.f, 0.46f, 0.f, 0.4f, 0.2f);
        m.spirit(3, 3).link(3, 1, 0.14f, 0.f, 0.36f, 0.f, 0.3f).spirit(4, 1).link(4, 2, 0.1f, 0.f, 0.3f, 0.f, 0.3f);
        m.depthFollows(3, 1, VEL, 0.45f).volumeFollows(1, VEL, 0.35f);
        m.steps(true, 2, 0.f, 1.f); }, nullptr});
    v.push_back({"Seance Played", "Thumb Harp", [](Maker &m) { // STEPS: thumb pedal over the chord you hold, with a little swing
        m.spirit(1, 1).heard(1, 0.8f, 0.f, 0.52f, 0.f, 0.44f).spirit(2, 4).link(2, 1, 0.1f, 0.f, 0.3f, 0.f, 0.3f).spirit(3, 1.41f).link(3, 1, 0.04f, 0.f, 0.24f, 0.f, 0.3f);
        m.depthFollows(2, 1, VEL, 0.5f).depthFollows(3, 1, RANDOM, 0.1f).volumeFollows(1, VEL, 0.4f);
        m.steps(true, 4, 0.25f, 1.1f); }, nullptr});
    v.push_back({"Seance Played", "Converging", [SH](Maker &m) { // STEPS: converge over the chord you hold; MACRO 1 brightens it
        m.shade(1, 1, 1, 0.2f, 0, 0.5f, 0, 0.5f, 1).heard(1, 0.8f, 0.f, 0.5f, 0.1f, 0.42f).spirit(2, 2).link(2, 1, 0.04f, 0.f, 0.36f, 0.f, 0.3f);
        m.throat(1, "K35", "Lowpass", 20, 0.3f).envMove(1, 0.f, 0.4f, 0.1f, 0.3f, 20).depthFollows(2, 1, MACRO1, 0.5f).macroName(1, "Bite").volumeFollows(1, VEL, 0.35f);
        m.steps(true, 6, 0.f, 0.9f); }, nullptr});
    // ------------------------------------------------------------------ on the owner's Vital factory wavetables
    auto vt = [&](const char *folder, const char *name, const char *table, Recipe r) { v.push_back({folder, name, r, table}); };
    vt("Vital Tables", "Shapes Pad", "Basic/Basic Shapes", [](Maker &m) {
        m.shade(1, 1, 0, 0.35f, 0, 0.5f, 0, 0.5f, 5).heard(1, 0.7f, 0.55f, 0.6f, 0.85f, 0.62f, -0.4f);
        m.shade(2, 1, 0, 0.7f, 0, 0.5f, 0, 0.5f, 5).heard(2, 0.6f, 0.6f, 0.6f, 0.85f, 0.64f, 0.4f);
        m.throat(1, "Vember Classic", "Lowpass 24 dB Standard", 30, 0.15f).lfoMove(1, -2.f, 6, 0, 0.3f).lfoMove(2, -1.7f, 0, 0, -0.3f); });
    vt("Vital Tables", "Classic Blend Bass", "Basic/Classic Blend", [](Maker &m) {
        m.shade(1, 1, 0, 0.5f).heard(1, 0.8f, 0.02f, 0.5f, 0.75f, 0.25f).spirit(2, 0.5f).heard(2, 0.5f, 0.02f, 0.5f, 1.f, 0.25f);
        m.throat(1, "Vintage Ladder", "", 2, 0.3f).envMove(1, 0.f, 0.45f, 0.2f, 0.3f, 30, 0, 0.3f); });
    vt("Vital Tables", "Harmonic Keys", "Basic/Harmonic Series", [](Maker &m) {
        m.shade(1, 1, 0, 0.12f).heard(1, 0.8f, 0.f, 0.6f, 0.2f, 0.5f).shade(2, 1, 0, 0.3f).heard(2, 0.4f, 0.f, 0.5f, 0.f, 0.45f, 0.35f);
        m.spirit(3, 1).link(3, 1, 0.15f, 0.f, 0.45f, 0.f, 0.4f); });
    vt("Vital Tables", "Three Graces Pluck", "Basic/Three Graces", [](Maker &m) {
        m.shade(1, 1, 0, 0.6f).heard(1, 0.8f, 0.f, 0.5f, 0.f, 0.45f).shade(2, 2, 0, 0.9f).heard(2, 0.35f, 0.f, 0.42f, 0.f, 0.4f, 0.3f);
        m.throat(1, "K35", "Lowpass", 30, 0.35f); });
    vt("Vital Tables", "Quad Saw Stack", "Basic/Quad Saw", [](Maker &m) {
        m.shade(1, 1, 0, 0.5f, 0, 0.5f, 0, 0.5f, 7).heard(1, 0.7f, 0.05f, 0.5f, 0.9f, 0.4f, -0.3f).shade(2, 2, 0, 0.8f, 0, 0.5f, 0, 0.5f, 5).heard(2, 0.35f, 0.05f, 0.5f, 0.9f, 0.4f, 0.3f);
        m.throat(1, "OB-Xd", "24", 14, 0.2f).envMove(1, 0.f, 0.55f, 0.5f, 0.4f, 24).lfoMove(2, -1.f, 0, 0, 0.2f); });
    vt("Vital Tables", "Juice Lead", "Complex/Drink the Juice", [](Maker &m) {
        m.shade(1, 1, 0, 0.4f).heard(1, 0.8f, 0.05f, 0.5f, 0.85f, 0.3f).spirit(2, 1).link(2, 1, 0.15f, 0.3f, 0.5f, 0.6f, 0.3f);
        m.throat(1, "Diode Ladder", "24", 46, 0.25f).lfoMove(1, 0.f, 0, 0, 0.25f); });
    vt("Vital Tables", "Low High Growl", "Complex/Low High Fold", [](Maker &m) {
        m.shade(1, 1, 0, 0.55f, 0, 0.5f, 1, 0.3f).heard(1, 0.8f, 0.02f, 0.5f, 0.85f, 0.25f).spirit(2, 0.5f).link(2, 1, 0.35f).spirit(3, 0.5f).heard(3, 0.45f, 0.02f, 0.5f, 1.f, 0.25f);
        m.throat(1, "Vintage Ladder", "", 16, 0.45f).envMove(1, 0.f, 0.5f, 0.1f, 0.3f, 12, 0, 0.5f); });
    vt("Vital Tables", "Post Modern Bass", "Complex/Post Modern Pulse", [](Maker &m) {
        m.shade(1, 1, 0, 0.3f).heard(1, 0.75f, 0.02f, 0.5f, 0.8f, 0.25f).shade(2, 1.005f, 0, 0.6f).heard(2, 0.5f, 0.02f, 0.5f, 0.8f, 0.25f, 0.3f);
        m.throat(1, "OB-Xd", "24", 12, 0.3f).throat(2, "OB-Xd", "24", 14, 0.3f).lfoMove(1, 1.f, 8, 0, 0.3f).lfoMove(2, 0.6f, 8, 0, -0.3f); });
    vt("Vital Tables", "Saws For Days", "Complex/Saws for Days", [](Maker &m) {
        m.shade(1, 1, 0, 0.5f, 0, 0.5f, 0, 0.5f, 7).heard(1, 0.7f, 0.04f, 0.5f, 0.9f, 0.4f).shade(2, 0.5f, 0, 0.2f).heard(2, 0.4f, 0.04f, 0.5f, 0.9f, 0.4f).lfoMove(1, -1.5f, 0, 0, 0.2f); });
    vt("Vital Tables", "Sine Two Bell", "Complex/Vital Sine 2", [](Maker &m) {
        m.shade(1, 1, 0, 0.5f).heard(1, 0.8f, 0.f, 0.66f, 0.f, 0.62f).spirit(2, 3.5f).link(2, 1, 0.25f, 0.f, 0.45f, 0.f, 0.4f).shade(3, 2, 0, 0.9f).heard(3, 0.3f, 0.f, 0.58f, 0.f, 0.58f, 0.4f); });
    vt("Vital Tables", "Jaw Harp Twang", "Physical/Jaw Harp", [](Maker &m) {
        m.shade(1, 1, 0, 0.2f).heard(1, 0.8f, 0.f, 0.55f, 0.1f, 0.4f).shade(2, 1, 0, 0.7f).heard(2, 0.4f, 0.f, 0.5f, 0.f, 0.4f, 0.3f);
        m.throat(1, "Vember Classic", "Bandpass 12 dB Standard", 20, 0.4f); });
    vt("Vital Tables", "Didg Drone", "Physical/Didg", [](Maker &m) {
        m.shade(1, 0.5f, 0, 0.3f, 0, 0.5f, 0, 0.5f, 3).heard(1, 1.f, 0.3f, 0.6f, 1.f, 0.6f).shade(2, 1, 0, 0.75f).heard(2, 0.8f, 0.35f, 0.6f, 1.f, 0.6f, 0.4f).spirit(3, 0.25f).link(3, 1, 0.2f).lfoMove(1, -2.f, 0, 0, 0.2f).lfoMove(2, -1.3f, 0, 0, 0.2f); });
    vt("Vital Tables", "Stabbed", "Complex/Stabbed", [](Maker &m) {
        m.shade(1, 1, 0, 0.3f, 0, 0.5f, 0, 0.5f, 3).heard(1, 0.8f, 0.f, 0.5f, 0.f, 0.42f).shade(2, 1, 0, 0.8f).heard(2, 0.4f, 0.f, 0.45f, 0.f, 0.4f, 0.3f); });
    return v;
}

int main(int argc, char **argv)
{
    MatrixIndex::initialize();
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode == "levels")
    { // seance-sounds levels <factory_patches dir> <Folder/File> ...: loudness of a held chord and where one note sits
        for (int i = 3; i < argc; ++i)
        {
            Maker m;
            std::ifstream f(fs::path(argv[2]) / (std::string(argv[i]) + ".sxsnp"));
            if (!f.good()) { std::printf("MISSING %s\n", argv[i]); continue; }
            std::stringstream ss; ss << f.rdbuf();
            if (!m.P.fromState(ss.str())) { std::printf("UNREADABLE %s\n", argv[i]); continue; }
            auto &e = *m.s;
            e.patch.copyValuesFrom(m.P);
            e.postLoad();
            e.reconcileWavetables();
            auto run = [&](std::vector<int> keys, int n, std::vector<float> &mono) {
                for (int k : keys) e.voiceManager->processNoteOnEvent(0, 0, k, -1, 0.85f, 0.f);
                size_t bp = 0;
                for (int j = 0; j < n; ++j)
                {
                    if (bp == 0) e.process(nullptr);
                    mono.push_back(0.5f * (e.output[0][bp] + e.output[1][bp]));
                    if (++bp == e.blockSize) bp = 0;
                }
                e.voiceManager->allSoundsOff();
                for (int j = 0; j < 48000; ++j) { if (bp == 0) e.process(nullptr); if (++bp == e.blockSize) bp = 0; }
            };
            std::vector<float> chord, one;
            run({48, 55, 60}, 48000 * 3 / 2, chord);
            run({57}, 48000, one);
            double sum = 0; float pk = 0;
            for (auto v : chord) { sum += (double)v * v; pk = std::max(pk, std::fabs(v)); }
            const double rmsDb = 10.0 * std::log10(std::max(1e-12, sum / (double)chord.size()));
            // the strongest period between 30 Hz and 1.5 kHz (normalised autocorrelation on 0.2 - 0.6 s of the single note)
            const size_t a0 = 9600, len = 19200;
            double best = -1; int bestLag = 0;
            for (int lag = 32; lag <= 1600; ++lag)
            {
                double c = 0, e0 = 0, e1 = 0;
                for (size_t j = a0; j < a0 + len && j + (size_t)lag < one.size(); j += 2) { c += one[j] * one[j + (size_t)lag]; e0 += one[j] * one[j]; e1 += one[j + (size_t)lag] * one[j + (size_t)lag]; }
                const double r = c / std::sqrt(std::max(1e-12, e0 * e1));
                if (r > best + 0.02) { best = r; bestLag = lag; } // (the first clear peak wins over its multiples)
            }
            const double hz = bestLag ? 48000.0 / bestLag : 0;
            std::printf("%-44s rms %6.1f dB  peak %.2f  note A3 sits at %6.1f Hz (%+.1f oct from 220, corr %.2f)\n", argv[i], rmsDb, pk, hz, hz > 0 ? std::log2(hz / 220.0) : 0.0, best);
        }
        return 0;
    }
    if ((mode != "factory" || argc < 3) && (mode != "vital" || argc < 4))
    {
        std::printf("seance-sounds factory <dir> | seance-sounds vital <Vital folder> <dir>\n");
        return 2;
    }
    const bool vital = mode == "vital";
    const fs::path vitalDir = vital ? fs::path(argv[2]) : fs::path(), out = fs::path(argv[vital ? 3 : 2]);
    int made = 0, failed = 0, skipped = 0;
    for (auto &sd : sounds())
    {
        if ((sd.vitalTable != nullptr) != vital) continue;
        Maker m;
        sd.make(m);
        if (sd.vitalTable)
        {
            const auto file = vitalDir / "Factory" / "Wavetables" / (std::string(sd.vitalTable) + ".vitaltable");
            std::ifstream f(file);
            if (!f.good()) { std::printf("skip: %-26s (no %s)\n", sd.name, file.u8string().c_str()); ++skipped; continue; }
            std::stringstream ss; ss << f.rdbuf();
            const auto nm = fs::path(sd.vitalTable).filename().u8string();
            for (int seat = 1; seat <= (int)numOps; ++seat)
                if ((int)std::round(m.P.sourceNodes[seat - 1].species.value) == SN::shadeSpecies) m.tableFile(seat, nm, ss.str());
        }
        if (std::string(sd.folder) == "Seance Twelve")
        {
            int loose = 0;
            for (int o = 0; o < (int)numOps; ++o)
            {
                bool tied = m.P.sourceNodes[o].active.value > 0.5f && m.P.mixerNodes[o].active.value > 0.5f;
                for (int t = 0; t < (int)numOps && !tied; ++t)
                    if (t != o) tied = m.P.sourceNodes[o].active.value > 0.5f && (m.P.matrixNodes[MatrixIndex::positionForSourceTarget(o, t)].active.value > 0.5f || m.P.matrixNodes[MatrixIndex::positionForSourceTarget(t, o)].active.value > 0.5f);
                if (!tied) ++loose;
            }
            if (loose) { std::printf("FAIL: %-26s %d of the twelve seats are not connected\n", sd.name, loose); ++failed; continue; }
        }
        if (m.write(out / sd.folder, sd.name)) ++made; else ++failed;
    }
    std::printf("%d sounds written to %s, %d failed, %d skipped\n", made, out.u8string().c_str(), failed, skipped);
    return failed ? 1 : 0;
}
