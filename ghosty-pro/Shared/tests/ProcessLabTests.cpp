// PROCESS LAB tests.
//   ProcessLabTests            -> engine / threading / undo / audition tests (exit code = failures)
//   ProcessLabTests shots DIR  -> PNG screenshots of the panel (needs a display / Xvfb)
//   ProcessLabTests demos DIR  -> WAV demos (synthetic drum loop + synth phrase through three chains; source, gap, result)
#include "../ProcessLab.h"
#include "../ProcessLabPanel.h"
#include "../GhostArt.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_dsp/juce_dsp.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <set>

using namespace plab;
static int fails = 0, passes = 0;
static void check (bool ok, const juce::String& what, const juce::String& detail = {})
{
    std::cout << (ok ? "  PASS  " : "  FAIL  ") << what << (detail.isNotEmpty() ? "  (" + detail + ")" : juce::String()) << std::endl;
    (ok ? passes : fails)++;
}
static constexpr double kTwoPi = juce::MathConstants<double>::twoPi;

// ---------------------------------------------------------------- signals
static Audio sine (double rate, double secs, double hz, float amp = 0.5f, int chans = 2)
{
    Audio a; a.rate = rate; a.resize (chans, (int) (secs * rate));
    for (int c = 0; c < chans; ++c) for (int i = 0; i < a.length(); ++i) a.ch[(size_t) c][(size_t) i] = amp * (float) std::sin (kTwoPi * hz * i / rate + 0.3 * c);
    return a;
}
static Audio tones (double rate, double secs, const std::vector<std::pair<double, float>>& parts)
{
    Audio a; a.rate = rate; a.resize (2, (int) (secs * rate));
    for (int i = 0; i < a.length(); ++i)
    {
        double v = 0; for (auto& p : parts) v += p.second * std::sin (kTwoPi * p.first * i / rate);
        a.ch[0][(size_t) i] = a.ch[1][(size_t) i] = (float) v;
    }
    return a;
}
static Audio drumLoop (double rate, int bars = 2, double bpm = 124)
{
    const double beat = 60.0 / bpm; const int n = (int) (bars * 4 * beat * rate);
    Audio a; a.rate = rate; a.resize (2, n); juce::Random r (7);
    auto add = [&] (double t0, auto&& gen, double len)
    {
        const int s0 = (int) (t0 * rate), L = (int) (len * rate);
        for (int i = 0; i < L && s0 + i < n; ++i) { const float v = gen ((double) i / rate); a.ch[0][(size_t) (s0 + i)] += v; a.ch[1][(size_t) (s0 + i)] += v; }
    };
    for (int b = 0; b < bars * 4; ++b)
    {
        const double t = b * beat;
        add (t, [] (double x) { const double f = 48 + 110 * std::exp (-x * 28); return (float) (0.9 * std::sin (kTwoPi * f * x - 0.0) * std::exp (-x * 7)); }, 0.45);   // kick
        if (b % 2 == 1) add (t, [&r] (double x) { return (float) ((0.45 * std::sin (kTwoPi * 190 * x) + 0.5 * r.nextFloat() * 2 - 0.5) * std::exp (-x * 18)); }, 0.25);  // snare
        for (int h = 0; h < 2; ++h) add (t + h * beat * 0.5, [&r, h] (double x) { return (float) ((r.nextFloat() * 2 - 1) * (h ? 0.22 : 0.14) * std::exp (-x * 60)); }, 0.06); // hats
        if (b % 4 == 3) add (t + beat * 0.75, [&r] (double x) { return (float) ((r.nextFloat() * 2 - 1) * 0.1 * std::exp (-x * 40)); }, 0.08);
    }
    return a;
}
static Audio synthPhrase (double rate)
{
    const int notes[] = { 57, 60, 64, 67, 69, 67, 64, 62, 60, 64, 67, 72 };
    const double step = 0.24; const int n = (int) (12 * step * rate + rate * 0.6);
    Audio a; a.rate = rate; a.resize (2, n);
    double lp[2] = { 0, 0 };
    for (int k = 0; k < 12; ++k)
    {
        const double f = 440.0 * std::pow (2.0, (notes[k] - 69) / 12.0);
        const int s0 = (int) (k * step * rate), L = (int) (step * 1.6 * rate);
        for (int i = 0; i < L && s0 + i < n; ++i)
        {
            const double t = (double) i / rate;
            const double env = std::min (1.0, t * 200) * std::exp (-t * 3.5);
            double v = 0; for (int hN = 1; hN <= 12; ++hN) v += std::sin (kTwoPi * f * hN * t * (1.0 + 0.002 * (hN % 2))) / hN;   // band-limited saw-ish
            for (int c = 0; c < 2; ++c)
            {
                const double cut = 0.05 + 0.25 * std::exp (-t * 6); lp[c] += cut * (v - lp[c]);
                a.ch[(size_t) c][(size_t) (s0 + i)] += (float) (0.22 * env * lp[c] * (c ? 0.95 : 1.05));
            }
        }
    }
    return a;
}

// ---------------------------------------------------------------- measures
static double peakHz (const std::vector<float>& x, double rate, size_t start = 0)
{
    constexpr int order = 16, M = 1 << order; juce::dsp::FFT fft (order); std::vector<float> b ((size_t) M * 2, 0.0f);
    for (int i = 0; i < M && start + (size_t) i < x.size(); ++i) b[(size_t) i] = x[start + (size_t) i] * (float) (0.5 - 0.5 * std::cos (kTwoPi * i / M));
    fft.performFrequencyOnlyForwardTransform (b.data(), true);
    int k = 2; for (int i = 2; i < M / 2 - 1; ++i) if (b[(size_t) i] > b[(size_t) k]) k = i;
    const double a = b[(size_t) k - 1], c = b[(size_t) k], d = b[(size_t) k + 1]; const double off = 0.5 * (a - d) / (a - 2 * c + d);
    return (k + off) * rate / M;
}
static std::vector<float> spectrum (const std::vector<float>& x, size_t start)   // 32768-point magnitude, Hann
{
    constexpr int order = 15, M = 1 << order; juce::dsp::FFT fft (order); std::vector<float> b ((size_t) M * 2, 0.0f);
    for (int i = 0; i < M && start + (size_t) i < x.size(); ++i) b[(size_t) i] = x[start + (size_t) i] * (float) (0.5 - 0.5 * std::cos (kTwoPi * i / M));
    fft.performFrequencyOnlyForwardTransform (b.data(), true); b.resize (M / 2); return b;
}
static float magAt (const std::vector<float>& s, double hz, double rate) { const int k = (int) std::round (hz * 32768 / rate); float m = 0; for (int i = k - 3; i <= k + 3; ++i) if (i >= 0 && i < (int) s.size()) m = std::max (m, s[(size_t) i]); return m; }
static bool finite (const Audio& a) { for (auto& c : a.ch) for (float v : c) if (! std::isfinite (v)) return false; return true; }
static bool same (const Audio& a, const Audio& b) { return a.ch == b.ch; }
static float maxDiff (const Audio& a, const Audio& b)
{
    if (a.channels() != b.channels() || a.length() != b.length()) return 1.0e9f;
    float m = 0; for (int c = 0; c < a.channels(); ++c) for (int i = 0; i < a.length(); ++i) m = std::max (m, std::abs (a.ch[(size_t) c][(size_t) i] - b.ch[(size_t) c][(size_t) i])); return m;
}
static float rmsRange (const std::vector<float>& x, size_t a, size_t b) { double s = 0; b = std::min (b, x.size()); for (size_t i = a; i < b; ++i) s += (double) x[i] * x[i]; return b > a ? (float) std::sqrt (s / (double) (b - a)) : 0.0f; }
static Op mk (OpType t, std::initializer_list<float> p = {})
{
    Op o (t); int i = 0; for (float v : p) o.p[(size_t) i++] = v; return o;
}
static Audio run (const Audio& in, const Op& o, uint32_t seed = 1) { Audio out; renderOp (in, o, seed, out); return out; }

// ---------------------------------------------------------------- tests
static void testBasics (double rate)
{
    std::cout << "-- basics @ " << rate << " Hz" << std::endl;
    const Audio x = synthPhrase (rate);
    const Audio rr = run (run (x, Op (OpType::Reverse)), Op (OpType::Reverse));
    check (same (rr, x), "reverse twice = identity (bit-exact)");
    const Audio nn = run (x, mk (OpType::Normalise, { -3.0f }));
    const float pk = nn.peak(), want = juce::Decibels::decibelsToGain (-3.0f);
    check (std::abs (pk - want) < 1.0e-6f, "normalise -3 dB exact", juce::String (pk, 7) + " vs " + juce::String (want, 7));
    // fades on DC 1.0
    Audio dc; dc.rate = rate; dc.resize (1, (int) (rate * 0.5)); for (float& v : dc.ch[0]) v = 1.0f;
    const int L = (int) std::round (0.1 * rate);
    const Audio fl = run (dc, mk (OpType::FadeIn, { 100.0f, 0.0f })), fe = run (dc, mk (OpType::FadeIn, { 100.0f, -1.0f })), fg = run (dc, mk (OpType::FadeIn, { 100.0f, 1.0f }));
    const Audio fo = run (dc, mk (OpType::FadeOut, { 100.0f, 0.0f }));
    const float mid = fl.ch[0][(size_t) L / 2];
    check (fl.ch[0][0] == 0.0f && std::abs (mid - 0.5f) < 1.0e-3f && fl.ch[0][(size_t) L] == 1.0f, "fade in linear: 0 -> 0.5 at half -> untouched after", juce::String (mid, 5));
    check (fe.ch[0][(size_t) L / 2] < 0.2f && fg.ch[0][(size_t) L / 2] > 0.75f, "fade shapes: exp below / log above linear at half",
           juce::String (fe.ch[0][(size_t) L / 2], 3) + " / " + juce::String (fg.ch[0][(size_t) L / 2], 3));
    bool mono = true; for (int i = 1; i < L; ++i) mono = mono && fe.ch[0][(size_t) i] >= fe.ch[0][(size_t) i - 1] && fg.ch[0][(size_t) i] >= fg.ch[0][(size_t) i - 1];
    check (mono, "fade shapes monotonic");
    check (fo.ch[0].back() == 0.0f && std::abs (fo.ch[0][(size_t) (dc.length() - 1 - L / 2)] - 0.5f) < 1.0e-3f, "fade out mirrors fade in");
    // envelope impose
    Op env (OpType::Envelope); env.env = { { 0.0f, 0.0f }, { 0.5f, 1.0f }, { 1.0f, 0.25f } };
    const Audio ee = run (dc, env); const int n = dc.length();
    check (std::abs (ee.ch[0][0]) < 1e-6f && std::abs (ee.ch[0][(size_t) (n - 1) / 2] - 1.0f) < 2e-3f && std::abs (ee.ch[0][(size_t) n - 1] - 0.25f) < 1e-6f,
           "envelope impose follows breakpoints (0, 1 at half, 0.25 at end)");
    // extend
    const Audio ex = run (x, mk (OpType::Extend, { 3.0f, 20.0f, 0.0f }));
    check (ex.length() == (int) std::round (x.length() * 3.0), "extend x3 length", juce::String (ex.length()) + " vs " + juce::String (x.length() * 3));
    Audio s1 = sine (rate, 0.5, 100.0, 0.5f, 1);
    const Audio exs = run (s1, mk (OpType::Extend, { 2.0f, 30.0f, 0.0f }));
    check (maxAbsDiff (exs.ch[0], 1) < 0.05f, "extend crossfaded loop join without a jump", "max |d1| " + juce::String (maxAbsDiff (exs.ch[0], 1), 4));
}

