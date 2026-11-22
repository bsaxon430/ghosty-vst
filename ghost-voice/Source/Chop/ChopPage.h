// Ghost Voice - AGPL-3.0-only (see LICENSE, NOTICE.md).
//
// The CHOP page (build 8): the clip, Block Step's tools and keys, AUTO CHOP, the keyboard and CONVERT VOICE.
//   Tools (Block Step's letters, which are FL Studio's): E SELECT, C SLICE, D DELETE, Z ZOOM, Y PLAYBACK.
//   Keys (FL Studio's, Cmd on a Mac = Ctrl): SPACE play / stop, Cmd+Z undo, Cmd+Shift+Z / Cmd+Y redo, Cmd+A select all,
//   Cmd+D deselect, Cmd+C copy, Cmd+X cut, Cmd+V paste, Cmd+B duplicate, Delete / Backspace delete, Cmd+U AUTO CHOP,
//   Cmd+L loop the selection, PgUp / PgDn zoom, Esc deselect.
//   Loop: drag along the ruler (left or right button, like Block Step's step numbers) and LOOP turns it on / off.
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "ChopEngine.h"
#include "GhostThemes.h"
#include "Choir.h"

namespace gv
{
class Processor;

class ChopPage : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer, private juce::MidiKeyboardState::Listener
{
public:
    ChopPage (Processor&, std::function<const ghosty::themes::Palette&()> palette, std::function<void()> openIdentityLab);
    ~ChopPage() override;

    enum class Tool { select = 0, slice, erase, zoom, playback, kTools };
    static juce::String toolName (Tool);
    static juce::String toolTip (Tool);
    static juce::juce_wchar toolKey (Tool);
    void setTool (Tool);
    Tool tool() const { return currentTool; }

    // the page's own state (what the tools and keys act on)
    chop::Region selection, loop;
    bool loopOn = false;
    juce::int64 cursor = 0;
    double viewStart = 0, viewFrames = 0;     // what the wave shows (clip frames)
    void zoomBy (double factor, double aroundFrame);
    void showAll();

    // actions (buttons, keys and tests call these)
    void togglePlay();
    bool loadFile (const juce::File&);
    void convertVoice();
    bool converting() const { return conv.active; }
    bool keyPressed (const juce::KeyPress&) override;
    juce::String status() const;              // the status line

    chop::ChopEngine& engine();
    Processor& proc;
    const ghosty::themes::Palette& pal() const { return palette(); }
    const choir::Pal& cp() const { return choir::pal (! palette().isLight); }   // THE GHOST CHOIR's own colours

    void paint (juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    void visibilityChanged() override;
    void mouseDown (const juce::MouseEvent&) override;

    struct Wave;
    struct Keys;
    struct Settings;
    struct InstallVeil;                       // (polish 2) the finder's install card over the dimmed wave
    std::unique_ptr<InstallVeil> veil;
    struct Stalls;                            // the choir stalls over the keys: one hooded singer per chop
    int selectedChop = 0;                     // the chop the SOUND panel edits (INTERSECT's slice index)
    void selectChop (int sliceIndex);
    void refreshControls();                   // (UNDER THE HOOD changes the engine's mode / count / chops too)
    Settings* settingsPanel() { return settings.get(); }
    juce::TextButton& installButton() { return installB; }
    // (polish 1) the clip's own buttons (ADD WAV, REC, DRAG WAV, CONVERT VOICE) stand in the window's left column on CHOP, like every
    // other page's list: the Editor adopts them as its own children and places them; the page still drives them
    std::vector<juce::Component*> sideControls();
    // (fixes) the status line is its own strip under the page: the Editor adopts it (like sideControls) and places it so the
    // window's resize corner keeps its own place beside it, never on top of the page
    juce::Component* statusStrip();
    void paintStatus (juce::Graphics&, juce::Rectangle<int>);
    juce::String stateLine() const;
    // (polish 3) EVERY BEAT / EVERY 2 BEATS / EVERY BAR stop at the count: how many of the clip's beats were chopped, and where the
    // part that was not chopped starts (the last chop runs on to the end of the clip). Read only: the chopping is the engine's
    struct BeatCap { bool on = false; int shown = 0, avail = 0; juce::int64 tailFrom = 0, tailTo = 0; juce::String unit; };
    BeatCap beatCap() const;
    juce::String beatCapLine() const;
    int playingNote() const;                  // (polish 2) the key of the chop the transport is playing through now (-1: none)           // what the page holds now, in words (the status line when nothing newer was said)
    struct ToolButton;
    struct DragOut;

private:
    void timerCallback() override;
    void handleNoteOn (juce::MidiKeyboardState*, int, int note, float v) override;
    void handleNoteOff (juce::MidiKeyboardState*, int, int note, float) override;
    std::function<const ghosty::themes::Palette&()> palette;
    std::function<void()> openLab;
    Tool currentTool = Tool::select;

    std::unique_ptr<Wave> wave;
    juce::MidiKeyboardState keyState;
    std::unique_ptr<Keys> keys;
    struct KeyArrow;                          // (polish 3) the keyboard's scroll arrows, each in its own gutter
    std::unique_ptr<KeyArrow> keyLeft, keyRight;
    void repaintStalls();
    std::unique_ptr<Settings> settings;
    struct StatusStrip;
    std::unique_ptr<StatusStrip> strip;
    std::unique_ptr<Stalls> stalls;
    choir::Cache back;                        // the page's static art (stone, the lectern, the frames)
    juce::Rectangle<int> waveFrame, soundFrame, choirFrame;
    std::vector<std::unique_ptr<ToolButton>> tools;
    juce::TextButton playB { "PLAY" }, loopB { "LOOP" }, autoB { "AUTO CHOP" }, minus { "-" }, plus { "+" };
    juce::ComboBox autoMode;
    juce::TextButton keysChops { "CHOPS" }, keysWhole { "WHOLE" }, convertB { "CONVERT VOICE" };
    juce::TextButton addB { "ADD WAV" }, recB { "REC" }, installB { "INSTALL" };
    std::unique_ptr<DragOut> dragOut;
    juce::TextButton undoB { "UNDO" }, redoB { "REDO" }, cutB { "CUT" }, copyB { "COPY" }, pasteB { "PASTE" }, delB { "DELETE" },
                     trimB { "TRIM" }, dupB { "DUPLICATE" }, clearB { "CLEAR CHOPS" };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> deckArea, infoArea, tipArea, countArea, keysLabelArea;
    bool recording = false;
    bool keysPlaced = false;
    int ticks = 0;
    int lastPlayingNote = -1;
    bool toolWasBusy = false;
    struct Conversion { bool active = false; chop::Region r; juce::File before; juce::String why; } conv;
    juce::String note;                        // the page's own last message (wins over the engine's while fresh)
    double noteTime = 0;
    mutable juce::String seenMessage, seenSig; // the engine's message is shown only while what it talks about is still what is on screen
    juce::String stateSig() const;
    void say (const juce::String& s) { note = s; noteTime = juce::Time::getMillisecondCounterHiRes(); repaint(); }
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChopPage)
};
}
