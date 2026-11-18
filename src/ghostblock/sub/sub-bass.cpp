/*
 * Poltergeist 2 (GhostBlock) - SUB: settings, host parameters, state, and the audio path (see sub-bass.h, README.md).
 *
 * The sound is made by two pieces of third-party DSP, both GPL-3.0-or-later:
 *   Bombaz (M.I. Hollemans)  - bombaz-voice.h hosts its unmodified oscillator;
 *   Subhoofer (Ardura)       - subhoofer-port.h is a C++ port of its harmonic algorithms.
 * Everything else in this file is ours: following the engine's note, the oversampling, the CLEAN / DIRTY mix, the
 * low cut on the engine (MAKE ROOM, sst-filters' CytomicSVF), the delay that keeps the sub in time with SPECTRAL FX.
 */
#include "ghostblock/sub/sub-bass.h"
#include "ghostblock/sub/bombaz-voice.h"
#include "ghostblock/sub/subhoofer-port.h"
#include "sst/cpputils/ring_buffer.h"
#include "sst/filters/CytomicSVF.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <locale>
#include <sstream>
#include <strings.h>
#include <vector>

namespace ghostblock::p2::sub
{
namespace
{
constexpr uint32_t kRing = 32768; // > the largest latency of SPECTRAL FX (16383) + a block step
constexpr uint32_t kMaxCtl = 1024;
constexpr uint32_t kTick = 16;    // host samples between control steps
constexpr uint32_t kTap = 1u << 16;
// LEVEL 0 dB: the sub's fundamental sits near 0.19 (-14 dBFS) on every bass note, its peak near 0.27
constexpr float kSubRef = 0.38f;

using P = SubBass;
const ParamDesc kDesc[SubBass::kParams] = {
    {P::pOn, "on", "Sub On", "SUB", 0, 1, 0, true},
    {P::pLevel, "level", "Sub Level", "LEVEL", -24, 6, 0, false},
    {P::pOctave, "octave", "Sub Octave", "OCTAVE", -2, 0, -1, true},
    {P::pWidth, "width", "Sub Width", "WIDTH", 0, 100, 0, false},
    {P::pDrive, "drive", "Sub Drive", "DRIVE", 0, 100, 0, false},
    {P::pHarmonics, "harmonics", "Sub Harmonics", "HARMONICS", 0, 100, 0, false},
    {P::pMode, "mode", "Sub Harmonic Mode", "MODE", 0, kModes - 1, 0, true},
    {P::pBlend, "blend", "Sub Clean/Dirty", "CLEAN / DIRTY", 0, 100, 50, false},
    {P::pGlide, "glide", "Sub Glide", "GLIDE", 0, 1000, 0, false},
    {P::pTune, "tune", "Sub Tune", "TUNE", -100, 100, 0, false},
    {P::pPhase, "phase", "Sub Phase", "PHASE", 0, 1, 0, true},
    {P::pRoom, "room", "Sub Make Room", "MAKE ROOM", 0, 100, 40, false},
};
inline int ix(uint32_t id) { return (int)(id - SubBass::pOn); }

// ---- the five Subhoofer algorithms as SUB drives them
// Subhoofer's algorithms answer to the LEVEL of what they are fed (they are waveshapers), so the Bombaz pulse is brought
// to one level first, whatever the note: `in` = the peak the algorithm sees. HARMONICS 0..100 % becomes Subhoofer's own
// "Harmonics" value, 0 .. `hmax`, on a square law (so the lower half of the wheel is the subtle half, as on upstream's
// skewed knob). `out` brings each algorithm's layer to about the sub's own loudness at HARMONICS 100 %.
struct ModeVoicing
{
    subhoofer::AlgorithmType algo;
    const char *name, *shortName;
    float in, hmax, out;
    int law; // HARMONICS -> "Harmonics": 1 = straight, 2 = square
};
#ifdef P2SUB_TUNING // (the voicing tool sets these while it searches; the plugin's are constants)
ModeVoicing kMode[kModes] = {
#else
const ModeVoicing kMode[kModes] = {
#endif
    {subhoofer::AlgorithmType::ABass3, "A Bass 3", "A BASS 3", 0.30f, 0.0103f, 1.0f, 2},
    {subhoofer::AlgorithmType::ABass2, "A Bass 2", "A BASS 2", 0.50f, 0.033f, 1.0f, 2},
    {subhoofer::AlgorithmType::BBass, "8 Harmonic Stack", "8 STACK", 1.20f, 0.0122f, 1.0f, 2},
    {subhoofer::AlgorithmType::CBass, "Duro Console", "DURO", 1.60f, 1.0f, 0.86f, 2},
    {subhoofer::AlgorithmType::TanH, "TanH Transfer", "TANH", 4.0f, 1.0f, 4.6f, 1},
};

// ---- the eight presets: octave, width, drive, harmonics, mode, blend, glide (ms), tune (cents), phase, make room
struct Preset
{
    const char *name;
    float octave, width, drive, harmonics, mode, blend, glide, tune, phase, room;
};
const Preset kPreset[kPresets] = {
    {"PURE", -1, 0, 0, 0, 0, 50, 0, 0, 0, 40},
    {"DEEP", -2, 0, 20, 25, 0, 35, 0, 0, 0, 45},
    {"808", -1, 12, 70, 35, 4, 70, 90, 0, 0, 50},
    {"ANALOG", -1, 28, 35, 40, 1, 55, 30, 4, 1, 40},
    {"REESE BODY", -1, 22, 45, 55, 2, 60, 0, -9, 1, 35},
    {"SPEAKER", -1, 40, 30, 80, 0, 80, 0, 0, 0, 40},
    {"RUMBLE", -2, 10, 55, 60, 3, 60, 0, 0, 1, 60},
    {"MONSTER", -1, 55, 100, 100, 2, 100, 0, 0, 0, 55},
};
const uint32_t kPresetIds[10] = {P::pOctave, P::pWidth, P::pDrive, P::pHarmonics, P::pMode, P::pBlend, P::pGlide, P::pTune, P::pPhase, P::pRoom};
inline float presetValue(const Preset &p, int k)
{
    const float v[10] = {p.octave, p.width, p.drive, p.harmonics, p.mode, p.blend, p.glide, p.tune, p.phase, p.room};
    return v[k];
}

struct HostMsg
{
    uint32_t id{0};
    int type{1};
    float value{0};
};

// modified Bessel function of the first kind, order 0 (the Kaiser window)
double besselI0(double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 64; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < 1e-17 * sum)
            break;
    }
    return sum;
}
// a linear-phase low-pass for taking every `m`-th sample: -6 dB at the new Nyquist, 100 dB down where it would fold
// back under 0.375 x the new rate (18 kHz at 48 kHz). `n` odd; its delay is (n - 1) / 2 samples of the high rate.
void designDecimator(std::vector<float> &h, int n, int m)
{
    const double fc = 0.5 / (double)m, beta = 0.1102 * (100.0 - 8.7);
    std::vector<double> d((size_t)n);
    double sum = 0;
    for (int i = 0; i < n; ++i)
    {
        const double t = (double)i - 0.5 * (double)(n - 1);
        const double sinc = t == 0.0 ? 2.0 * fc : std::sin(2.0 * M_PI * fc * t) / (M_PI * t);
        const double r = 2.0 * (double)i / (double)(n - 1) - 1.0;
        d[(size_t)i] = sinc * besselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / besselI0(beta);
        sum += d[(size_t)i];
    }
    h.resize((size_t)n);
    for (int i = 0; i < n; ++i)
        h[(size_t)i] = (float)(d[(size_t)i] / sum);
}
// the layer never runs away: exactly itself up to 1 (it never gets there in normal use), then bends over towards 2
inline float softLimit(float x)
{
    const float a = std::fabs(x);
    if (a <= 1.0f)
        return x;
    const float y = 1.0f + std::tanh(a - 1.0f);
    return x < 0.0f ? -y : y;
}
} // namespace

