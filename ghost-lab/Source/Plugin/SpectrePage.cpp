// Ghost Lab - AGPL-3.0-only (see LICENSE, NOTICE.md). SPECTRE's page (SpectrePage.h).
#include "SpectrePage.h"
#include "Editor.h"

namespace glab
{
using namespace theme;
using namespace spectre;

static const juce::Colour orange { 0xffff8a2a }, hotPink { 0xffff4fd8 };
juce::Colour SpectrePage::accent() { return orange; }

// ---- the travel of each control is the engine's own (FL's automation uses the same, whole steps included)
static float normOf (int p, float v) { return spectre::toNorm (p, v); }
static float valueAt (int p, float t, bool) { return spectre::fromNorm (p, t); }
static juce::String show (int p, float v) { return spectre::display (p, v); }
// a few plain words under each control (the full line shows at the bottom while your hand is on it)
static const char* shortWords (int p)
{
    switch (p)
    {
        case Shift: return "moves the formants";
        case Transpose: return "the notes, in semitones";
        case Cloud: return "ring on, drift apart";
        case Smooth: return "changes glide";
        case MidOscs: return "oscillators: middle";
        case SideOscs: return "oscillators: sides";
        case Inf: return "hold, freeze at the top";
        case Locd: return "phase locked to a saw";
        case Rate: return "how often it looks";
        case Division: return "how often, in notes";
        case Output: return "how loud";
        case Noise: return "hiss and drums stay noise";
        case Transient: return "keeps the punch of hits";
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
        g.drawFittedText (label, r.reduced (r.getWidth() < 70.0f ? 3.0f : 10.0f, 2.0f).toNearestInt(), juce::Justification::centred, 1, 0.6f);
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

    float norm() const { return normOf (param, owner.value (param)); }
    float shownValue = -1e9f;
    void repaintIfMoved() { const float v = owner.value (param); if (v != shownValue) { shownValue = v; repaint(); } }

    void paint (juce::Graphics& g) override
    {
        const auto& in = spectre::param (param);
        const float a = active ? 1.0f : 0.28f;
        const bool hot = active && (isMouseOverOrDragging());
        auto r = getLocalBounds().toFloat();
        // name on top, value under it, the plain words at the bottom
        // (the small knobs in the panel keep only their name and value: the plain words show at the bottom on hover)
        const bool compact = isKnob;
        auto nameR = r.removeFromTop (compact ? 13.0f : (isKnob ? 16.0f : 18.0f));
        auto wordsR = r.removeFromBottom (compact ? (active ? 0.0f : 22.0f) : (isKnob ? 24.0f : 30.0f));
        auto valueR = r.removeFromBottom (compact ? 13.0f : (isKnob ? 15.0f : 18.0f));
        g.setColour (text.withAlpha (a)); g.setFont (font (isBig ? 12.0f : (compact ? 10.0f : 11.5f), 2, 0.14f));
        g.drawFittedText (in.name, nameR.toNearestInt(), juce::Justification::centred, 1, 0.7f);
        g.setColour ((hot ? colour.brighter (0.3f) : colour).withAlpha (a)); g.setFont (font (compact ? 10.0f : 11.5f, 1, 0.04f));
        g.drawFittedText (show (param, owner.value (param)), valueR.toNearestInt(), juce::Justification::centred, 1, 0.8f);
        g.setColour (sub.withAlpha (active ? 0.95f : 0.45f)); g.setFont (font (9.8f, 0, 0.0f));
        if (wordsR.getHeight() > 1.0f) g.drawFittedText (active ? juce::String (shortWords (param)) : owner.greyReason (param), wordsR.reduced (2, 0).toNearestInt(), juce::Justification::centredTop, 2, 0.85f);
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
    void mouseDown (const juce::MouseEvent&) override { startNorm = norm(); owner.setHover (param); if (active) { owner.ed.proc.beginSpectreGesture (param); grabbed = true; } }
    bool grabbed = false;
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! active) return;
        const bool fine = e.mods.isShiftDown();
        const float travel = isKnob ? 220.0f : juce::jmax (120.0f, (float) getHeight() - 70.0f);
        const float t = startNorm - (float) e.getDistanceFromDragStartY() / travel * (fine ? 0.2f : 1.0f);
        owner.setValue (param, valueAt (param, t, fine));
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (grabbed) { owner.ed.proc.endSpectreGesture (param); grabbed = false; }
        if (! getLocalBounds().contains (e.getPosition())) owner.setHover (-1);
    }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (! active) return;
        owner.ed.proc.beginSpectreGesture (param); owner.setValue (param, spectre::param (param).def); owner.ed.proc.endSpectreGesture (param);
    }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        if (! active) return;
        owner.setValue (param, valueAt (param, norm() + w.deltaY * 0.05f, true));
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
        auto words = in.removeFromTop (14.0f);
        g.setFont (font (9.5f, 1, 0.12f)); g.setColour (sub);
        g.drawText (getWidth() > 300 ? "WHAT COMES OUT" : "OUT", words, juce::Justification::centredLeft);
        g.setColour (live > 0 ? orange : dim);
        g.drawText (juce::String (live) + (getWidth() > 300 ? " OSCILLATORS LIVE" : " LIVE"), words, juce::Justification::centredRight);
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
    // the morph pad (as in RESYNTH): across, the PHASE - yours (left) to the rebuilt one's (right); up and down, the
    // CEPSTRUM, the tone - yours (bottom) to the rebuilt one's (top). Bottom-left is your sound untouched, top-right is
    // all oscillators. The rails (along the bottom, up the right) move one at a time.
    explicit Pad (SpectrePage& o) : owner (o) { setMouseCursor (juce::MouseCursor::CrosshairCursor); }
    SpectrePage& owner;
    std::vector<juce::Point<float>> ghosts;
    bool active = true;
    int dragging = 0;   // 0 the pad, 1 the PHASE rail, 2 the CEPSTRUM rail

