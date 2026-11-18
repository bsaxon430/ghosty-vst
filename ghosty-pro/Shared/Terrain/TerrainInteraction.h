#pragma once
// TERRAIN editing: the state a page holds (the terrain, A/B, undo / redo) and the mouse gestures that shape it.
// No drawing here: the view (TerrainView.h) turns screen points into landscape points and forwards the mouse.
//
// Gestures (as in the "Moving mountains" reference, plus ours). The land is ONE mass: pulling anywhere on it raises a new
// peak out of it; the thin handle lines with dots are the grab points of the peaks already there.
//   drag up anywhere on the land   a new mountain rises there (drag down: a valley); a click alone raises a small one
//   drag a handle's dot            up / down = height (a value tag shows it)
//   drag a handle's line           move that mountain (sideways = frequency, forward / back = time)
//   drag the "< 0.22 >" width tag  sideways = width (Q); up / down = how long it lasts in time
//   Shift + drag a dot             grit on that mountain
//   wheel / pinch on a mountain    width (Q)
//   double-click a dot / mountain  remove it
// Every gesture is ONE undo step. Buttons (RANDOMIZE, SMOOTH, ...) go through apply(), also one undo step each.
#include "TerrainModel.h"
#include <juce_gui_basics/juce_gui_basics.h>   // juce::Point, juce::ModifierKeys
#include <deque>
#include <array>
#include <vector>
#include <algorithm>
#include <functional>

namespace ghost::terrain
{
class TerrainEditor
{
public:
    TerrainEditor();

    const Terrain& terrain() const { return slots[(size_t) active]; }
    void set (const Terrain& t);                                     // replaces, undoable
    void apply (const std::function<void (Terrain&)>& edit);        // one undoable edit
    void setWithoutUndo (const Terrain& t);                          // EVOLVE animation, restoring state

    // gestures: begin, change freely (each change notifies), end = one undo step if anything changed
    void beginGesture();
    Terrain& live();                     // the terrain to change during a gesture
    void changed();                      // notify after changing live()
    void endGesture();
    bool inGesture() const { return gesture; }
    // how fast the land is being pulled about right now (0..1; a quick drag ~1), easing back to 0 a quarter second after
    // the hand stops: the "movement" feature mappings use (drag speed -> wobble ...)
    float motion() const;

    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }
    bool undo();
    bool redo();
    static constexpr int kMaxUndo = 64;

    // A/B: two terrains to compare; toggling is not an edit (A/B choice itself is not undone)
    int activeSlot() const { return active; }
    void toggleAB();
    const Terrain& slot (int i) const { return slots[(size_t) juce::jlimit (0, 1, i)]; }
    void copyActiveToOther();

    int selected = -1;                   // the peak the handles highlight
    void select (int i) { if (i != selected) { selected = i; notify(); } }

    std::function<void()> onChange;      // any change of the visible terrain (message thread)
    std::function<void()> onCommit;      // an undoable edit finished (gesture end, apply, undo, redo, A/B)

    int watch (std::function<void()> f); // more change listeners (views); unwatch before they go away
    void unwatch (int id);

    juce::var stateToVar() const;        // both slots + which one is active (sessions)
    bool stateFromVar (const juce::var& v);

private:
    std::array<Terrain, 2> slots;
    int active = 0;
    bool gesture = false;
    Terrain before;
    std::deque<std::pair<int, Terrain>> undoStack, redoStack;   // (slot, terrain before)
    std::vector<std::pair<int, std::function<void()>>> watchers;
    int nextWatch = 0;
    void pushUndo (const Terrain& prior);
    Terrain lastSeen;
    double lastSeenMs = 0, motionMs = 0;
    float motionLevel = 0.0f;
    void trackMotion();
    void notify();
};

// The mouse, in landscape coordinates (the reference's sculpt mode, plus ours).
//   tap empty land (nothing chosen)  a new mountain there (0.30 tall, r 0.20), chosen
//   tap empty land (one chosen)      let go of it
//   pull up on empty land            a new mountain rises with the pull (ours)
//   drag a dot or its line           move that mountain over the ground
//   drag the "0.30" height tag       up / down = height (0 .. 1.2; below 0 a valley, ours)
//   drag the "< 0.20 >" size tag     sideways = size; or drag the chosen mountain's lit outline in / out
//   Shift + drag a dot               grit on that mountain (ours)
//   wheel / pinch on a mountain      size (ours);  double-click / Delete  remove it
// The view says what is under the pointer (hit) and turns screen points into ground points (groundAt); a host without a
// view can use the simple callbacks (pick / handleAt / lineAt / widthTagAt / bodyAt) instead.
class TerrainInteraction
{
public:
    explicit TerrainInteraction (TerrainEditor& e) : ed (e) {}

    enum class Kind { none, move, height, size };
    struct Hit { int peak = -1; Kind kind = Kind::none; bool byNumber = false; float lift = 0.0f; };
    std::function<Hit (juce::Point<float> screen)> hit;
    // screen -> ground (x, z) on the level plane `lift` (land units); false when it is off the island
    std::function<bool (juce::Point<float> screen, float lift, float& x, float& z)> groundAt;

    std::function<bool (juce::Point<float> screen, float& x, float& z)> pick;   // the land under the pointer; false off the island
    std::function<int (juce::Point<float> screen)> handleAt;     // simple hosts: the dot (height)
    std::function<int (juce::Point<float> screen)> lineAt;       // the handle line (move)
    std::function<int (juce::Point<float> screen)> widthTagAt;   // the size tag
    std::function<int (juce::Point<float> screen)> bodyAt;       // the mountain itself (double-click, wheel)
    float pixelsPerUnitHeight = 160.0f;  // how far to drag for 1 of height (the view sets it from its camera)
    float pixelsPerUnitSize = 200.0f;    // how far to drag sideways for 1 of size (r) on the size tag

    void mouseDown (juce::Point<float> p, juce::ModifierKeys mods);
    void mouseDrag (juce::Point<float> p, juce::ModifierKeys mods);
    void mouseUp (juce::Point<float> p, juce::ModifierKeys mods);
    void doubleClick (juce::Point<float> p);
    void wheel (juce::Point<float> p, float deltaY);
    void magnify (juce::Point<float> p, float scale);
    bool removeSelected();               // the Delete key

    juce::String liveText;               // what the gesture is doing ("0.42")
    bool active() const { return mode != Mode::none && mode != Mode::pending; }
    enum class Mode { none, pending, create, height, width, move, grit };
    Mode currentMode() const { return mode; }
    int currentPeak() const { return index; }
    bool hasMoved() const { return moved; }
    // a press on a tag that did not move (a click): the view lets you type the number (the reference's)
    Kind clickedTag() const { return clicked; }
    float dragThreshold = 3.0f;          // pixels before a press becomes a drag (the reference: 3 with a mouse, 10 touch)

private:
    Mode mode = Mode::none;
    TerrainEditor& ed;
    juce::Point<float> start;
    Peak startPeak;
    float startX = 0, startZ = 0, lift = 0, grabX = 0, grabZ = 0, grabRatio = 0;
    bool byNumber = false, wasSelected = false;
    int index = -1;
    bool moved = false;
    Kind clicked = Kind::none;
    Hit legacyHit (juce::Point<float> p, juce::ModifierKeys mods) const;
};
}
