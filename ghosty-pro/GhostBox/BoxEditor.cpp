#include "BoxEditor.h"
#include "BoxCapturePage.h"
#include "BoxDnaPage.h"
#include "BoxSplitPage.h"
#include "BoxFocusPage.h"
#include "BoxPrintPage.h"
#include "BoxSpacePage.h"

std::unique_ptr<juce::Component> makeCapturePage (BoxProcessor& p) { return std::make_unique<BoxCapturePage> (p); }
std::unique_ptr<juce::Component> makeDnaPage (BoxProcessor& p) { return std::make_unique<BoxDnaPage> (p); }
std::unique_ptr<juce::Component> makeSplitPage (BoxProcessor& p) { return std::make_unique<BoxSplitPage> (p); }
std::unique_ptr<juce::Component> makeFocusPage (BoxProcessor& p) { return std::make_unique<BoxFocusPage> (p); }
std::unique_ptr<juce::Component> makePrintPage (BoxProcessor& p) { return std::make_unique<BoxPrintPage> (p); }
std::unique_ptr<juce::Component> makeSpacePage (BoxProcessor& p) { return std::make_unique<BoxSpacePage> (p); }

using namespace gpfx::ui;

BoxEditor::BoxEditor (BoxProcessor& p)
    : AudioProcessorEditor (p), proc (p),
      inK (p.apvts, "box_in", "INPUT", col::ice), outK (p.apvts, "box_out", "OUTPUT", col::slime),
      mixK (p.apvts, "box_mix", "DRY/WET", col::pink),
      panel (p.apvts, p.rack)
{
    setLookAndFeel (&lnf);
    for (auto* k : { &inK, &outK, &mixK })
    {
        k->slider.setSliderStyle (juce::Slider::LinearHorizontal);
        k->slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 54, 15);
        k->caption = k == &mixK ? "MIX" : (k == &inK ? "IN" : "OUT");
        k->resized();
        addAndMakeVisible (*k);
    }
    bypassButton.setDark (true);
    addAndMakeVisible (bypassButton);
    abBtn.setButtonText ("A"); abBtn.setColour (juce::TextButton::buttonOnColourId, col::ice); abBtn.setClickingTogglesState (false);
    abBtn.setTooltip ("A/B compare of the WHOLE box: click to switch between two complete states (the first click makes B a copy of A). UNDO goes back.");
    abBtn.onClick = [this] { proc.abToggle(); abBtn.setButtonText (proc.abCurrent() == 0 ? "A" : "B"); abBtn.setToggleState (proc.abCurrent() == 1, juce::dontSendNotification); repaint(); };
    abCopy.setButtonText ("A>B"); abCopy.setColour (juce::TextButton::buttonOnColourId, col::ice);
    abCopy.setTooltip ("Copy what you hear into the other slot (then tweak and switch to compare)");
    abCopy.onClick = [this] { proc.abCopyToOther(); abCopy.setButtonText (proc.abCurrent() == 0 ? "A>B" : "B>A"); };
    addAndMakeVisible (abBtn); addAndMakeVisible (abCopy);
    headerRightClick.ed = this; addMouseListener (&headerRightClick, true);
    addAndMakeVisible (snapBar);
    snapBar.onMessage = [this] (const juce::String& m) { toastText = m; toastUntil = t + 2.5; repaint (mixHint); };
    snapBar.onLoaded = [this] { repaint(); };
    proc.snapshots->onChanged = [this] { snapBar.refresh(); };
    setWantsKeyboardFocus (true);
    themeBtn.onClick = [this] { setLook (nextLook (shownLook)); };   // BLACK -> PURPLE -> LIGHT -> GHOST -> BLACK
    addAndMakeVisible (themeBtn);
    unlock.onChange = [this] { repaint(); };
    addAndMakeVisible (unlock);
    addAndMakeVisible (panel);
    scenes.onMessage = [this] (const juce::String& m) { toastText = m; toastUntil = t + 2.5; repaint (mixHint); };
    addAndMakeVisible (scenes);
    tempoSw.set (proc.tempoMul.load(), proc.hostBpm.load());
    tempoSw.onChange = [this] (int c)
    {
        proc.tempoMul.store (c);
        proc.updateHostDisplay (juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged (true));   // (the project has a change to save)
        tempoSw.set (c, proc.hostBpm.load());
        toastText = "TEMPO " + ghosty::tempomul::multName (c) + ": " + ghosty::tempomul::bpmText (proc.hostBpm.load() * ghosty::tempomul::factor (c)) + " bpm (locked to the DAW's bars)";
        toastUntil = t + 2.5; repaint (mixHint);
    };
    tempoSw.setTooltip (juce::String::fromUTF8 ("TEMPO: every tempo-synced effect (GATE, GRID, HAUNT, synced echoes and LFOs, PUMP, MOTION, scene landings, SPACE orbits) "
                                                "runs at the DAW's tempo (1x), half of it (\xc2\xbd" "x) or double it (2x), still locked to the DAW's bars. Saved with the project."));
    scenes.addAndMakeVisible (tempoSw); scenes.rightExtra = &tempoSw;   // (at the right end of the GHOST SCENES bar: the header has no room left)
    setResizable (true, true);
    // opens at the fitting size (kOpenW x kOpenH, Brandon 2026-11-06) whatever size the last window had; GHOST SCENES sits below the rack
    setResizeLimits (960, 700, 1900, 1200 + kSceneBar);
    setSize (kOpenW, kOpenH);
    setLook (proc.uiTheme.load(), true);
    startTimerHz (30);
}

