// Ghost Lab - AGPL-3.0-only from build 12 (see LICENSE, NOTICE.md). The rooms' pages (RoomPage.h).
#include "RoomPage.h"
#include "Editor.h"
#include "SpectrePage.h"
#include "jcdp_envelope.h"
#include "envelope_component.h"   // PaulXStretch's own free-filter curve editor (third_party/paulxstretch, unmodified but for marked colour shims)

namespace glab
{
using namespace theme;
using rooms::Room;

juce::Colour RoomPage::colourOf (Room r)
{
    switch (r)
    {
        case Room::Grow: return mint;
        case Room::Stretch: return purple;
        case Room::Reborn: return pink;
        case Room::Space: return cyan;
        case Room::Spectre: return SpectrePage::accent();
        default: return green;
    }
}

static juce::String num (float v) { return std::abs (v) < 10.0f ? juce::String (v, 1) : juce::String (juce::roundToInt (v)); }

static juce::String noteName (int midi)
{
    static const char* n[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (n[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1);
}

// ================================================================================================ the strip of rooms
Room RouteBar::roomAt (int i)
{
    static const Room order[] = { Room::Spectre, Room::Lab, Room::Stretch, Room::Reborn, Room::Space };
    return order[juce::jlimit (0, kChips - 1, i)];
}

juce::Rectangle<float> RouteBar::chip (int i) const
{
    const bool tight = getWidth() < 700;   // (the phone-width page: just the chips)
    const float h = (float) getHeight() - 4.0f, lead = tight ? 0.0f : 150.0f, tail = tight ? 0.0f : 70.0f, gap = tight ? 5.0f : 30.0f;
    const float w = juce::jmin (150.0f, ((float) getWidth() - lead - tail - gap * (float) (kChips - 1)) / (float) kChips);
    return { lead + (float) i * (w + gap), 2.0f, w, h };
}

void RouteBar::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const bool tight = getWidth() < 700;
    g.setColour (sub.brighter (0.2f)); g.setFont (font (11.0f, 1, 0.14f));
    if (! tight) g.drawText ("YOUR SOUND GOES", r.withWidth (146.0f), juce::Justification::centredLeft);
    for (int i = 0; i < kChips; ++i)
    {
        const auto room = roomAt (i);
        const auto c = RoomPage::colourOf (room);
        const auto b = chip (i);
        const bool on = room == current, hot = hover == i;
        const float rad = b.getHeight() * 0.5f;
        g.setColour (night0.withAlpha (0.85f)); g.fillRoundedRectangle (b, rad);
        if (on) { juce::Path p; p.addRoundedRectangle (b, rad); art::glow (g, p, c.withAlpha (0.5f), 10); g.setColour (c.withAlpha (0.26f)); g.fillRoundedRectangle (b, rad); }
        else if (hot) { g.setColour (c.withAlpha (0.10f)); g.fillRoundedRectangle (b, rad); }
        g.setColour (c.withAlpha (on ? 1.0f : (hot ? 0.9f : 0.5f))); g.drawRoundedRectangle (b, rad, on ? 1.6f : 1.1f);
        g.setColour (on || hot ? text : text.withAlpha (0.8f)); g.setFont (font (tight ? 10.5f : 12.5f, on ? 2 : 1, tight ? 0.02f : 0.1f));
        g.drawFittedText (rooms::info (room).name, b.toNearestInt(), juce::Justification::centred, 1, 0.7f);
        if (tight) continue;
        // the arrow to the next one
        const float ax = b.getRight() + 8.0f, ay = b.getCentreY();
        g.setColour (text.withAlpha (0.4f));
        g.drawLine (ax, ay, ax + 12.0f, ay, 1.3f);
        juce::Path head; head.addTriangle (ax + 15.0f, ay, ax + 9.0f, ay - 4.0f, ax + 9.0f, ay + 4.0f); g.fillPath (head);
    }
    if (tight) return;
    g.setColour (green.withAlpha (0.9f)); g.setFont (font (11.0f, 2, 0.14f));
    g.drawText ("INTO FL", juce::Rectangle<float> (chip (kChips - 1).getRight() + 30.0f, 0.0f, 70.0f, r.getHeight()), juce::Justification::centredLeft);
}

void RouteBar::mouseMove (const juce::MouseEvent& e)
{
    int h = -1;
    for (int i = 0; i < kChips; ++i) if (chip (i).contains (e.position)) h = i;
    if (h != hover) { hover = h; repaint(); }
}
void RouteBar::mouseExit (const juce::MouseEvent&) { hover = -1; repaint(); }
void RouteBar::mouseUp (const juce::MouseEvent& e)
{
    for (int i = 0; i < kChips; ++i) if (chip (i).contains (e.position) && onPick) { onPick (roomAt (i)); return; }
}

// ================================================================================================ the controls
static juce::String showValue (const rooms::Ctl& c, float v)
{
    if (c.kind == 'b') return v > 0.5f ? "ON" : "OFF";
    if (c.kind == 'c') { const int i = juce::jlimit (0, juce::jmax (0, c.choices.size() - 1), juce::roundToInt (v)); return c.choices.isEmpty() ? juce::String (i) : c.choices[i]; }
    const auto unit = c.unit.isNotEmpty() ? " " + c.unit : juce::String();
    if (c.key == "pxs.stretchamount0") return num (v) + "x";
    if (c.kind == 'i') return juce::String (juce::roundToInt (v)) + unit;
    const float span = c.hi - c.lo;
    return (span > 500.0f ? juce::String (juce::roundToInt (v)) : juce::String (v, span > 20.0f ? 1 : 2)) + unit;
}

// a control drawn as a bar (PaulXStretch's sliders) or a switch (its tick boxes)
struct RoomPage::Bar : public juce::Component
{
    rooms::Ctl c;
    float v = 0;
    juce::Colour colour { purple };
    std::function<void (float)> onChange;
    bool hot = false;

    bool stepped() const { return c.kind == 'i' || c.kind == 'c' || c.kind == 'b'; }
    bool logScale() const { return c.lo > 0.0f && c.hi / c.lo >= 50.0f; }
    float toT (float x) const { return logScale() ? std::log (x / c.lo) / std::log (c.hi / c.lo) : (c.hi > c.lo ? (x - c.lo) / (c.hi - c.lo) : 0.0f); }
    float fromT (float t) const { return logScale() ? c.lo * std::pow (c.hi / c.lo, t) : c.lo + (c.hi - c.lo) * t; }
    void set (float nv, bool tell)
    {
        nv = juce::jlimit (c.lo, c.hi, nv);
        if (stepped()) nv = std::round (nv);
        if (std::abs (nv - v) < 1e-7f) return;
        v = nv; repaint();
        if (tell && onChange) onChange (v);
    }
    void paint (juce::Graphics& g) override
    {
        const bool on = isEnabled();
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (night0.withAlpha (0.8f)); g.fillRoundedRectangle (r, 6.0f);
        if (on && c.kind != 'b')
        {
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path clip; clip.addRoundedRectangle (r, 6.0f); g.reduceClipRegion (clip);
            const auto fill = r.withWidth (juce::jmax (3.0f, r.getWidth() * juce::jlimit (0.0f, 1.0f, toT (v))));
            g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.14f), fill.getX(), 0, colour.withAlpha (hot ? 0.6f : 0.45f), fill.getRight(), 0, false));
            g.fillRect (fill);
            g.setColour (colour.brighter (0.4f)); g.fillRect (fill.getRight() - 2.0f, r.getY(), 2.0f, r.getHeight());
        }
        if (on && c.kind == 'b' && v > 0.5f) { g.setColour (colour.withAlpha (0.3f)); g.fillRoundedRectangle (r, 6.0f); }
        g.setColour ((on ? colour : dim).withAlpha (on ? (hot ? 0.9f : 0.5f) : 0.3f)); g.drawRoundedRectangle (r, 6.0f, 1.0f);
        const float pad = 10.0f, vw = juce::jmin (120.0f, r.getWidth() * 0.4f);
        const bool tall = r.getHeight() >= 34.0f && c.plain.isNotEmpty();
        g.setColour (on ? text : dim); g.setFont (font (12.5f, 1, 0.02f));
        g.drawFittedText (c.name, juce::Rectangle<float> (r.getX() + pad, r.getY() + (tall ? 3.0f : 0.0f), r.getWidth() - vw - pad * 2, tall ? 17.0f : r.getHeight()).toNearestInt(),
                          juce::Justification::centredLeft, 1, 0.8f);
        if (tall)
        {
            g.setColour (on ? sub.brighter (0.35f) : dim.withAlpha (0.8f)); g.setFont (font (10.0f));
            g.drawFittedText (c.plain, juce::Rectangle<float> (r.getX() + pad, r.getY() + 19.0f, r.getWidth() - vw - pad, r.getHeight() - 21.0f).toNearestInt(),
                              juce::Justification::topLeft, 1, 0.75f);
        }
        g.setColour (on ? text : dim); g.setFont (font (13.0f, 1));
        g.drawFittedText (showValue (c, v), juce::Rectangle<float> (r.getRight() - vw - pad, r.getY(), vw, r.getHeight()).toNearestInt(), juce::Justification::centredRight, 1, 0.8f);
    }
    void mouseEnter (const juce::MouseEvent&) override { hot = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hot = false; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override { if (c.kind == 'b') { if (isEnabled()) set (v > 0.5f ? 0.0f : 1.0f, true); } else drag (e); }
    void mouseDrag (const juce::MouseEvent& e) override { if (c.kind != 'b') drag (e); }
    void mouseDoubleClick (const juce::MouseEvent&) override { if (c.kind != 'b') set (c.def, true); }
    void drag (const juce::MouseEvent& e)
    {
        if (! isEnabled()) return;
        set (fromT (juce::jlimit (0.0f, 1.0f, (e.position.x - 4.0f) / juce::jmax (1.0f, (float) getWidth() - 8.0f))), true);
    }
};

