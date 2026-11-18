#pragma once
// KEYS (round 3): three small additions to the PLAY / FLOW keyboard strip at the bottom of the SYNTH page.
//   1. readable note labels: the A .. G names on the strip take a fixed ink (white or black, whichever reads best)
//      against the key colour actually behind them (>= 4.5:1 in BLACK / PURPLE / LIGHT); the key colours stay.
//   2. HOLD (latch): notes played on the FLOW strip stay held after release; playing a held note again lets it go;
//      HOLD off lets all of them go. A session control: never saved in a preset or the project.
//   3. the ghost cursor: a tiny ghost that follows the pointer over the strip, glitches now and then and squashes on a press.
// Everything here is UI only (the notes go into the processor's MidiKeyboardState, the same path as before): no sound change.
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <bitset>
#include "GhostArt.h"
#include "ProGhostLook.h"   // GHOST LOOK: the cursor ghost is outlined on the glass

namespace keysx
{
// ------------------------------------------------------------------ 1. label contrast
inline double luminance (juce::Colour c)
{
    auto ch = [] (juce::uint8 v) { const double x = v / 255.0; return x <= 0.03928 ? x / 12.92 : std::pow ((x + 0.055) / 1.055, 2.4); };
    return 0.2126 * ch (c.getRed()) + 0.7152 * ch (c.getGreen()) + 0.0722 * ch (c.getBlue());
}
inline double contrast (juce::Colour a, juce::Colour b)
{
    const double la = luminance (a), lb = luminance (b);
    return (std::max (la, lb) + 0.05) / (std::min (la, lb) + 0.05);
}

// the FLOW strip's key colour for a scale degree (0..11 semitones above the key) - unchanged from the strip's own formula
inline juce::Colour keyColour (int degree)
{
    const float deg = (float) (((degree % 12) + 12) % 12) / 12.0f;
    return juce::Colour::fromHSV (0.42f + deg * 0.38f, 0.85f, 1.0f, 1.0f);
}
inline constexpr juce::uint32 stripBase = 0xff050608;   // the strip's backdrop (a display: dark in every theme)

// the colour of a key at height yFrac (0 top .. 1 bottom) as the strip paints it: its vertical gradient over the backdrop
// GLASS (round 3): base = the strip's backdrop at that height (Ghosty Pro's glass LIGHT paints a light frosted screen; default = stripBase)
inline juce::Colour keyBehind (juce::Colour c, float flash, float level, float yFrac, juce::Colour base = juce::Colour (stripBase))
{
    const float a0 = juce::jmin (1.0f, 0.05f + flash * 0.55f), a1 = juce::jmin (1.0f, 0.14f + flash * 0.35f + level * 0.15f);
    const float a = juce::jlimit (0.0f, 1.0f, a0 + (a1 - a0) * yFrac);
    return base.interpolatedWith (c.withAlpha (1.0f), a);
}
// the label ink: pure white or pure black, whichever has more contrast (one of them is always >= 4.58:1)
inline juce::Colour labelInk (juce::Colour behind)
{
    const auto w = juce::Colours::white, k = juce::Colours::black;
    return contrast (w, behind) >= contrast (k, behind) ? w : k;
}
inline constexpr float labelYFrac (float stripHeight) { return (stripHeight - 9.0f) / stripHeight; }   // the label's centre line

// ------------------------------------------------------------------ 2. HOLD (latch)
// Sits between the FLOW strip and the keyboard state. press() / release() replace the strip's own noteOn / noteOff.
struct NoteLatch
{
    explicit NoteLatch (juce::MidiKeyboardState& s) : ks (s) {}
    ~NoteLatch() { releaseAll(); }      // closing the editor never leaves a latched note hanging
    NoteLatch (const NoteLatch&) = delete; NoteLatch& operator= (const NoteLatch&) = delete;