#ifdef P2SUB_TUNING
void tuneVoicing(int m, float in, float hmax, float out, int law) { kMode[m].in = in; kMode[m].hmax = hmax; kMode[m].out = out; kMode[m].law = law; }
#endif
const char *modeName(int m) { return kMode[std::clamp(m, 0, kModes - 1)].name; }
const char *modeShortName(int m) { return kMode[std::clamp(m, 0, kModes - 1)].shortName; }
const char *presetName(int p) { return kPreset[std::clamp(p, 0, kPresets - 1)].name; }

// ===================================================================================================== Impl
struct SubBass::Impl
{
    // ---------------- settings (atomics: the host on the audio thread and the editor on the main thread both write)
    std::atomic<float> v[SubBass::kParams];
    std::atomic<uint32_t> changes{0};
    std::atomic<bool> resetFlag{false};
    std::atomic<float> peak{0.f}, hzNow{0.f};
    sst::cpputils::SimpleRingBuffer<HostMsg, 1024> toHost;
    // the scope's tap: one writer (audio), one reader (the editor)
    std::atomic<bool> tapOn{false};
    std::vector<float> tap = std::vector<float>(kTap, 0.f);
    std::atomic<uint32_t> tapW{0}, tapR{0};

    // ---------------- audio thread
    double sr{0};
    int os{4};        // the voice runs at os x the host rate (176.4 / 192 kHz)
    float fsOs{192000.f};
    BombazVoice voice;
    subhoofer::Harmonics harm;
    subhoofer::Settings hset;
    std::vector<float> fir, hist; // the decimator: taps, the last n samples (twice, so they are always in a row)
    int firN{0}, histPos{0};
    std::vector<float> ring, hzRing; // the sub and its pitch, as long ago as the FX rack's latency
    uint32_t wpos{0};
    struct Ctl
    {
        uint32_t frame;
        Track t;
    } ctl[kMaxCtl];
    Track cur;
    bool haveNew{false};
    uint32_t tickPhase{0};
    bool running{false};
    float master{0.f}; // SUB ON's own fade (20 ms)
    // the note
    bool following{false};
    int lastKey{-1};
    uint32_t lastAttack{0};
    int restartStage{0}; // 0 none, 1 the old pulse fades out, 2 the new one fades in
    float restartHz{0.f};
    float gate{1.f};
    float env{0.f}, envTarget{0.f};
    double idle{0};
    // smoothed settings
    float levelLin{1.f}, levelMid{1.f}, levelTarget{1.f}, blend{0.5f}, blendMid{0.5f}, blendTarget{0.5f}, harmAmt{0.f}, harmMid{0.f}, harmTarget{0.f};
    float hNow{0.f}, hStep{0.f}, hTo{0.f}; // Subhoofer's "Harmonics" as it is this sample: it runs straight from one control step's value to the next
    int hLeft{0};
    uint32_t sinceTick{kTick}; // host samples since the last control step
    float width{0.f}, drive{0.f};
    int mode{0};
    bool dirtyRuns{false};
    // the harmonic layer
    int modeWanted{0};
    float inGain{1.f}, outGain{1.f};
    float sumDuty{1.f};
    double sN{0}, sRaw{0}, sX{0}, sXX{0}, sP0{0}, sP1{0}, sXP0{0}, sXP1{0}; // one cycle's sums (the balance, see renderSub)
    float mean{0.35875f};                // the pulse's average over its own length (0.359 with no DRIVE)
    float cycLoudInv{1.f}, cycDuty{1.f}; // the cycle that is playing: 1 / its pulse's height, the part of it the pulse takes
    // where the algorithm is between two pulses: what it is fed there (x0) and what it answers (p0d + harmonics * p1d);
    // they move to a new cycle's values over 2 ms
    float x0{0.f}, p0d{0.f}, p1d{0.f}, x0T{0.f}, p0dT{0.f}, p1dT{0.f}, x0Step{0.f}, p0dStep{0.f}, p1dStep{0.f};
    int rampLeft{0}, rampLen{1};
    subhoofer::Harmonics probe;
    bool firstCycle{true};
    bool layerWaits{true}, layerRan{false}; // the layer joins at the start of a cycle; it ran in the last sample
    float harmGate{0.f};                    // the layer's own fade around HARMONICS 0
    float g0{0.f}, g1{0.f}, g0T{0.f}, g1T{0.f};
    float layerFade{1.f};
    float loud{1.f}, loudSm{1.f};
    // MAKE ROOM
    sst::filters::CytomicSVF hp[2];
    float roomMix{0.f}, roomHzLog{std::log2(30.f)}, lastHz{0.f};
    bool roomWarm{false};
    // one-pole steps
    float kEnv{0.f}, kLevel{0.f}, kBlend{0.f}, kGate{0.f}, kMaster{0.f}, kBal{0.f}, kFade{0.f}, kLoud{0.f};

