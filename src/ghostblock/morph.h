/*
 * Poltergeist 2 (GhostBlock) - MORPH: the blend board.
 *
 * Four corners, each holding a sound. A corner is empty, THIS SOUND (the plugin's own engine, the one the
 * ghost pages edit), or its own sound played by its own copy of the Six Sines engine (unchanged upstream
 * code, one more instance). The puck mixes the REAL sounds (owner, 2026-11-07, as in Ghost Bass): bilinear
 * weights over the four corners, equal power, renormalised over the corners that hold a sound, so the puck
 * on a corner is exactly that sound. Volume and pan per corner. The mix happens after the engines, in the
 * CLAP wrapper; nothing inside an engine changes.
 *
 * Threads: the main thread owns the engines (made once, never freed while the plugin lives) and loads
 * sounds into them through their own queues; the audio thread reads the published pointers and the atomics.
 */
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <clap/clap.h>
#include "synth/synth.h"
#include "sst/cpputils/ring_buffer.h"
#include <cstdio>
#include <cstring>

namespace ghostblock::p2
{
namespace ss6 = baconpaul::six_sines;

struct Morph
{
    static constexpr int N = 4;
    enum Kind : int
    {
        EMPTY = 0,
        THIS_SOUND = 1, // the main engine
        OWN = 2         // its own engine
    };

    std::atomic<bool> on{false};
    std::atomic<float> x{0.5f}, y{0.5f};
    std::array<std::atomic<int>, N> kind{};
    std::array<std::atomic<float>, N> volDb{}, pan{};
    std::array<std::atomic<ss6::Synth *>, N> live{}; // audio thread: the corner's engine (OWN only)
    std::array<std::string, N> names{};             // main thread
    std::array<std::unique_ptr<ss6::Synth>, N> owned{};
    std::atomic<uint32_t> changed{0}; // bumped on a main-thread change (the editor refreshes)

    // ---- OCTAVE per corner (owner, 2026-11-12): -3 .. +3, the notes that corner's engine hears move by octaves.
    // Only the keys are moved (the engines and their sounds are untouched). A note is let go at the key it was
    // played at, even if the octave changed while it was held (no stuck notes).
    static constexpr int kOctMin = -3, kOctMax = 3;
    std::array<std::atomic<int>, N> octave{};
    int8_t heldShift[1 + N][16][128]{}; // audio thread: [0] THIS SOUND's engine, [1 + c] corner c's own engine
    static int clampKey(int k) { return std::clamp(k, 0, 127); }
    // the semitones for an engine slot now: slot 0 is the main engine (moved only while MORPH is on, by the corner that holds THIS SOUND)
    int semisFor(int slot) const
    {
        if (slot == 0)
        {
            if (!on.load(std::memory_order_relaxed))
                return 0;
            const int c = thisSoundCorner();
            return c < 0 ? 0 : 12 * octave[(size_t)c].load(std::memory_order_relaxed);
        }
        return 12 * octave[(size_t)(slot - 1)].load(std::memory_order_relaxed);
    }
    int keyOn(int slot, int ch, int key)
    {
        const int s = semisFor(slot);
        heldShift[slot][ch < 0 ? 0 : ch & 15][key & 127] = (int8_t)s;
        return clampKey(key + s);
    }
    int keyHeld(int slot, int ch, int key) const { return clampKey(key + heldShift[slot][ch < 0 ? 0 : ch & 15][key & 127]); }
    // a raw MIDI 1 message, its key moved (note on / off / poly pressure); out = in otherwise
    void midi(int slot, const uint8_t in[3], uint8_t out[3])
    {
        out[0] = in[0];
        out[1] = in[1];
        out[2] = in[2];
        const int st = in[0] & 0xf0, ch = in[0] & 0x0f;
        if (st == 0x90 && in[2] > 0)
            out[1] = (uint8_t)keyOn(slot, ch, in[1]);
        else if (st == 0x80 || st == 0x90 || st == 0xa0)
            out[1] = (uint8_t)keyHeld(slot, ch, in[1]);
    }

