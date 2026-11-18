#pragma once
// Ghosty Pro MODULATION UI (round 3 item 3), strictly additive: nothing existing moves, resizes or recolours.
//   * SOURCE TILES: small drag tiles in the free header space of the MOD page's LFO 1 / LFO 2 / ENV 2 / ENV 3 / ENV 1 / MACROS cards,
//     and every source on the ROUTES sub-page (button in the MOD MATRIX card's header). Drag a tile onto any knob: a connection;
//     hold it over a tab to switch pages while dragging. Hovering a tile lights up every knob it modulates.
//   * AMOUNT RINGS: drawn by a transparent overlay around every knob that has connections (arc = depth, colour per source, dim when
//     bypassed, a dot where the modulation is right now). Drag a ring up / down to change its amount (Shift = fine).
//   * RIGHT-CLICK on any knob: Type a value (what right-click did before), Mod Source, Aux Source, Edit Custom Curve, Bypass,
//     Remove, Remove All, Reset Control, MIDI Learn.
//   * ROUTES sub-page: LFO 3 / LFO 4, all source tiles, LFO 1 / 2 DEPTH, and the MOD ROUTES list (source / target / amount / curve /
//     aux / bipolar / bypass / delete), sortable, with a drag handle that moves a modulation to another knob.
#include "ProProcessor.h"
#include "GhostUI.h"
#include "ProTheme.h"

namespace gpro
{
class ModOverlay;
class ModTile;
class ModRoutesPage;

class ModUi : private juce::MouseListener, private juce::Timer, private juce::ComponentListener
{
public:
    struct Hooks
    {
        std::function<void (int)> showTab;                       // page index (0 SYNTH, 1 MOD, ...)
        std::function<int (juce::Point<int>)> tabAt;              // content point -> the page index of the tab under it (-1 none)
        std::function<int()> currentTab;
        std::function<void (const juce::String&)> toast;
    };
    ModUi (ProProcessor& p, juce::Component& content, juce::Component& modPage, Hooks hooks);
    ~ModUi() override;

    // right-click on a slider (ghosty::TypeIn::intercept): true = the knob menu opened
    bool knobRightClick (juce::Slider& s);
    juce::PopupMenu buildKnobMenu (const juce::String& paramId) const;
    void runKnobMenu (const juce::String& paramId, int result, juce::Slider* s);
    void showKnobMenu (juce::Slider& s);

    // what a tile drop / a move drop does (also the tests' way in)
    int dropSource (int src, const juce::String& paramId);
    bool dropMove (int connIndex, const juce::String& paramId);

    // rings: which ring of which knob is at a content point (-1 none); geometry of a knob's rings
    struct RingHit { juce::Slider* slider = nullptr; int conn = -1; int ring = -1; };
    RingHit ringAt (juce::Point<float> contentPos) const;
    struct KnobGeom { juce::Point<float> c; float rad = 0, a0 = 0, a1 = 0, pos = 0; };
    bool knobGeom (juce::Slider& s, KnobGeom& g) const;          // in content coordinates
    static float ringRadius (float rad, int ring) noexcept { return rad + 2.2f + 3.6f * (float) ring; }
    juce::Slider* sliderFor (const juce::String& paramId) const;
    bool knobVisible (juce::Slider& s) const;                     // showing and not covered by another component
    void setHoverSource (int src);
    int hoverSource() const noexcept { return hoverSrc; }
    void openRoutes (bool open);
    bool routesOpen() const;
    juce::Component* overlay() const;
    juce::Component* routes() const;
    juce::Component* routesButton() const { return routesBtn.get(); }
    std::vector<juce::Component*> tiles() const;
    std::unique_ptr<juce::Component> makeCurveEditor (int connIndex);   // the custom-curve popup's content
    static juce::Colour srcColour (int src);
    // other layouts (STUDIO, round 3 item 7): a source tile to place anywhere (the parent owns it), and "knobs were added" (the
    // knob cache is rebuilt so new knobs get rings, drops and the knob menu)
    std::unique_ptr<juce::Component> makeTile (int src, bool small, const juce::String& label = {});
    void knobsChanged();
    juce::Component* menuParent = nullptr;                        // tests: menus open inside this component (snapshots)
    std::function<void (int)> onMenuResult;                       // tests: called after a knob-menu item ran

    // a drag in progress (tile -> knob, or a route's handle -> another knob)
    void beginDrag (int src, int moveConn, juce::Point<int> screenPos);
    struct DragState { bool on = false; int src = 0, moveConn = -1; juce::Point<float> pos; juce::Component::SafePointer<juce::Slider> target; int tab = -1; double tabSince = 0; };
    const DragState& drag() const noexcept { return dragSt; }
    void dragMoveTo (juce::Point<int> screenPos);                 // (tests drive these directly)
    void dragEnd (juce::Point<int> screenPos);

    ProProcessor& proc;
    juce::Component& content;
    juce::Component& modPage;
    Hooks hooks;

private:
    std::unique_ptr<ModOverlay> ov;
    std::unique_ptr<ModRoutesPage> routesPg;
    std::unique_ptr<juce::TextButton> routesBtn;
    std::vector<std::unique_ptr<ModTile>> headerTiles;
    mutable std::vector<juce::Component::SafePointer<juce::Slider>> knobCache; mutable bool knobsBuilt = false;
    int hoverSrc = 0;
    DragState dragSt;
    juce::uint32 seenVer = 0;
    void layoutHeaderTiles();
    void timerCallback() override;
    void componentMovedOrResized (juce::Component&, bool, bool) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    const std::vector<juce::Component::SafePointer<juce::Slider>>& knobs() const;
    friend class ModOverlay;
};
} // namespace gpro
