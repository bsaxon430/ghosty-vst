// Ghost Voice - AGPL-3.0-only (see LICENSE, NOTICE.md).
#include "HoodOriginals.h"
#include "Editor.h"
#include "ChopPage.h"
#include "ChopEngine.h"
#include "src/PluginProcessor.h"   // INTERSECT: IntersectProcessor::Field* (the per-slice / per-sample field ids)

namespace gv::hoodui
{
using P = IntersectProcessor;
using choir::serif;
using choir::display;

// ================================================================================================ INTERSECT's own value formats
static juce::String trimmed (float v, int dec)
{
    juce::String t (v, dec);
    if (t.containsChar ('.')) { while (t.endsWithChar ('0')) t = t.dropLastCharacters (1); if (t.endsWithChar ('.')) t = t.dropLastCharacters (1); }
    return t;
}
static juce::String signedText (float v, int dec, const juce::String& suf) { return (v >= 0 ? "+" : "") + trimmed (v, dec) + suf; }
static juce::String msText (float v) { return juce::String (juce::roundToInt (v)) + "ms"; }
static juce::String pctText (float v, int dec = 1) { return trimmed (v, dec) + "%"; }
static juce::String hzText (float v) { return v >= 1000.0f ? trimmed (v / 1000.0f, 2) + "k" : trimmed (v, 0); }
static juce::String gainText (float v) { return (v >= 0 ? "+" : "") + trimmed (v, 1) + "dB"; }
static juce::String noteName (int n) { return juce::MidiMessage::getMidiNoteName (n, true, true, 3); }   // FL Studio's octaves: C3 = 60

// ================================================================================================ Cell
juce::String Cell::valueText() const
{
    switch (kind)
    {
        case Kind::choice: return choices[juce::jlimit (0, juce::jmax (0, choices.size() - 1), juce::roundToInt (value))];
        case Kind::toggle: return value > 0.5f ? "ON" : "OFF";
        case Kind::button: return name;
        default: break;
    }
    return fmt ? fmt (value) : trimmed (value, 2);
}

float Cell::quantise (float v) const
{
    v = juce::jlimit (lo, hi, v);
    if (step > 0) v = lo + std::round ((v - lo) / step) * step;
    return juce::jlimit (lo, hi, v);
}

void Cell::set (float v)
{
    if (v == value) return;
    value = v; repaint();
}

void Cell::setOn (bool e, const juce::String& reason)
{
    const auto t = e ? tip : (reason.isNotEmpty() ? name + ": greyed. " + reason : tip);
    if (e == on && t == getTooltip()) return;
    on = e; why = e ? juce::String() : reason;
    setEnabled (e); setTooltip (t); repaint();
}

bool Cell::press (float v)
{
    if (! on || kind == Kind::readOnly) return false;
    if (kind == Kind::button) { if (onPress) onPress(); return true; }
    apply (v, false);
    return true;
}

void Cell::apply (float v, bool dragging)
{
    v = kind == Kind::number ? quantise (v) : juce::jlimit (lo, hi, std::round (v));
    const bool changed = v != value;
    value = v; repaint();
    if ((changed || ! dragging) && onChange) onChange (v, dragging);
}

void Cell::mouseDown (const juce::MouseEvent& e) { startY = e.y; startV = value; dragged = false; }

void Cell::mouseDrag (const juce::MouseEvent& e)
{
    if (! on || kind != Kind::number) return;
    const float dy = (float) (startY - e.y);
    if (! dragged && std::abs (dy) < 2.0f) return;
    dragged = true;
    const float fine = e.mods.isShiftDown() ? 0.1f : 1.0f;
    float v;
    if (logDrag && lo > 0)
    {
        const float n = std::log (juce::jmax (lo, startV) / lo) / std::log (hi / lo) + dy * perPx * fine;
        v = lo * std::pow (hi / lo, juce::jlimit (0.0f, 1.0f, n));
    }
    else v = startV + dy * perPx * fine;
    apply (v, true);
}

void Cell::mouseUp (const juce::MouseEvent& e)
{
    if (! on) return;
    if (dragged) { dragged = false; if (onChange) onChange (value, false); return; }
    if (! getLocalBounds().contains (e.getPosition())) return;
    if (kind == Kind::toggle) apply (value > 0.5f ? 0.0f : 1.0f, false);
    else if (kind == Kind::choice)
    {   // INTERSECT's own click: the next choice
        const int n = cycle > 0 ? cycle : choices.size();
        apply ((float) ((juce::roundToInt (value) + 1) % juce::jmax (1, n)), false);
    }
    else if (kind == Kind::button && onPress) onPress();
}

void Cell::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (! on || kind != Kind::number || w.deltaY == 0) return;
    const float d = (w.deltaY > 0 ? 1.0f : -1.0f) * juce::jmax (step, perPx * 4.0f);
    if (logDrag && lo > 0) apply (value * std::pow (2.0f, (w.deltaY > 0 ? 1.0f : -1.0f) / 12.0f), false);
    else apply (value + d, false);
}

