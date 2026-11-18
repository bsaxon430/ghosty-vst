#pragma once
// Ghost Box 3 modulation layer (GPFX_BOX builds only): GHOST MORPH (A/B states, one knob), GHOST MOTION (5 drawable
// tempo-synced lanes), 8 MACROS, and the loudness matcher used by CHAIN BRAIN auditions.
//
// How it reaches the units: while the rack attaches, every P is bound to a shadow value (ShadowBinder in FxCore.h).
// Before each processing chunk the audio thread writes shadow = host value, or, for modulated parameters,
// morph(A, B) + macro offsets + motion offsets (in the parameter's normalised space). Ghosty Pro never compiles this.
// Real-time rule: the audio thread reads an immutable ModConfig through an atomic pointer; the message thread builds
// new configs and retires old ones only when the audio thread can no longer hold them.
#include "FxCore.h"
#include "FxSpaceDsp.h"
#include "BendLane.h"
#include "SpiritBoard.h"
#include "FxBoxScenes.h"
#include "FxBoxTimeline.h"
#include <map>

#if GPFX_BOX
namespace gpfx {

constexpr int kMacros = 8, kLanes = 5, kMaxSteps = 64;
enum MorphKind { mkLinear, mkLog, mkSwitch, mkTime, mkOn, mkMix, mkHoldA, mkHoldB };
enum MotionLane { lnVolume, lnFilter, lnPitch, lnSpace, lnDamage };
enum MacroCurve { mcLinear, mcEaseIn, mcEaseOut, mcSmooth };

inline const char* laneName (int k) { static const char* n[] { "VOLUME", "FILTER", "PITCH", "SPACE", "DAMAGE" }; return n[juce::jlimit (0, kLanes - 1, k)]; }
inline const char* laneKey (int k) { static const char* n[] { "volume", "filter", "pitch", "space", "damage" }; return n[juce::jlimit (0, kLanes - 1, k)]; }
inline const juce::StringArray& motionLenNames() { static juce::StringArray s { "1 BEAT", "2 BEATS", "1 BAR", "2 BARS", "4 BARS", "8 BARS", "16 BARS" }; return s; }
inline const juce::StringArray& motionFeelNames() { static juce::StringArray s { "STRAIGHT", "TRIPLET", "DOTTED" }; return s; }
inline const juce::StringArray& motionModeNames() { static juce::StringArray s { "CURVE", "STEPS" }; return s; }
inline const juce::StringArray& motionStepsNames() { static juce::StringArray s { "8", "16", "32", "64" }; return s; }
inline int motionStepCount (int i) noexcept { return 8 << juce::jlimit (0, 3, i); }
inline double motionBeats (int len, int feel) noexcept
{
    static const double b[] { 1, 2, 4, 8, 16, 32, 64 };
    const double base = b[juce::jlimit (0, 6, len)];
    return base * (feel == 1 ? 2.0 / 3.0 : feel == 2 ? 1.5 : 1.0);
}
inline const juce::StringArray& macroDefaultNames() { static juce::StringArray s { "DARKNESS", "CHAOS", "SIZE", "IMPACT", "MOVEMENT", "WIDTH", "DIRT", "AIR" }; return s; }
inline const juce::StringArray& boardOrbitNames() { static juce::StringArray s { "OFF", "CIRCLE", "FIGURE 8", "RANDOM WALK" }; return s; }
inline const juce::StringArray& boardOrbitRateNames() { static juce::StringArray s { "1 BEAT", "2 BEATS", "1 BAR", "2 BARS", "4 BARS", "8 BARS" }; return s; }
inline double boardOrbitBeats (int i) noexcept { static const double b[] { 1, 2, 4, 8, 16, 32 }; return b[juce::jlimit (0, 5, i)]; }
inline const juce::StringArray& morphCurveNames() { static juce::StringArray s { "LINEAR", "SMOOTH", "EASE IN", "EASE OUT" }; return s; }
inline const juce::StringArray& macroCurveNames() { static juce::StringArray s { "LINEAR", "EASE IN", "EASE OUT", "S-CURVE" }; return s; }
inline float shapeCurve (int c, float x) noexcept
{
    x = clampf (x, 0.0f, 1.0f);
    switch (c) { case 1: return x * x; case 2: return 1.0f - (1.0f - x) * (1.0f - x); case 3: return x * x * (3.0f - 2.0f * x); default: return x; }
}
inline float morphCurve (int c, float m) noexcept { static const int map[] { 0, 3, 1, 2 }; return shapeCurve (map[juce::jlimit (0, 3, c)], m); }

// one shadowed parameter
struct ModSlot
{
    juce::String id; std::atomic<float>* host = nullptr; std::atomic<float> shadow { 0 };
    juce::RangedAudioParameter* param = nullptr; juce::NormalisableRange<float> range; int unit = -1; bool discrete = false;
    // audio thread only
    float committed = 0; float pending = 0; bool hasPending = false; bool touched = false;
    float to01 (float v) const noexcept { return clampf (range.convertTo0to1 (clampf (v, range.start, range.end)), 0.0f, 1.0f); }
    float from01 (float x) const noexcept { return range.convertFrom0to1 (clampf (x, 0.0f, 1.0f)); }
};

struct MorphItem { int slot; float a, b, aEff, bEff; int kind; };
// SPIRIT BOARD: one rack parameter blended over N ghosts' scenes
enum BoardKind { bkLinear, bkLog, bkSwitch, bkOnMix, bkMix, bkHold, bkHoldSwitch };
struct BoardItem { int slot = -1; int kind = bkLinear; int unit = -1; std::array<float, ghosty::spirit::kMaxGhosts> v {}, eff {}; std::array<bool, ghosty::spirit::kMaxGhosts> on {}; };
struct MacroItem { int slot; int macro; float depth; int curve; };
// GHOST MOTION timeline: one lane as the audio thread reads it (immutable inside a published ModConfig)
struct TlItem { int slot = -1, unit = -1, lane = 0; uint32_t ident = 0; bool on = true; float depth = 0; int mode = 0, steps = 16; std::array<float, kTlMaxSteps> st {}; int nEv = 0; std::array<TlEvent, kTlEvents> ev {}; };
struct ModConfig
{
    int tlBars = 1; std::vector<TlItem> tl;   // GHOST MOTION timeline (empty: no timeline)
    bool morphOn = false; int morphCurve = 1;
    std::vector<MorphItem> morph;
    bool boardOn = false; int boardN = 0;
    std::vector<BoardItem> board;
    std::vector<MacroItem> macros;
};

// K-weighted short-term loudness (3 s window in 100 ms blocks), BS.1770 style.
struct Loudness
{
    BiquadCoefs shelf, hp; std::array<Biquad, 2> s1, s2; double acc = 0; int cnt = 0, blk = 4800;
    std::array<double, 30> ring {}; int w = 0, filled = 0; float lufs = -70.0f;
    void prepare (double sr)
    {
        shelf = rbj (rbjHighShelf, sr, 1681.97, 0.7071, 3.999); hp = rbj (rbjHP, sr, 38.13, 0.5003);
        blk = std::max (1, (int) (sr * 0.1)); reset();
    }
    void reset() { for (auto& b : s1) b.reset(); for (auto& b : s2) b.reset(); acc = 0; cnt = 0; ring.fill (0); w = 0; filled = 0; lufs = -70.0f; }
    void process (const float* L, const float* R, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const float l = s2[0].process (s1[0].process (L[i], shelf), hp), r = s2[1].process (s1[1].process (R[i], shelf), hp);
            acc += (double) l * l + (double) r * r;
            if (++cnt >= blk)
            {
                ring[(size_t) w] = acc / cnt; w = (w + 1) % 30; filled = std::min (30, filled + 1); acc = 0; cnt = 0;
                const int use = filled < 30 ? std::min (filled, 4) : 30;   // momentary (400 ms) until 3 s are in, then short-term
                double s = 0; for (int k = 0; k < use; ++k) s += ring[(size_t) ((w + 30 - 1 - k) % 30)];
                const double ms = s / std::max (1, use);
                lufs = ms > 1.0e-10 ? (float) (-0.691 + 10.0 * std::log10 (ms)) : -70.0f;
            }
        }
    }
};

