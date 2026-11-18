// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX — the one interface every effect module implements. No module talks to the host; the rack does.
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace phantom
{
constexpr int kMaxBlock = 4096;     // the rack never hands a module more than this many samples per call (after oversampling)
constexpr int kSubBlock = 64;       // modulation / smoothing is refreshed at least this often (samples at the host rate)

// ---------------------------------------------------------------- host clock, one source for every rhythmic effect
// a MIDI note at a sample offset inside the host block (velocity 0 = note off); HAUNT EDIT plays its pads from these
struct NoteEvent { int sample = 0; int note = 0; float velocity = 0.0f; };

struct TempoInfo
{
    double bpm = 120.0;
    double ppqPosition = 0.0;        // quarter notes since the song start, at the first sample of this block
    double ppqAtBlockStart = 0.0;
    bool playing = false;
    bool looping = false;
    int timeSigNumerator = 4, timeSigDenominator = 4;
    double sampleRate = 48000.0;
    const NoteEvent* notes = nullptr;   // this block's MIDI notes, in sample order (host-rate offsets)
    int numNotes = 0;

    double samplesPerBeat() const noexcept { return sampleRate * 60.0 / juce::jmax (1.0, bpm); }
    double beatsPerBar() const noexcept { return timeSigNumerator * 4.0 / juce::jmax (1, timeSigDenominator); }
};

// ---------------------------------------------------------------- parameter declaration (declared once; the rack builds UI + automation)
struct ParamSpec
{
    enum class Type { Float, Choice, Bool, Int };
    std::string id;                  // stable, never renamed: "drive", "mix" ... (the rack prefixes "fx_<unit>_")
    std::string name;                // shown in the UI and the host
    Type type = Type::Float;
    float min = 0.0f, max = 1.0f, def = 0.0f;
    float skewCentre = 0.0f;         // > 0 inside (min,max): log-like range around this value
    std::string unit;                // "%", "dB", "Hz", "ms", "s", "st", ""
    std::vector<std::string> choices;
    float smoothMs = 20.0f;          // smoothing time in the module (0 = stepped: choices / switches only)
    float modDepth = 1.0f;           // 1 = a full-range modulation sweeps min..max; 0 = not modulatable
    bool automatable = true;

    static ParamSpec pct (std::string id, std::string name, float def, float smoothMs = 20.0f)
    { ParamSpec p; p.id = std::move (id); p.name = std::move (name); p.min = 0; p.max = 1; p.def = def; p.unit = "%"; p.smoothMs = smoothMs; return p; }
    static ParamSpec range (std::string id, std::string name, float min, float max, float def, std::string unit, float centre = 0.0f, float smoothMs = 20.0f)
    { ParamSpec p; p.id = std::move (id); p.name = std::move (name); p.min = min; p.max = max; p.def = def; p.unit = std::move (unit); p.skewCentre = centre; p.smoothMs = smoothMs; return p; }
    static ParamSpec choice (std::string id, std::string name, std::vector<std::string> items, int def)
    { ParamSpec p; p.id = std::move (id); p.name = std::move (name); p.type = Type::Choice; p.min = 0; p.max = (float) items.size() - 1; p.def = (float) def; p.choices = std::move (items); p.smoothMs = 0; p.modDepth = 0; return p; }
    static ParamSpec toggle (std::string id, std::string name, bool def)
    { ParamSpec p; p.id = std::move (id); p.name = std::move (name); p.type = Type::Bool; p.min = 0; p.max = 1; p.def = def ? 1.0f : 0.0f; p.smoothMs = 0; p.modDepth = 0; return p; }
    static ParamSpec integer (std::string id, std::string name, int min, int max, int def)
    { ParamSpec p; p.id = std::move (id); p.name = std::move (name); p.type = Type::Int; p.min = (float) min; p.max = (float) max; p.def = (float) def; p.smoothMs = 0; p.modDepth = 0; return p; }

    bool isContinuous() const noexcept { return type == Type::Float; }
};

// ---------------------------------------------------------------- what the rack hands a module every process() call
// values[i] = the i-th declared parameter's value in its own units, host value + modulation, for this block. Modules smooth it themselves
// (EffectBase does that). A module reads what it declared; it never sees the host.
struct ModulationContext
{
    const float* values = nullptr;   // one per declared parameter
    int numValues = 0;
    TempoInfo tempo;
    bool bypassed = false;           // the rack is fading this module out / in (modules keep running so tails and latency hold)
    int oversamplingFactor = 1;      // the rate the block is at, relative to the host rate (the shared layer applied it)
    double sampleRate = 48000.0;     // the rate of THIS block (host rate × oversamplingFactor)

    float value (int i) const noexcept { return (i >= 0 && i < numValues) ? values[i] : 0.0f; }
};

enum class StereoMode { TrueStereo, DualMono, MidSide };

// ---------------------------------------------------------------- the interface
// a lock-free tap of a unit's output (mono sum) for a spectrum display: one writer (audio thread), one reader (the editor's timer)
struct AnalyserTap
{
    static constexpr int kSize = 8192;
    juce::AbstractFifo fifo { kSize }; std::array<float, kSize> buf {}; std::atomic<bool> wanted { false }; std::atomic<double> sampleRate { 48000.0 };
    void push (const float* L, const float* R, int n) noexcept
    {
        if (! wanted.load (std::memory_order_relaxed)) return;
        int s1, z1, s2, z2; fifo.prepareToWrite (n, s1, z1, s2, z2);
        for (int i = 0; i < z1; ++i) buf[(size_t) (s1 + i)] = 0.5f * (L[i] + R[i]);
        for (int i = 0; i < z2; ++i) buf[(size_t) (s2 + i)] = 0.5f * (L[z1 + i] + R[z1 + i]);
        fifo.finishedWrite (z1 + z2);
    }
    int pull (float* dst, int maxN) noexcept
    {
        int s1, z1, s2, z2; fifo.prepareToRead (juce::jmin (maxN, fifo.getNumReady()), s1, z1, s2, z2);
        for (int i = 0; i < z1; ++i) dst[i] = buf[(size_t) (s1 + i)]; for (int i = 0; i < z2; ++i) dst[z1 + i] = buf[(size_t) (s2 + i)];
        fifo.finishedRead (z1 + z2); return z1 + z2;
    }
};

class PhantomEffect
{
public:
    virtual ~PhantomEffect() = default;

    virtual const char* key() const = 0;          // "gate", "crackle" ... (state keys and parameter ids: never renamed)
    virtual const char* title() const = 0;        // "GHOST GATE"
    virtual std::vector<ParamSpec> parameters() const = 0;

    // lifecycle (message / prepare thread; may allocate)
    virtual void prepare (double sampleRate, int maxBlockSize, int numChannels) = 0;
    virtual void reset() = 0;

    // audio thread: stereo in / out in place. No allocation, locks, strings or file I/O. Ever.
    virtual void process (juce::dsp::AudioBlock<float> block, const ModulationContext& ctx) noexcept = 0;

    // exact, in samples AT THE RATE THE MODULE WAS PREPARED WITH (the oversampled rate for nonlinear modules); the shared layer pads it
    // up to a whole host sample and reports that, so the rack compensates the dry path and parallel lanes exactly. It may change only in
    // prepare() or when a latency-changing choice parameter changes (the rack re-asks after such changes).
    virtual int getLatencySamples() const = 0;

    virtual StereoMode stereoMode() const { return StereoMode::TrueStereo; }
    virtual bool isNonlinear() const { return false; }     // nonlinear modules get the global oversampling policy; linear ones skip it
    virtual int oversamplingFactor() const { return 1; }   // what the module asks for (1, 2, 4, 8); the shared layer applies min(policy, this)
    virtual bool isGenerator() const { return false; }     // generators are exempt from the null test
    virtual bool hasNullSetting() const { return true; }   // false for filters / EQs that always shape (reported, not tested)
    virtual void neutralise (std::vector<float>& values) const { juce::ignoreUnused (values); }   // adjust the declared defaults to the module's true 'does nothing' setting
    // for the zipper check of a control that moves pitch or time (tape wow, for example): the other sections it should be heard
    // without, so the check measures the control's own stepping rather than the frequency modulation of distortion harmonics
    virtual void zipperIsolate (const std::string& controlId, std::vector<float>& values) const { juce::ignoreUnused (controlId, values); }
    // for the aliasing check: pitch / time modulation to switch off (it spreads the harmonics, which is not aliasing)
    virtual void aliasIsolate (std::vector<float>& values) const { juce::ignoreUnused (values); }
    virtual bool changesLevelOnPurpose() const { return false; }   // dynamics / limiter: exempt from the ≤ 1 dB gain-staging rule
    virtual bool isRhythmic() const { return false; }      // must pass the tempo-lock test
    // a wet-only space effect (reverb, echo, chorus ...): its output is all effect; the slot's MIX blends the dry. Its 'neutral / 0 %'
    // setting is MIX at 0 %, so the null and latency checks run there (the dry path must be exact and aligned)
    virtual bool wetOnly() const { return false; }
    // its 'does nothing' setting is the slot's MIX (DEPTH) at 0 % although its output is not wet-only (GHOST SQUEEZE: a crossover
    // compressor has no setting that is the dry signal; at DEPTH 0 % the slot passes the latency-aligned dry)
    virtual bool neutralAtMixZero() const { return wetOnly(); }
    // makes a stereo image from a mono source by design (reverbs, chorus): the stereo check then asks for exact mono at WIDTH 0 and
    // for a mono sum that never cancels at the defaults
    virtual bool widensOnPurpose() const { return false; }
    // how far (Hz) the module's own modulation spreads a steady tone (a modulated reverb or chorus): the zipper check leaves that band
    // around each harmonic out, as it does for a nonlinear module's harmonics, and measures stepping everywhere else
    virtual double ownSpreadHz() const { return 0.0; }
    // a control that only changes the plan of future grains in a granular engine (each grain is its own event): its zipper reading is
    // reported, not failed (DECISIONS.md 24)
    virtual bool zipperEventControl (const std::string&) const { return false; }
    virtual double tailSeconds() const { return 0.0; }
    // after a reset (switching on from sleep), how many host samples the module needs before its output is right (a stretcher filling
    // its buffers): the slot keeps playing the latency-aligned dry meanwhile, then fades the module in
    virtual int warmupSamples() const { return 0; }
    // the level of the null test's noise (1 = as for every effect). A limiter's 'does nothing' case is a signal under its ceiling.
    virtual float nullTestGain() const { return 1.0f; }
    virtual void tempo (const TempoInfo&) {}               // called once per block before process() (same info as ctx.tempo)
    // non-parameter state (drawn patterns, painted grids, pads, bend lanes). Message thread only: getData() returns it as a var
    // (saved in the project), setData() replaces it and hands it to the audio thread lock-free. A fresh module holds its factory data.
    // the unit's output for a spectrum display in the editor: the audio thread only copies its finished block into the FIFO (and
    // only while an editor asks); the editor's own timer does the analysis
    virtual struct AnalyserTap* analyserTap() { return nullptr; }
    // a gain-reduction read-out for the editor (dB, >= 0), written by the audio thread as one atomic value; nullptr = the module has none
    virtual const std::atomic<float>* reductionMeter() const { return nullptr; }
    virtual bool hasData() const { return false; }
    virtual juce::var getData() const { return {}; }
    virtual void setData (const juce::var&) {}
    // the rack calls this (message / prepare thread) before asking getLatencySamples(), so a latency-changing choice is seen at once
    virtual void updateParameters (const ModulationContext&) {}
};

} // namespace phantom
