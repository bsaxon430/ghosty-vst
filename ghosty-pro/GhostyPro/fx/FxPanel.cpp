// Ghost FX rack: the editor ("FX" tab). Unit strip (processing order, drag to reorder, LED = on/off),
// a header (title, ON, MIX, scenes) and the selected unit's big editor page.
#include "FxRackImpl.h"
#include "FxUi.h"
#include "FxPresets.h"
#if GPFX_BOX
 #include "FxBoxTimelineUi.h"   // (GHOST MOTION timeline view interface)
#endif
#if GPFX_BOX
 #include "BendLaneEditor.h"
 #include "FxBrain.h"
#endif

namespace gpfx {
using namespace ui;

namespace {

void setParam (juce::AudioProcessorValueTreeState& s, const juce::String& id, float v)
{
    if (auto* p = s.getParameter (id)) { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (v)); p->endChangeGesture(); }
}
float getParam (juce::AudioProcessorValueTreeState& s, const juce::String& id)
{
    if (auto* v = s.getRawParameterValue (id)) return v->load();
    return 0.0f;
}

// A faint ghost drifting through a visualiser.
void driftGhost (juce::Graphics& g, juce::Rectangle<float> r, double t, int look, float alpha, float glitch = 0)
{
    const float x = r.getX() + r.getWidth() * (0.5f + 0.42f * (float) std::sin (t * 0.13 + look));
    const float y = r.getY() + r.getHeight() * (0.5f + 0.30f * (float) std::sin (t * 0.21 + look * 1.7));
    ghosty::drawGhost (g, ghosty::lookFor (look), x, y, r.getHeight() * 0.32f, glitch, false, alpha, std::cos (t * 0.13 + look) < 0);
}

// ================================================================ unit strip
class Strip : public juce::Component
{
public:
    Strip (juce::AudioProcessorValueTreeState& s, Rack::Impl& r) : apvts (s), rack (r) {}
    std::function<void (int)> onSelect;
    std::function<void (int)> onReorder;   // the unit that was dragged (order tips)
#if GPFX_BOX
    std::function<bool (int, juce::Point<int>)> onDropOutside;   // a tile dragged out of the strip and dropped (unit, screen position): true = taken (GHOST MOTION timeline)
    std::function<bool()> deferSelect;                           // true: a click selects on mouse-up (a drag may be headed for the timeline, which a select would close)
#endif
    int selected = 0; double t = 0;
    bool unitOn (int u) const { return rack.onP[(size_t) u].b(); }   // what the audio hears (Ghost Box: includes MORPH)

    // below 1300 px the strip folds into two rows (the tiles stay readable at the default window size)
    int rows() const { return rowsFor (getWidth()); }
    int perRow() const { return (kNumUnits + rows() - 1) / rows(); }
    static int rowsFor (int width) { return width < 1300 ? 2 : 1; }
    juce::Rectangle<float> tile (int k) const
    {
        const int pr = perRow(), row = k / pr, col = k % pr;
        const float w = (float) getWidth() / (float) pr, rh = ((float) getHeight() - 14.0f) / (float) rows();
        return { (float) col * w, 14.0f + row * rh, w, rh };
    }
    void paint (juce::Graphics& g) override
    {
        const auto order = rack.getOrder();
        // group legend
        g.setFont (font (9.0f));
        int x0 = 0;
        for (int gidx = 0; gidx < 4; ++gidx)
        {
            const juce::Colour gc = gidx == gRhythm ? col::slime : gidx == gColour ? col::gold : gidx == gSpace ? col::ice : col::pink;
            g.setColour (gc.withAlpha (0.8f)); g.fillEllipse ((float) x0 + 2, 3, 7, 7);
            g.setColour (ui::ghost() ? ghosty::glass::ink() : col::dim.get()); g.drawText (groupName (gidx), x0 + 12, 0, 70, 13, juce::Justification::centredLeft);
            x0 += 74;
        }
        g.setColour (ui::ghost() ? ghosty::glass::ink() : col::dim.withAlpha (0.7f));   // (GHOST: full ink, the line sits on the aurora)
#if GPFX_BOX
        g.drawText (rows() == 2 ? "signal flows left to right, then down  -  drag tiles to reorder  -  click the light to switch a unit on  -  right-click: lanes"
                                : "signal flows left to right  -  drag tiles to reorder  -  click the light to switch a unit on  -  right-click: lanes", getLocalBounds().removeFromTop (13), juce::Justification::centredRight);
#else
        g.drawText ("signal flows left to right  -  drag tiles (or the page title) to reorder  -  click the light to switch a unit on  -  right-click: order", getLocalBounds().removeFromTop (13), juce::Justification::centredRight);
#endif

        // chain flow: arrows between the tiles (bright where the signal passes an active unit), position numbers on active tiles
        int firstOn = kNumUnits, lastOn = -1;
        for (int k = 0; k < kNumUnits; ++k) if (unitOn (order[(size_t) k])) { firstOn = std::min (firstOn, k); lastOn = k; }
        for (int k = 0; k + 1 < kNumUnits; ++k)
        {
            const auto a = tile (k), b = tile (k + 1);
            const bool flow = k >= firstOn && k < lastOn;
            const auto c = flow ? (unitOn (order[(size_t) k]) ? accentOf (order[(size_t) k]) : col::white).withAlpha (0.85f) : col::white.withAlpha (0.13f);
            g.setColour (c);
            if ((k + 1) % perRow() == 0)
            {   // row wrap: a small "continues below" hook at the end of the row and at the start of the next
                juce::Path ar; ar.startNewSubPath (a.getRight() - 6, a.getY() + a.getHeight() * 0.40f); ar.lineTo (a.getRight() - 2, a.getY() + a.getHeight() * 0.40f); ar.lineTo (a.getRight() - 2, a.getBottom() - 2);
                ar.startNewSubPath (b.getX() + 2, b.getY() + 2); ar.lineTo (b.getX() + 2, b.getY() + b.getHeight() * 0.40f); ar.lineTo (b.getX() + 6, b.getY() + b.getHeight() * 0.40f);
                g.strokePath (ar, juce::PathStrokeType (flow ? 1.6f : 1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                continue;
            }
            const float x = 0.5f * (a.getRight() + b.getX()), y = a.getY() + a.getHeight() * 0.40f;
            juce::Path ar; ar.startNewSubPath (x - 2.2f, y - 4.0f); ar.lineTo (x + 2.0f, y); ar.lineTo (x - 2.2f, y + 4.0f);
            g.strokePath (ar, juce::PathStrokeType (flow ? 1.6f : 1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        int position = 0;
        for (int k = 0; k < kNumUnits; ++k)
        {
            const int u = order[(size_t) k];
            auto r = tile (k).reduced (3.5f, 2.5f);
            if (dragging && k == dropIndex) { g.setColour (col::white.withAlpha (0.6f)); g.fillRect (r.getX() - 3, r.getY(), 2.0f, r.getHeight()); }
            const bool on = unitOn (u);
            const bool sel = u == selected;
            const auto acc = accentOf (u);
            const auto& in = unitInfo (u);
            const juce::Colour gc = in.group == gRhythm ? col::slime : in.group == gColour ? col::gold : in.group == gSpace ? col::ice : col::pink;
            const float lvl = clampf (rack.tm.unitLevel[(size_t) u].load() * 1.5f, 0.0f, 1.0f);
            if (on) { g.setColour (acc.withAlpha (0.10f + 0.12f * lvl)); g.fillRoundedRectangle (r.expanded (2), 10); }
#if GPFX_BOX
            const int routeB = rack.routeRouting() ? rack.routeBranchOf (u) : 0;   // GHOST SPLIT branch (replaces the parallel lanes while it routes)
            const bool laneB = routeB == 0 && ! rack.routeRouting() && rack.parallelOn() && u != uLim && rack.laneOf[(size_t) u].load() == 1;
            if (laneB) { g.setColour (T (juce::Colour (0xffFF4FD8)).withAlpha (0.10f)); g.fillRoundedRectangle (r.expanded (2), 10); }   // lane B tiles are tinted
#endif
            if (ui::ghost())
            {   // GHOST: each effect tile is a glass card (cached image per size), the selected one with a violet rim
                tileGlass.draw (g, r, 9.0f, sel ? 0.50f : 0.36f, 4.0f);
                g.setColour (sel ? ghosty::glass::violetDeep() : acc.withAlpha (on ? 0.55f : 0.22f)); g.drawRoundedRectangle (r.reduced (0.5f), 9, sel ? 1.8f : 1.0f);
            }
            else
            {
            juce::ColourGradient bg (sel ? T (juce::Colour (0xff232739)) : T (juce::Colour (0xff161925)), 0, r.getY(), T (juce::Colour (0xff0f1118)), 0, r.getBottom(), false);
            g.setGradientFill (bg); g.fillRoundedRectangle (r, 9);
            g.setColour (sel ? acc : acc.withAlpha (on ? 0.45f : 0.15f)); g.drawRoundedRectangle (r, 9, sel ? 1.6f : 1.0f);
            }
            g.setColour (gc.withAlpha (on ? 0.9f : 0.35f)); g.fillRoundedRectangle (r.getX() + 8, r.getBottom() - 4, r.getWidth() - 16, 2.0f, 1.0f);
            // ghost icon
            const float bob = on ? (float) std::sin (t * 2.6 + u) * 2.0f : 0.0f;
            ghosty::GhostLook L = ghosty::lookFor (in.look);
            if (! on) { L.body = T (juce::Colour (0xff3a3e4f)); L.slime = T (juce::Colour (0xff4a4f63)); L.pupil = T (juce::Colour (0xff4a4f63)); }
            else L.slime = acc;
            const float gs = juce::jmin (r.getHeight() * 0.42f, r.getWidth() * 0.5f);
            ghosty::drawGhost (g, L, r.getCentreX() - 2, r.getY() + r.getHeight() * 0.40f + bob, gs,
                               on && u == uHaunt ? rack.tm.hauntEnv.load() : 0.0f, false, on ? 1.0f : 0.6f);
            if (ui::ghost()) ui::ghostglass::outline (g, r.getCentreX() - 2, r.getY() + r.getHeight() * 0.40f + bob, gs, on ? 1.0f : 0.5f);   // GHOST: legible on glass
            g.setColour (on ? col::white : col::dim); g.setFont (font (juce::jmin (11.0f, r.getWidth() * 0.15f)));
            g.drawText (in.shortName, r.withTrimmedTop (r.getHeight() * 0.68f).withTrimmedBottom (5), juce::Justification::centred, false);
            // LED
            auto led = ledRect (k);
            if (on) glowDot (g, led.getCentre(), 3.5f, acc);
            else { g.setColour (T (juce::Colour (0xff2c3040))); g.fillEllipse (led.reduced (1.5f)); }
#if GPFX_BOX
            if (laneB)
            {   // lane badge only on the parallel (B) tiles; lane A is the main chain and needs no label
                auto badge = juce::Rectangle<float> (r.getX() + 3, r.getY() + 3, 12, 11);
                g.setColour (T (juce::Colour (0xffFF4FD8)).withAlpha (0.85f)); g.fillRoundedRectangle (badge, 3);
                g.setColour (T (juce::Colour (0xff0b0c10))); g.setFont (font (8.5f)); g.drawText ("B", badge, juce::Justification::centred, false);
            }
            if (routeB > 0)
            {   // GHOST SPLIT: the branch this unit runs in (L / M / H, A / B, M / S, L / Q ...)
                auto badge = juce::Rectangle<float> (r.getX() + 3, r.getY() + 3, 12, 11);
                const int rm = rack.route.mode.i(), nb = routeBranchCount (rm, rack.route.bands.i());
                g.setColour (T (juce::Colour (0xff5AF0C8)).withAlpha (0.9f)); g.fillRoundedRectangle (badge, 3);
                g.setColour (T (juce::Colour (0xff0b0c10))); g.setFont (font (8.5f)); g.drawText (routeBranchName (rm, rack.route.bands.i(), std::min (routeB, nb) - 1).substring (0, 1), badge, juce::Justification::centred, false);
            }
#endif
            if (on)
            {   // position in the active chain
                g.setColour (acc.withAlpha (0.9f)); g.setFont (font (9.5f));
#if GPFX_BOX
                const float px = laneB || routeB > 0 ? 18.0f : 4.0f;
#else
                const float px = 4.0f;
#endif
                g.drawText (juce::String (++position), juce::Rectangle<float> (r.getX() + px, r.getY() + 3, 16, 11), juce::Justification::centredLeft, false);
            }
            else
            {   // bypassed: greyed out
                g.setColour (T (juce::Colour (0xff0b0c10)).withAlpha (ui::ghost() ? 0.25f : 0.45f)); g.fillRoundedRectangle (r, 9);
            }
        }
    }
    juce::Rectangle<float> ledRect (int k) const { auto r = tile (k).reduced (3.5f, 2.5f); return { r.getRight() - 14, r.getY() + 4, 10, 10 }; }
    ghosty::glass::PanelCache tileGlass;   // GHOST: the glass tile images (per size)
    int tileAt (juce::Point<float> p) const { for (int k = 0; k < kNumUnits; ++k) if (tile (k).contains (p)) return k; return -1; }

    void mouseDown (const juce::MouseEvent& e) override
    {
        downTile = tileAt (e.position); dragging = false;
        if (downTile < 0) return;
        const int u = rack.getOrder()[(size_t) downTile];
#if GPFX_BOX
        if (e.mods.isPopupMenu() && u != uLim)
        {   // PARALLEL LANES: put this unit in lane A (main) or lane B (parallel)
            juce::PopupMenu m; m.addSectionHeader (juce::String (unitInfo (u).title) + "  -  PARALLEL LANES");
            const int lane = rack.laneOf[(size_t) u].load();
            m.addItem (1, "Lane A (main chain)", true, lane == 0);
            m.addItem (2, "Lane B (parallel chain)", true, lane == 1);
            m.addSeparator();
            m.addItem (3, rack.parallelOn() ? "Parallel lanes are ON" : "Turn parallel lanes on", ! rack.parallelOn());
            {   // GHOST SPLIT: which branch this unit runs in (FxRoute.h; it replaces the parallel lanes while it routes units)
                m.addSectionHeader (juce::String (unitInfo (u).title) + "  -  GHOST SPLIT");
                const int rm = juce::jlimit (0, 4, (int) std::lround (getParam (apvts, "fx_route_mode"))), bi = (int) std::lround (getParam (apvts, "fx_route_bands"));
                const int cur = rack.routeBranchOf (u), nb = routeBranchCount (rm, bi);
                m.addItem (10, "Not split (main chain)", true, cur == 0);
                for (int b = 0; b < nb; ++b) m.addItem (11 + b, "Branch " + routeBranchName (rm, bi, b) + "  (" + routeModeNames()[rm] + ")", true, cur == b + 1);
            }
            juce::Component::SafePointer<Strip> safe (this);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe, u] (int r)
            {
                if (safe == nullptr || r <= 0) return;
                auto& st = *safe;
                if (r == 1 || r == 2) { st.rack.pushUndo(); st.rack.setLane (u, r == 2 ? 1 : 0); if (r == 2 && ! st.rack.parallelOn()) setParam (st.apvts, "fx_par_on", 1.0f); }
                if (r == 3) { st.rack.pushUndo(); setParam (st.apvts, "fx_par_on", 1.0f); }
                if (r >= 10 && r <= 10 + kRouteBranches) { st.rack.pushUndo(); st.rack.setRoute (u, r - 10); if (r > 10 && getParam (st.apvts, "fx_route_on") < 0.5f) setParam (st.apvts, "fx_route_on", 1.0f); }
                st.repaint();
            });
            downTile = -1; return;
        }
#else
        if (e.mods.isPopupMenu())
        {   // FX RACK (round 3): the chain order menu
            juce::PopupMenu m; m.addSectionHeader ("CHAIN ORDER");
            auto o = rack.getOrder(); bool isDefault = true; for (int i = 0; i < kNumUnits; ++i) isDefault = isDefault && o[(size_t) i] == i;
            m.addItem (1, "Reset order (default chain)", ! isDefault);
            if (u != uLim) { m.addItem (2, juce::String ("Move ") + unitInfo (u).shortName + " to the start"); m.addItem (3, juce::String ("Move ") + unitInfo (u).shortName + " to the end (before the limiter)"); }
            juce::Component::SafePointer<Strip> safe (this);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe, u] (int r)
            {
                if (safe == nullptr || r <= 0) return;
                auto& st = *safe;
                if (r == 1) { st.rack.pushUndo(); st.rack.resetOrder(); }
                else if (r == 2 || r == 3) st.moveUnit (u, r == 2 ? 0 : kNumUnits - 1);
                st.repaint();
            });
            downTile = -1; return;
        }
#endif
        if (ledRect (downTile).expanded (5).contains (e.position)) { setParam (apvts, pid (u, "on"), getParam (apvts, pid (u, "on")) > 0.5f ? 0.0f : 1.0f); downTile = -1; repaint(); return; }
        if (e.getNumberOfClicks() >= 2) setParam (apvts, pid (u, "on"), getParam (apvts, pid (u, "on")) > 0.5f ? 0.0f : 1.0f);
#if GPFX_BOX
        selectOnUp = deferSelect && deferSelect();
        if (selectOnUp) return;
#endif
        if (onSelect) onSelect (u);
    }
#if GPFX_BOX
    bool selectOnUp = false;
#endif
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (downTile < 0 || rack.getOrder()[(size_t) downTile] == uLim) return;
        if (e.getDistanceFromDragStart() > 6) dragging = true;
        if (dragging)
        {
            const int pr = perRow(); const float w = (float) getWidth() / (float) pr, rh = ((float) getHeight() - 14.0f) / (float) rows();
            const int row = juce::jlimit (0, rows() - 1, (int) ((e.position.y - 14.0f) / rh));
            dropIndex = juce::jlimit (0, kNumUnits - 1, row * pr + juce::jlimit (0, pr, (int) std::lround (e.position.x / w)));
            repaint();
        }
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
#if GPFX_BOX
        if (dragging && downTile >= 0 && ! getLocalBounds().contains (e.getPosition()) && onDropOutside && onDropOutside (rack.getOrder()[(size_t) downTile], e.getScreenPosition()))
        { dragging = false; downTile = -1; selectOnUp = false; repaint(); return; }
        if (selectOnUp && ! dragging && downTile >= 0) { selectOnUp = false; const int u = rack.getOrder()[(size_t) downTile]; downTile = -1; if (onSelect) onSelect (u); return; }
        selectOnUp = false;
#else
        juce::ignoreUnused (e);
#endif
        if (dragging && downTile >= 0)
        {
            auto o = rack.getOrder();
            std::vector<int> v (o.begin(), o.end() - 1);   // without the limiter
            const int u = v[(size_t) downTile];
            v.erase (v.begin() + downTile);
            int ins = dropIndex > downTile ? dropIndex - 1 : dropIndex;
            ins = juce::jlimit (0, (int) v.size(), ins);
            v.insert (v.begin() + ins, u);
            std::array<int, kNumUnits> no {}; for (size_t i = 0; i < v.size(); ++i) no[i] = v[i]; no[kNumUnits - 1] = uLim;
            if (no != o) { rack.pushUndo(); rack.setOrder (no); }   // a reorder is one undo step
            if (onReorder) onReorder (u);
        }
        dragging = false; downTile = -1; repaint();
    }
#if ! GPFX_BOX
    // FX RACK (round 3): puts unit u at drop slot 'drop' (0 .. kNumUnits - 1 = a gap between tiles, the limiter stays last); one undo step
    void moveUnit (int u, int drop)
    {
        auto o = rack.getOrder();
        std::vector<int> v (o.begin(), o.end() - 1);   // without the limiter
        const auto it = std::find (v.begin(), v.end(), u);
        if (it == v.end()) return;
        const int from = (int) (it - v.begin());
        v.erase (it);
        int ins = drop > from ? drop - 1 : drop;
        ins = juce::jlimit (0, (int) v.size(), ins);
        v.insert (v.begin() + ins, u);
        std::array<int, kNumUnits> no {}; for (size_t i = 0; i < v.size(); ++i) no[i] = v[i]; no[kNumUnits - 1] = uLim;
        if (no != o) { rack.pushUndo(); rack.setOrder (no); }   // a reorder is one undo step
    }
    // FX RACK (round 3): dragging the selected unit's page header onto the strip (positions in this strip's coordinates)
    void headerDrag (int u, juce::Point<float> p)
    {
        if (u == uLim) return;
        const auto o = rack.getOrder(); for (int k = 0; k < kNumUnits; ++k) if (o[(size_t) k] == u) downTile = k;
        dragging = true; dropIndex = dropAt (p); repaint();
    }
    void headerDrop (int u, juce::Point<float> p)
    {
        if (dragging && u != uLim && getLocalBounds().toFloat().expanded (0, 40).contains (p)) { moveUnit (u, dropAt (p)); if (onReorder) onReorder (u); }
        dragging = false; downTile = -1; repaint();
    }
    int dropAt (juce::Point<float> p) const
    {
        const int pr = perRow(); const float w = (float) getWidth() / (float) pr, rh = ((float) getHeight() - 14.0f) / (float) rows();
        const int row = juce::jlimit (0, rows() - 1, (int) ((p.y - 14.0f) / rh));
        return juce::jlimit (0, kNumUnits - 1, row * pr + juce::jlimit (0, pr, (int) std::lround (p.x / w)));
    }
#endif
private:
    juce::AudioProcessorValueTreeState& apvts; Rack::Impl& rack;
    int downTile = -1, dropIndex = 0; bool dragging = false;
};

// ================================================================ pattern / index tiles bound to an int parameter
class IndexTiles : public juce::Component
{
public:
    IndexTiles (juce::AudioProcessorValueTreeState& s, const juce::String& id, int count, juce::Colour acc) : n (count), accent (acc)
    {
        if (auto* p = s.getParameter (id))
        {
            param = p;
            att = std::make_unique<juce::ParameterAttachment> (*p, [this] (float v) { value = (int) std::lround (v); repaint(); if (onChange) onChange (value); });
            att->sendInitialUpdate();
        }
    }
    std::function<void (int)> onChange;
    std::function<juce::String (int)> tip;
    int value = 1;
    void paint (juce::Graphics& g) override
    {
        const float w = (float) getWidth() / (float) n;
        for (int k = 0; k < n; ++k)
        {
            auto r = juce::Rectangle<float> ((float) k * w, 0, w, (float) getHeight()).reduced (2);
            const bool on = k + 1 == value;
            if (on) { g.setColour (accent.withAlpha (0.2f)); g.fillRoundedRectangle (r.expanded (1.5f), 6); }
            g.setColour (on ? accent.withAlpha (0.75f) : T (juce::Colour (0xff161926))); g.fillRoundedRectangle (r, 5);
            g.setColour (accent.withAlpha (on ? 1.0f : 0.22f)); g.drawRoundedRectangle (r, 5, 1.0f);
            g.setColour (on ? col::bg : col::white.withAlpha (0.7f)); g.setFont (font (11.0f));
            g.drawText (juce::String (k + 1), r, juce::Justification::centred);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int k = juce::jlimit (0, n - 1, (int) (e.position.x / ((float) getWidth() / (float) n)));
        if (att) att->setValueAsCompleteGesture ((float) (k + 1));
    }
private:
    int n; fxtheme::Held accent; juce::RangedAudioParameter* param = nullptr; std::unique_ptr<juce::ParameterAttachment> att;
};

// ================================================================ GHOST GATE page
class GateEditor : public juce::Component
{
public:
    explicit GateEditor (Rack::Impl& r) : rack (r) {}
    int pat = 0; bool snap = true; GatePattern data; int hover = -1;
    std::function<void()> onEdit;
    void load (int p) { pat = p; data = rack.getGatePattern (p); repaint(); }
    void commit() { rack.setGatePattern (pat, data); if (onEdit) onEdit(); }

