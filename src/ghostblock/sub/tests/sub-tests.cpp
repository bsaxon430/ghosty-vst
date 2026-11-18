/*
 * Poltergeist 2 (GhostBlock) - SUB checks (no ears, no screen).
 *   p2-sub-tests checks        everything below (what the Mac build runs)
 *   p2-sub-tests <name>        one of: port bombaz params tracking glide clicks aliasing levels presets state follow
 *                              bypass alloc cpu
 * Exit code 0 = ALL PASS.
 */
#include "ghostblock/sub/sub-bass.h"
#include "ghostblock/sub/bombaz-voice.h"
#include "ghostblock/sub/subhoofer-port.h"
#include "ghostblock/spectral/spectral-fx.h"
#include "ghostblock/spectral/tests/spectral-test-util.h"
#include "subhoofer-golden.h"
#include "bombaz-reference.h"
#include "ghostblock/morph.h"
#include "presets/preset-manager.h"
#include "synth/synth.h"
#include <juce_dsp/juce_dsp.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <chrono>
#include <complex>
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
extern ghostblock::p2::sub::SubBass *getSubFromPlugin(const clap_plugin_t *);
extern Synth *getSynthFromPlugin(const clap_plugin_t *);
} // namespace baconpaul::six_sines
namespace ss = baconpaul::six_sines;
using namespace ghostblock::p2::sub;
using ghostblock::p2::spectral::SpectralFx;
using namespace p2t;
using Mono = std::vector<float>;

// ---------------------------------------------------------------------------------------- measuring
static double hzOfKey(double key) { return 440.0 * std::exp2((key - 69.0) / 12.0); }
// the size of the partial at `f` Hz in x[a, b) (Hann window; 1 = a sine of amplitude 1)
static double toneMag(const Mono &x, int a, int b, double sr, double f)
{
    const int n = b - a;
    const std::complex<double> rot = std::polar(1.0, -2.0 * M_PI * f / sr);
    std::complex<double> z(1.0, 0.0), acc(0.0, 0.0);
    double ws = 0;
    for (int i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / (n - 1));
        acc += (double)x[(size_t)(a + i)] * w * z;
        ws += w;
        z *= rot;
    }
    return 2.0 * std::abs(acc) / ws;
}
// the strongest partial within `span` cents of `want`, in Hz
static double pitchNear(const Mono &x, int a, int b, double sr, double want, double span = 90.0)
{
    const double step = 3.0;
    double bestC = 0, best = -1;
    for (double c = -span; c <= span; c += step)
    {
        const double m = toneMag(x, a, b, sr, want * std::exp2(c / 1200.0));
        if (m > best)
        {
            best = m;
            bestC = c;
        }
    }
    const double lo = toneMag(x, a, b, sr, want * std::exp2((bestC - step) / 1200.0)), hi = toneMag(x, a, b, sr, want * std::exp2((bestC + step) / 1200.0));
    const double den = lo - 2.0 * best + hi;
    const double off = std::fabs(den) > 1e-18 ? 0.5 * (lo - hi) / den : 0.0;
    return want * std::exp2((bestC + std::clamp(off, -1.0, 1.0) * step) / 1200.0);
}
static double centsOff(double got, double want) { return 1200.0 * std::log2(got / want); }
static double rms(const Mono &x, int a, int b)
{
    double e = 0;
    for (int i = a; i < b; ++i)
        e += (double)x[(size_t)i] * x[(size_t)i];
    return std::sqrt(e / std::max(1, b - a));
}
static float peak(const Mono &x, int a, int b)
{
    float p = 0;
    for (int i = std::max(0, a); i < std::min((int)x.size(), b); ++i)
        p = std::max(p, std::fabs(x[(size_t)i]));
    return p;
}
// how rough x[a, b) is: the biggest step between two samples, and the biggest bend (the second difference: a click
// stands out of a smooth wave far more here)
struct Rough
{
    float step{0}, bend{0};
};
static Rough rough(const Mono &x, int a, int b)
{
    Rough r;
    for (int i = std::max(2, a); i < std::min((int)x.size(), b); ++i)
    {
        r.step = std::max(r.step, std::fabs(x[(size_t)i] - x[(size_t)i - 1]));
        r.bend = std::max(r.bend, std::fabs(x[(size_t)i] - 2.0f * x[(size_t)i - 1] + x[(size_t)i - 2]));
    }
    return r;
}
// the part of the energy of x[a, a + 65536) that is not on a multiple of f0 (in dB under the whole)
static double inharmonicDb(const Mono &x, int a, double sr, double f0)
{
    constexpr int order = 16, n = 1 << order;
    static juce::dsp::FFT fft(order);
    std::vector<float> buf((size_t)n * 2, 0.f);
    for (int i = 0; i < n; ++i)
    {
        const double t = 2.0 * M_PI * i / (n - 1);
        const double w = 0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t);
        buf[(size_t)i] = (float)(x[(size_t)(a + i)] * w);
    }
    fft.performRealOnlyForwardTransform(buf.data(), true);
    double tot = 0, inh = 0;
    const double binHz = sr / n;
    for (int k = (int)std::ceil(20.0 / binHz); k < n / 2; ++k)
    {
        const double e = (double)buf[(size_t)k * 2] * buf[(size_t)k * 2] + (double)buf[(size_t)k * 2 + 1] * buf[(size_t)k * 2 + 1];
        const double f = k * binHz, h = std::round(f / f0);
        tot += e;
        if (h < 1 || std::fabs(f - h * f0) > 6.0 * binHz)
            inh += e;
    }
    return 10.0 * std::log10(std::max(inh, 1e-30) / std::max(tot, 1e-30));
}

// ---------------------------------------------------------------------------------------- the sub on its own
// SubBass with a stand-in for the engine: `engine(t)` says what the engine plays at time t (asked every 8 samples,
// the engine's smallest block); `bus` is what the engine + FX put on the output (silence unless set)
static SubBass::Track heldNote(double t, double on, double off, int key, uint32_t attack = 1, float vel = 1.f)
{
    SubBass::Track tr;
    const double rel = 0.12;
    tr.sounding = t >= on && t < off + rel;
    tr.key = key;
    tr.hz = (float)hzOfKey(key);
    tr.env = vel * (float)(t < off ? std::min(1.0, (t - on) / 0.002) : std::max(0.0, 1.0 - (t - off) / rel));
    tr.attack = attack;
    return tr;
}
struct Rig
{
    SubBass sb;
    double sr;
    uint32_t bs{256}, delay{0};
    int pos{0};
    bool active{false};
    std::function<SubBass::Track(double)> engine;
    std::function<void(double, SubBass &)> perBlock;
    std::function<float(double)> bus;
    Mono right; // (the last render's right channel)
    explicit Rig(double rate = 48000) : sr(rate) { sb.setParam(SubBass::pOn, 1); }
    void set(uint32_t id, double v) { sb.setParam(id, v); }
    void preset(int p)
    {
        sb.uiPreset(p);
        sb.sendToHost(nullptr);
    }
    Mono render(double seconds)
    {
        if (!active)
        {
            sb.activate(sr);
            active = true;
        }
        const int total = (int)(sr * seconds);
        Mono out((size_t)total, 0.f);
        right.assign((size_t)total, 0.f);
        std::vector<float> L(bs), R(bs);
        for (int p = 0; p < total; p += (int)bs)
        {
            const uint32_t n = (uint32_t)std::min<int>((int)bs, total - p);
            if (perBlock)
                perBlock((pos + p) / sr, sb);
            for (uint32_t i = 0; i < n; ++i)
                L[i] = R[i] = bus ? bus((pos + p + (int)i) / sr) : 0.f;
            for (uint32_t f = 0; f < n; f += 8)
                if (sb.wantsTrack())
                    sb.track(f, engine ? engine((pos + p + (int)f) / sr) : SubBass::Track());
            sb.process(L.data(), R.data(), n, delay);
            std::copy(L.begin(), L.begin() + n, out.begin() + p);
            std::copy(R.begin(), R.begin() + n, right.begin() + p);
        }
        pos += total;
        return out;
    }
};

