#pragma once
// GHOST SPLIT (Ghost Box only): an envelope transient detector splits the signal into an ATTACK path and a BODY path
// (ATTACK + BODY == input exactly), each path gets its own mini chain (level, saturate, tilt EQ, width, reverb send,
// echo send), then they are summed with the shared reverb / echo returns. Zero latency. Original design.
#include "FxCore.h"
#include "FxSpaceDsp.h"

#if GPFX_BOX
namespace gpfx {

inline const juce::StringArray& splitListenNames() { static juce::StringArray s { "BOTH", "ATTACK", "BODY" }; return s; }

class SplitUnit : public UnitDsp
{
public:
    Telemetry* tm = nullptr;
    void attach (juce::AudioProcessorValueTreeState& s) override
    {
        auto b = [&s] (P& p, const char* id, float d) { p.bind (s, pid (uSplit, id), d); };
        b (sens, "sens", 0.5f); b (xover, "xover", 30); b (attack, "attack", 0); b (sustain, "sustain", 0); b (blend, "blend", 0.5f);
        b (listen, "listen", 0); b (out, "out", 0);
        for (int k = 0; k < 2; ++k)
        {
            const juce::String pre = k == 0 ? "a_" : "b_";
            auto bb = [&] (P& p, const char* id, float d) { p.bind (s, pid (uSplit, (pre + id).toRawUTF8()), d); };
            bb (path[k].gain, "gain", 0); bb (path[k].drive, "drive", 0); bb (path[k].tilt, "tilt", 0); bb (path[k].width, "width", 1);
            bb (path[k].verb, "verb", 0); bb (path[k].echo, "echo", 0);
        }
        b (vsize, "vsize", 0.6f); b (vdecay, "vdecay", 2.5f); b (ediv, "ediv", 7); b (efb, "efb", 0.35f);
    }
    void prepare (double sr_, int) override
    {
        sr = sr_;
        verb.prepare (sr, 2.4f);
        for (auto& r : eRing) r.prepare ((int) (sr * 4.2) + 16);
        for (auto& pth : path) for (auto& t : pth.tiltF) t.lp.setLP (sr, 900.0);
        for (auto& pth : path) { pth.gSm.setTime (sr, 20.0f); pth.dSm.setTime (sr, 30.0f); pth.wSm.setTime (sr, 30.0f); pth.vSm.setTime (sr, 30.0f); pth.eSm.setTime (sr, 30.0f); }
        eTime.setTime (sr, 150.0f); outSm.setTime (sr, 20.0f);
        for (auto& o : eLp) o.setLP (sr, 5200.0);
        for (auto& o : eHp) o.setLP (sr, 160.0);
        reset();
    }
    void reset() override
    {
        fast = slow = 0; mask = 0; verb.clear(); for (auto& r : eRing) r.clear(); for (auto& o : eLp) o.reset(); for (auto& o : eHp) o.reset();
        for (auto& pth : path)
        {
            for (auto& t : pth.tiltF) t.reset();
            pth.gSm.snap (1); pth.dSm.snap (pth.drive()); pth.wSm.snap (pth.width()); pth.vSm.snap (pth.verb()); pth.eSm.snap (pth.echo());
        }
        eTime.snap ((float) echoSamples (120.0)); outSm.snap (dbToGain (out())); tail = 0; snapNext = false;
    }
    void jumpSmoothing() override { snapNext = true; }
    void process (float* L, float* R, int n, const BlockCtx& c) override
    {
        // detector: a level envelope (0.1 ms attack, 8 ms release) against a slow copy of it that rises over ATTACK LENGTH
        // and falls quickly; steady sound -> both equal (BODY), onsets -> the level runs ahead (ATTACK)
        const float xo = clampf (xover(), 2.0f, 400.0f);
        const float fa = coef (0.1f), fr = coef (8.0f), sa = coef (xo), sRel = coef (8.0f);
        const float mRel = coef (xo * 0.5f), mAtt = coef (0.25f);
        const float sn = clampf (sens(), 0.0f, 1.0f), k = 1.5f + 6.0f * sn, thr = 0.08f + 0.5f * (1.0f - sn);
        const float bl = clampf (blend(), 0.0f, 1.0f);
        const float balA = std::min (1.0f, 2.0f * (1.0f - bl)), balB = std::min (1.0f, 2.0f * bl);
        const int lis = listen.i();
        const float gA = dbToGain (clampf (attack(), -1, 1) * 15.0f + path[0].gain()) * balA * (lis == 2 ? 0.0f : 1.0f);
        const float gB = dbToGain (clampf (sustain(), -1, 1) * 15.0f + path[1].gain()) * balB * (lis == 1 ? 0.0f : 1.0f);
        path[0].gSm.target = gA; path[1].gSm.target = gB;
        float sendsMax = 0;
        for (auto& pth : path)
        {
            pth.dSm.target = clampf (pth.drive(), 0, 1); pth.wSm.target = clampf (pth.width(), 0, 2);
            pth.vSm.target = clampf (pth.verb(), 0, 1); pth.eSm.target = clampf (pth.echo(), 0, 1);
            const float t = clampf (pth.tilt(), -1, 1);
            for (auto& f : pth.tiltF) setTilt (f, t);
            sendsMax = std::max ({ sendsMax, pth.vSm.target, pth.eSm.target, pth.vSm.cur, pth.eSm.cur });
        }
        verb.set (clampf (vsize(), 0, 1), clampf (vdecay(), 0.2f, 20.0f), 7500.0f, 0.35f, false);
        eTime.target = (float) echoSamples (c.bpm);
        if (snapNext) { eTime.snap (eTime.target); snapNext = false; }
        const float fb = clampf (efb(), 0.0f, 0.95f);
        outSm.target = dbToGain (out());
        const bool fx = sendsMax > 1.0e-4f || tail > 0;
        float pkM = 0, pkA = 0, pkB = 0;
        for (int i = 0; i < n; ++i)
        {
            const float xl = L[i], xr = R[i];
            const float x = 0.5f * (std::abs (xl) + std::abs (xr));
            fast += (x > fast ? fa : fr) * (x - fast);
            slow += (fast > slow ? sa : sRel) * (fast - slow);
            const float ratio = fast / (slow + 1.0e-5f);
            const float tgt = clampf ((ratio - 1.0f - thr) * k, 0.0f, 1.0f) * (fast > 1.0e-5f ? 1.0f : 0.0f);
            mask += (tgt > mask ? mAtt : mRel) * (tgt - mask);
            const float m = mask;
            float al = xl * m, ar = xr * m, bl_ = xl - al, br = xr - ar;
            float vl = 0, vr = 0, el = 0, er = 0;
            for (int p = 0; p < 2; ++p)
            {
                auto& pth = path[p];
                float& l = p == 0 ? al : bl_; float& r = p == 0 ? ar : br;
                const float g = pth.gSm.next(), d = pth.dSm.next(), w = pth.wSm.next(), vs = pth.vSm.next(), es = pth.eSm.next();
                l *= g; r *= g;
                if (d > 1.0e-4f)
                {
                    const float dg = 1.0f + 14.0f * d, comp = 1.0f / std::sqrt (dg);
                    l = l + d * (fastTanh (l * dg) * comp - l); r = r + d * (fastTanh (r * dg) * comp - r);
                }
                l = pth.tiltF[0].process (l); r = pth.tiltF[1].process (r);
                if (std::abs (w - 1.0f) > 1.0e-4f) { const float mid = 0.5f * (l + r), sd = 0.5f * (l - r) * w; l = mid + sd; r = mid - sd; }
                vl += vs * l; vr += vs * r; el += es * l; er += es * r;
            }
            float yl = al + bl_, yr = ar + br;
            if (fx)
            {
                float wl, wr; verb.process (vl, vr, wl, wr);
                const double d = eTime.next();
                const float dl = eRing[0].readLinear (d), dr = eRing[1].readLinear (d);
                eRing[0].push (el + fb * eHp[1].hp (eLp[1].lp (dr)));   // ping-pong
                eRing[1].push (er + fb * eHp[0].hp (eLp[0].lp (dl)));
                yl += wl * 0.7f + dl; yr += wr * 0.7f + dr;
            }
            const float og = outSm.next();
            L[i] = yl * og; R[i] = yr * og;
            pkM = std::max (pkM, m); pkA = std::max (pkA, std::abs (al)); pkB = std::max (pkB, std::abs (bl_));
        }
        // keep the returns running ~4 s after the last send so tails ring out (and then go idle)
        if (sendsMax > 1.0e-4f) tail = (int) (sr * (4.0 + clampf (vdecay(), 0.2f, 20.0f))); else tail = std::max (0, tail - n);
        if (tail == 0 && sendsMax <= 1.0e-4f && fx) { verb.clear(); for (auto& r : eRing) r.clear(); }
        if (tm) { tm->splitMask.store (pkM); tm->splitAtk.store (pkA); tm->splitBody.store (pkB); }
    }
    float maskNow() const noexcept { return mask; }

private:
    struct TiltF { OnePole lp; float gl = 1, gh = 1; float process (float x) noexcept { if (gl == 1.0f && gh == 1.0f) return x; const float lo = lp.lp (x); return lo * gl + (x - lo) * gh; } void reset() { lp.reset(); } };
    static void setTilt (TiltF& f, float t) { if (std::abs (t) < 1.0e-4f) { f.gl = f.gh = 1.0f; return; } f.gh = dbToGain (t * 9.0f); f.gl = dbToGain (-t * 6.0f); }
    struct Path { P gain, drive, tilt, width, verb, echo; std::array<TiltF, 2> tiltF; ExpSmooth gSm, dSm, wSm, vSm, eSm; };
    float coef (float ms) const { return 1.0f - (float) std::exp (-1.0 / std::max (1.0, ms * 0.001 * sr)); }
    double echoSamples (double bpm) const
    {
        const auto& d = echoDivs(); const int i = juce::jlimit (0, (int) d.size() - 1, ediv.i());
        return juce::jlimit (16.0, sr * 4.0, d[(size_t) i].beats * 60.0 / std::max (20.0, bpm) * sr);
    }
    P sens, xover, attack, sustain, blend, listen, out, vsize, vdecay, ediv, efb;
    std::array<Path, 2> path;
    Fdn8 verb; std::array<Ring, 2> eRing; std::array<OnePole, 2> eLp, eHp; ExpSmooth eTime, outSm;
    double sr = 48000; float fast = 0, slow = 0, mask = 0; int tail = 0; bool snapNext = false;
};

inline void addSplitParameters (Layout& l)
{
    auto T = [] (const char* s) { return juce::String ("FX ") + unitInfo (uSplit).title + " " + s; };
    addPct (l, pid (uSplit, "sens"), T ("Sensitivity"), 0.5f);
    addFloat (l, pid (uSplit, "xover"), T ("Attack Length"), skewRange (5.0f, 300.0f, 50.0f), 30.0f, "ms", 0);
    addFloat (l, pid (uSplit, "attack"), T ("Attack"), juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f, "", 2);
    addFloat (l, pid (uSplit, "sustain"), T ("Sustain"), juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f, "", 2);
    addPct (l, pid (uSplit, "blend"), T ("Blend"), 0.5f);
    addChoice (l, pid (uSplit, "listen"), T ("Listen"), splitListenNames(), 0);
    addFloat (l, pid (uSplit, "out"), T ("Output"), juce::NormalisableRange<float> (-24.0f, 12.0f), 0.0f, "dB", 1);
    for (int k = 0; k < 2; ++k)
    {
        const juce::String pre = k == 0 ? "a_" : "b_", nm = k == 0 ? "Attack " : "Body ";
        auto id = [&] (const char* s) { return pid (uSplit, (pre + s).toRawUTF8()); };
        addFloat (l, id ("gain"), T ((nm + "Gain").toRawUTF8()), juce::NormalisableRange<float> (-24.0f, 12.0f), 0.0f, "dB", 1);
        addPct (l, id ("drive"), T ((nm + "Drive").toRawUTF8()), 0.0f);
        addFloat (l, id ("tilt"), T ((nm + "Tilt").toRawUTF8()), juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f, "", 2);
        addFloat (l, id ("width"), T ((nm + "Width").toRawUTF8()), juce::NormalisableRange<float> (0.0f, 2.0f), 1.0f, "", 2);
        addPct (l, id ("verb"), T ((nm + "Reverb Send").toRawUTF8()), 0.0f);
        addPct (l, id ("echo"), T ((nm + "Echo Send").toRawUTF8()), 0.0f);
    }
    addPct (l, pid (uSplit, "vsize"), T ("Reverb Size"), 0.6f);
    addFloat (l, pid (uSplit, "vdecay"), T ("Reverb Decay"), skewRange (0.3f, 12.0f, 2.5f), 2.5f, "s", 2);
    addChoice (l, pid (uSplit, "ediv"), T ("Echo Division"), divNames (echoDivs()), 7);
    addPct (l, pid (uSplit, "efb"), T ("Echo Feedback"), 0.35f);
}

} // namespace gpfx
#endif
