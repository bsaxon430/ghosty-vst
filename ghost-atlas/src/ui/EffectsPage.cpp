// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST ATLAS - the Effects page: the eight places an effect can sit (the two on the layer in hand, the three sends
// every layer can feed, the three on the master), and the full set of controls of the one that is open.
// The effects are Phantom FX's own units; which of a unit's controls do nothing in its current mode is asked of
// Phantom FX itself (effects/ControlUse.h), and those are greyed out with the reason as their tooltip.
// SPECTRAL FX (Poltergeist 2's rack) is one unit with parts of its own: four places in a chain, each holding one of 38
// effects with that effect's own controls, WARP CORE, and the window the chain works on. One part is shown at a time.
#include "Pages.h"
#include "effects/ControlUse.h"
#include "engine/SpectralRack.h"

namespace atlas::ui
{
namespace
{
juce::String slotName (FxAddress a)
{
    if (a.kind == FxAddress::Insert) return "Layer " + juce::String (a.layer + 1) + (a.slot == 0 ? ", first" : ", second");
    if (a.kind == FxAddress::Send) return "Send " + juce::String::charToString ((juce::juce_wchar) ('A' + a.slot));
    static const char* m[] = { "Master, first", "Master, second", "Master, last" };
    return m[juce::jlimit (0, 2, a.slot)];
}

// how a unit's value is written, from the unit it declares
juce::String written (const phantom::ParamSpec& s, float v)
{
    const juce::String u (s.unit);
    if (s.type == phantom::ParamSpec::Type::Choice) return worded (juce::String (s.choices[(size_t) juce::jlimit (0, (int) s.choices.size() - 1, (int) std::lround (v))]));
    if (s.type == phantom::ParamSpec::Type::Bool) return v > 0.5f ? "on" : "off";
    if (s.type == phantom::ParamSpec::Type::Int) return juce::String ((int) std::lround (v)) + (u.isNotEmpty() ? " " + u : juce::String());
    if (u == "%") return juce::String ((int) std::lround (v * (s.max <= 1.5f ? 100.0f : 1.0f))) + " %";
    if (u == "Hz") return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String (v, v < 20.0f ? 2 : 0) + " Hz";
    if (u == "ms") return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " s" : juce::String (v, v < 10.0f ? 1 : 0) + " ms";
    if (u == "dB") return juce::String (v, 1) + " dB";
    if (u == "s") return juce::String (v, 2) + " s";
    if (u == "st") return (v > 0 ? "+" : "") + juce::String (v, std::abs (v - std::round (v)) < 0.005f ? 0 : 1) + " st";
    if (u == "ct") return juce::String ((int) std::lround (v)) + " ct";
    return juce::String (v, std::abs (s.max - s.min) >= 20.0f ? 1 : 2) + (u.isNotEmpty() ? " " + u : juce::String());
}

// Which of a unit's controls are shown here, and under what name (none listed: all of them, under their own names).
// SPIRIT EQ has twenty-four bands of seventeen controls each; here it is five bands, switched on and flat, with a gain
// and a frequency each (the engine gives it those starting values: Engine.cpp, atlasDefaults).
struct Shown { const char* id; const char* label; };
const std::vector<Shown>& shownFor (const juce::String& unit)
{
    static const std::vector<Shown> eq = { { "1_gain", "Low Gain" }, { "1_freq", "Low Freq" }, { "2_gain", "Low Mid Gain" }, { "2_freq", "Low Mid Freq" },
                                           { "3_gain", "Mid Gain" }, { "3_freq", "Mid Freq" }, { "4_gain", "High Mid Gain" }, { "4_freq", "High Mid Freq" },
                                           { "5_gain", "High Gain" }, { "5_freq", "High Freq" }, { "out", "Output" } };
    static const std::vector<Shown> all;
    return unit == "eq" ? eq : all;
}

// a control on show: what it is declared as, and what it is called (knob >= 0: a control of an effect in SPECTRAL FX's chain)
struct Entry { phantom::ParamSpec spec; juce::String label; int effect = 0, knob = -1; };

// one of the eight places, across the top
class SlotTile final : public juce::Component
{
public:
    FxAddress address;
    juce::String unit; bool on = false, selected = false, unfed = false;      // (unfed: a send no layer is sending to)
    std::function<void()> onClick;
    void paint (juce::Graphics& g) override
    {
        const auto& l = look();
        const auto b = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (selected ? l.lantern.withAlpha (0.16f) : l.text (hover ? 0.12f : 0.07f)); g.fillRoundedRectangle (b, 10.0f);
        g.setColour (selected ? l.lantern.withAlpha (0.8f) : l.text (0.14f)); g.drawRoundedRectangle (b.reduced (0.5f), 10.0f, selected ? 1.4f : 1.0f);
        g.setColour (l.accent.withAlpha (0.9f)); g.setFont (fellCaps (15.5f));
        g.drawText (slotName (address), b.reduced (12.0f, 6.0f).removeFromTop (18.0f), juce::Justification::centredLeft, true);
        g.setColour (unit.isEmpty() ? l.text (0.42f) : l.text (on && ! unfed ? 0.96f : 0.6f));
        drawFitted (g, unit.isEmpty() ? juce::String ("Empty") : named (fxTitle (unit)) + (! on ? juce::String (" (off)") : (unfed ? juce::String (", not fed") : juce::String())),
                    b.reduced (12.0f, 6.0f).removeFromBottom (21.0f), juce::Justification::centredLeft, sans (15.5f), 0.7f);
    }
    void mouseUp (const juce::MouseEvent& e) override { if (contains (e.getPosition()) && onClick) onClick(); }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
private:
    bool hover = false;
};

class EffectsPage final : public Page
{
public:
    explicit EffectsPage (Shell& s) : Page (s)
    {
        specIn.fill (-96.0f); specOut.fill (-96.0f);
        for (int i = 0; i < 8; ++i)
        {
            auto* t = tiles.add (new SlotTile());
            t->onClick = [this, t] { shell.fx = t->address; refresh(); };
            addAndMakeVisible (t);
        }
        // the two places on a layer belong to the layer in hand: it is chosen here as well as on the Layers page
        layerPick.menuParent = shell.canvas;
        layerPick.caption = [this] { return layerTitle (shell.layer); };
        layerPick.fill = [this] (juce::PopupMenu& m) {
            const auto p = shell.proc.engine().getPreset();
            for (int l = 0; l < kLayers; ++l)
                if (p.layers[(size_t) l].used() || l == shell.layer) m.addItem (l + 1, layerTitle (l), true, l == shell.layer);
        };
        layerPick.picked = [this] (int id) { shell.layer = juce::jlimit (0, kLayers - 1, id - 1); if (shell.soundChanged) shell.soundChanged(); };
        layerPick.setTooltip ("The layer whose two effects are shown");
        addChildComponent (layerPick);

        unit.menuParent = shell.canvas;
        unit.caption = [this] { return fx.unit.isEmpty() ? juce::String ("No effect here: choose one") : named (fxTitle (fx.unit)); };
        unit.fill = [this] (juce::PopupMenu& m) {
            m.addItem (1, "No effect", true, fx.unit.isEmpty());
            static const char* groups[] = { "Rhythm", "Colour", "Space", "Master" };
            for (int grp = 0; grp < 4; ++grp)
            {
                m.addSectionHeader (groups[grp]);
                int id = 2;
                for (const auto& u : fxUnits()) { if (u.group == grp) m.addItem (id, named (u.title), true, u.key == fx.unit); ++id; }
            }
        };
        unit.picked = [this] (int id) {
            const auto key = id <= 1 ? juce::String() : fxUnits()[(size_t) (id - 2)].key;
            if (key == fx.unit) return;
            auto next = key.isEmpty() ? FxState() : fxDefault (key);
            if (shell.fx.kind == FxAddress::Send) next.mix = 1.0f;
            shell.proc.engine().setFx (shell.fx, next);
            if (shell.soundChanged) shell.soundChanged();
        };
        addAndMakeVisible (unit);

        on.label = "On";
        on.get = [this] { return fx.on ? 1.0f : 0.0f; };
        on.set = [this] (float v) { fx.on = v > 0.5f; shell.proc.engine().setFxOn (shell.fx, fx.on); for (auto* t : tiles) if (t->address == shell.fx) { t->on = fx.on; t->repaint(); } readNotice(); repaintControls(); };
        on.usable = [this] { return fx.unit.isNotEmpty(); };
        addAndMakeVisible (on);

        amount.labelWidth = 62.0f; amount.valueWidth = 54.0f;
        amount.text = [] (float v) { return juce::String ((int) std::lround (v * 100.0f)) + " %"; };
        amount.get = [this] { return shell.fx.kind == FxAddress::Send ? shell.proc.engine().getPreset().returns[(size_t) shell.fx.slot] : fx.mix; };
        amount.set = [this] (float v) { if (shell.fx.kind == FxAddress::Send) shell.proc.engine().setReturn (shell.fx.slot, v); else { fx.mix = v; shell.proc.engine().setFxMix (shell.fx, v); } };
        amount.usable = [this] { return fx.unit.isNotEmpty() && fx.on && heard; };
        addAndMakeVisible (amount);

        // ---- SPECTRAL FX: which of its parts is shown, the effect in a place, and a place's (or WARP CORE's) own switch
        rackTabs.lo = 0.0f; rackTabs.hi = (float) (spectral::kPlaces + 1); rackTabs.whole = true;       // (six parts: a control keeps what it is given inside lo..hi)
        rackTabs.get = [this] { return (float) rackPart; };
        rackTabs.set = [this] (float v) { rackPart = juce::jlimit (0, spectral::kPlaces + 1, (int) std::lround (v)); buildParams(); readNotice(); slow = 7; tick(); repaintControls(); };
        rackTabs.usable = [this] { return fx.unit.isNotEmpty(); };
        rackTabs.setComponentID ("rack-parts");
        addChildComponent (rackTabs);
        rackEffect.menuParent = shell.canvas;
        rackEffect.caption = [this] { const int e = effectAt (rackPart); return e > 0 ? spectral::effectName (e) : juce::String ("No effect here: choose one"); };
        rackEffect.fill = [this] (juce::PopupMenu& m) {
            const int now = effectAt (rackPart);
            m.addItem (1, "No effect", true, now == 0);
            for (const auto& folder : spectral::folders())
            {
                juce::PopupMenu sub; bool holds = false;
                for (int e = 1; e < spectral::numEffects(); ++e)
                    if (spectral::effectFolder (e) == folder) { sub.addItem (e + 1, spectral::effectName (e), true, e == now); holds |= e == now; }
                m.addSubMenu (named (folder), sub, true, nullptr, holds);
            }
        };
        rackEffect.picked = [this] (int id) { putEffect (rackPart, id - 1); };
        rackEffect.usable = [this] { return fx.on && heard; };
        rackEffect.setTooltip ("The effect in this place of the chain: 38 to choose from, in eight folders");
        rackEffect.setComponentID ("rack-effect");
        addChildComponent (rackEffect);
        rackOn.get = [this] { return valueOf (rackOnId()) > 0.5f ? 1.0f : 0.0f; };
        rackOn.set = [this] (float v) {
            const auto id = rackOnId();
            fx.values[id] = v > 0.5f ? 1.0f : 0.0f;
            shell.proc.engine().setFxValue (shell.fx, id, fx.values[id]);
            rackLabels(); readNotice(); slow = 7; tick(); repaintControls();
        };
        rackOn.usable = [this] { return fx.on && heard && (rackPart < spectral::kPlaces ? effectAt (rackPart) > 0 : spectral::warpAvailable()); };
        rackOn.setComponentID ("rack-on");
        addChildComponent (rackOn);
        refresh();
    }

