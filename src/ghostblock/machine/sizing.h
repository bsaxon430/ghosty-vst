/*
 * Poltergeist 2 (GhostBlock) - the machine editor's window size: one fixed aspect (the design canvas, 1040 x 744),
 * 75 % to 200 %, never bigger than the screen it is on. Shared by the editor (its brass corner grip and the size menu)
 * and the plugin's CLAP gui extension (can_resize / get_resize_hints / adjust_size), so the host and the grip agree.
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <juce_gui_basics/juce_gui_basics.h>

namespace ghostblock::p2::machine::sizing
{
constexpr int W = 1040, H = 744;           // 130 : 93
constexpr double kMin = 0.75, kMax = 2.0;  // of the design size
constexpr int kHostChrome = 40;            // a host's own title bar round the plugin (FL Studio's wrapper window): kept free on the screen
constexpr int kSteps[] = {75, 100, 125, 150, 200}; // the size menu

// how big it may get on the display that holds `c` (or the main display): the screen's user area less the host's title bar.
// Never under 100 % (the size it always had) and never over 200 %.
inline double displayCap(const juce::Component *c)
{
    const auto &ds = juce::Desktop::getInstance().getDisplays();
    const juce::Displays::Display *d = nullptr;
    if (c != nullptr && c->isShowing())
        d = ds.getDisplayForRect(c->getScreenBounds());
    if (d == nullptr)
        d = ds.getPrimaryDisplay();
    if (d == nullptr)
        return kMax;
    const auto ua = d->userBounds.toNearestInt();
    const double cap = std::min((double)ua.getWidth() / W, (double)(ua.getHeight() - kHostChrome) / H);
    return std::clamp(cap, 1.0, kMax);
}

inline int widthFor(double s) { return (int)std::lround(W * s); }
inline int heightForWidth(int w) { return (int)std::lround((double)w * H / W); }

// The CLAP adjust_size rule: the biggest window of the right shape inside (w, h), between 75 % and `cap`.
// Stable: adjusting its own answer gives the same answer (the height always follows from the width).
inline void adjust(uint32_t &w, uint32_t &h, double cap)
{
    const int wMin = (int)std::ceil(W * kMin), wMax = std::max(wMin, (int)std::floor(W * std::clamp(cap, kMin, kMax)));
    int wa = std::clamp((int)w, wMin, wMax);
    if (heightForWidth(wa) > (int)h) // too tall for the height asked: the height decides
        wa = std::clamp((int)std::floor((double)h * W / H), wMin, wMax);
    w = (uint32_t)wa;
    h = (uint32_t)heightForWidth(wa);
}

// the scale a window of this size shows the machine at (the shape is kept: the smaller of the two ratios)
inline double scaleOf(int w, int h) { return std::max(0.05, std::min((double)w / W, (double)h / H)); }
} // namespace ghostblock::p2::machine::sizing