// a control drawn as a knob with its name under it (DDSP-VST's and Pertylizer's)
struct RoomPage::Knob : public juce::Component
{
    rooms::Ctl c;
    float v = 0, dragFrom = 0;
    juce::Colour colour { pink };
    std::function<void (float)> onChange;
    bool hot = false;
    void set (float nv, bool tell)
    {
        nv = juce::jlimit (c.lo, c.hi, nv);
        if (c.kind == 'i') nv = std::round (nv);
        if (std::abs (nv - v) < 1e-7f) return;
        v = nv; repaint();
        if (tell && onChange) onChange (v);
    }
    void paint (juce::Graphics& g) override
    {
        const bool on = isEnabled();
        auto r = getLocalBounds().toFloat();
        const bool hint = r.getHeight() >= 92.0f && c.plain.isNotEmpty();
        auto hintR = hint ? r.removeFromBottom (22.0f) : juce::Rectangle<float>();
        auto nameR = r.removeFromBottom (15.0f);
        auto valR = r.removeFromBottom (14.0f);
        const float d = juce::jmin (r.getWidth() - 8.0f, r.getHeight() - 2.0f);
        const auto k = juce::Rectangle<float> (d, d).withCentre (r.getCentre());
        const float a0 = juce::MathConstants<float>::pi * 1.25f, a1 = juce::MathConstants<float>::pi * 2.75f;
        const float t = c.hi > c.lo ? (v - c.lo) / (c.hi - c.lo) : 0.0f, at = a0 + (a1 - a0) * t;
        g.setColour (night0.withAlpha (0.9f)); g.fillEllipse (k.reduced (3.0f));
        juce::Path back; back.addCentredArc (k.getCentreX(), k.getCentreY(), d * 0.5f - 3.0f, d * 0.5f - 3.0f, 0.0f, a0, a1, true);
        g.setColour (border.brighter (0.2f)); g.strokePath (back, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (on)
        {
            // (a control that runs both ways from the middle, like Pitch Shift or X, fills from the middle)
            const bool both = c.lo < 0.0f && c.hi > 0.0f;
            const float from = both ? a0 + (a1 - a0) * (-c.lo / (c.hi - c.lo)) : a0;
            juce::Path arc; arc.addCentredArc (k.getCentreX(), k.getCentreY(), d * 0.5f - 3.0f, d * 0.5f - 3.0f, 0.0f, juce::jmin (from, at), juce::jmax (from, at), true);
            if (hot) art::glow (g, arc, colour.withAlpha (0.6f), 6);
            g.setColour (colour); g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        const juce::Point<float> tip (k.getCentreX() + std::sin (at) * (d * 0.5f - 9.0f), k.getCentreY() - std::cos (at) * (d * 0.5f - 9.0f));
        g.setColour (on ? text : dim); g.fillEllipse (tip.x - 2.5f, tip.y - 2.5f, 5.0f, 5.0f);
        g.setFont (font (11.0f, 1)); g.drawFittedText (on ? showValue (c, v) : juce::String ("-"), valR.toNearestInt(), juce::Justification::centred, 1, 0.75f);
        g.setColour (on ? text.withAlpha (0.95f) : dim); g.setFont (font (11.0f, 2, 0.02f));
        g.drawFittedText (c.name, nameR.toNearestInt(), juce::Justification::centred, 1, 0.7f);
        if (hint) { g.setColour (on ? sub.brighter (0.35f) : dim.withAlpha (0.8f)); g.setFont (font (9.5f)); g.drawFittedText (c.plain, hintR.toNearestInt(), juce::Justification::centredTop, 2, 0.75f); }
    }
    void mouseEnter (const juce::MouseEvent&) override { hot = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hot = false; repaint(); }
    void mouseDown (const juce::MouseEvent&) override { dragFrom = v; }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! isEnabled()) return;
        set (dragFrom + (c.hi - c.lo) * (float) (e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY()) / 160.0f, true);
    }
    void mouseDoubleClick (const juce::MouseEvent&) override { if (isEnabled()) set (c.def, true); }
};

