// SPIRIT EQ editor: interactive curve over a live pre/post analyser, per-band controls, dynamics display,
// EQ sketch, A/B and undo/redo.
#include "FxEqInternal.h"
#include "FxTheme.h"

namespace gpfx {
namespace eq {
namespace {

namespace col = fxtheme::col;
using fxtheme::T;
using ghosty::font;

juce::Colour bandColour (int k)
{
    static const juce::uint32 c[kBands] { 0xff3CFF8F, 0xff45E6FF, 0xffA06BFF, 0xffFF4FD8, 0xffFFC35A, 0xffFF9E5A,
                                          0xffFF5A6E, 0xffE6B8FF, 0xff5AFFD6, 0xffB6FF3C, 0xff7FD8FF, 0xffFFB58A };
    return T (juce::Colour (c[(size_t) juce::jlimit (0, kBands - 1, k)]));
}
const char* placementTag (int p) { static const char* t[] { "", "L", "R", "M", "S" }; return t[juce::jlimit (0, 4, p)]; }

constexpr double kFMin = 10.0, kFMax = 30000.0;
constexpr int kPts = 360;   // curve resolution

// ---------------------------------------------------------------------------- parameter access with gestures
struct Params
{
    juce::AudioProcessorValueTreeState& s;
    explicit Params (juce::AudioProcessorValueTreeState& st) : s (st) {}
    juce::RangedAudioParameter* get (const juce::String& id) const { return s.getParameter (id); }
    float plain (const juce::String& id) const { if (auto* p = get (id)) return p->convertFrom0to1 (p->getValue()); return 0; }
    void set (const juce::String& id, float v, bool gesture = true) const
    {
        if (auto* p = get (id))
        {
            if (gesture) p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (v));
            if (gesture) p->endChangeGesture();
        }
    }
    void begin (const juce::String& id) const { if (auto* p = get (id)) p->beginChangeGesture(); }
    void end (const juce::String& id) const { if (auto* p = get (id)) p->endChangeGesture(); }
    void setRaw (const juce::String& id, float v) const { if (auto* p = get (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); }
};

// ---------------------------------------------------------------------------- analyser (runs on the message thread)
struct Analyser
{
    static constexpr int kOrder = 12, kN = 1 << kOrder, kDisp = 256;
    juce::dsp::FFT fft { kOrder };
    std::vector<float> ringPre = std::vector<float> (kN, 0.0f), ringPost = std::vector<float> (kN, 0.0f);
    int wPre = 0, wPost = 0, newPre = 0, newPost = 0;
    std::vector<float> work = std::vector<float> (2 * kN, 0.0f), win = std::vector<float> (kN, 0.0f), tmp = std::vector<float> (8192, 0.0f);
    std::array<float, kDisp> pre {}, post {}, peak {}; std::array<int, kDisp> hold {};
    float norm = 1;
    Analyser()
    {
        double sum = 0;
        for (int i = 0; i < kN; ++i) { win[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * kPi * (float) i / kN); sum += win[(size_t) i]; }
        norm = (float) (2.0 / sum);
        pre.fill (-120); post.fill (-120); peak.fill (-120); hold.fill (0);
    }
    void pull (AnaFifo& f, std::vector<float>& ring, int& w, int& fresh)
    {
        for (;;)
        {
            const int got = f.pull (tmp.data(), (int) tmp.size());
            if (got <= 0) break;
            for (int i = 0; i < got; ++i) { ring[(size_t) w] = tmp[(size_t) i]; w = (w + 1) % kN; }
            fresh += got;
        }
    }
    void spectrum (const std::vector<float>& ring, int w, double sr, std::array<float, kDisp>& out)
    {
        for (int i = 0; i < kN; ++i) work[(size_t) i] = ring[(size_t) ((w + i) % kN)] * win[(size_t) i];
        std::fill (work.begin() + kN, work.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (work.data(), true);
        const double lr = std::log (20000.0 / 20.0);
        for (int d = 0; d < kDisp; ++d)
        {
            const double f0 = 20.0 * std::exp (lr * d / kDisp), f1 = 20.0 * std::exp (lr * (d + 1) / kDisp);
            const double b0 = f0 * kN / sr, b1 = f1 * kN / sr;
            float m = 0;
            if (b1 - b0 < 1.0)
            {
                const double b = 0.5 * (b0 + b1); const int i = (int) b; const float fr = (float) (b - i);
                if (i + 1 < kN / 2) m = work[(size_t) i] + fr * (work[(size_t) (i + 1)] - work[(size_t) i]);
            }
            else for (int i = (int) b0; i <= (int) b1 && i < kN / 2; ++i) m = std::max (m, work[(size_t) i]);
            const double fc = std::sqrt (f0 * f1);
            float db = gainToDb (m * norm) + (float) (4.5 * std::log2 (fc / 1000.0));   // 4.5 dB/oct display tilt
            float& o = out[(size_t) d];
            o = db > o ? o + 0.6f * (db - o) : o + 0.14f * (db - o);
        }
    }
    void update (EqUnit& u, bool wantPre, bool wantPost, bool peakHold)
    {
        pull (u.preFifo, ringPre, wPre, newPre); pull (u.postFifo, ringPost, wPost, newPost);
        const double sr = u.sampleRate.load();
        if (wantPre && newPre > 0) { spectrum (ringPre, wPre, sr, pre); newPre = 0; }
        else if (! wantPre) pre.fill (-120);
        if (newPost > 0) { spectrum (ringPost, wPost, sr, post); newPost = 0; }
        else for (auto& v : post) v = std::max (-120.0f, v - 1.0f);
        if (! wantPost) post.fill (-120);
        for (int d = 0; d < kDisp; ++d)
        {
            const float v = wantPost ? post[(size_t) d] : pre[(size_t) d];
            if (! peakHold) { peak[(size_t) d] = -120; continue; }
            if (v >= peak[(size_t) d]) { peak[(size_t) d] = v; hold[(size_t) d] = 45; }
            else if (hold[(size_t) d] > 0) --hold[(size_t) d];
            else peak[(size_t) d] -= 0.35f;
        }
    }
};

class EqPage;

// ---------------------------------------------------------------------------- the curve
class CurveView : public juce::Component
{
public:
    explicit CurveView (EqPage& p) : page (p) { setRepaintsOnMouseActivity (false); }
    void paint (juce::Graphics& g) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; hoverPos = { -1, -1 }; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;

    // geometry
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (2.0f).withTrimmedBottom (14.0f).withTrimmedRight (26.0f); }
    float range() const;
    float freqToX (double f) const { auto p = plot(); return p.getX() + (float) (std::log (f / kFMin) / std::log (kFMax / kFMin)) * p.getWidth(); }
    double xToFreq (float x) const { auto p = plot(); return kFMin * std::exp ((double) ((x - p.getX()) / p.getWidth()) * std::log (kFMax / kFMin)); }
    float dbToY (float db) const { auto p = plot(); return p.getCentreY() - db / range() * (p.getHeight() * 0.5f - 6.0f); }
    float yToDb (float y) const { auto p = plot(); return (p.getCentreY() - y) / (p.getHeight() * 0.5f - 6.0f) * range(); }
    float anaToY (float db) const { auto p = plot(); return p.getY() + (-db / 96.0f) * p.getHeight() + 6.0f; }

