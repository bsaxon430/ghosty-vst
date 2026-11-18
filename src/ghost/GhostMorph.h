/*
 * Ghost Bass - a bass instrument built on Surge XT.
 * Copyright 2026 the Ghost Bass authors. Based on Surge XT by the Surge Synth Team.
 * Released under the GNU General Public Licence v3 or later (see LICENSE).
 *
 * MORPH (build 7): blend two sounds, A and B, with Surge's own engine.
 *   TRUE MORPH   when A and B share their structure (the same oscillator / filter / waveshaper / FX
 *                types, the same modulation routings, the same wavetables and sounds), the morph
 *                patch is A and every continuous control glides between A's and B's value (plus the
 *                macros). Surge smooths each parameter itself.
 *   SCENE BLEND  otherwise A plays in Surge's scene A and B in scene B (Surge's own Dual scene mode, B's
 *                scene and insert FX copied over by GhostPatchTools::morphPatch), and the blend is an
 *                equal-power crossfade of the two scene volumes.
 * What moves the blend:
 *   BLEND        the slider (0 = A, 1 = B)
 *   PENDULUM     a swing between A and B synced to the DAW tempo (1/16 .. 8 bars, straight / dotted /
 *                triplet) or free-running in Hz; shapes SMOOTH swing, TRIANGLE, JUMP (square) and RANDOM;
 *                SWING sets how far it goes from the slider towards the swing's value
 *   DRAW         your own A <-> B curve over 1 .. 8 bars, held in Surge's MSEGStorage and played by
 *                Surge's own MSEG engine (Surge::MSEG::valueAt; a freehand drawing becomes segments with
 *                Surge's freehandRDP fit)
 * BLEND BOARD (build 11): up to four sounds sit in the corners of a board; a puck you drag (or a host / DAW write) sets weights
 *   by its distance to each sound. Two sounds use the TRUE MORPH / SCENE BLEND above with BLEND = the second sound's weight; three or
 *   four use M_BOARD: the patch is the first sound and every continuous control that the sounds share (the same module built the
 *   same way) glides between the sounds' values by the weights (modules built differently stay as the first sound has them).
 *   The weights pass the same 35 ms one-pole, so a drag never zippers and what you hear while dragging is what it settles to.
 * The blend that reaches the sound passes a 35 ms one-pole (JUMP and RANDOM 40 ms), so nothing
 * clicks (build 8's click test). Audio thread: no allocation (the morph set and the drawn curve are immutable once published,
 * and retired a few blocks later).
 */
#ifndef GHOSTBASS_SRC_GHOST_GHOSTMORPH_H
#define GHOSTBASS_SRC_GHOST_GHOSTMORPH_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "SurgeStorage.h"
#include "dsp/modulators/MSEGModulationHelper.h"

class SurgeSynthesizer;
class TiXmlElement;

namespace ghost
{
class Morph
{
  public:
    enum Mode
    {
        M_OFF = 0,
        M_PARAMS,
        M_SCENES,
        M_BOARD
    };
    // BLEND BOARD: four corners, a puck, weights (message / audio threads: plain functions, no allocation)
    static constexpr int kCorners = 4;
    static void cornerPos(int c, float &x, float &y);
    // the puck at (px, py) on the 0..1 board: normalised weights of the filled corners (inverse distance squared, with a small
    // plateau around each corner so a puck sitting on a sound plays that sound alone). Nothing filled: all 0.
    static void boardWeights(const bool filled[kCorners], float px, float py, float *out);
    enum Shape
    {
        SH_SMOOTH = 0,
        SH_TRIANGLE,
        SH_JUMP,
        SH_RANDOM,
        n_shapes
    };
    enum Feel
    {
        F_STRAIGHT = 0,
        F_DOTTED,
        F_TRIPLET,
        n_feels
    };
    static constexpr int kRates = 9; // 1/16 1/8 1/4 1/2 1 bar 2 bars 4 bars 8 bars ... (index)
    static const char *rateName(int i);
    static const char *shapeName(int i);
    static const char *feelName(int i);
    static double rateBeats(int rate, int feel); // beats per swing cycle

