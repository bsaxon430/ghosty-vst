// SPDX-License-Identifier: AGPL-3.0-or-later
#include "../effects/ControlUse.h"
#include "../ui/CurveDrawEditor.h"
#include "../ui/CurveShowPanel.h"
#include "../engines/Engines.h"
#include "PluginEditor.h"
#include "../ui/UnitVisual.h"
#include "../ui/pv/Units.h"
#include "../effects/ColourEq.h"

namespace phantom
{
using namespace gpfx::ui;
namespace col = fxtheme::col;
using fxtheme::T; using ghosty::font;
namespace
{
    constexpr int kLookGhost = fxtheme::kLookGhost;
    // the looks the look button cycles: the painted ones (DAWN, CEL, NEON). The four drawn looks (BLACK, PURPLE, LIGHT, GHOST,
    // numbers 0..3) keep their code and their numbers but are not offered (FxTheme.h, oldLooksOffered): firstLook() skips them
    int lookCount() { return fxtheme::kFirstPainted + fxtheme::paintedCount(); }
    int firstLook() { return juce::jmin (fxtheme::firstLook(), lookCount() - 1); }
    bool lookOffered (int l) { return l >= firstLook() && l < lookCount(); }
    int nextLook (int l) { return lookOffered (l + 1) ? l + 1 : firstLook(); }
    // a new instance with no remembered look, and anything that names a look no longer offered, opens in DAWN
    int defaultLook()
    {
        if (auto* r = painted::Registry::instance()) { if (const int d = r->indexOf ("dawn"); d >= 0) return fxtheme::kFirstPainted + d; if (r->count() > 0) return fxtheme::kFirstPainted; }
        return 1;   // (no painted look could be read: PURPLE, as before 0.1.9)
    }
    juce::String lookName (int l)
    {
        if (l >= fxtheme::kFirstPainted) { auto* r = painted::Registry::instance(); return r != nullptr && l - fxtheme::kFirstPainted < r->count() ? r->looks[(size_t) (l - fxtheme::kFirstPainted)]->name : juce::String ("?"); }
        return l == kLookGhost ? juce::String ("GHOST") : ghosty::themes::displayName (ghosty::themes::fromInt (l));
    }
    juce::Colour groupPaint (int g) { static const juce::uint32 c[4] { 0xff6dcc3e, 0xfff2ad36, 0xff49c6df, 0xffe576bc }; return juce::Colour (c[juce::jlimit (0, 3, g == gRhythm ? 0 : g == gColour ? 1 : g == gSpace ? 2 : 3)]); }
    juce::Colour groupColour (int g) { return g == gRhythm ? col::slime : g == gColour ? col::gold : g == gSpace ? col::ice : col::pink; }
    juce::Colour accentOf (int u) { return T (juce::Colour (unitDef (u).accent)); }
    const juce::Identifier kModDrag ("phantom-mod-source");
}

} // namespace phantom
#include "../ui/RhythmEditors.h"
namespace phantom
{
// ================================================================ a knob that can receive a dragged modulator and offers modulation on right-click
struct ModKnob : public Knob, public juce::DragAndDropTarget
{
    PhantomProcessor& proc; int unit, param;
    ModKnob (PhantomProcessor& p, int u, int pi, const juce::String& id, const juce::String& cap, juce::Colour acc) : Knob (p.apvts, id, cap, acc), proc (p), unit (u), param (pi)
    {
        slider.addMouseListener (this, false);
    }
    bool isInterestedInDragSource (const SourceDetails& d) override { return d.description.isInt() && proc.rack.slot (unit).params()[(size_t) param].isContinuous(); }
    void itemDropped (const SourceDetails& d) override { proc.addRoute ({ (int) d.description, unit, param, 0.5f, true }); over = false; repaint(); }
    void itemDragEnter (const SourceDetails&) override { over = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { over = false; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu()) return;
        juce::PopupMenu m; const auto routes = proc.getRoutes(); const bool cont = proc.rack.slot (unit).params()[(size_t) param].isContinuous();
        if (cont)
        {
            juce::PopupMenu mod; for (int s = 0; s < kNumModSources; ++s) mod.addItem (100 + s, juce::String ("Modulate with ") + modSourceName (s));
            m.addSubMenu ("Modulation", mod);
            for (size_t i = 0; i < routes.size(); ++i) if (routes[i].unit == unit && routes[i].param == param) { m.addItem (200 + (int) i, "Remove " + juce::String (modSourceName (routes[i].source)) + " (" + juce::String (juce::roundToInt (routes[i].depth * 100)) + " %)"); m.addItem (300 + (int) i, juce::String (routes[i].bipolar ? "Make unipolar: " : "Make bipolar: ") + modSourceName (routes[i].source)); }
            m.addSeparator();
        }
        addSliderItems (m, true);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, routes] (int r)
        {
            if (r >= 100 && r < 100 + kNumModSources) proc.addRoute ({ r - 100, unit, param, 0.5f, true });
            else if (r >= 200 && r < 300) proc.removeRoute (r - 200);
            else if (r >= 300 && r < 400) { auto v = routes; const int i = r - 300; if (i < (int) v.size()) { v[(size_t) i].bipolar = ! v[(size_t) i].bipolar; proc.setRoutes (v); } }
            else handleSliderItem (r, &slider);
        });
    }
    void paint (juce::Graphics& g) override
    {
        Knob::paint (g);
        int n = 0; for (auto& r : proc.getRoutes()) if (r.unit == unit && r.param == param) ++n;
        if (n > 0) { g.setColour (col::ecto); g.fillEllipse (getWidth() - 9.0f, 2.0f, 7.0f, 7.0f); }   // the modulation dot
        if (over) { g.setColour (col::slime.withAlpha (0.6f)); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1), 6, 1.5f); }
    }
    bool over = false;
};

// one unit's tile art (the strip and the SPATIAL page draw the same tile): glass / card, group bar, the unit's ghost, its short name
static void drawUnitTile (juce::Graphics& g, juce::Rectangle<float> r, int u, bool on, bool sel, double t, ghosty::glass::PanelCache& tileGlass, int lane = 0, juce::Colour laneTint = {})
{
    const auto acc = accentOf (u); const auto& in = unitDef (u); const auto gc = groupColour (in.group);
    if (on) { g.setColour (acc.withAlpha (0.14f)); g.fillRoundedRectangle (r.expanded (2), 10); }
    if (lane > 0) { g.setColour (laneTint.withAlpha (0.10f)); g.fillRoundedRectangle (r.expanded (2), 10); }
    if (ghost()) { tileGlass.draw (g, r, 9.0f, sel ? 0.50f : 0.36f, 4.0f); g.setColour (sel ? ghosty::glass::violetDeep() : acc.withAlpha (on ? 0.55f : 0.22f)); g.drawRoundedRectangle (r.reduced (0.5f), 9, sel ? 1.8f : 1.0f); }
    else
    {
        juce::ColourGradient bg (sel ? T (juce::Colour (0xff232739)) : T (juce::Colour (0xff161925)), 0, r.getY(), T (juce::Colour (0xff0f1118)), 0, r.getBottom(), false);
        g.setGradientFill (bg); g.fillRoundedRectangle (r, 9);
        g.setColour (sel ? acc : acc.withAlpha (on ? 0.45f : 0.15f)); g.drawRoundedRectangle (r, 9, sel ? 1.6f : 1.0f);
    }
    g.setColour (gc.withAlpha (on ? 0.9f : 0.35f)); g.fillRoundedRectangle (r.getX() + 8, r.getBottom() - 4, r.getWidth() - 16, 2.0f, 1.0f);
    const float bob = on ? (float) std::sin (t * 2.6 + u) * 2.0f : 0.0f;
    ghosty::GhostLook L = ghosty::lookFor (in.look);
    if (! on) { L.body = T (juce::Colour (0xff3a3e4f)); L.slime = T (juce::Colour (0xff4a4f63)); L.pupil = T (juce::Colour (0xff4a4f63)); } else L.slime = acc;
    const float gs = juce::jmin (r.getHeight() * 0.42f, r.getWidth() * 0.5f);
    ghosty::drawGhost (g, L, r.getCentreX() - 2, r.getY() + r.getHeight() * 0.40f + bob, gs, 0.0f, false, on ? 1.0f : 0.6f);
    if (ghost()) ghostglass::outline (g, r.getCentreX() - 2, r.getY() + r.getHeight() * 0.40f + bob, gs, on ? 1.0f : 0.5f);
    g.setColour (on ? col::white.get() : col::dim.get()); g.setFont (font (juce::jmin (11.0f, r.getWidth() * 0.15f)));
    g.drawText (in.shortName, r.withTrimmedTop (r.getHeight() * 0.68f).withTrimmedBottom (5), juce::Justification::centred, false);
}

// PAINTED: a unit's tile from the look's sprites: the hanging tag (off / on / selected, underline in the group colour), the unit's
// ghost (awake = on, asleep = off; "pose" = the held bounce frame) and its short name. "small" = the SPATIAL tile (46 x 48).
// "pose" = the ghost's held frame of its drawn pose strip (painted::poseAfter); "dim" 0..9 = how far a lit sign is out (flicker, buzz)
static void drawPaintedTile (juce::Graphics& g, painted::Look& L, juce::Rectangle<float> cell, int u, bool on, bool sel, int pose, bool small, float tiltDeg = 0.0f, float drop = 0.0f, int dimStep = 0, bool hover = false)
{
    const auto& in = unitDef (u); const char* gl = in.group == gRhythm ? "r" : in.group == gColour ? "c" : in.group == gSpace ? "s" : "m";
    juce::Graphics::ScopedSaveState ss (g);
    if (const auto sp = L.str (small ? "spatial.tileSprite" : "rack.tiles.sprite"); sp.isNotEmpty())
    {   // a look whose unit signs are complete pictures (ghost, name, lamp, frame in the art): "pose" > 0 = the sign's dim frame (0..9)
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        if (small)
        {
            const float cw = L.el ("spatial.tileCell", 0, 70), chh = L.el ("spatial.tileCell", 1, 72), k = cell.getWidth() / 46.0f;
            L.draw (g, sp + in.shortName + (sel ? "-selected" : ""), juce::Rectangle<float> (cw * k, chh * k).withCentre (cell.getCentre())); return;
        }
        const float k = cell.getWidth() / 91.0f, ox = L.el ("rack.tiles.spriteAt", 0, 12) * k, oy = L.el ("rack.tiles.spriteAt", 1, 12) * k;
        const juce::String unitSp = sp + in.shortName, offName = unitSp + (sel && L.hasFile (unitSp + "-selected-off") ? "-selected-off" : "-off");
        const juce::String base = on ? unitSp + (sel ? "-selected" : "-on") : offName; const auto& im = L.img (base);
        const juce::Rectangle<float> r (cell.getX() - ox, cell.getY() - oy, im.w() * k, im.h() * k);
        if (const auto gp = L.str ("rack.tiles.softGlow.prefix"); on && gp.isNotEmpty())
        {   // the soft light of a lit sign, under it (one picture per accent, stretched)
            const float gw = L.el ("rack.tiles.softGlow.size", 0, 150) * k, gh = L.el ("rack.tiles.softGlow.size", 1, 130) * k;
            L.draw (g, L.accentSprite (gp, juce::Colour (in.accent)), juce::Rectangle<float> (gw, gh).withCentre (cell.getCentre()), L.num ("rack.tiles.softGlow.alpha", 0.6f));
        }
        const float dim = on ? juce::jlimit (0.0f, 1.0f, (float) dimStep / 9.0f) : 0.0f;
        L.draw (g, base, r); if (dim > 0.001f) L.draw (g, L.hasFile (base + "-dim") ? base + "-dim" : offName, r, dim);   // (flicker / buzz: the lit sign cross-fades to its dim frame when the look ships one, else to its unlit picture)
        if (const auto ps = L.str ("rack.tiles.pose.strip"); on && ps.isNotEmpty() && (bool) L.at ("rack.tiles.pose.onSign"))
        {   // a lit sign's ghost is bent from tube: its drawn pose strip over the sign (an unlit sign keeps its painted sleeping ghost)
            const juce::String key = L.listHas ("rack.tiles.pose.plainUnits", in.shortName) ? "rack.tiles.pose.plain" : "rack.tiles.pose.kana";
            const float sc = L.el (key, 2, 1.0f) * k, cw = L.el ("rack.tiles.pose.cell", 0, 64), chh = L.el ("rack.tiles.pose.cell", 1, 76);
            L.frame (g, ps + in.shortName + "-strip", juce::jlimit (0, painted::kPoseAsleep - 1, pose), painted::kPoseFrames, false, { cell.getX() + L.el (key, 0) * k, cell.getY() + L.el (key, 1) * k, cw * sc, chh * sc }, 1.0f - dim * 0.85f);
        }
        if (hover && L.hasFile ("tile/hover")) L.draw (g, "tile/hover", juce::Rectangle<float> (cell.getX() - ox, cell.getY() - oy, L.img ("tile/hover").w() * k, L.img ("tile/hover").h() * k));   // the pointer is over it
        return;
    }
    if (tiltDeg != 0.0f || drop != 0.0f) g.addTransform (juce::AffineTransform::rotation (juce::degreesToRadians (tiltDeg), cell.getCentreX(), cell.getY()).translated (0.0f, drop));
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    const juce::String n = small ? juce::String (sel ? "tile/spatial-selected" : "tile/spatial") : juce::String ("tile/") + (sel ? "selected-" : on ? "on-" : "off-") + gl;
    const auto& im = L.img (n); const float k = cell.getWidth() / (small ? 46.0f : 91.0f);
    L.draw (g, n, juce::Rectangle<float> (im.w() * k, im.h() * k).withCentre (cell.getCentre()));
    const float gs = (small ? 0.50f : 0.58f) * k; const juce::Point<float> c (cell.getCentreX(), cell.getY() + cell.getHeight() * (small ? 0.36f : 0.37f));
    if (const auto ps = L.str ("rack.tiles.pose.strip"); ps.isNotEmpty() && L.hasFile (ps + in.shortName + "-strip"))
    {   // the unit's ghost in its drawn poses (held frames; asleep = switched off)
        const float cw = L.el ("rack.tiles.pose.cell", 0, 72), chh = L.el ("rack.tiles.pose.cell", 1, 88);
        L.frame (g, ps + in.shortName + "-strip", on ? juce::jlimit (0, painted::kPoseAsleep - 1, pose) : painted::kPoseAsleep, painted::kPoseFrames, false, { c.x - L.el ("rack.tiles.pose.anchor", 0, 35) * gs, c.y - L.el ("rack.tiles.pose.anchor", 1, 42) * gs, cw * gs, chh * gs });
    }
    else
    {   // (a look with one drawing per ghost: its poses are that drawing squashed and stretched in held steps)
        float kx, ky, lift; painted::poseOf (on ? painted::squashOf (pose) : 0, kx, ky, lift);
        const juce::String gn = juce::String ("ghosts/unit-") + in.shortName + (on ? "" : "-off");
        L.draw (g, gn, { c.x - 31.0f * gs * kx, c.y - 32.0f * gs * ky - lift * 30.0f * gs + (1.0f - ky) * 22.0f * gs, 64.0f * gs * kx, 76.0f * gs * ky });
    }
    const auto band = juce::Rectangle<float> (cell.getX() + 4.0f * k, cell.getY() + cell.getHeight() * (small ? 0.64f : 0.61f), cell.getWidth() - 8.0f * k, cell.getHeight() * (small ? 0.24f : 0.25f));
    g.setColour (on ? L.cream() : L.sub().withAlpha (0.78f)); g.setFont (painted::pfont (small ? 7.4f * k : 10.2f * k));
    g.drawFittedText (in.shortName, band.toNearestInt(), juce::Justification::centred, 1, 0.75f);
    if (hover && ! small && L.hasFile ("tile/hover")) { const auto& hv = L.img ("tile/hover"); L.draw (g, "tile/hover", juce::Rectangle<float> (hv.w() * k, hv.h() * k).withCentre (cell.getCentre())); }   // the pointer is over it (same cell as the tag)
}

