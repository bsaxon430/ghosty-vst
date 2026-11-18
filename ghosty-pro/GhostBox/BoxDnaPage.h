#pragma once
// GHOST DNA page (a tab of the rack panel, after CAPTURE): BREED (A x B, BLEND), MUTATE (STRENGTH), SEED (shown, editable, NEW),
// EVOLVE (4 variation tiles: click = hear it), KEEP / UNDO TO ORIGINAL, and the LOCKS: a chip per unit that is on (click = lock the whole
// unit, right-click = lock single settings). Themed in all 4 looks (GHOST: glass tiles, indigo ink). Results are normal chains.
#include "BoxProcessor.h"
#include "FxUi.h"
#include "GhostGlass.h"

class BoxDnaPage : public juce::Component, private juce::Timer
{
public:
    explicit BoxDnaPage (BoxProcessor& p) : proc (p)
    {
        using namespace gpfx::ui;
        auto btn = [this] (juce::TextButton& b, const char* text, const char* tip, juce::Colour acc) { b.setButtonText (text); b.setTooltip (tip); b.setColour (juce::TextButton::buttonOnColourId, acc); addAndMakeVisible (b); };
        btn (breedB, "BREED", "BREED: a new chain from A and B: every unit from one of them (BLEND = how much of B), shared settings glide between them", col::pink);
        btn (mutateB, "MUTATE", "MUTATE: seeded random changes to the units that are on (STRENGTH = how far), never the locked ones; gain-matched", col::slime);
        btn (evolveB, "EVOLVE", "EVOLVE: 4 variations (seeds SEED .. SEED+3) to audition: click a tile to hear it, KEEP the one you like", col::ice);
        btn (keepB, "KEEP", "KEEP what you hear: the next MUTATE / EVOLVE starts from it", col::slime);
        btn (undoB, "UNDO TO ORIGINAL", "Back to exactly what played before the first DNA result", col::gold);
        btn (newSeedB, "NEW", "A new random seed", col::ecto);
        breedB.onClick = [this] { proc.dnaBreed (idOf (srcA), idOf (srcB)); };
        mutateB.onClick = [this] { proc.dnaMutate(); };
        evolveB.onClick = [this] { proc.dnaEvolve(); };
        keepB.onClick = [this] { proc.dnaKeep(); };
        undoB.onClick = [this] { proc.dnaUndo(); };
        newSeedB.onClick = [this] { proc.dnaSeed = juce::Random::getSystemRandom().nextInt (999999) + 1; refresh(); };
        seedBox.setInputRestrictions (7, "0123456789"); seedBox.setJustification (juce::Justification::centred); seedBox.setTooltip ("SEED: the same seed + settings + locks give the same result (saved with the session)");
        seedBox.onTextChange = [this] { proc.dnaSeed = juce::jmax (1, seedBox.getText().getIntValue()); };
        addAndMakeVisible (seedBox);
        for (auto* s : { &strength, &blend })
        {
            s->setSliderStyle (juce::Slider::LinearHorizontal); s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 42, 16); s->setRange (0.0, 100.0, 1.0); s->setTextValueSuffix (" %");
            s->setColour (juce::Slider::rotarySliderFillColourId, s == &strength ? col::slime : col::pink); addAndMakeVisible (s);
        }
        strength.setTooltip ("STRENGTH: how far MUTATE / EVOLVE move things"); blend.setTooltip ("BLEND: how much of B a BREED takes");
        strength.onValueChange = [this] { proc.dnaStrength = (float) strength.getValue() / 100.0f; };
        blend.onValueChange = [this] { proc.dnaBlend = (float) blend.getValue() / 100.0f; };
        for (auto* c : { &srcA, &srcB }) { c->setTooltip (c == &srcA ? "BREED parent A (the rest of the sound comes from A)" : "BREED parent B"); addAndMakeVisible (c); }
        for (int k = 0; k < 4; ++k) { auto* t = tiles.add (new VarTile (*this, k)); addAndMakeVisible (t); }
        refresh();
        startTimerHz (10);
    }
    ~BoxDnaPage() override { stopTimer(); }

    void refresh()
    {
        seedBox.setText (juce::String (proc.dnaSeed), juce::dontSendNotification);
        strength.setValue (juce::roundToInt (proc.dnaStrength * 100.0f), juce::dontSendNotification);
        blend.setValue (juce::roundToInt (proc.dnaBlend * 100.0f), juce::dontSendNotification);
        const int a = idOf (srcA), b = idOf (srcB);
        names = proc.dnaSourceNames (ids);
        for (auto* c : { &srcA, &srcB }) { c->clear (juce::dontSendNotification); c->addItemList (names, 1); }
        auto pick = [this] (juce::ComboBox& c, int id, int def) { int i = 0; for (; i < (int) ids.size(); ++i) if (ids[(size_t) i] == id) break; c.setSelectedItemIndex (i < (int) ids.size() ? i : juce::jmin (def, (int) ids.size() - 1), juce::dontSendNotification); };
        pick (srcA, a, 0); pick (srcB, b, juce::jmin (1, (int) ids.size() - 1));
        rebuildChips();
        for (auto* t : tiles) t->repaint();
        const bool busy = proc.dnaBusy.load();
        for (auto* b2 : { &breedB, &mutateB, &evolveB }) b2->setEnabled (! busy);
        keepB.setEnabled (proc.dnaOriginalValid()); undoB.setEnabled (proc.dnaOriginalValid());
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        using namespace gpfx::ui;
        const auto ink2 = ghost() ? ghosty::glass::ink2() : col::dim.get();
        drawCaption (g, labA.toFloat(), "A", ink2, 11.0f); drawCaption (g, labB.toFloat(), "B", ink2, 11.0f);
        drawCaption (g, labBlend.toFloat(), "BLEND", ink2, 10.0f); drawCaption (g, labStr.toFloat(), "STRENGTH", ink2, 10.0f); drawCaption (g, labSeed.toFloat(), "SEED", ink2, 10.0f);
        drawCaption (g, labLocks.toFloat(), chips.isEmpty() ? "LOCKS: switch some units on, then lock what must not change" : "LOCKS", ink2, 10.0f);
        const auto msg = proc.dnaBusy.load() ? juce::String ("working (matching the loudness)...") : proc.dnaMessage;
        drawCaption (g, msgR.toFloat(), msg, ghost() ? ghosty::glass::violetDeep() : col::slime.get(), 10.5f);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (6, 2);
        auto row1 = r.removeFromTop (28);
        labA = row1.removeFromLeft (14); srcA.setBounds (row1.removeFromLeft (170).reduced (0, 2)); row1.removeFromLeft (8);
        labB = row1.removeFromLeft (14); srcB.setBounds (row1.removeFromLeft (170).reduced (0, 2)); row1.removeFromLeft (8);
        labBlend = row1.removeFromLeft (40); blend.setBounds (row1.removeFromLeft (120)); row1.removeFromLeft (6);
        breedB.setBounds (row1.removeFromLeft (76)); row1.removeFromLeft (16);
        labStr = row1.removeFromLeft (60); strength.setBounds (row1.removeFromLeft (120)); row1.removeFromLeft (6);
        mutateB.setBounds (row1.removeFromLeft (80)); row1.removeFromLeft (16);
        labSeed = row1.removeFromLeft (34); seedBox.setBounds (row1.removeFromLeft (70).reduced (0, 3)); row1.removeFromLeft (4); newSeedB.setBounds (row1.removeFromLeft (46));
        r.removeFromTop (6);
        auto row2 = r.removeFromTop (26);
        labLocks = row2.removeFromLeft (row2.getWidth()); chipsR = labLocks.withTrimmedLeft (46);
        r.removeFromTop (6);
        auto row3 = r.removeFromTop (28);
        evolveB.setBounds (row3.removeFromLeft (96)); row3.removeFromLeft (8);
        keepB.setBounds (row3.removeFromLeft (70)); row3.removeFromLeft (6);
        undoB.setBounds (row3.removeFromLeft (150)); row3.removeFromLeft (12);
        msgR = row3;
        r.removeFromTop (8);
        const int gap = 8, w = (r.getWidth() - 3 * gap) / 4;
        for (int k = 0; k < 4; ++k) tiles[k]->setBounds (r.getX() + k * (w + gap), r.getY(), w, r.getHeight());
        layoutChips();
    }
    // tests
    juce::Component* tile (int k) { return tiles[k]; }
    juce::Component* chip (int u) { for (auto* c : chips) if (c->unit == u) return c; return nullptr; }
    void chipClick (int u) { if (proc.dnaLocks.unitLocked (u)) proc.dnaLocks.units.erase (u); else proc.dnaLocks.units.insert (u); refresh(); }
    struct Text { juce::Rectangle<int> r; juce::Colour c; juce::String what; };
    std::vector<Text> texts();

