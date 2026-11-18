#pragma once
// Ghosty Spectrum VOICE engine: pitch + formant analysis / resynthesis (speech-analysis style).
//
// Built on WORLD v1.0.1 (M. Morise, modified BSD; vendored, see Voice.cmake): every sound is taken apart into
//   * an f0 track (Harvest, or DIO + StoneMask in FAST mode) plus a per-frame periodicity score, so the voiced /
//     unvoiced decision can be changed at render time (VOICING) without analysing again,
//   * a spectral envelope per frame (CheapTrick: the "formants", free of the harmonic comb), stored compactly
//     (16-bit log power on a frequency grid that keeps full resolution below 1.5 kHz),
//   * an aperiodicity per frame (D4C: how much of each band is breath / noise), stored with WORLD's own codec,
//   * percussive onsets (spectral flux) for TRANSIENTS,
// and put back together by WORLD's pulse + noise synthesis, streamed frame by frame (memory stays flat for long files).
// Analysis runs in parallel pieces (2-4 threads) with progress / cancel.
// Modes: PITCH (shift, keep or move formants), BORROW (A takes on B's formants; RESYNTH for single voices, VOCODE for
// chords / pads / detuned stacks: A's real audio filtered by B's envelope over A's), CONTOUR (draw the pitch, snap to a
// key with a retune glide), VOWEL (impose / morph vowels along a lane), TUNE (correct the detected pitch to a key:
// retune speed, flatten, humanize). All lanes and curves are read in SOURCE time (A's own clock), so SPEED never moves
// a note off its syllable. Extra voices: DOUBLE (2-8 detuned / delayed copies, stereo) and HARMONY (up to 3 in-key voices).
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <vector>
#include <string>
#include <functional>
#include <cstdint>
#include <memory>

