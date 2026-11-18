#pragma once
// GHOST VOICE - the NATURAL engine (Params::engine = 1; shown to the user as NATURAL, the other one is CLASSIC).
// Internal code name "clear" (files, namespace): kept so the round-1 history stays readable. Our own code, no models, no GPU.
//
// Why (measured, docs/research/GHOST-VOICE-QUALITY.md): CLASSIC's biggest cost is the WORLD re-synthesis itself (the vocoder
// floor), plus loudness that moves with the blend and a per-frame vowel codebook that jumps. NATURAL keeps YOUR waveform
// (your glottal source, phase, breath and attacks) and reshapes it:
//   1. IDENTITY SPACE. Each voice = its vowel clusters (Identity::frames) + its vocal-tract size. For every frame of yours
//      the engine finds your own vowel prototype (your nearest clusters) and B's prototype for the same vowel, matched in
//      B's tract size (codebook mapping, smoothed over ~25 ms in both directions: no cluster jumps).
//   2. TRACT SIZE. The blended size is the geometric blend of the sizes (a shift on the log-frequency axis): one set of
//      formants moves, and at 100 % it is B's size.
//   3. BLEND. Two envelopes at the blended size: yours, and B's vowel prototype carrying your detail around your own prototype
//      (consonants, transitions, the exact vowel). They are warped so their formants meet at the blend (one set of peaks) and
//      blended; the depth of the colour follows SLERP's rule (VoxMorph's interpolation: the length of the blended shape is
//      interpolated, never shrunk by averaging). (+ VOICE DESIGN.)
//   5. DIFFERENTIAL FILTER on your audio (spectrum differential, Kobayashi et al. 2014), STFT overlap-add at the take's rate.
//      Boosts are capped by what your recording has (no hiss lifted): +24 dB on sung frames, +12 dB on consonants, 0 in
//      silence. Where a sung frame needs MORE than the cap (a dark recording turned bright), the missing part comes from
//      harmonics re-built at your pitch with the target envelope, above 1.5 kHz only (harmonic-driven brightening).
//   6. BREATH: B's breathiness (aperiodicity profile) above yours is added as noise shaped by the target envelope, + BREATH.
//   7. PITCH / STACK / GRIT: pitch-synchronous overlap-add (TD-PSOLA, Moulines & Charpentier 1990) on the reshaped audio:
//      grains keep the envelope, so formants stay where the blend put them. The STACK copies and octave layers are PSOLA
//      copies too (detune, drift, delay, pan); GRIT = period jitter + grain shimmer. No vocoder on any layer.
//   8. LOUDNESS: ITU-R BS.1770 integrated loudness to -18 LUFS through a look-ahead peak limiter at -1 dBFS, so every blend
//      lands at the same loudness (within 0.1 dB) whatever its peaks.
#include "VoiceFusion.h"
#include <functional>

namespace fusion {
// NATURAL's render (VoiceFusionNatural.cpp); fusion::render calls it when p.engine == 1. p is already sanitised.
bool renderNatural(const voice::Analysis& a, const voice::AudioRef& aAudio, const Identity* b, const Identity* c, const Params& p,
                   voice::Rendered& out, voice::Progress prog, Info* info);
namespace detail {   // CLASSIC's own helpers, shared (unchanged)
std::array<float, kApBands> apBandsOf(const double* ap, int bins, int fs);
void designCurveOf(const Params& p, double* add);
}
}

namespace fusion::clear {

std::array<double, kGrid> meanEnvelope(const Identity& id);
// log of the vocal-tract size ratio other / ref (+ = other smaller, its formants higher)
double tractShift(const Identity& ref, const Identity& other);
// spherical interpolation (t = 0 -> x, 1 -> y), length interpolated linearly
void slerp(const double* x, const double* y, int n, double t, double* out, double* angle = nullptr);
// out(f) = in(f * exp(-u)) on the grid (an envelope moved UP by u)
void shiftGrid(const double* in, double u, double* out);
// mean over 100 Hz .. 8 kHz on the grid (the identity vector's band)
double bandMean(const double* g);
int bandLo(); int bandHi();

// the STFT stage: A's audio x gain-shaped per 5 ms frame (log amplitude on the grid), + the fill signal w (deficit fraction
// on the grid, 0..1) + shaped noise (8 bands, fraction of the target's energy). Output length = length.
struct Shaping {
    int frames = 0; double fp = 5.0;
    std::vector<float> gain;       // frames * kGrid, natural-log amplitude gain
    std::vector<float> deficit;    // frames * kGrid, 0..1 (empty = no fill)
    std::vector<float> noise;      // frames * kApBands, 0..1 (empty = none)
};
std::vector<float> shape(const voice::AudioRef& x, int start, int length, const std::vector<float>& fill, const Shaping& s,
                         voice::Progress prog, int pFrom, int pTo, bool& cancelled);

// TD-PSOLA: x with its pitch track f0 (Hz per frame of fp ms, 0 = unvoiced) re-pitched by ratio(frame) (1 = as sung).
// grit 0..1 = period jitter + grain shimmer. Envelope (formants) kept. Same length.
std::vector<float> psola(const std::vector<float>& x, int fs, const std::vector<double>& f0, double fp,
                         const std::function<double(int)>& ratio, float grit, unsigned seed);

double integratedLufs(const voice::Rendered& r);
// -18 LUFS (normalise) through a 3 ms look-ahead limiter at -1 dBFS (iterated so the limited result lands on target), + gain
void finaliseLoudness(voice::Rendered& out, bool normalise, float gainDb);
constexpr double kTargetLufs = -18.0;

} // namespace fusion::clear
