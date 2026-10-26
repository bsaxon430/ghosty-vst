#include "SynthEditor.h"
#include "GhostyLogo.h"
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
        const bool on = proc.keyboardState.isNoteOnForChannels (0xffff, n) || proc.isNoteSounding (n);
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

// ------------------------------------------------------------------ "Get Pro" pill + info panel (no nags, no locks: free Ghosty stays free)
namespace
{
const char* const proStoreUrl = "https://bsaxon430.github.io/ghosty-vst/store/?from=ghosty";

// Small glowing pill in the header. Only does something when you click it.
struct ProPill : public juce::Button
{
    ProPill() : juce::Button ("GET PRO") { setMouseCursor (juce::MouseCursor::PointingHandCursor); setTooltip ("What Ghosty Pro adds"); }
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (5.0f, 4.0f);
        const float rad = r.getHeight() * 0.5f;
        for (int k = 3; k >= 1; --k)
        {
            g.setColour (col::ecto.withAlpha ((over ? 0.10f : 0.06f) * (float) (4 - k)));
            g.fillRoundedRectangle (r.expanded ((float) k * 1.6f), rad + (float) k * 1.6f);
        }
        juce::ColourGradient gr (col::ecto.withAlpha (down ? 0.9f : 0.55f), r.getX(), 0, col::pink.withAlpha (down ? 0.9f : 0.5f), r.getRight(), 0, false);
        g.setGradientFill (gr); g.fillRoundedRectangle (r, rad);
        g.setColour (col::white.withAlpha (over ? 0.9f : 0.55f)); g.drawRoundedRectangle (r, rad, 1.0f);
        // tiny ghost on the left of the label
        {
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path clip; clip.addRoundedRectangle (r, rad); g.reduceClipRegion (clip);
            GhostLook L; L.eyes = 3; L.mouth = 1; L.lines = false;
            drawGhost (g, L, r.getX() + rad + 4.0f, r.getCentreY() + 2.0f, r.getHeight() * 0.72f, 0.0f, false, 0.95f);
        }
        g.setColour (col::white); g.setFont (font (12.5f));
        g.drawText ("GET PRO", r.withTrimmedLeft (rad * 2.0f + 2.0f), juce::Justification::centred);
    }
};

// Compact panel drawn inside the plugin window: what Ghosty Pro adds. Closed by "Not now", Esc, or a click outside.
struct ProPanel : public juce::Component
{
    std::function<void()> onClose;
    juce::TextButton seeBtn { "See Ghosty Pro" }, laterBtn { "Not now" };

    ProPanel()
    {
        addAndMakeVisible (seeBtn); addAndMakeVisible (laterBtn);
        seeBtn.setClickingTogglesState (false);
        seeBtn.setToggleState (true, juce::dontSendNotification);   // the Ghosty LnF draws "on" buttons in slime green
        seeBtn.onClick = [this] { juce::URL (proStoreUrl).launchInDefaultBrowser(); close(); };
        laterBtn.onClick = [this] { close(); };
        setWantsKeyboardFocus (true);
    }

    juce::Rectangle<float> card() const
    {
        return juce::Rectangle<float> (0, 0, 600, 436).withCentre (getLocalBounds().toFloat().getCentre());
    }

    void close() { setVisible (false); if (onClose) onClose(); }

    bool keyPressed (const juce::KeyPress& k) override { if (k == juce::KeyPress::escapeKey) { close(); return true; } return false; }
    void mouseDown (const juce::MouseEvent& e) override { if (! card().contains (e.position)) close(); }

