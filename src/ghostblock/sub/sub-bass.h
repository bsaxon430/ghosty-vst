/*
 * Poltergeist 2 (GhostBlock) - SUB: a sub-bass layer under the Six Sines engine.
 *
 *   notes ---> Six Sines engine (+ MORPH) ---> SPECTRAL FX ---> [MAKE ROOM: low cut] ---(+)---> the plugin's output
 *     |                                                                              ^
 *     +------> SUB: Bombaz voice (CLEAN | DIRTY: drive + Subhoofer harmonics) -------+   (mono, delayed like the FX)
 *
 * The sub is made by Bombaz's own synthesis code (window-function pulses; libs/ghostblock-sub/bombaz, unmodified,
 * hosted by bombaz-voice.h) and dirtied by the harmonic algorithms of Subhoofer (subhoofer-port.h, a C++ port checked
 * against upstream's Rust). It follows the note the engine plays - the lowest held key, its tuning, pitch bend and
 * glide, its volume envelope and velocity - and joins the output AFTER the engine's own distortion and after
 * SPECTRAL FX, so nothing in the plugin can smear or distort it. With SUB off the output is not read or written.
 *
 * This header is all the editor and the CLAP wrapper need (no JUCE, no upstream headers). README.md in this folder
 * is the guide to it.
 *
 * Threads: every setting is an atomic (the host's automation on the audio thread and the editor on the main thread
 * both write). The audio thread never allocates or locks.
 */
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <clap/clap.h>

namespace ghostblock::p2::sub
{
// one host parameter, as the editor shows it
struct ParamDesc
{
    uint32_t id{0};
    const char *key{""};  // the word in the saved state
    const char *name{""}; // the host's name ("Sub Level")
    const char *label{""}; // the panel's label ("LEVEL")
    float min{0}, max{1}, def{0};
    bool stepped{false};
};

constexpr int kModes = 5;   // Subhoofer's named algorithms: A Bass 3, A Bass 2, 8 Harmonic Stack, Duro Console, TanH Transfer
constexpr int kPresets = 8; // PURE, DEEP, 808, ANALOG, REESE BODY, SPEAKER, RUMBLE, MONSTER
const char *modeName(int mode);      // upstream's name ("8 Harmonic Stack")
const char *modeShortName(int mode); // for a small plate ("8 STACK")
const char *presetName(int preset);

class SubBass
{
  public:
    SubBass();
    ~SubBass();
    SubBass(const SubBass &) = delete;
    SubBass &operator=(const SubBass &) = delete;

    // ================================================================================== the host's parameters
    // ids (after the engine's, MORPH's and SPECTRAL FX's); all automatable, all in real units
    static constexpr uint32_t pOn = 920001,        // SUB on (off = the plugin exactly as it was)
                              pLevel = 920002,     // dB, -24 .. +6
                              pOctave = 920003,    // -2, -1, 0 (below the note the engine plays)
                              pWidth = 920004,     // %, Bombaz's WIDTH: 0 = the widest pulse (purest), 100 = 3 ms (brightest)
                              pDrive = 920005,     // %, Bombaz's DRIVE (the DIRTY side only)
                              pHarmonics = 920006, // %, how much Subhoofer's algorithm adds (the DIRTY side only)
                              pMode = 920007,      // 0 .. 4, which Subhoofer algorithm
                              pBlend = 920008,     // %, 0 = CLEAN only, 100 = DIRTY only
                              pGlide = 920009,     // ms, Bombaz's GLIDE (under 10 = off)
                              pTune = 920010,      // cents, -100 .. +100
                              pPhase = 920011,     // 0 = RESTART (every new note begins a new pulse), 1 = FREE (it runs on)
                              pRoom = 920012;      // %, MAKE ROOM: 0 = off, else the engine + FX are cut below the sub
    static constexpr uint32_t kParams = 12;
    static bool isParam(uint32_t id) { return id >= pOn && id < pOn + kParams; }
    static uint32_t paramIdAt(uint32_t index) { return pOn + index; }
    static const ParamDesc &desc(uint32_t id);
    bool paramInfo(uint32_t index, clap_param_info *info) const;
    double getParam(uint32_t id) const;
    void setParam(uint32_t id, double v); // any thread
    bool valueToText(uint32_t id, double v, char *out, uint32_t n) const;
    bool textToValue(uint32_t id, const char *t, double *v) const;

