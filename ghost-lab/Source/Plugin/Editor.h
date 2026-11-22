#pragma once
#include "Processor.h"
#include "SoundBody.h"
#include "TerrainView.h"
#include "Theme.h"
#include "RoomPage.h"
#include "SpectrePage.h"

namespace glab
{
class Card; class WaveView; class Pill; class FooterButton; class DeepPanel; class Content; class Toast; class HoodPage;

class Editor : public juce::AudioProcessorEditor, public juce::DragAndDropContainer, private juce::Timer
{
public:
    explicit Editor (Processor&);
    ~Editor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    // the size it opens at: fits FL's plug-in window on a 1440x900 MacBook Pro with room to spare (never wider than 1240 or
    // taller than 780). Resizable; nothing overlaps at any size (the wide layout scales, the narrow one scrolls).
    static constexpr int kW = 1200, kH = 780, kFooterH = 92, kNarrowBelow = 760;

    // ---- actions (also what the tests press)
    void card (Op op);                       // a card was tapped
    void run (const Step& s, bool fromBody = false);   // process the sound (fromBody: a gesture on THE LIVING SOUND - its result plays when it lands)
    void bodyFeel (const SoundBody::Feel& f);          // a hand on THE LIVING SOUND: the sound changes live while it moves
    void generate();                         // GENERATE MUTATION
    void breed();                            // SPECIMEN 002 dropped on the body
    void undo();
    void redo();
    void openSaveMenu();
    void showAbout();                        // the one About / Credits item (licences, authors, source links)
    static juce::String creditsText();
    void loadFiles (const juce::StringArray& files, bool asPartner);
    void chooseFile (bool asPartner);
    void playWave (int slot);
    void dragOut();
    // TRIM (build 16): from / to are 0..1 of the sound's length. The original: what is kept becomes the new original that
    // everything works on. The processed sound: the result only. Each is one undoable step.
    void trimWave (bool processedWave, float from, float to);
    juce::Component& waveComponent (bool processedWave);   // (tests: the strip, to drag its handles)
    void setDeep (bool on);
    void toast (const juce::String& msg, bool bad = false);
    void hideToast();   // (screenshots)
    void refresh();

    // ---- TERRAIN: the landscape page (ghosty-pro/Shared/Terrain)
    void setTerrainPage (bool on);
    bool terrainPageOn() const { return terrainPage; }
    void renderTerrain();                    // the full render of the landscape onto the sound underneath (one history step)
    void terrainListen();                    // LISTEN: play the sound through the live bank (toggles)
    ghost::terrain::TerrainView& terrainView();

    // ---- UNDER THE HOOD: the CDP processes with all their own settings
    void setHoodPage (bool on);
    bool hoodPageOn() const { return hoodPage; }
    HoodPage& hood();
    void runHood (const Step& s);

    // ---- THE ROOMS (build 12): GROW, STRETCH, REBORN, SPACE each have their own page (RoomPage.h)
    void setRoom (rooms::Room r);            // Room::Lab = back to the lab
    rooms::Room roomNow() const { return roomOn; }
    RoomPage& roomPage();
    SpectrePage& spectrePage();            // build 17: SPECTRE, the page Ghost Lab opens on
    static inline bool opensOnSpectre = true;   // (the tests that drive THE LAB open there instead)
    RouteBar& routeBar();
    void runRoom (Op op);                    // STRETCH / REBORN / SPACE on the sound (running the same room again replaces its last run)
    void growRoom (bool asSecond);           // GROW: a new SPECIMEN 001 (or the second sound)
    void spaceListen();                      // SPACE's LISTEN (toggles)

    Step& settingsFor (Op op);               // what a card / deep panel uses
    bool deepOn() const { return deep; }
    bool isNarrow() const { return narrow; }
    Op selectedOp() const { return selected; }
    juce::Component& contentComponent();
    SoundBody& body();
    juce::String statusText() const;

    Processor& proc;
    Engine& engine;

private:
    void timerCallback() override;
    void layoutWide();
    void layoutNarrow();

    juce::SharedResourcePointer<theme::LabLook> look;           // our colours on JUCE's own parts
    juce::Component::SafePointer<juce::AlertWindow> openAlert;  // closed with the editor (it uses the editor's look)
    std::unique_ptr<juce::Viewport> viewport;
    std::unique_ptr<Content> content;
    std::unique_ptr<juce::Component> footer, footerLine;
    std::unique_ptr<FooterButton> undoBtn, redoBtn, genBtn, saveBtn;
    std::unique_ptr<juce::FileChooser> chooser;
    std::map<Op, Step> settings;
    Op selected = Op::Pull;
    rooms::Room roomOn = opensOnSpectre ? rooms::Room::Spectre : rooms::Room::Lab;
    bool deep = false, narrow = false, terrainPage = false, hoodPage = false, terrainPending = false;
    juce::uint32 seed = 1;
    friend class Content;
};
}