    void resized() override
    {
        auto c = card().toNearestInt();
        const int by = c.getBottom() - 64;
        seeBtn.setBounds (c.getX() + 28, by, 300, 36);
        laterBtn.setBounds (c.getRight() - 28 - 140, by, 140, 36);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::bg.withAlpha (0.78f));
        const auto c = card();
        for (int k = 4; k >= 1; --k) { g.setColour (col::ecto.withAlpha (0.05f)); g.fillRoundedRectangle (c.expanded ((float) k * 5.0f), 18.0f + (float) k * 5.0f); }
        g.setColour (col::card); g.fillRoundedRectangle (c, 18);
        juce::ColourGradient haze (col::ecto.withAlpha (0.22f), c.getX(), c.getY(), col::card.withAlpha (0.0f), c.getX() + c.getWidth() * 0.6f, c.getY() + 160, true);
        g.setGradientFill (haze); g.fillRoundedRectangle (c, 18);
        g.setColour (col::ecto.withAlpha (0.55f)); g.drawRoundedRectangle (c, 18, 1.2f);

        // header: a happy ghost + "ghosty PRO"
        GhostLook look; look.eyes = 3; look.mouth = 1; look.acc = 4; look.lines = false; look.slime = col::pink;
        drawGhost (g, look, c.getX() + 70, c.getY() + 62, 62);
        drawWordmark (g, { c.getX() + 112, c.getY() + 26, 300, 40 }, "PRO", 0.0f);
        g.setColour (col::dim); g.setFont (font (12.0f, false));
        g.drawText ("Everything in free Ghosty, plus:", juce::Rectangle<float> (c.getX() + 114, c.getY() + 68, 400, 18), juce::Justification::centredLeft);

        struct Row { juce::Colour c; const char* head; const char* sub; };
        const Row rows[] {
            { col::slime, "245 presets",            "about 60 of them basses" },
            { col::pink,  "8-voice unison supersaws", "huge, wide leads and pads" },
            { col::ice,   "WAV to wavetable",        "drop in any sound and play it as a wave" },
            { col::gold,  "8-slot mod matrix",       "route anything to anything" },
            { col::ecto,  "15-unit Ghost FX studio", "stack, reorder and tweak effects" },
        };
        float y = c.getY() + 104;
        for (auto& rw : rows)
        {
            g.setColour (rw.c.withAlpha (0.25f)); g.fillEllipse (c.getX() + 30, y + 5, 14, 14);
            g.setColour (rw.c); g.fillEllipse (c.getX() + 33, y + 8, 8, 8);
            g.setColour (col::white); g.setFont (font (15.0f));
            g.drawText (rw.head, juce::Rectangle<float> (c.getX() + 54, y, 250, 24), juce::Justification::centredLeft);
            g.setColour (col::dim); g.setFont (font (12.5f, false));
            g.drawText (rw.sub, juce::Rectangle<float> (c.getX() + 290, y, c.getWidth() - 310, 24), juce::Justification::centredLeft);
            y += 34;
        }

        // Ghost Box + bundle
        y += 6;
        auto box = juce::Rectangle<float> (c.getX() + 24, y, c.getWidth() - 48, 74);
        g.setColour (col::card2); g.fillRoundedRectangle (box, 10);
        g.setColour (col::slime.withAlpha (0.25f)); g.drawRoundedRectangle (box, 10, 1.0f);
        g.setColour (col::slime); g.setFont (font (12.5f));
        g.drawText ("ALSO: GHOST BOX", box.reduced (14, 8).removeFromTop (16), juce::Justification::centredLeft);
        g.setColour (col::white.withAlpha (0.85f)); g.setFont (font (12.5f, false));
        g.drawText ("FX rack for any track: Fatten, Haunted Halls reverb, Crackle, Retro", box.reduced (14, 8).withTrimmedTop (18).removeFromTop (18), juce::Justification::centredLeft);
        g.setColour (col::gold); g.setFont (font (12.5f));
        g.drawText ("Everything bundle: all six plugins for $69", box.reduced (14, 8).withTrimmedTop (38).removeFromTop (18), juce::Justification::centredLeft);