    juce::Rectangle<float> area() const { return getLocalBounds().toFloat().reduced (10, 10); }
    juce::Point<float> toScreen (float x, float y) const { auto a = area(); return { a.getX() + x * a.getWidth(), a.getBottom() - y * a.getHeight() }; }
    juce::Point<float> fromScreen (juce::Point<float> p) const
    {
        auto a = area();
        return { clampf ((p.x - a.getX()) / a.getWidth(), 0, 1), clampf ((a.getBottom() - p.y) / a.getHeight(), 0, 1) };
    }
    void paint (juce::Graphics& g) override
    {
        const auto acc = accentOf (uGate);
        drawScreen (g, getLocalBounds().toFloat(), acc);
        auto a = area();
        // time-lane colouring behind
        for (int s = 0; s < kGateSteps; ++s)
            if (data.steps[(size_t) s] != tmNormal)
            {
                g.setColour (T (juce::Colour (timeModeColour (data.steps[(size_t) s]))).withAlpha (0.10f));
                g.fillRect (a.getX() + a.getWidth() * (float) s / kGateSteps, a.getY(), a.getWidth() / kGateSteps, a.getHeight());
            }
        for (int s = 0; s <= 32; ++s)
        {
            const float x = a.getX() + a.getWidth() * (float) s / 32.0f;
            g.setColour (col::white.withAlpha (s % 8 == 0 ? 0.16f : s % 2 == 0 ? 0.07f : 0.03f));
            g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
        }
        for (int k = 1; k < 4; ++k) { g.setColour (col::white.withAlpha (0.04f)); g.drawHorizontalLine ((int) (a.getY() + a.getHeight() * k / 4.0f), a.getX(), a.getRight()); }
        // curve
        juce::Path curve, fill;
        const int N = juce::jmax (64, (int) a.getWidth());
        for (int i = 0; i <= N; ++i)
        {
            const float x = (float) i / (float) N; const auto p = toScreen (x, evalGate (data, x));
            if (i == 0) { curve.startNewSubPath (p); fill.startNewSubPath (a.getX(), a.getBottom()); }
            else curve.lineTo (p);
            fill.lineTo (p);
        }
        fill.lineTo (a.getRight(), a.getBottom()); fill.closeSubPath();
        juce::ColourGradient fg (acc.withAlpha (0.35f), 0, a.getY(), col::ecto.withAlpha (0.05f), 0, a.getBottom(), false);
        g.setGradientFill (fg); g.fillPath (fill);
        glowStroke (g, curve, acc, 2.0f);
        // tension handles + points
        for (size_t i = 0; i + 1 < data.pts.size(); ++i)
        {
            const auto& p0 = data.pts[i]; const auto& p1 = data.pts[i + 1];
            if (p1.x - p0.x < 0.01f || std::abs (p1.y - p0.y) < 0.02f) continue;
            const float mx = 0.5f * (p0.x + p1.x); const auto m = toScreen (mx, evalGate (data, mx));
            juce::Path d; d.addRectangle (-3.5f, -3.5f, 7.0f, 7.0f); d.applyTransform (juce::AffineTransform::rotation (kPi * 0.25f).translated (m));
            g.setColour (col::ecto.withAlpha (0.8f)); g.strokePath (d, juce::PathStrokeType (1.2f));
        }
        for (size_t i = 0; i < data.pts.size(); ++i)
        {
            const auto p = toScreen (data.pts[i].x, data.pts[i].y);
            if ((int) i == hover) glowDot (g, p, 5.5f, col::white); else glowDot (g, p, 4.0f, acc);
        }
        // playhead with a riding ghost
        if (rack.tm.gatePattern.load() == pat)
        {
            const float pos = rack.tm.gatePos.load();
            const float x = a.getX() + pos * a.getWidth();
            g.setColour (col::white.withAlpha (0.35f)); g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
            const auto p = toScreen (pos, evalGate (data, pos));
            ghosty::drawGhost (g, ghosty::lookFor (3), p.x, p.y - 16, 26, 0, false, 0.9f);
        }
        drawCaption (g, a.removeFromTop (14).reduced (4, 0), data.name, col::white.withAlpha (0.5f), 10, juce::Justification::centredRight);
        drawCaption (g, getLocalBounds().toFloat().reduced (14, 6).removeFromBottom (14), "click: add  -  drag: move  -  double-click: delete  -  drag the diamonds: curve", col::dim, 9.5f);
    }
    int hitPoint (juce::Point<float> p) const
    {
        for (size_t i = 0; i < data.pts.size(); ++i) if (toScreen (data.pts[i].x, data.pts[i].y).getDistanceFrom (p) < 8.0f) return (int) i;
        return -1;
    }
    int hitHandle (juce::Point<float> p) const
    {
        for (size_t i = 0; i + 1 < data.pts.size(); ++i)
        {
            const float mx = 0.5f * (data.pts[i].x + data.pts[i + 1].x);
            if (toScreen (mx, evalGate (data, mx)).getDistanceFrom (p) < 7.0f) return (int) i;
        }
        return -1;
    }
    float snapX (float x) const { return snap ? std::round (x * 32.0f) / 32.0f : x; }
    void mouseMove (const juce::MouseEvent& e) override { const int h = hitPoint (e.position); if (h != hover) { hover = h; repaint(); } }
    void mouseDown (const juce::MouseEvent& e) override
    {
        drag = hitPoint (e.position); tension = -1;
        if (e.getNumberOfClicks() >= 2 && drag > 0 && drag < (int) data.pts.size() - 1)
        {
            data.pts.erase (data.pts.begin() + drag); drag = -1; commit(); repaint(); return;
        }
        if (drag >= 0) return;
        tension = hitHandle (e.position);
        if (tension >= 0) { startT = data.pts[(size_t) tension].t; return; }
        auto q = fromScreen (e.position); q.x = snapX (q.x);
        size_t i = 1; while (i < data.pts.size() && data.pts[i].x < q.x) ++i;
        if (i >= data.pts.size()) i = data.pts.size() - 1;
        data.pts.insert (data.pts.begin() + (long) i, { juce::jlimit (data.pts[i - 1].x + 0.001f, data.pts[i].x - 0.001f, q.x), q.y, 0 });
        drag = (int) i; commit(); repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (tension >= 0)
        {
            const auto& p0 = data.pts[(size_t) tension]; const auto& p1 = data.pts[(size_t) tension + 1];
            const float dir = p1.y > p0.y ? 1.0f : -1.0f;
            data.pts[(size_t) tension].t = clampf (startT + dir * (float) e.getDistanceFromDragStartY() / 80.0f, -1.0f, 1.0f);
            commit(); repaint(); return;
        }
        if (drag < 0) return;
        auto q = fromScreen (e.position);
        auto& pt = data.pts[(size_t) drag];
        pt.y = q.y;
        if (drag > 0 && drag < (int) data.pts.size() - 1)
            pt.x = juce::jlimit (data.pts[(size_t) drag - 1].x + 0.001f, data.pts[(size_t) drag + 1].x - 0.001f, snapX (q.x));
        commit(); repaint();
    }
private:
    Rack::Impl& rack; int drag = -1, tension = -1; float startT = 0;
};

class TimeLane : public juce::Component
{
public:
    explicit TimeLane (GateEditor& ed) : editor (ed) {}
    int brush = tmRepeat;
    void paint (juce::Graphics& g) override
    {
        const float w = (float) getWidth() / kGateSteps;
        const float pos = editor.pat == -1 ? -1.0f : 0.0f; juce::ignoreUnused (pos);
        for (int s = 0; s < kGateSteps; ++s)
        {
            auto r = juce::Rectangle<float> ((float) s * w, 0, w, (float) getHeight()).reduced (1.5f);
            const int m = editor.data.steps[(size_t) s];
            const auto c = T (juce::Colour (timeModeColour (m)));
            g.setColour (m == tmNormal ? T (juce::Colour (0xff141722)) : c.withAlpha (0.55f)); g.fillRoundedRectangle (r, 4);
            g.setColour (m == tmNormal ? col::white.withAlpha (s % 4 == 0 ? 0.18f : 0.08f) : c); g.drawRoundedRectangle (r, 4, 1.0f);
            g.setColour (m == tmNormal ? col::dim.withAlpha (0.5f) : col::white); g.setFont (font (9.5f));
            g.drawText (timeModeName (m), r, juce::Justification::centred);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int s = stepAt (e.position.x); if (s < 0) return;
        paintMode = editor.data.steps[(size_t) s] == brush ? tmNormal : brush;
        apply (s);
    }
    void mouseDrag (const juce::MouseEvent& e) override { apply (stepAt (e.position.x)); }
private:
    int stepAt (float x) const { return juce::jlimit (0, kGateSteps - 1, (int) (x / ((float) getWidth() / kGateSteps))); }
    void apply (int s) { if (s < 0 || editor.data.steps[(size_t) s] == paintMode) return; editor.data.steps[(size_t) s] = (uint8_t) paintMode; editor.commit(); repaint(); editor.repaint(); }
    GateEditor& editor; int paintMode = 0;
};

#if GPFX_BOX
// GHOST GATE's PUMP (Ghost Box): rhythmic / drawn / sidechain ducking
class PumpPanel : public juce::Component
{
public:
    PumpPanel (juce::AudioProcessorValueTreeState& s, Rack::Impl& r) : apvts (s), rack (r), mode (s, pid (uGate, "pmode"), accentOf (uGate)),
        depth (s, pid (uGate, "pdepth"), "DEPTH", accentOf (uGate)), shape (s, pid (uGate, "pshape"), "PUMP SHAPE", accentOf (uGate)),
        atk (s, pid (uGate, "patk"), "ATTACK", accentOf (uGate)), hold (s, pid (uGate, "phold"), "HOLD", accentOf (uGate)),
        rel (s, pid (uGate, "prel"), "SC RELEASE", accentOf (uGate)), thr (s, pid (uGate, "pthr"), "SC THRESHOLD", accentOf (uGate)),
        only (s, pid (uGate, "ponly"), "PUMP ONLY", accentOf (uGate)), look (s, pid (uGate, "plook"), "LOOKAHEAD", accentOf (uGate)), len (s, pid (uGate, "plen"), accentOf (uGate)), curve (r.pumpShape)
    {
        curve.accent = accentOf (uGate); curve.rangeSemis = [] { return 1.0f; };
        curve.bars = [this] { return pumpLenBeats ((int) getParam (apvts, pid (uGate, "plen"))) / 4.0; };
        curve.amount = [this] { return getParam (apvts, pid (uGate, "pdepth")); };
        curve.onEditStart = [this] { rack.pushUndo(); };
        curve.valueText = [] (float v) { return juce::String (juce::roundToInt (v * 100)) + "%"; };
        only.setTooltip ("On: only the pump works (the pattern gate is bypassed). Off: pattern gate and pump together");
        atk.slider.setTooltip ("How fast the duck goes down (the slew of the gain drop): short = tight, long = soft");
        hold.slider.setTooltip ("Keeps the duck at its deepest for this long before the recovery (rhythmic modes and SIDECHAIN)");
        look.setTooltip ("SIDECHAIN: the audio runs 5 ms behind the key so the duck is already down when the kick lands (adds 5 ms latency, reported to the host)");
        mode.onChange = [this] (int) { updateMode(); };
        for (auto* c : std::initializer_list<juce::Component*> { &mode, &depth, &shape, &atk, &hold, &rel, &thr, &only, &look, &len, &curve }) addAndMakeVisible (c);
        updateMode();
    }
    int modeNow() const { return (int) getParam (apvts, pid (uGate, "pmode")); }
    void updateMode()
    {
        const int md = modeNow();
        curve.setVisible (md == pmDrawn); len.setVisible (md == pmDrawn);
        rel.setEnabled (md == pmSidechain); thr.setEnabled (md == pmSidechain); look.setEnabled (md == pmSidechain);
        shape.setEnabled (md != pmDrawn && md != pmSidechain); hold.setEnabled (md != pmDrawn);
        resized(); repaint();
    }
    void resized() override
    {
        auto r = getLocalBounds();
        mode.setBounds (r.removeFromTop (32)); r.removeFromTop (6);
        auto knobs = r.removeFromRight (3 * 84);
        const int kw = 84, kh = juce::jmin (92, (knobs.getHeight() - 30) / 2);
        depth.setBounds (knobs.getX(), knobs.getY(), kw, kh); shape.setBounds (knobs.getX() + kw, knobs.getY(), kw, kh); atk.setBounds (knobs.getX() + 2 * kw, knobs.getY(), kw, kh);
        hold.setBounds (knobs.getX(), knobs.getY() + kh + 4, kw, kh); rel.setBounds (knobs.getX() + kw, knobs.getY() + kh + 4, kw, kh); thr.setBounds (knobs.getX() + 2 * kw, knobs.getY() + kh + 4, kw, kh);
        look.setBounds (juce::Rectangle<int> (knobs.getX(), knobs.getY() + 2 * kh + 10, 3 * kw, 26).reduced (10, 1));
        auto bottom = r.removeFromBottom (30);
        only.setBounds (bottom.removeFromLeft (110).reduced (2, 2)); bottom.removeFromLeft (10);
        const bool drawn = modeNow() == pmDrawn;
        lenCap = bottom.removeFromLeft (drawn ? 96 : 0); if (drawn) len.setBounds (bottom.removeFromLeft (110).reduced (2, 2));
        info = bottom.withTrimmedLeft (10);
        r.removeFromRight (8); curveArea = r.withTrimmedBottom (4); curve.setBounds (curveArea);
    }
    void paint (juce::Graphics& g) override
    {
        const int md = modeNow(); const auto acc = accentOf (uGate);
        if (md == pmDrawn) drawCaption (g, lenCap.toFloat(), "DRAWN LENGTH", col::dim.brighter (0.25f), 10);
        else
        {   // the gain curve the mode really produces (the same formula the audio uses), read-only
            auto a = curveArea.toFloat(); drawScreen (g, a, acc); a = a.reduced (36, 24);
            const float depthV = getParam (apvts, pid (uGate, "pdepth")), ex = std::exp2 ((getParam (apvts, pid (uGate, "pshape")) - 0.5f) * 3.0f);
            const double period = md == pmHalf ? 2.0 : md == pmFast ? 0.5 : 1.0;
            const float holdFrac = (float) (getParam (apvts, pid (uGate, "phold")) * 0.001 / (period * 0.5));   // shown at 120 bpm
            for (float db : { 0.0f, -6.0f, -12.0f, -24.0f })
            {
                const float y = a.getY() + a.getHeight() * clampf (-db / 30.0f, 0.0f, 1.0f);
                g.setColour (col::white.withAlpha (db == 0.0f ? 0.25f : 0.08f)); g.fillRect (a.getX(), y, a.getWidth(), 1.0f);
                drawCaption (g, { curveArea.toFloat().getX() + 4, y - 6, 28, 12 }, juce::String ((int) db), col::dim, 8.5f, juce::Justification::centredRight);
            }
            if (md == pmSidechain)
            {
                const float thrV = getParam (apvts, pid (uGate, "pthr"));
                g.setColour (acc.withAlpha (0.85f)); g.setFont (font (10.0f));
                g.drawFittedText ("SIDECHAIN: the key's level above " + juce::String (thrV, 1) + " dB ducks the sound by up to " + juce::String (juce::roundToInt (depthV * 100)) + " %\n"
                                  + "attack " + juce::String (getParam (apvts, pid (uGate, "patk")), 1) + " ms, hold " + juce::String ((int) getParam (apvts, pid (uGate, "phold"))) + " ms, release " + juce::String ((int) getParam (apvts, pid (uGate, "prel"))) + " ms"
                                  + (getParam (apvts, pid (uGate, "plook")) > 0.5f ? ", 5 ms lookahead" : ""), a.withHeight (40).toNearestInt(), juce::Justification::topLeft, 3, 0.9f);
            }
            else if (md != pmOff)
            {
                juce::Path p;
                for (int i = 0; i <= 240; ++i)
                {
                    const double ph = i / 240.0; const float gn = pumpTargetAt (ph, depthV, ex, holdFrac);
                    const float x = a.getX() + a.getWidth() * (float) ph, y = a.getY() + a.getHeight() * clampf (-gainToDb (std::max (gn, 1.0e-3f)) / 30.0f, 0.0f, 1.0f);
                    if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
                }
                glowStroke (g, p, acc, 2.0f);
                drawCaption (g, { a.getX(), a.getBottom() + 2, a.getWidth(), 12 }, "one pump = " + juce::String (period == 0.5 ? "1/2 beat" : period == 1.0 ? "1 beat" : "2 beats") + "   deepest " + juce::String (gainToDb (std::max (1.0f - depthV, 1.0e-3f)), 1) + " dB   attack " + juce::String (getParam (apvts, pid (uGate, "patk")), 1) + " ms   hold " + juce::String ((int) getParam (apvts, pid (uGate, "phold"))) + " ms", col::dim, 9);
            }
        }
        juce::String t;
        if (md == pmSidechain) { const int key = rack.tm.pumpKey.load(); t = key == 2 ? "sidechain key live: the external key ducks the sound" : key == 1 ? "sidechain connected but silent for 1 s: the input ducks itself" : "no sidechain connected: the input ducks itself (route a kick to the sidechain input)"; }
        else t = md == pmDrawn ? "drawn pump: draw the duck below the line" : md == pmOff ? "pump is off" : "pump follows the song tempo";
        drawCaption (g, info.toFloat(), t + "   gain " + juce::String (gainToDb (rack.tm.pumpGain.load()), 1) + " dB", acc.withAlpha (0.85f), 10);
    }
private:
    juce::AudioProcessorValueTreeState& apvts; Rack::Impl& rack; Segmented mode; Knob depth, shape, atk, hold, rel, thr; Toggle only, look; Choice len; bendlane::Editor curve;
    juce::Rectangle<int> lenCap, info, curveArea;
};
#endif

class GatePage : public Page
{
public:
    GatePage (juce::AudioProcessorValueTreeState& s, Rack::Impl& r)
        : apvtsRef (s), rack (r), tiles (s, pid (uGate, "pattern"), kGatePatterns, accentOf (uGate)), editor (r), lane (editor),
          smooth (s, pid (uGate, "smooth"), "SMOOTH", accentOf (uGate)), timeAmt (s, pid (uGate, "time"), "TIME FX", accentOf (uGate)),
          length (s, pid (uGate, "length"), accentOf (uGate)), snapB ("SNAP", accentOf (uGate))
    {
        for (auto* c : std::initializer_list<juce::Component*> { &tiles, &editor, &lane, &smooth, &timeAmt, &length, &snapB, &invertB, &clearB, &resetB }) addAndMakeVisible (c);
#if GPFX_BOX
        pump = std::make_unique<PumpPanel> (s, r); addChildComponent (*pump);
        pumpB.setClickingTogglesState (true); pumpB.setColour (juce::TextButton::buttonOnColourId, accentOf (uGate));
        pumpB.setTooltip ("PUMP: sidechain-style ducking (4 on the floor, half-time, fast, drawn, or an external sidechain)");
        pumpB.onClick = [this] { showPump (pumpB.getToggleState()); };
        addAndMakeVisible (pumpB);
        pumpB.setToggleState (getParam (s, pid (uGate, "pmode")) > 0.5f, juce::dontSendNotification);
#endif
        tiles.onChange = [this] (int v) { editor.load (v - 1); lane.repaint(); };
        editor.load (tiles.value - 1);
        snapB.setToggleState (true, juce::dontSendNotification);
        snapB.onClick = [this] { editor.snap = snapB.getToggleState(); };
        invertB.onClick = [this] { for (auto& p : editor.data.pts) p.y = 1.0f - p.y; editor.commit(); editor.repaint(); };
        clearB.onClick = [this] { editor.data.pts = { { 0, 1, 0 }, { 1, 1, 0 } }; editor.data.steps.fill (0); editor.commit(); repaint(); };
        resetB.onClick = [this] { editor.data = factoryGatePattern (editor.pat); editor.commit(); repaint(); };
        for (int m = 0; m < kNumTimeModes; ++m)
        {
            auto* b = brushes.add (new juce::TextButton (m == 0 ? "ERASE" : timeModeName (m)));
            b->setClickingTogglesState (false); b->setColour (juce::TextButton::buttonOnColourId, T (juce::Colour (timeModeColour (m == 0 ? 0 : m))).withAlpha (1.0f));
            b->setToggleState (m == lane.brush, juce::dontSendNotification);
            b->onClick = [this, m] { lane.brush = m; for (int k = 0; k < brushes.size(); ++k) brushes[k]->setToggleState (k == m, juce::dontSendNotification); };
            addAndMakeVisible (b);
        }
    }
    void resized() override
    {
        auto r = getLocalBounds();
        auto right = r.removeFromRight (170).reduced (8, 0);
        auto top = r.removeFromTop (30);
        auto tools = top.removeFromRight (250);
#if GPFX_BOX
        pumpB.setBounds (top.removeFromRight (70).reduced (2, 3)); top.removeFromRight (6);
        if (pump) pump->setBounds (r.withTrimmedTop (34));
#endif
        tiles.setBounds (top.reduced (0, 2));
        for (auto* b : std::initializer_list<juce::Button*> { &snapB, &invertB, &clearB, &resetB }) b->setBounds (tools.removeFromLeft (62).reduced (2, 3));
        auto bottom = r.removeFromBottom (70);
        auto pal = bottom.removeFromBottom (26);
        const int bw = pal.getWidth() / kNumTimeModes;
        for (auto* b : brushes) b->setBounds (pal.removeFromLeft (bw).reduced (2, 1));
        lane.setBounds (bottom.reduced (0, 4));
        editor.setBounds (r.reduced (0, 4));
        right.removeFromTop (30);
        smooth.setBounds (right.removeFromTop (92).withSizeKeepingCentre (84, 90));
        timeAmt.setBounds (right.removeFromTop (92).withSizeKeepingCentre (84, 90));
        right.removeFromTop (8);
        length.setBounds (right.removeFromTop (28).reduced (6, 0));
    }
    void paint (juce::Graphics& g) override
    {
        auto right = getLocalBounds().removeFromRight (170).reduced (8, 0);
#if GPFX_BOX
        if (pumpOnlyShown) { drawCaption (g, right.removeFromTop (30).toFloat(), "PUMP ONLY", accentOf (uGate), 12, juce::Justification::centred); g.setColour (col::dim); g.setFont (font (9.5f)); g.drawFittedText ("the pattern gate is bypassed while PUMP ONLY is on (switch it off to run gate and pump together)", right.withHeight (70), juce::Justification::topLeft, 5, 0.9f); }
        else
#endif
        drawCaption (g, right.removeFromTop (30).toFloat(), "PATTERN " + juce::String (editor.pat + 1), accentOf (uGate), 12, juce::Justification::centred);
        drawCaption (g, getLocalBounds().removeFromBottom (70).removeFromBottom (26).translated (0, -28).toFloat().withHeight (14), "", col::dim);
    }
#if GPFX_BOX
    void tick (double) override
    {
        editor.repaint(); if (pump && pump->isVisible()) pump->repaint();
        const bool wantHidden = pump && pump->isVisible() && getParam (apvtsRef, pid (uGate, "ponly")) > 0.5f;
        if (wantHidden != pumpOnlyShown) showPump (pump->isVisible());
    }
#else
    void tick (double) override { editor.repaint(); }
#endif
#if GPFX_BOX
    void showPump (bool on)
    {
        pump->setVisible (on);
        for (auto* c : std::initializer_list<juce::Component*> { &editor, &lane, &tiles, &snapB, &invertB, &clearB, &resetB }) c->setVisible (! on);
        for (auto* b : brushes) b->setVisible (! on);
        const bool patternHidden = on && getParam (apvtsRef, pid (uGate, "ponly")) > 0.5f;   // PUMP ONLY bypasses the pattern gate: its controls go away
        for (auto* c : std::initializer_list<juce::Component*> { &smooth, &timeAmt, &length }) c->setVisible (! patternHidden);
        pumpOnlyShown = patternHidden;
        resized(); repaint();
    }
    void visibilityChanged() override { if (isVisible() && pump && pumpB.getToggleState() && ! pump->isVisible()) showPump (true); }
    void parentHierarchyChanged() override { if (pump && pumpB.getToggleState() && ! pump->isVisible()) showPump (true); }
    std::unique_ptr<PumpPanel> pump; juce::TextButton pumpB { "PUMP" }; bool pumpOnlyShown = false;
#endif
private:
    juce::AudioProcessorValueTreeState& apvtsRef; Rack::Impl& rack; IndexTiles tiles; GateEditor editor; TimeLane lane; Knob smooth, timeAmt; Choice length;
    Toggle snapB; juce::TextButton invertB { "INVERT" }, clearB { "CLEAR" }, resetB { "RESET" };
    juce::OwnedArray<juce::TextButton> brushes;
};

// ================================================================ GHOST GRID page
class GridCells : public juce::Component
{
public:
    explicit GridCells (Rack::Impl& r) : rack (r) {}
    int pat = 0, steps = 16; GridPattern data;
    void load (int p) { pat = p; data = rack.getGridPattern (p); repaint(); }
    juce::Rectangle<float> cellRect (int row, int s) const
    {
        const float w = (float) getWidth() / (float) steps, h = (float) getHeight() / kGridRows;
        return { (float) s * w, (float) row * h, w, h };
    }
    void paint (juce::Graphics& g) override
    {
        drawScreen (g, getLocalBounds().toFloat(), accentOf (uGrid), 6);
        const bool live = rack.tm.gridPattern.load() == pat;
        const int playStep = live ? juce::jlimit (0, steps - 1, (int) (rack.tm.gridPos.load() * (float) steps)) : -1;
        for (int row = 0; row < kGridRows; ++row)
        {
            const auto c = T (juce::Colour (gridRow (row).colour));
            for (int s = 0; s < steps; ++s)
            {
                auto r = cellRect (row, s).reduced (1.5f);
                const bool on = (data.rows[(size_t) row] >> s) & 1u;
                const bool beat = (s % (steps / 4)) == 0;
                if (on)
                {
                    const bool hot = s == playStep;
                    g.setColour (c.withAlpha (hot ? 0.35f : 0.15f)); g.fillRoundedRectangle (r.expanded (1.0f), 4);
                    juce::ColourGradient gr (c.brighter (hot ? 0.6f : 0.1f), 0, r.getY(), c.darker (0.5f), 0, r.getBottom(), false);
                    g.setGradientFill (gr); g.fillRoundedRectangle (r, 3);
                }
                else
                {
                    g.setColour (s == playStep ? T (juce::Colour (0xff262a3a)) : (beat ? T (juce::Colour (0xff171a26)) : T (juce::Colour (0xff12141d))));
                    g.fillRoundedRectangle (r, 3);
                }
            }
        }
        if (playStep >= 0)
        {
            auto pr = cellRect (0, playStep).withHeight ((float) getHeight());
            g.setColour (col::white.withAlpha (0.10f)); g.drawRoundedRectangle (pr.reduced (0.5f), 3, 1.0f);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        int row, s; if (! hit (e.position, row, s)) return;
        paintValue = ! ((data.rows[(size_t) row] >> s) & 1u);
        apply (row, s);
    }
    void mouseDrag (const juce::MouseEvent& e) override { int row, s; if (hit (e.position, row, s)) apply (row, s); }
private:
    bool hit (juce::Point<float> p, int& row, int& s) const
    {
        row = (int) (p.y / ((float) getHeight() / kGridRows)); s = (int) (p.x / ((float) getWidth() / (float) steps));
        return row >= 0 && row < kGridRows && s >= 0 && s < steps;
    }
    void apply (int row, int s)
    {
        const uint32_t bit = 1u << s; auto& m = data.rows[(size_t) row];
        const uint32_t nm = paintValue ? (m | bit) : (m & ~bit);
        if (nm == m) return;
        m = nm; rack.setGridPattern (pat, data); repaint();
    }
    Rack::Impl& rack; bool paintValue = true;
};

class GridPage : public Page
{
public:
    GridPage (juce::AudioProcessorValueTreeState& s, Rack::Impl& r)
        : rack (r), tiles (s, pid (uGrid, "pattern"), kGridPatterns, accentOf (uGrid)), steps (s, pid (uGrid, "steps"), accentOf (uGrid)), cells (r)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &tiles, &steps, &cells, &clearB, &resetB }) addAndMakeVisible (c);
        tiles.onChange = [this] (int v) { cells.load (v - 1); };
        steps.onChange = [this] (int v) { cells.steps = v == 1 ? 32 : 16; cells.repaint(); };
        cells.load (tiles.value - 1); cells.steps = steps.value == 1 ? 32 : 16;
        clearB.onClick = [this] { cells.data.rows.fill (0); rack.setGridPattern (cells.pat, cells.data); cells.repaint(); };
        resetB.onClick = [this] { cells.data = factoryGridPattern (cells.pat); rack.setGridPattern (cells.pat, cells.data); cells.repaint(); };
        for (int row = 0; row < kGridRows; ++row)
        {
            auto* k = knobs.add (new FineSlider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox));
            k->setColour (juce::Slider::rotarySliderFillColourId, T (juce::Colour (gridRow (row).colour)));
            k->setRotaryParameters (kPi * 1.25f, kPi * 2.75f, true);
            k->setTooltip (juce::String (gridRow (row).name) + " " + gridRow (row).knob);
            atts.add (new juce::AudioProcessorValueTreeState::SliderAttachment (s, pid (uGrid, gridRow (row).key), *k));
            k->getProperties().set ("gpfxParam", pid (uGrid, gridRow (row).key));
            if (auto* gp = s.getParameter (pid (uGrid, gridRow (row).key))) { k->setDoubleClickReturnValue (true, gp->convertFrom0to1 (gp->getDefaultValue())); k->param = gp; }
            addAndMakeVisible (k);
        }
    }
    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (30);
        tiles.setBounds (top.removeFromLeft (juce::jmin (330, top.getWidth() / 3)).reduced (0, 2));
        top.removeFromLeft (12);
        steps.setBounds (top.removeFromLeft (200).reduced (0, 2));
        resetB.setBounds (top.removeFromRight (70).reduced (2, 3)); clearB.setBounds (top.removeFromRight (70).reduced (2, 3));
        r.removeFromTop (4);
        labels = r.removeFromLeft (100);
        auto kn = r.removeFromRight (120);
        cells.setBounds (r);
        const float h = (float) r.getHeight() / kGridRows;
        for (int row = 0; row < kGridRows; ++row)
            knobs[row]->setBounds (kn.getX() + 6, r.getY() + (int) (h * (float) row), (int) h, (int) h);
        knobArea = kn;
    }
    void paint (juce::Graphics& g) override
    {
        const float h = (float) cells.getHeight() / kGridRows;
        const int active = rack.tm.gridActive.load();
        for (int row = 0; row < kGridRows; ++row)
        {
            const auto c = T (juce::Colour (gridRow (row).colour));
            auto lr = juce::Rectangle<float> ((float) labels.getX(), (float) cells.getY() + h * (float) row, (float) labels.getWidth() - 6, h).reduced (0, 2);
            const bool hot = (active >> row) & 1;
            if (hot) { g.setColour (c.withAlpha (0.18f)); g.fillRoundedRectangle (lr, 5); }
            g.setColour (c.withAlpha (hot ? 1.0f : 0.6f)); g.fillRoundedRectangle (lr.getX(), lr.getY() + 3, 3, lr.getHeight() - 6, 1.5f);
            drawCaption (g, lr.withTrimmedLeft (9), gridRow (row).name, hot ? col::white : col::white.withAlpha (0.75f), 11);
            auto kr = juce::Rectangle<float> ((float) knobArea.getX() + h + 8, (float) cells.getY() + h * (float) row, (float) knobArea.getWidth() - h - 8, h);
            drawCaption (g, kr.withTrimmedBottom (h * 0.5f), gridRow (row).knob, col::dim, 8.5f);
            drawCaption (g, kr.withTrimmedTop (h * 0.45f), juce::String (juce::roundToInt (knobs[row]->getValue() * 100)) + "%", c, 9.5f);
        }
    }
    void tick (double) override { cells.repaint(); repaint (labels); }