// ---------------------------------------------------------------------------------------- the whole plugin
static std::atomic<int> restartAsks{0};
struct EvList
{
    std::vector<const clap_event_header_t *> all;
    static uint32_t size(const clap_input_events *l) { return (uint32_t)((EvList *)l->ctx)->all.size(); }
    static const clap_event_header_t *get(const clap_input_events *l, uint32_t i) { return ((EvList *)l->ctx)->all[i]; }
};
static bool tryPush(const clap_output_events *, const clap_event_header_t *) { return true; }
struct Act
{
    enum What { On, Off, Bend, Param } what;
    double t;
    int key{0};       // On / Off
    double v{0.8};    // velocity; Bend: -1 .. 1; Param: the value
    uint32_t id{0};   // Param
};
struct Plug
{
    clap_host host{};
    const clap_plugin *pl{nullptr};
    SubBass *sub{nullptr};
    SpectralFx *fx{nullptr};
    double sr{48000};
    uint32_t bs{256};
    int pos{0};
    bool active{false}, guarded{false};
    Plug()
    {
        host.clap_version = CLAP_VERSION;
        host.name = "p2-sub-tests";
        host.vendor = "ghostblock";
        host.url = "";
        host.version = "1";
        host.get_extension = [](const clap_host *, const char *) -> const void * { return nullptr; };
        host.request_restart = [](const clap_host *) { ++restartAsks; };
        host.request_process = [](const clap_host *) {};
        host.request_callback = [](const clap_host *) {};
        pl = ss::makePlugin(&host, false);
        pl->init(pl);
        sub = ss::getSubFromPlugin(pl);
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
    uint32_t paramCount()
    {
        auto *p = static_cast<const clap_plugin_params *>(pl->get_extension(pl, CLAP_EXT_PARAMS));
        return p ? p->count(pl) : 0u;
    }
    // renders `seconds`, doing `acts` at their times (seconds from the start of this call)
    Stereo render(double seconds, const std::vector<Act> &acts)
    {
        const int total = (int)(sr * seconds);
        Stereo out(total);
        std::vector<float> inL(bs, 0.f), inR(bs, 0.f), oL(bs), oR(bs);
        EvList ev;
        clap_input_events in{&ev, &EvList::size, &EvList::get};
        clap_output_events o{nullptr, &tryPush};
        std::vector<clap_event_note> notes;
        std::vector<clap_event_midi> midis;
        std::vector<clap_event_param_value> params;
        notes.reserve(64);
        midis.reserve(64);
        params.reserve(64);
        ev.all.reserve(256);
        std::vector<std::pair<uint32_t, const clap_event_header_t *>> order;
        order.reserve(256);
        for (int p = 0; p < total; p += (int)bs)
        {
            notes.clear();
            midis.clear();
            params.clear();
            ev.all.clear();
            order.clear();
            for (auto &a : acts)
            {
                const int at = (int)std::lround(a.t * sr);
                if (at < p || at >= p + (int)bs)
                    continue;
                const uint32_t t = (uint32_t)(at - p);
                if (a.what == Act::On || a.what == Act::Off)
                {
                    clap_event_note n{};
                    n.header.size = sizeof(n);
                    n.header.time = t;
                    n.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                    n.header.type = a.what == Act::On ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF;
                    n.note_id = -1;
                    n.key = (int16_t)a.key;
                    n.velocity = a.v;
                    notes.push_back(n);
                    order.push_back({t, &notes.back().header});
                }
                else if (a.what == Act::Bend)
                {
                    const int v14 = std::clamp((int)std::lround(8192.0 + a.v * 8191.0), 0, 16383);
                    clap_event_midi m{};
                    m.header.size = sizeof(m);
                    m.header.time = t;
                    m.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                    m.header.type = CLAP_EVENT_MIDI;
                    m.data[0] = 0xE0;
                    m.data[1] = (uint8_t)(v14 & 0x7F);
                    m.data[2] = (uint8_t)(v14 >> 7);
                    midis.push_back(m);
                    order.push_back({t, &midis.back().header});
                }
                else
                {
                    clap_event_param_value pv{};
                    pv.header.size = sizeof(pv);
                    pv.header.time = t;
                    pv.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                    pv.header.type = CLAP_EVENT_PARAM_VALUE;
                    pv.param_id = a.id;
                    pv.note_id = -1; pv.port_index = -1; pv.channel = -1; pv.key = -1;
                    pv.value = a.v;
                    params.push_back(pv);
                    order.push_back({t, &params.back().header});
                }
            }
            std::stable_sort(order.begin(), order.end(), [](auto &x, auto &y) { return x.first < y.first; });
            for (auto &e : order) ev.all.push_back(e.second);
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
            if (guarded) guard = 1;
            pl->process(pl, &pr);
            guard = 0;
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
static std::vector<Act> chord(std::vector<int> keys, double on, double off)
{
    std::vector<Act> a;
    for (int k : keys)
    {
        a.push_back({Act::On, on, k});
        if (off < 1e8) a.push_back({Act::Off, off, k});
    }
    return a;
}
// everything SUB has, set away from its defaults (ON is left alone)
static void busySub(SubBass &s)
{
    s.setParam(SubBass::pLevel, 3);
    s.setParam(SubBass::pOctave, -2);
    s.setParam(SubBass::pWidth, 55);
    s.setParam(SubBass::pDrive, 80);
    s.setParam(SubBass::pHarmonics, 90);
    s.setParam(SubBass::pMode, 2);
    s.setParam(SubBass::pBlend, 85);
    s.setParam(SubBass::pGlide, 120);
    s.setParam(SubBass::pTune, -7);
    s.setParam(SubBass::pPhase, 1);
    s.setParam(SubBass::pRoom, 70);
}

// ======================================================================================== (a) the Subhoofer port
static void goldenInputs(std::vector<float> &l, std::vector<float> &r, int N)
{ // (the same numbers libs/ghostblock-sub/reference/src/main.rs feeds upstream's Rust)
    l.assign((size_t)N, 0);
    r.assign((size_t)N, 0);
    uint32_t s = 0x12345678u;
    for (int i = 0; i < N; ++i)
    {
        if (i < N / 2)
        {
            const float p = (float)(i % 37) / 37.0f, q = (float)(i % 29) / 29.0f;
            l[(size_t)i] = 16.0f * p * p * (1.0f - p) * (1.0f - p) - 0.3f;
            r[(size_t)i] = 0.5f - 16.0f * q * q * (1.0f - q) * (1.0f - q);
        }
        else
        {
            s = s * 1664525u + 1013904223u;
            l[(size_t)i] = (float)(s >> 8) / 8388608.0f - 1.0f;
            s = s * 1664525u + 1013904223u;
            r[(size_t)i] = (float)(s >> 8) / 8388608.0f - 1.0f;
        }
    }
}
static void testPort()
{
    section("Subhoofer: the C++ port against upstream's Rust");
    using namespace subhoofer;
    const int N = kSubhooferGoldenN;
    std::vector<float> l, r;
    goldenInputs(l, r, N);
    int exactCases = 0;
    long diffs = 0, values = 0;
    double worst = 0; // the biggest difference, as a part of the biggest value of its case
    for (int c = 0; c < kSubhooferGoldenCases; ++c)
    {
        const auto &k = kSubhooferGoldenCase[c];
        Harmonics h;
        Settings s;
        s.h_algorithm = (AlgorithmType)k.algo;
        s.harmonics = k.harmonics;
        s.hoof_hardness = k.hardness;
        s.overall_scale = k.scale;
        s.custom_harmonics1 = kSubhooferGoldenCustom[0];
        s.custom_harmonics2 = kSubhooferGoldenCustom[1];
        s.custom_harmonics3 = kSubhooferGoldenCustom[2];
        s.custom_harmonics4 = kSubhooferGoldenCustom[3];
        s.dry_wet = kSubhooferGoldenDryWet;
        s.output_gain = kSubhooferGoldenOutGain;
        bool exact = true;
        double maxAbs = 1e-9, maxDiffHere = 0;
        for (int i = 0; i < N; ++i)
        {
            float a = l[(size_t)i] * k.gain, b = r[(size_t)i] * k.gain;
            h.process(a, b, s);
            float ga, gb;
            const uint32_t ua = kSubhooferGolden[(size_t)(c * N + i) * 2], ub = kSubhooferGolden[(size_t)(c * N + i) * 2 + 1];
            std::memcpy(&ga, &ua, 4);
            std::memcpy(&gb, &ub, 4);
            for (auto pr : {std::pair<float, float>(a, ga), std::pair<float, float>(b, gb)})
            {
                ++values;
                maxAbs = std::max(maxAbs, (double)std::fabs(pr.second));
                if (std::memcmp(&pr.first, &pr.second, 4) != 0)
                {
                    exact = false;
                    ++diffs;
                    maxDiffHere = std::max(maxDiffHere, std::fabs((double)pr.first - pr.second));
                }
            }
        }
        if (exact) ++exactCases;
        worst = std::max(worst, maxDiffHere / maxAbs);
    }
    int fnExact = 0, fnAll = 0;
    double fnWorst = 0;
    for (int row = 0; row < kSubhooferGoldenFnRows; ++row)
    {
        const unsigned int *g = kSubhooferGoldenFn + row * 8;
        float x, d;
        std::memcpy(&x, g, 4);
        std::memcpy(&d, g + 1, 4);
        const float v[6] = {chebyshev_tape(x, d), tape_saturation(x, d), b_bass_saturation(x, d), c_bass_saturation(x, d),
                            custom_sincos_saturation(x, d * 31.0f, d * 189.0f, d * 25.0f, d * 26.0f), leaf_saturation(x, db_to_gain(-30.0f), d)};
        for (int j = 0; j < 6; ++j)
        {
            float q;
            std::memcpy(&q, g + 2 + j, 4);
            ++fnAll;
            if (std::memcmp(&q, &v[j], 4) == 0) ++fnExact;
            else fnWorst = std::max(fnWorst, std::fabs((double)q - v[j]) / std::max(1e-6, (double)std::fabs(q)));
        }
    }
    std::printf("upstream's process(): %d of %d cases bit-identical (6 algorithms x 3 Harmonics x 2 sample rates x 2 hardness / input gains, %d stereo samples each); %ld of %ld values differ, the biggest by %.3g of its case's peak\n",
                exactCases, kSubhooferGoldenCases, N, diffs, values, worst);
    std::printf("upstream's six saturation functions: %d of %d values bit-identical; biggest relative difference %.3g\n", fnExact, fnAll, fnWorst);
#if defined(__GLIBC__)
    // (the reference values were made by rustc on Linux; with the same maths library the port has to match to the bit)
    CHECK(exactCases == kSubhooferGoldenCases && diffs == 0, "the port of Subhoofer's harmonic path matches upstream's Rust bit for bit (72 cases)");
    CHECK(fnExact == fnAll, "the six ported saturation functions match upstream's Rust bit for bit");
#else
    // (another maths library - sin, cos, tanh, atan, pow round differently in the last place - so: the same within a hair)
    CHECK(worst < 2e-4, "the port of Subhoofer's harmonic path matches upstream's Rust (72 cases; this platform's maths library rounds differently)");
    CHECK(fnWorst < 2e-5, "the six ported saturation functions match upstream's Rust");
#endif
    // the two helpers the plugin uses
    long zeroDiff = 0, splitDiff = 0;
    uint32_t seed = 99;
    for (int a = 0; a < 6; ++a)
    {
        Settings s;
        s.h_algorithm = (AlgorithmType)a;
        s.harmonics = 0.02f;
        s.dry_wet = 1.0f;
        Harmonics whole, split;
        for (int i = 0; i < 20000; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            const float x = ((float)(seed >> 8) / 8388608.0f - 1.0f) * (i % 3 == 0 ? 6.0f : i % 3 == 1 ? 1.0f : 0.02f);
            const float z = Harmonics::shapeZero(x, s), zr = Harmonics::shapeZeroReference(x, s);
            if (std::memcmp(&z, &zr, 4) != 0) ++zeroDiff;
            float wl = x, wr = x;
            whole.process(wl, wr, s);
            float in = x;
            if (std::fabs(in) < 1.18e-23f) in = 0.1f * 1.18e-17f;
            const float sp = (in + split.dcLeft(split.shapeLeft(x, s)) * s.dry_wet) * s.output_gain;
            if (a != (int)AlgorithmType::ABass3 && std::memcmp(&wl, &sp, 4) != 0) ++splitDiff; // (A Bass 3: the right channel shares the buffer in process())
        }
    }
    CHECK(zeroDiff == 0, "shapeZero (the plugin's shortcut for Harmonics = 0) equals the ported functions called with 0, bit for bit");
    CHECK(splitDiff == 0, "shapeLeft + dcLeft (how the plugin calls the port) equal the left channel of process(), bit for bit");
}

// ======================================================================================== (b) the Bombaz voice
static void testBombaz()
{
    section("Bombaz: our voice against upstream's Synth");
    struct Ev
    {
        int block;
        uint8_t d0, d1, d2;
    };
    struct Scene
    {
        const char *name;
        float sr, width, drive, tune, glide;
        int octave;
        std::vector<Ev> ev;
    };
    // (MIDI lands on block starts; 64-sample blocks; upstream's plugin splits its buffer at each MIDI event the same way)
    const std::vector<Scene> scenes = {
        {"C2, default width, no drive, 192 kHz", 192000, 50, 0, 0, 0, 2, {{2, 0x90, 36, 100}, {700, 0x80, 36, 0}}},
        {"C1 / G1 legato with a 200 ms glide, drive 70, 192 kHz", 192000, 30, 70, 0, 200, 2, {{2, 0x90, 24, 100}, {400, 0x90, 31, 100}, {1200, 0x80, 31, 0}, {1600, 0x80, 24, 0}}},
        {"octave -1, tune +37 cents, pitch wheel, 176.4 kHz", 176400, 80, 25, 37, 0, 1, {{2, 0x90, 48, 90}, {300, 0xE0, 0, 96}, {600, 0xE0, 0x7F, 0x7F}, {900, 0x80, 48, 0}, {1000, 0x90, 55, 90}, {1500, 0x80, 55, 0}}},
        {"octave -2, widest pulse, full drive, a note right after another, 48 kHz", 48000, 0, 100, -12, 60, 0, {{2, 0x90, 60, 100}, {200, 0x80, 60, 0}, {203, 0x90, 67, 100}, {500, 0x90, 72, 100}, {700, 0x80, 72, 0}, {900, 0x80, 67, 0}}},
        {"narrowest pulse, 96 kHz", 96000, 100, 40, 0, 0, 2, {{2, 0x90, 40, 100}, {500, 0x80, 40, 0}}},
    };
    int exact = 0;
    for (auto &sc : scenes)
    {
        // upstream, twice: without drive (our CLEAN side) and with it (our DIRTY side)
        p2sub_ref::BombazRef refClean, refDirty;
        for (auto *r : {&refClean, &refDirty})
            r->prepare(sc.sr);
        refClean.set(sc.width, 0.f, sc.octave, sc.tune, sc.glide);
        refDirty.set(sc.width, sc.drive, sc.octave, sc.tune, sc.glide);
        BombazVoice v;
        v.prepare(sc.sr);
        v.setPureExtension(false); // (the one thing of ours that changes the sound - WIDTH on low notes - is off here)
        // upstream's Synth: the last note held plays; released, the one before it comes back
        std::array<int, 10> queue;
        queue.fill(-1);
        int activeNote = -1;
        auto hz = [&](int note) { return bombaz::noteToHz(std::clamp(note + refClean.transpose(), 0, 127)); };
        const int blocks = 1900, bsz = 64;
        std::vector<float> a((size_t)bsz), b((size_t)bsz);
        long diff = 0;
        double worst = 0, energy = 0;
        size_t next = 0;
        for (int blk = 0; blk < blocks; ++blk)
        {
            refClean.update();
            refDirty.update();
            while (next < sc.ev.size() && sc.ev[next].block == blk)
            {
                const auto &e = sc.ev[next++];
                refClean.midi(e.d0, e.d1, e.d2);
                refDirty.midi(e.d0, e.d1, e.d2);
                refClean.update(); // (the wheel is part of the pitch modulation)
                refDirty.update();
                const bool on = (e.d0 & 0xF0) == 0x90 && e.d2 > 0, off = (e.d0 & 0xF0) == 0x80 || ((e.d0 & 0xF0) == 0x90 && e.d2 == 0);
                if (on)
                { // Synth::noteOn
                    activeNote = e.d1;
                    for (size_t i = 0; i < queue.size(); ++i)
                        if (queue[i] == e.d1)
                        {
                            for (; i < queue.size() - 1; ++i) queue[i] = queue[i + 1];
                            queue[i] = -1;
                            break;
                        }
                    for (size_t i = queue.size() - 1; i > 0; --i) queue[i] = queue[i - 1];
                    queue[0] = e.d1;
                    v.setModulation(refClean.pitchModulation());
                    v.start(hz(e.d1));
                }
                else if (off)
                { // Synth::noteOff
                    for (size_t i = 0; i < queue.size(); ++i)
                        if (queue[i] == e.d1)
                        {
                            for (; i < queue.size() - 1; ++i) queue[i] = queue[i + 1];
                            queue[i] = -1;
                            break;
                        }
                    if (activeNote == e.d1)
                    {
                        activeNote = queue[0];
                        if (activeNote != -1) v.legato(hz(activeNote));
                        else v.stop();
                    }
                }
            }
            refClean.render(a.data(), bsz);
            refDirty.render(b.data(), bsz);
            v.setWidth(refClean.width());
            v.setDrive(refDirty.drive());
            v.setGlideMs(refClean.glideMs());
            v.setModulation(refClean.pitchModulation());
            v.pushParameters();
            for (int i = 0; i < bsz; ++i)
            {
                float c, d, dp;
                v.render(c, d, dp, true);
                energy += (double)a[(size_t)i] * a[(size_t)i];
                for (auto pr : {std::pair<float, float>(c, a[(size_t)i]), std::pair<float, float>(d, b[(size_t)i])})
                    if (std::memcmp(&pr.first, &pr.second, 4) != 0)
                    {
                        ++diff;
                        worst = std::max(worst, std::fabs((double)pr.first - pr.second));
                    }
            }
        }
        std::printf("%-72s %ld of %d samples differ (biggest %.3g); upstream rms %.4f\n", sc.name, diff, blocks * bsz * 2, worst, std::sqrt(energy / (blocks * bsz)));
        if (diff == 0 && energy > 1.0) ++exact;
    }
    CHECK(exact == (int)scenes.size(), "our Bombaz voice (clean and driven) is sample for sample upstream's Synth: notes, legato, glide, width, drive, tune, pitch wheel, octave");

    // what is ours: WIDTH 0 on low notes = one pure window wave on every note (no gap between pulses)
    BombazVoice v;
    v.prepare(192000);
    v.setWidth(0);
    v.pushParameters();
    v.start(27.5f);
    float mn = 1e9f;
    int run = 0, longest = 0;
    for (int i = 0; i < 192000; ++i)
    {
        float c, d, dp;
        v.render(c, d, dp, true);
        if (i > 20000)
        {
            run = dp == 0.f ? run + 1 : 0;
            longest = std::max(longest, run);
            mn = std::min(mn, dp);
        }
    }
    // (where one pulse ends and the next begins the window is at nothing for a sample or two; no more)
    CHECK(longest <= 2 && mn >= 0.f, "WIDTH 0 on a 27.5 Hz note: the pulse fills its cycle (upstream's 15 ms pulse would leave 21 ms of nothing)");
}

// ======================================================================================== (c) host parameters
static void testParams()
{
    section("host parameters");
    SubBass s;
    std::set<uint32_t> ids;
    bool ok = true;
    for (uint32_t i = 0; i < SubBass::kParams; ++i)
    {
        clap_param_info info{};
        ok &= s.paramInfo(i, &info) && SubBass::isParam(info.id) && info.name[0] != 0 && info.max_value > info.min_value;
        ok &= (info.flags & CLAP_PARAM_IS_AUTOMATABLE) != 0 && info.default_value >= info.min_value && info.default_value <= info.max_value;
        ok &= s.getParam(info.id) == info.default_value;
        ids.insert(info.id);
        // text <-> value
        for (double f : {0.0, 0.3, 1.0})
        {
            double v = info.min_value + f * (info.max_value - info.min_value);
            if (info.flags & CLAP_PARAM_IS_STEPPED) v = std::round(v);
            char txt[64]{};
            double back = -1e9;
            ok &= s.valueToText(info.id, v, txt, sizeof(txt)) && s.textToValue(info.id, txt, &back);
            const double tol = info.id == SubBass::pLevel ? 0.06 : 1.0;
            if (std::fabs(back - v) > tol)
            {
                ok = false;
                std::printf("  %s: %g -> \"%s\" -> %g\n", info.name, v, txt, back);
            }
        }
    }
    CHECK(ok && ids.size() == SubBass::kParams && SubBass::kParams == 12, "the host sees 12 SUB parameters: own ids, names, ranges, automatable, text both ways");
    CHECK(!s.on() && s.getParam(SubBass::pOn) == 0, "SUB is off by default");
    Plug p;
    const uint32_t n = p.paramCount();
    auto *pp = static_cast<const clap_plugin_params *>(p.pl->get_extension(p.pl, CLAP_EXT_PARAMS));
    int found = 0;
    bool last = true;
    for (uint32_t i = 0; i < n; ++i)
    {
        clap_param_info info{};
        pp->get_info(p.pl, i, &info);
        if (SubBass::isParam(info.id))
        {
            ++found;
            last &= i >= n - SubBass::kParams;
        }
    }
    double v = 0;
    p.sub->setParam(SubBass::pWidth, 42);
    const bool got = pp->get_value(p.pl, SubBass::pWidth, &v) && v == 42;
    CHECK(found == 12 && last && got, "the plugin lists them after its other parameters (existing parameter numbers are unchanged)");
}

// ======================================================================================== (d) pitch
static void testTracking()
{
    section("the sub's pitch (measured from the rendered sound)");
    double worst = 0;
    int n = 0, bad = 0;
    for (double sr : {44100.0, 48000.0, 96000.0})
        for (int key : {24, 31, 36, 43, 48, 60})
            for (int oct : {0, -1, -2})
            {
                const double want = hzOfKey(key) * std::exp2(oct);
                if (want < 15.0) continue;
                Rig r(sr);
                r.set(SubBass::pOctave, oct);
                r.engine = [&](double t) { return heldNote(t, 0.05, 9.0, key); };
                const auto x = r.render(2.4);
                const double got = pitchNear(x, (int)(1.0 * sr), (int)(2.2 * sr), sr, want), c = std::fabs(centsOff(got, want));
                // the note has to be THE note: more of it than of its octave below or above
                const double m1 = toneMag(x, (int)(1.0 * sr), (int)(2.2 * sr), sr, want), mLo = toneMag(x, (int)(1.0 * sr), (int)(2.2 * sr), sr, want / 2);
                worst = std::max(worst, c);
                ++n;
                if (c > 1.0 || m1 < 0.05 || mLo > 0.05 * m1)
                {
                    ++bad;
                    std::printf("  %g Hz key %d octave %d: want %.3f Hz, got %.3f (%.2f cents); size %.3f, octave below %.4f\n", sr, key, oct, want, got, c, m1, mLo);
                }
            }
    std::printf("%d notes at 44.1 / 48 / 96 kHz, octaves 0 / -1 / -2: biggest error %.3f cents\n", n, worst);
    CHECK(bad == 0, "the sub plays the engine's note at the chosen octave, within 1 cent, at 44.1, 48 and 96 kHz");

    // what moves the note: TUNE, the bend the engine reports, dirt and harmonics must not move it
    bad = 0;
    worst = 0;
    struct Case
    {
        const char *what;
        double tune, bend;
        bool dirty;
    };
    for (auto &c : {Case{"TUNE +37 cents", 37, 0, false}, Case{"TUNE -100 cents", -100, 0, false}, Case{"bend +2 semitones", 0, 2, false}, Case{"bend -7.5 semitones", 0, -7.5, false},
                    Case{"full dirt", 0, 0, true}, Case{"full dirt, TUNE +11, bend +1", 11, 1, true}})
        for (double sr : {44100.0, 96000.0})
        {
            Rig r(sr);
            r.set(SubBass::pTune, c.tune);
            if (c.dirty)
            {
                r.set(SubBass::pWidth, 60);
                r.set(SubBass::pDrive, 100);
                r.set(SubBass::pHarmonics, 100);
                r.set(SubBass::pMode, 2);
                r.set(SubBass::pBlend, 100);
            }
            r.engine = [&](double t) {
                auto tr = heldNote(t, 0.05, 9.0, 40);
                tr.modSemis = (float)c.bend;
                return tr;
            };
            const auto x = r.render(2.4);
            const double want = hzOfKey(40) * 0.5 * std::exp2(c.tune / 1200.0 + c.bend / 12.0);
            const double got = pitchNear(x, (int)(1.0 * sr), (int)(2.2 * sr), sr, want), e = std::fabs(centsOff(got, want));
            worst = std::max(worst, e);
            if (e > 1.0)
            {
                ++bad;
                std::printf("  %s at %g Hz: want %.3f got %.3f (%.2f cents)\n", c.what, sr, want, got, e);
            }
        }
    std::printf("TUNE, pitch bend, dirt: biggest error %.3f cents\n", worst);
    CHECK(bad == 0, "TUNE and the engine's pitch bend move it by exactly that much; drive and harmonics do not move it");
}

// ======================================================================================== (e) glide
static void testGlide()
{
    section("glide");
    bool ok = true;
    for (double sr : {44100.0, 48000.0, 96000.0})
    {
        // C1 held, then C2 legato at 1.0 s with GLIDE 300 ms (the sub plays an octave below: 32.7 -> 65.4 Hz)
        Rig r(sr);
        r.set(SubBass::pGlide, 300);
        r.engine = [&](double t) { return heldNote(t, 0.05, 9.0, t < 1.0 ? 36 : 48); };
        const auto x = r.render(2.2);
        auto at = [&](double t0, double t1, double want) { return pitchNear(x, (int)(t0 * sr), (int)(t1 * sr), sr, want, 400.0); };
        const double f1 = hzOfKey(36) / 2, f2 = hzOfKey(48) / 2, mid = std::sqrt(f1 * f2);
        const double before = at(0.6, 0.98, f1), during = at(1.10, 1.20, mid), after = at(1.45, 2.1, f2);
        std::printf("%g Hz: before %.2f Hz (want %.2f), halfway through the glide %.2f Hz (want about %.2f), after %.2f Hz (want %.2f)\n", sr, before, f1, during, mid, after, f2);
        ok &= std::fabs(centsOff(before, f1)) < 3 && std::fabs(centsOff(after, f2)) < 3 && std::fabs(centsOff(during, mid)) < 150;
        // without GLIDE the same change is there within one cycle
        Rig q(sr);
        q.engine = r.engine;
        const auto y = q.render(2.2);
        const double soon = pitchNear(y, (int)(1.06 * sr), (int)(1.4 * sr), sr, f2, 400.0);
        ok &= std::fabs(centsOff(soon, f2)) < 3;
        // the engine's own glide (it reports the note moving): the sub follows that, with no glide of its own on top
        Rig e(sr);
        e.set(SubBass::pGlide, 300);
        e.engine = [&](double t) {
            auto tr = heldNote(t, 0.05, 9.0, t < 1.0 ? 36 : 48);
            if (t >= 1.0 && t < 1.1)
            { // (100 ms from C1 up to C2)
                tr.modSemis = (float)(-12.0 * (1.0 - (t - 1.0) / 0.1));
                tr.engineGlides = true;
            }
            return tr;
        };
        const auto z = e.render(2.2);
        const double eng = pitchNear(z, (int)(1.16 * sr), (int)(1.5 * sr), sr, f2, 400.0);
        ok &= std::fabs(centsOff(eng, f2)) < 3;
    }
    CHECK(ok, "GLIDE: a legato note slides to the new pitch in the set time (Bombaz's glide); off = at once; the engine's own glide is followed as it is");
}

// ======================================================================================== (f) clicks
static void testClicks()
{
    section("clicks");
    struct Sound
    {
        const char *name;
        std::vector<std::pair<uint32_t, double>> set;
    };
    const std::vector<Sound> sounds = {
        {"PURE", {}},
        {"8 STACK dirty", {{SubBass::pWidth, 30}, {SubBass::pDrive, 70}, {SubBass::pHarmonics, 60}, {SubBass::pMode, 2}, {SubBass::pBlend, 80}}},
        {"DURO everything up", {{SubBass::pWidth, 60}, {SubBass::pDrive, 100}, {SubBass::pHarmonics, 100}, {SubBass::pMode, 3}, {SubBass::pBlend, 100}}},
        {"A BASS 3 full", {{SubBass::pWidth, 20}, {SubBass::pDrive, 40}, {SubBass::pHarmonics, 100}, {SubBass::pMode, 0}, {SubBass::pBlend, 100}}},
        {"A BASS 2 full", {{SubBass::pWidth, 40}, {SubBass::pDrive, 60}, {SubBass::pHarmonics, 100}, {SubBass::pMode, 1}, {SubBass::pBlend, 100}}},
        {"TANH full", {{SubBass::pWidth, 10}, {SubBass::pDrive, 100}, {SubBass::pHarmonics, 100}, {SubBass::pMode, 4}, {SubBass::pBlend, 100}}},
    };
    // every scene: a note from 0.05 s, something at 0.7 s, released at 1.3 s (the engine's release ends at 1.42 s)
    enum Scene { Plain, Legato, Restart, Free, Glide, ModeChange, PresetChange, OctaveChange, BlendSweep, HarmSweep, kScenes };
    const char *sceneName[kScenes] = {"note on / off", "legato +7", "new note +7 (RESTART)", "new note +7 (FREE)", "glide +12", "another algorithm while held",
                                      "another preset while held", "another octave while held", "CLEAN / DIRTY swept from 0 to 100", "HARMONICS swept from 0 to 100"};
    double worstStep = 0, worstBend = 0;
    std::string worstWhere;
    int bad = 0, cases = 0;
    for (double sr : {44100.0, 48000.0, 96000.0})
        for (auto &snd : sounds)
            for (int key : {36, 50})
                for (int scn = 0; scn < kScenes; ++scn)
                {
                    Rig r(sr);
                    for (auto &[id, v] : snd.set) r.set(id, v);
                    if (scn == Free) r.set(SubBass::pPhase, 1);
                    if (scn == Glide) r.set(SubBass::pGlide, 200);
                    const bool second = scn == Legato || scn == Restart || scn == Free || scn == Glide;
                    const int key2 = scn == Glide ? key + 12 : key + 7;
                    r.engine = [&](double t) {
                        const bool two = second && t >= 0.7;
                        return heldNote(t, 0.05, 1.3, two ? key2 : key, (two && (scn == Restart || scn == Free)) ? 2 : 1);
                    };
                    r.perBlock = [&](double t, SubBass &s) {
                        if (t < 0.7) return;
                        if (scn == ModeChange) s.setParam(SubBass::pMode, ((int)s.getParam(SubBass::pMode) == 4) ? 1 : 4);
                        if (scn == PresetChange) { s.setParam(SubBass::pWidth, 55); s.setParam(SubBass::pDrive, 100); s.setParam(SubBass::pHarmonics, 100); s.setParam(SubBass::pMode, 2); s.setParam(SubBass::pBlend, 100); }
                        if (scn == OctaveChange) s.setParam(SubBass::pOctave, -2);
                        // (the two sweeps end on the brightest setting, so nothing on the way may be rougher than the ends)
                        if (scn == BlendSweep) s.setParam(SubBass::pBlend, 100.0 * std::min(1.0, (t - 0.7) / 0.3));
                        if (scn == HarmSweep) s.setParam(SubBass::pHarmonics, 100.0 * std::min(1.0, (t - 0.7) / 0.3));
                    };
                    if (scn == ModeChange && snd.set.empty()) { r.set(SubBass::pHarmonics, 100); r.set(SubBass::pBlend, 100); r.set(SubBass::pMode, 1); }
                    const auto x = r.render(1.6);
                    // how rough the sound is by itself, before and after the event
                    const Rough s1 = rough(x, (int)(0.25 * sr), (int)(0.6 * sr)), s2 = rough(x, (int)(1.0 * sr), (int)(1.25 * sr));
                    const Rough ref{std::max(s1.step, s2.step), std::max(s1.bend, s2.bend)};
                    const float pk = peak(x, 0, (int)x.size());
                    ++cases;
                    auto event = [&](const char *what, double t, double len) {
                        const Rough e = rough(x, (int)((t - 0.02) * sr), (int)((t + len) * sr));
                        const double rs = e.step / std::max(1e-9f, ref.step), rb = e.bend / std::max(1e-9f, ref.bend);
                        // a click = far rougher than the sound itself AND big enough to hear next to it
                        const bool click = (rs > 1.6 && e.step > 0.004f * pk) || (rb > 2.5 && e.bend > 0.0008f * pk);
                        if (rs > worstStep || rb > worstBend)
                        {
                            if (rs > worstStep) worstStep = rs;
                            if (rb > worstBend) worstBend = rb;
                            worstWhere = std::string(snd.name) + ", key " + std::to_string(key) + ", " + sceneName[scn] + ": " + what;
                        }
                        if (click)
                        {
                            ++bad;
                            std::printf("  CLICK %g Hz %s key %d, %s, at %s: step x%.2f (%.5f), bend x%.2f (%.6f), peak %.3f\n", sr, snd.name, key, sceneName[scn], what, rs, e.step, rb, e.bend, pk);
                        }
                    };
                    event("note on", 0.05, 0.06);
                    if (scn != Plain) event("the change", 0.7, scn == Glide || scn == BlendSweep || scn == HarmSweep ? 0.35 : 0.08);
                    event("note off", 1.3, 0.2);
                    if (!(pk > 0.02f && pk < 0.9f) || !std::isfinite(pk))
                    {
                        ++bad;
                        std::printf("  LEVEL %g Hz %s key %d, %s: peak %.3f\n", sr, snd.name, key, sceneName[scn], pk);
                    }
                }
    std::printf("%d renders (3 sample rates x 6 sounds x 2 keys x 10 scenes): the roughest moment is x%.2f the sound's own biggest step, x%.2f its own biggest bend (%s)\n", cases, worstStep, worstBend,
                worstWhere.c_str());
    CHECK(bad == 0, "no clicks at note on, note off, legato, restart, glide, or when the algorithm / preset / octave / blend changes while a note sounds");

    // SUB ON switched on and off while a note sounds, with the engine's own sound on the output (MAKE ROOM cuts into it)
    bad = 0;
    for (double sr : {44100.0, 48000.0, 96000.0})
        for (double room : {0.0, 40.0, 100.0})
        {
            Rig r(sr);
            r.sb.setParam(SubBass::pOn, 0);
            r.set(SubBass::pRoom, room);
            r.set(SubBass::pOctave, 0);
            r.engine = [&](double t) { return heldNote(t, 0.02, 9.0, 33); };
            r.bus = [&](double t) { return (float)(0.25 * std::sin(2 * M_PI * 55.0 * t) + 0.1 * std::sin(2 * M_PI * 110.0 * t + 0.4) + 0.05 * std::sin(2 * M_PI * 440.0 * t)); };
            r.perBlock = [&](double t, SubBass &s) { s.setParam(SubBass::pOn, (t >= 0.5 && t < 1.2) ? 1 : 0); };
            const auto x = r.render(2.0);
            const Rough before = rough(x, (int)(0.1 * sr), (int)(0.45 * sr)), during = rough(x, (int)(0.8 * sr), (int)(1.15 * sr));
            const Rough ref{std::max(before.step, during.step), std::max(before.bend, during.bend)};
            for (double t : {0.5, 1.2})
            {
                const Rough e = rough(x, (int)((t - 0.01) * sr), (int)((t + 0.08) * sr));
                if (e.step > 1.5f * ref.step || e.bend > 2.0f * ref.bend)
                {
                    ++bad;
                    std::printf("  CLICK %g Hz MAKE ROOM %g: SUB %s: step x%.2f bend x%.2f\n", sr, room, t < 1 ? "on" : "off", e.step / ref.step, e.bend / ref.bend);
                }
            }
            // and once it is off again the output is the engine's, exactly
            bool exact = true;
            for (int i = (int)(1.5 * sr); i < (int)x.size(); ++i)
                exact &= x[(size_t)i] == r.bus(i / sr);
            if (!exact) { ++bad; std::printf("  %g Hz MAKE ROOM %g: the output is not the engine's again after SUB went off\n", sr, room); }
        }
    CHECK(bad == 0, "SUB switched on and off while playing: 20 ms fades, no click in the sub or in the engine's sound, and the output is exactly the engine's again afterwards");
}

// ======================================================================================== (g) aliasing
static void testAliasing()
{
    section("aliasing (everything turned up, high notes)");
    // WIDTH 100, DRIVE 100, HARMONICS 100, DIRTY only: the brightest the sub can be
    struct Note
    {
        int key, oct;
        double limitDb;
        const char *what;
    };
    bool ok = true;
    for (auto &nt : {Note{60, -1, -60.0, "131 Hz (the top of a sub's range)"}, Note{72, 0, -46.0, "523 Hz"}, Note{84, 0, -46.0, "1047 Hz"}})
    {
        double worst = -200;
        std::string where;
        for (double sr : {44100.0, 48000.0, 96000.0})
            for (int m = 0; m < kModes; ++m)
            {
                Rig r(sr);
                r.set(SubBass::pOctave, nt.oct);
                r.set(SubBass::pWidth, 100);
                r.set(SubBass::pDrive, 100);
                r.set(SubBass::pHarmonics, 100);
                r.set(SubBass::pMode, m);
                r.set(SubBass::pBlend, 100);
                r.engine = [&](double t) { return heldNote(t, 0.05, 9.0, nt.key); };
                const auto x = r.render(0.5 + 65536.0 / sr + 0.1);
                const double db = inharmonicDb(x, (int)(0.5 * sr), sr, hzOfKey(nt.key) * std::exp2(nt.oct));
                if (db > worst)
                {
                    worst = db;
                    where = std::string(modeShortName(m)) + " at " + std::to_string((int)sr) + " Hz";
                }
            }
        std::printf("%-34s everything that is not a harmonic of the note: at most %.1f dB under the sound (%s)\n", nt.what, worst, where.c_str());
        ok &= worst < nt.limitDb;
    }
    CHECK(ok, "heavy drive + every harmonic algorithm on high notes: what is not a harmonic of the note stays 60 dB down in the sub's range (46 dB at 523 / 1047 Hz)");
}

// ======================================================================================== (h) levels
static void testLevels()
{
    section("levels, headroom, DC, and the harmonics never thinning the sub");
    const double sr = 48000;
    float maxPk = 0, minPk = 9;
    double maxDc = 0, minGain = 99, maxGain = -99;
    bool finite = true;
    for (int key : {24, 28, 36, 43, 48, 55, 60})
    {
        const double f0 = hzOfKey(key) / 2;
        const int a = (int)(0.6 * sr), b = a + (int)(std::floor(0.9 * f0) / f0 * sr); // (whole cycles)
        Rig base(sr);
        base.set(SubBass::pBlend, 100);
        base.engine = [&](double t) { return heldNote(t, 0.05, 9.0, key); };
        const auto x0 = base.render(1.7);
        const double h0 = toneMag(x0, a, b, sr, f0);
        for (int m = 0; m < kModes; ++m)
            for (double width : {0.0, 50.0, 100.0})
                for (double drive : {0.0, 100.0})
                {
                    Rig ref(sr), r(sr);
                    for (auto *q : {&ref, &r})
                    {
                        q->set(SubBass::pWidth, width);
                        q->set(SubBass::pDrive, drive);
                        q->set(SubBass::pBlend, 100);
                        q->set(SubBass::pMode, m);
                        q->engine = base.engine;
                    }
                    r.set(SubBass::pHarmonics, 100);
                    const auto y0 = ref.render(1.7), y = r.render(1.7);
                    const float pk = peak(y, 0, (int)y.size());
                    finite &= std::isfinite(pk);
                    maxPk = std::max(maxPk, pk);
                    minPk = std::min(minPk, pk);
                    double dc = 0;
                    for (int i = a; i < b; ++i) dc += y[(size_t)i];
                    maxDc = std::max(maxDc, std::fabs(dc / (b - a)));
                    // the fundamental with the harmonics full, against the same sound without them
                    const double g = 20.0 * std::log10(toneMag(y, a, b, sr, f0) / std::max(1e-9, toneMag(y0, a, b, sr, f0)));
                    minGain = std::min(minGain, g);
                    maxGain = std::max(maxGain, g);
                }
        (void)h0;
    }
    std::printf("LEVEL 0 dB, DIRTY only, HARMONICS 100 %%, 7 keys x 5 algorithms x 3 widths x 2 drives: peak %.3f .. %.3f; DC at most %.5f; the fundamental moves by %+.2f .. %+.2f dB when the harmonics come in\n",
                minPk, maxPk, maxDc, minGain, maxGain);
    CHECK(finite && maxPk < 0.75f && minPk > 0.05f, "every algorithm at full strength stays finite and under 0.75 (-2.5 dBFS) at LEVEL 0 dB");
    CHECK(maxDc < 0.004, "no DC in the output");
    CHECK(minGain > -1.0, "the harmonic layer never thins the sub: its fundamental loses less than 1 dB with any algorithm at full strength");
    // PURE: what the clean sub is
    Rig p(sr);
    p.engine = [&](double t) { return heldNote(t, 0.05, 9.0, 36); };
    const auto x = p.render(1.7);
    const double f0 = hzOfKey(36) / 2;
    const int a = (int)(0.6 * sr), b = a + (int)(std::floor(0.9 * f0) / f0 * sr);
    const double h1 = toneMag(x, a, b, sr, f0), h2 = toneMag(x, a, b, sr, 2 * f0), h3 = toneMag(x, a, b, sr, 3 * f0), h4 = toneMag(x, a, b, sr, 4 * f0), h5 = toneMag(x, a, b, sr, 5 * f0);
    std::printf("PURE on C1 (32.7 Hz): fundamental %.3f (%.1f dBFS), 2nd %.1f dB, 3rd %.1f dB, 4th %.1f dB, 5th %.1f dB under it; peak %.3f\n", h1, 20 * std::log10(h1), 20 * std::log10(h2 / h1),
                20 * std::log10(h3 / h1), 20 * std::log10(h4 / h1), 20 * std::log10(h5 / h1), peak(x, 0, (int)x.size()));
    CHECK(h1 > 0.15 && h1 < 0.25 && h4 < 1e-4 * h1 && h5 < 1e-4 * h1, "the clean sub is a pure tone: fundamental near -14.5 dBFS, nothing above its 3rd harmonic (under -80 dB)");
    // mono: the same on both sides
    bool mono = true;
    for (size_t i = 0; i < x.size(); ++i) mono &= x[i] == p.right[i];
    CHECK(mono, "the sub is mono: exactly the same samples left and right");
    // LEVEL is dB
    Rig lo(sr), hi(sr);
    lo.set(SubBass::pLevel, -12);
    hi.set(SubBass::pLevel, 6);
    lo.engine = hi.engine = p.engine;
    const double rl = rms(lo.render(1.2), (int)(0.5 * sr), (int)(1.1 * sr)), rh = rms(hi.render(1.2), (int)(0.5 * sr), (int)(1.1 * sr)), r0 = rms(x, (int)(0.5 * sr), (int)(1.1 * sr));
    CHECK(std::fabs(20 * std::log10(rl / r0) + 12) < 0.05 && std::fabs(20 * std::log10(rh / r0) - 6) < 0.05, "LEVEL -12 dB and +6 dB are exactly that");
    // velocity and the engine's envelope: the sub is as loud as the engine says
    Rig v(sr);
    v.engine = [&](double t) { return heldNote(t, 0.05, 9.0, 36, 1, 0.5f); };
    const double rv = rms(v.render(1.2), (int)(0.5 * sr), (int)(1.1 * sr));
    CHECK(std::fabs(20 * std::log10(rv / r0) + 6.02) < 0.05, "it follows the engine's envelope and velocity (half the level = -6 dB)");
}

// ======================================================================================== (i) the eight presets
static void testPresets()
{
    section("the eight SUB presets");
    const double sr = 48000;
    bool ok = true;
    std::vector<std::vector<double>> prints;
    for (int p = 0; p < kPresets; ++p)
    {
        Rig r(sr);
        r.sb.setParam(SubBass::pOn, 0);
        r.preset(p);
        ok &= r.sb.on() && r.sb.currentPreset() == p; // (a preset turns SUB on)
        const int oct = (int)r.sb.getParam(SubBass::pOctave);
        r.engine = [&](double t) { return heldNote(t, 0.05, 9.0, 40); };
        const auto x = r.render(1.9);
        const double f0 = hzOfKey(40) * std::exp2(oct) * std::exp2(r.sb.getParam(SubBass::pTune) / 1200.0);
        const int a = (int)(0.7 * sr), b = a + (int)(std::floor(1.0 * f0) / f0 * sr);
        const double got = pitchNear(x, a, b, sr, f0);
        std::vector<double> h;
        for (int k = 1; k <= 10; ++k) h.push_back(toneMag(x, a, b, sr, k * f0));
        const float pk = peak(x, 0, (int)x.size());
        double hiSum = 0;
        for (int k = 1; k < 10; ++k) hiSum += h[(size_t)k] * h[(size_t)k];
        std::printf("%-11s octave %d, %s: %.2f Hz, peak %.3f, fundamental %.1f dBFS, harmonics 2-10 together %.1f dB under it |", presetName(p), oct, modeShortName((int)r.sb.getParam(SubBass::pMode)), got,
                    pk, 20 * std::log10(h[0]), -10 * std::log10(std::max(1e-12, hiSum) / (h[0] * h[0])));
        for (int k = 1; k < 8; ++k) std::printf(" %5.1f", 20 * std::log10(std::max(1e-9, h[(size_t)k]) / h[0]));
        std::printf("\n");
        ok &= std::fabs(centsOff(got, f0)) < 1.0 && pk > 0.15f && pk < 0.7f && h[0] > 0.1;
        prints.push_back(h);
    }
    // each is a sound of its own: its harmonics differ from every other preset's by at least 3 dB somewhere
    int alike = 0;
    for (int p = 0; p < kPresets; ++p)
        for (int q = p + 1; q < kPresets; ++q)
        {
            double far = 0;
            for (int k = 0; k < 10; ++k)
            {
                const double a = 20 * std::log10(std::max(1e-5, prints[(size_t)p][(size_t)k])), b = 20 * std::log10(std::max(1e-5, prints[(size_t)q][(size_t)k]));
                far = std::max(far, std::fabs(a - b));
            }
            Rig rp, rq;
            rp.preset(p);
            rq.preset(q);
            const bool octDiffers = rp.sb.getParam(SubBass::pOctave) != rq.sb.getParam(SubBass::pOctave), behaves = rp.sb.getParam(SubBass::pGlide) != rq.sb.getParam(SubBass::pGlide) || rp.sb.getParam(SubBass::pPhase) != rq.sb.getParam(SubBass::pPhase);
            if (far < 3.0 && !octDiffers)
            {
                ++alike;
                std::printf("  %s and %s are alike (%.1f dB apart%s)\n", presetName(p), presetName(q), far, behaves ? ", they differ in glide / phase" : "");
            }
        }
    CHECK(ok, "every preset turns SUB on, plays the right note at its octave, with a healthy level");
    CHECK(alike == 0, "the eight presets are eight different sounds (measured harmonics / octave)");
    SubBass s;
    s.uiPreset(3);
    s.sendToHost(nullptr);
    const float lv = (float)s.getParam(SubBass::pLevel);
    s.setParam(SubBass::pWidth, s.getParam(SubBass::pWidth) + 1);
    CHECK(s.currentPreset() == -1 && lv == 0.f, "a preset leaves LEVEL alone; changing any wheel afterwards un-lights it");
}

// ======================================================================================== (j) state
static void testState()
{
    section("state");
    SubBass a, b;
    busySub(a);
    a.setParam(SubBass::pOn, 1);
    const auto st = a.toState();
    bool ok = b.fromState(st);
    for (uint32_t i = 0; i < SubBass::kParams; ++i)
        ok &= a.getParam(SubBass::paramIdAt(i)) == b.getParam(SubBass::paramIdAt(i));
    CHECK(ok && b.on(), "all 12 settings survive toState + fromState");
    SubBass c;
    CHECK(!c.fromState("garbage") && c.fromState("P2SUB 1\non 1\nfuture 3\nwidth 33\nend\n") && c.on() && c.getParam(SubBass::pWidth) == 33 && c.getParam(SubBass::pOctave) == -1,
          "keys from a newer version are skipped; what is missing takes its default");
    c.fromState("P2SUB 1\nlevel 99\noctave -7\nmode 44\nwidth -3\nend\n");
    CHECK(c.getParam(SubBass::pLevel) == 6 && c.getParam(SubBass::pOctave) == -2 && c.getParam(SubBass::pMode) == kModes - 1 && c.getParam(SubBass::pWidth) == 0, "values out of range are brought into range");

    // through the plugin
    Plug p;
    p.preset(3);
    busySub(*p.sub);
    p.sub->setParam(SubBass::pOn, 1);
    const auto full = p.save();
    Plug q;
    const bool loaded = q.load(full);
    CHECK(loaded && p.sub->toState() == q.sub->toState() && q.sub->on(), "the plugin's state carries SUB: save + load gives the same settings");
    p.start();
    q.start();
    const auto acts = chord({36, 48}, 0.02, 0.9);
    const auto ya = p.render(1.3, acts), yb = q.render(1.3, acts);
    CHECK(same(ya, yb) && rmsOf(ya, 9600, 38400) > 0.01, "the loaded plugin renders bit-identically to the one that was saved");
    // a state from before SUB (patch + MORPH + SPECTRAL FX only): SUB is off, everything at its default
    size_t z = full.find('\0');
    z = full.find('\0', z + 1);
    z = full.find('\0', z + 1);
    const std::string old = full.substr(0, z + 1);
    Plug o;
    o.load(full); // (first make it non-default)
    const bool okOld = old.find("P2SUB") == std::string::npos && full.find("P2SUB 1") != std::string::npos && o.load(old);
    bool defaults = okOld && !o.sub->on();
    for (uint32_t i = 0; i < SubBass::kParams; ++i)
        defaults &= o.sub->getParam(SubBass::paramIdAt(i)) == SubBass::desc(SubBass::paramIdAt(i)).def;
    CHECK(defaults, "a state saved before SUB existed loads with SUB off and every SUB setting at its default");
    // ... and sounds exactly as it did: the old state in this build against the same patch with no SUB section ever seen
    Plug n;
    n.preset(3);
    o.start();
    n.start();
    CHECK(same(o.render(1.0, acts), n.render(1.0, acts)), "and it renders bit-identically to the same sound in a plugin that never had SUB");
    // a patch (preset) alone, as the first Poltergeist 2 saved it: only the text up to the first zero
    Plug m;
    m.load(full);
    CHECK(m.load(full.substr(0, full.find('\0') + 1)) && !m.sub->on(), "a state with nothing after the patch (the first Poltergeist 2 builds, upstream Six Sines) loads with SUB off");
    // host automation reaches it, sample-accurately enough: a parameter event changes the value
    Plug h;
    h.start();
    h.render(0.1, {{Act::Param, 0.05, 0, 1.0, SubBass::pOn}, {Act::Param, 0.05, 0, 77.0, SubBass::pDrive}, {Act::Param, 0.06, 0, -2.0, SubBass::pOctave}});
    CHECK(h.sub->on() && h.sub->getParam(SubBass::pDrive) == 77 && h.sub->getParam(SubBass::pOctave) == -2, "the host's automation events set SUB's parameters");
}

// ======================================================================================== (k) following the engine
static Mono subOnly(const Stereo &with, const Stereo &without)
{
    Mono d((size_t)with.n());
    for (int i = 0; i < with.n(); ++i) d[(size_t)i] = with.l[(size_t)i] - without.l[(size_t)i];
    return d;
}
static void testFollow()
{
    section("the sub inside the plugin: it follows the engine, and nothing in the plugin touches it");
    const double sr = 48000;
    // two plugins, the same sound, the same notes, the same seed: one with SUB (MAKE ROOM off, so the engine's part
    // is identical in both) - the difference is the sub alone
    auto pair = [&](const std::vector<Act> &acts, double seconds, std::function<void(Plug &)> both = nullptr, std::function<void(Plug &)> onlySub = nullptr) {
        Plug a, b;
        for (auto *p : {&a, &b})
        {
            if (both) both(*p);
            p->sub->setParam(SubBass::pRoom, 0);
        }
        b.sub->setParam(SubBass::pOn, 1);
        if (onlySub) onlySub(b);
        a.start();
        b.start();
        const auto ya = a.render(seconds, acts), yb = b.render(seconds, acts);
        struct R
        {
            Stereo off, on;
            Mono sub;
            uint32_t latency;
        };
        return R{ya, yb, subOnly(yb, ya), b.latency()};
    };
    {
        // (1) one note; (2) a chord: the lowest key; (3) the lowest released: the next lowest held
        std::vector<Act> acts = {{Act::On, 0.05, 55}, {Act::On, 0.05, 43}, {Act::On, 0.05, 62}, {Act::Off, 1.2, 43}, {Act::Off, 2.2, 55}, {Act::Off, 2.2, 62}};
        const auto r = pair(acts, 3.6);
        const double low = pitchNear(r.sub, (int)(0.5 * sr), (int)(1.15 * sr), sr, hzOfKey(43) / 2), next = pitchNear(r.sub, (int)(1.5 * sr), (int)(2.15 * sr), sr, hzOfKey(55) / 2);
        const double during = rms(r.sub, (int)(0.5 * sr), (int)(1.1 * sr)), end = rms(r.sub, (int)(3.3 * sr), (int)(3.6 * sr));
        std::printf("chord G1 + G2 + D3: the sub plays %.2f Hz (want %.2f); G1 released: %.2f Hz (want %.2f); sub rms while held %.4f, after everything is released %.2g\n", low, hzOfKey(43) / 2, next,
                    hzOfKey(55) / 2, during, end);
        CHECK(std::fabs(centsOff(low, hzOfKey(43) / 2)) < 2 && std::fabs(centsOff(next, hzOfKey(55) / 2)) < 2, "in a chord the sub takes the lowest key held (an octave below it); when that key is released, the next lowest");
        CHECK(during > 0.05 && end < 1e-5, "it sounds with the engine's note and is silent once the engine's note has died away");
        // the sub is the same on both sides and the difference of the sides is untouched: mono-compatible
        double sideDiff = 0;
        for (int i = 0; i < r.on.n(); ++i)
            sideDiff = std::max(sideDiff, (double)std::fabs((r.on.l[(size_t)i] - r.on.r[(size_t)i]) - (r.off.l[(size_t)i] - r.off.r[(size_t)i])));
        CHECK(sideDiff < 2e-6, "the sub adds the same to left and right: the stereo difference of the output is unchanged");
    }
    {
        // pitch bend through the engine (the patch's own bend range)
        Plug probe;
        const double range = ss::getSynthFromPlugin(probe.pl)->patchMain.output.bendUp.value;
        const auto r = pair({{Act::On, 0.05, 45}, {Act::Bend, 0.6, 0, 1.0}}, 1.8);
        const double want = hzOfKey(45) / 2 * std::exp2(range * (8191.0 / 8192.0) / 12.0);
        const double got = pitchNear(r.sub, (int)(1.1 * sr), (int)(1.75 * sr), sr, want, 200);
        std::printf("pitch wheel fully up (the patch bends %.0f semitones): the sub plays %.2f Hz (want %.2f)\n", range, got, want);
        CHECK(range > 0.5 && std::fabs(centsOff(got, want)) < 3, "the pitch wheel bends the sub with the engine, by the patch's own bend range");
    }
    {
        // velocity: a patch that listens to velocity makes the sub listen too
        auto loud = pair({{Act::On, 0.05, 40, 1.0}}, 0.9), soft = pair({{Act::On, 0.05, 40, 0.25}}, 0.9);
        const double rl = rms(loud.sub, (int)(0.4 * sr), (int)(0.85 * sr)), rs = rms(soft.sub, (int)(0.4 * sr), (int)(0.85 * sr));
        const double el = rmsOf(loud.off, (int)(0.4 * sr), (int)(0.85 * sr)), es = rmsOf(soft.off, (int)(0.4 * sr), (int)(0.85 * sr));
        std::printf("velocity 1.0 against 0.25: the engine is %.1f dB quieter, the sub %.1f dB\n", 20 * std::log10(es / el), 20 * std::log10(rs / rl));
        CHECK(std::fabs(20 * std::log10(rs / rl) - 20 * std::log10(es / el)) < 1.5, "the sub gets quieter with velocity as much as the engine's own sound does");
    }
    {
        // SPECTRAL FX on, with an effect that tears the spectrum up: the sub is not in it, and it is exactly as late
        // (three effects that leave nothing as it was: a frequency shifter, a spectral pixelator, a distortion)
        auto rack = [&](Plug &p) {
            using namespace ghostblock::p2::spectral;
            int slot = 0;
            for (const char *id : {"FrequencyShift", "SpectralPixelation", "Distortion"})
            {
                const int t = typeIndex(id);
                if (t < 0) continue;
                p.fx->setParam(SpectralFx::pSlotType(slot), t);
                const auto &d = types()[(size_t)t];
                for (size_t q = 0; q < d.params.size(); ++q)
                {
                    const auto &pd = d.params[q];
                    p.fx->uiSlotValue(slot, (int)q, pd.kind == Kind::Float ? pd.fromNorm(0.7f) : pd.kind == Kind::Bool ? 1.f : pd.def, 1);
                }
                ++slot;
            }
            p.fx->setParam(SpectralFx::pOn, 1);
        };
        const std::vector<Act> acts = {{Act::On, 0.05, 41}, {Act::Off, 1.0, 41}};
        const auto plain = pair(acts, 1.6), fx = pair(acts, 1.6, rack);
        const int L = (int)fx.latency;
        double worst = 0, ref = 0;
        for (int i = 0; i + L < (int)plain.sub.size(); ++i)
        {
            worst = std::max(worst, (double)std::fabs(fx.sub[(size_t)(i + L)] - plain.sub[(size_t)i]));
            ref = std::max(ref, (double)std::fabs(plain.sub[(size_t)i]));
        }
        const double changed = rmsDiff(plain.off, fx.off, L, 4800, plain.off.n() - L - 4800) / std::max(1e-9, rmsOf(plain.off, 4800, plain.off.n() - L - 4800));
        std::printf("SPECTRAL FX on (latency %d samples): the rack changes the engine's sound by %.0f %% rms; the sub differs from the sub without the rack, moved %d samples, by at most %.2g (its peak: %.3f)\n", L,
                    100 * changed, L, worst, ref);
        CHECK(L > 0 && changed > 0.05 && worst < 2e-6 && ref > 0.1, "SPECTRAL FX does not touch the sub, and the sub is delayed by exactly the latency the rack reports (it stays in time with the engine)");
    }
    {
        // MAKE ROOM: the engine's lows under the sub are cut; the sub is not
        const std::vector<Act> acts = {{Act::On, 0.05, 36}};
        Plug a, b, c;
        for (auto *p : {&b, &c})
        {
            p->sub->setParam(SubBass::pOn, 1);
            p->sub->setParam(SubBass::pOctave, 0);
        }
        b.sub->setParam(SubBass::pRoom, 0);
        c.sub->setParam(SubBass::pRoom, 100);
        for (auto *p : {&a, &b, &c}) p->start();
        const auto ya = a.render(1.6, acts), yb = b.render(1.6, acts), yc = c.render(1.6, acts);
        const Mono eng(ya.l.begin(), ya.l.end()), sub0 = subOnly(yb, ya);
        Mono cut((size_t)ya.n()); // the engine's part with MAKE ROOM = the whole minus the sub (which is the same in b and c)
        for (int i = 0; i < ya.n(); ++i) cut[(size_t)i] = yc.l[(size_t)i] - sub0[(size_t)i];
        const double f0 = hzOfKey(36);
        const int a0 = (int)(0.7 * sr), a1 = (int)(1.5 * sr);
        const double eFund = toneMag(eng, a0, a1, sr, f0), cFund = toneMag(cut, a0, a1, sr, f0), eHigh = toneMag(eng, a0, a1, sr, 8 * f0), cHigh = toneMag(cut, a0, a1, sr, 8 * f0);
        std::printf("MAKE ROOM 100 %%, sub on the engine's own note (65.4 Hz): the engine's fundamental goes from %.4f to %.4f (%.1f dB); its 8th harmonic from %.5f to %.5f (%.2f dB)\n", eFund, cFund,
                    20 * std::log10(cFund / eFund), eHigh, cHigh, 20 * std::log10(cHigh / std::max(1e-12, eHigh)));
        CHECK(eFund > 1e-3 && cFund < 0.2 * eFund && (eHigh < 1e-6 || std::fabs(20 * std::log10(cHigh / eHigh)) < 0.5), "MAKE ROOM takes the engine's fundamental out from under a sub on the same note (more than 14 dB) and leaves its upper harmonics alone");
    }
    {
        // MORPH on: the sub still follows THIS SOUND's engine
        Plug p;
        p.sub->setParam(SubBass::pOn, 1);
        p.start();
        p.render(0.05, {{Act::Param, 0.0, 0, 1.0, ghostblock::p2::Morph::pOn}});
        const auto y = p.render(1.2, {{Act::On, 0.05, 38}});
        const Mono l(y.l.begin(), y.l.end());
        const double want = hzOfKey(38) / 2, m = toneMag(l, (int)(0.5 * sr), (int)(1.1 * sr), sr, want);
        CHECK(m > 0.1, "with MORPH on the sub still plays under the note");
    }
}

// ======================================================================================== (l) SUB off = untouched
static void testBypass()
{
    section("SUB off = untouched (every factory preset, through the CLAP plugin)");
    int n = 0;
    {
        Plug probe;
        while (probe.preset(n)) ++n;
    }
    int identical = 0, differs = 0, unstable = 0;
    const auto acts = chord({36, 48, 55}, 0.02, 0.5);
    for (int i = -1; i < n; ++i)
    {
        auto one = [&](bool loaded) {
            Plug p;
            if (i >= 0) p.preset(i);
            if (loaded) busySub(*p.sub); // every SUB setting away from its default, but SUB ON is off
            p.start();
            return p.render(0.8, acts);
        };
        const auto a = one(false), b = one(true);
        if (same(a, b)) ++identical;
        else if (!same(a, one(false))) ++unstable; // (this preset does not repeat itself even untouched)
        else { ++differs; std::printf("FAIL: preset %d differs with SUB set up but off\n", i); }
    }
    std::printf("factory presets + init: %d rendered twice; identical %d, differing %d, not repeatable by themselves %d\n", n + 1, identical, differs, unstable);
    CHECK(n > 50 && differs == 0 && identical >= n - 5, "SUB off: bit-identical output for every factory preset");
    // on for a while, then off: after the fade the output is bit-identical again
    const int asks0 = restartAsks.load();
    Plug p, q;
    p.start();
    q.start();
    const std::vector<Act> hold = {{Act::On, 0.02, 36}, {Act::On, 0.02, 48}};
    p.render(0.5, hold);
    q.render(0.5, hold);
    const auto on1 = p.render(0.5, {{Act::Param, 0.0, 0, 1.0, SubBass::pOn}}), ref1 = q.render(0.5, {});
    p.render(0.3, {{Act::Param, 0.0, 0, 0.0, SubBass::pOn}});
    q.render(0.3, {});
    const auto off2 = p.render(0.6, {}), ref2 = q.render(0.6, {});
    CHECK(!same(on1, ref1) && same(off2, ref2), "SUB switched on and off again in a running plugin: the output returns to bit-identical");
    CHECK(p.latency() == 0 && restartAsks.load() == asks0, "SUB adds no latency and never asks the host to restart");
}

// ======================================================================================== (m) allocations
static void testAllocations()
{
    section("audio thread allocations");
    int total = 0;
    for (double sr : {44100.0, 48000.0, 96000.0})
    {
        Rig r(sr);
        r.engine = [&](double t) {
            const double u = std::fmod(t, 0.5);
            return heldNote(u, 0.02, 0.3, 30 + ((int)(t / 0.5) * 7) % 30, 1 + (uint32_t)(t / 0.5));
        };
        r.bus = [](double t) { return (float)(0.2 * std::sin(2 * M_PI * 82.4 * t)); };
        r.render(0.2); // (running)
        int k = 0;
        r.perBlock = [&](double, SubBass &s) {
            // everything a host can automate, changing while it runs
            ++k;
            s.setParam(SubBass::pLevel, -24 + k % 30);
            s.setParam(SubBass::pOctave, -(k / 9 % 3));
            s.setParam(SubBass::pWidth, (k * 7) % 101);
            s.setParam(SubBass::pDrive, (k * 13) % 101);
            s.setParam(SubBass::pHarmonics, (k * 3) % 101);
            s.setParam(SubBass::pMode, (k / 5) % kModes);
            s.setParam(SubBass::pBlend, (k * 11) % 101);
            s.setParam(SubBass::pGlide, (k * 17) % 1000);
            s.setParam(SubBass::pTune, -100 + (k * 5) % 201);
            s.setParam(SubBass::pPhase, (k / 7) % 2);
            s.setParam(SubBass::pRoom, (k * 19) % 101);
            s.setParam(SubBass::pOn, k % 41 < 33);
            if (k % 57 == 0) s.requestReset();
            if (k % 23 == 0) s.uiPreset(k % kPresets);
        };
        // (the rig's own buffers are made before the guard goes up)
        const int blocks = (int)(4.0 * sr / r.bs);
        std::vector<float> L(r.bs), R(r.bs);
        guardHits = 0;
        guard = 1;
        for (int b = 0; b < blocks; ++b)
        {
            const double t0 = (r.pos + b * (int)r.bs) / sr;
            r.perBlock(t0, r.sb);
            for (uint32_t i = 0; i < r.bs; ++i) L[i] = R[i] = r.bus(t0 + i / sr);
            for (uint32_t f = 0; f < r.bs; f += 8)
                if (r.sb.wantsTrack()) r.sb.track(f, r.engine(t0 + f / sr));
            r.sb.process(L.data(), R.data(), r.bs, (uint32_t)((b / 40) % 3) * 1023);
            r.sb.sendToHost(nullptr);
        }
        guard = 0;
        total += guardHits.load();
    }
    CHECK(total == 0, "the sub allocates and frees nothing while it runs, whatever is being changed (4 s each at 44.1, 48, 96 kHz)");
    // the whole plugin: no more with SUB on than without
    auto count = [&](bool on) {
        Plug p;
        p.preset(2);
        if (on)
        {
            busySub(*p.sub);
            p.sub->setParam(SubBass::pOn, 1);
        }
        p.start();
        p.render(0.3, chord({36, 43}, 0.02, 0.2)); // (running; the engine's first notes may set things up)
        guardHits = 0;
        p.guarded = true;
        std::vector<Act> acts;
        for (int i = 0; i < 12; ++i)
        {
            acts.push_back({Act::On, 0.05 + i * 0.15, 30 + (i * 5) % 24});
            acts.push_back({Act::Off, 0.15 + i * 0.15, 30 + (i * 5) % 24});
            acts.push_back({Act::Param, 0.07 + i * 0.15, 0, (double)((i * 37) % 100), SubBass::pDrive});
            acts.push_back({Act::Param, 0.09 + i * 0.15, 0, (double)(i % kModes), SubBass::pMode});
        }
        p.render(2.0, acts);
        p.guarded = false;
        return guardHits.load();
    };
    const int off = count(false), on = count(true);
    std::printf("the plugin's process(), 2 s of notes: %d allocations with SUB off, %d with SUB on\n", off, on);
    CHECK(on <= off, "the plugin's audio thread allocates no more with SUB on than with it off");
}

// ======================================================================================== (n) CPU
static void testCpu()
{
    section("CPU");
    double worst = 0;
    for (double sr : {44100.0, 48000.0, 96000.0})
    {
        std::printf("%6.0f Hz:", sr);
        for (int m = -1; m < kModes; ++m)
        {
            Rig r(sr);
            r.bs = 128;
            if (m >= 0)
            {
                r.set(SubBass::pWidth, 30);
                r.set(SubBass::pDrive, 70);
                r.set(SubBass::pHarmonics, 80);
                r.set(SubBass::pMode, m);
                r.set(SubBass::pBlend, 80);
            }
            r.engine = [&](double t) { return heldNote(std::fmod(t, 1.0), 0.02, 0.8, 36 + ((int)t * 5) % 12, 1 + (uint32_t)t); };
            r.render(0.5);
            const auto t0 = std::chrono::steady_clock::now();
            r.render(8.0);
            const double pct = 100.0 * std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / 8.0;
            worst = std::max(worst, pct);
            std::printf("  %s %.2f %%", m < 0 ? "PURE" : modeShortName(m), pct);
        }
        std::printf("\n");
    }
    std::printf("(the sub alone, one core, of real time; the heaviest: %.2f %%)\n", worst);
    CHECK(worst < 20.0, "the sub costs well under a fifth of one core in every mode (the figures are printed above)");
}

int main(int argc, char **argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const std::string mode = argc > 1 ? argv[1] : "checks";
    auto want = [&](const char *m) { return mode == "all" || mode == "checks" || mode == m; };
    if (want("port")) testPort();
    if (want("bombaz")) testBombaz();
    if (want("params")) testParams();
    if (want("tracking")) testTracking();
    if (want("glide")) testGlide();
    if (want("clicks")) testClicks();
    if (want("aliasing")) testAliasing();
    if (want("levels")) testLevels();
    if (want("presets")) testPresets();
    if (want("state")) testState();
    if (want("follow")) testFollow();
    if (want("bypass")) testBypass();
    if (want("alloc")) testAllocations();
    if (want("cpu")) testCpu();
    return finish();
}
