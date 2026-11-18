#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "ProTables.h"
#include "Fx.h"
#include "Performer.h"
#include "License.h"
#include "ProFx.h"
#include "BendLane.h"
#include "CleanStage.h"
#include "ProEngines.h"     // ENGINES (round 3)

class ProProcessor;

namespace gpro
{
// ---------------------------------------------------------------- names shared by processor + editor
const juce::StringArray& filterTypeNames();
const juce::StringArray& lfoShapeNames();
const juce::StringArray& lfoDivNames();
const juce::StringArray& modSourceNames();
const juce::StringArray& modDestNames();
const juce::StringArray& xmodNames();
const juce::StringArray& subOctNames();
const juce::StringArray& subShapeNames();
const juce::StringArray& stealPriorityNames();   // VOICING: which voice a new note takes when every voice is busy
enum StealPriority { stNewest = 0, stOldest, stHighest, stLowest, stQuietest, numSteal };   // stOldest = the behaviour before VOICING (the default)
constexpr int NUM_SLOTS = 8;
constexpr int MAX_UNI = 8;
constexpr int MAX_VOICES = 16;

enum ModSrc { srcNone = 0, srcLfo1, srcLfo2, srcEnv2, srcEnv3, srcFEnv, srcAmpEnv, srcVel, srcWheel, srcAT, srcRandom, srcNote,
              srcMac1, srcMac2, srcMac3, srcMac4, srcBend, numSrc };   // srcBend = the BEND LANE (shape x AMOUNT)
enum ModDst { dNone = 0, dPitch, dFine, dPitch1, dPitch2, dPos1, dPos2, dLvl1, dLvl2, dDet1, dDet2, dSpread, dXmod, dSub, dNoise,
              dCutoff, dRes, dFDrive, dVolume, dPan, dLfo1Rate, dLfo2Rate, dGrowl,
              dCharacter, dTape, dLy2Lvl, dLy3Lvl, dSmpStart,
              dW1a, dW1b, dW2a, dW2b,
              dE1h, dE1t, dE1m, dE2h, dE2t, dE2m,     // ENGINES (round 3): Osc n Engine H / T / M
              numDst };   // TRAP targets appended, old indices unchanged (dCharacter .. dSmpStart are block-level: newest voice's sources); round 3: the WARP amounts

// Linear-time ADSR whose times/sustain can move while a note is held. Decay and release are curved
// (curve 0 = straight line like the free Ghosty, 1 = strongly exponential) but always finish in the set time.
struct Env
{
    enum class St { idle, attack, decay, sustain, release };
    St st = St::idle; double sr = 44100;
    float out = 0, lin = 0, relFrom = 0, a = 0.01f, d = 0.1f, s = 1.0f, r = 0.1f, curve = 0.0f;
    void set (float A, float D, float S, float R, float C) noexcept { a = A; d = D; s = juce::jlimit (0.0f, 1.0f, S); r = R; curve = C; }
    void on() noexcept
    {
        if (a > 0.0005f) { lin = out; st = St::attack; }        // from wherever it is: no click on retrigger
        else { out = 1; lin = 1; st = (d > 0 && s < 1) ? St::decay : St::sustain; }
    }
    void off() noexcept { if (st == St::idle) return; if (r > 0) { relFrom = out; lin = 1; st = St::release; } else reset(); }
    void reset() noexcept { st = St::idle; out = 0; lin = 0; }
    bool active() const noexcept { return st != St::idle; }
    float shape (float x) const noexcept { const float c = 1.0f + curve * 3.0f; return c == 1.0f ? x : std::pow (x, c); }
    float next (int steps = 1) noexcept
    {
        const float n = (float) steps;
        switch (st)
        {
            case St::idle: return 0;
            case St::attack:  lin += n / (float) (a * sr); if (lin >= 1) { lin = 1; st = (d > 0 && s < 1) ? St::decay : St::sustain; } out = lin; if (st == St::decay) lin = 1; break;
            case St::decay:   lin -= n / (float) (d * sr); if (lin <= 0) { lin = 0; st = St::sustain; } out = s + (1.0f - s) * shape (lin); break;
            case St::sustain: out += (s - out) * juce::jmin (1.0f, 0.002f * n); break;
            case St::release: lin -= n / (float) (r * sr); if (lin <= 0) { lin = 0; out = 0; st = St::idle; break; } out = relFrom * shape (lin); break;
        }
        return out;
    }
};

// FILTER DRIVE round 2 (ProDrive.h): a bounded soft saturator that is EXACTLY linear below half its headroom S and bends
// smoothly (C2) to S above it: odd, slope <= 1. Exactly linear below S/2 matters because it sits on the filter's integrator states:
// a state that passes through it every sample must not leak when the signal is small.
inline float fastTanh (float x) noexcept
{   // continued-fraction tanh (textbook), |error| < 2e-5 up to |x| = 4.5, clamped beyond
    if (x > 4.97f) return 1.0f; if (x < -4.97f) return -1.0f;
    const float x2 = x * x;
    return x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2))) / (135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f)));
}
inline float satS (float u, float S) noexcept
{
    const float T = 0.5f * S, a = std::abs (u);
    if (a <= T) return u;
    const float K = S - T;
    return std::copysign (T + K * fastTanh ((a - T) / K), u);
}

// Zero-delay state-variable filter (Cytomic / Simper), stereo.
struct Svf
{
    float a1 = 0, a2 = 0, a3 = 0, k = 1;
    float ic1[2] {}, ic2[2] {};
    void set (float g, float kk) noexcept { k = kk; a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2; }
    void reset() noexcept { ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0; }
    // returns lp, writes bp/hp
    inline float tick (float v0, int ch, float& bp, float& hp) noexcept
    {
        const float v3 = v0 - ic2[ch];
        const float v1 = a1 * ic1[ch] + a2 * v3;
        const float v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
        ic1[ch] = 2 * v1 - ic1[ch]; ic2[ch] = 2 * v2 - ic2[ch];
        bp = v1; hp = v0 - k * v1 - v2;
        return v2;
    }
    // the same with both integrator states saturating at S (the DRIVE's copy: harmonics are made at every stage, inside the loop)
    inline float tickS (float v0, int ch, float& bp, float& hp, float S) noexcept
    {
        const float v3 = v0 - ic2[ch];
        const float v1 = a1 * ic1[ch] + a2 * v3;
        const float v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
        ic1[ch] = satS (2 * v1 - ic1[ch], S); ic2[ch] = satS (2 * v2 - ic2[ch], S);
        bp = v1; hp = v0 - k * v1 - v2;
        return v2;
    }
};

