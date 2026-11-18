/*
 * Poltergeist 2 (GhostBlock) - SPECTRAL FX: settings, host parameters, state, and the audio-thread shell
 * (bypass crossfade, latency, swapping ready-made effect objects in). The upstream DSP is run by sc-engine.cpp
 * (Spectral Corruptor) and the warpcore-isa-*.cpp files (WarpCore). See spectral-fx.h and README.md.
 */
#include "ghostblock/spectral/spectral-fx.h"
#include "ghostblock/spectral/sc-engine.h"
#include "ghostblock/spectral/warpcore-isa.h"
#include "sst/cpputils/ring_buffer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <locale>
#include <sstream>
#include <strings.h>

namespace ghostblock::p2::spectral
{
using namespace detail;

// ===================================================================================================== descriptors
float ParamDesc::clamp(float v) const
{
    if (!std::isfinite(v)) return def;
    v = std::min(max, std::max(min, v));
    return stepped() ? std::round(v) : v;
}
float ParamDesc::toNorm(float v) const
{
    v = clamp(v);
    if (max <= min) return 0.f;
    if (logScale && !stepped()) return std::log(v / min) / std::log(max / min);
    return (v - min) / (max - min);
}
float ParamDesc::fromNorm(float n) const
{
    n = std::min(1.f, std::max(0.f, std::isfinite(n) ? n : 0.f));
    if (logScale && !stepped()) return clamp(min * std::pow(max / min, n));
    return clamp(min + n * (max - min));
}
std::string ParamDesc::text(float v) const
{
    v = clamp(v);
    char b[96];
    switch (kind)
    {
    case Kind::Bool: return v > 0.5f ? "On" : "Off";
    case Kind::Choice:
    {
        const int i = (int)std::lround(v);
        return i >= 0 && i < (int)choices.size() ? choices[(size_t)i] : std::to_string(i);
    }
    case Kind::Int: std::snprintf(b, sizeof(b), "%d%s", (int)std::lround(v), unit.c_str()); return b;
    default: break;
    }
    const int dec = step >= 1.f ? 0 : step >= 0.1f ? 1 : step >= 0.01f ? 2 : 3;
    std::snprintf(b, sizeof(b), "%.*f%s", dec, (double)v, unit.c_str());
    return b;
}
bool ParamDesc::parse(const std::string &t, float &v) const
{
    if (kind == Kind::Choice)
        for (size_t i = 0; i < choices.size(); ++i)
            if (strcasecmp(choices[i].c_str(), t.c_str()) == 0)
            {
                v = (float)i;
                return true;
            }
    if (kind == Kind::Bool && (strcasecmp(t.c_str(), "on") == 0 || strcasecmp(t.c_str(), "off") == 0))
    {
        v = strcasecmp(t.c_str(), "on") == 0 ? 1.f : 0.f;
        return true;
    }
    char *end = nullptr;
    const double d = std::strtod(t.c_str(), &end);
    if (end == t.c_str()) return false;
    v = clamp((float)d);
    return true;
}

namespace
{
struct Catalogue
{
    std::vector<TypeDesc> types;
    std::vector<std::string> categories;
    std::vector<ParamDesc> warp;
    Catalogue()
    {
        buildCatalogue(types, categories);
        // WarpCore's parameters, as its PluginProcessor.cpp declares them (ids, ranges, steps, defaults)
        auto add = [&](const char *id, const char *name, Kind k, float mn, float mx, float df, float st, const char *unit, std::vector<std::string> ch = {}) {
            ParamDesc d;
            d.id = id; d.name = name; d.kind = k; d.min = mn; d.max = mx; d.def = df; d.step = st; d.unit = unit; d.choices = std::move(ch);
            d.hostParam = true;
            warp.push_back(d);
        };
        add("warp", "Warp", Kind::Int, 1, 256, 50, 1, "");
        add("f_high", "Freq High", Kind::Float, 4000, 20010, 20010, 0.4f, " Hz");
        add("scale", "Scale", Kind::Float, 0.1f, 3.0f, 1.0f, 0.01f, "");
        add("poles", "Poles", Kind::Int, 1, 8, 2, 1, "");
        add("pitch", "Pitch", Kind::Float, -24, 24, 0, 0.01f, " st");
        add("pitch_affect", "Pitch Affect", Kind::Bool, 0, 1, 1, 1, "");
        add("fill_gap", "Fill Gap", Kind::Bool, 0, 1, 0, 1, "");
        add("freq_mode", "Freq Mode", Kind::Choice, 0, 3, 2, 1, "", {"voice: 0 + n", "voice: 1 + n", "music: 0 + 2n", "music: 1 + 2n"});
        add("drywet", "Dry/Wet", Kind::Float, 0, 1, 1, 0.01f, "");
    }
};
const Catalogue &catalogue()
{
    static const Catalogue c;
    return c;
}
const char *const kWindowNames[kWindows] = {"Hann", "Hamming", "Blackman", "Rectangular", "Blackman-Harris", "Gaussian", "Flat Top", "Welch", "Triangle"};
const float kLimitHz[kLimits] = {11025, 16000, 22050, 24000, 44100, 48000, 96000};
} // namespace

const std::vector<TypeDesc> &types() { return catalogue().types; }
const std::vector<std::string> &categories() { return catalogue().categories; }
const std::vector<ParamDesc> &warpParams() { return catalogue().warp; }
int typeIndex(const std::string &id)
{
    const auto &t = types();
    for (size_t i = 1; i < t.size(); ++i)
        if (t[i].id == id) return (int)i;
    return -1;
}
const char *windowName(int i) { return kWindowNames[std::clamp(i, 0, kWindows - 1)]; }
float limitHz(int i) { return kLimitHz[std::clamp(i, 0, kLimits - 1)]; }
bool warpAvailable() { return warpIsa() != nullptr; }
const char *warpSimdName() { return warpIsa() ? warpIsa()->name : "none"; }

// ===================================================================================================== Impl
namespace
{
constexpr int kRing = 32768; // > the largest latency (16383) + a step
struct HostMsg
{
    uint32_t id{0};
    int type{1};
    float value{0};
};
struct Update // what the main thread hands the audio thread; it comes back holding the replaced objects
{
    Core *core{nullptr};
    bool setCore{false};
    Proc *proc[kSlots]{};
    bool setProc[kSlots]{};
    bool any() const
    {
        bool a = setCore;
        for (bool b : setProc) a |= b;
        return a;
    }
};
void freeUpdate(Update *u)
{
    if (!u) return;
    if (u->setCore) freeCore(u->core);
    for (int s = 0; s < kSlots; ++s)
        if (u->setProc[s]) freeProc(u->proc[s]);
    delete u;
}
} // namespace

struct SpectralFx::Impl
{
    // ---------------- settings (atomics: the host on the audio thread and the editor on the main thread both write)
    std::atomic<float> mix{100.f}, chainMix{100.f}, overlap{4.f};
    std::atomic<int> fft{kDefaultFft}, window{0}, limit{kDefaultLimit}, order{0};
    struct Slot
    {
        std::atomic<int> type{0};
        std::atomic<bool> on{true};
        std::atomic<float> mix{100.f};
        std::array<std::atomic<float>, kMaxParams> v{};
        std::atomic<uint32_t> gen{1};
    } slot[kSlots];
    std::atomic<bool> warpOn{false};
    std::array<std::atomic<float>, kWarpParams> warpV{};
    std::atomic<uint32_t> warpGen{1};
    std::atomic<uint32_t> changes{0};
    std::atomic<bool> restartFlag{false}, mainFlag{false}, resetFlag{false}, rescanFlag{false};
    std::atomic<uint32_t> reported{0}; // latency the host knows
    std::atomic<float> inPeak{0}, outPeak{0};
    std::array<std::atomic<float>, kSlots> slotPeak{};
    sst::cpputils::SimpleRingBuffer<HostMsg, 1024> toHost;