    std::vector<Stage::Pane> panes() const override
    {
        return { { { 0.0f, 0.0f, (float) kBaseW, 104.0f }, 0.0f, Stage::scrimTop },
                 { { 16.0f, 68.0f, 1288.0f, 636.0f }, 14.0f, Stage::glass } };
    }

    void resized() override
    {
        for (int i = 0; i < tiles.size(); ++i) tiles[i]->setBounds (32 + i * 157, 82, 150, 54);
        layoutTop();
        layoutParams();
    }

    void refresh() override
    {
        const FxAddress places[8] = { FxAddress::insert (shell.layer, 0), FxAddress::insert (shell.layer, 1), FxAddress::send (0), FxAddress::send (1), FxAddress::send (2),
                                      FxAddress::master (0), FxAddress::master (1), FxAddress::master (2) };
        if (shell.fx.kind == FxAddress::Insert) shell.fx.layer = shell.layer;
        for (int i = 0; i < 8; ++i)
        {
            const auto st = shell.proc.engine().getFx (places[i]);
            tiles[i]->address = places[i]; tiles[i]->unit = st.unit; tiles[i]->on = st.on; tiles[i]->selected = places[i] == shell.fx;
            tiles[i]->unfed = places[i].kind == FxAddress::Send && ! fed (places[i].slot);
            tiles[i]->repaint();
        }
        const auto now = shell.proc.engine().getFx (shell.fx);
        const bool rebuilt = now.unit != fx.unit || ! (shown == shell.fx);
        if (rebuilt) rackPart = 0;
        fx = now; shown = shell.fx;
        if (isRack()) rackLabels();
        amount.label = shell.fx.kind == FxAddress::Send ? "Return" : "Mix";
        amount.def = shell.fx.kind == FxAddress::Send ? 0.7f : 1.0f;
        amount.hi = shell.fx.kind == FxAddress::Send ? 2.0f : 1.0f;
        buildFeeds();
        layoutTop();
        if (rebuilt) buildParams(); else layoutParams();
        readNotice();
        repaintControls();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& l = look();
        g.setColour (l.text (0.1f)); g.fillRect (40.0f, 148.0f, 1240.0f, 1.0f);
        if (! feeds.isEmpty()) { g.setColour (l.text (0.78f)); g.setFont (sans (14.0f)); g.drawText ("Sent here by", juce::Rectangle<float> (40.0f, 206.0f, 106.0f, 24.0f), juce::Justification::centredLeft, true); }
        if (fx.unit.isEmpty())
        {
            g.setColour (l.text (0.75f)); g.setFont (sans (16.0f));
            g.drawText (shell.fx.kind == FxAddress::Send ? "Nothing sits on this send. Choose an effect; the bars above set how much of each layer goes to it."
                                                         : "Nothing sits here. Choose an effect from the list above.", juce::Rectangle<float> (40.0f, (float) top + 8.0f, 1000.0f, 26.0f), juce::Justification::centredLeft, true);
            return;
        }
        g.setColour (l.text (0.66f)); g.setFont (sans (14.5f));
        g.drawFittedText (blurb, blurbAt.toNearestInt(), juce::Justification::centredLeft, 2, 0.85f);
        if (notice.isNotEmpty())
        {
            g.setColour (noticeLit ? l.lantern : l.text (0.62f)); g.setFont (sans (14.5f));
            g.drawText (notice, juce::Rectangle<float> (40.0f, (float) top - 28.0f, 1240.0f, 20.0f), juce::Justification::centredLeft, true);
        }
        if (! switchLabels.empty()) headingAt (g, "Modes and switches", 40.0f, (float) switchesY - 30.0f);
        for (const auto& sl : switchLabels) { g.setColour (l.text (0.7f)); g.setFont (sans (13.5f)); g.drawText (sl.second, sl.first, juce::Justification::centredLeft, true); }
        if (isRack()) paintRack (g);
    }

