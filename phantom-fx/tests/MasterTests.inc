// SPDX-License-Identifier: AGPL-3.0-or-later
// Phase 6 (Master) checks: GHOST LIMITER's true-peak ceiling, the brief's challenger procedure (the check table for the engine the
// plugin does not use, plus a true-peak / CPU / latency / pitch comparison of both), and the blind A/B clips for Brandon.
#include "../src/master/Master.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace mastertest
{
// a fixed, synthesised test mix (no recordings): 0 = beat (kick, snare, hats) + saw bass + chord stabs at 100 BPM, 1 = a sung-like vowel
// melody (saw through two formant band-passes) over soft chords
static juce::AudioBuffer<float> material (int which, double sr, int n)
{
    juce::AudioBuffer<float> b (2, n); b.clear(); std::mt19937 rng (1234 + which); std::uniform_real_distribution<float> u (-1.0f, 1.0f);
    const double beat = 60.0 / 100.0 * sr, step = beat / 4.0;
    auto saw = [] (double ph) { return (float) (2.0 * (ph - std::floor (ph)) - 1.0); };
    const int bassNotes[8] { 36, 36, 39, 41, 36, 36, 43, 41 }, mel[16] { 60, 62, 63, 67, 65, 63, 62, 60, 58, 60, 62, 63, 62, 60, 58, 55 };
    const int chords[4][3] { { 48, 51, 55 }, { 46, 50, 53 }, { 44, 48, 51 }, { 43, 47, 50 } };
    auto hz = [] (int note) { return 440.0 * std::pow (2.0, (note - 69) / 12.0); };
    double phB = 0, phM = 0, phC[3] {}; float lpB = 0, bp1a = 0, bp1b = 0, bp2a = 0, bp2b = 0, hp = 0, prevN = 0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr; const int s16 = (int) (i / step); const double inStep = (i - s16 * step) / sr;
        float l = 0, r = 0;
        const int chord = (int) (i / (beat * 4)) % 4;
        float pad = 0; for (int k = 0; k < 3; ++k) { phC[k] += hz (chords[chord][k]) * (1.0 + 0.002 * k) / sr; pad += saw (phC[k]); }
        if (which == 0)
        {
            // kick on 1 and 3 (and the 'and' of 3), snare on 2 and 4, 16th hats
            const int s = s16 % 16;
            if (s == 0 || s == 8 || s == 11) { const double f = 50.0 + 100.0 * std::exp (-inStep * 30.0); l += 0.9f * (float) (std::sin (2.0 * juce::MathConstants<double>::pi * f * inStep) * std::exp (-inStep * 7.0)); }
            if (s == 4 || s == 12) { const float nz = u (rng) * (float) std::exp (-inStep * 18.0) * 0.5f + 0.3f * (float) (std::sin (2 * juce::MathConstants<double>::pi * 190.0 * inStep) * std::exp (-inStep * 25.0)); l += nz; r += nz * 0.9f; }
            { const float nz = u (rng); hp = nz - prevN + 0.0f * hp; prevN = nz; const float h = hp * 0.12f * (float) std::exp (-inStep * (s % 2 ? 60.0 : 35.0)); l += h * 0.8f; r += h; }
            phB += hz (bassNotes[(s16 / 4) % 8]) / sr; const float bs = saw (phB); lpB += 0.05f * (bs - lpB);
            const float bassEnv = (float) std::exp (-inStep * 3.0); l += 0.45f * lpB * bassEnv; r += 0.45f * lpB * bassEnv;
            const float stab = (s % 4 == 2) ? (float) std::exp (-inStep * 9.0) : 0.0f; l += 0.12f * pad * stab; r += 0.10f * pad * stab;
        }
        else
        {
            const int note = mel[(int) (i / (beat * 0.5)) % 16]; const double vib = 1.0 + 0.004 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.5 * t);
            phM += hz (note) * vib / sr; const float src = saw (phM);
            // two resonant band-passes (an 'ah' vowel: ~800 Hz and ~1200 Hz)
            auto bpf = [&] (float x, float& a, float& bb, double fc, double q) { const float f = (float) (2.0 * std::sin (juce::MathConstants<double>::pi * fc / sr)); const float hpv = x - a - (float) (1.0 / q) * bb; bb += f * hpv; a += f * bb; return bb; };
            const float v = 0.5f * bpf (src, bp1a, bp1b, 800.0, 6.0) + 0.35f * bpf (src, bp2a, bp2b, 1200.0, 7.0) + 0.06f * src;
            const double noteStep = (i / (beat * 0.5)) - std::floor (i / (beat * 0.5)); const float env = (float) juce::jmin (1.0, noteStep * 20.0) * (float) (0.75 + 0.25 * std::exp (-noteStep * 4.0));
            l += 0.7f * v * env; r += 0.7f * v * env;
            l += 0.05f * pad; r += 0.05f * pad;
        }
        b.setSample (0, i, l); b.setSample (1, i, r);
    }
    return b;
}

