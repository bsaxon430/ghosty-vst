#pragma once
// GHOST MAP: the nondestructive project model (GHOST-SLICE-STUDY.md 2.3). The original audio is never changed: a project is
// data (this file, saved as JSON) + a reference to the audio by SHA-256 + delete-safe caches. Every edit is a pure function
// Project -> Project (namespace ops); undo keeps whole snapshots (History).
#include "SliceCore.h"
#include "PlayTypes.h"
#include <memory>
#include <optional>

namespace gslice
{
// A warp marker pins an output time (region-local, PF) to a source position (SF). anchor = ANCHOR LOCK (never moves in output time).
// kind: USER (placed or moved by hand: ALIGN never touches it) or AUTO (made by GHOST ALIGN: re-align regenerates them).
enum class MarkerKind : uint8_t { User = 0, Auto = 1 };
struct WarpMarker
{
    int64 id = 0;
    double t = 0;        // PF, region-local output time
    int64 s = 0;         // SF, source position
    bool anchor = false;
    MarkerKind kind = MarkerKind::User;
    bool isAuto() const { return kind == MarkerKind::Auto; }
    bool operator== (const WarpMarker& o) const = default;
};

// GHOST PITCH (phase 14): a note edit or a drawn pitch curve, attached to a SOURCE span (so it follows the audio through
// TIMING / FLEX moves). Note: the shift (target - detected centre) + the note's own FORMANT / DRIFT / VIBRATO / TRANSITION.
// Draw: a free curve (SF, cents) over its span - ABSOLUTE pitch (MIDI cents) or an OFFSET. Compiled by pitchedit::build into
// a time-varying shift ratio on the render pieces (GHOST-SLICE-STUDY.md 6.2).
enum class PitchEditKind : uint8_t { Note = 0, Draw = 1 };
// GHOST PITCH engine (phase 15): VOCAL = our own TD-PSOLA (formants kept by construction, the edited f0 followed period by
// period, up to +-12 st), WIDE = the Signalsmith stretcher + the LPC formant correction (any shift), AUTO = VOCAL on voiced
// notes within +-4 st, WIDE beyond. LIVE always previews with the stretcher.
enum class PitchEngine : uint8_t { Auto = 0, Vocal = 1, Wide = 2 };
struct PitchEdit
{
    int64 id = 0; int64 srcStart = 0, srcEnd = 0; double cents = 0, formantCents = 0;
    PitchEditKind kind = PitchEditKind::Note;
    double centre = 0;            // Note: the detected centre when the edit was made (MIDI x 100; 0 = unknown)
    double drift = 1.0;           // DRIFT scale a_d (0 = flatten the slow wander, 1 = as sung, 2 = exaggerate)
    double vibrato = 1.0;         // VIBRATO scale a_v (0 = flatten, 2 = double)
    double transitionMs = 40;     // TRANSITION length into this note (0 .. 250 ms)
    bool glide = false;           // TRANSITION curve: false = blend (the sung glide shape stays), true = a straight line between the targets
    bool absolute = false;        // Draw: points are absolute pitch (MIDI x 100) instead of an offset in cents
    int engine = -1;              // Note: -1 = the project's ENGINE, else a PitchEngine (phase 15)
    std::vector<std::pair<int64, double>> curve;   // Draw: (SF, cents), sorted by SF
    bool isNote() const { return kind == PitchEditKind::Note; }
    double target() const { return centre + cents; }   // Note: where it sounds (MIDI x 100)
    bool neutral() const { return isNote() && cents == 0 && formantCents == 0 && drift == 1.0 && vibrato == 1.0; }
    bool operator== (const PitchEdit& o) const = default;
};

struct Region
{
    int64 id = 0;
    double dstStart = 0;                  // PF, where it sits on the output timeline
    std::vector<WarpMarker> warp;         // >= 2, t strictly increasing from 0, s strictly increasing; ends = source in / out
    double gainDb = 0;
    double fadeInMs = 0, fadeOutMs = 0;
    bool reverse = false;
    bool muted = false;
    std::vector<PitchEdit> pitch;         // GHOST PITCH note edits + drawn curves (phase 14)
    double pitchSemis = 0, pitchCents = 0, formantSemis = 0;   // the region's own PITCH / CENTS / FORMANT (on top of the global ones)
    bool operator== (const Region& o) const = default;
    double totalPitchSemis() const { return pitchSemis + pitchCents / 100.0; }

