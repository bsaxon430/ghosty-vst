#pragma once
// GHOST SCENES, the audio side (Ghost Box only, GPFX_BOX). The message thread (GhostBox/BoxScenes.h) publishes the 8 scenes as
// target values for every shadowed rack parameter (plus the box's DRY/WET and the chain order); this engine lands a triggered scene
// on the host grid and overrides the shadow slots' BASE value (MORPH / MACROS / MOTION still ride on top) until the message thread has
// written the scene into the host parameters (the commit) and the transition has finished. Then it lets go: the rack reads the host
// values again, which are the same numbers, so nothing moves.
//
//   CUT    lands on the grid (quantise OFF / 1/16 / 1/8 / BEAT / BAR / 2 BARS, from the host's beat / bar position; transport stopped =
//          the next 64-sample chunk). Sample-accurate: the rack's chunk is split at the grid sample, so continuous settings change on
//          exactly that sample. Switches / choices / delay times of units that keep running go through the rack's own 4 ms duck
//          (the wet signal ducks to dry from the grid sample, the value flips under it, the unit fades back in): click-free.
//          Units switching on / off use the rack's 15 ms on / off fade; continuous settings glide over 10 ms from the grid sample
//          (a de-click: a filter or drive step would otherwise be a transient), mixes also ride the rack's 25 ms mix smoothing.
//   MORPH  lands on the grid like CUT, then glides every compatible setting from what you hear to the scene over the MORPH TIME
//          (1/4 beat .. 4 bars, smoothstep curve; Hz / ms / s settings glide in log space). A unit that only one side uses crossfades
//          through its own mix (on for the whole morph, mix from 0 or to 0); switches, choices and delay times flip half way inside
//          the duck. The chain order changes at the landing (the rack's click-free reorder).
//   SPILL  lands like CUT, but every echo / reverb unit (ECHO, CRYSTAL, SPACE, SHIMMER, HALLS, SPECTRA, DOORWAY) that the new scene
//          turns OFF keeps ringing: its input fades out over 8 ms, its settings are held at the old scene's values and its wet tail is
//          added to the new chain until it falls below -90 dBFS for 250 ms (then a 30 ms fade) or 10 s have passed (then a 600 ms
//          fade): bounded CPU (only units that were already running keep running), no extra memory.
//
// Hold (MIDI key or a long click) = the scene while held, release = back to what was playing before (the values captured at the
// landing; the message thread restores the full state of before at the return's commit). The 'box_scene' host parameter follows the
// active scene (1..8, 0 = none) and a change of it from host automation triggers that scene; 'box_scene_morph' (default 100 %) is how
// far the box sits between the previous sound and the active scene.
//
// Real time: commands arrive through a lock-free FIFO; the scene data is immutable and swapped through an atomic pointer, retired by
// the message thread once the audio thread has left it; every array the audio thread writes is allocated in attach().
#include "FxCore.h"
#include <memory>
#include <vector>

#if GPFX_BOX
namespace gpfx {

constexpr int kScenes = 8, kSceneReturn = 8;
enum SceneMode { smCut, smMorph, smSpill };
inline const juce::StringArray& sceneModeNames() { static juce::StringArray s { "CUT", "MORPH", "SPILL" }; return s; }
inline const juce::StringArray& sceneQuantNames() { static juce::StringArray s { "OFF", "1/16", "1/8", "BEAT", "BAR", "2 BARS" }; return s; }
inline double sceneQuantBeats (int q, double beatsPerBar) noexcept
{
    switch (q) { case 1: return 0.25; case 2: return 0.5; case 3: return 1.0; case 4: return beatsPerBar; case 5: return 2.0 * beatsPerBar; default: return 0.0; }
}
inline const juce::StringArray& sceneMorphNames() { static juce::StringArray s { "1/4 BEAT", "1/2 BEAT", "1 BEAT", "2 BEATS", "1 BAR", "2 BARS", "4 BARS" }; return s; }
inline double sceneMorphBeats (int i, double beatsPerBar) noexcept
{
    switch (juce::jlimit (0, 6, i)) { case 0: return 0.25; case 1: return 0.5; case 2: return 1.0; case 3: return 2.0; case 4: return beatsPerBar; case 5: return 2.0 * beatsPerBar; default: return 4.0 * beatsPerBar; }
}
// the units whose tail SPILL keeps ringing
inline bool sceneSpillUnit (int u) noexcept { return u == uEcho || u == uCrystal || u == uSpace || u == uShimmer || u == uHalls || u == uSpectra || u == uPortal; }

// what the message thread publishes (immutable once published)
struct SceneData
{
    std::array<std::vector<float>, kScenes> to;                 // per scene: the target of every shadow slot (real values); empty = empty slot
    std::array<float, kScenes> mix {};                          // box_mix
    std::array<std::array<int, kNumUnits>, kScenes> order {};   // the chain order
    std::array<bool, kScenes> hasOrder {};
};

// one shadow slot as the scene engine sees it (copied at attach)
struct SceneSlotInfo { std::atomic<float>* host = nullptr; int unit = -1; bool discrete = false, time = false, logish = false, isOn = false, isMix = false; };

class SceneEngine
{
public:
    SceneEngine();
    ~SceneEngine();

