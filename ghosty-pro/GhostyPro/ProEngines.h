#pragma once
// ENGINES (round 3, item 5): extra oscillator types for Ghosty Pro, built on the MIT-licensed DSP of the STM32 eurorack modules
// (third_party/eurorack-mit, see THIRD_PARTY_LICENSES.md). They appear in each OSC's table menu after the wavetables and are
// played per voice and per oscillator by an OscEngine. Names say what an engine does; no module names in the UI.
//
//   synthesis engines (macro oscillators, run at the voice's engine rate: Draft / High / Ultra = 1x / 2x / 4x):
//     VA, WAVESHAPE, FM 2-OP, ADDITIVE, GRAIN FORMANT, SWARM, STRING, MODAL, CHORD, SPEECH, PARTICLE
//   processors (fed by the OTHER oscillator or a wavetable picked in SOURCE):
//     TEXTURE   - a granular cloud (1 s buffer per voice, 32 kHz inside)
//     RESONATOR - a tuned modal / string resonator (48 kHz inside); with no source signal it plucks itself
//
// Every engine has three controls (HARMONICS / TIMBRE / MORPH, parameters oN_eh / oN_et / oN_em, mod targets "Osc n Engine H/T/M"),
// smoothed per sample. TEXTURE / RESONATOR have a few more (oN_esrc, oN_tsize, oN_tpitch, oN_tfb, oN_tfrz, oN_rpos, oN_rmodel).
// The drum engines of the same code base are not in Ghosty Pro (meant for Ghosty Beats later).
#include <juce_core/juce_core.h>
#include <memory>

namespace gpro::eng
{
enum Id { eVA = 0, eWaveshape, eFm2, eAdditive, eGrain, eSwarm, eString, eModal, eChord, eSpeech, eParticle, eTexture, eResonator, numEngines };

const juce::StringArray& engineNames();            // menu names, in Id order
const juce::StringArray& tableChoiceNames();       // the OSC table menu: every wavetable (tableNames()), then the engines
const juce::StringArray& sourceNames();            // TEXTURE / RESONATOR input: "Other OSC", then every wavetable
const juce::StringArray& resonatorModelNames();    // RESONATOR: Modal, String
int engineOf (int tblParam) noexcept;              // the engine an OSC table index selects, -1 = a wavetable
bool usesInput (int engine) noexcept;              // TEXTURE / RESONATOR
int tableSlotFor (int tblParam, int srcParam) noexcept;   // the wavetable the voice's table slot holds for this choice
juce::String controlCaption (int engine, int k);   // k = 0 / 1 / 2: what HARMONICS / TIMBRE / MORPH do on this engine (MORE panel)
float levelTrim (int engine) noexcept;             // measured: the engine at the saw's RMS at C3 (ProTests engines)

// ENGINES DIARY (installer #24, after the Mac crash of installer #23): a few lines per engine switch so the next crash in a host leaves
// its last steps on disk. Mac: ~/Library/Logs/Ghosty Pro/engines.log; elsewhere <app data>/Ghosty Pro/engines.log (512 kB cap).
// post() is for the audio thread: no I/O, no allocation, no blocking (a try-lock into a small fifo; dropped when busy or full).
// flush() runs on the message thread (ProProcessor's timer); note() writes a message-thread line at once (palette clicks, prepare).
namespace diary
{
enum Kind { kSwitch = 1, kFirstRender, kFallback, kRateGuard };
void post (int kind, int engine, double rate, int info = 0) noexcept;
void note (const juce::String& line) noexcept;
void flush() noexcept;
juce::File file();
}

// per-oscillator settings the voice reads once per control block
struct Settings
{
    int engine = -1;                // -1 = off
    double hz = 261.63;             // pitch (base, before the per-sample bend-lane ratio)
    float h = 0.5f, t = 0.5f, m = 0.5f;          // targets (knob + mod), smoothed per sample inside
    float accent = 0.8f;            // the note's velocity
    // TEXTURE
    float size = 0.5f, pitch = 0.0f, feedback = 0.0f; bool freeze = false;
    // RESONATOR
    float position = 0.5f; int model = 0;
    bool haveInput = false;         // the source plays (else a RESONATOR plucks itself)
};

// One engine slot of one voice's oscillator. Everything it can ever need is allocated in prepare() (message thread / prepareToPlay);
// start() / control() / tick() run on the audio thread without allocating or locking.
class OscEngine
{
public:
    OscEngine();
    ~OscEngine();
    void prepare();                                     // allocates the cores (call once, off the audio thread)
    int osc = 0;                                        // which oscillator this slot is (diary only)
    void start (float velocity, bool fromSilence, juce::uint32 seed);   // note-on (fromSilence: the voice was idle -> clear states)
    void control (const Settings& s, double engineRate);// once per control block
    float tick (float input, double freqRatio) noexcept;// one engine-rate sample; input = the source signal (TEXTURE / RESONATOR)
    bool active() const noexcept { return engine >= 0; }
    int current() const noexcept { return engine; }
private:
    int engine = -1;
    struct Impl; std::unique_ptr<Impl> d;
    void fail (int e, const char* where) noexcept;
    float tickImpl (float input, double freqRatio);      // an engine threw: this slot plays the wavetable (Saw) instead, logged
};
} // namespace gpro::eng
