#pragma once
// TERRAIN view: the landscape on screen, its handles, tags, ring labels and the small toolbar. GhostBlock's own code (closed).
//
// The look and the feel are the reference's (topographically.vercel.app, which Brandon showed us; measured, its maths
// re-derived from what the page does, none of its code used):
//   - a near-black ground; the land drawn ONLY as thin (1 px) contour rings, 25 to a full height, seen from 50 degrees
//     down and turned 20 degrees (orthographic); rings bright in the middle of the island, fading towards its shore;
//     hidden lines hidden (each ring's land hides what is behind it)
//   - every mountain has a thin vertical line rising from its summit with a dot on top; taller than the top ring, a
//     mountain has a flat top (a plateau / a column)
//   - choosing a mountain lights ITS rings in the accent (the rest dims to a third), its outline at the base is drawn in the
//     accent, a "0.30" height tag (arrows above and below) sits on its line and a "< 0.20 >" size tag under its base;
//     while one is dragged it becomes a filled pill
//   - grey height numbers ride IN the rings ("0", "50", "100" ...); the TAG button lets you tap a ring and write your own
//   - opening, the land tilts up from a flat map; a tiny bottom-centre toolbar: shuffle, tag, x
// Ours on top: per-plug-in colours (Palette), our extra buttons behind "...", valleys, a live "what this is doing" readout.
//
// Rendering: the land is sampled on a grid over the island, its rings cut cell by cell (marching squares), simplified,
// smoothed into curves, lifted to their heights and painted from the lowest ring up, each ring's land filled with the
// ground colour first (that is what hides the lines behind it). The rings are drawn into a cache at the screen's pixel
// scale, redone only when the land, the choice or the size changes; handles, tags and labels go on top every frame.
#include "TerrainInteraction.h"
#include "TerrainMapping.h"
#include "TerrainFeatures.h"
#include "TerrainPalette.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <array>
#include <vector>

namespace ghost::terrain
{
class TerrainView : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit TerrainView (TerrainEditor& editor, Range range = {});
    ~TerrainView() override;

    Palette palette;
    Range range;
    float groundScale = 1.0f;   // the island's size before the fit (the reference: 1 on a desktop, 1.5 on a phone)

    // ---- host hooks
    std::function<void()> onListen;                 // LISTEN pressed
    std::function<bool()> isListening;              // for the LISTEN icon (play / stop)
    std::function<void (const juce::String&)> onStatus;   // the live gesture text, if the host shows it elsewhere
    // UNDO / REDO (the toolbar, Cmd+Z in the view): the editor's own history, or the host's when it keeps one history for the
    // whole plug-in (set all four; the hooks return whether anything was undone)
    std::function<bool()> onUndo, onRedo, canUndoHook, canRedoHook;
    void setPlayhead (float z);                      // the time cursor (< 0 hides it)
    void setSpectrum (std::vector<float> dbAcrossX); // analyzer overlay data (x = 0..1 evenly, dB; 0 = top, -72 = ground)
    void setAnalyzerVisible (bool on) { analyzer = on; repaint(); }
    bool analyzerVisible() const { return analyzer; }
    void setReadoutsVisible (bool on) { readouts = on; repaint(); }   // the chosen mountain in sound ("820 Hz +9 dB Q 4")
    bool readoutsVisible() const { return readouts; }
    void setToolbarVisible (bool on) { toolbarOn = on; resized(); repaint(); }
    void setExtrasVisible (bool on) { extrasOn = on; repaint(); }    // our buttons (LISTEN, SMOOTH, FREEZE ...) behind "..."
    bool extrasVisible() const { return extrasOn; }
    void setTimeLabel (const juce::String& s) { timeLabel = s; }
    void setDisplayPhase (float phase);              // EVOLVE: draw with this phase (the audio thread's); < 0 = the view ticks it
    void setPalette (const Palette& p);
    // the "what this is doing" readout: up to 5 meters (name, 0..1) the host's mapping is pushing, drawn small at the top left
    void setMeters (std::vector<Meter> m);
    // SHAPE PRESETS: a small "SHAPES" pill at the top right lists them; picking one sets the land (one undo step).
    // Empty = the shared list (shapePresetName()); a host may give its own names and build each land itself (onShape).
    juce::StringArray shapeNames;
    std::function<Terrain (const juce::String& name)> makeShape;   // optional: the host's own land for a name
    std::function<void (const juce::String& name)> onShape;         // after a shape was picked
    juce::Rectangle<float> shapesBounds() const;
    void pickShape (const juce::String& name);
    // the opening (the land tilts up from a flat map over 1.8 s); hosts call it when the page is shown
    void playOpening();
    float openingProgress() const { return openT; }

