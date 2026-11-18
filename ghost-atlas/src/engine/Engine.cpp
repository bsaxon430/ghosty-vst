// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Engine.h"
#include <mutex>
#include <thread>
#include "VoiceShift.h"
#include "SpectralRack.h"

namespace atlas
{
// ============================================================================================ the effect units
static int unitIndex (const juce::String& key)
{
    for (int i = 0; i < phantom::kNumUnits; ++i) if (key == phantom::unitDef (i).key) return i;
    return -1;
}
static std::unique_ptr<phantom::PhantomEffect> makeUnit (const juce::String& key)
{
    if (key == "shift") return std::make_unique<VoiceShiftFx>();
    if (key == "spectral") return makeSpectralRack();
    const int u = unitIndex (key);
    if (u < 0) return nullptr;
    return phantom::makeEffect (u);
}
// Ghost Atlas's own starting values for three of Phantom FX's units. There a unit is opened on a page of its own, with
// pads to hold, a pattern to draw and bands to switch on; here it arrives in a slot with knobs only, so it arrives
// already doing what it is for: the gate with a pattern that gates (its first pattern is open all the way), the stutter
// with a pad held, and the equaliser as five bands that are switched on, and flat. Only values a sound has not set for
// itself are affected: a sound that says what it wants keeps it.
static void atlasDefaults (const juce::String& unit, std::vector<phantom::ParamSpec>& specs)
{
    auto set = [&specs] (const std::string& id, float v) { for (auto& s : specs) if (s.id == id) s.def = juce::jlimit (s.min, s.max, v); };
    if (unit == "gate") set ("pattern", 4.0f);
    else if (unit == "haunt") set ("pad", 1.0f);
    else if (unit == "eq")
    {
        static const float freq[5] = { 90.0f, 300.0f, 1000.0f, 3500.0f, 9000.0f };
        for (int b = 0; b < 5; ++b)
        {
            const std::string n = std::to_string (b + 1);
            set (n + "_on", 1.0f);
            set (n + "_freq", freq[b]);
            set (n + "_type", b == 0 ? 1.0f : (b == 4 ? 2.0f : 0.0f));      // a low shelf, three bells, a high shelf
            if (b == 0 || b == 4) set (n + "_q", 0.71f);
        }
    }
}
int fxUnitIndex (const juce::String& key) { return unitIndex (key); }
const std::vector<UnitInfo>& fxUnits()
{
    static const std::vector<UnitInfo> units = [] {
        std::vector<UnitInfo> v;
        for (int i = 0; i < phantom::kNumUnits; ++i) { const auto& d = phantom::unitDef (i); v.push_back ({ d.key, d.title, d.blurb, d.group, d.accent }); }
        v.push_back ({ "shift", "VOICE SHIFT", "Moves the pitch and the vowel colour of a voice separately", phantom::gColour, 0xffffb3e6 });
        v.push_back ({ "spectral", "SPECTRAL FX", "Poltergeist 2's FX page: four spectral effects in a row, from 38, and Warp Core's spectral inversion", phantom::gColour, 0xff9ad9ff });
        return v;
    }();
    return units;
}
std::vector<phantom::ParamSpec> fxParams (const juce::String& key)
{
    if (key == "shift") return VoiceShiftFx().parameters();
    if (key == "spectral") return spectralRackParams();
    const int u = unitIndex (key);
    if (u < 0) return {};
    auto specs = phantom::unitParams (u);
    atlasDefaults (key, specs);
    return specs;
}
juce::String fxTitle (const juce::String& key)
{
    for (const auto& u : fxUnits()) if (u.key == key) return u.title;
    return key.isEmpty() ? juce::String ("EMPTY") : key.toUpperCase();
}
FxState fxDefault (const juce::String& key, bool on)
{
    FxState fx;
    fx.unit = key; fx.on = on && key.isNotEmpty();
    const int u = unitIndex (key);
    fx.mix = u >= 0 && phantom::unitDef (u).hasMix ? phantom::unitDef (u).defaultMix : 1.0f;
    for (const auto& s : fxParams (key)) fx.values[juce::String (s.id)] = s.def;
    return fx;
}

// ============================================================================================ keys and signatures
namespace
{
enum { keyInsert = 100, keySend = 200, keyMaster = 300 };
int keyOf (FxAddress w)
{
    if (w.kind == FxAddress::Insert) return keyInsert + juce::jlimit (0, kLayers - 1, w.layer) * kInserts + juce::jlimit (0, kInserts - 1, w.slot);
    if (w.kind == FxAddress::Send) return keySend + juce::jlimit (0, kSends - 1, w.slot);
    return keyMaster + juce::jlimit (0, kMasterSlots - 1, w.slot);
}
FxAddress addressOf (int key)
{
    if (key >= keyMaster) return FxAddress::master (key - keyMaster);
    if (key >= keySend) return FxAddress::send (key - keySend);
    return FxAddress::insert ((key - keyInsert) / kInserts, (key - keyInsert) % kInserts);
}
// how a note's velocity reaches the instrument (TOUCH and CURVE)
float touched (float velocity01, float sens, float curve)
{
    return lerp (0.8f, std::pow (clamp01 (velocity01), std::pow (2.0f, -1.6f * curve)), sens);
}
// everything that needs the instrument loaded again when it changes
juce::String sourceSignature (const LayerState& l)
{
    if (! l.used()) return "-";
    juce::String s;
    s << l.instrument << "|" << l.startRange << "|" << l.loopMode << "|" << l.loopStart << "|" << l.loopEnd << "|" << l.loopFade << "|" << (int) l.reverse << "|" << l.voices;
    if (l.keyFadeIn > 0 || l.keyFadeOut > 0) s << "|k" << l.keyFadeIn << "," << l.keyFadeOut << "," << l.p[lpKeyLo] << "," << l.p[lpKeyHi] << "," << l.p[lpCoarse];
    if (l.velFadeIn > 0 || l.velFadeOut > 0) s << "|v" << l.velFadeIn << "," << l.velFadeOut << "," << l.p[lpVelLo] << "," << l.p[lpVelHi] << "," << l.p[lpVelSens] << "," << l.p[lpVelCurve];
    return s;
}
} // namespace

// ============================================================================================ what the loader builds
struct Engine::SlotRt
{
    juce::String unit;
    std::unique_ptr<phantom::EffectSlot> slot;        // null: an empty slot
    std::vector<phantom::ParamSpec> specs;
    std::atomic<int> latency { 0 };
    std::atomic<bool> on { false };
    std::atomic_flag busy = ATOMIC_FLAG_INIT;         // held by whoever is inside the slot (the audio thread never waits for it)
    bool isSend = false;
};
struct Engine::SourceRt
{
    juce::String signature;
    std::unique_ptr<SfzLayer> sfz;                    // null: an empty layer
    bool startHook = false;
};

struct Engine::Loader : public juce::Thread
{
    explicit Loader (Engine& e) : juce::Thread ("Ghost Atlas loader"), engine (e) { startThread (juce::Thread::Priority::normal); }
    ~Loader() override
    {
        signalThreadShouldExit(); notify(); stopThread (20000);
        collect (true);
    }
    void request (int key)
    {
        {
            const juce::ScopedLock sl (lock);
            if (std::find (queue.begin(), queue.end(), key) != queue.end()) return;     // already waiting: it will read the newest settings
            queue.push_back (key);
            engine.jobsPending.fetch_add (1);
        }
        notify();
    }
    void run() override
    {
        while (! threadShouldExit())
        {
            int key = -1;
            std::vector<int> layers;
            {
                const juce::ScopedLock sl (lock);
                if (! queue.empty()) { key = queue.front(); queue.erase (queue.begin()); }
                // a layer's instrument: every other layer waiting with it is loaded at the same time, each on its own
                // thread (a sound's layers no longer wait in line for one another; the effects still come one by one)
                if (key >= 0 && key < keyInsert)
                    for (auto it = queue.begin(); it != queue.end();)
                        if (*it < keyInsert) { layers.push_back (*it); it = queue.erase (it); } else ++it;
            }
            if (key < 0) { collect (false); engine.tendRacks(); wait (120); continue; }
            if (layers.empty()) { engine.build (key); engine.jobsPending.fetch_sub (1); continue; }
            layers.push_back (key);
            std::vector<std::thread> side;
            side.reserve (layers.size() - 1);
            for (size_t i = 1; i < layers.size(); ++i)
                side.emplace_back ([this, k = layers[i]] { engine.build (k); engine.jobsPending.fetch_sub (1); });
            engine.build (layers[0]); engine.jobsPending.fetch_sub (1);
            for (auto& s : side) s.join();
        }
    }
    void drain() const
    {
        const auto t0 = juce::Time::getMillisecondCounter();
        while (engine.jobsPending.load() > 0 && juce::Time::getMillisecondCounter() - t0 < 120000u) juce::Thread::sleep (2);
    }
    // the audio thread hands old objects back here; they are destroyed a moment later, off the audio thread
    void retire (void* p, int type) noexcept
    {
        int s1, z1, s2, z2;
        fifo.prepareToWrite (1, s1, z1, s2, z2);
        if (z1 + z2 < 1) return;                      // (not in practice: 2048 places, emptied several times a second)
        ring[(size_t) (z1 > 0 ? s1 : s2)] = { p, type };
        fifo.finishedWrite (1);
    }
    void age (void* p, int type) { if (p != nullptr) { const std::lock_guard<std::mutex> g (agingLock); aging.push_back ({ { p, type }, juce::Time::getMillisecondCounter() }); } }   // loader threads
    void collect (bool everything)
    {
        const std::lock_guard<std::mutex> g (agingLock);
        int s1, z1, s2, z2;
        fifo.prepareToRead (fifo.getNumReady(), s1, z1, s2, z2);
        const auto now = juce::Time::getMillisecondCounter();
        for (int i = 0; i < z1; ++i) aging.push_back ({ ring[(size_t) (s1 + i)], now });
        for (int i = 0; i < z2; ++i) aging.push_back ({ ring[(size_t) (s2 + i)], now });
        fifo.finishedRead (z1 + z2);
        for (size_t i = 0; i < aging.size();)
        {
            if (everything || now - aging[i].when > 2000u)
            {
                if (aging[i].item.second == 0) delete static_cast<SourceRt*> (aging[i].item.first); else delete static_cast<SlotRt*> (aging[i].item.first);
                aging.erase (aging.begin() + (std::ptrdiff_t) i);
            }
            else ++i;
        }
    }
    Engine& engine;
    juce::CriticalSection lock;
    std::vector<int> queue;
    juce::AbstractFifo fifo { 2048 };
    std::array<std::pair<void*, int>, 2048> ring {};
    struct Aged { std::pair<void*, int> item; juce::uint32 when; };
    std::vector<Aged> aging;
    std::mutex agingLock;                             // (the layers' threads hand back what they replaced while the loader collects)
};

// ============================================================================================ the audio thread's own state
struct Engine::Dsp
{
    struct L
    {
        juce::dsp::StateVariableTPTFilter<float> f1, f2;
        float gl = 0.0f, gr = 0.0f, fade = 0.0f; int fadeState = 2;          // 0 open, 1 closing for a swap, 2 opening
        float lfoPhase = 0.0f, driftFrom = 0.0f, driftTo = 0.0f; bool lfoStarted = false;
        float sentAttack = -1.0f, sentUp = -1.0f, sentDown = -1.0f, sentStart = -1.0f, sentPitchUp = -1.0f, sentPitchDown = -1.0f, sentBpm = 0.0f;
        float cutoff = 20000.0f, width = 1.0f; int filterType = -1;
        std::array<float, kSends> sendNow {};
        std::array<juce::uint8, 128> held {};
        bool wasOn = false, fresh = true, idle = false, toldOffline = false;
        int quiet = 0;
        // the layer's own delay line: it is held back by lagNow samples to meet the slowest layer (crossing over from
        // lagFrom when that changes, so nothing jumps)
        std::vector<float> lineL, lineR;
        int linePos = 0, lagNow = 0, lagFrom = 0; float lagMix = 1.0f; bool lineLive = false;
        void arrive() noexcept     // a new instrument stands here: it knows nothing yet
        {
            sentAttack = sentUp = sentDown = sentStart = sentPitchUp = sentPitchDown = -1.0f; sentBpm = 0.0f;
            held.fill (0); fresh = true; idle = false; quiet = 0; f1.reset(); f2.reset();
        }
    };
    static constexpr int kAlignMax = 1 << 15;        // the longest a layer can be held back (a power of two)
    std::array<L, kLayers> layer;
    juce::AudioBuffer<float> buf;
    std::array<juce::AudioBuffer<float>, kSends> sendBuf;
    std::array<float, kSends> sendFade { { 0, 0, 0 } }, sendOn { { 0, 0, 0 } }, retNow { { 0, 0, 0 } };
    std::array<int, kSends> sendFadeState { { 2, 2, 2 } };
    float masterFade = 0.0f, masterGain = 1.0f, wheel = 0.0f;
    int masterFadeState = 2;
    std::array<float, 128> ccValue {}; std::array<bool, 128> ccSeen {};
    juce::MidiBuffer midi;
    juce::Random rng { 0x47686f73 };
    struct Aud { juce::int64 on = -1, off = -1; int note = 60; float vel = 0.7f; bool sounding = false; };
    std::array<Aud, 24> aud;
    juce::int64 clock = 0;

