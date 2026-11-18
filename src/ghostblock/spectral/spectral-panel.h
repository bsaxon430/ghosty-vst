/*
 * Poltergeist 2 (GhostBlock) - the minimal SPECTRAL page: one component the editor hosts on its fourth page.
 * It draws with the editor's own look (the LookAndFeel it inherits) and only talks to spectral-fx.h.
 */
#pragma once
#include <functional>
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>
#include "ghostblock/spectral/spectral-fx.h"

namespace ghostblock::p2::spectral
{
struct PanelHost // what the page needs from the editor
{
    std::function<void(uint32_t id, double value, int type)> edit; // a host parameter: 0 begin, 1 value, 2 end
    std::function<void()> stateDirty;                              // an editor-only setting changed
    std::function<void(const juce::String &)> status;              // the editor's status line
};
// fits 1004 x 444 (the editor's page area)
std::unique_ptr<juce::Component> makeSpectralPanel(SpectralFx &fx, PanelHost host);
} // namespace ghostblock::p2::spectral
