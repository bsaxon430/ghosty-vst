/*
 * Poltergeist 2 (GhostBlock) - render-hash: the proof that a build sounds exactly like another build.
 *
 * For every factory preset: a fresh CLAP plugin, the preset loaded the way a host loads it, three notes played,
 * and a hash of every output sample's bits. Run it on two builds and compare the files: equal files = equal sound,
 * bit for bit. It only uses what both the build before SPECTRAL FX (135d871) and the builds after it have
 * (the CLAP plugin, the preset list, MORPH), so the same source compiles in both.
 *
 *   p2-render-hash <out.txt> [first] [count]
 */
#include <clap/clap.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "ghostblock/morph.h"
#include "presets/preset-manager.h"
#include "synth/synth.h"
#include <juce_events/juce_events.h>

namespace baconpaul::six_sines
{
extern const clap_plugin *makePlugin(const clap_host *, bool);
}
namespace ss = baconpaul::six_sines;
using ghostblock::p2::Morph;

namespace
{
struct EvList
{
    std::vector<const clap_event_header_t *> all;
    static uint32_t size(const clap_input_events *l) { return (uint32_t)((EvList *)l->ctx)->all.size(); }
    static const clap_event_header_t *get(const clap_input_events *l, uint32_t i) { return ((EvList *)l->ctx)->all[i]; }
};
bool tryPush(const clap_output_events *, const clap_event_header_t *) { return true; }
struct MemIn
{
    std::string d;
    size_t pos{0};
    static int64_t read(const clap_istream *s, void *b, uint64_t n)
    {
        auto *m = (MemIn *)s->ctx;
        const size_t k = std::min<size_t>((size_t)n, m->d.size() - m->pos);
        memcpy(b, m->d.data() + m->pos, k);
        m->pos += k;
        return (int64_t)k;
    }
};
clap_host makeHost()
{
    clap_host host{};
    host.clap_version = CLAP_VERSION;
    host.name = "p2-render-hash";
    host.vendor = "ghostblock";
    host.url = "";
    host.version = "1";
    host.get_extension = [](const clap_host *, const char *) -> const void * { return nullptr; };
    host.request_restart = [](const clap_host *) {};
    host.request_process = [](const clap_host *) {};
    host.request_callback = [](const clap_host *) {};
    return host;
}
struct Result
{
    uint64_t hash{1469598103934665603ull};
    double peak{0}, sumSq{0};
    uint64_t n{0};
    void add(const float *x, uint32_t count)
    {
        for (uint32_t i = 0; i < count; ++i)
        {
            uint32_t bits;
            memcpy(&bits, &x[i], 4);
            for (int b = 0; b < 4; ++b)
            {
                hash ^= (bits >> (8 * b)) & 0xffu;
                hash *= 1099511628211ull;
            }
            peak = std::max(peak, (double)std::fabs(x[i]));
            sumSq += (double)x[i] * x[i];
            ++n;
        }
    }
};

// one render: preset `idx` (or -1 = the init sound), `morphState` appended to the state when not empty
Result render(const clap_host *host, int idx, double sr, uint32_t bs, const std::string &morphState)
{
    Result r;
    auto *pl = ss::makePlugin(host, false);
    pl->init(pl);
    if (idx >= 0)
    {
        auto *pld = static_cast<const clap_plugin_preset_load *>(pl->get_extension(pl, CLAP_EXT_PRESET_LOAD));
        const auto key = std::to_string(idx);
        if (!pld || !pld->from_location(pl, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, key.c_str()))
        {
            pl->destroy(pl);
            r.n = 0;
            return r;
        }
    }
    if (!morphState.empty())
    {
        // the state a project holds: this sound's patch, a zero, MORPH, a zero
        std::string st;
        {
            struct Out
            {
                std::string d;
                static int64_t write(const clap_ostream *s, const void *b, uint64_t n)
                {
                    ((Out *)s->ctx)->d.append((const char *)b, (size_t)n);
                    return (int64_t)n;
                }
            } o;
            clap_ostream os{&o, &Out::write};
            auto *se = static_cast<const clap_plugin_state *>(pl->get_extension(pl, CLAP_EXT_STATE));
            se->save(pl, &os);
            st = o.d.substr(0, o.d.find('\0'));
        }
        st.push_back('\0');
        st += morphState;
        st.push_back('\0');
        MemIn mi{st};
        clap_istream is{&mi, &MemIn::read};
        auto *se = static_cast<const clap_plugin_state *>(pl->get_extension(pl, CLAP_EXT_STATE));
        se->load(pl, &is);
    }
    pl->activate(pl, sr, 1, bs);
    pl->start_processing(pl);
    const int total = (int)(sr * 1.5), onAt = 1024, offAt = (int)(sr * 1.0);
    std::vector<float> inL(bs, 0.f), inR(bs, 0.f), oL(bs), oR(bs);
    EvList ev;
    clap_input_events in{&ev, &EvList::size, &EvList::get};
    clap_output_events o{nullptr, &tryPush};
    std::vector<clap_event_note> notes;
    notes.reserve(16);
    const int16_t keys[3] = {36, 48, 55};
    auto note = [&](uint16_t type, int16_t key, uint32_t t) {
        clap_event_note n{};
        n.header.size = sizeof(n);
        n.header.time = t;
        n.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        n.header.type = type;
        n.note_id = -1;
        n.port_index = 0;
        n.channel = 0;
        n.key = key;
        n.velocity = 0.8;
        notes.push_back(n);
    };
    for (int pos = 0; pos < total; pos += (int)bs)
    {
        notes.clear();
        ev.all.clear();
        if (pos <= onAt && onAt < pos + (int)bs)
            for (auto k : keys) note(CLAP_EVENT_NOTE_ON, k, (uint32_t)(onAt - pos));
        if (pos <= offAt && offAt < pos + (int)bs)
            for (auto k : keys) note(CLAP_EVENT_NOTE_OFF, k, (uint32_t)(offAt - pos));
        for (auto &n : notes) ev.all.push_back(&n.header);
        float *ip[2] = {inL.data(), inR.data()};
        float *op[2] = {oL.data(), oR.data()};
        clap_audio_buffer ib{ip, nullptr, 2, 0, 0}, ob{op, nullptr, 2, 0, 0};
        clap_process p{};
        p.steady_time = pos;
        p.frames_count = bs;
        p.audio_inputs = &ib;
        p.audio_inputs_count = 1;
        p.audio_outputs = &ob;
        p.audio_outputs_count = 1;
        p.in_events = &in;
        p.out_events = &o;
        pl->process(pl, &p);
        r.add(oL.data(), bs);
        r.add(oR.data(), bs);
    }
    pl->stop_processing(pl);
    pl->deactivate(pl);
    pl->destroy(pl);
    return r;
}
} // namespace

