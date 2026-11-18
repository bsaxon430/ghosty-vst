// GHOST SLICE build 6 tests (included by SliceTests.cpp after its helpers): the INSTRUMENT distortion diagnosis (measured before /
// after), the vocoder, VOICE <-> INSTRUMENT, DREAM, the melody styles, VARY, the state, the UNDER THE HOOD page, the A/B renders.
#pragma once
#include "../Vocoder.h"
#include "../VoiceMorph.h"

namespace six
{
using Buf = juce::AudioBuffer<float>;
using namespace clearm;

// the share of energy NOT on the harmonics of f0 (200 Hz .. 12 kHz), dB: noise, grain roughness, aliasing and buzz all raise it
static double inharmonicDb (const Buf& b, int from, int to, double f0)
{
    if (f0 <= 0) return 0;
    auto m = monoOf (b); const int N = 8192; juce::dsp::FFT f (13); std::vector<float> buf ((size_t) N * 2); double on = 0, off = 0;
    for (int s = from; s + N <= to; s += N / 2)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < N; ++i) buf[(size_t) i] = m.getSample (0, s + i) * (float) (0.5 - 0.5 * std::cos (6.283185307 * i / N));
        f.performFrequencyOnlyForwardTransform (buf.data());
        for (int k = (int) (200.0 * N / 48000); k < (int) (12000.0 * N / 48000); ++k)
        {
            const double hz = k * 48000.0 / N, hn = hz / f0, d = std::abs (hn - std::round (hn)) * f0;
            const double p = (double) buf[(size_t) k] * buf[(size_t) k];
            (d < juce::jmax (12.0, 0.03 * f0) ? on : off) += p;
        }
    }
    return on > 0 ? 10.0 * std::log10 ((off + 1e-20) / on) : 0;
}
// period-to-period loudness wobble (shimmer, %): grains that do not line up make every period a little different
static double shimmerPct (const Buf& b, int from, int to, double f0)
{
    if (f0 <= 0) return 0;
    auto m = monoOf (b); const double P = 48000.0 / f0; std::vector<double> r;
    for (double s = from; s + P < to; s += P) { double e = 0; const int a = (int) s, z = (int) (s + P); for (int i = a; i < z; ++i) e += (double) m.getSample (0, i) * m.getSample (0, i); r.push_back (std::sqrt (e / (z - a))); }
    if (r.size() < 4) return 0;
    double d = 0, mean = 0; for (size_t i = 1; i < r.size(); ++i) { d += std::abs (r[i] - r[i - 1]); mean += r[i]; }
    return mean > 0 ? 100.0 * d / mean : 0;
}
// energy above hz relative to 300 Hz .. hz (dB): harsh / fizzy highs
static double topDb (const Buf& b, int from, int to, double hz)
{
    auto m = monoOf (b); const int N = 4096; juce::dsp::FFT f (12); std::vector<float> buf ((size_t) N * 2); double hi = 0, lo = 0;
    for (int s = from; s + N <= to; s += N / 2)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < N; ++i) buf[(size_t) i] = m.getSample (0, s + i) * (float) (0.5 - 0.5 * std::cos (6.283185307 * i / N));
        f.performFrequencyOnlyForwardTransform (buf.data());
        for (int k = 300 * N / 48000; k < N / 2; ++k) { const double p = (double) buf[(size_t) k] * buf[(size_t) k]; (k * 48000.0 / N >= hz ? hi : lo) += p; }
    }
    return lo > 0 ? 10.0 * std::log10 ((hi + 1e-20) / lo) : 0;
}
// the worst sample step at a zone's loop seam, relative to the zone's typical step (1 = invisible, > 4 = a tick)
static double seamStep (const Zone& z)
{
    if (z.loopEnd <= z.loopStart + 8) return 0;
    const float* x = z.audio.getReadPointer (0);
    double typ = 0; int n = 0; for (int i = z.loopStart; i < z.loopEnd - 1; i += 7) { typ += std::abs (x[i + 1] - x[i]); ++n; }
    typ /= juce::jmax (1, n);
    const double seam = std::abs (x[z.loopStart] - x[z.loopEnd - 1]);
    return typ > 0 ? seam / typ : 0;
}
struct InstMeasures { double hnr = 0, inh = 0, shim = 0, top = 0, env = 0, flux = 0, cents = 0, warmDb = -200, seam = 0, peak = 0; int clicks = 0, clipped = 0; double alias = 0; };
// a take's INSTRUMENT, held at its root and an octave up / down (the extreme ratios), through the engine + the FX it gets
static InstMeasures measureInstrument (std::shared_ptr<const Take> t, const std::vector<double>& srcEnv, double srcTop)
{
    InstMeasures M; int cnt = 0;
    for (int d : { 0, 7, -12, 12 })
    {
        const int key = t->rootNote + d;
        auto h = held (t, key, 3.2);
        const double f0 = median (pitchTrack (h, 48000, 48000 * 3));
        M.hnr += hnr (h, 48000, 48000 * 3); M.inh += inharmonicDb (h, 48000, 48000 * 3, f0); M.shim += shimmerPct (h, 48000, 48000 * 3, f0);
        M.top += topDb (h, 48000, 48000 * 3, 6000.0) - srcTop; M.flux += flux (h, 48000, 48000 * 3); M.cents += centsSpread (pitchTrack (h, (int) (0.7 * 48000), 48000 * 3));
        if (d == 0) M.env = pearson (srcEnv, envelope (h, 48000, 48000 * 3));
        ++cnt;
    }
    M.hnr /= cnt; M.inh /= cnt; M.shim /= cnt; M.top /= cnt; M.flux /= cnt; M.cents /= cnt;
    // the warmth stage: the same key with warmth 0.35 vs 0 -> what the tanh curve added (dB under the signal)
    {
        Engine e; e.prepare (48000, 256); e.publish (t);
        auto dry = play (e, { { 0.0, t->rootNote, true }, { 3.0, t->rootNote, false } }, 3.2);
        auto run = [&] (float warm) { Fx fx; fx.space.store (0.0f); fx.warmth.store (warm); fx.ensemble.store (0.0f); fx.width.store (0.5f); fx.clear.store (t->clear()); fx.prepare (48000, 512);
                                      Buf o; o.makeCopyOf (dry); o.applyGain (dsp::dbToGain (-14.0 - dsp::lufs (o, 48000)) * 1.0f); for (int i = 0; i < o.getNumSamples(); i += 512) fx.process (o.getWritePointer (0) + i, o.getWritePointer (1) + i, juce::jmin (512, o.getNumSamples() - i)); return o; };
        auto a = run (0.35f), b = run (0.0f);
        double dd = 0, ss = 0; for (int i = 4800; i < a.getNumSamples(); ++i) { const double x = a.getSample (0, i) - b.getSample (0, i); dd += x * x; ss += (double) b.getSample (0, i) * b.getSample (0, i); }
        M.warmDb = ss > 0 ? 10.0 * std::log10 ((dd + 1e-20) / ss) : -200;
    }
    for (auto& z : t->zones) { M.clicks += dsp::clicksIn (asPlayed (z)); M.seam = std::max (M.seam, seamStep (z)); M.peak = std::max (M.peak, (double) dsp::peakOf (z.audio)); for (int i = 0; i < z.audio.getNumSamples(); ++i) if (std::abs (z.audio.getSample (0, i)) > 0.99f) ++M.clipped; }
    // the 4-point read between zones (a key a semitone above a zone, the oldest engine's case): energy above 12 kHz that the zone itself does not have
    {
        const int key = t->rootNote + 1; const int zi = Engine::zoneFor (*t, key); const auto& Z = t->zones[(size_t) zi];
        auto h = held (t, key, 3.2); auto own = held (t, Z.rootNote, 3.2);
        M.alias = topDb (h, 48000, 48000 * 3, 12000.0) - topDb (own, 48000, 48000 * 3, 12000.0);
    }
    return M;
}
static void printMeasures (const char* name, const InstMeasures& M)
{
    std::printf ("    %-26s HNR %5.1f dB | off-harmonic %6.1f dB | shimmer %5.1f %% | top>6k vs voice %+5.1f dB | env corr %.3f | flux %.3f | pitch %.1f c | warmth adds %6.1f dB | seam %.1f | clicks %d | zone peak %.3f (%d clipped) | read top>12k %+5.1f dB\n",
                 name, M.hnr, M.inh, M.shim, M.top, M.env, M.flux, M.cents, M.warmDb, M.seam, M.clicks, M.peak, M.clipped, M.alias);
}

