// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX editor: the Ghost Box layout ported as it is (header / rack panel with tab row, unit strip, unit page, macro strip /
// GHOST SCENES bar), its colours, fonts, ghost art and knob styles, renamed PHANTOM FX. The new rack features (lanes, modulators,
// scenes with morph time, A/B, presets) sit inside that design in its own visual language.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "../ui/FxUi.h"

namespace phantom
{
class RackPanel;
class SceneBar;

class PhantomEditor : public juce::AudioProcessorEditor, public juce::DragAndDropContainer, private juce::Timer
{
public:
    explicit PhantomEditor (PhantomProcessor&);
    ~PhantomEditor() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    // the looks: the painted ones (DAWN, CEL, NEON, and any further asset folder), numbered from fxtheme::kFirstPainted. Numbers
    // 0..3 are the old drawn looks (BLACK, PURPLE, LIGHT, GHOST): still in the code, not offered; setLook() turns them into DAWN
    int numLooks() const; int firstLook() const; int currentLook() const { return shownLook; } juce::String lookTitle (int look) const;   // (the looks offered are firstLook() .. numLooks() - 1)
    void showTab (int tab);   // 0 RACK, 1 LANES, 2 MODS, 3 PERFORM, 4 SPATIAL (screenshots, tests)
    // tests: run the 12 fps animation without a window, and note every area an animation tick repaints
    bool forceAnim = false; juce::Array<juce::Rectangle<int>> dirtyLog; int animTicks() const { return tick; }
    void animStepForTest() { animAcc = 1.0; animStep(); }
    juce::Rectangle<int> blurbAreaForTest() const; std::vector<juce::Rectangle<int>> sceneGhostAreasForTest() const;   // (window pixels; empty when the page has none)
    std::vector<juce::Rectangle<float>> sceneGhostRects (painted::Look&, const juce::String& page) const;
    static juce::String creditsText();
    void setLook (int look);
    bool unitVisuals() const; void setUnitVisuals (bool on);   // the unit visuals' switch (a UI preference, remembered like the look)
    void toast (const juce::String& s);
    void showUnit (int unit);   // open a unit's page (screenshots, tests)
    void showSpatial (int unit);   // open the SPATIAL tab with a tile selected (screenshots, tests)
    PhantomProcessor& proc;

private:
    void timerCallback() override;
    void drawLogo (juce::Graphics& g, juce::Rectangle<float> r, double t);
    void drawMeter (juce::Graphics& g, juce::Rectangle<float> r, const float* lv, const float* tp);
    void paintPainted (juce::Graphics&, painted::Look&); void resizedPainted (painted::Look&); void animStep(); void dirty (juce::Rectangle<int>);
    juce::Rectangle<float> hatchRect (painted::Look&, const juce::String& page) const;
    juce::String pageKey (painted::Look&) const;   // "rack", "rack-wide" (a page on the wide slab, on looks that paint it differently) or "spatial"
    juce::Rectangle<float> rainRect (painted::Look&, const juce::var& rain, const juce::String& page) const;
    juce::SharedResourcePointer<painted::Registry> looks;   // the painted looks' sprites, shared by every open editor
    gpfx::ui::LnF lnf;
    std::unique_ptr<gpfx::ui::Knob> inK, outK, mixK;
    juce::TextButton snapBtn { "SNAPSHOT" }, snapList { "v" }, abBtn { "A" }, abCopy { "A>B" }, themeBtn { "PURPLE" }, aboutBtn { "i" }, bypassBtn { "BYPASS" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAtt;
    std::unique_ptr<RackPanel> panel; std::unique_ptr<SceneBar> scenes;
    juce::Rectangle<int> header, logo, inMeter, outMeter, mixHint;
    float inLv[2] { 0, 0 }, outLv[2] { 0, 0 }, outTp[2] { 0, 0 }; double t = 0, toastUntil = 0; juce::String toastText; int shownLook = 1;
    juce::Image overlayCache; juce::String overlayKey; std::map<juce::String, juce::Image> bgCaches, glassCaches;   // each page's painting, its blurred copy and the light overlay at the window's size (scaled once, kept)
    int tick = 0, hatchTrig = -100, hatchFrame = 0, trainStart = -1, holoFrame = 0, glowStep = 0; double animAcc = 0; float hatchSlow = 0; juce::Random rng;
    static constexpr int kHeader = 62;
    static constexpr int kMinW = 960, kMinH = 640;   // the drawn looks' smallest window
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhantomEditor)
};
} // namespace phantom
