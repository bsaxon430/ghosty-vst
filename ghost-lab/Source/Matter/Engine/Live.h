// Ghost Matter - AGPL-3.0-only (see LICENSE, NOTICE.md).
//
// The live side (audio thread): PLAY the matter on MIDI keys (12 voices, at most 8 sounding, pitched from C3), the
// PATTERN, BOUNCE, SPACE, the output level and the true-peak ceiling. (TERRAIN moves these engines' own controls: Processor.)
// Nothing here allocates or locks after prepare(): the matter arrives as a pointer the message thread keeps alive.
#pragma once
#include "Renderer.h"
#include "Space.h"
#include "Pattern.h"
#include "Limiter.h"
#include "Bounce.h"
#include "TerrainDsp.h"
#include "HoodDefs.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

namespace matter
{
struct LiveControls
{
    float levelDb = 0.0f;       // -24 .. +6
    float attack = 0.01f;       // seconds 0.001 .. 4
    float release = 0.6f;       // seconds 0.01 .. 8
    bool loop = true;           // a held key keeps going round the matter
    float start = 0.0f;         // 0..1 where in the matter a key starts
    SpaceParams space;
    bool pattern = false;
    int steps = 16, hits = 5, rotate = 0;
    int rate = 2;               // 0 = 1/4, 1 = 1/8, 2 = 1/16, 3 = 1/32
    float mutate = 0.25f;       // 0 locked .. 1 random
    float range = 1.0f;         // octaves the pattern's notes spread over (0 = all on the key)
    float gate = 0.7f;          // how long each hit lasts (fraction of a step)
    float terrain = 1.0f;       // 0..1 how much the TERRAIN acts
    float bounceLevel = 0.7f, gravity = 0.5f, springy = 0.75f;   // BOUNCE
    hood::LivePins pins;        // UNDER THE HOOD: the live engines' controls you moved (Pertylizer, Box2D, PM-Synth in BOUNCE)
    bool ceiling = false;       // our own true-peak ceiling: an extra, OFF by default (the engines' sound stays as it is)
};

struct Transport { double bpm = 120.0; double ppq = 0.0; bool playing = false; };

// note events from the UI (the on-screen keys, the PLAY button) to the audio thread, lock free
class NoteQueue
{
public:
    void push (int note, float velocity) noexcept;      // velocity 0 = note off
    template <typename F> void drain (F&& f) noexcept
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (fifo.getNumReady(), s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) f (ev[(size_t) (s1 + i)].note, ev[(size_t) (s1 + i)].vel);
        for (int i = 0; i < n2; ++i) f (ev[(size_t) (s2 + i)].note, ev[(size_t) (s2 + i)].vel);
        fifo.finishedRead (n1 + n2);
    }
private:
    struct E { int note; float vel; };
    std::array<E, 256> ev {};
    juce::AbstractFifo fifo { 256 };
};

class LiveEngine
{
public:
    static constexpr int kVoices = 12, kMaxSounding = 8;
    void prepare (double sampleRate, int maxBlock);
    void reset();
    // audio thread: the matter to play (the caller keeps it alive while it, or the one before it, is in use)
    void setMatter (const Matter* m) noexcept;
    const Matter* currentMatter() const noexcept { return cur; }
    const Matter* previousMatter() const noexcept { return prev; }
    // fills `out` (adds the voices to what is in it when `keepInput`: the effect version)
    void process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const LiveControls& c, const Transport& tp, bool keepInput) noexcept;
    void noteOn (int note, float vel) noexcept;
    void noteOff (int note) noexcept;
    void allOff() noexcept;

    NoteQueue ui;                                  // the on-screen keys
    ghost::terrain::SpectrumTap tap;               // the TERRAIN page's analyzer
    Bounce bounce;                                 // the BOUNCE page's balls (struck physical models)

    // for the pages (read on the message thread)
    std::atomic<float> playhead { -1.0f };         // 0..1 through the matter of the newest voice (-1 = silent)
    std::atomic<int> sounding { 0 };
    std::atomic<float> outPeak { 0.0f };
    std::atomic<int> lastStep { -1 };
    int latency (bool ceilingOn) const { return ceilingOn ? limiter.latency() : 0; }

private:
    struct Voice
    {
        bool active = false, released = false, fromPattern = false;
        int note = 60, age = 0, gateLeft = -1, kill = 0;
        float vel = 1, env = 0, pan = 0.0f, panL = 1, panR = 1;
        double pos = 0, inc = 1;
    };
    std::array<Voice, kVoices> voices;
    std::array<bool, 128> held {};
    int heldCount = 0, ageCounter = 0, lastNote = kRootNote;
    const Matter* cur = nullptr;
    const Matter* prev = nullptr;
    int xfade = 0, xfadeLen = 1;
    double sr = 48000;
    LiveControls ctl;
    // pattern
    Pattern pattern;
    double clockBeats = 0;
    long long lastStepIndex = -1;
    int hitCount = 0;
    // chain
    Space space;
    TruePeakLimiter limiter;
    juce::AudioBuffer<float> scratch;
    float gainNow = 1.0f;

    void start (int note, float vel, bool fromPattern, float pan, int gateSamples) noexcept;
    void renderVoices (float* L, float* R, int n) noexcept;
    void patternTick (int offset, int n, const Transport& tp) noexcept;
    static float readAt (const Matter& m, double pos, int ch, bool loop) noexcept;
};
}
