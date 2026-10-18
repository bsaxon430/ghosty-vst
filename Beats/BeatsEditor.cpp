#include "BeatsEditor.h"
using namespace ghosty;

namespace
{
const char* names[8] { "KICK", "SNARE", "CLAP", "HAT", "OPEN", "TOM", "PERC", "CRASH" };
const juce::Colour trackCol[8] { col::slime, col::pink, col::ecto, col::ice, col::gold, juce::Colour (0xffB6FF3C), juce::Colour (0xffFF785A), juce::Colour (0xffC8AAFF) };
}

// ------------------------------------------------------------------ grid
StepGrid::StepGrid (BeatsProcessor& p) : proc (p)
{
    const int idx[8] { 0, 4, 9, 13, 17, 22, 27, 31 };
    for (int t = 0; t < 8; ++t) looks[t] = lookFor (idx[t]);
    startTimerHz (40);
}

bool StepGrid::cellAt (juce::Point<float> p, int& c, int& r, bool& head) const
{
    const float cw = (float) getWidth() / 8.0f, rh = ((float) getHeight() - HEAD) / 16.0f;
    c = juce::jlimit (0, 7, (int) (p.x / cw));
    head = p.y < HEAD;
    r = (int) ((p.y - HEAD) / rh);
    return head || (r >= 0 && r < 16);
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    int c, r; bool head;
    if (! cellAt (e.position, c, r, head)) return;
    selected = c; if (onSelect) onSelect (c);
    if (head) { proc.previewTrack (c); hit[c] = 1; return; }
    paintValue = proc.steps[c][r].load() ? 0 : 1;
    proc.setStep (c, r, paintValue == 1);
    if (paintValue) { rings.push_back ({ c, r, 1.0f }); if (! proc.hostIsPlaying() && ! proc.internalPlay.load()) proc.previewTrack (c); }
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    int c, r; bool head;
    if (! cellAt (e.position, c, r, head) || head) return;
    if ((proc.steps[c][r].load() != 0) != (paintValue == 1)) { proc.setStep (c, r, paintValue == 1); if (paintValue) rings.push_back ({ c, r, 1.0f }); }
}

void StepGrid::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff06070a)); g.fillRoundedRectangle (b, 12);
    const float cw = b.getWidth() / 8.0f, rh = (b.getHeight() - HEAD) / 16.0f;
    const int ps = proc.playStep.load();
    const float lvl = proc.level.load();

    for (int t = 0; t < 8; ++t)
    {
        const float x = t * cw;
        const bool muted = proc.apvts.getRawParameterValue (BeatsProcessor::trackId ("mute", t))->load() > 0.5f;
        if (t == selected)
        {
            g.setColour (trackCol[t].withAlpha (0.08f)); g.fillRect (x + 1, 0.0f, cw - 2, b.getHeight());
            g.setColour (trackCol[t].withAlpha (0.6f)); g.drawRect (x + 1.5f, 1.5f, cw - 3, b.getHeight() - 3, 1.5f);
        }
        drawGhost (g, looks[t], x + cw / 2, 26 - hit[t] * 10, juce::jmin (28.0f, cw * 0.5f), hit[t] * 1.2f, hit[t] > 0.6f, muted ? 0.3f : 1.0f);
        g.setColour (trackCol[t].withAlpha (muted ? 0.35f : 0.95f)); g.setFont (font (10.0f));
        const int snd = (int) proc.apvts.getRawParameterValue (BeatsProcessor::trackId ("snd", t))->load();
        g.drawText (DrumBank::names()[snd].toUpperCase(), juce::Rectangle<float> (x, HEAD - 18, cw, 14), juce::Justification::centred);

        for (int r = 0; r < 16; ++r)
        {
            const float y = HEAD + r * rh;
            auto cell = juce::Rectangle<float> (x + 3, y + 2, cw - 6, rh - 4);
            const int v = proc.steps[t][r].load();
            const bool now = r == ps;
            if (v)
            {
                auto c = now ? col::white : trackCol[t];
                if (now && ! muted) { g.setColour (trackCol[t].withAlpha (0.4f)); g.fillRoundedRectangle (cell.expanded (3), 8); }
                g.setColour (c.withAlpha (muted ? 0.3f : 0.85f)); g.fillRoundedRectangle (cell, 6);
            }
            else
            {
                g.setColour (juce::Colours::white.withAlpha (r % 4 == 0 ? 0.07f : 0.035f)); g.fillRoundedRectangle (cell, 6);
            }
        }
    }
    if (ps >= 0)
    {
        const float y = HEAD + ps * rh;
        g.setColour (col::slime.withAlpha (0.12f + lvl * 0.25f)); g.fillRect (0.0f, y, b.getWidth(), rh);
    }
    for (auto& rg : rings)
    {
        const float x = rg.c * cw + cw / 2, y = HEAD + rg.r * rh + rh / 2, rad = (1 - rg.a) * cw * 0.7f + 6;
        g.setColour (trackCol[rg.c].withAlpha (rg.a)); g.drawEllipse (x - rad, y - rad, rad * 2, rad * 2, 2.0f);
    }
}

