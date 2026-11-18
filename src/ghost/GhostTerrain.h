/*
 * Ghost Bass - a bass instrument built on Surge XT.
 * Copyright 2026 the Ghost Bass authors. Based on Surge XT by the Surge Synth Team.
 * Released under the GNU General Public Licence v3 or later (see LICENSE).
 *
 * TERRAIN (build 7): the landscape only MOVES SURGE'S OWN SOUND. Brandon (2026-11-04): "terrain should
 * never make bass sound worse ... we aren't adding our own sound in there really, we are using theirs and
 * moving it around through terrain." So there is no DSP of ours here any more (the build 4/5 resonator bank
 * and the GRIT / SPARKLE / smear / WOBBLE / TONE chain are gone): the land is read as a few numbers
 * (terrain/TerrainFeatures, GhostBlock's shared landscape code) and those numbers move
 *   - the patch's SCULPT mapping (SHAPE / TWIST / STRETCH / PINCH: its own cutoffs, drive, oscillator
 *     shape / morph, unison, envelopes ... as the preset maps them),
 *   - Surge's macros SUB .. SPACE through Surge's own macro modulation input (MacroModulationSource's
 *     modulation depth, the one CLAP host modulation uses): GRIT = the preset's drive / distortion FX,
 *     BITE = its filter / attack, MOTION = its LFO depths and rates, WIDTH = its unison detune / chorus,
 *     SPACE = its reverb / delay FX sends (the macro's saved value is never changed),
 *   - scene A's filter 1 cutoff / resonance and its waveshaper drive.
 * Surge does all of the sound: its filters, waveshapers, oscillators, LFOs and FX.
 *   depth (z)  time across each note: z runs 0 -> 1 over two beats from every note-on, so a mountain
 *              at the back moves the sound late in the note; FREEZE holds one moment
 *   EVOLVE     a slow drift the audio thread advances itself (40 s a cycle)
 * A flat land moves nothing: every sample bit for bit the patch alone.
 * build 9 (MORE): the land is also read as a FIELD along the frequency axis (field(x) at this moment of the note), and every
 * Surge sound control that can move live and smoothly sits at its own two places on it (GhostEngine::build's overlay), so a
 * mountain here, a valley there, moves a different mix of the sliders. MOVEMENT adds tempo-synced random wandering of the
 * same controls (a smooth step-and-glide per control, kMoveRates beats a step, or FREE in Hz). The drag-speed wobble of
 * build 5 is gone: what you hear while you pull is the settled sound (the same settings, the same smoothing).
 * Threads: set() from any thread (a lock-free triple buffer); tick() on the audio thread (before the
 * voices run); landFeatures() is allocation-free, so the land is read on the audio thread every block.
 */
#ifndef GHOSTBASS_SRC_GHOST_GHOSTTERRAIN_H
#define GHOSTBASS_SRC_GHOST_GHOSTTERRAIN_H

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <string>
#include "terrain/TerrainDsp.h"
#include "terrain/TerrainSerial.h"
#include "terrain/TerrainFeatures.h"
#include <vector>

