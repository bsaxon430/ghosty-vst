// SPDX-License-Identifier: AGPL-3.0-or-later
// 2 GHOST GRID: paint effects on a one-bar step grid (16 or 32 steps, 8 patterns, 12 rows). Mango's lane / block model: a painted run of
// steps on a row is one block; it enters on the exact sample the host clock reaches its first step (loop jumps and tempo changes included),
// processes, and exits. Rows: GATE, LOOP and STUTTER (grain repeat), REVERSE, FREEZE, CRUSH, FILTER and DELAY are Mango's own effects;
// VERB and TONAL are FxmeTools' reverb and pitch shifter (the same foundation); SCRATCH and TAPE STOP read TIME-12's buffer. Mango's random
// draws are keyed on SEED, row and step, so every pass plays the same. Each row fades in over 2 ms and out over 4 ms (no clicks); DELAY and
// VERB are throws (their tails ring on after the painted steps).
#pragma once
#include "RhythmCore.h"
#include <climits>
#include "MangoLanes.h"
#include "../effects/ColourSst.h"

namespace phantom
{
namespace griddata
{
constexpr int kPatterns = 8, kRows = 12, kMaxSteps = 32;
enum Row { rLoop, rStutter, rScratch, rReverse, rTape, rFilter, rCrush, rDelay, rVerb, rTonal, rGate, rFreeze };
struct RowInfo { const char* key; const char* name; const char* knob; juce::uint32 colour; };
inline const RowInfo& row (int r)
{
    static const RowInfo k[kRows] {
        { "loop", "LOOP", "LENGTH", 0xff45E6FF }, { "stutter", "STUTTER", "RATE", 0xffFF4FD8 }, { "scratch", "SCRATCH", "DEPTH", 0xff3CFF8F },
        { "reverse", "REVERSE", "AMOUNT", 0xffFFC35A }, { "tape", "TAPE STOP", "SPEED", 0xffFF5A6E }, { "filter", "FILTER", "DEPTH", 0xffA06BFF },
        { "crush", "CRUSH", "AMOUNT", 0xffFF9E5A }, { "delay", "DELAY", "FEEDBACK", 0xff7FD8FF }, { "verb", "VERB", "SIZE", 0xffE6B8FF },
        { "tonal", "TONAL", "PITCH", 0xffB6FF3C }, { "gate", "GATE", "RATE", 0xff3CFF8F }, { "freeze", "FREEZE", "MIX", 0xff8FA8FF } };
    return k[juce::jlimit (0, kRows - 1, r)];
}
struct Pattern { juce::String name; std::array<uint32_t, kRows> rows {}; };
inline Pattern factory (int i)
{
    Pattern p; auto set = [&p] (int r, std::initializer_list<int> s) { for (int k : s) p.rows[(size_t) r] |= (1u << k); };
    switch (i)
    {
        case 0: p.name = "BREAKDOWN"; set (rLoop, { 12, 13, 14, 15 }); set (rFilter, { 8, 9, 10, 11, 12, 13, 14, 15 }); set (rDelay, { 14 }); break;
        case 1: p.name = "ROLL"; set (rStutter, { 12, 13, 14, 15 }); set (rReverse, { 7 }); set (rVerb, { 15 }); break;
        case 2: p.name = "SCRATCHY"; set (rScratch, { 2, 3, 10, 11 }); set (rCrush, { 6, 14 }); set (rGate, { 4, 5 }); break;
        case 3: p.name = "TAPE OUT"; set (rTape, { 12, 13, 14, 15 }); set (rDelay, { 15 }); break;
        case 4: p.name = "REVERSO"; set (rReverse, { 4, 5, 6, 7, 12, 13, 14, 15 }); set (rVerb, { 7 }); break;
        case 5: p.name = "DUB THROWS"; set (rDelay, { 3, 7, 11, 15 }); set (rVerb, { 15 }); set (rFilter, { 0, 1, 2, 3 }); break;
        case 6: p.name = "TONAL"; set (rTonal, { 0, 1, 8, 9 }); set (rCrush, { 4, 5, 12, 13 }); set (rFreeze, { 14, 15 }); break;
        default: p.name = "CHAOS"; set (rLoop, { 6, 7 }); set (rStutter, { 14, 15 }); set (rScratch, { 3 }); set (rReverse, { 10, 11 });
                 set (rCrush, { 1, 5, 9 }); set (rDelay, { 7 }); set (rTonal, { 12, 13 }); set (rFilter, { 8, 9, 10, 11 }); set (rGate, { 2 }); break;
    }
    return p;
}
} // namespace griddata

class GhostGridFx final : public CatalogEffect
{
public:
    using Patterns = std::array<griddata::Pattern, griddata::kPatterns>;
    struct Masks { std::array<std::array<uint32_t, griddata::kRows>, griddata::kPatterns> m {}; };