void Cell::paint (juce::Graphics& g)
{
    if (P == nullptr) return;
    const auto& C = *P;
    juce::String k; k << (int) C.dark << getWidth() << "x" << getHeight() << "|" << name << "|" << valueText() << "|" << plain << "|" << (int) on << (int) own
                      << (int) isMouseOver() << (int) kind << (int) showPlain;
    art.draw (g, k, getLocalBounds(), [this, &C] (juce::Graphics& ig, juce::Rectangle<int> r)
    {
        const float a = on ? 1.0f : choir::greyA (C);
        const auto W = (float) r.getWidth();
        const int nameH = compact ? 0 : kNameH;
        if (! compact && kind != Kind::button && name.isNotEmpty())
        {
            auto nr = juce::Rectangle<float> (0, 0, W, (float) kNameH);
            const auto t = own ? name + " *" : name;
            ig.setColour (choir::greyInk (C, own ? C.select : C.ink, a)); ig.setFont (choir::fitted (choir::bold, t, W - 4, 13.5f, 9.0f));
            ig.drawText (t, nr, juce::Justification::centred, false);
        }
        const float bw = kind == Kind::button ? W - 4 : juce::jmin (W - 6, 124.0f);
        auto box = juce::Rectangle<float> ((W - bw) * 0.5f, (float) nameH + 0.5f, bw, (float) kBoxH - 1.0f);
        const bool ro = kind == Kind::readOnly;
        if (! on)
        {
            juce::Path o; o.addRoundedRectangle (box, 4.0f); juce::Path d; const float dash[] { 3.0f, 3.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (d, o, dash, 2);
            ig.setColour (C.faint.withAlpha (C.dark ? 0.55f : 0.65f)); ig.fillPath (d);
        }
        else
        {
            const bool lit = kind == Kind::toggle && value > 0.5f;
            if (lit)
            {
                juce::ColourGradient gr (C.glass[4], box.getX(), box.getY(), C.glass[1], box.getRight(), box.getBottom(), false);
                ig.setGradientFill (gr); ig.fillRoundedRectangle (box, 4.0f);
            }
            else { ig.setColour (ro ? C.cell.withMultipliedAlpha (0.5f) : C.card); ig.fillRoundedRectangle (box, 4.0f); }
            ig.setColour (ro ? C.edgeDim : (isMouseOver() ? C.goldHi : C.edge)); ig.drawRoundedRectangle (box, 4.0f, isMouseOver() && ! ro ? 1.4f : 1.0f);
        }
        const auto v = valueText();
        juce::Colour vc = kind == Kind::toggle ? (value > 0.5f ? juce::Colours::white : C.dim)
                        : kind == Kind::button ? C.ink
                        : ro ? C.ink
                        : (C.dark ? C.gold : C.edge.darker (0.35f));
        ig.setColour (on ? vc : choir::greyInk (C, C.ink, a));
        ig.setFont (choir::fitted (choir::bold, v, box.getWidth() - 8, 15.0f, 8.5f));
        ig.drawText (v, box.reduced (4, 0), juce::Justification::centred, false);
        if (showPlain && plain.isNotEmpty())
        {
            auto pr = juce::Rectangle<int> (0, nameH + kBoxH + 3, r.getWidth(), kPlainH);
            ig.setColour (choir::greyInk (C, C.dim, a)); ig.setFont (serif (12.5f));
            ig.drawFittedText (plain, pr.reduced (2, 0), juce::Justification::centredTop, 2, 0.85f);
        }
    });
}

// ================================================================================================ Panel
Cell* Panel::find (int id) const
{
    std::function<Cell* (const juce::Component&)> walk = [&] (const juce::Component& c) -> Cell*
    {
        for (auto* k : c.getChildren())
        {
            if (auto* cell = dynamic_cast<Cell*> (k)) if (cell->id == id) return cell;
            if (auto* f = walk (*k)) return f;
        }
        return nullptr;
    };
    return walk (*this);
}

juce::Array<Cell*> Panel::cells() const
{
    juce::Array<Cell*> out;
    std::function<void (const juce::Component&)> walk = [&] (const juce::Component& c)
    { for (auto* k : c.getChildren()) { if (auto* cell = dynamic_cast<Cell*> (k)) out.add (cell); walk (*k); } };
    walk (*this);
    return out;
}

namespace
{
bool onScreen (const juce::Component& c)
{   // the panel and everything up to the window is visible (works without a desktop window too: the harness, the tests)
    for (auto* k = &c; k != nullptr && k->getParentComponent() != nullptr; k = k->getParentComponent()) if (! k->isVisible()) return false;
    return true;
}

struct Painted : public juce::Component
{
    std::function<void (juce::Graphics&)> painter;
    void paint (juce::Graphics& g) override { if (painter) painter (g); }
};

void styleButton (juce::TextButton& b) { b.setWantsKeyboardFocus (false); b.getProperties().set ("fontH", 13.5f); }

// a plain-words line in the look's dim ink
void plainLine (juce::Graphics& g, const choir::Pal& C, const juce::String& t, juce::Rectangle<int> r, bool on = true, juce::Justification j = juce::Justification::centredTop, int lines = 2)
{
    g.setColour (choir::greyInk (C, C.dim, on ? 1.0f : choir::greyA (C))); g.setFont (serif (12.5f));
    g.drawFittedText (t, r, j, lines, 0.85f);
}

// a module's frame and title, the way the CHOP page's SOUND panel draws INTERSECT's modules
void moduleFrame (juce::Graphics& g, const choir::Pal& C, juce::Rectangle<float> a, const juce::String& title, juce::Colour accent, const juce::String& right = {})
{
    g.setColour (C.cell.withAlpha (C.dark ? 0.35f : 0.45f)); g.fillRoundedRectangle (a, 4.0f);
    g.setColour (C.edgeDim); g.drawRoundedRectangle (a.reduced (0.5f), 4.0f, 1.0f);
    auto t = a.removeFromTop (24).reduced (10, 0);
    g.setColour (C.edgeDim.withMultipliedAlpha (0.6f)); g.fillRect (a.getX() + 6, a.getY(), a.getWidth() - 12, 1.0f);
    g.setColour (accent); g.fillEllipse (t.getX(), t.getCentreY() - 3.5f, 7, 7);
    g.setColour (C.dark ? C.gold : C.edge.darker (0.2f)); g.setFont (display (15.0f));
    g.drawText (title, t.withTrimmedLeft (14), juce::Justification::centredLeft, false);
    if (right.isNotEmpty()) { g.setColour (C.select); g.setFont (serif (13.0f, choir::bold)); g.drawText (right, t, juce::Justification::centredRight, false); }
}
}

// ================================================================================================ INTERSECT: SOUND (its SignalChainBar)
class IntersectSound : public Panel, private juce::Timer
{
public:
    explicit IntersectSound (Editor& e) : ed (e)
    {
        using K = Cell::Kind;
        auto sec = [] (float v) { return msText (v); };
        // INTERSECT's own cells (SignalChainBar.cpp): name, field, kind, range in its SAMPLE-tab units, drag per pixel, format, plain words
        const juce::StringArray onOff;
        // TIME/PITCH, its row 1 then row 2
        add (0, 0, 1.0f, P::FieldBpm, "BPM", "its own tempo, for STRETCH", K::number, 20, 999, 0.01f, 0.25f, [] (float v) { return trimmed (v, 2); });
        add (0, 0, 1.0f, P::FieldPitch, "PITCH", "semitones up / down", K::number, -48, 48, 0.01f, 0.5f, [] (float v) { return signedText (v, 2, "st"); });
        add (0, 0, 1.0f, P::FieldCentsDetune, "TUNE", "fine pitch, in cents", K::number, -100, 100, 0.1f, 1.0f, [] (float v) { return signedText (v, 1, "ct"); }, 12);
        add (0, 0, 1.0f, P::FieldStretchEnabled, "STRETCH", "follows the song's tempo", K::toggle, 0, 1);
        add (0, 1, 2.0f, P::FieldAlgorithm, "ALGO", "how it is pitched: like a sampler, or keeping the length", K::choice, 0, 2, 1, 0, nullptr, 0, { "Repitch", "Signalsmith", "Bungee" });
        add (0, 1, 1.0f, P::FieldRepitchMode, "MODE", "how smooth Repitch is", K::choice, 0, 2, 1, 0, nullptr, 0, { "Linear", "Cubic", "Sinc" })->cycle = 2;   // (its click steps Linear / Cubic)
        add (0, 1, 1.0f, P::FieldTonality, "TONAL", "keeps tones under this clean", K::number, 0, 8000, 1, 20.0f, [] (float v) { return hzText (v); });
        add (0, 1, 1.0f, P::FieldFormant, "FMNT", "moves the voice's formants", K::number, -24, 24, 0.1f, 0.1f, [] (float v) { return signedText (v, 1, "st"); });
        add (0, 1, 1.0f, P::FieldFormantComp, "FMNT C", "keeps the voice when pitched", K::toggle, 0, 1);
        add (0, 1, 1.0f, P::FieldGrainMode, "GRAIN", "the stretch's grain size", K::choice, 0, 2, 1, 0, nullptr, 0, { "Fast", "Normal", "Smooth" });
        // FILTER (its ON / OFF sits in the module's header)
        add (1, 0, 1.0f, P::FieldFilterType, "TYPE", "the filter's shape", K::choice, 0, 3, 1, 0, nullptr, 0, { "LP", "HP", "BP", "NT" });
        add (1, 0, 1.0f, P::FieldFilterSlope, "SLOPE", "how steep the filter is", K::choice, 0, 1, 1, 0, nullptr, 0, { "12dB", "24dB" });
        add (1, 0, 1.0f, P::FieldFilterCutoff, "CUT", "the filter's frequency", K::number, 20, 20000, 1, 0.005f, [] (float v) { return hzText (v); })->logDrag = true;
        add (1, 0, 1.0f, P::FieldFilterReso, "RESO", "a ring at the cutoff", K::number, 0, 100, 0.1f, 0.5f, [] (float v) { return pctText (v); });
        add (1, 0, 1.0f, P::FieldFilterDrive, "DRIVE", "warmth pushed into the filter", K::number, 0, 100, 0.1f, 0.5f, [] (float v) { return pctText (v); });
        add (1, 0, 1.0f, P::FieldFilterAsym, "ASYM", "lopsided drive: more grit", K::number, 0, 100, 0.1f, 0.5f, [] (float v) { return pctText (v); });
        add (1, 1, 1.0f, P::FieldFilterKeyTrack, "KEY", "the cutoff follows the key played", K::number, 0, 100, 0.1f, 0.5f, [] (float v) { return pctText (v); });
        add (1, 1, 1.0f, P::FieldFilterEnvAmount, "AMT", "how far its envelope moves CUT", K::number, -96, 96, 0.1f, 0.2f, [] (float v) { return signedText (v, 1, "st"); });
        add (1, 1, 1.0f, P::FieldFilterEnvAttack, "ATK", "the envelope's rise", K::number, 0, 10000, 1, 1.0f, sec);
        add (1, 1, 1.0f, P::FieldFilterEnvDecay, "DEC", "its fall to SUS", K::number, 0, 10000, 1, 1.0f, sec);
        add (1, 1, 1.0f, P::FieldFilterEnvSustain, "SUS", "its level while held", K::number, 0, 100, 0.1f, 0.5f, [] (float v) { return pctText (v); });
        add (1, 1, 1.0f, P::FieldFilterEnvRelease, "REL", "its fall after key-up", K::number, 0, 10000, 1, 1.0f, sec);
        // AMP
        add (2, 0, 1.0f, P::FieldAttack, "ATK", "fade-in when a key goes down", K::number, 0, 1000, 1, 1.0f, sec);
        add (2, 0, 1.0f, P::FieldDecay, "DEC", "fall to the SUS level", K::number, 0, 5000, 1, 1.0f, sec);
        add (2, 0, 1.0f, P::FieldSustain, "SUS", "the level while held", K::number, 0, 100, 0.1f, 0.5f, [] (float v) { return pctText (v); });
        add (2, 1, 1.0f, P::FieldRelease, "REL", "fade on key-up", K::number, 0, 5000, 1, 1.0f, sec);
        add (2, 1, 1.0f, P::FieldReleaseTail, "TAIL", "REL plays on past the end", K::toggle, 0, 1);
        add (2, 1, 1.0f, P::FieldVolume, "GAIN", "the level", K::number, -100, 24, 0.1f, 0.3f, [] (float v) { return gainText (v); });
        // PLAYBACK
        add (3, 0, 1.0f, P::FieldReverse, "REV", "plays backwards", K::toggle, 0, 1);
        add (3, 0, 1.0f, P::FieldLoop, "LOOP", "loops while the key is held", K::choice, 0, 2, 1, 0, nullptr, 0, { "Off", "Loop", "PP" });
        add (3, 0, 1.0f, P::FieldCrossfade, "FADE", "smooths the loop's seam", K::number, 0, 100, 1, 0.5f, [] (float v) { return pctText (v, 0); });
        add (3, 1, 1.0f, P::FieldMuteGroup, "MUTE", "chops in one group cut each other off", K::number, 0, 32, 1, 0.25f, [] (float v) { return juce::String (juce::roundToInt (v)); });
        add (3, 1, 1.0f, P::FieldOneShot, "1SHOT", "plays to the end after key-up", K::toggle, 0, 1);
        add (3, 1, 1.0f, P::FieldOutputBus, "OUT", "which output it plays on", K::number, 0, 15, 1, 0.25f, [] (float v) { return juce::String (juce::roundToInt (v) + 1); });
        // the modules' header controls: SET BPM (TIME/PITCH) and the filter's ON / OFF (FILTER)
        setBpm = header (0, idSetBpm, "SET BPM", K::button, "sets BPM from the length in bars");
        setBpm->onPress = [this] { setBpmMenu(); };
        filterOn = header (1, P::FieldFilterEnabled, "FILTER", K::toggle, "turns the filter on");
        filterOn->onChange = [this] (float v, bool d) { write (P::FieldFilterEnabled, v, d); };
        for (int m = 0; m < 4; ++m) mods[m].name = juce::StringArray { "TIME/PITCH", "FILTER", "AMP", "PLAYBACK" }[m];
        body.painter = [this] (juce::Graphics& g) { paintBody (g); };
        vp.setViewedComponent (&body, false); vp.setScrollBarsShown (true, false); vp.setScrollBarThickness (10);
        addAndMakeVisible (vp);
        // the context row (SignalChainBar's own row under the modules)
        for (auto* b : { &sampleTab, &sliceTab, &prevB, &nextB }) { styleButton (*b); addAndMakeVisible (*b); }
        sampleTab.setClickingTogglesState (false); sliceTab.setClickingTogglesState (false);
        sampleTab.setTooltip ("SAMPLE: the clip's own settings, which every chop follows unless it sets its own");
        sliceTab.setTooltip ("SLICE: the chosen chop's own settings (the chop the CHOP page's SOUND panel shows)");
        prevB.setTooltip ("The chop before"); nextB.setTooltip ("The next chop");
        sampleTab.onClick = [this] { setScope (false); };
        sliceTab.onClick = [this] { setScope (true); };
        prevB.onClick = [this] { stepChop (-1); };
        nextB.onClick = [this] { stepChop (1); };
        ctxRange = ctx (-1, "SLICE", "where the chop starts and ends", K::readOnly);
        ctxMode = ctx (-1, "KEYS", "one key, or a range of keys", K::readOnly);
        ctxLow = ctx (idNote, "NOTE", "the key it plays on", K::readOnly);
        ctxHigh = ctx (-1, "HIGH", "the top key of its range", K::readOnly);
        ctxRoot = ctx (-1, "ROOT", "the key that plays it as it is", K::readOnly);
        ctxName = ctx (-1, "", "its note name", K::readOnly);
        ctxInfo = ctx (-1, "SAMPLE", "the clip on the CHOP page", K::readOnly);
        ctxSlices = ctx (idSlices, "SLICES", "how many chops it holds", K::readOnly);
        ctxRootNote = ctx (idRoot, "ROOT", "the first chop's key", K::readOnly);
        ctxVoices = ctx (idVoices, "VOICES", "how many notes can sound at once", K::number);
        ctxVoices->lo = 1; ctxVoices->hi = 31; ctxVoices->step = 1; ctxVoices->perPx = 0.25f; ctxVoices->fmt = [] (float v) { return juce::String (juce::roundToInt (v)); };
        ctxVoices->onChange = [this] (float v, bool) { engine().setVoices (juce::roundToInt (v)); };
        startTimerHz (15);
    }
    juce::String title() const override { return "INTERSECT  SOUND"; }
    juce::String subLine() const override { return "INTERSECT's signal chain, as its own window lays it out: the clip (SAMPLE) or one chop (SLICE), under INTERSECT's own names."; }
    juce::String countLine() const override
    {
        int n = 0, live = 0; for (auto* c : cells()) if (c->id >= 0 && c->id < 1000) { ++n; live += c->on ? 1 : 0; }
        return juce::String (n) + " original controls: " + juce::String (live) + " live now, " + juce::String (n - live) + " greyed (each says why). The same values as CHOP's SOUND panel.";
    }
    std::vector<std::pair<juce::String, juce::String>> sideRows() const override
    {
        auto& e = engine();
        return { { "EDITING", slice ? "SLICE " + juce::String (sel + 1) + " (one chop)" : juce::String ("SAMPLE (the clip)") },
                 { "CHOPS", juce::String (e.sliceCount()) }, { "VOICES", juce::String (e.voices()) },
                 { "ALGO", juce::StringArray { "Repitch", "Signalsmith", "Bungee" }[juce::jlimit (0, 2, juce::roundToInt (read (P::FieldAlgorithm)))] },
                 { "MODULES", "TIME/PITCH, FILTER, AMP, PLAYBACK" } };
    }
    void setScope (bool s) override { slice = s; sync(); resized(); }
    bool pressButton (const juce::String& b) override
    {
        if (b == "SAMPLE") { setScope (false); return true; }
        if (b == "SLICE") { setScope (true); return true; }
        return false;
    }

    void sync() override
    {
        auto& e = engine();
        const bool clip = e.hasClip() && e.sliceCount() > 0;
        if (auto* cp = ed.chopPage()) sel = juce::jlimit (0, juce::jmax (0, e.sliceCount() - 1), cp->selectedChop);
        const bool sc = slice;
        sliceTab.setButtonText ("SLICE " + juce::String (sel + 1));
        sampleTab.setToggleState (! sc, juce::dontSendNotification); sliceTab.setToggleState (sc, juce::dontSendNotification);
        sampleTab.setEnabled (clip); sliceTab.setEnabled (clip);
        prevB.setEnabled (clip && sel > 0); nextB.setEnabled (clip && sel + 1 < e.sliceCount());
        for (auto& [f, c] : byField)
        {
            c->set (clip ? read (f) : c->value);
            c->own = clip && sc && f != P::FieldOutputBus && e.sliceOverrides (sel, f);
        }
        // greyed when it does nothing now (Brandon's rule): INTERSECT's own rules, and Ghost Voice's single output
        const int algo = juce::roundToInt (read (P::FieldAlgorithm));
        const bool stretch = read (P::FieldStretchEnabled) > 0.5f, filt = read (P::FieldFilterEnabled) > 0.5f, loop = juce::roundToInt (read (P::FieldLoop)) != 0;
        const bool envMoves = std::abs (read (P::FieldFilterEnvAmount)) > 0.0f;
        const juce::String noClip = "Load a clip on the CHOP page first.";
        auto grey = [&] (int f, bool ok, const juce::String& why) { if (auto it = byField.find (f); it != byField.end()) it->second->setOn (clip && ok, clip ? why : noClip); };
        for (auto& [f, c] : byField) grey (f, true, {});
        grey (P::FieldBpm, stretch, "BPM is only used with STRETCH ON (it is the chop's own tempo, to follow the song's).");
        grey (P::FieldPitch, ! (algo == 0 && stretch), "With Repitch and STRETCH ON the song's tempo sets the pitch.");
        grey (P::FieldCentsDetune, ! (algo == 0 && stretch), "With Repitch and STRETCH ON the song's tempo sets the pitch.");
        grey (P::FieldRepitchMode, algo == 0, "MODE only works with ALGO Repitch.");
        for (int f : { (int) P::FieldTonality, (int) P::FieldFormant, (int) P::FieldFormantComp }) grey (f, algo == 1, "Only works with ALGO Signalsmith.");
        grey (P::FieldGrainMode, algo == 2, "GRAIN only works with ALGO Bungee.");
        for (int f : { (int) P::FieldFilterType, (int) P::FieldFilterSlope, (int) P::FieldFilterCutoff, (int) P::FieldFilterReso, (int) P::FieldFilterDrive,
                       (int) P::FieldFilterAsym, (int) P::FieldFilterKeyTrack, (int) P::FieldFilterEnvAmount })
            grey (f, filt, "The filter is OFF (switch it on in the FILTER header).");
        for (int f : { (int) P::FieldFilterEnvAttack, (int) P::FieldFilterEnvDecay, (int) P::FieldFilterEnvSustain, (int) P::FieldFilterEnvRelease })
            grey (f, filt && envMoves, filt ? "AMT is 0, so the filter's envelope moves nothing." : "The filter is OFF (switch it on in the FILTER header).");
        grey (P::FieldCrossfade, loop, "FADE only works when LOOP is on.");
        grey (P::FieldOutputBus, false, "Ghost Voice has one output: every chop plays on OUT 1.");
        filterOn->set (filt ? 1.0f : 0.0f); filterOn->own = clip && sc && e.sliceOverrides (sel, P::FieldFilterEnabled);
        filterOn->setOn (clip, noClip);
        setBpm->setOn (clip && stretch, clip ? "BPM is only used with STRETCH ON." : noClip);
        // the context row
        const auto views = e.slices();
        chop::SliceView cur {}; for (auto& v : views) if (v.index == sel) cur = v;
        const bool range = cur.highNote != cur.note;
        const double rate = e.rate();
        ctxRange->fmt = [cur, rate] (float) { return juce::String (cur.r.a) + "-" + juce::String (cur.r.b) + " (" + trimmed ((float) ((double) cur.r.length() / rate), 2) + "s)"; };
        ctxRange->set ((float) cur.r.a + (float) cur.r.b * 1e-3f); ctxRange->repaint();
        ctxMode->fmt = [range] (float) { return juce::String (range ? "RANGE" : "NOTE"); }; ctxMode->set (range ? 1.0f : 0.0f); ctxMode->repaint();
        if (ctxLow->name != (range ? "LOW" : "NOTE")) { ctxLow->name = range ? "LOW" : "NOTE"; ctxLow->repaint(); }
        ctxLow->fmt = [] (float v) { return juce::String (juce::roundToInt (v)); }; ctxLow->set ((float) cur.note);
        ctxHigh->fmt = ctxLow->fmt; ctxHigh->set ((float) cur.highNote);
        const int sroot = juce::roundToInt (e.sliceParam (sel, P::FieldSliceRootNote));
        ctxRoot->fmt = ctxLow->fmt; ctxRoot->set ((float) sroot);
        ctxName->fmt = [cur, range] (float) { return range ? noteName (cur.note) + "-" + noteName (cur.highNote) : noteName (cur.note); }; ctxName->set ((float) (cur.note * 200 + cur.highNote)); ctxName->repaint();
        const auto keysWhy = juce::String ("Ghost Voice puts each chop on its own key (KEYS on the CHOP page), so it is set for you.");
        for (auto* c : { ctxMode, ctxLow, ctxHigh, ctxRoot }) c->setOn (false, clip ? keysWhy : noClip);
        ctxRange->setOn (clip, noClip); ctxName->setOn (clip, noClip);
        auto c = e.clip();
        const auto clipName = c != nullptr ? c->name + "   " + trimmed ((float) c->seconds(), 2) + " s" : juce::String ("no clip");
        ctxInfo->fmt = [clipName] (float) { return clipName; }; ctxInfo->set ((float) clipName.hashCode()); ctxInfo->repaint();
        ctxInfo->setOn (clip, noClip);
        ctxSlices->fmt = ctxLow->fmt; ctxSlices->set ((float) e.sliceCount()); ctxSlices->setOn (clip, noClip);
        ctxRootNote->fmt = ctxLow->fmt; ctxRootNote->set ((float) e.engineRootNote());
        ctxRootNote->setOn (false, "INTERSECT lets ROOT change only before there are slices; Ghost Voice always starts the chops on C1 (36).");
        ctxVoices->set ((float) e.voices()); ctxVoices->setOn (clip, noClip);
        // each module's override count (INTERSECT's own number in the module's header)
        for (int m = 0; m < 4; ++m) { mods[m].overrides = 0; for (auto& row : mods[m].rows) for (auto& [cell, w] : row) mods[m].overrides += cell->own ? 1 : 0; }
        mods[0].note = slice ? "" : "";
        if (sc != lastScope || (bool) range != lastRange) { lastScope = sc; lastRange = range; resized(); }
        juce::String bk; for (auto& m : mods) bk << m.overrides << "|";
        bk << (int) filt;
        if (bk != bodyKey) { bodyKey = bk; body.repaint(); }
        repaint (ctxArea);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        ctxArea = r.removeFromBottom (Cell::kHeight + 14);
        r.removeFromBottom (8);
        vp.setBounds (r);
        const int w = r.getWidth() - (needsScroll (r.getHeight()) ? vp.getScrollBarThickness() + 4 : 0);
        int y = 0;
        auto layoutMod = [&] (Mod& m, juce::Rectangle<int> a)
        {
            m.r = a;
            auto in = a.reduced (10, 0).withTrimmedTop (28).withTrimmedBottom (8);
            // the rows share the module's height evenly (a taller window spreads them; no empty band at the foot)
            const int spare = juce::jmax (0, in.getHeight() - (int) m.rows.size() * Cell::kHeight), step = spare / (int) (m.rows.size() + 1);
            in.removeFromTop (juce::jmax (0, step - 3));
            for (size_t ri = 0; ri < m.rows.size(); ++ri)
            {
                auto row = in.removeFromTop (Cell::kHeight); in.removeFromTop (step);
                float total = 0; int gaps = 0;
                for (size_t i = 0; i < m.rows[ri].size(); ++i) { total += m.rows[ri][i].second; gaps += i + 1 < m.rows[ri].size() ? gapAfter (m.rows[ri][i].first) : 0; }
                const float unit = ((float) row.getWidth() - (float) gaps) / juce::jmax (1.0f, total);
                float x = (float) row.getX();
                for (auto& [cell, wgt] : m.rows[ri])
                {
                    const int x0 = juce::roundToInt (x), x1 = juce::roundToInt (x + unit * wgt);
                    cell->setBounds (x0, row.getY(), x1 - x0, Cell::kHeight);
                    x += unit * wgt + (float) gapAfter (cell);
                }
            }
            if (m.header != nullptr)
            {
                const int tw = (int) std::ceil (choir::width (display (15.0f), m.name)) + 34;
                m.header->setBounds (a.getX() + tw, a.getY() + 1, m.header->kind == Cell::Kind::button ? 84 : 64, Cell::kBoxH);
            }
        };
        // INTERSECT's four modules in their order, wrapped into two columns read top to bottom: TIME/PITCH and FILTER (its widest
        // rows: 7 and 6 cells), then AMP and PLAYBACK (3 cells each). Each column is as wide as its rows need, so every cell is alike
        const int modH = juce::jmax (28 + 2 * Cell::kHeight + 6 + 8 + 8, (r.getHeight() - 10) / 2);
        const int lw = (w - 10) * 7 / 10;
        layoutMod (mods[0], { 0, 0, lw, modH });
        layoutMod (mods[1], { 0, modH + 10, lw, modH });
        layoutMod (mods[2], { lw + 10, 0, w - lw - 10, modH });
        layoutMod (mods[3], { lw + 10, modH + 10, w - lw - 10, modH });
        y = 2 * modH + 10;
        body.setSize (w, y);
        // the context row: SAMPLE | SLICE n (with the chop before / after), then what INTERSECT shows for that level
        auto c = ctxArea.reduced (12, 7);
        auto boxes = c.withTrimmedTop (Cell::kNameH).withHeight (Cell::kBoxH);
        auto tabs = boxes.removeFromLeft (290);
        sampleTab.setBounds (tabs.removeFromLeft (92)); tabs.removeFromLeft (4);
        sliceTab.setBounds (tabs.removeFromLeft (112)); tabs.removeFromLeft (8);
        prevB.setBounds (tabs.removeFromLeft (34)); tabs.removeFromLeft (4); nextB.setBounds (tabs.removeFromLeft (34));
        tabPlain = juce::Rectangle<int> (sampleTab.getX(), boxes.getBottom() + 3, nextB.getRight() - sampleTab.getX(), Cell::kPlainH);
        c.removeFromLeft (300);
        for (auto* k : { ctxRange, ctxMode, ctxLow, ctxHigh, ctxRoot, ctxName, ctxInfo, ctxSlices, ctxRootNote, ctxVoices }) k->setVisible (false);
        auto place = [&] (Cell* k, int width) { k->setVisible (true); k->setBounds (c.removeFromLeft (width).withHeight (Cell::kHeight)); c.removeFromLeft (8); };
        const bool range = lastRange;
        if (slice)
        {
            const int n = range ? 3 : 1, rest = c.getWidth() - 8 * (n + 2);
            const int each = juce::jlimit (70, 120, rest / (n + 4));
            place (ctxRange, juce::jmax (150, rest - each * (n + 2)));
            place (ctxMode, each);
            place (ctxLow, each);
            if (range) { place (ctxHigh, each); place (ctxRoot, each); }
            place (ctxName, c.getWidth());
        }
        else
        {
            const int each = juce::jlimit (90, 130, (c.getWidth() - 32) / 5);
            place (ctxInfo, c.getWidth() - 3 * (each + 8));
            place (ctxSlices, each); place (ctxRootNote, each); place (ctxVoices, each);
        }
    }

    void paint (juce::Graphics& g) override
    {   // the context row's frame and its line (cached)
        const auto& C = ed.cp();
        juce::String k; k << (int) C.dark << ctxArea.toString() << tabPlain.toString() << (int) sampleTab.isEnabled();
        ctxArt.draw (g, k, ctxArea.expanded (2, 4), [this, &C] (juce::Graphics& ig, juce::Rectangle<int>)
        {
            ig.addTransform (juce::AffineTransform::translation ((float) -(ctxArea.getX() - 2), (float) -(ctxArea.getY() - 4)));
            choir::page (ig, C, ctxArea.toFloat());
            plainLine (ig, C, "SAMPLE: the clip, every chop follows it.  SLICE: this chop only.", tabPlain, sampleTab.isEnabled());
            ig.setColour (C.edgeDim); ig.fillRect ((float) tabPlain.getRight() + 5, (float) ctxArea.getY() + 12, 1.0f, (float) ctxArea.getHeight() - 24);
        });
    }
    choir::Cache ctxArt;

private:
    void timerCallback() override { if (onScreen (*this)) sync(); }
    static bool needsScroll (int h) { return 2 * (28 + 2 * Cell::kHeight + 6 + 8 + 8) + 10 > h; }
    Editor& ed;
    struct Mod { juce::String name; std::vector<std::vector<std::pair<Cell*, float>>> rows; Cell* header = nullptr; juce::Rectangle<int> r; int overrides = 0; juce::String note; };
    Mod mods[4];
    juce::OwnedArray<Cell> owned;
    std::map<int, Cell*> byField;
    std::map<Cell*, int> gaps;
    Painted body; juce::Viewport vp;
    juce::TextButton sampleTab { "SAMPLE" }, sliceTab { "SLICE" }, prevB { "<" }, nextB { ">" };
    Cell *setBpm = nullptr, *filterOn = nullptr;
    Cell *ctxRange, *ctxMode, *ctxLow, *ctxHigh, *ctxRoot, *ctxName, *ctxInfo, *ctxSlices, *ctxRootNote, *ctxVoices;
    juce::Rectangle<int> ctxArea, tabPlain;
    bool slice = true, lastScope = true, lastRange = false;
    int sel = 0;
    juce::String bodyKey;
    choir::Cache bodyArt;

    chop::ChopEngine& engine() const { return *ed.proc.chopper; }
    int gapAfter (Cell* c) const { if (auto it = gaps.find (c); it != gaps.end()) return it->second; return 6; }
    static float scaleOf (int f)
    {   // a chop keeps times in seconds and sustains as 0..1; INTERSECT's SAMPLE tab (and this page) shows ms and %
        switch (f)
        {
            case P::FieldAttack: case P::FieldDecay: case P::FieldRelease: case P::FieldFilterEnvAttack: case P::FieldFilterEnvDecay: case P::FieldFilterEnvRelease: return 1000.0f;
            case P::FieldSustain: case P::FieldFilterEnvSustain: return 100.0f;
            default: return 1.0f;
        }
    }
    float read (int f) const
    {
        auto& e = engine();
        if (! e.hasClip() || e.sliceCount() == 0) return 0.0f;
        return slice ? e.sliceParam (sel, f) * scaleOf (f) : e.sampleParam (f);
    }
    void write (int f, float v, bool dragging)
    {
        auto& e = engine();
        if (! e.hasClip() || e.sliceCount() == 0) return;
        if (slice) e.setSliceParam (sel, f, v / scaleOf (f));
        else e.setSampleParam (f, v, ! dragging);
    }
    Cell* make (int id, const juce::String& name, const juce::String& plain, Cell::Kind k)
    {
        auto* c = owned.add (new Cell());
        c->P = &ed.cp(); c->id = id; c->name = name; c->plain = plain; c->kind = k;
        c->tip = name + ": " + plain;
        c->setTooltip (c->tip);
        return c;
    }
    Cell* add (int m, int row, float weight, int field, const juce::String& name, const juce::String& plain, Cell::Kind k, float lo, float hi,
               float step = 1, float perPx = 0, std::function<juce::String (float)> fmt = nullptr, int gapAfterIt = 6, juce::StringArray choices = {})
    {
        auto* c = make (field, name, plain, k);
        c->lo = lo; c->hi = hi; c->step = step; c->perPx = perPx; c->fmt = std::move (fmt); c->choices = std::move (choices);
        c->onChange = [this, field] (float v, bool d) { write (field, v, d); };
        if ((int) mods[m].rows.size() <= row) mods[m].rows.resize ((size_t) row + 1);
        mods[m].rows[(size_t) row].push_back ({ c, weight });
        gaps[c] = gapAfterIt;
        byField[field] = c;
        body.addAndMakeVisible (c);
        return c;
    }
    Cell* header (int m, int id, const juce::String& name, Cell::Kind k, const juce::String& plain)
    {
        auto* c = make (id, k == Cell::Kind::button ? name : juce::String(), plain, k);
        c->lo = 0; c->hi = 1; c->showPlain = false; c->compact = true; c->tip = name + ": " + plain; c->setTooltip (c->tip);
        mods[m].header = c;
        body.addAndMakeVisible (c);
        return c;
    }
    Cell* ctx (int id, const juce::String& name, const juce::String& plain, Cell::Kind k)
    {
        auto* c = make (id, name, plain, k);
        addChildComponent (c);
        return c;
    }
    void stepChop (int d)
    {
        auto& e = engine();
        const int n = juce::jlimit (0, juce::jmax (0, e.sliceCount() - 1), sel + d);
        if (auto* cp = ed.chopPage()) cp->selectChop (n);
        sync();
    }
    void setBpmMenu()
    {   // INTERSECT's own SET BPM menu
        juce::PopupMenu menu;
        menu.addSectionHeader ("Set BPM");
        const char* names[] { "16 Bars", "8 Bars", "4 Bars", "2 Bars", "1 Bar", "1/2 Note", "1/4 Note", "1/8 Note", "1/16 Note" };
        for (int i = 0; i < 9; ++i) menu.addItem (i + 1, names[i]);
        juce::Component::SafePointer<IntersectSound> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (setBpm), [safe] (int r)
        {
            if (safe == nullptr || r <= 0 || r > 9) return;
            const float bars[] { 0.0f, 16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f, 0.0625f };
            safe->engine().setBpmFromLength (safe->sel, safe->slice, bars[r]);
        });
    }
    void paintBody (juce::Graphics& g)
    {
        const auto& C = ed.cp();
        juce::String k; k << (int) C.dark << body.getWidth() << "x" << body.getHeight() << "|" << bodyKey;
        bodyArt.draw (g, k, body.getLocalBounds(), [this, &C] (juce::Graphics& ig, juce::Rectangle<int>)
        {
            const juce::Colour accents[] { C.glass[1], C.glass[2], C.glass[3], C.glass[4] };
            for (int m = 0; m < 4; ++m)
            {
                const auto& md = mods[m];
                moduleFrame (ig, C, md.r.toFloat(), md.name, accents[m], md.overrides > 0 ? juce::String (md.overrides) + (md.overrides == 1 ? " override" : " overrides") : juce::String());
                if (md.header != nullptr)
                {   // the header control's plain line beside it
                    auto hr = juce::Rectangle<int> (md.header->getRight() + 10, md.r.getY() + 2, md.r.getRight() - md.header->getRight() - 130, 22);
                    plainLine (ig, C, md.header->plain, hr, md.header->on, juce::Justification::centredLeft, 1);
                }
                for (size_t ri = 1; ri < md.rows.size(); ++ri)
                    if (! md.rows[ri].empty())
                    {
                        const auto* prev = md.rows[ri - 1].front().first; const auto* next = md.rows[ri].front().first;
                        const float y = std::floor (((float) prev->getBottom() + (float) next->getY()) * 0.5f);
                        ig.setColour (C.edgeDim.withMultipliedAlpha (0.45f)); ig.fillRect ((float) md.r.getX() + 12, y, (float) md.r.getWidth() - 24, 1.0f);
                    }
            }
        });
    }
};

