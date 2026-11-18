#pragma once
// TERRAIN -> audio. GhostBlock's own code (closed). Three pieces every TERRAIN page uses:
//
//   TerrainShare   hands a Terrain (a small POD) from the message thread to the audio thread without locks or allocation
//                  (a triple buffer: the writer never waits, the reader always gets the newest whole terrain).
//   TerrainBank    the realtime sound of a landscape: one resonant band per mountain / valley (a parallel bandpass bank:
//                  out = in + sum (gain - 1) * saturate (bandpass (in))), coefficients ramped sample by sample so fast drags
//                  never click or zipper, per-band harmonic drive from the roughness, the time axis (z) set per block by the
//                  host (where we are in the note / the take). No allocation after prepare().
//   sculpt()       the offline render (Ghost Lab, Ghost Slice): a time-varying spectral filter that follows the landscape's
//                  slice at each moment (what you see is the gain curve), then the bank for the ringing and the grit.
//   SpectrumTap    the analyzer: the audio thread drops samples in, the message thread turns them into dB across x.
//   TerrainLive    the live sound of a landscape beyond an EQ: a small realtime chain the plug-ins' mappings drive from the
//                  land's features (drive, a stutter that shatters, an octave shimmer, a wash that smears and freezes,
//                  wobble, tilt, stereo spread kept mono below ~150 Hz, a peak ceiling). Every amount is smoothed (no
//                  zipper, no clicks on a fast drag); all zero = the sound passes untouched, bit for bit. No allocation
//                  after prepare(). Offline plug-ins play their take through it (plus the bank) while you drag, and
//                  cross-fade to the exact render once it is ready.
//   LiveShare      the LiveParams hand-off, message -> audio (the same triple buffer).
#include "TerrainMapping.h"
#include <atomic>
#include <array>
#include <vector>

namespace ghost::terrain
{
// ------------------------------------------------------------------------------------------------ hand-off
class TerrainShare
{
public:
    TerrainShare();
    void publish (const Terrain& t) noexcept;     // message thread (any single writer)
    bool pull (Terrain& out) noexcept;            // audio thread (single reader): true when a newer terrain arrived
private:
    std::array<Terrain, 3> slots;
    std::atomic<int> middle { 1 };                // index of the middle slot | 4 when it holds something new
    int back = 0, front = 2;
};

// ------------------------------------------------------------------------------------------------ realtime bank
class TerrainBank
{
public:
    static constexpr int kBands = Terrain::kMaxPeaks, kMaxChannels = 2;

    void prepare (double sampleRate, int maxChannels);
    void reset() noexcept;                         // clears the filters (a new note / a new sound)
    void setRange (const Range& r) noexcept { range = r; }

    // audio thread, per block (none of these allocate)
    void setTerrain (const Terrain& t) noexcept { terrain = t; haveTerrain = true; }
    void setDepth (float z) noexcept { depth = z; }                 // the moment along the time axis for the next block
    void setAmount (float a) noexcept { amount = a; }               // 0 = untouched, 1 = the full landscape (ramped)
    void setResonanceOnly (bool on) noexcept { resonanceOnly = on; } // offline use: boosts ring, cuts are left to the spectral stage
    void process (float* const* channels, int numChannels, int numSamples) noexcept;   // in place

