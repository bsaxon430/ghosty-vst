// Ghost Voice - AGPL-3.0-only (see LICENSE, NOTICE.md).
//
// BUBBLES: a soap bubble blooms where a button or a switch is pressed, then pops. Brandon asked for the look of ReactDrops'
// "Bubble UI" in our colours; its licence (ReactDrops Component License 1.0) does not allow its source inside an open-source
// product that ships its source, so this is our own small effect, written from the physics, not from that code:
//   - thin-film interference: a soap film's colour comes from its thickness (light reflected from the front and the back of
//     the film interferes; the path difference is 2 n d). Here the "thickness" runs round the rim, swirls with time and drains
//     downward (thicker at the bottom), and is read through OUR palette (violet -> purple -> acid green), not the spectrum;
//   - a glassy, almost clear middle; an upright window reflection at the top left and a small inverted one at the bottom right;
//   - a little squash on the press that springs back, a grow, a fade, a pop.
// Light: an overlay that draws only while a bubble lives (a 60 Hz timer that stops itself), at most 6 bubbles, never takes the
// mouse. Off when macOS "Reduce motion" is on, or with BUBBLES off in SETTINGS.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <vector>

namespace gv
{
class BubbleFx : public juce::Component, private juce::Timer   // (a Component is already a MouseListener: the editor adds it as one)
{
public:
    BubbleFx() { setInterceptsMouseClicks (false, false); setAlwaysOnTop (true); reduceMotion = systemReducesMotion(); }
    bool enabled = true;
    juce::Colour c0 { 0xff8b5cf6 }, c1 { 0xffd77bff }, c2 { 0xffa6f23a };   // violet, purple, acid green
    std::function<bool (juce::Component*)> wants;                          // which pressed components bloom (the host decides)
    int alive() const { return (int) bubbles.size(); }

    void spawn (juce::Point<float> at, float size)
    {
        if (! enabled || reduceMotion) return;
        if (bubbles.size() >= 6) bubbles.erase (bubbles.begin());
        bubbles.push_back ({ at, juce::jlimit (18.0f, 90.0f, size), 0.0f, (float) juce::Random::getSystemRandom().nextDouble() * 6.283f });
        startTimerHz (60);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.eventComponent == nullptr || getParentComponent() == nullptr || (wants && ! wants (e.eventComponent))) return;
        const auto p = getParentComponent()->getLocalPoint (e.eventComponent, e.position);
        spawn (p, (float) juce::jmin (e.eventComponent->getHeight(), 70) * 0.9f);
    }
    void paint (juce::Graphics& g) override
    {
        for (auto& b : bubbles) paintBubble (g, b);
    }
    static bool systemReducesMotion()
    {
       #if JUCE_MAC
        juce::ChildProcess p;
        if (p.start (juce::StringArray { "/usr/bin/defaults", "read", "com.apple.universalaccess", "reduceMotion" }))
            return p.readAllProcessOutput().trim() == "1";
       #endif
        return false;
    }
    bool reduceMotion = false;

private:
    struct Bubble { juce::Point<float> at; float size, t, seed; };
    std::vector<Bubble> bubbles;
    static constexpr float kLife = 0.62f;   // seconds

    juce::Colour film (float thickness) const
    {   // our palette across one "order" of the film, wrapping (a film shows its colours in repeating bands)
        float x = thickness - std::floor (thickness);
        x *= 3.0f;
        if (x < 1.0f) return c0.interpolatedWith (c1, x);
        if (x < 2.0f) return c1.interpolatedWith (c2, x - 1.0f);
        return c2.interpolatedWith (c0, x - 2.0f);
    }
    void paintBubble (juce::Graphics& g, const Bubble& b) const
    {
        const float u = juce::jlimit (0.0f, 1.0f, b.t / kLife);
        const float grow = 0.55f + 0.6f * (1.0f - std::pow (1.0f - u, 3.0f));            // ease out
        const float fade = u < 0.55f ? 1.0f : 1.0f - (u - 0.55f) / 0.45f;
        const float squash = 1.0f - 0.10f * std::exp (-u * 9.0f) * std::cos (u * 26.0f);  // the press squash, springing back
        const float r = b.size * 0.5f * grow, rx = r / std::sqrt (squash), ry = r * squash;
        const float cx = b.at.x, cy = b.at.y;
        // the glassy body: almost clear
        g.setColour (juce::Colours::white.withAlpha (0.035f * fade)); g.fillEllipse (cx - rx, cy - ry, 2 * rx, 2 * ry);
        // the film round the rim: thickness swirls with time and drains downward
        const int n = 40;
        for (int k = 0; k < n; ++k)
        {
            const float a0 = juce::MathConstants<float>::twoPi * (float) k / (float) n, a1 = a0 + juce::MathConstants<float>::twoPi / (float) n + 0.01f;
            const float am = 0.5f * (a0 + a1);
            const float thick = 0.35f + 0.22f * std::sin (3.0f * am + b.seed + 4.0f * b.t) + 0.18f * std::sin (5.0f * am - 3.0f * b.t + b.seed * 0.7f)
                              + 0.25f * (0.5f + 0.5f * std::sin (am)) + 0.6f * b.t;              // + drainage (sin > 0 = lower half), + thinning over its life
            const float rim = 0.55f + 0.45f * std::abs (std::sin (am + b.seed));
            juce::Path arc; arc.addCentredArc (cx, cy, rx * 0.9f, ry * 0.9f, 0.0f, a0 + juce::MathConstants<float>::halfPi, a1 + juce::MathConstants<float>::halfPi, true);
            g.setColour (film (thick).withAlpha (0.6f * rim * fade));
            g.strokePath (arc, juce::PathStrokeType (juce::jmax (1.2f, r * 0.16f), juce::PathStrokeType::curved, juce::PathStrokeType::butt));
        }
        // the outer edge
        g.setColour (juce::Colours::white.withAlpha (0.35f * fade)); g.drawEllipse (cx - rx, cy - ry, 2 * rx, 2 * ry, 0.8f);
        // reflections: an upright window top left, a small inverted one bottom right
        juce::Path w1; w1.addCentredArc (cx, cy, rx * 0.72f, ry * 0.72f, 0.0f, -2.35f, -1.15f, true);
        g.setColour (juce::Colours::white.withAlpha (0.55f * fade)); g.strokePath (w1, juce::PathStrokeType (juce::jmax (1.0f, r * 0.07f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        juce::Path w2; w2.addCentredArc (cx, cy, rx * 0.66f, ry * 0.66f, 0.0f, 1.0f, 1.6f, true);
        g.setColour (juce::Colours::white.withAlpha (0.25f * fade)); g.strokePath (w2, juce::PathStrokeType (juce::jmax (0.8f, r * 0.045f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    void timerCallback() override { advance (1.0f / 60.0f); }
public:
    // one step of the bubbles' life (the timer calls it; tests too)
    void advance (float dt)
    {
        juce::Rectangle<int> dirty;
        for (auto& b : bubbles) { dirty = dirty.getUnion (juce::Rectangle<float> (b.at.x - b.size, b.at.y - b.size, 2 * b.size, 2 * b.size).getSmallestIntegerContainer()); b.t += dt; }
        bubbles.erase (std::remove_if (bubbles.begin(), bubbles.end(), [] (const Bubble& b) { return b.t > kLife; }), bubbles.end());
        repaint (dirty.expanded (4));
        if (bubbles.empty()) stopTimer();
    }
};
}
