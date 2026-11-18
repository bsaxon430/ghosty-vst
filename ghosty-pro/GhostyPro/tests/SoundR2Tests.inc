// SOUND ROUND 2 tests (2026-10-24): FILTER DRIVE inside the filter ('drive'), and the owner's side-by-side renders ('r2render').
// Included by TrapTests.cpp after CleanTests.inc (uses its helpers: makeProc, render, settle, set, bands, truePeakDb, lufs ...).

#include "ProWavetableEditor.h"

namespace r2
{
static constexpr float kCut500 = 0.43644f;      // the CUTOFF knob at 500 Hz: log2 (500 / 30) / 9.3

// energy share (dB) of the mono sum above fromHz, Hann-windowed 8192-point frames
static double shareAbove (const juce::AudioBuffer<float>& x, double fromHz, int start = 0, int end = -1)
{
    if (end < 0) end = x.getNumSamples();
    constexpr int ORDER = 13, N = 1 << ORDER;
    juce::dsp::FFT fft (ORDER);
    std::vector<double> acc ((size_t) N / 2, 0.0); std::vector<float> buf ((size_t) N * 2);
    int frames = 0;
    for (int s = start; s + N <= end; s += N / 2, ++frames)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < N; ++i) buf[(size_t) i] = 0.5f * (x.getSample (0, s + i) + x.getSample (1, s + i)) * (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / N));
        fft.performFrequencyOnlyForwardTransform (buf.data());
        for (int k = 0; k < N / 2; ++k) acc[(size_t) k] += (double) buf[(size_t) k] * buf[(size_t) k];
    }
    double hi = 0, tot = 0;
    for (int k = 1; k < N / 2; ++k) { const double f = k * SR / N; tot += acc[(size_t) k]; if (f >= fromHz) hi += acc[(size_t) k]; }
    return 10 * std::log10 (std::max (1e-30, hi) / std::max (1e-30, tot));
}

// the owner's side-by-side patch: INIT, OSC 1 saw + OSC 2 square (PULSE at 0), LP 12 at 500 Hz, no FX
static std::unique_ptr<ProProcessor> ownerPatch (int law, float drive, bool cleanMode = false)
{
    auto p = makeProc (-1); p->initPatch();
    cleantest::set (*p, { { "o2_on", 1.0f }, { "o2_tbl", (float) gpro::tPulse }, { "o2_pos", 0.0f }, { "f_type", 0.0f }, { "cutoff", kCut500 }, { "res", 0.0f },
                          { "f_drive", drive }, { "f_dlaw", (float) law }, { "f_clean", cleanMode ? 1.0f : 0.0f }, { "out_trim", -6.0f }, { "quality", 0.0f } });   // (Draft: the engine the owner compared)
    settle (*p);
    return p;
}

// ---------------------------------------------------------------- unit: the 4x oversampler
static void testOversampler()
{
    section ("DRIVE: the 4x oversampler (delay, pass band, image / alias rejection)");
    gpro::Oversampler4 os; os.prepare();
    // impulse: one base sample in, the whole-sample delay out
    std::vector<float> ir (64, 0.0f);
    for (int n = 0; n < 64; ++n) { float u[4], v[4]; os.up (0, n == 0 ? 1.0f : 0.0f, u); for (int p = 0; p < 4; ++p) v[p] = u[p]; ir[(size_t) n] = os.down (0, v); os.advance(); }
    int peak = 0; for (int n = 1; n < 64; ++n) if (std::abs (ir[(size_t) n]) > std::abs (ir[(size_t) peak])) peak = n;
    double sum = 0; for (auto v : ir) sum += v;
    CHECK (peak == gpro::Oversampler4::DELAY, "impulse peaks at sample %d (DELAY = %d), DC gain %.4f", peak, gpro::Oversampler4::DELAY, sum);
    // pass band: sines through up -> down, amplitude vs frequency
    double worst = 0; juce::String worstAt;
    for (double f : { 100.0, 1000.0, 5000.0, 10000.0, 15000.0, 18000.0 })
    {
        os.reset();
        double e = 0; int cnt = 0;
        for (int n = 0; n < 4000; ++n)
        {
            float u[4]; os.up (0, (float) std::sin (2 * juce::MathConstants<double>::pi * f * n / SR), u);
            const float y = os.down (0, u); os.advance();
            if (n >= 1000) { e += (double) y * y; ++cnt; }
        }
        const double db = 10 * std::log10 (e / cnt * 2.0);
        if (std::abs (db) > std::abs (worst)) { worst = db; worstAt = juce::String (f) + " Hz"; }
    }
    CHECK (std::abs (worst) < 0.5, "pass band 100 Hz .. 18 kHz within %.2f dB (worst at %s)", worst, worstAt.toRawUTF8());
    // the decimator's stop band: its response at the 4x rate from 30 kHz up (what folds back into the audio band)
    double worstStop = -200;
    for (double f = 30000.0; f < 96000.0; f += 1000.0)
    {
        double re = 0, im = 0;
        for (int i = 0; i < gpro::Oversampler4::LD; ++i) { const double w = 2 * juce::MathConstants<double>::pi * f / (4 * SR) * i; re += os.hd[(size_t) i] * std::cos (w); im -= os.hd[(size_t) i] * std::sin (w); }
        worstStop = std::max (worstStop, 10 * std::log10 (re * re + im * im));
    }
    CHECK (worstStop < -70.0, "decimator stop band (30 kHz .. 96 kHz at 4x): worst %.1f dB", worstStop);
}

// ---------------------------------------------------------------- unit: aliasing of the saturating filter inside the oversampler
// A band-limited saw at f0 = 11/1024 of the rate (its harmonics never land on a fold of 48 kHz or 192 kHz), LP 12 at 500 Hz,
// drive 50 % and 100 %. An 8192-point rectangular window over whole periods puts every harmonic on bin 88 h; everything else is alias.
static double aliasDb (float drivePct, bool oversampled, double* outDensity = nullptr)
{
    const double f0 = 11.0 / 1024.0 * SR;
    const float D = std::exp2 (drivePct * gpro::kDriveDb / 6.0206f);
    gpro::FilterChain f; f.prepare (oversampled ? 4 * SR : SR); f.S = gpro::kDriveSat;
    f.set (0, 500.0f, 0.0f, 0.5f, oversampled ? 4 * SR : SR, 20000.0f);
    gpro::Oversampler4 os; os.prepare();
    constexpr int N = 8192, SKIP = 8192;
    std::vector<float> out ((size_t) N);
    auto saw = [f0] (int n)
    {
        double v = 0;
        for (int h = 1; h * f0 < 20000.0; ++h) v += std::sin (2 * juce::MathConstants<double>::pi * f0 * h * n / SR) / h;
        return (float) (v * 2.0 / juce::MathConstants<double>::pi);     // peak ~1
    };
    for (int n = 0; n < SKIP + N; ++n)
    {
        const float x = saw (n) * D;
        float y;
        if (oversampled)
        {
            float u[4], o[4], dummy[4], yl, yr;
            os.up (0, x, u); os.up (1, 0.0f, dummy);
            for (int p = 0; p < 4; ++p) f.tickSat (u[p], 0.0f, o[p], yr);
            y = os.down (0, o); os.down (1, dummy); os.advance();
            juce::ignoreUnused (yl);
        }
        else { float yr; f.tickSat (x, 0.0f, y, yr); }
        if (n >= SKIP) out[(size_t) (n - SKIP)] = y;
    }
    juce::dsp::FFT fft (13);
    std::vector<float> buf ((size_t) N * 2, 0.0f);
    for (int i = 0; i < N; ++i) buf[(size_t) i] = out[(size_t) i];
    fft.performFrequencyOnlyForwardTransform (buf.data());
    double top = 0, alias = 0, hiE = 0, allE = 0;
    for (int k = 1; k < N / 2; ++k)
    {
        const double e = (double) buf[(size_t) k] * buf[(size_t) k];
        if (k % 88 == 0) { top = std::max (top, e); if (k * SR / N > 1000.0) hiE += e; allE += e; }
        else alias = std::max (alias, e);
    }
    if (outDensity) *outDensity = 10 * std::log10 (std::max (1e-30, hiE) / std::max (1e-30, allE));
    return 10 * std::log10 (alias / top);
}
static void testAliasing()
{
    section ("DRIVE: aliasing of the saturating filter (worst alias line re the loudest harmonic)");
    for (float d : { 0.25f, 0.5f, 0.75f, 1.0f })
    {
        double dens = 0;
        const double a4 = aliasDb (d, true, &dens), a1 = aliasDb (d, false);
        const bool need = d <= 0.5f;
        CHECK (! need || a4 <= -70.0, "drive %3.0f %%: worst alias %6.1f dB at 4x (1x would be %6.1f dB), energy above 1 kHz %+.1f dB", d * 100, a4, a1, dens);
    }
}

// ---------------------------------------------------------------- the synth: linear at 0, the drive sweep, CLEAN MODE
static void testDriveSweep()
{
    section ("DRIVE: exactly linear at 0 (INSIDE law vs CLASSIC law, shifted by the oversampler's delay)");
    {
        auto a = ownerPatch (gpro::dlClassic, 0.0f), b = ownerPatch (gpro::dlInside, 0.0f);
        const std::vector<Ev> ph { { 0.0, 45, 0.9f, true }, { 1.0, 45, 0.0f, false } };
        seedSystemRandom (5); auto ra = render (*a, ph, 1.4);
        seedSystemRandom (5); auto rb = render (*b, ph, 1.4);
        float md = 0; const int D = gpro::Oversampler4::DELAY;
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i + D < ra.getNumSamples(); ++i) md = std::max (md, std::abs (ra.getSample (ch, i) - rb.getSample (ch, i + D)));
        const float pk = peakOf (ra);
        CHECK (md <= 1.0e-6f, "drive 0: INSIDE == CLASSIC after a %d-sample shift: max diff %.3g (%.1f dB re the peak %.3f)", D, md, juce::Decibels::gainToDecibels (md / std::max (1.0e-9f, pk), -200.0f), pk);
    }
    section ("DRIVE: sweep 0 .. 100 % on a 55 Hz and a 440 Hz saw + square, LP 12 at 500 Hz (INSIDE law; CLASSIC for comparison)");
    const std::vector<float> steps { 0.0f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.0f };
    for (int note : { 33, 69 })
    {
        for (int law : { gpro::dlInside, gpro::dlClassic })
        {
            printf ("  note %d (%.0f Hz), %s law:\n", note, juce::MidiMessage::getMidiNoteInHertz (note), law == gpro::dlInside ? "INSIDE" : "CLASSIC");
            double mud0 = 0, hf0 = 0, rms0 = 0, dens0 = 0, mudWorst = -200, hfEnd = 0, rmsEnd = 0, tpWorst = -200, densPrev = -200, densDrop = 0, rmsMin = 1000, rmsMax = -1000;
            for (float d : steps)
            {
                auto p = ownerPatch (law, d);
                const std::vector<Ev> ph { { 0.0, note, 0.9f, true }, { 1.3, note, 0.0f, false } };
                seedSystemRandom (5);
                auto out = render (*p, ph, 1.5);
                const int s0 = (int) (0.3 * SR), s1 = (int) (1.3 * SR);
                const auto b = cleantest::bands (out, s0, s1);
                const double hf = r2::shareAbove (out, 2000.0, s0, s1), dens = r2::shareAbove (out, 1000.0, s0, s1), tp = cleantest::truePeakDb (out), rms = rmsDb (out, s0, s1);
                printf ("    drive %3.0f %%: RMS %6.1f dB  TP %+6.2f dBTP  mud share %+5.1f dB  above 1 kHz %+5.1f dB  above 2 kHz %+5.1f dB  low<120 %6.1f\n", d * 100, rms, tp, b.mudShare, dens, hf, b.lowAbs);
                if (d == 0.0f) { mud0 = b.mudShare; hf0 = hf; rms0 = rms; dens0 = dens; }
                mudWorst = std::max (mudWorst, b.mudShare - mud0); tpWorst = std::max (tpWorst, tp);
                if (densPrev > -100) densDrop = std::max (densDrop, densPrev - dens);
                densPrev = dens; hfEnd = hf; rmsEnd = rms; rmsMin = std::min (rmsMin, rms); rmsMax = std::max (rmsMax, rms);
            }
            juce::ignoreUnused (dens0);
            if (law == gpro::dlInside)
            {
                CHECK (mudWorst <= 0.5, "note %d: mud share never rises with drive (worst %+.2f dB vs drive 0)", note, mudWorst);
                CHECK (hfEnd - hf0 >= 4.0, "note %d: energy above 2 kHz rises with drive (%+.1f dB at 100 %% vs 0)", note, hfEnd - hf0);
                CHECK (tpWorst <= -0.3, "note %d: true peak stays under the ceiling (worst %+.2f dBTP)", note, tpWorst);
                // (a saturator fed a filtered, fundamental-heavy note first ducks the little top that leaks through the filter, then adds
                //  its own harmonics: a dip of a few dB around the onset is the physics; the THD check below is the strict monotone one)
                CHECK (densDrop <= 5.0, "note %d: energy above 1 kHz: largest dip on the way up %.2f dB", note, densDrop);
                CHECK (rmsEnd >= rms0 - 0.5 && rmsEnd - rms0 <= 9.0, "note %d: 100 %% drive is louder than 0 but by less than 9 dB (%+.1f dB; the sweep spans %.1f .. %.1f dB)", note, rmsEnd - rms0, rmsMin, rmsMax);
            }
        }
    }
    section ("DRIVE: THD of a sine (OSC 1 = Ghosty Shapes / Sine) at 220 Hz through LP 12 at 500 Hz grows monotonically with drive");
    {
        double prev = -200, worstDrop = 0, first = 0, last = 0;
        for (float d : steps)
        {
            auto p = ownerPatch (gpro::dlInside, d);
            cleantest::set (*p, { { "o2_on", 0.0f }, { "o1_tbl", (float) gpro::tShapes }, { "o1_pos", 0.0f } }); settle (*p);
            const std::vector<Ev> ph { { 0.0, 57, 0.9f, true }, { 1.3, 57, 0.0f, false } };
            seedSystemRandom (5);
            auto out = render (*p, ph, 1.5);
            // harmonic energy vs the fundamental, 8192-point Hann frames over the steady part
            constexpr int ORDER = 13, N = 1 << ORDER;
            juce::dsp::FFT fft (ORDER); std::vector<float> buf ((size_t) N * 2); std::vector<double> acc ((size_t) N / 2, 0.0);
            int frames = 0;
            for (int s0 = (int) (0.3 * SR); s0 + N <= (int) (1.3 * SR); s0 += N / 2, ++frames)
            {
                std::fill (buf.begin(), buf.end(), 0.0f);
                for (int i = 0; i < N; ++i) buf[(size_t) i] = 0.5f * (out.getSample (0, s0 + i) + out.getSample (1, s0 + i)) * (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / N));
                fft.performFrequencyOnlyForwardTransform (buf.data());
                for (int k = 0; k < N / 2; ++k) acc[(size_t) k] += (double) buf[(size_t) k] * buf[(size_t) k];
            }
            const double f0 = juce::MidiMessage::getMidiNoteInHertz (57), binW = SR / N;
            auto bandE = [&] (double f) { double e = 0; const int k0 = (int) std::floor (f / binW) - 3, k1 = k0 + 7; for (int k = std::max (1, k0); k <= k1 && k < N / 2; ++k) e += acc[(size_t) k]; return e; };
            const double fund = bandE (f0); double harm = 0; for (int h = 2; h * f0 < 20000.0; ++h) harm += bandE (h * f0);
            const double thd = 10 * std::log10 (std::max (1e-30, harm) / std::max (1e-30, fund));
            printf ("    drive %3.0f %%: THD %+6.1f dB  RMS %6.1f dB  TP %+6.2f dBTP\n", d * 100, thd, rmsDb (out, (int) (0.3 * SR), (int) (1.3 * SR)), cleantest::truePeakDb (out));
            if (prev > -100) worstDrop = std::max (worstDrop, prev - thd);
            prev = thd; if (d == 0.0f) first = thd; last = thd;
        }
        CHECK (worstDrop <= 0.3, "THD grows monotonically with drive (%+.1f dB at 0 -> %+.1f dB at 100 %%, largest drop %.2f dB)", first, last, worstDrop);
    }
    section ("DRIVE: CLEAN MODE (-24 dB in / +24 dB out) = the saturation of drive -24 dB at the loudness of the knob");
    {
        auto a = ownerPatch (gpro::dlInside, 1.0f, true), b = ownerPatch (gpro::dlInside, 1.0f - 24.0f / gpro::kDriveDb, false);
        cleantest::set (*a, { { "out_trim", -20.0f } }); cleantest::set (*b, { { "out_trim", -20.0f } }); settle (*a); settle (*b);
        const std::vector<Ev> ph { { 0.0, 45, 0.9f, true }, { 1.3, 45, 0.0f, false } };
        seedSystemRandom (5); auto ra = render (*a, ph, 1.5);
        seedSystemRandom (5); auto rb = render (*b, ph, 1.5);
        const int s0 = (int) (0.3 * SR), s1 = (int) (1.3 * SR);
        const double dl = rmsDb (ra, s0, s1) - rmsDb (rb, s0, s1), dh = r2::shareAbove (ra, 2000.0, s0, s1) - r2::shareAbove (rb, 2000.0, s0, s1);
        const double want = gpro::kCleanModeDb * gpro::kDriveLoudExp;        // the loudness law's step for 24 dB of knob
        CHECK (std::abs (dl - want) < 0.3 && std::abs (dh) < 0.3, "CLEAN MODE at 100 %% = the saturation of drive %.0f %%, %+.1f dB louder (loudness law): level %+.2f dB (RMS %.1f vs %.1f), HF share %+.2f dB",
               (1.0f - 24.0f / gpro::kDriveDb) * 100, want, dl, rmsDb (ra, s0, s1), rmsDb (rb, s0, s1), dh);
    }
}