// ================================================================ the unit strip: one tile per effect, drag to reorder, light = on/off, right-click = lane
class Strip : public juce::Component, private juce::Timer
{
public:
    Strip (PhantomProcessor& p, std::function<void (int)> onSelect) : proc (p), select (std::move (onSelect)) { trig.fill (-100); startTimerHz (30); }
    int selected = uFatten;
    int rows() const { return getWidth() < 1300 ? 2 : 1; }
    int perRow() const { return (kNumUnits + rows() - 1) / rows(); }
    juce::Rectangle<float> tile (int k) const
    {
        if (auto* L = painted::active())
        {   // PAINTED: the look's own rows (LAYOUT.md: CEL 3 x 8 beside the machine, DAWN 2 x 12), the whole cell is the hit area
            const painted::Map m (*this); const int cols = juce::jmax (1, (int) L->num ("rack.tiles.cols", 8));
            return m.f (L->num ("rack.tiles.x", 13) + L->num ("rack.tiles.dx", 97.6f) * (float) (k % cols), L->num ("rack.tiles.y", 122) + L->num ("rack.tiles.dy", 74) * (float) (k / cols), L->num ("rack.tiles.w", 91), L->num ("rack.tiles.h", 66));
        }
        const int pr = perRow(), row = k / pr, c = k % pr; const float w = (float) getWidth() / (float) pr, rh = ((float) getHeight() - 14.0f) / (float) rows(); return { c * w, 14.0f + row * rh, w, rh };
    }
    juce::Rectangle<float> ledRect (int k) const
    {
        if (painted::active() != nullptr) { const auto r = tile (k); const float d = juce::jmax (16.0f, r.getHeight() * 0.30f); return { r.getRight() - d - 2.0f, r.getY() + 1.0f, d, d }; }   // the lamp in the tag's top right corner
        auto r = tile (k).reduced (3.5f, 2.5f); return { r.getRight() - 14, r.getY() + 4, 10, 10 };
    }
    // PAINTED, on the editor's 12 fps tick: each switched-on unit's ghost bounces once when its level peaks (and keeps a slow beat
    // while sound flows); only tiles whose held frame changed are repainted
    void animTick (int tick)
    {
        const auto topo = proc.getTopology();
        for (int k = 0; k < kNumUnits; ++k)
        {
            const int u = topo.order[(size_t) k]; const bool on = unitOn (u); const float lv = proc.rack.slot (u).uiLevel.exchange (0.0f);
            if (on && lv > 0.02f && tick - trig[(size_t) u] > 10 && (lv > slow[(size_t) u] * 1.4f || tick - trig[(size_t) u] > 24)) trig[(size_t) u] = tick;
            slow[(size_t) u] += (lv - slow[(size_t) u]) * 0.3f;
            const int pose = on ? painted::poseAfter (tick - trig[(size_t) u], tick, u) : painted::kPoseAsleep; int dim = 0;
            if (auto* L = painted::active(); L != nullptr && on && u == selected && L->str ("rack.tiles.sprite").isNotEmpty())
                if (const auto buzz = L->str ("anim.tileBuzz"); buzz.isNotEmpty()) dim = 9 - L->tableAt (buzz, tick + u * 5, 53);   // sign looks: the selected sign buzzes by the look's table
            if (pose != poses[(size_t) u] || dim != dims[(size_t) u]) { poses[(size_t) u] = pose; dims[(size_t) u] = dim; const auto dr = tile (k).expanded (14.0f).toNearestInt(); repaint (dr); if (onDirty) onDirty (dr); }
        }
    }
    std::function<void (juce::Rectangle<int>)> onDirty;
    void paintPainted (juce::Graphics& g, painted::Look& L)
    {
        painted::trace ("strip: start");
        const painted::Map m (*this); const float s = m.s(); const auto topo = proc.getTopology();
        const float lx = L.el ("rack.legend", 0, 14), ly = L.el ("rack.legend", 1, 108), step = L.el ("rack.legend", 2, 72);
        for (int gi = 0; gi < 4; ++gi)
        {
            const auto d = m.sq (lx + step * (float) gi + 4.0f, ly, 8.0f, 8.0f); g.setColour (L.ink()); g.fillEllipse (d.expanded (1.2f)); g.setColour (groupPaint (gi)); g.fillEllipse (d);
            L.haloText (g, groupName (gi), m.f (lx + step * (float) gi + 12.0f, ly - 7.0f, step - 14.0f, 14.0f), juce::Justification::centredLeft, 8.6f * s);
        }
        const float hx = lx + step * 4.0f + 6.0f, hr = L.num ("rack.hintRight", 790);
        L.haloText (g, hr - hx > 560.0f ? "signal flows left to right, then down  -  drag tiles to reorder  -  click the light to switch a unit on  -  right-click: lanes" : "signal flows left to right, then down  -  drag tiles to reorder  -  click the light: unit on  -  right-click: lanes", m.f (hx, ly - 7.0f, hr - hx, 14.0f), juce::Justification::centredRight, 8.2f * s);
        const int cols = juce::jmax (1, (int) L.num ("rack.tiles.cols", 8)), rowsN = (kNumUnits + cols - 1) / cols;
        const juce::String cord = L.str ("rack.cord.img");
        if (L.hasImg (cord)) for (int row = 0; row < rowsN; ++row) L.draw (g, cord, m.f (L.num ("rack.cord.x", 18), L.num ("rack.cord.y", 119) + L.num ("rack.cord.dy", 74) * (float) row, L.num ("rack.cord.w", 782), L.img (cord).h()));
        int firstOn = kNumUnits, lastOn = -1;
        for (int k = 0; k < kNumUnits; ++k) if (unitOn (topo.order[(size_t) k])) { firstOn = std::min (firstOn, k); lastOn = k; }
        for (int k = 0; k + 1 < kNumUnits; ++k)
        {   // the signal's way: a small inked chevron between neighbours (lit in the unit's accent while sound passes that way)
            if ((k + 1) % cols == 0) continue;
            const auto a = tile (k), b = tile (k + 1); const bool flow = k >= firstOn && k < lastOn;
            const float x = 0.5f * (a.getRight() + b.getX()), y = a.getY() + a.getHeight() * 0.42f, d = 3.2f * s;
            juce::Path ar; ar.startNewSubPath (x - d * 0.6f, y - d); ar.lineTo (x + d * 0.6f, y); ar.lineTo (x - d * 0.6f, y + d);
            g.setColour (L.halo().withAlpha (0.8f)); g.strokePath (ar, juce::PathStrokeType (3.2f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (flow ? L.ink() : L.ink().withAlpha (0.45f)); g.strokePath (ar, juce::PathStrokeType (1.5f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        painted::trace ("strip: legend, cords, chevrons drawn");
        const float tilt = L.num ("rack.tiles.tilt", 0), dropAmt = L.num ("rack.tiles.drop", 0);
        for (int k = 0; k < kNumUnits; ++k)
        {
            const int u = topo.order[(size_t) k]; const auto r = tile (k); const bool on = unitOn (u), sel = u == selected; const int lane = topo.lane[(size_t) u];
            if (dragging && k == dropIndex) { g.setColour (L.cream()); g.fillRoundedRectangle (r.getX() - 3.0f, r.getY() + 2.0f, 3.0f, r.getHeight() - 4.0f, 1.5f); }
            const float drop = std::sin ((float) k * 1.3f) * dropAmt * s;
            drawPaintedTile (g, L, r, u, on, sel, poses[(size_t) u], false, tilt * std::sin ((float) k * 2.7f), drop, dims[(size_t) u], k == hoverTile && ! dragging);
            const float k1 = r.getWidth() / 91.0f;
            if (lane > 0) { auto badge = juce::Rectangle<float> (r.getX() + 6.0f * k1, r.getY() + 6.0f * k1 + drop, 14.0f * k1, 12.0f * k1); g.setColour (L.ink()); g.fillRoundedRectangle (badge.expanded (1.0f), 3.5f); g.setColour (painted::Look::accentColour (laneColour (lane))); g.fillRoundedRectangle (badge, 3.0f); L.text (g, juce::String::charToString ((juce::juce_wchar) ('A' + lane - 1)), badge, juce::Justification::centred, 8.6f * k1, L.ink()); }
            else if (u == uLim && L.str ("rack.tiles.sprite").isEmpty()) L.text (g, "LAST", { r.getX() + 8.0f * k1, r.getY() + 5.0f * k1 + drop, 40.0f * k1, 10.0f * k1 }, juce::Justification::centredLeft, 7.5f * k1, L.cream());
        }
    }
    bool unitOn (int u) const { return proc.apvts.getRawParameterValue (PhantomProcessor::pid (u, "on"))->load() >= 0.5f; }
    void paint (juce::Graphics& g) override
    {
        if (auto* L = painted::active()) { paintPainted (g, *L); return; }
        const auto topo = proc.getTopology();
        g.setFont (font (9.0f)); int x0 = 0;
        for (int gi = 0; gi < 4; ++gi) { g.setColour (groupColour (gi).withAlpha (0.8f)); g.fillEllipse ((float) x0 + 2, 3, 7, 7); g.setColour (ghost() ? ghosty::glass::ink() : col::dim.get()); g.drawText (groupName (gi), x0 + 12, 0, 70, 13, juce::Justification::centredLeft); x0 += 74; }
        g.setColour (ghost() ? ghosty::glass::ink() : col::dim.withAlpha (0.7f));
        g.drawText (rows() == 2 ? "signal flows left to right, then down  -  drag tiles to reorder  -  click the light to switch a unit on  -  right-click: lanes" : "signal flows left to right  -  drag tiles to reorder  -  click the light to switch a unit on  -  right-click: lanes", getLocalBounds().removeFromTop (13), juce::Justification::centredRight);
        int firstOn = kNumUnits, lastOn = -1;
        for (int k = 0; k < kNumUnits; ++k) if (unitOn (topo.order[(size_t) k])) { firstOn = std::min (firstOn, k); lastOn = k; }
        for (int k = 0; k + 1 < kNumUnits; ++k)
        {
            const auto a = tile (k), b = tile (k + 1); const bool flow = k >= firstOn && k < lastOn;
            g.setColour ((flow ? (unitOn (topo.order[(size_t) k]) ? accentOf (topo.order[(size_t) k]) : col::white.get()).withAlpha (0.85f) : col::white.withAlpha (0.13f)));
            if ((k + 1) % perRow() == 0)
            { juce::Path ar; ar.startNewSubPath (a.getRight() - 6, a.getY() + a.getHeight() * 0.40f); ar.lineTo (a.getRight() - 2, a.getY() + a.getHeight() * 0.40f); ar.lineTo (a.getRight() - 2, a.getBottom() - 2);
              ar.startNewSubPath (b.getX() + 2, b.getY() + 2); ar.lineTo (b.getX() + 2, b.getY() + b.getHeight() * 0.40f); ar.lineTo (b.getX() + 6, b.getY() + b.getHeight() * 0.40f);
              g.strokePath (ar, juce::PathStrokeType (flow ? 1.6f : 1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); continue; }
            const float x = 0.5f * (a.getRight() + b.getX()), y = a.getY() + a.getHeight() * 0.40f;
            juce::Path ar; ar.startNewSubPath (x - 2.2f, y - 4.0f); ar.lineTo (x + 2.0f, y); ar.lineTo (x - 2.2f, y + 4.0f);
            g.strokePath (ar, juce::PathStrokeType (flow ? 1.6f : 1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        for (int k = 0; k < kNumUnits; ++k)
        {
            const int u = topo.order[(size_t) k]; auto r = tile (k).reduced (3.5f, 2.5f);
            if (dragging && k == dropIndex) { g.setColour (col::white.withAlpha (0.6f)); g.fillRect (r.getX() - 3, r.getY(), 2.0f, r.getHeight()); }
            const bool on = unitOn (u), sel = u == selected; const int lane = topo.lane[(size_t) u];
            drawUnitTile (g, r, u, on, sel, t, tileGlass, lane, laneColour (lane)); const auto acc = accentOf (u);
            auto led = ledRect (k);
            if (on) glowDot (g, led.getCentre(), 3.5f, acc); else { g.setColour (T (juce::Colour (0xff2c3040))); g.fillEllipse (led.reduced (1.5f)); }
            if (lane > 0) { auto badge = juce::Rectangle<float> (r.getX() + 3, r.getY() + 3, 12, 11); g.setColour (laneColour (lane).withAlpha (0.85f)); g.fillRoundedRectangle (badge, 3); g.setColour (T (juce::Colour (0xff0b0c10))); g.setFont (font (8.5f)); g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + lane - 1)), badge, juce::Justification::centred, false); }
            if (u == uLim) { g.setColour (col::dim.withAlpha (0.8f)); g.setFont (font (8.0f)); g.drawText ("LAST", r.withTrimmedLeft (4).withHeight (12).translated (0, 3), juce::Justification::centredLeft, false); }
        }
    }
    static juce::Colour laneColour (int l) { static const juce::uint32 c[kLanes] { 0xffF2F4F5, 0xffFF4FD8, 0xff45E6FF, 0xffFFC35A }; return T (juce::Colour (c[juce::jlimit (0, kLanes - 1, l)])); }
    int tileAt (juce::Point<float> p) const { for (int k = 0; k < kNumUnits; ++k) if (tile (k).contains (p)) return k; return -1; }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int k = tileAt (e.position); if (k < 0) return; const auto topo = proc.getTopology(); const int u = topo.order[(size_t) k];
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu m; m.addSectionHeader (unitDef (u).title);
            if (u != uLim) { m.addItem (1, "Main chain", true, topo.lane[(size_t) u] == 0); for (int l = 1; l < kLanes; ++l) m.addItem (1 + l, "Parallel lane " + juce::String::charToString ((juce::juce_wchar) ('A' + l - 1)), true, topo.lane[(size_t) u] == l); m.addSeparator(); }
            m.addItem (10, unitOn (u) ? "Switch off" : "Switch on"); m.addItem (11, "A/B this unit"); m.addItem (12, "Copy this unit's A to B");
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, u] (int r) { if (r >= 1 && r <= kLanes) proc.setUnitLane (u, r - 1); else if (r == 10) toggleOn (u); else if (r == 11) proc.unitAbToggle (u); else if (r == 12) proc.unitAbCopy (u); repaint(); });
            return;
        }
        if (ledRect (k).expanded (3).contains (e.position)) { toggleOn (u); return; }
        pressed = k; dragging = false; selected = u; select (u); repaint();
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override { const int k = tileAt (e.position); if (k >= 0) toggleOn (proc.getTopology().order[(size_t) k]); }
    // PAINTED: the tile under the pointer shows the look's hover picture (only the two tiles concerned are repainted)
    void setHover (int k) { if (k == hoverTile) return; for (int t : { hoverTile, k }) if (t >= 0) repaint (tile (t).expanded (14.0f).toNearestInt()); hoverTile = k; }
    void mouseMove (const juce::MouseEvent& e) override { if (painted::active() != nullptr) setHover (tileAt (e.position)); }
    void mouseExit (const juce::MouseEvent&) override { setHover (-1); }
    int hoverTile = -1;
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (pressed < 0 || pressed == kNumUnits - 1) return;
        if (! dragging && e.getDistanceFromDragStart() > 6) dragging = true;
        if (dragging) { dropIndex = juce::jlimit (0, kNumUnits - 2, tileAt (e.position) < 0 ? dropIndex : tileAt (e.position)); repaint(); }
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging && pressed >= 0) { const auto topo = proc.getTopology(); proc.moveUnit (topo.order[(size_t) pressed], dropIndex); }
        dragging = false; pressed = -1; repaint();
    }
    void toggleOn (int u) { if (auto* p = proc.apvts.getParameter (PhantomProcessor::pid (u, "on"))) { p->beginChangeGesture(); p->setValueNotifyingHost (unitOn (u) ? 0.0f : 1.0f); p->endChangeGesture(); } repaint(); }
private:
    void timerCallback() override
    {
        if (painted::active() != nullptr)
        {   // PAINTED: nothing moves by itself here (the ghosts ride the editor's 12 fps tick): repaint only when the strip's state changed
            const auto topo = proc.getTopology(); juce::uint64 sig = (juce::uint64) (selected + 1) * 131u + (dragging ? (juce::uint64) (dropIndex + 7) * 977u : 0u);
            for (int k = 0; k < kNumUnits; ++k) { const int u = topo.order[(size_t) k]; sig = sig * 1099511628211ull + (juce::uint64) (u * 8 + topo.lane[(size_t) u] * 2 + (unitOn (u) ? 1 : 0)); }
            if (sig != lastSig) { lastSig = sig; repaint(); }
            return;
        }
        t += 1.0 / 30.0; repaint();
    }
    PhantomProcessor& proc; std::function<void (int)> select; double t = 0; int pressed = -1, dropIndex = 0; bool dragging = false; ghosty::glass::PanelCache tileGlass;
    std::array<int, kNumUnits> poses {}, dims {}, trig {}; std::array<float, kNumUnits> slow {}; juce::uint64 lastSig = 0;
};

// ================================================================ a unit's visual (UnitVisual.h): the pane that feeds and draws it. The feed is
// read here on the UI timer: the mapped controls from the parameter tree, the level from one atomic the audio thread writes
// while this pane shows. 24 pictures a second; if a machine paints it slowly it drops to 12, then to redrawing only on a change.
class VisualPane : public juce::Component, private juce::Timer
{
public:
    VisualPane (PhantomProcessor& p, int u, std::unique_ptr<visual::UnitVisual> v) : proc (p), unit (u), vis (std::move (v))
    {
        setName ("UNIT VISUAL"); ids = vis->mapped(); jassert ((int) ids.size() <= visual::kMaxMapped);
        for (auto* id : ids) params.push_back (proc.apvts.getParameter (PhantomProcessor::pid (unit, id)));
        onParam = proc.apvts.getParameter (PhantomProcessor::pid (unit, "on"));
    }
    ~VisualPane() override { proc.rack.slot (unit).visWanted = false; }
    void visibilityChanged() override { const bool v = isVisible(); proc.rack.slot (unit).visWanted = v; if (v) startTimerHz (24); else stopTimer(); }
    std::function<void()> onHide;
    void mouseDown (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) return; juce::PopupMenu m; m.addItem (1, "Hide the unit visuals (the v menu at the top brings them back)"); m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int r) { if (r == 1 && onHide) onHide(); }); }
    visual::Feed feed() const
    {
        visual::Feed f; for (size_t i = 0; i < params.size() && i < f.v.size(); ++i) f.v[i] = params[i] != nullptr ? params[i]->getValue() : 0.0f;
        f.level = level; f.hit = hit; f.t = t; f.on = onParam == nullptr || onParam->getValue() >= 0.5f; return f;
    }
    void advance (double dt)
    {
        const float lv = juce::jlimit (0.0f, 1.0f, proc.rack.slot (unit).visLevel.load (std::memory_order_relaxed));
        if (lv > 0.03f && lv > slow * 1.5f && hit < 0.4f) hit = 1.0f; else hit *= 0.80f; if (hit < 0.01f) hit = 0.0f;
        slow += (lv - slow) * 0.2f; level = lv > level ? lv : level * 0.88f; if (level < 0.001f) level = 0.0f; t += dt;
    }
    void paint (juce::Graphics& g) override
    {
        const double t0 = juce::Time::getMillisecondCounterHiRes(); const auto st = visual::styleFor (unit); auto r = getLocalBounds().toFloat();
        g.setColour (st.back); g.fillRoundedRectangle (r, 7.0f); g.setColour (st.accent.withAlpha (0.35f)); g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.0f);
        { juce::Graphics::ScopedSaveState ss (g); juce::Path clip; clip.addRoundedRectangle (r.reduced (1.5f), 6.0f); g.reduceClipRegion (clip); vis->paint (g, r.reduced (6.0f), feed(), st); }
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0; avgMs += (ms - avgMs) * 0.1;
    }
    double paintMs() const { return avgMs; }
private:
    void timerCallback() override
    {
        advance (1.0 / 24.0); ++frame;
        // a slow machine: half the pictures; a very slow one: only when something changed
        const bool moving = level > 0.002f || hit > 0.0f; auto f = feed(); juce::uint64 sig = 0; for (float v : f.v) sig = sig * 1099511628211ull + (juce::uint64) (v * 4096.0f); sig += f.on ? 1 : 0;
        const bool changed = sig != lastSig; lastSig = sig;
        if (avgMs > 14.0) { if (changed) repaint(); return; }
        if (avgMs > 6.0 && (frame & 1) && ! changed) return;
        if (changed || moving || f.tick() != lastTick) { lastTick = f.tick(); repaint(); }
    }
    PhantomProcessor& proc; int unit; std::unique_ptr<visual::UnitVisual> vis; std::vector<const char*> ids; std::vector<juce::RangedAudioParameter*> params; juce::RangedAudioParameter* onParam = nullptr;
    float level = 0.0f, hit = 0.0f, slow = 0.0f; double t = 0.0, avgMs = 0.0; int frame = 0, lastTick = -1; juce::uint64 lastSig = 0;
};
// the unit visuals' switch: a UI preference like the look (never part of a project), on unless switched off
inline bool& unitVisualsOn() { static bool on = true; return on; }

// ================================================================ a unit's PAINTED visual (src/ui/pv: the designer's scenes in DAWN, CEL, NEON). An opaque
// component exactly on the look's stage rectangle (look.json "visual.stage"): under the scene the smoked glass (NEON), then the
// scene (which lays its own backdrop), then what lies over the stage's edge (DAWN: the unit plate's brass frame; CEL / NEON: the
// frame sprite; the rack panel draws the same picture round it, so the two meet without a seam). The feed is read on the UI
// timer: the controls from the parameter tree (a menu as its index, a switch 0 / 1), the level from the unit's one atomic.
// what a wide-page unit's own editor knows, for its painted visual (the playhead, the pump, the pattern shown); message thread
inline std::function<void (pv::Feed&)> pvExtras (PhantomProcessor& proc, int u)
{
    if (u == uGate)
    {
        struct Cache { juce::uint64 sig = 0; std::vector<float> curve; std::vector<int> steps; };
        auto c = std::make_shared<Cache>();
        return [&proc, c] (pv::Feed& f)
        {
            auto* fx = dynamic_cast<GhostGateFx*> (&proc.rack.slot (uGate).fx()); if (fx == nullptr) return;
            f.pos = fx->uiPos.load (std::memory_order_relaxed); f.pump = fx->uiPump.load (std::memory_order_relaxed);
            const int pat = juce::jlimit (0, gatedata::kPatterns - 1, juce::roundToInt (f.v ("pattern")) - 1); const auto& P = fx->pattern (pat);
            juce::uint64 h = 1469598103934665603ull ^ (juce::uint64) pat; auto mix = [&h] (float v) { juce::uint32 b; std::memcpy (&b, &v, 4); h = (h ^ b) * 1099511628211ull; };
            for (auto& q : P.pts) { mix (q.x); mix (q.level); mix (q.tension); mix ((float) q.type); } for (auto st : P.steps) mix ((float) st);
            if (h != c->sig || c->curve.empty())
            {   // (baked again only when the pattern changes)
                std::array<float, gatedata::kTab + 1> tab {}; gatedata::bake (P, tab.data()); c->curve.assign (128, 0.0f);
                for (int i = 0; i < 128; ++i) c->curve[(size_t) i] = tab[(size_t) ((i * 2 + 1) * gatedata::kTab / 256)];
                c->steps.assign (P.steps.begin(), P.steps.end()); c->sig = h;
            }
            f.curve = c->curve; f.steps = c->steps;
        };
    }
    if (u == uGrid)
        return [&proc] (pv::Feed& f)
        {
            auto* fx = dynamic_cast<GhostGridFx*> (&proc.rack.slot (uGrid).fx()); if (fx == nullptr) return;
            f.pos = fx->uiPos.load (std::memory_order_relaxed); const auto& P = fx->pattern (juce::roundToInt (f.v ("pattern")) - 1);
            f.rows.resize (griddata::kRows); for (int r = 0; r < griddata::kRows; ++r) f.rows[(size_t) r] = (int) P.rows[(size_t) r];
        };
    if (u == uHaunt)
        return [&proc] (pv::Feed& f)
        {
            auto* fx = dynamic_cast<HauntEditFx*> (&proc.rack.slot (uHaunt).fx()); if (fx == nullptr) return;
            const int mouse = fx->uiPad.load (std::memory_order_relaxed), held = fx->uiHeld.load (std::memory_order_relaxed);
            f.held = held >= 0 ? held : mouse; f.env = fx->uiEnv.load (std::memory_order_relaxed); if (held >= 0) f.prog = fx->uiProgress.load (std::memory_order_relaxed);
            f.sel = juce::jlimit (0, hauntdata::kPads - 1, fx->uiSel.load (std::memory_order_relaxed));
            f.pads.resize (hauntdata::kPads); for (int i = 0; i < hauntdata::kPads; ++i) { const auto d = fx->pad (i); f.pads[(size_t) i] = { (float) d.type, (float) d.size, d.motion }; }
        };
    if (u == uWarp)
        return [&proc] (pv::Feed& f)
        {
            auto* fx = dynamic_cast<WarpFx*> (&proc.rack.slot (uWarp).fx()); if (fx == nullptr) return;
            f.pos = fx->uiPos.load (std::memory_order_relaxed); if (f.pos >= 0) f.semis = fx->uiSemis.load (std::memory_order_relaxed);
            const auto& ln = fx->lane(); f.lane.resize (128); for (int i = 0; i < 128; ++i) f.lane[(size_t) i] = ln.valueAt ((i + 0.5) / 128.0);
        };
    if (u == uEq)
        return [&proc] (pv::Feed& f)
        {
            auto* fx = dynamic_cast<SpiritEqFx*> (&proc.rack.slot (uEq).fx()); if (fx == nullptr) return;
            fx->uiCurve (f.curve, f.bands); f.sel = juce::jlimit (0, SpiritEqFx::kBands - 1, fx->uiSel.load (std::memory_order_relaxed));
        };
    return {};
}

class PaintedPane : public juce::Component, private juce::Timer
{
public:
    PaintedPane (PhantomProcessor& p, int u, std::unique_ptr<pv::Scene> sc) : proc (p), unit (u), scene (std::move (sc))
    {
        setName ("UNIT VISUAL"); setOpaque (true); names = scene->controls(); defs = scene->defaults();
        for (auto& n : names) params.push_back (proc.apvts.getParameter (PhantomProcessor::pid (unit, n)));
        onParam = proc.apvts.getParameter (PhantomProcessor::pid (unit, "on"));
    }
    ~PaintedPane() override { proc.rack.slot (unit).visWanted = false; }
    void visibilityChanged() override { const bool v = isVisible(); proc.rack.slot (unit).visWanted = v; if (v) startTimerHz (24); else stopTimer(); }
    std::function<void()> onHide;
    void mouseDown (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) return; juce::PopupMenu m; m.addItem (1, "Hide the unit visuals (the v menu at the top brings them back)"); m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int r) { if (r == 1 && onHide) onHide(); }); }
    // a control's value as the scene reads it
    static float valueOf (juce::RangedAudioParameter* rp, float def)
    {
        if (rp == nullptr) return def;
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (rp)) return (float) c->getIndex();
        if (auto* b = dynamic_cast<juce::AudioParameterBool*> (rp)) return b->get() ? 1.0f : 0.0f;
        if (auto* n = dynamic_cast<juce::AudioParameterInt*> (rp)) return (float) n->get();   // (a count: the scene reads the number itself)
        return rp->getValue();
    }
    pv::Feed feed() const
    {
        pv::Feed f; f.names = &names; for (size_t i = 0; i < names.size() && i < f.vals.size(); ++i) { f.vals[i] = valueOf (params[i], i < defs.size() ? defs[i] : 0.0f); if (dynamic_cast<juce::AudioParameterFloat*> (params[i]) != nullptr) f.vals[i] *= scene->scaleOf (names[i]); }
        f.level = level; f.hit = hit; f.t = t; f.on = onParam == nullptr || onParam->getValue() >= 0.5f; if (extra) extra (f); return f;
    }
    // where it stands: 0 the look's stage (narrow page), 1 the look's wide-page stage, 2 DAWN behind the unit's editor, 3 DAWN's own small pane
    enum Mode { onStage, onWideStage, dawnUnder, dawnOwn };
    void setMode (Mode m) { getProperties().set ("pvMode", (int) m); if (m == mode) return; mode = m; setOpaque (m != dawnUnder); setInterceptsMouseClicks (m != dawnUnder, false); repaint(); }
    Mode getMode() const { return mode; }
    pv::Scene& getScene() { return *scene; }
    std::function<void (pv::Feed&)> extra;   // what the unit's own editor knows (playhead, pattern ...)
    std::function<void()> beforePaint;     // (the rack panel reads the editor's geometry for a DAWN picture)
    // draws one picture of the scene for a feed into g, on the stage rectangle r (local units), at ds device pixels per unit
    static void drawScene (juce::Graphics& g, painted::Look& L, pv::Scene& sc, const pv::Feed& f, pv::State& st, juce::Rectangle<float> r, float ds, std::map<std::string, juce::Image>& cache)
    {
        const auto look = pv::lookFor (L.str ("visual.style")); const float kDev = r.getWidth() * ds / pv::SU_W;
        pv::G G (g, look, &L.visualBank(), kDev, std::round (r.getX() * ds), std::round (r.getY() * ds), ds, &cache);
        juce::Graphics::ScopedSaveState ss (g); g.reduceClipRegion (r.toNearestInt()); sc.scene (G, f, st, pv::Clock::at (f.t));
    }
    void paint (juce::Graphics& g) override
    {
        auto* L = painted::active(); if (L == nullptr) { g.fillAll (fxtheme::pal().window); return; }
        const double t0 = juce::Time::getMillisecondCounterHiRes(); const float ds = g.getInternalContext().getPhysicalPixelScaleFactor() > 1.4f ? 2.0f : 1.0f; L->pixelScale = ds;
        const painted::Map m (*this); const auto r = getLocalBounds().toFloat();
        if (cacheLook != L) { cache.clear(); cacheLook = L; }
        if (mode == dawnUnder || mode == dawnOwn)
        {
            if (beforePaint) beforePaint();   // DAWN's wide pages: the unit's second picture, 1 su = 1 design px
            if (mode == dawnOwn) g.fillAll (fxtheme::pal().window);
            const auto dr = scene->dawnRect(); const float kDev = (float) getWidth() * ds / juce::jmax (1.0f, dr.getWidth());
            pv::G G (g, pv::Look::dawn, &L->visualBank(), kDev, 0, 0, ds, &cache); juce::Graphics::ScopedSaveState ss (g); g.reduceClipRegion (getLocalBounds());
            const auto f = feed(); scene->dawnScene (G, f, state, pv::Clock::at (f.t), dr.getWidth(), dr.getHeight());
        }
        else
        {
            if ((bool) L->at ("visual.glass") && L->glassBlur.isValid()) g.drawImage (L->glassBlur, juce::Rectangle<float> (-m.o.x, -m.o.y, m.sx * painted::kW, m.sy * painted::kH));
            else g.fillAll (fxtheme::pal().window);
            drawScene (g, *L, *scene, feed(), state, r, ds, cache);
            drawOver (g, *L, m, mode == onWideStage);
        }
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0; avgMs += (ms - avgMs) * 0.1;
    }
    // what lies over the stage's edge (the parent draws the same picture all round)
    static void drawOver (juce::Graphics& g, painted::Look& L, const painted::Map& m, bool wide = false)
    {
        if (wide)
        {   // a wide page: the wide frame, then what lies over the slab's top edge (CEL: the robot's hands hold the box)
            if (const auto fr = L.str ("visual.wide.frame.img"); fr.isNotEmpty()) L.draw (g, fr, m.f (L.el ("visual.wide.frame.at", 0), L.el ("visual.wide.frame.at", 1), L.el ("visual.wide.frame.size", 0), L.el ("visual.wide.frame.size", 1)));
            if (const auto wh = L.str ("rack.wideHands.img"); L.hasFile (wh)) L.draw (g, wh, m.f (L.el ("rack.wideHands.at", 0), L.el ("rack.wideHands.at", 1), L.img (wh).w(), L.img (wh).h()));
            return;
        }
        if (const auto fr = L.str ("visual.frame.img"); fr.isNotEmpty()) L.draw (g, fr, m.f (L.el ("visual.frame.at", 0), L.el ("visual.frame.at", 1), L.el ("visual.frame.size", 0), L.el ("visual.frame.size", 1)));
        else if (const auto n = L.str ("rack.plate.img"); n.isNotEmpty()) L.draw (g, n, m.f (L.el ("rack.plate.at", 0), L.el ("rack.plate.at", 1), L.img (n).w(), L.img (n).h()));
    }
    double paintMs() const { return avgMs; }
    int unit;
