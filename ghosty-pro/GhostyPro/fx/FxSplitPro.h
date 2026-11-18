#pragma once
// SPLITTER (Ghosty Pro, FX RACK round 3, 2026-10-25). Original DSP (no third-party code).
//   L/H, L/M/H or M/S: the signal is split into 2 / 3 bands (Linkwitz-Riley 4th order, the low band of L/M/H phase-matched) or
//   into MID and SIDE, each band runs its own mini rack of up to 3 units (WRECKER, TUBE, SOUL CRUSH, FILTER, CHORUS, CRYSTAL, ECHO,
//   SPACE, SHIMMER, SQUEEZE, FATTEN, HYPER, DISTORTION, COMPRESSOR, TONE EQ: each slot uses that unit's settings from its own page and has
//   its own AMOUNT), then the bands are summed back. The bands are delayed to the slowest one (latency compensated between the
//   branches, crossfaded when it changes), so with empty slots the sum is flat (an allpass for L/H and L/M/H, exact for M/S).
//   MIX blends with the same split summed without the units (phase-matched dry). A slot that changes crossfades old -> new (20 ms).
// Threading: slot instances are made, prepared and owned on the message thread (Rack::Impl::setBandSlot); the audio thread
// picks a new one up through an atomic hand-off and never allocates or frees; an instance is freed only once the audio thread
// has finished two blocks without it.
#include "FxProCommon.h"

#if ! GPFX_BOX
namespace gpfx
{
inline const juce::StringArray& bandsModeNames() { static juce::StringArray s { "L / H", "L / M / H", "M / S" }; return s; }
// the units a slot can hold (in menu order); none of them may be the SPLITTER itself, the limiter, or units with editor-side
// stores (GATE, GRID, HAUNT, SPIRIT EQ)
inline const std::vector<int>& bandSlotUnits()
{
    static const std::vector<int> v { uWreck, uTube, uCrush, uDist, uComp, uEq3, uFilter, uChorus, uHyper, uCrystal, uEcho, uSpace, uShimmer, uOtt, uFatten };
    return v;
}
constexpr int kBands = 3, kSlots = 3;

class BandsUnit : public UnitDsp
{
public:
    Telemetry* tm = nullptr;
    std::atomic<float>* hostOn = nullptr;
    bool selfMix() const override { return true; }   // fx_bands_mix blends with the phase-matched dry (the rack only fades on / off)
    void attach (juce::AudioProcessorValueTreeState& s) override
    {
        mode.bind (s, pid (uBands, "mode"), 0); mix.bind (s, pid (uBands, "mix"), 1.0f); xlo.bind (s, pid (uBands, "xlo"), 200.0f); xhi.bind (s, pid (uBands, "xhi"), 3000.0f);
        for (int b = 0; b < kBands; ++b)
        {
            gain[(size_t) b].bind (s, pid (uBands, ("b" + juce::String (b + 1) + "_gain").toRawUTF8()), 0.0f);
            for (int k = 0; k < kSlots; ++k) amt[(size_t) b][(size_t) k].bind (s, pid (uBands, ("s" + juce::String (b + 1) + juce::String (k + 1) + "_amt").toRawUTF8()), 1.0f);
        }
        hostOn = s.getRawParameterValue (pid (uBands, "on"));
    }
    void prepare (double sr_, int) override
    {
        sr = sr_;
        for (auto& d : bandDelay) d.prepare (kMaxLat, sr);
        dryDelay.prepare (kMaxLat, sr); rackDry.prepare (kMaxLat, sr);
        mixSm.setTime (sr, 25.0f); xloSm.setTime (sr, 30.0f); xhiSm.setTime (sr, 30.0f);
        for (auto& g : gainSm) g.setTime (sr, 20.0f);
        for (auto& a : amtSm) for (auto& x : a) x.setTime (sr, 20.0f);
        xfLen = (int) (0.02 * sr);
        reset();
    }
    void reset() override
    {
        for (auto& x : xLo) x.reset();
        for (auto& x : xHi) x.reset();
        for (auto& x : apHi) x.reset();
        for (auto& d : bandDelay) d.clear();
        dryDelay.clear();
        mixSm.snap (clampf (mix(), 0.0f, 1.0f)); xloSm.snap (std::log2 (xlo())); xhiSm.snap (std::log2 (xhi()));
        for (int b = 0; b < kBands; ++b) { gainSm[(size_t) b].snap (dbToGain (gain[(size_t) b]())); for (int k = 0; k < kSlots; ++k) amtSm[(size_t) b][(size_t) k].snap (clampf (amt[(size_t) b][(size_t) k](), 0.0f, 1.0f)); }
        curMode = prevMode = juce::jlimit (0, 2, mode.i()); modeFade = 0;
        for (auto& bs : slot) for (auto& sl : bs) { if (sl.cur != nullptr) { sl.cur->reset(); sl.cur->clearLines(); } sl.xf = 0; sl.old.store (nullptr); }
    }
    int latency() const override { return hostOn == nullptr || hostOn->load (std::memory_order_relaxed) > 0.5f ? maxLatNow : 0; }
    void delayDry (float* L, float* R, int n) override { rackDry.process (L, R, n, latency()); }
    void clearLines() override { rackDry.clear(); for (auto& d : bandDelay) d.clear(); dryDelay.clear(); }
    void idle (const float*, const float*, int) override { pickUp(); epoch.fetch_add (1, std::memory_order_release); }

