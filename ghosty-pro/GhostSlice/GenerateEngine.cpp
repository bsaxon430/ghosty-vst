#include "GenerateEngine.h"
#include "TerrainDsp.h"
#include <cmath>
#include <limits>

namespace gslice::gen
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
inline float hermite (const float* x, int n, double p) noexcept
{
    const int i = (int) p; const float f = (float) (p - i);
    auto at = [x, n] (int k) { return k < 0 || k >= n ? 0.0f : x[k]; };
    const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
    const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + y1;
}
// a looped read: the 4 points wrap from the loop end to the loop start (the crossfade is baked, so the wrap is seamless)
inline float hermiteLoop (const float* x, int n, double p, int ls, int le) noexcept
{
    const int i = (int) p; const float f = (float) (p - i);
    const int len = le - ls;
    auto at = [&] (int k) { if (k >= le && len > 0) k -= len; return k < 0 || k >= n ? 0.0f : x[k]; };
    const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
    const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + y1;
}
// CLEAR's read: the sinc (an integer position = the sample itself); looped reads wrap from the loop end to the loop start
inline float sincRead (const float* x, int n, double p, bool loop, int ls, int le) noexcept
{
    const int i = (int) p; const float f = (float) (p - i);
    const int len = le - ls;
    if (f == 0.0f) { int k = i; if (loop && k >= le && len > 0) k -= len; return k < 0 || k >= n ? 0.0f : x[k]; }
    return dsp::Sinc::get().read (i, f, [&] (int k) { if (loop && k >= le && len > 0) k -= len; return k < 0 || k >= n ? 0.0f : x[k]; });
}
inline void panGains (float pan, float& gl, float& gr) noexcept
{
    const double a = (juce::jlimit (-1.0f, 1.0f, pan) + 1.0) * kPi / 4.0;
    gl = (float) (std::cos (a) * std::sqrt (2.0)); gr = (float) (std::sin (a) * std::sqrt (2.0));
}
}

// ---------------------------------------------------------------- Engine
void Engine::prepare (double sampleRate, int)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    for (auto& v : voices) v = Voice();
    previewOn.store (false); active.store (0);
}

void Engine::publish (std::shared_ptr<const Take> t, bool hopPreview)
{
    auto tb = std::make_shared<Table>(); tb->gen = ++genCounter; tb->take = std::move (t);
    keep.push_back (tb);
    hopReq.store (hopPreview, std::memory_order_release);
    table.store (tb.get(), std::memory_order_release);
    collectGarbage();
}

void Engine::collectGarbage()
{
    const uint64 m = minGenInUse.load (std::memory_order_acquire);
    const Table* h = hazard.load (std::memory_order_acquire);
    for (size_t i = 0; i + 1 < keep.size();)
    {
        if (keep[i].get() != h && keep[i]->gen < m) keep.erase (keep.begin() + (long) i);
        else ++i;
    }
}

int Engine::zoneFor (const Take& t, int note)
{
    int best = -1, bd = 1 << 20;
    for (size_t i = 0; i < t.zones.size(); ++i) { const int d = std::abs (t.zones[i].rootNote - note); if (d < bd) { bd = d; best = (int) i; } }
    return best;
}

Engine::Voice* Engine::freeVoice() noexcept
{
    for (auto& v : voices) if (v.kind == None) return &v;
    Voice* best = &voices[0];
    for (auto& v : voices) if (v.env < best->env) best = &v;   // (never happens with the spare slots: the quietest)
    return best;
}

void Engine::release (Voice& v) noexcept
{
    if (v.kind == None || v.releasing) return;
    v.releasing = true;
}