    juce::Point<float> nodePos (int k) const;
    int hitNode (juce::Point<float> p) const;
    void computeCurves();

    std::array<std::array<float, kPts>, kBands> bandDb {};    // per band response (with live dynamics)
    std::array<std::array<float, kPts>, kBands> staticDb {};  // per band static response
    std::array<float, kPts> totalDb {};
    std::array<bool, kBands> active {}, shown {};
    std::array<BandSpec, kBands> lastSpec {}; std::array<double, kBands> lastFrac {};
    int hover = -1; juce::Point<float> hoverPos { -1, -1 };
    int drag = -1; juce::Point<float> dragStart; float startF = 0, startG = 0, startQ = 1; bool dragQ = false;
    // sketch
    std::array<float, kPts> sketch {}; std::array<bool, kPts> sketched {}; bool sketching = false; juce::Point<float> lastSketch;

private:
    EqPage& page;
    void showBandMenu (int k);
    void showEmptyMenu (juce::Point<float> p);
    int addBand (double f, float gain);
};

// ---------------------------------------------------------------------------- the selected band's controls
struct BandControls : public juce::Component
{
    std::unique_ptr<ui::Toggle> on, byp, dyn, autoT, spec; ui::Toggle solo { "SOLO", col::gold };
    std::unique_ptr<ui::Choice> type, slope, stereo, detect;
    std::unique_ptr<ui::Knob> freq, gain, q, thr, range, ratio, att, rel;
    int band = 0; fxtheme::Held c;
    BandControls (juce::AudioProcessorValueTreeState& s, int k, EqUnit& u) : band (k), c (bandColour (k))
    {
        on = std::make_unique<ui::Toggle> (s, bid (k, "on"), "ON", c);
        byp = std::make_unique<ui::Toggle> (s, bid (k, "byp"), "BYP", col::dim);
        dyn = std::make_unique<ui::Toggle> (s, bid (k, "dyn"), "DYNAMIC", c);
        autoT = std::make_unique<ui::Toggle> (s, bid (k, "auto"), "AUTO", c);
        spec = std::make_unique<ui::Toggle> (s, bid (k, "spec"), "SPECTRAL", col::pink);
        type = std::make_unique<ui::Choice> (s, bid (k, "type"), c);
        slope = std::make_unique<ui::Choice> (s, bid (k, "slope"), c);
        stereo = std::make_unique<ui::Choice> (s, bid (k, "stereo"), c);
        detect = std::make_unique<ui::Choice> (s, bid (k, "detect"), c);
        freq = std::make_unique<ui::Knob> (s, bid (k, "freq"), "FREQ", c);
        gain = std::make_unique<ui::Knob> (s, bid (k, "gain"), "GAIN", c);
        q = std::make_unique<ui::Knob> (s, bid (k, "q"), "Q", c);
        thr = std::make_unique<ui::Knob> (s, bid (k, "thr"), "THRESH", c);
        range = std::make_unique<ui::Knob> (s, bid (k, "range"), "RANGE", c);
        ratio = std::make_unique<ui::Knob> (s, bid (k, "ratio"), "RATIO", c);
        att = std::make_unique<ui::Knob> (s, bid (k, "att"), "ATTACK", c);
        rel = std::make_unique<ui::Knob> (s, bid (k, "rel"), "RELEASE", c);
        solo.setToggleState (u.soloBand.load() == k, juce::dontSendNotification);
        solo.onClick = [this, &u] { u.soloBand.store (solo.getToggleState() ? band : -1); };
        for (juce::Component* x : std::initializer_list<juce::Component*> { on.get(), byp.get(), dyn.get(), autoT.get(), spec.get(), &solo, type.get(), slope.get(),
                                     stereo.get(), detect.get(), freq.get(), gain.get(), q.get(), thr.get(), range.get(), ratio.get(), att.get(), rel.get() })
            addAndMakeVisible (x);
        type->setTooltip ("Band shape"); slope->setTooltip ("Slope (cuts / band pass)"); stereo->setTooltip ("Stereo placement");
        detect->setTooltip ("Detection: band-filtered or wide");
    }
    void refresh (EqUnit& u)
    {
        const bool d = u.bp[(size_t) band].dyn.b();
        for (juce::Component* x : std::initializer_list<juce::Component*> { thr.get(), range.get(), ratio.get(), att.get(), rel.get(), autoT.get(), spec.get(), detect.get() })
            x->setAlpha (d ? 1.0f : fxtheme::kDisabledAlpha);
        const int t = u.bp[(size_t) band].type.i();
        slope->setAlpha (usesSlope (t) ? 1.0f : fxtheme::kDisabledAlpha);
        gain->setAlpha (isGainType (t) ? 1.0f : fxtheme::kDisabledAlpha);
        solo.setToggleState (u.soloBand.load() == band, juce::dontSendNotification);
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (c.withAlpha (0.05f)); g.fillRoundedRectangle (r, 9);
        g.setColour (c.withAlpha (0.25f)); g.drawRoundedRectangle (r.reduced (0.5f), 9, 1.0f);
        if (! dynArea.isEmpty())
        {
            g.setColour (c.withAlpha (0.12f)); g.drawVerticalLine (dynArea.getX() - 6, r.getY() + 8, r.getBottom() - 8);
        }
    }
    juce::Rectangle<int> dynArea;
    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 6);
        auto col1 = r.removeFromLeft (112);
        {
            auto a = col1.removeFromTop (24); on->setBounds (a.removeFromLeft (54)); a.removeFromLeft (4); byp->setBounds (a);
            col1.removeFromTop (6); solo.setBounds (col1.removeFromTop (24));
            col1.removeFromTop (6); stereo->setBounds (col1.removeFromTop (24));
        }
        r.removeFromLeft (8);
        auto col2 = r.removeFromLeft (112);
        type->setBounds (col2.removeFromTop (26)); col2.removeFromTop (8); slope->setBounds (col2.removeFromTop (26));
        r.removeFromLeft (6);
        const int kw = juce::jmin (66, (r.getWidth() - 120) / 8);
        freq->setBounds (r.removeFromLeft (kw)); gain->setBounds (r.removeFromLeft (kw)); q->setBounds (r.removeFromLeft (kw));
        r.removeFromLeft (14);
        dynArea = r;
        auto col3 = r.removeFromLeft (104);
        dyn->setBounds (col3.removeFromTop (24)); col3.removeFromTop (4);
        { auto a = col3.removeFromTop (24); autoT->setBounds (a.removeFromLeft (46)); a.removeFromLeft (4); detect->setBounds (a); }
        col3.removeFromTop (4); spec->setBounds (col3.removeFromTop (24));
        r.removeFromLeft (6);
        for (auto* k : { thr.get(), range.get(), ratio.get(), att.get(), rel.get() }) k->setBounds (r.removeFromLeft (kw));
    }
};

// ---------------------------------------------------------------------------- band selector chips
struct Chips : public juce::Component
{
    std::function<void (int)> onPick; std::function<bool (int)> isOn; int sel = 0;
    void paint (juce::Graphics& g) override
    {
        for (int k = 0; k < kBands; ++k)
        {
            auto r = cell (k).reduced (3);
            const bool on = isOn && isOn (k), s = k == sel;
            const auto c = bandColour (k);
            if (s) { g.setColour (c.withAlpha (0.25f)); g.fillEllipse (r.expanded (3)); }
            g.setColour (on ? c : c.withAlpha (0.14f)); g.fillEllipse (r);
            g.setColour (s ? col::white : c.withAlpha (0.5f)); g.drawEllipse (r, s ? 1.6f : 1.0f);
            g.setColour (on ? col::bg : col::white.withAlpha (0.5f)); g.setFont (font (10.0f));
            g.drawText (juce::String (k + 1), r, juce::Justification::centred, false);
        }
    }
    juce::Rectangle<float> cell (int k) const
    {
        const float w = (float) getWidth() / 6.0f, h = (float) getHeight() / 2.0f;
        const float d = juce::jmin (w, h);
        return { (float) (k % 6) * w + (w - d) * 0.5f, (float) (k / 6) * h + (h - d) * 0.5f, d, d };
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int k = 0; k < kBands; ++k) if (cell (k).expanded (2).contains (e.position)) { if (onPick) onPick (k); return; }
    }
};

