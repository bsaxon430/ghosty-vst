#pragma once
// ArpEZator window: pick a MODE, GENERATE, drag the MIDI out. Lanes (CHORDS / ARP / MELODY / BASS) each with a big
// DRAG handle; the selected lane opens in the editor (tweak buttons, pattern editor, piano roll); everything else
// lives under MORE. Same layout in every mode; colours and art crossfade with the mode.
#include "ArpProcessor.h"
#include "WaveRegion.h"
#include "RollLogic.h"
#include "ArpArt.h"
#include "GhostArt.h"
#include "TypeIn.h"

struct ArpEditorTestAccess;   // the demo export gate test (tests/DemoGateTests.inc) drives the drag pills
namespace arpui
{
class ArpEditorBase;   // fwd
struct ThemeRef { Palette p; int mode = 2; };

// ---------------------------------------------------------------- small widgets (draw with the live theme)
struct Btn : juce::Component, juce::SettableTooltipClient
{
    ThemeRef& th; juce::String text; std::function<Colour()> tint; bool toggle = false, on = false, big = false, dashed = false; int icon = 0;   // 1 undo 2 redo 3 save 4 load 5 speaker 6 speaker muted
    std::function<void()> onClick; std::function<void()> onRight;
    Btn (ThemeRef& t, juce::String s, std::function<Colour()> c = nullptr) : th (t), text (std::move (s)), tint (std::move (c)) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics& g) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
};
struct Seg : juce::Component, juce::SettableTooltipClient
{
    ThemeRef& th; juce::StringArray items; int sel = 0; std::function<Colour()> tint; std::function<void (int)> onChange; float fontH = 11.5f;
    Seg (ThemeRef& t, juce::StringArray it, std::function<Colour()> c = nullptr) : th (t), items (std::move (it)), tint (std::move (c)) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics& g) override;
    void mouseUp (const juce::MouseEvent& e) override;
};
// a small number with - / + (click the ends, wheel, double-click = reset): EUCLID hits, ROTATE
struct Stepper : juce::Component, juce::SettableTooltipClient
{
    ThemeRef& th; juce::String label; int value = 0, lo = 0, hi = 16, reset = 0; std::function<juce::String (int)> fmt; std::function<void (int)> onChange; std::function<Colour()> tint;
    Stepper (ThemeRef& t, juce::String l, int lo_, int hi_) : th (t), label (std::move (l)), lo (lo_), hi (hi_) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void set (int v) { v = juce::jlimit (lo, hi, v); if (v != value) { value = v; repaint(); if (onChange) onChange (v); } }
    void paint (juce::Graphics& g) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent&) override { set (reset); }
    std::function<void()> onRight;   // right-click (type a value)
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { if (std::abs (w.deltaY) > 0.0001f) set (value + (w.deltaY > 0 ? 1 : -1)); }
};
class ArpLnF : public juce::LookAndFeel_V4
{
public:
    explicit ArpLnF (ThemeRef& t) : th (t) {}
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minP, float maxP, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics& g, const juce::Path& path, juce::Image&) override   // opaque (type-in boxes)
    { g.setColour (th.p.card); g.fillPath (path); g.setColour (th.p.a1); g.strokePath (path, juce::PathStrokeType (1.5f)); }
    juce::Font getPopupMenuFont() override { return ghosty::font (14.0f, false); }
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;   // GHOST: glass cards (other looks: LookAndFeel_V4)
    void drawTooltip (juce::Graphics&, const juce::String& text, int w, int h) override;
    juce::Label* createSliderTextBox (juce::Slider& s) override { auto* l = LookAndFeel_V4::createSliderTextBox (s); l->setFont (ghosty::font (10.0f)); return l; }
    void refresh();
    ThemeRef& th;
};
struct Knob : juce::Component
{
    ThemeRef& th; juce::Slider s { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox }; juce::String label; std::function<void()> onGesture;   // one undo step per drag
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    Knob (ThemeRef& t, juce::AudioProcessorValueTreeState& st, const juce::String& id, juce::String lab);
    void resized() override { auto r = getLocalBounds(); r.removeFromBottom (14); s.setBounds (r); }
    void paint (juce::Graphics& g) override;
};