    Impl()
    {
        for (uint32_t i = 0; i < SubBass::kParams; ++i)
            v[i].store(kDesc[i].def);
        ring.assign(kRing, 0.f);
        hzRing.assign(kRing, 0.f);
    }
    float get(uint32_t id) const { return v[ix(id)].load(std::memory_order_relaxed); }

    // main thread, audio stopped
    void prepare(double sampleRate)
    {
        sr = sampleRate;
        os = sampleRate < 60000.0 ? 4 : sampleRate < 120000.0 ? 2 : 1;
        fsOs = (float)(sampleRate * os);
        firN = os == 4 ? 105 : os == 2 ? 53 : 1;
        if (os > 1)
            designDecimator(fir, firN, os);
        else
            fir.assign(1, 1.f);
        hist.assign((size_t)firN * 2, 0.f);
        voice.prepare(fsOs);
        // Subhoofer's DC high-pass has one fixed coefficient (-0.995: 35 Hz at 44.1 kHz, 153 Hz at 192 kHz). Here it only
        // has to take the DC off the layer, under every note: 12 Hz at the rate this runs at.
        harm.hp_a1 = -(1.0f - 6.2831853f * 12.0f / fsOs);
        // A Bass 3 (SweetenX) changes its nature with the sample rate; it is run as it is at 44.1 / 48 kHz
        hset.overall_scale = 1.0f;
        auto pole = [&](double seconds, double rate) { return (float)(1.0 - std::exp(-1.0 / (seconds * rate))); };
        kEnv = pole(0.0015, fsOs);
        kLevel = pole(0.010, fsOs);
        kBlend = pole(0.010, fsOs);
        kBal = pole(0.004, fsOs);
        rampLen = std::max(1, (int)(0.002 * fsOs));
        kLoud = pole(0.004, fsOs);
        kGate = (float)(1.0 / (0.003 * fsOs));
        kFade = (float)(1.0 / (0.010 * sr)); // (per host sample: the layer steps aside while the algorithm is changed)
        kMaster = (float)(1.0 / (0.020 * sr));
        hardReset();
        running = false;
        master = 0.f;
    }

    void hardReset()
    {
        voice.reset();
        harm.reset();
        std::fill(hist.begin(), hist.end(), 0.f);
        std::fill(ring.begin(), ring.end(), 0.f);
        std::fill(hzRing.begin(), hzRing.end(), 0.f);
        histPos = 0;
        following = false;
        lastKey = -1;
        restartStage = 0;
        gate = 1.f;
        env = envTarget = 0.f;
        idle = 0;
        mode = modeWanted = std::clamp((int)std::lround(get(P::pMode)), 0, kModes - 1);
        newLayer();
        layerFade = 1.f;
        layerRan = false;
        harmGate = 0.f;
        dirtyRuns = false;
        for (auto &f : hp)
        {
            f.init();
            f.firstBlock = true;
        }
        roomMix = 0.f;
        roomWarm = false;
        cur = Track();
        haveNew = false;
        tickPhase = 0;
        sinceTick = kTick;
        readSettings(true);
    }

    void readSettings(bool snap, uint32_t elapsed = kTick)
    {
        levelTarget = kSubRef * std::pow(10.f, std::clamp(get(P::pLevel), -24.f, 6.f) / 20.f);
        blendTarget = std::clamp(get(P::pBlend), 0.f, 100.f) * 0.01f;
        harmTarget = std::clamp(get(P::pHarmonics), 0.f, 100.f) * 0.01f;
        const float w = std::clamp(get(P::pWidth), 0.f, 100.f), d = std::clamp(get(P::pDrive), 0.f, 100.f);
        modeWanted = std::clamp((int)std::lround(get(P::pMode)), 0, kModes - 1); // (renderSub changes over to it)
        if (snap)
        {
            levelLin = levelMid = levelTarget;
            blend = blendMid = blendTarget;
            harmAmt = harmMid = harmTarget;
            width = w;
            drive = d;
        }
        else
        { // (the voice takes these at the start of a pulse; a step every 16 samples is far finer than that)
            const float k = 1.0f - std::exp(-(float)elapsed / (0.02f * (float)sr));
            width += (w - width) * k;
            drive += (d - drive) * k;
            harmMid += (harmTarget - harmMid) * k; // (two steps, as CLEAN / DIRTY and LEVEL)
            harmAmt += (harmMid - harmAmt) * k;
            if (std::fabs(harmTarget - harmAmt) < 1e-5f && std::fabs(harmTarget - harmMid) < 1e-5f)
                harmAmt = harmMid = harmTarget;
        }
        voice.setWidth(width);
        voice.setDrive(drive);
        voice.setGlideMs(std::clamp(get(P::pGlide), 0.f, 1000.f));
        const auto &mv = kMode[mode];
        hset.h_algorithm = mv.algo;
        hTo = mv.hmax * (mv.law == 2 ? harmAmt * harmAmt : harmAmt);
        if (snap || hTo == hNow)
        {
            hNow = hTo;
            hStep = 0.f;
            hLeft = 0;
        }
        else
        { // (it gets there in as many samples as the last control step took)
            hLeft = (int)(elapsed * (uint32_t)os);
            hStep = (hTo - hNow) / (float)hLeft;
        }
        hset.harmonics = hNow;
    }