    // EVOLVE / FREEZE state lives in the terrain (saved with it); the toolbar toggles these
    void toggleEvolve();
    void toggleFreeze();

    // ---- the TAG tool (the reference's label mode): tap a ring to write on it
    void setLabelMode (bool on);
    bool labelMode() const { return labelling; }
    int editingLabel() const { return editLabel; }

    // ---- geometry (public for tests and the host)
    juce::Point<float> project (float x, float z, float h) const;     // landscape -> screen (h in land units: 1 = the top ring)
    bool pickGround (juce::Point<float> p, float& x, float& z) const; // screen -> landscape on the ground (h = 0)
    bool groundAtLift (juce::Point<float> p, float lift, float& x, float& z) const;   // the same on the plane at height `lift`
    bool landAt (juce::Point<float> p, float& x, float& z) const;     // the land under the pointer (its height taken in)
    int handleAt (juce::Point<float> p) const;                         // peak whose dot is under p, or -1
    int lineAt (juce::Point<float> p) const;                           // peak whose handle line is under p, or -1
    int bodyAt (juce::Point<float> p) const;                           // peak whose mountain is under p, or -1
    int widthTagAt (juce::Point<float> p) const;
    TerrainInteraction::Hit hitAt (juce::Point<float> p) const;        // what a press there would grab (the reference's order)
    juce::Point<float> widthTagCentre (int peak) const;
    juce::Point<float> heightTagCentre (int peak) const;
    juce::Point<float> summit (int peak) const;                        // the top of that mountain on screen
    juce::Rectangle<int> changingBounds() const;
    juce::Point<float> handleDot (int peak) const;
    juce::Rectangle<float> toolbarBounds() const;
    enum class Button { randomize, listen, smooth, freeze, evolve, ab, labels, analyzer, undo, redo, reset, more, count };
    juce::Rectangle<float> buttonBounds (Button b) const;              // empty when it is not shown
    void press (Button b, juce::ModifierKeys mods = {});                // what a click on that button does
    bool undoAvailable() const; bool redoAvailable() const;
    bool doUndo(); bool doRedo();
    float groundAspect() const { return 1.0f; }
    float cameraTilt() const { return tiltNow; }                         // degrees down (the reference's 50)
    float pixelsPerRadius() const { return K; }                          // the island's radius on screen (before the tilt)

    // tests
    double benchmarkFrameMs (int frames, bool changing);
    static double profileMs[8];
    int contourCount() const { return segmentCount; }
    int ringCount() const { return (int) rings.size(); }
    int labelCountShown() const;
    juce::Image snapshot (float scale = 1.0f);
    juce::String labelTextAt (int i) const;                              // the drawn auto / own labels (tests)

    TerrainInteraction interaction;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scale) override;
    bool keyPressed (const juce::KeyPress&) override;

public:
    void settle();                                     // tests / screenshots: jump the drawn land to the model (no lag, no opening)
    float stillOpening = -1.0f;                        // tests: settle() leaves the opening this far (0 = flat map .. 1 = up)
    float tintOf (int peak) const { return peak >= 0 && peak < Terrain::kMaxPeaks ? heldNow[(size_t) peak] : 0.0f; }

    // one contour ring as drawn (public for tests)
    struct Ring
    {
        int level = 0;                                 // 1..25 (negative: under the ground)
        bool closed = true;
        std::vector<juce::Point<float>> ground;        // (u, v) island coordinates, -1..1
        std::vector<juce::Point<float>> screen;        // projected
        juce::Rectangle<float> box;                    // screen bounds
        float length = 0.0f;                           // screen length
    };
    const std::vector<Ring>& drawnRings() const { return rings; }