private:
    struct VarTile : juce::Component, juce::SettableTooltipClient
    {
        VarTile (BoxDnaPage& o, int k) : page (o), i (k) { setTooltip ("Click: hear this variation (KEEP keeps it, UNDO TO ORIGINAL goes back)"); }
        BoxDnaPage& page; int i;
        void paint (juce::Graphics& g) override;
        void mouseUp (const juce::MouseEvent& e) override { if (! e.mouseWasDraggedSinceMouseDown() && i < page.proc.dnaCount) { page.proc.dnaAudition (i); page.refresh(); } }
    };
    struct Chip : juce::Button
    {
        Chip (BoxDnaPage& o, int u) : juce::Button (gpfx::unitInfo (u).shortName), page (o), unit (u) { setColour (juce::TextButton::buttonOnColourId, gpfx::ui::col::gold); setTooltip ("Click: lock / unlock this whole unit. Right-click: lock single settings"); }
        BoxDnaPage& page; int unit;
        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            using namespace gpfx::ui;
            const bool locked = page.proc.dnaLocks.unitLocked (unit);
            int some = 0; for (auto& q : page.proc.genome().all()) if (q.unit == unit && page.proc.dnaLocks.paramLocked (q.id)) ++some;
            setToggleState (locked, juce::dontSendNotification);
            getLookAndFeel().drawButtonBackground (g, *this, findColour (juce::TextButton::buttonColourId), over, down);
            const auto ink = ghost() ? (locked ? juce::Colours::white : ghosty::glass::ink()) : locked ? LnF::litColours (findColour (juce::TextButton::buttonOnColourId)).second : col::white.get();
            g.setColour (ink); g.setFont (font (10.5f));
            g.drawFittedText (juce::String (locked ? "LOCK " : some > 0 ? juce::String (some) + "x " : "") + getButtonText(), getLocalBounds().reduced (4, 0), juce::Justification::centred, 1, 0.8f);
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu()) { page.paramMenu (unit, this); return; }
            if (! e.mouseWasDraggedSinceMouseDown()) page.chipClick (unit);
        }
    };
    BoxProcessor& proc;
    juce::TextButton breedB, mutateB, evolveB, keepB, undoB, newSeedB;
    juce::ComboBox srcA, srcB; juce::Slider strength, blend; juce::TextEditor seedBox;
    juce::OwnedArray<VarTile> tiles; juce::OwnedArray<Chip> chips;
    juce::StringArray names; std::vector<int> ids;
    juce::Rectangle<int> labA, labB, labBlend, labStr, labSeed, labLocks, chipsR, msgR;
    int seenVersion = -1; juce::String chipKey;

    int idOf (const juce::ComboBox& c) const { const int i = c.getSelectedItemIndex(); return juce::isPositiveAndBelow (i, (int) ids.size()) ? ids[(size_t) i] : 0; }
    void rebuildChips()
    {   // a chip per unit that is on in what you hear (locked units stay listed)
        juce::String key; std::vector<int> us;
        for (int u = 0; u < gpfx::kNumUnits; ++u)
            if (u != gpfx::uLim && (proc.unitIsOn (u) || proc.dnaLocks.unitLocked (u))) { us.push_back (u); key << u << ","; }
        if (key == chipKey) { for (auto* c : chips) c->repaint(); return; }
        chipKey = key; chips.clear();
        for (int u : us) { auto* c = chips.add (new Chip (*this, u)); addAndMakeVisible (c); }
        fxtheme::retint (*this);
        layoutChips();
    }
    void layoutChips()
    {
        auto r = chipsR;
        for (auto* c : chips)
        {
            const int w = (int) ghosty::textWidth (gpfx::ui::font (10.5f), juce::String ("LOCK ") + c->getButtonText()) + 16;
            c->setVisible (r.getWidth() >= w);
            if (r.getWidth() >= w) { c->setBounds (r.removeFromLeft (w)); r.removeFromLeft (4); }
        }
    }
    void paramMenu (int unit, juce::Component* target)
    {
        juce::PopupMenu m; m.addSectionHeader ("LOCK settings of " + juce::String (gpfx::unitInfo (unit).title));
        std::vector<juce::String> list;
        for (auto& q : proc.genome().all())
            if (q.unit == unit && ! q.isOn) { list.push_back (q.id); m.addItem ((int) list.size(), q.p->getName (40).replace ("FX ", ""), true, proc.dnaLocks.paramLocked (q.id)); }
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [sp = juce::Component::SafePointer<BoxDnaPage> (this), list] (int r)
        {
            if (sp == nullptr || r <= 0 || r > (int) list.size()) return;
            auto& L = sp->proc.dnaLocks.params; const auto id = list[(size_t) r - 1];
            if (L.count (id)) L.erase (id); else L.insert (id);
            sp->refresh();
        });
    }
    void timerCallback() override
    {
        if (proc.dnaVersion.load() != seenVersion || proc.dnaBusy.load()) { seenVersion = proc.dnaVersion.load(); refresh(); }
        else rebuildChips();
    }
};

