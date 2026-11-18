#include "GhostLayers.h"
#include "GhostEngine.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <functional>
#include <juce_dsp/juce_dsp.h>

namespace ghost::layers
{
namespace
{
// a host parameter that is a view of one engine parameter (the engine keeps the value)
class EngineParam : public juce::RangedAudioParameter
{
  public:
    EngineParam(juce::RangedAudioParameter &e, const juce::String &id, const juce::String &name)
        : juce::RangedAudioParameter(juce::ParameterID(id, 1), name), eng(e)
    {
    }
    const juce::NormalisableRange<float> &getNormalisableRange() const override { return eng.getNormalisableRange(); }
    float getValue() const override { return eng.getValue(); }
    void setValue(float v) override { eng.setValue(v); }
    float getDefaultValue() const override { return eng.getDefaultValue(); }
    int getNumSteps() const override { return eng.getNumSteps(); }
    bool isDiscrete() const override { return eng.isDiscrete(); }
    bool isBoolean() const override { return eng.isBoolean(); }
    juce::String getText(float v, int len) const override { return eng.getText(v, len); }
    float getValueForText(const juce::String &t) const override { return eng.getValueForText(t); }
    juce::String getLabel() const override { return eng.getLabel(); }

  private:
    juce::RangedAudioParameter &eng;
};

// a host parameter for one of our own mix controls (on/off, level, pan)
class MixParam : public juce::RangedAudioParameter
{
  public:
    MixParam(const juce::String &id, const juce::String &name, juce::NormalisableRange<float> r, float def, int steps, bool isBool,
             std::function<float()> g, std::function<void(float)> s, std::function<juce::String(float)> f)
        : juce::RangedAudioParameter(juce::ParameterID(id, 1), name), range(r), defV(def), nSteps(steps), boolean(isBool),
          get(std::move(g)), set(std::move(s)), fmt(std::move(f))
    {
    }
    const juce::NormalisableRange<float> &getNormalisableRange() const override { return range; }
    float getValue() const override { return range.convertTo0to1(get()); }
    void setValue(float v) override { set(range.convertFrom0to1(v)); }
    float getDefaultValue() const override { return range.convertTo0to1(defV); }
    int getNumSteps() const override { return nSteps; }
    bool isDiscrete() const override { return nSteps != juce::AudioProcessor::getDefaultNumParameterSteps(); }
    bool isBoolean() const override { return boolean; }
    juce::String getText(float v, int) const override { return fmt(range.convertFrom0to1(v)); }
    float getValueForText(const juce::String &t) const override { return range.convertTo0to1(t.getFloatValue()); }

