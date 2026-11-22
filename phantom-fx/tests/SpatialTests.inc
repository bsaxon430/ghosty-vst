// SPDX-License-Identifier: AGPL-3.0-or-later
// Phase 6b SPATIAL checks (SPATIAL.md 6): off / centred = bit-exact; left / right / above / below / near / far each move the image
// (ILD, ITD, elevation colour, level); automation records and plays back on the playhead, survives save / load, a new instance is empty;
// no clicks while dragging; the page fits at 1200 x 760 with nothing overlapping; SPATIAL's own CPU with 4, 8 and all tiles placed.
#include "../src/spatial/SteamVoice.h"
#include "../src/engines/Engines.h"
#include "../src/rhythm/NudgeLink.h"
namespace spatialtest
{
using namespace phantom;

struct TestHead : juce::AudioPlayHead
{
    double ppq = 0, bpm = 120; bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    { PositionInfo p; p.setBpm (bpm); p.setPpqPosition (ppq); p.setIsPlaying (playing); p.setTimeSignature (TimeSignature { 4, 4 }); return p; }
};
static void setP (PhantomProcessor& p, const juce::String& id, float value) { if (auto* q = dynamic_cast<juce::RangedAudioParameter*> (p.apvts.getParameter (id))) q->setValueNotifyingHost (q->convertTo0to1 (value)); }
static void place (PhantomProcessor& p, int u, float az, float el, float d) { setP (p, PhantomProcessor::spid (u, "azimuth"), az); setP (p, PhantomProcessor::spid (u, "elevation"), el); setP (p, PhantomProcessor::spid (u, "distance"), d); }
static const int kUnits[] { uEcho, uSpace, uFilter, uCrystal };   // FATTEN's Airwindows code seeds its dither from rand(): two instances never match
static juce::AudioBuffer<float> render (bool spatialOn, bool placed, int seconds = 2, int* latency = nullptr)
{
    PhantomProcessor p;
    for (int u : kUnits) setP (p, PhantomProcessor::pid (u, "on"), 1.0f);
    if (spatialOn) setP (p, "fx_spatial_on", 1.0f);
    if (placed) { place (p, uCrystal, 90, 0, 3); place (p, uEcho, -60, 40, 6); place (p, uSpace, 150, -30, 2); place (p, uFilter, 0, -60, 10); }
    p.prepareToPlay (48000.0, 256);
    if (latency) *latency = p.getLatencySamples();
    auto in = noise (48000 * seconds, 0.3f, 11); juce::MidiBuffer m;
    for (int i = 0; i + 256 <= in.getNumSamples(); i += 256) { juce::AudioBuffer<float> part (in.getArrayOfWritePointers(), 2, i, 256); p.processBlock (part, m); }
    return in;
}
static float bandDb (const juce::AudioBuffer<float>& b, int ch, double lo, double hi)
{   // energy in [lo, hi) Hz, one 16384-point FFT of the middle of the buffer
    const int order = 14, N = 1 << order; juce::dsp::FFT fft (order); std::vector<float> d ((size_t) N * 2, 0.0f);
    const int st = (b.getNumSamples() - N) / 2; for (int i = 0; i < N; ++i) d[(size_t) i] = b.getSample (ch, st + i) * (0.5f - 0.5f * std::cos (6.2831853f * i / N));
    fft.performFrequencyOnlyForwardTransform (d.data()); double e = 0;
    for (int k = (int) (lo * N / 48000.0); k < (int) (hi * N / 48000.0); ++k) e += d[(size_t) k] * (double) d[(size_t) k];
    return (float) (10.0 * std::log10 (e + 1e-20));
}
// Steam Audio alone: one voice, white noise in both wet channels, held at a place
static juce::AudioBuffer<float> voice (float az, float el, float dist, bool speakers = false, int frames = 1200)
{
    spatial::Renderer r; r.prepare (48000, 128, 1); auto in = noise (128 * frames, 0.3f, 5); in.copyFrom (1, 0, in, 0, 0, in.getNumSamples());
    juce::AudioBuffer<float> out (2, in.getNumSamples()); out.clear();
    const float a = juce::degreesToRadians (az), e = juce::degreesToRadians (el);
    for (int f = 0; f < frames; ++f) r.render (0, in.getReadPointer (0, f * 128), in.getReadPointer (1, f * 128), std::sin (a) * std::cos (e), std::sin (e), -std::cos (a) * std::cos (e), dist, speakers, out.getWritePointer (0, f * 128), out.getWritePointer (1, f * 128));
    return out;
}
static float rmsDb (const juce::AudioBuffer<float>& b, int ch) { return db (b.getRMSLevel (ch, b.getNumSamples() / 4, b.getNumSamples() / 2)); }
static int itdLag (const juce::AudioBuffer<float>& b)
{   // lag (samples) of the best L/R cross-correlation; > 0 = the right ear hears it first
    int best = 0; double bv = -1e30; const int st = b.getNumSamples() / 4, n = b.getNumSamples() / 2;
    for (int lag = -48; lag <= 48; ++lag) { double s = 0; for (int i = 0; i < n; ++i) s += b.getSample (0, st + i + lag) * (double) b.getSample (1, st + i); if (s > bv) { bv = s; best = lag; } }
    return best;
}

// SPATIAL at every common sample rate: at 88.2 / 96 / 176.4 / 192 kHz the renderer runs at 44.1 / 48 kHz behind half-band rate
// changers. At each rate: it is available; with every tile on the head the output is the SPATIAL-off output delayed by exactly the
// reported latency (bit for bit: the chain itself is never resampled); a tile at the right is louder in the right ear; a click
// through a placed unit arrives at the same moment (in milliseconds after the reported latency) as it does at 48 kHz
static void rateChecks()
{
    juce::StringArray lines; bool ok = true; double refMs = 0.0, refIld = 0.0, refTone = 0.0;
    for (double sr : { 48000.0, 44100.0, 88200.0, 96000.0, 176400.0, 192000.0 })
    {
        const int bs = 512, blocks = (int) (sr * 0.75 / bs);
        auto run = [&] (bool spatialOn, float az, float dist, bool click, int* latency, bool* active, int* stage)
        {
            PhantomProcessor p; setP (p, PhantomProcessor::pid (uFilter, "on"), 1.0f); if (spatialOn) setP (p, "fx_spatial_on", 1.0f); place (p, uFilter, az, 0, dist); p.prepareToPlay (sr, bs);
            if (latency) *latency = p.getLatencySamples(); if (active) *active = p.rack.spatial.ready() && p.rack.isSpatialActive(); if (stage) *stage = p.rack.spatial.latency();
            auto buf = noise (bs * blocks, 0.3f, 5); if (click) { buf.clear(); buf.setSample (0, (int) (sr * 0.25), 1.0f); buf.setSample (1, (int) (sr * 0.25), 1.0f); }
            juce::MidiBuffer m; for (int i = 0; i < blocks; ++i) { juce::AudioBuffer<float> part (buf.getArrayOfWritePointers(), 2, i * bs, bs); p.processBlock (part, m); }
            return buf;
        };
        int latOff = 0, latOn = 0, stage = 0; bool active = false;
        const auto off = run (false, 0, 0, false, &latOff, nullptr, nullptr), head = run (true, 0, 0, false, &latOn, &active, &stage);
        int bad = 0; const int d = latOn - latOff; for (int ch = 0; ch < 2; ++ch) for (int i = 0; i + d < off.getNumSamples(); ++i) if (off.getSample (ch, i) != head.getSample (ch, i + d)) ++bad;
        // the placed sound alone, straight through the SPATIAL stage: one unit's wet at the right (noise), then in front (a click)
        auto stageRun = [&] (float az, bool click, int& lat)
        {
            SpatialEngine eng; eng.prepare (sr, bs); eng.setTarget (uFilter, az, 0.0f, 1.0f); lat = eng.latency();
            auto src = noise (bs * blocks, 0.3f, 5); src.copyFrom (1, 0, src, 0, 0, src.getNumSamples()); if (click) { src.clear(); src.setSample (0, (int) (sr * 0.25), 1.0f); src.setSample (1, (int) (sr * 0.25), 1.0f); }
            juce::AudioBuffer<float> out (2, bs * blocks), chain (2, bs), dry (2, bs); out.clear();
            for (int i = 0; i < blocks; ++i)
            {
                chain.clear(); dry.clear(); for (int c = 0; c < 2; ++c) std::copy (src.getReadPointer (c, i * bs), src.getReadPointer (c, i * bs) + bs, eng.tapPtr (uFilter, c));
                eng.tap (uFilter, juce::dsp::AudioBlock<float> (chain), juce::dsp::AudioBlock<float> (dry), bs, 1.0f); eng.finish (juce::dsp::AudioBlock<float> (chain), bs);
                for (int c = 0; c < 2; ++c) out.copyFrom (c, i * bs, chain, c, 0, bs);
            }
            return out;
        };
        int latS = 0; const auto right = stageRun (90.0f, false, latS); const double ild = rmsDb (right, 1) - rmsDb (right, 0);
        const double k48 = 48000.0 / sr; const double tone = bandDb (right, 1, 3000 * k48, 8000 * k48) - bandDb (right, 1, 300 * k48, 1000 * k48);   // (bandDb counts its bins for 48 kHz)   // the right ear's colour: the same at every rate if the rate change is clean
        const auto ck = stageRun (0.0f, true, latS); int pk = 0; float pv = 0; for (int i = 0; i < ck.getNumSamples(); ++i) { const float v = std::abs (ck.getSample (0, i)); if (v > pv) { pv = v; pk = i; } }
        const double ms = (pk - (int) (sr * 0.25) - latS) * 1000.0 / sr; if (sr == 48000.0) { refMs = ms; refIld = ild; refTone = tone; }
        const bool good = active && finite (right) && d == stage && bad == 0 && ild > 6.0 && std::abs (ild - refIld) < 2.0 && std::abs (tone - refTone) < 1.5 && std::abs (ms - refMs) < 0.12 && pv > 0.01f; ok = ok && good;
        lines.add (juce::String (sr / 1000.0, 1) + " kHz: " + (active ? "on" : "NOT AVAILABLE") + ", latency +" + juce::String (d) + " samples (" + juce::String (stage) + " reported), on the head " + (bad == 0 ? "bit-exact" : juce::String (bad) + " samples differ")
                   + ", a tile at the right " + juce::String (ild, 1) + " dB louder in the right ear (colour " + juce::String (tone, 1) + " dB), click " + juce::String (ms, 2) + " ms after the reported latency" + (good ? "" : "  <-- WRONG"));
    }
    report (ok, "SPATIAL sample rates", lines.joinIntoString ("; "));
}

static void imageChecks()
{
    const auto R = voice (90, 0, 1), L = voice (-90, 0, 1);
    const float ildR = rmsDb (R, 1) - rmsDb (R, 0), ildL = rmsDb (L, 0) - rmsDb (L, 1); const int lagR = itdLag (R), lagL = itdLag (L);
    report (ildR > 6.0f && ildL > 6.0f && lagR > 10 && lagL < -10, "SPATIAL left / right (ILD, ITD)",
            "right: R louder by " + juce::String (ildR, 1) + " dB, R ahead by " + juce::String (lagR) + " samples; left: L louder by " + juce::String (ildL, 1) + " dB, lag " + juce::String (lagL) + " (need > 6 dB and > 10 samples)");
    const auto F = voice (0, 0, 1), B = voice (180, 0, 1);
    const float fb = (bandDb (F, 0, 2000, 6000) - bandDb (F, 0, 200, 1000)) - (bandDb (B, 0, 2000, 6000) - bandDb (B, 0, 200, 1000));
    report (std::abs (fb) > 2.0f, "SPATIAL front / back (pinna colour)", "2-6 kHz vs 0.2-1 kHz balance differs by " + juce::String (fb, 1) + " dB between front and back (need > 2)");
    const auto U = voice (0, 60, 1), D = voice (0, -45, 1);
    float worst = 0; juce::String at;
    for (double f0 : { 4000.0, 5000.0, 6000.0, 7000.0, 8000.0, 9000.0, 10000.0, 11000.0, 12000.0 })
    { const float dd = std::abs ((bandDb (U, 0, f0, f0 * 1.15) - bandDb (U, 0, 300, 1000)) - (bandDb (D, 0, f0, f0 * 1.15) - bandDb (D, 0, 300, 1000))); if (dd > worst) { worst = dd; at = juce::String (f0 / 1000.0, 0) + " kHz"; } }
    report (worst > 4.0f, "SPATIAL above / below (elevation notches)", "biggest above-vs-below difference " + juce::String (worst, 1) + " dB in the band at " + at + " (need > 4)");
    const auto N1 = voice (30, 0, 1), N8 = voice (30, 0, 8);
    const float lvl = rmsDb (N1, 0) - rmsDb (N8, 0), tilt = (bandDb (N1, 0, 8000, 16000) - bandDb (N1, 0, 300, 1000)) - (bandDb (N8, 0, 8000, 16000) - bandDb (N8, 0, 300, 1000));
    report (lvl > 15.0f && lvl < 21.0f && tilt > 0.3f, "SPATIAL near / far (level, air)", "1 m vs 8 m: " + juce::String (lvl, 1) + " dB louder (1/r = 18.1), highs " + juce::String (tilt, 2) + " dB brighter near (air absorption)");
    const auto S = voice (90, 0, 1, true), SL = voice (-90, 0, 1, true);
    const float sr = rmsDb (S, 1) - rmsDb (S, 0), sl = rmsDb (SL, 0) - rmsDb (SL, 1);
    report (sr > 6.0f && sl > 6.0f && std::abs (itdLag (S)) <= 1, "SPATIAL speaker mode", "right: R louder by " + juce::String (sr, 1) + " dB; left: L by " + juce::String (sl, 1) + " dB; no ear delay (lag " + juce::String (itdLag (S)) + ")");
    // through the whole plug-in: a unit placed right moves the output right; the dry stays centre
    {
        PhantomProcessor p; setP (p, PhantomProcessor::pid (uFatten, "on"), 1.0f); setP (p, "fx_spatial_on", 1.0f); place (p, uFatten, 90, 0, 2);
        p.prepareToPlay (48000.0, 256); auto in = noise (48000 * 2, 0.3f, 3); in.copyFrom (1, 0, in, 0, 0, in.getNumSamples()); juce::MidiBuffer m;
        for (int i = 0; i + 256 <= in.getNumSamples(); i += 256) { juce::AudioBuffer<float> part (in.getArrayOfWritePointers(), 2, i, 256); p.processBlock (part, m); }
        const float d = rmsDb (in, 1) - rmsDb (in, 0);
        report (d > 1.0f && p.rack.spatial.voicesLastFrame() == 1, "SPATIAL in the rack (FATTEN placed right)", "output right louder by " + juce::String (d, 2) + " dB (dry centre + wet on the right); voices running: " + juce::String (p.rack.spatial.voicesLastFrame()));
    }
}

static void bitExactChecks()
{
    int latOff = 0, latOn = 0;
    const auto a = render (false, false, 2, &latOff), b = render (false, true), c = render (true, false, 2, &latOn), a2 = render (false, false);
    { int diff = 0, first = -1; for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < a.getNumSamples(); ++i) if (a.getSample (ch, i) != a2.getSample (ch, i)) { ++diff; if (first < 0 || i < first) first = i; } std::printf ("(same rack twice: %d samples differ, first at %d)\n", diff, first); }
    bool ab = true; for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < a.getNumSamples(); ++i) if (a.getSample (ch, i) != b.getSample (ch, i)) ab = false;
    report (ab, "SPATIAL off = 0.1.7 (bit-exact)", juce::String ("4 units on, every tile placed but SPATIAL off: ") + (ab ? "identical to the last sample" : "DIFFERENT"));
    bool ac = latOn - latOff == SpatialEngine::kFrame; int bad = 0;
    for (int ch = 0; ch < 2; ++ch) for (int i = 0; i + SpatialEngine::kFrame < a.getNumSamples(); ++i) if (a.getSample (ch, i) != c.getSample (ch, i + SpatialEngine::kFrame)) ++bad;
    report (ac && bad == 0, "SPATIAL on, all tiles on the head (bit-exact)", "latency " + juce::String (latOff) + " -> " + juce::String (latOn) + " samples (+" + juce::String (SpatialEngine::kFrame) + ", reported to the host); after it: " + juce::String (bad) + " samples differ");
    const auto d = render (true, true);
    report (finite (d) && peakOf (d) < 4.0f, "SPATIAL on, 4 tiles placed", "output finite, peak " + juce::String (db (peakOf (d)), 1) + " dBFS");
}