    GhostGridFx() : CatalogEffect (uGrid)
    {
        for (int i = 0; i < griddata::kPatterns; ++i) model[(size_t) i] = griddata::factory (i);
        published.publishAll (masksOf (model));
        std::vector<std::unique_ptr<juce::RangedAudioParameter>> ps;
        mng::GaterEffect::addParameters (ps, "g_", ""); mng::GrainDupEffect::addParameters (ps, "l_", ""); mng::GrainDupEffect::addParameters (ps, "s_", "S ");
        mng::ReverserEffect::addParameters (ps, "r_", ""); mng::FreezeEffect::addParameters (ps, "f_", ""); mng::QuantizerEffect::addParameters (ps, "q_", "");
        mng::FilterEnvEffect::addParameters (ps, "e_", ""); mng::DelayEffect::addParameters (ps, "d_", "");
        juce::AudioProcessorValueTreeState::ParameterLayout layout; for (auto& q : ps) layout.add (std::move (q));
        host = std::make_unique<MangoParamHost> (std::move (layout));
        gater.bindParameters (host->apvts, "g_"); loop.bindParameters (host->apvts, "l_"); stut.bindParameters (host->apvts, "s_");
        rev.bindParameters (host->apvts, "r_"); frz.bindParameters (host->apvts, "f_"); qnt.bindParameters (host->apvts, "q_");
        flt.bindParameters (host->apvts, "e_"); dly.bindParameters (host->apvts, "d_");
        auto w = [this] (const char* pre, std::array<std::atomic<float>*, 8>& a)
        { const char* sfx[] { "w4", "w8", "w16", "w32", "w64", "wstr", "wtrip", "wdot" }; for (int k = 0; k < 8; ++k) a[(size_t) k] = host->raw (juce::String (pre) + sfx[k]); };
        w ("g_gate_", wGate); w ("l_grain_", wLoop); w ("s_grain_", wStut); w ("f_frz_", wFrz); w ("e_flt_", wFlt);
        aGateRel = host->raw ("g_gate_rel"); aGateAtt = host->raw ("g_gate_att"); aRevMix = host->raw ("r_rev_mix"); aFrzMix = host->raw ("f_frz_mix");
        aBits = host->raw ("q_qnt_bits"); aDown = host->raw ("q_qnt_down"); aF0 = host->raw ("e_flt_f0"); aF1 = host->raw ("e_flt_f1"); aQ = host->raw ("e_flt_q");
        aDur = host->raw ("d_dly_dur"); aFb = host->raw ("d_dly_fb"); aDamp = host->raw ("d_dly_damp"); aDMix = host->raw ("d_dly_mix");
    }
    bool isRhythmic() const override { return true; }
    bool hasNullSetting() const override { return false; }          // every factory pattern paints something (an empty pattern nulls: tested apart)
    bool changesLevelOnPurpose() const override { return true; }    // painted gates, filters and throws change the level by design (reported)
    double tailSeconds() const override { return 6.0; }

