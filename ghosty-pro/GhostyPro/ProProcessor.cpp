#include "ProProcessor.h"
#include "HostNotify.h"
#include "ParamText.h"
#include "ExportGate.h"   // demo: plugin-made wavetables stay off disk
#include <set>
#include <map>
#include "GhostThemes.h"
#include <cstring>
#include "ProBank.h"
#include "ProEditor.h"

using namespace gpro;

// ------------------------------------------------------------------ names
namespace gpro
{
const juce::StringArray& filterTypeNames() { static juce::StringArray s { "LP 12", "BP", "HP 12", "Notch", "LP 24", "HP 24", "Comb +", "Comb -", "Formant", "Off" }; return s; }
const juce::StringArray& lfoShapeNames()   { static juce::StringArray s { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "S&H", "Smooth" }; return s; }
const juce::StringArray& lfoDivNames()     { static juce::StringArray s { "4/1", "2/1", "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/2 T", "1/4 T", "1/8 T", "1/16 T", "1/4 D", "1/8 D" }; return s; }
const juce::StringArray& modSourceNames()  { static juce::StringArray s { "-", "LFO 1", "LFO 2", "Env 2", "Env 3", "Filter Env", "Amp Env", "Velocity", "Mod Wheel", "Aftertouch", "Random", "Note", "Macro 1", "Macro 2", "Macro 3", "Macro 4", "Bend Lane" }; return s; }
const juce::StringArray& modDestNames()    { static juce::StringArray s { "-", "Pitch", "Fine Pitch", "Osc 1 Pitch", "Osc 2 Pitch", "Osc 1 Pos", "Osc 2 Pos", "Osc 1 Level", "Osc 2 Level",
                                                                          "Osc 1 Detune", "Osc 2 Detune", "Unison Spread", "X-Mod", "Sub Level", "Noise Level", "Cutoff", "Resonance",
                                                                          "Filter Drive", "Volume", "Pan", "LFO 1 Rate", "LFO 2 Rate", "Growl",
                                                                          "Character", "Tape Amount", "Layer 2 Level", "Layer 3 Level", "Sample Start",
                                                                          "Osc 1 Warp 1", "Osc 1 Warp 2", "Osc 2 Warp 1", "Osc 2 Warp 2",
                                                                          "Osc 1 Engine H", "Osc 1 Engine T", "Osc 1 Engine M", "Osc 2 Engine H", "Osc 2 Engine T", "Osc 2 Engine M" }; return s; }   // (+ ENGINES, round 3)
const juce::StringArray& xmodNames()       { static juce::StringArray s { "Off", "FM", "Sync", "Ring" }; return s; }
const juce::StringArray& subOctNames()     { static juce::StringArray s { "-1 oct", "-2 oct" }; return s; }
const juce::StringArray& subShapeNames()   { static juce::StringArray s { "Sine", "Triangle", "Square" }; return s; }
const juce::StringArray& stealPriorityNames() { static juce::StringArray s { "Newest", "Oldest", "Highest", "Lowest", "Quietest" }; return s; }
}

static double lfoDivQuarterNotes (int i)
{
    static const double q[] { 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 4.0 / 3.0, 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0, 1.5, 0.75 };
    return q[juce::jlimit (0, 13, i)];
}

static inline juce::uint32 hash32 (juce::uint32 x) noexcept
{
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x;
}

float ProProcessor::lfoValue (int shape, double ph, juce::uint32 seed) noexcept
{
    const double cyc = std::floor (ph);
    const float fr = (float) (ph - cyc);
    switch (shape)
    {
        case 1: return 1.0f - 4.0f * std::abs (fr - 0.5f);
        case 2: return 2.0f * fr - 1.0f;
        case 3: return 1.0f - 2.0f * fr;
        case 4: return fr < 0.5f ? 1.0f : -1.0f;
        case 5: return (float) (hash32 ((juce::uint32) (juce::int64) cyc * 2654435761U + seed) / 4294967295.0) * 2.0f - 1.0f;
        case 6:
        {
            const float a = (float) (hash32 ((juce::uint32) (juce::int64) cyc * 2654435761U + seed) / 4294967295.0) * 2.0f - 1.0f;
            const float b = (float) (hash32 ((juce::uint32) ((juce::int64) cyc + 1) * 2654435761U + seed) / 4294967295.0) * 2.0f - 1.0f;
            const float t = 0.5f - 0.5f * std::cos (fr * juce::MathConstants<float>::pi);
            return a + (b - a) * t;
        }
        default: return std::sin (juce::MathConstants<float>::twoPi * fr);
    }
}

// ------------------------------------------------------------------ voice
void ProVoice::prepare (double s, int)
{
    sr = s;
    for (auto* e : { &amp, &fenv, &env2, &env3 }) e->sr = s;
    fc.prepare (4.0 * s); fcSn.prepare (4.0 * s); fcDrv.prepare (4.0 * s);     // (comb buffers for the Ultra engine rate)
    os.prepare(); os2.prepare(); dlyC.reset(); dlySn.reset(); dlyG.reset(); tilt.set (s, kDriveTiltHz, kDriveTiltDb); tilt.reset();
    decim.prepare (1); decimQ = 1;
    noiseSeed = (juce::uint32) juce::Random::getSystemRandom().nextInt() | 1u;
    driveMatch.prepare (s);
    nzColAt = -1.0f;
    for (int k = 0; k < 2; ++k) { eng[k].prepare(); eng[k].osc = k; }   // ENGINES (round 3): every core's memory, once
}

static void updateEnvs (gpro::Env& amp, gpro::Env& fenv, gpro::Env& e2, gpro::Env& e3, const ProProcessor& p)
{
    const float c = p.curve->load();
    if (p.reverseEnv)
    {
        // REVERSE (TRAP stack): the sound swells in over its own decay / release time and is cut off, like a reversed hit
        const float rise = juce::jlimit (0.15f, 4.0f, 0.5f * (p.decay->load() + p.release->load()) + p.attack->load());
        amp.set (rise, 0.0f, 1.0f, 0.03f, 0.0f);
        fenv.set (juce::jlimit (0.15f, 4.0f, 0.5f * (p.fD->load() + p.fR->load()) + p.fA->load()), 0.0f, 1.0f, 0.03f, 0.0f);
    }
    else
    {
        amp.set (p.attack->load(), p.decay->load(), p.sustain->load(), p.release->load(), c);
        fenv.set (p.fA->load(), p.fD->load(), p.fS->load(), p.fR->load(), c);
    }
    e2.set (p.e2[0]->load(), p.e2[1]->load(), p.e2[2]->load(), p.e2[3]->load(), c);
    e3.set (p.e3[0]->load(), p.e3[1]->load(), p.e3[2]->load(), p.e3[3]->load(), c);
}

void ProVoice::startNote (int midi, float vel, juce::SynthesiserSound*, int wheel)
{
    note = midi; velRaw = vel; velocity = vel;
    pitchWheelMoved (wheel);
    seenSwap = proc.ctl.wavesSwap; tableFade = 0;      // a new note starts on the current tables
    auto& r = juce::Random::getSystemRandom();
    auto& c = proc.ctl;
    const int u1 = juce::jlimit (1, MAX_UNI, (int) proc.o1.uni->load()), u2 = juce::jlimit (1, MAX_UNI, (int) proc.o2.uni->load());
    for (int u = 0; u < MAX_UNI; ++u)
    {
        ph1[(size_t) u] = u1 > 1 ? r.nextDouble() : 0.0;        // single voice: same start every time (punchy basses)
        ph2[(size_t) u] = u2 > 1 ? r.nextDouble() : 0.0;
    }
    subPhase = 0; noteTime = 0; scoopTime = 0; relTime = -1; pitchRampInit = false; rampMul = rampStep = 1.0; fcLast = -1.0f;
    rnd = r.nextFloat() * 2.0f - 1.0f;
    growlPh = (double) (rnd * 0.5f + 0.5f);
    proc.modr.voiceStart (modv, rnd);                 // MOD (round 3): LFO 3 / 4 start (no extra random draws)
    for (int i = 0; i < 2; ++i)
        lfoPh[i] = proc.lfoTrig[i]->load() > 0.5f ? 0.0 : c.lfoGlobal[i];
    logTarget = std::log2 (juce::MidiMessage::getMidiNoteInHertz (midi));
    logNow = (c.glide > 0.0005f && c.haveLastNote) ? c.lastNoteLog2 : logTarget;
    c.lastNoteLog2 = logTarget; c.haveLastNote = true;
    startedAt = ++proc.voiceStamp;
    const bool wasSounding = amp.active();
    amp.reset(); fenv.reset(); env2.reset(); env3.reset();
    updateEnvs (amp, fenv, env2, env3, proc);
    amp.on(); fenv.on(); env2.on(); env3.on();
    if (! wasSounding) { fc.reset(); fcSn.reset(); fcDrv.reset(); svfForm.reset(); os.reset(); os2.reset(); decim.reset(); dlyC.reset(); dlySn.reset(); dlyG.reset(); tilt.reset(); sideLp.fill (0.0f); dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0.0f;
                         nzSvf.reset(); nzBreath.reset(); for (auto& a : nzPk) for (auto& v : a) v = 0; nzBr[0] = nzBr[1] = nzPrev[0] = nzPrev[1] = nzGeigerEnv[0] = nzGeigerEnv[1] = 0; }
    syncPend.fill (0.0f); wPend1.fill (0.0f); wPend2.fill (0.0f); last2.fill (0.0f); qPrev1.fill (-1); qPrev2.fill (-1); qLast1.fill (0.0f); qLast2.fill (0.0f); wSyncPh1.fill (0.0); wSyncPh2.fill (0.0); kWInit = true;
    for (int u = 0; u < MAX_UNI + 1; ++u) driftPh[(size_t) u] = (float) ((hash32 ((juce::uint32) (juce::int32) (rnd * 1.0e6f) + (juce::uint32) u * 104729u) >> 8) & 0xffff) / 65536.0f;   // (no extra random draws: older sounds keep their phases)
    dstPrev.fill (0.0f);
    driveMatch.reset(); drvInit = true;
    for (int k = 0; k < 2; ++k) { eng[k].start (velRaw, ! wasSounding, (juce::uint32) (hash32 ((juce::uint32) (juce::int32) (rnd * 1.0e6f) + 31u * (juce::uint32) k) | 1u)); engSrcPh[k] = 0; }   // ENGINES (round 3)
    snap();
}

void ProVoice::legatoTo (int midi, float vel, bool retrigger)
{
    note = midi;
    logTarget = std::log2 (juce::MidiMessage::getMidiNoteInHertz (midi));
    auto& c = proc.ctl;
    if (c.glide <= 0.0005f) logNow = logTarget;
    c.lastNoteLog2 = logTarget; c.haveLastNote = true;
    if (retrigger)
    {
        velRaw = vel; velocity = vel; scoopTime = 0; relTime = -1;
        updateEnvs (amp, fenv, env2, env3, proc);
        amp.on(); fenv.on(); env2.on(); env3.on();
    }
}

void ProVoice::hardReset()
{
    amp.reset(); fenv.reset(); env2.reset(); env3.reset();
    declickL = declickR = lastL = lastR = 0;
    fc.resetFilters(); fcSn.resetFilters(); fcDrv.resetFilters(); svfForm.reset();
    clearCurrentNote();
}

void ProVoice::snap()
{
    sCut = proc.cutoff->load(); sRes = proc.res->load(); sDrive = proc.fDrive->load();
    sVol = 1.0f; sPan = 0.0f;
    sPos1 = proc.o1.pos->load(); sPos2 = proc.o2.pos->load();
    kLvl1 = proc.o1.lvl->load(); kLvl2 = proc.o2.lvl->load(); kSub = proc.subLvl->load(); kNz = proc.nzLvl->load(); kNzCol = proc.nzCol->load();
    kDet1 = proc.o1.det->load(); kSpr1 = proc.o1.spr->load(); kBln1 = proc.o1.bln->load();
    kDet2 = proc.o2.det->load(); kSpr2 = proc.o2.spr->load(); kBln2 = proc.o2.bln->load(); kXamt = proc.xamt->load();
    nzStereoSm = proc.nzStereo != nullptr ? proc.nzStereo->load() : 1.0f;
}

void ProVoice::stopNote (float, bool allowTail)
{
    if (allowTail) { amp.off(); fenv.off(); env2.off(); env3.off(); }
    else
    {
        declickL += lastL; declickR += lastR;          // a hard cut (voice steal): fade the last sample out instead of jumping
        lastL = lastR = 0;
        amp.reset(); fenv.reset(); env2.reset(); env3.reset(); clearCurrentNote();
    }
}

float ProVoice::lfoAt (int i, double phase) const noexcept
{
    return ProProcessor::lfoValue ((int) proc.lfoShape[i]->load(), phase, proc.lfoTrig[i]->load() > 0.5f ? (juce::uint32) (rnd * 100000.0f) + (juce::uint32) i * 977u : (juce::uint32) i * 977u);
}

namespace
{
struct UniSetup
{
    int n = 1; std::array<double, MAX_UNI> inc {}; std::array<float, MAX_UNI> pl {}, pr {}; std::array<bool, MAX_UNI> side {}; int mip = 0; float maxRatio = 1;
    int mipShift = 0;    // FULL BANDWIDTH (sound audit 2026-10-26): 1 = read the fine (third-octave, half-rate) mips: the 2x / 4x engine
    float sideHpA = 0, sideMakeup = 1;   // UNISON V2: one-pole high-pass coefficient for the side voices' sum (keytracked: only the centre carries the sub) + its make-up at f0
    // mode: 0 Linear, 1 Super (the classic curve), 2 Exp, 3 Inv, 4 Random; stack: 0 Off, 1 "12", 2 "12+7", 3 Centre -12, 4 Centre -24
    // v2 = the round-2 law (side high-pass, drift); drift = per-voice cents (nullptr = none). With mode 1, stack 0, no drift the
    // increments are computed with the classic expression, op for op (older sounds stay bit-exact).
    void make (int voices, double f, float det, float spr, float bln, double sr, bool clean = false, int mode = 1, int stack = 0, bool v2 = false, const float* drift = nullptr)
    {
        n = voices;
        const float maxCents = 100.0f * std::pow (juce::jlimit (0.0f, 1.0f, det), 1.6f);
        if (clean)
        {   // CLEAN: low notes get less stereo spread (the bass stays solid and centred), the centre voice stays mono;
            // the DETUNE itself is left alone (it is the sound's character: a detuned bass stays detuned)
            const float hi = juce::jlimit (0.0f, 1.0f, (float) std::log2 (juce::jmax (1.0, f) / 55.0) * 0.5f);   // 55 Hz -> 0 .. 220 Hz -> 1
            spr *= 0.25f + 0.75f * hi;
        }
        float sumSq = 0; std::array<float, MAX_UNI> lv {};
        float topCents = maxCents;
        for (int u = 0; u < n; ++u)
        {
            const float s = n == 1 ? 0.0f : (float) u / (float) (n - 1) * 2.0f - 1.0f;
            float shaped;
            switch (mode)
            {
                case 0:  shaped = s; break;                                                        // LINEAR: even spacing
                case 2:  shaped = s * std::abs (s) * std::abs (s) * 0.6f + s * 0.4f; break;       // EXP: a tight core, the outer pair far out
                case 3:  shaped = s < 0.0f ? s * 1.3f : s * 0.7f; break;                            // INV: the flat side detunes more (sits lower, fatter)
                case 4:  shaped = s * 0.5f + ((float) hash32 ((juce::uint32) u * 2654435761U + 0x9e37u) / 4294967295.0f - 0.5f) * 1.0f; break;   // RANDOM: fixed per voice slot
                default: shaped = s * (0.55f + 0.45f * std::abs (s)); break;                       // SUPER: denser near the centre, like a supersaw
            }
            const bool centre = n == 1 || (n % 2 == 1 ? u == n / 2 : (u == n / 2 || u == n / 2 - 1));
            side[(size_t) u] = ! centre;
            float extra = 0;      // semitones x 100 from STACK, cents from drift
            if (stack != 0 && n > 1)
            {
                const int k = u < n / 2 ? n / 2 - 1 - u : u - (n + 1) / 2;      // 0 for the pair next to the centre, 1 for the next ...
                if (stack == 1 && ! centre) extra += 1200.0f * (float) (1 + (k % 3));                 // 12: the pairs climb 1, 2, 3 octaves
                else if (stack == 2 && ! centre) extra += (k % 2 == 0) ? 1200.0f : 1900.0f;          // 12+7: octaves and fifths
                else if (stack == 3 && centre) extra -= 1200.0f;                                     // CENTRE -12: the centre an octave down
                else if (stack == 4 && centre) extra -= 2400.0f;                                     // CENTRE -24: two octaves down
            }
            if (drift != nullptr && n > 1) extra += drift[u];       // (a single voice never wanders: every note starts the same)
            if (extra == 0.0f) inc[(size_t) u] = f * std::exp2 (shaped * maxCents / 1200.0f) / sr;
            else inc[(size_t) u] = f * std::exp2 ((shaped * maxCents + extra) / 1200.0f) / sr;
            topCents = std::max (topCents, shaped * maxCents + extra);
            lv[(size_t) u] = centre ? 1.0f : juce::jlimit (0.0f, 1.0f, bln);
            sumSq += lv[(size_t) u] * lv[(size_t) u];
            const float pan = (clean && centre) ? 0.0f : juce::jlimit (-1.0f, 1.0f, s * spr);
            pl[(size_t) u] = std::sqrt (0.5f * (1.0f - pan)); pr[(size_t) u] = std::sqrt (0.5f * (1.0f + pan));
        }
        const float norm = 1.0f / std::sqrt (juce::jmax (1.0e-3f, sumSq));
        for (int u = 0; u < n; ++u) { pl[(size_t) u] *= lv[(size_t) u] * norm * 1.41421f; pr[(size_t) u] *= lv[(size_t) u] * norm * 1.41421f; }
        maxRatio = std::exp2 (topCents / 1200.0f);
        mip = mipShift > 0 ? MipFrame::fineFor (f * maxRatio * 1.02) : MipFrame::mipFor (f * maxRatio * 1.02);
        // V2: the side voices lose what sits BELOW their fundamental on low notes (a one-pole high-pass at 0.6 x the pitch, up to 120 Hz,
        // with the fundamental's own level restored), so eight detuned voices add no sub / rumble under the centre voice's bass
        sideHpA = 0.0f; sideMakeup = 1.0f;
        if (v2 && n > 1)
        {
            const double fc = std::min (120.0, 0.6 * f);
            sideHpA = (float) std::exp (-juce::MathConstants<double>::twoPi * fc / sr);
            const double r = f / fc; sideMakeup = (float) (std::sqrt (1.0 + r * r) / r);     // 1 / |H (f0)| of the one-pole high pass
        }
    }
};

inline float polyBlep (double t, double dt) noexcept
{
    if (t < dt) { const double x = t / dt; return (float) (x + x - x * x - 1.0); }
    if (t > 1.0 - dt) { const double x = (t - 1.0) / dt; return (float) (x * x + x + x + 1.0); }
    return 0.0f;
}
} // namespace