    int64 srcIn() const { return warp.front().s; }
    int64 srcOut() const { return warp.back().s; }
    double length() const { return warp.back().t; }        // PF
    double dstEnd() const { return dstStart + length(); }
    WarpMap map() const { WarpMap m; for (auto& w : warp) { m.t.push_back (w.t); m.s.push_back ((double) w.s); } return m; }
    bool valid() const;
    // GHOST HOLD (phase 12): held warp segments (rendered by a HOLD mode; exempt from the stretch limits)
    std::vector<HoldOp> holds;
    const HoldOp* holdOfSegment (size_t i) const   // the hold whose segment is warp[i] .. warp[i + 1] (or null)
    {
        if (i + 1 >= warp.size()) return nullptr;
        for (auto& h : holds) if (h.markerA == warp[i].id && h.markerB == warp[i + 1].id) return &h;
        return nullptr;
    }
};

struct SourceRef
{
    juce::String fingerprint;   // SHA-256 of the file bytes
    juce::String path;          // absolute path when last saved
    juce::String relPath;       // relative to the project folder (when saved in one)
    juce::String fileName;
    double sampleRate = 0;
    int64 frames = 0;
    int channels = 0;
    bool operator== (const SourceRef& o) const = default;
};

struct Project
{
    int formatVersion = kFormatVersion;
    double sampleRate = 48000;            // the PF rate (= the source's rate when the project was made)
    SourceRef source;
    std::vector<Region> regions;          // the arrangement order: later = on top (top wins where regions overlap)
    double bpm = 120;
    int gridDiv = 16;                     // 0 = snap off, else 4 / 8 / 16 / 32 / 64 (1/N notes)
    bool gridTriplet = false;
    double beatOffset = 0;                // PF of the first beat (the grid's phase)
    double pitchSemis = 0, pitchCents = 0, formantSemis = 0;   // global PITCH / FORMANT (phase 4 groundwork for phase 9)
    bool transientLock = true;            // hold rate 1.0 for ~20 ms after strong onsets inside stretched segments
    juce::String key = "--";              // the source's key chosen by hand ("--" = use the detected one), e.g. "F# MIN"
    juce::String projectKey = "--";       // the project / song key MATCH PROJECT KEY moves the source to ("--" = none)
    juce::String material = "AUTO";       // analysis material profile: AUTO / DRUMS / VOCAL / MELODIC
    double tonalityHz = 8000;             // global PITCH: above this the stretcher's transposition is not tonal (Signalsmith tonalityLimit)
    int pitchEngine = 0;                  // GHOST PITCH: the note edits' engine (PitchEngine; a note may choose its own)
    int keyMode = 0;                      // GHOST KEY: what MATCH PROJECT KEY does (keyops::Mode: CHROMATIC / SCALE SNAP / MELODY PRESERVE / MANUAL / USE CHORDS)
    int64 nextId = 1;
    juce::var extra;                      // unknown JSON fields, kept on save (forward compatibility)
    bool operator== (const Project& o) const;