// ---------------------------------------------------------------------------- the page
class EqPage : public ui::Page, private juce::Timer
{
public:
    EqPage (juce::AudioProcessorValueTreeState& s, EqUnit& u)
        : apvts (s), unit (u), params (s), curve (*this),
          modeC (s, "fx_eq_mode", ui::accentOf (uEq)), charSeg (s, "fx_eq_char", ui::accentOf (uEq)),
          autoGainT (s, "fx_eq_autogain", "AUTO GAIN", col::slime), invertT (s, "fx_eq_invert", "INVERT", col::pink),
          scaleK (s, "fx_eq_scale", "SCALE", ui::accentOf (uEq)), outK (s, "fx_eq_out", "OUTPUT", ui::accentOf (uEq))
    {
        addAndMakeVisible (curve);
        for (juce::Component* c : std::initializer_list<juce::Component*> { &modeC, &charSeg, &anaC, &peakT, &sketchT, &aB, &bB, &copyB, &undoB, &redoB,
                                     &autoGainT, &invertT, &chips, &scaleK, &outK })
            addAndMakeVisible (c);
        anaC.addItemList ({ "Analyser Off", "Analyser Pre", "Analyser Post", "Pre + Post" }, 1);
        anaC.setColour (juce::ComboBox::outlineColourId, ui::accentOf (uEq)); anaC.setColour (juce::ComboBox::arrowColourId, ui::accentOf (uEq));
        {
            const juce::ScopedLock sl (unit.uiLock);
            anaC.setSelectedItemIndex (unit.anaMode, juce::dontSendNotification);
            peakT.setToggleState (unit.peakHold, juce::dontSendNotification);
        }
        anaC.onChange = [this] { const juce::ScopedLock sl (unit.uiLock); unit.anaMode = anaC.getSelectedItemIndex(); };
        peakT.onClick = [this] { const juce::ScopedLock sl (unit.uiLock); unit.peakHold = peakT.getToggleState(); };
        sketchT.onClick = [this] { curve.sketched.fill (false); curve.repaint(); };
        aB.setClickingTogglesState (false); bB.setClickingTogglesState (false);
        aB.setColour (juce::TextButton::buttonOnColourId, ui::accentOf (uEq)); bB.setColour (juce::TextButton::buttonOnColourId, ui::accentOf (uEq));
        aB.onClick = [this] { switchAB (0); }; bB.onClick = [this] { switchAB (1); };
        copyB.onClick = [this] { copyToOther(); };
        undoB.onClick = [this] { stepHistory (-1); }; redoB.onClick = [this] { stepHistory (1); };
        sketchT.setTooltip ("Draw a curve freehand; bands are fitted to it");
        chips.isOn = [this] (int k) { return unit.bp[(size_t) k].on.b(); };
        chips.onPick = [this] (int k) { select (k); };
        select (firstActiveOr (0));
        updateAB();
        startTimerHz (30);
    }
    ~EqPage() override { unit.uiStamp.store (0); }

    void select (int k)
    {
        k = juce::jlimit (0, kBands - 1, k);
        if (controls && controls->band == k) return;
        selected = k; chips.sel = k; chips.repaint();
        if (unit.soloBand.load() >= 0) unit.soloBand.store (k);
        controls = std::make_unique<BandControls> (apvts, k, unit);
        addAndMakeVisible (*controls);
        resized(); curve.repaint();
    }
    int firstActiveOr (int d) const { for (int k = 0; k < kBands; ++k) if (unit.bp[(size_t) k].on.b()) return k; return d; }

    void paint (juce::Graphics& g) override
    {
        // auto-gain / latency readout in the toolbar
        g.setColour (col::dim); g.setFont (font (10.0f));
        juce::String s;
        const int lat = unit.latency();
        s << (lat > 0 ? "LATENCY " + juce::String (lat) + " smp" : "ZERO LATENCY");
        if (unit.autoGain.b()) s << "   AUTO " << juce::String (unit.autoGainDb.load(), 1) << " dB";
        g.drawText (s, readout, juce::Justification::centredRight, false);
    }
    void resized() override
    {
        auto r = getLocalBounds();
        auto tb = r.removeFromTop (28);
        modeC.setBounds (tb.removeFromLeft (128)); tb.removeFromLeft (6);
        charSeg.setBounds (tb.removeFromLeft (210)); tb.removeFromLeft (6);
        anaC.setBounds (tb.removeFromLeft (118)); tb.removeFromLeft (4);
        peakT.setBounds (tb.removeFromLeft (50)); tb.removeFromLeft (4);
        sketchT.setBounds (tb.removeFromLeft (62)); tb.removeFromLeft (10);
        aB.setBounds (tb.removeFromLeft (28)); bB.setBounds (tb.removeFromLeft (28)); tb.removeFromLeft (2);
        copyB.setBounds (tb.removeFromLeft (48)); tb.removeFromLeft (8);
        undoB.setBounds (tb.removeFromLeft (46)); redoB.setBounds (tb.removeFromLeft (46)); tb.removeFromLeft (8);
        autoGainT.setBounds (tb.removeFromLeft (78)); tb.removeFromLeft (4);
        invertT.setBounds (tb.removeFromLeft (58)); tb.removeFromLeft (6);
        readout = tb;
        r.removeFromTop (6);
        auto strip = r.removeFromBottom (104); r.removeFromBottom (6);
        curve.setBounds (r);
        auto ch = strip.removeFromLeft (132).reduced (0, 14);
        chips.setBounds (ch);
        strip.removeFromLeft (6);
        auto glob = strip.removeFromRight (136);
        scaleK.setBounds (glob.removeFromLeft (68).withTrimmedTop (6)); outK.setBounds (glob.withTrimmedTop (6));
        strip.removeFromRight (6);
        if (controls) controls->setBounds (strip);
    }

    void tick (double) override {}

