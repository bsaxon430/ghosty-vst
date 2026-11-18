/*
 * Ghost Bass - a bass instrument built on Surge XT.
 * Copyright 2026 the Ghost Bass authors. Based on Surge XT by the Surge Synth Team.
 * Released under the GNU General Public Licence v3 or later (see LICENSE).
 *
 * GhostEngine: the Ghost Bass layer that sits on SurgeSynthesizer.
 *
 *  SHAKE   bounded random offsets on the mapped params, undoable from the UI. Each preset's <ghostbass> section lists
 *          which parameter each gesture moves and by how much (normalised 0..1 units at full gesture); TERRAIN's land
 *          is what pulls the gestures. Offsets are non-destructive (the patch keeps its base values), applied on the
 *          audio thread once per 32-sample block through a ~35 ms one-pole smoother (no zipper noise), and a parameter
 *          edited elsewhere (DEEP mode, host automation) simply becomes the new base.
 *          (build 11: the SCULPT gesture pad, TERRITORY map and PIXEL TRAIL were removed.)
 *  TERRAIN the landscape (GhostTerrain.h) moves only Surge's own controls: the sculpt mapping, the
 *          macros (through Surge's macro modulation input), filter 1 and the waveshaper; depth = time
 *          across each note, EVOLVE drift. A flat land changes nothing.
 *  MORPH   two sounds blended (GhostMorph.h): a true parameter morph, or Surge's two scenes crossfaded;
 *          PENDULUM swings it with the tempo, DRAW plays a curve through Surge's MSEG engine.
 *  Build 8 (PURE SURGE): no sound code of ours anywhere - the sound is Surge XT's engine exactly; this
 *          layer only moves Surge's own parameters (smoothed), and at rest it writes nothing.
 *
 * Threads: setters are called from any thread (atomics); preBlock/postBlock only on the audio
 * thread; mapping sets are immutable once published (atomic pointer) and retired safely.
 */
#ifndef GHOSTBASS_SRC_GHOST_GHOSTENGINE_H
#define GHOSTBASS_SRC_GHOST_GHOSTENGINE_H

#include <array>
#include "GhostSequencer.h"
#include "GhostArp.h"
#include <atomic>
#include <functional>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "GhostTerrain.h"
#include "GhostMorph.h"

class SurgeSynthesizer;
class TiXmlElement;
class Parameter;

namespace ghost
{

enum Gesture
{
    G_PULL = 0,
    G_SQUASH,
    G_TWIST,
    G_STRETCH,
    G_PINCH,
    n_gestures
};
const char *gestureName(int g);
int gestureFromName(const std::string &s);

struct MapEntry
{
    int gesture{G_PULL};
    std::string param; // Surge storage name, e.g. "a_filter1_cutoff"
    float depth{0};    // normalised change at full gesture (may be negative)
};

// build 9: how TERRAIN treats one Surge control (the coverage table in ghost/docs/TERRAIN-COVERAGE.txt)
enum TerrainKind : uint8_t
{
    TK_SKIP = 0, // not moved (see `why`)
    TK_LEVEL,    // gain / level / send: only ever dips a little
    TK_CUTOFF,
    TK_RESO,
    TK_ATTACK,
    TK_RELEASE,
    TK_SUSTAIN,
    TK_TIME,     // decay / hold / delay / FX times
    TK_FEEDBACK, // feedback amounts: kept short of runaway
    TK_HPF,      // high-pass cut-offs: only ever opened down (the sub stays)
    TK_GENERIC,  // everything else that is a smooth continuous control
    n_terrain_kinds
};
const char *terrainKindName(int k);
struct TerrainCoverage
{
    int id{-1};
    std::string storage, label;
    uint8_t kind{TK_SKIP};
    std::string why; // for TK_SKIP: the reason
};

class GhostEngine
{
  public:
    explicit GhostEngine(SurgeSynthesizer *s);
    ~GhostEngine();

    // ---- patch data (non-audio threads) ----
    std::vector<MapEntry> getMapping() const;
    void setMapping(const std::vector<MapEntry> &m, bool rebaseFromCurrent = true);
    std::vector<MapEntry> autoMapping() const; // a sensible mapping from what the patch contains
    std::string category, tags;
    std::string subOsc{"a_osc1"}; // the layer DNA keeps intact

    // ---- live controls (any thread) ----
    // SHAKE: new random offsets (amount 0..1); returns previous so the UI can undo
    std::vector<std::pair<std::string, float>> shake(float amount, uint32_t seed);
    void setShake(const std::vector<std::pair<std::string, float>> &offsets);
    std::vector<std::pair<std::string, float>> getShake() const;
    void clearShake(); // no shake