// STEPS mode: one value per step (-1..1), a tie bit per step (hold the previous step's value: no retrigger)
struct StepLane { std::array<float, kMaxSteps> v {}; uint64_t ties = 0; int count = 16; float valueAt (double ph) const noexcept
    { const int n = juce::jlimit (1, kMaxSteps, count); int i = juce::jlimit (0, n - 1, (int) (ph * n)); int guard = 0; while ((ties >> i) & 1u && guard++ < n) i = (i + n - 1) % n; return v[(size_t) i]; } };
struct MotionLaneDef { bool on = false; float depth = 1.0f; int len = 2, feel = 0, mode = 0, stepsIdx = 1; float phase = 0; bendlane::Shape shape; StepLane steps; };
struct MotionDef { juce::String name; std::array<MotionLaneDef, kLanes> lanes; };
const std::vector<MotionDef>& factoryMotions();
juce::File motionDir();                                              // Documents/Ghosty/Ghost Box/Motion
bool saveMotionFile (const MotionDef& m, const juce::File& f);
bool loadMotionFile (const juce::File& f, MotionDef& out);
bendlane::Shape flatShape();
juce::ValueTree timelineToTree (const TimelineDef& d);                 // GHOST MOTION timeline <-> state (BOX3/TIMELINE)
TimelineDef timelineFromTree (const juce::ValueTree& t);

