#pragma once
// GHOST PITCH note editing (phase 14, monophonic; GHOST-SLICE-STUDY.md 6).
//
//   PitchCurve   a region's compiled pitch edits: per analysis frame (the pitch track's hop, in SOURCE positions) the shift in
//                cents to apply, the FORMANT offset in cents and the note's own f0 (the formant base). Pieces carry it; the
//                stretch stream reads it every 128 output frames (one output latency ahead, as the stretcher's automation
//                wants), so LIVE and STUDIO follow the same time-varying ratio rho(t) = 2^(delta(t) / 1200).
//   build        the edit equation (6.2): inside note n with detected cents c(t), centre C_n, slow drift d(t) (< 1.5 Hz) and
//                vibrato v(t) (3.5 - 9 Hz):  delta(t) = (T_n - C_n) + (a_d - 1) d(t) + (a_v - 1) v(t); raised-cosine blends of
//                the neighbours' deltas over the TRANSITION length (or a straight GLIDE between the targets); drawn curves on
//                top (OFFSET adds cents; ABSOLUTE replaces the pitch, 20 ms edge blends); unvoiced frames fade to 0 over 10 ms.
//   engine       (phase 15) per frame the share of the VOCAL engine (our TD-PSOLA, Psola.h) against WIDE (the stretcher + the
//                LPC formant correction, FormantFix.h): each note resolves its ENGINE (its own, else the project's; AUTO = VOCAL on a
//                voiced note whose shift stays within +-4 st), every frame takes the nearest note's, 10 ms raised-cosine joins.
//                The vibrato / drift split is complementary: drift = low-pass 1.5 Hz of the deviation, vibrato = low-pass 12 Hz
//                minus the drift (so VIBRATO x0 removes the whole wobble and x2 doubles it), the rest (jitter) stays.
//   notes        what the PITCH tab shows and edits: the detected notes of a region, with the user's note edits in their place
//                (an edit IS a note once it exists: split / merge / timing work on edits).
//   ops          pure edits on the project (one undo step each in the editor).
#include "Model.h"
#include "Analysis.h"
#include <functional>

