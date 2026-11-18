// FX RACK (round 3) tests: reorderable rack, DISTORTION, COMPRESSOR, SPLITTERS. Included by TrapTests.cpp after SoundR2Tests.inc.
//   ProTests fxrack [renders dir] [shots dir]
#include "FxRackImpl.h"

namespace fxr3
{
using namespace gpfx;

// an 8-voice saw chord (init patch: one saw), held
static std::vector<Ev> sawChord (double len = 3.0)
{
    std::vector<Ev> e;
    for (int n : { 36, 48, 55, 60, 64, 67, 71, 74 }) { e.push_back ({ 0.0, n, 0.8f, true }); e.push_back ({ len, n, 0.0f, false }); }
    return e;
}
static std::unique_ptr<ProProcessor> chordProc (std::initializer_list<std::pair<const char*, float>> kv = {})
{
    auto p = makeProc (-1); p->initPatch();
    cleantest::set (*p, { { "out_trim", -6.0f }, { "quality", 0.0f }, { "verb", 0.0f }, { "echo", 0.0f } });
    cleantest::set (*p, kv);
    settle (*p);
    return p;
}
// renders like render(), calling 'act' (seconds) before every block
static juce::AudioBuffer<float> renderActs (ProProcessor& p, const std::vector<Ev>& evs, double seconds, const std::function<void (double)>& act, int bs = BS)
{
    const int total = (int) (seconds * SR);
    juce::AudioBuffer<float> out (2, total), blk (2, bs);
    for (int pos = 0, idx = 0; pos < total; pos += bs)
    {
        const int n = juce::jmin (bs, total - pos);
        juce::MidiBuffer mb;
        for (auto& e : evs) { const int s = (int) (e.t * SR); if (s >= pos && s < pos + n) mb.addEvent (e.on ? juce::MidiMessage::noteOn (1, e.note, e.vel) : juce::MidiMessage::noteOff (1, e.note), s - pos); }
        if (act) act (pos / SR);
        blk.setSize (2, n, false, false, true);
        seedSystemRandom (0x5eed + idx++);
        p.processBlock (blk, mb);
        for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, blk, ch, 0, n);
    }
    return out;
}
// click metrics: largest sample step, loudest 10 ms of HF (> 6 kHz) power, quietest 5 ms window (RMS dB) in [s, e)
struct Clicks { float jump = 0; double hfWin = -200, minWin = 200; int jumpAt = 0, hfAt = 0; };
static Clicks clicks (const juce::AudioBuffer<float>& b, int s, int e)
{
    Clicks c; zippertest::HfMeter hm;
    double acc = 0, rmsAcc = 0; int k = 0, k5 = 0;
    for (int i = std::max (1, s - 4800); i < e; ++i)
    {
        double h = 0, r = 0;
        for (int ch = 0; ch < 2; ++ch)
        {
            const float x = b.getSample (ch, i);
            const double hh = hm.tick (x, ch); h += hh * hh; r += (double) x * x;
            if (i >= s && std::abs (x - b.getSample (ch, i - 1)) > c.jump) { c.jump = std::abs (x - b.getSample (ch, i - 1)); c.jumpAt = i; }
        }
        if (i < s) continue;                  // (the meter warms up)
        acc += h; if (++k == 480) { const double w = 10 * std::log10 (acc / 480 + 1e-20); if (w > c.hfWin) { c.hfWin = w; c.hfAt = i; } acc = 0; k = 0; }
        rmsAcc += r; if (++k5 == 240) { c.minWin = std::min (c.minWin, 10 * std::log10 (rmsAcc / 480 + 1e-20)); rmsAcc = 0; k5 = 0; }
    }
    return c;
}
static void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b)
{
    f.getParentDirectory().createDirectory(); f.deleteFile();
    juce::WavAudioFormat wav;
    if (auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream()))
        if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (os.get(), SR, 2, 24, {}, 0))) { os.release(); w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples()); printf ("    wrote %s\n", f.getFullPathName().toRawUTF8()); }
}
static std::array<int, kNumUnits> orderWith (std::initializer_list<int> front)
{   // these units first (in this order), the rest in default order, the limiter last
    std::array<int, kNumUnits> o {}; int k = 0; std::array<bool, kNumUnits> used {};
    for (int u : front) { o[(size_t) k++] = u; used[(size_t) u] = true; }
    for (int u = 0; u < kNumUnits; ++u) if (! used[(size_t) u] && u != uLim) o[(size_t) k++] = u;
    o[kNumUnits - 1] = uLim; return o;
}

// ---------------------------------------------------------------- 1. reorderable rack
static void reorderTests (const juce::File& renders)
{
    section ("REORDER: the order survives save / load; a reorder while audio runs crossfades (12 ms) through the chain's input, no click, no drop");
    {   // round trip
        auto p = chordProc ({ { "fx_chorus_on", 1.0f }, { "fx_wreck_on", 1.0f } });
        const auto o = orderWith ({ uChorus, uHyper, uEcho, uWreck, uFatten });
        p->rack.impl->setOrder (o);
        juce::MemoryBlock mb; p->getStateInformation (mb);
        auto q = makeProc (-1); q->setStateInformation (mb.getData(), (int) mb.getSize()); settle (*q);
        CHECK (q->rack.impl->getOrder() == o, "a moved chain (CHORUS, HYPER, ECHO, WRECKER, FATTEN first) comes back from the saved state");
        auto d = makeProc (-1);
        const auto ord = d->rack.saveState().getProperty ("order").toString();
        bool lateLeftOut = true; for (int u : lateUnits()) lateLeftOut &= ! juce::StringArray::fromTokens (ord, ",", "").contains (unitInfo (u).key);
        CHECK (lateLeftOut, "the default order saves without the late units (older builds read it unchanged): \"%s\"", ord.toRawUTF8());
        d->rack.impl->setOrder (o); d->rack.impl->resetOrder();
        std::array<int, kNumUnits> def {}; for (int i = 0; i < kNumUnits; ++i) def[(size_t) i] = i;
        CHECK (d->rack.impl->getOrder() == def, "RESET ORDER gives the default chain back");
    }
    {   // click-free: an 8-voice saw chord through WRECKER -> CHORUS -> ECHO (+ SPACE); the order flips every 0.45 s
        auto kv = { std::pair<const char*, float> { "fx_wreck_on", 1.0f }, { "fx_wreck_drive", 0.5f }, { "fx_chorus_on", 1.0f }, { "fx_chorus_mix", 0.5f },
                    { "fx_echo_on", 1.0f }, { "fx_echo_mix", 0.3f }, { "fx_space_on", 1.0f }, { "fx_space_mix", 0.25f } };
        const auto A = orderWith ({}), B = orderWith ({ uEcho, uSpace, uChorus, uWreck });
        const double len = 3.2;
        auto stat = [&] (const std::array<int, kNumUnits>& o) { auto p = chordProc (kv); p->rack.impl->setOrder (o); p->rack.impl->applyPendingOrderNow(); settle (*p); return renderActs (*p, sawChord (len), len, {}); };
        const auto ra = stat (A), rb = stat (B);
        const auto rd = [&] { auto q = chordProc(); return renderActs (*q, sawChord (len), len, {}); }();   // the chain's input (what a switch passes through)
        auto p = chordProc (kv);
        int flips = 0; double next = 0.6;
        const auto rs = renderActs (*p, sawChord (len), len, [&] (double t) { if (t >= next && next < len - 0.5) { p->rack.impl->setOrder ((flips++ & 1) ? A : B); next += 0.45; } });
        const int s = (int) (0.5 * SR), e = (int) ((len - 0.1) * SR);
        const auto ca = clicks (ra, s, e), cb = clicks (rb, s, e), cd = clicks (rd, s, e), cs = clicks (rs, s, e);
        const float refJump = std::max ({ ca.jump, cb.jump, cd.jump }); const double refHf = std::max ({ ca.hfWin, cb.hfWin, cd.hfWin }), refMin = std::min (ca.minWin, cb.minWin);
        printf ("    static A / B / chain input: largest step %.4f / %.4f / %.4f, loudest 10 ms HF %.1f / %.1f / %.1f dB, quietest 5 ms %.1f / %.1f dB\n", ca.jump, cb.jump, cd.jump, ca.hfWin, cb.hfWin, cd.hfWin, ca.minWin, cb.minWin);
        printf ("    %d reorders:  largest step %.4f (at %.4f s), loudest 10 ms HF %.1f dB (at %.3f s), quietest 5 ms %.1f dB\n", flips, cs.jump, cs.jumpAt / SR, cs.hfWin, cs.hfAt / SR, cs.minWin);
        CHECK (flips >= 5 && cs.jump <= refJump + 0.05f && cs.hfWin <= refHf + 3.0, "reorders without a click: step %+.4f over the static renders' own, HF window %+.1f dB", cs.jump - refJump, cs.hfWin - refHf);
        CHECK (cs.minWin >= refMin - 6.0, "a reorder never drops out: quietest 5 ms %.1f dB vs %.1f dB in the static renders", cs.minWin, refMin);
        if (renders.getFullPathName().isNotEmpty()) writeWav (renders.getChildFile ("reorder_flips_8voice_saw.wav"), rs);
    }
}

