// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST ATLAS - the four pages (Play, Layers, Effects, Library) and what they share with the screen around them.
#pragma once
#include "Stage.h"
#include "Widgets.h"
#include "../plugin/Processor.h"

namespace atlas::ui
{
enum PageId { pagePlay, pageLayers, pageEffects, pageLibrary, kNumPages };

// what a page can ask of the screen around it
struct Shell
{
    explicit Shell (Processor& p) : proc (p) {}
    Processor& proc;
    juce::Component* canvas = nullptr;                    // where menus and overlays live (scaled with everything else)
    int layer = 0;                                        // the layer in hand
    FxAddress fx = FxAddress::send (2);                   // the effect slot in hand
    std::function<void (int page)> goTo;
    std::function<void (int world)> showWorld;            // sail there (the picture follows)
    std::function<void (int layer)> pickInstrument;       // open the instrument picker for a layer
    std::function<void()> soundChanged;                   // something about the sound was changed here: read it again everywhere
    std::function<void (const juce::String&)> say;        // one short line under the preset's name
    std::function<void()> savePreset;
};

class Page : public juce::Component
{
public:
    explicit Page (Shell& s) : shell (s) {}
    virtual void refresh() {}                             // the sound changed: read everything again
    virtual void tick() {}                                // thirty times a second
    virtual std::vector<Stage::Pane> panes() const = 0;   // where this page wants glass
    virtual juce::Rectangle<float> open() const { return {}; }   // the part of the picture it leaves uncovered
    virtual bool showsTitle() const { return false; }
    virtual bool secondView() const { return true; }
    virtual int worldOverride() const { return -1; }
    virtual void showFor (const juce::String&) {}          // put a named view of the page on the screen (pictures taken by the build)
protected:
    Shell& shell;
};

std::unique_ptr<Page> makePlayPage (Shell&);
std::unique_ptr<Page> makeLayersPage (Shell&);
std::unique_ptr<Page> makeEffectsPage (Shell&);
std::unique_ptr<Page> makeLibraryPage (Shell&);

// an overlay sits over the page, on a veiled picture, until it is closed
class Overlay : public juce::Component
{
public:
    std::function<void()> onClose;
    void close() { if (onClose) onClose(); }
    virtual bool busy() const { return false; }           // at work on something (MUTATE): a picture of the screen waits for it
};
std::unique_ptr<Overlay> makeInstrumentPicker (Shell&, int layer);
std::unique_ptr<Overlay> makeSaveDialog (Shell&);
std::unique_ptr<Overlay> makeMutatePanel (Shell&);

// ---- shared by the pages
void bindLayer (Control&, Shell&, int layer, int param);                 // a layer control from the engine's own table
juce::String shortName (const juce::String& instrumentId, Processor&);   // an instrument's name for a small space
float matchedLevel (const Instrument&);                                  // the level a new layer starts at, so sounds arrive at a like loudness
void headingAt (juce::Graphics&, const juce::String& text, float x, float y, float w = 300.0f);
} // namespace atlas::ui