// 4x inter-sample peak (dBTP), the brief's true-peak measurement: JUCE's linear-phase half-band FIR oversampler (2 stages)
static float truePeakDb (const juce::AudioBuffer<float>& in, int start)
{
    juce::dsp::Oversampling<float> os (2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, false);
    os.initProcessing ((size_t) in.getNumSamples()); juce::AudioBuffer<float> c (in);
    juce::dsp::AudioBlock<float> blk (c); auto up = os.processSamplesUp (blk);
    float pk = 0; for (size_t ch = 0; ch < up.getNumChannels(); ++ch) for (size_t i = (size_t) start * 4; i < up.getNumSamples(); ++i) pk = juce::jmax (pk, std::abs (up.getSample ((int) ch, (int) i)));
    return db (pk);
}

static void setParam (Bench& b, const char* id, float v) { auto& ps = b.slot.params(); for (size_t i = 0; i < ps.size(); ++i) if (ps[i].id == id) b.slot.setValue ((int) i, v); }

// the limiter driven hard (+12 dB into a ceiling of -0.3 dB) with the beat, the melody and full-scale noise bursts
struct TpResult { float worstOver = -100.0f; float reductionDb = 0.0f; };
static TpResult limiterTruePeak (int engine, int quality, float ceiling = -0.3f)
{
    TpResult r;
    for (int which = 0; which < 3; ++which)
    {
        Bench b (std::make_unique<GhostLimiterFx> (engine)); b.setDefaults(); setParam (b, "gain", 12.0f); setParam (b, "quality", (float) quality); setParam (b, "ceiling", ceiling); b.slot.applyPolicy();
        auto buf = which < 2 ? material (which, 48000.0, 48000 * 4) : noise (48000 * 4, 0.9f, 99);
        const float inRms = rmsOf (buf); b.run (buf);
        r.worstOver = juce::jmax (r.worstOver, truePeakDb (buf, 4800 + b.slot.getLatencySamples()) - ceiling);
        r.reductionDb = juce::jmax (r.reductionDb, db (inRms) + 12.0f - db (rmsOf (buf)));
    }
    return r;
}

static double usPerBlock (std::unique_ptr<PhantomEffect> fx, std::function<void (Bench&)> setup)
{
    Bench b (std::move (fx)); b.setDefaults(); if (setup) setup (b); b.slot.applyPolicy(); auto buf = material (0, 48000.0, 256 * 800);
    b.run (buf); const auto t0 = std::chrono::steady_clock::now(); b.run (buf);
    return std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count() / 800.0;
}

