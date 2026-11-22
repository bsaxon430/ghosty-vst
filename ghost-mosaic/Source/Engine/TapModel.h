// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
//
// mosaic::tap::Model: everything a view may know about what the engine is doing, as plain numbers.
// The designers' guide is studio/TAP.md; this header is the reference.
//
// HOW IT IS USED (all on the message thread):
//     Model model (engine);          once, e.g. as a member of the page
//     model.setActive (true);        when the page is shown    (false when it is left)
//     model.tick (dtSeconds);        once per 60 Hz frame, BEFORE the views draw
//     ... views read the model ...   everything they get stays valid until the next tick
//
// WHAT IT NEVER DOES: it never touches the audio thread. The engine copies numbers into a queue while a model
// is switched on (Tap.h); tick() empties that queue. The slow part (doing every search again to explain it)
// runs on a low-priority thread of the model's own. Nothing here can change a sample of the output.
//
// HONESTY: when the screen falls behind, cells are dropped (never the audio); the model counts them
// (counters().cellsLost) and marks what it can no longer be sure of (Why::approximate, usedValid()).
#pragma once
#include "TapWhy.h"
#include <span>

namespace mosaic { class Engine; }

namespace mosaic::tap
{
using Floats = std::span<const float>;         // a row of numbers; empty() when it is not available

// The centre of weight of a row of band strengths, in Hz (0 for an empty or silent row). One brightness measure that
// library pieces, target pieces and live-input pieces all have: bandCentroidHz (lib.bands (i), lib.rate()).
inline float bandCentroidHz (Floats bands, double rate) noexcept
{
    double weighted = 0, sum = 0;
    for (size_t n = 0; n < bands.size(); ++n) { weighted += (double) bands[n] * ((double) n + 0.5); sum += (double) bands[n]; }
    return sum > 0 ? (float) (weighted / sum / (double) kBands * rate * 0.5) : 0.0f;
}

// ------------------------------------------------------------------------------------ the settings in force
// The engine's own values at this moment (after TERRAIN moved them), under the screen's names.
struct Settings
{
    // search
    int style = 0;                     // SEARCH STYLE in use: 0 Closest, 1 Most different, 2 Network, 3 Network + wait
    bool styleSetByTerrain = false;    // TERRAIN switched Closest to Network for now
    float toneTimbre = 0.5f;           // TONE / TIMBRE, 0..1: 0 = the 100 bands only, 1 = the 12 timbre numbers only
    float shapePitchOnly = 0;          // SHAPE / PITCH ONLY, 0..1: 0 = plain measurements, 1 = flattened only
    int lowestBand = 0, highestBand = 100;   // the bands compared: lowestBand <= band < highestBand
    float novelty = 0;                 // NOVELTY, 0..1: how much the "used" memory counts in a score
    float boredom = 0.9f;              // BOREDOM, 0..1: the "used" memory is multiplied by this at every pick
    float smooth = 0;                  // SMOOTH (stickyness), 0..1
    int repeat = 1;                    // REPEAT: searches per target piece, 1..50
    int connections = 20;              // CONNECTIONS: neighbours a Network search looks at
    float waitUntilClose = 1000;       // WAIT UNTIL CLOSE: the score a match must be under before the target moves on (style 3)
    // mix
    float autotune = 0;                // AUTOTUNE, 0..1
    float flatten = 0;                 // FLATTEN, 0..1: share of each piece's level-flattened copy
    float originalMix = 0;             // ORIGINAL MIX, 0..1: share of the target's own sound
    float volume = 1;                  // VOLUME as the engine multiplies it (the dial x 10: 0..9.9)
    float dry = 0;                     // DRY, 0..1 (the effect only)
    bool stereo = false;               // STEREO: separate searches left and right. Off: only channel 0 picks, the right output is a copy
    float pieceGain = 0.2f;            // one library sample reaches the mix multiplied by this: (1 - originalMix) x 0.2 x volume
    float originalGain = 0;            // one target sample by this: originalMix x 0.2 x volume
    // TERRAIN
    bool terrainWeights = false;       // a weight per band is in force (Model::terrainWeights())
    bool terrainFactor = false;        // a brightness preference scales every piece's score
    float terrainTilt = 0;             // that preference: -1 darker .. 1 brighter (0 = none)
    bool terrainMovesNovelty = false;  // NOVELTY and BOREDOM change along the target
    bool terrainMask = false;          // TERRAIN keeps some sounds out of play (LibraryView::soundOn)
};

// ------------------------------------------------------------------------------------ one pick
// The engine picks one library piece per target step (about 32 a second at the default sizes).
struct Pick
{
    uint64_t serial = 0;               // 1, 2, 3 ... in the engine's order. 0 = no pick (an empty slot)
    int channel = 0;                   // 0 left, 1 right
    int targetPiece = 0;               // the piece of the target that was matched
    int libraryPiece = 0;              // the piece of the library that plays
    int previousPiece = -1;            // the piece the library was "at" before this search (-1 = not known)
    int style = 0;                     // the SEARCH STYLE it used (0..3)
    uint32_t startSample = 0;          // when it starts, on its channel's render clock (samples since the last restart)
    double ageSeconds = 0;             // seconds since it started to sound; negative = it starts in that long. New at every tick
    bool sounding = false;             // it is among the voices now
    bool targetMovedOn = true;         // false: the next pick matches the same target piece again
    bool heldByRepeat = false;         // ... because of REPEAT
    bool heldBySlide = false;          // ... because the match was not close enough (WAIT UNTIL CLOSE)
    bool liveTarget = false;           // the target was the live input
    enum Followed : uint8_t { no = 0, yes, notYetKnown };
    Followed playedFollowing = no;     // SMOOTH played the piece after the previous one instead of the search's winner
    uint32_t libraryGen = 0, targetGen = 0;   // which library / target it was picked from
    bool current = true;               // false: its piece numbers belong to an older library or target than the model shows now
    int liveRow = -1;                  // (the model's own handle for a live-input piece: use Model::targetPieceOf)
    Settings settings;                 // the settings this pick was searched with
    Why why;                           // why: see TapWhy.h. why.state tells whether it is ready
};

// ------------------------------------------------------------------------------------ one sounding piece
struct Voice
{
    uint64_t pickSerial = 0;           // the pick that started it (Model::findPick)
    int channel = 0, libraryPiece = 0, targetPiece = 0;
    uint32_t startSample = 0;          // on its channel's render clock
    double ageSeconds = 0;             // negative: found, about to start
    int length = 0;                    // samples in the library piece
    float position = 0;                // where the engine is reading inside the piece, in samples, 0..length (an estimate between meter frames)
    float progress = 0;                // 0..1 through its life
    float fade = 1;                    // the piece's own fade at `position`, 0..1 (1 when the fade shape is not known)
    float pitchRatio = 1;              // playback speed: AUTOTUNE's ratio x the MIDI key's (1 = as recorded)
    bool pitchKnown = true;            // false: AUTOTUNE is on and the target's pitch is not known (a live target not captured)
    float pieceGain = 0, originalGain = 0;   // the mix gains in force (as Settings)
};

// ------------------------------------------------------------------------------------ transport, meters, clocks
struct Transport
{
    bool playing = false;              // the engine is turning picks into sound right now
    bool wanted = false;               // playing is asked for (the button, a key, or the DAW)
    const char* whyNot = "";           // when not playing: the reason in plain words ("" while playing; never null)
    int playMode = 0;                  // PLAY BY: 0 the play button, 1 MIDI keys, 2 DAW play
    int notesDown = 0, lastNote = -1;  // MIDI
    float velocity = 1;                // of the last key, 0..1
    bool hostPlaying = false, hostReports = false;   // the DAW's transport, and whether it tells us
    double bpm = 120, ppq = 0;         // the DAW's tempo and position in quarter notes (as last heard)
    bool recording = false, fadingOut = false, waitingForLiveInput = false;
};

struct Meters
{
    float outputPeak = 0;              // peak of the finished output over the last meter frame (1 = full scale)
    float outputPeakHold = 0;          // the same, falling back slowly: for a meter needle
    float limiterGain = 1;             // SAFETY LIMITER: the lowest gain it applied in the last frame (1 = untouched)
    bool limiterOn = false;
    float fade = 1;                    // SOFT START/STOP's gain now, 0..1
    float velocityGain = 1;            // the MIDI key's gain now
    float outMin[2] = { 0, 0 }, outMax[2] = { 0, 0 };   // the shape of the finished output over the last frame: lowest / highest sample, left / right
    bool shapeValid = false;
    int engineVoices[2] = { 0, 0 };    // pieces in the engine's own list, left / right
    uint32_t droppedPieces = 0;        // pieces the engine could not start because its list was full
    double frameSeconds = 0;           // how much audio the last meter frame covered (about 1/120 s, or one host buffer)
};

struct Playhead
{
    bool valid = false;                // false until the engine has been heard from
    double targetPiece = 0;            // where the engine is in the target, in pieces (not whole: it moves between picks)
    double targetSeconds = 0;          // the same in seconds
    uint32_t renderSample = 0;         // the channel's render clock: samples since the last restart
    double renderSeconds = 0;
    uint32_t restarts = 0;             // restarts seen since the model was switched on
    int lastRestart = 0;               // 0 none yet; 1 from outside (play from the top, a new library, a setting); 2 the target ran out or there was nothing to play
};

struct Counters
{
    bool attached = false;             // the model is reading the tap
    bool fullRead = true;              // false: the fallback build (TapPeek.h): no live-target measurements, no fade shape, the first pick after a restart not fully explained
    const char* note = "Switched off."; // plain words about the model itself (never null)
    uint64_t cellsRead = 0;            // since switched on
    uint64_t cellsLost = 0;            // dropped because the queue was full (the screen fell behind). Marked, never guessed
    uint64_t picksSeen = 0, picksExplained = 0, picksSkipped = 0, picksUnavailable = 0;
    int picksWaiting = 0;              // seen, not explained yet
    int explainingDone = 0, explainingOf = 0;   // of the picks in the ring: "explaining n of m"
    double explainMs = 0;              // how long explaining one pick takes lately, on the model's own thread (it grows with the library)
    bool usedValid = false;            // the "used" memory is the engine's own, to the bit
    int usedChecked = 0;               // pieces of it confirmed so far (of library().pieces())
    uint32_t parkedBuffers = 0;        // audio buffers the engine spent silent, waiting for a library job
    double audioSeconds = 0;           // audio processed since switched on
    double modelSeconds = 0;           // the sum of tick()'s dt since switched on
    double secondsSinceEngine = 0;     // since the audio thread was last heard from (large = the host is not running the audio)
    double lastTickMs = 0, worstTickMs = 0;   // how long tick() itself took
};

// The target piece a pick was matched to, wherever it came from: a piece of the target file, or (the effect) the
// piece of live input it was matched to. Model::targetPieceOf (pick) gives it.
struct TargetPiece
{
    bool valid = false;                // false: see note (the rows are then empty)
    const char* note = "Not available for this pick.";   // plain words, never null
    bool live = false;                 // it was a piece of the live input
    Floats bands, timbre, flatBands, flatTimbre;         // as LibraryView's rows
    float pitchAsEngineHolds = 0;      // the engine's dominant frequency of the piece (it assumes 44100 Hz)
    bool finite = true;                // false: some numbers were not numbers (silence has no flattened measurements); shown as 0
};

// ------------------------------------------------------------------------------------ the library
// Every accessor is safe with any index: out of range gives 0 / empty / -1.
class LibraryView
{
public:
    bool ok() const noexcept { return s != nullptr && s->numBlocks > 0; }
    const char* note() const noexcept { return why; }                       // plain words when ! ok() (never null)
    uint32_t generation() const noexcept { return s != nullptr ? s->gen : 0; }   // changes when a new library arrives
    int pieces() const noexcept { return ok() ? s->numBlocks : 0; }
    int sounds() const noexcept { return ok() ? (int) s->sounds.size() : 0; }
    int pieceLength() const noexcept { return ok() ? s->pcmLen : 0; }       // samples of audio in every piece
    int hop() const noexcept { return ok() ? s->hop : 0; }                  // samples from one piece's start to the next, inside a sound
    double rate() const noexcept { return ok() ? s->rate : 0; }             // Hz
    double pieceSeconds() const noexcept { return rate() > 0 ? pieceLength() / rate() : 0; }
    int bandCount() const noexcept { return ok() ? s->bandCount : 0; }      // 100 (fewer only when pieces are shorter than 100 samples)
    static constexpr int timbreCount = kTimbre;                             // 12
    bool hasMeasurements() const noexcept { return ok() && s->hasMeasurements; }   // false: left out (a huge library and the byte budget)
    bool hasFlattened() const noexcept { return ok() && s->hasFlattened; }
    bool valid (int piece) const noexcept { return ok() && piece >= 0 && piece < s->numBlocks; }

