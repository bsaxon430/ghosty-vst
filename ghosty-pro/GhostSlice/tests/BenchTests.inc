// GhostSliceTests bench [DIR]: the pitch-engine benchmark (GHOST-SLICE-STUDY.md 1.6 / phase 15): every material shifted by
// -7 / -5 / -2 / +2 / +5 / +7 semitones with each engine, measured against the source:
//   SIGNALSMITH  the global PITCH path (Signalsmith Stretch, its own formant keep)
//   WIDE         a note edit, ENGINE WIDE (Signalsmith + our LPC / harmonic formant correction, FormantFix)
//   VOCAL        a note edit, ENGINE VOCAL (our TD-PSOLA + the same correction)
//   WORLD        WORLD 1.0.1 (M. Morise, modified BSD; measurement only, linked into the test binary): Harvest f0, CheapTrick envelope,
//                D4C aperiodicity, f0 x ratio, synthesis with the source envelope
// Metrics (voiced frames of both): f0 error (median / p95 |cents| of our tracker vs the source f0 x ratio), F1 / F2 drift (the
// centroid of the LPC envelope in 250-1000 Hz / 1000-2800 Hz, median |out / in - 1| over 40 ms frames), clicks (samples whose step
// exceeds 4x the source's 99.9th percentile step x the pitch ratio), runtime (ms per second of audio, one core).
// Writes DIR/bench.csv and prints the summary (docs/research/ghost-slice/BENCHMARK.md).
#include "../Lpc.h"
#if GSLICE_BENCH_WORLD
 #include "world/harvest.h"
 #include "world/cheaptrick.h"
 #include "world/d4c.h"
 #include "world/synthesis.h"
#endif

namespace bench
{
// a sung vowel: glottal-like harmonics (-12 dB / oct) through five formant sets, f0 with a 5 Hz +-15 ct vibrato
static juce::AudioBuffer<float> vowel (double sr, double f0, int which, double secs = 1.4)
{
    static const double F[5][3] { { 730, 1090, 2440 }, { 270, 2290, 3010 }, { 300, 870, 2240 }, { 530, 1840, 2480 }, { 570, 840, 2410 } };   // a i u e o
    const int n = (int) (sr * secs);
    juce::AudioBuffer<float> b (2, n); b.clear();
    double ph = 0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr, f = f0 * std::exp2 (15.0 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.0 * t) / 1200.0);
        ph += f / sr;
        double v = 0;
        for (int h = 1; h * f0 < std::min (6000.0, sr * 0.45); ++h)
        {
            const double fh = h * f0; double g = 0;
            for (int k = 0; k < 3; ++k) { const double bw = 70.0 + 30.0 * k, d = (fh - F[which][k]) / bw; g += (k == 0 ? 1.0 : k == 1 ? 0.7 : 0.35) / (1.0 + d * d); }
            v += g / h * std::sin (2.0 * juce::MathConstants<double>::pi * h * ph);
        }
        const double env = std::min ({ 1.0, t / 0.03, (secs - t) / 0.05 });
        for (int c = 0; c < 2; ++c) b.setSample (c, i, (float) (0.25 * env * v));
    }
    return b;
}

static std::vector<float> monoOf (const juce::AudioBuffer<float>& b, int n = -1)
{
    if (n < 0) n = b.getNumSamples();
    std::vector<float> m ((size_t) n, 0.0f);
    for (int c = 0; c < b.getNumChannels(); ++c) for (int i = 0; i < juce::jmin (n, b.getNumSamples()); ++i) m[(size_t) i] += b.getSample (c, i) / (float) b.getNumChannels();
    return m;
}

struct Metrics { double f0Med = 0, f0P95 = 0, f1 = 0, f2 = 0; int clicks = 0; double msPerS = 0; int frames = 0; };

// F1 / F2 band centroids of the LPC envelope of a 40 ms frame at c
static bool bandCentroids (const std::vector<float>& x, int64 c, double sr, double& c1, double& c2)
{
    const int n = (int) std::lround (0.04 * sr), p = 28;
    if (c - n / 2 < 0 || c + n / 2 >= (int64) x.size()) return false;
    std::vector<double> a ((size_t) p + 1), scratch ((size_t) n + 2 * (p + 1) + 8);
    const double err = lpc::analyseFrame (x.data() + c - n / 2, n, p, sr, 60.0, 0.9, a.data(), scratch.data());
    if (! (err > 0)) return false;
    double n1 = 0, d1 = 0, n2 = 0, d2 = 0;
    for (double f = 250; f <= 2800; f += 10)
    {
        const double m = 1.0 / std::max (1e-9, lpc::magA (a.data(), p, 2.0 * juce::MathConstants<double>::pi * f / sr)), e = m * m;
        if (f <= 1000) { n1 += f * e; d1 += e; } else { n2 += f * e; d2 += e; }
    }
    if (d1 <= 0 || d2 <= 0) return false;
    c1 = n1 / d1; c2 = n2 / d2; return true;
}

