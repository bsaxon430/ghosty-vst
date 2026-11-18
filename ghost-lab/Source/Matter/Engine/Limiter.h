// Ghost Matter - AGPL-3.0-only (see LICENSE, NOTICE.md).
//
// The output ceiling: a look-ahead true-peak limiter (our own). Each sample's true peak is estimated by 8x oversampling
// (an 8-phase, 32-tap windowed-sinc interpolator, as in ITU-R BS.1770's true-peak meter); the gain it needs is held over the
// look-ahead window and averaged over the same window (so it is already down when the peak arrives, and never clicks),
// with a smooth release. The ceiling sits a little under -1 dBTP. No allocation after prepare().
#pragma once
#include <array>
#include <vector>

namespace matter
{
class TruePeakLimiter
{
public:
    static constexpr int kTaps = 32, kPhases = 8;   // taps per phase, 8x oversampled peak detection
    void prepare (double sr, float ceilingDb = -1.5f);
    void reset();
    int latency() const { return delayLen; }
    void process (float* L, float* R, int n) noexcept;
    float reduction() const { return lastGain; } // the gain applied last (1 = nothing)
private:
    double sr = 48000;
    float ceiling = 0.86f, releaseCoef = 0.999f, held = 1.0f, lastGain = 1.0f;
    int look = 48;
    std::array<std::array<float, kTaps>, kPhases> phase {};
    std::array<float, kTaps> histL {}, histR {};
    int histPos = 0;
    // look-ahead delay + the min-hold (a ring and a running minimum by brute force over a short window) + box average
    std::vector<float> dL, dR, need, avg;
    int pos = 0, dpos = 0, npos = 0, delayLen = 1;
    double avgSum = 0;
    float truePeak (std::array<float, kTaps>& h, float x) noexcept;
};
}
