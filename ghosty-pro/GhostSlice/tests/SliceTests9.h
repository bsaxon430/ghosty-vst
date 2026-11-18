// GHOST SLICE build 9 tests (included by SliceTests.cpp after SliceTests8.h): the in-order PSOLA at VOICE 0 (engine 7), TEXTURE edits
// re-rendered fast, older takes edited (upgraded silently), VARY remembers deleted notes, chop notes that change octave, the CHORD BED.
#pragma once

namespace nine
{
using namespace six;
using namespace eight;

// the worst spectral-envelope correlation of the re-pitched syllables of a VOICE 0 take against the recording (build 6's measure)
static double worstSyllableCorr (const Prepared& P, const Take& t, int* count = nullptr)
{
    const auto& W = P.work->buffer(); double worstC = 1.0; int n = 0; const double spb = 60.0 / t.bpm;
    for (size_t i = 0; i + 1 < t.notes.size() && n < 6; ++i)
    {
        const auto& u = t.used[(size_t) i % t.used.size()]; const int sl = (int) ((u.second - u.first) * 48000.0), nl = (int) ((t.notes[i + 1].beat - t.notes[i].beat) * spb * 48000.0);
        const int len = juce::jmin (sl, nl); if (len < 4096 + 1024) continue;
        const int o = (int) (t.notes[i].beat * spb * 48000.0), a = (int) (u.first * 48000.0);
        const double cc = pearson (envelope (W, a, a + len), envelope (t.preview, o, o + len));
        if (juce::SystemStats::getEnvironmentVariable ("GS_DEBUG", "").isNotEmpty()) std::printf ("      note %d midi %d len %d corr %.3f hnr src %.1f out %.1f\n", (int) i, t.notes[i].note, len, cc, clearm::hnr (W, a, a + len), clearm::hnr (t.preview, o, o + len));
        worstC = std::min (worstC, cc); ++n;
    }
    if (count != nullptr) *count = n;
    return worstC;
}
static std::shared_ptr<const Take> instE (std::shared_ptr<const Prepared> P, double voice, double musical, int engine, uint32_t seed = 2024)
{
    Recipe r; r.type = Type::Instrument; r.seed = seed; r.detail = 5; r.musical = musical; r.clear = true; r.engine = engine; r.knob = voice; r.style = Style::Hook; r.bars = 4;
    return generate (*P, r);
}

static void psolaNine()
{
    section ("build 9: VOICE 0 with MUSICAL on: the in-order PSOLA on the voice's own periods (as close to the recording as it gets)");
    for (auto src : { sung(), pdVoice() })
    {
        if (src == nullptr) continue;
        const juce::String nm = src == sung() ? "synthetic voice" : "public-domain voice";
        auto P = prepOf (src);
        auto t6 = instE (P, 0.0, 1.0, 6), t7 = instE (P, 0.0, 1.0, 7);
        if (t6 == nullptr || t7 == nullptr) { check (false, nm + ": the takes"); continue; }
        int n6 = 0, n7 = 0; const double c6 = worstSyllableCorr (*P, *t6, &n6), c7 = worstSyllableCorr (*P, *t7, &n7);
        // (the same notes: only the re-pitch changed)
        check (sameNotes (t6->notes, t7->notes), nm + ": the same melody in both (only the re-pitch differs)");
        check (c7 >= c6 && c7 >= (src == sung() ? 0.98 : 0.95), nm + ": syllable envelope correlation, worst of " + juce::String (n7) + " notes: build 8 " + f2 (c6, 3) + " -> build 9 " + f2 (c7, 3) + " (wanted >= " + (src == sung() ? "0.98" : "0.95") + ")");
        const int k7 = dsp::clicksIn (t7->preview);
        check (dsp::finiteAndClean (t7->preview) && k7 == 0 && dsp::peakOf (t7->preview) < 0.999f, nm + ": clean (no clicks, no clipping)");
        // identity still holds at engine 7 (no pitch change = the recording itself)
        auto id = instE (P, 0.0, 0.0, 7);
        bool same = id != nullptr && t6 != nullptr; auto id6 = instE (P, 0.0, 0.0, 6);
        same = same && id6 != nullptr && id->digest == id6->digest;
        check (same, nm + ": VOICE 0 + NATURAL (no re-pitch) is still the recording itself, bit for bit as build 8");
        // the sweep has no comb at engine 7 (PSOLA at 0, the model above 5 %)
        std::vector<double> fl; for (double v : { 0.0, 0.25, 0.5, 0.75, 1.0 }) { auto t = instE (P, v, 1.0, 7); fl.push_back (t != nullptr ? flatnessDb (t->preview) : 0.0); }
        const double ends = std::min (fl.front(), fl.back()); double worst = 0; for (size_t i = 1; i + 1 < fl.size(); ++i) worst = std::min (worst, fl[i] - ends);
        check (worst > -3.0, nm + ": the VOICE sweep still has no comb (flatness " + f2 (fl[0], 1) + " " + f2 (fl[1], 1) + " " + f2 (fl[2], 1) + " " + f2 (fl[3], 1) + " " + f2 (fl[4], 1) + " dB)");
    }
}

static void editsNine (const juce::File& dir)
{
    section ("build 9: TEXTURE edits fast, older takes edit (upgraded), VARY remembers deletions, chop octaves, the CHORD BED");
    auto P = prepOf (sung());
    // 1. TEXTURE: an edit re-renders fast and equals a full render
    {
        Recipe r; r.type = Type::Texture; r.seed = 44; r.detail = 3; r.clear = true; r.keyTonic = 9; r.keyMinor = true;
        const double t0 = juce::Time::getMillisecondCounterHiRes(); auto a = generate (*P, r); const double full = juce::Time::getMillisecondCounterHiRes() - t0;
        Recipe r2 = r; NoteEvent n = a->notes[1]; r2.edit.removed.push_back ({ n.beat, n.note }); n.note = stepInKey (n.note, 2, Key { 9, true }); n.mine = true; r2.edit.locked.push_back (n);
        const double t1 = juce::Time::getMillisecondCounterHiRes(); auto b = generate (*P, r2, {}, a); const double fast = juce::Time::getMillisecondCounterHiRes() - t1;
        auto c = generate (*P, r2);
        check (b != nullptr && c != nullptr && b->digest == c->digest && b->digest != a->digest && fast < 1000.0, "TEXTURE: an edit re-renders in " + f2 (fast, 0) + " ms (a whole take: " + f2 (full, 0) + " ms), bit for bit a full render");
    }
    // 2. CHOPS: your note an octave up plays an octave up (the chop re-pitched cleanly); engine 6 folded it back
    {
        Recipe r; r.type = Type::Chops; r.seed = 12; r.detail = 3; r.clear = true; r.keyTonic = 0; r.keyMinor = false; r.style = Style::Hook; r.bars = 4;
        auto a = generate (*P, r);
        Recipe r2 = r; NoteEvent n = a->notes[2]; r2.edit.removed.push_back ({ n.beat, n.note }); n.note += 12; n.mine = true; r2.edit.locked.push_back (n);
        auto b = generate (*P, r2); Recipe r6 = r2; r6.engine = 6; auto b6 = generate (*P, r6);
        const int j = noteAt (b->notes, n.beat), j6 = noteAt (b6->notes, n.beat);
        check (j >= 0 && b->notes[(size_t) j].note == n.note && j6 >= 0 && b6->notes[(size_t) j6].note != n.note && dsp::clicksIn (b->preview) == 0 && dsp::finiteAndClean (b->preview),
               "CHOPS: a chop note moved an octave up stays an octave up (" + juce::MidiMessage::getMidiNoteName (n.note, true, true, 5) + "; build 8 folded it to " + (j6 >= 0 ? juce::MidiMessage::getMidiNoteName (b6->notes[(size_t) j6].note, true, true, 5) : juce::String ("?")) + "), clean");
        // its pitch really moved: the chop's spectral centroid of the harmonics rises (autocorrelation pitch of the piece)
        const double spb = 60.0 / b->bpm; const int s0 = (int) (n.beat * spb * 48000.0) + 960, len = juce::jmin (4096, (int) (n.length * spb * 48000.0 * 0.4));
        auto f0of = [&] (const Buf& x) { auto m = monoOf (x); double best = 0; int bl = 0; for (int L = 40; L < 600; ++L) { double s = 0, e = 0; for (int i = 0; i < len && s0 + i + L < m.getNumSamples(); ++i) { s += m.getSample (0, s0 + i) * m.getSample (0, s0 + i + L); e += m.getSample (0, s0 + i) * m.getSample (0, s0 + i); } if (e > 0 && s / e > best) { best = s / e; bl = L; } } return bl > 0 ? 48000.0 / bl : 0.0; };
        const double fa = f0of (b6->preview), fb = f0of (b->preview);
        check (len > 1000 && fa > 0 && fb > 1.6 * fa, "its pitch is an octave higher in the audio (" + f2 (fa, 0) + " Hz -> " + f2 (fb, 0) + " Hz)");
    }
    // 3. CHORD BED: off = exactly as before; on = the chords under the melody, clean, and the fast re-render equals a full one
    {
        Recipe r; r.type = Type::Instrument; r.seed = 2027; r.detail = 9; r.clear = true; r.knob = 0.6; r.style = Style::Hook; r.bars = 4; r.keyTonic = 9; r.keyMinor = true;
        auto a = generate (*P, r);
        Recipe r0 = r; r0.edit.bed = 0.0; auto a0 = generate (*P, r0, {}, a);
        Recipe rb = r; rb.edit.bed = 0.6; rb.edit.chords = 1; auto b = generate (*P, rb, {}, a), bf = generate (*P, rb);
        check (a0 != nullptr && a0->digest == a->digest, "CHORD BED at 0 (off, the default): the take is exactly as without it");
        check (b != nullptr && bf != nullptr && b->digest == bf->digest && b->digest != a->digest && dsp::clicksIn (b->preview) == 0 && dsp::finiteAndClean (b->preview) && dsp::peakOf (b->preview) < 0.999f,
               "CHORD BED 60 % + RICH: a new layer, clean, the fast re-render = the full one");
        // the bed is the chords: the share of the spectrum's energy on the chords' pitch classes rises
        auto chordShare = [&] (const Take& t)
        {
            auto m = monoOf (t.preview); const int N = 8192; juce::dsp::FFT f (13); std::vector<float> buf ((size_t) N * 2); double on = 0, all = 0;
            const double spbb = 60.0 / t.bpm;
            for (int s2 = 0; s2 + N <= m.getNumSamples(); s2 += N)
            {
                const auto& ch = t.chords[(size_t) ((int) (s2 / 48000.0 / spbb / t.beatsPerChord) % (int) t.chords.size())];
                std::fill (buf.begin(), buf.end(), 0.0f); for (int i = 0; i < N; ++i) buf[(size_t) i] = m.getSample (0, s2 + i) * (float) (0.5 - 0.5 * std::cos (6.283185307 * i / N));
                f.performFrequencyOnlyForwardTransform (buf.data());
                for (int k = (int) (90.0 * N / 48000); k < (int) (1200.0 * N / 48000); ++k) { const double hz = k * 48000.0 / N, p = (double) buf[(size_t) k] * buf[(size_t) k]; all += p; const int pc = ((int) std::lround (dsp::hzToMidi (hz)) % 12 + 12) % 12; if (std::find (ch.pcs.begin(), ch.pcs.end(), pc) != ch.pcs.end()) on += p; }
            }
            return all > 0 ? on / all : 0.0;
        };
        Recipe rr = r; rr.edit.chords = 1; auto bo = generate (*P, rr, {}, a);
        const double s0 = chordShare (*bo), s1 = chordShare (*b);
        check (s1 > s0 + 0.03, "with the bed, more of the sound sits on the chords' notes (" + juce::String (juce::roundToInt (s0 * 100)) + " % -> " + juce::String (juce::roundToInt (s1 * 100)) + " %)");
    }
    // 4. (the roll edits of an older take: the old window's UNDER THE HOOD page, removed in build 16)
    juce::ignoreUnused (dir);
}

// ---------------------------------------------------------------- the demos: CHORD BED off / on, VOICE 0 MUSICAL before / after
static void abNine (const juce::File& dir)
{
    section ((juce::String ("build 9 demos: ") + dir.getFullPathName()).toRawUTF8());
    dir.createDirectory();
    auto src = pdVoice() != nullptr ? pdVoice() : sung(); auto P = prepOf (src);
    auto write = [&] (std::shared_ptr<const Take> t, const juce::String& f, const juce::String& what)
    {
        if (t == nullptr) { check (false, f + ": rendered"); return; }
        auto w = renderForExport (*t, 0.0f, 0.3f, 0.55f, 0.35f); juce::String err;
        const bool ok = exporter::writeWav (w, 48000, dir.getChildFile (f + ".wav"), 24, err);
        check (ok && dsp::finiteAndClean (w) && dsp::clicksIn (w) == 0, f + ".wav: " + what + " (" + f2 (w.getNumSamples() / 48000.0, 1) + " s, no clicks; " + t->progName + ")");
    };
    Recipe r; r.type = Type::Instrument; r.seed = 2027; r.detail = 9; r.musical = 0.85; r.clear = true; r.knob = 0.6; r.style = Style::Hook; r.bars = 4; r.edit.chords = 1;
    auto a = generate (*P, r);
    if (a != nullptr && a->writtenChords.size() > 1) { r.edit.chordAlt.assign (a->writtenChords.size(), -1); r.edit.chordAlt[1] = ((a->writtenChords[1].degree + 5) % 7) * 8 + 2; a = generate (*P, r, {}, a); }
    Recipe rb = r; rb.edit.bed = 0.6;
    write (a, "29-chord-bed-OFF", "INSTRUMENT VOICE 60 %, RICH chords, chord 2 swapped, CHORD BED off");
    write (generate (*P, rb, {}, a), "30-chord-bed-ON", "the same take, CHORD BED 60 %: the chords held softly under the melody, on the same voice");
    Recipe v6; v6.type = Type::Instrument; v6.seed = 2024; v6.detail = 5; v6.musical = 1.0; v6.clear = true; v6.knob = 0.0; v6.style = Style::Hook; v6.bars = 4; v6.engine = 6;
    Recipe v7 = v6; v7.engine = 7;
    write (generate (*P, v6), "31-voice0-musical-BEFORE", "VOICE 0 %, MUSICAL: build 8 (every re-pitched syllable through the model)");
    write (generate (*P, v7), "32-voice0-musical-AFTER", "VOICE 0 %, MUSICAL: build 9 (small re-pitches by in-order PSOLA on the voice's own periods)");
}
} // namespace nine