static Metrics measure (const juce::AudioBuffer<float>& src, const juce::AudioBuffer<float>& out, double sr, double semis, double ms)
{
    Metrics m; m.msPerS = ms / (src.getNumSamples() / sr);
    const int n = juce::jmin (src.getNumSamples(), out.getNumSamples());
    const auto xs = monoOf (src, n), ys = monoOf (out, n);
    const auto ts = an::trackPitch (xs.data(), n, sr), to = an::trackPitch (ys.data(), n, sr);
    const double ratio = std::exp2 (semis / 12.0);
    std::vector<double> err, d1, d2;
    for (int f = 0; f < juce::jmin (ts.frames(), to.frames()); ++f)
    {
        if (ts.f0[(size_t) f] <= 0 || to.f0[(size_t) f] <= 0 || ts.voicedProb[(size_t) f] < 0.6f) continue;
        err.push_back (std::abs (1200.0 * std::log2 (to.f0[(size_t) f] / (ts.f0[(size_t) f] * ratio))));
        if (f % 8 == 0)
        {
            double a1, a2, b1, b2; const int64 c = (int64) f * ts.hop;
            if (bandCentroids (xs, c, sr, a1, a2) && bandCentroids (ys, c, sr, b1, b2)) { d1.push_back (std::abs (b1 / a1 - 1.0) * 100.0); d2.push_back (std::abs (b2 / a2 - 1.0) * 100.0); }
        }
    }
    m.frames = (int) err.size();
    m.f0Med = an::detail::median (err); m.f0P95 = an::detail::percentile (err, 0.95);
    m.f1 = an::detail::median (d1); m.f2 = an::detail::median (d2);
    std::vector<double> steps; for (int i = 1; i < n; ++i) steps.push_back (std::abs ((double) xs[(size_t) i] - xs[(size_t) i - 1]));
    const double thr = 4.0 * an::detail::percentile (steps, 0.999) * std::max (1.0, ratio);
    for (int i = 1; i < n; ++i) if (std::abs ((double) ys[(size_t) i] - ys[(size_t) i - 1]) > thr) ++m.clicks;
    return m;
}

#if GSLICE_BENCH_WORLD
static juce::AudioBuffer<float> world (const juce::AudioBuffer<float>& src, double sr, double semis)
{
    const auto xm = monoOf (src);
    const int n = (int) xm.size(), fs = (int) std::lround (sr);
    std::vector<double> x (xm.begin(), xm.end());
    HarvestOption ho; InitializeHarvestOption (&ho); ho.frame_period = 5.0; ho.f0_floor = 60.0; ho.f0_ceil = 900.0;
    const int fl = GetSamplesForHarvest (fs, n, ho.frame_period);
    std::vector<double> f0 ((size_t) fl), tp ((size_t) fl);
    Harvest (x.data(), n, fs, &ho, tp.data(), f0.data());
    CheapTrickOption co; InitializeCheapTrickOption (fs, &co); co.f0_floor = 60.0; co.fft_size = GetFFTSizeForCheapTrick (fs, &co);
    const int nb = co.fft_size / 2 + 1;
    std::vector<std::vector<double>> sp ((size_t) fl, std::vector<double> ((size_t) nb)), ap ((size_t) fl, std::vector<double> ((size_t) nb));
    std::vector<double*> spp ((size_t) fl), app ((size_t) fl);
    for (int i = 0; i < fl; ++i) { spp[(size_t) i] = sp[(size_t) i].data(); app[(size_t) i] = ap[(size_t) i].data(); }
    CheapTrick (x.data(), n, fs, tp.data(), f0.data(), fl, &co, spp.data());
    D4COption dop; InitializeD4COption (&dop); dop.threshold = 0.85;
    D4C (x.data(), n, fs, tp.data(), f0.data(), fl, co.fft_size, &dop, app.data());
    const double ratio = std::exp2 (semis / 12.0);
    for (auto& v : f0) v *= ratio;
    std::vector<double> y ((size_t) n, 0.0);
    Synthesis (f0.data(), fl, spp.data(), app.data(), co.fft_size, ho.frame_period, fs, n, y.data());
    juce::AudioBuffer<float> b (2, n);
    for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) b.setSample (c, i, (float) y[(size_t) i]);
    return b;
}
#endif

struct Material { juce::String name; juce::AudioBuffer<float> audio; bool real = false; };
} // namespace bench