private:
    void timerCallback() override
    {
        if (! isShowing()) return;
        const float lv = juce::jlimit (0.0f, 1.0f, proc.rack.slot (unit).visLevel.load (std::memory_order_relaxed));
        if (lv > 0.03f && lv > slow * 1.5f && hit < 0.4f) hit = 1.0f; else hit *= 0.80f; if (hit < 0.01f) hit = 0.0f;
        slow += (lv - slow) * 0.2f; level = lv > level ? lv : level * 0.88f; if (level < 0.001f) level = 0.0f; t += 1.0 / 24.0; ++frame;
        const auto f = feed(); state.step (f, pv::Clock::at (t));
        juce::uint64 sig = 0; for (size_t i = 0; i < names.size(); ++i) sig = sig * 1099511628211ull + (juce::uint64) (f.vals[i] * 4096.0f); sig += f.on ? 1 : 0;
        const bool changed = sig != lastSig; lastSig = sig;
        // a slow machine: half the pictures; a very slow one: only when something changed
        if (avgMs > 14.0) { if (changed) repaint(); return; }
        if (avgMs > 6.0 && (frame & 1) && ! changed) return;
        repaint();
    }
    PhantomProcessor& proc; std::unique_ptr<pv::Scene> scene; std::vector<std::string> names; std::vector<float> defs; std::vector<juce::RangedAudioParameter*> params; juce::RangedAudioParameter* onParam = nullptr;
    pv::State state; std::map<std::string, juce::Image> cache; const painted::Look* cacheLook = nullptr; Mode mode = onStage;
    float level = 0.0f, hit = 0.0f, slow = 0.0f; double t = 0.0, avgMs = 0.0; int frame = 0; juce::uint64 lastSig = 0;
};

// ================================================================ a unit's gain-reduction meter (GHOST LIMITER): reads one atomic value the audio
// thread writes; fast up, slow down, with a held peak; repaints itself 24 times a second while it shows
class ReductionMeter : public juce::Component, private juce::Timer
{
public:
    ReductionMeter (const std::atomic<float>& v, std::function<bool()> unitIsOn, juce::Colour accent) : value (v), isOn (std::move (unitIsOn)), acc (accent) { setName ("GR METER"); setInterceptsMouseClicks (false, false); startTimerHz (24); }
    static constexpr float kRange = 24.0f;   // dB shown
    float shownDb() const { return shown; }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat(); auto* L = painted::active(); const float h = r.getHeight(), fh = juce::jlimit (9.0f, 11.5f, h * 0.5f);
        const auto back = L != nullptr ? L->ink().withAlpha (0.86f) : T (juce::Colour (0xff0c0d13)), ink = L != nullptr ? L->cream() : col::white.get(), dim = ink.withAlpha (0.55f);
        const auto fill = L != nullptr ? painted::Look::accentColour (acc) : acc;
        g.setColour (back); g.fillRoundedRectangle (r, 5.0f);
        auto setF = [&] { if (L != nullptr) g.setFont (painted::pfont (fh)); else g.setFont (font (fh + 1.0f)); };
        auto cap = r.removeFromLeft (juce::jmin (128.0f, r.getWidth() * 0.26f)).reduced (8.0f, 0.0f), num = r.removeFromRight (juce::jmin (70.0f, r.getWidth() * 0.2f)).reduced (6.0f, 0.0f);
        setF(); g.setColour (ink); g.drawFittedText ("GAIN REDUCTION", cap.toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
        g.drawFittedText (shown < 0.05f ? juce::String ("0.0 dB") : "-" + juce::String (shown, 1) + " dB", num.toNearestInt(), juce::Justification::centredRight, 1, 0.7f);
        auto bar = r.reduced (2.0f, h * 0.30f); g.setColour (dim.withAlpha (0.22f)); g.fillRoundedRectangle (bar, 2.0f);
        for (float db : { 3.0f, 6.0f, 12.0f, 18.0f }) { g.setColour (dim); g.fillRect (bar.getX() + bar.getWidth() * db / kRange - 0.5f, bar.getBottom() + 1.0f, 1.0f, 3.0f); }
        g.setColour (fill); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, shown / kRange)), 2.0f);
        if (held > 0.1f) { g.setColour (ink); g.fillRect (bar.getX() + bar.getWidth() * juce::jlimit (0.0f, 1.0f, held / kRange) - 1.0f, bar.getY() - 1.0f, 2.0f, bar.getHeight() + 2.0f); }
    }
    void step()
    {
        const float v = isOn() ? juce::jlimit (0.0f, 60.0f, value.load (std::memory_order_relaxed)) : 0.0f;
        const float s0 = shown, h0 = held; shown = v > shown ? v : shown + (v - shown) * 0.22f; if (shown < 0.02f) shown = 0.0f;
        if (shown >= held) { held = shown; holdTicks = 24; } else if (--holdTicks < 0) held = juce::jmax (shown, held - 0.5f);
        if (std::abs (shown - s0) > 0.02f || std::abs (held - h0) > 0.02f) repaint();
    }
private:
    void timerCallback() override { if (isShowing()) step(); }
    const std::atomic<float>& value; std::function<bool()> isOn; juce::Colour acc; float shown = 0.0f, held = 0.0f; int holdTicks = 0;
};

// ================================================================ a unit's spectrum display (SPIRIT EQ): the audio thread only copies its output
// into a FIFO while this shows; the analysis (2048-point FFT, Hann window, fast up / slow down) runs here, on the UI timer
class SpectrumView : public juce::Component, private juce::Timer
{
public:
    SpectrumView (PhantomProcessor& p, int u, AnalyserTap& t, juce::Colour accent) : proc (p), unit (u), tap (t), acc (accent) { setName ("SPECTRUM VIEW"); setInterceptsMouseClicks (false, false); level.fill (-100.0f); }
    ~SpectrumView() override { tap.wanted = false; }
    void visibilityChanged() override { const bool v = isVisible(); tap.wanted = v; if (v) startTimerHz (24); else { stopTimer(); level.fill (-100.0f); } }
    static constexpr int kOrder = 11, kN = 1 << kOrder, kBins = 96;
    // feed samples and run one analysis (also what the checks call)
    void analyse()
    {
        float in[2048]; int n; while ((n = tap.pull (in, 2048)) > 0) { for (int i = 0; i < n; ++i) { ring[(size_t) wpos] = in[i]; wpos = (wpos + 1) % kN; } }
        for (int i = 0; i < kN; ++i) work[(size_t) i] = ring[(size_t) ((wpos + i) % kN)] * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) kN));
        std::fill (work.begin() + kN, work.end(), 0.0f); fft.performFrequencyOnlyForwardTransform (work.data());
        const double sr = tap.sampleRate.load();
        for (int b = 0; b < kBins; ++b)
        {   // log-spaced points from 20 Hz to 20 kHz: the loudest FFT bin each one spans
            const double f0 = 20.0 * std::pow (1000.0, (double) b / kBins), f1 = 20.0 * std::pow (1000.0, (double) (b + 1) / kBins);
            const int k0 = juce::jlimit (1, kN / 2 - 1, (int) std::floor (f0 * kN / sr)), k1 = juce::jlimit (k0, kN / 2 - 1, (int) std::ceil (f1 * kN / sr));
            float m = 0.0f; for (int k = k0; k <= k1; ++k) m = juce::jmax (m, work[(size_t) k]);
            const float db = juce::Decibels::gainToDecibels (m * 4.0f / (float) kN, -100.0f);   // (a full-scale sine reads 0 dB)
            level[(size_t) b] = db > level[(size_t) b] ? db : level[(size_t) b] + (db - level[(size_t) b]) * 0.18f;
        }
    }
    float levelAtHz (double hz) const { return level[(size_t) juce::jlimit (0, kBins - 1, (int) (std::log (hz / 20.0) / std::log (1000.0) * kBins))]; }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat(); auto* L = painted::active();
        const auto back = L != nullptr ? L->ink().withAlpha (0.88f) : T (juce::Colour (0xff0c0d13)), ink = L != nullptr ? L->cream() : col::white.get(), fill = L != nullptr ? painted::Look::accentColour (acc) : acc;
        g.setColour (back); g.fillRoundedRectangle (r, 6.0f); auto a = r.reduced (8.0f, 6.0f); a.removeFromBottom (12.0f);
        auto xOf = [&] (double hz) { return a.getX() + a.getWidth() * (float) (std::log (hz / 20.0) / std::log (1000.0)); };
        auto yOf = [&] (float db) { return a.getY() + a.getHeight() * juce::jlimit (0.0f, 1.0f, (0.0f - db) / 84.0f); };   // 0 .. -84 dB
        if (L != nullptr) g.setFont (painted::pfont (8.6f)); else g.setFont (font (9.0f));
        for (auto [hz, name] : { std::pair<double, const char*> { 50, "50" }, { 100, "100" }, { 200, "200" }, { 500, "500" }, { 1000, "1k" }, { 2000, "2k" }, { 5000, "5k" }, { 10000, "10k" } })
        { g.setColour (ink.withAlpha (0.14f)); g.fillRect (xOf (hz), a.getY(), 1.0f, a.getHeight()); g.setColour (ink.withAlpha (0.6f)); g.drawText (name, juce::Rectangle<float> (xOf (hz) - 20.0f, a.getBottom() + 1.0f, 40.0f, 11.0f), juce::Justification::centred, false); }
        for (float db : { -24.0f, -48.0f, -72.0f }) { g.setColour (ink.withAlpha (0.10f)); g.fillRect (a.getX(), yOf (db), a.getWidth(), 1.0f); }
        juce::Path pth; pth.startNewSubPath (a.getX(), a.getBottom());
        for (int b = 0; b < kBins; ++b) pth.lineTo (a.getX() + a.getWidth() * ((float) b + 0.5f) / (float) kBins, yOf (level[(size_t) b]));
        pth.lineTo (a.getRight(), a.getBottom()); pth.closeSubPath();
        g.setColour (fill.withAlpha (0.30f)); g.fillPath (pth); g.setColour (fill); g.strokePath (pth, juce::PathStrokeType (1.4f));
        // the bands that are on: a dot at each one's frequency and gain (0 dB on the middle line)
        const float mid = a.getCentreY(); g.setColour (ink.withAlpha (0.22f)); g.fillRect (a.getX(), mid, a.getWidth(), 1.0f);
        for (int b = 1; b <= 24; ++b)
        {
            const auto pre = PhantomProcessor::pid (unit, std::to_string (b) + "_"); auto raw = [&] (const char* k) { auto* v = proc.apvts.getRawParameterValue (pre + k); return v != nullptr ? v->load() : 0.0f; };
            if (raw ("on") < 0.5f) continue; const float x = xOf (juce::jlimit (20.0f, 20000.0f, raw ("freq"))), y = mid - (a.getHeight() * 0.5f) * juce::jlimit (-1.0f, 1.0f, raw ("gain") / 30.0f);
            g.setColour (back); g.fillEllipse (x - 5.0f, y - 5.0f, 10.0f, 10.0f); g.setColour (raw ("byp") >= 0.5f ? ink.withAlpha (0.4f) : ink); g.drawEllipse (x - 4.0f, y - 4.0f, 8.0f, 8.0f, 1.5f);
        }
    }
private:
    void timerCallback() override
    {
        analyse(); repaint();
        int best = 0; for (int b = 1; b < kBins; ++b) if (level[(size_t) b] > level[(size_t) best]) best = b;   // (what it reads, for the checks)
        getProperties().set ("peakHz", 20.0 * std::pow (1000.0, ((double) best + 0.5) / kBins)); getProperties().set ("peakDb", level[(size_t) best]);
    }
    PhantomProcessor& proc; int unit; AnalyserTap& tap; juce::Colour acc; juce::dsp::FFT fft { kOrder };
    std::array<float, kN> ring {}; std::array<float, 2 * kN> work {}; std::array<float, kBins> level {}; int wpos = 0;
};