void Engine::startVoice (int note, float vel, Kind k, const juce::AudioBuffer<float>* b, int ls, int le, double inc, double detune, float pan) noexcept
{
    if (b == nullptr || b->getNumSamples() < 4 || cur == nullptr || cur->take == nullptr) return;
    // the voice limit: the oldest sounding voice fades out (4 ms)
    int n = 0; Voice* oldest = nullptr;
    for (auto& v : voices) if (v.kind != None && v.kill == 0) { ++n; if (oldest == nullptr || v.age < oldest->age) oldest = &v; }
    if (n >= kMaxActive && oldest != nullptr) oldest->kill = 1.0f;
    Voice* v = freeVoice();
    const Take& t = *cur->take;
    *v = Voice();
    v->kind = k; v->note = note; v->take = &t; v->gen = cur->gen; v->buf = b; v->loopStart = ls; v->loopEnd = le;
    v->pos = 0; v->inc = inc; v->detune = detune; v->vel = 0.25f + 0.75f * juce::jlimit (0.0f, 1.0f, vel);
    panGains (pan, v->gl, v->gr);
    const double att = k == ZoneV ? t.attackS : 0.002;
    v->env = 0; v->attackInc = (float) (1.0 / juce::jmax (1.0, att * sr));
    const double rel = k == PreviewV ? 0.05 : t.releaseS;
    v->releaseMul = (float) std::exp (std::log (1e-4) / juce::jmax (8.0, rel * sr));
    v->held = true; v->age = ++clock; v->sinc = t.clear();
}

void Engine::noteOn (int note, float vel) noexcept
{
    if (cur == nullptr || cur->take == nullptr) return;
    const Take& t = *cur->take;
    const double rr = t.rate / sr;
    // a re-struck key: its old voice releases (no doubling of a held pad)
    for (auto& v : voices) if (v.kind != None && v.kind != PreviewV && v.note == note && ! v.releasing) release (v);
    if (t.type == Type::Chops)
    {
        if (note == Take::kPhraseNote) { startVoice (note, vel, ShotV, &t.preview, 0, 0, rr, 1.0, 0.0f); return; }
        const int i = note - Take::kChopNote;
        if (i >= 0 && i < (int) t.chops.size()) startVoice (note, vel, ShotV, &t.chops[(size_t) i], 0, 0, rr, 1.0, 0.0f);
        return;
    }
    const std::vector<Zone>* set = &t.zones;
    if (t.type == Type::Instrument && ! t.sylZones.empty())
    {   // the next syllable (in the order they were said), its zone nearest the note
        set = &t.sylZones[(size_t) (sylNext % (int) t.sylZones.size())];
        sylNext = (sylNext + 1) % (int) t.sylZones.size();
    }
    int z = -1, bd = 1 << 20;
    for (size_t i = 0; i < set->size(); ++i) { const int d = std::abs ((*set)[i].rootNote - note); if (d < bd) { bd = d; z = (int) i; } }
    if (z < 0) return;
    const auto& Z = (*set)[(size_t) z];
    const double inc = std::exp2 ((note - Z.rootNote) / 12.0) * rr;
    if (t.pairDetune && ! t.clear())
    {
        startVoice (note, vel * 0.74f, ZoneV, &Z.audio, Z.loopStart, Z.loopEnd, inc, std::exp2 (6.0 / 1200.0), -0.55f);
        startVoice (note, vel * 0.74f, ZoneV, &Z.audio, Z.loopStart, Z.loopEnd, inc, std::exp2 (-6.0 / 1200.0), 0.55f);
    }
    else startVoice (note, vel, ZoneV, &Z.audio, Z.loopStart, Z.loopEnd, inc, 1.0, 0.0f);
}

void Engine::noteOff (int note) noexcept
{
    for (auto& v : voices)
    {
        if (v.kind == None || v.kind == PreviewV || v.note != note || ! v.held) continue;
        v.held = false;
        if (v.kind == ShotV && note == Take::kPhraseNote) continue;   // the phrase plays to its end
        if (pedal) v.pedalHeld = true; else release (v);
    }
}