        g.setColour (col::dim.withAlpha (0.8f)); g.setFont (font (10.0f, false));
        g.drawText ("You're on Ghosty " JucePlugin_VersionString " - free and open source (AGPL-3.0), nothing locked.",
                    juce::Rectangle<float> (c.getX(), c.getBottom() - 18, c.getWidth(), 14), juce::Justification::centred);
    }
};
} // namespace

// ------------------------------------------------------------------ editor content (fixed size, scaled by the editor)
class SynthContent : public juce::Component, private juce::Timer
{
public:
    static constexpr int W = SynthEditor::W, H = SynthEditor::H;

    SynthContent (SynthProcessor& p, juce::LookAndFeel& lf) : proc (p), wave (p), flow (p)
    {
        setLookAndFeel (&lf);       // before any child is made, so knob text boxes pick up the Ghosty colours
        using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
        using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
        addAndMakeVisible (wave); addAndMakeVisible (pad); addAndMakeVisible (flow);

        auto combo = [this] (juce::ComboBox& c, const juce::StringArray& items, const char* id)
        {
            addAndMakeVisible (c); c.addItemList (items, 1);
            comboAtts.push_back (std::make_unique<CA> (proc.apvts, id, c));
        };
        auto toggle = [this] (juce::TextButton& b, const char* id)
        {
            addAndMakeVisible (b); b.setClickingTogglesState (true);
            buttonAtts.push_back (std::make_unique<BA> (proc.apvts, id, b));
        };
        combo (waveABox, shapeNames(), "waveA");  combo (waveBBox, shapeNames(), "waveB");
        combo (keyBox, keyNames(), "key");        combo (scaleBox, scaleNames(), "scale");   combo (chordBox, chordNames(), "chord");
        combo (rateBox, arpRateNames(), "arpRate"); combo (modeBox, arpModeNames(), "arpMode");
        combo (octBox, { "1 octave", "2 octaves", "3 octaves" }, "arpOct");
        combo (fTypeBox, SynthProcessor::filterTypeNames(), "fType");
        combo (lfoDivBox, SynthProcessor::lfoDivNames(), "lfoDiv");
        combo (lfoShapeBox, SynthProcessor::lfoShapeNames(), "lfoShape");
        combo (lfoDestBox, SynthProcessor::lfoDestNames(), "lfoDest");
        toggle (arpBtn, "arp"); toggle (monoBtn, "mono"); toggle (legatoBtn, "legato"); toggle (linkBtn, "fLink"); toggle (syncBtn, "lfoSync");

        for (auto* b : { &prevBtn, &nextBtn, &saveBtn, &loadBtn, &initBtn, &editA, &editB, &diceBtn }) addAndMakeVisible (b);
        addAndMakeVisible (presetBox);
        refreshPresetBox();
        presetBox.onChange = [this]
        {
            const int id = presetBox.getSelectedId();
            if (id == proTeaserId) { syncPresetBox(); showPro(); return; }
            if (id >= userBase && id - userBase < userFiles.size()) proc.loadUserPreset (userFiles[id - userBase]);
            else if (id >= 1 && id <= proc.getNumPrograms()) proc.setCurrentProgram (id - 1);
            wordGlitch = 1;
        };
        prevBtn.onClick = [this] { const int n = proc.getNumPrograms(); proc.setCurrentProgram ((proc.getCurrentProgram() + n - 1) % n); wordGlitch = 1; };
        nextBtn.onClick = [this] { proc.setCurrentProgram ((proc.getCurrentProgram() + 1) % proc.getNumPrograms()); wordGlitch = 1; };
        saveBtn.onClick = [this] { askSave(); };
        loadBtn.onClick = [this] { showLoadMenu(); };
        initBtn.onClick = [this] { proc.initPatch(); wordGlitch = 1; };
        editA.setToggleState (true, juce::dontSendNotification);
        editA.onClick = [this] { wave.editB = false; editA.setToggleState (true, juce::dontSendNotification); editB.setToggleState (false, juce::dontSendNotification); };
        editB.onClick = [this] { wave.editB = true; editB.setToggleState (true, juce::dontSendNotification); editA.setToggleState (false, juce::dontSendNotification); };
        diceBtn.onClick = [this] { wave.randomise(); wordGlitch = 1; };
        addAndMakeVisible (proPill);
        proPill.onClick = [this] { showPro(); };
        addChildComponent (proPanel);
        proPanel.onClose = [this] { wordGlitch = 1; };

        pad.addGhost ("TONE",  col::ice,  proc.apvts.getParameter ("cutoff"), proc.apvts.getParameter ("res"), 4);
        pad.addGhost ("MORPH", col::pink, proc.apvts.getParameter ("morph"),  proc.apvts.getParameter ("width"), 9);
        pad.addGhost ("SPACE", col::ecto, proc.apvts.getParameter ("echo"),   proc.apvts.getParameter ("verb"), 17);
        pad.addGhost ("HEAT",  col::gold, proc.apvts.getParameter ("drive"),  proc.apvts.getParameter ("sub"), 22);
        pad.levelSource = [this] { return proc.level.load(); };

        auto knobRow = [this] (std::vector<GhostKnob*>& row, std::initializer_list<std::pair<const char*, const char*>> ks)
        {
            for (auto& k : ks) { auto* g = knobs.add (new GhostKnob (proc.apvts, k.first, k.second)); addAndMakeVisible (g); row.push_back (g); }
        };
        knobRow (oscKnobs,  { { "morph", "MORPH" }, { "width", "WIDTH" }, { "unison", "UNISON" }, { "sub", "SUB" }, { "glide", "GLIDE" } });
        knobRow (ampKnobs,  { { "attack", "ATTACK" }, { "decay", "DECAY" }, { "sustain", "SUSTAIN" }, { "release", "RELEASE" } });
        knobRow (filtKnobs, { { "cutoff", "CUTOFF" }, { "res", "RES" }, { "fDrive", "F.DRIVE" }, { "fenv", "ENV AMT" } });
        knobRow (fenvKnobs, { { "fAttack", "F.ATTACK" }, { "fDecay", "F.DECAY" }, { "fSustain", "F.SUSTAIN" }, { "fRelease", "F.RELEASE" } });
        knobRow (lfoKnobs,  { { "lfoRate", "RATE" }, { "lfoDepth", "DEPTH" } });
        knobRow (fxKnobs,   { { "drive", "DRIVE" }, { "crush", "CRUSH" }, { "chorus", "CHORUS" }, { "phaser", "PHASER" }, { "echo", "ECHO" }, { "verb", "REVERB" }, { "haunt", "HAUNT" } });
        knobRow (modKnobs,  { { "wheelAmt", "MOD WHEEL" }, { "velFilt", "VEL > FILT" } });
        knobRow (outKnobs,  { { "gain", "VOLUME" } });
        knobRow (arpKnobs,  { { "arpGate", "ARP GATE" } });

        for (int i = 0; i < 9; ++i)
        {
            auto& r = juce::Random::getSystemRandom();
            bg.push_back ({ lookFor ((i * 4 + 1) % 35), r.nextFloat() * 6.28f, 0.25f + r.nextFloat() * 0.4f, (0.04f + r.nextFloat() * 0.08f) * (r.nextBool() ? 1.0f : -1.0f), 22 + r.nextFloat() * 26, 0.12f + r.nextFloat() * 0.12f });
        }
        setSize (W, H);
        startTimerHz (24);
    }

