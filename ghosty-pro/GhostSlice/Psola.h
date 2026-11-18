#pragma once
// GHOST SLICE epoch marker (from the phase-15 TD-PSOLA engine): glottal-closure instants of voiced monophonic material, one per
// period. Written from the papers, no third-party code:
//   E. Moulines, F. Charpentier, "Pitch-synchronous waveform processing techniques for text-to-speech synthesis using diphones",
//   Speech Communication 9 (1990) 453-467; J. Makhoul, "Linear prediction: a tutorial review", Proc. IEEE 63 (1975);
//   P. A. Naylor et al., "Estimation of glottal closure instants in voiced speech using the DYPSA algorithm", IEEE TASLP 15 (2007).
//   markEpochs  per voiced run, one positive peak of the zero-phase low-passed signal (1.5 x the run's f0) per period, each searched
//               0.7 .. 1.3 periods after the last; then refined to the LPC residual's dominant peak at the run's consistent phase
//               offset (circular mean, +-0.25 period search); the epoch = the sub-sample (parabolic) low-pass peak + the median residual
//               offset of 5 marks, so it sits on the closure and is consistent to a fraction of a sample.
// GENERATE's voice engine (GenDsp.h, psolaTone) re-spaces 2-period grains centred on these marks: formant-true pitching of voices.
// Offline (a worker thread): it allocates; deterministic.
#include "SourceAudio.h"
#include <functional>

namespace gslice::psola
{
struct Epoch { double pos = 0; int run = 0; };

// epochs of mono x[0, n) at sr; f0At(i) = the f0 (Hz, <= 0 = unvoiced) at sample i. Sorted, one per period.
std::vector<Epoch> markEpochs (const float* x, int64 n, double sr, const std::function<double (int64)>& f0At);

}