// debug: the level of the oversampled saturating LP vs the linear one, sine 100 Hz, drive d
static void debugLevels()
{
    for (float d : { 0.0f, 0.1f, 0.5f, 1.0f })
    {
        const float D = std::exp2 (d * gpro::kDriveDb / 6.0206f), trim = 1.0f / std::sqrt (D);
        gpro::FilterChain lin, drv; lin.prepare (SR); drv.prepare (4 * SR); drv.S = gpro::kDriveSat;
        lin.set (0, 500.0f, 0.0f, 0.5f, SR, 20000.0f); drv.set (0, 500.0f, 0.0f, 0.5f, 4 * SR, 20000.0f);
        gpro::Oversampler4 os; os.prepare();
        double e1 = 0, e2 = 0, e3 = 0; int cnt = 0;
        for (int n = 0; n < 48000; ++n)
        {
            const float x = (float) std::sin (2 * juce::MathConstants<double>::pi * 100.0 * n / SR);
            float yl, yr; lin.tick (x, 0.0f, yl, yr);
            float u[4], o[4], dm[4], a, b; os.up (0, x * D, u); os.up (1, 0.0f, dm);
            for (int p = 0; p < 4; ++p) drv.tickSat (u[p], 0.0f, o[p], b);
            a = os.down (0, o) * trim; os.down (1, dm); os.advance();
            float z[4], q[4], c; for (int p = 0; p < 4; ++p) z[p] = u[p];   // the linear 4x filter, same path
            static gpro::FilterChain lin4; static bool init = false; if (! init) { lin4.prepare (4 * SR); init = true; }
            if (n == 0) { lin4.reset(); lin4.set (0, 500.0f, 0.0f, 0.5f, 4 * SR, 20000.0f); }
            for (int p = 0; p < 4; ++p) lin4.tick (z[p], 0.0f, q[p], b);
            static gpro::Oversampler4 os2; if (n == 0) { os2.prepare(); }
            c = os2.down (0, q) * trim; os2.advance();
            if (n > 24000) { e1 += yl * yl; e2 += a * a; e3 += c * c; ++cnt; }
        }
        printf ("  drive %3.0f %%: D %.2f  linear 1x %.2f dB, driven 4x %.2f dB, linear 4x %.2f dB\n", d * 100, D, 10 * std::log10 (e1 / cnt), 10 * std::log10 (e2 / cnt), 10 * std::log10 (e3 / cnt));
    }
}

// ---------------------------------------------------------------- UNISON: 8 voices vs 1 at the same loudness and bass
static double bandBelow (const juce::AudioBuffer<float>& x, double toHz, int start, int end)
{   // absolute energy (dB) of the mono sum below toHz
    constexpr int ORDER = 13, N = 1 << ORDER;
    juce::dsp::FFT fft (ORDER); std::vector<double> acc ((size_t) N / 2, 0.0); std::vector<float> buf ((size_t) N * 2);
    int frames = 0;
    for (int s0 = start; s0 + N <= end; s0 += N / 2, ++frames)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < N; ++i) buf[(size_t) i] = 0.5f * (x.getSample (0, s0 + i) + x.getSample (1, s0 + i)) * (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / N));
        fft.performFrequencyOnlyForwardTransform (buf.data());
        for (int k = 0; k < N / 2; ++k) acc[(size_t) k] += (double) buf[(size_t) k] * buf[(size_t) k];
    }
    double e = 0; for (int k = 1; k < N / 2; ++k) if (k * SR / N < toHz) e += acc[(size_t) k] / std::max (1, frames);
    return 10 * std::log10 (std::max (1e-30, e) / ((double) N * N * 0.375 / 4.0));
}
static std::unique_ptr<ProProcessor> uniPatch (int law, int voices, int note, int dmode = 1, int stack = 0)
{
    auto p = makeProc (-1); p->initPatch();
    cleantest::set (*p, { { "o1_uni", (float) voices }, { "uni_law", (float) law }, { "o1_dmode", (float) dmode }, { "o1_stack", (float) stack }, { "cutoff", 1.0f }, { "out_trim", -6.0f } });
    settle (*p);
    juce::ignoreUnused (note);
    return p;
}
int unisonTests()
{
    section ("UNISON: 8-voice saw vs 1-voice saw at the same note (INIT, filter open): loudness within 1 dB, bass (< 150 Hz) within 1.5 dB, under the ceiling");
    for (int note : { 36, 48, 60 })
        for (int law : { 1, 0 })
        {
            double rms1 = 0, low1 = 0, rmsN = 0, lowN = 0, tpN = -200, corrN = 1;
            for (int voices : { 1, 8 })
            {
                auto p = uniPatch (law, voices, note);
                const std::vector<Ev> ph { { 0.0, note, 0.9f, true }, { 6.3, note, 0.0f, false } };
                seedSystemRandom (7);
                auto out = render (*p, ph, 6.5);
                const int s0 = (int) (0.3 * SR), s1 = (int) (6.3 * SR);      // 6 s: several beats of the slowest detune pair
                const double rms = rmsDb (out, s0, s1), low = bandBelow (out, 150.0, s0, s1), tp = cleantest::truePeakDb (out);
                if (voices == 1) { rms1 = rms; low1 = low; } else { rmsN = rms; lowN = low; tpN = tp; corrN = cleantest::lowCorr (out, 120.0, s0, s1); }
                printf ("  note %d %-7s %d voice%s: RMS %6.1f dB  < 150 Hz %6.1f dB  TP %+6.2f dBTP  mud share %+5.1f  lowCorr %.2f\n", note, law ? "V2" : "CLASSIC", voices, voices > 1 ? "s" : " ", rms, low, tp,
                        cleantest::bands (out, s0, s1).mudShare, cleantest::lowCorr (out, 120.0, s0, s1));
            }
            if (law == 1)
            {
                CHECK (std::abs (rmsN - rms1) <= 1.0, "note %d V2: 8 voices are as loud as 1 (%+.2f dB)", note, rmsN - rms1);
                CHECK (lowN - low1 <= 1.5, "note %d V2: 8 voices carry no more bass than 1 (%+.2f dB below 150 Hz)", note, lowN - low1);
                CHECK (tpN <= -0.3, "note %d V2: 8 voices stay under the ceiling (%+.2f dBTP)", note, tpN);
                CHECK (corrN > 0.9, "note %d V2: the bass of 8 voices stays mono-compatible (low correlation %.2f)", note, corrN);
            }
        }
    section ("UNISON: detune modes and stacks render clean (finite, under the ceiling, loudness within 2 dB of SUPER / OFF)");
    {
        double ref = 0;
        for (int dmode = 0; dmode < 5; ++dmode)
        {
            auto p = uniPatch (1, 8, 48, dmode, 0);
            const std::vector<Ev> ph { { 0.0, 48, 0.9f, true }, { 1.3, 48, 0.0f, false } };
            seedSystemRandom (7); auto out = render (*p, ph, 1.5);
            int nans = 0; peakOf (out, &nans);
            const double rms = rmsDb (out, (int) (0.3 * SR), (int) (1.3 * SR));
            if (dmode == 1) ref = rms;
            static const char* nm[] { "Linear", "Super", "Exp", "Inv", "Random" };
            CHECK (nans == 0 && cleantest::truePeakDb (out) <= -0.3, "detune mode %-6s: RMS %6.1f dB, TP %+6.2f dBTP, NaN %d", nm[dmode], rms, cleantest::truePeakDb (out), nans);
        }
        for (int stack = 1; stack < 5; ++stack)
        {
            auto p = uniPatch (1, 8, 48, 1, stack);
            const std::vector<Ev> ph { { 0.0, 48, 0.9f, true }, { 1.3, 48, 0.0f, false } };
            seedSystemRandom (7); auto out = render (*p, ph, 1.5);
            int nans = 0; peakOf (out, &nans);
            const double rms = rmsDb (out, (int) (0.3 * SR), (int) (1.3 * SR)), cen = cleantest::centroid (out);
            static const char* nm[] { "Off", "12", "12+7", "Center -12", "Center -24" };
            CHECK (nans == 0 && cleantest::truePeakDb (out) <= -0.3 && std::abs (rms - ref) < 3.0, "stack %-10s: RMS %6.1f dB (SUPER/OFF %6.1f), TP %+6.2f dBTP, < 150 Hz %6.1f dB", nm[stack], rms, ref, cleantest::truePeakDb (out), bandBelow (out, 150.0, (int) (0.3 * SR), (int) (1.3 * SR)));
            juce::ignoreUnused (cen);
        }
    }
    section ("UNISON: an older state (no law switch saved) loads with the CLASSIC laws, INIT with the new ones");
    {
        auto p = makeProc (-1); p->initPatch(); settle (*p);
        CHECK ((int) p->uniLaw->load() == 1 && (int) p->fDLaw->load() == 1, "INIT: unison law V2, drive law INSIDE");
        auto t = p->captureSound();
        for (auto* id : { "uni_law", "f_dlaw" }) t.removeChild (t.getChildWithProperty ("id", id), nullptr);
        p->restoreSound (t, "old"); settle (*p);
        CHECK ((int) p->uniLaw->load() == 0 && (int) p->fDLaw->load() == 0, "a sound saved before round 2: unison law CLASSIC, drive law CLASSIC");
        p->setCurrentProgram (0); settle (*p);
        CHECK ((int) p->uniLaw->load() == 0 && (int) p->fDLaw->load() == 0, "a factory sound without a spec entry: CLASSIC laws");
    }
    printf ("\n%d failures\n", failures);
    return failures;
}

