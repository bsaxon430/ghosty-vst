#pragma once
// GHOST SLICE GENERATE - the offline sound DSP (a worker thread: it allocates; deterministic per seed, bit for bit on one build).
// Our own code. Methods (papers / published descriptions; GPL code was never copied):
//   voice tone   TD-PSOLA (Moulines & Charpentier 1990): the vowel's glottal-closure epochs (Psola.h markEpochs), 2-period asymmetric
//                Hann grains centred on them, re-spaced at the target period. The grains keep the vocal tract's response, so the
//                formants stay where they are at any pitch (NATURAL). A formant ratio reads each grain faster / slower (CHIPMUNK =
//                the formants follow the pitch, DEEP = a bigger, darker throat), from a band-limited copy so nothing aliases. The
//                sustain is a seeded random walk over the stable epochs (never a loop you can hear), with optional natural pitch
//                movement, delayed vibrato and slow drift; L and R take their own walks (a real, decorrelated width).
//   grain cloud  granular synthesis after Roads, "Microsound" (MIT Press 2001) and Bencina, "Implementing Real-Time Granular
//                Synthesis" (2001); the parameter set follows Mutable Instruments Clouds (MIT, vendored in third_party/eurorack-mit:
//                density, size, spray around a moving head, per-grain pitch and pan): Tukey grains (flat middle, raised-cosine
//                tapers), pitch from a list of notes in key (resampled from band-limited copies: no aliasing on the way up), pitch
//                scatter, constant-power pan spread, reverse grains.
//   spectral     the HOLD engine's FREEZE (Paulstretch-style: long windows, a fresh random phase per bin and frame) and TEXTURE
//                (phase-locked STFT magnitudes with randomised phases elsewhere), Hold.h, run on our own buffers.
//   loudness     ITU-R BS.1770-4 integrated loudness (K-weighting + the two gates), true peak on the 4x interpolation of
//                Shared/CleanStage.h; finish() = -14 LUFS with the shared true-peak ceiling below -1 dBTP.
#include "SourceAudio.h"
#include <functional>

