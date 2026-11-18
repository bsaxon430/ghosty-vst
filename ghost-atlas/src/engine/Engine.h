// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST ATLAS - the sound engine: eight layers, each one sfizz instrument with its own strip (level, pan, pitch, key
// and velocity range, envelope shaping, filter, width, an LFO, two effect slots, three sends), then three shared
// effect sends and a master chain. Every effect is one of Phantom FX's own units (or VOICE SHIFT, which is Signalsmith
// Stretch in the same kind of slot).
//
// Threads. The audio thread only plays: it never loads, allocates, locks or waits. Anything that takes time (an
// instrument, an effect unit) is built by the loader thread and handed over finished; the audio thread fades the old
// one out (12 ms), swaps, and fades the new one in, so changing a preset neither clicks nor stops the stream. Old
// objects go back to the loader thread and are destroyed there a moment later. Offline (bounces, the analysis, the
// checks) the same code runs on the calling thread with every sample loaded first.
//
// Whatever the host's block size, the engine works in pieces of at most kBlock samples, so nothing has to be built
// again when the host changes its buffer.
#pragma once
#include "../core/Preset.h"
#include "../core/Analysis.h"
#include "../sfz/SfzLayer.h"
#include "effects/Catalog.h"
#include "rack/EffectSlot.h"

namespace atlas
{
struct FxAddress
{
    enum Kind { Insert, Send, Master } kind = Send;
    int layer = 0, slot = 0;
    static FxAddress insert (int layer, int slot) { return { Insert, layer, slot }; }
    static FxAddress send (int k) { return { Send, 0, k }; }
    static FxAddress master (int k) { return { Master, 0, k }; }
    bool operator== (const FxAddress& o) const { return kind == o.kind && layer == o.layer && slot == o.slot; }
};

// the effect units a slot can hold: Phantom FX's 24, VOICE SHIFT, and SPECTRAL FX (Poltergeist 2's rack)
struct UnitInfo { juce::String key, title, blurb; int group = 0; juce::uint32 accent = 0xffffffff; };
const std::vector<UnitInfo>& fxUnits();
std::vector<phantom::ParamSpec> fxParams (const juce::String& unitKey);
juce::String fxTitle (const juce::String& unitKey);
FxState fxDefault (const juce::String& unitKey, bool on = true);       // the unit at its own defaults
int fxUnitIndex (const juce::String& unitKey);                         // its place in Phantom FX's table (-1: not one of those)

class Engine
{
public:
    static constexpr int kBlock = 512;
    using Resolver = std::function<juce::String (const juce::String& instrumentId)>;   // library id -> SFZ path ("" = not found); any thread

    Engine();
    ~Engine();

    void setResolver (Resolver r) { resolver = std::move (r); }
    void setOffline (bool on, bool withEffects = true);    // before prepare(): one thread, everything loaded at once
    void prepare (double sampleRate);                      // never while process() runs
    void process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const phantom::TempoInfo& tempo);   // audio thread
    // What the host is told: the master chain's delay, and the most any layer's own effects hold it back (VOICE SHIFT
    // works on a window of sound, so its layer speaks late). See setOnTimeline().
    int latencySamples() const { return latency.load() + alignBy.load(); }
    int layersHeldBackBy() const { return alignBy.load(); }
    // The host's timeline is running (or it is exporting): every layer is held back to meet the slowest one, so the notes
    // the host plays land where they were written - it sends them early by the latency it was told. With the host
    // stopped nothing is held back: keys played by hand answer at once, and only the slow layer trails. Any thread.
    void setOnTimeline (bool on) noexcept { timeline.store (on, std::memory_order_relaxed); }

    // ---- the sound (message thread)
    void setPreset (const Preset& p);
    Preset getPreset() const;
    void setLayerParam (int layer, int param, float value);
    float getLayerParam (int layer, int param) const;
    void setLayerSource (int layer, const LayerState& source);         // instrument / loop / reverse / voices / fades / insert units
    void setFx (FxAddress where, const FxState& fx);                   // a different unit is built in the background
    void setFxValue (FxAddress where, const juce::String& id, float value);
    void setFxOn (FxAddress where, bool on);
    void setFxMix (FxAddress where, float mix);
    FxState getFx (FxAddress where) const;
    // SPECTRAL FX in that place: the newest spectrum going into its window (before) or coming out (see SpectralRack.h)
    bool rackSpectrum (FxAddress where, std::vector<float>& magnitudes, double& rate, bool before);
    void setReturn (int send, float level);
    void setMasterDb (float db);
    void setMacro (int index, float value);
    float getMacro (int index) const { return macroValue[(size_t) juce::jlimit (0, kMacros - 1, index)].load(); }
    void setMacros (const std::array<Macro, kMacros>& m);
    void setMeta (const Preset& p);                                    // name, category, world, tags, description only
    float getMasterDb() const { return masterDb.load(); }

    // ---- audio thread: one of the host's own controls moved (no locks; getPreset() picks these values up)
    void hostMasterDb (float db) noexcept { masterDb.store (juce::jlimit (-60.0f, 12.0f, db), std::memory_order_relaxed); }
    void hostMacro (int i, float v) noexcept { if (i >= 0 && i < kMacros) macroValue[(size_t) i].store (clamp01 (v), std::memory_order_relaxed); }
    void hostLayerGain (int layer, float db) noexcept { if (layer >= 0 && layer < kLayers) lp[(size_t) layer][lpGain].store (juce::jlimit (-60.0f, 12.0f, db), std::memory_order_relaxed); }

