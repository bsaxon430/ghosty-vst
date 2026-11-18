// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST ATLAS - the Layers page: the layers down the left, and everything about the one in hand on the right
// (its sample, pitch and range, shape and tone, motion, level and sends, and its two effects).
#include "Pages.h"

namespace atlas::ui
{
namespace
{
// ============================================================================================ one layer, on the left
// (a row is 72 high while eight or nine are shown; with more it is lower, and then leaves out its second line)
class LayerRow final : public juce::Component
{
public:
    LayerRow (Shell& s, int i) : shell (s), index (i)
    {
        bindLayer (level, shell, index, lpGain);
        level.label.clear(); level.labelWidth = 0.0f; level.valueWidth = 62.0f;
        level.usable = [this] { return used; };
        addAndMakeVisible (level);
        on.get = [this] { return shell.proc.engine().getLayerParam (index, lpOn); };
        on.set = [this] (float v) { shell.proc.engine().setLayerParam (index, lpOn, v); if (shell.soundChanged) shell.soundChanged(); };
        on.usable = [this] { return hasInstrument; };
        on.setTooltip ("Switch this layer on or off (off also frees its memory)");
        addAndMakeVisible (on);
        auto flag = [this] (TextBtn& b, const char* text, int param, const char* tip) {
            b.text = text; b.size = 13.0f;
            b.usable = [this] { return used; };
            b.onClick = [this, param] { auto& e = shell.proc.engine(); e.setLayerParam (index, param, e.getLayerParam (index, param) > 0.5f ? 0.0f : 1.0f); if (shell.soundChanged) shell.soundChanged(); };
            b.setTooltip (tip);
            addAndMakeVisible (b);
        };
        flag (mute, "M", lpMute, "Mute this layer");
        flag (solo, "S", lpSolo, "Hear only this layer");
    }
    bool tall() const { return getHeight() >= 60; }
    void resized() override
    {
        if (tall())
        {
            on.setBounds (getWidth() - 46, 8, 36, 22);
            level.setBounds (44, 40, getWidth() - 44 - 70, 22);
            mute.setBounds (getWidth() - 62, 39, 25, 24);
            solo.setBounds (getWidth() - 34, 39, 25, 24);
        }
        else
        {
            const int y = getHeight() - 24;
            on.setBounds (getWidth() - 44, 2, 34, 18);
            level.setBounds (44, y, getWidth() - 44 - 70, 20);
            mute.setBounds (getWidth() - 62, y - 1, 25, 22);
            solo.setBounds (getWidth() - 34, y - 1, 25, 22);
        }
    }
    void read (const LayerState& ls, const juce::String& shownName, const juce::String& shownKind)
    {
        hasInstrument = ls.instrument.isNotEmpty(); used = ls.used();
        name = shownName; kind = shownKind;
        mute.selected = ls.p[lpMute] > 0.5f; solo.selected = ls.p[lpSolo] > 0.5f;
        repaint(); level.repaint(); on.repaint(); mute.repaint(); solo.repaint();
    }
    void setMeter (float v) { if (std::abs (v - meter) > 0.01f) { meter = v; repaint (0, 0, 44, getHeight()); } }
    void paint (juce::Graphics& g) override
    {
        const auto& l = look();
        const bool sel = shell.layer == index;
        const auto b = getLocalBounds().toFloat().reduced (2.0f, 2.0f);
        if (sel) { g.setColour (l.lantern.withAlpha (0.13f)); g.fillRoundedRectangle (b, 9.0f); g.setColour (l.lantern.withAlpha (0.5f)); g.drawRoundedRectangle (b.reduced (0.5f), 9.0f, 1.0f); }
        else if (hover) { g.setColour (l.text (0.06f)); g.fillRoundedRectangle (b, 9.0f); }
        const juce::Point<float> at (22.0f, tall() ? 21.0f : (float) getHeight() * 0.5f);
        if (used) glow (g, at, 12.0f + 20.0f * meter, l.lantern, 0.25f + 0.55f * meter);
        g.setColour (used ? l.lantern.interpolatedWith (juce::Colours::white, 0.5f * meter) : l.text (0.16f));
        g.fillEllipse (at.x - 10.0f, at.y - 10.0f, 20.0f, 20.0f);
        g.setColour (used ? l.ink() : l.text (0.6f)); g.setFont (sansBold (12.5f));
        g.drawText (juce::String (index + 1), juce::Rectangle<float> (at.x - 10.0f, at.y - 10.0f, 20.0f, 20.0f), juce::Justification::centred, false);
        g.setColour (sel ? l.lantern : l.text (hasInstrument ? 0.95f : 0.5f));
        drawFitted (g, hasInstrument ? name : juce::String ("Empty"), { 44.0f, tall() ? 4.0f : 2.0f, (float) getWidth() - 96.0f, 20.0f }, juce::Justification::centredLeft, sel ? sansBold (15.5f) : sans (15.5f), 0.72f);
        if (! tall()) return;
        g.setColour (l.text (0.55f));
        drawFitted (g, kind, { 44.0f, 22.0f, (float) getWidth() - 96.0f, 16.0f }, juce::Justification::centredLeft, sans (12.5f), 0.75f);
    }
    void mouseDown (const juce::MouseEvent&) override { shell.layer = index; if (shell.soundChanged) shell.soundChanged(); }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }

private:
    Shell& shell; int index;
    Bar level; Switch on; TextBtn mute, solo;
    juce::String name, kind;
    bool hasInstrument = false, used = false, hover = false;
    float meter = 0.0f;
};

// ============================================================================================ the page
class LayersPage final : public Page
{
public:
    explicit LayersPage (Shell& s) : Page (s)
    {
        for (int i = 0; i < kLayers; ++i) { rows.add (new LayerRow (shell, i)); addAndMakeVisible (rows.getLast()); }

        change.text = "Change instrument"; change.onClick = [this] { if (shell.pickInstrument) shell.pickInstrument (shell.layer); };
        remove.text = "Remove"; remove.usable = [this] { return src.instrument.isNotEmpty(); };
        remove.onClick = [this] { LayerState blank; shell.proc.engine().setLayerSource (shell.layer, blank); for (int p = 0; p < kNumLayerParams; ++p) if (p != lpGain) shell.proc.engine().setLayerParam (shell.layer, p, blank.p[(size_t) p]); shell.proc.setHost (Processor::hpLevel1 + shell.layer, 0.0f); if (shell.soundChanged) shell.soundChanged(); };
        addAndMakeVisible (change); addAndMakeVisible (remove);

        const auto has = [this] { return src.used(); };

        // ---- the sample
        loopMode.choices = { "As made", "Looped", "One shot" };
        bindSource (loopMode, [] (const LayerState& l) { return (float) l.loopMode; }, [] (LayerState& l, float v) { l.loopMode = (int) v; });
        loopMode.usable = has;
        reverse.label = "Backwards";
        bindSource (reverse, [] (const LayerState& l) { return l.reverse ? 1.0f : 0.0f; }, [] (LayerState& l, float v) { l.reverse = v > 0.5f; });
        reverse.usable = has;
        voices.label = "Voices"; voices.labelWidth = 58.0f; voices.menuParent = shell.canvas; voices.choices = { "8", "16", "32", "64" };
        bindSource (voices, [] (const LayerState& l) { return l.voices <= 8 ? 0.0f : (l.voices <= 16 ? 1.0f : (l.voices <= 32 ? 2.0f : 3.0f)); }, [] (LayerState& l, float v) { static const int n[4] = { 8, 16, 32, 64 }; l.voices = n[juce::jlimit (0, 3, (int) v)]; });
        voices.usable = has;
        auto loopBar = [this] (Bar& b, const char* name, float lo, float hi, bool log, float LayerState::* field, std::function<juce::String (float)> text) {
            b.label = name; b.labelWidth = 84.0f; b.valueWidth = 54.0f; b.lo = lo; b.hi = hi; b.log = log; b.text = std::move (text);
            bindSource (b, [field] (const LayerState& l) { return l.*field; }, [field] (LayerState& l, float v) { l.*field = v; });
            b.usable = [this] { return src.used() && src.loopMode == 1; };
            b.setTooltip ("Used when the sample is looped");
            addAndMakeVisible (b);
        };
        const auto percent = [] (float v) { return juce::String ((int) std::lround (v * 100.0f)) + " %"; };
        loopBar (loopStart, "Loop from", 0.0f, 0.95f, false, &LayerState::loopStart, percent); loopStart.def = 0.35f;
        loopBar (loopEnd, "Loop to", 0.05f, 1.0f, false, &LayerState::loopEnd, percent); loopEnd.def = 0.94f;
        loopBar (loopFade, "Crossfade", 0.005f, 1.0f, true, &LayerState::loopFade, [] (float v) { return juce::String (v, 2) + " s"; }); loopFade.def = 0.2f;
        startReach.label = "Start reach"; startReach.labelWidth = 84.0f; startReach.menuParent = shell.canvas; startReach.choices = { "Off", "Half a second", "2 seconds", "5 seconds" };
        bindSource (startReach, [] (const LayerState& l) { return l.startRange <= 0.0f ? 0.0f : (l.startRange <= 0.5f ? 1.0f : (l.startRange <= 2.0f ? 2.0f : 3.0f)); },
                    [] (LayerState& l, float v) { static const float r[4] = { 0.0f, 0.5f, 2.0f, 5.0f }; l.startRange = r[juce::jlimit (0, 3, (int) v)]; });
        startReach.usable = has;
        startReach.setTooltip ("How far into each sample the Start control can reach");
        for (auto* c : std::initializer_list<juce::Component*> { &loopMode, &reverse, &voices, &startReach }) addAndMakeVisible (*c);

        // ---- pitch and range
        static const int strip[] = { lpCoarse, lpFine, lpVelSens, lpVelCurve, lpKeyLo, lpKeyHi, lpVelLo, lpVelHi };
        for (int i = 0; i < 8; ++i) { bindLayer (range[(size_t) i], shell, -1, strip[i]); range[(size_t) i].usable = has; range[(size_t) i].labelSize = 13.5f; addAndMakeVisible (range[(size_t) i]); }
        auto fade = [this, has] (Knob& k, const char* name, float hi, float LayerState::* field, const char* unit) {
            k.label = name; k.lo = 0.0f; k.hi = hi; k.whole = true; k.labelSize = 13.5f;
            k.text = [unit] (float v) { return v < 1.0f ? juce::String ("off") : juce::String ((int) v) + unit; };
            bindSource (k, [field] (const LayerState& l) { return l.*field; }, [field] (LayerState& l, float v) { l.*field = v; });
            k.usable = has;
            addAndMakeVisible (k);
        };
        fade (keyIn, "Key fade in", 24.0f, &LayerState::keyFadeIn, ""); fade (keyOut, "Key fade out", 24.0f, &LayerState::keyFadeOut, "");
        fade (velIn, "Vel fade in", 60.0f, &LayerState::velFadeIn, ""); fade (velOut, "Vel fade out", 60.0f, &LayerState::velFadeOut, "");
        keyIn.setTooltip ("Keys over which the layer fades in above its low key"); keyOut.setTooltip ("Keys over which it fades out towards its high key");
        velIn.setTooltip ("Velocity steps over which the layer fades in"); velOut.setTooltip ("Velocity steps over which it fades out");

        // ---- shape and tone
        bindLayer (attack, shell, -1, lpAttack); bindLayer (release, shell, -1, lpRelease); bindLayer (start, shell, -1, lpStart);
        attack.usable = has; release.usable = has;
        start.usable = [this] { return src.used() && src.startRange > 0.0f; };
        start.setTooltip ("Where in the sample a note begins. Set Start reach first");
        bindLayer (filter, shell, -1, lpFilterType); filter.choices = { "Off", "Low", "Low 24", "High", "Band" }; filter.usable = has;
        filter.set = [this] (float v) { shell.proc.engine().setLayerParam (shell.layer, lpFilterType, v); repaintControls(); };
        bindLayer (cutoff, shell, -1, lpCutoff); bindLayer (reso, shell, -1, lpReso); bindLayer (width, shell, -1, lpWidth);
        const auto filtering = [this] { return src.used() && shell.proc.engine().getLayerParam (shell.layer, lpFilterType) > 0.5f; };
        cutoff.usable = filtering; reso.usable = filtering; width.usable = has;
        cutoff.setTooltip ("Used when the filter is on"); reso.setTooltip ("Used when the filter is on");
        for (auto* c : std::initializer_list<juce::Component*> { &attack, &release, &start, &filter, &cutoff, &reso, &width }) addAndMakeVisible (*c);

        // ---- motion
        bindLayer (lfoShape, shell, -1, lpLfoShape); lfoShape.usable = has;
        bindLayer (lfoSync, shell, -1, lpLfoSync); lfoSync.label = "In time"; lfoSync.usable = has;
        lfoSync.set = [this] (float v) { shell.proc.engine().setLayerParam (shell.layer, lpLfoSync, v); repaintControls(); };
        bindLayer (lfoRate, shell, -1, lpLfoRate); lfoRate.usable = [this] { return src.used() && shell.proc.engine().getLayerParam (shell.layer, lpLfoSync) < 0.5f; };
        lfoRate.setTooltip ("Used when the motion is not in time with the song");
        bindLayer (lfoDiv, shell, -1, lpLfoDiv); lfoDiv.label.clear(); lfoDiv.menuParent = shell.canvas;
        lfoDiv.usable = [this] { return src.used() && shell.proc.engine().getLayerParam (shell.layer, lpLfoSync) > 0.5f; };
        lfoDiv.setTooltip ("Used when the motion is in time with the song");
        static const int aims[] = { lpLfoAmp, lpLfoPan, lpLfoCutoff, lpLfoPitch };
        for (int i = 0; i < 4; ++i) { bindLayer (lfoTo[(size_t) i], shell, -1, aims[i]); lfoTo[(size_t) i].usable = has; addAndMakeVisible (lfoTo[(size_t) i]); }
        lfoTo[2].usable = filtering; lfoTo[2].setTooltip ("Used when the filter is on");
        for (auto* c : std::initializer_list<juce::Component*> { &lfoShape, &lfoSync, &lfoRate, &lfoDiv }) addAndMakeVisible (*c);

        // ---- level and sends
        bindLayer (level, shell, -1, lpGain); bindLayer (pan, shell, -1, lpPan);
        level.usable = has; pan.usable = has;
        static const int sendParams[] = { lpSendA, lpSendB, lpSendC };
        for (int i = 0; i < kSends; ++i)
        {
            bindLayer (sends[(size_t) i], shell, -1, sendParams[i]);
            sends[(size_t) i].usable = [this, i] { return src.used() && sendUnit[(size_t) i].isNotEmpty(); };
            addAndMakeVisible (sends[(size_t) i]);
        }
        addAndMakeVisible (level); addAndMakeVisible (pan);

        // ---- its two effects
        for (int k = 0; k < kInserts; ++k)
        {
            auto& u = insUnit[(size_t) k];
            u.menuParent = shell.canvas;
            u.caption = [this, k] { return src.inserts[(size_t) k].unit.isEmpty() ? juce::String ("No effect") : named (fxTitle (src.inserts[(size_t) k].unit)); };
            u.fill = [this, k] (juce::PopupMenu& m) {
                m.addItem (1, "No effect", true, src.inserts[(size_t) k].unit.isEmpty());
                static const char* groups[] = { "Rhythm", "Colour", "Space", "Master" };
                for (int grp = 0; grp < 4; ++grp)
                {
                    m.addSectionHeader (groups[grp]);
                    int id = 2;
                    for (const auto& unit : fxUnits()) { if (unit.group == grp) m.addItem (id, named (unit.title), true, unit.key == src.inserts[(size_t) k].unit); ++id; }
                }
            };
            u.picked = [this, k] (int id) {
                const auto key = id <= 1 ? juce::String() : fxUnits()[(size_t) (id - 2)].key;
                if (key == src.inserts[(size_t) k].unit) return;
                src.inserts[(size_t) k] = key.isEmpty() ? FxState() : fxDefault (key);
                shell.proc.engine().setLayerSource (shell.layer, src);
                if (shell.soundChanged) shell.soundChanged();
            };
            u.usable = has;
            auto& sw = insOn[(size_t) k];
            sw.get = [this, k] { return src.inserts[(size_t) k].on ? 1.0f : 0.0f; };
            sw.set = [this, k] (float v) { src.inserts[(size_t) k].on = v > 0.5f; shell.proc.engine().setFxOn (FxAddress::insert (shell.layer, k), v > 0.5f); };
            sw.usable = [this, k] { return src.used() && src.inserts[(size_t) k].unit.isNotEmpty(); };
            auto& mx = insMix[(size_t) k];
            mx.label = "Mix"; mx.labelWidth = 40.0f; mx.valueWidth = 50.0f; mx.def = 1.0f;
            mx.text = [] (float v) { return juce::String ((int) std::lround (v * 100.0f)) + " %"; };
            mx.get = [this, k] { return src.inserts[(size_t) k].mix; };
            mx.set = [this, k] (float v) { src.inserts[(size_t) k].mix = v; shell.proc.engine().setFxMix (FxAddress::insert (shell.layer, k), v); };
            mx.usable = [this, k] { return src.used() && src.inserts[(size_t) k].unit.isNotEmpty() && src.inserts[(size_t) k].on; };
            auto& ed = insEdit[(size_t) k];
            ed.text = "Open"; ed.usable = [this, k] { return src.inserts[(size_t) k].unit.isNotEmpty(); };
            ed.onClick = [this, k] { shell.fx = FxAddress::insert (shell.layer, k); if (shell.goTo) shell.goTo (pageEffects); };
            ed.setTooltip ("Open this effect's own controls");
            for (auto* c : std::initializer_list<juce::Component*> { &u, &sw, &mx, &ed }) addAndMakeVisible (*c);
        }
        refresh();
    }