void Engine::process (float* L, float* R, int n, const juce::MidiBuffer& midi) noexcept
{
    // the table for this block (a hazard pointer: the message thread never frees the one we hold)
    Table* p = table.load (std::memory_order_acquire);
    while (true) { hazard.store (p, std::memory_order_release); Table* q = table.load (std::memory_order_acquire); if (q == p) break; p = q; }
    cur = p;
    if (hopReq.exchange (false) && cur != nullptr && cur->take != nullptr)
        for (auto& v : voices)   // (build 12) the preview hops to the new take's preview, same place, when it is the same length
            if (v.kind == PreviewV && v.kill == 0 && v.buf != nullptr && v.buf != &cur->take->preview && v.buf->getNumSamples() == cur->take->preview.getNumSamples())
            { v.buf = &cur->take->preview; v.take = cur->take.get(); v.gen = cur->gen; }
    if (panicReq.exchange (false)) { for (auto& v : voices) if (v.kind != None) v.kill = 1.0f; pedal = false; }
    if (const int pr = previewReq.exchange (0); pr != 0)
    {
        for (auto& v : voices) if (v.kind == PreviewV) v.kill = 1.0f;
        if (pr == 1 && cur != nullptr && cur->take != nullptr && cur->take->preview.getNumSamples() > 0)
        {
            startVoice (-2, 1.0f, PreviewV, &cur->take->preview, 0, 0, cur->take->rate / sr, 1.0, 0.0f);
            for (auto& v : voices) if (v.kind == PreviewV && v.kill == 0) v.vel = 1.0f;
            previewOn.store (true);
        }
        else previewOn.store (false);
    }
    bendSm += (bend - bendSm) * 0.5;
    const double bendRatio = std::exp2 (bendSm / 12.0);
    const float killStep = (float) (1.0 / (0.004 * sr));
    int at = 0;
    auto renderTo = [&] (int end)
    {
        for (auto& v : voices)
        {
            if (v.kind == None) continue;
            const auto* b = v.buf; const int bn = b->getNumSamples();
            const float* x0 = b->getReadPointer (0); const float* x1 = b->getReadPointer (juce::jmin (1, b->getNumChannels() - 1));
            const bool loop = v.kind == ZoneV && v.loopEnd > v.loopStart + 16;
            const double step = v.inc * v.detune * (v.kind == ZoneV ? bendRatio : 1.0);
            for (int i = at; i < end; ++i)
            {
                if (v.releasing) { v.env *= v.releaseMul; if (v.env < 1e-4f) { v.kind = None; break; } }
                else if (v.env < 1.0f) v.env = juce::jmin (1.0f, v.env + v.attackInc);
                float g = v.env * v.env * (3.0f - 2.0f * v.env) * v.vel;
                if (v.kill > 0) { v.kill -= killStep; if (v.kill <= 0) { v.kind = None; break; } g *= v.kill; }
                float l, r;
                if (v.sinc) { l = sincRead (x0, bn, v.pos, loop, v.loopStart, v.loopEnd); r = x1 == x0 ? l : sincRead (x1, bn, v.pos, loop, v.loopStart, v.loopEnd); }
                else if (loop) { l = hermiteLoop (x0, bn, v.pos, v.loopStart, v.loopEnd); r = hermiteLoop (x1, bn, v.pos, v.loopStart, v.loopEnd); }
                else { l = hermite (x0, bn, v.pos); r = hermite (x1, bn, v.pos); }
                L[i] += g * v.gl * l; R[i] += g * v.gr * r;
                v.pos += step;
                if (loop) { while (v.pos >= v.loopEnd) v.pos -= (double) (v.loopEnd - v.loopStart); }
                else if (v.pos >= bn) { v.kind = None; break; }
            }
        }
        at = end;
    };
    for (const auto meta : midi)
    {
        const int pos = juce::jlimit (0, n, meta.samplePosition);
        if (pos > at) renderTo (pos);
        const auto m = meta.getMessage();
        if (m.isNoteOn()) noteOn (m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff()) noteOff (m.getNoteNumber());
        else if (m.isSustainPedalOn()) pedal = true;
        else if (m.isSustainPedalOff()) { pedal = false; for (auto& v : voices) if (v.pedalHeld) { v.pedalHeld = false; release (v); } }
        else if (m.isPitchWheel()) bend = (m.getPitchWheelValue() - 8192) / 8192.0 * 2.0;
        else if (m.isAllNotesOff() || m.isAllSoundOff()) { for (auto& v : voices) if (v.kind != None && v.kind != PreviewV) release (v); }
    }
    if (at < n) renderTo (n);
    // what the message thread may free: tables older than every voice's
    uint64 m = std::numeric_limits<uint64>::max(); int a = 0; bool prev = false; float pp = 0;
    for (auto& v : voices)
    {
        if (v.kind == None) continue;
        ++a; m = std::min (m, v.gen);
        if (v.kind == PreviewV && v.kill == 0) { prev = true; pp = (float) (v.pos / juce::jmax (1, v.buf->getNumSamples())); }
    }
    minGenInUse.store (m, std::memory_order_release);
    active.store (a, std::memory_order_relaxed);
    if (previewOn.load (std::memory_order_relaxed) && ! prev) previewOn.store (false);
    previewPos.store (prev ? pp : 0.0f, std::memory_order_relaxed);
}

