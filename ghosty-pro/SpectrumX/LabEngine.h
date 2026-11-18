#pragma once
// SPECTRAL LAB engine: an editable time x frequency canvas with resynthesis.
//
// Two planes share one time grid (frames every HOP samples at 48 kHz):
//  * the STFT plane (N = 2048, 75 % overlap, Hann): an imported sound's bins. Untouched bins keep their original
//    complex value, so an unedited sound comes back bit-for-bit (OLA of Hann^2). Bins moved by SHIFT / STRETCH /
//    FREEZE / MORPH become phase-vocoder bins: the peak bin's phase runs at its own frequency, the bins around it
//    stay phase-locked to it (identity phase locking), so moved partials stay clean. With KEEP TRANSIENTS on, frames
//    at an onset take their original phases back (phase reset), so moved drums keep their snap.
//  * the PAINT plane: 348 log-frequency rows (27.5 Hz .. 22 kHz, 3 rows per semitone). Every row is an oscillator
//    (exact frequency per cell when a line was snapped / shifted) plus a noise share that is synthesised as
//    random-phase bins inside the row's band. Brightness = amplitude.
// Stereo: every render also makes a SIDE signal (L = mid + width * side, R = mid - width * side): partials are spread
// by a smooth per-frequency pan, noise gets its own decorrelated copy, and nothing below ~150 Hz goes to the side.
// Rendering never touches the audio thread: a worker renders a copy, the player gets finished buffers.
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_dsp/juce_dsp.h>
#include "SpecScale.h"
#include <vector>
#include <complex>
#include <memory>
#include <functional>
#include <algorithm>

