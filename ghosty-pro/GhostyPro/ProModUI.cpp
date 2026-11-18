// Ghosty Pro MODULATION UI (round 3 item 3): see ProModUI.h. Behaviour reference: the Serum 2 manual (drag-to-knob, amount
// rings, knob menus, a matrix list); own design and code.
#include "ProModUI.h"
#include "TypeIn.h"

namespace gpro
{
using namespace mod;
namespace
{
const pt::Palette& P() { return pt::pal(); }
using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
constexpr float kGreyedM = 0.78f;

// the editor's slider convention (ProContent::sliderConvention): hover value, Shift = fine, bars jump to the click
void convention (juce::Slider& s, juce::Component* popupParent)
{
    s.setPopupDisplayEnabled (true, true, popupParent, 700); s.getProperties().set ("hoverValue", true);
    s.setVelocityModeParameters (0.25, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
    if (! s.isRotary()) s.setSliderSnapsToMousePosition (true);
}
juce::String paramName (ProProcessor& p, const juce::String& id) { if (auto* prm = p.apvts.getParameter (id)) return prm->getName (40); return id; }
juce::String pct (float a) { const int v = juce::roundToInt (a * 100.0f); return (v > 0 ? "+" : "") + juce::String (v) + "%"; }
juce::String paramIdOf (const juce::Component* c) { return c != nullptr ? c->getProperties()["paramId"].toString() : juce::String(); }
// visible all the way up to 'root' (isShowing() also needs a window on the desktop, which a headless test editor doesn't have)
bool shownIn (const juce::Component* c, const juce::Component* root)
{
    for (; c != nullptr && c != root; c = c->getParentComponent()) if (! c->isVisible()) return false;
    return c == root;
}

void paintCardM (juce::Graphics& g, juce::Rectangle<int> rr, const juce::String& title, juce::Colour cc)
{   // the same card as the editor's pages (opaque panel, a tint at the top, a hairline, the title)
    auto r = rr.toFloat();
    g.setColour (P().card); g.fillRoundedRectangle (r, 12);
    juce::ColourGradient top (cc.withAlpha (0.10f), 0, r.getY(), cc.withAlpha (0.0f), 0, r.getY() + 34, false);
    g.setGradientFill (top); g.fillRoundedRectangle (r, 12);
    g.setColour (cc.withAlpha (0.25f)); g.drawRoundedRectangle (r, 12, 1.0f);
    g.setColour (P().dark ? cc.withAlpha (0.9f) : cc); g.setFont (ghosty::font (10.5f));
    g.drawText (title, r.reduced (12, 0).removeFromTop (22).translated (0, 2), juce::Justification::centredLeft);
}
} // namespace

juce::Colour ModUi::srcColour (int src)
{
    const auto& p = P();
    switch (src)
    {
        case env1: return p.slime;   case env2: return p.pink;    case env3: return p.ice;     case menv: return p.gold;
        case lfo1: return p.slime;   case lfo2: return p.gold;    case lfo3: return p.ecto;    case lfo4: return p.red;
        case mac1: case mac2: case mac3: case mac4: return p.pink;
        case vel: return p.gold;     case note: return p.ice;     case aftertouch: return p.ecto;
        case wheel: return p.slime;  case bend: return p.ice;     case random: return p.red;
        default: return p.dim;
    }
}

// ================================================================== a source tile
class ModTile : public juce::Component, public juce::SettableTooltipClient
{
public:
    ModTile (ModUi& u, int s, bool headerSize, juce::String shortLabel = {}) : ui (u), src (s), small (headerSize), label (std::move (shortLabel))
    {
        setTooltip ("Drag " + srcNames()[src] + " onto any knob to modulate it (hold it over a tab to switch pages). Hover: lights up what it modulates");
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        getProperties().set ("modTile", src);
    }
    const int src;
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        const auto c = ModUi::srcColour (src);
        const bool hot = ui.hoverSource() == src || (ui.drag().on && ui.drag().src == src && ui.drag().moveConn < 0);
        const float rr = juce::jmin (8.0f, r.getHeight() * 0.5f);
        g.setColour (P().card2); g.fillRoundedRectangle (r, rr);
        g.setColour (c.withAlpha (hot ? 0.30f : 0.14f)); g.fillRoundedRectangle (r, rr);
        g.setColour (c.withAlpha (hot ? 1.0f : 0.65f)); g.drawRoundedRectangle (r, rr, hot ? 1.6f : 1.0f);
        // grip dots
        const float gx = r.getX() + (small ? 5.0f : 8.0f), gy = r.getCentreY();
        g.setColour (c);
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 2; ++j) g.fillEllipse (gx + j * 3.5f - 1.1f, gy + (i - 1) * 3.5f - 1.1f, 2.2f, 2.2f);
        // live value (a thin bar along the bottom)
        const float live = ui.proc.modr.srcLive[src].load (std::memory_order_relaxed);
        const float u = juce::jlimit (0.0f, 1.0f, unipolarOf (src, live));
        auto bar = r.reduced (rr * 0.6f, 0).removeFromBottom (2.0f).translated (0, -1.5f);
        g.setColour (c.withAlpha (0.85f)); g.fillRect (bar.withWidth (bar.getWidth() * u));
        // name + how many knobs it drives
        int n = 0; for (auto& m : ui.proc.modr.list()) n += m.src == src ? 1 : 0;
        auto t = r.withTrimmedLeft (small ? 13.0f : 18.0f).withTrimmedRight (4.0f);
        g.setFont (ghosty::font (small ? 9.0f : 10.5f));
        if (n > 0)
        {
            auto badge = t.removeFromRight (small ? 12.0f : 18.0f);
            g.setColour (c); g.drawText (juce::String (n), badge, juce::Justification::centred);
        }
        g.setColour (P().white); g.drawText (label.isNotEmpty() ? label : small ? srcShortNames()[src] : srcNames()[src], t, juce::Justification::centredLeft, true);
    }
    void mouseEnter (const juce::MouseEvent&) override { ui.setHoverSource (src); }
    void mouseExit (const juce::MouseEvent&) override { if (ui.hoverSource() == src && ! ui.drag().on) ui.setHoverSource (0); }
    void mouseDown (const juce::MouseEvent& e) override { pressed = ! e.mods.isPopupMenu(); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (pressed && ! ui.drag().on && e.getDistanceFromDragStart() > 4) ui.beginDrag (src, -1, e.getScreenPosition());
    }
    void mouseUp (const juce::MouseEvent&) override { pressed = false; }
private:
    ModUi& ui; const bool small; const juce::String label; bool pressed = false;
};

