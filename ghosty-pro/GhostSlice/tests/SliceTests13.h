// GHOST SLICE build 13 tests: the land drives the whole sound (SliceTerrainMap.h): what each kind of shape does, nothing at all
// from a flat land (old sessions identical), every change heard at once, exports carry it. Included from SliceTests.cpp.
#pragma once
#include "../SliceTerrainMap.h"
#include "TerrainFeatures.h"

namespace thirteen
{
using ghost::terrain::Terrain;

static void coreThirteen()
{
    section ("build 13: the land drives the whole sound (shapes -> DREAM, STUTTER, SHIMMER, GRIT, SPACE, WIDTH, TONE, CHORUS, WOBBLE)");
    const auto flat = land::mapLand (ghost::terrain::flatTerrain());
    check (! flat.any && flat.tone == 0 && flat.space == 0 && flat.width == 0 && flat.warmth == 0 && flat.ensemble == 0 && flat.dream == 0 && flat.live.isZero() && flat.meters.empty(),
           "a flat land does nothing at all (every older session sounds the same)");
    auto shape = [] (const char* n) { return land::mapLand (ghost::terrain::shapePreset (n)); };
    const auto spires = shape ("SPIRE FOREST"), plateau = shape ("PLATEAU"), bad = shape ("BADLANDS"), arch = shape ("ARCHIPELAGO"), twin = shape ("TWIN PEAKS");
    check (spires.live.shatter > 0.35f && spires.live.shimmer > 0.3f && spires.dream < 0.2f, "spires stutter and shimmer (" + f2 (spires.live.shatter) + ", " + f2 (spires.live.shimmer) + ")");
    check (plateau.dream > 0.4f && plateau.live.freeze > 0.5f && plateau.live.shatter < 0.15f, "a plateau dreams and holds (" + f2 (plateau.dream) + ", freeze " + f2 (plateau.live.freeze) + ")");
    check (bad.live.drive > 0.3f && bad.live.reverse > 0.3f, "badlands are gritty, with backwards chops (" + f2 (bad.live.drive) + ", " + f2 (bad.live.reverse) + ")");
    check (arch.width > twin.width && arch.ensemble > twin.ensemble, "an archipelago is wider and has more voices than twin peaks");
    juce::StringArray tops;
    for (int q = 0; q < ghost::terrain::shapePresetCount(); ++q) { const auto m = land::mapLand (ghost::terrain::shapePreset (ghost::terrain::shapePresetName (q))); tops.addIfNotAlreadyThere (m.meters.empty() ? juce::String() : juce::String (m.meters[0].name)); }
    check (tops.size() >= 4, "the shape presets each push something different first (" + tops.joinIntoString (", ") + ")");
    check (land::mapLand (ghost::terrain::shapePreset ("RIDGE")) == land::mapLand (ghost::terrain::shapePreset ("RIDGE")), "the mapping is repeatable");
    const auto still = land::mapLand (ghost::terrain::shapePreset ("TWIN PEAKS"), 0.0f), moving = land::mapLand (ghost::terrain::shapePreset ("TWIN PEAKS"), 0.9f);
    check (moving.live.wobble > still.live.wobble + 0.5f, "a quick drag wobbles");
    for (int q = 0; q < ghost::terrain::shapePresetCount(); ++q)
    {
        const juce::String n (ghost::terrain::shapePresetName (q));
        const auto m = land::mapLand (ghost::terrain::shapePreset (n.toRawUTF8()));
        const bool sane = m.dream >= 0 && m.dream <= 1 && std::abs (m.tone) <= 1 && m.live.shatter <= 1 && m.live.drive <= 1 && m.live.wobble <= 1;
        if (! sane) check (false, "the mapping stays in range for " + n);
    }
    // exports: a flat land is bit for bit the old export; a land changes it
    {
        gen::Take t; t.rate = 48000.0; t.preview.setSize (2, 96000); 
        for (int i = 0; i < 96000; ++i) { const float v = 0.3f * std::sin (0.031f * (float) i) * (i % 12000 < 6000 ? 1.0f : 0.4f); t.preview.setSample (0, i, v); t.preview.setSample (1, i, v * 0.9f); }
        const auto a = gen::renderForExport (t, 0.0f, 0.4f, 0.55f, 0.35f);
        const auto fm = land::mapLand (ghost::terrain::flatTerrain());
        gen::FxMod none { fm.tone, fm.space, fm.width, fm.warmth, fm.ensemble, fm.dream, fm.any ? &fm.live : nullptr };
        const auto b = gen::renderForExport (t, 0.0f, 0.4f, 0.55f, 0.35f, none);
        check (twelve::sameBits (a, b), "an export with a flat land is bit for bit the old one");
        gen::FxMod sp { spires.tone, spires.space, spires.width, spires.warmth, spires.ensemble, spires.dream, &spires.live };
        const auto c = gen::renderForExport (t, 0.0f, 0.4f, 0.55f, 0.35f, sp);
        double d = 0; const int n = juce::jmin (a.getNumSamples(), c.getNumSamples()); for (int i = 0; i < n; ++i) d += std::abs (a.getSample (0, i) - c.getSample (0, i));
        check (d / n > 0.003, "an export with spires carries them (mean difference " + f2 (d / n, 4) + ")");
    }
    // the processor: a land on the page moves the FX at once, the live chain's amounts reach the audio thread at the next block
    {
        SliceProcessor p; p.prepareToPlay (48000, 512);
        p.terrain.setWithoutUndo (ghost::terrain::shapePreset ("PLATEAU"));
        check (p.getFx().modDream.load() > 0.4f && p.landMod().any, "a plateau on the page raises DREAM at once (" + f2 (p.getFx().modDream.load()) + ")");
        juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer midi;
        double worstMs = 0;
        for (int k = 0; k < 8; ++k)
        {
            p.terrain.setWithoutUndo (ghost::terrain::shapePreset (k % 2 ? "SPIRE FOREST" : "BADLANDS", (uint32_t) k + 1));
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            buf.clear(); p.processBlock (buf, midi);   // (the block that follows the change)
            worstMs = juce::jmax (worstMs, juce::Time::getMillisecondCounterHiRes() - t0);
        }
        check (p.getFx().modDream.load() < 0.2f, "and lets go of it when the land changes");
        p.terrain.setWithoutUndo (ghost::terrain::flatTerrain());
        check (p.getFx().modDream.load() == 0.0f && p.getFx().modTone.load() == 0.0f && p.getFx().modSpace.load() == 0.0f && ! p.landMod().any, "back to flat: every offset is 0 again");
        std::printf ("  (a land change reaches the audio thread with the next block: <= %.1f ms at 512 / 48 kHz; that block took %.2f ms)\n", 512.0 / 48.0, worstMs);
    }

}

// demos: the same take through six shape presets, and a wild shape built step by step (each stage 3 s, cross-faded)
static void abThirteen (const juce::File& dir)
{
    section ((juce::String ("build 13 demos: ") + dir.getFullPathName()).toRawUTF8());
    dir.createDirectory();
    auto src = six::pdVoice() != nullptr ? six::pdVoice() : sung(); auto P = prepOf (src);
    Recipe r; r.type = Type::Instrument; r.seed = 905; r.detail = 5; r.musical = 1.0; r.clear = true; r.knob = 0.0; r.style = Style::Hook; r.bars = 4; r.keyTonic = 9; r.keyMinor = true;
    auto dry = generate (*P, r);
    if (dry == nullptr) { check (false, "rendered"); return; }
    auto render = [&] (const Terrain& t)
    {
        auto shaped = land::flat (t) ? dry : land::shapeTake (*dry, t);
        const auto m = land::mapLand (t);
        gen::FxMod mod { m.tone, m.space, m.width, m.warmth, m.ensemble, m.dream, m.any ? &m.live : nullptr };
        return renderForExport (*shaped, 0.0f, 0.3f, 0.55f, 0.35f, mod);
    };
    auto write = [&] (const juce::AudioBuffer<float>& w, const juce::String& name, const juce::String& what)
    {
        juce::String err;
        const bool ok = exporter::writeWav (w, 48000, dir.getChildFile (name + ".wav"), 24, err);
        check (ok && dsp::finiteAndClean (w) && dsp::peakOf (w) < 0.999f, name + ".wav: " + what + " (peak " + f2 (dsp::peakOf (w), 3) + ")");
    };
    write (render (ghost::terrain::flatTerrain()), "50-flat-land", "the take as generated");
    int k = 51;
    for (const char* n : { "SPIRE FOREST", "PLATEAU", "BADLANDS", "ARCHIPELAGO", "VOLCANO", "TWIN PEAKS" })
        write (render (ghost::terrain::shapePreset (n)), juce::String (k++) + "-shape-" + juce::String (n).toLowerCase().replace (" ", "-"), juce::String ("SHAPES: ") + n);
    // building: one mountain, a second merging, spires appear, a plateau, rough ground
    std::vector<Terrain> stages;
    Terrain b = ghost::terrain::defaultTerrain(); stages.push_back (b);
    b.add ({ 0.62f, 0.55f, 0.6f, 0.06f, 0.06f, 0.0f }); stages.push_back (b);
    for (int i = 0; i < 4; ++i) b.add ({ 0.25f + 0.15f * (float) i, 0.3f + 0.1f * (float) (i % 2), 1.1f, 0.012f, 0.012f, 0.0f });
    stages.push_back (b);
    b.add ({ 0.4f, 0.62f, 1.2f, 0.07f, 0.07f, 0.0f }); stages.push_back (b);
    for (auto& p : b) p.rough = 0.6f;
    b.grit = 0.3f; stages.push_back (b);
    juce::AudioBuffer<float> all (2, 0);
    const int seg = 48000 * 3, fade = 2400;
    for (auto& st : stages)
    {
        auto w = render (st);
        const int at = all.getNumSamples(), len = juce::jmin (seg, w.getNumSamples());
        all.setSize (2, at + len, true, true, true);
        for (int c = 0; c < 2; ++c)
        {
            all.copyFrom (c, at, w, c, 0, len);
            all.applyGainRamp (c, at, fade, 0.0f, 1.0f); all.applyGainRamp (c, at + len - fade, fade, 1.0f, 0.0f);
        }
    }
    write (all, "59-building-a-wild-shape", "one mountain -> a second merging -> spires -> a plateau -> rough ground");
}
}
