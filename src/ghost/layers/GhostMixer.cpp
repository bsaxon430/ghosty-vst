/*
 * Ghost Bass b11 - BLEND BOARD MIXER (see GhostMixer.h). Copyright 2026 the Ghost Bass authors. GPL-3.
 */
#include "GhostMixer.h"
#include "GhostEngine.h"
#include "GhostMorph.h"
#include "SurgeSynthesizer.h"
#include "SurgeSynthProcessor.h"
#include "SurgeStorage.h"
#include <cstdio>
#include <cstdlib>
#include <ostream>
#include <algorithm>
#include <cmath>

namespace ghost::layers
{
Mixer::Slot::Slot() = default;
Mixer::Slot::~Slot() = default;
Mixer::Mixer() = default;
Mixer::~Mixer() { cancelPendingUpdate(); }

std::string Mixer::slotPath(int k) const { return filled(k) ? slot[(size_t)k].path : std::string(); }
std::string Mixer::slotName(int k) const { return filled(k) ? slot[(size_t)k].name : std::string(); }
int Mixer::filledCount() const
{
    int n = 0;
    for (int k = 0; k < kSlots; ++k)
        n += filled(k) ? 1 : 0;
    return n;
}

void Mixer::Clean::prepare(double sr)
{
    static const float f[kCross] = {120.f, 300.f, 800.f, 2000.f, 5000.f};
    juce::dsp::ProcessSpec spec{sr, (juce::uint32)kBlock, 2};
    for (int i = 0; i < kCross; ++i)
    {
        x[i].prepare(spec);
        x[i].setCutoffFrequency(std::min(f[i], (float)(0.45 * sr)));
    }
    for (int b = 0; b < kCross; ++b)
        for (int j = b + 1; j < kCross; ++j)
        {
            ap[b][j].prepare(spec);
            ap[b][j].setType(juce::dsp::LinkwitzRileyFilterType::allpass);
            ap[b][j].setCutoffFrequency(std::min(f[j], (float)(0.45 * sr)));
        }
    hp.prepare(spec);
    hp.setType(juce::dsp::LinkwitzRileyFilterType::highpass);
    juce::dsp::ProcessSpec mono{sr, (juce::uint32)kBlock, 1};
    for (auto &e : env)
    {
        e.prepare(mono);
        e.setLevelCalculationType(juce::dsp::BallisticsFilterLevelCalculationType::peak);
        e.setAttackTime(6.f);
        e.setReleaseTime(120.f);
    }
    reset();
}
void Mixer::Clean::reset()
{
    for (auto &f : x)
        f.reset();
    hp.reset();
    for (auto &r : ap)
        for (auto &a : r)
            a.reset();
    for (auto &e : env)
        e.reset();
    for (int b = 0; b < kBands; ++b)
        gain[b] = 1.f, level[b] = 0.f;
    own = 0.f;
    fresh = true;
}

void Mixer::prepare(double sampleRate)
{
    sr = sampleRate;
    guard.prepare(sr);
    for (auto &c : clean_)
        c.prepare(sr);
    cleanPrepared = true;
    for (auto &s : slot)
        if (s.proc)
        {
            std::lock_guard<std::mutex> g(s.mtx);
            s.proc->setRateAndBufferSizeDetails(sr, BLOCK_SIZE);
            s.proc->prepareToPlay(sr, BLOCK_SIZE);
        }
}

bool Mixer::setSlot(int k, const std::string &path, const std::string &name)
{
    if (k < 0 || k >= kSlots)
        return false;
    auto &s = slot[(size_t)k];
    std::lock_guard<std::mutex> g(s.mtx); // the audio thread skips this slot while it loads
    s.filled.store(false, std::memory_order_release);
    if (!s.proc)
    {
        // a whole plugin processor, built exactly like the main one (same construction, same MIDI path, same block loop), so that a sound plays
        // here exactly as it plays alone. Only the library scan is skipped: the main engine's wavetable and IR lists are copied in.
        SurgeStorage::skipScanThisThread = true;
        s.proc = std::make_unique<SurgeSynthProcessor>();
        SurgeStorage::skipScanThisThread = false;
        if (!s.proc->surge)
        {
            s.proc.reset();
            return false;
        }
        if (mainSurge)
        {
            auto &d = s.proc->surge->storage;
            auto &m = mainSurge->storage;
            d.wt_list = m.wt_list;
            d.wt_category = m.wt_category;
            d.firstThirdPartyWTCategory = m.firstThirdPartyWTCategory;
            d.firstUserWTCategory = m.firstUserWTCategory;
            d.wtOrdering = m.wtOrdering;
            d.wtCategoryOrdering = m.wtCategoryOrdering;
            d.ir_list = m.ir_list;
            d.ir_category = m.ir_category;
            d.firstThirdPartyIRCategory = m.firstThirdPartyIRCategory;
            d.firstUserIRCategory = m.firstUserIRCategory;
            d.irOrdering = m.irOrdering;
            d.irCategoryOrdering = m.irCategoryOrdering;
        }
        s.proc->setRateAndBufferSizeDetails(sr, BLOCK_SIZE);
        s.proc->prepareToPlay(sr, BLOCK_SIZE);
        s.audio.setSize(6, BLOCK_SIZE);
        s.midi.ensureSize(2048);
    }
    // A sound file made before streaming version 17 does not carry the global volume (and no sound file carries the FX bypass): the engine
    // keeps whatever it had. A board sound must keep what the MAIN engine has, so it plays exactly as it would if loaded into the main engine.
    if (mainSurge)
    {
        auto &dp = s.proc->surge->storage.getPatch();
        auto &mp = mainSurge->storage.getPatch();
        dp.volume.val.f = mp.volume.val.f;
        dp.fx_bypass.val.i = mp.fx_bypass.val.i;
    }
    if (!s.proc->surge->loadPatchByPath(path.c_str(), -1, name.c_str(), true))
        return false;
    s.path = path;
    s.name = name;
    s.awake = false;
    s.g[0] = s.g[1] = 0.f;
    s.filled.store(true, std::memory_order_release);
    return true;
}

void Mixer::clearSlot(int k)
{
    if (k < 0 || k >= kSlots)
        return;
    auto &s = slot[(size_t)k];
    std::lock_guard<std::mutex> g(s.mtx);
    s.filled.store(false, std::memory_order_release);
    s.path.clear();
    s.name.clear();
    s.awake = false;
    s.g[0] = s.g[1] = 0.f;
    shown[k].store(0.f);
    s.proc.reset(); // frees the memory of an engine that is not used
}

void Mixer::pushSub(const float *x, int n, float gain)
{
    float t[BLOCK_SIZE];
    n = std::min(n, (int)BLOCK_SIZE);
    for (int i = 0; i < n; ++i)
        t[i] = x[i] * gain;
    ringPush(kSlots, t, n);
}
void Mixer::snapshot(int src, float *out) const
{
    const auto &r = ring[(size_t)src];
    const uint32_t p = r.pos.load(std::memory_order_acquire);
    for (int i = 0; i < kRing; ++i)
        out[i] = r.buf[(p + (uint32_t)i) & (kRing - 1)];
}
const float *Mixer::lastBlock(int k, int ch) const { return slot[(size_t)k].last[ch & 1].data(); }

void Mixer::wake(Slot &s)
{
    // the notes that are held now start in this slot too (same velocity), with the wheel and pedal where they are
    s.midi.clear();
    s.midi.addEvent(juce::MidiMessage::pitchWheel(1, bend + 8192), 0);
    if (sustain)
        s.midi.addEvent(juce::MidiMessage::controllerEvent(1, 64, sustain), 0);
    for (auto &kv : held)
        s.midi.addEvent(juce::MidiMessage::noteOn(1, kv.first, (juce::uint8)kv.second), 0);
    s.awake = true;
}
void Mixer::sleepSlot(Slot &s)
{
    s.proc->surge->allNotesOff();
    s.awake = false;
}
SurgeSynthesizer *Mixer::engineForTest(int k) { return slot[(size_t)k].proc ? slot[(size_t)k].proc->surge.get() : nullptr; }

void Mixer::cleanMix(float *accL, float *accR, const bool *act, int n)
{
    const bool hpOn = subHighPass.load(std::memory_order_relaxed);
    const float split = std::clamp(splitHz.load(std::memory_order_relaxed), 40.f, 400.f);
    float bandL[kSlots][kBands][kBlock], bandR[kSlots][kBands][kBlock];
    // 1. every sound split into six bands (each one is also the SUB's high-pass first, when the SUB owns the lows)
    for (int k = 0; k < kSlots; ++k)
    {
        if (!act[k])
            continue;
        auto &c = clean_[(size_t)k];
        if (hpOn)
            c.hp.setCutoffFrequency(split);
        for (int ch = 0; ch < 2; ++ch)
        {
            const float *in = ch == 0 ? vL_[k] : vR_[k];
            float(*bd)[kBlock] = ch == 0 ? bandL[k] : bandR[k];
            for (int i = 0; i < n; ++i)
            {
                float rest = in[i], lo, hi;
                if (hpOn)
                {
                    float junkLo;
                    c.hp.processSample(ch, rest, junkLo, hi);
                    rest = hi;
                }
                for (int b = 0; b < kCross; ++b)
                {
                    c.x[b].processSample(ch, rest, lo, hi);
                    for (int j = b + 1; j < kCross; ++j)
                        lo = c.ap[b][j].processSample(ch, lo);
                    bd[b][i] = lo;
                    rest = hi;
                }
                bd[kCross][i] = rest;
            }
        }
    }
    // 2. each band's level per sound (peak with attack/release), then who is the loudest in that band
    float lvl[kSlots][kBands] = {};
    for (int k = 0; k < kSlots; ++k)
    {
        if (!act[k])
            continue;
        auto &c = clean_[(size_t)k];
        for (int b = 0; b < kBands; ++b)
        {
            float e = 0.f;
            for (int i = 0; i < n; ++i)
                e = c.env[b].processSample(0, std::max(std::fabs(bandL[k][b][i]), std::fabs(bandR[k][b][i])));
            lvl[k][b] = e;
        }
    }
    // build 15: the lows of basses playing the same notes are not independent, so the equal-power law (sqrt of the puck's weight) that is
    // right for the rest adds them up wrong: in step they sum up to +6 dB (clipping), out of step they cancel (the bass goes quiet), and a
    // sequencer re-rolls that with every note. So below 120 Hz ONE sound owns the lows: the sound the puck is nearest to (a fast glide when
    // that changes), and 120-300 Hz uses the plain weight (a sum that can never be louder than the loudest sound alone). A puck fully on a
    // corner is still exactly that sound (owner, weight 1).
    float wk[kSlots] = {};
    int owner = -1;
    for (int k = 0; k < kSlots; ++k)
        if (act[k])
        {
            wk[k] = std::clamp(weight[k].load(std::memory_order_relaxed), 0.f, 1.f);
            if (owner < 0 || wk[k] > wk[owner] + 1e-4f)
                owner = k;
        }
    // a little hysteresis, so a puck resting between two corners does not flip the owner back and forth
    if (lowOwner_ >= 0 && lowOwner_ < kSlots && act[lowOwner_] && owner >= 0 && wk[lowOwner_] + 0.03f >= wk[owner])
        owner = lowOwner_;
    lowOwner_ = owner;
    float lowF[kSlots][2] = {}; // per sound: [0] factor for band 0 (start of block), [1] end of block; band 1 uses lin[k]
    float lin[kSlots] = {};
    for (int k = 0; k < kSlots; ++k)
    {
        if (!act[k])
            continue;
        auto &c = clean_[(size_t)k];
        const float undo = wk[k] > 1e-4f ? 1.f / std::sqrt(wk[k]) : 0.f; // the signal already carries sqrt(w)
        const float tgt = (k == owner) ? 1.f : 0.f;
        if (c.fresh)
            c.own = tgt, c.fresh = false;
        const float o1 = c.own + (tgt - c.own) * 0.06f; // about 15 ms: a crossfade, never a click
        lowF[k][0] = c.own * undo;
        lowF[k][1] = o1 * undo;
        c.own = o1;
        lin[k] = std::sqrt(wk[k]); // sqrt(w) * sqrt(w) = w
    }
    // 3. the quieter sound of a fighting pair is ducked, gently: nothing within 3 dB of the loudest, up to -6 dB when it is 15 dB under
    const float inv = 1.f / (float)n;
    for (int b = 0; b < kBands; ++b)
    {
        float top = 0.f;
        for (int k = 0; k < kSlots; ++k)
            if (act[k])
                top = std::max(top, lvl[k][b]);
        for (int k = 0; k < kSlots; ++k)
        {
            if (!act[k])
                continue;
            float target = 1.f;
            if (b > 0 && top > 1e-4f && lvl[k][b] < top) // (band 0 has one owner: nothing there to fight)
            {
                const float dDb = 20.f * std::log10(std::max(lvl[k][b], 1e-6f) / top); // <= 0
                if (dDb < -3.f)
                {
                    const float duck = std::clamp((dDb + 3.f) * 0.5f, -6.f, 0.f);
                    // only a sound that is really there is ducked (not one that is 50 dB down anyway)
                    if (lvl[k][b] > 1e-4f)
                        target = std::pow(10.f, duck / 20.f);
                }
            }
            auto &c = clean_[(size_t)k];
            // glide (about 20 ms: the gain moves a fifth of the way each block)
            const float g1 = c.gain[b] + (target - c.gain[b]) * 0.2f;
            // the low bands: band 0 belongs to the owner, band 1 is summed by plain weight (see above)
            float f0 = 1.f, f1 = 1.f;
            if (b == 0)
                f0 = lowF[k][0], f1 = lowF[k][1];
            else if (b == 1)
                f0 = f1 = lin[k];
            const float step = (g1 * f1 - c.gain[b] * f0) * inv;
            float g = c.gain[b] * f0;
            for (int i = 0; i < n; ++i)
            {
                g += step;
                accL[i] += g * bandL[k][b][i];
                accR[i] += g * bandR[k][b][i];
            }
            c.gain[b] = g1;
        }
    }
}

bool Mixer::block(float *L, float *R, int n, ghost::GhostEngine &gb)
{
    if (!on.load(std::memory_order_relaxed) || n != BLOCK_SIZE)
        return false;
    auto *main = gb.mainSynth();
    // the notes of this block, as the main engine got them (same order, at the block start, like the engine itself)
    const int ne = gb.layerEvN;
    for (int i = 0; i < ne; ++i)
    {
        const auto &e = gb.layerEv[(size_t)i];
        switch (e.type)
        {
        case 0: held[e.a] = e.b; break;
        case 1: held.erase(e.a); break;
        case 2: bend = e.c; break;
        case 3:
            if (e.a == 64)
                sustain = e.b;
            break;
        case 4: held.clear(); break;
        }
    }
    float accL[BLOCK_SIZE] = {}, accR[BLOCK_SIZE] = {};
    bool active[kSlots] = {false, false, false, false};
    int nActive = 0;
    for (int k = 0; k < kSlots; ++k)
    {
        auto &s = slot[(size_t)k];
        if (!s.filled.load(std::memory_order_acquire))
        {
            shown[k].store(0.f, std::memory_order_relaxed);
            continue;
        }
        const float w = std::clamp(weight[k].load(std::memory_order_relaxed), 0.f, 1.f);
        const float t = w > 1e-5f ? std::sqrt(w) * vol[k].load(std::memory_order_relaxed) : 0.f;
        const float pn = pan[k].load(std::memory_order_relaxed);
        const float tg[2] = {t * panGain(pn, 0), t * panGain(pn, 1)};
        const bool silent = tg[0] == 0.f && tg[1] == 0.f && s.g[0] == 0.f && s.g[1] == 0.f;
        std::unique_lock<std::mutex> lk(s.mtx, std::try_to_lock);
        if (!lk.owns_lock() || !s.proc)
            continue;
        if (silent)
        {
            if (s.awake)
                sleepSlot(s);
            shown[k].store(0.f, std::memory_order_relaxed);
            {
                const float z[BLOCK_SIZE] = {};
                ringPush(k, z, n);
            }
            continue;
        }
        if (!s.awake)
            wake(s); // (fills s.midi with the held notes, which already include this block's events)
        else
        {
            s.midi.clear();
            for (int i = 0; i < ne; ++i)
            {
                const auto &e = gb.layerEv[(size_t)i];
                switch (e.type)
                {
                case 0: s.midi.addEvent(juce::MidiMessage::noteOn(1, e.a, (juce::uint8)e.b), 0); break;
                case 1: s.midi.addEvent(juce::MidiMessage::noteOff(1, e.a), 0); break;
                case 2: s.midi.addEvent(juce::MidiMessage::pitchWheel(1, e.c + 8192), 0); break;
                case 3: s.midi.addEvent(juce::MidiMessage::controllerEvent(1, e.a, e.b), 0); break;
                case 4: s.midi.addEvent(juce::MidiMessage::allNotesOff(1), 0); break;
                }
            }
        }
        if (main)
            s.proc->standaloneTempo.store(main->time_data.tempo);
        s.audio.clear();
        s.proc->processBlock(s.audio, s.midi);
        const float inv = 1.f / (float)n;
        float mono[BLOCK_SIZE];
        for (int ch = 0; ch < 2; ++ch)
        {
            const float *o = s.audio.getReadPointer(ch);
            float *v = ch == 0 ? vL_[k] : vR_[k];
            const float d = (tg[ch] - s.g[ch]) * inv;
            float a = s.g[ch];
            for (int i = 0; i < n; ++i)
            {
                a += d;
                v[i] = a * o[i];
                mono[i] = ch == 0 ? 0.5f * v[i] : mono[i] + 0.5f * v[i];
                s.last[ch][(size_t)i] = o[i];
            }
            s.g[ch] = tg[ch];
        }
        ringPush(k, mono, n);
        active[k] = true;
        ++nActive;
        shown[k].store(std::max(tg[0], tg[1]), std::memory_order_relaxed);
    }
    // BLEND GUARD, part 1: sounds that cancel each other get one of them flipped (in place, before the mix)
    float *gsL[ghostblock::BlendGuard::kSrc]{}, *gsR[ghostblock::BlendGuard::kSrc]{};
    for (int k = 0; k < kSlots; ++k)
        if (active[k])
        {
            gsL[k] = vL_[k];
            gsR[k] = vR_[k];
        }
    guard.phase(gsL, gsR, n);
    if (clean.load(std::memory_order_relaxed) && nActive >= 2 && cleanPrepared)
        cleanMix(accL, accR, active, n);
    else
    {
        // plain sum (one sound alone is exactly what its engine renders: 0 + 1 * x)
        for (int k = 0; k < kSlots; ++k)
            if (active[k])
                for (int i = 0; i < n; ++i)
                {
                    accL[i] += vL_[k][i];
                    accR[i] += vR_[k][i];
                }
        // a sound that just dropped out of the clean path restarts its filters next time
        for (auto &c : clean_)
            c.reset();
        lowOwner_ = -1;
    }
    // BLEND GUARD, part 2: each band of the mix held at the level the sounds should add up to (nothing changes with one sound)
    guard.body(gsL, gsR, accL, accR, n);
    std::copy(accL, accL + n, L);
    std::copy(accR, accR + n, R);
    return true;
}

bool Mixer::isDefault() const
{
    if (filledCount() > 0 || on.load() || puckX.load() != 0.5f || puckY.load() != 0.5f || !clean.load())
        return false;
    for (int k = 0; k < kSlots; ++k)
        if (vol[k].load() != 1.f || pan[k].load() != 0.f)
            return false;
    return true;
}
void Mixer::resetAll()
{
    on = false;
    for (int k = 0; k < kSlots; ++k)
    {
        clearSlot(k);
        weight[k] = 0.f;
        vol[k] = 1.f;
        pan[k] = 0.f;
        pend[k][0].clear();
        pend[k][1].clear();
    }
    puckX = puckY = 0.5f;
    clean = true;
}
static std::string esc(const std::string &s)
{
    std::string r;
    for (unsigned char c : s)
    {
        if (c == ';' || c == '=' || c == '%' || c < 32)
        {
            char b[4];
            std::snprintf(b, sizeof(b), "%%%02X", c);
            r += b;
        }
        else
            r += (char)c;
    }
    return r;
}
static std::string unesc(const std::string &s)
{
    std::string r;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '%' && i + 2 < s.size() + 0 && std::isxdigit((unsigned char)s[i + 1]) && std::isxdigit((unsigned char)s[i + 2]))
        {
            r += (char)std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
            i += 2;
        }
        else
            r += s[i];
    }
    return r;
}
void Mixer::save(std::ostream &o) const
{
    if (isDefault())
        return;
    for (int k = 0; k < kSlots; ++k)
    {
        if (filled(k))
            o << "b" << k << ".path=" << esc(slotPath(k)) << ";b" << k << ".name=" << esc(slotName(k)) << ";";
        if (vol[k].load() != 1.f)
            o << "b" << k << ".vol=" << vol[k].load() << ";";
        if (pan[k].load() != 0.f)
            o << "b" << k << ".pan=" << pan[k].load() << ";";
    }
    o << "bpx=" << puckX.load() << ";bpy=" << puckY.load() << ";";
    if (!clean.load())
        o << "bclean=0;";
}
bool Mixer::loadKey(const std::string &key, const std::string &raw)
{
    if (key == "bpx") { puckX = (float)std::atof(raw.c_str()); return true; }
    if (key == "bclean") { clean = std::atof(raw.c_str()) != 0.0; return true; }
    if (key == "bpy") { puckY = (float)std::atof(raw.c_str()); return true; }
    if (key.size() > 3 && key[0] == 'b' && key[1] >= '0' && key[1] < '0' + kSlots && key[2] == '.')
    {
        const int k = key[1] - '0';
        const std::string w = key.substr(3);
        if (w == "path") pend[k][0] = unesc(raw);
        else if (w == "name") pend[k][1] = unesc(raw);
        else if (w == "vol") vol[k] = (float)std::atof(raw.c_str());
        else if (w == "pan") pan[k] = (float)std::atof(raw.c_str());
        else return false;
        return true;
    }
    return false;
}
void Mixer::loadDone()
{
    // the sounds are loaded on the message thread (a project's state can arrive on another thread)
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        loadPending();
    else
        triggerAsyncUpdate();
}
void Mixer::handleAsyncUpdate() { loadPending(); }
void Mixer::loadPending()
{
    for (int k = 0; k < kSlots; ++k)
        if (!pend[k][0].empty())
        {
            setSlot(k, pend[k][0], pend[k][1]);
            pend[k][0].clear();
            pend[k][1].clear();
        }
    bool f[kSlots];
    for (int k = 0; k < kSlots; ++k)
        f[k] = filled(k);
    float w[kSlots];
    ghost::Morph::boardWeights(f, puckX.load(), puckY.load(), w);
    for (int k = 0; k < kSlots; ++k)
        weight[k] = w[k];
    on = filledCount() >= 2;
}
} // namespace ghost::layers
