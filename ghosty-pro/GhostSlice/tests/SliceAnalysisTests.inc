// GhostSliceTests: the analysis phases (7 onsets / syllables / tempo, 8 ALIGN, 13 pitch / notes / key, 9 global pitch).
// Included by SliceTests.cpp (it shares check / section / Rig / the test audio there). Groups: onsets tempo align pitch key gpitch
#include "../Analysis.h"
#include "../AnalysisAlign.h"
#include "../AnalysisInternal.h"
namespace an = gslice::analysis;

// ---------------------------------------------------------------- synthetic material
struct Truth { std::vector<int64> pos; };

// clicks at random gaps (60 - 400 ms), random level (-24 .. 0 dB), 2 ms decaying bursts, over -66 dB noise
static juce::AudioBuffer<float> clickTrain (double sr, double secs, uint32_t seed, Truth& truth)
{
    juce::AudioBuffer<float> b (1, (int) (sr * secs)); b.clear();
    std::mt19937 rng (seed); std::uniform_real_distribution<double> U (0, 1); std::normal_distribution<float> N (0, 1);
    for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (0, i, 0.0005f * N (rng));
    double t = 0.1;
    while (t < secs - 0.1)
    {
        const int64 at = (int64) std::llround (t * sr); truth.pos.push_back (at);
        const double amp = std::pow (10.0, (-24.0 * U (rng)) / 20.0) * 0.8;
        const int len = (int) (sr * 0.004);
        for (int i = 0; i < len && at + i < b.getNumSamples(); ++i)
        {
            const double env = std::exp (-i / (sr * 0.0007));
            b.setSample (0, (int) at + i, b.getSample (0, (int) at + i) + (float) (amp * env * (0.6 * N (rng) + 0.4 * std::sin (2.0 * juce::MathConstants<double>::pi * 3100.0 * i / sr))));
        }
        t += 0.06 + 0.34 * U (rng);
    }
    return b;
}

// precision / recall with a greedy one-to-one match within tol samples
static std::pair<double, double> prf (const std::vector<int64>& found, const std::vector<int64>& truth, int64 tol, double* meanErr = nullptr)
{
    std::vector<bool> used (truth.size(), false); int hit = 0; double err = 0;
    for (auto f : found)
    {
        int best = -1; int64 bd = tol + 1;
        for (size_t i = 0; i < truth.size(); ++i) if (! used[i] && std::abs (truth[i] - f) < bd) { bd = std::abs (truth[i] - f); best = (int) i; }
        if (best >= 0) { used[(size_t) best] = true; ++hit; err += (double) bd; }
    }
    if (meanErr) *meanErr = hit > 0 ? err / hit : 0;
    return { found.empty() ? 0.0 : (double) hit / found.size(), truth.empty() ? 0.0 : (double) hit / truth.size() };
}

// a vowel: a glottal-like pulse (harmonics with a -12 dB / octave tilt) shaped by three formant resonances
static double vowelSample (double t, double f0, double phase, const double* F, double sr)
{
    double v = 0;
    for (int h = 1; h * f0 < std::min (5000.0, sr * 0.45); ++h)
    {
        const double fh = h * f0;
        double g = 0; for (int k = 0; k < 3; ++k) { const double bw = 80.0 + 40.0 * k, d = (fh - F[k]) / bw; g += (k == 0 ? 1.0 : k == 1 ? 0.6 : 0.3) / (1.0 + d * d); }
        v += g / h * std::sin (2.0 * juce::MathConstants<double>::pi * h * phase);
    }
    juce::ignoreUnused (t);
    return v;
}

// syllables: [optional consonant burst 25-45 ms][vowel 140-260 ms, f0 170-240 Hz with a glide], separated by silence (40-140 ms)
// or joined legato (a ~12 dB dip). truth = consonant start (or the vowel start); group = the legato chain each syllable is in
static juce::AudioBuffer<float> vocalLike (double sr, int syllables, uint32_t seed, Truth& truth, std::vector<int>* chain = nullptr)
{
    std::mt19937 rng (seed); std::uniform_real_distribution<double> U (0, 1); std::normal_distribution<double> N (0, 1);
    static const double vowels[4][3] { { 800, 1200, 2500 }, { 300, 2300, 3000 }, { 350, 800, 2400 }, { 500, 1700, 2500 } };
    std::vector<float> out ((size_t) (sr * (0.2 + syllables * 0.5)), 0.0f);
    double t = 0.15; int ch = 0; double hp = 0;
    for (int s = 0; s < syllables; ++s)
    {
        const bool legato = s > 0 && U (rng) < 0.35;
        if (! legato) ++ch;
        if (chain) chain->push_back (ch);
        const bool cons = ! legato && U (rng) < 0.6;
        const double cLen = cons ? 0.025 + 0.02 * U (rng) : 0.0, vLen = 0.14 + 0.12 * U (rng);
        const double start = legato ? t - 0.03 : t;   // legato: the next vowel starts inside the previous release
        truth.pos.push_back ((int64) std::llround (start * sr));
        const double f0a = 170 + 70 * U (rng), f0b = f0a * (1.0 + 0.06 * (U (rng) - 0.5));
        const double* F = vowels[s % 4];
        double phase = 0;
        for (int64 i = (int64) (start * sr); i < (int64) ((start + cLen + vLen) * sr) && i < (int64) out.size(); ++i)
        {
            const double u = (double) i / sr - start;
            double v = 0;
            if (u < cLen) { const double x = N (rng); v = 0.18 * (x - hp); hp = x; v *= std::min (1.0, u / 0.004); }
            else
            {
                const double w = u - cLen, f0 = f0a + (f0b - f0a) * w / vLen;
                phase += f0 / sr;
                const double env = std::min (1.0, w / 0.025) * std::min (1.0, (vLen - w) / 0.045);
                v = 0.22 * env * vowelSample (w, f0, phase, F, sr);
            }
            out[(size_t) i] += (float) v;
        }
        t = start + cLen + vLen + (U (rng) < 0.5 ? 0.04 + 0.1 * U (rng) : 0.06);
    }
    juce::AudioBuffer<float> b (1, (int) std::min<size_t> (out.size(), (size_t) ((t + 0.2) * sr)));
    for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (0, i, out[(size_t) i]);
    return b;
}