static std::shared_ptr<const Take> make (std::shared_ptr<const Prepared> P, Type ty, bool cl, int engine, double knob = -1, Style st = Style::Auto, uint32_t seed = 2024, uint32_t detail = 5, int variation = 0)
{
    Recipe r; r.type = ty; r.seed = seed; r.detail = detail; r.musical = 0.7; r.clear = cl; r.engine = engine; r.knob = knob; r.style = st; r.variation = variation;
    return generate (*P, r);
}

// the public-domain voice (LibriVox, Public Domain Mark 1.0; see /home/claude/work/gs/pd/LICENSE-pd-voice.txt) when it is on this
// machine (it is not in the repo: the tests never depend on it), else nullptr
static std::shared_ptr<const SourceAudio> pdVoice()
{
    static std::shared_ptr<const SourceAudio> s = []
    {
        juce::File f ("/home/claude/work/gs/pd/pd-voice.wav"); juce::String e;
        return f.existsAsFile() ? std::shared_ptr<const SourceAudio> (source::load (f, e)) : std::shared_ptr<const SourceAudio>();
    }();
    return s;
}
// what the preview / export chain does to a take: the peaks the finish squashed (dB of crest lost) and the residual it added
static void chainMeasures (const Take& t, double& squashDb, double& residualDb)
{
    Fx fx; fx.space.store (0.0f); fx.tone.store (0.0f); fx.width.store (0.55f); fx.warmth.store (0.35f); fx.ensemble.store (t.ensemble); fx.clear.store (t.clear()); fx.prepare (48000, 512);
    Buf o; o.makeCopyOf (t.preview); for (int i = 0; i < o.getNumSamples(); i += 512) fx.process (o.getWritePointer (0) + i, o.getWritePointer (1) + i, juce::jmin (512, o.getNumSamples() - i));
    const auto w = renderForExport (t, 0.0f, 0.0f, 0.55f, 0.35f);
    squashDb = crestDb (o) - crestDb (w);
    const double g = dsp::rmsOf (w, 0, o.getNumSamples()) / juce::jmax (1e-9, dsp::rmsOf (o));
    double dd = 0, ss = 0; for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < o.getNumSamples(); ++i) { const double x = w.getSample (ch, i) - g * o.getSample (ch, i); dd += x * x; ss += g * g * (double) o.getSample (ch, i) * o.getSample (ch, i); }
    residualDb = ss > 0 ? 10.0 * std::log10 ((dd + 1e-20) / ss) : -200;
}

// ---------------------------------------------------------------- the diagnosis (mode "diag"): measure first
static void diagOn (std::shared_ptr<const SourceAudio> src, const char* what);
static void diag()
{
    diagOn (sung(), "the synthetic sung phrase");
    if (pdVoice() != nullptr) diagOn (pdVoice(), "the public-domain voice (LibriVox)");
}
static void diagOn (std::shared_ptr<const SourceAudio> src, const char* what)
{
    section ((juce::String ("INSTRUMENT distortion, measured on ") + what + ": CLEAR off / on, old vs new").toRawUTF8());
    auto P = prepOf (src);
    auto i5 = make (P, Type::Instrument, false, 5);
    const auto& u = i5->used.front();
    const auto srcEnv = envelope (src->audio, (int) ((u.first + 0.25 * (u.second - u.first)) * 48000), (int) ((u.second - 0.25 * (u.second - u.first)) * 48000));
    const double srcTop = topDb (src->audio, (int) (u.first * 48000), (int) (u.second * 48000), 6000.0);
    auto i5c = make (P, Type::Instrument, true, 5), i6 = make (P, Type::Instrument, false, 6, 1.0), i6c = make (P, Type::Instrument, true, 6, 1.0);
    printMeasures ("OLD (build 5) CLEAR off", measureInstrument (i5, srcEnv, srcTop));
    printMeasures ("OLD (build 5) CLEAR on", measureInstrument (i5c, srcEnv, srcTop));
    for (auto& [nm, t] : std::vector<std::pair<const char*, std::shared_ptr<const Take>>> { { "OLD CLEAR off", i5 }, { "OLD CLEAR on", i5c }, { "NEW CLEAR off", i6 }, { "NEW CLEAR on", i6c } })
    {
        double sq, res; chainMeasures (*t, sq, res);
        std::printf ("    %-14s preview -> export: peaks squashed %.2f dB, the finish added %.1f dB of residual (limiter / clip)\n", nm, sq, res);
    }
    // PSOLA in isolation: the same vowel, the grain normalisation and the random walk (what the tone is made of)
    {
        Recipe r; r.type = Type::Instrument; r.seed = 2024; r.detail = 5; r.clear = true; r.engine = 5;
        auto t = generate (*P, r);
        std::printf ("    (old zones: %d, %d rendered every %d semitones)\n", (int) t->zones.size(), (int) t->zones.size(), t->zones.size() > 20 ? 1 : 3);
    }
    printMeasures ("NEW (build 6) CLEAR off", measureInstrument (i6, srcEnv, srcTop));
    printMeasures ("NEW (build 6) CLEAR on", measureInstrument (i6c, srcEnv, srcTop));
    if (juce::SystemStats::getEnvironmentVariable ("GS_EXP", "").isNotEmpty())
        for (double br : { 0.0, 0.2, 0.5 })
        {
            Recipe r; r.type = Type::Instrument; r.seed = 2024; r.detail = 5; r.musical = 0.7; r.clear = true; r.engine = 6; r.knob = 1.0; r.deep[1] = br;
            auto t = generate (*P, r);
            printMeasures ((juce::String ("NEW CLEAR on, BREATH ") + juce::String (br, 1)).toRawUTF8(), measureInstrument (t, srcEnv, srcTop));
        }
    // the vocoder on its own: the vowel resynthesised at its own pitch vs the original (level, envelope, HNR)
    {
        auto F = P->vocoder();
        const double a = u.first * 48000, b = u.second * 48000;
        voc::Track T; for (double i = a / 240.0; i < b / 240.0; i += 1.0) T.push_back (F->at (i));
        auto y = voc::synth (T, 48000, {});
        Buf yb (1, (int) y.size()); yb.copyFrom (0, 0, y.data(), (int) y.size());
        auto orig = dsp::slice (P->work->buffer(), a, a + y.size());
        std::printf ("    vocoder resynth of the vowel at its own pitch: level %+.1f dB vs the original, env corr %.3f, HNR %.1f vs %.1f dB\n",
                     dsp::gainToDb (dsp::rmsOf (yb) / dsp::rmsOf (orig)), pearson (envelope (orig, 0, orig.getNumSamples()), envelope (yb, 0, yb.getNumSamples())), hnr (yb, 0, yb.getNumSamples()), hnr (orig, 0, orig.getNumSamples()));
    }
}
// the build 6 UI: the main page (PURPLE) with the third knob + MELODY + UNDER THE HOOD, and the UNDER THE HOOD page for INSTRUMENT
// and PAD; checks that the page opens and closes and that its controls reach the processor
static void uiSix (const juce::File& dir)
{
    section ("build 6 UI (UNDER THE HOOD, the STYLE browser): the old window's page, removed in build 16 (the new screen has no such page)");
    juce::ignoreUnused (dir);
}