static void automationChecks()
{
    TestHead head; const int bs = 256; const double beatsPerBlock = bs / (48000.0 * 60.0 / head.bpm);
    auto run = [&] (PhantomProcessor& p, double fromPpq, int blocks, std::function<void (int, double)> each)
    {
        juce::AudioBuffer<float> buf (2, bs); juce::MidiBuffer m; head.ppq = fromPpq;
        for (int k = 0; k < blocks; ++k) { if (each) each (k, head.ppq); buf.clear(); p.processBlock (buf, m); head.ppq += beatsPerBlock; }
        p.drainSpatial();
    };
    auto azAt = [] (double ppq) { return (float) (-150.0 + 300.0 * std::fmod (ppq, 16.0) / 16.0); };   // a 4-bar sweep
    PhantomProcessor p; p.setPlayHead (&head); setP (p, PhantomProcessor::pid (uEcho, "on"), 1.0f); setP (p, "fx_spatial_on", 1.0f); p.prepareToPlay (48000.0, bs);
    p.spatialAuto.mode = spWrite; p.spatialAuto.armed[uEcho] = true;
    const int blocks = (int) (16.0 / beatsPerBlock);
    run (p, 0.0, blocks, [&] (int, double ppq) { place (p, uEcho, azAt (ppq), 20.0f, 4.0f); });
    p.spatialAuto.mode = spRead; place (p, uEcho, 0, 0, 1);
    float worst = 0; int driven = 0;
    run (p, 0.0, blocks - 2, [&] (int k, double ppq) { if (k == 0) return; const float got = p.spatialView[uEcho].az.load(); juce::ignoreUnused (ppq); if (p.spatialView[uEcho].driven.load()) ++driven; worst = juce::jmax (worst, std::abs (got - azAt (ppq - beatsPerBlock))); });
    const auto path = p.spatialAuto.copyPath (uEcho);
    report (worst < 0.5f && driven >= blocks - 4 && path.size() >= (size_t) blocks - 2, "SPATIAL automation: write then read", juce::String ((int) path.size()) + " points over 4 bars; playback follows the recording to within " + juce::String (worst, 3) + " deg on " + juce::String (driven) + " blocks");
    // touch: re-record bar 2 only, while holding the tile; bars 1, 3, 4 are kept
    p.spatialAuto.mode = spTouch;
    run (p, 4.0, (int) (4.0 / beatsPerBlock), [&] (int, double) { p.spatialAuto.touching[uEcho] = true; place (p, uEcho, 100.0f, 0.0f, 2.0f); });
    p.spatialAuto.touching[uEcho] = false; run (p, 8.0, 2, nullptr);
    { SpPoint q1, q2, q3; const juce::SpinLock::ScopedLockType sl (p.spatialAuto.lock);
    const bool l1 = p.spatialAuto.lookup (uEcho, 2.0, 4.0, q1), l2 = p.spatialAuto.lookup (uEcho, 6.0, 4.0, q2), l3 = p.spatialAuto.lookup (uEcho, 12.0, 4.0, q3);
    report (l1 && l2 && l3 && std::abs (q1.az - azAt (2.0)) < 0.5f && std::abs (q2.az - 100.0f) < 0.01f && std::abs (q3.az - azAt (12.0)) < 0.5f, "SPATIAL automation: touch replaces only its section",
            "bar 1 " + juce::String (q1.az, 1) + " (was " + juce::String (azAt (2.0), 1) + "), bar 2 " + juce::String (q2.az, 1) + " (touched 100), bar 4 " + juce::String (q3.az, 1) + " (was " + juce::String (azAt (12.0), 1) + ")");
    // quantise and loop
    p.spatialAuto.quant[uEcho] = 3; SpPoint qq; p.spatialAuto.lookup (uEcho, 12.7, 4.0, qq); const bool qok = std::abs (qq.az - azAt (12.0)) < 0.5f; p.spatialAuto.quant[uEcho] = 0;
    p.spatialAuto.loop[uEcho] = 1; SpPoint lp; p.spatialAuto.lookup (uEcho, 18.0, 4.0, lp); const bool lok = std::abs (lp.az - azAt (2.0)) < 0.5f; p.spatialAuto.loop[uEcho] = 0;
    report (qok && lok, "SPATIAL automation: quantise / loop", "1/4 quantise holds beat 12 at 12.7 (" + juce::String (qq.az, 1) + "); 1-bar loop plays bar 1 again at beat 18 (" + juce::String (lp.az, 1) + ")");
    }
}

