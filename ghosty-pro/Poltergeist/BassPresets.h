#pragma once
// Poltergeist BASS presets: 61 original bass sounds in 8 folders, plus SUMMON's bass words.
// Each preset is a recipe on top of the default patch (everything off / neutral), so it only states what it uses.
// Levels: BassTrim.h holds a per-preset trim measured by `PoltergeistBassTests tune` (peaks within ~3 dB).
#include "Summon.h"

namespace gs {

static const juce::StringArray kBassFolders { "GROWL / SCREECH", "RIDDIM", "WOBBLE", "REESE / NEURO", "ROLLING / CLEAN", "SUB", "808", "MID-BASS PLUCK", "LEADS" };

// a tiny builder so each recipe reads like a patch sheet
struct PB {
    Patch& p;
    PB& set(const juce::String& id, float v) { p[id] = v; return *this; }
    // ghost o (0-based): ratio, level, table wave (0 = classic morph), wave/bend
    PB& g(int o, float ratio, float level, int wtype = 1, float wave = 0) { set(opId(o, "ratio"), ratio); set(opId(o, "level"), level); set(opId(o, "wtype"), (float) wtype); set(opId(o, "wave"), wave); return *this; }
    PB& fine(int o, float c) { return set(opId(o, "fine"), c); }
    PB& env(int o, float a, float d, float s, float r) { set(opId(o, "a"), a); set(opId(o, "d"), d); set(opId(o, "s"), s); return set(opId(o, "r"), r); }
    PB& dahd(int o, float dly, float hold, bool loop) { set(opId(o, "dly"), dly); set(opId(o, "hold"), hold); return set(opId(o, "loop"), loop ? 1.0f : 0.0f); }
    PB& fb(int o, float v) { return set(opId(o, "fb"), v); }
    PB& vel(int o, float v) { return set(opId(o, "vel"), v); }
    PB& m(int f, int t, float v) { return set(modId(f, t), v); }                  // f haunts t (f = 6: the noise ghost)
    PB& noise(int type, float level, float cut, float res = 0.2f, int ftype = 0) { set("nz_type", (float) type); set("nz_level", level); set("nz_cut", cut); set("nz_res", res); return set("nz_ftype", (float) ftype); }
    PB& nenv(float a, float d, float s, float r) { set("nz_a", a); set("nz_d", d); set("nz_s", s); return set("nz_r", r); }
    PB& flt(int f, int type, float cut, float res, float drive = 0, float key = 0, float envAmt = 0) {
        const juce::String q = "f" + juce::String(f) + "_";
        set(q + "on", 1); set(q + "type", (float) type); set(q + "cut", cut); set(q + "res", res); set(q + "drive", drive); set(q + "key", key); return set(q + "env", envAmt);
    }
    PB& route(int r) { return set("flt_route", (float) r); }
    PB& fenv(float a, float d, float s, float r) { set("flt_a", a); set("flt_d", d); set("flt_s", s); return set("flt_r", r); }
    PB& sub(int wave, int oct, float level, float cut = 300, bool clean = true) { set("sub_on", 1); set("sub_wave", (float) wave); set("sub_oct", (float) oct); set("sub_level", level); set("sub_cut", cut); return set("sub_clean", clean ? 1.0f : 0.0f); }
    PB& dist(int type, float drive, float tone = 1, float mix = 1) { set("dist_on", 1); set("dist_type", (float) type); set("dist_drive", drive); set("dist_tone", tone); return set("dist_mix", mix); }
    PB& mb(float lo, float mid, float hi, float mix = 1) { set("mb_on", 1); set("mb_lo", lo); set("mb_mid", mid); set("mb_hi", hi); return set("mb_mix", mix); }
    PB& wob(int div, std::initializer_list<std::pair<float, float>> pts, float smooth = 1) {
        set("wob_div", (float) div); set("wob_smooth", smooth); set("wob_n", (float) pts.size()); int i = 0;
        for (auto& [x, y] : pts) { set("wob_x" + juce::String(i + 1), x); set("wob_y" + juce::String(i + 1), y); ++i; }
        return *this;
    }
    PB& lfo(int l, float rate, int shape = 0, bool sync = false, int div = 6) { const juce::String q = "lfo" + juce::String(l) + "_"; set(q + "rate", rate); set(q + "shape", (float) shape); set(q + "sync", sync ? 1.0f : 0.0f); return set(q + "div", (float) div); }
    PB& mm(int slot, int src, int dst, float amt) { const juce::String q = "mm" + juce::String(slot) + "_"; set(q + "src", (float) src); set(q + "dst", (float) dst); return set(q + "amt", amt); }
    PB& macro(int i, float v) { return set("macro_" + kMacroIds[i], v); }
};

// modulation sources (kModSrcs)
enum { srcWob = 1, srcL1, srcL2, srcDrift, srcFEnv, srcG1Env, srcVel, srcWheel, srcKey, srcRnd };
// wave tables (kWaveNames index)
enum { wSine = 1, wTri, wSaw, wSquare, wP25, wP12, wHalf, wAbs, wQuarter, wPara, wSineSaw, wDouble, wCamel, wSteps4, wSteps8, wSteps16, wForm2, wForm3, wForm5,
       wNoisy, wWail, wMoan, wHollow, wStack, wBuzz, wFold, wSpike, wThroatO, wThroatE, wSoftSq, wChoir };
enum { fLP12, fLP24, fBP, fHP12, fHP24, fNotch, fCombP, fCombN, fFormant, fDirty };
enum { dOD, dTube, dHard, dFold, dCrush, dDown };
enum { v4_1, v2_1, v1_1, v1_2, v1_2T, v1_2D, v1_4, v1_4T, v1_4D, v1_8, v1_8T, v1_8D, v1_16, v1_16T, v1_16D, v1_32, v1_32T };

// wobble shapes (also offered on the WOBBLE page)
struct WobShape { const char* name; std::vector<std::pair<float, float>> pts; float smooth; };
inline const std::vector<WobShape>& wobShapes() {
    static const std::vector<WobShape> v {
        { "Smooth", { { 0.0f, 1.0f }, { 0.5f, 0.0f } }, 1.0f },
        { "Wub", { { 0.0f, 0.0f }, { 0.5f, 1.0f } }, 1.0f },
        { "Saw Down", { { 0.0f, 1.0f }, { 0.97f, 0.0f } }, 0.0f },
        { "Saw Up", { { 0.0f, 0.0f }, { 0.97f, 1.0f } }, 0.0f },
        { "Gate", { { 0.0f, 1.0f }, { 0.48f, 1.0f }, { 0.5f, 0.0f }, { 0.98f, 0.0f } }, 0.0f },
        { "Stairs", { { 0.0f, 1.0f }, { 0.24f, 1.0f }, { 0.25f, 0.62f }, { 0.49f, 0.62f }, { 0.5f, 0.3f }, { 0.74f, 0.3f }, { 0.75f, 0.0f }, { 0.99f, 0.0f } }, 0.0f },
        { "Yoi", { { 0.0f, 0.1f }, { 0.15f, 0.95f }, { 0.45f, 0.6f }, { 0.7f, 0.08f } }, 0.8f },
        { "Talk", { { 0.0f, 0.0f }, { 0.3f, 1.0f }, { 0.55f, 0.2f }, { 0.8f, 0.75f } }, 1.0f },
        { "Gargle", { { 0.0f, 0.2f }, { 0.12f, 0.9f }, { 0.25f, 0.3f }, { 0.37f, 1.0f }, { 0.5f, 0.15f }, { 0.62f, 0.8f }, { 0.75f, 0.25f }, { 0.87f, 0.7f } }, 0.7f },
        { "Pluck", { { 0.0f, 1.0f }, { 0.12f, 0.35f }, { 0.4f, 0.0f } }, 1.0f },
    };
    return v;
}
inline void applyWob(PB& b, int div, int shape) { const auto& s = wobShapes()[(size_t) shape]; b.set("wob_div", (float) div); b.set("wob_smooth", s.smooth); b.set("wob_n", (float) s.pts.size()); int i = 0; for (auto& [x, y] : s.pts) { b.set("wob_x" + juce::String(i + 1), x); b.set("wob_y" + juce::String(i + 1), y); ++i; } }
enum { wsSmooth, wsWub, wsSawDown, wsSawUp, wsGate, wsStairs, wsYoi, wsTalk, wsGargle, wsPluck };

// what every bass preset shares: mono, fast legato glide, low end mono below 120 Hz, no space
inline PB bassStart(Patch& p, int octave = 0) {
    PB b { p };
    b.set("mono", 1).set("glide", 0.025f).set("octave", (float) octave).set("mono_below", 120).macro(4, 0).macro(0, 0.5f);
    b.env(0, 0.002f, 1.0f, 1.0f, 0.09f);
    return b;
}
inline void sustained(PB& b, std::initializer_list<int> ops) { for (int o : ops) b.env(o, 0.002f, 1.0f, 1.0f, 0.09f); }
// LEADS: mono with a quick glide, played around C4-C6; a little width and space, nothing below 150 Hz in the sides
inline PB leadStart(Patch& p, float glide = 0.04f) {
    PB b = bassStart(p);
    b.set("glide", glide).set("mono_below", 150).set("flt_key", 0.0f);
    b.env(0, 0.004f, 1.0f, 1.0f, 0.22f);
    return b;
}
inline void leadEnv(PB& b, std::initializer_list<int> ops, float s = 1.0f, float d = 1.0f) { for (int o : ops) b.env(o, 0.004f, d, s, 0.22f); }

struct BassPreset { const char* folder; const char* name; const char* tags; void (*make)(Patch&); };

inline const std::vector<BassPreset>& bassPresets() {
    static const std::vector<BassPreset> v {
        // ================= GROWL / SCREECH
        { "GROWL / SCREECH", "Grave Growler", "growl dubstep", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.8f, wSaw).g(1, 1, 0, wSine).g(2, 0.5f, 0, wSine); sustained(b, { 1, 2 });
            b.fb(1, 0.5f).m(1, 0, 0.65f).m(2, 1, 0.45f);
            b.flt(1, fFormant, 80, 0.7f).flt(2, fLP24, 3500, 0.2f, 0.3f).route(0);
            applyWob(b, v1_4, wsYoi); b.mm(1, srcWob, mdVowel, 0.7f).mm(2, srcWob, mdFm, 0.3f);
            b.dist(dOD, 0.45f).mb(0, 0.5f, 0.4f).set("ott_amt", 0.4f).sub(0, 0, 0.65f, 200); } },
        { "GROWL / SCREECH", "Yoi Phantom", "yoi talking growl dubstep", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.8f, wThroatO).g(1, 2, 0, wSine); sustained(b, { 1 }); b.m(1, 0, 0.55f).fb(1, 0.3f);
            b.flt(1, fFormant, 40, 0.8f).flt(2, fLP24, 4000, 0.15f, 0.2f);
            applyWob(b, v1_8, wsTalk); b.mm(1, srcWob, mdVowel, 0.9f).mm(2, srcWob, mdFm, 0.4f);
            b.dist(dTube, 0.4f).mb(0, 0.45f, 0.35f).set("ott_amt", 0.45f).sub(0, 0, 0.6f, 200); } },
        { "GROWL / SCREECH", "Throat Ripper", "growl tearout dubstep", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.75f, wThroatE).g(1, 1.5f, 0, wSine); sustained(b, { 1 }); b.fb(1, 0.7f).m(1, 0, 0.82f);   // (0.82: its top end back at ~18 % with FM-aware tables)
            b.noise(1, 0, 3000, 0.2f).m(6, 0, 0.15f);
            b.flt(1, fDirty, 1200, 0.6f, 0.3f);
            applyWob(b, v1_16T, wsStairs); b.mm(1, srcWob, mdF1Cut, 0.5f);
            b.dist(dFold, 0.4f, 0.8f).mb(0, 0.5f, 0.5f).set("ott_amt", 0.6f).sub(0, 0, 0.6f, 200); } },
        { "GROWL / SCREECH", "Banshee Screech", "screech dubstep", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.75f, wSaw).g(1, 3, 0, wSine); sustained(b, { 1 }); b.m(1, 0, 0.6f).set("sync_amt", 0.6f);
            b.flt(1, fBP, 1800, 0.5f, 0, 0.5f);
            applyWob(b, v1_8, wsSawDown); b.mm(1, srcWob, mdFm, 0.6f).mm(2, srcWob, mdF1Cut, 0.4f);
            b.dist(dHard, 0.5f).set("ott_amt", 0.5f).sub(0, 0, 0.6f, 200); } },
        { "GROWL / SCREECH", "Laser Wraith", "screech laser", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.7f, wSquare).g(1, 7, 0, wSine); sustained(b, { 1 }); b.m(1, 0, 0.45f).fb(1, 0.3f);
            b.flt(1, fHP12, 200, 0.2f).flt(2, fCombP, 261.63f, 0.7f, 0, 1);
            b.lfo(1, 4, 0, true, v1_16).mm(1, srcL1, mdFm, 0.5f);
            b.dist(dOD, 0.6f).set("ott_amt", 0.5f).sub(0, 0, 0.6f, 200); } },
        { "GROWL / SCREECH", "Talking Tomb", "talking yoi growl", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.8f, wSaw).g(1, 1, 0, wSine).g(5, 0.5f, 0, wSine); sustained(b, { 1, 5 }); b.fb(1, 0.4f).m(1, 0, 0.55f).m(5, 0, 0.3f);
            b.flt(1, fFormant, 56, 0.75f).flt(2, fLP24, 2600, 0.25f, 0.2f);
            applyWob(b, v1_4, wsTalk); b.mm(1, srcWob, mdVowel, 0.8f);
            b.lfo(1, 1, 0, true, v1_2).mm(2, srcL1, mdF2Cut, 0.3f);
            b.dist(dOD, 0.35f).mb(0, 0.4f, 0.4f).set("ott_amt", 0.4f).sub(0, 0, 0.65f, 200); } },
        { "GROWL / SCREECH", "Monster Mouth", "growl monster yoi", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.75f, wWail).g(1, 1, 0, wSine).g(2, 2, 0, wSine).g(3, 0.5f, 0, wSine); sustained(b, { 1, 2, 3 });
            b.m(1, 0, 0.7f).m(2, 0, 0.4f).m(3, 1, 0.4f);
            b.flt(1, fFormant, 20, 0.8f).flt(2, fDirty, 2500, 0.3f, 0.2f);
            applyWob(b, v1_8T, wsYoi); b.mm(1, srcWob, mdVowel, 1.0f);
            b.dist(dTube, 0.6f).set("ott_amt", 0.5f).sub(0, 0, 0.6f, 200); } },
        { "GROWL / SCREECH", "Possessed Engine", "growl metallic", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.75f, wSaw).g(1, 1.4142f, 0, wSine).g(2, 3.5f, 0, wSine); sustained(b, { 1, 2 }); b.fb(1, 0.6f).m(1, 0, 0.6f).m(2, 1, 0.3f);
            b.flt(1, fCombN, 300, 0.6f, 0, 0.5f).flt(2, fLP24, 2000, 0.3f, 0.5f);
            applyWob(b, v1_16, wsGate); b.mm(1, srcWob, mdF2Cut, 0.5f);
            b.set("ott_amt", 0.6f).mb(0, 0.4f, 0.4f).sub(0, 0, 0.6f, 200); } },
        { "GROWL / SCREECH", "Rattle Screamer", "screech", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.75f, wP25).g(1, 2, 0, wSine); sustained(b, { 1 }); b.fb(1, 0.8f).m(1, 0, 0.65f);
            b.flt(1, fNotch, 900, 0.6f);
            applyWob(b, v1_32, wsSmooth); b.mm(1, srcWob, mdF1Cut, 0.4f);
            b.dist(dHard, 0.7f).mb(0, 0.5f, 0.7f).sub(0, 0, 0.6f, 200); } },
        { "GROWL / SCREECH", "Crypt Gargle", "growl talking gargle", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.8f, wCamel).g(1, 0.5f, 0, wSine); sustained(b, { 1 }); b.m(1, 0, 0.6f);
            b.noise(1, 0, 2000).m(6, 1, 0.2f);
            b.flt(1, fFormant, 113, 0.7f).flt(2, fLP12, 3000, 0.2f);
            applyWob(b, v1_4D, wsGargle); b.mm(1, srcWob, mdVowel, 0.7f);
            b.dist(dOD, 0.4f).set("ott_amt", 0.45f).sub(0, 0, 0.6f, 200); } },
        { "GROWL / SCREECH", "Hollow Howl", "growl howl", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.8f, wHollow).g(1, 1, 0, wSine); sustained(b, { 1 }); b.m(1, 0, 0.5f).fb(1, 0.3f);
            b.flt(1, fBP, 700, 0.7f, 0, 0.3f);
            applyWob(b, v1_2, wsSawUp); b.mm(1, srcWob, mdF1Cut, 0.7f);
            b.dist(dOD, 0.5f).set("ott_amt", 0.35f).sub(0, 0, 0.65f, 200); } },
        { "GROWL / SCREECH", "Feedback Fiend", "growl screech feedback", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.75f, wSine).g(1, 1, 0, wSine); sustained(b, { 1 }); b.fb(0, 0.9f).fb(1, 0.7f).m(1, 0, 0.7f);
            b.flt(1, fLP24, 2500, 0.5f, 0.6f);
            b.lfo(1, 4, 1, true, v1_8T).mm(1, srcL1, mdFm, 0.7f);
            b.set("ott_amt", 0.7f).sub(0, 0, 0.6f, 200); } },

        // ================= RIDDIM
        { "RIDDIM", "Riddim Revenant", "riddim", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.8f, wSquare).g(1, 1, 0, wSine); sustained(b, { 1 }); b.m(1, 0, 0.4f);
            b.flt(1, fLP24, 400, 0.4f, 0.5f);
            applyWob(b, v1_8T, wsStairs); b.mm(1, srcWob, mdF1Cut, 0.9f);
            b.dist(dHard, 0.6f).set("ott_amt", 0.7f).sub(0, 0, 0.65f, 200); } },
        { "RIDDIM", "Stair Ghoul", "riddim", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.8f, wSaw).g(2, 0.5f, 0.3f, wSquare); sustained(b, { 2 });
            b.flt(1, fCombP, 130.81f, 0.6f, 0, 1).flt(2, fLP24, 500, 0.35f, 0.3f);
            applyWob(b, v1_4T, wsStairs); b.mm(1, srcWob, mdF2Cut, 0.8f);
            b.dist(dHard, 0.5f).set("ott_amt", 0.6f).sub(0, 0, 0.65f, 200); } },
        { "RIDDIM", "Ghost Stomper", "riddim tearout", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.8f, wSquare).g(1, 2, 0, wSine); sustained(b, { 1 }); b.m(1, 0, 0.3f);
            b.flt(1, fDirty, 600, 0.5f, 0.3f);
            applyWob(b, v1_16, wsSmooth); b.mm(1, srcWob, mdF1Cut, 0.5f);
            static const float PAT[16] = { 1, 1, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 1, 1 };
            for (int i = 0; i < 16; ++i) b.set("seq_" + juce::String(i + 1), PAT[i]);
            b.set("seq_on", 1).set("seq_dest", 0).set("seq_div", 7).set("seq_glide", 0.05f);
            b.dist(dOD, 0.7f).set("ott_amt", 0.8f).sub(0, 0, 0.6f, 200); } },
        { "RIDDIM", "Bone Saw", "riddim", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.8f, wP12).g(1, 2, 0, wSine); sustained(b, { 1 }); b.m(1, 0, 0.35f);
            b.flt(1, fNotch, 600, 0.5f);
            applyWob(b, v1_8, wsSawDown); b.mm(1, srcWob, mdF1Cut, 0.6f);
            b.dist(dCrush, 0.35f).set("ott_amt", 0.6f).sub(0, 0, 0.65f, 200); } },
        { "RIDDIM", "Chain Rattle", "riddim metallic", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.8f, wSquare).g(1, 3.5f, 0, wSine); sustained(b, { 1 }); b.m(1, 0, 0.3f);
            b.flt(1, fCombN, 130.81f, 0.7f, 0, 1).flt(2, fLP24, 450, 0.4f, 0.3f);
            applyWob(b, v1_8T, wsGate); b.mm(1, srcWob, mdF2Cut, 0.7f);
            b.dist(dHard, 0.7f).set("ott_amt", 0.5f).sub(0, 0, 0.65f, 200); } },
        { "RIDDIM", "Reaper Bounce", "riddim wobble", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.8f, wSoftSq).g(1, 1, 0, wSine); sustained(b, { 1 }); b.m(1, 0, 0.5f).fb(1, 0.4f);
            b.flt(1, fLP24, 300, 0.45f, 0.4f);
            applyWob(b, v1_4T, wsWub); b.mm(1, srcWob, mdF1Cut, 0.9f).mm(2, srcWob, mdFm, 0.4f);
            b.dist(dTube, 0.6f).set("ott_amt", 0.6f).sub(0, 0, 0.65f, 200); } },
        { "RIDDIM", "Tomb Shuffle", "riddim", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.8f, wSaw);
            b.flt(1, fLP24, 350, 0.5f, 0.4f).flt(2, fFormant, 600, 0.6f);
            applyWob(b, v1_8D, wsStairs); b.mm(1, srcWob, mdF1Cut, 0.8f);
            b.lfo(1, 1, 1, true, v1_2).mm(2, srcL1, mdVowel, 0.4f);
            b.dist(dHard, 0.5f).set("ott_amt", 0.5f).sub(0, 0, 0.65f, 200); } },

        // ================= WOBBLE
        { "WOBBLE", "Wobble Wraith", "wobble dubstep", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.7f, wSaw).g(2, 1, 0.5f, wSquare).fine(2, 7); sustained(b, { 2 });
            b.flt(1, fLP24, 300, 0.55f, 0.3f);
            applyWob(b, v1_4, wsSmooth); b.mm(1, srcWob, mdF1Cut, 0.9f);
            b.dist(dOD, 0.3f).sub(0, 0, 0.65f, 200); } },
        { "WOBBLE", "Wub Spirit", "wobble wub", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.8f, wSaw).g(1, 1, 0, wSine); sustained(b, { 1 }); b.m(1, 0, 0.3f);
            b.flt(1, fLP24, 250, 0.5f, 0.3f);
            applyWob(b, v1_8, wsWub); b.mm(1, srcWob, mdF1Cut, 0.9f);
            b.dist(dOD, 0.4f).set("ott_amt", 0.4f).sub(0, 0, 0.65f, 200); } },
        { "WOBBLE", "Triplet Haunt", "wobble", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.8f, wSquare);
            b.flt(1, fLP24, 280, 0.5f, 0.3f);
            applyWob(b, v1_8T, wsSmooth); b.mm(1, srcWob, mdF1Cut, 0.85f);
            b.dist(dTube, 0.4f).sub(0, 0, 0.65f, 200); } },
        { "WOBBLE", "Slow Specter", "wobble reese", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.65f, wSaw).g(2, 1, 0.55f, wSaw).fine(2, 9); sustained(b, { 2 });
            b.flt(1, fLP24, 200, 0.6f, 0.3f);
            applyWob(b, v1_2, wsSmooth); b.mm(1, srcWob, mdF1Cut, 0.9f);
            b.dist(dOD, 0.3f).sub(0, 0, 0.65f, 200); } },
        { "WOBBLE", "Dotted Drifter", "wobble", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.8f, wSaw);
            b.flt(1, fLP12, 300, 0.7f);
            applyWob(b, v1_4D, wsWub); b.mm(1, srcWob, mdF1Cut, 0.8f);
            b.dist(dOD, 0.35f).sub(0, 0, 0.65f, 200); } },
        { "WOBBLE", "Stutter Shade", "wobble", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.6f, wSquare).g(1, 2, 0, wSine); sustained(b, { 1 }); b.m(1, 0, 0.3f);
            b.flt(1, fLP24, 400, 0.45f, 0.3f);
            applyWob(b, v1_16, wsSmooth); b.mm(1, srcWob, mdF1Cut, 0.7f);
            b.set("ott_amt", 0.5f).sub(0, 0, 0.65f, 200); } },
        { "WOBBLE", "Vowel Wobbler", "wobble yoi talking", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.8f, wSaw).g(1, 1, 0, wSine); sustained(b, { 1 }); b.m(1, 0, 0.35f);
            b.flt(1, fFormant, 28, 0.75f).flt(2, fLP24, 3000, 0.2f);
            applyWob(b, v1_4, wsSmooth); b.mm(1, srcWob, mdVowel, 0.9f);
            b.dist(dOD, 0.35f).set("ott_amt", 0.35f).sub(0, 0, 0.65f, 200); } },
        { "WOBBLE", "Saw Wobble Ghoul", "wobble", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.8f, wSaw).g(1, 0.5f, 0, wSine); sustained(b, { 1 }); b.m(1, 0, 0.3f);
            b.flt(1, fLP24, 260, 0.5f, 0.4f);
            applyWob(b, v1_8, wsSawDown); b.mm(1, srcWob, mdF1Cut, 0.9f);
            b.dist(dOD, 0.5f).sub(0, 0, 0.65f, 200); } },
        { "WOBBLE", "Wheel Wobbler", "wobble", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.75f, wSaw).g(2, 1, 0.4f, wSquare).fine(2, -6); sustained(b, { 2 });
            b.flt(1, fLP24, 250, 0.5f, 0.3f);
            applyWob(b, v1_8, wsSmooth); b.mm(1, srcWob, mdF1Cut, 0.6f).mm(2, srcWheel, mdF1Cut, 0.5f);
            b.dist(dTube, 0.35f).sub(0, 0, 0.65f, 200); } },

        // ================= REESE / NEURO
        { "REESE / NEURO", "Reese Revenant", "reese dnb", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.55f, wSaw).g(2, 1, 0.55f, wSaw).fine(2, 12).g(4, 1, 0.45f, wSaw).fine(4, -9); sustained(b, { 2, 4 });
            b.macro(1, 0.3f).flt(1, fLP24, 900, 0.3f, 0.4f);
            b.lfo(1, 0.15f).mm(1, srcL1, mdF1Cut, 0.2f);
            b.dist(dOD, 0.3f).sub(0, 0, 0.6f, 200); } },
        { "REESE / NEURO", "Neuro Necro", "neuro dnb", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.75f, wSaw).g(1, 1, 0, wSine).g(5, 2, 0, wSine); sustained(b, { 1, 5 }); b.fb(1, 0.5f).m(1, 0, 0.4f).m(5, 1, 0.3f);
            b.flt(1, fNotch, 1200, 0.5f).flt(2, fLP24, 2500, 0.3f, 0.4f);
            b.lfo(1, 1, 1, true, v1_2).mm(1, srcL1, mdF1Cut, 0.5f).lfo(2, 1, 0, true, v1_4).mm(2, srcL2, mdFm, 0.5f);
            b.dist(dOD, 0.5f).mb(0, 0.6f, 0.5f).set("ott_amt", 0.5f).sub(0, 0, 0.6f, 200); } },
        { "REESE / NEURO", "Phase Phantom", "reese", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.6f, wSaw).g(2, 1, 0.6f, wSaw).fine(2, 8); sustained(b, { 2 });
            b.flt(1, fCombN, 150, 0.5f, 0, 0.6f).flt(2, fLP24, 1200, 0.25f, 0.3f);
            b.lfo(1, 0.2f).mm(1, srcL1, mdF1Cut, 0.4f);
            b.sub(0, 0, 0.6f, 200); } },
        { "REESE / NEURO", "Hollow Reese", "reese", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.6f, wSquare).fine(0, -8).g(2, 1, 0.6f, wSquare).fine(2, 8); sustained(b, { 2 });
            b.flt(1, fLP24, 700, 0.3f, 0.3f);
            b.lfo(1, 0.12f).mm(1, srcL1, mdF1Cut, 0.25f);
            b.sub(0, 0, 0.6f, 200); } },
        { "REESE / NEURO", "Neuro Gnasher", "neuro", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.75f, wSaw).g(1, 2, 0, wSine); sustained(b, { 1 }); b.fb(1, 0.5f).m(1, 0, 0.6f);
            b.noise(1, 0, 1500).m(6, 1, 0.15f);
            b.flt(1, fDirty, 1500, 0.6f, 0.2f);
            applyWob(b, v1_16, wsStairs); b.mm(1, srcWob, mdFm, 0.5f);
            b.lfo(1, 1, 1, true, v1_2).mm(2, srcL1, mdF1Cut, 0.4f);
            b.dist(dFold, 0.35f, 0.8f).set("ott_amt", 0.6f).sub(0, 0, 0.6f, 200); } },
        { "REESE / NEURO", "Drone Reese", "reese", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.6f, wSineSaw).g(2, 1, 0.55f, wSaw).fine(2, 15); sustained(b, { 2 });
            b.macro(1, 0.45f).flt(1, fLP24, 500, 0.2f, 0.3f);
            b.lfo(1, 0.08f, 6).mm(1, srcL1, mdF1Cut, 0.3f);
            b.dist(dTube, 0.3f).sub(0, 0, 0.6f, 200); } },
        { "REESE / NEURO", "Jungle Specter", "reese jungle dnb", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.6f, wSaw).g(2, 1, 0.6f, wSaw).fine(2, 7); sustained(b, { 2 });
            b.flt(1, fLP12, 1200, 0.25f, 0, 0, 0.3f).fenv(0.002f, 0.5f, 0.3f, 0.1f);
            b.set("glide", 0.05f).sub(0, 0, 0.6f, 200); } },

        // ================= ROLLING / CLEAN
        { "ROLLING / CLEAN", "Rolling Phantom", "rolling progressive clean", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.8f, wSaw).env(0, 0.002f, 0.4f, 0.7f, 0.08f);
            b.flt(1, fLP24, 300, 0.35f, 0, 0, 0.45f).fenv(0.001f, 0.18f, 0.1f, 0.08f);
            b.sub(0, 0, 0.5f, 200); } },
        { "ROLLING / CLEAN", "Night Drive", "rolling progressive", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.7f, wSquare).g(1, 2, 0, wSine).env(0, 0.002f, 0.4f, 0.7f, 0.08f).env(1, 0.001f, 0.2f, 0.2f, 0.08f); b.m(1, 0, 0.2f);
            b.flt(1, fLP24, 400, 0.3f, 0, 0, 0.4f).fenv(0.001f, 0.25f, 0.1f, 0.08f);
            b.sub(0, 0, 0.5f, 200); } },
        { "ROLLING / CLEAN", "Pulse Runner", "rolling", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.9f, wP25).env(0, 0.002f, 0.3f, 0.6f, 0.07f);
            b.flt(1, fLP12, 900, 0.3f, 0, 0, 0.35f).fenv(0.001f, 0.15f, 0.1f, 0.07f);
            b.sub(1, 0, 0.45f, 200); } },
        { "ROLLING / CLEAN", "Deep Rolling Spirit", "rolling deep progressive", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.6f, wSaw).g(2, 1, 0.4f, wSaw).fine(2, 6).env(0, 0.002f, 0.5f, 0.7f, 0.08f).env(2, 0.002f, 0.5f, 0.7f, 0.08f);
            b.macro(1, 0.15f).flt(1, fLP24, 250, 0.25f, 0, 0, 0.5f).fenv(0.001f, 0.3f, 0.15f, 0.08f);
            b.sub(0, 0, 0.6f, 200); } },
        { "ROLLING / CLEAN", "Clean Ghost Bass", "clean", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.8f, wSine).g(1, 1, 0, wSine).g(3, 2, 0, wSine); sustained(b, { 1, 3 }); b.m(1, 0, 0.3f).m(3, 0, 0.1f);
            b.sub(0, 0, 0.5f, 200); } },
        { "ROLLING / CLEAN", "Velvet Undertow", "clean melodic", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.8f, wTri).g(1, 1, 0, wSine); sustained(b, { 1 }); b.m(1, 0, 0.25f);
            b.flt(1, fLP12, 900, 0.2f, 0, 0, 0.2f).fenv(0.001f, 0.3f, 0.3f, 0.1f);
            b.sub(0, 0, 0.5f, 200); } },
        { "ROLLING / CLEAN", "Melodic Mist", "melodic clean progressive", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.8f, wSineSaw).set("glide", 0.05f);
            b.flt(1, fLP24, 600, 0.2f, 0, 0, 0.3f).fenv(0.002f, 0.4f, 0.3f, 0.1f);
            b.sub(0, 0, 0.4f, 200); } },
        { "ROLLING / CLEAN", "Offbeat Haunt", "rolling progressive", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.8f, wSaw).env(0, 0.002f, 0.25f, 0.4f, 0.06f);
            b.flt(1, fLP24, 350, 0.45f, 0, 0, 0.6f).fenv(0.001f, 0.12f, 0.0f, 0.06f);
            b.sub(0, 0, 0.55f, 200); } },
        { "ROLLING / CLEAN", "Progressive Pulse", "progressive rolling", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.6f, wSquare).g(2, 1, 0.4f, wSaw).fine(2, 5).env(0, 0.002f, 0.4f, 0.65f, 0.08f).env(2, 0.002f, 0.4f, 0.65f, 0.08f);
            b.flt(1, fLP24, 450, 0.3f, 0, 0, 0.45f).fenv(0.001f, 0.2f, 0.1f, 0.08f);
            b.sub(0, 0, 0.55f, 200); } },

        // ================= SUB
        { "SUB", "Pure Sub Spirit", "sub deep", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0, wSine).sub(0, 0, 1.0f, 120); } },
        { "SUB", "Warm Sub Ghost", "sub warm", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.15f, wSine).sub(3, 0, 0.9f, 160); } },
        { "SUB", "Triangle Tomb", "sub", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0, wSine).sub(1, 0, 0.9f, 200); } },
        { "SUB", "Square Sub Shade", "sub", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0, wSine).sub(2, 0, 1.0f, 150); } },
        { "SUB", "Deep Sub Abyss", "sub deep", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.1f, wSine).sub(0, 1, 0.95f, 120); } },

        // ================= 808
        { "808", "808 Revenant", "808", [](Patch& p) { auto b = bassStart(p, -1);
            b.g(0, 1, 1.0f, wSine).env(0, 0.001f, 2.2f, 0.0f, 0.25f).set("penv_amt", 14).set("penv_dec", 0.05f).set("glide", 0.08f);
            b.dist(dTube, 0.35f, 1, 0.6f); } },
        { "808", "808 Distorted Spirit", "808 dirty", [](Patch& p) { auto b = bassStart(p, -1);
            b.g(0, 1, 1.0f, wSine).env(0, 0.001f, 2.0f, 0.0f, 0.2f).set("penv_amt", 18).set("penv_dec", 0.05f).set("glide", 0.08f);
            b.dist(dOD, 0.7f, 0.6f).mb(0, 0.3f, 0.5f); } },
        { "808", "808 Long Haunt", "808 long", [](Patch& p) { auto b = bassStart(p, -1);
            b.g(0, 1, 1.0f, wSine).env(0, 0.001f, 4.0f, 0.0f, 0.4f).set("penv_amt", 8).set("penv_dec", 0.06f).set("glide", 0.08f).dist(dTube, 0.25f, 1, 0.5f); } },
        { "808", "808 Punch Wraith", "808 punch", [](Patch& p) { auto b = bassStart(p, -1);
            b.g(0, 1, 1.0f, wSine).env(0, 0.001f, 1.2f, 0.0f, 0.15f).g(1, 2, 0, wSine).env(1, 0.001f, 0.04f, 0.0f, 0.04f).m(1, 0, 0.15f);
            b.set("penv_amt", 24).set("penv_dec", 0.03f).set("glide", 0.06f).dist(dHard, 0.3f); } },
        { "808", "808 Glide Ghost", "808 slide glide", [](Patch& p) { auto b = bassStart(p, -1);
            b.g(0, 1, 1.0f, wSine).env(0, 0.001f, 3.0f, 0.3f, 0.3f).set("penv_amt", 10).set("penv_dec", 0.05f).set("glide", 0.15f).dist(dTube, 0.25f, 1, 0.5f); } },
        { "808", "808 Fold Phantom", "808 dirty", [](Patch& p) { auto b = bassStart(p, -1);
            b.g(0, 1, 1.0f, wSine).env(0, 0.001f, 2.2f, 0.0f, 0.25f).set("penv_amt", 12).set("penv_dec", 0.05f).set("glide", 0.08f);
            b.dist(dFold, 0.45f, 0.5f, 0.7f); } },
        { "808", "808 Rounded Tomb", "808 warm", [](Patch& p) { auto b = bassStart(p, -1);
            b.g(0, 1, 0.95f, wPara).env(0, 0.001f, 2.5f, 0.0f, 0.25f).set("penv_amt", 12).set("penv_dec", 0.05f).set("glide", 0.08f);
            b.flt(1, fLP12, 400, 0.2f).dist(dTube, 0.25f, 1, 0.5f); } },

        // ================= MID-BASS PLUCK
        { "MID-BASS PLUCK", "Pluck Poltergeist", "pluck midbass", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 1.0f, wSine).env(0, 0.001f, 0.3f, 0.0f, 0.1f).g(1, 2, 0, wSine).env(1, 0.001f, 0.12f, 0.0f, 0.1f).m(1, 0, 0.55f).sub(0, 0, 0.5f, 200);
            b.flt(1, fLP24, 1800, 0.3f, 0, 0, 0.4f).fenv(0.001f, 0.2f, 0.0f, 0.1f).set("ott_amt", 0.3f); } },
        { "MID-BASS PLUCK", "Rubber Ghost", "pluck midbass", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.8f, wSquare).env(0, 0.001f, 0.35f, 0.0f, 0.1f);
            b.flt(1, fLP24, 300, 0.6f, 0, 0, 0.7f).fenv(0.001f, 0.15f, 0.0f, 0.1f).dist(dOD, 0.3f); } },
        { "MID-BASS PLUCK", "Bell Bass Wisp", "pluck midbass bell", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.8f, wSine).env(0, 0.001f, 0.6f, 0.0f, 0.15f).g(1, 3.5f, 0, wSine).env(1, 0.001f, 0.2f, 0.0f, 0.1f).m(1, 0, 0.4f);
            b.g(2, 1, 0.4f, wSine).env(2, 0.001f, 0.6f, 0.0f, 0.15f).sub(0, 0, 0.3f, 200); } },
        { "MID-BASS PLUCK", "Knock Spirit", "pluck midbass", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 1.0f, wTri).env(0, 0.001f, 0.3f, 0.0f, 0.1f).g(1, 1, 0, wSine).env(1, 0.001f, 0.08f, 0.0f, 0.06f).fb(1, 0.5f).m(1, 0, 0.6f).sub(0, 0, 0.5f, 200);
            b.set("penv_amt", 7).set("penv_dec", 0.03f).flt(1, fLP12, 2000, 0.2f); } },
        { "MID-BASS PLUCK", "Zap Specter", "pluck midbass zap", [](Patch& p) { auto b = bassStart(p);
            b.g(0, 1, 0.8f, wSaw).env(0, 0.001f, 0.3f, 0.0f, 0.1f);
            b.flt(1, fLP24, 500, 0.7f, 0, 0, 0.8f).fenv(0.001f, 0.1f, 0.0f, 0.08f).set("penv_amt", 12).set("penv_dec", 0.02f).dist(dOD, 0.4f).sub(0, 0, 0.35f, 200); } },

        // ================= LEADS (FM screech / metal leads; simple, clean, big: the melody carries it)
        { "LEADS", "Wraith Whistle", "lead clean fm whistle", [](Patch& p) { auto b = leadStart(p, 0.05f);
            b.g(0, 1, 0.8f, wSine).g(1, 2, 0, wSine); leadEnv(b, { 1 }, 0.6f, 0.5f); b.m(1, 0, 0.32f).fb(1, 0.15f);
            b.lfo(1, 5.2f, 0).mm(1, srcL1, mdPitch, 0.005f).macro(1, 0.15f).macro(4, 0.22f); } },
        { "LEADS", "Sync Siren", "lead sync screech", [](Patch& p) { auto b = leadStart(p);
            b.g(0, 1, 0.75f, wSaw).set("sync_amt", 0.45f);
            b.flt(1, fLP24, 5200, 0.2f, 0, 0, 0.25f).fenv(0.002f, 0.35f, 0.4f, 0.2f);
            b.dist(dOD, 0.25f).macro(1, 0.3f).macro(4, 0.18f); } },
        { "LEADS", "Metal Banshee", "lead metal screech fm", [](Patch& p) { auto b = leadStart(p);
            b.g(0, 1, 0.75f, wSaw).g(1, 3.5f, 0, wSine); leadEnv(b, { 1 }, 0.7f, 0.6f); b.m(1, 0, 0.42f);
            b.flt(2, fCombP, 261.63f, 0.45f, 0, 1).flt(1, fLP24, 7000, 0.15f);
            b.dist(dHard, 0.3f, 0.8f).set("ott_amt", 0.3f).macro(1, 0.2f).macro(4, 0.15f); } },
        { "LEADS", "Chrome Howl", "lead vowel wah metal", [](Patch& p) { auto b = leadStart(p);
            b.g(0, 1, 0.75f, wSquare).g(1, 2, 0, wSine); leadEnv(b, { 1 }); b.m(1, 0, 0.4f);
            b.flt(1, fFormant, 300, 0.75f).lfo(1, 1, 1, true, v1_4).mm(1, srcL1, mdVowel, 0.35f);
            b.dist(dTube, 0.3f).macro(1, 0.25f).macro(4, 0.18f); } },
        { "LEADS", "Phantom Pluck Lead", "lead pluck fm", [](Patch& p) { auto b = leadStart(p, 0.02f);
            b.g(0, 1, 0.8f, wSaw).env(0, 0.002f, 0.6f, 0.45f, 0.25f).g(1, 2, 0, wSine).env(1, 0.001f, 0.25f, 0.15f, 0.2f).m(1, 0, 0.55f);
            b.flt(1, fLP24, 1800, 0.3f, 0, 0, 0.45f).fenv(0.001f, 0.3f, 0.2f, 0.2f).macro(1, 0.25f).macro(4, 0.25f); } },
        { "LEADS", "Glass Specter", "lead bell glass fm clean", [](Patch& p) { auto b = leadStart(p);
            b.g(0, 1, 0.8f, wSine).g(1, 3.5f, 0, wSine).env(1, 0.002f, 0.8f, 0.25f, 0.3f).m(1, 0, 0.38f);
            b.g(2, 2, 0.35f, wSine); leadEnv(b, { 2 }); b.macro(1, 0.2f).macro(4, 0.28f); } },
        { "LEADS", "Razor Choir", "lead choir big wide", [](Patch& p) { auto b = leadStart(p);
            b.g(0, 1, 0.55f, wChoir).g(2, 1, 0.45f, wSaw).fine(2, 7); leadEnv(b, { 2 });
            b.flt(1, fLP24, 6000, 0.2f).set("ott_amt", 0.25f).macro(1, 0.5f).macro(4, 0.2f); } },
        { "LEADS", "Scream Engine", "lead scream metal aggressive", [](Patch& p) { auto b = leadStart(p);
            b.g(0, 1, 0.75f, wThroatE).g(1, 1, 0, wSine); leadEnv(b, { 1 }); b.fb(1, 0.45f).m(1, 0, 0.5f);
            b.flt(1, fBP, 1500, 0.3f, 0, 0.7f).dist(dFold, 0.35f, 0.8f).set("ott_amt", 0.15f).macro(1, 0.2f).macro(4, 0.12f); } },   // (band follows the note: same weight in every register)
        { "LEADS", "Hollow Halo", "lead hollow clean wide", [](Patch& p) { auto b = leadStart(p, 0.06f);
            b.g(0, 1, 0.8f, wHollow).g(1, 2, 0, wSine); leadEnv(b, { 1 }, 0.5f, 0.8f); b.m(1, 0, 0.28f);
            b.flt(1, fLP12, 5000, 0.15f).macro(1, 0.4f).macro(4, 0.3f); } },
        { "LEADS", "Laser Grave", "lead laser sync zap", [](Patch& p) { auto b = leadStart(p, 0.03f);
            b.g(0, 1, 0.75f, wSaw).set("sync_amt", 0.3f).set("penv_amt", 12).set("penv_dec", 0.07f);
            b.flt(1, fLP24, 4500, 0.25f, 0, 0, 0.3f).fenv(0.001f, 0.2f, 0.3f, 0.2f).dist(dOD, 0.2f).macro(1, 0.2f).macro(4, 0.15f); } },
    };
    return v;
}

} // namespace gs