// ---------------------------------------------------------------- the melody brain: every style, every LENGTH
struct NoteStats { double density = 0, range = 0, meanLen = 0, offbeat = 0, step = 0; };
static NoteStats statsOf (const std::vector<NoteEvent>& ev, int bars)
{
    NoteStats S; if (ev.empty()) return S;
    int lo = 127, hi = 0; double len = 0, off = 0, st = 0;
    for (size_t i = 0; i < ev.size(); ++i) { lo = std::min (lo, ev[i].note); hi = std::max (hi, ev[i].note); len += ev[i].length; if (std::fmod (ev[i].beat + 1e-6, 1.0) > 0.05) off += 1; if (i) st += std::abs (ev[i].note - ev[i - 1].note) <= 2; }
    S.density = ev.size() / (bars * 4.0); S.range = hi - lo; S.meanLen = len / ev.size(); S.offbeat = off / ev.size(); S.step = ev.size() > 1 ? st / (ev.size() - 1) : 0;
    return S;
}
static void brainSix()
{
    section ("the melody brain: every style, every LENGTH, in key, phrase ends, different styles sound different");
    int bad = 0, total = 0, endsOk = 0, endsN = 0; juce::StringArray badNames; double worstMs = 0; juce::String worst;
    std::vector<std::pair<juce::String, NoteStats>> stats;
    for (int si = 1; si < styleCount(); ++si)
    {
        const auto st = (Style) si;
        for (const Key k : { Key { 9, true }, Key { 0, false } })
            for (int bars : { 1, 2, 4, 8, 16 })
            {
                const auto t0 = juce::Time::getMillisecondCounterHiRes();
                const auto S = compose (k, 62, st, bars, 1234u + (uint32_t) bars, 77u, 0, 0.5, 0.5, {}, 120.0);
                const double ms = juce::Time::getMillisecondCounterHiRes() - t0; if (ms > worstMs) { worstMs = ms; worst = styleName (st); }
                ++total;
                bool ok = ! S.melody.empty() && ! S.chords.empty();
                for (auto& e : S.melody) ok = ok && inScale (e.note, k) && e.beat >= 0 && e.beat < bars * 4.0 && e.length > 0 && e.beat + e.length <= bars * 4.0 + 1e-6;
                for (auto& c : S.chords) for (int n : c.notes) ok = ok && inScale (n, k);
                if (! ok) { ++bad; badNames.addIfNotAlreadyThere (styleName (st));
                    if (juce::SystemStats::getEnvironmentVariable ("GS_STATS", "").isNotEmpty()) { bool mel = true; for (auto& e : S.melody) mel = mel && inScale (e.note, k) && e.beat >= 0 && e.beat < bars * 4.0 && e.length > 0 && e.beat + e.length <= bars * 4.0 + 1e-6; std::printf ("    bad: %s %s bars %d melodyOk %d chords %s\n", styleName (st), k.minor ? "min" : "maj", bars, (int) mel, S.progName.toRawUTF8()); } }
                if (k.minor && bars == 8)
                {   // the last note sits on the chord under it (a chord tone or the tonic)
                    const auto& e = S.melody.back(); const int ci = juce::jlimit (0, (int) S.chords.size() - 1, (int) std::floor (e.beat / S.beatsPerChord));
                    ++endsN; if (isChordTone (e.note, S.chords[(size_t) ci]) || ((e.note - k.tonic) % 12 + 12) % 12 == 0) ++endsOk;
                }
                if (k.minor && bars == 8) stats.push_back ({ styleName (st), statsOf (S.melody, 8) });
            }
    }
    {   // deterministic: the same seed twice (and after other styles ran) = the same notes, every style
        int diff = 0; juce::StringArray dn;
        for (int si = 1; si < styleCount(); ++si)
        {
            auto a = compose ({ 9, true }, 62, (Style) si, 8, 99u, 5u, 1, 0.5, 0.5, {}, 120.0);
            for (int sj : { 40, 45, 55, si }) compose ({ 0, false }, 60, (Style) juce::jmin (styleCount() - 1, sj), 4, 7u, 3u, 0, 0.5, 0.5, {}, 120.0);
            auto b = compose ({ 9, true }, 62, (Style) si, 8, 99u, 5u, 1, 0.5, 0.5, {}, 120.0);
            bool same = a.melody.size() == b.melody.size(); for (size_t i = 0; same && i < a.melody.size(); ++i) same = a.melody[i].note == b.melody[i].note && a.melody[i].beat == b.melody[i].beat;
            if (! same) { ++diff; dn.add (styleName ((Style) si)); }
        }
        check (diff == 0, "every style is deterministic (the same seed = the same notes, whatever ran before)" + (dn.isEmpty() ? juce::String() : ": " + dn.joinIntoString (", ")));
    }
    check (bad == 0, "every style (" + juce::String (styleCount() - 1) + ") x every LENGTH (1, 2, 4, 8, 16) x major / minor: notes and chords in key, inside the length (" + juce::String (total - bad) + " / " + juce::String (total) + (badNames.isEmpty() ? juce::String() : ": " + badNames.joinIntoString (", ")) + ")");
    check (endsOk >= (int) (0.85 * endsN), "phrase ends land on a chord tone (" + juce::String (endsOk) + " / " + juce::String (endsN) + " styles at 8 bars)");
    check (worstMs < 1500, "composing is quick (the slowest: " + worst + ", " + f2 (worstMs, 0) + " ms)");
    // different styles: their note statistics spread out (density, range, length, syncopation)
    int distinctPairs = 0, pairs = 0;
    for (size_t i = 0; i < stats.size(); ++i)
        for (size_t j = i + 1; j < stats.size(); ++j)
        {
            const auto& a = stats[i].second; const auto& b = stats[j].second; ++pairs;
            const double d = std::abs (a.density - b.density) / 2.0 + std::abs (a.range - b.range) / 12.0 + std::abs (a.meanLen - b.meanLen) / 1.5 + std::abs (a.offbeat - b.offbeat) + std::abs (a.step - b.step);
            if (d > 0.25) ++distinctPairs;
        }
    double dmin = 1e9, dmax = 0; for (auto& s2 : stats) { dmin = std::min (dmin, s2.second.density); dmax = std::max (dmax, s2.second.density); }
    check (distinctPairs >= (int) (0.85 * pairs), "different styles give clearly different notes (" + juce::String (distinctPairs) + " / " + juce::String (pairs) + " pairs differ; density " + f2 (dmin, 2) + " .. " + f2 (dmax, 2) + " notes per beat)");
    if (juce::SystemStats::getEnvironmentVariable ("GS_STATS", "").isNotEmpty())
        for (auto& [n, a] : stats) std::printf ("    %-16s density %.2f range %2.0f len %.2f off %.2f step %.2f\n", n.toRawUTF8(), a.density, a.range, a.meanLen, a.offbeat, a.step);
}