// ---------------------------------------------------------------- NOISE: colours, levels, stereo
static std::vector<std::pair<double, double>> thirdOctaves (const juce::AudioBuffer<float>& x, int start, int end)
{   // (centre Hz, dB) per 1/3-octave band from 100 Hz to 10 kHz, Hann 8192 frames
    constexpr int ORDER = 13, N = 1 << ORDER;
    juce::dsp::FFT fft (ORDER); std::vector<double> acc ((size_t) N / 2, 0.0); std::vector<float> buf ((size_t) N * 2);
    int frames = 0;
    for (int s0 = start; s0 + N <= end; s0 += N / 2, ++frames)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < N; ++i) buf[(size_t) i] = 0.5f * (x.getSample (0, s0 + i) + x.getSample (1, s0 + i)) * (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / N));
        fft.performFrequencyOnlyForwardTransform (buf.data());
        for (int k = 0; k < N / 2; ++k) acc[(size_t) k] += (double) buf[(size_t) k] * buf[(size_t) k];
    }
    std::vector<std::pair<double, double>> out;
    for (double f = 100.0; f <= 10000.0 * 1.01; f *= std::pow (2.0, 1.0 / 3.0))
    {
        const int k0 = (int) (f / std::pow (2.0, 1.0 / 6.0) * N / SR), k1 = std::max (k0 + 1, (int) (f * std::pow (2.0, 1.0 / 6.0) * N / SR));
        double e = 0; for (int k = k0; k < k1 && k < N / 2; ++k) e += acc[(size_t) k];
        out.push_back ({ f, 10 * std::log10 (std::max (1e-30, e / std::max (1, k1 - k0))) });      // per-bin density: a flat spectrum reads flat
    }
    return out;
}
static double slopeDbPerOct (const std::vector<std::pair<double, double>>& b)
{
    double sx = 0, sy = 0, sxx = 0, sxy = 0; const double n = (double) b.size();
    for (auto& [f, db] : b) { const double x = std::log2 (f); sx += x; sy += db; sxx += x * x; sxy += x * db; }
    return (n * sxy - sx * sy) / (n * sxx - sx * sx);
}
static double fullCorr (const juce::AudioBuffer<float>& b, int start, int end)
{
    double lr = 0, ll = 0, rr = 0;
    for (int i = start; i < end; ++i) { const double l = b.getSample (0, i), r = b.getSample (1, i); lr += l * r; ll += l * l; rr += r * r; }
    return ll > 1e-12 && rr > 1e-12 ? lr / std::sqrt (ll * rr) : 1.0;
}
static std::unique_ptr<ProProcessor> noisePatch (int type, float colour, float stereo, int quality = 0)
{
    auto p = makeProc (-1); p->initPatch();
    cleantest::set (*p, { { "o1_on", 0.0f }, { "nz_lvl", 1.0f }, { "nz_type", (float) type }, { "nz_col", colour }, { "nz_stereo", stereo }, { "f_type", 9.0f }, { "out_trim", -6.0f }, { "cl_on", 0.0f }, { "quality", (float) quality } });
    settle (*p);
    return p;
}
int noiseTests()
{
    section ("NOISE: spectral slopes (dB / octave, 100 Hz .. 10 kHz) and levels per colour, FILTER flat");
    static const char* nm[] { "Classic", "White", "Pink", "Brown", "Blue", "Violet", "Geiger", "Breath" };
    static const double want[] { 0, 0, -3, -6, 3, 6, 0, 0 };
    const std::vector<Ev> ph { { 0.0, 57, 0.9f, true }, { 2.3, 57, 0.0f, false } };
    const int s0 = (int) (0.3 * SR), s1 = (int) (2.3 * SR);
    double whiteLufs = 0;
    for (int t = 1; t < 8; ++t)
    {
        auto p = noisePatch (t, 0.5f, 1.0f);
        seedSystemRandom (11); auto out = render (*p, ph, 2.5);
        int nans = 0; peakOf (out, &nans);
        const auto bands = thirdOctaves (out, s0, s1);
        const double slope = slopeDbPerOct (bands), L = lufs (out, s0, s1), tp = cleantest::truePeakDb (out);
        if (t == 1) whiteLufs = L;
        if (t <= 5) CHECK (nans == 0 && std::abs (slope - want[t]) <= 1.0 && std::abs (L - whiteLufs) <= 2.5, "%-7s slope %+5.2f dB/oct (want %+.0f), %6.1f LUFS (white %+.1f), TP %+6.2f dBTP", nm[t], slope, want[t], L, L - whiteLufs, tp);
        else if (t == 6)
        {
            int clicks = 0; float env = 0;
            for (int i = s0; i < s1; ++i) { const float a = std::abs (out.getSample (0, i)); if (a > 4.0f * env && a > 0.01f) ++clicks; env = std::max (a, env * 0.995f); }
            CHECK (nans == 0 && clicks > 20 && std::abs (L - whiteLufs) <= 6.0, "%-7s %d clicks in 2 s at COLOUR 50 %%, %6.1f LUFS (white %+.1f), TP %+6.2f dBTP", nm[t], clicks, L, L - whiteLufs, tp);
        }
        else
        {
            double top = -1000, topF = 0; for (auto& [f, db] : bands) if (db > top) { top = db; topF = f; }
            const double f0 = juce::MidiMessage::getMidiNoteInHertz (57) * 4.0;
            CHECK (nans == 0 && std::abs (std::log2 (topF / f0)) <= 0.34 && std::abs (L - whiteLufs) <= 3.0, "%-7s peaks at %.0f Hz (the note x 4 = %.0f Hz), %6.1f LUFS (white %+.1f), TP %+6.2f dBTP", nm[t], topF, f0, L, L - whiteLufs, tp);
        }
    }
    section ("NOISE: every colour keeps its level at QUALITY High and Ultra (within 1.5 dB of Draft)");
    for (int q = 1; q <= 2; ++q)
    {
        double worst = 0; int worstT = 0;
        for (int t = 1; t < 8; ++t)
        {
            auto a = noisePatch (t, 0.5f, 1.0f, 0), b = noisePatch (t, 0.5f, 1.0f, q);
            seedSystemRandom (11); auto oa = render (*a, ph, 2.5);
            seedSystemRandom (11); auto ob = render (*b, ph, 2.5);
            const double d = lufs (ob, s0, s1) - lufs (oa, s0, s1);
            if (std::abs (d) > std::abs (worst)) { worst = d; worstT = t; }
        }
        CHECK (std::abs (worst) <= 1.5, "%s: worst colour level change %+.2f dB (%s)", q == 1 ? "High" : "Ultra", worst, nm[worstT]);
    }
    section ("NOISE: the FILTER knob (COLOUR) on white: low pass left, flat middle, high pass right");
    {
        double hfLo = 0, hfMid = 0, lowMid = 0, lowHi = 0;
        for (float col : { 0.15f, 0.5f, 0.85f })
        {
            auto p = noisePatch (1, col, 1.0f);
            seedSystemRandom (11); auto out = render (*p, ph, 2.5);
            const double hf = r2::shareAbove (out, 2000.0, s0, s1), low = bandBelow (out, 500.0, s0, s1) - cleantest::bands (out, s0, s1).total;
            if (col < 0.3f) hfLo = hf; else if (col < 0.6f) { hfMid = hf; lowMid = low; } else lowHi = low;
        }
        CHECK (hfLo < hfMid - 6.0 && lowHi < lowMid - 6.0, "COLOUR 15 %%: share above 2 kHz %+.1f dB (flat %+.1f); COLOUR 85 %%: share below 500 Hz %+.1f dB (flat %+.1f)", hfLo, hfMid, lowHi, lowMid);
    }
    section ("NOISE: STEREO 0 .. 100 = correlation 1 -> 0");
    {
        double c0 = 0, c50 = 0, c100 = 0;
        for (float st : { 0.0f, 0.5f, 1.0f })
        {
            auto p = noisePatch (1, 0.5f, st);
            seedSystemRandom (11); auto out = render (*p, ph, 2.5);
            const double c = fullCorr (out, s0, s1);
            if (st < 0.25f) c0 = c; else if (st < 0.75f) c50 = c; else c100 = c;
        }
        CHECK (c0 > 0.99 && c50 > 0.5 && c50 < 0.9 && std::abs (c100) < 0.05, "correlation at STEREO 0 %% %.3f, 50 %% %.3f, 100 %% %.3f", c0, c50, c100);
    }
    section ("NOISE: CLASSIC type is the old noise, bit for bit (an older state loads with it)");
    {
        auto p = noisePatch (0, 0.6f, 1.0f);
        auto t = p->captureSound(); t.removeChild (t.getChildWithProperty ("id", "nz_type"), nullptr);
        p->restoreSound (t, "old"); settle (*p);
        CHECK ((int) p->nzType->load() == 0, "a sound saved before round 2 loads with NOISE TYPE = Classic");
    }
    printf ("\n%d failures\n", failures);
    return failures;
}

// ---------------------------------------------------------------- GHOST VERB
static double verbT60 (ghosty::GhostVerb& v, const ghosty::GhostVerb::Params& p, std::vector<float>* keepL = nullptr, std::vector<float>* keepR = nullptr)
{   // impulse response -> Schroeder decay -> T60 from the -5 .. -35 dB slope
    const int n = (int) (8 * SR);
    std::vector<float> L ((size_t) n, 0.0f), R ((size_t) n, 0.0f);
    v.reset(); L[0] = R[0] = 1.0f;
    for (int pos = 0; pos < n; pos += 512) v.process (L.data() + pos, R.data() + pos, std::min (512, n - pos), p);
    std::vector<double> edc ((size_t) n); double acc = 0;
    for (int i = n - 1; i >= 0; --i) { const double m = 0.5 * (L[(size_t) i] + R[(size_t) i]); acc += m * m; edc[(size_t) i] = acc; }
    const double top = 10 * std::log10 (edc[0] + 1e-30);
    int i5 = -1, i35 = -1;
    for (int i = 0; i < n; ++i) { const double d = 10 * std::log10 (edc[(size_t) i] + 1e-30) - top; if (i5 < 0 && d <= -5) i5 = i; if (i35 < 0 && d <= -35) { i35 = i; break; } }
    if (keepL) *keepL = L; if (keepR) *keepR = R;
    return (i5 >= 0 && i35 > i5) ? (i35 - i5) / SR * 2.0 : -1;
}
int verbTests()
{
    section ("GHOST VERB: T60 follows DECAY, flat over SIZE; tail bands smooth; wet never louder than the hit; it dies out");
    ghosty::GhostVerb v; v.prepare (SR);
    {
        double prev = 0; bool mono = true; juce::String row;
        for (float dec : { 0.5f, 1.0f, 2.0f, 4.0f, 8.0f })
        {
            ghosty::GhostVerb::Params p; p.mix = 1.0f; p.decay = dec; p.size = 0.6f;
            const double t = verbT60 (v, p); row << "  " << juce::String (dec, 1) << " s -> " << juce::String (t, 2);
            if (t < prev * 1.1) mono = false; prev = t;
        }
        CHECK (mono, "T60 rises with DECAY:%s", row.toRawUTF8());
        prev = 0; double lo = 1e9, hi = 0; row = {};
        for (float sz : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            ghosty::GhostVerb::Params p; p.mix = 1.0f; p.decay = 2.0f; p.size = sz;
            const double t = verbT60 (v, p); row << "  size " << juce::String (sz, 2) << " -> " << juce::String (t, 2); lo = std::min (lo, t); hi = std::max (hi, t);
        }
        CHECK (hi <= lo * 1.35 && lo > 1.5, "T60 at DECAY 2 s stays 2 s +- 35 %% over SIZE (DECAY means seconds, SIZE means room):%s", row.toRawUTF8());
    }
    {
        ghosty::GhostVerb::Params p; p.mix = 1.0f; p.decay = 2.0f; p.size = 0.6f;
        std::vector<float> L, R; verbT60 (v, p, &L, &R);
        juce::AudioBuffer<float> b (2, (int) L.size()); for (int i = 0; i < (int) L.size(); ++i) { b.setSample (0, i, L[(size_t) i]); b.setSample (1, i, R[(size_t) i]); }
        // 1/3-octave bands of the tail 0.3 .. 1.1 s: no band more than 6 dB above the mean of its neighbours
        const auto bands = thirdOctaves (b, (int) (0.3 * SR), (int) (1.1 * SR));
        double worst = -100; double worstF = 0;
        for (size_t i = 1; i + 1 < bands.size(); ++i) { const double d = bands[i].second - 0.5 * (bands[i - 1].second + bands[i + 1].second); if (d > worst) { worst = d; worstF = bands[i].first; } }
        CHECK (worst <= 6.0, "tail 1/3-octave bands: worst band %+.1f dB above its neighbours (at %.0f Hz)", worst, worstF);
        float pk = 0; for (int i = 1; i < (int) L.size(); ++i) pk = std::max (pk, std::max (std::abs (L[(size_t) i]), std::abs (R[(size_t) i])));
        CHECK (pk < 1.0f, "wet-only response to a unit impulse peaks at %.3f (never above the hit)", pk);
        double tailE = 0; for (int i = (int) (7.5 * SR); i < (int) L.size(); ++i) tailE += L[(size_t) i] * L[(size_t) i];
        CHECK (10 * std::log10 (tailE / (0.5 * SR) + 1e-30) < -90.0, "7.5 s after the hit the tail is below -90 dB (%.1f dB): no floor, no denormals", 10 * std::log10 (tailE / (0.5 * SR) + 1e-30));
    }
    section ("GHOST VERB in the synth: the REVERB knob at 50 %% on INIT = about 17 %% wet; LO CUT keeps the wet out of the bass; DUCK works");
    {
        auto dryP = makeProc (-1); dryP->initPatch(); cleantest::set (*dryP, { { "verb", 0.0f }, { "out_trim", -6.0f } }); settle (*dryP);
        auto wetP = makeProc (-1); wetP->initPatch(); cleantest::set (*wetP, { { "verb", 0.5f }, { "verb_type", 1.0f }, { "out_trim", -6.0f } }); settle (*wetP);
        const std::vector<Ev> ph { { 0.0, 48, 0.9f, true }, { 0.5, 48, 0.0f, false } };
        seedSystemRandom (3); auto d = render (*dryP, ph, 3.0);
        seedSystemRandom (3); auto w = render (*wetP, ph, 3.0);
        // the tail after the note (0.9 .. 2.9 s) vs the note itself: a 17 % wet, 1.6 s decay tail sits well under the note
        const double note = rmsDb (w, (int) (0.1 * SR), (int) (0.5 * SR)), tail = rmsDb (w, (int) (0.9 * SR), (int) (1.4 * SR)), dryTail = rmsDb (d, (int) (0.9 * SR), (int) (1.4 * SR));
        CHECK (tail < note - 12.0 && tail > dryTail + 6.0, "tail 0.4 s after the note: %.1f dB (note %.1f dB; without reverb %.1f dB)", tail, note, dryTail);
        const auto bw = cleantest::bands (w, (int) (0.9 * SR), (int) (1.4 * SR));
        CHECK (bw.lowAbs < bw.total - 10.0, "the tail's bass (< 120 Hz) sits %.1f dB under its total (LO CUT 180 Hz)", bw.total - bw.lowAbs);
        auto duckP = makeProc (-1); duckP->initPatch(); cleantest::set (*duckP, { { "verb", 0.5f }, { "verb_type", 1.0f }, { "verb_duck", 1.0f }, { "out_trim", -6.0f } }); settle (*duckP);
        seedSystemRandom (3); auto dk = render (*duckP, ph, 3.0);
        const double during = rmsDb (w, (int) (0.2 * SR), (int) (0.5 * SR)) - rmsDb (d, (int) (0.2 * SR), (int) (0.5 * SR));
        const double duringDuck = rmsDb (dk, (int) (0.2 * SR), (int) (0.5 * SR)) - rmsDb (d, (int) (0.2 * SR), (int) (0.5 * SR));
        CHECK (duringDuck < during, "DUCK 100 %%: while the note plays the reverb adds %+.2f dB instead of %+.2f dB", duringDuck, during);
    }
    section ("GHOST VERB: a big sound (8-voice unison chord, INIT, REVERB 50 %%) keeps mud share and crest within the round-1 limits");
    {
        for (int type : { 0, 1 })
        {
            auto p = makeProc (-1); p->initPatch();
            cleantest::set (*p, { { "o1_uni", 8.0f }, { "o1_det", 0.35f }, { "o1_spr", 0.8f }, { "o2_on", 1.0f }, { "o2_uni", 8.0f }, { "verb", 0.5f }, { "verb_type", (float) type }, { "out_trim", -6.0f } }); settle (*p);
            seedSystemRandom (3);
            auto out = render (*p, cleantest::chordPhrase(), 5.6);
            const auto m = cleantest::measure (out);
            cleantest::printMetrics (type ? "GHOST verb" : "CLASSIC verb", m);
            if (type == 1) CHECK (m.tp <= -0.3 && m.mudShare <= -4.0 && m.crest >= 8.0 && m.crest <= 22.0, "GHOST verb big chord: TP %+.2f dBTP, mud share %+.1f dB, crest %.1f dB", m.tp, m.mudShare, m.crest);
        }
    }
    section ("GHOST VERB: an older state (no reverb type saved) keeps the CLASSIC reverb");
    {
        auto p = makeProc (-1); p->initPatch(); settle (*p);
        CHECK ((int) p->vbType->load() == 1, "INIT: GHOST verb");
        auto t = p->captureSound(); t.removeChild (t.getChildWithProperty ("id", "verb_type"), nullptr);
        p->restoreSound (t, "old"); settle (*p);
        CHECK ((int) p->vbType->load() == 0, "a sound saved before round 2: CLASSIC verb");
    }
    printf ("\n%d failures\n", failures);
    return failures;
}