    // ---------------------------------------------------------------- A/B and undo
    juce::Array<float> snapshot() const
    {
        juce::Array<float> v;
        for (auto& id : ids) if (auto* p = params.get (id)) v.add (p->getValue()); else v.add (0);
        return v;
    }
    void apply (const juce::Array<float>& v)
    {
        applying = true;
        for (int i = 0; i < ids.size() && i < v.size(); ++i)
            if (auto* p = params.get (ids[i]))
                if (std::abs (p->getValue() - v[i]) > 1e-6f) { p->beginChangeGesture(); p->setValueNotifyingHost (v[i]); p->endChangeGesture(); }
        applying = false;
    }
    juce::ValueTree toTree (const juce::Array<float>& v, const juce::String& name) const
    {
        juce::ValueTree t (name);
        for (int i = 0; i < ids.size() && i < v.size(); ++i) t.setProperty (ids[i], v[i], nullptr);
        return t;
    }
    juce::Array<float> fromTree (const juce::ValueTree& t) const
    {
        auto v = snapshot();
        for (int i = 0; i < ids.size(); ++i) if (t.hasProperty (ids[i])) v.set (i, (float) t.getProperty (ids[i]));
        return v;
    }
    void switchAB (int slot)
    {
        const juce::ScopedLock sl (unit.uiLock);
        if (slot == unit.abActive) return;
        unit.abSlot[unit.abActive] = toTree (snapshot(), unit.abActive == 0 ? "A" : "B");
        auto& t = unit.abSlot[slot];
        if (t.getNumProperties() > 0) apply (fromTree (t));
        unit.abActive = slot;
        updateAB();
    }
    void copyToOther()
    {
        const juce::ScopedLock sl (unit.uiLock);
        const int other = 1 - unit.abActive;
        unit.abSlot[other] = toTree (snapshot(), other == 0 ? "A" : "B");
    }
    void updateAB()
    {
        aB.setToggleState (unit.abActive == 0, juce::dontSendNotification);
        bB.setToggleState (unit.abActive == 1, juce::dontSendNotification);
        copyB.setButtonText (unit.abActive == 0 ? "A>B" : "B>A");
    }
    void stepHistory (int d)
    {
        const int ni = histIdx + d;
        if (ni < 0 || ni >= history.size()) return;
        histIdx = ni; apply (history.getReference (ni)); lastSeen = history.getReference (ni);
    }
    void trackHistory()
    {
        if (juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown()) return;
        auto now = snapshot();
        if (history.isEmpty()) { history.add (now); histIdx = 0; lastSeen = now; return; }
        if (now == lastSeen) return;
        lastSeen = now;
        while (history.size() > histIdx + 1) history.removeLast();
        history.add (now);
        if (history.size() > 64) history.remove (0);
        histIdx = history.size() - 1;
    }

    // ---------------------------------------------------------------- sketch fit
    void fitSketch();

    juce::AudioProcessorValueTreeState& apvts;
    EqUnit& unit;
    Params params;
    CurveView curve;
    Analyser analyser;
    int selected = 0;
    std::unique_ptr<BandControls> controls;
    ui::Choice modeC; ui::Segmented charSeg;
    juce::ComboBox anaC; ui::Toggle peakT { "PEAK", col::gold }, sketchT { "SKETCH", col::white };
    juce::TextButton aB { "A" }, bB { "B" }, copyB { "A>B" }, undoB { "UNDO" }, redoB { "REDO" };
    ui::Toggle autoGainT, invertT;
    Chips chips;
    ui::Knob scaleK, outK;
    juce::Rectangle<int> readout;
    juce::StringArray ids = allParamIds();
    juce::Array<juce::Array<float>> history; int histIdx = -1; juce::Array<float> lastSeen; bool applying = false;
    int frame = 0;

private:
    void timerCallback() override
    {
        if (! isVisible()) return;
        unit.uiStamp.store (juce::Time::getMillisecondCounter());
        if (getProperties().contains ("select")) { select ((int) getProperties()["select"]); getProperties().remove ("select"); }   // test hooks
        if (getProperties().contains ("testHover")) curve.hover = (int) getProperties()["testHover"];
        int ana = 3; bool ph = true;
        { const juce::ScopedLock sl (unit.uiLock); ana = unit.anaMode; ph = unit.peakHold; }
        analyser.update (unit, ana == 1 || ana == 3, ana == 2 || ana == 3, ph);
        curve.computeCurves();
        curve.repaint();
        if (controls) controls->refresh (unit);
        chips.repaint();
        if ((++frame % 8) == 0) { trackHistory(); repaint (readout); }
        undoB.setEnabled (histIdx > 0); redoB.setEnabled (histIdx >= 0 && histIdx < history.size() - 1);
    }
};

// ============================================================================ CurveView implementation
float CurveView::range() const { static const float r[] { 6, 12, 24, 36 }; const juce::ScopedLock sl (page.unit.uiLock); return r[juce::jlimit (0, 3, page.unit.dispRange)]; }

void CurveView::computeCurves()
{
    auto& u = page.unit;
    const double sr = u.sampleRate.load();
    const double nyq = sr * 0.5 * 0.999;
    totalDb.fill (0.0f);
    for (int k = 0; k < kBands; ++k)
    {
        active[(size_t) k] = u.bandActive (k);
        shown[(size_t) k] = u.bp[(size_t) k].on.b();
        if (! shown[(size_t) k]) continue;
        const auto& p = u.bp[(size_t) k];
        BandSpec s = u.specOf (k, true);
        const bool dynCut = p.dyn.b() && ! p.spec.b() && ! isGainType (s.type);
        const double frac = dynCut ? (double) u.dynFrac[(size_t) k].load() : 1.0;
        const auto& ls = lastSpec[(size_t) k];
        if (ls.type != s.type || ls.slope != s.slope || ls.freq != s.freq || ls.gain != s.gain || ls.q != s.q || lastFrac[(size_t) k] != frac)
        {
            lastSpec[(size_t) k] = s; lastFrac[(size_t) k] = frac;
            Sections sec; designBand (s, sr, sec);
            Sections st; BandSpec ss = u.specOf (k, false); designBand (ss, sr, st);
            for (int i = 0; i < kPts; ++i)
            {
                const double f = std::min (nyq, kFMin * std::pow (kFMax / kFMin, (double) i / (kPts - 1)));
                bandDb[(size_t) k][(size_t) i] = (float) (frac * 20.0 * std::log10 (std::max (1e-9, sec.mag (f, sr))));
                staticDb[(size_t) k][(size_t) i] = (float) (20.0 * std::log10 (std::max (1e-9, st.mag (f, sr))));
            }
        }
        if (active[(size_t) k]) for (int i = 0; i < kPts; ++i) totalDb[(size_t) i] += bandDb[(size_t) k][(size_t) i];
    }
}

juce::Point<float> CurveView::nodePos (int k) const
{
    auto& p = page.unit.bp[(size_t) k];
    const int t = p.type.i();
    const double f = juce::jlimit (kFMin, kFMax, (double) p.freq());
    float db = 0;
    if (isGainType (t)) db = p.gain() * page.unit.scale();
    else if (t == btLowCut || t == btHighCut)
    {
        const int i = juce::jlimit (0, kPts - 1, (int) std::lround (std::log (f / kFMin) / std::log (kFMax / kFMin) * (kPts - 1)));
        db = staticDb[(size_t) k][(size_t) i];
    }
    if (t == btFlatTilt) db = 0;
    const float r = range();
    return { freqToX (f), dbToY (juce::jlimit (-r, r, db)) };
}

int CurveView::hitNode (juce::Point<float> p) const
{
    int best = -1; float bd = 13.0f;
    for (int k = 0; k < kBands; ++k)
    {
        if (! shown[(size_t) k]) continue;
        const float d = nodePos (k).getDistanceFrom (p);
        if (d < bd) { bd = d; best = k; }
    }
    return best;
}