void StepGrid::timerCallback()
{
    for (int t = 0; t < 8; ++t)
    {
        const int h = proc.hitCount[t].load();
        if (h != lastHits[t]) { lastHits[t] = h; hit[t] = 1.0f; const int ps = proc.playStep.load(); if (ps >= 0 && proc.steps[t][ps].load()) rings.push_back ({ t, ps, 1.0f }); }
        hit[t] *= 0.86f;
    }
    for (auto& r : rings) r.a *= 0.86f;
    rings.erase (std::remove_if (rings.begin(), rings.end(), [] (const Ring& r) { return r.a < 0.03f; }), rings.end());
    repaint();
}

// ------------------------------------------------------------------ editor
BeatsEditor::BeatsEditor (BeatsProcessor& p) : AudioProcessorEditor (&p), proc (p), grid (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (grid); addAndMakeVisible (pad);
    for (auto* c : std::initializer_list<juce::Component*> { &patternBox, &soundBox, &speedBox, &prevBtn, &nextBtn, &playBtn, &revBtn, &diceBtn, &clearBtn,
                     &trackRev, &trackMute, &trackPreview, &pitch, &vol, &trackLabel, &pitchLabel, &volLabel })
        addAndMakeVisible (c);

    refreshPatternBox();
    patternBox.onChange = [this] { const int i = patternBox.getSelectedItemIndex(); if (i >= 0 && i != proc.getCurrentProgram()) { proc.setCurrentProgram (i); wordGlitch = 1; selectTrack (grid.selected); } };
    prevBtn.onClick = [this] { const int n = proc.getNumPrograms(); proc.setCurrentProgram ((proc.getCurrentProgram() + n - 1) % n); refreshPatternBox(); selectTrack (grid.selected); wordGlitch = 1; };
    nextBtn.onClick = [this] { proc.setCurrentProgram ((proc.getCurrentProgram() + 1) % proc.getNumPrograms()); refreshPatternBox(); selectTrack (grid.selected); wordGlitch = 1; };
    playBtn.onClick = [this] { proc.internalPlay.store (! proc.internalPlay.load()); };
    diceBtn.onClick = [this] { proc.randomise(); wordGlitch = 1; };
    clearBtn.onClick = [this] { proc.clearAll(); };
    trackPreview.onClick = [this] { proc.previewTrack (grid.selected); };

    speedBox.addItemList ({ "1/2x SPEED", "1x SPEED", "2x SPEED" }, 1);
    speedAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "speed", speedBox);
    revBtn.setClickingTogglesState (true);
    revAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "reverse", revBtn);
    trackRev.setClickingTogglesState (true); trackMute.setClickingTogglesState (true);
    soundBox.addItemList (DrumBank::names(), 1);
    pitchLabel.setText ("PITCH", juce::dontSendNotification); volLabel.setText ("VOLUME", juce::dontSendNotification);
    trackLabel.setFont (font (14.0f));

    grid.onSelect = [this] (int t) { selectTrack (t); };

    pad.addGhost ("FILTER", col::ice,  proc.apvts.getParameter ("filter"), proc.apvts.getParameter ("res"), 3);
    pad.addGhost ("SPACE",  col::ecto, proc.apvts.getParameter ("echo"),   proc.apvts.getParameter ("verb"), 12);
    pad.addGhost ("WARP",   col::pink, proc.apvts.getParameter ("warp"),   proc.apvts.getParameter ("swing"), 21);
    pad.addGhost ("CRUSH",  col::gold, proc.apvts.getParameter ("crush"),  proc.apvts.getParameter ("drive"), 30);
    pad.levelSource = [this] { return proc.level.load(); };

    const std::pair<const char*, const char*> ks[] { { "bpm", "TEMPO*" }, { "glitch", "GLITCH" }, { "human", "HUMAN" }, { "res", "RESONANCE" }, { "gain", "VOLUME" } };
    for (auto& k : ks) addAndMakeVisible (knobs.add (new GhostKnob (proc.apvts, k.first, k.second)));

    const std::pair<const char*, int> hs[] { { "1/4", 8 }, { "1/8", 4 }, { "1/16", 2 }, { "1/32", 1 } };
    for (auto& h : hs)
    {
        auto* b = holds.add (new HoldButton (h.first, col::ecto));
        const int len = h.second;
        b->onHold = [this, len] (bool down)
        {
            if (down) { if (! proc.hostIsPlaying()) proc.internalPlay.store (true); proc.stutterLen.store (len); wordGlitch = 1; }
            else if (proc.stutterLen.load() == len) proc.stutterLen.store (0);
        };
        addAndMakeVisible (b);
    }
    auto* tape = holds.add (new HoldButton ("TAPE STOP", col::pink));
    tape->onHold = [this] (bool down) { proc.tapeHeld.store (down); if (down) wordGlitch = 1; };
    addAndMakeVisible (tape);

    selectTrack (0);
    setSize (1060, 720);
    startTimerHz (20);
}