  private:
    juce::NormalisableRange<float> range;
    float defV;
    int nSteps;
    bool boolean;
    std::function<float()> get;
    std::function<void(float)> set;
    std::function<juce::String(float)> fmt;
};

struct MoveSpec
{
    const char *id;
    float swing, wander, lo, hi;
    const char *why; // non-null: not moved, and why
};
// SUB (Bombaz): width and drive are the XY pad; glide time; the others are pitch / octave and are left alone
const MoveSpec subSpec[] = {{"width", 0.30f, 0.25f, -0.45f, 0.45f, nullptr},
                            {"drive", 0.25f, 0.20f, -0.40f, 0.40f, nullptr},
                            {"glideTime", 0.15f, 0.10f, 0.f, 0.30f, nullptr},
                            {"octave", 0, 0, 0, 0, "a switch (it would jump an octave)"},
                            {"tuning", 0, 0, 0, 0, "tuning (it would put the sub out of tune)"}};
} // namespace

// the mix-stage tools: JUCE's own LinkwitzRileyFilter (24 dB/oct; its low and high outputs sum to an all-pass, i.e. flat) - no filter math of ours
struct Host::BlendDsp
{
    juce::dsp::LinkwitzRileyFilter<float> lc[2], hc[2], mb, mt[2];
    float rLc[2]{}, rHc[2]{}, rMono[2]{}, rPh[2]{}, rMb{};
    float curLc[2]{}, curHc[2]{}, curMb{};
    bool lcDirty[2]{}, hcDirty[2]{}, mbDirty{};
    bool settling{false};
    double E[2]{}, D{};
    float maxHz{20000.f};
    void prepare(double sr)
    {
        juce::dsp::ProcessSpec spec{sr, 64, 2};
        for (auto *f : {&lc[0], &lc[1], &hc[0], &hc[1], &mb, &mt[0], &mt[1]})
        {
            f->prepare(spec);
            f->setCutoffFrequency(100.f);
            f->reset();
        }
        for (auto &f : mt)
            f.setCutoffFrequency(200.f);
        for (int i = 0; i < 2; ++i)
            rLc[i] = rHc[i] = rMono[i] = rPh[i] = 0.f, curLc[i] = curHc[i] = 0.f, lcDirty[i] = hcDirty[i] = false;
        rMb = 0.f;
        curMb = 0.f;
        mbDirty = false;
        settling = false;
        maxHz = (float)(0.45 * sr);
    }
};

Host::Host(ghost::GhostEngine &g) : gb(g), dsp(std::make_unique<BlendDsp>())
{
    surge.on = true;
    gb.layersSave = [this]() { return saveText(); };
    gb.layersLoad = [this](const std::string &s) { loadText(s); };
    registerTerrain();
    mixer.setMain(gb.mainSynth());
}

void Host::registerTerrain()
{
    auto reg = [this](EngineBase &e, const MoveSpec *spec, size_t n) {
        const std::string pre = "sub.";
        for (size_t i = 0; i < n; ++i)
        {
            auto *p = e.param(spec[i].id);
            if (!p)
                continue;
            const std::string name = pre + spec[i].id, label = "SUB " + p->getName(40).toStdString();
            if (spec[i].why)
            {
                gb.extSkipped.emplace_back(name, spec[i].why);
                cover.push_back({name, label, spec[i].why});
                continue;
            }
            ghost::GhostEngine::ExtTarget t;
            t.name = name;
            t.label = label;
            t.swing = spec[i].swing;
            t.wander = spec[i].wander;
            t.lo = spec[i].lo;
            t.hi = spec[i].hi;
            t.enabled = [this]() { return sub.on.load(std::memory_order_relaxed); };
            t.get = [p]() { return p->getValue(); };
            t.set = [p](float v) { p->setValue(v); };
            gb.addExt(std::move(t));
            cover.push_back({name, label, ""});
        }
    };
    reg(subEngine, subSpec, std::size(subSpec));
    for (const char *m : {"surge.level", "sub.level", "sub.pan"})
        cover.push_back({m, m, "your mix (LEVEL / PAN stay where you put them)"});
    for (const char *m : {"blend.stack", "blend.split", "blend.monobass", "blend.monobelow", "blend.autogain", "surge.mono", "surge.phase", "surge.lowcut",
                          "surge.highcut", "sub.mono", "sub.phase", "sub.lowcut", "sub.highcut"})
        cover.push_back({m, m, "your blend settings (they keep the low end clean, so the land does not move them)"});
}

std::vector<Host::Cover> Host::terrainCoverage() const { return cover; }

juce::RangedAudioParameter *Host::hostParam(const juce::String &id) const
{
    auto it = hostParams.find(id.toStdString());
    return it == hostParams.end() ? nullptr : it->second;
}

juce::RangedAudioParameter *Host::uiParam(const juce::String &engineId)
{
    if (auto *h = hostParam(juce::String("layer_sub_") + engineId))
        return h;
    return subEngine.param(engineId);
}

std::vector<std::unique_ptr<juce::RangedAudioParameter>> Host::makeHostParams()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> out;
    auto add = [&](std::unique_ptr<juce::RangedAudioParameter> p) {
        hostParams[p->getParameterID().toStdString()] = p.get();
        out.push_back(std::move(p));
    };
    auto pct = [](float v) { return juce::String((int)std::lround(v * 100.f)) + "%"; };
    auto onoff = [](float v) { return juce::String(v > 0.5f ? "On" : "Off"); };
    auto panTxt = [](float v) { return std::fabs(v) < 0.005f ? juce::String("Centre") : juce::String((int)std::lround(std::fabs(v) * 100.f)) + (v < 0 ? " L" : " R"); };
    struct L
    {
        const char *key, *nm;
        Mix *m;
        bool hasPan;
    };
    for (auto l : {L{"surge", "ENGINE", &surge, false}, L{"sub", "SUB", &sub, true}})
    {
        Mix *m = l.m;
        const juce::String k = l.key, n = l.nm;
        add(std::make_unique<MixParam>("layer_" + k + "_on", n + " On", juce::NormalisableRange<float>(0.f, 1.f, 1.f), k == "surge" ? 1.f : 0.f, 2, true,
                                       [m]() { return m->on.load() ? 1.f : 0.f; },
                                       [this, m](float v) { m->on = v > 0.5f; touch(); }, onoff));
        add(std::make_unique<MixParam>("layer_" + k + "_level", n + " Level", juce::NormalisableRange<float>(0.f, 1.5f), 1.f,
                                       juce::AudioProcessor::getDefaultNumParameterSteps(), false, [m]() { return m->level.load(); },
                                       [m](float v) { m->level = v; }, pct));
        if (l.hasPan)
            add(std::make_unique<MixParam>("layer_" + k + "_pan", n + " Pan", juce::NormalisableRange<float>(-1.f, 1.f), 0.f,
                                           juce::AudioProcessor::getDefaultNumParameterSteps(), false, [m]() { return m->pan.load(); },
                                           [m](float v) { m->pan = std::fabs(v) < 0.04f ? 0.f : v; }, panTxt));
    }
    // BLEND: per layer (MONO, PHASE, LOW CUT, HIGH CUT) and the master tools
    auto hzTxt = [](float v) { return v < 1.f ? juce::String("Off") : juce::String((int)std::lround(v)) + " Hz"; };
    for (auto l : {L{"surge", "ENGINE", &surge, false}, L{"sub", "SUB", &sub, true}})
    {
        Mix *m = l.m;
        const juce::String k = l.key, n = l.nm;
        add(std::make_unique<MixParam>("layer_" + k + "_mono", n + " Mono", juce::NormalisableRange<float>(0.f, 1.f, 1.f), 0.f, 2, true,
                                       [m]() { return m->mono.load() ? 1.f : 0.f; }, [m](float v) { m->mono = v > 0.5f; }, onoff));
        add(std::make_unique<MixParam>("layer_" + k + "_phase", n + " Phase Flip", juce::NormalisableRange<float>(0.f, 1.f, 1.f), 0.f, 2, true,
                                       [m]() { return m->phase.load() ? 1.f : 0.f; }, [m](float v) { m->phase = v > 0.5f; }, onoff));
        add(std::make_unique<MixParam>("layer_" + k + "_lowcut", n + " Low Cut", juce::NormalisableRange<float>(0.f, 2000.f, 0.f, 0.35f), 0.f,
                                       juce::AudioProcessor::getDefaultNumParameterSteps(), false, [m]() { return m->lowCut.load(); },
                                       [m](float v) { m->lowCut = v < 20.f ? 0.f : v; }, hzTxt));
        add(std::make_unique<MixParam>("layer_" + k + "_highcut", n + " High Cut", juce::NormalisableRange<float>(0.f, 20000.f, 0.f, 0.3f), 0.f,
                                       juce::AudioProcessor::getDefaultNumParameterSteps(), false, [m]() { return m->highCut.load(); },
                                       [m](float v) { m->highCut = v < 20.f ? 0.f : v; }, hzTxt));
    }
    {
        Blend *b = &blend;
        add(std::make_unique<MixParam>("layer_blend_stack", "Stack Mode", juce::NormalisableRange<float>(0.f, 1.f, 1.f), 1.f, 2, true,
                                       [b]() { return b->stack.load() ? 1.f : 0.f; }, [b](float v) { b->stack = v > 0.5f; }, onoff));
        add(std::make_unique<MixParam>("layer_blend_split", "Split", juce::NormalisableRange<float>(40.f, 300.f), 110.f,
                                       juce::AudioProcessor::getDefaultNumParameterSteps(), false, [b]() { return b->split.load(); },
                                       [b](float v) { b->split = v; }, hzTxt));
        add(std::make_unique<MixParam>("layer_blend_monobass", "Mono Bass", juce::NormalisableRange<float>(0.f, 1.f, 1.f), 1.f, 2, true,
                                       [b]() { return b->monoBass.load() ? 1.f : 0.f; }, [b](float v) { b->monoBass = v > 0.5f; }, onoff));
        add(std::make_unique<MixParam>("layer_blend_monobelow", "Mono Bass Below", juce::NormalisableRange<float>(40.f, 300.f), 120.f,
                                       juce::AudioProcessor::getDefaultNumParameterSteps(), false, [b]() { return b->monoBelow.load(); },
                                       [b](float v) { b->monoBelow = v; }, hzTxt));
        add(std::make_unique<MixParam>("layer_blend_autogain", "Auto Gain", juce::NormalisableRange<float>(0.f, 1.f, 1.f), 1.f, 2, true,
                                       [b]() { return b->autoGain.load() ? 1.f : 0.f; }, [b](float v) { b->autoGain = v > 0.5f; }, onoff));
    }
    for (auto *p : subEngine.apvts().processor.getParameters())
        if (auto *rp = dynamic_cast<juce::RangedAudioParameter *>(p))
            add(std::make_unique<EngineParam>(*rp, "layer_sub_" + rp->getParameterID(), "SUB " + rp->getName(40)));
    return out;
}

