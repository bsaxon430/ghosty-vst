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

    void paint (juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    void visibilityChanged() override;
    void mouseDown (const juce::MouseEvent&) override;

    struct Wave;
    struct Keys;
    struct Settings;
    int selectedChop = 0;                     // the chop the SOUND panel edits (INTERSECT's slice index)
    void selectChop (int sliceIndex);
    Settings* settingsPanel() { return settings.get(); }
    juce::TextButton& installButton() { return installB; }
    struct ToolButton;
    struct DragOut;

private:
    void timerCallback() override;
    void handleNoteOn (juce::MidiKeyboardState*, int, int note, float v) override;
    void handleNoteOff (juce::MidiKeyboardState*, int, int note, float) override;
    void refreshControls();
    std::function<const ghosty::themes::Palette&()> palette;
    std::function<void()> openLab;
    Tool currentTool = Tool::select;

    std::unique_ptr<Wave> wave;
    juce::MidiKeyboardState keyState;
    std::unique_ptr<Keys> keys;
    std::unique_ptr<Settings> settings;
    std::vector<std::unique_ptr<ToolButton>> tools;
    juce::TextButton playB { "PLAY" }, loopB { "LOOP" }, autoB { "AUTO CHOP" }, minus { "-" }, plus { "+" };
    juce::ComboBox autoMode;
    juce::TextButton keysChops { "CHOPS" }, keysWhole { "WHOLE" }, convertB { "CONVERT VOICE" };
    juce::TextButton addB { "ADD WAV" }, recB { "REC" }, installB { "INSTALL" };
    std::unique_ptr<DragOut> dragOut;
    juce::TextButton undoB { "UNDO" }, redoB { "REDO" }, cutB { "CUT" }, copyB { "COPY" }, pasteB { "PASTE" }, delB { "DELETE" },
                     trimB { "TRIM" }, dupB { "DUPLICATE" }, clearB { "CLEAR CHOPS" };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> deckArea, infoArea, tipArea, statusArea, countArea, keysLabelArea;
    bool recording = false;
    int ticks = 0;
    bool toolWasBusy = false;
    struct Conversion { bool active = false; chop::Region r; juce::File before; juce::String why; } conv;
    juce::String note;                        // the page's own last message (wins over the engine's while fresh)
    double noteTime = 0;
    void say (const juce::String& s) { note = s; noteTime = juce::Time::getMillisecondCounterHiRes(); repaint(); }
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChopPage)
};
}
