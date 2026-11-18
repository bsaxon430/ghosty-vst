/*
 * Ghost Bass b11 - BLEND BOARD MIXER: up to four sounds, each one played by its OWN copy of the full original engine
 * (a light copy: no library scan), mixed by the puck. Copyright 2026 the Ghost Bass authors. GPL-3.
 *
 * A puck fully on a corner plays ONLY that sound, exactly as the engine renders it alone (gain 1, pan centre, nothing else in the path).
 * In between, the levels follow the puck's distance weights, equal power (gain = sqrt(weight)), so the loudness stays even.
 * A corner whose weight is 0 stops being rendered (CPU). Held notes are remembered, so a sound that wakes up joins in at once.
 */
#ifndef GHOSTBASS_SRC_GHOST_LAYERS_GHOSTMIXER_H
#define GHOSTBASS_SRC_GHOST_LAYERS_GHOSTMIXER_H

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_events/juce_events.h>
#include <array>
#include <iosfwd>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include "BlendGuard.h"

class SurgeSynthesizer;
class SurgeSynthProcessor;
namespace ghost
{
class GhostEngine;
}

namespace ghost::layers
{
class Mixer : private juce::AsyncUpdater
{
  public:
    static constexpr int kSlots = 4;
    Mixer();
    ~Mixer();

    // ---- message thread ----
    bool setSlot(int k, const std::string &path, const std::string &name); // loads the sound into the slot's own engine
    void clearSlot(int k);
    bool filled(int k) const { return k >= 0 && k < kSlots && slot[(size_t)k].filled.load(std::memory_order_acquire); }
    std::string slotPath(int k) const;
    std::string slotName(int k) const;
    int filledCount() const;
    void prepare(double sampleRate);

    // ---- any thread ----
    std::atomic<bool> on{false};                 // the board is the mixer (set by the board page when 2+ slots are filled)
    std::atomic<float> weight[kSlots]{{0.f}, {0.f}, {0.f}, {0.f}}; // the puck's distance weights (sum 1)
    std::atomic<float> vol[kSlots]{{1.f}, {1.f}, {1.f}, {1.f}};    // linear, 0..1.5
    std::atomic<float> pan[kSlots]{{0.f}, {0.f}, {0.f}, {0.f}};    // -1..1, 0 = centre (no gain change)
    std::atomic<float> shown[kSlots]{{0.f}, {0.f}, {0.f}, {0.f}};  // the gain each slot really has now (for the display)
    // CLEAN BLEND (ours; ON by default): only acts while two or more sounds are playing. Where two of them fight in the same band, the
    // quieter one is ducked gently in that band (6 bands, JUCE's Linkwitz-Riley splits + ballistics). With the SUB on and GHOST LOCK's
    // stack off, every sound is also high-passed at the split so the SUB owns the lows (with the stack on, GHOST LOCK does that to the sum).
    std::atomic<bool> clean{true};
    std::atomic<float> splitHz{110.f}; // set by the layer host from GHOST LOCK (the crossover)
    std::atomic<bool> subHighPass{false}; // set by the layer host: SUB is on and the stack is off
    std::atomic<float> puckX{0.5f}, puckY{0.5f};
    // BLEND GUARD (owner, 2026-11-16): keeps the blend full when the sounds cancel each other, and says so (BlendGuard.h).
    // Sources 0..3 = slots A..D; it runs after each sound's level and pan, before (phase) and after (body) the mix.
    ghostblock::BlendGuard guard;
    bool isDefault() const;
    void resetAll(); // message thread: every slot empty, volumes 100%, pans centre, puck in the middle
    // the board's state as text keys (saved in the project with the layers) and back
    void save(std::ostream &o) const;
    bool loadKey(const std::string &key, const std::string &raw);
    void loadDone(); // after all keys: loads the sounds, switches the board on when two or more slots are filled
    bool wantsEvents() const { return on.load(std::memory_order_relaxed); }