void ProVoice::renderNextBlock (juce::AudioBuffer<float>& out, int start, int num)
{
    auto* L = out.getWritePointer (0, start);
    auto* R = out.getNumChannels() > 1 ? out.getWritePointer (1, start) : nullptr;

    if (! isVoiceActive())
    {
        // still fading a stolen note's last sample
        if (std::abs (declickL) + std::abs (declickR) > 1.0e-6f)
        {
            const float dc = (float) std::exp (-1.0 / (0.003 * sr));
            for (int i = 0; i < num; ++i) { L[i] += declickL; if (R) R[i] += declickR; declickL *= dc; declickR *= dc; }
            if (std::abs (declickL) + std::abs (declickR) < 1.0e-6f) declickL = declickR = 0;
        }
        return;
    }
    const auto& c = proc.ctl;
    if (c.waves == nullptr || c.waves->t1 == nullptr || c.waves->t2 == nullptr) return;
    const Table& T1 = *c.waves->t1;
    const Table& T2 = *c.waves->t2;
    // a new table set arrived (a redrawn GHOST A/B wave, a table switch): fade from the previous one over ~8 ms instead of jumping
    if (c.wavesSwap != seenSwap)
    {
        seenSwap = c.wavesSwap;
        const bool changed = c.wavesPrev != nullptr && c.wavesPrev->t1 != nullptr && c.wavesPrev->t2 != nullptr
                             && (c.wavesPrev->t1 != c.waves->t1 || c.wavesPrev->t2 != c.waves->t2);
        tableFade = changed ? tableFadeLen : 0;
    }
    const Table* O1 = tableFade > 0 && c.wavesPrev->t1 != c.waves->t1 ? c.wavesPrev->t1.get() : nullptr;
    const Table* O2 = tableFade > 0 && c.wavesPrev->t2 != c.waves->t2 ? c.wavesPrev->t2.get() : nullptr;

    updateEnvs (amp, fenv, env2, env3, proc);

    // ---- block constants
    const bool on1 = proc.o1.on->load() > 0.5f, on2 = proc.o2.on->load() > 0.5f;
    const int xm = juce::jlimit (0, 3, (int) proc.xmod->load());
    const bool need1 = on1 || (on2 && xm != 0);
    const int n1 = juce::jlimit (1, MAX_UNI, (int) proc.o1.uni->load()), n2 = juce::jlimit (1, MAX_UNI, (int) proc.o2.uni->load());
    const float tune1 = (float) (proc.o1.oct->load() * 12.0f + proc.o1.semi->load() + proc.o1.fine->load() * 0.01f);
    const float tune2 = (float) (proc.o2.oct->load() * 12.0f + proc.o2.semi->load() + proc.o2.fine->load() * 0.01f);
    const int ft = juce::jlimit (0, 9, (int) proc.fType->load());
    if (ft != filterType) { filterType = ft; fc.resetFilters(); fcSn.resetFilters(); fcDrv.resetFilters(); }
    // FILTER DRIVE round 2: INSIDE = gain into the filter's saturating copy (4x oversampled), SUB + NOISE on their own clean path;
    // CLASSIC = the old tanh on the summed voice. CLEAN MODE = -24 dB in / +24 dB out (the filter curve without the saturation)
    const bool inside = (int) proc.fDLaw->load() == dlInside;
    const bool uniV2 = (int) proc.uniLaw->load() == 1;        // UNISON V2 (round 2): side-voice low cut + drift; Classic = the old sum
    // QUALITY: the engine runs Q x the sample rate (Draft 1, High 2, Ultra 4): oscillators, sub, noise, filter and the drive run at
    // rs = Q * sr and a per-voice decimator brings the result down. At Q = 1 every expression below is the old one (rs == sr).
    const int Q = juce::jlimit (1, 4, c.engineQ);
    const double rs = sr * (double) Q;
    if (Q != decimQ) { decim.prepare (Q); decimQ = Q; dlyC.reset(); dlySn.reset(); dlyG.reset(); }
    // FULL BANDWIDTH (sound audit 2026-10-26): the octave mips are cut at 0.45 x the host rate for the top note of their octave, so a note
    // at the bottom of an octave kept only about half the audio band (a saw on E4 stopped at 9.6 kHz, -5 dB of top octave). When the
    // engine runs at 2x / 4x (QUALITY High / Ultra: every V2 sound) it reads the fine third-octave mips (ProTables.h), cut at half the host
    // rate: the whole audio band on every note, nothing that could fold back. Keyed on the QUALITY parameter itself (a Draft / CLASSIC
    // sound lifted to 4x by "Ultra when rendering" keeps its old mips and sound).
    const bool v2q = Q >= 2 && (int) proc.qualityP->load() >= 1;       // the sound-audit fixes below ride on this (QUALITY High / Ultra)
    const int mipShift = v2q ? 1 : 0;
    fc.ladderOpen = fcSn.ladderOpen = fcDrv.ladderOpen = v2q;
    // DC BLOCK (sound audit 2026-10-26): the ladder's input saturation on a saw with resonance leaves a DC offset (-25 dB re the RMS on
    // C4 at RES 30 %, -22 dB at RES 50 %) that the envelope turns into a sub-audio thump per note; a 5 Hz one-pole high pass after the
    // decimator takes it out (v2q only; -0.1 dB at 30 Hz)
    const float dcR = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * 5.0 / sr);
    const int drvDelay = Q == 1 ? Oversampler4::DELAY : Q == 2 ? Oversampler2::DELAY : 0;                       // (engine samples)
    const float nzQGain = Q == 1 ? 1.0f : Q == 2 ? 1.41421356f : 2.0f;                                           // white noise per engine sample: the in-band density keeps its level
    const int gainDelay = Q == 1 ? (inside ? Oversampler4::DELAY : 0) : Q == 2 ? (inside ? 37 : 25) : 12;        // (base samples)
    tilt.set (rs, kDriveTiltHz, kDriveTiltDb);
    const float cleanModeIn = proc.fClean->load() > 0.5f ? ghosty::clean::dbToGain (-kCleanModeDb) : 1.0f;
    fcDrv.S = kDriveSat;
    const float subOct = proc.subOct->load() > 0.5f ? 0.25f : 0.5f;
    const int subShape = (int) proc.subShp->load();
    // WARP (round 3): two slots per oscillator; a slot is skipped entirely while its mode is Off or its (glided) amount is 0
    const int wm[4] { (int) proc.o1.w1->load(), (int) proc.o1.w2->load(), (int) proc.o2.w1->load(), (int) proc.o2.w2->load() };
    const bool anyWarp = wm[0] != wOff || wm[1] != wOff || wm[2] != wOff || wm[3] != wOff;
    float tW[4] {};
    auto warpSrcOf = [] (int mode, float other, float sub, float noise) noexcept { const int k = warpSource (mode); return k == 0 ? other : k == 1 ? sub : k == 2 ? noise : 0.0f; };
    // the phase part of a slot: FM scales the increment (through zero), PD shifts the phase, the shape warps remap it, Sync is handled by the caller
    auto warpPh = [] (int m, float a, float src, double p, double& incMul, int& qStep) noexcept -> double
    {
        if (m == wOff || a <= 0.0f || m == wSync) return p;
        if (warpIsFm (m)) { incMul *= 1.0 + 2.0 * a * src; return p; }
        if (m >= wPdOsc && m <= wPdNoise) { p += a * src; return p - std::floor (p); }
        if (warpIsPhase (m)) return warpPhase (m, p, a, &qStep);
        return p;
    };
    const float nzColT = proc.nzCol->load();
    // NOISE round 2: real colours. 0 = CLASSIC (the old white -> one-pole low pass, bit-exact), then White, Pink, Brown, Blue, Violet,
    // Geiger, Breath. The COLOUR knob becomes a FILTER for them (below 0.5 a low pass closing, above 0.5 a high pass opening; 0.5 .. 0.6 flat),
    // STEREO blends the two independent streams from mono to fully decorrelated. Levels are matched per colour (equal loudness).
    // The colour-dependent parts follow the glided colour (kNzCol, 10 ms per 16 samples) like the classic filter does.
    const int nzType = (int) proc.nzType->load();
    const float nzStereo = juce::jlimit (0.0f, 1.0f, proc.nzStereo->load());
    float nzFiltG = 0, nzFiltMix = 0, nzGain = 1, nzBrownA = 0, nzGeigerP = 0, nzGeigerD = 0; bool nzHp = false; float nzPink[6] {}, nzPinkW[6] {}, nzPinkNorm = 1;
    auto setNoiseColour = [&] (float col)
    {
        const float nzMaxHz = (float) juce::jmin (20000.0, sr * 0.46);
        if (col < 0.5f) { nzHp = false; const float fcut = 200.0f * std::pow (100.0f, col / 0.5f); nzFiltG = (float) std::tan (juce::MathConstants<double>::pi * juce::jmin (nzMaxHz, fcut) / rs); nzFiltMix = fcut < 18000.0f ? 1.0f : 0.0f; }
        else { nzHp = true; const float fcut = 20.0f * std::pow (1000.0f, (col - 0.5f) / 0.5f); nzFiltG = (float) std::tan (juce::MathConstants<double>::pi * juce::jmin (nzMaxHz, fcut) / rs); nzFiltMix = fcut > 25.0f ? 1.0f : 0.0f; }
        nzSvf.set (nzFiltG, 1.0f / 0.7071f);
        nzGeigerP = (5.0f + 495.0f * col) / (float) rs;                                             // geiger: clicks per sample (5 .. 500 / s via COLOUR)
    };
    if (nzType > 0)
    {
        setNoiseColour (kNzCol);
        // pink: six one-pole low passes two octaves apart (14 Hz .. 14.4 kHz), each weighted by the square root of its corner, so the
        // sum slopes -3 dB / oct from the bottom to the top of the band (weights fc^-0.40: measured -3 dB / oct over 100 Hz .. 10 kHz)
        for (int k = 0; k < 6; ++k) { const double fk = 14.0 * std::pow (4.0, k); nzPink[k] = 1.0f - (float) std::exp (-juce::MathConstants<double>::twoPi * fk / rs); nzPinkW[k] = (float) std::pow (fk / 14.0, -0.40); }
        nzPinkNorm = 1.0f;
        nzBrownA = 1.0f - (float) std::exp (-juce::MathConstants<double>::twoPi * 12.0 / rs);        // brown: a leaky integrator (-6 dB/oct above 12 Hz)
        nzGeigerD = (float) std::exp (-1.0 / (0.0012 * rs));                                        // ... each a 1.2 ms burst
        // per-colour level matching (equal loudness, measured: ProTests noise); an env override for re-measuring
        static const float typeGain[8] { 1.0f, 1.0f, 6.5f, 24.0f, 1.65f, 1.38f, 1.0f, 0.73f };
        nzGain = typeGain[juce::jlimit (0, 7, nzType)];
        if (const char* e = std::getenv ("R2_NZGAIN")) nzGain = (float) std::atof (e);
    }
    const float keyTrack = proc.fKey->load(), velFilt = proc.velFilt->load(), fAmt = proc.fenvAmt->load();
    const float velGain = 1.0f - proc.velAmp->load() * (1.0f - velRaw);
    const float bendSemis = wheelPos * c.bendSemis;
    const bool lanePitch = c.lanePitch && c.laneSemi != nullptr && c.laneLen > 0;
    auto laneIdx = [&c, start] (int i) { return juce::jlimit (0, juce::jmax (0, c.laneLen - 1), start + i); };
    double laneInv = 1.0;     // 1 / lane ratio at the start of the control block (per sample: ratio now * laneInv)
    double lfoInc[2]; float lfoFade[2];
    for (int i = 0; i < 2; ++i) { lfoInc[i] = c.lfoInc[i]; lfoFade[i] = proc.lfoFade[i]->load(); }

    constexpr int CTRL = 16;
    const float aCtl = 1.0f - (float) std::exp (-CTRL / (0.010 * sr));
    const float aSmp = 1.0f - (float) std::exp (-1.0 / (0.010 * sr));
    const double gCoef = c.glide > 0.0005f ? 1.0 - std::exp (-CTRL / (c.glide * 0.35 * sr)) : 1.0;
    const float maxHz = (float) juce::jmin (20000.0, sr * 0.46);
    const float declickCoef = (float) std::exp (-1.0 / (0.003 * sr));
    // TRAP brass: pitch scoop on the attack, growl wobble, formant-ish peak
    const float scoopAmt = proc.brScoop->load(), scoopT = juce::jmax (0.005f, proc.brScoopT->load()), formAmt = proc.brForm->load();
    const float growlBase = proc.brGrowl->load(), fallAmt = proc.brFall->load();
    float formK = 1.0f / 3.0f, growl = 0;
    if (formAmt > 0.0f) svfForm.set ((float) std::tan (juce::MathConstants<double>::pi * juce::jmin (maxHz, 400.0f * std::exp2 (proc.brFormF->load() * 3.3f)) / sr), formK);

    // ENGINES (round 3): an oscillator whose table menu picks an engine plays it instead of the wavetable (ProEngines.h). OSC 2 is kept
    // running (at level 0 if so set) while it feeds OSC 1's TEXTURE / RESONATOR, and OSC 1 always runs when it is on.
    const int engId[2] { gpro::eng::engineOf ((int) proc.o1.tbl->load()), gpro::eng::engineOf ((int) proc.o2.tbl->load()) };
    const int engSrc[2] { (int) proc.eng1.src->load(), (int) proc.eng2.src->load() };
    const bool feed2 = engId[0] >= 0 && gpro::eng::usesInput (engId[0]) && engSrc[0] == 0 && on2;
    double engInc[2] {}; int engMip[2] {};
    auto engControl = [&] (int k, double f, const std::array<float, numDst>& d)
    {
        gpro::eng::Settings es; const auto& E = k == 0 ? proc.eng1 : proc.eng2; const int b = k == 0 ? (int) dE1h : (int) dE2h;
        es.engine = (k == 0 ? (need1 || on1) : on2) ? engId[k] : -1;
        es.hz = f; es.accent = velRaw;
        es.h = juce::jlimit (0.0f, 1.0f, E.h->load() + d[(size_t) b]); es.t = juce::jlimit (0.0f, 1.0f, E.t->load() + d[(size_t) b + 1]); es.m = juce::jlimit (0.0f, 1.0f, E.m->load() + d[(size_t) b + 2]);
        es.size = E.size->load(); es.pitch = E.pitch->load(); es.feedback = E.fb->load(); es.freeze = E.frz->load() > 0.5f;
        es.position = E.rpos->load(); es.model = (int) E.rmodel->load();
        es.haveInput = engSrc[k] > 0 || (k == 0 ? on2 : on1);
        eng[k].control (es, rs);
        engInc[k] = f / rs; engMip[k] = mipShift > 0 ? MipFrame::fineFor (f) : MipFrame::mipFor (f);
    };
    auto engSource = [&] (int k, const Table& T, float pos, double lr) noexcept -> float
    {   // what a TEXTURE / RESONATOR hears: the other oscillator (its first unison voice, before its level), or the SOURCE wavetable at the note
        if (engSrc[k] <= 0) return k == 0 ? (on2 ? last2[0] : 0.0f) : (on1 ? last1[0] : 0.0f);
        const auto cu = T.cursor (pos); double& ph = engSrcPh[k];
        const float a = cu.a->read4 (engMip[k], ph), b = cu.b->read4 (engMip[k], ph);
        ph += engInc[k] * lr; if (ph >= 1.0) ph -= 1.0;
        return a + (b - a) * cu.fr;
    };

    UniSetup U1, U2;
    float fltFrom = 0, fltTo = 0, fltRes = 0, fltForm = 0; int fltN = CTRL;
    auto setFilters = [&] (float t) noexcept
    {
        const float hz = t >= 1.0f || fltFrom == fltTo ? fltTo : fltFrom * std::pow (fltTo / fltFrom, t);
        fc.set (filterType, hz, fltRes, fltForm, rs, maxHz);
        if (inside) { fcSn.set (filterType, hz, fltRes, fltForm, rs, maxHz); fcDrv.set (filterType, hz, fltRes, fltForm, filterType <= 5 ? 4.0 * sr : rs, maxHz); }   // (the saturating copy always runs at 4x)
    };
    float lvl1 = 0, lvl2 = 0, subLvl = 0, nzLvl = 0, xamt = 0, drv = 0, driveK = 1, driveNorm = 1, volStep = 0, panStep = 0;
    float pos1T = sPos1, pos2T = sPos2; double f0 = 440, syncRatio = 1;
    float oscLaw = 1.0f, dinStep = 0, trimStep = 0, fadeStep = 0;
    // the knob part of each level glides per sample; the mod-matrix part (dl*) is held per 16 samples as before
    float tLvl1 = kLvl1, tLvl2 = kLvl2, tSub = kSub, tNz = kNz, dl1 = 0, dl2 = 0, dlSub = 0, dlNz = 0;
    auto glide = [aSmp] (float& k, float t) noexcept { if (k != t) { k += (t - k) * aSmp; if (std::abs (t - k) < 1.0e-5f) k = t; } };
    auto glideCtl = [aCtl] (float& k, float t) noexcept { if (k != t) { k += (t - k) * aCtl; if (std::abs (t - k) < 1.0e-5f) k = t; } };

    for (int i = 0; i < num; ++i)
    {
        if (i % CTRL == 0)
        {
            const int n = juce::jmin (CTRL, num - i);
            // ---- mod sources
            const float fe = fenv.next (n), e2v = env2.next (n), e3v = env3.next (n);
            float lv[2];
            for (int k = 0; k < 2; ++k)
            {
                const float rateMod = dstPrev[(size_t) (k == 0 ? dLfo1Rate : dLfo2Rate)];
                const float fade = lfoFade[k] > 0.001f ? juce::jmin (1.0f, (float) noteTime / lfoFade[k]) : 1.0f;
                lv[k] = lfoAt (k, lfoPh[k]) * fade * proc.modr.lfoDepth (k);     // (MOD round 3: DEPTH, 1 = as before)
                lfoPh[k] += lfoInc[k] * std::exp2 (juce::jlimit (-1.0f, 1.0f, rateMod) * 3.0f) * n;
                if (lfoPh[k] > 1.0e6) lfoPh[k] -= 1.0e6;
            }
            srcVals[srcLfo1] = lv[0]; srcVals[srcLfo2] = lv[1]; srcVals[srcEnv2] = e2v; srcVals[srcEnv3] = e3v; srcVals[srcFEnv] = fe;
            srcVals[srcAmpEnv] = amp.out; srcVals[srcVel] = velRaw; srcVals[srcWheel] = c.wheel; srcVals[srcAT] = at;
            srcVals[srcRandom] = rnd; srcVals[srcNote] = juce::jlimit (-1.0f, 1.0f, (note - 60) / 36.0f);
            for (int k = 0; k < 4; ++k) srcVals[(size_t) (srcMac1 + k)] = proc.macro[k]->load();
            srcVals[srcBend] = c.laneMod && c.laneNorm != nullptr && c.laneLen > 0 ? c.laneNorm[laneIdx (i)] : 0.0f;
            std::array<float, numDst> d {};
            if (c.anyMod)
                for (int s = 0; s < NUM_SLOTS; ++s)
                    if (c.slotSrc[s] > 0 && c.slotDst[s] > 0) d[(size_t) c.slotDst[s]] += srcVals[(size_t) c.slotSrc[s]] * c.slotAmt[s];
            // MOD (round 3): this voice's sources for the router, and its per-voice connections into the same targets
            proc.modr.voiceStep (modv, { amp.out, e2v, e3v, fe, lv[0], lv[1], velRaw, juce::jlimit (-1.0f, 1.0f, (note - 60) / 60.0f), at, c.wheel, wheelPos, rnd }, start + i, n, d.data());
            dstPrev = d;
            noteTime += n / sr;

            // ---- pitch
            logNow += (logTarget - logNow) * gCoef;
            double common = bendSemis + d[dPitch] * 24.0f + d[dFine];
            if (c.extraSemis != 0.0f) common += c.extraSemis;
            if (scoopAmt > 0.0f) { common -= scoopAmt * 3.0f * std::exp (-(float) scoopTime / scoopT); scoopTime += n / sr; }
            if (fallAmt > 0.0f)          // FALL: once the key is let go the pitch drops away over the release (the brass "doit")
            {
                if (amp.st == Env::St::release) { if (relTime < 0) relTime = 0; relTime += n / sr; }
                if (relTime >= 0) common -= fallAmt * 12.0f * (1.0f - std::exp (-(float) relTime / juce::jmax (0.03f, 0.5f * proc.release->load())));
            }
            const float growlAmt = juce::jlimit (0.0f, 1.0f, growlBase + d[dGrowl]);
            if (growlAmt > 0.0f)
            {
                growlPh += (30.0 + 4.0 * rnd) / sr * n; if (growlPh > 1.0e6) growlPh -= 1.0e6;
                growl = std::sin ((float) growlPh * juce::MathConstants<float>::twoPi) * growlAmt * (0.5f + 0.5f * velRaw);
                common += growl * 0.15f;
            }
            else growl = 0;
            if (lanePitch) { const int li = laneIdx (i); common += c.laneSemi[li]; laneInv = 1.0 / (double) c.laneRatio[li]; }
            f0 = std::exp2 (logNow + common / 12.0);
            if (v2q)
            {   // PITCH RAMP (sound audit 2026-10-26): the pitch moves once per 16 samples; a vibrato / bend / glide then stepped the phase
                // increments at fs / 16 (sidebands 3 kHz +- every harmonic: -65 dB on a C4 saw with 20 cents of vibrato). The step is spread
                // over the 16 samples geometrically, through the per-sample ratio every increment already takes (lr)
                const double lf = logNow + common / 12.0;
                if (! pitchRampInit) { pitchRampInit = true; lfPrev = lf; }
                const double dl = lf - lfPrev;
                rampMul = std::exp2 (-dl * (double) (n - 1) / (double) n); rampStep = std::exp2 (dl / (double) n);
                lfPrev = lf;
            }
            if (nzType == 7) nzBreath.set ((float) std::tan (juce::MathConstants<double>::pi * juce::jmin ((double) maxHz, f0 * 4.0) / rs), 1.0f / 6.0f);
            const double f1 = f0 * std::exp2 ((tune1 + d[dPitch1] * 24.0f) / 12.0);
            const double f2 = f0 * std::exp2 ((tune2 + d[dPitch2] * 24.0f) / 12.0);
            const float spreadMod = d[dSpread];
            glideCtl (kDet1, proc.o1.det->load()); glideCtl (kSpr1, proc.o1.spr->load()); glideCtl (kBln1, proc.o1.bln->load());
            glideCtl (kDet2, proc.o2.det->load()); glideCtl (kSpr2, proc.o2.spr->load()); glideCtl (kBln2, proc.o2.bln->load());
            glideCtl (kXamt, proc.xamt->load());
            if (uniV2)
            {   // UNISON V2: every unison voice wanders a little (0.08 .. 0.35 Hz, +-1.5 cents), so eight voices never lock into one beat
                for (int u = 0; u < MAX_UNI; ++u)
                {
                    const float rate = 0.08f + 0.27f * (float) (hash32 ((juce::uint32) (juce::int32) (rnd * 1.0e6f) + (juce::uint32) u * 7919u) & 0xffff) / 65535.0f;
                    driftPh[(size_t) u] += rate * (float) n / (float) sr; if (driftPh[(size_t) u] > 1.0f) driftPh[(size_t) u] -= 1.0f;
                    driftCents[(size_t) u] = 1.5f * std::sin (driftPh[(size_t) u] * juce::MathConstants<float>::twoPi);
                }
            }
            U1.mipShift = mipShift; U2.mipShift = (xm == 1) ? 0 : mipShift;      // (X-MOD FM keeps its own widened mip law)
            if (need1) U1.make (n1, f1, kDet1 + d[dDet1], juce::jlimit (0.0f, 1.0f, kSpr1 + spreadMod), kBln1, rs, c.cleanUni || uniV2,
                                (int) proc.o1.dmode->load(), (int) proc.o1.stack->load(), uniV2, uniV2 ? driftCents.data() : nullptr);
            xamt = juce::jlimit (0.0f, 1.0f, kXamt + d[dXmod]);
            syncRatio = xm == 2 ? 1.0 + xamt * 6.0 : 1.0;
            if (on2) U2.make (n2, f2 * syncRatio, kDet2 + d[dDet2], juce::jlimit (0.0f, 1.0f, kSpr2 + spreadMod), kBln2, rs, c.cleanUni || uniV2,
                              (int) proc.o2.dmode->load(), (int) proc.o2.stack->load(), uniV2, uniV2 ? driftCents.data() + 1 : nullptr);
            if (on2 && xm == 1) U2.mip = juce::jmin (TBL_MIPS - 1, U2.mip + (int) (xamt * 3.0f));
            if (engId[0] >= 0 || eng[0].active()) engControl (0, f1, d);      // ENGINES (round 3)
            if (engId[1] >= 0 || eng[1].active()) engControl (1, f2, d);
            if (anyWarp)
            {   // the amounts (knob + mod) and the mips for the steepest read the warps can make
                tW[0] = juce::jlimit (0.0f, 1.0f, proc.o1.w1a->load() + d[dW1a]); tW[1] = juce::jlimit (0.0f, 1.0f, proc.o1.w2a->load() + d[dW1b]);
                tW[2] = juce::jlimit (0.0f, 1.0f, proc.o2.w1a->load() + d[dW2a]); tW[3] = juce::jlimit (0.0f, 1.0f, proc.o2.w2a->load() + d[dW2b]);
                if (kWInit) { kW = { tW[0], tW[1], tW[2], tW[3] }; kWInit = false; }
                const float sl1 = warpSlope (wm[0], kW[0]) * warpSlope (wm[1], kW[1]), sl2 = warpSlope (wm[2], kW[2]) * warpSlope (wm[3], kW[3]);
                if (need1 && sl1 > 1.0f) U1.mip = juce::jmin (TBL_MIPS - 1, MipFrame::mipFor (f1 * U1.maxRatio * 1.02 * sl1));
                if (on2 && sl2 > 1.0f) U2.mip = juce::jmin (TBL_MIPS - 1, MipFrame::mipFor (f2 * syncRatio * U2.maxRatio * 1.02 * sl2));
            }
            else kWInit = true;
            tLvl1 = proc.o1.lvl->load(); tLvl2 = proc.o2.lvl->load(); tSub = proc.subLvl->load(); tNz = proc.nzLvl->load();
            dl1 = d[dLvl1]; dl2 = d[dLvl2]; dlSub = d[dSub]; dlNz = d[dNoise];
            lvl1 = on1 ? juce::jlimit (0.0f, 1.0f, kLvl1 + dl1) : 0.0f;
            lvl2 = on2 ? juce::jlimit (0.0f, 1.0f, kLvl2 + dl2) : 0.0f;
            pos1T = juce::jlimit (0.0f, 1.0f, proc.o1.pos->load() + d[dPos1]);
            pos2T = juce::jlimit (0.0f, 1.0f, proc.o2.pos->load() + d[dPos2]);
            subLvl = juce::jlimit (0.0f, 1.0f, kSub + dlSub);
            nzLvl = juce::jlimit (0.0f, 1.0f, kNz + dlNz);
            glideCtl (kNzCol, nzColT);
            if (kNzCol != nzColAt)
            {
                nzColAt = kNzCol;
                nzAc = 1.0f - (float) std::exp (-juce::MathConstants<double>::twoPi * (200.0 * std::pow (90.0, (double) kNzCol)) / rs);
                nzComp = 1.0f + (1.0f - kNzCol) * 2.0f;
                if (nzType > 0) setNoiseColour (kNzCol);        // the round-2 colours' FILTER / GEIGER rate follow the glide too
            }

            // ---- filter
            sCut += (proc.cutoff->load() - sCut) * aCtl;
            sRes += (proc.res->load() - sRes) * aCtl;
            sDrive += (proc.fDrive->load() - sDrive) * aCtl;
            const float res = juce::jlimit (0.0f, 1.0f, sRes + d[dRes]);
            drv = juce::jlimit (0.0f, 1.0f, sDrive + d[dFDrive]);
            driveK = 1.0f + 7.0f * drv; driveNorm = 1.0f / std::tanh (driveK);
            float octs = (sCut + d[dCutoff]) * 9.3f + fAmt * fe * 7.0f + velFilt * velRaw * 4.0f + keyTrack * (float) (note - 60) / 12.0f;
            if (growl != 0.0f) octs += growl * 1.2f;
            const float fcHz = juce::jlimit (20.0f, maxHz, 30.0f * std::exp2 (octs));
            const float formantPos = sCut + d[dCutoff] + fAmt * fe * 0.5f;        // (the FORMANT type reads the knob, not the Hz)
            // CUTOFF GLIDE (sound audit 2026-10-26, v2q, the SVF / ladder types): the cut-off moved once per 16 samples, so a fast filter
            // envelope (a pluck) stepped the coefficients at fs / 16; it now walks from the last block's value to this one in 4-sample
            // steps (log-frequency), the first step here and three more inside the block
            fltRes = res; fltForm = formantPos; fltN = n;
            if (v2q && filterType <= 5) { fltFrom = fcLast > 0.0f ? fcLast : fcHz; fltTo = fcHz; fcLast = fcHz; setFilters (std::min (1.0f, 4.0f / (float) n)); }
            else { fcLast = fcHz; fltFrom = fltTo = fcHz; setFilters (1.0f); }
            if (inside)
            {
                // dB gain into the filter, less of it as the resonance rises (the peak must not run away). The output trim follows
                // the circuit model: a unit-peak input comes out of the saturator at satS (gain), the trim turns that into
                // gain^kDriveLoudExp, so the loudness climbs gently and steadily (about +4 dB peak, +7 dB RMS at the top) while the
                // density does the real work. CLEAN MODE lands at the same loudness with the saturation of 24 dB less drive.
                const float D = std::exp2 (drv * kDriveDb / 6.0206f);
                const float dEff = D / (1.0f + 1.5f * res * res);
                const float dinT = dEff * cleanModeIn, trimT = std::pow (dEff, kDriveLoudExp) / satS (dinT, kDriveSat), fadeT = juce::jmin (1.0f, drv / kDriveFade);
                if (drvInit) { dinNow = dinT; trimNow = trimT; fadeNow = fadeT; drvInit = false; }
                dinStep = (dinT - dinNow) / (float) n; trimStep = (trimT - trimNow) / (float) n; fadeStep = (fadeT - fadeNow) / (float) n;
            }

            // CLEAN: the oscillators sum at a steady loudness (equal power over OSC 1 / OSC 2 / SUB / NOISE levels)
            if (c.clean) oscLaw = 1.0f / std::sqrt (juce::jmax (1.0f, lvl1 * lvl1 + lvl2 * lvl2 + 0.5f * subLvl * subLvl + 0.5f * nzLvl * nzLvl));
            const float volT = juce::jmax (0.0f, 1.0f + d[dVolume]);
            const float panT = juce::jlimit (-1.0f, 1.0f, d[dPan]);
            volStep = (volT - sVol) / (float) n; panStep = (panT - sPan) / (float) n;
        }

        // ---- oscillators
        if (kLvl1 != tLvl1 || kLvl2 != tLvl2 || kSub != tSub || kNz != tNz)
        {   // a level knob is moving: glide it per sample (with CLEAN, the equal-power law follows every sample too)
            glide (kLvl1, tLvl1); glide (kLvl2, tLvl2); glide (kSub, tSub); glide (kNz, tNz);
            lvl1 = on1 ? juce::jlimit (0.0f, 1.0f, kLvl1 + dl1) : 0.0f;
            lvl2 = on2 ? juce::jlimit (0.0f, 1.0f, kLvl2 + dl2) : 0.0f;
            subLvl = juce::jlimit (0.0f, 1.0f, kSub + dlSub);
            nzLvl = juce::jlimit (0.0f, 1.0f, kNz + dlNz);
            if (c.clean) oscLaw = 1.0f / std::sqrt (juce::jmax (1.0f, lvl1 * lvl1 + lvl2 * lvl2 + 0.5f * subLvl * subLvl + 0.5f * nzLvl * nzLvl));
        }
        sPos1 += (pos1T - sPos1) * aSmp; sPos2 += (pos2T - sPos2) * aSmp;
        double lr = lanePitch ? (double) c.laneRatio[laneIdx (i)] * laneInv : 1.0;   // bend lane, per sample (no zipper)
        if (v2q) { lr *= rampMul; rampMul *= rampStep; }
        if (fltFrom != fltTo && (i % CTRL) % 4 == 0 && (i % CTRL) > 0) setFilters ((float) ((i % CTRL) + 4) / (float) fltN);
        float yl = 0, yr = 0;
        if (inside) { dinNow += dinStep; trimNow += trimStep; fadeNow += fadeStep; }
        for (int q = 0; q < Q; ++q)
        {   // ---- one engine sample (Q per output sample)
        float l = 0, r = 0, s1l = 0, s1r = 0, s2l = 0, s2r = 0;
        bool wrapped[MAX_UNI] {};
        if (anyWarp) for (int k = 0; k < 4; ++k) if (kW[(size_t) k] != tW[k]) { kW[(size_t) k] += (tW[k] - kW[(size_t) k]) * aSmp; if (std::abs (tW[k] - kW[(size_t) k]) < 1.0e-5f) kW[(size_t) k] = tW[k]; }
        // ---- sub + noise (with the INSIDE drive law they never see the drive: their own clean sum)
        float sv = 0, nl = 0, nr = 0;
        if (subLvl > 0.0f)
        {
            const double dt = f0 * subOct / rs * lr;
            if (subShape == 1) sv = 1.0f - 4.0f * std::abs ((float) subPhase - 0.5f);
            else if (subShape == 2) { double t2 = subPhase + 0.5; if (t2 >= 1.0) t2 -= 1.0; sv = (subPhase < 0.5 ? 1.0f : -1.0f) + polyBlep (subPhase, dt) - polyBlep (t2, dt); sv *= 0.7f; }
            else sv = std::sin ((float) subPhase * juce::MathConstants<float>::twoPi);
            subPhase += dt; if (subPhase >= 1.0) subPhase -= 1.0;
        }
        if (nzLvl > 0.0f && nzType == 0)
        {   // CLASSIC noise (the old code, op for op; at a higher engine rate the stream is scaled by sqrt (Q) so the in-band level stays)
            noiseSeed ^= noiseSeed << 13; noiseSeed ^= noiseSeed >> 17; noiseSeed ^= noiseSeed << 5;
            float w1 = (float) (noiseSeed & 0xffff) / 32768.0f - 1.0f, w2 = (float) (noiseSeed >> 16) / 32768.0f - 1.0f;
            if (Q > 1) { w1 *= nzQGain; w2 *= nzQGain; }
            nzLp[0] += (w1 - nzLp[0]) * nzAc; nzLp[1] += (w2 - nzLp[1]) * nzAc;
            nl = nzLp[0] * nzLvl * 0.6f * nzComp; nr = nzLp[1] * nzLvl * 0.6f * nzComp;
        }
        else if (nzLvl > 0.0f)
        {
            // two independent white streams (xorshift), then the colour
            noiseSeed ^= noiseSeed << 13; noiseSeed ^= noiseSeed >> 17; noiseSeed ^= noiseSeed << 5;
            float w[2] { ((float) (noiseSeed & 0xffff) / 32768.0f - 1.0f) * nzQGain, ((float) (noiseSeed >> 16) / 32768.0f - 1.0f) * nzQGain };
            float c[2];
            for (int ch = 0; ch < 2; ++ch)
            {
                const float x = w[ch];
                switch (nzType)
                {
                    case 2:     // PINK: -3 dB / oct
                    {
                        float acc = 0;
                        for (int k = 0; k < 6; ++k) { nzPk[ch][k] += (x - nzPk[ch][k]) * nzPink[k]; acc += nzPk[ch][k] * nzPinkW[k]; }
                        c[ch] = acc * nzPinkNorm;
                        break;
                    }
                    case 3: nzBr[ch] += (x - nzBr[ch]) * nzBrownA; c[ch] = nzBr[ch] * 9.0f; break;                   // BROWN: -6 dB / oct
                    case 4:     // BLUE: +3 dB / oct = the first difference of pink
                    {
                        float acc = 0;
                        for (int k = 0; k < 6; ++k) { nzPk[ch][k] += (x - nzPk[ch][k]) * nzPink[k]; acc += nzPk[ch][k] * nzPinkW[k]; }
                        acc *= nzPinkNorm; c[ch] = (acc - nzPrev[ch]) * (4.0f * (float) Q); nzPrev[ch] = acc;      // (a first difference loses 1 / Q per engine rate)
                        break;
                    }
                    case 5: c[ch] = (x - nzPrev[ch]) * (0.5f * (float) Q); nzPrev[ch] = x; break;                    // VIOLET: +6 dB / oct
                    case 6:     // GEIGER: random clicks, each a short decaying burst of the white stream
                    {
                        if (std::abs (x) * (0.5f / nzQGain) < nzGeigerP) nzGeigerEnv[ch] = 1.0f;   // (|x| / nzQGain is uniform in 0..1: a click with probability p)
                        c[ch] = x * nzGeigerEnv[ch] * 2.2f; nzGeigerEnv[ch] *= nzGeigerD;
                        break;
                    }
                    case 7:     // BREATH: the white stream through a resonant band pass that tracks the note (two octaves up, Q 6)
                    {
                        float bp, hp; nzBreath.tick (x, ch, bp, hp); c[ch] = bp * 3.5f;
                        break;
                    }
                    default: c[ch] = x; break;                                                                      // WHITE
                }
            }
            // STEREO: 1 = the two streams as they are, 0 = both channels the left one (power kept); 10 ms glide per sample
            nzStereoSm += (nzStereo - nzStereoSm) * aSmp;
            if (nzStereoSm < 0.999f)
            {
                const float a = nzStereoSm, b = 1.0f - nzStereoSm, nrm = 1.0f / std::sqrt (a * a + b * b);
                c[1] = (a * c[1] + b * c[0]) * nrm;
            }
            // FILTER (the COLOUR knob): low pass below the middle, high pass above
            if (nzFiltMix > 0.0f)
                for (int ch = 0; ch < 2; ++ch) { float bp, hp; const float lp = nzSvf.tick (c[ch], ch, bp, hp); c[ch] = nzHp ? hp : lp; }
            nl = c[0] * nzLvl * 0.6f * nzGain; nr = c[1] * nzLvl * 0.6f * nzGain;
        }

        const float nzMono = 0.5f * (nl + nr);
        if (need1 && eng[0].active())
        {   // ENGINES (round 3): OSC 1 plays its engine (mono, centre; UNISON, WARP and X-MOD act on wavetables only)
            const float v = eng[0].tick (gpro::eng::usesInput (engId[0]) ? engSource (0, T1, sPos1, lr) : 0.0f, lr);
            last1.fill (v);
            if (lvl1 > 0.0f) { l += v * lvl1; r += v * lvl1; }
        }
        else if (need1)
        {
            const auto cu = T1.cursor (sPos1);
            const float xf = O1 != nullptr ? 1.0f - (float) juce::jmax (0, tableFade - i) / (float) tableFadeLen : 1.0f;
            const auto co = O1 != nullptr && xf < 1.0f ? O1->cursor (sPos1) : cu;
            for (int u = 0; u < U1.n; ++u)
            {
                auto& ph = ph1[(size_t) u];
                double rp = ph, incMul = 1.0; int qs = -1; float sA = 0, sB = 0;
                const bool wa = wm[0] != wOff && kW[0] > 0.0f, wb = wm[1] != wOff && kW[1] > 0.0f;
                if (wa || wb)
                {   // WARP: sources (the other oscillator one sample late, the sub, the noise), then slot 1, then slot 2 on the read phase
                    const float other = last2[(size_t) (U2.n > 0 ? u % U2.n : 0)];
                    sA = warpSrcOf (wm[0], other, sv, nzMono); sB = warpSrcOf (wm[1], other, sv, nzMono);
                    if (wa) rp = wm[0] == wSync ? wSyncPh1[(size_t) u] : warpPh (wm[0], kW[0], sA, rp, incMul, qs);
                    if (wb) rp = wm[1] == wSync ? wSyncPh1[(size_t) u] : warpPh (wm[1], kW[1], sB, rp, incMul, qs);
                }
                const float a = cu.a->read4 (U1.mip, rp), b = cu.b->read4 (U1.mip, rp);
                float v = a + (b - a) * cu.fr;
                if (xf < 1.0f) { const float oa = co.a->read4 (U1.mip, rp), ob = co.b->read4 (U1.mip, rp); const float ov = oa + (ob - oa) * co.fr; v = ov + (v - ov) * xf; }
                if (wa || wb)
                {
                    v += wPend1[(size_t) u]; wPend1[(size_t) u] = 0;
                    if (qs >= 0)
                    {   // Quantize: the read is delayed by one sample so a step (at a known fraction of the sample) gets both halves of a
                        // 2-point polynomial BLEP: + h (1-f)^2 / 2 on the sample before it (the one leaving the delay now), - h f^2 / 2 on this one
                        const int qa = (wb && wm[1] == wQuantize) ? 1 : 0;
                        const int n = (int) std::lround (4.0 + (1.0 - kW[(size_t) qa]) * 60.0);
                        float before = qLast1[(size_t) u];
                        if (qPrev1[(size_t) u] >= 0 && qs != qPrev1[(size_t) u])
                        {
                            const double pOld = (double) qPrev1[(size_t) u] / n, pNew = (double) qs / n;
                            const float h = (cu.a->read4 (U1.mip, pNew) + (cu.b->read4 (U1.mip, pNew) - cu.a->read4 (U1.mip, pNew)) * cu.fr)
                                          - (cu.a->read4 (U1.mip, pOld) + (cu.b->read4 (U1.mip, pOld) - cu.a->read4 (U1.mip, pOld)) * cu.fr);
                            const double since = (ph - pNew) / (U1.inc[(size_t) u] * lr);                             // samples since the step (0 .. 1)
                            const float f = (float) juce::jlimit (0.0, 1.0, (double) since);
                            before += h * f * f * 0.5f;                                                               // the sample before the step: + h (1 - d)^2 / 2 with d = 1 - since
                            v -= h * (1.0f - f) * (1.0f - f) * 0.5f;                                                  // the sample after it: - h d^2 / 2
                        }
                        qLast1[(size_t) u] = v; v = qPrev1[(size_t) u] >= 0 ? before : v;
                        qPrev1[(size_t) u] = qs;
                    }
                    else qPrev1[(size_t) u] = -1;
                    if (wa && warpIsOut (wm[0])) v = warpOut (wm[0], v, kW[0], sA);
                    if (wb && warpIsOut (wm[1])) v = warpOut (wm[1], v, kW[1], sB);
                }
                last1[(size_t) u] = v;
                if (lvl1 > 0.0f)
                {
                    if (U1.sideHpA > 0.0f && U1.side[(size_t) u]) { s1l += v * U1.pl[(size_t) u] * lvl1; s1r += v * U1.pr[(size_t) u] * lvl1; }
                    else { l += v * U1.pl[(size_t) u] * lvl1; r += v * U1.pr[(size_t) u] * lvl1; }
                }
                ph += U1.inc[(size_t) u] * lr * incMul;
                // floor, not -1: at a low sample rate (or FM x3, octave up, unison) one sample can step more than a whole cycle
                if (ph >= 1.0) { ph -= std::floor (ph); wrapped[u] = true; } else if (ph < 0.0) ph -= std::floor (ph);   // (FM through zero runs the phase backwards)
                if (wa || wb)
                {   // the Sync warp's own phase: runs faster, restarts at the base cycle (step spread by the BLEP's after-sample)
                    const int si = (wa && wm[0] == wSync) ? 0 : (wb && wm[1] == wSync) ? 1 : -1;
                    if (si >= 0)
                    {
                        auto& sp = wSyncPh1[(size_t) u];
                        const double sinc = U1.inc[(size_t) u] * lr * (1.0 + 5.0 * kW[(size_t) si]);
                        sp += sinc;
                        if (wrapped[u])
                        {
                            const double np = (ph / (U1.inc[(size_t) u] * lr)) * sinc, cont = sp - std::floor (sp), npw = np - std::floor (np);
                            const float f = (float) juce::jlimit (0.0, 1.0, 1.0 - ph / (U1.inc[(size_t) u] * lr));
                            const float h = (cu.a->read4 (U1.mip, npw) + (cu.b->read4 (U1.mip, npw) - cu.a->read4 (U1.mip, npw)) * cu.fr)
                                          - (cu.a->read4 (U1.mip, cont) + (cu.b->read4 (U1.mip, cont) - cu.a->read4 (U1.mip, cont)) * cu.fr);
                            wPend1[(size_t) u] = -h * f * f * 0.5f;
                            sp = npw;
                        }
                        else if (sp >= 1.0) sp -= std::floor (sp);
                    }
                }
            }
            if (U1.sideHpA > 0.0f)
            {   // V2: the side voices' sum minus its low end (one-pole), the centre keeps the whole bass
                sideLp[0] += (1.0f - U1.sideHpA) * (s1l - sideLp[0]); sideLp[1] += (1.0f - U1.sideHpA) * (s1r - sideLp[1]);
                l += (s1l - sideLp[0]) * U1.sideMakeup; r += (s1r - sideLp[1]) * U1.sideMakeup;
            }
        }
        if (on2 && eng[1].active())
        {   // ENGINES (round 3): OSC 2 plays its engine
            const float v = eng[1].tick (gpro::eng::usesInput (engId[1]) ? engSource (1, T2, sPos2, lr) : 0.0f, lr);
            last2.fill (v);
            if (lvl2 > 0.0f) { l += v * lvl2; r += v * lvl2; }
        }
        else if (lvl2 > 0.0f || (on2 && xm != 0) || feed2)
        {
            const auto cu = T2.cursor (sPos2);
            const float xf2 = O2 != nullptr ? 1.0f - (float) juce::jmax (0, tableFade - i) / (float) tableFadeLen : 1.0f;
            const auto co2 = O2 != nullptr && xf2 < 1.0f ? O2->cursor (sPos2) : cu;
            for (int u = 0; u < U2.n; ++u)
            {
                auto& ph = ph2[(size_t) u];
                const int m = u % U1.n;
                double rp = ph, incMul = 1.0; int qs = -1; float sA = 0, sB = 0;
                if (xm == 1) { rp += xamt * 1.2 * last1[(size_t) m]; rp -= std::floor (rp); }
                const bool wa = wm[2] != wOff && kW[2] > 0.0f, wb = wm[3] != wOff && kW[3] > 0.0f;
                if (wa || wb)
                {
                    sA = warpSrcOf (wm[2], last1[(size_t) m], sv, nzMono); sB = warpSrcOf (wm[3], last1[(size_t) m], sv, nzMono);
                    if (wa) rp = wm[2] == wSync ? wSyncPh2[(size_t) u] : warpPh (wm[2], kW[2], sA, rp, incMul, qs);
                    if (wb) rp = wm[3] == wSync ? wSyncPh2[(size_t) u] : warpPh (wm[3], kW[3], sB, rp, incMul, qs);
                }
                float a = cu.a->read4 (U2.mip, rp), b = cu.b->read4 (U2.mip, rp);
                float v = a + (b - a) * cu.fr;
                if (xf2 < 1.0f) { const float oa = co2.a->read4 (U2.mip, rp), ob = co2.b->read4 (U2.mip, rp); const float ov = oa + (ob - oa) * co2.fr; v = ov + (v - ov) * xf2; }
                if (xm == 2) { v += syncPend[(size_t) u]; syncPend[(size_t) u] = 0; }      // (the second half of the last reset's BLEP)
                if (wa || wb)
                {
                    v += wPend2[(size_t) u]; wPend2[(size_t) u] = 0;
                    if (qs >= 0)
                    {   // (as OSC 1: one-sample delayed read, both halves of the step's BLEP)
                        const int qa = (wb && wm[3] == wQuantize) ? 3 : 2;
                        const int n = (int) std::lround (4.0 + (1.0 - kW[(size_t) qa]) * 60.0);
                        float before = qLast2[(size_t) u];
                        if (qPrev2[(size_t) u] >= 0 && qs != qPrev2[(size_t) u])
                        {
                            const double pOld = (double) qPrev2[(size_t) u] / n, pNew = (double) qs / n;
                            const float h = (cu.a->read4 (U2.mip, pNew) + (cu.b->read4 (U2.mip, pNew) - cu.a->read4 (U2.mip, pNew)) * cu.fr)
                                          - (cu.a->read4 (U2.mip, pOld) + (cu.b->read4 (U2.mip, pOld) - cu.a->read4 (U2.mip, pOld)) * cu.fr);
                            const double since = (ph - pNew) / (U2.inc[(size_t) u] * lr);
                            const float f = (float) juce::jlimit (0.0, 1.0, (double) since);
                            before += h * f * f * 0.5f;
                            v -= h * (1.0f - f) * (1.0f - f) * 0.5f;
                        }
                        qLast2[(size_t) u] = v; v = qPrev2[(size_t) u] >= 0 ? before : v;
                        qPrev2[(size_t) u] = qs;
                    }
                    else qPrev2[(size_t) u] = -1;
                    if (wa && warpIsOut (wm[2])) v = warpOut (wm[2], v, kW[2], sA);
                    if (wb && warpIsOut (wm[3])) v = warpOut (wm[3], v, kW[3], sB);
                }
                last2[(size_t) u] = v;
                ph += U2.inc[(size_t) u] * lr * incMul;
                if (xm == 2 && wrapped[m])
                {   // hard SYNC: the slave restarts where the master wrapped. The restart is a step between this sample and the next; a
                    // 2-point polynomial BLEP (band-limited step) spreads it: + h (1-f)^2 / 2 here, - h f^2 / 2 on the next sample, f = how
                    // far into this sample the master wrapped, h = the step (round 3, 2026-10-24)
                    const double master = ph1[(size_t) m];
                    const double newPh = master / U1.inc[(size_t) m] * U2.inc[(size_t) u];
                    const double contPh = ph >= 1.0 ? ph - 1.0 : ph;
                    const float f = (float) juce::jlimit (0.0, 1.0, 1.0 - master / (U1.inc[(size_t) m] * lr));
                    const double np = newPh - std::floor (newPh);
                    const float ha = cu.a->read4 (U2.mip, np), hb = cu.b->read4 (U2.mip, np), ca = cu.a->read4 (U2.mip, contPh), cb = cu.b->read4 (U2.mip, contPh);
                    const float h = (ha + (hb - ha) * cu.fr) - (ca + (cb - ca) * cu.fr);
                    v += h * (1.0f - f) * (1.0f - f) * 0.5f;
                    syncPend[(size_t) u] = -h * f * f * 0.5f;
                    ph = np;
                }
                else if (ph >= 1.0) ph -= std::floor (ph);   // (floor: more than a cycle per sample at low sample rates, see OSC 1)
                else if (ph < 0.0) ph -= std::floor (ph);
                if (wa || wb)
                {   // the Sync warp's own phase (OSC 2)
                    const int si = (wa && wm[2] == wSync) ? 2 : (wb && wm[3] == wSync) ? 3 : -1;
                    if (si >= 0)
                    {
                        auto& sp = wSyncPh2[(size_t) u];
                        const double sinc = U2.inc[(size_t) u] * lr * (1.0 + 5.0 * kW[(size_t) si]);
                        sp += sinc;
                        const bool wrap2 = ph < U2.inc[(size_t) u] * lr * incMul && incMul > 0.0;             // the base phase just wrapped
                        if (wrap2)
                        {
                            const double np = (ph / (U2.inc[(size_t) u] * lr)) * sinc, cont = sp - std::floor (sp), npw = np - std::floor (np);
                            const float f = (float) juce::jlimit (0.0, 1.0, 1.0 - ph / (U2.inc[(size_t) u] * lr));
                            const float h = (cu.a->read4 (U2.mip, npw) + (cu.b->read4 (U2.mip, npw) - cu.a->read4 (U2.mip, npw)) * cu.fr)
                                          - (cu.a->read4 (U2.mip, cont) + (cu.b->read4 (U2.mip, cont) - cu.a->read4 (U2.mip, cont)) * cu.fr);
                            wPend2[(size_t) u] = -h * f * f * 0.5f;
                            sp = npw;
                        }
                        else if (sp >= 1.0) sp -= std::floor (sp);
                    }
                }
                if (xm == 3) v *= 1.0f - xamt + xamt * last1[(size_t) m];
                if (U2.sideHpA > 0.0f && U2.side[(size_t) u]) { s2l += v * U2.pl[(size_t) u] * lvl2; s2r += v * U2.pr[(size_t) u] * lvl2; }
                else { l += v * U2.pl[(size_t) u] * lvl2; r += v * U2.pr[(size_t) u] * lvl2; }
            }
            if (U2.sideHpA > 0.0f)
            {
                sideLp[2] += (1.0f - U2.sideHpA) * (s2l - sideLp[2]); sideLp[3] += (1.0f - U2.sideHpA) * (s2r - sideLp[3]);
                l += (s2l - sideLp[2]) * U2.sideMakeup; r += (s2r - sideLp[3]) * U2.sideMakeup;
            }
        }
        if (! inside)
        {
            // CLASSIC: sub + noise into the voice sum, tanh drive on everything, then the filter (the old code, op for op)
            if (subLvl > 0.0f) { l += sv * subLvl * 0.9f; r += sv * subLvl * 0.9f; }
            if (nzLvl > 0.0f) { l += nl; r += nr; }
            if (drv > 1.0e-4f)
            {
                const float pin = l * l + r * r;
                l += drv * (std::tanh (driveK * l) * driveNorm - l);
                r += drv * (std::tanh (driveK * r) * driveNorm - r);
                if (c.clean && ! (proc.cleanOff & 4)) { const float mg = driveMatch.step (pin, l * l + r * r, 0.06f, 1.4f); l *= mg; r *= mg; }   // CLEAN: same loudness, more grit
            }
            fc.tick (l, r, yl, yr);
        }
        else
        {
            const float snl = sv * subLvl * 0.9f + nl, snr = sv * subLvl * 0.9f + nr;
            float cl, cr; fc.tick (l + snl, r + snr, cl, cr);                     // the clean filter on everything: the reference at drive 0 ...
            dlyC.push (cl, cr, cl, cr, drvDelay);                                 // ... aligned with the oversampled path
            if (fadeNow > 1.0e-4f)
            {
                float sl, sr2; fcSn.tick (snl, snr, sl, sr2); dlySn.push (sl, sr2, sl, sr2, drvDelay);
                float u[2][4], o[2][4], dl, dr;
                const float xl0 = tilt.pre (l, 0) * dinNow, xr0 = tilt.pre (r, 1) * dinNow;          // TILT in: less bass into the saturator
                if (Q == 4)
                {   // Ultra: the engine already runs at 4x, the saturating copy runs straight at the engine rate
                    if (filterType <= 5) fcDrv.tickSat (xl0, xr0, dl, dr);
                    else fcDrv.tick (satS (xl0, kDriveSat), satS (xr0, kDriveSat), dl, dr);
                }
                else if (Q == 2)
                {   // High: 2x more inside, to the same 4x
                    os2.up (0, xl0, u[0]); os2.up (1, xr0, u[1]);
                    if (filterType <= 5) { for (int p = 0; p < 2; ++p) fcDrv.tickSat (u[0][p], u[1][p], o[0][p], o[1][p]); dl = os2.down (0, o[0]); dr = os2.down (1, o[1]); }
                    else { for (int p = 0; p < 2; ++p) { o[0][p] = satS (u[0][p], kDriveSat); o[1][p] = satS (u[1][p], kDriveSat); } const float xl = os2.down (0, o[0]), xr = os2.down (1, o[1]); fcDrv.tick (xl, xr, dl, dr); }
                    os2.advance();
                }
                else
                {
                os.up (0, xl0, u[0]); os.up (1, xr0, u[1]);
                if (filterType <= 5)
                {   // SVF family + ladder: the saturation sits inside the filter, at 4x
                    for (int p = 0; p < 4; ++p) fcDrv.tickSat (u[0][p], u[1][p], o[0][p], o[1][p]);
                    dl = os.down (0, o[0]); dr = os.down (1, o[1]);
                }
                else
                {   // comb / formant / off: saturate at 4x in front of the (linear) filter
                    for (int p = 0; p < 4; ++p) { o[0][p] = satS (u[0][p], kDriveSat); o[1][p] = satS (u[1][p], kDriveSat); }
                    const float xl = os.down (0, o[0]), xr = os.down (1, o[1]);
                    fcDrv.tick (xl, xr, dl, dr);
                }
                os.advance();
                }
                dl = tilt.post (dl, 0); dr = tilt.post (dr, 1);                       // TILT out: the bass comes back, the added harmonics sit above it
                dl = dl * trimNow + sl; dr = dr * trimNow + sr2;
                yl = cl + fadeNow * (dl - cl); yr = cr + fadeNow * (dr - cr);
            }
            else { yl = cl; yr = cr; }
        }
        if (Q > 1) decim.push (yl, yr);
        }   // engine sample
        if (Q > 1) decim.out (yl, yr);
        if (v2q)
        {
            const float hl = yl - dcX[0] + dcR * dcY[0], hr = yr - dcX[1] + dcR * dcY[1];
            dcX[0] = yl; dcX[1] = yr; dcY[0] = hl; dcY[1] = hr; yl = hl; yr = hr;
        }

        if (formAmt > 0.0f)
        {
            float bp, hp;
            svfForm.tick (yl, 0, bp, hp); yl += formAmt * 3.0f * bp * formK;
            svfForm.tick (yr, 1, bp, hp); yr += formAmt * 3.0f * bp * formK;
        }
        sVol += volStep; sPan += panStep;
        float env = amp.next() * 0.4f * velGain * sVol * (1.0f + growl * 0.2f);   // growl: a little tremolo rides on the pitch / filter flutter
        if (c.clean && ! (proc.cleanOff & 2)) env *= oscLaw;
        float gl = env, gr = env;
        if (sPan != 0.0f) { gl *= std::sqrt (juce::jmax (0.0f, 1.0f - sPan)); gr *= std::sqrt (juce::jmax (0.0f, 1.0f + sPan)); }
        if (gainDelay > 0) dlyG.push (gl, gr, gl, gr, gainDelay);          // the voice path is late by its oversampling, so is its gain (the envelope stays on the sound)
        float ol = yl * gl, orr = yr * gr;
        if (! std::isfinite (ol) || ! std::isfinite (orr)) { ol = orr = 0; fc.resetFilters(); fcSn.resetFilters(); fcDrv.resetFilters(); svfForm.reset(); os.reset(); }
        lastL = ol; lastR = orr;
        if (declickL != 0.0f || declickR != 0.0f) { ol += declickL; orr += declickR; declickL *= declickCoef; declickR *= declickCoef; if (std::abs (declickL) + std::abs (declickR) < 1.0e-6f) declickL = declickR = 0; }
        L[i] += ol; if (R) R[i] += orr;
    }
    tableFade = juce::jmax (0, tableFade - num);
    if (! amp.active()) { lastL = lastR = 0; clearCurrentNote(); }
}