private:
    Rack::Impl& rack; IndexTiles tiles; Segmented steps; GridCells cells;
    juce::TextButton clearB { "CLEAR" }, resetB { "RESET" };
    juce::OwnedArray<FineSlider> knobs; juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> atts;
    juce::Rectangle<int> labels, knobArea;
};

// ================================================================ HAUNT EDIT page
class PadGrid : public juce::Component
{
public:
    explicit PadGrid (Rack::Impl& r) : rack (r) {}
    std::function<void (int)> onSelect;
    bool latch = false; int selected = 0;
    juce::Rectangle<float> padRect (int i) const
    {
        const float w = (float) getWidth() / 4.0f, h = (float) getHeight() / 4.0f;
        return { (float) (i % 4) * w, (float) (i / 4) * h, w, h };
    }
    void paint (juce::Graphics& g) override
    {
        const int held = rack.tm.hauntPad.load(); const float env = rack.tm.hauntEnv.load(), prog = rack.tm.hauntProgress.load();
        for (int i = 0; i < kPads; ++i)
        {
            auto r = padRect (i).reduced (5);
            const auto d = rack.getPad (i);
            const auto c = T (juce::Colour (hauntTypeColour (d.type)));
            const bool down = held == i;
            if (down) { g.setColour (c.withAlpha (0.25f + 0.25f * env)); g.fillRoundedRectangle (r.expanded (4), 14); }
            juce::ColourGradient gr (down ? c.withAlpha (0.9f) : c.withAlpha (fxtheme::light() ? 0.30f : 0.16f), 0, r.getY(), down ? c.darker (0.6f) : T (juce::Colour (0xff12101c)), 0, r.getBottom(), false);
            g.setGradientFill (gr); g.fillRoundedRectangle (r, 11);
            g.setColour (c.withAlpha (i == selected ? 1.0f : 0.5f)); g.drawRoundedRectangle (r, 11, i == selected ? 2.0f : 1.0f);
            if (down)
            {
                juce::Path arc; arc.addCentredArc (r.getRight() - 16, r.getY() + 16, 8, 8, 0, 0, kTwoPi * prog, true);
                g.setColour (col::white); g.strokePath (arc, juce::PathStrokeType (2.0f));
            }
            g.setColour (down ? col::white : (fxtheme::light() ? c.darker (0.35f) : c.brighter (0.4f))); g.setFont (font (juce::jmin (15.0f, r.getHeight() * 0.2f)));   // LIGHT: darker ink, not paler
            g.drawText (hauntTypeNames()[d.type], r.withTrimmedBottom (r.getHeight() * 0.3f), juce::Justification::centred);
            g.setColour (down ? col::white.withAlpha (0.8f) : col::dim); g.setFont (font (11.0f));
            g.drawText (padDivs()[(size_t) d.div].name, r.withTrimmedTop (r.getHeight() * 0.55f), juce::Justification::centred);
            g.setColour (col::white.withAlpha (0.25f)); g.setFont (font (9.0f));
            g.drawText (juce::String (i + 1), r.reduced (8, 6), juce::Justification::topLeft);
        }
    }
    int padAt (juce::Point<float> p) const { for (int i = 0; i < kPads; ++i) if (padRect (i).contains (p)) return i; return -1; }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = padAt (e.position); if (i < 0) return;
        selected = i; if (onSelect) onSelect (i);
        if (latch) rack.haunt->uiPad.store (rack.haunt->uiPad.load() == i ? -1 : i);
        else rack.haunt->uiPad.store (i);
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override { if (! latch) rack.haunt->uiPad.store (-1); repaint(); }
private:
    Rack::Impl& rack;
};

