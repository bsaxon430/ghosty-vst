#pragma once
// Ghosty Pro MODULATION (round 3 item 3): a router of up to 64 connections "source -> any knob", next to the old 8-slot matrix.
//
// Behaviour reference: the Serum 2 manual (drag a source onto a knob, an amount ring, right-click menus, a matrix list with
// curves / aux / bypass). Clean-room: no code from any GPL synth was read or used.
//
// How a connection sounds:
//   r   = the source's own value (unipolar sources 0..1: envelopes, velocity, aftertouch, mod wheel, macros; bipolar -1..1: LFOs,
//         note, pitch bend, random)
//   u   = r as 0..1 (bipolar sources: (r + 1) / 2)
//   c   = CURVE (u): linear / exp / log / S / a custom curve through 5 drawn points
//   x   = BIPOLAR ? 2c - 1 : c       (a new connection from a bipolar source starts BIPOLAR, so a linear LFO gives x = r exactly)
//   x  *= u of the AUX source        (when an aux source is set: e.g. macro x velocity)
//   the knob moves by  AMOUNT * x  of its whole range (in the knob's own normalised travel, like turning it by hand)
//
// Where it is applied (one path per connection, chosen by the target):
//   * VOICE targets (the knobs the voice already reads per 16-sample control step: cutoff, resonance, drive, positions, levels,
//     detune, X-MOD, sub, noise, growl, the four warp amounts): every voice adds its own sum into the same per-voice target array the
//     old matrix uses, so ENV / LFO / velocity / note / random modulate each voice on its own.
//   * GLOBAL targets (every other continuous knob, through the MacroEngine shadows the engine reads inside processBlock; parameters
//     are never written): once per block (and per 32-sample sub-block while a modulation moves) from global sources and the newest
//     voice's values.
//   * AMOUNTS move without a click: the audio thread glides each amount (10 ms one-pole per block) and the voices read it linearly
//     interpolated at each control step (ProTests zipper MODAMT).
//
// Nothing on the audio thread allocates or locks: the compiled set is handed over by pointer (pending -> live, the old one to
// 'trash', freed on the message thread), amounts are atomics.
#include <juce_audio_processors/juce_audio_processors.h>
#include "ProMacros.h"
#include <array>
#include <atomic>
#include <vector>
#include <map>

namespace gpro
{
namespace mod
{
enum Src { none = 0, env1, env2, env3, menv, lfo1, lfo2, lfo3, lfo4, mac1, mac2, mac3, mac4, vel, note, aftertouch, wheel, bend, random, numSrc };
enum Curve { cLinear = 0, cExp, cLog, cS, cCustom, numCurves };
constexpr int MAX_CONN = 64;
constexpr int NUM_LFO = 4;         // LFO 1 / 2 = the old ones (ids l1_ / l2_), LFO 3 / 4 new (l3_ / l4_)

const juce::StringArray& srcNames();          // "ENV 1 (AMP)" ...
const juce::StringArray& srcShortNames();     // "ENV 1" ... (tiles, rings)
const juce::StringArray& curveNames();
inline bool srcIsBipolar (int s) noexcept { return (s >= lfo1 && s <= lfo4) || s == note || s == bend || s == random; }
inline bool srcIsVoice (int s) noexcept { return (s >= env1 && s <= lfo4) || s == vel || s == note || s == aftertouch || s == random || s == bend; }

// the curve on 0..1 (custom: 5 points (x, y), x rising, x0 = 0, x4 = 1)
inline float applyCurve (int curve, float u, const float* px, const float* py) noexcept
{
    u = juce::jlimit (0.0f, 1.0f, u);
    switch (curve)
    {
        case cExp:  return (std::exp2 (4.0f * u) - 1.0f) * (1.0f / 15.0f);
        case cLog:  return std::log2 (1.0f + 15.0f * u) * 0.25f;
        case cS:    return u * u * (3.0f - 2.0f * u);
        case cCustom:
            for (int k = 0; k < 4; ++k)
                if (u <= px[k + 1] || k == 3)
                {
                    const float w = px[k + 1] - px[k];
                    const float t = w > 1.0e-6f ? juce::jlimit (0.0f, 1.0f, (u - px[k]) / w) : 1.0f;
                    return juce::jlimit (0.0f, 1.0f, py[k] + (py[k + 1] - py[k]) * t);
                }
            return u;
        default:    return u;
    }
}
inline float unipolarOf (int src, float r) noexcept { return srcIsBipolar (src) ? 0.5f * (r + 1.0f) : r; }
// x of one connection for source value r (and aux value ra when aux > 0)
inline float shapeOf (int src, float r, int curve, bool bipolar, const float* px, const float* py, int aux, float ra) noexcept
{
    float x;
    if (curve == cLinear && bipolar && srcIsBipolar (src)) x = juce::jlimit (-1.0f, 1.0f, r);      // (exactly r)
    else
    {
        const float c = applyCurve (curve, unipolarOf (src, r), px, py);
        x = bipolar ? 2.0f * c - 1.0f : c;
    }
    if (aux > none) x *= juce::jlimit (0.0f, 1.0f, unipolarOf (aux, ra));
    return x;
}
} // namespace mod

// one connection (message thread copy; saved in the MODS child of the state)
struct ModConn
{
    int id = 0;                       // stable while the list lives (UI selections, morph / amount carry-over)
    int src = mod::none;
    juce::String dst;                 // parameter id
    float amount = 0.0f;              // -1 .. +1 of the knob's travel
    bool bipolar = false;
    int curve = mod::cLinear;
    std::array<float, 5> px { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }, py { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
    int aux = mod::none;
    bool bypass = false;
    int slot = -1;                    // (not saved) its amount / live-value slot: stable while the connection lives
};

// per voice (lives in ProVoice): the new LFOs' phases and the latest source values (the newest voice feeds the global path / UI)
struct ModVoice
{
    double ph[2] {};                  // LFO 3 / 4 phase (cycles)
    double t = 0;                     // seconds since the note started (LFO fade in)
    juce::uint32 seed = 0;
    std::array<float, mod::numSrc> last {};
};

struct ModVoiceIn { float env1, env2, env3, menv, lfo1, lfo2, vel, note, at, wheel, bend, rnd; };

class ModRouter
{
public:
    using LfoFn = float (*) (int shape, double phase, juce::uint32 seed);
    ModRouter();
    ~ModRouter();

