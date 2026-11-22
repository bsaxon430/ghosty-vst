// Ghost Lab - AGPL-3.0-only (see LICENSE, NOTICE.md). SPECTRE's page (SpectrePage.h).
#include "SpectrePage.h"
#include "Editor.h"

namespace glab
{
using namespace theme;
using namespace spectre;

static const juce::Colour orange { 0xffff8a2a }, hotPink { 0xffff4fd8 };
juce::Colour SpectrePage::accent() { return orange; }

// ---- how far along its travel a control sits (0..1), and back. SHIFT is cubed (fine near zero), PARTIALS is
// logarithmic (16, 32 ... 1024 are evenly spaced), TRANSPOSE snaps to whole semitones.
static float toNorm (int p, float v)
{
    const auto& in = param (p);
    if (p == Shift) { const float t = v / in.hi; return 0.5f + 0.5f * (t < 0 ? -std::cbrt (-t) : std::cbrt (t)); }
    if (p == Partials) return std::log (v / in.lo) / std::log (in.hi / in.lo);
    return (v - in.lo) / (in.hi - in.lo);
}
static float fromNorm (int p, float t, bool fine)
{
    const auto& in = param (p);
    t = juce::jlimit (0.0f, 1.0f, t);
    if (p == Shift) { const float u = 2.0f * t - 1.0f; return in.hi * u * u * u; }
    if (p == Partials) return (float) juce::roundToInt (in.lo * std::pow (in.hi / in.lo, t));
    const float v = in.lo + t * (in.hi - in.lo);
    if (p == Transpose && ! fine) return (float) juce::roundToInt (v);
    return v;
}
static juce::String show (int p, float v)
{
    switch (p)
    {
        case Shift:     return (v > 0.5f ? "+" : "") + juce::String (juce::roundToInt (v)) + " Hz";
        case Transpose: return (v > 0.05f ? "+" : "") + (std::abs (v - std::round (v)) < 0.01f ? juce::String (juce::roundToInt (v)) : juce::String (v, 2)) + " st";
        case Partials:  return juce::String (juce::roundToInt (v));
        case Output:    return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB";
        case Inf:       return v >= 0.999f ? "INF" : juce::String (juce::roundToInt (v * 100)) + "%";
        case PhaseX: case CepstrumY: return (v > 0.005f ? "+" : "") + juce::String (juce::roundToInt (v * 100));
        default:        return juce::String (juce::roundToInt (v * 100)) + "%";
    }
}
// a few plain words under each control (the full line shows at the bottom while your hand is on it)
static const char* shortWords (int p)
{
    switch (p)
    {
        case Shift: return "metallic slide";
        case Transpose: return "pitch, keeps the body";
        case Cloud: return "ring on, drift apart";
        case Smooth: return "changes glide";
        case Mid: return "rebuild the middle";
        case Side: return "rebuild the sides";
        case Inf: return "hold, freeze at the top";
        case Partials: return "how many sine waves";
        case Output: return "how loud";
        default: return "";
    }
}

// ================================================================================================ a button
class SpectrePage::Button : public juce::Component
{
public:
    juce::String label;
    juce::Colour colour { orange };
    bool lit = false, hot = false, solid = false;
    std::function<void()> onClick;
    void paint (juce::Graphics& g) override
    {
        const bool on = isEnabled();
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        juce::Path p; p.addRoundedRectangle (r, r.getHeight() * 0.5f);
        if (solid && on)
        {
            art::glow (g, p, colour.withAlpha (hot ? 0.85f : 0.55f), 12);
            g.setGradientFill (juce::ColourGradient (colour.brighter (hot ? 0.3f : 0.15f), 0, r.getY(), colour.darker (0.2f), 0, r.getBottom(), false));
            g.fillPath (p);
            g.setColour (juce::Colour (0xff160a02));
        }
        else
        {
            g.setColour (night0.withAlpha (0.85f)); g.fillPath (p);
            if (lit && on) { art::glow (g, p, colour.withAlpha (0.6f), 12); g.setColour (colour.withAlpha (0.28f)); g.fillPath (p); }
            else if (hot && on) { g.setColour (colour.withAlpha (0.12f)); g.fillPath (p); }
            g.setColour (colour.withAlpha (on ? (lit ? 1.0f : 0.75f) : 0.25f)); g.strokePath (p, juce::PathStrokeType (lit ? 1.8f : 1.3f));
            g.setColour (on ? text : dim);
        }
        g.setFont (font (juce::jmin (13.0f, r.getHeight() * 0.42f), 2, 0.1f));
        g.drawFittedText (label, r.reduced (10, 2).toNearestInt(), juce::Justification::centred, 1, 0.75f);
    }
    void mouseEnter (const juce::MouseEvent&) override { hot = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hot = false; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override { if (isEnabled() && getLocalBounds().contains (e.getPosition()) && onClick) onClick(); }
};

// ================================================================================================ a slider or a knob
class SpectrePage::Control : public juce::Component
{
public:
    Control (SpectrePage& o, int p, bool knob, bool big = false) : owner (o), param (p), isKnob (knob), isBig (big)
    {
        setRepaintsOnMouseActivity (true);
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    }
    SpectrePage& owner;
    const int param;
    const bool isKnob, isBig;
    juce::Colour colour = orange;
    bool active = true;   // false: greyed (does nothing right now)
    float startNorm = 0.0f;

