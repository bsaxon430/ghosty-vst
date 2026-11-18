#pragma once
#include "BoxProcessor.h"
#include "FxUi.h"
#include "BoxSceneBar.h"

std::unique_ptr<juce::Component> makeCapturePage (BoxProcessor& p);   // the CAPTURE / DNA tabs' pages (made by the rack panel on demand)
std::unique_ptr<juce::Component> makeDnaPage (BoxProcessor& p);
std::unique_ptr<juce::Component> makeSplitPage (BoxProcessor& p);   // GHOST SPLIT (the SPLITS tab)
std::unique_ptr<juce::Component> makeFocusPage (BoxProcessor& p);   // GHOST FOCUS (the FOCUS tab)
std::unique_ptr<juce::Component> makePrintPage (BoxProcessor& p);   // GHOSTPRINT (the PRINT tab)
std::unique_ptr<juce::Component> makeSpacePage (BoxProcessor& p);   // GHOST SPACE (the G-SPACE tab)

// Ghost Box window: header (logo, input / output gain with meters, dry/wet, bypass, unlock) over the Ghost FX rack.
class BoxEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BoxEditor (BoxProcessor&);
    ~BoxEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    static void drawLogo (juce::Graphics& g, juce::Rectangle<float> r, double t);   // the ghost in a box
    static juce::String versionString();   // "v3.0.0" (JucePlugin_VersionString)

private:
    void timerCallback() override;
public:
    void setTheme (ghosty::themes::Theme t, bool force = false) { setLook (ghosty::themes::toInt (t), force); }   // BLACK / PURPLE / LIGHT (saved per project in the processor, never in presets)
    void setLight (bool light, bool force = false) { setTheme (ghosty::themes::fromLegacyBool (light), force); }
    // the look: 0 BLACK, 1 PURPLE, 2 LIGHT, 3 GHOST (liquid glass on the LIGHT base, Ghost Box only; theme() then reads LIGHT)
    static constexpr int kLookGhost = BoxProcessor::kLookGhost;
    void setLook (int look, bool force = false);
    int look() const { return shownLook; }
    bool isGhost() const { return shownLook == kLookGhost; }
    static int nextLook (int l) { return l == kLookGhost ? 0 : l == 2 ? kLookGhost : ghosty::themes::toInt (ghosty::themes::next (ghosty::themes::fromInt (l))); }
    static juce::String lookName (int l) { return l == kLookGhost ? juce::String ("GHOST") : ghosty::themes::displayName (ghosty::themes::fromInt (l)); }
    ghosty::themes::Theme theme() const { return shownTheme; }
    bool isLight() const { return shownTheme == ghosty::themes::Theme::Light; }
    // GHOST tests: the cached glass layers (built once per size) and the surfaces under the header texts
    ghosty::glass::Backdrop backdrop; ghosty::glass::PanelCache headerGlass;
    void paintGhostSurfaces (juce::Graphics& g);   // the aurora, the header glass, the meter wells (no texts)
    // PERFORMANCE: the aurora + every static glass panel (header, scene bar, page card, macro bar) composited once per size / layout into one
    // opaque image; a repaint blits it (then only the live parts: tiles, controls, texts, meters)
    juce::Image ghostStatic; juce::String ghostStaticKey; int ghostStaticBuilds = 0;
    void drawGhostStatic (juce::Graphics& g);
    struct GhostText { juce::Rectangle<int> r; juce::Colour c; juce::String what; };
    std::vector<GhostText> ghostHeaderTexts() const;   // where paintGhost draws its texts, in which colour (tests: contrast on pixels)
    juce::TextButton& lookButton() { return themeBtn; }
    bool keyPressed (const juce::KeyPress& k) override   // Shift+Alt+S = SNAPSHOT, Shift+Alt+C = GHOST CAPTURE (plain letters stay piano keys)
    {
        const auto m = k.getModifiers();
        if (m.isShiftDown() && m.isAltDown() && ! m.isCommandDown() && ! m.isCtrlDown() && juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) k.getKeyCode()) == 'C'
            && ! ghosty::SnapshotBank::isTypingIntoText()) { toastText = proc.captureNow(); toastUntil = t + 2.5; repaint (mixHint); return true; }
        return snapBar.handleKey (k);
    }
    ghosty::SnapshotBar& snapshotBar() { return snapBar; }
    BoxSceneBar& sceneBar() { return scenes; }
    int auroraHeight() const { return getHeight() - (scenes.isVisible() ? scenes.getHeight() : 0); }   // GHOST: the aurora's own height (above the scene bar)
    // GHOST SCENES: the scene bar sits below the rack, the window is this much taller than before (nothing above it moved)
    static constexpr int kSceneBar = BoxSceneBar::kHeight;
private:
    juce::String toastText; double toastUntil = 0;
    ghosty::themes::Theme shownTheme = ghosty::themes::Theme::Dark; int shownLook = 0;
    void paintGhost (juce::Graphics& g);
    void drawMeter (juce::Graphics& g, juce::Rectangle<float> r, const float* lv, const juce::String& label);
    BoxProcessor& proc;
    gpfx::ui::LnF lnf;
    gpfx::ui::Knob inK, outK, mixK; juce::TextButton abBtn, abCopy, themeBtn;
    ghosty::BypassButton bypassButton { *proc.bypassParam };
    ghosty::UnlockButton unlock { proc.license };
    gpfx::Panel panel;
    ghosty::SnapshotBar snapBar { *proc.snapshots, true };   // (after proc: it needs the bank)
    BoxSceneBar scenes { proc };                              // GHOST SCENES (below the rack)
    juce::TooltipWindow tips { this, 600 };   // the one tooltip window of this editor (themed through its look-and-feel)
    struct HeaderRightClick : juce::MouseListener   // header sliders (IN / OUT / MIX): Type value... / Reset (the rack has its own menu)
    {
        BoxEditor* ed = nullptr;
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (! e.mods.isPopupMenu() || ed->panel.isParentOf (e.eventComponent)) return;
            if (auto* sl = gpfx::ui::sliderAt (e.eventComponent, ed))
            {
                juce::PopupMenu m; gpfx::ui::addSliderItems (m, true); gpfx::ui::addSliderItems (m, false);
                juce::Component::SafePointer<juce::Slider> sp (sl);
                m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (sl).withMousePosition(), [sp] (int r) { gpfx::ui::handleSliderItem (r, sp.getComponent()); });
            }
        }
    } headerRightClick;
    juce::Rectangle<int> header, logo, inMeter, outMeter, mixHint;
    bool bypassed() const { return proc.bypassParam != nullptr && proc.bypassParam->get(); }
    float inLv[2] {}, outLv[2] {}; double t = 0;
    static constexpr int kHeader = 62;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BoxEditor)
};