// ---------------------------------------------------------------- VOICE: the morph model (identity, harmonics + residual)
static double nullDb (const std::vector<float>& a, const float* b, int from, int to)
{
    double d = 0, s = 0; for (int i = from; i < to; ++i) { const double x = (double) a[(size_t) i] - b[i]; d += x * x; s += (double) b[i] * b[i]; }
    return s > 0 ? 10.0 * std::log10 ((d + 1e-30) / s) : 0;
}
static vm::Plan identityPlan (const vm::Model& m)
{
    vm::Plan p; const int nf = m.n / vm::kHop;
    for (int j = 0; j < nf; ++j) { p.src.push_back (j); p.f0.push_back (m.fr[(size_t) j].f0); p.gain.push_back (1); p.inst.push_back (0); p.phase.push_back (0); p.resid.push_back (1); p.air.push_back (0); p.copy.push_back (1); p.lock.push_back (1); }
    return p;
}
static void morphSix()
{
    section ("VOICE morph: the recording comes back at VOICE 0 (harmonics + residual), the model is clean");
    for (auto src : { sung(), pdVoice() })
    {
        if (src == nullptr) continue;
        auto P = prepOf (src);
        const auto& W = P->work->buffer(); const int n = juce::jmin (W.getNumSamples(), 48000 * 4);
        std::vector<float> x ((size_t) n); for (int i = 0; i < n; ++i) x[(size_t) i] = W.getSample (0, i);
        auto m = vm::analyse (x.data(), n, 48000, [&] (double s) { return P->f0AtWork (s); });
        // the harmonic part: how much of a voiced stretch it explains
        double eh = 0, er = 0; int voicedFrames = 0;
        for (size_t j = 0; j < m.fr.size(); ++j) if (m.fr[j].f0 > 0) { ++voicedFrames; for (int t = 0; t < vm::kHop && (int) j * vm::kHop + t < n; ++t) { const int i = (int) j * vm::kHop + t; eh += (double) m.harm[(size_t) i] * m.harm[(size_t) i]; er += (double) m.resid[(size_t) i] * m.resid[(size_t) i]; } }
        const double hr = 10.0 * std::log10 ((eh + 1e-20) / (er + 1e-20));
        auto y = vm::render (m, identityPlan (m));
        const int len = juce::jmin ((int) y.size(), n) - vm::kHop;
        const double nd = nullDb (y, x.data(), vm::kHop, len);
        const juce::String nm = src == sung() ? "the synthetic sung phrase" : "the public-domain voice";
        check (voicedFrames > 50 && hr > 10.0, nm + ": the harmonic model holds the voiced sound (harmonics " + f2 (hr, 1) + " dB over the residual)");
        check (nd <= -40.0, nm + ": harmonics + residual played back as they are = the recording (null " + f2 (nd, 1) + " dB, wanted <= -40)");
    }
}

// spectral flatness (dB) of the voiced, loud frames of a take: comb filtering (two copies of a sound mixed) cuts notches in it
static double flatnessDb (const Buf& b)
{
    auto m = monoOf (b); const int N = 2048; juce::dsp::FFT f (11); std::vector<float> buf ((size_t) N * 2); double sum = 0; int cnt = 0;
    const double rmsAll = dsp::rmsOf (m);
    for (int s = 0; s + N <= m.getNumSamples(); s += N / 2)
    {
        if (dsp::rmsOf (m, s, s + N) < 0.5 * rmsAll) continue;
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < N; ++i) buf[(size_t) i] = m.getSample (0, s + i) * (float) (0.5 - 0.5 * std::cos (6.283185307 * i / N));
        f.performFrequencyOnlyForwardTransform (buf.data());
        double lg = 0, ar = 0; int k0 = 200 * N / 48000, k1 = 8000 * N / 48000;
        for (int k = k0; k < k1; ++k) { const double p = (double) buf[(size_t) k] * buf[(size_t) k] + 1e-18; lg += std::log (p); ar += p; }
        sum += 10.0 * std::log10 (std::exp (lg / (k1 - k0)) / (ar / (k1 - k0))); ++cnt;
    }
    return cnt ? sum / cnt : 0;
}
static std::shared_ptr<const Take> inst6 (std::shared_ptr<const Prepared> P, double voice, double musical, bool cl = true, uint32_t seed = 2024)
{
    Recipe r; r.type = Type::Instrument; r.seed = seed; r.detail = 5; r.musical = musical; r.clear = cl; r.engine = 6; r.knob = voice; r.style = Style::Hook; r.bars = 4;
    return generate (*P, r);
}
static void voiceSix()
{
    section ("VOICE: 0 = the real recording, the sweep to INSTRUMENT is smooth (no comb), formants stay at any pitch");
    for (auto src : { sung(), pdVoice() })
    {
        if (src == nullptr) continue;
        const juce::String nm = src == sung() ? "synthetic voice" : "public-domain voice";
        auto P = prepOf (src);
        // 1. identity: VOICE 0, NATURAL (no pitch change): the first note is the recording itself (times its velocity)
        {
            auto t = inst6 (P, 0.0, 0.0);
            if (t == nullptr || t->notes.size() < 2 || t->used.empty()) { check (false, nm + ": an INSTRUMENT take at VOICE 0"); continue; }
            const double spb = 60.0 / t->bpm; const int len = juce::jmin ((int) (t->notes[0].length * spb * 48000.0), (int) ((t->used[0].second - t->used[0].first) * 48000.0) - 480) - 96;
            const int a = (int) std::lround (t->used[0].first * 48000.0) + 96, o = (int) std::lround (t->notes[0].beat * spb * 48000.0) + 96;   // (after the preview's 2 ms fade-in)
            const auto& W = P->work->buffer();
            double xy = 0, xx = 0; for (int i = 0; i < len; ++i) { const double x = (W.getSample (0, a + i) + W.getSample (juce::jmin (1, W.getNumChannels() - 1), a + i)) * 0.5; xy += x * t->preview.getSample (0, o + i); xx += x * x; }
            const double g = xx > 0 ? xy / xx : 0; double d = 0, e = 0;
            for (int i = 0; i < len; ++i) { const double x = (W.getSample (0, a + i) + W.getSample (juce::jmin (1, W.getNumChannels() - 1), a + i)) * 0.5; const double y = t->preview.getSample (0, o + i); d += (y - g * x) * (y - g * x); e += g * x * g * x; }
            const double nd = e > 0 ? 10.0 * std::log10 ((d + 1e-30) / e) : 0;
            check (len > 2000 && nd <= -90.0, nm + ": VOICE 0 + NATURAL plays the recording itself (null " + f2 (nd, 1) + " dB over " + f2 (len / 48.0, 0) + " ms, wanted <= -90)");
        }
        // 2. the sweep (MUSICAL: every note re-pitched): no comb notches anywhere, the words fade into the tone
        std::vector<double> fl; std::vector<std::shared_ptr<const Take>> ts;
        for (double v : { 0.0, 0.25, 0.5, 0.75, 1.0 }) { auto t = inst6 (P, v, 1.0); ts.push_back (t); fl.push_back (t != nullptr ? flatnessDb (t->preview) : 0.0); }
        const double ends = std::min (fl.front(), fl.back()); double worst = 0; for (size_t i = 1; i + 1 < fl.size(); ++i) worst = std::min (worst, fl[i] - ends);
        check (worst > -3.0, nm + ": the VOICE sweep has no comb notches (spectral flatness 0 / 25 / 50 / 75 / 100 %: " + f2 (fl[0], 1) + " " + f2 (fl[1], 1) + " " + f2 (fl[2], 1) + " " + f2 (fl[3], 1) + " " + f2 (fl[4], 1) + " dB)");
        bool clean = true; juce::String cl; for (auto& t : ts) { const int k = t != nullptr ? dsp::clicksIn (t->preview) : -1; cl << k << " "; clean = clean && t != nullptr && dsp::finiteAndClean (t->preview) && k == 0 && dsp::peakOf (t->preview) < 0.999f; }
        check (clean, nm + ": every step of the sweep is clean (no clipping, finite; clicks per step: " + cl.trim() + ")");
        // 3. VOICE 0 re-pitched (MUSICAL): each syllable keeps its spectral envelope (its vowel, its formants)
        {
            auto t = ts.front(); const auto& W = P->work->buffer(); double worstC = 1.0; int n = 0; const double spb = 60.0 / t->bpm;
            for (size_t i = 0; i + 1 < t->notes.size() && n < 6; ++i)
            {
                const auto& u = t->used[(size_t) i % t->used.size()]; const int sl = (int) ((u.second - u.first) * 48000.0), nl = (int) ((t->notes[i + 1].beat - t->notes[i].beat) * spb * 48000.0);
                const int len = juce::jmin (sl, nl); if (len < 4096 + 1024) continue;
                const int o = (int) (t->notes[i].beat * spb * 48000.0), a = (int) (u.first * 48000.0);
                const double c = pearson (envelope (W, a, a + len), envelope (t->preview, o, o + len)); worstC = std::min (worstC, c); ++n;
            }
            // (the cepstral envelope of a re-pitched voice is biased by its new harmonic spacing: speech moved up to 8 semitones reads lower)
            check (n == 0 || worstC >= (src == sung() ? 0.95 : 0.90), nm + ": VOICE 0 re-pitched keeps each syllable's spectral envelope (worst correlation " + f2 (worstC, 3) + " over " + juce::String (n) + " notes)");
        }
    }
    // 4. formants at +-12 semitones: the morph's harmonic model of a vowel, moved an octave up / down. Measured on the harmonic-peak
    //    envelope (re-analysed from the output): the best log-frequency shift that lines it up with the original's, and F1 / F2 (the
    //    two strongest peaks). A formant below the new fundamental cannot exist, so the vowel is one whose F1 sits above 2 x f0.
    {
        auto src = pdVoice() != nullptr ? pdVoice() : sung();
        auto P = prepOf (src); const auto& W = P->work->buffer();
        const auto& syls = P->analysis->syllables; const analysis::Syllable* best = nullptr;
        for (auto& y : syls) if (y.vowelEnd - y.vowelStart > (int64) (0.12 * 48000) && (best == nullptr || y.vowelEnd - y.vowelStart > best->vowelEnd - best->vowelStart)) best = &y;
        if (best != nullptr)
        {
            const int a0 = (int) best->vowelStart, n = (int) (best->vowelEnd - best->vowelStart);
            std::vector<float> x ((size_t) n); for (int i = 0; i < n; ++i) x[(size_t) i] = 0.5f * (W.getSample (0, a0 + i) + W.getSample (juce::jmin (1, W.getNumChannels() - 1), a0 + i));
            auto m = vm::analyse (x.data(), n, 48000, [&] (double s2) { return P->f0AtWork (a0 + s2); });
            auto envLog = [] (const vm::Model& mm, double ratio)
            {   // mean peak envelope on a log grid 150 Hz .. 4 kHz (200 points), only above the (new) fundamental
                std::vector<double> e; for (int j = 0; j < 200; ++j) { const double hz = 150.0 * std::pow (4000.0 / 150.0, j / 199.0); double s2 = 0; int c2 = 0; for (auto& f : mm.fr) if (f.f0 > 0 && f.nh > 3 && hz >= f.f0 * juce::jmax (1.0, ratio)) { s2 += mm.envAt (f, hz); ++c2; } e.push_back (c2 ? s2 / c2 : std::nan ("")); }
                return e;
            };
            auto shiftCents = [] (const std::vector<double>& r, const std::vector<double>& b2)
            {
                const double cpp = 1200.0 * std::log2 (4000.0 / 150.0) / 199.0; int bestS = 0; double bc = -2;
                for (int sh = -(int) (600 / cpp); sh <= (int) (600 / cpp); ++sh)
                {
                    double ma = 0, mb = 0; int cnt = 0; for (int i = 0; i < 200; ++i) { const int j = i + sh; if (j < 0 || j >= 200 || std::isnan (r[(size_t) i]) || std::isnan (b2[(size_t) j])) continue; ma += r[(size_t) i]; mb += b2[(size_t) j]; ++cnt; }
                    if (cnt < 60) continue; ma /= cnt; mb /= cnt; double xy = 0, xx = 0, yy = 0;
                    for (int i = 0; i < 200; ++i) { const int j = i + sh; if (j < 0 || j >= 200 || std::isnan (r[(size_t) i]) || std::isnan (b2[(size_t) j])) continue; xy += (r[(size_t) i] - ma) * (b2[(size_t) j] - mb); xx += (r[(size_t) i] - ma) * (r[(size_t) i] - ma); yy += (b2[(size_t) j] - mb) * (b2[(size_t) j] - mb); }
                    const double c3 = xx > 0 && yy > 0 ? xy / std::sqrt (xx * yy) : -2; if (c3 > bc) { bc = c3; bestS = sh; }
                }
                return bestS * cpp;
            };
            for (double ratio : { 2.0, 0.5 })
            {
                vm::Plan p; const int nf = n / vm::kHop;
                for (int j = 0; j < nf; ++j) { p.src.push_back (j); p.f0.push_back (m.fr[(size_t) j].f0 * ratio); p.gain.push_back (1); p.inst.push_back (0); p.phase.push_back (0); p.resid.push_back (0); p.air.push_back (0); p.copy.push_back (0); p.lock.push_back (0); }
                auto y = vm::render (m, p);
                auto m2 = vm::analyse (y.data(), (int) y.size(), 48000, [&] (double s2) { const int j = juce::jlimit (0, nf - 1, (int) std::lround (s2 / vm::kHop)); return m.fr[(size_t) j].f0 * ratio; });
                const double sh = shiftCents (envLog (m, ratio), envLog (m2, 1.0));
                check (std::abs (sh) <= 84.0, "a vowel of the " + juce::String (src == sung() ? "synthetic" : "public-domain") + " voice moved " + juce::String (ratio > 1 ? "+12" : "-12") + " semitones keeps its formants within 5 % (the envelope moved " + f2 (sh, 0) + " cents)");
            }
        }
    }
}