// ================================================================ the unit page: the selected unit's controls, generated from its declared parameters
class UnitPage : public juce::Component, private juce::Timer
{
public:
    UnitPage (PhantomProcessor& p) : proc (p) { startTimerHz (8); }
    ~UnitPage() override { stopTimer(); }
    // a control that does nothing at the unit's current settings is greyed out and says why (effects/ControlUse.h); it stays movable
    void timerCallback() override
    {
        if (! isShowing() || unit < 0) return;
        const int u = unit;
        const ControlValue val = [this, u] (const std::string& id) { auto* a = proc.apvts.getRawParameterValue (PhantomProcessor::pid (u, id)); return a != nullptr ? a->load() : 0.0f; };
        for (auto& [sp, specId] : controlIds)
        {
            juce::Component* c = sp.getComponent(); if (c == nullptr) continue;
            const auto why = controlIdleReason (u, specId, val); const bool idle = ! why.empty();
            if ((c->getAlpha() < 0.99f) != idle) c->setAlpha (idle ? 0.32f : 1.0f);
            const juce::String tip = idle ? juce::String ("GREYED OUT: ") + why : juce::String();
            if (auto* k = dynamic_cast<Knob*> (c)) { if (k->slider.getTooltip() != tip) k->slider.setTooltip (tip); }
            else if (auto* t = dynamic_cast<juce::SettableTooltipClient*> (c)) { if (idle || t->getTooltip().startsWith ("GREYED OUT")) t->setTooltip (tip); }
        }
    }
    void show (int u)
    {
        controlIds.clear(); unit = u; knobs.clear(); choices.clear(); toggles.clear(); labelsFor.clear(); engineBox.reset();
        rhythmPanel = makeRhythmPanel (proc, u); if (rhythmPanel) addAndMakeVisible (*rhythmPanel);
        pane.reset(); if (auto v = visual::make (u)) { pane = std::make_unique<VisualPane> (proc, u, std::move (v)); pane->onHide = [this] { if (onVisualsOff) onVisualsOff(); }; addChildComponent (*pane); }
        spectrum.reset(); if (auto* tp = proc.rack.slot (u).fx().analyserTap())
        {   // a SPECTRUM switch beside the band menu: it shows the unit's output spectrum where the dials are (a view, never saved)
            spectrum = std::make_unique<SpectrumView> (proc, u, *tp, accentOf (u)); addChildComponent (*spectrum);
            if (! spectrumBtn) { spectrumBtn = std::make_unique<juce::TextButton> ("SPECTRUM"); spectrumBtn->setClickingTogglesState (true); spectrumBtn->setTooltip ("Show the unit's output spectrum in place of the dials"); spectrumBtn->onClick = [this] { resized(); repaint(); }; }
            spectrumBtn->setColour (juce::TextButton::buttonOnColourId, accentOf (u)); addAndMakeVisible (*spectrumBtn);
        }
        else if (spectrumBtn) { spectrumBtn->setToggleState (false, juce::dontSendNotification); removeChildComponent (spectrumBtn.get()); }
        meter.reset(); if (auto* mv = proc.rack.slot (u).fx().reductionMeter()) { meter = std::make_unique<ReductionMeter> (*mv, [this, u] { return proc.apvts.getRawParameterValue (PhantomProcessor::pid (u, "on"))->load() >= 0.5f; }, accentOf (u)); addAndMakeVisible (*meter); }
        const auto& specs = proc.rack.slot (u).params(); const auto acc = accentOf (u);
        auto wants = [&] (const ParamSpec& s) { if (u != uEq) return true; const auto id = juce::String (s.id); if (! id.containsChar ('_')) return true; return id.startsWith (juce::String (eqBand + 1) + "_"); };
        for (size_t i = 0; i < specs.size(); ++i)
        {
            const auto& s = specs[i]; if (! wants (s)) continue; const auto id = PhantomProcessor::pid (u, s.id);
            if (u == uHaunt && (s.id == "ptype" || s.id == "psize" || s.id == "pad")) continue;
            if (u == uGrid || (u == uGate && s.id == "pattern")) continue;   // the drawn panel has these (pattern pills, row controls, steps, seed)   // the pad editor sets each pad's type and size
            if (s.type == ParamSpec::Type::Choice && s.id == "engine") { engineBox = std::make_unique<Choice> (proc.apvts, id, acc); engineBox->setTooltip ("ENGINE: this effect's own engine (PHANTOM) or another one, driven by the same controls"); addAndMakeVisible (*engineBox); continue; }   // (its own row at the top: DECISIONS.md 85)
            if (s.type == ParamSpec::Type::Choice) { auto* c = choices.add (new Choice (proc.apvts, id, acc)); addAndMakeVisible (c); labelsFor[c] = s.name; controlIds.push_back ({ c, s.id }); }
            else if (s.type == ParamSpec::Type::Bool) { auto* tg = toggles.add (new Toggle (proc.apvts, id, s.name, acc)); addAndMakeVisible (tg); controlIds.push_back ({ tg, s.id }); }
            else { auto* k = knobs.add (new ModKnob (proc, u, (int) i, id, juce::String (s.name).toUpperCase(), acc)); addAndMakeVisible (k); controlIds.push_back ({ k, s.id }); }
        }
        int curveSet = -1;
        if (engineBox)
        {   // a change of engine re-forms the page (the DRAW engine brings its curve editor); after the menu has closed
            engineBox->onChange = [sp = juce::Component::SafePointer<UnitPage> (this), u] { juce::MessageManager::callAsync ([sp, u] { if (sp != nullptr && sp->unit == u) { sp->show (u); if (auto* par = sp->getParentComponent()) par->resized(); } }); };
            const auto labels = engines::engineLabels (u); const int e = engineBox->getSelectedId() - 1;
            if (e >= 0 && e < (int) labels.size() && labels[(size_t) e] == "DRAW" && rhythmPanel == nullptr)
            { rhythmPanel = std::make_unique<CurveDrawEditor> (proc, u, acc); addAndMakeVisible (*rhythmPanel); }
            // SHOW CURVE beside the menu when a CURVES set is picked (DECISIONS.md 90): it opens the set's curves in a floating panel
            int firstSet = -1; for (int k = 0; k < (int) labels.size(); ++k) if (labels[(size_t) k].rfind ("CURVES", 0) == 0) { firstSet = k; break; }
            const int set = (firstSet >= 0 && e >= firstSet && e < (int) labels.size() && labels[(size_t) e].rfind ("CURVES", 0) == 0) ? e - firstSet : -1;
            curveSet = set;
            if (set >= 0)
            {
                if (! showCurveBtn) { showCurveBtn = std::make_unique<juce::TextButton> ("SHOW CURVE"); showCurveBtn->setClickingTogglesState (true); showCurveBtn->setTooltip ("Show this set's curves (and where DRIVE has got to) in a window you can move and resize"); }
                showCurveBtn->setColour (juce::TextButton::buttonOnColourId, acc); addAndMakeVisible (*showCurveBtn);
                if (curvePanel != nullptr && curvePanelUnit != u) { curveBounds = {}; curvePanel->close(); curvePanel.reset(); }
                if (curvePanel == nullptr) { curvePanel = std::make_unique<CurveShowPanel> (proc, u, set, acc); curvePanelUnit = u; curvePanel->followPage (this);
                                             curvePanel->onClose = [this] { if (showCurveBtn) showCurveBtn->setToggleState (false, juce::dontSendNotification); if (curvePanel) curveBounds = curvePanel->getBounds(); }; }
                else curvePanel->setSet (set);
                showCurveBtn->onClick = [this]
                {
                    if (curvePanel == nullptr) return;
                    if (showCurveBtn->getToggleState()) { juce::Component* top = findParentComponentOfClass<juce::AudioProcessorEditor>(); if (top == nullptr) top = getTopLevelComponent(); if (top != nullptr) curvePanel->placeIn (*top, curveBounds); }
                    else { curveBounds = curvePanel->getBounds(); curvePanel->close(); }
                };
                showCurveBtn->setToggleState (curvePanel->isOpen(), juce::dontSendNotification);
            }
        }
        if (curveSet < 0)
        {   // not a CURVES set here (any more): no button, no panel
            if (curvePanel) { if (curvePanel->isOpen()) curveBounds = curvePanel->getBounds(); curvePanel->close(); }
            if (showCurveBtn) { showCurveBtn->setToggleState (false, juce::dontSendNotification); removeChildComponent (showCurveBtn.get()); }
            if (curvePanelUnit != u) { curvePanel.reset(); curvePanelUnit = -1; curveBounds = {}; }
        }
        if (u == uEq)
        {
            if (! bandBox) { bandBox = std::make_unique<juce::ComboBox>(); for (int b = 1; b <= 24; ++b) bandBox->addItem ("BAND " + juce::String (b), b); bandBox->onChange = [this] { eqBand = bandBox->getSelectedId() - 1; if (auto* q = dynamic_cast<SpiritEqFx*> (&proc.rack.slot (uEq).fx())) q->uiSel.store (eqBand, std::memory_order_relaxed); show (uEq); }; }
            bandBox->setSelectedId (eqBand + 1, juce::dontSendNotification); addAndMakeVisible (*bandBox);
        }
        else if (bandBox) removeChildComponent (bandBox.get());
        resized(); repaint(); timerCallback();
    }
    void paint (juce::Graphics& g) override
    {
        if (auto* L = painted::active())
        {   // PAINTED: a menu's name above it: on the slab in the slab's ink, straight on the painting with the look's halo
            const float fh = 9.0f * painted::Map (*this).s();
            if (pods)
            {   // a look with smoked glass: one glass pod behind each dial and one strip behind the switches (the street shows between them)
                const auto gl = L->str ("slabs.editor"); const float s = painted::Map (*this).s();
                for (auto* k : knobs) L->panel (g, *this, gl, k->getBounds().toFloat().withSizeKeepingCentre (juce::jmin ((float) k->getWidth() - 4.0f, (float) k->getHeight() * 0.86f), (float) k->getHeight() + 8.0f * s));
                juce::Rectangle<int> sw; for (auto* c : choices) sw = sw.isEmpty() ? c->getBounds() : sw.getUnion (c->getBounds()); for (auto* tg : toggles) sw = sw.isEmpty() ? tg->getBounds() : sw.getUnion (tg->getBounds());
                if (! sw.isEmpty()) L->panel (g, *this, gl, sw.toFloat().expanded (6.0f * s, 5.0f * s).withTrimmedTop (-13.0f * s).getIntersection (getLocalBounds().toFloat().expanded (0.0f, 4.0f)));
            }
            for (auto* c : choices) { const auto r = c->getBounds().translated (2, -(int) (fh + 4.0f)).withHeight ((int) fh + 3).toFloat(); if (onSlab) L->text (g, labelsFor[c], r, juce::Justification::centredLeft, fh, fxtheme::pal().ink); else L->haloText (g, labelsFor[c], r, juce::Justification::centredLeft, fh); }
            if (engineBox) { const auto r = engineLabel.toFloat(); if (onSlab) L->text (g, "ENGINE", r, juce::Justification::centredLeft, fh * 1.1f, fxtheme::pal().ink); else L->haloText (g, "ENGINE", r, juce::Justification::centredLeft, fh * 1.1f); }
            return;
        }
        g.setColour (ghost() ? ghosty::glass::ink2() : col::dim.get()); g.setFont (font (9.0f));
        for (auto* c : choices) g.drawText (labelsFor[c], c->getBounds().translated (0, -13).withHeight (12), juce::Justification::centredLeft, false);
        if (engineBox) g.drawText ("ENGINE", engineLabel, juce::Justification::centredLeft, false);
    }
    // ---- PAINTED layout (LAYOUT.md's rule for every unit page): the switches and menus in rows of look-sized buttons at the
    // bottom, the dials on a grid with no scrolling (as big as fits, up to the look's big dial), a drawn editor on its slab with
    // its dials in one row beneath
    bool hasEditor() const { return rhythmPanel != nullptr; }
    bool drawsCurve() const { return dynamic_cast<CurveDrawEditor*> (rhythmPanel.get()) != nullptr; }
    // the ENGINE menu is on a CURVES set for unit u
    bool curveSetShown (int u) const
    {
        if (! engineBox) return false; const auto labels = engines::engineLabels (u); const int e = engineBox->getSelectedId() - 1;
        return e >= 0 && e < (int) labels.size() && labels[(size_t) e].rfind ("CURVES", 0) == 0;
    }
    CurveShowPanel* curveView() const { return curvePanel != nullptr && curvePanel->isOpen() ? curvePanel.get() : nullptr; }
    juce::TextButton* showCurveButton() const { return showCurveBtn != nullptr && showCurveBtn->getParentComponent() == this ? showCurveBtn.get() : nullptr; }
    // (a switch or menu is never under about 85 % of its width on the drawn looks: there a row holds 4 of them, 6 beside a drawn editor,
    // which is 280 / 184 px at 1200 wide; so here a row holds only as many as stay 242 / 160 px wide)
    int switchCols (int w, float s, painted::Look& L) const { const int nb = choices.size() + toggles.size(); const int fit = juce::jmax (1, (int) (((float) w + 1.0f) / ((hasEditor() ? 160.0f : 242.0f) * s + 2.0f * (float) juce::roundToInt (5.0f * s)))); return juce::jmax (1, juce::jmin (fit, juce::jlimit (3, 6, (int) ((float) w / (186.0f * s))), juce::jmax (nb, (int) L.num ("rack.choiceCols", 3)))); }
    int switchRowsH (int w, float s, painted::Look& L) const { const int nb = choices.size() + toggles.size(), bc = switchCols (w, s, L); return nb == 0 ? 0 : ((nb + bc - 1) / bc) * juce::roundToInt (43.0f * s) + 2; }
    // the dial size a grid of "rows" rows gets in a w x h area
    float dialFor (int rows, int w, int h, float s, painted::Look& L) const { const int nk = knobs.size(), cols = (nk + rows - 1) / rows; return juce::jmin (L.num ("rack.knobCell", 124) * s, (float) w / (float) cols - 6.0f, (float) h / (float) rows - 20.0f * s); }
    int bestRows (int w, int h, float s, painted::Look& L, int maxRows = 4) const
    {
        const int nk = knobs.size(), pref = (int) L.num ("rack.knobCols", 5); int best = 1; float bd = 0.0f;
        for (int rows = 1; rows <= maxRows; ++rows) { const float d = dialFor (rows, w, h, s, L); if (d > bd + 0.5f) { bd = d; best = rows; } }
        for (int rows = 1; rows <= maxRows; ++rows) if ((nk + rows - 1) / rows <= pref && dialFor (rows, w, h, s, L) >= bd * 0.9f) return rows;   // the look's drawn arrangement when it costs (almost) no size
        return best;
    }
    // where the switches go on a page without a drawn editor: in rows under the dials, or (when that leaves the dials bigger) in a
    // column of 1 to 3 beside them. Either way each switch keeps its full width.
    // One row underneath is the drawn arrangement (FATTEN's mode row) and stays whenever the switches fit in one row. In a
    // column the rows may sit a little closer (40 instead of 43) when that is what makes it fit.
    struct Plan { int swCols = 0, rowH = 0; float dial = 0.0f; };   // swCols 0 = rows underneath
    Plan plan (int w, int h, float s, painted::Look& L) const
    {
        Plan p; const int nb = choices.size() + toggles.size(), nk = knobs.size(), rowH = juce::roundToInt (43.0f * s);
        if (nk > 0) { const int kh = h - switchRowsH (w, s, L); p.dial = kh > 0 ? dialFor (bestRows (w, kh, s, L), w, kh, s, L) : 0.0f; }
        if (nb == 0 || nk == 0 || nb <= switchCols (w, s, L)) return p;
        const int cellW = juce::roundToInt (242.0f * s) + 2 * juce::roundToInt (5.0f * s);
        for (int c = 1; c <= 3; ++c)
        {
            const int rowsS = (nb + c - 1) / c, wd = w - c * cellW - juce::roundToInt (12.0f * s), rh = juce::jmin (rowH, h / rowsS); if (rh < juce::roundToInt (40.0f * s) || wd < w / 3) continue;
            const float d = dialFor (bestRows (wd, h, s, L), wd, h, s, L); if (d > p.dial * 1.03f) { p.dial = d; p.swCols = c; p.rowH = rh; }
        }
        return p;
    }
    // would this page's dials stay comfortable in a w x h area? (else the page takes the wide slab)
    bool fitsIn (int w, int h, float s, painted::Look& L) const
    {
        if (rhythmPanel) return false; if (knobs.isEmpty()) return true;
        return plan (w, h - (bandBox && unit == uEq ? juce::roundToInt (30.0f * s) : 0) - (meter ? juce::roundToInt (28.0f * s) : 0) - (engineBox ? juce::roundToInt (28.0f * s) : 0), s, L).dial >= 64.0f * s;
    }
    void layoutPainted (painted::Look& L)
    {
        const float s = painted::Map (*this).s(); auto r = getLocalBounds(); auto px = [s] (float v) { return juce::roundToInt (v * s); };
        if (engineBox) { auto row = r.removeFromTop (px (24)); engineLabel = row.removeFromLeft (px (58)); engineBox->setBounds (row.removeFromLeft (px (250))); if (auto* b = showCurveButton()) { row.removeFromLeft (px (8)); b->setBounds (row.removeFromLeft (px (110))); } r.removeFromTop (px (4)); }
        if (bandBox && bandBox->isVisible() && unit == uEq) { auto row = r.removeFromTop (px (26)); bandBox->setBounds (row.removeFromLeft (px (170))); row.removeFromLeft (px (10)); if (spectrumBtn && spectrum) spectrumBtn->setBounds (row.removeFromLeft (px (120))); r.removeFromTop (px (4)); }
        if (meter) { meter->setBounds (r.removeFromTop (px (22))); r.removeFromTop (px (6)); }
        const int nb = choices.size() + toggles.size(), nk = knobs.size();
        // a drawn editor with room beside it (the wide slab): the editor on the left, its dials and switches in a column on the right;
        // otherwise the editor on top with one row of small dials beneath
        const bool beside = rhythmPanel != nullptr && nk + nb > 0 && r.getWidth() >= px (900);
        auto ctl = r;
        if (beside) { ctl = r.removeFromRight (juce::jlimit (px (360), px (512), juce::roundToInt ((float) r.getWidth() * 0.445f))); r.removeFromRight (px (12)); }
        const Plan pl = rhythmPanel == nullptr ? plan (r.getWidth(), r.getHeight(), s, L) : Plan();
        const int bc = pl.swCols > 0 ? pl.swCols : switchCols (ctl.getWidth(), s, L), rowH = pl.swCols > 0 ? pl.rowH : px (43);
        juce::Rectangle<int> br;
        if (pl.swCols > 0) { br = ctl.removeFromRight (pl.swCols * (px (242) + 2 * px (5))); ctl.removeFromRight (px (12)); const int need = ((nb + bc - 1) / bc) * rowH; br = br.withSizeKeepingCentre (br.getWidth(), juce::jmin (br.getHeight(), need)); }   // the switch column, centred beside the dials
        else br = ctl.removeFromBottom (switchRowsH (ctl.getWidth(), s, L));
        const int bw = br.getWidth() / bc; int i = 0;
        auto place = [&] (juce::Component* c) { auto cell = juce::Rectangle<int> (br.getX() + (i % bc) * bw, br.getY() + (i / bc) * rowH, bw, rowH); c->setBounds (cell.reduced (px (5), 0).withTrimmedTop (rowH - px (28)).withHeight (px (26))); ++i; };
        for (auto* c : choices) place (c); for (auto* tg : toggles) place (tg);
        if (rhythmPanel && ! beside)
        {
            if (! beside) r = ctl;
            const int rowsK = nk > 0 ? (nk + 11) / 12 : 0, cols = nk > 0 ? (nk + rowsK - 1) / rowsK : 1, cw = r.getWidth() / cols, dial = juce::jlimit (px (40), px (64), cw - 6), kh = dial + px (20);
            auto kr = r.removeFromBottom (rowsK * kh); if (nk > 0) r.removeFromBottom (px (4));
            rhythmPanel->setBounds (r);
            for (int k = 0; k < nk; ++k) knobs[k]->setBounds (juce::Rectangle<int> (kr.getX() + (k % cols) * cw, kr.getY() + (k / cols) * kh, cw, kh).withSizeKeepingCentre (cw, kh));   // (the whole cell is the dial's hit area)
            return;
        }
        if (rhythmPanel) rhythmPanel->setBounds (r);
        if (nk == 0) return;
        const auto kr = ctl; const int maxRows = beside ? 6 : 4;
        const int rows = bestRows (kr.getWidth(), kr.getHeight(), s, L, maxRows), cols = (nk + rows - 1) / rows, cw = kr.getWidth() / cols, ch = kr.getHeight() / rows;
        const int dial = juce::roundToInt (dialFor (rows, kr.getWidth(), kr.getHeight(), s, L)), kh = dial + px (20);
        for (int k = 0; k < nk; ++k) knobs[k]->setBounds (juce::Rectangle<int> (kr.getX() + (k % cols) * cw, kr.getY() + (k / cols) * ch, cw, ch).withSizeKeepingCentre (cw, juce::jmin (ch, kh)));
    }
    bool onSlab = false, pods = false;
    void resized() override
    {
        layoutControls();
        if (spectrum)
        {   // the spectrum takes the dials' place while its switch is on
            const bool show = spectrumBtn && spectrumBtn->getToggleState(); juce::Rectangle<int> u; for (auto* k : knobs) u = u.isEmpty() ? k->getBounds() : u.getUnion (k->getBounds());
            spectrum->setBounds (u); for (auto* k : knobs) k->setVisible (! show); spectrum->setVisible (show && ! u.isEmpty());
        }
    }
    std::function<void()> onVisualsOff;
    void layoutControls()
    {
        // the unit's visual: on the drawn looks a pane at the right of the page (a painted look places its own, when its art is in)
        const bool showPane = pane != nullptr && unitVisualsOn() && painted::active() == nullptr && rhythmPanel == nullptr && getWidth() >= 900;
        if (pane) pane->setVisible (showPane);
        if (auto* L = painted::active()) { layoutPainted (*L); return; }
        auto r = getLocalBounds().reduced (6);
        if (showPane) { pane->setBounds (r.removeFromRight (juce::jlimit (220, 300, r.getWidth() / 4)).withTrimmedLeft (8)); }
        if (engineBox) { auto row = r.removeFromTop (24); engineLabel = row.removeFromLeft (58); engineBox->setBounds (row.removeFromLeft (250)); if (auto* b = showCurveButton()) { row.removeFromLeft (8); b->setBounds (row.removeFromLeft (110)); } r.removeFromTop (4); }
        if (bandBox && bandBox->isVisible() && unit == uEq) { auto row = r.removeFromTop (26); bandBox->setBounds (row.removeFromLeft (160)); row.removeFromLeft (10); if (spectrumBtn && spectrum) spectrumBtn->setBounds (row.removeFromLeft (120)); r.removeFromTop (6); }
        if (meter) { meter->setBounds (r.removeFromTop (22)); r.removeFromTop (6); }
        if (rhythmPanel)
        {   // the drawn editor gets what the knobs (one row of up to 12, 66 high) and the switches leave
            const int nk = knobs.size(), ck = juce::jlimit (3, 12, nk), rk = (nk + ck - 1) / ck, nb = choices.size() + toggles.size();
            const int need = (nk > 0 ? rk * 66 : 0) + (nb > 0 ? ((nb + 5) / 6) * 46 + 8 : 0) + 6;
            rhythmPanel->setBounds (r.removeFromTop (nk + nb == 0 ? r.getHeight() : juce::jmax (150, r.getHeight() - need))); r.removeFromTop (6);
        }
        const int n = knobs.size(); int cols = rhythmPanel ? juce::jlimit (3, 12, n) : juce::jlimit (3, 8, (int) std::ceil (std::sqrt ((double) juce::jmax (1, n) * 1.6))), rowsK = (n + cols - 1) / cols;
        const int bcols = rhythmPanel ? 6 : 4;
        const int bottomH = (choices.isEmpty() && toggles.isEmpty()) ? 0 : (((choices.size() + toggles.size() + bcols - 1) / bcols) * 46 + 8);
        auto kr = r.withTrimmedBottom (bottomH);
        // (0.1.9: a full page - RETRO, SPIRIT EQ - takes more columns instead of letting its last dial row run under the menus)
        while (! rhythmPanel && rowsK > 1 && rowsK * 70 > kr.getHeight() && cols < 12) { ++cols; rowsK = (n + cols - 1) / cols; }
        const int cellH = rhythmPanel ? juce::jmin (66, rowsK > 0 ? kr.getHeight() / rowsK : 66) : juce::jmin (juce::jlimit (70, 110, rowsK > 0 ? kr.getHeight() / rowsK : 90), rowsK > 0 ? kr.getHeight() / rowsK : 110);
        std::vector<juce::Component*> items; for (auto* k : knobs) items.push_back (k);
        gridLayout (kr, items, cols, cellH);
        auto br = r.removeFromBottom (bottomH); br.removeFromTop (8); const int bc = bcols, bw = br.getWidth() / bc; int i = 0;
        for (auto* c : choices) { auto cell = juce::Rectangle<int> (br.getX() + (i % bc) * bw, br.getY() + (i / bc) * 46, bw, 46); c->setBounds (cell.reduced (4, 0).withTrimmedTop (14).withHeight (26)); ++i; }
        for (auto* tg : toggles) { auto cell = juce::Rectangle<int> (br.getX() + (i % bc) * bw, br.getY() + (i / bc) * 46, bw, 46); tg->setBounds (cell.reduced (4, 0).withTrimmedTop (14).withHeight (26)); ++i; }
    }
    int unit = uFatten, eqBand = 0;
private:
    std::vector<std::pair<juce::Component::SafePointer<juce::Component>, std::string>> controlIds;   // each control's own id, for the grey-out
    PhantomProcessor& proc; juce::OwnedArray<ModKnob> knobs; juce::OwnedArray<Choice> choices; juce::OwnedArray<Toggle> toggles; std::map<juce::Component*, juce::String> labelsFor; std::unique_ptr<juce::ComboBox> bandBox; std::unique_ptr<Choice> engineBox;
    std::unique_ptr<juce::TextButton> showCurveBtn; std::unique_ptr<CurveShowPanel> curvePanel; int curvePanelUnit = -1; juce::Rectangle<int> curveBounds; juce::Rectangle<int> engineLabel; std::unique_ptr<juce::Component> rhythmPanel; std::unique_ptr<ReductionMeter> meter; std::unique_ptr<SpectrumView> spectrum; std::unique_ptr<juce::TextButton> spectrumBtn; std::unique_ptr<VisualPane> pane;
};

// ================================================================ LANES page
class LanesPage : public juce::Component
{
public:
    LanesPage (PhantomProcessor& p) : proc (p)
    {
        for (int k = 0; k < kLanes; ++k)
        {
            const juce::String b = "lane" + juce::String (k) + "_"; const auto acc = Strip::laneColour (k);
            gain.add (new Knob (proc.apvts, b + "gain", "GAIN", acc)); pan.add (new Knob (proc.apvts, b + "pan", "PAN", acc)); mute.add (new Toggle (proc.apvts, b + "mute", "MUTE", acc));
            addAndMakeVisible (gain[k]); addAndMakeVisible (pan[k]); addAndMakeVisible (mute[k]);
        }
    }
    void paint (juce::Graphics& g) override
    {
        const auto topo = proc.getTopology(); const int w = getWidth() / kLanes;
        for (int k = 0; k < kLanes; ++k)
        {
            auto c = juce::Rectangle<int> (k * w, 0, w, getHeight()).reduced (4); const auto acc = Strip::laneColour (k);
            drawScreen (g, c.toFloat(), acc, 10.0f);
            g.setColour (acc); g.setFont (font (12.0f)); g.drawText (k == 0 ? "MAIN CHAIN" : "PARALLEL LANE " + juce::String::charToString ((juce::juce_wchar) ('A' + k - 1)), c.removeFromTop (22).reduced (8, 0), juce::Justification::centredLeft);
            c.removeFromTop (96 + muteBelow());
            g.setColour (col::dim.get()); g.setFont (font (9.0f)); g.drawText (k == 0 ? "the serial chain; the limiter always ends here" : "fed from the input, summed back latency-aligned", c.removeFromTop (14).reduced (8, 0), juce::Justification::centredLeft);
            int y = c.getY() + 4;
            for (int i = 0; i < kNumUnits; ++i) { const int u = topo.order[(size_t) i]; if (u == uLim || topo.lane[(size_t) u] != k) continue; auto chip = juce::Rectangle<int> (c.getX() + 8, y, c.getWidth() - 16, 18); g.setColour (accentOf (u).withAlpha (0.25f)); g.fillRoundedRectangle (chip.toFloat(), 5); g.setColour (col::white.get()); g.setFont (font (10.0f)); g.drawText (unitDef (u).title, chip.reduced (6, 0), juce::Justification::centredLeft, false); y += 21; if (y > c.getBottom() - 20) break; }
            if (k > 0) { g.setColour (col::dim.withAlpha (0.8f)); g.setFont (font (9.0f)); g.drawText ("right-click a tile in the strip to put it here", juce::Rectangle<int> (c.getX(), c.getBottom() - 16, c.getWidth(), 14), juce::Justification::centred, false); }
        }
    }
    void resized() override
    {
        const int w = getWidth() / kLanes;
        for (int k = 0; k < kLanes; ++k)
        {
            auto c = juce::Rectangle<int> (k * w, 0, w, getHeight()).reduced (4); c.removeFromTop (22); auto row = c.removeFromTop (96).reduced (6, 0);
            if (muteBelow() > 0) { gain[k]->setBounds (row.removeFromLeft (row.getWidth() / 2)); pan[k]->setBounds (row); mute[k]->setBounds (c.removeFromTop (muteBelow()).reduced (10, 1)); continue; }   // a narrow lane: two dials across, MUTE under them
            const int cw = row.getWidth() / 3; gain[k]->setBounds (row.removeFromLeft (cw)); pan[k]->setBounds (row.removeFromLeft (cw)); mute[k]->setBounds (row.reduced (4, 34));
        }
    }
    int muteBelow() const { return getWidth() / kLanes < 250 ? 32 : 0; }
private:
    PhantomProcessor& proc; juce::OwnedArray<Knob> gain, pan; juce::OwnedArray<Toggle> mute;
};

// ================================================================ MODS page: the modulator pool, draggable source chips, the step editors, the routes
class ModsPage : public juce::Component
{
public:
    struct SourceChip : public juce::Component
    {
        int src; juce::String name; PhantomProcessor& proc;
        SourceChip (int s, PhantomProcessor& p) : src (s), name (modSourceName (s)), proc (p) {}
        void paint (juce::Graphics& g) override
        {
            const float v = proc.rack.sourceValue[(size_t) src]; auto r = getLocalBounds().toFloat().reduced (1);
            g.setColour (col::card2.get()); g.fillRoundedRectangle (r, 6); g.setColour (col::ecto.withAlpha (0.9f)); g.drawRoundedRectangle (r, 6, 1.0f);
            g.setColour (col::ecto.withAlpha (0.35f)); g.fillRoundedRectangle (r.withWidth (r.getWidth() * (v + 1.0f) * 0.5f), 6);
            g.setColour (col::white.get()); g.setFont (font (10.0f)); g.drawText (name, r, juce::Justification::centred, false);
        }
        void mouseDrag (const juce::MouseEvent& e) override { if (e.getDistanceFromDragStart() > 5) if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor (this)) if (! c->isDragAndDropActive()) c->startDragging (src, this); }
    };
    struct StepEditor : public juce::Component
    {
        PhantomProcessor& proc; int seq; StepEditor (PhantomProcessor& p, int s) : proc (p), seq (s) {}
        int count() const { static const int c[] { 8, 16, 32 }; return c[juce::jlimit (0, 2, (int) proc.apvts.getRawParameterValue ("step" + juce::String (seq + 1) + "_count")->load())]; }
        void paint (juce::Graphics& g) override
        {
            const int n = count(); const float w = (float) getWidth() / n; const int cur = proc.rack.steps[(size_t) seq].currentStep();
            for (int i = 0; i < n; ++i) { const float v = proc.getStepValue (seq, i); auto c = juce::Rectangle<float> (i * w, 0, w, (float) getHeight()).reduced (1); g.setColour (col::card.get()); g.fillRect (c); g.setColour ((i == cur ? col::slime : col::ecto).withAlpha (0.8f)); g.fillRect (c.withTop (c.getBottom() - c.getHeight() * v)); }
        }
        void edit (const juce::MouseEvent& e) { const int n = count(); const int i = juce::jlimit (0, n - 1, (int) (e.position.x / getWidth() * n)); proc.setStepValue (seq, i, 1.0f - juce::jlimit (0.0f, 1.0f, e.position.y / getHeight())); repaint(); }
        void mouseDown (const juce::MouseEvent& e) override { edit (e); } void mouseDrag (const juce::MouseEvent& e) override { edit (e); }
    };
    ModsPage (PhantomProcessor& p) : proc (p)
    {
        for (int s = 0; s < kNumModSources; ++s) { auto* c = chips.add (new SourceChip (s, p)); addAndMakeVisible (c); }
        for (int k = 0; k < kNumLfos; ++k) { const juce::String b = "lfo" + juce::String (k + 1) + "_"; const auto acc = col::ecto.get(); lfoShape.add (new Choice (p.apvts, b + "shape", acc)); lfoDiv.add (new Choice (p.apvts, b + "div", acc)); lfoSync.add (new Toggle (p.apvts, b + "sync", "SYNC", acc)); lfoRate.add (new Knob (p.apvts, b + "rate", "RATE", acc)); addAndMakeVisible (lfoShape[k]); addAndMakeVisible (lfoDiv[k]); addAndMakeVisible (lfoSync[k]); addAndMakeVisible (lfoRate[k]); }
        for (int k = 0; k < kNumSteps; ++k) { const juce::String b = "step" + juce::String (k + 1) + "_"; const auto acc = col::ice.get(); stepCount.add (new Choice (p.apvts, b + "count", acc)); stepLen.add (new Choice (p.apvts, b + "len", acc)); stepGlide.add (new Knob (p.apvts, b + "glide", "GLIDE", acc)); editors.add (new StepEditor (p, k)); addAndMakeVisible (stepCount[k]); addAndMakeVisible (stepLen[k]); addAndMakeVisible (stepGlide[k]); addAndMakeVisible (editors[k]); }
        for (int k = 0; k < kNumEnvs; ++k) { const juce::String b = "env" + juce::String (k + 1) + "_"; const auto acc = col::gold.get(); envAtk.add (new Knob (p.apvts, b + "atk", "ATTACK", acc)); envRel.add (new Knob (p.apvts, b + "rel", "RELEASE", acc)); addAndMakeVisible (envAtk[k]); addAndMakeVisible (envRel[k]); }
        routesBox.setMultiLine (true); routesBox.setReadOnly (true); routesBox.setFont (font (10.0f, false)); addAndMakeVisible (routesBox);
        clearBtn.onClick = [this] { proc.setRoutes ({}); }; addAndMakeVisible (clearBtn);
        startTimer (100);
    }
    void paint (juce::Graphics& g) override
    {
        g.setColour (col::dim.get()); g.setFont (font (9.0f));
        g.drawText ("drag a source chip onto any knob on a unit page (or right-click the knob) to modulate it", getLocalBounds().removeFromTop (14), juce::Justification::centredLeft);
        for (int k = 0; k < kNumLfos; ++k) g.drawText ("LFO " + juce::String (k + 1), lfoRate[k]->getX(), lfoRate[k]->getY() - 14, 80, 12, juce::Justification::centredLeft);
        for (int k = 0; k < kNumSteps; ++k) g.drawText ("STEP " + juce::String (k + 1), editors[k]->getX(), editors[k]->getY() - 14, 80, 12, juce::Justification::centredLeft);
        for (int k = 0; k < kNumEnvs; ++k) g.drawText ("ENV " + juce::String (k + 1) + " (input)", envAtk[k]->getX(), envAtk[k]->getY() - 14, 120, 12, juce::Justification::centredLeft);
        g.drawText ("ROUTES", routesBox.getX(), routesBox.getY() - 14, 100, 12, juce::Justification::centredLeft);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (4); r.removeFromTop (16);
        auto chipRow = r.removeFromTop (24); const int cw = chipRow.getWidth() / kNumModSources; for (auto* c : chips) c->setBounds (chipRow.removeFromLeft (cw).reduced (2, 0));
        r.removeFromTop (18);
        auto left = r.removeFromLeft (r.getWidth() * 2 / 3); auto right = r.withTrimmedLeft (10);
        // a low page (the painted looks have three tile rows above it; the drawn looks at their smallest window): the step length
        // menu sits beside the dial, not under it, so nothing is covered
        const bool compact = painted::active() != nullptr || left.getHeight() - juce::jlimit (84, 110, left.getHeight() / 2) - 18 < 96;
        auto lfoRow = left.removeFromTop (compact ? juce::jlimit (84, 104, left.getHeight() - 96) : juce::jlimit (84, 110, left.getHeight() / 2)); const int lw = lfoRow.getWidth() / kNumLfos;
        for (int k = 0; k < kNumLfos; ++k) { auto c = lfoRow.removeFromLeft (lw).reduced (4, 0); lfoRate[k]->setBounds (c.removeFromLeft (64)); auto cc = c.reduced (4, 2); lfoShape[k]->setBounds (cc.removeFromTop (22)); cc.removeFromTop (4); lfoDiv[k]->setBounds (cc.removeFromTop (22)); cc.removeFromTop (4); lfoSync[k]->setBounds (cc.removeFromTop (20)); }
        left.removeFromTop (18);
        auto stepRow = left; const int sw = stepRow.getWidth() / kNumSteps;
        for (int k = 0; k < kNumSteps; ++k) { auto c = stepRow.removeFromLeft (sw).reduced (4, 0); auto side = c.removeFromLeft (compact ? 176 : 120); stepGlide[k]->setBounds (side.withHeight (juce::jmin (compact ? 78 : 64, side.getHeight())).withWidth (64));
            if (compact) { stepCount[k]->setBounds (side.getX() + 70, c.getY() + 4, 102, 24); stepLen[k]->setBounds (side.getX() + 70, c.getY() + 34, 102, 24); }
            else { stepCount[k]->setBounds (side.getX() + 68, c.getY() + 2, 50, 22); stepLen[k]->setBounds (side.getX(), c.getY() + 68, 118, 22); }
            editors[k]->setBounds (c.reduced (2)); }
        right.removeFromTop (14);
        auto envRow = right.removeFromTop (74); const int ew = envRow.getWidth() / kNumEnvs;
        for (int k = 0; k < kNumEnvs; ++k) { auto c = envRow.removeFromLeft (ew).reduced (4, 0); envAtk[k]->setBounds (c.removeFromLeft (c.getWidth() / 2)); envRel[k]->setBounds (c); }
        right.removeFromTop (18);
        clearBtn.setBounds (right.removeFromBottom (24).removeFromRight (120)); routesBox.setBounds (right.withTrimmedBottom (6));
    }