static void testCycles (double rate)
{
    std::cout << "-- wavecycles @ " << rate << " Hz" << std::endl;
    const Audio x = tones (rate, 1.0, { { 220.0, 0.5f }, { 440.0, 0.2f }, { 661.0, 0.1f } });
    const float d1 = maxAbsDiff (x.ch[0], 1), bound = 3.0f * d1;
    struct C { const char* name; Op op; };
    const C cs[] = {
        { "shuffle", mk (OpType::CycleShuffle, { 1, 1 }) }, { "shuffle g4", mk (OpType::CycleShuffle, { 4, 1 }) },
        { "repeat x3", mk (OpType::CycleRepeat, { 1, 3, 1 }) }, { "omit 2/1", mk (OpType::CycleOmit, { 1, 2, 1, 0 }) },
        { "omit rnd", mk (OpType::CycleOmit, { 2, 1, 1, 1 }) }, { "reverse in", mk (OpType::CycleReverse, { 1, 0 }) },
        { "reverse order", mk (OpType::CycleReverse, { 3, 1 }) }, { "reverse both", mk (OpType::CycleReverse, { 1, 2 }) },
        { "interleave 3", mk (OpType::CycleInterleave, { 2, 3 }) } };
    for (auto& c : cs)
    {
        const Audio y = run (x, c.op, 11);
        const float d2 = maxAbsDiff (y.ch[0], 2);
        check (d2 <= bound && finite (y), juce::String ("cycle ") + c.name + " click-free (2nd diff <= 3 x input 1st diff)", juce::String (d2, 4) + " <= " + juce::String (bound, 4));
    }
    // the metric catches clicks: shuffling fixed 300-sample blocks (not at zero crossings) must exceed the bound
    Audio bad = x; { const int B = 300, K = x.length() / B; Rng r (3); for (int k = K - 1; k > 0; --k) { const int j = r.below (k + 1); for (auto& ch : bad.ch) std::swap_ranges (ch.begin() + k * B, ch.begin() + (k + 1) * B, ch.begin() + j * B); } }
    check (maxAbsDiff (bad.ch[0], 2) > bound, "control: blind block shuffle exceeds the click bound", juce::String (maxAbsDiff (bad.ch[0], 2), 4));
    const Audio sh = run (x, mk (OpType::CycleShuffle, { 1, 1 }), 5);
    check (sh.length() == x.length() && std::abs (sh.rms() / x.rms() - 1.0f) < 0.02f && ! same (sh, x), "shuffle keeps length + level, changes order",
           "rms ratio " + juce::String (sh.rms() / x.rms(), 4));
    const auto units = cycleUnits (tones (rate, 1.0, { { 220.0, 0.5f }, { 440.0, 0.2f } }), 1);
    check (std::abs ((int) units.size() - 1 - 220) <= 2, "cycleUnits finds 220 cycles in 1 s of 220 Hz (+ 2nd harmonic)", juce::String ((int) units.size() - 1));
    const Audio rp = run (x, mk (OpType::CycleRepeat, { 1, 2, 1 })), om = run (x, mk (OpType::CycleOmit, { 1, 1, 1, 0 }));
    check (std::abs (rp.length() - 2 * x.length()) < x.length() / 50 && std::abs (om.length() - x.length() / 2) < x.length() / 50, "repeat x2 doubles, omit 1/1 halves",
           juce::String (rp.length()) + " / " + juce::String (om.length()));
    check (std::abs (peakHz (rp.ch[0], rate) - 220.0) < 2.0, "repeat keeps the cycle pitch", juce::String (peakHz (rp.ch[0], rate), 1) + " Hz");
}

static void testSeeds (double rate)
{
    std::cout << "-- seeds @ " << rate << " Hz" << std::endl;
    const Audio x = drumLoop (rate, 1);
    const Op br = mk (OpType::Brassage, { 50, 3, 80, 5, 1.5f }), it = mk (OpType::Iterate, { 5, 3, 20, 3, 30 });
    const Audio b1 = run (x, br, 42), b2 = run (x, br, 42), b3 = run (x, br, 43);
    check (same (b1, b2), "brassage bit-identical for the same seed");
    check (maxDiff (b1, b3) > 1.0e-2f, "brassage differs across seeds", juce::String (maxDiff (b1, b3), 3));
    check (std::abs (b1.length() - (int) std::round (x.length() * 1.5)) <= 1, "brassage stretch 1.5 length");
    const Audio i1 = run (x, it, 42), i2 = run (x, it, 42), i3 = run (x, it, 99);
    check (same (i1, i2), "iterate bit-identical for the same seed");
    check (! same (i1, i3), "iterate differs across seeds", juce::String (i1.length()) + " vs " + juce::String (i3.length()));
    check (i1.length() > 4 * x.length(), "iterate 5 repeats lengthens", juce::String (i1.length() / (double) x.length(), 2) + " x");
    Chain c; c.seed = 7; c.ops = { br, mk (OpType::CycleShuffle, { 2, 1 }), mk (OpType::SpecBlur, { 10, 4, 1 }) };
    Audio o1, o2; renderChain (x, Region::all (x), c, o1); renderChain (x, Region::all (x), c, o2);
    c.seed = 8; Audio o3; renderChain (x, Region::all (x), c, o3);
    check (same (o1, o2) && ! same (o1, o3), "chain render deterministic per chain seed, differs across seeds");
}

static void testSpectral (double rate)
{
    std::cout << "-- spectral @ " << rate << " Hz" << std::endl;
    const Audio s = sine (rate, 2.0, 440.0, 0.4f);
    const Audio up = run (s, mk (OpType::SpecShift, { 12, 0, 8000 }));
    const double hz = peakHz (up.ch[0], rate, (size_t) (rate * 0.4));
    check (up.length() == s.length() && std::abs (hz / 880.0 - 1.0) < 0.005, "shift +12 = octave (440 -> 880), length kept", juce::String (hz, 2) + " Hz");
    const Audio dn = run (s, mk (OpType::SpecShift, { -7, 0, 8000 }));
    const double hz2 = peakHz (dn.ch[0], rate, (size_t) (rate * 0.4)), want2 = 440.0 * std::pow (2.0, -7.0 / 12.0);
    check (std::abs (hz2 / want2 - 1.0) < 0.005, "shift -7 st", juce::String (hz2, 2) + " vs " + juce::String (want2, 2));
    const Audio st = run (s, mk (OpType::SpecStretch, { 2, 0, 0 }));
    const double hz3 = peakHz (st.ch[0], rate, (size_t) (rate * 0.6));
    check (st.length() == 2 * s.length() && std::abs (hz3 / 440.0 - 1.0) < 0.005, "stretch 2x doubles length, keeps pitch", juce::String (st.length()) + " smp, " + juce::String (hz3, 2) + " Hz");
    check (rmsRange (st.ch[0], (size_t) (rate * 0.5), (size_t) (rate * 3.5)) > 0.2f, "stretch keeps the level",
           juce::String (rmsRange (st.ch[0], (size_t) (rate * 0.5), (size_t) (rate * 3.5)), 3));
    const Audio sq = run (s, mk (OpType::SpecStretch, { 0.5f, 0, 0 }));
    check (sq.length() == s.length() / 2, "stretch 0.5x halves length");
    // trace: bin-centred tones (2048-point frames); amplitudes above every neighbour lobe of the louder ones
    const double bin = rate / 2048.0;
    const int bins[] = { 20, 45, 70, 95, 120, 150, 180 }; const float amps[] = { 1.0f, 0.9f, 0.8f, 0.7f, 0.6f, 0.3f, 0.2f };
    std::vector<std::pair<double, float>> parts; for (int i = 0; i < 7; ++i) parts.push_back ({ bins[i] * bin, amps[i] * 0.12f });
    const Audio tx = tones (rate, 2.0, parts);
    for (int keep : { 1, 3, 5 })
    {
        const Audio tr = run (tx, mk (OpType::SpecTrace, { 11, (float) keep }));
        const auto sp = spectrum (tr.ch[0], (size_t) (rate * 0.5)), sp0 = spectrum (tx.ch[0], (size_t) (rate * 0.5));
        int survived = 0; juce::String d;
        for (int i = 0; i < 7; ++i) { const float rel = magAt (sp, bins[i] * bin, rate) / magAt (sp0, bins[i] * bin, rate); if (rel > 0.5f) ++survived; d << juce::String (juce::Decibels::gainToDecibels (rel), 0) << " "; }
        bool loudest = true; for (int i = 0; i < keep; ++i) loudest = loudest && magAt (sp, bins[i] * bin, rate) > 0.5f * magAt (sp0, bins[i] * bin, rate);
        check (survived == keep && loudest, "trace keep " + juce::String (keep) + " -> the " + juce::String (keep) + " loudest partials survive", "dB per partial: " + d);
    }
    // freeze: 0.5 s of tone then silence; freeze at 20 % sustains
    Audio fz = tones (rate, 2.0, { { 330.0, 0.3f }, { 495.0, 0.15f } });
    for (auto& c : fz.ch) for (size_t i = (size_t) (rate * 0.5); i < c.size(); ++i) c[i] = 0.0f;
    const Audio fr = run (fz, mk (OpType::SpecFreeze, { 11, 12.5f, 0 })), nf = run (fz, mk (OpType::SpecBlur, { 11, 1, 0 }));
    const float early = rmsRange (fz.ch[0], (size_t) (rate * 0.1), (size_t) (rate * 0.4)), late = rmsRange (fr.ch[0], (size_t) (rate * 1.4), (size_t) (rate * 1.9));
    check (late > 0.9f * early && rmsRange (nf.ch[0], (size_t) (rate * 1.4), (size_t) (rate * 1.9)) < 1.0e-3f, "freeze sustains the held spectrum after the source stops (> 0.9, same at every rate) [#5]",
           "late/early " + juce::String (late / early, 2));
    check (std::abs (peakHz (fr.ch[0], rate, (size_t) (rate * 0.8)) - 330.0) < 2.0, "freeze keeps the frozen pitch", juce::String (peakHz (fr.ch[0], rate, (size_t) (rate * 0.8)), 1) + " Hz");
    // blur 1 frame (no smearing) ~ identity (STFT round trip)
    const Audio bl = run (s, mk (OpType::SpecBlur, { 11, 1, 0 }));
    float err = 0; for (int i = 4096; i < s.length() - 4096; ++i) err = std::max (err, std::abs (bl.ch[0][(size_t) i] - s.ch[0][(size_t) i]));
    check (err < 1.0e-3f, "blur width 1 (own frame) is a clean STFT round trip", "max err " + juce::String (err, 6));
    const Audio bw = run (drumLoop (rate, 1), mk (OpType::SpecBlur, { 11, 16, 0 }));
    check (finite (bw) && bw.rms() > 0.01f, "blur 16 frames renders");
}

static void testChainAndState()
{
    std::cout << "-- chain + state" << std::endl;
    const Audio x = synthPhrase (48000);
    Chain a; a.ops = { Op (OpType::Reverse), mk (OpType::FadeIn, { 300, 0 }) };
    Chain b; b.ops = { mk (OpType::FadeIn, { 300, 0 }), Op (OpType::Reverse) };
    Audio oa, ob; renderChain (x, Region::all (x), a, oa); renderChain (x, Region::all (x), b, ob);
    check (! same (oa, ob) && maxDiff (oa, ob) > 0.01f, "chain order matters (reverse->fade != fade->reverse)", juce::String (maxDiff (oa, ob), 3));
    Chain c; c.seed = 1234; c.ops = { mk (OpType::Brassage, { 33, 2.5f, 10, 2, 1 }), mk (OpType::CycleOmit, { 2, 3, 1, 1 }) };
    Op e (OpType::Envelope); e.env = { { 0, 0.2f }, { 0.3f, 0.9f }, { 1, 0 } }; c.ops.push_back (e); c.ops[1].enabled = false;
    const Chain d = Chain::fromXml (c.toXml());
    check (d.seed == 1234 && d.ops.size() == 3 && ! d.ops[1].enabled && d.ops[0].p == c.ops[0].p && d.ops[2].env.size() == 3, "chain state round trip (XML)");
    juce::ValueTree v = c.toValueTree(); juce::ValueTree unk ("OP"); unk.setProperty ("id", "fromTheFuture", nullptr); v.addChild (unk, 1, nullptr);
    check (Chain::fromValueTree (v).ops.size() == 3, "unknown op ids are skipped on load");
    Audio od, oc; renderChain (x, Region::all (x), c, oc); renderChain (x, Region::all (x), d, od);
    check (same (oc, od), "restored chain renders bit-identically");
    // region: only the region changes; outside bit-exact; length change shifts the tail
    Audio y = x; Region r { 10000, 20000 };
    Chain g; g.ops = { mk (OpType::CycleRepeat, { 1, 2, 1 }) };
    check (applyChain (y, r, g), "applyChain on a region");
    bool head = std::equal (x.ch[0].begin(), x.ch[0].begin() + 10000, y.ch[0].begin());
    bool tail = std::equal (x.ch[0].begin() + 30000, x.ch[0].end(), y.ch[0].begin() + r.end());
    check (head && tail && r.start == 10000 && y.length() == x.length() - 20000 + r.length, "region apply: head + tail bit-exact, region grows", "new region " + juce::String (r.length));
}