// ================================================================== the overlay: rings, highlights, the drag
class ModOverlay : public juce::Component
{
public:
    explicit ModOverlay (ModUi& u) : ui (u) { setName ("MOD OVERLAY"); setInterceptsMouseClicks (true, false); setOpaque (false); }
    bool hitTest (int x, int y) override
    {
        if (ui.dragSt.on || probing) return false;
        return ui.ringAt ({ (float) x, (float) y }).conn >= 0;
    }
    bool probing = false;
    void paint (juce::Graphics& g) override
    {
        const auto list = ui.proc.modr.list();
        std::map<juce::String, std::vector<int>> byDst;
        for (int i = 0; i < (int) list.size(); ++i) byDst[list[(size_t) i].dst].push_back (i);
        for (auto& sp : ui.knobs())
        {
            auto* s = sp.getComponent();
            if (s == nullptr) continue;
            const auto id = paramIdOf (s);
            auto it = byDst.find (id);
            const bool hoverTarget = ui.hoverSrc > 0 && it != byDst.end() && std::any_of (it->second.begin(), it->second.end(), [&] (int i) { return list[(size_t) i].src == ui.hoverSrc; });
            const bool dropTarget = ui.dragSt.on && ui.dragSt.target.getComponent() == s;
            if (it == byDst.end() && ! dropTarget) continue;
            if (! ui.knobVisible (*s)) continue;
            ModUi::KnobGeom kg; if (! ui.knobGeom (*s, kg)) continue;
            if (hoverTarget)
            {
                const auto c = ModUi::srcColour (ui.hoverSrc);
                g.setColour (c.withAlpha (0.22f)); g.fillEllipse (juce::Rectangle<float> (kg.rad * 2.0f + 16, kg.rad * 2.0f + 16).withCentre (kg.c));
                g.setColour (c); g.drawEllipse (juce::Rectangle<float> (kg.rad * 2.0f + 14, kg.rad * 2.0f + 14).withCentre (kg.c), 1.5f);
            }
            if (dropTarget)
            {
                const auto c = ModUi::srcColour (ui.dragSt.src);
                g.setColour (c.withAlpha (0.25f)); g.fillEllipse (juce::Rectangle<float> (kg.rad * 2.0f + 18, kg.rad * 2.0f + 18).withCentre (kg.c));
                g.setColour (c); g.drawEllipse (juce::Rectangle<float> (kg.rad * 2.0f + 16, kg.rad * 2.0f + 16).withCentre (kg.c), 2.0f);
            }
            if (it == byDst.end()) continue;
            int ring = 0;
            for (int i : it->second)
            {
                if (ring >= 3) break;
                const auto& m = list[(size_t) i];
                const float R = ModUi::ringRadius (kg.rad, ring);
                const auto c = ModUi::srcColour (m.src).withMultipliedAlpha (m.bypass ? 0.35f : 1.0f);
                auto angle = [&] (float p) { return kg.a0 + juce::jlimit (0.0f, 1.0f, p) * (kg.a1 - kg.a0); };
                // the faint full travel, then the depth arc (unipolar: knob -> knob + amount; bipolar: knob -/+ amount)
                juce::Path track; track.addCentredArc (kg.c.x, kg.c.y, R, R, 0, kg.a0, kg.a1, true);
                g.setColour (c.withAlpha (0.13f * c.getFloatAlpha())); g.strokePath (track, juce::PathStrokeType (2.4f));
                const float lo = m.bipolar ? kg.pos - std::abs (m.amount) : juce::jmin (kg.pos, kg.pos + m.amount);
                const float hi = m.bipolar ? kg.pos + std::abs (m.amount) : juce::jmax (kg.pos, kg.pos + m.amount);
                if (hi - lo > 0.002f)
                {
                    juce::Path arc; arc.addCentredArc (kg.c.x, kg.c.y, R, R, 0, angle (lo), angle (hi), true);
                    g.setColour (c); g.strokePath (arc, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }
                if (! m.bypass)
                {   // where the modulation is right now
                    const float a = angle (kg.pos + ui.proc.modr.liveValue (i));
                    const juce::Point<float> d (kg.c.x + std::sin (a) * R, kg.c.y - std::cos (a) * R);
                    g.setColour (P().white); g.fillEllipse (juce::Rectangle<float> (4.2f, 4.2f).withCentre (d));
                }
                ++ring;
            }
            if ((int) it->second.size() > 3)
            {
                g.setColour (P().white); g.setFont (ghosty::font (8.5f));
                g.drawText ("+" + juce::String ((int) it->second.size() - 3), juce::Rectangle<float> (20, 10).withCentre (kg.c.translated (kg.rad + 10, -kg.rad - 4)), juce::Justification::centred);
            }
        }
        if (ringDrag >= 0 || bubble.isNotEmpty())
        {   // the amount bubble while a ring is dragged / hovered
            auto t = juce::Rectangle<float> (ghosty::textWidth (ghosty::font (11.0f), bubble) + 16, 20).withCentre (bubbleAt);
            t = t.withY (juce::jmax (0.0f, t.getY())).withX (juce::jlimit (0.0f, (float) getWidth() - t.getWidth(), t.getX()));
            g.setColour (P().card2); g.fillRoundedRectangle (t, 8);
            g.setColour (bubbleColour); g.drawRoundedRectangle (t, 8, 1.2f);
            g.setColour (P().white); g.setFont (ghosty::font (11.0f)); g.drawText (bubble, t, juce::Justification::centred);
        }
        if (ui.dragSt.on)
        {   // the dragged tile
            const auto c = ModUi::srcColour (ui.dragSt.src);
            const auto txt = (ui.dragSt.moveConn >= 0 ? "MOVE " : "") + srcShortNames()[ui.dragSt.src];
            auto t = juce::Rectangle<float> (ghosty::textWidth (ghosty::font (10.5f), txt) + 22, 22).withCentre (ui.dragSt.pos.translated (14, 14));
            g.setColour (P().card2); g.fillRoundedRectangle (t, 9);
            g.setColour (c.withAlpha (0.3f)); g.fillRoundedRectangle (t, 9);
            g.setColour (c); g.drawRoundedRectangle (t, 9, 1.6f);
            g.setColour (P().white); g.setFont (ghosty::font (10.5f)); g.drawText (txt, t, juce::Justification::centred);
        }
    }
    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto h = ui.ringAt (e.position);
        setMouseCursor (h.conn >= 0 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
        showBubble (h);
    }
    void mouseExit (const juce::MouseEvent&) override { if (ringDrag < 0) { bubble = {}; repaint(); } }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto h = ui.ringAt (e.position);
        if (h.conn < 0) return;
        if (e.mods.isPopupMenu()) { if (h.slider != nullptr) ui.showKnobMenu (*h.slider); return; }
        ringDrag = h.conn; ringStart = ui.proc.modr.list()[(size_t) h.conn].amount;
        showBubble (h);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (ringDrag < 0) return;
        const float k = e.mods.isShiftDown() ? 0.001f : 0.005f;
        ui.proc.modr.setAmount (ringDrag, juce::jlimit (-1.0f, 1.0f, ringStart - (float) e.getDistanceFromDragStartY() * k));
        showBubble (ui.ringAt (e.mouseDownPosition), ringDrag);
    }
    void mouseUp (const juce::MouseEvent&) override { ringDrag = -1; bubble = {}; repaint(); }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {   // double-click a ring: its amount back to 0 (like a knob's default)
        const auto h = ui.ringAt (e.position);
        if (h.conn >= 0) ui.proc.modr.setAmount (h.conn, 0.0f);
    }
    int ringDrag = -1; float ringStart = 0;
    juce::String bubble; juce::Point<float> bubbleAt; juce::Colour bubbleColour;
    void showBubble (const ModUi::RingHit& h, int conn = -1)
    {
        if (conn < 0) conn = h.conn;
        const auto l = ui.proc.modr.list();
        if (! juce::isPositiveAndBelow (conn, (int) l.size()) || h.slider == nullptr) { if (bubble.isNotEmpty()) { bubble = {}; repaint(); } return; }
        const auto& m = l[(size_t) conn];
        ModUi::KnobGeom kg; ui.knobGeom (*h.slider, kg);
        bubble = srcShortNames()[m.src] + "  " + pct (m.amount) + (m.bypass ? "  (bypassed)" : "");
        bubbleAt = kg.c.translated (0, -kg.rad - 22); bubbleColour = ModUi::srcColour (m.src);
        repaint();
    }
private:
    ModUi& ui;
};