private:
    void timerCallback()
    {
        for (auto* c : chips) c->repaint(); for (auto* e : editors) e->repaint();
        juce::String s; int i = 1; for (auto& r : proc.getRoutes()) { const auto& specs = proc.rack.slot (r.unit).params(); s << i++ << ". " << modSourceName (r.source) << "  ->  " << unitDef (r.unit).title << " / " << (r.param < (int) specs.size() ? specs[(size_t) r.param].name : "?") << "   " << juce::roundToInt (r.depth * 100) << " %" << (r.bipolar ? "  +/-" : "  +") << "\n"; }
        if (s.isEmpty()) s = "(no modulation yet)";
        if (s != routesBox.getText()) routesBox.setText (s, false);
    }
    struct Tm : public juce::Timer { std::function<void()> f; void timerCallback() override { f(); } } tm; void startTimer (int ms) { tm.f = [this] { timerCallback(); }; tm.startTimer (ms); }
    PhantomProcessor& proc; juce::OwnedArray<SourceChip> chips; juce::OwnedArray<Choice> lfoShape, lfoDiv, stepCount, stepLen; juce::OwnedArray<Toggle> lfoSync; juce::OwnedArray<Knob> lfoRate, stepGlide, envAtk, envRel; juce::OwnedArray<StepEditor> editors;
    juce::TextEditor routesBox; juce::TextButton clearBtn { "CLEAR ALL" };
};

// ================================================================ PERFORM page: the 8 macros big, the scene morph progress
class PerformPage : public juce::Component
{
public:
    PerformPage (PhantomProcessor& p) : proc (p) { for (int k = 0; k < kNumMacros; ++k) { auto* m = macros.add (new Knob (p.apvts, "macro" + juce::String (k + 1), macroDefaultName (k), col::ecto.get())); addAndMakeVisible (m); } }
    void paint (juce::Graphics& g) override { g.setColour (col::dim.get()); g.setFont (font (9.0f)); g.drawText ("MACROS: right-click any knob on a unit page and choose Modulate with MACRO n (or drag a MACRO chip from MODS). Scenes: the bar below.", getLocalBounds().removeFromTop (14), juce::Justification::centredLeft); }
    void resized() override { auto r = getLocalBounds().reduced (6); r.removeFromTop (16); std::vector<juce::Component*> items; for (auto* m : macros) items.push_back (m); gridLayout (r, items, 4, juce::jmin (130, r.getHeight() / 2)); }
private:
    PhantomProcessor& proc; juce::OwnedArray<Knob> macros;
};

} // namespace phantom
#include "../ui/SpatialPage.h"
namespace phantom
{
// ================================================================ the rack panel: tab row, strip, page, macro strip (Ghost Box's layout)
class RackPanel : public juce::Component
{
public:
    RackPanel (PhantomProcessor& p, PhantomEditor& e) : proc (p), editor (e), strip (p, [this] (int u) { unitPage.show (u); refreshHeader(); }), unitPage (p), lanes (p), mods (p), perform (p), spatialPage (p)
    {
        addAndMakeVisible (strip); addAndMakeVisible (unitPage); addChildComponent (lanes); addChildComponent (mods); addChildComponent (perform); addChildComponent (spatialPage);
        const char* names[] { "RACK", "LANES", "MODS", "PERFORM", "SPATIAL" };
        for (int i = 0; i < 5; ++i) { auto* b = tabs.add (new juce::TextButton (names[i])); b->setClickingTogglesState (true); b->setRadioGroupId (77); b->onClick = [this, i] { setMode (i); }; addAndMakeVisible (b); }
        tabs[0]->setToggleState (true, juce::dontSendNotification);
        presetBox.setTextWhenNothingSelected ("PRESETS"); presetBox.onChange = [this] { pickPreset(); }; addAndMakeVisible (presetBox); refreshPresets();
        saveBtn.onClick = [this] { savePreset(); }; undoBtn.onClick = [this] { proc.undo.undo(); }; redoBtn.onClick = [this] { proc.undo.redo(); };
        addAndMakeVisible (saveBtn); addAndMakeVisible (undoBtn); addAndMakeVisible (redoBtn);
        unitPreset.setTextWhenNothingSelected ("UNIT PRESET"); unitPreset.onChange = [this] { pickUnitPreset(); }; addAndMakeVisible (unitPreset);
        unitAb.onClick = [this] { proc.unitAbToggle (unitPage.unit); refreshHeader(); }; addAndMakeVisible (unitAb);
        for (int k = 0; k < kNumMacros; ++k) { auto* m = macroKnobs.add (new Knob (p.apvts, "macro" + juce::String (k + 1), macroDefaultName (k), col::ecto.get())); m->slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0); m->slider.setPopupDisplayEnabled (true, true, this); m->getProperties().set ("pfxDark", true); m->slider.getProperties().set ("pfxMacro", true); addAndMakeVisible (m); }
        unitPage.show (strip.selected); refreshHeader();
    }
    void showUnit (int u) { setMode (0); strip.selected = u; strip.repaint(); unitPage.show (u); refreshHeader(); }
    void setMode (int m) { mode = m; unitPage.setVisible (m == 0); lanes.setVisible (m == 1); mods.setVisible (m == 2); perform.setVisible (m == 3); spatialPage.setVisible (m == 4); strip.setVisible (m != 4); for (auto* k : macroKnobs) k->setVisible (m != 4); unitMix.reset(); unitTrim.reset(); refreshHeader(); resized(); repaint(); }
    void refreshHeader()
    {
        const int u = unitPage.unit; const auto acc = accentOf (u);
        unitMix.reset(); unitTrim.reset(); unitOn.reset();
        if (mode == 0)
        {
            if (unitDef (u).hasMix) { unitMix = std::make_unique<Knob> (proc.apvts, PhantomProcessor::pid (u, "mix"), u == uOtt ? "DEPTH" : "MIX", acc); unitMix->slider.setSliderStyle (juce::Slider::LinearHorizontal); unitMix->slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 16); addAndMakeVisible (*unitMix); }
            unitTrim = std::make_unique<Knob> (proc.apvts, PhantomProcessor::pid (u, "trim"), "TRIM", acc); unitTrim->slider.setSliderStyle (juce::Slider::LinearHorizontal); unitTrim->slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 16); addAndMakeVisible (*unitTrim);
            unitOn = std::make_unique<Toggle> (proc.apvts, PhantomProcessor::pid (u, "on"), "ON", acc); addAndMakeVisible (*unitOn);
            for (auto* k : { unitMix.get(), unitTrim.get() }) if (k != nullptr) { k->getProperties().set ("pfxDark", true); k->slider.getProperties().set ("pfxDark", true); }   // (painted looks: these sit on a dark rail)
        }
        if (pvPane == nullptr || pvPane->unit != u)
        {   // the unit's painted visual (only units the designer has painted have one)
            pvPane.reset(); if (auto sc = pv::makeScene (u)) { pvPane = std::make_unique<PaintedPane> (proc, u, std::move (sc)); pvPane->extra = pvExtras (proc, u); pvPane->beforePaint = [this] { pvGeometry(); }; pvPane->onHide = [this] { editor.setUnitVisuals (false); }; addChildComponent (*pvPane); }
        }
        unitAb.setButtonText (proc.unitAbCurrent (u) == 0 ? "A" : "B"); refreshUnitPresets();
        resized(); repaint();
    }
    // ---- PAINTED (LAYOUT.md): every rectangle below is in the 1200 x 760 design space and scales with the window
    int currentMode() const { return mode; }
    bool isWide() const { return mode != 4 && wideNow; }   // the page showing sits on the wide slab
    juce::Rectangle<int> blurbArea() const { return mode == 0 && blurbShown ? blurbR : juce::Rectangle<int>(); }   // (the unit page's blurb line, in this panel's own pixels; the layout check reads it)
    juce::Rectangle<int> selectedTileArea() { return strip.getBounds(); }
    void animTick (int tick) { if (mode == 4) spatialPage.animTick (tick); else strip.animTick (tick); }
    void setDirtySink (std::function<void (juce::Component*, juce::Rectangle<int>)> f) { strip.onDirty = [this, f] (juce::Rectangle<int> r) { f (&strip, r); }; spatialPage.onDirty = [this, f] (juce::Rectangle<int> r) { f (&spatialPage, r); }; }
    void resizedPainted (painted::Look& L)
    {
        const painted::Map m (*this);
        presetBox.setBounds (m.r (8, 72, 240, 25));
        static const float tx[5][2] { { 258, 50 }, { 313, 55 }, { 373, 51 }, { 429, 72 }, { 506, 66 } };
        for (int i = 0; i < tabs.size(); ++i) tabs[i]->setBounds (m.r (tx[i][0], 72, tx[i][1], 25));
        saveBtn.setBounds (m.r (959, 72, 94, 25)); undoBtn.setBounds (m.r (1061, 72, 62, 25)); redoBtn.setBounds (m.r (1129, 72, 62, 25)); infoR = m.r (584, 77, 368, 14);
        for (int k = 0; k < macroKnobs.size(); ++k) macroKnobs[k]->setBounds (m.r (82.0f + 148.0f * (float) k - 62.0f, 659, 124, 57));
        macroR = m.r (16, L.num ("rack.macroHintY", 641), 420, 14); slabR = {}; plateShown = false; unitPage.onSlab = true;
        const bool wasWide = isWide(); struct Redraw { RackPanel& p; bool was; ~Redraw() { if (p.isWide() != was) p.editor.repaint(); } } redraw { *this, wasWide };   // (a wide page has its own picture)
        if (mode == 4)
        {   // SPATIAL: no unit page, so no unit visual (this early return once left it showing from the unit page before; 0.1.12)
            if (pvPane != nullptr) pvPane->setVisible (false); screenBehindVisual() = false;
            card = {}; titleR = m.r (12, 108, 700, 20); spatialPage.setBounds (getLocalBounds()); unitPreset.setVisible (false); unitAb.setVisible (false); return;
        }
        strip.setBounds (m.r (L.rect ("rack.strip", { 0, 100, 800, 240 })));
        const auto pageD = L.rect ("rack.page"), wideD = L.rect ("rack.wide"), contentD = L.rect ("rack.content"), wideC = L.rect ("rack.wideContent"), edC = L.rect ("rack.editorContent", wideC);
        const bool plate = L.has ("rack.plate.img");
        juce::Rectangle<int> inner;
        if (mode == 0)
        {
            titleR = m.r (L.rect ("rack.title")); blurbR = m.r (L.rect ("rack.blurb")); railR = m.r (L.rect ("rack.rail"));
            { auto rr = railR.reduced (juce::roundToInt (8.0f * m.sx), 1); const int tw = unitMix ? rr.getWidth() * 2 / 5 : rr.getWidth();
              if (unitTrim) unitTrim->setBounds (rr.removeFromLeft (tw)); rr.removeFromLeft (juce::roundToInt (8.0f * m.sx)); if (unitMix) unitMix->setBounds (rr); }
            if (unitOn) unitOn->setBounds (m.r (L.rect ("rack.on"))); unitAb.setBounds (m.r (L.rect ("rack.ab"))); unitPreset.setBounds (m.r (L.rect ("rack.preset")));
            const auto narrow = m.r (contentD);
            const bool editorPage = unitPage.hasEditor(), wide = editorPage ? L.listHas ("rack.widePages", "editor") : ! unitPage.fitsIn (narrow.getWidth(), narrow.getHeight(), m.s(), L);
            wideNow = wide; blurbShown = ! (editorPage && ! plate); blurbOnSlab = false; unitPage.pods = false; blurbWide = false;
            if (wide && ! plate && L.has ("rack.wideBlurb"))
            {
                blurbShown = true; blurbWide = true; auto bd = L.rect ("rack.wideBlurb");
                // it ends before the scene's ghost (and any other ghost drawn as a strip there): the words wrap to two lines instead
                for (auto gr : editor.sceneGhostRects (L, "rack-wide")) if (gr.getY() < bd.getBottom() && gr.getBottom() > bd.getY() && gr.getRight() > bd.getX()) bd = bd.withRight (juce::jmin (bd.getRight(), gr.getX() - 6.0f));
                blurbR = m.r (bd);
            }   // a wide page's blurb: its own pane right of the unit header row
            if (! editorPage && ! wide) { inner = narrow; plateShown = plate; unitPage.onSlab = plate; if (L.has ("rack.pageSlab")) { slabR = m.r (L.rect ("rack.pageSlab")); unitPage.onSlab = true; blurbOnSlab = true; } else if ((bool) L.at ("rack.pods")) { unitPage.pods = true; unitPage.onSlab = true; blurbOnSlab = true; } }
            else { const auto c = wide ? wideC : edC; inner = m.r (c); slabR = plate ? m.r (wide ? wideD : pageD) : wide ? m.r (wideD) : m.r (c.expanded (12.0f, 9.0f)); }
        }
        else
        {
            static const char* pages[] { "", "lanes", "mods", "perform" };
            wideNow = L.listHas ("rack.widePages", pages[mode]); titleOut = false;
            slabR = m.r (wideNow ? wideD : pageD);
            if (wideNow && ! plate && L.has ("rack.wideTitle")) { inner = m.r (wideC); titleR = m.r (L.rect ("rack.wideTitle")); titleOut = true; }   // the page's title line sits above the wide slab, where the unit header row is on a unit page
            else { inner = slabR.reduced (juce::roundToInt (14.0f * m.sx), juce::roundToInt (10.0f * m.sy)); titleR = inner.removeFromTop (juce::roundToInt (20.0f * m.sy)); inner.removeFromTop (juce::roundToInt (4.0f * m.sy)); }
        }
        unitPreset.setVisible (mode == 0); unitAb.setVisible (mode == 0);
        unitPage.setBounds (inner); lanes.setBounds (inner); mods.setBounds (inner); perform.setBounds (inner);
        // the painted visual on its stage: a unit page on the narrow layout, in a look that has the visuals' parts, switched on
        if (pvPane != nullptr)
        {   // narrow page: the look's stage; a wide page: the look's wide stage (CEL, NEON) or the unit's own DAWN picture
            bool show = mode == 0 && L.hasVisuals() && unitVisualsOn() && ! unitPage.drawsCurve();   // (the DRAW editor takes the page: no picture over it)
            if (show && ! wideNow && L.has ("visual.stage")) { pvPane->setMode (PaintedPane::onStage); pvPane->setBounds (m.r (L.rect ("visual.stage"))); }
            else if (show && wideNow && L.has ("visual.wide.stage")) { pvPane->setMode (PaintedPane::onWideStage); pvPane->setBounds (m.r (L.rect ("visual.wide.stage"))); }
            else if (show && wideNow && L.str ("visual.style") == "dawn" && pvPane->getScene().hasDawn())
            {
                pvPane->setMode (pvPane->getScene().dawnUnder() ? PaintedPane::dawnUnder : PaintedPane::dawnOwn); pvPane->setBounds (m.r (pvPane->getScene().dawnRect()));
                if (pvPane->getMode() == PaintedPane::dawnUnder && ! pvPane->getScene().dawnOver()) pvPane->toBehind (&unitPage); else pvPane->toFront (false);
            }
            else if (show && wideNow && L.str ("visual.style") == "dawn" && L.has ("visual.stage")) { pvPane->setMode (PaintedPane::onStage); pvPane->setBounds (m.r (L.rect ("visual.stage"))); }   // (DAWN CRACKLE: its page leaves the brass window free)
            else show = false;
            if (show && pvPane->getMode() != PaintedPane::dawnUnder) pvPane->toFront (false);
            pvPane->setVisible (show);
        }
        screenBehindVisual() = pvPane != nullptr && pvPane->isVisible() && pvPane->getMode() == PaintedPane::dawnUnder && ! pvPane->getScene().dawnOver();
    }
    // DAWN's pictures behind / over a wide page's editor take the editor's own geometry (GRID's cells, HAUNT's pads, WARP's lane):
    // read just before each picture, as the editor lays itself out after the rack panel does
    void pvGeometry()
    {
        if (pvPane == nullptr || ! pvPane->isVisible() || (pvPane->getMode() != PaintedPane::dawnUnder && pvPane->getMode() != PaintedPane::dawnOwn)) return;
        const auto pb = pvPane->getBounds().toFloat(); const float kd = pvPane->getScene().dawnRect().getWidth() / juce::jmax (1.0f, pb.getWidth());
        auto toPic = [&] (juce::Component* c, juce::Point<float> p) { const auto q = getLocalPoint (c, p); return juce::Point<float> ((q.x - pb.getX()) * kd, (q.y - pb.getY()) * kd); };
        std::function<juce::Component* (juce::Component*, const std::function<bool (juce::Component*)>&)> findIn = [&] (juce::Component* c, const std::function<bool (juce::Component*)>& want) -> juce::Component* { if (want (c)) return c; for (auto* k : c->getChildren()) if (auto* r = findIn (k, want)) return r; return nullptr; };
        if (auto* hs = dynamic_cast<pv::HauntScene*> (&pvPane->getScene()))
            if (auto* hp = dynamic_cast<HauntPanel*> (findIn (&unitPage, [] (juce::Component* c) { return dynamic_cast<HauntPanel*> (c) != nullptr; })))
                for (int i = 0; i < 16; ++i) { const auto q = toPic (hp, hp->padRect (i).getCentre()); hs->padAt[(size_t) i] = { q.x, q.y }; }   // HAUNT: the editor's own pads
        if (auto* ws = dynamic_cast<pv::WarpScene*> (&pvPane->getScene()))
            if (auto* le = dynamic_cast<bendlane::Editor*> (findIn (&unitPage, [] (juce::Component* c) { return dynamic_cast<bendlane::Editor*> (c) != nullptr; })))
            {   // WARP: the editor's own lane mapping (its 0 line, and how far the range's end lies from it)
                const auto a = le->laneArea(); const auto c0 = toPic (le, { a.getCentreX(), a.getCentreY() }), c1 = toPic (le, { a.getCentreX(), a.getY() }); ws->laneY0 = c0.y; ws->laneYa = c0.y - c1.y;
            }
        if (auto* gs = dynamic_cast<pv::GridScene*> (&pvPane->getScene()))
        {   // GRID: its light lies behind the editor's own cells
            std::function<GridPanel* (juce::Component*)> findGrid = [&] (juce::Component* c) -> GridPanel* { if (auto* gp = dynamic_cast<GridPanel*> (c)) return gp; for (auto* k : c->getChildren()) if (auto* r = findGrid (k)) return r; return nullptr; };
            if (auto* gp = findGrid (&unitPage))
            {
                const auto pb = pvPane->getBounds().toFloat(); const float k = gs->dawnRect().getWidth() / juce::jmax (1.0f, pb.getWidth());
                const auto c0 = getLocalArea (gp, gp->cellRect (0, 0)), c1 = getLocalArea (gp, gp->cellRect (griddata::kRows - 1, gp->steps() - 1));
                gs->cellX0 = (c0.getX() - pb.getX()) * k; gs->cellY0 = (c0.getY() - pb.getY()) * k; gs->cellW = (c1.getRight() - c0.getX()) * k; gs->rowH = c0.getHeight() * k;
            }
        }
    }
    bool pvShowing() const { return pvPane != nullptr && pvPane->isVisible(); }
    bool pvOnWideStage() const { return pvShowing() && pvPane->getMode() == PaintedPane::onWideStage; }
    void paintPainted (juce::Graphics& g, painted::Look& L)
    {
        const painted::Map m (*this); const float s = m.s();
        if (L.glassBlur.isValid()) L.panel (g, *this, L.str ("slabs.rail"), m.f (802, 74, 154, 21));   // (smoked-glass looks: the latency read-out has its own small pane)
        L.haloText (g, "latency " + juce::String (proc.totalLatency()) + " smp   oversampling " + proc.apvts.getParameter ("os")->getCurrentValueAsText(), infoR.toFloat(), juce::Justification::centredRight, 8.6f * s);
        if (mode == 4) { L.paperText (g, "SPATIAL: place each unit's sound around your head  -  drag the tiles; record their moves while the song plays", titleR.toFloat(), 9.6f * s, true, "tag"); return; }
        L.haloText (g, "MACROS  -  right-click any knob to put it under a macro", macroR.toFloat(), juce::Justification::centredLeft, 8.6f * s);
        const juce::String ed = L.str ("slabs.editor");
        if (plateShown) { const auto n = L.str ("rack.plate.img"); L.draw (g, n, m.f (L.el ("rack.plate.at", 0), L.el ("rack.plate.at", 1), L.img (n).w(), L.img (n).h())); }
        else if (const auto wp = L.str ("rack.widePanel.img"); isWide() && ! slabR.isEmpty() && L.hasFile (wp)) L.draw (g, wp, m.f (L.el ("rack.widePanel.at", 0), L.el ("rack.widePanel.at", 1), L.img (wp).w(), L.img (wp).h()));   // the look's own wide slab, one fixed picture
        else if (! slabR.isEmpty()) L.panel (g, *this, ed, slabR.toFloat());
        if (pvShowing() && pvPane->getMode() == PaintedPane::onStage) if (const auto fr = L.str ("visual.frame.img"); fr.isNotEmpty()) L.draw (g, fr, m.f (L.el ("visual.frame.at", 0), L.el ("visual.frame.at", 1), L.el ("visual.frame.size", 0), L.el ("visual.frame.size", 1)));   // the stage's frame (the visual draws its inner edge again over the picture)
        if (pvOnWideStage()) if (const auto fr = L.str ("visual.wide.frame.img"); fr.isNotEmpty()) L.draw (g, fr, m.f (L.el ("visual.wide.frame.at", 0), L.el ("visual.wide.frame.at", 1), L.el ("visual.wide.frame.size", 0), L.el ("visual.wide.frame.size", 1)));   // the wide stage's frame (under the hands)
        if (const auto wh = L.str ("rack.wideHands.img"); isWide() && L.hasFile (wh)) L.draw (g, wh, m.f (L.el ("rack.wideHands.at", 0), L.el ("rack.wideHands.at", 1), L.img (wh).w(), L.img (wh).h()));   // what lies over the slab's top edge
        if (mode == 0)
        {
            const auto& d = unitDef (unitPage.unit);
            if (const auto ts = L.str ("rack.titleSprite"); ts.isNotEmpty() && L.hasImg (ts + d.shortName)) L.draw (g, ts + d.shortName, m.f (L.el ("rack.titleSpriteAt", 0, 8), L.el ("rack.titleSpriteAt", 1, 340), L.img (ts + d.shortName).w(), L.img (ts + d.shortName).h()));   // the unit's name as art
            else if ((bool) L.at ("rack.titleOnSign")) { L.nineBody (g, L.str ("slabs.title"), titleR.toFloat()); g.setColour (L.cream()); g.setFont (painted::pfont (15.0f * s, true)); g.drawFittedText (d.title, titleR.reduced (juce::roundToInt (8.0f * s), 0).translated (0, -1), juce::Justification::centred, 1, 0.6f); }
            else L.text (g, d.title, titleR.toFloat(), juce::Justification::centredLeft, 20.0f * s, L.cream(), true);
            if (blurbShown && blurbWide && ! pvOnWideStage())
            {   // on a wide page: a pane of the look's glass with the blurb in it, or the blurb on paper
                auto br = blurbR.toFloat(); br = br.withWidth (juce::jmin (br.getWidth(), juce::GlyphArrangement::getStringWidth (painted::pfont (9.2f * s), d.blurb) + 20.0f * s));   // (no wider than its words; a long one wraps to two lines inside its rectangle)
                if (L.glassBlur.isValid()) { L.panel (g, *this, L.str ("slabs.rail"), br); g.setColour (painted::Look::accentText (juce::Colour (0xff45E6FF))); g.setFont (painted::pfont (8.6f * s)); g.drawFittedText (d.blurb, br.reduced (7.0f * s, 0.0f).toNearestInt(), juce::Justification::centredLeft, 2, 0.75f); }
                else if (L.str ("slabs.paper").isNotEmpty()) { L.nineBody (g, L.str ("slabs.paper"), br); g.setColour (L.inkOn (L.str ("slabs.paper"))); g.setFont (painted::pfont (8.6f * s)); g.drawFittedText (d.blurb, br.reduced (7.0f * s, 0.0f).toNearestInt(), juce::Justification::centredLeft, 2, 0.75f); }
                else L.haloText (g, d.blurb, br, juce::Justification::centredLeft, 9.2f * s);
            }
            else if (blurbShown && blurbOnSlab && unitPage.pods) L.panel (g, *this, L.str ("slabs.rail"), blurbR.toFloat().withWidth (juce::GlyphArrangement::getStringWidth (painted::pfont (9.4f * s), d.blurb) + 16.0f * s).translated (-8.0f * s, 0.0f).expanded (0.0f, 2.0f * s));
            if (blurbShown && blurbWide) {}
            else if (blurbShown && blurbOnSlab) L.text (g, d.blurb, blurbR.toFloat(), juce::Justification::centredLeft, 9.4f * s, painted::Look::accentText (juce::Colour (0xff45E6FF)));
            else if (blurbShown) { if ((bool) L.at ("rack.blurbOnPaper")) L.paperText (g, d.blurb, blurbR.toFloat().withWidth (m.sx * 700.0f), 9.4f * s, true); else L.text (g, d.blurb, blurbR.toFloat(), juce::Justification::centredLeft, 9.4f * s, L.sub()); }
            if (const auto rail = L.str ("slabs.rail"); rail.isNotEmpty()) L.panel (g, *this, rail, railR.toFloat());
        }
        else
        {
            const char* n[] { "", "PARALLEL LANES: up to 3 lanes beside the main chain, each with gain, pan and mute, summed latency-aligned", "MODULATORS: 4 LFOs, 2 step sequencers, 2 envelope followers and 8 macros - any of them to any knob", "PERFORM" };
            if (titleOut && wideNow) L.paperText (g, n[mode], titleR.toFloat(), 10.4f * s, true, "tag");
            else { g.setColour (fxtheme::pal().ink); g.setFont (painted::pfont (12.0f * s)); g.drawFittedText (n[mode], titleR, juce::Justification::centredLeft, 1, 0.7f); }
        }
    }
    void paint (juce::Graphics& g) override
    {
        if (auto* L = painted::active()) { paintPainted (g, *L); return; }
        if (! ghost()) { g.setColour (col::bg); g.fillAll(); }
        drawScreen (g, card.toFloat(), accentOf (unitPage.unit), 12.0f);
        if (mode == 0)
        {
            const auto& d = unitDef (unitPage.unit);
            g.setColour (col::white.get()); g.setFont (font (16.0f)); g.drawText (d.title, titleR, juce::Justification::centredLeft, false);
            drawCaption (g, titleR.translated (0, 18).toFloat(), d.blurb, col::dim.get(), 9.5f);
        }
        else { const char* n[] { "", "PARALLEL LANES: up to 3 lanes beside the main chain, each with gain, pan and mute, summed latency-aligned", "MODULATORS: 4 LFOs, 2 step sequencers, 2 envelope followers and 8 macros - any of them to any knob", "PERFORM", "SPATIAL: place each unit's sound around your head - drag the tiles; record their moves while the song plays" }; g.setColour (col::white.get()); g.setFont (font (13.0f)); g.drawText (n[mode], titleR.withTrimmedRight (0), juce::Justification::centredLeft, false); }
        g.setColour (col::dim.get()); g.setFont (font (9.0f));
        if (mode != 4) g.drawText ("MACROS  -  right-click any knob to put it under a macro", macroR.withHeight (12).translated (0, -2), juce::Justification::centredLeft);
        g.drawText ("latency " + juce::String (proc.totalLatency()) + " smp   oversampling " + proc.apvts.getParameter ("os")->getCurrentValueAsText(), infoR, juce::Justification::centredRight);
    }
    void resized() override
    {
        if (auto* L = painted::active()) { resizedPainted (*L); return; }
        if (pvPane != nullptr) pvPane->setVisible (false);
        auto r = getLocalBounds().reduced (8);
        auto bar = r.removeFromTop (28); { auto b = bar.reduced (0, 2); presetBox.setBounds (b.removeFromLeft (juce::jlimit (170, 240, b.getWidth() / 4))); redoBtn.setBounds (b.removeFromRight (64)); b.removeFromRight (4); undoBtn.setBounds (b.removeFromRight (64)); b.removeFromRight (6); saveBtn.setBounds (b.removeFromRight (96)); b.removeFromRight (6); infoR = b.removeFromRight (230); b.removeFromLeft (10);
            for (auto* t : tabs) { const int tw = (int) ghosty::textWidth (font (12.0f), t->getButtonText()) + 24; t->setBounds (b.removeFromLeft (tw).reduced (2, 0)); } }
        if (mode == 4)
        {   // SPATIAL takes the strip's and the macros' room: its own tiles are the units
            card = r; auto c = r.reduced (10, 6); titleR = c.removeFromTop (30); spatialPage.setBounds (c);
            unitPreset.setVisible (false); unitAb.setVisible (false); return;
        }
        macroR = r.removeFromBottom (62); r.removeFromBottom (6);
        { auto m = macroR.withTrimmedTop (12); const int mw = m.getWidth() / kNumMacros; for (auto* k : macroKnobs) k->setBounds (m.removeFromLeft (mw)); }
        r.removeFromTop (4);
        const int stripH = strip.rows() == 2 || r.getWidth() < 1300 ? juce::jlimit (128, 170, getHeight() / 4) : juce::jlimit (64, 92, getHeight() / 8);
        strip.setBounds (r.removeFromTop (stripH)); r.removeFromTop (6);
        card = r; auto c = r.reduced (10, 6); auto h = c.removeFromTop (44); titleR = h;
        if (mode == 0)
        {
            unitPreset.setBounds (h.removeFromRight (juce::jlimit (170, 220, h.getWidth() / 5)).reduced (0, 9)); h.removeFromRight (6); unitAb.setBounds (h.removeFromRight (34).reduced (0, 9)); h.removeFromRight (8);
            if (unitOn) unitOn->setBounds (h.removeFromRight (54).reduced (0, 10)); h.removeFromRight (8);
            if (unitMix) { unitMix->setBounds (h.removeFromRight (juce::jlimit (150, 210, h.getWidth() / 4)).reduced (0, 10)); h.removeFromRight (8); }
            if (unitTrim) unitTrim->setBounds (h.removeFromRight (juce::jlimit (130, 180, h.getWidth() / 4)).reduced (0, 10));
            titleR = h;
        }
        unitPreset.setVisible (mode == 0); unitAb.setVisible (mode == 0);
        c.removeFromTop (4);
        unitPage.setBounds (c); lanes.setBounds (c); mods.setBounds (c); perform.setBounds (c); spatialPage.setBounds (c);
    }
    void themeChanged() { strip.repaint(); repaint(); }
    Strip strip; UnitPage unitPage;
    void showTab (int tab) { if (auto* b = tabs[juce::jlimit (0, 4, tab)]) b->setToggleState (true, juce::dontSendNotification); setMode (juce::jlimit (0, 4, tab)); }
    void showSpatial (int unit) { if (auto* b = tabs[4]) b->setToggleState (true, juce::dontSendNotification); setMode (4); spatialPage.selectUnit (unit); }