    float norm() const { return toNorm (param, owner.value (param)); }

    void paint (juce::Graphics& g) override
    {
        const auto& in = spectre::param (param);
        const float a = active ? 1.0f : 0.28f;
        const bool hot = active && (isMouseOverOrDragging());
        auto r = getLocalBounds().toFloat();
        // name on top, value under it, the plain words at the bottom
        auto nameR = r.removeFromTop (isKnob ? 16.0f : 18.0f);
        auto wordsR = r.removeFromBottom (isKnob ? 24.0f : 30.0f);
        auto valueR = r.removeFromBottom (isKnob ? 15.0f : 18.0f);
        g.setColour (text.withAlpha (a)); g.setFont (font (isBig ? 13.0f : 11.5f, 2, 0.14f));
        g.drawFittedText (in.name, nameR.toNearestInt(), juce::Justification::centred, 1, 0.7f);
        g.setColour ((hot ? colour.brighter (0.3f) : colour).withAlpha (a)); g.setFont (font (11.5f, 1, 0.04f));
        g.drawFittedText (show (param, owner.value (param)), valueR.toNearestInt(), juce::Justification::centred, 1, 0.8f);
        g.setColour (sub.withAlpha (active ? 0.95f : 0.45f)); g.setFont (font (9.8f, 0, 0.0f));
        g.drawFittedText (active ? juce::String (shortWords (param)) : owner.greyReason (param), wordsR.reduced (2, 0).toNearestInt(), juce::Justification::centredTop, 2, 0.85f);
        const float t = norm();
        if (isKnob) paintKnob (g, r.reduced (2.0f), t, a, in.bipolar);
        else paintSlider (g, r.reduced (0.0f, 4.0f), t, a, in.bipolar);
    }

    void paintSlider (juce::Graphics& g, juce::Rectangle<float> r, float t, float a, bool bipolar)
    {
        const float w = juce::jmin (34.0f, r.getWidth() * 0.6f);
        auto track = juce::Rectangle<float> (w, r.getHeight()).withCentre (r.getCentre());
        g.setColour (night0.withAlpha (0.9f)); g.fillRoundedRectangle (track, 7.0f);
        g.setColour (border.withAlpha (a)); g.drawRoundedRectangle (track, 7.0f, 1.2f);
        // the dotted scale, like a column of LEDs
        auto in = track.reduced (6.0f, 8.0f);
        const int dots = juce::jmax (6, (int) (in.getHeight() / 9.0f));
        const float y0 = bipolar ? in.getBottom() - 0.5f * in.getHeight() : in.getBottom();
        const float yv = in.getBottom() - t * in.getHeight();
        for (int i = 0; i < dots; ++i)
        {
            const float y = in.getBottom() - (float) i / (float) (dots - 1) * in.getHeight();
            const bool lit = (yv <= y && y <= y0) || (y0 <= y && y <= yv);
            g.setColour (lit ? colour.withAlpha (0.85f * a) : dim.withAlpha (0.35f * a));
            g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre ({ in.getCentreX() - 6.0f, y }));
            g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre ({ in.getCentreX() + 6.0f, y }));
        }
        if (bipolar) { g.setColour (text.withAlpha (0.35f * a)); g.drawLine (track.getX() + 3, y0, track.getRight() - 3, y0, 1.0f); }
        // the handle
        auto h = juce::Rectangle<float> (w - 6.0f, 12.0f).withCentre ({ track.getCentreX(), yv });
        juce::Path hp; hp.addRoundedRectangle (h, 3.0f);
        if (active) art::glow (g, hp, colour.withAlpha (0.6f), 8);
        g.setColour (active ? juce::Colours::white.withAlpha (0.95f) : dim); g.fillPath (hp);
        g.setColour (colour.withAlpha (a)); g.fillRect (h.reduced (4.0f, 4.5f));
    }

