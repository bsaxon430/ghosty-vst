/*
 * Poltergeist 2 (GhostBlock) - SPECTRAL FX: a rack of spectral effects after the synth.
 *
 *   Six Sines engine (+ MORPH's mix)  ->  SPECTRAL FX  ->  the plugin's output
 *
 * Two sections: the CHAIN, four slots that each hold one of Spectral Corruptor's 38 audio effects (Dirty Octopus,
 * MPL-2.0; its real module code, driven by its own STFT engine), and WARPCORE, WarpCore's spectral inversion
 * (ManasWolrd, GPL-3; its real DSP, zero latency). A master ON and a master wet/dry sit over both, and ORDER says
 * which section comes first. The upstream sources are vendored unmodified in libs/ghostblock-spectral/.
 * Nothing inside the Six Sines engine changes; with SPECTRAL off the plugin's output is not touched at all.
 *
 * This header is all the editor and the CLAP wrapper need (no JUCE, no upstream headers). README.md in this
 * folder is the guide to it.
 *
 * Threads: the main thread owns the settings' meaning (it builds the effect objects and frees the old ones);
 * the audio thread only swaps ready-made objects in and runs them: no allocation, no lock. Every setting is an
 * atomic, so the host's automation (audio thread) and the editor (main thread) can both write.
 */
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <clap/clap.h>

namespace ghostblock::p2::spectral
{
constexpr int kSlots = 4;             // effects in series
constexpr int kHostParamsPerSlot = 8; // a slot's first 8 parameters are the host's (automation); any others are the editor's
constexpr int kMaxParams = 40;        // the most parameters one effect has (Spectral Equalizer: 8 bands x 5)

enum class Kind : int
{
    Float = 0,
    Int,
    Bool,
    Choice
};
// where a chain effect runs. Inside the chain the order is fixed by this, as in Spectral Corruptor's own rack:
// 1. Spectral + Partial effects, in slot order, inside ONE shared STFT (this is what costs latency)
// 2. Time effects (Spectral Corruptor's GENERIC folder), in slot order
enum class Domain : int
{
    None = 0,
    Spectral, // works on FFT frames
    Partial,  // works on frequency slices of FFT frames (Spectral Corruptor's PARTIAL folder)
    Time      // works on samples (Reverb, Delay, Compressor, Distortion, Utility)
};

// one parameter of an effect, exactly as upstream defines it (id, label, range, default, step, unit, choices)
struct ParamDesc
{
    std::string id;   // upstream's parameter id; also the key in the saved state
    std::string name; // upstream's label
    Kind kind{Kind::Float};
    float min{0}, max{1}, def{0}, step{0.01f};
    std::string unit; // " %", " Hz", " dB", ... (with its leading space, as upstream writes it)
    bool logScale{false};
    std::vector<std::string> choices; // Choice only
    bool hostParam{false};            // true for the slot's first kHostParamsPerSlot parameters

