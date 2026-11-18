#pragma once

// Fruity Parametric EQ 2's "linear phase" option, rebuilt in docs/analise/REA_rodada50.md (clean-room: behaviour and
// formulas in our own words). The plugin hands it to Frequency Splitter's engine: the EQ's magnitude response is
// sampled on an N/2+1-bin grid, turned into a windowed zero-phase FIR of N/2 taps, and run as a block FFT convolution
// (block N/2, FFT N); every new response cross-fades in over a fixed time. Latency 3N/4 samples.

#include <array>
#include <complex>
#include <memory>
#include <vector>

#include "core/ParametricEqFl.h"

namespace lynx::peqfl
{
// FFT size for the plugin's quality setting (0 = default 2) at the host rate
int linearFftSize (int quality, int hostRate) noexcept;

// The magnitude table (N/2 + 1 amplitudes) the plugin builds from its bands
std::vector<float> linearMagnitudes (const Settings& s, int hostRate, int n);

// The two option fields of a Parametric EQ 2 state the mode uses: FFT quality (state offset 0x135, default 2) and
// cross-fade time index (0x139, default 0: 0.1 s; 1: 0.25 s; 2: 0.01 s; 3: 0.02 s); both exist from state version 4
struct LinearOptions
{
    int quality = 2, smoothing = 0;
};
LinearOptions parseLinearOptions (const std::vector<std::uint8_t>& state);

// The engine's block FFT convolver (shared with Frequency Splitter's linear-phase mode and Maximus's linear-phase band
// split, REA_rodada62): stereo, block n/2, FFT n, the kernel an n/2-tap windowed zero-phase FIR from an n/2 + 1 bin
// magnitude table; output delayed by 3n/4. Computed here in double precision.
// Twiddle factors of the convolver's FFT for one size (ParametricEqLinear.cpp)
struct Twiddles
{
    std::vector<std::complex<double>> forward, inverse;
};

struct FftConvolver
{
    int n = 0, hop = 0, pos = 0;
    std::vector<std::complex<double>> kernel;          // spectrum of the zero-padded FIR
    std::vector<float> in[2], out[2];                  // block input (hop) and overlap-add output (n), per channel
    std::vector<std::complex<double>> work, work2;     // FFT scratch (n), so the audio thread does not allocate
    std::shared_ptr<const Twiddles> twiddles;       // per-size table shared by every convolver of that size
    void init (int size);
    void clear() noexcept;
    void setKernel (const std::vector<float>& table);
    void step (float& l, float& r) noexcept;
    void block() noexcept;
};

class LinearPhase
{
public:
    void setOptions (LinearOptions o) noexcept;
    void prepare (int hostRate) noexcept;
    void reset() noexcept;
    // new settings: a new response is computed and cross-faded in (as FL does on every parameter change)
    void set (const Settings& s);
    // interleaved stereo, in place (main level applied first, as in the plugin)
    void process (float* io, int n) noexcept;
    int latencySamples() const noexcept { return 3 * size / 4; }

private:
    using Convolver = FftConvolver;

    void load() noexcept;

    Settings settings;
    int rate = 44100, size = 2048, quality = 2, smoothingIndex = 0;
    float mainGain = 1.0f, gainNow = 1.0f, gainStepMax = 0.001f;
    bool gainSnap = true;
    bool pending = false, configured = false;
    std::vector<float> table;
    std::array<Convolver, 2> conv;
    std::vector<float> history[2];
    int historyPos = 0, fadeCount = 0, fadeLength = 0, active = 0;
};
} // namespace lynx::peqfl
