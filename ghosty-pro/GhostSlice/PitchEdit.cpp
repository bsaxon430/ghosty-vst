#include "PitchEdit.h"
#include "AnalysisInternal.h"
#include <set>

namespace gslice
{
bool PitchCurve::activeIn (double a, double b) const
{
    const double fpw = 1.0 / (wfPerSf * hop);
    const int n = (int) cents.size();
    const int i0 = juce::jlimit (0, n, (int) std::floor (a * fpw) - 1), i1 = juce::jlimit (0, n, (int) std::ceil (b * fpw) + 2);
    for (int i = i0; i < i1; ++i) if (std::abs (cents[(size_t) i]) > 0.05f || std::abs (formant[(size_t) i]) > 0.05f) return true;
    return false;
}

namespace pitchedit
{
using analysis::detail::zeroPhaseLowpass;

double centreOf (const analysis::PitchTrack& t, int64 s0, int64 s1)
{
    if (t.frames() == 0 || s1 <= s0) return 0;
    const int a = juce::jlimit (0, t.frames(), (int) std::ceil ((double) s0 / t.hop)), b = juce::jlimit (0, t.frames(), (int) std::floor ((double) s1 / t.hop) + 1);
    const int L = b - a; if (L <= 0) return 0;
    const int m0 = a + (int) std::floor (L * 0.2), m1 = juce::jmax (m0 + 1, a + (int) std::ceil (L * 0.8));
    std::vector<std::pair<double, double>> v;
    for (int f = m0; f < m1 && f < b; ++f) if (t.f0[(size_t) f] > 0) v.push_back ({ centsOfHz (t.f0[(size_t) f]), std::pow (10.0, t.rmsDb[(size_t) f] / 20.0) });
    if (v.empty()) for (int f = a; f < b; ++f) if (t.f0[(size_t) f] > 0) v.push_back ({ centsOfHz (t.f0[(size_t) f]), 1.0 });
    if (v.empty()) return 0;
    std::sort (v.begin(), v.end());
    double tot = 0; for (auto& x : v) tot += x.second;
    double acc = 0; for (auto& x : v) { acc += x.second; if (acc >= 0.5 * tot) return x.first; }
    return v.back().first;
}

juce::String engineName (int e) { return e == (int) PitchEngine::Vocal ? "VOCAL" : e == (int) PitchEngine::Wide ? "WIDE" : "AUTO"; }

std::shared_ptr<const PitchCurve> build (const Region& r, const analysis::PitchTrack* track, double wfPerSf, double sourceRate, PitchEngine projectEngine, double globalCents)
{
    if (r.pitch.empty()) return nullptr;
    bool any = false; for (auto& e : r.pitch) if (! e.neutral()) any = true;
    if (! any) return nullptr;
    auto C = std::make_shared<PitchCurve>();
    C->wfPerSf = wfPerSf;
    const bool hasTrack = track != nullptr && track->frames() > 0;
    C->hop = hasTrack ? (double) track->hop : 256.0;
    const double hop = C->hop, fps = sourceRate / hop;
    int64 maxS = r.srcOut(); for (auto& e : r.pitch) maxS = std::max (maxS, e.srcEnd);
    const int N = (int) (maxS / hop) + 3;
    std::vector<double> c ((size_t) N, 0.0);            // the detected pitch (MIDI x 100), unvoiced frames held from the neighbours
    std::vector<uint8_t> voiced ((size_t) N, hasTrack ? 0 : 1);
    if (hasTrack)
    {
        for (int f = 0; f < N && f < track->frames(); ++f) if (track->f0[(size_t) f] > 0) { c[(size_t) f] = centsOfHz (track->f0[(size_t) f]); voiced[(size_t) f] = 1; }
        double last = 0; int firstV = -1;
        for (int f = 0; f < N; ++f) { if (voiced[(size_t) f]) { last = c[(size_t) f]; if (firstV < 0) firstV = f; } else c[(size_t) f] = last; }
        if (firstV > 0) for (int f = 0; f < firstV; ++f) c[(size_t) f] = c[(size_t) firstV];
    }
    auto frameOf = [&] (int64 s) { return (double) s / hop; };
    // ---- the note edits: their own delta functions (defined everywhere: outside the note = its constant shift)
    std::vector<const PitchEdit*> notes, draws;
    for (auto& e : r.pitch) (e.isNote() ? notes : draws).push_back (&e);
    std::sort (notes.begin(), notes.end(), [] (const PitchEdit* a, const PitchEdit* b) { return a->srcStart < b->srcStart; });
    struct NF { const PitchEdit* e; int a, b; double C; std::vector<double> d; };   // d: delta over [a, b)
    std::vector<NF> nf;
    for (auto* e : notes)
    {
        NF x { e, juce::jlimit (0, N, (int) std::ceil (frameOf (e->srcStart))), juce::jlimit (0, N, (int) std::floor (frameOf (e->srcEnd)) + 1), e->centre, {} };
        if (x.b <= x.a) continue;
        if (hasTrack && x.C == 0) x.C = centreOf (*track, e->srcStart, e->srcEnd);
        const int L = x.b - x.a;
        x.d.assign ((size_t) L, e->cents);
        if (hasTrack && x.C != 0 && (e->drift != 1.0 || e->vibrato != 1.0) && L >= 8)
        {   // 6.1: drift = low-pass 1.5 Hz of the deviation (zero phase); vibrato = low-pass 12 Hz minus the drift (complementary,
            // phase 15: a 3.5 - 9 Hz band-pass only caught ~74 % of a 5 Hz wobble, so x0 left a quarter of it); the rest (jitter) stays
            std::vector<double> dev ((size_t) L); for (int i = 0; i < L; ++i) dev[(size_t) i] = c[(size_t) (x.a + i)] - x.C;
            std::vector<double> drift = dev; zeroPhaseLowpass (drift, 1.5, fps);
            std::vector<double> vib = dev; zeroPhaseLowpass (vib, 12.0, fps);
            for (int i = 0; i < L; ++i) vib[(size_t) i] -= drift[(size_t) i];
            for (int i = 0; i < L; ++i) x.d[(size_t) i] += (e->drift - 1.0) * drift[(size_t) i] + (e->vibrato - 1.0) * vib[(size_t) i];
        }
        nf.push_back (std::move (x));
    }
    auto dOf = [&] (int k, int f) -> double   // note k's delta at frame f (-1 = no note: 0)
    {
        if (k < 0) return 0.0;
        const auto& x = nf[(size_t) k];
        return x.d[(size_t) juce::jlimit (0, x.b - x.a - 1, f - x.a)];
    };
    std::vector<double> delta ((size_t) N, 0.0), form ((size_t) N, 0.0), base ((size_t) N, 0.0);
    std::vector<int> owner ((size_t) N, -1);
    for (int k = 0; k < (int) nf.size(); ++k) for (int f = nf[(size_t) k].a; f < nf[(size_t) k].b; ++f) owner[(size_t) f] = k;
    for (int f = 0; f < N; ++f) delta[(size_t) f] = dOf (owner[(size_t) f], f);
    // ---- TRANSITIONS: a raised-cosine blend of the neighbours' deltas over L centred at each boundary (6.2), or a GLIDE
    const double gapJoin = 0.030 * fps;   // notes closer than 30 ms are neighbours (the blend runs between their shifts)
    auto blend = [&] (int left, int right, double tb, double Lms, bool glide)
    {
        const double L = juce::jmax (1.0, Lms * 0.001 * fps);
        const int f0 = juce::jmax (0, (int) std::floor (tb - L / 2)), f1 = juce::jmin (N - 1, (int) std::ceil (tb + L / 2));
        const bool canGlide = glide && left >= 0 && right >= 0 && hasTrack && nf[(size_t) left].C != 0 && nf[(size_t) right].C != 0;
        for (int f = f0; f <= f1; ++f)
        {
            const double x = juce::jlimit (0.0, 1.0, ((double) f - tb + L / 2) / L);
            const double h = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * x);
            if (canGlide && voiced[(size_t) f])
            {
                const double TA = nf[(size_t) left].C + nf[(size_t) left].e->cents, TB = nf[(size_t) right].C + nf[(size_t) right].e->cents;
                delta[(size_t) f] = TA + (TB - TA) * x - c[(size_t) f];
            }
            else delta[(size_t) f] = dOf (left, f) * (1.0 - h) + dOf (right, f) * h;
        }
    };
    for (int k = 0; k < (int) nf.size(); ++k)
    {
        const auto& x = nf[(size_t) k];
        const bool joined = k > 0 && (double) x.a - (double) nf[(size_t) k - 1].b <= gapJoin;
        blend (joined ? k - 1 : -1, k, joined ? 0.5 * (x.a + nf[(size_t) k - 1].b) : (double) x.a, x.e->transitionMs, x.e->glide);
        const bool nextJoined = k + 1 < (int) nf.size() && (double) nf[(size_t) k + 1].a - (double) x.b <= gapJoin;
        if (! nextJoined) blend (k, -1, (double) x.b, x.e->transitionMs, false);
    }
    // FORMANT per note (20 ms linear edges) and the note's own f0 as the formant base
    for (int k = 0; k < (int) nf.size(); ++k)
    {
        const auto& x = nf[(size_t) k];
        const double ramp = juce::jmax (1.0, 0.020 * fps);
        for (int f = juce::jmax (0, x.a - (int) ramp); f < juce::jmin (N, x.b + (int) ramp); ++f)
        {
            const double w = juce::jlimit (0.0, 1.0, juce::jmin ((f - (x.a - ramp)) / ramp, ((x.b + ramp) - f) / ramp));
            form[(size_t) f] += x.e->formantCents * w;
            if (f >= x.a && f < x.b && x.C != 0) base[(size_t) f] = hzOfCents (x.C);
        }
    }
    // ---- PITCH DRAW curves (in edit order: later ones on top)
    std::vector<double> cs = c; if (hasTrack) zeroPhaseLowpass (cs, 8.0, fps);   // the pitch the ABSOLUTE curve replaces (jitter kept)
    for (auto& e : r.pitch)
    {
        if (e.isNote() || e.curve.size() < 2) continue;
        const auto& pts = e.curve;
        const int a = juce::jlimit (0, N, (int) std::ceil (frameOf (pts.front().first))), b = juce::jlimit (0, N, (int) std::floor (frameOf (pts.back().first)) + 1);
        const double ramp = juce::jmax (1.0, 0.020 * fps);
        size_t j = 0;
        for (int f = a; f < b; ++f)
        {
            const double s = f * hop;
            while (j + 2 < pts.size() && (double) pts[j + 1].first <= s) ++j;
            const double sa = (double) pts[j].first, sb = (double) pts[j + 1].first;
            const double D = sb > sa ? pts[j].second + (pts[j + 1].second - pts[j].second) * juce::jlimit (0.0, 1.0, (s - sa) / (sb - sa)) : pts[j].second;
            const double w = juce::jlimit (0.0, 1.0, juce::jmin ((f - a + 1) / ramp, (b - f) / ramp));
            if (e.absolute) { if (hasTrack && voiced[(size_t) f]) delta[(size_t) f] = delta[(size_t) f] * (1.0 - w) + (D - cs[(size_t) f]) * w; }
            else delta[(size_t) f] += D * w;
        }
    }
    // ---- unvoiced frames: no shift (faded over 10 ms), noise only gets artifacts from it
    if (hasTrack)
    {
        const double fade = juce::jmax (1.0, 0.010 * fps);
        std::vector<double> dist ((size_t) N, 1e9);
        double d = 1e9; for (int f = 0; f < N; ++f) { d = voiced[(size_t) f] ? 0.0 : d + 1.0; dist[(size_t) f] = d; }
        d = 1e9; for (int f = N; f-- > 0;) { d = voiced[(size_t) f] ? 0.0 : d + 1.0; dist[(size_t) f] = std::min (dist[(size_t) f], d); }
        for (int f = 0; f < N; ++f) delta[(size_t) f] *= juce::jlimit (0.0, 1.0, 1.0 - dist[(size_t) f] / fade);
    }
    C->cents.resize ((size_t) N); C->formant.resize ((size_t) N); C->baseHz.resize ((size_t) N);
    bool active = false;
    for (int f = 0; f < N; ++f)
    {
        C->cents[(size_t) f] = (float) juce::jlimit (-3600.0, 3600.0, delta[(size_t) f]);
        C->formant[(size_t) f] = (float) juce::jlimit (-2400.0, 2400.0, form[(size_t) f]);
        C->baseHz[(size_t) f] = (float) base[(size_t) f];
        if (std::abs (C->cents[(size_t) f]) > 0.05f || std::abs (C->formant[(size_t) f]) > 0.05f) active = true;
    }
    if (! active) return nullptr;
    // ---- ENGINE (phase 15): each note resolves VOCAL / WIDE, every frame takes the nearest note's (draw-only curves: the project's)
    if (hasTrack)
    {
        auto resolve = [&] (int eng, int a, int b) -> bool
        {
            if (eng == (int) PitchEngine::Wide) return false;
            int nv = 0, nTot = 0; double mx = 0;
            for (int f = juce::jmax (0, a); f < juce::jmin (N, b); ++f)
            {
                ++nTot; if (! voiced[(size_t) f]) continue;
                ++nv; mx = std::max (mx, std::abs ((double) C->cents[(size_t) f] + globalCents));
            }
            const double vf = nTot > 0 ? (double) nv / nTot : 0.0;
            if (eng == (int) PitchEngine::Vocal) return vf >= 0.3 && mx <= kMaxVocalCents + 0.5;
            return vf >= 0.5 && mx <= kAutoVocalCents + 0.5;   // AUTO
        };
        std::vector<uint8_t> nv ((size_t) nf.size(), 0);
        bool anyV = false;
        for (size_t k = 0; k < nf.size(); ++k)
        {
            const auto& x = nf[k];
            const int m = (int) std::ceil (0.5 * x.e->transitionMs * 0.001 * fps) + 1;   // the note's transitions count
            nv[k] = resolve (x.e->engine >= 0 ? x.e->engine : (int) projectEngine, x.a - m, x.b + m) ? 1 : 0;
            anyV |= nv[k] != 0;
        }
        const bool drawVocal = nf.empty() && resolve ((int) projectEngine, 0, N);
        if (anyV || drawVocal)
        {
            std::vector<float> m ((size_t) N, drawVocal ? 1.0f : 0.0f);
            if (! nf.empty())
                for (int f = 0; f < N; ++f)
                {
                    int best = 0; double bd = 1e18;
                    for (size_t k = 0; k < nf.size(); ++k)
                    {
                        const double d = f < nf[k].a ? (double) (nf[k].a - f) : f >= nf[k].b ? (double) (f - nf[k].b + 1) : 0.0;
                        if (d < bd) { bd = d; best = (int) k; }
                    }
                    m[(size_t) f] = nv[(size_t) best] ? 1.0f : 0.0f;
                }
            // 10 ms raised-cosine joins (a normalised Hann kernel; frames whose whole window agrees stay exactly 0 / 1)
            const int R = juce::jmax (1, (int) std::ceil (0.005 * fps));
            std::vector<double> ker ((size_t) (2 * R + 1)); double ks = 0;
            for (int j = -R; j <= R; ++j) { ker[(size_t) (j + R)] = 0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * j / (R + 1)); ks += ker[(size_t) (j + R)]; }
            C->vocal.assign ((size_t) N, 0.0f);
            for (int f = 0; f < N; ++f)
            {
                bool same = true; double acc = 0;
                for (int j = -R; j <= R; ++j)
                {
                    const float v = m[(size_t) juce::jlimit (0, N - 1, f + j)];
                    same &= v == m[(size_t) f]; acc += ker[(size_t) (j + R)] * v;
                }
                C->vocal[(size_t) f] = same ? m[(size_t) f] : (float) (acc / ks);
            }
        }
        C->srcF0.assign ((size_t) N, 0.0f);   // (the WIDE formant correction reads it too)
        for (int f = 0; f < N && f < track->frames(); ++f) C->srcF0[(size_t) f] = track->f0[(size_t) f];
    }
    Hasher h; h.add ("pcurve").add (C->hop).add (C->wfPerSf).add (N);
    for (int f = 0; f < N; ++f) h.add (C->cents[(size_t) f]).add (C->formant[(size_t) f]).add (C->baseHz[(size_t) f]);
    if (C->anyVocal()) { h.add ("vocal"); for (int f = 0; f < N; ++f) h.add (C->vocal[(size_t) f]); }
    if (! C->srcF0.empty()) { h.add ("f0"); for (int f = 0; f < N; ++f) h.add (C->srcF0[(size_t) f]); }
    C->hash = h.key();
    return C;
}