    juce::Rectangle<float> area() const { auto r = getLocalBounds().toFloat(); r.removeFromBottom (26.0f); r.removeFromRight (26.0f); r.removeFromLeft (24.0f); r.removeFromTop (22.0f); return r; }
    juce::Rectangle<float> railX() const { const auto a = area(); return { a.getX(), a.getBottom() + 8.0f, a.getWidth(), 12.0f }; }
    juce::Rectangle<float> railY() const { const auto a = area(); return { a.getRight() + 8.0f, a.getY(), 12.0f, a.getHeight() }; }
    void readGhosts()
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
        // the partials living now, as standing columns of light (low notes left, high right; taller = louder)
        for (auto& gh : ghosts)
        {
            const float x = r.getX() + gh.x * r.getWidth(), h = gh.y * r.getHeight();
            g.setColour (orange.withAlpha (0.08f + 0.2f * gh.y)); g.fillRect (x - 0.7f, r.getBottom() - h, 1.4f, h);
            g.setColour (orange.withAlpha (0.3f + 0.5f * gh.y)); g.fillEllipse (x - 1.6f, r.getBottom() - h - 1.6f, 3.2f, 3.2f);
        }
        g.setColour (text.withAlpha (0.06f));
        for (int i = 1; i < 4; ++i)
        {
            const float fx = r.getX() + r.getWidth() * (float) i / 4.0f, fy = r.getY() + r.getHeight() * (float) i / 4.0f;
            g.drawLine (fx, r.getY(), fx, r.getBottom(), 1.0f); g.drawLine (r.getX(), fy, r.getRight(), fy, 1.0f);
        }
        g.setColour (text.withAlpha (0.16f)); g.drawRect (r, 1.0f);
        // the words: corners and axes
        g.setFont (font (11.0f, 2, 0.2f)); g.setColour (text.withAlpha (0.8f * a));
        g.drawText (juce::CharPointer_UTF8 ("PHASE  \xe2\x86\x92"), juce::Rectangle<float> (r.getX(), full.getY(), r.getWidth(), 22.0f), juce::Justification::centred);
        g.setFont (font (9.5f, 1, 0.12f)); g.setColour (sub.withAlpha (a));
        g.drawText ("YOUR SOUND", juce::Rectangle<float> (r.getX() + 6.0f, r.getBottom() - 18.0f, 120.0f, 14.0f), juce::Justification::centredLeft);
        g.drawText ("REBUILT", juce::Rectangle<float> (r.getRight() - 126.0f, r.getY() + 4.0f, 120.0f, 14.0f), juce::Justification::centredRight);
        g.drawText ("yours", juce::Rectangle<float> (r.getX(), full.getY() + 4.0f, 60.0f, 14.0f), juce::Justification::centredLeft);
        g.drawText ("rebuilt", juce::Rectangle<float> (r.getRight() - 60.0f, full.getY() + 4.0f, 60.0f, 14.0f), juce::Justification::centredRight);
        {
            juce::Graphics::ScopedSaveState s (g);
            const juce::Point<float> c (full.getX() + 12.0f, r.getCentreY());
            g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, c.x, c.y));
            g.setFont (font (11.0f, 2, 0.2f)); g.setColour (text.withAlpha (0.8f * a));
            g.drawText (juce::CharPointer_UTF8 ("CEPSTRUM (TONE)  \xe2\x86\x92"), juce::Rectangle<float> (r.getHeight(), 20.0f).withCentre (c), juce::Justification::centred);
        }
        // the rails
        const float x = owner.value (PhaseX), y = owner.value (CepstrumY);
        auto rail = [&] (juce::Rectangle<float> b, bool horiz, float v, bool hot)
        {
            g.setColour (night0.withAlpha (0.9f)); g.fillRoundedRectangle (b, 5.0f);
            g.setColour ((hot ? orange : border).withAlpha (a)); g.drawRoundedRectangle (b, 5.0f, 1.0f);
            auto fill = horiz ? b.withWidth (b.getWidth() * v) : b.withTrimmedTop (b.getHeight() * (1.0f - v));
            g.setColour (orange.withAlpha (0.45f * a)); g.fillRoundedRectangle (fill.reduced (2.0f), 3.0f);
            const auto dot = horiz ? juce::Point<float> (b.getX() + v * b.getWidth(), b.getCentreY()) : juce::Point<float> (b.getCentreX(), b.getBottom() - v * b.getHeight());
            g.setColour (juce::Colours::white.withAlpha (0.9f * a)); g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (dot));
        };
        rail (railX(), true, x, dragging == 1);
        rail (railY(), false, y, dragging == 2);
        // the puck
        const float px = r.getX() + x * r.getWidth(), py = r.getBottom() - y * r.getHeight();
        g.setColour (hotPink.withAlpha (0.35f * a));
        g.drawLine (r.getX(), py, r.getRight(), py, 1.0f); g.drawLine (px, r.getY(), px, r.getBottom(), 1.0f);
        juce::Path puck; puck.addEllipse (juce::Rectangle<float> (22.0f, 22.0f).withCentre ({ px, py }));
        if (active) art::glow (g, puck, orange.withAlpha (0.8f), 14);
        g.setColour (orange.withAlpha (a)); g.fillPath (puck);
        g.setColour (juce::Colours::white.withAlpha (0.85f * a)); g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ px - 3.0f, py - 3.0f }));
        g.setFont (font (11.0f, 1, 0.04f)); g.setColour (text.withAlpha (0.9f * a));
        g.drawText ("tone " + show (CepstrumY, y) + " / phase " + show (PhaseX, x),
                    juce::Rectangle<float> (190.0f, 16.0f).withCentre ({ juce::jlimit (r.getX() + 95.0f, r.getRight() - 95.0f, px), py + (py > r.getBottom() - 30.0f ? -24.0f : 24.0f) }), juce::Justification::centred);
    }
    void set (juce::Point<float> at)
    {
        if (! active) return;
        const auto r = area();
        if (dragging != 2) owner.setValue (PhaseX, juce::jlimit (0.0f, 1.0f, (at.x - r.getX()) / r.getWidth()));
        if (dragging != 1) owner.setValue (CepstrumY, juce::jlimit (0.0f, 1.0f, (r.getBottom() - at.y) / r.getHeight()));
    }
    bool grabbed = false;
    void grab (bool on)
    {
        if (on == grabbed) return;
        grabbed = on;
        for (int p : { PhaseX, CepstrumY }) { if (on) owner.ed.proc.beginSpectreGesture (p); else owner.ed.proc.endSpectreGesture (p); }
    }
    void mouseEnter (const juce::MouseEvent&) override { owner.setHover (PhaseX); }
    void mouseExit (const juce::MouseEvent&) override { if (! isMouseButtonDown()) owner.setHover (-1); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        owner.setHover (PhaseX);
        dragging = railX().expanded (4.0f).contains (e.position) ? 1 : (railY().expanded (4.0f).contains (e.position) ? 2 : 0);
        if (active) grab (true);
        set (e.position);
    }
    void mouseDrag (const juce::MouseEvent& e) override { set (e.position); }
    void mouseUp (const juce::MouseEvent&) override { grab (false); dragging = 0; repaint(); }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (! active) return;
        grab (true); owner.setValue (PhaseX, 1.0f); owner.setValue (CepstrumY, 1.0f); grab (false);   // (back to all rebuilt)
    }
};

