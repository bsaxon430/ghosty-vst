// GhostSliceTests: GHOST PITCH note editing (phase 14). Included by SliceTests.cpp after SliceAnalysisTests.inc (it shares
// check / section / Rig / melody / centroid there). Group: notes
#include "../PitchEdit.h"

static juce::AudioBuffer<float> stereoOf (const juce::AudioBuffer<float>& m)
{
    juce::AudioBuffer<float> st (2, m.getNumSamples()); st.copyFrom (0, 0, m, 0, 0, m.getNumSamples()); st.copyFrom (1, 0, m, 0, 0, m.getNumSamples()); return st;
}
static an::PitchTrack trackOf (const juce::AudioBuffer<float>& b)
{
    const auto x = an::mono (b); return an::trackPitch (x.data(), (int64) x.size(), 48000.0);
}
// the cents (MIDI x 100) of a track frame range, voiced frames only
static std::vector<double> centsIn (const an::PitchTrack& t, int64 s0, int64 s1)
{
    std::vector<double> v;
    for (int f = (int) (s0 / t.hop); f <= (int) (s1 / t.hop) && f < t.frames(); ++f) if (f >= 0 && t.f0[(size_t) f] > 0) v.push_back (pitchedit::centsOfHz (t.f0[(size_t) f]));
    return v;
}
static double stddev (const std::vector<double>& v)
{
    if (v.size() < 2) return 1e9;
    double m = 0; for (double x : v) m += x; m /= (double) v.size();
    double s = 0; for (double x : v) s += (x - m) * (x - m); return std::sqrt (s / (double) (v.size() - 1));
}
// the lag (frames of 5 ms) that best lines up two RMS envelopes
static int envelopeLag (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    auto env = [] (const juce::AudioBuffer<float>& x) { std::vector<double> e; for (int s = 0; s + 240 <= x.getNumSamples(); s += 240) { double v = 0; for (int i = 0; i < 240; ++i) v += (double) x.getSample (0, s + i) * x.getSample (0, s + i); e.push_back (std::sqrt (v / 240)); } return e; };
    const auto ea = env (a), eb = env (b);
    int best = 0; double bv = -1;
    for (int lag = -8; lag <= 8; ++lag)
    {
        double v = 0; for (int i = 0; i < (int) ea.size(); ++i) { const int j = i + lag; if (j >= 0 && j < (int) eb.size()) v += ea[(size_t) i] * eb[(size_t) j]; }
        if (v > bv) { bv = v; best = lag; }
    }
    return best;
}

static std::vector<pitchedit::NoteRef> notesNow (SliceProcessor& p) { auto a = p.analysis(); return pitchedit::notesOf (p.project(), a.get()); }