std::vector<NoteRef> notesOf (const Region& r, const analysis::Result* a)
{
    std::vector<NoteRef> out;
    std::vector<const PitchEdit*> ed; for (auto& e : r.pitch) if (e.isNote() && e.srcEnd > r.srcIn() && e.srcStart < r.srcOut()) ed.push_back (&e);
    for (auto* e : ed)
    {
        NoteRef n; n.regionId = r.id; n.s0 = e->srcStart; n.s1 = e->srcEnd; n.editId = e->id;
        n.centre = e->centre / 100.0; n.target = e->target() / 100.0; n.confidence = 1.0f;
        if (a != nullptr)
            for (auto& dn : a->notes) if (dn.s0 < n.s1 && dn.s1 > n.s0) { n.confidence = dn.confidence; break; }
        out.push_back (n);
    }
    if (a != nullptr)
        for (auto& dn : a->notes)
        {
            if (dn.s1 <= r.srcIn() || dn.s0 >= r.srcOut()) continue;
            bool covered = false; for (auto* e : ed) if (e->srcStart < dn.s1 && e->srcEnd > dn.s0) covered = true;
            if (covered) continue;
            NoteRef n; n.regionId = r.id; n.s0 = dn.s0; n.s1 = dn.s1; n.centre = n.target = dn.pitch; n.confidence = dn.confidence;
            out.push_back (n);
        }
    std::sort (out.begin(), out.end(), [] (const NoteRef& x, const NoteRef& y) { return x.s0 < y.s0; });
    return out;
}