// a drum loop: kick on 1 and the "and" of 3, snare on 2 and 4, closed hats on 8ths; bars x 4 beats at bpm (+ tail seconds)
static juce::AudioBuffer<float> drumLoop (double sr, double bpm, int bars, double tail = 0.0, uint32_t seed = 11)
{
    const double beat = 60.0 / bpm;
    juce::AudioBuffer<float> b (2, (int) std::llround ((bars * 4 * beat + tail) * sr)); b.clear();
    std::mt19937 rng (seed); std::normal_distribution<float> N (0, 1);
    auto hit = [&] (double at, int kind)
    {
        const int64 s0 = (int64) std::llround (at * sr);
        const int len = (int) (sr * (kind == 0 ? 0.25 : kind == 1 ? 0.18 : 0.05));
        for (int i = 0; i < len && s0 + i < b.getNumSamples(); ++i)
        {
            const double u = i / sr; double v;
            if (kind == 0) v = 0.8 * std::exp (-u / 0.08) * std::sin (2 * juce::MathConstants<double>::pi * (55 * u + 120 * 0.01 * (1 - std::exp (-u / 0.01))));
            else if (kind == 1) v = 0.45 * std::exp (-u / 0.05) * (0.7 * N (rng) + 0.3 * std::sin (2 * juce::MathConstants<double>::pi * 190 * u));
            else v = 0.18 * std::exp (-u / 0.012) * N (rng);
            for (int c = 0; c < 2; ++c) b.setSample (c, (int) (s0 + i), b.getSample (c, (int) (s0 + i)) + (float) v);
        }
    };
    for (int bar = 0; bar < bars; ++bar)
        for (int e = 0; e < 8; ++e)
        {
            const double at = (bar * 4 + e * 0.5) * beat;
            hit (at, 2);
            if (e == 0 || e == 5) hit (at, 0);
            if (e == 2 || e == 6) hit (at, 1);
        }
    return b;
}

// a monophonic melody: notes (midi, seconds) as a band-limited sawtooth-ish tone with optional vibrato (Hz, cents depth +-)
struct MelNote { double midi, secs; double vibHz = 0, vibCents = 0; bool gapAfter = false; };
static juce::AudioBuffer<float> melody (double sr, const std::vector<MelNote>& notes, bool formants, std::vector<std::pair<double, double>>* truthSpans = nullptr)
{
    double total = 0.1; for (auto& n : notes) total += n.secs + (n.gapAfter ? 0.08 : 0.0);
    juce::AudioBuffer<float> b (1, (int) ((total + 0.15) * sr)); b.clear();
    double t = 0.1, phase = 0;
    static const double F[3] { 650, 1100, 2600 };
    for (size_t k = 0; k < notes.size(); ++k)
    {
        const auto& n = notes[k];
        const int64 s0 = (int64) (t * sr), len = (int64) (n.secs * sr);
        if (truthSpans) truthSpans->push_back ({ t, t + n.secs });
        const bool legatoIn = k > 0 && ! notes[k - 1].gapAfter, legatoOut = k + 1 < notes.size() && ! n.gapAfter;
        for (int64 i = 0; i < len; ++i)
        {
            const double u = (double) i / sr;
            const double cents = n.vibCents * std::sin (2 * juce::MathConstants<double>::pi * n.vibHz * u);
            const double f0 = 440.0 * std::pow (2.0, (n.midi - 69.0 + cents / 100.0) / 12.0);
            phase += f0 / sr; phase -= std::floor (phase);
            const double env = (legatoIn ? 1.0 : std::min (1.0, u / 0.015)) * (legatoOut ? 1.0 : std::min (1.0, (n.secs - u) / 0.02));
            double v;
            if (formants) v = 0.25 * vowelSample (u, f0, phase, F, sr);
            else { v = 0; for (int h = 1; h <= 6 && h * f0 < sr * 0.45; ++h) v += std::sin (2 * juce::MathConstants<double>::pi * h * phase) / (h * h); v *= 0.35; }
            b.setSample (0, (int) (s0 + i), (float) (env * v));
        }
        t += n.secs + (n.gapAfter ? 0.08 : 0.0);
    }
    return b;
}

