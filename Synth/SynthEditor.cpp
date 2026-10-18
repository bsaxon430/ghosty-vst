#include "SynthEditor.h"
using namespace ghosty;

// ------------------------------------------------------------------ wave screen
void WaveScreen::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff06070a)); g.fillRoundedRectangle (r, 10);
    const float pad = 10, dX = r.getWidth() * 0.13f, dY = r.getHeight() * 0.26f;
    front = { pad, pad + dY, r.getWidth() - 2 * pad - dX, r.getHeight() - 2 * pad - dY };

    g.setColour (col::ecto.withAlpha (0.09f));
    for (int i = 0; i <= 8; ++i) { const float x = front.getX() + i / 8.0f * front.getWidth(); g.drawLine (x, front.getY(), x, front.getBottom()); }
    for (int i = 0; i <= 4; ++i) { const float y = front.getY() + i / 4.0f * front.getHeight(); g.drawLine (front.getX(), y, front.getRight(), y); }

    const auto& fa = editB ? proc.frameB : proc.frameA;
    const auto& fb = editB ? proc.frameA : proc.frameB;
    const float morph = proc.morph->load(), mt = editB ? 1.0f - morph : morph;
    auto pathFor = [&] (float t, float ox, float oy)
    {
        juce::Path p;
        for (int i = 0; i <= FRAME; i += 2)
        {
            const float v = fa[(size_t) (i % FRAME)] + (fb[(size_t) (i % FRAME)] - fa[(size_t) (i % FRAME)]) * t;
            const float x = front.getX() + ox + (float) i / FRAME * front.getWidth(), y = front.getCentreY() + oy - v * front.getHeight() * 0.44f;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        return p;
    };
    for (int k = 10; k >= 1; --k)
    {
        const float t = k / 10.0f;
        g.setColour (col::slime.interpolatedWith (col::ecto, t).withAlpha (0.1f + (1 - t) * 0.2f));
        g.strokePath (pathFor (t, t * dX, -t * dY), juce::PathStrokeType (1.0f));
    }
    const float lvl = proc.level.load();
    auto mp = pathFor (mt, mt * dX, -mt * dY);
    juce::ColourGradient gr (col::slime, front.getX(), 0, col::ecto, front.getRight(), 0, false); gr.addColour (0.5, col::ice);
    for (int k = 3; k >= 1; --k) { g.setColour (col::slime.withAlpha (0.08f)); g.strokePath (mp, juce::PathStrokeType (2.6f + k * 4.0f)); }
    g.setGradientFill (gr); g.strokePath (mp, juce::PathStrokeType (2.6f + lvl * 3.0f));

    auto fp = pathFor (0, 0, 0);
    auto fill = fp; fill.lineTo (front.getRight(), front.getCentreY()); fill.lineTo (front.getX(), front.getCentreY()); fill.closeSubPath();
    juce::ColourGradient fg (col::slime.withAlpha (0.26f), 0, front.getY(), col::ecto.withAlpha (0.3f), 0, front.getBottom(), false);
    g.setGradientFill (fg); g.fillPath (fill);
    if (mt > 0.02f) { g.setColour (col::white.withAlpha (0.85f)); g.strokePath (fp, juce::PathStrokeType (1.6f)); }

    g.setColour (col::white.withAlpha (0.5f)); g.setFont (font (11.0f));
    g.drawText (editB ? "B" : "A", juce::Rectangle<float> (front.getX() + 6, front.getY() + 4, 20, 14), juce::Justification::centredLeft);
    g.drawText (editB ? "A" : "B", juce::Rectangle<float> (front.getRight() + dX - 22, front.getY() - dY + 2, 20, 14), juce::Justification::centredLeft);
    if (glitch > 0.05f) { g.setColour (col::slime.withAlpha (glitch * 0.15f)); g.fillRect (r.withHeight (6 + glitch * 20).withY (juce::Random::getSystemRandom().nextFloat() * r.getHeight())); }
}