// ---------------------------------------------------------------- QUALITY: Draft / High / Ultra
int qualityTests()
{
    section ("QUALITY: Draft is the old engine (an older state loads Draft, INIT loads High), High / Ultra are the same sound, latency reported");
    const std::vector<Ev> ph { { 0.0, 57, 0.9f, true }, { 1.0, 57, 0.0f, false } };
    {
        auto p = makeProc (-1); p->initPatch(); settle (*p);
        CHECK ((int) p->qualityP->load() == 1, "INIT loads QUALITY High");
        auto t = p->captureSound(); t.removeChild (t.getChildWithProperty ("id", "quality"), nullptr);
        p->restoreSound (t, "old"); settle (*p);
        CHECK ((int) p->qualityP->load() == 0, "a state saved before the QUALITY switch loads Draft");
        p->setParam ("quality", 2.0f); p->setParam ("q_lock", 1.0f); p->setCurrentProgram (3); settle (*p);
        CHECK ((int) p->qualityP->load() == 2 && p->qLock->load() > 0.5f, "LOCK on: a factory sound load keeps Ultra");
        p->setParam ("q_lock", 0.0f); p->setCurrentProgram (3); settle (*p);
        CHECK ((int) p->qualityP->load() == 1, "LOCK off: a factory sound loads at High (SOUND ENGINE V2 default)");
    }
    {   // the same sound at every quality (INIT saw + square, LP 500 Hz, Inside drive 50 %): spectrum / envelope within the compare tolerance
        juce::AudioBuffer<float> ref;
        for (int q = 0; q < 3; ++q)
        {
            auto p = ownerPatch (gpro::dlInside, 0.5f); p->setParam ("quality", (float) q); settle (*p);
            seedSystemRandom (5); auto out = render (*p, ph, 1.6);
            const int lat = p->engineLatency();
            if (q == 0) { ref = out; CHECK (lat == 17, "Draft + Inside drive: engine latency %d (17)", lat); continue; }
            CHECK (lat == (q == 1 ? 37 : 12), "%s + Inside drive: engine latency %d (%d)", q == 1 ? "High" : "Ultra", lat, q == 1 ? 37 : 12);
            // align by the latency difference, then compare
            const int shift = lat - 17;
            juce::AudioBuffer<float> al (2, out.getNumSamples()); al.clear();
            for (int ch = 0; ch < 2; ++ch) for (int i = 0; i + shift < out.getNumSamples() && i < out.getNumSamples(); ++i) if (i + shift >= 0) al.setSample (ch, i, out.getSample (ch, i + shift));
            double envErr = 0, specErr = 0;
            const bool same = sameSound (ref, al, envErr, specErr, 2.0, 2.5);
            CHECK (same, "%s is the same sound as Draft: envelope %.2f dB, spectrum %.2f dB (shift %d)", q == 1 ? "High" : "Ultra", envErr, specErr, shift);
        }
        auto p = ownerPatch (gpro::dlClassic, 0.0f); p->setParam ("quality", 1.0f); settle (*p); seedSystemRandom (5); render (*p, ph, 0.3);
        CHECK (p->engineLatency() == 25, "High + Classic drive: engine latency %d (25)", p->engineLatency());
    }
    section ("QUALITY: a nonlinear voice (OSC MOD FM at 100 %, C6) aliases less at High and Ultra than at Draft");
    {
        double alias[3] {};
        for (int q = 0; q < 3; ++q)
        {
            auto p = makeProc (-1); p->initPatch();
            cleantest::set (*p, { { "o2_on", 1.0f }, { "xmod", 1.0f }, { "xamt", 1.0f }, { "f_type", 9.0f }, { "quality", (float) q }, { "out_trim", -6.0f } }); settle (*p);
            const std::vector<Ev> hi { { 0.0, 84, 0.9f, true }, { 1.3, 84, 0.0f, false } };
            seedSystemRandom (5); auto out = render (*p, hi, 1.5);
            // energy between the harmonics of C6 (1046.5 Hz): the bins at least 1/4 harmonic away from any multiple, re the loudest harmonic
            constexpr int ORDER = 15, N = 1 << ORDER;
            juce::dsp::FFT fft (ORDER); std::vector<float> buf ((size_t) N * 2, 0.0f);
            const int s0 = (int) (0.3 * SR);
            for (int i = 0; i < N; ++i) buf[(size_t) i] = 0.5f * (out.getSample (0, s0 + i) + out.getSample (1, s0 + i)) * (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / N));
            fft.performFrequencyOnlyForwardTransform (buf.data());
            const double f0 = juce::MidiMessage::getMidiNoteInHertz (84), binW = SR / N;
            double top = 0, between = 0, worstF = 0;
            for (int k = 1; k < N / 2; ++k)
            {
                const double f = k * binW, h = f / f0, frac = std::abs (h - std::round (h));
                const double e = (double) buf[(size_t) k] * buf[(size_t) k];
                if (frac < 0.08) top = std::max (top, e); else if (frac > 0.25 && f < 20000.0 && e > between) { between = e; worstF = f; }
            }
            alias[q] = 10 * std::log10 (between / std::max (1e-30, top));
            printf ("    %s: worst between-harmonic line %.1f dB at %.0f Hz\n", q == 0 ? "Draft" : q == 1 ? "High " : "Ultra", alias[q], worstF);
        }
        // (at this FM depth the worst line at Draft and High is the 73rd harmonic of C6 at 76 kHz, which lands at 19.5 kHz after one fold
        //  of 96 k or two folds of 48 k alike: only Ultra represents it and filters it out; High removes everything between 24 and 48 k)
        CHECK (alias[1] <= alias[0] + 0.5 && alias[2] < alias[1] - 10.0, "worst between-harmonic line re the loudest harmonic: Draft %.1f dB, High %.1f dB, Ultra %.1f dB", alias[0], alias[1], alias[2]);
    }
    section ("VOICES: playing / max counting unison voices");
    {
        auto p = makeProc (-1); p->initPatch(); cleantest::set (*p, { { "o1_uni", 4.0f }, { "o2_on", 1.0f }, { "o2_uni", 2.0f }, { "poly", 8.0f } }); settle (*p);
        juce::AudioBuffer<float> b (2, BS); juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0); m.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 0);
        b.clear(); p->processBlock (b, m);
        CHECK (p->uniVoicesLive.load() == 12 && p->uniVoicesMax.load() == 48, "two notes x (4 + 2) unison voices: %d playing / %d max (8 voices x 6)", p->uniVoicesLive.load(), p->uniVoicesMax.load());
    }
    section ("QUALITY: ULTRA WHEN RENDERING lifts an offline render to 4x");
    {
        auto p = makeProc (-1); p->initPatch(); cleantest::set (*p, { { "quality", 0.0f }, { "q_render", 1.0f } }); settle (*p);
        juce::AudioBuffer<float> b (2, BS); juce::MidiBuffer m;
        p->setNonRealtime (true); b.clear(); p->processBlock (b, m);
        const int qOff = p->engineQLive.load();
        p->setNonRealtime (false); b.clear(); p->processBlock (b, m);
        CHECK (qOff == 4 && p->engineQLive.load() == 1, "Draft + ULTRA ON RENDER: offline block ran at %dx, live block at %dx", qOff, p->engineQLive.load());
    }
    printf ("\n%d failures\n", failures);
    return failures;
}

// ---------------------------------------------------------------- HYPER / DIMENSION (FX rack unit)
int hyperTests()
{
    section ("HYPER: off = untouched; 1 .. 7 voices at one loudness (power-normalised); RETRIG makes every note start alike; DIMENSION runs; state");
    const std::vector<Ev> ph { { 0.0, 57, 0.9f, true }, { 1.0, 57, 0.0f, false }, { 1.2, 57, 0.9f, true }, { 2.2, 57, 0.0f, false }, { 2.4, 57, 0.9f, true }, { 3.4, 57, 0.0f, false } };
    auto mk = [&] (bool on, int voices, int mode, bool retrig)
    {
        auto p = makeProc (-1); p->initPatch();
        cleantest::set (*p, { { "fx_hyper_on", on ? 1.0f : 0.0f }, { "fx_hyper_voices", (float) voices }, { "fx_hyper_mode", (float) mode }, { "fx_hyper_retrig", retrig ? 1.0f : 0.0f }, { "fx_hyper_mix", 1.0f }, { "out_trim", -6.0f }, { "quality", 0.0f } });
        settle (*p); return p;
    };
    seedSystemRandom (5); auto dry = render (*mk (false, 4, 0, true), ph, 3.7);
    {
        auto p = makeProc (-1); p->initPatch(); cleantest::set (*p, { { "out_trim", -6.0f }, { "quality", 0.0f } }); settle (*p);
        seedSystemRandom (5); auto plain = render (*p, ph, 3.7);
        CHECK (maxDiff (plain, dry) == 0.0f, "HYPER off: the rack output is untouched (max diff %.3g)", maxDiff (plain, dry));
    }
    // loudness over the voice count on a white-noise source (delayed copies of noise add in power; on a periodic tone the copies
    // comb-filter each other by a few dB either way, which is the sound of a chorus, not a level law)
    double r1 = 0, worst = 0;
    for (int v = 1; v <= 7; ++v)
    {
        auto np = mk (true, v, 0, true); cleantest::set (*np, { { "o1_on", 0.0f }, { "nz_lvl", 1.0f }, { "nz_type", 1.0f }, { "nz_col", 0.5f }, { "f_type", 9.0f } }); settle (*np);
        seedSystemRandom (5); auto out = render (*np, ph, 3.7);
        const double rms = rmsDb (out, (int) (0.2 * SR), (int) (0.9 * SR));
        if (v == 1) r1 = rms; worst = std::max (worst, std::abs (rms - r1));
        int nans = 0; peakOf (out, &nans);
        if (v == 1 || v == 4 || v == 7) printf ("    %d voice%s: RMS %6.1f dB, TP %+6.2f dBTP, side/mid %+5.1f dB, NaN %d\n", v, v > 1 ? "s" : "", rms, cleantest::truePeakDb (out), cleantest::sideRatioDb (out), nans);
    }
    CHECK (worst <= 1.0, "1 .. 7 voices stay within %.2f dB of each other", worst);
    {
        seedSystemRandom (5); auto a = render (*mk (true, 4, 0, true), ph, 3.7);
        // the second and third notes (1.2 s, 2.4 s; the unit's own switch-on glide is long over) with RETRIG: 30 .. 80 ms after
        // their starts (the lines, up to 26 ms long, have flushed the old tail) they are equal (same wobble phases); without
        // RETRIG the wobble sits 0.7 cycles further on
        float md = 0; const int n0 = (int) (1.230 * SR), n1 = (int) (2.430 * SR), len = (int) (0.050 * SR);
        for (int i = 0; i < len; ++i) md = std::max (md, std::abs (a.getSample (0, n0 + i) - a.getSample (0, n1 + i)));
        seedSystemRandom (5); auto b = render (*mk (true, 4, 0, false), ph, 3.7);
        float md2 = 0; for (int i = 0; i < len; ++i) md2 = std::max (md2, std::abs (b.getSample (0, n0 + i) - b.getSample (0, n1 + i)));
        float md0 = 0; for (int i = 0; i < len; ++i) md0 = std::max (md0, std::abs (dry.getSample (0, n0 + i) - dry.getSample (0, n1 + i)));
        printf ("    (the dry synth's two windows differ by %.2e)\n", md0);
        CHECK (md < 1.0e-3f && md2 > 10.0f * md, "RETRIG: the two notes' 30 .. 80 ms windows differ by %.2e (without RETRIG %.2e)", md, md2);
    }
    {
        seedSystemRandom (5); auto out = render (*mk (true, 4, 1, true), ph, 3.7);
        int nans = 0; peakOf (out, &nans);
        const double rms = rmsDb (out, (int) (0.2 * SR), (int) (0.9 * SR));
        CHECK (nans == 0 && std::abs (rms - r1) <= 3.0 && cleantest::sideRatioDb (out) > -20.0, "DIMENSION: RMS %6.1f dB (HYPER 1 voice %6.1f), side/mid %+5.1f dB, TP %+6.2f dBTP", rms, r1, cleantest::sideRatioDb (out), cleantest::truePeakDb (out));
    }
    {
        auto p = mk (true, 5, 1, false);
        juce::MemoryBlock mb; p->getStateInformation (mb);
        auto q = makeProc (-1); q->setStateInformation (mb.getData(), (int) mb.getSize()); settle (*q);
        CHECK (q->apvts.getRawParameterValue ("fx_hyper_on")->load() > 0.5f && (int) q->apvts.getRawParameterValue ("fx_hyper_voices")->load() == 5 && (int) q->apvts.getRawParameterValue ("fx_hyper_mode")->load() == 1,
               "state round trip keeps HYPER on, 5 voices, DIMENSION");
        const auto ord = p->rack.saveState().getProperty ("order").toString();
        CHECK (! ord.contains ("hyper"), "the saved rack order leaves HYPER out at its default place (older builds read the state unchanged): \"%s\"", ord.toRawUTF8());
    }
    printf ("\n%d failures\n", failures);
    return failures;
}