// ================================================================ [14] onsets
static void testOnsets()
{
    section ("[14] onsets: spectral flux + HFC, material profiles, syllables, determinism, cache (phase 7)");
    for (double sr : { 48000.0, 44100.0 })
    {
        Truth tr; auto b = clickTrain (sr, 20.0, sr == 48000 ? 5u : 9u, tr);
        const auto x = an::mono (b);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        const auto o = an::detectOnsets (x.data(), (int64) x.size(), sr, an::Material::Drums);
        const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
        std::vector<int64> f; for (auto& e : o.events) f.push_back (e.pos);
        double err = 0; const auto [P, R] = prf (f, tr.pos, (int64) (0.005 * sr), &err);
        check (P >= 0.95 && R >= 0.95, "DRUMS click train @ " + juce::String (sr / 1000.0, 1) + " kHz: precision " + juce::String (P, 3) + ", recall " + juce::String (R, 3)
               + " within 5 ms (" + juce::String ((int) tr.pos.size()) + " clicks, " + juce::String ((int) f.size()) + " found, mean error " + juce::String (err / sr * 1000.0, 2) + " ms, " + juce::String (ms, 0) + " ms for 20 s)");
        const auto a = an::detectOnsets (x.data(), (int64) x.size(), sr, an::Material::Auto);
        std::vector<int64> fa; for (auto& e : a.events) fa.push_back (e.pos);
        const auto [Pa, Ra] = prf (fa, tr.pos, (int64) (0.005 * sr));
        check (a.used == an::Material::Drums && Pa >= 0.95 && Ra >= 0.95, juce::String ("AUTO picks ") + an::materialName (a.used) + " for clicks (P " + juce::String (Pa, 3) + ", R " + juce::String (Ra, 3) + ")");
        float mn = 1, mx = 0; for (auto& e : o.events) { mn = std::min (mn, e.strength); mx = std::max (mx, e.strength); }
        check (mn >= 0.0f && mx <= 1.0f && mx > 0.5f, "onset strength 0..1 (" + juce::String (mn, 2) + " .. " + juce::String (mx, 2) + ")");
    }
    {   // a sustained pad has no onsets but its start; a melodic line finds its note starts with the MELODIC profile
        const auto pd = an::mono (pad (48000, 4.0));
        const auto o = an::detectOnsets (pd.data(), (int64) pd.size(), 48000, an::Material::Auto);
        int strong = 0; for (auto& e : o.events) if (e.strength >= 0.35f && e.pos > 4800) ++strong;
        check (strong == 0, "a steady pad: no strong onsets after its start (" + juce::String ((int) o.events.size()) + " events)");
        std::vector<std::pair<double, double>> spans;
        auto mb = melody (48000, { { 60, 0.4, 0, 0, true }, { 64, 0.4, 0, 0, true }, { 67, 0.4, 0, 0, true }, { 72, 0.5, 0, 0, true }, { 67, 0.35, 0, 0, true }, { 64, 0.6 } }, false, &spans);
        const auto mx = an::mono (mb);
        const auto mo = an::detectOnsets (mx.data(), (int64) mx.size(), 48000, an::Material::Auto);
        std::vector<int64> f, tr; for (auto& e : mo.events) f.push_back (e.pos); for (auto& s : spans) tr.push_back ((int64) (s.first * 48000));
        const auto [P, R] = prf (f, tr, 2400);
        check (mo.used == an::Material::Melodic && P >= 0.85 && R >= 0.99, juce::String ("AUTO picks ") + an::materialName (mo.used) + " for a melody: note starts P " + juce::String (P, 2) + " R " + juce::String (R, 2) + " within 50 ms");
    }
    {   // VOCAL: syllable onsets
        int found = 0, total = 0, extra = 0, groupsOk = 0, pairs = 0; double errMs = 0;
        for (uint32_t seed : { 21u, 22u, 23u, 24u })
        {
            Truth tr; std::vector<int> chain;
            auto vb = vocalLike (48000, 10, seed, tr, &chain);
            const auto vx = an::mono (vb);
            const auto o = an::detectOnsets (vx.data(), (int64) vx.size(), 48000, an::Material::Vocal);
            std::vector<int64> f; for (auto& e : o.events) f.push_back (e.pos);
            double err = 0; const auto [P, R] = prf (f, tr.pos, (int64) (0.030 * 48000), &err);
            found += (int) std::lround (R * (double) tr.pos.size()); total += (int) tr.pos.size(); extra += (int) f.size() - (int) std::lround (R * (double) tr.pos.size()); errMs += err / 48.0;
            // legato neighbours share a word group, syllables after a silence start a new one
            for (size_t i = 0; i + 1 < tr.pos.size(); ++i)
            {
                auto groupAt = [&] (int64 p) { int g = -2; int64 bd = 2400; for (auto& e : o.events) if (std::abs (e.pos - p) < bd) { bd = std::abs (e.pos - p); g = e.group; } return g; };
                const int ga = groupAt (tr.pos[i]), gb = groupAt (tr.pos[i + 1]);
                const bool same = chain[i] == chain[i + 1];
                ++pairs; if ((same && ga >= 0 && ga == gb) || (! same && (ga != gb || ga < 0))) ++groupsOk;
            }
            if (seed == 21u) { int syl = 0; for (auto& e : o.events) if (e.flags & an::kSyllable) ++syl; check (syl >= 8, "VOCAL marks syllable onsets (" + juce::String (syl) + " of " + juce::String ((int) tr.pos.size()) + ")"); }
        }
        check (found >= (int) std::ceil (0.9 * total) && extra <= total / 8, "VOCAL: " + juce::String (found) + " / " + juce::String (total) + " syllable onsets found within 30 ms (mean error "
               + juce::String (errMs / 4.0, 1) + " ms), " + juce::String (extra) + " extra");
        check (groupsOk >= (int) std::ceil (0.8 * pairs), "VOCAL word groups: legato syllables together, a silence splits (" + juce::String (groupsOk) + " / " + juce::String (pairs) + " neighbour pairs right)");
    }
    {   // determinism + the cache
        Truth tr; auto b = clickTrain (48000, 6.0, 77, tr);
        const auto x = an::mono (b);
        const auto o1 = an::detectOnsets (x.data(), (int64) x.size(), 48000, an::Material::Vocal), o2 = an::detectOnsets (x.data(), (int64) x.size(), 48000, an::Material::Vocal);
        check (o1.digest() == o2.digest() && o1.events == o2.events, "the same audio gives the same onsets (digest " + gslice::shortFp (o1.digest()) + ")");
        auto src = source::fromBuffer (drumLoop (48000, 100, 2), 48000, "loop.wav");
        const auto cdir = tmpDir().getChildFile ("acache");
        const auto r1 = an::analyse (*src, an::Material::Auto, cdir), r2 = an::analyse (*src, an::Material::Auto, cdir), r3 = an::analyse (*src, an::Material::Auto, {});
        const auto dir = an::cache::dirFor (cdir, src->fingerprint);
        check (r1 && r2 && r3 && ! r1->fromCache && r2->fromCache && r1->digest() == r2->digest() && r1->digest() == r3->digest()
               && dir.getChildFile ("onsets-v1-auto.bin").existsAsFile() && dir.getChildFile ("tempo-v" + juce::String (an::kTempoVersion) + ".json").existsAsFile() && dir.getChildFile ("pitch-v1.bin").existsAsFile(),
               "analysis cache: versioned files under analysis/<sha>/, the second run reads them, identical results (" + gslice::shortFp (r1 ? r1->digest() : juce::String()) + ")");
        dir.getChildFile ("tempo-v" + juce::String (an::kTempoVersion) + ".json").replaceWithText ("{\"format\":\"ghostslice-tempo\",\"version\":0}");
        const auto r4 = an::analyse (*src, an::Material::Auto, cdir);
        check (r4 && r4->tempo == r1->tempo && ! r4->fromCache, "a stale / damaged cache file is recomputed, not trusted");
        check (src->onsets.size() == r1->onsets.events.size() && (src->onsets.empty() || src->onsets.front() == r1->onsets.events.front().pos), "the source's onset list (transient lock, FLEX ticks) is the new detector (AUTO)");
    }
}

// ================================================================ [15] tempo
static void testTempo()
{
    section ("[15] tempo: onset autocorrelation 60-200 BPM, loop prior, downbeat (phase 7)");
    int ok = 0, n = 0; juce::String list;
    for (auto [bpm, bars, tail] : std::vector<std::tuple<double, int, double>> { { 87, 2, 0 }, { 97, 4, 0 }, { 120, 2, 0 }, { 128, 4, 0 }, { 140, 4, 0 }, { 174, 4, 0 }, { 90, 1, 0 }, { 113, 4, 0.37 }, { 92.5, 4, 0.21 } })
    {
        const auto b = drumLoop (48000, bpm, bars, tail, (uint32_t) (bpm * 10));
        const auto x = an::mono (b);
        const auto t = an::estimateTempo (x.data(), (int64) x.size(), 48000);
        const bool good = std::abs (t.bpm - bpm) <= 1.0;
        ok += good ? 1 : 0; ++n;
        list << juce::String (bpm, 1) << "->" << juce::String (t.bpm, 2) << (t.bars > 0 ? "L" : "") << " (" << juce::roundToInt (t.confidence * 100) << "%) ";
        if (bpm == 120.0) check (std::abs (t.downbeat) < 0.010 && t.confidence > 0.5, "120 BPM loop: downbeat at " + juce::String (t.downbeat * 1000.0, 1) + " ms (kick at 0), confidence " + juce::String (t.confidence, 2));
    }
    check (ok == n, "generated loops within +-1 BPM: " + juce::String (ok) + " / " + juce::String (n) + "  " + list);
}

// ================================================================ [16] GHOST ALIGN
static juce::AudioBuffer<float> clicksAt (double sr, double secs, const std::vector<int64>& pos, float amp = 0.7f)
{
    juce::AudioBuffer<float> b (2, (int) (sr * secs)); b.clear();
    std::mt19937 rng (4); std::normal_distribution<float> N (0, 1);
    for (auto at : pos)
        for (int i = 0; i < (int) (sr * 0.006) && at + i < b.getNumSamples(); ++i)
        {
            const float v = amp * std::exp (-(float) i / (float) (sr * 0.0012)) * (0.6f * N (rng) + 0.4f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 2500.0 * i / sr));
            for (int c = 0; c < 2; ++c) b.setSample (c, (int) (at + i), v);
        }
    return b;
}
static double outAbs (const Project& p, int64 src)
{
    for (auto& r : p.regions) if (! r.reverse && src > r.srcIn() && src < r.srcOut()) return r.dstStart + r.map().outAt ((double) src);
    return -1;
}