// ================================================================================================ the wavetable box
// "the partials play ...": SINE (plain sine waves, the start) or a wavetable - the built-in ones under the arrows, or one
// cycle from a WAV of yours (the folder, or drop the file here; a Serum / Vital wavetable file gives its first frame)
class SpectrePage::WaveBox : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    explicit WaveBox (SpectrePage& o) : owner (o) {}
    SpectrePage& owner;
    juce::String shownName;
    bool hotL = false, hotR = false, hotF = false, dropping = false;

    juce::Rectangle<float> leftR() const { return getLocalBounds().toFloat().removeFromLeft (26.0f); }
    juce::Rectangle<float> folderR() const { return getLocalBounds().toFloat().removeFromRight (34.0f); }
    juce::Rectangle<float> rightR() const { return getLocalBounds().toFloat().withTrimmedRight (34.0f).removeFromRight (26.0f); }
    void repaintIfChanged() { if (owner.ed.proc.spectreTableName() != shownName) repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        shownName = owner.ed.proc.spectreTableName();
        g.setColour (night0.withAlpha (0.92f)); g.fillRoundedRectangle (r, 9.0f);
        g.setColour ((dropping ? orange : border).withAlpha (dropping ? 1.0f : 0.9f)); g.drawRoundedRectangle (r.reduced (0.5f), 9.0f, dropping ? 2.0f : 1.0f);
        auto arrow = [&] (juce::Rectangle<float> b, bool left, bool hot)
        {
            juce::Path p; const auto c = b.getCentre();
            if (left) p.addTriangle (c.x - 4, c.y, c.x + 3, c.y - 6, c.x + 3, c.y + 6); else p.addTriangle (c.x + 4, c.y, c.x - 3, c.y - 6, c.x - 3, c.y + 6);
            g.setColour (hot ? orange : text.withAlpha (0.7f)); g.fillPath (p);
        };
        arrow (leftR(), true, hotL); arrow (rightR(), false, hotR);
        // the folder
        {
            auto f = folderR().reduced (8.0f, 9.0f);
            juce::Path p;
            p.startNewSubPath (f.getX(), f.getY() + 3); p.lineTo (f.getX() + f.getWidth() * 0.4f, f.getY() + 3); p.lineTo (f.getX() + f.getWidth() * 0.5f, f.getY() + 6);
            p.lineTo (f.getRight(), f.getY() + 6); p.lineTo (f.getRight(), f.getBottom()); p.lineTo (f.getX(), f.getBottom()); p.closeSubPath();
            g.setColour (hotF ? orange.brighter (0.2f) : orange); g.fillPath (p);
        }
        // the name and one cycle of it
        auto mid = r.withTrimmedLeft (26.0f).withTrimmedRight (60.0f);
        auto label = mid.removeFromTop (16.0f).translated (0, 3);
        const bool roomy = getWidth() > 250;
        if (roomy) { g.setFont (font (9.5f, 1, 0.12f)); g.setColour (sub); g.drawText ("WAVETABLE", label, juce::Justification::centredLeft); }
        g.setFont (font (10.5f, 2, 0.08f)); g.setColour (shownName == "SINE" ? text.withAlpha (0.85f) : orange);
        g.drawFittedText (shownName.toUpperCase(), label.toNearestInt(), roomy ? juce::Justification::centredRight : juce::Justification::centred, 1, 0.7f);
        auto wr = mid.reduced (2.0f, 5.0f);
        const auto& cyc = owner.ed.proc.spectreTable();
        float peak = 1e-6f; for (float v : cyc) peak = juce::jmax (peak, std::abs (v));
        juce::Path w;
        const int pts = 160;
        for (int i = 0; i < pts; ++i)
        {
            const float t = (float) i / (float) (pts - 1);
            const float v = cyc.empty() ? std::sin (6.2831853f * t) : cyc[(size_t) juce::jlimit (0, (int) cyc.size() - 1, (int) (t * (float) cyc.size()))] / peak;
            const float x = wr.getX() + t * wr.getWidth(), y = wr.getCentreY() - v * wr.getHeight() * 0.42f;
            if (i == 0) w.startNewSubPath (x, y); else w.lineTo (x, y);
        }
        art::glow (g, w, orange.withAlpha (0.45f), 5);
        g.setColour (orange); g.strokePath (w, juce::PathStrokeType (1.5f));
    }
    void mouseMove (const juce::MouseEvent& e) override
    {
        const bool l = leftR().contains (e.position), rr = rightR().contains (e.position), f = folderR().contains (e.position);
        if (l != hotL || rr != hotR || f != hotF) { hotL = l; hotR = rr; hotF = f; repaint(); }
        owner.setHover (SpectrePage::kWaveHover);
    }
    void mouseExit (const juce::MouseEvent&) override { hotL = hotR = hotF = false; repaint(); owner.setHover (-1); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (leftR().contains (e.position)) owner.stepTable (-1);
        else if (rightR().contains (e.position)) owner.stepTable (1);
        else if (folderR().contains (e.position)) owner.chooseTable();
        else owner.stepTable (1);
    }
    bool isInterestedInFileDrag (const juce::StringArray& f) override { return f.size() == 1 && juce::File (f[0]).hasFileExtension ("wav;aif;aiff;flac"); }
    void fileDragEnter (const juce::StringArray&, int, int) override { dropping = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropping = false; repaint(); }
    void filesDropped (const juce::StringArray& f, int, int) override { dropping = false; repaint(); owner.loadTable (juce::File (f[0])); }
};