    void paintKnob (juce::Graphics& g, juce::Rectangle<float> r, float t, float a, bool bipolar)
    {
        const float d = juce::jmin (r.getWidth(), r.getHeight());
        auto k = juce::Rectangle<float> (d, d).withCentre (r.getCentre());
        const float a0 = -2.35f, a1 = 2.35f, av = a0 + t * (a1 - a0), am = bipolar ? 0.0f : a0;
        const auto c = k.getCentre(); const float rad = d * 0.5f - 5.0f, thick = isBig ? 7.0f : 5.0f;
        // the dish
        g.setGradientFill (juce::ColourGradient (night2, c.x, k.getY(), night0, c.x, k.getBottom(), false));
        g.fillEllipse (k.reduced (thick + 3.0f));
        g.setColour (border.withAlpha (a)); g.drawEllipse (k.reduced (thick + 3.0f), 1.2f);
        // the track, and the value on it
        juce::Path tr; tr.addCentredArc (c.x, c.y, rad, rad, 0.0f, a0, a1, true);
        g.setColour (dim.withAlpha (0.4f * a)); g.strokePath (tr, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        juce::Path vp; vp.addCentredArc (c.x, c.y, rad, rad, 0.0f, juce::jmin (am, av), juce::jmax (am, av), true);
        if (active) art::glow (g, vp, colour.withAlpha (0.55f), 8);
        g.setColour (colour.withAlpha (a)); g.strokePath (vp, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        // INF: the dish fills as the hold rises, and reads INF at the top
        if (param == Inf)
        {
            const float lvl = owner.value (Inf);
            auto in = k.reduced (thick + 6.0f);
            juce::Path dish; dish.addEllipse (in);
            g.saveState(); g.reduceClipRegion (dish);
            const float y = in.getBottom() - lvl * in.getHeight();
            for (int i = 0; i < 7; ++i)   // a column of pixels, like a filling glass
                for (int j = 0; j < 7; ++j)
                {
                    auto cell = juce::Rectangle<float> (in.getX() + (float) i * in.getWidth() / 7.0f, in.getY() + (float) j * in.getHeight() / 7.0f, in.getWidth() / 7.0f - 1.5f, in.getHeight() / 7.0f - 1.5f);
                    if (cell.getBottom() < y) continue;
                    g.setColour (green.withAlpha (a * (0.35f + 0.6f * (float) ((i * 5 + j * 3) % 4) / 3.0f)));
                    g.fillRect (cell);
                }
            g.restoreState();
            g.setColour (text.withAlpha (a)); g.setFont (font (isBig ? 15.0f : 12.0f, 2, 0.1f));
            g.drawText ("INF", in, juce::Justification::centred);
        }
        else
        {
            const juce::Point<float> tip (c.x + std::sin (av) * (rad - thick - 6.0f), c.y - std::cos (av) * (rad - thick - 6.0f));
            g.setColour (text.withAlpha (a)); g.drawLine ({ c, tip }, 2.2f);
        }
    }

    void mouseEnter (const juce::MouseEvent&) override { owner.setHover (param); }
    void mouseExit (const juce::MouseEvent&) override { if (! isMouseButtonDown()) owner.setHover (-1); }
    void mouseDown (const juce::MouseEvent&) override { startNorm = norm(); owner.setHover (param); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! active) return;
        const bool fine = e.mods.isShiftDown();
        const float travel = isKnob ? 220.0f : juce::jmax (120.0f, (float) getHeight() - 70.0f);
        const float t = startNorm - (float) e.getDistanceFromDragStartY() / travel * (fine ? 0.2f : 1.0f);
        owner.setValue (param, fromNorm (param, t, fine));
    }
    void mouseUp (const juce::MouseEvent& e) override { if (! getLocalBounds().contains (e.getPosition())) owner.setHover (-1); }
    void mouseDoubleClick (const juce::MouseEvent&) override { if (active) owner.setValue (param, spectre::param (param).def); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        if (! active) return;
        owner.setValue (param, fromNorm (param, norm() + w.deltaY * 0.05f, true));
    }
};

// ================================================================================================ the scope
class SpectrePage::Scope : public juce::Component
{
public:
    explicit Scope (SpectrePage& o) : owner (o) { setInterceptsMouseClicks (false, false); }
    SpectrePage& owner;
    std::array<float, 1024> snap {};
    void grab()
    {
        const auto& e = owner.ed.proc.spectreShown();
        const int end = e.scopePos.load (std::memory_order_relaxed);
        // line it up on a rising zero crossing so a steady tone stands still
        const int look = spectre::Engine::kScope - (int) snap.size();
        int start = end - (int) snap.size();
        for (int i = 0; i < look - 1; ++i)
        {
            const int k = end - (int) snap.size() - i;
            const float a = e.scope[(size_t) ((k - 1 + 4 * spectre::Engine::kScope) % spectre::Engine::kScope)].load (std::memory_order_relaxed);
            const float b = e.scope[(size_t) ((k + 4 * spectre::Engine::kScope) % spectre::Engine::kScope)].load (std::memory_order_relaxed);
            if (a <= 0.0f && b > 0.0f) { start = k; break; }
        }
        for (size_t i = 0; i < snap.size(); ++i) snap[i] = e.scope[(size_t) ((start + (int) i + 4 * spectre::Engine::kScope) % spectre::Engine::kScope)].load (std::memory_order_relaxed);
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (night0.withAlpha (0.92f)); g.fillRoundedRectangle (r, 9.0f);
        g.setColour (border); g.drawRoundedRectangle (r.reduced (0.5f), 9.0f, 1.0f);
        auto in = r.reduced (12.0f, 6.0f);
        const auto& e = owner.ed.proc.spectreShown();
        const int live = e.mid.activeCount.load() + e.side.activeCount.load();
        g.setFont (font (10.0f, 1, 0.12f)); g.setColour (sub);
        g.drawText ("THE REBUILT SOUND", in.removeFromLeft (120.0f), juce::Justification::centredLeft);
        g.setColour (live > 0 ? orange : dim);
        g.drawText (juce::String (live) + " PARTIALS LIVE", in.removeFromRight (128.0f), juce::Justification::centredRight);
        float peak = 1e-4f; for (float v : snap) peak = juce::jmax (peak, std::abs (v));
        const float sc = 0.42f * in.getHeight() / juce::jmax (0.05f, peak);
        juce::Path p;
        for (size_t i = 0; i < snap.size(); ++i)
        {
            const float x = in.getX() + (float) i / (float) (snap.size() - 1) * in.getWidth(), y = in.getCentreY() - snap[i] * sc;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (dim.withAlpha (0.4f)); g.drawLine (in.getX(), in.getCentreY(), in.getRight(), in.getCentreY(), 1.0f);
        art::glow (g, p, orange.withAlpha (0.5f), 6);
        g.setColour (orange); g.strokePath (p, juce::PathStrokeType (1.6f));
    }
};

// ================================================================================================ the pad
class SpectrePage::Pad : public juce::Component
{
public:
    explicit Pad (SpectrePage& o) : owner (o) { setMouseCursor (juce::MouseCursor::CrosshairCursor); }
    SpectrePage& owner;
    std::vector<juce::Point<float>> ghosts;   // (log frequency 0..1, loudness 0..1) of the partials living now
    bool active = true;