    // ---- setup (constructor time, message thread)
    struct VoiceTarget { const char* param; int dst; };     // a parameter the voice reads with the old matrix's per-voice target 'dst'
    void attach (MacroEngine& m, juce::AudioProcessorValueTreeState& s, std::vector<VoiceTarget> voiceTargets, LfoFn fn);
    // the LFO 3 / 4 and depth parameters (shadow pointers)
    struct LfoP { std::atomic<float>* rate = nullptr, *sync = nullptr, *div = nullptr, *shape = nullptr, *trig = nullptr, *fade = nullptr, *depth = nullptr; };
    std::array<LfoP, mod::NUM_LFO> lfoP {};
    static void addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& l);    // l1_depth, l2_depth, l3_* / l4_* (after everything else)
    static bool isModParam (const juce::String& id);                                       // (left out of a saved sound while at its default)

    // ---- the connection list (message thread)
    std::vector<ModConn> list() const;
    int size() const;
    int add (int src, const juce::String& dst, float amount);        // returns the index (an existing src -> dst connection is reused); -1 = full / not a target
    void setAmount (int index, float amount);                         // live: no recompile (an amount drag)
    void setBypass (int index, bool b);
    void setBipolar (int index, bool b);
    void setCurve (int index, int curve);
    void setCustom (int index, const std::array<float, 5>& px, const std::array<float, 5>& py);
    void setAux (int index, int aux);
    void setSource (int index, int src);
    bool move (int index, const juce::String& newDst);                // 'move modulation' to another knob
    void remove (int index);
    void removeAllFor (const juce::String& dst);
    void clear();
    std::vector<int> indicesFor (const juce::String& dst) const;
    bool isTarget (const juce::String& paramId) const;                // can this knob be modulated
    bool isVoiceTarget (const juce::String& paramId) const;
    juce::uint32 version() const noexcept { return ver.load(); }      // bumps on every change (UI refresh)
    juce::ValueTree toTree() const;                                   // "MODS" (empty list = an invalid tree: nothing is written)
    void fromTree (const juce::ValueTree& t, bool snapNow = true);    // absent / invalid = no connections
    void setList (const std::vector<ModConn>& l, bool snapNow);
    float liveValue (int index) const noexcept;                       // what connection 'index' adds right now (amount * x, newest voice / global)

    // ---- MORPH BOARD: per-ghost amounts of the current list (message thread builds, the audio thread blends with the live weights)
    void setMorphTable (int ghosts, std::vector<float> amounts);       // ghosts x list size (0 = the ghost has no such connection); ghosts 0 = off
    void setMorphWeights (const float* w, int n) noexcept { mw = w; mwN = n; }   // audio thread, per block (nullptr = board off)

