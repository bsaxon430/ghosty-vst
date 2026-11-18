// GHOST SLICE build 8 tests (included by SliceTests.cpp after SliceTests7.h): PLAY IT - the editable roll (always in key, grid,
// undo / redo, saved, VARY keeps your notes, DRAG MIDI exports them), ArpEZator's controls (MASTER, CHORDS SIMPLE / RICH, chord
// swaps, the ARP, GLIDE / FATTEN), fast re-renders that equal a full render, no audio-thread allocation, the screenshots, the demo.
#pragma once
#include "../Brain.h"

namespace eight
{
using namespace six;

static bool allInKey (const std::vector<NoteEvent>& ev, const Key& k) { for (auto& e : ev) if (! inScale (e.note, k)) return false; return true; }
static bool oneLine (const std::vector<NoteEvent>& ev) { for (size_t i = 1; i < ev.size(); ++i) if (ev[i].beat < ev[i - 1].beat + ev[i - 1].length - 1e-6) return false; return true; }
static bool sameNotes (const std::vector<NoteEvent>& a, const std::vector<NoteEvent>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) if (a[i].note != b[i].note || std::abs (a[i].beat - b[i].beat) > 1e-9 || std::abs (a[i].length - b[i].length) > 1e-9) return false;
    return true;
}

