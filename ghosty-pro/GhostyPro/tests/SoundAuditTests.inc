// SOUND-ENGINE AUDIT (2026-10-26): `ProTests soundaudit [outdir] [sections]` - objective measurements of the whole voice path (no UI),
// offline, 48 kHz and 44.1 kHz, float analysis with its own FFT (radix-2, double). Prints a PASS / WARN / FAIL table, writes
// <outdir>/soundaudit.csv and <outdir>/presets.csv. References are mathematically ideal only (additive band-limited shapes, textbook
// bilinear-transformed filter prototypes); no third-party audio.
//   sections (default all): osc unison filter env fx gain
//   `ProTests auditrender <outdir>`         the owner's before / after renders (8 sounds x C2 / C4 / C4 major, raw f32 + lufs.txt)
//   `ProTests auditmatch <before> <after> <out>`   loudness-match both sets, 24-bit WAVs, spectrogram PNGs, index.html
// The same file builds against the pre-audit sources (the 'before' build), so every number in docs/research/PRO-SOUND-AUDIT.md
// comes from one harness on both builds.
#include <cstdarg>
namespace saudit
{
using cd = std::complex<double>;
static const double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------- own FFT (iterative radix-2, double, cached twiddles)
static void fft (std::vector<cd>& a)
{
    const size_t n = a.size();
    static std::map<size_t, std::vector<cd>> tw;
    auto& w = tw[n];
    if (w.size() != n / 2) { w.resize (n / 2); for (size_t k = 0; k < n / 2; ++k) w[k] = std::polar (1.0, -2.0 * kPi * (double) k / (double) n); }
    for (size_t i = 1, j = 0; i < n; ++i) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap (a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const size_t step = n / len, half = len / 2;
        for (size_t i = 0; i < n; i += len)
            for (size_t j = 0; j < half; ++j) { const cd u = a[i + j], v = a[i + j + half] * w[j * step]; a[i + j] = u + v; a[i + j + half] = u - v; }
    }
}
static inline double bh4w (size_t i, size_t N) { const double a = 2.0 * kPi * (double) i / (double) N; return 0.35875 - 0.48829 * std::cos (a) + 0.14128 * std::cos (2 * a) - 0.01168 * std::cos (3 * a); }
// power spectrum (bins 0 .. N/2) of x[start .. start + N) with a Blackman-Harris 4-term window (side lobes -92 dB)
static std::vector<double> powSpec (const std::vector<double>& x, size_t start, size_t N, bool window = true)
{
    std::vector<cd> a (N);
    for (size_t i = 0; i < N; ++i) a[i] = cd ((start + i < x.size() ? x[start + i] : 0.0) * (window ? bh4w (i, N) : 1.0), 0.0);
    fft (a);
    std::vector<double> p (N / 2 + 1);
    for (size_t k = 0; k <= N / 2; ++k) p[k] = std::norm (a[k]);
    return p;
}
static std::vector<double> monoOf (const juce::AudioBuffer<float>& b)
{
    std::vector<double> m ((size_t) b.getNumSamples());
    for (int i = 0; i < b.getNumSamples(); ++i) m[(size_t) i] = 0.5 * ((double) b.getSample (0, i) + (double) b.getSample (b.getNumChannels() > 1 ? 1 : 0, i));
    return m;
}
static double dbp (double p) { return 10.0 * std::log10 (std::max (1e-30, p)); }
static double dba (double a) { return 20.0 * std::log10 (std::max (1e-15, a)); }

// ---------------------------------------------------------------- the table
struct Row { std::string sec, test, cond; double value; std::string unit, target; int verdict; };
static std::vector<Row> rows;
static int nWarn = 0, nFail = 0, nPass = 0;
static const char* vname (int v) { return v == 0 ? "PASS" : v == 1 ? "WARN" : v == 2 ? "FAIL" : "INFO"; }
// lower is better: <= pass -> PASS, <= warn -> WARN, else FAIL (higher-is-better: pass the negated numbers)
static int judge (double v, double pass, double warn) { return ! std::isfinite (v) ? 2 : v <= pass ? 0 : v <= warn ? 1 : 2; }
static void rec (const std::string& sec, const std::string& test, const std::string& cond, double value, const std::string& unit, const std::string& target, int verdict)
{
    rows.push_back ({ sec, test, cond, value, unit, target, verdict });
    if (verdict == 0) ++nPass; else if (verdict == 1) ++nWarn; else if (verdict == 2) ++nFail;
    printf ("  %-4s  %-9s %-34s %-30s %10.2f %-6s %s\n", vname (verdict), sec.c_str(), test.c_str(), cond.c_str(), value, unit.c_str(), target.c_str());
    fflush (stdout);
}
static void info (const std::string& sec, const std::string& test, const std::string& cond, double value, const std::string& unit) { rec (sec, test, cond, value, unit, "-", 3); }
static std::string fmt (const char* f, ...) { char b[512]; va_list ap; va_start (ap, f); vsnprintf (b, sizeof b, f, ap); va_end (ap); return b; }
static std::string noteName (int n) { static const char* nm[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return std::string (nm[n % 12]) + std::to_string (n / 12 - 1); }

// ---------------------------------------------------------------- rendering at any rate
static std::unique_ptr<ProProcessor> procAt (double sr, int program = -1, int block = 512)
{
    seedSystemRandom (777);
    auto p = std::make_unique<ProProcessor>();
    p->prepareToPlay (sr, block);
    p->setSoundEngine (ProProcessor::engineV2);
    if (program >= 0) p->setCurrentProgram (program);
    settle (*p);
    return p;
}
struct BlockStats { double grTimeMs = 0, maxGrDb = 0; float prePeak = 0; double cpuSec = 0; };
static juce::AudioBuffer<float> renderAt (ProProcessor& p, const std::vector<Ev>& evs, double seconds, double sr, int block = 512,
                                          const std::function<void (ProProcessor&, int)>& perBlock = {}, BlockStats* st = nullptr,
                                          const std::vector<std::pair<double, int>>* bends = nullptr)
{
    const int total = (int) (seconds * sr);
    juce::AudioBuffer<float> out (2, total); out.clear();
    juce::AudioBuffer<float> blk (2, block);
    struct Head : juce::AudioPlayHead
    {
        double bpm = 120, pos = 0;
        juce::Optional<PositionInfo> getPosition() const override { PositionInfo i; i.setBpm (bpm); i.setIsPlaying (true); i.setPpqPosition (pos); i.setTimeSignature (TimeSignature { 4, 4 }); return i; }
    } head;
    p.setPlayHead (&head);
    const int preBlocks = (int) std::ceil (0.1 * sr / block);
    int bi = 0;
    for (int k = 0; k < preBlocks; ++k) { juce::MidiBuffer mb; blk.setSize (2, block, false, false, true); head.pos = -(double) (preBlocks - k) * block / sr * 2.0; seedSystemRandom (0x5eed + bi++); p.processBlock (blk, mb); }
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        juce::MidiBuffer mb;
        for (auto& e : evs) { const int s = (int) (e.t * sr); if (s >= pos && s < pos + n) mb.addEvent (e.on ? juce::MidiMessage::noteOn (1, e.note, e.vel) : juce::MidiMessage::noteOff (1, e.note), s - pos); }
        if (bends != nullptr) for (auto& b : *bends) { const int s = (int) (b.first * sr); if (s >= pos && s < pos + n) mb.addEvent (juce::MidiMessage::pitchWheel (1, b.second), s - pos); }
        if (perBlock) perBlock (p, pos);
        head.pos = pos / sr * 2.0;
        blk.setSize (2, n, false, false, true);
        seedSystemRandom (0x5eed + bi++);
#if GPRO_TAPS
        if (st) p.resetTaps();
#endif
        const auto t0 = juce::Time::getHighResolutionTicks();
        p.processBlock (blk, mb);
        if (st)
        {
            st->cpuSec += juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
            const double gr = -p.ceiling.lastBlockGrDb;
            if (gr > 0.1) st->grTimeMs += 1000.0 * n / sr;
            st->maxGrDb = std::max (st->maxGrDb, gr);
#if GPRO_TAPS
            st->prePeak = std::max (st->prePeak, p.tapPk[ProProcessor::tapGain]);
#endif
        }
        for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, blk, ch, 0, n);
    }
    p.setPlayHead (nullptr);
    return out;
}
static void setp (ProProcessor& p, std::initializer_list<std::pair<const char*, float>> kv) { for (auto& [k, v] : kv) p.setParam (k, v); }
// the bare oscillator patch: INIT, filter Off, CLEAN off, no FX / rack limiter, -6 dB trim, flat velocity, fast attack, open envelope
static std::unique_ptr<ProProcessor> oscPatch (double sr, int tbl, float pos, int quality = 1)
{
    auto p = procAt (sr); p->initPatch();
    setp (*p, { { "o1_tbl", (float) tbl }, { "o1_pos", pos }, { "f_type", 9.0f }, { "cl_on", 0.0f }, { "out_trim", -6.0f }, { "fenv", 0.0f }, { "vel_f", 0.0f },
                { "vel_amp", 0.0f }, { "attack", 0.001f }, { "sustain", 1.0f }, { "release", 0.05f }, { "fx_lim_on", 0.0f }, { "verb", 0.0f }, { "quality", (float) quality } });
    settle (*p);
    return p;
}
static double midiHz (int n) { return 440.0 * std::pow (2.0, (n - 69) / 12.0); }

// ---------------------------------------------------------------- harmonic analysis
struct Harm
{
    double aliasDb = -200;       // inharmonic energy / harmonic energy (20 Hz .. Nyquist)
    double alias20Db = -200;     // the same, 20 Hz .. 20 kHz only (what can be heard)
    double worstDb = -200;       // worst inharmonic bin re the loudest harmonic bin
    double f0 = 0;               // measured fundamental (Hz)
    double hfDefDb = 0;          // harmonic energy 8 .. 19 kHz measured vs the ideal shape (dB, < 0 = missing top)
    double bw3 = 0;              // highest harmonic still within 3 dB of the ideal (Hz)
    double dcDb = -200;          // |mean| re RMS
};
// ideal: 0 none, 1 saw (1/h), 2 square (odd 1/h), 3 triangle (odd 1/h^2), 4 sine. tolCents widens each harmonic's band (vibrato).
static Harm harm (const std::vector<double>& x, double sr, double f0, size_t start, int ideal = 0, double tolCents = 0.0, int order = 16, double tolHz = 0.0)
{
    const size_t N = (size_t) 1 << order;
    Harm h;
    auto p = powSpec (x, start, N);
    const double binW = sr / (double) N;
    const int guard = 8;
    const int H = (int) std::floor ((sr * 0.5) / f0);
    std::vector<char> isH (p.size(), 0);
    std::vector<double> hE ((size_t) H + 2, 0.0);
    for (int k = 1; k <= H; ++k)
    {
        const double fc = k * f0, w = fc * (std::pow (2.0, tolCents / 1200.0) - 1.0) + tolHz;
        const int a = std::max (1, (int) std::floor ((fc - w) / binW) - guard), b = std::min ((int) p.size() - 1, (int) std::ceil ((fc + w) / binW) + guard);
        for (int j = a; j <= b; ++j) { if (! isH[(size_t) j]) hE[(size_t) k] += p[(size_t) j]; isH[(size_t) j] = 1; }
    }
    double harmE = 0, inE = 0, inE20 = 0, topBin = 0, worst = 0;
    for (size_t j = 1; j < p.size(); ++j)
    {
        const double f = (double) j * binW;
        if (isH[j]) { harmE += p[j]; topBin = std::max (topBin, p[j]); }
        else if (f > 20.0) { inE += p[j]; if (f <= 20000.0) inE20 += p[j]; worst = std::max (worst, p[j]); }
    }
    h.aliasDb = dbp (inE / std::max (1e-30, harmE));
    h.alias20Db = dbp (inE20 / std::max (1e-30, harmE));
    if (std::getenv ("SA_DBG"))
    {   // debug: the loudest inharmonic bins
        std::vector<std::pair<double, size_t>> v; for (size_t j = 1; j < p.size(); ++j) if (! isH[j] && j * binW > 20.0) v.push_back ({ p[j], j });
        std::sort (v.rbegin(), v.rend());
        printf ("    [dbg f0 %.2f alias %.1f dB] ", f0, h.aliasDb); for (size_t i = 0; i < std::min<size_t> (12, v.size()); ++i) printf ("%.1fHz:%.0f ", v[i].second * binW, dbp (v[i].first / topBin)); printf ("\n");
    }
    h.worstDb = dbp (worst / std::max (1e-30, topBin));
    // fundamental: parabolic interpolation on the log power around the peak near f0
    {
        const int c = (int) std::round (f0 / binW); int m = c;
        for (int j = std::max (1, c - 6); j <= std::min ((int) p.size() - 2, c + 6); ++j) if (p[(size_t) j] > p[(size_t) m]) m = j;
        const double a = dbp (p[(size_t) m - 1]), b = dbp (p[(size_t) m]), cc = dbp (p[(size_t) m + 1]);
        const double den = a - 2 * b + cc; const double d = std::abs (den) > 1e-12 ? 0.5 * (a - cc) / den : 0.0;
        h.f0 = (m + d) * binW;
    }
    if (ideal >= 1 && ideal <= 3 && hE[1] > 0)
    {
        auto idealRel = [ideal] (int k) -> double { if (ideal == 1) return 1.0 / ((double) k * k); if (k % 2 == 0) return 0.0; return ideal == 2 ? 1.0 / ((double) k * k) : 1.0 / std::pow ((double) k, 4.0); };
        const double top = std::min (19000.0, 0.45 * sr);
        double mE = 0, iE = 0; h.bw3 = f0;
        bool broke = false;
        for (int k = 2; k <= H; ++k)
        {
            const double fk = k * f0; if (fk > top) break;
            const double ir = idealRel (k); if (ir <= 0) continue;
            const double mr = hE[(size_t) k] / hE[1];
            if (fk >= 8000.0) { mE += mr; iE += ir; }
            if (! broke) { if (dbp (mr / ir) > -3.0) h.bw3 = fk; else broke = true; }
        }
        h.hfDefDb = iE > 0 ? dbp (mE / iE) : 0.0;
    }
    double mean = 0, sq = 0; for (size_t i = start; i < std::min (x.size(), start + N); ++i) { mean += x[i]; sq += x[i] * x[i]; }
    mean /= (double) N; sq /= (double) N;
    h.dcDb = dba (std::abs (mean) / std::max (1e-15, std::sqrt (sq)));
    return h;
}
// ideal band-limited reference by additive synthesis (double), for the measurement floor
static std::vector<double> idealWave (int kind, double f0, double sr, size_t len)
{
    std::vector<double> x (len, 0.0);
    const int H = (int) std::floor (0.5 * sr / f0 * 0.98);
    for (int k = 1; k <= H; ++k)
    {
        double a = 0;
        if (kind == 1) a = 1.0 / k; else if (kind == 2) a = (k % 2) ? 1.0 / k : 0.0; else if (kind == 3) a = (k % 2) ? 1.0 / ((double) k * k) : 0.0; else a = k == 1 ? 1.0 : 0.0;
        if (a == 0) continue;
        const double w = 2 * kPi * k * f0 / sr;
        for (size_t i = 0; i < len; ++i) x[i] += 0.2 * a * std::sin (w * (double) i);
    }
    for (auto& v : x) v = (double) (float) v;     // the plug-in's own output word: float
    return x;
}

