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
#include "RenderPlan.h"
#include "SourceAudio.h"
#include <functional>

namespace gslice::analysis { struct Syllable; }
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

// ELASTIC: how many frames before u0 (region-local) the stretcher starts and runs at 1x over the head before its output is used,
// so at the join it already reproduces the source (no seek transient against the crossfade). 0 = none (not the join piece)
int64 elasticPreroll (const HoldPlan&, int64 u0) noexcept;
// ELASTIC out join (STUDIO). The stretched body meets the tail with whatever phase the stretcher ended on, and the crossfade bends
// the pitch by the phase difference over its length. The engine renders the body with a few phase steers (an extra transposition
// shaped as a raised-cosine bump of a few cents over the body: its integral moves the output's phase) and keeps the one whose
// join measures the most continuous pitch.
struct Steer { int64 a = 0, b = 0; double f0 = 0; };
constexpr double kSteerCandidates[] { 0.0, -1.5, 1.5, -3.0, 3.0, -0.75, 0.75, -4.5, 4.5 };   // cents (peak of the bump: the held body moves by half of it on average)
constexpr double kSteerGoodCents = 2.5;                       // a join this continuous ends the search
// the span of the body the bump covers (region-local output frames, clear of the ramps / plateau / crossfade); false when the
// piece is not the whole held segment, it has no out join or no pitch
bool steerSpan (const HoldPlan&, int64 u0, int64 len, Steer& out) noexcept;
// the largest pitch deviation (cents, 3-frame medians on 5 ms frames) of the out join, against the source at the mapped position,
// of the rendered held piece L / R (len frames, BEFORE applyJoins: the join crossfade is applied to a copy here). 1e9 = unmeasurable
double joinDrift (const HoldPlan&, const WorkingAudio&, const float* L, const float* R, int64 len);
// SUSTAIN grains (stateless; v = the segment-local output frame). Writes (does not add).
void renderGrains (const HoldPlan&, const WorkingAudio&, int64 v, float* L, float* R, int n) noexcept;
// the source crossfades at the joins (ELASTIC / TEXTURE / FREEZE; nothing for SUSTAIN)
void applyJoins (const HoldPlan&, const WorkingAudio&, int64 v, float* L, float* R, int n) noexcept;
// TEXTURE / FREEZE: the whole held segment (2 x len), joins included. false = cancelled
bool renderSpectral (const HoldPlan&, const WorkingAudio&, juce::AudioBuffer<float>& out, const std::function<bool()>& cancel = {});
// the whole segment in its mode, STUDIO quality (for tests / the cache): SUSTAIN grains, TEXTURE / FREEZE spectral; ELASTIC is the
// stretcher's (render::piece does that one)
}

namespace gslice::ops
{
// HOLD the stable region [stA, stB] (SF) of the selection [selA, selB] (SF) of a region: four markers (selA, stA, stB, selB), the
// head and tail at 1x, the stable segment held for heldLen (PF). Refused on a reversed region / markers inside the selection.
R addHold (const Project&, int64 regionId, int64 selA, int64 selB, int64 stA, int64 stB, double heldLen, HoldMode mode, int64* newId = nullptr, juce::String* why = nullptr);
R setHoldLength (const Project&, int64 regionId, int64 holdId, double heldLen, juce::String* why = nullptr);   // the right-edge drag
R setHoldStable (const Project&, int64 regionId, int64 holdId, int64 stA, int64 stB, juce::String* why = nullptr);   // the manual handles
R setHold (const Project&, int64 regionId, const HoldOp& h);                    // mode / drift / variation / smear
R removeHold (const Project&, int64 regionId, int64 holdId);                    // back to 1x
// where a hold sits (output PF, absolute): selection, stable segment start / end (held)
struct HoldSpan { double selA = 0, a = 0, b = 0, selB = 0; bool ok = false; };
HoldSpan holdSpan (const Project&, const Region&, const HoldOp&);
}