// ================================================================================================ the big button
class RoomPage::RunButton : public juce::Component
{
public:
    juce::String label;
    juce::Colour colour { green };
    bool solid = true, hot = false, lit = false;
    std::function<void()> onClick;
    void paint (juce::Graphics& g) override
    {
        const bool on = isEnabled();
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        juce::Path p; p.addRoundedRectangle (r, 9.0f);
        if (solid)
        {
            const auto c = colour.withAlpha (on ? 1.0f : 0.3f);
            if (on) art::glow (g, p, colour.withAlpha (hot ? 0.8f : 0.5f), 12);
            g.setGradientFill (juce::ColourGradient (c.brighter (hot && on ? 0.3f : 0.15f), 0, r.getY(), c.darker (0.15f), 0, r.getBottom(), false));
            g.fillPath (p);
            g.setColour (juce::Colour (0xff0e1408).withAlpha (on ? 1.0f : 0.6f));
        }
        else
        {
            g.setColour (night0.withAlpha (0.8f)); g.fillPath (p);
            if (lit) { art::glow (g, p, colour.withAlpha (0.5f), 10); g.setColour (colour.withAlpha (0.25f)); g.fillPath (p); }
            else if (hot && on) { g.setColour (colour.withAlpha (0.12f)); g.fillPath (p); }
            g.setColour (colour.withAlpha (on ? (lit ? 1.0f : 0.7f) : 0.25f)); g.strokePath (p, juce::PathStrokeType (1.3f));
            g.setColour (on ? text : dim);
        }
        g.setFont (font (juce::jmin (14.0f, r.getHeight() * 0.4f), 2, 0.1f));
        g.drawFittedText (label, r.reduced (6, 2).toNearestInt(), juce::Justification::centred, 1, 0.8f);
    }
    void mouseEnter (const juce::MouseEvent&) override { hot = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hot = false; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override { if (isEnabled() && getLocalBounds().contains (e.getPosition()) && onClick) onClick(); }
};

// ================================================================================================ STRETCH: the parameter groups
// PaulXStretch's own groups, in its order, stacked (the pairs it puts side by side are side by side). A spectral module's
// group has that module's on / off switch in its title bar; its controls are greyed while the module is off.
class RoomPage::Groups : public juce::Component
{
public:
    struct Group { juce::String title; int module = -1; bool half = false, binaural = false, curve = false; juce::Array<Bar*> bars; juce::Rectangle<int> box, head; };
    explicit Groups (RoomPage& p) : page (p)
    {
        const auto col = RoomPage::colourOf (Room::Stretch);
        // (title, the table's group name, half width)
        struct Def { const char* title; const char* group; bool half; };
        static const Def defs[] = { { "Main", "Main", false }, { "Harmonics", "Harmonics", false }, { "Tonal vs Noise", "Tonal vs noise", false },
                                    { "Frequency shift", "Frequency shift", true }, { "Pitch shift", "Pitch shift", true },
                                    { "Frequency spread", "Spread", true }, { "Compress", "Compressor", true },
                                    { "Ratios (octave mixer)", "Octave mixer (ratios)", false }, { "Filter", "Filter", false }, { "Free filter", "Free filter", false },
                                    { "Binaural beats", "Binaural beats", false } };
        for (auto& d : defs)
        {
            Group gr; gr.title = d.title; gr.half = d.half; gr.binaural = juce::String (d.group) == "Binaural beats";
            gr.curve = juce::String (d.group) == "Free filter";
            for (auto& c : rooms::controls (Room::Stretch))
            {
                if (c.group != d.group) continue;
                gr.module = c.module;
                auto* b = bars.add (new Bar());
                b->c = c; b->colour = col; b->v = page.value (c.key);
                b->onChange = [this, key = c.key] (float v) { page.setValue (key, v); };
                addAndMakeVisible (b);
                gr.bars.add (b);
            }
            groups.push_back (gr);
        }
        // the plug-in's own free-filter envelope and its own editor, set up as its PluginProcessor / PluginEditor do
        ffEnv = std::make_shared<breakpoint_envelope>();
        ffEnv->SetName ("Free filter");
        ffEnv->AddNode ({ 0.0, 0.75 }); ffEnv->AddNode ({ 1.0, 0.75 });
        ffEnv->set_reset_nodes (ffEnv->get_all_nodes());
        if (page.step().data.isNotEmpty()) if (auto xml = juce::parseXML (page.step().data)) ffEnv->restoreState (juce::ValueTree::fromXml (*xml));
        env.set_envelope (ffEnv, "Free filter");
        env.XFromNormalized = [] (double x) { return 30.0 * std::pow (1.05946309436, x * 115.0); };     // Hz along the bottom, as the plug-in
        env.YFromNormalized = [] (double y) { return juce::jmap<double> (y, 0.0, 1.0, -48.0, 12.0); };  // dB, as the plug-in
        env.EnvelopeUnderlayDraw = [] (EnvelopeComponent*, juce::Graphics& g) { g.fillAll (juce::Colour (0xff07060f)); };
        env.envelopeColour = col; env.transformedColour = green; env.nodeColour = text; env.textColour = sub.brighter (0.5f);
        addAndMakeVisible (env);
        pushed = tree();
    }
    // the curve as the plug-in saves it, or an invalid tree while it is still the plug-in's flat default
    juce::ValueTree tree() const
    {
        const auto n = ffEnv->get_all_nodes();
        const bool flat = n.size() == 2 && n[0].pt_x == 0.0 && n[1].pt_x == 1.0 && n[0].pt_y == 0.75 && n[1].pt_y == 0.75 && n[0].ShapeParam1 == 0.5 && n[1].ShapeParam1 == 0.5
                       && n[0].ShapeParam2 == 0.5 && n[1].ShapeParam2 == 0.5 && ! ffEnv->m_transform_wrap_x && ! ffEnv->m_transform_y_random_linear_interpolation;
        return flat ? juce::ValueTree() : ffEnv->saveState (juce::Identifier ("freefilter_envelope"));
    }
    // the curve you drew goes into the step (so RENDER uses it and the session keeps it); the seven transform sliders show
    // on the editor's second curve, as in the plug-in
    void sync()
    {
        if (env.isMouseButtonDown()) return;
        ffEnv->m_transform_x_shift = page.value ("pxs.freefilter_shiftx_0"); ffEnv->m_transform_y_shift = page.value ("pxs.freefilter_shifty_0");
        ffEnv->m_transform_y_scale = page.value ("pxs.freefilter_scaley_0"); ffEnv->m_transform_y_tilt = page.value ("pxs.freefilter_tilty_0");
        ffEnv->m_transform_y_random_bands = juce::roundToInt (page.value ("pxs.freefilter_randomybands0"));
        ffEnv->m_transform_y_random_rate = juce::roundToInt (page.value ("pxs.freefilter_randomyrate0"));
        ffEnv->m_transform_y_random_amount = page.value ("pxs.freefilter_randomyamount0");
        const auto mine = tree();
        const bool same = mine.isValid() == pushed.isValid() && (! mine.isValid() || mine.isEquivalentTo (pushed));
        if (! same) { pushed = mine; page.step().data = mine.isValid() ? mine.toXmlString (juce::XmlElement::TextFormat().singleLine()) : juce::String(); }
        const auto sig = juce::String (ffEnv->m_transform_x_shift) + juce::String (ffEnv->m_transform_y_shift) + juce::String (ffEnv->m_transform_y_scale)
                         + juce::String (ffEnv->m_transform_y_tilt) + juce::String (ffEnv->m_transform_y_random_amount);
        if (sig != lastSig) { lastSig = sig; env.repaint(); }
    }
    int layout (int width)
    {
        int y = 0;
        const int gap = 6, headH = 22, barH = 38, pad = 6;
        for (size_t i = 0; i < groups.size(); ++i)
        {
            auto place = [&] (Group& gr, int x, int w)
            {
                const int cols = w >= 520 ? 2 : 1, rows = (gr.bars.size() + cols - 1) / cols;
                const int curveH = gr.curve ? 150 : 0;   // the plug-in's own curve editor sits above the Free filter sliders
                gr.box = { x, y, w, headH + pad + curveH + rows * (barH + 4) + 2 };
                gr.head = { x, y, w, headH };
                if (gr.curve) env.setBounds (x + pad, y + headH + pad, w - pad * 2, curveH - 6);
                const int bw = (w - pad * 2 - (cols - 1) * 6) / cols;
                for (int k = 0; k < gr.bars.size(); ++k)
                    gr.bars[k]->setBounds (x + pad + (k % cols) * (bw + 6), y + headH + pad + curveH + (k / cols) * (barH + 4), bw, barH);
                return gr.box.getHeight();
            };
            if (groups[i].half && i + 1 < groups.size() && groups[i + 1].half && width >= 520)
            {
                const int hw = (width - gap) / 2;
                const int h1 = place (groups[i], 0, hw), h2 = place (groups[i + 1], hw + gap, width - hw - gap);
                y += juce::jmax (h1, h2) + gap; ++i;
            }
            else y += place (groups[i], 0, width) + gap;
        }
        setSize (width, juce::jmax (1, y - gap));
        return getHeight();
    }
    void paint (juce::Graphics& g) override
    {
        const auto col = RoomPage::colourOf (Room::Stretch);
        for (auto& gr : groups)
        {
            const bool sw = gr.module >= 0, on = ! sw || page.moduleOn (gr.module);
            auto b = gr.box.toFloat().reduced (0.5f);
            g.setColour (night0.withAlpha (0.45f)); g.fillRoundedRectangle (b, 7.0f);
            g.setColour ((on ? col : dim).withAlpha (on ? 0.45f : 0.3f)); g.drawRoundedRectangle (b, 7.0f, 1.0f);
            auto h = gr.head.toFloat().reduced (8.0f, 0.0f);
            if (sw)
            {
                // the module's own switch
                auto dot = juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ h.getX() + 7.0f, h.getCentreY() + 2.0f });
                if (on) { juce::Path p; p.addEllipse (dot); art::glow (g, p, green.withAlpha (0.8f), 6); }
                g.setColour (on ? green : night0); g.fillEllipse (dot);
                g.setColour (on ? green : text.withAlpha (0.5f)); g.drawEllipse (dot, 1.3f);
                h.removeFromLeft (22.0f);
            }
            g.setColour (on ? text : dim.brighter (0.3f)); g.setFont (font (12.5f, 2, 0.06f));
            g.drawText (gr.title + (sw ? (on ? juce::String ("   ON") : juce::String ("   OFF: click to switch on")) : juce::String()), h.translated (0.0f, 2.0f), juce::Justification::centredLeft);
        }
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        for (auto& gr : groups) if (gr.module >= 0 && gr.head.contains (e.getPosition())) { page.setModule (gr.module, ! page.moduleOn (gr.module)); return; }
    }
    RoomPage& page;
    juce::OwnedArray<Bar> bars;
    std::vector<Group> groups;
    juce::CriticalSection envLock;
    EnvelopeComponent env { &envLock };
    std::shared_ptr<breakpoint_envelope> ffEnv;
    juce::ValueTree pushed;
    juce::String lastSig;
};

