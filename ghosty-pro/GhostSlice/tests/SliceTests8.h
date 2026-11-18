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
    section ("build 8 UI (the MELODY roll, PLAY IT): the old window's page, removed in build 16 (the new screen has no such page)");
    juce::ignoreUnused (dir);
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
    for (size_t i = 2; i < a->notes.size() && i < 9; i += 3) { auto n = a->notes[i]; E.removed.push_back ({ n.beat, n.note }); n.note = stepInKey (n.note, 2, k); n.mine = true; E.locked.push_back (n); }
    { NoteEvent n; n.beat = 7.5; n.length = 0.5; n.note = stepInKey (a->notes.back().note, 3, k); n.link = 1; n.linkAmt = 0.35f; n.mine = true; E.locked.push_back (n); }
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