    // what the bank is doing right now (tests, readouts)
    int activeBands() const noexcept { return active; }
    float bandHz (int i) const noexcept { return i < kBands ? state[(size_t) i].hz : 0.0f; }

private:
    struct BandState
    {
        // coefficients now and their per-sample step (ramped over the block)
        float b0 = 0, b2 = 0, a1 = 0, a2 = 0, db0 = 0, db2 = 0, da1 = 0, da2 = 0;
        float mix = 0, dmix = 0;            // (gain - 1), ramped
        float drive = 0, ddrive = 0;        // 0..1, ramped
        float hz = 0;
        float z1[kMaxChannels] = {}, z2[kMaxChannels] = {};
        bool on = false;
    };
    std::array<BandState, kBands> state;
    Range range;
    Terrain terrain;
    bool haveTerrain = false, resonanceOnly = false;
    double sampleRate = 48000.0;
    float depth = 0.5f, amount = 1.0f, amountNow = 0.0f;
    int active = 0;
    void targets (int numSamples) noexcept;   // the next block's coefficient targets from the terrain at `depth`
};

// ------------------------------------------------------------------------------------------------ offline
struct SculptOptions
{
    Range range;
    float strength = 1.0f;        // 0..1 how much of the landscape's dB to apply
    int fftOrder = 11;            // 2048 bins at 44.1 / 48 k
    bool resonate = true;         // run the bank after the spectral stage (the ringing + the grit)
    float resonanceDb = 0.5f;     // the bank's share of a mountain's height (0.5 = half its dB, on top of the spectral boost)
};

// in[ch][n] -> out[ch][n] (out may alias in). Output is time-aligned with the input. Message / worker thread (allocates).
void sculpt (const Terrain& t, const float* const* in, float* const* out, int channels, int numSamples, double sampleRate, const SculptOptions& o = {});

// the landscape's gain in dB at `hz` at the moment z, exactly what sculpt() applies (tests)
float sculptGainDb (const Terrain& t, float hz, float z, const SculptOptions& o = {});

// ------------------------------------------------------------------------------------------------ the live chain
struct LiveParams
{
    float drive = 0.0f;        // 0..1 saturation
    float shatter = 0.0f;      // 0..1 how often a slice repeats (a stutter / glitch)
    float shatterSize = 0.3f;  // 0..1 slice length 20 .. 250 ms
    float reverse = 0.0f;      // 0..1 how many repeats play backwards
    float shimmer = 0.0f;      // 0..1 an octave-up grain spray
    float smear = 0.0f;        // 0..1 a diffuse wash
    float freeze = 0.0f;       // 0..1 how long the wash holds (1 = for ever)
    float wobble = 0.0f;       // 0..1 tremolo + a little vibrato
    float wobbleHz = 3.0f;     // its rate
    float tilt = 0.0f;         // -1 darker .. +1 brighter
    float width = 0.0f;        // 0..1 stereo spread (above ~150 Hz only)
    float gainDb = 0.0f;       // output trim
    bool operator== (const LiveParams& o) const;
    bool isZero() const;       // nothing to do (the chain passes the sound untouched)
};

class TerrainLive
{
public:
    void prepare (double sampleRate, int maxChannels);   // allocates (message thread / prepareToPlay)
    void reset() noexcept;
    void setParams (const LiveParams& p) noexcept { target = p; }
    void setSeed (uint32_t s) noexcept { rng = s * 2654435761u + 1u; }
    void process (float* const* channels, int numChannels, int numSamples) noexcept;   // audio thread, in place
    const LiveParams& current() const noexcept { return now; }
    bool isIdle() const noexcept { return idle; }                 // nothing is being done to the sound right now
private:
    static constexpr int kMaxCh = 2;
    double sr = 48000.0;
    int chans = 2;
    LiveParams target, now;
    bool idle = true;
    uint32_t rng = 1;
    // stutter / shimmer: a ring of the last ~2 s
    std::vector<float> ring[kMaxCh];
    int ringLen = 1, ringPos = 0;
    int sliceLeft = 0, sliceLen = 1, sliceRead = 0, sliceStart = 0; bool sliceRepeat = false, sliceBack = false;
    float sliceGain = 0.0f;
    double grainPhase = 0.0;
    // the wash: four delay lines (a feedback network)
    std::vector<float> fdn[4];
    int fdnLen[4] {}, fdnP[4] {};
    float fdnLp[4] {};
    // wobble, tilt, width
    double wobPhase = 0.0;
    std::vector<float> vib[kMaxCh];
    int vibPos = 0;
    float tiltLp[kMaxCh] {}, sideHp1 = 0, sideHp2 = 0, sideX1 = 0, sideX2 = 0, decor[3] {};
    float env = 0.0f;
    float rand01() noexcept { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (float) (rng & 0xffffff) / (float) 0xffffff; }
};

class LiveShare
{
public:
    void publish (const LiveParams& p) noexcept;
    bool pull (LiveParams& out) noexcept;
private:
    std::array<LiveParams, 3> slots {};
    std::atomic<int> middle { 1 };
    int back = 0, front = 2;
};

// ------------------------------------------------------------------------------------------------ analyzer
class SpectrumTap
{
public:
    static constexpr int kSize = 2048;
    void push (const float* const* channels, int numChannels, int numSamples) noexcept;   // audio thread
    // message thread: the latest spectrum as dB (0 = full scale) at `points` evenly spaced x (log frequency); false if nothing yet
    bool read (std::vector<float>& dbAcrossX, int points, const Range& r, double sampleRate, float fallDbPerCall = 2.0f);
private:
    std::array<float, kSize * 4> ring {};
    std::atomic<int> writePos { 0 };
    std::vector<float> work, mags, held;
};
}