private:
    TerrainEditor& ed;
    // ---- camera (updateCamera): the island (radius K px) turned kTurn, seen tiltNow degrees down, orthographic
    float K = 100.0f, ax = 0, ay = 0, m11 = 1, m12 = 0, m21 = 0, m22 = 1, rise = 0, heightUnits = 0.64f;
    float viewOffX = 0, viewOffY = 0; bool offsetValid = false;
    float tiltNow = 50.0f, openT = 1.0f; double openStart = 0;
    void updateCamera();
    void fitOffset();
    float liftPx (float h) const { return h * heightUnits * rise; }   // a height on screen
    juce::Point<float> toScreen (float u, float v, float h) const;
    bool toGround (juce::Point<float> p, float h, float& u, float& v) const;
    float uiScale() const;                         // tags, stems, fonts: the reference's at 745 px tall and above, smaller below

    // ---- the sampled land
    int N = 0;                                     // grid points across (and deep)
    std::vector<float> field, shapeCache, litField, rimField;
    uint32_t shapeSeed = 0; uint8_t shapeVersion = 0; int shapeN = 0;
    float landStep (float u, float v) const;       // the land's height at (u, v) snapped down to its ring (the reference's)
    float landRaw (float u, float v) const;

    // ---- rings and the cache
    std::vector<Ring> rings;
    juce::Image cache, colourImg;
    float cacheScale = 1.0f;
    bool cacheValid = false, dirty = true;
    int segmentCount = 0;
    void renderCache (float scale);
    void ensureBuilt() const;
    void buildRings (const Terrain& t);

    // ---- handles, tags
    struct Handle
    {
        juce::Point<float> summitPt, valuePt, pinTop, dot, stemTop, foot;
        float top = 0.0f;                          // summit height (snapped), land units
        float landTop = 0.0f;                      // the land's height there (not snapped, capped at 1)
        std::vector<std::pair<float, float>> stemVisible;   // y ranges of the stem not hidden by land in front
        bool dotVisible = true;
        std::vector<juce::Point<float>> outline;   // the chosen one's base outline (screen), with hidden flags
        std::vector<bool> outlineHidden;
        juce::Point<float> sizeFrom, sizeTo, sizeAnchor;
        int sizeSide = 0;                           // 0 below, 1 right, 2 left
        juce::Rectangle<float> heightBox, sizeBox;
    };
    std::vector<Handle> handles;
    std::array<int, Terrain::kMaxPeaks> sizeSideOf {};   // the side each one's size tag hangs on (kept while it fits)
    void buildHandles (const Terrain& t);
    juce::Point<float> outlinePoint (const Terrain& t, int i, float angle, float& lift) const;

    // ---- labels (auto heights + own ones)
    struct Label { int ring = -1; float at = 0.0f; juce::String text; bool own = false; int slot = -1; float alpha = 1.0f; };
    std::vector<Label> labels;
    void placeLabels (const Terrain& t);
    double quietUntil = 0, labelsShownAt = 0; bool labelsQuiet = false;
    Terrain lastLand; bool labelsPending = true; double landChangedAt = 0;
    bool labelling = false; int editLabel = -1; juce::String typing; bool typedAny = false;
    int ringUnder (juce::Point<float> p, float reach, float& at) const;

    // ---- the tag editor (a click on a tag lets you type the number)
    int editPeak = -1; TerrainInteraction::Kind editKind = TerrainInteraction::Kind::none; juce::String editText; bool editTyped = false;

    // ---- motion: the drawn land follows the model (a spring, 0.35 s); new mountains grow in (700 ms), removed ones shrink
    // (500 ms); a chosen mountain's light eases in and out (180 ms) - the reference's timings
    struct Shown
    {
        Peak p;
        float v[5] = {};
        float growFrom = 1.0f, growTo = 1.0f; double growT0 = 0; float growMs = 0;
        bool ghost = false;
    };
    std::vector<Shown> shown;
    int shownCount = -1;
    std::array<float, Terrain::kMaxPeaks> heldNow {}, heldFrom {}, heldTo {}, hoverNow {}, hoverFrom {}, hoverTo {};
    std::array<double, Terrain::kMaxPeaks> heldT0 {}, hoverT0 {};
    bool motionActive = false;
    double lastStep = 0;
    float growOf (const Shown&, double now) const;
    void stepMotion (double now);
    void setHeld (int peak, float to, double now);
    void setHover (int peak, float to, double now);
    void refreshLights (double now);
    Terrain drawn() const;

    // ---- the rest
    bool analyzer = false, readouts = false, toolbarOn = true, extrasOn = false;
    juce::String timeLabel { "TIME" };
    float playZ = -1.0f, displayPhase = -1.0f;
    std::vector<float> spectrum;
    std::vector<Meter> meters;
    int hoverButton = -1, hoverPeak = -1, watchId = 0, randomCount = 0;
    TerrainInteraction::Kind hoverKind = TerrainInteraction::Kind::none;
    bool pressingButton = false;
    double lastTick = 0;
    mutable std::map<int, juce::Font> fontCache;

    void timerCallback() override;
    void markDirty();
    void drawHandles (juce::Graphics&);
    void drawTag (juce::Graphics&, juce::Point<float> c, const juce::String& text, bool vertical, float chip, float arrows, juce::Colour accent, float alpha);
    void drawToolbar (juce::Graphics&);
    void drawOverlays (juce::Graphics&);
    void drawMeters (juce::Graphics&);
    void drawIcon (juce::Graphics&, Button, juce::Rectangle<float>, juce::Colour) const;
    juce::String buttonName (Button) const;
    juce::Font fontOf (float h, bool bold = false) const;
    void status (const juce::String&);
    bool buttonShown (Button b) const;
};
}
