#include "FxPanel.h"

namespace ghost
{
// ================================================================== FX
FxPanel::FxPanel (sfx::SpectralFx& r, const juce::String& t, std::function<void()> ch) : rack (r), title (t), changed (std::move (ch))
{
    for (auto* c : std::initializer_list<juce::Component*> { &close, &onBtn, &warpOn, &order, &fftBtn, &mix, &chainMix }) addAndMakeVisible (c);
    close.onClick = [this] { if (onClose) onClose(); };
    onBtn.onClick = [this] { set (sfx::SpectralFx::pOn, fx().on() ? 0.0 : 1.0); sync(); };
    onBtn.setTooltip ("The whole FX rack on / off (off = the sound exactly as it comes in)");
    warpOn.onClick = [this] { set (sfx::SpectralFx::pWarpOn, fx().warpOn() ? 0.0 : 1.0); sync(); };
    warpOn.setTooltip ("WARP: WarpCore's spectral inversion");
    order.onClick = [this] { set (sfx::SpectralFx::pOrder, fx().order() == 0 ? 1.0 : 0.0); sync(); };
    order.setTooltip ("Which comes first: the 4 bays or WARP");
    fftBtn.onClick = [this]
    {
        juce::PopupMenu m;
        for (int i = 0; i < sfx::kFftSizes; ++i) m.addItem (i + 1, "FFT " + juce::String (sfx::fftSizeAt (i)) + (i == sfx::kDefaultFft ? "  (normal)" : ""), true, fx().fftIndex() == i);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (fftBtn), [this] (int r) { if (r > 0) { set (sfx::SpectralFx::pFft, r - 1); sync(); } });
    };
    fftBtn.setTooltip ("The window the spectral bays work on: bigger = smoother and later, smaller = grittier and quicker");
    mix.s.setRange (0.0, 100.0, 0.1); mix.text = [] (double v) { return juce::String ((int) std::lround (v)) + " %"; };
    mix.s.onValueChange = [this] { if (! syncing) turn (sfx::SpectralFx::pMix, mix.s.getValue()); };
    chainMix.s.setRange (0.0, 100.0, 0.1); chainMix.text = mix.text;
    chainMix.s.onValueChange = [this] { if (! syncing) turn (sfx::SpectralFx::pChainMix, chainMix.s.getValue()); };
    for (int b = 0; b < sfx::kSlots; ++b)
    {
        auto& B = bays[(size_t) b];
        addAndMakeVisible (B.effect); addAndMakeVisible (B.on); addAndMakeVisible (B.mix);
        B.effect.onClick = [this, b] { pickEffect (b); };
        B.effect.setTooltip ("Pick this bay's effect (Spectral Corruptor's 38, in its eight folders)");
        B.on.onClick = [this, b] { set (sfx::SpectralFx::pSlotOn (b), fx().slotOn (b) ? 0.0 : 1.0); sync(); };
        B.mix.s.setRange (0.0, 100.0, 0.1); B.mix.text = mix.text;
        B.mix.s.onValueChange = [this, b] { if (! syncing) turn (sfx::SpectralFx::pSlotMix (b), bays[(size_t) b].mix.s.getValue()); };
    }
    const auto& wp = sfx::warpParams();
    for (int k = 0; k < (int) wp.size(); ++k)
    {
        auto* kn = warpKnobs.add (new Knob (juce::String (wp[(size_t) k].name).toUpperCase()));
        addAndMakeVisible (kn);
        const auto d = wp[(size_t) k];
        kn->s.setRange (d.min, d.max, d.stepped() ? 1.0 : 0.0);
        if (d.logScale && d.min > 0) kn->s.setSkewFactorFromMidPoint (std::sqrt (d.min * d.max));
        kn->text = [d] (double v) { return juce::String (d.text ((float) v)); };
        kn->s.onValueChange = [this, k, kn] { if (! syncing) turn (sfx::SpectralFx::pWarpParam (k), kn->s.getValue()); };
    }
    rebuild();
    sync();
    startTimerHz (10);
}
FxPanel::~FxPanel() { stopTimer(); }

void FxPanel::pickEffect (int bay)
{
    juce::PopupMenu m;
    m.addItem (1000, "EMPTY", true, fx().slotType (bay) == 0);
    const auto& types = sfx::types();
    for (auto& cat : sfx::categories())
    {
        juce::PopupMenu sub;
        for (auto& t : types) if (t.index > 0 && t.category == cat) sub.addItem (t.index + 1, t.name, true, fx().slotType (bay) == t.index);
        m.addSubMenu (juce::String (cat), sub);
    }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (bays[(size_t) bay].effect), [this, bay] (int r)
    {
        if (r <= 0) return;
        const int type = r == 1000 ? 0 : r - 1;
        set (sfx::SpectralFx::pSlotType (bay), type);
        if (type > 0 && ! fx().on()) set (sfx::SpectralFx::pOn, 1.0);    // picking an effect turns the rack on
        if (type > 0 && fx().slotMix (bay) < 1.0f) set (sfx::SpectralFx::pSlotMix (bay), 100.0);
        rebuild(); sync();
    });
}

