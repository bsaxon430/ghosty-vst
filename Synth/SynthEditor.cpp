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

// ------------------------------------------------------------------ editor
SynthEditor::SynthEditor (SynthProcessor& p) : AudioProcessorEditor (&p), proc (p), wave (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (wave); addAndMakeVisible (pad);
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
        { "unison", "UNISON" }, { "fenv", "FILTER ENV" }, { "chorus", "CHORUS" }, { "phaser", "PHASER" }, { "crush", "CRUSH" }, { "haunt", "HAUNT" }, { "gain", "VOLUME" } };
    for (auto& k : ks) addAndMakeVisible (knobs.add (new GhostKnob (proc.apvts, k.first, k.second)));

    for (int i = 0; i < 9; ++i)
    {
        auto& r = juce::Random::getSystemRandom();
        bg.push_back ({ lookFor ((i * 4 + 1) % 35), r.nextFloat() * 6.28f, 0.25f + r.nextFloat() * 0.4f, (0.04f + r.nextFloat() * 0.08f) * (r.nextBool() ? 1.0f : -1.0f), 22 + r.nextFloat() * 26, 0.12f + r.nextFloat() * 0.12f });
    }
    setSize (1000, 660);
    startTimerHz (24);
}

SynthEditor::~SynthEditor() { setLookAndFeel (nullptr); }

void SynthEditor::refreshPresetBox()
{
    presetBox.clear (juce::dontSendNotification);
    for (int i = 0; i < proc.getNumPrograms(); ++i) presetBox.addItem (proc.getProgramName (i), i + 1);
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
    g.drawText ("WAVE  ·  draw on it", juce::Rectangle<float> (20, 66, 200, 16), juce::Justification::centredLeft);
    g.drawText ("GHOSTS  ·  drag & hold  ·  double-click to swap", juce::Rectangle<float> (600, 66, 380, 16), juce::Justification::centredLeft);
    // card behind knobs
    g.setColour (col::card.withAlpha (0.9f)); g.fillRoundedRectangle (juce::Rectangle<float> (14, 446, 572, 200), 14);
    g.setColour (col::ecto.withAlpha (0.2f)); g.drawRoundedRectangle (juce::Rectangle<float> (14, 446, 572, 200), 14, 1.0f);
    g.setColour (col::dim); g.drawText ("Tip: play notes from Ableton's MIDI clip or your keyboard.", juce::Rectangle<float> (600, 600, 380, 40), juce::Justification::centredLeft);
}

void SynthEditor::resized()
{
    presetBox.setBounds (330, 14, 300, 34);
    prevBtn.setBounds (640, 14, 40, 34); nextBtn.setBounds (686, 14, 40, 34);
    waveABox.setBounds (20, 88, 130, 28); waveBBox.setBounds (156, 88, 130, 28);
    editA.setBounds (300, 88, 80, 28); editB.setBounds (386, 88, 80, 28); diceBtn.setBounds (472, 88, 108, 28);
    wave.setBounds (20, 124, 560, 312);
    pad.setBounds (600, 88, 380, 500);
    for (int i = 0; i < knobs.size(); ++i)
    {
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