    // ---- ORBIT: the puck moves by itself, locked to the song's bars, around where you put it
    enum Orbit : int { O_OFF = 0, O_CIRCLE, O_EIGHT, O_SQUARE, O_SWING, O_DRIFT, O_COUNT };
    static constexpr const char *orbitNames[O_COUNT] = {"OFF", "CIRCLE", "FIGURE 8", "SQUARE", "SWING", "DRIFT"};
    static constexpr int kRates = 7;
    static constexpr float rateBars[kRates] = {0.25f, 0.5f, 1, 2, 4, 8, 16};
    static constexpr const char *rateNames[kRates] = {"1/4 BAR", "1/2 BAR", "1 BAR", "2 BARS", "4 BARS", "8 BARS", "16 BARS"};
    std::atomic<int> orbit{O_OFF}, orbitRate{3};
    std::atomic<float> orbitSize{0.6f};
    std::atomic<float> effX{0.5f}, effY{0.5f}; // where the puck really is (= x, y unless orbiting); the audio thread writes
    double freePhaseBeats{0};                   // audio thread: the clock when the song is not playing

    // the puck, orbiting: the shape's point at phase 0..1, around (cx, cy)
    static void orbitAt(int shape, float size, float ph, float cx, float cy, float &ox, float &oy)
    {
        const float r = 0.5f * std::clamp(size, 0.f, 1.f), tw = 6.28318530718f;
        float dx = 0, dy = 0;
        switch (shape)
        {
        case O_CIRCLE: dx = r * std::cos(tw * ph); dy = r * std::sin(tw * ph); break;
        case O_EIGHT: dx = r * std::sin(tw * ph); dy = 0.5f * r * std::sin(2 * tw * ph); break;
        case O_SQUARE:
        { // round the edges of a square: top, right, bottom, left
            const float q = ph * 4.f, f = q - std::floor(q);
            switch ((int)std::floor(q) & 3)
            {
            case 0: dx = -r + 2 * r * f; dy = -r; break;
            case 1: dx = r; dy = -r + 2 * r * f; break;
            case 2: dx = r - 2 * r * f; dy = r; break;
            default: dx = -r; dy = r - 2 * r * f; break;
            }
        }
        break;
        case O_SWING: dx = r * std::sin(tw * ph); break; // a pendulum, side to side
        case O_DRIFT:
        { // a smooth wander: two slow value-noise curves (the same path every loop)
            auto noise = [](float t, uint32_t seed) {
                auto h = [seed](int i) { uint32_t v = (uint32_t)(i & 7) * 2654435761u ^ seed; v ^= v >> 13; v *= 0x5bd1e995u; v ^= v >> 15; return (float)(v & 0xffff) / 32767.5f - 1.f; };
                const int i = (int)std::floor(t);
                const float f = t - (float)i, sm = f * f * (3 - 2 * f);
                return h(i) + (h(i + 1) - h(i)) * sm; // 8 lattice points that wrap: the loop joins up
            };
            const float t = ph * 8.f;
            dx = r * noise(t, 0x9e3779b9u);
            dy = r * noise(t, 0x7f4a7c15u);
        }
        break;
        default: break;
        }
        ox = std::clamp(cx + dx, 0.f, 1.f);
        oy = std::clamp(cy + dy, 0.f, 1.f);
    }
    // audio thread, once per host buffer: move the puck (songBeats < 0 = the song is not playing)
    void advance(double songBeats, double tempo, uint32_t frames, double sr)
    {
        const int sh = orbit.load(std::memory_order_relaxed);
        if (sh == O_OFF || sr <= 0)
        {
            effX.store(x.load(std::memory_order_relaxed), std::memory_order_relaxed);
            effY.store(y.load(std::memory_order_relaxed), std::memory_order_relaxed);
            return;
        }
        const float bars = rateBars[std::clamp(orbitRate.load(std::memory_order_relaxed), 0, kRates - 1)];
        double beats;
        if (songBeats >= 0)
            beats = freePhaseBeats = songBeats;
        else
        {
            freePhaseBeats += (double)frames / sr * (tempo > 0 ? tempo : 120.0) / 60.0;
            beats = freePhaseBeats;
        }
        const double L = 4.0 * bars;
        const float ph = (float)(beats / L - std::floor(beats / L));
        float ox, oy;
        orbitAt(sh, orbitSize.load(std::memory_order_relaxed), ph, x.load(std::memory_order_relaxed), y.load(std::memory_order_relaxed), ox, oy);
        effX.store(ox, std::memory_order_relaxed);
        effY.store(oy, std::memory_order_relaxed);
    }

