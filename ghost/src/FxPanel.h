#pragma once
// GHOST - Poltergeist 2's SPECTRAL rack page (4 bays + WARP), used by GHOST's lanes and by the GHOST FX plug-in
#include "Widgets.h"
#include "ghostblock/spectral/spectral-fx.h"
#include <array>
#include <functional>

namespace ghost
{
namespace sfx = ghostblock::p2::spectral;

// Poltergeist 2's FX page for one lane: 4 bays (Spectral Corruptor's 38 effects) then WARP (WarpCore), in that rack's order
struct FxPanel : juce::Component, private juce::Timer
{
    FxPanel (sfx::SpectralFx& rack, const juce::String& title, std::function<void()> structuralChange);
    ~FxPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void rebuild();            // after the effect in a bay changes: its knobs
    void sync();               // from the rack (automation, a loaded song)
    std::function<void()> onClose;

private:
    void timerCallback() override;
    sfx::SpectralFx& fx() { return rack; }
    // a switch / an effect / the FFT: the rack rebuilds what changed and GHOST lines up again
    void set (uint32_t id, double v) { fx().setParam (id, v); if (changed) changed(); lastChange = fx().changeCount(); }
    // a knob being turned: only the value (the audio picks it up at once; nothing is rebuilt, the window isn't redrawn)
    void turn (uint32_t id, double v) { fx().setParam (id, v); lastChange = fx().changeCount(); }
    void pickEffect (int bay);
    sfx::SpectralFx& rack; juce::String title; std::function<void()> changed;
    juce::TextButton close { "CLOSE" }, onBtn { "FX ON" }, warpOn { "WARP ON" }, order { "BAYS > WARP" }, fftBtn { "FFT 2048" };
    Knob mix { "MIX" }, chainMix { "BAYS MIX" };
    struct Bay
    {
        juce::TextButton effect { "EMPTY" }, on { "ON" };
        Knob mix { "MIX" };
        juce::OwnedArray<Knob> knobs;
        juce::OwnedArray<juce::TextButton> choices;   // (a choice / on-off parameter is a button that steps)
    };
    std::array<Bay, sfx::kSlots> bays;
    juce::OwnedArray<Knob> warpKnobs;
    std::array<int, sfx::kSlots> shownType { -1, -1, -1, -1 };
    uint32_t lastChange = 0;
    bool syncing = false;
};

} // namespace ghost
