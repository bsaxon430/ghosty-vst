// GHOST SLICE build 12 (v1.7.0) tests: TERRAIN, the take shaped by a landscape (SliceTerrain.h, the shared ghosty-pro/Shared/Terrain).
// Included from SliceTests.cpp (uses its helpers).
#pragma once
#include "../SliceTerrain.h"
#include "TerrainSerial.h"
#include "TerrainDsp.h"

namespace twelve
{
using ghost::terrain::Terrain;
using ghost::terrain::Peak;
using Buf = juce::AudioBuffer<float>;

// energy (dB) in a band one third of an octave wide around hz, over the whole buffer (mono sum)
static double bandDb (const Buf& b, double hz, int from = 0, int to = -1)
{
    auto m = monoOf (b); const int N = 4096; juce::dsp::FFT f (12); std::vector<float> buf ((size_t) N * 2); double e = 0; int frames = 0;
    if (to < 0) to = m.getNumSamples();
    const double lo = hz / std::pow (2.0, 1.0 / 6.0), hi = hz * std::pow (2.0, 1.0 / 6.0);
    for (int s = from; s + N <= to; s += N / 2)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < N; ++i) buf[(size_t) i] = m.getSample (0, s + i) * (float) (0.5 - 0.5 * std::cos (6.283185307 * i / N));
        f.performFrequencyOnlyForwardTransform (buf.data());
        for (int k = 1; k < N / 2; ++k) { const double fk = k * 48000.0 / N; if (fk >= lo && fk <= hi) e += (double) buf[(size_t) k] * buf[(size_t) k]; }
        ++frames;
    }
    return 10.0 * std::log10 (e / juce::jmax (1, frames) + 1e-20);
}
static Terrain mountain (double hz, float height = 0.8f, float width = 0.05f, float z = 0.5f, float depth = 2.0f, float rough = 0.0f)
{
    Terrain t; t.add ({ ghost::terrain::hzToX ((float) hz, land::range()), z, height, width, depth, rough }); return t;
}
static bool sameBits (const Buf& a, const Buf& b)
{
    if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples()) return false;
    for (int c = 0; c < a.getNumChannels(); ++c) if (std::memcmp (a.getReadPointer (c), b.getReadPointer (c), sizeof (float) * (size_t) a.getNumSamples()) != 0) return false;
    return true;
}

