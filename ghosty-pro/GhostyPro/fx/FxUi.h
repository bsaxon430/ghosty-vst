#pragma once
// Ghost FX rack: shared UI widgets (look and feel, knobs, toggles, segmented choices, glow helpers, page base).
#include <juce_audio_processors/juce_audio_processors.h>
#include "GhostArt.h"
#include "GhostUI.h"
#include "FxCore.h"
#include "FxTheme.h"
#include "../ProGlass.h"     // GLASS (round 3): Ghosty Pro's glass LIGHT materials (never on in Ghost Box: it does not opt in)
#include "GhostGlass.h"      // GHOST (Ghost Box only): the liquid-glass primitives (aurora, frosted panels, pills, glossy knobs)

namespace gpfx {
namespace ui {

using ghosty::font;
using fxtheme::T;
namespace col = fxtheme::col;   // the ghost palette, themed (FxTheme.h)

// GLASS (round 3): true only while Ghosty Pro paints its glass LIGHT look
inline bool glass() { return fxtheme::light() && ghosty::themes::glassLight(); }
// GHOST (Ghost Box's 4th look, fxtheme::ghost()): liquid glass on the LIGHT base. Constant false in Ghosty Pro (FxTheme.h).
inline bool ghost() { return fxtheme::ghost(); }
namespace ghostglass
{
    // PERFORMANCE: the pills and the knob caps are rendered once per (size, state, display scale) into small cached images
    struct ImageCache
    {
        std::map<std::tuple<int, int, int, int, int>, juce::Image> m; int builds = 0;
        template <typename Fn> void draw (juce::Graphics& g, juce::Rectangle<float> r, int kind, int state, Fn&& paint)
        {
            const float sc = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
            const int w = juce::roundToInt (r.getWidth()), h = juce::roundToInt (r.getHeight());
            if (w <= 0 || h <= 0) return;
            const auto k = std::make_tuple (kind, w, h, state, juce::roundToInt (sc * 100));
            auto it = m.find (k);
            if (it == m.end())
            {
                if (m.size() > 400) m.clear();
                juce::Image img (juce::Image::ARGB, juce::jmax (1, juce::roundToInt ((float) w * sc)), juce::jmax (1, juce::roundToInt ((float) h * sc)), true, juce::SoftwareImageType());
                { juce::Graphics ig (img); ig.addTransform (juce::AffineTransform::scale (sc)); paint (ig, juce::Rectangle<float> (0, 0, (float) w, (float) h)); }
                it = m.emplace (k, img).first; ++builds;
            }
            if (sc == 1.0f) g.drawImageAt (it->second, juce::roundToInt (r.getX()), juce::roundToInt (r.getY()));
            else g.drawImageTransformed (it->second, juce::AffineTransform::scale (1.0f / sc).translated (std::round (r.getX()), std::round (r.getY())));
        }
    };
    inline ImageCache& cache() { static ImageCache c; return c; }
    // a ghost drawn on glass gets an ink outline so a white / pale body still reads on the frosted panels
    inline void outline (juce::Graphics& g, float cx, float cy, float size, float alpha = 1.0f, bool flip = false)
    {
        const float s = size / 118.0f;
        const auto t = juce::AffineTransform::translation (-130.0f, -106.0f).scaled (flip ? -s : s, s).translated (cx, cy);
        juce::Path p = ghosty::cuteBody(); p.addPath (ghosty::cuteSlime()); p.addPath (ghosty::cuteDrips()); p.applyTransform (t);
        g.setColour (ghosty::glass::ink().withAlpha (0.78f * alpha));
        g.strokePath (p, juce::PathStrokeType (juce::jlimit (1.0f, 1.8f, size * 0.035f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    // a glass display screen (visualisers, lanes): a frosted inset over the panel, a soft inner shadow at the top, a white lower rim
    inline void screen (juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour accent)
    {
        radius = juce::jmin (radius, r.getHeight() * 0.5f, r.getWidth() * 0.5f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe6e1f4), r.getX(), r.getY(), juce::Colour (0xffeeebf8), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, radius);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0x223a2e6b), r.getX(), r.getY(), juce::Colour (0x003a2e6b), r.getX(), r.getY() + juce::jmin (18.0f, r.getHeight() * 0.3f), false));
        g.fillRoundedRectangle (r, radius);
        g.setColour (juce::Colour (0x4a463e6c)); g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
        g.setColour (juce::Colour (0xccffffff)); g.drawLine (r.getX() + radius, r.getBottom() - 0.6f, r.getRight() - radius, r.getBottom() - 0.6f, 1.2f);
        g.setColour (accent.withAlpha (0.22f)); g.drawRoundedRectangle (r.reduced (1.5f), juce::jmax (0.0f, radius - 1.0f), 1.0f);
    }
    // the glossy knob: a glass cap on a soft shadow, a violet -> teal value arc on a frosted track
    inline void knob (juce::Graphics& g, float cx, float cy, float rad, float a0, float a1, float from, float ang, bool hot, bool enabled)
    {
        juce::Path track; track.addCentredArc (cx, cy, rad - 3, rad - 3, 0, a0, a1, true);
        g.setColour (juce::Colour (0x30443a7a)); g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (std::abs (ang - from) > 0.001f)
        {
            juce::Path val; val.addCentredArc (cx, cy, rad - 3, rad - 3, 0, juce::jmin (from, ang), juce::jmax (from, ang), true);
            if (enabled) g.setGradientFill (juce::ColourGradient (ghosty::glass::violet(), cx - rad, cy + rad * 0.3f, ghosty::glass::teal(), cx + rad, cy - rad * 0.3f, false));
            else g.setColour (juce::Colour (0x60443a7a));
            g.strokePath (val, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        const float cr = juce::roundToInt (rad * 0.66f * 2.0f) * 0.5f;   // (half-pixel steps: one cached cap per size)
        const float pad = juce::jmax (2.0f, cr * 0.25f) * 1.6f + 2.0f;
        const auto box = juce::Rectangle<float> (cx - cr - pad, cy - cr - pad, (cr + pad) * 2.0f, (cr + pad) * 2.0f);
        cache().draw (g, box, 2, hot ? 1 : 0, [cr, pad, hot] (juce::Graphics& ig, juce::Rectangle<float> b)
        {
            const float x = b.getCentreX(), y = b.getCentreY(); juce::ignoreUnused (pad);
            const juce::Rectangle<float> cap (x - cr, y - cr, cr * 2, cr * 2);
            ghosty::glass::shadow (ig, cap, cr, juce::jmax (2.0f, cr * 0.25f), 1.0f, 4);
            ig.setGradientFill (juce::ColourGradient (juce::Colours::white, x - cr * 0.4f, cap.getY() + 1, juce::Colour (0xffdcd4f5), x, cap.getBottom(), false)); ig.fillEllipse (cap);
            ig.setColour (juce::Colour (0xb0ffffff)); ig.drawEllipse (cap.reduced (0.7f), 1.4f);
            ig.setColour (hot ? juce::Colour (0x908b5cf6) : juce::Colour (0x50a06bff)); ig.drawEllipse (cap.reduced (cr * 0.18f), 0.8f);
        });
        juce::Path ptr; ptr.startNewSubPath (cx + std::sin (ang) * cr * 0.30f, cy - std::cos (ang) * cr * 0.30f);
        ptr.lineTo (cx + std::sin (ang) * cr * 0.80f, cy - std::cos (ang) * cr * 0.80f);
        g.setColour (enabled ? ghosty::glass::violetDeep() : juce::Colour (0x80463e6c));
        g.strokePath (ptr, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    // a dropdown well: a glass pill and a violet chevron
    inline void pill (juce::Graphics& g, juce::Rectangle<float> bounds, bool on, bool over, bool down, bool enabled);
    inline void combo (juce::Graphics& g, juce::Rectangle<float> r, bool over, bool enabled)
    {
        pill (g, r, false, over, false, enabled);
        const auto b = ghosty::glass::pillBody (r);
        juce::Path a; const float ax = b.getRight() - 12.0f, ay = b.getCentreY();
        a.startNewSubPath (ax - 4.0f, ay - 2.0f); a.lineTo (ax, ay + 2.5f); a.lineTo (ax + 4.0f, ay - 2.0f);
        g.setColour (enabled ? ghosty::glass::violetDeep() : juce::Colour (0x90463e6c));
        g.strokePath (a, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    inline juce::Colour disabledInk() { return juce::Colour (0xff6a6390); }   // >= 3:1 on the off pills (greyed controls)
    inline void pill (juce::Graphics& g, juce::Rectangle<float> bounds, bool on, bool over, bool down, bool enabled = true)
    {
        cache().draw (g, bounds, 1, (on ? 1 : 0) | (over ? 2 : 0) | (down ? 4 : 0) | (enabled ? 8 : 0),
                      [=] (juce::Graphics& ig, juce::Rectangle<float> r) { ghosty::glass::pill (ig, r, on, over, down, enabled); });
    }
}

inline juce::Colour accentOf (int unit) { return T (juce::Colour (unitInfo (unit).accent)); }

// Stroke a path with a soft neon glow underneath.
inline void glowStroke (juce::Graphics& g, const juce::Path& p, juce::Colour c, float w, float glow = 1.0f)
{
    if (glow > 0.01f)
    {
        g.setColour (c.withMultipliedAlpha (0.07f * glow)); g.strokePath (p, juce::PathStrokeType (w * 7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (c.withMultipliedAlpha (0.14f * glow)); g.strokePath (p, juce::PathStrokeType (w * 3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour (c); g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
inline void glowDot (juce::Graphics& g, juce::Point<float> p, float r, juce::Colour c)
{
    juce::ColourGradient gr (c.withAlpha (0.55f), p.x, p.y, c.withAlpha (0.0f), p.x + r * 3.0f, p.y, true);
    g.setGradientFill (gr); g.fillEllipse (p.x - r * 3.0f, p.y - r * 3.0f, r * 6.0f, r * 6.0f);
    g.setColour (c); g.fillEllipse (p.x - r, p.y - r, r * 2, r * 2);
}
// Dark inset screen used behind every visualiser.
inline void drawScreen (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour accent, float radius = 10.0f)
{
    if (glass()) { progl::screen (g, r, radius); g.setColour (accent.withAlpha (0.25f)); g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f); return; }   // GLASS (round 3)
    if (ghost()) { ghostglass::screen (g, r, radius, accent); return; }   // GHOST: a glass screen
    juce::ColourGradient bg (T (juce::Colour (0xff07080c)), r.getX(), r.getY(), T (juce::Colour (0xff0d0f17)), r.getX(), r.getBottom(), false);
    g.setGradientFill (bg); g.fillRoundedRectangle (r, radius);
    g.setColour (accent.withAlpha (0.10f)); g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f)); g.drawRoundedRectangle (r.reduced (1.5f), radius - 1, 1.0f);
}
inline void drawCaption (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& s, juce::Colour c, float h = 10.0f,
                         juce::Justification j = juce::Justification::centredLeft)
{
    g.setColour (c); g.setFont (font (h)); g.drawText (s, r, j, false);
}


// Chain order tips: a short, friendly note when the processing order is unusual (empty = nothing to say).
// Only units that are switched on (plus the one just moved) count.
inline juce::String orderTip (const std::array<int, kNumUnits>& order, const std::array<bool, kNumUnits>& on, int moved)
{
    auto pos = [&order] (int u) { for (int k = 0; k < kNumUnits; ++k) if (order[(size_t) k] == u) return k; return -1; };
    auto live = [&on, moved] (int u) { return u >= 0 && u < kNumUnits && (on[(size_t) u] || u == moved); };
    std::vector<int> verbs { uSpace, uShimmer, uCrystal }, dirt { uWreck, uTube, uCrush }, comp { uOtt, uCrush }, rhythm { uGate, uGrid, uHaunt };
#if GPFX_BOX
    verbs.push_back (uHalls); dirt.push_back (uCrackle); dirt.push_back (uFatten); dirt.push_back (uRetro);
#endif
    auto before = [&] (const std::vector<int>& a, const std::vector<int>& b, int& ua, int& ub)
    {
        for (int x : a) for (int y : b)
            if (x != y && live (x) && live (y) && (on[(size_t) x] || on[(size_t) y]) && (x == moved || y == moved || (on[(size_t) x] && on[(size_t) y])) && pos (x) < pos (y)) { ua = x; ub = y; return true; }
        return false;
    };
    int a = -1, b = -1;
    auto nm = [] (int u) { return juce::String (unitInfo (u).shortName); };
    if (before (verbs, dirt, a, b)) return nm (a) + " before " + nm (b) + ": reverb into distortion = big messy tail. Put the reverb after the drive for a cleaner space.";
    if (before ({ uEcho }, dirt, a, b)) return nm (a) + " before " + nm (b) + ": every echo gets driven again, gritty and loud. Lower the feedback or swap them.";
    if (before (verbs, rhythm, a, b)) return nm (a) + " before " + nm (b) + ": the gate chops the reverb tail into rhythm. Put the gate first for a clean gated sound.";
    if (before (verbs, comp, a, b) || before ({ uEcho }, comp, a, b)) return nm (a) + " before " + nm (b) + ": compression after the space pumps the tail up. Great as an effect, can get muddy.";
#if GPFX_BOX
    if (before (verbs, { uSplit }, a, b) || before ({ uEcho }, { uSplit }, a, b)) return nm (a) + " before SPLIT: the hits are already smeared, SPLIT finds fewer clean attacks. Put SPLIT early.";
    if (before (verbs, { uWarp }, a, b)) return nm (a) + " before WARP: the pitch bend bends the whole reverb tail too.";
#endif
    juce::ignoreUnused (a, b);
    return {};
}

// ---------------------------------------------------------------- look and feel
class LnF : public ghosty::GhostLnF
{
public:
    LnF()
    {
        setColour (juce::Slider::textBoxTextColourId, col::white.withAlpha (0.85f));
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, col::ecto.withAlpha (0.5f));
        setColour (juce::TextEditor::backgroundColourId, T (juce::Colour (0xff06070a)));
        setColour (juce::TextEditor::textColourId, col::white);
        setColour (juce::TextEditor::outlineColourId, col::ecto.withAlpha (0.4f));
        setColour (juce::TextEditor::focusedOutlineColourId, col::slime);
        setColour (juce::CaretComponent::caretColourId, col::slime);
        setColour (juce::BubbleComponent::backgroundColourId, col::card);
        setColour (juce::TooltipWindow::backgroundColourId, col::card);
        setColour (juce::TooltipWindow::textColourId, col::white);
        setColour (juce::TooltipWindow::outlineColourId, col::ecto.withAlpha (0.6f));
        setColour (juce::BubbleComponent::outlineColourId, col::ecto.withAlpha (0.6f));
        for (int id : themedIds()) base.push_back ({ id, findColour (id) });   // the DARK values (built while the theme may be LIGHT: unmapped)
        for (auto& b : base) b.second = fxtheme::unmap (b.second);
        applyTheme();
    }
    // re-apply the look-and-feel colours for the current theme (call after fxtheme::set, then sendLookAndFeelChange)
    void applyTheme()
    {
        for (auto& [id, c] : base) setColour (id, fxtheme::T (c));
        if (ghost())
        {   // GHOST: the menu highlight is the violet glass with white text; tooltips / menus on the glass card (drawTooltip / drawPopupMenuBackground)
            setColour (juce::PopupMenu::highlightedBackgroundColourId, ghosty::glass::violetDeep()); setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
            setColour (juce::PopupMenu::textColourId, ghosty::glass::ink()); setColour (juce::TooltipWindow::textColourId, ghosty::glass::ink());
        }
    }
    static const std::vector<int>& themedIds()
    {
        static const std::vector<int> ids {
            juce::Slider::textBoxTextColourId, juce::Slider::textBoxHighlightColourId, juce::Slider::textBoxOutlineColourId,
            juce::TextEditor::backgroundColourId, juce::TextEditor::textColourId, juce::TextEditor::outlineColourId, juce::TextEditor::focusedOutlineColourId,
            juce::TextEditor::highlightColourId, juce::TextEditor::highlightedTextColourId, juce::CaretComponent::caretColourId,
            juce::BubbleComponent::backgroundColourId, juce::BubbleComponent::outlineColourId, juce::TooltipWindow::backgroundColourId,
            juce::TooltipWindow::textColourId, juce::TooltipWindow::outlineColourId, juce::ResizableWindow::backgroundColourId,
            juce::TextButton::buttonColourId, juce::TextButton::buttonOnColourId, juce::TextButton::textColourOffId, juce::TextButton::textColourOnId,
            juce::ComboBox::backgroundColourId, juce::ComboBox::outlineColourId, juce::ComboBox::textColourId, juce::ComboBox::arrowColourId,
            juce::PopupMenu::backgroundColourId, juce::PopupMenu::highlightedBackgroundColourId, juce::PopupMenu::textColourId,
            juce::PopupMenu::highlightedTextColourId, juce::PopupMenu::headerTextColourId, juce::Label::textColourId, juce::Label::backgroundColourId,
            juce::ScrollBar::thumbColourId, juce::ScrollBar::trackColourId, juce::ListBox::backgroundColourId, juce::ListBox::textColourId,
            juce::AlertWindow::backgroundColourId, juce::AlertWindow::textColourId, juce::AlertWindow::outlineColourId };
        return ids;
    }
    std::vector<std::pair<int, juce::Colour>> base;

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override
    {
        const auto acc = s.findColour (juce::Slider::rotarySliderFillColourId);
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3);
        const float rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f, cx = r.getCentreX(), cy = r.getCentreY();
        const bool hot = s.isMouseOverOrDragging();
        if (ghost())
        {   // GHOST: a glossy glass knob with the violet -> teal arc; the unit accent stays as the small end dot
            const bool bip = s.getMinimum() < 0 && s.getMaximum() > 0;
            const float an = a0 + pos * (a1 - a0), fr = bip ? (a0 + (float) s.valueToProportionOfLength (0.0) * (a1 - a0)) : a0;
            ghostglass::knob (g, cx, cy, rad, a0, a1, fr, an, hot, s.isEnabled());
            g.setColour (acc); g.fillEllipse (cx + std::sin (an) * (rad - 3) - 2.0f, cy - std::cos (an) * (rad - 3) - 2.0f, 4, 4);
            return;
        }
        // halo
        juce::ColourGradient halo (acc.withAlpha (hot ? 0.20f : 0.10f), cx, cy, acc.withAlpha (0.0f), cx + rad * 1.25f, cy, true);
        g.setGradientFill (halo); g.fillEllipse (cx - rad * 1.25f, cy - rad * 1.25f, rad * 2.5f, rad * 2.5f);
        juce::Path track; track.addCentredArc (cx, cy, rad - 3, rad - 3, 0, a0, a1, true);
        g.setColour (T (juce::Colours::white).withAlpha (0.07f));
        g.strokePath (track, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const bool bipolar = s.getMinimum() < 0 && s.getMaximum() > 0;
        const float ang = a0 + pos * (a1 - a0);
        const float from = bipolar ? (a0 + (float) s.valueToProportionOfLength (0.0) * (a1 - a0)) : a0;
        juce::Path val; val.addCentredArc (cx, cy, rad - 3, rad - 3, 0, juce::jmin (from, ang), juce::jmax (from, ang), true);
        g.setColour (acc.withAlpha (0.25f)); g.strokePath (val, juce::PathStrokeType (8.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        juce::ColourGradient gr (acc.darker (0.3f), cx - rad, cy + rad, acc.brighter (0.4f), cx + rad, cy - rad, false);
        g.setGradientFill (gr); g.strokePath (val, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        // cap
        const float cr = rad * 0.64f;
        if (glass()) progl::knobFace (g, cx, cy, cr);     // GLASS (round 3): a glassy cap
        else
        {
        juce::ColourGradient cap (T (juce::Colour (0xff2a2e3e)), cx, cy - cr, T (juce::Colour (0xff12141c)), cx, cy + cr, false);
        g.setGradientFill (cap); g.fillEllipse (cx - cr, cy - cr, cr * 2, cr * 2);
        g.setColour (T (juce::Colours::white).withAlpha (0.06f)); g.drawEllipse (cx - cr, cy - cr, cr * 2, cr * 2, 1.0f);
        }
        juce::Path ptr; ptr.startNewSubPath (cx + std::sin (ang) * cr * 0.35f, cy - std::cos (ang) * cr * 0.35f);
        ptr.lineTo (cx + std::sin (ang) * cr * 0.92f, cy - std::cos (ang) * cr * 0.92f);
        g.setColour (hot ? acc.brighter (0.5f) : col::white.withAlpha (0.9f));
        g.strokePath (ptr, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (acc); g.fillEllipse (cx + std::sin (ang) * (rad - 3) - 2.5f, cy - std::cos (ang) * (rad - 3) - 2.5f, 5, 5);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle st, juce::Slider& s) override
    {
        const auto acc = s.findColour (juce::Slider::rotarySliderFillColourId);
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        if (ghost() && st == juce::Slider::LinearHorizontal)
        {   // GHOST: a glass track, the violet -> teal fill, a glossy thumb
            const float d = juce::jlimit (9.0f, 14.0f, (float) h - 2.0f);
            ghosty::glass::track (g, r.withSizeKeepingCentre (r.getWidth(), 6.0f), r.getX(), pos, s.isEnabled());
            ghosty::glass::thumb (g, { juce::jlimit (r.getX() + d * 0.5f - 2.0f, r.getRight() - d * 0.5f + 2.0f, pos), r.getCentreY() }, d);
            return;
        }
        if (ghost())
        {
            const float d = juce::jlimit (9.0f, 14.0f, (float) w - 2.0f);
            auto tr = r.withSizeKeepingCentre (6.0f, r.getHeight());
            g.setColour (juce::Colour (0x30443a7a)); g.fillRoundedRectangle (tr, 3.0f);
            g.setGradientFill (juce::ColourGradient (ghosty::glass::teal(), 0, tr.getY(), ghosty::glass::violet(), 0, tr.getBottom(), false));
            g.fillRoundedRectangle (tr.withTop (pos), 3.0f);
            ghosty::glass::thumb (g, { tr.getCentreX(), juce::jlimit (r.getY() + d * 0.5f - 2.0f, r.getBottom() - d * 0.5f + 2.0f, pos) }, d);
            return;
        }
        if (st == juce::Slider::LinearHorizontal)
        {
            auto tr = r.withSizeKeepingCentre (r.getWidth(), 4.0f);
            g.setColour (T (juce::Colours::white).withAlpha (0.07f)); g.fillRoundedRectangle (tr, 2);
            g.setColour (acc); g.fillRoundedRectangle (tr.withRight (pos), 2);
            glowDot (g, { pos, tr.getCentreY() }, 5.0f, acc);
        }
        else
        {
            auto tr = r.withSizeKeepingCentre (4.0f, r.getHeight());
            g.setColour (T (juce::Colours::white).withAlpha (0.07f)); g.fillRoundedRectangle (tr, 2);
            g.setColour (acc); g.fillRoundedRectangle (tr.withTop (pos), 2);
            glowDot (g, { tr.getCentreX(), pos }, 5.0f, acc);
        }
    }

    // a lit (toggled-on) button: the fill (the accent, nudged until the ink reads) and the ink (dark or light, whichever reads better)
    static std::pair<juce::Colour, juce::Colour> litColours (juce::Colour acc)
    {
        acc = acc.withAlpha (1.0f);
        const juce::Colour dark = col::bg, lightInk = col::white;
        const juce::Colour inkDark = fxtheme::luminance (dark) < fxtheme::luminance (lightInk) ? dark : lightInk;
        const juce::Colour inkLight = inkDark == dark ? lightInk : dark;
        const bool useDark = fxtheme::contrast (inkDark, acc) >= fxtheme::contrast (inkLight, acc);
        const auto ink = useDark ? inkDark : inkLight;
        for (int i = 0; i < 40 && fxtheme::contrast (ink, acc) < 7.0; ++i) acc = useDark ? acc.brighter (0.06f) : acc.darker (0.06f);
        return { acc, ink };
    }
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (1);
        const bool on = b.getToggleState();
        const auto acc = b.findColour (juce::TextButton::buttonOnColourId);
        if (glass() && ! on) { progl::pill (g, r, 7, false, acc, over || down); return; }     // GLASS (round 3): a glass pill (lit = the original accent fill)
        if (ghost()) { ghostglass::pill (g, b.getLocalBounds().toFloat(), on, over, down, b.isEnabled()); return; }   // GHOST: a glass pill, lit = violet glass
        if (on) { g.setColour (acc.withAlpha (0.18f)); g.fillRoundedRectangle (r.expanded (1.5f), 8); }
        const auto lit = litColours (acc);
        juce::ColourGradient gr (on ? lit.first.brighter (0.08f) : T (juce::Colour (0xff20232f)), 0, r.getY(),
                                 on ? lit.first.darker (0.08f) : T (juce::Colour (0xff13151d)), 0, r.getBottom(), false);   // a lit tab: opaque, its text >= 4.5:1
        g.setGradientFill (gr); g.fillRoundedRectangle (r, 7);
        g.setColour ((on ? acc : col::ecto).withAlpha (over || down ? 0.8f : 0.3f)); g.drawRoundedRectangle (r, 7, 1.0f);
    }
    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        g.setFont (font (juce::jmin (12.0f, (float) b.getHeight() * 0.5f)));
        if (b.getProperties().contains ("fitText"))
        {   // (small buttons that asked for it: the label shrinks to fit, down to 9 px, instead of being cut to "SPL...")
            float fs = juce::jmin (12.0f, (float) b.getHeight() * 0.5f); const float avail = (float) b.getWidth() - (ghost() ? 10.0f : 6.0f);
            while (fs > 9.0f && ghosty::textWidth (font (fs), b.getButtonText()) > avail) fs -= 0.5f;
            g.setFont (font (fs));
            if (ghosty::textWidth (font (fs), b.getButtonText()) > avail)
            {   // still too wide at 9 px: squeeze it a little (never below 75 %) rather than cut it
                g.setColour (ghost() ? (! b.isEnabled() ? ghostglass::disabledInk() : b.getToggleState() ? juce::Colours::white : ghosty::glass::ink())
                                     : b.getToggleState() ? litColours (b.findColour (juce::TextButton::buttonOnColourId)).second : col::white.withAlpha (b.isEnabled() ? (fxtheme::theme() == fxtheme::Theme::Dark ? 0.85f : 1.0f) : fxtheme::kDisabledAlpha));
                g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (ghost() ? 5 : 3, 0), juce::Justification::centred, 1, 0.75f);
                return;
            }
        }
        if (ghost())
        {   // GHOST: white on the violet glass, deep indigo on the clear glass
            g.setColour (! b.isEnabled() ? ghostglass::disabledInk() : b.getToggleState() ? juce::Colours::white : ghosty::glass::ink());
            g.drawText (b.getButtonText(), ghosty::glass::pillBody (b.getLocalBounds().toFloat()).toNearestInt().reduced (3, 0), juce::Justification::centred, true);
            return;
        }
        if (b.getToggleState()) g.setColour (litColours (b.findColour (juce::TextButton::buttonOnColourId)).second);
        else g.setColour (col::white.withAlpha (b.isEnabled() ? (fxtheme::theme() == fxtheme::Theme::Dark ? 0.85f : 1.0f) : fxtheme::kDisabledAlpha));   // full ink on the lighter looks
        g.drawText (b.getButtonText(), b.getLocalBounds().reduced (3, 0), juce::Justification::centred, true);
    }
    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& c) override
    {
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        if (ghost()) { ghostglass::combo (g, juce::Rectangle<float> (0, 0, (float) w, (float) h), c.isMouseOver (true), c.isEnabled()); return; }   // GHOST: a glass pill
        if (glass()) progl::well (g, r, 6, T (juce::Colour (0xff07080c)), c.findColour (juce::ComboBox::outlineColourId).withAlpha (c.isMouseOver() ? 0.8f : 0.5f));   // GLASS (round 3)
        else
        {
        g.setColour (T (juce::Colour (0xff07080c))); g.fillRoundedRectangle (r, 6);
        g.setColour (c.findColour (juce::ComboBox::outlineColourId).withAlpha (c.isMouseOver() ? 0.8f : 0.4f)); g.drawRoundedRectangle (r, 6, 1.0f);
        }
        juce::Path ar; const float ax = (float) w - 13, ay = (float) h * 0.5f;
        ar.addTriangle (ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
        g.setColour (c.findColour (juce::ComboBox::arrowColourId)); g.fillPath (ar);
    }
    juce::Font getComboBoxFont (juce::ComboBox& c) override { return font (juce::jmin (13.0f, (float) c.getHeight() * 0.52f)); }
    void positionComboBoxText (juce::ComboBox& c, juce::Label& l) override
    {
        l.setBounds (6, 1, c.getWidth() - 22, c.getHeight() - 2); l.setFont (getComboBoxFont (c));
        l.setColour (juce::Label::textColourId, col::white);
    }
    // GHOST: menus and tooltips on a glass card (every other look keeps the house drawing)
    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        if (! ghost()) { ghosty::GhostLnF::drawPopupMenuBackground (g, w, h); return; }
        g.fillAll (juce::Colour (0xfff4f1fc)); ghosty::glass::card (g, juce::Rectangle<float> (0, 0, (float) w, (float) h), 6.0f);
    }
    void drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h) override
    {
        if (! ghost()) { ghosty::GhostLnF::drawTooltip (g, text, w, h); return; }
        const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h);
        g.fillAll (juce::Colour (0xfff4f1fc)); ghosty::glass::card (g, r, 6.0f);
        juce::AttributedString s; s.setJustification (juce::Justification::centred);
        s.append (text, font (12.5f), ghosty::glass::ink());
        juce::TextLayout tl; tl.createLayoutWithBalancedLineLengths (s, 400.0f); tl.draw (g, r);
    }
    juce::Font getLabelFont (juce::Label& l) override { return font (juce::jmin (11.0f, (float) l.getHeight() * 0.8f)); }
    juce::Slider::SliderLayout getSliderLayout (juce::Slider& s) override
    {
        auto lay = LookAndFeel_V4::getSliderLayout (s);
        return lay;
    }
    juce::Label* createSliderTextBox (juce::Slider& s) override
    {
        auto* l = LookAndFeel_V4::createSliderTextBox (s);
        l->setFont (font (10.5f, false));
        l->setColour (juce::Label::textColourId, col::white.withAlpha (0.8f));
        l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
        l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        return l;
    }
};

// ---------------------------------------------------------------- the GhostBlock slider convention (every knob / bar in the rack)
//   left-drag: rotary knobs drag relative, linear BARS jump to the click position and scrub; SHIFT = fine; wheel = one step (1 %);
//   double-click = default; hover = the value in real units; right-click = a menu (Type value..., Reset, and in Ghost Box: macros).
//   The slider itself never reacts to a right-click (the panel / editor shows the menu), so a right-click never moves a value.
struct FineSlider : public juce::Slider
{
    using juce::Slider::Slider;
    juce::RangedAudioParameter* param = nullptr;   // when attached: programmatic sets (type-in, wheel, reset) are one host gesture
    void setAsGesture (double v)
    {
        v = juce::jlimit (getMinimum(), getMaximum(), v);
        if (param != nullptr) { param->beginChangeGesture(); param->setValueNotifyingHost (param->convertTo0to1 ((float) v)); param->endChangeGesture(); }
        else setValue (v, juce::sendNotificationSync);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) return;
        fine = e.mods.isShiftDown();
        if (fine)
        {
            if (isRotary()) { savedSens = getMouseDragSensitivity(); setMouseDragSensitivity (savedSens * 6); }
            else { setSliderSnapsToMousePosition (false); setVelocityBasedMode (true); setVelocityModeParameters (0.12, 1, 0.0, false); }
        }
        juce::Slider::mouseDown (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) juce::Slider::mouseDrag (e); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) return;
        juce::Slider::mouseUp (e);
        if (fine)
        {
            if (isRotary()) setMouseDragSensitivity (savedSens);
            else { setVelocityBasedMode (false); setSliderSnapsToMousePosition (true); }
            fine = false;
        }
    }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {   // one step per wheel notch: 1 % of the knob's travel (or the parameter's own step when that is coarser)
        if (! isEnabled() || ! isVisible()) return;
        const float d = std::abs (w.deltaY) >= std::abs (w.deltaX) ? w.deltaY : -w.deltaX;
        if (d == 0.0f) return;
        const double range = getMaximum() - getMinimum();
        const double p = valueToProportionOfLength (getValue()) + (w.isReversed ? -1.0 : 1.0) * (d > 0 ? 0.01 : -0.01);
        double v = proportionOfLengthToValue (juce::jlimit (0.0, 1.0, p));
        if (getInterval() > 0 && std::abs (v - getValue()) < getInterval()) v = getValue() + (d > 0 ? getInterval() : -getInterval());
        juce::ignoreUnused (range);
        setAsGesture (v);
    }
    // hover: the value in real units, in a bubble inside the plugin window (never a separate desktop window over the host)
    void parentHierarchyChanged() override { juce::Slider::parentHierarchyChanged(); auto* top = getTopLevelComponent(); setPopupDisplayEnabled (false, top != this, top != this ? top : nullptr); }
    void resetToDefault() { if (isDoubleClickReturnEnabled()) setAsGesture (getDoubleClickReturnValue()); }
    void mouseDoubleClick (const juce::MouseEvent& e) override { if (isDoubleClickReturnEnabled() && ! e.mods.isPopupMenu()) resetToDefault(); }
private:
    bool fine = false; double savedSens = 250;
};

// right-click "Type value...": a small box at the slider; Enter applies (one undo step), Esc cancels (Shared/TypeIn.h pattern)
inline void typeValue (juce::Slider& sl)
{
    struct Box : juce::Component
    {
        juce::TextEditor ed; juce::Component::SafePointer<juce::Slider> s;
        explicit Box (juce::Slider& x) : s (&x)
        {
            ed.setText (x.getTextFromValue (x.getValue()), false); ed.setJustification (juce::Justification::centred);
            ed.setFont (juce::FontOptions (16.0f, juce::Font::bold)); ed.setSelectAllWhenFocused (true);
            ed.onReturnKey = [this] { apply(); }; ed.onEscapeKey = [this] { close(); };
            addAndMakeVisible (ed); setSize (120, 34);
        }
        void resized() override { ed.setBounds (getLocalBounds()); }
        void visibilityChanged() override { if (isShowing()) ed.grabKeyboardFocus(); }
        void apply()
        {
            if (s != nullptr)
            {
                const auto t = ed.getText().trim();
                if (t.isNotEmpty())
                {
                    const double v = juce::jlimit (s->getMinimum(), s->getMaximum(), s->getValueFromText (t));
                    if (auto* fs = dynamic_cast<FineSlider*> (s.getComponent())) fs->setAsGesture (v); else s->setValue (v, juce::sendNotificationSync);
                }
            }
            close();
        }
        void close() { if (auto* cb = findParentComponentOfClass<juce::CallOutBox>()) cb->dismiss(); }
    };
    auto* top = sl.getTopLevelComponent();
    juce::CallOutBox::launchAsynchronously (std::make_unique<Box> (sl), top != nullptr ? top->getLocalArea (&sl, sl.getLocalBounds()) : sl.getScreenBounds(), top);
}
// the slider under a component (the component itself or one of its parents, up to 'stop')
inline juce::Slider* sliderAt (juce::Component* c, juce::Component* stop)
{
    for (; c != nullptr && c != stop; c = c->getParentComponent()) if (auto* s = dynamic_cast<juce::Slider*> (c)) return s;
    return nullptr;
}
// the common part of every slider right-click menu: "Type value..." first, "Reset to default" last (ids 9001 / 9002)
inline void addSliderItems (juce::PopupMenu& m, bool first) { if (first) m.addItem (9001, "Type value..."); else { m.addSeparator(); m.addItem (9002, "Reset to default"); } }
inline bool handleSliderItem (int r, juce::Slider* s)
{
    auto* fs = dynamic_cast<FineSlider*> (s);
    if (s == nullptr) return false;
    if (r == 9001) { typeValue (*s); return true; }
    if (r == 9002) { if (fs) fs->resetToDefault(); else if (s->isDoubleClickReturnEnabled()) s->setValue (s->getDoubleClickReturnValue(), juce::sendNotificationSync); return true; }
    return false;
}

// ---------------------------------------------------------------- knob (caption on top, value below)
struct Knob : public juce::Component
{
    FineSlider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::String caption; fxtheme::Held accent;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;

    Knob (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& cap, juce::Colour acc)
        : caption (cap), accent (acc)
    {
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 15);
        slider.setColour (juce::Slider::rotarySliderFillColourId, acc);
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        addAndMakeVisible (slider);
        if (s.getParameter (id) != nullptr) att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, id, slider);
        slider.getProperties().set ("gpfxParam", id);   // right-click "assign to macro" (Ghost Box) finds the parameter here
        if (auto* p = s.getParameter (id)) { slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue())); slider.param = p; }
    }
    Knob (const juce::String& cap, juce::Colour acc) : caption (cap), accent (acc)   // unattached (for non-parameter state)
    {
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 15);
        slider.setColour (juce::Slider::rotarySliderFillColourId, acc);
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        addAndMakeVisible (slider);
    }
    void setAccent (juce::Colour c) { accent = c; slider.setColour (juce::Slider::rotarySliderFillColourId, c); repaint(); }
    void paint (juce::Graphics& g) override
    {
        g.setColour (col::dim.brighter (0.25f)); g.setFont (font (10.0f));
        if (slider.isRotary()) g.drawText (caption, getLocalBounds().removeFromTop (13), juce::Justification::centred, false);
        else g.drawText (caption, getLocalBounds().removeFromLeft (40), juce::Justification::centredLeft, false);
    }
    void resized() override { auto r = getLocalBounds(); if (slider.isRotary()) r.removeFromTop (13); else r.removeFromLeft (40); slider.setBounds (r); }
    bool captionLeft() const { return ! slider.isRotary(); }
};

// ---------------------------------------------------------------- toggle pill bound to a bool parameter
struct Toggle : public juce::TextButton
{
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att;
    Toggle (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& label, juce::Colour acc)
    {
        setButtonText (label); setClickingTogglesState (true);
        setColour (juce::TextButton::buttonOnColourId, acc);
        if (s.getParameter (id) != nullptr) att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (s, id, *this);
        getProperties().set ("gpfxParam", id);
    }
    Toggle (const juce::String& label, juce::Colour acc)
    {
        setButtonText (label); setClickingTogglesState (true); setColour (juce::TextButton::buttonOnColourId, acc);
    }
};

// ---------------------------------------------------------------- combo bound to a choice parameter
struct Choice : public juce::ComboBox
{
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> att;
    Choice (juce::AudioProcessorValueTreeState& s, const juce::String& id, juce::Colour acc)
    {
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (s.getParameter (id)))
            addItemList (p->choices, 1);
        setColour (juce::ComboBox::outlineColourId, acc); setColour (juce::ComboBox::arrowColourId, acc);
        if (s.getParameter (id) != nullptr) att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (s, id, *this);
        getProperties().set ("gpfxParam", id);
    }
};