Host::~Host()
{
    gb.layersSave = nullptr;
    gb.layersLoad = nullptr;
    gb.ext.clear(); // the TERRAIN targets point into this Host
    gb.layerWanted.store(false, std::memory_order_relaxed);
}

void Host::layerWantedSync() { gb.layerWanted.store(sub.on.load() || mixer.wantsEvents(), std::memory_order_relaxed); }

void Host::prepare(double sampleRate)
{
    sr = sampleRate;
    subEngine.prepare(sampleRate, 32);
    dsp->prepare(sampleRate);
    mixer.prepare(sampleRate);
    gSub = 0.f;
    subWasOn = false;
    prepared = true;
}

static void panGains(float pan, float *g)
{
    g[0] = pan > 0.f ? 1.f - pan : 1.f;
    g[1] = pan < 0.f ? 1.f + pan : 1.f;
}

void Host::block(float *L, float *R, int n)
{
    const bool sOn = sub.on.load(std::memory_order_relaxed);
    gb.layerWanted.store(sOn || mixer.wantsEvents(), std::memory_order_relaxed);
    gb.boardOwnsNotes.store(prepared && mixer.wantsEvents(), std::memory_order_relaxed);
    // the BLEND BOARD: while it is the mixer, the main engine's block is replaced by the board's mix (before the events are consumed)
    if (prepared)
    {
        mixer.splitHz.store(blend.split.load(std::memory_order_relaxed), std::memory_order_relaxed);
        mixer.subHighPass.store(sOn && !blend.stack.load(std::memory_order_relaxed), std::memory_order_relaxed);
        mixer.block(L, R, n, gb);
    }
    // AUTO GAIN: both layers on = each one a little lower (equal power), moved with the same gain ramps
    const bool suOn = surge.on.load(std::memory_order_relaxed);
    const float ag = (sOn && blend.autoGain.load(std::memory_order_relaxed)) ? 1.f / std::sqrt((float)std::max(1, (int)suOn + (int)sOn)) : 1.f;
    const float tSurge = suOn ? surge.level.load(std::memory_order_relaxed) * ag : 0.f;
    const float tSub = sOn ? sub.level.load(std::memory_order_relaxed) * ag : 0.f;

    // events: handed over, in order, at the start of this block (Surge itself starts notes at block starts)
    const int ne = gb.layerEvN;
    gb.layerEvN = 0;
    if (!prepared)
        return;

    // nothing to do: leave the buffer alone (bit-identical to build 9)
    const bool full = blendRequested() || dsp->settling || blend.meterOn.load(std::memory_order_relaxed);
    if (!full && tSurge == 1.f && gSurge == 1.f && !sOn && gSub == 0.f)
    {
        if (subWasOn)
        {
            subEngine.reset();
            subWasOn = false;
        }
        return;
    }

    if (sOn || gSub > 0.f)
    {
        if (sOn && !subWasOn)
            subEngine.reset();
        subWasOn = sOn;
        for (int i = 0; i < ne && sOn; ++i)
        {
            const auto &e = gb.layerEv[i];
            switch (e.type)
            {
            case 0: subEngine.midi(0x90, e.a, e.b); break;
            case 1: subEngine.midi(0x80, e.a, 0); break;
            case 2: { int v = e.c + 8192; subEngine.midi(0xE0, (uint8_t)(v & 127), (uint8_t)(v >> 7)); } break;
            case 3: subEngine.midi(0xB0, e.a, e.b); break;
            case 4: subEngine.midi(0xB0, 123, 0); break;
            }
        }
        subEngine.render(bufSub.data(), n);
        mixer.pushSub(bufSub.data(), n, sOn ? gSub : 0.f);
    }
    if (!full)
    {
        float pS[2];
        panGains(sub.pan.load(std::memory_order_relaxed), pS);
        const float inv = 1.f / (float)n;
        const float d0 = (tSurge - gSurge) * inv, d1 = (tSub - gSub) * inv;
        float a0 = gSurge, a1 = gSub;
        for (int i = 0; i < n; ++i)
        {
            a0 += d0;
            a1 += d1;
            const float s = a1 * bufSub[i];
            L[i] = (tSurge == 1.f && gSurge == 1.f ? L[i] : a0 * L[i]) + s * pS[0];
            R[i] = (tSurge == 1.f && gSurge == 1.f ? R[i] : a0 * R[i]) + s * pS[1];
        }
    }
    else
        blendBlock(L, R, n, tSurge, tSub, sOn);
    gSurge = tSurge;
    gSub = tSub;
    // a layer that has been faded out is cleared so it starts clean next time
    if (!sOn && gSub == 0.f)
        subEngine.reset();
}