// ================================================================== the custom curve editor (popup)
class CurveEditor : public juce::Component
{
public:
    CurveEditor (ModUi& u, int connId) : ui (u), id (connId)
    {
        for (auto& m : ui.proc.modr.list()) if (m.id == id) { px = m.px; py = m.py; if (m.curve != cCustom && m.curve != cLinear) sampleCurve (m.curve); }
        setSize (280, 214);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (P().card);
        g.setColour (P().ecto); g.setFont (ghosty::font (10.5f)); g.drawText ("CUSTOM CURVE  (drag the points)", getLocalBounds().removeFromTop (22).reduced (10, 0), juce::Justification::centredLeft);
        {
            pt::ScopedDark screen;
            auto r = plot().toFloat();
            g.setColour (P().deep); g.fillRoundedRectangle (r.expanded (6), 8);
            g.setColour (P().white.withAlpha (0.08f));
            for (int i = 1; i < 4; ++i) { g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * i / 4.0f), r.getX(), r.getRight()); g.drawVerticalLine ((int) (r.getX() + r.getWidth() * i / 4.0f), r.getY(), r.getBottom()); }
            juce::Path p;
            for (int i = 0; i <= 64; ++i) { const float u = (float) i / 64.0f; const auto q = toScreen (u, applyCurve (cCustom, u, px.data(), py.data())); if (i == 0) p.startNewSubPath (q); else p.lineTo (q); }
            g.setColour (P().slime.withAlpha (0.25f)); g.strokePath (p, juce::PathStrokeType (5.0f));
            g.setColour (P().slime); g.strokePath (p, juce::PathStrokeType (1.8f));
            for (int k = 0; k < 5; ++k)
            {
                const auto q = toScreen (px[(size_t) k], py[(size_t) k]);
                g.setColour (k == drag ? P().white : P().slime); g.fillEllipse (juce::Rectangle<float> (9, 9).withCentre (q));
                g.setColour (P().deep); g.drawEllipse (juce::Rectangle<float> (9, 9).withCentre (q), 1.2f);
            }
        }
        const char* names[] { "LINEAR", "EXP", "LOG", "S" };
        for (int b = 0; b < 4; ++b)
        {
            auto r = button (b).toFloat();
            g.setColour (P().card2); g.fillRoundedRectangle (r, 6);
            g.setColour (P().ecto.withAlpha (0.5f)); g.drawRoundedRectangle (r, 6, 1.0f);
            g.setColour (P().white); g.setFont (ghosty::font (9.5f)); g.drawText (names[b], r, juce::Justification::centred);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int b = 0; b < 4; ++b) if (button (b).contains (e.getPosition())) { sampleCurve (b == 0 ? cLinear : b == 1 ? cExp : b == 2 ? cLog : cS); commit(); repaint(); return; }
        drag = -1; float best = 12.0f;
        for (int k = 0; k < 5; ++k) { const float d = toScreen (px[(size_t) k], py[(size_t) k]).getDistanceFrom (e.position); if (d < best) { best = d; drag = k; } }
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag < 0) return;
        const auto r = plot().toFloat();
        float x = (e.position.x - r.getX()) / r.getWidth(), y = 1.0f - (e.position.y - r.getY()) / r.getHeight();
        y = juce::jlimit (0.0f, 1.0f, y);
        if (drag > 0 && drag < 4) x = juce::jlimit (px[(size_t) drag - 1], px[(size_t) drag + 1], x); else x = drag == 0 ? 0.0f : 1.0f;
        px[(size_t) drag] = x; py[(size_t) drag] = y;
        commit(); repaint();
    }
    void mouseUp (const juce::MouseEvent&) override { drag = -1; repaint(); }
    // tests
    void setPoint (int k, float x, float y) { px[(size_t) k] = x; py[(size_t) k] = y; commit(); }
private:
    ModUi& ui; const int id; int drag = -1;
    std::array<float, 5> px { 0, .25f, .5f, .75f, 1 }, py { 0, .25f, .5f, .75f, 1 };
    juce::Rectangle<int> plot() const { return { 22, 34, getWidth() - 44, getHeight() - 34 - 44 }; }
    juce::Rectangle<int> button (int b) const { const int w = (getWidth() - 20 - 18) / 4; return { 10 + b * (w + 6), getHeight() - 30, w, 22 }; }
    juce::Point<float> toScreen (float x, float y) const { const auto r = plot().toFloat(); return { r.getX() + x * r.getWidth(), r.getBottom() - y * r.getHeight() }; }
    void sampleCurve (int c)
    {
        const float lx[5] { 0, .25f, .5f, .75f, 1 };
        for (int k = 0; k < 5; ++k) { px[(size_t) k] = lx[k]; py[(size_t) k] = applyCurve (c, lx[k], lx, lx); }
    }
    void commit()
    {
        const auto l = ui.proc.modr.list();
        for (int i = 0; i < (int) l.size(); ++i) if (l[(size_t) i].id == id) { ui.proc.modr.setCustom (i, px, py); return; }
    }
};

// ================================================================== the ROUTES sub-page
class LfoView : public juce::Component
{
public:
    LfoView (ProProcessor& p, int k) : proc (p), lfo (k) {}
    void paint (juce::Graphics& g) override
    {
        pt::ScopedDark screen;
        auto r = getLocalBounds().toFloat();
        g.setColour (P().deep); g.fillRoundedRectangle (r, 8);
        const auto c = ModUi::srcColour (lfo3 + lfo);
        g.setColour (c.withAlpha (0.08f));
        for (int i = 1; i < 4; ++i) g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * i / 4.0f), r.getX() + 4, r.getRight() - 4);
        const juce::String pre = "l" + juce::String (lfo + 3) + "_";
        const int shape = (int) proc.apvts.getRawParameterValue (pre + "shape")->load();
        const float depth = proc.apvts.getRawParameterValue (pre + "depth")->load();
        auto yOf = [&] (float v) { return r.getCentreY() - v * (r.getHeight() * 0.5f - 7); };
        const float x0 = r.getX() + 6, w = r.getWidth() - 12;
        const juce::uint32 seed = (juce::uint32) (lfo + 2) * 977u;
        juce::Path sp;
        for (int i = 0; i <= 96; ++i) { const float ph = (float) i / 96.0f; const float v = ProProcessor::lfoValue (shape, ph, seed) * depth; if (i == 0) sp.startNewSubPath (x0 + ph * w, yOf (v)); else sp.lineTo (x0 + ph * w, yOf (v)); }
        g.setColour (c.withAlpha (0.18f)); g.strokePath (sp, juce::PathStrokeType (6.0f));
        g.setColour (c); g.strokePath (sp, juce::PathStrokeType (1.8f));
        const float ph = proc.modr.lfoPhaseLive[lfo].load();
        const float v = ProProcessor::lfoValue (shape, ph, seed) * depth;
        const juce::Point<float> d (x0 + ph * w, yOf (v));
        g.setColour (c.withAlpha (0.3f)); g.fillEllipse (juce::Rectangle<float> (14, 14).withCentre (d));
        g.setColour (P().white); g.fillEllipse (juce::Rectangle<float> (7, 7).withCentre (d));
    }
private:
    ProProcessor& proc; const int lfo;
};