// ---------------------------------------------------------------- row of glowing tiles for a choice parameter
struct Segmented : public juce::Component
{
    juce::StringArray items; fxtheme::Held accent; int value = 0; int columns = 0;
    std::function<void (int)> onChange;
    std::unique_ptr<juce::ParameterAttachment> att;
    std::vector<int> subset;   // when set, only these choice indices are shown (in this order)

    Segmented (juce::AudioProcessorValueTreeState& s, const juce::String& id, juce::Colour acc, std::vector<int> only = {})
        : accent (acc), subset (std::move (only))
    {
        if (auto* p = s.getParameter (id))
        {
            if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p)) items = c->choices;
            att = std::make_unique<juce::ParameterAttachment> (*p, [this] (float v) { value = (int) std::lround (v); repaint(); if (onChange) onChange (value); });
            att->sendInitialUpdate();
            getProperties().set ("gpfxParam", id);
        }
    }
    int count() const { return subset.empty() ? items.size() : (int) subset.size(); }
    int indexAt (int k) const { return subset.empty() ? k : subset[(size_t) k]; }
    juce::Rectangle<float> cell (int k) const
    {
        const int n = juce::jmax (1, count()); const int cols = columns > 0 ? columns : n; const int rows = (n + cols - 1) / cols;
        const float w = (float) getWidth() / (float) cols, h = (float) getHeight() / (float) rows;
        return { (float) (k % cols) * w, (float) (k / cols) * h, w, h };
    }
    void paint (juce::Graphics& g) override
    {
        for (int k = 0; k < count(); ++k)
        {
            auto r = cell (k).reduced (2.5f);
            const bool on = indexAt (k) == value;
            if (ghost())
            {   // GHOST: glass pills, the chosen one violet glass
                ghostglass::pill (g, cell (k).reduced (1.0f), on, false, false, isEnabled());
                g.setColour (on ? juce::Colours::white : ghosty::glass::ink());
                g.setFont (font (juce::jmin (12.5f, r.getHeight() * 0.42f)));
                g.drawText (items[indexAt (k)], ghosty::glass::pillBody (cell (k).reduced (1.0f)), juce::Justification::centred, true);
                continue;
            }
            if (on)
            {
                g.setColour (accent.withAlpha (0.16f)); g.fillRoundedRectangle (r.expanded (2), 8);
                juce::ColourGradient gr (accent.withAlpha (0.55f), 0, r.getY(), accent.darker (0.7f).withAlpha (0.6f), 0, r.getBottom(), false);
                g.setGradientFill (gr);
            }
            else g.setColour (T (juce::Colour (0xff141722)));
            g.fillRoundedRectangle (r, 7);
            g.setColour (accent.withAlpha (on ? 0.9f : 0.18f)); g.drawRoundedRectangle (r, 7, on ? 1.4f : 1.0f);
            g.setColour (on ? col::white : col::white.withAlpha (0.55f));
            g.setFont (font (juce::jmin (12.5f, r.getHeight() * 0.42f)));
            g.drawText (items[indexAt (k)], r, juce::Justification::centred, true);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int k = 0; k < count(); ++k)
            if (cell (k).contains (e.position))
            {
                if (att) att->setValueAsCompleteGesture ((float) indexAt (k));
                else { value = indexAt (k); repaint(); if (onChange) onChange (value); }
            }
    }
};

// ---------------------------------------------------------------- page base: the selected unit's big editor
struct Page : public juce::Component
{
    virtual void tick (double /*timeSeconds*/) {}
};

// Lay a list of components out as an evenly spaced grid.
inline void gridLayout (juce::Rectangle<int> r, std::vector<juce::Component*> items, int cols, int cellH)
{
    if (items.empty()) return;
    const int rows = ((int) items.size() + cols - 1) / cols;
    const int cw = r.getWidth() / cols;
    const int gap = rows > 1 ? juce::jmax (0, (r.getHeight() - rows * cellH) / (rows - 1)) : 0;
    for (size_t k = 0; k < items.size(); ++k)
    {
        const int c = (int) k % cols, rr = (int) k / cols;
        if (items[k]) items[k]->setBounds (r.getX() + c * cw, r.getY() + rr * (cellH + juce::jmin (gap, 18)), cw, cellH);
    }
}

} // namespace ui
} // namespace gpfx