// ================================================================================================ INTERSECT: AUTO CHOP (its AutoChopPanel)
class IntersectAutoChop : public Panel, private juce::Timer
{
public:
    explicit IntersectAutoChop (Editor& e) : ed (e)
    {
        using K = Cell::Kind;
        auto& s = engine().split;
        sens = make (idSens, "SENS", "how easily a new chop starts", K::number);
        sens->lo = 0; sens->hi = 100; sens->step = 1; sens->perPx = 0.5f; sens->value = s.sens; sens->fmt = [] (float v) { return juce::String (juce::roundToInt (v)); };
        minC = make (idMin, "MIN", "the shortest chop it makes", K::number);
        minC->lo = 20; minC->hi = 500; minC->step = 1; minC->perPx = 2.4f; minC->value = s.minMs; minC->fmt = [] (float v) { return juce::String (juce::roundToInt (v)) + "ms"; };
        div = make (idDiv, "DIV", "how many equal pieces", K::number);
        div->lo = 2; div->hi = 128; div->step = 1; div->perPx = 0.63f; div->value = (float) s.div; div->fmt = [] (float v) { return juce::String (juce::roundToInt (v)); };
        trans = make (-1, "SPLIT TRANSIENTS", "cuts the chop at the hits it finds", K::button);
        equal = make (-1, "SPLIT EQUAL", "cuts the chop into DIV equal pieces", K::button);
        cancel = make (-1, "CANCEL", "closes INTERSECT's pop-up", K::button);
        undoC = make (-1, "UNDO", "takes the last split back", K::button);
        sens->onChange = [this] (float v, bool) { engine().split.sens = v; dirty = true; };
        minC->onChange = [this] (float v, bool) { engine().split.minMs = v; dirty = true; };
        div->onChange = [this] (float v, bool) { engine().split.div = juce::roundToInt (v); repaint(); };
        trans->onPress = [this] { pressButton ("SPLIT TRANSIENTS"); };
        equal->onPress = [this] { pressButton ("SPLIT EQUAL"); };
        undoC->onPress = [this] { engine().undo(); dirty = true; };
        for (auto* b : { &prevB, &nextB }) { styleButton (*b); addAndMakeVisible (*b); }
        prevB.onClick = [this] { stepChop (-1); }; nextB.onClick = [this] { stepChop (1); };
        prevB.setTooltip ("Split the chop before"); nextB.setTooltip ("Split the next chop");
        startTimerHz (15);
    }
    juce::String title() const override { return "INTERSECT  AUTO CHOP"; }
    juce::String subLine() const override { return "INTERSECT's own auto-chop panel: split one chop at its transients, or into equal pieces. The pieces land on the keys (Cmd+Z undoes)."; }
    juce::String countLine() const override
    {
        return "SENS " + juce::String (juce::roundToInt (sens->value)) + ", MIN " + juce::String (juce::roundToInt (minC->value)) + " ms: " + juce::String ((int) points.size())
             + " transients found, so SPLIT TRANSIENTS makes " + juce::String (points.empty() ? 0 : (int) points.size() + 1) + " pieces. SPLIT EQUAL makes " + juce::String (juce::roundToInt (div->value)) + ".";
    }
    std::vector<std::pair<juce::String, juce::String>> sideRows() const override
    {
        return { { "SPLITS", target() }, { "SENS", juce::String (juce::roundToInt (sens->value)) }, { "MIN", juce::String (juce::roundToInt (minC->value)) + " ms" },
                 { "DIV", juce::String (juce::roundToInt (div->value)) }, { "TRANSIENTS FOUND", juce::String ((int) points.size()) } };
    }
    bool pressButton (const juce::String& b) override
    {
        auto& e = engine();
        if (! e.hasClip() || e.sliceCount() == 0) return false;
        bool ok = false;
        if (b == "SPLIT EQUAL") ok = e.splitChop (sel, e.equalPoints (sel, juce::roundToInt (div->value)), "SPLIT EQUAL");
        else if (b == "SPLIT TRANSIENTS") { compute(); ok = ! points.empty() && e.splitChop (sel, points, "SPLIT TRANSIENTS"); }
        else if (b == "UNDO") ok = e.undo();
        dirty = true;
        if (auto* cp = ed.chopPage()) cp->showAll();
        return ok;
    }
    void sync() override
    {
        auto& e = engine();
        const bool clip = e.hasClip() && e.sliceCount() > 0;
        if (auto* cp = ed.chopPage()) { const int s = juce::jlimit (0, juce::jmax (0, e.sliceCount() - 1), cp->selectedChop); if (s != sel) { sel = s; dirty = true; } }
        if (e.engineVersion() != version) { version = e.engineVersion(); dirty = true; }
        sens->set (e.split.sens); minC->set (e.split.minMs); div->set ((float) e.split.div);
        const juce::String noClip = "Load a clip on the CHOP page first.";
        for (auto* c : { sens, minC, div, equal }) c->setOn (clip, noClip);
        trans->setOn (clip && ! points.empty(), clip ? "No transients found at this SENS / MIN: raise SENS or lower MIN." : noClip);
        cancel->setOn (false, "In INTERSECT this closes its pop-up panel. Here the panel is a page of its own, so there is nothing to close.");
        undoC->setOn (e.canUndo(), "Nothing to undo.");
        prevB.setEnabled (clip && sel > 0); nextB.setEnabled (clip && sel + 1 < e.sliceCount());
        if (dirty && onScreen (*this)) compute();
        repaint();
    }
    void resized() override
    {
        auto r = getLocalBounds();
        top = r.removeFromTop (Cell::kHeight + 24);
        auto row = top.reduced (14, 10);
        // INTERSECT's order: SENS, MIN, SPLIT TRANSIENTS, a wider gap, DIV, SPLIT EQUAL ... CANCEL on the right
        const int unit = juce::jmax (70, (row.getWidth() - 6 * 8 - 24) / 10);
        sens->setBounds (row.removeFromLeft (unit + 10).withHeight (Cell::kHeight)); row.removeFromLeft (8);
        minC->setBounds (row.removeFromLeft (unit + 10).withHeight (Cell::kHeight)); row.removeFromLeft (8);
        trans->setBounds (row.removeFromLeft (unit * 2 + 30).withHeight (Cell::kHeight)); row.removeFromLeft (8 + 24);
        div->setBounds (row.removeFromLeft (unit + 10).withHeight (Cell::kHeight)); row.removeFromLeft (8);
        equal->setBounds (row.removeFromLeft (unit + 50).withHeight (Cell::kHeight)); row.removeFromLeft (8);
        cancel->setBounds (row.removeFromRight (unit + 10).withHeight (Cell::kHeight)); row.removeFromRight (8);
        undoC->setBounds (row.withHeight (Cell::kHeight));
        r.removeFromTop (10);
        below = r;
        auto t = r.reduced (14, 10).removeFromTop (30);
        nextB.setBounds (t.removeFromRight (34).reduced (0, 1)); t.removeFromRight (4);
        prevB.setBounds (t.removeFromRight (34).reduced (0, 1));
        waveR = r.reduced (14, 10).withTrimmedTop (40).withTrimmedBottom (34);
    }
    void paint (juce::Graphics& g) override
    {   // (everything here is cached in one image until something it shows changes)
        juce::String k; k << (int) ed.cp().dark << getWidth() << "x" << getHeight() << "|" << target() << "|" << (int) points.size() << "|" << (points.empty() ? 0 : (int) points.front()) << "|" << juce::roundToInt (div->value) << "|" << (juce::pointer_sized_int) engine().clip().get() << "|" << (int) engine().keys << "|" << (int) version;
        panelArt.draw (g, k, getLocalBounds(), [this] (juce::Graphics& ig, juce::Rectangle<int>) { paintArt (ig); });
    }
    void paintArt (juce::Graphics& g)
    {
        const auto& C = ed.cp();
        choir::page (g, C, top.toFloat());
        choir::page (g, C, below.toFloat());
        auto t = below.reduced (14, 10).removeFromTop (30).withTrimmedRight (90);
        g.setColour (C.dark ? C.gold : C.edge.darker (0.2f)); g.setFont (display (15.0f));
        g.drawText ("SPLITS:", t.removeFromLeft (80), juce::Justification::centredLeft, false);
        g.setColour (C.ink); g.setFont (serif (16.0f, choir::bold)); g.drawText (target(), t, juce::Justification::centredLeft, true);
        // the chop's wave with INTERSECT's preview: the transients it found (lines), and where SPLIT EQUAL would cut (dashes)
        auto& e = engine();
        auto wr = waveR.toFloat();
        g.setColour (C.waveBg); g.fillRoundedRectangle (wr, 4.0f);
        g.setColour (C.edgeDim); g.drawRoundedRectangle (wr, 4.0f, 1.0f);
        const auto rr = region();
        auto c = e.clip();
        if (c != nullptr && ! rr.empty())
        {
            const auto& wave = peaks (c, rr, (int) wr.getWidth());
            const float mid = wr.getCentreY(), h = wr.getHeight() * 0.45f;
            g.setColour (C.waveInk.withAlpha (0.85f));
            for (size_t i = 0; i < wave.size(); ++i) g.fillRect (wr.getX() + (float) i, mid - wave[i] * h, 1.0f, juce::jmax (1.0f, 2.0f * wave[i] * h));
            auto xOf = [&] (juce::int64 f) { return wr.getX() + (float) ((double) (f - rr.a) / (double) rr.length()) * wr.getWidth(); };
            g.setColour (C.gold.withAlpha (0.55f));
            for (auto f : e.equalPoints (sel, juce::roundToInt (div->value)))
            { const float x = xOf (f); for (float y = wr.getY() + 3; y < wr.getBottom() - 3; y += 8) g.fillRect (x, y, 1.0f, 4.0f); }
            g.setColour (C.play);
            for (auto f : points) g.fillRect (xOf (f) - 0.5f, wr.getY() + 2, 1.5f, wr.getHeight() - 4);
        }
        else { g.setColour (C.dim); g.setFont (serif (16.0f, choir::italic)); g.drawText ("Load a clip on the CHOP page: its chops show here.", wr, juce::Justification::centred, false); }
        auto legend = below.reduced (14, 10).removeFromBottom (24);
        plainLine (g, C, "Solid lines: where SPLIT TRANSIENTS cuts (the hits it found).   Dashes: where SPLIT EQUAL cuts.   The chop's own sound settings go to every piece.", legend, true, juce::Justification::centredLeft, 1);
    }
private:
    Editor& ed;
    juce::OwnedArray<Cell> owned;
    Cell *sens, *minC, *div, *trans, *equal, *cancel, *undoC;
    juce::TextButton prevB { "<" }, nextB { ">" };
    juce::Rectangle<int> top, below, waveR;
    std::vector<juce::int64> points;
    int sel = 0; bool dirty = true; juce::uint32 version = 0;
    juce::String peakKey; std::vector<float> peakCache;
    choir::Cache panelArt;
    chop::ChopEngine& engine() const { return *ed.proc.chopper; }
    Cell* make (int id, const juce::String& name, const juce::String& plain, Cell::Kind k)
    {
        auto* c = owned.add (new Cell()); c->P = &ed.cp(); c->id = id; c->name = name; c->plain = plain; c->kind = k; c->tip = name + ": " + plain; c->setTooltip (c->tip);
        addAndMakeVisible (c); return c;
    }
    void timerCallback() override { if (onScreen (*this)) sync(); }
    void compute()
    {
        dirty = false;
        auto& e = engine();
        points = e.hasClip() && e.sliceCount() > 0 ? e.transientPoints (sel, sens->value, minC->value) : std::vector<juce::int64>();
        trans->setOn (e.hasClip() && ! points.empty(), "No transients found at this SENS / MIN: raise SENS or lower MIN.");
        repaint();
    }
    void stepChop (int d)
    {
        auto& e = engine();
        if (auto* cp = ed.chopPage()) cp->selectChop (juce::jlimit (0, juce::jmax (0, e.sliceCount() - 1), sel + d));
        sync();
    }
    chop::Region region() const
    {
        for (auto& v : engine().slices()) if (v.index == sel) return v.r;
        return {};
    }
    juce::String target() const
    {
        auto& e = engine();
        if (! e.hasClip() || e.sliceCount() == 0) return "no clip yet";
        const auto r = region();
        const auto len = trimmed ((float) ((double) r.length() / e.rate()), 2) + " s";
        if (e.keys == chop::KeysMode::whole || e.chops.empty()) return "THE WHOLE CLIP (KEYS: WHOLE), " + len;
        chop::SliceView cur {}; for (auto& v : e.slices()) if (v.index == sel) cur = v;
        return "SLICE " + juce::String (sel + 1) + " of " + juce::String (e.sliceCount()) + "  (on " + noteName (cur.note) + "), " + len;
    }
    const std::vector<float>& peaks (const chop::ClipPtr& c, chop::Region r, int w)
    {
        juce::String k; k << (juce::pointer_sized_int) c.get() << "|" << r.a << "|" << r.b << "|" << w;
        if (k == peakKey) return peakCache;
        peakKey = k; peakCache.assign ((size_t) juce::jmax (1, w), 0.0f);
        const auto n = r.length();
        for (int i = 0; i < w; ++i)
        {
            const auto a = r.a + n * i / w, b = juce::jmax (a + 1, r.a + n * (i + 1) / w);
            float m = 0;
            for (int ch = 0; ch < c->audio.getNumChannels(); ++ch)
            { const float* s = c->audio.getReadPointer (ch); for (auto f = a; f < b && f < c->frames(); f += juce::jmax ((juce::int64) 1, (b - a) / 64)) m = juce::jmax (m, std::abs (s[f])); }
            peakCache[(size_t) i] = juce::jmin (1.0f, m);
        }
        return peakCache;
    }
};