    bool hasData() const override { return true; }
    juce::var getData() const override
    {
        juce::Array<juce::var> pats;
        for (auto& p : model) { auto* o = new juce::DynamicObject(); o->setProperty ("name", p.name); juce::Array<juce::var> r; for (auto m : p.rows) r.add ((juce::int64) m); o->setProperty ("rows", r); pats.add (juce::var (o)); }
        auto* o = new juce::DynamicObject(); o->setProperty ("patterns", pats); return juce::var (o);
    }
    void setData (const juce::var& v) override
    {
        for (int i = 0; i < griddata::kPatterns; ++i) model[(size_t) i] = griddata::factory (i);
        if (auto* a = v["patterns"].getArray())
            for (int i = 0; i < griddata::kPatterns && i < a->size(); ++i)
            {
                const auto& e = (*a)[i]; auto& p = model[(size_t) i]; p.name = e["name"].toString();
                if (auto* r = e["rows"].getArray()) for (int k = 0; k < griddata::kRows; ++k) p.rows[(size_t) k] = k < r->size() ? (uint32_t) (juce::int64) (*r)[k] : 0u;
            }
        published.edit() = masksOf (model); published.publish();
    }
    const griddata::Pattern& pattern (int i) const { return model[(size_t) juce::jlimit (0, griddata::kPatterns - 1, i)]; }
    void setPattern (int i, const griddata::Pattern& p) { model[(size_t) juce::jlimit (0, griddata::kPatterns - 1, i)] = p; published.edit() = masksOf (model); published.publish(); }

    std::atomic<float> uiPos { -1.0f }; std::atomic<int> uiPattern { 0 };
    // test hook: absolute sample (since reset) and absolute step of each row's latest block start
    std::array<long long, griddata::kRows> debugEnterAt {}; std::array<long long, griddata::kRows> debugEnterStep {}; std::array<int, griddata::kRows> debugEnters {};

protected:
    void prepareEffect (double sampleRate, int maxBlockSize, int) override
    {
        iPattern = id ("pattern"); iSteps = id ("steps"); iSeed = id ("seed");
        for (int r = 0; r < griddata::kRows; ++r) iKnob[(size_t) r] = id (griddata::row (r).key);
        const int mb = juce::jmax (maxBlockSize, kMaxBlock);
        for (auto* e : std::initializer_list<mng::EffectBase*> { &gater, &loop, &stut, &rev, &frz, &qnt, &flt, &dly }) e->prepare (sampleRate, mb, 2);
        rowBuf.setSize (2, mb); sendBuf.setSize (2, mb); wetBuf.setSize (2, mb);
        tape.prepare ((int) (sampleRate * 6.0)); for (auto& h : heads) h.prepare (sampleRate);
        verb.prepare (sampleRate, mb); verb.setRoomSize (1.0f); verb.setWidth (1.0f);
        { juce::AudioBuffer<float> t (2, mb); t.clear(); verb.process (t.getReadPointer (0), t.getReadPointer (1), t.getWritePointer (0), t.getWritePointer (1), mb); }   // sized at its largest room here, never on the audio thread
        for (auto& ps : shift) { ps.prepare (sampleRate, 60.0f); ps.setWindow (40.0f); ps.setSmoothingTime (0.005f); }
        up = 1.0f - (float) std::exp (-1.0 / (sampleRate * 0.002 * 0.5)); down = 1.0f - (float) std::exp (-1.0 / (sampleRate * 0.004 * 0.5));
        resetEffect();
    }
    void resetEffect() override
    {
        for (auto* e : std::initializer_list<mng::EffectBase*> { &gater, &loop, &stut, &rev, &frz, &qnt, &flt, &dly }) e->reset();
        tape.clear(); for (auto& h : heads) h.reset(); verb.reset(); for (auto& ps : shift) ps.reset();
        for (auto& r : rows) r = {};
        lastStepAbs = LLONG_MIN; lastPat = -1; absPos = 0; tailQuiet = 0; dlyTail = verbTail = false;
        debugEnterAt.fill (-1); debugEnterStep.fill (-1); debugEnters.fill (0);
    }
    void process (juce::dsp::AudioBlock<float> block, const ModulationContext& ctx) noexcept override
    {
        using namespace rhythm; using namespace griddata;
        beginBlock (ctx);
        published.fetch();
        const auto& M = published.read();
        const int n = (int) block.getNumSamples(), nch = juce::jmin (2, (int) block.getNumChannels());
        float* L = block.getChannelPointer (0); float* R = nch > 1 ? block.getChannelPointer (1) : L;
        HostClock clk; clk.set (ctx.tempo); bpm = ctx.tempo.bpm;
        const int pat = juce::jlimit (0, kPatterns - 1, choice (iPattern) - 1);
        const int steps = choice (iSteps) == 1 ? 32 : 16;
        const double stepBeats = ctx.tempo.beatsPerBar() / steps;
        stepLen = stepBeats * clk.spb;
        int a = 0;
        while (a < n)
        {
            const int b = clk.nextBoundary (a, n, stepBeats);
            const long long sAbs = clk.unitIndex (a, stepBeats);
            const int step = (int) positiveMod ((double) sAbs, (double) steps);
            if (sAbs != lastStepAbs) { onStep (M.m[(size_t) pat], sAbs, step, steps, sAbs == lastStepAbs + 1 && pat == lastPat); lastStepAbs = sAbs; lastPat = pat; }
            segment (L, R, nch, a, b, step);
            absPos += b - a; a = b;
        }
        uiPos.store ((float) positiveMod (clk.ppqAt (n) / ctx.tempo.beatsPerBar(), 1.0), std::memory_order_relaxed); uiPattern.store (pat, std::memory_order_relaxed);
    }

private:
    struct RowState { bool active = false, live = false; float g = 0.0f; int runStart = 0, runLen = 1; long long enteredAbs = -1; };
    static Masks masksOf (const Patterns& ps) { Masks m; for (int i = 0; i < griddata::kPatterns; ++i) m.m[(size_t) i] = ps[(size_t) i].rows; return m; }
    static void oneHot (std::array<std::atomic<float>*, 8>& w, int base) noexcept
    { for (int k = 0; k < 5; ++k) if (w[(size_t) k]) w[(size_t) k]->store (k == base ? 1.0f : 0.0f); if (w[5]) w[5]->store (1.0f); if (w[6]) w[6]->store (0.0f); if (w[7]) w[7]->store (0.0f); }
    mng::EffectBase* mangoOf (int r) noexcept
    {
        using namespace griddata;
        switch (r) { case rGate: return &gater; case rLoop: return &loop; case rStutter: return &stut; case rReverse: return &rev; case rFreeze: return &frz;
                     case rCrush: return &qnt; case rFilter: return &flt; case rDelay: return &dly; default: return nullptr; }
    }
    float knob (int r) const noexcept { const int i = iKnob[(size_t) r]; return i >= 0 ? juce::jlimit (0.0f, 1.0f, target (i)) : 0.5f; }

