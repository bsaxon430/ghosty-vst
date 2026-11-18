#pragma once
#include "GhostThemes.h"
// =====================================================================================================================
// INTEGRATION NOTE (for the Ghosty Spectrum owner) - the VOICE page is self-contained and NOT yet in the plugin target.
//   1. CMake: configure with -DGHOSTY_VOICE=ON (fetches WORLD v1.0.1, modified BSD, pinned zip + SHA256; offline builds
//      set FETCHCONTENT_SOURCE_DIR_WORLD=/root/vendor/World-1.0.1), then
//          target_sources(GhostySpectrum PRIVATE ${GHOSTY_VOICE_SOURCES})
//          target_link_libraries(GhostySpectrum PRIVATE ghosty-world)
//          target_compile_definitions(GhostySpectrum PRIVATE JUCE_USE_MP3AUDIOFORMAT=1)   // the page offers .mp3 drops
//      (SpectrumX/Voice.cmake is already included from CMakeLists.txt; nothing is fetched or built while the option is OFF.)
//   2. Licence (WORLD clause 2 - required for binary distribution):
//        * in the product: the page's "i" button shows voice::thirdPartyNotice() (done, nothing to wire);
//        * in the installer: ship packaging/third_party/WORLD-LICENSE.txt. The lines to add to GhostBlock.iss and
//          build-mac-pkg.sh are in packaging/third_party/README.txt (existing packaging files were not edited);
//        * release checklist: "WORLD licence present in the installer + about panel".
//   3. Processor: own one `voice::VoiceHost voice;` (it starts its own low-priority worker; analysis uses 2-4 threads).
//          prepareToPlay:   voice.prepare(sampleRate, samplesPerBlock);
//          processBlock:    voice.process(buffer);          // ADDS the page's preview (result / A / B, A/B, loop) to the output
//          getState:        state.appendChild(voice.toTree(), nullptr);              // tree type "VOICE"
//          setState:        if (auto v = state.getChildWithName("VOICE"); v.isValid()) voice.fromTree(v);
//      process() is lock-free and allocation-free; toTree() encodes the sources once (FLAC 16/24-bit, cached per sound).
//      Memory: analyses are compact (~0.4 MB per second of audio), the undo history keeps at most 2 extra analyses and
//      ~768 MB of audio; sources are capped at 5 minutes (a banner says when a file was trimmed).
//   4. Editor: add a tab that shows `std::make_unique<VoicePage>(processor.voice)` filling the content area
//      (designed at 1200 x 790; it lays out at any size >= 1100 x 740). The page sets its own LookAndFeel and palette per
//      instance (light by default, DARK = the Spectrum night palette); the theme is saved via VoiceHost::uiPrefs.
//      `onLeave` (optional) is called by the page's "<" button if the tab bar wants a way back.
//      Demo / licence: page->allowExport = [&] (juce::Component* ui, const char* id) { return ghosty::exportAllowed (license, ui, id); };  (refused exports say
//      "Exporting is part of the full version"). Tooltips: page->setOwnsTooltips(false) before showing when the editor
//      already has a TooltipWindow. Theme: page->setThemeButtonVisible(false) + page->setDark(...) when Spectrum drives it.
//   5. Tests: -DGHOSTY_BUILD_VOICE_TESTS=ON builds VoiceTests (engine, host, state, undo, memory, CPU, every punch-list
//      regression, screenshots, demos).
// No existing SpectrumX file is touched by this page.
// =====================================================================================================================
// Ghosty Spectrum VOICE page: waveform + editable pitch overlay (zoom / scroll, selection, repairs), automation lanes,
// formant view + vowel chart, source A / formant source B slots (drop audio), PITCH / BORROW / CONTOUR / VOWEL / TUNE,
// DOUBLE / HARMONY voices, offline render with progress / time left / cancel, A/B + loop preview, drag the result out.
#include "VoiceHost.h"
#include <juce_gui_basics/juce_gui_basics.h>

class VoicePage : public juce::Component, public juce::FileDragAndDropTarget, public juce::DragAndDropContainer, private juce::Timer {
public:
    explicit VoicePage(voice::VoiceHost& host);
    ~VoicePage() override;
    std::function<void()> onLeave;
    // Demo gate - asked with its id ("spec.voice.*") before every export (drag WAV / drag SET / RENDER SET / the source's WAV grip).
    // Wire it to ghosty::exportAllowed (licence, ui, id). null = refused (fail closed).
    std::function<bool(juce::Component* ui, const char* id)> allowExport;
    // false = the host shows tooltips itself, so the page never creates its own TooltipWindow (set before showing)
    bool ownsTooltips = true;
    void setOwnsTooltips(bool owns);
    bool hasOwnTooltipWindow() const;
    // hide the page's DARK / LIGHT button when the host drives the theme via setDark()
    void setThemeButtonVisible(bool visible);
    bool themeButtonVisible() const;
    void parentHierarchyChanged() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void fileDragEnter(const juce::StringArray&, int x, int y) override;
    void fileDragMove(const juce::StringArray&, int x, int y) override;
    void fileDragExit(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int x, int y) override;
    // also used by the tests / screenshots
    void setMode(int mode);
    void setDark(bool dark);
    void setLook(ghosty::themes::Theme t);       // the house look (BLACK / PURPLE / LIGHT); Spectrum drives it
    bool isDark() const;
    void inspectAt(double seconds);       // the frame the formant view shows (hover does this too)
    void refreshNow();                    // pull everything from the host now
    void showAbout(bool show);
    juce::String aboutText() const;
    void setKnob(const juce::String& name, double value, bool fine);   // as if turned by hand (fine = Shift / Alt)
    juce::String knobText(const juce::String& name) const;
    juce::Range<double> visibleTime() const;
    void zoomTime(double factor, double centreSeconds);
    void scrollTime(double seconds);
    void selectForTest(double t0, double t1);
    juce::Rectangle<int> boundsOf(const juce::String& name) const;
    bool hintFits() const;
    juce::String formantLabel() const;
    double wavePaintMs(int paints);
    juce::StringArray exportForTest(int which);   // 0 drag WAV, 1 drag SET, 2 RENDER SET (octaves), 3 RENDER SET to Shared Sounds
    juce::Component& sourceWave();                // the START / END / LOOP waveform (a ghosty::WaveRegion)   // 0 drag WAV, 1 drag SET, 2 RENDER SET (octaves)
    juce::String lastMessage() const;
    struct Impl;
private:
    void timerCallback() override;
    voice::VoiceHost& host;
    std::unique_ptr<Impl> impl;
};