    // ================================================================================== the editor's API (main thread)
    bool on() const { return onFlag.load(std::memory_order_relaxed); }
    // an edit from the editor: sets it at once and tells the host. type: 0 begin, 1 value, 2 end (as Morph / SpectralFx)
    void uiEdit(uint32_t id, double v, int type);
    void uiSet(uint32_t id, double v)
    {
        uiEdit(id, v, 0);
        uiEdit(id, v, 1);
        uiEdit(id, v, 2);
    }
    void uiPreset(int preset);  // one of the eight: sets WIDTH .. MAKE ROOM (not LEVEL) and turns SUB on
    int currentPreset() const;  // the preset the settings are, or -1
    uint32_t changeCount() const; // moves when the host / a loaded state changed something
    float level() const;        // the sub's own peak (falling), for the lamp
    float pitchHz() const;      // what it plays now (0 = silent)
    // the sub's signal for the scope / SPYGLASS (mono, the same clock as the engine's output ring). One reader.
    void tapOpen(bool open);
    uint32_t tapRead(float *dst, uint32_t max);

    // ================================================================================== the plugin's glue
    void activate(double sampleRate); // main thread, audio stopped
    void requestReset() noexcept;      // any thread: silence at the next block
    void sendToHost(const clap_output_events *out); // audio thread / flush: the editor's edits, as host events
    // what the engine plays, read by the wrapper from the engine's voices after each engine block
    struct Track
    {
        bool sounding{false};   // there is a voice to follow (held or still ringing out)
        int key{-1};            // its key
        float hz{0};            // its pitch as the engine tunes the key (MTS-ESP, the patch's TUNE and OCTAVE)
        float modSemis{0};      // what moves it: pitch bend, the engine's own glide, MPE / note-expression tuning
        bool engineGlides{false}; // the engine's GLIDE is moving the note now
        float env{0};           // its volume envelope times its velocity factor, 0 .. 1
        uint32_t attack{0};     // counts the notes that began from nothing (no other key down)
    };
    bool wantsTrack() const noexcept { return onFlag.load(std::memory_order_relaxed) || inPath.load(std::memory_order_relaxed); }
    void track(uint32_t frame, const Track &t) noexcept; // audio thread: from this frame of the block on
    // audio thread, in place on the plugin's stereo output, after SPECTRAL FX. `delay` = the latency the FX rack
    // reports (the sub waits as long). Off = returns at once, the samples are not read or written.
    void process(float *L, float *R, uint32_t frames, uint32_t delay) noexcept
    {
        if (!inPath.load(std::memory_order_relaxed) && !onFlag.load(std::memory_order_relaxed))
        {
            nCtl = 0;
            return;
        }
        processImpl(L, R, frames, delay);
    }

    // ================================================================================== state
    // text, after the patch's, MORPH's and SPECTRAL FX's sections in the plugin state. "P2SUB <version>".
    std::string toState() const;
    bool fromState(const std::string &s); // main thread
    void resetToDefaults();               // main thread: off (what a state without SUB means)

    struct Impl;

  private:
    void processImpl(float *L, float *R, uint32_t frames, uint32_t delay) noexcept;
    std::atomic<bool> onFlag{false}; // SUB ON
    std::atomic<bool> inPath{false}; // the audio thread still has it in the signal path (fading out)
    uint32_t nCtl{0};                // (audio thread) control points of the block being made
    std::unique_ptr<Impl> impl;
};
} // namespace ghostblock::p2::sub