// ---------------------------------------------------------------- the phase-locked vocoder (DREAM's freeze, long stretches)
static double peakHz (const Buf& b, int ch, int from, int to, double nearHz)
{   // the frequency of the strongest peak near nearHz (+-3 %), parabolic interpolation on a 65536-point spectrum
    const int N = 65536; juce::dsp::FFT f (16); std::vector<float> buf ((size_t) N * 2, 0.0f);
    const int L = juce::jmin (N, to - from); for (int i = 0; i < L; ++i) buf[(size_t) i] = b.getSample (ch, from + i) * (float) (0.5 - 0.5 * std::cos (6.283185307 * i / L));
    f.performFrequencyOnlyForwardTransform (buf.data());
    const int k0 = (int) (nearHz * 0.97 * N / 48000), k1 = (int) (nearHz * 1.03 * N / 48000); int kb = k0; for (int k = k0; k <= k1; ++k) if (buf[(size_t) k] > buf[(size_t) kb]) kb = k;
    const double a = std::log (buf[(size_t) kb - 1] + 1e-12), bb = std::log (buf[(size_t) kb] + 1e-12), c = std::log (buf[(size_t) kb + 1] + 1e-12);
    return (kb + 0.5 * (a - c) / (a - 2 * bb + c)) * 48000.0 / N;
}
static double amRippleDb (const Buf& b, int from, int to)
{   // the level's wobble over 10 ms blocks (max - min, dB)
    double lo = 1e9, hi = -1e9; for (int s2 = from; s2 + 480 <= to; s2 += 480) { const double r = dsp::gainToDb (dsp::rmsOf (b, s2, s2 + 480)); lo = std::min (lo, r); hi = std::max (hi, r); } return hi - lo;
}
static void plSix()
{
    section ("the phase-locked vocoder: stretches keep pitch, steadiness, attacks and the stereo image");
    const int n = 48000;
    for (int chord = 0; chord < 2; ++chord)
    {
        const std::vector<double> fs = chord ? std::vector<double> { 261.63, 329.63, 392.0 } : std::vector<double> { 440.0 };
        Buf x (1, n); for (int i = 0; i < n; ++i) { double v = 0; for (double f : fs) v += 0.25 * std::sin (6.283185307 * f * i / 48000.0); x.setSample (0, i, (float) v); }
        pl::Spec sp; sp.fftOrder = 12;
        auto y = pl::stretch (x, 48000, 4.0, sp);
        double worst = 0; for (double f : fs) worst = std::max (worst, std::abs (1200.0 * std::log2 (peakHz (y, 0, 20000, y.getNumSamples() - 20000, f) / f)));
        const double rip = amRippleDb (y, 20000, y.getNumSamples() - 20000);
        check (worst <= 1.0 && (chord || rip <= 0.5), juce::String (chord ? "a 3-note chord" : "a sine") + " stretched 4x keeps its pitch (worst " + f2 (worst, 2) + " cents)" + (chord ? juce::String() : ", no AM ripple (" + f2 (rip, 2) + " dB)"));
    }
    {   // a click train stretched 2x: the attacks stay sharp
        Buf x (1, n); x.clear(); for (int c = 4800; c < n - 4800; c += 9600) for (int i = 0; i < 480; ++i) x.setSample (0, c + i, (float) (0.8 * std::exp (-i / 60.0) * std::sin (6.283185307 * 1500.0 * i / 48000.0)));
        pl::Spec sp; sp.fftOrder = 11;
        auto y = pl::stretch (x, 48000, 2.0, sp);
        const double cx = clearm::crestDb (x), cy = clearm::crestDb (y);
        check (std::abs (cx - cy) <= 2.0, "a click train stretched 2x keeps its attacks (crest factor " + f2 (cx, 1) + " -> " + f2 (cy, 1) + " dB)");
    }
    {   // stereo: L / R correlation unchanged
        Buf x (2, n); dsp::Rng r (7); double lp = 0, rp2 = 0;
        for (int i = 0; i < n; ++i) { const double s2 = std::sin (6.283185307 * 330.0 * i / 48000.0) * 0.3 + std::sin (6.283185307 * 495.0 * i / 48000.0) * 0.2; lp = 0.9 * lp + 0.1 * r.sym(); rp2 = 0.9 * rp2 + 0.1 * r.sym(); x.setSample (0, i, (float) (s2 + 0.05 * lp)); x.setSample (1, i, (float) (0.8 * s2 + 0.05 * rp2)); }
        pl::Spec sp; auto y = pl::stretch (x, 48000, 3.0, sp);
        const double c0 = clearm::lrCorr (x, 4800, n - 4800), c1 = clearm::lrCorr (y, 14400, y.getNumSamples() - 14400);
        check (std::abs (c0 - c1) <= 0.03, "a stereo sound stretched 3x keeps its image (L / R correlation " + f2 (c0, 3) + " -> " + f2 (c1, 3) + ")");
    }
}

