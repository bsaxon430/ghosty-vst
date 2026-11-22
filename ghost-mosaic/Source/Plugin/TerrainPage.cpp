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
    p.font = [] (float h, bool bold) { return look::font (juce::jmax (10.0f, h * 0.92f), bold ? 1 : 0); };   // (never under 10 px)
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

int TerrainPage::headH() const { return look::headOf (winH()); }
int TerrainPage::gut() const { return look::gutterOf (winH()); }
juce::Rectangle<int> TerrainPage::cardArea() const { return getLocalBounds().withTrimmedBottom (footH() + gut()).withTrimmedRight (sideW() + gut()).withTrimmedTop (headH()); }
juce::Rectangle<int> TerrainPage::barsArea() const { return getLocalBounds().withTrimmedBottom (footH() + gut()).removeFromRight (sideW()); }
juce::Rectangle<int> TerrainPage::footArea() const { return getLocalBounds().removeFromBottom (footH()); }

void TerrainPage::resized()
{
    m_view.setBounds (cardArea().reduced (2));
    const float k = scaleNow();
    const int hh = headH(), ch = juce::jmin (hh - 10, juce::roundToInt (28.0f * k));
    auto head = cardArea().withY (0).withHeight (hh).withTrimmedRight (hh / 2);
    auto take = [&] (float w) { const int ww = juce::roundToInt (w * k); return head.removeFromRight (ww).withSizeKeepingCentre (ww, ch); };
    for (juce::Component* c : std::initializer_list<juce::Component*> { &m_on, &m_rate, &m_move }) look::setScale (*c, k);
    m_on.setBounds (take (74)); head.removeFromRight (10);
    m_rate.setBounds (take (100)); head.removeFromRight (8);
    m_move.setBounds (take (130));
    m_moveLabel = take (96);
    m_shapes.setBounds (head.removeFromRight (juce::jmin (180, head.getWidth())).withSizeKeepingCentre (juce::jmin (180, head.getWidth()), ch));   // (hidden)
    look::setScale (m_rate, k);   // (again, now that it has its place: the menu's type follows)
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
    if (changed) { m_readout = sum; m_pushes = ps; m_moving = moving; m_soundsLine = soundsLine; repaint (barsArea()); repaint (footArea()); }
    const auto now = e.shapeNow();
    // the playhead: where the target is (front = its start)
    const auto tp = e.targetPicture();
    const int cur = e.currentTargetBlock();
    if (e.isPlaying() && now.time) m_view.setPlayhead (now.pos);
    else if (e.isPlaying() && tp != nullptr && tp->numBlocks > 1 && cur >= 0) m_view.setPlayhead ((float) cur / (float) (tp->numBlocks - 1));
    else m_view.setPlayhead (-1.0f);
}

// Words on the land (the land itself is a shared component and is not edited here): what its two directions mean,
// and, while it is flat, where to start.
void TerrainPage::paintOverChildren (juce::Graphics& g)
{
    const float k = scaleNow();
    const int lh = juce::roundToInt (17.0f * k);
    auto land = cardArea().reduced (juce::roundToInt (16.0f * k), juce::roundToInt (12.0f * k));
    g.setColour (look::inkDim);
    g.setFont (look::font (juce::jmax (11.0f, 11.5f * k), 0));
    g.drawText ("left to right: low notes to high notes", land.removeFromTop (lh), juce::Justification::centredLeft, true);
    g.drawText (m_proc.isEffect() ? "front to back: an 8-second loop of the input" : "front to back: the target's start to its end", land.removeFromTop (lh), juce::Justification::centredLeft, true);
    if (m_proc.terrainOn() && m_pushes.empty())
    {
        land.removeFromTop (juce::roundToInt (4.0f * k));
        g.setColour (look::ink);
        g.setFont (look::spaced (juce::jmax (11.0f, 12.0f * k), 1, 0.08f));
        g.drawText ("FLAT LAND: PULL THE GROUND UP ANYWHERE, OR PICK A SHAPE", land.removeFromTop (lh), juce::Justification::centredLeft, true);
    }
}