// ------------------------------------------------------------------ voice allocation, mono / legato
juce::SynthesiserVoice* ProSynth::findFreeVoice (juce::SynthesiserSound* s, int, int noteNum, bool steal) const
{
    if (sameNoteLimit)   // LIMIT SAME NOTE POLY TO 1: a retrigger takes over the voice already on that note instead of stacking a second one
        for (auto* v : voices) if (v->isVoiceActive() && v->getCurrentlyPlayingNote() == noteNum && v->canPlaySound (s)) return v;
    int active = 0;
    for (auto* v : voices) if (v->isVoiceActive()) ++active;
    if (active < polyLimit)
        for (auto* v : voices) if (! v->isVoiceActive() && v->canPlaySound (s)) return v;
    if (! steal) return nullptr;
    juce::SynthesiserVoice* best = nullptr; int bestScore = -1; juce::uint32 bestAge = 0xffffffffu;
    if (stealPriority == gpro::stOldest)
    {   // the default: the allocation before VOICING, unchanged
        for (auto* v : voices)
        {
            if (! v->isVoiceActive()) continue;
            auto* pv = static_cast<ProVoice*> (v);
            const int score = v->getCurrentlyPlayingNote() == noteNum ? 3 : (! v->isKeyDown() && ! v->isSustainPedalDown()) ? 2 : 1;
            if (score > bestScore || (score == bestScore && pv->startedAt < bestAge)) { best = v; bestScore = score; bestAge = pv->startedAt; }
        }
        return best;
    }
    // STEAL PRIORITY: the same note first, then a released voice, then a held one (as before); inside that class the priority picks
    // (Newest = the latest note-on, Highest / Lowest = the note number, Quietest = the lowest amp envelope level); ties go to the oldest
    float bestKey = 0;
    for (auto* v : voices)
    {
        if (! v->isVoiceActive()) continue;
        auto* pv = static_cast<ProVoice*> (v);
        const int score = v->getCurrentlyPlayingNote() == noteNum ? 3 : (! v->isKeyDown() && ! v->isSustainPedalDown()) ? 2 : 1;
        const float key = stealPriority == gpro::stNewest ? (float) pv->startedAt
                        : stealPriority == gpro::stHighest ? (float) v->getCurrentlyPlayingNote()
                        : stealPriority == gpro::stLowest ? -(float) v->getCurrentlyPlayingNote()
                        : -pv->ampLevel();                                   // stQuietest
        if (score > bestScore || (score == bestScore && (key > bestKey || (key == bestKey && pv->startedAt < bestAge)))) { best = v; bestScore = score; bestKey = key; bestAge = pv->startedAt; }
    }
    return best;
}

void ProSynth::setMono (bool m, bool l)
{
    if (m != mono) allNotesOff (0, true);
    mono = m; legato = l;
}

void ProSynth::removeFromStack (int n)
{
    int w = 0;
    for (int i = 0; i < stackN; ++i) if (stack[(size_t) i] != n) stack[(size_t) w++] = stack[(size_t) i];
    stackN = w;
}

void ProSynth::noteOn (int ch, int n, float vel)
{
    if (! mono || voices.isEmpty() || sounds.isEmpty()) { juce::Synthesiser::noteOn (ch, n, vel); return; }
    removeFromStack (n);
    if (stackN < (int) stack.size()) stack[(size_t) stackN++] = n;
    auto* v = static_cast<ProVoice*> (voices.getUnchecked (0));
    if (v->isVoiceActive()) v->legatoTo (n, vel, ! (legato && monoNote >= 0));
    else startVoice (v, sounds.getUnchecked (0), ch, n, vel);
    monoNote = n;
}

void ProSynth::noteOff (int ch, int n, float vel, bool allowTail)
{
    if (! mono || voices.isEmpty()) { juce::Synthesiser::noteOff (ch, n, vel, allowTail); return; }
    removeFromStack (n);
    if (n != monoNote) return;
    auto* v = static_cast<ProVoice*> (voices.getUnchecked (0));
    if (stackN > 0) { monoNote = stack[(size_t) stackN - 1]; v->legatoTo (monoNote, vel, false); }
    else { monoNote = -1; if (v->isVoiceActive()) stopVoice (v, vel, allowTail); }
}

void ProSynth::allNotesOff (int ch, bool allowTail)
{
    stackN = 0; monoNote = -1;
    juce::Synthesiser::allNotesOff (ch, allowTail);
}

void ProSynth::handleController (int ch, int cc, int value)
{
    if (cc == 1) proc.ctl.wheel = (float) value / 127.0f;
    juce::Synthesiser::handleController (ch, cc, value);
}

