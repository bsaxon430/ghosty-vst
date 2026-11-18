#pragma once
// Ghosty Spectrum ARRANGE page: the data model. Tracks of clips on a tempo-synced timeline (positions in beats), each
// clip a rendered sound handed over by any Spectrum page (SPECTRAL LAB, ARCS, VOICE, PROCESS LAB, a file...) plus its
// edit: pitch (keep length or tape), stretch, tempo WARP, content LOOP, reverse, curved fades, gain + gain envelope, pan,
// spectral morph into the next clip. Tracks have gain / pan and VOLUME / PAN automation lanes; the ruler has markers.
// Everything here is plain data on the message thread; ArrangeEngine turns it into audio.
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <cmath>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace arr {

constexpr int kMaxTracks = 16;
constexpr double kMinClipBeats = 1.0 / 64.0;
constexpr double kMaxBeats = 16384.0;               // 4096 bars of 4/4: nothing in a state or an edit goes past this
constexpr double kMaxOffsetSeconds = 3600.0;
constexpr double kMaxSoundSeconds = 20.0 * 60.0;    // longest sound Arrange keeps (longer is cut, and the user is told)
constexpr int kMaxEnvPoints = 4096;

inline double finiteOr(double v, double def) { return std::isfinite(v) ? v : def; }
inline double clampF(double v, double lo, double hi, double def) { return juce::jlimit(lo, hi, finiteOr(v, def)); }   // NaN / Inf safe

// ------------------------------------------------------------------ CLIP SOURCE interface
// A page hands Arrange rendered audio (+ optional notes for MIDI export) and a tag + opaque state blob that lets the page
// re-render the sound later (SourceRef::page names the page, SourceRef::state is whatever it needs). Arrange never looks inside.
struct Note { int pitch = 60; float velocity = 1.0f; double start = 0, length = 0.5; };   // seconds, relative to the sound's start
struct SourceRef { juce::String page, name; juce::MemoryBlock state; };
juce::uint64 newPayloadUid();
struct Payload {
    juce::uint64 uid = newPayloadUid();             // identifies the sound for the render cache (never reused)
    juce::AudioBuffer<float> audio;                 // 1 or 2 channels, any sample rate (resampled to the project rate when rendered)
    double sampleRate = 48000.0;
    juce::String name;
    std::vector<Note> notes;                        // optional: the sound is note based (ARCS arcs, a melody...)
    SourceRef source;
    double bpm = 0;                                 // tempo the sound was made at (> 0: a tempo-tagged loop; its clips WARP to the song tempo)
    bool rendered = false;                          // provenance: the PLUGIN made it (a page render, bounce, demo clip); false = the user's own
                                                    // imported file ("File" / "Shared"). Saved as "made"; older states (no "made") load as imported.
    int lengthSamples() const { return audio.getNumSamples(); }
    double seconds() const { return sampleRate > 0 ? audio.getNumSamples() / sampleRate : 0.0; }
    bool valid() const { return audio.getNumChannels() >= 1 && audio.getNumSamples() > 0 && sampleRate > 1000; }
};
using PayloadPtr = std::shared_ptr<const Payload>;
// convenience for pages: "here is a buffer, call it this" (copies the audio, clears NaN / Inf, keeps it on a 24-bit grid,
// cuts it at kMaxSoundSeconds; sanitises notes and bpm)
PayloadPtr makePayload(const juce::AudioBuffer<float>& audio, double sampleRate, const juce::String& name,
                       std::vector<Note> notes = {}, SourceRef source = {}, double bpm = 0);

// Re-render: a page registers how to re-render its sounds, PER Session (per plugin instance), and keeps the returned token;
// destroying the token unregisters (and waits for a re-render that is running). See Session::registerSource.
using Rerender = std::function<PayloadPtr(const SourceRef&)>;
class SourceRegistry;
class SourceRegistration {                          // move-only RAII token
public:
    SourceRegistration() = default;
    SourceRegistration(SourceRegistration&&) noexcept = default;
    SourceRegistration& operator=(SourceRegistration&& o) noexcept { reset(); entry = std::move(o.entry); return *this; }
    ~SourceRegistration() { reset(); }
    void reset();                                   // unregister now
    bool active() const { return entry != nullptr; }
    struct Entry { std::recursive_mutex m; Rerender fn; juce::String page; };
private:
    friend class SourceRegistry;
    std::shared_ptr<Entry> entry;
};
class SourceRegistry {
public:
    SourceRegistration add(const juce::String& page, Rerender fn);   // the newest registration for a tag wins
    PayloadPtr rerender(const SourceRef& ref);      // nullptr when nobody (alive) owns that tag
    bool has(const juce::String& page);
private:
    std::mutex m; std::vector<std::weak_ptr<SourceRegistration::Entry>> entries;
};

// ------------------------------------------------------------------ fades / envelopes
enum class Curve : int { Linear = 0, EqualPower, Exponential, SCurve, Count };
const char* curveName(Curve c);
float fadeInGain(Curve c, double x);                // x 0..1 along the fade -> gain 0..1 (fade out uses 1 - x)
enum class MorphShape : int { SCurve = 0, Linear, Early, Late, Count };
const char* morphShapeName(MorphShape s);
double morphCurve(MorphShape s, double x);          // 0..1 -> 0..1 (0 and 1 kept)
struct EnvPoint { double beat = 0; float value = 0; };
using Envelope = std::vector<EnvPoint>;             // sorted by beat; clip gain: dB, beats from the clip start; track volume: dB, pan: -1..1 (song beats)
float evalEnvelope(const Envelope& e, double beat, float def);
void sortEnvelope(Envelope& e);

// ------------------------------------------------------------------ the arrangement
struct Clip {
    juce::uint32 id = 0;
    PayloadPtr source;                              // shared between duplicates; immutable
    juce::String name; int colour = 0;
    double start = 0, length = 4, offset = 0;       // beats, beats, seconds into the processed (pitched / stretched / warped / reversed) sound
    float pitch = 0;                                // semitones, -24..24
    bool keepLength = true;                         // true: pitch shift keeps the length (Signalsmith Stretch); false: tape (resample, length changes)
    float stretch = 1.0f;                           // 0.25..4 (2 = twice as long)
    bool warp = false;                              // follow the song tempo (tempo-tagged sounds: Payload::bpm > 0; on by default for those)
    bool loop = false;                              // clip longer than its sound: the sound repeats (declicked at the wrap)
    bool reverse = false;
    double fadeIn = 0, fadeOut = 0;                 // beats
    Curve fadeInCurve = Curve::EqualPower, fadeOutCurve = Curve::EqualPower;
    float gainDb = 0, pan = 0;                      // dB, -1..1
    Envelope gainEnv;                               // clip gain envelope (dB, beats from the clip start), on top of gainDb
    bool morph = false;                             // spectral morph over the overlap with the next clip on the track
    MorphShape morphShape = MorphShape::SCurve; float morphAmount = 1.0f, morphSmear = 0.0f;   // amount 0 = plain crossfade, 1 = full spectral; smear = spectral blur at the centre
    bool mute = false;
    double end() const { return start + length; }
    double warpRatio(double bpm) const { return warp && source && source->bpm > 0 && bpm > 0 ? source->bpm / bpm : 1.0; }   // time scale from WARP
    double processedSeconds(double bpm) const;      // length of the processed sound in seconds at the song tempo
    double naturalBeats(double bpm) const { return processedSeconds(bpm) * bpm / 60.0; }
    bool noteBased() const { return source && !source->notes.empty(); }
    juce::String processKey(double bpm) const;      // identifies the processed audio (source + pitch + keepLength + stretch + warp + reverse)
    juce::String baseKey() const;                   // the same without the tempo (a stand-in while a re-warp is processed)
};
struct Track {
    juce::uint32 id = 0; juce::String name; int colour = 0;
    float gainDb = 0, pan = 0; bool mute = false, solo = false;
    Envelope volEnv, panEnv;                        // automation (song beats); a non-empty lane replaces the static value
    bool collapsed = false; bool showAuto = false; int autoLane = 0;   // view: collapsed header, automation lane shown, 0 = VOLUME, 1 = PAN
    std::vector<Clip> clips;                        // kept sorted by start
};
struct Marker { double beat = 0; juce::String name; };
struct Arrangement {
    std::vector<Track> tracks;
    std::vector<Marker> markers;                    // sorted by beat
    double bpm = 120; int beatsPerBar = 4;
    double loopStart = 0, loopEnd = 16; bool loopOn = false;
    double snapBeats = 1; bool snapOn = true;       // grid: bar, 1/4, 1/8, 1/16, 1/32, 1/8T, 1/16T
    juce::uint32 nextId = 1;
    Arrangement();
    Track& addTrack(const juce::String& name = {});
    void removeTrack(int index);
    Clip* addClip(int track, PayloadPtr source, double startBeats, double lengthBeats = -1);   // -1 = the sound's natural length
    Clip* findClip(juce::uint32 id, int* trackIndex = nullptr);
    const Clip* findClip(juce::uint32 id, int* trackIndex = nullptr) const;
    bool removeClip(juce::uint32 id);
    Clip* duplicateClip(juce::uint32 id, double newStart);   // same track, same source
    Clip* splitClip(juce::uint32 id, double atBeats);       // returns the right half
    bool moveClip(juce::uint32 id, int toTrack, double newStart);
    void sortClips();
    void sanitise();                                // clamps every field into its legal range (NaN / Inf / huge -> defaults)
    double lengthBeats() const;                     // end of the last clip
    double snap(double beats) const { return snapOn && snapBeats > 0 ? std::round(beats / snapBeats) * snapBeats : beats; }
    double snapDown(double beats) const { return snapOn && snapBeats > 0 ? std::floor(beats / snapBeats + 1e-9) * snapBeats : beats; }
    bool anySolo() const;
    bool trackAudible(int i) const;                 // mute / solo resolved
    int clipCount() const;
    // beats <-> seconds at the arrangement tempo
    double beatsToSeconds(double b) const { return b * 60.0 / bpm; }
    double secondsToBeats(double s) const { return s * bpm / 60.0; }
    static double legalSnap(double v, int beatsPerBar);   // the nearest grid value the page offers
};

// ------------------------------------------------------------------ state
// Audio is stored once per distinct sound as 24-bit FLAC (lossless for sounds made with makePayload); duplicates share it.
juce::ValueTree toTree(const Arrangement& a);
// for a save: keepRendered false (the demo) leaves the audio, notes and re-render state of plugin-made sounds out ("stripped"
// sounds; their clips don't come back on load); the user's own imported files stay. *stripped counts the sounds left out.
juce::ValueTree toTree(const Arrangement& a, bool keepRendered, int* stripped);
juce::MemoryBlock saveState(const Arrangement& a, bool keepRendered, int* stripped);
bool fromTree(const juce::ValueTree& t, Arrangement& a, juce::String* error = nullptr);   // hostile input safe (caps, NaN, bad FLAC)
juce::MemoryBlock saveState(const Arrangement& a);                 // binary blob
bool loadState(const void* data, size_t size, Arrangement& a, juce::String* error = nullptr);
// 24-bit FLAC on a power-of-two scaled grid: exact for sounds that went through makePayload (quantise24)
int flacExponent(const juce::AudioBuffer<float>& audio);
void quantise24(juce::AudioBuffer<float>& audio, int exponent);
juce::MemoryBlock encodeFlac(const juce::AudioBuffer<float>& audio, double sampleRate, int exponent);
bool decodeFlac(const juce::MemoryBlock& flac, juce::AudioBuffer<float>& audio, double& sampleRate, int exponent);   // never trusts the stored length
juce::MemoryBlock encodeFlac(const juce::AudioBuffer<float>& audio, double sampleRate);   // plain +-1 range
bool decodeFlac(const juce::MemoryBlock& flac, juce::AudioBuffer<float>& audio, double& sampleRate);

// ------------------------------------------------------------------ undo
class History {
public:
    void push(const Arrangement& before);           // call before an edit
    bool undo(Arrangement& a); bool redo(Arrangement& a);
    bool canUndo() const { return !past.empty(); } bool canRedo() const { return !future.empty(); }
    void clear() { past.clear(); future.clear(); }
    int depth() const { return (int) past.size(); }
    size_t payloadBytesHeld() const;                // audio kept alive only by the history
    static constexpr size_t kMax = 120, kMaxBytes = (size_t) 384 << 20;
    size_t maxBytes = kMaxBytes;                    // audio the history may keep alive (beyond what the arrangement uses)
private:
    void trim();
    std::vector<Arrangement> past, future;
};

} // namespace arr