    // ---- per piece
    int soundOf (int piece) const noexcept { return valid (piece) && s->sound[(size_t) piece] != 0xffff ? (int) s->sound[(size_t) piece] : -1; }
    Floats bands (int piece) const noexcept { return row (piece, s != nullptr ? &s->rows.bands : nullptr, bandCount()); }            // energy per band, low to high
    Floats flatBands (int piece) const noexcept { return row (piece, s != nullptr ? &s->rows.flatBands : nullptr, bandCount()); }    // the same of the level-flattened piece
    Floats timbre (int piece) const noexcept { return row (piece, s != nullptr ? &s->rows.timbre : nullptr, kTimbre); }              // the 12 timbre numbers (MFCC)
    Floats flatTimbre (int piece) const noexcept { return row (piece, s != nullptr ? &s->rows.flatTimbre : nullptr, kTimbre); }
    bool silent (int piece) const noexcept { return valid (piece) && (s->rows.flags[(size_t) piece] & rowFlag::silent) != 0; }
    bool measurementsFinite (int piece) const noexcept { return valid (piece) && (s->rows.flags[(size_t) piece] & 15) == 0; }   // false: some were not numbers (shown as 0)
    float loudness (int piece) const noexcept { return valid (piece) ? s->rows.rms[(size_t) piece] : 0.0f; }     // RMS of the faded piece (linear)
    float peak (int piece) const noexcept { return valid (piece) ? s->rows.peak[(size_t) piece] : 0.0f; }        // its highest |sample|
    float brightness (int piece) const noexcept { return valid (piece) && s->bright.size() == (size_t) s->numBlocks ? s->bright[(size_t) piece] : 0.0f; }       // 0 darkest .. 1 brightest of this library (a rank)
    float brightnessHz (int piece) const noexcept { return valid (piece) && s->centroidHz.size() == (size_t) s->numBlocks ? s->centroidHz[(size_t) piece] : 0.0f; }   // its spectral centroid
    float pitchAsEngineHolds (int piece) const noexcept { return valid (piece) ? s->rows.freq[(size_t) piece] : 0.0f; }   // the engine's dominant frequency (it assumes 44100 Hz)
    float pitchHz (int piece) const noexcept { return (float) (pitchAsEngineHolds (piece) * rate() / 44100.0); }           // the same in true Hz
    static constexpr int envelopeSlices = kEnvSlices;                       // 32
    // the piece's outline: lowest and highest sample of slice 0..31, as fractions of its own peak (-1..1)
    void envelope (int piece, int slice, float& lowest, float& highest) const noexcept
    {
        lowest = highest = 0.0f;
        if (! valid (piece) || slice < 0 || slice >= kEnvSlices) return;
        const int8_t* e = s->envOf (piece);
        lowest = (float) e[2 * slice] / 127.0f; highest = (float) e[2 * slice + 1] / 127.0f;
    }
    double secondsInSound (int piece) const noexcept          // where the piece starts inside its own sound
    {
        const int k = soundOf (piece);
        return k >= 0 && rate() > 0 ? (double) (piece - s->sounds[(size_t) k].first) * hop() / rate() : 0.0;
    }