void Host::blendTest(float *L, float *R, const float *subIn, int n)
{
    n = std::min(n, 64);
    std::copy(subIn, subIn + n, bufSub.begin());
    gSurge = gSub = 1.f;
    blendBlock(L, R, n, 1.f, 1.f, true);
}

bool Host::blendRequested() const
{
    for (const Mix *m : {&surge, &sub})
        if (m->mono.load(std::memory_order_relaxed) || m->phase.load(std::memory_order_relaxed) ||
            m->lowCut.load(std::memory_order_relaxed) > 0.f || m->highCut.load(std::memory_order_relaxed) > 0.f)
            return true;
    // GHOST LOCK is on by default but only acts while the SUB layer is on (so the engine alone is never touched)
    return sub.on.load(std::memory_order_relaxed) &&
           (blend.stack.load(std::memory_order_relaxed) || blend.monoBass.load(std::memory_order_relaxed) || blend.autoGain.load(std::memory_order_relaxed));
}

// The mix stage with the GHOST LOCK tools engaged (any of them on, still settling, or the clash meter open).
void Host::blendBlock(float *L, float *R, int n, float tSurge, float tSub, bool sOn)
{
    auto &d = *dsp;
    const bool stack = blend.stack.load(std::memory_order_relaxed) && sOn;
    const float split = std::clamp(blend.split.load(std::memory_order_relaxed), 20.f, d.maxHz);
    struct LS
    {
        float lc, hc;
        bool mono, phase;
    } ls[2];
    Mix *mixes[2] = {&surge, &sub};
    for (int i = 0; i < 2; ++i)
    {
        ls[i] = {mixes[i]->lowCut.load(std::memory_order_relaxed), mixes[i]->highCut.load(std::memory_order_relaxed),
                 mixes[i]->mono.load(std::memory_order_relaxed), mixes[i]->phase.load(std::memory_order_relaxed)};
        if (stack)
        {
            if (i == 0)
                ls[i].lc = std::max(ls[i].lc, split);
            else
                ls[i].hc = ls[i].hc > 0.f ? std::min(ls[i].hc, split) : split;
        }
        ls[i].lc = ls[i].lc > 0.f ? std::clamp(ls[i].lc, 20.f, d.maxHz) : 0.f;
        ls[i].hc = ls[i].hc > 0.f ? std::clamp(ls[i].hc, 20.f, d.maxHz) : 0.f;
    }
    static constexpr float kStep = 1.f / 16.f; // a switch takes 16 blocks (about 11 ms) to fade in or out
    auto approach = [](float cur, float tgt) { return cur + std::clamp(tgt - cur, -kStep, kStep); };
    bool settling = false;

    float sl[64], sr[64];
    std::copy(L, L + n, sl);
    std::copy(R, R + n, sr);
    float *sig[2][2] = {{sl, sr}, {bufSub.data(), nullptr}};
    for (int i = 0; i < 2; ++i)
    {
        const bool lcT = ls[i].lc > 0.f, hcT = ls[i].hc > 0.f;
        const float lc0 = d.rLc[i], hc0 = d.rHc[i], mo0 = d.rMono[i], ph0 = d.rPh[i];
        const float lc1 = approach(lc0, lcT ? 1.f : 0.f), hc1 = approach(hc0, hcT ? 1.f : 0.f);
        const float mo1 = approach(mo0, ls[i].mono ? 1.f : 0.f), ph1 = approach(ph0, ls[i].phase ? 1.f : 0.f);
        d.rLc[i] = lc1;
        d.rHc[i] = hc1;
        d.rMono[i] = mo1;
        d.rPh[i] = ph1;
        settling |= lc1 != (lcT ? 1.f : 0.f) || hc1 != (hcT ? 1.f : 0.f) || mo1 != (ls[i].mono ? 1.f : 0.f) || ph1 != (ls[i].phase ? 1.f : 0.f);
        const bool lcAct = lcT || lc1 > 0.f, hcAct = hcT || hc1 > 0.f;
        if (lcT && ls[i].lc != d.curLc[i])
            d.lc[i].setCutoffFrequency(d.curLc[i] = ls[i].lc);
        if (hcT && ls[i].hc != d.curHc[i])
            d.hc[i].setCutoffFrequency(d.curHc[i] = ls[i].hc);
        if (!lcAct && d.lcDirty[i])
            d.lc[i].reset(), d.lcDirty[i] = false;
        if (!hcAct && d.hcDirty[i])
            d.hc[i].reset(), d.hcDirty[i] = false;
        const float invn = 1.f / (float)n;
        const int chans = sig[i][1] ? 2 : 1;
        for (int ch = 0; ch < chans; ++ch)
        {
            float *x = sig[i][ch];
            if (lcAct)
            {
                d.lcDirty[i] = true;
                for (int k = 0; k < n; ++k)
                {
                    float lo, hi;
                    d.lc[i].processSample(ch, x[k], lo, hi);
                    x[k] += (hi - x[k]) * (lc0 + (lc1 - lc0) * (float)(k + 1) * invn);
                }
            }
            if (hcAct)
            {
                d.hcDirty[i] = true;
                for (int k = 0; k < n; ++k)
                {
                    float lo, hi;
                    d.hc[i].processSample(ch, x[k], lo, hi);
                    x[k] += (lo - x[k]) * (hc0 + (hc1 - hc0) * (float)(k + 1) * invn);
                }
            }
        }
        if (sig[i][1] && (mo0 > 0.f || mo1 > 0.f))
            for (int k = 0; k < n; ++k)
            {
                const float a = mo0 + (mo1 - mo0) * (float)(k + 1) * invn, m = 0.5f * (sig[i][0][k] + sig[i][1][k]);
                sig[i][0][k] += (m - sig[i][0][k]) * a;
                sig[i][1][k] += (m - sig[i][1][k]) * a;
            }
        if (ph0 > 0.f || ph1 > 0.f)
            for (int ch = 0; ch < chans; ++ch)
                for (int k = 0; k < n; ++k)
                    sig[i][ch][k] *= 1.f - 2.f * (ph0 + (ph1 - ph0) * (float)(k + 1) * invn);
    }

    float pS[2];
    panGains(ls[1].mono ? 0.f : sub.pan.load(std::memory_order_relaxed), pS);
    const float inv = 1.f / (float)n;
    const float d0 = (tSurge - gSurge) * inv, d1 = (tSub - gSub) * inv;
    float a0 = gSurge, a1 = gSub;
    const bool meter = blend.meterOn.load(std::memory_order_relaxed);
    double ss[2]{}, dd = 0;
    for (int i = 0; i < n; ++i)
    {
        a0 += d0;
        a1 += d1;
        const float s = a1 * bufSub[i];
        const float xl = a0 * sl[i], xr = a0 * sr[i];
        L[i] = xl + s * pS[0];
        R[i] = xr + s * pS[1];
        if (meter)
        {
            // the clash meter listens to each layer below 200 Hz (JUCE's crossover again; it only measures, it is not in the sound)
            float m[2] = {0.5f * (xl + xr), 0.5f * s * (pS[0] + pS[1])}, lo, hi;
            for (int j = 0; j < 2; ++j)
            {
                d.mt[j].processSample(0, m[j], lo, hi);
                m[j] = lo;
                ss[j] += (double)lo * lo;
            }
            dd += (double)m[0] * m[1];
        }
    }
    if (meter)
    {
        for (int j = 0; j < 2; ++j)
        {
            d.E[j] = d.E[j] * 0.97 + 0.03 * ss[j] / n;
            blend.meterDb[j].store((float)(10.0 * std::log10(d.E[j] + 1e-12)), std::memory_order_relaxed);
        }
        d.D = d.D * 0.97 + 0.03 * dd / n;
        const double den = std::sqrt(d.E[0] * d.E[1]);
        blend.meterCorr.store(den > 1e-9 ? (float)std::clamp(d.D / den, -1.0, 1.0) : 0.f, std::memory_order_relaxed);
    }
    else
        d.E[0] = d.E[1] = d.D = 0.0;

    // MONO BASS: below MONO BELOW the two sides are summed (only while SUB is on)
    const bool mbT = blend.monoBass.load(std::memory_order_relaxed) && sOn;
    const float mb0 = d.rMb, mb1 = approach(mb0, mbT ? 1.f : 0.f);
    d.rMb = mb1;
    settling |= mb1 != (mbT ? 1.f : 0.f);
    if (mbT || mb1 > 0.f)
    {
        const float hz = std::clamp(blend.monoBelow.load(std::memory_order_relaxed), 20.f, d.maxHz);
        if (hz != d.curMb)
            d.mb.setCutoffFrequency(d.curMb = hz);
        d.mbDirty = true;
        for (int i = 0; i < n; ++i)
        {
            float loL, hiL, loR, hiR;
            d.mb.processSample(0, L[i], loL, hiL);
            d.mb.processSample(1, R[i], loR, hiR);
            const float a = mb0 + (mb1 - mb0) * (float)(i + 1) * inv, m = 0.5f * (loL + loR);
            L[i] += ((m + hiL) - L[i]) * a;
            R[i] += ((m + hiR) - R[i]) * a;
        }
    }
    else if (d.mbDirty)
    {
        d.mb.reset();
        d.mbDirty = false;
    }
    d.settling = settling;
}

