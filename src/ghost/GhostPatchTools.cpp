/*
 * Ghost Bass - a bass instrument built on Surge XT.
 * Copyright 2026 the Ghost Bass authors. Based on Surge XT by the Surge Synth Team.
 * Released under the GNU General Public Licence v3 or later (see LICENSE).
 */
#include "GhostPatchTools.h"
#include "GhostEngine.h"
#include "SurgeSynthesizer.h"
#include "PatchFileHeaderStructs.h"
#include "tinyxml/tinyxml.h"
#include "sst/basic-blocks/mechanics/endian-ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <random>
#include <set>

namespace ghost
{
namespace dna
{
namespace mech = sst::basic_blocks::mechanics;
using sst::io::fxChunkSetCustom;
using sst::io::patch_header;

namespace
{
struct Parts
{
    std::string xml;
    Blob wt[2][3]; // whole wavetable chunk (wt_header + data) per scene/osc, empty if none
    Blob tail;     // arbitrary block storage after the wavetables
    bool ok{false};
};

Parts split(const Blob &b)
{
    Parts p;
    if (b.size() < sizeof(patch_header) || memcmp(b.data(), "sub3", 4))
        return p;
    patch_header h;
    memcpy(&h, b.data(), sizeof(h));
    size_t xs = mech::endian_read_int32LE(h.xmlsize);
    size_t pos = sizeof(patch_header);
    if (pos + xs > b.size())
        return p;
    p.xml.assign((const char *)b.data() + pos, xs);
    pos += xs;
    for (int sc = 0; sc < 2; ++sc)
        for (int o = 0; o < 3; ++o)
        {
            size_t w = mech::endian_read_int32LE(h.wtsize[sc][o]);
            if (w)
            {
                if (pos + w > b.size())
                    return p;
                p.wt[sc][o].assign(b.begin() + pos, b.begin() + pos + w);
                pos += w;
            }
        }
    p.tail.assign(b.begin() + pos, b.end());
    p.ok = true;
    return p;
}

Blob join(const Parts &p)
{
    patch_header h;
    memcpy(h.tag, "sub3", 4);
    h.xmlsize = mech::endian_write_int32LE((uint32_t)p.xml.size());
    size_t tot = sizeof(h) + p.xml.size() + p.tail.size();
    for (int sc = 0; sc < 2; ++sc)
        for (int o = 0; o < 3; ++o)
        {
            h.wtsize[sc][o] = mech::endian_write_int32LE((uint32_t)p.wt[sc][o].size());
            tot += p.wt[sc][o].size();
        }
    Blob b;
    b.reserve(tot);
    b.insert(b.end(), (uint8_t *)&h, (uint8_t *)&h + sizeof(h));
    b.insert(b.end(), p.xml.begin(), p.xml.end());
    for (int sc = 0; sc < 2; ++sc)
        for (int o = 0; o < 3; ++o)
            b.insert(b.end(), p.wt[sc][o].begin(), p.wt[sc][o].end());
    b.insert(b.end(), p.tail.begin(), p.tail.end());
    return b;
}

std::string toString(TiXmlDocument &d)
{
    std::string s;
    s << d;
    return s;
}

// module a parameter belongs to: "a_osc1", "a_filter2", "a_lfo3", "fx4", "global", ...
std::string groupOf(const std::string &n)
{
    if (n.size() > 2 && (n[0] == 'a' || n[0] == 'b') && n[1] == '_')
    {
        auto u = n.find('_', 2);
        auto tok = n.substr(2, u == std::string::npos ? std::string::npos : u - 2);
        // fold per-osc mixer controls into their oscillator ("a_level_o2" -> "a_osc2")
        if (tok == "level" && u != std::string::npos)
        {
            auto rest = n.substr(u + 1);
            if (rest.size() == 2 && rest[0] == 'o' && rest[1] >= '1' && rest[1] <= '3')
                return n.substr(0, 2) + "osc" + rest.substr(1);
            if (rest.rfind("o", 0) == 0 && rest.size() == 2)
                return n.substr(0, 2) + "osc" + rest.substr(1);
        }
        if (tok == "mute" || tok == "route")
            return n.substr(0, 2) + "mixer";
        return n.substr(0, 2) + tok;
    }
    auto u = n.find('_');
    auto tok = n.substr(0, u);
    if (tok.size() >= 3 && tok.rfind("fx", 0) == 0)
        return tok;
    return "global";
}

bool isIntType(TiXmlElement *e)
{
    int t = 2;
    e->QueryIntAttribute("type", &t);
    return t != 2;
}

Parameter *paramByName(SurgeSynthesizer *s, const std::string &n)
{
    for (auto *p : s->storage.getPatch().param_ptr)
        if (n == p->get_storage_name())
            return p;
    return nullptr;
}
} // namespace

Blob capture(SurgeSynthesizer *s)
{
    void *d = nullptr;
    auto sz = s->storage.getPatch().save_patch(&d);
    // save_patch keeps ownership (patchptr); copy out
    return Blob((uint8_t *)d, (uint8_t *)d + sz);
}

bool readFxp(const std::string &path, Blob &out)
{
    std::ifstream f(string_to_path(path), std::ios::binary);
    if (!f)
        return false;
    Blob all((std::istreambuf_iterator<char>(f)), {});
    if (all.size() < sizeof(fxChunkSetCustom) + sizeof(patch_header))
        return false;
    fxChunkSetCustom fx;
    memcpy(&fx, all.data(), sizeof(fx));
    if (mech::endian_read_int32BE(fx.chunkMagic) != 'CcnK' ||
        mech::endian_read_int32BE(fx.fxMagic) != 'FPCh')
        return false;
    out.assign(all.begin() + sizeof(fxChunkSetCustom), all.end());
    return true;
}

bool writeFxp(const std::string &path, const Blob &b, const std::string &name)
{
    std::ofstream f(string_to_path(path), std::ios::out | std::ios::binary);
    if (!f)
        return false;
    fxChunkSetCustom fxp;
    memset(&fxp, 0, sizeof(fxp));
    fxp.chunkMagic = mech::endian_write_int32BE('CcnK');
    fxp.fxMagic = mech::endian_write_int32BE('FPCh');
    fxp.fxID = mech::endian_write_int32BE('cjs3');
    fxp.numPrograms = mech::endian_write_int32BE(1);
    fxp.version = mech::endian_write_int32BE(1);
    fxp.fxVersion = mech::endian_write_int32BE(1);
    strncpy(fxp.prgName, name.c_str(), 27);
    fxp.chunkSize = mech::endian_write_int32BE((int)b.size());
    fxp.byteSize = 0;
    f.write((const char *)&fxp, sizeof(fxp));
    f.write((const char *)b.data(), b.size());
    return (bool)f;
}

std::string patchName(const Blob &b)
{
    auto p = split(b);
    if (!p.ok)
        return {};
    TiXmlDocument d;
    d.Parse(p.xml.c_str());
    auto *patch = d.FirstChildElement("patch");
    auto *meta = patch ? patch->FirstChildElement("meta") : nullptr;
    return meta && meta->Attribute("name") ? meta->Attribute("name") : "";
}

static void setMeta(TiXmlElement *patch, const std::string &name, const char *category)
{
    if (!patch)
        return;
    auto *meta = patch->FirstChildElement("meta");
    if (!meta)
        return;
    if (!name.empty())
        meta->SetAttribute("name", name);
    if (category)
        meta->SetAttribute("category", category);
}

bool queueLoad(SurgeSynthesizer *s, const Blob &b, const std::string &name)
{
    preloadSamples(s, b.data(), (int)b.size());
    static int counter = 0;
    auto dir = s->storage.userDataPath / "Ghost Bass Temp";
    std::error_code ec;
    fs::create_directories(dir, ec);
    // (the file name carries this synth's address: two instances in one process never share a temp file)
    auto f = dir / ("gb-" + std::to_string((uintptr_t)s % 100000007u) + "-" + std::to_string((counter++) % 8) + ".fxp");
    if (!writeFxp(path_to_string(f), b, name))
        return false;
    {
        std::lock_guard<std::mutex> mg(s->patchLoadSpawnMutex);
        strncpy(s->patchid_file, path_to_string(f).c_str(), FILENAME_MAX - 1);
        s->patchid_file_isPreset = false; // keep the name/category we wrote
        s->has_patchid_file = true;
    }
    s->processAudioThreadOpsWhenAudioEngineUnavailable();
    return true;
}

void preloadSamples(SurgeSynthesizer *, const void *, int) {} // (build 8: no sample oscillators)

// ------------------------------------------------------------------------------------------------ MORPH
namespace
{
TiXmlElement *paramsOf(TiXmlDocument &d)
{
    auto *patch = d.FirstChildElement("patch");
    return patch ? patch->FirstChildElement("parameters") : nullptr;
}
std::string routingsOf(TiXmlElement *e)
{
    std::string r;
    for (auto *m = e->FirstChildElement("modrouting"); m; m = m->NextSiblingElement("modrouting"))
    {
        r += m->Attribute("source") ? m->Attribute("source") : "";
        r += "/";
        r += m->Attribute("source_index") ? m->Attribute("source_index") : "0";
        r += "/";
        r += m->Attribute("source_scene") ? m->Attribute("source_scene") : "";
        r += ";";
    }
    return r;
}
std::string sectionText(TiXmlDocument &d, const char *name, int scene)
{
    auto *patch = d.FirstChildElement("patch");
    auto *sec = patch ? patch->FirstChildElement(name) : nullptr;
    std::string r;
    if (!sec)
        return r;
    for (auto *c = sec->FirstChildElement(); c; c = c->NextSiblingElement())
    {
        int sc = 0;
        c->QueryIntAttribute("scene", &sc);
        if (scene >= 0 && sc != scene)
            continue;
        std::string t;
        t << *c;
        r += t;
    }
    return r;
}
int sceneModeOf(TiXmlElement *params)
{
    int v = 0;
    if (auto *e = params ? params->FirstChildElement("scenemode") : nullptr)
        e->QueryIntAttribute("value", &v);
    return v;
}
// fx slot numbers (fx1 .. fx16) Surge stores scene A's / B's inserts in
const int kFxA[4] = {1, 2, 9, 10}, kFxB[4] = {3, 4, 11, 12};
} // namespace

Blob morphPatch(SurgeSynthesizer *s, const Blob &a, const Blob &b, const std::string &pathA, const std::string &nameA,
                const std::string &pathB, const std::string &nameB, int &mode, std::string &why,
                const std::vector<std::pair<std::string, std::string>> &extra)
{
    mode = 0;
    auto pa = split(a), pb = split(b);
    if (!pa.ok || !pb.ok)
    {
        why = "one of the two sounds could not be read";
        return {};
    }
    TiXmlDocument da, db;
    da.Parse(pa.xml.c_str());
    db.Parse(pb.xml.c_str());
    auto *xa = paramsOf(da), *xb = paramsOf(db);
    if (!xa || !xb)
    {
        why = "one of the two sounds could not be read";
        return {};
    }
    // 1. do they share their structure?
    bool same = true;
    std::string firstDiff;
    std::vector<std::pair<std::string, float>> bvals;
    for (auto *ea = xa->FirstChildElement(); ea; ea = ea->NextSiblingElement())
    {
        auto *eb = xb->FirstChildElement(ea->Value());
        if (!eb)
            continue;
        if (isIntType(ea) || isIntType(eb))
        {
            int va = 0, vb = 0;
            ea->QueryIntAttribute("value", &va);
            eb->QueryIntAttribute("value", &vb);
            if (va != vb && same)
            {
                same = false;
                firstDiff = ea->Value();
            }
        }
        else
        {
            double va = 0, vb = 0;
            ea->QueryDoubleAttribute("value", &va);
            eb->QueryDoubleAttribute("value", &vb);
            if (std::fabs(va - vb) > 1e-7)
                bvals.emplace_back(ea->Value(), (float)vb);
        }
        if (same && routingsOf(ea) != routingsOf(eb))
        {
            same = false;
            firstDiff = std::string(ea->Value()) + " modulation";
        }
    }
    for (int sc = 0; sc < 2 && same; ++sc)
        for (int o = 0; o < 3 && same; ++o)
            if (pa.wt[sc][o] != pb.wt[sc][o])
            {
                same = false;
                firstDiff = "a wavetable";
            }
    for (const char *sec : {"stepsequences", "msegs", "formulae", "extraoscdata"})
        if (same && sectionText(da, sec, -1) != sectionText(db, sec, -1))
        {
            same = false;
            firstDiff = sec;
        }
    auto *patchA = da.FirstChildElement("patch");
    auto *gbA = patchA ? patchA->FirstChildElement("ghostbass") : nullptr;
    if (!gbA && patchA)
    {
        TiXmlElement g("ghostbass");
        g.SetAttribute("version", 1);
        patchA->InsertEndChild(g);
        gbA = patchA->FirstChildElement("ghostbass");
    }
    // (an older morph inside A is replaced)
    if (gbA)
        while (auto *old = gbA->FirstChildElement("morph"))
            gbA->RemoveChild(old);
    TiXmlElement mo("morph");
    mo.SetAttribute("a", pathA);
    mo.SetAttribute("an", nameA);
    mo.SetAttribute("b", pathB);
    mo.SetAttribute("bn", nameB);
    mo.SetDoubleAttribute("blend", 0.0);
    for (auto &kv : extra)
        mo.SetAttribute(kv.first, kv.second);
    if (same)
    {
        mode = 1;
        std::string bv;
        char buf[64];
        for (size_t i = 0; i < bvals.size(); ++i)
        {
            std::snprintf(buf, sizeof(buf), "%.9g", bvals[i].second);
            bv += (i ? ";" : "") + bvals[i].first + "=" + buf;
        }
        mo.SetAttribute("mode", 1);
        mo.SetAttribute("bvals", bv);
        // the macros glide too
        auto *ccA = patchA->FirstChildElement("customcontroller");
        auto *patchB = db.FirstChildElement("patch");
        auto *ccB = patchB ? patchB->FirstChildElement("customcontroller") : nullptr;
        if (ccA && ccB)
        {
            std::string mv;
            for (int k = 0; k < 8; ++k)
            {
                double v = 0;
                for (auto *e = ccB->FirstChildElement("entry"); e; e = e->NextSiblingElement("entry"))
                {
                    int i = -1;
                    e->QueryIntAttribute("i", &i);
                    if (i == k)
                        e->QueryDoubleAttribute("v", &v);
                }
                std::snprintf(buf, sizeof(buf), "%.6g", v);
                mv += (k ? ";" : "") + std::string(buf);
            }
            mo.SetAttribute("bmacros", mv);
        }
        why = "TRUE MORPH: the two sounds are built the same way, so every one of their " + std::to_string(bvals.size()) +
              " differing controls glides from A to B";
        gbA->InsertEndChild(mo);
        setMeta(patchA, nameA + " > " + nameB, nullptr);
        Parts out = pa;
        out.xml = toString(da);
        return join(out);
    }
    // 2. SCENE BLEND: A in scene A, B's scene A as scene B, Surge's Dual mode
    if (sceneModeOf(xa) != 0 || sceneModeOf(xb) != 0)
    {
        why = "these two sounds are built differently and one of them already plays both of Surge's scenes: pick another pair";
        return {};
    }
    mode = 2;
    std::vector<TiXmlElement *> drop;
    for (auto *e = xa->FirstChildElement(); e; e = e->NextSiblingElement())
    {
        const std::string n = e->Value();
        bool fxB = false;
        for (int k : kFxB)
            fxB = fxB || n.rfind("fx" + std::to_string(k) + "_", 0) == 0;
        if (n.rfind("b_", 0) == 0 || fxB)
            drop.push_back(e);
    }
    for (auto *e : drop)
        xa->RemoveChild(e);
    for (auto *e = xb->FirstChildElement(); e; e = e->NextSiblingElement())
    {
        const std::string n = e->Value();
        std::string to;
        if (n.rfind("a_", 0) == 0)
            to = "b_" + n.substr(2);
        for (int k = 0; k < 4 && to.empty(); ++k)
        {
            const std::string pre = "fx" + std::to_string(kFxA[k]) + "_";
            if (n.rfind(pre, 0) == 0)
                to = "fx" + std::to_string(kFxB[k]) + "_" + n.substr(pre.size());
        }
        if (to.empty())
            continue;
        TiXmlElement c(*e);
        c.SetValue(to);
        xa->InsertEndChild(c);
    }
    // the FX on / off bits of B's scene inserts follow them
    {
        int ma = 0, mb = 0;
        auto *fa = xa->FirstChildElement("fx_disable");
        auto *fb = xb->FirstChildElement("fx_disable");
        if (fa)
            fa->QueryIntAttribute("value", &ma);
        if (fb)
            fb->QueryIntAttribute("value", &mb);
        for (int k = 0; k < 4; ++k)
        {
            const int from = kFxA[k] - 1, to = kFxB[k] - 1;
            ma = (ma & ~(1 << to)) | (((mb >> from) & 1) << to);
        }
        if (fa)
            fa->SetAttribute("value", ma);
    }
    if (auto *sm = xa->FirstChildElement("scenemode"))
        sm->SetAttribute("value", 2); // Dual: every note plays both scenes
    // per-scene sections: A keeps its scene 0 entries, B's scene 0 entries become scene 1
    auto *patchB = db.FirstChildElement("patch");
    for (const char *sec : {"stepsequences", "msegs", "formulae", "extralfo", "extraoscdata"})
    {
        auto *sa = patchA->FirstChildElement(sec);
        auto *sb = patchB ? patchB->FirstChildElement(sec) : nullptr;
        if (!sa)
            continue;
        std::vector<TiXmlElement *> rm;
        for (auto *c = sa->FirstChildElement(); c; c = c->NextSiblingElement())
        {
            int sc = 0;
            c->QueryIntAttribute("scene", &sc);
            if (sc == 1)
                rm.push_back(c);
        }
        for (auto *c : rm)
            sa->RemoveChild(c);
        if (!sb)
            continue;
        for (auto *c = sb->FirstChildElement(); c; c = c->NextSiblingElement())
        {
            int sc = 0;
            c->QueryIntAttribute("scene", &sc);
            if (sc != 0)
                continue;
            TiXmlElement cp(*c);
            cp.SetAttribute("scene", 1);
            std::string v = cp.Value();
            auto at = v.find("_sc0_");
            if (at != std::string::npos)
                cp.SetValue(v.substr(0, at) + "_sc1_" + v.substr(at + 5));
            sa->InsertEndChild(cp);
        }
    }
    Parts out = pa;
    for (int o = 0; o < 3; ++o)
        out.wt[1][o] = pb.wt[0][o];
    mo.SetAttribute("mode", 2);
    why = "SCENE BLEND: the two sounds are built differently (" + firstDiff +
          "), so A plays in Surge's scene A and B in scene B, and BLEND crossfades them";
    gbA->InsertEndChild(mo);
    setMeta(patchA, nameA + " > " + nameB, nullptr);
    out.xml = toString(da);
    return join(out);
}


Blob boardPatch(SurgeSynthesizer *s, const std::vector<BoardSlot> &slots, float px, float py, int &mode, std::string &why)
{
    mode = 0;
    if (slots.size() < 2 || slots.size() > Morph::kCorners)
    {
        why = "pick at least two sounds for the board";
        return {};
    }
    bool filled[Morph::kCorners]{};
    for (auto &sl : slots)
        if (sl.corner >= 0 && sl.corner < Morph::kCorners)
            filled[sl.corner] = true;
    float w[Morph::kCorners];
    Morph::boardWeights(filled, px, py, w);
    auto fmt = [](float v) {
        char b[64];
        std::snprintf(b, sizeof(b), "%.9g", v);
        return std::string(b);
    };
    std::vector<std::pair<std::string, std::string>> extra = {{"board", "1"}, {"px", fmt(px)}, {"py", fmt(py)}};
    std::string cs;
    for (auto &sl : slots)
    {
        const std::string k = std::to_string(sl.corner);
        cs += (cs.empty() ? "" : ",") + k;
        extra.push_back({"c" + k + "p", sl.path});
        extra.push_back({"c" + k + "n", sl.name});
    }
    extra.push_back({"corners", cs});
    if (slots.size() == 2)
    {
        // two sounds: TRUE MORPH or SCENE BLEND, BLEND = the second sound's weight
        extra.push_back({"blend", fmt(w[slots[1].corner])});
        auto &a = slots[0], &b = slots[1];
        return morphPatch(s, a.blob, b.blob, a.path, a.name, b.path, b.name, mode, why, extra);
    }
    // three or four: the first sound is the patch; the others contribute the values of the controls they share
    auto pb = split(slots[0].blob);
    if (!pb.ok)
    {
        why = "the first sound could not be read";
        return {};
    }
    TiXmlDocument da;
    da.Parse(pb.xml.c_str());
    auto *xa = paramsOf(da);
    auto *patchA = da.FirstChildElement("patch");
    if (!xa || !patchA)
    {
        why = "the first sound could not be read";
        return {};
    }
    struct Other
    {
        int corner;
        TiXmlDocument doc;
        TiXmlElement *params{nullptr};
        std::map<std::string, bool> sameGroup;
    };
    std::vector<std::unique_ptr<Other>> others;
    int differing = 0;
    for (size_t k = 1; k < slots.size(); ++k)
    {
        auto ps = split(slots[k].blob);
        if (!ps.ok)
        {
            why = "one of the sounds could not be read";
            return {};
        }
        auto o = std::make_unique<Other>();
        o->corner = slots[k].corner;
        o->doc.Parse(ps.xml.c_str());
        o->params = paramsOf(o->doc);
        if (!o->params)
        {
            why = "one of the sounds could not be read";
            return {};
        }
        // which modules are built the same way as in the first sound (every switch / type / choice of the module equal)
        for (auto *ea = xa->FirstChildElement(); ea; ea = ea->NextSiblingElement())
        {
            const std::string g = groupOf(ea->Value());
            if (!o->sameGroup.count(g))
                o->sameGroup[g] = true;
            auto *eb = o->params->FirstChildElement(ea->Value());
            if (!eb || !isIntType(ea))
                continue;
            int va = 0, vb = 0;
            ea->QueryIntAttribute("value", &va);
            eb->QueryIntAttribute("value", &vb);
            if (va != vb)
                o->sameGroup[g] = false;
        }
        for (auto &kv : o->sameGroup)
            differing += kv.second ? 0 : 1;
        others.push_back(std::move(o));
    }
    std::string bvals;
    int nvals = 0;
    for (auto *ea = xa->FirstChildElement(); ea; ea = ea->NextSiblingElement())
    {
        if (isIntType(ea))
            continue;
        double va = 0;
        ea->QueryDoubleAttribute("value", &va);
        float v[Morph::kCorners] = {};
        for (int k = 0; k < Morph::kCorners; ++k)
            v[k] = (float)va;
        bool any = false;
        const std::string g = groupOf(ea->Value());
        for (auto &o : others)
        {
            auto *eb = o->params->FirstChildElement(ea->Value());
            if (!eb || isIntType(eb) || !o->sameGroup[g])
                continue;
            double vb = va;
            eb->QueryDoubleAttribute("value", &vb);
            v[o->corner] = (float)vb;
            any = any || std::fabs(vb - va) > 1e-7;
        }
        // the first sound's own corner
        v[slots[0].corner] = (float)va;
        if (!any)
            continue;
        bvals += (nvals ? ";" : "") + std::string(ea->Value()) + "=";
        for (int k = 0; k < Morph::kCorners; ++k)
            bvals += (k ? "," : "") + fmt(v[k]);
        ++nvals;
    }
    // the macros glide too
    std::string bmac;
    {
        auto macroOf = [](TiXmlDocument &d, int k) {
            double v = 0;
            auto *p = d.FirstChildElement("patch");
            auto *cc = p ? p->FirstChildElement("customcontroller") : nullptr;
            if (cc)
                for (auto *e = cc->FirstChildElement("entry"); e; e = e->NextSiblingElement("entry"))
                {
                    int i = -1;
                    e->QueryIntAttribute("i", &i);
                    if (i == k)
                        e->QueryDoubleAttribute("v", &v);
                }
            return (float)v;
        };
        for (int mk = 0; mk < 8; ++mk)
        {
            float v[Morph::kCorners] = {};
            const float base = macroOf(da, mk);
            for (int k = 0; k < Morph::kCorners; ++k)
                v[k] = base;
            for (auto &o : others)
                v[o->corner] = macroOf(o->doc, mk);
            bmac += (mk ? ";" : "");
            for (int k = 0; k < Morph::kCorners; ++k)
                bmac += (k ? "," : "") + fmt(v[k]);
        }
    }
    auto *gbA = patchA->FirstChildElement("ghostbass");
    if (!gbA)
    {
        TiXmlElement g("ghostbass");
        g.SetAttribute("version", 1);
        patchA->InsertEndChild(g);
        gbA = patchA->FirstChildElement("ghostbass");
    }
    while (auto *old = gbA->FirstChildElement("morph"))
        gbA->RemoveChild(old);
    TiXmlElement mo("morph");
    mo.SetAttribute("mode", 3);
    mo.SetAttribute("a", slots[0].path);
    mo.SetAttribute("an", slots[0].name);
    mo.SetDoubleAttribute("blend", 0.0);
    for (auto &kv : extra)
        mo.SetAttribute(kv.first, kv.second);
    mo.SetAttribute("bvals", bvals);
    mo.SetAttribute("bmac", bmac);
    gbA->InsertEndChild(mo);
    std::string nm = "BLEND";
    for (auto &sl : slots)
        nm += (&sl == &slots[0] ? ": " : " + ") + sl.name;
    setMeta(patchA, nm.substr(0, 60), nullptr);
    mode = 3;
    why = "BLEND BOARD: " + std::to_string(slots.size()) + " sounds, " + std::to_string(nvals) + " shared controls glide between them" +
          (differing ? " (parts built differently stay as the first sound has them)" : "");
    Parts out = pb;
    out.xml = toString(da);
    return join(out);
}

} // namespace dna
} // namespace ghost
