// GHOST SLICE build 10 tests (included by SliceTests.cpp after SliceTests9.h): VOICE 0 big moves (engine 8: the in-order PSOLA with the
// formant guard up to an octave), the CHORDS track in DRAG MIDI, older CHOPS takes keeping their phrase and sound when edited.
#pragma once

namespace ten
{
using namespace six;
using namespace eight;
using namespace nine;

// every syllable of a VOICE 0 take: its re-pitch (semitones from the syllable's own pitch) and its spectral-envelope correlation / HNR
struct SylScore { double semis = 0, corr = 1, hnrOff = 0; };
static std::vector<SylScore> scoresOf (const Prepared& P, const Take& t)
{
    std::vector<SylScore> out; const auto& W = P.work->buffer(); const double spb = 60.0 / t.bpm;
    for (size_t i = 0; i + 1 < t.notes.size() && i < 16; ++i)
    {
        const auto& u = t.used[i % t.used.size()]; const int sl = (int) ((u.second - u.first) * 48000.0), nl = (int) ((t.notes[i + 1].beat - t.notes[i].beat) * spb * 48000.0);
        const int len = juce::jmin (sl, nl); if (len < 4096 + 1024) continue;
        const int o = (int) (t.notes[i].beat * spb * 48000.0), a = (int) (u.first * 48000.0);
        std::vector<double> f; for (int s = a; s < a + sl; s += 240) { const double h = P.f0AtWork ((double) s); if (h > 40.0) f.push_back (h); }
        if (f.size() < 3) continue;
        std::nth_element (f.begin(), f.begin() + (long) f.size() / 2, f.end());
        SylScore sc; sc.semis = t.notes[i].note - dsp::hzToMidi (f[f.size() / 2]); sc.semis -= 0.0;
        sc.corr = pearson (envelope (W, a, a + len), envelope (t.preview, o, o + len));
        sc.hnrOff = std::abs (clearm::hnr (W, a, a + len) - clearm::hnr (t.preview, o, o + len));
        out.push_back (sc);
    }
    return out;
}

static void bigMovesTen()
{
    section ("build 10: VOICE 0 big moves (more than a fifth): the natural in-order re-pitch with the formant guard, up to an octave");
    auto src = pdVoice() != nullptr ? pdVoice() : sung(); auto P = prepOf (src);
    const juce::String nm = pdVoice() != nullptr ? "public-domain voice" : "synthetic voice";
    double w7 = 1, w8 = 1, h7 = 0, h8 = 0, a7 = 0, a8 = 0, all7 = 1, all8 = 1; int nBig = 0;
    for (int key : { 0, 4, 7, 10 })
        for (uint32_t seed : { 2024u, 77u, 905u })
        {
            Recipe r; r.type = Type::Instrument; r.seed = seed; r.detail = 5; r.musical = 1.0; r.clear = true; r.knob = 0.0; r.style = Style::Hook; r.bars = 4; r.keyTonic = key; r.keyMinor = key % 2 == 1;
            Recipe r7 = r; r7.engine = 7; Recipe r8 = r; r8.engine = 8;
            auto t7 = generate (*P, r7), t8 = generate (*P, r8);
            if (t7 == nullptr || t8 == nullptr || ! sameNotes (t7->notes, t8->notes)) { check (false, "the same melody in both engines"); return; }
            const auto s7 = scoresOf (*P, *t7), s8 = scoresOf (*P, *t8);
            for (size_t i = 0; i < s7.size() && i < s8.size(); ++i)
            {
                all7 = std::min (all7, s7[i].corr); all8 = std::min (all8, s8[i].corr);
                if (s8[i].corr < 0.95 && juce::SystemStats::getEnvironmentVariable ("GS_DEBUG", "").isNotEmpty()) std::printf ("      LOW key %d seed %u semis %+.1f corr %.3f -> %.3f\n", key, seed, s7[i].semis, s7[i].corr, s8[i].corr);
                if (std::abs (s7[i].semis) <= 7.5) continue;
                ++nBig; w7 = std::min (w7, s7[i].corr); w8 = std::min (w8, s8[i].corr); a7 += s7[i].corr; a8 += s8[i].corr; h7 = std::max (h7, s7[i].hnrOff); h8 = std::max (h8, s8[i].hnrOff);
                if (juce::SystemStats::getEnvironmentVariable ("GS_DEBUG", "").isNotEmpty()) std::printf ("      key %d seed %u semis %+.1f corr %.3f -> %.3f hnr off %.1f -> %.1f\n", key, seed, s7[i].semis, s7[i].corr, s8[i].corr, s7[i].hnrOff, s8[i].hnrOff);
            }
            const int k7 = dsp::clicksIn (t7->preview), k8 = dsp::clicksIn (t8->preview);
            check (k8 <= k7 && dsp::finiteAndClean (t8->preview) && dsp::peakOf (t8->preview) < 0.999f, nm + " key " + juce::String (key) + " seed " + juce::String ((int) seed) + ": clean (clicks " + juce::String (k7) + " -> " + juce::String (k8) + ", peak " + f2 (dsp::peakOf (t8->preview), 2) + ")");
        }
    check (nBig >= 4, nm + ": " + juce::String (nBig) + " syllables moved by more than a fifth were measured");
    check (w8 >= w7 && a8 >= a7, nm + ", the moves bigger than a fifth: worst syllable-envelope correlation " + f2 (w7, 3) + " -> " + f2 (w8, 3) + ", mean " + f2 (a7 / juce::jmax (1, nBig), 3) + " -> " + f2 (a8 / juce::jmax (1, nBig), 3) + "; worst HNR off the recording " + f2 (h7, 1) + " -> " + f2 (h8, 1) + " dB");
    {   // build 9's measure (the same 6 syllables, VOICE 0 MUSICAL): build 8 (engine 6) / build 9 (engine 7) / build 10 (engine 8)
        auto t6 = instE (P, 0.0, 1.0, 6), t7 = instE (P, 0.0, 1.0, 7), t8 = instE (P, 0.0, 1.0, 8);
        const double c6 = worstSyllableCorr (*P, *t6), c7 = worstSyllableCorr (*P, *t7), c8 = worstSyllableCorr (*P, *t8);
        check (c8 >= 0.95, nm + ", build 9's six syllables (still >= 0.95; the second guard pass trades 0.005 here for the 12-take worst case): worst correlation " + f2 (c6, 3) + " (build 8) -> " + f2 (c7, 3) + " (build 9) -> " + f2 (c8, 3) + " (build 10)");
    }
    check (all8 >= all7 - 1e-9, nm + ", every syllable of the 12 takes: worst " + f2 (all7, 3) + " -> " + f2 (all8, 3));
}

static int noteOnsIn (const juce::MidiFile& f, int track, int* channel = nullptr)
{
    int n = 0; if (track >= f.getNumTracks()) return -1;
    for (auto* e : *f.getTrack (track)) if (e->message.isNoteOn()) { ++n; if (channel != nullptr) *channel = e->message.getChannel(); }
    return n;
}
static void midiTen()
{
    section ("build 10: DRAG MIDI carries the chords (a second track, channel 2: the CHORD BED's notes, or the chords)");
    auto P = prepOf (sung());
    Recipe r; r.type = Type::Instrument; r.seed = 2027; r.detail = 9; r.clear = true; r.knob = 0.6; r.style = Style::Hook; r.bars = 4; r.keyTonic = 9; r.keyMinor = true;
    auto a = generate (*P, r);
    int ch = 0; const int n1 = noteOnsIn (a->midi, 1, &ch);
    check (a->midi.getNumTracks() == 2 && noteOnsIn (a->midi, 0) == (int) a->notes.size() && n1 == (int) chordNotes (a->chords, a->beatsPerChord, 16.0).size() && ch == 2,
           "INSTRUMENT: track 1 = the melody (" + juce::String ((int) a->notes.size()) + " notes), track 2 = the chords on channel 2 (" + juce::String (n1) + " notes: " + a->progName + ")");
    Recipe rb = r; rb.edit.bed = 0.6; rb.edit.chords = 1; auto b = generate (*P, rb, {}, a);
    check (b->midi.getNumTracks() == 2 && noteOnsIn (b->midi, 1) > 0 && b->digest != a->digest, "with the CHORD BED on, the second track is the bed's own notes (" + juce::String (noteOnsIn (b->midi, 1)) + ", RICH)");
    Recipe rc; rc.type = Type::Chops; rc.seed = 12; rc.detail = 3; rc.clear = true; rc.style = Style::Hook; rc.bars = 4;
    auto c = generate (*P, rc);
    check (c->midi.getNumTracks() == 2 && noteOnsIn (c->midi, 0) == (int) c->notes.size() && noteOnsIn (c->midi, 1) > 0, "CHOPS: the phrase + the chords track");
    Recipe r6 = r; r6.engine = 6; auto d = generate (*P, r6);
    Recipe rp; rp.type = Type::Pad; rp.seed = 31; rp.detail = 4; rp.clear = true; rp.style = Style::Dreamy; rp.bars = 8; auto pd = generate (*P, rp);
    check (d->midi.getNumTracks() == 1 && pd->midi.getNumTracks() == 1 && noteOnsIn (pd->midi, 0) == (int) pd->notes.size(), "a build 8 take's MIDI is as it was (one track); a PAD's MIDI is its chords (" + juce::String (noteOnsIn (pd->midi, 0)) + " notes)");
    // the file DRAG MIDI writes
    {
        ghosty::License::testForce().store (1);
        SliceProcessor p; p.prepareToPlay (48000, 512); p.loadSourceForTests (sung()); pump (20); p.waitIdle (60000);
        p.type = Type::Instrument; p.generate(); p.waitIdle (120000);
        juce::String err; const auto f = p.writeTakeMidi (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("gs10-midi"), err);
        juce::MidiFile mf; bool ok = false; if (f.existsAsFile()) { juce::FileInputStream in (f); ok = mf.readFrom (in); }
        juce::String name; if (ok && mf.getNumTracks() > 1) for (auto* e : *mf.getTrack (1)) if (e->message.isTextMetaEvent()) name = e->message.getTextFromTextMetaEvent();
        check (ok && mf.getNumTracks() == 2 && name == "CHORDS" && noteOnsIn (mf, 0) == (int) p.currentTake()->notes.size(), "the .mid DRAG MIDI writes: 2 tracks, the second named CHORDS");
    }
}

// an older (build 5) take of a type, reopened in a processor + editor (the session JSON as build 5 wrote it)
static void oldTakeOf (SliceProcessor& p, SliceProcessor& q, Type ty)
{
    { auto file = wavOf (sung(), "sung-play10.wav"); juce::String err; p.loadAudio (file, err); } pump (20); p.waitIdle (60000);
    p.keyTonic = 9; p.keyMinor = true; p.type = ty; p.generate(); p.waitIdle (120000); p.playPreview (false);
    juce::MemoryBlock mb; p.getStateInformation (mb);
    auto v = juce::JSON::parse (mb.toString());
    if (auto* arr = v["takes"].getArray()) for (auto& x : *arr) if (auto* o = x.getDynamicObject()) { o->setProperty ("engine", 5); for (auto* k : { "edit", "knob", "style", "deep", "bars" }) o->removeProperty (k); }
    const auto js = juce::JSON::toString (v, true);
    q.setStateInformation (js.toRawUTF8(), (int) js.getNumBytesAsUTF8()); waitReopened (q); q.waitIdle (120000); pump (20);
}
static void oldChopsTen (const juce::File& dir)
{
    section ("build 10: an older CHOPS take edits keeping its phrase (the same chops, in the same order); older takes keep their sound's parts");
    ghosty::License::testForce().store (1);
    for (Type ty : { Type::Chops, Type::Instrument })
    {
        SliceProcessor p, q; p.setPlayConfigDetails (0, 2, 48000, 512); p.prepareToPlay (48000, 512); q.setPlayConfigDetails (0, 2, 48000, 512); q.prepareToPlay (48000, 512);
        std::unique_ptr<SliceEditor> eq (static_cast<SliceEditor*> (q.createEditor())); eq->setVisible (true); pump (20); eq->setSize (1477, 1065); eq->setLook (1); pump (10);
        oldTakeOf (p, q, ty);
        auto old = q.currentTake();
        const juce::String nm = typeName (ty);
        if (old == nullptr || old->recipe.engine != 5) { check (false, nm + ": an older take reopened"); continue; }
        const auto dOld = old->digest; const auto oldNotes = notesOf (*old); const auto oldUsed = old->used;
        eq->hoodBtn.triggerClick(); pump (10); auto& roll = eq->hood.roll; roll.sync(); pump (5);
        bool chopNums = ty != Type::Chops; for (auto& n : oldNotes) chopNums = chopNums || n.chop >= 0;
        check (roll.notes.size() == oldNotes.size() && oldNotes.size() >= 4 && chopNums, nm + ": the older take's " + juce::String ((int) oldNotes.size()) + " notes on the roll" + (ty == Type::Chops ? " with their chop numbers" : ""));
        if (ty == Type::Chops) savePng (shot (*eq), dir.getChildFile ("ui10-old-chops-before.png"));
        roll.sel.assign (roll.notes.size(), false); const int mi = (int) roll.notes.size() / 2; roll.sel[(size_t) mi] = true;
        roll.nudge (0.25, 0); q.waitIdle (120000); pump (20); roll.sync();
        auto up = q.currentTake();
        int kept = 0; for (size_t i = 0; i < oldNotes.size(); ++i) { if ((int) i == mi) continue; const int j = noteAt (up->notes, oldNotes[i].beat); if (j >= 0 && (ty != Type::Chops || up->notes[(size_t) j].chop == oldNotes[i].chop)) ++kept; }
        bool sameParts = up->used.size() == oldUsed.size(); for (size_t i = 0; sameParts && i < oldUsed.size(); ++i) sameParts = std::abs (up->used[i].first - oldUsed[i].first) < 0.01 && std::abs (up->used[i].second - oldUsed[i].second) < 0.01;
        check (up->recipe.engine == 8 && kept >= (int) oldNotes.size() - 2 && sameParts, nm + ": one note moved: upgraded (engine 8), " + juce::String (kept) + " of the other " + juce::String ((int) oldNotes.size() - 1) + " notes kept" + (ty == Type::Chops ? " with their chops" : "") + ", the same " + juce::String ((int) oldUsed.size()) + " parts of the sound");
        if (ty == Type::Chops) savePng (shot (*eq), dir.getChildFile ("ui10-old-chops-after.png"));
        q.undoEdit(); q.waitIdle (120000); pump (10);
        check (q.currentTake() != nullptr && q.currentTake()->recipe.engine == 5 && q.currentTake()->digest == dOld && q.takes()[(size_t) q.currentIndex()].recipe.parts.empty(), nm + ": UNDO brings the older take back, bit for bit");
    }
}

static void abTen (const juce::File& dir)
{
    section ((juce::String ("build 10 demos: ") + dir.getFullPathName()).toRawUTF8());
    auto src = pdVoice() != nullptr ? pdVoice() : sung(); auto P = prepOf (src);
    Recipe r; r.type = Type::Instrument; r.seed = 77; r.detail = 5; r.musical = 1.0; r.clear = true; r.knob = 0.0; r.style = Style::Hook; r.bars = 4; r.keyTonic = 0; r.keyMinor = false;
    for (int e : { 7, 8 })
    {
        Recipe x = r; x.engine = e; auto t = generate (*P, x);
        if (t == nullptr) { check (false, "rendered"); continue; }
        auto w = renderForExport (*t, 0.0f, 0.3f, 0.55f, 0.35f); juce::String err;
        const juce::String f = e == 7 ? "33-voice0-bigmoves-BUILD9" : "34-voice0-bigmoves-BUILD10";
        const bool ok = exporter::writeWav (w, 48000, dir.getChildFile (f + ".wav"), 24, err);
        check (ok && dsp::finiteAndClean (w), f + ".wav: VOICE 0 %, MUSICAL, C major (syllables moved up to an octave): " + (e == 7 ? "build 9 (the model above a fifth)" : "build 10 (in-order PSOLA + formant guard up to an octave)") + " (" + juce::String (dsp::clicksIn (w)) + " clicks)");
    }
}
} // namespace ten