// ---------------------------------------------------------------- the editor proper
class ArpEditor;
struct LaneCard : juce::Component
{
    ArpEditor& ed; int lane; Btn mute, drag; std::unique_ptr<Seg> simpleSeg;   // CHORDS: SIMPLE | RICH
    juce::Slider vol { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox }; std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volAtt;
    LaneCard (ArpEditor& e, int l);
    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent& e) override;
};
struct DragPill : juce::Component, juce::SettableTooltipClient
{
    ArpEditor& ed; int mask; juce::String text; bool dragging = false; bool huge = false; bool audio = false;   // audio: renders WAV (Alt on a lane pill does too)
    DragPill (ArpEditor& e, int m, juce::String t) : ed (e), mask (m), text (std::move (t)) { setMouseCursor (juce::MouseCursor::DraggingHandCursor); }
    void paint (juce::Graphics& g) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
};
struct PatternEd : juce::Component
{
    ArpEditor& ed; PatternEd (ArpEditor& e) : ed (e) {}
    void paint (juce::Graphics& g) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;
    juce::Rectangle<float> box (int i, int len) const;
};
struct ChordChips : juce::Component
{
    ArpEditor& ed; ChordChips (ArpEditor& e) : ed (e) {}
    void paint (juce::Graphics& g) override;
    void mouseUp (const juce::MouseEvent& e) override;
    int slotAt (float x) const;
};
struct Roll : juce::Component, juce::SettableTooltipClient
{
    ArpEditor& ed; Roll (ArpEditor& e) : ed (e) { setWantsKeyboardFocus (true); }
    arp::RollView view;   // the visible time window (ticks)
    int lo = 60, hi = 84; bool frozen = false, pitchManual = false, denseNow = false; int selLink = -1, hoverLink = -1; bool hoverHandle = false; std::vector<bool> sel; int selBar0 = -1, selBar1 = -1;
    int audPitch = -1;   // a key / note being auditioned
    std::vector<float> drawE; bool drawingE = false;   // the ENERGY lane being drawn (committed on mouse up)
    juce::Rectangle<float> energyStrip() const;          // the bottom strip of the ARP roll while GROW / WAVE / JOURNEY is on (empty otherwise)
    bool energyDraw (const juce::MouseEvent& e);
    void setCursorAt (float x, float y);                 // PLAY FROM THE CURSOR: a click sets where SPACE starts
    void transposeSelected (int semis);                  // UP / DOWN (+ SHIFT = octave) on the selected notes
    int secMode = 0, secS0 = 0, secE0 = 0, secT0 = 0;    // LOOP SECTION brace drag: 1 new, 2 start, 3 end, 4 move
    int snapSec (double t, const juce::ModifierKeys& m) const;   // 1 beat (SHIFT = the lane's grid, ALT = whole bars)
    bool laneMutedNow = false; juce::Rectangle<float> bannerUnmute;   // the "lane is muted" bar + its UNMUTE button
    void unmuteLane();
    bool glideHintShown = false;
    struct Drag { int kind = 0; /*1 note 2 link handle 3 band 4 empty 5 keys 6 pan*/ int idx = -1; int t0 = 0, p0 = 0; float x0 = 0, y0 = 0; bool moved = false, undo = false; arp::Link l0; juce::Rectangle<float> band; double vs0 = 0; int lo0 = 0; } dr;
    // rows: chord names [0, hy1), bar numbers + padlocks [hy1, hy2), the TIMELINE strip [hy2, hy) (right-click = play from here,
    // right-drag = loop, the loop's grips drag out MIDI / WAV), the notes below hy
    static constexpr float kx = 34, hy1 = 16, hy2 = 34, hy = 48;
    bool inStrip (juce::Point<float> p) const { return p.y >= hy2 && p.y < hy && p.x >= kx; }
    bool inPadlock (juce::Point<float> p) const;          // the padlock of the bar under p (bar-number row)
    juce::Rectangle<float> gripRect (bool audio) const;   // the loop's MIDI / WAV drag-out grips (empty without a loop)
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;
    void mouseMagnify (const juce::MouseEvent& e, float scale) override;
    bool keyPressed (const juce::KeyPress& k) override;
    int total() const;
    int viewBars() const { return juce::jmax (1, (int) std::lround (view.len / arp::BAR)); }
    void zoomAround (float x, double factor);
    void setView (arp::RollView v) { view = arp::clampView (v, total()); repaint(); }
    float X (double t) const { return kx + (float) ((t - view.start) / view.len) * ((float) getWidth() - kx); }
    double T (float x) const { return view.start + (x - kx) / ((float) getWidth() - kx) * view.len; }
    float rh() const { return ((float) getHeight() - hy) / (float) (hi - lo + 1); }
    float Y (int p) const { return hy + (float) (hi - p) * rh(); }
    int P (float y) const { return hi - (int) std::floor ((y - hy) / rh()); }
    void fitRows (const std::vector<arp::Note>& notes);
    int hitNote (const std::vector<arp::Note>& n, float x, float y) const;
    int hitLink (const std::vector<arp::Note>& n, float x, float y) const;   // anywhere on the line (9 px around it)
    bool hitHandle (const std::vector<arp::Note>& n, int i, float x, float y) const;
    std::vector<juce::Point<float>> linkPath (const std::vector<arp::Note>& n, int i) const;
    juce::Point<float> linkHandle (const std::vector<arp::Note>& n, int i) const;   // GLIDE dot / FATTEN ball (never under a note)
    void noteMenu (int idx);
    void noteAction (int idx, int r);   // a note-menu item (1 lock, 2 delete, 3 louder, 4 softer, 5 longer, 6 shorter, 7 / 8 octave up / down, 9 unlock all, 20-23 transpose selected)
    void chordMenu (int slot);
    void rollMenu();
    void audition (int pitch);   // -1 = stop
    void clearSel() { sel.clear(); selBar0 = selBar1 = -1; selLink = -1; }
};
struct MorePanel : juce::Component
{
    ArpEditor& ed; MorePanel (ArpEditor& e);
    juce::TextEditor words; juce::OwnedArray<Btn> chips; juce::Slider master, groove, colour, gate, stutterS;
    Seg bars, resync, evolveLen, phrase, melRate, reg, keySeg;
    Btn hold, pushB, listenB, listenFile, savePre, loadPre, starters, midiOut, ch1B, velB, bendB, oneTrack, follow, close, genWords; Seg bendSeg;
    juce::Slider darkS; juce::ComboBox key, scale, flavourBox; std::unique_ptr<Knob> vol, verb;   // flavourBox: TRAP / HIP HOP FLAVOUR (AUTO + five)
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> midiAtt;
    // LANES: each lane's own mood / variation (ARP, MELODY) and its own FX (DRIVE, TONE, ECHO, SPACE) - added below the left columns
    juce::OwnedArray<Seg> laneMoodS; juce::OwnedArray<Btn> laneVarB; juce::OwnedArray<juce::Slider> laneFxS; juce::Rectangle<int> laneBox;
    void paint (juce::Graphics& g) override;
    void resized() override;
    void sync();
};