    void prepare (double sr, bool offline)
    {
        buf.setSize (2, kBlock, false, true, true);
        for (auto& b : sendBuf) b.setSize (2, kBlock, false, true, true);
        midi.ensureSize (16384);
        const juce::dsp::ProcessSpec spec { sr, (juce::uint32) kBlock, 2 };
        for (auto& l : layer) { l.f1.prepare (spec); l.f2.prepare (spec); l.filterType = -1; l.arrive(); l.gl = l.gr = 0.0f; l.sendNow.fill (0.0f); }
        for (auto& l : layer) { l.lineL.assign ((size_t) kAlignMax, 0.0f); l.lineR.assign ((size_t) kAlignMax, 0.0f); l.linePos = l.lagNow = l.lagFrom = 0; l.lagMix = 1.0f; l.lineLive = false; }
        if (offline)
        {   // nobody is listening to a swap offline: everything starts open
            masterFade = 1.0f; masterFadeState = 0;
            for (int k = 0; k < kSends; ++k) { sendFade[(size_t) k] = 1.0f; sendFadeState[(size_t) k] = 0; }
            for (auto& l : layer) { l.fade = 1.0f; l.fadeState = 0; }
        }
    }
};

// ============================================================================================ life
Engine::Engine()
{
    for (auto& l : lp) { for (int i = 0; i < kNumLayerParams; ++i) l[(size_t) i].store (layerParam (i).def); l[lpOn].store (0.0f); }
    for (auto& r : returnLevel) r.store (0.7f);
    for (auto& m : macroValue) m.store (0.0f);
    for (auto& p : layerPeak) p.store (0.0f);
    for (auto& p : lfoOut) p.store (0.0f);
    for (auto& p : outPeak) p.store (0.0f);
    for (auto& s : scope) s.store (0.0f);
    current = Preset::blank();
    for (auto& s : sourceAsked) s = "?";
}

Engine::~Engine()
{
    loader.reset();     // stops the thread and destroys whatever was handed back
    for (auto& c : source) { delete c.pending.exchange (nullptr); delete c.live.exchange (nullptr); }
    auto drop = [] (Cell<SlotRt>& c) { delete c.pending.exchange (nullptr); delete c.live.exchange (nullptr); };
    for (auto& l : insert) for (auto& c : l) drop (c);
    for (auto& c : send) drop (c);
    for (auto& c : masterSlot) drop (c);
}

void Engine::setOffline (bool on, bool withEffects) { offline = on; effects = withEffects; }

void Engine::prepare (double sampleRate)
{
    if (loader != nullptr) loader->drain();
    const double rate = sampleRate > 0 ? sampleRate : 48000.0;
    const bool rateChanged = prepared && std::abs (rate - sr) > 0.5;
    sr = rate;
    if (dsp == nullptr) dsp = std::make_unique<Dsp>();
    if (! prepared || rateChanged) dsp->prepare (sr, offline);
    if (! prepared)
    {
        prepared = true;
        if (! offline) loader = std::make_unique<Loader> (*this);
        setPreset (getPreset());        // everything is asked for now
        return;
    }
    if (! rateChanged) return;
    // the audio is stopped and the loader is idle: everything is told the new rate where it stands
    for (auto& c : source)
        for (auto* rt : { c.pending.load(), c.live.load() })
            if (rt != nullptr && rt->sfz != nullptr) { rt->sfz->allSoundOff(); rt->sfz->prepare (sr, kBlock); }
    auto again = [this] (Cell<SlotRt>& c) {
        for (auto* rt : { c.pending.load(), c.live.load() })
            if (rt != nullptr && rt->slot != nullptr)
            {
                while (rt->busy.test_and_set (std::memory_order_acquire)) juce::Thread::yield();
                rt->slot->prepare (sr, kBlock, 2);
                rt->latency.store (rt->slot->getLatencySamples());
                rt->busy.clear (std::memory_order_release);
            }
    };
    for (auto& l : insert) for (auto& c : l) again (c);
    for (auto& c : send) again (c);
    for (auto& c : masterSlot) again (c);
    updateLatency();
}

void Engine::retire (void* p, int type) noexcept
{
    if (p == nullptr) return;
    if (loader != nullptr) { loader->retire (p, type); return; }
    if (type == 0) delete static_cast<SourceRt*> (p); else delete static_cast<SlotRt*> (p);
}

void Engine::waitUntilLoaded (int timeoutMs) const
{
    const auto t0 = juce::Time::getMillisecondCounter();
    while (jobsPending.load() > 0 && (int) (juce::Time::getMillisecondCounter() - t0) < timeoutMs) juce::Thread::sleep (3);
}

// ============================================================================================ building
void Engine::applyValues (SlotRt& rt, const FxState& fx) const
{
    if (rt.slot == nullptr) return;
    for (size_t i = 0; i < rt.specs.size(); ++i)
    {
        const auto& s = rt.specs[i];
        const auto it = fx.values.find (juce::String (s.id));
        rt.slot->setValue ((int) i, it != fx.values.end() ? juce::jlimit (s.min, s.max, it->second) : s.def);
    }
    // a send is always running (its own level ramp switches it); an insert or master unit uses the slot's click-free bypass
    rt.slot->setEnabled (rt.isSend ? true : fx.on);
    rt.slot->setMix (rt.isSend ? 1.0f : juce::jlimit (0.0f, 1.0f, fx.mix));
    rt.on.store (fx.on);
}

// a stepped control (a mode, a switch, the ENGINE menu) can move a unit's latency: the slot is asked again, with nobody else inside it
void Engine::reask (SlotRt& rt) const
{
    if (rt.slot == nullptr) return;
    while (rt.busy.test_and_set (std::memory_order_acquire)) juce::Thread::yield();
    rt.slot->applyPolicy();
    rt.latency.store (rt.slot->getLatencySamples());
    rt.busy.clear (std::memory_order_release);
}

std::unique_ptr<Engine::SlotRt> Engine::buildSlot (const FxState& fx, bool asSend) const
{
    auto rt = std::make_unique<SlotRt>();
    rt->unit = fx.unit;
    rt->isSend = asSend;
    if (fx.unit.isEmpty() || ! effects) return rt;
    auto effect = makeUnit (fx.unit);
    if (effect == nullptr) return rt;
    rt->slot = std::make_unique<phantom::EffectSlot> (std::move (effect));
    rt->specs = rt->slot->params();
    atlasDefaults (fx.unit, rt->specs);
    rt->slot->setPolicy (phantom::OversamplingPolicy::X2);
    applyValues (*rt, fx);
    rt->slot->prepare (sr, kBlock, 2);
    if (! fx.data.isVoid() && rt->slot->fx().hasData()) rt->slot->fx().setData (fx.data);
    applyValues (*rt, fx);
    rt->slot->applyPolicy();
    rt->latency.store (rt->slot->getLatencySamples());
    return rt;
}

std::unique_ptr<Engine::SourceRt> Engine::buildSource (const LayerState& l, juce::String& problem) const
{
    auto rt = std::make_unique<SourceRt>();
    rt->signature = sourceSignature (l);
    problem.clear();
    if (! l.used()) return rt;
    const auto path = resolver ? resolver (l.instrument) : juce::String();
    if (path.isEmpty()) { problem = "This instrument is not in the library: " + l.instrument; return rt; }
    auto sfz = std::make_unique<SfzLayer>();
    sfz->prepare (sr, kBlock);
    sfz->setVoices (juce::jlimit (4, 128, l.voices));
    sfz->setQuality (3, 5);
    if (offline) sfz->setOffline (true);
    SfzShape shape;
    shape.startRange = juce::jmax (0.0f, l.startRange);
    shape.loopMode = l.loopMode; shape.loopStart = l.loopStart; shape.loopEnd = l.loopEnd; shape.loopCrossfade = l.loopFade;
    shape.reverse = l.reverse;
    const int coarse = (int) l.p[lpCoarse];
    {   // crossfades, in the keys the instrument hears (after the layer's own transposition): the first key of the
        // range is already faintly there, the last one is not yet gone
        const int lo = (int) l.p[lpKeyLo], hi = (int) l.p[lpKeyHi];
        if (l.keyFadeIn >= 1.0f) { shape.xfKeyInLo = lo - 1 + coarse; shape.xfKeyInHi = juce::jmin (hi, lo + (int) l.keyFadeIn) + coarse; }
        if (l.keyFadeOut >= 1.0f) { shape.xfKeyOutLo = juce::jmax (lo, hi - (int) l.keyFadeOut) + coarse; shape.xfKeyOutHi = hi + 1 + coarse; }
    }
    {   // and in the velocities the instrument hears (after TOUCH and CURVE)
        const int lo = (int) l.p[lpVelLo], hi = (int) l.p[lpVelHi];
        auto heard = [&l] (int v) { return (int) std::lround (127.0f * touched ((float) v / 127.0f, l.p[lpVelSens], l.p[lpVelCurve])); };
        if (l.velFadeIn >= 1.0f) { const int a = heard (lo - 1), b = heard (juce::jmin (hi, lo + (int) l.velFadeIn)); if (b > a) { shape.xfVelInLo = a; shape.xfVelInHi = b; } }
        if (l.velFadeOut >= 1.0f) { const int a = heard (juce::jmax (lo, hi - (int) l.velFadeOut)), b = heard (hi + 1); if (b > a) { shape.xfVelOutLo = a; shape.xfVelOutHi = b; } }
    }
    if (! sfz->load (path.toStdString(), shape)) { problem = juce::String (sfz->info().error); return rt; }
    rt->startHook = shape.startRange > 0.0f;
    rt->sfz = std::move (sfz);
    return rt;
}

Engine::Cell<Engine::SlotRt>& Engine::cellFor (FxAddress w)
{
    if (w.kind == FxAddress::Insert) return insert[(size_t) juce::jlimit (0, kLayers - 1, w.layer)][(size_t) juce::jlimit (0, kInserts - 1, w.slot)];
    if (w.kind == FxAddress::Send) return send[(size_t) juce::jlimit (0, kSends - 1, w.slot)];
    return masterSlot[(size_t) juce::jlimit (0, kMasterSlots - 1, w.slot)];
}
FxState& Engine::stateFor (FxAddress w)
{
    if (w.kind == FxAddress::Insert) return current.layers[(size_t) juce::jlimit (0, kLayers - 1, w.layer)].inserts[(size_t) juce::jlimit (0, kInserts - 1, w.slot)];
    if (w.kind == FxAddress::Send) return current.sends[(size_t) juce::jlimit (0, kSends - 1, w.slot)];
    return current.master[(size_t) juce::jlimit (0, kMasterSlots - 1, w.slot)];
}

// builds what `key` names from the settings as they are now, and offers it to the audio thread
void Engine::build (int key)
{
    if (key < keyInsert)
    {
        const int layer = juce::jlimit (0, kLayers - 1, key);
        LayerState ls;
        { const juce::ScopedLock sl (stateLock); ls = current.layers[(size_t) layer]; }
        juce::String problem;
        auto rt = buildSource (ls, problem);
        const juce::ScopedLock sl (stateLock);
        if (sourceSignature (current.layers[(size_t) layer]) != rt->signature) return;      // it moved on meanwhile: the newer request is already waiting
        problems[(size_t) layer] = problem;
        auto& c = source[(size_t) layer];
        auto* old = c.pending.exchange (rt.release());
        if (loader != nullptr) { loader->age (old, 0); return; }
        delete old;
        delete c.live.exchange (c.pending.exchange (nullptr));
        if (dsp != nullptr) dsp->layer[(size_t) layer].arrive();
        return;
    }
    const auto w = addressOf (key);
    FxState fx;
    { const juce::ScopedLock sl (stateLock); fx = stateFor (w); }
    auto rt = buildSlot (fx, w.kind == FxAddress::Send);
    const juce::ScopedLock sl (stateLock);
    const auto& now = stateFor (w);
    if (now.unit != rt->unit) return;
    if (rt->slot != nullptr) { applyValues (*rt, now); rt->slot->applyPolicy(); rt->latency.store (rt->slot->getLatencySamples()); }
    auto& c = cellFor (w);
    auto* old = c.pending.exchange (rt.release());
    if (loader != nullptr) { loader->age (old, 1); return; }
    delete old;
    delete c.live.exchange (c.pending.exchange (nullptr));
    updateLatency();
}

void Engine::request (int key)
{
    if (! prepared) return;
    if (loader != nullptr) loader->request (key); else build (key);
}

bool Engine::rackSpectrum (FxAddress w, std::vector<float>& magnitudes, double& rate, bool before)
{
    auto* rt = cellFor (w).live.load();
    return rt != nullptr && rt->slot != nullptr && rt->unit == "spectral" && spectralSpectrum (rt->slot->fx(), magnitudes, rate, before);
}

// SPECTRAL FX racks, looked in on by the loader between its jobs: each frees what the audio thread has handed back and
// builds anything still asked for; one whose latency has moved is asked again
void Engine::tendRacks()
{
    bool master = false;
    auto tend = [this, &master] (Cell<SlotRt>& c, bool isMaster) {
        for (SlotRt* rt : { c.pending.load(), c.live.load() })
            if (rt != nullptr && rt->slot != nullptr && rt->unit == "spectral" && spectralService (rt->slot->fx())) { reask (*rt); master |= isMaster; }
    };
    for (auto& l : insert) for (auto& c : l) tend (c, false);
    for (auto& c : send) tend (c, false);
    for (auto& c : masterSlot) tend (c, true);
    if (master) updateLatency();
}

void Engine::updateLatency()
{
    int sum = 0;
    for (auto& c : masterSlot) if (auto* rt = c.live.load()) if (rt->slot != nullptr) sum += rt->latency.load();
    latency.store (sum);
}

// the unit in this place stays; its settings follow the preset (under stateLock)
void Engine::syncSlot (FxAddress w)
{
    const auto& fx = stateFor (w);
    auto& c = cellFor (w);
    for (SlotRt* rt : { c.pending.load(), c.live.load() })
        if (rt != nullptr && rt->unit == fx.unit) { applyValues (*rt, fx); reask (*rt); }
    if (w.kind == FxAddress::Master) updateLatency();
}

// ============================================================================================ the sound, from the message thread
// what a macro moves, as one number: layer * 100 + its control, or kReturnCode + the send whose return it moves
// (well clear of any layer's numbers, however many layers there are)
static constexpr int kReturnCode = 1 << 20;
void Engine::compileMacros()
{
    for (int m = 0; m < kMacros; ++m)
    {
        int k = 0;
        for (const auto& t : current.macros[(size_t) m].targets)
        {
            if (k >= kMacroTargets) break;
            int code = -1;
            if (t.param.startsWithChar ('L') && t.param.containsChar ('.'))
            {
                const int layer = t.param.substring (1).upToFirstOccurrenceOf (".", false, false).getIntValue() - 1;
                const int p = layerParamIndex (t.param.fromFirstOccurrenceOf (".", false, false));
                if (layer >= 0 && layer < kLayers && p >= 0) code = layer * 100 + p;
            }
            else if (t.param.startsWithChar ('R')) { const int s = t.param.substring (1).getIntValue() - 1; if (s >= 0 && s < kSends) code = kReturnCode + s; }
            if (code < 0) continue;
            macroMap[(size_t) m][(size_t) k].amount.store (t.amount);
            macroMap[(size_t) m][(size_t) k].code.store (code);
            ++k;
        }
        for (; k < kMacroTargets; ++k) macroMap[(size_t) m][(size_t) k].code.store (-1);
    }
}

void Engine::setPreset (const Preset& p)
{
    std::vector<int> ask;
    {
        const juce::ScopedLock sl (stateLock);
        const Preset old = current;
        current = p;
        for (int l = 0; l < kLayers; ++l)
        {
            const auto& nl = current.layers[(size_t) l];
            for (int i = 0; i < kNumLayerParams; ++i) lp[(size_t) l][(size_t) i].store (nl.p[(size_t) i]);
            const auto sig = sourceSignature (nl);
            if (sig != sourceAsked[(size_t) l]) { if (prepared) sourceAsked[(size_t) l] = sig; ask.push_back (l); }
        }
        for (int k = 0; k < kSends; ++k) returnLevel[(size_t) k].store (p.returns[(size_t) k]);
        masterDb.store (p.masterDb);
        for (int m = 0; m < kMacros; ++m) macroValue[(size_t) m].store (p.macros[(size_t) m].value);
        compileMacros();
        if (prepared)
        {
            // an effect that stays the same unit keeps running (a reverb tail survives the change); only its settings move
            auto place = [&] (FxAddress w, const FxState& was) {
                auto& c = cellFor (w);
                const bool built = c.pending.load() != nullptr || c.live.load() != nullptr;
                if (! built || was.unit != stateFor (w).unit) ask.push_back (keyOf (w)); else syncSlot (w);
            };
            for (int l = 0; l < kLayers; ++l) for (int k = 0; k < kInserts; ++k) place (FxAddress::insert (l, k), old.layers[(size_t) l].inserts[(size_t) k]);
            for (int k = 0; k < kSends; ++k) place (FxAddress::send (k), old.sends[(size_t) k]);
            for (int k = 0; k < kMasterSlots; ++k) place (FxAddress::master (k), old.master[(size_t) k]);
        }
    }
    for (int key : ask) request (key);
}

Preset Engine::getPreset() const
{
    const juce::ScopedLock sl (stateLock);
    Preset p = current;
    // the three kinds of control the host can move on the audio thread are read where they live
    p.masterDb = masterDb.load();
    for (int m = 0; m < kMacros; ++m) p.macros[(size_t) m].value = macroValue[(size_t) m].load();
    for (int l = 0; l < kLayers; ++l) p.layers[(size_t) l].p[lpGain] = lp[(size_t) l][lpGain].load();
    return p;
}

void Engine::setMeta (const Preset& p)
{
    const juce::ScopedLock sl (stateLock);
    current.name = p.name; current.author = p.author; current.category = p.category; current.world = p.world; current.description = p.description; current.tags = p.tags;
    current.factory = p.factory; current.file = p.file;
}

void Engine::setLayerParam (int layer, int param, float value)
{
    if (layer < 0 || layer >= kLayers || param < 0 || param >= kNumLayerParams) return;
    const auto& d = layerParam (param);
    value = juce::jlimit (d.min, d.max, value);
    if (d.kind != ParamDef::Float) value = std::round (value);
    bool reload = false;
    {
        const juce::ScopedLock sl (stateLock);
        auto& l = current.layers[(size_t) layer];
        l.p[(size_t) param] = value;
        const auto sig = sourceSignature (l);
        if (prepared && sig != sourceAsked[(size_t) layer]) { sourceAsked[(size_t) layer] = sig; reload = true; }
    }
    lp[(size_t) layer][(size_t) param].store (value);
    if (reload) request (layer);
}
float Engine::getLayerParam (int layer, int param) const
{
    if (layer < 0 || layer >= kLayers || param < 0 || param >= kNumLayerParams) return 0.0f;
    return lp[(size_t) layer][(size_t) param].load();
}

void Engine::setLayerSource (int layer, const LayerState& s)
{
    if (layer < 0 || layer >= kLayers) return;
    std::vector<int> ask;
    {
        const juce::ScopedLock sl (stateLock);
        auto& l = current.layers[(size_t) layer];
        const auto was = l;
        l = s;
        l.p = was.p;                         // the strip's own controls are set one by one ...
        l.p[lpOn] = s.p[lpOn];               // ... but a layer given an instrument comes on with it
        lp[(size_t) layer][lpOn].store (l.p[lpOn]);
        const auto sig = sourceSignature (l);
        if (prepared && sig != sourceAsked[(size_t) layer]) { sourceAsked[(size_t) layer] = sig; ask.push_back (layer); }
        if (prepared)
            for (int k = 0; k < kInserts; ++k)
            { if (was.inserts[(size_t) k].unit != l.inserts[(size_t) k].unit) ask.push_back (keyOf (FxAddress::insert (layer, k))); else syncSlot (FxAddress::insert (layer, k)); }
    }
    for (int key : ask) request (key);
}

void Engine::setFx (FxAddress w, const FxState& fx)
{
    bool rebuild = false;
    {
        const juce::ScopedLock sl (stateLock);
        auto& st = stateFor (w);
        rebuild = st.unit != fx.unit;
        st = fx;
        if (! rebuild && prepared) syncSlot (w);
    }
    if (rebuild) request (keyOf (w));
}
void Engine::setFxValue (FxAddress w, const juce::String& id, float value)
{
    const juce::ScopedLock sl (stateLock);
    stateFor (w).values[id] = value;
    auto& c = cellFor (w);
    bool master = false;
    for (SlotRt* rt : { c.pending.load(), c.live.load() })
        if (rt != nullptr && rt->slot != nullptr)
            for (size_t i = 0; i < rt->specs.size(); ++i)
                if (id == juce::String (rt->specs[i].id))
                {
                    const float limited = juce::jlimit (rt->specs[i].min, rt->specs[i].max, value);
                    rt->slot->setValue ((int) i, limited);
                    // SPECTRAL FX builds what a control asks for itself, and swaps it in between blocks with a fade: its
                    // slot is asked again only when that moves its latency (asking takes the slot from the audio thread for a block)
                    if (rt->unit == "spectral") { if (spectralSet (rt->slot->fx(), (int) i, limited)) { reask (*rt); master = w.kind == FxAddress::Master; } }
                    else if (! rt->specs[i].isContinuous()) { reask (*rt); master = w.kind == FxAddress::Master; }
                }
    if (master) updateLatency();
}
void Engine::setFxOn (FxAddress w, bool on)
{
    const juce::ScopedLock sl (stateLock);
    stateFor (w).on = on;
    auto& c = cellFor (w);
    for (SlotRt* rt : { c.pending.load(), c.live.load() })
        if (rt != nullptr && rt->slot != nullptr) { if (! rt->isSend) rt->slot->setEnabled (on); rt->on.store (on); }
}
void Engine::setFxMix (FxAddress w, float mix)
{
    mix = clamp01 (mix);
    const juce::ScopedLock sl (stateLock);
    stateFor (w).mix = mix;
    auto& c = cellFor (w);
    for (SlotRt* rt : { c.pending.load(), c.live.load() }) if (rt != nullptr && rt->slot != nullptr && ! rt->isSend) rt->slot->setMix (mix);
}
FxState Engine::getFx (FxAddress w) const { const juce::ScopedLock sl (stateLock); return const_cast<Engine*> (this)->stateFor (w); }
void Engine::setReturn (int s, float level)
{
    if (s < 0 || s >= kSends) return;
    level = juce::jlimit (0.0f, 2.0f, level);
    { const juce::ScopedLock sl (stateLock); current.returns[(size_t) s] = level; }
    returnLevel[(size_t) s].store (level);
}
void Engine::setMasterDb (float db)
{
    db = juce::jlimit (-60.0f, 12.0f, db);
    { const juce::ScopedLock sl (stateLock); current.masterDb = db; }
    masterDb.store (db);
}
void Engine::setMacro (int i, float v)
{
    if (i < 0 || i >= kMacros) return;
    v = clamp01 (v);
    { const juce::ScopedLock sl (stateLock); current.macros[(size_t) i].value = v; }
    macroValue[(size_t) i].store (v);
}
void Engine::setMacros (const std::array<Macro, kMacros>& m)
{
    const juce::ScopedLock sl (stateLock);
    current.macros = m;
    for (int i = 0; i < kMacros; ++i) macroValue[(size_t) i].store (m[(size_t) i].value);
    compileMacros();
}
juce::String Engine::layerProblem (int layer) const { const juce::ScopedLock sl (stateLock); return problems[(size_t) juce::jlimit (0, kLayers - 1, layer)]; }

void Engine::audition (int pattern, int root) { auditionRoot.store (root); auditionRequest.store (juce::jlimit (0, 2, pattern)); }
void Engine::stopAudition() { auditionRequest.store (-2); }
void Engine::panic() { panicFlag.store (true); auditionRequest.store (-2); }
void Engine::noteFromScreen (int note, float velocity, bool on)
{
    int s1, z1, s2, z2;
    screenFifo.prepareToWrite (1, s1, z1, s2, z2);
    if (z1 + z2 < 1) return;
    note = juce::jlimit (0, 127, note);
    screenNotes[(size_t) (z1 > 0 ? s1 : s2)] = on ? juce::MidiMessage::noteOn (1, note, juce::jlimit (0.01f, 1.0f, velocity)) : juce::MidiMessage::noteOff (1, note);
    screenFifo.finishedWrite (1);
}

// ============================================================================================ the audio thread
void Engine::runAudition (juce::MidiBuffer& into, int n)
{
    auto& d = *dsp;
    const int req = auditionRequest.exchange (-1);
    if (req == -2 || req >= 0)
        for (auto& a : d.aud) { if (a.sounding) into.addEvent (juce::MidiMessage::noteOff (1, a.note), 0); a = Dsp::Aud(); }
    if (req >= 0)
    {
        struct Step { float at, len; int semi; float vel; };
        static const Step chord[] = { { 0.00f, 3.6f, -12, 0.62f }, { 0.04f, 3.6f, 0, 0.66f }, { 0.08f, 3.6f, 7, 0.60f }, { 0.12f, 3.6f, 11, 0.58f }, { 0.16f, 3.6f, 16, 0.62f } };
        static const Step arp[] = { { 0.00f, 0.5f, 0, 0.74f }, { 0.20f, 0.5f, 7, 0.62f }, { 0.40f, 0.5f, 12, 0.70f }, { 0.60f, 0.5f, 16, 0.64f }, { 0.80f, 0.5f, 19, 0.72f },
                                    { 1.00f, 0.5f, 16, 0.60f }, { 1.20f, 0.5f, 12, 0.66f }, { 1.40f, 0.5f, 7, 0.58f }, { 1.60f, 2.4f, 0, 0.70f }, { 1.62f, 2.4f, 12, 0.60f }, { 1.64f, 2.4f, 19, 0.56f } };
        static const Step line[] = { { 0.00f, 0.55f, 0, 0.74f }, { 0.60f, 0.25f, 3, 0.62f }, { 0.90f, 0.25f, 5, 0.66f }, { 1.20f, 0.85f, 7, 0.76f }, { 2.10f, 0.25f, 10, 0.62f },
                                     { 2.40f, 0.25f, 7, 0.60f }, { 2.70f, 1.6f, 12, 0.72f } };
        const Step* steps = req == 1 ? arp : (req == 2 ? line : chord);
        const size_t count = req == 1 ? std::size (arp) : (req == 2 ? std::size (line) : std::size (chord));
        const int root = auditionRoot.load();
        for (size_t k = 0; k < count && k < d.aud.size(); ++k)
        {
            auto& a = d.aud[k];
            a.on = d.clock + (juce::int64) (steps[k].at * sr); a.off = a.on + (juce::int64) (steps[k].len * sr);
            a.note = juce::jlimit (0, 127, root + steps[k].semi); a.vel = steps[k].vel; a.sounding = false;
        }
    }
    for (auto& a : d.aud)
    {
        if (a.on < 0) continue;
        if (! a.sounding && a.on < d.clock + n) { into.addEvent (juce::MidiMessage::noteOn (1, a.note, a.vel), (int) juce::jlimit<juce::int64> (0, n - 1, a.on - d.clock)); a.sounding = true; }
        if (a.sounding && a.off < d.clock + n) { into.addEvent (juce::MidiMessage::noteOff (1, a.note), (int) juce::jlimit<juce::int64> (0, n - 1, a.off - d.clock)); a = Dsp::Aud(); }
    }
}

void Engine::process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midiIn, const phantom::TempoInfo& tempo)
{
    const int total = out.getNumSamples();
    out.clear();
    if (! prepared || dsp == nullptr || total <= 0 || out.getNumChannels() < 2) return;
    auto& d = *dsp;
    // An export does not wait for anybody, so when the host renders faster than real time the engine finishes loading
    // first. (In real time nothing ever waits here: a note played while its instrument is still loading is not heard.)
    if (! offline && hostOffline.load (std::memory_order_relaxed) && jobsPending.load (std::memory_order_relaxed) > 0) waitUntilLoaded (60000);
    const bool panicNow = panicFlag.exchange (false);
    float* L = out.getWritePointer (0); float* R = out.getWritePointer (1);
    for (int pos = 0; pos < total; pos += kBlock)
    {
        const int n = juce::jmin (kBlock, total - pos);
        d.midi.clear();
        d.midi.addEvents (midiIn, pos, n, -pos);
        if (pos == 0)
        {   // the screen's keyboard
            int s1, z1, s2, z2;
            screenFifo.prepareToRead (screenFifo.getNumReady(), s1, z1, s2, z2);
            for (int i = 0; i < z1; ++i) d.midi.addEvent (screenNotes[(size_t) (s1 + i)], 0);
            for (int i = 0; i < z2; ++i) d.midi.addEvent (screenNotes[(size_t) (s2 + i)], 0);
            screenFifo.finishedRead (z1 + z2);
        }
        runAudition (d.midi, n);
        phantom::TempoInfo t = tempo;
        t.notes = nullptr; t.numNotes = 0; t.sampleRate = sr;
        t.ppqPosition = tempo.ppqPosition + (double) pos / t.samplesPerBeat(); t.ppqAtBlockStart = t.ppqPosition;
        processChunk (L + pos, R + pos, n, d.midi, t, panicNow && pos == 0);
        d.clock += n;
    }
}