static void testCancelUndo()
{
    std::cout << "-- cancel + undo + memory" << std::endl;
    const Audio x = drumLoop (48000, 8);
    Progress pg; pg.cancel.store (true); Audio out;
    Chain c; c.ops = { mk (OpType::SpecStretch, { 3, 0, 0 }) };
    check (! renderChain (x, Region::all (x), c, out, &pg), "pre-cancelled render returns false");
    pg.reset(); Audio y = x; Region r = Region::all (y);
    auto t0 = std::chrono::steady_clock::now(); Audio full; renderChain (x, Region::all (x), c, full, &pg);
    const double fullMs = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    pg.reset();
    std::thread killer ([&] { std::this_thread::sleep_for (std::chrono::milliseconds ((int) std::max (2.0, fullMs * 0.1))); pg.cancel.store (true); });
    t0 = std::chrono::steady_clock::now();
    const bool ok = applyChain (y, r, c, &pg);
    const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    killer.join();
    check (! ok && same (y, x) && ms < fullMs * 0.6, "cancel mid-render stops early and leaves the sound untouched",
           juce::String (ms, 1) + " ms vs full " + juce::String (fullMs, 1) + " ms, progress " + juce::String (pg.fraction.load(), 2));
    // undo bit-exact over length-changing steps
    UndoStack u; Audio a = x; std::vector<Audio> states { a };
    const Op steps[] = { mk (OpType::CycleRepeat, { 1, 3, 0.5f }), Op (OpType::Reverse), mk (OpType::Brassage, { 20, 2, 5, 1, 0.7f }), mk (OpType::Extend, { 2.5f, 10, 1 }) };
    const Region regs[] = { { 1000, 30000 }, { 0, 100000 }, { 50000, 40000 }, { 20000, 10000 } };
    for (int i = 0; i < 4; ++i)
    {
        Region rr = regs[i]; auto cap = UndoStack::capture (a, rr, "step"); applyOp (a, rr, steps[i], 9); u.push (std::move (cap), rr); states.push_back (a);
    }
    bool exact = true; for (int i = 3; i >= 0; --i) { Region rr; u.undo (a, rr); exact = exact && same (a, states[(size_t) i]); }
    check (exact && ! u.canUndo(), "undo x4 bit-exact back to the original (lengths changed on the way)");
    bool redo = true; for (int i = 1; i <= 4; ++i) { Region rr; u.redo (a, rr); redo = redo && same (a, states[(size_t) i]); }
    check (redo, "redo x4 bit-exact");
    // budget
    UndoStack small (2u << 20); Audio m = sine (48000, 4.0, 100.0);   // 4 s stereo = 1.5 MB
    for (int i = 0; i < 8; ++i) { Region rr { 0, 48000 * 2 }; auto cap = UndoStack::capture (m, rr, "x"); applyOp (m, rr, Op (OpType::Reverse), 1); small.push (std::move (cap), rr); }
    check (small.bytes() <= small.getBudget() && small.depth() >= 1, "undo memory budget respected (2 MB)", juce::String ((double) small.bytes() / 1048576.0, 2) + " MB, depth " + juce::String (small.depth()));
    UndoStack tiny (1024); { Region rr { 0, 48000 }; auto cap = UndoStack::capture (m, rr, "big"); tiny.push (std::move (cap), rr); }
    check (tiny.depth() == 1, "newest step always kept even above the budget");
}

static void testEdgeCases (double rate)
{
    std::cout << "-- edge cases @ " << rate << " Hz" << std::endl;
    Audio silence; silence.rate = rate; silence.resize (2, (int) rate);
    Audio dc = silence; for (auto& c : dc.ch) for (float& v : c) v = 0.5f;
    Audio one; one.rate = rate; one.resize (2, 1); one.ch[0][0] = 0.7f; one.ch[1][0] = -0.3f;
    Audio none; none.rate = rate; none.resize (2, 0);
    Audio mono = sine (rate, 0.3, 300, 0.5f, 1);
    Audio quad = sine (rate, 0.3, 300, 0.5f, 3);
    int bad = 0; juce::String which;
    for (int t = 0; t < (int) OpType::Count; ++t)
        for (const Audio* in : { &silence, &dc, &one, &none, &mono, &quad })
        {
            Audio o; const bool ok = renderOp (*in, Op ((OpType) t), 3, o);
            const bool good = ok && finite (o) && o.channels() == in->channels() && (in->length() == 0 || o.length() > 0) && o.peak() < 16.0f;
            if (! good) { ++bad; which << info ((OpType) t).id << "(" << in->length() << "x" << in->channels() << ") "; }
        }
    check (bad == 0, "every op on silence / DC / 1 sample / 0 samples / mono / 3 ch: finite, sane", which);
    Audio o; renderOp (silence, mk (OpType::Normalise, { 0 }), 1, o);
    check (o.peak() == 0.0f, "normalise silence stays silent");
}

static void testLabThread()
{
    std::cout << "-- Lab worker (message loop)" << std::endl;
    Lab lab; int changes = 0, commits = 0; lab.onChanged = [&] { ++changes; }; lab.onCommitted = [&] (const Audio&) { ++commits; };
    const Audio x = drumLoop (48000, 2); lab.setAudio (x);
    lab.chain.ops = { mk (OpType::CycleShuffle, { 4, 1 }), mk (OpType::Normalise, { -1 }) }; lab.chainChanged();
    lab.setRegion ({ 24000, 48000 });
    auto wait = [&] { for (int i = 0; i < 1000 && lab.busy(); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (10); juce::MessageManager::getInstance()->runDispatchLoopUntil (50); };
    lab.preview(); wait();
    check (lab.previewValid() && lab.previewAudio->length() == 48000 && same (lab.audio, x), "preview renders the region without committing");
    Audio expect; renderChain (x, { 24000, 48000 }, lab.chain, expect);
    check (same (*lab.previewAudio, expect), "worker preview = direct render (bit-exact)");
    lab.apply(); wait();
    check (commits == 1 && lab.undo.canUndo() && ! same (lab.audio, x), "apply commits one undo step");
    lab.undoStep();
    check (same (lab.audio, x) && commits == 2, "Lab undo restores bit-exact");
    lab.redoStep(); lab.bounce(); wait();
    check (lab.chain.ops.empty() && lab.undo.depth() == 2, "bounce applies and clears the chain");
    lab.chain.ops = { mk (OpType::SpecStretch, { 4, 0, 0 }) }; lab.setRegion (Region::all (lab.audio));
    const Audio before = lab.audio; lab.apply(); juce::MessageManager::getInstance()->runDispatchLoopUntil (5); lab.cancel(); wait();
    check (same (lab.audio, before) && ! lab.busy(), "Lab cancel leaves the sound untouched");
}

static void testAudition()
{
    std::cout << "-- audition player" << std::endl;
    AuditionPlayer p; p.prepare (48000);
    auto clip = std::make_shared<Audio> (sine (44100, 0.25, 500, 0.8f));   // other rate: resampled
    juce::AudioBuffer<float> buf (2, 512); std::vector<float> all;
    p.play (clip, true);
    for (int b = 0; b < 100; ++b) { buf.clear(); p.render (buf, 0, 512); all.insert (all.end(), buf.getReadPointer (0), buf.getReadPointer (0) + 512); }
    p.stop(); for (int b = 0; b < 4; ++b) { buf.clear(); p.render (buf, 0, 512); all.insert (all.end(), buf.getReadPointer (0), buf.getReadPointer (0) + 512); }
    const float d1 = maxAbsDiff (all, 1), ideal = 0.8f * (float) (kTwoPi * 500 / 48000);
    check (d1 < ideal * 1.5f, "audition: fades at start / loop joins / stop (no jump)", "max |d1| " + juce::String (d1, 4) + " vs sine slope " + juce::String (ideal, 4));
    check (! p.playing() && std::abs (all.back()) == 0.0f, "audition: stop ends in silence");
    const double hz = peakHz (std::vector<float> (all.begin() + 2000, all.begin() + 12000), 48000);
    check (std::abs (hz - 500) < 5, "audition resamples 44.1k clip on a 48k host", juce::String (hz, 1) + " Hz");
    // switching clips while playing (and replaying without a render in between) restarts cleanly without a jump
    auto other = std::make_shared<Audio> (sine (48000, 0.05, 300, 0.8f)); all.clear();
    p.play (clip, false);
    for (int b = 0; b < 6; ++b) { buf.clear(); p.render (buf, 0, 512); all.insert (all.end(), buf.getReadPointer (0), buf.getReadPointer (0) + 512); }
    p.play (other, false); p.play (clip, false); p.play (other, false);
    for (int b = 0; b < 12; ++b) { buf.clear(); p.render (buf, 0, 512); all.insert (all.end(), buf.getReadPointer (0), buf.getReadPointer (0) + 512); }
    const float d1b = maxAbsDiff (all, 1);
    check (d1b < ideal * 1.3f && ! p.playing(), "audition: replacing a playing clip ramps out, short clip ends cleanly", "max |d1| " + juce::String (d1b, 4));
    p.release();
}

// ================================================================ final-gate punch list regressions (#n = PUNCHLIST item)
#if defined(__SANITIZE_ADDRESS__)
static constexpr double kSlow = 8.0;
#else
static constexpr double kSlow = 1.0;
#endif
static void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }
static bool waitIdle (Lab& lab, int ms = 60000) { const double t0 = juce::Time::getMillisecondCounterHiRes(); while (lab.busy() && juce::Time::getMillisecondCounterHiRes() - t0 < ms) pump (5); pump (20); return ! lab.busy(); }
static float jumpAt (const std::vector<float>& x, int i) { return (i <= 0 || i >= (int) x.size()) ? 0.0f : std::abs (x[(size_t) i] - x[(size_t) i - 1]); }
static float maxD1 (const std::vector<float>& x, int a, int b) { float m = 0; for (int i = std::max (1, a); i < std::min ((int) x.size(), b); ++i) m = std::max (m, std::abs (x[(size_t) i] - x[(size_t) i - 1])); return m; }
static double msSince (double t0) { return juce::Time::getMillisecondCounterHiRes() - t0; }
static Audio noise (double rate, double secs, float amp, uint32_t seed, int chans = 2)
{
    Audio a; a.rate = rate; a.resize (chans, (int) (secs * rate)); Rng r (seed);
    for (auto& c : a.ch) for (float& v : c) v = amp * r.bi();
    return a;
}

namespace plab {
struct PanelTestAccess {
    static juce::Array<juce::TextButton*> buttons (ProcessLabPanel& p)
    {
        return { &p.addB, &p.delB, &p.upB, &p.downB, &p.dupB, &p.previewB, &p.autoB, &p.playSrcB, &p.playPrevB, &p.stopB, &p.loopB, &p.applyB, &p.bounceB,
                 &p.againB, &p.sliceB, &p.undoB, &p.redoB, &p.cancelB, p.dragB.get() };
    }
    static juce::File drag (ProcessLabPanel& p) { return p.writeDragFile(); }
    static juce::String status (ProcessLabPanel& p) { return p.status; }
    static void bounceNew (ProcessLabPanel& p) { p.bounceNewB.triggerClick(); }
    static float playhead (ProcessLabPanel& p) { return p.playhead01(); }
    static void playSrc (ProcessLabPanel& p) { p.playSource(); }
};
}