    // the layer starts from nothing (another algorithm, a reset)
    void newLayer()
    {
        harm.reset();
        newBalance();
        g0 = g1 = g0T = g1T = 0.f;
        const auto &mv = kMode[mode];
        inGain = mv.in;
        outGain = mv.out / mv.in;
        hset.h_algorithm = mv.algo;
        deadPoint(true);
        layerWaits = true; // (it joins when the next cycle begins, the way it joins a note)
    }
    // between two pulses the algorithm is fed one value, x0 (the pulse's average, upside down), and answers one value.
    // The layer is counted from there, so it is exactly nothing whenever the pulse is.
    void deadPoint(bool atOnce)
    {
        x0T = -mean * cycDuty * inGain;
        const float h = hset.harmonics > 0.f ? hset.harmonics : 1e-6f;
        subhoofer::Settings st = hset;
        st.harmonics = h;
        probe.hp_a1 = harm.hp_a1;
        probe.reset();
        probe.shapeLeft(x0T, st); // (A Bass 3 remembers one sample: the second answer is the lasting one)
        const float pd = probe.shapeLeft(x0T, st);
        p0dT = subhoofer::Harmonics::shapeZero(x0T, st);
        p1dT = (pd - p0dT) / h;
        if (atOnce)
        {
            x0 = x0T;
            p0d = p0dT;
            p1d = p1dT;
            rampLeft = 0;
        }
        else
        {
            rampLeft = rampLen;
            const float k = 1.0f / (float)rampLen;
            x0Step = (x0T - x0) * k;
            p0dStep = (p0dT - p0d) * k;
            p1dStep = (p1dT - p1d) * k;
        }
    }
    // the layer's balance is measured anew, from the pulse that begins now
    void newBalance()
    {
        sN = sRaw = sX = sXX = sP0 = sP1 = sXP0 = sXP1 = 0;
        firstCycle = true;
    }
    // a new pulse begins from nothing
    void freshPulse(float hz)
    {
        voice.hardRestart(hz);
        newBalance();
        takeCycle();
        loudSm = loud;
        deadPoint(true);
        harm.primeLeft(0.f);
        layerWaits = false;
    }
    // the voice began a cycle: what its pulse is
    void takeCycle()
    {
        loud = std::max(1e-3f, voice.cycleLoudness());
        cycLoudInv = 1.0f / loud;
        cycDuty = voice.cycleDuty();
    }

    void beginRestart(float hz)
    {
        if (env * gate < 0.002f || !voice.active())
        { // nothing to hear of the old pulse: the new one starts here
            freshPulse(hz);
            restartStage = 0;
            gate = 1.f;
            return;
        }
        restartHz = hz;
        restartStage = 1;
    }

    // what the engine plays -> the voice (only when the engine said something new)
    void applyTrack()
    {
        const Track &t = cur;
        const bool restartMode = get(P::pPhase) < 0.5f;
        if (t.sounding && t.hz > 0.f)
        {
            const int oct = std::clamp((int)std::lround(get(P::pOctave)), -2, 0);
            const float hz = std::clamp(t.hz * (oct == -2 ? 0.25f : oct == -1 ? 0.5f : 1.0f), 8.0f, 4000.0f);
            const float mod = std::exp2(std::clamp(t.modSemis, -48.f, 48.f) / 12.0f + std::clamp(get(P::pTune), -100.f, 100.f) / 1200.0f);
            voice.setModulation(mod);
            const bool newAttack = t.attack > lastAttack;
            lastAttack = std::max(lastAttack, t.attack);
            if (restartStage == 1)
                restartHz = hz; // (the old pulse is still fading: the new one will start where the note is by then)
            else if (!following)
            {
                if (!voice.active())
                    freshPulse(hz);
                else if (restartMode)
                    beginRestart(hz);
                else
                    voice.follow(hz);
            }
            else if (newAttack)
            {
                if (restartMode)
                    beginRestart(hz);
                else
                    voice.follow(hz);
            }
            else if (t.key != lastKey)
            { // another key while one sounds: Bombaz's legato (it glides when GLIDE is on) - unless the engine glides itself
                if (t.engineGlides)
                    voice.follow(hz);
                else
                    voice.legato(hz);
            }
            else
                voice.follow(hz);
            following = true;
            lastKey = t.key;
            envTarget = std::clamp(t.env, 0.f, 1.f);
            idle = 0;
        }
        else
        {
            if (following)
            {
                following = false;
                if (restartMode)
                    voice.stop(); // (FREE: it runs on, unheard)
            }
            envTarget = 0.f;
        }
    }