inline void BoxDnaPage::VarTile::paint (juce::Graphics& g)
{
    using namespace gpfx::ui; namespace gg = ghosty::glass;
    auto& p = page.proc;
    auto r = getLocalBounds().toFloat();
    const bool has = i < p.dnaCount && p.dnaVariations[(size_t) i].isValid(), on = has && p.dnaAuditioning == i, gl = ghost();
    if (gl && on) { g.setColour (gg::violetDeep()); g.fillRoundedRectangle (r.reduced (1.0f), 12.0f); g.setColour (juce::Colours::white.withAlpha (0.5f)); g.drawRoundedRectangle (r.reduced (1.5f), 12.0f, 1.2f); }   // (solid: white text >= 7:1)
    else if (gl) gg::body (g, r.reduced (1.0f), 12.0f, juce::Colours::white, 0.42f);
    else
    {
        g.setColour (on ? LnF::litColours (col::ice.get()).first : T (juce::Colour (0xff151824))); g.fillRoundedRectangle (r.reduced (0.5f), 10);
        g.setColour (col::ice.withAlpha (on ? 0.9f : 0.25f)); g.drawRoundedRectangle (r.reduced (0.5f), 10, 1.2f);
    }
    const auto ink = gl ? (on ? juce::Colours::white : gg::ink()) : on ? LnF::litColours (col::ice.get()).second : col::white.get();
    const auto ink2 = gl ? (on ? juce::Colours::white.withAlpha (0.85f) : gg::ink2()) : on ? ink.withAlpha (0.8f) : col::dim.get();
    auto tr = getLocalBounds().reduced (12, 10);
    g.setColour (ink); g.setFont (font (13.0f));
    g.drawText (has ? "VARIATION " + juce::String (i + 1) : "VARIATION " + juce::String (i + 1), tr.removeFromTop (18), juce::Justification::centredLeft, false);
    g.setColour (ink2); g.setFont (font (10.5f));
    if (! has) { g.drawFittedText ("EVOLVE makes 4 variations of what you hear. Click one to hear it.", tr.removeFromTop (40), juce::Justification::topLeft, 3); return; }
    g.drawText ("seed " + juce::String (p.dnaSeeds[(size_t) i]) + "   output " + (p.dnaTrim[(size_t) i] >= 0 ? "+" : "") + juce::String (p.dnaTrim[(size_t) i], 1) + " dB", tr.removeFromTop (16), juce::Justification::centredLeft, false);
    g.drawFittedText (p.dnaNotes[(size_t) i], tr.removeFromTop (40), juce::Justification::topLeft, 3);
    if (on) { g.setColour (ink); g.setFont (font (10.5f)); g.drawText ("PLAYING", getLocalBounds().reduced (12, 8), juce::Justification::bottomRight, false); }
}

inline std::vector<BoxDnaPage::Text> BoxDnaPage::texts()
{
    using namespace gpfx::ui; std::vector<Text> v;
    for (int k = 0; k < 4; ++k)
    {
        const bool on = proc.dnaAuditioning == k && k < proc.dnaCount;
        const auto c = ghost() ? (on ? juce::Colours::white : ghosty::glass::ink()) : on ? LnF::litColours (col::ice.get()).second : col::white.get();
        v.push_back ({ tiles[k]->getBounds().reduced (12, 10).removeFromTop (18).withWidth (90).withSizeKeepingCentre (90, 9), c, "tile " + juce::String (k + 1) });
    }
    v.push_back ({ labStr.withSizeKeepingCentre (labStr.getWidth(), 9), ghost() ? ghosty::glass::ink2() : col::dim.get(), "STRENGTH" });
    return v;
}