// a sung-like voice for formant checks: harmonics shaped by three WIDE formant resonances (300 - 500 Hz bandwidth, wider than
// the harmonic spacing, as a measured vocal envelope is) over a -6 dB / octave source tilt in frequency; legato notes glide in
// 25 ms. A formant-keeping shift of +2 st moves this voice's spectral centroid by ~1.5 % (computed from the envelope).
static juce::AudioBuffer<float> voice (double sr, const std::vector<MelNote>& notes)
{
    double total = 0.1; for (auto& n : notes) total += n.secs + (n.gapAfter ? 0.08 : 0.0);
    juce::AudioBuffer<float> b (1, (int) ((total + 0.15) * sr)); b.clear();
    static const double F[3] { 700, 1200, 2600 }, BW[3] { 300, 400, 500 }, G[3] { 1.0, 0.7, 0.4 };
    auto env = [] (double f) { double e = 0; for (int k = 0; k < 3; ++k) { const double d = (f - F[k]) / BW[k]; e += G[k] / (1.0 + d * d); } return e * 200.0 / f; };
    double t = 0.1, phase = 0, prevMidi = notes.empty() ? 60 : notes[0].midi;
    for (size_t k = 0; k < notes.size(); ++k)
    {
        const auto& n = notes[k];
        const int64 s0 = (int64) (t * sr), len = (int64) (n.secs * sr);
        const bool legatoIn = k > 0 && ! notes[k - 1].gapAfter, legatoOut = k + 1 < notes.size() && ! n.gapAfter;
        for (int64 i = 0; i < len; ++i)
        {
            const double u = (double) i / sr;
            const double glide = legatoIn ? juce::jlimit (0.0, 1.0, u / 0.025) : 1.0;
            const double midi = prevMidi + (n.midi - prevMidi) * glide + n.vibCents / 100.0 * std::sin (2 * juce::MathConstants<double>::pi * n.vibHz * u);
            const double f0 = 440.0 * std::pow (2.0, (midi - 69.0) / 12.0);
            phase += f0 / sr; phase -= std::floor (phase);
            const double e = (legatoIn ? 1.0 : std::min (1.0, u / 0.015)) * (legatoOut ? 1.0 : std::min (1.0, (n.secs - u) / 0.02));
            double v = 0; for (int h = 1; h * f0 < std::min (6000.0, sr * 0.45); ++h) v += env (h * f0) * std::sin (2 * juce::MathConstants<double>::pi * h * phase);
            b.setSample (0, (int) (s0 + i), (float) (0.22 * e * v));
        }
        prevMidi = n.midi;
        t += n.secs + (n.gapAfter ? 0.08 : 0.0);
    }
    return b;
}

static const std::vector<MelNote>& fourNotes()
{   // C4 D4 F4 E4, a sung-like line (formants, legato pairs, a short breath in the middle)
    static const std::vector<MelNote> v { { 60, 0.55 }, { 62, 0.55, 0, 0, true }, { 65, 0.55 }, { 64, 0.6 } };
    return v;
}