    void tick(bool on)
    {
        readSettings(false, std::max<uint32_t>(1, sinceTick));
        sinceTick = 0;
        if (haveNew)
        {
            haveNew = false;
            applyTrack();
        }
        else if (following)
            voice.setModulation(std::exp2(std::clamp(cur.modSemis, -48.f, 48.f) / 12.0f + std::clamp(get(P::pTune), -100.f, 100.f) / 1200.0f));
        voice.pushParameters();
        if (!following && voice.active() && get(P::pPhase) >= 0.5f && idle > 5.0)
            voice.stop(); // FREE: after five seconds of nobody playing it stops (the next note starts a pulse of its own)
        (void)on;
    }

    // n host samples of the sub into `out`, its pitch into `hz`
    void renderSub(float *out, float *hz, uint32_t n)
    {
        for (uint32_t i = 0; i < n; ++i)
        {
            const bool wantDirty = blend > 0.f || blendTarget > 0.f;
            if (wantDirty && !dirtyRuns)
                voice.relockDirty(); // the DIRTY side comes back: it joins the CLEAN pulse where that is
            dirtyRuns = wantDirty;
            const bool layerOn = wantDirty && (harmAmt > 0.f || harmTarget > 0.f) && hset.harmonics > 0.f;
            if (layerOn && !layerRan)
                layerWaits = true; // it was off: it joins when the next cycle (or the next note) begins
            layerRan = layerOn;
            // another algorithm: the layer steps aside (10 ms), changes over, and the new one joins at the next cycle
            if (modeWanted != mode)
            {
                layerFade -= kFade;
                if (layerFade <= 0.f || !layerOn || layerWaits)
                {
                    mode = modeWanted;
                    const auto &mv = kMode[mode];
                    hset.h_algorithm = mv.algo;
                    hNow = hTo = mv.hmax * (mv.law == 2 ? harmAmt * harmAmt : harmAmt);
                    hStep = 0.f;
                    hLeft = 0;
                    hset.harmonics = hNow;
                    newLayer();
                    layerFade = 1.f;
                }
            }
            else if (layerFade < 1.f)
                layerFade = std::min(1.f, layerFade + kFade);
            const float harmGateT = std::min(1.0f, harmAmt * 50.0f); // (under HARMONICS 2 % the layer fades out entirely)
            for (int k = 0; k < os; ++k)
            {
                // ---- the restart of the pulse, between two short fades
                if (restartStage == 1)
                {
                    gate -= kGate;
                    if (gate <= 0.f)
                    {
                        gate = 0.f;
                        freshPulse(restartHz);
                        restartStage = 2;
                    }
                }
                else if (restartStage == 2)
                {
                    gate += kGate;
                    if (gate >= 1.f)
                    {
                        gate = 1.f;
                        restartStage = 0;
                    }
                }
                float c, d, dp;
                voice.render(c, d, dp, wantDirty);
                const bool began = voice.cycleBegan();
                if (began)
                    takeCycle();
                blendMid += (blendTarget - blendMid) * kBlend; // (two steps: a wheel thrown over starts to move without a corner)
                blend += (blendMid - blend) * kBlend;
                if (blendTarget <= 0.f && blend < 1e-5f)
                    blend = blendMid = 0.f;
                if (hLeft > 0)
                {
                    hNow = --hLeft == 0 ? hTo : hNow + hStep;
                    hset.harmonics = hNow;
                }
                float mix = c;
                if (wantDirty)
                {
                    float dirt = d;
                    if (layerOn && layerWaits && began && voice.active())
                    { // the layer joins here, at the start of a cycle, exactly as it joins a new note
                        newBalance();
                        deadPoint(true);
                        harm.primeLeft(0.f);
                        loudSm = loud;
                        layerWaits = false;
                    }
                    if (layerOn && !layerWaits)
                    {
                        // ---- Subhoofer's algorithm on the DIRTY pulse, brought to one level whatever the note:
                        // the pulse itself (0 .. 1), moved down by its own average so it swings around 0 as the sub
                        // does - from its very first cycle on, where Bombaz's DC killer has not settled yet
                        loudSm += (loud - loudSm) * kLoud;
                        if (rampLeft > 0)
                        {
                            x0 += x0Step;
                            p0d += p0dStep;
                            p1d += p1dStep;
                            if (--rampLeft == 0)
                            {
                                x0 = x0T;
                                p0d = p0dT;
                                p1d = p1dT;
                            }
                        }
                        const float xn = dp * cycLoudInv;
                        const float x = xn * inGain + x0;
                        const float p = harm.shapeLeft(x, hset);
                        // What of its answer is only a copy of the pulse (louder, quieter or upside down) is taken out
                        // again: the layer is the new overtones alone, so it can never thin the sub out or cancel it.
                        // How much of a copy there is - the balance - is the straight line that fits the algorithm's
                        // answer best over one whole cycle of the pulse, measured cycle after cycle as it plays, in two
                        // parts: what the algorithm gives with no harmonics at all (p0), and what its harmonics add
                        // per unit of "Harmonics" (p1; so turning the wheel needs no new measuring).
                        const float p0 = subhoofer::Harmonics::shapeZero(x, hset);
                        const float p1 = (p - p0) / hset.harmonics;
                        if (voice.active())
                        {
                            if (began && sN > 0.0)
                            { // a whole cycle has been played: its balance, its pulse's average
                                const double var = sXX - sX * sX / sN;
                                if (var > 1e-12)
                                {
                                    g0T = (float)((sXP0 - sX * sP0 / sN) / var);
                                    g1T = (float)((sXP1 - sX * sP1 / sN) / var);
                                }
                                if (firstCycle)
                                {
                                    g0 = g0T;
                                    g1 = g1T;
                                    firstCycle = false;
                                }
                                if (sumDuty > 0.01f)
                                    mean = std::clamp((float)(sRaw / sN) / sumDuty, 0.2f, 0.9f); // (it moves with DRIVE)
                                sN = sRaw = sX = sXX = sP0 = sP1 = sXP0 = sXP1 = 0;
                                deadPoint(false);
                            }
                            sumDuty = cycDuty;
                            sN += 1.0;
                            sRaw += xn;
                            sX += x;
                            sXX += (double)x * x;
                            sP0 += p0;
                            sP1 += p1;
                            sXP0 += (double)x * p0;
                            sXP1 += (double)x * p1;
                            if (firstCycle)
                            { // (a pulse's first cycle: the best fit to what there has been of it so far)
                                const double inv = 1.0 / (sXX - sX * sX / sN + 1e-4 * inGain * inGain * sN);
                                g0 = g0T = (float)((sXP0 - sX * sP0 / sN) * inv);
                                g1 = g1T = (float)((sXP1 - sX * sP1 / sN) * inv);
                            }
                        }
                        if (!firstCycle)
                        {
                            g0 += (g0T - g0) * kBal;
                            g1 += (g1T - g1) * kBal;
                        }
                        const float u = (p - (p0d + hset.harmonics * p1d)) - (g0 + hset.harmonics * g1) * (x - x0);
                        float lay = softLimit(harm.dcLeft(u));
                        if (!std::isfinite(lay))
                        { // (never seen; a waveshaper fed something absurd must not poison the sub)
                            lay = 0.f;
                            newLayer();
                        }
                        // (a pulse's first cycle: the layer comes up with it, as its balance is found)
                        const float ph = voice.cyclePhase();
                        const float bloom = firstCycle ? ph * ph * (3.0f - 2.0f * ph) : 1.0f;
                        harmGate += (harmGateT - harmGate) * kBlend;
                        dirt += lay * bloom * layerFade * harmGate * loudSm * outGain;
                    }
                    mix = c + blend * (dirt - c);
                }
                env += (envTarget - env) * kEnv;
                levelMid += (levelTarget - levelMid) * kLevel;
                levelLin += (levelMid - levelLin) * kLevel;
                const float gateShape = restartStage == 0 ? 1.f : 0.5f - 0.5f * std::cos(3.14159265f * gate); // (a raised cosine: no corners)
                const float s = mix * env * gateShape * levelLin;
                // ---- down to the host rate
                hist[(size_t)histPos] = s;
                hist[(size_t)(histPos + firN)] = s;
                if (++histPos >= firN)
                    histPos = 0;
            }
            float y = 0.f;
            if (os > 1)
            {
                const float *w = hist.data() + histPos;
                for (int j = 0; j < firN; ++j)
                    y += fir[(size_t)j] * w[j];
            }
            else
                y = hist[(size_t)(histPos == 0 ? firN - 1 : histPos - 1)];
            out[i] = y;
            const float f = voice.active() ? voice.frequency() : 0.f;
            hz[i] = f;
        }
    }
};