// arp-vs-progression phrase alignment: figure starts (ticks) against chord changes (bars) over the loop
struct AlignStrip : juce::Component, juce::SettableTooltipClient
{
    ArpEditor& ed; explicit AlignStrip (ArpEditor& e) : ed (e) { setTooltip ("Where the arp figure starts again (ticks) against the chord changes (bars). Gold = they meet: the phrase lines up with the progression"); }
    void paint (juce::Graphics& g) override;
};
// HOOK LAB: 16 hook tiles over the current chords, audition on click, stars, FIND THE MAGIC / MAKE IT A SONG / COMBINE / USE THIS.
// Keys: arrows move, space plays / stops, 1-9 / 0 play a tile, S stars, M finds the magic, Enter = USE THIS. Drag a tile out = its .mid
struct HookLabPanel : juce::Component, juce::SettableTooltipClient, private juce::Timer
{
    ArpEditor& ed; explicit HookLabPanel (ArpEditor& e);
    Seg barsSeg, styleSeg, candySeg, songSeg, regSeg, meterSeg, feelSeg; Stepper euclidS, rotS;
    Btn genB, magicB, songB, combineB, useB, respondB, motifB, harvestB, close, pianoB, autoB, prevB, nextB, cancelB;
    juce::Slider tension, similarity;
    int combineStep = -1; int pick[3] { -1, -1, -1 };
    juce::Rectangle<int> grid, harvestArea, rowB, rowC, rowD, titleText, seedArea;
    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    bool keyPressed (const juce::KeyPress& k) override;
    juce::Rectangle<int> tileRect (int i) const;
    juce::Rectangle<int> starRect (int i) const { return tileRect (i).removeFromTop (24).removeFromRight (26); }
    juce::Rectangle<int> chipRect (int k) const;
    void sync();
    void play (int tile);
    int hover = -1; bool draggedOut = false;
private:
    void timerCallback() override;   // TENSION: commits 150 ms after the last move (drag, wheel, keys)
    void commitParams();
    void commitRhythm();
};