namespace voice {

constexpr int kFormants = 4;
constexpr int kVowels = 5;                       // A E I O U (+ a captured custom vowel at lane position 5)
constexpr int kLaneVowels = 6;
constexpr double kMinSeconds = 0.05;             // shorter sounds can't be analysed

// progress 0..100 + cancel; both optional
struct Progress {
    std::atomic<int>* pct = nullptr; std::atomic<bool>* cancel = nullptr;
    void set(int p) const { if (pct) pct->store(juce::jlimit(0, 100, p)); }
    bool stop() const { return cancel && cancel->load(); }
};

struct AnalysisOptions {
    enum Method { Quality = 0, Fast = 1 };       // Harvest / DIO + StoneMask
    int method = Quality;
    double f0Floor = 71.0, f0Ceil = 800.0;       // Hz; the floor also sets the envelope FFT size (25 Hz for sub / 808)
    double framePeriodMs = 5.0;
    float voicingThreshold = 0.45f;              // periodicity below this -> unvoiced (the default VOICING; render can change it)
    float silenceDb = -60.0f;                    // frames this far below the loudest frame are silence (relative gate)
    int threads = 0;                             // 0 = automatic (2-4)
};

struct Formant { float hz = 0, bwHz = 0, dB = 0; };
using Formants = std::array<Formant, kFormants>;

struct Onset { float t = 0, strength = 0; };     // seconds, 0..1

struct Analysis {
    int fs = 0, fftSize = 0, length = 0;         // length = samples analysed
    double framePeriod = 5.0;                    // ms
    std::vector<double> f0;                      // Hz per frame after the default voicing gate (0 = unvoiced): display, tests
    std::vector<float> f0Raw;                    // the tracker's f0 (0 = no pitch / silence), before the periodicity gate
    std::vector<float> periodicity;              // normalised autocorrelation at 1/f0Raw (0..1)
    std::vector<float> rms;                      // per frame, linear
    std::vector<Formants> formants;              // per frame (F1..F4, hz 0 when not found)
    std::vector<Onset> onsets;
    // compact envelope: storedBins per frame, 16-bit log power; bins below lowBins exact, above them every dec-th bin
    std::vector<uint16_t> specQ; int storedBins = 0, lowBins = 0, dec = 1;
    std::vector<double> apCoded; int nAp = 0;    // WORLD-coded aperiodicity, nAp values per frame
    int frames() const { return (int) f0.size(); }
    int bins() const { return fftSize / 2 + 1; }
    bool empty() const { return f0.empty(); }
    double seconds() const { return fs > 0 ? length / (double) fs : 0.0; }
    double frameTime(int i) const { return i * framePeriod / 1000.0; }
    int frameAt(double seconds) const { return juce::jlimit(0, juce::jmax(0, frames() - 1), (int) std::lround(seconds * 1000.0 / framePeriod)); }
    void specRow(int f, double* out) const;      // decode one frame's envelope into out[bins()]
    void apRow(int f, double* out) const;        // decode one frame's aperiodicity into out[bins()]
    std::vector<double> specFrame(int f) const { std::vector<double> v((size_t) bins()); specRow(f, v.data()); return v; }
    std::vector<double> apFrame(int f) const { std::vector<double> v((size_t) bins()); apRow(f, v.data()); return v; }
    bool voiced(int f) const { return f >= 0 && f < frames() && f0[(size_t) f] > 0; }
    float voicedFraction() const;
    double medianF0() const;                     // Hz of the voiced frames (0 if none)
    size_t memoryBytes() const;
};

// mono float in -> full analysis. Returns false when cancelled (out is then empty). Throws std::bad_alloc on no memory.
bool analyse(const float* x, int n, int fs, const AnalysisOptions& o, Analysis& out, Progress p = {});
// f0 only (same gating as analyse): tests and the quick overlay
bool trackF0(const float* x, int n, int fs, const AnalysisOptions& o, std::vector<double>& f0, Progress p = {});

// formants of one power envelope (bins = fftSize/2+1 over 0..fs/2); f0 = the frame's pitch (0 = unknown / unvoiced)
Formants formantsOf(const double* spec, int bins, int fs, double f0 = 0);
// formants straight from a waveform (pre-emphasised LPC over a Hann window), independent of the envelope path: tests
Formants formantsOfWave(const float* x, int n, int fs);

// -------------------------------------------------------------------------------------------- vowels
struct Vowel { const char* name; float F[kFormants], BW[kFormants], dB[kFormants]; };
enum VowelSet { VowelsMale = 0, VowelsFemale = 1, VowelsChild = 2 };
const std::array<Vowel, kVowels>& vowelTable(int set = VowelsMale);    // A E I O U (average targets per voice type)
// pos 0..4 along A-E-I-O-U (5 = the custom vowel when given), scale moves every formant (voice size)
Vowel vowelAt(float pos, float scale = 1.0f, int set = VowelsMale, const Vowel* custom = nullptr);
void vowelEnvelope(const Vowel& v, int bins, int fs, double* out);   // the power envelope of a vowel (unit-ish level)
// the SIZE that best fits a voice's measured F1/F2 to a vowel table (log-least-squares over the voiced frames)
float fitVowelScale(const Analysis& a, int set = VowelsMale);
// how peaky the voiced envelopes are (median dB range 200 Hz..4 kHz after tilt correction): vowels ~20-35, synths / chords < 12
float envelopeContrast(const Analysis& a);

// -------------------------------------------------------------------------------------------- curves
struct Pt { double t = 0; float v = 0; };                      // seconds (source time), value
struct Curve {                                                 // breakpoints sorted by t, linear between, flat outside
    static constexpr int kMaxPoints = 20000;
    std::vector<Pt> pts;
    bool empty() const { return pts.empty(); }
    float at(double t) const;
    void add(double t, float v, double mergeWithin = 0.0);     // keeps the list sorted; replaces a point closer than mergeWithin
    void erase(double t0, double t1);
    void clear() { pts.clear(); }
    void sanitise(float lo, float hi);                         // drop non-finite, clamp values, sort, cap the count
    juce::String save() const; bool load(const juce::String&);
    bool operator==(const Curve& o) const;
};
float snapMidi(float midi, int root, int scale);               // scale: 0 chromatic 1 major 2 minor 3 pentatonic 4 dorian 5 mixolydian 6 whole-tone
float scaleStep(float midi, int steps, int root, int scale);   // the note `steps` scale degrees away from midi's nearest scale note
const juce::StringArray& scaleNames();
inline double midiHz(double m) { return 440.0 * std::exp2((m - 69.0) / 12.0); }
inline double hzMidi(double hz) { return 69.0 + 12.0 * std::log2(juce::jmax(1.0, hz) / 440.0); }

// -------------------------------------------------------------------------------------------- render
// a hand repair of the pitch track over [t0, t1] (source seconds): move by semis (octave fixes) and / or force voicing
struct Fix { double t0 = 0, t1 = 0; float semis = 0; int voicing = 0; };   // voicing: 0 keep, 1 voiced, -1 unvoiced
struct Harmony { int steps = 0; float level = 0.7f, pan = 0.0f; };          // scale degrees (0 = off), e.g. +2 = a third up

struct RenderParams {
    enum Mode { Pitch = 0, Borrow = 1, Contour = 2, VowelMorph = 3, Tune = 4 };
    int mode = Pitch;
    // all modes
    float pitchSemis = 0;                  // moves the f0 track (+ the pitch lane)
    bool keepFormants = true;              // false: the envelope moves with the pitch (chipmunk / giant)
    float formantSemis = 0;                // extra envelope shift (+ the formant lane)
    float speed = 1.0f;                    // 0.25 .. 4 (frames re-timed, pitch unchanged)
    float breath = 0.0f;                   // -1 .. +1 around the calibrated (natural) breathiness (+ the breath lane)
    float voicing = 0.45f;                 // periodicity gate for voiced / unvoiced
    std::vector<Fix> fixes;                // pitch-track repairs (octave, voicing)
    Curve pitchLane, formantLane, breathLane;   // automation in source time (empty = knob only)
    double regionStart = -1, regionEnd = -1;    // source seconds: process only this region (10 ms crossfades); < 0 = all
    double trimStart = -1, trimEnd = -1;        // source seconds: START / END - the result is only this part of A (< 0 = the whole file)
    bool trimmed(double sourceSeconds) const { return (trimStart > 1e-9 || (trimEnd > 0 && trimEnd < sourceSeconds - 1e-9)) && std::isfinite(trimStart) && std::isfinite(trimEnd); }
    bool transients = true; float transientSens = 0.5f;   // keep percussive attacks (original audio around onsets)
    // BORROW (A = pitch + timing, B = the envelope)
    enum TimeMode { FollowB = 0, FollowBLoop = 1, StretchBToA = 2 };
    enum Carrier { CarrierAuto = 0, CarrierResynth = 1, CarrierVocode = 2 };
    int timeMode = FollowB, carrier = CarrierAuto;
    float borrowMix = 1.0f;                // 0 = A's own envelope .. 1 = all B
    float dynamics = 0.7f;                 // 0 = A's loudness .. 1 = B's loudness (speech dynamics incl. its silences)
    float breathFromB = 0.6f;              // B's aperiodicity blended in (RESYNTH)
    bool consonantsFromB = true;           // frames where B is unvoiced but audible become noise (s, f, t...)
    // CONTOUR
    Curve contour;                         // midi vs source seconds (empty = keep the original track)
    bool contourSnap = false; int root = 0, scale = 0;
    float contourAmount = 1.0f;            // 0 = original .. 1 = the drawn curve
    bool forceVoiced = false;              // unvoiced frames follow the curve too (whispers become notes)
    // CONTOUR snap + TUNE
    float retuneMs = 40.0f;                // glide time to a new note (0 = instant)
    float flatten = 0.0f;                  // TUNE: 0 = keep vibrato / drift around the note .. 1 = dead flat
    float humanize = 0.0f;                 // TUNE: slower correction on long notes
    // VOWEL
    Curve lane;                            // vowel position 0..5 vs source seconds (empty = position 0 = A)
    float vowelAmount = 1.0f;              // 0 = A's own envelope .. 1 = the vowel table
    float vowelScale = 1.0f;               // 0.7 .. 1.4 formant scale (big .. small voice)
    int vowelSet = VowelsMale;
    bool hasCustomVowel = false; std::array<float, kFormants> customF { 500, 1500, 2500, 3500 };
    // extra voices (RESYNTH paths)
    int doubles = 0;                       // 0 = off, else 2..8 voices in total
    float doubleCents = 12, doubleMs = 15, doubleFormant = 0.3f, width = 0.8f;
    std::array<Harmony, 3> harmony {};
    // output
    float gainDb = 0; bool normalise = true;
    bool stereoOut() const;                // doubles / harmonies / a stereo carrier make a stereo result
};

struct RenderInfo {
    std::vector<double> f0;                // the main voice's f0 per output frame (0 = unvoiced)
    std::vector<float> tSrc;               // A's source time per output frame (seconds)
    std::vector<Formants> formants;        // the main voice's envelope formants per output frame
    double framePeriod = 5.0; int fs = 0;
    bool vocoded = false;                  // BORROW used the VOCODE carrier
    double regionOut0 = -1, regionOut1 = -1;   // output seconds of the processed region (when a region was set)
    double sourceToOutput(double tSrc) const;  // inverse of tSrc (first match), seconds
    double outputToSource(double tOut) const;
};

// A's audio (any channel count; mono = the analysed signal) for TRANSIENTS, VOCODE and regions
struct AudioRef { std::vector<const float*> ch; int n = 0, fs = 0; bool valid() const { return !ch.empty() && n > 0 && fs > 0; } };
struct Rendered { std::vector<float> L, R; int fs = 0; bool stereo() const { return !R.empty(); } size_t size() const { return L.size(); } };

// BORROW AUTO carrier: VOCODE when A isn't one clean voiced pitch (a chord / noise, a wide stereo stack, a detuned or
// beating unison); RESYNTH for single voices and mono lines
bool preferVocode(const Analysis& a, const AudioRef& aAudio, float voicing);
// A's analysis is the carrier for every mode; B is only read in BORROW. Returns false when cancelled.
bool render(const Analysis& a, const AudioRef& aAudio, const Analysis* b, const RenderParams& p, Rendered& out, Progress prog = {}, RenderInfo* info = nullptr);
// mono convenience (no transients / vocode / region splice: there's no audio)
bool render(const Analysis& a, const Analysis* b, const RenderParams& p, std::vector<float>& out, Progress prog = {}, RenderInfo* info = nullptr);
// the TUNE target track of a pitch track (midi per frame, NaN = unvoiced) -> corrected midi per frame
std::vector<double> tunePitch(const std::vector<double>& midi, double framePeriodMs, int root, int scale, float retuneMs, float flatten, float humanize);
// the effective A pitch per frame (voicing gate + fixes), Hz (0 = unvoiced)
std::vector<double> effectiveF0(const Analysis& a, const RenderParams& p);

// envelope at another frequency scale: out(f) = in(f / factor) (log-domain interpolation)
void warpEnvelope(const double* in, int bins, double factor, double* out);
double frameEnergy(const double* spec, int bins);
// harmonic-to-noise measure used to calibrate the breathiness (tests)
double breathCalibration(double hz);   // the aperiodicity gain applied at hz (1 = none)
void calibrateAperiodicity(double* ap, int bins, int fs, double periodicity);   // the breath calibration applied to every resynthesised frame

// -------------------------------------------------------------------------------------------- building blocks (shared with Ghost Voice)
// the voicing gate of the renderer: raw f0 + periodicity -> f0 track (0 = unvoiced), median-smoothed, short runs dropped, holes bridged
std::vector<double> gateF0(const std::vector<float>& f0Raw, const std::vector<float>& periodicity, float threshold);
// streamed WORLD synthesis: frames (f0, power envelope, aperiodicity over bins = fftSize/2+1) go in, samples come out; memory stays flat
class StreamSynth {
public:
    StreamSynth(int fs, double framePeriodMs, int fftSize, size_t targetSamples);
    ~StreamSynth();
    void push(double f0, const double* spec, const double* ap);
    std::vector<float> finish();                                   // pads to targetSamples
    bool ok() const;
private:
    struct Impl; std::unique_ptr<Impl> impl;
};

// -------------------------------------------------------------------------------------------- utilities
// band-limited resampler (Kaiser-windowed sinc, >= 90 dB stop band, cut-off 0.45 x the lower rate)
std::vector<float> resample(const std::vector<float>& x, double fromRate, double toRate);
int workingRate(int fs);                                       // the analysis rate for a file rate (16..96 kHz kept, else 48 kHz)
void normalise(std::vector<float>& x, float peak = 0.89f);
bool finite(const std::vector<float>& x);
bool finite(const Analysis& a);
// WORLD's copyright + licence text (modified BSD): must be shown with binary distributions (about box / manual / installer)
const char* thirdPartyNotice();
} // namespace voice