// ===================================================================================================== SubBass
SubBass::SubBass() : impl(std::make_unique<Impl>()) {}
SubBass::~SubBass() = default;

const ParamDesc &SubBass::desc(uint32_t id) { return kDesc[std::clamp(ix(id), 0, (int)kParams - 1)]; }

bool SubBass::paramInfo(uint32_t index, clap_param_info *info) const
{
    if (index >= kParams)
        return false;
    std::memset(info, 0, sizeof(*info));
    const auto &d = kDesc[index];
    info->id = d.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | (d.stepped ? CLAP_PARAM_IS_STEPPED : 0);
    std::strncpy(info->module, "Sub", CLAP_PATH_SIZE - 1);
    std::strncpy(info->name, d.name, CLAP_NAME_SIZE - 1);
    info->min_value = d.min;
    info->max_value = d.max;
    info->default_value = d.def;
    return true;
}

double SubBass::getParam(uint32_t id) const
{
    if (!isParam(id))
        return 0;
    if (id == pOn)
        return onFlag.load() ? 1 : 0;
    return impl->v[ix(id)].load();
}

void SubBass::setParam(uint32_t id, double val)
{
    if (!isParam(id) || !std::isfinite(val))
        return;
    const auto &d = kDesc[ix(id)];
    float x = (float)std::clamp(val, (double)d.min, (double)d.max);
    if (d.stepped)
        x = std::round(x);
    impl->v[ix(id)].store(x);
    if (id == pOn)
        onFlag = x > 0.5f;
    impl->changes.fetch_add(1, std::memory_order_relaxed);
}

bool SubBass::valueToText(uint32_t id, double val, char *out, uint32_t n) const
{
    if (!isParam(id))
        return false;
    char b[64];
    switch (id)
    {
    case pOn: std::snprintf(b, sizeof(b), "%s", val > 0.5 ? "On" : "Off"); break;
    case pLevel: std::snprintf(b, sizeof(b), "%.1f dB", val); break;
    case pOctave: std::snprintf(b, sizeof(b), "%d", (int)std::lround(val)); break;
    case pMode: std::snprintf(b, sizeof(b), "%s", modeName((int)std::lround(val))); break;
    case pGlide:
        if (val < 10.0) std::snprintf(b, sizeof(b), "off");
        else if (val < 1000.0) std::snprintf(b, sizeof(b), "%d ms", (int)val);
        else std::snprintf(b, sizeof(b), "%.2f s", val / 1000.0);
        break;
    case pTune: std::snprintf(b, sizeof(b), "%d cents", (int)std::lround(val)); break;
    case pPhase: std::snprintf(b, sizeof(b), "%s", val > 0.5 ? "Free" : "Restart"); break;
    case pRoom:
        if (val < 0.5) std::snprintf(b, sizeof(b), "off");
        else std::snprintf(b, sizeof(b), "%d %%", (int)std::lround(val));
        break;
    default: std::snprintf(b, sizeof(b), "%d %%", (int)std::lround(val)); break;
    }
    std::snprintf(out, n, "%s", b);
    return true;
}

