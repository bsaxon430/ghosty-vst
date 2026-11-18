// ENGINES (round 3, item 5): names, the table-menu mapping, the per-voice OscEngine wrapper and its band-limited rate bridge.
// The DSP itself is in ProEnginesPlaits.cpp / ProEnginesTexture.cpp / ProEnginesResonator.cpp (vendored MIT code, own wrappers).
#include "ProEngines.h"
#include "ProEnginesCore.h"
#include "ProTables.h"
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace gpro::eng
{
// engines available in this build, in Id order (the list only ever grows at the end: saved table indices stay what they were)
static constexpr int kAvailable = eResonator + 1;

const juce::StringArray& engineNames()
{
    static juce::StringArray s = []
    {
        const juce::StringArray all { "VA", "WAVESHAPE", "FM 2-OP", "ADDITIVE", "GRAIN FORMANT", "SWARM", "STRING", "MODAL", "CHORD", "SPEECH", "PARTICLE",
                                      "TEXTURE", "RESONATOR" };
        juce::StringArray a; for (int i = 0; i < kAvailable; ++i) a.add (all[i]);
        return a;
    }();
    return s;
}

const juce::StringArray& tableChoiceNames()
{
    static juce::StringArray s = [] { juce::StringArray a (tableNames()); a.addArray (engineNames()); return a; }();
    return s;
}

const juce::StringArray& sourceNames()
{
    static juce::StringArray s = [] { juce::StringArray a { "Other OSC" }; a.addArray (tableNames()); return a; }();
    return s;
}

const juce::StringArray& resonatorModelNames() { static juce::StringArray s { "Modal", "String" }; return s; }

int engineOf (int tblParam) noexcept
{
    const int e = tblParam - (int) numTables;
    return e >= 0 && e < kAvailable ? e : -1;
}

bool usesInput (int e) noexcept { return e == eTexture || e == eResonator; }

int tableSlotFor (int tblParam, int srcParam) noexcept
{
    const int e = engineOf (tblParam);
    if (tblParam < (int) numTables) return juce::jmax (0, tblParam);
    if (e >= 0 && usesInput (e) && srcParam >= 1 && srcParam - 1 < (int) numTables) return srcParam - 1;   // the wavetable the engine is fed with
    return (int) tSaw;          // (a synthesis engine reads no table: the slot keeps a cheap cached one)
}

juce::String controlCaption (int e, int k)
{
    static const char* cap[numEngines][3] {
        { "DETUNE", "SQUARE", "SAW" },            // VA: second osc detune, square PW / sync, saw shape
        { "SHAPE", "FOLD", "ASYMMETRY" },         // WAVESHAPE
        { "RATIO", "INDEX", "FEEDBACK" },         // FM 2-OP
        { "BUMPS", "CENTRE", "SLOPE" },           // ADDITIVE
        { "FORMANT 2", "FORMANT", "WIDTH" },      // GRAIN FORMANT
        { "SPREAD", "DENSITY", "LENGTH" },        // SWARM
        { "INHARM", "BRIGHT", "DECAY" },          // STRING
        { "MATERIAL", "BRIGHT", "DECAY" },        // MODAL
        { "CHORD", "INVERSION", "WAVE" },         // CHORD
        { "VOICE", "SPECIES", "VOWEL" },          // SPEECH
        { "SCATTER", "DENSITY", "FILTER" },       // PARTICLE
        { "POSITION", "TEXTURE", "DENSITY" },     // TEXTURE
        { "STRUCTURE", "BRIGHT", "DAMPING" } };   // RESONATOR
    if (e < 0 || e >= numEngines || k < 0 || k > 2) return k == 0 ? "HARMONICS" : k == 1 ? "TIMBRE" : "MORPH";
    return cap[e][k];
}

float levelTrim (int e) noexcept
{
    // the engine's own output scaled to the Saw table's RMS at C3 with HARMONICS / TIMBRE / MORPH at 0.5 (ProTests engines measures
    // it; the residual is printed and must stay inside +-1 dB). The ear-envelope engines are matched over their first 250 ms.
    static const float trim[numEngines] { 1.0113f, 0.9968f, 0.7112f, 1.5113f, 1.1323f, 4.5597f, 10.0665f, 2.0824f, 1.0931f, 2.4117f, 3.6304f, 0.7190f, 9.4569f };
    static const bool off = [] { const char* env = std::getenv ("GPRO_ENGINE_TRIM_OFF"); return env != nullptr && env[0] == '1'; }();   // (tests: measure the raw level)
    if (off) return 1.0f;
    return e >= 0 && e < numEngines ? trim[e] : 1.0f;
}
[[maybe_unused]] static const float trimAtLoad = levelTrim (0);   // (its environment check runs at load, not on the first audio block)

// ------------------------------------------------------------------ ENGINES DIARY
namespace diary
{
namespace
{
struct Event { int kind, engine, info; float rate; };
struct Fifo
{
    static constexpr int N = 256;
    juce::SpinLock lock;                       // producers only ever try-lock it (never wait); the message thread holds it briefly
    juce::AbstractFifo fifo { N };
    std::array<Event, N> ev {};
    std::atomic<int> dropped { 0 };
};
Fifo& fifo() { static Fifo f; return f; }
[[maybe_unused]] const Fifo& fifoAtLoad = fifo();               // (built at load, never lazily on an audio thread)

const char* kindName (int k)
{
    switch (k)
    {
        case kSwitch:      return "audio: engine selected";
        case kFirstRender: return "audio: first render OK";
        case kFallback:    return "audio: ENGINE FAILED -> Saw wavetable";
        case kRateGuard:   return "audio: engine off (no valid rate yet)";
        default:           return "audio: ?";
    }
}
juce::String engineName (int e) { return e >= 0 && e < numEngines ? engineNames()[e] : juce::String (e); }

void writeLines (const juce::StringArray& lines) noexcept
{
    try
    {
        auto f = file(); f.getParentDirectory().createDirectory();
        if (f.getSize() > 512 * 1024) f.deleteFile();
        juce::FileOutputStream o (f);          // (appends)
        if (! o.openedOk()) return;
#ifdef JucePlugin_VersionString
        const char* ver = JucePlugin_VersionString;
#else
        const char* ver = "dev";
#endif
        const auto now = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S");
        for (auto& l : lines) o << now << "  v" << ver << " (" << __DATE__ << ")  " << l << "\n";
        o.flush();
    }
    catch (...) {}
}
} // namespace

juce::File file()
{
#if JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Logs/Ghosty Pro/engines.log");
#else
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Ghosty Pro/engines.log");
#endif
}

void post (int kind, int engine, double rate, int info) noexcept
{
    auto& F = fifo();
    if (! F.lock.tryEnter()) { ++F.dropped; return; }
    const auto w = F.fifo.write (1);
    if (w.blockSize1 > 0) F.ev[(size_t) w.startIndex1] = { kind, engine, info, (float) rate };
    else ++F.dropped;
    F.lock.exit();
}

void note (const juce::String& line) noexcept
{
    flush();                                   // (the audio events so far first: the file stays in order)
    try { writeLines ({ line }); } catch (...) {}
}

void flush() noexcept
{
    try
    {
        auto& F = fifo();
        std::array<Event, Fifo::N> got; int n = 0;
        {
            const juce::SpinLock::ScopedLockType sl (F.lock);
            const auto r = F.fifo.read (F.fifo.getNumReady());
            for (int i = 0; i < r.blockSize1; ++i) got[(size_t) n++] = F.ev[(size_t) (r.startIndex1 + i)];
            for (int i = 0; i < r.blockSize2; ++i) got[(size_t) n++] = F.ev[(size_t) (r.startIndex2 + i)];
        }
        const int dropped = F.dropped.exchange (0);
        if (n == 0 && dropped == 0) return;
        // the same event from many voices / oscillators: one line with a count
        juce::StringArray lines; std::vector<int> counts;
        for (int i = 0; i < n; ++i)
        {
            const auto& e = got[(size_t) i];
            juce::String l; l << kindName (e.kind) << ": " << engineName (e.engine) << " @ " << juce::String (e.rate, 0) << " Hz";
            if (e.kind == kSwitch) l << " (osc " << (e.info + 1) << ")";
            if (e.kind == kFallback) l << " (" << (e.info == 1 ? "select" : e.info == 2 ? "render" : "?") << ")";
            const int at = lines.indexOf (l);
            if (at >= 0) ++counts[(size_t) at]; else { lines.add (l); counts.push_back (1); }
        }
        for (int i = 0; i < lines.size(); ++i) if (counts[(size_t) i] > 1) lines.getReference (i) << "  x" << counts[(size_t) i];
        if (dropped > 0) lines.add ("audio: " + juce::String (dropped) + " diary events dropped");
        writeLines (lines);
    }
    catch (...) {}
}
} // namespace diary

// ------------------------------------------------------------------ rate bridge
// A streaming windowed-sinc resampler (Kaiser, beta 8, 8 zero crossings each side at the lower of the two rates, cut-off 0.45 of it):
// the cores that keep their module rate (TEXTURE 32 kHz, RESONATOR 48 kHz) talk to the voice's engine rate through two of these.
namespace
{
struct Kernel
{
    static constexpr int W = 8, OS = 256;          // half width in lower-rate samples, table points per sample
    static constexpr double FC = 0.45;             // cut-off re the lower rate
    std::array<float, W * OS + 2> tab {};
    Kernel()
    {
        auto bessel0 = [] (double x) { double s = 1, t = 1; for (int k = 1; k < 30; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; } return s; };
        const double beta = 8.0, norm = bessel0 (beta);
        for (int i = 0; i <= W * OS + 1; ++i)
        {
            const double x = (double) i / OS;
            if (x >= W) { tab[(size_t) i] = 0; continue; }
            const double sx = x == 0 ? 1.0 : std::sin (juce::MathConstants<double>::pi * 2 * FC * x) / (juce::MathConstants<double>::pi * 2 * FC * x);
            const double r = x / W, win = bessel0 (beta * std::sqrt (juce::jmax (0.0, 1 - r * r))) / norm;
            tab[(size_t) i] = (float) (2 * FC * sx * win);
        }
    }
    inline float at (double x) const noexcept
    {
        x = std::abs (x) * OS; const int i = (int) x; if (i >= W * OS) return 0.0f;
        const float f = (float) (x - i);
        return tab[(size_t) i] + (tab[(size_t) i + 1] - tab[(size_t) i]) * f;
    }
};
const Kernel& kernel() { static const Kernel k; return k; }
[[maybe_unused]] const Kernel& kernelAtLoad = kernel();         // (built when the plug-in loads: never lazily - behind a guard lock - on an audio thread)

struct Bridge
{
    static constexpr int RING = 2048;              // (power of two; the widest window is 2 x 8 x 6 = 96 input samples)
    std::array<float, RING> buf {};
    std::int64_t written = 0;
    double t = 0, step = 1; float scale = 1; int hw = Kernel::W;
    void setup (double inRate, double outRate, double delay)
    {
        step = inRate / outRate; scale = (float) juce::jmin (1.0, outRate / inRate);
        hw = (int) std::ceil (Kernel::W / scale) + 1;
        buf.fill (0.0f); written = 0; t = -delay;
    }
    inline void push (float x) noexcept { buf[(size_t) (written & (RING - 1))] = x; ++written; }
    inline bool ready() const noexcept { return (std::int64_t) std::floor (t) + hw < written; }
    inline float pull() noexcept
    {   // (ready() holds: every tap index is below written; indices below 0 read the zeroed ring of a fresh start)
        const auto& K = kernel();
        const std::int64_t c = (std::int64_t) std::floor (t);
        const float fr = (float) (t - (double) c);
        const float ds = scale * (float) Kernel::OS;
        float x = (fr + (float) (hw - 1)) * ds;            // |t - k| x scale x OS for the first tap (k = c - hw + 1), falling by ds per tap
        float s = 0;
        for (std::int64_t k = c - hw + 1; k <= c + hw; ++k, x -= ds)
        {
            const float ax = std::abs (x); const int i = (int) ax;
            if (i < Kernel::W * Kernel::OS)
            {
                const float f = ax - (float) i;
                s += buf[(size_t) (k & (RING - 1))] * (K.tab[(size_t) i] + (K.tab[(size_t) i + 1] - K.tab[(size_t) i]) * f);
            }
        }
        t += step;
        return s * scale;
    }
};
} // namespace

// ------------------------------------------------------------------ OscEngine
struct OscEngine::Impl
{
    std::unique_ptr<Core> plaits, texture, resonator;
    Core* core = nullptr;
    int engine = -1;
    double engineRate = 0, coreRate = 0; int block = 12; bool bridged = false;
    Bridge in, out;
    double nativeDue = 0, nativeStep = 1;    // a core without input: native samples owed to the output bridge
    Settings s;
    float sh = 0.5f, st = 0.5f, sm = 0.5f, aSm = 0.01f; bool snap = true;
    bool trigger = false, fromSilence = true; float accent = 0.8f;
    std::array<float, 64> ib {}, ob {};
    int obPos = 0, obLen = 0, ibLen = 0;
    float fade = 1.0f, fadeStep = 0.0f, trim = 1.0f;
    bool firstRender = false;                  // diary: the first block after a switch
    std::uint32_t failed = 0;                  // engines that threw in this slot (bit per Id): they stay off, the slot plays its wavetable

    Core* coreFor (int e) const { return e == eTexture ? texture.get() : e == eResonator ? resonator.get() : plaits.get(); }

    void setRate (double rate)
    {
        engineRate = rate;
        coreRate = core->rateFor (engine, rate);
        core->select (engine, coreRate);
        block = juce::jlimit (1, 48, core->blockFor (coreRate));
        bridged = std::abs (coreRate - rate) > 0.5;
        if (bridged)
        {   // the output side starts one block + both windows late, so a whole block is always there when the voice asks
            in.setup (rate, coreRate, 0.0);
            out.setup (coreRate, rate, 0.0);
            out.t = -(double) (block + 2 * Kernel::W + 4);
            nativeStep = coreRate / rate; nativeDue = 0;
        }
        aSm = 1.0f - (float) std::exp (-1.0 / (0.010 * rate));
        obPos = obLen = ibLen = 0;
    }

    void renderBlock (double freqRatio)
    {
        CoreParams p;
        p.rate = (float) coreRate; p.hz = (float) (s.hz * freqRatio);
        p.h = sh; p.t = st; p.m = sm; p.accent = accent; p.trigger = trigger; trigger = false;
        p.size = s.size; p.pitch = s.pitch; p.feedback = s.feedback; p.freeze = s.freeze; p.position = s.position; p.model = s.model;
        p.haveInput = s.haveInput;
        core->render (p, ib.data(), ob.data(), block);
        obPos = 0; obLen = block; ibLen = 0;
        if (firstRender) { firstRender = false; diary::post (diary::kFirstRender, engine, coreRate); }
    }
};

OscEngine::OscEngine() = default;
OscEngine::~OscEngine() = default;

void OscEngine::prepare()
{
    if (d != nullptr) return;
    auto i = std::make_unique<Impl>();
    i->plaits = makePlaitsCore();
    i->texture = makeTextureCore();
    i->resonator = makeResonatorCore();
    d = std::move (i);
}

void OscEngine::start (float velocity, bool fromSilence, juce::uint32 seed)
{
    if (d == nullptr) return;
    d->accent = juce::jlimit (0.0f, 1.0f, velocity);
    d->trigger = true; d->fromSilence = fromSilence; d->snap = true;
    for (auto* c : { d->plaits.get(), d->texture.get(), d->resonator.get() }) if (c != nullptr) c->rng = seed | 1u;
    if (fromSilence && d->core != nullptr && d->engine >= 0 && d->engineRate > 0)
    {
        try { d->setRate (d->engineRate); }   // (re-initialises the engine, empties the bridges and the block buffers)
        catch (...) { fail (d->engine, "select"); }
    }
}

void OscEngine::fail (int e, const char* where) noexcept
{
    if (d != nullptr)
    {
        if (e >= 0 && e < 32) d->failed |= 1u << e;
        d->engine = -1; d->core = nullptr;
    }
    engine = -1;
    diary::post (diary::kFallback, e, d != nullptr ? d->engineRate : 0.0, where[0] == 's' ? 1 : 2);
}

void OscEngine::control (const Settings& s, double rate)
{
    if (d == nullptr) { engine = -1; return; }
    const int e = s.engine;
    if (e < 0 || e >= numEngines || d->coreFor (e) == nullptr || (d->failed & (1u << e)) != 0) { engine = -1; d->engine = -1; return; }
    if (! (rate >= 8000.0 && rate <= 1.0e6))
    {   // (no valid rate yet - prepareToPlay not called, or a host's 0 Hz: the rate maths would divide by zero / loop forever)
        if (d->engine != -2) diary::post (diary::kRateGuard, e, rate);
        engine = -1; d->engine = -2; return;
    }
    try
    {
        if (e != d->engine)
        {   // a new engine: its state from scratch, faded in over 5 ms
            d->core = d->coreFor (e);
            d->engine = e;
            d->setRate (rate);
            d->fade = 0.0f; d->fadeStep = (float) (1.0 / (0.005 * rate));
            d->trim = levelTrim (e);
            d->firstRender = true;
            diary::post (diary::kSwitch, e, d->coreRate, osc);
        }
        else if (std::abs (rate - d->engineRate) > 0.5) d->setRate (rate);
    }
    catch (...) { fail (e, "select"); return; }
    engine = e;
    d->s = s;
    if (d->snap) { d->sh = s.h; d->st = s.t; d->sm = s.m; d->snap = false; }
}

float OscEngine::tick (float input, double freqRatio) noexcept
{
    if (engine < 0 || d == nullptr || d->core == nullptr) return 0.0f;
    try { return tickImpl (input, freqRatio); }
    catch (...) { fail (engine, "render"); return 0.0f; }
}

float OscEngine::tickImpl (float input, double freqRatio)
{
    auto& I = *d;
    // HARMONICS / TIMBRE / MORPH: 10 ms one-pole per sample (the core reads them at each of its blocks and ramps inside)
    I.sh += (I.s.h - I.sh) * I.aSm; I.st += (I.s.t - I.st) * I.aSm; I.sm += (I.s.m - I.sm) * I.aSm;
    float y = 0.0f;
    if (! I.bridged)
    {
        if (I.core->takesInput())
        {   // one block late: the block is rendered once its input is in
            I.ib[(size_t) I.ibLen++] = input;
            y = I.obPos < I.obLen ? I.ob[(size_t) I.obPos++] : 0.0f;
            if (I.ibLen >= I.block) I.renderBlock (freqRatio);
        }
        else
        {
            if (I.obPos >= I.obLen) I.renderBlock (freqRatio);
            y = I.ob[(size_t) I.obPos++];
        }
    }
    else if (I.core->takesInput())
    {
        I.in.push (input);
        while (I.in.ready())
        {
            I.ib[(size_t) I.ibLen++] = I.in.pull();
            if (I.ibLen >= I.block) { I.renderBlock (freqRatio); for (int k = 0; k < I.obLen; ++k) I.out.push (I.ob[(size_t) k]); I.obLen = 0; }
        }
        y = I.out.ready() ? I.out.pull() : 0.0f;
    }
    else
    {   // no input: the core just has to stay ahead of the output bridge
        I.nativeDue += I.nativeStep;
        while (I.nativeDue >= 1.0) { I.nativeDue -= 1.0; if (++I.ibLen >= I.block) { I.renderBlock (freqRatio); for (int k = 0; k < I.obLen; ++k) I.out.push (I.ob[(size_t) k]); I.obLen = 0; } }
        y = I.out.ready() ? I.out.pull() : 0.0f;
    }
    if (I.fade < 1.0f) { I.fade = juce::jmin (1.0f, I.fade + I.fadeStep); y *= I.fade; }
    return y * I.trim;
}
} // namespace gpro::eng