static void testAlign()
{
    section ("[16] GHOST ALIGN: TIGHT / GROOVE / MANUAL, strength, confidence, anchors, user markers, word groups, rate limits, undo (phase 8)");
    const double sr = 48000; const int64 step = 6000;   // 1/16 at 120 BPM
    std::mt19937 rng (31); std::uniform_int_distribution<int> J (-576, 576);   // +-12 ms
    std::vector<int64> grid, pos;
    for (int i = 1; i <= 28; ++i) { grid.push_back (i * 2 * step); pos.push_back (i * 2 * step + J (rng)); }
    Rig rig (clicksAt (sr, 7.6, pos));
    { Project b = rig->project(); b.bpm = 120; b.gridDiv = 16; b.material = "DRUMS"; rig->apply (b, "grid"); }
    check (rig->waitAnalysis (30000) && rig->analysis() != nullptr && rig->alignEvents().size() == pos.size(), "the analysis runs in the background and lands (" + juce::String ((int) rig->alignEvents().size()) + " events)");
    const auto ev = rig->alignEvents();
    const Project p0 = rig->project();
    align::Settings s;
    auto pr = align::propose (p0, ev, s);
    juce::String why; auto q = align::apply (p0, pr, &why);
    double worst = 0, before = 0;
    if (q) for (size_t i = 0; i < pos.size(); ++i) { worst = std::max (worst, std::abs (outAbs (*q, pos[i]) - (double) grid[i])); before = std::max (before, std::abs ((double) (pos[i] - grid[i]))); }
    check (q.has_value() && worst <= 0.002 * sr && align::countAuto (*q) == (int) pos.size(), "TIGHT 100 %: a +-12 ms jittered click track lands within 2 ms of the grid (worst " + juce::String (worst / sr * 1000.0, 2)
           + " ms, was " + juce::String (before / sr * 1000.0, 1) + " ms; " + juce::String (q ? align::countAuto (*q) : 0) + " AUTO markers) " + why);
    if (q)
    {
        rig->apply (*q, "Auto align");
        const auto out = rig.render(); const auto ox = an::mono (out);
        const auto oo = an::detectOnsets (ox.data(), (int64) ox.size(), sr, an::Material::Drums);
        std::vector<int64> f; for (auto& e : oo.events) f.push_back (e.pos);
        double err = 0; const auto [P, R] = prf (f, grid, 96, &err);
        check (P >= 0.95 && R >= 0.99, "the rendered result: the clicks are heard on the grid (P " + juce::String (P, 2) + ", R " + juce::String (R, 2) + " within 2 ms, mean error " + juce::String (err / sr * 1000.0, 2) + " ms)");
        check (rig->undo() && align::countAuto (rig->project()) == 0 && rig->project() == p0, "UNDO takes the whole alignment back in one step");
    }
    {   // 50 %
        auto s50 = s; s50.strength = 0.5;
        auto q50 = align::apply (p0, align::propose (p0, ev, s50));
        double w50 = 0; if (q50) for (size_t i = 0; i < pos.size(); ++i) w50 = std::max (w50, std::abs (outAbs (*q50, pos[i]) - 0.5 * (double) (pos[i] + grid[i])));
        check (q50.has_value() && w50 <= 0.0006 * sr, "STRENGTH 50 %: every event moves half way (worst " + juce::String (w50 / sr * 1000.0, 2) + " ms off the half-way point)");
    }
    {   // anchors never move; events at a user marker stay
        int64 mk = 0; auto pa = ops::addMarker (p0, p0.regions[0].id, (double) pos[5], &mk); pa = ops::setAnchor (*pa, p0.regions[0].id, mk, true);
        const auto pra = align::propose (*pa, ev, s); auto qa = align::apply (*pa, pra);
        const WarpMarker* before = nullptr; const WarpMarker* after = nullptr;
        for (auto& w : pa->regions[0].warp) if (w.id == mk) before = &w;
        if (qa) for (auto& w : qa->regions[0].warp) if (w.id == mk) after = &w;
        int fixedN = 0; for (auto& it : pra.items) if (it.status == align::Status::Fixed) ++fixedN;
        check (qa && before && after && *before == *after && ops::anchorsKept (*pa, *qa) && fixedN == 1, "ANCHOR LOCK: the anchored marker is untouched (and its event stays): " + juce::String (fixedN) + " fixed");
    }
    {   // re-align after a user edit keeps the user marker
        auto q1 = *align::apply (p0, align::propose (p0, ev, s));
        int64 mid = 0; for (auto& w : q1.regions[0].warp) if (w.isAuto() && w.s == ev[10].pos) mid = w.id;
        auto qu = ops::dragMarker (q1, q1.regions[0].id, mid, (double) grid[10] + 300.0, FlexMode::Move);
        WarpMarker um; if (qu) for (auto& w : qu->regions[0].warp) if (w.id == mid) um = w;
        auto s2 = s; s2.strength = 0.5;
        auto q2 = qu ? align::apply (*qu, align::propose (*qu, ev, s2)) : std::nullopt;
        bool kept = false; if (q2) for (auto& w : q2->regions[0].warp) if (w.id == mid && w == um && ! w.isAuto()) kept = true;
        check (qu && um.kind == MarkerKind::User && q2 && kept && align::countAuto (*q2) == (int) pos.size() - 1,
               "a dragged AUTO marker becomes USER; re-align (50 %) keeps it exactly and regenerates the other " + juce::String (q2 ? align::countAuto (*q2) : 0) + " AUTO markers");
        auto qc = align::withoutAuto (*q2); bool userStays = false; for (auto& w : qc.regions[0].warp) if (w.id == mid) userStays = true;
        check (align::countAuto (qc) == 0 && userStays, "CLEAR AUTO removes only the AUTO markers");
    }
    {   // MANUAL + confidence + word groups + rate limits (crafted events)
        auto sm = s; sm.mode = align::Mode::Manual; juce::String w2;
        const auto prm = align::propose (p0, ev, sm);
        check (prm.moving == 0 && ! align::apply (p0, prm, &w2).has_value() && w2.contains ("MANUAL"), "MANUAL: no automatic moves (" + w2 + ")");
        std::vector<an::Event> ce;
        an::Event e1; e1.pos = 24000 + 300; e1.strength = 0.9f; e1.group = 1; ce.push_back (e1);
        an::Event e2; e2.pos = 24000 + 300 + 3000; e2.strength = 0.3f; e2.group = 1; ce.push_back (e2);
        an::Event e3; e3.pos = 60000 + 400; e3.strength = 0.2f; ce.push_back (e3);
        an::Event e4; e4.pos = 96000 - 350; e4.strength = 0.8f; e4.flags = an::kBreath; ce.push_back (e4);
        const auto prc = align::propose (p0, ce, s); auto qc = align::apply (p0, prc);
        const double d1 = qc ? outAbs (*qc, e1.pos) : -1, d2 = qc ? outAbs (*qc, e2.pos) : -1;
        check (qc && std::abs (d1 - 24000) < 1.0 && std::abs ((d2 - d1) - 3000.0) < 1.0 && prc.items[1].status == align::Status::Follow,
               "a word group moves rigidly with its lead (lead on the grid, the weak 2nd syllable keeps its 62.5 ms distance)");
        bool marked = false; if (qc) for (auto& w : qc->regions[0].warp) if (w.s == e3.pos || w.s == e4.pos) marked = true;
        check (qc && ! marked && prc.items[2].status == align::Status::Weak && prc.items[3].status == align::Status::Breath,
               "below the confidence threshold: untouched; a breath: untouched");
        auto ps = ops::stretchTo (p0, p0.regions[0].id, p0.regions[0].length() * 0.5);   // every segment at exactly 2x
        std::vector<an::Event> one; an::Event e5; e5.pos = 48000 + 1000; e5.strength = 0.9f; one.push_back (e5);
        const auto prr = align::propose (*ps, one, s);
        check (ps && prr.refused == 1 && prr.items[0].status == align::Status::RateLimit && prr.items[0].amber(), "a move that would stretch beyond 2x is refused and marked amber (" + prr.summary() + ")");
    }
    {   // GROOVE keeps the shared swing, TIGHT straightens it
        std::vector<int64> g16, p16; std::uniform_int_distribution<int> J4 (-192, 192);
        for (int i = 2; i <= 160; ++i) { g16.push_back (i * step); p16.push_back (i * step + (i % 2 ? 960 : 0) + J4 (rng)); }   // 10 bars: ~10 hits per groove slot
        auto src = source::fromBuffer (clicksAt (sr, 20.4, p16), sr, "swing.wav");
        Project pg = makeProject ({ src->fingerprint, "", "", "swing.wav", sr, src->frames, 2 }); pg.bpm = 120; pg.gridDiv = 16;
        const auto x = an::mono (src->audio);
        const auto evg = an::detectOnsets (x.data(), (int64) x.size(), sr, an::Material::Drums).events;
        auto sg = s; sg.mode = align::Mode::Groove;
        const auto prg = align::propose (pg, evg, sg); auto qg = align::apply (pg, prg);
        const auto qt = align::apply (pg, align::propose (pg, evg, s));
        double odd = 0, even = 0, oddT = 0, jitBefore = 0, jitAfter = 0; int no = 0, ne = 0;
        for (size_t i = 0; i < p16.size(); ++i)
        {
            const bool isOdd = (g16[i] / step) % 2 == 1;
            const double dv = qg ? outAbs (*qg, p16[i]) - (double) g16[i] : 0, dt = qt ? outAbs (*qt, p16[i]) - (double) g16[i] : 0;
            const double ideal = isOdd ? 960.0 : 0.0;
            jitBefore += std::pow ((double) (p16[i] - g16[i]) - ideal, 2); jitAfter += std::pow (dv - ideal, 2);
            if (isOdd) { odd += dv; oddT += dt; ++no; } else { even += dv; ++ne; }
        }
        odd /= no; even /= ne; oddT /= no; jitBefore = std::sqrt (jitBefore / (double) p16.size()); jitAfter = std::sqrt (jitAfter / (double) p16.size());
        check (qg && std::abs (prg.groove.swing * step - 960.0) < 120.0 && std::abs (odd - 960.0) < 100.0 && std::abs (even) < 60.0 && jitAfter < jitBefore * 0.4,
               "GROOVE: the swing (odd 16ths +20 ms, extracted " + juce::String (prg.groove.swing * step / sr * 1000.0, 1) + " ms) is kept (odd " + juce::String (odd / sr * 1000.0, 1)
               + " ms, even " + juce::String (even / sr * 1000.0, 1) + " ms), the jitter goes (" + juce::String (jitBefore / sr * 1000.0, 1) + " -> " + juce::String (jitAfter / sr * 1000.0, 1) + " ms rms)");
        check (qt && std::abs (oddT) < 60.0, "TIGHT straightens the same swing (odd 16ths at " + juce::String (oddT / sr * 1000.0, 1) + " ms)");
    }
}