namespace ghost
{
// what the land moves besides the sculpt gestures (all Surge's own controls)
enum TerrainTarget
{
    TT_MACRO_GRIT = 0, // macro 2: the preset's drive / distortion
    TT_MACRO_BITE,     // macro 3: its filter / attack
    TT_MACRO_MOTION,   // macro 4: its LFO depths / rates
    TT_MACRO_WIDTH,    // macro 5: unison detune / chorus
    TT_MACRO_SPACE,    // macro 6: reverb / delay sends
    TT_CUTOFF,         // scene A filter 1 cutoff (signed)
    TT_RESO,           // scene A filter 1 resonance
    TT_DRIVE,          // scene A waveshaper drive
    n_terrain_targets
};
inline int terrainTargetMacro(int t) { return t <= TT_MACRO_SPACE ? t + 1 : -1; } // macro index (0 = SUB)
inline const char *terrainTargetParam(int t)
{
    switch (t)
    {
    case TT_CUTOFF:
        return "a_filter1_cutoff";
    case TT_RESO:
        return "a_filter1_resonance";
    case TT_DRIVE:
        return "a_ws_drive";
    default:
        return nullptr;
    }
}

// the land read as amounts: the sculpt gestures (pull, squash, twist -1..1, stretch, pinch) on top of
// the EASY sculpt's own, and the targets above (normalised 0..1 offsets; the cutoff is signed)
struct BassLand
{
    float gesture[5]{};
    float target[n_terrain_targets]{};
    bool any{false};
};
inline BassLand mapBass(const terrain::Terrain &t, float z, float motion)
{
    using namespace terrain;
    BassLand B;
    if (t.empty())
        return B;
    const auto f = landFeatures(t, z, motion);
    float maxPlateau = 0, valley = 0;
    for (int i = 0; i < f.count; ++i)
    {
        const float pr = f.m[i].presence;
        maxPlateau = std::max(maxPlateau, f.m[i].plateau * pr);
        if (f.m[i].valley)
            valley = std::max(valley, -f.m[i].height * pr);
    }
    const float spires = clamp01(f.spires / 3.f), plateaus = clamp01(f.plateaus / 1.5f), lean = (f.balance - 0.5f) * 2.f;
    // the sculpt (build 5's mapping, kept)
    B.gesture[0] = clamp01(0.55f * f.mass + 0.3f * clamp01(f.tallest / 1.2f));
    B.gesture[1] = clamp01(0.7f * valley);
    B.gesture[2] = std::clamp(0.8f * lean, -1.f, 1.f);
    B.gesture[3] = clamp01(0.8f * f.spread);
    B.gesture[4] = clamp01(0.6f * f.merges + 0.35f * plateaus);
    // Surge's own controls: rough land = the preset's drive, spires = bite and resonance, a quick drag or
    // EVOLVE = its LFOs, spread = its width, plateaus = its space, the tallest mountain's place = the tone
    auto *T = B.target;
    T[TT_MACRO_GRIT] = 0.5f * clamp01(0.65f * f.jagged + 0.2f * f.mass);
    T[TT_MACRO_BITE] = 0.4f * clamp01(0.6f * spires + 0.3f * clamp01(f.tallest / 1.2f));
    T[TT_MACRO_MOTION] = 0.6f * clamp01(0.9f * f.motion + 0.4f * f.evolve);
    T[TT_MACRO_WIDTH] = 0.5f * clamp01(0.7f * f.spread);
    T[TT_MACRO_SPACE] = 0.45f * clamp01(0.35f * plateaus + 0.5f * maxPlateau);
    T[TT_CUTOFF] = f.tallest > 0.2f ? 0.12f * std::clamp((f.tallestX - 0.5f) * 1.4f, -1.f, 1.f) : 0.f;
    T[TT_RESO] = 0.15f * clamp01(0.45f * spires + 0.25f * clamp01(f.tallest / 1.2f));
    T[TT_DRIVE] = 0.12f * clamp01(0.65f * f.jagged);
    B.any = true;
    return B;
}
// the page's readout: the four strongest things the land is moving
inline std::vector<terrain::Meter> bassMeters(const BassLand &B)
{
    const auto *T = B.target;
    std::vector<terrain::Meter> all = {{"SHAPE +", B.gesture[0]}, {"SHAPE -", B.gesture[1]}, {"TWIST", std::fabs(B.gesture[2])},
                                       {"STRETCH", B.gesture[3]}, {"PINCH", B.gesture[4]}, {"GRIT", 2.f * T[TT_MACRO_GRIT]},
                                       {"BITE", 2.5f * T[TT_MACRO_BITE]}, {"MOTION", T[TT_MACRO_MOTION] / 0.6f},
                                       {"WIDTH", 2.f * T[TT_MACRO_WIDTH]}, {"SPACE", T[TT_MACRO_SPACE] / 0.45f},
                                       {"TONE", std::fabs(T[TT_CUTOFF]) / 0.12f}};
    std::stable_sort(all.begin(), all.end(), [](const terrain::Meter &a, const terrain::Meter &b) { return a.value > b.value; });
    std::vector<terrain::Meter> r;
    for (int i = 0; i < 4; ++i)
        if (all[(size_t)i].value > 0.02f)
            r.push_back(all[(size_t)i]);
    return r;
}

// MOVEMENT: how often the wandering picks a new target (beats a step); the last one is FREE (Hz)
constexpr int kMoveRates = 8, kMoveFree = 7;
inline float moveBeats(int r)
{
    static const float b[7] = {0.25f, 0.5f, 1.f, 2.f, 4.f, 8.f, 16.f};
    return b[std::clamp(r, 0, 6)];
}
inline const char *moveRateName(int r)
{
    static const char *n[kMoveRates] = {"1/16", "1/8", "1/4", "1/2", "1 BAR", "2 BARS", "4 BARS", "FREE"};
    return n[std::clamp(r, 0, kMoveRates - 1)];
}
constexpr float kMoveDefault = 0.5f; // build 9: noticeably livelier than before
constexpr int kMoveRateDefault = 2;  // 1/4
constexpr int kFieldCols = 64;

// a repeatable random number in -1..1 for (control, step)
inline float moveRandom(uint32_t id, uint32_t step, uint32_t seed)
{
    uint32_t h = id * 0x9E3779B1u ^ (step + 0x7F4A7C15u) * 0x85EBCA6Bu ^ seed * 0xC2B2AE35u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return (float)(h & 0xFFFFFFu) / 8388607.5f - 1.f;
}

class TerrainStage
{
  public:
    TerrainStage()
    {
        range.minHz = 120.f;
        range.maxHz = 12000.f;
        range.maxDb = 12.f;
        range.minQ = 0.5f;
        range.maxQ = 30.f;
        share.publish(terrain::Terrain()); // flat until a patch or the page says otherwise
    }