void CurveView::paint (juce::Graphics& g)
{
    auto& u = page.unit;
    const auto acc = ui::accentOf (uEq);
    ui::drawScreen (g, getLocalBounds().toFloat(), acc);
    auto P = plot();
    const float r = range();

    // grid
    {
        const double lines[] { 20, 30, 40, 50, 60, 70, 80, 90, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000, 20000 };
        for (double f : lines)
        {
            const bool major = f == 100 || f == 1000 || f == 10000;
            g.setColour (T (juce::Colours::white).withAlpha (major ? 0.10f : 0.035f));
            g.drawVerticalLine ((int) freqToX (f), P.getY(), P.getBottom());
        }
        g.setFont (font (9.5f, false));
        for (double f : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
        {
            g.setColour (col::dim.withAlpha (0.8f));
            const juce::String s = f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f);
            g.drawText (s, juce::Rectangle<float> (freqToX (f) - 20, P.getBottom() + 1, 40, 12), juce::Justification::centred, false);
        }
        const float step = r <= 6 ? 2.0f : (r <= 12 ? 3.0f : (r <= 24 ? 6.0f : 12.0f));
        for (float d = -r; d <= r + 0.01f; d += step)
        {
            const float y = dbToY (d);
            g.setColour (T (juce::Colours::white).withAlpha (std::abs (d) < 0.01f ? 0.16f : 0.05f));
            g.drawHorizontalLine ((int) y, P.getX(), P.getRight());
            g.setColour (std::abs (d) < 0.01f ? col::white.withAlpha (0.7f) : col::dim.withAlpha (0.8f));
            g.drawText ((d > 0 ? "+" : "") + juce::String ((int) d), juce::Rectangle<float> (P.getRight() + 2, y - 6, 24, 12), juce::Justification::centredLeft, false);
        }
    }

    // analyser
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (P.toNearestInt());
        const auto& A = page.analyser;
        auto path = [&] (const std::array<float, Analyser::kDisp>& v, bool closed)
        {
            juce::Path p; const double lr = std::log (20000.0 / 20.0);
            for (int d = 0; d < Analyser::kDisp; ++d)
            {
                const double f = 20.0 * std::exp (lr * (d + 0.5) / Analyser::kDisp);
                const float x = freqToX (f), y = juce::jlimit (P.getY(), P.getBottom() + 2, anaToY (v[(size_t) d]));
                if (d == 0) { if (closed) { p.startNewSubPath (x, P.getBottom() + 2); p.lineTo (x, y); } else p.startNewSubPath (x, y); }
                else p.lineTo (x, y);
            }
            if (closed) { p.lineTo (freqToX (20000.0), P.getBottom() + 2); p.closeSubPath(); }
            return p;
        };
        int ana; bool ph; { const juce::ScopedLock sl (u.uiLock); ana = u.anaMode; ph = u.peakHold; }
        if (ana == 1 || ana == 3)
        {
            auto p = path (A.pre, true);
            g.setColour (col::white.withAlpha (ana == 3 ? 0.035f : 0.07f)); g.fillPath (p);
            g.setColour (col::white.withAlpha (ana == 3 ? 0.16f : 0.3f)); g.strokePath (path (A.pre, false), juce::PathStrokeType (1.0f));
        }
        if (ana == 2 || ana == 3)
        {
            auto p = path (A.post, true);
            juce::ColourGradient gr (col::ecto.withAlpha (0.32f), 0, P.getY() + P.getHeight() * 0.3f, col::ice.withAlpha (0.03f), 0, P.getBottom(), false);
            g.setGradientFill (gr); g.fillPath (p);
            g.setColour (col::ice.withAlpha (0.45f)); g.strokePath (path (A.post, false), juce::PathStrokeType (1.1f));
        }
        if (ana > 0 && ph)
        {
            g.setColour (col::gold.withAlpha (0.35f)); g.strokePath (path (A.peak, false), juce::PathStrokeType (0.8f));
        }
        // spectral dynamics activity
        bool anySpec = false;
        for (auto& a : u.specDisp) if (std::abs (a.load()) > 0.05f) { anySpec = true; break; }
        if (anySpec)
        {
            juce::Path sp; const double lr = std::log (20000.0 / 20.0);
            sp.startNewSubPath (freqToX (20.0), dbToY (0));
            for (int i = 0; i < Spectral::kDisp; ++i)
            {
                const double f = 20.0 * std::exp (lr * (i + 0.5) / Spectral::kDisp);
                sp.lineTo (freqToX (f), dbToY (juce::jlimit (-r, r, u.specDisp[(size_t) i].load())));
            }
            sp.lineTo (freqToX (20000.0), dbToY (0)); sp.closeSubPath();
            g.setColour (col::pink.withAlpha (0.28f)); g.fillPath (sp);
            g.setColour (col::pink.withAlpha (0.7f)); g.strokePath (sp, juce::PathStrokeType (1.0f));
        }
    }

    auto curvePath = [&] (const std::array<float, kPts>& v, bool closeToZero)
    {
        juce::Path p;
        for (int i = 0; i < kPts; ++i)
        {
            const float x = P.getX() + (float) i / (kPts - 1) * P.getWidth();
            const float y = juce::jlimit (P.getY() - 2, P.getBottom() + 2, dbToY (v[(size_t) i]));
            if (i == 0) { if (closeToZero) { p.startNewSubPath (x, dbToY (0)); p.lineTo (x, y); } else p.startNewSubPath (x, y); }
            else p.lineTo (x, y);
        }
        if (closeToZero) { p.lineTo (P.getRight(), dbToY (0)); p.closeSubPath(); }
        return p;
    };

    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (P.expanded (0, 2).toNearestInt());
        // per-band shapes
        for (int k = 0; k < kBands; ++k)
        {
            if (! shown[(size_t) k]) continue;
            const auto c = bandColour (k);
            const bool sel = k == page.selected, hov = k == hover || k == drag;
            const float a = active[(size_t) k] ? 1.0f : 0.35f;
            if (sel || hov)
            {
                g.setColour (c.withAlpha ((sel ? 0.16f : 0.10f) * a)); g.fillPath (curvePath (bandDb[(size_t) k], true));
            }
            g.setColour (c.withAlpha ((sel ? 0.75f : (hov ? 0.55f : 0.22f)) * a));
            g.strokePath (curvePath (bandDb[(size_t) k], false), juce::PathStrokeType (sel ? 1.4f : 1.0f));
            // dynamic: show the static shape dashed + the reachable range
            const auto& p = u.bp[(size_t) k];
            if (p.dyn.b() && isGainType (p.type.i()) && active[(size_t) k])
            {
                juce::Path st = curvePath (staticDb[(size_t) k], false), dashed;
                const float dl[] { 3.0f, 3.0f };
                juce::PathStrokeType (1.0f).createDashedStroke (dashed, st, dl, 2);
                g.setColour (c.withAlpha (0.45f)); g.fillPath (dashed);
            }
        }
        // total curve
        const bool anyOn = std::any_of (active.begin(), active.end(), [] (bool b) { return b; });
        if (anyOn)
        {
            juce::ColourGradient gr (col::slime.withAlpha (0.10f), 0, dbToY (r), col::slime.withAlpha (0.0f), 0, dbToY (0), false);
            g.setGradientFill (gr); g.fillPath (curvePath (totalDb, true));
        }
        ui::glowStroke (g, curvePath (totalDb, false), anyOn ? col::white.withAlpha (0.95f) : col::white.withAlpha (0.35f), 1.8f, anyOn ? 1.0f : 0.0f);
    }

    // sketch
    if (page.sketchT.getToggleState())
    {
        juce::Path sp; bool started = false;
        for (int i = 0; i < kPts; ++i)
        {
            if (! sketched[(size_t) i]) { started = false; continue; }
            const float x = P.getX() + (float) i / (kPts - 1) * P.getWidth(), y = dbToY (sketch[(size_t) i]);
            if (! started) { sp.startNewSubPath (x, y); started = true; } else sp.lineTo (x, y);
        }
        ui::glowStroke (g, sp, col::gold, 2.0f);
        g.setColour (col::gold.withAlpha (0.8f)); g.setFont (font (11.0f));
        g.drawText ("SKETCH: draw the curve you want, release to fit bands", P.reduced (10, 8).removeFromTop (16), juce::Justification::centredLeft, false);
    }

    // nodes
    for (int k = 0; k < kBands; ++k)
    {
        if (! shown[(size_t) k]) continue;
        const auto c = bandColour (k);
        const auto pt = nodePos (k);
        const bool sel = k == page.selected, hov = k == hover || k == drag, on = active[(size_t) k];
        const auto& p = u.bp[(size_t) k];
        // dynamic range bar + live position
        if (p.dyn.b() && on && isGainType (p.type.i()))
        {
            const float g0 = p.gain() * u.scale(), rng = p.range();
            const float y0 = dbToY (juce::jlimit (-r, r, g0)), y1 = dbToY (juce::jlimit (-r, r, g0 + rng));
            g.setColour (c.withAlpha (0.25f)); g.fillRoundedRectangle (pt.x - 2.5f, std::min (y0, y1), 5.0f, std::abs (y1 - y0), 2.5f);
            const float yd = dbToY (juce::jlimit (-r, r, g0 + u.dynDb[(size_t) k].load()));
            g.setColour (c); g.fillRoundedRectangle (pt.x - 6, yd - 1.5f, 12, 3, 1.5f);
        }
        const float rad = sel || hov ? 8.5f : 7.0f;
        if (on) ui::glowDot (g, pt, 0.1f, c.withAlpha (sel ? 0.9f : 0.6f));
        g.setColour (on ? c : col::bg); g.fillEllipse (pt.x - rad, pt.y - rad, rad * 2, rad * 2);
        g.setColour (on ? (sel ? col::white : c.brighter (0.3f)) : c.withAlpha (0.6f)); g.drawEllipse (pt.x - rad, pt.y - rad, rad * 2, rad * 2, sel ? 2.0f : 1.2f);
        g.setColour (on ? col::bg : c); g.setFont (font (9.5f));
        g.drawText (juce::String (k + 1), juce::Rectangle<float> (pt.x - rad, pt.y - rad, rad * 2, rad * 2), juce::Justification::centred, false);
        const char* tag = placementTag (p.stereo.i());
        if (*tag) { g.setColour (c); g.setFont (font (9.0f)); g.drawText (tag, juce::Rectangle<float> (pt.x + rad - 1, pt.y - rad - 9, 12, 10), juce::Justification::centredLeft, false); }
        if (p.dyn.b()) { g.setColour (c.withAlpha (0.9f)); g.drawEllipse (pt.x - rad - 3, pt.y - rad - 3, rad * 2 + 6, rad * 2 + 6, 0.8f); }
        if (u.soloBand.load() == k) { g.setColour (col::gold); g.setFont (font (9.0f)); g.drawText ("SOLO", juce::Rectangle<float> (pt.x - 20, pt.y + rad + 2, 40, 10), juce::Justification::centred, false); }
    }

    // readouts
    const int hk = drag >= 0 ? drag : hover;
    if (hk >= 0)
    {
        const auto& p = u.bp[(size_t) hk];
        const auto pt = nodePos (hk);
        const double f = p.freq();
        juce::StringArray lines;
        lines.add ("BAND " + juce::String (hk + 1) + "  " + typeNames()[p.type.i()].toUpperCase()
                   + (usesSlope (p.type.i()) ? "  " + slopeNames()[p.slope.i()] : juce::String())
                   + (p.stereo.i() > 0 ? "  " + placementNames()[p.stereo.i()].toUpperCase() : juce::String()));
        lines.add (freqText (f) + "   " + noteName (f));
        juce::String l3;
        if (isGainType (p.type.i())) l3 << (p.gain() >= 0 ? "+" : "") << juce::String (p.gain() * u.scale(), 1) << " dB   ";
        if (p.type.i() != btFlatTilt) l3 << "Q " << juce::String (p.q(), 2);
        lines.add (l3);
        if (p.dyn.b())
            lines.add ((p.spec.b() ? "SPECTRAL " : "DYN ") + juce::String (u.dynDb[(size_t) hk].load(), 1) + " dB   thr "
                       + juce::String (u.thrUsed[(size_t) hk].load(), 0));
        const float w = 170, h = 14.0f * (float) lines.size() + 10;
        auto box = juce::Rectangle<float> (pt.x + 14, pt.y - h - 6, w, h);
        if (box.getRight() > P.getRight()) box.setX (pt.x - w - 14);
        if (box.getY() < P.getY()) box.setY (pt.y + 12);
        g.setColour (T (juce::Colour (0xee0b0c12))); g.fillRoundedRectangle (box, 7);
        g.setColour (bandColour (hk).withAlpha (0.6f)); g.drawRoundedRectangle (box, 7, 1.0f);
        for (int i = 0; i < lines.size(); ++i)
        {
            g.setColour (i == 0 ? bandColour (hk) : col::white.withAlpha (0.9f)); g.setFont (font (i == 0 ? 10.0f : 11.0f, i == 0));
            g.drawText (lines[i], box.reduced (8, 5).withHeight (14).translated (0, 14.0f * (float) i), juce::Justification::centredLeft, false);
        }
    }
    else if (hoverPos.x >= 0 && P.contains (hoverPos))
    {
        const double f = xToFreq (hoverPos.x);
        const juce::String s = freqText (f) + "  " + noteName (f) + "   " + juce::String (yToDb (hoverPos.y), 1) + " dB";
        g.setColour (col::dim.brighter (0.3f)); g.setFont (font (10.0f, false));
        g.drawText (s, P.reduced (10, 6).removeFromTop (14), juce::Justification::centredRight, false);
        g.setColour (col::white.withAlpha (0.08f)); g.drawVerticalLine ((int) hoverPos.x, P.getY(), P.getBottom());
    }
    // mode badge
    {
        const int m = u.mode.i();
        juce::String s = m == 0 ? "NATURAL PHASE" : "LINEAR PHASE " + juce::String (m == 1 ? "LOW" : (m == 2 ? "MED" : "HIGH"));
        const int ch = u.character.i();
        if (ch > 0) s << "  ·  " << (ch == 1 ? "SUBTLE" : "WARM");
        g.setColour (acc.withAlpha (0.55f)); g.setFont (font (9.5f));
        g.drawText (s, P.reduced (10, 6).removeFromBottom (12), juce::Justification::centredLeft, false);
    }
}