bool Host::blendIsDefault() const
{
    for (const Mix *m : {&surge, &sub})
        if (m->mono.load() || m->phase.load() || m->lowCut.load() != 0.f || m->highCut.load() != 0.f)
            return false;
    return blend.stack.load() && blend.split.load() == 110.f && blend.monoBass.load() && blend.monoBelow.load() == 120.f && blend.autoGain.load();
}

bool Host::isDefault() const
{
    return surge.on.load() && surge.level.load() == 1.f && !sub.on.load() && sub.level.load() == 1.f && sub.pan.load() == 0.f &&
           const_cast<Host *>(this)->subEngine.isDefault() && blendIsDefault() && mixer.isDefault() && gb.tempoMul.load() == 1;
}

static void putParams(std::ostringstream &o, const char *tag, EngineBase &e)
{
    for (auto *p : e.apvts().processor.getParameters())
        if (auto *rp = dynamic_cast<juce::RangedAudioParameter *>(p))
            o << tag << "." << rp->getParameterID().toStdString() << "=" << rp->getValue() << ";";
}

std::string Host::saveText()
{
    if (isDefault())
        return {};
    std::ostringstream o;
    o.precision(9);
    o << "v=2;surge.on=" << surge.on.load() << ";surge.level=" << surge.level.load() << ";sub.on=" << sub.on.load()
      << ";sub.level=" << sub.level.load() << ";sub.pan=" << sub.pan.load() << ";";
    {
        int i = 0;
        for (const Mix *m : {&surge, &sub})
        {
            const std::string t = i == 0 ? "surge" : "sub";
            ++i;
            if (m->mono.load()) o << t << ".mono=1;";
            if (m->phase.load()) o << t << ".phase=1;";
            if (m->lowCut.load() != 0.f) o << t << ".lowcut=" << m->lowCut.load() << ";";
            if (m->highCut.load() != 0.f) o << t << ".highcut=" << m->highCut.load() << ";";
        }
        // GHOST LOCK is on by default: what is saved is what differs (off, or another value)
        o << "blend.stack=" << (blend.stack.load() ? 1 : 0) << ";";
        if (blend.split.load() != 110.f) o << "blend.split=" << blend.split.load() << ";";
        o << "blend.monobass=" << (blend.monoBass.load() ? 1 : 0) << ";";
        if (blend.monoBelow.load() != 120.f) o << "blend.monobelow=" << blend.monoBelow.load() << ";";
        o << "blend.autogain=" << (blend.autoGain.load() ? 1 : 0) << ";";
    }
    if (gb.tempoMul.load() != 1)
        o << "tmul=" << gb.tempoMul.load() << ";";
    putParams(o, "sub", subEngine);
    mixer.save(o);
    return o.str();
}