    // the patch's morph (saved in <ghostbass><morph>)
    struct Info
    {
        int mode{M_OFF};
        std::string pathA, nameA, pathB, nameB;
        std::vector<std::pair<std::string, float>> bValues; // TRUE MORPH: B's value of each control that differs (raw)
        float macroB[8]{};
        bool haveMacroB{false};
        // BLEND BOARD: the patch came from the board (corner slots, the puck)
        bool board{false};
        bool filled[kCorners]{};
        std::string cpath[kCorners], cname[kCorners];
        float px{0.5f}, py{0.5f};
        int nBoard{0}; // M_BOARD: how many sounds
        std::string boardVals, boardMacros; // M_BOARD: the saved values (name=v0,v1,v2,v3;...), macros (v0,v1,v2,v3;... per macro)
    };

    Morph();
    ~Morph();

    // ---- live controls (any thread) ----
    std::atomic<float> blend{0.f};
    std::atomic<bool> pendOn{false}, pendFree{false}, drawOn{false};
    std::atomic<int> pendRate{4}, pendFeel{F_STRAIGHT}, pendShape{SH_SMOOTH}, drawBars{1};
    std::atomic<float> pendHz{0.5f}, swing{1.f};
    // BLEND BOARD: the puck, and the weights the message thread computed from it (the audio thread glides to them)
    std::atomic<float> puckX{0.5f}, puckY{0.5f};
    std::atomic<float> bw[kCorners]{{0.f}, {0.f}, {0.f}, {0.f}};
    float weightNow(int c) const { return nowW[c].load(std::memory_order_relaxed); }
    // what the sound is at right now (0 = A .. 1 = B), and the swing's phase (for the page)
    float now() const { return nowBlend.load(std::memory_order_relaxed); }
    float phase() const { return nowPhase.load(std::memory_order_relaxed); }
    int mode() const { return modeNow.load(std::memory_order_relaxed); }
    Info info() const;

    // DRAW: the curve (Surge MSEG storage, 0 = A .. 1 = B as its value 0..1); message thread
    void setCurve(const MSEGStorage &ms);
    MSEGStorage curve() const;
    static MSEGStorage defaultCurve();
    // a freehand drawing (t 0..1 across the loop, v 0..1) -> the curve, through Surge's own fit
    static MSEGStorage curveFromDrawing(const std::vector<std::pair<float, float>> &pts);
    static float curveAt(const MSEGStorage &ms, double phase01); // Surge's MSEG engine, for the page

    // ---- patch (patch-load / message thread) ----
    void onPatchLoad(SurgeSynthesizer *s, TiXmlElement *morph); // nullptr: the patch has no morph
    void onPatchSave(TiXmlElement &gb) const;

    // ---- audio thread ----
    // applied: the sculpt's current offset per parameter id (so a morphed control keeps its sculpt)
    void preBlock(SurgeSynthesizer *s, const float *applied, int nApplied);

  private:
    struct Set
    {
        int mode{M_OFF};
        struct P
        {
            int id;
            float a, b; // 0..1
            float last{-1.f};
        };
        std::vector<P> params;
        float macroA[8]{}, macroB[8]{};
        bool macros{false};
        // M_BOARD
        struct PB
        {
            int id;
            float v[kCorners];
            float last{-1.f};
        };
        std::vector<PB> bparams;
        float macroC[kCorners][8]{};
        bool bmacros{false};
        float lastW[kCorners]{-1.f, -1.f, -1.f, -1.f};
        int volA{-1}, volB{-1};
        float volA0{1.f}, volB0{1.f}; // the scenes' own volume (Surge's amplitude units, 0..1)
        float lastBlend{-1.f};
        Info info;
    };
    struct Curve
    {
        MSEGStorage ms;
    };
    template <typename T> struct Slot
    {
        std::atomic<T *> live{nullptr};
        std::vector<std::pair<std::unique_ptr<T>, uint64_t>> retired;
    };
    template <typename T> void publish(Slot<T> &sl, std::unique_ptr<T> v);
    template <typename T> void reap(Slot<T> &sl);

    mutable std::mutex mtx;
    Slot<Set> set;
    Slot<Curve> crv;
    std::atomic<uint64_t> blocks{0};
    std::atomic<float> nowBlend{0.f}, nowPhase{0.f};
    std::atomic<float> nowW[kCorners]{{0.f}, {0.f}, {0.f}, {0.f}};
    float sw[kCorners]{};
    bool swInit{false};
    std::atomic<int> modeNow{M_OFF};
    // audio-thread state
    double freePhase{0.0};
    float sm{0.f}, slew{0.f};
    bool smInit{false};
    float rndFrom{0.f}, rndTo{0.f};
    int lastCycle{-1};
    uint32_t rng{0x2545F491u};
    Surge::MSEG::EvaluatorState eval;
};
} // namespace ghost

#endif
