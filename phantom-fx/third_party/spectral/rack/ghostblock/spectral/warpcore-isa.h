/*
 * Poltergeist 2 (GhostBlock) - SPECTRAL FX: one plain table per SIMD build of WarpCore's DSP.
 *
 * WarpCore ships its DSP once per instruction set (SSE2, SSE4.1, AVX, AVX2, AVX2+FMA, NEON) and picks one at run time.
 * Each of our warpcore-isa-*.cpp compiles one upstream variant, unmodified, inside its own namespace (so nothing
 * compiled for AVX can ever be linked into code that runs on a CPU without it) and exports this table.
 * warpcore-dispatch.cpp picks the table for the CPU (upstream's own picker needs an unlicensed library; see
 * libs/ghostblock-spectral/README.md).
 */
#pragma once
namespace ghostblock::p2::spectral::detail
{
struct WarpParams // warpcore::Param, field for field
{
    int bands{50};
    float fHigh{20010.f};
    float scale{1.f};
    int poles{2};
    float pitch{0.f};
    float dryWet{1.f};
    bool pitchAffect{true};
    bool fillGap{false};
    int freqMode{2};
};
struct WarpIsa
{
    const char *name;
    void *(*create)(); // not on the audio thread
    void (*destroy)(void *);
    void (*init)(void *, float sampleRate);
    void (*reset)(void *);
    void (*update)(void *, const WarpParams &);
    void (*process)(void *, float *left, float *right, int frames);
};
const WarpIsa *warpIsa();                     // the best one this CPU runs; nullptr = none in this build
const WarpIsa *warpIsaNamed(const char *name); // tests: "SSE2", "SSE4", "AVX", "AVX2", "FMA", "NEON"
} // namespace ghostblock::p2::spectral::detail
