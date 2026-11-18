/*
 * Ghost Bass - a bass instrument built on Surge XT.
 * Copyright 2026 the Ghost Bass authors. Based on Surge XT by the Surge Synth Team.
 * Released under the GNU General Public Licence v3 or later (see LICENSE).
 */
#include "GhostMorph.h"
#include "SurgeSynthesizer.h"
#include "dsp/utilities/DSPUtils.h"
#include "tinyxml/tinyxml.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace ghost
{
const char *Morph::rateName(int i)
{
    static const char *n[kRates] = {"1/16", "1/8", "1/4", "1/2", "1 BAR", "2 BARS", "4 BARS", "8 BARS", "16 BARS"};
    return n[std::clamp(i, 0, kRates - 1)];
}
const char *Morph::shapeName(int i)
{
    static const char *n[n_shapes] = {"SMOOTH", "TRIANGLE", "JUMP", "RANDOM"};
    return n[std::clamp(i, 0, n_shapes - 1)];
}
const char *Morph::feelName(int i)
{
    static const char *n[n_feels] = {"STRAIGHT", "DOTTED", "TRIPLET"};
    return n[std::clamp(i, 0, n_feels - 1)];
}
double Morph::rateBeats(int rate, int feel)
{
    static const double b[kRates] = {0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0, 64.0};
    const double x = b[std::clamp(rate, 0, kRates - 1)];
    return feel == F_DOTTED ? x * 1.5 : feel == F_TRIPLET ? x * 2.0 / 3.0 : x;
}

void Morph::cornerPos(int c, float &x, float &y)
{
    static const float p[kCorners][2] = {{0.13f, 0.20f}, {0.87f, 0.20f}, {0.13f, 0.80f}, {0.87f, 0.80f}};
    c = std::clamp(c, 0, kCorners - 1);
    x = p[c][0];
    y = p[c][1];
}
void Morph::boardWeights(const bool filled[kCorners], float px, float py, float *out)
{
    float sum = 0.f;
    int best = -1;
    float bestD = 1e9f;
    for (int c = 0; c < kCorners; ++c)
    {
        out[c] = 0.f;
        if (!filled[c])
            continue;
        float x, y;
        cornerPos(c, x, y);
        const float dx = px - x, dy = py - y, d = std::sqrt(dx * dx + dy * dy);
        if (d < bestD)
        {
            bestD = d;
            best = c;
        }
        const float e = std::max(0.f, d - 0.06f); // a plateau around each sound
        out[c] = e < 1e-4f ? 1e8f : 1.f / (e * e);
        sum += out[c];
    }
    if (best < 0)
        return;
    if (!(sum > 0.f) || !std::isfinite(sum))
    {
        out[best] = 1.f;
        return;
    }
    for (int c = 0; c < kCorners; ++c)
        out[c] /= sum;
}

Morph::Morph()
{
    publish(crv, std::make_unique<Curve>(Curve{defaultCurve()}));
}
Morph::~Morph()
{
    delete set.live.exchange(nullptr);
    delete crv.live.exchange(nullptr);
}

template <typename T> void Morph::publish(Slot<T> &sl, std::unique_ptr<T> v)
{
    std::lock_guard<std::mutex> g(mtx);
    reap(sl);
    T *old = sl.live.exchange(v.release(), std::memory_order_acq_rel);
    if (old)
        sl.retired.emplace_back(std::unique_ptr<T>(old), blocks.load(std::memory_order_acquire));
}
template <typename T> void Morph::reap(Slot<T> &sl)
{
    // the audio thread reads the live pointer once per block: anything retired two blocks ago is free
    const uint64_t now = blocks.load(std::memory_order_acquire);
    sl.retired.erase(std::remove_if(sl.retired.begin(), sl.retired.end(), [now](auto &r) { return now > r.second + 2; }),
                     sl.retired.end());
}

Morph::Info Morph::info() const
{
    std::lock_guard<std::mutex> g(mtx);
    auto *s = set.live.load(std::memory_order_acquire);
    return s ? s->info : Info();
}

// ------------------------------------------------------------------------------------------------ DRAW (Surge's MSEG)
MSEGStorage Morph::defaultCurve()
{
    // A -> B -> A over the loop (two sine segments), as an LFO-mode MSEG of one phase unit
    MSEGStorage ms;
    ms.editMode = MSEGStorage::LFO;
    ms.loopMode = MSEGStorage::LOOP;
    ms.endpointMode = MSEGStorage::LOCKED;
    ms.n_activeSegments = 2;
    for (int i = 0; i < 2; ++i)
    {
        auto &s = ms.segments[i];
        s = MSEGStorage::segment();
        s.duration = 0.5f;
        s.type = MSEGStorage::segment::SCURVE; // a smooth S from one end to the other
        s.v0 = i == 0 ? -1.f : 1.f; // (MSEG values run -1 .. 1: A at -1, B at 1)
        s.cpduration = 0.5f;
        s.cpv = 0.f;
    }
    Surge::MSEG::rebuildCache(&ms);
    return ms;
}

MSEGStorage Morph::curveFromDrawing(const std::vector<std::pair<float, float>> &pts)
{
    if (pts.size() < 2)
        return defaultCurve();
    // Surge's own freehand fit (the MSEG editor's draw mode): samples (t, v) -> segments
    std::vector<std::pair<float, float>> s;
    s.reserve(pts.size());
    for (auto &p : pts)
        s.emplace_back(std::clamp(p.first, 0.f, 1.f), std::clamp(p.second, 0.f, 1.f) * 2.f - 1.f); // MSEG values are -1..1
    std::sort(s.begin(), s.end(), [](auto &a, auto &b) { return a.first < b.first; });
    if (s.front().first > 0.f)
        s.insert(s.begin(), {0.f, s.front().second});
    if (s.back().first < 1.f)
        s.emplace_back(1.f, s.back().second);
    std::vector<MSEGStorage::segment> segs;
    Surge::MSEG::freehandRDP(s, 0.02f, 1.f, segs);
    MSEGStorage ms;
    ms.editMode = MSEGStorage::LFO;
    ms.loopMode = MSEGStorage::LOOP;
    ms.endpointMode = MSEGStorage::FREE;
    ms.n_activeSegments = std::min((int)segs.size(), (int)max_msegs);
    if (ms.n_activeSegments == 0)
        return defaultCurve();
    float tot = 0.f;
    for (int i = 0; i < ms.n_activeSegments; ++i)
    {
        ms.segments[i] = segs[(size_t)i];
        tot += ms.segments[i].duration;
    }
    // one phase unit exactly (LFO mode)
    if (tot > 0.f)
        for (int i = 0; i < ms.n_activeSegments; ++i)
            ms.segments[i].duration /= tot;
    // the fit gives each segment its start; the end of the last one is where the drawing ends (as
    // Surge's MSEG editor sets it after its own freehand fit, the endpoint being free)
    ms.segments[ms.n_activeSegments - 1].nv1 = s.back().second;
    Surge::MSEG::rebuildCache(&ms);
    return ms;
}

float Morph::curveAt(const MSEGStorage &ms, double phase01)
{
    MSEGStorage m = ms; // valueAt keeps nothing in the storage it is given, but takes it non-const
    Surge::MSEG::EvaluatorState st;
    const double p = phase01 - std::floor(phase01);
    return std::clamp(0.5f * (Surge::MSEG::valueAt(0, (float)p, 0.f, &m, &st) + 1.f), 0.f, 1.f);
}

void Morph::setCurve(const MSEGStorage &ms) { publish(crv, std::make_unique<Curve>(Curve{ms})); }
MSEGStorage Morph::curve() const
{
    auto *c = crv.live.load(std::memory_order_acquire);
    return c ? c->ms : defaultCurve();
}

// ------------------------------------------------------------------------------------------------ patch
namespace
{
std::string curveToString(const MSEGStorage &ms)
{
    std::ostringstream o;
    o.precision(6);
    for (int i = 0; i < ms.n_activeSegments; ++i)
    {
        auto &s = ms.segments[i];
        o << (i ? ";" : "") << s.duration << "," << s.v0 << "," << (int)s.type << "," << s.cpv << "," << s.cpduration;
    }
    if (ms.n_activeSegments > 0)
        o << ";end," << ms.segments[ms.n_activeSegments - 1].nv1;
    return o.str();
}
bool curveFromString(const std::string &str, MSEGStorage &ms)
{
    MSEGStorage m;
    m.editMode = MSEGStorage::LFO;
    m.loopMode = MSEGStorage::LOOP;
    m.endpointMode = MSEGStorage::FREE;
    std::istringstream in(str);
    std::string seg;
    int n = 0;
    float endV = 0.f;
    bool haveEnd = false;
    while (std::getline(in, seg, ';') && n < max_msegs)
    {
        if (seg.rfind("end,", 0) == 0)
        {
            endV = (float)std::atof(seg.c_str() + 4);
            haveEnd = true;
            continue;
        }
        float d, v0, cpv, cpd;
        int t;
        if (std::sscanf(seg.c_str(), "%f,%f,%d,%f,%f", &d, &v0, &t, &cpv, &cpd) != 5)
            return false;
        auto &s = m.segments[n++];
        s = MSEGStorage::segment();
        s.duration = std::max(1e-4f, d);
        s.v0 = std::clamp(v0, -1.f, 1.f);
        s.type = (MSEGStorage::segment::Type)std::clamp(t, 1, (int)MSEGStorage::segment::RATCHET_8);
        s.cpv = cpv;
        s.cpduration = cpd;
    }
    if (n == 0)
        return false;
    m.n_activeSegments = n;
    m.segments[n - 1].nv1 = haveEnd ? std::clamp(endV, -1.f, 1.f) : m.segments[0].v0;
    Surge::MSEG::rebuildCache(&m);
    ms = m;
    return true;
}
} // namespace

void Morph::onPatchSave(TiXmlElement &gb) const
{
    Info inf = info();
    if (inf.mode == M_OFF)
        return;
    TiXmlElement m("morph");
    m.SetAttribute("mode", inf.mode);
    m.SetAttribute("a", inf.pathA);
    m.SetAttribute("an", inf.nameA);
    m.SetAttribute("b", inf.pathB);
    m.SetAttribute("bn", inf.nameB);
    m.SetDoubleAttribute("blend", blend.load());
    m.SetAttribute("pend", pendOn.load() ? 1 : 0);
    m.SetAttribute("rate", pendRate.load());
    m.SetAttribute("feel", pendFeel.load());
    m.SetAttribute("shape", pendShape.load());
    m.SetAttribute("free", pendFree.load() ? 1 : 0);
    m.SetDoubleAttribute("hz", pendHz.load());
    m.SetDoubleAttribute("swing", swing.load());
    m.SetAttribute("draw", drawOn.load() ? 1 : 0);
    m.SetAttribute("bars", drawBars.load());
    m.SetAttribute("curve", curveToString(curve()));
    if (inf.board)
    {
        m.SetAttribute("board", 1);
        m.SetDoubleAttribute("px", puckX.load());
        m.SetDoubleAttribute("py", puckY.load());
        std::string cs;
        for (int k = 0; k < kCorners; ++k)
            if (inf.filled[k])
            {
                cs += (cs.empty() ? "" : ",") + std::to_string(k);
                m.SetAttribute("c" + std::to_string(k) + "p", inf.cpath[k]);
                m.SetAttribute("c" + std::to_string(k) + "n", inf.cname[k]);
            }
        m.SetAttribute("corners", cs);
    }
    if (inf.mode == M_BOARD)
    {
        m.SetAttribute("bvals", inf.boardVals);
        m.SetAttribute("bmac", inf.boardMacros);
    }
    if (inf.mode == M_PARAMS)
    {
        std::ostringstream o;
        o.precision(9);
        for (size_t i = 0; i < inf.bValues.size(); ++i)
            o << (i ? ";" : "") << inf.bValues[i].first << "=" << inf.bValues[i].second;
        m.SetAttribute("bvals", o.str());
        if (inf.haveMacroB)
        {
            std::ostringstream mo;
            for (int k = 0; k < 8; ++k)
                mo << (k ? ";" : "") << inf.macroB[k];
            m.SetAttribute("bmacros", mo.str());
        }
    }
    gb.InsertEndChild(m);
}

void Morph::onPatchLoad(SurgeSynthesizer *s, TiXmlElement *m)
{
    if (!m)
    {
        publish(set, std::unique_ptr<Set>());
        modeNow.store(M_OFF);
        return;
    }
    auto ns = std::make_unique<Set>();
    auto &inf = ns->info;
    int i = 0;
    double d = 0;
    if (m->QueryIntAttribute("mode", &i) == TIXML_SUCCESS)
        inf.mode = std::clamp(i, 0, 3);
    auto str = [m](const char *a) { return m->Attribute(a) ? std::string(m->Attribute(a)) : std::string(); };
    inf.pathA = str("a");
    inf.nameA = str("an");
    inf.pathB = str("b");
    inf.nameB = str("bn");
    if (m->QueryDoubleAttribute("blend", &d) == TIXML_SUCCESS)
        blend.store(std::clamp((float)d, 0.f, 1.f));
    if (m->QueryIntAttribute("board", &i) == TIXML_SUCCESS && i)
    {
        inf.board = true;
        if (m->QueryDoubleAttribute("px", &d) == TIXML_SUCCESS)
            inf.px = std::clamp((float)d, 0.f, 1.f);
        if (m->QueryDoubleAttribute("py", &d) == TIXML_SUCCESS)
            inf.py = std::clamp((float)d, 0.f, 1.f);
        std::istringstream cin(str("corners"));
        std::string tok;
        while (std::getline(cin, tok, ','))
        {
            const int k = std::atoi(tok.c_str());
            if (k < 0 || k >= kCorners)
                continue;
            inf.filled[k] = true;
            inf.cpath[k] = str(("c" + std::to_string(k) + "p").c_str());
            inf.cname[k] = str(("c" + std::to_string(k) + "n").c_str());
        }
        puckX.store(inf.px);
        puckY.store(inf.py);
        float w[kCorners];
        boardWeights(inf.filled, inf.px, inf.py, w);
        for (int k = 0; k < kCorners; ++k)
            bw[k].store(w[k]);
        if (inf.mode != M_BOARD)
        {
            // two sounds: BLEND is the second one's weight (the first filled corner is A, the second is B)
            int b = -1;
            for (int k = kCorners - 1; k >= 0; --k)
                if (inf.filled[k])
                {
                    b = k;
                    break;
                }
            if (b >= 0)
                blend.store(w[b]);
        }
    }
    if (m->QueryIntAttribute("pend", &i) == TIXML_SUCCESS)
        pendOn.store(i != 0);
    if (m->QueryIntAttribute("rate", &i) == TIXML_SUCCESS)
        pendRate.store(std::clamp(i, 0, kRates - 1));
    if (m->QueryIntAttribute("feel", &i) == TIXML_SUCCESS)
        pendFeel.store(std::clamp(i, 0, n_feels - 1));
    if (m->QueryIntAttribute("shape", &i) == TIXML_SUCCESS)
        pendShape.store(std::clamp(i, 0, n_shapes - 1));
    if (m->QueryIntAttribute("free", &i) == TIXML_SUCCESS)
        pendFree.store(i != 0);
    if (m->QueryDoubleAttribute("hz", &d) == TIXML_SUCCESS)
        pendHz.store(std::clamp((float)d, 0.01f, 20.f));
    if (m->QueryDoubleAttribute("swing", &d) == TIXML_SUCCESS)
        swing.store(std::clamp((float)d, 0.f, 1.f));
    if (m->QueryIntAttribute("draw", &i) == TIXML_SUCCESS)
        drawOn.store(i != 0);
    if (m->QueryIntAttribute("bars", &i) == TIXML_SUCCESS)
        drawBars.store(std::clamp(i, 1, 8));
    MSEGStorage ms;
    if (curveFromString(str("curve"), ms))
        setCurve(ms);
    auto &patch = s->storage.getPatch();
    auto &pp = patch.param_ptr;
    auto idOf = [&pp](const std::string &n) {
        for (int k = 0; k < (int)pp.size(); ++k)
            if (n == pp[k]->get_storage_name())
                return k;
        return -1;
    };
    for (int k = 0; k < 8; ++k)
        ns->macroA[k] = ns->macroB[k] = s->getMacroParameterTarget01(k);
    if (inf.mode == M_PARAMS)
    {
        std::istringstream in(str("bvals"));
        std::string kv;
        while (std::getline(in, kv, ';'))
        {
            auto eq = kv.find('=');
            if (eq == std::string::npos)
                continue;
            const std::string name = kv.substr(0, eq);
            const float raw = (float)std::atof(kv.c_str() + eq + 1);
            const int id = idOf(name);
            if (id < 0 || pp[id]->valtype != vt_float)
                continue;
            inf.bValues.emplace_back(name, raw);
            Parameter tmp = *pp[id];
            tmp.val.f = raw;
            Set::P q;
            q.id = id;
            q.a = pp[id]->get_value_f01();
            q.b = tmp.get_value_f01();
            ns->params.push_back(q);
        }
        std::istringstream mi(str("bmacros"));
        std::string v;
        int k = 0;
        while (std::getline(mi, v, ';') && k < 8)
        {
            inf.macroB[k] = std::clamp((float)std::atof(v.c_str()), 0.f, 1.f);
            ns->macroB[k] = inf.macroB[k];
            ++k;
        }
        inf.haveMacroB = k == 8;
        ns->macros = inf.haveMacroB;
    }
    else if (inf.mode == M_BOARD)
    {
        inf.boardVals = str("bvals");
        inf.boardMacros = str("bmac");
        std::istringstream in(inf.boardVals);
        std::string kv;
        while (std::getline(in, kv, ';'))
        {
            auto eq = kv.find('=');
            if (eq == std::string::npos)
                continue;
            const std::string name = kv.substr(0, eq);
            const int id = idOf(name);
            if (id < 0 || pp[id]->valtype != vt_float)
                continue;
            Set::PB q;
            q.id = id;
            std::istringstream vs(kv.substr(eq + 1));
            std::string x;
            int k = 0;
            while (std::getline(vs, x, ',') && k < kCorners)
            {
                Parameter tmp = *pp[id];
                tmp.val.f = (float)std::atof(x.c_str());
                q.v[k++] = tmp.get_value_f01();
            }
            if (k != kCorners)
                continue;
            ns->bparams.push_back(q);
        }
        std::istringstream mi(inf.boardMacros);
        std::string mv;
        int mk = 0;
        while (std::getline(mi, mv, ';') && mk < 8)
        {
            std::istringstream vs(mv);
            std::string x;
            int k = 0;
            while (std::getline(vs, x, ',') && k < kCorners)
                ns->macroC[k++][mk] = std::clamp((float)std::atof(x.c_str()), 0.f, 1.f);
            ++mk;
        }
        ns->bmacros = mk == 8;
        for (int k = 0; k < kCorners; ++k)
            inf.nBoard += inf.filled[k] ? 1 : 0;
    }
    else if (inf.mode == M_SCENES)
    {
        ns->volA = idOf("a_volume");
        ns->volB = idOf("b_volume");
        if (ns->volA >= 0)
            ns->volA0 = pp[ns->volA]->get_value_f01();
        if (ns->volB >= 0)
            ns->volB0 = pp[ns->volB]->get_value_f01();
    }
    ns->mode = inf.mode;
    swInit = false;
    modeNow.store(inf.mode);
    publish(set, std::move(ns));
}

// ------------------------------------------------------------------------------------------------ audio thread
void Morph::preBlock(SurgeSynthesizer *s, const float *applied, int nApplied)
{
    blocks.fetch_add(1, std::memory_order_acq_rel);
    Set *st = set.live.load(std::memory_order_acquire);
    if (!st || st->mode == M_OFF)
        return;
    auto &storage = s->storage;
    const double dt = (double)BLOCK_SIZE / std::max(1000.f, storage.samplerate);
    if (st->mode == M_BOARD)
    {
        // BLEND BOARD: glide the four weights (35 ms one-pole), then every shared control = the weighted sum of the sounds' values
        const float c = (float)(1.0 - std::exp(-dt / 0.035));
        float tgt[kCorners], total = 0.f;
        for (int k = 0; k < kCorners; ++k)
        {
            tgt[k] = std::clamp(bw[k].load(std::memory_order_relaxed), 0.f, 1.f);
            total += tgt[k];
        }
        for (int k = 0; k < kCorners; ++k)
        {
            if (!swInit)
                sw[k] = tgt[k];
            sw[k] += (tgt[k] - sw[k]) * c;
            if (std::fabs(tgt[k] - sw[k]) < 1e-6f)
                sw[k] = tgt[k];
            nowW[k].store(sw[k], std::memory_order_relaxed);
        }
        swInit = true;
        (void)total;
        bool moved = false;
        for (int k = 0; k < kCorners; ++k)
            moved = moved || std::fabs(sw[k] - st->lastW[k]) >= 1e-6f;
        if (!moved)
            return;
        for (int k = 0; k < kCorners; ++k)
            st->lastW[k] = sw[k];
        auto &ppb = storage.getPatch().param_ptr;
        for (auto &q : st->bparams)
        {
            float v = 0.f;
            for (int k = 0; k < kCorners; ++k)
                v += sw[k] * q.v[k];
            if (std::fabs(v - q.last) < 1e-7f)
                continue;
            q.last = v;
            const float off = q.id < nApplied ? applied[q.id] : 0.f;
            ppb[q.id]->set_value_f01(std::clamp(v + off, 0.f, 1.f));
        }
        if (st->bmacros)
            for (int mk = 0; mk < 8; ++mk)
            {
                float v = 0.f;
                for (int k = 0; k < kCorners; ++k)
                    v += sw[k] * st->macroC[k][mk];
                s->setMacroParameter01(mk, v);
            }
        return;
    }
    const double bps = 2.0 * std::max(0.05f, storage.temposyncratio); // beats per second (temposyncratio 1 = 120 bpm)
    float target = std::clamp(blend.load(std::memory_order_relaxed), 0.f, 1.f);
    const bool drawing = drawOn.load(std::memory_order_relaxed), pend = pendOn.load(std::memory_order_relaxed);
    // build 8: the blend glides at the sculpt's own speed (6-8 ms clicked on low notes in the click test)
    float slewMs = 35.f;
    if (drawing || pend)
    {
        // the swing's phase: the DAW's song position when it plays, else free-running
        double cycleBeats = drawing ? 4.0 * std::clamp(drawBars.load(std::memory_order_relaxed), 1, 8)
                                    : rateBeats(pendRate.load(std::memory_order_relaxed), pendFeel.load(std::memory_order_relaxed));
        double ph;
        const bool free = !drawing && pendFree.load(std::memory_order_relaxed);
        if (free)
        {
            freePhase += dt * std::clamp(pendHz.load(std::memory_order_relaxed), 0.01f, 20.f);
            freePhase -= std::floor(freePhase);
            ph = freePhase;
        }
        else if (storage.isPlaying)
        {
            ph = storage.songpos / cycleBeats;
            freePhase = ph - std::floor(ph);
        }
        else
        {
            freePhase += dt * bps / cycleBeats;
            ph = freePhase;
        }
        const int cycle = (int)std::floor(ph);
        const float p = (float)(ph - std::floor(ph));
        float v = 0.f;
        if (drawing)
        {
            if (auto *c = crv.live.load(std::memory_order_acquire))
                v = std::clamp(0.5f * (Surge::MSEG::valueAt(0, p, 0.f, &c->ms, &eval) + 1.f), 0.f, 1.f);
        }
        else
        {
            switch (pendShape.load(std::memory_order_relaxed))
            {
            case SH_SMOOTH:
                v = 0.5f - 0.5f * std::cos(2.f * (float)M_PI * p);
                break;
            case SH_TRIANGLE:
                v = p < 0.5f ? 2.f * p : 2.f - 2.f * p;
                break;
            case SH_JUMP:
                v = p < 0.5f ? 0.f : 1.f;
                slewMs = 40.f;
                break;
            default: // RANDOM: a new place every half cycle
            {
                const int half = (int)std::floor(ph * 2.0);
                if (half != lastCycle)
                {
                    lastCycle = half;
                    rng = rng * 1664525u + 1013904223u;
                    rndTo = (float)(rng >> 8) / 16777216.f;
                }
                v = rndTo;
                slewMs = 40.f;
            }
            }
        }
        (void)cycle;
        const float sw = std::clamp(swing.load(std::memory_order_relaxed), 0.f, 1.f);
        target = (1.f - sw) * target + sw * v;
        nowPhase.store(p, std::memory_order_relaxed);
    }
    // one-pole on the blend: nothing jumps
    const float c = (float)(1.0 - std::exp(-dt / (slewMs * 0.001)));
    if (!smInit)
    {
        sm = target;
        smInit = true;
    }
    sm += (target - sm) * c;
    if (std::fabs(target - sm) < 1e-6f)
        sm = target;
    nowBlend.store(sm, std::memory_order_relaxed);
    if (std::fabs(sm - st->lastBlend) < 1e-6f)
        return;
    st->lastBlend = sm;
    auto &pp = storage.getPatch().param_ptr;
    if (st->mode == M_PARAMS)
    {
        for (auto &q : st->params)
        {
            const float v = q.a + (q.b - q.a) * sm;
            if (std::fabs(v - q.last) < 1e-7f)
                continue;
            q.last = v;
            // keep the sculpt's offset on top (the sculpt then reads this as its new base)
            const float off = q.id < nApplied ? applied[q.id] : 0.f;
            pp[q.id]->set_value_f01(std::clamp(v + off, 0.f, 1.f));
        }
        if (st->macros)
            for (int k = 0; k < 8; ++k)
                if (st->macroA[k] != st->macroB[k])
                    s->setMacroParameter01(k, st->macroA[k] + (st->macroB[k] - st->macroA[k]) * sm);
    }
    else if (st->mode == M_SCENES)
    {
        // equal power: the two scenes' own volumes, in Surge's amplitude units
        const float ga = std::cos(sm * 0.5f * (float)M_PI), gb = std::sin(sm * 0.5f * (float)M_PI);
        if (st->volA >= 0)
            pp[st->volA]->set_value_f01(linear_to_amp(ga * amp_to_linear(st->volA0)));
        if (st->volB >= 0)
            pp[st->volB]->set_value_f01(linear_to_amp(gb * amp_to_linear(st->volB0)));
    }
}
} // namespace ghost