    const Region* find (int64 id) const { for (auto& r : regions) if (r.id == id) return &r; return nullptr; }
    Region* find (int64 id) { for (auto& r : regions) if (r.id == id) return &r; return nullptr; }
    int indexOf (int64 id) const { for (int i = 0; i < (int) regions.size(); ++i) if (regions[(size_t) i].id == id) return i; return -1; }
    double length() const { double e = 0; for (auto& r : regions) e = std::max (e, r.dstEnd()); return e; }
    double secondsPerBeat() const { return 60.0 / juce::jlimit (20.0, 999.0, bpm); }
    double gridStep() const;              // PF between grid lines (0 = off)
    double snap (double pf, double strength = 1.0) const;   // strength 0..1 toward the nearest grid line
    double totalPitchSemis() const { return pitchSemis + pitchCents / 100.0; }
    bool hasSource() const { return source.fingerprint.isNotEmpty() && source.frames > 0; }
    // GHOST REPEAT spans (phase 11) and the PLAY key map (phase 10)
    std::vector<RepeatOp> repeats;
    PlayMap play;
    const RepeatOp* findRepeat (int64 rid) const { for (auto& r : repeats) if (r.id == rid) return &r; return nullptr; }
};

// the default project for a freshly loaded source: one region over the whole file at 1x
Project makeProject (const SourceRef& src);

// ---------------------------------------------------------------- JSON (project.json)
namespace json
{
    juce::var toVar (const Project& p);
    juce::String toString (const Project& p);
    // false (and a reason) when it is not a Ghost Slice project; unknown fields are kept in Project::extra
    bool fromVar (const juce::var& v, Project& out, juce::String& error);
    bool fromString (const juce::String& s, Project& out, juce::String& error);
    juce::String fingerprint (const Project& p);   // SHA-256 of the canonical JSON (undo / dirty checks)
}

// ---------------------------------------------------------------- edits (pure functions; nullopt = refused, reason in why)
enum class FlexMode { Move, Stretch, Slip };
namespace ops
{
    using R = std::optional<Project>;
    // EDIT
    R splitAt (const Project&, int64 regionId, double dstPos, juce::String* why = nullptr, int64* newRightId = nullptr);   // CUT at the cursor
    R splitRange (const Project&, double a, double b, juce::String* why = nullptr);       // CUT a selection: split at both edges
    R splitAtMarkers (const Project&, int64 regionId, juce::String* why = nullptr);
    R deleteRegion (const Project&, int64 regionId);
    R deleteRange (const Project&, double a, double b, bool ripple);                      // silence (or remove + close the gap)
    R duplicate (const Project&, int64 regionId, int64* newId = nullptr);                 // a copy right after it, on top
    R setReverse (const Project&, int64 regionId, bool on);
    R setGain (const Project&, int64 regionId, double db);
    R setRegionPitch (const Project&, int64 regionId, double semis, double cents, double formantSemis);   // the region's own PITCH / FORMANT
    R setFades (const Project&, int64 regionId, double inMs, double outMs);
    R moveRegion (const Project&, int64 regionId, double newDstStart, juce::String* why = nullptr);
    R rearrange (const Project&, int from, int to);                                        // lane strip: block `from` (output order) to slot `to`
    R trim (const Project&, int64 regionId, bool startEdge, int64 newSrc, juce::String* why = nullptr);   // edge trim at 1x of the edge segment
    // STRETCH (phase 4): scale the region's output length (all markers' t)
    R stretchTo (const Project&, int64 regionId, double newLength, juce::String* why = nullptr);
    // FLEX (phase 5 / 6)
    R addMarker (const Project&, int64 regionId, double localT, int64* newId = nullptr, juce::String* why = nullptr);
    R removeMarker (const Project&, int64 regionId, int64 markerId, juce::String* why = nullptr);
    R dragMarker (const Project&, int64 regionId, int64 markerId, double newT, FlexMode mode, juce::String* why = nullptr);
    R slip (const Project&, int64 regionId, int64 deltaSrc, juce::String* why = nullptr);
    R setAnchor (const Project&, int64 regionId, int64 markerId, bool on);
    R setTempo (const Project&, double bpm);
    // tap the BPM from two markers (absolute output times a < b, PF): the beat count between them that lands in 70..180 BPM
    double bpmFromSpan (double spanSeconds, int* beatsOut = nullptr);
    // all output-time positions of anchored markers (absolute PF) - an edit must keep them
    std::vector<double> anchorTimes (const Project&);
    bool anchorsKept (const Project& before, const Project& after);
}

// ---------------------------------------------------------------- undo / redo as snapshots
class History
{
public:
    explicit History (size_t limit = 200) : maxSteps (limit) {}
    void reset (const Project& p) { cur = std::make_shared<const Project> (p); undoStack.clear(); redoStack.clear(); gesture = false; }
    const Project& current() const { return *cur; }
    std::shared_ptr<const Project> currentPtr() const { return cur; }
    // a new state. inGesture: a drag in progress (merged into one undo step until endGesture)
    void push (const Project& p, const juce::String& label);
    void beginGesture() { gestureOpen = true; gesture = false; }
    void endGesture() { gestureOpen = false; gesture = false; }
    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }
    bool undo();
    bool redo();
    juce::String undoLabel() const { return undoStack.empty() ? juce::String() : undoStack.back().second; }
    juce::String redoLabel() const { return redoStack.empty() ? juce::String() : redoStack.back().second; }
    size_t undoCount() const { return undoStack.size(); }
private:
    using Snap = std::pair<std::shared_ptr<const Project>, juce::String>;
    std::shared_ptr<const Project> cur = std::make_shared<const Project>();
    std::vector<Snap> undoStack, redoStack;
    size_t maxSteps; bool gestureOpen = false, gesture = false;
};
} // namespace gslice
