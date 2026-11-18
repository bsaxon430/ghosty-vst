#pragma once
// Ghost FX rack: Rack::Impl (shared between the audio side in ProFx.cpp and the editor in FxPanel.cpp).
#include "ProFx.h"
#include "FxRhythm.h"
#include "FxColour.h"
#include "FxSpace.h"
#include "FxMaster.h"
#include "FxEq.h"
#include "FxBox.h"
#include "FxSplit.h"
#include "FxBoxMod.h"
#include "FxBoxUnits2.h"
#include "FxRoute.h"
#include "FxGhostSpace.h"
#include "SnapshotBank.h"
#if ! GPFX_BOX
 #include "FxDist.h"
 #include "FxComp.h"
 #include "FxSplitPro.h"
 #include "FxEq3.h"
#endif

namespace gpfx {

struct Rack::Impl
{
    Impl();

    juce::AudioProcessorValueTreeState* apvts = nullptr;
#if GPFX_BOX
    // Declared BEFORE the units: every unit parameter reads a shadow value owned by BoxMod (and SPIRIT EQ's designer
    // thread keeps reading its parameters until its unit is destroyed), so these must outlive the units.
    bendlane::SharedShape warpShape;   // WARP's drawn curve (saved in saveState, so scenes / undo / presets carry it)
    BoxMod box;                         // Ghost Box 3: MORPH / MOTION / MACROS (shadow parameters)
    bendlane::SharedShape formantShape, pumpShape;   // GHOST FORMANT's drawn vowel path, GHOST GATE's drawn pump
    std::atomic<bool> offline { false };              // host bounce (GHOST SPECTRA's HQ overlap)
    SideChainFeed sc;                                 // GHOST GATE pump: external sidechain for this block
    SpectraMask spectraMask;                          // GHOST SPECTRA's painted per-band freeze
#endif
#if ! GPFX_BOX
    DistCurveStore distCurves;   // FX RACK (round 3): X-SHAPER's two drawn curves (before the units: they point at it)
    int lastNote = -1;           // the newest note-on so far (DISTORTION's KEY TRACK)
    CompUnit* comp = nullptr;    // COMPRESSOR (the rack tells it its place in the chain: KEY = RACK INPUT)
    // SPLITTER: the band slots' unit instances, made / prepared / owned here on the message thread, handed to the audio thread
    BandsUnit* bands = nullptr;
    Telemetry slotTm;                                         // (the slot units' meters go nowhere)
    juce::CriticalSection slotLock;
    std::vector<std::unique_ptr<UnitDsp>> slotPool; std::vector<int64_t> slotSeen;   // instances + the epoch each was first seen unused (-1 = in use)
    std::array<std::array<int, kSlots>, kBands> slotType {};  // -1 empty, else the unit (message thread)
    std::unique_ptr<UnitDsp> makeSlotUnit (int u);
    void setBandSlot (int band, int slot, int u);             // message thread (or a state load): -1 clears
    int bandSlot (int band, int slot) const { return slotType[(size_t) juce::jlimit (0, kBands - 1, band)][(size_t) juce::jlimit (0, kSlots - 1, slot)]; }
    void slotGc();
#endif
    std::array<std::unique_ptr<UnitDsp>, kNumUnits> units;
    GateUnit* gate = nullptr; GridUnit* grid = nullptr; HauntUnit* haunt = nullptr; UnitDsp* eqUnit = nullptr;