static void automationStateChecks()
{
    PhantomProcessor a; std::vector<SpPoint> v; for (int i = 0; i < 500; ++i) v.push_back ({ i * 0.03125, -120.0f + i * 0.4f, (float) (i % 90) - 45.0f, 0.5f + i * 0.01f, i == 0 || i == 250 });
    a.spatialAuto.setPath (uCrystal, v); a.spatialAuto.armed[uCrystal] = true; a.spatialAuto.quant[uCrystal] = 2; a.spatialAuto.loop[uCrystal] = 3; a.spatialAuto.mode = spTouch;
    juce::MemoryBlock mb; a.getStateInformation (mb);
    PhantomProcessor b; b.setStateInformation (mb.getData(), (int) mb.getSize());
    const auto w = b.spatialAuto.copyPath (uCrystal); bool same = w.size() == v.size();
    for (size_t i = 0; same && i < v.size(); ++i) same = w[i].ppq == v[i].ppq && w[i].az == v[i].az && w[i].el == v[i].el && w[i].dist == v[i].dist && w[i].segStart == v[i].segStart;
    same = same && b.spatialAuto.armed[uCrystal].load() && b.spatialAuto.quant[uCrystal].load() == 2 && b.spatialAuto.loop[uCrystal].load() == 3 && b.spatialAuto.mode.load() == spTouch;
    juce::MemoryBlock mb2; b.getStateInformation (mb2);
    PhantomProcessor fresh; bool empty = true; for (int u = 0; u < kNumUnits; ++u) empty = empty && ! fresh.spatialAuto.hasPath (u) && ! fresh.spatialAuto.armed[(size_t) u].load();
    report (same && mb == mb2 && empty && fresh.spatialAuto.mode.load() == spRead, "SPATIAL automation: save / load, fresh start",
            juce::String ((int) w.size()) + " points + tile settings restored exactly: " + (same ? "yes" : "NO") + "; saved bytes identical after reload: " + (mb == mb2 ? "yes" : "NO") + "; a new instance has no paths, nothing armed: " + (empty ? "yes" : "NO"));
}