// #1 stale preview after an edit during the render
static void testStalePreview()
{
    std::cout << "-- #1 stale preview" << std::endl;
    Lab lab; lab.setAudio (sine (48000, 3.0, 220.0));
    lab.chain.ops = { mk (OpType::SpecStretch, { 4, 0, 0 }) }; lab.chainChanged();
    lab.preview(); pump (10);
    lab.chain.ops[0].p[0] = 0.5f; lab.chainChanged();          // knob moved mid-render
    waitIdle (lab); pump (100);
    const bool stale = lab.previewValid() && lab.previewAudio->length() != 72000;
    check (! stale, "a preview of the old chain is never marked valid", lab.previewValid() ? "len " + juce::String (lab.previewAudio->length()) : juce::String ("invalid (good)"));
    lab.preview(); waitIdle (lab);
    check (lab.previewValid() && lab.previewAudio->length() == 72000, "the next preview matches the current chain (0.5x)", juce::String (lab.previewValid() ? lab.previewAudio->length() : -1));
    lab.setRegion ({ 1000, 48000 });
    check (! lab.previewValid(), "a region edit invalidates the preview");
}

// #2 length caps, iterate walk, worker failures
static void testCaps()
{
    std::cout << "-- #2 output caps" << std::endl;
    const Audio x = sine (48000, 0.05, 220.0);
    double worst = 0;
    for (uint32_t seed = 1; seed <= 300; ++seed) { Audio o; renderOp (x, mk (OpType::Iterate, { 32, 12, 0, 0, 0 }), seed, o); worst = std::max (worst, o.length() / (double) x.length()); }
    check (worst <= 32.0 * 4.0 + 1.0, "iterate 32 x pitch 12: worst length over 300 seeds <= 128x (walk reflects at +/-24 st)", juce::String (worst, 1) + " x");
    const Audio y = sine (48000, 10.0, 220.0, 0.3f, 1);
    Chain c; c.ops = { mk (OpType::Extend, { 16, 0, 0 }), mk (OpType::Extend, { 16, 0, 0 }), mk (OpType::Extend, { 16, 0, 0 }) };
    Progress pg; Audio o; const double t0 = juce::Time::getMillisecondCounterHiRes();
    const bool ok = renderChain (y, Region::all (y), c, o, &pg);
    check (ok && o.length() <= maxOutSamples (48000) && pg.note.contains ("capped"), "3 x EXTEND x16 on 10 s stops at the cap (600 s) with a note",
           juce::String (o.length() / 48000.0, 1) + " s in " + juce::String (msSince (t0), 0) + " ms: " + pg.note);
    // a huge request through the Lab: finishes, is not stuck busy, reports the cap
    Lab lab; lab.setAudio (sine (48000, 2.0, 220.0, 0.3f, 1)); juce::String msg; lab.onMessage = [&] (const juce::String& m) { msg = m; };
    lab.chain.ops = { mk (OpType::Brassage, { 40, 2, 0, 0, 4 }), mk (OpType::Extend, { 16, 0, 0 }), mk (OpType::Extend, { 16, 0, 0 }) }; lab.chainChanged();
    lab.preview(); const bool idle = waitIdle (lab, 120000);
    check (idle && lab.previewValid() && lab.previewAudio->length() <= maxOutSamples (48000) && msg.contains ("capped"), "Lab: capped job finishes, busy() clears, message says so", msg);
}

// #3 cancel latency per op, non-blocking cancel, envelope caps
static void testCancelLatency()
{
    std::cout << "-- #3 cancel latency" << std::endl;
    const Audio x = drumLoop (48000, 16);   // ~31 s stereo
    double worstMs = 0; juce::String worstOp;
    for (int t = 0; t < (int) OpType::Count; ++t)
    {
        Op op ((OpType) t);
        if (t == (int) OpType::Envelope) { op.env.clear(); for (int i = 0; i < 300; ++i) op.env.push_back ({ i / 299.0f, (i % 2) ? 1.0f : 0.2f, 0 }); }
        Progress pg; Audio out; std::atomic<bool> done { false }; double stopMs = 0;
        std::thread w ([&] { renderOp (x, op, 1, out, &pg); done = true; });
        std::this_thread::sleep_for (std::chrono::milliseconds (15));
        const double t0 = juce::Time::getMillisecondCounterHiRes(); pg.cancel.store (true);
        while (! done.load()) std::this_thread::sleep_for (std::chrono::microseconds (200));
        stopMs = msSince (t0); w.join();
        if (stopMs > worstMs) { worstMs = stopMs; worstOp = info ((OpType) t).id; }
    }
    check (worstMs < 250.0 * kSlow, "every op stops within 250 ms of cancel (31 s stereo input)", "worst " + worstOp + " " + juce::String (worstMs, 1) + " ms");
    Op e (OpType::Envelope); e.env.clear(); for (int i = 0; i < 20000; ++i) e.env.push_back ({ i / 19999.0f, (i % 2) ? 1.0f : 0.0f, 0 });
    const Audio ten = sine (48000, 10.0, 220.0);
    double t0 = juce::Time::getMillisecondCounterHiRes(); Audio o; renderOp (ten, e, 1, o); const double envMs = msSince (t0);
    check (envMs < 400.0 * kSlow, "envelope with 20k points on 10 s: capped to 256 points, incremental lookup", juce::String (envMs, 0) + " ms");
    juce::ValueTree v ("OP"); v.setProperty ("id", "envelope", nullptr); juce::ValueTree ev ("ENV");
    for (int i = 0; i < 100000; ++i) { juce::ValueTree q ("PT"); q.setProperty ("x", i / 1e5, nullptr); q.setProperty ("y", 0.5, nullptr); ev.appendChild (q, nullptr); }
    v.appendChild (ev, nullptr); Op lo; Op::fromValueTree (v, lo);
    check ((int) lo.env.size() == kMaxEnvPoints, "envelope points capped at 256 on load", juce::String ((int) lo.env.size()));
    // Lab cancel never blocks the message thread
    Lab lab; lab.setAudio (drumLoop (48000, 16)); lab.chain.ops = { mk (OpType::SpecStretch, { 6, 0, 0 }) }; lab.chainChanged();
    lab.apply(); std::this_thread::sleep_for (std::chrono::milliseconds (30));
    t0 = juce::Time::getMillisecondCounterHiRes(); lab.cancel(); const double cMs = msSince (t0);
    lab.preview(); t0 = juce::Time::getMillisecondCounterHiRes(); lab.chainChanged(); const double c2 = msSince (t0);
    waitIdle (lab);
    check (cMs < 5.0 && c2 < 5.0 && ! lab.undo.canUndo(), "Lab::cancel() / chainChanged() return at once (no thread kill), nothing committed", juce::String (cMs, 2) + " / " + juce::String (c2, 2) + " ms");
}

// #4 region edges: no clicks where the result meets the file
static void testRegionEdges (double rate)
{
    std::cout << "-- #4 region edges @ " << rate << " Hz" << std::endl;
    const Audio src = sine (rate, 3.0, 220.0, 0.5f);
    const float nat = maxD1 (src.ch[0], 1, src.length());
    struct C { const char* name; Op op; float insideK; };
    Op env (OpType::Envelope); env.env = { { 0, 0.3f, 0 }, { 0.5f, 1, 0 }, { 1, 0.4f, 0 } };
    const C cs[] = { { "brassage", mk (OpType::Brassage, { 40, 2, 20, 0, 1 }), 2.2f }, { "stretch 2x", mk (OpType::SpecStretch, { 2, 0, 0 }), 1.6f },
                     { "iterate", mk (OpType::Iterate, { 4, 1, 0, 3, 0 }), 2.0f }, { "extend 2.3x", mk (OpType::Extend, { 2.3f, 20, 0 }), 1.6f },
                     { "trace 4", mk (OpType::SpecTrace, { 11, 4 }), 1.6f }, { "normalise 0 dB (2x gain)", mk (OpType::Normalise, { 0, 0 }), 2.1f }, { "envelope", env, 1.6f },
                     { "shift +7", mk (OpType::SpecShift, { 7, 0, 8000 }), 2.0f }, { "freeze", mk (OpType::SpecFreeze, { 11, 50, 0 }), 1.6f },
                     { "blur 200 ms", mk (OpType::SpecBlur, { 11, 200, 0 }), 1.6f }, { "varispeed -5", mk (OpType::Varispeed, { -5, 0 }), 1.6f },
                     { "drive", mk (OpType::Drive, { 0, 6, 1, -6 }), 3.0f }, { "gain -12", mk (OpType::Gain, { -12 }), 1.6f } };
    float worstEdge = 0, worstIn = 0; juce::String we, wi;
    for (auto& c : cs)
    {
        Audio a = src; Region r { (int) (rate * 1.0) + 37, (int) rate };
        Chain ch; ch.ops = { c.op }; applyChain (a, r, ch);
        const auto& y = a.ch[0];
        const float e = std::max (jumpAt (y, r.start), jumpAt (y, r.end())) / nat;
        const float in = maxD1 (y, r.start + 1, r.end() - 1) / nat;
        if (e > worstEdge) { worstEdge = e; we = c.name; }
        if (in / c.insideK > worstIn) { worstIn = in / c.insideK; wi = c.name + juce::String (" ") + juce::String (in, 2) + "x"; }
    }
    check (worstEdge < 2.5f, "edge jumps <= 2.5x the natural step for every op (was up to 44x)", "worst " + we + " " + juce::String (worstEdge, 2) + "x");
    check (worstIn <= 1.0f, "no clicks inside: max step within each op's bound (brassage 2.2x, iterate 2x, normalise 2.1x, drive 3x, others 1.6x)", "worst " + wi);
    // sample-exact edits stay bit-exact (no blend where the joint does not click)
    Audio a = sine (rate, 1.0, 100.0, 0.5f, 1); for (int i = 0; i < a.length(); ++i) a.ch[0][(size_t) i] *= (float) std::abs (std::sin (juce::MathConstants<double>::pi * i / (a.length() / 2.0)));
    const Audio b = a; Region r { a.length() / 2, a.length() - a.length() / 2 }; Chain rc; rc.ops = { Op (OpType::Reverse) }; applyChain (a, r, rc);
    bool exact = true; for (int i = 0; i < r.length; ++i) exact = exact && a.ch[0][(size_t) (r.start + i)] == b.ch[0][(size_t) (b.length() - 1 - i)];
    check (exact, "reversing between quiet points stays bit-exact (edge blend only where it would click)");
}

// #5 sample-rate independence of the spectral ops
static void testRateIndependence()
{
    std::cout << "-- #5 spectral ops vs rate" << std::endl;
    juce::String d; float lo = 1e9f, hi = 0;
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        Audio click; click.rate = rate; click.resize (1, (int) (rate * 1.5)); for (int i = 0; i < (int) (rate * 0.01); ++i) click.ch[0][(size_t) (rate * 0.3) + (size_t) i] = 0.8f * (float) std::sin (i * 0.3);
        const Audio b = run (click, mk (OpType::SpecBlur, { 11, 160, 0 }));
        float pk = b.peak(); int last = 0; for (int i = 0; i < b.length(); ++i) if (std::abs (b.ch[0][(size_t) i]) > pk * 0.01f) last = i;
        const float tailMs = (float) (1000.0 * (last - rate * 0.31) / rate); lo = std::min (lo, tailMs); hi = std::max (hi, tailMs);
        d << juce::String ((int) rate) << ": " << juce::String (tailMs, 0) << " ms  ";
    }
    check (hi / lo < 1.3f, "blur 160 ms smears the same time at 44.1 / 48 / 96 kHz", d);
    check (fftOrderFor (11, 96000) == 12 && fftOrderFor (11, 44100) == 11 && fftOrderFor (13, 192000) == 15, "FFT order scales with the rate");
    juce::ValueTree v ("OP"); v.setProperty ("id", "specBlur", nullptr); v.setProperty ("p1", 16.0, nullptr); Op o; Op::fromValueTree (v, o);
    check (std::abs (o.p[1] - 170.67f) < 0.1f, "old states: blur 16 frames -> 170.7 ms", juce::String (o.p[1], 2));
}

