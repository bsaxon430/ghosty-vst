// Ghost Slice: digests of a fixed set of takes, to check that old sessions sound the same after a change (build 7 vs 8: 52 identical;
// build 8 vs 9: 60; build 9 vs 10: 70 identical, incl. edited build-8 and engine-7 takes). Build against two trees' engine objects, diff.
#include "Generate.h"
#include "tests/TestVoices.h"
#include <cstdio>
using namespace gslice; using namespace gslice::gen;
int main()
{

    auto sung = source::fromBuffer (slicetest::sungPhrase(), 48000, "sung.wav");
    auto spoken = source::fromBuffer (slicetest::spokenSentence(), 48000, "spoken.wav");
    const char* styles[] { "HOOK", "DREAMY", "WISTFUL", "MELANCHOLIC", "POP BRAIN", "MIDNIGHT ARP", "EMO", "BOOM BAP" };
    for (auto src : { sung, spoken })
    {
        auto P = prepare (src, nullptr);
        for (int ty = 1; ty <= 4; ++ty)
            for (const char* st : styles)
            {
                if (ty == 4 && juce::String (st) != "HOOK") continue;
                Recipe r; r.type = (Type) ty; r.seed = 2027; r.detail = 9; r.musical = 0.85; r.clear = true; r.engine = 6; r.style = styleFromName (st); r.bars = ty == 2 ? 8 : 4;
                if (juce::String (st) == "WISTFUL" && r.style == Style::Auto) continue;   // (build 7 has it; build 6 does not)
                // (as a saved session has it: through the JSON)
                const auto rr = Recipe::fromVar (juce::JSON::parse (juce::JSON::toString (r.toVar())));
                auto t = generate (*P, rr);
                std::printf ("%s %s %-12s %s\n", (src == sung ? "sung" : "spoken"), typeName ((Type) ty), st, t != nullptr ? t->digest.toRawUTF8() : "null");
                std::fflush (stdout);
            }
        // build 8 sessions: engine 6 takes with PLAY IT edits
        for (int ty = 1; ty <= 4; ++ty)
        {
            Recipe r; r.type = (Type) ty; r.seed = 77; r.detail = 4; r.musical = 0.85; r.clear = true; r.engine = 6; r.style = Style::Hook; r.bars = 4; r.keyTonic = 9; r.keyMinor = true;
            auto a = generate (*P, r);
            if (a == nullptr || a->notes.size() < 3) { std::printf ("%s EDIT %s null\n", src == sung ? "sung" : "spoken", typeName ((Type) ty)); continue; }
            NoteEdit e; NoteEvent n = a->notes[1]; e.removed.push_back ({ n.beat, n.note }); n.note += ty == 3 ? 12 : 2; n.mine = true; n.link = ty == 1 ? 1 : 0; e.locked.push_back (n);
            e.chords = ty == 4 ? -1 : 1; if (! a->writtenChords.empty()) { e.chordAlt.assign (a->writtenChords.size(), -1); e.chordAlt[0] = ((a->writtenChords[0].degree + 3) % 7) * 8; }
            if (ty == 1) e.master = 0.8;
            r.edit = e;
            const auto rr = Recipe::fromVar (juce::JSON::parse (juce::JSON::toString (r.toVar())));
            auto t = generate (*P, rr);
            std::printf ("%s EDIT %s %s\n", src == sung ? "sung" : "spoken", typeName ((Type) ty), t != nullptr ? t->digest.toRawUTF8() : "null");
            std::fflush (stdout);
        }
        // build 9 sessions: engine 7 takes (the PSOLA at VOICE 0, the CHORD BED, a chop note an octave up)
        for (int key : { 0, 7 })
        {
            Recipe r; r.type = Type::Instrument; r.seed = 77; r.detail = 5; r.musical = 1.0; r.clear = true; r.knob = 0.0; r.style = Style::Hook; r.bars = 4; r.keyTonic = key; r.engine = 7;
            auto t = generate (*P, Recipe::fromVar (juce::JSON::parse (juce::JSON::toString (r.toVar()))));
            std::printf ("%s E7 VOICE0 key %d %s\n", src == sung ? "sung" : "spoken", key, t != nullptr ? t->digest.toRawUTF8() : "null");
        }
        {
            Recipe r; r.type = Type::Instrument; r.seed = 2027; r.detail = 9; r.clear = true; r.knob = 0.6; r.style = Style::Hook; r.bars = 4; r.keyTonic = 9; r.keyMinor = true; r.engine = 7; r.edit.bed = 0.6; r.edit.chords = 1;
            auto t = generate (*P, Recipe::fromVar (juce::JSON::parse (juce::JSON::toString (r.toVar()))));
            std::printf ("%s E7 BED %s\n", src == sung ? "sung" : "spoken", t != nullptr ? t->digest.toRawUTF8() : "null");
            Recipe c; c.type = Type::Chops; c.seed = 12; c.detail = 3; c.clear = true; c.style = Style::Hook; c.bars = 4; c.engine = 7;
            auto a = generate (*P, c);
            if (a != nullptr && a->notes.size() > 3) { NoteEvent n = a->notes[2]; c.edit.removed.push_back ({ n.beat, n.note }); n.note += 12; n.mine = true; c.edit.locked.push_back (n); }
            auto b = generate (*P, Recipe::fromVar (juce::JSON::parse (juce::JSON::toString (c.toVar()))));
            std::printf ("%s E7 CHOPS %s\n", src == sung ? "sung" : "spoken", b != nullptr ? b->digest.toRawUTF8() : "null");
            Recipe x; x.type = Type::Texture; x.seed = 44; x.detail = 3; x.clear = true; x.engine = 7;
            auto tx = generate (*P, x);
            std::printf ("%s E7 TEXTURE %s\n", src == sung ? "sung" : "spoken", tx != nullptr ? tx->digest.toRawUTF8() : "null");
            std::fflush (stdout);
        }
        // an engine 5 take (build 5's), too
        Recipe r5; r5.type = Type::Instrument; r5.seed = 5; r5.detail = 5; r5.engine = 5;
        auto t5 = generate (*P, Recipe::fromVar (juce::JSON::parse (juce::JSON::toString (r5.toVar()))));
        std::printf ("%s ENGINE5 INSTRUMENT %s\n", (src == sung ? "sung" : "spoken"), t5 != nullptr ? t5->digest.toRawUTF8() : "null");
    }
    return 0;
}