    // ---- the host can automate MORPH: ON, the puck's X / Y, ORBIT, its RATE and SIZE (ids after the engine's)
    static constexpr uint32_t pOn = 900001, pX = 900002, pY = 900003, pOrbit = 900004, pRate = 900005, pSize = 900006;
    static constexpr uint32_t kParams = 6;
    static constexpr uint32_t paramIdAt(uint32_t i) { return pOn + i; }
    static bool isParam(uint32_t id) { return id >= pOn && id < pOn + kParams; }
    bool paramInfo(uint32_t i, clap_param_info *info) const
    {
        if (i >= kParams) return false;
        memset(info, 0, sizeof(*info));
        info->id = paramIdAt(i);
        info->flags = CLAP_PARAM_IS_AUTOMATABLE;
        strncpy(info->module, "Morph", CLAP_PATH_SIZE - 1);
        const char *nm[kParams] = {"Morph On", "Morph X", "Morph Y", "Morph Orbit", "Morph Orbit Rate", "Morph Orbit Size"};
        strncpy(info->name, nm[i], CLAP_NAME_SIZE - 1);
        switch (info->id)
        {
        case pOn: info->min_value = 0; info->max_value = 1; info->default_value = 0; info->flags |= CLAP_PARAM_IS_STEPPED; break;
        case pOrbit: info->min_value = 0; info->max_value = O_COUNT - 1; info->default_value = 0; info->flags |= CLAP_PARAM_IS_STEPPED; break;
        case pRate: info->min_value = 0; info->max_value = kRates - 1; info->default_value = 3; info->flags |= CLAP_PARAM_IS_STEPPED; break;
        case pSize: info->min_value = 0; info->max_value = 1; info->default_value = 0.6; break;
        default: info->min_value = 0; info->max_value = 1; info->default_value = 0.5; break;
        }
        return true;
    }
    double getParam(uint32_t id) const
    {
        switch (id)
        {
        case pOn: return on.load() ? 1 : 0;
        case pX: return x.load();
        case pY: return y.load();
        case pOrbit: return orbit.load();
        case pRate: return orbitRate.load();
        case pSize: return orbitSize.load();
        default: return 0;
        }
    }
    void setParam(uint32_t id, double v)
    {
        switch (id)
        {
        case pOn: on.store(v > 0.5); break;
        case pX: x.store((float)std::clamp(v, 0.0, 1.0)); if (orbit.load() == O_OFF) effX.store(x.load()); break;
        case pY: y.store((float)std::clamp(v, 0.0, 1.0)); if (orbit.load() == O_OFF) effY.store(y.load()); break;
        case pOrbit: orbit.store(std::clamp((int)std::lround(v), 0, O_COUNT - 1)); break;
        case pRate: orbitRate.store(std::clamp((int)std::lround(v), 0, kRates - 1)); break;
        case pSize: orbitSize.store((float)std::clamp(v, 0.0, 1.0)); break;
        default: break;
        }
    }
    bool valueToText(uint32_t id, double v, char *out, uint32_t n) const
    {
        if (!isParam(id)) return false;
        switch (id)
        {
        case pOn: snprintf(out, n, "%s", v > 0.5 ? "On" : "Off"); break;
        case pOrbit: snprintf(out, n, "%s", orbitNames[std::clamp((int)std::lround(v), 0, O_COUNT - 1)]); break;
        case pRate: snprintf(out, n, "%s", rateNames[std::clamp((int)std::lround(v), 0, kRates - 1)]); break;
        default: snprintf(out, n, "%d %%", (int)std::lround(v * 100)); break;
        }
        return true;
    }
    bool textToValue(uint32_t id, const char *t, double *v) const
    {
        if (!isParam(id)) return false;
        for (int i = 0; i < O_COUNT && id == pOrbit; ++i)
            if (strcasecmp(t, orbitNames[i]) == 0) { *v = i; return true; }
        for (int i = 0; i < kRates && id == pRate; ++i)
            if (strcasecmp(t, rateNames[i]) == 0) { *v = i; return true; }
        if (id == pOn) { *v = (strcasecmp(t, "on") == 0 || atof(t) > 0.5) ? 1 : 0; return true; }
        double d = atof(t);
        *v = (id == pOrbit || id == pRate) ? d : std::clamp(d > 1.0 ? d / 100.0 : d, 0.0, 1.0);
        return true;
    }
    // the editor's edits of these, on their way to the host as gestures (UI pushes, audio / flush pops)
    struct HostMsg
    {
        uint32_t id{0};
        int type{1}; // 0 begin, 1 value, 2 end
        float value{0};
    };
    sst::cpputils::SimpleRingBuffer<HostMsg, 1024> toHost;
    // UI: set it now (so the sound follows at once) and tell the host
    void uiEdit(uint32_t id, double v, int type)
    {
        if (type == 1) setParam(id, v);
        toHost.push(HostMsg{id, type, (float)getParam(id)});
    }
    // audio thread (or a flush): hand the editor's edits to the host
    void sendToHost(const clap_output_events *out)
    {
        while (auto m = toHost.pop())
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

    double sampleRate{0}; // set by activate (main thread); 0 = not active
    // the corner engines' host: no params, no callbacks (they are not the host's plugin; the wrapper drives them)
    static const clap_host_t *quietHost()
    {
        static clap_host_t h = [] {
            clap_host_t q{};
            q.clap_version = CLAP_VERSION;
            q.name = "Poltergeist 2 morph";
            q.vendor = "GhostBlock";
            q.url = "";
            q.version = "1";
            q.get_extension = [](const clap_host_t *, const char *) -> const void * { return nullptr; };
            q.request_restart = [](const clap_host_t *) {};
            q.request_process = [](const clap_host_t *) {};
            q.request_callback = [](const clap_host_t *) {};
            return q;
        }();
        return &h;
    }

    Morph()
    {
        for (int c = 0; c < N; ++c)
        {
            kind[(size_t)c] = EMPTY;
            volDb[(size_t)c] = 0.f;
            pan[(size_t)c] = 0.f;
            live[(size_t)c] = nullptr;
        }
        kind[0] = THIS_SOUND; // corner A starts as the sound you are editing
        names[0] = "THIS SOUND";
    }

    // main thread: the corner's engine, made (and readied for the current sample rate) the first time
    ss6::Synth *engineFor(int c)
    {
        auto &o = owned[(size_t)c];
        if (!o)
        {
            o = std::make_unique<ss6::Synth>(false);
            o->clapHost = quietHost();
            if (sampleRate > 0)
            {
                o->patch.copyValuesFrom(o->patchMain);
                o->setSampleRate(sampleRate);
            }
            live[(size_t)c].store(o.get());
        }
        return o.get();
    }
    // main thread (audio stopped): a new rate for every corner engine
    void activate(double sr)
    {
        sampleRate = sr;
        for (auto &o : owned)
            if (o)
            {
                o->patch.copyValuesFrom(o->patchMain);
                o->setSampleRate(sr);
            }
    }

    // main thread: a sound (patch state) into corner c
    void setOwn(int c, const std::string &state, const std::string &name)
    {
        auto *e = engineFor(c);
        e->patchMain.fromState(state);
        ss6::Synth::sendEntirePatchToAudio(e->patchMain, e->mainToAudio, quietHost());
        names[(size_t)c] = name.empty() ? std::string(e->patchMain.name) : name;
        kind[(size_t)c] = OWN;
        changed++;
    }
    void setThisSound(int c)
    {
        for (int k = 0; k < N; ++k) // one corner at most is THIS SOUND
            if (k != c && kind[(size_t)k] == THIS_SOUND) { kind[(size_t)k] = EMPTY; names[(size_t)k].clear(); }
        kind[(size_t)c] = THIS_SOUND;
        names[(size_t)c] = "THIS SOUND";
        changed++;
    }
    void clear(int c)
    {
        kind[(size_t)c] = EMPTY;
        names[(size_t)c].clear();
        changed++;
    }
    int thisSoundCorner() const
    {
        for (int c = 0; c < N; ++c)
            if (kind[(size_t)c] == THIS_SOUND) return c;
        return -1;
    }

    // each corner's share at the puck (corner 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right). All four
    // filled: bilinear (the board's grid). Some empty: inverse-distance over the filled ones, so the share stays
    // smooth everywhere and the puck on a corner is still exactly that corner. False when no corner holds a sound.
    static bool weightsAt(float px, float py, const int k[N], float w[N])
    {
        px = std::clamp(px, 0.f, 1.f);
        py = std::clamp(py, 0.f, 1.f);
        int filled = 0;
        for (int c = 0; c < N; ++c) filled += k[c] != EMPTY;
        if (filled == 0) return false;
        if (filled == N)
        {
            w[0] = (1 - px) * (1 - py); w[1] = px * (1 - py); w[2] = (1 - px) * py; w[3] = px * py;
            return true;
        }
        const float cx[N] = {0, 1, 0, 1}, cy[N] = {0, 0, 1, 1};
        float sum = 0;
        for (int c = 0; c < N; ++c)
        {
            w[c] = 0.f;
            if (k[c] == EMPTY) continue;
            const float d2 = (px - cx[c]) * (px - cx[c]) + (py - cy[c]) * (py - cy[c]);
            if (d2 < 1e-10f)
            { // on this corner: exactly it
                for (int j = 0; j < N; ++j) w[j] = j == c ? 1.f : 0.f;
                return true;
            }
            sum += (w[c] = 1.f / d2);
        }
        for (int c = 0; c < N; ++c) w[c] /= sum;
        return true;
    }
    bool weights(float w[N]) const
    {
        int k[N];
        for (int c = 0; c < N; ++c) k[c] = kind[(size_t)c].load();
        return weightsAt(effX.load(), effY.load(), k, w);
    }

    // ---- state (after the patch in the plugin's saved state)
    std::string toState() const
    {
        std::ostringstream o;
        o << "P2MORPH 3\n" << (on.load() ? 1 : 0) << " " << x.load() << " " << y.load() << " " << orbit.load() << " " << orbitRate.load() << " " << orbitSize.load();
        for (int c = 0; c < N; ++c)
            o << " " << octave[(size_t)c].load();
        o << "\n";
        for (int c = 0; c < N; ++c)
        {
            const int k = kind[(size_t)c].load();
            std::string st;
            if (k == OWN && owned[(size_t)c]) st = owned[(size_t)c]->patchMain.toState();
            o << c << " " << k << " " << volDb[(size_t)c].load() << " " << pan[(size_t)c].load() << " " << names[(size_t)c].size() << " "
              << st.size() << "\n"
              << names[(size_t)c] << st << "\n";
        }
        return o.str();
    }
    bool fromState(const std::string &s)
    {
        std::istringstream in(s);
        std::string tag;
        int ver{0};
        in >> tag >> ver;
        if (tag != "P2MORPH" || ver < 1) return false;
        int onI{0};
        float px{0.5f}, py{0.5f};
        in >> onI >> px >> py;
        int ob{O_OFF}, rt{3};
        float sz{0.6f};
        if (ver >= 2) in >> ob >> rt >> sz;
        int oc[N]{};
        if (ver >= 3)
            for (int c = 0; c < N; ++c) in >> oc[c];
        for (int c = 0; c < N; ++c) octave[(size_t)c] = std::clamp(oc[c], (int)kOctMin, (int)kOctMax);
        for (int i = 0; i < N; ++i)
        {
            int c{0}, k{0};
            float v{0}, p{0};
            size_t nl{0}, sl{0};
            if (!(in >> c >> k >> v >> p >> nl >> sl)) return false;
            in.get(); // the newline
            std::string nm(nl, '\0'), st(sl, '\0');
            in.read(nm.data(), (std::streamsize)nl);
            in.read(st.data(), (std::streamsize)sl);
            if (c < 0 || c >= N) continue;
            volDb[(size_t)c] = v;
            pan[(size_t)c] = p;
            if (k == OWN && !st.empty()) setOwn(c, st, nm);
            else if (k == THIS_SOUND) setThisSound(c);
            else clear(c);
        }
        x = px;
        y = py;
        effX = px;
        effY = py;
        orbit = std::clamp(ob, 0, O_COUNT - 1);
        orbitRate = std::clamp(rt, 0, kRates - 1);
        orbitSize = std::clamp(sz, 0.f, 1.f);
        on = onI != 0;
        changed++;
        return true;
    }
};
} // namespace ghostblock::p2