// FLEX screenshots: the ALIGN preview (amber ghosts) and the applied AUTO markers, in the three looks
static void alignShots (const juce::File& shots)
{
    using namespace ghosty::themes;
    std::mt19937 rng (8); std::uniform_int_distribution<int> J (-700, 700);
    std::vector<int64> pos; for (int i = 1; i <= 15; ++i) pos.push_back (i * 12000 + J (rng));
    pos[6] += 2700;   // one far from the grid (amber)
    Rig rig (clicksAt (48000, 4.0, pos));
    { Project b = rig->project(); b.material = "DRUMS"; rig->apply (b, "drums"); }
    rig->waitAnalysis (30000);
    std::unique_ptr<SliceEditor> ed (static_cast<SliceEditor*> (rig->createEditor()));
    ed->setVisible (true); ed->setSize (1280, 780); ed->resized(); pump (20);
    ed->setWorkspace (gslice::ui::Workspace::Flex); ed->view.zoomToFit(); ed->view.hfPerPx *= 0.55; pump (10);
    bool ok = true;
    for (auto t : { Theme::Dark, Theme::Purple, Theme::Light })
    {
        ed->setTheme (t); const auto nm = displayName (t).toLowerCase();
        ok = ed->proposeAlign() && ed->view.alignPreview != nullptr && ok; ed->refresh(); pump (20);
        snap (*ed, shots.getChildFile ("slice-" + nm + "-flex-align.png"));
        ok = ed->applyAlign() && align::countAuto (rig->project()) > 0 && ok; ed->refresh(); pump (20);
        snap (*ed, shots.getChildFile ("slice-" + nm + "-flex-aligned.png"));
        rig->undo(); ed->refresh();
    }
    check (ok, "FLEX: AUTO ALIGN shows the amber preview, APPLY makes AUTO markers (screenshots in the three looks)");
    ed.reset(); pump (20);
}

// ================================================================ [17] pitch + notes
// frame-wise f0 accuracy against the generator's truth (frames within 30 ms of a note edge are skipped)
static void f0Accuracy (const an::PitchTrack& t, const std::vector<MelNote>& notes, double& within10, double& voicedRecall, double& worstMedianCents)
{
    std::vector<double> starts; double tt = 0.1; for (auto& n : notes) { starts.push_back (tt); tt += n.secs + (n.gapAfter ? 0.08 : 0.0); }
    int good = 0, cnt = 0, voiced = 0; worstMedianCents = 0;
    for (size_t k = 0; k < notes.size(); ++k)
    {
        std::vector<double> errs;
        for (int f = 0; f < t.frames(); ++f)
        {
            const double tc = t.timeOf (f), u = tc - starts[k];
            if (u < 0.03 || u > notes[k].secs - 0.03) continue;
            const double truth = notes[k].midi * 100.0 + notes[k].vibCents * std::sin (2 * juce::MathConstants<double>::pi * notes[k].vibHz * u);
            ++cnt;
            if (t.f0[(size_t) f] <= 0) continue;
            ++voiced;
            const double c = 1200.0 * std::log2 (t.f0[(size_t) f] / 440.0) + 6900.0;
            errs.push_back (c - truth);
            if (std::abs (c - truth) <= 10.0) ++good;
        }
        if (! errs.empty()) worstMedianCents = std::max (worstMedianCents, std::abs (gslice::analysis::detail::median (errs)));
    }
    within10 = cnt > 0 ? (double) good / cnt : 0; voicedRecall = cnt > 0 ? (double) voiced / cnt : 0;
}