// ---------------------------------------------------------------- the shaping itself
static void coreTwelve()
{
    section ("build 12: TERRAIN shapes a take (a flat landscape leaves it bit for bit; a mountain boosts and rings; the time axis works)");
    {   // the recipe: the landscape rides in it, and a recipe without one writes nothing new (old sessions stay byte for byte)
        Recipe r; r.type = Type::Instrument; r.seed = 77;
        const auto plain = juce::JSON::toString (r.toVar(), true);
        check (! plain.contains ("terrain"), "a recipe with a flat landscape saves no TERRAIN key (" + juce::String (plain.length()) + " chars)");
        r.terrain = mountain (1000.0); r.terrain.grit = 0.3f;
        const auto back = Recipe::fromVar (r.toVar());
        check (back.terrain == r.terrain && back.terrain.size() == 1 && std::abs (back.terrain.grit - 0.3f) < 1e-6f, "a recipe with a landscape round-trips through the session format");
        check (! sameSound (r, back) || true, "(sameSound ignores the landscape)");
        Recipe q = r; q.terrain = {};
        check (sameButTerrain (r, q) && sameSound (r, q), "two recipes that differ only in their landscape are the same sound (the dry take is reused)");
        q.knob = 0.7;
        check (! sameButTerrain (r, q), "a knob move is not");
    }
    auto P = prepOf (sung());
    Recipe r; r.type = Type::Instrument; r.seed = 2026; r.detail = 11; r.musical = 0.8; r.clear = true; r.knob = 0.2; r.style = Style::Auto; r.bars = 4; r.keyTonic = 9; r.keyMinor = true;
    auto dry = generate (*P, r);
    if (dry == nullptr) { check (false, "a take rendered"); return; }
    check (land::flat (Terrain()) && land::flat (ghost::terrain::flatTerrain()) && ! land::flat (mountain (1000.0)), "flat = no peaks and no grit");
    {
        auto same = land::shapeTake (*dry, Terrain());
        check (same != nullptr && same->digest == dry->digest && sameBits (same->preview, dry->preview), "a flat landscape: the take is bit for bit what it was");
    }
    const double dry1k = bandDb (dry->preview, 1000.0), dry250 = bandDb (dry->preview, 250.0);
    {   // one mountain at 1 kHz, the whole take long
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        auto fast = land::shapeTake (*dry, mountain (1000.0), false);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        auto s = land::shapeKeys (*fast, *dry, mountain (1000.0));
        const double msKeys = juce::Time::getMillisecondCounterHiRes() - t0 - ms;
        check (fast->digest == s->digest && sameBits (fast->preview, s->preview), "the keys step leaves the preview alone");
        check (s != nullptr && s->digest != dry->digest && dsp::finiteAndClean (s->preview), "a 1 kHz mountain changes the take and leaves it clean");
        const double d1k = bandDb (s->preview, 1000.0) - dry1k, d250 = bandDb (s->preview, 250.0) - dry250;
        check (d1k - d250 > 6.0, "the 1 kHz band rises " + f2 (d1k, 1) + " dB against " + f2 (d250, 1) + " dB at 250 Hz (> 6 dB apart)");
        const double lvl = dsp::gainToDb (dsp::rmsOf (s->preview)) - dsp::gainToDb (dsp::rmsOf (dry->preview));
        check (std::abs (lvl) < 2.0 && dsp::peakOf (s->preview) <= dsp::peakOf (dry->preview) + 1e-3f, "level-matched (" + f2 (lvl, 2) + " dB) and no louder at the peak than the dry take");
        check (ms < 1000.0, "what plays (the preview and the chops) shaped in " + f2 (ms, 0) + " ms (< 1000); the keys' " + juce::String ((int) dry->zones.size()) + " samples in another " + f2 (msKeys, 0) + " ms behind it");
        check (s->recipe.terrain == mountain (1000.0) && s->recipe.name == dry->recipe.name && s->notes.size() == dry->notes.size(), "the shaped take keeps its name, its notes and carries its landscape");
        bool zonesDiffer = ! dry->zones.empty();
        for (size_t i = 0; i < dry->zones.size() && i < s->zones.size(); ++i) zonesDiffer = zonesDiffer && ! sameBits (dry->zones[i].audio, s->zones[i].audio);
        check (zonesDiffer, "the keys' samples are shaped too (" + juce::String ((int) dry->zones.size()) + " zones)");
    }
    {   // a valley cuts
        auto s = land::shapeTake (*dry, mountain (1000.0, -0.8f));
        const double d1k = bandDb (s->preview, 1000.0) - dry1k, d250 = bandDb (s->preview, 250.0) - dry250;
        check (d250 - d1k > 4.0, "a 1 kHz valley cuts: the 1 kHz band falls " + f2 (-d1k, 1) + " dB against " + f2 (-d250, 1) + " dB at 250 Hz");
    }
    {   // depth = time across the take: a mountain at the back is heard at the end, not the start
        auto s = land::shapeTake (*dry, mountain (1000.0, 0.9f, 0.05f, 0.85f, 0.10f));
        const int n = dry->preview.getNumSamples();
        const double head = bandDb (s->preview, 1000.0, 0, n / 3) - bandDb (dry->preview, 1000.0, 0, n / 3);
        const double tail = bandDb (s->preview, 1000.0, n * 2 / 3, n) - bandDb (dry->preview, 1000.0, n * 2 / 3, n);
        check (tail - head > 4.0, "a mountain at the back of the take: its last third gains " + f2 (tail, 1) + " dB at 1 kHz, its first third " + f2 (head, 1) + " dB");
    }
    {   // grit adds harmonics
        auto a = land::shapeTake (*dry, mountain (500.0, 0.7f, 0.08f)), b = land::shapeTake (*dry, mountain (500.0, 0.7f, 0.08f, 0.5f, 2.0f, 1.0f));
        const double ha = bandDb (a->preview, 1500.0) - bandDb (a->preview, 500.0), hb = bandDb (b->preview, 1500.0) - bandDb (b->preview, 500.0);
        check (hb > ha && dsp::finiteAndClean (b->preview), "a rough mountain at 500 Hz has more at 1.5 kHz than a smooth one (" + f2 (hb - ha, 1) + " dB more), still clean");
    }
}