// ------------------------------------------------------------------ parameters
juce::AudioProcessorValueTreeState::ParameterLayout ProProcessor::layout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    // typed values (right-click a knob) are read back in the same units the knob shows: "40%", "-40%", "250 ms", "1.2 s"
    auto pctText = [] (const String& t) { return t.retainCharacters ("0123456789.-+").getFloatValue() / 100.0f; };
    auto pct = [pctText] (const String& id, const String& nm, float def)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, nm, NormalisableRange<float> (0.0f, 1.0f), def,
                   AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100)) + "%"; })
                                                  .withValueFromStringFunction (pctText));
    };
    auto bip = [pctText] (const String& id, const String& nm, float def)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, nm, NormalisableRange<float> (-1.0f, 1.0f), def,
                   AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0.0049f ? "+" : "") + String (roundToInt (v * 100)) + "%"; })
                                                  .withValueFromStringFunction (pctText));
    };
    auto secs = [] (const String& id, const String& nm, float lo, float hi, float def)
    {
        NormalisableRange<float> r (lo, hi); r.setSkewForCentre (lo + (hi - lo) * 0.1f);
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, nm, r, def,
                   AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 1 ? String (roundToInt (v * 1000)) + " ms" : String (v, 2) + " s"; })
                                                  .withValueFromStringFunction ([hi] (const String& t)
                                                  {
                                                      const float n = t.retainCharacters ("0123456789.-").getFloatValue();
                                                      const auto low = t.toLowerCase();
                                                      if (low.contains ("ms")) return n / 1000.0f;
                                                      if (low.contains ("s")) return n;
                                                      return n > hi ? n / 1000.0f : n;          // a bare number: seconds, unless it only makes sense as ms
                                                  }));
    };
    auto choice = [] (const String& id, const String& nm, const StringArray& items, int def)
    { return std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, nm, items, def,
                 AudioParameterChoiceAttributes().withValueFromStringFunction ([items] (const String& t) { return ghosty::ptext::choiceIndex (items, t); })); };
    auto boolean = [] (const String& id, const String& nm, bool def) { return std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, nm, def); };
    auto integer = [] (const String& id, const String& nm, int lo, int hi, int def) { return std::make_unique<AudioParameterInt> (ParameterID { id, 1 }, nm, lo, hi, def); };

    for (int o = 1; o <= 2; ++o)
    {
        const String p = "o" + String (o) + "_", N = "Osc " + String (o) + " ";
        l.add (boolean (p + "on", N + "On", o == 1));
        l.add (choice (p + "tbl", N + "Table", gpro::eng::tableChoiceNames(), o == 1 ? (int) tSaw : (int) tClassic));   // ENGINES (round 3): the engines follow the wavetables
        l.add (pct (p + "pos", N + "Position", 0.0f));
        l.add (pct (p + "lvl", N + "Level", 0.8f));
        l.add (integer (p + "oct", N + "Octave", -3, 3, 0));
        l.add (integer (p + "semi", N + "Semitone", -12, 12, 0));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { p + "fine", 1 }, N + "Fine", NormalisableRange<float> (-100.0f, 100.0f), 0.0f,
                   AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " ct"; })));
        l.add (integer (p + "uni", N + "Unison", 1, MAX_UNI, 1));
        l.add (pct (p + "det", N + "Detune", 0.25f));
        l.add (pct (p + "spr", N + "Spread", 0.6f));
        l.add (pct (p + "bln", N + "Blend", 0.75f));
    }
    l.add (choice ("waveA", "Wave A", ghosty::shapeNames(), 2));
    l.add (choice ("waveB", "Wave B", ghosty::shapeNames(), 3));
    l.add (pct ("sub_lvl", "Sub Level", 0.0f));
    l.add (choice ("sub_oct", "Sub Octave", subOctNames(), 0));
    l.add (choice ("sub_shp", "Sub Shape", subShapeNames(), 0));
    l.add (pct ("nz_lvl", "Noise Level", 0.0f));
    l.add (pct ("nz_col", "Noise Colour", 0.6f));
    l.add (choice ("xmod", "Osc Mod", xmodNames(), 0));
    l.add (pct ("xamt", "Osc Mod Amount", 0.0f));

    l.add (choice ("f_type", "Filter Type", filterTypeNames(), 0));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "cutoff", 1 }, "Cutoff", NormalisableRange<float> (0.0f, 1.0f), 1.0f,
               AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { const float f = cutoffHz (v); return roundToInt (f) >= 1000 ? String (f / 1000.0f, 1) + " kHz" : String (roundToInt (f)) + " Hz"; })
                                              .withValueFromStringFunction ([] (const String& t)
                                              {
                                                  float f = t.retainCharacters ("0123456789.").getFloatValue();
                                                  if (t.toLowerCase().contains ("k") || f < 20.0f) f *= 1000.0f;       // "1.6 kHz", "1.6"
                                                  return jlimit (0.0f, 1.0f, std::log2 (jmax (30.0f, f) / 30.0f) / 9.3f);
                                              })));
    l.add (pct ("res", "Resonance", 0.0f));
    l.add (pct ("fenv", "Filter Env Amount", 0.0f));
    l.add (pct ("f_drive", "Filter Drive", 0.0f));
    l.add (pct ("f_key", "Key Tracking", 0.0f));
    l.add (pct ("vel_f", "Velocity to Filter", 0.0f));
    l.add (secs ("f_a", "Filter Attack", 0.001f, 8.0f, 0.005f));
    l.add (secs ("f_d", "Filter Decay", 0.005f, 8.0f, 0.4f));
    l.add (pct ("f_s", "Filter Sustain", 0.0f));
    l.add (secs ("f_r", "Filter Release", 0.005f, 10.0f, 0.4f));

    l.add (secs ("attack", "Attack", 0.001f, 8.0f, 0.003f));
    l.add (secs ("decay", "Decay", 0.005f, 8.0f, 0.4f));
    l.add (pct ("sustain", "Sustain", 1.0f));
    l.add (secs ("release", "Release", 0.005f, 10.0f, 0.2f));
    l.add (pct ("curve", "Envelope Curve", 0.5f));
    l.add (pct ("vel_amp", "Velocity to Volume", 0.5f));
    for (int e = 2; e <= 3; ++e)
    {
        const String p = "e" + String (e) + "_", N = "Env " + String (e) + " ";
        l.add (secs (p + "a", N + "Attack", 0.001f, 8.0f, 0.005f));
        l.add (secs (p + "d", N + "Decay", 0.005f, 8.0f, 0.6f));
        l.add (pct (p + "s", N + "Sustain", 0.0f));
        l.add (secs (p + "r", N + "Release", 0.005f, 10.0f, 0.4f));
    }
    for (int k = 1; k <= 2; ++k)
    {
        const String p = "l" + String (k) + "_", N = "LFO " + String (k) + " ";
        NormalisableRange<float> r (0.02f, 30.0f); r.setSkewForCentre (2.0f);
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { p + "rate", 1 }, N + "Rate", r, k == 1 ? 2.0f : 0.5f,
                   AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, v < 1 ? 2 : 1) + " Hz"; })));
        l.add (boolean (p + "sync", N + "Sync", false));
        l.add (choice (p + "div", N + "Division", lfoDivNames(), 4));
        l.add (choice (p + "shape", N + "Shape", lfoShapeNames(), 0));
        l.add (boolean (p + "trig", N + "Retrigger", false));
        l.add (secs (p + "fade", N + "Fade In", 0.0f, 4.0f, 0.0f));
    }
    for (int s = 1; s <= NUM_SLOTS; ++s)
    {
        const String p = "m" + String (s) + "_", N = "Mod " + String (s) + " ";
        l.add (choice (p + "src", N + "Source", modSourceNames(), 0));
        l.add (choice (p + "dst", N + "Target", modDestNames(), 0));
        l.add (bip (p + "amt", N + "Amount", 0.0f));
    }
    for (int m = 1; m <= 4; ++m) l.add (pct ("mac" + String (m), "Macro " + String (m), 0.0f));

    l.add (secs ("glide", "Glide", 0.0f, 2.0f, 0.0f));
    l.add (boolean ("mono", "Mono", false));
    l.add (boolean ("legato", "Legato", false));
    l.add (integer ("poly", "Voices", 1, MAX_VOICES, MAX_VOICES));
    l.add (integer ("bend", "Bend Range", 1, 24, 2));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "gain", 1 }, "Volume", NormalisableRange<float> (0.0f, 1.0f), 0.5f,
               AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { const float g = outGain (v); return g < 1.0e-4f ? String ("-inf") : String (Decibels::gainToDecibels (g), 1) + " dB"; })
                                              .withValueFromStringFunction ([] (const String& t)
                                              {
                                                  if (t.containsIgnoreCase ("inf")) return 0.0f;
                                                  return jlimit (0.0f, 1.0f, std::sqrt (Decibels::decibelsToGain (t.retainCharacters ("0123456789.-").getFloatValue()) / 4.0f));
                                              })));

    l.add (pct ("drive", "Drive", 0.0f));
    l.add (pct ("crush", "Crush", 0.0f));
    l.add (pct ("chorus", "Chorus", 0.0f));
    l.add (pct ("phaser", "Phaser", 0.0f));
    l.add (pct ("echo", "Echo", 0.0f));
    l.add (pct ("verb", "Reverb", 0.0f));
    l.add (pct ("haunt", "Haunt", 0.0f));

    l.add (choice ("key", "Key", ghosty::keyNames(), 9));
    l.add (choice ("scale", "Scale", ghosty::scaleNames(), 0));
    l.add (choice ("chord", "Chords", ghosty::chordNames(), 0));
    l.add (boolean ("arp", "Arp", false));
    l.add (choice ("arpRate", "Arp Rate", ghosty::arpRateNames(), 2));
    l.add (choice ("arpMode", "Arp Mode", ghosty::arpModeNames(), 0));
    l.add (integer ("arpOct", "Arp Octaves", 1, 3, 1));
    l.add (pct ("arpGate", "Arp Gate", 0.6f));

    gpfx::Rack::addParameters (l);
    // BEND LANE (after everything else; left out of the saved state while they sit at their defaults)
    l.add (boolean ("bl_on", "Bend Lane On", false));
    l.add (pct ("bl_amt", "Bend Lane Amount", 1.0f));
    l.add (choice ("bl_len", "Bend Lane Length", bendlane::lengthNames(), 1));
    l.add (choice ("bl_rng", "Bend Lane Range", bendlane::rangeNames(), 2));
    l.add (choice ("bl_mode", "Bend Lane Mode", { "SYNC", "RETRIGGER" }, 0));
    // TRAP (after everything else; left out of the saved state while at their defaults, so old states stay byte-for-byte the same)
    l.add (pct ("br_scoop", "Brass Scoop", 0.0f));
    l.add (secs ("br_scoopt", "Brass Scoop Time", 0.01f, 0.5f, 0.08f));
    l.add (pct ("br_growl", "Brass Growl", 0.0f));
    l.add (pct ("br_form", "Brass Formant", 0.0f));
    l.add (pct ("br_formf", "Brass Formant Freq", 0.45f));
    l.add (pct ("br_fall", "Brass Fall", 0.0f));
    l.add (pct ("ch_amt", "Character", 0.0f));
    l.add (boolean ("tp_on", "Tape Mode", false));
    l.add (pct ("tp_amt", "Tape Amount", 0.5f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "tp_slow", 1 }, "Tape Slow", NormalisableRange<float> (-12.0f, 0.0f, 0.01f), -3.0f,
               AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " st"; })));
    for (int k = 1; k <= NUM_LAYERS; ++k)
    {
        const String p = "ly" + String (k) + "_", N = "Layer " + String (k) + " ";
        if (k > 1) l.add (choice (p + "src", N + "Source", layerSrcNames(), 0));
        l.add (integer (p + "oct", N + "Octave", -3, 3, 0));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { p + "det", 1 }, N + "Detune", NormalisableRange<float> (-100.0f, 100.0f), 0.0f,
                   AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " ct"; })));
        l.add (pct (p + "lvl", N + "Level", k == 1 ? 1.0f : 0.8f));
        l.add (bip (p + "pan", N + "Pan", 0.0f));
        l.add (secs (p + "dly", N + "Start Delay", 0.0f, 1.0f, 0.0f));
        l.add (boolean (p + "rev", N + "Reverse", false));
        l.add (choice (p + "play", N + "Play Mode", playModeNames(), 0));
        if (k > 1) l.add (boolean (p + "fx", N + "Layer FX", false));
    }
    l.add (integer ("stk_vsplit", "Velocity Split", 1, 127, 80));
    l.add (integer ("stk_ksplit", "Key Split", 0, 127, 60));
    l.add (integer ("smp_root", "Sample Root", 0, 127, 60));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "smp_fine", 1 }, "Sample Fine", NormalisableRange<float> (-100.0f, 100.0f), 0.0f,
               AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " ct"; })));
    l.add (boolean ("smp_formant", "Sample Formant Keep", true));
    l.add (boolean ("smp_loop", "Sample Loop", false));
    l.add (pct ("smp_start", "Sample Start", 0.0f));
    l.add (pct ("smp_end", "Sample End", 1.0f));
    l.add (secs ("smp_a", "Sample Attack", 0.001f, 8.0f, 0.002f));
    l.add (secs ("smp_d", "Sample Decay", 0.005f, 8.0f, 1.0f));
    l.add (pct ("smp_s", "Sample Sustain", 1.0f));
    l.add (secs ("smp_r", "Sample Release", 0.005f, 10.0f, 0.3f));
    l.add (pct ("smp_revvel", "Sample Reverse Above Velocity", 0.0f));
    // BYPASS stays LAST so no existing automation index moves (see Shared/Bypass.h)
    l.add (boolean ("bypass", "Bypass", false));
    // MORPH BOARD puck (after BYPASS: no older index moves); never part of a sound
    l.add (pct ("morph_x", "Morph X", 0.5f));
    l.add (pct ("morph_y", "Morph Y", 0.5f));
    // FATTEN (FX rack unit added 2026-10-23): after everything else, no older index moves; left out of the saved state at its defaults
    gpfx::Rack::addLateParameters (l);
    // CLEAN SOUND + the visible TRIM (2026-10-23): after everything else, no older index moves
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "out_trim", 1 }, "Trim", NormalisableRange<float> (-24.0f, 18.0f, 0.01f), 0.0f,
               AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));
    l.add (boolean ("cl_on", "Clean", true));
    l.add (pct ("cl_width", "Clean Pocket Width", 0.5f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "cl_lowcut", 1 }, "Clean Low Cut", NormalisableRange<float> (30.0f, 300.0f, 1.0f, 0.5f), 100.0f,
               AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " Hz"; })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "cl_mono", 1 }, "Clean Mono Below", NormalisableRange<float> (0.0f, 300.0f, 1.0f, 0.6f), 120.0f,
               AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 1.0f ? String ("off") : String (roundToInt (v)) + " Hz"; })));
    l.add (pct ("cl_mud", "Clean Mud", 0.5f));
    // SOUND ROUND 2 (2026-10-24): after everything else, no older index moves. A state without them loads the CLASSIC laws (same
    // sound); new sounds / INIT get the new ones
    l.add (choice ("f_dlaw", "Filter Drive Law", { "Classic", "Inside" }, 1));
    l.add (boolean ("f_clean", "Filter Clean Mode", false));
    l.add (choice ("uni_law", "Unison Law", { "Classic", "V2" }, 1));
    l.add (choice ("nz_type", "Noise Type", { "Classic", "White", "Pink", "Brown", "Blue", "Violet", "Geiger", "Breath" }, 1));
    l.add (pct ("nz_stereo", "Noise Stereo", 1.0f));
    // GHOST VERB: the REVERB knob's engine (Classic = the old one for older sounds) and its settings
    l.add (choice ("verb_type", "Reverb Type", { "Classic", "Ghost" }, 1));
    l.add (pct ("verb_size", "Reverb Size", 0.6f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "verb_decay", 1 }, "Reverb Decay", NormalisableRange<float> (0.2f, 12.0f, 0.01f, 0.4f), 1.6f,
               AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " s"; })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "verb_pre", 1 }, "Reverb Pre-Delay", NormalisableRange<float> (0.0f, 200.0f, 0.1f, 0.5f), 12.0f,
               AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " ms"; })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "verb_lo", 1 }, "Reverb Low Cut", NormalisableRange<float> (20.0f, 1000.0f, 1.0f, 0.5f), 180.0f,
               AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " Hz"; })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "verb_hi", 1 }, "Reverb High Cut", NormalisableRange<float> (1000.0f, 20000.0f, 1.0f, 0.5f), 9000.0f,
               AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return roundToInt (v) >= 1000 ? String (v / 1000.0f, 1) + " kHz" : String (roundToInt (v)) + " Hz"; })
                                              .withValueFromStringFunction ([] (const String& t) { float f = t.retainCharacters ("0123456789.").getFloatValue(); if (t.toLowerCase().contains ("k")) f *= 1000.0f; return jlimit (1000.0f, 20000.0f, f); })));
    l.add (pct ("verb_duck", "Reverb Duck", 0.0f));
    l.add (pct ("verb_width", "Reverb Width", 1.0f));
    // QUALITY (round 2 item 6): the engine rate. A state without it loads Draft (the sound it was saved with, bit for bit); INIT gets High.
    // "Ultra when rendering" and LOCK are session settings (never part of a sound)
    l.add (choice ("quality", "Quality", { "Draft", "High", "Ultra" }, 1));
    l.add (boolean ("q_render", "Ultra When Rendering", true));
    l.add (boolean ("q_lock", "Quality Lock", false));
    // WARP + SPECTRAL (round 3 item 2): per oscillator two warp slots in series and one spectral slot; everything defaults to Off
    for (int o = 1; o <= 2; ++o)
    {
        const String p = "o" + String (o) + "_", N = "Osc " + String (o) + " ";
        l.add (choice (p + "w1", N + "Warp 1", warpModeNames(), 0));
        l.add (pct (p + "w1a", N + "Warp 1 Amount", 0.0f));
        l.add (choice (p + "w2", N + "Warp 2", warpModeNames(), 0));
        l.add (pct (p + "w2a", N + "Warp 2 Amount", 0.0f));
        l.add (choice (p + "spec", N + "Spectral", specModeNames(), 0));
        l.add (pct (p + "speca", N + "Spectral Amount", 0.0f));
    }
    for (int o = 1; o <= 2; ++o)
    {
        const String p = "o" + String (o) + "_", N = "Osc " + String (o) + " ";
        l.add (choice (p + "dmode", N + "Detune Mode", { "Linear", "Super", "Exp", "Inv", "Random" }, 1));
        l.add (choice (p + "stack", N + "Stack", { "Off", "12", "12+7", "Center -12", "Center -24" }, 0));
    }
    // VOICING (round 3 item 6): after everything else, no older index moves; the defaults are the voice allocation before them, bit for bit
    l.add (boolean ("v_same1", "Limit Same Note Poly To 1", false));
    l.add (choice ("v_steal", "Voice Steal Priority", stealPriorityNames(), (int) gpro::stOldest));
    gpro::ModRouter::addParameters (l);          // MOD (round 3): LFO 1 / 2 DEPTH, LFO 3 / 4 (after everything else, no older index moves)
    // ENGINES (round 3): an engine's three controls, the source a TEXTURE / RESONATOR is fed with and their own settings (defaults: an older
    // state never had an engine, so none of these changes its sound)
    for (int o = 1; o <= 2; ++o)
    {
        const String p = "o" + String (o) + "_", N = "Osc " + String (o) + " ";
        l.add (pct (p + "eh", N + "Engine Harmonics", 0.5f));
        l.add (pct (p + "et", N + "Engine Timbre", 0.5f));
        l.add (pct (p + "em", N + "Engine Morph", 0.5f));
        l.add (choice (p + "esrc", N + "Engine Source", gpro::eng::sourceNames(), 0));
        l.add (pct (p + "tsize", N + "Texture Size", 0.5f));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { p + "tpitch", 1 }, N + "Texture Pitch", NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
                   AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " st"; })));
        l.add (pct (p + "tfb", N + "Texture Feedback", 0.0f));
        l.add (boolean (p + "tfrz", N + "Texture Freeze", false));
        l.add (pct (p + "rpos", N + "Resonator Position", 0.5f));
        l.add (choice (p + "rmodel", N + "Resonator Model", gpro::eng::resonatorModelNames(), 0));
    }
    return l;
}

// the round-2 law switches (choice 0 = CLASSIC = the sound before 2026-10-24): older states and factory sounds without a spec entry load with 0
const std::vector<const char*>& ProProcessor::legacyLawParams() { static const std::vector<const char*> v { "f_dlaw", "uni_law", "nz_type", "verb_type", "quality" }; return v; }

int ProProcessor::engineLatency() const noexcept
{   // Draft: the INSIDE drive's 4x path (17) or nothing; High: the 2x decimator (25) + the drive's 2x path (24 engine = 12); Ultra: the 4x decimator (12)
    const int q = engineQLive.load (std::memory_order_relaxed);
    const bool inside = fDLaw != nullptr && (int) fDLaw->load() == dlInside;
    return q == 1 ? (inside ? Oversampler4::DELAY : 0) : q == 2 ? (inside ? 37 : 25) : 12;
}

ProProcessor::ProProcessor() : ProProcessor (false) {}

ProProcessor::ProProcessor (bool layerProcessor)
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "GHOSTYPRO", layout()), isLayer (layerProcessor)
{
    auto P = [this] (const juce::String& id) { auto* v = apvts.getRawParameterValue (id); jassert (v != nullptr); return macros.shadow (id, v, apvts.getParameter (id)); };
    for (int o = 0; o < 2; ++o)
    {
        auto& O = o == 0 ? o1 : o2; const juce::String p = "o" + juce::String (o + 1) + "_";
        O.on = P (p + "on"); O.tbl = P (p + "tbl"); O.pos = P (p + "pos"); O.lvl = P (p + "lvl"); O.oct = P (p + "oct"); O.semi = P (p + "semi");
        O.fine = P (p + "fine"); O.uni = P (p + "uni"); O.det = P (p + "det"); O.spr = P (p + "spr"); O.bln = P (p + "bln");
        O.w1 = P (p + "w1"); O.w1a = P (p + "w1a"); O.w2 = P (p + "w2"); O.w2a = P (p + "w2a"); O.spec = P (p + "spec"); O.speca = P (p + "speca");
        auto& E = o == 0 ? eng1 : eng2;     // ENGINES (round 3)
        E.h = P (p + "eh"); E.t = P (p + "et"); E.m = P (p + "em"); E.src = P (p + "esrc"); E.size = P (p + "tsize"); E.pitch = P (p + "tpitch");
        E.fb = P (p + "tfb"); E.frz = P (p + "tfrz"); E.rpos = P (p + "rpos"); E.rmodel = P (p + "rmodel");
    }
    subLvl = P ("sub_lvl"); subOct = P ("sub_oct"); subShp = P ("sub_shp"); nzLvl = P ("nz_lvl"); nzCol = P ("nz_col"); xmod = P ("xmod"); xamt = P ("xamt");
    fType = P ("f_type"); cutoff = P ("cutoff"); res = P ("res"); fenvAmt = P ("fenv"); fDrive = P ("f_drive"); fKey = P ("f_key"); velFilt = P ("vel_f");
    fA = P ("f_a"); fD = P ("f_d"); fS = P ("f_s"); fR = P ("f_r");
    attack = P ("attack"); decay = P ("decay"); sustain = P ("sustain"); release = P ("release"); curve = P ("curve"); velAmp = P ("vel_amp");
    const char* adsr[] { "a", "d", "s", "r" };
    for (int i = 0; i < 4; ++i) { e2[i] = P (juce::String ("e2_") + adsr[i]); e3[i] = P (juce::String ("e3_") + adsr[i]); }
    for (int k = 0; k < 2; ++k)
    {
        const juce::String p = "l" + juce::String (k + 1) + "_";
        lfoRate[k] = P (p + "rate"); lfoSync[k] = P (p + "sync"); lfoDiv[k] = P (p + "div"); lfoShape[k] = P (p + "shape"); lfoTrig[k] = P (p + "trig"); lfoFade[k] = P (p + "fade");
    }
    for (int s = 0; s < NUM_SLOTS; ++s)
    {
        const juce::String p = "m" + juce::String (s + 1) + "_";
        mSrc[s] = P (p + "src"); mDst[s] = P (p + "dst"); mAmt[s] = P (p + "amt");
    }
    for (int m = 0; m < 4; ++m) macro[m] = P ("mac" + juce::String (m + 1));
    glideP = P ("glide"); monoP = P ("mono"); legatoP = P ("legato"); polyP = P ("poly"); bendP = P ("bend"); gain = P ("gain");
    drive = P ("drive"); crush = P ("crush"); chorus = P ("chorus"); phaser = P ("phaser"); echo = P ("echo"); verb = P ("verb"); haunt = P ("haunt");
    blOn = P ("bl_on"); blAmt = P ("bl_amt"); blLen = P ("bl_len"); blRng = P ("bl_rng"); blMode = P ("bl_mode");
    brScoop = P ("br_scoop"); brScoopT = P ("br_scoopt"); brGrowl = P ("br_growl"); brForm = P ("br_form"); brFormF = P ("br_formf"); brFall = P ("br_fall");
    chAmt = P ("ch_amt"); tpOn = P ("tp_on"); tpAmt = P ("tp_amt"); tpSlow = P ("tp_slow");
    for (int k = 0; k < NUM_LAYERS; ++k)
    {
        const juce::String p = "ly" + juce::String (k + 1) + "_";
        ly[k].src = k > 0 ? P (p + "src") : nullptr; ly[k].oct = P (p + "oct"); ly[k].det = P (p + "det"); ly[k].lvl = P (p + "lvl"); ly[k].pan = P (p + "pan");
        ly[k].dly = P (p + "dly"); ly[k].rev = P (p + "rev"); ly[k].play = P (p + "play"); ly[k].fx = k > 0 ? P (p + "fx") : nullptr;
        layerMidi[k].prepare();
    }
    stkVSplit = P ("stk_vsplit"); stkKSplit = P ("stk_ksplit");
    smpRoot = P ("smp_root"); smpFine = P ("smp_fine"); smpFormant = P ("smp_formant"); smpLoop = P ("smp_loop"); smpStart = P ("smp_start"); smpEnd = P ("smp_end");
    smpA = P ("smp_a"); smpD = P ("smp_d"); smpS = P ("smp_s"); smpR = P ("smp_r"); smpRevVel = P ("smp_revvel");
    layerBuf.setSize (2, 8192); chunkMidi.ensureSize (midiCapacity + 64); fineMidi.ensureSize (midiCapacity + 64);
    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter ("bypass")); jassert (bypassParam != nullptr);
    // the STACK's two layer processors exist from the start (message thread), so loading a preset only switches their program
    if (! isLayer) for (int k = 1; k < NUM_LAYERS; ++k) child[k] = std::make_unique<ProProcessor> (true);
    if (! isLayer) makeSnapshotBank();
    morphX = apvts.getRawParameterValue ("morph_x"); morphY = apvts.getRawParameterValue ("morph_y");
    fDLaw = P ("f_dlaw"); fClean = P ("f_clean"); uniLaw = P ("uni_law"); nzType = P ("nz_type"); nzStereo = P ("nz_stereo");
    qualityP = P ("quality"); qRender = P ("q_render"); qLock = P ("q_lock");
    sameNoteP = P ("v_same1"); stealP = P ("v_steal");
    vbType = P ("verb_type"); vbSize = P ("verb_size"); vbDecay = P ("verb_decay"); vbPre = P ("verb_pre"); vbLo = P ("verb_lo"); vbHi = P ("verb_hi"); vbDuck = P ("verb_duck"); vbWidth = P ("verb_width");
    o1.dmode = P ("o1_dmode"); o1.stack = P ("o1_stack"); o2.dmode = P ("o2_dmode"); o2.stack = P ("o2_stack");
    trimP = P ("out_trim"); trimRaw = apvts.getRawParameterValue ("out_trim"); clOn = P ("cl_on"); clWidth = P ("cl_width"); clLowCut = P ("cl_lowcut"); clMono = P ("cl_mono"); clMud = P ("cl_mud");
    keyP = P ("key"); scaleP = P ("scale"); chordP = P ("chord"); arpP = P ("arp"); arpRateP = P ("arpRate"); arpModeP = P ("arpMode"); arpOctP = P ("arpOct"); arpGateP = P ("arpGate");

    modAttach();                                 // MOD (round 3): after every other shadow
    rack.attach (apvts);
    frameA = ghosty::makeShape (2); frameB = ghosty::makeShape (3);
    savedA = frameA; savedB = frameB;
    for (int i = 0; i < MAX_VOICES; ++i) synth.addVoice (new ProVoice (*this));
    synth.addSound (new ProSound());
    for (auto* id : { "waveA", "waveB", "o1_tbl", "o2_tbl", "o1_spec", "o1_speca", "o2_spec", "o2_speca", "o1_esrc", "o2_esrc" }) apvts.addParameterListener (id, this);   // (ENGINES: + SOURCE)
    keyboardState.addListener (this);
    merged.ensureSize (midiCapacity); performed.ensureSize (midiCapacity);
    performer.setOutputCapacity (midiCapacity);
    for (auto* v : { &laneSemiBuf, &laneRatioBuf, &laneNormBuf }) v->assign (8192, 0.0f);
    builtRate = 0;
    if (juce::MessageManager::existsAndIsCurrentThread()) flushPendingWaves();
#if GHOSTY_FACTORY_PRESETS
    setCurrentProgram (0);
#else
    if (! isLayer) refreshUserPrograms();        // FACTORY PRESETS OFF: a fresh instance starts at INIT (program 0)
    initPatch();
#endif
    morph.init (macros.slotCount());
    {   // the FX rack's continuous parameters, for the MORPH BOARD blend
        std::vector<juce::RangedAudioParameter*> fxp; std::vector<std::atomic<float>*> fxr;
        for (auto* prm0 : getParameters())
            if (auto* fp = dynamic_cast<juce::AudioParameterFloat*> (prm0); fp != nullptr && fp->getParameterID().startsWith ("fx_"))
            { fxp.push_back (fp); fxr.push_back (apvts.getRawParameterValue (fp->getParameterID())); }
        morph.initFx (std::move (fxp), std::move (fxr));
    }
    startTimerHz (20);
}

ProProcessor::~ProProcessor()
{
    stopTimer();
    keyboardState.removeListener (this);
    for (auto* id : { "waveA", "waveB", "o1_tbl", "o2_tbl", "o1_spec", "o1_speca", "o2_spec", "o2_speca", "o1_esrc", "o2_esrc" }) apvts.removeParameterListener (id, this);
}

void ProProcessor::parameterChanged (const juce::String& id, float)
{
    if (id == "waveA") needA = true;
    else if (id == "waveB") needB = true;
    else needTables = true;
}

void ProProcessor::timerCallback()
{
    flushPendingWaves();
    gpro::eng::diary::flush();                  // ENGINES DIARY: the audio thread's engine events, to disk (message thread)
    if (! isLayer) morphTick();
    waves.collect(); sampleBox.collect(); macros.collect();
    modr.collect(); if (! isLayer) modr.applyLearned (apvts);      // MOD (round 3): MIDI learn
    const int lat = rack.latencySamples() + ceilingLatency() + engineLatency();
    if (lat != latencyNow) { latencyNow = lat; setLatencySamples (lat); }
}

// ------------------------------------------------------------------ waves
void ProProcessor::postFrames (const ghosty::Frame& a, const ghosty::Frame& b, int shapeA, int shapeB)
{
    {
        const juce::SpinLock::ScopedLockType sl (reqLock);
        request.has = true; request.a = a; request.b = b; request.shapeA = shapeA; request.shapeB = shapeB;
    }
    requestPending = true;
    if (juce::MessageManager::existsAndIsCurrentThread()) flushPendingWaves();
}