static bool verboseClicks = false;
static void clickChecks()
{
    // A 300 Hz tone through FILTER (its wet is the tone), placed 2 m away, while the tile is dragged the way the page drags it: a new
    // place 30 times a second, 6 degrees round each time (180 degrees a second, a fast drag) and up / down. A click is one step that
    // stands out: the 4th difference of the output (it ignores the tone and the smooth movement), worst 128-sample stretch against the
    // typical (median) moving stretch. A smooth move stays within a few dB; a click stands 20 dB or more above it.
    PhantomProcessor p; setP (p, PhantomProcessor::pid (uFilter, "on"), 1.0f); setP (p, "fx_spatial_on", 1.0f); place (p, uFilter, 0.0f, 0.0f, 2.0f);
    p.prepareToPlay (48000.0, 256); const int blocks = 48000 * 6 / 256; auto tone = sine (48000.0, blocks * 256, 300.0f, 0.5f); juce::MidiBuffer m;
    for (int b = 0; b < blocks; ++b)
    {
        if (b % 6 == 0 && b > 180) { const float k = (float) (b / 6 - 30); place (p, uFilter, std::remainder (6.0f * k, 360.0f), 35.0f * std::sin (k * 0.05f), 2.0f + std::sin (k * 0.03f)); }
        juce::AudioBuffer<float> part (tone.getArrayOfWritePointers(), 2, b * 256, 256); p.processBlock (part, m);
    }
    // what a click or zipper adds: energy far above the tone. Every 1024-sample window (Hann, hop 256), both ears, energy above 3 kHz
    // (the tone and its movement sidebands sit at 300 Hz), in dBFS; the worst window counts.
    const int order = 10, N = 1 << order; juce::dsp::FFT fft (order); std::vector<float> d ((size_t) N * 2); float worst = -300.0f; int worstAt = 0;
    for (int c = 0; c < 2; ++c) for (int st = 48000; st + N < tone.getNumSamples(); st += 256)
    {
        std::fill (d.begin(), d.end(), 0.0f); for (int i = 0; i < N; ++i) d[(size_t) i] = tone.getSample (c, st + i) * (0.5f - 0.5f * std::cos (6.2831853f * i / N));
        fft.performFrequencyOnlyForwardTransform (d.data()); double e = 0; for (int k = 3000 * N / 48000; k < N / 2; ++k) e += d[(size_t) k] * (double) d[(size_t) k];
        const float lv = (float) (10.0 * std::log10 (e * 2.0 / ((double) N * N * 0.375) + 1e-30));   // Hann-normalised power (dBFS for a full-scale sine = 0 - 3)
        if (lv > worst) { worst = lv; worstAt = st; }
    }
    if (verboseClicks) std::printf ("worst at %.2f s\n", worstAt / 48000.0);
    report (worst < -70.0f && finite (tone), "SPATIAL no clicks while dragging", "a fast drag (180 degrees a second, up / down, near / far) on a 300 Hz tone at -6 dBFS: worst energy above 3 kHz " + juce::String (worst, 1) + " dBFS in any 21 ms (limit -70; a small click, a 1 ms step of -34 dBFS, reads -58)");
}