class HauntPage : public Page
{
public:
    HauntPage (juce::AudioProcessorValueTreeState& s, Rack::Impl& r)
        : rack (r), pads (r), motion ("MOTION", accentOf (uHaunt)), sync (s, pid (uHaunt, "sync"), "SYNC START", accentOf (uHaunt)),
          latch ("LATCH", accentOf (uHaunt))
    {
        for (auto* c : std::initializer_list<juce::Component*> { &pads, &type, &size, &motion, &sync, &latch }) addAndMakeVisible (c);
        type.addItemList (hauntTypeNames(), 1); size.addItemList (divNames (padDivs()), 1);
        for (auto* c : { &type, &size }) { c->setColour (juce::ComboBox::outlineColourId, accentOf (uHaunt)); c->setColour (juce::ComboBox::arrowColourId, accentOf (uHaunt)); }
        motion.slider.setRange (0.0, 1.0, 0.01);
        pads.onSelect = [this] (int) { refresh(); };
        type.onChange = [this] { auto d = rack.getPad (pads.selected); d.type = type.getSelectedItemIndex(); rack.setPad (pads.selected, d); pads.repaint(); };
        size.onChange = [this] { auto d = rack.getPad (pads.selected); d.div = size.getSelectedItemIndex(); rack.setPad (pads.selected, d); pads.repaint(); };
        motion.slider.onValueChange = [this] { auto d = rack.getPad (pads.selected); d.motion = (float) motion.slider.getValue(); rack.setPad (pads.selected, d); };
        latch.onClick = [this] { pads.latch = latch.getToggleState(); if (! pads.latch) rack.haunt->uiPad.store (-1); };
        refresh();
    }
    ~HauntPage() override { rack.haunt->uiPad.store (-1); }
    void refresh()
    {
        const auto d = rack.getPad (pads.selected);
        type.setSelectedItemIndex (d.type, juce::dontSendNotification); size.setSelectedItemIndex (d.div, juce::dontSendNotification);
        motion.slider.setValue (d.motion, juce::dontSendNotification);
        repaint();
    }
    void resized() override
    {
        auto r = getLocalBounds();
        auto right = r.removeFromRight (juce::jmax (260, r.getWidth() * 34 / 100));
        pads.setBounds (r.reduced (0, 2));
        right.reduce (14, 0);
        vis = right.removeFromTop (right.getHeight() - 170);
        right.removeFromTop (22);
        auto row = right.removeFromTop (28); type.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (3, 0)); size.setBounds (row.reduced (3, 0));
        right.removeFromTop (6);
        auto row2 = right.removeFromTop (100);
        motion.setBounds (row2.removeFromLeft (90));
        auto tg = row2.reduced (8, 18);
        sync.setBounds (tg.removeFromTop (28)); tg.removeFromTop (8); latch.setBounds (tg.removeFromTop (28));
    }
    void paint (juce::Graphics& g) override
    {
        const auto acc = accentOf (uHaunt);
        drawScreen (g, vis.toFloat(), acc);
        const float env = rack.tm.hauntEnv.load();
        const int held = rack.tm.hauntPad.load();
        auto v = vis.toFloat();
        const auto c = held >= 0 ? T (juce::Colour (hauntTypeColour (rack.getPad (held).type))) : acc;
        // slices: stutter lines march when held
        for (int k = 0; k < 24; ++k)
        {
            const float x = v.getX() + v.getWidth() * (float) k / 24.0f;
            const float hgt = v.getHeight() * (0.15f + 0.6f * env * hashFloat ((uint32_t) (k * 31 + (int) (t * 12) * 7)));
            g.setColour (c.withAlpha (0.10f + 0.4f * env)); g.fillRect (x + 2, v.getCentreY() - hgt * 0.5f, v.getWidth() / 24.0f - 4, hgt);
        }
        ghosty::drawGhost (g, ghosty::lookFor (11), v.getCentreX(), v.getCentreY(), v.getHeight() * 0.45f, env, held >= 0 && env > 0.5f, 1.0f);
        drawCaption (g, v.reduced (10, 6).removeFromTop (14), held >= 0 ? juce::String ("PAD ") + juce::String (held + 1) + "  " + hauntTypeNames()[rack.getPad (held).type] : "HOLD A PAD", c, 11);
        auto lab = juce::Rectangle<float> ((float) type.getX(), (float) type.getY() - 18, (float) (size.getRight() - type.getX()), 14);
        drawCaption (g, lab, "PAD " + juce::String (pads.selected + 1) + "  GESTURE / SIZE", col::dim, 10);
    }
    void tick (double now) override { t = now; pads.repaint(); repaint (vis); }
private:
    Rack::Impl& rack; PadGrid pads; juce::ComboBox type, size; Knob motion; Toggle sync, latch; juce::Rectangle<int> vis; double t = 0;
};

// ================================================================ generic unit page: visual + knobs + switches
struct KnobSpec { const char* id; const char* caption; };
class UnitPage : public Page
{
public:
    using VisFn = std::function<void (juce::Graphics&, juce::Rectangle<float>, double)>;
    UnitPage (juce::AudioProcessorValueTreeState& s, int unit, const char* segId, std::vector<KnobSpec> knobs,
              std::vector<KnobSpec> toggles, std::vector<KnobSpec> combos, VisFn visFn, std::vector<int> segSubset = {})
        : u (unit), drawVis (std::move (visFn))
    {
        const auto acc = accentOf (u);
        if (segId != nullptr) { seg = std::make_unique<Segmented> (s, pid (u, segId), acc, std::move (segSubset)); addAndMakeVisible (*seg); }
        for (auto& k : knobs) addAndMakeVisible (kn.add (new Knob (s, pid (u, k.id), k.caption, acc)));
        for (auto& k : toggles) addAndMakeVisible (tg.add (new Toggle (s, pid (u, k.id), k.caption, acc)));
        for (auto& k : combos) { addAndMakeVisible (cb.add (new Choice (s, pid (u, k.id), acc))); cbCaps.add (k.caption); }
    }
    void resized() override
    {
        auto r = getLocalBounds();
        if (seg) { seg->setBounds (r.removeFromTop (36)); r.removeFromTop (6); }
        const int cols = kn.size() <= 4 ? 2 : kn.size() <= 6 ? 3 : 4;
        auto right = r.removeFromRight (cols * 88 + 10).withTrimmedLeft (10);
        vis = r;
        auto sw = right.removeFromBottom ((tg.isEmpty() ? 0 : 34) + (cb.isEmpty() ? 0 : 46));
        if (! cb.isEmpty())
        {
            auto row = sw.removeFromTop (46).withTrimmedTop (16);
            const int w = row.getWidth() / cb.size();
            for (auto* c : cb) c->setBounds (row.removeFromLeft (w).reduced (3, 0));
        }
        if (! tg.isEmpty())
        {
            auto row = sw.removeFromTop (34).reduced (0, 3);
            const int w = row.getWidth() / tg.size();
            for (auto* b : tg) b->setBounds (row.removeFromLeft (w).reduced (3, 0));
        }
        std::vector<juce::Component*> items; for (auto* k : kn) items.push_back (k);
        gridLayout (right, items, cols, juce::jmin (96, right.getHeight() / juce::jmax (1, (kn.size() + cols - 1) / cols)));
    }
    void paint (juce::Graphics& g) override
    {
        drawScreen (g, vis.toFloat(), accentOf (u));
        {
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (vis.reduced (2));
            if (drawVis) drawVis (g, vis.toFloat().reduced (4), t);
        }
        for (int i = 0; i < cb.size(); ++i)
            drawCaption (g, cb[i]->getBounds().toFloat().translated (0, -15).withHeight (13), cbCaps[i], col::dim.brighter (0.25f), 10, juce::Justification::centred);
    }
    void tick (double now) override { t = now; repaint (vis); }
private:
    int u; VisFn drawVis; std::unique_ptr<Segmented> seg;
    juce::OwnedArray<Knob> kn; juce::OwnedArray<Toggle> tg; juce::OwnedArray<Choice> cb; juce::StringArray cbCaps;
    juce::Rectangle<int> vis; double t = 0;
};

// ---------------------------------------------------------------- per-unit visuals
struct History { std::array<float, 200> v {}; int w = 0; void push (float x) { v[(size_t) w] = x; w = (w + 1) % 200; } float at (int k) const { return v[(size_t) ((w + k) % 200)]; } };

void drawMeter (juce::Graphics& g, juce::Rectangle<float> r, float v01, juce::Colour c, const juce::String& label)
{
    g.setColour (T (juce::Colour (0xff12141c))); g.fillRoundedRectangle (r, 4);
    auto f = r.withTrimmedTop (r.getHeight() * (1.0f - clampf (v01, 0, 1)));
    juce::ColourGradient gr (c.brighter (0.4f), 0, r.getY(), c.darker (0.4f), 0, r.getBottom(), false);
    g.setGradientFill (gr); g.fillRoundedRectangle (f, 4);
    drawCaption (g, r.withY (r.getBottom() + 2).withHeight (12), label, col::dim, 9, juce::Justification::centred);
}

#if GPFX_BOX
#include "FxBoxPages.inc"
#else
#include "FxFattenPage.inc"   // Ghosty Pro: FATTEN (2026-10-23)
#include "FxProPages.inc"     // FX RACK (round 3): DISTORTION, COMPRESSOR, SPLITTER pages
#endif

// ECTO ECHO visual: every input hit throws a ghost that bounces wall to wall, hitting a wall on each repeat.
struct EchoViz
{
    struct Throw { double t0 = -1; float level = 0; int lane = 0; };
    std::array<Throw, 8> th {}; int next = 0; float lastIn = 0, slowIn = 0; double lastSpawn = -10;
    void update (double t, float in, double delaySec)
    {
        slowIn += 0.08f * (in - slowIn);
        const bool onset = in > 0.02f && in > lastIn * 1.4f + 0.008f && in > slowIn * 1.3f && t - lastSpawn > std::max (0.09, delaySec * 0.45);
        const bool sustain = in > 0.03f && t - lastSpawn > std::max (0.7, delaySec * 4.0);
        if (onset || sustain) { th[(size_t) next] = { t, clampf (in * 2.2f, 0.25f, 1.0f), next % 3 }; next = (next + 1) % 8; lastSpawn = t; }
        lastIn = in;
    }
};

void drawEchoGhosts (juce::Graphics& g, juce::Rectangle<float> r, double t, Telemetry& tm, juce::AudioProcessorValueTreeState& s, EchoViz& ev)
{
    const auto c = accentOf (uEcho);
    const float fb = getParam (s, pid (uEcho, "fb")); const bool ping = getParam (s, pid (uEcho, "ping")) > 0.5f;
    const int mode = (int) getParam (s, pid (uEcho, "mode")); const float ch = getParam (s, pid (uEcho, "char"));
    float ms = tm.echoMs.load();
    if (ms < 1.0f) ms = getParam (s, pid (uEcho, "sync")) > 0.5f ? (float) echoDivs()[(size_t) juce::jlimit (0, (int) echoDivs().size() - 1, (int) getParam (s, pid (uEcho, "div")))].beats * 500.0f : getParam (s, pid (uEcho, "time"));
    const double D = std::max (0.02, (double) ms * 0.001);
    const float outLvl = clampf (tm.echoLevel.load() * 2.5f, 0.0f, 1.0f), inLvl = tm.echoIn.exchange (0.0f);
    const bool on = getParam (s, pid (uEcho, "on")) > 0.5f;
    if (on) ev.update (t, inLvl, D);
    const float wob = (mode == 0 ? 1.0f : mode == 2 ? 0.55f : mode == 1 ? 0.3f : 0.0f) * (0.3f + ch);
    auto timeline = r.removeFromBottom (40);
    const float wallL = r.getX() + 26, wallR = r.getRight() - 26, cx = 0.5f * (wallL + wallR), half = 0.5f * (wallR - wallL) - 20.0f;
    // walls light up when a ghost hits them
    float hitL = 0, hitR = 0;
    struct G { float x, y, size, alpha, glitch; bool flip; };
    std::vector<G> ghosts;
    for (auto& tw : ev.th)
    {
        if (tw.t0 < 0) continue;
        const double ph = (t - tw.t0) / D;
        if (ph < 0) continue;
        const int k = (int) std::floor (ph);
        const float lvl = tw.level * std::pow (std::max (0.001f, fb), (float) std::max (0, k));
        if (k > 60 || (k >= 1 && lvl < 0.035f)) { tw.t0 = -1; continue; }
        const float fr = (float) (ph - k);
        // position: from the middle to the first wall, then wall to wall (ping-pong) or wall-middle-wall (both sides)
        float sx;
        if (ping) { if (ph < 1.0) sx = -(float) ph; else { const float u = (float) std::fmod (ph, 2.0); sx = u < 1.0f ? 1.0f - 2.0f * u : 2.0f * u - 3.0f; } }
        else sx = ph < 1.0 ? (float) ph : 1.0f - 2.0f * std::abs (fr - (fr > 0.5f ? 1.0f : 0.0f));
        const float hop = std::sin (kPi * (ph < 1.0 ? (float) ph * 0.5f + 0.5f : fr)) * r.getHeight() * 0.10f * (ping ? 1.0f : 0.6f);
        const float laneY = r.getCentreY() + ((float) tw.lane - 1.0f) * r.getHeight() * 0.20f;
        const float wy = wob * (float) (std::sin (t * 3.1 + tw.lane) * 5.0 + std::sin (t * 17.0 + k) * 1.5);
        const float sz = 18.0f + 30.0f * lvl;
        const float al = clampf (0.25f + 0.9f * lvl, 0.0f, 1.0f) * (0.6f + 0.4f * std::max (outLvl, 0.4f));
        const float glitch = mode == 3 ? 0.0f : clampf (0.08f * (float) k * ch, 0.0f, 0.6f);
        ghosts.push_back ({ cx + sx * half, laneY - hop + wy, sz, al, glitch, sx < 0 });
        if (! ping) ghosts.push_back ({ cx - sx * half, laneY - hop + wy, sz, al, glitch, sx > 0 });   // both sides at once
        if (ph >= 0.9 && (fr < 0.12f || fr > 0.94f))
        {
            const int wallK = fr > 0.5f ? k + 1 : k;
            const float hl = tw.level * std::pow (std::max (0.001f, fb), (float) std::max (0, wallK - 1));
            if (ping) { if (wallK % 2 == 1) hitL = std::max (hitL, hl); else hitR = std::max (hitR, hl); }
            else { hitL = std::max (hitL, hl); hitR = std::max (hitR, hl); }
        }
    }
    for (int side = 0; side < 2; ++side)
    {
        const float x = side == 0 ? wallL : wallR, h = side == 0 ? hitL : hitR;
        juce::ColourGradient wg (c.withAlpha (0.10f + 0.6f * h), x, r.getCentreY(), c.withAlpha (0.0f), x + (side == 0 ? 40.0f : -40.0f), r.getCentreY(), false);
        g.setGradientFill (wg); g.fillRect (side == 0 ? x : x - 40.0f, r.getY() + 8, 40.0f, r.getHeight() - 16);
        g.setColour (c.withAlpha (0.35f + 0.65f * h)); g.fillRoundedRectangle (x - 2.0f, r.getY() + 8, 4.0f, r.getHeight() - 16, 2.0f);
        drawCaption (g, { x - 10, r.getBottom() - 22, 20, 12 }, side == 0 ? "L" : "R", col::white.withAlpha (0.6f), 9.5f, juce::Justification::centred);
    }
    // the source ghost in the middle (it "shouts" when the input hits)
    auto src = ghosty::lookFor (23);
    ghosty::drawGhost (g, src, cx, r.getCentreY(), 34.0f + 10.0f * clampf (inLvl * 3.0f, 0.0f, 1.0f), 0, false, on ? 0.9f : 0.4f);
    for (auto& gh : ghosts) { auto L = ghosty::lookFor (23); L.slime = c; ghosty::drawGhost (g, L, gh.x, gh.y, gh.size, gh.glitch, false, gh.alpha, gh.flip); }
    // timeline: one tick per repeat, height = its level, coloured by side
    auto tl = timeline.reduced (14, 6);
    g.setColour (col::white.withAlpha (0.06f)); g.drawHorizontalLine ((int) tl.getBottom(), tl.getX(), tl.getRight());
    const float spanS = (float) std::max (1.0, std::min (8.0, D * 12.0));
    float a = 1.0f; int audible = 0;
    for (int k = 1; k <= 64; ++k)
    {
        a *= std::max (0.0f, fb);
        if (a < 0.03f) break;
        ++audible;
        const float x = tl.getX() + tl.getWidth() * (float) (D * k) / spanS;
        if (x > tl.getRight()) continue;
        const juce::Colour tc = ping ? (k % 2 ? col::ice : col::pink) : c;
        g.setColour (tc.withAlpha (0.4f + 0.6f * a)); g.fillRoundedRectangle (x - 1.5f, tl.getBottom() - tl.getHeight() * a, 3.0f, tl.getHeight() * a, 1.5f);
    }
    g.setColour (col::white.withAlpha (0.8f)); g.fillRoundedRectangle (tl.getX() - 1.5f, tl.getY(), 3.0f, tl.getHeight(), 1.5f);
    drawCaption (g, r.reduced (10, 6).removeFromTop (14), juce::String (juce::roundToInt (ms)) + " ms   " + juce::String (audible) + (audible == 1 ? " repeat" : " repeats") + (ping ? "   PING-PONG" : ""), c, 11);
    drawCaption (g, timeline.reduced (14, 0).removeFromTop (12), juce::String (spanS, 1) + " s", col::dim, 9, juce::Justification::centredRight);
}