class BoxMod : public ShadowBinder
{
public:
    BoxMod();
    ~BoxMod() override;

    // ---- attach (message thread, while the rack's units bind their parameters)
    std::atomic<float>* shadowFor (const juce::String& id, std::atomic<float>* host) override;
    void finishAttach (juce::AudioProcessorValueTreeState& s);
    void prepare (double sr);

    // ---- audio thread
    int chunkSize() const noexcept { return busy ? 64 : kSub; }
    void beginBlock() noexcept { inProcess.store (1); cfgNow = cfg.load(); }
    void snapSmoothing() noexcept;   // macro / morph / lane smoothers start at their targets (called after beginChunk, audio thread)
    void endBlock() noexcept { cfgNow = nullptr; inProcess.store (0); blocks.fetch_add (1); }
    void beginChunk (const BlockCtx& ctx, int n, const std::array<P, kNumUnits>& onP) noexcept;
    void builtin (float* L, float* R, int n, const BlockCtx& ctx) noexcept;   // pre-limiter MOTION stage
    bool duckWanted (int u) const noexcept { return duckPending[(size_t) u]; }
    void commitDuck (int u, UnitDsp& unit) noexcept;
    void measureIn (const float* L, const float* R, int n) noexcept { if (matchOn.load (std::memory_order_relaxed)) inLu.process (L, R, n); }
    void matchOut (float* L, float* R, int n) noexcept;

    // ---- message thread
    struct Snap { std::map<juce::String, float> vals; juce::String order; juce::ValueTree rack; };   // rack: the rack's own state (SPIRIT BOARD ghosts, for LAYER)
    Snap snapNow (const std::array<int, kNumUnits>& order) const;          // real values of every fx_ parameter
    void setState (int which, const Snap& s);                               // 0 = A, 1 = B
    bool hasState (int which) const { return which == 0 ? hasA : hasB; }
    const Snap& state (int which) const { return which == 0 ? snapA : snapB; }
    void swapStates() { std::swap (snapA, snapB); std::swap (hasA, hasB); rebuild(); }
    void clearMorph() { hasA = hasB = false; snapA = {}; snapB = {}; morphOn = false; rebuild(); }
    void setMorphOn (bool b) { morphOn = b && hasA && hasB; if (morphOn) boardOn = false; rebuild(); }
    bool isMorphOn() const { return morphOn; }
    void setMorphCurve (int c) { morphCurveSel = juce::jlimit (0, 3, c); rebuild(); }
    int getMorphCurve() const { return morphCurveSel; }
    int morphCount() const { return lastMorphCount; }
    juce::String morphNote() const;                                         // order / missing notes for the page

    // ---- SPIRIT BOARD (message thread; the audio thread reads boardShared + the published config)
    ghosty::spirit::Board board;                      // ghosts, puck, weight + orbit settings (the puck mirrors box_board_x / y)
    ghosty::spirit::SharedBoard boardShared;
    std::map<juce::String, Snap> boardSnaps;          // ghost id -> its scene
    std::map<juce::String, float> boardPct;           // ghost id -> CUSTOM weight in % (LAYER)
    int boardContentVersion = 0;                      // bumped whenever a ghost's scene / the ghost set changes (LAYER rebuilds its racks)
    bool boardOn = false;
    void setBoardOn (bool b) { boardOn = b && ! board.ghosts.empty(); if (boardOn) morphOn = false; rebuild(); }
    bool isBoardOn() const { return boardOn; }
    void boardChanged() { boardShared.publish (board); rebuild(); }   // after any board edit
    static juce::String snapText (const Snap& s);       // a scene's settings as text (LAYER keys its racks by it)
    juce::ValueTree saveBoard() const;                 // board + its scenes (also the user BOARD file format)
    void loadBoard (const juce::ValueTree& t);
    std::array<float, ghosty::spirit::kMaxGhosts> boardWeightsNow() const;   // what the audio uses (UI)