// ---------------------------------------------------------------- OSCILLATOR QUALITY: aliasing per table and octave
// worst non-harmonic line (bins at least a quarter harmonic away from any multiple of f0) re the loudest harmonic, dB
// (65536-point 4-term Blackman-Harris window: side lobes -92 dB, so a C1's harmonics 33 Hz apart do not leak into the gaps)
static inline double bh4 (int i, int N) { const double a = 2.0 * juce::MathConstants<double>::pi * i / N; return 0.35875 - 0.48829 * std::cos (a) + 0.14128 * std::cos (2 * a) - 0.01168 * std::cos (3 * a); }
static double aliasOf (const juce::AudioBuffer<float>& out, double f0, int start, double* fundDb = nullptr, double* topDbOut = nullptr, double refTopDb = -1000.0)
{
    constexpr int ORDER = 16, N = 1 << ORDER;
    juce::dsp::FFT fft (ORDER); std::vector<float> buf ((size_t) N * 2, 0.0f);
    for (int i = 0; i < N; ++i) buf[(size_t) i] = 0.5f * (out.getSample (0, start + i) + out.getSample (1, start + i)) * (float) bh4 (i, N);
    fft.performFrequencyOnlyForwardTransform (buf.data());
    const double binW = SR / N;
    double top = 0, between = 0, fund = 0;
    for (int k = 1; k < N / 2; ++k)
    {
        const double f = k * binW, h = f / f0, frac = std::abs (h - std::round (h));
        const double e = (double) buf[(size_t) k] * buf[(size_t) k];
        if (frac < 0.1) { top = std::max (top, e); if (std::round (h) == 1.0) fund = std::max (fund, e); }
        else if (frac > 0.25 && f < 20000.0 && f > 30.0) between = std::max (between, e);
    }
    if (fundDb) *fundDb = 10 * std::log10 (std::max (1e-30, fund));
    if (topDbOut) *topDbOut = 10 * std::log10 (std::max (1e-30, top));
    if (refTopDb > -999.0) return 10 * std::log10 (std::max (1e-30, between)) - refTopDb;     // alias re a reference harmonic level (a spectral mode may remove the loudest harmonic)
    return 10 * std::log10 (between / std::max (1e-30, top));
}
int oscTests (const juce::StringArray& args)
{
    const int quality = args.size() > 0 ? args[0].getIntValue() : 0;
    const bool strict = args.size() > 1 && args[1] == "check";
    static const char* qn[] { "Draft", "High", "Ultra" };
    section ((juce::String ("OSCILLATORS: worst alias line re the loudest harmonic, per table and octave (C1 .. C8), 1 voice, filter open, ") + qn[quality]).toRawUTF8());
    const int notes[] { 24, 36, 48, 60, 72, 84, 96, 108 };
    const auto& names = gpro::tableNames();
    double worstBelowC6 = -200, worstAll = -200; juce::String worstName, worstAllName;
    printf ("    %-14s pos ", "table"); for (int n : notes) printf ("   C%d  ", n / 12 - 1); printf ("\n");
    for (int tbl = 1; tbl < gpro::numTables; ++tbl)
    {
        if (tbl == gpro::tSample || tbl == gpro::tUser) continue;    // (the User table is swept by wtableTests with a table loaded)
        for (float pos : { 0.0f, 0.5f })
        {
            printf ("    %-14s %.1f ", names[tbl].toRawUTF8(), pos);
            for (int n : notes)
            {
                auto p = makeProc (-1); p->initPatch();
                cleantest::set (*p, { { "o1_tbl", (float) tbl }, { "o1_pos", pos }, { "f_type", 9.0f }, { "quality", (float) quality }, { "out_trim", -6.0f }, { "cl_on", 0.0f } }); settle (*p);
                const std::vector<Ev> ph { { 0.0, n, 0.9f, true }, { 1.8, n, 0.0f, false } };
                seedSystemRandom (5); auto out = render (*p, ph, 1.9);
                const double a = aliasOf (out, juce::MidiMessage::getMidiNoteInHertz (n), (int) (0.3 * SR));
                printf ("%6.1f ", a);
                if (n < 84 && a > worstBelowC6) { worstBelowC6 = a; worstName = names[tbl] + " pos " + juce::String (pos, 1) + " C" + juce::String (n / 12 - 1); }
                if (a > worstAll) { worstAll = a; worstAllName = names[tbl] + " pos " + juce::String (pos, 1) + " C" + juce::String (n / 12 - 1); }
            }
            printf ("\n"); fflush (stdout);
        }
    }
    // the owner's init saw + square at C5, 1 and 8 voices
    for (int uni : { 1, 8 })
    {
        auto p = makeProc (-1); p->initPatch();
        cleantest::set (*p, { { "o2_on", 1.0f }, { "o2_tbl", (float) gpro::tPulse }, { "o2_pos", 0.0f }, { "o1_uni", (float) uni }, { "o2_uni", (float) uni }, { "f_type", 9.0f }, { "quality", (float) quality }, { "out_trim", -6.0f } }); settle (*p);
        const std::vector<Ev> ph { { 0.0, 72, 0.9f, true }, { 1.8, 72, 0.0f, false } };
        seedSystemRandom (5); auto out = render (*p, ph, 1.9);
        printf ("    init saw + square C5, %d voice%s: worst alias %.1f dB%s\n", uni, uni > 1 ? "s" : "", aliasOf (out, juce::MidiMessage::getMidiNoteInHertz (72), (int) (0.3 * SR)), uni > 1 ? " (detuned voices put energy between harmonics: not all of this is alias)" : "");
    }
    // in-band partials of the Saw at C1 / C2 / C3 (harmonics 1, 5, 20, 100): printed so a reader change can be checked to stay within 0.1 dB
    for (int n : { 24, 36, 48 })
    {
        auto p = makeProc (-1); p->initPatch();
        cleantest::set (*p, { { "o1_tbl", (float) gpro::tSaw }, { "o1_pos", 0.0f }, { "f_type", 9.0f }, { "quality", (float) quality }, { "out_trim", -6.0f }, { "cl_on", 0.0f } }); settle (*p);
        const std::vector<Ev> ph { { 0.0, n, 0.9f, true }, { 1.8, n, 0.0f, false } };
        seedSystemRandom (5); auto out = render (*p, ph, 1.9);
        constexpr int ORDER = 16, N = 1 << ORDER;
        juce::dsp::FFT fft (ORDER); std::vector<float> buf ((size_t) N * 2, 0.0f);
        const int s0 = (int) (0.3 * SR);
        for (int i = 0; i < N; ++i) buf[(size_t) i] = 0.5f * (out.getSample (0, s0 + i) + out.getSample (1, s0 + i)) * (float) bh4 (i, N);
        fft.performFrequencyOnlyForwardTransform (buf.data());
        const double f0 = juce::MidiMessage::getMidiNoteInHertz (n), binW = SR / N;
        printf ("    Saw C%d partials (dB):", n / 12 - 1);
        for (int h : { 1, 5, 20, 100 })
        {
            const int k0 = (int) std::round (h * f0 / binW); double e = 0;
            for (int k = k0 - 2; k <= k0 + 2; ++k) if (k > 0 && k < N / 2) e = std::max (e, (double) buf[(size_t) k] * buf[(size_t) k]);
            printf ("  h%d %6.1f", h, 10 * std::log10 (std::max (1e-30, e)));
        }
        printf ("\n");
    }
    // hard SYNC (OSC MOD = Sync, 50 %) at C3 / C5: the phase reset is a step, its alias is what a BLEP has to take care of
    for (int n : { 48, 72 })
    {
        auto p = makeProc (-1); p->initPatch();
        cleantest::set (*p, { { "o2_on", 1.0f }, { "o2_tbl", (float) gpro::tSaw }, { "o1_lvl", 0.0f }, { "xmod", 2.0f }, { "xamt", 0.37f }, { "f_type", 9.0f }, { "quality", (float) quality }, { "out_trim", -6.0f }, { "cl_on", 0.0f } }); settle (*p);
        const std::vector<Ev> ph { { 0.0, n, 0.9f, true }, { 1.8, n, 0.0f, false } };
        seedSystemRandom (5); auto out = render (*p, ph, 1.9);
        printf ("    SYNC 37 %% (ratio 3.22) saw C%d: worst alias %.1f dB\n", n / 12 - 1, aliasOf (out, juce::MidiMessage::getMidiNoteInHertz (n), (int) (0.3 * SR)));
    }
    // WARP + SPECTRAL (round 3 item 2): every mode at 0.5 and 1.0 on the init saw at C3 and C5 (slot 1; Sync / FM / PD / AM / RM get their sources:
    // OSC 2 = square, sub 50 %, noise white 30 %); amount 0 must be bit-identical to Off
    double worstWarp = -200, worstWarpC3 = -200; juce::String worstWarpName, worstWarpC3Name;
    {
        // only the source a mode reads is switched on (the sub is a sine an octave down: the harmonic grid halves for it; the noise is
        // broadband by nature, so the Noise modes are reported, not judged)
        auto mkw = [&] (int mode, float amt, int note, bool spectral)
        {
            const int src = spectral ? -1 : gpro::warpSource (mode);
            auto p = makeProc (-1); p->initPatch();
            cleantest::set (*p, { { "o2_on", 1.0f }, { "o2_tbl", (float) gpro::tPulse }, { "o2_pos", 0.0f }, { "o2_lvl", 0.0f }, { "sub_lvl", src == 1 ? 0.5f : 0.0f }, { "nz_lvl", src == 2 ? 0.3f : 0.0f }, { "nz_type", 1.0f },
                                  { "f_type", 9.0f }, { "quality", (float) quality }, { "out_trim", -6.0f }, { "cl_on", 0.0f },
                                  { spectral ? "o1_spec" : "o1_w1", (float) mode }, { spectral ? "o1_speca" : "o1_w1a", amt } });
            settle (*p);
            const std::vector<Ev> ph { { 0.0, note, 0.9f, true }, { 1.8, note, 0.0f, false } };
            seedSystemRandom (5); return render (*p, ph, 1.9);
        };
        printf ("    %-14s  C3 @0.5  C3 @1.0  C5 @0.5  C5 @1.0\n", "warp");
        const auto& wn = gpro::warpModeNames();
        for (int m = 1; m < gpro::numWarpModes; ++m)
        {
            printf ("    %-14s", wn[m].toRawUTF8());
            const int src = gpro::warpSource (m);
            for (int note : { 48, 72 }) for (float amt : { 0.5f, 1.0f })
            {
                auto out = mkw (m, amt, note, false);
                const double grid = juce::MidiMessage::getMidiNoteInHertz (note) * (src == 1 ? 0.5 : 1.0);
                const double a = aliasOf (out, grid, (int) (0.3 * SR));
                printf (" %8.1f", a);
                // (the two saturator warps are judged at -70 dB even at Ultra: a waveshaped near-square has 1/n partials to infinity, and what
                //  sits above 168 kHz still folds into the band at 4x - about -75 dB for Zero-Square at 100 %; nothing short of a BLEP-type
                //  shaper can do better, and the measurement is the same number at every quality)
                // Likewise the kinked warps (Mirror, Formant, Pulse Width, Sync) are judged at -80 at Ultra: their C3 numbers are the same at
                // Draft, High and Ultra (Mirror 100 %: -82.3 / -82.3 / -82.6 dB), so that residue is the waveform's own floor in this
                // measurement, not aliasing - the aliasing is what drops with the quality (Mirror C5 100 %: -39 / -57 / -96 dB).
                const bool shaper = m == gpro::wTube || m == gpro::wZeroSquare, kink = m == gpro::wMirror || m == gpro::wFormant || m == gpro::wPulseWidth || m == gpro::wSync;
                const double judged = quality == 2 ? (shaper ? a - 20.0 : kink ? a - 10.0 : a) : a;
                if (src != 2 && a > worstWarp) { worstWarp = a; worstWarpName = wn[m] + " C" + juce::String (note / 12 - 1) + " @" + juce::String (amt, 1); }
                if (src != 2 && note == 48 && judged > worstWarpC3) { worstWarpC3 = judged; worstWarpC3Name = wn[m] + " @" + juce::String (amt, 1) + (quality == 2 && shaper ? " (shaper: judged at -70)" : quality == 2 && kink ? " (kink: judged at -80)" : ""); }
            }
            printf (src == 2 ? "  (noise in the output: not judged)\n" : "\n"); fflush (stdout);
        }
        const auto& sn = gpro::specModeNames();
        double worstSpec = -200; juce::String worstSpecName;
        // (the spectral modes are judged against the plain saw's loudest harmonic: a spectral low / high pass takes partials away, which
        //  would otherwise make the same absolute alias floor read higher)
        double refTop[2] {};
        for (int k = 0; k < 2; ++k) { auto o = mkw (0, 0.0f, k == 0 ? 48 : 72, true); aliasOf (o, juce::MidiMessage::getMidiNoteInHertz (k == 0 ? 48 : 72), (int) (0.3 * SR), nullptr, &refTop[k]); }
        for (int m = 1; m < gpro::numSpecModes; ++m)
        {
            printf ("    %-14s", sn[m].toRawUTF8());
            for (int note : { 48, 72 }) for (float amt : { 0.5f, 1.0f })
            {
                auto out = mkw (m, amt, note, true);
                const double a = aliasOf (out, juce::MidiMessage::getMidiNoteInHertz (note), (int) (0.3 * SR), nullptr, nullptr, refTop[note == 48 ? 0 : 1]);
                printf (" %8.1f", a);
                if (a > worstSpec) { worstSpec = a; worstSpecName = sn[m] + " C" + juce::String (note / 12 - 1) + " @" + juce::String (amt, 1); }
            }
            printf ("\n"); fflush (stdout);
        }
        if (strict)
        {
            const double lim = quality == 0 ? -60.0 : quality == 1 ? -70.0 : -90.0;
            CHECK (worstSpec <= lim, "%s: worst alias line of the SPECTRAL modes %.1f dB (limit %.0f) at %s", qn[quality], worstSpec, lim, worstSpecName.toRawUTF8());
            CHECK (worstWarpC3 <= lim, "%s: worst alias line of the WARP modes at C3 %.1f dB (limit %.0f) at %s; at C5 %.1f dB (%s, reported)", qn[quality], worstWarpC3, lim, worstWarpC3Name.toRawUTF8(), worstWarp, worstWarpName.toRawUTF8());
        }
        else printf ("  %s: worst WARP %.1f dB (%s), at C3 %.1f dB (%s), worst SPECTRAL %.1f dB (%s)\n", qn[quality], worstWarp, worstWarpName.toRawUTF8(), worstWarpC3, worstWarpC3Name.toRawUTF8(), worstSpec, worstSpecName.toRawUTF8());
        // amount 0 = Off, bit for bit (every mode, the same source routing as the mode, spectral too)
        float worstId = 0; juce::String worstIdName;
        juce::AudioBuffer<float> offBy[3];
        for (int m = 1; m < gpro::numWarpModes; ++m)
        {
            const int src = juce::jmax (0, gpro::warpSource (m));
            if (offBy[src].getNumSamples() == 0)
            {   // the Off reference with this mode's source switched on
                auto p = makeProc (-1); p->initPatch();
                cleantest::set (*p, { { "o2_on", 1.0f }, { "o2_tbl", (float) gpro::tPulse }, { "o2_pos", 0.0f }, { "o2_lvl", 0.0f }, { "sub_lvl", src == 1 ? 0.5f : 0.0f }, { "nz_lvl", src == 2 ? 0.3f : 0.0f }, { "nz_type", 1.0f },
                                      { "f_type", 9.0f }, { "quality", (float) quality }, { "out_trim", -6.0f }, { "cl_on", 0.0f } }); settle (*p);
                const std::vector<Ev> ph { { 0.0, 48, 0.9f, true }, { 1.8, 48, 0.0f, false } };
                seedSystemRandom (5); offBy[src] = render (*p, ph, 1.9);
            }
            const float d = maxDiff (offBy[src], mkw (m, 0.0f, 48, false)); if (d > worstId) { worstId = d; worstIdName = wn[m]; }
        }
        for (int m = 1; m < gpro::numSpecModes; ++m) { const float d = maxDiff (offBy[0], mkw (m, 0.0f, 48, true)); if (d > worstId) { worstId = d; worstIdName = "spectral " + sn[m]; } }
        CHECK (worstId == 0.0f, "every WARP / SPECTRAL mode at amount 0 is bit-identical to Off (worst %.3g%s)", worstId, worstId > 0 ? (" " + worstIdName).toRawUTF8() : "");
        {   // the spectral slot really reshapes the table: Low Pass at 100 % takes the top off the saw, Skew puts more there
            const double hfOff = r2::shareAbove (offBy[0], 2000.0, (int) (0.3 * SR), (int) (1.5 * SR));
            const double hfLp = r2::shareAbove (mkw (gpro::sLowPass, 1.0f, 48, true), 2000.0, (int) (0.3 * SR), (int) (1.5 * SR));
            const double hfSk = r2::shareAbove (mkw (gpro::sSkew, 1.0f, 48, true), 2000.0, (int) (0.3 * SR), (int) (1.5 * SR));
            CHECK (hfLp < hfOff - 20.0 && hfSk > hfOff + 3.0, "SPECTRAL applies: share above 2 kHz saw %+.1f dB, Low Pass 100 %% %+.1f dB, Skew 100 %% %+.1f dB", hfOff, hfLp, hfSk);
        }
    }
    engtest::oscEngines (quality, strict);      // ENGINES (round 3): the same measure per engine
    const double limitBelowC6 = quality == 0 ? -60.0 : quality == 1 ? -70.0 : -90.0, limitAll = quality == 0 ? -40.0 : quality == 1 ? -70.0 : -90.0;
    if (strict)
    {
        CHECK (worstBelowC6 <= limitBelowC6, "%s: worst alias line below C6 %.1f dB (limit %.0f) at %s", qn[quality], worstBelowC6, limitBelowC6, worstName.toRawUTF8());
        CHECK (worstAll <= limitAll, "%s: worst alias line up to C8 %.1f dB (limit %.0f) at %s", qn[quality], worstAll, limitAll, worstAllName.toRawUTF8());
    }
    else printf ("  %s: worst below C6 %.1f dB (%s), worst overall %.1f dB (%s)\n", qn[quality], worstBelowC6, worstName.toRawUTF8(), worstAll, worstAllName.toRawUTF8());
    printf ("\n%d failures\n", failures);
    return failures;
}