// ---------------------------------------------------------------- unit-level helpers
// runs one rack unit directly on a stereo test signal (blocks of 256), after warm samples of the same signal
static juce::AudioBuffer<float> runUnit (ProProcessor& p, int u, const std::function<float (int)>& sig, int len, int warm = 9600)
{
    auto& unit = *p.rack.impl->units[(size_t) u];
    unit.reset(); unit.clearLines();
    juce::AudioBuffer<float> out (2, len);
    std::array<float, kSub> l {}, r {};
    BlockCtx ctx; ctx.sr = SR;
    for (int pos = -warm; pos < len; pos += kSub)
    {
        const int n = kSub;
        for (int i = 0; i < n; ++i) { l[(size_t) i] = r[(size_t) i] = sig (pos + i); }
        unit.process (l.data(), r.data(), n, ctx);
        for (int i = 0; i < n; ++i) if (pos + i >= 0 && pos + i < len) { out.setSample (0, pos + i, l[(size_t) i]); out.setSample (1, pos + i, r[(size_t) i]); }
    }
    return out;
}
// THD (harmonics 2 .. up to 20 kHz re the fundamental, dB) and the worst non-harmonic line re the loudest harmonic (dB), 65536-point BH4
struct Spec { double thdDb = -200, aliasDb = -200, fundDb = -200; };
static Spec spectrum (const juce::AudioBuffer<float>& b, double f0)
{
    constexpr int ORDER = 16, N = 1 << ORDER;
    juce::dsp::FFT fft (ORDER); std::vector<float> buf ((size_t) N * 2, 0.0f);
    for (int i = 0; i < N; ++i) buf[(size_t) i] = b.getSample (0, i) * (float) r2::bh4 (i, N);
    fft.performFrequencyOnlyForwardTransform (buf.data());
    const double binW = SR / N; double fund = 0, harm = 0, top = 0, between = 0;
    for (int k = 2; k < N / 2; ++k)
    {
        const double f = k * binW, h = f / f0, frac = std::abs (h - std::round (h)), e = (double) buf[(size_t) k] * buf[(size_t) k];
        if (frac * f0 < 6.0 * binW) { top = std::max (top, e); if (std::round (h) == 1.0) fund += e; else if (f < 20000.0) harm += e; }
        else if (frac > 0.02 && f < 20000.0 && f > 30.0) between = std::max (between, e);
    }
    Spec s; s.fundDb = 10 * std::log10 (fund + 1e-30); s.thdDb = 10 * std::log10 ((harm + 1e-30) / (fund + 1e-30)); s.aliasDb = 10 * std::log10 ((between + 1e-30) / (top + 1e-30));
    return s;
}
// ZIPPER (as ZipperTests.inc): a held, nearly-sine saw while 'id' sweeps lo..hi..lo at mouse speed (a value per 64-sample block)
// vs the same path quasi-static; bars: HF > 6 kHz +1 dB, step +0.05, loudest 10 ms HF +3 dB
static bool knobZipper (const char* unitName, std::initializer_list<std::pair<const char*, float>> kv, const char* id, float lo, float hi)
{
    auto mk = [&] { auto p = makeProc (-1); p->prepareToPlay (SR, zippertest::ZBS); p->setParam ("verb", 0.0f); p->setParam ("echo", 0.0f); p->setParam ("cutoff", 0.25f);
                    cleantest::set (*p, { { "out_trim", -6.0f } }); cleantest::set (*p, kv); settle (*p); return p; };
    auto setV = [] (ProProcessor& p, const char* pid_, float v) { p.setParam (pid_, v); };
    constexpr double SWEEP = 1.8, PRE = 0.5;
    auto at = [&] (double t) { return lo + (hi - lo) * (float) zippertest::tri (t, 0.3); };
    zippertest::Meas fast, ref;
    {
        auto p = mk(); setV (*p, id, at (0));
        juce::AudioBuffer<float> blk (2, zippertest::ZBS); juce::MidiBuffer mb; zippertest::HfMeter hm; float prev[2] {}; int idx = 0;
        mb.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0);
        for (int k = 0; k < (int) (PRE * SR / zippertest::ZBS); ++k) zippertest::block (*p, blk, mb, idx, hm, prev, nullptr);
        double lastWin = 0;
        for (int k = 0; k < (int) (SWEEP * SR / zippertest::ZBS); ++k)
        {
            setV (*p, id, at (k * (double) zippertest::ZBS / SR)); zippertest::block (*p, blk, mb, idx, hm, prev, &fast);
            if (std::getenv ("FXR3_ZIPDBG") != nullptr && fast.win > lastWin * 1.5) { printf ("      loudest 10 ms so far %.1f dB at %.3f s (value %.3f)\n", 10 * std::log10 (fast.win + 1e-20), k * (double) zippertest::ZBS / SR, at (k * (double) zippertest::ZBS / SR)); lastWin = fast.win; }
        }
    }
    {
        auto p = mk(); setV (*p, id, at (0));
        juce::AudioBuffer<float> blk (2, zippertest::ZBS); juce::MidiBuffer mb; zippertest::HfMeter hm; float prev[2] {}; int idx = 0;
        mb.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0);
        for (int k = 0; k < (int) (PRE * SR / zippertest::ZBS); ++k) zippertest::block (*p, blk, mb, idx, hm, prev, nullptr);
        for (int j = 0; j < 36; ++j)
        {
            setV (*p, id, at ((j + 0.5) * SWEEP / 36));
            for (int k = 0; k < (int) (0.3 * SR / zippertest::ZBS); ++k) zippertest::block (*p, blk, mb, idx, hm, prev, nullptr);
            zippertest::Meas m;
            for (int k = 0; k < (int) (0.05 * SR / zippertest::ZBS); ++k) zippertest::block (*p, blk, mb, idx, hm, prev, &m);
            ref.hf += m.hf; ref.cnt += m.cnt; ref.jump = std::max (ref.jump, m.jump); ref.win = std::max (ref.win, m.win);
        }
    }
    const double pf = fast.hf / std::max (1L, fast.cnt), pr = ref.hf / std::max (1L, ref.cnt);
    // (a -100 dB floor: below it there is no audible crackle to measure, only rounding)
    const double addDb = 10 * std::log10 ((pf + 1e-10) / (pr + 1e-10)), winDb = 10 * std::log10 ((fast.win + 1e-10) / (ref.win + 1e-10));
    const float extra = fast.jump - ref.jump;
    const bool ok = addDb <= 1.0 && extra <= 0.05f && winDb <= 3.0;
    CHECK (ok, "ZIPPER %s %-6s %g .. %g: HF %+5.1f dB, loudest 10 ms %+5.1f dB, step %+.4f", unitName, id + 3, lo, hi, addDb, winDb, extra);
    return ok;
}