    juce::Rectangle<float> area() const { return getLocalBounds().toFloat().reduced (30.0f, 26.0f); }
    void grab()
    {
        ghosts.clear();
        const auto& e = owner.ed.proc.spectreShown();
        for (auto* v : { &e.mid, &e.side })
        {
            const int n = v->showCount.load (std::memory_order_relaxed);
            for (int i = 0; i < n; ++i)
            {
                const float f = v->showFreq[(size_t) i].load (std::memory_order_relaxed), a = v->showAmp[(size_t) i].load (std::memory_order_relaxed);
                if (f <= 20.0f || a <= 0.0f) continue;
                const float x = std::log (f / 20.0f) / std::log (1000.0f), y = juce::jlimit (0.0f, 1.0f, 1.0f + juce::Decibels::gainToDecibels (a) / 72.0f);
                if (x >= 0.0f && x <= 1.0f) ghosts.push_back ({ x, y * (v == &e.side ? 0.7f : 1.0f) });
            }
        }
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        auto full = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff141416).withAlpha (0.96f)); g.fillRoundedRectangle (full, 12.0f);
        g.setColour (border); g.drawRoundedRectangle (full.reduced (0.5f), 12.0f, 1.2f);
        const auto r = area();
        const float a = active ? 1.0f : 0.35f;
        // the partials, as standing columns of light (low notes left, high right; taller = louder)
        for (auto& gh : ghosts)
        {
            const float x = r.getX() + gh.x * r.getWidth(), h = gh.y * r.getHeight();
            g.setColour (orange.withAlpha (0.10f + 0.25f * gh.y)); g.fillRect (x - 0.7f, r.getBottom() - h, 1.4f, h);
            g.setColour (orange.withAlpha (0.35f + 0.6f * gh.y)); g.fillEllipse (x - 1.6f, r.getBottom() - h - 1.6f, 3.2f, 3.2f);
        }
        // the grid
        g.setColour (text.withAlpha (0.06f));
        for (int i = 1; i < 4; ++i)
        {
            const float fx = r.getX() + r.getWidth() * (float) i / 4.0f, fy = r.getY() + r.getHeight() * (float) i / 4.0f;
            g.drawLine (fx, r.getY(), fx, r.getBottom(), 1.0f); g.drawLine (r.getX(), fy, r.getRight(), fy, 1.0f);
        }
        g.setColour (text.withAlpha (0.18f)); g.drawLine (r.getCentreX(), r.getY(), r.getCentreX(), r.getBottom(), 1.0f); g.drawLine (r.getX(), r.getCentreY(), r.getRight(), r.getCentreY(), 1.0f);
        // the axes, in words
        g.setFont (font (11.0f, 2, 0.2f)); g.setColour (text.withAlpha (0.75f * a));
        g.drawText (juce::CharPointer_UTF8 ("\xe2\x86\x90  PHASE  \xe2\x86\x92"), full.withHeight (24.0f), juce::Justification::centred);
        g.setFont (font (9.5f, 1, 0.12f)); g.setColour (sub.withAlpha (a));
        g.drawText ("ROBOT", juce::Rectangle<float> (r.getX(), full.getY() + 4, 80, 18), juce::Justification::centredLeft);
        g.drawText ("WHISPER", juce::Rectangle<float> (r.getRight() - 80, full.getY() + 4, 80, 18), juce::Justification::centredRight);
        g.drawText ("BODY ONLY", juce::Rectangle<float> (r.getX(), r.getY() + 2, 90, 14), juce::Justification::centredLeft);
        g.drawText ("DETAIL ONLY", juce::Rectangle<float> (r.getX(), r.getBottom() - 16, 90, 14), juce::Justification::centredLeft);
        g.drawText ("low notes", juce::Rectangle<float> (r.getX(), full.getBottom() - 22, 80, 18), juce::Justification::centredLeft);
        g.drawText ("high notes", juce::Rectangle<float> (r.getRight() - 80, full.getBottom() - 22, 80, 18), juce::Justification::centredRight);
        {
            juce::Graphics::ScopedSaveState s (g);
            g.addTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::halfPi, full.getRight() - 14.0f, full.getCentreY()));
            g.setFont (font (11.0f, 2, 0.2f)); g.setColour (text.withAlpha (0.75f * a));
            g.drawText (juce::CharPointer_UTF8 ("\xe2\x86\x90  CEPSTRUM  \xe2\x86\x92"), juce::Rectangle<float> (180.0f, 20.0f).withCentre ({ full.getRight() - 14.0f, full.getCentreY() }), juce::Justification::centred);
        }
        // the puck
        const float px = r.getX() + (owner.value (PhaseX) * 0.5f + 0.5f) * r.getWidth();
        const float py = r.getBottom() - (owner.value (CepstrumY) * 0.5f + 0.5f) * r.getHeight();
        g.setColour (hotPink.withAlpha (0.35f * a));
        g.drawLine (r.getX(), py, r.getRight(), py, 1.0f); g.drawLine (px, r.getY(), px, r.getBottom(), 1.0f);
        juce::Path puck; puck.addEllipse (juce::Rectangle<float> (22.0f, 22.0f).withCentre ({ px, py }));
        if (active) art::glow (g, puck, orange.withAlpha (0.8f), 14);
        g.setColour (orange.withAlpha (a)); g.fillPath (puck);
        g.setColour (juce::Colours::white.withAlpha (0.85f * a)); g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ px - 3.0f, py - 3.0f }));
        // the corner it is heading for, in words
        g.setFont (font (11.0f, 1, 0.04f)); g.setColour (text.withAlpha (0.9f * a));
        g.drawText (show (PhaseX, owner.value (PhaseX)) + " / " + show (CepstrumY, owner.value (CepstrumY)),
                    juce::Rectangle<float> (120.0f, 16.0f).withCentre ({ juce::jlimit (r.getX() + 60.0f, r.getRight() - 60.0f, px), py + (py > r.getBottom() - 30.0f ? -24.0f : 24.0f) }), juce::Justification::centred);
    }
    void set (juce::Point<float> at)
    {
        if (! active) return;
        const auto r = area();
        owner.setValue (PhaseX, juce::jlimit (-1.0f, 1.0f, ((at.x - r.getX()) / r.getWidth()) * 2.0f - 1.0f));
        owner.setValue (CepstrumY, juce::jlimit (-1.0f, 1.0f, ((r.getBottom() - at.y) / r.getHeight()) * 2.0f - 1.0f));
    }
    void mouseEnter (const juce::MouseEvent&) override { owner.setHover (PhaseX); }
    void mouseExit (const juce::MouseEvent&) override { if (! isMouseButtonDown()) owner.setHover (-1); }
    void mouseDown (const juce::MouseEvent& e) override { owner.setHover (PhaseX); set (e.position); }
    void mouseDrag (const juce::MouseEvent& e) override { set (e.position); }
    void mouseDoubleClick (const juce::MouseEvent&) override { if (active) { owner.setValue (PhaseX, 0.0f); owner.setValue (CepstrumY, 0.0f); } }
};