void Host::resetAll()
{
    surge.on = true;
    surge.level = 1.f;
    sub.on = false;
    sub.level = 1.f;
    sub.pan = 0.f;
    sub.octave = 0;
    subEngine.setToDefaults();
    resetBlend();
    mixer.resetAll();
    gb.tempoMul = 1;
}

void Host::resetBlend()
{
    for (auto *m : {&surge, &sub})
    {
        m->mono = m->phase = false;
        m->lowCut = m->highCut = 0.f;
    }
    blend.stack = true;
    blend.split = 110.f;
    blend.monoBass = true;
    blend.monoBelow = 120.f;
    blend.autoGain = true;
}

void Host::loadText(const std::string &t)
{
    // (loading a sound that has no TEMPO setting keeps the switch where it is; a saved project or sound that has one sets it)
    const int keepTempo = gb.tempoMul.load();
    resetAll();
    gb.tempoMul = keepTempo;
    // sessions from b10 (v=1) had GHOST LOCK off unless it was saved as on; a b10 file with no blend keys keeps the b10 meaning (off)
    if (t.rfind("v=1;", 0) == 0)
        blend.stack = blend.monoBass = blend.autoGain = false;
    std::istringstream in(t);
    std::string kv;
    while (std::getline(in, kv, ';'))
    {
        auto eq = kv.find('=');
        if (eq == std::string::npos)
            continue;
        auto k = kv.substr(0, eq);
        const float v = (float)std::atof(kv.c_str() + eq + 1);
        if (mixer.loadKey(k, kv.substr(eq + 1)))
            continue;
        if (k == "tmul")
        {
            gb.tempoMul = juce::jlimit(0, 2, (int)v);
            continue;
        }
        if (k == "surge.on") surge.on = v != 0.f;
        else if (k == "surge.level") surge.level = v;
        else if (k == "sub.on") sub.on = v != 0.f;
        else if (k == "sub.level") sub.level = v;
        else if (k == "sub.pan") sub.pan = v;
        else if (k == "blend.stack") blend.stack = v != 0.f;
        else if (k == "blend.split") blend.split = v;
        else if (k == "blend.monobass") blend.monoBass = v != 0.f;
        else if (k == "blend.monobelow") blend.monoBelow = v;
        else if (k == "blend.autogain") blend.autoGain = v != 0.f;
        else if (k == "surge.mono" || k == "surge.phase" || k == "surge.lowcut" || k == "surge.highcut" || k == "sub.mono" || k == "sub.phase" ||
                 k == "sub.lowcut" || k == "sub.highcut")
        {
            Mix *m = k[1] == 'u' && k[2] == 'r' ? &surge : &sub;
            const std::string w = k.substr(k.find('.') + 1);
            if (w == "mono") m->mono = v != 0.f;
            else if (w == "phase") m->phase = v != 0.f;
            else if (w == "lowcut") m->lowCut = v;
            else if (w == "highcut") m->highCut = v;
        }
        else if (k.rfind("sub.", 0) == 0)
        {
            if (auto *p = subEngine.param(k.substr(4))) p->setValue(v);
        }
        // (b10 sessions: "kick.*" keys - the 808 layer was removed in b11 - are ignored)
    }
    mixer.loadDone();
}
} // namespace ghost::layers