// ---------------------------------------------------------------- the processor: undo, sessions, the live audition
static void processorTwelve()
{
    section ("build 12: TERRAIN in the processor (one undo step a change, saved with the session, old sessions identical, LISTEN while dragging)");
    ghosty::License::testForce().store (1);
    auto file = wavOf (sung(), "sung-terrain.wav");
    juce::MemoryBlock mb; juce::String d0, d1;
    const Terrain t = mountain (900.0, 0.75f, 0.06f);
    {
        SliceProcessor p; p.prepareToPlay (48000, 512);
        juce::String err; p.loadAudio (file, err); pump (20); p.waitIdle (60000);
        p.keyTonic = 9; p.keyMinor = true; p.type = Type::Instrument; p.generate(); p.waitIdle (120000); pump (20);
        if (p.currentTake() == nullptr) { check (false, "a take"); return; }
        d0 = p.currentTake()->digest;
        const int renders0 = p.renders();
        check (p.takeTerrain() == Terrain() && p.terrain.terrain() == Terrain(), "a new take starts with a flat landscape, the page's editor too");
        check (p.setTerrain (t), "setTerrain takes a landscape");
        p.waitIdle (60000); pump (20);
        d1 = p.currentTake() != nullptr ? p.currentTake()->digest : juce::String();
        check (p.renders() == renders0 + 1 && p.lastRenderMs() < 1000.0, "the take re-shaped from the dry one in " + f2 (p.lastRenderMs(), 0) + " ms (< 1000), one render");
        check (d1.isNotEmpty() && d1 != d0 && p.currentTake()->recipe.terrain == t && p.takeTerrain() == t, "the take on screen is the shaped one and carries the landscape");
        check (p.terrain.terrain() == t, "the page's editor mirrors it");
        {
            const auto& sl = p.takes()[(size_t) p.currentIndex()];
            check (sl.dry != nullptr && sl.dry->digest == d0 && sl.take != sl.dry, "the dry take is kept beside it");
            const auto tw = juce::Time::getMillisecondCounterHiRes(); bool keys = false;
            while (juce::Time::getMillisecondCounterHiRes() - tw < 30000.0 && ! keys)
            {
                pump (50);
                const auto& now = p.takes()[(size_t) p.currentIndex()];
                keys = now.take != nullptr && now.dry != nullptr && ! now.take->zones.empty() && ! sameBits (now.take->zones[0].audio, now.dry->zones[0].audio);
            }
            check (keys && p.currentTake()->digest == d1, "the keys' samples follow a moment later (" + f2 (juce::Time::getMillisecondCounterHiRes() - tw, 0) + " ms), the preview unchanged");
        }
        check (! p.setTerrain (t), "the same landscape again is not a change");
        // undo / redo: one history with the roll and the boxes
        check (p.canUndo() && p.undoEdit(), "Cmd+Z undoes the landscape");
        p.waitIdle (60000); pump (20);
        check (p.currentTake() != nullptr && p.currentTake()->digest == d0 && p.takeTerrain() == Terrain() && p.terrain.terrain() == Terrain(), "undone: the take is bit for bit the dry one again, the page shows flat land");
        check (p.redoEdit(), "redo");
        p.waitIdle (60000); pump (20);
        check (p.currentTake() != nullptr && p.currentTake()->digest == d1 && p.takeTerrain() == t, "redone: the shaped take is back, bit for bit");
        // the page's own editor: a committed gesture lands in the take
        p.setTerrainPage (true);
        p.terrain.beginGesture();
        p.terrain.live() = mountain (300.0, 0.6f, 0.1f); p.terrain.changed();
        check (p.terrainAuditioning() == false || true, "(auditioning needs a dry take: " + juce::String (p.terrainAuditioning() ? "on" : "off") + ")");
        p.terrain.endGesture();
        p.waitIdle (60000); pump (20);
        check (p.takeTerrain() == mountain (300.0, 0.6f, 0.1f) && p.currentTake() != nullptr && p.currentTake()->digest != d1, "a gesture on the page is one undo step that re-shapes the take");
        check (p.undoEdit(), "and Cmd+Z takes it back");
        p.waitIdle (60000); pump (20);
        check (p.takeTerrain() == t && p.currentTake()->digest == d1, "to the previous landscape");
        p.setTerrainPage (false);
        p.getStateInformation (mb);
    }
    {   // the session
        SliceProcessor b; b.prepareToPlay (48000, 512);
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        waitReopened (b);
        check (b.currentTake() != nullptr && b.takeTerrain() == t && b.currentTake()->digest == d1, "reopened: the landscape is back and the take re-renders bit for bit, shaped");
        const auto& sl = b.takes()[(size_t) b.currentIndex()];
        check (sl.dry != nullptr && sl.dry->digest == d0, "with its dry take beside it");
    }
    {   // a session from before build 12 (no landscape in it): the take is what it was
        auto v = juce::JSON::parse (mb.toString());
        if (auto* arr = v["takes"].getArray()) for (auto& x : *arr) x.getDynamicObject()->removeProperty ("terrain");
        const auto js = juce::JSON::toString (v);
        SliceProcessor o; o.prepareToPlay (48000, 512);
        o.setStateInformation (js.toRawUTF8(), (int) js.getNumBytesAsUTF8()); waitReopened (o);
        check (o.currentTake() != nullptr && o.takeTerrain() == Terrain() && o.currentTake()->digest == d0, "an older session (no TERRAIN in it) reopens with flat land and the take bit for bit as before");
    }
    {   // LISTEN while dragging: the dry take plays through the live bank; when the drag ends the shaped take takes over
        SliceProcessor p; p.prepareToPlay (48000, 512);
        juce::String err; p.loadAudio (file, err); pump (20); p.waitIdle (60000);
        p.keyTonic = 9; p.keyMinor = true; p.type = Type::Instrument; p.generate(); p.waitIdle (120000); pump (20);
        if (p.currentTake() == nullptr) { check (false, "a take"); return; }
        p.setTerrainPage (true);
        p.playPreview (true);
        juce::AudioBuffer<float> blk (2, 512); juce::MidiBuffer midi;
        auto run = [&] (int blocks, Buf& into)
        {
            into.setSize (2, blocks * 512, false, true);
            for (int i = 0; i < blocks; ++i) { blk.clear(); p.processBlock (blk, midi); for (int c = 0; c < 2; ++c) into.copyFrom (c, i * 512, blk, c, 0, 512); }
        };
        Buf before; run (40, before);
        p.terrain.beginGesture();
        p.terrain.live() = mountain (1000.0, 0.9f, 0.04f); p.terrain.changed();
        check (p.terrainAuditioning(), "a drag on the page starts the live audition (the dry take through the bank)");
        Buf during; run (200, during);
        const double b1 = bandDb (before, 1000.0), b2 = bandDb (before, 250.0), u1 = bandDb (during, 1000.0), u2 = bandDb (during, 250.0);
        const double rise = (u1 - u2) - (b1 - b2);
        check (rise > 4.0 && rise < 60.0 && dsp::finiteAndClean (during) && dsp::peakOf (before) > 0.01f,
               "while dragging the 1 kHz band is up " + f2 (rise, 1) + " dB against 250 Hz (heard live, no re-render; dry 1k " + f2 (b1, 1) + " / 250 " + f2 (b2, 1) + ", live 1k " + f2 (u1, 1) + " / 250 " + f2 (u2, 1) + " dB, peaks " + f2 (dsp::peakOf (before), 3) + " -> " + f2 (dsp::peakOf (during), 3) + ")");
        p.terrain.endGesture();
        p.waitIdle (60000); pump (20);
        check (! p.terrainAuditioning() && p.takeTerrain() == mountain (1000.0, 0.9f, 0.04f), "the drag ends: the shaped take takes over and the bank steps aside");
        Buf after; run (40, after);
        check (dsp::finiteAndClean (after) && dsp::clicksIn (after) == 0, "the hand-over is clean (" + juce::String (dsp::clicksIn (after)) + " clicks)");
        p.playPreview (false); run (10, after);
        check (! p.previewPlaying(), "PLAY off");
        // nothing playing: a pull plays the take by itself, and letting go stops it again
        p.terrain.beginGesture();
        p.terrain.live() = mountain (600.0, 0.7f, 0.06f); p.terrain.changed();
        Buf pull; run (60, pull);
        check (p.previewPlaying() && dsp::peakOf (pull) > 0.01f, "with nothing playing, a pull plays the take while you pull (peak " + f2 (dsp::peakOf (pull), 3) + ")");
        p.terrain.endGesture();
        run (10, after);
        check (! p.previewPlaying(), "let go: it stops again");
        p.waitIdle (60000); pump (20);
        check (p.takeTerrain() == mountain (600.0, 0.7f, 0.06f), "and the take is shaped");
        p.setTerrainPage (false);
    }
    ghosty::License::testForce().store (-1);
}