// ---------------------------------------------------------------- 2. DISTORTION
static void distTests (const juce::File& renders)
{
    section ("DISTORTION: THD rises with DRIVE, aliases <= -70 dB at ~1 kHz full drive (4x + ADAA), true peak under the ceiling, MIX 0 = bypass, no zipper");
    auto p = makeProc (-1);
    const double F0 = 1361.0 * SR / 65536.0;             // 996.8 Hz, on a bin (not a divisor of 48 kHz: the aliases fall between the harmonics)
    auto sine = [F0] (double amp) { return [F0, amp] (int i) { return (float) (amp * std::sin (2.0 * juce::MathConstants<double>::pi * F0 * i / SR)); }; };
    auto setM = [&] (int m, float drv) { p->setParam ("fx_dist_mode", (float) m); p->setParam ("fx_dist_drive", drv); p->setParam ("fx_dist_fpos", 0.0f); p->setParam ("fx_dist_level", 0.0f); };
    // THD over the drive
    const int clipModes[] { dmTube, dmSoft, dmHard, dmDiode, dmAsym };
    bool mono = true; juce::String rows;
    for (int m = 0; m < kNumDistModes; ++m)
    {
        if (m == dmXShaper) continue;
        printf ("    %-11s THD:", distModeNames()[m].toRawUTF8());
        double prev = -1000; bool up = true; double first = 0, last = 0;
        for (float d : { 0.0f, 6.0f, 12.0f, 18.0f, 24.0f, 30.0f, 36.0f })
        {
            setM (m, d);
            const auto out = runUnit (*p, uDist, sine (0.5), 65536);
            const auto sp = spectrum (out, F0);
            printf (" %6.1f", sp.thdDb);
            if (d == 0.0f) first = sp.thdDb; last = sp.thdDb;
            if (sp.thdDb < prev - 0.05) up = false;
            prev = sp.thdDb;
        }
        const bool isClip = std::find (std::begin (clipModes), std::end (clipModes), m) != std::end (clipModes);
        printf ("  dB (drive 0 .. 36 dB)%s\n", isClip ? (up ? "  monotone" : "  NOT MONOTONE") : (last > first ? "  rises" : "  FLAT"));
        if (isClip) mono &= up; else if (m != dmDownsample) mono &= last > first;
    }
    CHECK (mono, "THD rises monotonically with DRIVE on the clipping curves (TUBE, SOFT, HARD, DIODE, ASYM); the folds / ZERO-SQUARE end higher than they start");
    // aliasing at full drive
    double worst = -200; juce::String worstName;
    for (int m = 0; m < kNumDistModes; ++m)
    {
        if (m == dmDownsample) continue;   // (sample-and-hold aliases by design: that is its sound)
        for (double amp : { 0.5, 1.0 })
        {
            setM (m, 36.0f);
            const auto sp = spectrum (runUnit (*p, uDist, sine (amp), 65536), F0);
            if (sp.aliasDb > worst) { worst = sp.aliasDb; worstName = distModeNames()[m] + " at " + juce::String (amp == 1.0 ? "0" : "-6") + " dBFS"; }
            printf ("    %-11s full drive, %s dBFS in: worst alias line %6.1f dB re the loudest harmonic\n", distModeNames()[m].toRawUTF8(), amp == 1.0 ? " 0" : "-6", sp.aliasDb);
        }
    }
    CHECK (worst <= -70.0, "aliases at ~1 kHz, full drive: worst line %.1f dB (%s), bar -70 dB", worst, worstName.toRawUTF8());
    // the whole synth: true peak under the ceiling on an 8-voice saw chord, every mode at full drive, filter POST
    {
        double tpWorst = -200;
        for (int m = 0; m < kNumDistModes; ++m)
        {
            auto q = chordProc ({ { "fx_dist_on", 1.0f }, { "fx_dist_mode", (float) m }, { "fx_dist_drive", 36.0f }, { "fx_dist_level", 12.0f }, { "out_trim", 6.0f } });
            const auto out = renderActs (*q, sawChord (1.5), 1.6, {});
            tpWorst = std::max (tpWorst, cleantest::truePeakDb (out));
        }
        CHECK (tpWorst <= -0.4, "every mode at full drive and +12 dB LEVEL (+6 dB trim): true peak %.2f dBTP, under the -0.5 dBTP ceiling (0.1 dB meter tolerance)", tpWorst);
    }
    // MIX 0 = bypass, bit for bit
    {
        // measured at the rack itself (the synth's final ceiling decides per block, so a few samples of shift change its rounding)
        auto rackRun = [] (ProProcessor& q, int len)
        {
            juce::AudioBuffer<float> out (2, len), blk (2, BS);
            q.rack.snapOnNextBlock();
            const double fr[] { 65.4, 130.8, 196.0, 261.6, 329.6, 392.0, 493.9, 587.3 };
            for (int pos = 0; pos < len; pos += BS)
            {
                const int n = std::min (BS, len - pos); blk.setSize (2, n, false, false, true);
                for (int i = 0; i < n; ++i) { double v = 0; for (int k = 0; k < 8; ++k) v += 0.08 * (2.0 * std::fmod (fr[k] * (pos + i) / SR + 0.1 * k, 1.0) - 1.0); blk.setSample (0, i, (float) v); blk.setSample (1, i, (float) (0.9 * v)); }
                q.rack.process (blk, { SR, 120.0, pos / SR * 2.0, true, 0, -1, -1 });
                for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, blk, ch, 0, n);
            }
            return out;
        };
        auto a = makeProc (-1); cleantest::set (*a, { { "fx_dist_on", 1.0f }, { "fx_dist_mix", 0.0f }, { "fx_dist_drive", 30.0f }, { "fx_dist_mode", (float) dmHard } });
        auto b = makeProc (-1); cleantest::set (*b, { { "fx_dist_on", 0.0f } });
        const auto ra = rackRun (*a, 48000), rb = rackRun (*b, 48000);
        const int dl = a->rack.latencySamples() - b->rack.latencySamples();
        float md = 0; for (int ch = 0; ch < 2; ++ch) for (int i = (int) (0.03 * SR); i + dl < ra.getNumSamples(); ++i) md = std::max (md, std::abs (ra.getSample (ch, i + dl) - rb.getSample (ch, i)));   // (after 30 ms: the dry delay glides in when the unit switches on)
        CHECK (dl >= 0 && md == 0.0f, "MIX 0 is the bypass, bit for bit at the rack after delay compensation (reported latency +%d samples while on; max diff %.3g)", dl, md);
        auto c = makeProc (-1); auto dflt = makeProc (-1); dflt->setParam ("fx_dist_on", 0.0f);
        const auto rc = render (*c, testPhrase(), 2.0), rd = render (*dflt, testPhrase(), 2.0);
        CHECK (maxDiff (rc, rd) == 0.0f, "DISTORTION off (default) leaves the synth untouched (max diff %.3g)", maxDiff (rc, rd));
    }
    // KEY TRACK follows the note; X-SHAPER: curves survive save / load, DRIVE morphs A -> B
    {
        auto q = chordProc ({ { "fx_dist_on", 1.0f }, { "fx_dist_mode", (float) dmXShaper } });
        auto cv = q->rack.impl->distCurves.get(); cv.symmetric = false; for (auto& v : cv.b) v = 0.3f * v; q->rack.impl->distCurves.set (cv);
        juce::MemoryBlock mb; q->getStateInformation (mb);
        auto r = makeProc (-1); r->setStateInformation (mb.getData(), (int) mb.getSize()); settle (*r);
        CHECK (r->rack.impl->distCurves.get() == cv, "X-SHAPER curves (asymmetric, B redrawn) come back from the saved state");
        auto dflt = makeProc (-1);
        CHECK (! dflt->rack.saveState().getChildWithName ("DISTCURVES").isValid(), "factory curves are not saved (older states stay byte-identical)");
        setM (dmXShaper, 0.0f); const auto sa = spectrum (runUnit (*p, uDist, sine (0.5), 65536), F0);
        setM (dmXShaper, 36.0f); const auto sb = spectrum (runUnit (*p, uDist, sine (0.5), 65536), F0);
        CHECK (sb.thdDb > sa.thdDb + 6.0, "X-SHAPER DRIVE 0 = curve A (THD %.1f dB), full = curve B (fold, THD %.1f dB)", sa.thdDb, sb.thdDb);
    }
    // zipper on the main knobs
    {
        const auto base = { std::pair<const char*, float> { "fx_dist_on", 1.0f }, { "fx_dist_mode", (float) dmSoft }, { "fx_dist_drive", 12.0f } };
        knobZipper ("DIST", base, "fx_dist_drive", 0.0f, 36.0f);
        knobZipper ("DIST", base, "fx_dist_level", -24.0f, 12.0f);
        knobZipper ("DIST", { { "fx_dist_on", 1.0f }, { "fx_dist_fpos", 2.0f }, { "fx_dist_q", 3.0f } }, "fx_dist_freq", 200.0f, 8000.0f);
        knobZipper ("DIST", { { "fx_dist_on", 1.0f }, { "fx_dist_fpos", 1.0f }, { "fx_dist_freq", 900.0f } }, "fx_dist_ftype", 0.0f, 1.0f);
        knobZipper ("DIST", { { "fx_dist_on", 1.0f }, { "fx_dist_mode", (float) dmXShaper } }, "fx_dist_drive", 0.0f, 36.0f);
    }
    if (renders.getFullPathName().isNotEmpty())
        for (int m : { (int) dmTube, (int) dmHard, (int) dmSinFold, (int) dmXShaper })
        {
            auto q = chordProc ({ { "fx_dist_on", 1.0f }, { "fx_dist_mode", (float) m }, { "fx_dist_drive", 18.0f }, { "fx_dist_fpos", 2.0f }, { "fx_dist_freq", 6000.0f }, { "fx_dist_ftype", 0.0f } });
            writeWav (renders.getChildFile ("dist_" + distModeNames()[m].toLowerCase().replace (" ", "").replace ("-", "") + "_8voice_saw.wav"), renderActs (*q, sawChord (2.5), 3.0, {}));
        }
}

