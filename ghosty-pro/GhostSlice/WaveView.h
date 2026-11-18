#pragma once
// The waveform workspace (output timeline) + the lane strip under it.
//
// Gestures (same as Shared/WaveRegion.h where they overlap):
//   right-click ............ the cursor (play-from point) here          right-drag ...... a LOOP over that span
//   left-drag on the waveform  select a time range                       click ........... cursor + select the region
//   drag a region's top bar .. MOVE the region (snaps to the grid)       Cmd/Ctrl-drag body  SLIP (the audio slides under it)
//   drag a region edge ....... trim                                       Alt-drag the right edge / the STRETCH grip: STRETCH
//   the fade squares ........ fade in / fade out                          wheel = scroll, Cmd/Ctrl+wheel or pinch = zoom
//   FLEX: double-click = add a warp marker (snaps to an onset within 8 px), drag a marker = MOVE / STRETCH (the mode),
//         body drag in SLIP mode = slip; Alt = no snap, Shift = fine (1/4 speed), Cmd/Ctrl = full snap; click a marker = select it
//   PITCH (phase 14): click a note blob = select it (Cmd/Ctrl-click = add / remove), drag on empty space = select many (a band),
//         drag a blob up / down = its PITCH (semitone snap; Shift = free cents), Cmd/Ctrl-drag a blob = TIMING (move it in time),
//         drag a blob's left / right edge = TIMING (stretch its start / end), double-click a blob = SPLIT it there,
//         DRAW on: drag = draw a pitch curve (absolute, violet), one undo step per stroke; Up / Down = +-1 semitone (Shift: +-10 ct)
// Waveforms are drawn from the source's min/max peak pyramid into 256-physical-pixel image tiles (HiDPI), cached by
// (region map, zoom, height, scale, theme) in an LRU, so scrolling only renders the new tiles.
#include <juce_gui_basics/juce_gui_basics.h>
#include "SliceProcessor.h"
#include "SliceTheme.h"
#include "PitchEdit.h"
#include "SliceMascot.h"
#include <set>
#include <list>
#include <unordered_map>