    // ---- audio thread: L/R hold the main engine's block; when the board is on they are replaced by the mix. false = untouched. ----
    bool block(float *L, float *R, int n, ghost::GhostEngine &gb);
    // the slot's last rendered block (BLOCK_SIZE samples), for the frequency view
    const float *lastBlock(int k, int ch) const;

    // what each sound (A-D = 0..3) and the SUB (4) is playing right now, post level and pan, mono, the last 2048 samples, for the frequency view
    static constexpr int kRing = 2048, kSources = kSlots + 1;
    void pushSub(const float *x, int n, float gain);
    // copies the last kRing samples of a source, oldest first (any thread; a torn read is harmless for a picture)
    void snapshot(int src, float *out) const;
    static float panGain(float pan, int ch) { return ch == 0 ? (pan > 0.f ? 1.f - pan : 1.f) : (pan < 0.f ? 1.f + pan : 1.f); }
    // the engine of a slot (tests only)
    SurgeSynthesizer *engineForTest(int k);
    // the main engine (its wavetable and IR lists are copied into every board sound's engine, so none of them scans the library again)
    void setMain(SurgeSynthesizer *m) { mainSurge = m; }

  private:
    struct Slot
    {
        std::unique_ptr<SurgeSynthProcessor> proc; // a whole plugin processor, built exactly like the main one: same construction, same MIDI path, same block loop
        std::mutex mtx;
        juce::AudioBuffer<float> audio{6, 32};
        juce::MidiBuffer midi;
        std::atomic<bool> filled{false};
        std::string path, name;
        bool awake{false};
        float g[2]{0.f, 0.f};
        std::array<float, 64> last[2]{};
        Slot();
        ~Slot();
    };
    std::array<Slot, kSlots> slot;
    struct Ring
    {
        std::array<float, kRing> buf{};
        std::atomic<uint32_t> pos{0};
    };
    std::array<Ring, kSources> ring;
    void ringPush(int src, const float *x, int n)
    {
        auto &r = ring[(size_t)src];
        uint32_t p = r.pos.load(std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
            r.buf[(p + (uint32_t)i) & (kRing - 1)] = x[i];
        r.pos.store(p + (uint32_t)n, std::memory_order_release);
    }
    double sr{48000.0};
    SurgeSynthesizer *mainSurge{nullptr};
    std::string pend[kSlots][2]; // paths and names read from a saved project, loaded by loadDone()
    // held notes (key -> velocity), the pitch wheel and the sustain pedal: what a waking slot needs to join in
    std::map<int, int> held;
    int bend{0};
    int sustain{0};
    void handleAsyncUpdate() override;
    void loadPending();
    // ---- CLEAN BLEND state ----
    static constexpr int kBands = 6, kCross = kBands - 1;
    struct Clean
    {
        juce::dsp::LinkwitzRileyFilter<float> x[kCross];
        juce::dsp::LinkwitzRileyFilter<float> hp;
        juce::dsp::LinkwitzRileyFilter<float> ap[kCross][kCross]; // band b is phase-matched to the crossovers above it (ap[b][j], j > b), so the bands add back flat
        juce::dsp::BallisticsFilter<float> env[kBands];
        float gain[kBands]{1.f, 1.f, 1.f, 1.f, 1.f, 1.f}; // the ducking each band has now (linear)
        bool fresh{true}; // just (re)started: the low owner takes its place at once instead of gliding in
        float own{0.f}; // build 15 LOW OWNER: how much of this sound's lows (below 120 Hz) is in the mix now (glides 0..1)
        float level[kBands]{};                              // band level (linear), this block
        void prepare(double sr);
        void reset();
    };
    std::array<Clean, kSlots> clean_;
    int lowOwner_{-1}; // build 15: which sound owns the lows now
    bool cleanPrepared{false};
    static constexpr int kBlock = 32; // Surge's block (BLOCK_SIZE)
    float vL_[kSlots][kBlock]{}, vR_[kSlots][kBlock]{}; // each playing sound's block, after its level and pan
    void cleanMix(float *accL, float *accR, const bool *act, int n);
    void wake(Slot &s);
    void sleepSlot(Slot &s);
};
} // namespace ghost::layers

#endif