void ProProcessor::rebuildWaves()
{
    needGhost = true;
    if (juce::MessageManager::existsAndIsCurrentThread()) flushPendingWaves();
}

void ProProcessor::flushPendingWaves()
{
    if (! juce::MessageManager::existsAndIsCurrentThread()) return;
    bool ghostChanged = false, sampleChanged = false, userChanged = false;
    if (requestPending.exchange (false))
    {
        FrameRequest req;
        {
            const juce::SpinLock::ScopedLockType sl (reqLock);
            req = std::move (request); request = FrameRequest();
        }
        if (req.has) { frameA = req.a; frameB = req.b; frameShapeA = req.shapeA; frameShapeB = req.shapeB; ghostChanged = true; }
        if (req.hasSample) { sampleFrames = std::move (req.sample); sampleLabel = req.sampleName; sampleChanged = true; }
        if (req.hasUser) { userDoc = std::move (req.user); userChanged = true; }
    }
    // a shape picked in the A / B boxes: lined up with the other frame (same fundamental phase) so POS morphs between them smoothly
    // (factory sounds and saved frames arrive through the request above, untouched)
    if (needA.exchange (false)) { const int s = (int) apvts.getRawParameterValue ("waveA")->load(); if (s != frameShapeA) { frameA = ghosty::makeShape (s); ghosty::alignFundamental (frameA, frameB); frameShapeA = s; ghostChanged = true; } }
    if (needB.exchange (false)) { const int s = (int) apvts.getRawParameterValue ("waveB")->load(); if (s != frameShapeB) { frameB = ghosty::makeShape (s); ghosty::alignFundamental (frameB, frameA); frameShapeB = s; ghostChanged = true; } }
    if (needGhost.exchange (false)) ghostChanged = true;
    const double sr = sampleRateNow.load();
    if (std::abs (sr - builtRate) > 1.0) { builtRate = sr; ghostChanged = sampleChanged = userChanged = true; }
    if (ghostChanged)
    {
        ghostTable = buildTable ({ fromFrame (frameA), fromFrame (frameB) }, builtRate, tGhost);
        const juce::SpinLock::ScopedLockType sl (reqLock);
        savedA = frameA; savedB = frameB;
    }
    if (sampleChanged)
    {
        sampleTable = sampleFrames.empty() ? nullptr : buildTable (sampleFrames, builtRate, tSample);
        const juce::SpinLock::ScopedLockType sl (reqLock);
        savedSample = sampleFrames; savedSampleName = sampleLabel;
    }
    if (userChanged)
    {
        userTbl = userDoc.valid() ? buildTable (userDoc.frames, builtRate, tUser) : nullptr;
        const juce::SpinLock::ScopedLockType sl (reqLock);
        savedUser = userDoc;
    }
    if (ghostChanged || sampleChanged || userChanged || needTables.exchange (false)) publishTables();
}

std::vector<HiFrame> ProProcessor::sourceFramesOf (int id) const
{
    if (id == tGhost) return { fromFrame (savedA), fromFrame (savedB) };
    if (id == tSample) return savedSample.empty() ? std::vector<HiFrame> { fromFrame (savedA), fromFrame (savedB) } : savedSample;
    if (id == tUser) return savedUser.valid() ? savedUser.frames : std::vector<HiFrame> { fromFrame (savedA), fromFrame (savedB) };
    return makeBuiltIn (id);
}

void ProProcessor::publishTables()
{
    auto tableFor = [this] (int id)
    {
        if (id == tGhost) return ghostTable;
        if (id == tSample) return sampleTable != nullptr ? sampleTable : ghostTable;
        if (id == tUser) return userTbl != nullptr ? userTbl : ghostTable;
        return cache.get (id, builtRate);
    };
    // SPECTRAL (round 3): a mode with an amount above 0 rebuilds the oscillator's table from its source frames, reshaped in the
    // spectral domain (the amount is quantised to 1/200 so a knob drag rebuilds at most 200 distinct tables; built-ins are cached)
    auto spectralFor = [this, &tableFor] (int id, int mode, float amt) -> std::shared_ptr<const Table>
    {
        const int q = (int) std::lround (juce::jlimit (0.0f, 1.0f, amt) * 200.0f);
        if (mode <= 0 || q <= 0) return tableFor (id);
        const float a = (float) q / 200.0f;
        const auto key = std::make_tuple (id, mode, q);
        const bool builtIn = id != tGhost && id != tSample && id != tUser;
        if (builtIn) { auto it = specCache.find (key); if (it != specCache.end()) return it->second; }
        auto t = buildTable (spectralProcessFrames (sourceFramesOf (id), mode, a), builtRate, id);
        if (builtIn) { if (specCache.size() > 24) specCache.clear(); specCache[key] = t; }
        return t;
    };
    auto set = std::make_unique<WaveSet>();
    // ENGINES (round 3): an oscillator on an engine holds the wavetable its SOURCE names (TEXTURE / RESONATOR) or a cheap cached one
    const int slot1 = gpro::eng::tableSlotFor ((int) o1.tbl->load(), (int) apvts.getRawParameterValue ("o1_esrc")->load());
    const int slot2 = gpro::eng::tableSlotFor ((int) o2.tbl->load(), (int) apvts.getRawParameterValue ("o2_esrc")->load());
    set->t1 = spectralFor (slot1, (int) apvts.getRawParameterValue ("o1_spec")->load(), apvts.getRawParameterValue ("o1_speca")->load());   // (raw: the macro shadows only follow inside processBlock)
    set->t2 = spectralFor (slot2, (int) apvts.getRawParameterValue ("o2_spec")->load(), apvts.getRawParameterValue ("o2_speca")->load());
    set->sampleRate = builtRate;
    shownT1 = set->t1; shownT2 = set->t2;
    waves.post (std::move (set));
}

std::shared_ptr<const Table> ProProcessor::uiTable (int osc) const { return osc == 0 ? shownT1 : shownT2; }

juce::String ProProcessor::sampleName() const { const juce::SpinLock::ScopedLockType sl (reqLock); return savedSampleName; }

void ProProcessor::setUserTable (const gpro::wt::Doc& doc)
{
    {
        const juce::SpinLock::ScopedLockType sl (reqLock);
        request.hasUser = true; request.user = doc.valid() ? doc : gpro::wt::Doc();
        if (! doc.valid()) request.user.frames.clear();
    }
    requestPending = true;
    flushPendingWaves();
}
gpro::wt::Doc ProProcessor::userTable() const { const juce::SpinLock::ScopedLockType sl (reqLock); return request.hasUser ? request.user : savedUser; }
juce::String ProProcessor::userTableName() const { const juce::SpinLock::ScopedLockType sl (reqLock); const auto& d = request.hasUser ? request.user : savedUser; return d.valid() ? d.name : juce::String(); }

bool ProProcessor::importSample (const juce::File& file, juce::String& message)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (file));
    if (rd == nullptr) { message = "Couldn't read that file. Use a WAV, AIFF or FLAC."; return false; }
    const int len = (int) std::min<juce::int64> (rd->lengthInSamples, (juce::int64) (rd->sampleRate * 20.0));
    if (len < 256) { message = "That sample is too short."; return false; }
    juce::AudioBuffer<float> buf ((int) rd->numChannels, len);
    rd->read (&buf, 0, len, 0, true, true);
    std::vector<float> mono ((size_t) len, 0.0f);
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        for (int i = 0; i < len; ++i) mono[(size_t) i] += buf.getSample (ch, i) / (float) buf.getNumChannels();
    auto frames = sliceSample (mono, rd->sampleRate);
    if (frames.empty()) { message = "That sample is silent."; return false; }
    {
        const juce::SpinLock::ScopedLockType sl (reqLock);
        request.hasSample = true; request.sample = std::move (frames); request.sampleName = file.getFileNameWithoutExtension();
    }
    requestPending = true;
    flushPendingWaves();
    message = juce::String ((int) sampleFrames.size()) + " frames from " + file.getFileName();
    return true;
}

// ------------------------------------------------------------------ audio
void ProProcessor::handleNoteOn (juce::MidiKeyboardState*, int, int note, float vel)
{
    uiFifo.write (1).forEach ([&] (int i) { uiNotes[(size_t) i] = { (int8_t) note, (uint8_t) juce::jlimit (1, 127, juce::roundToInt (vel * 127.0f)), true }; });
}
void ProProcessor::handleNoteOff (juce::MidiKeyboardState*, int, int note, float)
{
    uiFifo.write (1).forEach ([&] (int i) { uiNotes[(size_t) i] = { (int8_t) note, 0, false }; });
}

void ProProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sampleRateNow = sampleRate;
    synth.setCurrentPlaybackSampleRate (sampleRate);
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* v = dynamic_cast<ProVoice*> (synth.getVoice (i))) v->prepare (sampleRate, samplesPerBlock);
    fx.prepare (sampleRate, samplesPerBlock);
    rack.prepare (sampleRate, samplesPerBlock);
    rack.reset();
    demo.prepare (sampleRate);
    performer.prepare (sampleRate);
    merged.ensureSize (midiCapacity); performed.ensureSize (midiCapacity);
    for (auto* v : { &laneSemiBuf, &laneRatioBuf, &laneNormBuf }) v->assign ((size_t) juce::jmax (8192, samplesPerBlock), 0.0f);
    laneSm = laneNormSm = 0; laneFreeBeats = laneTrigBeats = 0;
    lastGain = -1.0f; ly0G[0] = ly0G[1] = -1.0f;
    blockNow = samplesPerBlock;
    character.prepare (sampleRate, samplesPerBlock); tape.prepare (sampleRate); tapeSemisSm = 0;
    bypass.prepare (sampleRate, 0.006, bypassParam != nullptr && bypassParam->get());
    sampler.prepare (sampleRate, samplesPerBlock);
    layerBuf.setSize (2, juce::jmax (8192, samplesPerBlock));
    for (auto& c : child) if (c != nullptr) c->prepareToPlay (sampleRate, samplesPerBlock);
    ceiling.prepare (sampleRate); sumPockets.prepare (sampleRate);
    for (auto& pk : pocket) pk.prepare (sampleRate);
    charMatch.prepare (sampleRate); tapeMatch.prepare (sampleRate);
    preBuf.setSize (2, juce::jmax (8192, samplesPerBlock)); smpBuf.setSize (2, juce::jmax (8192, samplesPerBlock));
    voiceG = layerG = 1.0f; for (auto& pr : presence) pr = 0; bassOwner = 0; ownerHold = 0; cleanWas = false;
    latencyNow = rack.latencySamples() + ceilingLatency() + engineLatency();
    setLatencySamples (latencyNow);
    if (juce::MessageManager::existsAndIsCurrentThread()) flushPendingWaves();
    if (! isLayer)
    {   // ENGINES DIARY: what the host asked for (the rate / block every engine is prepared for)
        static std::atomic<int> left { 20 };   // (the first 20 per session: hosts call this often)
        if (left.fetch_sub (1) > 0)
        {
            juce::String l; l << "prepareToPlay " << sampleRate << " Hz, block " << samplesPerBlock << ", " << juce::AudioProcessor::getWrapperTypeDescription (wrapperType)
                              << ", host " << juce::PluginHostType().getHostDescription() << ", " << (juce::SystemStats::isOperatingSystem64Bit() ? "64" : "32") << "-bit "
                              << juce::SystemStats::getOperatingSystemName() << ", " << juce::SystemStats::getCpuModel()
                              << ", OSC tables " << (int) o1.tbl->load() << " / " << (int) o2.tbl->load();
            gpro::eng::diary::note (l);
        }
    }
}

bool ProProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void ProProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n > blockNow && blockNow > 0)
    {
        // a block bigger than prepareToPlay promised (some hosts do that): run it in prepared-size pieces, no buffer here grows
        for (int pos = 0; pos < n; pos += blockNow)
        {
            const int len = juce::jmin (blockNow, n - pos);
            juce::AudioBuffer<float> sub (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), pos, len);
            chunkMidi.clear();
            for (const auto meta : midi)
                if (meta.samplePosition >= pos && meta.samplePosition < pos + len && chunkMidi.data.size() + 8 + meta.numBytes <= midiCapacity)
                    chunkMidi.addEvent (meta.data, meta.numBytes, meta.samplePosition - pos);
            processBlock (sub, chunkMidi);
        }
        return;
    }
    if (! isLayer && ! inFineSteps && n > fineStep && (macros.wantsFineSteps() || modr.wantsFineSteps()))   // (MOD round 3: a moving modulation too)
    {
        // a sound with MACROS whose knob is moving (or still gliding): render in short sub-blocks, so every macro-driven parameter
        // (and the per-block ones such as CHARACTER) steps every 32 samples instead of once per host block. The play head is offset
        // per sub-block (tempo-synced LFOs, arp, BEND lane stay on the beat). Sounds without macros never get here.
        inFineSteps = true;
        fineBlocks.fetch_add (1, std::memory_order_relaxed);
        auto* outer = getPlayHead();
        fineHead.inner = outer; fineHead.sr = sampleRateNow.load();
        for (int pos = 0; pos < n; pos += fineStep)
        {
            const int len = juce::jmin (fineStep, n - pos);
            juce::AudioBuffer<float> sub (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), pos, len);
            fineMidi.clear();
            for (const auto meta : midi)
                if (meta.samplePosition >= pos && meta.samplePosition < pos + len && fineMidi.data.size() + 8 + meta.numBytes <= midiCapacity)
                    fineMidi.addEvent (meta.data, meta.numBytes, meta.samplePosition - pos);
            fineHead.offset = pos;
            setPlayHead (outer != nullptr ? &fineHead : nullptr);
            processBlock (sub, fineMidi);
        }
        setPlayHead (outer);
        inFineSteps = false;
        return;
    }
    if (! isLayer)
    {
        // BYPASS: on the engage edge every voice, layer, pending note and FX tail goes (the 6 ms fade covers the cut); fully
        // bypassed nothing runs and incoming MIDI (host and on-screen keys) is dropped; the fade back in happens in applyMute
        bypass.set (bypassParam->get());
        if (bypass.takeEngageEdge()) bypassResetPending = true;      // the 6 ms fade runs on the live sound first (a hard reset here clicks)
        if (! bypass.isBypassed()) bypassResetPending = false;
        if (bypass.silent())
        {
            if (bypassResetPending)
            {
                // faded out: now everything lets go, so nothing is left to come back when BYPASS is released
                bypassResetPending = false;
                bypassReset();
            }
            buffer.clear(); midi.clear();
            uiFifo.read (uiFifo.getNumReady());
            level.store (0.0f); voicesLive.store (0); for (auto& v : layerVoices) v.store (0);
            return;
        }
    }
    buffer.clear();
    if (! isLayer)
    {   // MORPH BOARD: the blended values go into the shadows below (nothing happens while the board is off)
        double ppqNow = 0; bool playingNow = false;
        if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) { playingNow = pos->getIsPlaying(); if (auto q = pos->getPpqPosition()) ppqNow = *q; }
        const double srNow = sampleRateNow.load();
        morphFreeSecs += n / srNow;
        morphTrim = morph.process (macros, morphX->load(), morphY->load(), ppqNow, playingNow, morphFreeSecs, n, srNow);
    }
    macros.update (n, sampleRateNow.load());      // the shadows every float parameter is read through: raw + smoothed macro offsets
    ctl.waves = waves.acquire(); ctl.wavesPrev = waves.previousSet(); ctl.wavesSwap = waves.swapCount();

    constexpr int header = (int) (sizeof (int32_t) + sizeof (uint16_t));
    merged.clear();
    for (const auto meta : midi)
        if (merged.data.size() + header + meta.numBytes <= midiCapacity) merged.addEvent (meta.data, meta.numBytes, meta.samplePosition);
    uiFifo.read (uiFifo.getNumReady()).forEach ([&] (int i)
    {
        const auto& u = uiNotes[(size_t) i];
        if (merged.data.size() + header + 3 <= midiCapacity)
            merged.addEvent (u.on ? juce::MidiMessage::noteOn (1, u.note, (juce::uint8) u.vel) : juce::MidiMessage::noteOff (1, u.note), 0);
    });

    ghosty::Performer::Settings ps;
    double beatsPerBar = 4.0;
    ps.key = (int) keyP->load(); ps.scale = (int) scaleP->load(); ps.chord = (int) chordP->load();
    ps.arp = arpP->load() > 0.5f; ps.arpRate = (int) arpRateP->load(); ps.arpMode = (int) arpModeP->load();
    ps.arpOct = (int) arpOctP->load(); ps.arpGate = arpGateP->load();
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) ps.bpm = *b;
            ps.hostPlaying = pos->getIsPlaying();
            if (auto q = pos->getPpqPosition()) { ps.ppq = *q; ps.havePpq = true; }
            if (auto ts = pos->getTimeSignature(); ts && ts->numerator > 0 && ts->denominator > 0) beatsPerBar = juce::jlimit (1.0, 32.0, ts->numerator * 4.0 / ts->denominator);
        }
    performer.process (merged, performed, n, ps);
    for (const auto meta : performed)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn()) heldNotes[(size_t) m.getNoteNumber()].store (1, std::memory_order_relaxed);
        else if (m.isNoteOff()) heldNotes[(size_t) m.getNoteNumber()].store (0, std::memory_order_relaxed);
        else if (m.isAllNotesOff() || m.isAllSoundOff()) for (auto& h : heldNotes) h.store (0, std::memory_order_relaxed);
    }
    modBlock (n, ps.bpm, ps.ppq, ps.hostPlaying && ps.havePpq, merged);      // MOD (round 3): the router's global part (into the shadows)

    const double sr = sampleRateNow.load();
    synth.setMono (monoP->load() > 0.5f, legatoP->load() > 0.5f);
    synth.polyLimit = juce::jlimit (1, MAX_VOICES, (int) polyP->load());
    synth.sameNoteLimit = sameNoteP->load() > 0.5f; synth.stealPriority = juce::jlimit (0, (int) gpro::numSteal - 1, (int) stealP->load());
    ctl.glide = glideP->load(); ctl.bendSemis = bendP->load();
    for (int k = 0; k < 2; ++k)
    {
        if (lfoSync[k]->load() > 0.5f)
        {
            const double qn = lfoDivQuarterNotes ((int) lfoDiv[k]->load());
            ctl.lfoInc[k] = ps.bpm / 60.0 / sr / qn;
            ctl.lfoGlobal[k] = (ps.hostPlaying && ps.havePpq) ? ps.ppq / qn : lfoFree[k];
        }
        else { ctl.lfoInc[k] = lfoRate[k]->load() / sr; ctl.lfoGlobal[k] = lfoFree[k]; }
        lfoFree[k] = ctl.lfoGlobal[k] + ctl.lfoInc[k] * n;
        if (lfoFree[k] > 1.0e6) lfoFree[k] -= 1.0e6;
    }
    ctl.anyMod = false;
    for (int s = 0; s < NUM_SLOTS; ++s)
    {
        ctl.slotSrc[s] = (int) mSrc[s]->load(); ctl.slotDst[s] = (int) mDst[s]->load(); ctl.slotAmt[s] = mAmt[s]->load();
        ctl.anyMod |= ctl.slotSrc[s] > 0 && ctl.slotDst[s] > 0 && std::abs (ctl.slotAmt[s]) > 1.0e-6f;
    }

    computeLane (n, ps.bpm, ps.ppq, ps.hostPlaying && ps.havePpq, beatsPerBar, performed);

    // ---- TRAP block-level mod targets (CHARACTER, TAPE amount, layer levels, sample start): sources from the newest voice
    std::fill (std::begin (extMod), std::end (extMod), 0.0f);
    if (ctl.anyMod)
    {
        const ProVoice* lead = nullptr;
        for (int i = 0; i < synth.getNumVoices(); ++i)
            if (auto* v = dynamic_cast<ProVoice*> (synth.getVoice (i))) if (v->isVoiceActive() && (lead == nullptr || v->startedAt > lead->startedAt)) lead = v;
        for (int s = 0; s < NUM_SLOTS; ++s)
        {
            const int src = ctl.slotSrc[s], dst = ctl.slotDst[s];
            if (src <= 0 || dst < dCharacter || dst >= numDst || std::abs (ctl.slotAmt[s]) <= 1.0e-6f) continue;
            float v = 0;
            if (src >= srcMac1 && src <= srcMac4) v = macro[src - srcMac1]->load();
            else if (src == srcWheel) v = ctl.wheel;
            else if (lead != nullptr) v = lead->modValue (src);
            extMod[dst] += v * ctl.slotAmt[s];
        }
    }

    // ---- TRAP: pitch offsets, the main layer's play mode / delay / level, then the other layers on top
    const bool tapeOn = ! isLayer && tpOn->load() > 0.5f;
    const float tapeAmt = juce::jlimit (0.0f, 1.0f, tpAmt->load() + extMod[dTape]);
    tapeSemisSm += (Tape::semisFor (tapeOn, tpSlow->load()) - tapeSemisSm) * juce::jmin (1.0f, (float) n / (0.1f * (float) sr));
    if (std::abs (tapeSemisSm) < 1.0e-4f) tapeSemisSm = 0;
    const float mainSemis = (float) ly[0].oct->load() * 12.0f + ly[0].det->load() * 0.01f;
    ctl.extraSemis = isLayer ? externalSemis.load() : mainSemis + tapeSemisSm;
    reverseEnv = isLayer ? externalReverse.load() : ly[0].rev->load() > 0.5f;
    const int mainDly = (int) (ly[0].dly->load() * sr), mainPlay = (int) ly[0].play->load();
    const bool clean = cleanNow();
    ctl.clean = clean;
    ctl.cleanUni = clean && ! (cleanOff & 512);
    {   // QUALITY: Draft 1x / High 2x / Ultra 4x; "Ultra when rendering" lifts an offline render to 4x; a STACK layer follows its parent
        int q = isLayer ? externalQ.load (std::memory_order_relaxed) : 0;
        if (q <= 0) { const int sel = (int) qualityP->load(); q = sel == 0 ? 1 : sel == 1 ? 2 : 4; if (qRender->load() > 0.5f && isNonRealtime()) q = 4; }
        ctl.engineQ = q; engineQLive.store (q, std::memory_order_relaxed);
    }
    if (clean != cleanWas)
    {   // CLEAN switched: the pockets / matches start fresh (smoothed from neutral)
        cleanWas = clean;
        for (auto& pk : pocket) { pk.hpAmt = 0; pk.lean = 0; pk.width = 1; pk.budget = 1; pk.reset(); }
        sumPockets.reset(); charMatch.reset(); tapeMatch.reset(); voiceG = layerG = 1.0f; bassOwner = 0; ownerHold = 0;
    }
    if (clean && ! isLayer) cleanLayerParams (n);
    const juce::int64 t0 = juce::Time::getHighResolutionTicks();
    if (mainDly > 0 || mainPlay != pmAll || layerMidi[0].pending())      // pending(): a delayed note still has to land after DELAY went to 0
    {
        layerMidi[0].run (performed, n, mainDly, mainPlay, (int) stkVSplit->load(), (int) stkKSplit->load());
        synth.renderNextBlock (buffer, layerMidi[0].out, 0, n);
    }
    else synth.renderNextBlock (buffer, performed, 0, n);
    {
        bool anySolo = false; for (int k = 0; k < NUM_LAYERS; ++k) anySolo |= layerSolo[k].load (std::memory_order_relaxed);
        const bool silent = layerMute[0].load (std::memory_order_relaxed) || (anySolo && ! layerSolo[0].load (std::memory_order_relaxed));
        const float lvl = silent ? 0.0f : ly[0].lvl->load(), pan = juce::jlimit (-1.0f, 1.0f, ly[0].pan->load());
        const float gL = lvl * juce::jmin (1.0f, 1.0f - pan), gR = lvl * juce::jmin (1.0f, 1.0f + pan);
        if (ly0G[0] < 0.0f) { ly0G[0] = gL; ly0G[1] = gR; }
        if (lvl != 1.0f || pan != 0.0f || ly0G[0] != 1.0f || ly0G[1] != 1.0f)
        {   // ramped from the last block's gains (the same gains as before once the knobs rest)
            buffer.applyGainRamp (0, 0, n, ly0G[0], gL); if (buffer.getNumChannels() > 1) buffer.applyGainRamp (1, 0, n, ly0G[1], gR);
        }
        ly0G[0] = gL; ly0G[1] = gR;
    }
    if (clean && ! (cleanOff & 1)) applyVoiceLaw (buffer, n);
    if (clean && ! isLayer && buffer.getNumChannels() > 1 && ! (cleanOff & 256)) pocket[0].process (buffer.getWritePointer (0), buffer.getWritePointer (1), n);
    tap (0, buffer);
#if GPRO_TAPS
    if (buffer.getNumChannels() > 1) layerTap (0, buffer.getReadPointer (0), buffer.getReadPointer (1), n, 1.0f, 1.0f);