bool SubBass::textToValue(uint32_t id, const char *txt, double *val) const
{
    if (!isParam(id) || !txt)
        return false;
    const auto &d = kDesc[ix(id)];
    if (id == pOn)
    {
        *val = (strcasecmp(txt, "on") == 0 || std::atof(txt) > 0.5) ? 1 : 0;
        return true;
    }
    if (id == pPhase && (strncasecmp(txt, "free", 4) == 0 || strncasecmp(txt, "restart", 7) == 0))
    {
        *val = strncasecmp(txt, "free", 4) == 0 ? 1 : 0;
        return true;
    }
    if (id == pMode)
        for (int m = 0; m < kModes; ++m)
            if (strcasecmp(txt, modeName(m)) == 0 || strcasecmp(txt, modeShortName(m)) == 0)
            {
                *val = m;
                return true;
            }
    if ((id == pGlide || id == pRoom) && strncasecmp(txt, "off", 3) == 0)
    {
        *val = 0;
        return true;
    }
    char *end = nullptr;
    double x = std::strtod(txt, &end);
    if (end == txt)
        return false;
    if (id == pGlide && end && (*end == 's' || (*end == ' ' && end[1] == 's')))
        x *= 1000.0; // "1.5 s"
    *val = std::clamp(x, (double)d.min, (double)d.max);
    return true;
}

// ---- editor
void SubBass::uiEdit(uint32_t id, double val, int type)
{
    if (!isParam(id))
        return;
    if (type == 1)
        setParam(id, val);
    impl->toHost.push(HostMsg{id, type, (float)getParam(id)});
}
void SubBass::uiPreset(int p)
{
    if (p < 0 || p >= kPresets)
        return;
    for (int k = 0; k < 10; ++k)
        uiSet(kPresetIds[k], presetValue(kPreset[p], k));
    if (!on())
        uiSet(pOn, 1.0);
}
int SubBass::currentPreset() const
{
    for (int p = 0; p < kPresets; ++p)
    {
        bool same = true;
        for (int k = 0; k < 10 && same; ++k)
            same = std::fabs(impl->get(kPresetIds[k]) - presetValue(kPreset[p], k)) < 0.01f;
        if (same)
            return p;
    }
    return -1;
}
uint32_t SubBass::changeCount() const { return impl->changes.load(std::memory_order_relaxed); }
float SubBass::level() const { return impl->peak.load(std::memory_order_relaxed); }
float SubBass::pitchHz() const { return impl->hzNow.load(std::memory_order_relaxed); }
void SubBass::tapOpen(bool open)
{
    if (open)
        impl->tapR.store(impl->tapW.load(std::memory_order_acquire), std::memory_order_release); // (start from now)
    impl->tapOn.store(open, std::memory_order_release);
}
uint32_t SubBass::tapRead(float *dst, uint32_t max)
{
    auto &I = *impl;
    const uint32_t w = I.tapW.load(std::memory_order_acquire);
    uint32_t r = I.tapR.load(std::memory_order_relaxed);
    uint32_t n = 0;
    if (w - r > kTap)
        r = w - kTap; // (the reader fell a whole ring behind)
    while (r != w && n < max)
        dst[n++] = I.tap[(r++) & (kTap - 1)];
    I.tapR.store(r, std::memory_order_release);
    return n;
}

// ---- glue
void SubBass::activate(double sampleRate)
{
    impl->prepare(sampleRate);
    nCtl = 0;
    inPath = false;
}
void SubBass::requestReset() noexcept { impl->resetFlag.store(true, std::memory_order_relaxed); }
void SubBass::sendToHost(const clap_output_events *out)
{
    while (auto m = impl->toHost.pop())
    {
        if (!out)
            continue;
        if (m->type == 1)
        {
            clap_event_param_value e{};
            e.header.size = sizeof(e);
            e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            e.header.type = CLAP_EVENT_PARAM_VALUE;
            e.param_id = m->id;
            e.note_id = -1;
            e.port_index = -1;
            e.channel = -1;
            e.key = -1;
            e.value = m->value;
            out->try_push(out, &e.header);
        }
        else
        {
            clap_event_param_gesture e{};
            e.header.size = sizeof(e);
            e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            e.header.type = m->type == 0 ? CLAP_EVENT_PARAM_GESTURE_BEGIN : CLAP_EVENT_PARAM_GESTURE_END;
            e.param_id = m->id;
            out->try_push(out, &e.header);
        }
    }
}

void SubBass::track(uint32_t frame, const Track &t) noexcept
{
    auto &I = *impl;
    if (nCtl < kMaxCtl)
        I.ctl[nCtl++] = {frame, t};
    else
        I.ctl[kMaxCtl - 1] = {I.ctl[kMaxCtl - 1].frame, t}; // (a block longer than 1024 engine steps: the newest wins)
}