namespace gslice::ui
{
enum class Workspace { Edit = 0, Flex = 1, Pitch = 2, Play = 3 };

struct Selection
{
    int64 regionId = 0;      // 0 = none
    int64 markerId = 0;
    int64 markerRegion = 0;
    int64 a = 0, b = 0;      // a time range (HF); a == b = none
    bool hasRange() const { return b > a; }
};

class WaveTileCache
{
public:
    struct Key
    {
        uint64 region = 0; int64 tile = 0; uint64 zoomBits = 0; int h = 0; int scale100 = 0; int theme = 0; bool focus = true;
        bool operator== (const Key& o) const = default;
    };
    struct KeyHash { size_t operator() (const Key& k) const noexcept { return (size_t) (k.region ^ (uint64) k.tile * 0x9e3779b97f4a7c15ull ^ k.zoomBits * 31 ^ (uint64) (k.h * 131 + k.scale100 * 7 + k.theme * 3 + (k.focus ? 1 : 0))); } };
    static constexpr int kTilePx = 256;
    size_t capacity = 96;
    juce::Image get (const Key& k, const std::function<juce::Image()>& make);
    int hits = 0, misses = 0;
    size_t size() const { return lru.size(); }
    void clear() { lru.clear(); map.clear(); }
private:
    std::list<std::pair<Key, juce::Image>> lru;
    std::unordered_map<Key, std::list<std::pair<Key, juce::Image>>::iterator, KeyHash> map;
};

// draws one tile of a region's waveform: physical columns [col0, col0 + kTilePx) of the region at hfPerPx (logical) and scale
juce::Image renderWaveTile (const SourceAudio& src, const Project& p, const Region& r, double hostRate, int64 col0, double hfPerPhysPx,
                            int heightPx, const Palette& P, bool focus);
// GHOST REPEAT: one tile of what a REPEAT span plays (its repetition pieces of the plan, each through its own warp map, reversed or
// not, scaled by its envelope = gain x fades x decay), physical columns [col0, col0 + kTilePx) from the span start
juce::Image renderRepeatTile (const SourceAudio& src, const WorkingAudio& work, const std::vector<const Piece*>& pieces, double spanStartHf,
                              int64 col0, double hfPerPhysPx, int heightPx, const Palette& P);
uint64 repeatKey (const std::vector<const Piece*>& pieces, int64 repeatId);   // the tile identity: changes with every parameter

class WaveView : public juce::Component, public juce::FileDragAndDropTarget, public juce::TooltipClient, private juce::Timer
{
public:
    explicit WaveView (SliceProcessor&);
    ~WaveView() override;
    void setPalette (const Palette& p) { P = p; tiles.clear(); repaint(); }
    const Palette& pal() const { return P; }
    void setWorkspace (Workspace w) { ws = w; repaint(); }
    Workspace workspace() const { return ws; }
    FlexMode flexMode = FlexMode::Stretch;
    float snapStrength = 1.0f;               // FLEX marker snapping strength (0..1); Cmd/Ctrl = full, Alt = none
    Selection sel;
    // GHOST ALIGN preview (amber ghosts until APPLY) and the project snapshot it was made for
    std::shared_ptr<const align::Proposals> alignPreview;
    std::shared_ptr<const Project> alignPreviewFor;
    // PITCH: the piano-roll range (MIDI, from the detected notes) and the f0 overlay
    struct PitchRange { double lo = 48, hi = 72; };
    PitchRange pitchRange() const;
    float yOfPitch (double midi) const;
    bool showF0 = true;
    // CHORDS (phase 17): the read-only chord lane (the analysis' chord track through each region's map, labels transposed by the
    // global + region PITCH), drawn over the top of the PITCH / EDIT view while on; nothing under it moves
    bool showChords = false;
    bool chordLabels = true;                             // (tests: false = the chips without their text, for the contrast maths)
    static constexpr int kChordLane = 18;
    juce::Rectangle<float> chordLane() const;
    struct ChordChip { juce::Rectangle<float> r; juce::String label; float confidence = 0; bool nc = false; };
    std::vector<ChordChip> chordChips() const;          // what the lane shows now (view coordinates)
    void paintChords (juce::Graphics& g, bool labels = true);
    juce::String getTooltip() override;                  // over the lane: the chord under the mouse
    // PITCH note editing (phase 14): the notes on screen (detected + edited), the selection (region, source start), DRAW mode
    std::vector<pitchedit::NoteRef> pitchNotes() const;
    std::vector<pitchedit::NoteRef> selectedNotes() const;
    std::set<std::pair<int64, int64>> noteSel;
    bool isSelected (const pitchedit::NoteRef& n) const { return noteSel.count ({ n.regionId, n.s0 }) > 0; }
    void selectNotes (const std::vector<pitchedit::NoteRef>& v) { noteSel.clear(); for (auto& n : v) noteSel.insert ({ n.regionId, n.s0 }); repaint(); }
    juce::Rectangle<float> noteRect (const pitchedit::NoteRef& n) const;
    std::optional<pitchedit::NoteRef> noteAt (juce::Point<float> p) const;
    double midiAt (float y) const;
    double outHfOfSrc (const Region& r, double src) const;
    bool pitchDraw = false;
    // actions (buttons / keys / tests); each is one undo step
    bool shiftSelected (double cents, bool snapSemitone);
    bool editSelected (const std::function<void (PitchEdit&)>& f, const juce::String& label);
    bool splitSelected();                     // the (first) selected note at the cursor, or at its middle
    bool mergeSelected();
    bool resetSelected();
    bool snapSelected (const pitchedit::Scale& s, bool all, int* count = nullptr);
    bool drawCurve (const std::vector<juce::Point<float>>& path);   // a PITCH DRAW stroke (view coordinates)
    // GHOST CHOP preview (EDIT): the slice points of the current slice mode (HF on the output timeline)
    std::vector<int64> chopPreview;
    std::function<void (const juce::String&)> toast;
    std::function<void()> selectionChanged;

    // view: [viewStart, viewStart + width * hfPerPx) of the output timeline (HF)
    double viewStart = 0, hfPerPx = 256;
    void zoomAround (double factor, float x);
    void zoomToFit();
    void zoomToSelection();
    void scrollBy (double px);
    float xOf (double hf) const { return (float) ((hf - viewStart) / hfPerPx) + (float) waveArea().getX(); }
    double hfAt (float x) const { return viewStart + ((double) x - waveArea().getX()) * hfPerPx; }
    juce::Rectangle<int> waveArea() const { return getLocalBounds().withTrimmedTop (kRuler); }
    juce::Rectangle<int> ruler() const { return getLocalBounds().withHeight (kRuler); }
    juce::Rectangle<float> regionRect (const Region& r) const;
    const Region* regionAt (juce::Point<float> p) const;   // the topmost region under the point
    static constexpr int kRuler = 22, kBar = 15, kEdge = 5;