#endif
    {
        int active = 0; for (int i = 0; i < synth.getNumVoices(); ++i) if (synth.getVoice (i)->isVoiceActive()) ++active;
        layerVoices[0].store (active, std::memory_order_relaxed);
        {   // playing / max counting unison voices (round 2 item 6): every sounding note x (OSC 1 unison + OSC 2 unison if on), max = the voice limit x the same
            const int per = juce::jlimit (1, MAX_UNI, (int) o1.uni->load()) + (o2.on->load() > 0.5f ? juce::jlimit (1, MAX_UNI, (int) o2.uni->load()) : 0);
            uniVoicesLive.store (active * per, std::memory_order_relaxed); uniVoicesMax.store (synth.polyLimit * per, std::memory_order_relaxed);
            uniLayerLive[0].store (active * per, std::memory_order_relaxed); uniLayerMax[0].store (synth.polyLimit * per, std::memory_order_relaxed);
        }
        const float pct = (float) (juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0) / (n / sr) * 100.0);
        cpuLive[0].store (cpuLive[0].load (std::memory_order_relaxed) * 0.9f + pct * 0.1f, std::memory_order_relaxed);
    }
    if (! isLayer) renderLayers (buffer, n, performed, getPlayHead());
    if (clean && ! isLayer && ! (cleanOff & 256))
    {   // CLEAN gain law: the sounding layers sum at a steady loudness (equal power: 1 / sqrt (sum of level^2))
        float sum = 0;
        bool anySolo = false; for (int k = 0; k < NUM_LAYERS; ++k) anySolo |= layerSolo[k].load (std::memory_order_relaxed);
        for (int k = 0; k < NUM_LAYERS; ++k)
        {
            const bool silent = layerMute[k].load (std::memory_order_relaxed) || (anySolo && ! layerSolo[k].load (std::memory_order_relaxed));
            const bool on = k == 0 || (int) ly[k].src->load() != lsOff;
            const float lvl = (silent || ! on) ? 0.0f : juce::jlimit (0.0f, 1.0f, ly[k].lvl->load() + (k == 1 ? extMod[dLy2Lvl] : k == 2 ? extMod[dLy3Lvl] : 0.0f));
            const float present = (layerVoices[k].load (std::memory_order_relaxed) > 0 && lvl > 0.0f) ? 1.0f : 0.0f;   // muted / solo'd out = not sounding
            layerLvl[k] = lvl;
            presence[k] += (present - presence[k]) * juce::jmin (1.0f, (float) n / (float) ((present > presence[k] ? 0.005 : 0.3) * sr));
            sum += lvl * lvl * presence[k];
        }
        const float target = 1.0f / std::sqrt (juce::jmax (1.0f, sum));
        const float next = target < layerG ? target : layerG + (target - layerG) * juce::jmin (1.0f, (float) n / (float) (0.25 * sr));
        if (layerG != 1.0f || next != 1.0f) for (int ch = 0; ch < buffer.getNumChannels(); ++ch) buffer.applyGainRamp (ch, 0, n, layerG, next);
        layerG = next; layerLawLive.store (next, std::memory_order_relaxed);
    }
    tap (1, buffer);
    const bool ownFx = ! isLayer || externalFx.load (std::memory_order_relaxed);     // a layer's Ghost FX / CHARACTER only with LAYER FX on
    if (ownFx)
    {
        const float chA = juce::jlimit (0.0f, 1.0f, chAmt->load() + extMod[dCharacter]);
        if (clean && chA > 1.0e-4f && ! (cleanOff & 8))
        {
            for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch) preBuf.copyFrom (ch, 0, buffer, ch, 0, n);
            character.process (buffer, chA);
            matchStage (charMatch, buffer, n, 0.06f, 2.0f);            // CLEAN: CHARACTER at the input's loudness
        }
        else { character.process (buffer, chA); charMatch.reset(); }
        tap (2, buffer);
        ghosty::FxParams p;
        p.drive = drive->load(); p.crush = crush->load(); p.chorus = chorus->load(); p.phaser = phaser->load();
        p.echo = echo->load(); p.verb = verb->load(); p.haunt = haunt->load();
        p.bpm = ps.bpm; p.clean = clean && ! (cleanOff & 16); p.smooth = true;
        p.hq = ctl.engineQ >= 2 && (int) qualityP->load() >= 1;     // (sound audit: the FX DRIVE 4x oversampled, as the voice fixes: QUALITY High / Ultra)
        if ((int) vbType->load() == 1)
        {   // GHOST VERB: the REVERB knob is the amount (50 % = 17 % wet, 100 % = 35 %: a synth verb sits behind the sound), the rest from MORE
            auto& g = ghostVerbParams;
            g.mix = juce::jlimit (0.0f, 1.0f, p.verb * 0.35f); g.size = vbSize->load(); g.decay = vbDecay->load(); g.predelayMs = vbPre->load();
            g.loCut = vbLo->load(); g.hiCut = vbHi->load(); g.duck = vbDuck->load(); g.width = vbWidth->load() * 1.2f;
            g.damp = 0.45f; g.lowDamp = 0.35f; g.mod = 0.5f; g.diffusion = 0.7f;
            p.ghostVerb = &g;
        }
        fx.process (buffer, p);
        tap (3, buffer);
    }

    if (! isLayer && traceOn.load (std::memory_order_relaxed)) publishTrace();      // the MOD page's envelope dots
    rack.setCleanMatch (clean && ! (cleanOff & 32));
    int noteOns = 0, noteOnAt = -1;                                                                        // (HYPER's RETRIG: count + the first one's sample)
    for (const auto meta : performed) if (meta.getMessage().isNoteOn()) { if (noteOns++ == 0) noteOnAt = meta.samplePosition; }
    int fxNote = -1; for (const auto meta : performed) if (meta.getMessage().isNoteOn()) fxNote = meta.getMessage().getNoteNumber();   // FX RACK (round 3): DISTORTION key track
    rack.process (buffer, { sr, ps.bpm, ps.ppq, ps.hostPlaying, noteOns, noteOnAt, fxNote });
    tap (4, buffer);
    if (! isLayer)
    {
        if (clean && tapeOn && ! (cleanOff & 64))
        {
            for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch) preBuf.copyFrom (ch, 0, buffer, ch, 0, n);
            tape.process (buffer, tapeOn, tapeAmt);
            matchStage (tapeMatch, buffer, n, 0.06f, 2.0f);             // CLEAN: TAPE at the input's loudness
        }
        else { tape.process (buffer, tapeOn, tapeAmt); tapeMatch.reset(); }
        bypass.applyMute (buffer);
        if (clean && buffer.getNumChannels() > 1 && ! (cleanOff & 128))
        {
            sumPockets.monoHz = clMono->load(); sumPockets.mud = clMud->load();
            sumPockets.layers = presence[0] + presence[1] + presence[2];
            sumPockets.process (buffer.getWritePointer (0), buffer.getWritePointer (1), n);
        }
    }
    tap (5, buffer);

    const float g = outGain (gain->load()) * juce::Decibels::decibelsToGain (trimDbOf (trimP->load())) * (morphTrim >= 0.0f ? morphTrim : 1.0f);
    if (lastGain < 0.0f) lastGain = g;
    buffer.applyGainRamp (0, n, lastGain, g);
    lastGain = g;

    if (! isLayer) demo.process (buffer, license.isUnlocked());
    tap (6, buffer);

    if (! isLayer && buffer.getNumChannels() > 1)
    {   // the hidden CEILING, always on: true-peak look-ahead limiter + oversampled soft clip (a pass-through when nothing is over)
        auto* L = buffer.getWritePointer (0); auto* R = buffer.getWritePointer (1);
        ceiling.process (L, R, n);
        double lr = 0, ll = 0, rr = 0;
        for (int i = 0; i < n; ++i) { lr += (double) L[i] * R[i]; ll += (double) L[i] * L[i]; rr += (double) R[i] * R[i]; }
        const float a = juce::jmin (1.0f, (float) n / (float) (0.3 * sr));
        corrLR += a * ((float) lr / n - corrLR); corrLL += a * ((float) ll / n - corrLL); corrRR += a * ((float) rr / n - corrRR);
        corrLive.store (corrLL > 1.0e-12f && corrRR > 1.0e-12f ? corrLR / std::sqrt (corrLL * corrRR) : 1.0f, std::memory_order_relaxed);
        if (monoCheck.load (std::memory_order_relaxed)) for (int i = 0; i < n; ++i) { const float m = 0.5f * (L[i] + R[i]); L[i] = R[i] = m; }   // MONO CHECK
    }
    else if (! clean)
    {   // a STACK layer (CLEAN off): its own soft clip as before; with CLEAN on the parent's ceiling handles the sum
        constexpr float T = 0.891f, K = 1.0f - T;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
            {
                const float x = d[i], ax = std::abs (x);
                if (ax > T) d[i] = std::copysign (T + K * std::tanh ((ax - T) / K), x);
                else if (! (ax <= T)) d[i] = 0.0f;
            }
        }
    }
    else
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {   // (CLEAN layer: only the NaN guard)
            auto* d = buffer.getWritePointer (ch);
            for (int i = 0; i < n; ++i) if (! (std::abs (d[i]) < 1.0e9f)) d[i] = 0.0f;
        }
    tap (7, buffer);
    const float rms = buffer.getRMSLevel (0, 0, n);
    level.store (level.load() * 0.8f + juce::jmin (1.0f, rms * 4.0f) * 0.2f);

    // live modulation for the UI: the newest sounding voice
    const ProVoice* newest = nullptr; int active = 0;
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* v = static_cast<const ProVoice*> (synth.getVoice (i)); v->isVoiceActive())
        {
            ++active;
            if (newest == nullptr || v->startedAt > newest->startedAt) newest = v;
        }
    voicesLive.store (active);
    for (int s = 0; s < numSrc; ++s)
    {
        float v = newest != nullptr ? newest->modValue (s) : 0.0f;
        if (newest == nullptr && (s == srcLfo1 || s == srcLfo2)) v = lfoValue ((int) lfoShape[s - 1]->load(), ctl.lfoGlobal[s - 1], (juce::uint32) (s - 1) * 977u);
        if (newest == nullptr && s >= srcMac1 && s <= srcMac4) v = macro[s - srcMac1]->load();
        if (newest == nullptr && s == srcBend) v = laneNormSm;
        if (newest == nullptr && s == srcWheel) v = ctl.wheel;
        srcLive[s].store (v, std::memory_order_relaxed);
    }
    for (int s = 0; s < NUM_SLOTS; ++s)
        modLive[s].store (ctl.slotSrc[s] > 0 ? srcLive[juce::jlimit (0, numSrc - 1, ctl.slotSrc[s])].load (std::memory_order_relaxed) * ctl.slotAmt[s] : 0.0f, std::memory_order_relaxed);
}

// ------------------------------------------------------------------ bend lane
// Fills ctl.lane* for this block: the drawn curve read at the lane position of every sample, times RANGE and AMOUNT,
// smoothed per sample (steps become ~1.5 ms glides, so jumps never click). SYNC follows the host bar (free-running at
// the host / 120 BPM tempo while stopped); RETRIGGER restarts the lane on every note-on.
void ProProcessor::computeLane (int n, double bpm, double ppq, bool playing, double beatsPerBar, const juce::MidiBuffer& notes)
{
    bendShape.fetch (laneShape, laneSeen);   // keeps the last copy if the editor is mid-write
    const bool on = blOn->load() > 0.5f;
    bool modUsed = false;
    for (int s = 0; s < NUM_SLOTS; ++s) modUsed |= ctl.slotSrc[s] == srcBend && ctl.slotDst[s] > 0 && std::abs (ctl.slotAmt[s]) > 1.0e-6f;
    const bool pitchLive = on || std::abs (laneSm) > 1.0e-4f;
    ctl.lanePitch = pitchLive; ctl.laneMod = modUsed;
    const double sr = sampleRateNow.load();
    const double spb = 60.0 / juce::jlimit (20.0, 400.0, bpm > 1.0 ? bpm : 120.0) * sr;   // samples per beat
    const double lenBeats = bendlane::barsOf ((int) blLen->load()) * beatsPerBar;
    const bool retrig = (int) blMode->load() == 1;
    if (! pitchLive && ! modUsed)
    {
        ctl.laneLen = 0; laneSm = laneNormSm = 0;
        laneFreeBeats += n / spb; laneTrigBeats += n / spb;
        laneLivePos.store (-1.0f, std::memory_order_relaxed); laneLiveSemis.store (0.0f, std::memory_order_relaxed);
        return;
    }
    const int len = juce::jmin (n, (int) laneSemiBuf.size());
    const float range = bendlane::rangeOf ((int) blRng->load()), amt = juce::jlimit (0.0f, 1.0f, blAmt->load());
    const float a = 1.0f - (float) std::exp (-1.0 / (0.0015 * sr));
    if (playing && ! laneWasPlaying) laneFreeBeats = ppq;   // stopped -> playing: the free clock jumps to the host bar
    laneWasPlaying = playing;
    auto it = notes.begin(); int nextOn = n;
    auto findNext = [&] { nextOn = n; for (; it != notes.end(); ++it) { const auto m = (*it).getMessage(); if (m.isNoteOn()) { nextOn = (*it).samplePosition; ++it; break; } } };
    if (retrig) findNext();
    double phase = 0;
    for (int i = 0; i < len; ++i)
    {
        if (retrig) while (i >= nextOn) { laneTrigBeats = 0; findNext(); }
        const double beats = retrig ? laneTrigBeats + i / spb : (playing ? ppq + i / spb : laneFreeBeats + i / spb);
        phase = beats / lenBeats; phase -= std::floor (phase);
        const float v = laneShape.valueAt (phase) * amt;
        laneSm += ((on ? v * range : 0.0f) - laneSm) * a;
        laneNormSm += (v - laneNormSm) * a;
        laneSemiBuf[(size_t) i] = laneSm;
        laneRatioBuf[(size_t) i] = std::exp2 (laneSm * (1.0f / 12.0f));
        laneNormBuf[(size_t) i] = laneNormSm;
    }
    laneFreeBeats += n / spb; laneTrigBeats += n / spb;
    if (laneFreeBeats > 1.0e7) laneFreeBeats = std::fmod (laneFreeBeats, lenBeats);
    if (laneTrigBeats > 1.0e7) laneTrigBeats = std::fmod (laneTrigBeats, lenBeats);
    ctl.laneSemi = laneSemiBuf.data(); ctl.laneRatio = laneRatioBuf.data(); ctl.laneNorm = laneNormBuf.data(); ctl.laneLen = len;
    laneLivePos.store ((float) phase, std::memory_order_relaxed);
    laneLiveSemis.store (laneSm, std::memory_order_relaxed);
}

// ------------------------------------------------------------------ programs
#if GHOSTY_FACTORY_PRESETS
int ProProcessor::getNumPrograms() { return (int) factoryBank().size(); }
const juce::String ProProcessor::getProgramName (int i) { return juce::isPositiveAndBelow (i, (int) factoryBank().size()) ? factoryBank()[(size_t) i].name : juce::String(); }
#else
// FACTORY PRESETS OFF: the host's programs are INIT (0) + your saved sounds (1..n, the list taken at start / after a save or a
// BROWSE refresh). An index this build doesn't have (an older version's factory program) loads INIT.
int ProProcessor::getNumPrograms() { const juce::ScopedLock sl (userProgLock); return 1 + userPrograms.size(); }
const juce::String ProProcessor::getProgramName (int i)
{
    if (i == 0) return "Init";
    const juce::ScopedLock sl (userProgLock);
    return juce::isPositiveAndBelow (i - 1, userPrograms.size()) ? userPrograms.getReference (i - 1).getFileNameWithoutExtension() : juce::String();
}
#endif
void ProProcessor::refreshUserPrograms()
{   // (looks without creating the sounds folder: a host's plugin scan makes instances too)
    const auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Ghosty").getChildFile ("Ghosty Pro");
    auto l = dir.isDirectory() ? listUserPresets() : juce::Array<juce::File>();
    const juce::ScopedLock sl (userProgLock);
    userPrograms = std::move (l);
}
int ProProcessor::userProgramOf (const juce::String& name) const
{
    const juce::ScopedLock sl (userProgLock);
    for (int i = 0; i < userPrograms.size(); ++i) if (userPrograms.getReference (i).getFileNameWithoutExtension() == name) return i + 1;
    return 0;
}

void ProProcessor::setParam (const juce::String& id, float v)
{
    if (auto* prm = apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

void ProProcessor::resetSoundParams()
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto id = rp->getParameterID();
            if (id == "key" || id == "scale" || id == "chord" || id == "bypass" || id.startsWith ("fx_")) continue;
            if (id == "q_render" || id == "q_lock" || (id == "quality" && qLock != nullptr && qLock->load() > 0.5f)) continue;   // session settings; a locked QUALITY stays
            rp->setValueNotifyingHost (rp->getDefaultValue());
        }
}

// Scenes leave the GHOST LIMITER alone (a user's master limiter survives browsing); a factory sound is a complete sound, so it
// starts from the limiter's defaults and its spec / "cleanlimit" scene set what it needs.
void ProProcessor::resetMasterLimiter()
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (rp->getParameterID().startsWith ("fx_lim_")) rp->setValueNotifyingHost (rp->getDefaultValue());
}

void ProProcessor::setCurrentProgram (int index)
{
#if GHOSTY_FACTORY_PRESETS
    if (snapshots != nullptr) snapshots->clearCurrent();
    const auto& bank = factoryBank();
    if (! juce::isPositiveAndBelow (index, (int) bank.size())) return;
    currentProgram = index;
    loadFactorySound (bank[(size_t) index]);
#else
    juce::File f;
    { const juce::ScopedLock sl (userProgLock); if (juce::isPositiveAndBelow (index - 1, userPrograms.size())) f = userPrograms.getReference (index - 1); }
    if (index >= 1 && f != juce::File() && loadUserPreset (f)) { currentProgram = index; return; }
    initPatch();                                  // 0, or an index this build doesn't have (older versions' factory programs): INIT
#endif
}

// a factory sound (the factory bank's program, or a STACK layer's sound by name: factoryLibrary())
void ProProcessor::loadFactorySound (const gpro::FactoryPreset& fp)
{
    resetSoundParams();
    for (int k = 1; k < NUM_LAYERS; ++k) setLayerSound (k, {});
    resetMasterLimiter();
    // SOUND ENGINE: a factory sound starts from the V2 laws (default) or the CLASSIC ones; its spec (or its V2 tuning) may still pin a law
    const int eng = soundEngine.load();
    for (auto* id : legacyLawParams()) if (! (juce::String (id) == "quality" && qLock->load() > 0.5f)) setParam (id, engineLawValue (id, eng));
    modr.clear();                                 // MOD (round 3): factory sounds have no router connections
    juce::String v2Pre, v2Post;   // V2 re-voicing: synth tokens with the spec, FX-rack tokens after the scene (a scene sets the whole rack)
    if (eng == engineV2)
    {
        juce::StringArray tk; tk.addTokens (factoryV2Spec (fp.name), " ", ""); for (auto& t : tk) if (t.isNotEmpty()) (t.startsWith ("fx_") ? v2Post : v2Pre) << t << " ";
        juce::StringArray own; own.addTokens (fp.spec, " ", ""); for (auto& t : own) if (t.startsWith ("fx_")) v2Post << t << " ";   // (a spec's own rack tokens - HYPER - outlive its scene too)
    }
    applySpec (*this, v2Pre.isEmpty() ? fp.spec : fp.spec + " " + v2Pre);   // (one call: applySpec also sets the MACRO map)
    setParam ("out_trim", factoryTrimDbFor (fp.name, eng));       // level matching: the TRIM knob (VOLUME stays at 0 dB)
    setParam ("cl_on", factoryCleanOnFor (fp.name, eng) ? 1.0f : 0.0f);   // CLEAN: the factory sounds that clipped before (+ V2 tuning)
    rack.applyScene (apvts, (fp.scene.isEmpty() || isLayer) ? juce::String ("dry") : fp.scene);   // a layer's FX come from the parent rack
    if (v2Post.isNotEmpty() && ! isLayer) applyParams (*this, v2Post);
    {
        const juce::SpinLock::ScopedLockType sl (reqLock);
        patchIsUser = false; currentPatchName = {}; scene = fp.scene;
    }
    const int sa = (int) apvts.getRawParameterValue ("waveA")->load(), sb = (int) apvts.getRawParameterValue ("waveB")->load();
    needTables = true;
    postFrames (ghosty::makeShape (sa), ghosty::makeShape (sb), sa, sb);
}

void ProProcessor::initPatch()
{
    if (snapshots != nullptr) snapshots->clearCurrent();
#if ! GHOSTY_FACTORY_PRESETS
    currentProgram = 0;                           // FACTORY PRESETS OFF: program 0 is INIT
#endif
    resetSoundParams();
    for (int k = 1; k < NUM_LAYERS; ++k) setLayerSound (k, {});
    resetMasterLimiter();
    setParam ("out_trim", 0.0f); setParam ("cl_on", 1.0f);   // a new sound: CLEAN on
    // SOUND ENGINE V2: INIT is the new engine (the law defaults: drive Inside, unison V2, Ghost reverb, High) with a warm, playable start:
    // the filter a little closed with a short envelope sweep, a softer release; the Ghost reverb is set up (pre-delay, low cut) but off
    applyParams (*this, "cutoff=.8 res=.1 fenv=.18 f_d=.55 vel_f=.15 release=.3 verb=0 verb_pre=18 verb_lo=220");
    macros.setMap ({});
    macros.syncIdle();
    modr.clear();                                 // MOD (round 3): INIT clears the connections
    bendShape.set (bendlane::defaultShape());
    rack.applyScene (apvts, "dry");
    {
        const juce::SpinLock::ScopedLockType sl (reqLock);
        patchIsUser = false; currentPatchName = "Init"; scene = {};
    }
    needTables = true;
    postFrames (ghosty::makeShape (2), ghosty::makeShape (3), 2, 3);
}

juce::String ProProcessor::currentScene() const { const juce::SpinLock::ScopedLockType sl (reqLock); return scene; }

// ------------------------------------------------------------------ state
static juce::String frameToString (const ghosty::Frame& f) { juce::MemoryBlock mb (f.data(), sizeof (float) * ghosty::FRAME); return mb.toBase64Encoding(); }
static bool frameFromString (const juce::String& s, ghosty::Frame& f)
{
    juce::MemoryBlock mb; if (! mb.fromBase64Encoding (s) || mb.getSize() != sizeof (float) * ghosty::FRAME) return false;
    ghosty::Frame t; std::memcpy (t.data(), mb.getData(), mb.getSize());
    for (auto v : t) if (! std::isfinite (v)) return false;
    f = t; return true;
}
static juce::String sampleToString (const std::vector<HiFrame>& frames)
{
    juce::MemoryOutputStream mo;
    for (auto& f : frames) for (int i = 0; i < TBL; ++i) mo.writeShort ((short) juce::jlimit (-32767, 32767, (int) std::lround (f[(size_t) i % f.size()] * 32767.0f)));
    return mo.getMemoryBlock().toBase64Encoding();
}
static std::vector<HiFrame> sampleFromString (const juce::String& s, int count)
{
    std::vector<HiFrame> out;
    juce::MemoryBlock mb;
    if (count <= 0 || count > MAX_FRAMES || ! mb.fromBase64Encoding (s) || (int) mb.getSize() != count * TBL * 2) return out;
    juce::MemoryInputStream mi (mb, false);
    for (int k = 0; k < count; ++k) { HiFrame f ((size_t) TBL); for (int i = 0; i < TBL; ++i) f[(size_t) i] = mi.readShort() / 32767.0f; out.push_back (std::move (f)); }
    return out;
}

juce::ValueTree ProProcessor::makeStateTree()
{
    auto state = apvts.copyState();
    ghosty::Frame a, b; juce::String name, sc; bool user; std::vector<HiFrame> smp; juce::String smpName; gpro::wt::Doc ut;
    {
        const juce::SpinLock::ScopedLockType sl (reqLock);
        a = request.has ? request.a : savedA;
        b = request.has ? request.b : savedB;
        smp = request.hasSample ? request.sample : savedSample;
        smpName = request.hasSample ? request.sampleName : savedSampleName;
        ut = request.hasUser ? request.user : savedUser;
        name = currentPatchName; user = patchIsUser; sc = scene;
    }
    state.setProperty ("frameA", frameToString (a), nullptr);
    state.setProperty ("frameB", frameToString (b), nullptr);
    state.setProperty ("program", currentProgram, nullptr);
    state.setProperty ("patchName", name, nullptr);
    state.setProperty ("patchUser", user, nullptr);
    state.setProperty ("scene", sc, nullptr);
    state.setProperty ("ghostyProVersion", 1, nullptr);
    if (! smp.empty())
    {
        juce::ValueTree s ("SAMPLE");
        s.setProperty ("name", smpName, nullptr); s.setProperty ("frames", (int) smp.size(), nullptr); s.setProperty ("data", sampleToString (smp), nullptr);
        state.appendChild (s, nullptr);
    }
    if (ut.valid())
    {   // the WAVETABLE EDITOR's table, float32 so a saved state plays back the exact frames (<= 512 KB before base64)
        juce::ValueTree u ("USERTABLE");
        u.setProperty ("name", ut.name, nullptr); u.setProperty ("frames", (int) ut.frames.size(), nullptr); u.setProperty ("cycleLen", ut.cycleLen, nullptr);
        juce::String keys; for (int k : ut.keys) keys << k << " ";
        u.setProperty ("keys", keys.trim(), nullptr); u.setProperty ("data", gpro::wt::framesToBase64 (ut.frames), nullptr);
        if (ut.made) u.setProperty ("made", true, nullptr);   // provenance: made in the WAVETABLE EDITOR (absent = imported); the demo keeps it off disk
        state.appendChild (u, nullptr);
    }
    juce::ValueTree fxs ("FXRACK"); fxs.appendChild (rack.saveState(), nullptr);
    state.appendChild (fxs, nullptr);
    // BEND LANE and TRAP parameters: only written when used, so a sound without them saves exactly as before
    for (auto* p : getParameters())
        if (auto* prm = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto id = prm->getParameterID();
            if (! id.startsWith ("bl_") && ! isTrapParam (id) && ! id.startsWith ("fx_fatten_") && ! gpro::ModRouter::isModParam (id)) continue;   // (+ FATTEN, added 2026-10-23; + MOD round 3)
            auto c = state.getChildWithProperty ("id", id);
            if (c.isValid() && std::abs ((float) c.getProperty ("value") - prm->convertFrom0to1 (prm->getDefaultValue())) < 1.0e-6f) state.removeChild (c, nullptr);
        }
    for (int k = 1; k < NUM_LAYERS; ++k)
    {
        const auto nm = layerSound (k);
        if (nm.isNotEmpty()) { state.setProperty ("ly" + juce::String (k + 1) + "_sound", nm, nullptr); state.setProperty ("ly" + juce::String (k + 1) + "_sound_id", soundIdOf (nm), nullptr); }
        if (nm.isNotEmpty() && layerEngine (k) == engineV2) state.setProperty ("ly" + juce::String (k + 1) + "_eng", "v2", nullptr);   // SOUND ENGINE of the layer (absent = CLASSIC: older states)
    }
    const float trimDb = trimDbOf (trimRaw->load());       // (the raw value: the shadow only follows inside processBlock)
    if (std::abs (trimDb) > 1.0e-4f) state.setProperty ("trimDb", trimDb, nullptr);        // (the TRIM knob, also for older builds: they read this property)
    if (auto smp2 = trapSample(); smp2 != nullptr && smp2->length() > 0)
    {
        // FLAC (16-bit, encoded once at import / load) instead of raw PCM: a third of the size in every undo step and user sound
        juce::ValueTree t ("TRAPSAMPLE");
        t.setProperty ("name", smp2->name, nullptr); t.setProperty ("rate", smp2->rate, nullptr); t.setProperty ("root", smp2->root, nullptr); t.setProperty ("cents", smp2->rootCents, nullptr);
        t.setProperty ("chans", smp2->chans, nullptr); t.setProperty ("len", smp2->length(), nullptr); t.setProperty ("path", smp2->path, nullptr);
        t.setProperty ("flac", smp2->encoded.toBase64Encoding(), nullptr);
        state.appendChild (t, nullptr);
    }
    if (macros.hasMap()) state.appendChild (macros.getMap().toTree(), nullptr);   // MACROS: only written when the sound has them
    if (auto mt = modr.toTree(); mt.isValid()) state.appendChild (mt, nullptr);   // MOD (round 3): the router's connections (only when there are some)
    const auto lane = bendShape.get();
    if (! lane.sameAs (bendlane::defaultShape()))
    {
        juce::ValueTree b ("BENDLANE");
        b.setProperty ("points", lane.toString(), nullptr); b.setProperty ("snap", lane.snap, nullptr); b.setProperty ("grid", lane.grid, nullptr);
        state.appendChild (b, nullptr);
    }
    return state;
}

