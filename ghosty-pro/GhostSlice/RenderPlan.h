#pragma once
// The render list compiler: Project -> a flat list of render pieces (GHOST-SLICE-STUDY.md 2.5). One pure function; LIVE and
// STUDIO both consume the same plan, so they always agree on timing. Every piece carries fingerprints:
//   liveKey    identity of a LIVE voice (source span + map + direction + where it sits): pitch / gain / fade changes keep the voice
//   studioKey  the STUDIO render cache key (the stream's samples: map, direction, pitch, formant, engine version) - position free
//   fingerprint everything (studioKey + position + gain + fades): equal plans = equal renders
#include "Model.h"
#include "SourceAudio.h"

namespace gslice
{
namespace hold { struct HoldPlan; }
namespace analysis { struct PitchTrack; }
struct PitchCurve;
struct Piece
{
    int64 regionId = 0; int part = 0;
    int64 dst0 = 0, dst1 = 0;           // HF on the output timeline [dst0, dst1)
    int64 u0 = 0;                        // region-local HF of dst0 (a region clipped by one on top starts later)
    int64 regionLen = 0;                 // HF
    WarpMap map;                         // region-local HF -> WF; reversed regions: in mirrored coordinates (see mirror)
    bool reverse = false;
    double mirror = 0;                   // reverse: a stored WF position p reads source index (mirror - 1 - p)
    bool stretch = false;                // needs the stretcher (a rate != 1 somewhere, or a global pitch / formant)
    bool extreme = false;                // a rate outside 0.5x .. 2x (amber)
    int lockedOnsets = 0;                // transient-lock spans inserted
    std::vector<std::pair<double, double>> splices;   // region-local HF spans where the original attack is spliced over the stretched sound (1x, no pitch)
    float gain = 1.0f;
    double tonalityHz = 8000, formantBaseHz = 0;   // (the plan's global PITCH voice settings, copied for the STUDIO worker)
    int64 fadeIn = 0, fadeOut = 0;       // HF, the user's fades (from the region's own start / end)
    int64 edgeIn = 0, edgeOut = 0;       // HF, the short anti-click fades at a discontinuous piece edge (0 = seamless)
    uint64 liveKey = 0, studioKey = 0;
    juce::String fingerprint;
    // GHOST REPEAT (phase 11): a repetition's own pitch (semitones on top of the global PITCH) and its fixed fades (edgeIn / edgeOut
    // are the repeat's attack / release; the edge pass leaves them); repeatIndex = n (-1 = not a repetition)
    double pitchOffset = 0;
    double formantOffset = 0;            // the region's own FORMANT (semitones on top of the global FORMANT)
    bool voiceOwn = false;               // the region's own PITCH / FORMANT set its tonality / formant base (keys include them)
    bool fixedEdges = false;
    int repeatIndex = -1; int64 repeatId = 0;
    // GHOST HOLD (phase 12): a held segment (holdMode = HoldMode + 1; 0 = none), rendered by hold:: instead of the plain stream
    int holdMode = 0;
    std::shared_ptr<const hold::HoldPlan> hold;
    // GHOST PITCH (phase 14): the region's note edits / drawn curves as a time-varying shift (null = none in this piece)
    std::shared_ptr<const PitchCurve> curve;

    int64 length() const { return dst1 - dst0; }
    // the mix envelope at region-local u (identical for LIVE and STUDIO): gain x shape
    inline float shape (int64 u) const noexcept
    {
        float g = 1.0f;
        if (fadeIn > 0 && u < fadeIn) g *= fadeShape ((double) u / (double) fadeIn);
        if (fadeOut > 0 && u >= regionLen - fadeOut) g *= fadeShape ((double) (regionLen - u) / (double) fadeOut);
        if (edgeIn > 0 && u < u0 + edgeIn) g *= fadeShape ((double) (u - u0) / (double) edgeIn);
        if (edgeOut > 0 && u >= u0 + length() - edgeOut) g *= fadeShape ((double) (u0 + length() - u) / (double) edgeOut);
        if (u < u0 || u >= u0 + length()) g = 0.0f;
        return g;
    }
    inline float envelope (int64 u) const noexcept { return gain * shape (u); }
};

struct Plan
{
    std::vector<Piece> pieces;           // sorted by dst0, never overlapping (top wins)
    int64 length = 0;                    // HF
    double hostRate = 48000;
    double pitchSemis = 0, formantSemis = 0;
    double tonalityHz = 8000;            // global PITCH: the stretcher's tonality limit (Project::tonalityHz)
    double formantBaseHz = 0;            // the voice's median f0 for formant analysis (0 = the stretcher guesses); only when pitched
    juce::String fingerprint;
    int extremeCount = 0;
    const Piece* at (int64 dst) const;   // the piece playing at an output frame (or null)
};

struct CompileOptions
{
    double hostRate = 48000;
    double edgeFadeMs = 1.0;             // anti-click fade at discontinuous piece edges
    double lockPreMs = 10.0, lockPostMs = 20.0;  // transient lock span around a strong onset (the original audio is spliced in there)
    float lockMinStrength = 0.35f;
    double formantBaseHz = 0;            // the analysis' median f0 (phase 9): FORMANT 0 stays natural at any PITCH
    double bpm = 0;                      // REPEAT tempo (0 = the project's; the processor passes the host's when it follows the DAW)
    const analysis::PitchTrack* pitchTrack = nullptr;   // GHOST PITCH: the source's f0 track (DRIFT / VIBRATO / GLIDE / DRAW need it)
};

// The pure compile. src may be null (then no transient lock; positions still compile).
Plan compile (const Project& p, const SourceAudio* src, const CompileOptions& o);
// the original, unedited source at 1x from 0 (A/B compare: "B")
Plan compileOriginal (const Project& p, const SourceAudio* src, const CompileOptions& o);
// insert rate-1 spans around strong onsets inside stretched segments (exposed for tests). onsets in the map's s units.
WarpMap transientLock (const WarpMap& m, const std::vector<double>& onsets, double preLen, double postLen, int* locked = nullptr,
                       std::vector<std::pair<double, double>>* spansOut = nullptr);
} // namespace gslice