// PaulXStretch's processing-chain strip: its nine spectral modules in the order the sound goes through them. A lit one
// is on (click switches it); drag one onto another place to move it there (the plug-in's own chain order).
class RoomPage::Chain : public juce::Component
{
public:
    explicit Chain (RoomPage& p) : page (p) {}
    RoomPage& page;
    int grabbed = -1, over = -1;
    juce::Rectangle<float> box (int i) const
    {
        const float w = ((float) getWidth() - 8.0f * 2.0f) / 3.0f, h = ((float) getHeight() - 16.0f - 4.0f * 2.0f) / 3.0f;
        return { (float) (i % 3) * (w + 8.0f), 16.0f + (float) (i / 3) * (h + 4.0f), w, h };
    }
    int at (juce::Point<float> p) const { for (int i = 0; i < 9; ++i) if (box (i).contains (p)) return i; return -1; }
    void paint (juce::Graphics& g) override
    {
        const auto col = RoomPage::colourOf (Room::Stretch);
        g.setColour (sub.brighter (0.3f)); g.setFont (font (10.0f, 2, 0.06f));
        g.drawText ("PROCESSING CHAIN  (lit = on; drag one to change the order)", getLocalBounds().toFloat().withHeight (14.0f), juce::Justification::centredLeft);
        const auto order = page.chainOrder();
        for (int i = 0; i < 9; ++i)
        {
            const int m = order[i];
            const bool on = page.moduleOn (m), enabled = isEnabled();
            auto b = box (i).reduced (0.5f);
            g.setColour (on && enabled ? col.withAlpha (0.32f) : night0.withAlpha (0.8f)); g.fillRoundedRectangle (b, 5.0f);
            g.setColour ((enabled ? col : dim).withAlpha (i == over && grabbed >= 0 && grabbed != i ? 1.0f : (on ? 1.0f : 0.35f)));
            g.drawRoundedRectangle (b, 5.0f, i == over && grabbed >= 0 && grabbed != i ? 2.2f : (on ? 1.4f : 1.0f));
            g.setColour (! enabled ? dim : (on ? text : text.withAlpha (0.6f))); g.setFont (font (10.5f, on ? 2 : 1));
            g.drawFittedText (juce::String (i + 1) + "  " + rooms::stretchModules()[m], b.reduced (4, 0).toNearestInt(), juce::Justification::centred, 1, 0.7f);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override { grabbed = isEnabled() ? at (e.position) : -1; over = grabbed; }
    void mouseDrag (const juce::MouseEvent& e) override { const int o = at (e.position); if (o != over) { over = o; repaint(); } }
    void mouseUp (const juce::MouseEvent& e) override
    {
        const int from = grabbed, to = at (e.position);
        grabbed = over = -1;
        if (from < 0 || to < 0) { repaint(); return; }
        if (from == to) { const int m = page.chainOrder()[from]; page.setModule (m, ! page.moduleOn (m)); }
        else page.moveInChain (from, to);
        repaint();
    }
};

// ================================================================================================ REBORN: the Pitch / Volume box
// DDSP-VST's own picture: pitch across, volume up. The lit rectangle is where the instrument model is at home; the dots
// are your sound's notes (after Pitch Shift). Dragging the rectangle moves the model's range (its Input Pitch / Input Gain).
class RoomPage::RangeBox : public juce::Component
{
public:
    explicit RangeBox (RoomPage& p) : page (p) {}
    RoomPage& page;
    juce::Point<float> grab; float pitch0 = 0, gain0 = 0; bool dragging = false;
    juce::Rectangle<float> area() const { return getLocalBounds().toFloat().withTrimmedLeft (22.0f).withTrimmedBottom (18.0f).reduced (1.0f); }
    juce::Rectangle<float> range() const
    {
        const auto a = area();
        const auto names = rooms::rebornInstruments();
        const int inst = page.instrument();
        const auto nr = inst >= 0 && inst < names.size() ? rooms::instrumentRange (names[inst]) : juce::Range<float> (48.0f, 84.0f);
        const float ip = page.value ("ddsp.InputPitch"), ig = page.value ("ddsp.InputGain");
        const float x0 = a.getX() + a.getWidth() * (nr.getStart() / 127.0f + ip), x1 = a.getX() + a.getWidth() * (nr.getEnd() / 127.0f + ip);
        const float y0 = a.getBottom() - a.getHeight() * (0.85f + ig), y1 = a.getBottom() - a.getHeight() * (0.25f + ig);
        return { x0, y0, x1 - x0, y1 - y0 };
    }
    void paint (juce::Graphics& g) override
    {
        const auto col = RoomPage::colourOf (Room::Reborn);
        const auto a = area();
        const bool has = page.instrument() >= 0 && page.instrument() < rooms::rebornInstruments().size();
        g.setColour (juce::Colour (0xff07060f)); g.fillRoundedRectangle (a, 8.0f);
        g.setColour (col.withAlpha (0.5f)); g.drawRoundedRectangle (a, 8.0f, 1.0f);
        g.setColour (sub.brighter (0.3f)); g.setFont (font (10.5f, 1, 0.08f));
        g.drawText ("Pitch  (low notes left, high notes right)", juce::Rectangle<float> (a.getX(), a.getBottom() + 3.0f, a.getWidth(), 14.0f), juce::Justification::centred);
        {
            juce::Graphics::ScopedSaveState ss (g);
            g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, 10.0f, a.getCentreY()));
            g.drawText ("Volume", juce::Rectangle<float> (10.0f - 60.0f, a.getCentreY() - 7.0f, 120.0f, 14.0f), juce::Justification::centred);
        }
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip; clip.addRoundedRectangle (a.reduced (1.0f), 7.0f); g.reduceClipRegion (clip);
        g.setColour (text.withAlpha (0.06f));
        for (int oct = 1; oct < 10; ++oct) { const float x = a.getX() + a.getWidth() * (float) (oct * 12) / 127.0f; g.drawVerticalLine ((int) x, a.getY(), a.getBottom()); }
        if (has)
        {
            const auto rb = range();
            g.setColour (col.withAlpha (0.2f)); g.fillRoundedRectangle (rb, 5.0f);
            g.setColour (col); g.drawRoundedRectangle (rb, 5.0f, 1.6f);
            g.setFont (font (10.0f, 2, 0.1f));
            g.drawFittedText (rooms::rebornInstruments()[page.instrument()].toUpperCase() + " IS AT HOME HERE  (drag the box)", rb.reduced (6, 3).withHeight (13.0f).toNearestInt(),
                              juce::Justification::centredLeft, 1, 0.7f);
        }
        // your sound's notes: one dot per moment with a clear note (across = its note after Pitch Shift, up = how loud)
        const auto& t = page.tune;
        const float shift = page.value ("ddsp.PitchShift");
        for (size_t i = 0; i < t.note.size(); ++i)
        {
            if (t.note[i] <= 0.0f) continue;
            const float x = a.getX() + a.getWidth() * juce::jlimit (0.0f, 1.0f, (t.note[i] + shift) / 127.0f);
            const float y = a.getBottom() - a.getHeight() * juce::jlimit (0.03f, 0.97f, 0.15f + 0.75f * std::sqrt (t.loud[i]));
            g.setColour (juce::Colours::white.withAlpha (0.85f)); g.fillEllipse (x - 2.5f, y - 2.5f, 5.0f, 5.0f);
        }
        if (t.voiced < 0.02f)
        {
            g.setColour (sub.withAlpha (0.9f)); g.setFont (font (12.0f));
            g.drawFittedText (page.ed.engine.hasSpecimen() ? "No clear notes found in this sound." : "Load a sound: its notes show up here as dots.", a.reduced (12, 6).toNearestInt(), juce::Justification::centredBottom, 2, 0.85f);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = isEnabled() && range().expanded (6.0f).contains (e.position);
        grab = e.position; pitch0 = page.value ("ddsp.InputPitch"); gain0 = page.value ("ddsp.InputGain");
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging) return;
        const auto a = area();
        page.setValue ("ddsp.InputPitch", pitch0 + (e.position.x - grab.x) / a.getWidth());
        page.setValue ("ddsp.InputGain", gain0 - (e.position.y - grab.y) / a.getHeight());
    }
    void mouseUp (const juce::MouseEvent&) override { dragging = false; }
};