    ~SynthContent() override { stopTimer(); setLookAndFeel (nullptr); }

    void paint (juce::Graphics& g) override
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
        // the Ghosty logo: the mascot (idle drip / blink) + the "ghosty" wordmark + the SYNTH tag; a preset change still glitches it
        {
            const float gx = wordGlitch > 0.05f ? 3.0f * wordGlitch : 0.0f;
            if (gx > 0.0f) { g.setColour (col::slime.withAlpha (0.35f * wordGlitch)); g.fillEllipse (14.0f - gx, 10.0f, 40.0f, 42.0f); }
            logo::drawMascot (g, { 14.0f + gx, 6.0f, 42.0f, 48.0f }, col::bg, phase);
            logo::drawWordmark (g, { 60.0f, 8.0f, 150.0f, 46.0f }, col::bg);
            auto tr = juce::Rectangle<float> (216.0f, 22.0f, 62.0f, 18.0f);
            g.setColour (col::ecto); g.drawRoundedRectangle (tr, 5, 1.2f);
            g.setFont (font (10.0f)); g.drawText ("SYNTH", tr, juce::Justification::centred);
        }
        g.setColour (col::dim); g.setFont (font (10.0f));
        g.drawText ("WAVE / draw on it", juce::Rectangle<float> (20, 60, 260, 16), juce::Justification::centredLeft);
        g.drawText ("GHOSTS / drag & hold / double-click to swap", juce::Rectangle<float> (600, 60, 484, 16), juce::Justification::centredLeft);