    std::vector<Stage::Pane> panes() const override
    {
        return { { { 0.0f, 0.0f, (float) kBaseW, 104.0f }, 0.0f, Stage::scrimTop },
                 { { 16.0f, 68.0f, 300.0f, 636.0f }, 14.0f, Stage::glass },
                 { { 332.0f, 68.0f, 972.0f, 636.0f }, 14.0f, Stage::glass } };
    }

    void resized() override
    {
        layoutRows();
        remove.setBounds (1204, 82, 80, 30); change.setBounds (1050, 82, 146, 30);
        const int c1 = 356, c2 = 680, c3 = 1004, w = 276;
        // the sample
        loopMode.setBounds (c1, 180, w, 30);
        reverse.setBounds (c1, 220, 132, 26); voices.setBounds (c1 + 140, 219, 136, 28);
        loopStart.setBounds (c1, 258, w, 22); loopEnd.setBounds (c1, 284, w, 22); loopFade.setBounds (c1, 310, w, 22);
        startReach.setBounds (c1, 344, w, 28);
        // pitch and range: three rows of four
        static const int order[12] = { 0, 1, 2, 3, 4, 5, -1, -2, 6, 7, -3, -4 };
        for (int i = 0; i < 12; ++i)
        {
            Knob* k = order[i] >= 0 ? &range[(size_t) order[i]] : (order[i] == -1 ? &keyIn : (order[i] == -2 ? &keyOut : (order[i] == -3 ? &velIn : &velOut)));
            k->setBounds (c2 + (i % 4) * 69, 176 + (i / 4) * 72, 66, 70);
        }
        // shape and tone
        attack.setBounds (c3, 176, 86, 76); release.setBounds (c3 + 95, 176, 86, 76); start.setBounds (c3 + 190, 176, 86, 76);
        filter.setBounds (c3, 262, w, 30);
        cutoff.setBounds (c3, 302, 86, 76); reso.setBounds (c3 + 95, 302, 86, 76); width.setBounds (c3 + 190, 302, 86, 76);
        // motion
        lfoShape.setBounds (c1, 446, w, 30);
        lfoSync.setBounds (c1, 494, 104, 26); lfoRate.setBounds (c1 + 112, 484, 70, 74); lfoDiv.setBounds (c1 + 190, 498, 86, 28);
        for (int i = 0; i < 4; ++i) lfoTo[(size_t) i].setBounds (c1 + i * 69, 578, 66, 76);
        // level and sends
        level.setBounds (c2, 446, 100, 96); pan.setBounds (c2 + 110, 456, 76, 80);
        for (int i = 0; i < kSends; ++i) sends[(size_t) i].setBounds (c2 + i * 92, 574, 84, 80);
        // effects
        for (int k = 0; k < kInserts; ++k)
        {
            const int y = 446 + k * 106;
            insUnit[(size_t) k].setBounds (c3, y, 166, 30); insOn[(size_t) k].setBounds (c3 + 174, y + 4, 36, 22); insEdit[(size_t) k].setBounds (c3 + 218, y, 58, 30);
            insMix[(size_t) k].setBounds (c3, y + 40, w, 22);
        }
    }

