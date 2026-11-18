#include "Model.h"
#include "PlayJson.h"
#include <cstdlib>
#include <cstdio>

namespace gslice
{
namespace
{
constexpr double kMinSegPF = 32.0;     // the shortest warp segment / region (PF): ~0.7 ms at 48 kHz
constexpr int64 kMinSegSF = 32;

// the normalised rate of a segment: 1 = real time (SF per PF scaled by the two rates)
double normRate (const Project& p, double ds, double dt) { return dt > 0 ? (ds / dt) * (p.sampleRate / juce::jmax (1.0, p.source.sampleRate)) : 1e30; }
double kps (const Project& p) { return juce::jmax (1.0, p.source.sampleRate) / juce::jmax (1.0, p.sampleRate); }   // SF per PF at 1x

bool ratesOk (const Project& p, const Region& r, juce::String* why)
{
    for (size_t i = 0; i + 1 < r.warp.size(); ++i)
    {
        if (r.holdOfSegment (i) != nullptr) continue;   // a HOLD segment is held on purpose (any length)
        const double rt = normRate (p, (double) (r.warp[i + 1].s - r.warp[i].s), r.warp[i + 1].t - r.warp[i].t);
        if (rt < limits::kHardMin - 1e-9 || rt > limits::kHardMax + 1e-9)
        {
            if (why) *why = "That would stretch a segment to " + juce::String (1.0 / rt, 2) + "x (limit 0.25x .. 4x)";
            return false;
        }
    }
    return true;
}
bool hasAnchors (const Region& r) { for (auto& w : r.warp) if (w.anchor) return true; return false; }
double absTime (const Region& r, const WarpMarker& w) { return r.dstStart + (r.reverse ? r.length() - w.t : w.t); }
}

bool Region::valid() const
{
    if (warp.size() < 2 || warp.front().t != 0.0 || warp.front().s < 0) return false;
    for (size_t i = 1; i < warp.size(); ++i)
        if (! (warp[i].t > warp[i - 1].t) || ! (warp[i].s > warp[i - 1].s) || ! std::isfinite (warp[i].t)) return false;
    return std::isfinite (dstStart) && dstStart >= 0 && std::isfinite (gainDb) && fadeInMs >= 0 && fadeOutMs >= 0;
}

bool Project::operator== (const Project& o) const
{
    return formatVersion == o.formatVersion && sampleRate == o.sampleRate && source == o.source && regions == o.regions && bpm == o.bpm
        && gridDiv == o.gridDiv && gridTriplet == o.gridTriplet && beatOffset == o.beatOffset && pitchSemis == o.pitchSemis
        && pitchCents == o.pitchCents && formantSemis == o.formantSemis && transientLock == o.transientLock && key == o.key && nextId == o.nextId
        && projectKey == o.projectKey && material == o.material && tonalityHz == o.tonalityHz && keyMode == o.keyMode && pitchEngine == o.pitchEngine
        && repeats == o.repeats && play == o.play
        && juce::JSON::toString (extra, true) == juce::JSON::toString (o.extra, true);
}

double Project::gridStep() const
{
    if (gridDiv <= 0) return 0.0;
    const double beat = secondsPerBeat() * sampleRate;      // a quarter note in PF
    return beat * 4.0 / (double) gridDiv * (gridTriplet ? 2.0 / 3.0 : 1.0);
}
double Project::snap (double pf, double strength) const
{
    const double g = gridStep();
    if (g <= 0 || strength <= 0) return pf;
    const double n = std::round ((pf - beatOffset) / g), target = beatOffset + n * g;
    return pf + juce::jlimit (0.0, 1.0, strength) * (target - pf);
}

Project makeProject (const SourceRef& src)
{
    Project p; p.source = src; p.sampleRate = src.sampleRate > 0 ? src.sampleRate : 48000.0;
    if (src.frames > 0)
    {
        Region r; r.id = p.nextId++;
        r.warp.push_back ({ p.nextId++, 0.0, 0, false });
        r.warp.push_back ({ p.nextId++, (double) src.frames / kps (p), src.frames, false });
        p.regions.push_back (r);
    }
    return p;
}

// ---------------------------------------------------------------- JSON
namespace json
{
static juce::var num (double v)
{
    if (std::isfinite (v) && v == std::floor (v) && std::abs (v) < 9.0e15) return juce::var ((juce::int64) v);
    char b[40]; std::snprintf (b, sizeof (b), "%.17g", v); return juce::var (juce::String (b));   // exact round trip
}
static double dbl (const juce::var& v, double def = 0)
{
    if (v.isString()) { const auto s = v.toString(); return s.isEmpty() ? def : std::strtod (s.toRawUTF8(), nullptr); }
    if (v.isVoid() || v.isUndefined()) return def;
    return (double) v;
}
static int64 i64 (const juce::var& v, int64 def = 0) { if (v.isVoid() || v.isUndefined()) return def; if (v.isString()) return v.toString().getLargeIntValue(); return (int64) (juce::int64) v; }

static const juce::StringArray& knownTop()
{
    static juce::StringArray k { "format", "formatVersion", "writer", "engineVersions", "sampleRate", "source", "regions", "bpm", "gridDiv", "gridTriplet",
                                 "beatOffset", "pitchSemis", "pitchCents", "formantSemis", "transientLock", "key", "nextId",
                                 "projectKey", "material", "tonalityHz", "keyMode", "pitchEngine",
                                 "repeats", "play" };
    return k;
}

juce::var toVar (const Project& p)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("format", "ghostslice");
    o->setProperty ("formatVersion", p.formatVersion);
    o->setProperty ("writer", juce::String ("Ghost Slice ") + GSLICE_VERSION);
    {
        auto* ev = new juce::DynamicObject(); ev->setProperty ("render", kEngineVersion); ev->setProperty ("signalsmith", "1.4.0 (header 1.3.2)");
        o->setProperty ("engineVersions", juce::var (ev));
    }
    o->setProperty ("sampleRate", num (p.sampleRate));
    {
        auto* s = new juce::DynamicObject();
        s->setProperty ("sha256", p.source.fingerprint); s->setProperty ("path", p.source.path); s->setProperty ("relPath", p.source.relPath);
        s->setProperty ("fileName", p.source.fileName); s->setProperty ("sampleRate", num (p.source.sampleRate));
        s->setProperty ("frames", (juce::int64) p.source.frames); s->setProperty ("channels", p.source.channels);
        o->setProperty ("source", juce::var (s));
    }
    juce::Array<juce::var> regs;
    for (auto& r : p.regions)
    {
        auto* ro = new juce::DynamicObject();
        ro->setProperty ("id", (juce::int64) r.id); ro->setProperty ("dstStart", num (r.dstStart));
        ro->setProperty ("srcIn", (juce::int64) r.srcIn()); ro->setProperty ("srcOut", (juce::int64) r.srcOut());   // (derived from the warp ends; for readers)
        ro->setProperty ("gainDb", num (r.gainDb)); ro->setProperty ("fadeInMs", num (r.fadeInMs)); ro->setProperty ("fadeOutMs", num (r.fadeOutMs));
        ro->setProperty ("reverse", r.reverse); ro->setProperty ("muted", r.muted);
        if (r.pitchSemis != 0.0) ro->setProperty ("pitchSemis", num (r.pitchSemis));   // (region PITCH / FORMANT: omitted at 0, older projects
        if (r.pitchCents != 0.0) ro->setProperty ("pitchCents", num (r.pitchCents));   //  stay byte-identical)
        if (r.formantSemis != 0.0) ro->setProperty ("formantSemis", num (r.formantSemis));
        juce::Array<juce::var> wm;
        for (auto& w : r.warp)
        {
            auto* m = new juce::DynamicObject(); m->setProperty ("id", (juce::int64) w.id); m->setProperty ("t", num (w.t)); m->setProperty ("s", (juce::int64) w.s);
            if (w.anchor) m->setProperty ("anchor", true);
            if (w.isAuto()) m->setProperty ("kind", "auto");
            wm.add (juce::var (m));
        }
        ro->setProperty ("warp", wm);
        juce::Array<juce::var> pe;
        for (auto& e : r.pitch)
        {
            auto* m = new juce::DynamicObject(); m->setProperty ("id", (juce::int64) e.id); m->setProperty ("srcStart", (juce::int64) e.srcStart);
            m->setProperty ("srcEnd", (juce::int64) e.srcEnd); m->setProperty ("cents", num (e.cents)); m->setProperty ("formantCents", num (e.formantCents));
            // phase 14 fields (written only when not the default, so older projects keep their bytes)
            if (! e.isNote()) m->setProperty ("kind", "draw");
            if (e.centre != 0) m->setProperty ("centre", num (e.centre));
            if (e.drift != 1.0) m->setProperty ("drift", num (e.drift));
            if (e.vibrato != 1.0) m->setProperty ("vibrato", num (e.vibrato));
            if (e.transitionMs != 40.0) m->setProperty ("transitionMs", num (e.transitionMs));
            if (e.glide) m->setProperty ("glide", true);
            if (e.absolute) m->setProperty ("absolute", true);
            if (e.engine >= 0) m->setProperty ("engine", e.engine);   // phase 15
            if (! e.curve.empty())
            {
                juce::Array<juce::var> cv; for (auto& [s, c] : e.curve) { juce::Array<juce::var> pt; pt.add ((juce::int64) s); pt.add (num (c)); cv.add (pt); }
                m->setProperty ("curve", cv);
            }
            pe.add (juce::var (m));
        }
        ro->setProperty ("pitch", pe);
        if (! r.holds.empty()) ro->setProperty ("holds", playjson::holdsToVar (r.holds));
        regs.add (juce::var (ro));
    }
    o->setProperty ("regions", regs);
    o->setProperty ("bpm", num (p.bpm)); o->setProperty ("gridDiv", p.gridDiv); o->setProperty ("gridTriplet", p.gridTriplet);
    o->setProperty ("beatOffset", num (p.beatOffset));
    o->setProperty ("pitchSemis", num (p.pitchSemis)); o->setProperty ("pitchCents", num (p.pitchCents)); o->setProperty ("formantSemis", num (p.formantSemis));
    o->setProperty ("transientLock", p.transientLock); o->setProperty ("key", p.key); o->setProperty ("nextId", (juce::int64) p.nextId);
    o->setProperty ("projectKey", p.projectKey); o->setProperty ("material", p.material); o->setProperty ("tonalityHz", num (p.tonalityHz));
    if (p.keyMode != 0) o->setProperty ("keyMode", p.keyMode);
    if (p.pitchEngine != 0) o->setProperty ("pitchEngine", p.pitchEngine);
    o->setProperty ("repeats", playjson::repeatsToVar (p.repeats)); o->setProperty ("play", playjson::playToVar (p.play));
    if (auto* ex = p.extra.getDynamicObject())
        for (auto& kv : ex->getProperties()) if (! knownTop().contains (kv.name.toString())) o->setProperty (kv.name, kv.value);
    return juce::var (o);
}

juce::String toString (const Project& p) { return juce::JSON::toString (toVar (p), false); }

bool fromVar (const juce::var& v, Project& out, juce::String& error)
{
    if (! v.isObject() || v["format"].toString() != "ghostslice") { error = "not a Ghost Slice project"; return false; }
    Project p;
    p.formatVersion = (int) v["formatVersion"];
    if (p.formatVersion > kFormatVersion) { error = "this project was saved by a newer Ghost Slice (format " + juce::String (p.formatVersion) + ")"; return false; }
    // (migration vN -> vN+1 goes here; format 1 is the first)
    p.formatVersion = kFormatVersion;
    p.sampleRate = dbl (v["sampleRate"], 48000);
    const auto s = v["source"];
    p.source.fingerprint = s["sha256"].toString(); p.source.path = s["path"].toString(); p.source.relPath = s["relPath"].toString();
    p.source.fileName = s["fileName"].toString(); p.source.sampleRate = dbl (s["sampleRate"], p.sampleRate);
    p.source.frames = i64 (s["frames"]); p.source.channels = (int) s["channels"];
    if (auto* arr = v["regions"].getArray())
        for (auto& rv : *arr)
        {
            Region r; r.id = i64 (rv["id"]); r.dstStart = dbl (rv["dstStart"]); r.gainDb = dbl (rv["gainDb"]);
            r.fadeInMs = dbl (rv["fadeInMs"]); r.fadeOutMs = dbl (rv["fadeOutMs"]); r.reverse = (bool) rv["reverse"]; r.muted = (bool) rv["muted"];
            r.pitchSemis = juce::jlimit (-24.0, 24.0, dbl (rv["pitchSemis"], 0.0)); r.pitchCents = juce::jlimit (-100.0, 100.0, dbl (rv["pitchCents"], 0.0));
            r.formantSemis = juce::jlimit (-24.0, 24.0, dbl (rv["formantSemis"], 0.0));
            if (auto* wa = rv["warp"].getArray())
                for (auto& wv : *wa) r.warp.push_back ({ i64 (wv["id"]), dbl (wv["t"]), i64 (wv["s"]), (bool) wv["anchor"],
                                                         wv["kind"].toString() == "auto" ? MarkerKind::Auto : MarkerKind::User });
            if (auto* pa = rv["pitch"].getArray())
                for (auto& pv : *pa)
                {
                    PitchEdit e; e.id = i64 (pv["id"]); e.srcStart = i64 (pv["srcStart"]); e.srcEnd = i64 (pv["srcEnd"]); e.cents = dbl (pv["cents"]); e.formantCents = dbl (pv["formantCents"]);
                    e.kind = pv["kind"].toString() == "draw" ? PitchEditKind::Draw : PitchEditKind::Note;
                    e.centre = dbl (pv["centre"]); e.drift = juce::jlimit (0.0, 2.0, dbl (pv["drift"], 1.0)); e.vibrato = juce::jlimit (0.0, 2.0, dbl (pv["vibrato"], 1.0));
                    e.transitionMs = juce::jlimit (0.0, 250.0, dbl (pv["transitionMs"], 40.0)); e.glide = (bool) pv["glide"]; e.absolute = (bool) pv["absolute"];
                    e.engine = pv.hasProperty ("engine") ? juce::jlimit (-1, 2, (int) pv["engine"]) : -1;
                    if (auto* ca = pv["curve"].getArray())
                        for (auto& pt : *ca) if (auto* xy = pt.getArray(); xy != nullptr && xy->size() == 2) e.curve.push_back ({ i64 ((*xy)[0]), dbl ((*xy)[1]) });
                    if (e.srcEnd > e.srcStart) r.pitch.push_back (std::move (e));
                }
            r.holds = playjson::holdsFromVar (rv["holds"]);
            if (! r.valid()) { error = "region " + juce::String (r.id) + " is damaged"; return false; }
            p.regions.push_back (std::move (r));
        }
    p.bpm = juce::jlimit (20.0, 999.0, dbl (v["bpm"], 120)); p.gridDiv = v.hasProperty ("gridDiv") ? (int) v["gridDiv"] : 16;
    p.gridTriplet = (bool) v["gridTriplet"]; p.beatOffset = dbl (v["beatOffset"]);
    p.pitchSemis = dbl (v["pitchSemis"]); p.pitchCents = dbl (v["pitchCents"]); p.formantSemis = dbl (v["formantSemis"]);
    p.transientLock = v.hasProperty ("transientLock") ? (bool) v["transientLock"] : true;
    p.key = v.hasProperty ("key") ? v["key"].toString() : juce::String ("--");
    p.projectKey = v.hasProperty ("projectKey") ? v["projectKey"].toString() : juce::String ("--");
    p.material = v.hasProperty ("material") ? v["material"].toString() : juce::String ("AUTO");
    p.tonalityHz = juce::jlimit (500.0, 24000.0, dbl (v["tonalityHz"], 8000));
    p.keyMode = juce::jlimit (0, 4, (int) v["keyMode"]);   // (4 = USE CHORDS, phase 17)
    p.pitchEngine = juce::jlimit (0, 2, (int) v["pitchEngine"]);
    p.nextId = juce::jmax<int64> (1, i64 (v["nextId"], 1));
    p.repeats = playjson::repeatsFromVar (v["repeats"]); p.play = playjson::playFromVar (v["play"]);
    for (auto& rp : p.repeats) p.nextId = std::max (p.nextId, rp.id + 1);
    for (auto& r : p.regions) { p.nextId = std::max (p.nextId, r.id + 1); for (auto& w : r.warp) p.nextId = std::max (p.nextId, w.id + 1); for (auto& h : r.holds) p.nextId = std::max (p.nextId, h.id + 1); for (auto& e : r.pitch) p.nextId = std::max (p.nextId, e.id + 1); }
    // keep unknown fields (a newer writer's extras survive a save by this version)
    auto* ex = new juce::DynamicObject(); bool any = false;
    if (auto* o = v.getDynamicObject()) for (auto& kv : o->getProperties()) if (! knownTop().contains (kv.name.toString())) { ex->setProperty (kv.name, kv.value); any = true; }
    p.extra = any ? juce::var (ex) : juce::var();
    if (! any) delete ex;
    out = std::move (p);
    return true;
}
bool fromString (const juce::String& s, Project& out, juce::String& error)
{
    juce::var v; const auto r = juce::JSON::parse (s, v);
    if (r.failed()) { error = "not valid JSON: " + r.getErrorMessage(); return false; }
    return fromVar (v, out, error);
}
juce::String fingerprint (const Project& p) { const auto s = juce::JSON::toString (toVar (p), true); return juce::SHA256 (s.toUTF8(), s.getNumBytesAsUTF8()).toHexString(); }
} // namespace json