// #6 wavecycles: stereo, DC, noise
static void testCyclesRobust()
{
    std::cout << "-- #6 wavecycles robust" << std::endl;
    Audio st; st.rate = 48000; st.resize (2, 48000);
    for (int i = 0; i < 48000; ++i) { st.ch[0][(size_t) i] = 0.5f * (float) std::sin (kTwoPi * 220 * i / 48000); st.ch[1][(size_t) i] = 0.5f * (float) std::sin (kTwoPi * 331 * i / 48000); }
    const Audio sh = run (st, mk (OpType::CycleShuffle, { 1, 1, 0.5f }), 3);
    const float natL = maxD1 (st.ch[0], 1, 48000), natR = maxD1 (st.ch[1], 1, 48000);
    const float dL = maxD1 (sh.ch[0], 1, sh.length()), dR = maxD1 (sh.ch[1], 1, sh.length());
    check (dL < 3 * natL && dR < 3 * natR, "stereo L 220 / R 331 shuffle: both channels click-free (was 1.0)", "L " + juce::String (dL, 3) + " R " + juce::String (dR, 3) + " (natural " + juce::String (natL, 3) + "/" + juce::String (natR, 3) + ")");
    Audio dc = sine (48000, 1.0, 220.0, 0.4f, 1); for (float& v : dc.ch[0]) v += 0.5f;
    const int nDc = (int) cycleUnits (dc, 1).size() - 1;
    check (std::abs (nDc - 220) <= 3, "DC offset 0.5: ~220 cycles found (was 0)", juce::String (nDc));
    Audio nz = sine (48000, 1.0, 110.0, 0.5f, 1); Rng r (5); for (float& v : nz.ch[0]) v += 0.3f * r.bi();
    const int nNz = (int) cycleUnits (nz, 1).size() - 1;
    check (std::abs (nNz - 110) <= 12, "110 Hz + 0.3 noise: ~110 cycles (was 4047)", juce::String (nNz));
    Audio sil; sil.rate = 48000; sil.resize (1, 48000); Progress pg; Audio o; renderOp (sil, Op (OpType::CycleShuffle), 1, o, &pg);
    check (pg.note.contains ("no wavecycles"), "silence: reported, not a silent no-op", pg.note);
}

// #7 + #8 job size, APPLY reuses the preview
static void testJobSizeAndReuse()
{
    std::cout << "-- #7 #8 job copy + preview reuse" << std::endl;
    Lab lab; lab.setAudio (sine (48000, 180.0, 220.0, 0.3f));   // 3 min stereo
    lab.chain.ops = { Op (OpType::Reverse) }; lab.chainChanged();
    lab.setRegion ({ 48000 * 60, 2400 }); lab.preview();
    const size_t bytes = lab.lastJobBytes(), file = lab.audio.bytes();
    waitIdle (lab);
    check (bytes < (size_t) (2400 + 2 * contextSamples (48000)) * 2 * sizeof (float) + 64, "PREVIEW copies region + context only", juce::String ((double) bytes / 1048576.0, 2) + " MB of " + juce::String ((double) file / 1048576.0, 1) + " MB");
    int commits = 0; lab.onCommitted = [&] (const Audio&) { ++commits; };
    lab.chain.ops = { mk (OpType::SpecStretch, { 3, 0, 0 }) }; lab.chainChanged(); lab.preview(); waitIdle (lab);
    const auto prev = lab.previewAudio; const Region r = lab.region;
    lab.apply();
    bool same2 = commits == 1 && ! lab.busy();
    for (int i = 0; same2 && i < prev->length(); i += 7) same2 = lab.audio.ch[0][(size_t) (r.start + i)] == prev->ch[0][(size_t) i];
    check (same2, "APPLY after a valid PREVIEW commits that preview at once (no second render)");
}

// #9 + #26 audition
static void testAuditionLoop()
{
    std::cout << "-- #9 #26 audition" << std::endl;
    AuditionPlayer p; p.prepare (48000);
    auto dc = std::make_shared<Audio>(); dc->rate = 48000; dc->resize (2, 4800); for (auto& c : dc->ch) for (float& v : c) v = 0.5f;
    p.play (dc, true); juce::AudioBuffer<float> buf (2, 256); float mn = 9, mx = 0; int blocks = 0;
    for (int b = 0; b < 200; ++b) { buf.clear(); p.render (buf, 0, 256); if (++blocks > 3) for (int i = 0; i < 256; ++i) { mn = std::min (mn, buf.getSample (0, i)); mx = std::max (mx, buf.getSample (0, i)); } }
    check (mn > 0.49f && mx < 0.51f, "looped DC clip: level stays 0.5 across every loop join (was 0.002)", juce::String (mn, 4) + " .. " + juce::String (mx, 4));
    p.stop(); for (int b = 0; b < 4; ++b) { buf.clear(); p.render (buf, 0, 256); }
    auto tone = std::make_shared<Audio> (sine (48000, 0.1, 1000.0, 0.5f)); p.play (tone, true);
    std::vector<float> all; for (int b = 0; b < 100; ++b) { buf.clear(); p.render (buf, 0, 256); all.insert (all.end(), buf.getReadPointer (0), buf.getReadPointer (0) + 256); }
    check (maxD1 (all, 1024, (int) all.size()) < 0.5f * (float) (kTwoPi * 1000 / 48000) * 1.5f, "looped tone: no jump at the joins");
    p.stop(); for (int b = 0; b < 4; ++b) { buf.clear(); p.render (buf, 0, 256); }
    AuditionPlayer q; q.prepare (44100);
    auto hi = std::make_shared<Audio> (sine (96000, 0.5, 40000.0, 0.5f, 1)); q.play (hi, false);
    std::vector<float> o2; for (int b = 0; b < 60; ++b) { buf.clear(); q.render (buf, 0, 256); o2.insert (o2.end(), buf.getReadPointer (0), buf.getReadPointer (0) + 256); }
    const float r = rmsRange (o2, 2000, 12000);
    check (r < 0.005f, "96 kHz clip with 40 kHz content on a 44.1 kHz host: no alias (anti-aliased resample)", "rms " + juce::String (r, 5));
    q.release(); p.release();
}

// #10 determinism of the stretcher (random above 2x) with our own engine
static void testStretchDeterminism()
{
    std::cout << "-- #10 determinism" << std::endl;
    const Audio x = synthPhrase (48000);
    const Op op = mk (OpType::SpecStretch, { 3, 0, 0 });
    const Audio a = run (x, op, 9), b = run (x, op, 9), c = run (x, op, 10);
    check (same (a, b) && ! same (a, c), "stretch 3x (randomised bins): bit-identical per seed, differs across seeds");
}

struct PunchHost : Host {
    Audio a; int sets = 0, news = 0; Audio lastNew; std::vector<Region> sl; std::shared_ptr<const Audio> last;
    Audio getBuffer() override { return a; }
    void setBuffer (const Audio& b) override { a = b; ++sets; }
    double sampleRate() override { return 48000; }
    void audition (std::shared_ptr<const Audio> c, bool) override { last = c; }
    double tempoBpm() override { return 120.0; }
    std::vector<Region> slices() override { return sl; }
    bool bounceToNew (const Audio& b) override { lastNew = b; ++news; return true; }
    bool canBounceToNew() override { return true; }
    bool allowExport (juce::Component*) override { return true; }   // a licensed host (the default refuses: fail closed)
};

// #11 #12 #13 #18 #19 #20 #22 #24 #25 panel + workflow
static void testPanelWorkflow()
{
    std::cout << "-- #11 #13 #18-#25 panel workflow" << std::endl;
    PunchHost h; h.a = drumLoop (48000, 2, 120);
    ProcessLabPanel p (h); p.setSize (980, 560);
    auto& lab = p.lab();
    // #11 zoom / snap / playhead
    p.setView (48000, 4800);
    check (p.view().start == 48000 && p.view().length == 4800, "zoom: view set to 0.1 s at 1 s");
    p.setSnap (ProcessLabPanel::SnapZero);
    const int z = p.snapPosition (48000 + 123);
    const auto m = monoMix (h.a).ch[0];
    check (z > 0 && m[(size_t) z - 1] < 0 && m[(size_t) z] >= 0, "snap to zero crossing", juce::String (z));
    p.setSnap (ProcessLabPanel::SnapTransient); p.setView (0, h.a.length());
    const int beat = 24000; const int tsn = p.snapPosition (beat * 2 + 300);
    check (std::abs (tsn - beat * 2) < 120, "snap to transient (a kick at 1.0 s)", juce::String (tsn - beat * 2) + " samples off");
    p.setSnap (ProcessLabPanel::SnapGrid);
    check (p.snapPosition (6100) == 6000, "snap to 1/16 grid at the host tempo (120 bpm)", juce::String (p.snapPosition (6100)));
    PanelTestAccess::playSrc (p); pump (60);
    check (PanelTestAccess::playhead (p) > 0.0f, "playhead runs while auditioning", juce::String (PanelTestAccess::playhead (p), 3));
    // #18 auto preview
    lab.setRegion ({ 0, 24000 }); lab.chain.ops = { Op (OpType::Reverse) }; lab.chainChanged();
    p.setAutoPreview (true); for (int i = 0; i < 60 && ! lab.previewValid(); ++i) pump (10);
    check (lab.previewValid(), "auto preview after an edit (150 ms debounce)");
    // #13 bounce to new keeps the original; repeat last; per slice; shortcuts; drag file
    const Audio orig = lab.audio;
    PanelTestAccess::bounceNew (p); pump (50);
    check (h.news == 1 && same (lab.audio, orig) && h.lastNew.length() == 24000, "BOUNCE NEW hands the result over and keeps the original");
    lab.apply(); waitIdle (lab);
    const Audio once = lab.audio; lab.repeatLast(); waitIdle (lab);
    check (same (lab.audio, orig) && ! same (once, orig), "AGAIN repeats the last chain (reverse twice = original)");
    h.sl = { { 0, 12000 }, { 24000, 12000 }, { 48000, 12000 } };
    lab.chain.ops = { Op (OpType::Reverse) }; lab.chainChanged(); const Audio beforeSl = lab.audio;
    lab.applyPerSlice (h.slices()); waitIdle (lab);
    bool sl = true; for (auto s : h.sl) for (int i = 200; i < s.length - 200; i += 5) sl = sl && lab.audio.ch[1][(size_t) (s.start + i)] == beforeSl.ch[1][(size_t) (s.end() - 1 - i)];
    const int depth = lab.undo.depth(); lab.undoStep();
    check (sl && same (lab.audio, beforeSl), "PER SLICE reverses every slice in place; one UNDO brings all back", "undo depth was " + juce::String (depth));
    lab.redoStep();
    p.keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0));
    check (same (lab.audio, beforeSl), "Ctrl/Cmd+Z undoes");
    const auto f = PanelTestAccess::drag (p);
    check (f.existsAsFile() && f.getSize() > 1000, "DRAG writes a WAV of the preview / region", f.getFileName()); f.deleteFile();
    {
        struct NoExportHost : PunchHost { bool allowExport (juce::Component*) override { return false; } } nh; nh.a = h.a;
        ProcessLabPanel q (nh); q.setSize (980, 560); q.lab().setRegion ({ 0, 24000 });
        const auto none = PanelTestAccess::drag (q);
        check (! none.existsAsFile() && PanelTestAccess::status (q) == "Exporting is part of the full version" && PanelTestAccess::drag (p).existsAsFile(),
               "allowExport() false: DRAG refused with a message; true: DRAG works", PanelTestAccess::status (q));
    }
    // #19 choice + toggle params
    check (info (OpType::CycleReverse).params[1].kind == Choice && choiceNames (info (OpType::CycleReverse).params[1]).size() == 3 && info (OpType::SpecShift).params[1].kind == Toggle,
           "binary / enumerated params are toggles / choices");
    // #20 every button label fits at 640 x 360
    p.setSize (640, 360); juce::String cut;
    for (auto* b : PanelTestAccess::buttons (p))
        if (b->isVisible() && juce::GlyphArrangement::getStringWidthInt (ghosty::font (juce::jmin (13.0f, (float) b->getHeight() * 0.45f)), b->getButtonText()) > b->getWidth() - 4) cut << b->getButtonText() << " ";
    check (cut.isEmpty(), "640 x 360: every button label fits (two button rows)", cut);
    p.setSize (980, 560);
    // #22 selection reset after BOUNCE
    lab.chain.ops = { Op (OpType::Reverse) }; lab.chainChanged(); p.selectOp (0); lab.bounce(); waitIdle (lab); pump (50);
    check (p.selectedOp() == -1, "BOUNCE clears the chain and the selection");
    // #24 undo name from the job's chain; BOUNCE keeps ops added during the render
    lab.setRegion (Region::all (lab.audio));
    lab.chain.ops = { mk (OpType::SpecStretch, { 2, 0, 0 }), mk (OpType::Normalise, { -1 }) }; lab.chainChanged();
    lab.bounce(); lab.chain.ops.push_back (Op (OpType::Reverse)); lab.chain.ops.push_back (Op (OpType::Invert)); lab.chainChanged();   // edits while it renders
    waitIdle (lab);
    check (lab.undo.undoName() == "BOUNCE 2 ops" && lab.chain.ops.size() == 4, "undo name counts the job's chain; BOUNCE leaves a chain edited meanwhile", lab.undo.undoName() + ", chain " + juce::String ((int) lab.chain.ops.size()));
    // #25 busy until the result is delivered
    lab.chain.ops = { Op (OpType::Invert) }; lab.chainChanged(); p.setAutoPreview (false);
    lab.apply(); std::this_thread::sleep_for (std::chrono::milliseconds (300));
    const bool stillBusy = lab.busy(); waitIdle (lab);
    check (stillBusy && ! lab.busy(), "busy() stays true until the result reached the message thread");
}