    void setSampleRate(double sr) { sampleRate = sr > 1000 ? sr : 48000.0; }

    // ---- any thread ----
    // the live landscape (the page's drags, a patch load). Keeps the audio thread's EVOLVE phase.
    // (`motion` is kept for old callers and ignored: the drag-speed wobble is gone, a drag is what it will settle to)
    void set(const terrain::Terrain &t, float motion = 0.f)
    {
        (void)motion;
        motion = 0.f;
        const auto m = mapBass(t, -1.f, 0.f);
        {
            std::lock_guard<std::mutex> g(mtx);
            last = t;
            landMeters = bassMeters(m);
        }
        motionNow.store(motion, std::memory_order_relaxed);
        share.publish(t);
    }
    std::vector<terrain::Meter> meters() const
    {
        std::lock_guard<std::mutex> g(mtx);
        return landMeters;
    }
    terrain::Terrain get() const
    {
        std::lock_guard<std::mutex> g(mtx);
        return last;
    }
    // the page's whole state (A / B and which is active), saved with the patch
    void setPageState(const std::string &s)
    {
        std::lock_guard<std::mutex> g(mtx);
        pageState = s;
    }
    std::string getPageState() const
    {
        std::lock_guard<std::mutex> g(mtx);
        return pageState;
    }
    const terrain::Range &getRange() const { return range; }
    // MOVEMENT / CHAOS (saved with the sound): 0 = the shape's fixed mapping only, 1 = wanders as far as the controls' safe range
    void setMovement(float m) { movement.store(std::clamp(m, 0.f, 1.f), std::memory_order_relaxed); }
    float getMovement() const { return movement.load(std::memory_order_relaxed); }
    void setMoveRate(int r) { moveRate.store(std::clamp(r, 0, kMoveRates - 1), std::memory_order_relaxed); }
    int getMoveRate() const { return moveRate.load(std::memory_order_relaxed); }
    void setMoveFreeHz(float h) { moveFreeHz.store(std::clamp(h, 0.05f, 12.f), std::memory_order_relaxed); }
    float getMoveFreeHz() const { return moveFreeHz.load(std::memory_order_relaxed); }
    float phase() const { return evolvePhase.load(std::memory_order_relaxed); } // the audio thread's EVOLVE phase
    float depth() const { return depthNow.load(std::memory_order_relaxed); }    // where we are across the note (0..1)
    bool active() const { return activeNow.load(std::memory_order_relaxed); }   // the land is moving something
    uint64_t loadStamp() const { return stamp.load(std::memory_order_acquire); } // bumps on every patch load
    void bumpStamp() { stamp.fetch_add(1, std::memory_order_acq_rel); }

