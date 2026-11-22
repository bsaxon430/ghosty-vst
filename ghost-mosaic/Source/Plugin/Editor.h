// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
//
// The GhostBlock screen:
//   header      the GHOST MOSAIC wordmark (heat word), status, and the page buttons: PLAY / TERRAIN /
//               UNDER THE HOOD / the new mode that shows the engine at work (Scope/, working name INSIDE)
//   left        SOURCE SOUNDS (the library: drop sounds or folders) and TARGET (drop the sound
//               to rebuild; in the effect, the track's own audio, live), with play and record
//   middle      THE LIVE VIEW (dots = library pieces, or the heat of which pieces are used)
//               and the big macros
//   bottom      UNDO / REDO / A-B / presets, GENERATE, MUTATE, DRAG WAV, session menu
#pragma once
#include "Processor.h"
#include "Look.h"
#include "Swarm.h"
#include "HeatWord.h"
#include "HoodPage.h"
#include "TerrainPage.h"
#include "Scope/ScopePage.h"

namespace mosaic
{
class Editor;
juce::String creditsText();   // the About / Credits words (the one place the open-source projects are named)

// ------------------------------------------------------------------ SOURCE SOUNDS
class SourcePanel : public juce::Component, public juce::FileDragAndDropTarget, private juce::ListBoxModel
{
public:
    explicit SourcePanel (Editor& e);
    void refresh();
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void fileDragEnter (const juce::StringArray&, int, int) override { m_hot = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { m_hot = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;
    juce::ListBox& list() { return m_list; }
    void syncLive();                              // MOVING NOW: sounds TERRAIN keeps out of play show it
    juce::TextButton m_add { "ADD" }, m_folder { "FOLDER" }, m_kits { "BUILT-IN" }, m_all { "ALL" }, m_none { "NONE" }, m_clear { "CLEAR" };

private:
    int getNumRows() override { return (int) m_sounds.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    juce::String getTooltipForRow (int) override { return "Click the switch to use this sound or not; x takes it out."; }
    Editor& m_ed;
    juce::ListBox m_list { "sounds", this };
    std::vector<SoundItem> m_sounds;
    bool m_hot = false;
    std::vector<char> m_benched; bool m_wasMasked = false;
};

// ------------------------------------------------------------------ TARGET
class TargetPanel : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    explicit TargetPanel (Editor& e);
    void refresh();
    void tick();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void fileDragEnter (const juce::StringArray&, int, int) override { m_hot = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { m_hot = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;
    juce::TextButton m_load { "LOAD" }, m_demo { "BUILT-IN" }, m_play { "PLAY" }, m_rec { "RECORD" };
    juce::ToggleButton m_live { "LIVE INPUT" };

private:
    juce::Rectangle<int> waveArea() const;
    Editor& m_ed;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> m_liveAtt;
    std::shared_ptr<const TargetPicture> m_pic;
    int m_cur = -1;
    bool m_hot = false;
};

// ------------------------------------------------------------------ a big macro knob
class Macro : public juce::Component
{
public:
    Macro (Processor& p, const juce::String& label, const char* paramId, const juce::String& left = {}, const juce::String& right = {});
    // a knob that sets several settings at once (PIECE SIZE: the library's and the target's)
    Macro (Processor& p, const juce::String& label, std::vector<const char*> ids, double lo, double hi, double mid);
    void resized() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void sync();                                  // follow the settings when they change elsewhere
    void syncLive (const Engine& e);              // MOVING NOW: the in-use value follows TERRAIN (a second indicator; the handle stays); greyed while the setting does nothing
    juce::Slider knob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::String label;

private:
    juce::String valueText() const;
    Processor& m_proc;
    std::vector<const char*> m_ids;
    juce::String m_left, m_right;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> m_att;
    int m_liveIdx = -1; bool m_liveOn = false; float m_liveP = -1; juce::String m_liveText;
    juce::String m_idleWhy;                       // rule 3: why the setting does nothing right now ("" = it does something)
};

// ------------------------------------------------------------------ DRAG WAV
class DragWav : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit DragWav (Editor& e);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void setFile (const juce::File& f) { m_file = f; m_rendering = false; m_fresh = f.existsAsFile(); repaint(); }
    void stale() { m_fresh = false; repaint(); }
    void render();
    juce::File file() const { return m_file; }
    bool rendering() const { return m_rendering; }
    bool fresh() const { return m_fresh && m_file.existsAsFile(); }

private:
    Editor& m_ed;
    juce::File m_file;
    bool m_rendering = false, m_fresh = false, m_dragged = false;
};

// ------------------------------------------------------------------ the editor
class Editor : public juce::AudioProcessorEditor, private juce::Timer, private juce::ChangeListener, private juce::ValueTree::Listener
{
public:
    explicit Editor (Processor& p);
    ~Editor() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    Processor& proc() { return m_proc; }
    void chooseSounds (bool folder);
    void chooseTarget();
    void showKitMenu (juce::Component& from);
    void showDemoTargetMenu (juce::Component& from);
    void togglePlay();
    void toggleRecord();
    void generate();
    void mutate();
    void showPage (bool hood) { setPage (hood ? 2 : 0); }
    void setPage (int page);                       // 0 PLAY, 1 TERRAIN, 2 UNDER THE HOOD, 3 the new mode (INSIDE)
    int page() const { return m_page; }
    bool hoodShown() const { return m_hoodShown; }

    // tests
    Swarm& swarm() { return m_swarm; }
    HoodPage& hood() { return *m_hood; }
    TerrainPage& terrainPage() { return *m_terrain; }
    SourcePanel& sources() { return m_source; }
    TargetPanel& target() { return m_target; }
    DragWav& dragWav() { return m_drag; }
    juce::Component* findById (const juce::String& id);
    int numMacros() const { return (int) m_macros.size(); }
    void frame() { timerCallback(); }
    juce::String shownStateLine() const { return m_stateLine; }
    juce::String shownStatus() const { return m_status; }
    bool showsProgressBar() const { return m_building; }
    scope::Page& scopePage() { return *m_scope; }
    HeatWord& wordmark() { return m_word; }
    juce::Rectangle<int> statusArea() const { return m_statusArea; }
    // tests and the studio harness: no wall clock. The editor's own 60 Hz timer stops (the new mode's page and the
    // wordmark stop following the clock too); each stepForTests is exactly one frame of dt seconds.
    void setTestClock (bool on);
    void stepForTests (double dtSeconds);

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void refreshAll();
    void syncLive();                               // ~30 fps: the in-use values of what TERRAIN moves, on PLAY and UNDER THE HOOD
    void scopeOnScreen();                          // the new mode runs only while its page shows and the window is up
    bool m_algoLive = false; juce::String m_algoLiveText;
    int m_liveFrame = 0;
    void openSession();
    void saveSession();
    void openLibrary();
    void saveLibrary();
    void showSessionMenu();
    void launch (std::unique_ptr<juce::FileChooser> c, int flags, std::function<void (const juce::FileChooser&)> fn);

    Processor& m_proc;
    look::LookAndFeel m_lnf;
    juce::TooltipWindow m_tips { this, 600 };
    HeatWord m_word;
    juce::TextButton m_tabPlay { "PLAY" }, m_tabTerrain { "TERRAIN" }, m_tabHood { "UNDER THE HOOD" }, m_tabScope { "INSIDE" };
    SourcePanel m_source;
    TargetPanel m_target;
    Swarm m_swarm;
    juce::TextButton m_viewDots { "DOTS" }, m_viewHeat { "HEAT" };
    juce::ComboBox m_algo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> m_algoAtt;
    juce::OwnedArray<Macro> m_macros;
    juce::TextButton m_undo { "UNDO" }, m_redo { "REDO" }, m_ab { "A / B" }, m_generate { "GENERATE" }, m_mutate { "MUTATE" }, m_session { "SESSION" };
    juce::ComboBox m_presets;
    DragWav m_drag;
    std::unique_ptr<HoodPage> m_hood;
    juce::Viewport m_hoodView;
    bool m_hoodShown = false;
    int m_page = 0;
    std::unique_ptr<TerrainPage> m_terrain;
    std::unique_ptr<scope::Page> m_scope;          // page 3: the view of the engine at work, and the tap's model it reads
    std::unique_ptr<juce::FileChooser> m_chooser;
    std::shared_ptr<const LibraryPicture> m_libShown;
    juce::String m_status, m_stateLine, m_notice;
    double m_eta = -1;
    float m_progress = 0, m_level = 0;
    juce::Rectangle<float> m_subSpan;   // the subtitle's line: exactly under the wordmark's letters, spread end to end
    bool m_busy = false, m_warnedSlow = false, m_building = false;
    int m_shock = 0;
    juce::Rectangle<int> m_viewCard, m_macroCard, m_statusArea;
    std::vector<Engine::Event> m_events;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Editor)
};
}