private:
    void refreshPresets()
    {
        presetBox.clear (juce::dontSendNotification); presetBox.addItem ("Init (everything off)", 1); presetBox.addSeparator();
        presetFiles = PhantomProcessor::presetFolder().findChildFiles (juce::File::findFiles, false, "*.phantomfx.json"); presetFiles.sort();
        if (! presetFiles.isEmpty()) presetBox.addSectionHeading ("YOUR PRESETS");
        for (int i = 0; i < presetFiles.size(); ++i) presetBox.addItem (presetFiles[i].getFileNameWithoutExtension().upToLastOccurrenceOf (".phantomfx", false, false), 10 + i);
        // the factory bank, grouped by each preset's first tag; the other tags follow the name
        juce::String group;
        for (int i = 0; i < PhantomProcessor::numFactoryPresets(); ++i)
        {
            const auto tags = PhantomProcessor::factoryPresetTags (i); const auto g = tags.isEmpty() ? juce::String ("factory") : tags[0];
            if (g != group) { group = g; presetBox.addSectionHeading ("FACTORY: " + g.toUpperCase()); }
            juce::StringArray rest (tags); rest.remove (0);
            presetBox.addItem (PhantomProcessor::factoryPresetName (i) + (rest.isEmpty() ? juce::String() : "   (" + rest.joinIntoString (", ") + ")"), kFactoryId + i);
        }
    }
    static constexpr int kFactoryId = 100000;
    void pickPreset() { const int id = presetBox.getSelectedId(); if (id == 1) proc.initAll(); else if (id >= kFactoryId) { if (proc.loadFactoryPreset (id - kFactoryId)) editor.toast ("Preset: " + PhantomProcessor::factoryPresetName (id - kFactoryId)); } else if (id >= 10 && id - 10 < presetFiles.size()) { if (! proc.loadPreset (presetFiles[id - 10])) editor.toast ("Could not read that preset"); } presetBox.setSelectedId (0, juce::dontSendNotification); strip.repaint(); }
    void savePreset()
    {
        auto* w = new juce::AlertWindow ("Save preset", "Name and tags (comma separated)", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", "My rack", "Name"); w->addTextEditor ("tags", "", "Tags"); w->addButton ("Save", 1); w->addButton ("Cancel", 0);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int r) { if (r == 1) { const auto name = w->getTextEditorContents ("name").trim(); juce::StringArray tags; tags.addTokens (w->getTextEditorContents ("tags"), ",", ""); tags.trim(); tags.removeEmptyStrings(); if (name.isNotEmpty()) { proc.savePreset (PhantomProcessor::presetFolder().getChildFile (juce::File::createLegalFileName (name) + ".phantomfx.json"), name, tags); refreshPresets(); editor.toast ("Saved: " + name); } } }), true);
    }
    juce::File unitPresetFolder (int u) const { return PhantomProcessor::presetFolder().getParentDirectory().getChildFile ("FX Presets").getChildFile (unitDef (u).key); }
    void refreshUnitPresets()
    {
        unitPreset.clear (juce::dontSendNotification); unitPreset.addItem ("Init", 1); unitPreset.addItem ("Save preset...", 2); unitPreset.addSeparator();
        unitPresetFiles = unitPresetFolder (unitPage.unit).findChildFiles (juce::File::findFiles, false, "*.json"); unitPresetFiles.sort();
        for (int i = 0; i < unitPresetFiles.size(); ++i) unitPreset.addItem (unitPresetFiles[i].getFileNameWithoutExtension(), 10 + i);
    }
    void pickUnitPreset()
    {
        const int id = unitPreset.getSelectedId(), u = unitPage.unit;
        if (id == 1) { proc.undo.beginNewTransaction ("Init unit"); for (auto& s : proc.rack.slot (u).params()) if (auto* p = proc.apvts.getParameter (PhantomProcessor::pid (u, s.id))) p->setValueNotifyingHost (p->getDefaultValue()); }
        else if (id == 2)
        {
            auto* w = new juce::AlertWindow ("Save unit preset", juce::String (unitDef (u).title), juce::MessageBoxIconType::NoIcon); w->addTextEditor ("name", "My " + juce::String (unitDef (u).shortName).toLowerCase(), "Name"); w->addButton ("Save", 1); w->addButton ("Cancel", 0);
            w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, u] (int r) { if (r == 1) { const auto name = w->getTextEditorContents ("name").trim(); if (name.isNotEmpty()) { auto f = unitPresetFolder (u).getChildFile (juce::File::createLegalFileName (name) + ".json"); f.getParentDirectory().createDirectory(); f.replaceWithText (juce::JSON::toString (proc.unitToJson (u))); refreshUnitPresets(); editor.toast ("Saved unit preset: " + name); } } }), true);
        }
        else if (id >= 10 && id - 10 < unitPresetFiles.size()) proc.unitFromJson (u, juce::JSON::parse (unitPresetFiles[id - 10]));
        unitPreset.setSelectedId (0, juce::dontSendNotification);
    }
    PhantomProcessor& proc; PhantomEditor& editor; LanesPage lanes; ModsPage mods; PerformPage perform; SpatialPage spatialPage;
    juce::OwnedArray<juce::TextButton> tabs; juce::ComboBox presetBox, unitPreset; juce::TextButton saveBtn { "SAVE PRESET" }, undoBtn { "UNDO" }, redoBtn { "REDO" }, unitAb { "A" };
    std::unique_ptr<Knob> unitMix, unitTrim; std::unique_ptr<Toggle> unitOn; juce::OwnedArray<Knob> macroKnobs;
    juce::Array<juce::File> presetFiles, unitPresetFiles; juce::Rectangle<int> card, titleR, macroR, infoR; int mode = 0;
    std::unique_ptr<PaintedPane> pvPane;
    juce::Rectangle<int> slabR, blurbR, railR; bool plateShown = false, wideNow = false, blurbShown = true, blurbOnSlab = false, blurbWide = false, titleOut = false;
};

// ================================================================ GHOST SCENES bar
class SceneBar : public juce::Component, private juce::Timer
{
public:
    static constexpr int kHeight = 44;
    SceneBar (PhantomProcessor& p) : proc (p), timeBox (p.apvts, "scene_time", col::pink.get()), quantBox (p.apvts, "scene_quant", col::pink.get())
    {
        addAndMakeVisible (timeBox); addAndMakeVisible (quantBox); startTimerHz (20);
    }
    juce::uint64 signature() const { juce::uint64 sig = (juce::uint64) (proc.activeScene() + 2) * 31u + (juce::uint64) (proc.sceneMorphProgress() * 200.0f); for (int i = 0; i < kScenes; ++i) sig = sig * 1099511628211ull + (juce::uint64) proc.sceneName (i).hashCode64() + (proc.sceneFilled (i) ? 1u : 0u); return sig; }
    void paintPainted (juce::Graphics& g, painted::Look& L)
    {   // PAINTED: scene buttons in the look's sprites: playing = pink, stored = violet, empty = wood
        const painted::Map m (*this); const float s = m.s();
        L.haloText (g, "GHOST SCENES", m.f (8, 731, 86, 14), juce::Justification::centredLeft, 9.6f * s, L.colour ("text.scenes", juce::Colour (0xfffa9ede)), juce::Colour (0xff150e20).withAlpha (0.85f));
        const int active = proc.activeScene(); const float prog = proc.sceneMorphProgress();
        for (int i = 0; i < kScenes; ++i)
        {
            const auto b = slotRect (i).toFloat(); const bool filled = proc.sceneFilled (i), act = i == active;
            const auto playing = L.str ("scenes.playing"), held = L.str ("scenes.held");
            if (act) L.nineBody (g, "button/on-" + (playing.isNotEmpty() ? playing : juce::String ("pink")), b, 1.0f, "button/on"); else if (filled) L.nineBody (g, "button/on-" + (held.isNotEmpty() ? held : juce::String ("violet")), b, 1.0f, "button/on"); else L.nineBody (g, "button/off", b);
            if (act && prog < 1.0f) { g.setColour (L.ink().withAlpha (0.55f)); g.fillRoundedRectangle (b.getX() + 6.0f, b.getBottom() - 9.0f * s, (b.getWidth() - 12.0f) * prog, 2.5f * s, 1.2f); }
            g.setColour (act ? L.ink() : filled && L.str ("scenes.held").isEmpty() ? L.ink() : filled ? L.ink() : L.cream()); g.setFont (painted::pfont (10.8f * s)); g.drawFittedText (juce::String (i + 1) + "  " + proc.sceneName (i), b.toNearestInt().reduced (6, 0).translated (0, -1), juce::Justification::centred, 1, 0.7f);
        }
        L.haloText (g, "MIDI C1-G1", midiR.toFloat(), juce::Justification::centred, 8.6f * s);
    }
    void paint (juce::Graphics& g) override
    {
        if (auto* L = painted::active()) { paintPainted (g, *L); return; }
        auto r = getLocalBounds();
        g.setColour (T (juce::Colour (0xff0d0f17))); g.fillRect (r); g.setColour (col::pink.withAlpha (0.25f)); g.drawHorizontalLine (0, 0, (float) getWidth());
        g.setColour (col::pink.get()); g.setFont (font (10.0f)); g.drawText ("GHOST SCENES", r.removeFromLeft (92).reduced (8, 0), juce::Justification::centredLeft);
        const int active = proc.activeScene(); const float prog = proc.sceneMorphProgress();
        for (int i = 0; i < kScenes; ++i)
        {
            auto s = slotRect (i).toFloat().reduced (3, 7); const bool filled = proc.sceneFilled (i), act = i == active;
            g.setColour (act ? col::pink.withAlpha (0.35f) : filled ? col::card2.get() : col::card.withAlpha (0.6f)); g.fillRoundedRectangle (s, 6);
            if (act && prog < 1.0f) { g.setColour (col::pink.withAlpha (0.6f)); g.fillRoundedRectangle (s.withWidth (s.getWidth() * prog), 6); }
            g.setColour (filled ? col::pink.get() : col::dim.withAlpha (0.5f)); g.drawRoundedRectangle (s, 6, act ? 1.6f : 1.0f);
            g.setColour (filled ? col::white.get() : col::dim.get()); g.setFont (font (10.0f)); g.drawText (juce::String (i + 1) + "  " + proc.sceneName (i), s, juce::Justification::centred, false);
        }
        g.setColour (col::dim.get()); g.setFont (font (9.0f)); g.drawText ("MIDI C1-G1", midiR, juce::Justification::centred);
    }
    juce::Rectangle<int> slotRect (int i) const { if (painted::active() != nullptr) return painted::Map (*this).r (95.0f + 101.0f * (float) i, 723, 95, 29); auto r = getLocalBounds(); r.removeFromLeft (92); r.removeFromRight (300); const int w = r.getWidth() / kScenes; return { r.getX() + i * w, 0, w, kHeight }; }
    void resized() override { if (painted::active() != nullptr) { const painted::Map m (*this); timeBox.setBounds (m.r (927, 725, 103, 25)); quantBox.setBounds (m.r (1037, 725, 83, 25)); midiR = m.r (1124, 731, 72, 14); return; } auto r = getLocalBounds().removeFromRight (300).reduced (6, 10); midiR = r.removeFromRight (70); quantBox.setBounds (r.removeFromRight (90).reduced (3, 0)); timeBox.setBounds (r.removeFromRight (110).reduced (3, 0)); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < kScenes; ++i) if (slotRect (i).contains (e.getPosition()))
        {
            if (e.mods.isPopupMenu())
            {
                juce::PopupMenu m; m.addItem (1, "Store what you hear here"); m.addItem (2, "Rename..."); m.addItem (3, "Clear", proc.sceneFilled (i));
                m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, i] (int r) { if (r == 1) proc.storeScene (i); else if (r == 3) proc.clearScene (i); else if (r == 2) { auto* w = new juce::AlertWindow ("Rename scene", "", juce::MessageBoxIconType::NoIcon); w->addTextEditor ("name", proc.sceneName (i)); w->addButton ("OK", 1); w->addButton ("Cancel", 0); w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, i] (int rr) { if (rr == 1) proc.renameScene (i, w->getTextEditorContents ("name").trim().toUpperCase()); repaint(); }), true); } repaint(); });
                return;
            }
            proc.recallScene (i); repaint(); return;
        }
    }
private:
    void timerCallback() override { if (painted::active() != nullptr) { const auto sig = signature(); if (sig == lastSig) return; lastSig = sig; } repaint(); }
    PhantomProcessor& proc; Choice timeBox, quantBox; juce::Rectangle<int> midiR; juce::uint64 lastSig = 0;
};