    // ---- message thread
    void attach (std::vector<SceneSlotInfo> slots, std::array<std::atomic<int>, kNumUnits>* pendingOrder, std::atomic<int>* orderVersion, std::atomic<int>* dataVersion);
    void prepare (double sr);
    void publish (std::unique_ptr<SceneData> d);
    bool post (int type, int scene);   // 0 latch, 1 hold (press), 2 release, 3 the press became a click (keep it latched); false = FIFO full
    // a host restore / another load replaced the sound: forget the landing (the next block), re-read box_scene; newActive = the scene the
    // loaded state says is active (a host restore: box_scene - 1), else -1
    void requestClear (int newActive = -1) { clearActive.store (newActive); clearReq.store (true); active.store (newActive); }
    int slotCount() const { return (int) info.size(); }
    bool hasScene (int k) const;                       // (message thread view of the published data)

    // settings (message thread writes, audio thread reads)
    std::atomic<int> mode { smCut }, quant { 3 }, morphLen { 4 };
    std::atomic<bool> midiOn { true }, midiLatch { false }; std::atomic<int> midiBase { 36 };
    std::atomic<float>* sceneParam = nullptr;   // box_scene (raw: 0 none, 1..8)
    std::atomic<float>* morphParam = nullptr;   // box_scene_morph (raw 0..1)
    std::atomic<float>* mixParam = nullptr;     // box_mix (raw)

    // status (any thread reads)
    std::atomic<int> active { -1 }, pending { -1 };
    std::atomic<int> landSeq { 0 }, landScene { -1 }; std::atomic<bool> landReturn { false }, landHold { false };
    std::atomic<int> commitSeq { 0 };            // the message thread wrote landing landSeq into the host parameters
    std::atomic<double> lastLandPpq { -1.0 };    // where the last landing happened (tests: on the grid)
    std::atomic<int> lastLandScene { -1 };
    std::atomic<float> progress { 1.0f };        // MORPH progress (the scene bar draws it)
    std::atomic<int> spillCount { 0 };           // units ringing out now

    // ---- audio thread: BoxProcessor, once per slice before the rack
    void beginBlock (double ppq, double samplesPerBeat, bool playing, double beatsPerBar, double barStartPpq, bool haveBar) noexcept;
    void endBlock() noexcept;   // after the rack ran (the scene data may be retired from here on)
    void midiNote (int noteNumber, bool on, double atPpq) noexcept;
    float mixValue (float host) noexcept;   // box_mix as the scene layer has it now

    // ---- audio thread: inside Rack::process
    int chunkLimit (double ppq, double samplesPerBeat, int n) const noexcept;   // split the chunk at a pending landing
    void beginChunk (double ppq, double samplesPerBeat, int n) noexcept;        // lands, advances the morph, computes every base value
    bool on() const noexcept { return isOn; }
    float value (int slot) const noexcept { return cur[(size_t) slot]; }        // the base value this chunk (valid while on())
    bool flipsInDuck (int slot) const noexcept { return kind[(size_t) slot] == kSwitch; }
    std::array<bool, kNumUnits> spill {};                                        // the unit rings out (the rack gates its input)
    void spillDone (int u) noexcept;

private:
    enum Kind : uint8_t { kSame, kLin, kLog, kSwitch, kOn, kMix, kHoldFrom, kHoldTo, kSpill };
    std::vector<SceneSlotInfo> info;
    std::vector<float> cur, from, to, retTo; std::vector<uint8_t> kind;
    std::array<float, kNumUnits> mixFromEff {}, mixToEff {};
    std::array<int, kNumUnits> retOrder {};
    std::array<std::atomic<int>, kNumUnits>* pendingOrder = nullptr; std::atomic<int>* orderVersion = nullptr; std::atomic<int>* dataVersion = nullptr;
    std::atomic<SceneData*> data { nullptr };
    std::unique_ptr<SceneData> live; std::vector<std::pair<std::unique_ptr<SceneData>, int64_t>> retired;
    std::atomic<int> inProcess { 0 }; std::atomic<int64_t> blocks { 0 };
    void retire();
    // command FIFO (message -> audio)
    juce::AbstractFifo fifo { 64 }; std::array<std::pair<int, int>, 64> cmds {};
    std::atomic<bool> clearReq { false }; std::atomic<int> clearActive { -1 }; bool syncParam = true; int lastParam = 0;
    // transport (this block)
    double blkPpq = 0, blkSpb = 24000, bpb = 4.0, barStart = 0; bool blkPlaying = false, haveBar = false;
    // the pending landing
    bool hasPend = false, pendHold = false; int pendScene = -1; double pendPpq = 0, pendQ = 0, lastPpq = -1.0e9;
    // holds: the scenes held now (most recent last), the return point
    std::array<int, kScenes> held {}; int nHeld = 0; bool holdLanded = false; int retActive = -1; float retMix = 1.0f;
    // the landed transition
    bool isOn = false, landed = false; int mySeq = 0; float autoPos = 1.0f, pEff = 1.0f, mixFrom = 1.0f, mixTo = 1.0f, mixCur = 1.0f; double morphSamples = 1.0;
    bool committedNow = false, glideOnly = true; int minOnLeft = 0;
    double sr = 48000;

    void trigger (int scene, double atPpq, bool hold) noexcept;
    void release (int scene, double atPpq) noexcept;
    double gridFor (double atPpq, double& q) const noexcept;
    void land (double ppq, double samplesPerBeat) noexcept;
    void compute() noexcept;
};

} // namespace gpfx
#endif