    // ---- per sound (0 .. sounds()-1, in the engine's order)
    bool validSound (int k) const noexcept { return ok() && k >= 0 && k < (int) s->sounds.size(); }
    juce::String soundName (int k) const { return validSound (k) ? s->sounds[(size_t) k].name : juce::String(); }
    int soundFirst (int k) const noexcept { return validSound (k) ? s->sounds[(size_t) k].first : -1; }     // its first piece
    int soundLast (int k) const noexcept { return validSound (k) ? s->sounds[(size_t) k].last : -1; }       // its last piece (never a search's winner: the engine stops one short)
    int soundPieces (int k) const noexcept { return validSound (k) ? juce::jmax (0, s->sounds[(size_t) k].last - s->sounds[(size_t) k].first + 1) : 0; }
    double soundSeconds (int k) const noexcept { return validSound (k) && rate() > 0 ? (double) s->sounds[(size_t) k].lengthSamples / rate() : 0.0; }
    bool soundOn (int k) const noexcept { return validSound (k) && state != nullptr && (size_t) k < state->size() ? ((*state)[(size_t) k] & 1) != 0 : validSound (k) && s->sounds[(size_t) k].enabledAtBuild; }   // the search may use it right now (your switch AND TERRAIN's pick)
    bool soundRemoved (int k) const noexcept { return validSound (k) && state != nullptr && (size_t) k < state->size() && ((*state)[(size_t) k] & 2) != 0; }   // deleted since the library was built