namespace lab {

constexpr double SR = 48000.0;
constexpr int N = 2048, HOP = 512, K = N / 2 + 1, OFFSET = N - HOP;
constexpr double BIN_HZ = SR / N;
constexpr int R = 348;                       // log rows
constexpr double F0 = 27.5, ROWS_PER_OCT = 36.0;
inline double rowHz(double r) { return F0 * std::exp2(r / ROWS_PER_OCT); }
inline double hzRow(double hz) { return ROWS_PER_OCT * std::log2(std::max(1.0, hz) / F0); }
inline double frameCentre(int t) { return (double) t * HOP + N / 2 - OFFSET; }   // the sample a frame is centred on
inline float midiHz(float m) { return 440.0f * std::exp2((m - 69.0f) / 12.0f); }
inline float hzMidi(float hz) { return 69.0f + 12.0f * std::log2(std::max(1.0f, hz) / 440.0f); }
using sp::scaleSteps; using sp::snapPitch;
inline int framesFor(double seconds) { return std::max(8, (int) std::ceil(seconds * SR / HOP)); }
// true when the calling thread (the lab worker) has been asked to stop: long loops bail out early
inline bool cancelled() { return juce::Thread::currentThreadShouldExit(); }

enum Kind : uint8_t { Orig = 0, Moved = 1 };
struct Cell { float mag = 0, freq = 0, phase = 0; uint8_t kind = Orig; int8_t lock = 0; uint16_t spare = 0; };   // mag in sine-amplitude units

struct Canvas {
    int T = 0;
    std::vector<Cell> cells;                 // T * K
    std::vector<float> amp, fq, noise;       // T * R  (fq 0 = the row's own frequency)
    std::vector<uint8_t> touched;            // T: frames whose STFT cells may differ from a fresh analysis of the source
    int srcLen = 0;                          // samples of the imported sound (0 = painted canvas)
    void resize(int frames) { T = frames; cells.assign((size_t) T * K, Cell()); amp.assign((size_t) T * R, 0); fq.assign((size_t) T * R, 0); noise.assign((size_t) T * R, 0); touched.assign((size_t) T, 0); srcLen = 0; }
    Cell* col(int t) { return cells.data() + (size_t) t * K; }
    const Cell* col(int t) const { return cells.data() + (size_t) t * K; }
    float& A(int t, int r) { return amp[(size_t) t * R + (size_t) r]; }
    float A(int t, int r) const { return amp[(size_t) t * R + (size_t) r]; }
    void touch(int t0, int t1) { if ((int) touched.size() != T) touched.assign((size_t) T, 1); for (int t = std::max(0, t0); t <= std::min(T - 1, t1); ++t) touched[(size_t) t] = 1; }
    int lengthSamples() const { return srcLen > 0 ? srcLen : T * HOP; }
    double seconds() const { return lengthSamples() / SR; }
    bool empty() const { return T == 0; }
    // level (linear sine amplitude) seen at frame t, row r: the louder of the paint row and the STFT bins in that row's band
    float level(int t, int r) const;
    bool hasStft() const;
    bool isBlank() const;                    // nothing painted, no STFT content
    size_t bytes() const { return cells.size() * sizeof(Cell) + (amp.size() + fq.size() + noise.size()) * 4 + touched.size(); }
};

// ---------------------------------------------------------------- STFT helpers
struct Stft {
    juce::dsp::FFT fft { 11 };               // 2^11 = N
    std::vector<float> win, buf;
    Stft();
    void forward(const float* frame, std::complex<float>* out);       // N windowed samples -> K bins
    void inverse(const std::complex<float>* in, float* frameOut);     // K bins -> N samples (windowed, OLA-normalised)
};
// analyse audio (48 kHz mono) into a fresh canvas: original phase, instantaneous frequency per bin
void analyse(const std::vector<float>& x, Canvas& c);
// raw complex STFT frames of any signal (used by split + tests); frames as in analyse
std::vector<std::complex<float>> stftFrames(const std::vector<float>& x, int& T);
std::vector<float> istftFrames(const std::vector<std::complex<float>>& X, int T, int length);

// ---------------------------------------------------------------- rendering
enum class Quality { Preview, HQ };
struct Stereo { std::vector<float> m, s; size_t size() const { return m.size(); } };   // mid + side (L = m + w s, R = m - w s)
struct RenderOpts { uint32_t seed = 1; bool keepTransients = true; bool side = true; };
Stereo renderStereo(const Canvas& c, Quality q, const RenderOpts& o);
std::vector<float> render(const Canvas& c, Quality q, uint32_t noiseSeed = 1);   // mid only, KEEP TRANSIENTS on
std::vector<uint8_t> onsetFrames(const Canvas& c, bool forEdit = true);   // attack frames (KEEP TRANSIENTS): edits mark a wider run than the render's fallback

// ---------------------------------------------------------------- editing
struct Brush { float size = 4, hardness = 0.6f, level = 0.5f, noise = 0.0f, timeScale = 1.0f; };
struct Sel { int t0 = 0, t1 = 0; float r0 = 0, r1 = R - 1; bool valid() const { return t1 > t0 && r1 > r0; } };
// free paint keeps the stroke's power constant: BRIGHT is the level of the whole stroke, not of every row under it
void paintAt(Canvas& c, float t, float r, const Brush& b, double exactHz = 0);   // exactHz > 0: the centre row gets that exact pitch
void eraseAt(Canvas& c, float t, float r, const Brush& b);
void smearAt(Canvas& c, float t, float r, const Brush& b);
void freeze(Canvas& c, const Sel& s);                                  // hold column t0 over the selection
// keepTransients: frames at an onset are moved as a whole with a coherent phase (attacks stay sharp)
int shift(Canvas& c, const Sel& s, double semitones, bool keepTransients = true);   // returns how many partials went above ~23 kHz (removed)
void stretchHarmonics(Canvas& c, const Sel& s, double factor, bool keepTransients = true);   // f' = f0 * (f/f0)^factor (f0 = lowest partial)
void gainRegion(Canvas& c, const Sel& s, float gain);
void clearRegion(Canvas& c, const Sel& s);
// region move of one STFT frame: bins in [k0,k1] are re-placed around their peaks at map(freq) (phase locked); returns regions dropped
int moveFrame(Cell* col, int k0, int k1, const std::function<double(double)>& map, float gain = 1.0f);
void lockColumn(Cell* col, int k0, int k1);                            // peak-pick a column, set locks + Moved
// the general form: every region (peak + its bins) of src[k0..k0+n) is asked where to go (f2, gain; false = drop) and max-combined into dst
int placeRegions(const Cell* src, int k0, int n, const std::function<bool(int peakBin, double fp, double& f2, float& gain)>& where, Cell* dst);
void peakRegions(const Cell* src, int n, std::vector<int>& peakOf);   // each bin -> index of its peak (-1 none)
Canvas trimmed(const Canvas& c, int maxFrames);                       // the first maxFrames frames (a copy)

// ---------------------------------------------------------------- image <-> canvas
struct ImageMap { double seconds = 4.0, loHz = 80.0, hiHz = 12000.0; float gamma = 1.0f, rangeDb = 60.0f; void sanitise(); };
void imageToCanvas(const juce::Image& img, const ImageMap& m, Canvas& c);
// log-frequency picture, colour = level; the window (frames t0..t1, rows r0..r1) defaults to the whole canvas
juce::Image canvasImage(const Canvas& c, int width, int height, float rangeDb = 72.0f, double t0 = 0, double t1 = -1, double r0 = 0, double r1 = R - 1);
juce::Colour heat(float v);   // 0..1 -> colour map

// ---------------------------------------------------------------- undo (column-range diffs, whole copies for resizes, settings snapshots)
class Undo {
public:
    void begin(const Canvas& c, int t0, int t1);       // store the state of frames t0..t1 before an edit
    void beginAll(const Canvas& c) { begin(c, -1, -1); }
    void beginSide();                                  // store only the settings (split, locks, lane ...) through the hooks below
    // frames t0..t1 as they were before an edit, already collected by the caller (a brush stroke backs up frames as it reaches them)
    void beginFrames(int t0, int t1, std::vector<Cell>&& cells, std::vector<float>&& amp, std::vector<float>&& fq, std::vector<float>&& noise);
    bool undo(Canvas& c); bool redo(Canvas& c);
    int depth() const { return (int) past.size(); }
    int redoDepth() const { return (int) future.size(); }
    size_t bytes() const;
    void clear() { past.clear(); future.clear(); }
    std::function<juce::MemoryBlock()> getSide;        // the host's settings, serialised
    std::function<void(const juce::MemoryBlock&)> setSide;
    static constexpr size_t kBudget = (size_t) 160 << 20;   // RAM for undo; the newest step is always kept
private:
    struct Entry { int t0 = -1, t1 = -1; std::vector<Cell> cells; std::vector<float> amp, fq, noise; std::unique_ptr<Canvas> whole; bool hasSide = false, hasCanvas = true; juce::MemoryBlock side; size_t bytes() const; };
    Entry grab(const Canvas& c, int t0, int t1);
    void put(Canvas& c, Entry& e);                     // swaps e with the canvas content
    std::vector<Entry> past, future;
    void trim();
};

// ---------------------------------------------------------------- serialisation
// compact form: 16-bit log magnitude / phase / frequency offset, empty cells skipped, and - when the canvas still
// matches its source sound - only the frames that were edited (the rest is re-analysed from the source on load).
juce::MemoryBlock save(const Canvas& c, bool sourceMatches = false);
// source: the canvas' imported sound, needed when the block was saved with sourceMatches (old full blocks load too)
bool load(const juce::MemoryBlock& m, Canvas& c, const std::vector<float>* source = nullptr);
// audio: lossless 24-bit FLAC
juce::MemoryBlock packAudio(const std::vector<float>& x);
std::vector<float> unpackAudio(const juce::MemoryBlock& m);           // also reads the old gzip float blocks

// ---------------------------------------------------------------- audio helpers
std::vector<float> resampleTo48k(const float* x, int n, double fromRate);   // windowed sinc, its delay removed
bool writeWav(const juce::File& f, const std::vector<float>& x, double rate = SR);
bool writeWav(const juce::File& f, const std::vector<float>& L, const std::vector<float>& Rt, double rate = SR);
float peakOf(const std::vector<float>& x);
float rmsOf(const std::vector<float>& x);
void normalise(std::vector<float>& x, float peak = 0.89f);
} // namespace lab
