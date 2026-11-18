// GHOST SLICE: the old-session check. `GhostSliceTests digests [engineMax]` prints one line per saved-session recipe (every older
// engine, every take type, three voices, seeds, VOICE 0 with notes moved near and far, CHORD BED, CLEAR off) with the SHA-256 of its
// preview and of its export render. Run it on the previous build's code and on this one: the lines must be identical (diff).
// Only uses what every build since 6 has, so the same file compiles into an older build's harness.
#pragma once

namespace digests
{
using namespace six;
static Recipe movedNotes (const Prepared& P, Recipe r, int far)
{
    auto a = generate (P, r);
    if (a == nullptr) return r;
    const Key k { a->keyTonic, a->keyMinor };
    static const int nearO[] { 3, -4, 5, -2 }, farO[] { 14, -14, 17, -17, 19, -13 };
    int j = 0;
    for (size_t i = 0; i < a->notes.size(); i += 2)
    {
        auto n = a->notes[i]; r.edit.removed.push_back ({ n.beat, n.note });
        n.note = edit::snapPitch (juce::jlimit (30, 96, n.note + (far ? farO[j % 6] : nearO[j % 4])), k); ++j; n.mine = true; r.edit.locked.push_back (n);
    }
    return r;
}

static juce::String shaOf (const juce::AudioBuffer<float>& b)
{
    juce::MemoryBlock m;
    for (int ch = 0; ch < b.getNumChannels(); ++ch) m.append (b.getReadPointer (ch), sizeof (float) * (size_t) b.getNumSamples());
    return juce::SHA256 (m).toHexString().substring (0, 16);
}

static void dump (int engineMax)
{
    struct V { const char* n; std::shared_ptr<const SourceAudio> s; };
    std::vector<V> voices { { "spoken", spoken() }, { "sung", sung() } };
    if (pdVoice() != nullptr) voices.push_back ({ "pd", pdVoice() });
    int lines = 0;
    for (auto& vc : voices)
    {
        auto P = prepOf (vc.s);
        for (int eng = 5; eng <= engineMax; ++eng)
            for (Type ty : { Type::Instrument, Type::Pad, Type::Chops, Type::Texture })
                for (uint32_t seed : { 42u, 2026u })
                {
                    std::vector<std::pair<juce::String, Recipe>> rs;
                    Recipe r; r.type = ty; r.seed = seed; r.detail = seed + 5; r.musical = 0.85; r.clear = eng >= 6; r.engine = eng;
                    r.keyTonic = (int) (seed % 12); r.keyMinor = seed % 2 == 0; r.bars = 4; r.style = ty == Type::Pad ? Style::Dreamy : Style::Auto;
                    rs.push_back ({ "plain", r });
                    if (eng >= 6 && ty == Type::Instrument)
                    {
                        Recipe v0 = r; v0.knob = 0.0; v0.musical = 1.0; v0.style = Style::Hook; rs.push_back ({ "voice0", v0 });
                        if (eng >= 8) { rs.push_back ({ "voice0-near", movedNotes (*P, v0, 0) }); rs.push_back ({ "voice0-far", movedNotes (*P, v0, 1) }); }
                        Recipe v6 = r; v6.knob = 0.6; rs.push_back ({ "voice60", v6 });
                        if (eng >= 7) { Recipe bd = v6; bd.edit.bed = 0.6; bd.edit.chords = 1; rs.push_back ({ "bed", bd }); }
                    }
                    if (ty == Type::Chops && eng >= 6) { Recipe ns = r; ns.clear = false; rs.push_back ({ "unclear", ns }); }
                    for (auto& [nm, rr] : rs)
                    {
                        auto t = generate (*P, rr);
                        const juce::String id = juce::String (vc.n) + " e" + juce::String (eng) + " " + typeName (ty) + " s" + juce::String ((int) seed) + " " + nm;
                        if (t == nullptr) { std::printf ("DIGEST %s none\n", id.toRawUTF8()); continue; }
                        auto w = renderForExport (*t, 0.0f, 0.3f, 0.55f, 0.35f);
                        std::printf ("DIGEST %s %s %s %d\n", id.toRawUTF8(), t->digest.substring (0, 16).toRawUTF8(), shaOf (w).toRawUTF8(), t->preview.getNumSamples());
                        std::fflush (stdout); ++lines;
                    }
                }
    }
    std::printf ("digests: %d lines\n", lines);
}
} // namespace digests
