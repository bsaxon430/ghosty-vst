// SPDX-License-Identifier: AGPL-3.0-or-later
// 1 GHOST GATE: 16 drawn gate patterns (GATE-12's curve engine: its point types and tension, baked into a table on the message thread),
// a 16-step time lane read from TIME-12's buffer, swing, host-synced or audio-triggered (GATE-12's transient detector, with 5 ms
// lookahead), click-free edges, and the PUMP (rhythmic, drawn or input-keyed ducking). The pattern data is saved with the project.
#pragma once
#include "RhythmCore.h"
#include "BendLane.h"
#include "../effects/ColourSst.h"

namespace phantom
{
namespace gatedata
{
constexpr int kPatterns = 16, kSteps = 16, kTab = 1024;
// a point of a drawn pattern: x 0..1 in the pattern, level 0..1 (1 = open), tension -1..1, type = GATE-12 PointType (Hold, Curve, S-Curve,
// Pulse, Wave, Triangle, Stairs, Smooth Stairs, Half Sine) of the segment that starts at this point
struct Pt { float x = 0, level = 1, tension = 0; int type = 1; };
struct Pattern { juce::String name; std::vector<Pt> pts { { 0, 1, 0, 1 }, { 1, 1, 0, 1 } }; std::array<uint8_t, kSteps> steps {}; };
inline const char* pointTypeName (int t) { static const char* n[] { "HOLD", "CURVE", "S-CURVE", "PULSE", "WAVE", "TRIANGLE", "STAIRS", "SMOOTH STAIRS", "HALF SINE" }; return n[juce::jlimit (0, 8, t)]; }

inline Pattern factory (int i)
{
    using namespace rhythm;
    Pattern p;
    auto pulse = [&p] (float x0, float x1, float lo, float hi, float edge = 0.0025f)
    { p.pts.push_back ({ x0, lo, 0, 1 }); p.pts.push_back ({ x0 + edge, hi, 0, 1 }); p.pts.push_back ({ x1 - edge, hi, 0, 1 }); p.pts.push_back ({ x1, lo, 0, 1 }); };
    auto pulses = [&] (const char* name, int n, float duty, const char* mask = nullptr)
    {
        p.name = name; p.pts.clear(); p.pts.push_back ({ 0, 0, 0, 1 });
        for (int k = 0; k < n; ++k)
        {
            if (mask != nullptr && mask[k] == '0') continue;
            const float hi = (mask != nullptr && mask[k] == '2') ? 0.6f : 1.0f, x0 = (float) k / (float) n + 0.0001f;
            pulse (x0, juce::jmin (0.9999f, x0 + duty / (float) n), 0.0f, hi);
        }
        p.pts.push_back ({ 1, 0, 0, 1 });
    };
    auto steps = [&p] (std::initializer_list<int> idx, int m) { for (int s : idx) p.steps[(size_t) s] = (uint8_t) m; };
    switch (i)
    {
        case 0: p.name = "FULL"; break;
        case 1: pulses ("8TH GATE", 8, 0.5f); break;
        case 2: pulses ("16TH GATE", 16, 0.5f); break;
        case 3: pulses ("TRANCE", 16, 0.62f, "1011011010110111"); break;
        case 4: p.name = "PUMP"; p.pts.clear(); for (int b = 0; b < 4; ++b) { const float x = b / 4.0f; p.pts.push_back ({ x + (b ? 0.002f : 0.0f), 0.05f, -0.55f, 1 }); p.pts.push_back ({ x + 0.17f, 1, 0, 1 }); } p.pts.push_back ({ 1, 1, 0, 1 }); break;
        case 5: p.name = "HALF-TIME"; for (auto& s : p.steps) s = tmHalf; break;
        case 6: p.name = "REV TAIL"; steps ({ 12, 13, 14, 15 }, tmReverse); break;
        case 7: p.name = "STUTTER FILL"; steps ({ 12, 13 }, tmRepeat); steps ({ 14, 15 }, tmStutter); break;
        case 8: p.name = "TAPE STOP"; steps ({ 12, 13, 14, 15 }, tmStop); p.pts = { { 0, 1, 0, 1 }, { 0.75f, 1, 0.4f, 1 }, { 1, 0.2f, 0, 1 } }; break;
        case 9: p.name = "SCRATCH"; steps ({ 3, 7, 11, 14, 15 }, tmScratch); break;
        case 10: pulses ("DOTTED", 16, 1.6f, "1001001001001000"); break;
        case 11: p.name = "SWING"; p.pts.clear(); p.pts.push_back ({ 0, 0, 0, 1 }); for (int k = 0; k < 8; ++k) { const float x0 = k / 8.0f + (k % 2 ? 0.045f : 0.0005f); pulse (x0, x0 + (k % 2 ? 0.045f : 0.07f), 0, k % 2 ? 0.75f : 1.0f); } p.pts.push_back ({ 1, 0, 0, 1 }); break;
        case 12: p.name = "SWELL"; p.pts.clear(); for (int b = 0; b < 2; ++b) { const float x = b * 0.5f; p.pts.push_back ({ x + (b ? 0.003f : 0.0f), 0, 0.6f, 1 }); p.pts.push_back ({ x + 0.497f, 1, 0, 1 }); } p.pts.push_back ({ 1, 1, 0, 1 }); break;
        case 13: p.name = "HALF + REV"; steps ({ 0, 1, 2, 3, 4, 5, 6, 7 }, tmHalf); steps ({ 12, 13, 14, 15 }, tmReverse); break;
        case 14: { pulses ("GLITCH", 16, 0.7f, "1101101111011010"); const int m[16] { 0, 0, 2, 0, 4, 0, 1, 1, 0, 3, 3, 0, 5, 2, 4, 6 }; for (int k = 0; k < 16; ++k) p.steps[(size_t) k] = (uint8_t) m[k]; break; }
        default: p.name = "WAVE"; p.pts = { { 0, 1, 0, 4 }, { 1, 1, 0, 1 } }; break;   // GATE-12's own wave segment
    }
    // Ghost Box's curve exponent was 5^t; GATE-12's is 1.1^(50 |t|): the same bends at t * 0.34
    for (auto& q : p.pts) if (q.type == 1) q.tension *= 0.34f;
    if (i == 15) p.pts[0].tension = 0.25f;   // WAVE: a few cycles across the bar
    return p;
}

constexpr int kMaxSegments = 256, kMaxPoints = 240;
// GATE-12's own segments (its Pattern builds them, with its wrap-around ghost points); message thread
inline int segmentsOf (const Pattern& src, gate12::Segment* out)
{
    gate12::Pattern g (0);
    for (const auto& q : src.pts) g.insertPoint (juce::jlimit (0.0, 1.0, (double) q.x), 1.0 - juce::jlimit (0.0, 1.0, (double) q.level), juce::jlimit (-1.0, 1.0, (double) q.tension), juce::jlimit (0, 8, q.type), false);
    g.sortPoints(); g.buildSegments();
    const auto segs = g.getSegments(); const int n = juce::jmin (kMaxSegments, (int) segs.size());
    for (int i = 0; i < n; ++i) out[i] = segs[(size_t) i];
    return n;
}
// audio thread: GATE-12's get_y_at (same search, same per-type curve functions of a Pattern that only this thread uses), without its lock
inline double levelAt (gate12::Pattern& ev, const gate12::Segment* segs, int n, double x) noexcept
{
    int low = 0, high = n - 1;
    while (low <= high)
    {
        const int mid = (low + high) / 2; const auto& seg = segs[mid];
        if (x < seg.x1) high = mid - 1;
        else if (x > seg.x2) low = mid + 1;
        else
        {
            double y;
            switch (seg.type)
            {
                case gate12::PointType::Hold: y = seg.y1; break;
                case gate12::PointType::Curve: y = ev.get_y_curve (seg, x); break;
                case gate12::PointType::SCurve: y = ev.get_y_scurve (seg, x); break;
                case gate12::PointType::Pulse: y = ev.get_y_pulse (seg, x); break;
                case gate12::PointType::Wave: y = ev.get_y_wave (seg, x); break;
                case gate12::PointType::Triangle: y = ev.get_y_triangle (seg, x); break;
                case gate12::PointType::Stairs: y = ev.get_y_stairs (seg, x); break;
                case gate12::PointType::SmoothSt: y = ev.get_y_smooth_stairs (seg, x); break;
                case gate12::PointType::HalfSine: y = ev.get_y_half_sine (seg, x); break;
                default: y = 0.0; break;
            }
            return juce::jlimit (0.0, 1.0, 1.0 - y);
        }
    }
    return 1.0;
}

// GATE-12's Pattern evaluates the curve (it draws upside down: y 0 = top, so level = 1 - y); called on the message thread only
inline void bake (const Pattern& src, float* table)
{
    gate12::Pattern g (0);
    for (const auto& q : src.pts) g.insertPoint (juce::jlimit (0.0, 1.0, (double) q.x), 1.0 - juce::jlimit (0.0, 1.0, (double) q.level), juce::jlimit (-1.0, 1.0, (double) q.tension), juce::jlimit (0, 8, q.type), false);
    g.sortPoints(); g.buildSegments();
    for (int k = 0; k <= kTab; ++k) table[k] = (float) juce::jlimit (0.0, 1.0, 1.0 - g.get_y_at ((double) k / kTab));
}

inline juce::var toVar (const Pattern& p)
{
    auto* o = new juce::DynamicObject(); o->setProperty ("name", p.name);
    juce::Array<juce::var> pts; for (auto& q : p.pts) pts.add (juce::Array<juce::var> { q.x, q.level, q.tension, q.type });
    juce::Array<juce::var> st; for (auto s : p.steps) st.add ((int) s);
    o->setProperty ("pts", pts); o->setProperty ("steps", st); return juce::var (o);
}
inline Pattern fromVar (const juce::var& v, int index)
{
    Pattern p = factory (index); if (! v.isObject()) return p;
    p.name = v["name"].toString();
    if (auto* a = v["pts"].getArray(); a != nullptr && a->size() >= 2 && a->size() <= kMaxPoints)
    {
        p.pts.clear();
        for (auto& e : *a) if (auto* q = e.getArray(); q != nullptr && q->size() >= 4)
            p.pts.push_back ({ juce::jlimit (0.0f, 1.0f, (float) (*q)[0]), juce::jlimit (0.0f, 1.0f, (float) (*q)[1]), juce::jlimit (-1.0f, 1.0f, (float) (*q)[2]), juce::jlimit (0, 8, (int) (*q)[3]) });
        std::sort (p.pts.begin(), p.pts.end(), [] (const Pt& a, const Pt& b) { return a.x < b.x; });
        if (p.pts.size() < 2) p = factory (index);
    }
    if (auto* s = v["steps"].getArray()) for (int k = 0; k < kSteps && k < s->size(); ++k) p.steps[(size_t) k] = (uint8_t) juce::jlimit (0, rhythm::kNumTimeModes - 1, (int) (*s)[k]);
    return p;
}
} // namespace gatedata

class GhostGateFx final : public CatalogEffect
{
public:
    struct Baked { std::array<std::array<gate12::Segment, gatedata::kMaxSegments>, gatedata::kPatterns> segs {}; std::array<int, gatedata::kPatterns> nseg {}; std::array<std::array<uint8_t, gatedata::kSteps>, gatedata::kPatterns> steps {}; bendlane::Shape pump; };
    enum PumpMode { pmOff, pmFloor, pmHalf, pmFast, pmDrawn, pmSidechain };