    // (pictures taken by the build) one of the eight places by name: "send-a" .. "send-c", "master-1" .. "master-3", "layer-1", "layer-2"
    void showFor (const juce::String& which) override
    {
        const int n = juce::jmax (0, which.fromLastOccurrenceOf ("-", false, false).getIntValue() - 1);      // ("master-2": 1. Not getTrailingIntValue, which reads the hyphen as a minus)
        if (which.startsWith ("send-")) shell.fx = FxAddress::send (juce::jlimit (0, kSends - 1, (int) (which.getLastCharacter() - 'a')));
        else if (which.startsWith ("master-")) shell.fx = FxAddress::master (juce::jlimit (0, kMasterSlots - 1, n));
        else if (which.startsWith ("layer-")) shell.fx = FxAddress::insert (shell.layer, juce::jlimit (0, kInserts - 1, n));
        refresh();
        // a part of SPECTRAL FX, in the place that is open: "rack-1" .. "rack-4", "rack-warp", "rack-window"
        if (which.startsWith ("rack-") && isRack())
        {
            rackPart = which.endsWith ("warp") ? spectral::kPlaces : (which.endsWith ("window") ? spectral::kPlaces + 1 : juce::jlimit (0, spectral::kPlaces - 1, n));
            buildParams(); readNotice(); repaintControls();
        }
        slow = 7; tick();                        // (what is greyed out, without waiting for the next look)
    }