    // ---- audio thread ----
    void noteOn() { noteFlag.store(true, std::memory_order_release); }
    void setBeatSeconds(double s) { beatSeconds = s > 0.01 ? s : 0.5; } // 60 / bpm (the note's time axis is two beats)
    // once per block, before the voices: read the land at this moment of the note
    void tick(int n)
    {
        terrain::Terrain t;
        if (share.pull(t))
        {
            const float ph = audio.phase;
            audio = t;
            if (t.evolve > 0.f && haveAudio)
                audio.phase = ph; // the drift carries on where the audio thread had it
            haveAudio = true;
        }
        if (noteFlag.exchange(false, std::memory_order_acq_rel))
            noteSeconds = 0.0;
        const double dt = n / sampleRate;
        if (haveAudio && audio.evolve > 0.f)
        {
            terrain::advance(audio, (float)dt);
            evolvePhase.store(audio.phase, std::memory_order_relaxed);
        }
        const float z = (float)std::min(1.0, noteSeconds / (2.0 * beatSeconds));
        noteSeconds += dt;
        depthNow.store(z, std::memory_order_relaxed);
        if (!haveAudio || audio.empty())
        {
            land = BassLand();
        }
        else
        {
            land = mapBass(audio, audio.frozen ? audio.freezeZ : z, motionNow.load(std::memory_order_relaxed));
        }
        activeNow.store(land.any, std::memory_order_relaxed);
        // MOVEMENT clock: steps a second from the DAW tempo (or FREE in Hz); it never jumps when the rate changes
        {
            const int r = moveRate.load(std::memory_order_relaxed);
            const double sps = r >= kMoveFree ? (double)moveFreeHz.load(std::memory_order_relaxed)
                                              : 1.0 / (beatSeconds * (double)moveBeats(r));
            stepPos += dt * sps;
            if (stepPos > 1.0e6)
                stepPos -= 1.0e6;
        }
        // the land as a field along the frequency axis (every fourth block: the controls glide anyway)
        if (haveAudio && !audio.empty())
        {
            if ((fieldTick++ & 3) == 0)
                buildField(audio.frozen ? audio.freezeZ : z);
        }
        else
        {
            if (fieldDirty)
                std::fill(std::begin(cols), std::end(cols), 0.f);
            fieldDirty = false;
        }
    }
    // build 9 (audio thread, after tick()): the land along the frequency axis at this moment of the note, -1.3..1.3, 0 on flat ground
    bool fieldOn() const { return haveAudio && !audio.empty(); }
    float field(float x) const
    {
        const float p = std::clamp(x, 0.f, 1.f) * (kFieldCols - 1);
        const int i = std::min((int)p, kFieldCols - 2);
        const float f = p - (float)i;
        return cols[i] + (cols[i + 1] - cols[i]) * f;
    }
    // a smooth random walk in -1..1 for one control (tempo-synced steps, or FREE), a fresh target every step
    float wander(uint32_t id) const
    {
        const uint32_t k = (uint32_t)stepPos;
        const float f = (float)(stepPos - (double)k);
        const float a = moveRandom(id, k, audio.seed), b = moveRandom(id, k + 1, audio.seed);
        return a + (b - a) * (0.5f - 0.5f * std::cos(3.14159265f * f));
    }
    // what the land asks for right now (audio thread, after tick())
    float gesture(int g) const { return g >= 0 && g < 5 ? land.gesture[g] : 0.f; }
    float target(int t) const { return t >= 0 && t < n_terrain_targets ? land.target[t] : 0.f; }
    bool any() const { return land.any; }
    void reset() { noteSeconds = 1e9; }

  private:
    // each mountain's reach along the frequency axis at depth z (a smooth bell, never narrower than ~3% so that a thin spire
    // still moves the few controls near it); valleys pull the other way
    void buildField(float z)
    {
        fieldDirty = true;
        float acc[kFieldCols] = {};
        for (int i = 0; i < audio.size(); ++i)
        {
            const terrain::Peak pk = audio.peakAt(i, z);
            const float w = std::max(2.f * pk.width, 0.06f), d = std::max(2.f * pk.depth, 0.06f);
            const float dz = (z - pk.z) / d, dz2 = dz * dz;
            if (dz2 >= 2.5f)
                continue;
            for (int c = 0; c < kFieldCols; ++c)
            {
                const float dx = ((float)c / (kFieldCols - 1) - pk.x) / w;
                const float t = 1.f - (dx * dx + dz2) * 0.4f;
                if (t > 0.f)
                    acc[c] += pk.height * t * t * (3.f - 2.f * t);
            }
        }
        for (int c = 0; c < kFieldCols; ++c)
            cols[c] = std::clamp(acc[c], -1.3f, 1.3f);
    }
    float cols[kFieldCols] = {};
    bool fieldDirty{false};
    uint32_t fieldTick{0};
    double stepPos{0.0};
    std::atomic<float> movement{kMoveDefault}, moveFreeHz{0.5f};
    std::atomic<int> moveRate{kMoveRateDefault};
    terrain::Range range;
    terrain::TerrainShare share;
    std::vector<terrain::Meter> landMeters;
    terrain::Terrain audio;
    BassLand land;
    bool haveAudio{false};
    double sampleRate{48000.0}, noteSeconds{1e9}, beatSeconds{0.5};
    std::atomic<float> evolvePhase{0.f}, depthNow{1.f}, motionNow{0.f};
    std::atomic<bool> activeNow{false};
    std::atomic<bool> noteFlag{false};
    std::atomic<uint64_t> stamp{0};
    mutable std::mutex mtx;
    terrain::Terrain last;
    std::string pageState;
};
} // namespace ghost

#endif