// ================================================================================================ the page
SpectrePage::SpectrePage (Editor& e) : ed (e)
{
    pad = std::make_unique<Pad> (*this); addAndMakeVisible (*pad);
    scope = std::make_unique<Scope> (*this); addAndMakeVisible (*scope);
    for (int p : { Shift, Transpose, Cloud, Smooth })
    {
        auto* c = controls.add (new Control (*this, p, false));
        c->colour = (p == Cloud || p == Smooth) ? hotPink : orange;
        addAndMakeVisible (c);
    }
    for (int p : { Inf, Mid, Side, Partials, Output })
    {
        auto* c = controls.add (new Control (*this, p, true, p == Inf));
        c->colour = p == Inf ? green : (p == Mid || p == Side ? juce::Colour (0xffff5050) : orange);
        addAndMakeVisible (c);
    }
    auto make = [this] (std::unique_ptr<Button>& b, const juce::String& label, juce::Colour c, bool solid)
    {
        b = std::make_unique<Button>(); b->label = label; b->colour = c; b->solid = solid; addAndMakeVisible (*b);
    };
    make (liveBtn, "LIVE ON THIS TRACK", orange, false);
    make (listenBtn, "LISTEN", green, false);
    make (printBtn, "PRINT IT", orange, true);
    make (resetBtn, "RESET", purple, false);
    liveBtn->setVisible (Processor::isFx);
    liveBtn->onClick = [this] { pressLive(); };
    listenBtn->onClick = [this] { pressListen(); };
    printBtn->onClick = [this] { pressPrint(); };
    resetBtn->onClick = [this] { pressReset(); };
    setHover (-1);
    refreshEnabled();
}