// ---------------------------------------------------------------- Fx
void Fx::prepare (double sampleRate, int)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    chLen = (int) (0.05 * sr) + 8;
    for (auto& b : chBuf) b.assign ((size_t) chLen, 0.0f);
    verb.prepare (sr);
    // rumble / DC high-pass, 30 Hz
    { const double w = 2.0 * kPi * 30.0 / sr, al = std::sin (w) / (2.0 * 0.7071), c = std::cos (w), a0 = 1.0 + al;
      hp.b0 = (1.0 + c) / 2.0 / a0; hp.b1 = -(1.0 + c) / a0; hp.b2 = hp.b0; hp.a1 = -2.0 * c / a0; hp.a2 = (1.0 - al) / a0; }
    toneFor = 99.0f; clearFor = clear.load(); setTone (tone.load());
    reset();
}

void Fx::reset()
{
    for (auto& b : chBuf) std::fill (b.begin(), b.end(), 0.0f);
    chPos = 0; lfo = 0;
    for (auto* f : { &hp, &low, &high, &top }) { f->z1[0] = f->z1[1] = f->z2[0] = f->z2[1] = 0; }
    verb.reset(); widthSm = ensSm = warmSm = -1;
}

void Fx::setTone (float t)
{
    toneFor = t;
    auto shelf = [this] (BQ& f, double hz, double db, bool highShelf)
    {   // RBJ shelves (S = 0.8)
        const double A = std::pow (10.0, db / 40.0), w = 2.0 * kPi * hz / sr, c = std::cos (w), s = std::sin (w);
        const double al = s / 2.0 * std::sqrt ((A + 1.0 / A) * (1.0 / 0.8 - 1.0) + 2.0), sq = 2.0 * std::sqrt (A) * al;
        double b0, b1, b2, a0, a1, a2;
        if (highShelf) { b0 = A * ((A + 1) + (A - 1) * c + sq); b1 = -2 * A * ((A - 1) + (A + 1) * c); b2 = A * ((A + 1) + (A - 1) * c - sq); a0 = (A + 1) - (A - 1) * c + sq; a1 = 2 * ((A - 1) - (A + 1) * c); a2 = (A + 1) - (A - 1) * c - sq; }
        else { b0 = A * ((A + 1) - (A - 1) * c + sq); b1 = 2 * A * ((A - 1) - (A + 1) * c); b2 = A * ((A + 1) - (A - 1) * c - sq); a0 = (A + 1) + (A - 1) * c + sq; a1 = -2 * ((A - 1) + (A + 1) * c); a2 = (A + 1) + (A - 1) * c - sq; }
        f.b0 = b0 / a0; f.b1 = b1 / a0; f.b2 = b2 / a0; f.a1 = a1 / a0; f.a2 = a2 / a0;
    };
    shelf (low, 260.0, -4.5 * t, false);
    shelf (high, 3200.0, 6.0 * t, true);
    // the top: always a gentle roll-off above 17 kHz (no fizz); darker turns it down to about 4 kHz. CLEAR: flat unless TONE is dark
    if (clearFor && t >= 0.0f) { top.b0 = 1; top.b1 = top.b2 = top.a1 = top.a2 = 0; return; }
    const double fc = juce::jlimit (3000.0, 0.45 * sr, t < 0 ? 17000.0 * std::exp2 (2.1 * t) : 17000.0);
    const double w = 2.0 * kPi * fc / sr, al = std::sin (w) / (2.0 * 0.6), c = std::cos (w), a0 = 1.0 + al;
    top.b0 = (1.0 - c) / 2.0 / a0; top.b1 = (1.0 - c) / a0; top.b2 = top.b0; top.a1 = -2.0 * c / a0; top.a2 = (1.0 - al) / a0;
}