void CurveView::mouseMove (const juce::MouseEvent& e)
{
    hoverPos = e.position;
    hover = hitNode (e.position);
    setMouseCursor (hover >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::CrosshairCursor);
    repaint();
}

int CurveView::addBand (double f, float gain)
{
    auto& u = page.unit;
    int k = -1;
    for (int i = 0; i < kBands; ++i) if (! u.bp[(size_t) i].on.b()) { k = i; break; }
    if (k < 0) return -1;
    int type = btBell;
    if (f < 30.0) type = btLowCut; else if (f > 15000.0) type = btHighCut;
    auto& pr = page.params;
    pr.set (bid (k, "type"), (float) type); pr.set (bid (k, "freq"), (float) f);
    pr.set (bid (k, "gain"), type == btBell ? juce::jlimit (-30.0f, 30.0f, gain) : 0.0f);
    pr.set (bid (k, "q"), type == btBell ? 1.0f : 0.71f); pr.set (bid (k, "slope"), type == btBell ? 1.0f : 3.0f);
    pr.set (bid (k, "stereo"), 0); pr.set (bid (k, "byp"), 0); pr.set (bid (k, "dyn"), 0); pr.set (bid (k, "spec"), 0);
    pr.set (bid (k, "on"), 1);
    lastSpec[(size_t) k] = {}; lastSpec[(size_t) k].freq = -1;
    page.select (k);
    computeCurves();
    return k;
}