    // ---- the network: each piece's nearest pieces, nearest first
    int connections (int piece) const noexcept { return valid (piece) ? (int) s->synCount[(size_t) piece] : 0; }    // how many the engine holds (up to 1000)
    std::span<const uint32_t> nearest (int piece) const noexcept                                                    // may be fewer than connections() (byte budget)
    {
        if (! valid (piece) || s->synK <= 0) return {};
        return { s->synOf (piece), (size_t) juce::jmin (s->synK, (int) s->synCount[(size_t) piece]) };
    }

    // ---- the fade every piece was given (pieceLength() gains, 0..1); empty when not known
    Floats fade() const noexcept { return ok() ? Floats (s->window) : Floats(); }
    int fadeType() const noexcept { return ok() ? s->windowType : -1; }      // PIECE FADE SHAPE 0..7 (-1 = not known)

    const BrainSnapshot* snapshot() const noexcept { return s; }             // (the raw rows, for code that wants them all at once)

private:
    friend class Model;
    Floats row (int piece, const std::vector<float>* v, int n) const noexcept
    {
        if (! valid (piece) || v == nullptr || n <= 0 || v->size() < (size_t) s->numBlocks * (size_t) n) return {};
        return { v->data() + (size_t) piece * (size_t) n, (size_t) n };
    }
    const BrainSnapshot* s = nullptr;
    const std::vector<uint8_t>* state = nullptr;
    const char* why = "No library yet.";
};

// ------------------------------------------------------------------------------------ the target
// A target file is cut into pieces like the library; `channel` is 0 left, 1 right (a mono file: the same twice).
// A live target (the effect's input) has no pieces to look ahead at: ok() is false and live() is true.
class TargetView
{
public:
    bool ok() const noexcept { return s != nullptr && s->numBlocks > 0 && ! isLive; }
    bool live() const noexcept { return isLive; }
    const char* note() const noexcept { return why; }                       // plain words when ! ok() (never null)
    uint32_t generation() const noexcept { return ok() ? s->gen : 0; }
    juce::String name() const { return ok() ? s->name : juce::String(); }
    int pieces() const noexcept { return ok() ? s->numBlocks : 0; }
    int pieceLength() const noexcept { return ok() ? s->pcmLen : 0; }
    int hop() const noexcept { return hopNow; }                             // samples the target moves on per pick (also set for a live target)
    double rate() const noexcept { return rateNow; }
    double seconds() const noexcept { return ok() && rateNow > 0 ? (double) s->numBlocks * hopNow / rateNow : 0.0; }
    double secondsOf (double piece) const noexcept { return rateNow > 0 ? piece * hopNow / rateNow : 0.0; }   // where a piece starts in the target
    int bandCount() const noexcept { return ok() ? s->bandCount : 0; }
    bool hasMeasurements() const noexcept { return ok() && s->hasMeasurements; }
    bool hasFlattened() const noexcept { return ok() && s->hasFlattened; }
    bool valid (int channel, int piece) const noexcept { return ok() && (channel == 0 || channel == 1) && piece >= 0 && piece < s->numBlocks; }