// ---------------------------------------------------------------- 3. COMPRESSOR
static void compTests (const juce::File& renders)
{
    section ("COMPRESSOR: static curve = RATIO (0.3 dB), LIMIT never passes its ceiling (true peak), 3 bands sum flat (0.1 dB), latency compensated, RACK INPUT key, no zipper");
    auto p = makeProc (-1);
    const double F0 = 1361.0 * SR / 65536.0;
    auto setC = [&] (std::initializer_list<std::pair<const char*, float>> kv) { cleantest::set (*p, { { "fx_comp_on", 1.0f }, { "fx_comp_mode", 0.0f }, { "fx_comp_knee", 0.0f }, { "fx_comp_makeup", 0.0f }, { "fx_comp_attack", 0.5f }, { "fx_comp_release", 300.0f } }); cleantest::set (*p, kv); };
    // static curve
    double worst = 0;
    for (float rat : { 2.0f, 4.0f, 10.0f })
    {
        printf ("    RATIO %4.1f, THRESH -30:", rat);
        for (float lvl : { -42.0f, -30.0f, -24.0f, -18.0f, -12.0f, -6.0f, -1.0f })
        {
            setC ({ { "fx_comp_ratio", rat }, { "fx_comp_thresh", -30.0f } });
            const double amp = std::pow (10.0, lvl / 20.0);
            const auto out = runUnit (*p, uComp, [=] (int i) { return (float) (amp * std::sin (2.0 * juce::MathConstants<double>::pi * F0 * i / SR)); }, 24000, 48000);
            double so = 0; for (int i = 4800; i < 24000; ++i) so += (double) out.getSample (0, i) * out.getSample (0, i);
            const double got = 10 * std::log10 (so / 19200.0) - 20 * std::log10 (amp / std::sqrt (2.0));
            const double want = CompUnit::curve (lvl, -30.0f, 0.0f, rat);
            worst = std::max (worst, std::abs (got - want));
            printf ("  %+.1f (%+.1f)", got, want);
        }
        printf ("  dB gain at -42 .. -1 dBFS\n");
    }
    CHECK (worst <= 0.3, "the static curve matches THRESH / RATIO within %.2f dB (bar 0.3)", worst);
    // LIMIT: an 8-voice saw stack driven +24 dB into a -1 dB ceiling, ATTACK 0 / 3 / 10 ms
    {
        double tpWorst = -200;
        for (float at : { 0.0f, 3.0f, 10.0f })
        {
            setC ({ { "fx_comp_ratio", kCompRatioMax }, { "fx_comp_thresh", -1.0f }, { "fx_comp_makeup", 24.0f }, { "fx_comp_attack", at }, { "fx_comp_release", 60.0f } });
            const double fr[] { 65.4, 130.8, 196.0, 261.6, 329.6, 392.0, 493.9, 587.3 };
            const auto out = runUnit (*p, uComp, [&] (int i) { double v = 0; for (int k = 0; k < 8; ++k) { const double ph = std::fmod (fr[k] * i / SR + 0.13 * k, 1.0); v += 0.11 * (2.0 * ph - 1.0); } return (float) (v * (0.6 + 0.4 * std::sin (i * 0.0003))); }, 96000);
            const double tp = cleantest::truePeakDb (out, 4800);
            tpWorst = std::max (tpWorst, tp);
            printf ("    LIMIT attack %4.1f ms: true peak %+.2f dBTP (ceiling -1.00), GR %.1f dB\n", at, tp, p->rack.impl->tm.compGr.load());
        }
        CHECK (tpWorst <= -1.0 + 0.05, "LIMIT never exceeds its ceiling: worst true peak %.2f dBTP vs -1.00 (0.05 dB meter tolerance)", tpWorst);
    }
    // MULTIBAND at 0 dB of gain reduction: the three bands sum flat
    {
        double dev = 0;
        for (auto xs : { std::pair<float, float> { 150.0f, 2500.0f }, { 80.0f, 6000.0f }, { 400.0f, 1200.0f } })
        {
            setC ({ { "fx_comp_mode", 1.0f }, { "fx_comp_thresh", 0.0f }, { "fx_comp_ratio", 4.0f }, { "fx_comp_xlo", xs.first }, { "fx_comp_xhi", xs.second } });
            const auto ir = runUnit (*p, uComp, [] (int i) { return i == 0 ? 0.001f : 0.0f; }, 65536, 4800);
            constexpr int ORDER = 16, N = 1 << ORDER; juce::dsp::FFT fft (ORDER); std::vector<float> buf ((size_t) N * 2, 0.0f);
            for (int i = 0; i < N; ++i) buf[(size_t) i] = ir.getSample (0, i) * 1000.0f;
            fft.performFrequencyOnlyForwardTransform (buf.data());
            double lo = 100, hi = -100;
            for (int k = 1; k < N / 2; ++k) { const double f = k * SR / N; if (f < 20.0 || f > 20000.0) continue; const double db = 20 * std::log10 (buf[(size_t) k] + 1e-12); lo = std::min (lo, db); hi = std::max (hi, db); }
            printf ("    bands %g / %g Hz: sum %+.3f .. %+.3f dB (20 Hz .. 20 kHz)\n", xs.first, xs.second, lo, hi);
            dev = std::max ({ dev, std::abs (lo), std::abs (hi) });
        }
        CHECK (dev <= 0.1, "MULTIBAND bands sum flat within %.3f dB (bar 0.1)", dev);
    }
    // latency: reported while on, the dry lines up with the wet (RATIO 1 at MIX 50 % = the input, 10 ms later, no comb)
    {
        auto a = chordProc ({ { "fx_comp_on", 1.0f }, { "fx_comp_ratio", 1.0f }, { "fx_comp_mix", 0.5f } }); a->rack.snapOnNextBlock();
        auto b = chordProc ({ { "fx_comp_on", 1.0f }, { "fx_comp_ratio", 1.0f }, { "fx_comp_mix", 1.0f } }); b->rack.snapOnNextBlock();
        const int lat = a->rack.latencySamples();
        auto c = chordProc(); const int lat0 = c->rack.latencySamples();
        const auto ra = renderActs (*a, sawChord (1.0), 1.2, {}), rb = renderActs (*b, sawChord (1.0), 1.2, {});
        float md = 0; int mdAt = 0; for (int ch = 0; ch < 2; ++ch) for (int i = (int) (0.05 * SR); i < ra.getNumSamples(); ++i) if (std::abs (ra.getSample (ch, i) - rb.getSample (ch, i)) > md) { md = std::abs (ra.getSample (ch, i) - rb.getSample (ch, i)); mdAt = i; }
        CHECK (lat - lat0 == (int) std::lround (0.010 * SR) + 12 && md < 1.0e-3f, "COMPRESSOR on reports %d samples more latency; dry and wet line up (MIX 50 %% vs 100 %% at RATIO 1: max diff %.2g)", lat - lat0, md);
    }
    // KEY = RACK INPUT: an echo before the compressor; its tail is not squashed when the key is the dry synth
    {
        double tail[2] {};
        for (int k = 0; k < 2; ++k)
        {
            auto q = chordProc ({ { "fx_echo_on", 1.0f }, { "fx_echo_mix", 1.0f }, { "fx_echo_fb", 0.7f }, { "fx_comp_on", 1.0f }, { "fx_comp_thresh", -40.0f }, { "fx_comp_ratio", 10.0f }, { "fx_comp_key", (float) k } });
            const auto out = renderActs (*q, sawChord (0.4), 2.0, {});
            tail[k] = rmsDb (out, (int) (1.0 * SR), (int) (1.9 * SR));
        }
        CHECK (tail[1] > tail[0] + 3.0, "RACK INPUT key: the echo tail after the notes stays %.1f dB louder than with the UNIT INPUT key (%.1f vs %.1f dB)", tail[1] - tail[0], tail[1], tail[0]);
    }
    // switching on / off and into LIMIT while a chord plays: no click
    {
        auto q = chordProc ({ { "fx_comp_thresh", -24.0f }, { "fx_comp_ratio", 4.0f } });
        const auto rs = renderActs (*q, sawChord (3.0), 3.0, [&] (double t) {
            if (std::abs (t - 0.6) < 0.006) q->setParam ("fx_comp_on", 1.0f);
            if (std::abs (t - 1.2) < 0.006) q->setParam ("fx_comp_ratio", kCompRatioMax);
            if (std::abs (t - 1.8) < 0.006) q->setParam ("fx_comp_mode", 1.0f);
            if (std::abs (t - 2.1) < 0.006) q->setParam ("fx_comp_ratio", 4.0f);
            if (std::abs (t - 2.5) < 0.006) q->setParam ("fx_comp_on", 0.0f); });
        auto st = chordProc ({ { "fx_comp_on", 1.0f }, { "fx_comp_thresh", -24.0f }, { "fx_comp_ratio", 4.0f } });
        const auto rr = renderActs (*st, sawChord (3.0), 3.0, {});
        auto dry = chordProc(); const auto rd = renderActs (*dry, sawChord (3.0), 3.0, {});
        const auto cs = clicks (rs, (int) (0.4 * SR), (int) (2.9 * SR)), cr = clicks (rr, (int) (0.4 * SR), (int) (2.9 * SR)), cd = clicks (rd, (int) (0.4 * SR), (int) (2.9 * SR));
        const float refJ = std::max (cr.jump, cd.jump); const double refH = std::max (cr.hfWin, cd.hfWin);
        CHECK (cs.jump <= refJ + 0.05f && cs.hfWin <= refH + 3.0, "on -> LIMIT -> MULTIBAND -> 4:1 -> off while playing: step %+.4f, HF window %+.1f dB over the static renders", cs.jump - refJ, cs.hfWin - refH);
    }
    {
        const auto base = { std::pair<const char*, float> { "fx_comp_on", 1.0f }, { "fx_comp_thresh", -30.0f }, { "fx_comp_ratio", 6.0f } };
        knobZipper ("COMP", base, "fx_comp_thresh", -50.0f, 0.0f);
        knobZipper ("COMP", base, "fx_comp_ratio", 1.0f, 19.0f);
        knobZipper ("COMP", base, "fx_comp_makeup", 0.0f, 24.0f);
        knobZipper ("COMP", { { "fx_comp_on", 1.0f }, { "fx_comp_mode", 1.0f }, { "fx_comp_thresh", -30.0f } }, "fx_comp_xlo", 60.0f, 1500.0f);
        knobZipper ("COMP", { { "fx_comp_on", 1.0f }, { "fx_comp_mode", 1.0f }, { "fx_comp_thresh", -30.0f } }, "fx_comp_b2_up", 0.0f, 1.0f);
    }
    if (renders.getFullPathName().isNotEmpty())
    {
        auto q = chordProc ({ { "fx_comp_on", 1.0f }, { "fx_comp_mode", 1.0f }, { "fx_comp_thresh", -30.0f }, { "fx_comp_ratio", 4.0f }, { "fx_comp_b1_up", 0.5f }, { "fx_comp_b3_up", 0.6f }, { "fx_comp_makeup", 4.0f } });
        writeWav (renders.getChildFile ("comp_multiband_updown_8voice_saw.wav"), renderActs (*q, sawChord (2.5), 3.0, {}));
        auto l = chordProc ({ { "fx_comp_on", 1.0f }, { "fx_comp_ratio", kCompRatioMax }, { "fx_comp_thresh", -3.0f }, { "fx_comp_makeup", 12.0f } });
        writeWav (renders.getChildFile ("comp_limit_12db_8voice_saw.wav"), renderActs (*l, sawChord (2.5), 3.0, {}));
    }
}