    Telemetry tm;
    GateStore gateStore; GridStore gridStore; PadStore padStore;
#if GPFX_BOX
    SplitUnit* split = nullptr;
    // PARALLEL LANES: units with lane 1 run in lane B (from the rack input), both lanes are summed phase-aligned before the limiter
    std::array<std::atomic<int>, kNumUnits> laneOf {}; std::atomic<int> laneVersion { 0 };
    std::array<int, kNumUnits> laneNow {}; int laneSeen = -1; bool parNow = false;
    P parOnP, parBlendP; ExpSmooth parBlendSm; LatentDry laneDelA, laneDelB;
    std::array<float, kSub> laneBL {}, laneBR {};
    void setLane (int u, int lane) { if (u >= 0 && u < kNumUnits && u != uLim) { laneOf[(size_t) u].store (lane ? 1 : 0); laneVersion.fetch_add (1); dataVersion.fetch_add (1); } }
    // GHOST SPLIT (FxRoute.h): units assigned to branches 1..3 (0 = the main chain), the split stage, what the audio thread latched
    RouteStage route;
    std::array<std::atomic<int>, kNumUnits> routeOf {}; std::atomic<int> routeVersion { 0 };
    std::array<int, kNumUnits> routeNow {}; int routeSeen = -1, routeModeNow = 0, routeBandsNow = 0; bool routeOnNow = false, routeActiveNow = false;
    void setRoute (int u, int branch) { if (u >= 0 && u < kNumUnits && u != uLim) { routeOf[(size_t) u].store (juce::jlimit (0, kRouteBranches, branch)); routeVersion.fetch_add (1); dataVersion.fetch_add (1); } }
    int routeBranchOf (int u) const { return u >= 0 && u < kNumUnits ? routeOf[(size_t) u].load() : 0; }
    // GHOST SPACE (FxGhostSpace.h): the field stage, and the GHOSTPRINT targets (VOLUME / WIDTH / TONE); both just before the limiter
    SpaceStage space; PrintStage print;
    void setSpacePath (const ghosty::spirit::OrbitPath& p) { space.drawnPath = p; space.tables.publish (p); dataVersion.fetch_add (1); }   // message thread
    bool routeRouting() const;                        // GHOST SPLIT is on and has units (message thread view)
    void latchRoute() noexcept;                       // audio thread, at a click-free switch (or a restore)
    template <typename F> void runRoute (int k, const BlockCtx& ctx, F& runUnit, float* L0, float* R0, int n);
    bool parallelOn() const { return parOnP.b(); }
    // MORPH actions (message thread; each one can be undone)
    void morphSet (int which);          // capture the rack as A (0) or B (1)
    // SPIRIT BOARD (message thread; every edit is one undo step)
    ghosty::SnapshotBank* snapshots = nullptr;                          // the host's SNAPSHOT bank (Ghost Box): BROWSE lists it, ghosts can hold one
    std::function<void()> onOtherLoad;                                  // a scene / preset / board load replaced the sound (the host: bank.clearCurrent())
    void otherLoad() { if (onOtherLoad) onOtherLoad(); }
    // the host's own pages (Ghost Box: CAPTURE, DNA): tabs placed after BOARD in the free space of the tab row (the six tabs keep their
    // places; a page whose tab does not fit is still opened through its host's own controls / setMode)
    struct ExtraPage { juce::String name, title, tip; juce::uint32 accent = 0xffA06BFF; std::function<std::unique_ptr<juce::Component>()> make;
                       bool shortcut = false; };   // shortcut: also a button in the strip's free tile cell (when the strip has one) so the page is one click away at any width
    std::vector<ExtraPage> extraPages;
    std::function<juce::String()> layerStatus;                          // SPIRIT BOARD LAYER: "3 racks running, CPU 6 %" (empty: no LAYER)
    std::function<bool()> layerRunning;                                 // LAYER is mixing racks now (the board shows its weights)
    int boardAdd (float x, float y);                                    // a new ghost holding the CURRENT state; -1 when full
    juce::ValueTree rackStateForLayer() const { auto t = owner->saveState(); t.removeChild (t.getChildWithName ("BOX3"), nullptr); return t; }
    void boardSetFrom (int ghost, const std::function<void()>& load);   // load (a scene...), then the ghost holds what you hear
    void boardRemove (int ghost);
    void boardEnable (bool on);                                         // on: the board drives the rack (MORPH lets go), the order from ghost A
    void boardEdited();                                                 // after moving ghosts / settings: publish
    bool saveBoardFile (const juce::String& name);
    bool loadBoardFile (const juce::File& f);
    void morphSwap();
    void morphClear();
    void morphEnable (bool on);         // MORPH ON / OFF through the click-free duck
    // CHAIN BRAIN audition state lives here (not in the page), so leaving the tab keeps the audition and its loudness match
    int brainSource = 1, brainGoal = 4, brainAudition = -1; uint32_t brainSeed = 1234; bool brainMatch = true;
    // latency: FIXED LATENCY (box_fixlat, default on) reports a constant figure that covers any one latent unit, so switching
    // SPECTRA / WARP / the pump lookahead on and off never moves the host's PDC; the output is padded to the reported figure
    P fixLatP; LatentDry padDelay; int padNow = 0;
    // GHOST SCENES SPILL (ProFx.cpp spillChunk): per unit the input gate, the ring-out gain, quiet / age counters (audio thread)
    std::array<float, kNumUnits> spG = filled (1.0f), spOut = filled (1.0f); std::array<int, kNumUnits> spQuiet {}, spAge {};
    std::array<float, kSub> spL {}, spR {}, spGate {};
    static std::array<float, kNumUnits> filled (float v) { std::array<float, kNumUnits> a; a.fill (v); return a; }
    void spillChunk (int u, UnitDsp& unit, float* L, float* R, int n, const BlockCtx& ctx);
    void upstreamLatency (std::array<int, kNumUnits>& up) const noexcept;   // GHOST MOTION timeline: latency before each unit (audio thread)
    int actualLatency() const;          // what the chain really delays right now
    int fixedLatency() const;           // the constant figure
    int reportedLatency() const;        // what the host is told
#endif

