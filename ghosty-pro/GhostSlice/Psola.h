#pragma once
// GHOST PITCH VOCAL engine (phase 15): our own TD-PSOLA for voiced monophonic material (GHOST-SLICE-STUDY.md 4.3 / 6.2).
// Written from the papers, no third-party code:
//   E. Moulines, F. Charpentier, "Pitch-synchronous waveform processing techniques for text-to-speech synthesis using
//   diphones", Speech Communication 9 (1990) 453-467 (TD-PSOLA: pitch-synchronous analysis grains, re-spaced at synthesis);
//   J. Makhoul, "Linear prediction: a tutorial review", Proc. IEEE 63 (1975) (the LPC residual for the epoch refinement);
//   P. A. Naylor et al., "Estimation of glottal closure instants in voiced speech using the DYPSA algorithm", IEEE TASLP 15
//   (2007) (GCIs as the strong residual peaks at a consistent place in each period - our simpler period-synchronous version).
//
//   markEpochs  glottal-closure instants from the f0 track: per voiced run, one positive peak of the zero-phase low-passed
//               signal (1.5 x the run's f0) per period, each searched 0.7 .. 1.3 periods after the last (period-synchronous);
//               then refined to the LPC residual's dominant peak at the run's consistent phase offset (circular mean, +-0.25
//               period search); the epoch = the sub-sample (parabolic) low-pass peak + the median residual offset of 5 marks, so it
//               sits on the closure and is consistent to a fraction of a sample (integer marks jitter the high harmonics).
//   render      the piece's stream: synthesis marks walk the output; voiced marks take the analysis grain whose epoch is nearest
//               the mapped source position (2 periods, asymmetric Hann: rising over the left period, falling over the right), and
//               the next mark follows at right-period / rho(t), rho = 2^((global + curve cents) / 1200) evaluated AT that mark: the
//               edited f0 curve is followed period by period (vibrato x0 / x2, DRIFT, TRANSITION). Entering a voiced run the
//               first mark is put on the output position of a source epoch (phase-continuous). FORMANT != 0 reads the grain
//               time-scaled by 2^(F / 1200) (the envelope moves on purpose); FORMANT 0 keeps it by construction. Unvoiced marks:
//               plain OLA at 1x (Hann, 4 ms hop) along the warp map, the source-vs-output offset of the last voiced grain decays
//               to 0 over 30 ms. The overlap-add is normalised by the window sum (floor 0.5: a -12 st gap stays a gap; Moulines &
//               Charpentier's normalised OLA), then levelled to the source's RMS at the mapped position (20 ms / 5 ms, +-6 dB: grains
//               re-spaced partly out of phase lose up to 3.5 dB). The edited voiced frames then get FormantFix's envelope correction (render::piece:
//               the 2-period window blurs narrow formants by a few %). Where nothing is edited (no shift,
//               no formant, 1x map) the source itself is copied (bit-identical), 5 ms raised-cosine joins 5 ms off the edits.
// Offline (the STUDIO worker): it allocates; deterministic (no randomness, plain double arithmetic).
#include "RenderPlan.h"
#include "PitchEdit.h"
#include "SourceAudio.h"
#include <functional>

namespace gslice::psola
{
struct Epoch { double pos = 0; int run = 0; };

// epochs of mono x[0, n) at sr; f0At(i) = the f0 (Hz, <= 0 = unvoiced) at sample i. Sorted, one per period.
std::vector<Epoch> markEpochs (const float* x, int64 n, double sr, const std::function<double (int64)>& f0At);

struct Stats { int voicedMarks = 0, unvoicedMarks = 0, epochs = 0; int64 directFrames = 0; };

// the VOCAL stream of a piece with a curve (forward, not held): exactly len frames from pc.u0 into L / R (written, not added).
// pitchSemis / formantSemis: the global PITCH / FORMANT on top of the curve (as the stretcher gets them).
void render (const Piece& pc, const WorkingAudio& w, double pitchSemis, double formantSemis, float* L, float* R, int64 len, Stats* stats = nullptr);
}
