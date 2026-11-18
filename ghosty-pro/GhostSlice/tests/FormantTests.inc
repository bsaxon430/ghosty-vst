// GhostSliceTests: phase 15 - our own TD-PSOLA VOCAL engine + the WIDE engine's LPC formant correction. Included by SliceTests.cpp
// after PitchEditTests.inc (it shares voice / melody / centroid / trackOf / centsIn / stddev there). Groups: formant, renderhashes

// ---------------------------------------------------------------- render hashes (old-vs-new bit identity of renders without note edits)
static uint64 bufferHash (const juce::AudioBuffer<float>& b, int from = 0, int to = -1)
{
    if (to < 0) to = b.getNumSamples();
    uint64 h = 1469598103934665603ull;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = juce::jmax (0, from); i < to; ++i) { uint32_t u; const float v = b.getSample (c, i); std::memcpy (&u, &v, 4); h = (h ^ u) * 1099511628211ull; }
    return h;
}

static void renderHashes()
{
    auto line = [] (const char* what, const juce::AudioBuffer<float>& b) { std::printf ("  hash %-28s %016llx (%d frames)\n", what, (unsigned long long) bufferHash (b), b.getNumSamples()); };
    Rig rig (stereoOf (voice (48000, fourNotes())));
    { Project q = rig->project(); q.material = "MELODIC"; rig->apply (q, "melodic"); }
    rig->waitAnalysis (30000);
    line ("plain", rig.render());
    const Project base = rig->project();
    { auto q = ops::stretchTo (base, base.regions[0].id, base.regions[0].length() * 1.3); rig->apply (*q, "s"); line ("stretch x1.3", rig.render()); rig->apply (base, "b"); }
    { Project q = base; q.pitchSemis = 3; q.formantSemis = 1; rig->apply (q, "p"); line ("global pitch +3 / formant +1", rig.render()); rig->apply (base, "b"); }
    { Project q = base; q.pitchSemis = -5; rig->apply (q, "p"); line ("global pitch -5", rig.render()); rig->apply (base, "b"); }
    {
        auto notes = notesNow (*rig.p);
        if (notes.size() == 4)
        {
            rig->apply (*ops::shiftNotes (base, notes[1].regionId, { notes[1], notes[2] }, 200.0, true), "n");
            const auto r = rig.render(); line ("notes 2+3 +2 st (edited)", r);
            std::printf ("  hash %-28s %016llx\n", "  note 1 span", (unsigned long long) bufferHash (r, (int) notes[0].s0, (int) (notes[0].s1 - 2400)));
            std::printf ("  hash %-28s %016llx\n", "  note 4 span", (unsigned long long) bufferHash (r, (int) (notes[3].s0 + 2400), (int) notes[3].s1));
            rig->apply (base, "b");
        }
    }
}

// ---------------------------------------------------------------- shared helpers
#include "../Psola.h"
#include "../FormantFix.h"
// a one-note project of the narrow-formant vowel (melody(): F 650 / 1100 / 2600 Hz, bandwidths 80 / 120 / 160 Hz, -6 dB / oct)
struct OneNote
{
    std::unique_ptr<Rig> rig; pitchedit::NoteRef note; bool ok = false;
    OneNote (double midi, double secs, double vibHz = 0, double vibCents = 0, bool formants = true)
    {
        rig = std::make_unique<Rig> (stereoOf (melody (48000, { { midi, secs, vibHz, vibCents } }, formants)));
        { Project q = (*rig)->project(); q.material = "MELODIC"; (*rig)->apply (q, "melodic"); }
        (*rig)->waitAnalysis (30000);
        auto n = notesNow (*rig->p); ok = n.size() == 1; if (ok) note = n[0];
    }
    // shift the note (cents) with an engine (-1 = the project's) + formant / vibrato / drift; render STUDIO
    juce::AudioBuffer<float> edit (double cents, int engine, double formantCents = 0, double vib = 1, double drift = 1)
    {
        Project base = (*rig)->project(); for (auto& r : base.regions) r.pitch.clear();
        auto q = ops::editNotes (base, note.regionId, { note }, [=] (PitchEdit& e) { e.cents = cents; e.engine = engine; e.formantCents = formantCents; e.vibrato = vib; e.drift = drift; });
        (*rig)->apply (q ? *q : base, "edit");
        return rig->render();
    }
    int64 mid0() const { return note.s0 + (note.s1 - note.s0) / 5; }
    int64 mid1() const { return note.s1 - (note.s1 - note.s0) / 5; }
};
static double centreCents (const juce::AudioBuffer<float>& b, int64 s0, int64 s1) { return pitchedit::centreOf (trackOf (b), s0, s1); }