class SortHeader : public juce::Component
{
public:
    std::function<void (int)> onSort; int sortKey = 0; bool desc = false;
    struct Col { const char* name; int x, w, key; };
    std::vector<Col> cols;
    void paint (juce::Graphics& g) override
    {
        g.setFont (ghosty::font (9.5f));
        for (auto& c : cols)
        {
            const bool on = c.key != 0 && c.key == sortKey;
            g.setColour (on ? P().slime : P().dim);
            juce::String t (c.name); if (on) t << (desc ? "  v" : "  ^");
            g.drawText (t, juce::Rectangle<int> (c.x, 0, c.w, getHeight()), juce::Justification::centredLeft);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        for (auto& c : cols) if (c.key != 0 && e.x >= c.x && e.x < c.x + c.w) { if (onSort) onSort (c.key); return; }
    }
};

class ModRoutesPage : public juce::Component, private juce::Timer
{
public:
    explicit ModRoutesPage (ModUi& u) : ui (u)
    {
        auto& s = ui.proc.apvts;
        for (int k = 0; k < 2; ++k)
        {
            auto& L = lfo[k]; const juce::String p = "l" + juce::String (k + 3) + "_";
            L.view = std::make_unique<LfoView> (ui.proc, k); addAndMakeVisible (*L.view);
            L.shape = std::make_unique<juce::ComboBox>(); L.shape->addItemList (lfoShapeNames(), 1); addAndMakeVisible (*L.shape); L.ca.push_back (std::make_unique<CA> (s, p + "shape", *L.shape));
            L.div = std::make_unique<juce::ComboBox>(); L.div->addItemList (lfoDivNames(), 1); addAndMakeVisible (*L.div); L.ca.push_back (std::make_unique<CA> (s, p + "div", *L.div));
            L.sync = std::make_unique<juce::TextButton> ("SYNC"); L.sync->setClickingTogglesState (true); addAndMakeVisible (*L.sync); L.ba.push_back (std::make_unique<BA> (s, p + "sync", *L.sync));
            L.trig = std::make_unique<juce::TextButton> ("RETRIG"); L.trig->setClickingTogglesState (true); addAndMakeVisible (*L.trig); L.ba.push_back (std::make_unique<BA> (s, p + "trig", *L.trig));
            L.trig->setTooltip ("On: restarts with every note. Off: one free-running LFO for all voices");
            L.rate = knob (p + "rate", "RATE"); L.fade = knob (p + "fade", "FADE IN"); L.depth = knob (p + "depth", "DEPTH");
        }
        depth1 = knob ("l1_depth", "LFO 1 DEPTH"); depth2 = knob ("l2_depth", "LFO 2 DEPTH");
        for (int src = 1; src < mod::numSrc; ++src) { tiles.push_back (std::make_unique<ModTile> (ui, src, false)); addAndMakeVisible (*tiles.back()); }
        back.setTooltip ("Back to the MOD page"); addAndMakeVisible (back);
        back.onClick = [this] { ui.openRoutes (false); };
        clearAll.setTooltip ("Remove every route of this sound"); addAndMakeVisible (clearAll);
        clearAll.onClick = [this] { ui.proc.modr.clear(); };
        addAndMakeVisible (header);
        header.onSort = [this] (int k) { if (header.sortKey == k) header.desc = ! header.desc; else { header.sortKey = k; header.desc = k == 3; } rebuildRows(); header.repaint(); };
        addAndMakeVisible (view); view.setViewedComponent (&rowsHolder, false); view.setScrollBarsShown (true, false); view.setScrollBarThickness (8);
        startTimerHz (20);
    }
    ~ModRoutesPage() override { rows.clear(); }

    static constexpr int LW = 552, RX = 566;
    void paint (juce::Graphics& g) override
    {
        {   // the window behind the cards (the same gradient the editor paints under every page)
            const auto& h = ghosty::themes::get (P().theme);
            const float y0 = (float) ui.content.getLocalPoint (this, juce::Point<int>()).y;
            juce::ColourGradient wg (h.windowTop, 0, -y0, h.windowBottom, 0, (float) ui.content.getHeight() - y0, false);
            g.setGradientFill (wg); g.fillAll();
        }
        paintCardM (g, { 0, 0, LW, 136 }, "LFO 3", ModUi::srcColour (lfo3));
        paintCardM (g, { 0, 144, LW, 136 }, "LFO 4", ModUi::srcColour (lfo4));
        paintCardM (g, sourcesCard(), "SOURCES   drag a tile onto any knob  (hold it over a tab to switch pages)", P().slime);
        paintCardM (g, routesCard(), "MOD ROUTES   " + juce::String (ui.proc.modr.size()) + " / " + juce::String (MAX_CONN), P().ecto);
        if (ui.proc.modr.size() == 0)
        {
            g.setColour (P().dim); g.setFont (ghosty::font (11.0f));
            g.drawFittedText ("No routes yet.\nDrag a source tile onto any knob, or right-click a knob > Mod Source.\nDrag a ring around a knob to change its amount.",
                              view.getBounds().reduced (20, 40), juce::Justification::centredTop, 4);
        }
    }
    juce::Rectangle<int> sourcesCard() const { return { 0, 288, LW, getHeight() - 288 }; }
    juce::Rectangle<int> routesCard() const { return { RX, 0, getWidth() - RX, getHeight() }; }
    void resized() override
    {
        for (int k = 0; k < 2; ++k)
        {
            auto& L = lfo[k]; const int y = k * 144;
            L.view->setBounds (10, y + 26, 170, 100);
            L.shape->setBounds (190, y + 30, 104, 26); L.div->setBounds (190, y + 64, 104, 26);
            L.sync->setBounds (190, y + 98, 48, 24); L.trig->setBounds (242, y + 98, 52, 24);
            L.rate->setBounds (300, y + 26, 82, 100); L.fade->setBounds (382, y + 26, 82, 100); L.depth->setBounds (464, y + 26, 82, 100);
        }
        const auto sc = sourcesCard();
        const int nCols = 4, tw = (LW - 20 - (nCols - 1) * 6) / nCols;
        for (int i = 0; i < (int) tiles.size(); ++i) tiles[(size_t) i]->setBounds (10 + (i % nCols) * (tw + 6), sc.getY() + 28 + (i / nCols) * 34, tw, 28);
        const int dy = sc.getY() + 28 + 5 * 34 + 2;
        depth1->setBounds (LW - 10 - 2 * 92, dy, 92, sc.getBottom() - dy - 4); depth2->setBounds (LW - 10 - 92, dy, 92, sc.getBottom() - dy - 4);
        const auto rc = routesCard();
        back.setBounds (rc.getRight() - 12 - 88, 3, 88, 18);
        clearAll.setBounds (back.getX() - 6 - 84, 3, 84, 18);
        view.setBounds (rc.getX() + 6, 46, rc.getWidth() - 12, rc.getHeight() - 52);
        header.setBounds (view.getX(), 26, view.getWidth(), 18);
        {
            const int rw = view.getWidth() - view.getScrollBarThickness() - 2, dx = view.getX() - header.getX();
            const auto c = cols (rw);
            header.cols = { { "SOURCE", dx + c.src, 84, 1 }, { "TARGET", dx + c.handle, c.amount - c.handle, 2 }, { "AMOUNT", dx + c.amount, 100, 3 }, { "CURVE", dx + c.curve, 62, 0 },
                            { "AUX", dx + c.aux, 62, 0 }, { "+/-", dx + c.bip, 26, 0 }, { "BYP", dx + c.byp, 34, 0 } };
        }
        layoutRows();
    }

