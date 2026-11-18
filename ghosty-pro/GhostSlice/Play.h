#pragma once
// GHOST SLICE PLAY (GHOST-SLICE-STUDY.md 7.4): the arrangement's slices on MIDI keys.
//   slices()     the slice list of a project: its regions (REGIONS), N equal parts of the arrangement (EQUAL), or the MANUAL cuts
//   Bank         every slice pre-rendered at the host rate (STUDIO quality, from the cache when it has it) + a thumbnail. A note
//                plays from the bank, so a key sounds on the very next sample: zero added latency (the whole slice is the
//                pre-rendered head). Built on a worker; the audio thread only reads a published table.
//   Engine       the audio-thread side: MIDI (note on / off, pitch bend, all notes off) and UI auditions -> the shared voice pool
//                (Shared/SliceVoice.h: polyphony, click-free stealing / choke / retrigger, ADSR, velocity -> gain / filter).
//   mappingMidi  the mapping as MIDI: every slice's key at the slice's place in the arrangement (play it back = the arrangement)
#include "Engine.h"
#include "SliceVoice.h"
#include <array>

namespace gslice::analysis { struct Result; }
namespace gslice::play
{
struct Slice { int64 dst0 = 0, dst1 = 0; int64 regionId = 0; int64 length() const { return dst1 - dst0; } };   // HF on the output timeline

// a = the analysis (TRANSIENT / SYLLABLE / NOTE need it; without it those modes give one slice)
std::vector<Slice> slices (const Project& p, const Plan& plan, const analysis::Result* a = nullptr);
juce::String bankKey (const Project& p, const Plan& plan, const analysis::Result* a = nullptr);      // what the bank depends on
// GHOST CHOP: the cut points of an analysis / grid mode on the output timeline (HF, sorted, inside (0, plan length))
std::vector<int64> chopCuts (const Project& p, const Plan& plan, const analysis::Result* a);
const char* modeName (SliceMode m);                              // REGIONS / EQUAL / MANUAL / TRANSIENT / SYLLABLE / NOTE / BEAT

struct Bank
{
    juce::String key;
    double rate = 48000;
    std::vector<Slice> slices;                     // (EQUAL / MANUAL cuts snapped to zero crossings within +-2 ms)
    std::vector<juce::AudioBuffer<float>> audio;   // per slice, 2 channels, 1 ms edge fades at the cuts
    std::vector<std::array<float, 48>> thumbs;     // per slice: peak per 1/48
    // phase 16 HOLD VOWEL: per slice (empty = none), the slice up to its vowel's stable region, then that region sustained by
    // SUSTAIN grains (hold::) for kVowelHoldSeconds; built only for slices a HOLD VOWEL key plays
    std::vector<juce::AudioBuffer<float>> sustained;
};
constexpr double kVowelHoldSeconds = 8.0;
// the sustained variant of one slice (empty when the slice has no syllable vowel inside a 1x, unreversed stretch of a region)
juce::AudioBuffer<float> vowelSustain (const Plan& plan, const Slice& s, const juce::AudioBuffer<float>& sliceAudio, const SourceAudio& src, const WorkingAudio& w,
                                       const analysis::Result& a, double seconds = kVowelHoldSeconds);
std::shared_ptr<const Bank> buildBank (const Project& p, const Plan& plan, const SourceAudio* src, const WorkingAudio& w, StudioCache* cache,
                                       const std::function<bool()>& cancel = {}, const analysis::Result* a = nullptr);

// TURN INTO INSTRUMENT: slice i -> note base + i (keeps the per-key settings of keys that stay)
PlayMap autoMap (const PlayMap& m, int nSlices, int baseNote = 36);
double repeatSeconds (int slot, double bpm);                    // a key's retrigger pattern slot -> its period (0 = off)
juce::String repeatName (int slot);
juce::String noteName (int note);                                // "C1" = 36
juce::MidiFile mappingMidi (const PlayMap& m, const std::vector<Slice>& sl, double bpm, double hostRate);
// (note, start tick, length ticks) of a MIDI file's notes (round trip tests)
std::vector<std::array<double, 3>> notesOf (const juce::MidiFile& f);

struct Table
{
    uint64 gen = 0;
    std::shared_ptr<const Bank> bank;
    PlayMap map;
};

class Engine
{
public:
    static constexpr int kVoices = 32;
    void prepare (double sampleRate, int maxBlock);
    void publish (std::shared_ptr<Table> t);                     // message thread
    void collectGarbage();                                       // message thread
    std::shared_ptr<Table> currentTable() const { return keep.empty() ? nullptr : keep.back(); }
    // audio thread: ADD the played slices for this block (MIDI events at their sample offsets). bpm: for the keys' REPEAT slots.
    void process (float* L, float* R, int n, const juce::MidiBuffer& midi, double bpm) noexcept;
    // any thread: a pad / key click (queued; played at the start of the next block)
    void audition (int note, float velocity, bool on) noexcept;
    bool isSounding (int note) const noexcept { return note >= 0 && note < 128 && (sounding[(size_t) note >> 6].load (std::memory_order_relaxed) >> (note & 63) & 1u) != 0; }
    std::atomic<int> activeVoices { 0 }, fadingVoices { 0 }, steals { 0 }, notesPlayed { 0 };
    ghosty::slicevoice::VoicePool<kVoices, 8>& voicePool() { return pool; }
private:
    void handle (const juce::MidiMessage& m, const Table& t, double bpm) noexcept;
    ghosty::slicevoice::VoicePool<kVoices, 8> pool;
    double sr = 48000;
    std::atomic<Table*> current { nullptr };
    std::atomic<uint64> minGenInUse { 0 };
    std::vector<std::shared_ptr<Table>> keep;
    uint64 genCounter = 0;
    struct Ev { int note; float vel; bool on; };
    std::array<Ev, 256> fifoBuf {}; juce::AbstractFifo fifo { 256 };
    juce::SpinLock auditionLock;                                 // (writers only: the audio thread never takes it)
    std::array<std::atomic<uint64>, 2> sounding {};
};
} // namespace gslice::play

namespace gslice::ops
{
R setPlay (const Project&, const PlayMap&);
R turnIntoInstrument (const Project&, int nSlices);
// SPLIT @ SLICES: regions cut at the slice points (output PF), so each slice is a block in the lane strip to rearrange
R splitAtSlices (const Project&, const std::vector<double>& cutsPF, int* made = nullptr);
}
