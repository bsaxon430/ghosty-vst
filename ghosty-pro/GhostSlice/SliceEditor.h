#pragma once
// Ghost Slice window, build 16 (2026-11-13): a brand-new screen in the layout of Brandon's reference picture; build 18 redrew it in
// the spirit of Land of the Lustrous (Haruko Ichikawa's graphic style): paper-white negative space, ink hairlines and solid black
// blocks, a colonnade, a cracked faceted crystal under the moon, the gem's luster the only colour (all drawn in code, our own
// drawing, no character from the series). The old window (four looks,
// UNDER THE HOOD, TERRAIN, the STYLE browser, CLEAR pill, VELOCITY, SCALE) is gone. Laid out on the picture's own 1536 x 1024
// pixels, scaled as a whole; a window of another shape gets the extra room in the picture, the TAKES box and the keys (no empty sides):
//   header   the waveform mark + GHOST SLICE / VOCALS · SOUNDS · INSPIRATION | KEY (AUTO = the sound's) | BPM | ½x 1x 2x | ⚙
//   left     TAKE TYPE  AUTO / INSTRUMENT / PAD / CHOPS / TEXTURE      CHARACTER  NATURAL / CHIPMUNK / DEEP crystals
//            STYLE (the melody style, a menu) and LENGTH (bars)
//   the view the landscape: NO SOUND LOADED / DROP A VOCAL OR ANY SOUND (drop a file anywhere on it), LOAD... / RECORD,
//            the sound's waveform strip, PLAY (the sound itself) and TRIM (drag the part GENERATE uses)
//   NATURAL <-> MUSICAL, TONE / the type's knob (VOICE, DREAM, STUTTER, DRIFT) / SPACE, the crystal GENERATE, VARY
//   TAKES n  < the takes >  PLAY | DRAG WAV | DRAG MIDI | DRAG CHORDS | SAVE TAKE
//   the keys C1 .. G7 (FL names), the take's notes lit.
// Anything that does nothing right now is greyed out (Brandon's rule). The ⚙ menu holds the rest: RECORD input, CLEAR, the key lock,
// TRIM reset, the licence (store builds), how it works.
#include "SliceProcessor.h"
#include <bitset>
#include <map>

namespace gslice::ui
{
constexpr int kDesignW = 1536, kDesignH = 1024;

// the look: colours and the two typefaces (Cinzel for the engraved caps, Cormorant Garamond italic for the soft lines)
struct Look
{
    // build 18: Land of the Lustrous-inspired: paper white space, ink black hairlines and blocks, the gem's luster the only colour
    // build 19: a DARK look (the moon's side): the same drawing at night, bone-white lines on near-black, the luster glowing.
    // One look for every Ghost Slice window at a time (the last one opened / toggled sets it); each session remembers its own.
    static inline bool dark = false;
    static juce::Colour paper()  { return dark ? juce::Colour (0xff0c0d11) : juce::Colour (0xfff4f2eb); }   // the space
    static juce::Colour paper2() { return dark ? juce::Colour (0xff1a1c22) : juce::Colour (0xffe8e5db); }
    static juce::Colour ink()    { return dark ? juce::Colour (0xffe8e4d8) : juce::Colour (0xff111215); }   // the lines and words
    static juce::Colour soft()   { return dark ? juce::Colour (0xff9b978d) : juce::Colour (0xff5c5a55); }   // the second line of text
    static juce::Colour grey()   { return dark ? juce::Colour (0xff4b4b52) : juce::Colour (0xffb0ada4); }   // greyed out
    static juce::Colour band()   { return dark ? juce::Colour (0xff17191f) : juce::Colour (0xff111215); }   // the header, the waveform strip, the keys' bed
    static juce::Colour bandFg() { return dark ? juce::Colour (0xffe8e4d8) : juce::Colour (0xfff4f2eb); }
    static juce::Colour black()  { return juce::Colour (0xff111215); }   // ink on the luster / on white stone, both looks
    static juce::Colour stone()  { return juce::Colour (0xfff4f2eb); }   // a clear crystal, both looks
    static juce::Colour keyWhite() { return dark ? juce::Colour (0xffd6d2c6) : juce::Colour (0xfff4f2eb); }
    static juce::Colour bg()     { return paper(); }
    static void applyTo (juce::LookAndFeel&);   // the menus' / tooltips' colours for the look
    static juce::ColourGradient luster (juce::Rectangle<float> r, float alpha = 1.0f);   // the gem's sheen: mint, lilac, champagne, ice
    static void hatch (juce::Graphics&, const juce::Path& clip, float angleDeg, float spacing, juce::Colour c, float thick = 0.7f);
    static void sparkle (juce::Graphics&, juce::Point<float> c, float r, juce::Colour col);
    static juce::Font caps (float h, bool semi = false, float track = 0.18f);
    static juce::Font italic (float h);
    static float width (const juce::Font& f, const juce::String& s);
    static void text (juce::Graphics& g, const juce::String& s, const juce::Font& f, juce::Rectangle<float> r, juce::Justification j = juce::Justification::centred);
};

struct LnF : juce::LookAndFeel_V4
{
    LnF();
    void applyLook();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    juce::Font getPopupMenuFont() override { return juce::Font (juce::FontOptions (15.0f)); }
    void drawTooltip (juce::Graphics&, const juce::String& text, int w, int h) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent) override;
};