// ================================================================================================ the page
SpectrePage::SpectrePage (Editor& e) : ed (e)
{
    pad = std::make_unique<Pad> (*this); addAndMakeVisible (*pad);
    scope = std::make_unique<Scope> (*this); addAndMakeVisible (*scope);
    wave = std::make_unique<WaveBox> (*this); addAndMakeVisible (*wave);
    for (int p : { Shift, Transpose, Cloud, Smooth })
    {
        auto* c = controls.add (new Control (*this, p, false));
        c->colour = (p == Cloud || p == Smooth) ? hotPink : orange;
        addAndMakeVisible (c);
    }
    for (int p : { Inf, MidOscs, SideOscs, Locd, Noise, Transient, Rate, Division, Output })
    {
        auto* c = controls.add (new Control (*this, p, true, p == Inf));
        c->colour = p == Inf ? green : (p == MidOscs || p == SideOscs ? juce::Colour (0xffff5050) : (p == Noise || p == Transient ? cyan : (p == Locd ? hotPink : orange)));
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
    // MODE (IF / PEAK / TRACK), UPDATE RATE's FREE / SNAPPED, and MID / SIDE's LINK
    for (int i = 0; i < 3; ++i)
    {
        auto* b = modeBtns.add (new Button()); b->label = modeNames()[i]; b->colour = orange; addAndMakeVisible (b);
        b->onClick = [this, i] { ed.proc.beginSpectreGesture (Mode); setValue (Mode, (float) i); ed.proc.endSpectreGesture (Mode); setHover (Mode); };
    }
    make (freeBtn, "FREE", orange, false);
    make (snapBtn, "SNAPPED", orange, false);
    make (linkBtn, "LINK", juce::Colour (0xffff5050), false);
    freeBtn->onClick = [this] { ed.proc.beginSpectreGesture (Sync); setValue (Sync, 0.0f); ed.proc.endSpectreGesture (Sync); };
    snapBtn->onClick = [this] { ed.proc.beginSpectreGesture (Sync); setValue (Sync, 1.0f); ed.proc.endSpectreGesture (Sync); };
    linkBtn->onClick = [this] { ed.proc.beginSpectreGesture (Link); setValue (Link, value (Link) >= 0.5f ? 0.0f : 1.0f); ed.proc.endSpectreGesture (Link); };
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
    if ((p == MidOscs || p == SideOscs) && value (Link) >= 0.5f)   // (LINK: the two move together)
    {
        const int other = p == MidOscs ? SideOscs : MidOscs;
        ed.proc.setSpectreParam (other, v);
        if (auto* c = controlFor (other)) c->repaint();
    }
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
    if (p == SideOscs) return "your sound has no sides (mono)";
    if (p == Cloud || p == Smooth || p == Transient) return "INF is holding everything";
    return {};
}

bool SpectrePage::controlEnabled (int p) const
{
    if (! soundGoesThrough (ed)) return false;
    if (p == SideOscs)
    {
        // the sides: what is going through it now (a live track, or LISTEN), else the lab's sound
        if (ed.proc.spectreLiveOn() || listening()) return ed.proc.spectreShown().sideLevel.load() > 0.01f;
        auto a = ed.engine.processedAudio();
        return a != nullptr && a->channels() > 1 && ! isEffectivelyMono (*a);
    }
    if (p == Cloud || p == Smooth || p == Transient) return value (Inf) < 0.999f;
    return true;
}

void SpectrePage::refreshEnabled()
{
    for (auto* c : controls)
    {
        const bool on = controlEnabled (c->param);
        if (c->active != on) { c->active = on; c->setMouseCursor (on ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor); c->repaint(); }
    }
    // UPDATE RATE: the one knob reads milliseconds (FREE) or note lengths (SNAPPED)
    const bool snapped = value (Sync) >= 0.5f;
    if (auto* c = controlFor (Rate)) c->setVisible (! snapped);
    if (auto* c = controlFor (Division)) c->setVisible (snapped);
    freeBtn->lit = ! snapped; snapBtn->lit = snapped; linkBtn->lit = value (Link) >= 0.5f;
    const int mode = juce::roundToInt (value (Mode));
    for (int i = 0; i < modeBtns.size(); ++i) if (modeBtns[i]->lit != (i == mode)) { modeBtns[i]->lit = i == mode; modeBtns[i]->repaint(); }
    for (auto* b : { freeBtn.get(), snapBtn.get(), linkBtn.get() }) b->repaint();
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
        else captionText = Processor::isFx ? juce::String (room.bestOn) + "  Press LISTEN and move anything; PRINT IT keeps it."
                                           : "Hold a key on your keyboard (or press LISTEN) and move anything: you hear it change as you go. PRINT IT keeps it.";
    }
    else if (p == kWaveHover)
        captionText = "WAVETABLE: each partial plays this shape instead of a plain sine wave. Arrows: the built-in ones. Folder (or drop a WAV here): your own; a wavetable file gives its first frame.";
    else if (p == PhaseX || p == CepstrumY)
        captionText = "The morph: bottom-left is your sound, top-right is all oscillators. Across: PHASE (" + juce::String (param (PhaseX).plain) + "). Up / down: CEPSTRUM, the tone (" + param (CepstrumY).plain + "). Rails move one at a time; double-click: all rebuilt.";
    else if (p == Mode)
        captionText = "MODE: " + juce::String (param (Mode).plain) + ".";
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
    st.data = tableToText (ed.proc.spectreTable());   // (the WAVETABLE goes with the step)
    st.values["bpm"] = (float) ed.proc.songTempo();
    ed.runRoom (Op::Spectre);
    refreshEnabled();
}