std::vector<NoteRef> notesOf (const Project& p, const analysis::Result* a)
{
    std::vector<NoteRef> out;
    for (auto& r : p.regions) if (! r.muted) { auto v = notesOf (r, a); out.insert (out.end(), v.begin(), v.end()); }
    return out;
}

static const int kMaj[7] { 0, 2, 4, 5, 7, 9, 11 }, kMin[7] { 0, 2, 3, 5, 7, 8, 10 };
bool inScale (int midi, const Scale& s)
{
    if (s.chromatic) return true;
    const int d = (((midi - s.tonic) % 12) + 12) % 12;
    for (int k : (s.minor ? kMin : kMaj)) if (k == d) return true;
    return false;
}
double snapToScale (double mc, const Scale& s, int tieDir)
{
    const double m = mc / 100.0;
    const int lo = (int) std::floor (m) - 2, hi = (int) std::ceil (m) + 2;
    double best = std::round (m); double bd = 1e9;
    for (int k = lo; k <= hi; ++k)
    {
        if (! inScale (k, s)) continue;
        const double d = std::abs ((double) k - m);
        if (d < bd - 1e-9 || (std::abs (d - bd) <= 1e-9 && ((tieDir >= 0) == ((double) k > m)))) { bd = d; best = k; }
    }
    return best * 100.0;
}
juce::String scaleName (const Scale& s) { return s.chromatic ? juce::String ("CHROMATIC") : analysis::pitchClassName (s.tonic) + (s.minor ? " MIN" : " MAJ"); }
} // namespace pitchedit