// ================================================================ 1. OSCILLATORS
struct OscCase { const char* name; int tbl; float pos; int ideal; };
static void oscSection()
{
    printf ("\n== 1. OSCILLATORS (single voice, filter Off, CLEAN off, QUALITY High = the V2 default; Draft rows for reference) ==\n");
    const OscCase cases[] {
        { "sine (Classic 0)", gpro::tClassic, 0.0f, 4 }, { "triangle (Classic .25)", gpro::tClassic, 0.25f, 3 }, { "saw (Saw 0)", gpro::tSaw, 0.0f, 1 },
        { "square (Pulse 0)", gpro::tPulse, 0.0f, 2 }, { "pulse 4% (Pulse 1)", gpro::tPulse, 1.0f, 0 }, { "Classic saw (.5)", gpro::tClassic, 0.5f, 1 },
        { "Harmonics .5", gpro::tHarm, 0.5f, 0 }, { "Hollow .5", gpro::tHollow, 0.5f, 0 }, { "Formant .5", gpro::tFormant, 0.5f, 0 },
        { "Organ 1", gpro::tOrgan, 1.0f, 0 }, { "Glass 1", gpro::tGlass, 1.0f, 0 }, { "FM Growl 1", gpro::tFmGrowl, 1.0f, 0 },
        { "FM Metal 1", gpro::tFmMetal, 1.0f, 0 }, { "Fold 1", gpro::tFold, 1.0f, 0 }, { "Sync 1", gpro::tSync, 1.0f, 0 },
        { "PD Res 1", gpro::tPdRes, 1.0f, 0 }, { "Dirty 1", gpro::tDirty, 1.0f, 0 }, { "Digital 1", gpro::tDigital, 1.0f, 0 },
        { "Ghost A/B .5", gpro::tGhost, 0.5f, 0 } };
    const int notes[] { 36, 60, 84, 96 };
    for (double sr : { 48000.0, 44100.0 })
    {
        const std::string srn = sr == 48000.0 ? "48k" : "44.1k";
        // measurement floor: the ideal additive saw at C7 through the same analysis
        {
            auto x = idealWave (1, midiHz (96), sr, (size_t) (1.6 * sr));
            const auto a = harm (x, sr, midiHz (96), (size_t) (0.1 * sr), 1);
            info ("osc", "analysis floor (ideal saw)", fmt ("C7 %s", srn.c_str()), a.aliasDb, "dB");
        }
        for (auto& c : cases)
            for (int n : notes)
            {
                auto p = oscPatch (sr, c.tbl, c.pos);
                auto out = renderAt (*p, { { 0.0, n, 0.9f, true }, { 2.0, n, 0.0f, false } }, 2.05, sr);
                const auto x = monoOf (out);
                const double f0 = midiHz (n);
                const auto a = harm (x, sr, f0, (size_t) (0.55 * sr), c.ideal);
                const bool primary = c.ideal == 1 || c.ideal == 2;
                // targets: saw / square below -80 dB at C4, below -60 dB at C7 (other shapes: same bars, reported as WARN at worst)
                const double pass = n <= 60 ? -80.0 : n <= 84 ? -70.0 : -60.0;
                const int v = judge (a.aliasDb, pass, pass + 10.0);
                rec ("osc", fmt ("alias %s", c.name), fmt ("%s %s", noteName (n).c_str(), srn.c_str()), a.aliasDb, "dB", fmt ("<= %.0f", pass), primary ? v : std::min (v, 1));
                rec ("osc", fmt ("pitch %s", c.name), fmt ("%s %s", noteName (n).c_str(), srn.c_str()), 1200.0 * std::log2 (a.f0 / f0), "cent", "|e| <= 1", judge (std::abs (1200.0 * std::log2 (a.f0 / f0)), 1.0, 3.0));
                if (c.ideal == 1 || c.ideal == 2)
                {
                    // HF completeness vs the ideal shape: harmonics 8 .. 19 kHz (only where the note has them)
                    if (n <= 72)
                    {
                        rec ("osc", fmt ("top band 8-19k %s", c.name), fmt ("%s %s", noteName (n).c_str(), srn.c_str()), a.hfDefDb, "dB", ">= -3", judge (-a.hfDefDb, 3.0, 6.0));
                        rec ("osc", fmt ("bandwidth (-3 dB) %s", c.name), fmt ("%s %s", noteName (n).c_str(), srn.c_str()), a.bw3 / 1000.0, "kHz", ">= 16", judge (-a.bw3 / 1000.0, -16.0, -12.0));
                    }
                }
                if (n == 60) rec ("osc", fmt ("DC %s", c.name), fmt ("C4 %s", srn.c_str()), a.dcDb, "dB", "<= -60", judge (a.dcDb, -60.0, -40.0));
            }
        // the keyboard: pitch of the sine across C1 .. C8 and the bandwidth of the saw across every semitone C2 .. C5 (worst of each)
        {
            double worstC = 0; int worstN = 0;
            for (int n = 24; n <= 108; n += 7)
            {
                auto p = oscPatch (sr, gpro::tClassic, 0.0f);
                auto out = renderAt (*p, { { 0.0, n, 0.9f, true } }, 1.0, sr);
                const auto a = harm (monoOf (out), sr, midiHz (n), (size_t) (0.25 * sr), 4, 0.0, 15);
                const double e = 1200.0 * std::log2 (a.f0 / midiHz (n)); if (std::abs (e) > std::abs (worstC)) { worstC = e; worstN = n; }
            }
            rec ("osc", "pitch sine C1..C8 worst", fmt ("%s %s", noteName (worstN).c_str(), srn.c_str()), worstC, "cent", "|e| <= 1", judge (std::abs (worstC), 1.0, 3.0));
            double minBw = 1e9, minTop = 1e9; int minN = 0, minTN = 0;
            for (int n = 36; n <= 72; ++n)
            {
                auto p = oscPatch (sr, gpro::tSaw, 0.0f);
                auto out = renderAt (*p, { { 0.0, n, 0.9f, true } }, 1.0, sr);
                const auto a = harm (monoOf (out), sr, midiHz (n), (size_t) (0.25 * sr), 1, 0.0, 15);
                if (a.bw3 < minBw) { minBw = a.bw3; minN = n; }
                if (a.hfDefDb < minTop) { minTop = a.hfDefDb; minTN = n; }
            }
            rec ("osc", "saw bandwidth worst C2..C5", fmt ("%s %s", noteName (minN).c_str(), srn.c_str()), minBw / 1000.0, "kHz", ">= 16", judge (-minBw / 1000.0, -16.0, -12.0));
            rec ("osc", "saw top band worst C2..C5", fmt ("%s %s", noteName (minTN).c_str(), srn.c_str()), minTop, "dB", ">= -3", judge (-minTop, 3.0, 6.0));
        }
    }
    // Draft (the CLASSIC rate, for reference): saw alias at C4 / C7
    for (int n : { 60, 96 })
    {
        auto p = oscPatch (48000.0, gpro::tSaw, 0.0f, 0);
        auto out = renderAt (*p, { { 0.0, n, 0.9f, true } }, 2.05, 48000.0);
        const auto a = harm (monoOf (out), 48000.0, midiHz (n), (size_t) (0.55 * 48000), 1);
        info ("osc", "alias saw at Draft (reference)", fmt ("%s 48k", noteName (n).c_str()), a.aliasDb, "dB");
    }
    // ---- with vibrato (LFO 1 5 Hz -> pitch +-20 cents) and a pitch bend held at +37 cents: the alias in the gaps between the
    // harmonics' vibrato bands (the bands are widened by the vibrato depth)
    for (int n : { 60, 96 })
    {
        for (int mode = 0; mode < 2; ++mode)
        {
            const double sr = 48000.0;
            auto p = oscPatch (sr, gpro::tSaw, 0.0f);
            std::vector<std::pair<double, int>> bends;
            if (mode == 0) setp (*p, { { "l1_rate", 5.0f }, { "m1_src", 1.0f }, { "m1_dst", 1.0f }, { "m1_amt", 0.2f / 24.0f } });
            else { setp (*p, { { "bend", 2.0f } }); bends.push_back ({ 0.0, 8192 + (int) std::lround (0.37 / 2.0 * 8191) }); }
            settle (*p);
            auto out = renderAt (*p, { { 0.0, n, 0.9f, true } }, 2.05, sr, 512, {}, nullptr, &bends);
            const double f0 = midiHz (n) * (mode == 1 ? std::pow (2.0, 0.37 / 12.0) : 1.0);
            const auto a = harm (monoOf (out), sr, f0, (size_t) (0.55 * sr), 0, mode == 0 ? 25.0 : 0.0, 16, mode == 0 ? 35.0 : 0.0);   // (vibrato: +-20 cents + the FM sidebands, 7 x 5 Hz)
            const double pass = n <= 60 ? -80.0 : -60.0;
            rec ("osc", mode == 0 ? "alias saw + vibrato 20ct" : "alias saw + bend +37ct", fmt ("%s 48k", noteName (n).c_str()), a.aliasDb, "dB", fmt ("<= %.0f", pass), judge (a.aliasDb, pass, pass + 10));
        }
    }
    // ---- mip boundary while gliding: a slow 2-octave glide on the saw; the top-band level must not step (dB jump between 20 ms frames)
    {
        const double sr = 48000.0;
        auto p = oscPatch (sr, gpro::tSaw, 0.0f);
        setp (*p, { { "glide", 3.0f }, { "mono", 1.0f }, { "legato", 1.0f } }); settle (*p);
        auto out = renderAt (*p, { { 0.0, 43, 0.9f, true }, { 0.3, 67, 0.9f, true }, { 0.31, 43, 0.0f, false } }, 4.0, sr);
        const auto x = monoOf (out);
        // energy above 10 kHz per 20 ms frame (one-pole-free: FFT of 1024 per frame), report the largest frame-to-frame step
        double worstStep = 0, prev = -1;
        for (size_t s = (size_t) (0.4 * sr); s + 1024 < x.size(); s += 960)
        {
            auto ps = powSpec (x, s, 1024);
            double hi = 0, all = 0; for (size_t k = 1; k < ps.size(); ++k) { all += ps[k]; if (k * sr / 1024 > 10000) hi += ps[k]; }
            const double r = dbp (hi / std::max (1e-30, all));
            if (prev > -900 && prev != -1) worstStep = std::max (worstStep, std::abs (r - prev));
            prev = r;
        }
        rec ("osc", "glide: top-band step per 20 ms", "saw G2->G4 3 s 48k", worstStep, "dB", "<= 1.5", judge (worstStep, 1.5, 3.0));
    }
    // ---- wavetable POS sweep: zipper score = max |2nd difference| during a 2 s sweep 0 -> 1 vs the static maxima at 17 positions
    for (int tbl : { (int) gpro::tSaw, (int) gpro::tPulse, (int) gpro::tFmGrowl, (int) gpro::tFormant })
    {
        const double sr = 48000.0; const int n = 48;
        std::vector<double> stat (17, 0.0);
        auto d2max = [] (const std::vector<double>& x, size_t a, size_t b) { double m = 0; for (size_t i = a + 2; i < b; ++i) m = std::max (m, std::abs (x[i] - 2 * x[i - 1] + x[i - 2])); return m; };
        for (int k = 0; k <= 16; ++k)
        {
            auto p = oscPatch (sr, tbl, k / 16.0f);
            auto out = renderAt (*p, { { 0.0, n, 0.9f, true } }, 0.5, sr);
            const auto x = monoOf (out); stat[(size_t) k] = d2max (x, (size_t) (0.2 * sr), x.size());
        }
        auto p = oscPatch (sr, tbl, 0.0f);
        auto out = renderAt (*p, { { 0.0, n, 0.9f, true } }, 2.6, sr, 512, [sr] (ProProcessor& q, int pos) { const double t = pos / sr - 0.3; q.setParam ("o1_pos", (float) juce::jlimit (0.0, 1.0, t / 2.0)); });
        const auto x = monoOf (out);
        double worst = 0;
        for (size_t s = (size_t) (0.3 * sr); s + 480 < (size_t) (2.3 * sr); s += 480)
        {
            const double pos = juce::jlimit (0.0, 1.0, (s / sr - 0.3) / 2.0) * 16.0; const int k = std::min (15, (int) pos);
            const double ref = std::max (stat[(size_t) k], stat[(size_t) k + 1]);
            worst = std::max (worst, d2max (x, s, s + 480) / std::max (1e-9, ref));
        }
        rec ("osc", fmt ("POS sweep zipper %s", gpro::tableNames()[tbl].toRawUTF8()), "C3 0->1 in 2 s 48k", worst, "x", "<= 1.5", judge (worst, 1.5, 3.0));
    }
    // ---- note start: the first 2 ms of each shape (attack 1 ms) - the biggest sample step re the steady-state biggest step
    for (auto& c : cases)
    {
        if (c.ideal == 0 && c.tbl != gpro::tPulse) continue;
        const double sr = 48000.0;
        auto p = oscPatch (sr, c.tbl, c.pos);
        auto out = renderAt (*p, { { 0.05, 48, 0.9f, true } }, 0.6, sr);
        const auto x = monoOf (out);
        const size_t on = (size_t) (0.05 * sr) + (size_t) p->getLatencySamples();
        double stepOn = 0, stepSt = 0;
        for (size_t i = on; i < on + (size_t) (0.003 * sr); ++i) stepOn = std::max (stepOn, std::abs (x[i] - x[i - 1]));
        for (size_t i = (size_t) (0.3 * sr); i < x.size(); ++i) stepSt = std::max (stepSt, std::abs (x[i] - x[i - 1]));
        rec ("osc", fmt ("note-on step %s", c.name), "C3 attack 1 ms", stepOn / std::max (1e-9, stepSt), "x", "<= 1.0", judge (stepOn / std::max (1e-9, stepSt), 1.0, 2.0));
    }
    // ---- engines: alias + pitch at C4 (inharmonic engines are reported, not judged)
    {
        const auto& en = gpro::eng::engineNames();
        for (int e = 0; e < gpro::eng::numEngines; ++e)
        {
            int tbl = -1; for (int t = 0; t < 64; ++t) if (gpro::eng::engineOf (t) == e) { tbl = t; break; }
            if (tbl < 0) continue;
            const double sr = 48000.0;
            auto p = oscPatch (sr, tbl, 0.0f);
            if (gpro::eng::usesInput (e)) { setp (*p, { { "o2_on", 1.0f }, { "o2_tbl", (float) gpro::tSaw }, { "o2_lvl", 0.0f } }); settle (*p); }
            auto out = renderAt (*p, { { 0.0, 60, 0.9f, true } }, 2.05, sr);
            const auto a = harm (monoOf (out), sr, midiHz (60), (size_t) (0.55 * sr), 0);
            const bool harmonic = e == gpro::eng::eVA || e == gpro::eng::eWaveshape || e == gpro::eng::eFm2 || e == gpro::eng::eAdditive;
            rec ("osc", fmt ("engine %s alias", en[e].toRawUTF8()), "C4 48k", a.aliasDb, "dB", harmonic ? "<= -70" : "(inharmonic)", harmonic ? judge (a.aliasDb, -70.0, -60.0) : 3);
            if (harmonic) rec ("osc", fmt ("engine %s pitch", en[e].toRawUTF8()), "C4 48k", 1200.0 * std::log2 (a.f0 / midiHz (60)), "cent", "|e| <= 1", judge (std::abs (1200.0 * std::log2 (a.f0 / midiHz (60))), 1.0, 3.0));
        }
    }
}

// ================================================================ 2. UNISON (V2 law)
static void unisonSection()
{
    printf ("\n== 2. UNISON (V2 law, saw, detune 30 %%, spread 100 %%, blend 100 %%, C3, 48 kHz) ==\n");
    const double sr = 48000.0;
    double ref = 0, refMono = 0;
    std::vector<double> lev (9), mono (9);
    for (int n = 1; n <= gpro::MAX_UNI; ++n)
    {
        auto p = oscPatch (sr, gpro::tSaw, 0.0f);
        setp (*p, { { "o1_uni", (float) n }, { "o1_det", 0.3f }, { "o1_spr", 1.0f }, { "o1_bln", 1.0f } }); settle (*p);
        BlockStats st;
        auto out = renderAt (*p, { { 0.0, 48, 0.9f, true } }, 2.0, sr, 512, {}, &st);
        double e = 0, m = 0, lr = 0, ll = 0, rr = 0;
        for (int i = (int) (0.3 * sr); i < out.getNumSamples(); ++i)
        {
            const double l = out.getSample (0, i), r = out.getSample (1, i);
            e += 0.5 * (l * l + r * r); m += 0.25 * (l + r) * (l + r); lr += l * r; ll += l * l; rr += r * r;
        }
        lev[(size_t) n] = dbp (e); mono[(size_t) n] = dbp (m);
        if (n == 1) { ref = lev[1]; refMono = mono[1]; }
        rec ("unison", "stereo level re 1 voice", fmt ("%d voices", n), lev[(size_t) n] - ref, "dB", "|d| <= 1.5", judge (std::abs (lev[(size_t) n] - ref), 1.5, 3.0));
        rec ("unison", "mono fold-down loss", fmt ("%d voices", n), lev[(size_t) n] - mono[(size_t) n], "dB", "<= 3", judge (lev[(size_t) n] - mono[(size_t) n], 3.0, 4.5));
        info ("unison", "L/R correlation", fmt ("%d voices", n), lr / std::sqrt (std::max (1e-30, ll * rr)), "");
        info ("unison", "CPU (one note)", fmt ("%d voices", n), 100.0 * st.cpuSec / 2.0, "% RT");
    }
    juce::ignoreUnused (refMono);
    // detune distribution: the voices' fundamentals at C6 (1 Hz resolution needs 65536 points)
    {
        auto p = oscPatch (sr, gpro::tClassic, 0.0f);
        setp (*p, { { "o1_uni", 8.0f }, { "o1_det", 0.5f }, { "o1_spr", 1.0f }, { "o1_bln", 1.0f } }); settle (*p);
        auto out = renderAt (*p, { { 0.0, 84, 0.9f, true } }, 2.0, sr);
        const auto x = monoOf (out);
        auto ps = powSpec (x, (size_t) (0.5 * sr), 65536);
        const double f0 = midiHz (84), binW = sr / 65536.0;
        double mx = 0; for (auto v : ps) mx = std::max (mx, v);
        std::string peaks; int count = 0;
        for (size_t k = 2; k + 2 < ps.size(); ++k)
        {
            const double f = k * binW; if (f < f0 * 0.93 || f > f0 * 1.07) continue;
            if (ps[k] > ps[k - 1] && ps[k] >= ps[k + 1] && ps[k] > ps[k - 2] && ps[k] >= ps[k + 2] && dbp (ps[k] / mx) > -30) { peaks += fmt ("%+.0f ", 1200.0 * std::log2 (f / f0)); ++count; }
        }
        info ("unison", "detune peaks found (8 voices, det 50%)", "C6 sine: " + peaks + "cents", count, "peaks");
    }
    // note-to-note level spread (random start phases): 10 note-ons, RMS of the first 100 ms
    {
        auto p = oscPatch (sr, gpro::tSaw, 0.0f);
        setp (*p, { { "o1_uni", 8.0f }, { "o1_det", 0.0f }, { "o1_spr", 0.0f }, { "o1_bln", 1.0f } }); settle (*p);
        std::vector<Ev> ev; for (int k = 0; k < 10; ++k) { ev.push_back ({ 0.3 * k, 48, 0.9f, true }); ev.push_back ({ 0.3 * k + 0.2, 48, 0.0f, false }); }
        auto out = renderAt (*p, ev, 3.1, sr);
        double lo = 1e9, hi = -1e9;
        for (int k = 0; k < 10; ++k) { const double r = rmsDb (out, (int) ((0.3 * k + 0.02) * sr), (int) ((0.3 * k + 0.12) * sr)); lo = std::min (lo, r); hi = std::max (hi, r); }
        rec ("unison", "note-to-note level spread", "8 voices detune 0, 10 notes", hi - lo, "dB", "<= 6 (phase random)", judge (hi - lo, 6.0, 10.0));
    }
    // comb notches in mono: harmonic levels of 8 voices at detune 0 (static phases) vs 1 voice, worst dip over harmonics 1..40
    {
        double worst = 0;
        for (int trial = 0; trial < 2; ++trial)
        {
            auto p = oscPatch (sr, gpro::tSaw, 0.0f);
            setp (*p, { { "o1_uni", 8.0f }, { "o1_det", trial == 0 ? 0.0f : 0.15f }, { "o1_spr", 1.0f }, { "o1_bln", 1.0f } }); settle (*p);
            auto out = renderAt (*p, { { 0.0, 48, 0.9f, true } }, 2.05, sr);
            const auto x = monoOf (out); const double f0 = midiHz (48);
            auto ps = powSpec (x, (size_t) (0.55 * sr), 65536); const double binW = sr / 65536.0;
            std::vector<double> hl;
            for (int h = 1; h <= 40; ++h) { double e = 0; const int c = (int) std::round (h * f0 / binW); for (int j = c - 10; j <= c + 10; ++j) e += ps[(size_t) j]; hl.push_back (dbp (e) + 20.0 * std::log10 ((double) h)); }
            double med = 0; { auto s = hl; std::sort (s.begin(), s.end()); med = s[s.size() / 2]; }
            double dip = 0; for (auto v : hl) dip = std::max (dip, med - v);
            rec ("unison", "mono comb: worst harmonic dip", trial == 0 ? "8 voices detune 0" : "8 voices detune 15%", dip, "dB", "<= 12", judge (dip, 12.0, 20.0));
            worst = std::max (worst, dip);
        }
    }
}

