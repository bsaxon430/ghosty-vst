/*
 * Ghost Bass - a bass instrument built on Surge XT.
 * Copyright 2026 the Ghost Bass authors. Based on Surge XT by the Surge Synth Team.
 * Released under the GNU General Public Licence v3 or later (see LICENSE).
 *
 * EASY mode: the Ghost Bass screen that sits on top of Surge's own editor (DEEP mode).
 *   top      the sound picker (a Serum-style drop-down on the patch name: folders as submenus, a search box, < > to step
 *            in the folder), SAVE, ENGINE (opens DEEP mode), About
 *   centre   pages: BLEND BOARD (2 to 4 sounds in the corners, a puck that blends them), TERRAIN (with SHAKE), MORPH,
 *            SUB, LOCK (GHOST LOCK), SEQUENCER
 *   right    the six big macros SUB GRIT BITE MOTION WIDTH SPACE
 *   bottom   the ENGINE and SUB layer strip, and the playable keyboard
 * Everything here drives real Surge parameters, macros or the GhostEngine; nothing is decoration only.
 */
#ifndef GHOSTBASS_SRC_GHOST_UI_GHOSTEASYVIEW_H
#define GHOSTBASS_SRC_GHOST_UI_GHOSTEASYVIEW_H

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class SurgeSynthEditor;
class SurgeSynthProcessor;
namespace ghost::terrain
{
class TerrainView;
class TerrainEditor;
} // namespace ghost::terrain

namespace ghost::ui
{
class EasyView : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
  public:
    EasyView(SurgeSynthEditor *ed, SurgeSynthProcessor &proc);
    ~EasyView() override;

    void paint(juce::Graphics &g) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray &files) override;
    void filesDropped(const juce::StringArray &files, int x, int y) override;

    std::function<void()> onOpenEngine; // switch to DEEP mode

    // pages
    enum Mode
    {
        M_BOARD = 0, // the main page: blend board
        M_TERRAIN,
        M_MORPH,
        M_SUB,
        M_LOCK, // GHOST LOCK
        M_SEQ,  // the bass sequencer
        n_modes
    };
    void setMode(int m);
    int mode() const;
    // TERRAIN (tests / screenshots): the page's view and editor, and a re-read of the engine's landscape
    ghost::terrain::TerrainView &terrainView();
    ghost::terrain::TerrainEditor &terrainEditor();
    void terrainSync();
    // screenshots: pump the pages' timers (no message loop)
    void tickForShot();
    // screenshots: show the light (DAWN) or dark (NIGHT) look without remembering it; returns the look it was in
    bool setThemeForShot(bool light);

    // the sound picker (tests / screenshots)
    void openMenuForShot(int slot = -1); // -1: the header's drop-down; 0..3: a blend-board slot's
    void closeMenuForShot();
    bool menuOpen() const;
    void menuSearchForShot(const juce::String &q);
    int menuResultCountForShot() const;
    std::vector<std::pair<std::string, int>> typeCountsForShot(); // the picker's types and how many sounds each lists
    bool typesPartitionForShot();                                 // every sound is in exactly one type
    static std::string typeNameForShot(const std::string &folder, const std::string &name);
    int libraryCountForShot();                 // how many patches the picker lists
    double indexBuildMsForShot();              // a fresh index build (ms)
    void librarySyntheticForShot(int n);       // replace the picker's library by n made-up sounds (speed tests)
    int indexBuildsForShot() const;            // how many times the library was indexed (opening the picker must not re-index)
    // the blend board (tests / screenshots): slot 0..3 = a patch file, or empty path = "CURRENT SOUND"; the puck 0..1
    bool boardSetSlotForShot(int slot, const std::string &path, const std::string &name);
    void boardClearForShot();
    void boardSetPuckForShot(float x, float y);
    void boardWeightsForShot(float *w4);       // the weights shown now (the filled slots add to 1)
    float boardSpectrumPeakForShot(int src); // the frequency view: the tallest bar (0..1) of a sound (0-3) or the SUB (4)
    int boardSlotsForShot() const;             // how many slots are filled (the board's own state, from the patch)
    std::string boardSlotNameForShot(int slot) const;
    // every visible control's bounds (leaf components) in canvas coordinates, for the layout test
    struct ControlBounds
    {
        std::string name;
        juce::Rectangle<float> r;
    };
    std::vector<ControlBounds> controlBounds();
    juce::Rectangle<float> canvasBoundsInView() const; // where the canvas sits in this component

    static constexpr int W = 1200, H = 760;                 // design canvas, scaled to the editor (it is also the default editor size)
    static constexpr int kMinW = 960, kMinH = 600, kMaxW = 1700, kMaxH = 1000; // the editor's resize limits

    struct Impl;

  private:
    void timerCallback() override;
    std::unique_ptr<Impl> impl;
    // everything lives here at W x H, scaled by a transform; it paints the GhostBlock background
    struct Canvas : public juce::Component
    {
        void paint(juce::Graphics &g) override;
        void paintOverChildren(juce::Graphics &g) override; // build 13: paper grain over everything
    } canvas;
};
} // namespace ghost::ui

#endif