enum class Icon { None, Folder, Record, Stop, Play, Scissors, Gear, ChevL, ChevR, ChevDown, Wave, Bars, Waves, Chops, Dots, Infinity, WaveFile, Note, Layers, Download, Star, Moon, Sun };
void drawIcon (juce::Graphics&, Icon, juce::Rectangle<float>, juce::Colour);

// a button of the picture: Box = the thin-outlined dark box (LOAD..., PLAY, the bottom row), Drop = a header / STYLE dropdown
// (text + chevron), Plain = an icon alone (⚙, < >). Greyed out when disabled.
struct Btn : juce::Button
{
    enum class Style { Box, Drop, Plain, Tile };
    Btn (Style s, Icon i, const juce::String& t) : juce::Button (t), style (s), icon (i) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    Style style; Icon icon; bool lit = false, red = false, inverse = false; float cap = 14.0f, iconSize = 18.0f;   // inverse: on black
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

// a bottom-row tile you drag out (DRAG WAV / MIDI / CHORDS): the drag starts the export (the demo gate asks first)
struct DragTile : Btn
{
    DragTile (Icon i, const juce::String& t) : Btn (Style::Tile, i, t) {}
    std::function<void()> onDragOut; bool started = false;
    void mouseDown (const juce::MouseEvent& e) override { started = false; Btn::mouseDown (e); }
    void mouseDrag (const juce::MouseEvent& e) override { if (! started && isEnabled() && e.getDistanceFromDragStart() > 4) { started = true; startDrag(); } }
    void startDrag() { if (onDragOut) onDragOut(); }   // (tests call this: the same path a real drag takes)
};

// TAKE TYPE: five wide rows; the chosen one is the teal crystal bar
struct TypeList : juce::Component, juce::TooltipClient
{
    int sel = 0, hover = -1; std::function<void (int)> onChange;
    static constexpr float kRowH = 50.0f, kGap = 5.0f;
    juce::Rectangle<float> row (int i) const { return { 0.0f, (float) i * (kRowH + kGap), (float) getWidth(), kRowH }; }
    int at (juce::Point<int> p) const { for (int i = 0; i < 5; ++i) if (row (i).contains (p.toFloat())) return i; return -1; }
    void set (int i) { if (i != sel) { sel = i; repaint(); } }
    juce::String getTooltip() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { const int i = at (e.getPosition()); if (i >= 0 && i != sel) { sel = i; repaint(); if (onChange) onChange (i); } }
    void mouseMove (const juce::MouseEvent& e) override { const int i = at (e.getPosition()); if (i != hover) { hover = i; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
};

// CHARACTER: three crystal tiles
struct CharTiles : juce::Component, juce::TooltipClient
{
    int sel = 0, hover = -1; std::function<void (int)> onChange;
    juce::Rectangle<float> tile (int i) const;
    int at (juce::Point<int> p) const { for (int i = 0; i < 3; ++i) if (tile (i).contains (p.toFloat())) return i; return -1; }
    void set (int i) { if (i != sel) { sel = i; repaint(); } }
    juce::String getTooltip() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { const int i = at (e.getPosition()); if (i >= 0 && i != sel) { sel = i; repaint(); if (onChange) onChange (i); } }
    void mouseMove (const juce::MouseEvent& e) override { const int i = at (e.getPosition()); if (i != hover) { hover = i; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
};

// ½x | 1x | 2x
struct TempoSeg : juce::Component, juce::SettableTooltipClient
{
    int sel = 1; std::function<void (int)> onChange;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { const int i = juce::jlimit (0, 2, e.x * 3 / juce::jmax (1, getWidth())); if (i != sel) { sel = i; repaint(); if (onChange) onChange (i); } }
};

// the landscape: the words over it, drop a file anywhere on it, the waveform strip along its foot (TRIM: drag the part)
struct Stage : juce::Component, juce::FileDragAndDropTarget, juce::TextDragAndDropTarget, juce::SettableTooltipClient
{
    explicit Stage (SliceProcessor& p) : proc (p) {}
    SliceProcessor& proc;
    std::function<void (const juce::File&)> onFile;
    bool dragOver = false, trimMode = false;
    double dragT0 = 0, dragT1 = 1; int trimDrag = 0;   // 1 left handle, 2 right handle, 3 a new range
    double trimAnchor = 0;
    juce::String headline, status;                       // set by the editor (the big words and the line above them)
    juce::Rectangle<float> strip() const;                // the waveform strip (local)
    juce::Rectangle<float> wave() const;                 // inside it
    double xToFrac (float x) const;
    float fracToX (double f) const;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    bool isInterestedInTextDrag (const juce::String& t) override;
    void textDropped (const juce::String& t, int, int) override;
    void textDragEnter (const juce::String&, int, int) override { dragOver = true; repaint(); }
    void textDragExit (const juce::String&) override { dragOver = false; repaint(); }
    // the drawing (made once per size, at twice the pixels), the waveform (once per sound and width)
    juce::Image art; int artW = 0, artH = 0; bool artDark = false;
    static void paintArt (juce::Graphics&, float w, float h);
    const void* pathFor = nullptr; int pathW = 0; juce::Path wavePath;
};

// the crystal GENERATE (the worker's progress lights it from the left)
struct GenerateButton : juce::Button
{
    explicit GenerateButton (SliceProcessor& p) : juce::Button ("GENERATE"), proc (p) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    SliceProcessor& proc; float phase = 0;
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

// VARY: the dark crystal outline with the infinity mark
struct VaryButton : juce::Button
{
    VaryButton() : juce::Button ("VARY") { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

// the takes: cards in the box (name, type · key, star); click = pick it, right-click = star / delete
struct TakesStrip : juce::Component, juce::TooltipClient
{
    explicit TakesStrip (SliceProcessor& p) : proc (p) {}
    SliceProcessor& proc; int first = 0; juce::Point<int> mouse { -1, -1 };
    static constexpr int kShown = 3;
    struct Hit { juce::Rectangle<int> card, star; int index; };
    std::vector<Hit> hits;
    juce::String getTooltip() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent& e) override { mouse = e.getPosition(); repaint(); }
    void mouseExit (const juce::MouseEvent&) override { mouse = { -1, -1 }; repaint(); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { first = juce::jlimit (0, juce::jmax (0, (int) proc.takes().size() - kShown), first + (w.deltaY < 0 ? 1 : -1)); repaint(); }
    void showCurrent();                      // scroll so the current take is in view
};

// the keys: ivory and ebony, C1 .. G7 in FL Studio's names, the take's notes lit teal
struct Keys : juce::MidiKeyboardComponent
{
    explicit Keys (juce::MidiKeyboardState& s) : juce::MidiKeyboardComponent (s, juce::MidiKeyboardComponent::horizontalKeyboard) {}
    std::bitset<128> lit;
    void drawWhiteNote (int note, juce::Graphics&, juce::Rectangle<float> area, bool down, bool over, juce::Colour, juce::Colour) override;
    void drawBlackNote (int note, juce::Graphics&, juce::Rectangle<float> area, bool down, bool over, juce::Colour) override;
    juce::String getWhiteNoteText (int) override { return {}; }   // (the C names are painted by drawWhiteNote)
};

// a message that fades in over the top of the landscape for a few seconds
struct Toast : juce::Component
{
    juce::String text;
    Toast() { setInterceptsMouseClicks (false, false); }
    void paint (juce::Graphics&) override;
};

// the design canvas: the frame, the panels' words and lines, the landscape; everything sits on it in the picture's own pixels
struct Canvas : juce::Component
{
    std::function<void (juce::Graphics&)> paintFn;
    void paint (juce::Graphics& g) override { if (paintFn) paintFn (g); }
};
} // namespace gslice::ui

class SliceEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SliceEditor (SliceProcessor&);
    ~SliceEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kDefaultW = 1140, kDefaultH = 760;   // the size it opens at (fits FL's window on a 1440 x 900 MacBook Pro)
    static constexpr int kLowKey = 12, kHighKey = 91;         // C1 .. G7 in FL Studio's names (middle C = C5)
    void refresh();                          // the controls from the processor
    void toast (const juce::String& s);

    // the export paths (each asks the demo gate first)
    void dragWavOut();
    void dragMidiOut();
    void dragChordsOut();
    void saveTakeClicked();
    void loadClicked();
    void recordClicked();
    void keyMenu();
    void tempoMenu();
    void styleMenu();
    void lengthMenu();
    void settingsMenu();
    void setDark (bool d);                   // the LIGHT / DARK look (the moon button, the gear menu)
    void toggleClear();
    bool keyPressed (const juce::KeyPress& k) override;   // Cmd / Ctrl + Z: undo the last edit

    SliceProcessor& proc;
    gslice::ui::LnF lnf;
    gslice::ui::Canvas canvas;
    gslice::ui::Stage stage { proc };
    gslice::ui::Btn loadBtn { gslice::ui::Btn::Style::Box, gslice::ui::Icon::Folder, "LOAD..." }, recordBtn { gslice::ui::Btn::Style::Box, gslice::ui::Icon::Record, "RECORD" };
    gslice::ui::Btn playSrcBtn { gslice::ui::Btn::Style::Box, gslice::ui::Icon::Play, "PLAY" }, trimBtn { gslice::ui::Btn::Style::Box, gslice::ui::Icon::Scissors, "TRIM" };
    gslice::ui::Btn keyBtn { gslice::ui::Btn::Style::Drop, gslice::ui::Icon::ChevDown, "KEY" }, tempoBtn { gslice::ui::Btn::Style::Drop, gslice::ui::Icon::ChevDown, "BPM" };
    gslice::ui::TempoSeg tempoSw;
    gslice::ui::Btn gearBtn { gslice::ui::Btn::Style::Plain, gslice::ui::Icon::Gear, "settings" };
    gslice::ui::Btn lookBtn { gslice::ui::Btn::Style::Plain, gslice::ui::Icon::Moon, "look" };
    gslice::ui::TypeList typeTiles;
    gslice::ui::CharTiles charTiles;
    gslice::ui::Btn styleBtn { gslice::ui::Btn::Style::Drop, gslice::ui::Icon::ChevDown, "STYLE" }, lengthBtn { gslice::ui::Btn::Style::Drop, gslice::ui::Icon::ChevDown, "LENGTH" };
    juce::Slider musicalSl { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Slider toneKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox }, typeKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox },
                 spaceKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    gslice::ui::GenerateButton generateBtn { proc };
    gslice::ui::VaryButton varyBtn;
    gslice::ui::TakesStrip strip { proc };
    gslice::ui::Btn prevBtn { gslice::ui::Btn::Style::Plain, gslice::ui::Icon::ChevL, "previous take" }, nextBtn { gslice::ui::Btn::Style::Plain, gslice::ui::Icon::ChevR, "next take" };
    gslice::ui::Btn playBtn { gslice::ui::Btn::Style::Tile, gslice::ui::Icon::Play, "PLAY" }, saveBtn { gslice::ui::Btn::Style::Tile, gslice::ui::Icon::Download, "SAVE TAKE" };
    gslice::ui::DragTile dragWav { gslice::ui::Icon::WaveFile, "DRAG WAV" }, dragMidi { gslice::ui::Icon::Note, "DRAG MIDI" }, dragChords { gslice::ui::Icon::Layers, "DRAG CHORDS" };
    gslice::ui::Keys keys { proc.keyboard };
    gslice::ui::Toast toastView;
    ghosty::UnlockButton unlock { proc.license };   // (not on the screen: the ⚙ menu opens it in store builds)
    juce::String toastText; double toastUntil = 0;
    juce::String keyText() const;            // "A MIN" (the key the next takes use)
    juce::String headline() const;           // the big words on the landscape
    juce::String statusText() const;         // the line above them
    float viewScale() const { return scale; }
private:
    void timerCallback() override;
    void paintCanvas (juce::Graphics&);
    void updateKeysLit();
    bool refreshing = false;
    float scale = 1.0f, dx = 0.0f, dy = 0.0f;   // the window's extra width / height past the picture's shape (design pixels)
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tips { this, 600 };
    bool wasBusy = false, wasRec = false, wasSrcPlaying = false, wasPreview = false;
    double flashUntil = 0;
    const void* litFor = nullptr; int litKey = -99;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SliceEditor)
};