void CurveView::mouseDown (const juce::MouseEvent& e)
{
    auto& u = page.unit;
    if (page.sketchT.getToggleState() && ! e.mods.isPopupMenu())
    {
        sketching = true; sketched.fill (false); lastSketch = e.position; mouseDrag (e); return;
    }
    const int k = hitNode (e.position);
    if (k >= 0 && (e.mods.isPopupMenu() || e.mods.isAltDown())) { page.select (k); showBandMenu (k); return; }
    if (k < 0 && e.mods.isPopupMenu()) { showEmptyMenu (e.position); return; }
    if (e.mods.isPopupMenu()) return;
    int target = k;
    if (target < 0)
    {
        // single click near the curve adds a band there
        const float rel = (e.position.x - plot().getX()) / plot().getWidth();
        const int i = juce::jlimit (0, kPts - 1, (int) std::lround (rel * (kPts - 1)));
        if (std::abs (dbToY (totalDb[(size_t) i]) - e.position.y) < 10.0f && e.getNumberOfClicks() == 1)
            target = addBand (xToFreq (e.position.x), yToDb (e.position.y));
        if (target < 0) return;
    }
    page.select (target);
    drag = target; dragStart = e.position;
    const auto& p = u.bp[(size_t) target];
    startF = p.freq(); startG = p.gain(); startQ = p.q();
    dragQ = ! isGainType (p.type.i()) && p.type.i() != btNotch && p.type.i() != btBandPass;
    page.params.begin (bid (target, "freq")); page.params.begin (bid (target, isGainType (p.type.i()) ? "gain" : "q"));
}

void CurveView::mouseDrag (const juce::MouseEvent& e)
{
    if (sketching)
    {
        auto P = plot();
        auto toIdx = [&] (float x) { return juce::jlimit (0, kPts - 1, (int) std::lround ((x - P.getX()) / P.getWidth() * (kPts - 1))); };
        const int i0 = toIdx (lastSketch.x), i1 = toIdx (e.position.x);
        const float d0 = yToDb (lastSketch.y), d1 = yToDb (e.position.y);
        const int lo = std::min (i0, i1), hi = std::max (i0, i1);
        for (int i = lo; i <= hi; ++i)
        {
            const float t = hi == lo ? 1.0f : (float) (i - i0) / (float) (i1 - i0);
            sketch[(size_t) i] = juce::jlimit (-range(), range(), d0 + t * (d1 - d0)); sketched[(size_t) i] = true;
        }
        lastSketch = e.position; repaint(); return;
    }
    if (drag < 0) return;
    auto& u = page.unit;
    const auto& p = u.bp[(size_t) drag];
    const float fine = e.mods.isShiftDown() ? 0.15f : 1.0f;
    const auto d = (e.position - dragStart) * fine;
    const double f = juce::jlimit (kFMin, kFMax, xToFreq (freqToX (startF) + d.x));
    page.params.setRaw (bid (drag, "freq"), (float) f);
    if (isGainType (p.type.i()))
    {
        const float sc = std::abs (u.scale()) > 0.05f ? u.scale() : 1.0f;
        const float g0 = startG * sc;
        const float gNew = yToDb (dbToY (g0) + d.y) / sc;
        page.params.setRaw (bid (drag, "gain"), juce::jlimit (-30.0f, 30.0f, gNew));
    }
    else if (dragQ)
        page.params.setRaw (bid (drag, "q"), juce::jlimit (0.1f, 20.0f, startQ * std::exp (-d.y * 0.012f)));
    computeCurves(); repaint();
}

void CurveView::mouseUp (const juce::MouseEvent&)
{
    if (sketching) { sketching = false; page.fitSketch(); return; }
    if (drag >= 0)
    {
        const auto& p = page.unit.bp[(size_t) drag];
        page.params.end (bid (drag, "freq")); page.params.end (bid (drag, isGainType (p.type.i()) ? "gain" : "q"));
        drag = -1;
    }
}

void CurveView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (page.sketchT.getToggleState()) return;
    const int k = hitNode (e.position);
    if (k >= 0) { page.params.set (bid (k, "byp"), page.unit.bp[(size_t) k].byp.b() ? 0.0f : 1.0f); return; }
    if (plot().contains (e.position)) addBand (xToFreq (e.position.x), yToDb (e.position.y));
}

void CurveView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    int k = hitNode (e.position);
    if (k < 0) k = hover >= 0 ? hover : -1;
    if (k < 0) return;
    const float q = page.unit.bp[(size_t) k].q();
    page.params.set (bid (k, "q"), juce::jlimit (0.025f, 40.0f, q * std::exp (w.deltaY * (e.mods.isShiftDown() ? 0.25f : 1.2f))));
    computeCurves(); repaint();
}

void CurveView::showBandMenu (int k)
{
    auto& u = page.unit; auto& pr = page.params;
    const auto& p = u.bp[(size_t) k];
    juce::PopupMenu m, shape, slope, place;
    for (int i = 0; i < typeNames().size(); ++i) shape.addItem (100 + i, typeNames()[i], true, p.type.i() == i);
    for (int i = 0; i < slopeNames().size(); ++i) slope.addItem (200 + i, slopeNames()[i] + "/oct", true, p.slope.i() == i);
    for (int i = 0; i < placementNames().size(); ++i) place.addItem (300 + i, placementNames()[i], true, p.stereo.i() == i);
    m.addSectionHeader ("BAND " + juce::String (k + 1));
    m.addSubMenu ("Shape", shape); m.addSubMenu ("Slope", slope, usesSlope (p.type.i())); m.addSubMenu ("Stereo placement", place);
    m.addSeparator();
    m.addItem (1, "Dynamic EQ", true, p.dyn.b());
    m.addItem (2, "Spectral dynamics", true, p.spec.b());
    m.addItem (3, "Auto threshold", p.dyn.b(), p.autoThr.b());
    m.addSeparator();
    m.addItem (4, "Solo (audition band)", true, u.soloBand.load() == k);
    m.addItem (5, "Bypass", true, p.byp.b());
    m.addItem (6, "Delete band");
    juce::Component::SafePointer<CurveView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe, k] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& pg = safe->page; auto& un = pg.unit; auto& pa = pg.params;
        const auto& bp = un.bp[(size_t) k];
        if (r >= 100 && r < 200) pa.set (bid (k, "type"), (float) (r - 100));
        else if (r >= 200 && r < 300) pa.set (bid (k, "slope"), (float) (r - 200));
        else if (r >= 300 && r < 400) pa.set (bid (k, "stereo"), (float) (r - 300));
        else if (r == 1) pa.set (bid (k, "dyn"), bp.dyn.b() ? 0.0f : 1.0f);
        else if (r == 2) { const bool on = ! bp.spec.b(); pa.set (bid (k, "spec"), on ? 1.0f : 0.0f); if (on) pa.set (bid (k, "dyn"), 1.0f); }
        else if (r == 3) pa.set (bid (k, "auto"), bp.autoThr.b() ? 0.0f : 1.0f);
        else if (r == 4) un.soloBand.store (un.soloBand.load() == k ? -1 : k);
        else if (r == 5) pa.set (bid (k, "byp"), bp.byp.b() ? 0.0f : 1.0f);
        else if (r == 6) { pa.set (bid (k, "on"), 0.0f); if (un.soloBand.load() == k) un.soloBand.store (-1); }
        safe->computeCurves(); safe->repaint();
    });
    (void) pr;
}