    // ---- one route: the columns, laid out from the right (the target's name takes what is left)
    struct Cols { int src = 0, handle = 88, name = 104, amount, curve, aux, bip, byp, del; };
    static Cols cols (int w)
    {
        Cols c; c.del = w - 22; c.byp = c.del - 4 - 34; c.bip = c.byp - 4 - 26; c.aux = c.bip - 4 - 62; c.curve = c.aux - 4 - 62; c.amount = c.curve - 4 - 100;
        return c;
    }
    struct Row : public juce::Component
    {
        ModRoutesPage& pg; const int id; int src = 0; juce::String dst;
        juce::ComboBox source, curve, aux; juce::Slider amount; juce::TextButton bip { "+/-" }, byp { "BYP" }, del { "X" };
        struct Handle : public juce::Component, public juce::SettableTooltipClient
        {
            Row& row;
            explicit Handle (Row& r) : row (r) { setMouseCursor (juce::MouseCursor::DraggingHandCursor); setTooltip ("Drag onto another knob to move this modulation there"); }
            void paint (juce::Graphics& g) override
            {
                g.setColour (ModUi::srcColour (row.src));
                const float cx = getWidth() * 0.5f, cy = getHeight() * 0.5f;
                for (int i = 0; i < 3; ++i) for (int j = 0; j < 2; ++j) g.fillEllipse (cx + (j - 0.5f) * 4.0f - 1.2f, cy + (i - 1) * 4.0f - 1.2f, 2.4f, 2.4f);
            }
            void mouseDrag (const juce::MouseEvent& e) override
            {
                if (! row.pg.ui.drag().on && e.getDistanceFromDragStart() > 4) row.pg.ui.beginDrag (row.src, row.index(), e.getScreenPosition());
            }
        } handle { *this };
        Row (ModRoutesPage& p, const ModConn& m) : pg (p), id (m.id), src (m.src), dst (m.dst)
        {
            for (int s = 1; s < mod::numSrc; ++s) source.addItem (srcNames()[s], s);
            source.setSelectedId (m.src, juce::dontSendNotification);
            source.onChange = [this] { if (const int i = index(); i >= 0) pg.ui.proc.modr.setSource (i, source.getSelectedId()); };
            for (int c = 0; c < numCurves; ++c) curve.addItem (curveNames()[c], c + 1);
            curve.setSelectedId (m.curve + 1, juce::dontSendNotification);
            curve.onChange = [this]
            {
                const int i = index(); if (i < 0) return;
                const int c = curve.getSelectedId() - 1;
                if (c == cCustom) { pg.ui.proc.modr.setCurve (i, cCustom); if (auto ed = pg.ui.makeCurveEditor (i)) juce::CallOutBox::launchAsynchronously (std::move (ed), curve.getScreenBounds(), nullptr); }
                else pg.ui.proc.modr.setCurve (i, c);
            };
            aux.addItem ("-", 1); for (int s = 1; s < mod::numSrc; ++s) aux.addItem ("x " + srcShortNames()[s], s + 1);
            aux.setSelectedId (m.aux + 1, juce::dontSendNotification);
            aux.onChange = [this] { if (const int i = index(); i >= 0) pg.ui.proc.modr.setAux (i, aux.getSelectedId() - 1); };
            amount.setSliderStyle (juce::Slider::LinearHorizontal); amount.setRange (-1.0, 1.0, 0.0);
            amount.setTextBoxStyle (juce::Slider::TextBoxRight, false, 40, 18);
            amount.textFromValueFunction = [] (double v) { return pct ((float) v); };
            amount.valueFromTextFunction = [] (const juce::String& t) { return (double) t.retainCharacters ("0123456789.-+").getFloatValue() / 100.0; };
            amount.setValue (m.amount, juce::dontSendNotification); amount.setDoubleClickReturnValue (true, 0.0);
            amount.onValueChange = [this] { if (const int i = index(); i >= 0) pg.ui.proc.modr.setAmount (i, (float) amount.getValue()); };
            amount.setTooltip ("Modulation amount (of the knob's whole travel)");
            convention (amount, &pg.ui.content);
            bip.setClickingTogglesState (true); bip.setToggleState (m.bipolar, juce::dontSendNotification); bip.setTooltip ("Bipolar: the modulation swings both ways around the knob");
            bip.onClick = [this] { if (const int i = index(); i >= 0) pg.ui.proc.modr.setBipolar (i, bip.getToggleState()); };
            byp.setClickingTogglesState (true); byp.setToggleState (m.bypass, juce::dontSendNotification); byp.setTooltip ("Bypass this route (kept, not heard)");
            byp.onClick = [this] { if (const int i = index(); i >= 0) pg.ui.proc.modr.setBypass (i, byp.getToggleState()); };
            del.setTooltip ("Remove this route");
            del.onClick = [this] { if (const int i = index(); i >= 0) pg.ui.proc.modr.remove (i); };
            for (juce::Component* c : { (juce::Component*) &source, (juce::Component*) &curve, (juce::Component*) &aux, (juce::Component*) &amount, (juce::Component*) &bip, (juce::Component*) &byp, (juce::Component*) &del, (juce::Component*) &handle })
                addAndMakeVisible (c);
            setTooltip (srcNames()[m.src] + " > " + paramName (pg.ui.proc, m.dst));
        }
        void setTooltip (const juce::String& t) { tip = t; }
        juce::String tip;
        int index() const { const auto l = pg.ui.proc.modr.list(); for (int i = 0; i < (int) l.size(); ++i) if (l[(size_t) i].id == id) return i; return -1; }
        void resized() override
        {
            const auto c = cols (getWidth());
            source.setBounds (c.src, 3, 84, 24); handle.setBounds (c.handle, 3, 14, 24);
            amount.setBounds (c.amount, 3, 100, 24); curve.setBounds (c.curve, 3, 62, 24); aux.setBounds (c.aux, 3, 62, 24);
            bip.setBounds (c.bip, 4, 26, 22); byp.setBounds (c.byp, 4, 34, 22); del.setBounds (c.del, 4, 22, 22);
        }
        void paint (juce::Graphics& g) override
        {
            const auto c = ModUi::srcColour (src);
            g.setColour (c.withAlpha (byp.getToggleState() ? 0.03f : 0.07f)); g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (0, 1), 7);
            g.setColour (byp.getToggleState() ? P().dim : P().white); g.setFont (ghosty::font (10.5f));
            const auto cc = cols (getWidth());
            g.drawText (paramName (pg.ui.proc, dst), juce::Rectangle<int> (cc.name, 0, cc.amount - 4 - cc.name, getHeight()), juce::Justification::centredLeft, true);
            // the route's live value under the amount bar
            const float v = juce::jlimit (-1.0f, 1.0f, index() >= 0 ? pg.ui.proc.modr.liveValue (index()) : 0.0f);
            auto bar = juce::Rectangle<float> ((float) cc.amount + 2.0f, (float) getHeight() - 3.0f, 58.0f, 2.0f);
            const float cx = bar.getCentreX(), w = v * bar.getWidth() * 0.5f;
            g.setColour (c.withAlpha (0.9f)); g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (cx, cx + w), bar.getY(), juce::jmax (cx, cx + w), bar.getBottom()));
        }
    };
    struct RowsHolder : public juce::Component {} rowsHolder;
    std::vector<std::unique_ptr<Row>> rows;
    juce::String structure;
    static constexpr int rowH = 30;

    void rebuildRows()
    {
        auto l = ui.proc.modr.list();
        std::vector<int> order ((size_t) l.size()); for (int i = 0; i < (int) l.size(); ++i) order[(size_t) i] = i;
        const int key = header.sortKey; const bool desc = header.desc;
        auto& P_ = ui.proc;
        std::stable_sort (order.begin(), order.end(), [&] (int a, int b)
        {
            const auto& x = l[(size_t) a]; const auto& y = l[(size_t) b];
            int c = 0;
            if (key == 1) c = x.src < y.src ? -1 : x.src > y.src ? 1 : 0;
            else if (key == 2) c = paramName (P_, x.dst).compareNatural (paramName (P_, y.dst));
            else if (key == 3) c = std::abs (x.amount) < std::abs (y.amount) ? -1 : std::abs (x.amount) > std::abs (y.amount) ? 1 : 0;
            return desc ? c > 0 : c < 0;
        });
        rows.clear();
        for (int i : order) { rows.push_back (std::make_unique<Row> (*this, l[(size_t) i])); rowsHolder.addAndMakeVisible (*rows.back()); }
        layoutRows();
        repaint();
    }
    void layoutRows()
    {
        const int w = view.getWidth() - view.getScrollBarThickness() - 2;
        rowsHolder.setSize (juce::jmax (10, w), juce::jmax (10, (int) rows.size() * rowH));
        for (int i = 0; i < (int) rows.size(); ++i) rows[(size_t) i]->setBounds (0, i * rowH, w, rowH);
    }
    static juce::String structureOf (const std::vector<ModConn>& l)
    {
        juce::String s;
        for (auto& m : l) s << m.id << ":" << m.src << ":" << m.dst << ":" << m.curve << ":" << m.aux << ":" << (int) m.bypass << (int) m.bipolar << ";";
        return s;
    }
    void sync()
    {
        const auto l = ui.proc.modr.list();
        const auto st = structureOf (l);
        if (st != structure) { structure = st; rebuildRows(); return; }
        for (auto& r : rows)
            for (auto& m : l)
                if (m.id == r->id && ! r->amount.isMouseButtonDown() && std::abs (r->amount.getValue() - m.amount) > 1e-6)
                    r->amount.setValue (m.amount, juce::dontSendNotification);
    }
    struct Lfo { std::unique_ptr<LfoView> view; std::unique_ptr<juce::ComboBox> shape, div; std::unique_ptr<juce::TextButton> sync, trig; ghosty::GhostKnob* rate = nullptr, *fade = nullptr, *depth = nullptr;
                 std::vector<std::unique_ptr<CA>> ca; std::vector<std::unique_ptr<BA>> ba; } lfo[2];
    juce::OwnedArray<ghosty::GhostKnob> knobsOwned;
    ghosty::GhostKnob* depth1 = nullptr, *depth2 = nullptr;
    std::vector<std::unique_ptr<ModTile>> tiles;
    juce::TextButton back { "< MOD PAGE" }, clearAll { "CLEAR ALL" };
    SortHeader header;
    juce::Viewport view;
    ModUi& ui;