// 4-pole zero-delay ladder with input saturation (the acid / ladder low pass).
struct Ladder
{
    float G = 0, g1 = 0, kk = 0; float s[2][4] {};
    void set (float g, float res) noexcept { G = g / (1.0f + g); g1 = 1.0f / (1.0f + g); kk = res * 3.95f; }
    void reset() noexcept { for (auto& c : s) for (auto& v : c) v = 0; }
    inline float tick (float x, int ch) noexcept
    {
        auto* st = s[ch];
        const float S = G * G * G * st[0] * g1 + G * G * st[1] * g1 + G * st[2] * g1 + st[3] * g1;
        float u = (x * (1.0f + kk * 0.35f) - kk * S) / (1.0f + kk * G * G * G * G);
        u = std::tanh (u);
        float y = u;
        for (int i = 0; i < 4; ++i) { const float v = (y - st[i]) * G; const float o = v + st[i]; st[i] = o + v; y = o; }
        return y;
    }
    // the DRIVE's copy: the input and every stage saturate at S (the circuit's headroom) instead of the unit tanh
    inline float tickS (float x, int ch, float S) noexcept
    {
        auto* st = s[ch];
        const float Ssum = G * G * G * st[0] * g1 + G * G * st[1] * g1 + G * st[2] * g1 + st[3] * g1;
        float u = (x * (1.0f + kk * 0.35f) - kk * Ssum) / (1.0f + kk * G * G * G * G);
        u = satS (u, S);
        float y = u;
        for (int i = 0; i < 4; ++i) { const float v = (y - st[i]) * G; const float o = v + st[i]; st[i] = satS (o + v, S); y = o; }
        return y;
    }
};
} // namespace gpro
#include "ProDrive.h"
#include "ProWarp.h"
#include "ProWavetable.h"
namespace gpro
{
// FILTER DRIVE round 2: the drive laws (parameter "f_dlaw"). CLASSIC = the old tanh on the summed voice (sub + noise included);
// INSIDE = gain into the filter's own saturating copy, 4x oversampled, sub + noise on a clean path, 1 / sqrt (drive) trim
enum DriveLaw { dlClassic = 0, dlInside = 1 };
constexpr float kDriveDb = 36.0f;          // the knob's range: 0 .. +36 dB into the filter
constexpr float kDriveSat = 6.0f;          // the filter circuit's headroom (a voice's oscillator sum peaks around 1 .. 1.5; linear below half of it)
constexpr float kDriveLoudExp = 0.12f;     // the loudness law: a unit-peak input leaves the drive at gain^0.12 (+4.3 dB peak at +36 dB)
constexpr float kDriveFade = 0.03f;        // below this knob value the clean and the driven filter cross-fade (exactly linear at 0)
constexpr float kCleanModeDb = 24.0f;      // CLEAN MODE: -24 dB in, +24 dB out
constexpr float kDriveTiltHz = 250.0f, kDriveTiltDb = 6.0f;   // the TILT around the saturator: lows -6 dB below 250 Hz going in, restored coming out
} // namespace gpro
#include "ProTrap.h"
#include "ProMacros.h"
#include "ProMod.h"          // MOD (round 3): the modulation router
#include "Bypass.h"
#include "SnapshotBank.h"
#include "ProMorph.h"
#include "ProBank.h"          // gpro::FactoryPreset (loadFactorySound) + the GHOSTY_FACTORY_PRESETS switch
#include <map>
namespace gspirit = ghosty::spirit;