// #14 new ops at 44.1 / 48 / 96 kHz
static void testNewOps (double rate)
{
    std::cout << "-- #14 new ops @ " << rate << " Hz" << std::endl;
    const Audio s = sine (rate, 1.0, 440.0, 0.4f);
    const Audio g = run (s, mk (OpType::Gain, { -6 }));
    check (std::abs (g.peak() / s.peak() - juce::Decibels::decibelsToGain (-6.0f)) < 1e-5f, "GAIN -6 dB exact");
    const Audio inv = run (s, Op (OpType::Invert)); bool invOk = true; for (int i = 0; i < s.length(); ++i) invOk = invOk && inv.ch[0][(size_t) i] == -s.ch[0][(size_t) i];
    check (invOk, "INVERT exact");
    Audio dc = s; for (auto& c : dc.ch) for (float& v : c) v += 0.3f;
    const Audio dr = run (dc, Op (OpType::DcRemove)); double mean = 0; for (int i = s.length() / 4; i < 3 * s.length() / 4; ++i) mean += dr.ch[0][(size_t) i]; mean /= s.length() / 2;
    check (std::abs (mean) < 0.002, "DC REMOVE takes out a 0.3 offset", juce::String (mean, 5));
    Audio lr = tones (rate, 0.5, { { 300, 0.3f } }); for (auto& v : lr.ch[1]) v *= 0.2f;
    const Audio mono = run (lr, mk (OpType::Stereo, { 0, 1 })), swp = run (lr, mk (OpType::Stereo, { 1, 1 })), w0 = run (lr, mk (OpType::Stereo, { 2, 0 }));
    check (mono.ch[0] == mono.ch[1] && swp.ch[0] == lr.ch[1] && maxDiff (w0, mono) < 1e-6f, "STEREO mono / swap / width 0 = mono");
    const Audio vs = run (s, mk (OpType::Varispeed, { -12, 0 }));
    check (std::abs (vs.length() - 2 * s.length()) <= 1 && std::abs (peakHz (vs.ch[0], rate) - 220) < 1.5, "VARISPEED -12: twice as long, an octave down", juce::String (peakHz (vs.ch[0], rate), 1) + " Hz");
    const Audio vu = run (s, mk (OpType::Varispeed, { 12, 0 }));
    check (std::abs (vu.length() * 2 - s.length()) <= 2 && std::abs (peakHz (vu.ch[0], rate) - 880) < 3, "VARISPEED +12: half as long, an octave up");
    const Audio dv = run (s, mk (OpType::Drive, { 1, 24, 1, 0 }));
    const auto sp = spectrum (dv.ch[0], (size_t) (rate * 0.2));
    check (magAt (sp, 1320, rate) > 0.1f * magAt (sp, 440, rate) && dv.peak() <= 1.2f && finite (dv), "DRIVE hard clip: strong 3rd harmonic, bounded",
           "H3/H1 " + juce::String (magAt (sp, 1320, rate) / magAt (sp, 440, rate), 2));
    const Audio bc = run (s, mk (OpType::Bitcrush, { 3, 1, 1 })); std::set<float> lv (bc.ch[0].begin(), bc.ch[0].end());
    check ((int) lv.size() <= 9, "BITCRUSH 3 bits: <= 9 levels", juce::String ((int) lv.size()));
    const Audio hiS = sine (rate, 1.0, 5000.0, 0.4f); const Audio fl = run (hiS, mk (OpType::FilterSweep, { 0, 500, 500, 0.7f, 0 }));
    check (rmsRange (fl.ch[0], (size_t) (rate * 0.2), (size_t) (rate * 0.8)) < 0.02f * hiS.rms(), "FILTER SWEEP low-pass 500 Hz removes 5 kHz (> 34 dB)");
    const Audio fs = run (s, mk (OpType::FreqShift, { 100, 0, 1 }));
    check (std::abs (peakHz (fs.ch[0], rate, (size_t) (rate * 0.1)) - 540) < 2, "FREQ SHIFT +100 Hz: 440 -> 540", juce::String (peakHz (fs.ch[0], rate, (size_t) (rate * 0.1)), 1));
    const Audio rm = run (s, mk (OpType::FreqShift, { 100, 1, 1 })); const auto rsp = spectrum (rm.ch[0], (size_t) (rate * 0.1));
    check (magAt (rsp, 340, rate) > 0.3f * magAt (rsp, 540, rate) && magAt (rsp, 440, rate) < 0.05f * magAt (rsp, 540, rate), "RING MOD: 340 + 540, no 440");
    const Audio two = tones (rate, 1.0, { { 440, 0.4f }, { 1250, 0.004f } });
    const Audio gt = run (two, mk (OpType::SpecGate, { 11, -30, 0, 3 })); const auto gsp = spectrum (gt.ch[0], (size_t) (rate * 0.3));
    check (magAt (gsp, 1250, rate) < 0.1f * magAt (spectrum (two.ch[0], (size_t) (rate * 0.3)), 1250, rate) && magAt (gsp, 440, rate) > 0.8f * magAt (spectrum (two.ch[0], (size_t) (rate * 0.3)), 440, rate),
           "SPECTRAL GATE -30 dB: the -40 dB partial goes, the loud one stays");
    const Audio bs = run (s, mk (OpType::SpecBinShift, { 11, 200, 1 }));
    check (std::abs (peakHz (bs.ch[0], rate, (size_t) (rate * 0.2)) - 640) < 6, "BIN SHIFT +200 Hz: 440 -> 640", juce::String (peakHz (bs.ch[0], rate, (size_t) (rate * 0.2)), 1));
    Op cs = mk (OpType::CrossSynth, { 11, 1, 0 }); cs.side = std::make_shared<Audio> (tones (rate, 1.0, { { 700, 0.3f }, { 1900, 0.3f } }));
    const Audio cso = run (noise (rate, 1.0, 0.3f, 4), cs); const auto csp = spectrum (cso.ch[0], (size_t) (rate * 0.3));
    float bg = 0; for (double hz : { 300.0, 1200.0, 3000.0, 5000.0 }) bg = std::max (bg, magAt (csp, hz, rate));
    check (magAt (csp, 700, rate) > 3 * bg && magAt (csp, 1900, rate) > 3 * bg, "CROSS SYNTH vocode: noise takes B's spectral shape");
    const Audio shortS = sine (rate, 0.1, 440.0, 0.4f, 1);
    const Audio ex = run (shortS, mk (OpType::SpecExtreme, { 50, 200 }));
    check (ex.length() == 50 * shortS.length() && std::abs (peakHz (ex.ch[0], rate, (size_t) ex.length() / 3) - 440) < 4.4, "EXTREME STRETCH 50x: 50x long, pitch kept",
           juce::String (peakHz (ex.ch[0], rate, (size_t) ex.length() / 3), 1) + " Hz");
    const Audio sto = run (s, mk (OpType::StretchTo, { 4, 120, 0 }));
    check (sto.length() == (int) std::llround (2.0 * rate) && std::abs (peakHz (sto.ch[0], rate, (size_t) (rate * 0.3)) - 440) < 3, "STRETCH TO 4 beats at 120 bpm = 2.000 s, pitch kept");
    const Audio ct = run (sine (rate, 1.0, 220.0, 0.4f), mk (OpType::CycleTranspose, { 1, 2, 0.5f }));
    check (std::abs (peakHz (ct.ch[0], rate) - 440) < 3 && ct.length() == (int) rate, "CYCLE TRANSPOSE x2: 220 -> 440, length kept", juce::String (peakHz (ct.ch[0], rate), 1));
    const Audio ca = run (synthPhrase (rate), mk (OpType::CycleAverage, { 1, 4, 0.5f })), cd = run (synthPhrase (rate), mk (OpType::CycleDistort, { 1, 1, 1, 0.5f }));
    check (finite (ca) && finite (cd) && ca.rms() > 0.01f && cd.rms() > ca.rms() * 0.5f, "CYCLE AVERAGE / DISTORT render");
    Op br = mk (OpType::Brassage, { 40, 3, 20, 0, 1, 12, 0, 1, 0 });
    const Audio bro = run (s, br, 3);
    check (! (bro.ch[0] == bro.ch[1]) && std::abs (peakHz (bro.ch[0], rate, (size_t) (rate * 0.2)) - 880) < 15, "BRASSAGE transpose +12 = 880 Hz (within the 25 Hz grain bandwidth), spread makes L != R");
    Op q = mk (OpType::Brassage, { 60, 2, 0, 24, 1, 0, 0, 0, 1 });
    const Audio qo = run (s, q, 7); const auto qsp = spectrum (qo.ch[0], (size_t) (rate * 0.2));
    float off = 0; for (double hz : { 494.0, 554.0, 659.0, 740.0 }) off = std::max (off, magAt (qsp, hz, rate));
    float on = 0; for (double hz : { 110.0, 220.0, 440.0, 880.0, 1760.0 }) on = std::max (on, magAt (qsp, hz, rate));
    check (on > 5 * off, "BRASSAGE quantise octaves: grains only land on octaves", "on/off " + juce::String (on / std::max (1e-9f, off), 1));
    Audio dcl; dcl.rate = rate; dcl.resize (1, (int) rate); for (float& v : dcl.ch[0]) v = 1.0f;
    const Audio ep = run (dcl, mk (OpType::FadeIn, { 100, 0, 2 })), sc = run (dcl, mk (OpType::FadeIn, { 100, 0, 1 }));
    const int L = (int) std::round (0.1 * rate);
    check (std::abs (ep.ch[0][(size_t) L / 2] - 0.7071f) < 2e-3f && std::abs (sc.ch[0][(size_t) L / 2] - 0.5f) < 2e-3f && sc.ch[0][1] < 1e-3f, "FADE equal power 0.707 at half; S-curve 0.5 with flat ends [#28]");
}