// ---------------------------------------------------------------- real-time safety of build 6's playback
static void rtSix()
{
    section ("real-time safety (build 6): syllables on the keys, DREAM's space, the meters, the sound's preview at another rate");
    SliceProcessor p; p.setPlayConfigDetails (0, 2, 44100, 512); p.prepareToPlay (44100, 512);
    p.loadSourceForTests (sung()); pump (20);
    p.type = Type::Instrument; p.setKnob (0.3, false); p.generate(); p.waitIdle (90000);
    check (p.currentTake() != nullptr && ! p.currentTake()->sylZones.empty(), "an INSTRUMENT take with syllables on the keys");
    juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer mb;
    gAudioAllocs.store (0);
    for (int i = 0; i < 300; ++i)
    {
        mb.clear(); if (i % 20 == 3) mb.addEvent (juce::MidiMessage::noteOn (1, 57 + (i / 20) % 7, 0.9f), 10); if (i % 20 == 15) mb.addEvent (juce::MidiMessage::noteOff (1, 57 + (i / 20) % 7), 0);
        if (i == 100) p.playSource (true);   // (the sound itself at 44.1 kHz host rate: the sinc read)
        if (i == 200) { p.playSource (false); p.playPreview (true); }
        tlAudio = true; p.processBlock (buf, mb); tlAudio = false;
        if (i % 10 == 0) { p.takeLevel (0); p.takeLevel (1); }
    }
    check (gAudioAllocs.load() == 0, "300 blocks: every key a new syllable, the sound's own preview, the take's preview: " + juce::String (gAudioAllocs.load()) + " allocations");
    p.type = Type::Pad; p.setKnob (1.0, false); p.generate(); p.waitIdle (120000);
    gAudioAllocs.store (0);
    for (int i = 0; i < 200; ++i) { mb.clear(); if (i == 2) mb.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0); tlAudio = true; p.processBlock (buf, mb); tlAudio = false; }
    check (gAudioAllocs.load() == 0 && p.currentTake() != nullptr && p.currentTake()->dream > 0.9f, "a PAD at DREAM 100 % (the bigger space): no allocation");
}

// ---------------------------------------------------------------- VARY: the same idea, clearly varied
static double specDistance (const Buf& a, const Buf& b)
{   // 1 - correlation of the long-term log spectra (0 = the same spectrum)
    auto ls = [] (const Buf& x) { auto m = monoOf (x); const int N = 4096; juce::dsp::FFT f (12); std::vector<float> buf ((size_t) N * 2); std::vector<double> P ((size_t) N / 2, 0.0);
                                  for (int s2 = 0; s2 + N <= m.getNumSamples(); s2 += N / 2) { std::fill (buf.begin(), buf.end(), 0.0f); for (int i = 0; i < N; ++i) buf[(size_t) i] = m.getSample (0, s2 + i) * (float) (0.5 - 0.5 * std::cos (6.283185307 * i / N)); f.performFrequencyOnlyForwardTransform (buf.data()); for (int k = 0; k < N / 2; ++k) P[(size_t) k] += (double) buf[(size_t) k] * buf[(size_t) k]; }
                                  std::vector<double> o; for (int k = 8; k < N / 2; k += 4) o.push_back (std::log (P[(size_t) k] + 1e-12)); return o; };
    return 1.0 - clearm::pearson (ls (a), ls (b));
}
static double onsetDistance (const Buf& a, const Buf& b)
{   // 1 - correlation of the 10 ms level envelopes (the rhythm)
    auto ea = clearm::rmsEnv (a, 480), eb = clearm::rmsEnv (b, 480);
    const size_t n = std::min (ea.size(), eb.size()); ea.resize (n); eb.resize (n);
    return 1.0 - clearm::pearson (ea, eb);
}
static void varySix()
{
    section ("VARY: the same idea (type, key, style, tempo, your boxes), clearly varied (notes, voicing, chops, grains)");
    auto P = prepOf (sung());
    for (auto ty : { Type::Instrument, Type::Pad, Type::Chops, Type::Texture })
    {
        Recipe r; r.type = ty; r.seed = 3131; r.detail = 11; r.musical = 0.8; r.clear = true; r.engine = 6; r.bars = 4;
        auto a = generate (*P, r);
        Recipe v = r; v.detail = 4242; v.variation = 1;
        auto b = generate (*P, v);
        if (a == nullptr || b == nullptr) { check (false, juce::String (typeName (ty)) + ": both takes"); continue; }
        // notes: what differs at each position of the parent's events
        int diff = 0; const size_t n = std::max (a->notes.size(), b->notes.size());
        for (size_t i = 0; i < n; ++i) { if (i >= a->notes.size() || i >= b->notes.size()) { ++diff; continue; } const auto& x = a->notes[i]; const auto& y = b->notes[i]; if (x.note != y.note || std::abs (x.beat - y.beat) > 0.05 || x.chop != y.chop) ++diff; }
        const double share = n ? (double) diff / n : 0.0, sd = specDistance (a->preview, b->preview), od = onsetDistance (a->preview, b->preview);
        const bool kept = a->type == b->type && a->keyTonic == b->keyTonic && a->keyMinor == b->keyMinor && a->style == b->style && a->bpm == b->bpm;
        const bool audible = ty == Type::Texture ? (sd > 0.002 || od > 0.05) : (share >= 0.30 && (sd > 0.002 || od > 0.05));
        check (kept && audible, juce::String (typeName (ty)) + " (" + styleName (a->style) + "): VARY keeps type / key / style / tempo and varies it (" + juce::String ((int) std::lround (share * 100)) + " % of note positions differ, spectrum " + f2 (sd, 3) + ", rhythm " + f2 (od, 3) + ")");
    }
    {   // the processor: VARY is "Take 1b", keeps the edited boxes
        ghosty::License::testForce().store (1);
        SliceProcessor p; p.prepareToPlay (48000, 512); p.loadSourceForTests (sung()); pump (20); p.waitIdle (60000);
        p.type = Type::Chops; p.generate(); p.waitIdle (60000);
        SliceProcessor::Parts mine { { 0.30, 0.55 }, { 1.20, 1.50 }, { 2.40, 2.70 } }; p.editParts (mine); p.waitIdle (60000);
        p.vary(); p.waitIdle (60000);
        check (p.currentTake() != nullptr && p.currentTake()->recipe.name.startsWith ("Take 1b") && p.takeParts() == mine, "VARY in the plug-in: \"" + (p.currentTake() != nullptr ? p.currentTake()->recipe.name : juce::String()) + "\", the user's boxes kept");
        ghosty::License::testForce().store (-1);
    }
}