    bool stepped() const { return kind != Kind::Float; }
    float clamp(float v) const;
    float toNorm(float v) const;   // real -> 0..1 (what the host automates)
    float fromNorm(float n) const; // 0..1 -> real
    std::string text(float v) const;
    bool parse(const std::string &t, float &v) const;
};

// one effect type
struct TypeDesc
{
    int index{0};
    std::string id;       // "BinShuffle", ... ("" = empty slot); the key in the saved state
    std::string name;     // "Bin Shuffle"
    std::string category; // "DESTRUCTION", "FREQUENCY", ... (Spectral Corruptor's eight folders)
    std::string source;   // "Spectral Corruptor"
    Domain domain{Domain::None};
    std::vector<ParamDesc> params;
    // false: a parameter change re-makes the module off the audio thread and swaps it in (the effect keeps no
    // audio history, so nothing is lost). true: parameters reach the running module directly.
    bool liveParams{true};
};

// the catalogue: built on first use, constant afterwards. Index 0 is the empty slot. The order never changes
// (new effects are appended), because the host automates a slot's effect by this index.
const std::vector<TypeDesc> &types();
const std::vector<std::string> &categories(); // in display order
int typeIndex(const std::string &id);         // -1 = not in this build

// the rack-wide choices (Spectral Corruptor's own STFT settings)
constexpr int kFftSizes = 8; // 128 ... 16384
inline int fftSizeAt(int i) { return 128 << (i < 0 ? 0 : (i >= kFftSizes ? kFftSizes - 1 : i)); }
constexpr int kDefaultFft = 4; // 2048, as upstream
constexpr int kWindows = 9;
const char *windowName(int i); // Hann, Hamming, Blackman, Rectangular, Blackman-Harris, Gaussian, Flat Top, Welch, Triangle
constexpr int kLimits = 7;
float limitHz(int i); // 11025, 16000, 22050 (default), 24000, 44100, 48000, 96000
constexpr int kDefaultLimit = 2;

// WarpCore's nine parameters (its own ids, ranges and defaults), in the order of the host ids pWarpParam(0..8)
constexpr int kWarpParams = 9;
const std::vector<ParamDesc> &warpParams();
bool warpAvailable();        // false when this build has no WarpCore code for the CPU family
const char *warpSimdName();  // "AVX2", "FMA", "SSE4", "NEON", ... (what WarpCore runs on here)

class SpectralFx
{
  public:
    SpectralFx();
    ~SpectralFx();
    SpectralFx(const SpectralFx &) = delete;
    SpectralFx &operator=(const SpectralFx &) = delete;

    // ================================================================================== the host's parameters
    // ids (after the engine's and MORPH's). A slot's 8 generic parameters travel as 0..1; what they mean follows
    // the slot's effect, and paramInfo / valueToText give the host the real name and the real value text.
    static constexpr uint32_t pOn = 910001,       // master: SPECTRAL ON (off = the plugin as it was, no latency)
                              pMix = 910002,      // master wet/dry over both sections, 0..100 %
                              pFft = 910003,      // chain: FFT size, 0..7 = 128..16384
                              pWindow = 910004,   // chain: window, 0..8
                              pOverlap = 910005,  // chain: overlap, 2..16 x
                              pLimit = 910006,    // chain: frequency limit, 0..6
                              pChainMix = 910007, // chain: its own wet/dry, 0..100 % (upstream's equal-power mixer)
                              pOrder = 910008;    // 0 = CHAIN then WARPCORE, 1 = WARPCORE then CHAIN
    static constexpr uint32_t kGlobalParams = 8;
    static constexpr uint32_t kPerSlot = 3 + kHostParamsPerSlot; // effect, on, mix, P1..P8
    static constexpr uint32_t pWarpOn = 910501;                  // WarpCore on
    static constexpr uint32_t pWarpParam(int k) { return 910502u + (uint32_t)k; } // its nine parameters, real units
    static constexpr uint32_t kWarpHostParams = 1 + kWarpParams;
    static constexpr uint32_t kParams = kGlobalParams + kSlots * kPerSlot + kWarpHostParams;
    static constexpr uint32_t slotBase(int s) { return 910100u + 100u * (uint32_t)s; }
    static constexpr uint32_t pSlotType(int s) { return slotBase(s); }
    static constexpr uint32_t pSlotOn(int s) { return slotBase(s) + 1; }
    static constexpr uint32_t pSlotMix(int s) { return slotBase(s) + 2; }
    static constexpr uint32_t pSlotParam(int s, int k) { return slotBase(s) + 10u + (uint32_t)k; }
    static bool isParam(uint32_t id);
    static uint32_t paramIdAt(uint32_t index); // index 0 .. kParams-1
    bool paramInfo(uint32_t index, clap_param_info *info) const;
    double getParam(uint32_t id) const;
    void setParam(uint32_t id, double v); // any thread
    bool valueToText(uint32_t id, double v, char *out, uint32_t n) const;
    bool textToValue(uint32_t id, const char *t, double *v) const;