// #15 #16 #17 #21 #23 #27 + edit commands
static void testMore()
{
    std::cout << "-- #15 #16 #17 #21 #23 #27 + edits" << std::endl;
    const double rate = 48000;
    const Audio t = tones (rate, 1.0, { { 330, 0.3f }, { 495, 0.15f } });
    const Audio fz = run (t, mk (OpType::SpecFreeze, { 11, 50, 0, 3, 40, 200, 1 }));
    const int P = (int) (0.5 * rate);
    check (fz.length() == P + (int) (3 * rate) && rmsRange (fz.ch[0], (size_t) (P + rate), (size_t) (P + 2 * rate)) > 0.9f * rmsRange (t.ch[0], 0, (size_t) rate)
           && std::abs (fz.ch[0].back()) < 1e-4f && maxD1 (fz.ch[0], 1, fz.length()) < 2 * maxD1 (t.ch[0], 1, t.length()), "FREEZE hold 3 s past the region end, crossfaded in, released to 0 [#15]");
    auto flutter = [&] (const Audio& a) { std::vector<float> r; for (int i = P + 4800; i + 960 < P + (int) (2.5 * rate); i += 960) r.push_back (rmsRange (a.ch[0], (size_t) i, (size_t) i + 960)); float mn = 9, mx = 0; for (float v : r) { mn = std::min (mn, v); mx = std::max (mx, v); } return mx / std::max (1e-9f, mn); };
    const Audio fzNo = run (t, mk (OpType::SpecFreeze, { 11, 50, 0, 3, 40, 200, 0 }));
    check (flutter (fz) <= flutter (fzNo) + 0.02f, "FREEZE peak phase-lock: steadier hold than plain", "lock " + juce::String (flutter (fz), 3) + " vs " + juce::String (flutter (fzNo), 3));
    const Audio monoX = noise (rate, 6.0, 0.3f, 1, 1), stX = noise (rate, 6.0, 0.3f, 1, 2);
    double t0 = juce::Time::getMillisecondCounterHiRes(); run (monoX, mk (OpType::SpecBlur, { 11, 200, 0 })); const double mMs = msSince (t0);
    t0 = juce::Time::getMillisecondCounterHiRes(); run (stX, mk (OpType::SpecBlur, { 11, 200, 0 })); const double sMs = msSince (t0);
    check (mMs < 0.7 * sMs, "mono spectral ops run once (mono vs stereo time) [#16]", juce::String (mMs, 0) + " vs " + juce::String (sMs, 0) + " ms");
    const Audio sl = sine (rate, 2.0, 220.0, 0.5f);
    const Audio bz = run (sl, mk (OpType::Brassage, { 40, 3, 400, 0, 1 }), 2);
    const float e0 = rmsRange (bz.ch[0], 0, (size_t) (0.1 * rate)), mid = rmsRange (bz.ch[0], (size_t) (0.9 * rate), (size_t) (1.1 * rate));
    check (e0 > 0.8f * mid, "brassage scatter 400 ms keeps the level at the edges [#17]", juce::String (e0 / mid, 2));
    Op e (OpType::Envelope); e.env = { { 0, 0, -0.6f }, { 1, 1, 0 } };
    Audio dc; dc.rate = rate; dc.resize (1, 1001); for (float& v : dc.ch[0]) v = 1.0f;
    const Audio eo = run (dc, e); const Chain rt = Chain::fromXml ([&] { Chain c; c.ops = { e }; return c.toXml(); }());
    check (eo.ch[0][500] < 0.4f && rt.ops[0].env[0].curve < -0.59f, "envelope curve bends the segment and is saved [#21]", juce::String (eo.ch[0][500], 3));
    // #23 hostile state
    juce::ValueTree v ("PLCHAIN"); v.setProperty ("seed", 1e20, nullptr); v.setProperty ("edgeMs", std::numeric_limits<double>::quiet_NaN(), nullptr);
    for (int i = 0; i < 10000; ++i) { juce::ValueTree o ("OP"); o.setProperty ("id", "gain", nullptr); o.setProperty ("p0", i % 2 ? 1e300 : std::numeric_limits<double>::quiet_NaN(), nullptr); v.appendChild (o, nullptr); }
    t0 = juce::Time::getMillisecondCounterHiRes(); const Chain hc = Chain::fromValueTree (v); const double hMs = msSince (t0);
    bool fin = true; for (auto& o : hc.ops) fin = fin && std::isfinite (o.p[0]) && o.p[0] <= 24.0f;
    check ((int) hc.ops.size() == kMaxChainOps && hc.seed >= 1 && fin && std::isfinite (hc.edgeMs) && hMs < 2000, "hostile state (seed 1e20, 10k ops, NaN/1e300): clamped, 64 ops",
           juce::String ((int) hc.ops.size()) + " ops, seed " + juce::String ((juce::int64) hc.seed) + ", " + juce::String (hMs, 0) + " ms");
    // #27 extend equal power on uncorrelated material
    const Audio nz = noise (rate, 1.0, 0.3f, 8, 1);
    const Audio ep = run (nz, mk (OpType::Extend, { 2, 300, 0, 2 })), ln = run (nz, mk (OpType::Extend, { 2, 300, 0, 1 }));
    const int j = (int) rate - (int) (0.15 * rate);   // middle of the first crossfade
    const float rEp = rmsRange (ep.ch[0], (size_t) j - 2400, (size_t) j + 2400) / 0.3f * std::sqrt (3.0f), rLn = rmsRange (ln.ch[0], (size_t) j - 2400, (size_t) j + 2400) / 0.3f * std::sqrt (3.0f);
    check (rEp > 0.9f && rLn < 0.8f, "EXTEND equal power keeps noise level through the crossfade (linear dips) [#27]", "equal power " + juce::String (rEp, 2) + ", linear " + juce::String (rLn, 2));
    // edit commands (#14 basics)
    Lab lab; const Audio x = sine (rate, 2.0, 220.0); lab.setAudio (x);
    lab.setRegion ({ 24000, 24000 }); lab.crop(); const bool crop = lab.audio.length() == 24000 && lab.audio.ch[0][0] == x.ch[0][24000];
    lab.undoStep(); lab.setRegion ({ 24000, 24000 }); lab.copyRegion(); lab.deleteRegion(); const bool del = lab.audio.length() == x.length() - 24000;
    lab.setRegion ({ 24000, 0 }); lab.paste(); const bool pst = same (lab.audio, x);
    lab.setRegion ({ 0, 48000 }); lab.silenceRegion(); const bool sil = lab.audio.ch[0][100] == 0.0f;
    lab.setRegion ({ 0, 0 }); lab.insertSilence (0.5); const bool ins = lab.audio.length() == x.length() + 24000;
    while (lab.undo.canUndo()) lab.undoStep();
    check (crop && del && pst && sil && ins && same (lab.audio, x), "CROP / DELETE / COPY+PASTE / SILENCE / INSERT, all undone bit-exact");
    lab.setRegion ({ 0, 24000 }); lab.copyRegion(); lab.setRegion ({ 0, 0 }); lab.mixPaste (1.0f);
    check (std::abs (lab.audio.ch[0][1000] - 2 * x.ch[0][1000]) < 1e-6f, "MIX PASTE adds the clipboard");
    lab.undoStep();
    lab.setRegion ({ 4800, 48000 + 77 }); lab.findLoop();
    const int e2 = lab.region.end(); const auto xm = monoMix (x).ch[0]; const float wrap = std::abs (xm[(size_t) e2] - xm[4800]);
    check (wrap < 0.03f, "FIND LOOP moves the end to a seamless loop point", "jump at the wrap " + juce::String (wrap, 4));
}

// ================================================================ palette (light / dark host themes)
static void testPalette()
{
    std::cout << "-- palette" << std::endl;
    const Palette L = Palette::light();
    struct Pair { const char* what; juce::Colour fg, bg; float need; };
    const Pair pairs[] = {
        { "body on window", L.text, L.bg, 7 }, { "body on panel", L.text, L.card, 7 }, { "body on cell", L.text, L.cell, 7 },
        { "op names on panel", L.opText, L.card, 7 }, { "wave info on wave view", L.waveText, L.waveBg, 7 },
        { "button text", L.buttonText, L.button, 7 }, { "combo text", L.comboText, L.comboBg, 7 }, { "popup text", L.popupText, L.popupBg, 7 }, { "label text", L.labelText, L.labelBg, 7 },
        { "secondary on window", L.text2, L.bg, 4.5f }, { "secondary on panel", L.text2, L.card, 4.5f }, { "secondary on cell", L.text2, L.cell, 4.5f },
        { "ruler on wave view", L.waveDim, L.waveBg, 4.5f }, { "popup highlight text", L.popupHighlightText, L.popupHighlight, 4.5f }, { "button-on text", L.buttonTextOn, L.buttonOn, 4.5f },
        { "accent on window", L.accent, L.bg, 3 }, { "accent on panel", L.accent, L.card, 3 }, { "accent on cell", L.accent, L.cell, 3 }, { "accent2 on panel", L.accent2, L.card, 3 },
        { "wave", L.wave, L.waveBg, 3 }, { "wave in selection", L.waveSel, L.waveBg, 3 }, { "preview lane", L.preview, L.waveBg, 3 }, { "envelope", L.envLine, L.waveBg, 3 },
        { "bar ruler", L.bar, L.waveBg, 3 }, { "playhead", L.play, L.waveBg, 3 }, { "cursor", L.cursor, L.waveBg, 3 }, { "loop", L.loop, L.waveBg, 3 },
        { "seed", L.seed, L.cell, 3 }, { "edge (info)", L.info, L.cell, 3 }, { "op on", L.opOn, L.card, 3 }, { "progress", L.progress, L.cell, 3 }, { "warn", L.warn, L.bg, 3 } };
    juce::String bad, worst; float wr = 99;
    for (auto& q : pairs) { const float r = contrastRatio (q.fg, q.bg); if (r < q.need) bad << q.what << " " << juce::String (r, 2) << "; "; if (r / q.need < wr) { wr = r / q.need; worst = juce::String (q.what) + " " + juce::String (r, 2) + ":1 (needs " + juce::String (q.need, 1) + ")"; } }
    check (bad.isEmpty(), "light palette contrast: body >= 7:1, secondary >= 4.5:1, accents >= 3:1 (" + juce::String ((int) std::size (pairs)) + " pairs)", bad.isEmpty() ? "tightest " + worst : bad);
    const Palette D = Palette::dark();
    check (D.bg == ghosty::col::bg && D.accent == ghosty::col::slime && D.waveBg == juce::Colour (0xff07080c) && D.wave == ghosty::col::ecto, "Palette::dark() = the original colours");
    PunchHost h; h.a = drumLoop (48000, 2, 120);
    for (int k = 0; k < 3; ++k)
    {
        ProcessLabPanel p (h); p.setSize (980, 560); p.setAutoPreview (false);
        if (k == 1) p.setPalette (Palette::light()); else if (k == 2) p.setPalette (Palette::dark());
        p.lab().chain.ops = { mk (OpType::Brassage), Op (OpType::Envelope), mk (OpType::CycleReverse) }; p.lab().chainChanged(); p.selectOp (1);
        p.lab().setRegion ({ 24000, 48000 }); p.lab().preview(); for (int i = 0; i < 300 && ! p.lab().previewValid(); ++i) pump (10);
        int seeThrough = 0;
        for (auto sz : { std::pair<int, int> { 980, 560 }, std::pair<int, int> { 640, 360 } })
        {
            p.setSize (sz.first, sz.second);
            juce::Image img (juce::Image::ARGB, sz.first, sz.second, true);
            { juce::Graphics g (img); p.paintEntireComponent (g, true); }
            const juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
            for (int y = 0; y < sz.second; ++y) for (int x = 0; x < sz.first; ++x) if (bd.getPixelColour (x, y).getAlpha() != 255) ++seeThrough;
        }
        check (seeThrough == 0 && p.isOpaque(), juce::String ("opaque: painting alone onto a transparent image leaves no see-through pixel (") + (k == 0 ? "default" : k == 1 ? "light" : "dark") + ", 980x560 + 640x360)",
               juce::String (seeThrough) + " see-through pixels");
    }
    ProcessLabPanel p (h); p.setSize (980, 560); p.setPalette (Palette::light());
    check (p.getPalette().bg == L.bg && &p.getLookAndFeel() != &juce::LookAndFeel::getDefaultLookAndFeel() && p.getLookAndFeel().findColour (juce::TextButton::buttonColourId) == L.button,
           "setPalette() stores the palette and installs the panel's own LookAndFeel");
}