    // a new step: rows whose run starts here enter (Mango's block enter), rows that are no longer painted start their fade-out
    void onStep (const std::array<uint32_t, griddata::kRows>& m, long long sAbs, int step, int steps, bool contiguous) noexcept
    {
        using namespace griddata;
        for (int r = 0; r < kRows; ++r)
        {
            auto& st = rows[(size_t) r]; const uint32_t mask = steps == 32 ? m[(size_t) r] : (m[(size_t) r] & 0xffffu);
            const bool painted = ((mask >> step) & 1u) != 0;
            // a run starts on its first painted step, at the pattern start, or wherever the song jumps to (loop points, a new pattern)
            if (st.active && (! painted || step == 0 || ! contiguous)) { st.active = false; }
            const bool startsRun = painted && (step == 0 || ((mask >> (step - 1)) & 1u) == 0 || ! st.active);
            if (startsRun)
            {
                int len = 1; while (step + len < steps && ((mask >> (step + len)) & 1u)) ++len;
                st.runStart = step; st.runLen = len; st.active = true; st.enteredAbs = absPos;
                enter (r, step, len);
                debugEnterAt[(size_t) r] = absPos; debugEnterStep[(size_t) r] = sAbs; ++debugEnters[(size_t) r];
            }
        }
    }
    void enter (int r, int step, int len) noexcept
    {
        using namespace griddata;
        const float k = knob (r);
        if (auto* e = mangoOf (r))
        {
            switch (r)
            {
                case rGate:    oneHot (wGate, 1 + juce::jlimit (0, 3, (int) (k * 3.99f))); aGateAtt->store (0.02f); aGateRel->store (0.25f); break;   // RATE: 1/8 .. 1/64 chops
                case rLoop:    oneHot (wLoop, juce::jlimit (0, 3, (int) ((1.0f - k) * 3.99f))); break;                                                  // LENGTH: 1/32 .. 1/4 grains
                case rStutter: oneHot (wStut, 2 + juce::jlimit (0, 2, (int) (k * 2.99f))); break;                                                       // RATE: 1/16 .. 1/64
                case rReverse: aRevMix->store (k); break;
                case rFreeze:  oneHot (wFrz, 0); aFrzMix->store (k); break;
                case rCrush:   aBits->store (16.0f - 13.0f * k); aDown->store (1.0f + 23.0f * k * k); break;
                case rFilter:  oneHot (wFlt, 0); aF0->store (18000.0f); aF1->store (18000.0f * std::pow (1.0f - 0.97f * k, 2.0f) + 20.0f); aQ->store (0.9f + 3.0f * k); break;
                case rDelay:   aDur->store ((float) juce::jlimit (0.01, 2.0, 0.75 * 60.0 / juce::jmax (1.0, bpm))); aFb->store (0.9f * k); aDamp->store (0.35f); aDMix->store (0.6f); break;
                default: break;
            }
            mng::BlockContext c; c.laneIndex = r; c.blockId = step; c.seed = (uint64_t) choice (iSeed); c.sampleRate = sr; c.bpm = bpm;
            c.blockLengthSamples = (int) std::lround (len * stepLen);
            e->onBlockEnter (c);
        }
        else if (r == rVerb) verb.setRoomSize (k);
        else if (r == rTonal) { const float st = std::round (24.0f * k); for (auto& ps : shift) { ps.reset(); ps.setPitchSemitones (st); } }
        else if (r == rScratch || r == rTape) heads[(size_t) (r == rScratch ? 0 : 1)].reset();
    }

