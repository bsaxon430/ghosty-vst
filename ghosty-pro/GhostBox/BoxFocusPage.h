#pragma once
// GHOST FOCUS page (a tab of the rack panel, "FOCUS", after SPLITS; also the second shortcut button in the strip's free tile cell): the 8
// intentions, then 4 tiles with the starting chains for the chosen one (click = hear it, gain-matched to what played before), AMOUNT
// (refine: how much of the chain's wet sound), KEEP (the chain stays, the rack is yours to edit) and BACK TO ORIGINAL. Themed in 4 looks.
#include "BoxProcessor.h"
#include "BoxFocus.h"
#include "FxUi.h"
#include "GhostGlass.h"

class BoxFocusPage : public juce::Component, private juce::Timer
{
public:
    static constexpr juce::uint32 kAccent = 0xffFFC35A;
    explicit BoxFocusPage (BoxProcessor& p) : proc (p)
    {
        for (int i = 0; i < boxfocus::kIntents; ++i)
        {
            auto* b = intents.add (new juce::TextButton (boxfocus::intentNames()[i]));
            b->setTooltip (boxfocus::intentTips()[i] + juce::String (": shows 4 starting chains built from the rack's own units and presets"));
            b->setClickingTogglesState (false); b->setColour (juce::TextButton::buttonOnColourId, acc());
            b->onClick = [this, i] { proc.focusPick (i); refresh(); };
            addAndMakeVisible (b);
        }
        auto btn = [this] (juce::TextButton& b, const char* text, const char* tip) { b.setButtonText (text); b.setTooltip (tip); b.setColour (juce::TextButton::buttonOnColourId, acc()); addAndMakeVisible (b); };
        btn (keepB, "KEEP", "KEEP the chain you hear: it becomes your rack (edit anything, the next FOCUS starts from it)");
        btn (backB, "BACK TO ORIGINAL", "Back to exactly what played before the first FOCUS chain");
        keepB.onClick = [this] { proc.focusKeep(); refresh(); };
        backB.onClick = [this] { proc.focusBack(); refresh(); };
        amount.setSliderStyle (juce::Slider::LinearHorizontal); amount.setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 16); amount.setRange (0.0, 100.0, 1.0); amount.setTextValueSuffix (" %");
        amount.setColour (juce::Slider::rotarySliderFillColourId, acc()); amount.setDoubleClickReturnValue (true, 100.0);
        amount.setTooltip ("AMOUNT (refine): how much of the chain's wet sound you hear (100 % = as designed). The rack stays fully editable");
        amount.onValueChange = [this] { proc.focusSetAmount ((float) amount.getValue() / 100.0f); };
        addAndMakeVisible (amount);
        for (int k = 0; k < boxfocus::kChains; ++k) { auto* t = tiles.add (new Tile (*this, k)); addAndMakeVisible (t); }
        refresh();
        startTimerHz (10);
    }
    ~BoxFocusPage() override { stopTimer(); }

    void refresh()
    {
        for (int i = 0; i < intents.size(); ++i) intents[i]->setToggleState (i == proc.focusIntent, juce::dontSendNotification);
        const bool busy = proc.focusBusy.load();
        for (auto* b : intents) b->setEnabled (! busy);
        keepB.setEnabled (proc.focusOriginalValid() && proc.focusAuditioning >= 0); backB.setEnabled (proc.focusOriginalValid());
        amount.setEnabled (proc.focusAuditioning >= 0);
        amount.setValue (juce::roundToInt (proc.focusAmount * 100.0f), juce::dontSendNotification);
        for (auto* t : tiles) t->repaint();
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        using namespace gpfx::ui;
        const auto ink2 = ghost() ? ghosty::glass::ink2() : col::dim.get();
        drawCaption (g, wantCap.toFloat(), "I WANT IT", ink2, 10.0f);
        drawCaption (g, amountCap.toFloat(), "AMOUNT", ink2, 10.0f);
        const auto msg = proc.focusBusy.load() ? juce::String ("building the chains (matching the loudness)...") : proc.focusMessage;
        drawCaption (g, msgR.toFloat(), msg, ghost() ? ghosty::glass::violetDeep() : acc().brighter (0.2f), 10.5f);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (6, 2);
        auto row1 = r.removeFromTop (34);
        wantCap = row1.removeFromLeft (64);
        const int w = juce::jmin (110, (row1.getWidth() - 7 * 6) / boxfocus::kIntents);
        for (auto* b : intents) { b->setBounds (row1.removeFromLeft (w).reduced (0, 2)); row1.removeFromLeft (6); }
        r.removeFromTop (6);
        auto row2 = r.removeFromTop (28);
        amountCap = row2.removeFromLeft (52); amount.setBounds (row2.removeFromLeft (220)); row2.removeFromLeft (12);
        keepB.setBounds (row2.removeFromLeft (80)); row2.removeFromLeft (6);
        backB.setBounds (row2.removeFromLeft (150)); row2.removeFromLeft (12);
        msgR = row2;
        r.removeFromTop (8);
        const int gap = 8, tw = (r.getWidth() - 3 * gap) / boxfocus::kChains;
        for (int k = 0; k < boxfocus::kChains; ++k) tiles[k]->setBounds (r.getX() + k * (tw + gap), r.getY(), tw, r.getHeight());
    }
    // tests
    juce::Component* tile (int k) { return tiles[k]; }
    void tileClick (int k) { if (k < proc.focusCount()) { proc.focusAudition (k); refresh(); } }
    struct Text { juce::Rectangle<int> r; juce::Colour c; juce::String what; };
    std::vector<Text> texts() const
    {
        using namespace gpfx::ui; std::vector<Text> v;
        for (int k = 0; k < boxfocus::kChains; ++k)
        {
            const bool on = proc.focusAuditioning == k;
            const auto c = ghost() ? (on ? juce::Colours::white : ghosty::glass::ink()) : on ? LnF::litColours (acc()).second : col::white.get();
            v.push_back ({ tiles[k]->getBounds().reduced (12, 10).removeFromTop (18).withWidth (90).withSizeKeepingCentre (90, 9), c, "tile " + juce::String (k + 1) });
        }
        v.push_back ({ wantCap.withSizeKeepingCentre (wantCap.getWidth(), 9), ghost() ? ghosty::glass::ink2() : col::dim.get(), "I WANT IT" });
        return v;
    }