static void paletteShots (const juce::File& dir)
{
    dir.createDirectory();
    PunchHost h; h.a = drumLoop (48000, 4, 120); h.sl = { { 0, 24000 } };
    auto snap = [&] (juce::Component& c, const juce::String& name)
    {
        auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f); juce::PNGImageFormat png;
        auto f = dir.getChildFile (name); f.deleteFile(); juce::FileOutputStream os (f); png.writeImageToStream (img, os);
        std::cout << "shot " << f.getFullPathName() << std::endl;
    };
    for (int dark = 0; dark < 2; ++dark)
    {
        const juce::String pre = dark ? "processlab-dark-" : "processlab-light-";
        ProcessLabPanel p (h); p.setSize (1004, 560); p.setAutoPreview (false);
        p.setPalette (dark ? Palette::dark() : Palette::light());
        Op e (OpType::Envelope); e.env = { { 0, 0.1f, 0.5f }, { 0.2f, 1, 0 }, { 0.55f, 0.4f, -0.6f }, { 0.8f, 0.85f, 0 }, { 1, 0, 0 } };
        p.lab().chain.seed = 4242;
        p.lab().chain.ops = { mk (OpType::CycleRepeat, { 8, 3, 0.4f }), mk (OpType::Brassage, { 45, 3, 30, 4, 1, 0, 20, 0.8f, 1 }), e, mk (OpType::FadeOut, { 400, 0, 2 }), mk (OpType::Normalise, { -1 }) };
        p.lab().chain.ops[4].enabled = false;
        p.lab().chainChanged(); p.lab().setRegion ({ 48000, 96000 }); p.selectOp (1);
        p.lab().preview(); for (int i = 0; i < 600 && ! p.lab().previewValid(); ++i) pump (10);
        pump (50);
        snap (p, pre + "brassage-preview.png");
        p.selectOp (2); snap (p, pre + "envelope.png");
        p.selectOp (3); p.setView (48000, 120000); snap (p, pre + "zoom-fade.png");
        p.setView (0, p.lab().audio.length()); p.selectOp (0); p.setSize (640, 360); snap (p, pre + "small.png");
    }
}

// ---------------------------------------------------------------- demos + screenshots
static void writeWav (const Audio& a, const juce::File& f)
{
    f.deleteFile(); juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), a.rate, (unsigned) a.channels(), 24, {}, 0));
    auto b = a.toBuffer(); w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}
static Audio beforeAfter (const Audio& src, const Audio& res)
{
    Audio o; o.rate = src.rate; const int gap = (int) (src.rate * 0.8);
    o.resize (2, src.length() + gap + res.length());
    for (int c = 0; c < 2; ++c)
    {
        std::copy (src.ch[(size_t) c].begin(), src.ch[(size_t) c].end(), o.ch[(size_t) c].begin());
        std::copy (res.ch[(size_t) c].begin(), res.ch[(size_t) c].end(), o.ch[(size_t) c].begin() + src.length() + gap);
    }
    return o;
}
static void demos (const juce::File& dir)
{
    dir.createDirectory(); const double sr = 48000;
    Audio drums = drumLoop (sr, 2), synth = synthPhrase (sr); normaliseTo (drums, -1); normaliseTo (synth, -1);
    {   // 1: resample loop: bar 2 cycle-stuttered + shuffled; then the whole loop varispeed -5 st, driven, filter-swept, crushed a little, loop-extended
        Audio a = drums; Region r { a.length() / 2, a.length() / 2 };
        Chain c; c.seed = 21; c.ops = { mk (OpType::CycleRepeat, { 12, 3, 0.35f }), mk (OpType::CycleShuffle, { 24, 0.4f }) };
        applyChain (a, r, c);
        Region all = Region::all (a);
        Chain d; d.seed = 3; d.ops = { mk (OpType::Varispeed, { -5, 0 }), mk (OpType::Drive, { 0, 14, 0.8f, -8 }), mk (OpType::FilterSweep, { 0, 300, 9000, 3, 0 }),
                                       mk (OpType::Bitcrush, { 10, 2, 0.5f }), mk (OpType::Extend, { 2, 15, 0, 0 }), mk (OpType::FadeOut, { 300, 0, 1 }), mk (OpType::Normalise, { -1, 0 }) };
        applyChain (a, all, d);
        writeWav (beforeAfter (drums, a), dir.getChildFile ("1_drums_resample_drive.wav"));
    }
    {   // 2: synth phrase -> stereo brassage cloud quantised to octaves -> 300 ms spectral blur -> freeze held 3 s -> drawn swell
        Chain c; c.seed = 5; Op e (OpType::Envelope); e.env = { { 0, 0, -0.5f }, { 0.3f, 1, 0 }, { 0.75f, 0.7f, 0.4f }, { 1, 0, 0 } };
        c.ops = { mk (OpType::Brassage, { 90, 4, 120, 12, 2, 0, 15, 0.9f, 1 }), mk (OpType::SpecBlur, { 12, 300, 0 }),
                  mk (OpType::SpecFreeze, { 12, 70, 0.15f, 3, 400, 1500, 1 }), e, mk (OpType::Normalise, { -1, 0 }) };
        Audio out; renderChain (synth, Region::all (synth), c, out);
        writeWav (beforeAfter (synth, out), dir.getChildFile ("2_synth_cloud_freeze.wav"));
    }
    {   // 3: the synth imprinted on the drums (cross synth), iterated with pitch drift, inharmonic bin shift, trace, then a 12x extreme-stretched tail
        Op x = mk (OpType::CrossSynth, { 11, 0.85f, 0 }); x.side = std::make_shared<Audio> (synth);
        Chain c; c.seed = 77; c.ops = { x, mk (OpType::Iterate, { 4, 2.5f, -20, 3, 20 }), mk (OpType::SpecBinShift, { 11, 90, 1.02f }), mk (OpType::SpecTrace, { 12, 48 }),
                                        mk (OpType::Normalise, { -1, 0 }) };
        Audio out; renderChain (drums, Region::all (drums), c, out);
        Audio tail = out.slice (out.length() - (int) (0.6 * sr), (int) (0.6 * sr));
        Chain t; t.seed = 9; t.ops = { mk (OpType::SpecExtreme, { 12, 300 }), mk (OpType::FadeOut, { 3000, 0, 2 }), mk (OpType::Normalise, { -3, 0 }) };
        Audio ts; renderChain (tail, Region::all (tail), t, ts);
        out.splice (out.length(), 0, ts);
        writeWav (beforeAfter (drums, out), dir.getChildFile ("3_cross_iterate_extreme.wav"));
    }
    std::cout << "demos written to " << dir.getFullPathName() << std::endl;
}

struct TestHost : Host {
    Audio a; std::shared_ptr<const Audio> last; int sets = 0;
    Audio getBuffer() override { return a; }
    void setBuffer (const Audio& b) override { a = b; ++sets; }
    double sampleRate() override { return 48000; }
    void audition (std::shared_ptr<const Audio> c, bool) override { last = c; }
};
static void shots (const juce::File& dir)
{
    dir.createDirectory();
    PunchHost h; h.a = drumLoop (48000, 4, 120); h.sl = { { 0, 24000 }, { 48000, 24000 } };
    auto snap = [&] (juce::Component& c, const juce::String& name)
    {
        auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f); juce::PNGImageFormat png;
        auto f = dir.getChildFile (name); f.deleteFile(); juce::FileOutputStream os (f); png.writeImageToStream (img, os);
        std::cout << "shot " << f.getFullPathName() << std::endl;
    };
    auto wait = [] (std::function<bool()> done) { for (int i = 0; i < 600 && ! done(); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (10); juce::MessageManager::getInstance()->runDispatchLoopUntil (60); };
    ProcessLabPanel p (h); p.setSize (1004, 560);
    p.setAutoPreview (false);
    Op e (OpType::Envelope); e.env = { { 0, 0.1f, 0.5f }, { 0.2f, 1, 0 }, { 0.55f, 0.4f, -0.6f }, { 0.8f, 0.85f, 0 }, { 1, 0, 0 } };
    p.lab().chain.seed = 4242;
    p.lab().chain.ops = { mk (OpType::CycleRepeat, { 8, 3, 0.4f }), mk (OpType::Brassage, { 45, 3, 30, 4, 1, 0, 20, 0.8f, 1 }), e, mk (OpType::FadeOut, { 400, 0, 2 }), mk (OpType::Normalise, { -1 }) };
    p.lab().chainChanged();
    p.lab().setRegion ({ 48000, 96000 });
    p.selectOp (1);
    p.lab().preview(); wait ([&] { return ! p.lab().busy() && p.lab().previewValid(); });
    snap (p, "processlab_brassage_preview.png");
    p.selectOp (2); snap (p, "processlab_envelope.png");
    p.selectOp (3); p.setView (48000, 120000); snap (p, "processlab_zoom_fade_handle.png");
    p.selectOp (0); snap (p, "processlab_choice_params.png");
    p.lab().apply(); wait ([&] { return ! p.lab().busy() && h.sets > 0; });
    p.setView (0, p.lab().audio.length()); snap (p, "processlab_applied.png");
    p.setSize (640, 360); snap (p, "processlab_small.png");
    std::cout << "host got " << h.sets << " commit(s); state: " << p.getState().toXmlString().length() << " chars" << std::endl;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::String mode = argc > 1 ? argv[1] : "";
    if (mode == "demos" && argc > 2) { demos (juce::File (argv[2])); return 0; }
    if (mode == "shots" && argc > 2) { shots (juce::File (argv[2])); paletteShots (juce::File (argv[2])); return 0; }
    const double tStart = juce::Time::getMillisecondCounterHiRes();
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        testBasics (rate); testCycles (rate); testSeeds (rate); testSpectral (rate); testEdgeCases (rate);
        std::cout << "   [" << juce::String ((juce::Time::getMillisecondCounterHiRes() - tStart) / 1000.0, 1) << " s]" << std::endl;
    }
    testChainAndState(); testCancelUndo(); testLabThread(); testAudition();
    std::cout << "   [" << juce::String ((juce::Time::getMillisecondCounterHiRes() - tStart) / 1000.0, 1) << " s]" << std::endl;
    std::cout << "\n==== final-gate punch list ====" << std::endl;
    auto T = [] (const char* n, auto&& f) { const double t0 = juce::Time::getMillisecondCounterHiRes(); f(); std::cout << "   [" << n << " " << juce::String ((juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0, 1) << " s]" << std::endl; };
    T ("stale", testStalePreview); T ("caps", testCaps); T ("cancel", testCancelLatency);
    for (double rate : { 44100.0, 48000.0, 96000.0 }) { T ("edges", [rate] { testRegionEdges (rate); }); T ("newops", [rate] { testNewOps (rate); }); }
    T ("rate", testRateIndependence); T ("cycles", testCyclesRobust); T ("job", testJobSizeAndReuse); T ("audition", testAuditionLoop); T ("determinism", testStretchDeterminism);
    T ("panel", testPanelWorkflow); T ("more", testMore); T ("palette", testPalette);
    std::cout << "\n" << passes << " passed, " << fails << " failed" << std::endl;
    return fails;
}