// ---------------------------------------------------------------- the music side (PlayEdit), no audio
static void coreEight()
{
    section ("build 8 PLAY IT (the music): your notes always in the key, chord swaps in the key, ArpEZator's arp / MASTER / SIMPLE / RICH");
    // 1. random edits (pitches anywhere, also out of the key) through the line step: every note lands in the key, inside the length, one line
    {
        int bad = 0, total = 0; juce::Random rnd (8080);
        for (const Key k : { Key { 9, true }, Key { 0, false }, Key { 6, true }, Key { 3, false }, Key { 11, false } })
            for (const char* st : { "HOOK", "DREAMY", "WISTFUL", "POP BRAIN", "MIDNIGHT ARP", "EMO", "MELANCHOLIC" })
                for (int bars : { 1, 4, 8, 16 })
                {
                    auto S = compose (k, 64, styleFromName (st), bars, 7u + (uint32_t) bars, 3u, 0, 0.5, 0.5, {}, 120.0);
                    NoteEdit e;
                    for (int i = 0; i < 6; ++i) { NoteEvent n; n.note = 40 + rnd.nextInt (50); n.beat = rnd.nextDouble() * bars * 4.2; n.length = 0.1 + rnd.nextDouble() * 3.0; n.link = rnd.nextInt (3); e.locked.push_back (n); }
                    for (size_t i = 0; i < S.melody.size(); i += 3) e.removed.push_back ({ S.melody[i].beat, S.melody[i].note });
                    e.chords = rnd.nextInt (3) - 1; e.master = rnd.nextDouble(); e.arp = rnd.nextInt (edit::kArps); e.arpRate = rnd.nextInt (2); e.arpOct = 1 + rnd.nextInt (3);
                    for (size_t i = 0; i < S.chords.size(); ++i) e.chordAlt.push_back (rnd.nextInt (3) == 0 ? -1 : rnd.nextInt (56));
                    edit::editSong (S, k, 64, bars, 7u, e, true, true);
                    for (bool mono : { true, false })
                    {
                        ++total;
                        const auto line = edit::editLine (S.melody, e, k, bars, mono);
                        bool ok = allInKey (line, k) && (! mono || oneLine (line));
                        for (auto& n : line) ok = ok && n.beat >= 0 && n.beat + n.length <= bars * 4.0 + 1e-6 && n.length > 0;
                        for (auto& c : S.chords) for (int n : c.notes) ok = ok && inScale (n, k);
                        if (! ok)
                        {
                            ++bad;
                            if (juce::SystemStats::getEnvironmentVariable ("GS_DEBUG", "").isNotEmpty())
                            {
                                std::printf ("    bad: %s bars %d mono %d inKey %d line %d\n", st, bars, (int) mono, (int) allInKey (line, k), (int) oneLine (line));
                                for (auto& x : line) if (! (x.beat >= 0 && x.beat + x.length <= bars * 4.0 + 1e-6 && x.length > 0)) std::printf ("      note %d beat %.4f len %.4f\n", x.note, x.beat, x.length);
                                for (auto& c : S.chords) for (int nn : c.notes) if (! inScale (nn, k)) std::printf ("      chord %s note %d\n", c.name.toRawUTF8(), nn);
                            }
                        }
                    }
                }
        check (bad == 0, "random edits (out-of-key pitches, past the end, overlapping) x 5 keys x 7 styles x LENGTH 1/4/8/16: every note in the key, inside the length, one line when it is one (" + juce::String (total - bad) + " / " + juce::String (total) + ")");
    }
    // 2. the chord choices: all in the key, each one gives the chord it says
    {
        int bad = 0, n = 0, opts = 0;
        for (const Key k : { Key { 9, true }, Key { 0, false }, Key { 4, true }, Key { 10, false } })
            for (int d = 0; d < 7; ++d)
                for (int kind : { -1, 0, 1 })
                {
                    const auto& sc = scaleOf (k.minor); const int root = k.tonic + sc[(size_t) d];
                    Chord w = brain::chordOf (k, root, { 0, sc[(size_t) (d + 2) % 7] - sc[(size_t) d] + (d + 2 >= 7 ? 12 : 0), sc[(size_t) (d + 4) % 7] - sc[(size_t) d] + (d + 4 >= 7 ? 12 : 0) }, { 36 + ((root - 36) % 12 + 12) % 12, 60, 64, 67 });
                    w.degree = d;
                    const auto o = edit::chordOptions (k, w, kind); ++n; opts += (int) o.size();
                    bool ok = o.size() >= 6;
                    for (auto& x : o) { for (int m : x.chord.notes) ok = ok && inScale (m, k); for (int pc : x.chord.pcs) ok = ok && inScale (pc + 60, k); ok = ok && edit::chordFor (k, w, x.code, kind).name == x.chord.name; }
                    if (! ok) ++bad;
                }
        check (bad == 0, "the chords you can swap to: every one in the key and the chord its name says, every degree x major / minor x AS WRITTEN / SIMPLE / RICH (" + juce::String (opts) + " choices in " + juce::String (n) + " menus)");
    }
    // 3. SIMPLE = triads, RICH = colour
    {
        const Key k { 9, true };
        auto S = compose (k, 64, Style::Hook, 8, 11u, 3u, 0, 0.5, 0.5, {}, 120.0);
        NoteEdit simple; simple.chords = 0; NoteEdit rich; rich.chords = 1;
        const auto a = edit::editChords (S.chords, k, simple), b = edit::editChords (S.chords, k, rich);
        bool tri = ! a.empty(); for (auto& c : a) tri = tri && c.pcs.size() == 3;
        int richN = 0; for (auto& c : b) richN += c.pcs.size() >= 4 ? 1 : 0;
        check (tri && richN >= (int) b.size() / 2, "SIMPLE gives plain triads (" + juce::String ((int) a.size()) + " / " + juce::String ((int) a.size()) + "), RICH gives 7ths / 9ths (" + juce::String (richN) + " of " + juce::String ((int) b.size()) + " have 4+ notes)");
    }
    // 4. ArpEZator's arp: every pattern x rate x octaves, in the key, inside the length, deterministic, the patterns differ
    {
        const Key k { 9, true };
        auto S = compose (k, 64, Style::Hook, 4, 5u, 3u, 0, 0.5, 0.5, {}, 120.0);
        int bad = 0, total = 0; std::vector<std::vector<NoteEvent>> byPattern;
        for (int a = 1; a < edit::kArps; ++a)
            for (int rate : { 0, 1 })
                for (int oct : { 1, 2, 3 })
                {
                    NoteEdit e; e.arp = a; e.arpRate = rate; e.arpOct = oct; ++total;
                    const auto x = edit::arpOver (S.chords, S.beatsPerChord, k, 4, 64, e, 5u), y = edit::arpOver (S.chords, S.beatsPerChord, k, 4, 64, e, 5u);
                    bool ok = ! x.empty() && allInKey (x, k) && sameNotes (x, y);
                    for (auto& n : x) ok = ok && n.beat + n.length <= 16.0 + 1e-6;
                    if (rate == 1 && oct == 2) byPattern.push_back (x);
                    if (! ok) ++bad;
                }
        int differ = 0; for (size_t i = 1; i < byPattern.size(); ++i) differ += sameNotes (byPattern[i], byPattern[0]) ? 0 : 1;
        check (bad == 0, "the ARP: 7 patterns x RATE 1/8, 1/16 x 1-3 OCTAVES, every note in the key, inside the length, the same twice (" + juce::String (total - bad) + " / " + juce::String (total) + ")");
        check (differ >= 5, "the patterns sound different (" + juce::String (differ) + " of 6 differ from FIGURE)");
    }
    // 5. MASTER: SIMPLE thins out, WILD fills in (ArpEZator's tools), in the key
    {
        const Key k { 0, false };
        auto S = compose (k, 64, Style::Hook, 8, 21u, 3u, 0, 0.5, 0.5, {}, 120.0);
        const auto lo = edit::master (S.melody, S.chords, S.beatsPerChord, k, 8, 0.0, 3u), hi = edit::master (S.melody, S.chords, S.beatsPerChord, k, 8, 1.0, 3u), mid = edit::master (S.melody, S.chords, S.beatsPerChord, k, 8, 0.5, 3u);
        check (lo.size() < S.melody.size() && hi.size() > S.melody.size() && sameNotes (mid, S.melody) && allInKey (lo, k) && allInKey (hi, k),
               "MASTER: SIMPLE " + juce::String ((int) lo.size()) + " notes, as composed " + juce::String ((int) S.melody.size()) + ", WILD " + juce::String ((int) hi.size()) + " (all in the key)");
    }
    // 6. a chord swap re-fits the melody: on-beat notes over the new chord are its chord tones
    {
        const Key k { 9, true };
        auto S = compose (k, 64, Style::Hook, 4, 9u, 3u, 0, 0.5, 0.5, {}, 120.0);
        NoteEdit e; e.chordAlt.assign (S.chords.size(), -1);
        for (size_t i = 0; i < S.chords.size(); ++i) e.chordAlt[i] = ((S.chords[i].degree + 3) % 7) * 8;   // (every chord swapped)
        auto T = S; edit::editSong (T, k, 64, 4, 9u, e, false, false);
        bool fits = true, changed = true; int on = 0;
        for (size_t i = 0; i < S.chords.size(); ++i) changed = changed && T.chords[i].name != S.chords[i].name;
        for (auto& n : T.melody)
            if (std::abs (n.beat - std::round (n.beat)) < 1e-6 || n.length >= 1.0) { ++on; fits = fits && isChordTone (n.note, T.chords[(size_t) ((int) std::floor (n.beat / T.beatsPerChord) % (int) T.chords.size())]); }
        check (changed && fits && on > 0 && allInKey (T.melody, k), "every chord swapped (" + S.progName + " -> " + T.progName + "): the " + juce::String (on) + " on-beat / long melody notes move onto the new chords' tones, all in the key");
    }
    // 6b. plain words in the window: no "BRAIN" (sessions that saved POP BRAIN / ACID BRAIN still load them)
    {
        bool plain = true;
        for (int i = 0; i < styleCount(); ++i) for (auto* x : { styleName ((Style) i), styleTip ((Style) i), styleGroup ((Style) i) }) plain = plain && ! juce::String (x).containsIgnoreCase ("brain") && ! juce::String (juce::CharPointer_UTF8 (x)).contains (juce::String (juce::CharPointer_UTF8 ("\xf0\x9f\xa7\xa0")));
        check (plain && juce::String (styleName (styleFromName ("POP BRAIN"))) == "LEARNED POP" && juce::String (styleName (styleFromName ("ACID BRAIN"))) == "LEARNED ACID", "no style, tip or group says BRAIN (LEARNED POP / LEARNED ACID; old sessions' names still load)");
    }
    // 7. saved and loaded: the edit round-trips; no edit = nothing written (old sessions are unchanged)
    {
        NoteEdit e; NoteEvent n; n.note = 69; n.beat = 1.25; n.length = 0.5; n.link = 1; n.linkAmt = 0.3f; n.mine = true; e.locked.push_back (n); e.removed.push_back ({ 2.0, 64 }); e.chordAlt = { -1, 17, -1 }; e.chords = 1; e.master = 0.8; e.arp = 4; e.arpRate = 0; e.arpOct = 3; e.grid = 3;
        const auto back = NoteEdit::fromVar (juce::JSON::parse (juce::JSON::toString (e.toVar())));
        Recipe r; r.engine = 6; const auto s0 = juce::JSON::toString (r.toVar());
        Recipe r2 = r; r2.edit = e; const auto r2b = Recipe::fromVar (juce::JSON::parse (juce::JSON::toString (r2.toVar())));
        check (back == e && ! s0.contains ("\"edit\"") && r2b.edit == e, "the edit is saved with the take and loads back the same; a take without one saves exactly as before (no \"edit\")");
        const auto old = Recipe::fromVar (juce::JSON::parse (s0));
        check (old.edit == NoteEdit() && ! old.edit.music(), "a recipe saved before build 8 loads with no edit (it renders as it did)");
    }
}

