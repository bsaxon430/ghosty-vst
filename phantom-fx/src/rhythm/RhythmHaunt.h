// SPDX-License-Identifier: AGPL-3.0-or-later
// 3 HAUNT EDIT: 16 pads, each a tempo-synced gesture (stutter, build, gate, glitch, reverse, tape stop, LP / HP sweep, crush, pitch drop)
// read from TIME-12's buffer (time12::Delay, cubic) through a crossfading read head. Played by the mouse, by the PAD parameter or by MIDI
// notes 48-63 (pads 1-16) at the exact sample of the note (latency-free). With SYNC START the gesture's slices line up with the host grid
// (the slice that is playing when the pad goes down started on the last grid line). Pads are saved with the project.
#pragma once
#include "RhythmCore.h"
#include "../effects/ColourSst.h"

namespace phantom
{
namespace hauntdata
{
constexpr int kPads = 16, kNoteBase = 48;
enum Type { htStutter, htBuild, htGate, htGlitch, htReverse, htTapeStop, htLpSweep, htHpSweep, htCrush, htPitchDrop, kNumTypes };
inline const juce::StringArray& typeNames() { static juce::StringArray s { "STUTTER", "BUILD", "GATE", "GLITCH", "REVERSE", "TAPE STOP", "LP SWEEP", "HP SWEEP", "CRUSH", "PITCH DROP" }; return s; }
inline const juce::StringArray& sizeNames() { static juce::StringArray s { "1/64", "1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4T", "1/4", "1/2", "1 BAR", "2 BARS" }; return s; }
inline double sizeBeats (int i, double beatsPerBar) noexcept
{ static const double b[] { 1.0 / 16, 1.0 / 8, 1.0 / 6, 1.0 / 4, 1.0 / 3, 1.0 / 2, 2.0 / 3, 1.0, 2.0, -1.0, -2.0 }; const double v = b[juce::jlimit (0, 10, i)]; return v < 0 ? -v * beatsPerBar : v; }
inline juce::uint32 typeColour (int t)
{ static const juce::uint32 c[] { 0xffFF4FD8, 0xffFFC35A, 0xff3CFF8F, 0xff45E6FF, 0xffA06BFF, 0xffFF5A6E, 0xff7FD8FF, 0xffC6FF4F, 0xffFF9E5A, 0xff8FA8FF }; return c[juce::jlimit (0, kNumTypes - 1, t)]; }
struct Pad { int type = htStutter, size = 7; float motion = 0; };
inline Pad factory (int i)
{
    static const Pad d[kPads] { { htStutter, 7, 0 }, { htStutter, 5, 0 }, { htStutter, 3, 0 }, { htStutter, 1, 0 },
                                { htBuild, 7, 0 }, { htGate, 5, 0.5f }, { htGate, 3, 0.5f }, { htGate, 2, 0.4f },
                                { htGlitch, 3, 0.6f }, { htGlitch, 1, 0.75f }, { htReverse, 7, 0 }, { htPitchDrop, 5, 0.5f },
                                { htTapeStop, 8, 0.3f }, { htLpSweep, 9, 0.5f }, { htHpSweep, 9, 0.5f }, { htCrush, 8, 0.5f } };
    return d[juce::jlimit (0, kPads - 1, i)];
}
} // namespace hauntdata

class HauntEditFx final : public CatalogEffect
{
public:
    using Pads = std::array<hauntdata::Pad, hauntdata::kPads>;
    HauntEditFx() : CatalogEffect (uHaunt) { for (int i = 0; i < hauntdata::kPads; ++i) model[(size_t) i] = hauntdata::factory (i); published.publishAll (model); }
    bool isRhythmic() const override { return true; }

    bool hasData() const override { return true; }
    juce::var getData() const override
    {
        juce::Array<juce::var> a; for (auto& p : model) a.add (juce::Array<juce::var> { p.type, p.size, p.motion });
        auto* o = new juce::DynamicObject(); o->setProperty ("pads", a); return juce::var (o);
    }
    void setData (const juce::var& v) override
    {
        for (int i = 0; i < hauntdata::kPads; ++i) model[(size_t) i] = hauntdata::factory (i);
        if (auto* a = v["pads"].getArray())
            for (int i = 0; i < hauntdata::kPads && i < a->size(); ++i) if (auto* q = (*a)[i].getArray(); q != nullptr && q->size() >= 3)
                model[(size_t) i] = { juce::jlimit (0, hauntdata::kNumTypes - 1, (int) (*q)[0]), juce::jlimit (0, 10, (int) (*q)[1]), juce::jlimit (0.0f, 1.0f, (float) (*q)[2]) };
        published.edit() = model; published.publish();
    }
    hauntdata::Pad pad (int i) const { return model[(size_t) juce::jlimit (0, hauntdata::kPads - 1, i)]; }
    void setPad (int i, hauntdata::Pad p) { model[(size_t) juce::jlimit (0, hauntdata::kPads - 1, i)] = p; published.edit() = model; published.publish(); }