static void testNotes()
{
    section ("[20] GHOST PITCH note editing: PITCH / FINE / FORMANT / DRIFT / VIBRATO / TRANSITION / TIMING / split / merge / DRAW (phase 14)");
    Rig rig (stereoOf (voice (48000, fourNotes())));
    { Project q = rig->project(); q.material = "MELODIC"; rig->apply (q, "melodic"); }
    rig->waitAnalysis (30000);
    auto a = rig->analysis();
    auto notes = notesNow (*rig.p);
    juce::String found; for (auto& n : notes) found << juce::String (n.centre, 2) << " ";
    check (notes.size() == 4, "the line C4 D4 F4 E4: " + juce::String ((int) notes.size()) + " notes found (" + found.trim() + ")");
    if (notes.size() != 4 || a == nullptr) return;
    const auto before = rig.render();
    const auto planBefore = rig->currentPlan().fingerprint;
    // C4 D4 F4 E4 -> C4 E4 G4 E4: drag notes 2 and 3 up a whole tone (semitone snap)
    const auto rid = notes[1].regionId;
    auto q = ops::shiftNotes (rig->project(), rid, { notes[1], notes[2] }, 200.0, true);
    check (q.has_value(), "SHIFT: two notes up 2 semitones (snapped)");
    rig->apply (*q, "Note pitch");
    const auto& pl = rig->currentPlan();
    bool curved = false; for (auto& pc : pl.pieces) curved |= pc.curve != nullptr && pc.stretch;
    check (curved && pl.fingerprint != planBefore && pl.length == rig->currentPlan().length, "the edits compile into a time-varying shift on the render pieces (" + juce::String ((int) pl.pieces.size()) + " pieces)");
    const auto after = rig.render();
    const auto ta = trackOf (after);
    const double want[4] { 6000, 6400, 6700, 6400 };
    double worst = 0; juce::String got;
    for (int k = 0; k < 4; ++k)
    {
        const double c = pitchedit::centreOf (ta, notes[(size_t) k].s0, notes[(size_t) k].s1);
        worst = std::max (worst, std::abs (c - want[k])); got << juce::String (c / 100.0, 2) << " ";
    }
    check (worst <= 10.0, "re-detected C4 E4 G4 E4 within 10 cents of the targets (worst " + juce::String (worst, 1) + " ct: " + got.trim() + ")");
    double worstF = 0;
    for (int k : { 1, 2 })
    {
        const int64 L = notes[(size_t) k].s1 - notes[(size_t) k].s0, s0 = notes[(size_t) k].s0 + L / 5, s1 = notes[(size_t) k].s1 - L / 5;
        const double r = centroid (after, (int) s0, (int) s1) / centroid (before, (int) s0, (int) s1);
        worstF = std::max (worstF, std::abs (r - 1.0));
    }
    check (worstF <= 0.05, "the edited notes keep their formants (spectral centroid within " + juce::String (worstF * 100.0, 1) + " %, limit 5 %)");
    {   // timing: same length, the notes start where they did
        const auto xo = an::mono (after);
        const auto on = an::detectOnsets (xo.data(), (int64) xo.size(), 48000.0, an::Material::Melodic);
        const auto outNotes = an::segmentNotes (ta, on.events);
        double worstT = 0;
        const auto xb = an::mono (before);
        const auto onb = an::detectOnsets (xb.data(), (int64) xb.size(), 48000.0, an::Material::Melodic);
        const auto refNotes = an::segmentNotes (trackOf (before), onb.events);
        for (auto& n : refNotes)
        {
            double best = 1e9; for (auto& m : outNotes) best = std::min (best, std::abs ((double) (m.s0 - n.s0)));
            worstT = std::max (worstT, best);
        }
        const int lag = envelopeLag (before, after);
        check (after.getNumSamples() == before.getNumSamples() && lag == 0 && outNotes.size() == notes.size() && worstT <= 0.020 * 48000,
               "timing unchanged: same length, the level envelope lines up at lag " + juce::String (lag) + ", " + juce::String ((int) outNotes.size())
               + " notes, their pitch-change starts within " + juce::String (worstT / 48.0, 1) + " ms (the 40 ms transition is centred on the boundary)");
    }
    {   // unedited parts of the pitch curve are zero; the curve is deterministic
        auto c1 = pitchedit::build (rig->project().regions[0], &a->pitch, 1.0, 48000.0), c2 = pitchedit::build (rig->project().regions[0], &a->pitch, 1.0, 48000.0);
        float c = 1, f = 1, b = 0; c1->at ((double) (notes[0].s0 + notes[0].s1) / 2, c, f, b);
        float c4 = 1; c1->at ((double) (notes[3].s0 + notes[3].s1) / 2, c4, f, b);
        float c2v = 0; c1->at ((double) (notes[1].s0 + notes[1].s1) / 2, c2v, f, b);
        check (c1->hash == c2->hash && c == 0.0f && c4 == 0.0f && std::abs (c2v - 200.0f) < 1.0f, "the curve: 0 on unedited notes, +200 ct inside the edited one, same hash on a rebuild");
    }
    check (bitEqual (after, rig.render()), "deterministic: the edited line renders the same samples twice");
    {   // LIVE previews the same ratio path (the player with the curve), no allocation on the audio thread
        Rig live (stereoOf (voice (48000, fourNotes())));
        { Project q2 = live->project(); q2.material = "MELODIC"; live->apply (q2, "melodic"); }
        live->waitAnalysis (30000);
        live->apply (*ops::shiftNotes (live->project(), live->project().regions[0].id, { notesNow (*live.p)[1], notesNow (*live.p)[2] }, 200.0, true), "pitch");
        { juce::AudioBuffer<float> w (2, 512); juce::MidiBuffer m; live->processBlock (w, m); }
        gAudioAllocs = 0;
        const auto out = playFor (*live.p, 0, before.getNumSamples() - 2048, 256);
        const long allocs = gAudioAllocs.load();
        const auto tl = trackOf (out);
        double w2 = 0; for (int k = 0; k < 4; ++k) w2 = std::max (w2, std::abs (pitchedit::centreOf (tl, notes[(size_t) k].s0, notes[(size_t) k].s1) - want[k]));
        check (w2 <= 15.0 && allocs == 0, "LIVE plays the edit too (worst " + juce::String (w2, 1) + " ct off the targets), " + juce::String (allocs) + " allocations on the audio thread");
    }
    {   // FINE (cents), FORMANT per note
        notes = notesNow (*rig.p);
        auto q2 = ops::editNotes (rig->project(), rid, { notes[3] }, [] (PitchEdit& e) { e.cents += 30; e.formantCents = 300; });
        rig->apply (*q2, "fine + formant");
        const auto r2 = rig.render(); const auto t2 = trackOf (r2);
        const double c = pitchedit::centreOf (t2, notes[3].s0, notes[3].s1);
        const int64 L = notes[3].s1 - notes[3].s0;
        const double cr = centroid (r2, (int) (notes[3].s0 + L / 5), (int) (notes[3].s1 - L / 5)) / centroid (before, (int) (notes[3].s0 + L / 5), (int) (notes[3].s1 - L / 5));
        check (std::abs (c - 6430) <= 10.0 && cr > 1.08, "FINE +30 ct lands at " + juce::String (c / 100.0, 2) + "; the note's FORMANT +3 st moves its centroid x" + juce::String (cr, 2));
        rig->undo();
    }
    {   // split / merge
        notes = notesNow (*rig.p);
        const auto n2 = notes[2];
        juce::String why;
        auto s = ops::splitNote (rig->project(), n2, (n2.s0 + n2.s1) / 2, &a->pitch, &why);
        const auto ns = s ? pitchedit::notesOf (*s, a.get()) : std::vector<pitchedit::NoteRef>();
        bool splitOk = s && ns.size() == 5 && std::abs (ns[2].target - 67.0) < 0.05 && std::abs (ns[3].target - 67.0) < 0.05 && ns[2].s1 == ns[3].s0;
        check (splitOk, "SPLIT a note at its middle: 5 notes, both halves keep the target G4");
        auto m = s ? ops::mergeNotes (*s, { ns[2], ns[3] }, &a->pitch, &why) : std::nullopt;
        const auto nm = m ? pitchedit::notesOf (*m, a.get()) : std::vector<pitchedit::NoteRef>();
        check (m && nm.size() == 4 && nm[2].s0 == n2.s0 && nm[2].s1 == n2.s1 && std::abs (nm[2].target - 67.0) < 0.05, "MERGE the halves: one note over the same span at G4 again");
        auto rs = ops::resetNotes (rig->project(), { notes[1], notes[2] });
        check (rs && rs->regions[0].pitch.empty(), "RESET: the notes' edits go, the region plays the original pitch");
    }
    {   // TIMING: move a note later by 40 ms (warp markers at its bounds, FLEX MOVE)
        notes = notesNow (*rig.p);
        juce::String why;
        const double d = 0.040 * rig->project().sampleRate;
        auto t = ops::noteTiming (rig->project(), notes[2], d, d, &why);
        bool ok = t.has_value();
        if (ok)
        {
            const auto& r = t->regions[0]; const auto m = r.map();
            const double o0 = m.outAt ((double) notes[2].s0), o1 = m.outAt ((double) notes[2].s1);
            ok = std::abs (o0 - ((double) notes[2].s0 + d)) < 2 && std::abs (o1 - ((double) notes[2].s1 + d)) < 2 && r.length() == rig->project().regions[0].length() && r.warp.size() == rig->project().regions[0].warp.size() + 2;
        }
        check (ok, "TIMING: the note moves 40 ms later (markers at its bounds, its neighbours stretch, the region keeps its length) " + why);
        auto st = ops::noteTiming (rig->project(), notes[2], 0, d, &why);
        check (st && std::abs (st->regions[0].map().outAt ((double) notes[2].s1) - ((double) notes[2].s1 + d)) < 2, "TIMING: stretch the note's end by 40 ms");
    }
    {   // DRIFT 0 flattens the slow wander
        Rig dr (stereoOf (melody (48000, { { 57, 2.4, 0.5, 30 } }, true)));
        { Project q2 = dr->project(); q2.material = "MELODIC"; dr->apply (q2, "melodic"); }
        dr->waitAnalysis (30000);
        auto dn = notesNow (*dr.p);
        bool ok = dn.size() == 1;
        double s0 = 0, s1 = 0;
        if (ok)
        {
            const auto& n = dn[0]; const int64 L = n.s1 - n.s0;
            s0 = stddev (centsIn (trackOf (dr.render()), n.s0 + L / 5, n.s1 - L / 5));
            dr->apply (*ops::editNotes (dr->project(), n.regionId, { n }, [] (PitchEdit& e) { e.drift = 0; }), "drift 0");
            s1 = stddev (centsIn (trackOf (dr.render()), n.s0 + L / 5, n.s1 - L / 5));
        }
        check (ok && s0 > 12 && s1 <= 3.0, "DRIFT 0 flattens a 0.5 Hz +-30 ct wander: f0 std " + juce::String (s0, 1) + " -> " + juce::String (s1, 2) + " ct (limit 3)");
    }
    {   // VIBRATO 0 / 2 scale the detected vibrato (the stretcher's block length limits how much of a fast wobble it follows)
        Rig vr (stereoOf (melody (48000, { { 64, 1.8, 5.0, 40 } }, true)));
        { Project q2 = vr->project(); q2.material = "MELODIC"; vr->apply (q2, "melodic"); }
        vr->waitAnalysis (30000);
        auto vn = notesNow (*vr.p);
        if (vn.size() == 1)
        {
            const auto& n = vn[0]; const int64 L = n.s1 - n.s0;
            const double s1 = stddev (centsIn (trackOf (vr.render()), n.s0 + L / 5, n.s1 - L / 5));
            vr->apply (*ops::editNotes (vr->project(), n.regionId, { n }, [] (PitchEdit& e) { e.vibrato = 0; }), "vib 0");
            const double s0 = stddev (centsIn (trackOf (vr.render()), n.s0 + L / 5, n.s1 - L / 5));
            vr->apply (*ops::editNotes (vr->project(), n.regionId, notesNow (*vr.p), [] (PitchEdit& e) { e.vibrato = 2; }), "vib 2");
            const double s2 = stddev (centsIn (trackOf (vr.render()), n.s0 + L / 5, n.s1 - L / 5));
            std::printf ("    vibrato f0 std: x1 %.1f ct, x0 %.1f ct, x2 %.1f ct\n", s1, s0, s2);
            check (s0 < s1 * 0.75 && s2 > s1 * 1.2, "VIBRATO scales a 5 Hz +-40 ct vibrato: f0 std " + juce::String (s1, 1) + " ct -> x0 " + juce::String (s0, 1) + " / x2 " + juce::String (s2, 1));
        }
        else check (false, "VIBRATO: the test note was found");
    }
    {   // PITCH DRAW: an offset ramp 0 -> +12 st over a steady note renders a rising f0
        Rig dw (stereoOf (melody (48000, { { 60, 1.6 } }, true)));
        { Project q2 = dw->project(); q2.material = "MELODIC"; dw->apply (q2, "melodic"); }
        dw->waitAnalysis (30000);
        auto wn = notesNow (*dw.p);
        bool ok = wn.size() == 1; juce::String detail;
        if (ok)
        {
            const auto& n = wn[0]; const int64 a0 = n.s0 + 9600, a1 = n.s1 - 9600;
            int64 id = 0;
            auto q2 = ops::addDraw (dw->project(), n.regionId, { { a0, 0.0 }, { (a0 + a1) / 2, 600.0 }, { a1, 1200.0 } }, false, &id);
            ok = q2.has_value() && id != 0;
            if (ok)
            {
                dw->apply (*q2, "Pitch draw");
                const auto out = dw.render(); const auto t = trackOf (out);
                // f0 smoothed over ~50 ms, sampled every ~100 ms over the drawn span: strictly rising, about an octave in total
                std::vector<double> pts;
                for (int64 s = a0 + 4800; s <= a1 - 4800; s += 4800) { auto v = centsIn (t, s - 1200, s + 1200); if (! v.empty()) { std::sort (v.begin(), v.end()); pts.push_back (v[v.size() / 2]); } }
                int falls = 0; for (size_t i = 1; i < pts.size(); ++i) if (pts[i] <= pts[i - 1]) ++falls;
                const double rise = pts.size() > 1 ? pts.back() - pts.front() : 0;
                ok = pts.size() >= 8 && falls == 0 && rise > 900;
                detail = juce::String ((int) pts.size()) + " points, " + juce::String (falls) + " falls, rise " + juce::String (rise, 0) + " ct";
                dw->undo();
                ok = ok && dw->project().regions[0].pitch.empty();
            }
        }
        check (ok, "PITCH DRAW: a 0 -> +12 st ramp renders a monotonic f0 (" + detail + "), and UNDO removes it");
    }
    {   // state round trip: every new field through JSON and the processor's state; older projects load with the defaults
        Project p = rig->project();
        auto& r = p.regions[0];
        PitchEdit e; e.id = p.nextId++; e.srcStart = 1000; e.srcEnd = 9000; e.cents = -150; e.formantCents = 120; e.centre = 6234.5; e.drift = 0.25; e.vibrato = 1.75; e.transitionMs = 120; e.glide = true;
        PitchEdit d; d.id = p.nextId++; d.kind = PitchEditKind::Draw; d.absolute = true; d.curve = { { 2000, 6100.5 }, { 4000, 6200.25 } }; d.srcStart = 2000; d.srcEnd = 4000;
        r.pitch.push_back (e); r.pitch.push_back (d); p.keyMode = 2;
        Project back; juce::String err;
        const bool ok = json::fromString (json::toString (p), back, err) && back == p;
        auto v = json::toVar (p);
        if (auto* arr = v["regions"][0]["pitch"].getArray()) for (auto& pv : *arr) for (auto* k : { "kind", "centre", "drift", "vibrato", "transitionMs", "glide", "absolute", "curve" }) pv.getDynamicObject()->removeProperty (k);
        v.getDynamicObject()->removeProperty ("keyMode");
        Project old; json::fromVar (v, old, err);
        const auto& oe = old.regions[0].pitch;
        bool defaults = old.keyMode == 0 && ! oe.empty();
        for (auto& x : oe) defaults = defaults && x.isNote() && x.drift == 1.0 && x.vibrato == 1.0 && x.transitionMs == 40.0 && ! x.glide && ! x.absolute;
        juce::MemoryBlock mb; rig->apply (p, "state"); rig->getStateInformation (mb);
        const auto sv = juce::JSON::parse (mb.toString());
        Project fromState; json::fromVar (sv["project"], fromState, err);
        fromState.source.path = p.source.path;
        check (ok && defaults && fromState == rig->project(), "state round trip: the note / draw fields + KEY MODE through JSON and the plugin state; older projects get the defaults");
    }
}