void SpectrePage::pressReset()
{
    const Params d;
    for (int i = 0; i < kParams; ++i) { ed.proc.beginSpectreGesture (i); setValue (i, d[i]); ed.proc.endSpectreGesture (i); }
    ed.proc.setSpectreTable ("SINE", {}); wave->repaint();
    ed.toast ("SPECTRE: everything back to the start.");
}

void SpectrePage::stepTable (int dir)
{
    const auto& names = tableNames();
    int i = names.indexOf (ed.proc.spectreTableName());
    i = i < 0 ? 0 : (i + dir + names.size()) % names.size();   // (your own table: the arrows go back to the built-in ones)
    ed.proc.setSpectreTable (names[i], tableCycle (i));
    wave->repaint();
}

void SpectrePage::chooseTable()
{
    chooser = std::make_unique<juce::FileChooser> ("A wavetable or a single-cycle WAV", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<SpectrePage> (this)] (const juce::FileChooser& fc)
    {
        if (safe != nullptr && fc.getResult().existsAsFile()) safe->loadTable (fc.getResult());
    });
}

bool SpectrePage::loadTable (const juce::File& f)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr || r->lengthInSamples < 16) { ed.toast ("That file could not be read as a wavetable.", true); return false; }
    juce::AudioBuffer<float> b ((int) std::min<long long> ((long long) r->numChannels, 2LL), (int) std::min<long long> ((long long) r->lengthInSamples, (long long) kTableSize));
    r->read (&b, 0, b.getNumSamples(), 0, true, true);
    auto cyc = cycleFromFile (b);
    if (cyc.empty()) { ed.toast ("That file is too short to be a wavetable.", true); return false; }
    ed.proc.setSpectreTable (f.getFileNameWithoutExtension(), cyc);
    wave->repaint();
    ed.toast ("The partials now play " + f.getFileNameWithoutExtension() + ".");
    return true;
}