static void formantLab()
{
    std::printf ("  formant lab: centroid of the middle 60 %% vs the source / vs the same vowel synthesised at the target pitch\n");
    const juce::String only = std::getenv ("GS_LAB_MIDI") ? std::getenv ("GS_LAB_MIDI") : "";
    for (double midi : { 55.0, 60.0, 64.0, 67.0 })
        for (double st : { -4.0, -2.0, 2.0, 4.0 })
        {
            if (only.isNotEmpty() && ! only.contains (juce::String ((int) midi))) continue;
            OneNote o (midi, 1.2);
            if (! o.ok) { std::printf ("  midi %.0f: note not found\n", midi); continue; }
            const auto src = o.rig->render();
            const auto ref = stereoOf (melody (48000, { { midi + st, 1.2 } }, true));
            const double cs = centroid (src, (int) o.mid0(), (int) o.mid1()), cr = centroid (ref, (int) o.mid0(), (int) o.mid1());
            formantfix::enabled = false; const auto wideOld = o.edit (st * 100, 2);
            formantfix::enabled = true;  const auto wideFix = o.edit (st * 100, 2);
            formantfix::enabled = false; const auto vocRaw = o.edit (st * 100, 1); formantfix::enabled = true;
            const auto voc = o.edit (st * 100, 1);
            auto c = [&] (const juce::AudioBuffer<float>& b) { return centroid (b, (int) o.mid0(), (int) o.mid1()); };
            auto p = [&] (const juce::AudioBuffer<float>& b) { return centreCents (b, o.mid0(), o.mid1()) - midi * 100 - st * 100; };
            std::printf ("  midi %.0f %+.0f st | ideal %+.1f %% | WIDE %+.1f %% (%+.1f ref, %+.1f ct) | WIDE+fix %+.1f %% (%+.1f ref, %+.1f ct) | VOCAL %+.1f %% (%+.1f ref, %+.1f ct) | raw PSOLA %+.1f %%\n",
                         midi, st, (cr / cs - 1) * 100, (c (wideOld) / cs - 1) * 100, (c (wideOld) / cr - 1) * 100, p (wideOld),
                         (c (wideFix) / cs - 1) * 100, (c (wideFix) / cr - 1) * 100, p (wideFix), (c (voc) / cs - 1) * 100, (c (voc) / cr - 1) * 100, p (voc), (c (vocRaw) / cs - 1) * 100);
        }
}


// ---------------------------------------------------------------- phase 15 tests
// a glottal pulse train: Rosenberg (1971) flow pulses (open 40 %, closing 16 % of the period), differentiated, through three
// formant resonators; the glottal closure instant (the end of the closing phase) of every period is returned in samples
static juce::AudioBuffer<float> glottalTrain (double sr, double secs, std::vector<double>& gci)
{
    const int n = (int) (secs * sr);
    std::vector<double> u ((size_t) n, 0.0);
    double ph = 0; const double Tp = 0.40, Tn = 0.16;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        double f0 = t < 0.8 ? 120.0 * std::pow (220.0 / 120.0, t / 0.8) : 220.0 * std::pow (2.0, 50.0 / 1200.0 * std::sin (2 * juce::MathConstants<double>::pi * 5.5 * (t - 0.8)));
        const double prev = ph; ph += f0 / sr;
        if (prev < Tp + Tn && ph >= Tp + Tn) gci.push_back (i - 1 + (Tp + Tn - prev) / (ph - prev));   // the crossing, sub-sample
        if (ph >= 1.0) { ph -= 1.0; if (ph >= Tp + Tn) gci.push_back (i - 1 + (1.0 + Tp + Tn - prev) / (ph + 1.0 - prev)); }
        u[(size_t) i] = ph < Tp ? 0.5 * (1.0 - std::cos (juce::MathConstants<double>::pi * ph / Tp)) : ph < Tp + Tn ? std::cos (juce::MathConstants<double>::pi * (ph - Tp) / (2 * Tn)) : 0.0;
    }
    std::vector<double> y ((size_t) n, 0.0);
    for (int i = 1; i < n; ++i) y[(size_t) i] = u[(size_t) i] - u[(size_t) i - 1];
    static const double F[3] { 700, 1220, 2600 }, BW[3] { 80, 100, 140 };
    for (int k = 0; k < 3; ++k)
    {
        const double r = std::exp (-juce::MathConstants<double>::pi * BW[k] / sr), th = 2 * juce::MathConstants<double>::pi * F[k] / sr;
        const double a1 = 2 * r * std::cos (th), a2 = -r * r, g = 1 - r;
        double y1 = 0, y2 = 0;
        for (int i = 0; i < n; ++i) { const double v = g * y[(size_t) i] + a1 * y1 + a2 * y2; y2 = y1; y1 = v; y[(size_t) i] = v; }
    }
    double pk = 0; for (auto v : y) pk = std::max (pk, std::abs (v));
    juce::AudioBuffer<float> b (1, n);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr, env = std::min ({ 1.0, t / 0.02, (secs - t) / 0.02 });
        b.setSample (0, i, (float) (0.5 * env * y[(size_t) i] / pk));
    }
    return b;
}