// ================================================================ 3. FILTERS
// textbook analog prototypes (s normalised to the cut-off), evaluated at s = j tan (pi f / fs) / tan (pi fc / fs): the bilinear transform
// with pre-warping at fc, which is what a zero-delay-feedback (TPT) filter realises exactly
static double idealMag (int type, double f, double fc, double fs, float res)
{
    const double W = std::tan (kPi * f / fs) / std::tan (kPi * fc / fs);
    const cd s (0.0, W);
    const double q = 0.7 + res * 11.0, k = 1.0 / q;
    switch (type)
    {
        case 0: return std::abs (1.0 / (s * s + k * s + 1.0));
        case 1: return std::abs (2.5 * k * s / (s * s + k * s + 1.0));
        case 2: return std::abs (s * s / (s * s + k * s + 1.0));
        case 3: return std::abs ((s * s + 1.0) / (s * s + k * s + 1.0));
        case 4: { const double kk = res * 3.95; const cd a = (1.0 + s); return std::abs ((1.0 + 0.35 * kk) / (a * a * a * a + kk)); }
        case 5: { const double k5 = 1.0 / (0.6 + res * 7.0); const cd h = s * s / (s * s + k5 * s + 1.0); return std::abs (h * h); }
        default: return 1.0;
    }
}
static void filterModTest()
{   // cutoff modulation: a SINE C4 through LP 12 (res .5) whose cutoff an LFO (0.5 Hz, +-2 octaves around the note) sweeps. The output
    // can only hold the sine +- the LFO's slow AM / PM sidebands (inside +-30 Hz); anything else - control-rate stepping lands at
    // k fs / 16 +- f0 - is zipper. Also with the fast filter envelope (decay 30 ms, 100 %, retriggered every 125 ms: 8 Hz sidebands).
    const double sr = 48000.0;
    double a[3] {};
    for (int m = 0; m < 3; ++m)
    {
        auto p = oscPatch (sr, gpro::tClassic, 0.0f);
        setp (*p, { { "f_type", 0.0f }, { "res", 0.5f }, { "cutoff", (float) (std::log2 (261.6 / 30.0) / 9.3) } });
        if (m == 1) setp (*p, { { "l1_rate", 0.5f }, { "m1_src", 1.0f }, { "m1_dst", 15.0f }, { "m1_amt", 0.215f } });
        if (m == 2) setp (*p, { { "fenv", 0.5f }, { "f_a", 0.001f }, { "f_d", 0.03f }, { "f_s", 0.0f } });
        settle (*p);
        std::vector<Ev> ev { { 0.0, 60, 0.9f, true } };
        if (m == 2) for (int k = 1; k < 17; ++k) { ev.push_back ({ 0.125 * k - 0.001, 60, 0.0f, false }); ev.push_back ({ 0.125 * k, 60, 0.9f, true }); }
        auto out = renderAt (*p, ev, 2.05, sr);
        // energy within 80 Hz of k fs / 16 +- f0 (k = 1 .. 7: where 16-sample coefficient steps put their images) re the total
        const auto x = monoOf (out); auto ps = powSpec (x, (size_t) (0.55 * sr), 65536); const double binW = sr / 65536.0;
        double z = 0, tot = 0;
        for (size_t j = 1; j < ps.size(); ++j)
        {
            const double f = j * binW; tot += ps[j];
            for (int k = 1; k <= 7; ++k) for (double sg : { -1.0, 1.0 }) if (std::abs (f - (k * sr / 16.0 + sg * midiHz (60))) < 80.0) { z += ps[j]; goto next; }
            next:;
        }
        a[m] = dbp (z / tot);
    }
    rec ("filter", "cutoff LFO zipper (fs/16 images)", "sine C4 LP12 +-2 oct 0.5 Hz", a[1], "dB", "<= -90", judge (a[1], -90.0, -70.0));
    info ("filter", "filter env: energy at fs/16 images (pluck transients included)", "sine C4 LP12 env 30 ms x8/s", a[2], "dB");
    info ("filter", "static reference (fs/16 images)", "sine C4 LP12", a[0], "dB");
}
static void filterSection()
{
    printf ("\n== 3. FILTERS ==\n");
    const auto& names = gpro::filterTypeNames();
    // ---- unit: each linear type's impulse response vs its textbook prototype (fc 1 kHz, res 0 / .5 / 1, at the engine rates 96 k / 88.2 k)
    for (double fs : { 96000.0, 88200.0 })
        for (int t = 0; t <= 5; ++t)
            for (float res : { 0.0f, 0.5f, 1.0f })
            {
                gpro::FilterChain fc; fc.prepare (fs); fc.set (t, 1000.0f, res, 0.5f, fs, 20000.0f);
                const size_t N = 1 << 16; std::vector<double> ir (N);
                const float amp = 1.0e-4f;
                for (size_t i = 0; i < N; ++i) { float yl, yr; fc.tick (i == 0 ? amp : 0.0f, 0.0f, yl, yr); ir[i] = yl / amp; }
                auto ps = powSpec (ir, 0, N, false);
                double worst = 0, peak = 0;
                for (size_t k = 1; k < ps.size(); ++k)
                {
                    const double f = k * fs / N; if (f < 20.0 || f > 20000.0) continue;
                    const double m = dbp (ps[k]), id = dba (idealMag (t, f, 1000.0, fs, res));
                    peak = std::max (peak, m);
                    if (id > -60.0) worst = std::max (worst, std::abs (m - id));
                }
                rec ("filter", fmt ("%s response vs textbook", names[t].toRawUTF8()), fmt ("res %.1f fs %.1fk", res, fs / 1000), worst, "dB", "<= 0.5", judge (worst, 0.5, 1.5));
                if (res == 1.0f && fs == 96000.0) rec ("filter", fmt ("%s peak gain at max RES", names[t].toRawUTF8()), "fc 1 kHz", peak, "dB", "<= 24", judge (peak, 24.0, 30.0));
            }
    // ---- comb: the peaks sit on multiples of the set frequency (fc 200 Hz, res .8)
    {
        const double fs = 96000.0; gpro::FilterChain fc; fc.prepare (fs); fc.set (6, 200.0f, 0.8f, 0.5f, fs, 20000.0f);
        const size_t N = 1 << 16; std::vector<double> ir (N);
        for (size_t i = 0; i < N; ++i) { float yl, yr; fc.tick (i == 0 ? 1.0e-3f : 0.0f, 0.0f, yl, yr); ir[i] = yl / 1.0e-3; }
        auto ps = powSpec (ir, 0, N, false);
        double worstC = 0;
        for (int h = 1; h <= 10; ++h)
        {
            const double binW = fs / N; const int c = (int) std::round (h * 200.0 / binW); int m = c;
            for (int j = c - 40; j <= c + 40; ++j) if (ps[(size_t) j] > ps[(size_t) m]) m = j;
            worstC = std::max (worstC, std::abs (1200.0 * std::log2 (m * binW / (h * 200.0))));
        }
        rec ("filter", "Comb + peak tuning (h1..10)", "200 Hz res .8 fs 96k", worstC, "cent", "<= 10", judge (worstC, 10.0, 25.0));
    }
    // ---- in the voice: max RES swept 20 Hz -> 20 kHz on a saw C2 + white noise, drive 0 and 100 %, both rates: no NaN, bounded peak
    for (double sr : { 48000.0, 44100.0 })
        for (int t : { 0, 1, 2, 3, 4, 5, 6, 7, 8 })
            for (float drv : { 0.0f, 1.0f })
            {
                auto p = oscPatch (sr, gpro::tSaw, 0.0f);
                setp (*p, { { "f_type", (float) t }, { "res", 1.0f }, { "f_drive", drv }, { "nz_lvl", 0.3f }, { "out_trim", 0.0f } }); settle (*p);
                BlockStats st;
                auto out = renderAt (*p, { { 0.0, 36, 0.9f, true } }, 4.0, sr, 512, [sr] (ProProcessor& q, int pos) { q.setParam ("cutoff", (float) juce::jlimit (0.0, 1.0, pos / sr / 3.8)); }, &st);
                int nans = 0; peakOf (out, &nans);
                const double pre = dba (st.prePeak);
                rec ("filter", fmt ("%s max RES sweep pre-limiter peak", names[t].toRawUTF8()), fmt ("drive %.0f%% %s", drv * 100, sr == 48000 ? "48k" : "44.1k"), pre, "dBFS", "<= +6, no NaN", nans > 0 ? 2 : judge (pre, 6.0, 12.0));
            }
    // ---- resonant peak tuning at max RES (LP 12 / LP 24) and key tracking 100 %: peak frequency of white noise through the filter
    auto peakHz = [] (const juce::AudioBuffer<float>& out, double sr, double lo, double hi)
    {
        const auto x = monoOf (out); std::vector<double> acc (8193, 0.0);
        for (size_t s = (size_t) (0.3 * sr); s + 16384 < x.size(); s += 8192) { auto ps = powSpec (x, s, 16384); for (size_t k = 0; k < acc.size(); ++k) acc[k] += ps[k]; }
        size_t m = 1; for (size_t k = 1; k + 1 < acc.size(); ++k) { const double f = k * sr / 16384; if (f >= lo && f <= hi && acc[k] > acc[m]) m = k; }
        const double a = dbp (acc[m - 1]), b = dbp (acc[m]), c = dbp (acc[m + 1]); const double d = 0.5 * (a - c) / (a - 2 * b + c);
        return (m + d) * sr / 16384;
    };
    for (int t : { 0, 4 })
        for (double target : { 200.0, 1000.0, 5000.0 })
        {
            const double sr = 48000.0;
            auto p = oscPatch (sr, gpro::tSaw, 0.0f);
            setp (*p, { { "o1_lvl", 0.0f }, { "nz_lvl", (float) juce::jlimit (0.0, 1.0, std::atof (std::getenv ("SA_NZ") ? std::getenv ("SA_NZ") : "0.05")) }, { "nz_type", 1.0f }, { "nz_col", 0.55f }, { "f_type", (float) t }, { "res", 1.0f }, { "cutoff", (float) (std::log2 (target / 30.0) / 9.3) } }); settle (*p);
            auto out = renderAt (*p, { { 0.0, 60, 0.9f, true } }, 5.0, sr);
            const double f = peakHz (out, sr, target / 2, target * 2);
            rec ("filter", fmt ("%s resonance tuning", names[t].toRawUTF8()), fmt ("%.0f Hz res 1", target), 1200.0 * std::log2 (f / target), "cent", "|e| <= 25", judge (std::abs (1200.0 * std::log2 (f / target)), 25.0, 60.0));
        }
    {
        const double sr = 48000.0; double worst = 0; std::string at;
        for (int n : { 36, 48, 72, 84 })
        {
            auto p = oscPatch (sr, gpro::tSaw, 0.0f);
            setp (*p, { { "o1_lvl", 0.0f }, { "nz_lvl", 0.5f }, { "nz_type", 1.0f }, { "f_type", 0.0f }, { "res", 0.9f }, { "f_key", 1.0f }, { "cutoff", (float) (std::log2 (1000.0 / 30.0) / 9.3) } }); settle (*p);
            auto out = renderAt (*p, { { 0.0, n, 0.9f, true } }, 2.0, sr);
            const double want = 1000.0 * std::pow (2.0, (n - 60) / 12.0);
            const double e = 1200.0 * std::log2 (peakHz (out, sr, want / 2, want * 2) / want);
            if (std::abs (e) > std::abs (worst)) { worst = e; at = noteName (n); }
        }
        rec ("filter", "key tracking 100% (LP 12)", "worst of C2..C6 at " + at, worst, "cent", "|e| <= 25", judge (std::abs (worst), 25.0, 60.0));
    }
    filterModTest();
    // ---- DRIVE nonlinearity: a sine at C6 (1046.5 Hz) through LP 12 open with FILTER DRIVE 100 % (inside the 4x oversampler)
    for (int q : { 1, 0 })
    {
        const double sr = 48000.0;
        auto p = oscPatch (sr, gpro::tClassic, 0.0f, q);
        setp (*p, { { "f_type", 0.0f }, { "cutoff", 1.0f }, { "f_drive", 1.0f }, { "out_trim", -6.0f } }); settle (*p);
        auto out = renderAt (*p, { { 0.0, 84, 0.9f, true } }, 2.05, sr);
        const auto a = harm (monoOf (out), sr, midiHz (84), (size_t) (0.55 * sr), 0);
        rec ("filter", "FILTER DRIVE 100% alias", fmt ("sine C6 %s", q == 1 ? "High" : "Draft"), a.aliasDb, "dB", "<= -70", judge (a.aliasDb, -70.0, -55.0));
        info ("filter", "FILTER DRIVE 100% alias <= 20 kHz", fmt ("sine C6 %s", q == 1 ? "High" : "Draft"), a.alias20Db, "dB");
    }
    // ---- denormals: a note, then 20 s of silence through the filter + Ghost Verb (CPU per block in the tail vs while playing)
    {
        const double sr = 48000.0;
        auto p = oscPatch (sr, gpro::tSaw, 0.0f);
        setp (*p, { { "f_type", 4.0f }, { "res", 0.7f }, { "verb", 0.6f }, { "release", 0.3f } }); settle (*p);
        BlockStats a, b;
        renderAt (*p, { { 0.0, 48, 0.9f, true }, { 1.0, 48, 0.0f, false } }, 1.0, sr, 512, {}, &a);
        renderAt (*p, {}, 20.0, sr, 512, {}, &b);
        info ("filter", "denormals: tail CPU per second re playing", "LP24 + Ghost Verb 20 s tail", (b.cpuSec / 20.0) / std::max (1e-9, a.cpuSec), "x");
    }
    // ---- DC out of the filter (saw C2 through LP 24 res .8 drive 50 %)
    {
        const double sr = 48000.0;
        auto p = oscPatch (sr, gpro::tSaw, 0.0f);
        setp (*p, { { "f_type", 4.0f }, { "res", 0.8f }, { "f_drive", 0.5f }, { "cutoff", 0.5f } }); settle (*p);
        auto out = renderAt (*p, { { 0.0, 36, 0.9f, true } }, 2.05, sr);
        rec ("filter", "DC after LP24 + drive", "saw C2 res .8 drive 50%", harm (monoOf (out), sr, midiHz (36), (size_t) (0.55 * sr), 0).dcDb, "dB", "<= -60", judge (harm (monoOf (out), sr, midiHz (36), (size_t) (0.55 * sr), 0).dcDb, -60.0, -40.0));
    }
}

// ================================================================ 4. ENVELOPES
static void envSection()
{
    printf ("\n== 4. ENVELOPES (the Env unit at 48 kHz, then in the voice) ==\n");
    const double sr = 48000.0;
    for (float t : { 0.001f, 0.01f, 0.1f, 1.0f })
    {
        gpro::Env e; e.sr = sr; e.set (t, 0.3f, 0.5f, t, 0.5f); e.on();
        int t63 = -1, t90 = -1, i = 0;
        for (; i < (int) (sr * 3 * t + 100); ++i) { const float v = e.next(); if (t63 < 0 && v >= 0.632f) t63 = i; if (t90 < 0 && v >= 0.9f) { t90 = i; break; } }
        info ("env", "attack time to 63% re set", fmt ("A %.0f ms (linear)", t * 1000), t63 / sr / t, "x");
        info ("env", "attack time to 90% re set", fmt ("A %.0f ms (linear)", t * 1000), t90 / sr / t, "x");
        gpro::Env r; r.sr = sr; r.set (0.001f, 0.1f, 1.0f, t, 0.5f); r.on(); for (int k = 0; k < 2000; ++k) r.next();
        r.off(); int r63 = -1, r90 = -1, r99 = -1;
        for (int k = 0; k < (int) (sr * 3 * t + 100); ++k) { const float v = r.next(); if (r63 < 0 && v <= 0.368f) r63 = k; if (r90 < 0 && v <= 0.1f) r90 = k; if (r99 < 0 && v <= 0.001f) { r99 = k; break; } }
        info ("env", "release time to -63% re set", fmt ("R %.0f ms curve .5", t * 1000), r63 / sr / t, "x");
        info ("env", "release time to -60 dB re set", fmt ("R %.0f ms curve .5", t * 1000), r99 / sr / t, "x");
    }
    // the decay curve shape: the level at half the decay time (curve 0 / .5 / 1): exponential would be about 0.06 .. 0.3
    for (float c : { 0.0f, 0.5f, 1.0f })
    {
        gpro::Env e; e.sr = sr; e.set (0.001f, 1.0f, 0.0f, 0.1f, c); e.on();
        float v = 0; for (int k = 0; k < (int) (sr * 0.001) + (int) (sr * 0.5); ++k) v = e.next();
        info ("env", "decay level at half the set time", fmt ("curve %.1f", c), dba (v), "dB");
    }
    // in the voice: click-free minimum attack / release and retrigger / steal continuity. Measured as SPLATTER: the energy a 4th-order
    // 4 kHz high pass finds in the 20 ms after the event (latency compensated) re the steady sine's energy over the same length. A sine
    // C4 has nothing up there, so whatever is found is the event's click. (A 1 ms raised-cosine fade scores about -62 dB, a step 0 dB.)
    auto splatter = [] (const juce::AudioBuffer<float>& out, double t, double sr, int lat)
    {
        auto x = monoOf (out);
        std::vector<double> y = x;
        auto hp = [&] (double q)
        {
            const double w = 2 * kPi * 4000.0 / sr, al = std::sin (w) / (2 * q), c = std::cos (w);
            const double b0 = (1 + c) / 2, b1 = -(1 + c), b2 = (1 + c) / 2, a0 = 1 + al, a1 = -2 * c, a2 = 1 - al;
            double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
            for (auto& v : y) { const double o = (b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) / a0; x2 = x1; x1 = v; y2 = y1; y1 = o; v = o; }
        };
        hp (0.5412); hp (1.3066);
        const size_t s0 = (size_t) (t * sr) + (size_t) lat, len = (size_t) (0.02 * sr);
        double e = 0, ref = 0; for (size_t i = s0; i < s0 + len; ++i) e += y[i] * y[i];
        const size_t r0 = (size_t) (0.3 * sr) + (size_t) lat; for (size_t i = r0; i < r0 + len; ++i) ref += x[i] * x[i];
        return dbp (e / std::max (1e-30, ref));
    };
    {
        auto p = oscPatch (sr, gpro::tClassic, 0.0f);            // sine: anything above 4 kHz is the envelope's
        setp (*p, { { "attack", 0.001f }, { "release", 0.005f } }); settle (*p);
        const int lat = p->getLatencySamples();
        auto out = renderAt (*p, { { 0.1, 60, 0.9f, true }, { 0.6, 60, 0.0f, false } }, 0.8, sr);
        info ("env", "voice path latency", "V2 High", lat, "samples");
        const double a = splatter (out, 0.098, sr, lat), r = splatter (out, 0.598, sr, lat);
        rec ("env", "min attack 1 ms: onset splatter", "sine C4 > 4 kHz", a, "dB", "<= -45", judge (a, -45.0, -30.0));
        rec ("env", "min release 5 ms: splatter", "sine C4 > 4 kHz", r, "dB", "<= -45", judge (r, -45.0, -30.0));
        auto q = oscPatch (sr, gpro::tClassic, 0.0f); setp (*q, { { "attack", 0.003f }, { "release", 0.2f } }); settle (*q);
        auto o2 = renderAt (*q, { { 0.1, 60, 0.9f, true }, { 0.6, 60, 0.0f, false } }, 0.8, sr);
        info ("env", "default attack 3 ms: onset splatter", "sine C4 > 4 kHz", splatter (o2, 0.098, sr, lat), "dB");
    }
    for (int mode = 0; mode < 3; ++mode)
    {   // 0: poly retrigger of the same note in its release, 1: one voice (steal), 2: mono retrigger (legato off)
        auto p = oscPatch (sr, gpro::tClassic, 0.0f);
        setp (*p, { { "attack", 0.02f }, { "release", 0.4f } });
        if (mode == 1) setp (*p, { { "poly", 1.0f } });
        if (mode == 2) setp (*p, { { "mono", 1.0f }, { "legato", 0.0f } });
        settle (*p);
        const int lat = p->getLatencySamples();
        auto out = renderAt (*p, { { 0.1, 60, 0.9f, true }, { 0.5, 60, 0.0f, false }, { 0.6, mode == 0 ? 60 : 64, 0.9f, true } }, 1.0, sr);
        const double r = splatter (out, 0.598, sr, lat);
        rec ("env", "retrigger: splatter", mode == 0 ? "poly same note" : mode == 1 ? "1 voice steal" : "mono retrig", r, "dB", "<= -45", judge (r, -45.0, -30.0));
    }
    {   // legato: the envelope must not restart (level dip within 10 ms of the new note < 1 dB)
        auto p = oscPatch (sr, gpro::tClassic, 0.0f);
        setp (*p, { { "mono", 1.0f }, { "legato", 1.0f }, { "attack", 0.05f } }); settle (*p);
        auto out = renderAt (*p, { { 0.1, 60, 0.9f, true }, { 0.5, 64, 0.9f, true }, { 0.51, 60, 0.0f, false } }, 0.8, sr);
        const double before = rmsDb (out, (int) (0.4 * sr), (int) (0.49 * sr)), after = rmsDb (out, (int) (0.5 * sr), (int) (0.52 * sr));
        rec ("env", "legato: level change at the new note", "mono legato", before - after, "dB", "<= 1", judge (before - after, 1.0, 3.0));
    }
}