// ---------------------------------------------------------------- through the plug-in: the roll, undo, VARY, MIDI, the session, speed
static int noteAt (const std::vector<NoteEvent>& ev, double beat) { for (size_t i = 0; i < ev.size(); ++i) if (std::abs (ev[i].beat - beat) < 0.6 / 1024.0) return (int) i; return -1; }   // (your notes sit on a 1/1024 beat grid)

static void rollEight (const juce::File& dir)
{
    section ("build 8 PLAY IT (the plug-in): drag notes in key, undo / redo, VARY keeps them, DRAG MIDI, the session, re-renders");
    ghosty::License::testForce().store (1);
    SliceProcessor p; p.setPlayConfigDetails (0, 2, 48000, 512); p.prepareToPlay (48000, 512);
    std::unique_ptr<SliceEditor> ed (static_cast<SliceEditor*> (p.createEditor())); ed->setVisible (true); pump (30);
    ed->setSize (1477, 1065); ed->setLook (1); pump (20);
    { auto file = wavOf (sung(), "sung-play8.wav"); juce::String err; p.loadAudio (file, err); } pump (30); p.waitIdle (60000);
    p.keyTonic = 9; p.keyMinor = true; p.musical = 0.85; p.type = Type::Instrument; p.style = styleFromName ("HOOK"); p.lengthBars = 4;
    ed->refresh(); p.generate(); p.waitIdle (120000); p.playPreview (false); pump (20);
    auto t0 = p.currentTake();
    if (t0 == nullptr) { check (false, "an INSTRUMENT take"); return; }
    const double fullMs = p.lastRenderMs();
    const Key k { t0->keyTonic, t0->keyMinor };
    ed->hoodBtn.triggerClick(); pump (20);
    auto& roll = ed->hood.roll; roll.sync(); pump (5);
    check (ed->hoodShown() && roll.canEdit() && roll.notes.size() == t0->notes.size() && roll.notes.size() >= 4, "UNDER THE HOOD shows the take's " + juce::String ((int) roll.notes.size()) + " notes on an editable roll");
    savePng (shot (*ed), dir.getChildFile ("ui8-roll-before.png"));
    const auto before = t0->notes; const auto d0 = t0->digest;
    // 1. drag a note: up (by the mouse, onto an out-of-key row too) and to the right, on the grid
    {
        const int i = (int) before.size() / 2; const auto& n = before[(size_t) i];
        const auto from = juce::Point<float> (roll.geo.x (n.beat) + 3.0f, roll.geo.y (n.note) + roll.geo.rowH() * 0.5f);
        int want = n.note + 3; if (inScale (want, k)) ++want;   // (aim at a note outside the key)
        const auto to = juce::Point<float> (roll.geo.x (n.beat + 0.5) + 3.0f, roll.geo.y (want) + roll.geo.rowH() * 0.5f);
        auto mk = [&] (juce::Point<float> pt, bool down) { return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pt, juce::ModifierKeys (down ? juce::ModifierKeys::leftButtonModifier : 0), 1.0f, 0, 0, 0, 0, &roll, &roll, juce::Time::getCurrentTime(), from, juce::Time::getCurrentTime(), 1, false); };
        roll.mouseDown (mk (from, true)); roll.mouseDrag (mk ({ (from.x + to.x) * 0.5f, (from.y + to.y) * 0.5f }, true)); roll.mouseDrag (mk (to, true)); roll.mouseUp (mk (to, false));
        p.waitIdle (60000); pump (20); roll.sync(); pump (5);
        auto t1 = p.currentTake();
        const double nb = edit::snapTime (n.beat + 0.5, 2);
        const int j = t1 != nullptr ? noteAt (t1->notes, nb) : -1;
        const bool moved = j >= 0 && t1->notes[(size_t) j].note > n.note && t1->notes[(size_t) j].mine;
        check (moved && inScale (t1->notes[(size_t) j].note, k) && allInKey (t1->notes, k) && oneLine (t1->notes), "a dragged note lands on the grid (beat " + f2 (n.beat, 2) + " -> " + f2 (nb, 2) + ") and on a note of the key ("
               + juce::MidiMessage::getMidiNoteName (n.note, true, true, 5) + " -> " + (j >= 0 ? juce::MidiMessage::getMidiNoteName (t1->notes[(size_t) j].note, true, true, 5) : juce::String ("?")) + ", aimed at " + juce::MidiMessage::getMidiNoteName (want, true, true, 5) + " which is outside " + t1->keyName() + ")");
        check (t1 != nullptr && t1->digest != d0, "the take re-renders with it (the audio changed)");
        if (juce::SystemStats::getEnvironmentVariable ("GS_DEBUG", "").isNotEmpty()) std::printf ("    (toast: %s; base %d want %d lo %d hi %d)\n", p.lastMessage.toRawUTF8(), n.note, want, roll.geo.lo, roll.geo.hi);
        const double editMs = p.lastRenderMs();
        check (editMs < 0.5 * fullMs || editMs < 1500.0, "the re-render is fast: " + f2 (editMs, 0) + " ms (a whole new take: " + f2 (fullMs, 0) + " ms); the zones are reused");
        // the fast re-render = a full render of the same recipe, bit for bit
        auto full = generate (*p.getPrepared(), p.takes()[(size_t) p.currentIndex()].recipe);
        check (full != nullptr && full->digest == t1->digest, "the fast re-render is bit for bit a full render of the same edit");
        savePng (shot (*ed), dir.getChildFile ("ui8-roll-after.png"));
    }
    // 2. resize, add (double-click), delete, keys, multi-select
    {
        auto t = p.currentTake(); roll.sync();
        const int n0 = (int) roll.notes.size();
        const double freeBeat = [&] { for (double b = 0; b < 15.5; b += 0.25) { bool used = false; for (auto& x : roll.notes) if (x.beat < b + 0.5 - 1e-6 && x.beat + x.length > b + 1e-6) used = true; if (! used) return b; } return 15.5; }();
        check (roll.addAt (freeBeat + 0.01, 62), "double-click on empty space adds a note");
        p.waitIdle (60000); pump (10); roll.sync();
        t = p.currentTake(); const int ai = noteAt (t->notes, edit::snapTime (freeBeat + 0.01 - 0.125, 2));
        check ((int) t->notes.size() == n0 + 1 && ai >= 0 && inScale (t->notes[(size_t) ai].note, k), "the new note sits on the grid, in the key (" + (ai >= 0 ? juce::MidiMessage::getMidiNoteName (t->notes[(size_t) ai].note, true, true, 5) : juce::String ("?")) + " asked for " + juce::MidiMessage::getMidiNoteName (62, true, true, 5) + ", " + juce::String ((int) t->notes.size()) + " notes now)");
        roll.sel.assign (roll.notes.size(), false); for (size_t i = 0; i < 3 && i < roll.sel.size(); ++i) roll.sel[i] = true;
        const auto three = roll.notes;
        roll.keyPressed (juce::KeyPress (juce::KeyPress::upKey)); p.waitIdle (60000); pump (10); roll.sync(); t = p.currentTake();
        bool up = true; for (size_t i = 0; i < 3; ++i) { const int j = noteAt (t->notes, three[i].beat); up = up && j >= 0 && t->notes[(size_t) j].note == gslice::ui::NoteRoll::stepInKey (three[i].note, 1, k); }
        check (up && allInKey (t->notes, k), "select 3, press UP: each moves to the next note of the key");
        roll.sel.assign (roll.notes.size(), false); roll.sel[0] = true; const int before2 = (int) roll.notes.size();
        roll.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey)); p.waitIdle (60000); pump (10); roll.sync(); t = p.currentTake();
        check ((int) t->notes.size() == before2 - 1, "DELETE deletes the selected note");
    }
    // 3. undo / redo walk the same history; undo all the way = the take as generated, bit for bit
    {
        const auto dEdited = p.currentTake()->digest;
        int steps = 0; while (p.canUndo() && steps < 20) { p.undoEdit(); p.waitIdle (60000); ++steps; }
        pump (10);
        check (p.currentTake() != nullptr && p.currentTake()->digest == d0 && sameNotes (p.currentTake()->notes, before), "UNDO x " + juce::String (steps) + ": the take is back as generated, bit for bit");
        int r = 0; while (p.canRedo() && r < 20) { p.redoEdit(); p.waitIdle (60000); ++r; }
        pump (10);
        check (r == steps && p.currentTake()->digest == dEdited, "REDO x " + juce::String (r) + ": every edit is back (the same audio)");
    }
    // 4. LINES: GLIDE / FATTEN into the next note (rendered, and in the MIDI)
    {
        roll.sync(); roll.sel.assign (roll.notes.size(), false);
        int gi = -1; for (size_t i = 0; i + 1 < roll.notes.size(); ++i) if (roll.notes[i + 1].note != roll.notes[i].note) { gi = (int) i; break; }
        if (gi >= 0) roll.sel[(size_t) gi] = true;
        const auto dB = p.currentTake()->digest;
        const bool ok = gi >= 0 && roll.setLink (1, 0.3f); p.waitIdle (60000); pump (10); roll.sync();
        auto t = p.currentTake(); const int j = gi >= 0 ? noteAt (t->notes, roll.notes[(size_t) gi].beat) : -1;
        bool overlap = false;
        if (auto* tr = t->midi.getTrack (0))
        {
            juce::MidiMessageSequence sq (*tr); sq.updateMatchedPairs();
            for (int a = 0; a < sq.getNumEvents(); ++a) for (int b = 0; b < sq.getNumEvents(); ++b)
            {
                auto* x = sq.getEventPointer (a); auto* y = sq.getEventPointer (b);
                if (x->message.isNoteOn() && y->message.isNoteOn() && x->noteOffObject != nullptr && y->message.getTimeStamp() > x->message.getTimeStamp() && y->message.getTimeStamp() < x->noteOffObject->message.getTimeStamp() - 1) overlap = true;
            }
        }
        check (ok && j >= 0 && t->notes[(size_t) j].link == 1 && t->digest != dB && overlap, "GLIDE on a line: the voice slides into the next note (new audio) and the MIDI notes overlap (a DAW's glide)");
        savePng (shot (*ed), dir.getChildFile ("ui8-roll-glide.png"));
    }
    // 5. a chord swap from the chord lane (ArpEZator's way), SIMPLE / RICH, MASTER, the ARP
    {
        auto t = p.currentTake();
        const auto& written = t->writtenChords;
        const auto opts = edit::chordOptions (k, written[0], p.takeEdit().chords);
        const bool sw = ! opts.empty() && roll.swapChord (0, opts.back().code); p.waitIdle (60000); pump (10);
        t = p.currentTake();
        check (sw && t->chords[0].name == opts.back().chord.name && allInKey (t->notes, k), "click the first chord, pick " + (opts.empty() ? juce::String ("?") : opts.back().label) + ": the take now plays it, the melody still in the key");
        ed->hood.setTab (1); pump (10);
        check (ed->hood.playPanel.isVisible() && ! ed->hood.deep[0].isVisible(), "the PLAY IT tab shows ArpEZator's controls (the deeper knobs are on SHAPE IT)");
        ed->hood.playPanel.chords.onChange (2); p.waitIdle (60000); pump (10); t = p.currentTake();
        int rich = 0; for (auto& c : t->chords) rich += c.pcs.size() >= 4 ? 1 : 0;
        check (p.takeEdit().chords == 1 && rich >= 1 && allInKey (t->notes, k), "CHORDS: RICH - " + t->progName);
        if (juce::SystemStats::getEnvironmentVariable ("GS_DEBUG", "").isNotEmpty()) { juce::String w; for (auto& c : t->writtenChords) w << c.name << " "; juce::String h; for (auto& c : t->chords) h << c.name << " "; std::printf ("    (RICH take: written %s / chords %s)\n", w.toRawUTF8(), h.toRawUTF8()); }
        ed->hood.playPanel.master.setValue (1.0, juce::sendNotificationSync); p.waitIdle (60000); pump (10);
        const auto nWild = p.currentTake()->notes.size();
        ed->hood.playPanel.master.setValue (0.0, juce::sendNotificationSync); p.waitIdle (60000); pump (10);
        const auto nSimple = p.currentTake()->notes.size();
        check (nWild > nSimple && allInKey (p.currentTake()->notes, k), "MASTER: WILD plays " + juce::String ((int) nWild) + " notes, SIMPLE " + juce::String ((int) nSimple) + " (your notes stay)");
        ed->hood.playPanel.master.setValue (0.5, juce::sendNotificationSync); p.waitIdle (60000); pump (10);
        p.setPlay ([] (NoteEdit& e) { e.arp = 4; e.arpOct = 2; e.arpRate = 1; }, "ARP"); p.waitIdle (60000); pump (10); ed->hood.refresh(); pump (5);
        t = p.currentTake();
        check (t->form.contains ("ARP") && allInKey (t->notes, k) && t->notes.size() >= 16, "ARP UP-DOWN (1/16, 2 octaves): " + juce::String ((int) t->notes.size()) + " notes over the chords, in the key");
        if (juce::SystemStats::getEnvironmentVariable ("GS_DEBUG", "").isNotEmpty())
        {
            juce::String w; for (auto& c : t->writtenChords) w << c.name << " "; juce::String h; for (auto& c : t->chords) h << c.name << " ";
            std::printf ("    (ARP take: written %s / chords %s / edit %s)\n", w.toRawUTF8(), h.toRawUTF8(), juce::JSON::toString (p.takeEdit().toVar(), true).toRawUTF8());
        }
        savePng (shot (*ed), dir.getChildFile ("ui8-playit-arp.png"));
        p.setPlay ([] (NoteEdit& e) { e.arp = 0; }, "ARP off"); p.waitIdle (60000); pump (10); ed->hood.refresh(); pump (5);
        savePng (shot (*ed), dir.getChildFile ("ui8-playit.png"));
    }
    // 6. VARY keeps your notes (locked)
    {
        auto e = p.takeEdit(); const auto mine = e.locked; auto tA = p.currentTake();
        p.vary(); p.waitIdle (120000); pump (10);
        auto tV = p.currentTake(); int kept = 0;
        for (auto& m : mine) { const int j = noteAt (tV->notes, m.beat); if (j >= 0 && tV->notes[(size_t) j].note == edit::snapPitch (m.note, Key { tV->keyTonic, tV->keyMinor })) ++kept; }
        int otherDiff = 0; for (auto& n : tV->notes) if (! n.mine && noteAt (tA->notes, n.beat) < 0) ++otherDiff;
        check (! mine.empty() && kept == (int) mine.size() && otherDiff > 0 && tV != tA, "VARY: all " + juce::String (kept) + " of your notes stay where you put them, the rest of the line is new (" + juce::String (otherDiff) + " new notes)");
    }
    // 7. DRAG MIDI exports what the roll shows
    {
        auto t = p.currentTake(); juce::String err;
        const auto f = p.writeTakeMidi (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("gs8-midi"), err);
        juce::MidiFile mf; bool ok = false; int n = 0;
        if (f.existsAsFile()) { juce::FileInputStream in (f); if (mf.readFrom (in)) { ok = true; mf.convertTimestampTicksToSeconds(); for (auto* ev : *mf.getTrack (0)) if (ev->message.isNoteOn()) ++n; } }   // (build 10: track 1 is the CHORDS)
        check (ok && n == (int) t->notes.size(), "DRAG MIDI: the .mid has the roll's " + juce::String ((int) t->notes.size()) + " notes (" + juce::String (n) + ")");
    }
    // 8. the session: saved, reopened, the same take (bit for bit)
    {
        const auto dNow = p.currentTake()->digest; const auto eNow = p.takeEdit();
        juce::MemoryBlock mb; p.getStateInformation (mb);
        SliceProcessor q; q.prepareToPlay (48000, 512); q.setStateInformation (mb.getData(), (int) mb.getSize());
        waitReopened (q); q.waitIdle (180000); pump (20);
        if (juce::SystemStats::getEnvironmentVariable ("GS_DEBUG", "").isNotEmpty())
            std::printf ("    (reopened: take %d edit same %d digest same %d; %s\n     vs %s)\n", q.currentTake() != nullptr, (int) (q.takeEdit() == eNow), q.currentTake() != nullptr && q.currentTake()->digest == dNow,
                         juce::JSON::toString (q.takeEdit().toVar(), true).toRawUTF8(), juce::JSON::toString (eNow.toVar(), true).toRawUTF8());
        check (q.currentTake() != nullptr && q.takeEdit() == eNow && q.currentTake()->digest == dNow, "the session saves your edit: reopened, the take is the same, bit for bit");
    }
    // 9. no allocation on the audio thread while an edited take (GLIDE, RICH, your notes) plays and the keys play it
    {
        juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer mb; p.playPreview (true);
        gAudioAllocs.store (0);
        for (int i = 0; i < 300; ++i)
        {
            mb.clear(); if (i % 25 == 3) mb.addEvent (juce::MidiMessage::noteOn (1, 57 + (i / 25) % 7, 0.9f), 10); if (i % 25 == 17) mb.addEvent (juce::MidiMessage::noteOff (1, 57 + (i / 25) % 7), 0);
            tlAudio = true; p.processBlock (buf, mb); tlAudio = false;
        }
        check (gAudioAllocs.load() == 0, "300 blocks of an edited take (preview + keys): " + juce::String (gAudioAllocs.load()) + " allocations on the audio thread");
        // and while an edit re-renders (the old take keeps playing)
        roll.sync(); roll.sel.assign (roll.notes.size(), false); if (! roll.sel.empty()) roll.sel.back() = true;
        roll.nudge (0, -1);
        gAudioAllocs.store (0);
        for (int i = 0; i < 200; ++i) { mb.clear(); tlAudio = true; p.processBlock (buf, mb); tlAudio = false; if (i % 20 == 0) pump (1); }
        p.waitIdle (60000); for (int i = 0; i < 50; ++i) { mb.clear(); tlAudio = true; p.processBlock (buf, mb); tlAudio = false; }
        check (gAudioAllocs.load() == 0, "playing on while an edit re-renders and the new take is swapped in: " + juce::String (gAudioAllocs.load()) + " allocations");
        p.playPreview (false);
    }
    ed->hood.setTab (0);
}