// ================================================================================================ OpenVPI audio-slicer: PHRASES (slicer2.py's own options)
class Phrases : public Panel, private juce::Timer
{
public:
    explicit Phrases (Editor& e) : ed (e)
    {
        // slicer2.py's argparse, in its order: audio, --out, --db_thresh, --min_length, --min_interval, --hop_size, --max_sil_kept
        rows.push_back ({ "audio", "", "str", "The audio to be sliced", "(required)", "the clip on the CHOP page, as Ghost Voice saved it", nullptr });
        rows.push_back ({ "--out", "", "str", "Output directory of the sliced audio clips", "the audio's own folder", "not used: Ghost Voice reads where the cuts are and writes no files", nullptr });
        auto num = [this] (const char* flag, const char* readme, const char* type, const char* help, const char* def, int id, const char* plain, float lo, float hi, float step, float perPx, juce::String unit)
        {
            Row r { flag, readme, type, help, def, {}, make (id, juce::String (flag).fromFirstOccurrenceOf ("--", false, false), plain) };
            r.cell->lo = lo; r.cell->hi = hi; r.cell->step = step; r.cell->perPx = perPx;
            r.cell->fmt = [unit, step] (float v) { return (step < 1 ? juce::String (v, 1) : juce::String (juce::roundToInt (v))) + unit; };
            r.cell->onChange = [this, id] (float v, bool) { setValue (id, v); };
            rows.push_back (r);
        };
        num ("--db_thresh", "db_threshold", "float", "The dB threshold for silence detection", "-40", idDb, "quieter than this counts as silence", -80, 0, 0.5f, 0.2f, " dB");
        num ("--min_length", "min_length", "int", "The minimum milliseconds required for each sliced audio clip", "5000", idMinLength, "the shortest phrase it makes", 10, 30000, 10, 20.0f, " ms");
        num ("--min_interval", "min_interval", "int", "The minimum milliseconds for a silence part to be sliced", "300", idMinInterval, "the shortest silence it cuts at", 10, 30000, 5, 5.0f, " ms");
        num ("--hop_size", "hop_size", "int", "Frame length in milliseconds", "10", idHop, "how finely it listens", 1, 1000, 1, 0.5f, " ms");
        num ("--max_sil_kept", "max_silence_kept", "int", "The maximum silence length kept around the sliced clip, presented in milliseconds", "500", idMaxSil, "silence kept around each phrase", 1, 30000, 10, 5.0f, " ms");
        for (auto* b : { &startB, &defaultsB, &useB, &runB }) { styleButton (*b); addAndMakeVisible (*b); }
        startB.setTooltip ("Every option back to Ghost Voice's start (-40 dB, 500, 150, 10, 200 ms): PHRASES then works exactly as it always has");
        defaultsB.setTooltip ("slicer2.py's own command-line defaults (-40 dB, 5000, 300, 10, 500 ms): made for long recordings, so a short clip may give one phrase");
        useB.setTooltip ("Switch CHOP's AUTO CHOP to PHRASES, so these options are used");
        runB.setTooltip ("AUTO CHOP with PHRASES now, with these options (the chops land on the keys from C1; Cmd+Z undoes)");
        startB.onClick = [this] { pressButton ("GHOST VOICE START"); };
        defaultsB.onClick = [this] { pressButton ("ORIGINAL DEFAULTS"); };
        useB.onClick = [this] { pressButton ("USE PHRASES"); };
        runB.onClick = [this] { pressButton ("AUTO CHOP"); };
        cmdBox.setMultiLine (true, true); cmdBox.setReadOnly (true); cmdBox.setCaretVisible (false); cmdBox.setScrollbarsShown (true);
        cmdBox.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.5f, juce::Font::plain))); cmdBox.setIndents (10, 8);
        cmdBox.setTooltip ("The exact command Ghost Voice starts for PHRASES with these options (the helper passes them to slicer2.py's Slicer)");
        addAndMakeVisible (cmdBox);
        list.painter = [this] (juce::Graphics& g) { paintRows (g); };
        vp.setViewedComponent (&list, false); vp.setScrollBarsShown (true, false); vp.setScrollBarThickness (10);
        addAndMakeVisible (vp);
        applyTheme();
        startTimerHz (10);
    }
    juce::String title() const override { return "OPENVPI  PHRASES"; }
    juce::String subLine() const override { return "OpenVPI audio-slicer's own options (slicer2.py), in its order: how AUTO CHOP's PHRASES finds the silences between sung phrases."; }
    juce::String countLine() const override
    {
        const auto& p = engine().phrase;
        return "7 original options: 5 live, 1 set for you (the clip), 1 not used.   Now: " + juce::String (p.db, 1) + " dB, " + juce::String (p.minLength) + ", " + juce::String (p.minInterval)
             + ", " + juce::String (p.hop) + ", " + juce::String (p.maxSilKept) + " ms" + (p.isDefault() ? "  (Ghost Voice's start)" : "");
    }
    std::vector<std::pair<juce::String, juce::String>> sideRows() const override
    {
        const auto& p = engine().phrase;
        return { { "AUTO CHOP MODE", chop::autoModeName (engine().autoMode) }, { "--db_thresh", juce::String (p.db, 1) + " dB" }, { "--min_length", juce::String (p.minLength) + " ms" },
                 { "--min_interval", juce::String (p.minInterval) + " ms" }, { "--hop_size", juce::String (p.hop) + " ms" }, { "--max_sil_kept", juce::String (p.maxSilKept) + " ms" } };
    }
    bool pressButton (const juce::String& b) override
    {
        auto& e = engine();
        if (b == "AUTO CHOP")
        {
            if (! e.hasClip() || ! e.tools.ready (chop::Tools::Kind::phrases) || ! e.phrase.valid()) return false;
            e.autoMode = chop::AutoMode::phrases;
            const bool ok = e.autoChop();
            if (auto* cp = ed.chopPage()) cp->refreshControls();
            sync(); return ok;
        }
        if (b == "GHOST VOICE START") e.phrase = {};
        else if (b == "ORIGINAL DEFAULTS") { auto& p = e.phrase; p.db = -40; p.minLength = 5000; p.minInterval = 300; p.hop = 10; p.maxSilKept = 500; }   // slicer2.py's own CLI defaults
        else if (b == "USE PHRASES") { e.autoMode = chop::AutoMode::phrases; if (auto* cp = ed.chopPage()) cp->refreshControls(); }
        else return false;
        sync();
        return true;
        return false;
    }
    juce::String commandText() const
    {   // what Tools::run starts (python + the clip are this Mac's; OUT.json is a fresh temp file each time)
        auto& e = engine();
        auto py = e.tools.pythonFor (chop::Tools::Kind::phrases);
        if (py.isEmpty()) py = "python3";
        const auto wav = e.clipFile() != juce::File() ? e.clipFile() : juce::File ("/clip.wav");
        auto c = e.tools.commandFor (chop::Tools::Kind::phrases, py, wav, juce::File ("/tmp/out.json"), e.phrase.args());
        c.removeRange (0, 4);   // (the env settings in front)
        juce::StringArray shown; for (auto& s : c) shown.add (s.startsWithChar ('/') ? choir::friendlyPath (s) : s);
        const auto& p = e.phrase;
        return shown.joinIntoString (" ") + "\n# the same cut with OpenVPI's own command line (it would write the phrases as WAV files):\npython slicer2.py "
             + choir::friendlyPath (wav.getFullPathName()) + " --db_thresh " + juce::String (p.db, 1) + " --min_length " + juce::String (p.minLength) + " --min_interval "
             + juce::String (p.minInterval) + " --hop_size " + juce::String (p.hop) + " --max_sil_kept " + juce::String (p.maxSilKept);
    }
    void sync() override
    {
        auto& e = engine();
        if (ed.cp().dark != themeDark) { themeDark = ed.cp().dark; applyTheme(); }
        const auto& p = e.phrase;
        const bool mode = e.autoMode == chop::AutoMode::phrases;
        const float vals[] { p.db, (float) p.minLength, (float) p.minInterval, (float) p.hop, (float) p.maxSilKept };
        int i = 0;
        for (auto& r : rows)
            if (r.cell != nullptr)
            {
                r.cell->set (vals[i++]);
                r.cell->setOn (mode, "Only used by AUTO CHOP's PHRASES mode (the CHOP page uses " + chop::autoModeName (e.autoMode) + " now): press USE PHRASES.");
            }
        useB.setEnabled (! mode);
        const bool ready = e.tools.ready (chop::Tools::Kind::phrases);
        runB.setEnabled (e.hasClip() && ready && ! e.toolBusy() && p.valid());
        runB.setTooltip (! e.hasClip() ? "Load a clip on the CHOP page first" : ! ready ? "The phrase finder isn't installed: pick PHRASES on the CHOP page and press INSTALL PHRASE FINDER (once)"
                         : ! p.valid() ? "slicer2.py needs min_length >= min_interval >= hop_size, and max_sil_kept >= hop_size" : "AUTO CHOP with PHRASES now, with these options (Cmd+Z undoes)");
        startB.setEnabled (! p.isDefault());
        const auto cmd = commandText();
        if (cmd != shownCmd) { shownCmd = cmd; cmdBox.setText (cmd, false); }
        juce::String k; k << (int) mode << (int) p.valid() << (int) ed.cp().dark; if (k != listKey) { listKey = k; list.repaint(); }
        repaint (statusR);
    }
    void applyTheme()
    {
        const auto& C = ed.cp();
        cmdBox.setColour (juce::TextEditor::backgroundColourId, C.waveBg); cmdBox.setColour (juce::TextEditor::textColourId, C.ink);
        cmdBox.applyColourToAllText (C.ink, true);
    }
    void resized() override
    {
        auto r = getLocalBounds();
        auto t = r.removeFromTop (34);
        defaultsB.setBounds (t.removeFromRight (180)); t.removeFromRight (8);
        startB.setBounds (t.removeFromRight (180)); t.removeFromRight (8);
        runB.setBounds (t.removeFromRight (130)); t.removeFromRight (8);
        useB.setBounds (t.removeFromRight (130)); t.removeFromRight (10);
        statusR = t;
        r.removeFromTop (8);
        const int w = r.getWidth() - vp.getScrollBarThickness() - 2;
        int y = 0;
        for (auto& row : rows)
        {
            row.r = { 0, y, w, row.cell != nullptr ? kRowH : kShortH };
            if (row.cell != nullptr) row.cell->setBounds (w - 14 - 190, y + (kRowH - Cell::kHeight) / 2, 190, Cell::kHeight);
            y += row.r.getHeight() + 4;
        }
        list.setSize (w, y - 4);
        // the list shows whole rows only (never one cut through at the foot); the command box takes what is left
        const int roomForList = r.getHeight() - 20 - 64 - 8;   // (the box holds its three lines whole)
        int listH = 0;
        for (auto& row : rows) if (row.r.getBottom() <= roomForList) listH = row.r.getBottom();
        vp.setBounds (r.removeFromTop (juce::jmax (kRowH, listH)));
        r.removeFromTop (8);
        cmdR = r.removeFromTop (20);
        cmdBox.setBounds (r);
    }
    void paint (juce::Graphics& g) override
    {
        const auto& C = ed.cp();
        auto& e = engine();
        const bool mode = e.autoMode == chop::AutoMode::phrases;
        const auto s = mode ? juce::String ("AUTO CHOP is on PHRASES: these options are used.") : "AUTO CHOP is on " + chop::autoModeName (e.autoMode) + ": these only work with PHRASES.";
        g.setColour (mode ? C.ink : C.warn); g.setFont (choir::fitted (choir::bold, s, (float) statusR.getWidth(), 15.0f, 10.0f));
        g.drawText (s, statusR, juce::Justification::centredLeft, false);
        g.setColour (C.gold); g.setFont (display (14.0f));
        g.drawText ("THE HELPER'S COMMAND  (helper/chop_tools.py, which runs slicer2.py's Slicer)", cmdR, juce::Justification::centredLeft, false);
    }
    bool touch (int id, float v) override { return press (id, v); }