static void cpuChecks()
{
    // SPATIAL's own cost: every unit on at its defaults (the whole-rack case), SPATIAL on, 4 / 8 / all 22 tiles placed, minus the same rack with SPATIAL off
    auto measure = [] (int placedCount, bool on) {
        PhantomProcessor p; for (int u = 0; u < kNumUnits; ++u) setP (p, PhantomProcessor::pid (u, "on"), 1.0f);
        if (on) setP (p, "fx_spatial_on", 1.0f);
        int k = 0; for (int u = 0; u < kNumUnits && k < placedCount; ++u) if (spatialEligible (u)) { place (p, u, -170.0f + 340.0f * k / 22.0f, (float) ((k * 37) % 120) - 60.0f, 3.0f); ++k; }
        p.prepareToPlay (48000.0, 256); auto buf = noise (256, 0.5f); juce::MidiBuffer m; for (int i = 0; i < 200; ++i) p.processBlock (buf, m);
        const int blocks = 3000; const auto t0 = std::chrono::steady_clock::now(); for (int i = 0; i < blocks; ++i) { buf = noise (256, 0.5f, (unsigned) i); p.processBlock (buf, m); }
        return 100.0 * std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count() / (blocks * 256.0 / 48000.0);
    };
    const double base = measure (0, false), on0 = measure (0, true), c4 = measure (4, true), c8 = measure (8, true), c22 = measure (22, true);
    report (true, "SPATIAL CPU (informational)", "all 24 units on: SPATIAL off " + juce::String (base, 2) + " %, on with nothing placed " + juce::String (on0, 2) + " %, 4 placed " + juce::String (c4, 2) + " %, 8 placed " + juce::String (c8, 2)
            + " %, all 22 placed " + juce::String (c22, 2) + " % of one core (SPATIAL's own: " + juce::String (c4 - base, 2) + " / " + juce::String (c8 - base, 2) + " / " + juce::String (c22 - base, 2) + ")");
}

