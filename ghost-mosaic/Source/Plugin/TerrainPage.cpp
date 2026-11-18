// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
#include "TerrainPage.h"
#include "Look.h"
#include "Live.h"

namespace mosaic
{
ghost::terrain::Palette TerrainPage::palette()
{
    // the Ghost Lab theme is Ghost Mosaic's palette too: charcoal, electric purple -> acid green
    auto p = ghost::terrain::Palette::ghostLab();
    p.ground = juce::Colour (0xff111113);
    p.font = [] (float h, bool bold) { return look::font (h * 0.8f, bold ? 1 : 0); };
    return p;
}

TerrainPage::TerrainPage (Processor& p) : m_proc (p), m_view (p.terrain, terrainRange())
{
    setComponentID ("terrainPage");
    m_view.setComponentID ("terrainView");
    m_view.setPalette (palette());
    m_view.setTimeLabel (p.isEffect() ? "LOOP (8 S)" : "TARGET");
    m_view.setTooltip ("TERRAIN: pull the ground up to raise a mountain (down for a valley). Across = frequency: the bands under a mountain count more "
                       "when a piece is picked. Front to back = the target's time: tall or rough land gets wild there, valleys stick close. "
                       "Thin spires stutter, wide plateaus run smooth, merging mountains blend. It only moves the engine's own settings. "
                       "Drag a dot for height, its line to move it, the width tag sideways. Double-click removes one. It plays while you pull.");
    m_view.onListen = [this]
    {
        auto& e = m_proc.engine();
        if (e.isPlaying()) e.pause(); else e.play();
    };
    m_view.isListening = [this] { return m_proc.engine().isPlaying(); };
    addAndMakeVisible (m_view);

    m_shapes.setComponentID ("terrainShapes");
    m_shapes.setTextWhenNothingSelected ("SHAPES");
    m_shapes.setTooltip ("Ready-made landscapes, each a different sound. UNDO takes it back.");
    int id = 1;
    for (auto& sp : shapePresets()) m_shapes.addItem (sp.name, id++);
    m_shapes.onChange = [this]
    {
        const int i = m_shapes.getSelectedItemIndex();
        if (i >= 0 && i < (int) shapePresets().size()) m_proc.terrain.set (shapePresets()[(size_t) i].make());
        m_shapes.setSelectedId (0, juce::dontSendNotification);
    };
    // the component's own SHAPES pill (top right of the land) lists Ghost Mosaic's shapes; the box stays as a hidden hook
    addChildComponent (m_shapes);
    for (auto& sp : shapePresets()) m_view.shapeNames.add (sp.name);
    m_view.makeShape = [] (const juce::String& name)
    {
        for (auto& sp : shapePresets()) if (name == sp.name) return sp.make();
        return ghost::terrain::flatTerrain();
    };
    m_on.setComponentID ("terrainOn");
    m_on.setTooltip ("TERRAIN on or off. Off: the sound is exactly as the knobs say (the landscape is kept).");
    m_on.setToggleState (p.terrainOn(), juce::dontSendNotification);
    m_on.onClick = [this] { m_proc.setTerrainOn (m_on.getToggleState()); repaint(); };
    addAndMakeVisible (m_on);
    m_move.setComponentID ("terrainMovement");
    m_move.setTooltip (paramInfo ("move_amount").help);
    m_moveAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.params(), "move_amount", m_move);
    m_move.setDoubleClickReturnValue (true, paramInfo ("move_amount").def);
    addAndMakeVisible (m_move);
    m_rate.setComponentID ("terrainMoveRate");
    m_rate.setTooltip (paramInfo ("move_rate").help);
    m_rate.addItemList (juce::StringArray::fromTokens (paramInfo ("move_rate").choices, "|", ""), 1);
    m_rateAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.params(), "move_rate", m_rate);
    addAndMakeVisible (m_rate);
}

juce::Rectangle<int> TerrainPage::cardArea() const { return getLocalBounds().withTrimmedTop (44).withTrimmedBottom (34).withTrimmedRight (262); }
juce::Rectangle<int> TerrainPage::barsArea() const { return getLocalBounds().withTrimmedTop (44).withTrimmedBottom (34).removeFromRight (250); }

void TerrainPage::resized()
{
    m_view.setBounds (cardArea().reduced (2));
    auto head = getLocalBounds().removeFromTop (44).reduced (0, 6);
    m_on.setBounds (head.removeFromRight (70));
    head.removeFromRight (8);
    m_rate.setBounds (head.removeFromRight (96));
    head.removeFromRight (6);
    m_move.setBounds (head.removeFromRight (120));
    head.removeFromRight (76);   // ("MOVEMENT" is painted left of it)
    m_shapes.setBounds (head.removeFromRight (180));   // (hidden)
}