private:
    ghosty::GhostKnob* knob (const juce::String& id, const juce::String& cap)
    {
        auto* k = knobsOwned.add (new ghosty::GhostKnob (ui.proc.apvts, id, cap));
        addAndMakeVisible (k);
        if (auto* prm = ui.proc.apvts.getParameter (id)) k->slider.setTooltip (prm->getName (60));
        k->slider.getProperties().set ("paramId", id);
        k->slider.setDoubleClickReturnValue (true, (double) ui.proc.apvts.getParameter (id)->convertFrom0to1 (ui.proc.apvts.getParameter (id)->getDefaultValue()));
        convention (k->slider, &ui.content);
        return k;
    }
    void timerCallback() override
    {
        if (! shownIn (this, &ui.content)) return;
        sync();
        for (int k = 0; k < 2; ++k)
        {
            lfo[k].view->repaint();
            const bool s = ui.proc.apvts.getRawParameterValue ("l" + juce::String (k + 3) + "_sync")->load() > 0.5f;
            lfo[k].rate->setAlpha (s ? kGreyedM : 1.0f); lfo[k].rate->setEnabled (! s);
            lfo[k].div->setAlpha (s ? 1.0f : kGreyedM); lfo[k].div->setEnabled (s);
        }
        for (auto& t : tiles) t->repaint();
        for (auto& r : rows) r->repaint();
    }
};

// ================================================================== ModUi
ModUi::ModUi (ProProcessor& p, juce::Component& c, juce::Component& mp, Hooks h) : proc (p), content (c), modPage (mp), hooks (std::move (h))
{
    routesPg = std::make_unique<ModRoutesPage> (*this);
    modPage.addChildComponent (*routesPg);
    routesBtn = std::make_unique<juce::TextButton> ("ROUTES  >");
    routesBtn->setTooltip ("MOD ROUTES: every source as a drag tile, LFO 3 / LFO 4, and the list of what modulates which knob (64 routes)");
    routesBtn->onClick = [this] { openRoutes (true); };
    modPage.addAndMakeVisible (*routesBtn);
    const int hs[] { lfo1, lfo2, env2, env3, env1, mac1, mac2, mac3, mac4 };
    for (int s : hs) { headerTiles.push_back (std::make_unique<ModTile> (*this, s, true, s >= mac1 && s <= mac4 ? "M" + juce::String (s - mac1 + 1) : juce::String())); modPage.addAndMakeVisible (*headerTiles.back()); }
    ov = std::make_unique<ModOverlay> (*this);
    content.addAndMakeVisible (*ov);
    ov->setBounds (content.getLocalBounds());
    content.addComponentListener (this);
    modPage.addComponentListener (this);
    layoutHeaderTiles();
    startTimerHz (30);
}

ModUi::~ModUi()
{
    stopTimer();
    if (dragSt.on) juce::Desktop::getInstance().removeGlobalMouseListener (this);
    content.removeComponentListener (this);
    modPage.removeComponentListener (this);
}

juce::Component* ModUi::overlay() const { return ov.get(); }
std::unique_ptr<juce::Component> ModUi::makeTile (int src, bool small, const juce::String& label) { return std::make_unique<ModTile> (*this, src, small, label); }
void ModUi::knobsChanged() { knobsBuilt = false; knobCache.clear(); if (ov != nullptr) { ov->toFront (false); ov->repaint(); } }
juce::Component* ModUi::routes() const { return routesPg.get(); }
std::vector<juce::Component*> ModUi::tiles() const
{
    std::vector<juce::Component*> v;
    for (auto& t : headerTiles) v.push_back (t.get());
    for (auto& t : routesPg->tiles) v.push_back (t.get());
    return v;
}

void ModUi::componentMovedOrResized (juce::Component& c, bool, bool)
{
    if (&c == &content) ov->setBounds (content.getLocalBounds());
    if (&c == &modPage) layoutHeaderTiles();
}

void ModUi::layoutHeaderTiles()
{
    // the MOD page's cards (ModPage::resized; nothing there moves): left column 552 wide (LFO 1, LFO 2, ENV 2, ENV 3 at y 0 / 144 / 288 / 432),
    // right column from x 566: MOD MATRIX (header at y 0), ENV 1 at y 336, MACROS at y 472. The tiles sit in the free right end of each header row.
    const int LW = 552, RX = 566, W = modPage.getWidth();
    if (W <= 0) return;
    const int tw = 64, th = 17;
    for (int k = 0; k < 4; ++k) headerTiles[(size_t) k]->setBounds (LW - 12 - tw, k * 144 + 3, tw, th);
    headerTiles[4]->setBounds (W - 12 - tw, 336 + 3, tw, th);
    // MACROS: four small tiles right-aligned, never over the card title
    const float titleEnd = (float) RX + 12 + ghosty::textWidth (ghosty::font (10.5f), "MACROS  (named by the sound; also sources in the matrix)") + 10;
    const int mw = juce::jlimit (30, 52, (int) ((W - 12 - titleEnd - 3 * 4) / 4));
    for (int m = 0; m < 4; ++m) headerTiles[(size_t) (5 + m)]->setBounds (W - 12 - (4 - m) * mw - (3 - m) * 4, 472 + 3, mw, th);
    routesBtn->setBounds (W - 12 - 104, 2, 104, 19);
    routesPg->setBounds (modPage.getLocalBounds());
    if (routesPg->isVisible()) routesPg->toFront (false);
}