// ================================================================ 5. FX in isolation
static double rt60Of (const std::vector<double>& ir, double sr)
{   // Schroeder backward integration, T30 (-5 .. -35 dB) extrapolated to 60 dB
    std::vector<double> e (ir.size()); double acc = 0;
    for (size_t i = ir.size(); i-- > 0;) { acc += ir[i] * ir[i]; e[i] = acc; }
    const double e0 = e[0]; int a = -1, b = -1;
    for (size_t i = 0; i < e.size(); ++i) { const double d = dbp (e[i] / e0); if (a < 0 && d <= -5) a = (int) i; if (b < 0 && d <= -35) { b = (int) i; break; } }
    if (a < 0 || b < 0) return -1;
    return (b - a) / sr * 2.0;
}
static void fxSection()
{
    printf ("\n== 5. FX in isolation (48 kHz) ==\n");
    const double sr = 48000.0;
    // ---- GHOST VERB impulse responses
    for (float decay : { 0.5f, 1.6f, 4.0f })
    {
        ghosty::GhostVerb v; v.prepare (sr);
        ghosty::GhostVerb::Params pr; pr.mix = 1.0f; pr.decay = decay; pr.predelayMs = 0; pr.loCut = 20; pr.hiCut = 20000; pr.duck = 0; pr.width = 1.0f; pr.size = 0.6f;
        pr.damp = 0.45f; pr.lowDamp = 0.35f; pr.mod = 0.5f; pr.diffusion = 0.7f;
        const size_t len = (size_t) (sr * (decay * 2.0 + 0.5)); std::vector<double> L (len), R (len);
        std::vector<float> l (512), r (512);
        for (size_t pos = 0; pos < len; pos += 512)
        {
            for (int i = 0; i < 512; ++i) { l[(size_t) i] = r[(size_t) i] = (pos + (size_t) i == 0) ? 1.0f : 0.0f; }
            v.process (l.data(), r.data(), 512, pr);
            for (int i = 0; i < 512 && pos + (size_t) i < len; ++i) { L[pos + (size_t) i] = l[(size_t) i]; R[pos + (size_t) i] = r[(size_t) i]; }
        }
        const double t = rt60Of (L, sr);
        rec ("fx", "Ghost Verb RT60 re set", fmt ("decay %.1f s", decay), t / decay, "x", "0.8 .. 1.25", judge (std::abs (std::log2 (std::max (1e-3, t / decay))), 0.32, 0.6));
        // echo density (normalised: fraction of samples above the local std in 20 ms windows, x 1 / erfc (1/sqrt 2) = 1 for Gaussian)
        for (double at : { 0.05, 0.1, 0.3 })
        {
            const size_t w = (size_t) (0.02 * sr), s0 = (size_t) (at * sr);
            double sq = 0; for (size_t i = s0; i < s0 + w; ++i) sq += L[i] * L[i];
            const double sd = std::sqrt (sq / w); int over = 0; for (size_t i = s0; i < s0 + w; ++i) if (std::abs (L[i]) > sd) ++over;
            const double ned = (double) over / w / 0.3173;
            if (decay == 1.6f) rec ("fx", "Ghost Verb echo density (NED)", fmt ("at %.0f ms", at * 1000), ned, "", ">= 0.8 by 100 ms", at < 0.09 ? 3 : judge (-ned, -0.8, -0.6));
        }
        // metallic ringing: the tail's (150 ms .. 1 s) narrow-band spectrum, worst peak above a 1/3-octave smoothed level (200 Hz .. 8 kHz)
        if (decay == 1.6f)
        {
            auto ps = powSpec (L, (size_t) (0.15 * sr), 32768);
            const double binW = sr / 32768; double worst = 0;
            for (size_t k = (size_t) (200 / binW); k < (size_t) (8000 / binW); ++k)
            {
                const size_t a = (size_t) (k / 1.12), b = std::min (ps.size() - 1, (size_t) (k * 1.12)); double m = 0; for (size_t j = a; j <= b; ++j) m += ps[j]; m /= (double) (b - a + 1);
                worst = std::max (worst, dbp (ps[k] / m));
            }
            rec ("fx", "Ghost Verb tail: worst narrow peak", "re 1/3-oct mean 200 Hz..8 kHz", worst, "dB", "<= 13 (noise ~10)", judge (worst, 13.0, 18.0));
            double lr = 0, ll = 0, rr = 0; for (size_t i = (size_t) (0.1 * sr); i < (size_t) (1.0 * sr); ++i) { lr += L[i] * R[i]; ll += L[i] * L[i]; rr += R[i] * R[i]; }
            rec ("fx", "Ghost Verb L/R correlation", "tail 0.1 .. 1 s", lr / std::sqrt (ll * rr), "", "|c| <= 0.3", judge (std::abs (lr / std::sqrt (ll * rr)), 0.3, 0.6));
        }
    }
    // ---- the synth FX chain (Shared/Fx.h) on a sine at C6: drive / crush alias, DC, level; chorus warble + mono sum; echo stability
    auto chainRun = [sr] (std::function<void (ghosty::FxParams&)> set, int note, double secs, float amp, bool impulse = false)
    {
        ghosty::FxChain fx; fx.prepare (sr, 512); ghosty::FxParams p; p.smooth = true; set (p);
        const size_t len = (size_t) (secs * sr); juce::AudioBuffer<float> b (2, 512); std::vector<double> L (len), R (len);
        const double f = midiHz (note);
        for (size_t pos = 0; pos < len; pos += 512)
        {
            for (int i = 0; i < 512; ++i) { const double t = (double) (pos + (size_t) i); const float v = impulse ? (pos + (size_t) i == 0 ? amp : 0.0f) : (float) (amp * std::sin (2 * kPi * f * t / sr)); b.setSample (0, i, v); b.setSample (1, i, v); }
            fx.process (b, p);
            for (int i = 0; i < 512 && pos + (size_t) i < len; ++i) { L[pos + (size_t) i] = b.getSample (0, i); R[pos + (size_t) i] = b.getSample (1, i); }
        }
        return std::make_pair (L, R);
    };
    for (float d : { 0.3f, 0.6f, 1.0f })
    {   // the FX DRIVE as a V2 sound plays it (through the processor: sine C6 from the voice -> FX chain)
        auto p = oscPatch (sr, gpro::tClassic, 0.0f);
        setp (*p, { { "drive", d } }); settle (*p);
        auto out = renderAt (*p, { { 0.0, 84, 0.9f, true } }, 2.05, sr);
        const auto a = harm (monoOf (out), sr, midiHz (84), (size_t) (0.55 * sr), 0);
        rec ("fx", "FX DRIVE alias <= 20 kHz", fmt ("drive %.0f%% sine C6 (V2 voice)", d * 100), a.alias20Db, "dB", "<= -70", judge (a.alias20Db, -70.0, -55.0));
        info ("fx", "FX DRIVE alias to Nyquist", fmt ("drive %.0f%% sine C6 (V2 voice)", d * 100), a.aliasDb, "dB");
        auto [L, R] = chainRun ([d] (ghosty::FxParams& q) { q.drive = d; }, 84, 1.6, 0.5f);
        info ("fx", "FX DRIVE alias, FxChain at 1x (CLASSIC)", fmt ("drive %.0f%% sine C6 -6 dBFS", d * 100), harm (L, sr, midiHz (84), (size_t) (0.2 * sr), 0).aliasDb, "dB");
        double e = 0; for (size_t i = (size_t) (0.2 * sr); i < L.size(); ++i) e += L[i] * L[i];
        info ("fx", "FX DRIVE level change", fmt ("drive %.0f%%", d * 100), dbp (e / (L.size() - 0.2 * sr)) - dbp (0.125), "dB");
    }
    {
        auto [L, R] = chainRun ([] (ghosty::FxParams& p) { p.crush = 0.6f; }, 84, 1.6, 0.5f);
        info ("fx", "FX CRUSH alias (by design)", "crush 60% sine C6", harm (L, sr, midiHz (84), (size_t) (0.2 * sr), 0).aliasDb, "dB");
    }
    for (float c : { 0.5f, 1.0f })
    {
        auto [L, R] = chainRun ([c] (ghosty::FxParams& p) { p.chorus = c; }, 84, 3.0, 0.5f);
        {   // warble of the wet path alone (the same juce::dsp::Chorus settings as FxChain, mix 1, no feedback): zero-crossing periods
            juce::dsp::Chorus<float> ch; ch.prepare ({ sr, 512, 1 }); ch.setRate (0.8f); ch.setDepth (0.2f + c * 0.5f); ch.setCentreDelay (12.0f); ch.setFeedback (0.0f); ch.setMix (1.0f);
            std::vector<double> w ((size_t) (3 * sr)); juce::AudioBuffer<float> bb (1, 512);
            for (size_t pos = 0; pos < w.size(); pos += 512)
            {
                for (int i = 0; i < 512; ++i) bb.setSample (0, i, (float) (0.5 * std::sin (2 * kPi * midiHz (84) * (double) (pos + (size_t) i) / sr)));
                juce::dsp::AudioBlock<float> blk (bb); ch.process (juce::dsp::ProcessContextReplacing<float> (blk));
                for (int i = 0; i < 512 && pos + (size_t) i < w.size(); ++i) w[pos + (size_t) i] = bb.getSample (0, i);
            }
            std::vector<double> zc; for (size_t i = (size_t) (0.3 * sr); i < w.size(); ++i) if (w[i - 1] < 0 && w[i] >= 0) zc.push_back ((double) i - w[i] / (w[i] - w[i - 1]));
            std::vector<double> per; for (size_t k = 1; k < zc.size(); ++k) per.push_back (zc[k] - zc[k - 1]);
            std::vector<double> sm; for (size_t k = 8; k + 8 < per.size(); ++k) { double a = 0; for (size_t j = k - 8; j <= k + 8; ++j) a += per[j]; sm.push_back (a / 17.0); }
            double mx = 0, mn = 1e9; for (auto v : sm) { mx = std::max (mx, v); mn = std::min (mn, v); }
            info ("fx", "chorus wet pitch warble (peak-peak)", fmt ("chorus %.0f%% sine C6", c * 100), 1200.0 * std::log2 (mx / mn), "cent");
        }
        double em = 0, es = 0; for (size_t i = (size_t) (0.3 * sr); i < L.size(); ++i) { em += 0.25 * (L[i] + R[i]) * (L[i] + R[i]); es += 0.5 * (L[i] * L[i] + R[i] * R[i]); }
        rec ("fx", "chorus mono-sum loss", fmt ("chorus %.0f%%", c * 100), dbp (es) - dbp (em), "dB", "<= 3", judge (dbp (es) - dbp (em), 3.0, 6.0));
    }
    {
        auto [L, R] = chainRun ([] (ghosty::FxParams& p) { p.echo = 1.0f; p.haunt = 1.0f; }, 60, 12.0, 0.5f, true);
        double lateSq = 0; for (size_t i = (size_t) (11 * sr); i < (size_t) (12 * sr); ++i) lateSq += L[i] * L[i] + R[i] * R[i];
        double earlySq = 0; for (size_t i = 0; i < (size_t) sr; ++i) earlySq += L[i] * L[i] + R[i] * R[i];
        rec ("fx", "echo + haunt at 100%: decay after 11 s", "impulse", dbp (lateSq / earlySq), "dB", "<= -40 (stable)", judge (dbp (lateSq / earlySq), -40.0, -20.0));
    }
    // ---- rack WRECKER / TUBE GLOW (via the processor, rack only): alias of a C6 sine at drive 30 / 100 %
    {
        struct U { const char* name; const char* on; const char* drv; int style; };
        const U units[] { { "WRECKER TAPE", "fx_wreck_on", "fx_wreck_drive", 0 }, { "WRECKER FUZZ", "fx_wreck_on", "fx_wreck_drive", 5 }, { "TUBE GLOW", "fx_tube_on", "fx_tube_drive", -1 } };
        for (auto& u : units)
            for (float d : { 0.3f, 1.0f })
            {
                auto p = oscPatch (sr, gpro::tClassic, 0.0f);
                p->setParam (u.on, 1.0f); p->setParam (u.drv, d); if (u.style >= 0) p->setParam ("fx_wreck_style", (float) u.style);
                if (u.style >= 0) p->setParam ("fx_wreck_mix", 1.0f); else p->setParam ("fx_tube_mix", 1.0f);
                settle (*p);
                auto out = renderAt (*p, { { 0.0, 84, 0.9f, true } }, 2.05, sr);
                const auto a = harm (monoOf (out), sr, midiHz (84), (size_t) (0.55 * sr), 0);
                rec ("fx", fmt ("rack %s alias <= 20 kHz", u.name), fmt ("drive %.0f%% sine C6", d * 100), a.alias20Db, "dB", "<= -70", judge (a.alias20Db, -70.0, -55.0));
                info ("fx", fmt ("rack %s alias to Nyquist", u.name), fmt ("drive %.0f%% sine C6", d * 100), a.aliasDb, "dB");
            }
    }
    // ---- wet / dry gain matching: pink-ish noise (white through a -3 dB/oct shaper is overkill: white) RMS out re in per FX at its knob 50 %
    {
        struct K { const char* name; std::function<void (ghosty::FxParams&)> set; };
        const K ks[] { { "chorus 50%", [] (ghosty::FxParams& p) { p.chorus = 0.5f; } }, { "phaser 50%", [] (ghosty::FxParams& p) { p.phaser = 0.5f; } },
                       { "echo 50%", [] (ghosty::FxParams& p) { p.echo = 0.5f; } }, { "haunt 50%", [] (ghosty::FxParams& p) { p.haunt = 0.5f; } },
                       { "drive 50%", [] (ghosty::FxParams& p) { p.drive = 0.5f; } } };
        for (auto& k : ks)
        {
            ghosty::FxChain fx; fx.prepare (sr, 512); ghosty::FxParams p; p.smooth = true; k.set (p);
            juce::Random rnd (3); juce::AudioBuffer<float> b (2, 512); double ein = 0, eout = 0;
            for (int blk = 0; blk < (int) (3 * sr / 512); ++blk)
            {
                for (int i = 0; i < 512; ++i) { const float v = (rnd.nextFloat() * 2 - 1) * 0.25f; b.setSample (0, i, v); b.setSample (1, i, v); if (blk > 40) ein += 2.0 * v * v; }
                fx.process (b, p);
                if (blk > 40) for (int i = 0; i < 512; ++i) eout += (double) b.getSample (0, i) * b.getSample (0, i) + (double) b.getSample (1, i) * b.getSample (1, i);
            }
            rec ("fx", "wet/dry level change", k.name, dbp (eout / ein), "dB", "|d| <= 2", judge (std::abs (dbp (eout / ein)), 2.0, 4.0));
        }
    }
}