BeatsEditor::~BeatsEditor() { setLookAndFeel (nullptr); }

void BeatsEditor::refreshPatternBox()
{
    patternBox.clear (juce::dontSendNotification);
    for (int i = 0; i < proc.getNumPrograms(); ++i) patternBox.addItem (proc.getProgramName (i), i + 1);
    patternBox.setSelectedItemIndex (proc.getCurrentProgram(), juce::dontSendNotification);
}

void BeatsEditor::selectTrack (int t)
{
    grid.selected = t;
    soundAtt.reset(); pitchAtt.reset(); volAtt.reset(); trackRevAtt.reset(); trackMuteAtt.reset();
    soundAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, BeatsProcessor::trackId ("snd", t), soundBox);
    pitchAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, BeatsProcessor::trackId ("pitch", t), pitch);
    volAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, BeatsProcessor::trackId ("vol", t), vol);
    trackRevAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, BeatsProcessor::trackId ("rev", t), trackRev);
    trackMuteAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, BeatsProcessor::trackId ("mute", t), trackMute);
    trackLabel.setText (juce::String ("TRACK  ·  ") + names[t], juce::dontSendNotification);
    trackLabel.setColour (juce::Label::textColourId, trackCol[t]);
}

void BeatsEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    auto b = getLocalBounds().toFloat();
    juce::ColourGradient haze (col::ecto.withAlpha (0.18f), 0, 0, col::bg.withAlpha (0.0f), b.getWidth() * 0.5f, b.getHeight() * 0.4f, true);
    g.setGradientFill (haze); g.fillRect (b);
    drawWordmark (g, { 20, 10, 300, 40 }, "BEATS", wordGlitch);
    for (auto r : { juce::Rectangle<float> (556, 66, 488, 128), juce::Rectangle<float> (556, 530, 488, 176) })
    {
        g.setColour (col::card.withAlpha (0.9f)); g.fillRoundedRectangle (r, 14);
        g.setColour (col::ecto.withAlpha (0.2f)); g.drawRoundedRectangle (r, 14, 1.0f);
    }
    g.setColour (col::dim); g.setFont (font (10.0f));
    g.drawText (proc.hostIsPlaying() ? "SYNCED TO ABLETON" : "Press Play in Ableton, or PLAY here", juce::Rectangle<float> (720, 50, 320, 14), juce::Justification::centredRight);
    g.drawText ("*TEMPO is only used outside a DAW. Hold the purple pads to stutter, TAPE STOP to slow down.", juce::Rectangle<float> (20, 698, 1020, 16), juce::Justification::centredLeft);
}

void BeatsEditor::resized()
{
    patternBox.setBounds (340, 14, 250, 34); prevBtn.setBounds (596, 14, 36, 34); nextBtn.setBounds (636, 14, 36, 34);
    playBtn.setBounds (690, 14, 80, 34); speedBox.setBounds (776, 14, 130, 34); revBtn.setBounds (912, 14, 128, 34);
    grid.setBounds (20, 66, 524, 628);
    trackLabel.setBounds (568, 72, 220, 22);
    soundBox.setBounds (568, 98, 200, 30); trackPreview.setBounds (774, 98, 70, 30); trackRev.setBounds (850, 98, 92, 30); trackMute.setBounds (948, 98, 84, 30);
    pitchLabel.setBounds (568, 134, 60, 24); pitch.setBounds (630, 134, 404, 24);
    volLabel.setBounds (568, 162, 60, 24); vol.setBounds (630, 162, 404, 24);
    diceBtn.setBounds (868, 72, 80, 22); clearBtn.setBounds (952, 72, 80, 22);
    pad.setBounds (556, 202, 488, 320);
    for (int i = 0; i < knobs.size(); ++i) knobs[i]->setBounds (566 + i * 94, 536, 90, 88);
    for (int i = 0; i < holds.size(); ++i) holds[i]->setBounds (566 + i * 94, 632, i == 4 ? 104 : 88, 64);
}

void BeatsEditor::timerCallback()
{
    wordGlitch *= 0.85f;
    if (proc.stutterLen.load() > 0 || proc.tapeHeld.load()) wordGlitch = juce::jmax (wordGlitch, 0.6f);
    playBtn.setToggleState (proc.internalPlay.load() || proc.hostIsPlaying(), juce::dontSendNotification);
    playBtn.setButtonText (proc.internalPlay.load() ? "STOP" : "PLAY");
    if (patternBox.getSelectedItemIndex() != proc.getCurrentProgram()) patternBox.setSelectedItemIndex (proc.getCurrentProgram(), juce::dontSendNotification);
    repaint (0, 0, getWidth(), 66);
}