SpectrePage::~SpectrePage() { stopTimer(); }

void SpectrePage::visibilityChanged()
{
    if (isVisible()) startTimerHz (30); else stopTimer();
}

float SpectrePage::value (int p) const { return ed.proc.spectreParam (p); }

void SpectrePage::setValue (int p, float v)
{
    ed.proc.setSpectreParam (p, v);
    if (auto* c = controlFor (p)) c->repaint();
    if (p == PhaseX || p == CepstrumY) pad->repaint();
    refreshEnabled();
}

SpectrePage::Control* SpectrePage::controlFor (int p) const
{
    for (auto* c : controls) if (c->param == p) return c;
    return nullptr;
}

bool SpectrePage::listening() const { return ed.proc.spectreListening(); }

static bool soundGoesThrough (const Editor& ed) { return ed.proc.spectreLiveOn() || ed.engine.hasSpecimen(); }

juce::String SpectrePage::greyReason (int p) const
{
    if (! soundGoesThrough (ed)) return "load a sound first";
    if (p == Side) return "your sound has no sides (mono)";
    if (p == Cloud || p == Smooth) return "INF is holding everything";
    return {};
}

bool SpectrePage::controlEnabled (int p) const
{
    if (! soundGoesThrough (ed)) return false;
    if (p == Side)
    {
        // the sides: what is going through it now (a live track, or LISTEN), else the lab's sound
        if (ed.proc.spectreLiveOn() || listening()) return ed.proc.spectreShown().sideLevel.load() > 0.01f;
        auto a = ed.engine.processedAudio();
        return a != nullptr && a->channels() > 1 && ! isEffectivelyMono (*a);
    }
    if (p == Cloud || p == Smooth) return value (Inf) < 0.999f;
    return true;
}

void SpectrePage::refreshEnabled()
{
    for (auto* c : controls)
    {
        const bool on = controlEnabled (c->param);
        if (c->active != on) { c->active = on; c->setMouseCursor (on ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor); c->repaint(); }
    }
    const bool padOn = controlEnabled (PhaseX);
    if (pad->active != padOn) { pad->active = padOn; pad->repaint(); }
    const bool has = ed.engine.hasSpecimen();
    listenBtn->setEnabled (has); printBtn->setEnabled (has && ! ed.engine.busy());
    listenBtn->lit = listening(); listenBtn->label = listening() ? "STOP" : "LISTEN";
    liveBtn->lit = ed.proc.spectreLiveOn(); liveBtn->label = ed.proc.spectreLiveOn() ? "LIVE ON THIS TRACK" : "LIVE: OFF (TRACK DRY)";
    for (auto* b : { liveBtn.get(), listenBtn.get(), printBtn.get() }) b->repaint();
}