// YOUR SAMPLE: the sample, its waveform (START / END / LOOP markers, click = hear it at its root), its settings and which lanes play it
struct SamplePanel : juce::Component, juce::SettableTooltipClient
{
    ArpEditor& ed; ThemeRef& thr; explicit SamplePanel (ArpEditor& e);
    Btn loadB, recentB, clearB, autoLoopB, revB, formantB, close; juce::OwnedArray<Btn> laneB;
    Seg modeSeg; Stepper rootS, fineS;
    // NOTE LENGTH + SLICES
    Seg fitSeg { thr, { "TRIM", "STRETCH" } }, orderSeg { thr, { "IN ORDER", "BY PITCH" } };
    Btn fitLoopB { thr, "HOLD IN LOOP" }, sliceB { thr, "SLICE TO NOTES" }, pitchedB { thr, "PITCHED" }, patternB { thr, "PATTERN FROM SLICES" };
    Stepper fadeS { thr, "END FADE", 1, 500 };
    juce::Rectangle<int> modeRow;
    void paintOverChildren (juce::Graphics& g) override;   // the slice markers over the waveform
    juce::Slider aS, dS, sS, rS, cutS, toneS, volS, xfS;
    ghosty::WaveRegion waveC;                        // the waveform: START / END handles, the loop, the shared gestures, WAV drag-out
    juce::Rectangle<int> lblRow, knobRow;
    void paint (juce::Graphics& g) override;
    void resized() override;
    void sync();
    void push (const std::function<void (arpsmp::Params&)>& fn);   // edit the settings
    juce::File writeRegion (float a, float b);       // fractions a..b of YOUR SAMPLE as a WAV (the drag-out)
};

class ArpEditor : public juce::AudioProcessorEditor, public juce::DragAndDropContainer, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    friend struct ::ArpEditorTestAccess;
    explicit ArpEditor (ArpProcessor&);
    ~ArpEditor() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseUp (const juce::MouseEvent&) override;

    ArpProcessor& proc;
    ThemeRef th; ArpLnF lnf { th };
    int lane() const { return proc.selLane; }
    void selectLane (int l);
    juce::String dispChord (const std::string& name) const;   // a chord / key name as it sounds (the live TRANSPOSE applied)
    juce::String dispKey() const;
    int dispShift (int lane) const;
    juce::String tempoText() const;
    void syncTransportUi();
    Btn& themeButton();                                            // (tests)
    juce::Component* bypassButton(); juce::Component* syncButton(); // (tests)
    std::vector<juce::Rectangle<int>> modalFocus();                // (tests) the SAVE / LOAD card or the first-run hint bubbles while one is up
    void applyThemeNow (int mode);                                 // the look for this mood + the saved theme, no fade (tests, screenshots)
    // GHOST (ArpEZator-only 4th look, liquid glass on the LIGHT base): the aurora is rendered once per size, the big cards once per size
    ghosty::glass::Backdrop backdrop; ghosty::glass::PanelCache panels;
    void paintGlassSurfaces (juce::Graphics& g);                   // (design coordinates) the aurora + the glass cards + plates, no text (the tests sample it)
    juce::Rectangle<int> soundCardArea() const { return soundCard; } juce::Rectangle<int> editorCardArea() const { return editorCard; } juce::Component& contentComp() { return content; } juce::Rectangle<int> moodGhostArea() const { return ghostArea; }   // (tests)
    juce::String toastShown() const { return juce::Time::getMillisecondCounterHiRes() < toastUntil ? toastText : juce::String(); }   // (tests)
    void toast (const juce::String& s, double ms = 2600) { toastText = s; toastUntil = juce::Time::getMillisecondCounterHiRes() + ms; repaint(); }
    void startDrag (int mask, juce::Component* src);
    void startDragOut (int mask, bool audio, bool stems, juce::Component* src, bool gated = false);   // MIDI / AUDIO, mix / stems (gated: the caller asked the export gate)
    void doUndo(); void doRedo();
    void openSave(); void openBrowser();
    void closeOverlay();
    bool stems() const { return mixStems.sel == 1; }
    juce::String renderStatus;                 // "RENDERING 40%" / "AUDIO READY" (drag pills show it)
    void refresh();           // controls follow the idea
    void setMode (int m);
    void tool (const juce::String& t);
    Colour laneColour (int l) const;
    static juce::String laneName (int l);
    bool snapKey = true, followPlay = true;
    juce::Rectangle<int> modeArea, genArea, dragAllArea, laneArea;   // for the first-run hints

    Roll roll { *this }; PatternEd pattern { *this }; ChordChips chips { *this };
    std::unique_ptr<MorePanel> more;
    std::unique_ptr<HookLabPanel> hookLab;
    void openHookLab (bool open);
    std::unique_ptr<SamplePanel> samplePanel;
    void openSample (bool open);
    void loadSampleFile (const juce::File& f);
    // SOUNDS PER LANE: the tabs pick the lane the dropdown edits (they follow the roll's lane until you click one); LINK ALL = one sound for all
    Seg soundTabs { th, { "CHORDS", "ARP", "MELODY", "BASS" } }; Btn linkB { th, "LINK ALL" }, sampleB { th, "SAMPLE" };
    int soundTab = -1;
    int soundLane() const;
    static juce::String versionString();   // JucePlugin_VersionString (3.0.0)
    void fillPresetBox();