    struct Assign { juce::String id; float depth = 0.5f; int curve = mcLinear; };
    std::array<std::vector<Assign>, kMacros> assigns; std::array<juce::String, kMacros> macroNames;
    bool assign (int macro, const juce::String& id, float depth, int curve = mcLinear);   // false: discrete parameters cannot be macro targets
    bool isDiscrete (const juce::String& id) const { const int s = slotOf (id); return s >= 0 && slots[(size_t) s]->discrete; }
    int liveAssignCount (int macro, const std::array<P, kNumUnits>& onP) const;      // assignments whose unit is on
    void unassign (int macro, const juce::String& id);
    void setDefaultMacros();
    void rebuild();                                                         // publish a new ModConfig

    juce::ValueTree save() const;
    void load (const juce::ValueTree& rackTree);                           // reads MORPH / MACROS / MOTION children

    // MOTION lanes (shapes and step values here, depth / length / on / mode / phase as host parameters)
    std::array<bendlane::SharedShape, kLanes> lanes;
    std::array<std::array<std::atomic<float>, kMaxSteps>, kLanes> stepVals {};   // message thread writes, audio thread copies on version change
    std::array<std::atomic<uint64_t>, kLanes> stepTies {};
    std::atomic<uint32_t> stepVer { 1 };
    StepLane getSteps (int lane) const;
    void setSteps (int lane, const StepLane& s);
    void setStep (int lane, int i, float v);
    void setTie (int lane, int i, bool tie);
    std::array<std::atomic<float>, kLanes> lanePos {}, laneVal {};
    std::array<std::atomic<int>, kLanes> laneTarget {};                     // 0 built-in, 1 rack units
    juce::String motionName;
    MotionDef currentMotion() const;
    void applyMotion (const MotionDef& m);                                 // shapes + lane parameters
    void resetMotion();

    // ---- GHOST MOTION timeline (FxBoxTimeline.h). Message thread edits 'timeline' (+ the curves in tlShapes) and calls tlChanged().
    bool tlView = false;                                                    // the MOTION page shows the TIMELINE (UI only, not saved)
    TimelineDef timeline;                                                   // lanes, events, steps, loop length, rule (the curves: tlShapes)
    std::array<bendlane::SharedShape, kTlLanes> tlShapes;                   // each lane's drawn CURVE (the editor writes, the audio thread fetches)
    int tlAddLane (const juce::String& id);                                 // a lane for this continuous fx_ setting (or the one it has); -1 = full / not allowed
    void tlRemoveLane (int lane);
    void tlChanged() { rebuild(); }                                         // after any timeline edit
    TimelineDef tlCurrent() const;                                          // the timeline with the curves as drawn
    void tlLoad (const TimelineDef& d);
    bool tlAllowed (const juce::String& id) const { const int sl = slotOf (id); return sl >= 0 && ! slots[(size_t) sl]->discrete && id.startsWith ("fx_"); }
    juce::String tlLaneName (int lane) const;                               // "ECHO  Mix"
    int tlPlaceOnHits (int lane, const TlAnalysis& a, int rule);            // AUDIO-AWARE: events on the hits the rule picks (replaces this lane's earlier auto-placed events)
    TlAnalysis tlDetectNow() const;                                         // onset analysis of the last complete loop of input (any thread but the audio thread)
    void tlDetectAsync (std::function<void (TlAnalysis)> done);            // the same on a worker thread; 'done' runs on the message thread
    TlAnalysis lastHits; std::atomic<int> hitsVersion { 0 };               // the newest analysis (message thread)
    OnsetFeed onsets;                                                       // the input feed (prepared in prepare(); written by the audio thread)
    std::atomic<bool> tlListen { false };                                   // the page is open: feed the detector even without lanes
    std::atomic<float> tlPos { -1.0f };                                     // 0..1 loop position (UI), -1 = no timeline
    std::array<std::atomic<float>, kTlLanes> tlVal {};                      // each lane's value now (UI)
    std::atomic<double> tlBpb { 4.0 }, tlSpb { 24000.0 }, tlOrigin { 0.0 };   // what the audio thread used last (beats per bar, samples per beat, loop origin)
    // ---- audio thread (called by the rack)
    bool timelineActive() const noexcept { return cfgNow != nullptr && ! cfgNow->tl.empty(); }
    void feedOnsets (const float* L, const float* R, int n, const BlockCtx& ctx) noexcept
    { if (tlListen.load (std::memory_order_relaxed) || timelineActive()) onsets.feed (L, R, n, ctx.ppq, ctx.samplesPerBeat); }
    int tlChunkLimit (double ppq, double spb, double bpb, int n) const noexcept;   // a chunk ends on the loop restart's sample
    std::array<int, kNumUnits> upLat {};                                    // samples of latency before each unit (the rack fills it while a timeline runs)