// ---------------------------------------------------------------- 4. SPLITTER
// magnitude response (dB) of a unit's impulse response, 20 Hz .. 20 kHz: lowest and highest, and the IR's peak position
static void irFlat (const juce::AudioBuffer<float>& ir, int ch, double& lo, double& hi, int& peakAt)
{
    constexpr int ORDER = 16, N = 1 << ORDER; juce::dsp::FFT fft (ORDER); std::vector<float> buf ((size_t) N * 2, 0.0f);
    float pk = 0; peakAt = 0;
    for (int i = 0; i < N; ++i) { buf[(size_t) i] = ir.getSample (ch, i) * 1000.0f; if (std::abs (ir.getSample (ch, i)) > pk) { pk = std::abs (ir.getSample (ch, i)); peakAt = i; } }
    fft.performFrequencyOnlyForwardTransform (buf.data());
    lo = 100; hi = -100;
    for (int k = 1; k < N / 2; ++k) { const double f = k * SR / N; if (f < 20.0 || f > 20000.0) continue; const double db = 20 * std::log10 (buf[(size_t) k] + 1e-12); lo = std::min (lo, db); hi = std::max (hi, db); }
}
static void bandsTests (const juce::File& renders)
{
    section ("SPLITTER: flat with empty slots (0.05 dB), latency compensated between the bands, units only touch their band, slot changes click-free, state");
    auto p = makeProc (-1);
    auto& rk = *p->rack.impl;
    auto clearSlots = [&] { for (int b = 0; b < kBands; ++b) for (int k = 0; k < kSlots; ++k) rk.setBandSlot (b, k, -1); };
    auto impulse = [] (int i) { return i == 0 ? 0.001f : 0.0f; };
    double worst = 0;
    for (int m = 0; m < 3; ++m)
        for (auto xs : { std::pair<float, float> { 200.0f, 3000.0f }, { 90.0f, 8000.0f } })
        {
            cleantest::set (*p, { { "fx_bands_on", 1.0f }, { "fx_bands_mode", (float) m }, { "fx_bands_xlo", xs.first }, { "fx_bands_xhi", xs.second } });
            clearSlots();
            const auto ir = runUnit (*p, uBands, impulse, 65536, 4800);
            double lo, hi; int at; irFlat (ir, 0, lo, hi, at);
            printf ("    %-9s %5g / %5g Hz, empty slots: %+.4f .. %+.4f dB\n", bandsModeNames()[m].toRawUTF8(), xs.first, xs.second, lo, hi);
            worst = std::max ({ worst, std::abs (lo), std::abs (hi) });
        }
    CHECK (worst <= 0.05, "empty slots sum flat within %.4f dB (bar 0.05)", worst);
    {   // a COMPRESSOR (its 10 ms look-ahead, RATIO 1 = no gain change) in the LOW band only: the other bands wait for it
        cleantest::set (*p, { { "fx_bands_mode", 1.0f }, { "fx_bands_xlo", 200.0f }, { "fx_bands_xhi", 3000.0f }, { "fx_comp_ratio", 1.0f } });
        clearSlots(); rk.setBandSlot (0, 0, uComp);
        const auto ir = runUnit (*p, uBands, impulse, 65536, 4800);
        double lo, hi; int at; irFlat (ir, 0, lo, hi, at);
        const int lat = rk.units[uBands]->latency();
        CHECK (lat == (int) std::lround (0.010 * SR) + 12 && std::abs (lo) <= 0.05 && std::abs (hi) <= 0.05,
               "COMPRESSOR in the LOW band: SPLITTER reports %d samples, the three bands still sum flat (%+.4f .. %+.4f dB, peak at %d)", lat, lo, hi, at);
        clearSlots();
    }
    {   // DISTORTION in the HIGH band of L / H (1 kHz): a 100 Hz sine stays clean, a 5 kHz one is driven
        cleantest::set (*p, { { "fx_bands_mode", 0.0f }, { "fx_bands_xlo", 1000.0f }, { "fx_dist_mode", (float) dmHard }, { "fx_dist_drive", 30.0f } });
        clearSlots(); rk.setBandSlot (1, 0, uDist);
        const double fLo = 102.0 * SR / 65536.0 * 1.0, fHi = 6827.0 * SR / 65536.0;
        auto sine = [] (double f) { return [f] (int i) { return (float) (0.3 * std::sin (2.0 * juce::MathConstants<double>::pi * f * i / SR)); }; };
        const auto sLo = spectrum (runUnit (*p, uBands, sine (fLo * 1.37), 65536), fLo * 1.37), sHi = spectrum (runUnit (*p, uBands, sine (fHi), 65536), fHi);
        CHECK (sLo.thdDb < -60.0 && sHi.thdDb > -20.0, "DISTORTION in HIGH only: %.0f Hz THD %.1f dB (clean), %.0f Hz THD %.1f dB (driven)", fLo * 1.37, sLo.thdDb, fHi, sHi.thdDb);
        clearSlots();
    }
    {   // M / S: HYPER on the SIDE band of a mono input: the output stays mono (nothing on the side to widen)
        cleantest::set (*p, { { "fx_bands_mode", 2.0f } });
        clearSlots(); rk.setBandSlot (1, 0, uHyper); rk.setBandSlot (0, 0, uWreck);
        const auto out = runUnit (*p, uBands, [] (int i) { return (float) (0.3 * std::sin (i * 0.031) + 0.2 * std::sin (i * 0.0071)); }, 48000);
        float side = 0; for (int i = 0; i < out.getNumSamples(); ++i) side = std::max (side, std::abs (out.getSample (0, i) - out.getSample (1, i)));
        CHECK (side < 1.0e-6f, "M / S: WRECKER on MID, HYPER on SIDE, mono input: the output stays mono (max |L - R| %.2g)", side);
        clearSlots();
    }
    {   // slots changing while an 8-voice saw chord plays: crossfaded, no click; old instances freed
        auto kv = { std::pair<const char*, float> { "fx_bands_on", 1.0f }, { "fx_bands_mode", 1.0f }, { "fx_dist_drive", 12.0f } };
        const double len = 3.4;
        auto q = chordProc (kv);
        auto& qr = *q->rack.impl;
        const int seq[][3] { { uDist, -1, uChorus }, { uComp, uFatten, -1 }, { -1, uDist, uEcho }, { uTube, -1, uHyper }, { uDist, uWreck, uChorus }, { -1, -1, -1 } };
        int step = 0; double next = 0.5;
        const auto rs = renderActs (*q, sawChord (len), len, [&] (double t) { if (t >= next && step < 6) { for (int b = 0; b < 3; ++b) qr.setBandSlot (b, 0, seq[step][b]); ++step; next += 0.45; } });
        // the references: every configuration held still
        Clicks ref; ref.jump = 0; ref.hfWin = -200;
        for (int k = -1; k < 6; ++k)
        {
            auto st = chordProc (kv); auto& sr2 = *st->rack.impl;
            if (k >= 0) for (int b = 0; b < 3; ++b) sr2.setBandSlot (b, 0, seq[k][b]);
            const auto rr = renderActs (*st, sawChord (len), len, {});
            const auto c = clicks (rr, (int) (0.4 * SR), (int) ((len - 0.1) * SR));
            ref.jump = std::max (ref.jump, c.jump); ref.hfWin = std::max (ref.hfWin, c.hfWin);
        }
        const auto cs = clicks (rs, (int) (0.4 * SR), (int) ((len - 0.1) * SR));
        qr.slotGc(); juce::Thread::sleep (5); { juce::AudioBuffer<float> b (2, BS); juce::MidiBuffer m; for (int k = 0; k < 4; ++k) q->processBlock (b, m); } qr.slotGc();
        { juce::AudioBuffer<float> b (2, BS); juce::MidiBuffer m; for (int k = 0; k < 4; ++k) q->processBlock (b, m); } qr.slotGc();
        CHECK (step == 6 && cs.jump <= ref.jump + 0.05f && cs.hfWin <= ref.hfWin + 3.0, "6 slot changes while playing: step %+.4f, HF window %+.1f dB over the held configurations", cs.jump - ref.jump, cs.hfWin - ref.hfWin);
        CHECK (qr.slotPool.size() <= 3, "the replaced slot units are freed (%d instances left, 3 at most)", (int) qr.slotPool.size());
        if (renders.getFullPathName().isNotEmpty()) writeWav (renders.getChildFile ("splitter_slot_changes_8voice_saw.wav"), rs);
    }
    {   // state
        auto q = chordProc ({ { "fx_bands_on", 1.0f }, { "fx_bands_mode", 1.0f } });
        q->rack.impl->setBandSlot (0, 0, uDist); q->rack.impl->setBandSlot (1, 2, uEcho); q->rack.impl->setBandSlot (2, 1, uComp);
        juce::MemoryBlock mb; q->getStateInformation (mb);
        auto r = makeProc (-1); r->setStateInformation (mb.getData(), (int) mb.getSize()); settle (*r);
        auto& rr = *r->rack.impl;
        CHECK (rr.bandSlot (0, 0) == uDist && rr.bandSlot (1, 2) == uEcho && rr.bandSlot (2, 1) == uComp && rr.bandSlot (0, 1) == -1, "the band slots come back from the saved state");
        auto d = makeProc (-1);
        CHECK (! d->rack.saveState().getChildWithName ("BANDS").isValid(), "empty slots are not saved (older states stay byte-identical)");
        q->rack.impl->pushUndo(); q->rack.impl->setBandSlot (0, 0, uTube); q->rack.impl->undo();
        CHECK (q->rack.impl->bandSlot (0, 0) == uDist, "UNDO brings a slot change back");
    }
    {
        const auto base = { std::pair<const char*, float> { "fx_bands_on", 1.0f }, { "fx_bands_mode", 1.0f } };
        knobZipper ("SPLIT", base, "fx_bands_xlo", 60.0f, 1500.0f);
        knobZipper ("SPLIT", base, "fx_bands_b1_gain", -24.0f, 12.0f);
        knobZipper ("SPLIT", base, "fx_bands_mix", 0.0f, 1.0f);
    }
    if (renders.getFullPathName().isNotEmpty())
    {
        auto q = chordProc ({ { "fx_bands_on", 1.0f }, { "fx_bands_mode", 1.0f }, { "fx_dist_mode", (float) dmTube }, { "fx_dist_drive", 18.0f }, { "fx_chorus_depth", 0.6f } });
        q->rack.impl->setBandSlot (1, 0, uDist); q->rack.impl->setBandSlot (2, 0, uChorus); q->rack.impl->setBandSlot (0, 0, uComp);
        writeWav (renders.getChildFile ("splitter_lmh_comp_dist_chorus_8voice_saw.wav"), renderActs (*q, sawChord (2.5), 3.0, {}));
    }
}

