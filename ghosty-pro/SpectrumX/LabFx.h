#pragma once
// SPECTRAL LAB transformations: GHOST (variations across repetitions), SPLIT (bands + step sequencers),
// CHAOS PAINT (seeded generator with locks), DNA (spectral morph A -> B).
#include "LabEngine.h"
#include <array>

namespace lab {

// ---------------------------------------------------------------- SPECTRAL GHOST
struct GhostParams { int count = 8; float evolve = 0.6f, memory = 0.5f, decay = 0.3f; uint32_t seed = 1; bool keepTransients = true; };
constexpr int kMaxGhosts = 32;
constexpr double kGhostMaxSeconds = 8.0;     // longer sounds are ghosted from their first 8 s
// variation 0 is the sound itself; then metallic resonance -> upward shift -> fragments -> grain dust -> return.
// ghostEach builds them one at a time (only the previous one is kept); fn returns false to stop early.
void ghostEach(const Canvas& src, const GhostParams& p, const std::function<bool(int index, const Canvas& variation)>& fn);
std::vector<Canvas> ghostSet(const Canvas& src, const GhostParams& p);   // all at once (tests, small sounds)
const char* ghostStage(int index, int count, float evolve);             // "SOURCE", "METAL", "RISE", "FRAGMENT", "DUST", "RETURN"

// ---------------------------------------------------------------- SPECTRAL SPLIT
constexpr int kMaxBands = 6, kSteps = 32;     // up to 32 steps per band, each band its own length (polymeter)
struct BandSeq {
    std::array<uint8_t, kSteps> steps {};     // on / off
    std::array<uint8_t, kSteps> prob {};      // 0..100 % chance the step plays
    std::array<int8_t, kSteps> pitch {};      // -12..+12 semitones (the band is re-read faster / slower from the step start)
    int length = 16, rateDiv = 16;            // steps in the band's loop; 4 = 1/4 ... 64 = 1/64
    float gate = 1.0f, level = 1.0f; bool retrig = false, mute = false;
    BandSeq() { steps.fill(1); prob.fill(100); pitch.fill(0); }
};
struct SplitSetup {
    int bands = 4; std::array<float, kMaxBands - 1> xover { 180.0f, 900.0f, 3500.0f, 9000.0f, 14000.0f }; std::array<BandSeq, kMaxBands> seq;
    float swing = 0.0f; uint32_t seed = 1;    // swing 0..0.5 of a step on every second step; seed for the step chances
    void sanitise();                          // clamp everything, sort the crossovers
};
float splitMask(const SplitSetup& s, int band, double hz);              // complementary: sums to 1 over bands
std::vector<std::vector<float>> splitBands(const std::vector<float>& x, const SplitSetup& s);   // STFT masks, original phase
struct StepEval { float gain = 1; double srcBeats = 0; int step = 0; };
StepEval evalBand(const BandSeq& b, double beats, int band = 0, float swing = 0.0f, uint32_t seed = 1, bool muted = false);   // gate, retrig, swing, chance, pitch
std::vector<float> renderSplit(const std::vector<std::vector<float>>& bands, const SplitSetup& s, double bpm, int length);

// ---------------------------------------------------------------- CHAOS PAINT
struct ChaosParams { float chaos = 0.4f, density = 0.5f, harmony = 0.6f, motion = 0.3f, constraint = 0.0f; uint32_t seed = 1; int root = 0, scale = 1; double bpm = 120; };
void chaosPaint(Canvas& c, const ChaosParams& p, const std::vector<Sel>& locks, const Canvas* follow);

// ---------------------------------------------------------------- SPECTRAL DNA (morph)
struct MorphPrep {
    Canvas A, B;                                   // analysed sounds (STFT plane)
    std::vector<std::pair<int, int>> path;          // DTW time alignment (frame of A, frame of B)
    std::vector<float> envA, envB;                  // per-frame spectral envelopes on the log rows (T * R)
};
std::shared_ptr<MorphPrep> morphPrepare(const std::vector<float>& a, const std::vector<float>& b);   // nullptr if cancelled
void morph(const MorphPrep& m, float t, Canvas& out);
} // namespace lab