private:
    struct Row { juce::String flag, readme, type, help, def, given; Cell* cell; juce::Rectangle<int> r {}; };
    Editor& ed;
    std::vector<Row> rows;
    juce::OwnedArray<Cell> owned;
    juce::TextButton startB { "GHOST VOICE START" }, defaultsB { "ORIGINAL DEFAULTS" }, useB { "USE PHRASES" }, runB { "AUTO CHOP" };
    juce::TextEditor cmdBox;
    Painted list; juce::Viewport vp;
    juce::Rectangle<int> statusR, cmdR;
    juce::String shownCmd, listKey;
    bool themeDark = true;
    choir::Cache listArt;
    static constexpr int kRowH = 76, kShortH = 66;
    chop::ChopEngine& engine() const { return *ed.proc.chopper; }
    void timerCallback() override { if (onScreen (*this)) sync(); }
    Cell* make (int id, const juce::String& name, const juce::String& plain)
    {
        auto* c = owned.add (new Cell()); c->P = &ed.cp(); c->id = id; c->name = name; c->plain = plain; c->kind = Cell::Kind::number;
        c->tip = name + ": " + plain; c->setTooltip (c->tip); list.addAndMakeVisible (c); return c;
    }
    void setValue (int id, float v)
    {
        auto& p = engine().phrase;
        switch (id)
        {
            case idDb: p.db = v; break;
            case idMinLength: p.minLength = juce::roundToInt (v); break;
            case idMinInterval: p.minInterval = juce::roundToInt (v); break;
            case idHop: p.hop = juce::roundToInt (v); break;
            case idMaxSil: p.maxSilKept = juce::roundToInt (v); break;
            default: break;
        }
        sync();
    }
    void paintRows (juce::Graphics& g)
    {
        const auto& C = ed.cp();
        juce::String k; k << listKey << list.getWidth() << "x" << list.getHeight();
        listArt.draw (g, k, list.getLocalBounds(), [this, &C] (juce::Graphics& ig, juce::Rectangle<int>)
        {
            const bool mode = engine().autoMode == chop::AutoMode::phrases;
            int n = 0;
            for (auto& row : rows)
            {
                ++n;
                auto r = row.r.toFloat().reduced (2, 1);
                choir::page (ig, C, r);
                const bool live = row.cell != nullptr;
                const float a = live ? (mode ? 1.0f : choir::greyA (C)) : 0.75f;
                auto num = r.removeFromLeft (40);
                ig.setColour (choir::greyInk (C, C.gold, a)); ig.setFont (display (20.0f)); ig.drawText (juce::String (n), num, juce::Justification::centred, false);
                ig.setColour (C.edgeDim); ig.fillRect (num.getRight(), r.getY() + 8, 1.0f, r.getHeight() - 16);
                auto left = r.reduced (12, 6).withTrimmedRight (live ? 220.0f : 330.0f);
                ig.setColour (choir::greyInk (C, C.select, a)); ig.setFont (serif (16.0f, choir::bold));
                ig.drawText (row.flag + "     slicer2.py  -  " + row.type, left.removeFromTop (20), juce::Justification::centredLeft, true);
                ig.setColour (choir::greyInk (C, C.ink, a)); ig.setFont (serif (14.5f));
                ig.drawText (row.help, left.removeFromTop (18), juce::Justification::centredLeft, true);
                juce::String d = "original default: " + row.def;
                if (row.readme.isNotEmpty()) d << "     README: " << row.readme;
                if (live) { const auto& p = engine().phrase; juce::ignoreUnused (p); d << "     Ghost Voice starts at: " << juce::StringArray { "-40", "500", "150", "10", "200" }[n - 3]; }
                ig.setColour (C.dim); ig.setFont (serif (13.5f)); ig.drawText (d, left.removeFromTop (18), juce::Justification::centredLeft, true);
                if (! live)
                {
                    ig.setColour (C.ink); ig.setFont (serif (14.5f, choir::italic));
                    ig.drawFittedText (row.given, r.reduced (12, 6).removeFromRight (318.0f).toNearestInt(), juce::Justification::centredRight, 2, 0.9f);
                }
            }
        });
    }
};