void TerrainPage::tick()
{
    auto& e = m_proc.engine();
    {
        // DRAG: while a mountain is being pulled, how fast and which way goes to the engine (heard at once)
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        const float dt = m_lastT > 0 ? (float) (now - m_lastT) : 0.0f;
        const auto& land = m_proc.terrain.terrain();
        DragFeel f;
        if (m_view.interaction.active() && dt > 0.0f) f = dragFeel (m_lastLand, land, m_view.interaction.currentPeak(), dt);
        // (drag events don't come every frame: hold the feel briefly so a steady pull doesn't flutter)
        const float hold = std::exp (-juce::jmax (0.0f, dt) / 0.08f);
        auto keep = [&] (float& v, float to) { v = std::abs (to) >= std::abs (v) * hold ? to : v * hold; if (std::abs (v) < 1e-3f) v = 0.0f; };
        if (m_view.interaction.active()) { keep (m_feel.burst, f.burst); keep (m_feel.side, f.side); keep (m_feel.lift, f.lift); }
        else m_feel = {};
        e.setDrag (m_feel.burst, m_feel.side, m_feel.lift);
        m_lastLand = land; m_lastT = now;
    }
    const auto shape = e.shaping();
    const auto sum = describe (summarise (shape, e.sampleRate()));
    // MOVING NOW: every original control TERRAIN is moving, with the value you set and the value in use (read from the audio thread's atomics)
    std::vector<Moving> moving;
    juce::String soundsLine;
    if (m_proc.terrainOn())
    {
        for (int i = 0; i < Engine::kLiveCount; ++i)
        {
            const auto v = e.liveValue (i);
            if (v.moving) moving.push_back ({ live::name (i), live::text (i, v.set), live::text (i, v.now) });
        }
        if (e.soundsMasked())
        {
            const int n = (int) e.sounds().size();
            int out = 0;
            for (int k = 0; k < n; ++k) out += e.soundBenched (k) ? 1 : 0;
            if (out > 0) soundsLine = juce::String (n - out) + " OF " + juce::String (n) + " IN PLAY";
        }
    }
    auto ps = pushes (shape);
    if (const float d = e.dragNow(); d > 0.01f) ps.insert (ps.begin(), { "DRAG BURST (WILD + STUTTER)", juce::jmin (1.0f, d) });
    if (ps.size() > 2) ps.resize (2);
    bool changed = sum != m_readout || ps.size() != m_pushes.size() || moving.size() != m_moving.size() || soundsLine != m_soundsLine;
    for (size_t i = 0; ! changed && i < ps.size(); ++i) changed = ps[i].name != m_pushes[i].name || std::abs (ps[i].amount - m_pushes[i].amount) > 0.005f;
    for (size_t i = 0; ! changed && i < moving.size(); ++i) changed = moving[i].name != m_moving[i].name || moving[i].now != m_moving[i].now || moving[i].set != m_moving[i].set;
    if (m_on.getToggleState() != m_proc.terrainOn()) m_on.setToggleState (m_proc.terrainOn(), juce::dontSendNotification);
    if (changed) { m_readout = sum; m_pushes = ps; m_moving = moving; m_soundsLine = soundsLine; repaint (barsArea()); repaint (0, getHeight() - 34, getWidth(), 34); }
    const auto now = e.shapeNow();
    // the playhead: where the target is (front = its start)
    const auto tp = e.targetPicture();
    const int cur = e.currentTargetBlock();
    if (e.isPlaying() && now.time) m_view.setPlayhead (now.pos);
    else if (e.isPlaying() && tp != nullptr && tp->numBlocks > 1 && cur >= 0) m_view.setPlayhead ((float) cur / (float) (tp->numBlocks - 1));
    else m_view.setPlayhead (-1.0f);
}

