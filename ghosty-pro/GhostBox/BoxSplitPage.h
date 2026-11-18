#pragma once
// GHOST SPLIT page (a tab of the rack panel, "SPLITS", after DNA; also a shortcut button in the strip's free tile cell): ON, the split MODE
// (FREQUENCY / TRANSIENT-BODY / MID-SIDE / LEVEL / PARALLEL) and its settings, then one column per branch: its LEVEL and the units that run
// in it (+ UNIT adds one, a click on a unit moves it). The audio side is GhostyPro/fx/FxRoute.h + Rack::Impl::runRoute. Themed in all 4 looks.
#include "BoxProcessor.h"
#include "FxUi.h"
#include "FxRackImpl.h"
#include "GhostGlass.h"

class BoxSplitPage : public juce::Component, private juce::Timer
{
public:
    static constexpr juce::uint32 kAccent = 0xff5AF0C8;
    explicit BoxSplitPage (BoxProcessor& p) : proc (p), rack (*p.rack.impl),
        onT (p.apvts, "fx_route_on", "SPLIT ON", acc()), mode (p.apvts, "fx_route_mode", acc()), bands (p.apvts, "fx_route_bands", acc()),
        xlo (p.apvts, "fx_route_xlo", "X LOW", acc()), xhi (p.apvts, "fx_route_xhi", "X HIGH", acc()), sens (p.apvts, "fx_route_sens", "SENSITIVITY", acc()),
        len (p.apvts, "fx_route_len", "ATTACK LENGTH", acc()), thr (p.apvts, "fx_route_thr", "THRESHOLD", acc()), knee (p.apvts, "fx_route_knee", "KNEE", acc()),
        blend (p.apvts, "fx_route_blend", "BLEND", acc()), keepT (p.apvts, "fx_route_keep", "KEEP IN BAND", acc())
    {
        keepT.setTooltip ("FREQUENCY: what a branch changes stays inside its band (distortion of HIGH never reaches the sub). Off: the branches sum as they come out");
        onT.setTooltip ("GHOST SPLIT on / off. While it routes units it replaces the PARALLEL lanes; a change fades the chain out and back in (click-free)");
        bands.setTooltip ("FREQUENCY: 2 bands (LOW / HIGH) or 3 (LOW / MID / HIGH)");
        xlo.slider.setTooltip ("Low crossover (Linkwitz-Riley 4th order): 120 Hz keeps an 808's sub in LOW");
        xhi.slider.setTooltip ("High crossover (3 BANDS)");
        sens.slider.setTooltip ("TRANSIENT / BODY: how easily a hit counts as ATTACK");
        len.slider.setTooltip ("TRANSIENT / BODY: how long the ATTACK part lasts");
        thr.slider.setTooltip ("LEVEL: above THRESHOLD = LOUD, below = QUIET");
        knee.slider.setTooltip ("LEVEL: the soft knee around the threshold (0 = hard)");
        blend.slider.setTooltip ("PARALLEL: 0 % = only A, 100 % = only B (50 % = half of each)");
        mode.onChange = [this] (int) { layoutKnobs(); refresh(); };
        for (auto* c : std::initializer_list<juce::Component*> { &onT, &mode, &bands, &xlo, &xhi, &sens, &len, &thr, &knee, &blend, &keepT }) addAndMakeVisible (c);
        for (int b = 0; b < gpfx::kRouteBranches; ++b)
        {
            auto* k = levels.add (new gpfx::ui::Knob (p.apvts, "fx_route_g" + juce::String (b + 1), "LEVEL", acc()));
            k->slider.setTooltip ("This branch's level (0 dB = as split)");
            addAndMakeVisible (k);
            auto* a = adds.add (new juce::TextButton ("+ UNIT")); a->setTooltip ("Put a unit in this branch (it leaves the main chain or its other branch)");
            a->setColour (juce::TextButton::buttonOnColourId, acc()); a->onClick = [this, b] { addMenu (b); }; addAndMakeVisible (a);
        }
        refresh();
        startTimerHz (10);
    }
    ~BoxSplitPage() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        using namespace gpfx::ui;
        const auto ink = ghost() ? ghosty::glass::ink() : col::white.get(), ink2 = ghost() ? ghosty::glass::ink2() : col::dim.get();
        drawCaption (g, modeCap.toFloat(), "MODE", ink2, 10.0f);
        const int m = curMode(), nb = gpfx::routeBranchCount (m, curBands());
        for (int b = 0; b < nb; ++b)
        {
            auto c = cols[(size_t) b].toFloat();
            if (ghost()) ghosty::glass::body (g, c, 12.0f, juce::Colours::white, 0.38f);
            else { g.setColour (T (juce::Colour (0xff121520))); g.fillRoundedRectangle (c, 10.0f); g.setColour (acc().withAlpha (0.35f)); g.drawRoundedRectangle (c.reduced (0.5f), 10.0f, 1.0f); }
            drawCaption (g, headR[(size_t) b].toFloat(), "BRANCH " + gpfx::routeBranchName (m, curBands(), b), ink, 12.0f);
            drawCaption (g, latR[(size_t) b].toFloat(), "latency " + juce::String (branchLatency (b)) + " samples", ink2, 10.0f);
            if (chipsIn (b) == 0) drawCaption (g, emptyR[(size_t) b].toFloat(), "empty: the branch passes its part as it is", ink2, 10.5f);
        }
        drawCaption (g, infoR.toFloat(), infoText(), ghost() ? ghosty::glass::violetDeep() : acc().brighter (0.2f), 10.5f);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (6, 2);
        auto row1 = r.removeFromTop (30);
        onT.setBounds (row1.removeFromLeft (100).reduced (0, 2)); row1.removeFromLeft (12);
        modeCap = row1.removeFromLeft (40); mode.setBounds (row1.removeFromLeft (560)); row1.removeFromLeft (10);
        bands.setBounds (row1.removeFromLeft (100).reduced (0, 3));
        r.removeFromTop (4);
        knobRow = r.removeFromTop (78);
        r.removeFromTop (4);
        infoR = r.removeFromBottom (18);
        r.removeFromBottom (4);
        const int gap = 10, nb = gpfx::routeBranchCount (curMode(), curBands());
        const int w = (r.getWidth() - (nb - 1) * gap) / nb;
        for (int b = 0; b < gpfx::kRouteBranches; ++b)
        {
            const bool vis = b < nb;
            levels[b]->setVisible (vis); adds[b]->setVisible (vis);
            if (! vis) continue;
            auto c = juce::Rectangle<int> (r.getX() + b * (w + gap), r.getY(), w, r.getHeight());
            cols[(size_t) b] = c;
            auto in = c.reduced (10, 8);
            auto top = in.removeFromTop (22); headR[(size_t) b] = top.removeFromLeft (top.getWidth() - 90); latR[(size_t) b] = in.removeFromTop (14);
            levels[b]->setBounds (in.removeFromRight (74).removeFromTop (78));
            adds[b]->setBounds (top.reduced (0, 1));
            emptyR[(size_t) b] = in.removeFromTop (20);
            chipArea[(size_t) b] = in.withTrimmedTop (2);
        }
        layoutKnobs(); layoutChips();
    }
    // tests
    void assign (int unit, int branch) { rack.pushUndo(); rack.setRoute (unit, branch); if (branch > 0) setParam ("fx_route_on", 1.0f); refresh(); }
    int chipsIn (int b) const { int n = 0; for (auto* c : chips) if (c->branch == b) ++n; return n; }
    struct Text { juce::Rectangle<int> r; juce::Colour c; juce::String what; };
    std::vector<Text> texts() const
    {
        using namespace gpfx::ui; std::vector<Text> v;
        const auto ink = ghost() ? ghosty::glass::ink() : col::white.get(), ink2 = ghost() ? ghosty::glass::ink2() : col::dim.get();
        v.push_back ({ modeCap.withSizeKeepingCentre (modeCap.getWidth(), 9), ink2, "MODE" });
        v.push_back ({ headR[0].withWidth (80).withSizeKeepingCentre (80, 10), ink, "branch head" });
        v.push_back ({ latR[1].withWidth (80).withSizeKeepingCentre (80, 9), ink2, "latency" });
        return v;
    }