std::unique_ptr<Page> makeUnitPage (int u, juce::AudioProcessorValueTreeState& s, Rack::Impl& rk)
{
    auto& tm = rk.tm;
    auto* sp = &s;
    switch (u)
    {
        case uWreck:
            return std::make_unique<UnitPage> (s, u, "style",
                std::vector<KnobSpec> { { "drive", "DRIVE" }, { "tone", "TONE" }, { "out", "OUTPUT" }, { "lowcut", "LOW CUT" }, { "highcut", "HIGH CUT" } },
                std::vector<KnobSpec> { { "thump", "THUMP" }, { "steep", "STEEP" }, { "punish", "PUNISH" }, { "auto", "AUTO" } }, std::vector<KnobSpec> {},
                [&tm, sp] (juce::Graphics& g, juce::Rectangle<float> r, double t)
                {
                    const int st = (int) getParam (*sp, pid (uWreck, "style"));
                    const float drive = getParam (*sp, pid (uWreck, "drive")) + (getParam (*sp, pid (uWreck, "punish")) > 0.5f ? 0.3f : 0.0f);
                    const float gdr = dbToGain (drive * 36.0f);
                    auto a = r.reduced (24, 20).withTrimmedRight (60);
                    a = a.withSizeKeepingCentre (juce::jmin (a.getWidth(), a.getHeight() * 1.5f), a.getHeight());
                    g.setColour (col::white.withAlpha (0.06f));
                    g.drawHorizontalLine ((int) a.getCentreY(), a.getX(), a.getRight()); g.drawVerticalLine ((int) a.getCentreX(), a.getY(), a.getBottom());
                    juce::Path p;
                    for (int i = 0; i <= 200; ++i)
                    {
                        const float x = -1.0f + 2.0f * (float) i / 200.0f;
                        float y = st == wsDecimate ? x : wreckShape (st, x, st >= wsCrush ? 1.0f : gdr, clampf (drive, 0, 1));
                        if (st < wsCrush) y *= std::pow (gdr, -0.15f);
                        const juce::Point<float> pt (a.getCentreX() + x * a.getWidth() * 0.5f, a.getCentreY() - clampf (y, -1.2f, 1.2f) * a.getHeight() * 0.42f);
                        if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
                    }
                    glowStroke (g, p, accentOf (uWreck), 2.2f);
                    drawCaption (g, a.removeFromTop (14), wreckStyleNames()[st] + "  TRANSFER", col::white.withAlpha (0.6f), 10);
                    auto m = r.removeFromRight (64).reduced (8, 26);
                    drawMeter (g, m.removeFromLeft (16), tm.wreckIn.load(), col::ice, "IN"); m.removeFromLeft (6);
                    drawMeter (g, m.removeFromLeft (20), tm.wreckOut.load(), accentOf (uWreck), "OUT");
                    driftGhost (g, r, t, 5, 0.07f, tm.wreckOut.load() * drive);
                });
        case uTube:
            return std::make_unique<UnitPage> (s, u, "char",
                std::vector<KnobSpec> { { "drive", "DRIVE" }, { "bass", "BASS" }, { "treble", "TREBLE" }, { "out", "OUTPUT" } },
                std::vector<KnobSpec> {}, std::vector<KnobSpec> {},
                [&tm, sp] (juce::Graphics& g, juce::Rectangle<float> r, double t)
                {
                    const float lvl = clampf (tm.tubeLevel.load() * 1.4f, 0, 1), drv = getParam (*sp, pid (uTube, "drive"));
                    auto tube = r.withSizeKeepingCentre (juce::jmin (130.0f, r.getWidth() * 0.3f), r.getHeight() * 0.8f);
                    const auto warm = T (juce::Colour (0xffFF9E5A));
                    juce::ColourGradient glow (warm.withAlpha (0.25f + 0.45f * lvl), tube.getCentreX(), tube.getCentreY(), warm.withAlpha (0.0f), tube.getCentreX() + tube.getWidth() * 1.6f, tube.getCentreY(), true);
                    g.setGradientFill (glow); g.fillEllipse (tube.expanded (tube.getWidth() * 1.2f, 20));
                    g.setColour (col::white.withAlpha (0.12f)); g.drawRoundedRectangle (tube, tube.getWidth() * 0.45f, 2.0f);
                    for (int k = 0; k < 5; ++k)
                    {
                        juce::Path f; const float y0 = tube.getY() + tube.getHeight() * (0.3f + 0.1f * (float) k);
                        f.startNewSubPath (tube.getX() + 20, y0);
                        for (int i = 1; i <= 20; ++i) f.lineTo (tube.getX() + 20 + (tube.getWidth() - 40) * (float) i / 20.0f, y0 + std::sin ((float) i * 1.3f + (float) t * 3.0f * (0.3f + drv)) * 4.0f * (0.3f + lvl));
                        glowStroke (g, f, warm.withAlpha (0.4f + 0.6f * lvl), 1.5f);
                    }
                    driftGhost (g, r, t, 9, 0.08f);
                    drawCaption (g, r.reduced (10, 6).removeFromTop (14), "WARMTH", warm, 10);
                });
        case uCrush:
        {
            auto hist = std::make_shared<History>();
            return std::make_unique<UnitPage> (s, u, nullptr,
                std::vector<KnobSpec> { { "crush", "CRUSH" }, { "dark", "DARKNESS" }, { "release", "RELEASE" }, { "out", "OUTPUT" } },
                std::vector<KnobSpec> {}, std::vector<KnobSpec> {},
                [&tm, hist] (juce::Graphics& g, juce::Rectangle<float> r, double t)
                {
                    hist->push (tm.crushGr.load());
                    const auto c = accentOf (uCrush);
                    juce::Path p; auto a = r.reduced (16, 24);
                    for (int k = 0; k < 200; ++k)
                    {
                        const float y = a.getY() + clampf (-hist->at (k) / 40.0f, 0, 1) * a.getHeight();
                        const float x = a.getX() + a.getWidth() * (float) k / 199.0f;
                        if (k == 0) { p.startNewSubPath (x, a.getY()); }
                        p.lineTo (x, y);
                    }
                    p.lineTo (a.getRight(), a.getY()); p.closeSubPath();
                    g.setColour (c.withAlpha (0.35f)); g.fillPath (p);
                    for (int db = 10; db <= 30; db += 10) { const float y = a.getY() + a.getHeight() * (float) db / 40.0f; g.setColour (col::white.withAlpha (0.06f)); g.drawHorizontalLine ((int) y, a.getX(), a.getRight()); drawCaption (g, { a.getX(), y - 12, 60, 12 }, "-" + juce::String (db) + " dB", col::dim, 9); }
                    driftGhost (g, r, t, 13, 0.08f, clampf (-tm.crushGr.load() / 20.0f, 0, 1));
                    drawCaption (g, r.reduced (10, 6).removeFromTop (14), "GAIN SMASH  " + juce::String (tm.crushGr.load(), 1) + " dB", c, 10);
                });
        }
        case uFilter:
            return std::make_unique<UnitPage> (s, u, "type",
                std::vector<KnobSpec> { { "cutoff", "CUTOFF" }, { "reso", "RESO" }, { "drive", "DRIVE" }, { "spread", "SPREAD" },
                                        { "lfoamt", "LFO AMT" }, { "lforate", "LFO HZ" }, { "envamt", "ENV AMT" }, { "envrel", "ENV REL" } },
                std::vector<KnobSpec> { { "lfosync", "LFO SYNC" } },
                std::vector<KnobSpec> { { "poles", "SLOPE" }, { "routing", "ROUTING" }, { "lfoshape", "LFO SHAPE" }, { "lfodiv", "LFO DIV" } },
                [&tm, sp] (juce::Graphics& g, juce::Rectangle<float> r, double t)
                {
                    const int type = (int) getParam (*sp, pid (uFilter, "type")), poles = (int) getParam (*sp, pid (uFilter, "poles")) == 1 ? 2 : 1;
                    const int route = (int) getParam (*sp, pid (uFilter, "routing"));
                    const float reso = getParam (*sp, pid (uFilter, "reso")), spread = getParam (*sp, pid (uFilter, "spread"));
                    const double fc = tm.filterCut.load(), q = 0.5 + reso * reso * 24.0;
                    auto a = r.reduced (14, 18).withTrimmedBottom (40);
                    for (double f : { 100.0, 1000.0, 10000.0 })
                    {
                        const float x = a.getX() + a.getWidth() * (float) (std::log (f / 20.0) / std::log (1000.0));
                        g.setColour (col::white.withAlpha (0.06f)); g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
                        drawCaption (g, { x + 3, a.getBottom() - 12, 50, 12 }, f >= 1000 ? juce::String (f / 1000, 0) + "k" : juce::String (f, 0), col::dim, 9);
                    }
                    juce::Path p, fill;
                    for (int i = 0; i <= 240; ++i)
                    {
                        const double f = 20.0 * std::pow (1000.0, i / 240.0);
                        auto stage = [&] (double fcx) { double m = 1; for (int k = 0; k < poles; ++k) m *= svfMag (type, f, fcx, poles == 2 && k == 0 ? 0.54 + 0.3 * reso : q); return m; };
                        double m = stage (fc);
                        const double fc2 = juce::jlimit (20.0, 20000.0, fc * std::pow (2.0, (double) spread));
                        if (route == 1) m *= stage (fc2); else if (route == 2) m = 0.5 * (m + stage (fc2));
                        const float db = clampf ((float) (20.0 * std::log10 (std::max (1e-6, m))), -48.0f, 24.0f);
                        const juce::Point<float> pt (a.getX() + a.getWidth() * (float) i / 240.0f, a.getY() + a.getHeight() * (1.0f - (db + 48.0f) / 72.0f));
                        if (i == 0) { p.startNewSubPath (pt); fill.startNewSubPath (a.getX(), a.getBottom()); } else p.lineTo (pt);
                        fill.lineTo (pt);
                    }
                    fill.lineTo (a.getRight(), a.getBottom()); fill.closeSubPath();
                    const auto c = accentOf (uFilter);
                    g.setColour (c.withAlpha (0.18f)); g.fillPath (fill);
                    glowStroke (g, p, c, 2.4f);
                    // LFO strip
                    auto l = r.removeFromBottom (36).reduced (14, 6);
                    const int shape = (int) getParam (*sp, pid (uFilter, "lfoshape"));
                    juce::Path lp;
                    for (int i = 0; i <= 120; ++i)
                    {
                        const float v = lfoValue (shape, (double) i / 40.0 + t * 0.5, 0x51f3u);
                        const juce::Point<float> pt (l.getX() + l.getWidth() * (float) i / 120.0f, l.getCentreY() - v * l.getHeight() * 0.45f);
                        if (i == 0) lp.startNewSubPath (pt); else lp.lineTo (pt);
                    }
                    glowStroke (g, lp, col::pink.withAlpha (0.8f), 1.4f, 0.5f);
                    drawCaption (g, r.reduced (10, 6).removeFromTop (14), "CUTOFF " + juce::String ((int) fc) + " Hz", c, 10);
                    driftGhost (g, r, t, 17, 0.06f);
                });
        case uChorus:
#if ! GPFX_BOX
            return withChorusMore (s,   // FX RACK (round 3): 4 TAPS / DELAY 2 / wet LP + HP in a MORE pop-down
#else
            return
#endif
                std::make_unique<UnitPage> (s, u, nullptr,
                std::vector<KnobSpec> { { "rate", "RATE" }, { "depth", "DEPTH" }, { "delay", "DELAY" }, { "fb", "FEEDBACK" }, { "width", "WIDTH" } },
                std::vector<KnobSpec> {}, std::vector<KnobSpec> {},
                [sp] (juce::Graphics& g, juce::Rectangle<float> r, double t)
                {
                    const float rate = getParam (*sp, pid (uChorus, "rate")), depth = getParam (*sp, pid (uChorus, "depth"));
                    const juce::Colour cs[3] { col::ice, col::ecto, col::pink };
                    for (int v = 0; v < 3; ++v)
                    {
                        juce::Path p;
                        for (int i = 0; i <= 160; ++i)
                        {
                            const float x = (float) i / 160.0f;
                            const float y = std::sin (kTwoPi * (x * 2.0f + (float) t * rate + (float) v / 3.0f)) * depth;
                            const juce::Point<float> pt (r.getX() + x * r.getWidth(), r.getCentreY() + y * r.getHeight() * 0.35f);
                            if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
                        }
                        glowStroke (g, p, cs[v].withAlpha (0.8f), 1.8f);
                        const float gx = r.getX() + r.getWidth() * (0.25f + 0.25f * (float) v);
                        const float gy = r.getCentreY() + std::sin (kTwoPi * (0.5f + 0.5f * (float) v + (float) t * rate + (float) v / 3.0f)) * depth * r.getHeight() * 0.35f;
                        auto L = ghosty::lookFor (19 + v); L.slime = cs[v];
                        ghosty::drawGhost (g, L, gx, gy - 14, 26, 0, false, 0.85f);
                    }
                })
#if ! GPFX_BOX
                )
#endif
                ;
        case uCrystal:
            return std::make_unique<UnitPage> (s, u, nullptr,
                std::vector<KnobSpec> { { "pitch", "PITCH" }, { "splice", "SPLICE" }, { "delay", "DELAY" }, { "fb", "FEEDBACK" }, { "reverse", "REVERSE" }, { "spread", "SPREAD" } },
                std::vector<KnobSpec> {}, std::vector<KnobSpec> {},
                [&tm, sp] (juce::Graphics& g, juce::Rectangle<float> r, double t)
                {
                    const float lvl = clampf (tm.crystalLevel.load() * 2.0f, 0, 1), rev = getParam (*sp, pid (uCrystal, "reverse")), pitch = getParam (*sp, pid (uCrystal, "pitch"));
                    const auto c = accentOf (uCrystal);
                    for (int k = 0; k < 28; ++k)
                    {
                        const float h = hashFloat ((uint32_t) k * 977u), dir = hashFloat ((uint32_t) k * 131u) < rev ? -1.0f : 1.0f;
                        float x = (float) std::fmod (h + dir * t * (0.04 + 0.03 * h) + 4.0, 1.0);
                        const float y = 0.15f + 0.7f * hashFloat ((uint32_t) k * 71u) - pitch / 48.0f * (x - 0.5f);
                        const float sz = 6.0f + 16.0f * hashFloat ((uint32_t) k * 13u) * (0.4f + lvl);
                        juce::Path d; d.addTriangle (0, -sz, sz * 0.5f, 0, -sz * 0.5f, 0); d.addTriangle (0, sz * 0.8f, sz * 0.5f, 0, -sz * 0.5f, 0);
                        d.applyTransform (juce::AffineTransform::rotation ((float) t * dir * 0.6f + h * 6).translated (r.getX() + x * r.getWidth(), r.getY() + y * r.getHeight()));
                        g.setColour ((dir < 0 ? col::pink : c).withAlpha (0.25f + 0.5f * lvl)); g.fillPath (d);
                        g.setColour (col::white.withAlpha (0.35f)); g.strokePath (d, juce::PathStrokeType (0.8f));
                    }
                    driftGhost (g, r, t, 21, 0.10f);
                    drawCaption (g, r.reduced (10, 6).removeFromTop (14), "SPLICES  " + juce::String (pitch, 1) + " st", c, 10);
                });
        case uEcho:
            return std::make_unique<UnitPage> (s, u, "mode",
                std::vector<KnobSpec> { { "time", "TIME" }, { "fb", "FEEDBACK" }, { "char", "CHARACTER" }, { "width", "WIDTH" },
                                        { "lowcut", "LOW CUT" }, { "highcut", "HIGH CUT" }, { "duck", "DUCKING" } },
                std::vector<KnobSpec> { { "sync", "SYNC" }, { "ping", "PING-PONG" } }, std::vector<KnobSpec> { { "div", "DIVISION" } },
                [&tm, sp, ev = std::make_shared<EchoViz>()] (juce::Graphics& g, juce::Rectangle<float> r, double t) { drawEchoGhosts (g, r, t, tm, *sp, *ev); });
        case uSpace:
            return std::make_unique<UnitPage> (s, u, nullptr,
                std::vector<KnobSpec> { { "size", "SIZE" }, { "blend", "BLEND" }, { "decay", "DECAY" }, { "tone", "TONE" },
                                        { "mod", "MOD" }, { "glimmer", "GLIMMER" }, { "predelay", "PRE-DELAY" }, { "width", "WIDTH" } },
                std::vector<KnobSpec> {}, std::vector<KnobSpec> {},
                [&tm, sp] (juce::Graphics& g, juce::Rectangle<float> r, double t)
                {
                    const float size = getParam (*sp, pid (uSpace, "size")), blend = getParam (*sp, pid (uSpace, "blend")), gl = getParam (*sp, pid (uSpace, "glimmer"));
                    const float lvl = clampf (tm.spaceLevel.load() * 3.0f, 0, 1);
                    const auto c = accentOf (uSpace);
                    const auto ctr = r.getCentre();
                    for (int k = 0; k < 9; ++k)
                    {
                        const float ph = (float) std::fmod (t * 0.25 + k / 9.0, 1.0);
                        const float rad = (0.1f + ph * (0.4f + 0.6f * size)) * r.getHeight() * 0.8f;
                        g.setColour ((k % 2 ? c : col::ice).withAlpha ((1.0f - ph) * (0.15f + 0.35f * lvl) * (k < 3 ? 1.0f - blend : blend + 0.2f)));
                        g.drawEllipse (ctr.x - rad * 1.4f, ctr.y - rad, rad * 2.8f, rad * 2.0f, 2.0f);
                    }
                    for (int k = 0; k < (int) (gl * 40); ++k)
                    {
                        const float x = hashFloat ((uint32_t) k * 91u + (uint32_t) (t * 3)), y = hashFloat ((uint32_t) k * 37u + (uint32_t) (t * 3));
                        glowDot (g, { r.getX() + x * r.getWidth(), r.getY() + y * r.getHeight() }, 1.2f, col::white.withAlpha (0.6f));
                    }
                    driftGhost (g, r, t, 25, 0.12f);
                });
        case uShimmer:
            return std::make_unique<UnitPage> (s, u, nullptr,
                std::vector<KnobSpec> { { "size", "SIZE" }, { "decay", "DECAY" }, { "damp", "DAMPING" }, { "shimmer", "SHIMMER" },
                                        { "predelay", "PRE-DELAY" }, { "mod", "MOD" }, { "width", "WIDTH" } },
                std::vector<KnobSpec> { { "freeze", "FREEZE" } }, std::vector<KnobSpec> {},
                [&tm, sp] (juce::Graphics& g, juce::Rectangle<float> r, double t)
                {
                    const float sh = getParam (*sp, pid (uShimmer, "shimmer")); const bool fz = getParam (*sp, pid (uShimmer, "freeze")) > 0.5f;
                    const float lvl = clampf (tm.shimmerLevel.load() * 3.0f, 0, 1);
                    const auto c = accentOf (uShimmer);
                    for (int k = 0; k < 60; ++k)
                    {
                        const float h = hashFloat ((uint32_t) k * 7919u);
                        const float y = (float) std::fmod (h + (fz ? 0.0 : t * (0.05 + 0.1 * sh) * (0.5 + h)), 1.0);
                        const float x = hashFloat ((uint32_t) k * 104729u) + 0.03f * std::sin ((float) t + (float) k);
                        glowDot (g, { r.getX() + x * r.getWidth(), r.getBottom() - y * r.getHeight() }, 1.0f + 2.0f * h * (0.3f + lvl), (k % 3 ? c : col::white).withAlpha (0.25f + 0.6f * lvl * (1.0f - y)));
                    }
                    driftGhost (g, r, t, 27, fz ? 0.35f : 0.12f);
                    if (fz) drawCaption (g, r.reduced (10, 6).removeFromTop (14), "FROZEN", col::ice, 11);
                });
        case uOtt:
            return std::make_unique<UnitPage> (s, u, nullptr,
                std::vector<KnobSpec> { { "time", "TIME" }, { "up", "UPWARD" }, { "down", "DOWNWARD" }, { "in", "INPUT" }, { "out", "OUTPUT" } },
                std::vector<KnobSpec> {}, std::vector<KnobSpec> {},
                [&tm, sp] (juce::Graphics& g, juce::Rectangle<float> r, double t)
                {
                    const char* names[3] { "LOW", "MID", "HIGH" };
                    const bool isOn = getParam (*sp, pid (uOtt, "on")) > 0.5f;
                    auto a = r.reduced (30, 30);
                    const float bw = a.getWidth() / 3.0f;
                    for (int b = 0; b < 3; ++b)
                    {
                        auto col3 = juce::Rectangle<float> (a.getX() + bw * (float) b, a.getY(), bw, a.getHeight()).reduced (bw * 0.22f, 0);
                        const float lv = isOn ? clampf ((tm.ottLevel[(size_t) b].load() + 60.0f) / 60.0f, 0, 1) : 0.0f, gn = isOn ? tm.ottGain[(size_t) b].load() : 0.0f;
                        g.setColour (T (juce::Colour (0xff12141c))); g.fillRoundedRectangle (col3, 6);
                        g.setColour (col::ice.withAlpha (0.5f)); g.fillRoundedRectangle (col3.withTrimmedTop (col3.getHeight() * (1.0f - lv)), 6);
                        const float mid = col3.getCentreY(), gh = clampf (gn / 30.0f, -1, 1) * col3.getHeight() * 0.5f;
                        g.setColour ((gn > 0 ? col::slime : col::pink).withAlpha (0.85f));
                        g.fillRect (col3.getX() + col3.getWidth() * 0.3f, gn > 0 ? mid - gh : mid, col3.getWidth() * 0.4f, std::abs (gh));
                        g.setColour (col::white.withAlpha (0.2f)); g.drawHorizontalLine ((int) mid, col3.getX(), col3.getRight());
                        drawCaption (g, col3.withY (col3.getBottom() + 4).withHeight (12), juce::String (names[b]) + "  " + juce::String (gn, 1) + " dB", col::dim, 9.5f, juce::Justification::centred);
                    }
                    driftGhost (g, r, t, 29, 0.06f);
                });
        case uLim:
        {
            auto hist = std::make_shared<History>();
            return std::make_unique<UnitPage> (s, u, nullptr,
                std::vector<KnobSpec> { { "ceiling", "CEILING" }, { "gain", "GAIN" }, { "release", "RELEASE" } },
                std::vector<KnobSpec> {}, std::vector<KnobSpec> {},
                [&tm, hist] (juce::Graphics& g, juce::Rectangle<float> r, double t)
                {
                    hist->push (tm.limGr.load());
                    auto m = r.removeFromRight (70).reduced (14, 24);
                    auto a = r.reduced (16, 24);
                    juce::Path p;
                    for (int k = 0; k < 200; ++k)
                    {
                        const float x = a.getX() + a.getWidth() * (float) k / 199.0f, y = a.getY() + clampf (-hist->at (k) / 12.0f, 0, 1) * a.getHeight();
                        if (k == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
                    }
                    glowStroke (g, p, col::pink, 2.0f);
                    for (int db = 3; db <= 9; db += 3) { const float y = a.getY() + a.getHeight() * (float) db / 12.0f; g.setColour (col::white.withAlpha (0.05f)); g.drawHorizontalLine ((int) y, a.getX(), a.getRight()); drawCaption (g, { a.getX(), y - 12, 60, 12 }, "-" + juce::String (db) + " dB", col::dim, 9); }
                    drawMeter (g, m.removeFromLeft (16), tm.outPeakL.load(), col::white, "L"); m.removeFromLeft (8);
                    drawMeter (g, m.removeFromLeft (16), tm.outPeakR.load(), col::white, "R");
                    drawCaption (g, r.reduced (10, 6).removeFromTop (14), "GAIN REDUCTION  " + juce::String (tm.limGr.load(), 1) + " dB", col::white.withAlpha (0.7f), 10);
                    juce::ignoreUnused (t);
                });
        }
#if GPFX_BOX
        default: return makeBoxPage (u, s, rk);
#else
        case uFatten: return std::make_unique<FattenPage> (s, rk);
        case uDist: return std::make_unique<DistPage> (s, rk);   // FX RACK (round 3)
        case uComp: return std::make_unique<CompPage> (s, rk);
        case uBands: return std::make_unique<BandsPage> (s, rk);
        case uEq3: return makeEq3Page (s, rk);
        case uHyper:
            return std::make_unique<UnitPage> (s, uHyper, "mode", std::vector<KnobSpec> { { "voices", "VOICES" }, { "detune", "DETUNE" }, { "rate", "RATE" }, { "width", "WIDTH" }, { "size", "SIZE" }, { "mix", "MIX" } },
                std::vector<KnobSpec> { { "retrig", "RETRIG" } }, std::vector<KnobSpec> {},
                [&tm = rk.tm, &s] (juce::Graphics& g, juce::Rectangle<float> r, double t)
                {   // the voices as wobbling ghosts, spread across the width; DIMENSION: four lines breathing
                    const bool dim = s.getRawParameterValue (pid (uHyper, "mode"))->load() > 0.5f;
                    const int nv = dim ? 4 : juce::jlimit (1, 7, (int) std::lround (s.getRawParameterValue (pid (uHyper, "voices"))->load()));
                    const float w = s.getRawParameterValue (pid (uHyper, "width"))->load(), lvl = juce::jlimit (0.0f, 1.0f, tm.hyperLevel.load() * 2.0f);
                    for (int k = 0; k < nv; ++k)
                    {
                        const float pos = nv == 1 ? 0.0f : ((float) k / (float) (nv - 1) * 2.0f - 1.0f) * (dim ? 1.0f : w);
                        const float x = r.getCentreX() + pos * r.getWidth() * 0.4f, y = r.getCentreY() + (float) std::sin (t * (0.8 + 0.07 * k) * 6.283 + k) * r.getHeight() * 0.25f;
                        const float sz = 10.0f + 10.0f * lvl;
                        ghosty::drawGhost (g, ghosty::lookFor (20 + k), x, y, sz, (float) std::sin (t * 0.7 + k) * 0.2f, false, 0.5f + 0.5f * lvl);
                    }
                    drawCaption (g, r.reduced (8, 6).removeFromBottom (14), dim ? "DIMENSION  four moving lines, slowly breathing" : juce::String (nv) + (nv == 1 ? " voice" : " voices") + "  sharp / flat pairs across the width",
                                 col::dim, 10, juce::Justification::centredLeft);
                });
        default: return std::make_unique<Page>();
#endif
    }
}

// A dropdown-looking button: small caption + the current name; the menu is built fresh on every click.
struct MenuButton : public juce::Component, public juce::SettableTooltipClient
{
    juce::String caption, text, placeholder; fxtheme::Held accent { col::ecto };
    std::function<void (juce::PopupMenu&)> build; std::function<void (int)> pick;
    juce::String shownText() const { return text.isNotEmpty() ? text : placeholder; }
    void setText (const juce::String& t) { if (t != text) { text = t; repaint(); } }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        if (ui::ghost())
        {   // GHOST: a glass pill, violet chevron, indigo text
            ui::ghostglass::combo (g, getLocalBounds().toFloat(), isMouseOver(), isEnabled());
            auto a = ghosty::glass::pillBody (getLocalBounds().toFloat()).reduced (10, 0); a.removeFromRight (16);
            if (caption.isNotEmpty()) { const float cw = ghosty::textWidth (font (9.5f), caption) + 8; drawCaption (g, a.removeFromLeft (cw), caption, accent, 9.5f); }
            g.setColour (text.isNotEmpty() ? ghosty::glass::ink() : ghosty::glass::ink2()); g.setFont (font (11.5f));
            g.drawFittedText (shownText(), a.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
            return;
        }
        g.setColour (T (juce::Colour (0xff07080c))); g.fillRoundedRectangle (r, 6);
        g.setColour (accent.withAlpha (isMouseOver() ? 0.85f : 0.45f)); g.drawRoundedRectangle (r, 6, 1.0f);
        auto a = r.reduced (8, 0);
        juce::Path ar; const float ax = a.getRight() - 4, ay = r.getCentreY();
        ar.addTriangle (ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
        g.setColour (accent); g.fillPath (ar);
        a.removeFromRight (14);
        if (caption.isNotEmpty())
        {
            const float cw = ghosty::textWidth (font (9.5f), caption) + 8;
            drawCaption (g, a.removeFromLeft (cw), caption, accent.withAlpha (0.8f), 9.5f);
        }
        g.setColour (text.isNotEmpty() ? col::white : col::white.withAlpha (0.55f)); g.setFont (font (11.5f));
        g.drawFittedText (shownText(), a.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent&) override
    {
        juce::PopupMenu m; if (build) build (m);
        juce::Component::SafePointer<MenuButton> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth()).withMaximumNumColumns (2),
                         [safe] (int r) { if (safe != nullptr && r != 0 && safe->pick) safe->pick (r); });
    }
};

// A small in-panel dialog (text prompt or yes/no), shown as a child of the panel so it stays inside the plugin window.
void askInPanel (juce::Component& parent, const juce::String& title, const juce::String& msg, bool withText, const juce::String& def,
                 const juce::String& okText, std::function<void (juce::String)> done)
{
    auto* w = new juce::AlertWindow (title, msg, juce::MessageBoxIconType::NoIcon);
    if (withText) w->addTextEditor ("name", def);
    w->addButton (okText, 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    parent.addAndMakeVisible (w);
    w->setCentrePosition (parent.getLocalBounds().getCentre());
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, withText, done] (int r)
    {
        if (r != 1) return;
        const auto t = withText ? w->getTextEditorContents ("name").trim() : juce::String ("ok");
        if (t.isNotEmpty()) done (t);
    }), true);
    if (withText) if (auto* te = w->getTextEditor ("name")) te->grabKeyboardFocus();
}