// ================================================================ 6. GAIN STAGING over every factory sound
static std::vector<Ev> c3Note() { return { { 0.0, 48, 100.0f / 127.0f, true }, { 3.0, 48, 0.0f, false } }; }
static std::vector<Ev> c3Chord() { std::vector<Ev> e; for (int n : { 48, 52, 55, 60 }) { e.push_back ({ 0.0, n, 100.0f / 127.0f, true }); e.push_back ({ 3.0, n, 0.0f, false }); } return e; }
// shard k of N renders every N-th sound into presets-k.csv; `gainsum` reads every presets-*.csv in the folder and judges the bank
static void gainSection (const juce::File& outDir, int shard = 0, int shards = 1)
{
    printf ("\n== 6. GAIN STAGING: every factory sound (V2), C3 note + C3 major chord, velocity 100, 4 s, 48 kHz (shard %d / %d) ==\n", shard, shards);
    const auto& bank = gpro::factoryBank();
    juce::String csv;
    const double sr = 48000.0;
    for (int i = shard; i < (int) bank.size(); i += shards)
    {
        const auto name = bank[(size_t) i].name, cat = bank[(size_t) i].cat;
        for (int k = 0; k < 2; ++k)
        {
            auto p = procAt (sr, i);
            BlockStats st;
            auto out = renderAt (*p, k == 0 ? c3Note() : c3Chord(), 4.0, sr, 512, {}, &st);
            int nans = 0; const double pk = dba (peakOf (out, &nans));
            const double tp = cleantest::truePeakDb (out), lu = lufs (out);
            double mean = 0; for (int ch = 0; ch < 2; ++ch) for (int s = 0; s < out.getNumSamples(); ++s) mean += out.getSample (ch, s);
            const double dc = dba (std::abs (mean) / (2.0 * out.getNumSamples()));
            double mmax = -100; for (int s0 = 0; s0 + (int) (0.4 * sr) <= out.getNumSamples(); s0 += (int) (0.1 * sr)) mmax = std::max (mmax, lufs (out, s0, s0 + (int) (0.4 * sr)));
            csv << i << ",\"" << name << "\",\"" << cat << "\"," << (k == 0 ? "C3" : "chord") << "," << juce::String (pk, 2) << "," << juce::String (tp, 2) << "," << juce::String (lu, 2) << "," << juce::String (dc, 1) << ","
                << juce::String (dba (st.prePeak), 2) << "," << juce::String (st.grTimeMs, 0) << "," << juce::String (st.maxGrDb, 2) << "," << nans << "," << juce::String (mmax, 2) << "\n";
        }
        if ((i / shards) % 10 == 0) { printf ("  ... %d / %d\n", i, (int) bank.size()); fflush (stdout); }
    }
    outDir.getChildFile ("presets-" + juce::String (shard) + ".csv").replaceWithText (csv);
}
static void gainSummary (const juce::File& outDir)
{
    printf ("\n== 6. GAIN STAGING summary (presets-*.csv in %s) ==\n", outDir.getFullPathName().toRawUTF8());
    struct R { int idx; juce::String name, cat; double v[2][8]; };   // per render: pk tp lufs dc pre grMs grMax momentaryMax
    std::map<int, R> m;
    for (auto& f : outDir.findChildFiles (juce::File::findFiles, false, "presets-*.csv"))
    {
        juce::StringArray lines; f.readLines (lines);
        for (auto& l : lines)
        {
            auto t = juce::StringArray::fromTokens (l, ",", "\""); if (t.size() < 12) continue;
            auto& r = m[t[0].getIntValue()]; r.idx = t[0].getIntValue(); r.name = t[1].unquoted(); r.cat = t[2].unquoted();
            const int k = t[3] == "C3" ? 0 : 1; for (int j = 0; j < 7; ++j) r.v[k][j] = t[4 + j].getDoubleValue();
            r.v[k][7] = t.size() > 12 ? t[12].getDoubleValue() : r.v[k][2];
        }
    }
    juce::String all ("name,category,render,sample_peak_dbfs,true_peak_dbtp,lufs,dc_db,pre_limiter_peak_dbfs,limiter_ms,limiter_max_gr_db,momentary_max_lufs\n");
    for (auto& [i, r] : m) for (int k = 0; k < 2; ++k) { all << "\"" << r.name << "\",\"" << r.cat << "\"," << (k == 0 ? "C3" : "chord"); for (int j = 0; j < 8; ++j) all << "," << juce::String (r.v[k][j], 2); all << "\n"; }
    outDir.getChildFile ("presets.csv").replaceWithText (all);
    for (int k = 0; k < 2; ++k)
    {
        const char* label = k == 0 ? "C3 note" : "C3 chord";
        std::vector<double> v; for (auto& [i, r] : m) if (r.v[k][2] > -70) v.push_back (r.v[k][2]);
        if (v.empty()) return;
        std::sort (v.begin(), v.end()); const double med = v[v.size() / 2];
        const double p05 = v[(size_t) (0.05 * v.size())], p95 = v[(size_t) (0.95 * v.size())];
        std::vector<std::pair<double, juce::String>> dev; for (auto& [i, r] : m) if (r.v[k][2] > -70) dev.push_back ({ r.v[k][2] - med, r.name });
        std::sort (dev.begin(), dev.end());
        int out3 = 0; for (auto& d : dev) if (std::abs (d.first) > 3.0) ++out3;
        juce::String lo, hi; int nl = 0, nh = 0;
        for (auto& d : dev) if (d.first < -3.0 && nl < 10) { lo << d.second << " (" << juce::String (d.first, 1) << ") "; ++nl; }
        for (auto it = dev.rbegin(); it != dev.rend(); ++it) if (it->first > 3.0 && nh < 10) { hi << it->second << " (+" << juce::String (it->first, 1) << ") "; ++nh; }
        info ("gain", fmt ("%s median loudness", label), fmt ("%d sounds", (int) v.size()), med, "LUFS");
        rec ("gain", fmt ("%s loudness 5..95%% range", label), fmt ("%.1f .. %.1f LUFS", p05, p95), p95 - p05, "LU", "<= 6 (+-3)", judge (p95 - p05, 6.0, 10.0));
        rec ("gain", fmt ("%s sounds outside +-3 LU", label), "of the median", (double) out3, "sounds", "<= 5% of bank", judge (out3 / (double) v.size(), 0.05, 0.15));
        printf ("        quietest: %s\n        loudest:  %s\n", lo.toRawUTF8(), hi.toRawUTF8());
        {   // the loudest 400 ms (momentary max): how loud a sound's hit / peak section is - the level a user hears when browsing
            std::vector<double> mv; for (auto& [i, r] : m) if (r.v[k][7] > -70) mv.push_back (r.v[k][7]);
            std::sort (mv.begin(), mv.end()); const double mmed = mv[mv.size() / 2];
            int o3 = 0; for (auto x : mv) if (std::abs (x - mmed) > 3.0) ++o3;
            info ("gain", fmt ("%s median momentary-max loudness", label), fmt ("%d sounds", (int) mv.size()), mmed, "LUFS");
            rec ("gain", fmt ("%s momentary-max 5..95%% range", label), fmt ("%.1f .. %.1f LUFS", mv[(size_t) (0.05 * mv.size())], mv[(size_t) (0.95 * mv.size())]), mv[(size_t) (0.95 * mv.size())] - mv[(size_t) (0.05 * mv.size())], "LU", "<= 6 (+-3)", judge (mv[(size_t) (0.95 * mv.size())] - mv[(size_t) (0.05 * mv.size())], 6.0, 10.0));
            rec ("gain", fmt ("%s momentary-max outside +-3 LU", label), "of the median", (double) o3, "sounds", "<= 5% of bank", judge (o3 / (double) mv.size(), 0.05, 0.15));
        }
        int hot = 0, limited = 0, preHot = 0; double worstTp = -200, worstPre = -200, worstDc = -200; juce::String wTp, wPre, wDc;
        for (auto& [i, r] : m)
        {
            const double tp = r.v[k][1], pre = r.v[k][4], gr = r.v[k][5], dc = r.v[k][3];
            if (tp > -0.5) ++hot; if (gr > 100.0) ++limited; if (pre > 0.0) ++preHot;
            if (tp > worstTp) { worstTp = tp; wTp = r.name; } if (pre > worstPre) { worstPre = pre; wPre = r.name; } if (dc > worstDc) { worstDc = dc; wDc = r.name; }
        }
        rec ("gain", fmt ("%s true peak worst", label), wTp.toStdString(), worstTp, "dBTP", "<= -0.5", judge (worstTp, -0.5, 0.0));
        rec ("gain", fmt ("%s pre-limiter peak worst", label), wPre.toStdString(), worstPre, "dBFS", "<= 0", judge (worstPre, 0.0, 6.0));
        rec ("gain", fmt ("%s sounds over 0 dBFS pre-limiter", label), "before the hidden ceiling", (double) preHot, "sounds", "<= 5% of bank", judge (preHot / (double) m.size(), 0.05, 0.2));
        rec ("gain", fmt ("%s sounds limited > 100 ms", label), "the hidden ceiling", (double) limited, "sounds", "<= 5% of bank", judge (limited / (double) m.size(), 0.05, 0.2));
        rec ("gain", fmt ("%s DC worst", label), wDc.toStdString(), worstDc, "dBFS", "<= -50", judge (worstDc, -50.0, -40.0));
    }
}

// ---------------------------------------------------------------- entry
static int run (const juce::StringArray& args)
{
    const bool dirGiven = args.size() > 0 && args[0].startsWithChar ('/');
    const juce::File outDir (dirGiven ? args[0] : juce::String ("/root/work/pro-audit-out"));
    outDir.createDirectory();
    juce::StringArray secs; for (int i = dirGiven ? 1 : 0; i < args.size(); ++i) secs.add (args[i]);
    auto want = [&] (const char* s) { return secs.isEmpty() || secs.contains (s); };
    printf ("SOUND AUDIT -> %s\n  verdict  section   test                               condition                           value  unit   target\n", outDir.getFullPathName().toRawUTF8());
    if (want ("osc")) oscSection();
    if (want ("unison")) unisonSection();
    if (want ("filter")) filterSection();
    if (secs.contains ("fmod")) filterModTest();
    if (want ("env")) envSection();
    if (want ("fx")) fxSection();
    for (auto& a : secs) if (a.startsWith ("shard="))
    {   // gain shard k/N
        const int k = a.fromFirstOccurrenceOf ("=", false, false).upToFirstOccurrenceOf ("/", false, false).getIntValue(), n = a.fromFirstOccurrenceOf ("/", false, false).getIntValue();
        gainSection (outDir, k, std::max (1, n)); return 0;
    }
    if (secs.isEmpty() || secs.contains ("gain")) gainSection (outDir);
    if (secs.isEmpty() || secs.contains ("gain") || secs.contains ("gainsum")) gainSummary (outDir);
    juce::String csv ("section,test,condition,value,unit,target,verdict\n");
    for (auto& r : rows) csv << r.sec << ",\"" << r.test << "\",\"" << r.cond << "\"," << juce::String (r.value, 3) << "," << r.unit << ",\"" << r.target << "\"," << vname (r.verdict) << "\n";
    outDir.getChildFile (secs.isEmpty() ? juce::String ("soundaudit.csv") : "soundaudit-" + secs.joinIntoString ("-") + ".csv").replaceWithText (csv);
    printf ("\nSOUND AUDIT: %d PASS, %d WARN, %d FAIL (%d info rows)\n", nPass, nWarn, nFail, (int) rows.size() - nPass - nWarn - nFail);
    return 0;
}

// a development probe: `ProTests auditprobe <note> [k=v ...]` on the bare oscillator patch (saw), prints the analysis
static int probe (const juce::StringArray& args)
{
    const double sr = 48000.0; const int note = args.size() > 0 ? args[0].getIntValue() : 60;
    auto p = oscPatch (sr, gpro::tSaw, 0.0f);
    for (int i = 1; i < args.size(); ++i) p->setParam (args[i].upToFirstOccurrenceOf ("=", false, false), args[i].fromFirstOccurrenceOf ("=", false, false).getFloatValue());
    settle (*p);
    BlockStats st;
    auto out = renderAt (*p, { { 0.0, note, 0.9f, true } }, 2.05, sr, 512, {}, &st);
    const double tc = std::getenv ("SA_TOLC") ? std::atof (std::getenv ("SA_TOLC")) : 0.0, th = std::getenv ("SA_TOLHZ") ? std::atof (std::getenv ("SA_TOLHZ")) : 0.0;
    const auto a = harm (monoOf (out), sr, midiHz (note), (size_t) (0.55 * sr), 1, tc, 16, th);
    printf ("alias20k %.1f dB  alias %.1f dB  worst %.1f dB  f0 %.3f Hz  top8-19k %.1f dB  bw3 %.0f Hz  DC %.1f dB  pre-limiter %.1f dBFS  rms %.1f dBFS  cpu %.2f %%\n",
            a.alias20Db, a.aliasDb, a.worstDb, a.f0, a.hfDefDb, a.bw3, a.dcDb, dba (st.prePeak), rmsDb (out, (int) (0.5 * sr), out.getNumSamples()), 100 * st.cpuSec / 2.05);
    return 0;
}

#if __has_include ("ProV2Norm.inc")
// `ProTests auditnorm <gain dir> <ProV2Norm.inc>`: from a gain run's presets.csv (gainsum), the V2 loudness normalisation table. Each
// sound's level = the mean of the loudest 400 ms of its C3 note and of its C3 chord; the new entry = its current entry + (bank median -
// level), within +-10 dB; a boost never takes the pre-ceiling peak of either render above -1.5 dBFS (so a CLEAN-off sound never reaches
// the old soft clip), silent renders keep their entry.
static int auditNorm (const juce::File& dir, const juce::File& outInc)
{
    juce::StringArray lines; dir.getChildFile ("presets.csv").readLines (lines);
    struct R { juce::String name; double mm[2] { -100, -100 }, pre[2] { -100, -100 }; };
    std::map<juce::String, R> m;
    for (auto& l : lines)
    {
        auto t = juce::StringArray::fromTokens (l, ",", "\""); if (t.size() < 11 || t[0] == "name") continue;
        auto& r = m[t[0].unquoted()]; r.name = t[0].unquoted(); const int k = t[2] == "C3" ? 0 : 1;
        r.mm[k] = t[10].getDoubleValue(); r.pre[k] = t[7].getDoubleValue();
    }
    std::vector<double> lv; for (auto& [n, r] : m) if (r.mm[0] > -60 && r.mm[1] > -60) lv.push_back (0.5 * (r.mm[0] + r.mm[1]));
    if (lv.empty()) { printf ("no data in %s\n", dir.getFullPathName().toRawUTF8()); return 1; }
    std::sort (lv.begin(), lv.end()); const double target = lv[lv.size() / 2];
    juce::String out ("// generated by ProTests auditnorm (sound audit 2026-10-26): { name, dB added to the V2 trim } - each factory sound's loudest 400 ms\n"
                      "// (mean of a C3 note and a C3 major chord, velocity 100) at the bank median ");
    out << juce::String (target, 2) << " LUFS; boosts capped so the pre-ceiling peak stays under -1.5 dBFS\n";
    int n = 0, capped = 0;
    for (auto& [nm, r] : m)
    {
        const double old = ProProcessor::factoryV2NormDb (nm);
        double d = old;
        if (r.mm[0] > -60 && r.mm[1] > -60)
        {
            d = old + (target - 0.5 * (r.mm[0] + r.mm[1]));
            d = juce::jlimit (-10.0, 10.0, d);
            const double room = -1.5 - std::max (r.pre[0], r.pre[1]);           // how much more the current renders can take
            if (d - old > room && d > old) { d = old + std::max (0.0, room); ++capped; }
        }
        d = std::round (d * 100.0) / 100.0;
        if (std::abs (d) >= 0.01) { out << "{ \"" << nm << "\", " << juce::String (d, 2) << "f },\n"; ++n; }
    }
    outInc.replaceWithText (out);
    printf ("auditnorm: target %.2f LUFS, %d entries (%d boosts capped by the peak), wrote %s\n", target, n, capped, outInc.getFullPathName().toRawUTF8());
    return 0;
}
#endif

// ================================================================ owner renders (before / after)
static const char* const kOwnerSounds[] { "Ladder Fat Bass", "Future Bass Pluck", "Warm Analog Pad", "Glide Saw Lead", "Velvet EP", "Synth Brass Stab", "Laser Zap", "808 Boom",
                                          "Grand Ghost Piano", "Crystal Pluck", "Hyper Saw Anthem", "Init" };   // ("Init" = the INIT patch)   // (the last three: the closest Pro sounds to ArpEZator presets)
// every take: 2.4 s held, 3 s rendered, velocity 100
struct OwnerTake { const char* tag; std::vector<int> notes; };
static const std::vector<OwnerTake>& ownerTakes()
{
    static const std::vector<OwnerTake> t { { "C2", { 36 } }, { "C4", { 60 } }, { "C4maj", { 60, 64, 67 } }, { "C5maj", { 72, 76, 79 } }, { "C6", { 84 } } };
    return t;
}
static std::vector<Ev> takeEvents (const OwnerTake& t) { std::vector<Ev> e; for (int n : t.notes) { e.push_back ({ 0.0, n, 100.0f / 127.0f, true }); e.push_back ({ 2.4, n, 0.0f, false }); } return e; }
static int auditRender (const juce::File& dir)
{
    dir.createDirectory();
    const double sr = 48000.0;
    juce::String lu;
    for (auto* nm : kOwnerSounds)
    {
        const bool init = juce::String (nm) == "Init";
        const int prog = init ? -1 : progOf (nm);
        if (prog < 0 && ! init) { printf ("no sound %s\n", nm); continue; }
        for (auto& t : ownerTakes())
        {
            auto p = procAt (sr, prog);
            if (init) { p->initPatch(); settle (*p); }
            auto out = renderAt (*p, takeEvents (t), 3.0, sr);
            const auto f = dir.getChildFile (safeName (nm) + "_" + t.tag + ".f32");
            writeRaw (out, f);
            lu << safeName (nm) << "_" << t.tag << " " << juce::String (lufs (out), 2) << " " << juce::String (cleantest::truePeakDb (out), 2) << "\n";
        }
        printf ("  rendered %s\n", nm); fflush (stdout);
    }
    // the spectrogram sources: saw C6 (bare oscillator) and the INIT saw C4 major chord
    {
        auto p = oscPatch (sr, gpro::tSaw, 0.0f); setp (*p, { { "out_trim", 0.0f } }); settle (*p);
        auto out = renderAt (*p, { { 0.0, 84, 0.9f, true }, { 2.4, 84, 0.0f, false } }, 3.0, sr);
        writeRaw (out, dir.getChildFile ("spec_saw_C6.f32"));
        lu << "spec_saw_C6 " << juce::String (lufs (out), 2) << " " << juce::String (cleantest::truePeakDb (out), 2) << "\n";
        auto q = oscPatch (sr, gpro::tSaw, 0.0f); setp (*q, { { "out_trim", 0.0f } }); settle (*q);
        std::vector<Ev> chord; for (int n : { 60, 64, 67 }) { chord.push_back ({ 0.0, n, 0.8f, true }); chord.push_back ({ 2.4, n, 0.0f, false }); }
        auto o2 = renderAt (*q, chord, 3.0, sr);
        writeRaw (o2, dir.getChildFile ("spec_saw_C4maj.f32"));
        lu << "spec_saw_C4maj " << juce::String (lufs (o2), 2) << " " << juce::String (cleantest::truePeakDb (o2), 2) << "\n";
    }
    dir.getChildFile ("lufs.txt").replaceWithText (lu);
    return 0;
}