void Engine::processChunk (float* outL, float* outR, int n, const juce::MidiBuffer& midi, const phantom::TempoInfo& tempo, bool panicNow)
{
    auto& d = *dsp;
    const float fadeStep = (float) n / (float) (0.012 * sr);
    const float inv = 1.0f / (float) n;

    // ---- every control, with the macros folded in
    float P[kLayers][kNumLayerParams];
    float adj[kLayers][kNumLayerParams];
    float retAdj[kSends] = {};
    bool anyAdj = false;
    for (int m = 0; m < kMacros; ++m)
    {
        const float mv = macroValue[(size_t) m].load (std::memory_order_relaxed);
        if (mv <= 0.0f) continue;
        for (int k = 0; k < kMacroTargets; ++k)
        {
            const int code = macroMap[(size_t) m][(size_t) k].code.load (std::memory_order_relaxed);
            if (code < 0) break;
            const float amt = macroMap[(size_t) m][(size_t) k].amount.load (std::memory_order_relaxed) * mv;
            if (code >= kReturnCode) { retAdj[juce::jlimit (0, kSends - 1, code - kReturnCode)] += amt; continue; }
            if (! anyAdj) { std::memset (adj, 0, sizeof (adj)); anyAdj = true; }
            adj[juce::jlimit (0, kLayers - 1, code / 100)][juce::jlimit (0, kNumLayerParams - 1, code % 100)] += amt;
        }
    }
    bool anySolo = false;
    for (int l = 0; l < kLayers; ++l)
    {
        for (int i = 0; i < kNumLayerParams; ++i)
        {
            float v = lp[(size_t) l][(size_t) i].load (std::memory_order_relaxed);
            if (anyAdj && adj[l][i] != 0.0f) { const auto& def = layerParam (i); v = fromNorm (def, toNorm (def, v) + adj[l][i]); }
            P[l][i] = v;
        }
        if (P[l][lpSolo] > 0.5f && P[l][lpOn] > 0.5f) anySolo = true;
    }

    const float bpm = (float) juce::jlimit (20.0, 400.0, tempo.bpm);
    const float wheelBefore = d.wheel;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isPitchWheel()) d.wheel = ((float) m.getPitchWheelValue() - 8192.0f) / 8192.0f;
        else if (m.isController()) { const int cc = m.getControllerNumber(); d.ccValue[(size_t) cc] = (float) m.getControllerValue() / 127.0f; d.ccSeen[(size_t) cc] = true; }
    }

    int voicesNow = 0;
    const bool hostIsOffline = ! offline && hostOffline.load (std::memory_order_relaxed);    // (a bounce of our own has its recordings read beforehand)

    // ---- lining the layers up. An effect on a layer can make it speak late (VOICE SHIFT works on a window of sound: 60
    //      or 120 ms). The most any layer in use is held back by its own two effects is what every other layer is held
    //      back to meet while the host's timeline runs, and the host is told that much more latency.
    int ownLag[kLayers]; int mostLag = 0;
    for (int l = 0; l < kLayers; ++l)
    {
        ownLag[l] = 0;
        const SourceRt* there = source[(size_t) l].live.load (std::memory_order_relaxed);
        if (there == nullptr || there->sfz == nullptr || P[l][lpOn] <= 0.5f) continue;
        for (int k = 0; k < kInserts; ++k)
            if (const SlotRt* rt = insert[(size_t) l][(size_t) k].live.load (std::memory_order_relaxed))
                if (rt->slot != nullptr) ownLag[l] += rt->latency.load (std::memory_order_relaxed);
        mostLag = juce::jmax (mostLag, ownLag[l]);
    }
    mostLag = juce::jmin (mostLag, Dsp::kAlignMax - (int) kBlock - 1);
    if (mostLag != alignBy.load (std::memory_order_relaxed)) alignBy.store (mostLag, std::memory_order_relaxed);
    const bool liningUp = mostLag > 0 && timeline.load (std::memory_order_relaxed);
    for (int k = 0; k < kSends; ++k) d.sendBuf[(size_t) k].clear (0, n);
    float* bl = d.buf.getWritePointer (0); float* br = d.buf.getWritePointer (1);

    // =========================================================== the eight layers
    for (int l = 0; l < kLayers; ++l)
    {
        auto& ld = d.layer[(size_t) l];
        auto& sc = source[(size_t) l];
        const float* p = P[l];
        SourceRt* live = sc.live.load (std::memory_order_relaxed);

        // a new instrument or effect is waiting: fade out, swap, fade in (a silent layer swaps at once)
        bool waiting = sc.pending.load (std::memory_order_acquire) != nullptr;
        for (int k = 0; k < kInserts; ++k) waiting = waiting || insert[(size_t) l][(size_t) k].pending.load (std::memory_order_acquire) != nullptr;
        if (waiting)
        {
            const bool silent = live == nullptr || live->sfz == nullptr || ld.idle || p[lpOn] <= 0.5f;
            if (ld.fade <= 0.0f || silent)
            {
                if (auto* np = sc.pending.exchange (nullptr, std::memory_order_acq_rel))
                { retire (live, 0); sc.live.store (np, std::memory_order_release); live = np; ld.arrive(); }
                for (int k = 0; k < kInserts; ++k)
                {
                    auto& ic = insert[(size_t) l][(size_t) k];
                    if (auto* np = ic.pending.exchange (nullptr, std::memory_order_acq_rel))
                    { retire (ic.live.load (std::memory_order_relaxed), 1); ic.live.store (np, std::memory_order_release); ld.idle = false; ld.quiet = 0; }
                }
                ld.fade = 0.0f; ld.fadeState = 2;
            }
            else ld.fadeState = 1;
        }

        SfzLayer* sfz = live != nullptr ? live->sfz.get() : nullptr;
        if (sfz == nullptr || p[lpOn] <= 0.5f)
        {
            if (sfz != nullptr && ld.wasOn) { sfz->allSoundOff(); ld.held.fill (0); }
            ld.wasOn = false; ld.gl = ld.gr = 0.0f; ld.sendNow.fill (0.0f);
            layerPeak[(size_t) l].store (layerPeak[(size_t) l].load (std::memory_order_relaxed) * 0.8f, std::memory_order_relaxed);
            continue;
        }
        ld.wasOn = true;
        if (panicNow) { sfz->allSoundOff(); ld.held.fill (0); }

        // ---- a new arrival is told where the wheels stand, and whether the host is rendering faster than real time
        if (ld.fresh || hostIsOffline != ld.toldOffline) { sfz->setHostOffline (hostIsOffline); ld.toldOffline = hostIsOffline; }
        if (ld.fresh)
        {
            for (int cc = 0; cc < 128; ++cc) if (d.ccSeen[(size_t) cc]) sfz->controller (0, cc, d.ccValue[(size_t) cc]);
            sfz->pitchWheel (0, wheelBefore);
        }
        if (bpm != ld.sentBpm) { sfz->tempo (bpm); ld.sentBpm = bpm; }

        // ---- the layer's LFO (one value for this piece; whatever it moves glides to it)
        float lfo = 0.0f;
        {
            static const float beats[10] = { 32.0f, 16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 1.0f / 3.0f, 0.25f, 0.125f };
            const float hz = p[lpLfoSync] > 0.5f ? (bpm / 60.0f) / beats[juce::jlimit (0, 9, (int) p[lpLfoDiv])] : p[lpLfoRate];
            ld.lfoPhase += hz * (float) n / (float) sr;
            const bool wrapped = ld.lfoPhase >= 1.0f || ! ld.lfoStarted;
            ld.lfoPhase -= std::floor (ld.lfoPhase);
            const float ph = ld.lfoPhase;
            switch ((int) p[lpLfoShape])
            {
                case 1: lfo = ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph; break;
                case 2: lfo = 2.0f * ph - 1.0f; break;
                case 3: lfo = ph < 0.5f ? 1.0f : -1.0f; break;
                case 4: if (wrapped) { ld.driftFrom = ld.lfoStarted ? ld.driftTo : 0.0f; ld.driftTo = d.rng.nextFloat() * 2.0f - 1.0f; }
                        { const float t = ph * ph * (3.0f - 2.0f * ph); lfo = ld.driftFrom + (ld.driftTo - ld.driftFrom) * t; } break;
                default: lfo = std::sin (ph * juce::MathConstants<float>::twoPi); break;
            }
            ld.lfoStarted = true;
            lfoOut[(size_t) l].store (lfo, std::memory_order_relaxed);
        }

        // ---- the hooks the layer reaches the instrument through (envelope and start act on new notes; pitch glides)
        { const float a = p[lpAttack] * p[lpAttack]; if (a != ld.sentAttack) { sfz->controller (0, kCcAttack, a); ld.sentAttack = a; } }
        {
            const float r = p[lpRelease]; const float up = r > 0 ? r * r : 0.0f, down = r < 0 ? -r : 0.0f;
            if (up != ld.sentUp) { sfz->controller (0, kCcReleaseUp, up); ld.sentUp = up; }
            if (down != ld.sentDown) { sfz->controller (0, kCcReleaseDown, down); ld.sentDown = down; }
        }
        if (live->startHook && p[lpStart] != ld.sentStart) { sfz->controller (0, kCcStart, p[lpStart]); ld.sentStart = p[lpStart]; }
        {
            const float cents = juce::jlimit (-200.0f, 200.0f, p[lpFine] + p[lpLfoPitch] * p[lpLfoPitch] * 100.0f * lfo);
            const float up = cents > 0 ? cents / 200.0f : 0.0f, down = cents < 0 ? -cents / 200.0f : 0.0f;
            const int at = ld.fresh ? 0 : n - 1;
            if (up != ld.sentPitchUp) { sfz->controller (at, kCcPitchUp, up); ld.sentPitchUp = up; }
            if (down != ld.sentPitchDown) { sfz->controller (at, kCcPitchDown, down); ld.sentPitchDown = down; }
        }
        ld.fresh = false;

        // ---- notes
        const int keyLo = (int) p[lpKeyLo], keyHi = (int) p[lpKeyHi], velLo = (int) p[lpVelLo], velHi = (int) p[lpVelHi], coarse = (int) p[lpCoarse];
        bool hadNotes = false;
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            const int pos = juce::jlimit (0, n - 1, meta.samplePosition);
            if (m.isNoteOn())
            {
                const int note = m.getNoteNumber(), vel = m.getVelocity();
                if (note < keyLo || note > keyHi || vel < velLo || vel > velHi) continue;
                const int played = note + coarse;
                if (played < 0 || played > 127) continue;
                if (ld.held[(size_t) note] != 0) sfz->noteOff (pos, ld.held[(size_t) note] - 1, 0.0f);
                ld.held[(size_t) note] = (juce::uint8) (played + 1);
                sfz->noteOn (pos, played, touched (m.getFloatVelocity(), p[lpVelSens], p[lpVelCurve]));
                hadNotes = true;
            }
            else if (m.isNoteOff())
            {
                const int note = m.getNoteNumber();
                if (ld.held[(size_t) note] != 0) { sfz->noteOff (pos, ld.held[(size_t) note] - 1, m.getFloatVelocity()); ld.held[(size_t) note] = 0; hadNotes = true; }
            }
            else if (m.isController()) sfz->controller (pos, m.getControllerNumber(), (float) m.getControllerValue() / 127.0f);
            else if (m.isPitchWheel()) sfz->pitchWheel (pos, ((float) m.getPitchWheelValue() - 8192.0f) / 8192.0f);
            else if (m.isChannelPressure()) sfz->aftertouch (pos, (float) m.getChannelPressureValue() / 127.0f);
            else if (m.isAllNotesOff() || m.isAllSoundOff()) { sfz->allSoundOff(); ld.held.fill (0); }
        }

        // ---- where the strip is heading this piece
        const bool audible = p[lpMute] < 0.5f && (! anySolo || p[lpSolo] > 0.5f);
        const float g = audible ? dbToGain (p[lpGain]) * (1.0f - p[lpLfoAmp] * (0.5f - 0.5f * lfo)) : 0.0f;
        const float pan = juce::jlimit (-1.0f, 1.0f, p[lpPan] + p[lpLfoPan] * lfo);
        const float ang = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        const float tl = g * std::cos (ang) * 1.41421356f, tr = g * std::sin (ang) * 1.41421356f;
        const int type = (int) p[lpFilterType];
        const float cutTarget = juce::jlimit (20.0f, (float) (sr * 0.45), p[lpCutoff] * std::pow (2.0f, p[lpLfoCutoff] * 3.0f * lfo));

        // ---- nothing sounding and nothing ringing: the layer rests
        if (ld.idle)
        {
            if (! hadNotes && sfz->activeVoices() == 0)
            {
                ld.gl = tl; ld.gr = tr; ld.cutoff = cutTarget; ld.width = p[lpWidth];
                for (int k = 0; k < kSends; ++k) { static const int sp[kSends] = { lpSendA, lpSendB, lpSendC }; ld.sendNow[(size_t) k] = p[sp[k]] * p[sp[k]]; }
                if (ld.fadeState == 2) { ld.fade = 1.0f; ld.fadeState = 0; }
                layerPeak[(size_t) l].store (layerPeak[(size_t) l].load (std::memory_order_relaxed) * 0.8f, std::memory_order_relaxed);
                continue;
            }
            ld.idle = false; ld.quiet = 0;
        }

        // ---- the instrument
        sfz->render (bl, br, n);
        const int voicesHere = sfz->activeVoices();
        voicesNow += voicesHere;
        float* chans[2] = { bl, br };
        juce::dsp::AudioBlock<float> block (chans, 2, (size_t) n);

        // ---- its two effect slots
        for (int k = 0; k < kInserts; ++k)
            if (auto* rt = insert[(size_t) l][(size_t) k].live.load (std::memory_order_relaxed))
                if (rt->slot != nullptr && ! rt->busy.test_and_set (std::memory_order_acquire))
                { rt->slot->process (block, tempo); rt->busy.clear (std::memory_order_release); }

        // ---- held back to meet the slowest layer (only while some layer's effects are slow; see above)
        float fedIn = 0.0f;
        {
            const int want = liningUp ? mostLag - juce::jmin (mostLag, ownLag[l]) : 0;
            if (mostLag > 0 || ld.lagNow > 0 || ld.lagMix < 1.0f)
            {
                if (! ld.lineLive) { std::fill (ld.lineL.begin(), ld.lineL.end(), 0.0f); std::fill (ld.lineR.begin(), ld.lineR.end(), 0.0f); ld.lineLive = true; }
                if (want != ld.lagNow && ld.lagMix >= 1.0f) { ld.lagFrom = ld.lagNow; ld.lagNow = want; ld.lagMix = 0.0f; }
                float* lineL = ld.lineL.data(); float* lineR = ld.lineR.data();
                const int mask = Dsp::kAlignMax - 1;
                const float mixStep = 1.0f / (float) (0.03 * sr);
                int at = ld.linePos;
                for (int i = 0; i < n; ++i)
                {
                    fedIn = juce::jmax (fedIn, std::abs (bl[i]), std::abs (br[i]));
                    lineL[at] = bl[i]; lineR[at] = br[i];
                    float a = lineL[(at - ld.lagNow) & mask], b = lineR[(at - ld.lagNow) & mask];
                    if (ld.lagMix < 1.0f)
                    {
                        ld.lagMix = juce::jmin (1.0f, ld.lagMix + mixStep);
                        a = a * ld.lagMix + lineL[(at - ld.lagFrom) & mask] * (1.0f - ld.lagMix);
                        b = b * ld.lagMix + lineR[(at - ld.lagFrom) & mask] * (1.0f - ld.lagMix);
                    }
                    bl[i] = a; br[i] = b;
                    at = (at + 1) & mask;
                }
                ld.linePos = at;
            }
            else ld.lineLive = false;
        }

        // ---- filter (the cutoff glides across the piece in 32-sample steps)
        if (type != ld.filterType)
        {
            using T = juce::dsp::StateVariableTPTFilterType;
            const auto t = type == 3 ? T::highpass : (type == 4 ? T::bandpass : T::lowpass);
            ld.f1.setType (t); ld.f2.setType (t); ld.f1.reset(); ld.f2.reset(); ld.filterType = type;
        }
        if (type != 0)
        {
            const float q = 0.5f * std::pow (24.0f, juce::jlimit (0.0f, 1.0f, p[lpReso]));
            ld.f1.setResonance (q); ld.f2.setResonance (0.7071f);
            const float from = juce::jlimit (20.0f, (float) (sr * 0.45), ld.cutoff);
            for (int pos = 0; pos < n; pos += 32)
            {
                const int m = juce::jmin (32, n - pos);
                const float c = from * std::pow (cutTarget / from, (float) (pos + m) * inv);
                ld.f1.setCutoffFrequency (c); if (type == 2) ld.f2.setCutoffFrequency (c);
                for (int i = pos; i < pos + m; ++i)
                {
                    bl[i] = ld.f1.processSample (0, bl[i]); br[i] = ld.f1.processSample (1, br[i]);
                    if (type == 2) { bl[i] = ld.f2.processSample (0, bl[i]); br[i] = ld.f2.processSample (1, br[i]); }
                }
            }
            ld.f1.snapToZero(); ld.f2.snapToZero();
        }
        ld.cutoff = cutTarget;

        // ---- width
        const float w0 = ld.width, w1 = p[lpWidth];
        if (std::abs (w0 - 1.0f) > 0.001f || std::abs (w1 - 1.0f) > 0.001f)
            for (int i = 0; i < n; ++i)
            {
                const float w = w0 + (w1 - w0) * (float) (i + 1) * inv, mid = 0.5f * (bl[i] + br[i]), side = 0.5f * (bl[i] - br[i]) * w;
                bl[i] = mid + side; br[i] = mid - side;
            }
        ld.width = w1;

        // ---- has it gone quiet? (measured before the fader, so a muted layer rests too)
        {
            float raw = 0.0f;
            for (int i = 0; i < n; ++i) raw = juce::jmax (raw, std::abs (bl[i]), std::abs (br[i]));
            // (what is still on its way through the layer's delay line counts as sound: it rests only once that is silent too)
            if (voicesHere == 0 && raw < 1.0e-6f && fedIn < 1.0e-6f) { ld.quiet += n; if (ld.quiet > (int) (0.3 * sr) + ld.lagNow) { ld.idle = true; ld.lineLive = false; } } else ld.quiet = 0;
        }

        // ---- level, pan, the swap fade; then out to the mix and the three sends
        const float f0 = ld.fade;
        float f1 = f0;
        if (ld.fadeState == 1) f1 = juce::jmax (0.0f, f0 - fadeStep);
        else if (ld.fadeState == 2) { f1 = juce::jmin (1.0f, f0 + fadeStep); if (f1 >= 1.0f) ld.fadeState = 0; }
        float pk = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float t = (float) (i + 1) * inv, f = f0 + (f1 - f0) * t;
            const float a = bl[i] * (ld.gl + (tl - ld.gl) * t) * f, b = br[i] * (ld.gr + (tr - ld.gr) * t) * f;
            bl[i] = a; br[i] = b;
            outL[i] += a; outR[i] += b;
            pk = juce::jmax (pk, std::abs (a), std::abs (b));
        }
        ld.gl = tl; ld.gr = tr; ld.fade = f1;
        layerPeak[(size_t) l].store (juce::jmax (pk, layerPeak[(size_t) l].load (std::memory_order_relaxed) * 0.8f), std::memory_order_relaxed);
        static const int sendParam[kSends] = { lpSendA, lpSendB, lpSendC };
        for (int k = 0; k < kSends; ++k)
        {
            const float s0 = ld.sendNow[(size_t) k], s1 = p[sendParam[k]] * p[sendParam[k]];
            ld.sendNow[(size_t) k] = s1;
            if (s0 <= 0.0f && s1 <= 0.0f) continue;
            float* sl = d.sendBuf[(size_t) k].getWritePointer (0); float* sr2 = d.sendBuf[(size_t) k].getWritePointer (1);
            for (int i = 0; i < n; ++i) { const float s = s0 + (s1 - s0) * (float) (i + 1) * inv; sl[i] += bl[i] * s; sr2[i] += br[i] * s; }
        }
    }
    voiceCount.store (voicesNow, std::memory_order_relaxed);

    // =========================================================== the three sends
    for (int k = 0; k < kSends; ++k)
    {
        auto& c = send[(size_t) k];
        SlotRt* rt = c.live.load (std::memory_order_relaxed);
        float& fade = d.sendFade[(size_t) k]; int& state = d.sendFadeState[(size_t) k]; float& onGain = d.sendOn[(size_t) k];
        if (c.pending.load (std::memory_order_acquire) != nullptr)
        {
            if (fade <= 0.0f || onGain <= 0.0f || rt == nullptr || rt->slot == nullptr)
            { retire (rt, 1); rt = c.pending.exchange (nullptr, std::memory_order_acq_rel); c.live.store (rt, std::memory_order_release); fade = 0.0f; state = 2; onGain = 0.0f; }
            else state = 1;
        }
        const float r0 = d.retNow[(size_t) k], r1 = juce::jlimit (0.0f, 2.0f, returnLevel[(size_t) k].load (std::memory_order_relaxed) + retAdj[k]);
        d.retNow[(size_t) k] = r1;
        const bool on = rt != nullptr && rt->slot != nullptr && rt->on.load (std::memory_order_relaxed);
        if (rt == nullptr || rt->slot == nullptr || (! on && onGain <= 0.0f)) { if (state == 1) fade = 0.0f; continue; }
        if (rt->busy.test_and_set (std::memory_order_acquire)) continue;        // (the message thread is re-asking its latency this very instant)
        float* chans[2] = { d.sendBuf[(size_t) k].getWritePointer (0), d.sendBuf[(size_t) k].getWritePointer (1) };
        juce::dsp::AudioBlock<float> block (chans, 2, (size_t) n);
        rt->slot->process (block, tempo);
        rt->busy.clear (std::memory_order_release);
        const float f0 = fade, o0 = onGain;
        float f1 = f0;
        if (state == 1) f1 = juce::jmax (0.0f, f0 - fadeStep);
        else if (state == 2) { f1 = juce::jmin (1.0f, f0 + fadeStep); if (f1 >= 1.0f) state = 0; }
        const float o1 = on ? juce::jmin (1.0f, o0 + fadeStep) : juce::jmax (0.0f, o0 - fadeStep);
        fade = f1; onGain = o1;
        for (int i = 0; i < n; ++i)
        {
            const float t = (float) (i + 1) * inv, gk = (r0 + (r1 - r0) * t) * (f0 + (f1 - f0) * t) * (o0 + (o1 - o0) * t);
            outL[i] += chans[0][i] * gk; outR[i] += chans[1][i] * gk;
        }
    }

    // =========================================================== the master chain
    bool masterWaiting = false;
    for (auto& c : masterSlot) masterWaiting = masterWaiting || c.pending.load (std::memory_order_acquire) != nullptr;
    if (masterWaiting)
    {
        if (d.masterFade <= 0.0f)
        {
            int sum = 0;
            for (auto& c : masterSlot)
            {
                if (auto* np = c.pending.exchange (nullptr, std::memory_order_acq_rel)) { retire (c.live.load (std::memory_order_relaxed), 1); c.live.store (np, std::memory_order_release); }
                if (auto* rt = c.live.load (std::memory_order_relaxed)) if (rt->slot != nullptr) sum += rt->latency.load (std::memory_order_relaxed);
            }
            latency.store (sum, std::memory_order_relaxed);
            d.masterFadeState = 2;
        }
        else d.masterFadeState = 1;
    }
    {
        // the master level comes first, so the limiter at the end of the chain always has the last word
        const float g1 = dbToGain (masterDb.load (std::memory_order_relaxed)), g0 = d.masterGain;
        d.masterGain = g1;
        if (g0 != 1.0f || g1 != 1.0f)
            for (int i = 0; i < n; ++i) { const float gk = g0 + (g1 - g0) * (float) (i + 1) * inv; outL[i] *= gk; outR[i] *= gk; }
        float* chans[2] = { outL, outR };
        juce::dsp::AudioBlock<float> block (chans, 2, (size_t) n);
        for (auto& c : masterSlot)
            if (auto* rt = c.live.load (std::memory_order_relaxed))
                if (rt->slot != nullptr && ! rt->busy.test_and_set (std::memory_order_acquire))
                { rt->slot->process (block, tempo); rt->busy.clear (std::memory_order_release); }
        const float f0 = d.masterFade;
        float f1 = f0;
        if (d.masterFadeState == 1) f1 = juce::jmax (0.0f, f0 - fadeStep);
        else if (d.masterFadeState == 2) { f1 = juce::jmin (1.0f, f0 + fadeStep); if (f1 >= 1.0f) d.masterFadeState = 0; }
        d.masterFade = f1;
        float pl = 0.0f, pr = 0.0f;
        const int scopeFrom = scopeAt.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            const float gk = f0 + (f1 - f0) * (float) (i + 1) * inv;
            float a = outL[i] * gk, b = outR[i] * gk;
            if (! (std::abs (a) < 4.0f)) a = a > 0 ? 4.0f : (a < 0 ? -4.0f : 0.0f);     // nothing unreasonable (or not a number) ever leaves
            if (! (std::abs (b) < 4.0f)) b = b > 0 ? 4.0f : (b < 0 ? -4.0f : 0.0f);
            outL[i] = a; outR[i] = b;
            pl = juce::jmax (pl, std::abs (a)); pr = juce::jmax (pr, std::abs (b));
            scope[(size_t) ((scopeFrom + i) & (kScope - 1))].store ((a + b) * 0.5f, std::memory_order_relaxed);
        }
        scopeAt.store ((scopeFrom + n) & (kScope - 1), std::memory_order_relaxed);
        outPeak[0].store (juce::jmax (pl, outPeak[0].load (std::memory_order_relaxed) * 0.9f), std::memory_order_relaxed);
        outPeak[1].store (juce::jmax (pr, outPeak[1].load (std::memory_order_relaxed) * 0.9f), std::memory_order_relaxed);
    }
}