void ProProcessor::applyStateTree (juce::ValueTree in, bool morphLight)
{
    if (! in.isValid() || ! in.hasType (apvts.state.getType())) return;
    auto state = in.createCopy();
    const auto smp = state.getChildWithName ("SAMPLE");
    const auto fxs = state.getChildWithName ("FXRACK");
    const auto lane = state.getChildWithName ("BENDLANE");
    const auto ts = state.getChildWithName ("TRAPSAMPLE");
    const auto ut = state.getChildWithName ("USERTABLE");
    const auto mcs = state.getChildWithName ("MACROS");
    state.removeChild (mcs, nullptr);
    {   // MOD (round 3): the router's connections (absent = none: older states and every factory sound)
        const auto mods = state.getChildWithName ("MODS"); state.removeChild (mods, nullptr);
        const auto mx = mods.isValid() ? mods.toXmlString() : juce::String();
        if (! morphLight || mx != lastModXml) modr.fromTree (mods, true);
        lastModXml = mx;
    }
    {
        const auto mx = mcs.isValid() ? mcs.toXmlString() : juce::String();
        if (! morphLight || mx != lastMacroXml) macros.setMap (gpro::MacroMap::fromTree (mcs));       // absent (older states / sounds without macros) = no map
        lastMacroXml = mx;
    }
    state.removeChild (smp, nullptr); state.removeChild (fxs, nullptr); state.removeChild (lane, nullptr); state.removeChild (ts, nullptr); state.removeChild (ut, nullptr);
    for (int k = 1; k < NUM_LAYERS; ++k)
    {
        // the name first; if that sound was renamed since, the stable "category/index" id
        auto nm = state.getProperty ("ly" + juce::String (k + 1) + "_sound", "").toString();
        const int eng = state.getProperty ("ly" + juce::String (k + 1) + "_eng", "").toString() == "v2" ? engineV2 : engineClassic;   // a layer plays with the laws it was saved with
        if (morphLight && nm == layerSound (k) && (nm.isEmpty() || (childLive[k].load() != nullptr && layerEngine (k) == eng))) continue;   // MORPH: already playing it
        if (nm.isNotEmpty() && ! setLayerSound (k, nm, eng)) nm = nameOfSoundId (state.getProperty ("ly" + juce::String (k + 1) + "_sound_id", "").toString());
        setLayerSound (k, nm, eng);
    }
    {   // migration: older states carry the trim as a hidden property and have no CLEAN switch (they load with CLEAN OFF: same sound)
        auto addParam = [&state] (const char* id, float v) { juce::ValueTree c ("PARAM"); c.setProperty ("id", id, nullptr); c.setProperty ("value", v, nullptr); state.appendChild (c, nullptr); };
        if (! state.getChildWithProperty ("id", "out_trim").isValid()) addParam ("out_trim", (float) state.getProperty ("trimDb", 0.0f));
        if (! state.getChildWithProperty ("id", "cl_on").isValid()) addParam ("cl_on", 0.0f);
        for (auto* id : legacyLawParams()) if (! state.getChildWithProperty ("id", id).isValid()) addParam (id, 0.0f);   // round 2: older states keep the CLASSIC laws
        // QUALITY session settings never come from a sound; a locked quality stays as it is
        // (set in place, not removed + re-added: the saved state keeps its order, so a save -> load -> save round trip is byte-identical)
        auto keep = [&] (const char* id, float v) { auto ch = state.getChildWithProperty ("id", id); if (ch.isValid()) ch.setProperty ("value", v, nullptr); else addParam (id, v); };
        for (auto* id : { "q_render", "q_lock" }) keep (id, apvts.getRawParameterValue (id)->load());
        if (qLock->load() > 0.5f) keep ("quality", qualityP->load());
    }
    bool sampleSame = false;
    if (morphLight)
    {   // MORPH: the same sample is not reloaded (a reload cuts its notes)
        auto held = trapSample();
        if (! ts.isValid()) sampleSame = held == nullptr;
        else if (held != nullptr && ts.hasProperty ("flac") && held->name == ts.getProperty ("name").toString())
        { juce::MemoryBlock mb; sampleSame = mb.fromBase64Encoding (ts.getProperty ("flac").toString()) && mb == held->encoded; }
    }
    if (sampleSame) {}
    else if (ts.isValid())
    {
        auto d = std::make_unique<SampleData>();
        d->name = ts.getProperty ("name").toString(); d->path = ts.getProperty ("path", "").toString(); d->rate = juce::jlimit (8000.0, 192000.0, (double) ts.getProperty ("rate", 48000.0));
        d->root = juce::jlimit (0, 127, (int) ts.getProperty ("root", 60)); d->rootCents = juce::jlimit (-100.0f, 100.0f, (float) ts.getProperty ("cents", 0.0f));
        const int len = (int) ts.getProperty ("len", 0);
        juce::MemoryBlock mb; bool ok = false;
        if (ts.hasProperty ("flac") && mb.fromBase64Encoding (ts.getProperty ("flac").toString())) ok = decodeSampleFlac (mb, *d);
        else if (len > 0 && len <= (int) (MAX_SAMPLE_SECS * 192000.0) && mb.fromBase64Encoding (ts.getProperty ("data").toString()) && (int) mb.getSize() == len * 2)
        {
            juce::MemoryInputStream mi (mb, false);              // the first TRAP builds: raw 16-bit mono
            d->chans = 1; d->fwd[0].resize ((size_t) len); for (auto& v : d->fwd[0]) v = mi.readShort() / 32767.0f;
            ok = true;
        }
        if (ok)
        {
            for (int c = 0; c < d->chans; ++c) d->rev[c].assign (d->fwd[c].rbegin(), d->fwd[c].rend());
            if (d->encoded.getSize() == 0) d->encoded = encodeSampleFlac (*d);
            publishSample (std::move (d));
        }
        else clearTrapSample();
    }
    else clearTrapSample();
    {
        const auto shape = lane.isValid() ? bendlane::Shape::fromString (lane.getProperty ("points").toString(), (int) lane.getProperty ("snap", 1), (int) lane.getProperty ("grid", 2))
                                          : bendlane::defaultShape();
        if (! morphLight || ! shape.sameAs (bendShape.get())) bendShape.set (shape);
    }
    // anything this version doesn't find (older or newer state) gets its default, never whatever happened to be set
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (! state.getChildWithProperty ("id", rp->getParameterID()).isValid())
            {
                juce::ValueTree c ("PARAM");
                c.setProperty ("id", rp->getParameterID(), nullptr);
                c.setProperty ("value", rp->convertFrom0to1 (rp->getDefaultValue()), nullptr);
                state.appendChild (c, nullptr);
            }
    apvts.replaceState (state);
    {
        const auto rx = fxs.isValid() && fxs.getNumChildren() > 0 ? fxs.getChild (0).toXmlString() : juce::String();
        if (rx.isNotEmpty() && (! morphLight || rx != lastRackXml)) rack.loadState (fxs.getChild (0));
        lastRackXml = rx;
    }
    if (! morphLight) macros.syncIdle();
    const int sa = (int) apvts.getRawParameterValue ("waveA")->load(), sb = (int) apvts.getRawParameterValue ("waveB")->load();
    ghosty::Frame a = ghosty::makeShape (sa), b = ghosty::makeShape (sb);
    frameFromString (state.getProperty ("frameA").toString(), a);
    frameFromString (state.getProperty ("frameB").toString(), b);
#if GHOSTY_FACTORY_PRESETS
    currentProgram = juce::jlimit (0, juce::jmax (0, getNumPrograms() - 1), (int) state.getProperty ("program", 0));
#else
    currentProgram = (bool) state.getProperty ("patchUser", false) ? userProgramOf (state.getProperty ("patchName", "").toString()) : 0;
    if (! (bool) state.getProperty ("patchUser", false) && state.getProperty ("patchName", "").toString().isEmpty())
    {   // a factory sound from an older version (the state has every value: it plays as it did): show its old name
        const auto& lib = factoryLibrary(); const int old = (int) state.getProperty ("program", -1);
        state.setProperty ("patchName", juce::isPositiveAndBelow (old, (int) lib.size()) ? lib[(size_t) old].name : juce::String ("Saved sound"), nullptr);
    }
#endif
    auto newSample = smp.isValid() ? sampleFromString (smp.getProperty ("data").toString(), (int) smp.getProperty ("frames", 0)) : std::vector<HiFrame>();
    gpro::wt::Doc newUser;
    if (ut.isValid())
    {
        newUser.name = ut.getProperty ("name", "User").toString(); newUser.cycleLen = (int) ut.getProperty ("cycleLen", 0);
        for (auto& k : juce::StringArray::fromTokens (ut.getProperty ("keys", "").toString(), " ", "")) if (k.isNotEmpty()) newUser.keys.push_back (k.getIntValue());
        newUser.frames = gpro::wt::framesFromBase64 (ut.getProperty ("data").toString(), (int) ut.getProperty ("frames", 0));
        newUser.made = (bool) ut.getProperty ("made", false);
        if (! newUser.valid()) newUser = gpro::wt::Doc();
    }
    bool wavesSame = false;
    {
        const juce::SpinLock::ScopedLockType sl (reqLock);
        currentPatchName = state.getProperty ("patchName", "").toString();
        patchIsUser = (bool) state.getProperty ("patchUser", false);
        scene = state.getProperty ("scene", "").toString();
        if (morphLight)
        {   // MORPH: the same drawn waves / sample / tables -> no rebuild (a rebuild swaps the tables)
            const auto& ca = request.has ? request.a : savedA; const auto& cb = request.has ? request.b : savedB;
            const auto& cs = request.hasSample ? request.sample : savedSample;
            const auto& cu = request.hasUser ? request.user : savedUser;
            wavesSame = ca == a && cb == b && cs == newSample && cu.frames == newUser.frames && frameShapeA == sa && frameShapeB == sb;
        }
        if (! wavesSame)
        {
            request.hasSample = true;
            request.sample = std::move (newSample);
            request.sampleName = smp.isValid() ? smp.getProperty ("name").toString() : juce::String();
            request.hasUser = true;
            request.user = std::move (newUser);
        }
    }
    if (wavesSame) return;
    needTables = true;
    postFrames (a, b, sa, sb);
}

void ProProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto tree = makeStateTree();
    // the look (never in a sound); GHOST LOOK: index 3 is saved as "ghost" (an older build reads that as BLACK)
    tree.setProperty ("theme_name", uiTheme.load() == 3 ? juce::String ("ghost") : ghosty::themes::toString (ghosty::themes::fromInt (uiTheme.load())), nullptr);
    if (! morphBoard.ghosts.empty() || morph.enabled.load()) tree.appendChild (morphTree(), nullptr);   // the MORPH BOARD (session only, never in a sound)
    if (auto lt = modr.learnTree(); lt.isValid()) tree.appendChild (lt, nullptr);   // MOD (round 3): MIDI LEARN (session only)
    leaveMadeTablesOut (tree, false);   // demo: no plugin-made wavetable in the project (no hint: the host saves)
    if (auto xml = tree.createXml()) copyXmlToBinary (*xml, dest);
}

void ProProcessor::setStateInformation (const void* data, int size)
{
    ghosty::StateLoadNotify notifyHost (*this);   // CLAP: tell the host the values changed (Shared/HostNotify.h)
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr) return;
    auto tree = juce::ValueTree::fromXml (*xml);
    auto morphState = tree.getChildWithName ("MORPHBOARD"); tree.removeChild (morphState, nullptr);   // the MORPH BOARD: session only
    { auto lt = tree.getChildWithName ("MIDILEARN"); tree.removeChild (lt, nullptr); modr.loadLearnTree (lt); }   // MOD (round 3): MIDI LEARN (session only)
    // the look comes back even in demo mode: 'theme_name', or the older LIGHT switch ("uiTheme" = "light" -> LIGHT, none -> BLACK)
    // GHOST LOOK: "ghost" -> 3; anything unknown -> BLACK, as before
    if (tree.hasProperty ("theme_name")) { const auto tn = tree.getProperty ("theme_name").toString().trim().toLowerCase();
                                           uiTheme.store (tn == "ghost" || tn == "3" ? 3 : ghosty::themes::toInt (ghosty::themes::fromString (tn))); }
    else uiTheme.store (ghosty::themes::toInt (ghosty::themes::fromLegacyBool (tree.getProperty ("uiTheme", "").toString() == "light")));
    if (! license.isUnlocked()) return;   // demo mode: a reopened project starts at the default sound (unlocking brings the saved sound back)
    applyStateTree (tree);
    {   // a host session brings back its QUALITY session settings too (a sound load never sets them: see applyStateTree)
        auto saved = [&tree] (const char* id) { return tree.getChildWithProperty ("id", id); };
        for (auto* id : { "q_render", "q_lock" }) if (auto c = saved (id); c.isValid()) setParam (id, (float) c.getProperty ("value"));
        if (qLock->load() > 0.5f) if (auto c = saved ("quality"); c.isValid()) setParam ("quality", (float) c.getProperty ("value"));
    }
    loadMorphTree (morphState);         // absent = an empty board, off
}

// ------------------------------------------------------------------ TRAP: stack layers + sample
bool ProProcessor::isTrapParam (const juce::String& id)
{
    return id.startsWith ("br_") || id.startsWith ("ch_") || id.startsWith ("tp_") || id.startsWith ("ly") || id.startsWith ("stk_") || id.startsWith ("smp_");
}

bool ProProcessor::setLayerSound (int layer, const juce::String& name, int engine)
{
    if (isLayer || layer < 1 || layer >= NUM_LAYERS) return false;
    if (name.isEmpty())
    {
        const juce::SpinLock::ScopedLockType sl (reqLock);
        layerName[layer] = {};
        return true;
    }
    const auto& bank = factoryLibrary();          // (FACTORY PRESETS OFF: the hidden library, so older projects keep their layers)
    int idx = -1; for (int i = 0; i < (int) bank.size(); ++i) if (bank[(size_t) i].name == name) idx = i;
    if (idx < 0 || child[layer] == nullptr) return false;
    const int eng = engine < 0 ? soundEngine.load() : (engine == engineClassic ? engineClassic : engineV2);
    child[layer]->setSoundEngine (eng);
    if (child[layer]->snapshots != nullptr) child[layer]->snapshots->clearCurrent();
    child[layer]->currentProgram = idx;
    child[layer]->loadFactorySound (bank[(size_t) idx]);
    { const juce::SpinLock::ScopedLockType sl (reqLock); layerName[layer] = name; layerEng[layer] = eng; }
    childLive[layer].store (child[layer].get(), std::memory_order_release);
    if (ly[layer].src != nullptr && (int) ly[layer].src->load() != lsSound) setParam ("ly" + juce::String (layer + 1) + "_src", (float) lsSound);
    return true;
}

int ProProcessor::layerEngine (int layer) const
{
    if (layer < 1 || layer >= NUM_LAYERS) return engineClassic;
    const juce::SpinLock::ScopedLockType sl (reqLock);
    return layerEng[layer];
}

juce::String ProProcessor::layerSound (int layer) const
{
    if (layer < 1 || layer >= NUM_LAYERS) return {};
    const juce::SpinLock::ScopedLockType sl (reqLock);
    return layerName[layer];
}

std::shared_ptr<const SampleData> ProProcessor::trapSample() const { const juce::SpinLock::ScopedLockType sl (reqLock); return sampleHeld; }

void ProProcessor::publishSample (std::unique_ptr<SampleData> d)
{
    std::shared_ptr<const SampleData> held = std::move (d);
    { const juce::SpinLock::ScopedLockType sl (reqLock); sampleHeld = held; }
    auto slot = std::make_unique<SampleSlot>(); slot->data = held; sampleBox.post (std::move (slot));
}

void ProProcessor::clearTrapSample() { publishSample (nullptr); }

juce::MemoryBlock ProProcessor::encodeSampleFlac (const SampleData& d)
{
    juce::MemoryBlock out;
    juce::FlacAudioFormat flac;
    std::unique_ptr<juce::AudioFormatWriter> w (flac.createWriterFor (new juce::MemoryOutputStream (out, false), d.rate, (unsigned int) d.chans, 16, {}, 5));   // NOT-EXPORT: the SAMPLE layer's own copy in the project state (memory, never a file)
    if (w == nullptr) return {};
    juce::AudioBuffer<float> b (d.chans, d.length());
    for (int c = 0; c < d.chans; ++c) b.copyFrom (c, 0, d.fwd[c].data(), d.length());
    w->writeFromAudioSampleBuffer (b, 0, d.length());
    w.reset();                                   // finishes the stream (and the STREAMINFO header) into 'out'
    return out;
}

bool ProProcessor::decodeSampleFlac (const juce::MemoryBlock& mb, SampleData& d)
{
    juce::FlacAudioFormat flac;
    std::unique_ptr<juce::AudioFormatReader> rd (flac.createReaderFor (new juce::MemoryInputStream (mb, false), true));
    if (rd == nullptr || rd->numChannels < 1 || rd->lengthInSamples < 8 || rd->lengthInSamples > (juce::int64) (MAX_SAMPLE_SECS * 192000.0)) return false;
    const int len = (int) rd->lengthInSamples; d.chans = juce::jmin (2, (int) rd->numChannels);
    juce::AudioBuffer<float> b ((int) rd->numChannels, len);
    rd->read (&b, 0, len, 0, true, true);
    for (int c = 0; c < d.chans; ++c) d.fwd[c].assign (b.getReadPointer (c), b.getReadPointer (c) + len);
    d.encoded = mb;
    return true;
}

juce::String ProProcessor::soundIdOf (const juce::String& name)
{
    const auto& bank = factoryLibrary();
    for (auto& f : bank)
        if (f.name == name) { int idx = 0; for (auto& g : bank) { if (&g == &f) break; if (g.cat == f.cat) ++idx; } return f.cat + "/" + juce::String (idx); }
    return {};
}

juce::String ProProcessor::nameOfSoundId (const juce::String& id)
{
    const int slash = id.lastIndexOfChar ('/');
    if (slash <= 0) return {};
    const auto cat = id.substring (0, slash); const int want = id.substring (slash + 1).getIntValue();
    int idx = 0;
    for (auto& f : factoryLibrary()) if (f.cat == cat) { if (idx == want) return f.name; ++idx; }
    return {};
}

void ProProcessor::audioReset()
{
    synth.allNotesOff (0, false);
    for (auto& lm : layerMidi) { lm.pend.clear(); lm.out.clear(); }
    fx.reset(); rack.reset(); character.reset();
    lastGain = -1.0f; ly0G[0] = ly0G[1] = -1.0f;
    ceiling.reset(); sumPockets.reset(); for (auto& pk : pocket) pk.reset(); charMatch.reset(); tapeMatch.reset();
}

// ------------------------------------------------------------------ envelope tracer
static juce::uint32 traceBits (float f) noexcept { juce::uint32 u; std::memcpy (&u, &f, 4); return u; }
static juce::uint32 traceMix (juce::uint32 h, juce::uint32 v) noexcept { return (h ^ v) * 16777619u + 0x9e3779b9u; }

void ProProcessor::publishTrace() noexcept
{
    const ProVoice* pick[kTraceVoices] {}; int n = 0;
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* v = static_cast<const ProVoice*> (synth.getVoice (i)); v->isVoiceActive())
        {
            if (n < kTraceVoices) { pick[n++] = v; continue; }
            int oldest = 0; for (int k = 1; k < n; ++k) if (pick[k]->startedAt < pick[oldest]->startedAt) oldest = k;
            if (v->startedAt > pick[oldest]->startedAt) pick[oldest] = v;
        }
    std::sort (pick, pick + n, [] (const ProVoice* x, const ProVoice* y) { return x->startedAt > y->startedAt; });   // newest first
    const juce::uint32 s0 = trace.seq.load (std::memory_order_relaxed);
    trace.seq.store (s0 + 1, std::memory_order_relaxed);          // odd: writing
    std::atomic_thread_fence (std::memory_order_release);
    juce::uint32 chk = 0;
    for (int i = 0; i < kTraceVoices; ++i)
    {
        auto& d = trace.v[i];
        const juce::uint32 age = i < n ? pick[i]->startedAt : 0; d.age.store (age, std::memory_order_relaxed); chk = traceMix (chk, age);
        for (int l = 0; l < 2; ++l) { const double ph = i < n ? pick[i]->lfoPhase (l) : 0.0; d.lfo[l].store ((float) (ph - std::floor (ph)), std::memory_order_relaxed); }
        for (int e = 0; e < kTraceEnvs; ++e)
        {
            int st = 0; float lin = 0, out = 0, a = 0, dd = 0, ss = 0, rr = 0;
            if (i < n) { const auto& env = pick[i]->envAt (e); st = (int) env.st; lin = env.lin; out = env.out; a = env.a; dd = env.d; ss = env.s; rr = env.r; }
            d.stage[e].store (st, std::memory_order_relaxed); d.lin[e].store (lin, std::memory_order_relaxed); d.out[e].store (out, std::memory_order_relaxed);
            d.a[e].store (a, std::memory_order_relaxed); d.d[e].store (dd, std::memory_order_relaxed); d.s[e].store (ss, std::memory_order_relaxed); d.r[e].store (rr, std::memory_order_relaxed);
            chk = traceMix (traceMix (traceMix (chk, (juce::uint32) st), traceBits (lin)), traceBits (out));
        }
    }
    for (int l = 0; l < 2; ++l) trace.lfoGlobal[l].store ((float) (ctl.lfoGlobal[l] - std::floor (ctl.lfoGlobal[l])), std::memory_order_relaxed);
    trace.count.store (n, std::memory_order_relaxed); trace.check.store (traceMix (chk, (juce::uint32) n), std::memory_order_relaxed);
    trace.seq.store (s0 + 2, std::memory_order_release);           // even: done
}

bool ProProcessor::readTrace (TraceSnap& out) const
{
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        const juce::uint32 s0 = trace.seq.load (std::memory_order_acquire);
        if (s0 == 0) { out.count = 0; return false; }
        if (s0 & 1u) continue;
        juce::uint32 chk = 0;
        for (int i = 0; i < kTraceVoices; ++i)
        {
            auto& d = out.v[i]; const auto& sv = trace.v[i];
            d.age = sv.age.load (std::memory_order_relaxed); chk = traceMix (chk, d.age);
            for (int l = 0; l < 2; ++l) d.lfo[l] = sv.lfo[l].load (std::memory_order_relaxed);
            for (int e = 0; e < kTraceEnvs; ++e)
            {
                d.stage[e] = sv.stage[e].load (std::memory_order_relaxed); d.lin[e] = sv.lin[e].load (std::memory_order_relaxed); d.out[e] = sv.out[e].load (std::memory_order_relaxed);
                d.a[e] = sv.a[e].load (std::memory_order_relaxed); d.d[e] = sv.d[e].load (std::memory_order_relaxed); d.s[e] = sv.s[e].load (std::memory_order_relaxed); d.r[e] = sv.r[e].load (std::memory_order_relaxed);
                chk = traceMix (traceMix (traceMix (chk, (juce::uint32) d.stage[e]), traceBits (d.lin[e])), traceBits (d.out[e]));
            }
        }
        out.count = trace.count.load (std::memory_order_relaxed);
        for (int l = 0; l < 2; ++l) out.lfoGlobal[l] = trace.lfoGlobal[l].load (std::memory_order_relaxed);
        const juce::uint32 chkW = trace.check.load (std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_acquire);
        if (trace.seq.load (std::memory_order_relaxed) == s0) { out.seq = s0; return traceMix (chk, (juce::uint32) out.count) == chkW; }
    }
    return false;
}

// BYPASS, once the 6 ms fade has reached silence (audio thread, allocation-free): every voice and tail is gone for good -- the
// synth voices incl. their declick tails, STACK layers 2/3 (their own voices + FX), the SAMPLE layer, the layer DELAY queues,
// the Ghost FX + rack tails, CHARACTER, the TAPE delay line and the arp -- so releasing BYPASS starts from silence
void ProProcessor::bypassReset()
{
    audioReset();
    for (int i = 0; i < synth.getNumVoices(); ++i) if (auto* v = dynamic_cast<ProVoice*> (synth.getVoice (i))) v->hardReset();
    for (int k = 1; k < NUM_LAYERS; ++k)
        if (auto* c = childLive[k].load (std::memory_order_acquire))
        {
            c->audioReset();                       // incl. rack.reset(): every delay / latency line of the layer's rack emptied
            for (int i = 0; i < c->synth.getNumVoices(); ++i) if (auto* v = dynamic_cast<ProVoice*> (c->synth.getVoice (i))) v->hardReset();
        }
    sampler.clear();
    tape.clear();                           // (audioReset above did rack.reset(): every rack delay / latency line emptied)
    performer.reset();
    for (auto& h : heldNotes) h.store (0, std::memory_order_relaxed);
}

bool ProProcessor::importTrapSample (const juce::File& file, juce::String& message)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (file));
    if (rd == nullptr) { message = "Couldn't read that file. Use a WAV, AIFF, FLAC or MP3."; return false; }
    const int len = (int) std::min<juce::int64> (rd->lengthInSamples, (juce::int64) (rd->sampleRate * MAX_SAMPLE_SECS));
    if (len < 64) { message = "That sample is too short."; return false; }
    juce::AudioBuffer<float> buf ((int) rd->numChannels, len);
    rd->read (&buf, 0, len, 0, true, true);
    auto d = std::make_unique<SampleData>();
    d->chans = juce::jmin (2, buf.getNumChannels());
    for (int c = 0; c < d->chans; ++c) d->fwd[c].assign (buf.getReadPointer (c), buf.getReadPointer (c) + len);
    float peak = 0; for (int c = 0; c < d->chans; ++c) for (auto v : d->fwd[c]) peak = std::max (peak, std::abs (v));
    if (peak < 1.0e-5f) { message = "That sample is silent."; return false; }
    for (int c = 0; c < d->chans; ++c) { for (auto& v : d->fwd[c]) v /= peak; d->rev[c].assign (d->fwd[c].rbegin(), d->fwd[c].rend()); }
    d->rate = rd->sampleRate; d->name = file.getFileNameWithoutExtension(); d->path = file.getFullPathName();
    std::vector<float> mono = d->fwd[0];
    if (d->chans > 1) for (int i = 0; i < len; ++i) mono[(size_t) i] = 0.5f * (mono[(size_t) i] + d->fwd[1][(size_t) i]);
    const double hz = yinPitch (mono, d->rate);
    if (hz > 0)
    {
        const double midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
        d->root = juce::jlimit (0, 127, (int) std::lround (midi));
        d->rootCents = (float) ((midi - d->root) * 100.0);
    }
    else { d->root = 60; d->rootCents = 0; }
    setParam ("smp_root", (float) d->root);
    d->encoded = encodeSampleFlac (*d);
    message = d->name + ": " + juce::String (len / d->rate, 1) + " s" + (d->chans > 1 ? " stereo" : "") + ", root " + juce::MidiMessage::getMidiNoteName (d->root, true, true, 4) + (hz > 0 ? "" : " (no clear pitch; set ROOT by ear)");
    publishSample (std::move (d));
    // make it playable at once: the first free layer becomes the sample layer; with both layers busy, layer 3 takes it
    int used = -1; for (int k = 1; k < NUM_LAYERS; ++k) if ((int) ly[k].src->load() == lsSample) used = k;
    if (used < 0) { for (int k = 1; k < NUM_LAYERS; ++k) if ((int) ly[k].src->load() == lsOff) { used = k; break; } }
    if (used < 0) { used = NUM_LAYERS - 1; message << " - on layer 3 (it was a sound)"; }
    else message << " - on layer " << (used + 1);
    setParam ("ly" + juce::String (used + 1) + "_src", (float) lsSample);
    return true;
}

// Layers 2 and 3 on top of the main sound (audio thread): a child processor playing another factory sound, or the sample.
void ProProcessor::renderLayers (juce::AudioBuffer<float>& buffer, int n, const juce::MidiBuffer& performed, juce::AudioPlayHead* head)
{
    const double sr = sampleRateNow.load();
    const auto* slot = sampleBox.acquire();
    const SampleData* smp = slot != nullptr ? slot->data.get() : nullptr;
    if (smp != samplePlaying) { sampler.allOff (-1, false); samplePlaying = smp; }     // the old data is going away: hard cut + declick
    SampleParams sp;
    sp.root = (int) smpRoot->load(); sp.fine = smpFine->load(); sp.formant = smpFormant->load() > 0.5f; sp.loop = smpLoop->load() > 0.5f;
    sp.start = juce::jlimit (0.0f, 1.0f, smpStart->load() + extMod[dSmpStart]); sp.end = juce::jlimit (sp.start + 0.001f, 1.0f, smpEnd->load());
    sp.a = smpA->load(); sp.d = smpD->load(); sp.s = smpS->load(); sp.r = smpR->load(); sp.revVel = smpRevVel->load();
    bool anySolo = false; for (int k = 0; k < NUM_LAYERS; ++k) anySolo |= layerSolo[k].load (std::memory_order_relaxed);
    for (int k = 1; k < NUM_LAYERS; ++k)
    {
        const int src = (int) ly[k].src->load();
        // a layer that stops being a sound is cut and cleared (voices, its FX tails), so nothing frozen comes back with it later;
        // a sample layer that goes away fades its notes out over 5 ms
        if (src != layerSrcWas[k])
        {
            if (auto* c = childLive[k].load (std::memory_order_acquire); c != nullptr && (layerSrcWas[k] == lsSound || src == lsSound)) c->audioReset();
            if (layerSrcWas[k] == lsSample) sampler.allOff (k, true);
            layerMidi[k].pend.clear();
            layerSrcWas[k] = src;
        }
        const bool clean = ctl.clean;
        // CLEAN: a hair of detune between the layers (+2 / -2 cents) so same-pitch layers never comb against each other
        const float semis = (float) ly[k].oct->load() * 12.0f + ly[k].det->load() * 0.01f + tapeSemisSm + (clean ? (k == 1 ? 0.02f : -0.02f) : 0.0f);
        const bool silent = layerMute[k].load (std::memory_order_relaxed) || (anySolo && ! layerSolo[k].load (std::memory_order_relaxed));
        const float lvl = silent ? 0.0f : juce::jlimit (0.0f, 1.0f, ly[k].lvl->load() + extMod[k == 1 ? dLy2Lvl : dLy3Lvl]), pan = juce::jlimit (-1.0f, 1.0f, ly[k].pan->load());
        sp.semis[k] = semis; sp.lvl[k] = lvl; sp.pan[k] = pan; sp.rev[k] = ly[k].rev->load() > 0.5f;
        const juce::int64 t0 = juce::Time::getHighResolutionTicks();
        int voices = 0, uniL = 0, uniM = 0;
        if (src == lsSound)
        {
            if (auto* c = childLive[k].load (std::memory_order_acquire))
            {
                layerMidi[k].run (performed, n, (int) (ly[k].dly->load() * sr), (int) ly[k].play->load(), (int) stkVSplit->load(), (int) stkKSplit->load());
                c->externalSemis.store (semis); c->externalReverse.store (sp.rev[k]); c->externalFx.store (ly[k].fx != nullptr && ly[k].fx->load() > 0.5f);
                c->externalClean.store (clean); c->externalQ.store (ctl.engineQ);
                { float mv[gpro::NUM_MACROS]; macros.macroValues (mv); c->macros.setExternal (mv, macros.mapLive()); }   // the parent's 4 knobs ride the layer
                c->setPlayHead (head);
                layerBuf.setSize (2, n, false, false, true);
                c->processBlock (layerBuf, layerMidi[k].out);
                if (clean) pocket[k].process (layerBuf.getWritePointer (0), layerBuf.getWritePointer (1), n);    // CLEAN: this layer's pocket
#if GPRO_TAPS
                layerTap (k, layerBuf.getReadPointer (0), layerBuf.getReadPointer (1), n, lvl * juce::jmin (1.0f, 1.0f - pan), lvl * juce::jmin (1.0f, 1.0f + pan));
#endif
                const float gL = lvl * juce::jmin (1.0f, 1.0f - pan), gR = lvl * juce::jmin (1.0f, 1.0f + pan);
                float& wL = layerGainWas[k][0]; float& wR = layerGainWas[k][1];
                if (wL < 0.0f) { wL = gL; wR = gR; }
                buffer.addFromWithRamp (0, 0, layerBuf.getReadPointer (0), n, wL, gL);          // ramped: a level move (SIZE) never zips
                if (buffer.getNumChannels() > 1) buffer.addFromWithRamp (1, 0, layerBuf.getReadPointer (1), n, wR, gR);
                wL = gL; wR = gR;
                voices = c->voicesLive.load();
                uniL = c->uniVoicesLive.load(); uniM = c->uniVoicesMax.load();
            }
        }
        else if (src == lsSample)
        {
            layerMidi[k].run (performed, n, (int) (ly[k].dly->load() * sr), (int) ly[k].play->load(), (int) stkVSplit->load(), (int) stkKSplit->load());   // always: no event waits for the next sample
            if (smp != nullptr)
                for (const auto meta : layerMidi[k].out)
                {
                    const auto m = meta.getMessage();
                    if (m.isNoteOn()) sampler.noteOn (k, m.getNoteNumber(), m.getFloatVelocity(), sp, *smp, ++voiceStamp);
                    else if (m.isNoteOff()) sampler.noteOff (k, m.getNoteNumber());
                    else if (m.isAllNotesOff() || m.isAllSoundOff()) sampler.allOff (-1, true);
                }
            voices = sampler.active (k);
        }
        layerVoices[k].store (voices, std::memory_order_relaxed);
        uniLayerLive[k].store (src == lsSample ? voices : uniL, std::memory_order_relaxed); uniLayerMax[k].store (src == lsSample ? voices : uniM, std::memory_order_relaxed);
        const float pct = (float) (juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0) / (n / sr) * 100.0);
        cpuLive[k].store (cpuLive[k].load (std::memory_order_relaxed) * 0.9f + pct * 0.1f, std::memory_order_relaxed);
    }
    if (sampler.busy())
    {
        const juce::int64 t0 = juce::Time::getHighResolutionTicks();
        if (ctl.clean && buffer.getNumChannels() > 1 && smpBuf.getNumSamples() >= n)
        {   // CLEAN: the sample layer gets the pocket of the (first) layer that plays it
            int ks = 2; for (int k = 1; k < NUM_LAYERS; ++k) if ((int) ly[k].src->load() == lsSample) { ks = k; break; }
            smpBuf.clear (0, 0, n); smpBuf.clear (1, 0, n);
            juce::AudioBuffer<float> sb (smpBuf.getArrayOfWritePointers(), 2, n);
            sampler.render (sb, n, sp, smp);
            pocket[ks].process (smpBuf.getWritePointer (0), smpBuf.getWritePointer (1), n);
            buffer.addFrom (0, 0, smpBuf, 0, 0, n); buffer.addFrom (1, 0, smpBuf, 1, 0, n);
        }
        else
        sampler.render (buffer, n, sp, smp);
        const float pct = (float) (juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0) / (n / sr) * 100.0);
        for (int k = 1; k < NUM_LAYERS; ++k) if ((int) ly[k].src->load() == lsSample) cpuLive[k].store (cpuLive[k].load (std::memory_order_relaxed) + pct * 0.1f, std::memory_order_relaxed);
    }
}