void SubBass::processImpl(float *L, float *R, uint32_t frames, uint32_t delay) noexcept
{
    auto &I = *impl;
    if (I.sr <= 0)
    {
        nCtl = 0;
        return;
    }
    const bool on = onFlag.load(std::memory_order_relaxed);
    if (I.resetFlag.exchange(false, std::memory_order_relaxed))
    {
        const float m = I.master;
        I.hardReset();
        I.master = m;
    }
    if (on && !I.running)
    {
        I.hardReset();
        I.master = 0.f;
        I.running = true;
    }
    delay = std::min<uint32_t>(delay, kRing - 2 * kTick);
    const float srInv = (float)(1.0 / I.sr);
    float pk = 0.f;
    uint32_t pos = 0, ci = 0;
    while (pos < frames)
    {
        while (ci < nCtl && I.ctl[ci].frame <= pos)
        {
            I.cur = I.ctl[ci].t;
            I.haveNew = true;
            ++ci;
        }
        if (I.tickPhase == 0 || I.haveNew)
            I.tick(on);
        uint32_t n = std::min<uint32_t>(frames - pos, kTick - I.tickPhase);
        if (ci < nCtl)
            n = std::min<uint32_t>(n, I.ctl[ci].frame - pos);
        n = std::max<uint32_t>(n, 1);
        float sub[kTick], hz[kTick];
        I.renderSub(sub, hz, n);
        I.sinceTick += n;
        if (!I.following)
            I.idle += (double)n / I.sr;
        // ---- MAKE ROOM: where the cut sits now (it follows the sub's own pitch, as late as the FX rack is)
        const float roomAmt = std::clamp(I.get(pRoom), 0.f, 100.f) * 0.01f;
        const bool roomOn = on && roomAmt >= 0.005f;
        const bool roomRuns = roomOn || I.roomMix > 0.f;
        if (roomRuns)
        {
            const float held = I.hzRing[(I.wpos - delay) & (kRing - 1)];
            if (held > 0.f)
                I.lastHz = held;
            const float fc = std::clamp((0.5f + 2.0f * roomAmt) * I.lastHz, 15.f, 500.f);
            const float target = std::log2(fc);
            if (!I.roomWarm)
            {
                I.roomHzLog = target;
                for (auto &f : I.hp)
                {
                    f.init();
                    f.firstBlock = true;
                }
                I.roomWarm = true;
            }
            I.roomHzLog += (target - I.roomHzLog) * (1.0f - std::exp(-(float)n / (0.03f * (float)I.sr)));
            const float f0 = std::exp2(I.roomHzLog);
            // two 12 dB / octave Butterworth high-passes in a row (Linkwitz-Riley, 24 dB / octave)
            for (auto &f : I.hp)
                f.setCoeffForBlock<(int)kTick>(sst::filters::CytomicSVF::Mode::Highpass, f0, 0.29289f, srInv);
        }
        else
            I.roomWarm = false;
        for (uint32_t i = 0; i < n; ++i)
        {
            const uint32_t w = I.wpos + i;
            I.ring[w & (kRing - 1)] = sub[i];
            I.hzRing[w & (kRing - 1)] = hz[i];
            I.master += std::clamp((on ? 1.f : 0.f) - I.master, -I.kMaster, I.kMaster);
            const float ms = I.master * I.master * (3.0f - 2.0f * I.master); // (the fades have no corners)
            if (roomRuns)
            {
                I.roomMix += std::clamp((roomOn ? 1.f : 0.f) - I.roomMix, -I.kMaster, I.kMaster);
                float l = L[pos + i], r = R[pos + i];
                I.hp[0].processBlockStep(l, r);
                I.hp[1].processBlockStep(l, r);
                const float m = I.roomMix * I.roomMix * (3.0f - 2.0f * I.roomMix) * ms;
                L[pos + i] += m * (l - L[pos + i]);
                R[pos + i] += m * (r - R[pos + i]);
            }
            const float s = I.ring[(w - delay) & (kRing - 1)] * ms;
            L[pos + i] += s;
            R[pos + i] += s;
            pk = std::max(pk, std::fabs(s));
        }
        if (I.tapOn.load(std::memory_order_relaxed))
        {
            uint32_t tw = I.tapW.load(std::memory_order_relaxed);
            const float ms = I.master * I.master * (3.0f - 2.0f * I.master);
            for (uint32_t i = 0; i < n; ++i)
                I.tap[(tw++) & (kTap - 1)] = sub[i] * ms;
            I.tapW.store(tw, std::memory_order_release);
        }
        I.wpos += n;
        pos += n;
        I.tickPhase = (I.tickPhase + n) % kTick;
    }
    nCtl = 0;
    I.peak.store(std::max(pk, I.peak.load(std::memory_order_relaxed) * 0.9f), std::memory_order_relaxed);
    I.hzNow.store(I.following && I.voice.active() ? I.voice.frequency() : 0.f, std::memory_order_relaxed);
    if (!on && I.master <= 0.f)
    {
        I.running = false;
        I.peak.store(0.f, std::memory_order_relaxed);
    }
    inPath.store(I.running, std::memory_order_relaxed);
}

// ---- state
std::string SubBass::toState() const
{
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o.precision(9);
    o << "P2SUB 1\n";
    for (uint32_t i = 0; i < kParams; ++i)
        o << kDesc[i].key << " " << getParam(kDesc[i].id) << "\n";
    o << "end\n";
    return o.str();
}
void SubBass::resetToDefaults()
{
    for (uint32_t i = 0; i < kParams; ++i)
        setParam(kDesc[i].id, kDesc[i].def);
}
bool SubBass::fromState(const std::string &st)
{
    std::istringstream in(st);
    in.imbue(std::locale::classic());
    std::string tag;
    int ver = 0;
    in >> tag >> ver;
    if (tag != "P2SUB" || ver < 1)
        return false;
    resetToDefaults();
    std::string key;
    while (in >> key && key != "end")
    {
        double val = 0;
        if (!(in >> val))
            return false;
        for (uint32_t i = 0; i < kParams; ++i)
            if (key == kDesc[i].key)
                setParam(kDesc[i].id, val);
        // (a key from a newer version: skipped)
    }
    return true;
}
} // namespace ghostblock::p2::sub
