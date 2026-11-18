#pragma once
// Ghost Slice window (GENERATE), the 2026-11-02 design (the owner's reference picture, "i need this exactly ui just this color").
// Laid out on a fixed 1477 x 1065 design canvas that is scaled as a whole (the window keeps the picture's proportions):
//   header    the ghost + GHOST SLICE / VOCALS • SOUNDS • INSPIRATION | KEY (AUTO = detected) | TEMPO | the four looks | ⚙ | LICENSED
//             (the ⚙ menu: the RECORD input AUTO / TRACK / MAC MIC, the key lock, TRIM reset, about)
//   SOUND     the file + key | tempo | length, LOAD... / RECORD, the waveform (purple -> cyan, the parts the take used boxed),
//             zoom in / out / fit, the overview strip (drag the view), play the sound, TRIM (the part GENERATE uses).
//             The highlighted parts are editable (a sampler's slice boxes): drag the middle to move, the edges to resize (snapped to
//             zero crossings and the sound's onsets / syllables; Shift = free), Alt-drag duplicates, double-click adds one, right-click:
//             delete / split / duplicate / hear. Letting go re-renders the take from the new parts; a click hears the part (CHOPS:
//             its key flashes). Cmd / Ctrl + Z undoes each edit.
//   TAKE TYPE AUTO / INSTRUMENT / PAD / CHOPS / TEXTURE tiles     CHARACTER [CLEAR]  NATURAL / CHIPMUNK / DEEP ghost tiles
//             (CLEAR: the small pill at the right end of the CHARACTER title row, glowing in the look's cyan when on; also in the ⚙ menu)
//   NATURAL <-> MUSICAL slider, TONE and SPACE knobs, the big GENERATE (progress) and VARY
//   TAKES n   < take cards (name, type, key, mini waveform, star, x on hover) >  PLAY | DRAG WAV | DRAG MIDI | SAVE TAKE
//   keys      C1..C7 (FL names), the scale / take notes lit, VELOCITY meter, SCALE (KEY lock, < A MIN >)
// The four house looks BLACK / PURPLE / LIGHT / GHOST share the layout (SliceTheme.h Skin). Tooltips on everything.
#include "SliceProcessor.h"
#include "SliceTheme.h"
#include "SliceMascot.h"
#include "SliceIcons.h"
#include "GhostGlass.h"
#include "GhostArt.h"
#include <bitset>
#include <optional>
#include <map>

namespace gslice::ui
{
constexpr int kDesignW = 1477, kDesignH = 1065;

struct GenLnF : LnF
{
    Skin S;
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int w, int h) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent) override;
    juce::Font getPopupMenuFont() override { return capFont (9.5f, Weight::Medium); }
};

enum class Icon { None, Folder, Record, Play, Stop, Scissors, ZoomIn, ZoomOut, Fit, Gear, Dice, WaveBars, Notes, Download, ChevL, ChevR, Lock, Unlock, Sparkle, Cloud, Chops, Waves };
void drawIcon (juce::Graphics&, Icon, juce::Rectangle<float>, const Skin&, juce::Colour c, bool gradient);