private:
    void timerCallback() override;
    void layoutContent();
    void linkSet (int type);
    std::unique_ptr<ghosty::TypeIn> typeIn;
    juce::Component content;   // fixed 1240 x 820 design, scaled to the window
    // header
    Seg modeSeg, modeSeg2; Btn gen, seedPrev, seedNext, undoB, redoB, saveB, loadB, moreB, themeB; std::unique_ptr<ghosty::UnlockButton> unlock;
    std::unique_ptr<ghosty::BypassButton> bypassB;
    std::unique_ptr<juce::TooltipWindow> tips;       // the one tooltip window (theme-coloured, opaque)
public:
    struct WheelStep : juce::MouseListener { juce::Component::SafePointer<juce::Slider> last; double lastT = 0; void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override; } wheelStep;
    void clickGenerate() { gen.onClick(); }
    void applySliderConvention (juce::Component& root);   // every knob / slider in root: type-in, default, units, scrub, fine, one wheel step
private:
    Seg playSeg { th, { "ALWAYS", "ON NOTES" } };   // PLAY: ALWAYS | ON NOTES (FL channel mute follow)
    std::unique_ptr<ghosty::SyncButton> syncB;       // SYNC: DAW | OWN
    Stepper trS { th, "TRANSPOSE", -24, 24 }; Btn bakeB { th, "BAKE" }, keyB { th, "TO KEY" };   // TRANSPOSE (live offset), BAKE, TO KEY + lanes
    std::unique_ptr<juce::Component> overlay;   // SAVE name prompt / LOAD browser
    std::unique_ptr<juce::MouseListener> presetUndo;
    juce::Slider kickVol { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox }; std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> kickVolAtt;
    // audio drag-out: rendered in the background, cached until something changes
    struct AudioJob { juce::String key; juce::StringArray files; std::shared_ptr<std::atomic<float>> progress; bool running = false, silent = false; };
    AudioJob audioJob; juce::Component::SafePointer<juce::Component> audioDragSrc;
    juce::String audioKey (int mask, bool stems) const;
    juce::Label seedLbl;
    // transport
    Btn play, bpmB, tap, kickB, followB, evolveB; std::unique_ptr<DragPill> dragAll, dragAudio; Seg mixStems;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> kickAtt;
    // lanes + sound
    juce::OwnedArray<LaneCard> lanes;
    juce::OwnedArray<DragPill> lanePills;   // each lane card's DRAG pill (owned here, children of the cards)
    juce::ComboBox presetBox; Btn layerPluck, layerOct, stabsB, soundB;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> presetAtt;   // (unused: the dropdown edits the lane's sound by hand)
    bool fillingPresets = false;
    juce::OwnedArray<Knob> macros;
    // editor area
    juce::OwnedArray<Btn> tools; Btn harvestB;
    Seg cycleSeg, rangeSeg, rateSeg, formSeg, staggerSeg, melRangeSeg, chordLen, chordRhythm, voicingSeg, bassRate;
    juce::Slider stutter; juce::Label stutterLbl;
    Btn lkGlide, lkFat, lkStep, lkAll, snapB, zoomIn, zoomOut, followRollB, lockSel, clearSelB;
    Btn loopB { th, "LOOP" };                         // LOOP SECTION on / off (the brace on the bar ruler); key L
    Seg dragSecSeg { th, { "WHOLE", "SECTION" } };    // DRAG: the whole idea or just the looped section