int main(int argc, char **argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 2)
    {
        std::printf("usage: p2-render-hash <out.txt> [first] [count]\n");
        return 2;
    }
    auto host = makeHost();
    ss::presets::PresetManager pm(&host);
    const int n = (int)pm.factoryPatchVector.size();
    const int first = argc > 2 ? std::atoi(argv[2]) : -1;
    const int count = argc > 3 ? std::atoi(argv[3]) : n + 1;
    FILE *f = std::fopen(argv[1], "w");
    if (!f) return 3;
    // MORPH for the "m" renders: corner B = its own sound (the init sound an octave up), the puck between A and B
    std::string morphState;
    {
        ss::Patch b;
        b.sourceNodes[0].ratio.value = 1.0f;
        std::strncpy(b.name, "Octave", sizeof(b.name) - 1);
        Morph m;
        m.setOwn(1, b.toState(), "Octave");
        m.on = true;
        m.x = 0.5f;
        m.y = 0.f;
        m.effX = 0.5f;
        m.effY = 0.f;
        morphState = m.toState();
    }
    int done = 0;
    for (int i = first; i < n && done < count; ++i, ++done)
    {
        const std::string name = i < 0 ? std::string("(init)") : pm.factoryPatchVector[(size_t)i].first + "/" + pm.factoryPatchVector[(size_t)i].second;
        auto line = [&](const char *cfg, const Result &r) {
            std::fprintf(f, "%d|%s|%s|%016llx|%.9g|%.9g\n", i, cfg, name.c_str(), (unsigned long long)r.hash, r.peak,
                         r.n ? std::sqrt(r.sumSq / (double)r.n) : 0.0);
        };
        line("48k/256", render(&host, i, 48000, 256, {}));
        if (i < 0 || i % 12 == 0)
        {
            line("44k1/441", render(&host, i, 44100, 441, {}));
            line("48k/64/morph", render(&host, i, 48000, 64, morphState));
            line("96k/1024", render(&host, i, 96000, 1024, {}));
        }
    }
    std::fclose(f);
    std::printf("render-hash: %d sounds -> %s\n", done, argv[1]);
    return 0;
}