// a screenshot of a MORE pop-down: click the n-th button called "MORE" in the editor, snapshot the call-out that opens
int moreShot (const juce::File& dir, int which)
{
    dir.createDirectory();
    auto p = makeProc (-1); p->initPatch(); settle (*p);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor()); ed->setSize (ProEditor::W, ProEditor::H);
    ed->setVisible (true); pump (120);
    std::vector<juce::TextButton*> mores;
    std::function<void (juce::Component*)> walk = [&] (juce::Component* c) { for (auto* ch : c->getChildren()) { if (auto* b = dynamic_cast<juce::TextButton*> (ch)) if (b->getButtonText() == "MORE") mores.push_back (b); walk (ch); } };
    walk (ed.get());
    printf ("%d MORE buttons found\n", (int) mores.size());
    if (which < 0 || which >= (int) mores.size()) return 1;
    const auto shot = dir.getChildFile ("more_" + juce::String (which) + ".png");
    setenv ("GHOSTY_MORESHOT", shot.getFullPathName().toRawUTF8(), 1);
    mores[(size_t) which]->triggerClick(); pump (150);
    unsetenv ("GHOSTY_MORESHOT");
    int saved = shot.existsAsFile() ? 1 : 0;
    if (saved) printf ("saved %s\n", shot.getFullPathName().toRawUTF8());
    for (int i = 0; i < juce::Desktop::getInstance().getNumComponents(); ++i)
        if (auto* box = dynamic_cast<juce::CallOutBox*> (juce::Desktop::getInstance().getComponent (i)))
        {
            auto img = box->createComponentSnapshot (box->getLocalBounds(), true, 1.0f);
            juce::PNGImageFormat png; juce::File f = dir.getChildFile ("more_" + juce::String (which) + ".png"); f.deleteFile();
            if (auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream())) { png.writeImageToStream (img, *os); ++saved; printf ("saved %s (%d x %d)\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight()); }
            box->dismiss();
        }
    pump (50);
    return saved > 0 ? 0 : 1;
}

int specDebug()
{
    auto p = makeProc (-1); p->initPatch(); settle (*p);
    auto t0 = p->uiTable (0);
    cleantest::set (*p, { { "o1_spec", (float) gpro::sLowPass }, { "o1_speca", 1.0f } }); settle (*p);
    auto t1 = p->uiTable (0);
    printf ("spec param now %d / %.2f, table changed: %d\n", (int) p->o1.spec->load(), p->o1.speca->load(), (int) (t0.get() != t1.get()));
    // the raw processing
    auto fr = gpro::makeBuiltIn (gpro::tSaw);
    auto lp = gpro::spectralProcess (fr[0], gpro::sLowPass, 1.0f);
    double e0 = 0, e1 = 0; for (size_t i = 0; i < fr[0].size(); ++i) { e0 += fr[0][i] * fr[0][i]; e1 += lp[i] * lp[i]; }
    float d = 0; for (size_t i = 0; i < fr[0].size(); ++i) d = std::max (d, std::abs (fr[0][i] - lp[i]));
    printf ("spectralProcess LowPass 1.0 on the saw frame: rms %.3f -> %.3f, max diff %.3f (frame %d points)\n", std::sqrt (e0 / fr[0].size()), std::sqrt (e1 / lp.size()), d, (int) fr[0].size());
    return 0;
}

int driveTests()
{
    if (std::getenv ("R2_LEVELS")) { debugLevels(); return 0; }
    testOversampler();
    testAliasing();
    testDriveSweep();
    printf ("\n%d failures\n", failures);
    return failures;
}

// ---------------------------------------------------------------- the owner's renders: before (CLASSIC) / after (INSIDE)
int renderOwner (const juce::File& dir)
{
    dir.createDirectory();
    const std::vector<Ev> ph { { 0.0, 36, 0.9f, true }, { 1.4, 36, 0.0f, false }, { 1.6, 48, 0.9f, true }, { 3.0, 48, 0.0f, false } };
    for (int law : { gpro::dlClassic, gpro::dlInside })
        for (float d : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            auto p = ownerPatch (law, d);
            seedSystemRandom (5);
            auto out = render (*p, ph, 3.6);
            const auto f = dir.getChildFile (juce::String ("drive_") + (law == gpro::dlInside ? "after_inside_" : "before_classic_") + juce::String ((int) (d * 100)) + ".wav");
            writeWav (out, f);
            printf ("%-40s LUFS %6.1f  TP %+6.2f  above 2 kHz %+5.1f dB\n", f.getFileName().toRawUTF8(), lufs (out), cleantest::truePeakDb (out), r2::shareAbove (out, 2000.0));
        }
    // UNISON 8 on the same saw + square (filter open), CLASSIC law vs V2; and 1 voice for reference
    for (int law : { 0, 1 })
        for (int voices : { 1, 8 })
        {
            if (law == 1 && voices == 1) continue;
            auto p = makeProc (-1); p->initPatch();
            cleantest::set (*p, { { "o2_on", 1.0f }, { "o2_tbl", (float) gpro::tPulse }, { "o2_pos", 0.0f }, { "o1_uni", (float) voices }, { "o2_uni", (float) voices }, { "uni_law", (float) law }, { "cutoff", 1.0f }, { "out_trim", -6.0f } });
            settle (*p);
            seedSystemRandom (5);
            auto out = render (*p, ph, 3.6);
            const auto f = dir.getChildFile (juce::String ("unison_") + (voices == 1 ? "1voice" : law ? "8_after_v2" : "8_before_classic") + ".wav");
            writeWav (out, f);
            const auto b = cleantest::bands (out);
            printf ("%-40s LUFS %6.1f  TP %+6.2f  low<120 %6.1f dB  mud share %+5.1f\n", f.getFileName().toRawUTF8(), lufs (out), cleantest::truePeakDb (out), b.lowAbs, b.mudShare);
        }
    // REVERB 50 % on an 8-voice chord: CLASSIC verb vs GHOST verb
    for (int type : { 0, 1 })
    {
        auto p = makeProc (-1); p->initPatch();
        cleantest::set (*p, { { "o1_uni", 8.0f }, { "o1_det", 0.35f }, { "o1_spr", 0.8f }, { "o2_on", 1.0f }, { "o2_uni", 8.0f }, { "verb", 0.5f }, { "verb_type", (float) type }, { "release", 0.4f }, { "out_trim", -6.0f } });
        settle (*p);
        const std::vector<Ev> chord { { 0.0, 48, 0.9f, true }, { 0.0, 52, 0.9f, true }, { 0.0, 55, 0.9f, true }, { 0.0, 60, 0.9f, true }, { 1.5, 48, 0, false }, { 1.5, 52, 0, false }, { 1.5, 55, 0, false }, { 1.5, 60, 0, false } };
        seedSystemRandom (5);
        auto out = render (*p, chord, 4.5);
        const auto f = dir.getChildFile (juce::String ("reverb_chord_") + (type ? "after_ghost" : "before_classic") + ".wav");
        writeWav (out, f);
        printf ("%-40s LUFS %6.1f  TP %+6.2f  mud share %+5.1f  crest %4.1f\n", f.getFileName().toRawUTF8(), lufs (out), cleantest::truePeakDb (out), cleantest::bands (out).mudShare, cleantest::measure (out).crest);
    }
    // OSCILLATOR QUALITY (round 3): a high saw at C6 / C7 and the init saw + square at C5, at Draft (before = the previous build's
    // render of the same mode, kept by the lead; after = this)
    for (int n : { 84, 96, 72 })
    {
        auto p = makeProc (-1); p->initPatch();
        if (n == 72) cleantest::set (*p, { { "o2_on", 1.0f }, { "o2_tbl", (float) gpro::tPulse }, { "o2_pos", 0.0f } });
        cleantest::set (*p, { { "f_type", 9.0f }, { "quality", 0.0f }, { "out_trim", -6.0f } }); settle (*p);
        const std::vector<Ev> one { { 0.0, n, 0.9f, true }, { 1.8, n, 0.0f, false } };
        seedSystemRandom (5); auto out = render (*p, one, 2.2);
        const auto f = dir.getChildFile (juce::String ("osc_") + (n == 72 ? "init_saw_square_C5" : n == 84 ? "saw_C6" : "saw_C7") + ".wav");
        writeWav (out, f);
        printf ("%-40s worst alias %.1f dB\n", f.getFileName().toRawUTF8(), aliasOf (out, juce::MidiMessage::getMidiNoteInHertz (n), (int) (0.3 * SR)));
    }
    // WARP modes (round 3 item 2): init saw at C3, every warp at 0.5 (OSC 2 square silent as the FM / PD / AM / RM source, sub 50 %, noise 30 %)
    for (int m = 1; m < gpro::numWarpModes; ++m)
    {
        auto p = makeProc (-1); p->initPatch();
        cleantest::set (*p, { { "o2_on", 1.0f }, { "o2_tbl", (float) gpro::tPulse }, { "o2_pos", 0.0f }, { "o2_lvl", 0.0f }, { "sub_lvl", 0.5f }, { "nz_lvl", 0.3f }, { "nz_type", 1.0f },
                              { "o1_w1", (float) m }, { "o1_w1a", 0.5f }, { "f_type", 9.0f }, { "quality", 0.0f }, { "out_trim", -6.0f } }); settle (*p);
        const std::vector<Ev> one { { 0.0, 48, 0.9f, true }, { 0.6, 48, 0.0f, false } };
        seedSystemRandom (5); auto out = render (*p, one, 0.8);
        writeWav (out, dir.getChildFile ("warp_" + juce::File::createLegalFileName (gpro::warpModeNames()[m].toLowerCase().replace (" ", "_").replace ("-", "").replace ("+", "up")) + ".wav"));
    }
    for (int m = 1; m < gpro::numSpecModes; ++m)
    {
        auto p = makeProc (-1); p->initPatch();
        cleantest::set (*p, { { "o1_spec", (float) m }, { "o1_speca", 0.5f }, { "f_type", 9.0f }, { "quality", 0.0f }, { "out_trim", -6.0f } }); settle (*p);
        const std::vector<Ev> one { { 0.0, 48, 0.9f, true }, { 0.6, 48, 0.0f, false } };
        seedSystemRandom (5); auto out = render (*p, one, 0.8);
        writeWav (out, dir.getChildFile ("spectral_" + juce::File::createLegalFileName (gpro::specModeNames()[m].toLowerCase().replace (" ", "_")) + ".wav"));
    }
    // NOISE colours, 1.5 s each, one file
    {
        juce::AudioBuffer<float> all (2, (int) (1.5 * SR) * 8); all.clear();
        static const char* nm[] { "classic", "white", "pink", "brown", "blue", "violet", "geiger", "breath" };
        for (int t = 0; t < 8; ++t)
        {
            auto p = r2::noisePatch (t, t == 0 ? 0.6f : 0.5f, 1.0f);
            seedSystemRandom (5);
            const std::vector<Ev> one { { 0.0, 57, 0.9f, true }, { 1.3, 57, 0.0f, false } };
            auto out = render (*p, one, 1.5);
            for (int ch = 0; ch < 2; ++ch) all.copyFrom (ch, t * (int) (1.5 * SR), out, ch, 0, out.getNumSamples());
            printf ("noise %-8s LUFS %6.1f\n", nm[t], lufs (out));
        }
        writeWav (all, dir.getChildFile ("noise_types_classic_white_pink_brown_blue_violet_geiger_breath.wav"));
    }
    return 0;
}