    Floats bands (int channel, int piece) const noexcept { return row (channel, piece, 0, bandCount()); }
    Floats flatBands (int channel, int piece) const noexcept { return row (channel, piece, 1, bandCount()); }
    Floats timbre (int channel, int piece) const noexcept { return row (channel, piece, 2, kTimbre); }
    Floats flatTimbre (int channel, int piece) const noexcept { return row (channel, piece, 3, kTimbre); }
    bool silent (int channel, int piece) const noexcept { return valid (channel, piece) && (s->rows[channel].flags[(size_t) piece] & rowFlag::silent) != 0; }
    bool measurementsFinite (int channel, int piece) const noexcept { return valid (channel, piece) && (s->rows[channel].flags[(size_t) piece] & 15) == 0; }
    float loudness (int channel, int piece) const noexcept { return valid (channel, piece) ? s->rows[channel].rms[(size_t) piece] : 0.0f; }
    float peak (int channel, int piece) const noexcept { return valid (channel, piece) ? s->rows[channel].peak[(size_t) piece] : 0.0f; }
    float pitchAsEngineHolds (int channel, int piece) const noexcept { return valid (channel, piece) ? s->rows[channel].freq[(size_t) piece] : 0.0f; }
    float pitchHz (int channel, int piece) const noexcept { return (float) (pitchAsEngineHolds (channel, piece) * rateNow / 44100.0); }
    void envelope (int channel, int piece, int slice, float& lowest, float& highest) const noexcept
    {
        lowest = highest = 0.0f;
        if (! valid (channel, piece) || slice < 0 || slice >= kEnvSlices) return;
        const int8_t* e = s->envOf (channel, piece);
        lowest = (float) e[2 * slice] / 127.0f; highest = (float) e[2 * slice + 1] / 127.0f;
    }
    Floats fade() const noexcept { return ok() ? Floats (s->window) : Floats(); }
    int fadeType() const noexcept { return ok() ? s->windowType : -1; }
    const TargetSnapshot* snapshot() const noexcept { return ok() ? s : nullptr; }

private:
    friend class Model;
    Floats row (int channel, int piece, int which, int n) const noexcept
    {
        if (! valid (channel, piece) || n <= 0) return {};
        const Rows& r = s->rows[channel];
        const std::vector<float>& v = which == 0 ? r.bands : (which == 1 ? r.flatBands : (which == 2 ? r.timbre : r.flatTimbre));
        if (v.size() < (size_t) s->numBlocks * (size_t) n) return {};
        return { v.data() + (size_t) piece * (size_t) n, (size_t) n };
    }
    const TargetSnapshot* s = nullptr;
    bool isLive = false;
    int hopNow = 0; double rateNow = 0;
    const char* why = "No target yet.";
};

// ------------------------------------------------------------------------------------ the model
class Model
{
public:
    explicit Model (Engine&);                    // message thread
    ~Model();