void WaveScreen::paintAt (juce::Point<float> p)
{
    auto& f = editB ? proc.frameB : proc.frameA;
    const int i = juce::jlimit (0, FRAME - 1, (int) std::round ((p.x - front.getX()) / front.getWidth() * (FRAME - 1)));
    const float v = juce::jlimit (-1.0f, 1.0f, -(p.y - front.getCentreY()) / (front.getHeight() * 0.44f));
    if (last < 0) f[(size_t) i] = v;
    else
    {
        const int a = juce::jmin (last, i), b = juce::jmax (last, i);
        const float va = last < i ? f[(size_t) last] : v, vb = last < i ? v : f[(size_t) last];
        for (int k = a; k <= b; ++k) f[(size_t) k] = (b == a) ? v : va + (vb - va) * (float) (k - a) / (float) (b - a);
    }
    last = i; dirty = true; glitch = 0.6f; repaint();
}

void WaveScreen::mouseUp (const juce::MouseEvent&)
{
    auto& f = editB ? proc.frameB : proc.frameA;
    auto c = f;
    for (int i = 0; i < FRAME; ++i) f[(size_t) i] = (c[(size_t) ((i + FRAME - 1) % FRAME)] + 2 * c[(size_t) i] + c[(size_t) ((i + 1) % FRAME)]) * 0.25f;
    proc.rebuildWaves(); dirty = false; repaint();
}

void WaveScreen::randomise()
{
    auto& r = juce::Random::getSystemRandom();
    float amps[24]; double ph[24];
    for (int k = 0; k < 24; ++k) { amps[k] = std::pow (r.nextFloat(), 2.2f) / std::pow ((float) (k + 1), 0.55f) * (k == 0 ? 2.0f : 1.0f); ph[k] = r.nextDouble() * juce::MathConstants<double>::twoPi; }
    auto& f = editB ? proc.frameB : proc.frameA;
    for (int i = 0; i < FRAME; ++i)
    {
        double v = 0; for (int k = 0; k < 24; ++k) v += amps[k] * std::sin (juce::MathConstants<double>::twoPi * (k + 1) * i / FRAME + ph[k]);
        f[(size_t) i] = (float) v;
    }
    normaliseFrame (f); proc.rebuildWaves(); glitch = 1.0f; repaint();
}

void WaveScreen::timerCallback()
{
    if (dirty && ++sinceRebuild > 3) { proc.rebuildWaves(); dirty = false; sinceRebuild = 0; }
    glitch *= 0.88f;
    repaint();
}

// ------------------------------------------------------------------ flow strip
void FlowStrip::buildNotes()
{
    const int key = (int) proc.keyP->load(), scale = (int) proc.scaleP->load(), mask = scaleMask (scale);
    const int start = 48 + key, end = start + 36;
    count = 0;
    for (int n = start; n <= end && count < 64; ++n) if (inScale (n, key, mask)) notes[count++] = n;
}

juce::Colour FlowStrip::colourFor (int note) const
{
    const int key = (int) proc.keyP->load();
    const float deg = (float) (((note - key) % 12 + 12) % 12) / 12.0f;
    return juce::Colour::fromHSV (0.42f + deg * 0.38f, 0.85f, 1.0f, 1.0f);
}

void FlowStrip::play (juce::Point<float> p)
{
    buildNotes();
    if (count == 0) return;
    finger = p;
    const int idx = juce::jlimit (0, count - 1, (int) (p.x / (float) getWidth() * (float) count));
    const int note = notes[idx];
    if (note == current) return;
    release();
    const float vel = juce::jlimit (0.2f, 1.0f, 1.0f - p.y / (float) getHeight() + 0.2f);
    proc.keyboardState.noteOn (1, note, vel);
    current = note;
    flash[note] = 1.0f;
    ripples.push_back ({ p.x, p.y, 6.0f, 1.0f, colourFor (note) });
}