static void limiterChecks()
{
    const int def = PHANTOM_LIMITER_ZL;
    const char* names[2] { "LSP", "ZL" };
    // the shipped engine at its defaults must hold the ceiling (brief: within 0.1 dBTP at 4x inter-sample measurement)
    { const auto t = limiterTruePeak (-1, 1); report (t.worstOver <= 0.1f, "GHOST LIMITER reverb tail / spectral / true-peak",
        juce::String ("true peak at most ") + juce::String (t.worstOver, 2) + " dB over the ceiling (limit +0.1) with +12 dB into it, engine " + names[def]); }
    // the gain-reduction read-out the page's meter shows: nothing while the sound is under the ceiling, about the overshoot when
    // it is pushed in (+12 dB of noise at -6 dBFS peak into a -0.3 dB ceiling: about 6 dB), nothing again when it goes quiet
    {
        auto run = [] (float gainDb, float amp) { Bench b (std::make_unique<GhostLimiterFx> (-1)); b.setDefaults(); setParam (b, "gain", gainDb); auto* m = b.slot.fx().reductionMeter(); auto buf = noise (48000, amp, 5); float worst = 0.0f; b.run (buf, [&] (int bi) { if (m != nullptr && bi > 40) worst = juce::jmax (worst, m->load()); }); return m == nullptr ? -1.0f : worst; };
        const float quiet = run (0.0f, 0.1f), pushed = run (12.0f, 0.5f);
        Bench b (std::make_unique<GhostLimiterFx> (-1)); b.setDefaults(); setParam (b, "gain", 12.0f); auto loud = noise (24000, 0.5f, 5); b.run (loud); juce::AudioBuffer<float> sil (2, 24000); sil.clear(); b.run (sil); const float after = b.slot.fx().reductionMeter() != nullptr ? b.slot.fx().reductionMeter()->load() : -1.0f;
        int others = 0; for (int u = 0; u < kNumUnits; ++u) if (u != uLim && makeEffect (u)->reductionMeter() != nullptr) ++others;
        report (quiet >= 0.0f && quiet < 0.3f && pushed > 4.0f && pushed < 12.0f && after >= 0.0f && after < 0.1f && others == 0, "GHOST LIMITER gain-reduction read-out", "under the ceiling " + juce::String (quiet, 2) + " dB; +12 dB pushed in " + juce::String (pushed, 1) + " dB; silence after it " + juce::String (after, 2) + " dB; one atomic value, read by the page's meter");
    }
    // challenger table (informational: the numbers for the choice)
    for (int e = 0; e < 2; ++e)
    {
        juce::String tp; for (int q = 0; q < 6; ++q) { const auto t = limiterTruePeak (e, q); tp += juce::String (q == 0 ? 1 : 1 << q) + "x " + juce::String (t.worstOver, 2) + " "; }
        Bench lb (std::make_unique<GhostLimiterFx> (e)); lb.setDefaults(); lb.slot.applyPolicy(); const int latDef = lb.slot.getLatencySamples();
        const double us = usPerBlock (std::make_unique<GhostLimiterFx> (e), [] (Bench& b) { setParam (b, "gain", 12.0f); });
        report (true, juce::String ("challenger: GHOST LIMITER ") + names[e] + (e == def ? " (shipped)" : " (not shipped)"),
                "true peak over the ceiling per oversampling: " + tp.trim() + " dB; latency at the default 2x " + juce::String (latDef) + " samples; CPU " + juce::String (us, 1) + " us per 256-sample block at 4x");
    }
    checkTable (def == 1 ? "GHOST LIMITER [challenger LSP]" : "GHOST LIMITER [challenger ZL]", [def] { return std::make_unique<GhostLimiterFx> (1 - def); });
    { const auto t = limiterTruePeak (1 - def, 1); report (true, juce::String ("GHOST LIMITER [challenger ") + names[1 - def] + "] true-peak", juce::String ("at most ") + juce::String (t.worstOver, 2) + " dB over the ceiling (informational)"); }
}

static void warpChecks()
{
    const int def = PHANTOM_WARP_RUBBERBAND;
    const char* names[2] { "SIGNALSMITH", "RUBBER BAND" };
    for (int e = 0; e < 2; ++e)
    {
        Bench b (std::make_unique<WarpFx> (e)); b.setDefaults(); auto& fx = dynamic_cast<WarpFx&> (b.slot.fx());
        bendlane::Shape s; s.add (0.0f, 7.0f / 12.0f); s.sanitise(); fx.setLane (s); b.slot.applyPolicy();
        const int n = 48000 * 2; auto buf = sine (48000.0, n, 440.0f, 0.5f); b.run (buf);
        int zc = 0; double first = -1, last = -1; for (int i = 48000 + 1; i < 48000 + 9600; ++i) if (buf.getSample (0, i - 1) < 0 && buf.getSample (0, i) >= 0) { const double f = i - 1 + buf.getSample (0, i - 1) / (buf.getSample (0, i - 1) - buf.getSample (0, i)); if (first < 0) first = f; last = f; ++zc; }
        const double hz = zc > 1 ? (zc - 1) * 48000.0 / (last - first) : 0.0, want = 440.0 * std::pow (2.0, 7.0 / 12.0);
        const double us = usPerBlock (std::make_unique<WarpFx> (e), [] (Bench& bb) { auto& w = dynamic_cast<WarpFx&> (bb.slot.fx()); w.setLane (bendlane::factory()[3].shape); });
        report (std::abs (hz / want - 1.0) < 0.005, juce::String ("challenger: WARP PITCH ") + names[e] + (e == def ? " (shipped)" : " (not shipped)"),
                "+7 st lane: " + juce::String (hz, 2) + " Hz (want " + juce::String (want, 2) + "); latency " + juce::String (b.slot.getLatencySamples()) + " samples (" + juce::String (b.slot.getLatencySamples() / 48.0, 1) + " ms); CPU " + juce::String (us, 1) + " us per 256-sample block");
    }
    checkTable (def == 1 ? "WARP [challenger SIGNALSMITH]" : "WARP [challenger RUBBER BAND]", [def] { return std::make_unique<WarpFx> (1 - def); });
    // switching WARP on mid-signal: never a silent gap (the slot plays the aligned dry while the stretcher fills, then fades in)
    for (int e = 0; e < 2; ++e)
    {
        Bench b (std::make_unique<WarpFx> (e)); b.setDefaults(); b.slot.setEnabled (false); b.slot.applyPolicy();
        auto& fx = dynamic_cast<WarpFx&> (b.slot.fx()); fx.setLane (bendlane::factory()[3].shape);
        auto buf = sine (48000.0, 48000 * 2, 220.0f, 0.5f); const int onAt = 48000 / 256;
        b.run (buf, [&] (int bi) { if (bi == onAt) b.slot.setEnabled (true); });
        float worst = 1.0f; const int lat = b.slot.getLatencySamples();
        for (int st = 48000 / 256 * 256 + lat; st + 480 < 48000 * 2; st += 480) { float pk = 0; for (int i = st; i < st + 480; ++i) pk = juce::jmax (pk, std::abs (buf.getSample (0, i))); worst = juce::jmin (worst, pk); }
        report (worst > 0.1f, juce::String ("WARP ") + names[e] + " switch-on has no silent gap", "quietest 10 ms after switching on: peak " + juce::String (db (worst), 1) + " dBFS (the signal is -6 dBFS)");
    }
}