    // ---------------- main thread
    double sr{0};
    CoreCfg builtCfg;
    bool built{false};
    int builtType[kSlots]{};
    uint32_t builtGen[kSlots]{};
    uint32_t serial{0};
    Core *mainCore{nullptr}; // the newest core made (for the spectrum tap); owned by the audio side / the mailbox
    std::atomic<Update *> mailbox{nullptr};
    std::array<std::atomic<Update *>, 8> retired{}; // audio -> main (each slot written once, cleared by main)

    // ---------------- audio thread
    Core *core{nullptr};
    Proc *proc[kSlots]{};
    Update *pending{nullptr}, *unretired{nullptr};
    bool running{false};
    float xf{0.f};
    int warm{0};
    uint32_t lrep{0}; // = reported (audio copy)
    int pad{0};
    int curOrder{0};
    float gain[kSlots]{}, mixCur[kSlots]{};
    bool hold[kSlots]{};
    float cur[kSlots][kMaxParams]{}, tgt[kSlots][kMaxParams]{};
    uint32_t seen[kSlots]{};
    bool ramping[kSlots]{};
    float masterMix{1.f};
    const WarpIsa *isa{nullptr};
    void *warp{nullptr};
    float warpGain{0.f};
    uint32_t warpSeen{0};
    uint32_t askedFor{0xffffffffu};
    std::vector<float> inRing[2], fxRing[2];
    uint32_t wpos{0};
    RackIo io;
    float buf[2][kQuantum]{}, keep[2][kQuantum]{};

    Impl()
    {
        for (int k = 0; k < kWarpParams; ++k) warpV[(size_t)k] = warpParams()[(size_t)k].def;
        for (auto &r : inRing) r.assign(kRing, 0.f);
        for (auto &r : fxRing) r.assign(kRing, 0.f);
    }
    ~Impl()
    {
        freeUpdate(mailbox.exchange(nullptr));
        drainRetired();
        freeUpdate(pending);
        freeUpdate(unretired);
        freeCore(core);
        for (auto *p : proc) freeProc(p);
        if (warp && isa) isa->destroy(warp);
    }

    const TypeDesc &desc(int s) const
    {
        const auto &t = types();
        return t[(size_t)std::clamp(slot[s].type.load(std::memory_order_relaxed), 0, (int)t.size() - 1)];
    }
    bool wantsStft() const
    {
        for (int s = 0; s < kSlots; ++s)
        {
            const auto d = desc(s).domain;
            if (d == Domain::Spectral || d == Domain::Partial) return true;
        }
        return false;
    }
    void touch() { changes.fetch_add(1, std::memory_order_relaxed); }
    void setType(int s, int t)
    {
        t = std::clamp(t, 0, (int)types().size() - 1);
        if (slot[s].type.load() == t) return;
        const auto &d = types()[(size_t)t];
        for (size_t i = 0; i < kMaxParams; ++i) slot[s].v[i] = i < d.params.size() ? d.params[i].def : 0.f;
        slot[s].type = t;
        slot[s].gen++;
        rescanFlag = true;
        mainFlag = true;
    }
    void drainRetired()
    {
        for (auto &r : retired) freeUpdate(r.exchange(nullptr, std::memory_order_acq_rel));
    }
    CoreCfg wantedCfg() const
    {
        CoreCfg c;
        c.sampleRate = sr;
        c.stft = wantsStft();
        c.fftSize = fftSizeAt(fft.load());
        c.window = std::clamp(window.load(), 0, kWindows - 1);
        c.overlap = std::clamp(overlap.load(), 2.f, 16.f);
        return c;
    }
    static bool same(const CoreCfg &a, const CoreCfg &b)
    {
        if (a.sampleRate != b.sampleRate || a.stft != b.stft) return false;
        return !a.stft || (a.fftSize == b.fftSize && a.window == b.window && a.overlap == b.overlap);
    }
    void readValues(int s, float *out) const
    {
        for (int i = 0; i < kMaxParams; ++i) out[i] = slot[s].v[(size_t)i].load(std::memory_order_relaxed);
    }
    // main thread: make what the settings ask for and is not there yet. `direct`: audio is stopped, install here.
    void build(bool direct)
    {
        if (sr <= 0) return;
        const auto cfg = wantedCfg();
        const bool coreChange = !built || !same(cfg, builtCfg);
        Update *u = direct ? new Update() : mailbox.exchange(nullptr, std::memory_order_acq_rel);
        if (!u) u = new Update();
        if (coreChange)
        {
            auto *c = makeCore(cfg, chainMix.load() * 0.01f, ++serial);
            if (u->setCore) freeCore(u->core); // (never reached the audio thread)
            u->core = c;
            u->setCore = true;
            mainCore = c;
            builtCfg = cfg;
        }
        for (int s = 0; s < kSlots; ++s)
        {
            const int t = slot[s].type.load();
            const auto &d = types()[(size_t)t];
            const uint32_t g = slot[s].gen.load();
            const bool spectral = d.domain == Domain::Spectral || d.domain == Domain::Partial;
            if (!((coreChange && (spectral || !built)) || t != builtType[s] || (!d.liveParams && g != builtGen[s]))) continue;
            float v[kMaxParams];
            readValues(s, v);
            auto *p = makeProc(t, mainCore, v);
            if (u->setProc[s]) freeProc(u->proc[s]);
            u->proc[s] = p;
            u->setProc[s] = true;
            builtType[s] = t;
            builtGen[s] = g;
        }
        built = true;
        if (direct)
        {
            if (u->setCore) std::swap(core, u->core);
            for (int s = 0; s < kSlots; ++s)
                if (u->setProc[s]) std::swap(proc[s], u->proc[s]);
            freeUpdate(u);
        }
        else if (u->any()) mailbox.store(u, std::memory_order_release);
        else delete u;
    }
    uint32_t latencyWanted(bool on) const { return on && wantsStft() ? (uint32_t)(fftSizeAt(fft.load()) - 1) : 0u; }

