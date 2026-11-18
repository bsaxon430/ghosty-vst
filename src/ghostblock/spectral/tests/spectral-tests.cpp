/*
 * Poltergeist 2 (GhostBlock) - SPECTRAL FX checks (no ears, no screen).
 *   p2-spectral-tests all [dir]     everything below; WAV demos + CPU table go to [dir]
 *   p2-spectral-tests list          the catalogue: every effect, every parameter (id, name, range, default, unit)
 * Exit code 0 = ALL PASS.
 */
#include "ghostblock/spectral/spectral-fx.h"
#include "ghostblock/spectral/warpcore-isa.h"
#include "spectral-test-util.h"
#include "sc-reference.h"
#include "presets/preset-manager.h"
#include "synth/synth.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <new>

// ---------------------------------------------------------------------------------------- allocation guard
// counts every allocation / free made while `guard` is on (the audio-thread rule: there must be none)
static std::atomic<int> guard{0}, guardHits{0};
#if defined(__GLIBC__)
extern "C"
{
void *__libc_malloc(size_t);
void *__libc_calloc(size_t, size_t);
void *__libc_realloc(void *, size_t);
void *__libc_memalign(size_t, size_t);
void __libc_free(void *);
void *malloc(size_t n) { if (guard.load(std::memory_order_relaxed)) ++guardHits; return __libc_malloc(n); }
void *calloc(size_t a, size_t b) { if (guard.load(std::memory_order_relaxed)) ++guardHits; return __libc_calloc(a, b); }
void *realloc(void *p, size_t n) { if (guard.load(std::memory_order_relaxed)) ++guardHits; return __libc_realloc(p, n); }
void *memalign(size_t a, size_t n) { if (guard.load(std::memory_order_relaxed)) ++guardHits; return __libc_memalign(a, n); }
void *aligned_alloc(size_t a, size_t n) { if (guard.load(std::memory_order_relaxed)) ++guardHits; return __libc_memalign(a, n); }
int posix_memalign(void **out, size_t a, size_t n) { if (guard.load(std::memory_order_relaxed)) ++guardHits; *out = __libc_memalign(a, n); return *out ? 0 : 12; }
void free(void *p) { if (p && guard.load(std::memory_order_relaxed)) ++guardHits; __libc_free(p); }
}
#else
void *operator new(size_t n) { if (guard.load(std::memory_order_relaxed)) ++guardHits; if (void *p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void *operator new[](size_t n) { return operator new(n); }
void operator delete(void *p) noexcept { if (p && guard.load(std::memory_order_relaxed)) ++guardHits; std::free(p); }
void operator delete[](void *p) noexcept { operator delete(p); }
void operator delete(void *p, size_t) noexcept { operator delete(p); }
void operator delete[](void *p, size_t) noexcept { operator delete(p); }
#endif

namespace baconpaul::six_sines
{
extern const clap_plugin *makePlugin(const clap_host *, bool);
extern ghostblock::p2::spectral::SpectralFx *getSpectralFromPlugin(const clap_plugin_t *);
extern Synth *getSynthFromPlugin(const clap_plugin_t *);
} // namespace baconpaul::six_sines
namespace ss = baconpaul::six_sines;

using namespace ghostblock::p2::spectral;
using namespace p2t;

// ---- a rack on its own (no plugin), fed with a signal
struct Rig
{
    SpectralFx fx;
    double sr;
    explicit Rig(double rate = 48000) : sr(rate) {}
    void effect(int s, int type, float norm = 0.6f)
    {
        fx.setParam(SpectralFx::pSlotType(s), type);
        const auto &d = types()[(size_t)type];
        for (size_t p = 0; p < d.params.size(); ++p)
        {
            const auto &q = d.params[p];
            float v = q.def;
            if (q.kind == Kind::Float) v = q.fromNorm(q.id == "freq" ? 0.3f : norm); // (the filter: low enough to bite on a bass)
            else if (q.kind == Kind::Bool) v = 1;
            fx.uiSlotValue(s, (int)p, v, 1);
        }
    }
    void start(bool on = true)
    {
        fx.setParam(SpectralFx::pOn, on ? 1 : 0);
        fx.activate(sr);
    }
    // processes `in` (in place) in blocks of `bs`
    void run(Stereo &x, uint32_t bs = 512, int from = 0, int to = -1)
    {
        const int end = to < 0 ? x.n() : to;
        for (int pos = from; pos < end; pos += (int)bs)
        {
            const uint32_t n = (uint32_t)std::min<int>((int)bs, end - pos);
            fx.process(x.l.data() + pos, x.r.data() + pos, n, 120.0);
        }
    }
};

static void testCatalogue()
{
    section("catalogue");
    const auto &t = types();
    int sc = 0, maxp = 0;
    std::map<std::string, int> perCat;
    for (auto &d : t)
        if (d.index > 0)
        {
            ++sc;
            perCat[d.category]++;
            maxp = std::max(maxp, (int)d.params.size());
        }
    std::printf("effects: %d in %d folders; most parameters on one effect: %d; WarpCore runs on: %s\n", sc, (int)categories().size(), maxp, warpSimdName());
    CHECK(sc == 38, "all 38 Spectral Corruptor audio effects are in the catalogue");
    CHECK(categories().size() == 8 && perCat["DESTRUCTION"] == 6 && perCat["PARTIAL"] == 5 && perCat["GENERIC"] == 5, "in upstream's eight folders");
    CHECK(maxp <= kMaxParams, "no effect has more parameters than a slot can hold");
    CHECK(warpAvailable() && warpParams().size() == (size_t)kWarpParams, "WarpCore is there with its nine parameters");
    bool ok = true;
    for (auto &d : t)
        for (auto &p : d.params)
        {
            ok &= p.max > p.min && p.def >= p.min && p.def <= p.max;
            ok &= std::abs(p.fromNorm(p.toNorm(p.def)) - p.def) <= 1e-3f * std::max(1.f, std::abs(p.def));
        }
    CHECK(ok, "every parameter: default inside its range, 0..1 <-> real round trip");
    // host parameters
    SpectralFx fx;
    std::set<uint32_t> ids;
    bool infoOk = true;
    for (uint32_t i = 0; i < SpectralFx::kParams; ++i)
    {
        clap_param_info info{};
        infoOk &= fx.paramInfo(i, &info) && SpectralFx::isParam(info.id) && info.name[0] != 0 && info.max_value > info.min_value;
        ids.insert(info.id);
    }
    CHECK(infoOk && ids.size() == SpectralFx::kParams, "the host sees 62 SPECTRAL parameters, each with its own id, name and range");
    fx.setParam(SpectralFx::pSlotType(0), typeIndex("SpectralFilter"));
    clap_param_info info{};
    fx.paramInfo(SpectralFx::kGlobalParams + 3 + 1, &info); // slot 1, P2
    char txt[64]{};
    fx.valueToText(SpectralFx::pSlotParam(0, 1), fx.getParam(SpectralFx::pSlotParam(0, 1)), txt, sizeof(txt));
    std::printf("slot 1 = Spectral Filter: host name of P2 \"%s\", its value reads \"%s\"\n", info.name, txt);
    CHECK(std::string(info.name) == "Spectral 1 Frequency" && std::string(txt) == "1000.0 Hz", "a slot's host parameters carry the effect's real names and units");
}

static void listCatalogue()
{
    for (auto &d : types())
    {
        if (d.index == 0) continue;
        std::printf("%2d  %-20s %-22s %-16s %s%s\n", d.index, d.id.c_str(), d.name.c_str(), d.category.c_str(),
                    d.domain == Domain::Time ? "time" : d.domain == Domain::Partial ? "partial" : "spectral", d.liveParams ? "" : " (rebuild on change)");
        for (size_t i = 0; i < d.params.size(); ++i)
        {
            const auto &p = d.params[i];
            std::string ch;
            for (auto &c : p.choices) ch += (ch.empty() ? " {" : ", ") + c;
            if (!ch.empty()) ch += "}";
            std::printf("      %s%-2d %-18s %-16s %g .. %g  default %g%s%s%s\n", p.hostParam ? "P" : "x", (int)i + 1, p.id.c_str(), p.name.c_str(), (double)p.min,
                        (double)p.max, (double)p.def, p.unit.c_str(), p.logScale ? "  (log)" : "", ch.c_str());
        }
    }
    std::printf("WarpCore (%s)\n", warpSimdName());
    for (auto &p : warpParams()) std::printf("      %-14s %-14s %g .. %g  default %g%s\n", p.id.c_str(), p.name.c_str(), (double)p.min, (double)p.max, (double)p.def, p.unit.c_str());
}

// (b) every effect on its own: finite, bounded, no denormals, and it changes the sound
static void testEveryEffect()
{
    section("every effect");
    const auto src = bassSignal(48000, 2.0);
    int bad = 0, same = 0;
    for (auto &d : types())
    {
        if (d.index == 0) continue;
        Rig r;
        r.effect(0, d.index);
        r.start();
        auto y = src;
        r.run(y);
        const auto h = health(y);
        const int lat = (int)r.fx.latency();
        const double diff = rmsDiff(src, y, lat, 24000, 90000);
        const bool okH = h.finite && h.peak < 8.01f && !h.denormal;
        const bool okD = diff > 1e-4;
        if (!okH) { ++bad; std::printf("FAIL health: %s (peak %g finite %d denormal %d)\n", d.name.c_str(), (double)h.peak, (int)h.finite, (int)h.denormal); }
        if (!okD) { ++same; std::printf("FAIL unchanged: %s (diff %g)\n", d.name.c_str(), diff); }
        std::printf("  %-22s latency %5d  peak %.3f  rms out %.4f  |out - dry| rms %.4f\n", d.name.c_str(), lat, (double)h.peak, rmsOf(y, 24000, 90000), diff);
    }
    CHECK(bad == 0, "all 38 effects: no NaN / inf / denormal, output bounded");
    CHECK(same == 0, "all 38 effects change the sound");
    for (int ord = 0; ord < 2; ++ord)
    {
        Rig r;
        r.fx.setParam(SpectralFx::pWarpOn, 1);
        r.fx.setParam(SpectralFx::pOrder, ord);
        r.start();
        auto y = src;
        r.run(y);
        const auto h = health(y);
        std::printf("  WarpCore (%s)%s latency %d  peak %.3f  |out - dry| rms %.4f\n", warpSimdName(), ord ? " first" : "", (int)r.fx.latency(), (double)h.peak, rmsDiff(src, y, 0, 24000, 90000));
        CHECK(h.finite && !h.denormal && h.peak < 8.01f && rmsDiff(src, y, 0, 24000, 90000) > 1e-3 && r.fx.latency() == 0,
              ord ? "WarpCore before the chain: clean, changes the sound, zero latency" : "WarpCore alone: clean, changes the sound, zero latency");
    }
}


// =================================================================================================== the plugin
static std::atomic<int> restartAsks{0}, callbackAsks{0}, latencyTells{0};
struct EvList
{
    std::vector<const clap_event_header_t *> all;
    static uint32_t size(const clap_input_events *l) { return (uint32_t)((EvList *)l->ctx)->all.size(); }
    static const clap_event_header_t *get(const clap_input_events *l, uint32_t i) { return ((EvList *)l->ctx)->all[i]; }
};
static bool tryPush(const clap_output_events *, const clap_event_header_t *) { return true; }
struct Plug
{
    clap_host host{};
    const clap_plugin *pl{nullptr};
    SpectralFx *fx{nullptr};
    double sr{48000};
    uint32_t bs{256};
    int pos{0};
    bool active{false};
    std::vector<std::pair<uint32_t, double>> queued; // host param changes for the next block
    Plug()
    {
        host.clap_version = CLAP_VERSION;
        host.name = "p2-spectral-tests";
        host.vendor = "ghostblock";
        host.url = "";
        host.version = "1";
        host.get_extension = [](const clap_host *, const char *id) -> const void * {
            static const clap_host_latency lat{[](const clap_host *) { ++latencyTells; }};
            return std::strcmp(id, CLAP_EXT_LATENCY) == 0 ? &lat : nullptr;
        };
        host.request_restart = [](const clap_host *) { ++restartAsks; };
        host.request_process = [](const clap_host *) {};
        host.request_callback = [](const clap_host *) { ++callbackAsks; };
        pl = ss::makePlugin(&host, false);
        pl->init(pl);
        fx = ss::getSpectralFromPlugin(pl);
    }
    ~Plug()
    {
        stop();
        pl->destroy(pl);
    }
    bool preset(int idx)
    {
        auto *pld = static_cast<const clap_plugin_preset_load *>(pl->get_extension(pl, CLAP_EXT_PRESET_LOAD));
        const auto key = std::to_string(idx);
        return pld && pld->from_location(pl, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, key.c_str());
    }
    std::string save()
    {
        struct Out
        {
            std::string d;
            static int64_t write(const clap_ostream *s, const void *b, uint64_t n) { ((Out *)s->ctx)->d.append((const char *)b, (size_t)n); return (int64_t)n; }
        } o;
        clap_ostream os{&o, &Out::write};
        static_cast<const clap_plugin_state *>(pl->get_extension(pl, CLAP_EXT_STATE))->save(pl, &os);
        return o.d;
    }
    bool load(const std::string &st)
    {
        struct In
        {
            std::string d;
            size_t pos{0};
            static int64_t read(const clap_istream *s, void *b, uint64_t n)
            {
                auto *m = (In *)s->ctx;
                const size_t k = std::min<size_t>((size_t)n, m->d.size() - m->pos);
                memcpy(b, m->d.data() + m->pos, k);
                m->pos += k;
                return (int64_t)k;
            }
        } in{st};
        clap_istream is{&in, &In::read};
        return static_cast<const clap_plugin_state *>(pl->get_extension(pl, CLAP_EXT_STATE))->load(pl, &is);
    }
    void start(uint32_t seed = 4242)
    {
        pl->activate(pl, sr, 1, bs);
        pl->start_processing(pl);
        active = true;
        ss::getSynthFromPlugin(pl)->monoValues.rng.reseed(seed); // (the engine seeds its randomness from the clock)
    }
    void stop()
    {
        if (!active) return;
        pl->stop_processing(pl);
        pl->deactivate(pl);
        active = false;
    }
    uint32_t latency()
    {
        auto *l = static_cast<const clap_plugin_latency *>(pl->get_extension(pl, CLAP_EXT_LATENCY));
        return l ? l->get(pl) : 0u;
    }
    void mainThread() { pl->on_main_thread(pl); }
    // renders `seconds`; keys held from `onAt` to `offAt` seconds (relative to this call)
    Stereo render(double seconds, double onAt = 0.02, double offAt = 1e9, std::vector<int16_t> keys = {36, 48, 55})
    {
        const int total = (int)(sr * seconds), on = (int)(sr * onAt), off = offAt > 1e8 ? -1 : (int)(sr * offAt);
        Stereo out(total);
        std::vector<float> inL(bs, 0.f), inR(bs, 0.f), oL(bs), oR(bs);
        EvList ev;
        clap_input_events in{&ev, &EvList::size, &EvList::get};
        clap_output_events o{nullptr, &tryPush};
        std::vector<clap_event_note> notes;
        std::vector<clap_event_param_value> params;
        notes.reserve(16);
        for (int p = 0; p < total; p += (int)bs)
        {
            notes.clear();
            params.clear();
            ev.all.clear();
            for (auto &[id, v] : queued)
            {
                clap_event_param_value pv{};
                pv.header.size = sizeof(pv);
                pv.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                pv.header.type = CLAP_EVENT_PARAM_VALUE;
                pv.param_id = id;
                pv.note_id = -1; pv.port_index = -1; pv.channel = -1; pv.key = -1;
                pv.value = v;
                params.push_back(pv);
            }
            queued.clear();
            for (auto &pv : params) ev.all.push_back(&pv.header);
            auto note = [&](uint16_t type, int16_t key, uint32_t t) {
                clap_event_note n{};
                n.header.size = sizeof(n);
                n.header.time = t;
                n.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                n.header.type = type;
                n.note_id = -1;
                n.key = key;
                n.velocity = 0.8;
                notes.push_back(n);
            };
            if (p <= on && on < p + (int)bs) for (auto k : keys) note(CLAP_EVENT_NOTE_ON, k, (uint32_t)(on - p));
            if (off >= 0 && p <= off && off < p + (int)bs) for (auto k : keys) note(CLAP_EVENT_NOTE_OFF, k, (uint32_t)(off - p));
            for (auto &n : notes) ev.all.push_back(&n.header);
            float *ip[2] = {inL.data(), inR.data()};
            float *op[2] = {oL.data(), oR.data()};
            clap_audio_buffer ib{ip, nullptr, 2, 0, 0}, ob{op, nullptr, 2, 0, 0};
            clap_process pr{};
            pr.steady_time = pos + p;
            pr.frames_count = bs;
            pr.audio_inputs = &ib;
            pr.audio_inputs_count = 1;
            pr.audio_outputs = &ob;
            pr.audio_outputs_count = 1;
            pr.in_events = &in;
            pr.out_events = &o;
            pl->process(pl, &pr);
            for (uint32_t i = 0; i < bs && p + (int)i < total; ++i)
            {
                out.l[(size_t)(p + (int)i)] = oL[i];
                out.r[(size_t)(p + (int)i)] = oR[i];
            }
        }
        pos += total;
        return out;
    }
};
static bool same(const Stereo &a, const Stereo &b)
{
    return a.n() == b.n() && std::memcmp(a.l.data(), b.l.data(), a.l.size() * 4) == 0 && std::memcmp(a.r.data(), b.r.data(), a.r.size() * 4) == 0;
}
static void busyRack(SpectralFx &fx) // a rack with something in every corner
{
    Rig *none = nullptr;
    (void)none;
    auto put = [&](int s, const char *id) {
        const int t = typeIndex(id);
        fx.setParam(SpectralFx::pSlotType(s), t);
        const auto &d = types()[(size_t)t];
        for (size_t p = 0; p < d.params.size(); ++p)
            fx.uiSlotValue(s, (int)p, d.params[p].kind == Kind::Float ? d.params[p].fromNorm(0.55f) : d.params[p].kind == Kind::Bool ? 1.f : d.params[p].def, 1);
    };
    put(0, "BinShuffle");
    put(1, "SpectralEqualizer");
    put(2, "FrameHold");
    put(3, "Reverb");
    fx.setParam(SpectralFx::pWarpOn, 1);
    fx.setParam(SpectralFx::pWarpParam(0), 24);
    fx.setParam(SpectralFx::pMix, 70);
    fx.setParam(SpectralFx::pChainMix, 80);
    fx.setParam(SpectralFx::pOrder, 1);
    fx.setParam(SpectralFx::pFft, 3);
    fx.setParam(SpectralFx::pWindow, 2);
}

// (a) SPECTRAL off = the plugin as it was, for every factory preset; and the latency rules through CLAP
static void testBypass()
{
    section("bypassed = untouched (every factory preset, through the CLAP plugin)");
    clap_host h{};
    int n = 0;
    {
        Plug probe;
        while (probe.preset(n)) ++n;
    }
    int identical = 0, differs = 0, unstable = 0;
    for (int i = -1; i < n; ++i)
    {
        auto one = [&](bool rack) {
            Plug p;
            if (i >= 0) p.preset(i);
            if (rack) busyRack(*p.fx); // a full rack, but SPECTRAL ON is off
            p.start();
            auto y = p.render(0.8, 0.02, 0.5);
            if (rack && p.latency() != 0) ++differs;
            return y;
        };
        const auto a = one(false), b = one(true);
        if (same(a, b)) ++identical;
        else if (!same(a, one(false))) ++unstable; // (this preset does not repeat itself even untouched)
        else { ++differs; std::printf("FAIL: preset %d differs with the rack loaded but off\n", i); }
    }
    (void)h;
    std::printf("factory presets + init: %d rendered twice; identical %d, differing %d, not repeatable by themselves %d\n", n + 1, identical, differs, unstable);
    CHECK(n > 50 && differs == 0 && identical >= n - 5, "SPECTRAL off: bit-identical output and zero latency for every factory preset");

    // the life of the latency: off -> on -> (host restarts) -> off -> (host restarts)
    Plug p, q;
    p.fx->setParam(SpectralFx::pSlotType(0), typeIndex("SpectralJPEG"));
    p.start();
    q.start();
    const int r0 = restartAsks.load();
    bool ok = p.latency() == 0;
    p.render(0.3);
    q.render(0.3);
    p.queued.push_back({SpectralFx::pOn, 1.0});
    auto on1 = p.render(0.3);
    q.render(0.3);
    ok &= p.latency() == 0 && restartAsks.load() == r0 + 1; // still what the host was told; a restart is asked for, once
    p.stop();
    q.stop();
    p.start();
    q.start();
    ok &= p.latency() == 2047 && latencyTells.load() > 0;
    p.render(0.3);
    q.render(0.3);
    p.queued.push_back({SpectralFx::pOn, 0.0});
    auto off1 = p.render(0.3);
    auto ref1 = q.render(0.3);
    ok &= p.latency() == 2047 && restartAsks.load() == r0 + 2;
    // off but not yet restarted: the plain sound, exactly as late as the host was told (2047 samples)
    const float lateDiff = maxDiff(ref1, off1, 2047, 4096, ref1.n() - 4096);
    p.stop();
    q.stop();
    p.start();
    q.start();
    ok &= p.latency() == 0;
    const auto a = p.render(0.6, 0.02, 0.4), b = q.render(0.6, 0.02, 0.4);
    std::printf("latency told to the host: off 0 -> on (restart asked) -> 2047 -> off (restart asked) -> 0; off-before-restart vs plain, shifted 2047: max diff %g\n", (double)lateDiff);
    CHECK(ok, "latency only changes when the host re-activates the plugin; a restart is requested each time it should");
    CHECK(lateDiff < 1e-6f, "switched off while the host still compensates 2047 samples: the dry sound stays exactly on that time");
    CHECK(same(a, b), "after the restart the plugin is bit-identical to one whose rack was never on");
}

// (c) the latency the host is told = the latency you measure, for every FFT size (and the dry side of every mix waits the same)
static void testLatency()
{
    section("latency");
    bool all = true;
    Stereo x(96000);
    uint32_t seed = 12345;
    for (int i = 0; i < x.n(); ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        x.l[(size_t)i] = ((float)(seed >> 8) / 8388608.f - 1.f) * 0.3f;
        seed = seed * 1664525u + 1013904223u;
        x.r[(size_t)i] = ((float)(seed >> 8) / 8388608.f - 1.f) * 0.3f;
    }
    for (int f = 0; f < kFftSizes; ++f)
        for (int variant = 0; variant < 3; ++variant)
        {
            Rig r;
            r.fx.setParam(SpectralFx::pFft, f);
            r.fx.setParam(SpectralFx::pSlotType(0), typeIndex("BinShuffle")); // amount 0: lets every frame through unchanged
            if (variant == 1) r.fx.setParam(SpectralFx::pMix, 50);
            if (variant == 2) { r.fx.setParam(SpectralFx::pChainMix, 50); r.fx.setParam(SpectralFx::pWindow, 4); r.fx.setParam(SpectralFx::pOverlap, 6.5); }
            r.start();
            auto y = x;
            r.run(y, 480);
            const int lat = (int)r.fx.latency();
            // (variant 2 is compared once the rack is let out: before that the output is the plain signal, not 1.414 x)
            const int from = variant == 2 ? 2 * fftSizeAt(f) + 4000 : 0, to = x.n() - 20000;
            // chain mix 50 % is upstream's equal-power law: both sides 0.7071 -> the sum of two identical signals is 1.414 x
            const float scale = variant == 2 ? 1.41421356f : 1.f;
            Stereo ys = y;
            for (auto *v : {&ys.l, &ys.r}) for (auto &s : *v) s /= scale;
            const float at = maxDiff(x, ys, lat, from, to), before = maxDiff(x, ys, lat - 1, from + 1, to), after = maxDiff(x, ys, lat + 1, from, to);
            const bool ok = lat == fftSizeAt(f) - 1 && at < 2e-4f && before > 0.05f && after > 0.05f;
            all &= ok;
            if (variant == 0 || !ok)
                std::printf("  FFT %5d [%s]: reported %5d samples (%.1f ms at 48 kHz); output vs input shifted by that: max diff %.2g (one sample off: %.2g)%s\n",
                            fftSizeAt(f), variant == 0 ? "wet" : variant == 1 ? "master mix 50" : "chain mix 50, Blackman-Harris, overlap 6.5", lat, lat / 48.0, (double)at, (double)std::min(before, after), ok ? "" : "  <-- FAIL");
        }
    CHECK(all, "every FFT size: reported latency = measured latency = size - 1; master mix and chain mix keep the dry side aligned");
}

// (a2) a new FFT size, then the host restart it asks for before the audio side took the new parts (Brandon, FL Studio:
// FFT 2048 -> 4096 crashed the host). The restart must leave the rack on the new size and the editor's spectrum tap on
// a live core of that size, for every way the timing can fall.
static void testFftRestart()
{
    section("fft restart");
    bool all = true;
    Stereo x(96000);
    for (int i = 0; i < x.n(); ++i)
        x.l[(size_t)i] = x.r[(size_t)i] = 0.3f * std::sin(0.031f * (float)i);
    for (int when = 0; when < 3; ++when) // 0: still in the mailbox, 1: waiting on the audio side, 2: already in
        for (int to = 0; to < kFftSizes; ++to)
        {
            Rig r;
            r.fx.setParam(SpectralFx::pSlotType(0), typeIndex("BinShuffle"));
            r.start();
            auto y = x;
            r.run(y, 512, 0, 24000);
            r.fx.uiSet(SpectralFx::pFft, to);
            r.fx.serviceMainThread(); // the new STFT is built and posted
            if (when >= 1) r.run(y, 512, 24000, 24512);  // the audio side picks it up (it waits for its fade)
            if (when >= 2) r.run(y, 512, 24512, 36000);  // ... and puts it in
            r.fx.activate(48000);                         // the host restarts the plugin (the new latency)
            std::vector<float> m;
            double rate = 0;
            r.run(y, 512, 36000, 96000); // (longer than two windows of the biggest size)
            const bool got = r.fx.spectrum(m, rate, false);
            const int bins = fftSizeAt(to) / 2 + 1;
            const bool ok = r.fx.latency() == (uint32_t)(fftSizeAt(to) - 1) && got && (int)m.size() == bins && rate == 48000;
            all &= ok;
            if (!ok)
                std::printf("  FFT 2048 -> %d, restart %s: latency %u, spectrum %s, %d bins (want %d)  <-- FAIL\n", fftSizeAt(to),
                            when == 0 ? "before the audio side saw it" : when == 1 ? "while it waited" : "after it went in", r.fx.latency(), got ? "fresh" : "none",
                            (int)m.size(), bins);
        }
    CHECK(all, "a new FFT size + the host's restart (before, during or after the audio side takes it): the rack runs the new size, the spectrum tap reads it");
}

// (d) nothing clicks: bypass, mixes, slot bypass, a new effect, WarpCore on / off, the order switch
static void testClicks()
{
    section("click-free");
    const double sr = 48000;
    Stereo x((int)(sr * 6));
    for (int i = 0; i < x.n(); ++i) x.l[(size_t)i] = x.r[(size_t)i] = 0.5f * (float)std::sin(2 * M_PI * 110.0 * i / sr);
    auto jump = [](const Stereo &y, int from, int to) {
        float d = 0;
        for (int i = std::max(1, from); i < to; ++i) d = std::max(d, std::fabs(y.l[(size_t)i] - y.l[(size_t)i - 1]));
        return d;
    };
    const float dryStep = jump(x, 1, x.n());
    struct Case { const char *name; std::function<void(Rig &)> setup; std::function<void(Rig &, int)> act; };
    const std::vector<Case> cases = {
        {"master ON / OFF", [](Rig &) {}, [](Rig &r, int k) { r.fx.setParam(SpectralFx::pOn, k % 2); }},
        {"master MIX swept", [](Rig &) {}, [](Rig &r, int k) { r.fx.setParam(SpectralFx::pMix, (k * 37) % 101); }},
        {"chain MIX swept", [](Rig &) {}, [](Rig &r, int k) { r.fx.setParam(SpectralFx::pChainMix, (k * 53) % 101); }},
        {"slot bypass (spectral)", [](Rig &) {}, [](Rig &r, int k) { r.fx.setParam(SpectralFx::pSlotOn(0), k % 2); }},
        {"slot bypass (time)", [](Rig &) {}, [](Rig &r, int k) { r.fx.setParam(SpectralFx::pSlotOn(1), k % 2); }},
        {"slot mix swept", [](Rig &) {}, [](Rig &r, int k) { r.fx.setParam(SpectralFx::pSlotMix(1), (k * 41) % 101); }},
        {"a parameter jumping", [](Rig &) {}, [](Rig &r, int k) { r.fx.setParam(SpectralFx::pSlotParam(1, 5), k % 2 ? 1.0 : 0.2); }},
        {"another effect into a slot", [](Rig &) {}, [](Rig &r, int k) { r.fx.setParam(SpectralFx::pSlotType(1), typeIndex(k % 2 ? "Compressor" : "Utility")); r.fx.serviceMainThread(); }},
        {"last spectral effect out / in", [](Rig &) {}, [](Rig &r, int k) { r.fx.setParam(SpectralFx::pSlotType(0), k % 2 ? 0 : typeIndex("SpectralFilter")); r.fx.serviceMainThread(); }},
        {"FFT size changed", [](Rig &) {}, [](Rig &r, int k) { r.fx.setParam(SpectralFx::pFft, 2 + k % 3); r.fx.serviceMainThread(); }},
        {"WarpCore ON / OFF", [](Rig &) {}, [](Rig &r, int k) { r.fx.setParam(SpectralFx::pWarpOn, k % 2); }},
        {"ORDER switched", [](Rig &r) { r.fx.setParam(SpectralFx::pWarpOn, 1); }, [](Rig &r, int k) { r.fx.setParam(SpectralFx::pOrder, k % 2); }},
    };
    bool all = true;
    for (auto &c : cases)
    {
        auto make = [&](Rig &r) {
            // gentle effects, so a click would stand out: a spectral low-pass and a -6 dB utility
            r.fx.setParam(SpectralFx::pSlotType(0), typeIndex("SpectralFilter"));
            r.fx.setParam(SpectralFx::pSlotParam(0, 1), types()[(size_t)typeIndex("SpectralFilter")].params[1].toNorm(2000.f));
            r.fx.setParam(SpectralFx::pSlotType(1), typeIndex("Utility"));
            r.fx.setParam(SpectralFx::pSlotParam(1, 5), 0.6);
            r.fx.setParam(SpectralFx::pWarpParam(0), 2); // (WarpCore with 2 bands on a 110 Hz sine: a smooth tone)
            r.fx.setParam(SpectralFx::pWarpParam(1), 4000);
            c.setup(r);
            r.start();
        };
        // what the sound's own steepest step is, in both states the case moves between
        float steadyStep = dryStep;
        for (int state = 0; state < 2; ++state)
        {
            Rig steady;
            make(steady);
            if (state == 1) c.act(steady, 1);
            steady.fx.serviceMainThread();
            auto ys = x;
            steady.run(ys, 256);
            steadyStep = std::max(steadyStep, jump(ys, 48000, ys.n()));
        }
        Rig r;
        make(r);
        auto y = x;
        int k = 0;
        for (int pos = 0; pos < y.n(); pos += 256)
        {
            if (pos >= 48000 && (pos / 256) % 60 == 0) c.act(r, ++k); // every 0.32 s, from 1 s on
            r.run(y, 256, pos, std::min(pos + 256, y.n()));
        }
        const float step = jump(y, 24000, y.n());
        const bool ok = step <= 2.5f * steadyStep && health(y).finite;
        all &= ok;
        std::printf("  %-30s largest sample-to-sample step %.4f (steady %.4f, dry sine %.4f)%s\n", c.name, (double)step, (double)steadyStep, (double)dryStep, ok ? "" : "  <-- FAIL");
    }
    CHECK(all, "bypass, mixes, slot bypass, parameter jumps, effect / FFT changes, WarpCore, order: no step larger than the sound's own");
}

// (e) save -> a new plugin -> load -> the same sound
static void testState()
{
    section("state");
    Plug a;
    a.preset(0);
    busyRack(*a.fx);
    a.fx->uiSlotValue(1, 12, 3000.f, 1); // an editor-only parameter of the equalizer (band 5 frequency)
    a.fx->setParam(SpectralFx::pOn, 1);
    const auto st = a.save();
    Plug b;
    const bool loaded = b.load(st);
    CHECK(loaded && a.fx->toState() == b.fx->toState(), "every setting survives save + load (effects, all parameters, mixes, order, FFT, WarpCore)");
    CHECK(b.fx->slotValue(1, 12) == 3000.f && b.fx->slotType(1) == typeIndex("SpectralEqualizer") && b.fx->order() == 1, "including the editor-only parameters");
    a.start();
    b.start();
    const auto ya = a.render(1.5, 0.02, 1.0), yb = b.render(1.5, 0.02, 1.0);
    std::printf("state: %zu bytes; render A vs B max diff %g; latency %u / %u; out rms %.4f\n", st.size(), (double)maxDiff(ya, yb, 0, 0, ya.n()), a.latency(), b.latency(), rmsOf(ya, 24000, 60000));
    CHECK(same(ya, yb) && a.latency() == 1023 && b.latency() == 1023, "the loaded plugin renders bit-identically to the one that was saved");
    // a state from before SPECTRAL (patch + MORPH only): the rack is off and empty
    Plug c;
    c.load(st); // (first make it non-default)
    const auto z2 = st.find('\0', st.find('\0') + 1);
    const bool ok = c.load(st.substr(0, z2 + 1));
    CHECK(ok && !c.fx->on() && c.fx->slotType(0) == 0 && !c.fx->warpOn() && c.fx->latencyWanted() == 0, "a state saved before SPECTRAL existed loads with the rack off and empty");
    SpectralFx f;
    CHECK(!f.fromState("garbage") && f.fromState("P2SPECTRAL 1\non 1\nfuture 3\nslot 0 NotAnEffect 1 100 1\nx 2\nend\n") && f.on() && f.slotType(0) == 0,
          "unknown keys and effects from a newer version are skipped, not fatal");
}

// (f) nothing is allocated or freed while processing, whatever is being changed
static void testAllocations()
{
    section("audio thread allocations");
    const auto src = bassSignal(48000, 1.0);
    int total = 0;
    std::string where;
    for (auto &d : types())
    {
        if (d.index == 0) continue;
        Rig r;
        r.effect(0, d.index);
        r.effect(1, typeIndex("Delay"));
        r.effect(2, typeIndex("PartialCentroid"));
        r.fx.setParam(SpectralFx::pWarpOn, 1);
        r.start();
        auto y = src;
        r.run(y, 512, 0, 8192); // (running)
        guardHits = 0;
        guard = 1;
        int k = 0;
        for (int pos = 8192; pos + 512 <= y.n(); pos += 512, ++k)
        {
            // everything a host can automate, changing while it runs
            for (int p = 0; p < kHostParamsPerSlot; ++p) r.fx.setParam(SpectralFx::pSlotParam(0, p), ((k * 7 + p * 13) % 100) / 100.0);
            r.fx.setParam(SpectralFx::pSlotParam(2, 3), (k % 10) / 10.0);
            r.fx.setParam(SpectralFx::pSlotParam(2, 0), k % 4 == 0);
            r.fx.setParam(SpectralFx::pSlotParam(2, 1), k % 4);
            r.fx.setParam(SpectralFx::pMix, 50 + k % 50);
            r.fx.setParam(SpectralFx::pChainMix, 100 - k % 50);
            r.fx.setParam(SpectralFx::pSlotOn(0), k % 9 != 0);
            r.fx.setParam(SpectralFx::pSlotMix(0), 100 - k % 30);
            r.fx.setParam(SpectralFx::pWarpParam(0), 10 + k % 100);
            r.fx.setParam(SpectralFx::pWarpParam(3), 1 + k % 8);
            r.fx.setParam(SpectralFx::pWarpOn, k % 11 != 0);
            r.fx.setParam(SpectralFx::pOrder, (k / 16) % 2);
            r.fx.setParam(SpectralFx::pLimit, k % kLimits);
            r.fx.setParam(SpectralFx::pOn, k % 23 != 22);
            if (k == 40) r.fx.requestReset();
            r.fx.process(y.l.data() + pos, y.r.data() + pos, 512, 120.0 + k);
        }
        guard = 0;
        if (guardHits.load()) where += (where.empty() ? "" : ", ") + d.name + " (" + std::to_string(guardHits.load()) + ")";
        total += guardHits.load();
    }
    // and a swap arriving while it runs (the new objects are made outside process(); taking them in must not allocate)
    {
        Rig r;
        r.effect(0, typeIndex("BinShuffle"));
        r.start();
        auto y = src;
        r.run(y, 512, 0, 8192);
        int k = 0;
        for (int pos = 8192; pos + 512 <= y.n(); pos += 512, ++k)
        {
            if (k % 6 == 0)
            {
                r.fx.setParam(SpectralFx::pSlotType(k / 6 % kSlots), 1 + (k * 5) % 38);
                r.fx.setParam(SpectralFx::pFft, 2 + (k / 6) % 4);
                r.fx.serviceMainThread();
            }
            guard = 1;
            r.fx.process(y.l.data() + pos, y.r.data() + pos, 512, 120.0);
            guard = 0;
        }
        total += guardHits.load();
    }
    std::printf("allocations / frees inside process(): %d%s%s\n", total, where.empty() ? "" : " in ", where.c_str());
    CHECK(total == 0, "no allocation or free on the audio thread: 38 effects + WarpCore, with every parameter moving and effects being swapped");
}

// (g) our hosting vs upstream's own engine: the same input, the same settings
static void testReference(const std::string &dir)
{
    section("against upstream's own engine");
    const auto src = bassSignal(48000, 1.5);
    int exact = 0, close = 0, bad = 0;
    float worst = 0, worstPartial = 0;
    for (auto &d : types())
    {
        if (d.index == 0) continue;
        Rig r;
        p2ref::Setup setup;
        const int slot = d.domain == Domain::Time ? 1 : 0;
        if (d.domain == Domain::Time)
        { // (upstream always has the STFT in the path; an idle spectral effect in front gives ours the same path)
            r.fx.setParam(SpectralFx::pSlotType(0), typeIndex("BinShuffle"));
            setup.chain.push_back({"BinShuffle", {{"amount", 0, 0.0}, {"seed", 1, 0.0}}, {}});
        }
        r.effect(slot, d.index);
        p2ref::Module m;
        m.type = d.id;
        float curve[6] = {0, 50, 50, 100, 0, 0};
        for (size_t p = 0; p < d.params.size(); ++p)
        {
            const auto &q = d.params[p];
            const float v = r.fx.slotValue(slot, (int)p);
            if (q.id == "slices") setup.slices = (int)v;
            else if (q.id == "crossfade") setup.crossfade = v;
            else if (q.id.rfind("curve.", 0) == 0) curve[p - 1] = v;
            else m.props.push_back({q.id, (int)q.kind, (double)v});
        }
        if (d.id == "FrequencyRemap")
        {
            char b[256];
            std::snprintf(b, sizeof(b), "2,4;0,%.9g,%.9g,1;%.9g,%.9g,%.9g,2;1,%.9g,0,3", (double)(curve[0] * 0.01f), (double)(curve[4] * 0.01f), (double)(curve[1] * 0.01f),
                          (double)(curve[2] * 0.01f), (double)(curve[5] * 0.01f), (double)(curve[3] * 0.01f));
            m.remapCurve = b;
        }
        setup.chain.push_back(m);
        setup.allRacks = d.domain == Domain::Partial;
        r.start();
        auto ours = src, theirs = src;
        r.run(ours, 512);
        p2ref::run(setup, theirs.l.data(), theirs.r.data(), theirs.n());
        const int from = 2047 + 4096, to = src.n();
        const float diff = maxDiff(theirs, ours, 0, from, to);
        const double level = rmsOf(theirs, from, to);
        const bool partial = d.domain == Domain::Partial;
        if (diff == 0.f) ++exact;
        else if (diff <= (partial ? 1e-5f : 0.f)) ++close; // (Partial: we sum the slices before the inverse FFT, upstream after: float rounding only)
        else { ++bad; std::printf("FAIL: %s differs from upstream by %g\n", d.name.c_str(), (double)diff); }
        (partial ? worstPartial : worst) = std::max(partial ? worstPartial : worst, diff);
        std::printf("  %-22s max |ours - upstream| %-10.3g (upstream rms %.4f)%s\n", d.name.c_str(), (double)diff, level, diff == 0.f ? "  identical" : "");
    }
    std::printf("vs upstream's SpectralProcessor: %d bit-identical, %d within tolerance, %d different; worst %g (Partial effects: %g)\n", exact, close, bad, (double)worst, (double)worstPartial);
    CHECK(bad == 0 && exact + close == 38, "all 38 effects sound as in upstream's own engine (same input, same settings)");
    (void)dir;
}

// WarpCore: every SIMD build this CPU can run gives the same sound
static void testWarpVariants()
{
    section("WarpCore SIMD builds");
    using namespace ghostblock::p2::spectral::detail;
    const auto src = bassSignal(48000, 0.5);
    Stereo first;
    bool ok = true;
    int n = 0;
    for (const char *name : {"SSE2", "SSE4", "AVX", "AVX2", "FMA", "NEON"})
    {
        auto *isa = warpIsaNamed(name);
        if (!isa) continue;
        ++n;
        void *st = isa->create();
        isa->init(st, 48000.f);
        isa->reset(st);
        WarpParams w;
        isa->update(st, w);
        auto y = src;
        for (int pos = 0; pos < y.n(); pos += 64) isa->process(st, y.l.data() + pos, y.r.data() + pos, std::min(64, y.n() - pos));
        isa->destroy(st);
        if (first.n() == 0) first = y;
        const float d = maxDiff(first, y, 0, 0, y.n());
        std::printf("  %-5s rms %.4f, max diff to the first build %.2g\n", name, rmsOf(y, 0, y.n()), (double)d);
        ok &= health(y).finite && d < 2e-3f;
    }
    CHECK(n >= 1 && ok, "every WarpCore build this CPU runs gives the same sound; the plugin picked the best one");
}

// (h) what it costs
static void testCpu(const std::string &dir)
{
    section("CPU");
    std::string table = "effect | folder | 44.1 kHz | 48 kHz\n--- | --- | --- | ---\n";
    auto cost = [&](std::function<void(Rig &)> setup, double sr) {
        Rig r(sr);
        setup(r);
        r.start();
        auto y = bassSignal(sr, 4.0);
        r.run(y, 512, 0, (int)sr); // warm
        const auto t0 = std::chrono::steady_clock::now();
        r.run(y, 512, (int)sr, y.n());
        const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return 100.0 * el / 3.0;
    };
    auto row = [&](const std::string &name, const std::string &cat, std::function<void(Rig &)> setup) {
        const double a = cost(setup, 44100), b = cost(setup, 48000);
        char line[256];
        std::snprintf(line, sizeof(line), "%s | %s | %.2f %% | %.2f %%\n", name.c_str(), cat.c_str(), a, b);
        table += line;
        std::printf("  %-28s %-16s %6.2f %%  %6.2f %%\n", name.c_str(), cat.c_str(), a, b);
        return b;
    };
    std::printf("  one core, 512-sample blocks, stereo, FFT 2048 / overlap 4 (%% of real time)\n");
    row("(rack on, empty)", "-", [](Rig &) {});
    double worst = 0;
    for (auto &d : types())
        if (d.index > 0) worst = std::max(worst, row(d.name, d.category, [&](Rig &r) { r.effect(0, d.index); }));
    row("WarpCore, 50 bands / 2 poles", "INVERSION", [](Rig &r) { r.fx.setParam(SpectralFx::pWarpOn, 1); });
    row("WarpCore, 256 bands / 8 poles", "INVERSION", [](Rig &r) { r.fx.setParam(SpectralFx::pWarpOn, 1); r.fx.setParam(SpectralFx::pWarpParam(0), 256); r.fx.setParam(SpectralFx::pWarpParam(3), 8); });
    row("4 spectral slots + WarpCore", "-", [](Rig &r) { r.effect(0, typeIndex("SpectralJPEG")); r.effect(1, typeIndex("FrequencyShift")); r.effect(2, typeIndex("SpectralDispersion")); r.effect(3, typeIndex("PhaseNoise")); r.fx.setParam(SpectralFx::pWarpOn, 1); });
    if (!dir.empty())
        if (FILE *f = std::fopen((dir + "/cpu.md").c_str(), "w")) { std::fputs(table.c_str(), f); std::fclose(f); }
    CHECK(worst < 60.0, "every effect runs in real time with room to spare on one core");
}

// short WAVs: a factory bass through one effect of every folder (for the listen-check and the spectrograms)
static void writeDemos(const std::string &dir)
{
    section("demos");
    juce::File(dir).createDirectory();
    struct Demo { const char *file; std::function<void(SpectralFx &)> setup; };
    auto one = [](const char *id, std::vector<std::pair<int, float>> realValues) {
        return [=](SpectralFx &fx) {
            fx.setParam(SpectralFx::pSlotType(0), typeIndex(id));
            for (auto &[p, v] : realValues) fx.uiSlotValue(0, p, v, 1);
            fx.setParam(SpectralFx::pOn, 1);
        };
    };
    const std::vector<Demo> demos = {
        {"00-dry", [](SpectralFx &) {}},
        {"01-destruction-bin-shuffle", one("BinShuffle", {{0, 0.35f}})},
        {"02-destruction-spectral-jpeg", one("SpectralJPEG", {{0, 85.f}, {1, 16.f}})},
        {"03-frequency-shift", one("FrequencyShift", {{0, 180.f}})},
        {"04-frequency-mirror", one("SpectralMirror", {{0, 100.f}, {1, 600.f}})},
        {"05-phase-dispersion", one("SpectralDispersion", {{0, 80.f}, {1, 50.f}})},
        {"06-texture-frame-hold", one("FrameHold", {{0, 100.f}, {1, 24.f}, {2, 18.f}})},
        {"07-texture-temporal-smear", one("TemporalSmear", {{0, 100.f}, {1, 40.f}})},
        {"08-filter-spectral-comb", one("SpectralComb", {{0, 100.f}, {1, 220.f}})},
        {"09-harmonics-match-square", one("HarmonicMatch", {{0, 1.f}, {2, 100.f}})},
        {"10-partial-stretcher", one("PartialStretcher", {{3, 7.f}, {5, 32.f}})},
        {"11-generic-reverb", one("Reverb", {{5, 45.f}})},
        {"12-warpcore", [](SpectralFx &fx) { fx.setParam(SpectralFx::pWarpOn, 1); fx.setParam(SpectralFx::pOn, 1); }},
        {"13-warpcore-then-chain", [](SpectralFx &fx) { fx.setParam(SpectralFx::pWarpOn, 1); fx.setParam(SpectralFx::pWarpParam(0), 12); fx.setParam(SpectralFx::pOrder, 1);
                                                         fx.setParam(SpectralFx::pSlotType(0), typeIndex("SpectralBloom")); fx.uiSlotValue(0, 0, 70.f, 1); fx.setParam(SpectralFx::pOn, 1); }},
    };
    bool ok = true;
    for (auto &d : demos)
    {
        Plug p;
        p.preset(0); // the first factory bass
        d.setup(*p.fx);
        p.start();
        Stereo y = p.render(1.2, 0.02, 1.0, {36});
        auto y2 = p.render(1.2, 0.0, 0.9, {43, 48});
        y.l.insert(y.l.end(), y2.l.begin(), y2.l.end());
        y.r.insert(y.r.end(), y2.r.begin(), y2.r.end());
        juce::AudioBuffer<float> buf(2, y.n());
        buf.copyFrom(0, 0, y.l.data(), y.n());
        buf.copyFrom(1, 0, y.r.data(), y.n());
        juce::File f(juce::File(dir).getChildFile(juce::String(d.file) + ".wav"));
        f.deleteFile();
        juce::WavAudioFormat fmt;
        if (auto w = std::unique_ptr<juce::AudioFormatWriter>(fmt.createWriterFor(new juce::FileOutputStream(f), 48000, 2, 24, {}, 0))) w->writeFromAudioSampleBuffer(buf, 0, y.n());
        const auto h = health(y);
        ok &= h.finite && h.peak > 0.01f;
        std::printf("  %-34s peak %.3f rms %.4f latency %u\n", d.file, (double)h.peak, rmsOf(y, 0, y.n()), p.latency());
    }
    CHECK(ok, "demo renders written (a factory bass through the plugin with one effect of every folder)");
}

int main(int argc, char **argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const std::string mode = argc > 1 ? argv[1] : "all";
    const std::string dir = argc > 2 ? argv[2] : "";
    if (mode == "list") { listCatalogue(); return 0; }
    // "checks" = everything that passes or fails (what the Mac build runs); "all" adds the CPU table and the demo files
    auto want = [&](const char *m) {
        const bool measure = std::string(m) == "cpu" || std::string(m) == "demos";
        return mode == "all" || mode == m || (mode == "checks" && !measure);
    };
    if (want("catalogue")) testCatalogue();
    if (want("effects")) testEveryEffect();
    if (want("warp")) testWarpVariants();
    if (want("reference")) testReference(dir);
    if (want("latency")) testLatency();
    if (want("latency")) testFftRestart();
    if (want("clicks")) testClicks();
    if (want("alloc")) testAllocations();
    if (want("state")) testState();
    if (want("bypass")) testBypass();
    if (want("cpu")) testCpu(dir);
    if (!dir.empty() && want("demos")) writeDemos(dir + "/demos");
    return finish();
}