    // ---- editor-side data (message thread; guarded for state save from other threads)
    juce::CriticalSection dataLock;
    std::array<GatePattern, kGatePatterns> gatePatterns;
    std::array<GridPattern, kGridPatterns> gridPatterns;
    void setGatePattern (int i, const GatePattern& p);
    GatePattern getGatePattern (int i) const;
    void setGridPattern (int i, const GridPattern& p);
    GridPattern getGridPattern (int i) const;
    void setPad (int i, const PadDef& d) { padStore.set (i, d); dataVersion.fetch_add (1); }
    PadDef getPad (int i) const { return padStore.get (i); }

    // ---- processing order (the limiter always stays last)
    std::array<std::atomic<int>, kNumUnits> pendingOrder;
    std::atomic<int> orderVersion { 0 };
    void setOrder (const std::array<int, kNumUnits>& o);
    std::array<int, kNumUnits> getOrder() const;
#if ! GPFX_BOX
    // FX RACK (round 3): reorder = a 12 ms crossfade of the chain to its own (latency-aligned) input and back, never through silence
    std::array<Ring, 2> xfRing; std::array<float, kSub> reorderIn {};
    int chainLatency() const;
    int reorderLen() const;
    void reorderBlend (float* L, float* R, int n);
    void resetOrder();   // the default chain (unit order), message thread
#endif

    // ---- audio state
    std::array<P, kNumUnits> onP, mixP;
    std::array<LinSmooth, kNumUnits> onSm; std::array<ExpSmooth, kNumUnits> mixSm; std::array<bool, kNumUnits> active {};
    std::array<int, kNumUnits> order {}; int seenVersion = 0; int orderFade = 0, fadePos = 0, switchWait = 0, flushLeft = 0;   // 0 none, 1 fading out (then switch), 3 flushing (silent), 2 fading in
    double sr = 48000, freePpq = 0; bool prepared = false, attached = false;
    std::array<float, kSub> dryL {}, dryR {}, monoR {};
    // CLEAN SOUND (Ghosty Pro only): the drive units (WRECKER, TUBE, SOUL CRUSH, FATTEN) never come out louder than they went in
    std::atomic<bool> cleanMatch { false };
    std::array<float, kNumUnits> cmIn {}, cmOut {}, cmG {}; float cmA = 0.0003f, cmGa = 0.002f;