void FlowStrip::paint (juce::Graphics& g)
{
    buildNotes();
    auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff050608)); g.fillRoundedRectangle (b, 10);
    if (count == 0) return;
    const float cw = b.getWidth() / (float) count;
    const int key = (int) proc.keyP->load();
    for (int i = 0; i < count; ++i)
    {
        const int n = notes[i];
        const bool on = proc.keyboardState.isNoteOnForChannels (0xffff, n);
        if (on) flash[n] = juce::jmax (flash[n], 0.8f);
        const auto c = colourFor (n);
        juce::ColourGradient gr (c.withAlpha (0.05f + flash[n] * 0.55f), 0, 0, c.withAlpha (0.14f + flash[n] * 0.35f + proc.level.load() * 0.15f), 0, b.getHeight(), false);
        g.setGradientFill (gr); g.fillRect (i * cw + 1, 0.0f, cw - 2, b.getHeight());
        if (((n - key) % 12 + 12) % 12 == 0) { g.setColour (c.withAlpha (0.9f)); g.fillRect (i * cw + 1, 0.0f, cw - 2, 3.0f); }
        g.setColour (c.withAlpha (0.5f + flash[n] * 0.5f)); g.setFont (font (9.0f));
        g.drawText (keyNames()[n % 12], juce::Rectangle<float> (i * cw, b.getHeight() - 16, cw, 14), juce::Justification::centred);
    }
    for (auto& r : ripples) { g.setColour (r.c.withAlpha (r.a)); g.drawEllipse (r.x - r.r, r.y - r.r, r.r * 2, r.r * 2, 2.0f); }
    if (down && current >= 0)
    {
        const auto c = colourFor (current);
        juce::ColourGradient glow (c.withAlpha (0.8f), finger.x, finger.y, c.withAlpha (0.0f), finger.x + 60, finger.y, true);
        g.setGradientFill (glow); g.fillEllipse (finger.x - 60, finger.y - 60, 120, 120);
        drawGhost (g, lookFor ((current * 7) % 35), finger.x, finger.y - 4, 22, 0.4f + proc.level.load(), juce::Random::getSystemRandom().nextBool());
    }
    g.setColour (col::dim); g.setFont (font (9.0f));
    g.drawText ("FLOW  - slide to play  (higher = louder)", b.reduced (8, 4).removeFromTop (12), juce::Justification::centredLeft);
}

void FlowStrip::timerCallback()
{
    for (auto& f : flash) f *= 0.9f;
    for (auto& r : ripples) { r.r += 3.0f; r.a *= 0.9f; }
    ripples.erase (std::remove_if (ripples.begin(), ripples.end(), [] (const Ripple& r) { return r.a < 0.03f; }), ripples.end());
    repaint();
}

