#pragma once
// Ghosty Pro's window: the Ghosty Pro header and tabs over
//   SYNTH   Vital's own full editor (FullInterface, unchanged code) in Ghosty Pro's skins: oscillators, wavetable
//           editor, filters, envelopes, LFOs, modulation, effects - the way the synth works is Vital's
//   MORPH   the SPIRIT BOARD (ours)
//   (presets: the picker under the preset name - every preset by TYPE; folders and banks in MENU)
//   EXTRAS  GHOST GATE + HAUNT (ours, off by default)
// It is a SynthGuiInterface, as Vital's own SynthEditor is, so Vital's engine talks to Vital's editor exactly as in Vital.
#include "JuceHeader.h"
#include "synth_gui_interface.h"
#include "synth_preset_selector.h"
#include "GhostyLook.h"
#include "GhostyPages.h"
#include "VitalPresetFinder.h"
#include "GhostyMatrix.h"
#include "GhostyPresetMenu.h"

class GhostyProcessor;

class GhostyEditor : public AudioProcessorEditor, public SynthGuiInterface, private Timer {
  public:
    // default size: fits FL Studio's plug-in window on a 1440 x 900 MacBook Pro with room to spare (never wider than 1240 or
    // taller than 780). Always the default on open: the size is never remembered from another project or instance.
    // 2.0.16: the window keeps the one shape where the SYNTH page (Vital's editor, always 1400:820) fills it to the edges:
    // no empty bars at the sides, it runs to the window's left, right and bottom edge. height = header and tabs (kChromeH)
    // + the width at 1400:820.
    static constexpr int kMargin = 4, kChromeH = 116;
    static int fittedHeight(int w) { return kChromeH + (int) (((long long) w * 820 + 700) / 1400); }
    static int fittedWidth(int h) { return (int) (((long long) (h - kChromeH) * 1400 + 410) / 820); }
    static constexpr int kWidth = 1100, kHeight = kChromeH + (int) ((1100LL * 820 + 700) / 1400);   // 1100 x 760
    // Vital's editor inside an area: the largest rectangle of Vital's own shape (1400:820), centred (letterbox)
    static Rectangle<int> vitalArea(Rectangle<int> area);
    Rectangle<int> vitalBounds() const { return vitalHolder_.getBounds(); }
    ghosty::MorphPage* morphPage() const { return dynamic_cast<ghosty::MorphPage*>(pages_[0].get()); }
    Component* vitalHolder() { return &vitalHolder_; }
    Component* vitalEditor() const { return (Component*) gui_.get(); }
    static constexpr int kNumTabs = 3;
    static String ghostyAboutText();   // MENU > About / Credits (the one place upstream projects are named)
    explicit GhostyEditor(GhostyProcessor& p);
    ~GhostyEditor() override;

    void paint(Graphics&) override;
    void resized() override;
    void updateFullGui() override;

    void openMenu(int which) { if (which == 0) presetMenu(); else if (which == 1) mainMenu(); else searchPresets(); }   // (screenshots, tests)
    void showPage(int index);   // 0 SYNTH (Vital's editor) 1 MORPH 2 EXTRAS
    int page() const { return page_; }
    void setLookIndex(int look);       // 0 GHOST, 1 BLACK, 2 LIGHT
    ghosty::VitalPresetFinder& finder() { return finder_; }
    ghosty::MatrixRow& matrix() { return matrix_; }
    const ghosty::MenuIndex& menuIndex() { if (indexDirty_) rebuildIndex(); return index_; }   // (tests)
    const ghosty::PresetCatalog& catalog() { if (indexDirty_) rebuildIndex(); return catalog_; }   // (tests)
    void stepPresetForTest(int delta) { stepPreset(delta); }
    Rectangle<int> ghostBox() const { return rGhost; }   // the box the header ghost must stay inside (tests)
    void visibilityChanged() override;

  private:
    void timerCallback() override;
    void ensureVitalGui();
    void applySkin();
    void stepPreset(int delta);
    void presetMenu();
    void scanFinished();
    void searchPresets();
    void mainMenu();
    void loadFound(int index);
    void refreshTempo();        // the TEMPO buttons and the tempo they give
    void rescanPresets();       // look for presets on this computer again (the picker's lists follow)
    void loadEntry(int entry);   // one preset of the picker's catalog (ours or a file)
    int currentEntry();          // the loaded preset in the catalog, or -1
    void rebuildIndex();
    void withVital(std::function<void(SynthPresetSelector&)> action);
    void savePreset();
    void openPreset();
    void soundChanged();
    ghosty::Page* ourPage(int tab) const { return tab >= 1 && tab < kNumTabs ? pages_[tab - 1].get() : nullptr; }

    GhostyProcessor& proc_;
    ghosty::LnF lnf_;
    ghosty::VitalPresetFinder finder_;
    std::unique_ptr<ghosty::Page> pages_[kNumTabs - 1];
    Component vitalHolder_;
    std::unique_ptr<MidiKeyboardComponent> keyboard_;
    ghosty::Pill prev_, next_, name_, save_, load_, init_, menu_, look_, bypass_, tabs_[kNumTabs];
    // resizing (the corner, or the host): the window keeps the SYNTH page's shape (see kWidth); dragging only the top or
    // bottom edge sets the width from the height, anything else sets the height from the width
    struct FitConstrainer : ComponentBoundsConstrainer {
      void checkBounds(Rectangle<int>& b, const Rectangle<int>& prev, const Rectangle<int>& limits,
                       bool top, bool left, bool bottom, bool right) override;
    } fit_;   // the look under SKETCH (what the LOOK button cycles, and what comes back when SKETCH goes off)
    ghosty::MenuIndex index_;      // the preset drop-down's folder tree (built once per scan)
    bool indexDirty_ = true;
    Component::SafePointer<ghosty::PresetPicker> openPicker_;   // the preset picker while it is open (refreshed when the search ends)
    ghosty::PresetCatalog catalog_;   // every preset by TYPE (the preset picker and the < > arrows)
    int pickerType_ = ghosty::kBass;  // the type the < > arrows step through
    // KEYS on every page, next to the tabs: ARP and HOLD switch, CHORDS / SCALE LOCK pick (full settings on EXTRAS)
    ghosty::Pill keyArp_, keyHold_, keyChord_, keyScale_;
    // TEMPO: 1/2x | 1x | 2x and the tempo it gives (host tempo x the multiple)
    ghosty::Pill tempo_[3];
    Label tempoBpm_;
  public:
    ghosty::Pill& tempoButton(int i) { return tempo_[i]; }   // (tests)
    String tempoText() const { return tempoBpm_.getText(); }
  private:
    void keysMenu(bool scale);
    Label version_;
    TooltipWindow tips_ { this, 600 };
    std::unique_ptr<FileChooser> chooser_;
    int page_ = 0, epoch_ = -1;
    Rectangle<int> rHeader, rTabs, rBody, rKeys, rGhost, rTag;
    ghosty::MatrixRow matrix_;
    std::vector<std::unique_ptr<FullInterface>> retired_;   // engine editors replaced by a look change (deleted on the next turn)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GhostyEditor)
};