// ================================================================================================ SPACE: a rack module, and the view from above
// one Pertylizer rack module: its title bar, its IN and OUT ports, its knobs in rows with their names under them
class RoomPage::Module : public juce::Component
{
public:
    Module (RoomPage& p, const juce::String& t, const char* engine, juce::Colour col) : page (p), title (t), colour (col)
    {
        const auto prefix = juce::String (engine) + ".";
        for (auto& c : rooms::controls (Room::Space))
        {
            if (! c.key.startsWith (prefix)) continue;
            auto* k = knobs.add (new Knob());
            k->c = c; k->colour = col; k->v = page.value (c.key);
            k->onChange = [this, key = c.key] (float v) { page.setValue (key, v); };
            addAndMakeVisible (k);
        }
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colour (0xff12111c)); g.fillRoundedRectangle (r, 6.0f);
        g.setColour (colour.withAlpha (0.8f)); g.drawRoundedRectangle (r, 6.0f, 1.2f);
        auto bar = r.removeFromTop (24.0f);
        g.setColour (colour); g.fillRect (bar.getX() + 8.0f, bar.getY() + 6.0f, 2.5f, 13.0f);
        g.setFont (font (13.0f, 1, 0.04f)); g.drawText (title, bar.withTrimmedLeft (18.0f), juce::Justification::centredLeft);
        g.setColour (text.withAlpha (0.12f)); g.fillRect (r.getX() + 8.0f, bar.getBottom(), r.getWidth() - 16.0f, 1.0f);
        // the ports: IN on the left, OUT on the right
        g.setFont (font (8.5f, 1, 0.1f)); g.setColour (sub);
        g.drawText ("IN", juce::Rectangle<float> (r.getX() + 2.0f, bar.getBottom() + 3.0f, 24.0f, 10.0f), juce::Justification::centred);
        g.drawText ("OUT", juce::Rectangle<float> (r.getRight() - 26.0f, bar.getBottom() + 3.0f, 24.0f, 10.0f), juce::Justification::centred);
        for (int side = 0; side < 2; ++side)
        {
            const auto c = port (side == 1);
            g.setColour (juce::Colour (0xff12111c)); g.fillEllipse (c.x - 7.0f, c.y - 7.0f, 14.0f, 14.0f);
            g.setColour (cyan); g.drawEllipse (c.x - 7.0f, c.y - 7.0f, 14.0f, 14.0f, 1.8f); g.fillEllipse (c.x - 3.0f, c.y - 3.0f, 6.0f, 6.0f);
        }
    }
    juce::Point<float> port (bool out) const { return { out ? (float) getWidth() - 14.0f : 14.0f, 50.0f }; }
    void resized() override
    {
        auto r = getLocalBounds().reduced (30, 4).withTrimmedTop (26);
        const int n = knobs.size(), cols = juce::jmax (1, juce::jmin (n, (n + 1) / 2 > 4 ? 5 : 4)), rows = (n + cols - 1) / cols;
        const int kw = r.getWidth() / cols, kh = r.getHeight() / juce::jmax (1, rows);
        for (int i = 0; i < n; ++i) knobs[i]->setBounds (r.getX() + (i % cols) * kw + 2, r.getY() + (i / cols) * kh, kw - 4, kh - 2);
    }
    RoomPage& page;
    juce::String title;
    juce::Colour colour;
    juce::OwnedArray<Knob> knobs;
};

// the view from above (ours, next to the modules): you in the middle, the sound where the panner's X and Distance put it.
// Dragging the sound turns those two knobs.
class RoomPage::Pad : public juce::Component
{
public:
    explicit Pad (RoomPage& p) : page (p) {}
    RoomPage& page;
    float meterL = 0, meterR = 0;
    juce::Rectangle<float> area() const { return getLocalBounds().toFloat().reduced (1.0f); }
    juce::Point<float> head() const { const auto r = area(); return { r.getCentreX(), r.getBottom() - 30.0f }; }
    juce::Point<float> dot() const
    {
        const auto r = area(); const auto h = head();
        const float x = page.value ("panner.x"), d = page.value ("panner.distance");
        return { r.getCentreX() + (r.getWidth() * 0.5f - 18.0f) * x, h.y - 22.0f - (h.y - 22.0f - r.getY() - 30.0f) * d };
    }
    void paint (juce::Graphics& g) override
    {
        const auto c = RoomPage::colourOf (Room::Space);
        const auto r = area();
        g.setColour (juce::Colour (0xff07060f)); g.fillRoundedRectangle (r, 8.0f);
        g.setColour (c.withAlpha (0.6f)); g.drawRoundedRectangle (r, 8.0f, 1.0f);
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip; clip.addRoundedRectangle (r.reduced (1.0f), 7.0f); g.reduceClipRegion (clip);
        const auto h = head();
        const float er = page.value ("panner.er_level"), mix = page.value ("fdn.mix"), size = page.value ("fdn.room_size");
        for (int k = 4; k >= 1; --k)
        {
            const float rad = 30.0f + (float) k * (16.0f + 22.0f * size);
            g.setColour (c.withAlpha (0.05f + (0.06f * er + 0.08f * mix) * (1.0f - (float) k / 6.0f)));
            g.drawEllipse (h.x - rad * 1.4f, h.y - rad, rad * 2.8f, rad * 2.0f, 1.0f + mix * 1.5f);
        }
        g.setColour (text.withAlpha (0.9f)); g.drawEllipse (h.x - 12.0f, h.y - 12.0f, 24.0f, 24.0f, 1.5f);
        juce::Path nose; nose.addTriangle (h.x, h.y - 18.0f, h.x - 4.0f, h.y - 11.0f, h.x + 4.0f, h.y - 11.0f); g.fillPath (nose);
        for (int e = 0; e < 2; ++e)
        {
            const float lv = juce::jlimit (0.0f, 1.0f, (e == 0 ? meterL : meterR) * 5.0f);
            auto ear = juce::Rectangle<float> (7.0f, 12.0f).withCentre ({ h.x + (e == 0 ? -16.0f : 16.0f), h.y });
            if (lv > 0.02f) { juce::Path p; p.addEllipse (ear.expanded (2.0f + lv * 6.0f)); art::glow (g, p, c.withAlpha (lv), 8); }
            g.setColour (c.interpolatedWith (text, lv)); g.fillEllipse (ear);
        }
        g.setColour (sub.brighter (0.3f)); g.setFont (font (9.5f, 1, 0.1f));
        g.drawText ("YOU", juce::Rectangle<float> (h.x - 30.0f, h.y + 13.0f, 60.0f, 12.0f), juce::Justification::centred);
        g.drawText ("FROM ABOVE  (drag the sound)", juce::Rectangle<float> (r.getX(), r.getY() + 5.0f, r.getWidth(), 12.0f), juce::Justification::centred);
        const auto d = dot();
        g.setColour (c.withAlpha (0.3f)); g.drawLine (d.x, d.y, h.x - 16.0f, h.y, 1.0f); g.drawLine (d.x, d.y, h.x + 16.0f, h.y, 1.0f);
        juce::Path p; p.addEllipse (d.x - 9.0f, d.y - 9.0f, 18.0f, 18.0f);
        art::glow (g, p, green.withAlpha (0.8f), 10);
        g.setColour (green); g.fillPath (p);
        g.setColour (text); g.drawEllipse (d.x - 9.0f, d.y - 9.0f, 18.0f, 18.0f, 1.2f);
    }
    void mouseDown (const juce::MouseEvent& e) override { mouseDrag (e); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! isEnabled()) return;
        const auto r = area(); const auto h = head();
        page.setValue ("panner.x", juce::jlimit (-1.0f, 1.0f, (e.position.x - r.getCentreX()) / juce::jmax (1.0f, r.getWidth() * 0.5f - 18.0f)));
        page.setValue ("panner.distance", juce::jlimit (0.0f, 1.0f, (h.y - 22.0f - e.position.y) / juce::jmax (1.0f, h.y - 22.0f - r.getY() - 30.0f)));
    }
};

// ================================================================================================ the sound display (BEFORE / AFTER)
class RoomPage::Picture : public juce::Component
{
public:
    explicit Picture (RoomPage& p) : page (p) {}
    RoomPage& page;
    AudioPtr before, after;            // after == nullptr: this room has not run on this sound yet
    juce::Image imBefore, imAfter;
    const Audio* keyBefore = nullptr; const Audio* keyAfter = nullptr;
    float playhead = -1.0f; int playing = -1;

    void setSounds (AudioPtr b, AudioPtr a)
    {
        before = std::move (b); after = std::move (a);
        const auto c = RoomPage::colourOf (page.now);
        if (before.get() != keyBefore) { keyBefore = before.get(); imBefore = before != nullptr ? rooms::spectrogram (*before, 360, 72, purple, cyan) : juce::Image(); }
        if (after.get() != keyAfter) { keyAfter = after.get(); imAfter = after != nullptr ? rooms::spectrogram (*after, 360, 72, c.darker (0.2f), green) : juce::Image(); }
        repaint();
    }
    void forget() { keyBefore = keyAfter = nullptr; imBefore = imAfter = {}; }