    std::atomic<int> uiSel { 0 };                      // the pad being edited (the editor sets it; for the painted visual)
    std::atomic<int> uiPad { -1 };                     // the pad held with the mouse (-1 none)
    std::atomic<int> uiHeld { -1 }; std::atomic<float> uiEnv { 0 }, uiProgress { 0 };
    // test hook: the absolute sample (since reset) at which the playing gesture's slice grid starts, and the sample the pad went down
    long long debugOrigin = -1, debugPress = -1;

protected:
    void prepareEffect (double sampleRate, int, int) override
    {
        iPad = id ("pad"); iSync = id ("sync"); iLatch = id ("latch"); iMotion = id ("motion");
        tape.prepare ((int) (sampleRate * 12.0)); head.prepare (sampleRate); setEnvTimes();
        resetEffect();
    }
    void resetEffect() override
    {
        tape.clear(); head.reset(); held = gPad = -1; midiPad = -1; latched = -1; o = 0; tapeD = 0; absPos = 0; envV = 0; gateV = 1; lastParamPad = 0;
        for (auto& f : svf) f = {};
        debugOrigin = debugPress = -1;
    }
    void process (juce::dsp::AudioBlock<float> block, const ModulationContext& ctx) noexcept override
    {
        beginBlock (ctx);
        published.fetch();
        const int n = (int) block.getNumSamples(), nch = juce::jmin (2, (int) block.getNumChannels());
        float* L = block.getChannelPointer (0); float* R = nch > 1 ? block.getChannelPointer (1) : L;
        rhythm::HostClock clk; clk.set (ctx.tempo);
        const bool latch = on (iLatch);
        // the PAD parameter (automation): 1..16 holds that pad, 0 releases it
        const int paramPad = choice (iPad);
        if (paramPad != lastParamPad) { lastParamPad = paramPad; paramWant = paramPad - 1; }
        int ni = 0; const auto* ev = ctx.tempo.notes; const int nev = ev != nullptr ? ctx.tempo.numNotes : 0;
        for (int i = 0; i < n; ++i)
        {
            // MIDI notes land on their own sample
            while (ni < nev && ev[ni].sample <= i)
            {
                const int k = ev[ni].note - hauntdata::kNoteBase;
                if (k >= 0 && k < hauntdata::kPads)
                {
                    if (ev[ni].velocity > 0) { if (latch) latched = (latched == k ? -1 : k); else midiPad = k; }
                    else if (! latch && midiPad == k) midiPad = -1;
                }
                ++ni;
            }
            const int ui = uiPad.load (std::memory_order_relaxed);
            const int want = ui >= 0 ? ui : midiPad >= 0 ? midiPad : latched >= 0 ? latched : paramWant;
            if (want != held) { held = want; if (held >= 0) start (held, clk, i, ctx.tempo); }
            const float envTarget = held >= 0 ? 1.0f : 0.0f;
            envV += (envTarget > envV ? envUp : envDown) * (envTarget - envV);
            const float dl = L[i], dr = R[i];
            tape.write (dl, dr);
            if (gPad >= 0)
            {
                float gl = dl, gr = dr;
                gesture (gl, gr, dl, dr);
                L[i] = dl + envV * (gl - dl); if (nch > 1) R[i] = dr + envV * (gr - dr);
                o += 1.0;
                if (held < 0 && envV < 1.0e-4f) { gPad = -1; head.reset(); }
            }
            ++absPos;
        }
        uiHeld.store (held, std::memory_order_relaxed); uiEnv.store (envV, std::memory_order_relaxed);
        uiProgress.store (gPad >= 0 ? (float) std::fmod (o / sliceLen, 1.0) : 0.0f, std::memory_order_relaxed);
    }

private:
    void start (int padIndex, const rhythm::HostClock& clk, int i, const TempoInfo& t)
    {
        using namespace hauntdata;
        const auto& pads = published.read();
        def = pads[(size_t) padIndex]; gPad = padIndex; seed = rhythm::hash32 ((uint32_t) (++starts * 2654435761u));
        def.motion = juce::jlimit (0.0f, 1.0f, def.motion * 2.0f * p (iMotion).target);
        const double beats = sizeBeats (def.size, t.beatsPerBar());
        sliceLen = std::max (16.0, beats * clk.spb);
        o = 0; tapeD = 0; holdC = 0; gateV = 1;
        const bool timeType = def.type == htStutter || def.type == htBuild || def.type == htGlitch || def.type == htReverse || def.type == htPitchDrop || def.type == htGate;
        if (on (iSync) && timeType)
        {
            // the slices line up with the host grid: the gesture's clock starts on the last grid line (at most a beat back), so the
            // first slice is the one the song is in (no waiting: the pad answers at once)
            const double grid = std::min (beats, 1.0);
            o = rhythm::positiveMod (clk.ppqAt (i), grid) * clk.spb;
            o = std::min (o, tape.size() * 0.25);
        }
        debugPress = absPos; debugOrigin = absPos - (long long) std::llround (o);
        if (def.type == htLpSweep || def.type == htHpSweep) for (auto& f : svf) f = {};
    }
    struct Svf { float ic1 = 0, ic2 = 0, g = 0, k = 1, a1 = 1, a2 = 0, a3 = 0;
        void set (double sr, double f, double q) { g = (float) std::tan (rhythm::kPi * juce::jmin (f, sr * 0.45) / sr); k = (float) (1.0 / q); a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2; }
        void tick (float v0, float& lp, float& hp) { const float v3 = v0 - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3; ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2; lp = v2; hp = v0 - k * v1 - v2; } };
    void gesture (float& gl, float& gr, float dryL, float dryR) noexcept
    {
        using namespace hauntdata;
        const float m = def.motion; const double Ls = sliceLen;
        double D = 0; float g = 1.0f;
        const float gk = 1.0f - (float) std::exp (-1.0 / (0.0012 * sr));
        switch (def.type)
        {
            case htStutter: { const double ph = std::fmod (o, Ls); D = o - ph; const double duty = 1.0 - m * 0.8;
                              if (duty < 0.999) g = (float) juce::jlimit (0.0, 1.0, (duty * Ls - ph) / (0.0015 * sr)); break; }
            case htBuild:   { const double segLen = Ls * (1.0 - 0.5 * m); const int seg = std::min (4, (int) (o / segLen));
                              const double Lk = std::max (16.0, Ls / std::pow (2.0, seg)), oo = o - seg * segLen; D = oo - std::fmod (oo, Lk); break; }
            case htGate:    { const double ph = std::fmod (o, Ls) / Ls, duty = 0.15 + 0.7 * m; gateV += gk * ((ph < duty ? 1.0f : 0.0f) - gateV); g = gateV; break; }
            case htGlitch:  { const auto k = (uint32_t) (o / Ls); const double oo = std::fmod (o, Ls); const float h = rhythm::hashFloat (seed + k * 7919u);
                              const int act = h < m ? 1 + (int) (rhythm::hash32 (seed ^ (k * 104729u)) % 4u) : 0;
                              if (act == 1) D = Ls; else if (act == 2) D = 2.0 * oo; else if (act == 3) D = oo * 0.5;
                              gateV += gk * ((act == 4 ? 0.0f : 1.0f) - gateV); g = gateV; break; }
            case htReverse: D = 2.0 * std::fmod (o, Ls); break;
            case htPitchDrop: { const double k = std::floor (o / Ls), oo = o - k * Ls; const double sp = std::pow (0.5, k * (0.25 + 0.75 * m) * 0.5);
                                D = std::min (o - oo * sp, tape.size() * 0.9); break; }
            case htTapeStop: { const double sp = std::pow (std::max (0.0, 1.0 - o / Ls), 1.0 + m * 2.0); tapeD = std::min (tapeD + (1.0 - sp), tape.size() * 0.9); D = tapeD;
                               g = (float) std::min (1.0, sp * 5.0); break; }
            case htLpSweep: case htHpSweep:
            {
                const double u = std::min (1.0, o / Ls);
                if (((int) o & 15) == 0)
                { const double f = def.type == htLpSweep ? 18000.0 * std::pow (120.0 / 18000.0, u) : 20.0 * std::pow (6000.0 / 20.0, u); for (auto& s : svf) s.set (sr, f, 0.7 + m * 7.0); }
                float lp, hp; svf[0].tick (dryL, lp, hp); gl = def.type == htLpSweep ? lp : hp; svf[1].tick (dryR, lp, hp); gr = def.type == htLpSweep ? lp : hp;
                return;
            }
            case htCrush:
            {
                const double u = std::min (1.0, o / Ls); const int hold = 1 + (int) (u * u * (10.0 + m * 50.0));
                if (++holdC >= hold) { holdC = 0; hl = dryL; hr = dryR; }
                const float q = std::pow (2.0f, (float) (15.0 - u * (8.0 + m * 5.0))); gl = std::round (hl * q) / q; gr = std::round (hr * q) / q; return;
            }
            default: break;
        }
        head.advance (D);
        gl = head.tap (tape, 0, dryL) * g; gr = head.tap (tape, 1, dryR) * g;
    }

    Pads model;   // message thread
    rhythm::Published<Pads> published;
    rhythm::TapeBuffer tape; rhythm::TimeHead head; std::array<Svf, 2> svf;
    hauntdata::Pad def; int iPad = 0, iSync = 1, iLatch = 2, iMotion = 3;
    int held = -1, gPad = -1, midiPad = -1, latched = -1, paramWant = -1, lastParamPad = 0, holdC = 0;
    double o = 0, tapeD = 0, sliceLen = 24000; long long absPos = 0; uint32_t seed = 1, starts = 0;
    float envV = 0, gateV = 1, hl = 0, hr = 0, envUp = 0.02f, envDown = 0.004f;
public:
    void setEnvTimes() noexcept { envUp = 1.0f - (float) std::exp (-1.0 / (0.002 * sr)); envDown = 1.0f - (float) std::exp (-1.0 / (0.012 * sr)); }
};
} // namespace phantom