    void tick() override
    {
        if (isRack()) rackLook();                // (its spectrum moves at the screen's own pace)
        if (++slow % 8 != 0) return;
        // whether each send is fed can change under the page (a layer's send, a macro): looked at four times a second
        bool changed = false;
        for (auto* t : tiles) if (t->address.kind == FxAddress::Send) { const bool u = ! fed (t->address.slot); if (u != t->unfed) { t->unfed = u; t->repaint(); changed = true; } }
        if (changed) { readNotice(); repaintControls(); }
        if (fx.unit.isEmpty()) return;
        // a control that does nothing in the unit's current mode is greyed out; Phantom FX's own rules say which and why
        const int u = fxUnitIndex (fx.unit);
        const bool rack = isRack();
        if (u < 0 && ! rack) return;
        const phantom::ControlValue val = [this] (const std::string& id) { return valueOf (juce::String (id)); };
        for (int i = 0; i < controls.size(); ++i)
        {
            const auto why = rack ? rackIdle (ids[(size_t) i]).toStdString() : phantom::controlIdleReason (u, ids[(size_t) i].toStdString(), val);
            const bool idle = ! why.empty();
            if (idle != idleNow[(size_t) i] || heard != heardShown)
            {
                idleNow[(size_t) i] = idle;
                controls[i]->setTooltip (! heard ? "Greyed out: " + unheardWhy : (idle ? "Greyed out: " + juce::String (why) : juce::String()));
                controls[i]->repaint();
            }
        }
        heardShown = heard;
    }

private:
    void repaintControls() { for (auto* c : getChildren()) c->repaint(); repaint(); }
    float valueOf (const juce::String& id) const
    {
        const auto it = fx.values.find (id);
        if (it != fx.values.end()) return it->second;
        const auto d = defaults.find (id);
        return d != defaults.end() ? d->second : 0.0f;
    }
    juce::String layerTitle (int l) const
    {
        const auto p = shell.proc.engine().getPreset();
        const auto& ls = p.layers[(size_t) juce::jlimit (0, kLayers - 1, l)];
        return "Layer " + juce::String (l + 1) + (ls.used() ? ": " + shortName (ls.instrument, shell.proc) : juce::String (" (empty)"));
    }

    // does any layer send to this send: by its own send level, or by one a macro is holding up?
    bool fed (int send) const
    {
        static const int sp[kSends] = { lpSendA, lpSendB, lpSendC };
        static const char* names[kSends] = { "sendA", "sendB", "sendC" };
        send = juce::jlimit (0, kSends - 1, send);
        const auto p = shell.proc.engine().getPreset();
        for (int l = 0; l < kLayers; ++l)
        {
            if (! p.layers[(size_t) l].used() || p.layers[(size_t) l].p[lpOn] <= 0.5f) continue;
            if (shell.proc.engine().getLayerParam (l, sp[send]) > 0.0f) return true;
            const auto target = "L" + juce::String (l + 1) + "." + names[send];
            for (int m = 0; m < kMacros; ++m)
                if (shell.proc.getHost (Processor::hpMacro1 + m) > 0.0f)
                    for (const auto& t : p.macros[(size_t) m].targets) if (t.param == target && t.amount > 0.0f) return true;
        }
        return false;
    }

    // The line under the top row: why nothing here can be heard (lit), or what a unit waits for before it changes
    // anything (plain). Controls of something that cannot be heard are greyed out, like any control that does nothing.
    void readNotice()
    {
        notice.clear(); noticeLit = false; heard = true; unheardWhy.clear();
        if (fx.unit.isEmpty()) return;
        const auto p = shell.proc.engine().getPreset();
        if (shell.fx.kind == FxAddress::Send && ! fed (shell.fx.slot))
        {
            heard = false; noticeLit = true; unheardWhy = "no layer sends to this effect yet";
            notice = "No layer is sending to this effect yet, so nothing here is heard. Raise a layer's bar, just above.";
        }
        else if (shell.fx.kind == FxAddress::Insert && ! p.layers[(size_t) shell.layer].used())
        {
            heard = false; noticeLit = true; unheardWhy = "this layer has no instrument";
            notice = "Layer " + juce::String (shell.layer + 1) + " has no instrument, so an effect here is not heard. Choose another layer, on the left.";
        }
        else if (shell.fx.kind == FxAddress::Insert && p.layers[(size_t) shell.layer].p[lpOn] <= 0.5f)
        {
            heard = false; noticeLit = true; unheardWhy = "this layer is switched off";
            notice = "Layer " + juce::String (shell.layer + 1) + " is switched off, so an effect here is not heard.";
        }
        else if (! fx.on) notice = "Switched off: the sound goes past it unchanged.";
        else if (isRack()) notice = rackNotice();
        else if (fx.unit == "eq") notice = "Five bands, all flat to begin with: turn a gain up or down.";
        else if (fx.unit == "lim") notice = "Holds back only the loudest peaks, so quiet playing passes through it unchanged.";
        else if (fx.unit == "haunt" && valueOf ("pad") < 0.5f) notice = "No pad is held, so the sound passes through. Turn Pad to 1 or more to hold one of its sixteen gestures.";
        else if (fx.unit == "gate" && valueOf ("pattern") < 1.5f && valueOf ("pmode") < 0.5f) notice = "Pattern 1 is open all the way, so nothing is cut. Turn Pattern up for the ones that gate.";
    }

