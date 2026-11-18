#pragma once
// Ghosty Pro's pages. Every knob drives one of Vital's own controls (GhostyLook.h VKnob); nothing here makes sound.
#include "JuceHeader.h"
#include "GhostyLook.h"
#include "VitalPresetFinder.h"
#include "SpiritBoard.h"
#include <memory>
#include <vector>

class GhostyProcessor;

namespace ghosty {

class Page : public Component {
  public:
    explicit Page(GhostyProcessor& p) : proc(p) { }
    virtual void sync() { }           // ~30 times a second while shown
    virtual void soundChanged() { }   // after a preset load
    std::function<void(int)> showPage; // ask the editor to switch page (0 SYNTH 1 MORPH 2 EXTRAS)
  protected:
    GhostyProcessor& proc;
    std::vector<std::unique_ptr<VKnob>> knobs;
    std::vector<std::unique_ptr<VSwitch>> switches;
    std::vector<std::unique_ptr<VChoice>> choices;
    VKnob& knob(const std::string& control, const String& caption);
    VSwitch& toggle(const std::string& control, const String& text);
    VChoice& choice(const std::string& control);
    void syncAll();
    // lays out knobs in a row inside r, each 'w' wide (centred)
    static void row(Rectangle<int> r, const std::vector<Component*>& items, int w);
};

// GHOSTY EXTRAS: GHOST GATE and HAUNT (Ghosty Pro's own; off by default)
class ExtrasPage : public Page {
  public:
    explicit ExtrasPage(GhostyProcessor& p);
    void paint(Graphics&) override;
    void resized() override;
    void sync() override;
  private:
    Pill gateOn, hauntOn, gatePat[16], gateLen[3], hauntPad[16];
    Label gateNote, hauntNote;
    Rectangle<int> rGate, rHaunt;
    // KEYS: ARPEGGIATOR / HOLD / CHORDS / SCALE LOCK (MIDI in front of Vital)
    Pill arpOn, holdOn;
    ComboBox arpRate, arpMode, chordPick, keyPick, scalePick;
    ghostorbit::OrbitKnob arpOct, arpGate;
    Rectangle<int> rKeys, rArp, rChord, rScale, rArpKnobs;
    int lastArpCount = 0;
    float arpFlash = 0.0f;
    bool compact_ = false;   // a short window: tighter rows, the notes hidden
};

class MorphPage : public Page {
  public:
    explicit MorphPage(GhostyProcessor& p);
    void paint(Graphics&) override;
    void resized() override;
    void sync() override;
    void soundChanged() override { }
    std::function<std::vector<FoundPreset>()> foundPresets;   // the MY VITAL PRESETS list, from the editor
    Point<float> shownPuck() const { return board_->shownPuck; }   // (tests)
  private:
    void addGhostFromCurrent(float x, float y);
    void ghostMenu(int ghost);
    void addFromPresetMenu();
    void setsMenu();   // MORPH SETS
    void edited();
    std::unique_ptr<spirit::BoardView> board_;
    Pill on_, sets_, addCurrent_, addPreset_, circle_, clear_, motionPick_[7], sync_, draw_, snap_;
    ComboBox division_, blend_;
    ghostorbit::OrbitKnob hz_, depth_, power_;   // GHOST ORBIT knobs: RATE, SIZE, PULL
    Rectangle<int> rKnobs;
    Label tips_;
    Rectangle<int> rBoard, rSide;
    // DRAW: the stroke being drawn on the board
    bool drawArmed_ = false, drawing_ = false;
    std::vector<Point<float>> stroke_;
    std::vector<double> strokeSecs_;
};


} // namespace ghosty