#include "BassTrim.h"

namespace gs {
// the whole patch for bass preset i: defaults + recipe + measured level
inline Patch bassPatch(int i) {
    Patch p = Summoner::base();
    const auto& v = bassPresets();
    if (!juce::isPositiveAndBelow(i, (int) v.size())) return p;
    v[(size_t) i].make(p);
    p["master"] = juce::jlimit(-24.0f, 6.0f, bassTrim(v[(size_t) i].name));
    return p;
}

// SUMMON's bass words. Returns an empty patch when the words don't ask for a bass (then the original SUMMON runs).
struct BassSummon {
    static juce::String folderFor(const juce::StringArray& w) {
        auto has = [&](std::initializer_list<const char*> ws) { for (auto x : ws) if (w.contains(x)) return true; return false; };
        if (has({ "808", "808s" })) return "808";
        if (has({ "lead", "leads" }) && has({ "screech", "metal", "metallic", "fm", "sync", "siren", "laser", "scream" })) return "LEADS";
        if (has({ "riddim" })) return "RIDDIM";
        if (has({ "reese", "neuro", "dnb", "jungle" })) return "REESE / NEURO";
        if (has({ "rolling", "progressive", "melodic" }) || (has({ "clean" }) && has({ "bass", "bassline" }))) return "ROLLING / CLEAN";
        if (has({ "growl", "screech", "yoi", "talking", "dubstep", "tearout", "monster" })) return "GROWL / SCREECH";
        if (has({ "wobble", "wub", "wubs" })) return "WOBBLE";
        if (has({ "sub", "subbass" }) && (has({ "deep" }) || has({ "sub" }))) return "SUB";
        if (has({ "midbass", "mid" }) && has({ "pluck", "bass" })) return "MID-BASS PLUCK";
        return {};
    }
    static Patch summon(const juce::String& query, uint32_t seed, juce::String* described) {
        const auto w = Summoner::tokenize(query);
        const auto folder = folderFor(w);
        if (folder.isEmpty()) return {};
        std::mt19937 rng(seed);
        auto R = [&](float a, float b) { std::uniform_real_distribution<float> u(a, b); return u(rng); };
        // candidates: the folder; words like "yoi" / "screech" / "talking" narrow it to presets tagged with them
        const auto& v = bassPresets();
        std::vector<int> pool, tagged;
        for (int i = 0; i < (int) v.size(); ++i) if (folder == v[(size_t) i].folder) {
            pool.push_back(i);
            juce::StringArray tags; tags.addTokens(v[(size_t) i].tags, " ", "");
            for (auto& x : w) if (x.length() > 2 && tags.contains(x) && x != "bass") { tagged.push_back(i); break; }
        }
        const auto& from = tagged.empty() ? pool : tagged;
        const int pick = from[(size_t) (rng() % from.size())];
        Patch p = bassPatch(pick);
        // a fresh take: nudge the FM depths, filters, wobble rate and drive
        for (int f = 0; f < kOps; ++f) for (int t = 0; t < kOps; ++t) if (f != t && p[modId(f, t)] > 0.01f) p[modId(f, t)] = juce::jlimit(0.0f, 1.0f, p[modId(f, t)] * R(0.8f, 1.2f));
        for (auto* id : { "f1_cut", "f2_cut" }) p[id] = juce::jlimit(30.0f, 18000.0f, p[id] * R(0.75f, 1.3f));
        if (p["dist_on"] > 0.5f) p["dist_drive"] = juce::jlimit(0.0f, 1.0f, p["dist_drive"] + R(-0.1f, 0.1f));
        bool usesWob = false; for (int k = 1; k <= kModSlots; ++k) usesWob = usesWob || p["mm" + juce::String(k) + "_src"] == (float) srcWob;
        if (usesWob && folder != "RIDDIM") { const int divs[] = { v1_4, v1_8, v1_8T, v1_4T, v1_16 }; if (rng() % 2) p["wob_div"] = (float) divs[rng() % 5]; }
        if (usesWob && folder == "RIDDIM") { const int divs[] = { v1_8T, v1_4T, v1_8, v1_8D }; if (rng() % 2) p["wob_div"] = (float) divs[rng() % 4]; }
        // the usual adjectives
        auto has = [&](std::initializer_list<const char*> ws) { for (auto x : ws) if (w.contains(x)) return true; return false; };
        if (has({ "dark", "deep", "evil", "night" })) { p["f1_cut"] *= 0.7f; p["f2_cut"] *= 0.7f; }
        if (has({ "bright", "shiny" })) { p["f1_cut"] *= 1.5f; p["f2_cut"] *= 1.5f; }
        if (has({ "dirty", "nasty", "gritty", "filthy", "heavy", "hard" })) { p["dist_on"] = 1; p["dist_drive"] = juce::jmin(1.0f, p["dist_drive"] + 0.2f); p["ott_amt"] = juce::jmin(1.0f, p["ott_amt"] + 0.2f); }
        if (has({ "clean", "pure", "smooth" }) && folder != "ROLLING / CLEAN") { p["dist_on"] = 0; p["mb_on"] = 0; }
        if (has({ "wide", "huge", "big" })) p["macro_spectre"] = juce::jmin(1.0f, p["macro_spectre"] + 0.3f);
        if (has({ "talking", "yoi" }) && p["f1_type"] != (float) fFormant && folder != "SUB" && folder != "808") {   // give it a mouth
            p["f2_on"] = 1; p["f2_type"] = (float) fFormant; p["f2_cut"] = 40; p["f2_res"] = 0.75f;
            PB b { p }; if (!usesWob) applyWob(b, v1_8, wsYoi);
            for (int k = 1; k <= kModSlots; ++k) if (p["mm" + juce::String(k) + "_src"] == 0.0f) { b.mm(k, srcWob, mdVowel, 0.8f); break; }
        }
        if (has({ "slide", "glide" })) p["glide"] = juce::jmax(p["glide"], 0.12f);
        for (auto* id : { "f1_cut", "f2_cut" }) p[id] = juce::jlimit(30.0f, 18000.0f, p[id]);
        if (described) *described = folder.toLowerCase() + " (" + v[(size_t) pick].name + ")";
        return p;
    }
};

} // namespace gs