    // a key goes down. Returns true when the note now sounds; false when HOLD let a held note go instead.
    bool press (int note, float vel, int ch = 1)
    {
        if (! juce::isPositiveAndBelow (note, 128)) return false;
        if (hold && held[(size_t) note]) { held.reset ((size_t) note); ks.noteOff (ch, note, 0.0f); return false; }
        ks.noteOn (ch, note, vel); return true;
    }
    // the key comes up: sounding = what press() returned
    void release (int note, bool sounding, int ch = 1)
    {
        if (! sounding || ! juce::isPositiveAndBelow (note, 128)) return;
        if (hold) held.set ((size_t) note); else ks.noteOff (ch, note, 0.0f);
    }
    void setHold (bool on) { if (on == hold) return; hold = on; if (! on) releaseAll(); if (onChange) onChange(); }
    bool isHold() const { return hold; }
    bool isHeld (int note) const { return juce::isPositiveAndBelow (note, 128) && held[(size_t) note]; }
    int numHeld() const { return (int) held.count(); }
    void releaseAll (int ch = 1) { for (int n = 0; n < 128; ++n) if (held[(size_t) n]) { held.reset ((size_t) n); ks.noteOff (ch, n, 0.0f); } }
    std::function<void()> onChange;
private:
    juce::MidiKeyboardState& ks;
    bool hold = false;
    std::bitset<128> held;
};

class HoldButton : public juce::TextButton
{
public:
    explicit HoldButton (NoteLatch& l) : juce::TextButton ("HOLD"), latch (l)
    {
        setName ("HOLD");
        setClickingTogglesState (true);
        setTooltip ("HOLD: notes you play on the FLOW strip keep sounding after you let go (they feed the ARP too).\n"
                    "Play a held note again to let it go; click HOLD off to let them all go. Not saved in sounds.");
        onClick = [this] { latch.setHold (getToggleState()); };
        latch.onChange = [this] { setToggleState (latch.isHold(), juce::dontSendNotification); repaint(); };
    }
    ~HoldButton() override { latch.onChange = nullptr; }
    NoteLatch& getLatch() { return latch; }   // test hook
protected:
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        juce::TextButton::paintButton (g, over, down);
        auto led = juce::Rectangle<float> ((float) getWidth() - 11, 5, 5, 5);    // the small lit state
        const bool on = getToggleState();
        g.setColour (on ? juce::Colour (0xff39ff88) : juce::Colours::grey.withAlpha (0.35f));
        g.fillEllipse (led);
        if (on) { g.setColour (juce::Colour (0xff39ff88).withAlpha (0.35f)); g.fillEllipse (led.expanded (2.0f)); }
    }
private:
    NoteLatch& latch;
};

// OCT - / +: shifts the FLOW strip's three octaves (session only, like HOLD)
struct StripOctave { int value = 0; static constexpr int lo = -2, hi = 2; std::function<void()> onChange; };

class OctaveControl : public juce::Component
{
public:
    explicit OctaveControl (StripOctave& o) : oct (o)
    {
        setName ("OCT");
        for (auto* b : { &down, &up }) { addAndMakeVisible (*b); b->onClick = [this, b] { shift (b == &up ? 1 : -1); }; }
        down.setTooltip ("OCT -: the FLOW strip plays an octave lower"); up.setTooltip ("OCT +: the FLOW strip plays an octave higher");
        refresh();
    }
    void shift (int d) { const int v = juce::jlimit (StripOctave::lo, StripOctave::hi, oct.value + d); if (v == oct.value) return; oct.value = v; refresh(); if (oct.onChange) oct.onChange(); }
    void resized() override { auto r = getLocalBounds(); down.setBounds (r.removeFromLeft (24)); up.setBounds (r.removeFromRight (24)); }
    void paint (juce::Graphics& g) override
    {
        g.setColour (findColour (juce::TextButton::textColourOffId)); g.setFont (ghosty::font (10.0f));
        g.drawText ("OCT " + juce::String (oct.value > 0 ? "+" : "") + juce::String (oct.value), getLocalBounds().reduced (24, 0), juce::Justification::centred);
    }
    juce::TextButton down { "-" }, up { "+" };
private:
    StripOctave& oct;
    void refresh() { down.setEnabled (oct.value > StripOctave::lo); up.setEnabled (oct.value < StripOctave::hi); repaint(); }
};

// ------------------------------------------------------------------ 3. the ghost cursor
// A tiny vector ghost that rides the pointer over the strip: a little lag and bounce, speed lines when he moves fast, a soft
// glow spot under him, an RGB-split glitch flicker every 1.5 .. 3 s (now and then he comes out of it wearing sunglasses),
// and on a key press he squashes while ripple rings and a small particle burst leave the key.
// An overlay child of the strip that never takes a mouse event (it listens to the strip's events instead). The timer
// (30 fps) runs only while something shows; every path is built once in the constructor, paint only fills them.
class GhostCursor : public juce::Component, private juce::Timer, private juce::ComponentListener
{
public:
    static constexpr float sizePx = 14.0f;      // the ghost's size (as drawGhost measures it)

