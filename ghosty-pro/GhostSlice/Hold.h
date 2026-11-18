#pragma once
// GHOST HOLD (GHOST-SLICE-STUDY.md section 4): hold a tiny section (a vowel, a note) for longer, nondestructively.
//
// A hold is a warp segment of a region (HoldOp: markers A .. B, the auto-picked or hand-placed STABLE region). The selection's
// head (selA .. A) and tail (B .. selB) stay at 1x, so consonants keep their speed; only A .. B is held, for as long as the user
// drags its right edge. The plan compiler turns the segment into one piece with a HoldPlan; the modes:
//   ELASTIC  Signalsmith Stretch along the (extreme) warp segment, crossfaded into the source at both joins (30 ms). The stretcher
//            starts 100 ms early and runs at 1x over the head (it meets the source in phase at the in join), the warp map ends on a
//            1x plateau of up to 120 ms, and STUDIO picks the phase steer of the body whose out join measures the most continuous pitch
//   SUSTAIN  our own pitch-synchronous grain loop (TD-PSOLA style): epochs from the period track, 2-period raised-cosine grains,
//            a seeded constrained random walk through the stable grains (never a sequential loop), output epochs spaced by
//            f_target = f_hold * 2^((vibrato + drift) / 1200): the source vibrato continues (rate re-fitted so its phase meets the
//            tail's), slow drift +-driftCents. The grain chain starts and ends ON the source's own epochs, so the joins are
//            the source itself (seamless, the head / tail bit-identical). Stateless per sample: LIVE == STUDIO, bit for bit.
//   TEXTURE  spectral hold: STFT magnitudes captured over the stable region, interpolated by a slow random walk, phase advance
//            with identity phase locking at peaks + SMEAR randomisation elsewhere (STUDIO render; LIVE previews with SUSTAIN)
//   FREEZE   Paulstretch-style (our own, from the published description): very long (1 - x^2)^1.25 windows, a fresh random phase
//            per bin and frame, the read position wandering slowly through the selection (STUDIO; LIVE previews with SUSTAIN)
// Everything is seeded from the op (deterministic renders); no allocation on the audio thread (the schedule is made by the
// compiler on the message thread).
#include "SourceAudio.h"
#include <functional>

namespace gslice::analysis { struct Syllable; }
namespace gslice
{
enum class HoldMode { Elastic = 0, Sustain = 1, Texture = 2, Freeze = 3 };
// one held segment's settings (GENERATE builds these directly)
struct HoldOp
{
    int64 id = 0;
    HoldMode mode = HoldMode::Sustain;
    double driftCents = 5;          // SUSTAIN: slow random pitch drift (+-)
    double variation = 1.0;         // SUSTAIN: grain walk width (0.25 .. 2)
    double smear = 0.6;             // TEXTURE: phase randomisation (0.05 .. 1)
    uint32_t seed = 1;
};
}
namespace gslice::hold
{
struct Grain { int64 out = 0; double src = 0; float gain = 1.0f; };   // output epoch (HF from the segment start), source centre (WF)

struct HoldPlan
{
    HoldMode mode = HoldMode::Sustain;   // what renders (a pitched project holds ELASTIC: the stretcher pitches it)
    HoldMode chosen = HoldMode::Sustain; // what the user picked
    int64 holdId = 0;
    int64 uStart = 0;                    // region-local HF where the held segment starts
    int64 len = 0;                       // the held segment's output length (HF)
    double sa = 0, sb = 0;               // WF source span of the held (stable) segment
    double selA = 0, selB = 0;           // WF: the selection (FREEZE reads its window here)
    bool joinIn = false, joinOut = false;// the neighbours are the region at 1x: SUSTAIN joins are the source itself, the other modes crossfade
    int64 xf = 0;                        // join crossfade (HF)
    std::vector<Grain> grains;           // SUSTAIN (also the LIVE preview of TEXTURE / FREEZE); empty = not analysable
    bool voiced = false; double f0 = 0, vibRate = 0, vibDepth = 0;
    double smear = 0.6, driftCents = 5; uint32_t seed = 1; double srcRms = 0;
    double rate = 48000;                 // the host rate the plan is for (HF per second)
    uint64 hash = 0;
    bool liveIsExact() const { return mode == HoldMode::Sustain; }   // LIVE renders exactly what STUDIO renders
};

// the auto-picked stable region inside a selection (SF of the source): lowest spectral change + voiced (study 4.2)
struct Stable { int64 a = 0, b = 0; double score = 0; bool unstable = false; bool inVowel = false; };
Stable pickStable (const SourceAudio& src, int64 selA, int64 selB);
// phase 16: with the analysis' syllables, the search stays inside the vowel of the syllable the selection covers most (30 ms clear
// of its consonant / coda edges: keep the consonants, hold the vowel, study 4.1); falls back to the whole selection
Stable pickStable (const SourceAudio& src, int64 selA, int64 selB, const std::vector<analysis::Syllable>* syllables);

// YIN (de Cheveigne & Kawahara 2002, our own implementation) on x[0, n): f0 in Hz (0 = unvoiced) + the aperiodicity at the dip
double yin (const float* x, int n, double sr, double fmin, double fmax, double* aperiodicity = nullptr);
// a mono pitch track of the source over [a, b) SF, one value per hop (0 = unvoiced); frame k is centred at a + k * hop
std::vector<double> pitchTrack (const SourceAudio& src, int64 a, int64 b, int hop, std::vector<double>* aper = nullptr);

// the compiler's call: the plan of one held segment. src may be null (then ELASTIC)
std::shared_ptr<const HoldPlan> makePlan (const SourceAudio* src, double kS, double hostRate, const HoldOp& op, int64 uStart, int64 len,
                                          double sa, double sb, double selA, double selB, bool joinIn, bool joinOut, bool pitched);

// SUSTAIN grains (stateless; v = the segment-local output frame). Writes (does not add).
void renderGrains (const HoldPlan&, const WorkingAudio&, int64 v, float* L, float* R, int n) noexcept;
// the source crossfades at the joins (ELASTIC / TEXTURE / FREEZE; nothing for SUSTAIN)
void applyJoins (const HoldPlan&, const WorkingAudio&, int64 v, float* L, float* R, int n) noexcept;
// TEXTURE / FREEZE: the whole held segment (2 x len), joins included. false = cancelled
bool renderSpectral (const HoldPlan&, const WorkingAudio&, juce::AudioBuffer<float>& out, const std::function<bool()>& cancel = {});
// the whole segment in its mode, STUDIO quality (for tests / the cache): SUSTAIN grains, TEXTURE / FREEZE spectral; ELASTIC is the
// stretcher's (render::piece does that one)
}