#if GPFX_BOX
#include "FxBoxTimelinePage.inc"
#include "FxBox3Pages.inc"
#endif

} // namespace

// ================================================================ Panel
struct Panel::Impl : private juce::Timer
{
    Impl (Panel& p, juce::AudioProcessorValueTreeState& s, Rack& r) : panel (p), apvts (s), rack (*r.impl), strip (s, *r.impl)
    {
        panel.setLookAndFeel (&lnf);
        panel.addAndMakeVisible (strip);
        strip.onSelect = [this] (int u) { select (u); };
        // per-unit PRESET menu (header) and the whole-rack SCENES bar (top)
        preset.caption = "PRESET"; preset.placeholder = "choose...";
        preset.setTooltip ("Presets for this effect only - the other effects are not touched");
        preset.build = [this] (juce::PopupMenu& m) { buildPresetMenu (m); };
        preset.pick = [this] (int id) { pickPreset (id); };
        panel.addAndMakeVisible (preset);
        sceneBox.placeholder = "choose a scene..."; sceneBox.accent = col::slime;
        sceneBox.setTooltip ("Scenes set up the WHOLE rack (every effect). UNDO brings back what you had.");
        sceneBox.build = [this] (juce::PopupMenu& m) { buildSceneMenu (m); };
        sceneBox.pick = [this] (int id) { pickScene (id); };
        panel.addAndMakeVisible (sceneBox);
        saveScene.setButtonText ("SAVE SCENE"); saveScene.setColour (juce::TextButton::buttonOnColourId, col::slime);
        saveScene.setTooltip ("Save the whole rack (order, patterns, every effect) as your own scene");
        saveScene.onClick = [this]
        {
            askInPanel (panel, "Save scene", "Name for this whole-rack scene:", true, rack.sceneName.upToFirstOccurrenceOf (" (", false, false), "Save",
                        [this] (juce::String n) { if (rack.saveUserScene (n)) toast ("Scene saved: " + n); else toast ("Could not save the scene"); refreshLabels(); });
        };
        undoBtn.setButtonText ("UNDO"); undoBtn.setColour (juce::TextButton::buttonOnColourId, col::slime);
        undoBtn.setTooltip ("Put the rack back exactly as it was before the last scene or preset");
        undoBtn.onClick = [this] { doUndo(); };
        panel.addAndMakeVisible (saveScene); panel.addAndMakeVisible (undoBtn);
#if GPFX_BOX
        redoBtn.setButtonText ("REDO"); redoBtn.setColour (juce::TextButton::buttonOnColourId, col::slime);
        redoBtn.setTooltip ("Redo what UNDO took back"); redoBtn.onClick = [this] { doRedo(); };
        browseBtn.setButtonText ("BROWSE"); browseBtn.setColour (juce::TextButton::buttonOnColourId, col::slime);
        browseBtn.setTooltip ("Browse every scene and this effect's presets: type to search, star your favourites");
        browseBtn.onClick = [this] { showBrowser (browser == nullptr); };
        panel.addAndMakeVisible (redoBtn); panel.addAndMakeVisible (browseBtn);
#endif
#if GPFX_BOX
        strip.onReorder = [this] (int u) { showTip (u); };   // chain order tips are a Ghost Box feature (Ghosty Pro's FX tab is unchanged)
        strip.onDropOutside = [this] (int u, juce::Point<int> at)   // GHOST MOTION timeline: a tile dropped on the MOTION page's timeline
        { if (mode == 2) if (auto* mp = dynamic_cast<MotionPage*> (global.get())) return mp->dropUnit (u, at); return false; };
        strip.deferSelect = [this] { return mode == 2 && rack.box.tlView; };
        static const char* tabNames[] { "RACK", "MORPH", "MOTION", "BRAIN", "PERFORM", "BOARD" };
        for (int i = 0; i < 6; ++i)
        {
            auto& b = tabs[(size_t) i]; b.setButtonText (tabNames[i]); b.setClickingTogglesState (false);
            b.setColour (juce::TextButton::buttonOnColourId, i == 0 ? col::slime : i == 1 ? T (juce::Colour (0xffFF4FD8)) : i == 2 ? T (juce::Colour (0xffC6FF4F)) : i == 3 ? T (juce::Colour (0xffFFC35A)) : T (juce::Colour (0xffA06BFF)));
            b.onClick = [this, i] { setMode (i); };
            panel.addAndMakeVisible (b);
        }
        tabs[1].setTooltip ("GHOST MORPH: capture two rack states and glide between them with one knob");
        tabs[2].setTooltip ("GHOST MOTION: 5 drawable tempo-synced lanes (volume, filter, pitch, space, damage)");
        tabs[3].setTooltip ("CHAIN BRAIN: pick a source and a goal, audition 3 generated chains at matched loudness");
        tabs[4].setTooltip ("PERFORM: big macro knobs, MORPH and the motion lanes on one screen");
        tabs[5].setTooltip ("SPIRIT BOARD: place up to 12 ghosts, each holding a scene, and drag the glowing puck between them: you hear the blend of all of them");
        for (int k = 0; k < (int) rack.extraPages.size(); ++k)
        {   // the host's own pages (Ghost Box: CAPTURE, DNA)
            auto* b = extraTabs.add (new juce::TextButton (rack.extraPages[(size_t) k].name)); b->setClickingTogglesState (false);
            b->setColour (juce::TextButton::buttonOnColourId, T (juce::Colour (rack.extraPages[(size_t) k].accent))); b->setTooltip (rack.extraPages[(size_t) k].tip);
            b->onClick = [this, k] { setMode (6 + k); };
            panel.addAndMakeVisible (b);
            if (rack.extraPages[(size_t) k].shortcut)
            {   // (the strip's free tile cell: GHOST SPLIT / GHOST FOCUS stay one click away when the tab row has no room left)
                auto* sb = shortcuts.add (new juce::TextButton (rack.extraPages[(size_t) k].name)); sb->setClickingTogglesState (false);
                sb->setColour (juce::TextButton::buttonOnColourId, T (juce::Colour (rack.extraPages[(size_t) k].accent))); sb->setTooltip (rack.extraPages[(size_t) k].tip);
                sb->onClick = [this, k] { setMode (6 + k); };
                sb->getProperties().set ("extraPage", k); sb->getProperties().set ("fitText", true);
                panel.addAndMakeVisible (sb);
            }
        }
        macros = std::make_unique<MacroStrip> (s, rack);
        panel.addAndMakeVisible (*macros);
        rightClick.impl = this;
        panel.addMouseListener (&rightClick, true);
#endif
#if ! GPFX_BOX
        headerDrag.impl = this; panel.addMouseListener (&headerDrag, false);   // FX RACK (round 3): drag the page title onto the strip
#endif
        overlay.setInterceptsMouseClicks (false, false);
        overlay.fn = [this] (juce::Graphics& g) { paintOver (g); };
        panel.addAndMakeVisible (overlay);
#if GPFX_BOX
        select (uRetro);
#else
        select (uGate);
#endif
        startTimerHz (30);
    }
    ~Impl() override
    {
        stopTimer();
#if GPFX_BOX
        panel.removeMouseListener (&rightClick); global.reset(); macros.reset();
#else
        panel.removeMouseListener (&headerDrag);
#endif
        page.reset(); panel.setLookAndFeel (nullptr);
    }

