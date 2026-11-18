#pragma once
// GHOST SLICE GENERATE, the audio-thread side: the current take on MIDI keys + the house FX chain.
//   Engine   voices read the published Take. INSTRUMENT / PAD / TEXTURE: the zone nearest the note, resampled the rest of the way
//            (4-point Hermite), looped at the baked loop points while the key is held, then the release; PAD / TEXTURE keys play a
//            detuned pair panned apart. CHOPS: MIDI 36 up = the chops, 60 = the whole phrase (FL Studio: C3 up, C5) (one shots). The take's preview plays on
//            request (the TAKES strip). Sustain pedal (CC 64) and pitch bend (+-2 semitones). Voices are stolen with a 4 ms fade.
//            The take is handed over by an atomic pointer swap; tables stay alive until no voice reads them (collectGarbage on the
//            message thread). No allocation, no lock on the audio thread.
//            A CLEAR take (Take::clear) plays through a 32-tap windowed sinc instead (dsp::Sinc; at the zone's own speed, with a zone on
//            every semitone, that is the sample itself), never as a detuned pair.
//            Build 6: an INSTRUMENT take with syllables on the keys (Take::sylZones, VOICE below the far right) sings the next syllable
//            of the sound on every key-press, in order (a vocal chop instrument), each held on its vowel while the key is down.
//   Fx       rumble high-pass -> gentle warmth (a soft curve, level-matched) -> TONE (tilt: low / high shelves, a top roll-off when dark)
//            -> ensemble chorus (two modulated delays, amount per take) -> SPACE (Ghost Verb, the house reverb of Ghosty Pro:
//            GhostyPro/ProVerb.h, original code) -> width (mid / side). The same class renders the exports offline, so DRAG WAV
//            sounds like the plugin.
#include "Generate.h"
#include "../GhostyPro/ProVerb.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <limits>

namespace gslice::gen
{
class Engine
{
public:
    static constexpr int kVoices = 40, kMaxActive = 32;
    void prepare (double sampleRate, int maxBlock);
    // message thread: the take the keys play (null = none). Old takes stay alive while voices still read them.
    // hopPreview (build 12): a preview already playing moves over to the new take's preview at the same place when the two are the same
    // length (TERRAIN: the shaped take takes over from the dry one without a break)
    void publish (std::shared_ptr<const Take> t, bool hopPreview = false);
    void collectGarbage();
    std::shared_ptr<const Take> current() const { return keep.empty() ? nullptr : keep.back()->take; }
    // audio thread: ADD this block's voices (MIDI at its sample offsets)
    void process (float* L, float* R, int n, const juce::MidiBuffer& midi) noexcept;
    // any thread (applied at the start of the next block)
    void playPreview (bool on) noexcept { previewReq.store (on ? 1 : 2); }
    void allNotesOff() noexcept { panicReq.store (true); }
    bool previewPlaying() const noexcept { return previewOn.load (std::memory_order_relaxed); }
    float previewPosition() const noexcept { return previewPos.load (std::memory_order_relaxed); }   // 0..1
    int activeVoices() const noexcept { return active.load (std::memory_order_relaxed); }
    static int zoneFor (const Take& t, int note);   // the zone index of a note (-1 = none)
private:
    struct Table { uint64 gen = 0; std::shared_ptr<const Take> take; };
    enum Kind { None = 0, ZoneV = 1, ShotV = 2, PreviewV = 3 };
    struct Voice
    {
        Kind kind = None; int note = -1; const Take* take = nullptr; uint64 gen = 0;
        const juce::AudioBuffer<float>* buf = nullptr; int loopStart = 0, loopEnd = 0;
        double pos = 0, inc = 1, detune = 1; float gl = 0.7f, gr = 0.7f, vel = 1.0f;
        float env = 0, attackInc = 1, releaseMul = 0.999f, kill = 0; bool releasing = false, held = false, pedalHeld = false, sinc = false; uint64 age = 0;
    };
    void noteOn (int note, float vel) noexcept;
    void noteOff (int note) noexcept;
    void startVoice (int note, float vel, Kind k, const juce::AudioBuffer<float>* b, int ls, int le, double inc, double detune, float pan) noexcept;
    Voice* freeVoice() noexcept;
    void release (Voice& v) noexcept;
    std::array<Voice, kVoices> voices;
    double sr = 48000; uint64 clock = 1;
    std::atomic<Table*> table { nullptr };
    Table* cur = nullptr;                    // the audio thread's copy for this block
    std::atomic<uint64> minGenInUse { std::numeric_limits<uint64>::max() };   // the oldest table a voice still reads
    std::atomic<const Table*> hazard { nullptr };                              // the table the audio thread is using
    std::vector<std::shared_ptr<Table>> keep;
    uint64 genCounter = 0;
    bool pedal = false; double bend = 0, bendSm = 0;
    int sylNext = 0;                         // INSTRUMENT with syllables on the keys: the next key-press sings this one
    std::atomic<bool> hopReq { false };
    std::atomic<int> previewReq { 0 }; std::atomic<bool> previewOn { false }, panicReq { false };
    std::atomic<float> previewPos { 0 }; std::atomic<int> active { 0 };
};

class Fx
{
public:
    // tone -1 (dark) .. 1 (bright); space 0..1 (the reverb); width 0..1 (0.5 = as it is); warmth 0..1; ensemble 0..1 (the take's)
    std::atomic<float> tone { 0.0f }, space { 0.4f }, width { 0.55f }, warmth { 0.35f }, ensemble { 0.3f };
    // CLEAR (the take's): no warmth curve, no chorus, the width as it is, and no top roll-off unless TONE goes dark
    std::atomic<bool> clear { false };
    // build 6: PAD DREAM / TEXTURE DRIFT (the take's): a bigger, longer, softer space on top of SPACE (0 = as before)
    std::atomic<float> dream { 0.0f };
    // build 13: the TERRAIN land's offsets on top of all of these (0 = none; SliceTerrainMap.h)
    std::atomic<float> modTone { 0.0f }, modSpace { 0.0f }, modWidth { 0.0f }, modWarmth { 0.0f }, modEnsemble { 0.0f }, modDream { 0.0f };
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void process (float* L, float* R, int n) noexcept;   // in place
    static constexpr double kTailSeconds = 4.0;
private:
    struct BQ { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1[2] { 0, 0 }, z2[2] { 0, 0 };
                inline float run (int ch, float x) noexcept { const double y = b0 * x + z1[ch]; z1[ch] = b1 * x - a1 * y + z2[ch]; z2[ch] = b2 * x - a2 * y; return (float) y; } };
    void setTone (float t);
    BQ hp, low, high, top; float toneFor = 99.0f; bool clearFor = false;
    std::vector<float> chBuf[2]; int chLen = 0, chPos = 0; double lfo = 0;
    ghosty::GhostVerb verb; ghosty::GhostVerb::Params vp;
    float widthSm = -1, ensSm = -1, warmSm = -1;
    double sr = 48000;
};

// the take's audition through a fresh FX chain + the reverb tail, finished to -14 LUFS / true peak below -1 dBTP (DRAG WAV / SAVE TAKE).
// A CLEAR take: its FX are CLEAR's and the finish is gain only (no limiter).
struct FxMod { float tone = 0, space = 0, width = 0, warmth = 0, ensemble = 0, dream = 0; const void* live = nullptr; };   // live: a ghost::terrain::LiveParams
juce::AudioBuffer<float> renderForExport (const Take& t, float tone, float space, float width, float warmth, const FxMod& mod = {});
} // namespace gslice::gen