// ---------------------------------------------------------------- the A/B demos (24-bit 48 kHz) for the owner
static std::shared_ptr<const Take> take6 (std::shared_ptr<const Prepared> P, Type ty, double knob, Style st, int bars = 4, int engine = 6, uint32_t seed = 2027, uint32_t detail = 9, int variation = 0)
{
    Recipe r; r.type = ty; r.seed = seed; r.detail = detail; r.musical = 0.85; r.clear = true; r.engine = engine; r.knob = knob; r.style = st; r.bars = bars; r.variation = variation;
    return generate (*P, r);
}
static Style styleNamed (const char* n) { return styleFromName (n); }
static void abDemos (const juce::File& dir)
{
    section ((juce::String ("A/B demos (24-bit 48 kHz): ") + dir.getFullPathName()).toRawUTF8());
    dir.createDirectory();
    auto src = pdVoice() != nullptr ? pdVoice() : sung();
    auto P = prepOf (src); auto PS = prepOf (sung());
    struct Item { juce::String file, what; Buf audio; };
    std::vector<Item> items;
    auto add = [&] (const juce::String& f, const juce::String& what, std::shared_ptr<const Take> t, float space = 0.3f)
    {
        if (t == nullptr) { check (false, f + ": rendered"); return; }
        auto w = renderForExport (*t, 0.0f, space, 0.55f, 0.35f); juce::String err;
        const bool ok = exporter::writeWav (w, 48000, dir.getChildFile (f + ".wav"), 24, err);
        check (ok && dsp::finiteAndClean (w) && dsp::clicksIn (w) == 0, f + ".wav: " + what + " (" + f2 (w.getNumSamples() / 48000.0, 1) + " s, no clicks)");
        items.push_back ({ f, what, w });
    };
    { Buf s2; s2.makeCopyOf (src->audio); juce::String err; exporter::writeWav (dsp::stereoOf (s2), 48000, dir.getChildFile ("00-source-voice.wav"), 24, err); items.push_back ({ "00-source-voice", "the source: a public-domain spoken voice (LibriVox, Christina Rossetti's \"A Birthday\", Public Domain Mark 1.0)", dsp::stereoOf (s2) }); }
    add ("01-instrument-OLD-build5", "OLD: build 5's INSTRUMENT (PSOLA) on the same voice, same seed", take6 (P, Type::Instrument, -1, Style::Hook, 4, 5));
    add ("02-instrument-NEW-build6", "NEW: build 6's INSTRUMENT, VOICE all the way to INSTRUMENT, same seed", take6 (P, Type::Instrument, 1.0, Style::Hook));
    add ("03-voice-0", "VOICE 0 %: your real recording, its words, in the melody's timing (re-pitched to the melody)", take6 (P, Type::Instrument, 0.0, Style::Hook));
    add ("04-voice-50", "VOICE 50 %: half voice, half instrument", take6 (P, Type::Instrument, 0.5, Style::Hook));
    add ("05-voice-100", "VOICE 100 %: the pure instrument made from the voice", take6 (P, Type::Instrument, 1.0, Style::Hook));
    add ("06-pad-dream-0", "PAD at DREAM 0 %: clear, glassy", take6 (P, Type::Pad, 0.0, Style::Dreamy), 0.3f);
    add ("07-pad-dream-100", "PAD at DREAM 100 %: longer, dreamier freeze, octave shimmer, wide chords, big space", take6 (P, Type::Pad, 1.0, Style::Dreamy), 0.3f);
    add ("08-style-HOOK", "melody style HOOK (INSTRUMENT, VOICE 60 %)", take6 (P, Type::Instrument, 0.6, Style::Hook));
    add ("09-style-MELANCHOLIC", "melody style REVERIE MELANCHOLIC (INSTRUMENT, VOICE 60 %)", take6 (P, Type::Instrument, 0.6, styleNamed ("MELANCHOLIC")));
    add ("10-style-POP-BRAIN", "melody style POP BRAIN (INSTRUMENT, VOICE 60 %)", take6 (P, Type::Instrument, 0.6, styleNamed ("POP BRAIN")));
    add ("11-vary-parent", "VARY pair, 1: the take (CHOPS, HOOK)", take6 (P, Type::Chops, -1, Style::Hook));
    add ("12-vary-child", "VARY pair, 2: its VARY (same idea: new answer phrase, other chops, new order)", take6 (P, Type::Chops, -1, Style::Hook, 4, 6, 2027, 5555, 1));
    add ("13-length-16-bars", "LENGTH 16 bars: the melody develops (A A' A B, then lifted and answered anew), INSTRUMENT VOICE 70 %", take6 (P, Type::Instrument, 0.7, Style::Hook, 16));
    const std::vector<std::pair<const char*, const char*>> groups { { "GHOST-SLICE", "DREAMY" }, { "PIANO", "LULLABY" }, { "REVERIE", "MINIMAL" }, { "SYNTH", "ACID" }, { "LEARNED", "ACID BRAIN" },
                                                                   { "ARPEZATOR", "MIDNIGHT ARP" }, { "HOOK-LAB", "EMOTIONAL" }, { "TRAP", "EMO" }, { "HIP-HOP", "JAZZ SOUL" } };
    int gi = 14;
    for (auto& [g, st] : groups) add (juce::String (gi++) + "-group-" + g + "-" + juce::String (st).replace (" ", "-"), juce::String ("group ") + g + ": " + st + " (CHOPS of the voice for the rhythmic ones, INSTRUMENT VOICE 60 % for the rest)",
                                      take6 (P, juce::String (g) == "SYNTH" || juce::String (g) == "LEARNED" || juce::String (g) == "HIP-HOP" ? Type::Chops : Type::Instrument, juce::String (g) == "SYNTH" || juce::String (g) == "LEARNED" || juce::String (g) == "HIP-HOP" ? -1 : 0.6, styleNamed (st)));
    add (juce::String (gi++) + "-texture-drift", "TEXTURE (DRIFT 70 %)", take6 (P, Type::Texture, 0.7, Style::Ambient), 0.3f);
    add (juce::String (gi++) + "-instrument-synthetic-voice", "INSTRUMENT on our synthetic sung test voice (VOICE 40 %)", take6 (PS, Type::Instrument, 0.4, Style::Dreamy));
    // build 7: the PARLOUR PIANO group (WISTFUL, the learned parlour piano), INSTRUMENT VOICE 60 %, LENGTH 8
    add (juce::String (gi++) + "-group-PARLOUR-PIANO", "group PARLOUR PIANO: WISTFUL (INSTRUMENT VOICE 60 %, 8 bars)", take6 (P, Type::Instrument, 0.6, styleNamed ("WISTFUL"), 8));
    // demo-all: everything in order, 1 s apart, and the list of what plays when
    int total = 0; for (auto& it : items) total += it.audio.getNumSamples() + 48000;
    Buf all (2, total); all.clear(); int at = 0; juce::String list;
    list << "Ghost Slice build 7 - demo-all.wav (24-bit 48 kHz). What plays when:\n";
    for (auto& it : items)
    {
        for (int ch = 0; ch < 2; ++ch) all.copyFrom (ch, at, it.audio, juce::jmin (ch, it.audio.getNumChannels() - 1), 0, it.audio.getNumSamples());
        const int sec = at / 48000; list << juce::String (sec / 60) << ":" << juce::String (sec % 60).paddedLeft ('0', 2) << "  " << it.file << ".wav  -  " << it.what << "\n";
        at += it.audio.getNumSamples() + 48000;
    }
    juce::String err; exporter::writeWav (all, 48000, dir.getChildFile ("demo-all.wav"), 24, err);
    dir.getChildFile ("demo-all.txt").replaceWithText (list);
    check (dir.getChildFile ("demo-all.wav").existsAsFile(), "demo-all.wav (" + f2 (total / 48000.0 / 60.0, 1) + " min) + demo-all.txt");
}