namespace gslice::gen::dsp
{
using Buf = juce::AudioBuffer<float>;

// splitmix64: a portable seeded stream (the std distributions differ between standard libraries)
struct Rng
{
    uint64 s;
    explicit Rng (uint64 seed) : s (seed * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull) {}
    uint64 next() { uint64 z = (s += 0x9E3779B97F4A7C15ull); z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; return z ^ (z >> 31); }
    double uni() { return (double) (next() >> 11) * (1.0 / 9007199254740992.0); }   // [0, 1)
    double sym() { return 2.0 * uni() - 1.0; }                                         // [-1, 1)
    int pick (int n) { return n <= 1 ? 0 : (int) (uni() * n) % n; }
    bool chance (double p) { return uni() < p; }
    int weighted (const std::vector<double>& w) { double t = 0; for (double x : w) t += x; double r = uni() * t; for (size_t i = 0; i < w.size(); ++i) { if (r < w[i]) return (int) i; r -= w[i]; } return (int) w.size() - 1; }
};

inline double dbToGain (double db) { return std::pow (10.0, db / 20.0); }
inline double gainToDb (double g) { return g > 1e-12 ? 20.0 * std::log10 (g) : -240.0; }
inline double midiToHz (double m) { return 440.0 * std::exp2 ((m - 69.0) / 12.0); }
inline double hzToMidi (double hz) { return 69.0 + 12.0 * std::log2 (hz / 440.0); }

// ---------------------------------------------------------------- basics
Buf stereoOf (const Buf& in);                                            // mono -> both sides; stereo stays
Buf slice (const Buf& in, double a, double b);                           // [a, b) (fractional start: linear interpolation)
void fadeEdges (Buf& b, int in, int out);                                // raised-cosine fades
void addInto (Buf& dst, const Buf& src, int at, float gain = 1.0f, float gl = 1.0f, float gr = 1.0f);
double rmsOf (const Buf& b, int from = 0, int to = -1);
float peakOf (const Buf& b);
// zero-phase low-pass (Butterworth sections run forward and backward), for reading a sound faster than 1x without aliasing
Buf lowpass (const Buf& in, double sr, double hz);
void highpassInPlace (Buf& b, double sr, double hz);                     // 2nd order, forward only (rumble / DC)
// cubic (Catmull-Rom) read of channel ch at a fractional position; 0 outside
inline float readAt (const Buf& b, int ch, double p) noexcept
{
    const int n = b.getNumSamples(); if (n == 0) return 0.0f;
    const float* x = b.getReadPointer (juce::jmin (ch, b.getNumChannels() - 1));
    const double fl = std::floor (p); const int i = (int) fl; const float f = (float) (p - fl);
    auto at = [&] (int k) { return k < 0 || k >= n ? 0.0f : x[k]; };
    const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
    const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + y1;
}

// CLEAR's reads: a 32-tap Kaiser-windowed sinc (beta 7.5, cutoff at Nyquist; flat to about 20 kHz at 48 kHz, images below -75 dB),
// 512 phases with linear interpolation between them. An integer position reads the sample itself (bit exact), so a sound played at
// its own speed is untouched. 0 outside [0, n).
struct Sinc
{
    static constexpr int kTaps = 32, kHalf = 16, kPhases = 512;
    float t[(kPhases + 1) * kTaps];
    Sinc();
    static const Sinc& get();
    // x[i0 - 15 .. i0 + 16] around frac in [0, 1); at(k) gives x[k] (any wrapping / bounds the caller wants)
    template <typename At> inline float read (int i0, float frac, At&& at) const noexcept
    {
        const float ph = frac * (float) kPhases; const int p = juce::jlimit (0, kPhases - 1, (int) ph); const float pf = ph - (float) p;
        const float* a = t + p * kTaps; const float* b = a + kTaps;
        float s = 0.0f;
        for (int k = 0; k < kTaps; ++k) s += (a[k] + pf * (b[k] - a[k])) * at (i0 - kHalf + 1 + k);
        return s;
    }
    // the 32 weights for one fractional offset (a grain read at a constant offset uses them for every sample: x[i0 - 15 + k] * h[k])
    void kernel (float frac, float* h) const noexcept
    {
        const float ph = frac * (float) kPhases; const int p = juce::jlimit (0, kPhases - 1, (int) ph); const float pf = ph - (float) p;
        const float* a = t + p * kTaps; const float* b = a + kTaps;
        for (int k = 0; k < kTaps; ++k) h[k] = a[k] + pf * (b[k] - a[k]);
    }
};
inline float sincAt (const float* x, int n, double p) noexcept
{
    const double fl = std::floor (p); const int i = (int) fl; const float f = (float) (p - fl);
    if (f == 0.0f) return i >= 0 && i < n ? x[i] : 0.0f;
    return Sinc::get().read (i, f, [x, n] (int k) { return k < 0 || k >= n ? 0.0f : x[k]; });
}
inline float readSinc (const Buf& b, int ch, double p) noexcept
{
    const int n = b.getNumSamples(); if (n == 0) return 0.0f;
    return sincAt (b.getReadPointer (juce::jmin (ch, b.getNumChannels() - 1)), n, p);
}
// a sound played ratio x faster (pitch up by 12 log2 ratio semitones, shorter by the same factor), r8brain (CDSPResampler24)
Buf varispeed (const Buf& in, double sr, double ratio);
// the loop end near `le` whose last `win` frames best match the `win` frames before loopStart (normalised correlation, +-search):
// with a steady pitch the crossfade then joins two in-phase waveforms (no dip, no phasing)
int phaseMatchedLoopEnd (const Buf& b, int loopStart, int le, int search, int win);
Buf monoOf (const Buf& b);                                               // the average of the channels, one channel

// band-limited copies of one sound for resampled reads: copy k is low-passed for ratios up to ratios[k] (0.45 sr / ratio)
struct BandBank
{
    double sr = 48000;
    std::vector<double> ratios;          // ascending; copy 0 = the sound itself (ratio <= 1.0)
    std::vector<Buf> copies;
    void build (const Buf& in, double sampleRate);
    const Buf& forRatio (double r) const;    // the copy that is clean when read r x faster
};

// ---------------------------------------------------------------- the voice tone (TD-PSOLA sustain)
struct Voice
{
    Buf audio;                   // the span (stereo) [from - pad, to + pad) of the working audio
    BandBank bank;               // its band-limited copies (CHIPMUNK reads grains faster)
    double sr = 48000, offset = 0;   // offset = the working-audio frame of audio[0]
    std::vector<double> E;       // epochs inside audio (frames), increasing
    std::vector<float> rms;      // the 2-period grain RMS per epoch
    std::vector<double> period;  // per epoch: its local period (frames, smoothed)
    std::vector<float> match;    // per epoch: how well its grain matches the core's reference grain (-1..1)
    double f0 = 0;               // median Hz over the core
    int first = 0, s0 = 0, s1 = 0;   // first = the first epoch at / after the vowel start; [s0, s1] = the stable core's epochs
    double coreRms = 0;
    bool ok() const { return f0 > 0 && (int) E.size() >= 8 && s1 - s0 >= 3; }
};
// from = the vowel start, [c0, c1) the stable core, to = the vowel end (working frames); f0At(working frame) from the analysis (0 = unvoiced)
Voice analyseVoice (const Buf& work, double sr, double from, double c0, double c1, double to, const std::function<double (double)>& f0At);

struct ToneSpec
{
    double seconds = 4.0;
    double semis = 0;            // the pitch relative to the voice's median f0
    double formant = 0;          // semitones the formants move (CHIPMUNK = semis, DEEP = -4, NATURAL = 0)
    double natural = 0;          // 0 = a steady target pitch .. 1 = the voice's own pitch movement around its median
    double vibratoCents = 0, vibratoHz = 5.2, vibratoDelayS = 0.4;
    double driftCents = 4;
    double width = 0.35;         // 0 = mono .. 1 = L / R fully their own walks
    double walk = 1.0;           // the walk's step size (0.5 calm .. 2 restless)
    bool onset = true;           // play the vowel's own start (in order) before the walk
    uint32_t seed = 1;
    // CLEAR: sinc reads (no cubic droop), only grains that match the vowel's core closely, the voice's own pitch kept in full through
    // the vowel's start (its scoop into the note) and, in the sustain, as a smoothed curve (no grain-to-grain jitter from the walk)
    bool clean = false;
};
Buf psolaTone (const Voice& v, const ToneSpec& s);
// the same for sounds without a clear pitch (noise, chords, drums): Tukey grains from the core, pitched by resampling
Buf grainTone (const Buf& work, const BandBank& bank, double sr, double c0, double c1, const ToneSpec& s);

// ---------------------------------------------------------------- the grain cloud
struct GrainSource { double a = 0, b = 0; double midi = 0; double weight = 1; };   // a span of the working audio (frames) + its own pitch (0 = none)
struct CloudSpec
{
    double seconds = 6.0;
    double density = 20.0;       // grains per second
    double grainS = 0.12, grainJitter = 0.35;   // grain length (s) +- share
    double taper = 0.5;          // Tukey: the share of the grain in the raised-cosine tapers (1 = Hann)
    double spray = 0.25;         // share of a source span around the walking head that a grain may start from
    double headSpeed = 0.3;      // how fast the head walks through a span (1 = real time)
    std::vector<double> notes;   // MIDI notes the grains are pitched to (weights below; empty = every grain keeps its own pitch)
    std::vector<double> noteWeights;
    bool foldOctaves = true;     // a note moves by octaves to the one nearest the grain's own pitch (small shifts, the voice stays a voice)
    double natural = 0;          // 0 = exactly on the note .. 1 = the grain's own pitch (the note only folds the octave)
    double scatterCents = 6;     // random detune per grain
    double spread = 0.8;         // pan spread 0..1
    double gainJitterDb = 3.0;
    double reverse = 0.0;        // chance a grain plays backwards
    double fadeS = 0.05;         // fades of the whole cloud
    std::function<double (double)> densityAt;   // optional 0..1 shape over time (seconds) (slow evolution)
    uint32_t seed = 1;
    bool clean = false;          // CLEAR: sinc reads
    int channels = 2;            // 1 = a mono cloud (pan ignored)
    // build 6 (off for older recipes, which render as they did):
    bool normalise = false;      // the level divided by the expected overlap (density x size x the window's power): DENSITY never pumps it;
                                 // with `clean` the grains also overlap at least 50 % (no holes)
    int window = 0;              // 0 Tukey (taper), 1 Hann, 2 Gaussian
};
Buf cloud (const Buf& work, const BandBank& bank, double sr, const std::vector<GrainSource>& src, const CloudSpec& c);

// ---------------------------------------------------------------- spectral (the HOLD engine's FREEZE / TEXTURE on any buffer)
Buf freeze (const Buf& in, double sr, double a, double b, double seconds, uint32_t seed);
Buf blur (const Buf& in, double sr, double a, double b, double seconds, double smear, uint32_t seed);
// Signalsmith Stretch (MIT), offline: semis up / down; keepFormants keeps the spectral envelope (f0Hz helps it), formantSemis moves it.
// Stereo out (mono in: both sides), or mono out when monoOut and the input is mono.
Buf shift (const Buf& in, double sr, double semis, bool keepFormants, double formantSemis, double f0Hz, bool monoOut = false);

// ---------------------------------------------------------------- loops, loudness, finishing
// bake a seamless loop: the tail before loopEnd crossfades (equal power) into the material before loopStart
void bakeLoop (Buf& y, int loopStart, int loopEnd, int crossfade);
double lufs (const Buf& b, double sr);                   // integrated loudness, -100 = silence
double truePeakDb (const Buf& b);                        // dBTP (4x interpolated)
// -14 LUFS (target), then the true-peak ceiling (Shared/CleanStage.h Ceiling, latency removed), then a trim so the true peak stays
// below peakDb. Returns the final loudness. limit = false (CLEAR): gain only - the loudness target or the true-peak ceiling, whichever
// is lower; nothing is limited or clipped (the waveform keeps its shape; a very peaky take lands a little under the target).
double finish (Buf& b, double sr, double targetLufs = -14.0, double peakDb = -1.2, bool limit = true);
int clicksIn (const Buf& b, float jump = 0.3f);          // sample-to-sample jumps above `jump`
bool finiteAndClean (const Buf& b);                      // no NaN / Inf / denormals
} // namespace gslice::gen::dsp