// ================================================================ the editor
static juce::File lookFile() { return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("GhostBlock").getChildFile ("Phantom FX").getChildFile ("look.settings"); }
juce::String PhantomEditor::creditsText()
{
    return "PHANTOM FX v" + juce::String ("1 beta") + " by GhostBlock. Free software: AGPL-3.0-or-later. Source: github.com/bsaxon430/ghosty-vst (phantom-fx)\n\n"
           "Built with:\nJUCE 8.0.4 (Raw Material Software, AGPL-3.0) - github.com/juce-framework/JUCE\n"
           "chowdsp_utils (Jatin Chowdhury; BSD-3-Clause / GPL-3.0 per module) - github.com/Chowdhury-DSP/chowdsp_utils\n"
           "Signalsmith DSP (Geraint Luff, MIT) - github.com/Signalsmith-Audio/dsp\n"
           "sst-basic-blocks, sst-filters, sst-waveshapers (Surge Synth Team, GPL-3.0) - github.com/surge-synthesizer\n"
           "Airwindows (Chris Johnson, MIT) - github.com/airwindows/airwindows\n"
           "ChowTapeModel (Jatin Chowdhury, GPL-3.0) - github.com/jatinchowdhury18/AnalogTapeModel\n"
           "sst-effects (Surge Synth Team, GPL-3.0) - github.com/surge-synthesizer/sst-effects\n"
           "Mutable Instruments eurorack code (Emilie Gillet, MIT; Surge Synth Team's desktop fork) - github.com/surge-synthesizer/eurorack\n"
           "CloudSeedCore (Ghost Note Audio, MIT) - github.com/GhostNoteAudio/CloudSeedCore\n"
           "Steam Audio (Valve Corporation, Apache-2.0; SPATIAL) - github.com/ValveSoftware/steam-audio\n"
           "PFFFT (Julien Pommier, Hayati Ayguen, Dario Mambro; BSD-style; Steam Audio's FFT) - github.com/marton78/pffft\n"
           "CIPIC HRTF Database (The Regents of the University of California; Steam Audio's built-in HRTF)\n"
           "Dragonfly Reverb (Michael Willis, GPL-3.0) with freeverb3 (Teru Kamogashira et al., GPL-2.0-or-later) - github.com/michaelwillis/dragonfly-reverb\n"
           "FFTConvolver (HiFi-LoFi, MIT) - github.com/HiFi-LoFi/FFTConvolver\n"
           "Signalsmith Stretch and Signalsmith Linear (Geraint Luff, MIT) - github.com/Signalsmith-Audio\n"
           "GATE-12 and TIME-12 (tiagolr, GPL-3.0) - github.com/tiagolr/gate12, github.com/tiagolr/time12\n"
           "Mango (Olivier Doare, AGPL-3.0-or-later) with FxmeTools core (LGPL-3.0-or-later) - github.com/odoare/Mango, github.com/odoare/FxmeTools\n"
           "WDL FFT and reverb engine (Cockos, zlib) - github.com/odoare/WDL\n"
           "vitOTTx (Sakhnovkrg, GPL-3.0; the multiband compressor of Vital by Matt Tytel, GPL-3.0) - github.com/Sakhnovkrg/vitOTTx, github.com/mtytel/vital\n"
           "ZL Limiter (ZL Audio / zsliu98, AGPL-3.0) with Highway (Google, Apache-2.0 / BSD-3-Clause) - github.com/ZL-Audio/ZLLimiter, github.com/google/highway\n"
           "LSP dsp-units, lsp-dsp-lib, lsp-common-lib (Vladimir Sadovnikov, LGPL-3.0) - github.com/lsp-plugins\n"
           "Rubber Band Library (Breakfast Quay, GPL-2.0-or-later) - github.com/breakfastquay/rubberband\n"
           "{fmt} (Victor Zverovich and contributors, MIT) - github.com/fmtlib/fmt\n"
           "Zen Maru Gothic (The Zen Maru Gothic Project Authors, SIL Open Font License 1.1; the typeface of the DAWN and CEL looks) - github.com/googlefonts/zen-marugothic\n"
           "Inter (The Inter Project Authors, SIL Open Font License 1.1; the typeface of the NEON look) - github.com/rsms/inter\n"
           "clap-juce-extensions (free-audio, MIT) - github.com/free-audio/clap-juce-extensions\nVST3 SDK (Steinberg, GPL-3.0 option)\n\n"
           "The full list with licence texts ships in THIRD_PARTY.md and the Licenses folder.";
}

void PhantomEditor::showUnit (int u) { if (panel) panel->showUnit (u); }
void PhantomEditor::showTab (int tab) { if (panel) panel->showTab (tab); }
int PhantomEditor::numLooks() const { return lookCount(); }
int PhantomEditor::firstLook() const { return phantom::firstLook(); }
juce::String PhantomEditor::lookTitle (int l) const { return lookName (l); }
void PhantomEditor::showSpatial (int unit) { panel->showSpatial (unit); }

PhantomEditor::PhantomEditor (PhantomProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    inK = std::make_unique<Knob> (proc.apvts, "in", "IN", col::slime.get()); outK = std::make_unique<Knob> (proc.apvts, "out", "OUT", col::ice.get()); mixK = std::make_unique<Knob> (proc.apvts, "mix", "MIX", col::pink.get());
    for (auto* k : { inK.get(), outK.get(), mixK.get() }) { k->slider.setSliderStyle (juce::Slider::LinearHorizontal); k->slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 16); k->getProperties().set ("pfxDark", true); k->slider.getProperties().set ("pfxDark", true); addAndMakeVisible (k); }
    panel = std::make_unique<RackPanel> (proc, *this); addAndMakeVisible (*panel);
    scenes = std::make_unique<SceneBar> (proc); addAndMakeVisible (*scenes);
    snapBtn.onClick = [this] { const auto name = "Snap " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H.%M.%S"); proc.saveSnapshot (name); toast ("Snapshot saved: " + name); snapBtn.setButtonText ("SAVED"); juce::Timer::callAfterDelay (900, [this] { snapBtn.setButtonText ("SNAPSHOT"); }); };
    snapList.onClick = [this] { juce::PopupMenu m; auto files = proc.listSnapshots(); files.sort(); for (int i = 0; i < files.size(); ++i) m.addItem (10 + i, files[i].getFileNameWithoutExtension().upToLastOccurrenceOf (".phantomfx", false, false)); if (files.isEmpty()) m.addItem (1, "(no snapshots yet)", false); m.addSeparator(); m.addItem (2, "Show the folder"); m.addSeparator(); m.addItem (3, "Unit visuals (the living picture on a unit's page)", true, unitVisualsOn());
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&snapList), [this, files] (int r) { if (r >= 10 && r - 10 < files.size()) proc.loadPreset (files[r - 10]); else if (r == 2) { PhantomProcessor::snapshotFolder().createDirectory(); PhantomProcessor::snapshotFolder().revealToUser(); } else if (r == 3) setUnitVisuals (! unitVisualsOn()); }); };
    abBtn.onClick = [this] { proc.abToggle(); }; abCopy.onClick = [this] { proc.abCopy(); toast ("Copied " + juce::String (proc.abCurrent() == 0 ? "A to B" : "B to A")); };
    themeBtn.onClick = [this] { setLook (nextLook (shownLook)); };
    aboutBtn.setTooltip ("About / Credits"); aboutBtn.onClick = [] { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "About Phantom FX", creditsText()); };
    bypassBtn.setClickingTogglesState (true); bypassAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "bypass", bypassBtn);
    for (auto* b : { &snapBtn, &snapList, &abBtn, &abCopy, &themeBtn, &aboutBtn, &bypassBtn }) addAndMakeVisible (b);
    setResizable (true, true); setResizeLimits (kMinW, kMinH, 1900, 1200);
    setSize (PhantomProcessor::kDefaultWidth, PhantomProcessor::kDefaultHeight);
    themeBtn.getProperties().set ("fitText", true);
    panel->setDirtySink ([this] (juce::Component* c, juce::Rectangle<int> r) { if (forceAnim) dirtyLog.add (getLocalArea (c, r)); });
    {   // the look is a UI preference (never sound), remembered per user; a painted look is remembered by its folder name
        juce::PropertiesFile props (lookFile(), {}); int l = props.getIntValue ("look", -1); const int byName = looks->indexOf (props.getValue ("lookName"));
        unitVisualsOn() = props.getBoolValue ("unitVisuals", true); panel->unitPage.onVisualsOff = [this] { setUnitVisuals (false); };
        // (a preference written before 0.1.10 may name BLACK, PURPLE, LIGHT or GHOST, or a painted look that is gone: DAWN then)
        if (byName >= 0) l = fxtheme::kFirstPainted + byName; else if (l >= fxtheme::kFirstPainted) l = -1;
        setLook (l);
    }
    startTimerHz (30);
}
PhantomEditor::~PhantomEditor() { stopTimer(); proc.rack.setUiLevels (false); setLookAndFeel (nullptr); }