private:
    static juce::Colour acc() { return fxtheme::T (juce::Colour (kAccent)); }
    struct Chip : juce::TextButton
    {
        Chip (BoxSplitPage& o, int u, int b) : juce::TextButton (gpfx::unitInfo (u).shortName), page (o), unit (u), branch (b)
        { setTooltip ("Click: move this unit to another branch or back to the main chain"); setColour (juce::TextButton::buttonOnColourId, gpfx::ui::accentOf (u)); }
        BoxSplitPage& page; int unit, branch;
        void clicked() override { page.chipMenu (unit, this); }
    };
    BoxProcessor& proc; gpfx::Rack::Impl& rack;
    gpfx::ui::Toggle onT; gpfx::ui::Segmented mode; gpfx::ui::Choice bands;
    gpfx::ui::Knob xlo, xhi, sens, len, thr, knee, blend; gpfx::ui::Toggle keepT;
    juce::OwnedArray<gpfx::ui::Knob> levels; juce::OwnedArray<juce::TextButton> adds; juce::OwnedArray<Chip> chips;
    juce::Rectangle<int> modeCap, knobRow, infoR;
    std::array<juce::Rectangle<int>, gpfx::kRouteBranches> cols, headR, latR, emptyR, chipArea;
    juce::String chipKey; int shownMode = -1, shownBands = -1;

    int curMode() const { auto* v = proc.apvts.getRawParameterValue ("fx_route_mode"); return v != nullptr ? juce::jlimit (0, 4, (int) std::lround (v->load())) : 0; }
    int curBands() const { auto* v = proc.apvts.getRawParameterValue ("fx_route_bands"); return v != nullptr ? juce::jlimit (0, 1, (int) std::lround (v->load())) : 0; }
    void setParam (const char* id, float v) { if (auto* q = proc.apvts.getParameter (id)) { q->beginChangeGesture(); q->setValueNotifyingHost (q->convertTo0to1 (v)); q->endChangeGesture(); } }
    int effBranch (int u) const { const int r = rack.routeBranchOf (u); return r > 0 ? std::min (r, gpfx::routeBranchCount (curMode(), curBands())) - 1 : -1; }
    int branchLatency (int b) const { int l = 0; for (int u = 0; u < gpfx::kNumUnits; ++u) if (effBranch (u) == b) l += rack.units[(size_t) u]->latency(); return l; }
    juce::String infoText() const
    {
        const auto order = rack.getOrder(); int first = -1;
        for (int k = 0; k < gpfx::kNumUnits && first < 0; ++k) if (rack.routeBranchOf (order[(size_t) k]) > 0) first = order[(size_t) k];
        if (first < 0) return "Put units into the branches (+ UNIT, or right-click a tile in the strip). With every branch empty the sound is unchanged.";
        auto* on = proc.apvts.getRawParameterValue ("fx_route_on");
        const juce::String where = "The split sits where " + juce::String (gpfx::unitInfo (first).title) + " is in the chain; units not in a branch run before or after it by their place. ";
        return (on != nullptr && on->load() > 0.5f ? where : "SPLIT is OFF: the units run in the main chain. ") + "Host latency now " + juce::String (proc.rack.latencySamples()) + " samples.";
    }
    void layoutKnobs()
    {
        const int m = curMode();
        std::vector<juce::Component*> vis;
        for (auto* k : std::initializer_list<juce::Component*> { &xlo, &xhi, &sens, &len, &thr, &knee, &blend }) k->setVisible (false);
        bands.setVisible (m == gpfx::rmFreq); keepT.setVisible (m == gpfx::rmFreq);
        if (m == gpfx::rmFreq) { vis.push_back (&xlo); if (curBands() == 1) vis.push_back (&xhi); }
        else if (m == gpfx::rmTransient) { vis.push_back (&sens); vis.push_back (&len); }
        else if (m == gpfx::rmLevel) { vis.push_back (&thr); vis.push_back (&knee); }
        else if (m == gpfx::rmParallel) vis.push_back (&blend);
        auto r = knobRow;
        for (auto* c : vis) { c->setVisible (true); c->setBounds (r.removeFromLeft (96)); r.removeFromLeft (8); }
        if (m == gpfx::rmFreq) keepT.setBounds (r.removeFromLeft (130).withSizeKeepingCentre (130, 26));
    }
    void layoutChips()
    {
        std::array<juce::Rectangle<int>, gpfx::kRouteBranches> area = chipArea;
        for (auto* c : chips)
        {
            auto& a = area[(size_t) c->branch];
            const int w = (int) ghosty::textWidth (gpfx::ui::font (11.0f), c->getButtonText()) + 22;
            if (a.getWidth() < w) { a.removeFromTop (30); a = a.withX (chipArea[(size_t) c->branch].getX()).withWidth (chipArea[(size_t) c->branch].getWidth()); }
            c->setVisible (a.getHeight() >= 26 && a.getWidth() >= w);
            if (c->isVisible()) { c->setBounds (a.getX(), a.getY(), w, 26); a.removeFromLeft (w + 6); }
        }
    }
    void refresh()
    {   // the chips: the units in each branch, in chain order
        juce::String key; key << curMode() << "/" << curBands() << ":";
        const auto order = rack.getOrder();
        for (int u : order) if (effBranch (u) >= 0) key << u << "=" << effBranch (u) << ",";
        if (key != chipKey)
        {
            chipKey = key; chips.clear();
            for (int u : order) if (const int b = effBranch (u); b >= 0) { auto* c = chips.add (new Chip (*this, u, b)); c->setToggleState (proc.unitIsOn (u), juce::dontSendNotification); addAndMakeVisible (c); }
            fxtheme::retint (*this);
            if (curMode() != shownMode || curBands() != shownBands) { shownMode = curMode(); shownBands = curBands(); resized(); }
            layoutChips();
        }
        for (auto* c : chips) c->setToggleState (proc.unitIsOn (c->unit), juce::dontSendNotification);
        repaint();
    }
    void addMenu (int b)
    {
        juce::PopupMenu m; m.addSectionHeader ("BRANCH " + gpfx::routeBranchName (curMode(), curBands(), b) + ": which unit runs here?");
        const auto order = rack.getOrder();
        for (int u : order)
        {
            if (u == gpfx::uLim) continue;
            const int cur = effBranch (u);
            m.addItem (1 + u, juce::String (gpfx::unitInfo (u).title) + (cur >= 0 ? "  (in " + gpfx::routeBranchName (curMode(), curBands(), cur) + ")" : "") + (proc.unitIsOn (u) ? "" : "  (off)"), cur != b, cur == b);
        }
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (adds[b]), [sp = juce::Component::SafePointer<BoxSplitPage> (this), b] (int r)
        {
            if (sp == nullptr || r <= 0) return;
            sp->assign (r - 1, b + 1);
        });
    }
    void chipMenu (int u, juce::Component* target)
    {
        juce::PopupMenu m; m.addSectionHeader (gpfx::unitInfo (u).title);
        const int nb = gpfx::routeBranchCount (curMode(), curBands()), cur = effBranch (u);
        for (int b = 0; b < nb; ++b) m.addItem (1 + b, "Branch " + gpfx::routeBranchName (curMode(), curBands(), b), b != cur, b == cur);
        m.addItem (10, "Back to the main chain (not split)");
        m.addItem (11, proc.unitIsOn (u) ? "Switch it off" : "Switch it on");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [sp = juce::Component::SafePointer<BoxSplitPage> (this), u] (int r)
        {
            if (sp == nullptr || r <= 0) return;
            if (r == 10) sp->assign (u, 0);
            else if (r == 11) { if (auto* q = sp->proc.apvts.getParameter (gpfx::pid (u, "on"))) q->setValueNotifyingHost (q->getValue() > 0.5f ? 0.0f : 1.0f); sp->refresh(); }
            else sp->assign (u, r);
        });
    }
    void timerCallback() override { refresh(); }
};
