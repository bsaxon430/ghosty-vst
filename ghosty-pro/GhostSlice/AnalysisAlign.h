#pragma once
// GHOST ALIGN (phase 8, GHOST-SLICE-STUDY.md 5): detected events -> the grid, as AUTO warp markers. Pure functions on the model:
//   propose()  what would move where (shown as amber ghosts before APPLY) and what is left alone and why
//   apply()    the proposals as a new Project (one undo step): AUTO markers are regenerated, USER markers and anchors never move
// Modes: TIGHT (nearest grid line), GROOVE (the grid + the groove template extracted from the strong onsets, so a swing or a
// push that every bar shares is kept while the random jitter goes), MANUAL (no automatic moves).
// Rules (5.3 / 5.5 / 5.6): only events with strength >= the confidence threshold; breaths stay; an event farther than 40 % of a
// grid step from its target stays (amber "far"); the events of one word / phrase group move together by the lead's offset;
// every resulting segment keeps its stretch rate inside 0.5x .. 2x (corrections shrink by halves up to 8 passes, then the
// event is refused and marked amber).
#include "Model.h"
#include "Analysis.h"

namespace gslice::align
{
enum class Mode { Tight = 0, Groove = 1, Manual = 2 };
const char* modeName (Mode m);

struct Settings
{
    Mode mode = Mode::Tight;
    double strength = 1.0;          // ALIGN STRENGTH 0..1: new = old + strength * (target - old)
    double minConfidence = 0.45;    // weaker events are untouched
    double grooveFeel = 1.0;        // GROOVE: how much of the extracted groove the targets keep (0 = TIGHT)
    bool includeBreaths = false;
    double farFraction = 0.4;       // |target - old| > this x the grid step = probably another grid line: untouched
    double rateMin = limits::kLiveMin, rateMax = limits::kLiveMax;
};

enum class Status { Move, Follow, OnGrid, Weak, Breath, Far, Fixed, RateLimit, Manual, Skipped };
const char* statusName (Status s);

struct Proposal
{
    int64 regionId = 0;
    int64 src = 0;                  // SF, the event
    double from = 0, to = 0;        // absolute output time (PF) now / after
    float strength = 0;
    int group = -1;
    Status status = Status::Move;
    bool moves() const { return status == Status::Move || status == Status::Follow; }
    bool amber() const { return status == Status::Far || status == Status::RateLimit; }
};

struct Groove
{
    int slots = 16;                 // grid steps per bar
    std::vector<double> dev;        // per slot: the mean offset from the grid line, in steps
    std::vector<int> count;
    double swing = 0;               // mean offset of the odd slots - the even ones (steps)
};

struct Proposals
{
    std::vector<Proposal> items;    // by region, then source position
    Settings settings;
    Groove groove;
    int moving = 0, skipped = 0, refused = 0;
    juce::String summary() const;   // "14 move, 3 untouched, 1 refused (rate)"
};

// the step the aligner uses (the project grid, 1/16 when the grid is off)
double alignStep (const Project& p);
Groove extractGroove (const Project& p, const std::vector<analysis::Event>& events, double minConfidence = 0.6);
Proposals propose (const Project& p, const std::vector<analysis::Event>& events, const Settings& s);
std::optional<Project> apply (const Project& p, const Proposals& pr, juce::String* why = nullptr);
Project withoutAuto (const Project& p);   // AUTO markers removed (anchored ones stay: anchors never move)
int countAuto (const Project& p);
} // namespace gslice::align

namespace gslice::keyops
{
// MATCH PROJECT KEY (phase 9, CHROMATIC only for now): the global PITCH (-6..+5 semitones) that moves the source key onto the
// project key (different modes: onto its relative, so the scale notes line up). The source key is Project::key when set by
// hand, else the detected one - which must be confident (>= 60 %, GHOST-SLICE-STUDY.md 3.7).
constexpr double kMinConfidence = 0.6;
std::optional<Project> matchProjectKey (const Project& p, const analysis::Key& detected, juce::String* why = nullptr, int* shiftOut = nullptr);

// GHOST KEY modes (phase 14, GHOST-SLICE-STUDY.md 6.3): what MATCH PROJECT KEY does (Project::keyMode)
//   CHROMATIC        the global PITCH by the smallest step into the project key (onto its relative when the modes differ)
//   SCALE SNAP       every detected note (>= 50 % confidence) snapped to the project key's scale by note edits (no global shift)
//   MELODY PRESERVE  the whole melody moved tonic to tonic (-6 .. +5 st, the same intervals) by the global PITCH, formant base kept
//   MANUAL           nothing automatic: the notes are edited by hand in the PITCH tab
//   USE CHORDS       (phase 17) the global PITCH (-6..+5 st) that puts the most of the detected chords (time x confidence) on the
//                    project key's diatonic chords (root + quality: I ii iii IV V vi vii, minor with the harmonic V); ties: the
//                    smallest shift. For harmony (pads, keys, guitar) where the notes key is weak.
enum class Mode { Chromatic = 0, ScaleSnap = 1, MelodyPreserve = 2, Manual = 3, UseChords = 4 };
constexpr int kModes = 5;
const char* modeName (Mode m);   // CHROMATIC / SCALE SNAP / MELODY PRESERVE / MANUAL / USE CHORDS
// USE CHORDS: the shift and its fit (the share of the chord time on the key's chords, 0..1); false + why when there are no chords
bool chordShift (const analysis::Result* a, int dstTonic, bool dstMinor, int& shift, double& fit, juce::String* why);
// the source key: Project::key when set by hand, else the detected one when confident enough (false + why otherwise)
bool sourceKey (const Project& p, const analysis::Key& detected, int& tonic, bool& minor, juce::String* why);
std::optional<Project> matchKey (const Project& p, const analysis::Result* a, juce::String* why = nullptr, int* shiftOut = nullptr, int* snappedOut = nullptr);
}