void Fx::process (float* L, float* R, int n) noexcept
{
    const float t = juce::jlimit (-1.0f, 1.0f, tone.load (std::memory_order_relaxed) + modTone.load (std::memory_order_relaxed));
    const bool cl = clear.load (std::memory_order_relaxed);
    if (cl != clearFor) { clearFor = cl; top.z1[0] = top.z1[1] = top.z2[0] = top.z2[1] = 0; setTone (toneFor); }
    if (std::abs (t - toneFor) > 0.004f) setTone (toneFor + juce::jlimit (-0.05f, 0.05f, t - toneFor));   // (moves in small steps: no zipper)
    const float wT = cl ? 1.0f : juce::jlimit (0.0f, 1.0f, width.load (std::memory_order_relaxed) + modWidth.load (std::memory_order_relaxed)) * 2.0f;
    const float eT = cl ? 0.0f : juce::jlimit (0.0f, 1.0f, ensemble.load (std::memory_order_relaxed) + modEnsemble.load (std::memory_order_relaxed));
    const float aT = cl ? 0.0f : juce::jlimit (0.0f, 1.0f, warmth.load (std::memory_order_relaxed) + modWarmth.load (std::memory_order_relaxed)) * 0.55f;
    if (widthSm < 0) { widthSm = wT; ensSm = eT; warmSm = aT; }
    const float sm = 1.0f - std::exp (-1.0f / (0.03f * (float) sr));
    const double lfoInc = 2.0 * kPi * 0.23 / sr;
    const float baseA = (float) (0.0095 * sr), baseB = (float) (0.0135 * sr), depth = (float) (0.0018 * sr);
    for (int i = 0; i < n; ++i)
    {
        widthSm += (wT - widthSm) * sm; ensSm += (eT - ensSm) * sm; warmSm += (aT - warmSm) * sm;
        float l = hp.run (0, L[i]), r = hp.run (1, R[i]);
        // warmth: a soft curve on the peaks (small signals pass at unity gain)
        l = (1.0f - warmSm) * l + warmSm * std::tanh (1.6f * l) / 1.6f;
        r = (1.0f - warmSm) * r + warmSm * std::tanh (1.6f * r) / 1.6f;
        l = top.run (0, high.run (0, low.run (0, l))); r = top.run (1, high.run (1, low.run (1, r)));
        // ensemble: two modulated taps per side (quadrature LFOs: the sides move apart)
        chBuf[0][(size_t) chPos] = l; chBuf[1][(size_t) chPos] = r;
        if (ensSm > 0.001f)
        {
            auto tap = [&] (int ch, float d) { float p = (float) chPos - d; while (p < 0) p += (float) chLen; int i0 = (int) p; const float f = p - (float) i0; i0 %= chLen; const int i1 = (i0 + 1) % chLen; return chBuf[(size_t) ch][(size_t) i0] * (1.0f - f) + chBuf[(size_t) ch][(size_t) i1] * f; };
            const float s0 = (float) std::sin (lfo), c0 = (float) std::cos (lfo), s1 = (float) std::sin (lfo * 1.37 + 1.0);
            const float wl = 0.5f * (tap (0, baseA + depth * s0) + tap (0, baseB + depth * s1)), wr = 0.5f * (tap (1, baseA + depth * c0) + tap (1, baseB - depth * s1));
            const float mix = 0.36f * ensSm;
            l = l * (1.0f - 0.35f * mix) + mix * wl; r = r * (1.0f - 0.35f * mix) + mix * wr;
        }
        chPos = (chPos + 1) % chLen; lfo += lfoInc; if (lfo > 2.0 * kPi * 100.0) lfo -= 2.0 * kPi * 100.0;
        // width (mid / side)
        const float m = 0.5f * (l + r), s = 0.5f * (l - r) * widthSm;
        L[i] = m + s; R[i] = m - s;
    }
    const float sp = juce::jlimit (0.0f, 1.0f, space.load (std::memory_order_relaxed) + modSpace.load (std::memory_order_relaxed));
    const float dr = juce::jlimit (0.0f, 1.0f, dream.load (std::memory_order_relaxed) + modDream.load (std::memory_order_relaxed));
    vp.mix = 0.52f * sp + 0.16f * dr * (0.4f + sp); vp.decay = 1.6f + 5.0f * sp + 4.0f * dr; vp.size = 0.45f + 0.45f * sp + 0.1f * dr; vp.predelayMs = 22.0f + 18.0f * dr; vp.loCut = 180.0f;
    vp.hiCut = 6500.0f + 3500.0f * (t + 1.0f) * 0.5f; vp.damp = 0.45f; vp.lowDamp = 0.35f; vp.mod = 0.55f; vp.width = 1.15f; vp.diffusion = 0.75f;
    if (sp > 0.0005f || dr > 0.0005f) verb.process (L, R, n, vp);
}