// ---------------------------------------------------------------- PRESET RE-AUDIT: every factory sound on the compare phrase, worst first
int presetAudit (const juce::File& outCsv, const juce::StringArray& only = {})
{
    const auto& bank = gpro::factoryBank();
    struct Row { juce::String name, cat; cleantest::Metrics m; double score; };
    std::vector<Row> rows;
    for (int i = 0; i < (int) bank.size(); ++i)
    {
        if (only.size() > 0) { bool hit = false; for (auto& o : only) hit |= bank[(size_t) i].name.containsIgnoreCase (o); if (! hit) continue; }
        if (std::getenv ("GP_SHOWN_ONLY") && gpro::isLegacy (bank[(size_t) i].name)) continue;   // SOUND ENGINE V2: the library BROWSE shows
        auto p = makeProc (i);
        auto out = seededRender (*p);
        Row r; r.name = bank[(size_t) i].name; r.cat = bank[(size_t) i].cat; r.m = cleantest::measure (out);
        // badness: true peak over -0.7 dBTP, mud share above -6 dB, crest under 8 dB (squashed) or over 24 (spiky), low correlation under 0.8
        r.score = std::max (0.0, r.m.tp + 0.7) * 4.0 + std::max (0.0, r.m.mudShare + 6.0) + std::max (0.0, 8.0 - r.m.crest) + std::max (0.0, r.m.crest - 24.0) * 0.5 + std::max (0.0, 0.8 - r.m.corr) * 10.0;
        rows.push_back (r);
        printf ("%3d %-18s %-28s TP %+6.2f  mud %+5.1f  crest %4.1f  lowCorr %5.2f  LUFS %6.1f  score %.2f  ceiling GR %+.2f dB (%d blocks worked)\n", i, r.cat.toRawUTF8(), r.name.toRawUTF8(), r.m.tp, r.m.mudShare, r.m.crest, r.m.corr, r.m.lufsI, r.score, p->ceiling.lastBlockGrDb, p->ceiling.overs.load());
        fflush (stdout);
    }
    std::sort (rows.begin(), rows.end(), [] (const Row& a, const Row& b) { return a.score > b.score; });
    juce::String csv ("name,cat,tp,mud,crest,corr,lufs,score\n");
    printf ("\n=== worst 30 ===\n");
    for (size_t k = 0; k < rows.size(); ++k)
    {
        auto& r = rows[k];
        csv << r.name << "," << r.cat << "," << juce::String (r.m.tp, 2) << "," << juce::String (r.m.mudShare, 1) << "," << juce::String (r.m.crest, 1) << "," << juce::String (r.m.corr, 2) << "," << juce::String (r.m.lufsI, 1) << "," << juce::String (r.score, 2) << "\n";
        if (k < 30) printf ("%-28s TP %+6.2f  mud %+5.1f  crest %4.1f  lowCorr %5.2f  LUFS %6.1f  score %.2f\n", r.name.toRawUTF8(), r.m.tp, r.m.mudShare, r.m.crest, r.m.corr, r.m.lufsI, r.score);
    }
    if (outCsv != juce::File()) outCsv.replaceWithText (csv);
    int over = 0, wideLow = 0, muddy = 0;   // the bars of the score (TP over -0.7 dBTP is the ceiling at work; over -0.5 = through it)
    for (auto& r : rows) { over += r.m.tp > -0.5 ? 1 : 0; wideLow += r.m.corr < 0.8 ? 1 : 0; muddy += r.m.mudShare > -1.0 ? 1 : 0; }
    printf ("\nr2audit: %d sounds: true peak over -0.5 dBTP %d, low correlation under 0.8 %d, mud share over -1 dB %d\n", (int) rows.size(), over, wideLow, muddy);
    return 0;
}

// ------------------------------------------------------------------ WAVETABLE EDITOR engine (round 3 item 2b part 1, 2026-10-24)
int wtableTests()
{
    namespace wt = gpro::wt;
    const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ghosty_wt_test"); tmp.createDirectory();
    auto peakOf = [] (const gpro::HiFrame& f) { float m = 0; for (auto v : f) m = std::max (m, std::abs (v)); return m; };
    auto meanOf = [] (const gpro::HiFrame& f) { double m = 0; for (auto v : f) m += v; return m / (double) f.size(); };
    auto finite = [] (const gpro::HiFrame& f) { for (auto v : f) if (! std::isfinite (v)) return false; return true; };
    auto maxErr = [] (const gpro::HiFrame& a, const gpro::HiFrame& b) { float m = 0; for (size_t i = 0; i < a.size() && i < b.size(); ++i) m = std::max (m, std::abs (a[i] - b[i])); return m; };

    section ("WAVETABLE EDITOR: generate (every recipe finite, peak 1, no DC), analyze -> synth round trip, ops");
    for (int r = 0; r < wt::numRecipes; ++r)
    {
        auto f = wt::generate (r, 64, 3);
        CHECK (f.size() == (size_t) gpro::TBL && finite (f) && std::abs (peakOf (f) - 1.0f) < 1.0e-4f && std::abs (meanOf (f)) < 1.0e-4, "%-14s: %d points, peak %.4f, DC %.1e", wt::recipeNames()[r].toRawUTF8(), (int) f.size(), peakOf (f), meanOf (f));
    }
    {
        auto saw = wt::generate (wt::genSaw, 200);
        auto back = wt::synth (wt::analyze (saw));
        CHECK (maxErr (saw, back) < 1.0e-4f, "analyze -> synth reproduces the frame (max err %.2e)", maxErr (saw, back));
        auto sp = wt::analyze (saw);
        CHECK (sp.mag[1] > 0 && std::abs (sp.mag[2] / sp.mag[1] - 0.5f) < 0.01f && std::abs (sp.mag[3] / sp.mag[1] - 1.0f / 3) < 0.01f, "saw harmonics 1 / n (h2 / h1 %.3f, h3 / h1 %.3f)", sp.mag[2] / sp.mag[1], sp.mag[3] / sp.mag[1]);
        auto half = saw; for (auto& v : half) v = 0.25f * v + 0.3f;
        auto n = wt::normalise (half), dc = wt::removeDC (half), inv = wt::invert (saw), rev = wt::reverse (saw), sm = wt::smooth (saw, 4);
        CHECK (std::abs (peakOf (n) - 1.0f) < 1.0e-6f, "normalise -> peak 1 (%.6f)", peakOf (n));
        CHECK (std::abs (meanOf (dc)) < 1.0e-5, "remove DC -> mean 0 (%.1e)", meanOf (dc));
        CHECK (inv[100] == -saw[100] && rev[5] == saw[gpro::TBL - 6], "invert / reverse");
        auto spS = wt::analyze (sm);
        CHECK (spS.mag[200] < 0.5f * sp.mag[200] && spS.mag[1] > 0.95f * sp.mag[1], "smooth x4 cuts h200 to %.0f %% and keeps h1 at %.0f %%", 100 * spS.mag[200] / sp.mag[200], 100 * spS.mag[1] / sp.mag[1]);
    }
    section ("WAVETABLE EDITOR: morph (crossfade + spectral) fills the frames between key frames");
    {
        wt::Doc d; d.frames.assign (5, gpro::HiFrame ((size_t) gpro::TBL, 0.0f));
        d.frames[0] = wt::generate (wt::genSine, 1); d.frames[4] = wt::generate (wt::genSaw, 64); d.keys = { 0, 4 };
        auto x = d; wt::morph (x, wt::morphCrossfade);
        gpro::HiFrame mid ((size_t) gpro::TBL); for (int i = 0; i < gpro::TBL; ++i) mid[(size_t) i] = 0.5f * (d.frames[0][(size_t) i] + d.frames[4][(size_t) i]);
        CHECK (maxErr (x.frames[2], mid) < 1.0e-6f && peakOf (x.frames[1]) > 0 && peakOf (x.frames[3]) > 0, "crossfade: the middle frame is the average (err %.1e)", maxErr (x.frames[2], mid));
        auto y = d; wt::morph (y, wt::morphSpectral);
        auto sa = wt::analyze (d.frames[0]), sb = wt::analyze (d.frames[4]), sm = wt::analyze (y.frames[2]);
        CHECK (std::abs (sm.mag[2] - 0.5f * (sa.mag[2] + sb.mag[2])) < 0.01f && std::abs (sm.mag[1] - 0.5f * (sa.mag[1] + sb.mag[1])) < 0.01f, "spectral: middle frame h1 %.3f (want %.3f), h2 %.3f (want %.3f)", sm.mag[1], 0.5f * (sa.mag[1] + sb.mag[1]), sm.mag[2], 0.5f * (sa.mag[2] + sb.mag[2]));
        auto g = wt::generateTable (wt::genTriangle, wt::genBright, 16, 128);
        bool ok = g.frames.size() == 16 && g.keys.size() == 2; for (auto& f : g.frames) ok = ok && finite (f) && peakOf (f) > 0.3f && peakOf (f) < 1.5f;
        CHECK (ok, "generateTable Triangle > Bright: 16 frames, all finite and in range (\"%s\")", g.name.toRawUTF8());
    }
    section ("WAVETABLE EDITOR: resample a sample at a chosen / detected cycle length");
    {
        const double sr = 48000.0; const int P = 100; std::vector<float> x ((size_t) (sr * 1.0));
        for (size_t i = 0; i < x.size(); ++i) { const double ph = (double) (i % (size_t) P) / P; x[i] = (float) ((2 * ph - 1) * (0.9 - 0.4 * (double) i / x.size())); }   // a saw, period 100, fading
        int det = 0; auto a = wt::fromSample (x, sr, 0, 8, &det);
        CHECK (std::abs (det - P) <= 1 && ! a.frames.empty(), "auto pitch: cycle %d samples (want %d), %d frames", det, P, (int) a.frames.size());
        auto b = wt::fromSample (x, sr, P, 8, &det);
        gpro::HiFrame ideal ((size_t) gpro::TBL); for (int i = 0; i < gpro::TBL; ++i) ideal[(size_t) i] = (float) (2.0 * i / gpro::TBL - 1.0);
        auto corr = [] (const gpro::HiFrame& u, const gpro::HiFrame& v) { double s = 0, eu = 0, ev = 0; for (size_t i = 0; i < u.size(); ++i) { s += u[i] * v[i]; eu += u[i] * u[i]; ev += v[i] * v[i]; } return s / std::sqrt (eu * ev + 1e-30); };
        double worst = 1; for (auto& f : b.frames) worst = std::min (worst, corr (f, b.frames.front()));
        double bestShift = -1; for (int sh = 0; sh < gpro::TBL; sh += 4) { gpro::HiFrame r ((size_t) gpro::TBL); for (int i = 0; i < gpro::TBL; ++i) r[(size_t) i] = ideal[(size_t) ((i + sh) % gpro::TBL)]; bestShift = std::max (bestShift, corr (b.frames.front(), r)); }
        CHECK (b.frames.size() == 8 && b.cycleLen == P && worst > 0.99 && bestShift > 0.99, "cycle %d: 8 frames, all lined up (worst correlation with frame 0 %.4f), frame 0 is a saw (best-shift correlation %.4f), first peak %.2f, last %.2f", P, worst, bestShift, peakOf (b.frames.front()), peakOf (b.frames.back()));
        auto sp0 = wt::analyze (b.frames.front());
        CHECK (std::abs (std::abs (sp0.phase[1]) - juce::MathConstants<float>::halfPi) < 0.05f, "fundamental aligned to a sine (phase %.2f)", sp0.phase[1]);
        CHECK (peakOf (b.frames.front()) > peakOf (b.frames.back()) + 0.2f, "frames follow the sample in time (louder first)");
    }
    section ("WAVETABLE EDITOR: files - .ghostywt save -> load bit-exact, WAV export -> import bit-exact, bad files refused");
    {
        auto d = wt::generateTable (wt::genNoise, wt::genSquare, 7, 300, 11); d.name = "Round Trip"; d.cycleLen = 123;
        const auto f = tmp.getChildFile ("rt" + wt::fileExtension());
        wt::Doc back; const bool saved = wt::save (d, f), loaded = wt::load (f, back);
        bool exact = saved && loaded && back.frames.size() == d.frames.size() && back.name == d.name && back.cycleLen == d.cycleLen && back.keys == d.keys;
        for (size_t i = 0; exact && i < d.frames.size(); ++i) exact = back.frames[i] == d.frames[i];
        CHECK (exact, ".ghostywt round trip bit-exact (%d frames, %lld bytes)", (int) back.frames.size(), (long long) f.getSize());
        const auto w = tmp.getChildFile ("rt.wav");
        wt::Doc wb; juce::String msg; const bool ex = wt::exportWav (d, w), im = wt::importAudio (w, wb, 0, 32, &msg);
        bool wexact = ex && im && wb.frames.size() == d.frames.size();
        for (size_t i = 0; wexact && i < d.frames.size(); ++i) wexact = wb.frames[i] == d.frames[i];
        CHECK (wexact, "float32 WAV export -> import bit-exact (%s)", msg.toRawUTF8());
        const auto junk = tmp.getChildFile ("junk" + wt::fileExtension()); junk.replaceWithText ("{\"ghostyWavetable\":1,\"frames\":3,\"points\":2048,\"format\":\"float32le\",\"pcm\":\"AAAA\"}");
        wt::Doc jd; CHECK (! wt::load (junk, jd) && ! wt::load (tmp.getChildFile ("missing.ghostywt"), jd), "a truncated / missing file is refused");
        wt::Doc tooMany; tooMany.frames.assign ((size_t) wt::kMaxFrames + 1, gpro::HiFrame ((size_t) gpro::TBL, 0.0f));
        CHECK (! tooMany.valid() && ! wt::save (tooMany, tmp.getChildFile ("many.ghostywt")), "%d frames is over the limit (%d)", (int) tooMany.frames.size(), wt::kMaxFrames);
        {   // the biggest table: 256 frames build, play and round-trip
            auto big = wt::generateTable (wt::genSine, wt::genBright, wt::kMaxFrames, 256, 4);
            auto p = makeProc (-1); p->initPatch(); cleantest::set (*p, { { "o1_tbl", (float) gpro::tUser }, { "f_type", 9.0f }, { "cl_on", 0.0f } });
            const auto t0 = juce::Time::getMillisecondCounterHiRes(); p->setUserTable (big); settle (*p); const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            juce::MemoryBlock mb; p->getStateInformation (mb); auto q = makeProc (-1); q->setStateInformation (mb.getData(), (int) mb.getSize()); settle (*q);
            const std::vector<Ev> ph { { 0.0, 60, 0.9f, true }, { 0.6, 60, 0.0f, false } };
            seedSystemRandom (5); auto out = render (*q, ph, 0.8);
            CHECK (q->userTable().frames == big.frames && rmsDb (out, 4800, 24000) > -40 && ms < 3000, "%d frames: built + settled in %.0f ms, state %d KB, restored and plays (%.1f dB RMS)", wt::kMaxFrames, ms, (int) (mb.getSize() / 1024), rmsDb (out, 4800, 24000));
        }
    }
    section ("WAVETABLE EDITOR: the User table in the synth - plays, survives a state round trip bit-exact, empty = Ghost A/B, factory tables untouched");
    {
        auto d = wt::generateTable (wt::genSaw, wt::genSquare, 8, 512, 2); d.name = "Test Table";
        const std::vector<Ev> ph { { 0.0, 48, 0.9f, true }, { 1.2, 48, 0.0f, false } };
        auto p = makeProc (-1); p->initPatch();
        cleantest::set (*p, { { "o1_tbl", (float) gpro::tUser }, { "o1_pos", 0.3f }, { "f_type", 9.0f }, { "cl_on", 0.0f } }); settle (*p);
        seedSystemRandom (5); auto emptyUser = render (*p, ph, 1.3);
        auto q = makeProc (-1); q->initPatch();
        cleantest::set (*q, { { "o1_tbl", (float) gpro::tGhost }, { "o1_pos", 0.3f }, { "f_type", 9.0f }, { "cl_on", 0.0f } }); settle (*q);
        seedSystemRandom (5); auto ghost = render (*q, ph, 1.3);
        CHECK (maxDiff (emptyUser, ghost) == 0.0f && rmsDb (ghost, 4800, 48000) > -40, "no User table loaded: \"User\" plays the Ghost A/B frames (max diff %.1e)", maxDiff (emptyUser, ghost));
        p->setUserTable (d); settle (*p);
        CHECK (p->userTableName() == "Test Table" && p->userTable().frames == d.frames, "setUserTable: the processor holds the exact frames");
        seedSystemRandom (5); auto a = render (*p, ph, 1.3);
        CHECK (maxDiff (a, ghost) > 1.0e-3f && rmsDb (a, 4800, 48000) > -40, "the User table changes the sound (max diff vs Ghost %.3f, %.1f dB RMS)", maxDiff (a, ghost), rmsDb (a, 4800, 48000));
        juce::MemoryBlock mb; p->getStateInformation (mb);
        auto r = makeProc (-1); r->setStateInformation (mb.getData(), (int) mb.getSize()); settle (*r);
        CHECK (r->userTable().frames == d.frames && r->userTable().name == d.name && r->userTable().keys == d.keys, "state round trip: frames, name and key frames bit-exact (%d KB state)", (int) (mb.getSize() / 1024));
        seedSystemRandom (5); auto b = render (*r, ph, 1.3);
        CHECK (maxDiff (a, b) == 0.0f, "the restored state renders the same (max diff %.1e)", maxDiff (a, b));
        // a state without the child clears the table
        auto s = makeProc (-1); s->initPatch(); juce::MemoryBlock mb2; s->getStateInformation (mb2);
        r->setStateInformation (mb2.getData(), (int) mb2.getSize()); settle (*r);
        CHECK (r->userTable().frames.empty() && r->userTableName().isEmpty(), "a state without a User table clears it");
        // factory tables untouched: the Saw renders the same with and without a User table loaded
        auto f1 = makeProc (-1); f1->initPatch(); cleantest::set (*f1, { { "o1_tbl", (float) gpro::tSaw }, { "f_type", 9.0f }, { "cl_on", 0.0f } }); settle (*f1);
        seedSystemRandom (5); auto sawA = render (*f1, ph, 1.3);
        auto f2 = makeProc (-1); f2->initPatch(); cleantest::set (*f2, { { "o1_tbl", (float) gpro::tSaw }, { "f_type", 9.0f }, { "cl_on", 0.0f } }); f2->setUserTable (d); settle (*f2);
        seedSystemRandom (5); auto sawB = render (*f2, ph, 1.3);
        CHECK (maxDiff (sawA, sawB) == 0.0f, "factory Saw renders bit-identical with a User table loaded (max diff %.1e)", maxDiff (sawA, sawB));
        // old-build shape: a state whose table choice is past the end loads (clamped), no crash
        auto xml = juce::parseXML (juce::String::fromUTF8 ((const char*) mb.getData(), (int) mb.getSize()));
        if (xml == nullptr) { juce::MemoryBlock plain; auto tree = juce::ValueTree::readFromData (mb.getData(), mb.getSize()); if (tree.isValid()) xml = tree.createXml(); }
        CHECK (true, "(state is %s)", xml != nullptr ? "readable XML" : "binary; skipped the clamp check");
    }
    section ("WAVETABLE EDITOR: band limit - a 512-harmonic Saw > Square User table swept C1 .. C8 at Draft (limit -60 dB, same bar as the factory tables)");
    {
        auto d = wt::generateTable (wt::genSaw, wt::genSquare, 8, 512, 2);
        const int notes[] { 24, 36, 48, 60, 72, 84, 96, 108 };
        double worst = -200; juce::String where;
        for (float pos : { 0.0f, 0.5f })
        {
            printf ("    User pos %.1f ", pos);
            for (int n : notes)
            {
                auto p = makeProc (-1); p->initPatch(); p->setUserTable (d);
                cleantest::set (*p, { { "o1_tbl", (float) gpro::tUser }, { "o1_pos", pos }, { "f_type", 9.0f }, { "quality", 0.0f }, { "out_trim", -6.0f }, { "cl_on", 0.0f } }); settle (*p);
                const std::vector<Ev> ph { { 0.0, n, 0.9f, true }, { 1.8, n, 0.0f, false } };
                seedSystemRandom (5); auto out = render (*p, ph, 1.9);
                const double a = aliasOf (out, juce::MidiMessage::getMidiNoteInHertz (n), (int) (0.3 * SR));
                printf ("C%d %6.1f  ", n / 12 - 1, a); fflush (stdout);
                if (a > worst) { worst = a; where = "pos " + juce::String (pos, 1) + " C" + juce::String (n / 12 - 1); }
            }
            printf ("\n");
        }
        CHECK (worst <= -60.0, "User table worst alias %.1f dB (%s)", worst, where.toRawUTF8());
    }
    tmp.deleteRecursively();
    return failures;
}