private:
    static juce::Colour acc() { return fxtheme::T (juce::Colour (kAccent)); }
    struct Tile : juce::Component, juce::SettableTooltipClient
    {
        Tile (BoxFocusPage& o, int k) : page (o), i (k) { setTooltip ("Click: hear this chain (gain-matched). KEEP keeps it, BACK TO ORIGINAL goes back"); }
        BoxFocusPage& page; int i;
        void paint (juce::Graphics& g) override
        {
            using namespace gpfx::ui; namespace gg = ghosty::glass;
            auto& p = page.proc;
            auto r = getLocalBounds().toFloat();
            const bool has = i < p.focusCount(), on = has && p.focusAuditioning == i, gl = ghost();
            if (gl && on) { g.setColour (gg::violetDeep()); g.fillRoundedRectangle (r.reduced (1.0f), 12.0f); g.setColour (juce::Colours::white.withAlpha (0.5f)); g.drawRoundedRectangle (r.reduced (1.5f), 12.0f, 1.2f); }
            else if (gl) gg::body (g, r.reduced (1.0f), 12.0f, juce::Colours::white, 0.42f);
            else
            {
                g.setColour (on ? LnF::litColours (acc()).first : T (juce::Colour (0xff151824))); g.fillRoundedRectangle (r.reduced (0.5f), 10);
                g.setColour (acc().withAlpha (on ? 0.9f : 0.25f)); g.drawRoundedRectangle (r.reduced (0.5f), 10, 1.2f);
            }
            const auto ink = gl ? (on ? juce::Colours::white : gg::ink()) : on ? LnF::litColours (acc()).second : col::white.get();
            const auto ink2 = gl ? (on ? juce::Colours::white.withAlpha (0.85f) : gg::ink2()) : on ? ink.withAlpha (0.8f) : col::dim.get();
            auto tr = getLocalBounds().reduced (12, 10);
            g.setColour (ink); g.setFont (font (13.0f));
            if (p.focusIntent < 0) { g.drawText ("CHAIN " + juce::String (i + 1), tr.removeFromTop (18), juce::Justification::centredLeft, false); g.setColour (ink2); g.setFont (font (10.5f));
                                     g.drawFittedText ("Pick what you want above: 4 starting chains appear here.", tr.removeFromTop (40), juce::Justification::topLeft, 3); return; }
            const auto& c = boxfocus::chain (p.focusIntent, i);
            g.drawText (c.name, tr.removeFromTop (18), juce::Justification::centredLeft, false);
            g.setColour (ink2); g.setFont (font (10.5f));
            g.drawFittedText (c.blurb, tr.removeFromTop (30), juce::Justification::topLeft, 2);
            g.drawFittedText (boxfocus::unitsText (c), tr.removeFromTop (30), juce::Justification::topLeft, 2);
            if (has) g.drawText ("gain match " + juce::String (p.focusTrim[(size_t) i] >= 0 ? "+" : "") + juce::String (p.focusTrim[(size_t) i], 1) + " dB", tr.removeFromTop (16), juce::Justification::centredLeft, false);
            if (on) { g.setColour (ink); g.drawText ("PLAYING", getLocalBounds().reduced (12, 8), juce::Justification::bottomRight, false); }
        }
        void mouseUp (const juce::MouseEvent& e) override { if (! e.mouseWasDraggedSinceMouseDown()) page.tileClick (i); }
    };
    BoxProcessor& proc;
    juce::OwnedArray<juce::TextButton> intents; juce::TextButton keepB, backB; juce::Slider amount; juce::OwnedArray<Tile> tiles;
    juce::Rectangle<int> wantCap, amountCap, msgR;
    int seenVersion = -1;
    void timerCallback() override { if (proc.focusVersion.load() != seenVersion || proc.focusBusy.load()) { seenVersion = proc.focusVersion.load(); refresh(); } }
};