namespace gslice
{
struct PitchCurve
{
    double hop = 256;                    // SF per frame (frame i sits at SF i * hop)
    double wfPerSf = 1;                  // working (host-rate) frames per source frame
    std::vector<float> cents, formant, baseHz;
    std::vector<float> vocal;            // phase 15: per frame 0..1 = the VOCAL engine's share (empty = WIDE everywhere)
    std::vector<float> srcF0;            // phase 15: the source f0 per frame (Hz, 0 = unvoiced): the epoch marker's guide, the formant fix's harmonics
    uint64 hash = 0;
    bool anyVocal() const noexcept { return ! vocal.empty(); }
    inline int frameAt (double wf) const noexcept { return (int) std::floor (wf / (wfPerSf * hop) + 0.5); }
    inline float vocalAt (double wf) const noexcept
    {
        if (vocal.empty()) return 0.0f;
        const double x = wf / (wfPerSf * hop); const int n = (int) vocal.size();
        if (x <= 0) return vocal[0];
        if (x >= (double) (n - 1)) return vocal[(size_t) n - 1];
        const int i = (int) x; const float k = (float) (x - i);
        return vocal[(size_t) i] + (vocal[(size_t) i + 1] - vocal[(size_t) i]) * k;
    }
    inline float f0At (double wf) const noexcept { const int i = frameAt (wf); return i >= 0 && i < (int) srcF0.size() ? srcF0[(size_t) i] : 0.0f; }
    // exactly no shift and no formant move at wf (the frames around it are 0): the source plays untouched there
    inline bool neutralAt (double wf) const noexcept
    {
        const double x = wf / (wfPerSf * hop); const int n = (int) cents.size();
        if (n == 0 || x < 0 || x > (double) (n - 1)) return true;
        const int i = juce::jmin (n - 2, (int) x);
        if (n == 1) return cents[0] == 0.0f && formant[0] == 0.0f;
        return cents[(size_t) i] == 0.0f && cents[(size_t) i + 1] == 0.0f && formant[(size_t) i] == 0.0f && formant[(size_t) i + 1] == 0.0f;
    }
    // the values at a working-audio position (linear between frames; 0 outside)
    inline void at (double wf, float& c, float& f, float& b) const noexcept
    {
        const double x = wf / (wfPerSf * hop);
        const int n = (int) cents.size();
        if (n == 0 || x < 0 || x > (double) (n - 1)) { c = f = b = 0.0f; return; }
        const int i = juce::jmin (n - 2, (int) x); const float k = (float) (x - i);
        if (n == 1) { c = cents[0]; f = formant[0]; b = baseHz[0]; return; }
        c = cents[(size_t) i] + (cents[(size_t) i + 1] - cents[(size_t) i]) * k;
        f = formant[(size_t) i] + (formant[(size_t) i + 1] - formant[(size_t) i]) * k;
        b = baseHz[(size_t) (k < 0.5f ? i : i + 1)];
    }
    bool activeIn (double wfA, double wfB) const;   // any shift / formant inside [wfA, wfB] (working frames)
};

namespace pitchedit
{
// cents (MIDI x 100) <-> Hz
inline double centsOfHz (double hz) { return 6900.0 + 1200.0 * std::log2 (hz / 440.0); }
inline double hzOfCents (double c) { return 440.0 * std::pow (2.0, (c - 6900.0) / 1200.0); }

// a region's compiled curve (null when it has no audible edit). track may be null (then: constant shifts only, every frame voiced,
// WIDE only). engine: the project's ENGINE; globalCents: the global PITCH on top (AUTO's +-4 st limit counts it)
std::shared_ptr<const PitchCurve> build (const Region& r, const analysis::PitchTrack* track, double wfPerSf, double sourceRate,
                                         PitchEngine engine = PitchEngine::Auto, double globalCents = 0);
// AUTO's limit (cents) and VOCAL's (beyond it the note falls back to WIDE)
constexpr double kAutoVocalCents = 400.0, kMaxVocalCents = 1200.0;
juce::String engineName (int e);   // "AUTO" / "VOCAL" / "WIDE"

// the energy-weighted median pitch of the middle 60 % of a source span (MIDI x 100; 0 = unvoiced)
double centreOf (const analysis::PitchTrack& t, int64 s0, int64 s1);

struct NoteRef
{
    int64 regionId = 0, s0 = 0, s1 = 0;  // SF span
    double centre = 0;                   // detected (MIDI, fractional)
    double target = 0;                   // where it sounds now (MIDI, fractional)
    float confidence = 1;
    int64 editId = 0;                    // 0 = not edited yet
    bool operator== (const NoteRef& o) const = default;
};
// the notes of one region (sorted by s0): the detected ones not covered by an edit + the note edits
std::vector<NoteRef> notesOf (const Region& r, const analysis::Result* a);
std::vector<NoteRef> notesOf (const Project& p, const analysis::Result* a);

// scales: tonic 0..11, minor; chromatic = every semitone
struct Scale { int tonic = 0; bool minor = false; bool chromatic = true; bool operator== (const Scale& o) const = default; };
bool inScale (int midi, const Scale& s);
// the nearest pitch of the scale (MIDI x 100, a whole semitone); ties go in the direction of tieDir (-1 / +1)
double snapToScale (double midiCents, const Scale& s, int tieDir = 1);
juce::String scaleName (const Scale& s);   // "CHROMATIC" / "F# MIN"
}

namespace ops
{
// apply f to each note's edit (making the edit first where the note has none). One region per call.
R editNotes (const Project&, int64 regionId, const std::vector<pitchedit::NoteRef>& notes, const std::function<void (PitchEdit&)>& f);
// move the notes' targets by a semitone / cent delta (snap: the target lands on a whole semitone)
R shiftNotes (const Project&, int64 regionId, const std::vector<pitchedit::NoteRef>& notes, double deltaCents, bool snapSemitone);
// split a note at a source position / merge adjacent notes (>= 2) into one (the first one's target and settings)
R splitNote (const Project&, const pitchedit::NoteRef& n, int64 srcPos, const analysis::PitchTrack* track, juce::String* why = nullptr);
R mergeNotes (const Project&, const std::vector<pitchedit::NoteRef>& notes, const analysis::PitchTrack* track, juce::String* why = nullptr);
// remove the notes' edits (and drawn curves over them): back to the original
R resetNotes (const Project&, const std::vector<pitchedit::NoteRef>& notes);
// SCALE SNAP: every note (minConfidence: notes below it are left alone; the count of snapped notes in *count)
R snapNotes (const Project&, const std::vector<pitchedit::NoteRef>& notes, const pitchedit::Scale& s, float minConfidence = 0.5f, int* count = nullptr);
// PITCH DRAW: a curve over a source span of a region (absolute MIDI x 100 or an offset in cents), one edit
R addDraw (const Project&, int64 regionId, std::vector<std::pair<int64, double>> points, bool absolute, int64* newId = nullptr, juce::String* why = nullptr);
// TIMING: move a note's start / end in output time (PF deltas) with warp markers at its source bounds (FLEX MOVE semantics:
// the neighbours stretch, the region keeps its length). Equal deltas = move the note; only dEnd = stretch it.
R noteTiming (const Project&, const pitchedit::NoteRef& n, double dStartPF, double dEndPF, juce::String* why = nullptr);
}
} // namespace gslice
