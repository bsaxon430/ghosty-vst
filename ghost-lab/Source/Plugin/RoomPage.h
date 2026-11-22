#pragma once
// THE ROOMS' pages (build 12): one page per room, all built the same way so you learn one and know them all:
//   left    what this room does, in one line, and the choices (which body, which instrument, a starting point)
//   middle  THE PICTURE: your sound BEFORE and AFTER (time across, low notes at the bottom, high at the top, brighter =
//           louder), with what the room is doing drawn on it
//   right   a few big controls and the one big button that runs it
// Every run is one step in the lab's history (UNDO takes it back). Running the same room again replaces its last run,
// so you can tweak without stacking. AGPL-3.0-only (see LICENSE, NOTICE.md).
#include "../Core/Engine.h"
#include "Theme.h"

namespace glab
{
class Editor;

// the strip under the banner: the rooms, in the order a sound usually goes through them
class RouteBar : public juce::Component
{
public:
    rooms::Room current = rooms::Room::Lab;
    std::function<void (rooms::Room)> onPick;
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    juce::Rectangle<float> chip (int i) const;       // (tests)
    static constexpr int kChips = 5;
    static rooms::Room roomAt (int i);                // the order on the strip: SPECTRE, THE LAB, STRETCH, REBORN, SPACE
private:
    int hover = -1;
};

// build 13: each room's page is laid out like the original plug-in's own window, with the original's own control names
// (and one line in plain words under each):
//   STRETCH  PaulXStretch: the render button and info line on top, its parameter groups stacked in its order (each
//            spectral module's group has its own on / off switch), its processing-chain strip, the sound display
//   REBORN   DDSP-VST: the model list on top, the Pitch / Volume box in the middle (the model's range, which you can
//            drag, and the sound's own notes as dots), its knobs along the bottom
//   SPACE    Pertylizer's rack: the Spatial Panner module wired into the Reverb module, each a panel of its own knobs
class RoomPage : public juce::Component, private juce::Timer
{
public:
    explicit RoomPage (Editor&);
    ~RoomPage() override;

    void setRoom (rooms::Room r);
    rooms::Room room() const { return now; }
    void soundChanged();                   // the lab's sound or history changed: the pictures follow
    static juce::Colour colourOf (rooms::Room r);

    void paint (juce::Graphics&) override;
    void resized() override;

    // ---- what the tests (and the page's own parts) press. Controls go by the engine's own key ("pxs.fftsize0").
    struct Bar; struct Knob;
    juce::StringArray controlIds() const;
    juce::String controlName (const juce::String& id) const;   // the original's own name, as shown
    bool controlEnabled (const juce::String& id) const;
    float value (const juce::String& id) const;
    void setValue (const juce::String& id, float v);
    void setModule (int module, bool on);  // STRETCH: one of the original's nine spectral module switches
    bool moduleOn (int module) const;
    juce::Array<int> chainOrder() const;   // STRETCH: the processing chain (which module sits at each of its nine places)
    void moveInChain (int from, int to);   // drag a module to another place in the chain
    void addFreeFilterNode (double x, double y);   // STRETCH: a point on the Free filter curve (x 0..1 low to high, y 0..1 quiet to loud)
    bool freeFilterDrawn() const;
    void scrollToGroup (const juce::String& title);   // STRETCH: bring one of its groups to the top of the list
    void pickInstrument (int index);       // REBORN: the model list
    int instrument() const;
    void pressRun();                       // the big button
    void pressListen();                    // SPACE's LISTEN
    void pressAuto();                      // REBORN's AUTO
    juce::String runLabel() const;
    juce::String verdict() const;          // REBORN: what the box says about the tune and the instrument's range
    bool listening() const;
    void pushSpace();                      // the modules' controls -> the live space engine

private:
    class Picture; class RunButton; class Groups; class Chain; class RangeBox; class Pad; class Module;
    Editor& ed;
    rooms::Room now = rooms::Room::Stretch;
    Op op() const { return rooms::info (now).op; }
    Step& step();
    void rebuild();
    void refreshPictures();
    void refreshEnabled();
    void timerCallback() override;

    std::unique_ptr<Picture> picture;
    std::unique_ptr<RunButton> runBtn, listenBtn, autoBtn;
    std::unique_ptr<Groups> groups;            // STRETCH: the stacked parameter groups (in a viewport)
    std::unique_ptr<juce::Viewport> groupView;
    std::unique_ptr<Chain> chain;              // STRETCH: the processing-chain strip
    std::unique_ptr<juce::ComboBox> modelList; // REBORN
    std::unique_ptr<RangeBox> rangeBox;        // REBORN
    std::unique_ptr<Pad> pad;                  // SPACE
    std::unique_ptr<Module> panner, reverb;    // SPACE
    juce::OwnedArray<Knob> knobs;              // REBORN's bottom row
    juce::Rectangle<float> titleR, doesR, infoR, noteR, knobsR, wireR;
    const Audio* melodyOf = nullptr;
    rooms::Melody tune;
    float earL = 0.0f, earR = 0.0f;
    friend class Picture; friend class Groups; friend class Chain; friend class RangeBox; friend class Pad; friend class Module;
};
}