// ============================================================================================ offline
juce::AudioBuffer<float> Engine::bounce (const Preset& p, const Resolver& resolver, const std::vector<Note>& notes, double seconds, double sampleRate, bool withEffects, bool onTimeline)
{
    const int total = juce::jmax (1, (int) (seconds * sampleRate));
    juce::AudioBuffer<float> out (2, total);
    out.clear();
    Engine e;
    e.setResolver (resolver);
    e.setOffline (true, withEffects);
    e.setOnTimeline (onTimeline);
    e.setPreset (p);                    // (before prepare: everything is then built once, from this preset)
    e.prepare (sampleRate);
    // every layer reads in, whole, the recordings these notes will play (see SfzLayer::primeFor)
    for (int l = 0; l < kLayers; ++l)
    {
        auto* rt = e.source[(size_t) l].live.load();
        if (rt == nullptr || rt->sfz == nullptr) continue;
        const auto& lp = p.layers[(size_t) l].p;
        std::vector<std::pair<int, float>> toPlay;
        for (const auto& nt : notes) toPlay.emplace_back (nt.note + (int) lp[lpCoarse],       // (the velocity as the note will carry it: through MIDI's 127 steps, then the layer's TOUCH)
                                                              touched (juce::MidiMessage::noteOn (1, juce::jlimit (0, 127, nt.note), juce::jlimit (0.01f, 1.0f, nt.velocity)).getFloatVelocity(), lp[lpVelSens], lp[lpVelCurve]));
        rt->sfz->primeFor (toPlay);
    }
    struct Ev { int sample; bool on; int note; float vel; };
    std::vector<Ev> evs;
    for (const auto& nt : notes)
    {
        evs.push_back ({ (int) (nt.start * sampleRate), true, nt.note, nt.velocity });
        evs.push_back ({ (int) ((nt.start + nt.length) * sampleRate), false, nt.note, 0.0f });
    }
    std::stable_sort (evs.begin(), evs.end(), [] (const Ev& a, const Ev& b) { return a.sample < b.sample; });
    size_t next = 0;
    juce::MidiBuffer midi;
    phantom::TempoInfo tempo; tempo.sampleRate = sampleRate; tempo.bpm = 120.0; tempo.playing = true;
    for (int pos = 0; pos < total; pos += kBlock)
    {
        const int n = juce::jmin ((int) kBlock, total - pos);
        midi.clear();
        while (next < evs.size() && evs[next].sample < pos + n)
        {
            const int at = juce::jlimit (0, n - 1, evs[next].sample - pos);
            midi.addEvent (evs[next].on ? juce::MidiMessage::noteOn (1, evs[next].note, juce::jlimit (0.01f, 1.0f, evs[next].vel)) : juce::MidiMessage::noteOff (1, evs[next].note), at);
            ++next;
        }
        float* chans[2] = { out.getWritePointer (0, pos), out.getWritePointer (1, pos) };
        juce::AudioBuffer<float> view (chans, 2, n);
        tempo.ppqPosition = (double) pos / tempo.samplesPerBeat();
        e.process (view, midi, tempo);
    }
    return out;
}
} // namespace atlas