    GhostGateFx() : CatalogEffect (uGate)
    {
        for (int i = 0; i < gatedata::kPatterns; ++i) model[(size_t) i] = gatedata::factory (i);
        pumpShape = bendlane::pumpDefault();
        Baked b; bakeInto (b); published.publishAll (b);
    }
    bool isRhythmic() const override { return true; }

    int getLatencySamples() const override { return lookOn ? lookSamples : 0; }

    // ---- drawn data (message thread)
    bool hasData() const override { return true; }
    juce::var getData() const override
    {
        juce::Array<juce::var> pats; for (auto& p : model) pats.add (gatedata::toVar (p));
        auto* o = new juce::DynamicObject(); o->setProperty ("patterns", pats); o->setProperty ("pump", pumpShape.toString()); return juce::var (o);
    }
    void setData (const juce::var& v) override
    {
        for (int i = 0; i < gatedata::kPatterns; ++i) model[(size_t) i] = gatedata::factory (i);
        pumpShape = bendlane::pumpDefault();
        if (auto* a = v["patterns"].getArray()) for (int i = 0; i < gatedata::kPatterns && i < a->size(); ++i) model[(size_t) i] = gatedata::fromVar ((*a)[i], i);
        if (v["pump"].isString()) pumpShape = bendlane::Shape::fromString (v["pump"].toString());
        bakeInto (published.edit()); published.publish();
    }
    const gatedata::Pattern& pattern (int i) const { return model[(size_t) juce::jlimit (0, gatedata::kPatterns - 1, i)]; }
    void setPattern (int i, const gatedata::Pattern& p) { model[(size_t) juce::jlimit (0, gatedata::kPatterns - 1, i)] = p; bakeInto (published.edit()); published.publish(); }
    const bendlane::Shape& pump() const { return pumpShape; }
    void setPump (const bendlane::Shape& s) { pumpShape = s; pumpShape.sanitise(); bakeInto (published.edit()); published.publish(); }