static void layoutChecks()
{
    if (juce::Desktop::getInstance().getDisplays().displays.isEmpty()) { report (true, "SPATIAL page layout", "no display: skipped"); return; }
    PhantomProcessor p; p.prepareToPlay (48000.0, 256); for (int u : kUnits) setP (p, PhantomProcessor::pid (u, "on"), 1.0f);
    std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditor()); e->setBounds (0, 0, PhantomProcessor::kDefaultWidth, PhantomProcessor::kDefaultHeight);
    auto* pe = dynamic_cast<PhantomEditor*> (e.get()); pe->showSpatial (uEcho); juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    juce::Component* page = nullptr; std::function<void (juce::Component*)> find = [&] (juce::Component* c) { if (c->getName() == "SPATIAL PAGE") page = c; for (auto* k : c->getChildren()) find (k); }; find (e.get());
    auto rectProp = [&] (const char* k) { auto t = juce::StringArray::fromTokens (page->getProperties()[k].toString(), " ", ""); return juce::Rectangle<int> (t[0].getIntValue(), t[1].getIntValue(), t[2].getIntValue(), t[3].getIntValue()); };
    juce::StringArray bad; std::vector<juce::Rectangle<int>> rs; std::vector<juce::String> names;
    if (page)
    {
        for (auto* c : page->getChildren()) if (c->isVisible()) { rs.push_back (c->getBounds()); names.push_back (c->getName().isEmpty() ? juce::String (typeid (*c).name()) : c->getName()); if (! page->getLocalBounds().contains (c->getBounds())) bad.add ("outside"); }
        for (auto k : { "topView", "sideView", "dock", "readout" }) { rs.push_back (rectProp (k)); names.push_back (k); }
        for (size_t i = 0; i < rs.size(); ++i) for (size_t j = i + 1; j < rs.size(); ++j) if (rs[i].intersects (rs[j])) bad.add (names[i] + " x " + names[j]);
    }
    report (page != nullptr && page->isVisible() && bad.isEmpty() && rectProp ("topView").getWidth() >= 300, "SPATIAL page layout at 1200 x 760",
            page ? "views " + juce::String (rectProp ("topView").getWidth()) + " px, " + juce::String ((int) rs.size()) + " parts, overlaps / outside: " + (bad.isEmpty() ? juce::String ("none") : bad.joinIntoString (", ")) : juce::String ("page not found"));
}

static int twice (std::vector<int> units, int seconds)
{
    juce::AudioBuffer<float> r[2];
    for (int k = 0; k < 2; ++k) { PhantomProcessor p; for (int u : units) setP (p, PhantomProcessor::pid (u, "on"), 1.0f); p.prepareToPlay (48000.0, 256); r[k] = noise (48000 * seconds, 0.3f, 11); juce::MidiBuffer m;
        for (int i = 0; i + 256 <= r[k].getNumSamples(); i += 256) { juce::AudioBuffer<float> part (r[k].getArrayOfWritePointers(), 2, i, 256); p.processBlock (part, m); } }
    int first = -1; for (int c = 0; c < 2; ++c) for (int i = 0; i < r[0].getNumSamples(); ++i) if (r[0].getSample (c, i) != r[1].getSample (c, i)) { if (first < 0 || i < first) first = i; break; }
    return first;
}
static void debug (int firstUnit = -1)
{
    if (firstUnit == 99) { verboseClicks = true; clickChecks(); return; }
    if (firstUnit == 97) { auto in = noise (128 * 1200, 0.3f, 5); for (float az : { 0.0f, 90.0f, 180.0f }) { auto o = voice (az, 0, 1); std::printf ("az %.0f: in %.1f dB, out L %.1f R %.1f dB\n", az, rmsDb (in, 0), rmsDb (o, 0), rmsDb (o, 1)); } return; }
    if (firstUnit == 98)
    {   // reference: a tone with a real click in it (a 0.02 step, then back 1 ms later) through the same detector
        auto x = sine (48000.0, 96000, 300.0f, 0.5f); for (int i = 60000; i < 60048; ++i) { x.setSample (0, i, x.getSample (0, i) + 0.02f); }
        const int order = 10, N = 1 << order; juce::dsp::FFT fft (order); std::vector<float> d ((size_t) N * 2); float worst = -300.0f;
        for (int st = 48000; st + N < 96000; st += 256) { std::fill (d.begin(), d.end(), 0.0f); for (int i = 0; i < N; ++i) d[(size_t) i] = x.getSample (0, st + i) * (0.5f - 0.5f * std::cos (6.2831853f * i / N));
            fft.performFrequencyOnlyForwardTransform (d.data()); double e = 0; for (int k = 3000 * N / 48000; k < N / 2; ++k) e += d[(size_t) k] * (double) d[(size_t) k]; worst = juce::jmax (worst, (float) (10.0 * std::log10 (e * 2.0 / ((double) N * N * 0.375) + 1e-30))); }
        std::printf ("a 0.02 (-34 dBFS) 1 ms click reads %.1f dBFS\n", worst); return;
    }
    if (firstUnit >= 0) { std::printf ("first-instance %s: %d\n", unitDef (firstUnit).key, twice ({ firstUnit }, 2)); return; }
    // clicks: one parameter moving at a time
    auto tone = sine (48000.0, 128 * 1500, 300.0f, 0.5f);
    auto d4 = [] (const juce::AudioBuffer<float>& b) { float m = 0; int at = 0; for (int c = 0; c < 2; ++c) for (int i = 48000; i < b.getNumSamples() - 2; ++i) { const float* x = b.getReadPointer (c); const float v = std::abs (x[i + 2] - 4 * x[i + 1] + 6 * x[i] - 4 * x[i - 1] + x[i - 2]); if (v > m) { m = v; at = i; } } std::printf (" (at %d, %%128=%d)", at, at % 128); return m; };
    for (int mode = 0; mode < 4; ++mode)
    {
        spatial::Renderer r; r.prepare (48000, 128, 1); juce::AudioBuffer<float> out (2, tone.getNumSamples()); out.clear();
        for (int f = 0; f < 1500; ++f) { const float t = f * 128 / 48000.0f; float a = 0.5f, e = 0, d = 2;
            if (mode == 1) a = 3.14159f * t; if (mode == 2) e = juce::degreesToRadians (40.0f * std::sin (1.3f * t)); if (mode == 3) d = 1.0f + 5.0f * (0.5f - 0.5f * std::cos (t));
            r.render (0, tone.getReadPointer (0, f * 128), tone.getReadPointer (1, f * 128), std::sin (a) * std::cos (e), std::sin (e), -std::cos (a) * std::cos (e), d, false, out.getWritePointer (0, f * 128), out.getWritePointer (1, f * 128)); }
        std::printf ("mode %d:", mode); std::printf (" d4 %.1f dB\n", db (d4 (out)));
        if (mode == 1) { std::vector<float> fm; for (int f = 400; f < 1500; ++f) { float m = 0; for (int i = 0; i < 128; ++i) { const float* x = out.getReadPointer (0); const int k = f * 128 + i; m = juce::jmax (m, std::abs (x[k + 2] - 4 * x[k + 1] + 6 * x[k] - 4 * x[k - 1] + x[k - 2])); } fm.push_back (m); }
            auto sorted = fm; std::sort (sorted.begin(), sorted.end()); std::printf ("per-frame d4: median %.1f, 90%% %.1f, 99%% %.1f, max %.1f dB\n", db (sorted[sorted.size() / 2]), db (sorted[sorted.size() * 9 / 10]), db (sorted[sorted.size() * 99 / 100]), db (sorted.back()));
            const float* x = out.getReadPointer (0); for (int k = 174265; k < 174285; ++k) std::printf ("%.5f ", x[k]); std::printf ("\n"); }
    }
}