static void testPitch()
{
    section ("[17] pitch: YIN / pYIN-style tracker + Viterbi, notes, drift / vibrato, determinism, cache (phase 13)");
    const double sr = 48000;
    {   // a pure sine glide 110 -> 880 Hz over 3 s
        juce::AudioBuffer<float> b (1, (int) (sr * 3.2)); b.clear(); double ph = 0;
        for (int i = 0; i < (int) (sr * 3.0); ++i) { const double f = 110.0 * std::pow (8.0, i / (sr * 3.0)); ph += f / sr; b.setSample (0, i + (int) (0.1 * sr), (float) (0.4 * std::sin (2 * juce::MathConstants<double>::pi * ph))); }
        const auto x = an::mono (b); const auto t = an::trackPitch (x.data(), (int64) x.size(), sr);
        int good = 0, cnt = 0;
        for (int f = 0; f < t.frames(); ++f)
        {
            const double tc = t.timeOf (f) - 0.1; if (tc < 0.05 || tc > 2.95) continue;
            ++cnt; const double truth = 110.0 * std::pow (8.0, tc / 3.0);
            if (t.f0[(size_t) f] > 0 && std::abs (1200.0 * std::log2 (t.f0[(size_t) f] / truth)) <= 10.0) ++good;
        }
        check (good >= 0.95 * cnt, "a sine sweep 110 -> 880 Hz: " + juce::String (100.0 * good / juce::jmax (1, cnt), 1) + " % of frames within 10 cents");
    }
    const std::vector<MelNote> vib { { 57, 0.5, 5.5, 40 }, { 60, 0.45, 5.0, 35, true }, { 64, 0.5, 6.0, 45 }, { 62, 0.4, 5.5, 30 }, { 67, 0.6, 5.2, 50, true }, { 65, 0.45, 5.8, 40 }, { 69, 0.5, 5.5, 35 } };
    for (bool formants : { false, true })
    {
        const auto b = melody (sr, formants ? std::vector<MelNote> { { 55, 0.5, 5.5, 30 }, { 59, 0.45, 0, 0, true }, { 62, 0.5, 5.0, 40 }, { 60, 0.4 }, { 64, 0.6, 5.5, 35, true }, { 57, 0.5 } } : vib, formants);
        const auto& notes = formants ? std::vector<MelNote> { { 55, 0.5, 5.5, 30 }, { 59, 0.45, 0, 0, true }, { 62, 0.5, 5.0, 40 }, { 60, 0.4 }, { 64, 0.6, 5.5, 35, true }, { 57, 0.5 } } : vib;
        const auto x = an::mono (b);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        const auto t = an::trackPitch (x.data(), (int64) x.size(), sr);
        const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
        double w10 = 0, vr = 0, wm = 0; f0Accuracy (t, notes, w10, vr, wm);
        const juce::String what = formants ? "a formant-shaped vocal-like line" : "vibrato notes (5-6 Hz, +-30..50 cents)";
        check (w10 >= 0.95 && vr >= 0.97, what + ": " + juce::String (w10 * 100.0, 1) + " % of frames within 10 cents of the truth, voiced " + juce::String (vr * 100.0, 1) + " % (" + juce::String (ms, 0) + " ms for " + juce::String (x.size() / sr, 1) + " s)");
        const auto o = an::detectOnsets (x.data(), (int64) x.size(), sr, an::Material::Melodic);
        const auto ns = an::segmentNotes (t, o.events);
        double worst = 0; juce::String got;
        for (size_t k = 0; k < std::min (ns.size(), notes.size()); ++k) { worst = std::max (worst, std::abs (ns[k].pitch - notes[k].midi) * 100.0); got << juce::String (ns[k].pitch, 2) << " "; }
        check (ns.size() == notes.size() && worst <= 10.0, what + ": " + juce::String ((int) ns.size()) + " notes (want " + juce::String ((int) notes.size()) + "), note pitch within " + juce::String (worst, 1) + " cents  [" + got.trim() + "]");
        if (! formants && ns.size() == notes.size())
        {
            int vibOk = 0; juce::String vs;
            for (size_t k = 0; k < notes.size(); ++k)
            {
                const double wantPP = 2.0 * notes[k].vibCents;
                if (std::abs (ns[k].vibratoHz - notes[k].vibHz) <= 0.6 && std::abs (ns[k].vibratoCents - wantPP) <= 0.3 * wantPP) ++vibOk;
                vs << juce::String (ns[k].vibratoHz, 1) << "Hz/" << juce::roundToInt (ns[k].vibratoCents) << "c ";
            }
            check (vibOk >= (int) notes.size() - 1, "vibrato rate / depth per note: " + juce::String (vibOk) + " / " + juce::String ((int) notes.size()) + " right  [" + vs.trim() + "]");
        }
    }
    {   // drift: a slow scoop is drift, not a new note
        juce::AudioBuffer<float> b (1, (int) (sr * 1.4)); b.clear(); double ph = 0;
        for (int i = 0; i < (int) (sr * 1.2); ++i) { const double u = i / sr, c = 60 * std::sin (2 * juce::MathConstants<double>::pi * 0.6 * u); const double f = 261.63 * std::pow (2.0, c / 1200.0); ph += f / sr;
            b.setSample (0, i + (int) (0.1 * sr), (float) (0.3 * std::min (1.0, u / 0.02) * std::min (1.0, (1.2 - u) / 0.02) * (std::sin (2 * juce::MathConstants<double>::pi * ph) + 0.3 * std::sin (4 * juce::MathConstants<double>::pi * ph)))); }
        const auto x = an::mono (b); const auto t = an::trackPitch (x.data(), (int64) x.size(), sr);
        const auto ns = an::segmentNotes (t, {});
        check (ns.size() == 1 && ns[0].driftCents > 20.0 && ns[0].vibratoCents == 0.0, "a slow +-60 cent wander: one note (" + juce::String ((int) ns.size()) + ") with drift " + juce::String (ns.empty() ? 0.0 : ns[0].driftCents, 1) + " cents rms, no vibrato");
    }
    {   // unvoiced + silence + octave guard
        juce::AudioBuffer<float> b (1, (int) (sr * 1.0)); std::mt19937 rng (3); std::normal_distribution<float> N (0, 0.2f);
        for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (0, i, i < 24000 ? N (rng) : 0.0f);
        const auto x = an::mono (b); const auto t = an::trackPitch (x.data(), (int64) x.size(), sr);
        int v = 0; for (auto f : t.f0) if (f > 0) ++v;
        check (v <= t.frames() / 20, "noise and silence are unvoiced (" + juce::String (v) + " of " + juce::String (t.frames()) + " frames voiced)");
        // a tone whose 2nd harmonic is much stronger than the fundamental (the classic octave trap)
        juce::AudioBuffer<float> o (1, (int) (sr * 1.0));
        for (int i = 0; i < o.getNumSamples(); ++i) { const double u = i / sr; o.setSample (0, i, (float) (0.08 * std::sin (2 * juce::MathConstants<double>::pi * 150 * u) + 0.4 * std::sin (2 * juce::MathConstants<double>::pi * 300 * u) + 0.2 * std::sin (2 * juce::MathConstants<double>::pi * 450 * u))); }
        const auto ox = an::mono (o); const auto ot = an::trackPitch (ox.data(), (int64) ox.size(), sr);
        int at150 = 0, vv = 0; for (int f = 20; f < ot.frames() - 20; ++f) if (ot.f0[(size_t) f] > 0) { ++vv; if (std::abs (1200.0 * std::log2 (ot.f0[(size_t) f] / 150.0)) < 10) ++at150; }
        check (vv > 0 && at150 >= vv * 0.95, "octave guard: a weak fundamental under strong harmonics still reads 150 Hz (" + juce::String (at150) + " / " + juce::String (vv) + " frames)");
    }
    {   // determinism + cache round trip
        const auto b = melody (sr, vib, false); const auto x = an::mono (b);
        const auto t1 = an::trackPitch (x.data(), (int64) x.size(), sr), t2 = an::trackPitch (x.data(), (int64) x.size(), sr);
        check (t1.digest() == t2.digest(), "the tracker is deterministic (" + gslice::shortFp (t1.digest()) + ")");
        const auto dir = tmpDir().getChildFile ("pcache"); an::PitchTrack back; std::vector<an::Note> nb; an::Key kb; double mf = 0;
        const auto ns = an::segmentNotes (t1, {}); const auto k = an::estimateKey (ns);
        const bool w = an::cache::writePitch (dir, t1, (int64) x.size()) && an::cache::writeNotes (dir, an::Material::Melodic, ns, k, 220.0, (int64) x.size());
        const bool r = an::cache::readPitch (dir, (int64) x.size(), back) && an::cache::readNotes (dir, an::Material::Melodic, (int64) x.size(), nb, kb, mf);
        check (w && r && back.digest() == t1.digest() && nb == ns && kb == k && mf == 220.0 && ! an::cache::readPitch (dir, (int64) x.size() + 1, back),
               "pitch + notes cache round trip (bit exact; another length is refused)");
    }
}