struct ProSound : public juce::SynthesiserSound
{
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

class ProVoice : public juce::SynthesiserVoice
{
public:
    explicit ProVoice (ProProcessor& p) : proc (p) {}
    void prepare (double sr, int block);
    bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<ProSound*> (s) != nullptr; }
    void startNote (int midi, float vel, juce::SynthesiserSound*, int wheel) override;
    void stopNote (float, bool allowTail) override;
    void pitchWheelMoved (int v) override { wheelPos = (v - 8192) / 8192.0f; }
    void controllerMoved (int, int) override {}
    void aftertouchChanged (int v) override { at = v / 127.0f; }
    void channelPressureChanged (int v) override { at = v / 127.0f; }
    void renderNextBlock (juce::AudioBuffer<float>&, int start, int num) override;
    void legatoTo (int midi, float vel, bool retrigger);
    void hardReset();          // audio thread: the voice is gone, no tail, no pending declick (BYPASS / a layer switched off)
    double lfoPhase (int k) const noexcept { return lfoPh[k & 1]; }        // LFO 1 / 2 phase in cycles (tracer dots)
    const gpro::Env& envAt (int k) const noexcept { return k == 0 ? amp : k == 1 ? fenv : k == 2 ? env2 : env3; }   // 0 amp, 1 filter, 2 ENV 2, 3 ENV 3 (tracer)
    float modValue (int src) const noexcept { return juce::isPositiveAndBelow (src, (int) gpro::numSrc) ? srcVals[(size_t) src] : 0.0f; }
    float ampLevel() const noexcept { return amp.out; }                   // CLEAN: voice-count aware headroom
    const gpro::ModVoice& modVoice() const noexcept { return modv; }      // MOD (round 3)
    juce::uint32 startedAt = 0;

private:
    ProProcessor& proc;
    gpro::Env amp, fenv, env2, env3;
    gpro::Svf svfForm;
    // the voice filter: fc = the linear chain (CLASSIC law: everything; INSIDE law: the clean reference the cross-fade starts from),
    // fcSn = SUB + NOISE's own clean copy, fcDrv = the saturating copy the DRIVE pushes into (inside the 4x oversampler)
    gpro::FilterChain fc, fcSn, fcDrv;
    gpro::Oversampler4 os; gpro::Oversampler2 os2; gpro::AlignDelay dlyC, dlySn, dlyG; gpro::TiltPair tilt;
    gpro::Decimator decim; int decimQ = 1;                    // QUALITY: the engine runs Q x (1 / 2 / 4) and this brings it back down
    double growlPh = 0, scoopTime = 0, relTime = -1;   // TRAP brass: growl wobble phase, time since the (re)trigger for the pitch scoop, time since release (FALL)
    int filterType = -1;
    std::array<double, gpro::MAX_UNI> ph1 {}, ph2 {};
    std::array<float, gpro::MAX_UNI> last1 {};
    double subPhase = 0, lfoPh[2] {}, noteTime = 0;
    float wheelPos = 0, at = 0, rnd = 0;
    double logTarget = 0, logNow = 0;
    int note = 60;
    float velocity = 1, velRaw = 1;
    double sr = 44100;
    juce::uint32 noiseSeed = 1; float nzLp[2] {};
    gpro::Svf nzSvf, nzBreath; float nzPk[2][6] {}, nzBr[2] {}, nzPrev[2] {}, nzGeigerEnv[2] {}, nzStereoSm = 1.0f;   // NOISE round 2 states
    float sCut = 0, sRes = 0, sDrive = 0, sVol = 1, sPan = 0, sPos1 = 0, sPos2 = 0;
    // the oscillator-section knobs glide instead of stepping once per block (2026-10-24 zipper fix; snapped at a note's start):
    // levels per sample (10 ms), detune / spread / blend / X-MOD amount / noise colour per 16 samples (10 ms)
    float kLvl1 = 0, kLvl2 = 0, kSub = 0, kNz = 0, kNzCol = 0, kDet1 = 0, kSpr1 = 0, kBln1 = 0, kDet2 = 0, kSpr2 = 0, kBln2 = 0, kXamt = 0;
    float nzColAt = -1.0f, nzAc = 1.0f, nzComp = 1.0f;       // the noise colour filter, recomputed only when the colour moves
    float declickL = 0, declickR = 0, lastL = 0, lastR = 0;
    juce::uint32 seenSwap = 0; int tableFade = 0;             // table crossfade: samples left (fades from ctl.wavesPrev)
    static constexpr int tableFadeLen = 384;
    std::array<float, gpro::numSrc> srcVals {};
    std::array<float, gpro::numDst> dstPrev {};
    ghosty::clean::LevelMatch driveMatch;                     // CLEAN: the filter DRIVE at the input's loudness (CLASSIC law)
    float dinNow = 1, trimNow = 1, fadeNow = 0; bool drvInit = true;   // INSIDE law: gain in, trim out, clean <-> driven fade (ramped per sample)
    std::array<float, gpro::MAX_UNI + 1> driftPh {}, driftCents {};   // UNISON V2: per-voice slow pitch drift (OSC 2 reads the array shifted by one)
    float dcX[2] {}, dcY[2] {};                                       // the V2 DC block after the decimator (sound audit)
    double lfPrev = 0, rampMul = 1, rampStep = 1; bool pitchRampInit = false;   // the V2 per-sample pitch ramp (sound audit)
    float fcLast = -1.0f;                                             // the V2 cut-off glide: the last block's cut-off (sound audit)
    std::array<float, 4> sideLp {};                                   // UNISON V2: the side voices' one-pole low-pass states (OSC 1 L/R, OSC 2 L/R)
    std::array<float, gpro::MAX_UNI> syncPend {};                     // hard SYNC: the pending half of the last reset's BLEP per unison voice
    // WARP (round 3): the four amounts (OSC 1 slot 1 / 2, OSC 2 slot 1 / 2) glided per sample; per unison voice the Sync warp's own phase,
    // the last Quantize step, the pending BLEP half, and OSC 2's last sample (OSC 1's source, one sample late)
    std::array<float, 4> kW {}; bool kWInit = true;
    std::array<double, gpro::MAX_UNI> wSyncPh1 {}, wSyncPh2 {};
    std::array<int, gpro::MAX_UNI> qPrev1 {}, qPrev2 {};
    std::array<float, gpro::MAX_UNI> wPend1 {}, wPend2 {}, last2 {}, qLast1 {}, qLast2 {};
    gpro::ModVoice modv;                                              // MOD (round 3): LFO 3 / 4 phases + this voice's source values
    // ENGINES (round 3): the engine each oscillator plays when its table menu picks one (ProEngines.h), and the phase of the
    // wavetable a TEXTURE / RESONATOR is fed with when its SOURCE is a table
    gpro::eng::OscEngine eng[2];
    double engSrcPh[2] {};
    void snap();
    float lfoAt (int which, double phase) const noexcept;
};

class ProSynth : public juce::Synthesiser
{
public:
    explicit ProSynth (ProProcessor& p) : proc (p) {}
    void setMono (bool shouldBeMono, bool shouldBeLegato);
    void noteOn (int ch, int note, float vel) override;
    void noteOff (int ch, int note, float vel, bool allowTail) override;
    void allNotesOff (int ch, bool allowTail) override;
    void handleController (int ch, int cc, int value) override;
    int polyLimit = gpro::MAX_VOICES;
    bool sameNoteLimit = false;                 // VOICING: LIMIT SAME NOTE POLY TO 1 (parameter "v_same1")
    int stealPriority = gpro::stOldest;         // VOICING: STEAL PRIORITY (parameter "v_steal")
protected:
    juce::SynthesiserVoice* findFreeVoice (juce::SynthesiserSound*, int ch, int note, bool steal) const override;
private:
    ProProcessor& proc;
    bool mono = false, legato = false;
    std::array<int, 32> stack {}; int stackN = 0; int monoNote = -1;
    void removeFromStack (int note);
};