    // the slots shown (see layerSlotsShown) share the height eight rows of 78 always had
    void layoutRows()
    {
        const int pitch = juce::jmin (78, 624 / juce::jmax (1, slots));
        for (int i = 0; i < kLayers; ++i)
        {
            rows[i]->setVisible (i < slots);
            rows[i]->setBounds (22, 76 + i * pitch, 288, pitch - 6);
        }
    }

    void refresh() override
    {
        const auto p = shell.proc.capture();
        auto& lib = shell.proc.library();
        src = p.layers[(size_t) shell.layer];
        int last = -1;
        for (int i = 0; i < kLayers; ++i) if (p.layers[(size_t) i].instrument.isNotEmpty() || i == shell.layer) last = i;
        if (layerSlotsShown (last) != slots) { slots = layerSlotsShown (last); layoutRows(); }
        for (int i = 0; i < kLayers; ++i)
        {
            const auto& ls = p.layers[(size_t) i];
            Instrument inst; const bool known = ls.instrument.isNotEmpty() && lib.findLoose (ls.instrument, inst);
            rows[i]->read (ls, ls.label.isNotEmpty() ? ls.label : shortName (ls.instrument, shell.proc),
                           ls.instrument.isEmpty() ? juce::String ("Click, then choose an instrument") : (known ? inst.category + (ls.role.isNotEmpty() ? ", " + ls.role : juce::String()) : juce::String ("Not in the library")));
        }
        Instrument inst;
        const bool known = src.instrument.isNotEmpty() && lib.findLoose (src.instrument, inst);
        title = src.instrument.isEmpty() ? "Layer " + juce::String (shell.layer + 1) + " is empty" : (src.label.isNotEmpty() ? src.label : shortName (src.instrument, shell.proc));
        about = src.instrument.isEmpty() ? juce::String ("Choose an instrument to give it a sound.")
              : known ? inst.category + ", from " + inst.libraryName + (inst.license.isNotEmpty() ? " (" + inst.license + ")" : juce::String()) : juce::String();
        problem = shell.proc.engine().layerProblem (shell.layer);
        if (! src.used() && src.instrument.isNotEmpty()) problem = "This layer is switched off.";
        change.text = src.instrument.isEmpty() ? "Choose an instrument" : "Change instrument";
        change.style = src.instrument.isEmpty() ? TextBtn::lit : TextBtn::plain;
        for (int i = 0; i < kSends; ++i)
        {
            sendUnit[(size_t) i] = p.sends[(size_t) i].unit;
            sends[(size_t) i].label = sendUnit[(size_t) i].isEmpty() ? "Send " + juce::String::charToString ((juce::juce_wchar) ('A' + i)) : "To " + named (fxTitle (sendUnit[(size_t) i]));
            sends[(size_t) i].setTooltip (sendUnit[(size_t) i].isEmpty() ? "This send has no effect on it (see Effects)" : juce::String());
        }
        repaintControls();
    }