    // ============================================================================================ SPECTRAL FX
    bool isRack() const { return fx.unit == "spectral"; }
    // the controls of the part on show: a place's MIX and the controls of the effect in it (in that effect's own units
    // and names, as upstream declares them), WARP CORE's nine, or the window's
    void rackList (const std::vector<phantom::ParamSpec>& specs, std::vector<Entry>& list) const
    {
        using namespace spectral;
        using Type = phantom::ParamSpec::Type;
        auto own = [&] (int index, const juce::String& label) { if (index >= 0 && index < (int) specs.size()) list.push_back ({ specs[(size_t) index], label }); };
        if (rackPart < kPlaces)
        {
            const int e = effectAt (rackPart);
            if (e <= 0) return;
            own (iMix (rackPart), "Mix");
            for (int k = 0; k < numKnobs (e); ++k)
            {
                const auto c = knob (e, k);
                phantom::ParamSpec sp;
                sp.id = idOf (iKnob (rackPart, k)).toStdString(); sp.name = c.name.toStdString();
                sp.type = c.kind == cChoice ? Type::Choice : (c.kind == cSwitch ? Type::Bool : (c.kind == cWhole ? Type::Int : Type::Float));
                sp.min = c.min; sp.max = c.max; sp.def = fromKept (e, k, specs[(size_t) iKnob (rackPart, k)].def);
                if (rackPart > 0 || e != (int) std::lround (specs[(size_t) iEffect (0)].def)) { auto fresh = std::map<juce::String, float>(); put (fresh, rackPart, e); sp.def = fromKept (e, k, fresh[idOf (iKnob (rackPart, k))]); }
                sp.skewCentre = c.log && c.min > 0.0f ? std::sqrt (c.min * c.max) : 0.0f;
                for (const auto& ch : c.choices) sp.choices.push_back (ch.toStdString());
                list.push_back ({ sp, c.name, e, k });
            }
        }
        else if (rackPart == kPlaces) for (int k = 0; k < kWarp; ++k) own (iWarp (k), juce::String (specs[(size_t) iWarp (k)].name));
        else
        {
            own (iWindow, "Window, in samples"); own (iShape, "Window shape"); own (iOverlap, "Overlap"); own (iReach, "How high the chain reaches");
            own (iChainMix, "Chain mix"); own (iOrder, "Order");
        }
    }
    int effectAt (int place) const { return juce::jlimit (0, spectral::numEffects() - 1, (int) std::lround (valueOf (spectral::idOf (spectral::iEffect (juce::jlimit (0, spectral::kPlaces - 1, place)))))); }
    juce::String rackOnId() const { return spectral::idOf (rackPart < spectral::kPlaces ? spectral::iOn (rackPart) : spectral::iWarpOn); }
    bool rackHolds (bool windowOnly) const
    {
        for (int s = 0; s < spectral::kPlaces; ++s) { const int e = effectAt (s); if (e > 0 && (! windowOnly || spectral::usesWindow (e))) return true; }
        return false;
    }
    int paramsTop() const { return ! isRack() ? top : top + (rackPart <= spectral::kPlaces ? 94 : 48); }
    // what the tabs say: each place by the effect in it
    void rackLabels()
    {
        juce::StringArray t;
        for (int s = 0; s < spectral::kPlaces; ++s)
        {
            const int e = effectAt (s);
            t.add (juce::String (s + 1) + "   " + (e <= 0 ? juce::String ("empty") : spectral::effectName (e) + (valueOf (spectral::idOf (spectral::iOn (s))) > 0.5f ? juce::String() : juce::String (" (off)"))));
        }
        t.add (valueOf (spectral::idOf (spectral::iWarpOn)) > 0.5f ? "Warp Core" : "Warp Core (off)");
        t.add ("Window");
        rackTabs.choices = t; rackTabs.repaint();
    }
    // another effect in a place: the effect first, then where its knobs start (the rack fades the new one in)
    void putEffect (int place, int effect)
    {
        effect = juce::jlimit (0, spectral::numEffects() - 1, effect);
        if (effect == effectAt (place)) return;
        spectral::put (fx.values, place, effect);
        auto& eng = shell.proc.engine();
        const auto effectId = spectral::idOf (spectral::iEffect (place));
        eng.setFxValue (shell.fx, effectId, fx.values[effectId]);
        for (int k = 0; k < spectral::kKnobs; ++k) { const auto id = spectral::idOf (spectral::iKnob (place, k)); eng.setFxValue (shell.fx, id, fx.values[id]); }
        rackLabels(); buildParams(); readNotice(); slow = 7; tick(); repaintControls();
    }
    // why a control of the part on show does nothing right now ("" = it is in use)
    juce::String rackIdle (const juce::String& id) const
    {
        using namespace spectral;
        if (rackPart < kPlaces) return valueOf (idOf (iOn (rackPart))) > 0.5f ? juce::String() : juce::String ("this place in the chain is switched off");
        if (rackPart == kPlaces)
        {
            if (! warpAvailable()) return "Warp Core is not built for this computer's processor";
            if (valueOf (idOf (iWarpOn)) <= 0.5f) return "Warp Core is switched off";
            // (Pitch Affect and Fill Gap say how a moved pitch is treated: upstream reads them only together with a pitch
            //  that is not 0, so with Pitch at 0 st neither changes a sample)
            if ((id == "w_affect" || id == "w_fill") && std::abs (valueOf ("w_pitch")) < 1.0e-6f) return "it changes nothing until Pitch is moved away from 0";
            return {};
        }
        if (id == "chain") return rackHolds (false) ? juce::String() : juce::String ("no effect is in the chain yet");
        if (id == "order") return rackHolds (false) && valueOf (idOf (iWarpOn)) > 0.5f ? juce::String() : juce::String ("it matters once the chain and Warp Core are both in use");
        return rackHolds (true) ? juce::String() : juce::String ("no effect in the chain works on the window yet");
    }
    juce::String rackNotice() const
    {
        using namespace spectral;
        const bool warp = valueOf (idOf (iWarpOn)) > 0.5f;
        if (! rackHolds (false) && ! warp) return "Nothing is in the rack yet, so the sound goes through it unchanged. Choose an effect for a place, or switch Warp Core on.";
        if (rackPart < kPlaces)
        {
            const auto name = effectName (effectAt (rackPart));
            if (name == "Frequency Remap") return "Frequency Remap moves nothing until its points are moved: turn Low To, Mid To or High To.";
            if (name == "Spectral Equalizer") return "Eight bands, all flat to begin with: turn a gain up or down.";
            if (name == "Utility") return "Utility is a tool: phase, channels, width, gain and balance. It changes nothing until one of them is moved.";
        }
        if (rackHolds (true))
        {
            const double ms = 1000.0 * (double) (windowSamples ((int) std::lround (valueOf ("fft"))) - 1) / juce::jmax (8000.0, shell.proc.getSampleRate());
            const auto late = juce::String ((int) std::lround (ms)) + " ms";
            // (where the rack stands decides what its window costs: a send's own sound is never held back)
            if (shell.fx.kind == FxAddress::Send)
                return "The chain works on a window of sound, so what this send gives back comes " + late + " after the sound that feeds it. The sound itself is not held back; a smaller window (under Window) brings the two closer.";
            if (shell.fx.kind == FxAddress::Insert)
                return "The chain works on a window of sound, so this layer speaks " + late + " late. While the song plays, the other layers wait for it and the host keeps the song in step; played by hand, a smaller window (under Window) answers sooner.";
            return "The chain works on a window of sound, so the whole sound speaks " + late + " late. The host is told and keeps the song in step; played by hand, a smaller window (under Window) answers sooner.";
        }
        return {};
    }
    // the rack's spectrum, read at the screen's pace: the newest frame folded into columns, each falling slowly
    void rackLook()
    {
        if (specAt.isEmpty()) return;
        auto& eng = shell.proc.engine();
        for (int side = 0; side < 2; ++side)
        {
            auto& cols = side == 0 ? specIn : specOut;
            for (auto& c : cols) c = juce::jmax (-96.0f, c - 1.6f);
            double rate = 48000.0;
            if (! eng.rackSpectrum (shell.fx, specMags, rate, side == 0) || specMags.size() < 16) continue;
            const int bins = (int) specMags.size();
            const double n = 2.0 * (double) (bins - 1);
            for (int c = 0; c < kSpecCols; ++c)
            {
                const double f0 = 30.0 * std::pow (20000.0 / 30.0, (double) c / kSpecCols), f1 = 30.0 * std::pow (20000.0 / 30.0, (double) (c + 1) / kSpecCols);
                const double at0 = f0 * n / rate, at1 = f1 * n / rate;       // (in bins)
                float most = 0.0f;
                if (at1 - at0 < 1.0)
                {   // low down a column is narrower than a bin: the curve passes smoothly between the two bins beside it
                    const double at = juce::jlimit (1.0, (double) (bins - 1), 0.5 * (at0 + at1));
                    const int b = juce::jlimit (1, bins - 2, (int) std::floor (at));
                    const float t = (float) (at - (double) b);
                    most = specMags[(size_t) b] * (1.0f - t) + specMags[(size_t) b + 1] * t;
                }
                else
                {
                    const int b0 = juce::jlimit (1, bins - 1, (int) std::floor (at0)), b1 = juce::jlimit (b0, bins - 1, (int) std::ceil (at1));
                    for (int b = b0; b <= b1; ++b) most = juce::jmax (most, specMags[(size_t) b]);
                }
                cols[(size_t) c] = juce::jmax (cols[(size_t) c], 20.0f * std::log10 (juce::jmax (1.0e-9f, most / (float) (n * 0.25))));      // (a full-scale tone reads about 0 dB)
            }
        }
        repaint (specAt.toNearestInt().expanded (2));
    }
    void paintRack (juce::Graphics& g)
    {
        const auto& l = look();
        using namespace spectral;
        const float rowY = (float) top + 48.0f;
        g.setFont (sans (14.0f));
        if (rackPart < kPlaces)
        {
            const int e = effectAt (rackPart);
            g.setColour (l.text (0.62f));
            if (e > 0) g.drawText (named (effectFolder (e)) + (usesWindow (e) ? juce::String (": works on the window") : juce::String (": works on the sound as it comes, with no delay")),
                                   juce::Rectangle<float> (500.0f, rowY, 760.0f, 30.0f), juce::Justification::centredLeft, true);
            else g.drawText ("This place is empty: the sound goes on to the next. There are 38 effects to choose from, in eight folders.", juce::Rectangle<float> (316.0f, rowY, 940.0f, 30.0f), juce::Justification::centredLeft, true);
        }
        else if (rackPart == kPlaces)
        {
            g.setColour (l.text (0.62f));
            g.drawText (warpAvailable() ? "Spectral inversion: it turns the spectrum over in bands. No window, no delay." : "Warp Core is not built for this computer's processor.",
                        juce::Rectangle<float> (250.0f, rowY, 1000.0f, 30.0f), juce::Justification::centredLeft, true);
        }
        if (specAt.isEmpty()) return;
        const auto b = specAt;
        g.setColour (l.text (0.045f)); g.fillRoundedRectangle (b, 10.0f);
        g.setColour (l.text (0.10f)); g.drawRoundedRectangle (b.reduced (0.5f), 10.0f, 1.0f);
        const float x0 = b.getX() + 14.0f, w = b.getWidth() - 28.0f, y1 = b.getBottom() - 22.0f, h = b.getHeight() - 52.0f;
        auto xOf = [x0, w] (double hz) { return x0 + w * (float) (std::log (hz / 30.0) / std::log (20000.0 / 30.0)); };
        g.setFont (sans (11.5f));
        for (double hz : { 100.0, 1000.0, 10000.0 })
        {
            g.setColour (l.text (0.09f)); g.fillRect (xOf (hz), b.getY() + 30.0f, 1.0f, b.getHeight() - 52.0f);
            g.setColour (l.text (0.45f)); g.drawText (hz >= 1000.0 ? juce::String ((int) (hz / 1000.0)) + " kHz" : juce::String ((int) hz) + " Hz", juce::Rectangle<float> (xOf (hz) - 30.0f, b.getBottom() - 20.0f, 60.0f, 16.0f), juce::Justification::centred, false);
        }
        auto shape = [&] (const std::array<float, kSpecCols>& cols, bool closed) {
            juce::Path p;
            for (int c = 0; c < kSpecCols; ++c)
            {
                const float x = x0 + w * ((float) c + 0.5f) / (float) kSpecCols, y = y1 - h * juce::jlimit (0.0f, 1.0f, (cols[(size_t) c] + 84.0f) / 84.0f);
                if (c == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            if (closed) { p.lineTo (x0 + w, y1); p.lineTo (x0, y1); p.closeSubPath(); }
            return p;
        };
        g.setColour (l.text (0.13f)); g.fillPath (shape (specIn, true));
        g.setGradientFill (juce::ColourGradient (l.accent.withAlpha (0.36f), 0.0f, b.getY() + 30.0f, l.accent.withAlpha (0.04f), 0.0f, y1, false)); g.fillPath (shape (specOut, true));
        g.setColour (l.accent.withAlpha (0.95f)); g.strokePath (shape (specOut, false), juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (l.text (0.66f)); g.setFont (sans (13.0f));
        g.drawText ("What goes into the chain's window (grey), and what comes out of it", b.reduced (14.0f, 7.0f).removeFromTop (18.0f), juce::Justification::centredLeft, true);
    }

    // a send's row of bars: how much of each layer in use goes to it
    void buildFeeds()
    {
        static const int sp[kSends] = { lpSendA, lpSendB, lpSendC };
        feeds.clear();
        if (shell.fx.kind != FxAddress::Send) return;
        const auto p = shell.proc.engine().getPreset();
        for (int l = 0; l < kLayers; ++l)
        {
            if (! p.layers[(size_t) l].used()) continue;
            auto* b = feeds.add (new Bar());
            bindLayer (*b, shell, l, sp[juce::jlimit (0, kSends - 1, shell.fx.slot)]);
            const auto set = b->set;
            b->set = [this, set] (float v) { set (v); if (heard != fed (shell.fx.slot)) { for (auto* t : tiles) if (t->address == shell.fx) { t->unfed = heard; t->repaint(); } readNotice(); repaintControls(); } };
            b->label = juce::String (l + 1) + "  " + shortName (p.layers[(size_t) l].instrument, shell.proc);
            b->setName (juce::String (l + 1));          // (what it is called when there is only room for its number)
            b->setTooltip ("How much of layer " + juce::String (l + 1) + " (" + shortName (p.layers[(size_t) l].instrument, shell.proc) + ") goes to this send");
            b->setComponentID ("feed" + juce::String (l + 1));
            addAndMakeVisible (b);
        }
    }

    void layoutTop()
    {
        const bool ins = shell.fx.kind == FxAddress::Insert, send = shell.fx.kind == FxAddress::Send;
        int x = 40;
        layerPick.setVisible (ins);
        if (ins) { layerPick.setBounds (x, 162, 214, 32); x += 224; }
        unit.setBounds (x, 162, 250, 32); on.setBounds (x + 266, 164, 80, 28); amount.setBounds (x + 360, 166, 300, 24);
        blurbAt = { (float) (x + 684), 160.0f, (float) (1280 - (x + 684)), 38.0f };
        top = send ? 262 : 240;
        const bool rack = isRack();
        rackTabs.setVisible (rack); rackEffect.setVisible (rack && rackPart < spectral::kPlaces);
        rackOn.setVisible (rack && (rackPart == spectral::kPlaces || (rackPart < spectral::kPlaces && effectAt (rackPart) > 0)));       // (an empty place has nothing to switch)
        if (rack)
        {
            rackTabs.setBounds (40, top, 1060, 34);
            rackEffect.setBounds (40, top + 47, 260, 32);
            rackOn.label = rackPart < spectral::kPlaces ? "On" : "Warp Core on";
            rackOn.setBounds (rackPart < spectral::kPlaces ? 316 : 40, top + 49, rackPart < spectral::kPlaces ? 150 : 190, 28);
        }
        const int n = feeds.size();
        if (n > 0)
        {
            const int w = juce::jmin (330, (1280 - 150) / n - 10);
            for (int i = 0; i < n; ++i)
            {
                auto* b = feeds[i];
                b->labelWidth = w >= 280 ? 160.0f : (w >= 200 ? 112.0f : (w >= 130 ? 62.0f : 24.0f));
                if (w < 130) b->label = b->getName();   // (more than seven layers: the number alone; the tooltip has the name)
                b->showValue = w >= 150; b->valueWidth = 44.0f;
                b->setBounds (150 + i * (w + 10), 206, w, 24);
            }
        }
    }

    void buildParams()
    {
        controls.clear(); ids.clear(); idleNow.clear(); kinds.clear(); names.clear(); defaults.clear();
        heardShown = true;                       // (new controls carry no tooltip yet: the next tick gives them theirs)
        blurb.clear();
        for (const auto& u : fxUnits()) if (u.key == fx.unit) blurb = u.blurb;
        if (fx.unit.isEmpty()) { layoutTop(); layoutParams(); return; }
        const auto specs = fxParams (fx.unit);
        for (const auto& spec : specs) defaults[juce::String (spec.id)] = spec.def;       // (every control's, shown or not: the greying rules ask about them all)
        // the controls to show, and what each is called
        std::vector<Entry> list;
        const auto& only = shownFor (fx.unit);
        if (isRack()) rackList (specs, list);
        else if (only.empty()) for (const auto& spec : specs) list.push_back ({ spec, worded (juce::String (spec.name)) });
        else for (const auto& o : only) for (const auto& spec : specs) if (spec.id == o.id) list.push_back ({ spec, juce::String (o.label) });
        layoutTop();                             // (the rack's own widgets follow the part on show)
        for (const auto& entry : list)
        {
            const auto spec = entry.spec;
            const juce::String id (spec.id);
            Control* c = nullptr;
            int kind = 0;
            if (spec.type == phantom::ParamSpec::Type::Choice) { auto* ch = new Choice(); ch->menuParent = shell.canvas; for (const auto& s : spec.choices) ch->choices.add (worded (juce::String (s))); c = ch; kind = 1; }
            else if (spec.type == phantom::ParamSpec::Type::Bool) { c = new Switch(); kind = 2; }
            else { auto* k = new Knob(); k->labelSize = 13.5f; k->valueSize = 12.5f; c = k; }
            c->label = kind == 1 ? juce::String() : entry.label;
            c->lo = spec.min; c->hi = spec.max; c->def = spec.def;
            c->whole = spec.type != phantom::ParamSpec::Type::Float;
            c->log = spec.type == phantom::ParamSpec::Type::Float && spec.min > 0.0f && (spec.skewCentre > 0.0f || spec.max / spec.min >= 100.0f);
            c->bipolar = spec.type == phantom::ParamSpec::Type::Float && spec.min < 0.0f && spec.max > 0.0f;
            if (entry.knob >= 0)
            {   // a control of the effect in one of the rack's places: shown and turned in the effect's own units, kept between 0 and 1
                const int e = entry.effect, k = entry.knob;
                if (kind == 0) c->text = [e, k] (float v) { return spectral::written (e, k, v).trim(); };
                c->get = [this, id, e, k] { return spectral::fromKept (e, k, valueOf (id)); };
                c->set = [this, id, e, k] (float v) { const float kept = spectral::toKept (e, k, v); fx.values[id] = kept; shell.proc.engine().setFxValue (shell.fx, id, kept); if (notice.isNotEmpty() && ! noticeLit) { readNotice(); repaint(); } };
            }
            else
            {
                if (kind == 0) c->text = [spec] (float v) { return written (spec, v); };
                c->get = [this, id, def = spec.def] { const auto it = fx.values.find (id); return it != fx.values.end() ? it->second : def; };
                c->set = [this, id] (float v) { fx.values[id] = v; shell.proc.engine().setFxValue (shell.fx, id, v); if (notice.isNotEmpty() && ! noticeLit) { readNotice(); repaint(); } };
            }
            const size_t index = ids.size();
            c->usable = [this, index] { return fx.on && heard && ! idleNow[index]; };
            c->setComponentID (id);
            ids.push_back (id); idleNow.push_back (false); kinds.push_back (kind);
            names.add (entry.label);
            controls.add (c);
            addAndMakeVisible (c);
        }
        layoutParams();
    }

    void layoutParams()
    {
        switchLabels.clear();
        const int from = paramsTop();
        int kx = 40, ky = from, n = 0;
        for (int i = 0; i < controls.size(); ++i)
            if (kinds[(size_t) i] == 0)
            {
                controls[i]->setBounds (kx, ky, 96, 96);
                kx += 103; ++n;
                if (n % 12 == 0) { kx = 40; ky += 108; }
            }
        const int knobRows = n == 0 ? 0 : (n + 11) / 12;
        switchesY = from + knobRows * 108 + 44;
        int sx = 40, sy = switchesY;
        for (int i = 0; i < controls.size(); ++i)
        {
            if (kinds[(size_t) i] == 0) continue;
            const int w = kinds[(size_t) i] == 1 ? 196 : 150;
            if (sx + w > 1280) { sx = 40; sy += 66; }
            if (kinds[(size_t) i] == 1)
            {
                switchLabels.push_back ({ juce::Rectangle<float> ((float) sx, (float) sy, (float) w, 18.0f), names[i] });
                controls[i]->setBounds (sx, sy + 20, w, 30);
            }
            else controls[i]->setBounds (sx, sy + 20, w, 28);
            sx += w + 14;
        }
        // SPECTRAL FX: under the controls, the spectrum going into the chain's window and coming out (when the chain uses it)
        specAt = {};
        if (isRack() && rackHolds (true))
        {
            const int under = controls.size() > n ? sy + 66 : from + knobRows * 108 + 14;
            if (690 - under >= 96) specAt = { 40.0f, (float) under, 1240.0f, (float) (690 - under) };
        }
        repaint();
    }

    juce::OwnedArray<SlotTile> tiles;
    Choice layerPick, unit;
    // SPECTRAL FX: its parts (0..3 a place in the chain, 4 WARP CORE, 5 the window), and its spectrum
    Segments rackTabs; Choice rackEffect; Switch rackOn;
    int rackPart = 0;
    static constexpr int kSpecCols = 300;
    std::array<float, kSpecCols> specIn, specOut;
    std::vector<float> specMags;
    juce::Rectangle<float> specAt;
    Switch on;
    Bar amount;
    juce::OwnedArray<Bar> feeds;                 // (a send only) one bar per layer in use
    juce::OwnedArray<Control> controls;
    std::vector<juce::String> ids;
    std::vector<bool> idleNow;
    std::vector<int> kinds;
    juce::StringArray names;
    std::map<juce::String, float> defaults;
    std::vector<std::pair<juce::Rectangle<float>, juce::String>> switchLabels;
    FxState fx;
    FxAddress shown = FxAddress::send (2);
    juce::String blurb, notice, unheardWhy;
    juce::Rectangle<float> blurbAt { 724.0f, 160.0f, 556.0f, 38.0f };
    bool noticeLit = false, heard = true, heardShown = true;
    int top = 262, switchesY = 400, slow = 0;
};
} // namespace

std::unique_ptr<Page> makeEffectsPage (Shell& s) { return std::make_unique<EffectsPage> (s); }
} // namespace atlas::ui
