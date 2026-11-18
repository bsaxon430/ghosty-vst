#pragma once
// YOUR SAMPLE checks: root detection, pitched playback, aliasing, loops, ADSR, stereo, voices / CPU, glide / fatten / step,
// one-shot / reverse / start, the lock-free swap, drag-out, state, TRANSPOSE / BYPASS / ON NOTES, sounds per lane.
// Included by ArpTests.cpp after SoundTests.h (uses soundtest::fft / lufs, render, setP, FakeHead, Counter).

namespace smptest
{
using namespace arp;
constexpr double kSr = 48000.0;
inline std::vector<float> tone (double hz, double sec, int harmonics = 1, double sr = kSr, double amp = 0.5)
{
    std::vector<float> x ((size_t) (sec * sr), 0.0f);
    for (int k = 1; k <= harmonics; ++k)
    {
        if (hz * k > sr * 0.45) break;
        const double a = amp / k;
        for (size_t i = 0; i < x.size(); ++i) x[i] += (float) (a * std::sin (2 * 3.14159265358979 * hz * k * (double) i / sr));
    }
    return x;
}
inline std::unique_ptr<arpsmp::SampleData> mk (std::vector<float> L, std::vector<float> R = {}, double sr = kSr)
{
    std::vector<float> x[2] = { std::move (L), std::move (R) }; const int ch = x[1].empty() ? 1 : 2;
    return arpsmp::build (x, ch, sr);
}
struct Ev { double t; int type; float pitch; double len; int chain; float fat; };   // type 0 start, 1 legato, 2 glide (len = glide time)
struct Out { std::vector<float> L, R; };
inline Out play (const arpsmp::SampleData* d, const arpsmp::Params& p, std::vector<Ev> evs, double sec, int lane = 2, double sr = kSr)
{
    arpsyn::Engine e; e.prepare (sr, 256);
    arpsyn::Params P; P.delay = 0; P.reverb = 0; P.pump = 0; P.lfoDepth = 0; P.volume = 0.6f; P.kick = false; P.sampleMask = 1 << lane;
    e.setSample (d, p);
    Out o; const int total = (int) (sec * sr); o.L.assign ((size_t) total, 0.0f); o.R.assign ((size_t) total, 0.0f);
    size_t ei = 0;
    std::stable_sort (evs.begin(), evs.end(), [] (const Ev& a, const Ev& b) { return a.t < b.t; });
    for (int pos = 0; pos < total; pos += 256)
    {
        const int n = std::min (256, total - pos);
        while (ei < evs.size() && evs[ei].t * sr < pos + n)
        {
            const auto& v = evs[ei++]; const int len = (int) (v.len * sr);
            if (v.type == 0) e.leadStart (lane, v.chain, v.pitch, 0.9f, len, P, -1.0f, v.fat);
            else if (v.type == 1) e.leadLegato (v.chain, v.pitch, 0.9f, v.fat);
            else e.leadGlide (v.chain, v.pitch, len);
        }
        e.render (o.L.data() + pos, o.R.data() + pos, n, P, 120, 0, 0.0, false);
    }
    return o;
}
// the frequency of the strongest partial (a single sine) over a segment: FFT + parabolic interpolation on the log magnitude
inline double f0 (const std::vector<float>& x, size_t from, size_t n, double lo = 20, double hi = 6000, double sr = kSr)
{
    size_t N = 1; while (N < n) N <<= 1; N = std::max<size_t> (N, 1 << 16);
    std::vector<std::complex<double>> a (N);
    for (size_t i = 0; i < n && from + i < x.size(); ++i) a[i] = x[from + i] * (0.5 - 0.5 * std::cos (2 * 3.14159265358979 * (double) i / (double) (n - 1)));
    soundtest::fft (a);
    size_t best = 1; double bm = 0;
    for (size_t k = (size_t) (lo * N / sr); k < (size_t) (hi * N / sr) && k < N / 2; ++k) { const double m = std::abs (a[k]); if (m > bm) { bm = m; best = k; } }
    const double l = std::log (std::abs (a[best - 1]) + 1e-30), c = std::log (bm + 1e-30), r = std::log (std::abs (a[best + 1]) + 1e-30);
    const double dl = 0.5 * (l - r) / (l - 2 * c + r);
    return (best + dl) * sr / (double) N;
}
inline double cents (double a, double b) { return 1200.0 * std::log2 (a / b); }
// zero-crossing frequency of a (near) sine in a short window: for tracking a glide
inline double zcHz (const std::vector<float>& x, size_t from, size_t n, double sr = kSr)
{
    double first = -1, last = -1; int cnt = 0;
    for (size_t i = from + 1; i < from + n && i < x.size(); ++i)
        if (x[i - 1] <= 0 && x[i] > 0) { const double t = (double) (i - 1) + x[i - 1] / (x[i - 1] - x[i]); if (first < 0) first = t; else ++cnt; last = t; }
    return cnt > 0 ? cnt / ((last - first) / sr) : 0.0;
}
inline double rmsDb (const std::vector<float>& x, size_t from, size_t n) { double s = 0; for (size_t i = from; i < from + n && i < x.size(); ++i) s += (double) x[i] * x[i]; return 10 * std::log10 (s / (double) n + 1e-30); }

inline void engineChecks (Counter& C)
{
    {   // root detection: synthetic tones + a trimmed one-shot padded with zeros
        double worst = 0;
        for (double hz : { 110.0, 220.0, 440.0 }) { const auto d = mk (tone (hz, 1.5, 12)); const double m = 69 + 12 * std::log2 (hz / 440.0); worst = std::max (worst, std::abs ((d->root + d->rootCents * 0.01) - m) * 100); }
        auto shot = tone (196.0, 0.4, 8); for (size_t i = 0; i < shot.size(); ++i) shot[i] *= (float) std::exp (-3.0 * (double) i / kSr); shot.resize (shot.size() + (size_t) (1.5 * kSr), 0.0f);
        const auto d = mk (shot); const double m = 69 + 12 * std::log2 (196.0 / 440.0);
        const double sc = std::abs ((d->root + d->rootCents * 0.01) - m) * 100;
        std::printf ("sample root: 110/220/440 Hz worst %.2f cents, trimmed one-shot (196 Hz + 1.5 s of zeros) %.2f cents\n", worst, sc);
        C.ok (worst < 5 && sc < 10, "YOUR SAMPLE: root detection within a few cents (tones + a trimmed one-shot)");
    }
    const auto sine = mk (tone (440.0, 2.0, 1));   // root A4
    {   // pitched playback across C1..C7
        arpsmp::Params p; p.root = 69; p.mode = 1; p.loopStart = 0.2f; p.loopEnd = 0.95f;
        double worst = 0; int at = 0;
        for (int note = 24; note <= 96; note += 6)
        {
            const auto o = play (sine.get(), p, { { 0.0, 0, (float) note, 1.6, 1, 0 } }, 1.5);
            const double want = 440.0 * std::pow (2.0, (note - 69) / 12.0), got = f0 (o.L, (size_t) (0.2 * kSr), (size_t) (1.0 * kSr), want * 0.8, want * 1.25);
            if (std::abs (cents (got, want)) > worst) { worst = std::abs (cents (got, want)); at = note; }
        }
        std::printf ("sample pitch: worst %.2f cents over C1..C7 (at %d)\n", worst, at);
        C.ok (worst < 5, "YOUR SAMPLE: pitched playback within 5 cents C1..C7");
    }
    {   // no aliasing pitching up an octave: a band-limited saw (440 Hz, partials to 21.6 kHz) at +12
        const auto saw = mk (tone (440.0, 2.0, 60));
        arpsmp::Params p; p.root = 69; p.mode = 0;   // (one pass: no loop seam in the analysed second)
        const auto o = play (saw.get(), p, { { 0.0, 0, 81.0f, 1.8, 1, 0 } }, 1.5);
        const int N = 1 << 15; std::vector<std::complex<double>> a ((size_t) N);
        for (int i = 0; i < N; ++i) { const double w = (double) i / (N - 1); a[(size_t) i] = o.L[(size_t) (0.3 * kSr) + (size_t) i] * (0.35875 - 0.48829 * std::cos (6.283185307 * w) + 0.14128 * std::cos (12.56637061 * w) - 0.01168 * std::cos (18.84955592 * w)); }   // Blackman-Harris: the window's own leakage stays under -90 dB
        soundtest::fft (a);
        double tot = 0, bad = 0; const double f = 880.0;
        for (int k = 2; k < N / 2; ++k) { const double fr = k * kSr / N; if (fr < 40 || fr > 20000) continue; const double pw = std::norm (a[(size_t) k]); tot += pw; const double h = std::round (fr / f); if (! (h >= 1 && std::abs (fr - h * f) <= 8 * kSr / N)) bad += pw; }
        const double al = 10 * std::log10 (bad / tot + 1e-30);
        if (std::getenv ("SMP_DEBUG")) { std::vector<std::pair<double, double>> v; for (int k = 2; k < N / 2; ++k) { const double fr = k * kSr / N; const double h = std::round (fr / f); if (fr > 40 && fr < 20000 && ! (h >= 1 && std::abs (fr - h * f) <= 6 * kSr / N)) v.push_back ({ std::norm (a[(size_t) k]), fr }); } std::sort (v.rbegin(), v.rend()); for (int i = 0; i < 8 && i < (int) v.size(); ++i) std::printf ("  bad %.1f Hz %.1f dB\n", v[(size_t) i].second, 10 * std::log10 (v[(size_t) i].first / tot)); }
        std::printf ("sample alias at +12 st: %.1f dB\n", al);
        C.ok (al < -60, "YOUR SAMPLE: pitching up an octave doesn't alias (< -60 dB)");
    }
    {   // a loop seam without a click (a sine, the auto-found loop)
        arpsmp::Params p; p.root = 69; p.mode = 1; p.loopStart = sine->loopS; p.loopEnd = sine->loopE; p.xfade = 0.02f;
        const auto o = play (sine.get(), p, { { 0.0, 0, 69.0f, 5.0, 1, 0 } }, 4.5);
        float peak = 0, step = 0; for (size_t i = (size_t) (0.1 * kSr); i < (size_t) (4.2 * kSr); ++i) { peak = std::max (peak, std::abs (o.L[i])); step = std::max (step, std::abs (o.L[i] - o.L[i - 1])); }
        const float ideal = peak * (float) (2 * 3.14159265358979 * 440.0 / kSr);
        std::printf ("sample loop: auto loop %.3f..%.3f, max step %.4f vs a clean sine %.4f\n", sine->loopS, sine->loopE, step, ideal);
        C.ok (step < ideal * 1.15f && sine->loopE > sine->loopS, "YOUR SAMPLE: the loop seam is click-free (max sample step ~ a clean sine's)");
    }
    {   // ADSR timing
        arpsmp::Params p; p.root = 69; p.mode = 1; p.a = 0.1f; p.d = 0.1f; p.s = 1.0f; p.r = 0.2f;
        const auto o = play (sine.get(), p, { { 0.0, 0, 69.0f, 0.6, 1, 0 } }, 1.2);
        auto env = [&] (double t) { const size_t c = (size_t) (t * kSr); float m = 0; for (size_t i = c; i < c + 120 && i < o.L.size(); ++i) m = std::max (m, std::abs (o.L[i])); return m; };
        const float full = env (0.4); double t90 = 0; for (double t = 0; t < 0.3; t += 0.0025) if (env (t) >= 0.9f * full) { t90 = t; break; }
        double tRel = 0; for (double t = 0.6; t < 1.2; t += 0.0025) if (env (t) <= 0.01f * full) { tRel = t - 0.6; break; }
        std::printf ("sample ADSR: 90%% at %.3f s (attack 0.1), -40 dB %.3f s after the note ends (release 0.2)\n", t90, tRel);
        C.ok (std::abs (t90 - 0.09) < 0.015 && std::abs (tRel - 0.2) < 0.03, "YOUR SAMPLE: ADSR timing (attack 0.1 s, release 0.2 s)");
    }
    {   // stereo stays stereo
        const auto st = mk (tone (440.0, 1.5), tone (660.0, 1.5));
        arpsmp::Params p; p.root = 69; p.mode = 1;
        const auto o = play (st.get(), p, { { 0.0, 0, 69.0f, 1.2, 1, 0 } }, 1.0);
        const double fl = f0 (o.L, 4800, 32768, 300, 900), fr = f0 (o.R, 4800, 32768, 300, 900);
        C.ok (std::abs (fl - 440) < 2 && std::abs (fr - 660) < 2, "YOUR SAMPLE: stereo preserved (L 440 Hz, R 660 Hz)");
    }
    {   // 16 voices: CPU
        const auto saw = mk (tone (220.0, 3.0, 40));
        arpsmp::Params p; p.root = 57; p.mode = 1;
        std::vector<Ev> ev; for (int k = 0; k < 16; ++k) ev.push_back ({ 0.0, 0, (float) (48 + k * 2), 2.2, 100 + k, 0 });
        const auto t0 = juce::Time::getMillisecondCounterHiRes(); const auto o = play (saw.get(), p, ev, 2.0, 1); const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        (void) o; std::printf ("sample: 16 voices for 2 s rendered in %.0f ms = %.1f %% of a core\n", ms, ms / 20.0);
        C.ok (ms / 20.0 < 60.0, "YOUR SAMPLE: 16 voices run in real time (" + std::to_string ((int) (ms / 20.0)) + " % of a core)");
    }
    {   // GLIDE: the pitch slides (no retrigger), FATTEN: detuned copies, STEP: a retrigger
        arpsmp::Params p; p.root = 69; p.mode = 1; p.loopStart = 0.2f; p.loopEnd = 0.95f;
        const auto o = play (sine.get(), p, { { 0.0, 0, 69.0f, 2.0, 7, 0 }, { 0.5, 2, 72.0f, 0.4, 7, 0 }, { 0.9, 1, 72.0f, 0, 7, 0 } }, 1.6);
        double maxErr = 0; double minDb = 0, maxDb = -200;
        for (double t = 0.55; t < 0.85; t += 0.05)
        {
            const double u = (t + 0.025 - 0.5) / 0.4, want = 440.0 * std::pow (2.0, 3.0 * std::clamp (u, 0.0, 1.0) / 12.0);
            maxErr = std::max (maxErr, std::abs (cents (zcHz (o.L, (size_t) (t * kSr), (size_t) (0.05 * kSr)), want)));
        }
        for (double t = 0.3; t < 1.3; t += 0.02) { const double db = rmsDb (o.L, (size_t) (t * kSr), (size_t) (0.02 * kSr)); minDb = std::min (minDb, db); maxDb = std::max (maxDb, db); }
        const double end = zcHz (o.L, (size_t) (1.0 * kSr), (size_t) (0.2 * kSr));
        std::printf ("sample glide: the pitch follows the 400 ms ramp within %.1f cents, level steady within %.2f dB, lands at %.1f Hz\n", maxErr, maxDb - minDb, end);
        C.ok (maxErr < 25 && maxDb - minDb < 1.5 && std::abs (cents (end, 523.25)) < 5, "YOUR SAMPLE: GLIDE slides the pitch smoothly, no retrigger");
        // FATTEN: copies around the note (measured spread)
        const auto thin = play (sine.get(), p, { { 0.0, 0, 69.0f, 2.0, 8, 0.0f } }, 1.5), fat = play (sine.get(), p, { { 0.0, 0, 69.0f, 2.0, 8, 1.0f } }, 1.5);
        auto spread = [&] (const Out& x) {
            const int N = 1 << 17; std::vector<std::complex<double>> a ((size_t) N);
            for (int i = 0; i < (int) (1.2 * kSr) && i < N; ++i) a[(size_t) i] = (x.L[(size_t) (0.2 * kSr) + (size_t) i] + x.R[(size_t) (0.2 * kSr) + (size_t) i]) * (0.5 - 0.5 * std::cos (6.283185307 * i / (1.2 * kSr - 1)));
            soundtest::fft (a); double num = 0, den = 0;
            for (int k = (int) (400.0 * N / kSr); k < (int) (480.0 * N / kSr); ++k) { const double fr = k * kSr / N, pw = std::norm (a[(size_t) k]); num += pw * std::abs (cents (fr, 440.0)); den += pw; }
            return num / den; };
        double lr = 0; { double s = 0, a2 = 0, b2 = 0; for (size_t i = (size_t) (0.2 * kSr); i < (size_t) (1.4 * kSr); ++i) { s += (double) fat.L[i] * fat.R[i]; a2 += (double) fat.L[i] * fat.L[i]; b2 += (double) fat.R[i] * fat.R[i]; } lr = s / std::sqrt (a2 * b2); }
        std::printf ("sample fatten: spread %.1f cents (none %.1f), L/R correlation %.2f\n", spread (fat), spread (thin), lr);
        C.ok (spread (fat) > spread (thin) + 5 && lr < 0.95, "YOUR SAMPLE: FATTEN layers detuned copies (wider spread, wider stereo)");
        // STEP: the second note starts the sample again (an onset marker at the start of the region)
        auto marked = tone (440.0, 1.0, 1, kSr, 0.2); for (int i = 0; i < 200; ++i) marked[(size_t) i] = 0.95f;   // a bright click opens the sample
        const auto ms = mk (marked); arpsmp::Params q; q.root = 69; q.mode = 1; q.a = 0.0005f;
        const auto st = play (ms.get(), q, { { 0.0, 0, 69.0f, 0.4, 9, 0 }, { 0.5, 0, 69.0f, 0.4, 10, 0 } }, 1.0);
        float pk1 = 0, pk2 = 0, mid = 0; for (size_t i = 0; i < 480; ++i) pk1 = std::max (pk1, std::abs (st.L[i])); for (size_t i = (size_t) (0.5 * kSr / 256) * 256; i < (size_t) (0.5 * kSr / 256) * 256 + 480; ++i) pk2 = std::max (pk2, std::abs (st.L[i])); for (size_t i = (size_t) (0.3 * kSr); i < (size_t) (0.35 * kSr); ++i) mid = std::max (mid, std::abs (st.L[i]));
        C.ok (pk2 > 2.0f * mid && pk1 > 2.0f * mid, "YOUR SAMPLE: STEP retriggers the sample (its opening click again)");
    }
    {   // ONE-SHOT: the whole region on a short note; REVERSE; START
        const auto ramp = mk (tone (300.0, 1.0, 6));
        arpsmp::Params p; p.root = 62; p.mode = 0; p.r = 0.05f;
        const auto o = play (ramp.get(), p, { { 0.0, 0, (float) ramp->root + ramp->rootCents * 0.01f, 0.1, 1, 0 } }, 1.6);
        const double at08 = rmsDb (o.L, (size_t) (0.8 * kSr), 2400), at12 = rmsDb (o.L, (size_t) (1.2 * kSr), 2400);
        std::printf ("sample one-shot: a 0.1 s note, level at 0.8 s %.1f dB, at 1.2 s %.1f dB\n", at08, at12);
        C.ok (at08 > -40 && at12 < -100, "YOUR SAMPLE: ONE-SHOT plays the whole region on a short note");
        // REVERSE: the output correlates with the reversed sample (a chirp, played at its root)
        std::vector<float> ch ((size_t) kSr); for (size_t i = 0; i < ch.size(); ++i) { const double t = (double) i / kSr; ch[i] = (float) (0.5 * std::sin (2 * 3.14159265358979 * (200 * t + 300 * t * t))); }
        const auto cd = mk (ch); arpsmp::Params r; r.root = 69; r.mode = 0; r.reverse = true; r.a = 0.0001f;
        const auto ro = play (cd.get(), r, { { 0.0, 0, 69.0f, 1.0, 1, 0 } }, 1.2);
        const auto& src = cd->lv[0][0]; double s = 0, a2 = 0, b2 = 0;
        for (size_t i = 2000; i < 40000; ++i) { const double ref = src[src.size() - 1 - i]; s += ref * ro.L[i]; a2 += ref * ref; b2 += (double) ro.L[i] * ro.L[i]; }
        const double corr = s / std::sqrt (a2 * b2);
        std::printf ("sample reverse: correlation with the reversed sample %.3f\n", corr);
        C.ok (corr > 0.95, "YOUR SAMPLE: REVERSE plays the region backwards");
        // START: the first samples played are the region's (best match at lag 0)
        arpsmp::Params q; q.root = 69; q.mode = 0; q.start = 0.25f; q.a = 0.00002f;
        const auto so = play (cd.get(), q, { { 0.0, 0, 69.0f, 0.5, 1, 0 } }, 0.3);
        const size_t s0 = (size_t) (0.25f * (float) src.size()); int bestLag = -99; double bc = -2;
        for (int lag = -20; lag <= 20; ++lag) { double s2 = 0, x2 = 0, y2 = 0; for (size_t i = 200; i < 4000; ++i) { const double ref = src[(size_t) ((long) s0 + (long) i + lag)]; s2 += ref * so.L[i]; x2 += ref * ref; y2 += (double) so.L[i] * so.L[i]; } const double c = s2 / std::sqrt (x2 * y2); if (c > bc) { bc = c; bestLag = lag; } }
        std::printf ("sample start 0.25: best match at lag %d (corr %.3f)\n", bestLag, bc);
        C.ok (bestLag == 0 && bc > 0.99, "YOUR SAMPLE: START moves exactly where playback begins");
    }
}
} // namespace smptest