    // ---------------------------------------------------------------- chain order tips (non-blocking bubble under the strip)
    void showTip (int moved)
    {
        std::array<bool, kNumUnits> on {}; for (int u = 0; u < kNumUnits; ++u) on[(size_t) u] = rack.onP[(size_t) u].b();
        const auto tip = orderTip (rack.getOrder(), on, moved);
        if (tip.isEmpty()) return;
        tipText = tip; tipUntil = t + 6.0;
        const auto o = rack.getOrder(); for (int k = 0; k < kNumUnits; ++k) if (o[(size_t) k] == moved) tipX = strip.getX() + (int) strip.tile (k).getCentreX();
        panel.getProperties().set ("tipText", tipText);   // read by the test harness
        overlay.repaint();
    }
#if GPFX_BOX
    // ---------------------------------------------------------------- Ghost Box 3 modes
    std::array<juce::TextButton, 6> tabs; int mode = 0; std::unique_ptr<Page> global; std::unique_ptr<MacroStrip> macros;
    juce::OwnedArray<juce::TextButton> extraTabs; std::unique_ptr<juce::Component> extra;   // the host's own pages (modes 6..)
    juce::OwnedArray<juce::TextButton> shortcuts;                                          // their buttons in the strip's free cell
    struct RightClick : juce::MouseListener
    {
        Impl* impl = nullptr;
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (! e.mods.isPopupMenu()) return;
            for (auto* c = e.eventComponent; c != nullptr && c != &impl->panel; c = c->getParentComponent())
            {
                if (c->getProperties().contains ("gpfxMacro")) { macroMenu (*c, impl->panel, impl->rack, (int) c->getProperties()["gpfxMacro"], [this] { impl->macros->refresh(); }); return; }
                if (c->getProperties().contains ("gpfxParam"))
                {
                    const auto id = c->getProperties()["gpfxParam"].toString();
                    auto* sl = sliderAt (e.eventComponent, &impl->panel);
                    if (id.startsWith ("fx_")) macroMenuForParam (*c, impl->rack, id, [this] { impl->macros->refresh(); }, sl);
                    else if (sl != nullptr) plainSliderMenu (*c, sl);
                    return;
                }
            }
            if (auto* sl = sliderAt (e.eventComponent, &impl->panel)) plainSliderMenu (*sl, sl);   // any other slider: Type value / Reset
        }
        static void plainSliderMenu (juce::Component& host, juce::Slider* sl)
        {
            juce::PopupMenu m; addSliderItems (m, true); addSliderItems (m, false);
            juce::Component::SafePointer<juce::Slider> sp (sl);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&host).withMousePosition(), [sp] (int r) { handleSliderItem (r, sp.getComponent()); });
        }
    } rightClick;
    void setMode (int m)
    {
        mode = juce::jlimit (0, 5 + (int) rack.extraPages.size(), m);
        if (global) panel.removeChildComponent (global.get());
        global.reset();
        if (extra) panel.removeChildComponent (extra.get());
        extra.reset();
        if (mode >= 6 && rack.extraPages[(size_t) (mode - 6)].make) extra = rack.extraPages[(size_t) (mode - 6)].make();
        if (mode == 1) { auto mp = std::make_unique<MorphPage> (apvts, rack); mp->onChanged = [this] { strip.repaint(); }; global = std::move (mp); }
        else if (mode == 2) global = std::make_unique<MotionPage> (apvts, rack);
        else if (mode == 3) global = std::make_unique<BrainPage> (apvts, rack);
        else if (mode == 4) global = std::make_unique<PerformPage> (apvts, rack);
        else if (mode == 5)
        {   // (a page that cannot be made must never leave the panel half switched)
            boardlog::crumb ("tab clicked: " + boardlog::context (rack, panel.getBounds()));
            try { global = std::make_unique<BoardPage> (apvts, rack); }
            catch (const std::exception& e) { boardlog::write ("board: ctor threw: " + juce::String (e.what()) + "  " + boardlog::context (rack, panel.getBounds())); global.reset(); mode = 0; toast ("SPIRIT BOARD could not open"); }
            catch (...) { boardlog::write ("board: ctor threw (unknown)  " + boardlog::context (rack, panel.getBounds())); global.reset(); mode = 0; toast ("SPIRIT BOARD could not open"); }
        }
        for (int i = 0; i < 6; ++i) tabs[(size_t) i].setToggleState (i == mode, juce::dontSendNotification);
        for (int k = 0; k < extraTabs.size(); ++k) extraTabs[k]->setToggleState (6 + k == mode, juce::dontSendNotification);
        for (auto* sb : shortcuts) sb->setToggleState (6 + (int) sb->getProperties()["extraPage"] == mode, juce::dontSendNotification);
        if (extra) { panel.addAndMakeVisible (*extra); fxtheme::retint (*extra); }
        const bool rackMode = mode == 0;
        if (page) page->setVisible (rackMode);
        if (onT) onT->setVisible (rackMode);
        if (mix) mix->setVisible (rackMode);
        preset.setVisible (rackMode);
        if (global) panel.addAndMakeVisible (*global);
        overlay.toFront (false);
        if (global) fxtheme::retint (*global);
        layout(); panel.repaint();
        if (mode == 5) boardlog::crumb ("tab switched, page " + (global ? global->getBounds().toString() : juce::String ("none")));
    }
#endif

#if ! GPFX_BOX
    // FX RACK (round 3): the page title (ghost + name) is a drag handle: drop it on the strip to move the unit in the chain
    struct HeaderDrag : juce::MouseListener
    {
        Impl* impl = nullptr; bool armed = false, moving = false;
        juce::Rectangle<int> handle() const { auto h = impl->header; return h.withWidth (juce::jmin (h.getWidth() / 2, 420)); }
        void mouseDown (const juce::MouseEvent& e) override { armed = e.eventComponent == &impl->panel && handle().contains (e.getPosition()) && impl->selected != uLim && ! e.mods.isPopupMenu(); moving = false; }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (! armed) return;
            if (e.getDistanceFromDragStart() > 6) moving = true;
            if (! moving) return;
            impl->panel.setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            impl->strip.headerDrag (impl->selected, (e.position - impl->strip.getPosition().toFloat()));
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (armed && moving) { impl->strip.headerDrop (impl->selected, (e.position - impl->strip.getPosition().toFloat())); impl->panel.setMouseCursor (juce::MouseCursor::NormalCursor); }
            armed = moving = false;
        }
    } headerDrag;
#endif
    std::function<void (const juce::String&)> applyScene;

    // ---------------------------------------------------------------- presets / scenes
    juce::Array<juce::File> userFiles;   // the files listed in the menu that is open
    enum { idInit = 1, idFactory = 100, idUser = 1000, idSave = 2000, idDelete = 3000 };
    void buildPresetMenu (juce::PopupMenu& m)
    {
        const int u = selected;
        const auto cur = rack.unitPreset[(size_t) u];
        m.addSectionHeader (juce::String (unitInfo (u).title) + " PRESETS  (this effect only)");
        m.addItem (idInit, "Init (default)");
        m.addSeparator();
        const auto& list = unitPresets (u);
        for (int i = 0; i < (int) list.size(); ++i) m.addItem (idFactory + i, list[(size_t) i].name, true, list[(size_t) i].name == cur);
        userFiles = listXml (unitPresetDir (u));
        juce::PopupMenu del;
        if (! userFiles.isEmpty())
        {
            m.addSectionHeader ("Your presets");
            for (int i = 0; i < userFiles.size(); ++i)
            {
                const auto n = userFiles[i].getFileNameWithoutExtension();
                m.addItem (idUser + i, n, true, n == cur);
                del.addItem (idDelete + i, n);
            }
        }
        m.addSeparator();
        m.addItem (idSave, "Save preset...");
        m.addSubMenu ("Delete preset", del, ! userFiles.isEmpty());
    }
    void pickPreset (int id)
    {
        const int u = selected;
        if (id == idInit) { rack.applyUnitPreset (u, -1); toast (juce::String (unitInfo (u).title) + " reset, UNDO to go back"); }
        else if (id >= idFactory && id < idUser) rack.applyUnitPreset (u, id - idFactory);
        else if (id >= idUser && id < idSave && juce::isPositiveAndBelow (id - idUser, userFiles.size())) rack.loadUserPreset (u, userFiles[id - idUser]);
        else if (id == idSave)
            askInPanel (panel, "Save preset", juce::String ("Name for this ") + unitInfo (u).title + " preset:", true,
                        rack.unitPreset[(size_t) u], "Save",
                        [this, u] (juce::String n) { if (rack.saveUserPreset (u, n)) toast ("Preset saved: " + n); else toast ("Could not save the preset"); refreshLabels(); });
        else if (id >= idDelete && juce::isPositiveAndBelow (id - idDelete, userFiles.size()))
        {
            const auto f = userFiles[id - idDelete];
            askInPanel (panel, "Delete preset", "Delete \"" + f.getFileNameWithoutExtension() + "\"? This can't be undone.", false, {}, "Delete",
                        [this, f] (juce::String) { f.deleteFile(); toast ("Preset deleted"); });
        }
        if (id < idSave) select (selected);   // pages that draw non-parameter state (patterns, pads) rebuild
        refreshLabels();
    }
    void buildSceneMenu (juce::PopupMenu& m)
    {
        const auto cur = rack.sceneName;
        m.addSectionHeader ("SCENES  (change every effect)");
        const auto& fs = factoryScenes();
        for (int i = 0; i < (int) fs.size(); ++i) m.addItem (idFactory + i, fs[(size_t) i].display, true, cur == fs[(size_t) i].display);
        userFiles = listXml (sceneDir());
        juce::PopupMenu del;
        if (! userFiles.isEmpty())
        {
            m.addSectionHeader ("Your scenes");
            for (int i = 0; i < userFiles.size(); ++i)
            {
                const auto n = userFiles[i].getFileNameWithoutExtension();
                m.addItem (idUser + i, n, true, n == cur);
                del.addItem (idDelete + i, n);
            }
            m.addSeparator();
            m.addSubMenu ("Delete a saved scene", del);
        }
    }
    void pickScene (int id)
    {
        const auto& fs = factoryScenes();
        if (id >= idFactory && id < idFactory + (int) fs.size()) { applyScene (fs[(size_t) (id - idFactory)].key); toast ("Scene loaded, UNDO to go back"); }
        else if (id >= idUser && id < idSave && juce::isPositiveAndBelow (id - idUser, userFiles.size()))
        { if (rack.loadUserScene (userFiles[id - idUser])) toast ("Scene loaded, UNDO to go back"); }
        else if (id >= idDelete && juce::isPositiveAndBelow (id - idDelete, userFiles.size()))
        {
            const auto f = userFiles[id - idDelete];
            askInPanel (panel, "Delete scene", "Delete \"" + f.getFileNameWithoutExtension() + "\"? This can't be undone.", false, {}, "Delete",
                        [this, f] (juce::String) { f.deleteFile(); toast ("Scene deleted"); });
        }
        if (id < idSave) select (selected);
        refreshLabels();
    }
    void doUndo()
    {
#if GPFX_BOX
        if (rack.brainAudition >= 0) brain::endAudition (rack, false);   // UNDO during an audition drops it (the undo step holds the rack from before)
#endif
        if (rack.undo()) { toast ("Undone"); select (selected); }
        else toast ("Nothing to undo");
        refreshLabels();
    }
#if GPFX_BOX
    void doRedo()
    {
        if (rack.redo()) { toast ("Redone"); select (selected); } else toast ("Nothing to redo");
        refreshLabels();
    }
    std::unique_ptr<Browser> browser; juce::TextButton redoBtn, browseBtn;
    void showBrowser (bool on)
    {
        if (! on) { if (browser) { panel.removeChildComponent (browser.get()); browser.reset(); } browseBtn.setToggleState (false, juce::dontSendNotification); return; }
        browser = std::make_unique<Browser> (apvts, rack); browser->getProperties().set ("browserOverlay", true);
        browser->selectedUnit = [this] { return selected; };
        browser->onClose = [this] { showBrowser (false); };
        browser->onLoaded = [this] { select (selected); refreshLabels(); toast ("Loaded, UNDO to go back"); if (browser) browser->rebuild(); };
        panel.addAndMakeVisible (*browser); browseBtn.setToggleState (true, juce::dontSendNotification);
        fxtheme::retint (*browser);
        layout(); browser->open(); overlay.toFront (false);
    }