    void segment (float* L, float* R, int nch, int a, int b, int step) noexcept
    {
        using namespace griddata; using namespace rhythm;
        const int len = b - a;
        for (int i = a; i < b; ++i) tape.write (L[i], R[i]);
        // insert rows, in series
        static constexpr int order[] { rTape, rScratch, rLoop, rStutter, rReverse, rGate, rFreeze, rTonal, rFilter, rCrush };
        for (int r : order)
        {
            auto& st = rows[(size_t) r];
            if (! st.active && st.g <= 0.0f) continue;
            for (int c = 0; c < 2; ++c) rowBuf.copyFrom (c, a, c == 0 ? L + a : R + a, len);
            if (auto* e = mangoOf (r)) e->process (rowBuf, a, len);
            else if (r == rTonal) { shift[0].process (rowBuf.getReadPointer (0) + a, rowBuf.getWritePointer (0) + a, len); shift[1].process (rowBuf.getReadPointer (1) + a, rowBuf.getWritePointer (1) + a, len); }
            else
            {   // SCRATCH / TAPE STOP: TIME-12's buffer read along the run
                auto& h = heads[(size_t) (r == rScratch ? 0 : 1)];
                for (int i = a; i < b; ++i)
                {
                    const double oRun = (double) (absPos + (i - a) - st.enteredAbs), runLen = st.runLen * stepLen;
                    const double oStep = positiveMod (oRun, stepLen);
                    h.advance (st.active ? timeModeDelay (r == rScratch ? tmScratch : tmStop, oRun, runLen, stepLen, oStep, knob (r)) : h.curD);
                    rowBuf.setSample (0, i, h.tap (tape, 0, L[i])); rowBuf.setSample (1, i, h.tap (tape, 1, R[i]));
                }
            }
            const float* wl = rowBuf.getReadPointer (0); const float* wr = rowBuf.getReadPointer (1);
            const float tgt = st.active ? 1.0f : 0.0f;
            for (int i = a; i < b; ++i)
            {
                st.g += (tgt > st.g ? up : down) * (tgt - st.g); if (st.g < 1.0e-5f && tgt == 0.0f) st.g = 0.0f;
                L[i] += st.g * (wl[i] - L[i]); if (nch > 1) R[i] += st.g * (wr[i] - R[i]);
            }
            if (! st.active && st.g <= 0.0f) { if (auto* e = mangoOf (r)) e->onBlockExit(); }
        }
        // throws: DELAY and VERB take the painted steps and ring on
        sendBuf.clear (a, len);
        bool any = false;
        for (int r : { rDelay, rVerb })
        {
            auto& st = rows[(size_t) r];
            const bool running = st.active || st.g > 0.0f || (r == rDelay ? dlyTail : verbTail);
            if (! running) continue;
            any = true; const float tgt = st.active ? 1.0f : 0.0f; float g = st.g;
            for (int i = a; i < b; ++i) { g += (tgt > g ? up : down) * (tgt - g); if (g < 1.0e-5f && tgt == 0.0f) g = 0.0f; wetBuf.setSample (0, i, L[i] * g); wetBuf.setSample (1, i, (nch > 1 ? R[i] : L[i]) * g); }
            st.g = g;
            if (r == rDelay)
            {
                for (int c = 0; c < 2; ++c) rowBuf.copyFrom (c, a, wetBuf, c, a, len);
                dly.process (rowBuf, a, len);    // Mango's delay adds its echoes to what it is given: the wet is the difference
                for (int c = 0; c < 2; ++c) { auto* s = sendBuf.getWritePointer (c); const auto* o = rowBuf.getReadPointer (c); const auto* in = wetBuf.getReadPointer (c); for (int i = a; i < b; ++i) s[i] += o[i] - in[i]; }
            }
            else
            {
                verb.process (wetBuf.getReadPointer (0) + a, wetBuf.getReadPointer (1) + a, rowBuf.getWritePointer (0) + a, rowBuf.getWritePointer (1) + a, len);
                for (int c = 0; c < 2; ++c) sendBuf.addFrom (c, a, rowBuf, c, a, len, 0.5f);
            }
        }
        if (any)
        {
            float pk = 0.0f;
            for (int i = a; i < b; ++i) { const float sl = sendBuf.getSample (0, i), sr2 = sendBuf.getSample (1, i); L[i] += sl; if (nch > 1) R[i] += sr2; pk = std::max (pk, std::max (std::abs (sl), std::abs (sr2))); }
            // a throw stops computing once nothing is sent and its tail is below -120 dBFS for half a second
            const bool sending = rows[rDelay].active || rows[rDelay].g > 0 || rows[rVerb].active || rows[rVerb].g > 0;
            if (sending || pk > 1.0e-6f) { tailQuiet = 0; dlyTail = dlyTail || rows[rDelay].active; verbTail = verbTail || rows[rVerb].active; }
            else if ((tailQuiet += len) > (int) (sr * 0.5)) { if (dlyTail) dly.onBlockExit(); dlyTail = verbTail = false; tailQuiet = 0; }
        }
        juce::ignoreUnused (step);
    }