// ================================================================================================ Flayr Chop: its key / tempo read-outs and its AUTO-CHOP section
class Flayr : public Panel, private juce::Timer
{
public:
    explicit Flayr (Editor& e) : ed (e)
    {
        // Flayr Chop's own AUTO-CHOP picker (PluginEditor.cpp), every item under its own name. The ones Ghost Voice has are live
        const char* items[] { "Find samples", "Every beat", "Every 2 beats", "Every bar", "Even", "Slicer from playhead", "Random", "Key shift pad" };
        for (int i = 0; i < 8; ++i) mode.addItem (items[i], i + 1);
        for (int i : { 6, 7, 8 }) mode.setItemEnabled (i, false);
        mode.setTooltip ("Flayr Chop's AUTO-CHOP modes. Find samples, Every beat, Every 2 beats, Every bar and Even are Ghost Voice's HITS, EVERY BEAT, EVERY 2 BEATS, "
                         "EVERY BAR and EVEN. Slicer from playhead, Random and Key shift pad are Flayr Chop's player tools, which Ghost Voice doesn't have (its own RANDOM is on CHOP)");
        mode.onChange = [this] { const int id = mode.getSelectedId(); if (id >= 1 && id <= 5) { engine().autoMode = toOurs (id - 1); if (auto* cp = ed.chopPage()) cp->refreshControls(); } sync(); };
        for (int c : { 4, 8, 16, 32, 64 }) pads.addItem (juce::String (c) + " pads", c);
        pads.setTooltip ("How many chops (Flayr Chop's pad counts; the CHOP page's - N + can set any number from 1 to 64)");
        pads.onChange = [this] { if (pads.getSelectedId() > 0 && pads.getSelectedId() <= 64) engine().chopCount = pads.getSelectedId(); if (auto* cp = ed.chopPage()) cp->repaint(); sync(); };
        for (auto* c : { &mode, &pads }) { c->setWantsKeyboardFocus (false); addAndMakeVisible (*c); }
        for (auto* b : { &snapB, &chopB, &halfB, &doubleB, &tapB }) { styleButton (*b); addAndMakeVisible (*b); }
        snapB.setEnabled (false); halfB.setEnabled (false); doubleB.setEnabled (false); tapB.setEnabled (false);
        snapB.setTooltip ("SNAP: greyed. In Flayr Chop moved cue flags snap to the nearest transient; Ghost Voice's chops aren't dragged as cue flags");
        halfB.setTooltip ("1/2: greyed. Halves a tempo found double. Ghost Voice uses the tempo as found (the chops snap to the hits either way)");
        doubleB.setTooltip ("x2: greyed. Doubles a tempo found half. Ghost Voice uses the tempo as found");
        tapB.setTooltip ("TAP: greyed. Tap the tempo in. Ghost Voice uses the tempo as found");
        chopB.setTooltip ("CHOP: chops the clip with this mode and count (the same as AUTO CHOP on the CHOP page; Cmd+Z undoes)");
        chopB.onClick = [this] { pressButton ("CHOP"); };
        startTimerHz (10);
    }
    juce::String title() const override { return "FLAYR CHOP"; }
    juce::String subLine() const override { return "Flayr Chop's analysis finds the tempo, the key and the hits. Its own read-outs and AUTO-CHOP section, under its own names."; }
    juce::String countLine() const override
    {
        const auto d = engine().detected();
        return d.ready ? juce::String ((int) d.onsets.size()) + " hits found, " + juce::String (d.bpm, 1) + " BPM, " + d.keyName() + ". 5 of its 8 AUTO-CHOP modes are Ghost Voice's; the other 3 are greyed."
                       : juce::String ("Load a clip on the CHOP page: Flayr Chop's analysis listens to it at once.");
    }
    std::vector<std::pair<juce::String, juce::String>> sideRows() const override
    {
        const auto d = engine().detected();
        return { { "KEY", d.ready ? d.keyName() + (d.keyConfidence < 0.35f ? " ?" : "") : juce::String ("-") }, { "BPM", d.ready ? juce::String (d.bpm, 1) + (d.bpmConfidence < 0.35f ? " ?" : "") : juce::String ("-") },
                 { "HITS", juce::String ((int) d.onsets.size()) }, { "AUTO-CHOP", mode.getText().isNotEmpty() ? mode.getText() : chop::autoModeName (engine().autoMode) } };
    }
    bool pressButton (const juce::String& b) override
    {
        if (b != "CHOP") return false;
        auto& e = engine();
        const int id = mode.getSelectedId();
        if (! e.hasClip() || id < 1 || id > 5) return false;
        e.autoMode = toOurs (id - 1);
        const bool ok = e.autoChop();
        if (auto* cp = ed.chopPage()) { cp->refreshControls(); cp->showAll(); }
        sync(); return ok;
    }
    bool touch (int id, float v) override
    {
        if (id == idFlayrMode) { mode.setSelectedId (juce::roundToInt (v) + 1, juce::sendNotificationSync); return mode.getSelectedId() == juce::roundToInt (v) + 1; }
        if (id == idFlayrPads) { pads.setSelectedId (juce::roundToInt (v), juce::sendNotificationSync); return true; }
        return false;
    }
    bool hasControl (int id) const override { return id == idFlayrMode || id == idFlayrPads; }
    void sync() override
    {
        auto& e = engine();
        const bool clip = e.hasClip();
        const int ours = fromOurs (e.autoMode);
        mode.setSelectedId (ours >= 0 ? ours + 1 : 0, juce::dontSendNotification);
        const auto nothing = chop::autoModeName (e.autoMode) + " (Ghost Voice's own)";
        if (mode.getTextWhenNothingSelected() != nothing) mode.setTextWhenNothingSelected (nothing);
        if (pads.indexOfItemId (e.chopCount) < 0) { pads.clear (juce::dontSendNotification); for (int c : { 4, 8, 16, 32, 64 }) pads.addItem (juce::String (c) + " pads", c); if (e.chopCount != 4 && e.chopCount != 8 && e.chopCount != 16 && e.chopCount != 32 && e.chopCount != 64) pads.addItem (juce::String (e.chopCount) + " pads (set on CHOP)", e.chopCount); }
        pads.setSelectedId (e.chopCount, juce::dontSendNotification);
        mode.setEnabled (clip); pads.setEnabled (clip);
        chopB.setEnabled (clip && ours >= 0 && ! e.analysing());
        chopB.setTooltip (! clip ? "Load a clip on the CHOP page first" : ours < 0 ? "Pick one of Flayr Chop's modes first (" + chop::autoModeName (e.autoMode) + " is Ghost Voice's own; use AUTO CHOP on CHOP for it)"
                          : "CHOP: chops the clip with this mode and count (the same as AUTO CHOP on the CHOP page; Cmd+Z undoes)");
        const auto d = e.detected();
        juce::String k; k << (int) d.ready << d.bpm << d.key << (int) d.minor << (int) d.onsets.size() << (int) ed.cp().dark << (int) e.chops.size() << ours << (int) clip;
        if (k != key) { key = k; repaint(); }
    }
    void resized() override
    {
        auto r = getLocalBounds();
        auto topR = r.removeFromTop (juce::jmin (236, r.getHeight() / 2));
        r.removeFromTop (10);
        hitsR = r;
        found = topR.removeFromLeft (topR.getWidth() * 45 / 100); topR.removeFromLeft (10);
        chopR = topR;
        // Flayr Chop's read-outs: the key, then the tempo with 1/2, x2 and TAP beside it
        auto f = found.reduced (18, 0).withTrimmedTop (36);
        keyR = f.removeFromTop (78);
        bpmR = f.removeFromTop (78);
        auto bb = bpmR.withTrimmedTop (22).withHeight (32).withTrimmedLeft (150);
        halfB.setBounds (bb.removeFromLeft (48)); bb.removeFromLeft (6); doubleB.setBounds (bb.removeFromLeft (48)); bb.removeFromLeft (6); tapB.setBounds (bb.removeFromLeft (64));
        // its AUTO-CHOP section: the mode (3/5) and the pad count, then SNAP and CHOP
        auto c = chopR.reduced (18, 0).withTrimmedTop (36);
        auto c1 = c.removeFromTop (32);
        mode.setBounds (c1.removeFromLeft (c1.getWidth() * 3 / 5).reduced (2, 1)); pads.setBounds (c1.reduced (2, 1));
        plain1 = c.removeFromTop (44);
        auto c2 = c.removeFromTop (34);
        snapB.setBounds (c2.removeFromLeft (c2.getWidth() / 2).reduced (2, 1)); chopB.setBounds (c2.reduced (2, 1));
        plain2 = c.removeFromTop (30);
        mapR = c.reduced (2, 0).withTrimmedBottom (14);
    }
    void paint (juce::Graphics& g) override
    {   // (everything here is cached in one image until something it shows changes)
        juce::String k; k << (int) ed.cp().dark << getWidth() << "x" << getHeight() << "|" << key << "|" << mode.getSelectedId() << "|" << (int) engine().autoMode;
        panelArt.draw (g, k, getLocalBounds(), [this] (juce::Graphics& ig, juce::Rectangle<int>) { paintArt (ig); });
    }
    void paintArt (juce::Graphics& g)
    {
        const auto& C = ed.cp();
        auto& e = engine();
        const auto d = e.detected();
        for (auto rr : { found, chopR, hitsR }) choir::page (g, C, rr.toFloat());
        auto heading = [&] (juce::Rectangle<int> rr, const juce::String& t)
        { g.setColour (C.dark ? C.gold : C.edge.darker (0.2f)); g.setFont (display (15.0f)); g.drawText (t, rr.reduced (18, 0).withTrimmedTop (8).withHeight (22), juce::Justification::centredLeft, false); };
        heading (found, "WHAT IT FOUND");
        heading (chopR, "AUTO-CHOP");
        heading (hitsR, "THE HITS (Flayr Chop's transients; Find samples chops on the strongest)");
        auto readout = [&] (juce::Rectangle<int> rr, const juce::String& label, const juce::String& v, bool unsure, const juce::String& plain)
        {
            g.setColour (C.ink); g.setFont (serif (13.5f, choir::bold)); g.drawText (label, rr.removeFromTop (18), juce::Justification::centredLeft, false);
            auto vr = rr.removeFromTop (36).withWidth (140);
            g.setColour (C.card); g.fillRoundedRectangle (vr.toFloat(), 4.0f); g.setColour (C.edge); g.drawRoundedRectangle (vr.toFloat().reduced (0.5f), 4.0f, 1.0f);
            g.setColour (unsure ? C.warn : (C.dark ? C.gold : C.ink)); g.setFont (serif (19.0f, choir::bold)); g.drawText (v, vr, juce::Justification::centred, false);
            plainLine (g, C, plain, rr.withHeight (20), true, juce::Justification::centredLeft, 1);
        };
        const bool keyUnsure = d.ready && d.keyConfidence < 0.35f, bpmUnsure = d.ready && d.bpmConfidence < 0.35f;
        readout (keyR, "KEY", d.ready ? d.keyName() + (keyUnsure ? "?" : "") : juce::String ("-"), keyUnsure, "the clip's key (a ? when it is unsure)");
        readout (bpmR, "BPM", d.ready ? juce::String (d.bpm, 1) + (bpmUnsure ? "?" : "") : juce::String ("-"), bpmUnsure, "the clip's tempo: the bars and the beat chops follow it");
        plainLine (g, C, "1/2, x2, TAP: greyed. Flayr Chop corrects the tempo with them; Ghost Voice uses it as found.", bpmR.withTrimmedTop (78).withHeight (20), false, juce::Justification::centredLeft, 1);
        plainLine (g, C, "The mode: how it chops (Find samples = the strongest hits).  The pads: how many chops. Its last three modes are Flayr Chop's player tools, greyed.", plain1.reduced (2, 3), true, juce::Justification::topLeft, 2);
        plainLine (g, C, "SNAP: greyed (Ghost Voice's chops aren't dragged as cue flags).  CHOP: chops the clip now, like AUTO CHOP on CHOP.", plain2.reduced (2, 3), true, juce::Justification::topLeft, 2);
        {   // what each of its modes is in Ghost Voice (the one now in use lit)
            const char* fm[] { "Find samples", "Every beat", "Every 2 beats", "Every bar", "Even" };
            const auto cur = fromOurs (e.autoMode);
            auto m = mapR; const int colW = m.getWidth() / 2, lineH = juce::jmin (18, m.getHeight() / 3);
            for (int i = 0; i < 6; ++i)
            {
                auto cell = juce::Rectangle<int> (m.getX() + (i / 3) * colW, m.getY() + (i % 3) * lineH, colW - 6, lineH);
                const juce::String t = i < 5 ? juce::String (fm[i]) + "  =  " + chop::autoModeName (toOurs (i)) : juce::String ("the other 3: Flayr Chop's player tools");
                g.setColour (i == cur ? (C.dark ? C.gold : C.ink) : (i < 5 ? C.dim : choir::greyInk (C, C.dim, choir::greyA (C))));
                g.setFont (choir::fitted (i == cur ? choir::bold : choir::regular, t, (float) cell.getWidth(), 13.5f, 9.0f));
                g.drawText (t, cell, juce::Justification::centredLeft, false);
            }
        }
        // the hits on the clip's wave
        auto wr = hitsR.reduced (18, 0).withTrimmedTop (36).withTrimmedBottom (14).toFloat();
        g.setColour (C.waveBg); g.fillRoundedRectangle (wr, 4.0f); g.setColour (C.edgeDim); g.drawRoundedRectangle (wr, 4.0f, 1.0f);
        auto c = e.clip();
        if (c == nullptr || c->frames() < 2) { g.setColour (C.dim); g.setFont (serif (16.0f, choir::italic)); g.drawText ("Load a clip on the CHOP page: the hits show here.", wr, juce::Justification::centred, false); return; }
        juce::String pk; pk << (juce::pointer_sized_int) c.get() << "|" << (int) wr.getWidth() << "|" << (int) wr.getHeight() << "|" << (int) C.dark << "|" << (int) d.onsets.size() << "|" << d.bpm;
        waveArt.draw (g, pk, wr.toNearestInt(), [&, c, d] (juce::Graphics& ig, juce::Rectangle<int> b)
        {
            const float W = (float) b.getWidth(), mid = (float) b.getHeight() * 0.5f, h = (float) b.getHeight() * 0.42f;
            const double secs = c->seconds();
            if (d.ready && d.bpm > 0)
            {   // the beat grid
                ig.setColour (C.edgeDim.withMultipliedAlpha (0.5f));
                for (double t = d.firstBeat; t < secs; t += 60.0 / d.bpm) ig.fillRect ((float) (t / secs) * W, 0.0f, 1.0f, (float) b.getHeight());
            }
            ig.setColour (C.waveInk.withAlpha (0.8f));
            const auto n = c->frames();
            for (int i = 0; i < (int) W; ++i)
            {
                const auto a0 = n * i / (juce::int64) W, a1 = juce::jmax (a0 + 1, n * (i + 1) / (juce::int64) W);
                float m = 0; const float* s = c->audio.getReadPointer (0);
                for (auto f = a0; f < a1; f += juce::jmax ((juce::int64) 1, (a1 - a0) / 48)) m = juce::jmax (m, std::abs (s[f]));
                ig.fillRect ((float) i, mid - m * h, 1.0f, juce::jmax (1.0f, 2.0f * m * h));
            }
            for (size_t i = 0; i < d.onsets.size(); ++i)
            {
                const float st = i < d.strength.size() ? juce::jlimit (0.15f, 1.0f, d.strength[i]) : 0.6f;
                ig.setColour (C.play.withAlpha (0.35f + 0.6f * st));
                ig.fillRect ((float) (d.onsets[i] / secs) * W - 0.5f, (float) b.getHeight() * (1.0f - st) * 0.5f, 1.5f, (float) b.getHeight() * st);
            }
        });
    }
private:
    Editor& ed;
    juce::ComboBox mode, pads;
    juce::TextButton snapB { "SNAP" }, chopB { "CHOP" }, halfB { "1/2" }, doubleB { "x2" }, tapB { "TAP" };
    juce::Rectangle<int> found, chopR, hitsR, keyR, bpmR, plain1, plain2, mapR;
    juce::String key;
    choir::Cache waveArt, panelArt;
    chop::ChopEngine& engine() const { return *ed.proc.chopper; }
    void timerCallback() override { if (onScreen (*this)) sync(); }
    static chop::AutoMode toOurs (int i)
    {
        switch (i) { case 0: return chop::AutoMode::hits; case 1: return chop::AutoMode::beats; case 2: return chop::AutoMode::twoBeats; case 3: return chop::AutoMode::bar; default: return chop::AutoMode::even; }
    }
    static int fromOurs (chop::AutoMode m)
    {
        switch (m) { case chop::AutoMode::hits: return 0; case chop::AutoMode::beats: return 1; case chop::AutoMode::twoBeats: return 2; case chop::AutoMode::bar: return 3; case chop::AutoMode::even: return 4; default: return -1; }
    }
};

std::unique_ptr<Panel> makeIntersectSound (Editor& e) { return std::make_unique<IntersectSound> (e); }
std::unique_ptr<Panel> makeIntersectAutoChop (Editor& e) { return std::make_unique<IntersectAutoChop> (e); }
std::unique_ptr<Panel> makePhrases (Editor& e) { return std::make_unique<Phrases> (e); }
std::unique_ptr<Panel> makeFlayr (Editor& e) { return std::make_unique<Flayr> (e); }
}