// ---------------------------------------------------------------- the PITCH tab: gestures + screenshots
static void testNotesUi (const juce::File& shots)
{
    section ("[20b] PITCH tab note editing: select, drag pitch (snap / free), band, TIMING, split, DRAW, keys, screenshots");
    using namespace ghosty::themes;
    Rig rig (stereoOf (voice (48000, fourNotes())));
    { Project q = rig->project(); q.material = "MELODIC"; rig->apply (q, "melodic"); }
    rig->waitAnalysis (30000);
    std::unique_ptr<SliceEditor> ed (static_cast<SliceEditor*> (rig->createEditor()));
    ed->setVisible (true); ed->setSize (1280, 780); ed->resized(); pump (20);
    ed->setWorkspace (gslice::ui::Workspace::Pitch); auto& v = ed->view; v.zoomToFit(); pump (10);
    auto notes = v.pitchNotes();
    if (notes.size() != 4) { check (false, "PITCH tab: 4 notes shown (" + juce::String ((int) notes.size()) + ")"); return; }
    const auto none = juce::ModifierKeys::leftButtonModifier;
    auto dragOn = [&] (juce::Point<float> a, juce::Point<float> b, juce::ModifierKeys mods)
    {
        v.mouseDown (mouseAt (v, a, mods)); v.mouseDrag (mouseAt (v, a.translated ((b.x - a.x) * 0.5f, (b.y - a.y) * 0.5f), mods, 1, a));
        v.mouseDrag (mouseAt (v, b, mods, 1, a)); v.mouseUp (mouseAt (v, b, juce::ModifierKeys (mods.getRawFlags() & ~juce::ModifierKeys::allMouseButtonModifiers), 1, a));
    };
    // click = select
    const auto c1 = v.noteRect (notes[1]).getCentre();
    v.mouseDown (mouseAt (v, c1)); v.mouseUp (mouseAt (v, c1, {}));
    check (v.selectedNotes().size() == 1 && v.selectedNotes()[0].s0 == notes[1].s0 && ed->fineF.isEnabled() && ! ed->mergeBtn.isEnabled(), "click a blob: it is selected (the note controls light up)");
    // drag up 2 rows (semitone snap)
    const float row = v.yOfPitch (60) - v.yOfPitch (61);
    const auto undo0 = rig->getHistory().undoCount();
    dragOn (c1, c1.translated (3, -2.2f * row), none);
    notes = v.pitchNotes();
    check (std::abs (notes[1].target - 64.0) < 1e-6 && rig->getHistory().undoCount() == undo0 + 1 && rig->getHistory().undoLabel() == "Note pitch",
           "drag a blob up two rows: D4 -> E4 exactly (snapped), one undo step (" + juce::String (notes[1].target, 2) + ")");
    // Shift-drag = free cents
    const auto c2 = v.noteRect (notes[2]).getCentre();
    dragOn (c2, c2.translated (0, -0.37f * row), juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier));
    notes = v.pitchNotes();
    const double frac = (notes[2].target - 65.0) * 100.0;
    check (frac > 25 && frac < 50, "Shift-drag: free cents (F4 " + juce::String (frac, 1) + " ct)");
    rig->undo(); notes = v.pitchNotes();
    // band select
    const auto wa = v.waveArea().toFloat();
    const auto b0 = juce::Point<float> (v.noteRect (notes[0]).getX() - 6, wa.getY() + 6), b1 = juce::Point<float> (v.noteRect (notes[2]).getCentreX(), wa.getBottom() - 6);
    dragOn (b0, b1, none);
    check (v.selectedNotes().size() == 3 && ed->mergeBtn.isEnabled(), "drag a band over empty space: 3 notes selected (MERGE is on)");
    // keys: Up = +1 semitone for the selection
    const bool upOk = ed->keyPressed (juce::KeyPress (juce::KeyPress::upKey)); notes = v.pitchNotes();
    check (upOk && std::abs (notes[0].target - 61.0) < 1e-6 && std::abs (notes[1].target - 65.0) < 1e-6 && std::abs (notes[2].target - 66.0) < 1e-6 && std::abs (notes[3].target - 64.0) < 0.1, "Up: the selected notes +1 semitone, the others stay");
    rig->undo(); rig->undo();
    // FINE / DRIFT through the fields
    v.selectNotes ({ v.pitchNotes()[3] }); ed->refresh();
    ed->fineF.s.setValue (12.0, juce::sendNotificationSync);
    ed->driftF.s.setValue (50.0, juce::sendNotificationSync);
    notes = v.pitchNotes();
    const PitchEdit* e3 = nullptr; for (auto& e : rig->project().regions[0].pitch) if (e.id == notes[3].editId) e3 = &e;
    check (e3 != nullptr && std::abs (notes[3].target - 64.12) < 1e-6 && e3->drift == 0.5, "FINE +12 ct and DRIFT 50 % from the note fields");
    // TIMING: Cmd-drag a blob later
    const auto warpBefore = rig->project().regions[0].warp.size();
    const auto c3 = v.noteRect (notes[2]).getCentre();
    dragOn (c3, c3.translated (30, 0), juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::commandModifier));
    check (rig->project().regions[0].warp.size() == warpBefore + 2 && rig->getHistory().undoLabel() == "Note timing", "Cmd/Ctrl-drag a blob: TIMING (two warp markers at its bounds)");
    snap (*ed, shots.getChildFile ("slice-black-pitch-timing.png"));
    rig->undo();
    // double-click = split
    notes = v.pitchNotes();
    const auto c0 = v.noteRect (notes[0]).getCentre();
    v.mouseDoubleClick (mouseAt (v, c0, {}, 2));
    check (v.pitchNotes().size() == 5, "double-click a blob: SPLIT there (5 notes)");
    rig->undo();
    // DRAW
    ed->drawBtn.triggerClick(); pump (10);
    {
        notes = v.pitchNotes();
        const auto R = v.noteRect (notes[3]);
        const auto a = juce::Point<float> (R.getX() + 8, v.yOfPitch (64)), b = juce::Point<float> (R.getRight() - 8, v.yOfPitch (66));
        v.mouseDown (mouseAt (v, a)); for (int i = 1; i <= 12; ++i) v.mouseDrag (mouseAt (v, a + (b - a) * ((float) i / 12.0f), none, 1, a)); v.mouseUp (mouseAt (v, b, {}, 1, a));
    }
    int draws = 0; for (auto& e : rig->project().regions[0].pitch) draws += e.isNote() ? 0 : 1;
    check (draws == 1 && rig->getHistory().undoLabel() == "Pitch draw", "DRAW: a stroke over the last note = one drawn curve, one undo step");
    for (auto t : { Theme::Dark, Theme::Purple, Theme::Light })
    {
        ed->setTheme (t); v.selectNotes ({ v.pitchNotes()[1], v.pitchNotes()[2] }); ed->refresh(); pump (20);
        snap (*ed, shots.getChildFile ("slice-" + displayName (t).toLowerCase() + "-pitch-edit.png"));
    }
    ed->setTheme (Theme::Dark);
    ed->drawBtn.triggerClick(); pump (10);
    rig->undo();
    {   // the edited curve + a moved note with its ghost
        v.selectNotes ({ v.pitchNotes()[1] }); ed->keyPressed (juce::KeyPress (juce::KeyPress::upKey)); ed->keyPressed (juce::KeyPress (juce::KeyPress::upKey));
        ed->refresh(); pump (20);
        snap (*ed, shots.getChildFile ("slice-black-pitch-moved.png"));
    }
    check (shots.getChildFile ("slice-light-pitch-edit.png").existsAsFile(), "PITCH editing screenshots written");
    {   // the light theme: the edited-curve violet stays readable
        for (auto t : { Theme::Dark, Theme::Purple, Theme::Light })
        {
            const auto P = gslice::ui::palette (t);
            check (contrast (P.draw, P.waveBg) >= 3.0,
                   displayName (t) + ": the PITCH DRAW violet >= 3:1 on the waveform (" + juce::String (contrast (P.draw, P.waveBg), 2) + ")");
        }
    }
    ed.reset(); pump (20);
}