    void frame (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title, const juce::String& info, juce::Colour c, const juce::Image& im, bool has,
                const juce::String& empty, float head)
    {
        g.setColour (juce::Colour (0xff07060f)); g.fillRoundedRectangle (r, 7.0f);
        if (has && im.isValid())
        {
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path clip; clip.addRoundedRectangle (r.reduced (1.0f), 6.0f); g.reduceClipRegion (clip);
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (im, r.reduced (1.0f), juce::RectanglePlacement::stretchToFit);
            if (head >= 0) { g.setColour (text.withAlpha (0.85f)); g.fillRect (r.getX() + r.getWidth() * head, r.getY(), 1.5f, r.getHeight()); }
        }
        else
        {
            g.setColour (sub.withAlpha (0.85f)); g.setFont (font (11.5f));
            g.drawFittedText (empty, r.reduced (10, 16).toNearestInt(), juce::Justification::centred, 3, 0.85f);
        }
        g.setColour (c.withAlpha (has ? 0.7f : 0.3f)); g.drawRoundedRectangle (r, 7.0f, 1.0f);
        auto plate = [&] (const juce::String& t, bool right, juce::Colour ink, int weight)
        {
            if (t.isEmpty()) return;
            g.setFont (font (10.0f, weight, weight == 2 ? 0.14f : 0.0f));
            const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), t) + 14.0f;
            if (w > r.getWidth() - 8.0f) return;
            auto p = juce::Rectangle<float> (right ? r.getRight() - 5.0f - w : r.getX() + 5.0f, r.getY() + 4.0f, w, 15.0f);
            g.setColour (night0.withAlpha (0.85f)); g.fillRoundedRectangle (p, 7.5f);
            g.setColour (ink); g.drawText (t, p, juce::Justification::centred);
        };
        plate (title, false, c, 2);
        plate (info, true, text.withAlpha (0.9f), 1);
    }

    void paint (juce::Graphics& g) override
    {
        const auto room = page.now;
        const auto c = RoomPage::colourOf (room);
        auto all = getLocalBounds().toFloat().reduced (1.0f);
        const bool hasB = before != nullptr, hasA = after != nullptr;
        auto secs = [] (const AudioPtr& a) { return a != nullptr ? num ((float) a->seconds()) + " s" : juce::String(); };
        const float gap = 6.0f;
        auto top = all.removeFromTop ((all.getHeight() - gap) * (room == Room::Stretch ? 0.36f : 0.5f));
        all.removeFromTop (gap);
        if (room == Room::Stretch && hasB)
        {
            // BEFORE at its true size next to AFTER: you see how much longer it gets
            const float amount = hasA && before->seconds() > 0 ? (float) (after->seconds() / before->seconds()) : page.value ("pxs.stretchamount0");
            const float w = juce::jmax (24.0f, top.getWidth() / juce::jmax (1.0f, amount));
            auto small = top.withWidth (w);
            frame (g, small, "BEFORE", {}, purple, imBefore, true, "", playing == 0 ? playhead : -1.0f);
            g.setColour (sub.brighter (0.3f)); g.setFont (font (11.0f));
            g.drawFittedText ("Before: " + secs (before) + ", drawn at its real size next to the stretched sound below.", top.withTrimmedLeft (w + 10.0f).toNearestInt(),
                              juce::Justification::centredLeft, 3, 0.85f);
            g.setColour (c.withAlpha (0.45f));
            g.drawLine (small.getX(), small.getBottom(), all.getX(), all.getY(), 1.0f);
            g.drawLine (small.getRight(), small.getBottom(), all.getRight(), all.getY(), 1.0f);
        }
        else frame (g, top, "BEFORE", secs (before), purple, imBefore, hasB, "Load a sound first.", playing == 0 ? playhead : -1.0f);
        juce::String empty;
        if (hasB) empty = room == Room::Stretch ? "Press RENDER. It will be about " + num ((float) before->seconds() * page.value ("pxs.stretchamount0")) + " s long."
                                                : juce::String ("Pick an instrument, then press REBIRTH.");
        frame (g, all, "AFTER", secs (after), c, imAfter, hasA, empty, playing == 1 ? playhead : -1.0f);
    }
};

// ================================================================================================ the page
RoomPage::RoomPage (Editor& e) : ed (e)
{
    picture = std::make_unique<Picture> (*this);
    addChildComponent (*picture);
    runBtn = std::make_unique<RunButton>(); listenBtn = std::make_unique<RunButton>(); autoBtn = std::make_unique<RunButton>();
    for (auto* b : { runBtn.get(), listenBtn.get(), autoBtn.get() }) addChildComponent (b);
    listenBtn->solid = autoBtn->solid = false;
    runBtn->onClick = [this] { pressRun(); };
    listenBtn->onClick = [this] { pressListen(); };
    autoBtn->onClick = [this] { pressAuto(); };
    rebuild();
    startTimerHz (15);
}

RoomPage::~RoomPage() { stopTimer(); }

Step& RoomPage::step() { return ed.settingsFor (op()); }
float RoomPage::value (const juce::String& id) const { return rooms::val (ed.settingsFor (op()), id); }
int RoomPage::instrument() const { return juce::roundToInt (ed.settingsFor (Op::Reborn).get ("instrument")); }
bool RoomPage::moduleOn (int m) const { return rooms::val (ed.settingsFor (Op::XStretch), rooms::moduleKey (m)) > 0.5f; }
bool RoomPage::listening() const { return ed.proc.spaceListening(); }
juce::String RoomPage::runLabel() const { return runBtn->label; }

void RoomPage::setValue (const juce::String& id, float v)
{
    if (auto* c = rooms::control (now, id)) step().values[id] = juce::jlimit (c->lo, c->hi, v);
    else return;
    if (groups != nullptr) for (auto* b : groups->bars) if (b->c.key == id) b->set (value (id), false);
    for (auto* k : knobs) if (k->c.key == id) k->set (value (id), false);
    for (auto* m : { panner.get(), reverb.get() }) if (m != nullptr) for (auto* k : m->knobs) if (k->c.key == id) k->set (value (id), false);
    if (now == Room::Space) { pushSpace(); if (pad != nullptr) pad->repaint(); }
    if (now == Room::Reborn) { if (rangeBox != nullptr) rangeBox->repaint(); repaint (noteR.toNearestInt()); }
    if (now == Room::Stretch) { picture->repaint(); repaint (infoR.toNearestInt()); if (id == "pxs.bypass_stretch0" || id == "pxs.binauralbeats") refreshEnabled(); }
}

void RoomPage::setModule (int m, bool on)
{
    if (now != Room::Stretch || m < 0 || m > 8) return;
    step().values[rooms::moduleKey (m)] = on ? 1.0f : 0.0f;
    refreshEnabled();
    if (groups != nullptr) groups->repaint();
    if (chain != nullptr) chain->repaint();
}

juce::Array<int> RoomPage::chainOrder() const
{
    juce::Array<int> order; bool used[9] {}; bool ok = true;
    for (int i = 0; i < 9; ++i)
    {
        const int m = juce::roundToInt (rooms::val (ed.settingsFor (Op::XStretch), "pxs.order" + juce::String (i)));
        if (m < 0 || m > 8 || used[m]) ok = false; else used[m] = true;
        order.add (m);
    }
    if (! ok) { order.clear(); for (int i = 0; i < 9; ++i) order.add (i); }
    return order;
}

void RoomPage::moveInChain (int from, int to)
{
    if (now != Room::Stretch || from < 0 || from > 8 || to < 0 || to > 8 || from == to) return;
    auto order = chainOrder();
    order.move (from, to);
    for (int i = 0; i < 9; ++i) step().values["pxs.order" + juce::String (i)] = (float) order[i];
    if (chain != nullptr) chain->repaint();
}

void RoomPage::addFreeFilterNode (double x, double y)
{
    if (groups == nullptr) return;
    groups->ffEnv->AddNode ({ juce::jlimit (0.0, 1.0, x), juce::jlimit (0.0, 1.0, y) });
    groups->ffEnv->SortNodes();
    groups->sync();
    groups->env.repaint();
}

void RoomPage::scrollToGroup (const juce::String& title)
{
    if (groups == nullptr || groupView == nullptr) return;
    for (auto& gr : groups->groups) if (gr.title == title) groupView->setViewPosition (0, gr.box.getY());
}

bool RoomPage::freeFilterDrawn() const { return ed.settingsFor (Op::XStretch).data.isNotEmpty(); }

void RoomPage::pickInstrument (int index)
{
    ed.settingsFor (Op::Reborn).values["instrument"] = (float) index;
    if (modelList != nullptr) modelList->setSelectedId (index + 1, juce::dontSendNotification);
    pressAuto();   // a new instrument: the tune is moved to where that instrument is at home (Pitch Shift shows by how much)
    soundChanged();
}

juce::StringArray RoomPage::controlIds() const
{
    juce::StringArray a;
    if (groups != nullptr && now == Room::Stretch) for (auto* b : groups->bars) a.add (b->c.key);
    if (now == Room::Reborn) for (auto* k : knobs) a.add (k->c.key);
    if (now == Room::Space) for (auto* m : { panner.get(), reverb.get() }) if (m != nullptr) for (auto* k : m->knobs) a.add (k->c.key);
    return a;
}

juce::String RoomPage::controlName (const juce::String& id) const { if (auto* c = rooms::control (now, id)) return c->name; return {}; }

bool RoomPage::controlEnabled (const juce::String& id) const
{
    if (groups != nullptr) for (auto* b : groups->bars) if (b->c.key == id) return b->isEnabled();
    for (auto* k : knobs) if (k->c.key == id) return k->isEnabled();
    for (auto* m : { panner.get(), reverb.get() }) if (m != nullptr) for (auto* k : m->knobs) if (k->c.key == id) return k->isEnabled();
    return false;
}