    // ---- message thread -> audio thread hand-off (Rack::Impl owns the instances)
    struct Slot
    {
        std::atomic<UnitDsp*> pending { nullptr };   // set by the message thread (kEmpty = clear the slot)
        std::atomic<UnitDsp*> curA { nullptr }, old { nullptr };   // what the audio thread is using (read by the message thread's GC)
        UnitDsp* cur = nullptr; int xf = 0;
    };
    std::array<std::array<Slot, kSlots>, kBands> slot;
    std::atomic<int64_t> epoch { 0 };                 // blocks finished by the audio thread
    static UnitDsp* emptyMark() { static char c; return reinterpret_cast<UnitDsp*> (&c); }

    void process (float* L, float* R, int n, const BlockCtx& ctx) override
    {
        pickUp();
        const int m = juce::jlimit (0, 2, mode.i());
        if (m != curMode) { prevMode = curMode; curMode = m; modeFade = xfLen; }
        mixSm.target = clampf (mix(), 0.0f, 1.0f);
        xloSm.target = std::log2 (juce::jlimit (30.0f, 2000.0f, xlo())); xhiSm.target = std::log2 (juce::jlimit (500.0f, 16000.0f, std::max (xhi(), xlo() * 1.5f)));
        for (int b = 0; b < kBands; ++b) { gainSm[(size_t) b].target = dbToGain (juce::jlimit (-24.0f, 12.0f, gain[(size_t) b]())); for (int k = 0; k < kSlots; ++k) amtSm[(size_t) b][(size_t) k].target = clampf (amt[(size_t) b][(size_t) k](), 0.0f, 1.0f); }
        // 1) split into the band buffers (and the phase-matched dry); a mode change crossfades the two splits
        for (int i = 0; i < n; ++i)
        {
            const float flo = std::exp2 (xloSm.next()), fhi = std::exp2 (xhiSm.next());
            if ((i & 3) == 0) { for (auto& x : xLo) x.set (sr, flo); for (auto& x : xHi) x.set (sr, fhi); for (auto& x : apHi) x.set (sr, fhi); }
            float bb[kBands][2]; splitOne (curMode, L[i], R[i], bb);
            if (modeFade > 0)
            {
                float pb[kBands][2]; splitOne (prevMode, L[i], R[i], pb);
                const float w = 0.5f - 0.5f * std::cos (kPi * (float) modeFade / (float) xfLen);   // old weight
                for (int b = 0; b < kBands; ++b) for (int c = 0; c < 2; ++c) bb[b][c] += w * (pb[b][c] - bb[b][c]);
                --modeFade;
            }
            for (int b = 0; b < kBands; ++b) { band[(size_t) b][0][(size_t) i] = bb[b][0]; band[(size_t) b][1][(size_t) i] = bb[b][1]; }
            dryL[(size_t) i] = bb[0][0] + bb[1][0] + bb[2][0]; dryR[(size_t) i] = bb[0][1] + bb[1][1] + bb[2][1];
        }
        // 2) each band's mini rack, then the band level
        int lat[kBands] {};
        for (int b = 0; b < kBands; ++b)
        {
            float* bl = band[(size_t) b][0].data(); float* br = band[(size_t) b][1].data();
            for (int k = 0; k < kSlots; ++k) lat[b] += runSlot (slot[(size_t) b][(size_t) k], amtSm[(size_t) b][(size_t) k], bl, br, n, ctx);
        }
        int maxLat = 0; for (int b = 0; b < kBands; ++b) maxLat = std::max (maxLat, lat[b]);
        maxLatNow = juce::jlimit (0, kMaxLat, maxLat);
        // 3) align the bands to the slowest, sum, MIX against the aligned dry
        for (int b = 0; b < kBands; ++b) bandDelay[(size_t) b].process (band[(size_t) b][0].data(), band[(size_t) b][1].data(), n, maxLatNow - lat[b]);
        dryDelay.process (dryL.data(), dryR.data(), n, maxLatNow);
        float pk[kBands] {};
        for (int i = 0; i < n; ++i)
        {
            float l = 0, r = 0;
            for (int b = 0; b < kBands; ++b)
            {
                const float g = gainSm[(size_t) b].next();
                const float bl = band[(size_t) b][0][(size_t) i] * g, br = band[(size_t) b][1][(size_t) i] * g;
                l += bl; r += br; pk[b] = std::max (pk[b], std::max (std::abs (bl), std::abs (br)));
            }
            const float mx = mixSm.next();
            L[i] = dryL[(size_t) i] + mx * (l - dryL[(size_t) i]); R[i] = dryR[(size_t) i] + mx * (r - dryR[(size_t) i]);
        }
        if (tm != nullptr) for (int b = 0; b < kBands; ++b) tm->bandsLevel[(size_t) b].store (pk[b], std::memory_order_relaxed);
        epoch.fetch_add (1, std::memory_order_release);
    }
    static constexpr int kMaxLat = 16384;
private:
    void pickUp() noexcept
    {
        for (auto& bandS : slot)
            for (auto& sl : bandS)
            {
                UnitDsp* p = sl.pending.exchange (nullptr, std::memory_order_acq_rel);
                if (p == nullptr) continue;
                sl.old.store (sl.cur, std::memory_order_release);
                sl.cur = p == emptyMark() ? nullptr : p;
                sl.curA.store (sl.cur, std::memory_order_release);
                if (sl.cur != nullptr) { sl.cur->reset(); sl.cur->clearLines(); }
                sl.xf = sl.old.load() != nullptr || sl.cur != nullptr ? xfLen : 0;
            }
    }
    // one slot: wet = unit, mixed by its AMOUNT with its own (latency-aligned) dry; a change crossfades old -> new
    int runSlot (Slot& sl, ExpSmooth& a, float* L, float* R, int n, const BlockCtx& ctx)
    {
        UnitDsp* old = sl.xf > 0 ? sl.old.load (std::memory_order_relaxed) : nullptr;
        if (sl.cur == nullptr && old == nullptr) { a.snap (a.target); return 0; }
        std::copy (L, L + n, inL.begin()); std::copy (R, R + n, inR.begin());
        auto run = [&] (UnitDsp* u, float* oL, float* oR)
        {   // oL / oR: the slot input in, the slot output out (unit output mixed with its aligned dry by AMOUNT)
            if (u == nullptr) return;
            std::copy (oL, oL + n, tmpL.begin()); std::copy (oR, oR + n, tmpR.begin());
            u->process (oL, oR, n, ctx);
            u->delayDry (tmpL.data(), tmpR.data(), n);
            for (int i = 0; i < n; ++i) { const float g = amtArr[(size_t) i]; oL[i] = tmpL[(size_t) i] + g * (oL[i] - tmpL[(size_t) i]); oR[i] = tmpR[(size_t) i] + g * (oR[i] - tmpR[(size_t) i]); }
        };
        for (int i = 0; i < n; ++i) amtArr[(size_t) i] = a.next();
        if (sl.cur != nullptr) run (sl.cur, L, R);
        if (old != nullptr)
        {
            std::copy (inL.begin(), inL.begin() + n, oldL.begin()); std::copy (inR.begin(), inR.begin() + n, oldR.begin());
            run (old, oldL.data(), oldR.data());
            for (int i = 0; i < n; ++i)
            {
                const float w = sl.xf > 0 ? 0.5f - 0.5f * std::cos (kPi * (float) sl.xf / (float) xfLen) : 0.0f;   // old weight 1 -> 0
                const float nl = sl.cur != nullptr ? L[i] : inL[(size_t) i], nr = sl.cur != nullptr ? R[i] : inR[(size_t) i];
                L[i] = nl + w * (oldL[(size_t) i] - nl); R[i] = nr + w * (oldR[(size_t) i] - nr);
                if (sl.xf > 0) --sl.xf;
            }
            if (sl.xf <= 0) sl.old.store (nullptr, std::memory_order_release);
        }
        else if (sl.xf > 0)
        {   // a unit came into an empty slot: fade it in from the dry
            for (int i = 0; i < n; ++i)
            {
                const float w = sl.xf > 0 ? 0.5f - 0.5f * std::cos (kPi * (float) sl.xf / (float) xfLen) : 0.0f;
                L[i] += w * (inL[(size_t) i] - L[i]); R[i] += w * (inR[(size_t) i] - R[i]);
                if (sl.xf > 0) --sl.xf;
            }
        }
        return sl.cur != nullptr ? sl.cur->latency() : 0;
    }
    void splitOne (int m, float l, float r, float out[kBands][2]) noexcept
    {
        if (m == 2)
        {   // M / S: mid as (M, M), side as (S, -S); summing gives L and R back exactly
            const float mid = 0.5f * (l + r), side = 0.5f * (l - r);
            out[0][0] = mid; out[0][1] = mid; out[1][0] = side; out[1][1] = -side; out[2][0] = out[2][1] = 0.0f;
            return;
        }
        const float in[2] { l, r };
        for (int c = 0; c < 2; ++c)
        {
            float lo, hi;
            xLo[(size_t) c].split (in[c], lo, hi);
            if (m == 0) { out[0][c] = lo; out[1][c] = hi; out[2][c] = 0.0f; }
            else
            {
                float mid, top; xHi[(size_t) c].split (hi, mid, top);
                out[0][c] = apHi[(size_t) c].process (lo); out[1][c] = mid; out[2][c] = top;
            }
        }
    }
    P mode, mix, xlo, xhi; std::array<P, kBands> gain; std::array<std::array<P, kSlots>, kBands> amt;
    double sr = 48000; int xfLen = 960, curMode = 0, prevMode = 0, modeFade = 0, maxLatNow = 0;
    std::array<Lr4, 2> xLo, xHi; std::array<Lr4Allpass, 2> apHi;
    std::array<SwitchDelay, kBands> bandDelay; SwitchDelay dryDelay, rackDry;
    ExpSmooth mixSm, xloSm, xhiSm; std::array<ExpSmooth, kBands> gainSm; std::array<std::array<ExpSmooth, kSlots>, kBands> amtSm;
    std::array<std::array<std::array<float, kSub>, 2>, kBands> band {};
    std::array<float, kSub> dryL {}, dryR {}, inL {}, inR {}, tmpL {}, tmpR {}, oldL {}, oldR {}, amtArr {};
};

inline void addBandsParameters (juce::AudioProcessorValueTreeState::ParameterLayout& l)
{
    auto T = [] (const char* s) { return juce::String ("FX ") + unitInfo (uBands).title + " " + s; };
    addChoice (l, pid (uBands, "mode"), T ("Mode"), bandsModeNames(), 0);
    addFloat (l, pid (uBands, "xlo"), T ("Low Crossover"), skewRange (30.0f, 2000.0f, 250.0f), 200.0f, "Hz", 0);
    addFloat (l, pid (uBands, "xhi"), T ("High Crossover"), skewRange (500.0f, 16000.0f, 3000.0f), 3000.0f, "Hz", 0);
    for (int b = 0; b < kBands; ++b)
    {
        addFloat (l, pid (uBands, ("b" + juce::String (b + 1) + "_gain").toRawUTF8()), T (("Band " + juce::String (b + 1) + " Level").toRawUTF8()), juce::NormalisableRange<float> (-24.0f, 12.0f), 0.0f, "dB", 1);
        for (int k = 0; k < kSlots; ++k)
            addPct (l, pid (uBands, ("s" + juce::String (b + 1) + juce::String (k + 1) + "_amt").toRawUTF8()), T (("Band " + juce::String (b + 1) + " Slot " + juce::String (k + 1) + " Amount").toRawUTF8()), 1.0f);
    }
}
} // namespace gpfx
#endif // ! GPFX_BOX