    // ---------------- audio thread
    void loadTargets(int s)
    {
        readValues(s, tgt[s]);
        seen[s] = slot[s].gen.load(std::memory_order_relaxed);
    }
    void snap(int s)
    {
        loadTargets(s);
        std::copy(tgt[s], tgt[s] + kMaxParams, cur[s]);
        ramping[s] = false;
        if (proc[s] && procType(proc[s]) == slot[s].type.load(std::memory_order_relaxed) && desc(s).liveParams) procApply(proc[s], cur[s]);
        mixCur[s] = slot[s].mix.load(std::memory_order_relaxed) * 0.01f;
    }
    void warpUpdate()
    {
        if (!isa || !warp) return;
        WarpParams w;
        const auto &d = warpParams();
        auto v = [&](int k) { return d[(size_t)k].clamp(warpV[(size_t)k].load(std::memory_order_relaxed)); };
        w.bands = (int)v(0); w.fHigh = v(1); w.scale = v(2); w.poles = (int)v(3); w.pitch = v(4);
        w.pitchAffect = v(5) > 0.5f; w.fillGap = v(6) > 0.5f; w.freqMode = (int)v(7); w.dryWet = v(8);
        isa->update(warp, w);
        warpSeen = warpGen.load(std::memory_order_relaxed);
    }
    void resetAll()
    {
        coreReset(core);
        for (int s = 0; s < kSlots; ++s)
        {
            procReset(proc[s]);
            snap(s);
            hold[s] = false;
            gain[s] = proc[s] && slot[s].on.load(std::memory_order_relaxed) && procType(proc[s]) == slot[s].type.load(std::memory_order_relaxed) ? 1.f : 0.f;
        }
        if (isa && warp)
        {
            isa->reset(warp);
            warpUpdate();
        }
        warpGain = warpOn.load(std::memory_order_relaxed) ? 1.f : 0.f;
        for (auto &r : fxRing) std::fill(r.begin(), r.end(), 0.f);
        masterMix = mix.load(std::memory_order_relaxed) * 0.01f;
        curOrder = order.load(std::memory_order_relaxed);
        setLatencies();
    }
    // pad: when the host was told more latency than the rack has now, the rack waits the difference.
    // warm: how long the rack runs before it is let out (an STFT needs two windows to overlap fully; the pad must fill)
    void setLatencies()
    {
        pad = std::max(0, (int)lrep - coreLatency(core));
        warm = 2 * (coreLatency(core) + (coreLatency(core) > 0 ? 1 : 0)) + pad;
    }
    void retire(Update *u)
    {
        for (auto &r : retired)
        {
            Update *expected = nullptr;
            if (r.compare_exchange_strong(expected, u, std::memory_order_acq_rel)) { mainFlag.store(true, std::memory_order_relaxed); return; }
        }
        unretired = u; // (main thread is far behind; try again next block)
    }
    void applyPending()
    {
        auto *u = pending;
        pending = nullptr;
        if (u->setCore)
        {
            std::swap(core, u->core);
            for (auto &r : fxRing) std::fill(r.begin(), r.end(), 0.f);
        }
        for (int s = 0; s < kSlots; ++s)
        {
            if (u->setProc[s]) std::swap(proc[s], u->proc[s]);
            else if (u->setCore && proc[s] && procSerial(proc[s]) != 0 && procSerial(proc[s]) != coreSerial(core))
            { // (a spectral effect made for the old STFT, with no replacement in this update: out it goes)
                u->proc[s] = proc[s];
                u->setProc[s] = true;
                proc[s] = nullptr;
            }
            else continue;
            hold[s] = false;
            gain[s] = 0.f;
            snap(s);
        }
        if (u->setCore) setLatencies();
        retire(u);
    }