    // CHAIN BRAIN audition loudness match
    std::atomic<bool> matchOn { false };
    std::atomic<float> lufsIn { -70 }, lufsOut { -70 }, trimDb { 0 };

    // GHOST SCENES (FxBoxScenes.h): the scene layer overrides the base value of every shadow slot while a scene lands / morphs / spills
    SceneEngine scenes;
    std::vector<SceneSlotInfo> sceneSlotInfo() const;                       // (Rack::attach hands it to the engine)
    const juce::String& slotId (int i) const { return slots[(size_t) i]->id; }
    juce::RangedAudioParameter* slotParam (int i) const { return slots[(size_t) i]->param; }

    float effective (const juce::String& id) const;                         // the shadow value units see
    int slotOf (const juce::String& id) const { auto it = slotIndex.find (id); return it == slotIndex.end() ? -1 : it->second; }
    int numSlots() const { return (int) slots.size(); }
    juce::AudioProcessorValueTreeState* apvts = nullptr;
    std::array<int, kNumUnits> orderA {};
    bool orderDiffers = false;

private:
    std::vector<std::unique_ptr<ModSlot>> slots;
    std::map<juce::String, int> slotIndex;
    std::atomic<ModConfig*> cfg { nullptr };
    ModConfig* cfgNow = nullptr;
    std::vector<std::pair<std::unique_ptr<ModConfig>, int64_t>> retired;
    std::unique_ptr<ModConfig> live;
    std::atomic<int> inProcess { 0 }; std::atomic<int64_t> blocks { 0 };
    Snap snapA, snapB; bool hasA = false, hasB = false, morphOn = false; int morphCurveSel = 1; int lastMorphCount = 0;

    // host parameters of the layer
    P morphP; std::array<P, kMacros> macroP; P boardXP, boardYP, orbitP, orbitRateP, orbitDepthP;
    struct LaneP { P on, depth, len, feel, mode, stepsN, phase; }; P resetP;
    std::array<LaneP, kLanes> laneP;
    struct Target { int slot, unit; float weight; };
    std::array<std::vector<Target>, kLanes> targets;
    int slotFilterOn = -1, slotWarpOn = -1;

    // audio state
    double sr = 48000; bool busy = false;
    float morphNow = 0; bool morphInit = false, wasMorphing = false, wasPlaying = false; double startPpq = 0, ppqOrigin = 0;
    std::array<float, ghosty::spirit::kMaxGhosts> boardW {}, boardWT {}; int boardWinner = 0; bool boardInit = false;
    std::array<ExpSmooth, kMacros> macroSm;
    std::array<bendlane::Shape, kLanes> laneShape; std::array<uint32_t, kLanes> laneSeen {}; std::array<StepLane, kLanes> laneSteps; uint32_t stepSeen = 0;
    std::array<int, kLanes> laneMode {}; std::array<float, kLanes> lanePhase {};
    float laneValue (int k, double ppq) const noexcept;   // the lane's raw value (-1..1, before depth) at this song position
    std::array<float, kLanes> laneRaw {}, laneDepth {}; std::array<bool, kLanes> laneOn {}, laneUnits {};
    std::array<double, kLanes> laneBeats {};
    std::array<float, kLanes> laneSm {}; float smA = 0.02f, volG = 1.0f, volG1 = 1.0f, volA = 0.02f;
    std::array<bool, kNumUnits> duckPending {};
    std::vector<float> offsets;   // per slot normalised offset this chunk
    // GHOST MOTION timeline (audio)
    std::array<bendlane::Shape, kTlLanes> tlShape {}; std::array<uint32_t, kTlLanes> tlSeen {}; std::array<float, kTlLanes> tlSm {}; std::array<uint32_t, kTlLanes> tlIdent {};
    std::shared_ptr<bool> tlAlive = std::make_shared<bool> (true); std::unique_ptr<juce::ThreadPool> tlPool;
    // built-in MOTION processors
    std::array<Svf, 2> djf; float djMix = 0; int djCount = 0; float djLast = 0;
    std::array<PitchShifter, 2> pitch; float pitchMix = 0;
    Fdn8 verb; int verbTail = 0;
    Loudness inLu, outLu; float trimNow = 1.0f;

    void retire();
};

} // namespace gpfx
#endif