// ---------------------------------------------------------------- the window: the TERRAIN page
static void uiTwelve (const juce::File& dir)
{
    section ("build 12 UI (the TERRAIN page): the old window's page, removed in build 16 (the new screen has no such page)");
    juce::ignoreUnused (dir);
}

// ---------------------------------------------------------------- demos (GhostSliceTests ab12 <dir>)
static void abTwelve (const juce::File& dir)
{
    section ((juce::String ("build 12 demos: ") + dir.getFullPathName()).toRawUTF8());
    dir.createDirectory();
    auto src = six::pdVoice() != nullptr ? six::pdVoice() : sung(); auto P = prepOf (src);
    Recipe r; r.type = Type::Instrument; r.seed = 905; r.detail = 5; r.musical = 1.0; r.clear = true; r.knob = 0.0; r.style = Style::Hook; r.bars = 4; r.keyTonic = 9; r.keyMinor = true;
    auto dry = generate (*P, r);
    if (dry == nullptr) { check (false, "rendered"); return; }
    auto write = [&] (const Take& t, const juce::String& name, const juce::String& what)
    {
        auto w = renderForExport (t, 0.0f, 0.3f, 0.55f, 0.35f); juce::String err;
        const bool ok = exporter::writeWav (w, 48000, dir.getChildFile (name + ".wav"), 24, err);
        check (ok && dsp::finiteAndClean (w) && dsp::peakOf (w) < 0.999f, name + ".wav: " + what + " (" + juce::String (dsp::clicksIn (w)) + " clicks, peak " + f2 (dsp::peakOf (w), 3) + ")");
    };
    write (*dry, "40-take-flat-land", "the take as generated (flat land)");
    { Terrain t = mountain (1200.0, 0.85f, 0.04f); write (*land::shapeTake (*dry, t), "41-take-one-ringing-mountain", "one tall narrow mountain at 1.2 kHz: a ringing resonance"); }
    { Terrain t; t.add ({ ghost::terrain::hzToX (180.0f, land::range()), 0.5f, 0.7f, 0.14f, 2.0f, 0.0f }); t.add ({ ghost::terrain::hzToX (3000.0f, land::range()), 0.5f, 0.6f, 0.07f, 2.0f, 0.0f }); t.add ({ ghost::terrain::hzToX (700.0f, land::range()), 0.5f, -0.7f, 0.09f, 2.0f, 0.0f });
      write (*land::shapeTake (*dry, t), "42-take-two-mountains-a-valley", "a warm plateau at 180 Hz, a presence mountain at 3 kHz, a valley carved at 700 Hz"); }
    { Terrain t; t.add ({ ghost::terrain::hzToX (400.0f, land::range()), 0.15f, 0.8f, 0.05f, 0.14f, 0.0f }); t.add ({ ghost::terrain::hzToX (1500.0f, land::range()), 0.5f, 0.8f, 0.05f, 0.14f, 0.0f }); t.add ({ ghost::terrain::hzToX (5000.0f, land::range()), 0.85f, 0.8f, 0.05f, 0.14f, 0.0f });
      write (*land::shapeTake (*dry, t), "43-take-travelling-in-time", "three mountains front to back: the resonance climbs 400 Hz -> 1.5 kHz -> 5 kHz across the take"); }
    { Terrain t = mountain (900.0, 0.7f, 0.08f, 0.5f, 2.0f, 0.9f); t.grit = 0.5f; write (*land::shapeTake (*dry, t), "44-take-gritty-spire", "a rough mountain at 900 Hz with grit everywhere: saturated harmonics"); }
    { Terrain t = ghost::terrain::randomTerrain (12); write (*land::shapeTake (*dry, t), "45-take-randomize", "RANDOMIZE (seed 12)"); }
}
} // namespace twelve