public:
    // CHORDS OCT - / OCT +: the whole chord lane an octave down / up (the other lanes stay), the readout between them
    Btn chordOctDn { th, "OCT -" }, chordOctUp { th, "OCT +" }; juce::Label chordOctLbl;
    void chordOctave (int d);
    int seenChordRewrite = 0;   // proc.chordRewriteSeq already toasted
    void toggleLoopSection();
    void startSectionDrag (bool audio, juce::Component* src);
    // the loop's WAV renders in the background as soon as the loop is made / changed (debounced), so ONE drag of the WAV grip delivers it
    juce::String sectionWavKey() const;                // the render's identity: section range + idea version + settings
    bool sectionWavReady() const;                      // the loop's WAV is rendered and on disk
    bool sectionWavRendering() const;
    void prerenderSection (bool now = false);          // (the timer calls it; tests pass now = true)
    juce::String prerenderKey; double prerenderSeen = 0; juce::String prerenderSeenKey;
    bool toldShortDawLoop = false; double toldLoopAt = -1e9; bool wasPlayingUi = false;   // the loop's grips: the section as MIDI / WAV (the whole idea without a loop)
private:
    juce::Slider lkAmt; juce::Label lkInfo;
    // HOOK LAB controls on the lanes
    Btn hookLabB { th, "HOOK LAB" }, respondB { th, "RESPONSE" }, motifB { th, "LOCK MOTIF" };
    Seg styleSeg { th, { "AUTO", "HYPNOTIC", "ANTHEMIC", "MINIMAL", "EMOTIONAL", "PLAYFUL" } }, candySeg { th, { "NO CANDY", "SUBTLE", "HOOK", "EUPHORIC", "UNHINGED" } };
    Seg evolveModeSeg { th, { "OFF", "SUBTLE", "GROW", "WAVE", "JOURNEY" } };
    Seg dirSeg { th, { "FIGURE", "UP", "DOWN", "UP-DOWN", "RANDOM", "PENDULUM", "CONVERGE" } }, contourSeg { th, { "FLAT", "RISE", "FALL", "ARCH", "VALLEY" } }, accentSeg { th, { "AS FIGURE", "DOWNBEAT", "BUILD", "OFFBEAT", "GROW" } };
    juce::Slider tensionS, simS; juce::Label tensionLbl, simLbl, evolveLbl;
    Seg arpMeterSeg { th, { "4/4", "3/4", "5/4", "7/8", "9/8" } }; Stepper arpEucS { th, "EUCLID", 0, 16 }, arpRotS { th, "ROTATE", 0, 15 };
    struct Debounce : juce::Timer { std::function<void()> fn; void timerCallback() override { stopTimer(); if (fn) fn(); } } tensionCommit;
    AlignStrip align { *this };
    juce::Rectangle<int> editorCard, toolsRow, ctlRow, linkRow, soundCard, header, transport, ghostArea;
    // animation
    double t = 0; Palette fromP, toP; double fadeStart = -1; int lastVersion = -1; Face face = faceFor ("happy"), faceTarget = faceFor ("happy"); std::string moodWord = "HAPPY";
    juce::String toastText; double toastUntil = 0; bool showHints = false;
    double bpmDragY = 0; int bpmDrag0 = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArpEditor)
};
} // namespace arpui

using ArpEditor = arpui::ArpEditor;
