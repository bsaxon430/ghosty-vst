// GHOST SLICE build 7 tests (included by SliceTests.cpp after SliceTests6.h): the PARLOUR PIANO group (Block Step's learned piano).
#pragma once

namespace seven
{
using namespace six;

static void parlourSeven()
{
    section ("build 7: PARLOUR PIANO (the learned parlour piano): in key, deterministic, every LENGTH, plain names");
    std::vector<Style> ps; for (int i = 1; i < styleCount(); ++i) if (juce::String (styleGroup ((Style) i)) == "PARLOUR PIANO") ps.push_back ((Style) i);
    check (ps.size() == 2, "the PARLOUR PIANO group has its two moods (" + juce::String ((int) ps.size()) + ")");
    bool plain = true; static const char* banned[] { "satie", "gnossienne", "gymnop", "mutopia", "brain" };
    for (auto st : ps) for (auto* b : banned) plain = plain && ! juce::String (styleName (st)).containsIgnoreCase (b) && ! juce::String (styleTip (st)).containsIgnoreCase (b);
    check (plain, "names and tips are plain words (no composer, no piece, no \"brain\")");
    int bad = 0, total = 0, empty = 0, det = 0;
    for (auto st : ps)
        for (const Key k : { Key { 9, true }, Key { 0, false }, Key { 6, true }, Key { 3, false } })
            for (int bars : { 1, 2, 4, 8, 16 })
                for (uint32_t seed : { 1u, 77u, 2026u })
                {
                    ++total;
                    const auto S = compose (k, 60, st, bars, seed, 3u, 0, 0.5, 0.5, {}, 96.0);
                    bool ok = ! S.melody.empty() && ! S.chords.empty();
                    for (auto& e : S.melody) ok = ok && inScale (e.note, k) && e.beat >= 0 && e.beat + e.length <= bars * 4.0 + 1e-6 && e.length > 0;
                    for (auto& e : S.accomp) ok = ok && inScale (e.note, k) && e.beat + e.length <= bars * 4.0 + 1e-6;
                    for (auto& c : S.chords) for (int n : c.notes) ok = ok && inScale (n, k);
                    for (size_t i = 1; i < S.melody.size(); ++i) ok = ok && S.melody[i].beat >= S.melody[i - 1].beat + S.melody[i - 1].length - 1e-6;   // one line
                    if (S.melody.empty()) ++empty;
                    if (! ok) ++bad;
                    const auto S2 = compose (k, 60, st, bars, seed, 3u, 0, 0.5, 0.5, {}, 96.0);
                    bool same = S2.melody.size() == S.melody.size();
                    for (size_t i = 0; same && i < S.melody.size(); ++i) same = S.melody[i].note == S2.melody[i].note && S.melody[i].beat == S2.melody[i].beat && S.melody[i].length == S2.melody[i].length;
                    if (same) ++det;
                }
    check (bad == 0, "every note and chord in key, inside the LENGTH, one lead line: " + juce::String (total - bad) + " / " + juce::String (total) + " (2 moods x 4 keys x LENGTH 1/2/4/8/16 x 3 seeds; " + juce::String (empty) + " empty)");
    check (det == total, "deterministic: the same seed gives the same notes (" + juce::String (det) + " / " + juce::String (total) + ")");
    // STILL NIGHT is the slower, sparser one; both differ from seed to seed
    auto density = [] (Style st) { double n = 0; for (uint32_t s = 1; s <= 6; ++s) n += (double) compose ({ 9, true }, 60, st, 8, s, 3u, 0, 0.5, 0.5, {}, 96.0).melody.size(); return n / 6.0 / 32.0; };
    const double dW = density (ps[0]), dS = density (ps[1]);
    check (dS < dW, "STILL NIGHT is sparser than WISTFUL (" + f2 (dS, 2) + " vs " + f2 (dW, 2) + " notes per beat)");
    const auto a = compose ({ 9, true }, 60, ps[0], 4, 1u, 3u, 0, 0.5, 0.5, {}, 96.0), b = compose ({ 9, true }, 60, ps[0], 4, 2u, 3u, 0, 0.5, 0.5, {}, 96.0);
    bool differ = a.melody.size() != b.melody.size(); for (size_t i = 0; ! differ && i < a.melody.size(); ++i) differ = a.melody[i].note != b.melody[i].note || a.melody[i].beat != b.melody[i].beat;
    check (differ, "a new seed gives a new tune");
}
} // namespace seven

namespace seven
{
// just build 7's demo (the full A/B set is "ab2"): 25-group-PARLOUR-PIANO.wav, the same render as ab2's entry 25
static void abSeven (const juce::File& dir)
{
    section ((juce::String ("build 7 demo: ") + dir.getFullPathName()).toRawUTF8());
    dir.createDirectory();
    auto src = pdVoice() != nullptr ? pdVoice() : sung(); auto P = prepOf (src);
    auto t = take6 (P, Type::Instrument, 0.6, styleNamed ("WISTFUL"), 8);
    if (t == nullptr) { check (false, "rendered"); return; }
    auto w = renderForExport (*t, 0.0f, 0.3f, 0.55f, 0.35f); juce::String err;
    const bool ok = exporter::writeWav (w, 48000, dir.getChildFile ("25-group-PARLOUR-PIANO.wav"), 24, err);
    check (ok && dsp::finiteAndClean (w) && dsp::clicksIn (w) == 0, "25-group-PARLOUR-PIANO.wav: WISTFUL, INSTRUMENT VOICE 60 %, 8 bars (" + f2 (w.getNumSamples() / 48000.0, 1) + " s, no clicks; " + t->progName + ")");
}
} // namespace seven