static void testFormant()
{
    section ("[21] GHOST PITCH engines: VOCAL (our TD-PSOLA) + WIDE formant correction, ENGINE AUTO / VOCAL / WIDE (phase 15)");
    {   // epoch marking on a synthetic glottal pulse train (the f0 from our own tracker)
        std::vector<double> gci;
        const auto g = glottalTrain (48000, 1.6, gci);
        const auto x = an::mono (g);
        const auto tr = an::trackPitch (x.data(), (int64) x.size(), 48000.0);
        auto f0At = [&] (int64 i) { const int f = (int) std::lround ((double) i / tr.hop); return f >= 0 && f < tr.frames() ? (double) tr.f0[(size_t) f] : 0.0; };
        const auto e1 = psola::markEpochs (x.data(), (int64) x.size(), 48000.0, f0At), e2 = psola::markEpochs (x.data(), (int64) x.size(), 48000.0, f0At);
        bool same = e1.size() == e2.size(); for (size_t i = 0; same && i < e1.size(); ++i) same = e1[i].pos == e2[i].pos;
        int hits = 0, total = 0, spurious = 0, inside = 0; double worst = 0, sumErr = 0, bias = 0;
        for (double t : gci)
        {
            if (t < 0.1 * 48000 || t > 1.5 * 48000) continue;
            ++total; double best = 1e9, sgn = 0; for (auto& e : e1) if (std::abs (e.pos - t) < best) { best = std::abs (e.pos - t); sgn = e.pos - t; }
            bias += sgn;
            if (best <= 0.0002 * 48000) { ++hits; sumErr += best; }
            worst = std::max (worst, best);
        }
        for (auto& e : e1)
        {
            if (e.pos < 0.1 * 48000 || e.pos > 1.5 * 48000) continue;
            ++inside; double best = 1e9; for (double t : gci) best = std::min (best, std::abs (e.pos - t));
            if (best > 0.0008 * 48000) ++spurious;
        }
        const double rate = total > 0 ? 100.0 * hits / total : 0;
        check (rate >= 98.0 && spurious <= inside / 50 && same,
               "epochs on a glottal pulse train (Rosenberg pulses, 120 -> 220 Hz glide + 5.5 Hz vibrato, narrow formants): " + juce::String (rate, 1) + " % of "
               + juce::String (total) + " closures within 0.2 ms (mean " + juce::String (hits > 0 ? sumErr / hits / 48.0 * 1000.0 : 0.0, 0) + " us, worst " + juce::String (worst / 48.0, 2)
               + " ms, mean signed " + juce::String (total > 0 ? bias / total / 48.0 * 1000.0 : 0.0, 0) + " us), " + juce::String (spurious) + " spurious of " + juce::String (inside) + ", deterministic");
    }
    {   // an impulse train through a resonator: the residual peak IS the epoch (exact to the sample)
        const int n = 48000; std::vector<float> x ((size_t) n, 0.0f); std::vector<double> imp;
        for (double t = 1000.3; t < n - 1000; t += 320.0) { x[(size_t) std::lround (t)] = 1.0f; imp.push_back (std::round (t)); }
        const double r = std::exp (-juce::MathConstants<double>::pi * 100 / 48000.0), th = 2 * juce::MathConstants<double>::pi * 700 / 48000.0;
        double y1 = 0, y2 = 0; for (int i = 0; i < n; ++i) { const double v = x[(size_t) i] + 2 * r * std::cos (th) * y1 - r * r * y2; y2 = y1; y1 = v; x[(size_t) i] = (float) (0.1 * v); }
        const auto e = psola::markEpochs (x.data(), n, 48000.0, [] (int64) { return 150.0; });
        double worst = 0; int cnt = 0;
        for (auto& ep : e) { if (ep.pos < 2000 || ep.pos > n - 2000) continue; ++cnt; double best = 1e9; for (double t : imp) best = std::min (best, std::abs (ep.pos - t)); worst = std::max (worst, best); }
        check (cnt >= 130 && worst < 0.05, "epochs of an impulse train through a 700 Hz resonator: " + juce::String (cnt) + " epochs, each on its impulse (worst " + juce::String (worst, 3) + " samples)");
    }
    {   // pitch accuracy after VOCAL shifts of +-2 / +-4 st (two voices of the narrow-formant vowel)
        double worst = 0, lvlLo = 0, lvlHi = 0; juce::String got;
        auto rmsDb = [] (const juce::AudioBuffer<float>& b, int64 a, int64 z) { double e = 0; for (int64 i = a; i < z; ++i) e += (double) b.getSample (0, (int) i) * b.getSample (0, (int) i); return 10.0 * std::log10 (e / (double) (z - a) + 1e-20); };
        for (double midi : { 57.0, 64.0 })
        {
            OneNote o (midi, 1.2);
            if (! o.ok) { check (false, "VOCAL pitch: the test note was found"); return; }
            const double l0 = rmsDb (o.rig->render(), o.mid0(), o.mid1());
            for (double st : { -4.0, -2.0, 2.0, 4.0 })
            {
                const auto out = o.edit (st * 100, (int) PitchEngine::Vocal);
                const double err = centreCents (out, o.mid0(), o.mid1()) - (midi + st) * 100.0;
                worst = std::max (worst, std::abs (err)); got << juce::String (err, 1) << " ";
                const double dl = rmsDb (out, o.mid0(), o.mid1()) - l0; lvlLo = std::min (lvlLo, dl); lvlHi = std::max (lvlHi, dl);
            }
        }
        check (worst <= 5.0 && lvlLo >= -1.5 && lvlHi <= 1.5, "VOCAL lands on pitch: shifts of -4 / -2 / +2 / +4 st on A3 and E4 within 5 ct (worst " + juce::String (worst, 2) + " ct: " + got.trim()
               + "), level " + juce::String (lvlLo, 2) + " .. " + juce::String (lvlHi, 2) + " dB of the source");
    }
    {   // VIBRATO x0 / x2 with the VOCAL engine (AUTO picks it): the wobble goes / doubles; WIDE for comparison
        OneNote o (64, 1.8, 5.0, 40);
        if (o.ok)
        {
            const int64 a = o.mid0(), b = o.mid1();
            const double s1 = stddev (centsIn (trackOf (o.rig->render()), a, b));
            const double v0 = stddev (centsIn (trackOf (o.edit (0, -1, 0, 0.0)), a, b)), v2 = stddev (centsIn (trackOf (o.edit (0, -1, 0, 2.0)), a, b));
            const bool vocal = (*o.rig)->currentPlan().pieces.size() > 0 && (*o.rig)->currentPlan().pieces[0].curve != nullptr && (*o.rig)->currentPlan().pieces[0].curve->anyVocal();
            const double w0 = stddev (centsIn (trackOf (o.edit (0, (int) PitchEngine::Wide, 0, 0.0)), a, b)), w2 = stddev (centsIn (trackOf (o.edit (0, (int) PitchEngine::Wide, 0, 2.0)), a, b));
            std::printf ("    vibrato f0 std: x1 %.1f ct | VOCAL x0 %.2f x2 %.1f | WIDE x0 %.1f x2 %.1f\n", s1, v0, v2, w0, w2);
            check (vocal && v0 <= 0.10 * s1 && v2 >= 1.8 * s1, "VIBRATO on a 5 Hz +-40 ct vibrato, VOCAL (AUTO): x0 " + juce::String (s1, 1) + " -> " + juce::String (v0, 2) + " ct ("
                   + juce::String (100.0 * v0 / s1, 1) + " %, limit 10 %), x2 -> " + juce::String (v2, 1) + " ct (x" + juce::String (v2 / s1, 2) + ", limit 1.8); WIDE x0 "
                   + juce::String (w0, 1) + " / x2 " + juce::String (w2, 1));
        }
        else check (false, "VIBRATO: the test note was found");
    }
    {   // formant keeping on the narrow-formant vowel (E4; F 650 / 1100 / 2600 Hz, bandwidths 80 - 160 Hz)
        OneNote o (64, 1.2);
        if (! o.ok) { check (false, "formants: the test note was found"); return; }
        const auto src = o.rig->render();
        const double cs = centroid (src, (int) o.mid0(), (int) o.mid1());
        double wv = 0, ww = 0, wb = 0; juce::String sv, sw, sb;
        for (double st : { -4.0, -2.0, 2.0, 4.0 })
        {
            formantfix::enabled = false;
            const double b = centroid (o.edit (st * 100, (int) PitchEngine::Wide), (int) o.mid0(), (int) o.mid1()) / cs - 1;
            formantfix::enabled = true;
            const double w = centroid (o.edit (st * 100, (int) PitchEngine::Wide), (int) o.mid0(), (int) o.mid1()) / cs - 1;
            const double v = centroid (o.edit (st * 100, (int) PitchEngine::Vocal), (int) o.mid0(), (int) o.mid1()) / cs - 1;
            wb = std::max (wb, std::abs (b)); ww = std::max (ww, std::abs (w)); wv = std::max (wv, std::abs (v));
            sb << juce::String (b * 100, 1) << " "; sw << juce::String (w * 100, 1) << " "; sv << juce::String (v * 100, 1) << " ";
        }
        check (wv <= 0.02, "VOCAL keeps the narrow vowel's formants: centroid drift at -4 / -2 / +2 / +4 st " + sv.trim() + " % (worst " + juce::String (wv * 100, 2) + " %, limit 2 %)");
        check (ww <= 0.02 && wb > 0.03, "WIDE + the formant correction: " + sw.trim() + " % (worst " + juce::String (ww * 100, 2) + " %, limit 2 %); without it (phase 14) " + sb.trim() + " % (worst "
               + juce::String (wb * 100, 1) + " %)");
        // a FORMANT offset still moves the envelope on purpose (both engines)
        const double fv = centroid (o.edit (200, (int) PitchEngine::Vocal, 300), (int) o.mid0(), (int) o.mid1()) / cs, fw = centroid (o.edit (200, (int) PitchEngine::Wide, 300), (int) o.mid0(), (int) o.mid1()) / cs;
        check (fv > 1.08 && fw > 1.08, "FORMANT +3 st on a +2 st note still moves the envelope: centroid x" + juce::String (fv, 3) + " (VOCAL) / x" + juce::String (fw, 3) + " (WIDE)");
    }
    {   // the line: unedited notes play the source bit for bit (VOCAL), no clicks, deterministic; WIDE leaves unedited frames alone
        Rig rig (stereoOf (voice (48000, fourNotes())));
        { Project q = rig->project(); q.material = "MELODIC"; rig->apply (q, "melodic"); }
        rig->waitAnalysis (30000);
        auto notes = notesNow (*rig.p);
        if (notes.size() != 4) { check (false, "the line: 4 notes"); return; }
        const auto src = rig->getSource()->audio;
        const Project base = rig->project();
        auto spansEqual = [&] (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int64 s0, int64 s1)
        {
            for (int c = 0; c < 2; ++c) if (std::memcmp (a.getReadPointer (juce::jmin (c, a.getNumChannels() - 1)) + s0, b.getReadPointer (juce::jmin (c, b.getNumChannels() - 1)) + s0, sizeof (float) * (size_t) (s1 - s0)) != 0) return false;
            return true;
        };
        rig->apply (*ops::shiftNotes (base, notes[1].regionId, { notes[1], notes[2] }, 400.0, true), "+4");
        const auto v1 = rig.render(), v2 = rig.render();
        const bool vocal = rig->currentPlan().pieces[0].curve != nullptr && rig->currentPlan().pieces[0].curve->anyVocal();
        const int64 m = 2400;   // 50 ms off the edited notes (the 40 ms transition + the 10 ms join)
        const bool untouched = spansEqual (v1, src, 0, notes[1].s0 - m) && spansEqual (v1, src, notes[2].s1 + m, src.getNumSamples());
        const float stepSrc = maxStep (src), stepOut = maxStep (v1);
        check (vocal && untouched && bitEqual (v1, v2) && stepOut <= 1.5f * stepSrc,
               "VOCAL on two notes +4 st: the unedited notes (and the silences) are the source bit for bit, the render is deterministic, max step "
               + juce::String (stepOut, 4) + " (the source's " + juce::String (stepSrc, 4) + ")");
        double worst = 0; const double want[4] { 6000, 6600, 6900, 6400 };
        const auto tv = trackOf (v1);
        for (int k = 0; k < 4; ++k) worst = std::max (worst, std::abs (pitchedit::centreOf (tv, notes[(size_t) k].s0, notes[(size_t) k].s1) - want[k]));
        check (worst <= 5.0, "VOCAL: the line re-detects as C4 F#4 A4 E4 within 5 ct (worst " + juce::String (worst, 2) + " ct)");
        // WIDE on the same notes: the correction leaves the unedited frames as the stretcher made them
        Project qw = *ops::shiftNotes (base, notes[1].regionId, { notes[1], notes[2] }, 400.0, true);
        for (auto& e : qw.regions[0].pitch) e.engine = (int) PitchEngine::Wide;
        rig->apply (qw, "wide");
        formantfix::enabled = false; const auto w0 = rig.render(); formantfix::enabled = true;
        const auto w1 = rig.render();
        const bool wideNoVocal = rig->currentPlan().pieces[0].curve != nullptr && ! rig->currentPlan().pieces[0].curve->anyVocal();
        check (wideNoVocal && spansEqual (w0, w1, 0, notes[1].s0 - m) && spansEqual (w0, w1, notes[2].s1 + m, src.getNumSamples()) && ! spansEqual (w0, w1, notes[1].s0 + m, notes[2].s1 - m)
               && maxStep (w1) <= 1.5f * stepSrc, "WIDE: the formant correction changes only the edited notes (the rest is the stretcher's stream sample for sample), no clicks");
        // mixed: note 2 VOCAL, note 3 WIDE in one piece (the 10 ms join)
        Project qm = *ops::shiftNotes (base, notes[1].regionId, { notes[1], notes[2] }, 200.0, true);
        for (auto& e : qm.regions[0].pitch) e.engine = e.srcStart == notes[1].s0 ? (int) PitchEngine::Vocal : (int) PitchEngine::Wide;
        rig->apply (qm, "mixed");
        const auto mx = rig.render();
        const auto* C = rig->currentPlan().pieces[0].curve.get();
        const double mid1 = 0.5 * (double) (notes[1].s0 + notes[1].s1), mid2 = 0.5 * (double) (notes[2].s0 + notes[2].s1);
        const auto tm = trackOf (mx);
        const double e1 = std::abs (pitchedit::centreOf (tm, notes[1].s0, notes[1].s1) - 6400), e2 = std::abs (pitchedit::centreOf (tm, notes[2].s0, notes[2].s1) - 6700);
        check (C != nullptr && C->vocalAt (mid1) == 1.0f && C->vocalAt (mid2) == 0.0f && e1 <= 5 && e2 <= 10 && maxStep (mx) <= 1.5f * stepSrc && bitEqual (mx, rig.render()),
               "mixed engines in one piece (note 2 VOCAL, note 3 WIDE): " + juce::String (e1, 1) + " / " + juce::String (e2, 1) + " ct off, no click at the join, deterministic");
    }
    {   // unvoiced passthrough: noise around a sung vowel stays the source bit for bit when the vowel is shifted
        juce::AudioBuffer<float> b (1, 48000 * 2); b.clear();
        std::mt19937 rng (7); std::uniform_real_distribution<float> U (-1.0f, 1.0f);
        const auto vow = melody (48000, { { 62, 0.8 } }, true);
        for (int i = 0; i < 14400; ++i) b.setSample (0, 2400 + i, 0.12f * U (rng) * std::min (1.0f, std::min ((float) i, (float) (14400 - i)) / 480.0f));   // 0.05 .. 0.35 s
        for (int i = 0; i < vow.getNumSamples() && 19200 + i < b.getNumSamples(); ++i) b.setSample (0, 19200 + i, vow.getSample (0, i));                    // vowel 0.5 .. 1.3 s (incl. its 0.1 s lead)
        for (int i = 0; i < 14400; ++i) b.setSample (0, 74400 + i, 0.12f * U (rng) * std::min (1.0f, std::min ((float) i, (float) (14400 - i)) / 480.0f)); // 1.55 .. 1.85 s
        Rig rig (stereoOf (b));
        { Project q = rig->project(); q.material = "VOCAL"; rig->apply (q, "vocal"); }
        rig->waitAnalysis (30000);
        auto notes = notesNow (*rig.p);
        bool ok = notes.size() == 1;
        if (ok)
        {
            rig->apply (*ops::shiftNotes (rig->project(), notes[0].regionId, { notes[0] }, 300.0, false), "+3");
            const auto out = rig.render(); const auto& src = rig->getSource()->audio;
            const bool vocal = rig->currentPlan().pieces[0].curve != nullptr && rig->currentPlan().pieces[0].curve->anyVocal();
            auto eq = [&] (int a, int z) { for (int c = 0; c < 2; ++c) for (int i = a; i < z; ++i) if (out.getSample (c, i) != src.getSample (0, i)) return false; return true; };
            const int64 m = 2400;
            ok = vocal && eq (2400, (int) (notes[0].s0 - m)) && eq ((int) (notes[0].s1 + m), 88800);
            check (ok, "unvoiced passthrough: the noise before and after a vowel shifted +3 st (VOCAL) is the source bit for bit (vowel " + juce::String ((double) notes[0].s0 / 48.0, 0) + " - "
                   + juce::String ((double) notes[0].s1 / 48.0, 0) + " ms)");
        }
        else check (false, "unvoiced passthrough: one note found (" + juce::String ((int) notes.size()) + ")");
    }
    {   // ENGINE resolution (AUTO / VOCAL / WIDE, per note and per project) + the JSON fields
        OneNote o (60, 1.0);
        if (! o.ok) { check (false, "ENGINE: the test note was found"); return; }
        auto a = (*o.rig)->analysis();
        auto share = [&] (double cents, int noteEngine, int projEngine, double global = 0)
        {
            Region r = (*o.rig)->project().regions[0]; r.pitch.clear();
            PitchEdit e; e.id = 99; e.srcStart = o.note.s0; e.srcEnd = o.note.s1; e.centre = o.note.centre * 100; e.cents = cents; e.engine = noteEngine; r.pitch.push_back (e);
            auto c = pitchedit::build (r, &a->pitch, 1.0, 48000.0, (PitchEngine) projEngine, global);
            return c == nullptr ? -1.0f : c->vocalAt (0.5 * (double) (o.note.s0 + o.note.s1));
        };
        const float autoIn = share (300, -1, 0), autoOut = share (600, -1, 0), autoGlobal = share (300, -1, 0, 200), vocal6 = share (600, 1, 0), vocal13 = share (1300, 1, 0), wide2 = share (200, 2, 0),
                    projWide = share (200, -1, 2), projVocal = share (900, -1, 1);
        check (autoIn == 1 && autoOut == 0 && autoGlobal == 0 && vocal6 == 1 && vocal13 == 0 && wide2 == 0 && projWide == 0 && projVocal == 1,
               "ENGINE: AUTO = VOCAL at +3 st, WIDE at +6 (and at +3 with a global +2); VOCAL forced up to 12 st (+6 yes, +13 no); WIDE forced; the project's ENGINE when the note has none");
        Project p = (*o.rig)->project(); p.pitchEngine = 1;
        PitchEdit e; e.id = p.nextId++; e.srcStart = 100; e.srcEnd = 9000; e.cents = 50; e.engine = 2; p.regions[0].pitch.push_back (e);
        Project back; juce::String err;
        const bool rt = json::fromString (json::toString (p), back, err) && back == p && back.pitchEngine == 1 && back.regions[0].pitch.back().engine == 2;
        auto v = json::toVar (p); v.getDynamicObject()->removeProperty ("pitchEngine");
        if (auto* arr = v["regions"][0]["pitch"].getArray()) for (auto& pv : *arr) pv.getDynamicObject()->removeProperty ("engine");
        Project old; json::fromVar (v, old, err);
        check (rt && old.pitchEngine == 0 && old.regions[0].pitch.back().engine == -1, "ENGINE through JSON (project + note); older projects load as AUTO / the project's");
    }
    {   // cost: a 1.2 s note +3 st, STUDIO
        OneNote o (64, 1.2);
        if (o.ok)
        {
            auto t0 = juce::Time::getMillisecondCounterHiRes(); o.edit (300, (int) PitchEngine::Vocal); const double tv = juce::Time::getMillisecondCounterHiRes() - t0;
            t0 = juce::Time::getMillisecondCounterHiRes(); o.edit (300, (int) PitchEngine::Wide); const double tw = juce::Time::getMillisecondCounterHiRes() - t0;
            formantfix::enabled = false; t0 = juce::Time::getMillisecondCounterHiRes(); o.edit (300, (int) PitchEngine::Wide); const double tb = juce::Time::getMillisecondCounterHiRes() - t0; formantfix::enabled = true;
            check (tv < 1500 && tw < 1500, "STUDIO render cost of a 1.6 s line with a 1.2 s note +3 st: VOCAL " + juce::String (tv, 0) + " ms, WIDE + correction " + juce::String (tw, 0)
                   + " ms (the stretcher alone " + juce::String (tb, 0) + " ms)");
        }
    }
}