class ProProcessor : public juce::AudioProcessor,
                     private juce::AudioProcessorValueTreeState::Listener,
                     private juce::MidiKeyboardState::Listener,
                     private juce::Timer
{
public:
    ProProcessor();
    explicit ProProcessor (bool layerProcessor);
    ~ProProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Ghosty Pro"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- drawn frames A/B (message thread owns them; same hand-over scheme as the free Ghosty)
    ghosty::Frame frameA, frameB;
    void rebuildWaves();
    void flushPendingWaves();
    std::shared_ptr<const gpro::Table> uiTable (int osc) const;     // message thread: what an oscillator plays (for drawing)
    std::map<std::tuple<int, int, int>, std::shared_ptr<const gpro::Table>> specCache;   // SPECTRAL copies of built-in tables (id, mode, amount / 200)
    std::vector<gpro::HiFrame> sourceFramesOf (int id) const;       // message thread: the single-cycle frames a table id is built from (SPECTRAL rebuilds)

    // ---- sample -> wavetable (message thread)
    bool importSample (const juce::File& wav, juce::String& message);
    juce::String sampleName() const;

    // ---- WAVETABLE EDITOR (round 3 item 2b, 2026-10-24; message thread): the "User" table. An empty doc clears it (the table then
    // plays the Ghost A/B frames, like an empty Sample). Saved in the state as a USERTABLE child (float32, bit-exact), which older
    // builds ignore (their table choice clamps to the last entry they know).
    void setUserTable (const gpro::wt::Doc& doc);
    gpro::wt::Doc userTable() const;                                 // a copy of what is loaded (empty frames = none)
    juce::String userTableName() const;

    // ---- presets
    static juce::File userPresetDir();
    juce::Array<juce::File> listUserPresets() const;
    bool saveUserPreset (const juce::String& name);
    bool loadUserPreset (const juce::File& file);
    void initPatch();
    void refreshUserPrograms();                  // message thread: the host programs' list of your sounds (FACTORY PRESETS OFF: programs = INIT + these)
    int userProgramOf (const juce::String& name) const;   // the program number of a saved sound (0 = not in the list)
    juce::String patchName() const;
    bool isUserPatch() const;
    juce::String currentScene() const;

    juce::AudioProcessorValueTreeState apvts;
    gpfx::Rack rack;
    ghosty::License license { "Ghosty Pro" };
    ghosty::DemoGate demo;
    std::atomic<float> level { 0.0f };
    std::atomic<float> modLive[gpro::NUM_SLOTS] {};          // what each matrix slot is doing right now (UI)
    std::atomic<float> srcLive[gpro::numSrc] {};
    std::atomic<int> voicesLive { 0 };
    std::atomic<int> uniVoicesLive { 0 }, uniVoicesMax { 0 };      // playing / max counting unison voices (this processor)
    std::atomic<int> uniLayerLive[gpro::NUM_LAYERS] {}, uniLayerMax[gpro::NUM_LAYERS] {};   // the same per layer, gathered by the parent (the header sums them)
    juce::uint32 voiceStamp = 0;      // audio thread: note-on order

    juce::MidiKeyboardState keyboardState;
    ghosty::Performer performer;
    bool isNoteSounding (int note) const { return juce::isPositiveAndBelow (note, 128) && heldNotes[(size_t) note].load (std::memory_order_relaxed) != 0; }

    static float cutoffHz (float v) { return 30.0f * std::pow (2.0f, v * 9.3f); }   // 30 Hz .. ~19 kHz
    static float outGain (float v) { return 4.0f * v * v; }                         // VOLUME knob -> linear (0.5 = 0 dB, 1 = +12 dB)

    // ---- per-block values the voices read (audio thread)
    struct Osc { std::atomic<float>* on, *tbl, *pos, *lvl, *oct, *semi, *fine, *uni, *det, *spr, *bln; std::atomic<float>* dmode = nullptr, *stack = nullptr;
                 std::atomic<float>* w1 = nullptr, *w1a = nullptr, *w2 = nullptr, *w2a = nullptr, *spec = nullptr, *speca = nullptr; };   // round 3: WARP 1 / 2, SPECTRAL
    Osc o1 {}, o2 {};
    // ENGINES (round 3): per oscillator HARMONICS / TIMBRE / MORPH, SOURCE, TEXTURE size / pitch / feedback / freeze, RESONATOR position / model
    struct EngP { std::atomic<float>* h = nullptr, *t = nullptr, *m = nullptr, *src = nullptr, *size = nullptr, *pitch = nullptr, *fb = nullptr, *frz = nullptr, *rpos = nullptr, *rmodel = nullptr; };
    EngP eng1 {}, eng2 {};
    std::atomic<float>* subLvl, *subOct, *subShp, *nzLvl, *nzCol, *xmod, *xamt;
    std::atomic<float>* fType, *cutoff, *res, *fenvAmt, *fDrive, *fKey, *velFilt;
    std::atomic<float>* fDLaw = nullptr, *fClean = nullptr;      // FILTER DRIVE round 2: drive law (Classic / Inside), CLEAN MODE
    std::atomic<float>* uniLaw = nullptr;                         // UNISON round 2: Classic / V2 (side-voice low cut, drift)
    std::atomic<float>* nzType = nullptr, *nzStereo = nullptr;    // NOISE round 2: colour (0 = Classic), stereo width
    std::atomic<float>* qualityP = nullptr, *qRender = nullptr, *qLock = nullptr;   // QUALITY Draft / High / Ultra (1x / 2x / 4x), Ultra when rendering, lock against sound loads
    std::atomic<int> engineQLive { 1 };                           // the engine rate factor in use (UI / tests)
    std::atomic<int> externalQ { 0 };                             // set by the parent: a STACK layer runs at the parent's quality (0 = own)
    int engineLatency() const noexcept;                           // the voice path's whole-sample delay at the current quality / drive law
    std::atomic<float>* vbType = nullptr, *vbSize = nullptr, *vbDecay = nullptr, *vbPre = nullptr, *vbLo = nullptr, *vbHi = nullptr, *vbDuck = nullptr, *vbWidth = nullptr;   // GHOST VERB (round 2)
    ghosty::GhostVerb::Params ghostVerbParams;                    // filled per block from the parameters above
    std::atomic<float>* fA, *fD, *fS, *fR;
    std::atomic<float>* attack, *decay, *sustain, *release, *curve, *velAmp;
    std::atomic<float>* e2[4], *e3[4];
    std::atomic<float>* lfoRate[2], *lfoSync[2], *lfoDiv[2], *lfoShape[2], *lfoTrig[2], *lfoFade[2];
    std::atomic<float>* mSrc[gpro::NUM_SLOTS], *mDst[gpro::NUM_SLOTS], *mAmt[gpro::NUM_SLOTS];
    std::atomic<float>* macro[4];
    std::atomic<float>* glideP, *monoP, *legatoP, *polyP, *bendP, *gain;
    std::atomic<float>* sameNoteP = nullptr, *stealP = nullptr;   // VOICING (round 3 item 6)
    int voiceNotes (int* out, int maxOut) const;                  // the notes the main layer's active voices play (tests / UI; not real-time safe)
    std::atomic<float>* drive, *crush, *chorus, *phaser, *echo, *verb, *haunt;
    std::atomic<float>* keyP, *scaleP, *chordP, *arpP, *arpRateP, *arpModeP, *arpOctP, *arpGateP;
    std::atomic<float>* blOn, *blAmt, *blLen, *blRng, *blMode;
    // ---- TRAP: brass, character, tape, stack, sample (all default off / neutral)
    std::atomic<float>* brScoop, *brScoopT, *brGrowl, *brForm, *brFormF, *brFall;
    std::atomic<float>* chAmt, *tpOn, *tpAmt, *tpSlow;
    struct LayerP { std::atomic<float>* src, *oct, *det, *lvl, *pan, *dly, *rev, *play, *fx; } ly[gpro::NUM_LAYERS] {};
    std::atomic<float>* stkVSplit, *stkKSplit;
    std::atomic<float>* smpRoot, *smpFine, *smpFormant, *smpLoop, *smpStart, *smpEnd, *smpA, *smpD, *smpS, *smpR, *smpRevVel;
    const bool isLayer;                                   // a child processor playing one STACK layer
    std::atomic<float> externalSemis { 0.0f };            // set by the parent: layer transpose + tape pitch
    std::atomic<bool> externalReverse { false };          // set by the parent: this layer's REVERSE
    std::atomic<bool> externalFx { false };               // set by the parent: this layer runs its own Ghost FX / CHARACTER (LAYER FX)
    // the preset's level trim is the TRIM knob (parameter "out_trim", dB, 0.01 dB steps); trimGain reads / writes it as a gain
    struct TrimProxy { ProProcessor& p; float load() const noexcept; void store (float gain); } trimGain { *this };
    static float trimDbOf (float v) noexcept { return std::round (v * 100.0f) / 100.0f; }      // exact: the 0.01 dB grid the trims are written on
    // ---- CLEAN SOUND (Shared/CleanStage.h): the hidden ceiling (always on), gain law, pockets, auto-gain; switch "cl_on" + 4 knobs
    std::atomic<float>* trimP = nullptr; std::atomic<float>* trimRaw = nullptr; std::atomic<float>* clOn = nullptr; std::atomic<float>* clWidth = nullptr;
    std::atomic<float>* clLowCut = nullptr; std::atomic<float>* clMono = nullptr; std::atomic<float>* clMud = nullptr;
    ghosty::clean::Ceiling ceiling;                       // audio thread (the parent only)
    ghosty::clean::SumPockets sumPockets;
    ghosty::clean::LayerPocket pocket[gpro::NUM_LAYERS];
    std::atomic<bool> monoCheck { false };                // MONO CHECK (a listening switch, never saved)
    std::atomic<bool> externalClean { false };            // set by the parent: CLEAN is on for the whole stack
    std::atomic<float> corrLive { 1.0f };                 // output L/R correlation (UI)
    std::atomic<float> voiceLawLive { 1.0f }, layerLawLive { 1.0f };   // the gain law right now (UI / tests)
    std::atomic<int> bassOwnerLive { 0 };                 // which layer owns the bass (0..2)
    // test hook (ProTests attribution): bits switch single CLEAN parts off: 1 voice law, 2 osc law, 4 filter-drive match, 8 CHARACTER match,
    // 16 synth-FX clean, 32 rack match, 64 tape match, 128 sum pockets, 256 layer pockets + layer law, 512 unison spread
    int cleanOff = 0;
    bool cleanNow() const noexcept { return isLayer ? externalClean.load (std::memory_order_relaxed) : clOn->load() > 0.5f; }
    static const std::vector<const char*>& legacyLawParams();   // round-2 law switches: 0 = the sound before them (older states; SOUND ENGINE CLASSIC)
    static bool factoryCleanOn (const juce::String& name);   // the factory sounds that clipped before CLEAN load with it ON
    // SOUND ENGINE: V2 is THE engine - every factory sound, INIT and anything new plays on filter drive Inside, unison V2, the Ghost
    // reverb and quality High, with the V2 re-voicing (ProV2Tune.inc). CLASSIC (the round-1 laws at Draft, exactly the factory bank before
    // round 2) is internal only: older saved states / layers keep it (a state without the law switches, a layer without "lyN_eng"), and
    // ProTests compares it against the old build. There is no user-facing switch.
    enum { engineClassic = 0, engineV2 = 1 };
    std::atomic<int> soundEngine { engineV2 };
    void setSoundEngine (int e) noexcept { soundEngine.store (e == engineClassic ? engineClassic : engineV2); }
    int getSoundEngine() const noexcept { return soundEngine.load(); }
    static float engineLawValue (const juce::String& id, int engine);   // what a factory sound's law switch starts at under that engine
    static float factoryTrimDbFor (const juce::String& name, int engine);   // the TRIM a factory sound loads with (V2: + its V2 trim)
    static bool factoryCleanOnFor (const juce::String& name, int engine);   // CLEAN on load (V2: may switch a sound's CLEAN on)
    static float factoryV2NormDb (const juce::String& name);               // V2 only: the loudness normalisation added to the trim (ProV2Norm.inc)
    static juce::String factoryV2Spec (const juce::String& name);          // V2 only: extra spec tokens (a law pinned back to Classic ...)
    static float factoryTrimDb (const juce::String& name);   // a factory sound's TRIM (the CLEAN ones: re-measured to stay as loud as before)
    int ceilingLatency() const noexcept { return isLayer ? 0 : ceiling.latency(); }
    // MOD (round 3): the modulation router (ProMod.h / ProMod.cpp): 64 connections source -> any knob, LFO 3 / 4, MIDI learn
    gpro::ModRouter modr;
    void modAttach();                                                   // constructor: LFO 3 / 4 + depth shadows, the voice targets
    void modBlock (int n, double bpm, double ppq, bool playing, const juce::MidiBuffer& midiIn);   // audio thread, per (sub-)block
    void modMorphRebuild();                                             // message thread: the MORPH BOARD's per-ghost amounts
    gpro::MacroEngine macros;                             // per-preset MACROS: 4 named knobs -> many parameters (ProMacros.h)                 // the preset's measured level trim (hidden gain stage; VOLUME stays at 0 dB)
    std::atomic<bool> layerMute[gpro::NUM_LAYERS] {}, layerSolo[gpro::NUM_LAYERS] {};   // UI solo / mute (not saved)
    void bypassReset();                                   // audio thread: BYPASS fully faded out -> every voice, layer, queue and tail gone
    void audioReset();                                    // audio thread: cut every voice, clear every FX tail (allocation-free)
    static juce::String soundIdOf (const juce::String& factoryName);     // "category/index": survives a rename of the sound
    static juce::String nameOfSoundId (const juce::String& id);
    bool reverseEnv = false;                              // audio thread: REVERSE on this (layer) processor -> swell-and-cut envelopes
    std::atomic<float> cpuLive[gpro::NUM_LAYERS] {};      // % of one core per layer (UI)
    std::atomic<int> layerVoices[gpro::NUM_LAYERS] {};
    bool setLayerSound (int layer, const juce::String& factoryName, int engine = -1);   // message thread; "" clears; engine -1 = this one's SOUND ENGINE
    int layerEngine (int layer) const;                                  // the SOUND ENGINE a layer's sound was loaded with
    juce::String layerSound (int layer) const;
    bool importTrapSample (const juce::File& file, juce::String& message);   // message thread
    void clearTrapSample();
    std::shared_ptr<const gpro::SampleData> trapSample() const;             // message thread (for drawing)

    // ---- BEND LANE: drawn tempo-synced curve on the master pitch (message thread edits bendShape, audio reads a copy)
    bendlane::SharedShape bendShape;
    std::atomic<float> laneLivePos { -1.0f }, laneLiveSemis { 0.0f };   // UI playhead (lane position 0..1) and pitch now

    struct BlockCtl
    {
        const gpro::WaveSet* waves = nullptr;
        const gpro::WaveSet* wavesPrev = nullptr;   // the set before the last swap (alive until the next swap): voices fade from it
        juce::uint32 wavesSwap = 0;                 // swap counter: a voice that sees it change starts its crossfade
        double lfoGlobal[2] {}, lfoInc[2] {};      // cycles at block start, cycles per sample (unmodulated)
        float wheel = 0, glide = 0, bendSemis = 2;
        double lastNoteLog2 = 0; bool haveLastNote = false;
        int slotSrc[gpro::NUM_SLOTS] {}, slotDst[gpro::NUM_SLOTS] {}; float slotAmt[gpro::NUM_SLOTS] {};
        bool anyMod = false;
        // bend lane, per sample for this block (audio thread): semitones on the master pitch, 2^(semis/12), shape x amount
        const float* laneSemi = nullptr; const float* laneRatio = nullptr; const float* laneNorm = nullptr; int laneLen = 0;
        bool lanePitch = false, laneMod = false;
        float extraSemis = 0;      // TRAP: layer transpose + tape pitch-down, on every voice
        int engineQ = 1;           // QUALITY: 1 Draft, 2 High, 4 Ultra (the voices run at engineQ x the sample rate)
        bool cleanUni = false;     // (CLEAN: the unison part)
        bool clean = false;        // CLEAN SOUND: unison spread / detune follow the pitch, the centre stays mono, DRIVE level-matched
    } ctl;
    static float lfoValue (int shape, double phaseCycles, juce::uint32 seed) noexcept;

    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    void setParam (const juce::String& id, float plainValue);
    void resetSoundParams();
    void resetMasterLimiter();
    // BYPASS (shared design, Shared/Bypass.h): the LAST parameter, never part of a sound; mutes the whole plug-in with a 6 ms fade
    juce::AudioParameterBool* bypassParam = nullptr;
    // ---- ENVELOPE TRACER (display only): where the newest voices' envelopes are, published once per block while the MOD page shows
    // (seqlock: the writer bumps seq to odd, writes atomics, bumps to even + a checksum; readers retry when it moved; no locks, no allocation)
    static constexpr int kTraceVoices = 8, kTraceEnvs = 4;
    struct TraceVoice { juce::uint32 age = 0; float lfo[2] {}; int stage[kTraceEnvs] {}; float lin[kTraceEnvs] {}, out[kTraceEnvs] {}, a[kTraceEnvs] {}, d[kTraceEnvs] {}, s[kTraceEnvs] {}, r[kTraceEnvs] {}; };
    struct TraceSnap { int count = 0; juce::uint32 seq = 0; float lfoGlobal[2] {}; TraceVoice v[kTraceVoices]; };
    std::atomic<bool> traceOn { false };
    bool readTrace (TraceSnap& out) const;      // any thread; false when nothing is published yet / the writer kept it busy
    void publishTrace() noexcept;               // audio thread (called by processBlock)
    std::atomic<int> uiTheme { 0 };
    // SNAPSHOTS: the bank lives here (survives the editor closing); capture = the whole sound, restore = a normal sound load
    std::unique_ptr<ghosty::SnapshotBank> snapshots;
    juce::ValueTree captureSound();
    void leaveMadeTablesOut (juce::ValueTree& tree, bool fromClick);   // demo: no plugin-made wavetable on disk (presets, boards, snapshots, project)
    void restoreSound (const juce::ValueTree& t, const juce::String& name);
    void makeSnapshotBank();
    // MORPH BOARD (ProMorph.h + Shared/SpiritBoard.h): ghosts holding whole sounds; the puck = parameters morph_x / morph_y
    gpro::MorphEngine morph;
    gspirit::Board morphBoard;                              // message thread
    std::map<juce::String, juce::ValueTree> morphSounds;    // ghost id -> its sound (message thread)
    void morphChanged (bool soundsChanged);                  // positions / settings changed (or the sounds: recompile)
    int morphAddGhost (float x, float y, const juce::ValueTree& sound, const juce::String& name);
    void morphSetSound (int ghost, const juce::ValueTree& sound, const juce::String& name);
    void morphRemove (int ghost);
    void morphTick (double nowMs = -1.0);                    // message thread (20 Hz timer); nowMs: a test clock (-1 = the real one)
    // ORBIT smoothness: a leader change reloads only what differs, never faster than every 150 ms, and while the leader keeps flipping
    // (a fast ORBIT) the discrete settings hold the current leader instead of thrashing
    std::atomic<int> morphReloads { 0 };                     // discrete reloads so far (tests)
    bool morphLegacyReload = false;                          // test hook: the old behaviour (every flip = a full reload)
    bool morphHolding = false;                               // the leader is being held (fast flips)
    juce::String morphEditId;                                // the ghost you were on when MORPH went OFF: your edits while it is off are written back into it when MORPH goes ON
    void morphWriteBackEdits();                              // MORPH ON: write the sound you edited while it was off back into the ghost you were on
    void morphCommit();                                      // MORPH OFF: the blend becomes the parameters' values                                        // message thread (timer): the leader's switches / tables / FX
    juce::ValueTree morphTree() const;
    void loadMorphTree (const juce::ValueTree&);
    static juce::ValueTree factorySound (int program, int engine = engineV2);
    static juce::File boardDir();
    bool saveBoard (const juce::String& name);
    bool loadBoard (const juce::File& f);
    std::atomic<float>* morphX = nullptr; std::atomic<float>* morphY = nullptr;
    float morphTrim = -1.0f; double morphFreeSecs = 0;          // the editor's look (0 BLACK, 1 PURPLE, 2 LIGHT): a per-project UI preference (session state only, never in a sound)
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

private:
    void parameterChanged (const juce::String& id, float) override;
    void timerCallback() override;
    void handleNoteOn (juce::MidiKeyboardState*, int ch, int note, float vel) override;
    void handleNoteOff (juce::MidiKeyboardState*, int ch, int note, float vel) override;
    juce::ValueTree makeStateTree();
    void applyStateTree (juce::ValueTree state, bool morphLight = false);   // morphLight: skip what is already loaded (layers, sample, rack, tables, macros, lane)
    double morphLastReloadMs = -1.0e9; int morphWanted = -1; double morphFlips[8] {}; int morphFlipW = 0;
    juce::String lastRackXml, lastMacroXml, lastModXml;   // (lastModXml: MOD round 3)

    ProSynth synth { *this };
    ghosty::FxChain fx;
    juce::MidiBuffer merged, performed;
    static constexpr int midiCapacity = 32768;
    std::atomic<double> sampleRateNow { 44100.0 };
    int currentProgram = 0;
    juce::CriticalSection userProgLock;       // guards userPrograms (a host may ask for program names off the message thread)
    juce::Array<juce::File> userPrograms;     // FACTORY PRESETS OFF: your sounds as host programs 1..n
    void loadFactorySound (const gpro::FactoryPreset&);
    juce::String currentPatchName, scene;     // guarded by reqLock
    bool patchIsUser = false;                 // guarded by reqLock
    float lastGain = -1.0f;
    float ly0G[2] { -1.0f, -1.0f };   // the main layer's LEVEL / PAN gains of the last block (ramped from, so a move never steps)
    std::array<std::atomic<uint8_t>, 128> heldNotes {};
    int latencyNow = 0;
    double lfoFree[2] {};

    // bend lane (audio thread)
    bendlane::Shape laneShape; uint32_t laneSeen = 0xffffffffu;
    std::vector<float> laneSemiBuf, laneRatioBuf, laneNormBuf;
    double laneFreeBeats = 0, laneTrigBeats = 0; float laneSm = 0, laneNormSm = 0; bool laneWasPlaying = false;
    void computeLane (int n, double bpm, double ppq, bool playing, double beatsPerBar, const juce::MidiBuffer& notes);

    // waves
    gpro::Mailbox waves;
    gpro::TableCache cache;
    double builtRate = 0;
    std::shared_ptr<const gpro::Table> ghostTable, sampleTable, shownT1, shownT2;   // message thread
    std::vector<gpro::HiFrame> sampleFrames;   // message thread
    juce::String sampleLabel;                  // message thread
    gpro::wt::Doc userDoc;                     // message thread (the "User" table)
    std::shared_ptr<const gpro::Table> userTbl; // message thread
    std::atomic<bool> needA { false }, needB { false }, needTables { true }, needGhost { true };
    int frameShapeA = 2, frameShapeB = 3;
    struct FrameRequest { bool has = false; ghosty::Frame a {}, b {}; int shapeA = 0, shapeB = 0; bool hasSample = false; std::vector<gpro::HiFrame> sample; juce::String sampleName;
                          bool hasUser = false; gpro::wt::Doc user; };
    mutable juce::SpinLock reqLock;
    FrameRequest request;
    ghosty::Frame savedA {}, savedB {};
    std::vector<gpro::HiFrame> savedSample; juce::String savedSampleName;   // guarded by reqLock
    gpro::wt::Doc savedUser;                                                 // guarded by reqLock
    std::atomic<bool> requestPending { false };
    void postFrames (const ghosty::Frame& a, const ghosty::Frame& b, int shapeA, int shapeB);
    void publishTables();

    // TRAP (audio thread unless noted)
    gpro::Character character; gpro::Tape tape; float tapeSemisSm = 0; int blockNow = 512;
    ghosty::BypassFader bypass; bool bypassResetPending = false;
    // MACROS: a moving macro renders the host block in 32-sample sub-blocks; the play head they see is offset to each sub-block
    struct TraceVoiceA { std::atomic<juce::uint32> age { 0 }; std::atomic<float> lfo[2] {}; std::atomic<int> stage[kTraceEnvs] {}; std::atomic<float> lin[kTraceEnvs] {}, out[kTraceEnvs] {}, a[kTraceEnvs] {}, d[kTraceEnvs] {}, s[kTraceEnvs] {}, r[kTraceEnvs] {}; };
    struct TraceA { std::atomic<juce::uint32> seq { 0 }, check { 0 }; std::atomic<int> count { 0 }; std::atomic<float> lfoGlobal[2] {}; TraceVoiceA v[kTraceVoices]; } trace;
    struct OffsetHead : juce::AudioPlayHead
    {
        juce::AudioPlayHead* inner = nullptr; double sr = 44100; int offset = 0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            auto p = inner != nullptr ? inner->getPosition() : juce::Optional<PositionInfo>();
            if (! p || offset == 0) return p;
            const double secs = offset / sr;
            if (auto t = p->getTimeInSamples()) p->setTimeInSamples (*t + offset);
            if (auto t = p->getTimeInSeconds()) p->setTimeInSeconds (*t + secs);
            if (p->getIsPlaying()) if (auto q = p->getPpqPosition()) if (auto b = p->getBpm()) p->setPpqPosition (*q + secs * *b / 60.0);
            return p;
        }
    } fineHead;
    static constexpr int fineStep = 32;
    bool inFineSteps = false;