void TerrainPage::paint (juce::Graphics& g)
{
    const float k = scaleNow();
    const int hh = headH(), gp = gut();
    auto f = [k] (float h, int w = 0) { return look::font (h * k, w); };
    // a card with nothing to show says so like a poster: the words before the colon big, the rest under them
    auto statement = [&] (juce::Rectangle<int>& b, const juce::String& text)
    {
        const auto head = text.upToFirstOccurrenceOf (":", false, false), tail = text.fromFirstOccurrenceOf (":", false, false).trim();
        g.setColour (look::ink); g.setFont (look::spaced (18.0f * k, 2, 0.1f));
        g.drawFittedText (head.toUpperCase(), b.removeFromTop (juce::roundToInt (28.0f * k)), juce::Justification::centredLeft, 1, 0.7f);
        g.setColour (look::inkDim); g.setFont (f (12.0f));
        g.drawFittedText (tail.substring (0, 1).toUpperCase() + tail.substring (1), b.removeFromTop (juce::roundToInt (38.0f * k)), juce::Justification::topLeft, 2);
    };
    // 01 TERRAIN: the land under its band; MOVEMENT and the switch at the band's right
    const auto land = cardArea();
    look::zone (g, land.withTop (0), "01", "TERRAIN", hh);
    g.setColour (palette().ground);
    g.fillRect (land.reduced (1));
    g.setColour (look::inkDim);
    g.setFont (look::spaced (11.0f * k, 1, 0.14f));
    g.drawText ("MOVEMENT", m_moveLabel.withTrimmedRight (8), juce::Justification::centredRight);

    // the right column: two numbered cards on the same grid
    auto col = barsArea();
    const int hs = juce::jmax (30, hh - 4), pad = juce::roundToInt (16.0f * k);
    auto top = col.removeFromTop ((col.getHeight() - gp) * 2 / 5);
    col.removeFromTop (gp);
    // 02 what the shape is doing: the strongest moves, with bars
    {
        look::zone (g, top, "02", "WHAT THE SHAPE DOES", hs);
        auto b = top.withTrimmedTop (hs).reduced (pad, juce::roundToInt (12.0f * k));
        if (! m_proc.terrainOn()) statement (b, "TERRAIN is off: the knobs as set.");
        else if (m_pushes.empty()) statement (b, "Flat land: nothing yet. Pull the ground up, or pick a shape.");
        for (auto& p : m_pushes)
        {
            auto row = b.removeFromTop (juce::roundToInt (46.0f * k));
            g.setColour (look::ink);
            g.setFont (look::spaced (11.0f * k, 1, 0.08f));
            g.drawText (p.name, row.removeFromTop (juce::roundToInt (18.0f * k)), juce::Justification::centredLeft, true);
            auto bar = row.removeFromTop (juce::roundToInt (10.0f * k));
            g.setColour (look::line); g.fillRect (bar);
            g.setColour (look::purple);
            g.fillRect (bar.withWidth (juce::jmax (8, juce::roundToInt ((float) bar.getWidth() * juce::jlimit (0.0f, 1.0f, p.amount)))));
        }
    }
    // 03 MOVING NOW: the original controls being moved, set value > value in use
    {
        look::zone (g, col, "03", "MOVING NOW", hs, false, false, look::live);
        auto b = col.withTrimmedTop (hs).reduced (pad, juce::roundToInt (12.0f * k));
        const int lh = juce::roundToInt (19.0f * k);
        if (! m_proc.terrainOn() || (m_moving.empty() && m_soundsLine.isEmpty()))
            statement (b, m_proc.terrainOn() ? "Nothing yet: the controls are as set." : "Nothing: TERRAIN is off.");
        for (auto& m : m_moving)
        {
            auto row = b.removeFromTop (lh);
            g.setColour (look::inkDim); g.setFont (look::spaced (11.0f * k, 1, 0.06f));
            g.drawText (m.name, row.removeFromLeft (row.getWidth() / 2 + 10), juce::Justification::centredLeft, true);
            g.setColour (look::inkDim); g.setFont (f (11.5f));
            g.drawText (m.set, row.removeFromLeft (row.getWidth() / 2 - 4), juce::Justification::centredRight, true);
            g.setColour (look::live); g.setFont (f (11.5f, 1));
            g.drawText ("> " + m.now, row, juce::Justification::centredRight, true);
        }
        if (m_soundsLine.isNotEmpty())
        {
            auto row = b.removeFromTop (lh);
            g.setColour (look::inkDim); g.setFont (look::spaced (11.0f * k, 1, 0.06f));
            g.drawText ("SOURCE SOUNDS", row.removeFromLeft (row.getWidth() / 2 + 10), juce::Justification::centredLeft, true);
            g.setColour (look::live); g.setFont (f (11.5f, 1));
            g.drawText (m_soundsLine, row, juce::Justification::centredRight, true);
        }
        // the two notes sit on the card's bottom edge, so the card is used from top to bottom
        auto notes = b.removeFromBottom (juce::jmin (b.getHeight(), juce::roundToInt (88.0f * k)));
        g.setColour (look::line); g.fillRect (notes.removeFromTop (1));
        notes.removeFromTop (juce::roundToInt (8.0f * k));
        g.setColour (look::inkDim); g.setFont (f (11.0f));
        g.drawFittedText ("NOT MOVED: needs a rebuild (PIECE SIZE, OVERLAP, FADE SHAPES), restarts the sound (STEREO), or level (VOLUME).", notes.removeFromTop (notes.getHeight() * 3 / 5), juce::Justification::topLeft, 3, 0.9f);
        g.setColour (look::purple.brighter (0.2f)); g.setFont (f (11.0f));
        g.drawFittedText ("DRAG FX: the burst of wildness and stutter from pulling a mountain fades out when you let go.", notes, juce::Justification::topLeft, 2, 0.9f);
    }
    // the band along the bottom: what the land is doing now (green), and how to read the land
    {
        auto foot = footArea();
        const auto shape = look::cutRect (foot.toFloat().reduced (0.5f), 14.0f);
        g.setColour (look::band); g.fillPath (shape);
        g.setColour (look::green); g.fillRect (foot.withWidth (juce::roundToInt (6.0f * k)));
        auto t = foot.reduced (juce::roundToInt (20.0f * k), juce::roundToInt (5.0f * k)).withTrimmedRight (10);
        g.setFont (look::spaced (12.0f * k, 1, 0.08f));
        g.drawText (m_readout, t.removeFromTop (t.getHeight() * 2 / 5), juce::Justification::centredLeft, true);
        g.setColour (look::inkDim);
        g.setFont (f (11.0f));
        g.drawFittedText ("Across = which frequencies count when a piece is picked.  Front to back = the target's time: tall or rough land gets wild there.  "
                          "Spires stutter, plateaus run smooth, merged mountains blend.", t, juce::Justification::centredLeft, 2, 0.85f);
    }
}
}