// ---------------------------------------------------------------- PAD / CHOPS: the roll edits them too (chords, phrase)
static void typesEight()
{
    section ("build 8 PLAY IT on PAD and CHOPS: chord swaps / SIMPLE / RICH on a pad, notes on a chop phrase");
    auto P = prepOf (sung());
    {   // PAD: RICH + a swap; the chord notes stay in the key; the fast path equals a full render
        Recipe r; r.type = Type::Pad; r.seed = 31; r.detail = 4; r.engine = 6; r.clear = true; r.keyTonic = 9; r.keyMinor = true; r.style = Style::Dreamy; r.bars = 8;
        auto a = generate (*P, r);
        Recipe r2 = r; r2.edit.chords = 1; r2.edit.chordAlt = { -1, a != nullptr && ! a->writtenChords.empty() ? ((a->writtenChords[1 % a->writtenChords.size()].degree + 2) % 7) * 8 + 2 : -1 };
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        auto b = a != nullptr ? generate (*P, r2, {}, a) : nullptr;
        const double fast = juce::Time::getMillisecondCounterHiRes() - t0;
        auto c = generate (*P, r2);
        bool inKey = b != nullptr; for (auto& ch : b->chords) for (int n : ch.notes) inKey = inKey && inScale (n, Key { 9, true });
        check (a != nullptr && b != nullptr && c != nullptr && inKey && b->digest != a->digest && b->digest == c->digest, "PAD: RICH + a swapped chord (" + (b != nullptr ? b->progName : juce::String()) + "), in the key; the fast re-render (" + f2 (fast, 0) + " ms) = the full one, bit for bit");
    }
    {   // CHOPS: move a chop note, it stays in the key (its pitch class: the chop keeps its own octave)
        Recipe r; r.type = Type::Chops; r.seed = 12; r.detail = 3; r.engine = 6; r.clear = true; r.keyTonic = 0; r.keyMinor = false; r.style = Style::Hook; r.bars = 4;
        auto a = generate (*P, r);
        Recipe r2 = r; NoteEvent n = a->notes[1]; r2.edit.removed.push_back ({ n.beat, n.note }); n.beat += 0.25; n.note += 1; r2.edit.locked.push_back (n);
        auto b = generate (*P, r2);
        const int j = b != nullptr ? noteAt (b->notes, n.beat) : -1;
        check (b != nullptr && j >= 0 && allInKey (b->notes, Key { 0, false }) && b->notes[(size_t) j].chop == n.chop && b->digest != a->digest, "CHOPS: a moved chop note plays on its new beat, in the key, the same chop");
    }
}