    GhostCursor()
    {
        setName ("GHOST CURSOR");
        setInterceptsMouseClicks (false, false); setOpaque (false);
        eyes.addEllipse (142, 69, 16, 22); eyes.addEllipse (167, 70, 14, 20);
        pupils.addEllipse (150.5f, 73, 5, 5); pupils.addEllipse (174, 73, 4.4f, 4.4f);
        glasses.addRoundedRectangle (136, 68, 24, 17, 6); glasses.addRoundedRectangle (164, 68, 24, 17, 6); glasses.addRectangle (158, 71, 8, 4);
        glassShine.addRectangle (141, 71, 7, 3); glassShine.addRectangle (169, 71, 7, 3);
        circle.addEllipse (-1, -1, 2, 2);
        ring.addEllipse (-1, -1, 2, 2); ring.addEllipse (-0.9f, -0.9f, 1.8f, 1.8f); ring.setUsingNonZeroWinding (false);
        for (int k = 0; k < 3; ++k) speed.addRoundedRectangle (-1.0f - 0.25f * (float) k, -0.45f + 0.45f * (float) k - 0.05f, 0.8f - 0.15f * (float) (k % 2), 0.1f, 0.05f);
    }
    ~GhostCursor() override { detach(); }

    void attach (juce::Component& strip)
    {
        detach();
        target = &strip;
        strip.addAndMakeVisible (this);
        strip.addMouseListener (this, false);
        strip.addComponentListener (this);
        setBounds (strip.getLocalBounds());
    }

    // test hooks
    void forceGlitch (int frames = 2) { glitchFrames = frames; rollGlitch(); }
    void setShadesForTest (bool on) { shades = on; }
    void hoverForTest (juce::Point<float> p) { hovering = true; aim = p; if (alpha <= 0.0f) pos = aim + offset; alpha = 1.0f; wake(); }
    void leaveForTest() { hovering = false; }
    void stepForTest() { timerCallback(); }
    void pressForTest() { press(); }
    void moveForTest (juce::Point<float> p) { aim = p; }
    bool isShowing() const { return alpha > 0.01f; }
    bool isAnimating() const { return isTimerRunning(); }
    bool isGlitching() const { return glitchFrames > 0; }
    float speedNow() const { return vel.getDistanceFromOrigin(); }
    juce::Point<float> ghostPos() const { return pos; }