void FxPanel::rebuild()
{
    const auto& types = sfx::types();
    for (int b = 0; b < sfx::kSlots; ++b)
    {
        auto& B = bays[(size_t) b];
        const int t = juce::jlimit (0, (int) types.size() - 1, fx().slotType (b));
        if (t == shownType[(size_t) b]) continue;
        shownType[(size_t) b] = t;
        B.knobs.clear(); B.choices.clear();
        const auto& d = types[(size_t) t];
        B.effect.setButtonText (t == 0 ? juce::String ("EMPTY") : juce::String (d.name).toUpperCase());
        const int count = juce::jmin (8, (int) d.params.size());   // (a bay shows the effect's first 8 controls, the ones the host can automate)
        for (int k = 0; k < count; ++k)
        {
            const auto pd = d.params[(size_t) k];
            auto* kn = B.knobs.add (new Knob (juce::String (pd.name).toUpperCase().substring (0, 12)));
            addAndMakeVisible (kn);
            kn->s.setRange (pd.min, pd.max, pd.stepped() ? juce::jmax (1.0f, pd.step) : 0.0);
            if (pd.logScale && pd.min > 0) kn->s.setSkewFactorFromMidPoint (std::sqrt (pd.min * pd.max));
            kn->text = [pd] (double v) { return juce::String (pd.text ((float) v)); };
            kn->s.onValueChange = [this, b, k, kn, pd]
            {
                if (syncing) return;
                turn (sfx::SpectralFx::pSlotParam (b, k), pd.toNorm ((float) kn->s.getValue()));
            };
        }
    }
    resized();
}

void FxPanel::sync()
{
    syncing = true;
    auto& F = fx();
    onBtn.setToggleState (F.on(), juce::dontSendNotification);
    warpOn.setToggleState (F.warpOn(), juce::dontSendNotification);
    warpOn.setEnabled (sfx::warpAvailable());
    order.setButtonText (F.order() == 0 ? "BAYS > WARP" : "WARP > BAYS");
    fftBtn.setButtonText ("FFT " + juce::String (sfx::fftSizeAt (F.fftIndex())));
    if (! mix.s.isMouseButtonDown()) mix.s.setValue (F.mix(), juce::dontSendNotification);
    if (! chainMix.s.isMouseButtonDown()) chainMix.s.setValue (F.chainMix(), juce::dontSendNotification);
    bool anySpectral = false;
    for (int b = 0; b < sfx::kSlots; ++b)
    {
        auto& B = bays[(size_t) b];
        const int t = F.slotType (b);
        const bool has = t > 0;
        if (has && sfx::types()[(size_t) t].domain != sfx::Domain::Time) anySpectral = true;
        B.on.setToggleState (F.slotOn (b), juce::dontSendNotification);
        B.on.setEnabled (has);            // (greyed when the bay is empty: nothing to switch)
        B.mix.setEnabled (has);
        if (! B.mix.s.isMouseButtonDown()) B.mix.s.setValue (F.slotMix (b), juce::dontSendNotification);
        for (int k = 0; k < B.knobs.size(); ++k)
        {
            if (! B.knobs[k]->s.isMouseButtonDown()) B.knobs[k]->s.setValue (F.slotValue (b, k), juce::dontSendNotification);
            B.knobs[k]->setEnabled (F.slotOn (b));
        }
    }
    fftBtn.setEnabled (anySpectral);      // (the window only matters to spectral effects)
    chainMix.setEnabled (anySpectral || F.slotType (0) || F.slotType (1) || F.slotType (2) || F.slotType (3));
    for (int k = 0; k < warpKnobs.size(); ++k)
    {
        if (! warpKnobs[k]->s.isMouseButtonDown()) warpKnobs[k]->s.setValue (F.warpValue (k), juce::dontSendNotification);
        warpKnobs[k]->setEnabled (F.warpOn());
    }
    syncing = false;
    lastChange = F.changeCount();
    repaint();
}