    // readout: for each mapped parameter, its base and current displayed value
    struct Readout
    {
        std::string param, label, from, to;
        float delta01;
    };
    std::vector<Readout> readout() const;

    // ---- audio thread ----
    void preBlock();
    void postBlock(const float *L, const float *R, int n); // read-only: the scope picture
    void setSampleRate(double sr);

    // TERRAIN: the landscape that moves Surge's own controls (GhostTerrain.h); its landscape and
    // the page's A/B state are saved with the patch
    TerrainStage terrain;
    // build 9: every Surge control TERRAIN may move, and why each of the others is left alone (any thread)
    std::vector<TerrainCoverage> terrainCoverage() const;
    static TerrainCoverage classifyForTerrain(const ::Parameter *p);
    bool terrainMapped(int id) const; // is this parameter id moved by the land (the current patch's map)
    // build 9: the knobs follow (GUI idle): calls f(id, value01) for every control the engine moved since the last call
    template <typename F> void takeMoved(F &&f)
    {
        if (!movedFlag)
            return;
        for (int i = 0; i < nMoved; ++i)
            if (movedFlag[i].load(std::memory_order_relaxed) && movedFlag[i].exchange(0, std::memory_order_acq_rel))
                f(i);
    }
    // how far the land / MOVEMENT / sculpt has pushed each control now (normalised offset, 0 at rest); for the page's readout
    float movedAmount(int id) const { return id >= 0 && id < nMoved ? movedAmt[id].load(std::memory_order_relaxed) : 0.f; }
    int movedCount() const { return nMoved; }
    // the TERRAIN audition: LISTEN and the drag's note. The audio thread plays it (C2, again every two beats so you hear the
    // whole land front to back, the way a played note goes) and ends it the block after `want` goes false (SurgeSynthProcessor)
    struct Audition
    {
        std::atomic<bool> want{false};
        bool held{false};
        double sinceOn{0};
        int key{36};
    } audition;
    // ---- build 10: the SUB layer's own continuous controls on the same TERRAIN land / MOVEMENT / SHAKE.
    // The layer host registers them once (before audio starts); a control only moves while its layer is on, and returns to where the
    // user left it when the land is flat (nothing is written at rest). Values are normalised 0..1 (the engine's own range).
    struct ExtTarget
    {
        std::string name, label;
        float swing{0.25f}, wander{0.2f}, lo{-0.4f}, hi{0.4f};
        bool dip{false};
        std::function<bool()> enabled;
        std::function<float()> get;
        std::function<void(float)> set;
        uint32_t hid{0};
        float fx1{0}, fx2{0}, fx3{0};
        float base{0}, applied{0}, off{0}, glv{0}, lastWritten{-1.f}, movedNow{0};
        bool init{false};
    };
    std::vector<ExtTarget> ext;
    std::vector<std::pair<std::string, std::string>> extSkipped; // control, why it is not moved
    void addExt(ExtTarget t);
    // ---- build 10 LAYERS (SUB = Bombaz): the notes Surge plays, handed to the layer host. Audio thread only.
    struct LayerEv
    {
        uint8_t type; // 0 note on, 1 note off, 2 pitch bend (c = -8192..8191), 3 controller (a = cc, b = value), 4 all notes off
        uint8_t a, b;
        int16_t c;
    };
    static constexpr int kLayerEvMax = 512;
    std::array<LayerEv, kLayerEvMax> layerEv{};
    int layerEvN{0};
    std::atomic<bool> layerWanted{false}; // set by the layer host while SUB is on; off = nothing is recorded
    // build 15: set by the layer host while the BLEND BOARD is the mixer. Its output replaces the main engine's, so the main engine
    // starts no voices of its own then (they were rendered and thrown away: a whole engine's CPU for nothing)
    std::atomic<bool> boardOwnsNotes{false};
    void layerPush(uint8_t type, uint8_t a, uint8_t b, int16_t c = 0)
    {
        if (layerWanted.load(std::memory_order_relaxed) && layerEvN < kLayerEvMax)
            layerEv[layerEvN++] = {type, a, b, c};
    }
    // the layers' settings travel in <ghostbass><layers/>: the host fills these (text, any thread), nothing is written while default
    std::function<std::string()> layersSave;
    std::function<void(const std::string &)> layersLoad;
    // set only on the BLEND BOARD's own engines and in the purity test: skips every hook of ours, so a render is what the original engine alone does
    SurgeSynthesizer *mainSynth() const { return synth; }
    std::atomic<bool> bypass{false};
    void noteOn()
    {
        if (!bypass.load(std::memory_order_relaxed))
            terrain.noteOn();
    }
    // (the synth tells us: the note's time axis restarts)
    // MORPH: two sounds blended by Surge's own engine (GhostMorph.h); saved with the patch
    Morph morph;
    // the bass sequencer (its pattern is saved with the patch / DAW session)
    Sequencer seq;
    // the arpeggiator (its settings are saved with the patch, like the pattern)
    Arpeggiator arp;
    // MIDI OUT: the sequencer's and the arp's notes are also sent to the host (VST3 / AU)
    std::atomic<bool> midiOut{true};
    // the last output samples (mono), for the TERRAIN page's analyzer; written by the
    // audio thread, read by the UI without locking (a torn read only affects a picture)
    static constexpr int kScope = 1024;
    std::array<float, kScope> scope{};
    std::atomic<int> scopePos{0};
    std::atomic<bool> seqLocked{false}; // LOCK: loading a sound keeps the current pattern
    std::atomic<bool> sculptEnabled{true};
    // TEMPO switch: 0 = half, 1 = normal, 2 = double. It only changes the tempo that Surge's own tempo input (and so the sequencer, the arp, TERRAIN
    // and every synced LFO / delay) is given; at 1 the tempo is multiplied by exactly 1.0, so nothing changes at all.
    std::atomic<int> tempoMul{1};
    double tempoFactor() const
    {
        const int m = tempoMul.load(std::memory_order_relaxed);
        return m == 0 ? 0.5 : (m == 2 ? 2.0 : 1.0);
    }
    // patch hooks (installed on storage.ghostPatchHooks by the constructor)
    void onPatchSave(TiXmlElement &patch);
    void onPatchLoad(TiXmlElement *patch);