    void paint (juce::Graphics& g) override
    {
        for (auto& r : rings) if (r.a > 0.01f && r.r > 0.5f)
        { g.setColour (r.c.withAlpha (r.a)); g.fillPath (ring, juce::AffineTransform::scale (r.r).translated (r.x, r.y)); }
        for (auto& q : parts) if (q.a > 0.01f)
        { g.setColour (q.c.withAlpha (q.a)); g.fillPath (circle, juce::AffineTransform::scale (q.sz).translated (q.x, q.y)); }
        if (alpha < 0.01f) return;

        float a = alpha;
        auto p = pos + juce::Point<float> (0.0f, std::sin (bob) * 1.2f);
        {   // the soft glow spot under him
            const auto gc = shades ? ghosty::col::pink : ghosty::col::slime;
            for (int k = 0; k < 4; ++k)
            {
                const float r = sizePx * (0.55f + 0.28f * (float) k);
                g.setColour (gc.withAlpha (a * 0.07f));
                g.fillPath (circle, juce::AffineTransform::scale (r, r * 0.42f).translated (p.x, p.y + sizePx * 0.85f));
            }
        }
        const float sp = vel.getDistanceFromOrigin();
        if (sp > 2.5f)
        {   // speed lines behind him, along the way he goes
            const float ang = std::atan2 (vel.y, vel.x), len = sizePx * juce::jmin (2.2f, 0.6f + sp * 0.12f);
            g.setColour (ghosty::col::ice.withAlpha (a * juce::jmin (0.85f, (sp - 2.5f) * 0.12f)));
            g.fillPath (speed, juce::AffineTransform::scale (len, sizePx * 0.9f).rotated (ang).translated (p.x - std::cos (ang) * sizePx * 0.4f, p.y - std::sin (ang) * sizePx * 0.4f));
        }
        const float sq = juce::jlimit (-0.45f, 0.45f, squash);
        const float s = sizePx / 118.0f;
        auto at = [&] (juce::Point<float> c)
        {   // anchored low, so a squash presses him down into the key
            return juce::AffineTransform::translation (-130.0f, -150.0f).scaled ((faceLeft ? -s : s) * (1.0f + sq), s * (1.0f - sq)).translated (c.x, c.y + sizePx * 0.37f);
        };
        if (glitchFrames > 0)
        {   // RGB split + offset jitter + flicker
            a *= flicker;
            g.setColour (ghosty::col::slime.withAlpha (a * 0.55f)); g.fillPath (ghosty::cuteBody(), at (p + juce::Point<float> (-split, 0.0f)));
            g.setColour (ghosty::col::pink.withAlpha (a * 0.55f));  g.fillPath (ghosty::cuteBody(), at (p + juce::Point<float> (split, jitter.y * 0.5f)));
            p += jitter;
        }
        const auto t = at (p);
        g.setColour (ghosty::col::slime.withAlpha (a)); g.fillPath (ghosty::cuteDrips(), t);
        g.setColour (ghosty::col::white.withAlpha (a)); g.fillPath (ghosty::cuteBody(), t);
        if (pghost::on()) { g.setColour (ghosty::glass::ink().withAlpha (a * 0.85f)); g.strokePath (ghosty::cuteBody(), juce::PathStrokeType (1.1f / s), t); }   // GHOST LOOK: outlined on the glass
        g.setColour (ghosty::col::slime.withAlpha (a)); g.fillPath (ghosty::cuteSlime(), t);
        if (shades)
        {
            g.setColour (juce::Colour (0xff0b0c10).withAlpha (a)); g.fillPath (glasses, t);
            g.setColour (ghosty::col::ice.withAlpha (a * 0.8f)); g.fillPath (glassShine, t);
        }
        else
        {
            g.setColour (ghosty::col::bg.withAlpha (a)); g.fillPath (eyes, t);
            g.setColour (ghosty::col::ecto.withAlpha (a)); g.fillPath (pupils, t);
        }
    }

    void mouseEnter (const juce::MouseEvent& e) override { hovering = true; aimAt (e); if (alpha <= 0.0f) { pos = aim + offset; vel = {}; } wake(); }
    void mouseMove  (const juce::MouseEvent& e) override { hovering = true; aimAt (e); wake(); }
    void mouseDrag  (const juce::MouseEvent& e) override { hovering = true; aimAt (e); wake(); }
    void mouseDown  (const juce::MouseEvent& e) override { aimAt (e); press(); }
    void mouseUp    (const juce::MouseEvent& e) override { squashV += 0.25f; hovering = getLocalBounds().contains (e.getEventRelativeTo (this).position.toInt()); wake(); }
    void mouseExit  (const juce::MouseEvent&) override  { hovering = false; wake(); }

private:
    struct Ring { float x = 0, y = 0, r = 0, a = 0; juce::Colour c; };
    struct Part { float x = 0, y = 0, vx = 0, vy = 0, a = 0, sz = 1; juce::Colour c; };
    juce::Component* target = nullptr;
    juce::Path eyes, pupils, glasses, glassShine, circle, ring, speed;
    Ring rings[9]; Part parts[30];
    int nextRing = 0, nextPart = 0;
    juce::Point<float> pos, vel, aim, jitter;
    const juce::Point<float> offset { 11.0f, -12.0f };   // he floats up-right of the pointer, never under the finger
    float alpha = 0.0f, squash = 0.0f, squashV = 0.0f, bob = 0.0f, split = 0.0f, flicker = 1.0f;
    bool hovering = false, shades = false, faceLeft = false;
    int glitchFrames = 0, untilGlitch = 60;
    juce::Rectangle<int> lastDirty;
    juce::Random rng { 0x9057 };

    void detach()
    {
        stopTimer();
        if (target != nullptr) { target->removeMouseListener (this); target->removeComponentListener (this); target = nullptr; }
    }
    void componentMovedOrResized (juce::Component& c, bool, bool) override { if (&c == target) setBounds (c.getLocalBounds()); }
    void aimAt (const juce::MouseEvent& e) { aim = e.getEventRelativeTo (this).position; }
    void wake() { if (! isTimerRunning()) { untilGlitch = 45 + rng.nextInt (46); startTimerHz (30); } }