static void clkDebug()
{
    auto src = pdVoice() != nullptr ? pdVoice() : sung(); auto P = prepOf (src);
    for (const char* st : { "POP BRAIN", "DREAMY" })
    {
        auto t = take6 (P, Type::Instrument, 0.6, styleNamed (st));
        const double spb = 60.0 / t->bpm;
        auto w = renderForExport (*t, 0.0f, 0.3f, 0.55f, 0.35f);
        std::printf ("  %s: %d clicks in the export, %d in the preview\n", st, dsp::clicksIn (w), dsp::clicksIn (t->preview));
        for (int blk = 0; blk + 2400 < t->preview.getNumSamples(); blk += 2400)
        {
            Buf part (2, 2400 + 512); for (int ch = 0; ch < 2; ++ch) part.copyFrom (ch, 0, t->preview, ch, juce::jmax (0, blk - 256), 2400 + 512);
            if (dsp::clicksIn (part) > 0)
            {
                double near = 1e9; int ni = -1; for (size_t k = 0; k < t->notes.size(); ++k) { const double o = t->notes[k].beat * spb * 48000.0; if (std::abs (blk - o) < std::abs (near)) { near = blk - o; ni = (int) k; } }
                const double e = (t->notes[(size_t) ni].beat + t->notes[(size_t) ni].length) * spb * 48000.0;
                std::printf ("    block %d: nearest note %d (onset %+.0f, end %+.0f) syl %d prevNoteEnd %+.0f\n", blk, ni, near, blk - e, ni, ni > 0 ? blk - (t->notes[(size_t) ni - 1].beat + t->notes[(size_t) ni - 1].length) * spb * 48000.0 : 0.0);
                const float* x = t->preview.getReadPointer (0); float worst = 0; int wi = 0;
                for (int i = juce::jmax (1, blk - 256); i < juce::jmin (t->preview.getNumSamples() - 1, blk + 2656); ++i) { const float d2 = std::abs (x[i + 1] - 2 * x[i] + x[i - 1]); if (d2 > worst) { worst = d2; wi = i; } }
                std::printf ("      worst d2 %.3f at %d (onset %+.0f): ", worst, wi, wi - t->notes[(size_t) ni].beat * spb * 48000.0); for (int i = wi - 3; i <= wi + 3; ++i) std::printf ("%.3f ", x[i]); std::printf ("\n");
            }
        }
    }
}

static void writeF32 (const Buf& b, const juce::File& f)
{
    f.deleteFile(); auto os = std::make_unique<juce::FileOutputStream> (f); juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), 48000, (unsigned) b.getNumChannels(), 32, {}, 0));   // NOT-EXPORT: test dumps
    if (w != nullptr) { os.release(); w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples()); }
}
static void vdump (const juce::File& dir)
{
    dir.createDirectory();
    for (auto src : { sung(), pdVoice() })
    {
        if (src == nullptr) continue;
        const juce::String nm = src == sung() ? "syn" : "pd";
        auto P = prepOf (src); writeF32 (P->work->buffer(), dir.getChildFile (nm + "-work.wav"));
        for (auto [v, mu, tag] : std::vector<std::tuple<double, double, const char*>> { { 0.0, 0.0, "v0nat" }, { 0.0, 1.0, "v0" }, { 0.5, 1.0, "v50" }, { 0.75, 1.0, "v75" }, { 1.0, 1.0, "v100" } })
        {
            auto t = inst6 (P, v, mu); if (t == nullptr) continue;
            writeF32 (t->preview, dir.getChildFile (nm + "-" + tag + ".wav"));
            juce::String js; js << "{\"bpm\":" << t->bpm << ",\"notes\":["; for (size_t i = 0; i < t->notes.size(); ++i) js << (i ? "," : "") << "[" << t->notes[i].beat << "," << t->notes[i].length << "," << t->notes[i].note << "]";
            js << "],\"used\":["; for (size_t i = 0; i < t->used.size(); ++i) js << (i ? "," : "") << "[" << t->used[i].first << "," << t->used[i].second << "]"; js << "]}";
            dir.getChildFile (nm + "-" + tag + ".json").replaceWithText (js);
        }
    }
}

static void exp2()
{
    for (int v = 0; v < 2; ++v)
    {
        auto P = prepOf (v == 0 ? spoken() : sung());
        for (int st = 0; st < styleCount(); ++st)
            for (int ty : { 1, 3 })
            {
                Recipe r; r.type = (Type) ty; r.seed = 101 + (uint32_t) ty; r.detail = 7; r.musical = 1.0; r.style = (Style) st; if (ty == 1) r.knob = 1.0;
                if (ty == 3 && juce::SystemStats::getEnvironmentVariable ("GS_KNOB", "").isNotEmpty()) r.knob = juce::SystemStats::getEnvironmentVariable ("GS_KNOB", "").getDoubleValue();
                if (ty == 3 && juce::SystemStats::getEnvironmentVariable ("GS_GATE", "").isNotEmpty()) r.deep[3] = juce::SystemStats::getEnvironmentVariable ("GS_GATE", "").getDoubleValue();
                if (juce::SystemStats::getEnvironmentVariable ("GS_STYLE", "").isNotEmpty() && juce::SystemStats::getEnvironmentVariable ("GS_STYLE", "") != styleName ((Style) st)) continue;
                const auto t0 = juce::Time::getMillisecondCounterHiRes();
                auto t = generate (*P, r); if (t == nullptr) continue;
                const Key k { t->keyTonic, t->keyMinor }; int fr = 0; const double pk = pitchInKey (t->preview, k, 35, &fr);
                std::printf ("  %s %s %-8s chroma %.0f %% pitch %.0f %% (%d) top7k %.2f %% hnr %.1f dB %.0f ms\n", v ? "sung" : "spoken", typeName (t->type), styleName (t->style), chromaInKey (t->preview, k) * 100, pk * 100, fr, highShare (t->preview, 7000) * 100, clearm::hnr (t->preview, 0, t->preview.getNumSamples()), juce::Time::getMillisecondCounterHiRes() - t0);
            }
    }
}
} // namespace six