    // FLEX helpers
    struct MarkerHit { int64 region = 0, marker = 0; bool anchor = false; };
    MarkerHit markerAt (juce::Point<float> p) const;
    double markerAbsHf (const Region& r, const WarpMarker& w) const;
    double snapHf (double hf, bool full, bool none) const;   // grid snap for EDIT moves (none = Alt)
    double nearOnsetHf (const Region& r, double hf, float px) const;   // FLEX double-click: an onset within px (or hf itself)

    // actions the editor buttons call
    bool cutAtCursorOrSelection();
    bool splitAtMarkers();
    bool deleteSelection();
    bool duplicateSelection();
    bool reverseSelection();
    bool nudge (int dir, bool fine);
    bool addMarkerAtCursor();
    bool removeSelectedMarker();
    bool toggleAnchor();
    bool bpmFromMarkers();                    // the selected marker and the next one (or the two around the cursor)
    const Region* selectedRegion() const { return proc.project().find (sel.regionId); }

    WaveTileCache tiles;
    void paint (juce::Graphics&) override;
    void resized() override { if (firstLayout && getWidth() > 0) { firstLayout = false; zoomToFit(); } }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scale) override;
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dropHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropHover = false; repaint(); }

    // what a drag is doing (tests)
    enum class Drag { None, Range, Loop, Move, Slip, TrimL, TrimR, Stretch, FadeIn, FadeOut, Marker, RulerCursor, RulerLoop,
                      NotePitch, NoteTime, NoteStart, NoteEnd, Band, Draw };
    Drag dragging() const { return drag; }
    juce::Rectangle<float> fadeHandle (const Region& r, bool in) const;
    juce::Rectangle<float> stretchGrip (const Region& r) const;
private:
    void timerCallback() override;
    void paintRegion (juce::Graphics& g, const Region& r, const Project& p, float scale, bool covered);
    void paintGrid (juce::Graphics& g, const Project& p);
    void paintRuler (juce::Graphics& g, const Project& p);
    void paintFlex (juce::Graphics& g, const Region& r, const Project& p);
public:
    void paintRepeats (juce::Graphics& g, const Project& p, float scale);   // the REPEAT spans' own waveform over the regions
    int repeatTilesDrawn = 0;                                                 // (tests: tiles drawn by the last paint)
private:
    void paintPitch (juce::Graphics& g, const Project& p);
    bool applyOp (const std::optional<Project>& q, const juce::String& label, const juce::String& why = {});
    SliceProcessor& proc;
    Palette P = palette (Theme::Dark);
    Workspace ws = Workspace::Edit;
    Drag drag = Drag::None;
    juce::Point<float> down; Project before; bool moved = false, firstLayout = true, dropHover = false;
    double downHf = 0, grabOffset = 0; int64 dragRegion = 0, dragMarker = 0; double dragMarkerT0 = 0;
    int64 lastPlayhead = -1;
    // PITCH drags
    pitchedit::NoteRef dragNote; std::vector<pitchedit::NoteRef> dragNotes; std::set<std::pair<int64, int64>> selBefore;
    juce::Rectangle<float> band; std::vector<juce::Point<float>> drawPath;
    void pitchDown (const juce::MouseEvent& e);
    void pitchDrag (const juce::MouseEvent& e);
    void pitchUp (const juce::MouseEvent& e);
};

class LaneStrip : public juce::Component
{
public:
    explicit LaneStrip (SliceProcessor& p, WaveView& w) : proc (p), view (w) {}
    void setPalette (const Palette& p) { P = p; repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    std::vector<std::pair<int64, juce::Rectangle<float>>> blocks() const;   // (region id, rect) in output order
    int slotAt (float x) const;
    int dragFrom = -1, dragTo = -1;
private:
    SliceProcessor& proc; WaveView& view; Palette P = palette (Theme::Dark);
    float dragX = 0;
};
} // namespace gslice::ui