#if __has_include ("../../ArpEZator/ArpSynth.h") && __has_include ("ProV2Norm.inc")
} // namespace saudit
#include "../../ArpEZator/ArpSynth.h"
namespace saudit
{
// ================================================================ ArpEZator's preview synth vs Pro (the owner's reference)
// `ProTests auditarp <renders dir>`: ArpEZator presets on the owner takes, through its Engine (its FX: reverb 30 %, delay off, output
// clip) into <dir>/raw-arp and as the bare LeadVoice into <dir>/raw-arp-dry, named after the paired Pro sound; then the same measures on
// those and on <dir>/raw-before + <dir>/raw-after (Pro) -> <dir>/arp-compare.csv + a printed table. Also ArpSynth's raw saw (PolyBLEP, 1x).
struct ArpPair { const char* pro; int arp; };
static const ArpPair kArpPairs[] { { "Future Bass Pluck", arpsyn::Neon }, { "Grand Ghost Piano", arpsyn::Piano }, { "Crystal Pluck", arpsyn::DuskPluck },
                                   { "Warm Analog Pad", arpsyn::SadPad }, { "808 Boom", arpsyn::Bass808 }, { "Hyper Saw Anthem", arpsyn::Supersaw },
                                   { "Glide Saw Lead", arpsyn::HypnoSaw }, { "Velvet EP", arpsyn::Keys }, { "Ladder Fat Bass", arpsyn::Acid }, { "Synth Brass Stab", arpsyn::Euphoric }, { "Init", arpsyn::Pluck } };
static juce::AudioBuffer<float> arpRender (int preset, const std::vector<int>& notes, double sr, bool withFx)
{
    const int total = (int) (3.0 * sr), len = (int) (2.4 * sr), B = 512;
    juce::AudioBuffer<float> out (2, total); out.clear();
    const float vel = 100.0f / 127.0f;
    if (withFx)
    {
        auto e = std::make_unique<arpsyn::Engine>(); e->prepare (sr, B);
        arpsyn::Params P; P.preset = preset; P.delay = 0.0f; P.pump = 0.0f;      // (reverb at its default 0.3, volume 0.8)
        int c = 100; for (int n : notes) e->leadStart (1, c++, (float) n, vel, len, P);
        std::vector<float> l ((size_t) B), r ((size_t) B);
        for (int pos = 0; pos < total; pos += B)
        {
            const int n = std::min (B, total - pos);
            e->render (l.data(), r.data(), n, P, 120.0, pos * (96.0 * 2.0 / sr), 96.0 * 2.0 / sr, false);
            for (int i = 0; i < n; ++i) { out.setSample (0, pos + i, l[(size_t) i]); out.setSample (1, pos + i, r[(size_t) i]); }
        }
        return out;
    }
    std::vector<std::unique_ptr<arpsyn::LeadVoice>> vs;
    int c = 0; for (int n : notes) { vs.push_back (std::make_unique<arpsyn::LeadVoice>()); vs.back()->start (preset, (float) n, vel, len, 1, c, (juce::uint64) (c + 1), (float) sr); ++c; }
    std::vector<float> l ((size_t) B), r ((size_t) B);
    for (int pos = 0; pos < total; pos += B)
    {
        const int n = std::min (B, total - pos);
        std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f);
        for (auto& v : vs) if (v->active) v->render (l.data(), r.data(), n, (float) sr, 1.0f, 0.0f);
        for (int i = 0; i < n; ++i) { out.setSample (0, pos + i, l[(size_t) i]); out.setSample (1, pos + i, r[(size_t) i]); }
    }
    return out;
}
struct AM { double lufsI, peak, crest, centroid, roll85, sideDb, corr, attackMs, tailDb, brightDecayOct, alias20, momMax; };
static AM aMeasure (const juce::AudioBuffer<float>& b, double sr, double f0)
{
    AM m {};
    const auto x = monoOf (b);
    m.lufsI = lufs (b); m.peak = dba (peakOf (b)); m.crest = m.peak - rmsDb (b, 0, (int) (2.4 * sr));
    double mm = -100; for (int s0 = 0; s0 + (int) (0.4 * sr) <= b.getNumSamples(); s0 += (int) (0.1 * sr)) mm = std::max (mm, lufs (b, s0, s0 + (int) (0.4 * sr)));
    m.momMax = mm;
    auto centroidOf = [&] (size_t a, size_t e, double* roll)
    {
        std::vector<double> acc (4097, 0.0); for (size_t s0 = a; s0 + 8192 <= e; s0 += 4096) { auto ps = powSpec (x, s0, 8192); for (size_t k = 0; k < acc.size(); ++k) acc[k] += ps[k]; }
        if (e - a < 8192) { auto ps = powSpec (x, a, 8192); for (size_t k = 0; k < acc.size(); ++k) acc[k] += ps[k]; }
        double num = 0, den = 0; for (size_t k = 1; k < acc.size(); ++k) { num += acc[k] * k * sr / 8192; den += acc[k]; }
        if (roll) { double c = 0; *roll = 0; for (size_t k = 1; k < acc.size(); ++k) { c += acc[k]; if (c >= 0.85 * den) { *roll = k * sr / 8192; break; } } }
        return den > 0 ? num / den : 0.0;
    };
    m.centroid = centroidOf (0, (size_t) (2.4 * sr), &m.roll85);
    const double cEarly = centroidOf ((size_t) (0.02 * sr), (size_t) (0.02 * sr) + 8192, nullptr), cLate = centroidOf ((size_t) (1.0 * sr), (size_t) (2.2 * sr), nullptr);
    m.brightDecayOct = cLate > 0 && cEarly > 0 ? std::log2 (cEarly / cLate) : 0;
    double mid = 0, side = 0, lr = 0, ll = 0, rr = 0;
    for (int i = 0; i < (int) (2.4 * sr); ++i) { const double l = b.getSample (0, i), r = b.getSample (1, i); mid += 0.25 * (l + r) * (l + r); side += 0.25 * (l - r) * (l - r); lr += l * r; ll += l * l; rr += r * r; }
    m.sideDb = dbp (side / std::max (1e-30, mid)); m.corr = lr / std::sqrt (std::max (1e-30, ll * rr));
    // attack: 1 ms RMS envelope, 10 % -> 90 % of its maximum
    { const int w = (int) (0.001 * sr); std::vector<double> env; for (int s0 = 0; s0 + w < (int) (1.0 * sr); s0 += w) { double e = 0; for (int i = 0; i < w; ++i) e += x[(size_t) (s0 + i)] * x[(size_t) (s0 + i)]; env.push_back (std::sqrt (e / w)); }
      double mx = 0; for (auto v : env) mx = std::max (mx, v); int a10 = -1, a90 = -1; for (int i = 0; i < (int) env.size(); ++i) { if (a10 < 0 && env[(size_t) i] >= 0.1 * mx) a10 = i; if (a90 < 0 && env[(size_t) i] >= 0.9 * mx) { a90 = i; break; } }
      m.attackMs = a10 >= 0 && a90 >= 0 ? (double) (a90 - a10) : -1; }
    m.tailDb = rmsDb (b, (int) (2.6 * sr), (int) (3.0 * sr)) - rmsDb (b, (int) (0.1 * sr), (int) (2.4 * sr));
    m.alias20 = f0 > 0 ? harm (x, sr, f0, (size_t) (0.05 * sr), 0, 0.0, 16).alias20Db : 0.0;
    return m;
}
static int auditArp (const juce::File& dir)
{
    const double sr = 48000.0;
    auto fx = dir.getChildFile ("raw-arp"), dry = dir.getChildFile ("raw-arp-dry"); fx.createDirectory(); dry.createDirectory();
    juce::String pairs;
    for (auto& pr : kArpPairs)
    {
        pairs << safeName (pr.pro) << "|" << arpsyn::presetName (pr.arp) << "\n";
        for (auto& t : ownerTakes())
        {
            writeRaw (arpRender (pr.arp, t.notes, sr, true), fx.getChildFile (safeName (pr.pro) + "_" + t.tag + ".f32"));
            writeRaw (arpRender (pr.arp, t.notes, sr, false), dry.getChildFile (safeName (pr.pro) + "_" + t.tag + ".f32"));
        }
    }
    fx.getChildFile ("pairs.txt").replaceWithText (pairs);
    juce::String csv ("pro_sound,arp_preset,take,source,lufs_i,momentary_max,peak_dbfs,crest_db,centroid_hz,rolloff85_hz,side_re_mid_db,lr_corr,attack_10_90_ms,tail_db,bright_decay_oct,alias_20k_db\n");
    printf ("\n%-20s %-14s %-6s %-11s %7s %7s %6s %8s %8s %7s %6s %7s %7s %6s %7s\n", "Pro sound", "Arp preset", "take", "source", "LUFS", "momMax", "crest", "centroid", "roll85", "side", "corr", "att ms", "tail", "brDec", "alias");
    for (auto& pr : kArpPairs)
        for (auto& t : ownerTakes())
        {
            const juce::String f = safeName (pr.pro) + "_" + t.tag + ".f32";
            const double f0 = t.notes.size() == 1 ? midiHz (t.notes[0]) : 0.0;
            const std::pair<const char*, juce::File> src[] { { "pro-before", dir.getChildFile ("raw-before").getChildFile (f) }, { "pro-after", dir.getChildFile ("raw-after").getChildFile (f) },
                                                             { "arp-fx", fx.getChildFile (f) }, { "arp-raw", dry.getChildFile (f) } };
            for (auto& [nm, file] : src)
            {
                auto b = readRaw (file); if (b.getNumSamples() < (int) (3.0 * sr) - 1) continue;
                const auto m = aMeasure (b, sr, f0);
                printf ("%-20s %-14s %-6s %-11s %7.1f %7.1f %6.1f %8.0f %8.0f %7.1f %6.2f %7.0f %7.1f %6.2f %7.1f\n", pr.pro, arpsyn::presetName (pr.arp), t.tag, nm, m.lufsI, m.momMax, m.crest, m.centroid, m.roll85, m.sideDb, m.corr, m.attackMs, m.tailDb, m.brightDecayOct, m.alias20);
                csv << "\"" << pr.pro << "\",\"" << arpsyn::presetName (pr.arp) << "\"," << t.tag << "," << nm;
                for (double v : { m.lufsI, m.momMax, m.peak, m.crest, m.centroid, m.roll85, m.sideDb, m.corr, m.attackMs, m.tailDb, m.brightDecayOct, m.alias20 }) csv << "," << juce::String (v, 2);
                csv << "\n";
            }
        }
    // the raw oscillators side by side: ArpSynth's PolyBLEP saw (1x, as LeadVoice plays it) vs the ideal; Pro's are in the audit (osc rows)
    for (int n : { 36, 60, 84, 96 })
    {
        const double f0 = midiHz (n); std::vector<double> x ((size_t) (2.0 * sr)); float ph = 0; const float dt = (float) (f0 / sr);
        for (auto& v : x) { v = 0.2 * arpsyn::sawB (ph, dt); ph += dt; if (ph >= 1.0f) ph -= 1.0f; }
        const auto a = harm (x, sr, f0, (size_t) (0.1 * sr), 1);
        printf ("ArpSynth raw saw (PolyBLEP 1x) %s: alias (to Nyquist) %.1f dB, alias <= 20 kHz %.1f dB, worst line %.1f dB, top band 8-19k %+.1f dB, bandwidth %.1f kHz\n", noteName (n).c_str(), a.aliasDb, a.alias20Db, a.worstDb, a.hfDefDb, a.bw3 / 1000.0);
        csv << "\"(raw saw)\",\"PolyBLEP saw\"," << noteName (n) << ",arp-raw-osc,0,0,0,0,0,0,0,0,0,0,0," << juce::String (a.alias20Db, 2) << "\n";
    }
    dir.getChildFile ("arp-compare.csv").replaceWithText (csv);
    printf ("wrote %s\n", dir.getChildFile ("arp-compare.csv").getFullPathName().toRawUTF8());
    return 0;
}
#endif