void RoomPage::setRoom (Room r)
{
    if (r == Room::Lab || r == Room::Grow) return;
    if (r != now)
    {
        if (now == Room::Space && listening()) ed.proc.setSpaceListening (false);
        now = r;
        picture->forget();
        rebuild();
    }
    soundChanged();
}

// the parts of the room now on the page (each room is built like the original plug-in's window)
void RoomPage::rebuild()
{
    const auto c = colourOf (now);
    groups.reset(); groupView.reset(); chain.reset(); modelList.reset(); rangeBox.reset(); pad.reset(); panner.reset(); reverb.reset(); knobs.clear();
    listenBtn->setVisible (false); autoBtn->setVisible (false);
    runBtn->setVisible (true); runBtn->colour = c; listenBtn->colour = c; autoBtn->colour = c;
    picture->setVisible (now != Room::Space);
    if (now == Room::Stretch)
    {
        groups = std::make_unique<Groups> (*this);
        groupView = std::make_unique<juce::Viewport>();
        groupView->setViewedComponent (groups.get(), false);
        groupView->setScrollBarsShown (true, false);
        groupView->setScrollBarThickness (8);
        addAndMakeVisible (*groupView);
        chain = std::make_unique<Chain> (*this);
        addAndMakeVisible (*chain);
        runBtn->label = "RENDER";
    }
    else if (now == Room::Reborn)
    {
        modelList = std::make_unique<juce::ComboBox>();
        const auto names = rooms::rebornInstruments();
        for (int i = 0; i < names.size(); ++i) modelList->addItem (names[i], i + 1);
        modelList->setTextWhenNothingSelected (names.isEmpty() ? "(the neural instruments are not in this build)" : "Pick an instrument...");
        modelList->setSelectedId (instrument() + 1, juce::dontSendNotification);
        modelList->onChange = [this] { pickInstrument (modelList->getSelectedId() - 1); };
        addAndMakeVisible (*modelList);
        rangeBox = std::make_unique<RangeBox> (*this);
        addAndMakeVisible (*rangeBox);
        for (auto& ctl : rooms::controls (Room::Reborn))
        {
            if (ctl.group == "Model range") continue;   // (Input Pitch / Input Gain are the box itself, as in the original)
            auto* k = knobs.add (new Knob());
            k->c = ctl; k->colour = c; k->v = value (ctl.key);
            k->onChange = [this, key = ctl.key] (float v) { setValue (key, v); };
            addAndMakeVisible (k);
        }
        runBtn->label = "REBIRTH"; autoBtn->label = "AUTO FIT"; autoBtn->setVisible (true);
    }
    else if (now == Room::Space)
    {
        pad = std::make_unique<Pad> (*this); addAndMakeVisible (*pad);
        panner = std::make_unique<Module> (*this, "Spatial Panner 1", "panner", juce::Colour (0xff35e0c0)); addAndMakeVisible (*panner);
        reverb = std::make_unique<Module> (*this, "Reverb 1", "fdn", juce::Colour (0xffff9a3a)); addAndMakeVisible (*reverb);
        runBtn->label = "PRINT IT"; listenBtn->label = "LISTEN"; listenBtn->setVisible (true);
    }
    resized();
    refreshEnabled();
    repaint();
}

void RoomPage::pushSpace()
{
    float v[rooms::kSpaceControls]; rooms::spacePins (ed.settingsFor (Op::Space), v);
    ed.proc.setSpaceLive (v);
}

void RoomPage::pressRun() { ed.runRoom (op()); }

void RoomPage::pressListen()
{
    if (now != Room::Space) return;
    ed.spaceListen();
    listenBtn->lit = listening(); listenBtn->repaint();
}

void RoomPage::pressAuto()
{
    if (now != Room::Reborn) return;
    const auto names = rooms::rebornInstruments();
    const int inst = instrument();
    if (inst < 0 || inst >= names.size()) { ed.toast ("Pick an instrument first.", true); return; }
    refreshPictures();
    setValue ("ddsp.PitchShift", (float) rooms::autoShift (tune, rooms::instrumentRange (names[inst])));
}

juce::String RoomPage::verdict() const
{
    if (rooms::rebornInstruments().isEmpty()) return "The neural instruments are not in this build.";
    if (! ed.engine.hasSpecimen()) return "Load a sound, then pick who plays it.";
    const auto names = rooms::rebornInstruments();
    const int inst = instrument();
    if (inst < 0 || inst >= names.size()) return "The dots are the notes in your sound. Pick who plays them.";
    const auto who = names[inst].toUpperCase();
    if (tune.voiced < 0.2f) return "No clear tune in this sound: the " + who + " will guess. Expect something strange.";
    const auto range = rooms::instrumentRange (names[inst]);
    const float at = tune.median + value ("ddsp.PitchShift") - value ("ddsp.InputPitch") * 127.0f;
    if (at < range.getStart() - 2.0f) return "The dots are LEFT of the box: too low for a " + who + ". Press AUTO FIT or turn Pitch Shift up.";
    if (at > range.getEnd() + 2.0f) return "The dots are RIGHT of the box: too high for a " + who + ". Press AUTO FIT or turn Pitch Shift down.";
    return "The dots sit inside the box: this is where it sounds most like a real " + who + ".";
}

void RoomPage::refreshPictures()
{
    auto& e = ed.engine;
    AudioPtr before, after;
    if (e.hasSpecimen())
    {
        const auto word = juce::String (rooms::info (now).name) + " ";
        const bool mine = e.historyCursor() > 0 && e.current().label.startsWith (word);
        before = mine ? e.audioUnderLast() : e.processedAudio();
        if (mine) after = e.processedAudio();
    }
    if (now == Room::Reborn && before.get() != melodyOf)
    {
        melodyOf = before.get();
        tune = before != nullptr ? rooms::melody (*before) : rooms::Melody();
        if (rangeBox != nullptr) rangeBox->repaint();
    }
    if (now != Room::Space) picture->setSounds (before, after);
}

// a control that would do nothing right now is greyed: no sound to work on, no instrument picked yet (REBORN), or its
// spectral module is switched off (STRETCH)
void RoomPage::refreshEnabled()
{
    const bool has = ed.engine.hasSpecimen(), busy = ed.engine.busy();
    const bool inst = now != Room::Reborn || (instrument() >= 0 && instrument() < rooms::rebornInstruments().size());
    if (groups != nullptr)
        for (auto& gr : groups->groups)
            for (auto* b : gr.bars)
            {
                bool on = has && (gr.module < 0 || moduleOn (gr.module));
                if (gr.binaural && b->c.key != "pxs.binauralbeats") on = on && value ("pxs.binauralbeats") > 0.5f;
                // the original's controls that have nothing to act on in the lab (it renders once: no loop, no live input, no recording)
                if (b->c.key == "pxs.loopxfadelen0" || b->c.key == "pxs.passthrough0" || b->c.key == "pxs.maxcapturelen_0") on = false;
                if (b->c.key == "pxs.dryplayrate0") on = on && value ("pxs.bypass_stretch0") > 0.5f;   // (only heard with Bypass stretch on)
                b->setEnabled (on);
            }
    if (groups != nullptr) { groups->env.setEnabled (has && moduleOn (7)); groups->env.setAlpha (has && moduleOn (7) ? 1.0f : 0.35f); }
    if (chain != nullptr) chain->setEnabled (has);
    for (auto* k : knobs) k->setEnabled (has && inst);
    for (auto* m : { panner.get(), reverb.get() }) if (m != nullptr) for (auto* k : m->knobs) k->setEnabled (has);
    if (rangeBox != nullptr) rangeBox->setEnabled (has && inst);
    if (pad != nullptr) pad->setEnabled (has);
    if (modelList != nullptr) modelList->setEnabled (! rooms::rebornInstruments().isEmpty());
    runBtn->setEnabled (! busy && has && inst);
    runBtn->label = busy ? juce::String ("WORKING...") : juce::String (now == Room::Stretch ? "RENDER" : now == Room::Reborn ? "REBIRTH" : "PRINT IT");
    listenBtn->setEnabled (has); listenBtn->lit = listening();
    autoBtn->setEnabled (has && inst);
    for (auto* b : { runBtn.get(), listenBtn.get(), autoBtn.get() }) b->repaint();
}

void RoomPage::soundChanged()
{
    refreshPictures();
    refreshEnabled();
    repaint();
}