// ---------------------------------------------------------------- ops
namespace ops
{
// split a region in its FORWARD local time uf: (left = source in .. cut, right = cut .. source out)
static bool splitForward (Project& p, const Region& r, double uf, Region& L, Region& Rr, juce::String* why)
{
    const double len = r.length();
    if (uf < kMinSegPF || uf > len - kMinSegPF) { if (why) *why = "too close to the region's edge"; return false; }
    const auto m = r.map();
    const int64 sv = (int64) std::llround (m.srcAt (uf));
    // an existing marker at (almost) this point is the cut itself
    const WarpMarker* at = nullptr;
    for (auto& w : r.warp) if (std::abs (w.t - uf) < 0.5 || w.s == sv) at = &w;
    const double ut = at != nullptr ? at->t : m.outAt ((double) sv);
    const int64 cs = at != nullptr ? at->s : sv;
    if (cs <= r.srcIn() + kMinSegSF || cs >= r.srcOut() - kMinSegSF || ut < kMinSegPF || ut > len - kMinSegPF) { if (why) *why = "too close to the region's edge"; return false; }
    L = r; Rr = r; L.warp.clear(); Rr.warp.clear();
    for (auto& w : r.warp) if (w.t < ut - 1e-9) L.warp.push_back (w);
    L.warp.push_back ({ at != nullptr ? at->id : p.nextId++, ut, cs, at != nullptr && at->anchor });
    Rr.warp.push_back ({ p.nextId++, 0.0, cs, false });
    for (auto& w : r.warp) if (w.t > ut + 1e-9) { auto x = w; x.t -= ut; Rr.warp.push_back (x); }
    L.id = r.id; Rr.id = p.nextId++;
    Rr.dstStart = r.dstStart + ut;
    return true;
}

R splitAt (const Project& in, int64 id, double dstPos, juce::String* why, int64* newRightId)
{
    Project p = in; const int idx = p.indexOf (id);
    if (idx < 0) { if (why) *why = "no region there"; return std::nullopt; }
    const Region r = p.regions[(size_t) idx];
    const double u = dstPos - r.dstStart;
    Region fl, fr;
    if (! splitForward (p, r, r.reverse ? r.length() - u : u, fl, fr, why)) return std::nullopt;
    Region outL, outR;   // in OUTPUT order
    if (! r.reverse) { outL = fl; outR = fr; }
    else
    {   // reversed: the output's left part is the forward right part (played backwards), and vice versa
        outL = fr; outR = fl;
        outL.dstStart = r.dstStart; outR.dstStart = r.dstStart + outL.length();
        outL.id = r.id; outR.id = fr.id;
    }
    outL.fadeOutMs = 0; outR.fadeInMs = 0;
    outL.fadeInMs = r.fadeInMs; outR.fadeOutMs = r.fadeOutMs;
    p.regions[(size_t) idx] = outL;
    p.regions.insert (p.regions.begin() + idx + 1, outR);
    if (newRightId) *newRightId = outR.id;
    return p;
}

R splitRange (const Project& in, double a, double b, juce::String* why)
{
    if (b < a) std::swap (a, b);
    Project p = in; bool any = false;
    for (double cut : { a, b })
    {
        std::vector<int64> ids; for (auto& r : p.regions) if (cut > r.dstStart + kMinSegPF && cut < r.dstEnd() - kMinSegPF) ids.push_back (r.id);
        for (auto id : ids) if (auto q = splitAt (p, id, cut, nullptr)) { p = *q; any = true; }
    }
    if (! any) { if (why) *why = "nothing to cut there"; return std::nullopt; }
    return p;
}

R splitAtMarkers (const Project& in, int64 id, juce::String* why)
{
    const auto* r = in.find (id); if (r == nullptr || r->warp.size() <= 2) { if (why) *why = "this region has no inner markers"; return std::nullopt; }
    std::vector<double> cuts; for (size_t i = 1; i + 1 < r->warp.size(); ++i) cuts.push_back (absTime (*r, r->warp[i]));
    Project p = in;
    for (double c : cuts)
        for (auto& x : p.regions)
            if (c > x.dstStart + kMinSegPF && c < x.dstEnd() - kMinSegPF )
            {   // the region (or the piece of it) that holds this cut
                bool mine = false; for (auto& w : x.warp) if (std::abs (absTime (x, w) - c) < 1e-6) mine = true;
                if (! mine) continue;
                if (auto q = splitAt (p, x.id, c, nullptr)) { p = *q; break; }
            }
    return p;
}

R deleteRegion (const Project& in, int64 id)
{
    Project p = in; const int i = p.indexOf (id); if (i < 0) return std::nullopt;
    if (hasAnchors (p.regions[(size_t) i])) return std::nullopt;
    p.regions.erase (p.regions.begin() + i); return p;
}

R deleteRange (const Project& in, double a, double b, bool ripple)
{
    if (b < a) std::swap (a, b);
    if (b - a < kMinSegPF) return std::nullopt;
    Project p = in;
    if (auto q = splitRange (p, a, b)) p = *q;
    bool any = false;
    for (int i = (int) p.regions.size() - 1; i >= 0; --i)
    {
        const auto& r = p.regions[(size_t) i];
        if (r.dstStart >= a - 0.5 && r.dstEnd() <= b + 0.5) { if (hasAnchors (r)) return std::nullopt; p.regions.erase (p.regions.begin() + i); any = true; }
    }
    if (ripple)
        for (auto& r : p.regions) if (r.dstStart >= b - 0.5) { if (hasAnchors (r)) return std::nullopt; r.dstStart = std::max (0.0, r.dstStart - (b - a)); any = true; }
    if (! any) return std::nullopt;
    return p;
}

R duplicate (const Project& in, int64 id, int64* newId)
{
    Project p = in; const auto* r = p.find (id); if (r == nullptr) return std::nullopt;
    Region c = *r; c.id = p.nextId++; c.dstStart = r->dstEnd();
    for (auto& w : c.warp) { w.id = p.nextId++; w.anchor = false; }
    p.regions.push_back (c);
    if (newId) *newId = c.id;
    return p;
}

R setReverse (const Project& in, int64 id, bool on)
{
    Project p = in; auto* r = p.find (id); if (r == nullptr || r->reverse == on) return std::nullopt;
    if (hasAnchors (*r)) return std::nullopt;   // the marker's output time would flip
    r->reverse = on; std::swap (r->fadeInMs, r->fadeOutMs); return p;
}
R setRegionPitch (const Project& in, int64 id, double semis, double cents, double formant)
{
    Project p = in; auto* r = p.find (id); if (r == nullptr) return std::nullopt;
    r->pitchSemis = juce::jlimit (-24.0, 24.0, semis); r->pitchCents = juce::jlimit (-100.0, 100.0, cents); r->formantSemis = juce::jlimit (-24.0, 24.0, formant);
    return p;
}
R setGain (const Project& in, int64 id, double db)
{
    Project p = in; auto* r = p.find (id); if (r == nullptr) return std::nullopt;
    r->gainDb = juce::jlimit (-60.0, 18.0, db); return p;
}
R setFades (const Project& in, int64 id, double inMs, double outMs)
{
    Project p = in; auto* r = p.find (id); if (r == nullptr) return std::nullopt;
    const double lenMs = r->length() / p.sampleRate * 1000.0;
    inMs = juce::jlimit (0.0, lenMs, inMs); outMs = juce::jlimit (0.0, lenMs, outMs);
    if (inMs + outMs > lenMs) { const double k = lenMs / (inMs + outMs); inMs *= k; outMs *= k; }
    r->fadeInMs = inMs; r->fadeOutMs = outMs; return p;
}

R moveRegion (const Project& in, int64 id, double nd, juce::String* why)
{
    Project p = in; auto* r = p.find (id); if (r == nullptr) return std::nullopt;
    nd = std::max (0.0, std::round (nd));
    if (nd == r->dstStart) return std::nullopt;
    if (hasAnchors (*r)) { if (why) *why = "ANCHOR LOCK: this region has an anchored marker"; return std::nullopt; }
    r->dstStart = nd;
    // moved = brought to the top
    Region c = *r; p.regions.erase (p.regions.begin() + p.indexOf (id)); p.regions.push_back (c);
    return p;
}

R rearrange (const Project& in, int from, int to)
{
    Project p = in; const int n = (int) p.regions.size();
    if (from < 0 || from >= n || to < 0 || to >= n || from == to) return std::nullopt;
    std::vector<int> order (n); for (int i = 0; i < n; ++i) order[(size_t) i] = i;
    std::stable_sort (order.begin(), order.end(), [&] (int a, int b) { return p.regions[(size_t) a].dstStart < p.regions[(size_t) b].dstStart; });
    std::vector<double> gapAfter ((size_t) n, 0.0);
    for (int k = 0; k + 1 < n; ++k) gapAfter[(size_t) k] = std::max (0.0, p.regions[(size_t) order[(size_t) k + 1]].dstStart - p.regions[(size_t) order[(size_t) k]].dstEnd());
    const double s0 = p.regions[(size_t) order[0]].dstStart;
    auto neworder = order; const int moved = neworder[(size_t) from]; neworder.erase (neworder.begin() + from); neworder.insert (neworder.begin() + to, moved);
    double pos = s0;
    for (int k = 0; k < n; ++k)
    {
        auto& r = p.regions[(size_t) neworder[(size_t) k]];
        const double nd = std::round (pos);
        if (nd != r.dstStart && hasAnchors (r)) return std::nullopt;
        r.dstStart = nd; pos = nd + r.length() + gapAfter[(size_t) k];
    }
    return p;
}

R trim (const Project& in, int64 id, bool startEdge, int64 ns, juce::String* why)
{
    Project p = in; auto* r = p.find (id); if (r == nullptr) return std::nullopt;
    auto& w = r->warp; const size_t n = w.size();
    if (r->reverse) startEdge = ! startEdge;   // output edges of a reversed region are the other source ends
    if (startEdge)
    {
        const double rate = (double) (w[1].s - w[0].s) / (w[1].t - w[0].t);
        ns = juce::jlimit<int64> (0, w[1].s - kMinSegSF, ns);
        if (ns == w[0].s) return std::nullopt;
        if (w[0].anchor) { if (why) *why = "ANCHOR LOCK"; return std::nullopt; }
        const double t1 = (double) (w[1].s - ns) / rate, d = t1 - w[1].t;
        w[0].s = ns;
        for (size_t i = 1; i < n; ++i) w[i].t += d;
        if (! r->reverse) r->dstStart -= d;
        if (r->dstStart < 0) { if (why) *why = "the region would start before 0"; return std::nullopt; }
    }
    else
    {
        const double rate = (double) (w[n - 1].s - w[n - 2].s) / (w[n - 1].t - w[n - 2].t);
        ns = juce::jlimit<int64> (w[n - 2].s + kMinSegSF, p.source.frames, ns);
        if (ns == w[n - 1].s) return std::nullopt;
        if (w[n - 1].anchor) { if (why) *why = "ANCHOR LOCK"; return std::nullopt; }
        const double newT = w[n - 2].t + (double) (ns - w[n - 2].s) / rate;
        const double d = newT - w[n - 1].t;
        w[n - 1].s = ns; w[n - 1].t = newT;
        if (r->reverse) r->dstStart -= d;
        if (r->dstStart < 0) { if (why) *why = "the region would start before 0"; return std::nullopt; }
    }
    if (! r->valid()) { if (why) *why = "too short"; return std::nullopt; }
    if (! anchorsKept (in, p)) { if (why) *why = "ANCHOR LOCK"; return std::nullopt; }
    return p;
}

R stretchTo (const Project& in, int64 id, double newLen, juce::String* why)
{
    Project p = in; auto* r = p.find (id); if (r == nullptr) return std::nullopt;
    const double len = r->length();
    newLen = std::round (newLen);
    if (newLen < kMinSegPF * (double) (r->warp.size() - 1) || newLen == len) return std::nullopt;
    for (auto& w : r->warp) if (w.anchor && w.t > 0) { if (why) *why = "ANCHOR LOCK: unlock the anchored marker to stretch"; return std::nullopt; }
    const double f = newLen / len;
    for (auto& w : r->warp) w.t = (&w == &r->warp.back()) ? newLen : w.t * f;
    if (! ratesOk (p, *r, why) || ! r->valid()) return std::nullopt;
    return p;
}

R addMarker (const Project& in, int64 id, double lt, int64* newId, juce::String* why)
{
    Project p = in; auto* r = p.find (id); if (r == nullptr) return std::nullopt;
    const double uf = r->reverse ? r->length() - lt : lt;   // markers live in forward local time
    const auto m = r->map();
    const int64 sv = (int64) std::llround (m.srcAt (uf));
    const double ut = m.outAt ((double) sv);
    for (auto& w : r->warp) if (std::abs (w.t - ut) < kMinSegPF || std::abs ((double) (w.s - sv)) < (double) kMinSegSF) { if (why) *why = "too close to another marker"; return std::nullopt; }
    WarpMarker nm { p.nextId++, ut, sv, false };
    auto it = std::upper_bound (r->warp.begin(), r->warp.end(), ut, [] (double t, const WarpMarker& w) { return t < w.t; });
    if (it == r->warp.begin() || it == r->warp.end()) return std::nullopt;
    r->warp.insert (it, nm);
    if (newId) *newId = nm.id;
    return p;
}

R removeMarker (const Project& in, int64 id, int64 mid, juce::String* why)
{
    Project p = in; auto* r = p.find (id); if (r == nullptr) return std::nullopt;
    for (size_t i = 1; i + 1 < r->warp.size(); ++i)
        if (r->warp[i].id == mid)
        {
            if (r->warp[i].anchor) { if (why) *why = "ANCHOR LOCK: unlock it first"; return std::nullopt; }
            r->warp.erase (r->warp.begin() + (std::ptrdiff_t) i);
            if (! ratesOk (p, *r, why)) return std::nullopt;
            return p;
        }
    if (why) *why = "the region's end markers stay";
    return std::nullopt;
}

R dragMarker (const Project& in, int64 id, int64 mid, double newT, FlexMode mode, juce::String* why)
{
    if (mode == FlexMode::Slip) return std::nullopt;
    Project p = in; auto* r = p.find (id); if (r == nullptr) return std::nullopt;
    auto& w = r->warp; const size_t n = w.size();
    // work in OUTPUT order (a reversed region shows its markers mirrored): T[k] = output time, M[k] = the stored marker
    const double len = r->length();
    std::vector<double> T (n); std::vector<size_t> M (n);
    for (size_t k = 0; k < n; ++k) { M[k] = r->reverse ? n - 1 - k : k; T[k] = r->reverse ? len - w[M[k]].t : w[M[k]].t; }
    size_t i = 0; for (; i < n; ++i) if (w[M[i]].id == mid) break;
    if (i == n) { if (why) *why = "no such marker"; return std::nullopt; }
    if (i == 0) { if (why) *why = "the first marker moves with the region (drag the region)"; return std::nullopt; }
    if (w[M[i]].anchor) { if (why) *why = "ANCHOR LOCK: this marker never moves"; return std::nullopt; }
    if (mode == FlexMode::Move)
    {   // MOVE: only this marker; its neighbours' segments absorb it; the region's length stays
        if (i == n - 1) { if (why) *why = "MOVE keeps the length: use STRETCH for the end marker"; return std::nullopt; }
        newT = juce::jlimit (T[i - 1] + kMinSegPF, T[i + 1] - kMinSegPF, newT);
        if (newT == T[i]) return std::nullopt;
        T[i] = newT;
    }
    else
    {   // STRETCH: the segment before the marker changes; everything after it shifts with it, up to the next anchor (which never moves)
        size_t k = i + 1; while (k < n && ! w[M[k]].anchor) ++k;
        double d = std::max (T[i - 1] + kMinSegPF, newT) - T[i];
        if (k < n) d = std::min (d, T[k] - kMinSegPF - T[k - 1]);
        if (d == 0) return std::nullopt;
        for (size_t j = i; j < k; ++j) T[j] += d;
    }
    const double nlen = T[n - 1];
    for (size_t k = 0; k < n; ++k) w[M[k]].t = r->reverse ? nlen - T[k] : T[k];
    w[M[i]].kind = MarkerKind::User;   // moved by hand: ALIGN keeps it from now on
    w.front().t = 0.0;   // (exact)
    if (! ratesOk (p, *r, why) || ! r->valid()) { if (why && why->isEmpty()) *why = "markers would cross"; return std::nullopt; }
    if (! anchorsKept (in, p)) { if (why) *why = "ANCHOR LOCK"; return std::nullopt; }
    return p;
}

R slip (const Project& in, int64 id, int64 d, juce::String* why)
{
    Project p = in; auto* r = p.find (id); if (r == nullptr) return std::nullopt;
    if (r->reverse) d = -d;
    d = juce::jlimit<int64> (-r->srcIn(), p.source.frames - r->srcOut(), d);
    if (d == 0) { if (why) *why = "the source ends here"; return std::nullopt; }
    for (auto& w : r->warp) w.s += d;
    return p;
}

R setAnchor (const Project& in, int64 id, int64 mid, bool on)
{
    Project p = in; auto* r = p.find (id); if (r == nullptr) return std::nullopt;
    for (auto& w : r->warp) if (w.id == mid) { if (w.anchor == on) return std::nullopt; w.anchor = on; w.kind = MarkerKind::User; return p; }
    return std::nullopt;
}

R setTempo (const Project& in, double bpm)
{
    if (! std::isfinite (bpm)) return std::nullopt;
    Project p = in; bpm = juce::jlimit (20.0, 999.0, bpm); if (bpm == p.bpm) return std::nullopt; p.bpm = bpm; return p;
}

double bpmFromSpan (double sec, int* beatsOut)
{
    if (! (sec > 0.01)) return 0;
    for (int n : { 1, 2, 4, 8, 16, 32, 64, 3, 6, 12, 24, 48, 5, 7 })
    {
        const double b = 60.0 * n / sec;
        if (b >= 70.0 && b < 180.0) { if (beatsOut) *beatsOut = n; return b; }
    }
    if (beatsOut) *beatsOut = 1;
    return juce::jlimit (20.0, 999.0, 60.0 / sec);
}

std::vector<double> anchorTimes (const Project& p)
{
    std::vector<double> v; for (auto& r : p.regions) for (auto& w : r.warp) if (w.anchor) v.push_back (absTime (r, w));
    std::sort (v.begin(), v.end()); return v;
}
bool anchorsKept (const Project& a, const Project& b)
{
    for (auto& r : a.regions)
        for (auto& w : r.warp)
        {
            if (! w.anchor) continue;
            bool found = false;
            for (auto& r2 : b.regions) for (auto& w2 : r2.warp) if (w2.id == w.id) { found = true; if (std::abs (absTime (r2, w2) - absTime (r, w)) > 1e-6) return false; }
            if (! found) return false;
        }
    return true;
}
} // namespace ops

// ---------------------------------------------------------------- History
void History::push (const Project& p, const juce::String& label)
{
    if (p == *cur) return;
    if (gestureOpen && gesture) { cur = std::make_shared<const Project> (p); return; }   // the same drag: one undo step
    undoStack.push_back ({ cur, label });
    if (undoStack.size() > maxSteps) undoStack.erase (undoStack.begin());
    redoStack.clear();
    cur = std::make_shared<const Project> (p);
    if (gestureOpen) gesture = true;
}
bool History::undo()
{
    if (undoStack.empty()) return false;
    redoStack.push_back ({ cur, undoStack.back().second }); cur = undoStack.back().first; undoStack.pop_back(); gesture = false; return true;
}
bool History::redo()
{
    if (redoStack.empty()) return false;
    undoStack.push_back ({ cur, redoStack.back().second }); cur = redoStack.back().first; redoStack.pop_back(); gesture = false; return true;
}
} // namespace gslice