// ---------------------------------------------------------------- ops
namespace ops
{
using pitchedit::NoteRef;

static PitchEdit* findEdit (Region& r, int64 id) { for (auto& e : r.pitch) if (e.id == id) return &e; return nullptr; }
static PitchEdit& ensureEdit (Project& p, Region& r, const NoteRef& n)
{
    if (n.editId != 0) if (auto* e = findEdit (r, n.editId)) return *e;
    for (auto& e : r.pitch) if (e.isNote() && e.srcStart == n.s0 && e.srcEnd == n.s1) return e;
    PitchEdit e; e.id = p.nextId++; e.srcStart = n.s0; e.srcEnd = n.s1; e.centre = n.centre * 100.0; e.cents = (n.target - n.centre) * 100.0;
    r.pitch.push_back (e);
    std::sort (r.pitch.begin(), r.pitch.end(), [] (const PitchEdit& a, const PitchEdit& b) { return a.srcStart < b.srcStart || (a.srcStart == b.srcStart && a.id < b.id); });
    for (auto& x : r.pitch) if (x.id == e.id) return x;
    return r.pitch.back();
}

R editNotes (const Project& in, int64 regionId, const std::vector<NoteRef>& notes, const std::function<void (PitchEdit&)>& f)
{
    Project p = in; auto* r = p.find (regionId); if (r == nullptr || notes.empty()) return std::nullopt;
    std::vector<int64> ids;
    for (auto& n : notes) if (n.regionId == regionId) ids.push_back (ensureEdit (p, *r, n).id);
    for (auto id : ids) if (auto* e = findEdit (*r, id)) f (*e);
    if (p == in) return std::nullopt;
    return p;
}

R shiftNotes (const Project& in, int64 regionId, const std::vector<NoteRef>& notes, double dc, bool snap)
{
    return editNotes (in, regionId, notes, [dc, snap] (PitchEdit& e)
    {
        double t = e.target() + dc;
        if (snap) t = std::round (t / 100.0) * 100.0;
        e.cents = juce::jlimit (-2400.0, 2400.0, t - e.centre);
    });
}

R splitNote (const Project& in, const NoteRef& n, int64 pos, const analysis::PitchTrack* track, juce::String* why)
{
    const int64 minLen = 1024;
    if (pos <= n.s0 + minLen || pos >= n.s1 - minLen) { if (why) *why = "Too close to the note's edge to split"; return std::nullopt; }
    Project p = in; auto* r = p.find (n.regionId); if (r == nullptr) return std::nullopt;
    PitchEdit left = ensureEdit (p, *r, n);
    r->pitch.erase (std::remove_if (r->pitch.begin(), r->pitch.end(), [&] (const PitchEdit& e) { return e.id == left.id; }), r->pitch.end());
    PitchEdit right = left; right.id = p.nextId++;
    left.srcEnd = pos; right.srcStart = pos;
    for (auto* e : { &left, &right })
    {
        const double tgt = e->target();
        if (track != nullptr) if (const double c = pitchedit::centreOf (*track, e->srcStart, e->srcEnd); c != 0) e->centre = c;
        e->cents = tgt - e->centre;   // each half keeps the target the note had (the halves are now free to move)
    }
    r->pitch.push_back (left); r->pitch.push_back (right);
    std::sort (r->pitch.begin(), r->pitch.end(), [] (const PitchEdit& a, const PitchEdit& b) { return a.srcStart < b.srcStart || (a.srcStart == b.srcStart && a.id < b.id); });
    return p;
}

R mergeNotes (const Project& in, const std::vector<NoteRef>& notesIn, const analysis::PitchTrack* track, juce::String* why)
{
    auto notes = notesIn;
    if (notes.size() < 2) { if (why) *why = "Select two or more neighbouring notes to MERGE"; return std::nullopt; }
    std::sort (notes.begin(), notes.end(), [] (const NoteRef& a, const NoteRef& b) { return a.s0 < b.s0; });
    for (auto& n : notes) if (n.regionId != notes[0].regionId) { if (why) *why = "MERGE works inside one region"; return std::nullopt; }
    Project p = in; auto* r = p.find (notes[0].regionId); if (r == nullptr) return std::nullopt;
    PitchEdit first = ensureEdit (p, *r, notes[0]);
    const double tgt = first.target();
    std::set<int64> drop;
    for (auto& n : notes) drop.insert (ensureEdit (p, *r, n).id);
    r->pitch.erase (std::remove_if (r->pitch.begin(), r->pitch.end(), [&] (const PitchEdit& e) { return drop.count (e.id) > 0; }), r->pitch.end());
    first.srcStart = notes.front().s0; first.srcEnd = 0; for (auto& n : notes) first.srcEnd = std::max (first.srcEnd, n.s1);
    if (track != nullptr) if (const double c = pitchedit::centreOf (*track, first.srcStart, first.srcEnd); c != 0) first.centre = c;
    first.cents = tgt - first.centre;
    r->pitch.push_back (first);
    std::sort (r->pitch.begin(), r->pitch.end(), [] (const PitchEdit& a, const PitchEdit& b) { return a.srcStart < b.srcStart || (a.srcStart == b.srcStart && a.id < b.id); });
    return p;
}

R resetNotes (const Project& in, const std::vector<NoteRef>& notes)
{
    Project p = in;
    for (auto& n : notes)
        if (auto* r = p.find (n.regionId))
            r->pitch.erase (std::remove_if (r->pitch.begin(), r->pitch.end(), [&] (const PitchEdit& e)
            {
                if (e.isNote()) return e.id == n.editId && n.editId != 0;
                return ! e.curve.empty() && e.curve.front().first < n.s1 && e.curve.back().first > n.s0;   // drawn curves over the note
            }), r->pitch.end());
    if (p == in) return std::nullopt;
    return p;
}

R snapNotes (const Project& in, const std::vector<NoteRef>& notes, const pitchedit::Scale& s, float minConf, int* count)
{
    Project p = in; int n = 0;
    for (auto& nr : notes)
    {
        if (nr.confidence < minConf || nr.centre <= 0) continue;
        auto* r = p.find (nr.regionId); if (r == nullptr) continue;
        auto& e = ensureEdit (p, *r, nr);
        const double t = pitchedit::snapToScale (e.centre, s, e.cents >= 0 ? 1 : -1);
        e.cents = t - e.centre; ++n;
    }
    if (count) *count = n;
    if (p == in) return std::nullopt;
    return p;
}

R addDraw (const Project& in, int64 regionId, std::vector<std::pair<int64, double>> pts, bool absolute, int64* newId, juce::String* why)
{
    Project p = in; auto* r = p.find (regionId); if (r == nullptr) return std::nullopt;
    std::sort (pts.begin(), pts.end());
    pts.erase (std::unique (pts.begin(), pts.end(), [] (const auto& a, const auto& b) { return a.first == b.first; }), pts.end());
    if (pts.size() < 2 || pts.back().first - pts.front().first < 256) { if (why) *why = "Draw a longer curve"; return std::nullopt; }
    PitchEdit e; e.id = p.nextId++; e.kind = PitchEditKind::Draw; e.absolute = absolute; e.curve = std::move (pts);
    e.srcStart = e.curve.front().first; e.srcEnd = e.curve.back().first;
    r->pitch.push_back (e);
    if (newId) *newId = e.id;
    return p;
}

R noteTiming (const Project& in, const NoteRef& n, double dS, double dE, juce::String* why)
{
    const auto* r0 = in.find (n.regionId); if (r0 == nullptr) return std::nullopt;
    if (r0->reverse) { if (why) *why = "TIMING works on forward regions"; return std::nullopt; }
    if (dS == 0 && dE == 0) return std::nullopt;
    std::optional<Project> q = in;
    auto markerAt = [&] (int64 s, int64& id) -> bool
    {
        const auto* r = q->find (n.regionId);
        if (s <= r->srcIn() || s >= r->srcOut()) { id = 0; return true; }   // the region's own edge
        for (auto& w : r->warp) if (std::abs ((double) (w.s - s)) < 48.0) { id = w.id; return true; }
        q = addMarker (*q, n.regionId, r->map().outAt ((double) s), &id, why);
        return q.has_value();
    };
    int64 mA = 0, mB = 0;
    if (! markerAt (std::max (n.s0, r0->srcIn()), mA) || ! markerAt (std::min (n.s1, r0->srcOut()), mB)) return std::nullopt;
    if ((dS != 0 && mA == 0) || (dE != 0 && mB == 0)) { if (why) *why = "The note sits on the region's edge: move or trim the region"; return std::nullopt; }
    auto tOf = [&] (int64 id) { for (auto& w : q->find (n.regionId)->warp) if (w.id == id) return w.t; return 0.0; };
    auto drag = [&] (int64 id, double d) { if (d == 0 || id == 0) return true; q = dragMarker (*q, n.regionId, id, tOf (id) + d, FlexMode::Move, why); return q.has_value(); };
    const bool endFirst = dE > 0;   // moving right: the end goes first, so the markers never cross
    if (endFirst ? ! (drag (mB, dE) && drag (mA, dS)) : ! (drag (mA, dS) && drag (mB, dE))) return std::nullopt;
    return q;
}
} // namespace ops
} // namespace gslice