void SpectrePage::timerCallback()
{
    scope->grab();
    pad->readGhosts();
    wave->repaintIfChanged();
    // FL's automation (or a restored session) may have moved the controls: show where they are now
    for (auto* c : controls) c->repaintIfMoved();
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
    if (! modeLabelR.isEmpty()) { g.setFont (font (9.5f, 1, 0.12f)); g.setColour (sub); g.drawText ("MODE", modeLabelR, juce::Justification::centredLeft); }
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
        auto strip = m.removeFromTop (juce::jlimit (48.0f, 80.0f, m.getHeight() * 0.16f));
        wave->setBounds (strip.removeFromLeft (juce::jmin (230.0f, strip.getWidth() * 0.34f)).toNearestInt());
        strip.removeFromLeft (8.0f);
        {
            auto mb = strip.removeFromLeft (juce::jmin (200.0f, strip.getWidth() * 0.45f));
            auto lbl = mb.removeFromTop (14.0f); juce::ignoreUnused (lbl);
            const float w = mb.getWidth() / 3.0f;
            for (auto* b : modeBtns) b->setBounds (mb.removeFromLeft (w).reduced (1.0f, 2.0f).toNearestInt());
            modeLabelR = lbl;
        }
        strip.removeFromLeft (8.0f);
        scope->setBounds (strip.toNearestInt());
        m.removeFromTop (10.0f);
        pad->setBounds (m.toNearestInt());
        // the knobs: MID, INF (bigger) and SIDE; LOCD, NOISE, TRANSIENT; UPDATE RATE (FREE / SNAPPED) and OUTPUT
        auto k = knobR.reduced (8.0f, 8.0f);
        auto row1 = k.removeFromTop (k.getHeight() * 0.42f);
        auto linkR = row1.removeFromBottom (22.0f);
        const float side = row1.getWidth() * 0.3f;
        ctl (MidOscs)->setBounds (row1.removeFromLeft (side).withTrimmedTop (row1.getHeight() * 0.1f).toNearestInt());
        ctl (SideOscs)->setBounds (row1.removeFromRight (side).withTrimmedTop (row1.getHeight() * 0.1f).toNearestInt());
        ctl (Inf)->setBounds (row1.toNearestInt());
        linkBtn->setBounds (linkR.withSizeKeepingCentre (70.0f, 20.0f).toNearestInt());
        k.removeFromTop (4.0f);
        auto row2 = k.removeFromTop (k.getHeight() * 0.5f);
        const float q = row2.getWidth() / 3.0f;
        for (int p : { Locd, Noise, Transient }) ctl (p)->setBounds (row2.removeFromLeft (q).reduced (2.0f, 0.0f).toNearestInt());
        auto row3 = k;
        auto rateR = row3.removeFromLeft (row3.getWidth() * 0.62f);
        auto toggles = rateR.removeFromBottom (20.0f);
        ctl (Rate)->setBounds (rateR.toNearestInt());
        ctl (Division)->setBounds (rateR.toNearestInt());
        freeBtn->setBounds (toggles.removeFromLeft (toggles.getWidth() * 0.45f).reduced (2.0f, 0.0f).toNearestInt());
        snapBtn->setBounds (toggles.reduced (2.0f, 0.0f).toNearestInt());
        ctl (Output)->setBounds (row3.reduced (4.0f, 0.0f).toNearestInt());
    }
    else
    {
        // a phone-width window: the pad on top, the four sliders in a row, the knobs in a row
        knobR = {};
        mainR = r.removeFromTop (r.getHeight() * 0.42f);
        auto m = mainR;
        auto strip = m.removeFromTop (52.0f);
        wave->setBounds (strip.removeFromLeft (strip.getWidth() * 0.4f).toNearestInt()); strip.removeFromLeft (6.0f);
        { auto mb = strip.removeFromLeft (strip.getWidth() * 0.5f); modeLabelR = mb.removeFromTop (14.0f); const float w = mb.getWidth() / 3.0f; for (auto* b : modeBtns) b->setBounds (mb.removeFromLeft (w).reduced (1.0f, 2.0f).toNearestInt()); }
        strip.removeFromLeft (6.0f);
        scope->setBounds (strip.toNearestInt()); m.removeFromTop (8.0f);
        pad->setBounds (m.toNearestInt());
        r.removeFromTop (10.0f);
        auto sl = r.removeFromTop (r.getHeight() * 0.55f);
        const float sw = sl.getWidth() / 4.0f;
        for (int p : { Shift, Transpose, Cloud, Smooth }) ctl (p)->setBounds (sl.removeFromLeft (sw).reduced (3.0f, 0.0f).toNearestInt());
        auto toggles = r.removeFromBottom (22.0f);
        linkBtn->setBounds (toggles.removeFromLeft (toggles.getWidth() / 3.0f).reduced (4.0f, 0.0f).toNearestInt());
        freeBtn->setBounds (toggles.removeFromLeft (toggles.getWidth() * 0.5f).reduced (4.0f, 0.0f).toNearestInt());
        snapBtn->setBounds (toggles.reduced (4.0f, 0.0f).toNearestInt());
        const float kw = r.getWidth() / 8.0f;
        for (int p : { MidOscs, Inf, SideOscs, Locd, Noise, Transient, Rate, Output })
        {
            const auto b = r.removeFromLeft (kw).reduced (2.0f, 0.0f).toNearestInt();
            ctl (p)->setBounds (b);
            if (p == Rate) ctl (Division)->setBounds (b);
        }
    }
}
}
