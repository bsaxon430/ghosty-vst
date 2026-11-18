#include "RenderPlan.h"
#include "Hold.h"
#include "Repeat.h"
#include "PitchEdit.h"

namespace gslice
{
static constexpr double kElasticPlateauS = 0.12;   // ELASTIC: the 1x plateau before the out join (seconds; at most half the stable region after the ramp)

const Piece* Plan::at (int64 dst) const
{
    auto it = std::upper_bound (pieces.begin(), pieces.end(), dst, [] (int64 d, const Piece& p) { return d < p.dst0; });
    if (it == pieces.begin()) return nullptr;
    --it; return dst < it->dst1 ? &*it : nullptr;
}

WarpMap transientLock (const WarpMap& m, const std::vector<double>& onsets, double pre, double post, int* locked, std::vector<std::pair<double, double>>* spansOut)
{
    if (locked) *locked = 0;
    if (spansOut) spansOut->clear();
    if (onsets.empty() || m.t.size() < 2) return m;
    WarpMap o; o.t.push_back (m.t[0]); o.s.push_back (m.s[0]);
    const double lockLen = pre + post;
    for (size_t i = 0; i + 1 < m.t.size(); ++i)
    {
        const double t0 = m.t[i], t1 = m.t[i + 1], s0 = m.s[i], s1 = m.s[i + 1], r = (s1 - s0) / (t1 - t0);
        std::vector<std::pair<double, double>> spans;   // locked source spans inside this segment
        if (std::abs (r - 1.0) > 0.01 && (s1 - s0) > 3.0 * lockLen)
            for (double e : onsets)
            {
                double a = e - pre, b = e + post;
                if (a <= s0 + 1 || b >= s1 - 1) continue;
                if (! spans.empty() && a < spans.back().second + lockLen) continue;   // keep spans apart
                spans.push_back ({ a, b });
            }
        if (! spans.empty())
        {
            double lockedS = 0; for (auto& sp : spans) lockedS += sp.second - sp.first;
            const double restS = (s1 - s0) - lockedS, restT = (t1 - t0) - lockedS;
            const double r2 = restT > 0 ? restS / restT : 0;
            if (restT <= 0 || r2 < limits::kHardMin || r2 > limits::kHardMax) spans.clear();   // can't absorb it: leave the segment as it is
            else
            {
                double t = t0, sPos = s0;
                for (auto& sp : spans)
                {
                    t += (sp.first - sPos) / r2; o.t.push_back (t); o.s.push_back (sp.first);
                    const double ta = t;
                    t += sp.second - sp.first; o.t.push_back (t); o.s.push_back (sp.second);
                    if (spansOut) spansOut->push_back ({ ta, t });
                    sPos = sp.second;
                }
                if (locked) *locked += (int) spans.size();
            }
        }
        o.t.push_back (t1); o.s.push_back (s1);
    }
    if (! o.valid()) { if (spansOut) spansOut->clear(); if (locked) *locked = 0; return m; }
    return o;
}

static void finishPlan (Plan& plan)
{
    std::sort (plan.pieces.begin(), plan.pieces.end(), [] (const Piece& a, const Piece& b) { return a.dst0 < b.dst0; });
    Hasher h; h.add ("plan").add (kEngineVersion).add (plan.hostRate).add (plan.pitchSemis).add (plan.formantSemis);
    if (plan.tonalityHz != 8000.0 || plan.formantBaseHz != 0.0) h.add ("voice").add (plan.tonalityHz).add (plan.formantBaseHz);   // (default plans keep their old keys)
    plan.length = 0; plan.extremeCount = 0;
    for (auto& p : plan.pieces) { h.add (p.fingerprint); plan.length = std::max (plan.length, p.dst1); plan.extremeCount += p.extreme ? 1 : 0; }
    plan.fingerprint = h.hex();
}

static void keys (Piece& pc, const juce::String& srcFp, const Plan& plan)
{
    Hasher lv; lv.add ("live").add (srcFp).add (pc.regionId).add (pc.dst0).add (pc.u0).add (pc.reverse).add (pc.mirror); pc.map.hashInto (lv);
    if (pc.repeatIndex >= 0 || pc.holdMode != 0) lv.add ("rh").add (pc.repeatIndex).add (pc.holdMode);   // (unchanged keys for plain pieces)
    pc.liveKey = lv.key();
    Hasher st; st.add ("studio").add (kEngineVersion).add (srcFp).add (plan.hostRate).add (pc.reverse).add (pc.mirror).add (pc.u0).add (pc.length())
                 .add (pc.stretch).add (plan.pitchSemis).add (plan.formantSemis); pc.map.hashInto (st);
    if (plan.tonalityHz != 8000.0 || plan.formantBaseHz != 0.0) st.add ("voice").add (plan.tonalityHz).add (plan.formantBaseHz);
    st.add ((int) pc.splices.size()); for (auto& sp : pc.splices) st.add (sp.first).add (sp.second);
    if (pc.pitchOffset != 0.0) st.add ("pitchOffset").add (pc.pitchOffset);
    if (pc.formantOffset != 0.0) st.add ("formantOffset").add (pc.formantOffset);
    if (pc.voiceOwn) st.add ("rvoice").add (pc.tonalityHz).add (pc.formantBaseHz);
    if (pc.hold != nullptr) st.add ("hold").add (pc.holdMode).addU (pc.hold->hash);
    if (pc.curve != nullptr) st.add ("curve").addU (pc.curve->hash).add (pc.tonalityHz).add (pc.formantBaseHz);
    pc.studioKey = st.key();
    Hasher f; f.addU (pc.studioKey).add (pc.dst0).add (pc.dst1).add (pc.regionLen).add (pc.gain).add (pc.fadeIn).add (pc.fadeOut).add (pc.edgeIn).add (pc.edgeOut);
    if (pc.repeatIndex >= 0) f.add ("rep").add (pc.repeatIndex).add (pc.repeatId).add (pc.fixedEdges);
    pc.fingerprint = f.hex();
}

Plan compile (const Project& p, const SourceAudio* src, const CompileOptions& o)
{
    Plan plan; plan.hostRate = o.hostRate;
    plan.pitchSemis = p.totalPitchSemis(); plan.formantSemis = p.formantSemis;
    const bool pitched = std::abs (plan.pitchSemis) > 1.0e-9 || std::abs (plan.formantSemis) > 1.0e-9;
    if (pitched) { plan.tonalityHz = juce::jlimit (500.0, 24000.0, p.tonalityHz); plan.formantBaseHz = std::max (0.0, o.formantBaseHz); }   // (unpitched: they change nothing)
    const double srcRate = src != nullptr ? src->sampleRate : (p.source.sampleRate > 0 ? p.source.sampleRate : p.sampleRate);
    const double kT = o.hostRate / p.sampleRate;   // HF per PF
    const double kS = o.hostRate / srcRate;        // WF per SF
    const int64 edge = (int64) std::llround (o.edgeFadeMs * 0.001 * o.hostRate);
    std::vector<double> onsetsWF;
    if (src != nullptr && p.transientLock)
        for (size_t i = 0; i < src->onsets.size(); ++i) if (src->onsetStrength[i] >= o.lockMinStrength) onsetsWF.push_back ((double) src->onsets[i] * kS);

    // each region's output span in HF and its map
    struct HSeg { const HoldOp* op; int64 u0, u1; double sA, sB; bool in1x, out1x; };   // a HOLD segment (region-local HF, WF)
    struct RS { const Region* r; int64 d0, len; WarpMap map; bool stretch, extreme; int locked; double mirror; std::vector<std::pair<double, double>> splices; std::vector<HSeg> holds; std::shared_ptr<const PitchCurve> curve;
                double rp = 0, rf = 0; bool rPitched = false; };   // the region's own PITCH (semitones) / FORMANT
    std::vector<RS> rs;
    for (auto& r : p.regions)
    {
        if (r.muted || ! r.valid()) continue;
        RS x { &r, (int64) std::llround (r.dstStart * kT), (int64) std::llround (r.length() * kT), {}, false, false, 0, 0, {}, {}, pitchedit::build (r, o.pitchTrack, kS, srcRate, (PitchEngine) juce::jlimit (0, 2, p.pitchEngine), plan.pitchSemis * 100.0) };
        if (x.len <= 0) continue;
        x.rp = r.totalPitchSemis(); x.rf = r.formantSemis; x.rPitched = std::abs (x.rp) > 1.0e-9 || std::abs (x.rf) > 1.0e-9;
        const double scaleT = (double) x.len / r.length();   // the map's last t is exactly the HF length
        WarpMap m;
        for (auto& w : r.warp) { m.t.push_back (w.t * scaleT); m.s.push_back ((double) w.s * kS); }
        m.t.back() = (double) x.len;
        if (! m.valid()) continue;
        x.extreme = false;
        for (size_t i = 0; i + 1 < m.t.size(); ++i) if (limits::extreme (m.rateOf (i)) && r.holdOfSegment (i) == nullptr) x.extreme = true;
        // GHOST HOLD segments (forward regions only): where they sit before the transient lock adds its knots
        auto lockOnsets = onsetsWF;
        if (! r.reverse)
            for (size_t i = 0; i + 1 < m.t.size(); ++i)
                if (const auto* h = r.holdOfSegment (i))
                {
                    HSeg hs { h, (int64) std::llround (m.t[i]), (int64) std::llround (m.t[i + 1]), m.s[i], m.s[i + 1],
                              i > 0 && std::abs (m.rateOf (i - 1) - 1.0) < 1e-6, i + 2 < m.t.size() && std::abs (m.rateOf (i + 1) - 1.0) < 1e-6 };
                    if (hs.u1 > hs.u0) x.holds.push_back (hs);
                    lockOnsets.erase (std::remove_if (lockOnsets.begin(), lockOnsets.end(), [&] (double e) { return e > m.s[i] - 0.05 * o.hostRate && e < m.s[i + 1] + 0.05 * o.hostRate; }), lockOnsets.end());
                }
        if (! lockOnsets.empty() && ! m.unity (1e-6)) m = transientLock (m, lockOnsets, o.lockPreMs * 0.001 * o.hostRate, o.lockPostMs * 0.001 * o.hostRate, &x.locked, &x.splices);
        if (r.reverse || pitched || x.rPitched || x.curve != nullptr) x.splices.clear();   // a reversed attack is no attack; pitched audio can't take the unpitched original
        if (r.reverse)
        {   // mirrored coordinates: W'(u) = (A + B) - W(len - u), increasing; the reader mirrors the index back
            const double A = m.s.front(), B = m.s.back(); x.mirror = A + B;
            WarpMap mm; const size_t n = m.t.size();
            for (size_t k = 0; k < n; ++k) { mm.t.push_back ((double) x.len - m.t[n - 1 - k]); mm.s.push_back (x.mirror - m.s[n - 1 - k]); }
            mm.t.front() = 0; m = mm;
        }
        x.stretch = pitched || x.rPitched || ! m.unity (1e-6);
        x.map = std::move (m);
        rs.push_back (std::move (x));
    }
    // top wins: walk from the top region down, each one keeps only what is not yet covered
    std::vector<std::pair<int64, int64>> covered;   // sorted, disjoint
    struct Part { size_t ri; int64 a, b; };
    std::vector<Part> parts;
    for (size_t k = rs.size(); k-- > 0;)
    {
        const int64 a = rs[k].d0, b = rs[k].d0 + rs[k].len;
        int64 pos = a;
        for (auto& c : covered)
        {
            if (c.second <= pos) continue;
            if (c.first >= b) break;
            if (c.first > pos) parts.push_back ({ k, pos, std::min (b, c.first) });
            pos = std::max (pos, c.second);
            if (pos >= b) break;
        }
        if (pos < b) parts.push_back ({ k, pos, b });
        covered.push_back ({ a, b });
        std::sort (covered.begin(), covered.end());
        std::vector<std::pair<int64, int64>> merged;
        for (auto& c : covered) { if (! merged.empty() && c.first <= merged.back().second) merged.back().second = std::max (merged.back().second, c.second); else merged.push_back (c); }
        covered.swap (merged);
    }
    std::map<int64, int> partNo;
    for (auto& pt : parts)
    {
        const auto& x = rs[pt.ri];
        Piece pc; pc.regionId = x.r->id; pc.part = partNo[x.r->id]++;
        pc.dst0 = pt.a; pc.dst1 = pt.b; pc.u0 = pt.a - x.d0; pc.regionLen = x.len;
        pc.map = x.map; pc.reverse = x.r->reverse; pc.mirror = x.mirror; pc.stretch = x.stretch; pc.extreme = x.extreme; pc.lockedOnsets = x.locked; pc.splices = x.splices;
        pc.tonalityHz = plan.tonalityHz; pc.formantBaseHz = plan.formantBaseHz;
        if (x.rPitched)
        {   // the region's own PITCH / FORMANT ride on the global ones (the stream's pitch = global + offset)
            pc.pitchOffset = x.rp; pc.formantOffset = x.rf;
            if (! pitched) { pc.tonalityHz = juce::jlimit (500.0, 24000.0, p.tonalityHz); pc.formantBaseHz = std::max (0.0, o.formantBaseHz); pc.voiceOwn = true; }
        }
        pc.gain = (float) juce::Decibels::decibelsToGain (x.r->gainDb, -100.0);
        if (x.r->gainDb == 0.0) pc.gain = 1.0f;
        pc.fadeIn = (int64) std::llround (x.r->fadeInMs * 0.001 * o.hostRate); pc.fadeOut = (int64) std::llround (x.r->fadeOutMs * 0.001 * o.hostRate);
        // GHOST PITCH: the region's curve on every plain piece it touches (they go through the stretcher, with the tonality)
        auto attachCurve = [&] (Piece& q)
        {
            if (x.curve == nullptr || q.hold != nullptr) return;
            double a = q.map.srcAt ((double) q.u0), b = q.map.srcAt ((double) (q.u0 + q.length()));
            if (q.reverse) { const double ra = q.mirror - b, rb = q.mirror - a; a = ra; b = rb; }
            if (! x.curve->activeIn (a, b)) return;
            q.curve = x.curve; q.stretch = true; q.splices.clear();
            if (! pitched && ! x.rPitched) { q.tonalityHz = juce::jlimit (500.0, 24000.0, p.tonalityHz); q.formantBaseHz = std::max (0.0, o.formantBaseHz); }
        };
        if (x.holds.empty()) { attachCurve (pc); plan.pieces.push_back (std::move (pc)); continue; }
        // HOLD: the part splits at its held segments (head | held | tail); the head and tail stay plain (DIRECT at 1x)
        std::vector<std::pair<int64, int64>> cuts;   // region-local [u0, u1) of the held parts inside this part
        const int64 pu0 = pc.u0, pu1 = pc.u0 + pc.length();
        auto plain = [&] (int64 a, int64 b)
        {
            if (b <= a) return;
            Piece q = pc; q.part = partNo[x.r->id]++; q.u0 = a; q.dst0 = pc.dst0 + (a - pu0); q.dst1 = q.dst0 + (b - a);
            bool unity = ! pitched && ! x.rPitched;
            for (size_t i = 0; i + 1 < x.map.t.size() && unity; ++i)
                if (x.map.t[i + 1] > (double) a && x.map.t[i] < (double) b && std::abs (x.map.rateOf (i) - 1.0) > 1e-6) unity = false;
            q.stretch = ! unity;
            attachCurve (q);
            plan.pieces.push_back (std::move (q));
        };
        int64 pos = pu0;
        for (auto& hs : x.holds)
        {
            const int64 a = std::max (pos, hs.u0), b = std::min (pu1, hs.u1);
            if (b <= a) continue;
            plain (pos, a);
            Piece q = pc; q.part = partNo[x.r->id]++; q.u0 = a; q.dst0 = pc.dst0 + (a - pu0); q.dst1 = q.dst0 + (b - a);
            q.stretch = true; q.extreme = false; q.splices.clear();
            q.holdMode = (int) hs.op->mode + 1;
            q.hold = hold::makePlan (src, kS, o.hostRate, *hs.op, hs.u0, hs.u1 - hs.u0, hs.sA, hs.sB, (double) hs.op->selA * kS, (double) hs.op->selB * kS,
                                     hs.in1x, hs.out1x, pitched || x.rPitched);
            if (q.hold->mode == HoldMode::Elastic && (q.hold->joinIn || q.hold->joinOut))
            {   // ELASTIC eases into (and out of) the stretch over 60 ms: at the joins the stretcher runs at ~1x, so its crossfade
                // with the source meets the same pitch and timing
                // (+ a 1x plateau before the out join: the stretcher has settled back on the source for its window length when the
                // crossfade into the tail starts, so the vibrato / pitch at the join is the source's - the in join gets the same
                // from a 1x pre-roll over the head, hold::elasticPreroll)
                const double len = (double) (hs.u1 - hs.u0), S = hs.sB - hs.sA, ramp = std::min (0.06 * o.hostRate, len / 4.0);
                const double plat = q.hold->joinOut ? std::max (0.0, std::min ({ kElasticPlateauS * o.hostRate, (S - ramp) * 0.5, len / 4.0 })) : 0.0;
                const double rm = (S - ramp - plat) / (len - ramp - plat);
                if (rm > 0 && ramp > 64)
                {
                    WarpMap nm; const auto& m0 = q.map;
                    for (size_t i = 0; i < m0.t.size(); ++i) if (m0.t[i] < (double) hs.u0 - 1e-9) { nm.t.push_back (m0.t[i]); nm.s.push_back (m0.s[i]); }
                    double t = (double) hs.u0, sv = hs.sA; nm.t.push_back (t); nm.s.push_back (sv);
                    auto seg = [&] (double dt, double rate) { t += dt; sv += dt * rate; nm.t.push_back (t); nm.s.push_back (sv); };
                    for (int k = 0; k < 4; ++k) seg (ramp / 4, 1.0 + (rm - 1.0) * (k + 0.5) / 4.0);
                    seg (len - 2 * ramp - plat, rm);
                    for (int k = 3; k >= 0; --k) seg (ramp / 4, 1.0 + (rm - 1.0) * (k + 0.5) / 4.0);
                    if (plat > 1.0) seg (plat, 1.0);
                    nm.t.back() = (double) hs.u1; nm.s.back() = hs.sB;
                    for (size_t i = 0; i < m0.t.size(); ++i) if (m0.t[i] > (double) hs.u1 + 1e-9) { nm.t.push_back (m0.t[i]); nm.s.push_back (m0.s[i]); }
                    if (nm.valid()) q.map = nm;
                }
            }
            plan.pieces.push_back (std::move (q));
            pos = b;
        }
        plain (pos, pu1);
    }
    std::sort (plan.pieces.begin(), plan.pieces.end(), [] (const Piece& a, const Piece& b) { return a.dst0 < b.dst0; });
    // GHOST REPEAT spans (on top of everything)
    repeat::expand (plan.pieces, p, o.hostRate, o.bpm > 0 ? o.bpm : p.bpm);
    // anti-click edge fades, except where the neighbour continues the audio seamlessly (a split at 1x plays as one)
    auto seamless = [] (const Piece& l, const Piece& r)
    {
        // a HOLD's joins: the held piece starts / ends on the source itself where its neighbour is the same region at 1x
        if (l.dst1 == r.dst0 && l.regionId == r.regionId && l.u0 + l.length() == r.u0 && l.gain == r.gain && l.repeatIndex < 0 && r.repeatIndex < 0)
        {
            if (r.hold != nullptr && l.hold == nullptr && ! l.stretch && r.u0 == r.hold->uStart && r.hold->joinIn) return true;
            if (l.hold != nullptr && r.hold == nullptr && ! r.stretch && l.u0 + l.length() == l.hold->uStart + l.hold->len && l.hold->joinOut) return true;
        }
        if (l.repeatIndex >= 0 || r.repeatIndex >= 0) return false;   // a REPEAT's repetitions fade on their own
        if (l.dst1 != r.dst0 || l.stretch || r.stretch || l.reverse != r.reverse || l.gain != r.gain) return false;
        if (l.fadeOut > 0 && l.u0 + l.length() >= l.regionLen - l.fadeOut) return false;
        if (r.fadeIn > 0 && r.u0 < r.fadeIn) return false;
        // source continuity: the left's next source frame is the right's first
        const double sl = l.map.srcAt ((double) (l.u0 + l.length())), sr = r.map.srcAt ((double) r.u0);
        const double al = l.reverse ? l.mirror - sl : sl, ar = r.reverse ? r.mirror - sr : sr;
        return std::abs (al - ar) < 0.5;
    };
    for (size_t i = 0; i < plan.pieces.size(); ++i)
    {
        auto& pc = plan.pieces[i];
        if (pc.fixedEdges) { keys (pc, p.source.fingerprint, plan); continue; }   // a REPEAT's own fades
        const bool prevOk = i > 0 && seamless (plan.pieces[i - 1], pc);
        const bool nextOk = i + 1 < plan.pieces.size() && seamless (pc, plan.pieces[i + 1]);
        const int64 maxEdge = pc.length() / 2;
        // a piece that starts at its region's start with a user fade-in needs no extra edge fade there (same for the end)
        pc.edgeIn = prevOk || (pc.u0 == 0 && pc.fadeIn >= edge) ? 0 : std::min (edge, maxEdge);
        pc.edgeOut = nextOk || (pc.u0 + pc.length() == pc.regionLen && pc.fadeOut >= edge) ? 0 : std::min (edge, maxEdge);
        // the file's own first / last frame at a region edge is not a cut: it plays exactly as the file does (1x nulls to the source)
        if (! pc.stretch && ! pc.reverse)
        {
            const double wfEnd = (double) p.source.frames * kS;
            auto actual = [&] (double u) { const double v = pc.map.srcAt (u); return pc.reverse ? pc.mirror - v : v; };
            if (pc.u0 == 0 && ! prevOk && std::abs (actual (0) - (pc.reverse ? wfEnd : 0.0)) < 0.5 && pc.dst0 == 0) pc.edgeIn = 0;
            if (pc.u0 + pc.length() == pc.regionLen && std::abs (actual ((double) pc.regionLen) - (pc.reverse ? 0.0 : wfEnd)) < 0.5) pc.edgeOut = 0;
        }
        keys (pc, p.source.fingerprint, plan);
    }
    finishPlan (plan);
    return plan;
}

Plan compileOriginal (const Project& p, const SourceAudio* src, const CompileOptions& o)
{
    Project q = p; q.regions.clear(); q.repeats.clear(); q.pitchSemis = q.pitchCents = q.formantSemis = 0; q.transientLock = false;
    const auto r = makeProject (p.source).regions;
    q.regions = r;
    auto plan = compile (q, src, o);
    for (auto& pc : plan.pieces) { pc.regionId = -1; keys (pc, p.source.fingerprint + "/original", plan); }
    finishPlan (plan);
    return plan;
}
} // namespace gslice