// the PITCH tab's ENGINE box: the project's ENGINE with nothing selected, the selected notes' otherwise
static void testFormantUi()
{
    section ("[21b] PITCH tab ENGINE (AUTO / VOCAL / WIDE)");
    Rig rig (stereoOf (voice (48000, fourNotes())));
    { Project q = rig->project(); q.material = "MELODIC"; rig->apply (q, "melodic"); }
    rig->waitAnalysis (30000);
    std::unique_ptr<SliceEditor> ed (static_cast<SliceEditor*> (rig->createEditor()));
    ed->setVisible (true); ed->setSize (1280, 780); ed->resized(); pump (10);
    ed->setWorkspace (gslice::ui::Workspace::Pitch); pump (10);
    auto& b = ed->engineBox;
    check (b.isVisible() && b.getTooltip().contains ("VOCAL") && b.getSelectedId() == 1, "the ENGINE box sits in the PITCH tab with a tooltip, AUTO by default");
    ed->view.selectNotes ({}); ed->refresh();
    b.setSelectedId (2, juce::sendNotificationSync);
    check (rig->project().pitchEngine == 1 && rig->getHistory().undoLabel() == "Pitch engine", "nothing selected: ENGINE VOCAL sets the project's (one undo step)");
    auto notes = ed->view.pitchNotes();
    ed->view.selectNotes ({ notes[2] }); ed->refresh();
    check (b.getSelectedId() == 2, "a note without its own ENGINE shows the project's");
    b.setSelectedId (3, juce::sendNotificationSync);
    int eng = -9; for (auto& e : rig->project().regions[0].pitch) if (e.srcStart == notes[2].s0) eng = e.engine;
    check (eng == 2 && rig->project().pitchEngine == 1 && rig->getHistory().undoLabel() == "Note engine", "a note selected: ENGINE WIDE goes to that note only");
    ed->view.selectNotes ({ ed->view.pitchNotes()[1] }); ed->refresh();
    const bool other = b.getSelectedId() == 2;
    ed->view.selectNotes ({ ed->view.pitchNotes()[2] }); ed->refresh();
    check (other && b.getSelectedId() == 3, "the box follows the selection (the other note shows the project's VOCAL, the edited one WIDE)");
    ed.reset(); pump (10);
}