// ================================================================ BANK MINING (owner's friend's three complaints): diversity, design hygiene, raw tones
// `ProTests auditbank <dir> [shard=k/N]`  every factory sound (V2): what it is built from (tables / engines / filter / unison / mods / FX)
//                                          + timbre features of a C3 note and a C3 major chord (4 s, 3 s held, velocity 100) -> bank-k.csv
// `ProTests auditbanksum <dir> <outCsv>`   histograms, timbre clusters, near-duplicate pairs, hygiene scores, keep / hide list
// `ProTests audittables <dir>`             the raw tone of every wavetable (POS 0 .. 1) and engine: richness, alias, evolution along POS
struct BankFeat
{
    double band[24] {};                 // spectral shape: band level re the total (dB), 40 Hz .. 16 kHz log-spaced, held part
    double centroid = 0, flat = 0, attackMs = 0, sideDb = 0, moveDb = 0, levelMoveDb = 0, tailDb = 0, harsh = 0, low = 0, mud = 0, lufs = -100, momMax = -100;
};
static BankFeat bankFeat (const juce::AudioBuffer<float>& b, double sr)
{
    BankFeat f;
    const auto x = monoOf (b);
    const int N = 4096; const double binW = sr / N;
    std::vector<double> edges; for (int k = 0; k <= 24; ++k) edges.push_back (40.0 * std::pow (400.0, k / 24.0));
    std::vector<std::vector<double>> frames; std::vector<double> mean (N / 2 + 1, 0.0); std::vector<double> tot;
    for (size_t s0 = (size_t) (0.3 * sr); s0 + N <= (size_t) (3.0 * sr); s0 += N / 2)
    {
        auto ps = powSpec (x, s0, N);
        std::vector<double> bd (24, 0.0); double t = 0;
        for (size_t k = 1; k < ps.size(); ++k) { mean[k] += ps[k]; const double fr = k * binW; t += ps[k]; for (int j = 0; j < 24; ++j) if (fr >= edges[(size_t) j] && fr < edges[(size_t) j + 1]) { bd[(size_t) j] += ps[k]; break; } }
        frames.push_back (bd); tot.push_back (t);
    }
    double T = 0; for (size_t k = 1; k < mean.size(); ++k) T += mean[k];
    std::vector<double> mb (24, 0.0); for (auto& fr : frames) for (int j = 0; j < 24; ++j) mb[(size_t) j] += fr[(size_t) j];
    for (int j = 0; j < 24; ++j) f.band[j] = std::max (-80.0, dbp (mb[(size_t) j] / std::max (1e-30, T)));
    double num = 0, lg = 0, ar = 0; int nb = 0;
    for (size_t k = 1; k < mean.size(); ++k) { num += mean[k] * k * binW; const double fr = k * binW; if (fr > 100 && fr < 10000) { lg += std::log (std::max (1e-30, mean[k])); ar += mean[k]; ++nb; } }
    f.centroid = T > 0 ? num / T : 0; f.flat = nb > 0 ? dbp (std::exp (lg / nb) / std::max (1e-30, ar / nb)) : -100;
    auto share = [&] (double a, double e) { double v = 0; for (size_t k = 1; k < mean.size(); ++k) { const double fr = k * binW; if (fr >= a && fr < e) v += mean[k]; } return dbp (v / std::max (1e-30, T)); };
    f.harsh = share (2000, 5000); f.low = share (20, 120); f.mud = share (150, 500);
    // movement: how much the spectral SHAPE changes over the held part (std over frames of each band re its frame's total, mean over the
    // bands that are within 40 dB of the loudest); levelMove: the frame level's wobble around its own trend (tremolo, gate, pump)
    {
        double acc = 0; int used = 0; double top = -300; for (int j = 0; j < 24; ++j) top = std::max (top, f.band[j]);
        double tmax = 0; for (auto t : tot) tmax = std::max (tmax, t);      // (only the frames within 30 dB of the loudest: a decayed tail is not "movement")
        for (int j = 0; j < 24; ++j)
        {
            if (f.band[j] < top - 40) continue;
            double m = 0, q = 0; int c = 0;
            for (size_t i = 0; i < frames.size(); ++i) { if (tot[i] < tmax * 1.0e-3) continue; const double v = std::max (-60.0, dbp (frames[i][(size_t) j] / std::max (1e-30, tot[i]))); m += v; q += v * v; ++c; }
            if (c > 1) { m /= c; acc += std::sqrt (std::max (0.0, q / c - m * m)); ++used; }
        }
        f.moveDb = used ? acc / used : 0;
        std::vector<double> ty; for (auto t : tot) if (t >= tmax * 1.0e-3) ty.push_back (dbp (t));
        const int n = (int) ty.size(); double sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (int i = 0; i < n; ++i) { const double y = ty[(size_t) i]; sx += i; sy += y; sxx += (double) i * i; sxy += i * y; }
        const double slope = n > 1 ? (n * sxy - sx * sy) / std::max (1e-9, n * sxx - sx * sx) : 0, icpt = n > 0 ? (sy - slope * sx) / n : 0;
        double r = 0; for (int i = 0; i < n; ++i) { const double e = ty[(size_t) i] - (icpt + slope * i); r += e * e; }
        f.levelMoveDb = n > 0 ? std::sqrt (r / n) : 0;
    }
    { const int w = (int) (0.001 * sr); std::vector<double> env; for (int s0 = 0; s0 + w < (int) (1.0 * sr); s0 += w) { double e = 0; for (int i = 0; i < w; ++i) e += x[(size_t) (s0 + i)] * x[(size_t) (s0 + i)]; env.push_back (std::sqrt (e / w)); }
      double mx = 0; for (auto v : env) mx = std::max (mx, v); int a10 = -1, a90 = -1; for (int i = 0; i < (int) env.size(); ++i) { if (a10 < 0 && env[(size_t) i] >= 0.1 * mx) a10 = i; if (a90 < 0 && env[(size_t) i] >= 0.9 * mx) { a90 = i; break; } }
      f.attackMs = a10 >= 0 && a90 >= 0 ? (double) (a90 - a10) : 1000; }
    double mid = 0, side = 0; for (int i = (int) (0.1 * sr); i < (int) (3.0 * sr); ++i) { const double l = b.getSample (0, i), r = b.getSample (1, i); mid += 0.25 * (l + r) * (l + r); side += 0.25 * (l - r) * (l - r); }
    f.sideDb = std::max (-60.0, dbp (side / std::max (1e-30, mid)));
    f.tailDb = std::max (-100.0, rmsDb (b, (int) (3.1 * sr), (int) (3.6 * sr)) - rmsDb (b, (int) (0.1 * sr), (int) (3.0 * sr)));
    f.lufs = lufs (b);
    double mm = -100; for (int s0 = 0; s0 + (int) (0.4 * sr) <= b.getNumSamples(); s0 += (int) (0.1 * sr)) mm = std::max (mm, lufs (b, s0, s0 + (int) (0.4 * sr))); f.momMax = mm;
    return f;
}
static juce::String featCsv (const BankFeat& f)
{
    juce::String s; for (double v : f.band) s << juce::String (v, 2) << ",";
    for (double v : { f.centroid, f.flat, f.attackMs, f.sideDb, f.moveDb, f.levelMoveDb, f.tailDb, f.harsh, f.low, f.mud, f.lufs, f.momMax }) s << juce::String (v, 2) << ",";
    return s;
}
static int auditBank (const juce::File& dir, int shard, int shards)
{
    dir.createDirectory();
    const auto& bank = gpro::factoryBank(); const double sr = 48000.0;
    juce::String csv;
    const auto& tn = gpro::eng::tableChoiceNames(); const auto& ftn = gpro::filterTypeNames();
    for (int i = shard; i < (int) bank.size(); i += shards)
    {
        auto p = procAt (sr, i);
        auto v = [&] (const char* id) { auto* a = p->apvts.getRawParameterValue (id); return a != nullptr ? a->load() : 0.0f; };
        const int t1 = (int) v ("o1_tbl"), t2 = (int) v ("o2_tbl");
        int mods = 0; for (int k = 1; k <= 8; ++k) { const juce::String b2 = "m" + juce::String (k); if ((int) v ((b2 + "_src").toRawUTF8()) > 0 && (int) v ((b2 + "_dst").toRawUTF8()) > 0 && std::abs (v ((b2 + "_amt").toRawUTF8())) > 1e-4f) ++mods; }
        int rackOn = 0; for (auto* prm : p->getParameters()) if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm)) { const auto id = rp->getParameterID(); if (id.startsWith ("fx_") && id.endsWith ("_on") && id != "fx_lim_on" && rp->getValue() > 0.5f) ++rackOn; }
        csv << i << ",\"" << bank[(size_t) i].name << "\",\"" << bank[(size_t) i].cat << "\",\"" << (v ("o1_on") > 0.5f ? tn[t1] : juce::String ("-")) << "\",\"" << (v ("o2_on") > 0.5f ? tn[t2] : juce::String ("-")) << "\","
            << juce::String (v ("o1_pos"), 2) << "," << (int) v ("o1_uni") << ",\"" << ftn[juce::jlimit (0, ftn.size() - 1, (int) v ("f_type"))] << "\"," << juce::String (v ("res"), 2) << "," << juce::String (v ("fenv"), 2) << ","
            << juce::String (v ("sub_lvl"), 2) << "," << juce::String (v ("nz_lvl"), 2) << "," << (int) v ("xmod") << "," << mods << "," << (int) v ("ly2_src") + (int) v ("ly3_src") << "," << rackOn << ","
            << juce::String (v ("verb"), 2) << "," << juce::String (v ("chorus"), 2) << "," << juce::String (v ("drive"), 2) << "," << (int) v ("o1_w1") + (int) v ("o1_w2") << "," << (int) v ("arp") << ",";
        for (int k = 0; k < 2; ++k)
        {
            auto q = procAt (sr, i);
            auto out = renderAt (*q, k == 0 ? c3Note() : c3Chord(), 4.0, sr);
            csv << featCsv (bankFeat (out, sr));
        }
        csv << "\n";
        if ((i / shards) % 10 == 0) { printf ("  ... %d / %d\n", i, (int) bank.size()); fflush (stdout); }
    }
    dir.getChildFile ("bank-" + juce::String (shard) + ".csv").replaceWithText (csv);
    return 0;
}
static constexpr int kMeta = 22, kFeat = 36;     // columns per row: meta, then 2 x features (24 bands + 12)
static int auditBankSum (const juce::File& dir, const juce::File& outCsv)
{
    struct P { int idx; juce::String name, cat, t1, t2, filt; double pos, res, fenv, sub, nz, verb, chorus, drive; int uni, xmod, mods, layers, rack, warp, arp; BankFeat f[2]; int cluster = -1; double nn = 0; juce::String nnName; double hygiene = 0; juce::String flags; double ident = 0; };
    std::vector<P> ps;
    for (auto& file : dir.findChildFiles (juce::File::findFiles, false, "bank-*.csv"))
    {
        juce::StringArray lines; file.readLines (lines);
        for (auto& l : lines)
        {
            auto t = juce::StringArray::fromTokens (l, ",", "\""); if (t.size() < kMeta - 1 + 2 * kFeat) continue;
            P p; int c = 0;
            p.idx = t[c++].getIntValue(); p.name = t[c++].unquoted(); p.cat = t[c++].unquoted(); p.t1 = t[c++].unquoted(); p.t2 = t[c++].unquoted();
            p.pos = t[c++].getDoubleValue(); p.uni = t[c++].getIntValue(); p.filt = t[c++].unquoted(); p.res = t[c++].getDoubleValue(); p.fenv = t[c++].getDoubleValue();
            p.sub = t[c++].getDoubleValue(); p.nz = t[c++].getDoubleValue(); p.xmod = t[c++].getIntValue(); p.mods = t[c++].getIntValue(); p.layers = t[c++].getIntValue(); p.rack = t[c++].getIntValue();
            p.verb = t[c++].getDoubleValue(); p.chorus = t[c++].getDoubleValue(); p.drive = t[c++].getDoubleValue(); p.warp = t[c++].getIntValue(); p.arp = t[c++].getIntValue();
            for (int k = 0; k < 2; ++k)
            {
                auto& f = p.f[k]; for (auto& b : f.band) b = t[c++].getDoubleValue();
                f.centroid = t[c++].getDoubleValue(); f.flat = t[c++].getDoubleValue(); f.attackMs = t[c++].getDoubleValue(); f.sideDb = t[c++].getDoubleValue(); f.moveDb = t[c++].getDoubleValue();
                f.levelMoveDb = t[c++].getDoubleValue(); f.tailDb = t[c++].getDoubleValue(); f.harsh = t[c++].getDoubleValue(); f.low = t[c++].getDoubleValue(); f.mud = t[c++].getDoubleValue(); f.lufs = t[c++].getDoubleValue(); f.momMax = t[c++].getDoubleValue();
            }
            ps.push_back (p);
        }
    }
    std::sort (ps.begin(), ps.end(), [] (const P& a, const P& b) { return a.idx < b.idx; });
    const int n = (int) ps.size(); if (n < 2) { printf ("no bank data\n"); return 1; }
    printf ("\n== BANK MINING: %d factory sounds (V2) ==\n", n);
    // ---- 1a. what the sounds are built from
    auto hist = [&] (const char* title, std::function<juce::String (const P&)> key)
    {
        std::map<juce::String, int> h; for (auto& p : ps) ++h[key (p)];
        std::vector<std::pair<int, juce::String>> v; for (auto& [k, c] : h) v.push_back ({ c, k }); std::sort (v.rbegin(), v.rend());
        printf ("  %s (%d distinct): ", title, (int) v.size()); for (auto& [c, k] : v) printf ("%s %d (%.0f%%)  ", k.toRawUTF8(), c, 100.0 * c / n); printf ("\n");
        return (int) v.size();
    };
    hist ("OSC 1 table / engine", [] (const P& p) { return p.t1; });
    hist ("OSC 2 table / engine", [] (const P& p) { return p.t2; });
    hist ("OSC 1 + OSC 2", [] (const P& p) { return p.t1 + " + " + p.t2; });
    hist ("filter type", [] (const P& p) { return p.filt; });
    hist ("OSC 1 unison voices", [] (const P& p) { return juce::String (p.uni); });
    hist ("matrix modulations", [] (const P& p) { return juce::String (p.mods); });
    hist ("rack units on", [] (const P& p) { return juce::String (p.rack); });
    hist ("warps on OSC 1", [] (const P& p) { return juce::String (p.warp > 0 ? "yes" : "no"); });
    // ---- 1b. timbre clusters: z-scored features (24 band shapes, log centroid, flatness, log attack, width, movement, tail; note + chord),
    // average-linkage clustering on the Euclidean distance; a "distinct" sound = its own cluster at the cut
    std::vector<std::vector<double>> X ((size_t) n);
    for (int i = 0; i < n; ++i)
        for (int k = 0; k < 2; ++k)
        {
            auto& f = ps[(size_t) i].f[k];
            for (int j = 0; j < 24; ++j) X[(size_t) i].push_back (f.band[j] / 2.0);       // (24 shape bands weigh like ~6 scalar features)
            for (double v : { std::log2 (std::max (20.0, f.centroid)), f.flat / 10.0, std::log2 (std::max (1.0, f.attackMs)), f.sideDb / 10.0, f.moveDb, f.tailDb / 20.0 }) X[(size_t) i].push_back (v);
        }
    const size_t D = X[0].size();
    for (size_t d = 0; d < D; ++d) { double m = 0, q = 0; for (auto& x : X) { m += x[d]; q += x[d] * x[d]; } m /= n; const double sd = std::sqrt (std::max (1e-9, q / n - m * m)); for (auto& x : X) x[d] = (x[d] - m) / sd; }
    for (auto& x : X) for (size_t d = 0; d < 48; ++d) x[d] *= 0.5;    // (the band features again at half weight after z-scoring)
    std::vector<std::vector<double>> dist ((size_t) n, std::vector<double> ((size_t) n, 0.0));
    for (int i = 0; i < n; ++i) for (int j = i + 1; j < n; ++j) { double s2 = 0; for (size_t d = 0; d < D; ++d) { const double e = X[(size_t) i][d] - X[(size_t) j][d]; s2 += e * e; } dist[(size_t) i][(size_t) j] = dist[(size_t) j][(size_t) i] = std::sqrt (s2 / D); }
    std::vector<double> nnd; for (int i = 0; i < n; ++i) { double best = 1e9; int bj = -1; for (int j = 0; j < n; ++j) if (j != i && dist[(size_t) i][(size_t) j] < best) { best = dist[(size_t) i][(size_t) j]; bj = j; } ps[(size_t) i].nn = best; ps[(size_t) i].nnName = ps[(size_t) bj].name; nnd.push_back (best); }
    std::vector<double> all; for (int i = 0; i < n; ++i) for (int j = i + 1; j < n; ++j) all.push_back (dist[(size_t) i][(size_t) j]);
    std::sort (all.begin(), all.end()); const double medAll = all[all.size() / 2];
    auto clusterAt = [&] (double cut, std::vector<int>* lab)
    {   // average linkage, merge while the closest pair of clusters is under the cut
        std::vector<std::vector<int>> C; for (int i = 0; i < n; ++i) C.push_back ({ i });
        while (C.size() > 1)
        {
            double best = 1e9; size_t ba = 0, bb = 0;
            for (size_t a = 0; a < C.size(); ++a) for (size_t b = a + 1; b < C.size(); ++b)
            { double s2 = 0; for (int i : C[a]) for (int j : C[b]) s2 += dist[(size_t) i][(size_t) j]; s2 /= (double) (C[a].size() * C[b].size()); if (s2 < best) { best = s2; ba = a; bb = b; } }
            if (best > cut) break;
            C[ba].insert (C[ba].end(), C[bb].begin(), C[bb].end()); C.erase (C.begin() + (long) bb);
        }
        if (lab) { lab->assign ((size_t) n, -1); for (size_t c = 0; c < C.size(); ++c) for (int i : C[c]) (*lab)[(size_t) i] = (int) c; }
        return (int) C.size();
    };
    // the cut: 0.35 x the median distance between two sounds (a pair closer than that is a variation of one sound)
    std::vector<int> lab; const double cut = 0.35 * medAll;
    const int nc = clusterAt (cut, &lab);
    for (int i = 0; i < n; ++i) ps[(size_t) i].cluster = lab[(size_t) i];
    std::map<int, int> csz; for (int c : lab) ++csz[c];
    int singles = 0; for (auto& [c, z] : csz) if (z == 1) ++singles;
    rec ("bank", "timbre clusters (distinct sounds)", fmt ("cut %.2f (0.35 x median pair distance)", cut), (double) nc, "clusters", ">= 60% of the bank", judge (-(double) nc / n, -0.6, -0.4));
    info ("bank", "sounds alone in their cluster", "", (double) singles, "sounds");
    info ("bank", "clusters at 0.5 x median", "", (double) clusterAt (0.5 * medAll, nullptr), "clusters");
    info ("bank", "clusters at 0.25 x median", "", (double) clusterAt (0.25 * medAll, nullptr), "clusters");
    { std::vector<std::pair<int, int>> big; for (auto& [c, z] : csz) big.push_back ({ z, c }); std::sort (big.rbegin(), big.rend());
      printf ("  the biggest clusters:\n"); for (size_t b = 0; b < std::min<size_t> (8, big.size()); ++b) { printf ("    #%d (%d sounds):", big[b].second, big[b].first); int shown = 0; for (auto& p : ps) if (p.cluster == big[b].second && shown++ < 14) printf (" %s;", p.name.toRawUTF8()); printf ("\n"); } }
    { std::vector<std::tuple<double, int, int>> pr; for (int i = 0; i < n; ++i) for (int j = i + 1; j < n; ++j) pr.push_back ({ dist[(size_t) i][(size_t) j], i, j }); std::sort (pr.begin(), pr.end());
      printf ("  the 40 closest pairs (near duplicates; distance re the median pair %.2f):\n", medAll);
      for (size_t k = 0; k < std::min<size_t> (40, pr.size()); ++k) printf ("    %.3f  %s  <->  %s\n", std::get<0> (pr[k]) / medAll, ps[(size_t) std::get<1> (pr[k])].name.toRawUTF8(), ps[(size_t) std::get<2> (pr[k])].name.toRawUTF8()); }
    // ---- 2. design hygiene: penalty points per sound (0 = clean); the reasons are listed
    auto catHas = [] (const P& p, std::initializer_list<const char*> w) { for (auto* s : w) if (p.cat.containsIgnoreCase (s) || p.name.containsIgnoreCase (s)) return true; return false; };
    std::vector<double> mmv; for (auto& p : ps) mmv.push_back (0.5 * (p.f[0].momMax + p.f[1].momMax)); std::sort (mmv.begin(), mmv.end()); const double mmMed = mmv[mmv.size() / 2];
    std::vector<double> cv; for (auto& p : ps) cv.push_back (std::log2 (std::max (20.0, p.f[1].centroid))); std::sort (cv.begin(), cv.end());
    const double c05 = cv[(size_t) (0.05 * n)], c95 = cv[(size_t) (0.95 * n)];
    for (auto& p : ps)
    {
        double pen = 0; juce::String why;
        auto flag = [&] (double pts, const juce::String& w) { pen += pts; why << w << "; "; };
        const auto& a = p.f[0]; const auto& c = p.f[1];
        const bool bass = catHas (p, { "bass", "808", "sub" }), pad = catHas (p, { "pad", "drone", "atmos", "swell", "choir", "string" }), pluck = catHas (p, { "pluck", "keys", "bell", "mallet", "piano", "stab", "ep", "organ", "arp" }), fx = catHas (p, { "fx", "riser", "sweep", "zap", "noise" });
        if (std::max (a.moveDb, c.moveDb) < 0.6 && std::max (a.levelMoveDb, c.levelMoveDb) < 0.6 && ! pluck) flag (2.0, "static: no movement over 3 s");
        if (! bass && c.sideDb < -25.0) flag (1.5, juce::String ("mono (side ") + juce::String (c.sideDb, 0) + " dB)");
        if (bass && a.sideDb > -8.0) flag (1.0, "wide bass");
        const double dev = 0.5 * (a.momMax + c.momMax) - mmMed; if (std::abs (dev) > 3.0) flag (std::min (2.0, (std::abs (dev) - 3.0) * 0.5 + 0.5), "level " + juce::String (dev, 1) + " LU off the bank");
        if (pluck && a.attackMs > 25) flag (1.5, "slow attack for a " + p.cat + " (" + juce::String (a.attackMs, 0) + " ms)");
        if ((bass || catHas (p, { "lead" })) && a.attackMs > 60 && ! pad) flag (1.0, "slow attack (" + juce::String (a.attackMs, 0) + " ms)");
        if (pad && a.attackMs < 8) flag (0.5, "pad with a hard attack");
        if (pad && c.tailDb < -40) flag (1.0, "pad with no release tail");
        const double lc = std::log2 (std::max (20.0, c.centroid)); if (lc < c05) flag (1.0, "very dark"); if (lc > c95) flag (1.0, "very bright");
        if (std::max (a.harsh, c.harsh) > -8.0) flag (1.5, "harsh (2-5 kHz " + juce::String (std::max (a.harsh, c.harsh), 1) + " dB of the total)");
        if (! bass && c.low > -3.0) flag (1.0, "boomy (below 120 Hz " + juce::String (c.low, 1) + " dB)");
        if (! bass && ! fx && c.low < -40.0 && c.mud < -25.0) flag (1.0, "thin");
        if (c.mud > -2.0 && ! bass) flag (1.0, "muddy (150-500 Hz " + juce::String (c.mud, 1) + " dB)");
        if (a.lufs < -60 || c.lufs < -60) flag (3.0, "nearly silent on a C3 note / chord");
        if (csz[p.cluster] > 1 && p.nn < 0.25 * medAll) flag (1.0, "near-duplicate of " + p.nnName);
        p.hygiene = pen; p.flags = why;
        // character (for keep): movement, width, uniqueness in the bank, a built sound (mods / layers / warps / engines), no hygiene flags
        p.ident = 1.5 * std::min (3.0, std::max (a.moveDb, c.moveDb)) + std::min (2.0, std::max (0.0, (c.sideDb + 20.0) / 10.0)) + 4.0 * std::min (1.0, p.nn / medAll)
                + 0.5 * (p.mods > 0) + 0.5 * (p.layers > 0) + 0.5 * (p.warp > 0) + 0.5 * (gpro::eng::engineOf (gpro::eng::tableChoiceNames().indexOf (p.t1)) >= 0) - pen;
    }
    std::vector<int> order ((size_t) n); for (int i = 0; i < n; ++i) order[(size_t) i] = i;
    std::sort (order.begin(), order.end(), [&] (int a, int b) { return ps[(size_t) a].ident > ps[(size_t) b].ident; });
    juce::String csv ("rank,name,category,keep,score,hygiene_penalty,cluster,cluster_size,nearest,nearest_distance_re_median,osc1,osc2,filter,unison,mods,layers,rack_units,move_db,width_side_db,attack_ms,tail_db,centroid_hz,harsh_db,low_db,mud_db,momentary_max_lufs,flags\n");
    for (int r = 0; r < n; ++r)
    {
        const auto& p = ps[(size_t) order[(size_t) r]];
        const bool keep = r < 100;
        csv << (r + 1) << ",\"" << p.name << "\",\"" << p.cat << "\"," << (keep ? "keep" : (p.hygiene >= 2.5 ? "hide" : "maybe")) << "," << juce::String (p.ident, 2) << "," << juce::String (p.hygiene, 1) << "," << p.cluster << "," << csz[p.cluster] << ",\"" << p.nnName << "\"," << juce::String (p.nn / medAll, 3) << ",\""
            << p.t1 << "\",\"" << p.t2 << "\",\"" << p.filt << "\"," << p.uni << "," << p.mods << "," << p.layers << "," << p.rack << "," << juce::String (std::max (p.f[0].moveDb, p.f[1].moveDb), 2) << "," << juce::String (p.f[1].sideDb, 1) << ","
            << juce::String (p.f[0].attackMs, 0) << "," << juce::String (p.f[1].tailDb, 1) << "," << juce::String (p.f[1].centroid, 0) << "," << juce::String (std::max (p.f[0].harsh, p.f[1].harsh), 1) << "," << juce::String (p.f[1].low, 1) << "," << juce::String (p.f[1].mud, 1) << ","
            << juce::String (0.5 * (p.f[0].momMax + p.f[1].momMax), 1) << ",\"" << p.flags.trimCharactersAtEnd ("; ") << "\"\n";
    }
    outCsv.replaceWithText (csv);
    int hide = 0, staticN = 0, monoN = 0, harshN = 0, dupN = 0; for (auto& p : ps) { if (p.hygiene >= 2.5) ++hide; if (p.flags.contains ("static")) ++staticN; if (p.flags.contains ("mono")) ++monoN; if (p.flags.contains ("harsh")) ++harshN; if (p.flags.contains ("near-duplicate")) ++dupN; }
    info ("bank", "hygiene: static sounds (no movement)", "", (double) staticN, "sounds"); info ("bank", "hygiene: mono non-bass sounds", "", (double) monoN, "sounds");
    info ("bank", "hygiene: harsh sounds", "", (double) harshN, "sounds"); info ("bank", "hygiene: near-duplicates", "", (double) dupN, "sounds");
    info ("bank", "hygiene: hide candidates (penalty >= 2.5)", "", (double) hide, "sounds");
    printf ("  the 100 worst by hygiene:\n   ");
    std::vector<int> worst ((size_t) n); for (int i = 0; i < n; ++i) worst[(size_t) i] = i; std::sort (worst.begin(), worst.end(), [&] (int a, int b) { return ps[(size_t) a].hygiene > ps[(size_t) b].hygiene; });
    for (int k = 0; k < std::min (100, n); ++k) printf (" %s (%.1f)%s", ps[(size_t) worst[(size_t) k]].name.toRawUTF8(), ps[(size_t) worst[(size_t) k]].hygiene, k % 6 == 5 ? "\n   " : ";");
    printf ("\n  wrote %s\n", outCsv.getFullPathName().toRawUTF8());
    return 0;
}
// the raw tone of each wavetable along POS and of each engine (C3, bare oscillator, V2 High): richness = harmonics within 40 dB of the
// loudest (up to 16 kHz), spectral entropy of the harmonic amplitudes (bits), alias <= 20 kHz, POS evolution = mean spectral distance between
// neighbouring POS steps (dB per harmonic)
static int auditTables (const juce::File& dir)
{
    const double sr = 48000.0; const int note = 48; const double f0 = midiHz (note);
    const auto& tn = gpro::eng::tableChoiceNames();
    juce::String csv ("source,pos,rich_harmonics,entropy_bits,alias_20k_db,centroid_hz,pos_evolution_db\n");
    printf ("\n%-18s %4s %6s %7s %8s %9s\n", "table / engine", "pos", "rich", "entropy", "alias20", "centroid");
    auto measure = [&] (int tbl, float pos, std::vector<double>* hl)
    {
        auto p = oscPatch (sr, tbl, pos);
        if (gpro::eng::engineOf (tbl) >= 0 && gpro::eng::usesInput (gpro::eng::engineOf (tbl))) { setp (*p, { { "o2_on", 1.0f }, { "o2_tbl", (float) gpro::tSaw }, { "o2_lvl", 0.0f } }); settle (*p); }
        auto out = renderAt (*p, { { 0.0, note, 0.9f, true } }, 1.7, sr);
        const auto x = monoOf (out);
        const auto a = harm (x, sr, f0, (size_t) (0.3 * sr), 0, 0.0, 16);
        auto ps = powSpec (x, (size_t) (0.3 * sr), 65536); const double binW = sr / 65536.0;
        std::vector<double> h; for (int k = 1; k * f0 < 16000.0; ++k) { double e = 0; const int c = (int) std::round (k * f0 / binW); for (int j = c - 8; j <= c + 8; ++j) e += ps[(size_t) j]; h.push_back (e); }
        double mx = 0, sum = 0; for (auto e : h) { mx = std::max (mx, e); sum += e; }
        int rich = 0; double ent = 0, num = 0; for (size_t k = 0; k < h.size(); ++k) { if (dbp (h[k] / mx) > -40) ++rich; const double q = h[k] / std::max (1e-30, sum); if (q > 0) ent -= q * std::log2 (q); num += h[k] * (k + 1) * f0; }
        if (hl) { hl->clear(); for (auto e : h) hl->push_back (std::max (-80.0, dbp (e / mx))); }
        return std::make_tuple (rich, ent, a.alias20Db, num / std::max (1e-30, sum));
    };
    for (int t = 0; t < tn.size(); ++t)
    {
        if (t == gpro::tSample || t == gpro::tUser) continue;
        const bool engine = gpro::eng::engineOf (t) >= 0;
        std::vector<double> prev, cur; double evo = 0; int steps = 0;
        for (float pos : engine ? std::vector<float> { 0.0f } : std::vector<float> { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            auto [rich, ent, al, cen] = measure (t, pos, &cur);
            if (! prev.empty()) { double d = 0; const size_t m = std::min (prev.size(), cur.size()); for (size_t k = 0; k < m; ++k) d += std::abs (prev[k] - cur[k]); evo += d / std::max<size_t> (1, m); ++steps; }
            prev = cur;
            printf ("%-18s %4.2f %6d %7.2f %8.1f %9.0f\n", tn[t].toRawUTF8(), pos, rich, ent, al, cen);
            csv << "\"" << tn[t] << "\"," << juce::String (pos, 2) << "," << rich << "," << juce::String (ent, 2) << "," << juce::String (al, 1) << "," << juce::String (cen, 0) << ",\n";
        }
        if (steps > 0) { printf ("%-18s POS evolution %.1f dB / step\n", tn[t].toRawUTF8(), evo / steps); csv << "\"" << tn[t] << "\",evolution,,,,," << juce::String (evo / steps, 2) << "\n"; }
    }
    dir.getChildFile ("tables.csv").replaceWithText (csv);
    return 0;
}

// a spectrogram PNG (8192-point FFT, 75 % overlap, log frequency 30 Hz .. Nyquist, -140 .. 0 dB)
static void spectrogram (const juce::AudioBuffer<float>& b, double sr, const juce::File& png, const juce::String& title)
{
    const int W = 900, H = 420, N = 8192, hop = 1024;
    juce::Image img (juce::Image::RGB, W, H + 24, true);
    juce::Graphics g (img); g.fillAll (juce::Colours::black);
    const auto x = monoOf (b);
    const int frames = std::max (1, ((int) x.size() - N) / hop);
    std::vector<std::vector<double>> sp;
    double mx = 1e-30;
    for (int f = 0; f < frames; ++f) { sp.push_back (powSpec (x, (size_t) (f * hop), (size_t) N)); for (auto v : sp.back()) mx = std::max (mx, v); }
    for (int px = 0; px < W; ++px)
    {
        const int f = std::min (frames - 1, px * frames / W);
        for (int py = 0; py < H; ++py)
        {
            const double hz = 30.0 * std::pow ((sr * 0.5) / 30.0, (double) (H - 1 - py) / (H - 1));
            const size_t k = std::min (sp[(size_t) f].size() - 1, (size_t) std::lround (hz / sr * N));
            const double d = juce::jlimit (0.0, 1.0, (dbp (sp[(size_t) f][k] / mx) + 140.0) / 140.0);
            const float hue = (float) (0.7 - 0.7 * d), bri = (float) std::pow (d, 0.8);
            img.setPixelAt (px, py + 24, juce::Colour::fromHSV (hue, 0.9f, bri, 1.0f));
        }
    }
    g.setColour (juce::Colours::white); g.setFont (14.0f);
    g.drawText (title + "   (log freq 30 Hz .. " + juce::String (sr / 2000.0, 0) + " kHz, colour -140 .. 0 dB)", 6, 2, W - 12, 20, juce::Justification::left);
    for (double hz : { 100.0, 1000.0, 10000.0, 20000.0 })
    {
        const int py = 24 + (int) ((H - 1) * (1.0 - std::log (hz / 30.0) / std::log (sr * 0.5 / 30.0)));
        g.setColour (juce::Colours::white.withAlpha (0.5f)); g.drawHorizontalLine (py, 0, 40);
        g.drawText (hz >= 1000 ? juce::String (hz / 1000, 0) + "k" : juce::String (hz, 0), 42, py - 8, 50, 16, juce::Justification::left);
    }
    png.deleteFile();
    juce::FileOutputStream os (png); juce::PNGImageFormat fmtPng; fmtPng.writeImageToStream (img, os);
}
static void writeWav24 (const juce::AudioBuffer<float>& b, double sr, const juce::File& f)
{
    f.deleteFile(); juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> os (f.createOutputStream());
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, 2, 24, {}, 0));
    if (w) { os.release(); w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples()); }
}
static int auditMatch (const juce::File& before, const juce::File& after, const juce::File& out, const juce::File& auditCsvBefore, const juce::File& auditCsvAfter)
{
    const double sr = 48000.0, target = -16.0;
    for (auto* s : { "before", "after" }) out.getChildFile (s).createDirectory();
    juce::String html;
    html << "<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>Ghosty Pro sound audit</title>"
         << "<style>body{font-family:system-ui,sans-serif;margin:16px;max-width:1100px;background:#fff;color:#111}table{border-collapse:collapse;width:100%}td,th{border-bottom:1px solid #ddd;padding:4px 6px;text-align:left;font-size:14px}"
         << "audio{width:240px;height:32px}img{max-width:100%}code{background:#f3f3f3;padding:1px 4px}@media (prefers-color-scheme:dark){body{background:#111;color:#eee}td,th{border-color:#333}code{background:#222}}</style></head><body>"
         << "<h1>Ghosty Pro sound audit: before / after</h1><p>Before = origin/main (85648d5). After = branch pro-audit. Every pair is loudness-matched so the ear judges the sound, not the level: "
         << "each file is brought to " << target << " LUFS (integrated), then both files of the pair are scaled down by the same amount if either would pass -1 dBTP. The raw loudness of each render is listed "
         << "(the after column includes the new per-sound loudness normalisation, which is a level change by design). 24-bit / 48 kHz WAV, velocity 100, 3 s.</p>";
    // ArpEZator (the owner's reference): its preview synth on the same notes for the paired sounds, with its FX and raw (auditarp)
    const juce::File arpDir = before.getSiblingFile ("raw-arp"), arpDry = before.getSiblingFile ("raw-arp-dry");
    std::map<juce::String, juce::String> arpOf;
    { juce::StringArray pl; arpDir.getChildFile ("pairs.txt").readLines (pl); for (auto& l : pl) if (l.containsChar ('|')) arpOf[l.upToFirstOccurrenceOf ("|", false, false)] = l.fromFirstOccurrenceOf ("|", false, false); }
    if (! arpOf.empty()) { out.getChildFile ("arp").createDirectory(); out.getChildFile ("arp-raw").createDirectory(); }
    html << "<p>ArpEZator columns: its preview synth's matching preset on the same notes, with its own FX chain (delay off, its reverb at the default 30 %) and RAW (the voice alone: no reverb / delay / output clip), loudness-matched the same way.</p>";
    html << "<table><tr><th>file</th><th>Pro before</th><th>Pro after</th><th>ArpEZator</th><th>ArpEZator raw</th><th>LUFS before / after / arp (raw)</th></tr>";
    juce::StringArray names;
    for (auto* nm : kOwnerSounds) for (auto& t : ownerTakes()) names.add (safeName (nm) + "_" + t.tag);
    names.add ("spec_saw_C6"); names.add ("spec_saw_C4maj");
    auto put = [&] (juce::AudioBuffer<float>& x, double gain, const juce::String& rel) { x.applyGain ((float) gain); writeWav24 (x, sr, out.getChildFile (rel)); };
    for (auto& nm : names)
    {
        auto b = readRaw (before.getChildFile (nm + ".f32")), a = readRaw (after.getChildFile (nm + ".f32"));
        if (b.getNumSamples() == 0 || a.getNumSamples() == 0) continue;
        auto r = readRaw (arpDir.getChildFile (nm + ".f32")), rd = readRaw (arpDry.getChildFile (nm + ".f32"));
        const bool haveArp = r.getNumSamples() > 0 && rd.getNumSamples() > 0;
        const double lb = lufs (b), la = lufs (a), lr = haveArp ? lufs (r) : -100, lrd = haveArp ? lufs (rd) : -100;
        double gb = juce::Decibels::decibelsToGain (target - lb), ga = juce::Decibels::decibelsToGain (target - la), gr = juce::Decibels::decibelsToGain (target - lr), grd = juce::Decibels::decibelsToGain (target - lrd);
        double over = std::max (cleantest::truePeakDb (b) + dba (gb), cleantest::truePeakDb (a) + dba (ga));
        if (haveArp) over = std::max (over, std::max (cleantest::truePeakDb (r) + dba (gr), cleantest::truePeakDb (rd) + dba (grd)));
        over += 1.0;
        const double down = over > 0 ? juce::Decibels::decibelsToGain (-over) : 1.0;
        put (b, gb * down, "before/" + nm + ".wav"); put (a, ga * down, "after/" + nm + ".wav");
        if (haveArp) { put (r, gr * down, "arp/" + nm + ".wav"); put (rd, grd * down, "arp-raw/" + nm + ".wav"); }
        auto au = [] (const juce::String& src) { return "<audio controls preload=\"none\" src=\"" + src + "\"></audio>"; };
        html << "<tr><td><code>" << nm << "</code>" << (haveArp ? "<br><small>ArpEZator: " + arpOf[nm.upToLastOccurrenceOf ("_", false, false)] + "</small>" : juce::String()) << "</td><td>" << au ("before/" + nm + ".wav") << "</td><td>" << au ("after/" + nm + ".wav") << "</td><td>"
             << (haveArp ? au ("arp/" + nm + ".wav") : juce::String ("-")) << "</td><td>" << (haveArp ? au ("arp-raw/" + nm + ".wav") : juce::String ("-")) << "</td><td>"
             << juce::String (lb, 1) << " / " << juce::String (la, 1) << (haveArp ? " / " + juce::String (lr, 1) : juce::String()) << "</td></tr>";
    }
    html << "</table><h2>Spectrograms (the alias floor)</h2>";
    for (auto* s : { "spec_saw_C6", "spec_saw_C4maj" })
        for (auto* side : { "before", "after" })
        {
            auto b = readRaw ((juce::String (side) == "before" ? before : after).getChildFile (juce::String (s) + ".f32"));
            if (b.getNumSamples() == 0) continue;
            const auto png = out.getChildFile (juce::String (side) + "/" + s + ".png");
            spectrogram (b, sr, png, juce::String (side) + ": " + (juce::String (s) == "spec_saw_C6" ? "bare saw C6" : "INIT saw C4 major chord"));
            html << "<p><b>" << side << "</b> " << s << "<br><img src=\"" << side << "/" << s << ".png\" alt=\"" << side << " " << s << " spectrogram\"></p>";
        }
    // key numbers: the audit rows that changed most between the two CSVs
    if (auditCsvBefore.existsAsFile() && auditCsvAfter.existsAsFile())
    {
        juce::StringArray lb, la; auditCsvBefore.readLines (lb); auditCsvAfter.readLines (la);
        std::map<juce::String, juce::String> mb; for (auto& l : lb) { auto t = juce::StringArray::fromTokens (l, ",", "\""); if (t.size() >= 7) mb[t[0] + "|" + t[1] + "|" + t[2]] = l; }
        html << "<h2>Key numbers (audit rows whose verdict or value changed)</h2><table><tr><th>section</th><th>test</th><th>condition</th><th>before</th><th>after</th><th>unit</th><th>target</th><th>verdict before / after</th></tr>";
        for (auto& l : la)
        {
            auto t = juce::StringArray::fromTokens (l, ",", "\""); if (t.size() < 7 || t[0] == "section") continue;
            auto it = mb.find (t[0] + "|" + t[1] + "|" + t[2]); if (it == mb.end()) continue;
            auto u = juce::StringArray::fromTokens (it->second, ",", "\"");
            const double vb = u[3].getDoubleValue(), va = t[3].getDoubleValue();
            if (u[6] == t[6] && std::abs (vb - va) < 0.5) continue;
            html << "<tr><td>" << t[0] << "</td><td>" << t[1].unquoted() << "</td><td>" << t[2].unquoted() << "</td><td>" << juce::String (vb, 2) << "</td><td>" << juce::String (va, 2) << "</td><td>" << t[4] << "</td><td>" << t[5].unquoted() << "</td><td>" << u[6] << " / " << t[6] << "</td></tr>";
        }
        html << "</table>";
    }
    html << "</body></html>";
    out.getChildFile ("index.html").replaceWithText (html);
    printf ("wrote %s\n", out.getChildFile ("index.html").getFullPathName().toRawUTF8());
    return 0;
}
} // namespace saudit
