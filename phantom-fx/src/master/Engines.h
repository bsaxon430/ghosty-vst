// SPDX-License-Identifier: AGPL-3.0-or-later
// Phase 6 engine hosts. Each open-source engine is compiled unchanged in its own source file (the only file that sees its headers);
// these small boxes are the glue the Phantom FX units call. THIRD_PARTY.md lists the pins and licences, DECISIONS.md 35-41 the choices.
#pragma once
#include <memory>
#include <cstddef>

namespace phantom
{
// GHOST SQUEEZE: the 3-band OTT compressor of the vitOTTx project (GPL-3.0; Vital's MultibandCompressor, src/master/squeeze.cpp)
struct SqueezeSettings
{
    float upward = 1.0f, downward = 1.0f, depth = 1.0f;         // vitOTTx's own multipliers on its band ratios
    float attack = 0.5f, release = 0.5f;                        // vitOTTx's 0..1 times
    float lowGainDb = 16.3f, midGainDb = 11.7f, highGainDb = 16.3f;
    float lowHz = 120.0f, highHz = 2500.0f;
};
class SqueezeBox
{
public:
    SqueezeBox(); ~SqueezeBox();
    void prepare (double sampleRate);
    void reset();
    void set (const SqueezeSettings&) noexcept;
    void process (float* l, float* r, int n) noexcept;          // stereo in place, any length
private:
    struct Impl; std::unique_ptr<Impl> d;
};

// GHOST LIMITER engines behind one interface (the brief's challenger procedure). Both own their oversampling and true-peak stage.
struct LimiterSettings
{
    float ceilingDb = -0.3f, releaseMs = 80.0f, lookaheadMs = 2.0f;
    bool adaptive = true, truePeak = true;
    int oversampling = 2;                                       // 0..5 = 1x, 2x, 4x, 8x, 16x, 32x
};
class LimiterEngine
{
public:
    virtual ~LimiterEngine() = default;
    virtual void prepare (double sampleRate, int maxBlock) = 0;
    virtual void reset() = 0;
    virtual void set (const LimiterSettings&) noexcept = 0;     // audio thread; an oversampling change takes effect at the next reset()
    virtual void process (float* l, float* r, int n) noexcept = 0;
    virtual int latencyFor (int oversampling) const = 0;        // fixed for a given oversampling choice (lookahead does not move it)
};
std::unique_ptr<LimiterEngine> makeZlLimiter();                 // ZL Limiter (AGPL-3.0), src/master/zl_limiter.cpp
std::unique_ptr<LimiterEngine> makeLspLimiter();                // LSP dsp-units Limiter (LGPL-3.0), src/master/lsp_limiter.cpp

// WARP challenger: Rubber Band (GPL-2.0-or-later) in real-time pitch-shift mode, fixed latency (src/rhythm/rubberband.cpp)
class RubberBandBox
{
public:
    RubberBandBox(); ~RubberBandBox();
    void prepare (double sampleRate, int maxBlock);
    void reset();
    int latency() const noexcept;
    int inputLatency() const noexcept;                          // the library's own start delay (the bend lane reads its phase that far back)
    void setPitch (double ratio, bool keepFormants) noexcept;
    void process (const float* const* in, float* const* out, int n) noexcept;   // stereo, n <= maxBlock; out = in delayed by latency(), pitched
private:
    struct Impl; std::unique_ptr<Impl> d;
};
} // namespace phantom