void FxPanel::timerCallback()
{
    if (fx().changeCount() == lastChange) return;   // (only changes from elsewhere: automation, a loaded song)
    bool typeChanged = false;
    for (int b = 0; b < sfx::kSlots; ++b) typeChanged = typeChanged || fx().slotType (b) != shownType[(size_t) b];
    if (typeChanged) rebuild();
    sync();
}

void FxPanel::paint (juce::Graphics& g)
{
    look::plate (g, getLocalBounds().toFloat(), 6.0f);
    look::engraved (g, title, { 18, 10, 400, 26 }, look::wordmark (20.0f), juce::Justification::left);
    look::engraved (g, "POLTERGEIST 2 SPECTRAL RACK:  4 BAYS  +  WARP", { 18, 34, 500, 16 }, look::label (11.0f), juce::Justification::left, look::inkSoft);
    const int bw = (getWidth() - 36 - 3 * 10) / 4;
    for (int b = 0; b < sfx::kSlots; ++b)
    {
        juce::Rectangle<float> r ((float) (18 + b * (bw + 10)), 66.0f, (float) bw, 330.0f);
        g.setColour (juce::Colours::black.withAlpha (0.10f)); g.fillRoundedRectangle (r, 6.0f);
        g.setColour (juce::Colours::black.withAlpha (0.25f)); g.drawRoundedRectangle (r, 6.0f, 1.0f);
        look::engraved (g, "BAY " + juce::String (b + 1), r.toNearestInt().removeFromTop (22).reduced (8, 0), look::label (12.0f), juce::Justification::left);
        look::led (g, { r.getRight() - 12.0f, r.getY() + 11.0f }, fx().on() && fx().slotOn (b) && fx().slotType (b) > 0);
    }
    juce::Rectangle<float> w (18.0f, 408.0f, (float) getWidth() - 36.0f, (float) getHeight() - 420.0f);
    g.setColour (juce::Colours::black.withAlpha (0.10f)); g.fillRoundedRectangle (w, 6.0f);
    g.setColour (juce::Colours::black.withAlpha (0.25f)); g.drawRoundedRectangle (w, 6.0f, 1.0f);
    look::engraved (g, "WARP", w.toNearestInt().removeFromTop (24).reduced (10, 0), look::label (13.0f), juce::Justification::left);
    if (! sfx::warpAvailable()) look::engraved (g, "(not on this computer's processor)", w.toNearestInt().removeFromTop (24).reduced (70, 0), look::label (11.0f), juce::Justification::left, look::inkSoft);
}

void FxPanel::resized()
{
    auto top = getLocalBounds().reduced (18, 12).removeFromTop (44);
    close.setVisible (onClose != nullptr);   // (GHOST FX has no CLOSE: its OPEN sits there)
    close.setBounds (top.removeFromRight (80).withHeight (28));
    top.removeFromRight (10);
    onBtn.setBounds (top.removeFromRight (80).withHeight (28));
    top.removeFromRight (10);
    mix.setBounds (top.removeFromRight (50).withHeight (50));
    top.removeFromRight (6);
    chainMix.setBounds (top.removeFromRight (56).withHeight (50));
    top.removeFromRight (10);
    order.setBounds (top.removeFromRight (110).withHeight (28));
    top.removeFromRight (8);
    fftBtn.setBounds (top.removeFromRight (90).withHeight (28));
    const int bw = (getWidth() - 36 - 3 * 10) / 4;
    for (int b = 0; b < sfx::kSlots; ++b)
    {
        auto& B = bays[(size_t) b];
        juce::Rectangle<int> r (18 + b * (bw + 10), 66, bw, 330);
        r = r.reduced (8); r.removeFromTop (18);
        B.effect.setBounds (r.removeFromTop (30));
        r.removeFromTop (6);
        auto row = r.removeFromTop (56);
        B.on.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (6, 14));
        B.mix.setBounds (row);
        r.removeFromTop (6);
        const int kw = r.getWidth() / 4, kh = 58;
        for (int k = 0; k < B.knobs.size(); ++k)
            B.knobs[k]->setBounds (r.getX() + (k % 4) * kw, r.getY() + (k / 4) * (kh + 4), kw, kh);
    }
    auto w = juce::Rectangle<int> (18, 408, getWidth() - 36, getHeight() - 420).reduced (10, 6);
    auto wt = w.removeFromTop (22);
    warpOn.setBounds (wt.removeFromRight (100));
    w.removeFromTop (4);
    const int kw = juce::jmin (90, w.getWidth() / juce::jmax (1, warpKnobs.size()));
    for (int k = 0; k < warpKnobs.size(); ++k) warpKnobs[k]->setBounds (w.getX() + k * kw, w.getY(), kw, juce::jmin (70, w.getHeight()));
}

} // namespace ghost