  private:
    struct MapSet
    {
        struct P
        {
            int id;
            std::string name;
            std::array<float, n_gestures> depth{};
            std::array<float, n_terrain_targets> tdepth{}; // TERRAIN targets this parameter is (1 = moved by it)
            float shake{0};
            // build 9: the field overlay (every smooth Surge sound control sits at two places on the land)
            uint8_t tkind{0};
            int ctype{0}; // the control type it was classified under (the audio thread classifies it again when the type changes)
            uint8_t ts{0};
            bool ovOnly{false}; // only the overlay moves it (not part of the sculpt mapping)
            float fx1{0}, fx2{0}, fx3{0};
            uint32_t hid{0};
            // audio-thread runtime
            float base{0}, off{0}, applied{0}, lastWritten{-1};
            float glv{0}; // build 9: the glitch's own (fast) offset, on top of `off`
        };
        std::vector<P> params;
        std::vector<MapEntry> entries;
    };
    std::unique_ptr<MapSet> build(const std::vector<MapEntry> &m,
                                  const std::vector<std::pair<std::string, float>> &shake,
                                  bool rebase) const;
    void publish(std::unique_ptr<MapSet> s);
    void reapRetired();

    SurgeSynthesizer *synth;
    std::atomic<MapSet *> active{nullptr};
    std::vector<std::pair<std::unique_ptr<MapSet>, uint64_t>> retired; // + block count at retire
    mutable std::mutex setMtx; // non-audio writers
    std::atomic<uint64_t> blockCount{0};
    std::atomic<bool> pendingRebase{false};

    void driveExt(const float *amt, float sculptMag, float glAmt, bool haveField, float mv);
    float smoothCoef{0.02f};
    // build 9: SCULPT's glitch (stepped random hops of Surge's own controls, tempo-synced; the more the sculpt is pulled the more it
    // glitches) and SHAKE's big burst of it. All zero at rest.
    std::atomic<float> burstReq{0.f};
    float burstEnv{0.f};
    double glPos{0.0};
    float glCoef{0.2f};
    float macroMod[8]{}; // the TERRAIN land's macro modulation last sent to Surge
    std::unique_ptr<std::atomic<uint8_t>[]> movedFlag; // set by the audio thread when it writes a control, taken by the GUI
    std::unique_ptr<std::atomic<float>[]> movedAmt;
    int nMoved{0};
    std::vector<float> appliedById; // the sculpt's applied offset per parameter id (MORPH keeps it)
    float slewAmt[n_gestures]{};    // the gesture amounts as they move (rate-limited: a flick takes ~120 ms)
    bool slewInit{false};
};

} // namespace ghost

#endif