void BoxEditor::setLook (int l, bool force)
{
    l = l == kLookGhost ? kLookGhost : ghosty::themes::toInt (ghosty::themes::fromInt (l));   // (unknown values: BLACK, as before)
    const auto t = l == kLookGhost ? ghosty::themes::Theme::Light : ghosty::themes::fromInt (l);
    proc.uiTheme.store (l);
    if (l == shownLook && ! force) return;
    shownTheme = t; shownLook = l;
    fxtheme::setLook (l);
    themeBtn.setButtonText (lookName (nextLook (l)));   // names the look a click switches to
    themeBtn.setTooltip ("Look: " + lookName (l) + ". Click for " + lookName (nextLook (l))
                         + " (BLACK -> PURPLE -> LIGHT -> GHOST, GHOST = liquid glass; saved with the project, not in presets)");
    lnf.applyTheme();
    fxtheme::retint (*this);
    panel.themeChanged();
    bypassButton.setTheme (t);
    snapBar.setTheme (t);
    tempoSw.setTheme (t);
    scenes.refresh();
    sendLookAndFeelChange();
    repaint();
}

BoxEditor::~BoxEditor() { proc.snapshots->onChanged = nullptr; removeMouseListener (&headerRightClick); stopTimer(); setLookAndFeel (nullptr); }

juce::String BoxEditor::versionString()
{
#ifdef JucePlugin_VersionString
    return "v1 beta";
#else
    return "v(dev)";   // (test builds have no plugin version)
#endif
}

