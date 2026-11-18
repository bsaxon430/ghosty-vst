#include "HoldStrip.h"

namespace gslice::ui
{
HoldStrip::HoldStrip (SliceProcessor& p) : proc (p)
{
    auto add = [this] (juce::Component& c, const juce::String& id, const juce::String& tip)
    {
        c.setComponentID (id); addAndMakeVisible (c);
        if (auto* s = dynamic_cast<juce::SettableTooltipClient*> (&c); s != nullptr && tip.isNotEmpty()) s->setTooltip (tip);
    };
    add (modeBox, "hold.mode", "ELASTIC: stretch it. SUSTAIN: a grain loop that follows the pitch (vowels, notes). TEXTURE: a spectral hold (pads). FREEZE: an endless smear");
    add (autoBtn, "hold.auto", "AUTO: pick the stable region again (lowest spectral change + voiced)");
    add (removeBtn, "hold.remove", "Remove this HOLD (the segment plays at 1x again)");
    add (closeBtn, "hold.close", "Close the HOLD strip");
    modeBox.addItemList ({ "ELASTIC", "SUSTAIN", "TEXTURE", "FREEZE" }, 1);
    modeBox.onChange = [this] { if (! refreshing) change ([this] (HoldOp& h) { h.mode = (HoldMode) (modeBox.getSelectedId() - 1); }, "Hold mode"); };
    autoBtn.onClick = [this] { autoPick(); };
    removeBtn.onClick = [this]
    {
        if (auto q = ops::removeHold (proc.project(), rgn, hid)) proc.apply (*q, "Remove hold");
        hid = rgn = 0; if (onClose) onClose();
    };
    closeBtn.onClick = [this] { hid = rgn = 0; if (onClose) onClose(); };
    for (auto* f : { &driftF, &variationF, &smearF })
    {
        addAndMakeVisible (*f); f->capW = f == &variationF ? 76 : 50;
        f->s.onDragStart = [this] { proc.beginGesture(); }; f->s.onDragEnd = [this] { proc.endGesture(); };
        f->s.onValueChange = [this, f]
        {
            if (refreshing) return;
            const double v = f->s.getValue();
            change ([this, f, v] (HoldOp& h) { if (f == &driftF) h.driftCents = v; else if (f == &variationF) h.variation = v; else h.smear = v / 100.0; }, "Hold " + f->cap.getText().toLowerCase());
        };
    }
    driftF.s.setTooltip ("SUSTAIN: slow random pitch drift (+- cents) so a long hold stays alive");
    variationF.s.setTooltip ("SUSTAIN: how far the grain walk roams through the stable region");
    smearF.s.setTooltip ("TEXTURE: how much the phases are randomised (low = a held tone, high = a wash)");
    setComponentID ("holdstrip");
}

std::vector<juce::Component*> HoldStrip::controls() { return { &modeBox, &autoBtn, &removeBtn, &closeBtn, &driftF, &variationF, &smearF }; }
void HoldStrip::setPalette (const Palette& p) { P = p; for (auto* f : { &driftF, &variationF, &smearF }) f->cap.setColour (juce::Label::textColourId, P.dim); repaint(); }
void HoldStrip::show (int64 r, int64 h) { rgn = r; hid = h; refresh(); }
const HoldOp* HoldStrip::hold() const
{
    if (const auto* r = proc.project().find (rgn)) for (auto& h : r->holds) if (h.id == hid) return &h;
    return nullptr;
}

void HoldStrip::change (const std::function<void (HoldOp&)>& f, const juce::String& label)
{
    const auto* h = hold(); if (h == nullptr) return;
    HoldOp x = *h; f (x);
    if (auto q = ops::setHold (proc.project(), rgn, x)) proc.apply (*q, label);
    refresh();
}

void HoldStrip::autoPick()
{
    const auto* h = hold(); auto src = proc.getSource(); if (h == nullptr || src == nullptr) return;
    const auto an = proc.analysis();
    const auto st = hold::pickStable (*src, h->selA, h->selB, an != nullptr && an->fingerprint == src->fingerprint ? &an->syllables : nullptr);
    juce::String why;
    auto q = ops::setHoldStable (proc.project(), rgn, hid, st.a, st.b, &why);
    Project base = q ? *q : proc.project();
    if (const auto* r = base.find (rgn))
        for (auto& x : r->holds) if (x.id == hid) { HoldOp y = x; y.autoStable = true; if (auto q2 = ops::setHold (base, rgn, y)) base = *q2; }
    proc.apply (base, "Hold: auto stable region");
    if (toast) toast (st.unstable ? "unstable region: TEXTURE may sound better" : "stable region " + juce::String (juce::roundToInt ((double) (st.b - st.a) / src->sampleRate * 1000.0)) + " ms (score " + juce::String (st.score, 2) + ")");
    refresh();
}

void HoldStrip::refresh()
{
    const auto* h = hold(); if (h == nullptr) return;
    refreshing = true;
    modeBox.setSelectedId ((int) h->mode + 1, juce::dontSendNotification);
    driftF.s.setValue (h->driftCents, juce::dontSendNotification); variationF.s.setValue (h->variation, juce::dontSendNotification); smearF.s.setValue (h->smear * 100.0, juce::dontSendNotification);
    driftF.setEnabled (h->mode == HoldMode::Sustain); variationF.setEnabled (h->mode == HoldMode::Sustain); smearF.setEnabled (h->mode == HoldMode::Texture);
    autoBtn.setToggleState (h->autoStable, juce::dontSendNotification);
    refreshing = false;
    repaint();
}

void HoldStrip::resized()
{
    auto r = getLocalBounds().reduced (10, 7);
    titleArea = r.removeFromLeft (250); r.removeFromLeft (8);
    closeBtn.setBounds (r.removeFromRight (30)); r.removeFromRight (6);
    removeBtn.setBounds (r.removeFromRight (84)); r.removeFromRight (6);
    autoBtn.setBounds (r.removeFromRight (64)); r.removeFromRight (12);
    modeBox.setBounds (r.removeFromLeft (118)); r.removeFromLeft (10);
    const int w = (r.getWidth() - 2 * 8) / 3;
    driftF.setBounds (r.removeFromLeft (w)); r.removeFromLeft (8);
    variationF.setBounds (r.removeFromLeft (w)); r.removeFromLeft (8);
    smearF.setBounds (r.removeFromLeft (w));
}

void HoldStrip::paint (juce::Graphics& g)
{
    if (! P.glass)   // (GHOST: the editor paints the glass panel under the strip)
    {
        g.fillAll (P.panel);
        g.setColour (P.line); g.fillRect (0, 0, getWidth(), 1);
    }
    auto t = titleArea;
    g.setFont (LnF::uiFont (13.0f)); g.setColour (P.marker);
    g.drawText ("GHOST HOLD", t.removeFromTop (15), juce::Justification::centredLeft);
    // what the analysis found (from the compiled plan's hold piece)
    juce::String info;
    for (auto& pc : proc.currentPlan().pieces)
        if (pc.hold != nullptr && pc.hold->holdId == hid)
        {
            const auto& H = *pc.hold;
            info = H.voiced ? juce::String (H.f0, 1) + " Hz" + (H.vibDepth > 0 ? "  vibrato " + juce::String (H.vibRate, 1) + " Hz +-" + juce::String (juce::roundToInt (H.vibDepth)) + " ct" : juce::String())
                            : juce::String ("unvoiced (grains)");
            info << "  held " << juce::String (juce::roundToInt ((double) H.len / proc.hostRate() * 1000.0)) << " ms";
            if (H.mode != H.chosen) info << "  (pitched: ELASTIC)";
            break;
        }
    g.setFont (LnF::uiFont (10.5f, false)); g.setColour (P.dim);
    g.drawText (info, t, juce::Justification::centredLeft, true);
}
} // namespace gslice::ui