    // ---- MIDI LEARN (session setting)
    void learn (const juce::String& paramId);                         // the next CC moves this knob
    void forget (const juce::String& paramId);
    int ccFor (const juce::String& paramId) const;                    // -1 = none
    juce::String learning() const;
    juce::ValueTree learnTree() const; void loadLearnTree (const juce::ValueTree& t);
    void scanMidi (const juce::MidiBuffer& m) noexcept;               // audio thread: CCs, pitch bend, channel pressure
    void applyLearned (juce::AudioProcessorValueTreeState& s);        // message thread (timer): CC values -> the learned knobs

    // ---- audio thread
    struct BlockIn
    {
        int n = 0; double sr = 44100; double bpm = 120, ppq = 0; bool playing = false;
        const ModVoice* lead = nullptr;                    // the newest sounding voice (nullptr = none)
        float wheel = 0; float macro[4] {};
    };
    void beginBlock (const BlockIn& in) noexcept;                     // amounts, LFO 3 / 4 clocks, global offsets into the shadows
    bool wantsFineSteps() const noexcept { return liveC != nullptr && liveC->anyGlobal && moving; }
    bool voiceActive() const noexcept { return liveC != nullptr && liveC->anyVoice; }
    void voiceStart (ModVoice& v, float rnd) const noexcept;
    // a voice's control step (n samples from 'pos' in this block): sources -> v.last, the voice connections -> d[] (old matrix targets)
    void voiceStep (ModVoice& v, const ModVoiceIn& in, int pos, int n, float* d) const noexcept;
    float lfoDepth (int k) const noexcept { auto* p = lfoP[(size_t) k].depth; return p != nullptr ? p->load (std::memory_order_relaxed) : 1.0f; }
    std::atomic<float> srcLive[mod::numSrc] {};                      // UI (tiles)
    std::atomic<float> lfoPhaseLive[2] {};                           // UI: LFO 3 / 4 global phase 0..1
    double sampleRate = 44100;

    void collect();                                                   // message thread: frees what the audio thread let go of

    // test hooks
    int liveCount() const noexcept { return liveC != nullptr ? (int) liveC->c.size() : 0; }
    float globalOffset (int slot) const noexcept { return juce::isPositiveAndBelow (slot, (int) off.size()) ? off[(size_t) slot] : 0.0f; }

private:
    MacroEngine* macros = nullptr;
    juce::AudioProcessorValueTreeState* apvts = nullptr;
    LfoFn lfoFn = nullptr;
    std::vector<VoiceTarget> vt;
    mutable juce::SpinLock lock; std::vector<ModConn> conns; int nextId = 1;   // message thread copy
    std::array<bool, mod::MAX_CONN> slotUsed {};                     // (under 'lock') amount slots in use
    int allocSlot() noexcept { for (int i = 0; i < mod::MAX_CONN; ++i) if (! slotUsed[(size_t) i]) { slotUsed[(size_t) i] = true; return i; } return -1; }
    std::atomic<juce::uint32> ver { 1 };
    std::array<std::atomic<float>, mod::MAX_CONN> amtT {};           // targets, by list index
    std::array<std::atomic<float>, mod::MAX_CONN> live {};           // UI: amount * x per connection
    struct CConn { int id, src, aux, curve; bool bipolar, bypass; float px[5], py[5]; int slot, vdst; float vscale; int idx, li; };   // idx = amount slot, li = list index
    struct Compiled { std::vector<CConn> c; bool anyVoice = false, anyGlobal = false; };
    Compiled* liveC = nullptr; std::atomic<Compiled*> pending { nullptr }, trash { nullptr };
    struct MorphT { int ghosts = 0, n = 0; std::vector<float> a; };
    MorphT* liveM = nullptr; std::atomic<MorphT*> pendingM { nullptr }, trashM { nullptr };
    const float* mw = nullptr; int mwN = 0;
    // audio thread
    std::array<float, mod::MAX_CONN> amtA {}, amtB {}; std::array<int, mod::MAX_CONN> amtId {};
    bool snap = true, moving = false; int blockLen = 1; std::atomic<bool> snapReq { false };
    double gph[2] {}; double inc[2] {}; int shape[2] {}; bool trig[2] {}; float fade[2] {};
    std::vector<float> off; std::vector<uint8_t> touched;
    std::array<float, mod::numSrc> gsrc {}, gsrcWas {};
    float bendNow = 0, atNow = 0;
    // MIDI learn
    std::array<std::atomic<int>, 128> ccVal {};                       // -1 = nothing new
    std::atomic<int> lastCc { -1 };
    mutable juce::SpinLock learnLock; std::map<juce::String, int> learned; juce::String learnId;

    int slotOf (const juce::String& id) const;
    int voiceDstOf (const juce::String& id) const;
    void rebuild (bool snapNow);
    float srcValueGlobal (int s) const noexcept { return gsrc[(size_t) s]; }
};
} // namespace gpro