void SpectrePage::soundChanged() { refreshEnabled(); repaint(); }

void SpectrePage::setHover (int p)
{
    hoverParam = p;
    const auto& room = rooms::info (rooms::Room::Spectre);
    if (p < 0)
    {
        if (Processor::isFx && ed.proc.spectreLiveOn()) captionText = "LIVE: this track plays through SPECTRE as you play it in FL. Move anything and you hear it at once.";
        else if (! ed.engine.hasSpecimen()) captionText = Processor::isFx ? "Turn on LIVE to run this track through SPECTRE, or CAPTURE a sound and press LISTEN." : "Load a sound (the button top left), then press LISTEN and move the pad.";
        else captionText = juce::String (room.bestOn) + "  Press LISTEN and move anything; PRINT IT keeps it.";
    }
    else if (p == PhaseX || p == CepstrumY)
        captionText = "PHASE (across): " + juce::String (param (PhaseX).plain) + ".   CEPSTRUM (up / down): " + param (CepstrumY).plain + ".   Double-click the pad: back to the middle.";
    else
        captionText = juce::String (param (p).name) + ": " + param (p).plain + ".   Drag up / down (hold shift for fine), double-click for the start value.";
    repaint (captionR.toNearestInt().expanded (2));
}

void SpectrePage::pressLive()
{
    if (! Processor::isFx) return;
    ed.proc.setSpectreLive (! ed.proc.spectreLiveOn());
    ed.toast (ed.proc.spectreLiveOn() ? "LIVE: the track now plays through SPECTRE." : "LIVE off: the track plays dry again.");
    setHover (hoverParam); refreshEnabled();
}

void SpectrePage::pressListen()
{
    if (! ed.engine.hasSpecimen()) { ed.toast ("Load a sound first.", true); return; }
    if (listening()) { ed.proc.setSpectreListening (false); refreshEnabled(); return; }
    // the sound underneath: if SPECTRE just printed, the one before that print (so it is never rebuilt twice)
    const bool printed = ed.engine.historyCursor() > 0 && ed.engine.current().label.startsWith ("SPECTRE ");
    ed.proc.setSpectreListening (true, printed ? ed.engine.audioUnderLast() : ed.engine.processedAudio());
    refreshEnabled();
}

void SpectrePage::pressPrint()
{
    auto& st = ed.settingsFor (Op::Spectre);
    for (int i = 0; i < kParams; ++i) st.values[param (i).key] = value (i);
    ed.runRoom (Op::Spectre);
    refreshEnabled();
}

void SpectrePage::pressReset()
{
    const Params d;
    for (int i = 0; i < kParams; ++i) setValue (i, d[i]);
    ed.toast ("SPECTRE: everything back to the start.");
}

void SpectrePage::timerCallback()
{
    scope->grab();
    pad->grab();
    refreshEnabled();
}

void SpectrePage::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    art::glassPanel (g, r, orange, 14.0f, 0.35f);
    // the title, and what it does
    art::neonTitle (g, "SPECTRE", titleR, titleR.getHeight() * 0.8f);
    g.setColour (text.withAlpha (0.85f)); g.setFont (font (12.0f, 0, 0.02f));
    g.drawFittedText (rooms::info (rooms::Room::Spectre).does, doesR.toNearestInt(), juce::Justification::centredLeft, 2, 0.85f);
    // the caption
    g.setColour (night0.withAlpha (0.8f)); g.fillRoundedRectangle (captionR, captionR.getHeight() * 0.5f);
    g.setColour ((hoverParam >= 0 ? orange : border).withAlpha (0.6f)); g.drawRoundedRectangle (captionR, captionR.getHeight() * 0.5f, 1.0f);
    g.setColour (hoverParam >= 0 ? text : sub.brighter (0.2f)); g.setFont (font (11.5f, hoverParam >= 0 ? 1 : 0, 0.02f));
    g.drawFittedText (captionText, captionR.reduced (14.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.75f);
    // the knob panel's frame
    if (! knobR.isEmpty()) { g.setColour (night0.withAlpha (0.55f)); g.fillRoundedRectangle (knobR, 12.0f); g.setColour (border); g.drawRoundedRectangle (knobR, 12.0f, 1.0f); }
}