// A/B renders for listening: before (phase 14: the stretcher, no correction) / after (WIDE + correction) / VOCAL
static void writeFormantRenders (const juce::File& dir)
{
    dir.createDirectory();
    juce::WavAudioFormat wav;
    int n = 0;
    auto out = [&] (const juce::String& name, const juce::AudioBuffer<float>& b) { if (writeAudio (b, 48000, dir.getChildFile (name + ".wav"), wav, 24)) { ++n; std::printf ("  %s.wav\n", name.toRawUTF8()); } };
    auto three = [&] (const juce::String& stem, const std::function<juce::AudioBuffer<float> (int)>& render)
    {
        formantfix::enabled = false; out (stem + "_A_before_WIDE-phase14", render ((int) PitchEngine::Wide)); formantfix::enabled = true;
        out (stem + "_B_after_WIDE+formantfix", render ((int) PitchEngine::Wide));
        out (stem + "_C_after_VOCAL-psola", render ((int) PitchEngine::Vocal));
    };
    {   // the narrow-formant vowel E4 at +-4 st
        OneNote o (64, 1.2);
        out ("01_narrow-vowel_E4_source", o.rig->render());
        for (double st : { 4.0, -4.0 }) three ("01_narrow-vowel_E4_" + juce::String (st > 0 ? "+" : "") + juce::String ((int) st) + "st", [&] (int e) { return o.edit (st * 100, e); });
    }
    {   // a 5 Hz +-40 ct vibrato: x0 / x2
        OneNote o (64, 1.8, 5.0, 40);
        out ("02_vibrato_E4_source", o.rig->render());
        three ("02_vibrato_x0", [&] (int e) { return o.edit (0, e, 0, 0.0); });
        three ("02_vibrato_x2", [&] (int e) { return o.edit (0, e, 0, 2.0); });
    }
    {   // the sung line C4 D4 F4 E4 -> C4 E4 G4 E4
        Rig rig (stereoOf (voice (48000, fourNotes())));
        { Project q = rig->project(); q.material = "MELODIC"; rig->apply (q, "melodic"); }
        rig->waitAnalysis (30000);
        auto notes = notesNow (*rig.p);
        if (notes.size() == 4)
        {
            out ("03_line_C4-D4-F4-E4_source", rig.render());
            const Project base = rig->project();
            three ("03_line_to_C4-E4-G4-E4", [&] (int e)
            {
                Project q = *ops::shiftNotes (base, notes[1].regionId, { notes[1], notes[2] }, 200.0, true);
                for (auto& x : q.regions[0].pitch) x.engine = e;
                rig->apply (q, "e"); return rig.render();
            });
        }
    }
    {   // the glottal pulse train +3 st
        std::vector<double> gci;
        Rig rig (stereoOf (glottalTrain (48000, 1.6, gci)));
        { Project q = rig->project(); q.material = "VOCAL"; rig->apply (q, "vocal"); }
        rig->waitAnalysis (30000);
        auto notes = notesNow (*rig.p);
        out ("04_glottal-train_source", rig.render());
        if (! notes.empty())
        {
            const Project base = rig->project();
            three ("04_glottal-train_+3st", [&] (int e)
            {
                Project q = *ops::shiftNotes (base, notes[0].regionId, notes, 300.0, false);
                for (auto& x : q.regions[0].pitch) x.engine = e;
                rig->apply (q, "e"); return rig.render();
            });
        }
    }
    std::printf ("  %d renders in %s\n", n, dir.getFullPathName().toRawUTF8());
}
// a real vocal (when the file is there): every note +2 st, VOCAL vs WIDE - finite, no clicks, on pitch
static void realVocalCheck (bool asTest)
{
    const juce::File mp3 ("/root/work/04_vocal_ecto_echo.mp3");
    if (! mp3.existsAsFile()) return;
    juce::String err; auto s = source::load (mp3, err); if (s == nullptr) return;
    juce::AudioBuffer<float> a (2, (int) std::min<int64> (s->frames, (int64) (s->sampleRate * 12)));
    for (int c = 0; c < 2; ++c) a.copyFrom (c, 0, s->audio, juce::jmin (c, s->audio.getNumChannels() - 1), 0, a.getNumSamples());
    Rig rig (std::move (a), s->sampleRate);
    { Project q = rig->project(); q.material = "VOCAL"; rig->apply (q, "vocal"); }
    rig->waitAnalysis (60000);
    auto notes = notesNow (*rig.p);
    std::vector<pitchedit::NoteRef> sel; for (auto& n : notes) if (n.confidence >= 0.5f) sel.push_back (n);
    if (sel.empty()) return;
    const auto src = rig.render();
    const Project base = rig->project();
    double worstV = 0, worstW = 0; float stepV = 0, stepW = 0; bool finite = true; int counted = 0; double tv = 0, tw = 0;
    for (int e : { (int) PitchEngine::Vocal, (int) PitchEngine::Wide })
    {
        Project q = *ops::shiftNotes (base, sel[0].regionId, sel, 200.0, false);
        for (auto& x : q.regions[0].pitch) x.engine = e;
        rig->apply (q, "e");
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        const auto out = rig.render();
        (e == 1 ? tv : tw) = juce::Time::getMillisecondCounterHiRes() - t0;
        for (int c = 0; c < 2; ++c) for (int i = 0; i < out.getNumSamples(); ++i) finite &= std::isfinite (out.getSample (c, i));
        const auto t = trackOf (out), ts = trackOf (src);
        double worst = 0; int cnt = 0;
        for (auto& n : sel)
        {
            const double c0 = pitchedit::centreOf (ts, n.s0, n.s1), c1 = pitchedit::centreOf (t, n.s0, n.s1);
            if (c0 <= 0 || c1 <= 0 || n.s1 - n.s0 < 9600) continue;
            worst = std::max (worst, std::abs (c1 - c0 - 200.0)); ++cnt;
        }
        (e == 1 ? worstV : worstW) = worst; counted = cnt;
        (e == 1 ? stepV : stepW) = maxStep (out);
    }
    const float stepS = maxStep (src);
    juce::String msg = "a real vocal (04_vocal_ecto_echo, 12 s, " + juce::String ((int) sel.size()) + " notes +2 st): VOCAL worst " + juce::String (worstV, 1) + " ct off over "
                       + juce::String (counted) + " notes >= 200 ms (WIDE " + juce::String (worstW, 1) + "), max step " + juce::String (stepV, 3) + " / " + juce::String (stepW, 3) + " (source "
                       + juce::String (stepS, 3) + "), renders " + juce::String (tv, 0) + " / " + juce::String (tw, 0) + " ms, all finite";
    if (asTest) check (finite && stepV <= 1.6f * stepS && worstV <= 25.0, msg);
    else std::printf ("  %s\n", msg.toRawUTF8());
}