// one button of the window: a glass button (icon + text), a header pill (LABEL value ⌄), a small icon button
struct GButton : juce::Button
{
    enum class Style { Glass, Pill, Icon };
    GButton (Style s, Icon i, const juce::String& text) : juce::Button (text), style (s), icon (i) {}
    Style style; Icon icon; juce::String label, value; bool lit = false, red = false; float textCap = 10.0f, iconSize = 20.0f; const Skin* S = nullptr;
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

// the SOUND panel: the drop zone, the file, the waveform with its view / zoom / overview, TRIM, the recording
struct Stage : juce::Component, juce::FileDragAndDropTarget, juce::TextDragAndDropTarget, juce::SettableTooltipClient
{
    static constexpr const char* kBoxTip = "Drag a highlighted part to move it, drag its edges to resize it; the take rebuilds from the new parts. Double-click to add one, right-click for more.";
    explicit Stage (SliceProcessor& p);
    SliceProcessor& proc; Palette P; const Skin* S = nullptr; bool dragOver = false;
    std::function<void (const juce::File&)> onFile;
    GButton loadBtn { GButton::Style::Glass, Icon::Folder, "LOAD..." }, recordBtn { GButton::Style::Glass, Icon::Record, "RECORD" };
    GButton zoomInBtn { GButton::Style::Icon, Icon::ZoomIn, "zoom in" }, zoomOutBtn { GButton::Style::Icon, Icon::ZoomOut, "zoom out" }, fitBtn { GButton::Style::Icon, Icon::Fit, "fit" };
    GButton playSrcBtn { GButton::Style::Glass, Icon::Play, "" }, trimBtn { GButton::Style::Glass, Icon::Scissors, "TRIM" };
    double v0 = 0.0, v1 = 1.0;               // the view (0..1 of the sound)
    bool trimMode = false;
    double dragT0 = 0, dragT1 = 1;           // the TRIM handles while dragging
    // the parts (boxes): what is under a point (what: 0 nothing, 1 the left edge, 2 the right edge, 3 the middle)
    struct BoxHit { int box = -1; int what = 0; };
    BoxHit hitBox (juce::Point<float> p) const;
    std::vector<std::pair<double, double>> editBoxes;   // during a box drag: the boxes as they will be (seconds)
    int editBox = -1, editWhat = 0, hoverBox = -1; bool editDup = false, dragMoved = false; double grabSec = 0; std::pair<double, double> grabBox;
    std::function<void (int)> onHear;        // a part was heard (the editor flashes its key for CHOPS)
    void hearBox (int i);
    void boxMenu (int i, double atSec);
    bool addBoxAt (double sec);              // double-click: a new part around sec (false: no room / the type's limit)
    double snapSec (double sec, bool free, double pxTol = 6.0) const;   // onsets / syllables within a few pixels, then a zero crossing
    static constexpr double kMinPart = 0.06; // seconds
    juce::String getTooltip() override { return hoverBox >= 0 || dragWhat == 7 ? juce::String (kBoxTip) : SettableTooltipClient::getTooltip(); }
    void zoomBy (double factor);              // < 1 zooms in (around the middle of the view)
    void fitAll() { v0 = 0.0; v1 = 1.0; repaint(); }
    juce::Rectangle<float> waveArea() const;  // (local)
    juce::Rectangle<float> overviewArea() const;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    bool isInterestedInTextDrag (const juce::String& t) override;
    void textDropped (const juce::String& t, int x, int y) override;
    void textDragEnter (const juce::String&, int, int) override { dragOver = true; repaint(); }
    void textDragExit (const juce::String&) override { dragOver = false; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { if (hoverBox >= 0) { hoverBox = -1; repaint(); } }
    void sourceChanged() { v0 = 0.0; v1 = 1.0; trimMode = false; cacheKey = {}; repaint(); }
    int dragWhat = 0; double dragAnchor = 0, dragV0 = 0, dragV1 = 0;
    const void* shownSrc = nullptr;
    // the waveform outline, made once per (sound, view, width)
    struct Key { const void* src = nullptr; double v0 = -1, v1 = -1; int w = 0; bool operator== (const Key& o) const { return src == o.src && v0 == o.v0 && v1 == o.v1 && w == o.w; } };
    Key cacheKey; juce::Path wavePath, corePath; const void* overKey = nullptr; juce::Path overPath;
    void paintWave (juce::Graphics&, const SourceAudio&);
    void paintOverview (juce::Graphics&, const SourceAudio&);
    double xToFrac (float x) const;
    float fracToX (double f) const;
};

// the tiles of TAKE TYPE / CHARACTER: an icon, a name, a caption; the selected one glows
struct TileGroup : juce::Component, juce::TooltipClient
{
    struct Item { juce::String name, caption, tip; Icon icon = Icon::None; int ghost = -1; };
    std::vector<Item> items; int sel = 0, hover = -1; float tileW = 117, gap = 12, pad = 8, nameCap = 10.5f, capCap = 7.5f; const Skin* S = nullptr;
    std::function<void (int)> onChange;
    juce::String getTooltip() override { const int i = hover >= 0 ? hover : sel; return i >= 0 && i < (int) items.size() ? items[(size_t) i].tip : juce::String(); }
    juce::Rectangle<float> tile (int i) const { return { pad + (float) i * (tileW + gap), pad, tileW, (float) getHeight() - pad * 2.0f }; }
    int at (juce::Point<int> p) const { for (int i = 0; i < (int) items.size(); ++i) if (tile (i).contains (p.toFloat())) return i; return -1; }
    void set (int i) { if (i != sel) { sel = i; repaint(); } }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { const int i = at (e.getPosition()); if (i >= 0 && i != sel) { sel = i; repaint(); if (onChange) onChange (i); } }
    void mouseMove (const juce::MouseEvent& e) override { const int i = at (e.getPosition()); if (i != hover) { hover = i; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
};

// BLACK / PURPLE / LIGHT / GHOST: four swatch chips, the active one glows
struct LookChips : juce::Component, juce::TooltipClient
{
    int sel = 1, hover = -1; const Skin* S = nullptr; std::function<void (int)> onChange;
    juce::Rectangle<float> chip (int i) const { const float w = (float) getWidth() / 4.0f; return { w * (float) i + w * 0.5f - 26.0f, 8.0f, 52.0f, 33.0f }; }
    int at (juce::Point<int> p) const { const int i = p.x * 4 / juce::jmax (1, getWidth()); return juce::jlimit (0, 3, i); }
    juce::String getTooltip() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { const int i = at (e.getPosition()); if (onChange) onChange (i); }
    void mouseMove (const juce::MouseEvent& e) override { const int i = at (e.getPosition()); if (i != hover) { hover = i; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
};

// CLEAR: a small pill toggle (a dot + CLEAR), lit in the look's cyan when on, dim when off
struct ClearPill : juce::Button
{
    ClearPill() : juce::Button ("CLEAR") {}
    bool on = true; const Skin* S = nullptr;
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

// the big GENERATE pill (the worker's progress fills it)
struct GenerateButton : juce::Button
{
    explicit GenerateButton (SliceProcessor& p) : juce::Button ("GENERATE"), proc (p) {}
    SliceProcessor& proc; Palette P; const Skin* S = nullptr; float phase = 0;
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

// the takes strip: cards (name, type, key, mini waveform, star, x on hover), click = pick it, right-click = menu
struct TakesStrip : juce::Component, juce::TooltipClient
{
    explicit TakesStrip (SliceProcessor& p) : proc (p) {}
    SliceProcessor& proc; Palette P; const Skin* S = nullptr; int first = 0; juce::Point<int> mouse { -1, -1 };
    struct Hit { juce::Rectangle<int> chip, star, del; int index; };
    std::vector<Hit> hits;
    std::map<const void*, std::vector<float>> minis;   // a take's mini waveform (its preview's peaks)
    juce::String getTooltip() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent& e) override { mouse = e.getPosition(); repaint(); }
    void mouseExit (const juce::MouseEvent&) override { mouse = { -1, -1 }; repaint(); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { first = juce::jlimit (0, juce::jmax (0, (int) proc.takes().size() - visibleCount()), first + (w.deltaY < 0 ? 1 : -1)); repaint(); }
    void showCurrent();                      // scroll so the current take is in view
    int visibleCount() const { return 4; }
    const std::vector<float>& miniOf (const gen::Take&);
};

// the big icon tiles under TAKES: PLAY (glows), SAVE TAKE
struct IconTile : juce::Button
{
    IconTile (Icon i, const juce::String& t) : juce::Button (t), icon (i) {}
    Icon icon; bool accent = false; const Skin* S = nullptr;
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

// a tile you drag out (DRAG WAV / DRAG MIDI): the drag starts the export (the demo gate asks first)
struct DragPill : juce::Component, juce::SettableTooltipClient
{
    juce::String text; Icon icon = Icon::WaveBars; Palette P; const Skin* S = nullptr; bool over = false, started = false; std::function<void()> onDragOut;
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { over = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { over = false; repaint(); }
    void mouseDown (const juce::MouseEvent&) override { started = false; }
    void mouseDrag (const juce::MouseEvent& e) override { if (! started && e.getDistanceFromDragStart() > 4) { started = true; startDrag(); } }
    void startDrag() { if (onDragOut) onDragOut(); }   // (tests call this: the same path a real drag takes)
};

// the keyboard: C1..E7 (FL names), the take's notes / the scale lit purple -> cyan, keys playable
struct GhostKeys : juce::MidiKeyboardComponent
{
    GhostKeys (juce::MidiKeyboardState& s) : juce::MidiKeyboardComponent (s, juce::MidiKeyboardComponent::horizontalKeyboard) {}
    const Skin* S = nullptr; std::bitset<128> lit, scale;
    void drawWhiteNote (int note, juce::Graphics&, juce::Rectangle<float> area, bool down, bool over, juce::Colour, juce::Colour) override;
    void drawBlackNote (int note, juce::Graphics&, juce::Rectangle<float> area, bool down, bool over, juce::Colour) override;
    int flash = -1;                          // a key flashed (a CHOPS part was heard)
    juce::String getWhiteNoteText (int note) override { return note % 12 == 0 ? "C" + juce::String (note / 12 - 5 + getOctaveForMiddleC()) : juce::String(); }
    juce::Colour litColour (int note) const;
};

// VELOCITY: the last note's velocity as a vertical bar of segments
struct VelocityMeter : juce::Component, juce::SettableTooltipClient
{
    float value = 0.0f; const Skin* S = nullptr;
    void paint (juce::Graphics&) override;
};

// SCALE: KEY + lock, < A MIN > (cycles the key)
struct ScaleBox : juce::Component
{
    ScaleBox();
    const Skin* S = nullptr; juce::String keyText;
    GButton lockBtn { GButton::Style::Icon, Icon::Unlock, "lock" }, prevBtn { GButton::Style::Icon, Icon::ChevL, "prev" }, nextBtn { GButton::Style::Icon, Icon::ChevR, "next" };
    void paint (juce::Graphics&) override;
    void resized() override;
};

// a message that fades in over the SOUND panel for a few seconds
struct Toast : juce::Component
{
    juce::String text; const Skin* S = nullptr;
    Toast() { setInterceptsMouseClicks (false, false); }
    void paint (juce::Graphics&) override;
};

// build 6: a small pill in a panel's title row: MELODY  HOOK ⌄ (a menu) / UNDER THE HOOD › (opens the second page)
struct TitlePill : juce::Button
{
    TitlePill (const juce::String& n) : juce::Button (n) {}
    juce::String label, value; bool chevron = false, menu = false; const Skin* S = nullptr;
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

// build 6: UNDER THE HOOD, the second page of a take type: what it is actually doing. Over the TAKE TYPE .. TAKES area (GENERATE,
// VARY, PLAY and the keys stay usable): the take's source parts (the boxes on the waveform above, numbered the same; a click hears
// one), the processing chain as a flow (VOICE PARTS -> RESYNTH -> PITCH -> ...), a mini piano roll of the melody / chords / phrase
// with the chord names and the play head, live level meters, and the type's deeper knobs (GRAIN SIZE, FORMANT, SHIMMER, ...) +
// the melody style. BACK returns to the main page (which stays exactly the picture).
// build 8: a row of choices in the window's look (ArpEZator's segmented control, in Ghost Slice's colours); momentary = buttons
struct PlaySeg : juce::Component, juce::SettableTooltipClient
{
    PlaySeg (juce::StringArray it = {}) : items (std::move (it)) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    juce::StringArray items; int sel = 0, hover = -1; bool momentary = false; float cap = 7.4f; const Skin* S = nullptr;
    std::function<void (int)> onChange;
    juce::Rectangle<float> cell (int i) const;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent&) override { if (momentary) { pressed = -1; repaint(); } }
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    int pressed = -1;
};

// build 8: the MELODY roll of UNDER THE HOOD, editable. Drag a note up / down (it only lands on notes of the KEY) and left / right
// (onto the GRID), drag its right end to make it longer / shorter, double-click empty space to add a note, double-click a note (or
// DELETE) to delete it, drag on empty space to select several (SHIFT adds), arrow keys move the selection (up / down = the next note
// of the key, left / right = one grid step), Cmd+A selects all. A click on a chord name swaps that chord (ArpEZator's way). Every edit
// re-renders the take off the audio thread (the old one keeps playing), is one undo step and is saved with the session.
struct NoteRoll : juce::Component, juce::TooltipClient
{
    explicit NoteRoll (SliceProcessor& p) : proc (p) { setWantsKeyboardFocus (true); setMouseClickGrabsKeyboardFocus (true); }
    SliceProcessor& proc; const Skin* S = nullptr;
    struct Geo
    {
        juce::Rectangle<float> roll; double total = 16; int lo = 48, hi = 72; float chordH = 0;
        float rowH() const { return roll.getHeight() / (float) (hi - lo + 1); }
        float x (double b) const { return roll.getX() + (float) (b / total) * roll.getWidth(); }
        double beat (float px) const { return (px - roll.getX()) / juce::jmax (1.0f, roll.getWidth()) * total; }
        float y (int n) const { return roll.getBottom() - (float) (n - lo + 1) * rowH(); }
        int note (float py) const { return lo + (int) std::floor ((roll.getBottom() - py) / juce::jmax (1.0f, rowH())); }
        juce::Rectangle<float> chordLane() const { return { roll.getX(), roll.getBottom() + 3.0f, roll.getWidth(), juce::jmax (0.0f, chordH - 5.0f) }; }
    };
    Geo geo; bool dragging = false;
    std::vector<gslice::gen::NoteEvent> notes;     // what the roll shows (the take's notes, or your edit while the take re-renders)
    std::vector<bool> sel;
    const void* shownTake = nullptr; const void* waitingFor = nullptr;   // the take the roll shows / the one an edit is replacing
    std::vector<std::pair<double, int>> keepSel;   // (beat, note) of the notes to select once the new take arrives
    struct Drag { int kind = 0; /*1 move 2 resize 3 band*/ juce::Point<float> p0; int anchor = -1; bool moved = false; juce::Rectangle<float> band; } dr;
    std::vector<gslice::gen::NoteEvent> base;      // the notes when the drag started
    int hover = -1; bool hoverEdge = false;
    void sync();                                   // the take's notes (unless an edit is waiting); called by the page's tick
    gslice::gen::Key key() const;
    bool canEdit() const;                          // a build 6+ take (an older one: VARY it to edit)
    bool mono() const;                             // one line (INSTRUMENT, CHOPS)
    int grid() const { return proc.takeEdit().grid; }
    int selectedCount() const { int n = 0; for (bool b : sel) n += b ? 1 : 0; return n; }
    // one edit: ops = (index in `notes` or -1 for a new one, the note as it becomes or nothing = deleted)
    using Op = std::pair<int, std::optional<gslice::gen::NoteEvent>>;
    bool commit (const std::vector<Op>& ops, const juce::String& what);
    bool deleteSelected();
    bool addAt (double beat, int note);
    bool nudge (double dBeat, int steps);           // the selection: time (beats) and key steps
    bool setLink (int type, float amount = -1);      // GLIDE / FATTEN / STEP from each selected note into the next
    bool resetNotes();                               // your notes gone: the take's own again
    void chordMenu (int slot);
    bool swapChord (int slot, int code);             // (the chord menu, tests) code -1 = the style's chord
    static int stepInKey (int note, int steps, const gslice::gen::Key& k);   // the note `steps` notes of the key up / down
    int hitNote (juce::Point<float> p) const;
    bool onRightEdge (int i, juce::Point<float> p) const;
    int chordSlotAt (juce::Point<float> p) const;   // -1 = not on the chord lane
    std::function<void()> onSelectionChanged;
    juce::String getTooltip() override;
    void layoutGeo();
    void paint (juce::Graphics&) override;
    void resized() override { layoutGeo(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
};

// build 8: PLAY IT, the second tab of UNDER THE HOOD's right side: ArpEZator's controls for this take. MASTER (simple <-> wild),
// CHORDS (AS WRITTEN / SIMPLE / RICH), the ARP (pattern, RATE, OCTAVES) and the LINE between two notes (STEP / GLIDE / FATTEN + how much)
struct PlayPanel : juce::Component, juce::TooltipClient
{
    explicit PlayPanel (SliceProcessor& p, NoteRoll& r);
    SliceProcessor& proc; NoteRoll& roll; const Skin* S = nullptr;
    juce::Slider master { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox }, lineAmt { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Slider bed { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };   // build 9: CHORD BED (INSTRUMENT), 0 = off
    PlaySeg chords { { "AS WRITTEN", "SIMPLE", "RICH" } }, rate { { "1/8", "1/16" } }, octaves { { "1", "2", "3" } }, line { { "STEP", "GLIDE", "FATTEN" } };
    TitlePill arpBtn { "ARP" };
    bool refreshing = false;
    void refresh();                                 // the controls from the take on screen
    void setSkin (const Skin* s);
    juce::String getTooltip() override;
    void paint (juce::Graphics&) override;
    void resized() override;
};

struct HoodPage : juce::Component, juce::TooltipClient
{
    explicit HoodPage (SliceProcessor& p);
    SliceProcessor& proc; const Skin* S = nullptr;
    GButton backBtn { GButton::Style::Glass, Icon::ChevL, "BACK" };
    TitlePill styleBtn { "MELODY" };
    static constexpr int kDeepShown = gslice::gen::Recipe::kDeep;
    juce::Slider deep[kDeepShown];
    std::function<void()> onBack, onStyleMenu;
    std::function<void (int)> onHearPart;
    float meterL = 0, meterR = 0, flow = 0;
    void refresh();                          // the knobs from the processor (the type they speak for)
    void tick();                             // 30 Hz: meters, the play head, the flow's glow
    juce::Rectangle<float> chainArea() const, rollArea() const, meterArea() const, partsArea() const;
    juce::Rectangle<float> partChip (int i) const;
    std::vector<juce::Rectangle<float>> chainBoxes;   // (painted; tests read them)
    int hoverPart = -1; bool refreshing = false;
    // build 8: the editable roll, its tools (GRID, UNDO, REDO, RESET), the right side's tabs SHAPE IT | PLAY IT
    NoteRoll roll { proc };
    PlaySeg gridSeg { { "1/4", "1/8", "1/16", "1/32" } }, histSeg { { "UNDO", "REDO", "RESET NOTES" } }, tabs { { "SHAPE IT", "PLAY IT" } };
    PlayPanel playPanel { proc, roll };
    int tab = 0;                             // 0 SHAPE IT (the type's knobs), 1 PLAY IT
    void setTab (int t);
    void setSkin (const Skin* s);
    juce::Rectangle<float> toolsArea() const;
    juce::String getTooltip() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hoverPart = -1; repaint(); }
};

// build 6: the STYLE browser (a preset browser for the melody brain): every style as a tile, grouped by where it came from (GHOST
// SLICE, PIANO, REVERIE, SYNTH, LEARNED, ARPEZATOR, HOOK LAB, TRAP, HIP HOP), a search box, the group chips, one plain line about the
// style under the pointer. A click picks it and plays two bars of it on a soft tone at once; DONE closes (the take on screen is then
// remade in the style). Over the TAKE TYPE .. TAKES area like UNDER THE HOOD.
struct StyleBrowser : juce::Component, juce::TooltipClient
{
    explicit StyleBrowser (SliceProcessor& p);
    SliceProcessor& proc; const Skin* S = nullptr;
    juce::TextEditor search; GButton doneBtn { GButton::Style::Glass, Icon::None, "DONE" };
    juce::String group = "ALL"; int hover = -1;
    std::vector<gslice::gen::Style> shown;   // (after the search and the group)
    std::vector<juce::Rectangle<float>> tiles, chips;
    std::function<void()> onDone; std::function<void (gslice::gen::Style)> onPick;
    static juce::StringArray groups();
    void rebuild();
    juce::String getTooltip() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
};

// a see-through layer that only paints (the knob words over the knobs)
struct PaintLayer : juce::Component { std::function<void (juce::Graphics&)> paintFn; void paint (juce::Graphics& g) override { if (paintFn) paintFn (g); } };

// the design canvas: everything sits on it in the picture's own pixels; the editor scales it
struct Canvas : juce::Component
{
    std::function<void (juce::Graphics&)> paintStatic, paintLive;
    juce::Image cache; float cacheScale = 0; int cacheLook = -1; int look = 1;
    void invalidate() { cache = {}; repaint(); }
    void paint (juce::Graphics& g) override;
};
} // namespace gslice::ui

class SliceEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SliceEditor (SliceProcessor&);
    ~SliceEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    enum { kLookBlack = 0, kLookPurple = 1, kLookLight = 2, kLookGhost = 3, kLooks = 4 };
    void setLook (int look);
    int look() const { return shownLook; }
    static int nextLook (int l) { return (l + 1) % kLooks; }
    static juce::String lookName (int l) { return l == kLookGhost ? "GHOST" : ghosty::themes::displayName (ghosty::themes::fromInt (l)); }
    void refresh();                          // the controls from the processor
    void toast (const juce::String& s);

    // the export paths (each asks the demo gate first)
    void dragWavOut();
    void dragMidiOut();
    void saveTakeClicked();
    void loadClicked();
    void recordClicked();
    void keyMenu();
    void tempoMenu();
    void settingsMenu();
    void stepKey (int d);                    // SCALE < >: the next / previous key (same mode)
    void toggleClear();                      // CLEAR on / off (the next takes; a toast says to press GENERATE)
    bool keyPressed (const juce::KeyPress& k) override;   // Cmd / Ctrl + Z: undo the last part edit
    double flashUntil = 0;

    SliceProcessor& proc;
    gslice::ui::GenLnF lnf;
    gslice::ui::Palette P;
    gslice::ui::Skin S;
    gslice::ui::Canvas canvas;
    gslice::ui::Stage stage { proc };
    gslice::ui::TileGroup typeTiles, charTiles;
    gslice::ui::ClearPill clearPill;
    juce::Slider musicalSl { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Slider toneKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox }, spaceKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    // build 6: the third knob (VOICE / DREAM / STUTTER / DRIFT by type), MELODY (the style), UNDER THE HOOD (the second page)
    juce::Slider typeKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    gslice::ui::TitlePill melodyBtn { "STYLE" }, hoodBtn { "UNDER THE HOOD" }, lengthBtn { "LENGTH" };
    gslice::ui::GButton stylePrev { gslice::ui::GButton::Style::Icon, gslice::ui::Icon::ChevL, "previous style" }, styleNext { gslice::ui::GButton::Style::Icon, gslice::ui::Icon::ChevR, "next style" };
    gslice::ui::StyleBrowser browser { proc };
    void showBrowser (bool on);
    bool browserShown() const { return browser.isVisible(); }
    void lengthMenu();
    gslice::gen::Style styleOnOpen = gslice::gen::Style::Auto;
    gslice::ui::HoodPage hood { proc };
    void showHood (bool on);
    bool hoodShown() const { return hood.isVisible(); }
    void styleMenu (juce::Component* target);
    gslice::ui::GenerateButton generateBtn { proc };
    gslice::ui::GButton varyBtn { gslice::ui::GButton::Style::Glass, gslice::ui::Icon::Dice, "VARY" };
    gslice::ui::TakesStrip strip { proc };
    gslice::ui::GButton prevBtn { gslice::ui::GButton::Style::Glass, gslice::ui::Icon::ChevL, "" }, nextBtn { gslice::ui::GButton::Style::Glass, gslice::ui::Icon::ChevR, "" };
    gslice::ui::IconTile playBtn { gslice::ui::Icon::Play, "PLAY" }, saveBtn { gslice::ui::Icon::Download, "SAVE TAKE" };
    gslice::ui::DragPill dragWav, dragMidi;
    gslice::ui::GButton keyBtn { gslice::ui::GButton::Style::Pill, gslice::ui::Icon::None, "KEY" }, tempoBtn { gslice::ui::GButton::Style::Pill, gslice::ui::Icon::None, "TEMPO" };
    gslice::ui::GButton gearBtn { gslice::ui::GButton::Style::Glass, gslice::ui::Icon::Gear, "" };
    gslice::ui::LookChips looks;
    ghosty::UnlockButton unlock { proc.license };
    gslice::ui::GhostKeys keys { proc.keyboard };
    gslice::ui::VelocityMeter velocity;
    gslice::ui::ScaleBox scaleBox;
    gslice::ui::Toast toastView;
    gslice::ui::PaintLayer knobLabels;
    juce::String toastText; double toastUntil = 0;
    juce::String statusText() const;
    juce::String infoText() const;
    juce::String keyText() const;            // "A MIN" (the key the next take uses)
    float viewScale() const { return scale; }
private:
    void timerCallback() override;
    void paintStatic (juce::Graphics&);
    void paintLive (juce::Graphics&);
    void paintKnobLabels (juce::Graphics&);
    void updateKeysLit();
    int shownLook = kLookPurple;
    bool refreshing = false;
    float scale = 1.0f;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tips { this, 500 };
    bool wasBusy = false, wasRec = false, wasSrcPlaying = false;
    int lastNoteOns = 0; float velShown = 0;
    const void* litFor = nullptr; int litKey = -99;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SliceEditor)
};