        for (auto& pn : panels)
        {
            auto r = pn.r.toFloat();
            g.setColour (col::card.withAlpha (0.92f)); g.fillRoundedRectangle (r, 12);
            g.setColour (pn.c.withAlpha (0.22f)); g.drawRoundedRectangle (r, 12, 1.0f);
            g.setColour (pn.c.withAlpha (0.85f)); g.setFont (font (10.5f));
            g.drawText (pn.title, r.reduced (12, 0).removeFromTop (24), juce::Justification::centredLeft);
        }
        // LFO captions
        g.setColour (col::dim); g.setFont (font (9.5f));
        for (auto* c : { &lfoShapeBox, &lfoDestBox, &lfoDivBox })
            g.drawText (c == &lfoShapeBox ? "SHAPE" : c == &lfoDestBox ? "TARGET" : "SYNC DIVISION", c->getBounds().translated (0, -14).withHeight (12).toFloat(), juce::Justification::centredLeft);

        // play card
        auto pc = playCard.toFloat();
        g.setColour (col::card.withAlpha (0.92f)); g.fillRoundedRectangle (pc, 14);
        g.setColour (col::slime.withAlpha (0.25f)); g.drawRoundedRectangle (pc, 14, 1.0f);
        g.setColour (col::dim); g.setFont (font (10.0f));
        g.drawText ("PLAY", juce::Rectangle<float> (pc.getX() + 12, (float) keyBox.getY(), 44, (float) keyBox.getHeight()), juce::Justification::centredLeft);
        // arp step light
        auto led = juce::Rectangle<float> ((float) arpBtn.getRight() - 12, (float) arpBtn.getY() + 4, 6, 6);
        g.setColour (col::slime.withAlpha (0.15f + arpFlash * 0.85f)); g.fillEllipse (led);
    }

    void resized() override
    {
        // header
        prevBtn.setBounds (300, 14, 34, 32); presetBox.setBounds (338, 14, 300, 32); nextBtn.setBounds (642, 14, 34, 32);
        saveBtn.setBounds (692, 14, 72, 32); loadBtn.setBounds (770, 14, 72, 32); initBtn.setBounds (848, 14, 72, 32);
        proPill.setBounds (952, 12, 132, 36);
        proPanel.setBounds (getLocalBounds());
        // wave + ghosts
        waveABox.setBounds (20, 78, 130, 28); waveBBox.setBounds (156, 78, 130, 28);
        editA.setBounds (296, 78, 88, 28); editB.setBounds (390, 78, 88, 28); diceBtn.setBounds (484, 78, 96, 28);
        wave.setBounds (20, 112, 560, 240);
        pad.setBounds (600, 78, 484, 274);

        panels.clear();
        auto panel = [this] (int x, int y, int w, int h, const char* title, juce::Colour c) { panels.push_back ({ { x, y, w, h }, title, c }); return juce::Rectangle<int> (x, y, w, h); };
        auto placeKnobs = [] (std::vector<GhostKnob*>& ks, juce::Rectangle<int> area)
        {
            const int n = (int) ks.size(); if (n == 0) return;
            const int w = area.getWidth() / n;
            for (int i = 0; i < n; ++i) ks[(size_t) i]->setBounds (area.getX() + i * w, area.getY(), w, area.getHeight());
        };
        auto knobArea = [] (juce::Rectangle<int> r) { return r.withTrimmedTop (26).withTrimmedBottom (4).reduced (6, 0); };
        constexpr int rowH = 108, gap = 12;

        // row 2: oscillator / amp envelope / filter
        int y = 362;
        auto osc = panel (16, y, 346, rowH, "OSCILLATOR", col::pink);
        placeKnobs (oscKnobs, knobArea (osc));
        legatoBtn.setBounds (osc.getRight() - 12 - 66, y + 5, 66, 18); monoBtn.setBounds (legatoBtn.getX() - 6 - 54, y + 5, 54, 18);
        auto amp = panel (osc.getRight() + gap, y, 346, rowH, "AMP ENVELOPE", col::slime);
        placeKnobs (ampKnobs, knobArea (amp));
        auto flt = panel (amp.getRight() + gap, y, W - 16 - (amp.getRight() + gap), rowH, "FILTER", col::ice);
        placeKnobs (filtKnobs, knobArea (flt));
        fTypeBox.setBounds (flt.getRight() - 12 - 120, y + 4, 120, 20);

        // row 3: filter envelope / LFO
        y += rowH + gap - 4;
        auto fe = panel (16, y, 346, rowH, "FILTER ENVELOPE", col::ice);
        placeKnobs (fenvKnobs, knobArea (fe));
        linkBtn.setBounds (fe.getRight() - 12 - 110, y + 5, 110, 18);
        auto lfo = panel (fe.getRight() + gap, y, W - 16 - (fe.getRight() + gap), rowH, "LFO", col::ecto);
        placeKnobs (lfoKnobs, knobArea (lfo).withWidth (180));
        {
            const int cx = lfo.getX() + 6 + 180 + 20, cw = 140, cg = 14, cy = y + 52;
            lfoShapeBox.setBounds (cx, cy, cw, 28);
            lfoDestBox.setBounds (cx + cw + cg, cy, cw + 20, 28);
            syncBtn.setBounds (cx + 2 * cw + 20 + 2 * cg, cy, 64, 28);
            lfoDivBox.setBounds (syncBtn.getRight() + 8, cy, 92, 28);
        }

        // row 4: effects / mod / output
        y += rowH + gap - 4;
        auto fxp = panel (16, y, 640, rowH, "EFFECTS", col::gold);
        placeKnobs (fxKnobs, knobArea (fxp));
        auto mod = panel (fxp.getRight() + gap, y, 240, rowH, "MOD WHEEL / VELOCITY", col::pink);
        placeKnobs (modKnobs, knobArea (mod));
        auto out = panel (mod.getRight() + gap, y, W - 16 - (mod.getRight() + gap), rowH, "OUTPUT", col::slime);
        placeKnobs (outKnobs, knobArea (out));

        // play card
        y += rowH + gap - 4;
        playCard = { 16, y, W - 32, H - 10 - y };
        const int ry = y + 10;
        keyBox.setBounds (66, ry, 62, 28); scaleBox.setBounds (134, ry, 160, 28); chordBox.setBounds (300, ry, 140, 28);
        arpBtn.setBounds (460, ry, 72, 28); rateBox.setBounds (538, ry, 86, 28); modeBox.setBounds (630, ry, 116, 28); octBox.setBounds (752, ry, 110, 28);
        arpKnobs[0]->setBounds (playCard.getRight() - 96, y + 6, 88, playCard.getHeight() - 12);
        flow.setBounds (26, ry + 36, arpKnobs[0]->getX() - 10 - 26, playCard.getBottom() - 10 - (ry + 36));
    }