    void processChunk (float* L, float* R, int n, const BlockCtx& ctx);
    // smoother snap: on a host state restore, a transport start, or a relocate / loop jump after an idle gap (> 200 ms), the on /
    // mix / duck smoothers (and the Box layer's) start at their targets in the next chunk. Running, continuing audio is never snapped.
    void snapSmoothers();
    void applyPendingOrderNow();   // host restore only: the loaded order / lanes take effect at once (no fade + flush on the first beat)
    std::atomic<bool> snapRequest { false }; bool snapPendingChunk = false, snapOrderPending = false, wasPlaying = false; double lastProcessMs = -1.0, expectedPpq = -1.0;

    // ---- click-free loads: the message thread asks units to duck to dry, waits briefly, changes parameters, releases
    std::array<std::atomic<int>, kNumUnits> duckReq {}; std::array<std::atomic<bool>, kNumUnits> duckLow {};
    std::array<float, kNumUnits> duckG {}; std::array<int, kNumUnits> duckHold {};
    std::atomic<int64_t> processCount { 0 }; std::atomic<float> blockMs { 10.0f };
    void duckAround (const std::array<bool, kNumUnits>& which, const std::function<void()>& change);

    // ---- presets, scenes, undo (FxPresets.cpp; message thread, or wherever the host loads a synth preset)
    Rack* owner = nullptr;
    std::atomic<int> dataVersion { 0 };                     // bumped by every pattern / pad / order change
    std::vector<juce::RangedAudioParameter*> allParams;      // every fx_ parameter
    std::array<std::vector<juce::RangedAudioParameter*>, kNumUnits> unitParams;
    struct Snapshot { std::vector<float> values; juce::ValueTree state; };   // normalised values (allParams order) + saveState()
    juce::CriticalSection presetLock;
    std::vector<Snapshot> undoStack, redoStack; static constexpr size_t kUndoMax = 48;   // every knob gesture is a step now
    // every knob / switch gesture on an fx_ parameter (UI drag, click) is one undo step; internal loads (setNorm) are not
    struct GestureUndo : juce::AudioProcessorParameter::Listener
    {
        Impl& d; explicit GestureUndo (Impl& i) : d (i) {}
        void parameterValueChanged (int, float) override {}
        void parameterGestureChanged (int, bool starting) override
        {
            if (! starting || d.internalEdit.load() != 0 || ! juce::MessageManager::existsAndIsCurrentThread()) return;
            const double now = juce::Time::getMillisecondCounterHiRes();
            if (now - lastMs > 30.0) d.pushUndo();   // several parameters set in one go (a page applying a snapshot) = one step
            lastMs = now;
        }
        double lastMs = -1.0e9;
    } gestureUndo { *this };
    std::atomic<int> internalEdit { 0 };
    std::vector<juce::RangedAudioParameter*> gestureHooked;
    ~Impl() { for (auto* p : gestureHooked) p->removeListener (&gestureUndo); }
    juce::String sceneName; bool sceneEdited = false;
    std::array<juce::String, kNumUnits> unitPreset; std::array<bool, kNumUnits> unitEdited {};
    std::array<std::vector<float>, kNumUnits> unitBase; std::vector<float> sceneBase; int sceneBaseVer = -1;
    std::atomic<bool> baselinePending { true };

    void collectParams (juce::AudioProcessorValueTreeState& s);
    Snapshot capture() const;
    void restore (const Snapshot& snap);
    void pushUndo();
    bool undo();
    bool redo();
    int undoDepth() const;
    int redoDepth() const;
    void rebaseline (int unitOrAll = -1);
    void updateEdited();                                    // compares the live values with the last load
    juce::String unitLabel (int u) const;                   // "" when no preset was chosen
    juce::String sceneLabel() const;
    void setNorm (juce::RangedAudioParameter* p, float v01);
    void resetUnit (int u, bool alsoOn);
    void applyUnitPreset (int u, int factoryIndex);         // -1 = Init (default)
    bool saveUserPreset (int u, const juce::String& name);
    bool loadUserPreset (int u, const juce::File& f);
    bool saveUserScene (const juce::String& name);
    bool loadUserScene (const juce::File& f);
    void afterFactoryScene (const juce::String& key);
};

} // namespace gpfx