// ---------------------------------------------------------------- 5. CHORUS upgrade
// ================================================================ CHORUS (3 voices per side)
class LegacyChorus : public UnitDsp   // (a verbatim copy of the CHORUS before round 3: the reference for bit-exactness)
{
public:
    Telemetry* tm = nullptr;
    void attach (juce::AudioProcessorValueTreeState& s) override
    {
        rate.bind (s, pid (uChorus, "rate"), 0.4f); depth.bind (s, pid (uChorus, "depth"), 0.5f); delay.bind (s, pid (uChorus, "delay"), 12);
        fb.bind (s, pid (uChorus, "fb")); width.bind (s, pid (uChorus, "width"), 1);
    }
    void prepare (double sr_, int) override
    {
        sr = sr_; for (auto* r : { &rl, &rr }) r->prepare ((int) (sr * 0.08));
        delSm.setTime (sr, 80.0f); depSm.setTime (sr, 40.0f); reset();
    }
    void reset() override { rl.clear(); rr.clear(); osc.setPhase (0); yl1 = yr1 = 0; delSm.snap (delay()); depSm.snap (depth()); ramp.start ((int) (sr * 0.01)); }
    void process (float* L, float* R, int n, const BlockCtx&) override
    {
        delSm.target = delay(); depSm.target = depth();
        if (snapNext) { delSm.snap (delSm.target); snapNext = false; }
        const float f = fb() * 0.85f, w = width();
        osc.setFreq (rate(), sr); osc.normalise();
        // voice offsets (120 degrees apart) and the right channel's extra offset (width * 90 degrees)
        double cl[3], sl[3], cr[3], sr2[3];
        for (int v = 0; v < 3; ++v)
        {
            const double a = juce::MathConstants<double>::twoPi * v / 3.0, b = a + juce::MathConstants<double>::halfPi * w;
            cl[v] = std::cos (a); sl[v] = std::sin (a); cr[v] = std::cos (b); sr2[v] = std::sin (b);
        }
        float pk = 0;
        for (int i = 0; i < n; ++i)
        {
            osc.step();
            const float dms = delSm.next(), dp = depSm.next();
            const float rg = ramp.next();
            rl.push ((L[i] + f * yl1) * rg); rr.push ((R[i] + f * yr1) * rg);
            float yl = 0, yr = 0;
            const double ms2s = 0.001 * sr;
            for (int v = 0; v < 3; ++v)
            {
                const float ml = (float) (osc.s * cl[v] + osc.c * sl[v]), mr = (float) (osc.s * cr[v] + osc.c * sr2[v]);
                yl += rl.read (std::max (0.5f, dms * (1.0f + 0.85f * dp * ml)) * ms2s);
                yr += rr.read (std::max (0.5f, dms * (1.0f + 0.85f * dp * mr)) * ms2s);
            }
            yl *= 0.4f; yr *= 0.4f; yl1 = yl; yr1 = yr;
            const float m = 0.5f * (yl + yr), s = 0.5f * (yl - yr) * (0.4f + 1.2f * w);
            L[i] = 0.5f * L[i] + (m + s); R[i] = 0.5f * R[i] + (m - s);
            pk = std::max (pk, std::abs (yl));
        }
        if (tm) tm->chorusLevel.store (pk);
    }
private:
    void jumpSmoothing() override { snapNext = true; }
    bool snapNext = false;
    P rate, depth, delay, fb, width; double sr = 48000; Ring rl, rr; Phasor osc; InRamp ramp; float yl1 = 0, yr1 = 0; ExpSmooth delSm, depSm;
};