private:
    SynthProcessor& proc;
    WaveScreen wave;
    ghosty::XYGhostPad pad;
    FlowStrip flow;
    juce::ComboBox presetBox, waveABox, waveBBox, keyBox, scaleBox, chordBox, rateBox, modeBox, octBox, fTypeBox, lfoDivBox, lfoShapeBox, lfoDestBox;
    juce::TextButton arpBtn { "ARP" }, monoBtn { "MONO" }, legatoBtn { "LEGATO" }, linkBtn { "LINK TO AMP" }, syncBtn { "SYNC" };
    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, saveBtn { "SAVE" }, loadBtn { "LOAD" }, initBtn { "INIT" };
    juce::TextButton editA { "DRAW A" }, editB { "DRAW B" }, diceBtn { "DICE" };
    ProPill proPill;
    ProPanel proPanel;       // declared after everything else it covers, so it sits on top
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAtts;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAtts;
    juce::OwnedArray<GhostKnob> knobs;
    std::vector<GhostKnob*> oscKnobs, ampKnobs, filtKnobs, fenvKnobs, lfoKnobs, fxKnobs, modKnobs, outKnobs, arpKnobs;
    struct Panel { juce::Rectangle<int> r; juce::String title; juce::Colour c; };
    std::vector<Panel> panels;
    juce::Rectangle<int> playCard;
    struct BgGhost { GhostLook look; float a, r, sp, sz, al; };
    std::vector<BgGhost> bg;
    float phase = 0, wordGlitch = 0, arpFlash = 0;
    int lastArpCount = 0;
    static constexpr int userBase = 1001, proTeaserId = 900000;
    juce::Array<juce::File> userFiles;

    void refreshPresetBox()
    {
        presetBox.clear (juce::dontSendNotification);
        juce::String lastCat;
        for (int i = 0; i < proc.getNumPrograms(); ++i)
        {
            const auto cat = SynthProcessor::getPresetCategory (i);
            if (cat != lastCat) { presetBox.addSectionHeading (cat.toUpperCase()); lastCat = cat; }
            presetBox.addItem (proc.getProgramName (i), i + 1);
        }
        userFiles = proc.listUserPresets();
        if (! userFiles.isEmpty())
        {
            presetBox.addSectionHeading ("YOUR SOUNDS");
            for (int i = 0; i < userFiles.size(); ++i) presetBox.addItem (userFiles[i].getFileNameWithoutExtension(), userBase + i);
        }
        // one quiet, greyed line at the very end; picking it just shows what Pro adds
        presetBox.addSeparator();
        juce::PopupMenu::Item teaser ("+ 245 more in Ghosty Pro");
        teaser.itemID = proTeaserId;
        teaser.colour = col::dim;
        presetBox.getRootMenu()->addItem (std::move (teaser));
        syncPresetBox();
    }

    void syncPresetBox()
    {
        const auto name = proc.patchName();
        int want = 0;
        if (proc.isUserPatch()) { for (int i = 0; i < userFiles.size(); ++i) if (userFiles[i].getFileNameWithoutExtension() == name) want = userBase + i; }
        else if (name.isEmpty()) want = proc.getCurrentProgram() + 1;
        if (want != 0) { if (presetBox.getSelectedId() != want) presetBox.setSelectedId (want, juce::dontSendNotification); }
        else if (presetBox.getText() != name || presetBox.getSelectedId() != 0) presetBox.setText (name, juce::dontSendNotification);
    }

    void askSave()
    {
        auto* w = new juce::AlertWindow ("Save sound", "Saves every knob and both drawn waves to\n" + SynthProcessor::userPresetDir().getFullPathName(), juce::MessageBoxIconType::NoIcon, this);
        const auto current = proc.isUserPatch() ? proc.patchName() : juce::String ("My Ghost");
        w->addTextEditor ("name", current, "Name");
        w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<SynthContent> safe (this);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result)
        {
            if (result != 1 || safe == nullptr) return;
            const auto name = w->getTextEditorContents ("name").trim();
            if (name.isEmpty()) return;
            auto target = SynthProcessor::userPresetDir().getChildFile (juce::File::createLegalFileName (name) + ".ghostysound");
            auto doSave = [safe, name]
            {
                if (safe == nullptr) return;
                if (! safe->proc.saveUserPreset (name))
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Ghosty", "Could not save the sound.");
                safe->refreshPresetBox(); safe->wordGlitch = 1;
            };
            if (target.existsAsFile())
                juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Replace?", "\"" + target.getFileNameWithoutExtension() + "\" already exists. Replace it?",
                                                    "Replace", "Cancel", nullptr, juce::ModalCallbackFunction::create ([doSave] (int r) { if (r == 1) doSave(); }));
            else doSave();
        }), true);
    }

    void showPro()
    {
        proPanel.setVisible (true);
        proPanel.toFront (true);
        proPanel.grabKeyboardFocus();
    }

    void showLoadMenu()
    {
        userFiles = proc.listUserPresets();
        juce::PopupMenu m;
        m.addSectionHeader ("YOUR SOUNDS");
        if (userFiles.isEmpty()) m.addItem (-1, "(nothing saved yet - press SAVE)", false);
        for (int i = 0; i < userFiles.size(); ++i) m.addItem (i + 1, userFiles[i].getFileNameWithoutExtension(), true, proc.isUserPatch() && userFiles[i].getFileNameWithoutExtension() == proc.patchName());
        m.addSeparator();
        m.addItem (100000, "Open sounds folder");
        juce::Component::SafePointer<SynthContent> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&loadBtn), [safe] (int r)
        {
            if (safe == nullptr || r == 0) return;
            if (r == 100000) { SynthProcessor::userPresetDir().startAsProcess(); return; }
            if (r >= 1 && r <= safe->userFiles.size()) { safe->proc.loadUserPreset (safe->userFiles[r - 1]); safe->refreshPresetBox(); safe->wordGlitch = 1; }
        });
    }

    void timerCallback() override
    {
        phase += 1.0f / 24.0f;
        for (auto& q : bg) q.a += q.sp * (1.0f + proc.level.load() * 4.0f) * 0.04f;
        wordGlitch *= 0.85f;
        const int ac = proc.performer.arpCounter.load();
        if (ac != lastArpCount) { lastArpCount = ac; arpFlash = 1.0f; } else arpFlash *= 0.75f;
        syncPresetBox();
        // dim what is not in use
        const bool link = proc.fLinkP->load() > 0.5f, sync = proc.lfoSyncP->load() > 0.5f, mono = proc.monoP->load() > 0.5f;
        for (auto* k : fenvKnobs) { k->setEnabled (! link); k->setAlpha (link ? 0.35f : 1.0f); }
        lfoKnobs[0]->setEnabled (! sync); lfoKnobs[0]->setAlpha (sync ? 0.35f : 1.0f);
        lfoDivBox.setEnabled (sync); lfoDivBox.setAlpha (sync ? 1.0f : 0.4f);
        legatoBtn.setEnabled (mono); legatoBtn.setAlpha (mono ? 1.0f : 0.4f);
        repaint();
    }
};

// ------------------------------------------------------------------ editor
SynthEditor::SynthEditor (SynthProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    content = std::make_unique<SynthContent> (p, lnf);
    addAndMakeVisible (*content);
    content->setBounds (0, 0, W, H);
    setResizable (true, true);
    setResizeLimits (W * 6 / 10, H * 6 / 10, W * 8 / 5, H * 8 / 5);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) W / H);
    setSize (W, H);
}

SynthEditor::~SynthEditor() { content.reset(); setLookAndFeel (nullptr); }

void SynthEditor::paint (juce::Graphics& g) { g.fillAll (col::bg); }

void SynthEditor::resized()
{
    if (content) content->setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) W));
}