    void quantum(float *L, float *R, int n, double tempo, bool on)
    {
        float *ch[2] = {L, R};
        const float srf = (float)sr;
        // ---- a waiting update goes in once the parts it replaces have faded out
        bool holdOut = curOrder != order.load(std::memory_order_relaxed);
        if (pending)
        {
            bool ready = true;
            if (running)
            {
                if (pending->setCore)
                {
                    holdOut = true;
                    if (xf > 0.f) ready = false;
                }
                for (int s = 0; s < kSlots; ++s)
                    if (pending->setProc[s] && proc[s] && procSerial(proc[s]) == 0)
                    { // a time effect is leaving: fade it out first (a spectral one changes between overlapping frames)
                        hold[s] = true;
                        if (gain[s] > 0.f) ready = false;
                    }
            }
            if (ready) applyPending();
        }
        if (on && !running)
        {
            resetAll();
            running = true;
        }
        if (running && xf <= 0.f && curOrder != order.load(std::memory_order_relaxed))
        { // the sections trade places while nothing of them is heard; the jump this makes inside them is let pass first
            curOrder = order.load(std::memory_order_relaxed);
            setLatencies();
        }
        // ---- the input, remembered for the dry paths
        for (int k = 0; k < 2; ++k)
            for (int i = 0; i < n; ++i) inRing[k][(wpos + (uint32_t)i) & (kRing - 1)] = ch[k][i];
        const int lint = coreLatency(core);
        if (running)
        {
            for (int k = 0; k < 2; ++k) std::copy(ch[k], ch[k] + n, buf[k]);
            // ---- each slot: its values (glided), its share
            const float gstep = (float)n / (0.03f * srf);
            io.core = core;
            for (int s = 0; s < kSlots; ++s)
            {
                io.proc[s] = proc[s];
                io.gain0[s] = io.gain1[s] = 0.f;
                if (!proc[s]) continue;
                const bool match = procType(proc[s]) == slot[s].type.load(std::memory_order_relaxed);
                const auto &d = types()[(size_t)procType(proc[s])];
                if (match && slot[s].gen.load(std::memory_order_relaxed) != seen[s])
                {
                    loadTargets(s);
                    ramping[s] = true;
                }
                if (ramping[s])
                {
                    bool moving = false;
                    const float f = (float)n / (0.3f * srf); // full range in 300 ms: a time effect gets its values in 64-sample steps, keep them small
                    for (size_t i = 0; i < d.params.size(); ++i)
                    {
                        float &c = cur[s][i];
                        const float t = tgt[s][i];
                        if (c == t) continue;
                        if (d.params[i].kind != Kind::Float) c = t;
                        else
                        {
                            const float step = (d.params[i].max - d.params[i].min) * f;
                            c = std::abs(t - c) <= step ? t : c + (t > c ? step : -step);
                            moving |= c != t;
                        }
                    }
                    ramping[s] = moving;
                    if (d.liveParams) procApply(proc[s], cur[s]);
                }
                const float target = match && !hold[s] && slot[s].on.load(std::memory_order_relaxed) ? 1.f : 0.f;
                const float g0 = gain[s];
                gain[s] += std::clamp(target - gain[s], -gstep, gstep);
                if (g0 <= 0.f && gain[s] > 0.f) procReset(proc[s]); // back from silence: no stale tails
                const float m0 = mixCur[s];
                mixCur[s] += std::clamp(slot[s].mix.load(std::memory_order_relaxed) * 0.01f - mixCur[s], -gstep, gstep);
                io.gain0[s] = g0 * m0;
                io.gain1[s] = gain[s] * mixCur[s];
            }
            io.limitHz = limitHz(limit.load(std::memory_order_relaxed));
            io.tempo = tempo;
            io.chainMix01 = std::clamp(chainMix.load(std::memory_order_relaxed) * 0.01f, 0.f, 1.f);
            // ---- WarpCore's share
            const bool wOn = isa && warp && warpOn.load(std::memory_order_relaxed);
            const float w0 = warpGain;
            warpGain += std::clamp((wOn ? 1.f : 0.f) - warpGain, -gstep, gstep);
            const bool wRun = isa && warp && (w0 > 0.f || warpGain > 0.f);
            auto runWarp = [&] {
                if (!wRun) return;
                if (w0 <= 0.f) isa->reset(warp);
                if (warpSeen != warpGen.load(std::memory_order_relaxed) || w0 <= 0.f) warpUpdate();
                const bool whole = w0 >= 1.f && warpGain >= 1.f;
                if (!whole)
                    for (int k = 0; k < 2; ++k) std::copy(buf[k], buf[k] + n, keep[k]);
                isa->process(warp, buf[0], buf[1], n);
                for (int k = 0; k < 2; ++k)
                    for (int i = 0; i < n; ++i)
                    {
                        float x = buf[k][i];
                        if (!std::isfinite(x)) x = 0.f;
                        if (!whole) x = keep[k][i] + (w0 + (warpGain - w0) * (float)(i + 1) / (float)n) * (x - keep[k][i]);
                        buf[k][i] = std::clamp(x, -8.f, 8.f);
                    }
            };
            if (curOrder == 1) runWarp();
            rackProcess(io, buf[0], buf[1], n);
            if (curOrder == 0) runWarp();
            for (int s = 0; s < kSlots; ++s) slotPeak[(size_t)s].store(io.level[s], std::memory_order_relaxed);
            // ---- master wet/dry (the dry side waits as long as the STFT takes), then the latency the host knows
            const float mt = std::clamp(mix.load(std::memory_order_relaxed) * 0.01f, 0.f, 1.f);
            const float mstep = 1.f / (0.02f * srf);
            for (int i = 0; i < n; ++i)
            {
                masterMix += std::clamp(mt - masterMix, -mstep, mstep);
                const uint32_t w = wpos + (uint32_t)i;
                for (int k = 0; k < 2; ++k)
                {
                    float x = buf[k][i];
                    if (masterMix < 1.f)
                    {
                        const float d = inRing[k][(w - (uint32_t)lint) & (kRing - 1)];
                        x = d + masterMix * (x - d);
                    }
                    if (pad > 0)
                    {
                        fxRing[k][w & (kRing - 1)] = x;
                        x = fxRing[k][(w - (uint32_t)pad) & (kRing - 1)];
                    }
                    buf[k][i] = x;
                }
            }
            if (warm > 0) warm = std::max(0, warm - n);
        }
        // ---- out: the rack, or the plain signal (as late as the host was told), crossfaded
        const float target = running && on && !holdOut && warm == 0 ? 1.f : 0.f;
        const float xstep = 1.f / (0.02f * srf);
        for (int i = 0; i < n; ++i)
        {
            xf += std::clamp(target - xf, -xstep, xstep);
            const uint32_t w = wpos + (uint32_t)i;
            for (int k = 0; k < 2; ++k)
            {
                const float b = lrep == 0 ? ch[k][i] : inRing[k][(w - lrep) & (kRing - 1)];
                ch[k][i] = xf >= 1.f ? buf[k][i] : (xf <= 0.f ? b : b + xf * (buf[k][i] - b));
            }
        }
        wpos += (uint32_t)n;
        if (running && !on && xf <= 0.f && !pending) running = false;
    }
};

// ===================================================================================================== SpectralFx
SpectralFx::SpectralFx() : impl(std::make_unique<Impl>()) { types(); }
SpectralFx::~SpectralFx() = default;

bool SpectralFx::isParam(uint32_t id)
{
    if (id >= pOn && id < pOn + kGlobalParams) return true;
    if (id >= pWarpOn && id < pWarpOn + kWarpHostParams) return true;
    for (int s = 0; s < kSlots; ++s)
    {
        const uint32_t b = slotBase(s);
        if (id >= b && id <= b + 2) return true;
        if (id >= b + 10 && id < b + 10 + kHostParamsPerSlot) return true;
    }
    return false;
}
uint32_t SpectralFx::paramIdAt(uint32_t i)
{
    if (i < kGlobalParams) return pOn + i;
    i -= kGlobalParams;
    if (i < kSlots * kPerSlot)
    {
        const int s = (int)(i / kPerSlot);
        const uint32_t k = i % kPerSlot;
        return k < 3 ? slotBase(s) + k : slotBase(s) + 10 + (k - 3);
    }
    return pWarpOn + (i - kSlots * kPerSlot);
}
namespace
{
// which slot / which part an id is: part 0 effect, 1 on, 2 mix, 10.. the generic parameters
bool slotOf(uint32_t id, int &s, int &part)
{
    if (id < SpectralFx::slotBase(0) || id >= SpectralFx::slotBase(kSlots)) return false;
    s = (int)((id - SpectralFx::slotBase(0)) / 100u);
    part = (int)((id - SpectralFx::slotBase(0)) % 100u);
    return part <= 2 || (part >= 10 && part < 10 + kHostParamsPerSlot);
}
} // namespace

bool SpectralFx::paramInfo(uint32_t index, clap_param_info *info) const
{
    if (index >= kParams) return false;
    std::memset(info, 0, sizeof(*info));
    const uint32_t id = paramIdAt(index);
    info->id = id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    std::strncpy(info->module, "Spectral", CLAP_PATH_SIZE - 1);
    auto name = [&](const std::string &n) { std::strncpy(info->name, n.c_str(), CLAP_NAME_SIZE - 1); };
    auto range = [&](double mn, double mx, double df, bool stepped) {
        info->min_value = mn;
        info->max_value = mx;
        info->default_value = df;
        if (stepped) info->flags |= CLAP_PARAM_IS_STEPPED;
    };
    int s, part;
    if (slotOf(id, s, part))
    {
        std::snprintf(info->module, CLAP_PATH_SIZE - 1, "Spectral/Slot %d", s + 1);
        const std::string pre = "Spectral " + std::to_string(s + 1) + " ";
        if (part == 0) { name(pre + "Effect"); range(0, (double)types().size() - 1, 0, true); }
        else if (part == 1) { name(pre + "On"); range(0, 1, 1, true); }
        else if (part == 2) { name(pre + "Mix"); range(0, 100, 100, false); }
        else
        {
            const auto &d = impl->desc(s);
            const int k = part - 10;
            name(k < (int)d.params.size() ? pre + d.params[(size_t)k].name : pre + "P" + std::to_string(k + 1));
            range(0, 1, 0, false);
        }
        return true;
    }
    if (id >= pWarpOn)
    {
        std::strncpy(info->module, "Spectral/WarpCore", CLAP_PATH_SIZE - 1);
        if (id == pWarpOn) { name("WarpCore On"); range(0, 1, 0, true); }
        else
        {
            const auto &d = warpParams()[(size_t)(id - pWarpParam(0))];
            name("WarpCore " + d.name);
            range(d.min, d.max, d.def, d.stepped());
        }
        return true;
    }
    switch (id)
    {
    case pOn: name("Spectral On"); range(0, 1, 0, true); break;
    case pMix: name("Spectral Mix"); range(0, 100, 100, false); break;
    case pFft: name("Spectral FFT Size"); range(0, kFftSizes - 1, kDefaultFft, true); info->flags &= ~(uint32_t)CLAP_PARAM_IS_AUTOMATABLE; break;
    case pWindow: name("Spectral Window"); range(0, kWindows - 1, 0, true); info->flags &= ~(uint32_t)CLAP_PARAM_IS_AUTOMATABLE; break;
    case pOverlap: name("Spectral Overlap"); range(2, 16, 4, false); info->flags &= ~(uint32_t)CLAP_PARAM_IS_AUTOMATABLE; break;
    case pLimit: name("Spectral Frequency Limit"); range(0, kLimits - 1, kDefaultLimit, true); break;
    case pChainMix: name("Spectral Chain Mix"); range(0, 100, 100, false); break;
    case pOrder: name("Spectral Order"); range(0, 1, 0, true); break;
    default: return false;
    }
    return true;
}

double SpectralFx::getParam(uint32_t id) const
{
    int s, part;
    if (slotOf(id, s, part))
    {
        auto &sl = impl->slot[s];
        if (part == 0) return sl.type.load();
        if (part == 1) return sl.on.load() ? 1 : 0;
        if (part == 2) return sl.mix.load();
        const auto &d = impl->desc(s);
        const int k = part - 10;
        return k < (int)d.params.size() ? d.params[(size_t)k].toNorm(sl.v[(size_t)k].load()) : 0.0;
    }
    if (id == pWarpOn) return impl->warpOn.load() ? 1 : 0;
    if (id > pWarpOn && id < pWarpOn + kWarpHostParams) return impl->warpV[(size_t)(id - pWarpParam(0))].load();
    switch (id)
    {
    case pOn: return onFlag.load() ? 1 : 0;
    case pMix: return impl->mix.load();
    case pFft: return impl->fft.load();
    case pWindow: return impl->window.load();
    case pOverlap: return impl->overlap.load();
    case pLimit: return impl->limit.load();
    case pChainMix: return impl->chainMix.load();
    case pOrder: return impl->order.load();
    default: return 0;
    }
}

void SpectralFx::setParam(uint32_t id, double v)
{
    if (!std::isfinite(v)) return;
    auto &I = *impl;
    int s, part;
    if (slotOf(id, s, part))
    {
        auto &sl = I.slot[s];
        if (part == 0) I.setType(s, (int)std::lround(v));
        else if (part == 1) sl.on = v > 0.5;
        else if (part == 2) sl.mix = (float)std::clamp(v, 0.0, 100.0);
        else
        {
            const auto &d = I.desc(s);
            const int k = part - 10;
            if (k < (int)d.params.size()) sl.v[(size_t)k] = d.params[(size_t)k].fromNorm((float)v);
        }
        sl.gen++;
        if (!I.desc(s).liveParams) I.mainFlag = true;
    }
    else if (id == pWarpOn) I.warpOn = v > 0.5;
    else if (id > pWarpOn && id < pWarpOn + kWarpHostParams)
    {
        const int k = (int)(id - pWarpParam(0));
        I.warpV[(size_t)k] = warpParams()[(size_t)k].clamp((float)v);
        I.warpGen++;
    }
    else
        switch (id)
        {
        case pOn: onFlag = v > 0.5; break;
        case pMix: I.mix = (float)std::clamp(v, 0.0, 100.0); break;
        case pFft: I.fft = std::clamp((int)std::lround(v), 0, kFftSizes - 1); I.mainFlag = true; break;
        case pWindow: I.window = std::clamp((int)std::lround(v), 0, kWindows - 1); I.mainFlag = true; break;
        case pOverlap: I.overlap = (float)std::clamp(v, 2.0, 16.0); I.mainFlag = true; break;
        case pLimit: I.limit = std::clamp((int)std::lround(v), 0, kLimits - 1); break;
        case pChainMix: I.chainMix = (float)std::clamp(v, 0.0, 100.0); break;
        case pOrder: I.order = v > 0.5 ? 1 : 0; break;
        default: return;
        }
    if (latencyWanted() != I.reported.load(std::memory_order_relaxed)) I.restartFlag = true;
    I.touch();
}

bool SpectralFx::valueToText(uint32_t id, double v, char *out, uint32_t n) const
{
    if (!isParam(id)) return false;
    int s, part;
    std::string t;
    char b[64];
    if (slotOf(id, s, part))
    {
        if (part == 0) t = types()[(size_t)std::clamp((int)std::lround(v), 0, (int)types().size() - 1)].name;
        else if (part == 1) t = v > 0.5 ? "On" : "Off";
        else if (part == 2) { std::snprintf(b, sizeof(b), "%.1f %%", v); t = b; }
        else
        {
            const auto &d = impl->desc(s);
            const int k = part - 10;
            t = k < (int)d.params.size() ? d.params[(size_t)k].text(d.params[(size_t)k].fromNorm((float)v)) : "-";
        }
    }
    else if (id == pWarpOn) t = v > 0.5 ? "On" : "Off";
    else if (id > pWarpOn)
    {
        const auto &d = warpParams()[(size_t)(id - pWarpParam(0))];
        t = d.id == "f_high" && v >= 20000.0 ? "Full" : d.text((float)v);
    }
    else
        switch (id)
        {
        case pOn: t = v > 0.5 ? "On" : "Off"; break;
        case pFft: t = std::to_string(fftSizeAt((int)std::lround(v))); break;
        case pWindow: t = windowName((int)std::lround(v)); break;
        case pOverlap: std::snprintf(b, sizeof(b), "%.2fx", v); t = b; break;
        case pLimit: std::snprintf(b, sizeof(b), "%d Hz", (int)limitHz((int)std::lround(v))); t = b; break;
        case pOrder: t = v > 0.5 ? "WarpCore > Chain" : "Chain > WarpCore"; break;
        default: std::snprintf(b, sizeof(b), "%.1f %%", v); t = b; break;
        }
    std::snprintf(out, n, "%s", t.c_str());
    return true;
}
bool SpectralFx::textToValue(uint32_t id, const char *txt, double *v) const
{
    if (!isParam(id) || !txt) return false;
    int s, part;
    float f = 0;
    if (slotOf(id, s, part))
    {
        if (part == 0)
        {
            for (auto &t : types())
                if (strcasecmp(t.name.c_str(), txt) == 0) { *v = t.index; return true; }
            *v = std::atof(txt);
            return true;
        }
        if (part == 1) { *v = (strcasecmp(txt, "on") == 0 || std::atof(txt) > 0.5) ? 1 : 0; return true; }
        if (part == 2) { *v = std::clamp(std::atof(txt), 0.0, 100.0); return true; }
        const auto &d = impl->desc(s);
        const int k = part - 10;
        if (k >= (int)d.params.size() || !d.params[(size_t)k].parse(txt, f)) return false;
        *v = d.params[(size_t)k].toNorm(f);
        return true;
    }
    if (id > pWarpOn)
    {
        const auto &d = warpParams()[(size_t)(id - pWarpParam(0))];
        if (strcasecmp(txt, "full") == 0) { *v = d.max; return true; }
        if (!d.parse(txt, f)) return false;
        *v = f;
        return true;
    }
    if (id == pOn || id == pWarpOn) { *v = (strcasecmp(txt, "on") == 0 || std::atof(txt) > 0.5) ? 1 : 0; return true; }
    if (id == pWindow)
        for (int i = 0; i < kWindows; ++i)
            if (strcasecmp(txt, windowName(i)) == 0) { *v = i; return true; }
    if (id == pFft)
    {
        const int size = std::atoi(txt);
        for (int i = 0; i < kFftSizes; ++i)
            if (fftSizeAt(i) == size) { *v = i; return true; }
    }
    *v = std::atof(txt);
    return true;
}

// ---- editor
bool SpectralFx::on() const { return onFlag.load(); }
float SpectralFx::mix() const { return impl->mix.load(); }
int SpectralFx::fftIndex() const { return impl->fft.load(); }
int SpectralFx::windowIndex() const { return impl->window.load(); }
float SpectralFx::overlap() const { return impl->overlap.load(); }
int SpectralFx::limitIndex() const { return impl->limit.load(); }
float SpectralFx::chainMix() const { return impl->chainMix.load(); }
int SpectralFx::order() const { return impl->order.load(); }
bool SpectralFx::warpOn() const { return impl->warpOn.load(); }
float SpectralFx::warpValue(int k) const { return impl->warpV[(size_t)std::clamp(k, 0, kWarpParams - 1)].load(); }
int SpectralFx::slotType(int s) const { return impl->slot[std::clamp(s, 0, kSlots - 1)].type.load(); }
bool SpectralFx::slotOn(int s) const { return impl->slot[std::clamp(s, 0, kSlots - 1)].on.load(); }
float SpectralFx::slotMix(int s) const { return impl->slot[std::clamp(s, 0, kSlots - 1)].mix.load(); }
float SpectralFx::slotValue(int s, int p) const
{
    return impl->slot[std::clamp(s, 0, kSlots - 1)].v[(size_t)std::clamp(p, 0, kMaxParams - 1)].load();
}
void SpectralFx::uiEdit(uint32_t id, double v, int type)
{
    if (!isParam(id)) return;
    if (type == 1) setParam(id, v);
    impl->toHost.push(HostMsg{id, type, (float)getParam(id)});
}
void SpectralFx::uiSlotValue(int s, int p, float real, int type)
{
    if (s < 0 || s >= kSlots) return;
    const auto &d = impl->desc(s);
    if (p < 0 || p >= (int)d.params.size()) return;
    if (p < kHostParamsPerSlot)
    {
        uiEdit(pSlotParam(s, p), d.params[(size_t)p].toNorm(real), type);
        return;
    }
    if (type != 1) return;
    impl->slot[s].v[(size_t)p] = d.params[(size_t)p].clamp(real);
    impl->slot[s].gen++;
    if (!d.liveParams) impl->mainFlag = true;
    impl->touch();
}
std::array<int, kSlots> SpectralFx::runOrder() const
{
    std::array<int, kSlots> o{};
    int n = 0;
    for (int pass = 0; pass < 2; ++pass)
        for (int s = 0; s < kSlots; ++s)
            if ((impl->desc(s).domain == Domain::Time) == (pass == 1)) o[(size_t)n++] = s;
    return o;
}
uint32_t SpectralFx::changeCount() const { return impl->changes.load(std::memory_order_relaxed); }
float SpectralFx::inLevel() const { return impl->inPeak.load(std::memory_order_relaxed); }
float SpectralFx::outLevel() const { return impl->outPeak.load(std::memory_order_relaxed); }
float SpectralFx::slotLevel(int s) const { return impl->slotPeak[(size_t)std::clamp(s, 0, kSlots - 1)].load(std::memory_order_relaxed); }
bool SpectralFx::spectrum(std::vector<float> &m, double &rate, bool input) { return coreSpectrum(impl->mainCore, m, rate, input); }

// ---- glue
uint32_t SpectralFx::latency() const noexcept { return impl->reported.load(std::memory_order_relaxed); }
uint32_t SpectralFx::latencyWanted() const noexcept { return impl->latencyWanted(onFlag.load(std::memory_order_relaxed)); }
void SpectralFx::activate(double sampleRate)
{
    auto &I = *impl;
    // the audio thread is stopped: everything it held is ours again
    freeUpdate(I.mailbox.exchange(nullptr));
    I.drainRetired();
    freeUpdate(I.pending);
    freeUpdate(I.unretired);
    I.pending = I.unretired = nullptr;
    // Everything is built again from the settings. What was just freed (an update still in the mailbox or waiting
    // on the audio side, e.g. the new FFT size whose latency asked for this restart) was what builtCfg / builtType /
    // builtGen describe, and mainCore (the editor's spectrum tap) pointed into it: keeping that bookkeeping would
    // leave the editor reading a freed core (heap corruption in the host) and the audio side on the old parts.
    freeCore(I.core);
    I.core = I.mainCore = nullptr;
    for (auto *&p : I.proc) { freeProc(p); p = nullptr; }
    I.built = false;
    for (auto &t : I.builtType) t = 0;
    for (auto &g : I.builtGen) g = 0;
    I.sr = sampleRate;
    I.build(true);
    if (!I.isa) I.isa = warpIsa();
    if (I.isa && !I.warp) I.warp = I.isa->create();
    if (I.isa && I.warp) I.isa->init(I.warp, (float)sampleRate);
    for (auto &r : I.inRing) std::fill(r.begin(), r.end(), 0.f);
    I.wpos = 0;
    I.lrep = latencyWanted();
    I.reported = I.lrep;
    I.askedFor = I.lrep;
    I.restartFlag = false;
    I.running = false;
    I.xf = 0.f;
    I.resetAll();
    I.warm = 0;
    inPath = I.lrep > 0;
}
bool SpectralFx::serviceMainThread()
{
    auto &I = *impl;
    I.mainFlag = false;
    I.drainRetired();
    I.build(false);
    if (latencyWanted() != I.reported.load()) I.restartFlag = true;
    return I.rescanFlag.exchange(false);
}
bool SpectralFx::takeRestartRequest()
{
    auto &I = *impl;
    if (!I.restartFlag.exchange(false, std::memory_order_relaxed)) return false;
    const uint32_t w = latencyWanted();
    if (w == I.reported.load(std::memory_order_relaxed) || w == I.askedFor) return false;
    I.askedFor = w; // (one request per new value)
    return true;
}
bool SpectralFx::takeMainThreadRequest() { return impl->mainFlag.exchange(false, std::memory_order_relaxed); }
void SpectralFx::requestReset() noexcept { impl->resetFlag.store(true, std::memory_order_relaxed); }
void SpectralFx::sendToHost(const clap_output_events *out)
{
    while (auto m = impl->toHost.pop())
    {
        if (!out) continue;
        if (m->type == 1)
        {
            clap_event_param_value e{};
            e.header.size = sizeof(e);
            e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            e.header.type = CLAP_EVENT_PARAM_VALUE;
            e.param_id = m->id;
            e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1;
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

void SpectralFx::processImpl(float *L, float *R, uint32_t frames, double tempo) noexcept
{
    auto &I = *impl;
    if (I.sr <= 0 || !I.core) return;
    if (I.unretired) { auto *u = I.unretired; I.unretired = nullptr; I.retire(u); }
    if (!I.pending && !I.unretired) I.pending = I.mailbox.exchange(nullptr, std::memory_order_acq_rel);
    if (I.resetFlag.exchange(false, std::memory_order_relaxed) && I.running)
    {
        I.resetAll();
        I.xf = 0.f;
    }
    float pin = 0, pout = 0;
    for (uint32_t i = 0; i < frames; ++i) pin = std::max(pin, std::max(std::abs(L[i]), std::abs(R[i])));
    for (uint32_t done = 0; done < frames;)
    {
        const int n = (int)std::min<uint32_t>(kQuantum, frames - done);
        I.quantum(L + done, R + done, n, tempo, onFlag.load(std::memory_order_relaxed));
        done += (uint32_t)n;
    }
    for (uint32_t i = 0; i < frames; ++i) pout = std::max(pout, std::max(std::abs(L[i]), std::abs(R[i])));
    I.inPeak.store(std::max(pin, I.inPeak.load(std::memory_order_relaxed) * 0.9f), std::memory_order_relaxed);
    I.outPeak.store(std::max(pout, I.outPeak.load(std::memory_order_relaxed) * 0.9f), std::memory_order_relaxed);
    inPath.store(I.running || I.lrep > 0 || I.pending != nullptr, std::memory_order_relaxed);
    if (latencyWanted() != I.lrep) I.restartFlag.store(true, std::memory_order_relaxed);
}

// ---- state
std::string SpectralFx::toState() const
{
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o.precision(9);
    auto &I = *impl;
    o << "P2SPECTRAL 1\n";
    o << "on " << (onFlag.load() ? 1 : 0) << "\nmix " << I.mix.load() << "\nfft " << fftSizeAt(I.fft.load()) << "\nwindow " << I.window.load() << "\noverlap "
      << I.overlap.load() << "\nlimit " << I.limit.load() << "\nchainmix " << I.chainMix.load() << "\norder " << I.order.load() << "\n";
    for (int s = 0; s < kSlots; ++s)
    {
        const auto &d = I.desc(s);
        o << "slot " << s << " " << (d.id.empty() ? "-" : d.id) << " " << (I.slot[s].on.load() ? 1 : 0) << " " << I.slot[s].mix.load() << " " << d.params.size() << "\n";
        for (size_t i = 0; i < d.params.size(); ++i) o << d.params[i].id << " " << I.slot[s].v[i].load() << "\n";
    }
    o << "warp " << (I.warpOn.load() ? 1 : 0) << " " << kWarpParams << "\n";
    for (int k = 0; k < kWarpParams; ++k) o << warpParams()[(size_t)k].id << " " << I.warpV[(size_t)k].load() << "\n";
    o << "end\n";
    return o.str();
}
void SpectralFx::resetToDefaults()
{
    auto &I = *impl;
    onFlag = false;
    I.mix = 100.f; I.chainMix = 100.f; I.overlap = 4.f; I.fft = kDefaultFft; I.window = 0; I.limit = kDefaultLimit; I.order = 0;
    for (int s = 0; s < kSlots; ++s)
    {
        I.setType(s, 0);
        I.slot[s].on = true;
        I.slot[s].mix = 100.f;
        I.slot[s].gen++;
    }
    I.warpOn = false;
    for (int k = 0; k < kWarpParams; ++k) I.warpV[(size_t)k] = warpParams()[(size_t)k].def;
    I.warpGen++;
    I.rescanFlag = true;
    I.mainFlag = true;
    I.touch();
}
bool SpectralFx::fromState(const std::string &st)
{
    std::istringstream in(st);
    in.imbue(std::locale::classic());
    std::string tag;
    int ver = 0;
    in >> tag >> ver;
    if (tag != "P2SPECTRAL" || ver < 1) return false;
    resetToDefaults();
    auto &I = *impl;
    std::string key;
    while (in >> key && key != "end")
    {
        if (key == "slot")
        {
            int s = 0, onI = 1;
            std::string id;
            float mx = 100;
            size_t n = 0;
            if (!(in >> s >> id >> onI >> mx >> n)) return false;
            const bool ok = s >= 0 && s < kSlots;
            const int t = id == "-" ? 0 : typeIndex(id);
            if (ok) I.setType(s, std::max(0, t)); // (an effect this build does not know: the slot stays empty)
            for (size_t i = 0; i < n; ++i)
            {
                std::string pid;
                float v = 0;
                if (!(in >> pid >> v)) return false;
                if (!ok || t <= 0) continue;
                const auto &d = types()[(size_t)t];
                for (size_t k = 0; k < d.params.size(); ++k)
                    if (d.params[k].id == pid) I.slot[s].v[k] = d.params[k].clamp(v);
            }
            if (ok)
            {
                I.slot[s].on = onI != 0;
                I.slot[s].mix = std::clamp(mx, 0.f, 100.f);
                I.slot[s].gen++;
            }
        }
        else if (key == "warp")
        {
            int onI = 0;
            size_t n = 0;
            if (!(in >> onI >> n)) return false;
            for (size_t i = 0; i < n; ++i)
            {
                std::string pid;
                float v = 0;
                if (!(in >> pid >> v)) return false;
                for (int k = 0; k < kWarpParams; ++k)
                    if (warpParams()[(size_t)k].id == pid) I.warpV[(size_t)k] = warpParams()[(size_t)k].clamp(v);
            }
            I.warpOn = onI != 0;
            I.warpGen++;
        }
        else
        {
            double v = 0;
            if (!(in >> v)) return false;
            if (key == "on") onFlag = v > 0.5;
            else if (key == "mix") I.mix = (float)std::clamp(v, 0.0, 100.0);
            else if (key == "fft")
            {
                for (int i = 0; i < kFftSizes; ++i)
                    if (fftSizeAt(i) == (int)v) I.fft = i;
            }
            else if (key == "window") I.window = std::clamp((int)v, 0, kWindows - 1);
            else if (key == "overlap") I.overlap = (float)std::clamp(v, 2.0, 16.0);
            else if (key == "limit") I.limit = std::clamp((int)v, 0, kLimits - 1);
            else if (key == "chainmix") I.chainMix = (float)std::clamp(v, 0.0, 100.0);
            else if (key == "order") I.order = v > 0.5 ? 1 : 0;
            // (a key from a newer version: skipped)
        }
    }
    I.rescanFlag = true;
    I.mainFlag = true;
    if (latencyWanted() != I.reported.load()) I.restartFlag = true;
    I.touch();
    return true;
}
} // namespace ghostblock::p2::spectral