void TerrainPage::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    auto head = r.removeFromTop (44);
    look::title (g, "TERRAIN", head.withWidth (120), look::ink, 13.0f);
    g.setColour (look::inkDim);
    g.setFont (look::font (12.0f, 0));
    g.drawFittedText ("Across = which frequencies count when a piece is picked.  Front to back = the target's time: tall or rough land gets wild there.  "
                      "Spires stutter, plateaus run smooth, merged mountains blend.",
                      head.withTrimmedLeft (110).withTrimmedRight (400).reduced (0, 4), juce::Justification::centredLeft, 2, 0.85f);
    g.setColour (look::inkDim);
    g.setFont (look::spaced (11.0f, 1, 0.14f));
    g.drawText ("MOVEMENT", m_move.getBounds().translated (-78, 0).withWidth (74), juce::Justification::centredRight);
    look::card (g, cardArea().toFloat());
    g.setColour (palette().ground);
    g.fillRoundedRectangle (cardArea().toFloat().reduced (1.0f), 11.0f);

    // what the shape is doing: the strongest moves, with bars
    auto b = barsArea();
    look::card (g, b.toFloat());
    b = b.reduced (16, 14);
    look::title (g, "WHAT THE SHAPE DOES", b.removeFromTop (22), look::ink, 12.0f);
    b.removeFromTop (8);
    if (! m_proc.terrainOn()) { g.setColour (look::inkFaint); g.setFont (look::font (12.0f, 0)); g.drawFittedText ("TERRAIN is off: the knobs as set.", b.removeFromTop (40), juce::Justification::topLeft, 2); }
    else if (m_pushes.empty()) { g.setColour (look::inkFaint); g.setFont (look::font (12.0f, 0)); g.drawFittedText ("Flat land: nothing yet. Pull the ground up, or pick a shape.", b.removeFromTop (40), juce::Justification::topLeft, 3); }
    for (auto& p : m_pushes)
    {
        auto row = b.removeFromTop (46);
        g.setColour (look::inkDim);
        g.setFont (look::spaced (10.5f, 1, 0.08f));
        g.drawText (p.name, row.removeFromTop (18), juce::Justification::centredLeft, true);
        auto bar = row.removeFromTop (8).toFloat();
        g.setColour (look::line); g.fillRoundedRectangle (bar, 4.0f);
        juce::ColourGradient gr (look::purple, bar.getX(), 0, look::green, bar.getRight(), 0, false);
        g.setGradientFill (gr);
        g.fillRoundedRectangle (bar.withWidth (juce::jmax (8.0f, bar.getWidth() * juce::jlimit (0.0f, 1.0f, p.amount))), 4.0f);
    }
    // MOVING NOW: the original controls being moved, set value > value in use
    {
        b.removeFromTop (6);
        look::title (g, "MOVING NOW", b.removeFromTop (22), look::live, 12.0f);
        if (! m_proc.terrainOn() || (m_moving.empty() && m_soundsLine.isEmpty()))
        {
            g.setColour (look::inkFaint); g.setFont (look::font (12.0f, 0));
            g.drawFittedText (m_proc.terrainOn() ? "Nothing yet: the controls are as set." : "Nothing: TERRAIN is off.", b.removeFromTop (34), juce::Justification::topLeft, 2);
        }
        for (auto& m : m_moving)
        {
            auto row = b.removeFromTop (18);
            g.setColour (look::inkDim); g.setFont (look::spaced (10.5f, 1, 0.06f));
            g.drawText (m.name, row.removeFromLeft (row.getWidth() / 2 + 10), juce::Justification::centredLeft, true);
            g.setColour (look::inkFaint); g.setFont (look::font (11.5f, 0));
            g.drawText (m.set, row.removeFromLeft (row.getWidth() / 2 - 4), juce::Justification::centredRight, true);
            g.setColour (look::live); g.setFont (look::font (11.5f, 1));
            g.drawText ("> " + m.now, row, juce::Justification::centredRight, true);
        }
        if (m_soundsLine.isNotEmpty())
        {
            auto row = b.removeFromTop (18);
            g.setColour (look::inkDim); g.setFont (look::spaced (10.5f, 1, 0.06f));
            g.drawText ("SOURCE SOUNDS", row.removeFromLeft (row.getWidth() / 2 + 10), juce::Justification::centredLeft, true);
            g.setColour (look::live); g.setFont (look::font (11.5f, 1));
            g.drawText (m_soundsLine, row, juce::Justification::centredRight, true);
        }
        b.removeFromTop (4);
        g.setColour (look::inkFaint); g.setFont (look::font (10.5f, 0));
        g.drawFittedText ("NOT MOVED: needs a rebuild (PIECE SIZE, OVERLAP, FADE SHAPES), restarts the sound (STEREO), or level (VOLUME).", b.removeFromTop (38), juce::Justification::topLeft, 3, 0.9f);
        g.setColour (look::purple); g.setFont (look::font (10.5f, 0));
        g.drawFittedText ("DRAG FX: the burst of wildness and stutter from pulling a mountain fades out when you let go.", b.removeFromTop (28), juce::Justification::topLeft, 2, 0.9f);
    }
    auto foot = r.removeFromBottom (34);
    g.setColour (look::green);
    g.setFont (look::spaced (11.5f, 1, 0.08f));
    g.drawText (m_readout, foot, juce::Justification::centredLeft, true);
}
}