    void press()
    {
        squashV -= 0.55f;
        static const juce::Colour cols[] { ghosty::col::slime, ghosty::col::ice, ghosty::col::ecto, ghosty::col::pink };
        for (int k = 0; k < 3; ++k)    // concentric rings, one after the other
        { auto& r = rings[nextRing]; nextRing = (nextRing + 1) % 9; r = { aim.x, aim.y, -6.0f * (float) k, 0.9f, cols[k % 3] }; }
        for (int k = 0; k < 10; ++k)   // a small particle burst from the key
        {
            auto& q = parts[nextPart]; nextPart = (nextPart + 1) % 30;
            const float ang = rng.nextFloat() * juce::MathConstants<float>::twoPi, v = 1.5f + rng.nextFloat() * 3.0f;
            q = { aim.x, aim.y, std::cos (ang) * v, std::sin (ang) * v - 1.2f, 1.0f, 0.8f + rng.nextFloat() * 1.0f, cols[rng.nextInt (4)] };
        }
        wake();
    }
    void rollGlitch()
    {
        split = 1.0f + rng.nextFloat() * 1.5f;
        jitter = { (rng.nextFloat() - 0.5f) * 5.0f, (rng.nextFloat() - 0.5f) * 3.0f };
        flicker = 0.35f + rng.nextFloat() * 0.65f;
    }
    bool draggingElsewhere() const
    {
        const auto& ms = juce::Desktop::getInstance().getMainMouseSource();
        if (! ms.isDragging()) return false;
        auto* c = ms.getComponentUnderMouse();
        return c != nullptr && c != target && ! (target != nullptr && target->isParentOf (c));
    }

    void timerCallback() override
    {
        const bool show = hovering && ! draggingElsewhere();
        alpha = show ? juce::jmin (1.0f, alpha + 0.2f) : juce::jmax (0.0f, alpha - 0.12f);
        // a springy follow: a little lag and a little overshoot
        auto want = aim + offset;
        want.x = juce::jlimit (8.0f, juce::jmax (8.0f, (float) getWidth() - 8.0f), want.x);
        want.y = juce::jlimit (10.0f, juce::jmax (10.0f, (float) getHeight() - 10.0f), want.y);
        vel = vel * 0.62f + (want - pos) * 0.22f;
        pos += vel;
        if (vel.x < -0.8f) faceLeft = true; else if (vel.x > 0.8f) faceLeft = false;
        squashV = squashV * 0.72f - squash * 0.35f;
        squash += squashV;
        bob += 0.16f;
        // the glitch: every 1.5 .. 3 s, for 1-2 frames; now and then he comes out of it with (or without) his sunglasses
        if (glitchFrames > 0) { if (--glitchFrames == 0 && rng.nextFloat() < 0.15f) shades = ! shades; }
        else if (--untilGlitch <= 0) { glitchFrames = 1 + rng.nextInt (2); untilGlitch = 45 + rng.nextInt (46); }
        if (glitchFrames > 0) rollGlitch();
        bool fx = false;
        auto dirty = juce::Rectangle<int>();
        for (auto& r : rings) if (r.a > 0.01f)
        { r.r += 2.6f; r.a *= 0.9f; fx = true; if (r.r > 0) dirty = dirty.getUnion (juce::Rectangle<float> (r.x - r.r - 2, r.y - r.r - 2, 2 * r.r + 4, 2 * r.r + 4).getSmallestIntegerContainer()); }
        for (auto& q : parts) if (q.a > 0.01f)
        { q.x += q.vx; q.y += q.vy; q.vy += 0.25f; q.vx *= 0.96f; q.a *= 0.88f; fx = true; dirty = dirty.getUnion (juce::Rectangle<int> ((int) q.x - 4, (int) q.y - 4, 8, 8)); }
        const int r = (int) (sizePx * 2.6f) + 8;
        if (alpha > 0.0f) dirty = dirty.getUnion (juce::Rectangle<int> ((int) pos.x - r, (int) pos.y - r, 2 * r, 2 * r + (int) sizePx));
        repaint (dirty.getUnion (lastDirty));
        lastDirty = dirty;
        if (alpha <= 0.0f && ! show && ! fx && std::abs (squash) < 0.01f) stopTimer();
    }
};
} // namespace keysx