// ------------------------------------------------------------------ CLEAN SOUND (audio thread)
// Which layer owns the bass (the one with clearly the most energy below 120 Hz, held ~80 ms before a switch), each layer's pocket
// (layer 2 leans one way, layer 3 the other, each a bit wider; the bass owner is centred and uncut), and the SIDE BUDGET.
void ProProcessor::cleanLayerParams (int n)
{
    const double sr = sampleRateNow.load();
    const float pw = juce::jlimit (0.0f, 1.0f, clWidth->load()), lc = juce::jlimit (30.0f, 300.0f, clLowCut->load());
    bool snd[NUM_LAYERS]; for (int k = 0; k < NUM_LAYERS; ++k) snd[k] = presence[k] > 0.5f;
    // what each layer brings to the mix: a sound layer's pocket sees it before its LEVEL (applied when it is added), the others after
    float low[NUM_LAYERS], all[NUM_LAYERS];
    for (int k = 0; k < NUM_LAYERS; ++k)
    {
        const float w = (k > 0 && (int) ly[k].src->load() == lsSound) ? layerLvl[k] * layerLvl[k] : 1.0f;
        low[k] = pocket[k].lowPow * w; all[k] = pocket[k].allPow * w;
    }
    int cand = bassOwner;
    if (! snd[bassOwner])
    {
        float best = -1; for (int k = 0; k < NUM_LAYERS; ++k) if (snd[k] && low[k] > best) { best = low[k]; cand = k; }
    }
    else
        for (int k = 0; k < NUM_LAYERS; ++k)
            if (k != bassOwner && snd[k] && low[k] > 2.0f * low[bassOwner] && low[k] > low[cand]) cand = k;
    if (cand != bassOwner) { ownerHold += n; if (! snd[bassOwner] || ownerHold > (int) (0.08 * sr)) { bassOwner = cand; ownerHold = 0; } }
    else ownerHold = 0;
    bassOwnerLive.store (bassOwner, std::memory_order_relaxed);
    static constexpr float leanOf[NUM_LAYERS] { 0.0f, -0.5f, 0.6f }, widthOf[NUM_LAYERS] { 0.0f, 0.4f, 0.6f };
    for (int k = 0; k < NUM_LAYERS; ++k)
    {
        auto& pk = pocket[k];
        pk.hpHz = lc;
        pk.hpAmt = k == bassOwner ? 0.0f : 1.0f;
        const bool centred = k == 0 || k == bassOwner;
        pk.lean = centred ? 0.0f : leanOf[k] * pw;
        pk.width = centred ? 1.0f : 1.0f + widthOf[k] * pw;
        pk.budget = 1.0f;
    }
    // SIDE BUDGET: two wide layers sounding at once -> the quieter one narrows until they separate
    const bool w1 = snd[1] && bassOwner != 1, w2 = snd[2] && bassOwner != 2;
    if (w1 && w2) pocket[all[1] < all[2] ? 1 : 2].budget = 1.0f - 0.8f * pw;
}

void ProProcessor::applyVoiceLaw (juce::AudioBuffer<float>& b, int n)
{   // voice-count aware headroom: 1-2 voices as they are, then -1.5 dB per doubling (a voice counts by its envelope level)
    float voices = 0;
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* v = static_cast<ProVoice*> (synth.getVoice (i)); v->isVoiceActive()) voices += juce::jmin (1.0f, v->ampLevel() * 2.5f);
    const float target = std::pow (2.0f / juce::jmax (2.0f, voices), 0.25f);      // -1.5 dB per doubling above two voices
    const float next = target < voiceG ? target : voiceG + (target - voiceG) * juce::jmin (1.0f, (float) n / (float) (0.3 * sampleRateNow.load()));
    if (voiceG != 1.0f || next != 1.0f) for (int ch = 0; ch < b.getNumChannels(); ++ch) b.applyGainRamp (ch, 0, n, voiceG, next);
    voiceG = next; voiceLawLive.store (next, std::memory_order_relaxed);
}

// auto-gain after a drive stage: preBuf holds the stage's input
void ProProcessor::matchStage (ghosty::clean::LevelMatch& m, juce::AudioBuffer<float>& b, int n, float lo, float hi)
{
    if (b.getNumChannels() < 2) return;
    auto* L = b.getWritePointer (0); auto* R = b.getWritePointer (1);
    const float* a = preBuf.getReadPointer (0); const float* c = preBuf.getReadPointer (1);
    for (int i = 0; i < n; ++i)
    {
        const float g = m.stepWeighted (a[i], c[i], L[i], R[i], lo, hi);
        L[i] *= g; R[i] *= g;
    }
}

float ProProcessor::TrimProxy::load() const noexcept { return juce::Decibels::decibelsToGain (trimDbOf (p.trimRaw->load())); }
void ProProcessor::TrimProxy::store (float gain) { p.setParam ("out_trim", juce::Decibels::gainToDecibels (gain, -24.0f)); }

namespace
{
struct CleanEntry { const char* name; float trimDb; };
const std::map<juce::String, float>& cleanFactory()
{   // measured (ProTests cliplist / cleantrims): these factory sounds hit the old output soft clip (peak above -1 dBFS) on the test phrase;
    // they load with CLEAN ON and the TRIM that keeps them as loud as before
    static const std::map<juce::String, float> m = []
    {
        static const CleanEntry list[] {
#if GHOSTY_FACTORY_DATA
#include "ProCleanList.inc"
#endif
            { nullptr, 0.0f } };
        std::map<juce::String, float> x;
        for (auto* e = list; e->name != nullptr; ++e) x[e->name] = e->trimDb;
        return x;
    }();
    return m;
}
}
bool ProProcessor::factoryCleanOn (const juce::String& name) { return cleanFactory().count (name) > 0; }
float ProProcessor::factoryTrimDb (const juce::String& name)
{
    auto it = cleanFactory().find (name);
    return it != cleanFactory().end() ? it->second : presetTrimDb (name);
}

// ------------------------------------------------------------------ SOUND ENGINE V2: the factory bank on the round-2 laws
namespace
{
struct V2Tune { const char* name; float trimDb; int clean; const char* spec; };   // trimDb: added to the CLASSIC trim; clean: -1 as CLASSIC, 0 off, 1 on
const std::map<juce::String, const V2Tune*>& v2Tunes()
{   // measured (ProTests v2audit): each factory sound under V2 vs its CLASSIC render - loudness within 1 dB, true peak under the ceiling
    static const V2Tune list[] {
#if GHOSTY_FACTORY_DATA
#include "ProV2Tune.inc"
#endif
        { nullptr, 0.0f, -1, "" } };
    static const std::map<juce::String, const V2Tune*> m = [] { std::map<juce::String, const V2Tune*> x; for (auto* e = list; e->name != nullptr; ++e) x[e->name] = e; return x; }();
    return m;
}
const V2Tune* v2TuneOf (const juce::String& name) { auto it = v2Tunes().find (name); return it != v2Tunes().end() ? it->second : nullptr; }
}
float ProProcessor::engineLawValue (const juce::String& id, int engine)
{
    if (engine == engineClassic || id == "nz_type") return 0.0f;    // CLASSIC: every round-2 law off, Draft; NOISE keeps the sound's own type (Classic unless its spec says)
    return 1.0f;                                                    // V2: drive Inside, unison V2, Ghost reverb, quality High
}
// LOUDNESS NORMALISATION (sound audit 2026-10-26, V2 only): the V2 trims above matched each sound to its CLASSIC loudness, and the CLASSIC
// bank spread +-6 LU (C3 note, integrated) - stepping through the presets jumped in level. ProV2Norm.inc (made by `ProTests auditnorm`
// from the audit's renders) adds the gain that puts every sound's loudest 400 ms (the mean of a C3 note and a C3 major chord, velocity 100)
// at the bank median. CLASSIC states keep their old trims.
float ProProcessor::factoryV2NormDb (const juce::String& name)
{
    struct N { const char* name; float db; };
    static const N list[] {
#include "ProV2Norm.inc"
        { nullptr, 0.0f } };
    static const std::map<juce::String, float> m = [] { std::map<juce::String, float> x; for (auto* e = list; e->name != nullptr; ++e) x[e->name] = e->db; return x; }();
    auto it = m.find (name);
    return it != m.end() ? it->second : 0.0f;
}
float ProProcessor::factoryTrimDbFor (const juce::String& name, int engine)
{
    const float t = factoryTrimDb (name);
    if (engine != engineV2) return t;
    auto* v = v2TuneOf (name);
    return (v != nullptr ? t + v->trimDb : t) + factoryV2NormDb (name);
}
bool ProProcessor::factoryCleanOnFor (const juce::String& name, int engine)
{
    if (engine == engineV2) if (auto* v = v2TuneOf (name); v != nullptr && v->clean >= 0) return v->clean > 0;
    return factoryCleanOn (name);
}
juce::String ProProcessor::factoryV2Spec (const juce::String& name)
{
    auto* v = v2TuneOf (name);
    return v != nullptr && v->spec != nullptr ? juce::String (v->spec) : juce::String();
}

// ------------------------------------------------------------------ user sounds
juce::File ProProcessor::userPresetDir()
{
    auto d = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Ghosty").getChildFile ("Ghosty Pro");
    d.createDirectory();
    return d;
}

juce::Array<juce::File> ProProcessor::listUserPresets() const
{
    auto files = userPresetDir().findChildFiles (juce::File::findFiles, false, "*.ghostypro");
    struct ByName { static int compareElements (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()); } } cmp;
    files.sort (cmp);
    return files;
}

bool ProProcessor::saveUserPreset (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty()) return false;
    {
        const juce::SpinLock::ScopedLockType sl (reqLock);
        currentPatchName = clean; patchIsUser = true;
    }
    auto tree = makeStateTree();
    tree.removeChild (tree.getChildWithProperty ("id", "bypass"), nullptr);       // BYPASS is never part of a sound
    leaveMadeTablesOut (tree, true);   // demo: the user preset keeps the user's own tables, not plugin-made ones
    auto xml = tree.createXml();
    const bool ok = xml != nullptr && xml->writeTo (userPresetDir().getChildFile (clean + ".ghostypro"));
#if ! GHOSTY_FACTORY_PRESETS
    if (ok && ! isLayer) { refreshUserPrograms(); currentProgram = userProgramOf (clean); }   // FACTORY PRESETS OFF: your sounds are the host programs
#endif
    return ok;
}

// ------------------------------------------------------------------ SNAPSHOTS (Shared/SnapshotBank.h)
// the demo (locked): a tree for disk loses its plugin-made wavetables (ExportGate.h keepRenderedAudio); a click save says why
void ProProcessor::leaveMadeTablesOut (juce::ValueTree& tree, bool fromClick)
{
    if (ghosty::exportgate::keepRenderedAudio (license)) return;
    if (gpro::wt::stripMadeTables (tree) > 0)
        ghosty::exportgate::renderedAudioLeftOut (fromClick ? getActiveEditor() : nullptr, fromClick ? "save:pro.user-wavetable" : "save:pro.user-wavetable.state");
}
juce::ValueTree ProProcessor::captureSound()
{
    auto tree = makeStateTree();
    tree.removeChild (tree.getChildWithProperty ("id", "bypass"), nullptr);       // BYPASS / the look / the morph puck are never part of a sound
    tree.removeChild (tree.getChildWithProperty ("id", "morph_x"), nullptr); tree.removeChild (tree.getChildWithProperty ("id", "morph_y"), nullptr);
    tree.removeChild (tree.getChildWithProperty ("id", "q_render"), nullptr); tree.removeChild (tree.getChildWithProperty ("id", "q_lock"), nullptr);
    return tree;
}
void ProProcessor::restoreSound (const juce::ValueTree& t, const juce::String& name)
{
    if (! t.isValid() || ! t.hasType (apvts.state.getType())) return;
    auto state = t.createCopy();
    state.setProperty ("patchName", name, nullptr);
    state.setProperty ("patchUser", true, nullptr);
    const bool wasBypassed = bypassParam != nullptr && bypassParam->get();
    const float mx = morphX->load(), my = morphY->load();
    applyStateTree (state);
    if (bypassParam != nullptr && bypassParam->get() != wasBypassed) bypassParam->setValueNotifyingHost (wasBypassed ? 1.0f : 0.0f);
    setParam ("morph_x", mx); setParam ("morph_y", my);       // a sound load never moves the puck
}
void ProProcessor::makeSnapshotBank()
{
    snapshots = std::make_unique<ghosty::SnapshotBank> (ghosty::SnapshotBank::Options { "Ghosty Pro" });
    snapshots->capture = [this] { auto t = captureSound(); leaveMadeTablesOut (t, true); return t; };   // (a snapshot is a file on disk)
    snapshots->restore = [this] (const juce::ValueTree& t) { restoreSound (t, "Snapshot"); };
    snapshots->onRestored = [this] (const ghosty::SnapshotInfo& in) { const juce::SpinLock::ScopedLockType sl (reqLock); currentPatchName = in.name; patchIsUser = true; };
}

// ------------------------------------------------------------------ MORPH BOARD (message thread)
juce::ValueTree ProProcessor::factorySound (int program, int engine)
{   // a factory preset as a whole sound, without touching this instance (a temporary one loads it, with the caller's SOUND ENGINE)
    auto tmp = std::make_unique<ProProcessor>();
    tmp->setSoundEngine (engine);
    if (juce::isPositiveAndBelow (program, (int) factoryBank().size())) tmp->setCurrentProgram (program);   // (FACTORY PRESETS OFF: no factory sounds; INIT)
    return tmp->captureSound();
}
void ProProcessor::morphChanged (bool soundsChanged)
{
    if (soundsChanged)
    {
        auto set = std::make_unique<gpro::MorphSet>();
        const int G = (int) morphBoard.ghosts.size(), S = macros.slotCount();
        set->ghosts = G; set->slots = S; set->plain.assign ((size_t) (G * S), 0.0f); set->norm.assign ((size_t) (G * S), 0.0f); set->trimDb.assign ((size_t) G, 0.0f);
        for (int k = 0; k < G; ++k)
        {
            const auto snd = morphSounds[morphBoard.ghosts[(size_t) k].id];
            set->trimDb[(size_t) k] = 0.0f;       // the trim is the TRIM knob now: it blends like every other knob
            for (int sl = 0; sl < S; ++sl)
            {
                auto* prm = macros.slotParam (sl);
                const auto c = snd.getChildWithProperty ("id", macros.slotId (sl));
                // a value the sound doesn't store (the state leaves defaults out) is the parameter's default
                float def = prm->convertFrom0to1 (static_cast<juce::RangedAudioParameter*> (prm)->getDefaultValue());
                if (macros.slotId (sl) == "out_trim") def = (float) snd.getProperty ("trimDb", 0.0f);   // an older sound: its hidden trim
                const float v = c.isValid() ? (float) c.getProperty ("value") : def;
                set->plain[(size_t) (k * S + sl)] = juce::jlimit (prm->range.start, prm->range.end, std::isfinite (v) ? v : def);
                set->norm[(size_t) (k * S + sl)] = prm->convertTo0to1 (set->plain[(size_t) (k * S + sl)]);
            }
        }
        const int F = morph.fxCount();
        set->fx = F; set->fxPlain.assign ((size_t) (G * F), 0.0f); set->fxNorm.assign ((size_t) (G * F), 0.0f);
        for (int k = 0; k < G; ++k)
        {
            const auto snd = morphSounds[morphBoard.ghosts[(size_t) k].id];
            for (int f = 0; f < F; ++f)
            {
                auto* prm = morph.fxParam (f);
                const auto c = snd.getChildWithProperty ("id", prm->getParameterID());
                const float def = prm->convertFrom0to1 (prm->getDefaultValue());
                const float v = c.isValid() ? (float) c.getProperty ("value") : def;
                const auto& rg = prm->getNormalisableRange();
                set->fxPlain[(size_t) (k * F + f)] = juce::jlimit (rg.start, rg.end, std::isfinite (v) ? v : def);
                set->fxNorm[(size_t) (k * F + f)] = prm->convertTo0to1 (set->fxPlain[(size_t) (k * F + f)]);
            }
        }
        morph.setSet (std::move (set));
        morph.lastLeader = -1;
    }
    modMorphRebuild();                            // MOD (round 3): the connections' amounts blend too
    morph.board.publish (morphBoard);
}
int ProProcessor::morphAddGhost (float x, float y, const juce::ValueTree& sound, const juce::String& name)
{
    const int i = morphBoard.add (x, y);
    if (i < 0) return -1;
    morphBoard.ghosts[(size_t) i].userData = name;
    morphSounds[morphBoard.ghosts[(size_t) i].id] = sound.createCopy();
    morphChanged (true);
    return i;
}
void ProProcessor::morphSetSound (int ghost, const juce::ValueTree& sound, const juce::String& name)
{
    if (! juce::isPositiveAndBelow (ghost, (int) morphBoard.ghosts.size())) return;
    morphSounds[morphBoard.ghosts[(size_t) ghost].id] = sound.createCopy();
    morphBoard.ghosts[(size_t) ghost].userData = name;
    morphChanged (true);
}
void ProProcessor::morphRemove (int ghost)
{
    if (! juce::isPositiveAndBelow (ghost, (int) morphBoard.ghosts.size())) return;
    morphSounds.erase (morphBoard.ghosts[(size_t) ghost].id);
    morphBoard.remove (ghost);
    if (morphBoard.ghosts.empty()) morph.enabled.store (false);
    morphChanged (true);
}
void ProProcessor::morphWriteBackEdits()
{   // turning MORPH on used to reload each ghost's OLD sound, so everything changed while MORPH was off was lost
    if (morphEditId.isEmpty() || morphSounds.count (morphEditId) == 0) return;
    morphSounds[morphEditId] = captureSound();
    morphChanged (true);
}
void ProProcessor::morphCommit()
{   // (called when MORPH goes OFF; see also morphEditId: remember WHICH ghost you are editing)
    if (! morphBoard.ghosts.empty())
    {
        const int lead = morph.leader();
        morphEditId = lead >= 0 && lead < (int) morphBoard.ghosts.size() ? morphBoard.ghosts[(size_t) lead].id : juce::String();
    }   // MORPH OFF: the blended sound becomes the parameters' values (the knobs show it, it is saved, editing goes on from it)
    for (int sl = 0; sl < macros.slotCount(); ++sl)
    {
        auto* prm = macros.slotParam (sl);
        float v = 0; if (! morph.blended (sl, v)) continue;
        if (std::abs (prm->get() - v) > 1.0e-7f) prm->setValueNotifyingHost (prm->convertTo0to1 (v));
    }
    for (int f = 0; f < morph.fxCount(); ++f)
    {
        auto* prm = morph.fxParam (f);
        const float v = apvts.getRawParameterValue (prm->getParameterID())->load();
        prm->setValueNotifyingHost (prm->convertTo0to1 (v));
    }
}
void ProProcessor::morphTick (double nowMs)
{   // the leader's sound (switches, tables, layers, FX rack, macros ...) when the highest weight moves to another ghost
    if (! morph.enabled.load() || morphBoard.ghosts.empty()) { morph.lastLeader = -1; morphWanted = -1; morphHolding = false; return; }
    const double now = nowMs >= 0.0 ? nowMs : juce::Time::getMillisecondCounterHiRes();
    const int lead = morph.leader();
    if (lead < 0 || lead >= (int) morphBoard.ghosts.size()) return;
    if (! morphLegacyReload && morph.lastLeader >= 0)
    {
        // ORBIT smoothness: count how often the wanted leader flips; faster than ~2 flips a second (a fast ORBIT) -> hold the leader
        if (lead != morphWanted) { morphFlips[morphFlipW] = now; morphFlipW = (morphFlipW + 1) % 8; morphWanted = lead; }
        int recent = 0; for (double t : morphFlips) if (now - t < 1000.0) ++recent;
        morphHolding = recent >= 3 && lead != morph.lastLeader;
        if (lead == morph.lastLeader || morphHolding) return;
        if (now - morphLastReloadMs < 150.0) return;                      // never more than one discrete reload per 150 ms
    }
    else morphWanted = lead;
    if (lead == morph.lastLeader) return;
    morph.lastLeader = lead;
    morphLastReloadMs = now;
    morphReloads.fetch_add (1);
    const auto& g = morphBoard.ghosts[(size_t) lead];
    // PERFORMANCE settings (key, scale, chords, arp + its rate / mode / octaves / gate, mono, legato, voices, glide, bend range) belong to HOW YOU PLAY,
    // not to a ghost's sound: they stay exactly as set on the PLAY / SYNTH pages whichever ghost leads. (Loading them from the ghost flipped the
    // arp / scale / chords to the other sound's values and switching MONO silences held notes: a long-held key kept cutting out.)
    auto snd = morphSounds[g.id].createCopy();
    static const char* const keep[] { "key", "scale", "chord", "arp", "arpRate", "arpMode", "arpOct", "arpGate", "mono", "legato", "poly", "glide", "bend", "v_same1", "v_steal" };
    for (const char* id : keep)
    {
        auto* live = apvts.getRawParameterValue (id); if (live == nullptr) continue;
        auto c = snd.getChildWithProperty ("id", id);
        if (! c.isValid()) { c = juce::ValueTree ("PARAM"); c.setProperty ("id", id, nullptr); snd.appendChild (c, nullptr); }
        c.setProperty ("value", live->load(), nullptr);
    }
    const auto name = "MORPH " + g.label + (g.userData.isNotEmpty() ? ": " + g.userData : juce::String());
    if (morphLegacyReload) { restoreSound (snd, name); return; }
    // a light restore: only what differs from what is playing is loaded
    snd.setProperty ("patchName", name, nullptr); snd.setProperty ("patchUser", true, nullptr);
    const bool wasBypassed = bypassParam != nullptr && bypassParam->get();
    const float mx = morphX->load(), my = morphY->load();
    applyStateTree (snd, true);
    modMorphRebuild();                            // MOD (round 3): the new leader's connections, with every ghost's amounts
    if (bypassParam != nullptr && bypassParam->get() != wasBypassed) bypassParam->setValueNotifyingHost (wasBypassed ? 1.0f : 0.0f);
    setParam ("morph_x", mx); setParam ("morph_y", my);
}
juce::ValueTree ProProcessor::morphTree() const
{
    juce::ValueTree t ("MORPHBOARD");
    t.setProperty ("on", morph.enabled.load(), nullptr);
    t.appendChild (morphBoard.toTree(), nullptr);
    for (auto& g : morphBoard.ghosts)
        if (auto it = morphSounds.find (g.id); it != morphSounds.end())
        { juce::ValueTree s ("GHOSTSOUND"); s.setProperty ("ghost", g.id, nullptr); s.appendChild (it->second.createCopy(), nullptr); t.appendChild (s, nullptr); }
    return t;
}
void ProProcessor::loadMorphTree (const juce::ValueTree& t)
{
    morphSounds.clear();
    morphBoard = gspirit::Board::fromTree (t.getChildWithName ("SPIRITBOARD"));
    for (auto c : t) if (c.hasType ("GHOSTSOUND") && c.getNumChildren() > 0) morphSounds[c.getProperty ("ghost").toString()] = c.getChild (0).createCopy();
    for (auto& g : morphBoard.ghosts) if (! morphSounds.count (g.id)) morphSounds[g.id] = captureSound();   // (a ghost without a sound: the current one)
    morph.enabled.store (t.isValid() && (bool) t.getProperty ("on", false) && ! morphBoard.ghosts.empty());
    morphChanged (true);
}
juce::File ProProcessor::boardDir() { return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("GhostBlock").getChildFile ("Ghosty Pro").getChildFile ("Boards"); }
bool ProProcessor::saveBoard (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim()); if (clean.isEmpty()) return false;
    boardDir().createDirectory();
    auto tree = morphTree(); leaveMadeTablesOut (tree, true);   // demo: the board's sounds without plugin-made tables
    auto xml = tree.createXml();
    return xml != nullptr && xml->writeTo (boardDir().getChildFile (clean + ".ghostyboard"));
}
bool ProProcessor::loadBoard (const juce::File& f)
{
    auto xml = juce::parseXML (f); if (xml == nullptr) return false;
    auto t = juce::ValueTree::fromXml (*xml); if (! t.hasType ("MORPHBOARD")) return false;
    const bool wasOn = morph.enabled.load();
    loadMorphTree (t); morph.enabled.store (wasOn && ! morphBoard.ghosts.empty());
    return true;
}

bool ProProcessor::loadUserPreset (const juce::File& file)
{
    if (snapshots != nullptr) snapshots->clearCurrent();
    auto xml = juce::parseXML (file);
    if (xml == nullptr) return false;
    auto state = juce::ValueTree::fromXml (*xml);
    if (! state.hasType (apvts.state.getType())) return false;
    state.setProperty ("patchName", file.getFileNameWithoutExtension(), nullptr);
    state.setProperty ("patchUser", true, nullptr);
    const bool wasBypassed = bypassParam != nullptr && bypassParam->get();     // a user sound never changes BYPASS
    applyStateTree (state);
    if (bypassParam != nullptr && bypassParam->get() != wasBypassed) bypassParam->setValueNotifyingHost (wasBypassed ? 1.0f : 0.0f);
    return true;
}

int ProProcessor::voiceNotes (int* out, int maxOut) const
{
    int n = 0;
    for (int i = 0; i < synth.getNumVoices() && n < maxOut; ++i)
        if (auto* v = synth.getVoice (i)) if (v->isVoiceActive()) out[n++] = v->getCurrentlyPlayingNote();
    return n;
}

juce::String ProProcessor::patchName() const { const juce::SpinLock::ScopedLockType sl (reqLock); return currentPatchName; }
bool ProProcessor::isUserPatch() const { const juce::SpinLock::ScopedLockType sl (reqLock); return patchIsUser; }

juce::AudioProcessorEditor* ProProcessor::createEditor() { return new ProEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ProProcessor(); }