void CurveView::showEmptyMenu (juce::Point<float> pos)
{
    juce::PopupMenu m;
    m.addItem (1, "Add band here");
    juce::PopupMenu rng; const char* names[] { "6 dB", "12 dB", "24 dB", "36 dB" };
    int cur; { const juce::ScopedLock sl (page.unit.uiLock); cur = page.unit.dispRange; }
    for (int i = 0; i < 4; ++i) rng.addItem (10 + i, juce::String ("Display range ") + names[i], true, cur == i);
    m.addSubMenu ("Display range", rng);
    m.addItem (20, "Reset all bands");
    juce::Component::SafePointer<CurveView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe, pos] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r == 1) safe->addBand (safe->xToFreq (pos.x), safe->yToDb (pos.y));
        else if (r >= 10 && r < 14) { const juce::ScopedLock sl (safe->page.unit.uiLock); safe->page.unit.dispRange = r - 10; }
        else if (r == 20) for (int k = 0; k < kBands; ++k) safe->page.params.set (bid (k, "on"), 0.0f);
        safe->computeCurves(); safe->repaint();
    });
}

// ============================================================================ sketch fitting
void EqPage::fitSketch()
{
    auto& c = curve;
    if (std::none_of (c.sketched.begin(), c.sketched.end(), [] (bool b) { return b; })) return;
    const double sr = unit.sampleRate.load();
    std::array<double, kPts> fr {}, T {}, R {};
    for (int i = 0; i < kPts; ++i)
    {
        fr[(size_t) i] = std::min (sr * 0.49, kFMin * std::pow (kFMax / kFMin, (double) i / (kPts - 1)));
        T[(size_t) i] = c.sketched[(size_t) i] ? c.sketch[(size_t) i] : 0.0;
    }
    struct Fit { BandSpec s; std::array<double, kPts> resp {}; };
    std::vector<Fit> fits;
    auto respOf = [&] (const BandSpec& s, std::array<double, kPts>& out)
    {
        Sections sec; designBand (s, sr, sec);
        for (int i = 0; i < kPts; ++i) out[(size_t) i] = 20.0 * std::log10 (std::max (1e-9, sec.mag (fr[(size_t) i], sr)));
    };
    auto residual = [&]
    {
        R = T;
        for (auto& f : fits) for (int i = 0; i < kPts; ++i) R[(size_t) i] -= f.resp[(size_t) i];
    };
    residual();
    for (int it = 0; it < 8; ++it)
    {
        int idx = 0; double best = 0;
        for (int i = 0; i < kPts; ++i) if (c.sketched[(size_t) i] && std::abs (R[(size_t) i]) > best) { best = std::abs (R[(size_t) i]); idx = i; }
        if (best < 0.6) break;
        const double pk = R[(size_t) idx], half = std::abs (pk) * 0.5;
        int lo = idx, hi = idx;
        while (lo > 0 && std::abs (R[(size_t) (lo - 1)]) > half && R[(size_t) (lo - 1)] * pk > 0) --lo;
        while (hi < kPts - 1 && std::abs (R[(size_t) (hi + 1)]) > half && R[(size_t) (hi + 1)] * pk > 0) ++hi;
        Fit f;
        if (lo == 0 && idx < kPts / 5) { f.s.type = btLowShelf; f.s.freq = fr[(size_t) hi]; f.s.gain = pk; f.s.q = 0.7; }
        else if (hi == kPts - 1 && idx > kPts * 4 / 5) { f.s.type = btHighShelf; f.s.freq = fr[(size_t) lo]; f.s.gain = pk; f.s.q = 0.7; }
        else
        {
            const double bw = std::max (0.15, std::log2 (fr[(size_t) hi] / fr[(size_t) lo]));
            const double p2 = std::pow (2.0, bw);
            f.s.type = btBell; f.s.freq = fr[(size_t) idx]; f.s.gain = pk; f.s.q = juce::jlimit (0.3, 12.0, std::sqrt (p2) / (p2 - 1.0));
        }
        f.s.gain = juce::jlimit (-30.0, 30.0, f.s.gain);
        respOf (f.s, f.resp); fits.push_back (f); residual();
    }
    // refine gains by least squares, one band at a time
    for (int pass = 0; pass < 6; ++pass)
        for (auto& f : fits)
        {
            if (std::abs (f.s.gain) < 0.05) continue;
            double num = 0, den = 0;
            for (int i = 0; i < kPts; ++i)
            {
                if (! c.sketched[(size_t) i]) continue;
                const double s = f.resp[(size_t) i] / f.s.gain;
                num += R[(size_t) i] * s; den += s * s;
            }
            if (den < 1e-9) continue;
            f.s.gain = juce::jlimit (-30.0, 30.0, f.s.gain + num / den);
            respOf (f.s, f.resp); residual();
        }
    const float sc = std::abs (unit.scale()) > 0.05f ? unit.scale() : 1.0f;
    for (int k = 0; k < kBands; ++k)
    {
        if (k < (int) fits.size())
        {
            const auto& s = fits[(size_t) k].s;
            params.set (bid (k, "type"), (float) s.type); params.set (bid (k, "freq"), (float) s.freq);
            params.set (bid (k, "gain"), (float) s.gain / sc); params.set (bid (k, "q"), (float) s.q);
            params.set (bid (k, "slope"), 1); params.set (bid (k, "stereo"), 0); params.set (bid (k, "dyn"), 0);
            params.set (bid (k, "spec"), 0); params.set (bid (k, "byp"), 0); params.set (bid (k, "on"), 1);
        }
        else params.set (bid (k, "on"), 0);
        curve.lastSpec[(size_t) k].freq = -1;
    }
    curve.computeCurves(); curve.repaint();
}

} // namespace

std::unique_ptr<ui::Page> createPage (juce::AudioProcessorValueTreeState& apvts, UnitDsp& unit)
{
    if (auto* u = dynamic_cast<EqUnit*> (&unit)) return std::make_unique<EqPage> (apvts, *u);
    return std::make_unique<ui::Page>();
}

} // namespace eq
} // namespace gpfx