    void tick() override
    {
        auto& e = shell.proc.engine();
        for (int i = 0; i < kLayers; ++i) rows[i]->setMeter (juce::jlimit (0.0f, 1.0f, std::sqrt (e.layerLevel (i)) * 1.5f));
        const auto now = e.layerProblem (shell.layer);
        if (src.used() && now != problem) { problem = now; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        const auto& l = look();
        g.setColour (l.text (0.98f));
        drawFitted (g, title, { 356.0f, 76.0f, 680.0f, 40.0f }, juce::Justification::centredLeft, fellRoman (32.0f), 0.7f);
        g.setColour (problem.isNotEmpty() ? l.danger : l.text (0.68f));
        drawFitted (g, problem.isNotEmpty() ? problem : about, { 357.0f, 114.0f, 900.0f, 20.0f }, juce::Justification::centredLeft, sans (14.5f), 0.75f);
        g.setColour (l.text (0.1f)); g.fillRect (356.0f, 142.0f, 924.0f, 1.0f); g.fillRect (356.0f, 410.0f, 924.0f, 1.0f);
        headingAt (g, "Sample", 356.0f, 150.0f); headingAt (g, "Pitch and range", 680.0f, 150.0f); headingAt (g, "Shape and tone", 1004.0f, 150.0f);
        headingAt (g, "Motion", 356.0f, 418.0f); headingAt (g, "Level and sends", 680.0f, 418.0f); headingAt (g, "Effects on this layer", 1004.0f, 418.0f);
        g.setColour (l.text (0.5f)); g.setFont (sans (12.5f));
        g.drawText ("what the motion moves", juce::Rectangle<float> (356.0f, 560.0f, 276.0f, 14.0f), juce::Justification::centredLeft, false);
    }

private:
    void bindSource (Control& c, std::function<float (const LayerState&)> readField, std::function<void (LayerState&, float)> writeField)
    {
        c.get = [this, readField] { return readField (src); };
        c.set = [this, writeField] (float v) { writeField (src, v); dirty = true; repaintControls(); };
        c.end = [this] { if (! dirty) return; dirty = false; shell.proc.engine().setLayerSource (shell.layer, src); if (shell.soundChanged) shell.soundChanged(); };
    }
    void repaintControls() { for (auto* c : getChildren()) c->repaint(); repaint(); }

    juce::OwnedArray<LayerRow> rows;
    int slots = 8;                               // how many of them are shown
    TextBtn change, remove;
    Segments loopMode, filter, lfoShape;
    Switch reverse, lfoSync;
    Choice voices, startReach, lfoDiv;
    Bar loopStart, loopEnd, loopFade;
    std::array<Knob, 8> range;
    Knob keyIn, keyOut, velIn, velOut, attack, release, start, cutoff, reso, width, lfoRate, level, pan;
    std::array<Knob, 4> lfoTo;
    std::array<Knob, kSends> sends;
    std::array<juce::String, kSends> sendUnit;
    std::array<Choice, kInserts> insUnit;
    std::array<Switch, kInserts> insOn;
    std::array<Bar, kInserts> insMix;
    std::array<TextBtn, kInserts> insEdit;
    LayerState src;
    juce::String title, about, problem;
    bool dirty = false;
};
} // namespace

std::unique_ptr<Page> makeLayersPage (Shell& s) { return std::make_unique<LayersPage> (s); }
} // namespace atlas::ui