public:
    std::atomic<int> fineBlocks { 0 };           // host blocks rendered in fine steps (tests)
#if GPRO_TAPS
    // TEST BUILD ONLY (ProTests): the peak at each stage of the output chain since the last reset (audit mode)
    enum { tapLayer1, tapLayers, tapCharacter, tapFx, tapRack, tapTape, tapGain, tapOut, numTaps };
    float tapPk[numTaps] {}; double tapSq[numTaps] {}; double tapN[numTaps] {};
    void tap (int k, const juce::AudioBuffer<float>& b) noexcept
    {
        for (int c = 0; c < b.getNumChannels(); ++c) { tapPk[k] = juce::jmax (tapPk[k], b.getMagnitude (c, 0, b.getNumSamples())); auto* d = b.getReadPointer (c); for (int i = 0; i < b.getNumSamples(); ++i) tapSq[k] += (double) d[i] * d[i]; }
        tapN[k] += b.getNumSamples() * b.getNumChannels();
    }
    double layerE[gpro::NUM_LAYERS][2] {};     // each layer's left / right energy as it goes into the mix (after its pocket, level and pan)
    void layerTap (int k, const float* l, const float* r, int n, float gl, float gr) noexcept { for (int i = 0; i < n; ++i) { layerE[k][0] += (double) l[i] * l[i] * gl * gl; layerE[k][1] += (double) r[i] * r[i] * gr * gr; } }
    double layerCentroid (int k) const noexcept { const double a = layerE[k][0], b = layerE[k][1]; return (b - a) / std::max (1e-20, a + b); }
    void resetTaps() noexcept { for (int k = 0; k < numTaps; ++k) { tapPk[k] = 0; tapSq[k] = tapN[k] = 0; } for (auto& e : layerE) e[0] = e[1] = 0; }
    double tapRmsDb (int k) const noexcept { return tapN[k] > 0 ? 10.0 * std::log10 (std::max (1e-20, tapSq[k] / tapN[k])) : -200.0; }
    ProProcessor* layerChild (int k) const { return child[k].get(); }