    Patterns model;   // message thread
    rhythm::Published<Masks> published;
    std::unique_ptr<MangoParamHost> host;
    mng::GaterEffect gater; mng::GrainDupEffect loop, stut; mng::ReverserEffect rev; mng::FreezeEffect frz; mng::QuantizerEffect qnt; mng::FilterEnvEffect flt; mng::DelayEffect dly;
    fxme::Reverb verb; std::array<fxme::PitchShifter, 2> shift;
    rhythm::TapeBuffer tape; std::array<rhythm::TimeHead, 2> heads;
    juce::AudioBuffer<float> rowBuf, sendBuf, wetBuf;
    std::array<std::atomic<float>*, 8> wGate {}, wLoop {}, wStut {}, wFrz {}, wFlt {};
    std::atomic<float>* aGateRel = nullptr; std::atomic<float>* aGateAtt = nullptr; std::atomic<float>* aRevMix = nullptr; std::atomic<float>* aFrzMix = nullptr;
    std::atomic<float>* aBits = nullptr; std::atomic<float>* aDown = nullptr; std::atomic<float>* aF0 = nullptr; std::atomic<float>* aF1 = nullptr; std::atomic<float>* aQ = nullptr;
    std::atomic<float>* aDur = nullptr; std::atomic<float>* aFb = nullptr; std::atomic<float>* aDamp = nullptr; std::atomic<float>* aDMix = nullptr;
    std::array<RowState, griddata::kRows> rows {}; std::array<int, griddata::kRows> iKnob {};
    int iPattern = 0, iSteps = 1, iSeed = 12, tailQuiet = 0; bool dlyTail = false, verbTail = false;
    long long lastStepAbs = LLONG_MIN, absPos = 0; int lastPat = -1; double stepLen = 6000.0, bpm = 120.0; float up = 0.01f, down = 0.005f;
};
} // namespace phantom
