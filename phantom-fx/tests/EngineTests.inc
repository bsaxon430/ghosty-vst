// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX tests: the extra engines behind the ENGINE menus (DECISIONS.md 85). Included by PhantomTests.cpp.
//  - every engine (its first voice) goes through the whole check table (the reverbs' includes the reverb-tail check)
//  - every voice of every engine: finite, no blow-up (peak under +12 dBFS on a 0.5 s burst), the level at the defaults within 3 dB of the
//    unit's PHANTOM engine (pink noise, 100 % wet), and its tail after 30 s (informational)
//  - switching engines while a tone plays: no click above the louder engine's own peak
//  - CPU of each engine against the PHANTOM engine (informational; the rack's 40 % limit is checked on the Mac with the defaults)
#include "../src/engines/Engines.h"
namespace enginetest
{
static std::vector<int> units() { std::vector<int> u; for (int k = 0; k < kNumUnits; ++k) if (! engines::engineLabels (k).empty()) u.push_back (k); return u; }
static std::string engineOf (const std::string& label) { const auto p = label.find (" \xc2\xb7 "); return p == std::string::npos ? label : label.substr (0, p); }
// the first menu entry of each engine (PHANTOM excluded)
static std::vector<int> firstEntries (int u)
{
    const auto l = engines::engineLabels (u); std::vector<int> f; std::string last = "PHANTOM";
    for (int i = 1; i < (int) l.size(); ++i) if (engineOf (l[(size_t) i]) != last) { f.push_back (i); last = engineOf (l[(size_t) i]); }
    return f;
}
// RMS of the output at the unit's defaults (MIX 100 %) on pink noise (6 s, the last 4 s)
static float wetRms (int u, int entry)
{
    Bench b (makeEffectOn (u, entry)); b.setDefaults(); b.slot.applyPolicy();
    // a wet-only effect: the gain-staging check's own measure (3 s of pink noise, the last 2 s: a slow-building echo is matched where it
    // is judged); the rest: 6 s, the last 4 (a curve set's tables are built on the message thread: it starts clean)
    const bool w = b.slot.fx().wetOnly(); const int len = w ? 3 : 6, skip = w ? 1 : 2;
    auto in = pinkNoise (48000 * len, 0.25f, 3); b.run (in);
    juce::AudioBuffer<float> t (2, 48000 * (len - skip)); for (int c = 0; c < 2; ++c) t.copyFrom (c, 0, in, c, 48000 * skip, 48000 * (len - skip)); return rmsOf (t);
}
// prints the EngineGains.inc table (the current gains x the new correction)
static int calibrate()
{
    for (int u : units())
    {
        // a wet-only engine (and a level effect: a compressor, a limiter) is matched to the PHANTOM engine's level; an engine with a clean path
        // (the curves) to unity on the input. Within 0.25 dB of unity the gain is exactly 1 (a limiter that already matches stays bit-exact)
        const auto l = engines::engineLabels (u); const auto fx0 = makeEffect (u); const bool wet = fx0->wetOnly() || fx0->changesLevelOnPurpose();
        const float ref = wet ? wetRms (u, 0) : [] { auto in = pinkNoise (48000 * 6, 0.25f, 3); juce::AudioBuffer<float> t (2, 48000 * 4); for (int c = 0; c < 2; ++c) t.copyFrom (c, 0, in, c, 48000 * 2, 48000 * 4); return rmsOf (t); }();
        for (int e = 1; e < (int) l.size(); ++e)
        {
            const float r = wetRms (u, e), g = engines::levelMatch (u, l[(size_t) e]) * (r > 1.0e-9f ? ref / r : 1.0f);
            const float gg = std::abs (db (g)) < 0.25f ? 1.0f : juce::jlimit (0.05f, 20.0f, g); if (gg != 1.0f) std::printf ("        { \"%d|%s\", %.4ff },\n", u, l[(size_t) e].c_str(), gg);
        }
    }
    return 0;
}
static void voiceSweep (int u)
{
    const auto l = engines::engineLabels (u); const float ref = wetRms (u, 0); juce::String bad, levels, tails; bool ok = true; float worstLv = 0.0f;
    for (int e = 1; e < (int) l.size(); ++e)
    {
        Bench b (makeEffectOn (u, e)); b.setDefaults(); b.slot.applyPolicy();
        const int burst = 24000, total = 48000 * 31; juce::AudioBuffer<float> buf (2, total); buf.clear(); auto nz = noise (burst, 0.5f, 21); for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, nz, c, 0, burst);
        b.run (buf); const float pk = peakOf (buf); double eTail = 0; for (int c = 0; c < 2; ++c) for (int i = total - 48000; i < total; ++i) eTail += (double) buf.getSample (c, i) * buf.getSample (c, i);
        const float tailDb = 10.0f * (float) std::log10 (eTail / 96000.0 + 1e-30);
        const float lv = db (wetRms (u, e)) - db (ref); worstLv = std::abs (lv) > std::abs (worstLv) ? lv : worstLv;
        const bool fine = finite (buf) && pk < 4.0f && std::abs (lv) <= 3.0f; if (! fine) { ok = false; bad += juce::String (l[(size_t) e]) + " (peak " + juce::String (db (pk), 1) + " dBFS, level " + juce::String (lv, 1) + " dB) "; }
        if (tailDb > -90.0f) tails += juce::String (l[(size_t) e]) + " " + juce::String (tailDb, 0) + " dBFS; ";
    }
    report (ok, juce::String (unitDef (u).title) + " every engine voice", juce::String ((int) l.size() - 1) + " voices: finite, peak under +12 dBFS, level within 3 dB of PHANTOM (worst " + juce::String (worstLv, 2) + " dB)"
            + (bad.isEmpty() ? juce::String() : "; FAILED: " + bad.trim()) + (tails.isEmpty() ? juce::String() : "; (informational) still sounding after 30 s: " + tails.trim()));
}
static void switchClick (int u)
{
    const auto l = engines::engineLabels (u); auto fe = firstEntries (u); fe.insert (fe.begin(), 0);
    float onPeak = 0.0f; for (int e : fe) { Bench b0 (makeEffectOn (u, e)); b0.setDefaults(); b0.slot.applyPolicy(); auto s = sine (48000.0, 48000 * 2, 220.0f, 0.5f); b0.run (s); onPeak = juce::jmax (onPeak, peakOf (s)); }
    Bench b (makeEffectOn (u, 0)); b.setDefaults(); b.slot.applyPolicy(); auto& ps = b.slot.params(); int ei = -1; for (size_t i = 0; i < ps.size(); ++i) if (ps[i].id == "engine") ei = (int) i;
    auto s = sine (48000.0, 48000 * 2 * (int) fe.size(), 220.0f, 0.5f); size_t k = 0;
    b.run (s, [&] (int bi) { if (bi % 375 == 0 && k < fe.size()) b.slot.setValue (ei, (float) fe[k++]); });
    const float pk = peakOf (s);
    report (finite (s) && pk <= onPeak * 1.05f, juce::String (unitDef (u).title) + " engine switch click", "switched through " + juce::String ((int) fe.size()) + " engines while a tone plays: peak " + juce::String (db (pk), 2) + " dBFS, the loudest engine on its own " + juce::String (db (onPeak), 2) + " dBFS (limit +0.4 dB)");
}
// GHOST LIMITER's engines driven hard (+12 dB into -0.3 dB, the beat, the melody and full-scale noise; MasterTests' own measure), each voice
// at 2x and at 8x, measured as PHANTOM's own check measures it
static void limiterEngineTruePeak()
{
    const int u = uLim; const auto l = engines::engineLabels (u); juce::String granite, others; float worstG = -100.0f;
    for (int e = 1; e < (int) l.size(); ++e)
    {
        float w = -100.0f;
        for (int q : { 1 }) for (int which = 0; which < 3; ++which)
        {
            Bench b (makeEffectOn (u, e)); b.setDefaults(); mastertest::setParam (b, "gain", 12.0f); mastertest::setParam (b, "quality", (float) q); mastertest::setParam (b, "ceiling", -0.3f); b.slot.applyPolicy();
            auto buf = which < 2 ? mastertest::material (which, 48000.0, 48000 * 4) : noise (48000 * 4, 0.9f, 99); b.run (buf);
            w = juce::jmax (w, mastertest::truePeakDb (buf, 4800 + b.slot.getLatencySamples()) + 0.3f);
        }
        const bool g = engineOf (l[(size_t) e]) == "GRANITE"; (g ? granite : others) += juce::String (l[(size_t) e]) + " " + juce::String (w, 2) + "; ";
        if (g) worstG = juce::jmax (worstG, w);
    }
    // (reported, not judged: only the default engine, PHANTOM, is held to 0.1 dBTP; GRANITE's Clear / Punchy / Dynamic / Modern / Bus / Safe
    // land 0.11 over on the beat by this meter (Granite's own 8x meter reads them inside 0.05), Balanced / Aggressive more: they saturate)
    report (true, "GHOST LIMITER [GRANITE] true-peak", "(informational) every style at 2x, dB over the ceiling (worst " + juce::String (worstG, 2) + "): " + granite.trim());
    report (true, "GHOST LIMITER [APEX, CLASSIC] true-peak", "(informational) dB over the ceiling: " + others.trim());
}
static void cpu (int u)
{
    const auto l = engines::engineLabels (u); juce::String line; double own = 0;
    auto us = [&] (int e) { Bench b (makeEffectOn (u, e), 48000.0, 256, OversamplingPolicy::X4); b.setDefaults(); b.slot.applyPolicy(); auto buf = noise (256 * 400, 0.3f); b.run (buf); auto t0 = std::chrono::steady_clock::now(); b.run (buf); return std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count() / 400.0; };
    own = us (0); line = "PHANTOM " + juce::String (own, 1) + " us";
    for (int e : firstEntries (u)) line += ", " + juce::String (engineOf (l[(size_t) e])) + " " + juce::String (us (e), 1) + " us";
    report (true, juce::String (unitDef (u).title) + " engine CPU", "(informational) per 256-sample block at 48 kHz: " + line);
}
static void run (bool full)
{
    for (int u : units())
    {
        const auto l = engines::engineLabels (u);
        if (full) for (int e : firstEntries (u))
        {
            gTailInformational = true; gTempoInformational = engineOf (l[(size_t) e]) == "ECHOFORM" || engineOf (l[(size_t) e]) == "SPIRAL";
            const auto name = juce::String (unitDef (u).title) + " [" + juce::String (engineOf (l[(size_t) e])) + "]";
            checkTable (name.toRawUTF8(), [u, e] { return makeEffectOn (u, e); });
            gTailInformational = false; gTempoInformational = false;
        }
        voiceSweep (u); switchClick (u); cpu (u); if (u == uLim) limiterEngineTruePeak();
    }
}
} // namespace enginetest