static void runBench (const juce::File& dir)
{
    using namespace bench;
    dir.createDirectory();
    const double sr = 48000;
    std::vector<Material> mats;
    static const char* vn[5] { "a", "i", "u", "e", "o" };
    for (double f0 : { 140.0, 220.0 }) for (int v = 0; v < 5; ++v) mats.push_back ({ juce::String ("vowel-") + vn[v] + "-" + juce::String ((int) f0), vowel (sr, f0, v) });
    {
        const juce::File f ("/root/work/04_vocal_ecto_echo.mp3");
        juce::String err; auto s = f.existsAsFile() ? source::load (f, err) : nullptr;
        if (s != nullptr && s->sampleRate == sr)
        {   // the first two sung notes (0 .. 1.35 s)
            juce::AudioBuffer<float> b (2, (int) (1.35 * sr));
            for (int c = 0; c < 2; ++c) b.copyFrom (c, 0, s->audio, juce::jmin (c, s->channels - 1), 0, b.getNumSamples());
            mats.push_back ({ "vocal-ecto", b, true });
        }
        else std::printf ("  (the real vocal /root/work/04_vocal_ecto_echo.mp3 is missing or not 48 kHz: synthetic only)\n");
    }
    const std::vector<double> shifts { -7, -5, -2, 2, 5, 7 };
    std::vector<juce::String> engines { "SIGNALSMITH", "WIDE", "VOCAL" };
#if GSLICE_BENCH_WORLD
    engines.push_back ("WORLD");
#endif
    juce::String csv = "material,engine,shift_st,f0_median_ct,f0_p95_ct,f1_drift_pct,f2_drift_pct,clicks,ms_per_s,frames\n";
    struct Acc { double f0 = 0, f1 = 0, f2 = 0, ms = 0; int clicks = 0, n = 0; };
    std::map<juce::String, Acc> small, large, vocalLarge;
    for (auto& m : mats)
    {
        Rig rig (m.audio);
        { Project q = rig->project(); q.material = "MELODIC"; rig->apply (q, "melodic"); }
        rig->waitAnalysis (60000);
        const Project base = rig->project();
        const auto notes = notesNow (*rig.p);
        for (double st : shifts)
            for (auto& e : engines)
            {
                juce::AudioBuffer<float> out; double ms = 0;
                const auto t0 = juce::Time::getMillisecondCounterHiRes();
                if (e == "SIGNALSMITH") { Project q = base; q.pitchSemis = st; rig->apply (q, "p"); out = rig.render(); }
                else if (e == "WIDE" || e == "VOCAL")
                {
                    Project q = base;
                    std::map<int64, std::vector<pitchedit::NoteRef>> byRegion; for (auto& n : notes) byRegion[n.regionId].push_back (n);
                    for (auto& [rid, ns] : byRegion) if (auto r = ops::editNotes (q, rid, ns, [&] (PitchEdit& pe) { pe.cents += st * 100.0; pe.engine = e == "WIDE" ? 2 : 1; })) q = *r;
                    rig->apply (q, "n"); out = rig.render();
                }
#if GSLICE_BENCH_WORLD
                else out = world (m.audio, sr, st);
#endif
                ms = juce::Time::getMillisecondCounterHiRes() - t0;
                rig->apply (base, "b");
                const auto r = measure (m.audio, out, sr, st, ms);
                csv << m.name << "," << e << "," << juce::String (st, 0) << "," << juce::String (r.f0Med, 2) << "," << juce::String (r.f0P95, 2) << "," << juce::String (r.f1, 2) << ","
                    << juce::String (r.f2, 2) << "," << r.clicks << "," << juce::String (r.msPerS, 1) << "," << r.frames << "\n";
                auto& A = std::abs (st) >= 5 ? large[e] : small[e];
                A.f0 += r.f0Med; A.f1 += r.f1; A.f2 += r.f2; A.ms += r.msPerS; A.clicks += r.clicks; ++A.n;
                if (std::abs (st) >= 5) { auto& V = vocalLarge[e + (m.real ? " real" : " synth")]; V.f0 += r.f0Med; V.f1 += r.f1; V.f2 += r.f2; V.ms += r.msPerS; V.clicks += r.clicks; ++V.n; }
                if (e == "WORLD" || e == "SIGNALSMITH") { juce::WavAudioFormat wav; if (m.name == "vowel-a-220" || m.real) writeAudio (out, sr, dir.getChildFile (m.name + "-" + e.toLowerCase() + "-" + juce::String ((int) st) + ".wav"), wav, 24); }
            }
        std::printf ("  %s done\n", m.name.toRawUTF8()); std::fflush (stdout);
    }
    dir.getChildFile ("bench.csv").replaceWithText (csv);
    auto table = [] (const char* title, std::map<juce::String, Acc>& t)
    {
        std::printf ("  %s\n  %-18s %10s %9s %9s %8s %9s\n", title, "engine", "f0 med ct", "F1 drift", "F2 drift", "clicks", "ms / s");
        for (auto& [k, a] : t) if (a.n > 0) std::printf ("  %-18s %10.2f %8.2f%% %8.2f%% %8d %9.1f\n", k.toRawUTF8(), a.f0 / a.n, a.f1 / a.n, a.f2 / a.n, a.clicks, a.ms / a.n);
    };
    table ("|shift| = 2 st (mean over the materials)", small);
    table ("|shift| = 5 / 7 st", large);
    table ("|shift| = 5 / 7 st, by material class", vocalLarge);
    std::printf ("  wrote %s\n", dir.getChildFile ("bench.csv").getFullPathName().toRawUTF8());
}