void ModUi::openRoutes (bool open)
{
    routesPg->setVisible (open);
    if (open) { routesPg->toFront (false); routesPg->sync(); }
    ov->repaint();
}
bool ModUi::routesOpen() const { return routesPg->isVisible(); }

const std::vector<juce::Component::SafePointer<juce::Slider>>& ModUi::knobs() const
{
    if (! knobsBuilt)
    {
        knobsBuilt = true; knobCache.clear();
        std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
        {
            for (auto* k : c.getChildren())
            {
                if (auto* s = dynamic_cast<juce::Slider*> (k); s != nullptr && s->isRotary() && paramIdOf (s).isNotEmpty()) knobCache.push_back (s);
                walk (*k);
            }
        };
        walk (content);
    }
    return knobCache;
}

juce::Slider* ModUi::sliderFor (const juce::String& id) const
{
    juce::Slider* any = nullptr;
    for (auto& sp : knobs()) if (auto* s = sp.getComponent(); s != nullptr && paramIdOf (s) == id) { if (shownIn (s, &content)) return s; if (any == nullptr) any = s; }
    return any;
}

bool ModUi::knobGeom (juce::Slider& s, KnobGeom& g) const
{
    const auto lay = s.getLookAndFeel().getSliderLayout (s);
    const auto b = lay.sliderBounds.toFloat().reduced (4);
    if (b.getWidth() < 4 || b.getHeight() < 4) return false;
    g.rad = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
    g.c = content.getLocalPoint (&s, b.getCentre());
    const auto rp = s.getRotaryParameters();
    g.a0 = rp.startAngleRadians; g.a1 = rp.endAngleRadians;
    g.pos = (float) s.valueToProportionOfLength (s.getValue());
    return true;
}

bool ModUi::knobVisible (juce::Slider& s) const
{
    if (! shownIn (&s, &content)) return false;
    const auto centre = content.getLocalPoint (&s, s.getLookAndFeel().getSliderLayout (s).sliderBounds.getCentre());
    ov->probing = true;
    auto* hit = content.getComponentAt (centre);
    ov->probing = false;
    return hit == &s || s.isParentOf (hit);
}

ModUi::RingHit ModUi::ringAt (juce::Point<float> pos) const
{
    RingHit best;
    const auto list = proc.modr.list();
    if (list.empty()) return best;
    float bestD = 1.0e9f;
    for (auto& sp : knobs())
    {
        auto* s = sp.getComponent();
        if (s == nullptr) continue;
        const auto id = paramIdOf (s);
        std::vector<int> mine; for (int i = 0; i < (int) list.size(); ++i) if (list[(size_t) i].dst == id) mine.push_back (i);
        if (mine.empty()) continue;
        KnobGeom kg; if (! knobGeom (*s, kg)) continue;
        const float d = kg.c.getDistanceFrom (pos);
        const int rings = juce::jmin (3, (int) mine.size());
        if (d < kg.rad - 0.5f || d > ringRadius (kg.rad, rings - 1) + 2.4f) continue;
        if (! knobVisible (*s)) continue;
        for (int r = 0; r < rings; ++r)
        {
            const float dd = std::abs (d - ringRadius (kg.rad, r));
            if (dd < bestD) { bestD = dd; best.slider = s; best.conn = mine[(size_t) r]; best.ring = r; }
        }
    }
    return best;
}

void ModUi::setHoverSource (int src)
{
    if (hoverSrc == src) return;
    hoverSrc = src;
    ov->repaint();
    for (auto& t : headerTiles) t->repaint();
    for (auto& t : routesPg->tiles) t->repaint();
}

// ---------------------------------------------------------------- drags
void ModUi::beginDrag (int src, int moveConn, juce::Point<int> screenPos)
{
    if (dragSt.on) return;
    dragSt = {}; dragSt.on = true; dragSt.src = src; dragSt.moveConn = moveConn;
    juce::Desktop::getInstance().addGlobalMouseListener (this);
    dragMoveTo (screenPos);
}
void ModUi::dragMoveTo (juce::Point<int> screenPos)
{
    if (! dragSt.on) return;
    const auto p = content.getLocalPoint (nullptr, screenPos);
    dragSt.pos = p.toFloat();
    juce::Slider* target = nullptr;
    for (auto* c = content.getComponentAt (p); c != nullptr && c != &content; c = c->getParentComponent())
        if (auto* s = dynamic_cast<juce::Slider*> (c); s != nullptr && proc.modr.isTarget (paramIdOf (s))) { target = s; break; }
    dragSt.target = target;
    // a tab under the dragged tile: switch to its page after a short hold (spring-loaded tabs)
    const int tab = hooks.tabAt ? hooks.tabAt (p) : -1;
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (tab != dragSt.tab) { dragSt.tab = tab; dragSt.tabSince = now; }
    ov->repaint();
}
void ModUi::dragEnd (juce::Point<int> screenPos)
{
    if (! dragSt.on) return;
    dragMoveTo (screenPos);
    juce::Desktop::getInstance().removeGlobalMouseListener (this);
    auto st = dragSt; dragSt = {};
    if (auto* s = st.target.getComponent())
    {
        const auto id = paramIdOf (s);
        if (st.moveConn >= 0) { if (dropMove (st.moveConn, id) && hooks.toast) hooks.toast (srcNames()[st.src] + " moved to " + paramName (proc, id)); }
        else if (dropSource (st.src, id) >= 0 && hooks.toast) hooks.toast (srcNames()[st.src] + " > " + paramName (proc, id) + "   (drag the ring to set the amount)");
    }
    ov->repaint();
}
void ModUi::mouseDrag (const juce::MouseEvent& e) { if (dragSt.on) dragMoveTo (e.getScreenPosition()); }
void ModUi::mouseUp (const juce::MouseEvent& e) { if (dragSt.on) dragEnd (e.getScreenPosition()); }

int ModUi::dropSource (int src, const juce::String& id)
{
    const int before = proc.modr.size();
    const int i = proc.modr.add (src, id, srcIsBipolar (src) ? 0.25f : 0.35f);
    if (i < 0 && before >= MAX_CONN && hooks.toast) hooks.toast ("All 64 routes are in use: remove one on the ROUTES page");
    ov->repaint();
    return i;
}
bool ModUi::dropMove (int conn, const juce::String& id) { const bool ok = proc.modr.move (conn, id); ov->repaint(); return ok; }

void ModUi::timerCallback()
{
    if (dragSt.on && dragSt.tab >= 0 && hooks.currentTab && hooks.showTab && hooks.currentTab() != dragSt.tab
        && juce::Time::getMillisecondCounterHiRes() - dragSt.tabSince > 350.0)
        hooks.showTab (dragSt.tab);
    if (ov->bubble.isNotEmpty() && ov->ringDrag < 0 && ! ov->isMouseOver()) { ov->bubble = {}; ov->repaint(); }
    const auto v = proc.modr.version();
    const bool any = proc.modr.size() > 0;
    if (v != seenVer || any || dragSt.on)
    {
        if (v != seenVer) { seenVer = v; ov->repaint(); for (auto& t : headerTiles) t->repaint(); }
        else if (any)
        {   // the live dots: repaint around the knobs that have rings
            const auto list = proc.modr.list();
            for (auto& sp : knobs())
                if (auto* s = sp.getComponent(); s != nullptr && shownIn (s, &content))
                {
                    const auto id = paramIdOf (s);
                    if (std::none_of (list.begin(), list.end(), [&] (const ModConn& m) { return m.dst == id; })) continue;
                    KnobGeom kg; if (! knobGeom (*s, kg)) continue;
                    const float R = ringRadius (kg.rad, 2) + 6;
                    ov->repaint (juce::Rectangle<float> (2 * R, 2 * R).withCentre (kg.c).getSmallestIntegerContainer());
                }
        }
    }
    if (shownIn (&modPage, &content)) for (auto& t : headerTiles) t->repaint();
}

