// GHOST SLICE build 11 tests (included by SliceTests.cpp after SliceTests10.h): VOICE 0 beyond an octave (engine 9), DRAG MIDI / DRAG
// CHORDS as single clean tracks, and the audio-quality sweep over several keys x seeds x voices (clicks, clipping, key, level).
#pragma once

namespace eleven
{
using namespace six;
using namespace eight;
using namespace nine;
using namespace ten;

// a VOICE 0 MUSICAL take with every other note dragged far (+-13..19 semitones, in the key): the moves beyond an octave
static Recipe farEdit (const Prepared& P, Recipe r, int engine, bool twoOct = false)
{
    auto a = generate (P, r); r.engine = engine;
    if (a == nullptr) return r;
    const Key k { a->keyTonic, a->keyMinor };
    static const int offs1[] { 14, -14, 17, -17, 19, -13, 15, -16 }, offs2[] { 24, -24, 27, -26, 29, -25, 22, -28 };
    const int* offs = twoOct ? offs2 : offs1;
    int j = 0;
    for (size_t i = 0; i < a->notes.size(); i += 2)
    {
        auto n = a->notes[i]; r.edit.removed.push_back ({ n.beat, n.note });
        n.note = edit::snapPitch (juce::jlimit (30, 96, n.note + offs[j++ % 8]), k); n.mine = true; r.edit.locked.push_back (n);
    }
    return r;
}

static void farMovesEleven()
{
    section ("build 11: VOICE 0 beyond an octave (notes dragged far on the roll): the natural re-pitch, measured over keys x seeds");
    auto src = pdVoice() != nullptr ? pdVoice() : sung(); auto P = prepOf (src);
    const juce::String nm = pdVoice() != nullptr ? "public-domain voice" : "synthetic voice";
    const bool dbg = juce::SystemStats::getEnvironmentVariable ("GS_DEBUG", "").isNotEmpty();
    // two bands: notes dragged 13-19 semitones (past an octave) and 22-29 (about two octaves; the roll clamps at its ends)
    for (int band : { 1, 2 })
    {
        double w8 = 1, w9 = 1, m8 = 0, m9 = 0, h8 = 0, h9 = 0; int n = 0, k8 = 0, k9 = 0, takes = 0, low9 = 0; bool clean = true;
        const double lo = band == 1 ? 12.5 : 19.5;
        for (int key : { 0, 5, 9 })
            for (uint32_t seed : { 2024u, 77u, 905u })
            {
                Recipe r; r.type = Type::Instrument; r.seed = seed; r.detail = 5; r.musical = 1.0; r.clear = true; r.knob = 0.0; r.style = Style::Hook; r.bars = 4; r.keyTonic = key; r.keyMinor = key == 9; r.engine = 8;
                auto t8 = generate (*P, farEdit (*P, r, 8, band == 2)), t9 = generate (*P, farEdit (*P, r, 9, band == 2));
                if (t8 == nullptr || t9 == nullptr) { check (false, "rendered"); return; }
                ++takes;
                const auto s8 = scoresOf (*P, *t8), s9 = scoresOf (*P, *t9);
                for (size_t i = 0; i < s8.size() && i < s9.size(); ++i)
                {
                    if (std::abs (s8[i].semis) <= lo) continue;
                    ++n; w8 = std::min (w8, s8[i].corr); w9 = std::min (w9, s9[i].corr); m8 += s8[i].corr; m9 += s9[i].corr; h8 = std::max (h8, s8[i].hnrOff); h9 = std::max (h9, s9[i].hnrOff); if (s9[i].corr < 0.9) ++low9;
                    if (dbg) std::printf ("      key %d seed %u semis %+.1f corr %.3f -> %.3f hnr off %.1f -> %.1f\n", key, seed, s8[i].semis, s8[i].corr, s9[i].corr, s8[i].hnrOff, s9[i].hnrOff);
                }
                k8 += dsp::clicksIn (t8->preview); k9 += dsp::clicksIn (t9->preview);
                clean = clean && dsp::finiteAndClean (t9->preview) && dsp::peakOf (t9->preview) < 0.999f;
            }
        const juce::String bn = band == 1 ? "beyond an octave (13-19 semitones)" : "about two octaves (20-29 semitones)";
        check (n >= 8, nm + ", " + bn + ": " + juce::String (n) + " syllables measured, over " + juce::String (takes) + " takes (3 keys x 3 seeds)");
        // (band 1: every syllable at least 0.9. band 2: the mean better and at most one syllable in 30 under 0.9; measured: one, a note
        //  dragged to the top of the roll, three octaves above the spoken syllable)
        const bool good = band == 1 ? w9 >= w8 && w9 >= 0.9 : low9 * 30 <= n;
        check (good && m9 >= m8, nm + ", " + bn + ": " + juce::String (n - low9) + " / " + juce::String (n) + " syllables at 0.9 or better; worst syllable-envelope correlation " + f2 (w8, 3) + " (build 10) -> " + f2 (w9, 3) + ", mean " + f2 (m8 / juce::jmax (1, n), 3) + " -> " + f2 (m9 / juce::jmax (1, n), 3) + "; worst HNR off the recording " + f2 (h8, 1) + " -> " + f2 (h9, 1) + " dB");
        check (k9 == 0 && clean, nm + ", " + bn + ": the click detector over the " + juce::String (takes) + " takes: " + juce::String (k8) + " (build 10) -> " + juce::String (k9) + " clicks; no clipping");
    }
}

// ---------------------------------------------------------------- DRAG MIDI / DRAG CHORDS: each one clean track
static void midiEleven()
{
    section ("build 11: DRAG MIDI = the melody alone (one track, as build 8), DRAG CHORDS = the chords alone (one track)");
    ghosty::License::testForce().store (1);
    auto P = prepOf (sung());
    auto one = [] (const juce::MidiFile& f, int& notes) { notes = 0; if (f.getNumTracks() != 1) return false; for (auto* e : *f.getTrack (0)) if (e->message.isNoteOn()) ++notes; return true; };
    SliceProcessor p; p.prepareToPlay (48000, 512);
    for (Type ty : { Type::Instrument, Type::Chops, Type::Pad, Type::Texture })
    {
        Recipe r; r.type = ty; r.seed = 31; r.detail = 4; r.clear = true; r.knob = ty == Type::Instrument ? 0.6 : -1.0; r.style = ty == Type::Pad ? Style::Dreamy : Style::Hook; r.bars = 4; r.keyTonic = 9; r.keyMinor = true;
        auto t = generate (*P, r);
        int nm = 0, nc = 0; const bool m1 = t != nullptr && one (t->midi, nm);
        const auto cm = t != nullptr ? p.chordsMidiOf (*t) : juce::MidiFile();
        const bool c1 = cm.getNumTracks() == 0 ? ty == Type::Texture : one (cm, nc);
        int expect = 0; if (t != nullptr) { if (ty == Type::Pad) expect = (int) t->notes.size(); else if (ty != Type::Texture) expect = (int) chordNotes (t->chords, t->beatsPerChord, 16.0).size(); }
        check (m1 && nm == (int) notesOf (*t).size() && c1 && nc == expect, juce::String (typeName (ty)) + ": DRAG MIDI one track (" + juce::String (nm) + " notes), DRAG CHORDS " + (ty == Type::Texture ? juce::String ("none (a drone has no chords)") : "one track (" + juce::String (nc) + " notes)"));
    }
    {   // the CHORD BED's own notes go out through DRAG CHORDS
        Recipe r; r.type = Type::Instrument; r.seed = 2027; r.detail = 9; r.clear = true; r.knob = 0.6; r.style = Style::Hook; r.bars = 4; r.keyTonic = 9; r.keyMinor = true; r.edit.bed = 0.6; r.edit.chords = 1;
        auto t = generate (*P, r); int nm = 0, nc = 0;
        const bool ok = t != nullptr && one (t->midi, nm) && ! t->chordEvents.empty() && one (p.chordsMidiOf (*t), nc) && nc == (int) t->chordEvents.size();
        check (ok, "CHORD BED on: DRAG CHORDS is the bed's own " + juce::String (nc) + " notes, DRAG MIDI still the melody alone");
    }
    {   // a build 10 take (engine 8, which had the two-track MIDI) reopens with one clean track too; its sound is unchanged
        Recipe r; r.type = Type::Instrument; r.seed = 77; r.detail = 4; r.clear = true; r.style = Style::Hook; r.bars = 4; r.engine = 8;
        auto t = generate (*P, r); int nm = 0;
        check (t != nullptr && one (t->midi, nm), "a build 10 (engine 8) take: DRAG MIDI one track now");
    }
    {   // the files the drags write, through the plug-in
        p.loadSourceForTests (sung()); pump (20); p.waitIdle (60000); p.type = Type::Instrument; p.generate(); p.waitIdle (120000);
        juce::String e1, e2; const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("gs11-midi");
        const auto fm = p.writeTakeMidi (dir, e1), fc = p.writeTakeChordsMidi (dir, e2);
        juce::MidiFile a, b; bool ok = fm.existsAsFile() && fc.existsAsFile();
        if (ok) { juce::FileInputStream i1 (fm), i2 (fc); ok = a.readFrom (i1) && b.readFrom (i2); }
        int na = 0, nb = 0;
        ok = ok && one (a, na) && one (b, nb);
        check (ok && fc.getFileName().contains ("CHORDS") && na > 0 && nb > 0, "the two .mid files: \"" + fm.getFileName() + "\" (" + juce::String (na) + " notes) and \"" + fc.getFileName() + "\" (" + juce::String (nb) + " notes), one track each");
    }
}

// ---------------------------------------------------------------- the audio-quality sweep: every take type, several keys x seeds x voices
static void qualityEleven()
{
    section ("build 11: the audio-quality sweep (2 voices x 3 keys x 2 seeds x 7 settings, CLEAR off and on): no clicks, no clipping, clean, in key, no harsher top than the recording; exports -14 LUFS (CLEAR: never limited, at most -14) under -1 dBTP");
    int total = 0, bad = 0; double worstTp = -100; juce::StringArray badNames;
    struct Cfg { const char* name; Type ty; double knob; double bed; };
    const Cfg cfgs[] { { "INSTRUMENT VOICE 0", Type::Instrument, 0.0, 0.0 }, { "INSTRUMENT VOICE 50", Type::Instrument, 0.5, 0.0 }, { "INSTRUMENT VOICE 100", Type::Instrument, 1.0, 0.0 },
                       { "INSTRUMENT + CHORD BED", Type::Instrument, 0.6, 0.6 }, { "PAD", Type::Pad, -1, 0.0 }, { "CHOPS", Type::Chops, -1, 0.0 }, { "TEXTURE", Type::Texture, -1, 0.0 } };
    for (auto src : { sung(), pdVoice() })
    {
        if (src == nullptr) continue;
        auto P = prepOf (src); const juce::String vn = src == sung() ? "sung" : "public-domain voice";
        // (the real recording has its own air and s-sounds above 7 kHz: a take may not be harsher than the voice it came from)
        const double topMax = juce::jmax (0.03, 1.25 * highShare (P->work->buffer(), 7000));
        for (int key : { 0, 5, 9 })
            for (uint32_t seed : { 11u, 2026u })
                for (auto& cf : cfgs)
                {
                    Recipe r; r.type = cf.ty; r.seed = seed; r.detail = seed + 3; r.musical = 0.85; r.clear = seed != 11;   // (seed 11: CLEAR off, 2026: on) r.knob = cf.knob; r.style = cf.ty == Type::Pad ? Style::Dreamy : Style::Auto; r.bars = 4;
                    r.keyTonic = key; r.keyMinor = key != 0; r.edit.bed = cf.bed; if (cf.bed > 0) r.edit.chords = 1;
                    auto t = generate (*P, r); ++total;
                    const Key k { key, key != 0 };
                    juce::String why;
                    if (t == nullptr) why = "no take";
                    else
                    {
                        const int clicks = dsp::clicksIn (t->preview);
                        if (! dsp::finiteAndClean (t->preview)) why << "not finite ";
                        if (clicks > 0) why << clicks << " clicks ";
                        if (dsp::peakOf (t->preview) >= 0.999f) why << "clipping ";
                        if (highShare (t->preview, 7000) >= topMax) why << "harsh top (" << f2 (highShare (t->preview, 7000) * 100, 1) << " % > " << f2 (topMax * 100, 1) << " %) ";
                        // (in key: the pitch tracker follows one line, so it is read on the melody alone; under a CHORD BED the bed's notes are
                        //  checked instead: every one a note of the key)
                        if (cf.bed > 0) { int out = 0; for (auto& e : t->chordEvents) if (edit::snapPitch (e.note, k) != e.note) ++out; if (out > 0) why << out << " bed notes off key "; }
                        if (cf.ty == Type::Instrument && cf.knob >= 0.5 && cf.bed <= 0) { int fr = 0; const double pk = pitchInKey (t->preview, k, 35, &fr); if (fr > 50 && pk < 0.85) why << "off key (" << juce::roundToInt (pk * 100) << " %) "; }
                        // the export (DRAG WAV / SAVE TAKE) of the same take: -14 LUFS, true peak below -1 dBTP, no clicks
                        const auto w = renderForExport (*t, seed % 2 ? -0.3f : 0.4f, 0.3f, 0.55f, 0.35f);
                        const double L = dsp::lufs (w, 48000), tp = dsp::truePeakDb (w);
                        // (CLEAR off: the finish brings it to -14. CLEAR on: never limited, so a peaky take stops where its peaks
                        //  meet the -1.2 dB ceiling: at most -14, never louder)
                        if (r.clear ? ! (L < -13.4) : ! (L > -14.6 && L < -13.4)) why << "export " << f2 (L) << " LUFS ";
                        if (tp >= -1.0) why << "export true peak " << f2 (tp) << " dBTP ";
                        if (dsp::clicksIn (w) > 0 || ! dsp::finiteAndClean (w)) why << "export clicks ";
                        worstTp = juce::jmax (worstTp, tp);
                    }
                    if (why.isNotEmpty()) { ++bad; badNames.add (vn + " " + cf.name + " key " + juce::String (key) + " seed " + juce::String ((int) seed) + ": " + why.trim()); }
                }
    }
    for (auto& b : badNames) std::printf ("      %s\n", b.toRawUTF8());
    check (bad == 0, "every take clean, and its export at -14 LUFS (CLEAR: at most) under -1 dBTP: " + juce::String (total - bad) + " / " + juce::String (total) + " (worst export true peak " + f2 (worstTp) + " dBTP)");
}
// ---------------------------------------------------------------- demos: the same far-moved melody, build 10 vs build 11, and the two MIDI files
static void abEleven (const juce::File& dir)
{
    section ((juce::String ("build 11 demos: ") + dir.getFullPathName()).toRawUTF8());
    auto src = pdVoice() != nullptr ? pdVoice() : sung(); auto P = prepOf (src);
    Recipe r; r.type = Type::Instrument; r.seed = 905; r.detail = 5; r.musical = 1.0; r.clear = true; r.knob = 0.0; r.style = Style::Hook; r.bars = 4; r.keyTonic = 9; r.keyMinor = true; r.engine = 8;
    std::shared_ptr<const Take> last;
    for (int e : { 8, 9 })
    {
        auto t = generate (*P, farEdit (*P, r, e));
        if (t == nullptr) { check (false, "rendered"); continue; }
        auto w = renderForExport (*t, 0.0f, 0.3f, 0.55f, 0.35f); juce::String err;
        const juce::String f = e == 8 ? "35-voice0-farmoves-BUILD10" : "36-voice0-farmoves-BUILD11";
        const bool ok = exporter::writeWav (w, 48000, dir.getChildFile (f + ".wav"), 24, err);
        check (ok && dsp::finiteAndClean (w), f + ".wav: VOICE 0 %, MUSICAL, A minor, every other note dragged 13-19 semitones away: " + (e == 8 ? "build 10 (the model past an octave)" : "build 11 (the natural re-pitch all the way)") + " (" + juce::String (dsp::clicksIn (w)) + " clicks)");
        last = t;
    }
    if (last != nullptr)
    {
        SliceProcessor p; juce::String e1;
        Recipe rb = r; rb.engine = 9; rb.knob = 0.6; rb.edit.bed = 0.6; rb.edit.chords = 1; auto tb = generate (*P, rb);
        const bool a = tb != nullptr && exporter::writeMidi (tb->midi, dir.getChildFile ("37-DRAG-MIDI-melody.mid"), e1);
        const bool b = tb != nullptr && exporter::writeMidi (p.chordsMidiOf (*tb), dir.getChildFile ("38-DRAG-CHORDS.mid"), e1);
        check (a && b, "37-DRAG-MIDI-melody.mid + 38-DRAG-CHORDS.mid: one track each (CHORD BED on, RICH)");
        if (tb != nullptr) { auto w = renderForExport (*tb, 0.0f, 0.3f, 0.55f, 0.35f); juce::String err; exporter::writeWav (w, 48000, dir.getChildFile ("39-midi-demo-take.wav"), 24, err); }
    }
}
// ---------------------------------------------------------------- the window: DRAG MIDI and DRAG CHORDS side by side
static void uiEleven (const juce::File& dir)
{
    section ("build 11 UI (DRAG CHORDS on the roll page): the old window's page, removed in build 16 (the new screen has no such page)");
    juce::ignoreUnused (dir);
}
} // namespace eleven
// ---------------------------------------------------------------- diag: where the export of a CHORD BED take clicks (GhostSliceTests clickdiag)
namespace eleven
{
static void clickPositions (const juce::AudioBuffer<float>& b, const char* what, double spb)
{
    const int n = b.getNumSamples(), W = 256; const float* x = b.getReadPointer (0);
    std::vector<float> d2 ((size_t) n); for (int i = 1; i + 1 < n; ++i) d2[(size_t) i] = std::abs (x[i + 1] - 2.0f * x[i] + x[i - 1]);
    std::vector<double> cs ((size_t) n + 1, 0.0); for (int i = 0; i < n; ++i) cs[(size_t) i + 1] = cs[(size_t) i] + (double) d2[(size_t) i] * d2[(size_t) i];
    for (int i = 1; i + 1 < n; ++i)
    {
        const int a = juce::jmax (0, i - W), e = juce::jmin (n, i + W);
        const double loc = std::sqrt ((cs[(size_t) e] - cs[(size_t) a] - (double) d2[(size_t) i] * d2[(size_t) i]) / juce::jmax (1, e - a - 1));
        if (d2[(size_t) i] > 0.25f * 0.25f && d2[(size_t) i] > 10.0 * loc + 1e-4) { std::printf ("      %s click at %.4f s (beat %.3f) d2 %.4f loc %.5f x %.4f\n", what, i / 48000.0, i / 48000.0 / spb, d2[(size_t) i], loc, x[i]); i += 32; }
    }
}
static void clickDiag()
{
    auto P = prepOf (pdVoice() != nullptr ? pdVoice() : spoken());
    {   // the recording itself at the export's loudness: does the detector already find its plosives?
        juce::AudioBuffer<float> w; w.makeCopyOf (P->work->buffer()); w.applyGain ((float) dsp::dbToGain (-15.1 - dsp::lufs (w, 48000)));
        std::printf ("  the recording at -15.1 LUFS: %d clicks, peak %.3f\n", dsp::clicksIn (w), dsp::peakOf (w)); clickPositions (w, "recording", 0.5);
        Recipe r; r.type = Type::Instrument; r.seed = 2026; r.detail = 2029; r.musical = 0.85; r.clear = true; r.knob = 0.0; r.musical = 0.0; r.style = Style::Auto; r.bars = 4;
        auto t = generate (*P, r); if (t != nullptr) { auto e = renderForExport (*t, 0.4f, 0.3f, 0.55f, 0.35f); std::printf ("  VOICE 0 NATURAL export: %d clicks\n", dsp::clicksIn (e)); clickPositions (e, "voice0", 0.5); }
    }
    for (int key : { 5, 9 })
    for (double bedv : { 0.0, 0.6 })
    {
        Recipe r; r.type = Type::Instrument; r.seed = 2026; r.detail = 2029; r.musical = 0.85; r.clear = true; r.knob = 0.6; r.style = Style::Auto; r.bars = 4;
        r.keyTonic = key; r.keyMinor = true; r.edit.bed = bedv; r.edit.chords = 1; std::printf ("  bed %.1f\n", bedv);
        auto t = generate (*P, r); if (t == nullptr) continue;
        const double spb = 60.0 / t->bpm;
        auto w = renderForExport (*t, 0.4f, 0.3f, 0.55f, 0.35f);
        std::printf ("  key %d: preview clicks %d, export clicks %d, export %.2f LUFS, gain %.2f dB\n", key, dsp::clicksIn (t->preview), dsp::clicksIn (w), dsp::lufs (w, 48000), dsp::lufs (w, 48000) - dsp::lufs (t->preview, 48000));
        clickPositions (w, "export", spb);
        juce::AudioBuffer<float> pv; pv.makeCopyOf (t->preview); pv.applyGain ((float) dsp::dbToGain (dsp::lufs (w, 48000) - dsp::lufs (t->preview, 48000)));
        clickPositions (pv, "preview x gain", spb);
        { const int c0 = (int) (3.5034 * 48000); std::printf ("      preview around:"); for (int i = c0 - 24; i < c0 + 24; i += 2) std::printf (" %.4f", t->preview.getSample (0, i)); std::printf ("\n      export around:"); for (int i = c0 - 24; i < c0 + 24; i += 2) std::printf (" %.4f", w.getSample (0, i)); std::printf ("\n"); }
        for (auto& e : t->notes) if (e.beat > 6 && e.beat < 8) std::printf ("      mel near: note %d beat %.3f len %.3f\n", e.note, e.beat, e.length);
        if (false) for (auto& e : t->chordEvents) std::printf ("      bed note %d beat %.2f len %.2f\n", e.note, e.beat, e.length);
        for (auto& e : t->notes) std::printf ("      mel note %d beat %.2f len %.2f\n", e.note, e.beat, e.length);
    }
}
}