    // ---- what the screen reads
    float layerLevel (int layer) const { return layerPeak[(size_t) juce::jlimit (0, kLayers - 1, layer)].load(); }
    float outputLevel (int ch) const { return outPeak[(size_t) juce::jlimit (0, 1, ch)].load(); }
    int voices() const { return voiceCount.load(); }
    // the last moments of what was played (both sides together), oldest first, for the picture of the sound on the Play
    // page. The audio thread writes and the screen copies; a copy made while it is written only blurs one picture.
    static constexpr int kScope = 4096;
    void readScope (float* dest, int count) const noexcept
    {
        count = juce::jlimit (0, kScope, count);
        const int end = scopeAt.load (std::memory_order_relaxed);
        for (int i = 0; i < count; ++i) dest[i] = scope[(size_t) ((end - count + i) & (kScope - 1))].load (std::memory_order_relaxed);
    }
    double sampleRateNow() const noexcept { return sr; }
    bool loading() const { return jobsPending.load() > 0; }
    juce::String layerProblem (int layer) const;                       // "" or why the layer has no sound (instrument missing ...)
    float lfoValue (int layer) const { return lfoOut[(size_t) juce::jlimit (0, kLayers - 1, layer)].load(); }
    void waitUntilLoaded (int timeoutMs = 60000) const;                // not the audio thread
    // The host is rendering faster than real time (an export): every note then waits for its recordings, so none is cut
    // short by a disk that cannot keep up. Any thread; the plug-in passes on what the host says before each block.
    void setHostOffline (bool on) noexcept { hostOffline.store (on, std::memory_order_relaxed); }

    // ---- playing without a keyboard
    void audition (int pattern, int rootNote = 60);                    // 0 chord, 1 arpeggio, 2 line
    void stopAudition();
    void panic();
    void noteFromScreen (int note, float velocity, bool on);

    // ---- offline: the same engine, run on the calling thread
    static juce::AudioBuffer<float> bounce (const Preset& p, const Resolver& resolver, const std::vector<Note>& notes,
                                            double seconds, double sampleRate = 44100.0, bool withEffects = true, bool onTimeline = false);

private:
    struct SlotRt;
    struct SourceRt;
    struct Dsp;
    struct Loader;
    template <typename T> struct Cell { std::atomic<T*> pending { nullptr }, live { nullptr }; };

    std::unique_ptr<SlotRt> buildSlot (const FxState& fx, bool asSend) const;
    std::unique_ptr<SourceRt> buildSource (const LayerState& l, juce::String& problem) const;
    void build (int key);                                              // loader thread (or inline when offline)
    void request (int key);
    Cell<SlotRt>& cellFor (FxAddress where);
    FxState& stateFor (FxAddress where);
    void retire (void* p, int type) noexcept;
    void applyValues (SlotRt& rt, const FxState& fx) const;
    void reask (SlotRt& rt) const;
    void syncSlot (FxAddress where);
    void compileMacros();
    void updateLatency();
    void tendRacks();
    void runAudition (juce::MidiBuffer& into, int numSamples);
    void processChunk (float* outL, float* outR, int n, const juce::MidiBuffer& midi, const phantom::TempoInfo& tempo, bool panicNow);

    Resolver resolver;
    double sr = 48000.0;
    bool offline = false, effects = true, prepared = false;

    // ---- shared with the audio thread
    std::array<std::array<std::atomic<float>, kNumLayerParams>, kLayers> lp;
    std::array<std::atomic<float>, kSends> returnLevel;
    std::atomic<float> masterDb { 0.0f };
    std::array<std::atomic<float>, kMacros> macroValue;
    static constexpr int kMacroTargets = 12;
    struct MacroSlot { std::atomic<int> code { -1 }; std::atomic<float> amount { 0.0f }; };
    std::array<std::array<MacroSlot, kMacroTargets>, kMacros> macroMap;
    std::array<Cell<SourceRt>, kLayers> source;
    std::array<std::array<Cell<SlotRt>, kInserts>, kLayers> insert;
    std::array<Cell<SlotRt>, kSends> send;
    std::array<Cell<SlotRt>, kMasterSlots> masterSlot;
    std::array<std::atomic<float>, kLayers> layerPeak, lfoOut;
    std::array<std::atomic<float>, 2> outPeak;
    std::array<std::atomic<float>, kScope> scope;
    std::atomic<int> scopeAt { 0 };
    std::atomic<int> voiceCount { 0 }, latency { 0 }, jobsPending { 0 };
    std::atomic<bool> hostOffline { false }, timeline { false };
    std::atomic<int> alignBy { 0 };                  // the most any layer in use is held back by its own effects, in samples
    std::atomic<bool> panicFlag { false };
    std::atomic<int> auditionRequest { -1 }, auditionRoot { 60 };
    juce::AbstractFifo screenFifo { 256 };
    std::array<juce::MidiMessage, 256> screenNotes;

    // ---- audio thread only
    std::unique_ptr<Dsp> dsp;

    // ---- message thread and loader thread, under stateLock
    mutable juce::CriticalSection stateLock;
    Preset current;
    std::array<juce::String, kLayers> problems;
    std::array<juce::String, kLayers> sourceAsked;                     // the signature last asked for (message thread)
    std::unique_ptr<Loader> loader;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Engine)
};
} // namespace atlas