// ---------------------------------------------------------------- the knob menu
namespace
{
enum MenuIds { idType = 1, idRemoveAll = 2, idReset = 3, idLearn = 4, idForget = 5, idAddSrc = 100, idAux = 1000, idEditCurve = 8000, idBypass = 8100, idRemove = 8200, idBipolar = 8300, idCurve = 9000 };
}

juce::PopupMenu ModUi::buildKnobMenu (const juce::String& id) const
{
    juce::PopupMenu m;
    m.addSectionHeader (paramName (proc, id).toUpperCase());
    m.addItem (idType, "Type a Value...");
    const auto l = proc.modr.list();
    std::vector<int> mine; for (int i = 0; i < (int) l.size(); ++i) if (l[(size_t) i].dst == id) mine.push_back (i);
    if (proc.modr.isTarget (id))
    {
        juce::PopupMenu srcMenu;
        for (int s = 1; s < mod::numSrc; ++s)
        {
            const bool has = std::any_of (mine.begin(), mine.end(), [&] (int i) { return l[(size_t) i].src == s; });
            srcMenu.addItem (idAddSrc + s, srcNames()[s], ! has && (int) l.size() < MAX_CONN, has);
        }
        m.addSubMenu ("Mod Source", srcMenu);
        auto auxMenu = [&] (int i)
        {
            juce::PopupMenu a;
            a.addItem (idAux + i * 100 + 0, "None", true, l[(size_t) i].aux == none);
            for (int s = 1; s < mod::numSrc; ++s) a.addItem (idAux + i * 100 + s, srcNames()[s], s != l[(size_t) i].src, l[(size_t) i].aux == s);
            return a;
        };
        auto curveMenu = [&] (int i)
        {
            juce::PopupMenu c;
            for (int k = 0; k < numCurves; ++k) c.addItem (idCurve + i * 10 + k, curveNames()[k], true, l[(size_t) i].curve == k);
            return c;
        };
        if (mine.size() == 1)
        {
            const int i = mine[0];
            m.addSubMenu ("Aux Source", auxMenu (i));
            m.addSubMenu ("Curve", curveMenu (i));
            m.addItem (idEditCurve + i, "Edit Custom Curve...");
            m.addItem (idBipolar + i, "Bipolar", true, l[(size_t) i].bipolar);
            m.addItem (idBypass + i, "Bypass Modulator", true, l[(size_t) i].bypass);
            m.addItem (idRemove + i, "Remove Modulator");
        }
        else if (mine.size() > 1)
        {
            juce::PopupMenu aux, curve, edit, bip, byp, rem;
            for (int i : mine)
            {
                const auto nm = srcNames()[l[(size_t) i].src];
                aux.addSubMenu (nm, auxMenu (i)); curve.addSubMenu (nm, curveMenu (i));
                edit.addItem (idEditCurve + i, nm); bip.addItem (idBipolar + i, nm, true, l[(size_t) i].bipolar);
                byp.addItem (idBypass + i, nm, true, l[(size_t) i].bypass); rem.addItem (idRemove + i, nm);
            }
            m.addSubMenu ("Aux Source", aux); m.addSubMenu ("Curve", curve); m.addSubMenu ("Edit Custom Curve", edit);
            m.addSubMenu ("Bipolar", bip); m.addSubMenu ("Bypass Modulator", byp); m.addSubMenu ("Remove Modulator", rem);
        }
        m.addItem (idRemoveAll, "Remove All Modulators", ! mine.empty());
    }
    else m.addItem (-1, "(this control can't be modulated)", false);
    m.addSeparator();
    m.addItem (idReset, "Reset Control");
    const int cc = proc.modr.ccFor (id);
    if (proc.modr.learning() == id) m.addItem (idForget, "MIDI Learn: move a controller now... (click to cancel)");
    else if (cc >= 0) { m.addItem (idLearn, "MIDI Learn (now CC " + juce::String (cc) + ")"); m.addItem (idForget, "Forget MIDI CC " + juce::String (cc)); }
    else m.addItem (idLearn, "MIDI Learn");
    return m;
}

void ModUi::runKnobMenu (const juce::String& id, int r, juce::Slider* s)
{
    auto& R = proc.modr;
    if (r <= 0) return;
    if (r == idType) { if (s != nullptr) ghosty::TypeIn::open (*s); }
    else if (r == idRemoveAll) R.removeAllFor (id);
    else if (r == idReset) { if (auto* prm = proc.apvts.getParameter (id)) prm->setValueNotifyingHost (prm->getDefaultValue()); }
    else if (r == idLearn) { R.learn (id); if (hooks.toast) hooks.toast ("MIDI LEARN: move a knob / fader on your controller for " + paramName (proc, id)); }
    else if (r == idForget) R.forget (id);
    else if (r >= idAddSrc && r < idAddSrc + mod::numSrc) dropSource (r - idAddSrc, id);
    else if (r >= idAux && r < idEditCurve) R.setAux ((r - idAux) / 100, (r - idAux) % 100);
    else if (r >= idEditCurve && r < idEditCurve + MAX_CONN)
    {
        const int i = r - idEditCurve;
        R.setCurve (i, cCustom);
        if (auto ed = makeCurveEditor (i))
        {
            if (menuParent != nullptr) { ed->setTopLeftPosition (20, 20); menuParent->addAndMakeVisible (ed.release()); }   // (tests: the caller owns / deletes it)
            else juce::CallOutBox::launchAsynchronously (std::move (ed), s != nullptr ? s->getScreenBounds() : content.getScreenBounds(), nullptr);
        }
    }
    else if (r >= idBypass && r < idBypass + MAX_CONN) { const int i = r - idBypass; R.setBypass (i, ! R.list()[(size_t) i].bypass); }
    else if (r >= idRemove && r < idRemove + MAX_CONN) R.remove (r - idRemove);
    else if (r >= idBipolar && r < idBipolar + MAX_CONN) { const int i = r - idBipolar; R.setBipolar (i, ! R.list()[(size_t) i].bipolar); }
    else if (r >= idCurve && r < idCurve + MAX_CONN * 10) R.setCurve ((r - idCurve) / 10, (r - idCurve) % 10);
    ov->repaint();
    if (onMenuResult) onMenuResult (r);
}

void ModUi::showKnobMenu (juce::Slider& s)
{
    const auto id = paramIdOf (&s);
    auto m = buildKnobMenu (id);
    juce::Component::SafePointer<juce::Slider> safe (&s);
    auto opts = juce::PopupMenu::Options();
    opts = menuParent != nullptr ? opts.withParentComponent (menuParent).withTargetScreenArea (s.getScreenBounds()) : opts.withTargetComponent (&s).withMousePosition();
    m.showMenuAsync (opts, [this, safe, id] (int r) { runKnobMenu (id, r, safe.getComponent()); });
}

bool ModUi::knobRightClick (juce::Slider& s)
{
    const auto id = paramIdOf (&s);
    if (id.isEmpty() || ! s.isRotary() || ! content.isParentOf (&s)) return false;
    showKnobMenu (s);
    return true;
}

std::unique_ptr<juce::Component> ModUi::makeCurveEditor (int i)
{
    const auto l = proc.modr.list();
    if (! juce::isPositiveAndBelow (i, (int) l.size())) return nullptr;
    return std::make_unique<CurveEditor> (*this, l[(size_t) i].id);
}
} // namespace gpro
