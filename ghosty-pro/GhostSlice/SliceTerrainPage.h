// GHOST SLICE build 12: the TERRAIN page (over TAKE TYPE .. TAKES, like UNDER THE HOOD). The shared landscape control surface in
// Ghost Slice's colours: across = frequency, back = time across the take, height = resonance, width = Q, roughness = grit.
// Every change re-shapes the take on screen (under a second); LISTEN (or PLAY) hears it through the landscape as you drag.
#pragma once
#include "SliceEditor.h"
#include "SliceTerrain.h"
#include "TerrainView.h"

namespace gslice::ui
{
struct TerrainPage : juce::Component
{
    // (the same glass panel UNDER THE HOOD draws, kept here so this page lives in its own file)
    static void panel (juce::Graphics& g, juce::Rectangle<float> r, float rad, const Skin& S)
    {
        juce::Path p; p.addRoundedRectangle (r, rad);
        g.setGradientFill (juce::ColourGradient (S.panelTop, r.getX(), r.getY(), S.panelBot, r.getX(), r.getBottom(), false)); g.fillPath (p);
        juce::ColourGradient hi (S.panelHi, r.getX(), r.getY(), S.panelHi.withAlpha (0.0f), r.getX(), r.getY() + juce::jmin (40.0f, r.getHeight() * 0.5f), false);
        g.setGradientFill (hi); g.fillPath (p);
        g.setColour (S.panelLine); g.strokePath (p, juce::PathStrokeType (1.0f));
    }
    static void textLeft (juce::Graphics& g, const juce::String& s, const juce::Font& f, float x, float cy, float w = 900.0f)
    {
        g.setFont (f);
        g.drawText (s, juce::Rectangle<float> (x, cy - f.getHeight() * 0.5f, w, f.getHeight()), juce::Justification::centredLeft, false);
    }
    explicit TerrainPage (SliceProcessor& p) : proc (p), view (p.terrain, land::range())
    {
        addAndMakeVisible (backBtn); backBtn.setTooltip ("BACK: the main page again. The landscape stays on the take.");
        backBtn.textCap = 10.0f; backBtn.iconSize = 12.0f;
        backBtn.onClick = [this] { if (onBack) onBack(); };
        addAndMakeVisible (view);
        view.setTooltip ("TERRAIN: tap the land for a mountain (or pull the ground up; down carves a valley). Drag a dot or its line to move it. The chosen one shows its height tag (drag up / down) and its size tag (drag sideways): thin and tall = a spire, past the top = a flat-topped plateau. SHAPES: whole landscapes. It plays while you pull.");
        view.setTimeLabel ("TAKE");
        view.onListen = [this] { proc.playPreview (! proc.previewPlaying()); view.repaint(); };
        view.isListening = [this] { return proc.previewPlaying(); };
        // one history for the whole plug-in: the page's UNDO / REDO and Cmd+Z are the plug-in's (the boxes, the roll, the land)
        view.onUndo = [this] { return proc.undoEdit(); };
        view.onRedo = [this] { return proc.redoEdit(); };
        view.canUndoHook = [this] { return proc.canUndo(); };
        view.canRedoHook = [this] { return proc.canRedo(); };
    }
    SliceProcessor& proc;
    GButton backBtn { GButton::Style::Glass, Icon::ChevL, "BACK" };
    ghost::terrain::TerrainView view;
    const Skin* S = nullptr;
    std::function<void()> onBack;

    // the landscape keeps the topographic look on a dark card in every look; the accents follow the look
    static ghost::terrain::Palette paletteFor (const Skin& K)
    {
        auto p = ghost::terrain::Palette::ghostSlice();
        if (K.look == 0)   // BLACK: graphite, lilac -> cyan
        {
            p.ground = juce::Colour (0xff111215); p.accent = juce::Colour (0xffa77bff); p.accentTop = juce::Colour (0xff22e6ff);
            p.handle = juce::Colour (0xff22e6ff); p.handleDim = juce::Colour (0x80a77bff); p.valley = juce::Colour (0x80c4a6ff);
            p.button = juce::Colour (0xff212228); p.buttonHi = juce::Colour (0xff2c2d35); p.text = juce::Colour (0xffeeebf5); p.dim = juce::Colour (0xffa6a2b5);
        }
        else if (K.light)   // LIGHT / GHOST: the dark card stays, the accents are the look's violet and its deep ink
        {
            p.ground = juce::Colour (0xff1b1830); p.accent = juce::Colour (0xff8b5cf6); p.accentTop = juce::Colour (0xffff9cf2);
            p.handle = juce::Colour (0xffff9cf2); p.handleDim = juce::Colour (0x808b5cf6);
            p.button = juce::Colour (0xff2a2545); p.buttonHi = juce::Colour (0xff3a3460);
        }
        p.font = [] (float h, bool bold) { return capFont (h * 0.72f, bold ? Weight::SemiBold : Weight::Medium, 0.04f); };
        return p;
    }
    void setSkin (const Skin* s) { S = s; backBtn.S = s; if (s != nullptr) view.setPalette (paletteFor (*s)); }
    void tick()
    {
        view.setPlayhead (proc.previewPlaying() ? proc.previewPosition() : -1.0f);
        view.setMeters (proc.landMod().meters);   // build 13: what the land is doing to the sound
        if (view.analyzerVisible())
        {
            std::vector<float> db;
            if (proc.terrainTap.read (db, 160, view.range, proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0)) view.setSpectrum (std::move (db));
        }
    }
    juce::Rectangle<float> cardArea() const { return { 16.0f, 50.0f, (float) getWidth() - 32.0f, (float) getHeight() - 64.0f }; }
    void resized() override
    {
        backBtn.setBounds (getWidth() - 132, 6, 120, 40);
        view.setBounds (cardArea().reduced (1.0f).toNearestInt());
    }
    void paint (juce::Graphics& g) override
    {
        if (S == nullptr) return;
        const auto& K = *S;
        const auto all = getLocalBounds().toFloat();
        g.setColour (K.bg); g.fillRoundedRectangle (all, 16.0f);
        panel (g, all, 16.0f, K);
        { juce::ColourGradient bd (K.a2.withAlpha (0.7f), 0.0f, 0.0f, K.b.withAlpha (0.6f), all.getRight(), all.getBottom(), false); g.setGradientFill (bd); g.drawRoundedRectangle (all.reduced (0.8f), 16.0f, 1.4f); }
        const auto title = capFont (12.0f, Weight::SemiBold, 0.10f), small = capFont (8.2f, Weight::Medium, 0.06f);
        g.setColour (K.ink); textLeft (g, "TERRAIN", title, 18.0f, 27.0f);
        float x = 18.0f + ghosty::textWidth (title, "TERRAIN") + 14.0f;
        juce::String what = "the landscape plays this take:  across = frequency,  back = later in the take,  height = resonance,  spires = STUTTER + SHIMMER,  plateaus = DREAM,  rough = GRIT,  spread = WIDTH,  merging = SPACE.  It plays while you pull";
        if (auto t = proc.currentTake()) what = t->recipe.name + "  -  " + what;
        else what = "press GENERATE first, then shape the take";
        g.setColour (K.dim); textLeft (g, what, small, x, 27.0f, (float) getWidth() - x - 150.0f);
        // the dark card the landscape sits on
        g.setColour (view.palette.ground); g.fillRoundedRectangle (cardArea(), 10.0f);
        g.setColour (K.panelLine.withAlpha (0.6f)); g.drawRoundedRectangle (cardArea().reduced (0.5f), 10.0f, 1.0f);
    }
};
}