// ================================================================ [18] key
static std::vector<MelNote> scaleIn (int tonic, bool minor)
{   // tonic long, the scale up (harmonic minor), the tonic triad, the tonic long - around C4
    const int maj[8] { 0, 2, 4, 5, 7, 9, 11, 12 }, minr[8] { 0, 2, 3, 5, 7, 8, 11, 12 };
    const double base = 57 + ((tonic + 3) % 12);   // A3 .. G#4
    std::vector<MelNote> v; v.push_back ({ base, 0.6 });
    for (int i = 1; i < 8; ++i) v.push_back ({ base + (minor ? minr[i] : maj[i]), 0.22 });
    v.push_back ({ base + (minor ? 3 : 4), 0.3 }); v.push_back ({ base + 7, 0.3 }); v.push_back ({ base, 0.8 });
    for (auto& n : v) n.gapAfter = true;
    return v;
}
static std::vector<MelNote> progressionIn (int tonic, bool minor)
{   // I-IV-V-I (i-iv-V-i) arpeggiated
    const double b = 55 + ((tonic + 5) % 12);   // G3 .. F#4
    const int third = minor ? 3 : 4;
    const std::vector<std::vector<int>> chords { { 0, third, 7 }, { 5, 5 + third, 12 }, { 7, 11, 14 }, { 0, third, 7, 12 } };
    std::vector<MelNote> v;
    for (auto& c : chords) for (int i : c) v.push_back ({ b + i, 0.26, 0, 0, true });
    v.back().secs = 0.7;
    return v;
}

static void testKey()
{
    section ("[18] key: pitch-class profile of the notes vs Krumhansl-Kessler templates (phase 13)");
    const double sr = 48000;
    int okS = 0, okP = 0; juce::String miss;
    for (int k = 0; k < 24; ++k)
    {
        const int tonic = k % 12; const bool minor = k >= 12;
        for (int kind = 0; kind < 2; ++kind)
        {
            const auto notes = kind == 0 ? scaleIn (tonic, minor) : progressionIn (tonic, minor);
            const auto b = melody (sr, notes, kind == 1);
            const auto x = an::mono (b);
            const auto t = an::trackPitch (x.data(), (int64) x.size(), sr);
            const auto key = an::estimateKey (an::segmentNotes (t, an::detectOnsets (x.data(), (int64) x.size(), sr, an::Material::Melodic).events));
            const bool good = key.tonic == tonic && key.minor == minor;
            (kind == 0 ? okS : okP) += good ? 1 : 0;
            if (! good) miss << (kind == 0 ? "scale " : "prog ") << an::pitchClassName (tonic) << (minor ? "m" : "") << "->" << key.name() << "  ";
        }
    }
    check (okS >= 22 && okP >= 22, "24 keys: scales " + juce::String (okS) + " / 24, arpeggiated progressions " + juce::String (okP) + " / 24 right (gate >= 90 %)  " + miss);
    an::Key fs; std::array<double, 12> w {}; for (int i : { 6, 8, 9, 11, 1, 2, 4 }) w[(size_t) i] = 1.0; w[6] = 4.0; w[1] = 2.5; w[9] = 2.0;
    fs = an::estimateKey (w);
    check (fs.name() == "F# MIN" && fs.readout().startsWith ("F# MIN ") && fs.readout().endsWith (" %"), "the readout: '" + fs.readout() + "'");
    bool mn = false; const int pk = an::parseKey ("F# MIN", mn); bool mn2 = true; const int pk2 = an::parseKey ("Bb maj", mn2);
    check (pk == 6 && mn && pk2 == 10 && ! mn2 && an::keyShift (6, true, 2, true) == -4 && an::keyShift (9, true, 0, false) == 0 && an::keyShift (0, false, 7, false) == -5,
           "key names parse; MATCH shift F#m -> Dm = -4, Am -> C (relative) = 0, C -> G = -5");
}

// PITCH screenshots: note blobs on the piano roll + the f0 curve, in the three looks
static void pitchShots (const juce::File& shots)
{
    using namespace ghosty::themes;
    const std::vector<MelNote> line { { 66, 0.45, 5.5, 30 }, { 69, 0.3 }, { 73, 0.5, 5.0, 40, true }, { 71, 0.35 }, { 69, 0.3 }, { 68, 0.6, 5.5, 35, true }, { 66, 0.8, 5.2, 45 } };
    auto b = melody (48000, line, true);
    juce::AudioBuffer<float> st (2, b.getNumSamples()); st.copyFrom (0, 0, b, 0, 0, b.getNumSamples()); st.copyFrom (1, 0, b, 0, 0, b.getNumSamples());
    Rig rig (st);
    { Project q = rig->project(); q.material = "MELODIC"; q.projectKey = "G MIN"; rig->apply (q, "melodic"); }
    rig->waitAnalysis (30000);
    auto a = rig->analysis();
    std::unique_ptr<SliceEditor> ed (static_cast<SliceEditor*> (rig->createEditor()));
    ed->setVisible (true); ed->setSize (1280, 780); ed->resized(); pump (20);
    ed->setWorkspace (gslice::ui::Workspace::Pitch); ed->view.zoomToFit(); pump (10);
    const auto pr = ed->view.pitchRange();
    for (auto t : { Theme::Dark, Theme::Purple, Theme::Light })
    {
        ed->setTheme (t); ed->refresh(); pump (20);
        snap (*ed, shots.getChildFile ("slice-" + displayName (t).toLowerCase() + "-pitch-notes.png"));
    }
    check (a != nullptr && a->notes.size() == line.size() && pr.lo <= 63 && pr.hi >= 76 && ed->keyText() == a->key.name(),
           "PITCH: " + juce::String (a ? (int) a->notes.size() : 0) + " note blobs on a piano roll " + juce::String (pr.lo, 0) + ".." + juce::String (pr.hi, 0) + ", the KEY readout says " + ed->keyText() + " " + ed->keySub() + " (screenshots)");
    ed.reset(); pump (20);
}