// demo: beat + melody, ECHO / HALLS / WRECKER / CRYSTAL on; SPATIAL off, then Brandon's example (reverb below, delay above, distortion right,
// crystal left), then the same with the delay's tile recorded circling the head (WRITE pass, then READ)
static int demo (const juce::File& dir)
{
    dir.createDirectory(); const double sr = 48000.0; const int n = (int) (sr * 16), bs = 256;
    auto src = mastertest::material (0, sr, n); { auto mel = mastertest::material (1, sr, n); for (int c = 0; c < 2; ++c) src.addFrom (c, 0, mel, c, 0, n, 0.7f); }
    src.applyGain (0.5f / juce::jmax (1.0e-6f, peakOf (src)));
    auto go = [&] (const char* name, int variant)
    {
        PhantomProcessor p; TestHead head; head.bpm = 100; p.setPlayHead (&head);
        for (int u : { uEcho, uHalls, uWreck, uCrystal }) setP (p, PhantomProcessor::pid (u, "on"), 1.0f);
        if (variant > 0) { setP (p, "fx_spatial_on", 1.0f); place (p, uHalls, 0, -60, 5); place (p, uEcho, 0, 75, 5); place (p, uWreck, 90, 0, 2); place (p, uCrystal, -90, 10, 3); }
        p.prepareToPlay (sr, bs); juce::MidiBuffer m; const double bpb = bs / (sr * 60.0 / head.bpm);
        if (variant == 2)
        {   // record the delay going round the head once every 2 bars, then play it back
            p.spatialAuto.mode = spWrite; p.spatialAuto.armed[uEcho] = true; juce::AudioBuffer<float> silent (2, bs); head.ppq = 0;
            for (int i = 0; i + bs <= n; i += bs) { place (p, uEcho, std::remainder ((float) (head.ppq / 8.0 * 360.0), 360.0f), 30.0f, 4.0f); silent.clear(); p.processBlock (silent, m); head.ppq += bpb; }
            p.drainSpatial(); p.spatialAuto.mode = spRead; p.spatialAuto.armed[uEcho] = false; p.reset();
        }
        auto out = src; head.ppq = 0;
        for (int i = 0; i + bs <= n; i += bs) { juce::AudioBuffer<float> part (out.getArrayOfWritePointers(), 2, i, bs); p.processBlock (part, m); head.ppq += bpb; }
        mastertest::writeWav (dir.getChildFile (name), out);
    };
    go ("1-spatial-off.wav", 0); go ("2-spatial-placed.wav", 1); go ("3-spatial-delay-circling.wav", 2);
    return 0;
}


// an ENGINE choice must apply at once: picking one CURVES set / CORRUPTOR voice after another (no other switch in between) must play it,
// not the dry signal (0.1.24: the update ran before the audio thread had the new choice, so a set picked from CURVES played dry)
static void engineSwitchChecks()
{
    struct Case { int unit; const char* engine; const char* drive; };
    for (const Case cs : { Case { uCrackle, "CURVES", "b1_drive" }, Case { uWreck, "CURVES", "drive" }, Case { uWreck, "CORRUPTOR", "drive" } })
    {
        const auto labels = engines::engineLabels (cs.unit); std::vector<int> picks;
        for (int i = 0; i < (int) labels.size() && picks.size() < 4; ++i) if (juce::String (labels[(size_t) i]).startsWith (juce::String (cs.engine) + " ")) picks.push_back (i);
        PhantomProcessor p; setP (p, PhantomProcessor::pid (cs.unit, "on"), 1.0f); setP (p, PhantomProcessor::pid (cs.unit, cs.drive), 1.0f);
        p.prepareToPlay (48000.0, 256); juce::MidiBuffer m; juce::String nums; int dry = 0;
        for (int k : picks)
        {
            setP (p, PhantomProcessor::pid (cs.unit, "engine"), (float) k); p.flushUpdates();
            auto in = sine (48000.0, 48000, 200.0f, 0.4f); const double f0 = bandDb (in, 0, 150, 250);
            for (int i = 0; i + 256 <= in.getNumSamples(); i += 256) { juce::AudioBuffer<float> part (in.getArrayOfWritePointers(), 2, i, 256); p.processBlock (part, m); }
            // dry would be the tone alone: no harmonics and the tone at its own level (a voice that only removes, SPECTRAL DROPOUT, moves the tone)
            const double h = bandDb (in, 0, 300, 20000) - bandDb (in, 0, 150, 250), moved = std::abs (bandDb (in, 0, 150, 250) - f0);
            if (h < -60.0 && moved < 1.0) ++dry; nums << labels[(size_t) k] << " " << juce::String (h, 1) << " / " << juce::String (moved, 1) << " dB; ";
        }
        report (! picks.empty() && dry == 0, juce::String ("rack ENGINE choice applies at once: ") + unitDef (cs.unit).title + " " + cs.engine,
                "harmonics vs the 200 Hz tone / the tone moved, after each pick: " + nums + "played dry: " + juce::String (dry));
    }
}