static void chorusTests (const juce::File& renders)
{
    section ("CHORUS upgrade: defaults = the classic chorus bit for bit; 4 TAPS + DELAY 2 wider; wet HP / LP on the wet only; switching while playing click-free; no zipper");
    auto p = makeProc (-1);
    auto sig = [] (int i) { return (float) (0.3 * std::sin (i * 0.013) + 0.2 * std::sin (i * 0.0517 + 1.0) + 0.05 * std::sin (i * 0.31)); };
    {   // bit-exact against the old code, over a few settings
        float worst = 0;
        for (auto kv : { std::vector<std::pair<const char*, float>> {}, { { "fx_chorus_rate", 2.0f }, { "fx_chorus_depth", 0.9f }, { "fx_chorus_fb", 0.6f } }, { { "fx_chorus_delay", 25.0f }, { "fx_chorus_width", 0.3f } } })
        {
            for (auto& [k, v] : kv) p->setParam (k, v);
            LegacyChorus ref; ref.attach (p->apvts); ref.prepare (SR, kSub); ref.reset();
            auto& now = *p->rack.impl->units[uChorus]; now.reset();
            std::array<float, kSub> al {}, ar {}, bl {}, br {}; BlockCtx ctx; ctx.sr = SR;
            for (int pos = 0; pos < 96000; pos += kSub)
            {
                for (int i = 0; i < kSub; ++i) { al[(size_t) i] = bl[(size_t) i] = sig (pos + i); ar[(size_t) i] = br[(size_t) i] = sig (pos + i + 77); }
                ref.process (al.data(), ar.data(), kSub, ctx); now.process (bl.data(), br.data(), kSub, ctx);
                for (int i = 0; i < kSub; ++i) worst = std::max ({ worst, std::abs (al[(size_t) i] - bl[(size_t) i]), std::abs (ar[(size_t) i] - br[(size_t) i]) });
            }
        }
        CHECK (worst == 0.0f, "CHORUS with the new controls at their defaults = the classic chorus, bit for bit (max diff %.3g)", worst);
        cleantest::set (*p, { { "fx_chorus_rate", 0.4f }, { "fx_chorus_depth", 0.5f }, { "fx_chorus_fb", 0.0f }, { "fx_chorus_delay", 12.0f }, { "fx_chorus_width", 1.0f } });
    }
    {   // 4 TAPS: wider, similar level
        auto run = [&] (float taps) { cleantest::set (*p, { { "fx_chorus_taps", taps }, { "fx_chorus_delay2", 21.0f } }); return runUnit (*p, uChorus, sig, 48000, 9600); };
        const auto a = run (0.0f), b = run (1.0f);
        const double la = rmsDb (a), lb = rmsDb (b), wa = cleantest::sideRatioDb (a), wb = cleantest::sideRatioDb (b);
        int nans = 0; peakOf (b, &nans);
        CHECK (nans == 0 && std::abs (lb - la) < 3.0 && wb > wa, "4 TAPS: level %+.1f dB vs 3 VOICES, side / mid %.1f dB vs %.1f dB (wider)", lb - la, wb, wa);
        cleantest::set (*p, { { "fx_chorus_taps", 0.0f } });
    }
    {   // the wet HP: a 100 Hz tone keeps only the dry half below the HP
        auto tone = [] (int i) { return (float) (0.4 * std::sin (2.0 * juce::MathConstants<double>::pi * 100.0 * i / SR)); };
        cleantest::set (*p, { { "fx_chorus_hpf", 20.0f } }); const auto a = runUnit (*p, uChorus, tone, 48000, 24000);
        cleantest::set (*p, { { "fx_chorus_hpf", 1500.0f } }); const auto b = runUnit (*p, uChorus, tone, 48000, 24000);
        const double dry = 20 * std::log10 (0.5 * 0.4 / std::sqrt (2.0));
        CHECK (std::abs (rmsDb (b) - dry) < 1.0 && rmsDb (a) > dry + 2.0, "wet HP 1.5 kHz: a 100 Hz tone comes out at the dry half only (%.1f dB vs dry %.1f dB; without the HP %.1f dB)", rmsDb (b), dry, rmsDb (a));
        cleantest::set (*p, { { "fx_chorus_hpf", 20.0f } });
    }
    {   // switching 3 VOICES <-> 4 TAPS and the wet filters on / off while a chord plays
        auto kv = { std::pair<const char*, float> { "fx_chorus_on", 1.0f }, { "fx_chorus_mix", 0.6f }, { "fx_chorus_depth", 0.6f } };
        auto q = chordProc (kv);
        const auto rs = renderActs (*q, sawChord (3.0), 3.0, [&] (double t) {
            if (std::abs (t - 0.6) < 0.006) q->setParam ("fx_chorus_taps", 1.0f);
            if (std::abs (t - 1.1) < 0.006) q->setParam ("fx_chorus_lpf", 4000.0f);
            if (std::abs (t - 1.6) < 0.006) q->setParam ("fx_chorus_hpf", 600.0f);
            if (std::abs (t - 2.1) < 0.006) { q->setParam ("fx_chorus_taps", 0.0f); q->setParam ("fx_chorus_lpf", 20000.0f); q->setParam ("fx_chorus_hpf", 20.0f); } });
        Clicks ref; ref.jump = 0; ref.hfWin = -200;
        for (auto extra : { std::vector<std::pair<const char*, float>> {}, { { "fx_chorus_taps", 1.0f } }, { { "fx_chorus_taps", 1.0f }, { "fx_chorus_lpf", 4000.0f }, { "fx_chorus_hpf", 600.0f } } })
        {
            auto st = chordProc (kv); cleantest::set (*st, std::initializer_list<std::pair<const char*, float>> {}); for (auto& [k, v] : extra) st->setParam (k, v); st->rack.snapOnNextBlock();
            const auto c = clicks (renderActs (*st, sawChord (3.0), 3.0, {}), (int) (0.4 * SR), (int) (2.9 * SR));
            ref.jump = std::max (ref.jump, c.jump); ref.hfWin = std::max (ref.hfWin, c.hfWin);
        }
        const auto cs = clicks (rs, (int) (0.4 * SR), (int) (2.9 * SR));
        CHECK (cs.jump <= ref.jump + 0.05f && cs.hfWin <= ref.hfWin + 3.0, "TAPS / wet filters switched while playing: step %+.4f, HF window %+.1f dB over the held settings", cs.jump - ref.jump, cs.hfWin - ref.hfWin);
        if (renders.getFullPathName().isNotEmpty())
        {
            auto w = chordProc ({ { "fx_chorus_on", 1.0f }, { "fx_chorus_mix", 0.6f }, { "fx_chorus_taps", 1.0f }, { "fx_chorus_delay2", 22.0f }, { "fx_chorus_hpf", 250.0f }, { "fx_chorus_lpf", 9000.0f } });
            writeWav (renders.getChildFile ("chorus_4taps_wetfilters_8voice_saw.wav"), renderActs (*w, sawChord (2.5), 3.0, {}));
        }
    }
    {
        const auto base = { std::pair<const char*, float> { "fx_chorus_on", 1.0f }, { "fx_chorus_mix", 0.6f }, { "fx_chorus_taps", 1.0f } };
        knobZipper ("CHORUS", base, "fx_chorus_delay2", 3.0f, 28.0f);
        knobZipper ("CHORUS", base, "fx_chorus_lpf", 1500.0f, 19000.0f);
        knobZipper ("CHORUS", base, "fx_chorus_hpf", 25.0f, 1500.0f);
    }
}