void PhantomEditor::setLook (int l)
{
    if (! lookOffered (l)) l = defaultLook();
    if (l < fxtheme::kFirstPainted) l = l == kLookGhost ? kLookGhost : ghosty::themes::toInt (ghosty::themes::fromInt (l));
    const bool isPainted = l >= fxtheme::kFirstPainted;
    shownLook = l; fxtheme::setLook (l); painted::syncFont();
    juce::String all; for (int i = phantom::firstLook(); i < lookCount(); ++i) all << (all.isNotEmpty() ? " -> " : "") << lookName (i);
    themeBtn.setButtonText (lookName (nextLook (l))); themeBtn.setTooltip ("Look: " + lookName (l) + ". Click for " + lookName (nextLook (l)) + " (" + all + ")");
    lnf.applyTheme(); fxtheme::retint (*this);
    // a painted look keeps the painting's shape (1200 : 760) at every size; the drawn looks resize freely as before
    if (auto* c = getConstrainer()) c->setFixedAspectRatio (isPainted ? (double) painted::kW / (double) painted::kH : 0.0);
    if (isPainted) { setResizeLimits (1100, 697, 1900, 1203); const int h = juce::roundToInt ((float) getWidth() * painted::kH / painted::kW); if (std::abs (h - getHeight()) > 2) setSize (getWidth(), h); }
    else setResizeLimits (kMinW, kMinH, 1900, 1200);
    bgCaches.clear(); glassCaches.clear(); overlayCache = {}; overlayKey = {};
    for (auto& lk : looks->looks) lk->clearSized();
    proc.rack.setUiLevels (false);
    panel->refreshHeader(); resized();
    std::function<void (juce::Component&)> relayout = [&] (juce::Component& c) { c.resized(); for (auto* k : c.getChildren()) relayout (*k); };   // every part lays itself out for the look
    relayout (*panel); relayout (*scenes); for (auto* k : { inK.get(), outK.get(), mixK.get() }) k->resized();
    panel->themeChanged(); sendLookAndFeelChange(); repaint();
    { juce::PropertiesFile props (lookFile(), {}); props.setValue ("look", l); props.setValue ("lookName", isPainted ? looks->looks[(size_t) (l - fxtheme::kFirstPainted)]->id : juce::String()); props.saveIfNeeded(); }
}
void PhantomEditor::dirty (juce::Rectangle<int> r) { repaint (r); if (forceAnim) dirtyLog.add (r); }
bool PhantomEditor::unitVisuals() const { return unitVisualsOn(); }
void PhantomEditor::setUnitVisuals (bool on)
{
    unitVisualsOn() = on; { juce::PropertiesFile props (lookFile(), {}); props.setValue ("unitVisuals", on); props.saveIfNeeded(); }
    std::function<void (juce::Component&)> relayout = [&] (juce::Component& c) { c.resized(); for (auto* k : c.getChildren()) relayout (*k); }; relayout (*panel); repaint();
}
juce::String PhantomEditor::pageKey (painted::Look& L) const
{
    if (panel->currentMode() == 4) return "spatial";
    return panel->isWide() && (bool) L.at ("rack.wideIsPage") ? "rack-wide" : "rack";
}
// the rain's rectangle: on a wide page only the part above the slab (the slab hides the rest; its controls are not repainted for it)
juce::Rectangle<float> PhantomEditor::rainRect (painted::Look& L, const juce::var& ra, const juce::String& page) const
{
    auto num = [] (const juce::var& v, const char* k, int i, float def) { if (auto* a = v.getProperty (k, {}).getArray()) if (i < a->size()) return (float) (double) (*a)[i]; return def; };
    const auto n = ra.getProperty ("img", {}).toString(); const int fr = juce::jmax (1, (int) ra.getProperty ("frames", 4));
    juce::Rectangle<float> r (num (ra, "at", 0, 0), num (ra, "at", 1, 0), L.img (n).w() / (float) fr, L.img (n).h());
    if (page == "rack-wide") r = r.withBottom (juce::jmin (r.getBottom(), L.el ("rack.wide", 1, 384.0f)));
    return r;
}
// every scene ghost on a page, in design px (1200 x 760): the main one and any drawn as a pose strip
std::vector<juce::Rectangle<float>> PhantomEditor::sceneGhostRects (painted::Look& L, const juce::String& page) const
{
    std::vector<juce::Rectangle<float>> out;
    if (const juce::String key = "anim.hatch." + page; L.has (key)) { const float k = L.el (key, 2, 1.0f); out.push_back ({ L.el (key, 0) - L.el ("anim.hatchAnchor", 0, 58.0f) * k, L.el (key, 1) - L.el ("anim.hatchAnchor", 1, 62.0f) * k, 110.0f * k, 130.0f * k }); }
    auto num = [] (const juce::var& v, const char* k, int i, float def) { if (auto* a = v.getProperty (k, {}).getArray()) if (i < a->size()) return (float) (double) (*a)[i]; return def; };
    if (auto* sa = L.at ("anim.sprites").getArray()) for (auto& sv : *sa)
    {
        if (! (bool) sv.getProperty ("strip", false)) continue; bool here = true;
        if (auto* a = sv.getProperty ("pages", {}).getArray()) { here = false; for (auto& x : *a) if (x.toString() == page) here = true; }
        if (! here) continue; const float k = (float) (double) sv.getProperty ("scale", 1.0);
        out.push_back ({ num (sv, "at", 0, 0) - num (sv, "c", 0, 0) * k, num (sv, "at", 1, 0) - num (sv, "c", 1, 0) * k, num (sv, "cell", 0, 110) * k, num (sv, "cell", 1, 130) * k });
    }
    return out;
}
juce::Rectangle<int> PhantomEditor::blurbAreaForTest() const { return panel->blurbArea().isEmpty() ? juce::Rectangle<int>() : getLocalArea (panel.get(), panel->blurbArea()); }
std::vector<juce::Rectangle<int>> PhantomEditor::sceneGhostAreasForTest() const
{
    std::vector<juce::Rectangle<int>> out; auto* L = painted::active(); if (L == nullptr) return out;
    const painted::Map m (*this); for (auto r : sceneGhostRects (*L, pageKey (*L))) out.push_back (m.f (r).getSmallestIntegerContainer());
    return out;
}
juce::Rectangle<float> PhantomEditor::hatchRect (painted::Look& L, const juce::String& page) const
{
    const juce::String key = "anim.hatch." + page; if (! L.has (key)) return {};
    const painted::Map m (*this); const float k = L.el (key, 2, 1.0f) * m.s(); const auto c = m.p (L.el (key, 0), L.el (key, 1));
    return { c.x - L.el ("anim.hatchAnchor", 0, 58.0f) * k, c.y - L.el ("anim.hatchAnchor", 1, 62.0f) * k, 110.0f * k, 130.0f * k };
}
// PAINTED life, on this UI timer only, 12 times a second (ASSETS.md "Animation"): the lamp flickers, the mist drifts, leaves fall,
// the ghosts bounce to the level meters. Paused while the window is hidden or minimised. Only the areas that changed are repainted.
void PhantomEditor::animStep()
{
    auto* L = painted::active(); if (L == nullptr) { proc.rack.setUiLevels (false); return; }
    auto* peer = getPeer(); const bool visible = forceAnim || (isShowing() && peer != nullptr && ! peer->isMinimised());
    proc.rack.setUiLevels (visible);
    if (! visible) return;
    animAcc += 12.0 / 30.0; if (animAcc < 1.0) return; animAcc -= 1.0; ++tick;
    const painted::Map m (*this); const juce::String page = pageKey (*L);
    auto onPage = [&] (const juce::var& v) { if (auto* a = v.getProperty ("pages", {}).getArray()) { for (auto& x : *a) if (x.toString() == page) return true; return false; } return true; };
    auto num = [] (const juce::var& v, const char* k, int i, float def) { if (auto* a = v.getProperty (k, {}).getArray()) if (i < a->size()) return (float) (double) (*a)[i]; return def; };
    auto each = [] (const juce::var& v, const std::function<void (const juce::var&)>& f) { if (auto* a = v.getArray()) { for (auto& x : *a) f (x); } else if (v.isObject()) f (v); };   // (one entry or a list of them)
    if (tick % 3 == 0 && L->has ("anim.lamp." + page)) dirty (m.sq (L->el ("anim.lamp." + page, 0), L->el ("anim.lamp." + page, 1), 164, 164).toNearestInt());
    if (tick % 2 == 0)
    {
        if (L->listHas ("anim.mist.pages", page)) dirty (m.r (L->num ("anim.mist.x0", 40), L->num ("anim.mist.y", 470), L->num ("anim.mist.x1", 760) - L->num ("anim.mist.x0", 40), 80));
        if ((bool) L->at ("anim.leaves")) for (int i = 0; i < 7; ++i) { dirty (m.f (painted::leafAt (i, tick - 2).r).expanded (2.0f).toNearestInt()); dirty (m.f (painted::leafAt (i, tick).r).expanded (2.0f).toNearestInt()); }
    }
    // the scene's ghosts listen to the output meter: squat, hit, settle on a peak, a slow beat while sound flows, idling in silence
    const float lv = juce::jmax (outLv[0], outLv[1]);
    if (lv > 0.02f && tick - hatchTrig > 10 && (lv > hatchSlow * 1.3f || tick - hatchTrig > 21)) hatchTrig = tick;
    hatchSlow += (lv - hatchSlow) * 0.3f;
    if (const int f = painted::poseAfter (tick - hatchTrig, tick, 3); f != hatchFrame)
    {
        hatchFrame = f; dirty (hatchRect (*L, page).expanded (2.0f).toNearestInt());
        if (const juce::String sk = "anim.shoulder." + page; L->has (sk)) { const float k = L->el (sk, 2, 0.8f); dirty (m.f (L->el (sk, 0) - 36.0f * k, L->el (sk, 1) - 44.0f * k, 74.0f * k, 90.0f * k).toNearestInt()); }
        each (L->at ("anim.sprites"), [&] (const juce::var& sv)
        {   // (further scene ghosts drawn as pose strips)
            if (! onPage (sv) || ! (bool) sv.getProperty ("strip", false) || (bool) sv.getProperty ("asleep", false)) return;
            const float k = (float) (double) sv.getProperty ("scale", 1.0);
            dirty (m.f (num (sv, "at", 0, 0) - num (sv, "c", 0, 0) * k, num (sv, "at", 1, 0) - num (sv, "c", 1, 0) * k, num (sv, "cell", 0, 110) * k, num (sv, "cell", 1, 130) * k).expanded (2.0f).toNearestInt());
        });
    }
    // the city: flicker patches and the logo (only on the ticks their table changes), the occasional train, the hologram, the rain
    if (auto* pa = L->at ("anim.patches").getArray()) for (auto& pv : *pa)
    {
        if (! onPage (pv)) continue; const auto tb = pv.getProperty ("table", {}).toString(); const int per = (int) pv.getProperty ("period", 0);
        if (L->tableAt (tb, tick, per) != L->tableAt (tb, tick - 1, per)) { const auto n = pv.getProperty ("img", {}).toString(); dirty (m.r (num (pv, "at", 0, 0), num (pv, "at", 1, 0), L->img (n).w(), L->img (n).h())); }
    }
    if (const auto tb = L->str ("anim.logoDim.table"); tb.isNotEmpty()) { const int per = (int) L->num ("anim.logoDim.period", 0); if (L->tableAt (tb, tick, per) != L->tableAt (tb, tick - 1, per)) dirty (m.r (0, 0, 256, 66)); }
    each (L->at ("anim.train"), [&] (const juce::var& tr)
    {   // it passes only now and then (every 30 to 60 s), never on a loop
        if (! onPage (tr)) return;
        const float lo = (float) (double) tr.getProperty ("everyMin", 20.0), hi = (float) (double) tr.getProperty ("everyMax", 40.0);
        if (trainStart < 0) trainStart = tick + 12 * rng.nextInt ({ 4, 12 });
        const auto n = tr.getProperty ("img", {}).toString(); const float c0 = num (tr, "clip", 0, 0), c1 = num (tr, "clip", 1, 1200), y = (float) (double) tr.getProperty ("y", 0.0);
        const int len = (int) (((float) (double) tr.getProperty ("from", 1082.0) - c0 + L->img (n).w()) / juce::jmax (1.0f, (float) (double) tr.getProperty ("speed", 30.0))) + 1;
        if (tick >= trainStart && tick <= trainStart + len) dirty (m.r (c0, y, c1 - c0, L->img (n).h()).expanded (1));
        if (tick > trainStart + len) trainStart = tick + (int) (12.0f * (lo + (hi - lo) * rng.nextFloat()));
    });
    {   // the hologram: a colour swap now and then, a glitch frame at irregular moments; a little brighter when the music is louder
        const int t42 = tick % 42, t97 = tick % 97; const int f = (t42 == 15 || t42 == 16 || t97 == 30 || t97 == 71) ? 2 : (t97 >= 28 && t97 <= 34) ? 1 : 0;
        const int gs = juce::jlimit (0, 3, (int) (juce::jmax (outLv[0], outLv[1]) * 5.0f));
        if (f != holoFrame || gs != glowStep)
        {
            holoFrame = f; glowStep = gs;
            each (L->at ("anim.holo"), [&] (const juce::var& ho) { if (! onPage (ho)) return; const auto n = ho.getProperty ("a", {}).toString(); dirty (m.r (num (ho, "at", 0, 0), num (ho, "at", 1, 0), L->img (n).w(), L->img (n).h())); });
        }
    }
    if (auto* st = L->at ("anim.strips").getArray()) for (auto& sv : *st)
    {
        if (! onPage (sv) || tick % juce::jmax (1, (int) sv.getProperty ("div", 2)) != 0) continue; const auto n = sv.getProperty ("img", {}).toString(); const int fr = juce::jmax (1, (int) sv.getProperty ("frames", 1));
        dirty (m.r (num (sv, "at", 0, 0), num (sv, "at", 1, 0), L->img (n).w() / (float) fr, L->img (n).h()));
    }
    if (tick % 2 == 0) each (L->at ("anim.rain"), [&] (const juce::var& ra) { if (onPage (ra)) dirty (m.r (rainRect (*L, ra, page)).getIntersection (getLocalBounds())); });
    panel->animTick (tick);
}
void PhantomEditor::toast (const juce::String& s) { toastText = s; toastUntil = t + 2.5; repaint (mixHint); }
void PhantomEditor::timerCallback()
{
    t += 1.0 / 30.0;
    inLv[0] = juce::jmax (proc.inMeter.peakL.load(), inLv[0] * 0.86f); inLv[1] = juce::jmax (proc.inMeter.peakR.load(), inLv[1] * 0.86f);
    outLv[0] = juce::jmax (proc.outMeter.peakL.load(), outLv[0] * 0.86f); outLv[1] = juce::jmax (proc.outMeter.peakR.load(), outLv[1] * 0.86f);
    outTp[0] = juce::jmax (proc.outMeter.truePeakL.load(), outTp[0] * 0.86f); outTp[1] = juce::jmax (proc.outMeter.truePeakR.load(), outTp[1] * 0.86f);
    repaint (inMeter.expanded (4, 4)); repaint (outMeter.expanded (4, 4)); repaint (mixHint);
    if (shownLook >= fxtheme::kFirstPainted) { fxtheme::setLook (shownLook); animStep(); } else repaint (logo);
    const bool onB = proc.abCurrent() == 1; if (abBtn.getButtonText() != (onB ? "B" : "A")) { abBtn.setButtonText (onB ? "B" : "A"); abCopy.setButtonText (onB ? "B>A" : "A>B"); }
}
void PhantomEditor::drawLogo (juce::Graphics& g, juce::Rectangle<float> r, double tt)
{
    // Ghost Box's logo: a cute ghost peeking out of a glowing box
    const float s = r.getHeight(); auto box = juce::Rectangle<float> (r.getX() + 4, r.getY() + s * 0.48f, s * 1.05f, s * 0.48f); const float bob = (float) std::sin (tt * 2.2) * s * 0.03f;
    auto L = ghosty::lookFor (0); L.eyes = 3; L.mouth = 1; ghosty::drawGhost (g, L, box.getCentreX(), r.getY() + s * 0.40f + bob, s * 0.72f, 0, false, 1.0f);
    juce::ColourGradient bg (juce::Colour (0xff3a2a5c), box.getX(), box.getY(), juce::Colour (0xff1a1430), box.getX(), box.getBottom(), false); g.setGradientFill (bg); g.fillRoundedRectangle (box, 4);
    g.setColour (col::slime.withAlpha (0.85f)); g.drawRoundedRectangle (box, 4, 1.6f);
    juce::Path flaps; flaps.startNewSubPath (box.getX(), box.getY()); flaps.lineTo (box.getX() - s * 0.12f, box.getY() - s * 0.12f); flaps.startNewSubPath (box.getRight(), box.getY()); flaps.lineTo (box.getRight() + s * 0.12f, box.getY() - s * 0.12f);
    g.setColour (col::slime.withAlpha (0.7f)); g.strokePath (flaps, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (col::pink.withAlpha (0.7f)); g.fillEllipse (box.getCentreX() - 2.5f, box.getCentreY() - 2.5f, 5, 5);
}
void PhantomEditor::drawMeter (juce::Graphics& g, juce::Rectangle<float> r, const float* lv, const float* tp)
{
    for (int c = 0; c < 2; ++c)
    {
        auto b = r.removeFromTop (r.getHeight() * (c == 0 ? 0.5f : 1.0f)).reduced (0, 1);
        g.setColour (T (juce::Colour (0xff12141c))); g.fillRoundedRectangle (b, 2);
        const float db = gpfx::gainToDb (lv[c]), f = gpfx::clampf ((db + 60.0f) / 60.0f, 0.0f, 1.0f);
        g.setColour ((db > -1.0f ? col::pink : db > -9.0f ? col::gold : col::slime).withAlpha (0.9f)); g.fillRoundedRectangle (b.withWidth (b.getWidth() * f), 2);
        if (tp) { const float tdb = gpfx::gainToDb (tp[c]), tf = gpfx::clampf ((tdb + 60.0f) / 60.0f, 0.0f, 1.0f); g.setColour (tdb > -0.1f ? col::pink.get() : col::white.withAlpha (0.8f)); g.fillRect (b.getX() + b.getWidth() * tf - 1.0f, b.getY(), 2.0f, b.getHeight()); }   // true-peak tick
    }
}
void PhantomEditor::paintPainted (juce::Graphics& g, painted::Look& L)
{
    const painted::Map m (*this); const float s = m.s(); const bool spatial = panel->currentMode() == 4; const juce::String page = pageKey (L);
    // the page's painting, scaled once to the window's own pixels and kept (per page, until the size or the look changes): after
    // that, drawing it is a plain copy of an opaque picture
    painted::trace ("editor: start");
    const float ds = g.getInternalContext().getPhysicalPixelScaleFactor() > 1.4f ? 2.0f : 1.0f; L.pixelScale = ds;
    const juce::String key = L.id + page + juce::String (getWidth()) + "x" + juce::String (getHeight()) + "@" + juce::String (ds, 2);
    if (bgCaches.size() > 6) bgCaches.clear(); if (glassCaches.size() > 6) glassCaches.clear();
    auto bgIt = bgCaches.find (key);
    if (bgIt == bgCaches.end())
    {
        const juce::String bn = L.hasFile ("bg/" + page) ? "bg/" + page : juce::String ("bg/rack"); const auto& src = L.img (bn);
        bgIt = bgCaches.emplace (key, src.ok() ? painted::Look::resample (src.im, src.im.getBounds(), juce::roundToInt ((float) getWidth() * ds), juce::roundToInt ((float) getHeight() * ds), true) : juce::Image()).first;
        L.drop (bn);   // (the full-size picture is not kept in memory)
    }
    if (bgIt->second.isValid()) g.drawImage (bgIt->second, getLocalBounds().toFloat()); else g.fillAll (fxtheme::pal().window);
    painted::trace ("editor: background drawn");
    // smoked glass (looks that have it): the page's blurred picture, scaled once, for every panel drawn in this paint pass
    if (const auto bl0 = L.str ("glass.blur"); bl0.isNotEmpty())
    {
        auto glIt = glassCaches.find (key);
        if (glIt == glassCaches.end())
        {
            const juce::String bl = L.hasFile (bl0.replace ("{page}", page)) ? bl0.replace ("{page}", page) : bl0.replace ("{page}", "rack"); const auto& src = L.img (bl);
            glIt = glassCaches.emplace (key, src.ok() ? painted::Look::resample (src.im, src.im.getBounds(), juce::roundToInt ((float) getWidth() * ds), juce::roundToInt ((float) getHeight() * ds), true) : juce::Image()).first; L.drop (bl);
        }
        L.glassBlur = glIt->second;
    }
    else L.glassBlur = {};
    // mist, lamp, the machine's ghosts, leaves: behind the slabs and controls
    if (L.listHas ("anim.mist.pages", page) && L.hasImg ("anim/mist-strip"))
    {
        const float x0 = L.num ("anim.mist.x0", 40), x1 = L.num ("anim.mist.x1", 760), y = L.num ("anim.mist.y", 470), al = L.num ("anim.mist.alpha", 1.0f), wrap = L.img ("anim/mist-strip").w();
        const float off = std::fmod ((float) (tick / 2) * 62.0f, wrap); const float fade = juce::jmin (120.0f, (x1 - x0) * 0.25f);
        for (int band = 0; band < 9; ++band)
        {   // fading at both ends, in steps
            const float fa = band < 4 ? 0.2f + 0.2f * (float) band : band > 4 ? 0.2f + 0.2f * (float) (8 - band) : 1.0f;
            const float bx0 = band < 4 ? x0 + fade * 0.25f * (float) band : band == 4 ? x0 + fade : x1 - fade + fade * 0.25f * (float) (band - 5), bx1 = band < 4 ? bx0 + fade * 0.25f : band == 4 ? x1 - fade : bx0 + fade * 0.25f;
            juce::Graphics::ScopedSaveState ss (g); g.reduceClipRegion (m.r (bx0, y, bx1 - bx0, 80));
            for (float x = x0 - off; x < x1; x += wrap) L.draw (g, "anim/mist-strip", m.f (x, y, wrap, 80), al * fa);
        }
    }
    // a city look's street life (look.json "anim"): the train between the buildings, signs that flicker (patches of the picture
    // with that sign out), the hologram, the passers-by, the rain
    auto onPage = [&] (const juce::var& v) { if (auto* a = v.getProperty ("pages", {}).getArray()) { for (auto& x : *a) if (x.toString() == page) return true; return false; } return true; };
    auto num = [] (const juce::var& v, const char* k, int i, float def) { if (auto* a = v.getProperty (k, {}).getArray()) if (i < a->size()) return (float) (double) (*a)[i]; return def; };
    auto each = [] (const juce::var& v, const std::function<void (const juce::var&)>& f) { if (auto* a = v.getArray()) { for (auto& x : *a) f (x); } else if (v.isObject()) f (v); };
    // while a unit's painted visual shows, what stood where its stage is is not drawn (look.json "visual.skipWhileOn": the hatch
    // ghost, the window's pose-strip ghosts, the hologram, the train)
    const bool vis = panel->pvShowing() && (page == "rack" || panel->pvOnWideStage());
    each (L.at ("anim.train"), [&] (const juce::var& tr)
    {
        if (vis || ! onPage (tr) || trainStart < 0 || tick < trainStart) return;
        const auto n = tr.getProperty ("img", {}).toString(); const float y = (float) (double) tr.getProperty ("y", 0.0), x = (float) (double) tr.getProperty ("from", 1082.0) - (float) (double) tr.getProperty ("speed", 30.0) * (float) (tick - trainStart);
        juce::Graphics::ScopedSaveState ss (g); g.reduceClipRegion (m.r (num (tr, "clip", 0, 0), y, num (tr, "clip", 1, 1200) - num (tr, "clip", 0, 0), L.img (n).h())); L.draw (g, n, m.f (x, y, L.img (n).w(), L.img (n).h()));
    });
    if (auto* pa = L.at ("anim.patches").getArray()) for (auto& pv : *pa)
    {
        if (! onPage (pv)) continue; const int d = L.tableAt (pv.getProperty ("table", {}).toString(), tick, (int) pv.getProperty ("period", 0)); if (d >= 9) continue;
        const auto n = pv.getProperty ("img", {}).toString(); L.draw (g, n, m.f (num (pv, "at", 0, 0), num (pv, "at", 1, 0), L.img (n).w(), L.img (n).h()), 1.0f - (float) d / 9.0f);
    }
    each (L.at ("anim.holo"), [&] (const juce::var& ho)
    {
        if (vis || ! onPage (ho)) return;
        const auto n = ho.getProperty (holoFrame == 2 ? "glitch" : holoFrame == 1 ? "b" : "a", {}).toString();
        L.draw (g, n, m.f (num (ho, "at", 0, 0), num (ho, "at", 1, 0), L.img (n).w(), L.img (n).h()), (float) (double) ho.getProperty ("alpha", 0.82) * (0.76f + 0.08f * (float) glowStep));
    });
    if (auto* sa = L.at ("anim.sprites").getArray()) for (auto& sv : *sa)
    {
        if (! onPage (sv) || (vis && (bool) sv.getProperty ("strip", false))) continue; const auto n = sv.getProperty ("img", {}).toString(); const float k = (float) (double) sv.getProperty ("scale", 1.0) * s; const auto c = m.p (num (sv, "at", 0, 0), num (sv, "at", 1, 0));
        if ((bool) sv.getProperty ("strip", false))
        {   // a scene ghost with its drawn poses: it moves with the output level like the main one ("asleep": it sleeps)
            const int fr = juce::jmax (1, (int) sv.getProperty ("frames", painted::kPoseFrames)); const float cw = num (sv, "cell", 0, 110), chh = num (sv, "cell", 1, 130);
            L.frame (g, n, (bool) sv.getProperty ("asleep", false) ? fr - 1 : juce::jmin (hatchFrame, fr - 2), fr, false, { c.x - num (sv, "c", 0, 0) * k, c.y - num (sv, "c", 1, 0) * k, cw * k, chh * k });
            continue;
        }
        L.draw (g, n, { c.x - num (sv, "c", 0, 0) * k, c.y - num (sv, "c", 1, 0) * k, L.img (n).w() * k, L.img (n).h() * k });
    }
    each (L.at ("anim.rain"), [&] (const juce::var& ra)
    {
        if (! onPage (ra)) return;
        const auto n = ra.getProperty ("img", {}).toString(); const int fr = juce::jmax (1, (int) ra.getProperty ("frames", 4)); const auto& im = L.img (n); const auto rr = rainRect (L, ra, page);
        juce::Graphics::ScopedSaveState ss (g); g.reduceClipRegion (m.r (rr));
        L.frame (g, n, (tick / 2) % fr, fr, false, m.f (rr.getX(), rr.getY(), im.w() / (float) fr, im.h()));
    });
    if (auto* st = L.at ("anim.strips").getArray()) for (auto& sv : *st)
    {   // small stepped loops on the picture (rain rings on the pavement, steam)
        if (! onPage (sv)) continue; const auto n = sv.getProperty ("img", {}).toString(); const int fr = juce::jmax (1, (int) sv.getProperty ("frames", 1)), dv = juce::jmax (1, (int) sv.getProperty ("div", 2)); const auto& im = L.img (n);
        L.frame (g, n, (tick / dv + (int) sv.getProperty ("phase", 0)) % fr, fr, false, m.f (num (sv, "at", 0, 0), num (sv, "at", 1, 0), im.w() / (float) fr, im.h()));
    }
    if (const juce::String lk = "anim.lamp." + page; L.has (lk)) L.draw (g, "anim/lamp-glow", m.sq (L.el (lk, 0), L.el (lk, 1), 160, 160), painted::lampAlpha (tick));
    if (const auto hr = hatchRect (L, page); ! hr.isEmpty() && ! vis)
    {   // the scene's ghost: its drawn pose strip (six held frames), or the older four-frame bounce strip
        if (const auto ss = L.str ("anim.sceneStrip"); L.hasFile (ss)) L.frame (g, ss, juce::jlimit (0, painted::kPoseAsleep - 1, hatchFrame), painted::kPoseFrames, false, hr);
        else L.frame (g, "anim/ghost-bounce-strip", painted::squashOf (hatchFrame), 4, false, hr);
    }
    if (const juce::String sk = "anim.shoulder." + page; L.has (sk))
    {   // the ghost on the machine's shoulder is the selected unit's own ghost
        const float k = L.el (sk, 2, 0.8f) * s; const auto c = m.p (L.el (sk, 0), L.el (sk, 1));
        const juce::String un = unitDef (panel->unitPage.unit).shortName;
        if (const auto ps = L.str ("rack.tiles.pose.strip"); ps.isNotEmpty() && ! (bool) L.at ("rack.tiles.pose.onSign") && L.hasFile (ps + un + "-strip"))
            L.frame (g, ps + un + "-strip", hatchFrame < 2 ? hatchFrame : 0, painted::kPoseFrames, false, { c.x - L.el ("rack.tiles.pose.anchor", 0, 35) * k, c.y - L.el ("rack.tiles.pose.anchor", 1, 42) * k, L.el ("rack.tiles.pose.cell", 0, 72) * k, L.el ("rack.tiles.pose.cell", 1, 88) * k });
        else L.draw (g, juce::String ("ghosts/unit-") + un, { c.x - 31.0f * k, c.y - 32.0f * k, 64.0f * k, 76.0f * k });
    }
    if ((bool) L.at ("anim.leaves")) for (int i = 0; i < 7; ++i) { const auto lf = painted::leafAt (i, tick); L.frame (g, "anim/leaf-" + juce::String (i % 3 + 1) + "-strip", lf.frame, 8, false, m.sq (lf.r.getCentreX(), lf.r.getCentreY(), 28, 28)); }
    painted::trace ("editor: life drawn");
    // the header: logo plaque, the level strip with its meters
    L.draw (g, "slab/logo-sign", m.f (0, 0, 256, 66));
    if (const auto ld = L.str ("anim.logoDim.img"); ld.isNotEmpty()) if (const int d = L.tableAt (L.str ("anim.logoDim.table"), tick, (int) L.num ("anim.logoDim.period", 0)); d < 9) L.draw (g, ld, m.f (0, 0, 256, 66), 1.0f - (float) d / 9.0f);
    L.text (g, "the open-source 24-effect rack  v" + juce::String ("1 beta"), m.f (L.el ("header.sub", 0, 67), L.el ("header.sub", 1, 41), 186, 12), juce::Justification::centredLeft, 8.4f * s, L.sub());
    L.panel (g, *this, L.str ("slabs.level"), m.f (258, 10, 522, 48));
    auto meter = [&] (juce::Rectangle<int> mr, const float* lv, const float* tp)
    {
        auto r = mr.toFloat();
        if ((bool) L.at ("header.ledMeter"))
        {   // a row of small lamps: lime, then orange, then red; the true peak as one white lamp
            const int n = 22; const float pk = juce::jmax (lv[0], lv[1]), f = gpfx::clampf ((gpfx::gainToDb (pk) + 60.0f) / 60.0f, 0.0f, 1.0f), cw = r.getWidth() / (float) n;
            const int lit = juce::roundToInt (f * (float) n), tpi = tp ? juce::roundToInt (gpfx::clampf ((gpfx::gainToDb (juce::jmax (tp[0], tp[1])) + 60.0f) / 60.0f, 0.0f, 1.0f) * (float) n) - 1 : -1;
            for (int i = 0; i < n; ++i) { const auto c = juce::Colour (i < 14 ? 0xff8dff1f : i < 19 ? 0xffffb129 : 0xffff2944); g.setColour (i == tpi && tpi >= lit ? juce::Colours::white : i < lit ? c : c.withAlpha (0.16f)); g.fillRoundedRectangle (r.getX() + cw * (float) i, r.getY(), cw * 0.7f, r.getHeight() * 0.72f, 1.0f); }
            return;
        }
        for (int c = 0; c < 2; ++c)
        {
            auto b = r.removeFromTop (r.getHeight() * (c == 0 ? 0.5f : 1.0f)).reduced (0, 0.6f);
            g.setColour (L.ink().withAlpha (0.85f)); g.fillRoundedRectangle (b, 1.5f);
            const float db = gpfx::gainToDb (lv[c]), f = gpfx::clampf ((db + 60.0f) / 60.0f, 0.0f, 1.0f);
            g.setColour (juce::Colour (db > -1.0f ? 0xffed6ec7 : db > -9.0f ? 0xfff7c245 : 0xffa1e935)); g.fillRoundedRectangle (b.withWidth (b.getWidth() * f), 1.5f);
            if (tp) { const float tdb = gpfx::gainToDb (tp[c]), tf = gpfx::clampf ((tdb + 60.0f) / 60.0f, 0.0f, 1.0f); g.setColour (tdb > -0.1f ? juce::Colour (0xffed6ec7) : L.cream()); g.fillRect (b.getX() + b.getWidth() * tf - 1.0f, b.getY(), 2.0f, b.getHeight()); }
        }
    };
    meter (inMeter, inLv, nullptr); meter (outMeter, outLv, outTp);
    const bool byp = proc.apvts.getRawParameterValue ("bypass")->load() >= 0.5f;
    if (t < toastUntil && toastText.isNotEmpty()) L.text (g, toastText, mixHint.toFloat(), juce::Justification::centred, 8.6f * s, juce::Colour (0xffa1e935));
    else L.text (g, byp ? "BYPASSED: dry signal only" : "dry  <->  wet (whole rack)", mixHint.toFloat(), juce::Justification::centred, 8.6f * s, byp ? juce::Colour (0xfff7c245) : L.sub());
    // the macro row's and the scenes bar's backing
    auto backing = [&] (const juce::String& role)
    {
        const auto n = L.str ("slabs." + role + ".img"); const auto r = L.rect ("slabs." + role + ".rect"); if (! L.hasImg (n)) return;
        if ((bool) L.at ("slabs." + role + ".stretch")) L.nine (g, n, m.f (r)); else L.draw (g, n, m.f (r.getX(), r.getY(), L.img (n).w(), L.img (n).h()));
    };
    painted::trace ("editor: header drawn");
    if (! spatial) backing ("macro");
    if (const auto me = L.str ("slabs.macroEach.img"); ! spatial && L.hasImg (me)) for (int i = 0; i < kNumMacros; ++i) L.draw (g, me, m.f (L.el ("slabs.macroEach.at", 0) + L.num ("slabs.macroEach.dx", 148) * (float) i, L.el ("slabs.macroEach.at", 1), L.img (me).w(), L.img (me).h()));
    backing ("scenes");
}
// PAINTED: the look's light (sun shafts, leaf dapples) lies over everything, as in the pictures
void PhantomEditor::paintOverChildren (juce::Graphics& g)
{
    fxtheme::setLook (shownLook); auto* L = painted::active(); if (L == nullptr) return;
    painted::trace ("editor: children drawn");
    const juce::String n = L->str ("anim.overlay." + pageKey (*L)); if (n.isEmpty()) return;
    const juce::String key = L->id + n + juce::String (getWidth()) + "x" + juce::String (getHeight());
    if (key != overlayKey) { overlayKey = key; const auto& src = L->img (n); overlayCache = src.ok() ? painted::Look::resample (src.im, src.im.getBounds(), juce::roundToInt ((float) getWidth() * L->pixelScale), juce::roundToInt ((float) getHeight() * L->pixelScale), false) : juce::Image(); L->drop (n); }
    if (overlayCache.isValid()) g.drawImage (overlayCache, getLocalBounds().toFloat());
}
void PhantomEditor::resizedPainted (painted::Look&)
{
    const painted::Map m (*this); auto r = getLocalBounds();
    header = r.removeFromTop (juce::roundToInt (62.0f * m.sy)); logo = m.r (0, 0, 256, 66);
    // (LAYOUT.md's header rectangles, each grown by a pixel or three where 0.1.8's control was bigger: no hit area shrinks)
    inK->setBounds (m.r (266, 12, 164, 28)); outK->setBounds (m.r (436, 12, 166, 28)); mixK->setBounds (m.r (608, 12, 166, 28));
    inMeter = m.r (312, 42, 112, 8); outMeter = m.r (484, 42, 112, 8); mixHint = m.r (606, 40, 168, 13);
    snapBtn.setBounds (m.r (790, 16, 87, 30)); snapList.setBounds (m.r (879, 16, 23, 30)); abBtn.setBounds (m.r (907, 16, 36, 30)); abCopy.setBounds (m.r (947, 16, 47, 30));
    aboutBtn.setBounds (m.r (1002, 16, 27, 30)); themeBtn.setBounds (m.r (1035, 16, 62, 30)); bypassBtn.setBounds (m.r (1103, 16, 86, 30));
    scenes->setBounds (r.removeFromBottom (juce::roundToInt (44.0f * m.sy))); panel->setBounds (r);
}
void PhantomEditor::paint (juce::Graphics& g)
{
    fxtheme::setLook (shownLook); painted::syncFont();
    if (auto* L = painted::active()) { paintPainted (g, *L); return; }
    if (ghost()) { ghosty::glass::paintAurora (g, (float) getWidth(), (float) getHeight()); }
    else { g.fillAll (col::bg); auto h = header.toFloat(); juce::ColourGradient bg (T (juce::Colour (0xff141726)), h.getX(), h.getY(), T (juce::Colour (0xff0d0f17)), h.getX(), h.getBottom(), false); g.setGradientFill (bg); g.fillRect (h); g.setColour (col::slime.withAlpha (0.15f)); g.drawHorizontalLine (header.getBottom() - 1, h.getX(), h.getRight()); }
    auto lg = logo.toFloat(); drawLogo (g, lg.removeFromLeft (lg.getHeight() * 1.2f), t);
    const juce::String title = "PHANTOM FX"; auto tr = lg.withTrimmedLeft (8).withHeight (30).translated (0, 6);
    g.setFont (ghosty::font (24.0f));
    g.setColour (col::ice.withAlpha (0.35f)); g.drawText (title, tr.translated (-1.5f, 0), juce::Justification::centredLeft);
    g.setColour (col::pink.withAlpha (0.30f)); g.drawText (title, tr.translated (1.5f, 0), juce::Justification::centredLeft);
    g.setColour (ghost() ? ghosty::glass::ink() : col::white.get()); g.drawText (title, tr, juce::Justification::centredLeft);
    drawCaption (g, tr.translated (0, 24).withHeight (14), "the open-source 24-effect rack  v" + juce::String ("1 beta"), ghost() ? ghosty::glass::ink2() : col::dim.get(), 10.0f);
    drawMeter (g, inMeter.toFloat(), inLv, nullptr); drawMeter (g, outMeter.toFloat(), outLv, outTp);
    const bool byp = proc.apvts.getRawParameterValue ("bypass")->load() >= 0.5f;
    if (t < toastUntil && toastText.isNotEmpty()) drawCaption (g, mixHint.toFloat(), toastText, col::slime.get(), 9.0f);
    else drawCaption (g, mixHint.toFloat(), byp ? "BYPASSED: dry signal only" : "dry  <->  wet (whole rack)", byp ? col::gold.get() : col::dim.get(), 9.0f);
}
void PhantomEditor::resized()
{
    if (shownLook >= fxtheme::kFirstPainted) { fxtheme::setLook (shownLook); if (auto* L = painted::active()) { resizedPainted (*L); return; } }
    auto r = getLocalBounds(); header = r.removeFromTop (kHeader); auto h = header.reduced (12, 8); logo = h.removeFromLeft (250);
    bypassBtn.setBounds (h.removeFromRight (84).withSizeKeepingCentre (84, 26)); h.removeFromRight (8);
    themeBtn.setBounds (h.removeFromRight (60).withSizeKeepingCentre (60, 26)); h.removeFromRight (6);
    aboutBtn.setBounds (h.removeFromRight (26).withSizeKeepingCentre (26, 26)); h.removeFromRight (10);
    abCopy.setBounds (h.removeFromRight (46).reduced (0, 9)); h.removeFromRight (4); abBtn.setBounds (h.removeFromRight (36).reduced (0, 9)); h.removeFromRight (8);
    snapList.setBounds (h.removeFromRight (22).withSizeKeepingCentre (22, 30)); snapBtn.setBounds (h.removeFromRight (86).withSizeKeepingCentre (86, 30)); h.removeFromRight (12);
    const int unit = h.getWidth() / 3; auto a = h.removeFromLeft (unit), b = h.removeFromLeft (unit), c = h;
    inK->setBounds (a.removeFromTop (a.getHeight() / 2 + 6).reduced (4, 1)); inMeter = a.withTrimmedLeft (44).reduced (4, 2);
    outK->setBounds (b.removeFromTop (b.getHeight() / 2 + 6).reduced (4, 1)); outMeter = b.withTrimmedLeft (44).reduced (4, 2);
    mixK->setBounds (c.removeFromTop (c.getHeight() / 2 + 6).reduced (4, 1)); mixHint = c.withTrimmedLeft (44).reduced (4, 0);
    scenes->setBounds (r.removeFromBottom (SceneBar::kHeight)); panel->setBounds (r);
}
} // namespace phantom