// ---------------------------------------------------------------- blind clips (the brief's challenger procedure): unlabelled pairs, A/B order
// drawn at random per pair, loudness matched (same RMS), key in a separate file
static void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b)
{
    f.deleteFile(); juce::WavAudioFormat wav; std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    if (auto* w = wav.createWriterFor (os.get(), 48000.0, 2, 24, {}, 0)) { os.release(); std::unique_ptr<juce::AudioFormatWriter> ww (w); ww->writeFromAudioSampleBuffer (b, 0, b.getNumSamples()); }
}
static int renderClips (const juce::File& dir)
{
    dir.createDirectory();
    struct Pair { const char* id; const char* what; int unit; int material; };
    const Pair pairs[] { { "limiter-1", "GHOST LIMITER, beat, +10 dB into -0.3 dB", 24, 0 }, { "limiter-2", "GHOST LIMITER, melody, +10 dB into -0.3 dB", 24, 1 },
                         { "warp-1", "WARP PITCH, melody, factory bend", 4, 1 }, { "warp-2", "WARP PITCH, beat, factory bend", 4, 0 } };
    std::mt19937 rng (20261109u);   // fixed: the clips rendered on the Mac and the listening page share one key
    juce::String key;
    key << "Phantom FX blind clips: which engine is A and which is B in each pair (do not open before listening)\n";
    for (auto& p : pairs)
    {
        juce::AudioBuffer<float> out[2]; const char* eng[2];
        for (int e = 0; e < 2; ++e)
        {
            std::unique_ptr<PhantomEffect> fx; if (p.unit == 24) fx = std::make_unique<GhostLimiterFx> (e); else fx = std::make_unique<WarpFx> (e);
            Bench b (std::move (fx)); b.setDefaults();
            if (p.unit == 24) { setParam (b, "gain", 10.0f); eng[e] = e ? "ZL Limiter" : "LSP Limiter"; }
            else { auto& w = dynamic_cast<WarpFx&> (b.slot.fx()); w.setLane (bendlane::factory()[3].shape); eng[e] = e ? "Rubber Band" : "Signalsmith Stretch"; }
            b.slot.applyPolicy();
            const int lead = 48000, len = 48000 * 7; auto buf = material (p.material, 48000.0, lead + len + 48000); b.tempo.bpm = 100.0;
            b.run (buf); const int lat = b.slot.getLatencySamples();
            out[e].setSize (2, len); for (int c = 0; c < 2; ++c) out[e].copyFrom (c, 0, buf, c, lead + lat, len);
        }
        // loudness match: both to the quieter one's RMS
        const float r0 = rmsOf (out[0]), r1 = rmsOf (out[1]), target = juce::jmin (r0, r1);
        out[0].applyGain (target / r0); out[1].applyGain (target / r1);
        { const float pk = juce::jmax (peakOf (out[0]), peakOf (out[1])); if (pk > 0.89f) { out[0].applyGain (0.89f / pk); out[1].applyGain (0.89f / pk); } }   // both down together: no clipping
        for (int e = 0; e < 2; ++e) { out[e].applyGainRamp (0, 480, 0.0f, 1.0f); out[e].applyGainRamp (out[e].getNumSamples() - 4800, 4800, 1.0f, 0.0f); }
        const bool swap = (rng() & 1) != 0;
        writeWav (dir.getChildFile (juce::String (p.id) + "-A.wav"), out[swap ? 1 : 0]);
        writeWav (dir.getChildFile (juce::String (p.id) + "-B.wav"), out[swap ? 0 : 1]);
        key << p.id << " (" << p.what << "): A = " << eng[swap ? 1 : 0] << ", B = " << eng[swap ? 0 : 1] << "  (matched RMS " << juce::String (db (rmsOf (out[0])), 1) << " dBFS)\n";
    }
    dir.getChildFile ("KEY.txt").replaceWithText (key);
    std::printf ("%s", key.toRawUTF8()); return 0;
}
} // namespace mastertest