    // ================================================================================== the editor's API (main thread)
    bool on() const;
    float mix() const; // 0..100 %
    int fftIndex() const;
    int windowIndex() const;
    float overlap() const; // 2..16
    int limitIndex() const;
    float chainMix() const; // 0..100 %
    int order() const;      // 0 = chain first, 1 = WarpCore first
    bool warpOn() const;
    float warpValue(int k) const;
    int slotType(int s) const;          // index into types()
    bool slotOn(int s) const;           // false = this slot is bypassed
    float slotMix(int s) const;         // 0..100 %
    float slotValue(int s, int p) const; // parameter p of the slot's effect, in its real units
    // an edit from the editor: sets it at once (the sound follows) and tells the host. type: 0 begin, 1 value, 2 end
    // (the same gesture protocol as Morph::uiEdit). Values are the host's (see getParam): generic slot params 0..1.
    void uiEdit(uint32_t id, double v, int type);
    void uiSet(uint32_t id, double v)
    {
        uiEdit(id, v, 0);
        uiEdit(id, v, 1);
        uiEdit(id, v, 2);
    }
    // parameter p of slot s in real units; works for every parameter of the effect (the first 8 go to the host as
    // above, the rest are set here and saved with the state: call the editor's markStateDirty after those)
    void uiSlotValue(int s, int p, float real, int type);
    // the slots in the order they really run inside the chain (see Domain)
    std::array<int, kSlots> runOrder() const;
    // bumped whenever something the editor shows may have changed (host automation, a loaded state, a new effect)
    uint32_t changeCount() const;
    // meters (peak, falling): into the rack, out of it, and after each slot
    float inLevel() const;
    float outLevel() const;
    float slotLevel(int s) const;
    // the newest FFT magnitudes of the left channel, before (input = true) or after the spectral slots; false when
    // there is nothing new (or no spectral effect is loaded). `rate` = the sample rate they belong to.
    bool spectrum(std::vector<float> &magnitudes, double &rate, bool input);

    // ================================================================================== the plugin's glue
    void activate(double sampleRate);   // main thread, audio stopped: (re)build for this rate; latches latency()
    uint32_t latency() const noexcept;  // the samples of latency the host was told at activate()
    uint32_t latencyWanted() const noexcept; // what it should be told now; differs from latency() until a restart
    // main thread: build what changed, free what the audio thread gave back. Returns true when the host should
    // re-read parameter names / values (a slot's effect changed).
    bool serviceMainThread();
    bool takeRestartRequest();    // any thread: true once when latencyWanted() != latency() (ask the host to restart)
    bool takeMainThreadRequest(); // any thread: true once when serviceMainThread() has work
    void sendToHost(const clap_output_events *out); // audio thread / flush: the editor's edits, as host events
    void requestReset() noexcept;                   // audio thread: forget tails and history at the next block
    // audio thread, in place on the plugin's stereo output. Off = returns at once, the samples are not read or written.
    void process(float *L, float *R, uint32_t frames, double tempoBpm) noexcept
    {
        if (!inPath.load(std::memory_order_relaxed) && !onFlag.load(std::memory_order_relaxed)) return;
        processImpl(L, R, frames, tempoBpm);
    }

    // ================================================================================== state
    // text, kept after the patch's (and MORPH's) terminating zero in the plugin state. "P2SPECTRAL <version>".
    std::string toState() const;
    bool fromState(const std::string &s); // main thread
    void resetToDefaults();               // main thread: off, empty slots (what a state without SPECTRAL means)

    struct Impl;

  private:
    void processImpl(float *L, float *R, uint32_t frames, double tempoBpm) noexcept;
    std::atomic<bool> onFlag{false}; // SPECTRAL ON
    std::atomic<bool> inPath{false}; // the audio thread has the rack in the signal path (running, or holding latency)
    std::unique_ptr<Impl> impl;
};
} // namespace ghostblock::p2::spectral