void RoomPage::timerCallback()
{
    if (! isVisible()) return;
    const int slot = ed.proc.player.playingSlot();
    const float head = ed.proc.player.playhead();
    const int shown = slot == Player::Processed ? (picture->after != nullptr ? 1 : 0) : -1;
    if (shown != picture->playing || (shown >= 0 && std::abs (head - picture->playhead) > 0.002f)) { picture->playing = shown; picture->playhead = head; picture->repaint(); }
    if (now == Room::Stretch && groups != nullptr) groups->sync();
    if (now == Room::Space && pad != nullptr)
    {
        const bool on = listening();
        const float l = on ? ed.proc.spaceMeter (0) : 0.0f, r = on ? ed.proc.spaceMeter (1) : 0.0f;
        if (std::abs (l - pad->meterL) > 0.002f || std::abs (r - pad->meterR) > 0.002f) { pad->meterL = earL = l; pad->meterR = earR = r; pad->repaint(); }
        if (listenBtn->lit != on) { listenBtn->lit = on; listenBtn->repaint(); }
    }
}

void RoomPage::paint (juce::Graphics& g)
{
    const auto c = colourOf (now);
    const auto& inf = rooms::info (now);
    art::glassPanel (g, getLocalBounds().toFloat().reduced (1.0f), c, 14.0f, 0.7f);
    {
        juce::GlyphArrangement ga; ga.addLineOfText (font (24.0f, 2, 0.12f), inf.name, 0.0f, 0.0f);
        juce::Path p; ga.createPath (p);
        const auto b = p.getBounds();
        p.applyTransform (juce::AffineTransform::translation (titleR.getX() - b.getX(), titleR.getCentreY() - b.getCentreY()));
        art::glow (g, p, c.withAlpha (0.6f), 10);
        g.setGradientFill (juce::ColourGradient (text, titleR.getX(), 0, c.brighter (0.3f), titleR.getX() + b.getWidth(), 0, false));
        g.fillPath (p);
    }
    g.setColour (text.withAlpha (0.92f)); g.setFont (font (12.0f));
    g.drawFittedText (juce::String (inf.does) + "  " + inf.bestOn, doesR.toNearestInt(), juce::Justification::centredLeft, 2, 0.8f);
    if (now == Room::Stretch && ed.engine.hasSpecimen())
    {
        // the original's info line: the sound and how long the render will be
        const float secs = (float) (picture->before != nullptr ? picture->before->seconds() : ed.engine.processedAudio()->seconds());
        g.setColour (c.brighter (0.3f)); g.setFont (font (11.5f, 1));
        const float part = juce::jmax (0.01f, value ("pxs.playrange_end0") - value ("pxs.playrange_start0"));   // (Sound start .. Sound end)
        g.drawFittedText (num (secs * part) + " s in  >  about " + num (secs * part * value ("pxs.stretchamount0")) + " s out", infoR.toNearestInt(), juce::Justification::centredRight, 1, 0.8f);
    }
    if (now == Room::Reborn)
    {
        g.setColour (text.withAlpha (0.92f)); g.setFont (font (11.5f, 1));
        g.drawFittedText (verdict(), noteR.toNearestInt(), juce::Justification::centredLeft, 2, 0.8f);
    }
    if (now == Room::Space && panner != nullptr && reverb != nullptr)
    {
        // the patch cords: the sound into the panner, the panner into the reverb, the reverb out
        const auto a = panner->getBounds().toFloat().getTopLeft() + panner->port (true), b = reverb->getBounds().toFloat().getTopLeft() + reverb->port (false);
        const auto in = panner->getBounds().toFloat().getTopLeft() + panner->port (false), out = reverb->getBounds().toFloat().getTopLeft() + reverb->port (true);
        g.setColour (cyan.withAlpha (0.8f));
        juce::Path w; w.startNewSubPath (a); w.cubicTo (a.translated (16, 0), b.translated (-16, 0), b);
        w.startNewSubPath (in.translated (-22.0f, 0.0f)); w.lineTo (in);
        w.startNewSubPath (out); w.lineTo (out.translated (22.0f, 0.0f));
        g.strokePath (w, juce::PathStrokeType (2.0f));
        g.setColour (sub.brighter (0.3f)); g.setFont (font (9.0f, 1, 0.08f));
        g.drawText ("SOUND", juce::Rectangle<float> (in.x - 62.0f, in.y - 18.0f, 44.0f, 10.0f), juce::Justification::centredRight);
        g.drawText ("TO FL", juce::Rectangle<float> (out.x + 18.0f, out.y - 18.0f, 44.0f, 10.0f), juce::Justification::centredLeft);
    }
}

void RoomPage::resized()
{
    auto r = getLocalBounds().toFloat().reduced (14.0f, 10.0f);
    const bool tight = getWidth() < 700;
    auto top = r.removeFromTop (tight ? 62.0f : 30.0f);
    if (tight) { titleR = top.removeFromTop (28.0f); doesR = top; }
    else { titleR = top.removeFromLeft (150.0f); doesR = top; }
    r.removeFromTop (6.0f);
    if (now == Room::Stretch)
    {
        auto right = tight ? r.removeFromBottom (300.0f) : r.removeFromRight (juce::jlimit (300.0f, 430.0f, r.getWidth() * 0.37f));
        if (tight) r.removeFromBottom (10.0f); else r.removeFromRight (12.0f);
        // left: the render button and the info line, then the groups (as the original stacks them)
        auto bar = r.removeFromTop (34.0f);
        runBtn->setBounds (bar.removeFromLeft (150.0f).toNearestInt());
        infoR = bar.reduced (8.0f, 0.0f);
        r.removeFromTop (6.0f);
        if (groupView != nullptr)
        {
            groupView->setBounds (r.toNearestInt());
            groups->layout (juce::jmax (120, (int) r.getWidth() - 10));
        }
        // right: the processing chain, then the sound display
        if (chain != nullptr) chain->setBounds (right.removeFromTop (98.0f).toNearestInt());
        right.removeFromTop (6.0f);
        picture->setBounds (right.toNearestInt());
    }
    else if (now == Room::Reborn)
    {
        auto right = tight ? r.removeFromBottom (260.0f) : r.removeFromRight (juce::jlimit (260.0f, 380.0f, r.getWidth() * 0.33f));
        if (tight) r.removeFromBottom (10.0f); else r.removeFromRight (12.0f);
        // left, as the original: the model list on top, the Pitch / Volume box in the middle, the knobs along the bottom
        if (modelList != nullptr) modelList->setBounds (r.removeFromTop (28.0f).toNearestInt());
        r.removeFromTop (6.0f);
        knobsR = r.removeFromBottom (juce::jlimit (84.0f, 112.0f, r.getHeight() * 0.42f));
        r.removeFromBottom (4.0f);
        if (rangeBox != nullptr) rangeBox->setBounds (r.toNearestInt());
        const int n = knobs.size();
        if (n > 0)
        {
            const float kw = knobsR.getWidth() / (float) n;
            for (int i = 0; i < n; ++i) knobs[i]->setBounds (juce::Rectangle<float> (knobsR.getX() + (float) i * kw, knobsR.getY(), kw, knobsR.getHeight()).reduced (2.0f, 0.0f).toNearestInt());
        }
        // right: the sound before and after, what the box is telling you, the buttons
        auto btn = right.removeFromBottom (38.0f);
        autoBtn->setBounds (btn.removeFromLeft (btn.getWidth() * 0.42f).toNearestInt()); btn.removeFromLeft (8.0f);
        runBtn->setBounds (btn.toNearestInt());
        right.removeFromBottom (4.0f);
        noteR = right.removeFromBottom (30.0f);
        right.removeFromBottom (2.0f);
        picture->setBounds (right.toNearestInt());
    }
    else if (now == Room::Space)
    {
        auto btn = r.removeFromBottom (38.0f);
        runBtn->setBounds (btn.removeFromRight (190.0f).toNearestInt()); btn.removeFromRight (8.0f);
        listenBtn->setBounds (btn.removeFromRight (150.0f).toNearestInt());
        r.removeFromBottom (6.0f);
        if (tight)
        {
            if (pad != nullptr) pad->setBounds (r.removeFromTop (200.0f).toNearestInt());
            r.removeFromTop (8.0f);
            if (panner != nullptr) panner->setBounds (r.removeFromTop (r.getHeight() * 0.5f - 4.0f).reduced (24.0f, 0.0f).toNearestInt());
            r.removeFromTop (8.0f);
            if (reverb != nullptr) reverb->setBounds (r.reduced (24.0f, 0.0f).toNearestInt());
        }
        else
        {
            if (pad != nullptr) pad->setBounds (r.removeFromLeft (juce::jlimit (200.0f, 280.0f, r.getWidth() * 0.22f)).toNearestInt());
            r.removeFromLeft (44.0f); r.removeFromRight (44.0f);
            const float mw = (r.getWidth() - 26.0f) * 0.5f;
            if (panner != nullptr) panner->setBounds (r.removeFromLeft (mw).toNearestInt());
            r.removeFromLeft (26.0f);
            if (reverb != nullptr) reverb->setBounds (r.toNearestInt());
        }
    }
}
}