// ------------------------------------------------------------------ editor
SynthEditor::SynthEditor (SynthProcessor& p) : AudioProcessorEditor (&p), proc (p), wave (p), flow (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (wave); addAndMakeVisible (pad); addAndMakeVisible (flow);
    for (auto* c : { &keyBox, &scaleBox, &chordBox, &rateBox, &modeBox, &octBox }) addAndMakeVisible (c);
    addAndMakeVisible (arpBtn);
    keyBox.addItemList (keyNames(), 1); scaleBox.addItemList (scaleNames(), 1); chordBox.addItemList (chordNames(), 1);
    rateBox.addItemList (arpRateNames(), 1); modeBox.addItemList (arpModeNames(), 1); octBox.addItemList ({ "1 octave", "2 octaves", "3 octaves" }, 1);
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    keyAtt = std::make_unique<CA> (proc.apvts, "key", keyBox);       scaleAtt = std::make_unique<CA> (proc.apvts, "scale", scaleBox);
    chordAtt = std::make_unique<CA> (proc.apvts, "chord", chordBox); rateAtt = std::make_unique<CA> (proc.apvts, "arpRate", rateBox);
    modeAtt = std::make_unique<CA> (proc.apvts, "arpMode", modeBox); octAtt = std::make_unique<CA> (proc.apvts, "arpOct", octBox);
    arpBtn.setClickingTogglesState (true);
    arpAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "arp", arpBtn);
    for (auto* b : { &prevBtn, &nextBtn, &editA, &editB, &diceBtn }) addAndMakeVisible (b);
    addAndMakeVisible (presetBox); addAndMakeVisible (waveABox); addAndMakeVisible (waveBBox);

    waveABox.addItemList (shapeNames(), 1); waveBBox.addItemList (shapeNames(), 1);
    attA = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "waveA", waveABox);
    attB = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "waveB", waveBBox);

    refreshPresetBox();
    presetBox.onChange = [this] { const int i = presetBox.getSelectedItemIndex(); if (i >= 0 && i != proc.getCurrentProgram()) { proc.setCurrentProgram (i); wordGlitch = 1; } };
    prevBtn.onClick = [this] { const int n = proc.getNumPrograms(); proc.setCurrentProgram ((proc.getCurrentProgram() + n - 1) % n); refreshPresetBox(); wordGlitch = 1; };
    nextBtn.onClick = [this] { proc.setCurrentProgram ((proc.getCurrentProgram() + 1) % proc.getNumPrograms()); refreshPresetBox(); wordGlitch = 1; };
    editA.setClickingTogglesState (false); editB.setClickingTogglesState (false); editA.setToggleState (true, juce::dontSendNotification);
    editA.onClick = [this] { wave.editB = false; editA.setToggleState (true, juce::dontSendNotification); editB.setToggleState (false, juce::dontSendNotification); };
    editB.onClick = [this] { wave.editB = true; editB.setToggleState (true, juce::dontSendNotification); editA.setToggleState (false, juce::dontSendNotification); };
    diceBtn.onClick = [this] { wave.randomise(); wordGlitch = 1; };

    pad.addGhost ("TONE",  col::ice,  proc.apvts.getParameter ("cutoff"), proc.apvts.getParameter ("res"), 4);
    pad.addGhost ("MORPH", col::pink, proc.apvts.getParameter ("morph"),  proc.apvts.getParameter ("width"), 9);
    pad.addGhost ("SPACE", col::ecto, proc.apvts.getParameter ("echo"),   proc.apvts.getParameter ("verb"), 17);
    pad.addGhost ("HEAT",  col::gold, proc.apvts.getParameter ("drive"),  proc.apvts.getParameter ("sub"), 22);
    pad.levelSource = [this] { return proc.level.load(); };

    const std::pair<const char*, const char*> ks[] { { "attack", "ATTACK" }, { "decay", "DECAY" }, { "sustain", "SUSTAIN" }, { "release", "RELEASE" },
        { "unison", "UNISON" }, { "fenv", "FILTER ENV" }, { "chorus", "CHORUS" }, { "phaser", "PHASER" }, { "crush", "CRUSH" }, { "haunt", "HAUNT" }, { "gain", "VOLUME" },
        { "arpGate", "ARP GATE" } };
    for (auto& k : ks) addAndMakeVisible (knobs.add (new GhostKnob (proc.apvts, k.first, k.second)));

    for (int i = 0; i < 9; ++i)
    {
        auto& r = juce::Random::getSystemRandom();
        bg.push_back ({ lookFor ((i * 4 + 1) % 35), r.nextFloat() * 6.28f, 0.25f + r.nextFloat() * 0.4f, (0.04f + r.nextFloat() * 0.08f) * (r.nextBool() ? 1.0f : -1.0f), 22 + r.nextFloat() * 26, 0.12f + r.nextFloat() * 0.12f });
    }
    setSize (1000, 830);
    startTimerHz (24);
}

SynthEditor::~SynthEditor() { setLookAndFeel (nullptr); }

void SynthEditor::refreshPresetBox()
{
    presetBox.clear (juce::dontSendNotification);
    juce::String lastCat;
    for (int i = 0; i < proc.getNumPrograms(); ++i)
    {
        const auto cat = SynthProcessor::getPresetCategory (i);
        if (cat != lastCat) { presetBox.addSectionHeading (cat.toUpperCase()); lastCat = cat; }
        presetBox.addItem (proc.getProgramName (i), i + 1);
    }
    presetBox.setSelectedItemIndex (proc.getCurrentProgram(), juce::dontSendNotification);
}

void SynthEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    auto b = getLocalBounds().toFloat();
    juce::ColourGradient haze (col::ecto.withAlpha (0.18f), 0, 0, col::bg.withAlpha (0.0f), b.getWidth() * 0.5f, b.getHeight() * 0.4f, true);
    g.setGradientFill (haze); g.fillRect (b);
    const float lvl = proc.level.load();
    for (auto& q : bg)
    {
        const float rr = q.r * juce::jmin (b.getWidth(), b.getHeight());
        const float x = b.getCentreX() + std::cos (q.a) * rr * 1.3f, y = b.getHeight() * 0.5f + std::sin (q.a) * rr * 0.8f;
        drawGhost (g, q.look, x, y, q.sz, wordGlitch * 0.4f, false, q.al + lvl * 0.25f, std::cos (q.a + 1.57f) < 0);
    }
    drawWordmark (g, { 20, 10, 260, 40 }, "SYNTH", wordGlitch);
    g.setColour (col::dim); g.setFont (font (10.0f));
    g.drawText ("WAVE / draw on it", juce::Rectangle<float> (20, 66, 200, 16), juce::Justification::centredLeft);
    g.drawText ("GHOSTS / drag & hold / double-click to swap", juce::Rectangle<float> (600, 66, 380, 16), juce::Justification::centredLeft);
    // card behind knobs
    g.setColour (col::card.withAlpha (0.9f)); g.fillRoundedRectangle (juce::Rectangle<float> (14, 446, 572, 200), 14);
    g.setColour (col::ecto.withAlpha (0.2f)); g.drawRoundedRectangle (juce::Rectangle<float> (14, 446, 572, 200), 14, 1.0f);
    g.setColour (col::dim); g.drawText ("Play your MIDI keyboard, the piano roll, or slide across the FLOW strip.", juce::Rectangle<float> (600, 600, 380, 40), juce::Justification::centredLeft);
    g.setColour (col::card.withAlpha (0.9f)); g.fillRoundedRectangle (juce::Rectangle<float> (14, 652, 972, 168), 14);
    g.setColour (col::slime.withAlpha (0.25f)); g.drawRoundedRectangle (juce::Rectangle<float> (14, 652, 972, 168), 14, 1.0f);
    g.setColour (col::dim); g.setFont (font (10.0f));
    g.drawText ("PLAY", juce::Rectangle<float> (24, 664, 40, 30), juce::Justification::centredLeft);
}

void SynthEditor::resized()
{
    presetBox.setBounds (330, 14, 300, 34);
    prevBtn.setBounds (640, 14, 40, 34); nextBtn.setBounds (686, 14, 40, 34);
    waveABox.setBounds (20, 88, 130, 28); waveBBox.setBounds (156, 88, 130, 28);
    editA.setBounds (300, 88, 80, 28); editB.setBounds (386, 88, 80, 28); diceBtn.setBounds (472, 88, 108, 28);
    wave.setBounds (20, 124, 560, 312);
    pad.setBounds (600, 88, 380, 500);
    keyBox.setBounds (64, 664, 64, 30); scaleBox.setBounds (134, 664, 160, 30); chordBox.setBounds (300, 664, 140, 30);
    arpBtn.setBounds (460, 664, 64, 30); rateBox.setBounds (530, 664, 80, 30); modeBox.setBounds (616, 664, 110, 30); octBox.setBounds (732, 664, 104, 30);
    flow.setBounds (24, 702, 952, 108);
    for (int i = 0; i < knobs.size(); ++i)
    {
        if (i == 11) { knobs[i]->setBounds (880, 654, 90, 48); continue; }
        const int row = i < 6 ? 0 : 1, c = row == 0 ? i : i - 6, n = row == 0 ? 6 : 5;
        const int w = 560 / n;
        knobs[i]->setBounds (20 + c * w, 454 + row * 96, w, 90);
    }
}

void SynthEditor::timerCallback()
{
    phase += 1.0f / 24.0f;
    for (auto& q : bg) q.a += q.sp * (1.0f + proc.level.load() * 4.0f) * 0.04f;
    wordGlitch *= 0.85f;
    if (presetBox.getSelectedItemIndex() != proc.getCurrentProgram()) presetBox.setSelectedItemIndex (proc.getCurrentProgram(), juce::dontSendNotification);
    repaint();
}
