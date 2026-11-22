// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
//
// UNDER THE HOOD (build 16): the original window rebuilt in our colours. The same three columns in the same order with the
// original's own lowercase control names as the main text (our plain-words name small underneath):
//   brain tweaks | target sound + mix | brain contents,
// the same bottom row (play, pause, record, stop, the volume knob, load session, save session) and the same two tabs
// (search | net). Our additions sit in their own strip underneath ("GHOST MOSAIC (added)").
// Every control is the SAME control as everywhere else (one parameter, one engine call); docs/CONTROLS.md maps each one.
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Processor.h"
#include "ParamRow.h"

namespace mosaic
{
class HoodPage : public juce::Component
{
public:
    struct Actions
    {
        std::function<void()> addSounds, addFolder, allOn, allOff, clearLibrary, generateLibrary, loadTarget, generateTarget,
            restart, openLibrary, saveLibrary, openSession, saveSession, record, stopRecord, play, pause;
    };
    HoodPage (Processor& p, Actions a);
    ~HoodPage() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();                          // network status, recording / playing state
    void refreshSounds();                    // the loaded-sounds list
    void syncLive (const Engine& e);         // MOVING NOW: what TERRAIN moves shows its in-use value; and a setting that does nothing right now greys

    // tests
    bool hasControlFor (const juce::String& paramId) const;
    int numControls() const { return (int) m_rows.size(); }
    juce::Button* button (const juce::String& id) const;
    bool netTabShown() const { return m_netTab; }
    int originalPartBottom() const { return m_originalBottom; }   // where the original part ends (the added strip is below)
    juce::String originalLabel (const juce::String& paramId) const;
    juce::Component* control (const juce::String& paramId) const;

private:
    class Transport;
    class SoundList;
    using Style = ParamRow::Style;
    using Row = ParamRow;                    // one setting (ParamRow.h): the same control the new mode's views can place
    struct Panel { juce::String title; juce::Rectangle<int> box; juce::String number; int headH = 40; bool lower = true; };
    juce::TextButton& addButton (const juce::String& id, const juce::String& text, std::function<void()> fn, const juce::String& tip);
    Row* addRow (const char* id, Style s, const juce::String& mainLabel, const juce::String& sub, const juce::String& prefix = {});
    Row* row (const char* id) const;
    void buildNetwork();
    void applyNetwork();
    void showTab (bool net);

    Processor& m_proc;
    Actions m_actions;
    juce::OwnedArray<Row> m_rows;
    juce::OwnedArray<juce::TextButton> m_buttons;
    std::unique_ptr<SoundList> m_list;
    juce::OwnedArray<Transport> m_transport;
    juce::Slider m_volume { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> m_volumeAtt;
    juce::ToggleButton m_micOff;             // "use mic input" in the instrument (it has no input: the effect version does)
    juce::TextButton m_tabSearch { "search" }, m_tabNet { "net" };
    bool m_netTab = false, m_fftIdle = false;
    int m_originalBottom = 0;
    std::vector<Panel> m_panels;
    juce::Rectangle<int> m_fftLabel, m_mixLabel, m_volumeLabel, m_micLabel;
    float m_k = 1.0f, m_rowK = 1.0f;         // GHOST POSTER: the page's scale and its rows' (from the room there is)
    int m_numW = 42;                         // the width of a section's number block
    // network
    juce::ToggleButton m_netOn { "NETWORK ON" };
    juce::Label m_portLabel[3];
    juce::TextEditor m_port[3];
    juce::TextEditor m_dest[OscBridge::kDestinations];
    juce::ToggleButton m_destOn[OscBridge::kDestinations];
    juce::Label m_netStatus;
};
}