// ---------------------------------------------------------------- 5b. TONE EQ
static void eq3Tests()
{
    section ("TONE EQ: flat = untouched, the bells' gains land within 0.1 dB at their centres, the measured curve follows the drawn one, no zipper");
    auto p = makeProc (-1);
    {
        const auto out = runUnit (*p, uEq3, [] (int i) { return (float) (0.4 * std::sin (i * 0.021) + 0.1 * std::sin (i * 0.77)); }, 24000, 9600);
        float md = 0; for (int i = 0; i < 24000; ++i) md = std::max (md, std::abs (out.getSample (0, i) - (float) (0.4 * std::sin (i * 0.021) + 0.1 * std::sin (i * 0.77))));
        CHECK (md == 0.0f, "every band at 0 dB, TILT 0: the signal passes untouched (max diff %.3g)", md);
    }
    double worstC = 0, worstCurve = 0;
    for (auto cfg : { std::array<float, 6> { 100.0f, 12.0f, 1000.0f, -9.0f, 5000.0f, 6.0f }, { 300.0f, -12.0f, 2500.0f, 10.0f, 9000.0f, -6.0f } })
    {
        cleantest::set (*p, { { "fx_eq3_b1_freq", cfg[0] }, { "fx_eq3_b1_gain", cfg[1] }, { "fx_eq3_b2_freq", cfg[2] }, { "fx_eq3_b2_gain", cfg[3] }, { "fx_eq3_b3_freq", cfg[4] }, { "fx_eq3_b3_gain", cfg[5] },
                              { "fx_eq3_b1_q", 4.0f }, { "fx_eq3_b2_q", 4.0f }, { "fx_eq3_b3_q", 4.0f }, { "fx_eq3_tilt", 0.0f } });
        const auto ir = runUnit (*p, uEq3, [] (int i) { return i == 0 ? 0.001f : 0.0f; }, 65536, 4800);
        constexpr int ORDER = 16, N = 1 << ORDER; juce::dsp::FFT fft (ORDER); std::vector<float> buf ((size_t) N * 2, 0.0f);
        for (int i = 0; i < N; ++i) buf[(size_t) i] = ir.getSample (0, i) * 1000.0f;
        fft.performFrequencyOnlyForwardTransform (buf.data());
        auto at = [&] (double f) { return 20 * std::log10 (buf[(size_t) std::lround (f * N / SR)] + 1e-12); };
        const float fr[3] { cfg[0], cfg[2], cfg[4] }, gd[3] { cfg[1], cfg[3], cfg[5] }, qs[3] { 4.0f, 4.0f, 4.0f };
        for (int k = 0; k < 3; ++k) worstC = std::max (worstC, std::abs (at (fr[k]) - Eq3Unit::responseDb (fr[k], fr, gd, qs, 0.0f)));
        for (double f = 30.0; f < 12000.0; f *= 1.1) worstCurve = std::max (worstCurve, std::abs (at (f) - Eq3Unit::responseDb (f, fr, gd, qs, 0.0f)));
    }
    CHECK (worstC <= 0.1 && worstCurve <= 0.5, "bell gains at their centres within %.3f dB of the drawn curve (bar 0.1); the whole curve 30 Hz .. 12 kHz within %.2f dB", worstC, worstCurve);
    knobZipper ("TONE", { { "fx_eq3_on", 1.0f }, { "fx_eq3_b2_freq", 800.0f } }, "fx_eq3_b2_gain", -18.0f, 18.0f);
    knobZipper ("TONE", { { "fx_eq3_on", 1.0f }, { "fx_eq3_b2_gain", 12.0f } }, "fx_eq3_b2_freq", 100.0f, 8000.0f);
    knobZipper ("TONE", { { "fx_eq3_on", 1.0f } }, "fx_eq3_tilt", -12.0f, 12.0f);
    {
        auto q = chordProc ({ { "fx_eq3_on", 1.0f }, { "fx_eq3_b1_gain", -6.0f }, { "fx_eq3_b1_freq", 250.0f }, { "fx_eq3_b2_gain", 4.0f }, { "fx_eq3_b2_freq", 2500.0f }, { "fx_eq3_tilt", 3.0f } });
        writeWav (juce::File ("/root/work/wt-pro-fx-renders").getChildFile ("toneeq_lessmud_presence_8voice_saw.wav"), renderActs (*q, sawChord (2.5), 3.0, {}));
    }
}

// ---------------------------------------------------------------- screenshots of the FX tab, every new page, all three looks
static void shotsOf (const juce::File& dir, std::initializer_list<std::pair<int, std::function<void (ProProcessor&)>>> pages, const char* tag)
{
    dir.createDirectory();
    static const char* themeName[] { "black", "purple", "light" };
    for (auto& [unit, setup] : pages)
    {
        auto p = makeProc (progOf ("Trap Starter Stack"));
        if (setup) setup (*p);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        auto* pe = dynamic_cast<ProEditor*> (ed.get());
        pe->setSize (ProEditor::W, ProEditor::H); pe->showTab (2); pump (200);
        gpfx::Panel* panel = nullptr;
        std::function<void (juce::Component&)> find = [&] (juce::Component& c) { for (auto* k : c.getChildren()) { if (auto* q = dynamic_cast<gpfx::Panel*> (k)) panel = q; else find (*k); } };
        find (*ed);
        if (panel == nullptr) { CHECK (false, "FX panel found"); return; }
        // play a chord so the meters move
        { juce::AudioBuffer<float> b (2, BS); juce::MidiBuffer m; for (int n : { 48, 55, 60, 64 }) m.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0); for (int k = 0; k < 40; ++k) { b.clear(); p->processBlock (b, m); m.clear(); } }
        for (int t = 0; t < 3; ++t)
        {
            pe->setTheme (t); pump (100);
            panel->getProperties().set ("select", unit); panel->resized(); pump (400);
            if (juce::String (tag) == "more")
            {   // open the MORE pop-down
                std::function<void (juce::Component&)> click = [&] (juce::Component& c) { for (auto* k : c.getChildren()) { if (auto* b = dynamic_cast<juce::TextButton*> (k); b != nullptr && b->getButtonText() == "MORE" && b->isVisible()) { b->setToggleState (true, juce::dontSendNotification); if (b->onClick) b->onClick(); } else click (*k); } };
                click (*panel); pump (200);
            }
            auto img = pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f);
            juce::File f = dir.getChildFile (juce::String ("pro-") + themeName[t] + "-fx-" + tag + "-" + unitInfo (unit).key + ".png"); f.deleteFile();
            if (auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream())) juce::PNGImageFormat().writeImageToStream (img, *os);
            std::set<juce::uint32> cols; for (int y = 0; y < img.getHeight(); y += 7) for (int x = 0; x < img.getWidth(); x += 7) cols.insert (img.getPixelAt (x, y).getARGB());
            CHECK (cols.size() > 40, "%s: %d colours", f.getFullPathName().toRawUTF8(), (int) cols.size());
        }
        pe->setTheme (0);
    }
}

int fxRackTests (const juce::File& renders, const juce::File& shots)
{
    juce::ignoreUnused (shots);
    const auto only = juce::String (std::getenv ("FXR3_ONLY") != nullptr ? std::getenv ("FXR3_ONLY") : "");
    if (only == "zipdbg") { knobZipper ("COMP", { { "fx_comp_on", 1.0f }, { "fx_comp_thresh", -30.0f }, { "fx_comp_ratio", 6.0f } }, "fx_comp_ratio", 1.0f, 19.0f); return failures; }
    if (only.isEmpty() || only.contains ("order")) reorderTests (renders);
    if (only.isEmpty() || only.contains ("dist")) distTests (renders);
    if (only.isEmpty() || only.contains ("comp")) compTests (renders);
    if (only.isEmpty() || only.contains ("bands")) bandsTests (renders);
    if (only.isEmpty() || only.contains ("chorus")) chorusTests (renders);
    if (only.isEmpty() || only.contains ("eq3")) eq3Tests();
    if (only.isEmpty() || only.contains ("shots"))
    {
        section ("SHOTS: the new FX pages in BLACK / PURPLE / LIGHT");
        shotsOf (shots, { { (int) uDist, [] (ProProcessor& p) { p.setParam ("fx_dist_on", 1.0f); p.setParam ("fx_dist_mode", (float) dmTube); p.setParam ("fx_dist_drive", 18.0f); p.setParam ("fx_dist_fpos", 2.0f); } } }, "a");
        shotsOf (shots, { { (int) uDist, [] (ProProcessor& p) { p.setParam ("fx_dist_on", 1.0f); p.setParam ("fx_dist_mode", (float) dmXShaper); p.setParam ("fx_dist_drive", 14.0f); } } }, "xshaper");
        shotsOf (shots, { { (int) uComp, [] (ProProcessor& p) { p.setParam ("fx_comp_on", 1.0f); p.setParam ("fx_comp_thresh", -30.0f); } } }, "a");
        shotsOf (shots, { { (int) uComp, [] (ProProcessor& p) { p.setParam ("fx_comp_on", 1.0f); p.setParam ("fx_comp_mode", 1.0f); p.setParam ("fx_comp_thresh", -30.0f); p.setParam ("fx_comp_b1_up", 0.5f); } } }, "multi");
        shotsOf (shots, { { (int) uBands, [] (ProProcessor& p) { p.setParam ("fx_bands_on", 1.0f); p.setParam ("fx_bands_mode", 1.0f); p.rack.impl->setBandSlot (0, 0, uComp); p.rack.impl->setBandSlot (1, 0, uDist); p.rack.impl->setBandSlot (1, 1, uChorus); p.rack.impl->setBandSlot (2, 0, uHyper); } } }, "a");
        shotsOf (shots, { { (int) uChorus, [] (ProProcessor& p) { p.setParam ("fx_chorus_on", 1.0f); } } }, "a");
        shotsOf (shots, { { (int) uEq3, [] (ProProcessor& p) { p.setParam ("fx_eq3_on", 1.0f); p.setParam ("fx_eq3_b1_gain", 4.0f); p.setParam ("fx_eq3_b2_gain", -5.0f); p.setParam ("fx_eq3_b3_gain", 6.0f); p.setParam ("fx_eq3_tilt", 2.0f); } } }, "a");
        shotsOf (shots, { { (int) uChorus, [] (ProProcessor& p) { p.setParam ("fx_chorus_on", 1.0f); p.setParam ("fx_chorus_taps", 1.0f); p.setParam ("fx_chorus_hpf", 250.0f); } } }, "more");
    }
    printf ("\n%d failures\n", failures);
    return failures;
}
} // namespace fxr3