void BoxEditor::drawLogo (juce::Graphics& g, juce::Rectangle<float> r, double t)
{
    // a cute ghost peeking out of a glowing box
    const float s = r.getHeight();
    auto box = juce::Rectangle<float> (r.getX() + 4, r.getY() + s * 0.48f, s * 1.05f, s * 0.48f);
    const float bob = (float) std::sin (t * 2.2) * s * 0.03f;
    auto L = ghosty::lookFor (0); L.eyes = 3; L.mouth = 1;
    ghosty::drawGhost (g, L, box.getCentreX(), r.getY() + s * 0.40f + bob, s * 0.72f, 0, false, 1.0f);
    // box front (covers the ghost's tail), with flaps
    juce::ColourGradient bg (juce::Colour (0xff3a2a5c), box.getX(), box.getY(), juce::Colour (0xff1a1430), box.getX(), box.getBottom(), false);   // the box is artwork: the same purple in both themes
    g.setGradientFill (bg); g.fillRoundedRectangle (box, 4);
    g.setColour (col::slime.withAlpha (0.85f)); g.drawRoundedRectangle (box, 4, 1.6f);
    juce::Path flaps;
    flaps.startNewSubPath (box.getX(), box.getY()); flaps.lineTo (box.getX() - s * 0.12f, box.getY() - s * 0.12f);
    flaps.startNewSubPath (box.getRight(), box.getY()); flaps.lineTo (box.getRight() + s * 0.12f, box.getY() - s * 0.12f);
    g.setColour (col::slime.withAlpha (0.7f)); g.strokePath (flaps, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (col::pink.withAlpha (0.7f)); g.fillEllipse (box.getCentreX() - 2.5f, box.getCentreY() - 2.5f, 5, 5);
}

void BoxEditor::drawMeter (juce::Graphics& g, juce::Rectangle<float> r, const float* lv, const juce::String& label)
{
    juce::ignoreUnused (label);
    for (int c = 0; c < 2; ++c)
    {
        auto b = r.removeFromTop (r.getHeight() * (c == 0 ? 0.5f : 1.0f)).reduced (0, 1);
        g.setColour (T (juce::Colour (0xff12141c))); g.fillRoundedRectangle (b, 2);
        const float db = gpfx::gainToDb (lv[c]);
        const float f = gpfx::clampf ((db + 60.0f) / 60.0f, 0.0f, 1.0f);
        const auto c1 = db > -1.0f ? col::pink : (db > -9.0f ? col::gold : col::slime);
        g.setColour (c1.withAlpha (0.9f)); g.fillRoundedRectangle (b.withWidth (b.getWidth() * f), 2);
    }
}

void BoxEditor::paint (juce::Graphics& g)
{
    fxtheme::setLook (shownLook);   // this window's theme for the paint pass (other plugin windows may use another one)
    if (shownLook == kLookGhost) { paintGhost (g); return; }   // (the panel paints after: its look is set again in Panel::paint)
    g.fillAll (col::bg);
    auto h = header.toFloat();
    juce::ColourGradient bg (T (juce::Colour (0xff141726)), h.getX(), h.getY(), T (juce::Colour (0xff0d0f17)), h.getX(), h.getBottom(), false);
    g.setGradientFill (bg); g.fillRect (h);
    g.setColour (col::slime.withAlpha (0.15f)); g.drawHorizontalLine (header.getBottom() - 1, h.getX(), h.getRight());
    auto lg = logo.toFloat();
    drawLogo (g, lg.removeFromLeft (lg.getHeight() * 1.2f), t);
    const juce::String title = "GHOST BOX";
    auto tr = lg.withTrimmedLeft (8).withHeight (30).translated (0, 6);
    g.setFont (ghosty::font (24.0f));
    g.setColour (col::ice.withAlpha (0.35f)); g.drawText (title, tr.translated (-1.5f, 0), juce::Justification::centredLeft);
    g.setColour (col::pink.withAlpha (0.30f)); g.drawText (title, tr.translated (1.5f, 0), juce::Justification::centredLeft);
    g.setColour (col::white); g.drawText (title, tr, juce::Justification::centredLeft);
    drawCaption (g, tr.translated (0, 24).withHeight (14), (proc.license.isUnlocked() || ! ghosty::License::required() ? "GhostBlock fun box  " : "DEMO - drops out every 20 s  ") + versionString(),
                 proc.license.isUnlocked() || ! ghosty::License::required() ? col::dim : col::gold, 10.0f);
    drawMeter (g, inMeter.toFloat(), inLv, "IN");
    drawMeter (g, outMeter.toFloat(), outLv, "OUT");
    if (t < toastUntil && toastText.isNotEmpty()) drawCaption (g, mixHint.toFloat(), toastText, col::slime, 9.0f);   // "Snapshot saved: Snap 3" ...
    else drawCaption (g, mixHint.toFloat(), bypassed() ? "BYPASSED: dry signal only" : "dry  <->  wet", bypassed() ? col::gold : col::dim, 9.0f);
}

// GHOST: the aurora (cached per size), the header as a cached glass panel, the meters in recessed glass wells, deep indigo texts and the
// logo outlined so the ghost reads on the glass. The rack panel below paints no flat fill in GHOST: the same aurora shows through it.
void BoxEditor::paintGhostSurfaces (juce::Graphics& g)
{
    // the aurora over the window as it was before the GHOST SCENES bar (the rack's glass reads exactly as before); the strip below it
    // continues the aurora's bottom edge
    const int hTop = auroraHeight();
    backdrop.draw (g, getWidth(), hTop);
    if (hTop < getHeight())
    {
        juce::Graphics::ScopedSaveState ss (g);
        const float sc = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        const auto& img = backdrop.get (getWidth(), hTop, sc);
        g.drawImage (img, 0, hTop, getWidth(), getHeight() - hTop, 0, juce::jmax (0, img.getHeight() - 2), img.getWidth(), 2);
    }
    headerGlass.draw (g, header.toFloat().reduced (6.0f, 4.0f).withTrimmedBottom (2.0f), 16.0f, 0.40f, 6.0f);
    for (auto m : { inMeter, outMeter }) ghosty::glass::recessed (g, m.toFloat().expanded (2.0f, 1.0f), 4.0f);
    if (scenes.isVisible())
    {   // GHOST SCENES bar glass (its own graphics state: the panel's glass is composited after this in the same pass)
        juce::Graphics::ScopedSaveState ss (g);
        scenes.paintGhostSurface (g, scenes.getBounds().toFloat().reduced (8.0f, 3.0f));
    }
}
std::vector<BoxEditor::GhostText> BoxEditor::ghostHeaderTexts() const
{
    auto lg = logo.toFloat(); lg.removeFromLeft (lg.getHeight() * 1.2f);
    const auto tr = lg.withTrimmedLeft (8).withHeight (30).translated (0, 6);
    const bool full = proc.license.isUnlocked() || ! ghosty::License::required();
    const auto cap = (full ? "GhostBlock fun box  " : "DEMO - drops out every 20 s  ") + versionString();
    const auto tw = juce::roundToInt (ghosty::textWidth (ghosty::font (24.0f), "GHOST BOX")), cw = juce::roundToInt (ghosty::textWidth (ghosty::font (10.0f), cap));
    return { { tr.toNearestInt().withWidth (tw).reduced (0, 7), ghosty::glass::ink(), "title" },
             { tr.translated (0, 24).withHeight (14).toNearestInt().withWidth (cw).reduced (0, 3), full ? ghosty::glass::ink2() : col::gold.get(), "version" },
             { mixHint.reduced (0, 2), ghosty::glass::ink2(), "dry/wet hint" } };
}
void BoxEditor::drawGhostStatic (juce::Graphics& g)
{
    const float sc = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const auto key = juce::String (getWidth()) + "x" + juce::String (getHeight()) + "@" + juce::String (sc, 3) + "|" + header.toString() + "|" + panel.getBounds().toString() + "|" + panel.ghostStaticKey() + "|" + scenes.getBounds().toString();
    if (ghostStatic.isNull() || key != ghostStaticKey)
    {
        ghostStatic = juce::Image (juce::Image::RGB, juce::jmax (1, juce::roundToInt ((float) getWidth() * sc)), juce::jmax (1, juce::roundToInt ((float) getHeight() * sc)), false, juce::SoftwareImageType());
        juce::Graphics ig (ghostStatic); ig.addTransform (juce::AffineTransform::scale (sc));
        paintGhostSurfaces (ig);
        ig.setOrigin (panel.getPosition()); panel.paintGhostSurfaces (ig);
        ghostStaticKey = key; ++ghostStaticBuilds;
    }
    if (sc == 1.0f) g.drawImageAt (ghostStatic, 0, 0);
    else g.drawImageTransformed (ghostStatic, juce::AffineTransform::scale (1.0f / sc));
}
void BoxEditor::paintGhost (juce::Graphics& g)
{
    drawGhostStatic (g);
    namespace gg = ghosty::glass;
    auto lg = logo.toFloat();
    auto lr = lg.removeFromLeft (lg.getHeight() * 1.2f);
    drawLogo (g, lr, t);
    {   // the logo ghost outlined (its body sits on the glass)
        const float s = lr.getHeight(); auto box = juce::Rectangle<float> (lr.getX() + 4, lr.getY() + s * 0.48f, s * 1.05f, s * 0.48f);
        const float bob = (float) std::sin (t * 2.2) * s * 0.03f;
        juce::Graphics::ScopedSaveState ss (g); g.excludeClipRegion (box.toNearestInt());
        gpfx::ui::ghostglass::outline (g, box.getCentreX(), lr.getY() + s * 0.40f + bob, s * 0.72f);
    }
    const juce::String title = "GHOST BOX";
    auto tr = lg.withTrimmedLeft (8).withHeight (30).translated (0, 6);
    g.setFont (ghosty::font (24.0f));
    g.setColour (gg::violet().withAlpha (0.30f)); g.drawText (title, tr.translated (1.2f, 0.8f), juce::Justification::centredLeft);
    g.setColour (gg::ink()); g.drawText (title, tr, juce::Justification::centredLeft);
    const bool full = proc.license.isUnlocked() || ! ghosty::License::required();
    drawCaption (g, tr.translated (0, 24).withHeight (14), (full ? "GhostBlock fun box  " : "DEMO - drops out every 20 s  ") + versionString(), full ? gg::ink2() : col::gold.get(), 10.0f);
    for (int k = 0; k < 2; ++k)
    {   // the meters inside their wells
        auto r = (k == 0 ? inMeter : outMeter).toFloat(); const float* lv = k == 0 ? inLv : outLv;
        for (int c = 0; c < 2; ++c)
        {
            auto b = r.removeFromTop (r.getHeight() * (c == 0 ? 0.5f : 1.0f)).reduced (1, 1);
            const float db = gpfx::gainToDb (lv[c]);
            const float f = gpfx::clampf ((db + 60.0f) / 60.0f, 0.0f, 1.0f);
            if (f <= 0.0f) continue;
            if (db > -1.0f) g.setColour (gg::pink());
            else g.setGradientFill (juce::ColourGradient (gg::teal(), b.getX(), 0, db > -9.0f ? col::gold.get() : gg::violet(), b.getX() + b.getWidth(), 0, false));
            g.fillRoundedRectangle (b.withWidth (b.getWidth() * f), 2);
        }
    }
    if (t < toastUntil && toastText.isNotEmpty()) drawCaption (g, mixHint.toFloat(), toastText, col::slime, 9.0f);
    else drawCaption (g, mixHint.toFloat(), bypassed() ? "BYPASSED: dry signal only" : "dry  <->  wet", bypassed() ? col::gold.get() : gg::ink2(), 9.0f);
}

void BoxEditor::resized()
{
    auto r = getLocalBounds();
    header = r.removeFromTop (kHeader);
    auto h = header.reduced (12, 8);
    logo = h.removeFromLeft (250);
    bypassButton.setBounds (h.removeFromRight (84).withSizeKeepingCentre (84, 26));   // top right, shared GhostBlock BYPASS
    h.removeFromRight (8);
    themeBtn.setBounds (h.removeFromRight (60).withSizeKeepingCentre (60, 26));
    h.removeFromRight (10);
    unlock.setBounds (h.removeFromRight (116).reduced (0, 7));
    h.removeFromRight (10);
    abCopy.setBounds (h.removeFromRight (46).reduced (0, 9)); h.removeFromRight (4);
    abBtn.setBounds (h.removeFromRight (36).reduced (0, 9));
    h.removeFromRight (8);
    snapBar.setBounds (h.removeFromRight (108).withSizeKeepingCentre (108, 30));
    h.removeFromRight (12);
    const int unit = h.getWidth() / 3;
    auto a = h.removeFromLeft (unit), b = h.removeFromLeft (unit), c = h;
    inK.setBounds (a.removeFromTop (a.getHeight() / 2 + 6).reduced (4, 1));
    inMeter = a.withTrimmedLeft (44).reduced (4, 2);
    outK.setBounds (b.removeFromTop (b.getHeight() / 2 + 6).reduced (4, 1));
    outMeter = b.withTrimmedLeft (44).reduced (4, 2);
    mixK.setBounds (c.removeFromTop (c.getHeight() / 2 + 6).reduced (4, 1));
    mixHint = c.withTrimmedLeft (44).reduced (4, 0);
    scenes.setBounds (r.removeFromBottom (kSceneBar));   // GHOST SCENES: the strip below the rack
    panel.setBounds (r);
}

void BoxEditor::timerCallback()
{
    t += 1.0 / 30.0;
    for (int c = 0; c < 2; ++c)
    {
        inLv[c] = juce::jmax (proc.inPeak[c].load(), inLv[c] * 0.86f);
        outLv[c] = juce::jmax (proc.outPeak[c].load(), outLv[c] * 0.86f);
    }
    repaint (inMeter.expanded (4, 4)); repaint (outMeter.expanded (4, 4)); repaint (mixHint); repaint (logo);
    if (proc.uiTheme.load() != shownLook) setLook (proc.uiTheme.load());   // a project loaded while the window is open
    tempoSw.set (proc.tempoMul.load(), proc.hostBpm.load());                 // (a project loaded, or the DAW's tempo changed: each choice shows its BPM)
    {   // the A/B switch follows the processor (A/B can also change through undo or a test / host call)
        const bool onB = proc.abCurrent() == 1;
        if (abBtn.getToggleState() != onB || abBtn.getButtonText() != (onB ? "B" : "A"))
        { abBtn.setButtonText (onB ? "B" : "A"); abBtn.setToggleState (onB, juce::dontSendNotification); abCopy.setButtonText (onB ? "B>A" : "A>B"); }
    }
}