#else
    void tap (int, const juce::AudioBuffer<float>&) noexcept {}
#endif
private:
    juce::MidiBuffer fineMidi;
    juce::MidiBuffer chunkMidi;                                        // a host block bigger than prepared is processed in prepared-size chunks
    float extMod[gpro::numDst] {};                                     // block-level mod values (CHARACTER, TAPE, layer levels, sample start)
    gpro::LayerMidi layerMidi[gpro::NUM_LAYERS]; int layerSrcWas[gpro::NUM_LAYERS] {};
    std::unique_ptr<ProProcessor> child[gpro::NUM_LAYERS];            // message thread owns; created once, kept
    std::atomic<ProProcessor*> childLive[gpro::NUM_LAYERS] {};
    juce::String layerName[gpro::NUM_LAYERS];                          // guarded by reqLock
    int layerEng[gpro::NUM_LAYERS] {};                                  // guarded by reqLock: the SOUND ENGINE each layer sound loaded with
    juce::AudioBuffer<float> layerBuf;
    float layerGainWas[gpro::NUM_LAYERS][2] { { -1, -1 }, { -1, -1 }, { -1, -1 } };   // audio thread: last layer gains (ramped, no zipper)
    // CLEAN (audio thread)
    float voiceG = 1.0f, layerG = 1.0f, presence[gpro::NUM_LAYERS] {}, layerLvl[gpro::NUM_LAYERS] { 1.0f, 1.0f, 1.0f }; int bassOwner = 0, ownerHold = 0; float corrLR = 0, corrLL = 0, corrRR = 0;
    bool cleanWas = false;
    ghosty::clean::LevelMatch charMatch, tapeMatch;
    juce::AudioBuffer<float> preBuf, smpBuf;              // scratch (sized in prepareToPlay)
    void cleanLayerParams (int n);                        // per block: who owns the bass, pockets, side budget
    void applyVoiceLaw (juce::AudioBuffer<float>& b, int n);
    void matchStage (ghosty::clean::LevelMatch& m, juce::AudioBuffer<float>& b, int n, float lo, float hi);
    gpro::SampleEngine sampler; gpro::Handoff<gpro::SampleSlot> sampleBox; const gpro::SampleData* samplePlaying = nullptr;
    std::shared_ptr<const gpro::SampleData> sampleHeld;                // message thread copy (drawing / state)
    void renderLayers (juce::AudioBuffer<float>& buffer, int n, const juce::MidiBuffer& performed, juce::AudioPlayHead* head);
    static bool isTrapParam (const juce::String& id);
    void publishSample (std::unique_ptr<gpro::SampleData> d);          // message thread: hand a new sample (or none) to the audio thread
    static juce::MemoryBlock encodeSampleFlac (const gpro::SampleData& d);
    static bool decodeSampleFlac (const juce::MemoryBlock& mb, gpro::SampleData& d);

    struct UiNote { int8_t note; uint8_t vel; bool on; };
    juce::AbstractFifo uiFifo { 256 };
    std::array<UiNote, 256> uiNotes {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProProcessor)
};