    // ---- what the editor shows (audio thread writes, UI reads)
    std::atomic<float> uiPos { -1.0f }, uiPump { 1.0f }; std::atomic<int> uiPattern { 0 };

    void updateParameters (const ModulationContext& ctx) override
    {
        CatalogEffect::updateParameters (ctx);
        lookOn = on (iLook) && (choice (iTrig) == 1 || choice (iPmode) == pmSidechain);
    }

protected:
    void prepareEffect (double sampleRate, int, int) override
    {
        iPattern = id ("pattern"); iLen = id ("length"); iSmooth = id ("smooth"); iTime = id ("time"); iPmode = id ("pmode"); iPdepth = id ("pdepth");
        iPshape = id ("pshape"); iPlen = id ("plen"); iPrel = id ("prel"); iPatk = id ("patk"); iPhold = id ("phold"); iPthr = id ("pthr"); iPonly = id ("ponly");
        iLook = id ("plook"); iSwing = id ("swing"); iTrig = id ("trig"); iTthr = id ("tthr"); iTsens = id ("tsens");
        lookSamples = (int) std::lround (sampleRate * 0.005);
        tape.prepare ((int) (sampleRate * 6.0)); head.prepare (sampleRate);
        for (auto& l : look) l.assign ((size_t) lookSamples, 0.0f);   // a ring of N samples delays by exactly N
        for (auto& t : trans) t.clear (sampleRate);
        resetEffect();
    }
    void resetEffect() override
    {
        tape.clear(); head.reset(); volSlew.snap (1.0f); g1 = g2 = 1.0f; env = 0; heldTgt = 1.0f; holdLeft = 0;
        lastStep = lastPat = -1; mode = 0; sinceHit = 1.0e12; lookPos = 0; for (auto& l : look) std::fill (l.begin(), l.end(), 0.0f);
        for (auto& t : trans) t.clear (sr);
    }
    void process (juce::dsp::AudioBlock<float> block, const ModulationContext& ctx) noexcept override
    {
        using namespace rhythm;
        beginBlock (ctx);
        published.fetch();
        const auto& B = published.read();
        const int n = (int) block.getNumSamples(), nch = juce::jmin (2, (int) block.getNumChannels());
        float* L = block.getChannelPointer (0); float* R = nch > 1 ? block.getChannelPointer (1) : L;
        HostClock clk; clk.set (ctx.tempo);
        const int pat = juce::jlimit (0, gatedata::kPatterns - 1, choice (iPattern) - 1);
        static const double barsFor[] { 0.5, 1.0, 2.0 };
        const double beats = barsFor[juce::jlimit (0, 2, choice (iLen))] * ctx.tempo.beatsPerBar();
        const double lenSamples = beats * clk.spb, stepLen = lenSamples / gatedata::kSteps;
        const int pm = juce::jlimit (0, 5, choice (iPmode));
        const bool patternOn = pm == pmOff || ! on (iPonly), audioTrig = choice (iTrig) == 1;
        const float sm = p (iSmooth).target;
        volSlew.set (sr, 0.3f + 30.0f * sm * sm, 0.3f + 30.0f * sm * sm);
        // GATE-12's sensitivity mapping: sense = (1 - s)^2; its threshold is a linear level
        const double tthr = std::pow (10.0, p (iTthr).target / 20.0), tsense = std::pow (1.0 - (double) p (iTsens).target, 2.0);
        // pump settings
        const float depth = juce::jlimit (0.0f, 1.0f, p (iPdepth).target), ex = std::exp2 ((juce::jlimit (0.0f, 1.0f, p (iPshape).target) - 0.5f) * 3.0f);
        static const double plens[] { 0.5, 1, 2, 4 };
        const double period = pm == pmFloor ? 1.0 : pm == pmHalf ? 2.0 : pm == pmFast ? 0.5 : plens[juce::jlimit (0, 3, choice (iPlen))];
        const float holdMs = juce::jlimit (0.0f, 200.0f, p (iPhold).target);
        const float holdFrac = (float) (holdMs * 0.001 / (period * 60.0 / juce::jmax (1.0, ctx.tempo.bpm)));
        const int holdSamples = (int) (holdMs * 0.001 * sr);
        const float atkA = 1.0f - (float) std::exp (-1.0 / (juce::jlimit (0.5f, 50.0f, p (iPatk).target) * 0.001 * sr));
        const float relA = 1.0f - (float) std::exp (-1.0 / (juce::jlimit (10.0f, 1000.0f, p (iPrel).target) * 0.001 * sr));
        const float envAtk = 1.0f - (float) std::exp (-1.0 / (0.0008 * sr)), ga = 1.0f - (float) std::exp (-1.0 / (0.0008 * sr));
        const float thrDb = p (iPthr).target, sw = p (iSwing).target;
        float pumpMin = 1.0f; double pos = 0;

        for (int i = 0; i < n; ++i)
        {
            const float p0 = p (iTime).next(); juce::ignoreUnused (p0);
            float xl = L[i], xr = R[i];
            // the key (transient trigger, sidechain pump) is heard before the audio when lookahead is on
            const float kl = xl, kr = xr;
            if (lookOn)
            {
                auto& la = look[0]; auto& lb = look[1]; const int ls = (int) la.size();
                const float dl = la[(size_t) lookPos], dr = lb[(size_t) lookPos]; la[(size_t) lookPos] = xl; lb[(size_t) lookPos] = xr; lookPos = (lookPos + 1) % ls;
                xl = dl; xr = dr;
            }
            tape.write (xl, xr);
            float yl = xl, yr = xr;
            if (patternOn)
            {
                if (audioTrig)
                {
                    if (trans[0].detect (0, kl, tthr, tsense) || trans[1].detect (0, kr, tthr, tsense))
                    { trans[0].startCooldown(); trans[1].startCooldown(); sinceHit = lookOn ? -(double) lookSamples : 0.0; }
                    sinceHit += 1.0;
                    pos = juce::jlimit (0.0, 1.0, sinceHit / lenSamples);
                }
                else pos = positiveMod (clk.ppqAt (i), beats) / beats;
                const double p16 = swingSteps (pos * gatedata::kSteps, sw);
                const double posSw = juce::jlimit (0.0, 1.0, p16 / gatedata::kSteps);
                // time lane (repeat / stutter / half / reverse / scratch / stop) over runs of equal steps
                const int step = juce::jlimit (0, gatedata::kSteps - 1, (int) p16);
                const double oStep = (p16 - step) * stepLen;
                if (step != lastStep || pat != lastPat)
                {
                    lastStep = step; lastPat = pat; const auto& st = B.steps[(size_t) pat];
                    mode = st[(size_t) step]; runStart = step; while (runStart > 0 && st[(size_t) runStart - 1] == mode) --runStart;
                    runEnd = step; while (runEnd < gatedata::kSteps - 1 && st[(size_t) runEnd + 1] == mode) ++runEnd;
                }
                const double oRun = (step - runStart) * stepLen + oStep, runLen = (runEnd - runStart + 1) * stepLen;
                head.advance (mode == tmNormal ? 0.0 : timeModeDelay (mode, oRun, runLen, stepLen, oStep));
                const float ta = p (iTime).current;
                if (ta > 0.0005f && ! head.idle()) { yl += ta * (head.tap (tape, 0, xl) - yl); yr += ta * (head.tap (tape, 1, xr) - yr); }
                const float target = (float) gatedata::levelAt (evaluator, B.segs[(size_t) pat].data(), B.nseg[(size_t) pat], posSw);
                const float v = volSlew.next (target);
                yl *= v; yr *= v;
            }
            if (pm != pmOff)
            {
                float tgt;
                if (pm == pmSidechain)
                {
                    const float k = 0.5f * (std::abs (kl) + std::abs (kr));
                    env += (k > env ? envAtk : relA) * (k - env);
                    const float over = juce::jlimit (0.0f, 1.0f, (gainToDb (env) - thrDb) / 18.0f);
                    tgt = 1.0f - depth * std::pow (over, 1.0f / ex);
                    if (tgt <= heldTgt || holdLeft <= 0) { heldTgt = tgt; holdLeft = holdSamples; } else { --holdLeft; tgt = heldTgt; }
                }
                else if (pm == pmDrawn) tgt = 1.0f - depth * juce::jlimit (0.0f, 1.0f, -B.pump.valueAt (positiveMod (clk.ppqAt (i), period) / period));
                else
                {
                    const float ph = (float) (positiveMod (clk.ppqAt (i), period) / period), recStart = juce::jlimit (0.0f, 0.6f, holdFrac);
                    const float rec = juce::jlimit (0.0f, 1.0f, (ph - recStart) / std::max (0.05f, 0.75f - recStart));
                    tgt = 1.0f - depth * (1.0f - std::pow (rec, ex));
                }
                g1 += (tgt < g1 ? atkA : ga) * (tgt - g1); g2 += ga * (g1 - g2);
                yl *= g2; yr *= g2; pumpMin = std::min (pumpMin, g2);
            }
            L[i] = yl; if (nch > 1) R[i] = yr;
        }
        uiPos.store (patternOn ? (float) pos : -1.0f, std::memory_order_relaxed); uiPattern.store (pat, std::memory_order_relaxed); uiPump.store (pumpMin, std::memory_order_relaxed);
    }

private:
    void bakeInto (Baked& b) const
    {
        for (int i = 0; i < gatedata::kPatterns; ++i) { b.nseg[(size_t) i] = gatedata::segmentsOf (model[(size_t) i], b.segs[(size_t) i].data()); b.steps[(size_t) i] = model[(size_t) i].steps; }
        b.pump = pumpShape;
    }
    std::array<gatedata::Pattern, gatedata::kPatterns> model;   // message thread
    bendlane::Shape pumpShape;
    rhythm::Published<Baked> published;
    rhythm::TapeBuffer tape; rhythm::TimeHead head; rhythm::GainSlew volSlew;
    gate12::Pattern evaluator { 0 };   // audio thread only: GATE-12's curve functions
    std::array<gate12::Transient, 2> trans; std::array<std::vector<float>, 2> look; int lookPos = 0, lookSamples = 240; bool lookOn = false;
    int iPattern = 0, iLen = 1, iSmooth = 2, iTime = 3, iPmode = 4, iPdepth = 5, iPshape = 6, iPlen = 7, iPrel = 8, iPatk = 9, iPhold = 10, iPthr = 11, iPonly = 12, iLook = 13, iSwing = 14, iTrig = 15, iTthr = 16, iTsens = 17;
    int lastStep = -1, lastPat = -1, mode = 0, runStart = 0, runEnd = 0, holdLeft = 0; double sinceHit = 1.0e12;
    float g1 = 1, g2 = 1, env = 0, heldTgt = 1;
};
} // namespace phantom