namespace ghost::layers
{
// One-tap stacks. Each is only a list of settings of the controls above (keys as in mixParam: "sub_on", "blend_split"...), applied after
// the blend controls were put back to neutral.
const std::vector<Host::StackPreset> &Host::stackPresets()
{
    static const std::vector<StackPreset> p = {
        {"SUB UNDER GROWL",
         {{"surge_on", 1.f}, {"sub_on", 1.f}, {"blend_stack", 1.f}, {"blend_split", 90.f}, {"blend_monobass", 1.f}, {"blend_monobelow", 120.f}}},
        {"DEEP AND WIDE",
         {{"surge_on", 1.f}, {"sub_on", 1.f}, {"blend_stack", 1.f}, {"blend_split", 140.f}, {"blend_monobass", 1.f}, {"blend_monobelow", 160.f},
          {"blend_autogain", 1.f}}}};
    return p;
}

std::string Host::clashHint(int &level) const
{
    level = 0;
    if (!surge.on.load() || !sub.on.load())
        return "";
    const float e0 = blend.meterDb[0].load(), e1 = blend.meterDb[1].load(), c = blend.meterCorr.load();
    const bool stack = blend.stack.load();
    // the two cancelling each other in the lows (opposite polarity)
    if (!stack && e0 > -50.f && e1 > -50.f && c < -0.3f)
    {
        level = 2;
        return "ENGINE and SUB cancel each other in the lows: flip PHASE on SUB, or turn on STACK MODE.";
    }
    if (!stack && e0 > -36.f && e1 > -36.f)
    {
        level = 1;
        return "ENGINE and SUB pile up in the lows: try STACK MODE (SUB below SPLIT, ENGINE above).";
    }
    return "";
}
} // namespace ghost::layers