juce::AudioBuffer<float> renderForExport (const Take& t, float tone, float space, float width, float warmth, const FxMod& mod)
{
    juce::ScopedNoDenormals nd;
    Fx fx; fx.tone.store (tone); fx.space.store (space); fx.width.store (width); fx.warmth.store (warmth); fx.ensemble.store (t.ensemble); fx.clear.store (t.clear()); fx.dream.store (t.dream);
    fx.modTone.store (mod.tone); fx.modSpace.store (mod.space); fx.modWidth.store (mod.width); fx.modWarmth.store (mod.warmth); fx.modEnsemble.store (mod.ensemble); fx.modDream.store (mod.dream);
    fx.prepare (t.rate, 512);
    const float dreamAll = t.dream + mod.dream, spaceAll = space + mod.space;
    const int n0 = t.preview.getNumSamples(), tail = (int) (Fx::kTailSeconds * t.rate * juce::jlimit (0.15f, dreamAll > 0.0f ? 2.0f : 1.0f, spaceAll + 0.1f + 1.2f * dreamAll));
    juce::AudioBuffer<float> b (2, n0 + tail); b.clear();
    for (int ch = 0; ch < 2; ++ch) b.copyFrom (ch, 0, t.preview, juce::jmin (ch, t.preview.getNumChannels() - 1), 0, n0);
    // build 13: the land's live chain (STUTTER, SHIMMER, GRIT, FREEZE ...), as it plays, then the FX
    ghost::terrain::TerrainLive live;
    if (mod.live != nullptr && ! static_cast<const ghost::terrain::LiveParams*> (mod.live)->isZero())
    {
        live.prepare (t.rate, 2); live.setSeed (7); live.setParams (*static_cast<const ghost::terrain::LiveParams*> (mod.live));
    }
    for (int i = 0; i < b.getNumSamples(); i += 512)
    {
        const int m = juce::jmin (512, b.getNumSamples() - i);
        if (mod.live != nullptr) { float* chs[2] = { b.getWritePointer (0) + i, b.getWritePointer (1) + i }; live.process (chs, 2, m); }
        fx.process (b.getWritePointer (0) + i, b.getWritePointer (1) + i, m);
    }
    // trim the silent end, fade the last 50 ms
    int end = b.getNumSamples();
    while (end > n0 && std::abs (b.getSample (0, end - 1)) < 1e-4f && std::abs (b.getSample (1, end - 1)) < 1e-4f) --end;
    b.setSize (2, juce::jmax (n0, end), true, true, true);
    dsp::fadeEdges (b, 0, (int) (0.05 * t.rate));
    double L = dsp::finish (b, t.rate, -14.0, -1.2, ! t.clear());
    // (very peaky takes, e.g. spoken words under a big SPACE: the ceiling holds the peaks, so the loudness can land short. A second /
    //  third pass of the same finish brings it to the target; a take that made it the first time is untouched)
    for (int pass = 0; pass < 2 && ! t.clear() && L < -14.3; ++pass) L = dsp::finish (b, t.rate, -14.0, -1.2, true);
    return b;
}
} // namespace gslice::gen