// ---------------------------------------------------------------- the demo: before / after an edit, the ARP
static void abEight (const juce::File& dir)
{
    section ((juce::String ("build 8 demo: ") + dir.getFullPathName()).toRawUTF8());
    dir.createDirectory();
    auto src = pdVoice() != nullptr ? pdVoice() : sung(); auto P = prepOf (src);
    Recipe r; r.type = Type::Instrument; r.seed = 2027; r.detail = 9; r.musical = 0.85; r.clear = true; r.engine = 6; r.knob = 0.6; r.style = Style::Hook; r.bars = 4;
    auto a = generate (*P, r);
    if (a == nullptr) { check (false, "rendered"); return; }
    const Key k { a->keyTonic, a->keyMinor };
    // the edit: three notes moved up the key, a note added at the end of bar 2, a GLIDE into bar 3, the second chord swapped, RICH chords
    Recipe e = r; auto& E = e.edit; E.chords = 1;
    for (size_t i = 2; i < a->notes.size() && i < 9; i += 3) { auto n = a->notes[i]; E.removed.push_back ({ n.beat, n.note }); n.note = gslice::ui::NoteRoll::stepInKey (n.note, 2, k); n.mine = true; E.locked.push_back (n); }
    { NoteEvent n; n.beat = 7.5; n.length = 0.5; n.note = gslice::ui::NoteRoll::stepInKey (a->notes.back().note, 3, k); n.link = 1; n.linkAmt = 0.35f; n.mine = true; E.locked.push_back (n); }
    if (a->writtenChords.size() > 1) { E.chordAlt.assign (a->writtenChords.size(), -1); E.chordAlt[1] = ((a->writtenChords[1].degree + 5) % 7) * 8 + 2; }
    auto b = generate (*P, e, {}, a);
    Recipe g = r; g.edit.arp = 4; g.edit.arpRate = 1; g.edit.arpOct = 2; g.edit.chords = 1;
    auto c = generate (*P, g, {}, a);
    int n = 26;
    for (auto [t, name, what] : { std::tuple { a, "edit-BEFORE", "INSTRUMENT VOICE 60 %, HOOK, as generated" }, std::tuple { b, "edit-AFTER", "the same take after editing the roll: 3 notes moved up the key, a note added with a GLIDE, chord 2 swapped, RICH chords" },
                                  std::tuple { c, "arp-UPDOWN-RICH", "the same take, ARP UP-DOWN 1/16 over 2 octaves, RICH chords (ArpEZator's arp engine)" } })
    {
        if (t == nullptr) { check (false, juce::String (name) + ": rendered"); continue; }
        auto w = renderForExport (*t, 0.0f, 0.3f, 0.55f, 0.35f); juce::String err;
        const auto f = juce::String (n++) + "-" + name;
        const bool ok = exporter::writeWav (w, 48000, dir.getChildFile (f + ".wav"), 24, err);
        check (ok && dsp::finiteAndClean (w) && dsp::clicksIn (w) == 0, f + ".wav: " + what + " (" + f2 (w.getNumSamples() / 48000.0, 1) + " s, no clicks; " + t->progName + ")");
    }
}
} // namespace eight