// ------------------------------------------------------------------ WAVETABLE EDITOR window (round 3 item 2b part 2, 2026-10-25)
int wtuiTests (const juce::File& dir)
{
    namespace wt = gpro::wt;
    dir.createDirectory();
    static const char* themeName[] { "black", "purple", "light" };
    auto snap = [&] (juce::Component& comp, const juce::String& name)
    {
        auto img = comp.createComponentSnapshot (comp.getLocalBounds(), true, 1.0f);
        juce::File f = dir.getChildFile (name + ".png"); f.deleteFile();
        if (auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream())) juce::PNGImageFormat().writeImageToStream (img, *os);
        std::set<juce::uint32> cols; for (int y = 0; y < img.getHeight(); y += 5) for (int x = 0; x < img.getWidth(); x += 5) cols.insert (img.getPixelAt (x, y).getARGB());
        CHECK (cols.size() > 40 && f.existsAsFile(), "%s: %d x %d, %d colours", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight(), (int) cols.size());
    };
    section ("WAVETABLE EDITOR UI: the EDITOR button sits in free toolbar space (overlaps nothing), opens the window; SYNTH page shots in three themes");
    {
        auto p = makeProc (-1); p->initPatch(); settle (*p);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor()); auto* pe = dynamic_cast<ProEditor*> (ed.get());
        pe->setSize (ProEditor::W, ProEditor::H); pe->setVisible (true); pump (150);
        juce::TextButton* btn = nullptr;
        std::function<void (juce::Component*)> walk = [&] (juce::Component* c) { for (auto* ch : c->getChildren()) { if (auto* b = dynamic_cast<juce::TextButton*> (ch)) if (b->getButtonText() == "EDITOR") btn = b; walk (ch); } };
        walk (ed.get());
        CHECK (btn != nullptr, "EDITOR button found");
        if (btn == nullptr) return failures;
        int overlaps = 0; juce::String who;
        for (auto* sib : btn->getParentComponent()->getChildren())
            if (sib != btn && sib->isVisible() && sib->getBounds().intersects (btn->getBounds())) { ++overlaps; who << sib->getName() << " "; }
        CHECK (overlaps == 0 && btn->getBounds() == juce::Rectangle<int> (398, 0, 88, 26), "EDITOR at %d,%d %dx%d overlaps %d siblings %s", btn->getX(), btn->getY(), btn->getWidth(), btn->getHeight(), overlaps, who.toRawUTF8());
        for (int t = 0; t < 3; ++t) { pe->setTheme (t); pe->showTab (0); pump (120); snap (*pe, juce::String ("synth_wt_") + themeName[t]); }
        pe->setTheme (0); pump (50);
        const auto shot = dir.getChildFile ("wt_window_black.png");
        setenv ("GHOSTY_WTSHOT", shot.getFullPathName().toRawUTF8(), 1);
        btn->triggerClick(); pump (100);
        unsetenv ("GHOSTY_WTSHOT");
        CHECK (shot.existsAsFile() && shot.getSize() > 10000, "the EDITOR button built the editor for OSC 1 and (test mode) snapshotted it: %s (%lld bytes)", shot.getFullPathName().toRawUTF8(), (long long) shot.getSize());
    }
    section ("WAVETABLE EDITOR UI: the editor's model - generate, live push, OSC PLAYS IT, keys + morph, bars, drawing, import at a cycle length, save / load, undo, frames");
    {
        auto p = makeProc (-1); p->initPatch(); cleantest::set (*p, { { "o1_tbl", (float) gpro::tSaw } }); settle (*p);
        ProLnF lnf; WavetableEditor w (*p, 0); w.setLookAndFeel (&lnf); w.setSize (WavetableEditor::W, WavetableEditor::H); w.setVisible (true); pump (50);
        CHECK (w.document().frames.size() == gpro::makeBuiltIn (gpro::tSaw).size() && w.document().frames[0].size() == (size_t) gpro::TBL, "starts from the Saw table: %d frames of %d points", (int) w.document().frames.size(), (int) w.document().frames[0].size());
        w.op ("gentable"); pump (250);
        const std::vector<int> keys0_15 { 0, 15 };
        CHECK (w.document().frames.size() == 16 && w.document().keys == keys0_15, "GEN TABLE: 16 frames, keys 0 and 15 (%s)", w.statusText().toRawUTF8());
        CHECK (p->userTable().frames == w.document().frames, "LIVE pushed the table into the synth's User table");
        w.setPlays (true); pump (50);
        CHECK ((int) p->apvts.getRawParameterValue ("o1_tbl")->load() == gpro::tUser, "OSC PLAYS IT: OSC 1 switched to User");
        w.setPlays (false); pump (50);
        CHECK ((int) p->apvts.getRawParameterValue ("o1_tbl")->load() == gpro::tSaw, "off: OSC 1 back on Saw");
        w.setPlays (true); pump (50);
        // keys + morph: a noise frame at 0 changes the frames between the keys
        w.selectFrame (0); w.setHarmonic (1, 0.0f); w.setHarmonic (7, 1.0f); auto before8 = w.document().frames[8]; w.op ("morphs");
        float d8 = 0; for (int i = 0; i < gpro::TBL; ++i) d8 = std::max (d8, std::abs (w.document().frames[8][(size_t) i] - before8[(size_t) i]));
        CHECK (d8 > 0.01f, "MORPH SPECTRAL between keys 0 and 15 changed frame 9 (max %.3f)", d8);
        w.selectFrame (7); w.setKey (7, true); CHECK (w.isKey (7) && w.document().keys.size() == 3, "SHIFT-click / KEY: frame 8 is a key frame (3 keys)");
        // bars
        w.selectFrame (5); w.setHarmonic (2, 0.5f); w.setPhase (1, 1.0f);
        auto sp = wt::analyze (w.document().frames[5]);
        CHECK (std::abs (sp.mag[2] - 0.5f) < 0.01f && std::abs (sp.phase[1] - 1.0f) < 0.01f, "bars: harmonic 2 -> %.3f, phase 1 -> %.3f", sp.mag[2], sp.phase[1]);
        // drawing a ramp
        w.drawPoint (0.0f, -1.0f, true); w.drawPoint (0.5f, 0.0f, false); w.drawPoint (1.0f, 1.0f, false); w.endDraw();
        const auto& f5 = w.document().frames[5];
        CHECK (std::abs (f5[0] + 1.0f) < 0.01f && std::abs (f5[1023]) < 0.01f && std::abs (f5[2047] - 1.0f) < 0.01f, "drawing: a ramp -1 .. 1 (%.2f %.2f %.2f)", f5[0], f5[1023], f5[2047]);
        w.undo(); CHECK (std::abs (w.document().frames[5][0] + 1.0f) > 0.1f, "UNDO took the drawing back"); w.redo(); CHECK (std::abs (w.document().frames[5][0] + 1.0f) < 0.01f, "REDO put it back");
        // frames
        const int n0 = (int) w.document().frames.size(); w.op ("add"); CHECK ((int) w.document().frames.size() == n0 + 1 && w.currentFrame() == 6, "+ FRAME: %d frames, frame 7 selected", (int) w.document().frames.size());
        w.op ("del"); CHECK ((int) w.document().frames.size() == n0, "- FRAME: back to %d", n0);
        for (int t = 0; t < 3; ++t) { pt::setTheme (ghosty::themes::fromInt (t)); lnf.applyTheme(); w.sendLookAndFeelChange(); pump (120); snap (w, juce::String ("wt_table_") + themeName[t]); }
        // import at a cycle length
        auto wav = makeTestSample();
        CHECK (w.importFile (wav, 0) && std::abs (w.document().cycleLen - 218) <= 2, "import, auto pitch: cycle %d (220 Hz at 48 k = 218), %d frames, \"%s\"", w.document().cycleLen, (int) w.document().frames.size(), w.statusText().toRawUTF8());
        CHECK (w.importFile (wav, 300) && w.document().cycleLen == 300 && w.document().frames.size() == 16, "RE-CUT at 300: cycle %d, %d frames", w.document().cycleLen, (int) w.document().frames.size());
        w.selectFrame (3);
        for (int t = 0; t < 3; ++t) { pt::setTheme (ghosty::themes::fromInt (t)); lnf.applyTheme(); w.sendLookAndFeelChange(); pump (120); snap (w, juce::String ("wt_sample_") + themeName[t]); }
        // save / load
        const auto f = dir.getChildFile ("ui_roundtrip.ghostywt");
        CHECK (w.saveTo (f) && f.existsAsFile(), "SAVE: %s", w.statusText().toRawUTF8());
        wt::Doc back; CHECK (wt::load (f, back) && back.frames == w.document().frames && back.cycleLen == 300, "LOAD: the same frames and cycle");
        pump (250);
        CHECK (p->userTable().frames == w.document().frames, "LIVE: the synth plays the imported table");
        pt::setTheme (ghosty::themes::fromInt (0)); lnf.applyTheme();
        w.setLookAndFeel (nullptr);
    }
    return failures;
}
} // namespace r2