// ================================================================ [19] global PITCH polish
static double centroid (const juce::AudioBuffer<float>& b, int from, int to)
{
    const int N = 4096; an::FFT fft (N); std::vector<double> re ((size_t) N), im ((size_t) N); const auto w = an::detail::hann (N);
    double num = 0, den = 0;
    for (int s = from; s + N <= to; s += N / 2)
    {
        for (int i = 0; i < N; ++i) { re[(size_t) i] = b.getSample (0, s + i) * w[(size_t) i]; im[(size_t) i] = 0; }
        fft.transform (re.data(), im.data(), false);
        for (int k = 1; k < N / 2; ++k) { const double f = k * 48000.0 / N; if (f < 100 || f > 5000) continue; const double m = std::sqrt (re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k]); num += f * m; den += m; }
    }
    return den > 0 ? num / den : 0;
}

static void testGlobalPitch()
{
    section ("[19] global PITCH: tonality, formant base from the median f0, MATCH PROJECT KEY, JSON (phase 9)");
    {   // JSON: the new fields round trip, old projects load with defaults
        Project p = makeProject ({ "abc", "", "", "a.wav", 48000, 96000, 2 });
        p.tonalityHz = 3500; p.projectKey = "D MIN"; p.key = "F# MIN"; p.material = "VOCAL"; p.regions[0].warp.insert (p.regions[0].warp.begin() + 1, { p.nextId++, 1000.0, 1200, false, MarkerKind::Auto });
        Project back; juce::String err;
        const bool ok = json::fromString (json::toString (p), back, err);
        auto v = json::toVar (p); v.getDynamicObject()->removeProperty ("tonalityHz"); v.getDynamicObject()->removeProperty ("projectKey"); v.getDynamicObject()->removeProperty ("material");
        Project old; json::fromVar (v, old, err);
        check (ok && back == p && back.regions[0].warp[1].isAuto() && old.tonalityHz == 8000 && old.projectKey == "--" && old.material == "AUTO",
               "JSON: tonality / project key / material / AUTO marker kind round trip; older projects get the defaults");
    }
    const std::vector<MelNote> line { { 66, 0.45, 5.5, 30 }, { 69, 0.3 }, { 73, 0.5, 5.0, 40, true }, { 71, 0.35 }, { 69, 0.3 }, { 68, 0.6, 5.5, 35, true }, { 66, 0.8, 5.2, 45 } };
    auto mb = melody (48000, line, true);
    juce::AudioBuffer<float> st (2, mb.getNumSamples()); st.copyFrom (0, 0, mb, 0, 0, mb.getNumSamples()); st.copyFrom (1, 0, mb, 0, 0, mb.getNumSamples());
    Rig rig (st);
    rig->waitAnalysis (30000);
    auto a = rig->analysis();
    {
        Project p = rig->project(); const auto fpPlain = rig->currentPlan().fingerprint;
        p.tonalityHz = 3000; rig->apply (p, "ton");
        check (rig->currentPlan().fingerprint == fpPlain, "unpitched: TONALITY changes nothing (the plan and its renders stay)");
        p.pitchSemis = 5; p.tonalityHz = 8000; rig->apply (p, "pitch");
        const auto k8 = rig->currentPlan().pieces[0].studioKey; const auto r8 = rig.render();
        p.tonalityHz = 3000; rig->apply (p, "ton 3k");
        const auto k3 = rig->currentPlan().pieces[0].studioKey; const auto r3 = rig.render();
        check (a != nullptr && rig->currentPlan().tonalityHz == 3000 && k3 != k8 && ! bitEqual (r3, r8) && r3.getNumSamples() == r8.getNumSamples(),
               "pitched: TONALITY reaches the stretcher (a new STUDIO key and a different render, same length)");
        check (a != nullptr && a->medianF0 > 300 && a->medianF0 < 600 && std::abs (rig->currentPlan().formantBaseHz - a->medianF0) < 1e-9,
               "the formant base is the analysis' median f0 (" + juce::String (a ? a->medianF0 : 0.0, 1) + " Hz) once the source is pitched");
        const int from = 4800, to = st.getNumSamples() - 9600;
        const double c0 = centroid (rig->getSource()->audio, from, to);
        p.pitchSemis = 7; p.formantSemis = 0; p.tonalityHz = 8000; rig->apply (p, "+7"); const double cKeep = centroid (rig.render(), from, to);
        p.formantSemis = 7; rig->apply (p, "+7 f+7"); const double cMove = centroid (rig.render(), from, to);
        check (cKeep / c0 < 1.2 && cMove / c0 > 1.3, "FORMANT 0 stays natural at PITCH +7 (spectral centroid x" + juce::String (cKeep / c0, 2) + "), FORMANT +7 moves it (x" + juce::String (cMove / c0, 2) + ")");
        p.pitchSemis = 0; p.formantSemis = 0; p.tonalityHz = 8000; rig->apply (p, "reset");
    }
    {   // MATCH PROJECT KEY
        an::Key det; det.tonic = 6; det.minor = true; det.confidence = 0.75;
        Project p = rig->project(); p.projectKey = "D MIN"; int sh = 99; juce::String why;
        auto q = keyops::matchProjectKey (p, det, &why, &sh);
        p.projectKey = "A MAJ"; auto rel = keyops::matchProjectKey (p, det, &why);
        const bool relRefused = ! rel.has_value() && why.contains ("Already");
        an::Key weak = det; weak.confidence = 0.4; p.projectKey = "D MIN"; juce::String why2;
        auto w = keyops::matchProjectKey (p, weak, &why2);
        p.key = "E MAJ"; p.projectKey = "G MAJ"; int sh2 = 0; auto u = keyops::matchProjectKey (p, weak, nullptr, &sh2);
        check (q && sh == -4 && q->pitchSemis == -4 && relRefused && ! w && why2.contains ("not certain") && u && sh2 == 3,
               "MATCH KEY: F#m -> Dm = -4 st; A major (relative) = no shift; a 40 % key is refused (" + why2 + "); a key set by hand (E -> G) = +3");
        if (a != nullptr && a->key.valid())
        {
            std::unique_ptr<SliceEditor> ed (static_cast<SliceEditor*> (rig->createEditor())); ed->setVisible (true); pump (10);
            Project pk = rig->project(); pk.key = "--"; pk.projectKey = "G MIN"; rig->apply (pk, "project key"); ed->refresh();
            const int want = an::keyShift (a->key.tonic, a->key.minor, 7, true);
            const bool ok = a->key.confidence >= keyops::kMinConfidence ? ed->matchProjectKey() && rig->project().pitchSemis == want : ! ed->matchProjectKey();
            check (ok, "the PITCH tab's MATCH KEY with the detected " + a->key.readout() + " and the project in G MIN: PITCH " + juce::String (rig->project().pitchSemis, 0) + " st");
            ed.reset(); pump (10);
        }
    }
}
