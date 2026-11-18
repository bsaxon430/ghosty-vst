#pragma once
// ENGINES (round 3): the internal interface between the OscEngine wrapper (ProEngines.cpp) and the three DSP hosts, each in its own
// translation unit because the vendored modules define the same macro / table names (ProEnginesPlaits.cpp, ProEnginesTexture.cpp,
// ProEnginesResonator.cpp). Plain C++ (no JUCE): the vendored headers are only ever included next to this one.
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

namespace gpro::eng
{
struct CoreParams
{
    float rate = 48000.0f;          // the rate the core runs at (its render() call writes samples at this rate)
    float hz = 261.63f;             // pitch
    float h = 0.5f, t = 0.5f, m = 0.5f, accent = 0.8f;
    bool trigger = false;           // a note started (first block after start())
    float size = 0.5f, pitch = 0.0f, feedback = 0.0f; bool freeze = false;   // TEXTURE
    float position = 0.5f; int model = 0; bool haveInput = false;             // RESONATOR (haveInput: the source is playing)
};

struct Core
{
    // The module code was written for objects in zeroed static memory and leaves some members to that (e.g. the grain player's
    // seeding phase): every core - and every vendored object it holds inline - is built in zeroed memory.
    static void* operator new (std::size_t n) { void* p = ::operator new (n); std::memset (p, 0, n); return p; }
    static void operator delete (void* p) noexcept { ::operator delete (p); }
    virtual ~Core() = default;
    virtual void select (int engine, double rate) = 0;    // an engine of this core was picked (or the rate changed): initialise its state at
                                                          // that rate (the one rateFor() returned), no allocation
    virtual void clear() = 0;                             // a note from silence: clear the running state of the current engine
    virtual double rateFor (int engine, double engineRate) const = 0;   // the rate this core runs that engine at when the voice runs at engineRate
    virtual int blockFor (double rate) const = 0;         // samples per render() call at that rate
    virtual bool takesInput() const = 0;
    virtual void render (const CoreParams& p, const float* in, float* out, int n) = 0;
    std::uint32_t rng = 0x21;                             // this voice's random stream (restored around every render)
};

std::unique_ptr<Core> makePlaitsCore();
std::unique_ptr<Core> makeTextureCore();
std::unique_ptr<Core> makeResonatorCore();
} // namespace gpro::eng