#endif
    void refreshLabels()
    {
        rack.updateEdited();
        preset.setText (rack.unitLabel (selected));
        sceneBox.setText (rack.sceneLabel());
        undoBtn.setEnabled (rack.undoDepth() > 0);
#if GPFX_BOX
        redoBtn.setEnabled (rack.redoDepth() > 0);
#endif
        panel.getProperties().set ("presetText", preset.shownText());   // read by the test harness
        panel.getProperties().set ("sceneText", sceneBox.shownText());
    }
    void toast (const juce::String& s) { toastText = s; toastUntil = t + 2.5; overlay.repaint(); }

    void select (int u)
    {
#if GPFX_BOX
        if (mode != 0 && global) { panel.removeChildComponent (global.get()); global.reset(); mode = 0; for (int i = 0; i < 6; ++i) tabs[(size_t) i].setToggleState (i == 0, juce::dontSendNotification); preset.setVisible (true); }
        if (mode != 0 && extra) { panel.removeChildComponent (extra.get()); extra.reset(); mode = 0; for (int i = 0; i < 6; ++i) tabs[(size_t) i].setToggleState (i == 0, juce::dontSendNotification); for (auto* b : extraTabs) b->setToggleState (false, juce::dontSendNotification); preset.setVisible (true); }
#endif
        selected = juce::jlimit (0, kNumUnits - 1, u); strip.selected = selected;
        if (page) panel.removeChildComponent (page.get());
        page.reset(); onT.reset(); mix.reset();
        page = selected == uEq ? eq::createPage (apvts, *rack.eqUnit) : selected == uGate ? std::make_unique<GatePage> (apvts, rack)
             : selected == uGrid ? std::make_unique<GridPage> (apvts, rack) : selected == uHaunt ? std::make_unique<HauntPage> (apvts, rack)
             : makeUnitPage (selected, apvts, rack);
        panel.addAndMakeVisible (*page);
        onT = std::make_unique<Toggle> (apvts, pid (selected, "on"), "ON", accentOf (selected));
        panel.addAndMakeVisible (*onT);
        if (unitInfo (selected).hasMix)
        {
            mix = std::make_unique<Knob> (apvts, pid (selected, "mix"), selected == uOtt ? "DEPTH" : "MIX", accentOf (selected));
            mix->slider.setSliderStyle (juce::Slider::LinearHorizontal);
            mix->slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 15);
            mix->resized();
            panel.addAndMakeVisible (*mix);
        }
        overlay.toFront (false);
        refreshLabels();
        fxtheme::retint (panel);   // components made now took JUCE / build-time colours: bring them to the current theme
        layout(); panel.repaint();
    }
    void layout()
    {
        auto r = panel.getLocalBounds().reduced (8);
        // whole-rack SCENES bar above the tiles
        sceneBar = r.removeFromTop (28);
        {
            auto b = sceneBar.reduced (0, 2);
            b.removeFromLeft (66);                                    // "SCENES" label (painted)
            sceneBox.setBounds (b.removeFromLeft (juce::jlimit (170, 240, b.getWidth() / 4)));
#if GPFX_BOX
            redoBtn.setBounds (b.removeFromRight (64)); b.removeFromRight (4);
            undoBtn.setBounds (b.removeFromRight (64)); b.removeFromRight (6);
            saveScene.setBounds (b.removeFromRight (96)); b.removeFromRight (6);
            browseBtn.setBounds (b.removeFromRight (80)); b.removeFromRight (6);
#else
            undoBtn.setBounds (b.removeFromRight (70)); b.removeFromRight (6);
            saveScene.setBounds (b.removeFromRight (100)); b.removeFromRight (6);
#endif
#if GPFX_BOX
            b.removeFromLeft (10);
            int textSum = 0; for (auto& tb : tabs) textSum += (int) ghosty::textWidth (font (12.0f), tb.getButtonText());
            const int pad = juce::jlimit (10, 26, (b.getWidth() - textSum) / juce::jmax (1, (int) std::size (tabs)));   // tighter padding before any label is cut
            for (int ti : { 0, 1, 5, 2, 3, 4 })   // BOARD sits next to MORPH
            {   // sized from the label, so PERFORM never becomes "PERF..."
                auto& tb = tabs[(size_t) ti];
                const int tw = (int) ghosty::textWidth (font (12.0f), tb.getButtonText()) + pad;
                tb.setBounds (b.removeFromLeft (juce::jmin (tw, b.getWidth())).reduced (2, 0));
            }
            for (auto* xb : extraTabs)
            {   // the host's pages: in what is left of the row (never at the six tabs' expense)
                const int tw = (int) ghosty::textWidth (font (12.0f), xb->getButtonText()) + 16;
                xb->setVisible (b.getWidth() >= tw);
                if (b.getWidth() >= tw) xb->setBounds (b.removeFromLeft (tw).reduced (2, 0));
            }
            sceneHint = {};
#else
            sceneHint = b.reduced (10, 0);
#endif
        }
#if GPFX_BOX
        if (macros) { macros->setBounds (r.removeFromBottom (62)); r.removeFromBottom (6); }
#endif
        r.removeFromTop (4);
#if GPFX_BOX
        const int stripH = Strip::rowsFor (r.getWidth()) == 2 ? juce::jlimit (128, 170, panel.getHeight() / 4) : juce::jlimit (64, 92, panel.getHeight() / 8);
        strip.setBounds (r.removeFromTop (stripH));
        {   // the host pages' shortcuts in the strip's free tile cell (two rows of 12 tiles for 23 units leave one cell after the limiter)
            const int rows = Strip::rowsFor (strip.getWidth()), perRow = (kNumUnits + rows - 1) / rows;
            const bool cell = rows * perRow > kNumUnits && ! shortcuts.isEmpty();
            auto cr = strip.tile (kNumUnits).reduced (3.5f, 2.5f).toNearestInt() + strip.getPosition();
            // up to 2: stacked; more: two columns (the first two keep the left column, top to bottom), so each stays at least 16 px tall
            const int n = shortcuts.size(), cols = n > 2 ? 2 : 1, rowsN = (n + cols - 1) / juce::jmax (1, cols);
            const int h = cell ? juce::jmin (30, (cr.getHeight() - 4 * (rowsN - 1)) / juce::jmax (1, rowsN)) : 0, w = (cr.getWidth() - 2 * (cols - 1)) / cols;
            for (int i = 0; i < n; ++i)
            {
                auto* sb = shortcuts[i];
                sb->setVisible (cell && h >= 16);
                if (cell) { const int c = cols == 2 ? i / rowsN : 0, rr = cols == 2 ? i % rowsN : i; sb->setBounds (cr.getX() + c * (w + 2), cr.getY() + rr * (h + 4), w, h); sb->toFront (false); }
            }
        }
#else
        strip.setBounds (r.removeFromTop (juce::jlimit (64, 92, panel.getHeight() / 8)));
#endif
        r.removeFromTop (6);
        card = r;
        auto c = r.reduced (10, 6);
        header = c.removeFromTop (44);
        auto h = header;
        preset.setBounds (h.removeFromRight (juce::jlimit (210, 250, h.getWidth() / 4)).reduced (0, 9));
        h.removeFromRight (10);
        if (mix) mix->setBounds (h.removeFromRight (juce::jlimit (140, 200, h.getWidth() / 4)).reduced (0, 11));
        h.removeFromRight (8);
        if (onT) onT->setBounds (h.removeFromRight (56).reduced (0, 10));
        c.removeFromTop (4);
        if (page) page->setBounds (c);
#if GPFX_BOX
        if (global) global->setBounds (card.reduced (12, 10).withTrimmedTop (22));
        if (extra) extra->setBounds (card.reduced (12, 10).withTrimmedTop (22));
        if (browser) { browser->setBounds (card.reduced (16, 12)); browser->toFront (false); }
#endif
        overlay.setBounds (panel.getLocalBounds());
    }
    void paint (juce::Graphics& g)
    {
        if (ui::ghost()) { paintGhost (g); return; }
        g.fillAll (col::bg);
        {
            auto sb = sceneBar.toFloat();
            g.setColour (T (juce::Colour (0xff11131a))); g.fillRoundedRectangle (sb, 8);
            g.setColour (col::slime.withAlpha (0.18f)); g.drawRoundedRectangle (sb.reduced (0.5f), 8, 1.0f);
            drawCaption (g, sb.withWidth (66).withTrimmedLeft (10), "SCENES", col::slime, 12.0f);
            drawCaption (g, sceneHint.toFloat(), "changes every effect  -  use PRESET below for one effect", col::dim, 10.0f);
        }
        const auto acc = accentOf (selected);
        auto cr = card.toFloat();
        if (ui::glass()) progl::card (g, cr, 14, acc);     // GLASS (round 3): a raised frosted card (Ghosty Pro LIGHT only)
        else
        {
        juce::ColourGradient bg (T (juce::Colour (0xff151824)), cr.getX(), cr.getY(), T (juce::Colour (0xff0e1017)), cr.getX(), cr.getBottom(), false);
        g.setGradientFill (bg); g.fillRoundedRectangle (cr, 14);
        g.setColour (acc.withAlpha (0.25f)); g.drawRoundedRectangle (cr.reduced (0.5f), 14, 1.2f);
        }
#if GPFX_BOX
        if (mode != 0)
        {
            // (one title per tab, BOARD = mode 5 included: this list used to stop at PERFORM, so BOARD read past its end = a random
            //  pointer; harmless padding on some builds, a crash on the owner's Mac)
            static const char* const titles[] { "", "GHOST MORPH  -  two states, one knob", "GHOST MOTION  -  draw how the sound moves, in time with the song",
                                                "CHAIN BRAIN  -  tell it the source and the goal, hear three chains", "PERFORM  -  macros, morph and motion on one screen",
                                                "SPIRIT BOARD  -  place ghosts, move the puck between them" };
            static_assert (std::size (titles) == 6, "one title per tab (modes 0..5)");
            if (mode == 5) boardlog::crumb ("panel paint (BOARD title)");
            if (juce::isPositiveAndBelow (mode, (int) std::size (titles)))
                drawCaption (g, cr.reduced (16, 6).removeFromTop (20), titles[mode], col::white, 13.0f);
            else if (mode >= 6 && mode - 6 < (int) rack.extraPages.size())
                drawCaption (g, cr.reduced (16, 6).removeFromTop (20), rack.extraPages[(size_t) (mode - 6)].title, col::white, 13.0f);
            return;
        }
#endif
        auto h = header.toFloat();
        const bool on = getParam (apvts, pid (selected, "on")) > 0.5f;
        ghosty::drawGhost (g, ghosty::lookFor (unitInfo (selected).look), h.getX() + 26, h.getCentreY() + 2, 34, 0, false, on ? 1.0f : 0.5f);
        const juce::String title = unitInfo (selected).title;
        g.setFont (font (22.0f));
        const float tw = ghosty::textWidth (font (22.0f), title);
        auto tr = juce::Rectangle<float> (h.getX() + 54, h.getY() + 2, tw + 10, 26);
        g.setColour (acc.withAlpha (0.25f)); g.drawText (title, tr.translated (-1.5f, 0), juce::Justification::centredLeft);
        g.setColour (col::pink.withAlpha (0.18f)); g.drawText (title, tr.translated (1.5f, 0), juce::Justification::centredLeft);
        g.setColour (col::white); g.drawText (title, tr, juce::Justification::centredLeft);
        drawCaption (g, { h.getX() + 54, h.getY() + 27, 520, 14 }, unitInfo (selected).blurb, col::dim, 10.5f);
        drawCaption (g, { tr.getRight() + 6, h.getY() + 8, 90, 14 }, groupName (unitInfo (selected).group), acc.withAlpha (0.7f), 9.5f);
    }
    // GHOST (Ghost Box): no flat fill (the editor's aurora shows through), the scene bar and the big card as cached glass panels,
    // the same texts in deep indigo; the unit's ghost outlined so it reads on the glass
    void paintGhost (juce::Graphics& g)
    {
        {   // (the scene bar, card and macro bar glass are in the window's cached static layer: Panel::paintGhostSurfaces)
            auto sb = sceneBar.toFloat();
            drawCaption (g, sb.withWidth (66).withTrimmedLeft (10), "SCENES", col::slime, 12.0f);
            drawCaption (g, sceneHint.toFloat(), "changes every effect  -  use PRESET below for one effect", col::dim, 10.0f);
        }
        const auto acc = accentOf (selected);
        auto cr = card.toFloat();
#if GPFX_BOX
        if (mode != 0)
        {
            static const char* const titles[] { "", "GHOST MORPH  -  two states, one knob", "GHOST MOTION  -  draw how the sound moves, in time with the song",
                                                "CHAIN BRAIN  -  tell it the source and the goal, hear three chains", "PERFORM  -  macros, morph and motion on one screen",
                                                "SPIRIT BOARD  -  place ghosts, move the puck between them" };
            if (juce::isPositiveAndBelow (mode, (int) std::size (titles)))
                drawCaption (g, cr.reduced (16, 6).removeFromTop (20), titles[mode], col::white, 13.0f);
            else if (mode >= 6 && mode - 6 < (int) rack.extraPages.size())
                drawCaption (g, cr.reduced (16, 6).removeFromTop (20), rack.extraPages[(size_t) (mode - 6)].title, col::white, 13.0f);
            return;
        }
#endif
        auto h = header.toFloat();
        const bool on = getParam (apvts, pid (selected, "on")) > 0.5f;
        ghosty::drawGhost (g, ghosty::lookFor (unitInfo (selected).look), h.getX() + 26, h.getCentreY() + 2, 34, 0, false, on ? 1.0f : 0.5f);
        ui::ghostglass::outline (g, h.getX() + 26, h.getCentreY() + 2, 34, on ? 1.0f : 0.5f);
        const juce::String title = unitInfo (selected).title;
        g.setFont (font (22.0f));
        const float tw = ghosty::textWidth (font (22.0f), title);
        auto tr = juce::Rectangle<float> (h.getX() + 54, h.getY() + 2, tw + 10, 26);
        g.setColour (ghosty::glass::ink()); g.drawText (title, tr, juce::Justification::centredLeft);
        drawCaption (g, { h.getX() + 54, h.getY() + 27, 520, 14 }, unitInfo (selected).blurb, col::dim, 10.5f);
        drawCaption (g, { tr.getRight() + 6, h.getY() + 8, 90, 14 }, groupName (unitInfo (selected).group), acc, 9.5f);
    }
    ghosty::glass::PanelCache ghostPanels;   // GHOST: the scene bar + card glass images (per size)
    void paintOver (juce::Graphics& g)
    {
        if (t < tipUntil && tipText.isNotEmpty())
        {
            const float a = (float) juce::jlimit (0.0, 1.0, (tipUntil - t) / 0.5);
            const juce::String full = juce::String ("TIP  ") + tipText;
            const float need = ghosty::textWidth (font (11.5f), full) + 40, maxW = (float) panel.getWidth() - 20.0f;
            const int lines = need <= maxW ? 1 : 2;   // long tips wrap onto a second line instead of being cut off
            const float w = lines == 1 ? need : juce::jmin (maxW, need * 0.55f + 40);
            auto tr = juce::Rectangle<float> (w, lines == 1 ? 30.0f : 46.0f).withPosition (juce::jlimit (10.0f, (float) panel.getWidth() - w - 10.0f, (float) tipX - w * 0.5f), (float) strip.getBottom() + 4);
            g.setColour ((ui::ghost() ? juce::Colour (0xfff4f1fc) : T (juce::Colour (0xff151822))).withMultipliedAlpha (a)); g.fillRoundedRectangle (tr, 8);   // opaque: never mixes with the page under it (GHOST: a frosted card)
            g.setColour (col::gold.withAlpha (0.8f * a)); g.drawRoundedRectangle (tr, 8, 1.2f);
            juce::Path pt; pt.addTriangle ((float) tipX - 6, tr.getY(), (float) tipX + 6, tr.getY(), (float) tipX, tr.getY() - 6);
            g.setColour (col::gold.withAlpha (0.8f * a)); g.fillPath (pt);
            g.setColour (col::white.withAlpha (a)); g.setFont (font (11.5f));
            g.drawFittedText (full, tr.reduced (12, 4).toNearestInt(), juce::Justification::centredLeft, lines, 0.9f);
        }
        if (t < toastUntil && toastText.isNotEmpty())
        {
            const float a = (float) juce::jlimit (0.0, 1.0, (toastUntil - t) / 0.4);
            const float w = ghosty::textWidth (font (12.0f), toastText) + 32;
            auto tr = juce::Rectangle<float> (w, 28).withCentre ({ (float) card.getCentreX(), (float) card.getY() + 64 });
            g.setColour (T (juce::Colour (0xf0101218)).withMultipliedAlpha (a)); g.fillRoundedRectangle (tr, 8);
            g.setColour (col::slime.withAlpha (0.7f * a)); g.drawRoundedRectangle (tr, 8, 1.2f);
            drawCaption (g, tr, toastText, col::white.withAlpha (a), 12.0f, juce::Justification::centred);
        }
        // RGB-split ghosting over the page while a HAUNT gesture is held
        const float env = rack.tm.hauntEnv.load();
        if (env < 0.05f || ! snap.isValid() || ! page) return;
        const auto pr = page->getBounds().toFloat();
        const float dx = 3.0f + 5.0f * env;
        g.setOpacity (0.22f * env); g.drawImage (snap, pr.translated (-dx, 0));
        g.setOpacity (0.22f * env); g.drawImage (snap, pr.translated (dx, 1));
        g.setOpacity (1.0f);
        auto& rnd = juce::Random::getSystemRandom();
        for (int k = 0; k < 3; ++k)
        {
            const float y = pr.getY() + rnd.nextFloat() * pr.getHeight();
            g.setColour ((k % 2 ? col::pink : col::slime).withAlpha (0.10f * env));
            g.fillRect (pr.getX(), y, pr.getWidth(), 2.0f + rnd.nextFloat() * 10.0f * env);
        }
    }
    void timerCallback() override
    {
        t += 1.0 / 30.0;
        strip.t = t; strip.repaint();
        if (page && page->isShowing()) page->tick (t);
        const float env = rack.tm.hauntEnv.load();
        if (env > 0.05f && page && page->getWidth() > 0)
        {
            if (++snapCount % 2 == 0) snap = page->createComponentSnapshot (page->getLocalBounds(), false, 0.5f);
            overlay.repaint (page->getBounds());
        }
        else if (snap.isValid() && env <= 0.05f) { snap = {}; overlay.repaint(); }
        panel.repaint (header);
        if (++labelCount % 6 == 0) refreshLabels();
        if (toastText.isNotEmpty() && t < toastUntil + 0.1) overlay.repaint (card.withHeight (100));
        if (tipText.isNotEmpty() && t < tipUntil + 0.1) overlay.repaint (strip.getBounds().withHeight (strip.getHeight() + 50));
#if GPFX_BOX
        if (global && global->isShowing())
        {
            try { global->tick (t); }
            catch (const std::exception& e) { boardlog::write ("page tick threw: " + juce::String (e.what()) + "  mode " + juce::String (mode)); }
            catch (...) { boardlog::write ("page tick threw (unknown)  mode " + juce::String (mode)); }
        }
        if (macros && labelCount % 15 == 0) macros->repaint();
#endif
    }

    Panel& panel; juce::AudioProcessorValueTreeState& apvts; Rack::Impl& rack;
    struct Overlay : juce::Component { std::function<void (juce::Graphics&)> fn; void paint (juce::Graphics& g) override { if (fn) fn (g); } };
    fxtheme::Theme theme = fxtheme::Theme::Dark;   // this panel's theme (themeChanged)
    int look = 0;                                   // its raw look (= theme, or GHOST in Ghost Box)
    LnF lnf; Strip strip; MenuButton preset, sceneBox; juce::TextButton saveScene, undoBtn; Overlay overlay;
    juce::Rectangle<int> sceneBar, sceneHint; juce::String toastText; double toastUntil = -1; int labelCount = 0;
    juce::String tipText; double tipUntil = -1; int tipX = 0;
    std::unique_ptr<Page> page; std::unique_ptr<Toggle> onT; std::unique_ptr<Knob> mix;
    juce::Rectangle<int> card, header; int selected = 0; double t = 0; juce::Image snap; int snapCount = 0;
};

Panel::Panel (juce::AudioProcessorValueTreeState& apvts, Rack& rack)
{
    impl = std::make_unique<Impl> (*this, apvts, rack);
    impl->applyScene = [&rack, &apvts] (const juce::String& s) { rack.applyScene (apvts, s); };
    setSize (1100, 560);
    themeChanged();   // stored colours onto the current theme (every theme maps the original colours, BLACK included)
}
Panel::~Panel() = default;
void Panel::themeChanged()
{
    impl->theme = fxtheme::theme(); impl->look = fxtheme::look();
    impl->lnf.applyTheme();
    fxtheme::retint (*this);
    sendLookAndFeelChange();
    repaint();
}
void Panel::paint (juce::Graphics& g) { fxtheme::setLook (impl->look); impl->paint (g); }   // this panel's theme for the whole paint pass
#if GPFX_BOX
void Panel::paintGhostSurfaces (juce::Graphics& g)
{
    fxtheme::setLook (impl->look);
    impl->ghostPanels.draw (g, impl->sceneBar.toFloat(), 12.0f, 0.34f, 6.0f);
    impl->ghostPanels.draw (g, impl->card.toFloat(), 18.0f, 0.38f, 12.0f);
    if (impl->macros && impl->macros->isVisible()) ghosty::glass::body (g, impl->macros->getBounds().toFloat().reduced (1.0f), 12.0f, juce::Colours::white, 0.40f);
}
juce::String Panel::ghostStaticKey() const
{   // what the static glass layer depends on (the editor rebuilds its cached layer when this changes)
    return impl->sceneBar.toString() + "|" + impl->card.toString() + "|" + (impl->macros && impl->macros->isVisible() ? impl->macros->getBounds().toString() : juce::String());
}
std::vector<std::pair<juce::Rectangle<int>, juce::Colour>> Panel::ghostTexts()
{
    fxtheme::setLook (impl->look);
    using namespace ui;
    auto& I = *impl; std::vector<std::pair<juce::Rectangle<int>, juce::Colour>> v;
    auto sb = I.sceneBar.toFloat();
    v.push_back ({ sb.withWidth (66).withTrimmedLeft (10).toNearestInt().withWidth ((int) ghosty::textWidth (font (12.0f), "SCENES")).reduced (0, 6), col::slime });
    v.push_back ({ I.sceneHint.withWidth (juce::jmin (I.sceneHint.getWidth(), (int) ghosty::textWidth (font (10.0f), "changes every effect  -  use PRESET below for one effect"))).reduced (0, 6), col::dim });
    if (I.mode != 0) { v.push_back ({ I.card.reduced (16, 6).removeFromTop (20).withWidth (300).reduced (0, 4), col::white }); return v; }
    auto h = I.header.toFloat();
    const juce::String title = unitInfo (I.selected).title;
    const float tw = ghosty::textWidth (font (22.0f), title);
    v.push_back ({ juce::Rectangle<float> (h.getX() + 54, h.getY() + 2, tw, 26).toNearestInt().reduced (0, 6), ghosty::glass::ink() });
    v.push_back ({ juce::Rectangle<float> (h.getX() + 54, h.getY() + 27, juce::jmin (520.0f, ghosty::textWidth (font (10.5f), unitInfo (I.selected).blurb)), 14).toNearestInt().reduced (0, 3), col::dim });
    v.push_back ({ juce::Rectangle<float> (h.getX() + 54 + tw + 16, h.getY() + 8, ghosty::textWidth (font (9.5f), groupName (unitInfo (I.selected).group)), 14).toNearestInt().reduced (0, 3), accentOf (I.selected) });
    return v;
}
#endif
void Panel::resized()
{
    // test hook: getProperties()["select"] picks the shown unit
    if (getProperties().contains ("select"))
    {
        const int u = getProperties()["select"]; getProperties().remove ("select");
        if (u != impl->selected) { impl->select (u); return; }
    }
#if GPFX_BOX
    if (getProperties().contains ("mode")) { const int m = getProperties()["mode"]; getProperties().remove ("mode"); impl->setMode (m); }
    if (getProperties().contains ("brainAudition"))
    {
        const int k = getProperties()["brainAudition"]; getProperties().remove ("brainAudition");
        if (auto* bp = dynamic_cast<BrainPage*> (impl->global.get())) bp->audition (k);
    }
    if (getProperties().contains ("tipFor")) { const int u = getProperties()["tipFor"]; getProperties().remove ("tipFor"); impl->showTip (u); }
#endif
    // test hooks: pick a menu item / press UNDO without a mouse
    for (auto* key : { "pickPreset", "pickScene", "undo" })
        if (getProperties().contains (key))
        {
            const int v = getProperties()[key]; getProperties().remove (key);
            if (juce::String (key) == "pickPreset") impl->pickPreset (v); else if (juce::String (key) == "pickScene") impl->pickScene (v); else impl->doUndo();
        }
#if GPFX_BOX
    if (getProperties().contains ("redo")) { getProperties().remove ("redo"); impl->doRedo(); }
    if (getProperties().contains ("browse")) { const int v = getProperties()["browse"]; getProperties().remove ("browse"); impl->showBrowser (v != 0); }
#endif
    impl->layout();
}

} // namespace gpfx