// HAUNT EDIT's FOLLOW GHOST BREAK (0.1.26): a stand-in Ghost Break in this process sends BAY A's move at every step; each click of the
// input must come out moved earlier by exactly that move, against the latency the plugin reports (FL Studio lines that up)
static void followChecks()
{
    const double sr = 48000.0, bpm = 120.0, spb = sr * 60.0 / bpm; const int bs = 256;
    const double maxQN = 127.0 / 16.0;   // a 32-slice break, less a 64th
    auto shiftOf = [] (long long step) { return step < 64 ? 0.25 : 0.75; };   // a NUDGE move at step 64 (ppq 16)
    auto run = [&] (bool withGb, bool follow, int& lat, int& hauntLat, int& good, int& bad, juce::String& info)
    {
        auto gb = withGb ? std::make_unique<ghostlink::Sender>() : nullptr;
        auto beat = [&] { if (gb) { gb->beat (true, 1.0); for (int b = 0; b < ghostlink::kBays; ++b) gb->setMaxShift (b, maxQN); } };
        beat();
        PhantomProcessor p; TestHead head; head.bpm = bpm; head.ppq = 0; p.setPlayHead (&head);
        setP (p, PhantomProcessor::pid (uHaunt, "on"), 1.0f); setP (p, PhantomProcessor::pid (uHaunt, "follow"), follow ? 1.0f : 0.0f);
        setP (p, PhantomProcessor::pid (uLim, "on"), 0.0f);   // the clicks are measured to the sample: nothing else in the chain
        p.prepareToPlay (sr, bs); p.flushUpdates();
        lat = p.getLatencySamples(); hauntLat = p.rack.slot (uHaunt).getLatencySamples();
        const int total = (int) (24 * sr); juce::AudioBuffer<float> in (2, total), out (2, total); in.clear();
        for (int k = 0; k * 24000 + 3000 < total; ++k) for (int c = 0; c < 2; ++c) in.setSample (c, k * 24000 + 3000, 0.001f * (float) (k + 1));
        juce::MidiBuffer m; long long sent = 0;
        for (int o = 0; o < total; o += bs)
        {
            beat();
            if (gb) for (; sent * 0.25 < head.ppq + bs / spb; ++sent) gb->step (0, sent, sent * 0.25, shiftOf (sent));
            juce::AudioBuffer<float> b (2, bs); for (int c = 0; c < 2; ++c) b.copyFrom (c, 0, in, c, o, bs);
            p.processBlock (b, m); for (int c = 0; c < 2; ++c) out.copyFrom (c, o, b, c, 0, bs);
            head.ppq += bs / spb;
        }
        good = bad = 0;
        for (int t = 0; t < total; ++t)
        {
            const float v = out.getSample (0, t); if (std::abs (v) < 0.0005f) continue;
            const int k = (int) std::lround (v / 0.001f) - 1; if (k < 0 || std::abs (v - 0.001f * (float) (k + 1)) > 1.0e-5f) continue;   // a click caught in a 1.5 ms move fade
            const long long x = 3000 + (long long) k * 24000, a = t - (lat - hauntLat) - hauntLat;   // the input sample leaving the look-ahead
            const double sh = (follow && withGb && a >= 0) ? shiftOf ((long long) std::floor (a / spb * 4.0)) : 0.0;
            const long long want = x + lat - (long long) std::llround (sh * spb);
            if (std::llabs (t - want) <= 1) ++good; else { ++bad; if (bad <= 3) info << "click " << k << " at " << t << " (want " << want << "); "; }
        }
    };
    {
        int lat, hl, good, bad; juce::String info; run (true, true, lat, hl, good, bad, info);
        const int want = (int) std::ceil (maxQN * spb) + juce::jmax (16, (int) (0.0015 * sr)) + 64;
        report (hl == want && lat >= hl, "FOLLOW GHOST BREAK: the look-ahead is the bay's longest move at the tempo", "HAUNT EDIT " + juce::String (hl) + " samples (want " + juce::String (want) + "), plugin " + juce::String (lat));
        report (bad == 0 && good >= 30, "FOLLOW GHOST BREAK: each click comes out moved earlier by the bay's NUDGE, before and after a move",
                juce::String (good) + " clicks in place, " + juce::String (bad) + " not; " + info);
    }
    {
        int lat, hl, good, bad; juce::String info; run (false, true, lat, hl, good, bad, info);
        report (hl == 0 && bad == 0 && good >= 40, "FOLLOW GHOST BREAK with no Ghost Break: no look-ahead, the sound passes in time", "HAUNT EDIT " + juce::String (hl) + " samples; " + juce::String (good) + " clicks in place, " + juce::String (bad) + " not; " + info);
    }
    {
        int lat, hl, good, bad; juce::String info; run (true, false, lat, hl, good, bad, info);
        report (hl == 0 && bad == 0 && good >= 40, "FOLLOW GHOST BREAK off: no look-ahead even with Ghost Break playing", "HAUNT EDIT " + juce::String (hl) + " samples; " + juce::String (good) + " in place, " + juce::String (bad) + " not");
    }
}

static void run (bool withCpu)
{
    rateChecks(); bitExactChecks(); imageChecks(); clickChecks(); automationChecks(); automationStateChecks(); layoutChecks(); engineSwitchChecks(); followChecks();
    if (withCpu) cpuChecks();
}
} // namespace spatialtest