    void setActive (bool shouldBeOn);            // true: start reading the tap (the page is shown); false: stop (the page is left)
    void tick (double dtSeconds);                // once per frame, before the views draw
    bool active() const noexcept;                // setActive (true) was called and the tap is ours (see counters().note when not)

    // ---- the world
    const LibraryView& library() const noexcept;
    const TargetView& target() const noexcept;

    // ---- the picks: the most recent kMaxPicks, newest first
    static constexpr int kMaxPicks = 1024;
    int numPicks() const noexcept;
    const Pick& pick (int index) const noexcept;             // 0 = the newest ... numPicks() - 1 = the oldest. Any other index: an empty pick (serial 0)
    const Pick* findPick (uint64_t serial) const noexcept;   // nullptr once it has left the ring
    const Pick* newestPick (int channel) const noexcept;     // nullptr when that channel has none
    TargetPiece targetPieceOf (const Pick&) const noexcept;  // the target piece it was matched to: from the file, or the live-input piece that was captured

    // ---- the voices: every piece sounding now, oldest first
    int numVoices() const noexcept;
    const Voice& voice (int index) const noexcept;           // out of range: an empty voice (pickSerial 0)

    // ---- the engine's own "used" memory (NOVELTY's): 1000 is added to a piece when it is picked, and every
    //      piece's value is multiplied by BOREDOM at every pick
    float used (int libraryPiece) const noexcept;            // 0 when out of range or not a number
    float usedUnit (int libraryPiece) const noexcept;        // the same as 0..1 (1 = just picked)
    bool usedValid() const noexcept;                         // false: still being read from the engine (a moment after switching on, or after lost cells)

    // ---- the settings in force, the transport, the meters, the clocks
    const Settings& settings() const noexcept;
    Floats terrainWeights() const noexcept;                  // 100 weights (1 = neutral) while settings().terrainWeights, else empty
    const Transport& transport() const noexcept;
    const Meters& meters() const noexcept;
    const Playhead& playhead (int channel) const noexcept;
    const Counters& counters() const noexcept;

    // ---- a stable 0..1 scale for scores (they have no fixed scale): see ScoreScale in TapWhy.h
    const ScoreScale& scoreScale() const noexcept;

    // ---- tests and offline use
    void explainEveryPick (bool on);             // true: never let a pick go unexplained, however far behind (the default drops the oldest)
    bool waitUntilExplained (int timeoutMs);     // block until the explaining thread has nothing left to do (then tick once to collect)

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    JUCE_DECLARE_NON_COPYABLE (Model)
};
}