void SpectrePage::resized()
{
    auto r = getLocalBounds().toFloat().reduced (14.0f, 10.0f);
    const bool narrow = r.getWidth() < 680.0f;
    // the top: title, words, buttons
    auto top = r.removeFromTop (narrow ? 84.0f : 42.0f);
    captionR = r.removeFromBottom (26.0f); r.removeFromBottom (8.0f);
    r.removeFromTop (8.0f);
    auto btns = narrow ? top.removeFromBottom (36.0f) : top.removeFromRight (Processor::isFx ? 560.0f : 360.0f);
    titleR = top.removeFromLeft (narrow ? 150.0f : 190.0f);
    doesR = top.reduced (10.0f, 0.0f);
    {
        auto b = btns.reduced (0.0f, 3.0f);
        const float gap = 8.0f;
        juce::Array<Button*> row;
        if (Processor::isFx) row.add (liveBtn.get());
        row.add (listenBtn.get()); row.add (printBtn.get()); row.add (resetBtn.get());
        const float wLive = Processor::isFx ? juce::jmin (200.0f, b.getWidth() * 0.36f) : 0.0f;
        const float wRest = (b.getWidth() - wLive - gap * (float) (row.size() - 1)) / (float) (row.size() - (Processor::isFx ? 1 : 0));
        for (auto* bt : row)
        {
            const float w = bt == liveBtn.get() ? wLive : (bt == resetBtn.get() ? wRest * 0.75f : wRest * (Processor::isFx ? 1.08f : 1.12f));
            bt->setBounds (b.removeFromLeft (w).toNearestInt()); b.removeFromLeft (gap);
        }
    }
    auto ctl = [this] (int p) { return controlFor (p); };
    if (! narrow)
    {
        const float sw = juce::jlimit (64.0f, 92.0f, r.getWidth() * 0.07f), gap = 10.0f;
        const float kw = juce::jlimit (230.0f, 320.0f, r.getWidth() * 0.25f);
        leftR = r.removeFromLeft (2 * sw + gap); r.removeFromLeft (14.0f);
        knobR = r.removeFromRight (kw); r.removeFromRight (14.0f);
        rightR = r.removeFromRight (2 * sw + gap); r.removeFromRight (14.0f);
        mainR = r;
        ctl (Shift)->setBounds (leftR.withWidth (sw).toNearestInt());
        ctl (Transpose)->setBounds (leftR.withTrimmedLeft (sw + gap).toNearestInt());
        ctl (Cloud)->setBounds (rightR.withWidth (sw).toNearestInt());
        ctl (Smooth)->setBounds (rightR.withTrimmedLeft (sw + gap).toNearestInt());
        auto m = mainR;
        scope->setBounds (m.removeFromTop (juce::jlimit (48.0f, 80.0f, m.getHeight() * 0.16f)).toNearestInt());
        m.removeFromTop (10.0f);
        pad->setBounds (m.toNearestInt());
        // the knobs: MID, INF (bigger, in the middle) and SIDE across the top; PARTIALS and OUTPUT under them
        auto k = knobR.reduced (8.0f, 8.0f);
        auto row = k.removeFromTop (k.getHeight() * 0.58f);
        k.removeFromTop (6.0f);
        const float side = row.getWidth() * 0.3f;
        ctl (Mid)->setBounds (row.removeFromLeft (side).withTrimmedTop (row.getHeight() * 0.12f).toNearestInt());
        ctl (Side)->setBounds (row.removeFromRight (side).withTrimmedTop (row.getHeight() * 0.12f).toNearestInt());
        ctl (Inf)->setBounds (row.toNearestInt());
        const float q = k.getWidth() * 0.12f;
        k.removeFromLeft (q); k.removeFromRight (q);
        ctl (Partials)->setBounds (k.removeFromLeft (k.getWidth() * 0.5f).reduced (6.0f, 0.0f).toNearestInt());
        ctl (Output)->setBounds (k.reduced (6.0f, 0.0f).toNearestInt());
    }
    else
    {
        // a phone-width window: the pad on top, the four sliders in a row, the knobs in a row
        knobR = {};
        mainR = r.removeFromTop (r.getHeight() * 0.42f);
        auto m = mainR;
        scope->setBounds (m.removeFromTop (52.0f).toNearestInt()); m.removeFromTop (8.0f);
        pad->setBounds (m.toNearestInt());
        r.removeFromTop (10.0f);
        auto sl = r.removeFromTop (r.getHeight() * 0.55f);
        const float sw = sl.getWidth() / 4.0f;
        for (int p : { Shift, Transpose, Cloud, Smooth }) ctl (p)->setBounds (sl.removeFromLeft (sw).reduced (3.0f, 0.0f).toNearestInt());
        const float kw = r.getWidth() / 5.0f;
        for (int p : { Mid, Inf, Side, Partials, Output }) ctl (p)->setBounds (r.removeFromLeft (kw).reduced (2.0f, 0.0f).toNearestInt());
    }
}
}
