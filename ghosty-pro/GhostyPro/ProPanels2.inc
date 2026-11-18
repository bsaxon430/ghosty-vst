// LAYOUT 2 (round 3): the STUDIO layout's panels. Included by ProEditor.cpp (after the pages, before ProContent): it reuses the
// editor's own pieces - Page (knob / combo / toggle / card helpers), WaveScreen, Scope, XYGhostPad, MorePanel, GhostKnob, FlowStrip,
// paintCard - and only adds what STUDIO shows that CLASSIC has no component for: the draggable filter / envelope / LFO displays,
// the on-screen keys, and the front page that holds them all (StudioBody). ProStudio (the window) is declared here and
// defined in ProLayout2Studio.inc, after ProContent.
// Behaviour reference only (docs/research/SURGE-ODIN-SERUM-STUDY.md §3.8: envelopes / LFOs edited in the graph, drag a source onto a
// knob); the look is Ghosty's (house palette, ghosts, our knobs).

namespace studio2
{
namespace L2 = gpro::layout2;

// one host gesture per mouse drag on a parameter
struct ParamDrag
{
    juce::RangedAudioParameter* p = nullptr; float start = 0;
    void begin (ProProcessor& proc, const juce::String& id) { end(); p = proc.apvts.getParameter (id); if (p != nullptr) { start = p->getValue(); p->beginChangeGesture(); } }
    void set (float norm) { if (p != nullptr) p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, norm)); }
    void end() { if (p != nullptr) p->endChangeGesture(); p = nullptr; }
    bool active() const { return p != nullptr; }
};
inline float norm (ProProcessor& proc, const juce::String& id) { auto* p = proc.apvts.getParameter (id); return p != nullptr ? p->getValue() : 0.0f; }
inline juce::String text (ProProcessor& proc, const juce::String& id) { auto* p = proc.apvts.getParameter (id); return p != nullptr ? p->getCurrentValueAsText() : juce::String(); }
inline void resetToDefault (ProProcessor& proc, const juce::String& id)
{
    if (auto* p = proc.apvts.getParameter (id)) { p->beginChangeGesture(); p->setValueNotifyingHost (p->getDefaultValue()); p->endChangeGesture(); }
}

// a glowing neon stroke (the house "screen" line): wide faint halo, glow, the core, a hot thin centre
inline void neon (juce::Graphics& g, const juce::Path& p, juce::Colour c, float w = 2.2f)
{
    g.setColour (c.withAlpha (0.07f)); g.strokePath (p, juce::PathStrokeType (w * 5.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (c.withAlpha (0.16f)); g.strokePath (p, juce::PathStrokeType (w * 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (c);                   g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (c.brighter (0.8f).withAlpha (0.55f)); g.strokePath (p, juce::PathStrokeType (juce::jmax (0.8f, w * 0.35f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
inline void screenGrid (juce::Graphics& g, juce::Rectangle<float> a, juce::Colour c, int cols, int rows)
{
    g.setColour (c.withAlpha (0.07f));
    for (int i = 0; i <= cols; ++i) g.drawVerticalLine ((int) (a.getX() + a.getWidth() * (float) i / (float) cols), a.getY(), a.getBottom());
    for (int i = 0; i <= rows; ++i) g.drawHorizontalLine ((int) (a.getY() + a.getHeight() * (float) i / (float) rows), a.getX(), a.getRight());
}

// ================================================================== the oscillator display, narrower than CLASSIC's
// The same WaveScreen (3D table, GHOST A/B drawing, WAV drop); its two long hint lines at the bottom do not fit a half-width
// display, so this one covers them with one short line.
class StudioWave : public WaveScreen
{
public:
    explicit StudioWave (ProProcessor& p) : WaveScreen (p), pr (p) {}
    int preview = -1;       // a WAVE PALETTE tile under the mouse: its wave drawn over the display
    void paint (juce::Graphics& g) override
    {
        WaveScreen::paint (g);
        pt::ScopedDark screen;
        const auto r = getLocalBounds().toFloat();
        if (ghosty::Frame f; preview >= 0 && WavePalette::thumbFor (pr, osc, preview, f))
        {
            g.setColour (C().deep.withAlpha (0.72f)); g.fillRoundedRectangle (r, 10);
            const auto a = r.reduced (14.0f, 22.0f);
            float mx = 1.0e-6f; for (auto v : f) mx = juce::jmax (mx, std::abs (v));
            juce::Path p;
            for (int k = 0; k < ghosty::FRAME; k += 2) { const float x = a.getX() + (float) k / (float) (ghosty::FRAME - 2) * a.getWidth(), y = a.getCentreY() - f[(size_t) k] / mx * a.getHeight() * 0.48f; if (k == 0) p.startNewSubPath (x, y); else p.lineTo (x, y); }
            neon (g, p, C().gold, 2.4f);
            g.setColour (C().gold); g.setFont (font (10.0f));
            g.drawText ("PREVIEW  " + tableNames()[preview].toUpperCase() + "  (click to play it)", r.reduced (12, 5).removeFromTop (14), juce::Justification::centredRight);
        }
        const auto strip = juce::Rectangle<float> (6.0f, r.getHeight() - 21.0f, r.getWidth() - 12.0f, 17.0f);
        g.setColour (C().deep); g.fillRect (strip);
        g.setFont (font (9.5f));
        if (ghostMode())
        {
            g.setColour (editB ? C().pink : C().slime);
            g.drawText (juce::String ("DRAW ") + (editB ? "B" : "A") + ": drag = draw,  SHIFT = line,  right-click = menu", strip.reduced (6, 0), juce::Justification::centredLeft);
        }
        else
        {
            g.setColour (C().dim);
            g.drawText ("POS " + juce::String (juce::roundToInt (paramValue (pr, posId()) * 100)) + "%", strip.reduced (6, 0), juce::Justification::centredLeft);
            g.drawText ("DRAW A / B to draw  -  drop a WAV", strip.reduced (6, 0), juce::Justification::centredRight);
        }
    }
private:
    ProProcessor& pr;
};

// ================================================================== FILTER: the response curve, dragged
// x = CUTOFF (the axis is the cutoff knob's own scale: 30 Hz .. 19 kHz, log), y = RES; the wheel = DRIVE, which shows as the
// curve's thickness and warmth. Double-click = cutoff + res back to their defaults. Display only: it draws the filter TYPE's
// textbook shape, it does not run the filter.
class FilterCurve : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit FilterCurve (ProProcessor& p) : proc (p)
    {
        setTooltip ("FILTER: drag left / right = CUTOFF, up / down = RESONANCE (SHIFT = fine), mouse wheel = DRIVE, double-click = reset; with focus the arrow keys do the same");
        setMouseCursor (juce::MouseCursor::CrosshairCursor);
        setWantsKeyboardFocus (true); setTitle ("Filter curve");
        startTimerHz (30);
    }
    // the magnitude (linear) of the drawn response at f Hz
    static float response (int type, float f, float fc, float res)
    {
        const float w = f / fc, Q = 0.6f + res * 8.0f;
        auto den = [w] (float q) { return std::sqrt ((1.0f - w * w) * (1.0f - w * w) + (w / q) * (w / q)); };
        switch (type)
        {
            case 0: return 1.0f / den (Q);                                                      // LP 12
            case 1: return (w / Q) / den (Q) * juce::jmax (1.0f, Q * 0.5f);                     // BP
            case 2: return w * w / den (Q);                                                     // HP 12
            case 3: return std::abs (1.0f - w * w) / den (juce::jmax (0.3f, Q * 0.3f));         // Notch
            case 4: return (1.0f / den (Q)) * (1.0f / den (0.707f));                            // LP 24
            case 5: return (w * w / den (Q)) * (w * w / den (0.707f));                          // HP 24
            case 6: case 7:
            {   // comb: peaks (+) or notches (-) at multiples of the cutoff
                const float gg = 0.3f + res * 0.65f, c = std::cos (juce::MathConstants<float>::twoPi * w);
                return std::sqrt (juce::jmax (1.0e-6f, 1.0f + gg * gg + (type == 6 ? 2.0f : -2.0f) * gg * c)) / (1.0f + gg) * (type == 6 ? 1.0f : 1.6f);
            }
            case 8:
            {   // formant: three vowel-like bands
                float s = 0; const float ratios[] { 1.0f, 1.9f, 3.1f }, gains[] { 1.0f, 0.7f, 0.45f };
                for (int k = 0; k < 3; ++k) { const float ww = f / (fc * ratios[k]), q = 3.0f + res * 8.0f; s += gains[k] * (ww / q) / std::sqrt ((1 - ww * ww) * (1 - ww * ww) + (ww / q) * (ww / q)); }
                return s * 1.4f;
            }
            default: return 1.0f;                                                                // Off
        }
    }
    void paint (juce::Graphics& g) override
    {
        pt::ScopedDark screen;
        auto r = getLocalBounds().toFloat();
        if (progl::on (C().theme)) progl::screen (g, r, 10); else { g.setColour (C().deep); g.fillRoundedRectangle (r, 10); }    // GLASS (round 3): light frosted screen
        const auto a = plot();
        const int type = (int) paramValue (proc, "f_type");
        const float cut = paramValue (proc, "cutoff"), res = paramValue (proc, "res"), drive = paramValue (proc, "f_drive");
        const float fc = ProProcessor::cutoffHz (cut);
        // grid: 100 Hz / 1 kHz / 10 kHz, 0 dB and -12 / -24 dB
        g.setColour (C().white.withAlpha (0.07f));
        for (float f : { 100.0f, 1000.0f, 10000.0f }) { const float x = xOfHz (f, a); g.drawVerticalLine ((int) x, a.getY(), a.getBottom()); }
        for (float db : { 0.0f, -12.0f, -24.0f }) g.drawHorizontalLine ((int) yOfDb (db, a), a.getX(), a.getRight());
        g.setColour (C().dim); g.setFont (font (8.5f, false));
        for (auto [f, t] : { std::pair<float, const char*> { 100.0f, "100" }, { 1000.0f, "1k" }, { 10000.0f, "10k" } })
            g.drawText (t, juce::Rectangle<float> (xOfHz (f, a) + 2, a.getBottom() - 11, 30, 10), juce::Justification::centredLeft);
        // the curve (thicker + warmer with DRIVE)
        juce::Path p; const int N = 160;
        for (int i = 0; i <= N; ++i)
        {
            const float x = a.getX() + (float) i / N * a.getWidth();
            const float f = 30.0f * std::pow (2.0f, (float) i / N * 9.3f);
            const float db = juce::Decibels::gainToDecibels (response (type, f, fc, res), -60.0f);
            const float y = yOfDb (db, a);
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        const auto col = C().ice.interpolatedWith (C().gold, juce::jmin (1.0f, drive * 1.6f)).interpolatedWith (C().pink, juce::jmax (0.0f, drive - 0.6f) * 2.0f);
        // ghosted earlier curves (where the knobs just were), fading
        for (int k = 0; k < kTrail; ++k)
        {
            const auto& t = trail[(size_t) ((trailHead + k) % kTrail)];
            if (t[0] < 0) continue;
            juce::Path q;
            for (int i = 0; i <= N; i += 2)
            {
                const float x = a.getX() + (float) i / N * a.getWidth(), f = 30.0f * std::pow (2.0f, (float) i / N * 9.3f);
                const float y = yOfDb (juce::Decibels::gainToDecibels (response ((int) t[3], f, ProProcessor::cutoffHz (t[0]), t[1]), -60.0f), a);
                if (i == 0) q.startNewSubPath (x, y); else q.lineTo (x, y);
            }
            g.setColour (col.withAlpha (0.05f + 0.035f * (float) k)); g.strokePath (q, juce::PathStrokeType (1.0f));
        }
        auto fill = p; fill.lineTo (a.getRight(), a.getBottom()); fill.lineTo (a.getX(), a.getBottom()); fill.closeSubPath();
        juce::ColourGradient fg (col.withAlpha (0.22f + drive * 0.12f), 0, a.getY(), col.withAlpha (0.02f), 0, a.getBottom(), false);
        g.setGradientFill (fg); g.fillPath (fill);
        neon (g, p, col, 2.0f + drive * 3.0f);
        // the handle: at the cutoff, on the curve
        const float hx = xOfHz (fc, a), hy = juce::jlimit (a.getY(), a.getBottom(), yOfDb (juce::Decibels::gainToDecibels (response (type, fc, fc, res), -60.0f), a));
        g.setColour (col.withAlpha (0.3f)); g.fillEllipse (hx - 9, hy - 9, 18, 18);
        g.setColour (drag.active() || isMouseOver() ? C().white : col); g.drawEllipse (hx - 6, hy - 6, 12, 12, 2.0f);
        // names + values
        g.setFont (font (10.0f)); g.setColour (C().ice);
        g.drawText (filterTypeNames()[type], r.reduced (10, 5).removeFromTop (14), juce::Justification::centredLeft);
        g.setColour (C().dim); g.setFont (font (9.5f));
        g.drawText ("CUTOFF " + text (proc, "cutoff") + "   RES " + text (proc, "res") + "   DRIVE " + text (proc, "f_drive"),
                    r.reduced (10, 5).removeFromTop (14), juce::Justification::centredRight);
    }
    void mouseDown (const juce::MouseEvent&) override { drag.begin (proc, "cutoff"); drag2.begin (proc, "res"); repaint(); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const float k = e.mods.isShiftDown() ? 0.2f : 1.0f;
        const auto a = plot();
        drag.set (drag.start + (float) e.getDistanceFromDragStartX() / a.getWidth() * k);
        drag2.set (drag2.start - (float) e.getDistanceFromDragStartY() / a.getHeight() * k);
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override { drag.end(); drag2.end(); repaint(); }
    void mouseDoubleClick (const juce::MouseEvent&) override { resetToDefault (proc, "cutoff"); resetToDefault (proc, "res"); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        if (auto* p = proc.apvts.getParameter ("f_drive")) { p->beginChangeGesture(); p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, p->getValue() + w.deltaY * 0.15f)); p->endChangeGesture(); }
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    bool keyPressed (const juce::KeyPress& k) override
    {   // arrows: left / right = CUTOFF, up / down = RESONANCE (SHIFT = fine)
        const float st = k.getModifiers().isShiftDown() ? 0.002f : 0.01f;
        auto nudge = [this] (const char* id, float d) { if (auto* p = proc.apvts.getParameter (id)) { p->beginChangeGesture(); p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, p->getValue() + d)); p->endChangeGesture(); } };
        const int kc = k.getKeyCode();
        if (kc == juce::KeyPress::leftKey)  { nudge ("cutoff", -st); return true; }
        if (kc == juce::KeyPress::rightKey) { nudge ("cutoff", st); return true; }
        if (kc == juce::KeyPress::upKey)    { nudge ("res", st); return true; }
        if (kc == juce::KeyPress::downKey)  { nudge ("res", -st); return true; }
        return false;
    }
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }
    void paintOverChildren (juce::Graphics& g) override
    {
        if (hasKeyboardFocus (false)) { g.setColour (C().slime.withAlpha (0.8f)); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 10.0f, 1.5f); }
    }

private:
    ProProcessor& proc;
    ParamDrag drag, drag2;
    float last[4] { -1, -1, -1, -1 };
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (10.0f, 6.0f).withTrimmedTop (16.0f); }
    static float xOfHz (float f, juce::Rectangle<float> a) { return a.getX() + juce::jlimit (0.0f, 1.0f, std::log2 (f / 30.0f) / 9.3f) * a.getWidth(); }
    static float yOfDb (float db, juce::Rectangle<float> a) { return a.getY() + (18.0f - juce::jlimit (-36.0f, 18.0f, db)) / 54.0f * a.getHeight(); }
    void timerCallback() override
    {
        const float v[4] { paramValue (proc, "cutoff"), paramValue (proc, "res"), paramValue (proc, "f_drive"), paramValue (proc, "f_type") };
        if (std::memcmp (v, last, sizeof (v)) != 0)
        {   // the old curve joins the fading trail
            if (last[0] >= 0) { trail[(size_t) trailHead] = { last[0], last[1], last[2], last[3] }; trailHead = (trailHead + 1) % kTrail; }
            std::memcpy (last, v, sizeof (v)); idle = 0; repaint();
        }
        else if (++idle == 15) { for (auto& t : trail) t[0] = -1; repaint(); }      // still for half a second: the trail goes
    }
    static constexpr int kTrail = 6;
    std::array<std::array<float, 4>, kTrail> trail { { { -1, 0, 0, 0 }, { -1, 0, 0, 0 }, { -1, 0, 0, 0 }, { -1, 0, 0, 0 }, { -1, 0, 0, 0 }, { -1, 0, 0, 0 } } };
    int trailHead = 0, idle = 0;
};

// ================================================================== ENVELOPES: one curve, its handles dragged
// Segments are drawn at their knob's travel (not in seconds), so a handle stays under the mouse: the attack peak = ATTACK, the
// decay corner = DECAY (x) + SUSTAIN (y), the end = RELEASE; dragging the decay or release slope up / down = CURVE (shared by every
// envelope, as in the engine). Live dots: one per sounding voice. Double-click a handle = its default.
class EnvCurve : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit EnvCurve (ProProcessor& p) : proc (p)
    {
        setTooltip ("Drag the handles: ATTACK (peak), DECAY + SUSTAIN (corner), RELEASE (end); drag a slope up / down = CURVE; double-click a handle = default");
        startTimerHz (30);
    }
    int env = 0;     // 0 AMP, 1 FILTER, 2 ENV 2, 3 ENV 3 (= the tracer's envelope index)
    static juce::StringArray idsFor (int e)
    {
        switch (e)
        {
            case 1: return { "f_a", "f_d", "f_s", "f_r" };
            case 2: return { "e2_a", "e2_d", "e2_s", "e2_r" };
            case 3: return { "e3_a", "e3_d", "e3_s", "e3_r" };
            default: return { "attack", "decay", "sustain", "release" };
        }
    }
    static juce::String nameFor (int e) { return e == 1 ? "FILTER ENV" : e == 2 ? "ENV 2" : e == 3 ? "ENV 3" : "AMP ENV"; }
    static juce::Colour colourFor (int e) { return e == 1 ? C().ecto : e == 2 ? C().pink : e == 3 ? C().ice : C().slime; }

    void paint (juce::Graphics& g) override
    {
        pt::ScopedDark screen;
        auto r = getLocalBounds().toFloat();
        if (progl::on (C().theme)) progl::screen (g, r, 10); else { g.setColour (C().deep); g.fillRoundedRectangle (r, 10); }    // GLASS (round 3): light frosted screen
        const auto ids = idsFor (env);
        const auto gm = geometry();
        const auto col = colourFor (env);
        const float s = paramValue (proc, ids[2]);
        const float c = 1.0f + paramValue (proc, "curve") * 3.0f;
        screenGrid (g, gm.a, C().white, 12, 4);
        g.setColour (col.withAlpha (0.12f));
        for (float x : { gm.xa, gm.xd, gm.xs }) g.drawVerticalLine ((int) x, gm.a.getY(), gm.a.getBottom());
        juce::Path p; p.startNewSubPath (gm.a.getX(), Y (0, gm)); p.lineTo (gm.xa, Y (1, gm));
        for (int i = 1; i <= 24; ++i) { const float t = (float) i / 24; p.lineTo (gm.xa + (gm.xd - gm.xa) * t, Y (s + (1 - s) * std::pow (1 - t, c), gm)); }
        p.lineTo (gm.xs, Y (s, gm));
        for (int i = 1; i <= 24; ++i) { const float t = (float) i / 24; p.lineTo (gm.xs + (gm.xr - gm.xs) * t, Y (s * std::pow (1 - t, c), gm)); }
        auto fill = p; fill.lineTo (gm.xr, Y (0, gm)); fill.closeSubPath();
        juce::ColourGradient fg (col.withAlpha (0.24f), 0, gm.a.getY(), col.withAlpha (0.02f), 0, gm.a.getBottom(), false);
        g.setGradientFill (fg); g.fillPath (fill);
        neon (g, p, col, 2.4f);
        // handles
        const juce::Point<float> hs[] { { gm.xa, Y (1, gm) }, { gm.xd, Y (s, gm) }, { gm.xr, Y (0, gm) } };
        for (int i = 0; i < 3; ++i)
        {
            const bool hot = i == hover || i == dragging;
            g.setColour (col.withAlpha (hot ? 0.45f : 0.25f)); g.fillEllipse (hs[i].x - 8, hs[i].y - 8, 16, 16);
            g.setColour (hot ? C().white : col); g.drawEllipse (hs[i].x - 5, hs[i].y - 5, 10, 10, 2.0f);
        }
        // live dots: one per sounding voice
        ProProcessor::TraceSnap snap;
        if (proc.readTrace (snap))
            for (int i = snap.count - 1; i >= 0; --i)
            {
                const auto& v = snap.v[i];
                float x = -1;
                switch (v.stage[env])
                {
                    case 1: x = gm.a.getX() + v.lin[env] * (gm.xa - gm.a.getX()); break;
                    case 2: x = gm.xa + (1.0f - v.lin[env]) * (gm.xd - gm.xa); break;
                    case 3: x = gm.xd + (gm.xs - gm.xd) * 0.5f; break;
                    case 4: x = gm.xs + (1.0f - v.lin[env]) * (gm.xr - gm.xs); break;
                    default: break;
                }
                if (x < 0) continue;
                const float y = Y (juce::jlimit (0.0f, 1.0f, v.out[env]), gm), al = 1.0f - 0.11f * (float) i;
                g.setColour (col.withAlpha (0.3f * al)); g.fillEllipse (x - 7, y - 7, 14, 14);
                g.setColour (C().white.withAlpha (al)); g.fillEllipse (x - 3.5f, y - 3.5f, 7, 7);
            }
        // name + the values (the one being dragged in white)
        g.setFont (font (10.0f)); g.setColour (col);
        g.drawText (nameFor (env), r.reduced (10, 5).removeFromTop (14), juce::Justification::centredLeft);
        juce::String vals; const char* nm[] { "A ", "D ", "S ", "R " };
        for (int i = 0; i < 4; ++i) vals << nm[i] << text (proc, ids[i]) << (i < 3 ? "   " : "");
        g.setColour (C().dim); g.setFont (font (9.5f));
        g.drawText (vals, r.reduced (10, 5).removeFromTop (14), juce::Justification::centredRight);
    }
    void mouseMove (const juce::MouseEvent& e) override { const int h = handleAt (e.position); if (h != hover) { hover = h; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto ids = idsFor (env);
        dragging = handleAt (e.position);
        if (dragging == 0) dx.begin (proc, ids[0]);
        else if (dragging == 1) { dx.begin (proc, ids[1]); dy.begin (proc, ids[2]); }
        else if (dragging == 2) dx.begin (proc, ids[3]);
        else
        {   // on a slope: CURVE
            const auto gm = geometry();
            if ((e.position.x > gm.xa && e.position.x < gm.xd) || (e.position.x > gm.xs && e.position.x < gm.xr)) { dragging = 3; dy.begin (proc, "curve"); }
        }
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto gm = geometry();
        const float k = e.mods.isShiftDown() ? 0.2f : 1.0f;
        const float ddx = (float) e.getDistanceFromDragStartX() / gm.seg * k, ddy = (float) e.getDistanceFromDragStartY() / gm.a.getHeight() * k;
        if (dragging >= 0 && dragging <= 2) dx.set (dx.start + ddx);
        if (dragging == 1) dy.set (dy.start - ddy);
        if (dragging == 3) dy.set (dy.start + ddy);       // pulling the slope down = more curve (it sags)
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override { dx.end(); dy.end(); dragging = -1; repaint(); }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const auto ids = idsFor (env);
        switch (handleAt (e.position))
        {
            case 0: resetToDefault (proc, ids[0]); break;
            case 1: resetToDefault (proc, ids[1]); resetToDefault (proc, ids[2]); break;
            case 2: resetToDefault (proc, ids[3]); break;
            default: break;
        }
    }
    int handleAt (juce::Point<float> p) const
    {
        const auto gm = geometry(); const float s = paramValue (proc, idsFor (env)[2]);
        const juce::Point<float> hs[] { { gm.xa, Y (1, gm) }, { gm.xd, Y (s, gm) }, { gm.xr, Y (0, gm) } };
        int best = -1; float bd = 13.0f;
        for (int i = 0; i < 3; ++i) if (const float d = hs[i].getDistanceFrom (p); d < bd) { bd = d; best = i; }
        return best;
    }

private:
    ProProcessor& proc;
    ParamDrag dx, dy;
    int hover = -1, dragging = -1;
    struct Geo { juce::Rectangle<float> a; float seg, xa, xd, xs, xr; };
    Geo geometry() const
    {
        Geo gm; gm.a = getLocalBounds().toFloat().reduced (12.0f, 8.0f).withTrimmedTop (14.0f);
        const auto ids = idsFor (env);
        const float plateau = gm.a.getWidth() * 0.16f;
        gm.seg = (gm.a.getWidth() - plateau) / 3.0f;
        gm.xa = gm.a.getX() + studio2::norm (proc, ids[0]) * gm.seg;
        gm.xd = gm.xa + studio2::norm (proc, ids[1]) * gm.seg;
        gm.xs = gm.xd + plateau;
        gm.xr = gm.xs + studio2::norm (proc, ids[3]) * gm.seg;
        return gm;
    }
    static float Y (float v, const Geo& gm) { return gm.a.getBottom() - v * gm.a.getHeight(); }
    void timerCallback() override { repaint(); }
};

// ================================================================== LFO: the shape display, dragged
// The MOD page's Scope (trace + shape + a dot per voice), plus: drag up / down = RATE (with SYNC on: the DIVISION), double-click =
// the next SHAPE. The knobs and boxes next to it stay.
class LfoView : public Scope, public juce::SettableTooltipClient
{
public:
    LfoView()
    {
        setTooltip ("LFO: drag up / down = RATE (with SYNC: the division), double-click = next SHAPE; with focus: up / down = RATE, space = next SHAPE");
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        setWantsKeyboardFocus (true); setTitle ("LFO display");
    }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (proc == nullptr) return false;
        const juce::String pfx = "l" + juce::String (lfoIndex + 1) + "_";
        const int kc = k.getKeyCode();
        if (kc == juce::KeyPress::upKey || kc == juce::KeyPress::downKey)
        {
            if (auto* p = proc->apvts.getParameter (paramValue (*proc, pfx + "sync") > 0.5f ? pfx + "div" : pfx + "rate"))
            { p->beginChangeGesture(); p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, p->getValue() + (kc == juce::KeyPress::upKey ? 1.0f : -1.0f) * (k.getModifiers().isShiftDown() ? 0.005f : 0.03f))); p->endChangeGesture(); }
            return true;
        }
        if (kc == juce::KeyPress::spaceKey) { nextShape(); return true; }
        return false;
    }
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }
    void mouseDown (const juce::MouseEvent&) override
    {
        if (proc == nullptr) return;
        const juce::String pfx = "l" + juce::String (lfoIndex + 1) + "_";
        drag.begin (*proc, paramValue (*proc, pfx + "sync") > 0.5f ? pfx + "div" : pfx + "rate");
    }
    void mouseDrag (const juce::MouseEvent& e) override { drag.set (drag.start - (float) e.getDistanceFromDragStartY() / 160.0f * (e.mods.isShiftDown() ? 0.2f : 1.0f)); }
    void mouseUp (const juce::MouseEvent&) override { drag.end(); }
    void mouseDoubleClick (const juce::MouseEvent&) override { nextShape(); }
    void nextShape()
    {
        if (proc == nullptr) return;
        const juce::String id = "l" + juce::String (lfoIndex + 1) + "_shape";
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (proc->apvts.getParameter (id)))
        { p->beginChangeGesture(); *p = (p->getIndex() + 1) % p->choices.size(); p->endChangeGesture(); }
    }
    void paint (juce::Graphics& g) override
    {
        Scope::paint (g);
        if (proc == nullptr) return;
        pt::ScopedDark screen;
        const juce::String pfx = "l" + juce::String (lfoIndex + 1) + "_";
        {   // the shape, as a neon line over the scope's faint one
            auto r = getLocalBounds().toFloat();
            const int shape = (int) paramValue (*proc, pfx + "shape");
            const float x0 = r.getX() + 4, w = r.getWidth() - 30;
            auto yOf = [&] (float v) { return r.getBottom() - 6 - juce::jlimit (0.0f, 1.0f, v * 0.5f + 0.5f) * (r.getHeight() - 12); };
            juce::Path sp;
            for (int i = 0; i <= 64; ++i) { const float ph = (float) i / 64.0f; const float x = x0 + ph * w, y = yOf (ProProcessor::lfoValue (shape, ph, (juce::uint32) lfoIndex * 977u)); if (i == 0) sp.startNewSubPath (x, y); else sp.lineTo (x, y); }
            const auto ink = progl::on (C().theme) ? ghosty::themes::readableOn (pt::themed (colour), pt::glassScreenBottom(), 3.0) : colour;    // GLASS: deepened on the light screen
            neon (g, sp, ink.withAlpha (0.85f), 1.6f);
        }
        if (hasKeyboardFocus (false)) { g.setColour (C().slime.withAlpha (0.8f)); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 8.0f, 1.5f); }
        g.setFont (font (9.5f)); g.setColour (progl::on (C().theme) ? ghosty::themes::readableOn (pt::themed (colour), pt::glassScreenBottom(), 4.5) : colour);
        g.drawText (lfoShapeNames()[(int) paramValue (*proc, pfx + "shape")].toUpperCase() + "   " + text (*proc, paramValue (*proc, pfx + "sync") > 0.5f ? pfx + "div" : pfx + "rate"),
                    getLocalBounds().reduced (8, 4).removeFromTop (12), juce::Justification::centredLeft);
    }
private:
    ParamDrag drag;
};

// ================================================================== on-screen keys
// A keyboard whose notes go through the FLOW strip's HOLD latch and follow its OCT - / + (GhostyPro/ProKeysExtras.h): the PLAY
// row's HOLD and OCT work for the keys and the FLOW strip alike.
class StudioKeys : public juce::Component, private juce::Timer, private juce::MidiKeyboardState::Listener
{
public:
    explicit StudioKeys (ProProcessor& p) : proc (p)
    {
        local.addListener (this);
        addAndMakeVisible (keys);
        keys.setAvailableRange (36, 96); keys.setLowestVisibleKey (36);
        keys.setScrollButtonsVisible (false); keys.setMidiChannel (1);
        keys.setWantsKeyboardFocus (false);
        keys.setTitle ("Keys");
        keys.setOctaveForMiddleC (4);      // middle C = C4 (note 60), as in most hosts
        startTimerHz (20);
    }
    ~StudioKeys() override { local.removeListener (this); }
    void attach (keysx::NoteLatch& l, keysx::StripOctave& o) { latch = &l; oct = &o; resized(); }
    void resized() override
    {
        keys.setBounds (getLocalBounds());
        keys.setKeyWidth (juce::jmax (8.0f, (float) getWidth() / 36.0f));     // five octaves + C from C2 (OCT moves them)
        showRange();
    }
    int lowest() const { return 36 + 12 * (oct != nullptr ? oct->value : 0); }
    void showRange() { keys.setAvailableRange (lowest(), lowest() + 60); keys.setLowestVisibleKey (lowest()); }
    void lookAndFeelChanged() override { applyColours(); }
    void parentHierarchyChanged() override { applyColours(); }
    void applyColours()
    {
        // dark looks: dark keys (colour-graded at the bottom, like the FLOW strip); LIGHT: light keys
        keys.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, C().dark ? C().card2 : C().card);
        keys.setColour (juce::MidiKeyboardComponent::blackNoteColourId, C().dark ? C().deep : C().white);
        keys.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, C().faint);
        keys.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, C().ecto.withAlpha (0.3f));
        keys.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, C().slime.withAlpha (0.85f));
        keys.setColour (juce::MidiKeyboardComponent::textLabelColourId, C().dim);
        keys.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
        keys.repaint();
    }

private:
    struct Keys : public juce::MidiKeyboardComponent
    {
        Keys (juce::MidiKeyboardState& s, StudioKeys& o) : juce::MidiKeyboardComponent (s, juce::MidiKeyboardComponent::horizontalKeyboard), owner (o) {}
        StudioKeys& owner;
        void drawWhiteNote (int n, juce::Graphics& g, juce::Rectangle<float> area, bool down, bool over, juce::Colour line, juce::Colour text) override
        {
            if (pghost::on()) { pghost::whiteKey (g, area, down, over); juce::MidiKeyboardComponent::drawWhiteNote (n, g, area, false, false, juce::Colours::transparentBlack, text); tint (n, g, area.reduced (1.5f, 0.0f)); return; }   // GHOST LOOK: glass keys (the label on top)
            juce::MidiKeyboardComponent::drawWhiteNote (n, g, area, down, over, line, text);
            tint (n, g, area.reduced (1.5f, 0.0f));
        }
        void drawBlackNote (int n, juce::Graphics& g, juce::Rectangle<float> area, bool down, bool over, juce::Colour fill) override
        {
            if (pghost::on()) { pghost::blackKey (g, area, down, over); tint (n, g, area.reduced (1.0f)); return; }    // GHOST LOOK: glossy indigo glass
            juce::MidiKeyboardComponent::drawBlackNote (n, g, area, down, over, fill);
            tint (n, g, area.reduced (1.0f));
        }
        void tint (int n, juce::Graphics& g, juce::Rectangle<float> a)
        {   // a colour grade per key (dark looks), and a glowing cap while the note sounds (host MIDI, the arp, the FLOW strip)
            const auto c = juce::Colour::fromHSV (0.42f + (float) ((n - 36) % 36) / 36.0f * 0.38f, 0.85f, 1.0f, 1.0f);
            if (C().dark) for (int k = 0; k < 4; ++k) { g.setColour (c.withAlpha (0.035f * (float) (k + 1))); g.fillRect (a.withTrimmedTop (a.getHeight() * (0.45f + 0.13f * (float) k))); }
            if (owner.proc.isNoteSounding (n) || (owner.latch != nullptr && owner.latch->isHeld (n))) { g.setColour (c.withAlpha (0.55f)); g.fillRect (a.withTrimmedTop (a.getHeight() * 0.35f)); g.setColour (c); g.fillRect (a.withHeight (3.0f)); }
        }
    };
    ProProcessor& proc;
    juce::MidiKeyboardState local;          // the keys' own state: notes go on through the latch
    Keys keys { local, *this };
    keysx::NoteLatch* latch = nullptr; keysx::StripOctave* oct = nullptr;
    bool sounding[128] {};
    int lastLowest = 36;
    void handleNoteOn (juce::MidiKeyboardState*, int ch, int n, float v) override
    { sounding[n] = latch != nullptr ? latch->press (n, v, ch) : (proc.keyboardState.noteOn (ch, n, v), true); }
    void handleNoteOff (juce::MidiKeyboardState*, int ch, int n, float) override
    { if (latch != nullptr) latch->release (n, sounding[n], ch); else proc.keyboardState.noteOff (ch, n, 0.0f); sounding[n] = false; }
    std::array<bool, 128> shown {};
    void timerCallback() override
    {   // repaint only when what sounds changed
        bool changed = false;
        if (lowest() != lastLowest) { lastLowest = lowest(); showRange(); changed = true; }
        for (int n = 12; n <= 120; ++n) { const bool s = proc.isNoteSounding (n) || (latch != nullptr && latch->isHeld (n)); if (s != shown[(size_t) n]) { shown[(size_t) n] = s; changed = true; } }
        if (changed) keys.repaint();
    }
};

// ================================================================== the STUDIO front page
// Everything a front page of this kind shows, at once: OSC 1 + OSC 2 (each with its display), SUB / NOISE / OSC MOD, FILTER with its
// curve, the ENVELOPES (AMP, FILTER, ENV 2, ENV 3) as one draggable curve, LFO 1 + LFO 2, the XY ghost pad, MORPH + SNAPSHOTS,
// GHOST FX / OUTPUT (+ the rack), VOICE and the MACROS. Source tiles (ENV / LFO / MACRO) drag onto any knob.
class StudioBody : public Page, private juce::Timer
{
public:
    static constexpr int LW = 470, CX = 482, CW = 452, RX = 946, RW = 442;     // the three columns (body width 1388)
    std::function<void (const juce::String&)> onMessage;
    std::function<void (int)> onTab;                    // open a page (MOD 1, FX 2, MORPH 6 ...)
    std::function<void()> onMorphToggle;
    std::function<void (int)> onSnapStep;
    std::function<void()> onSnapList;
    std::function<juce::String()> snapName;

    explicit StudioBody (ProProcessor& p) : Page (p), wave { StudioWave (p), StudioWave (p) }, palette { WavePalette (p, 0), WavePalette (p, 1) }, filterCurve (p), envCurve (p)
    {
        for (int o = 0; o < 2; ++o)
        {
            const juce::String pfx = o == 0 ? "o1_" : "o2_";
            auto& w = wave[o]; w.osc = o; addAndMakeVisible (w);
            w.onMessage = [this] (const juce::String& m) { say (m); };
            addAndMakeVisible (palette[o]);
            palette[o].onHover = [this, o] (int t) { wave[o].preview = t; wave[o].repaint(); };
            palette[o].onMessage = [this] (const juce::String& m) { say (m); };
            tbl[o] = combo (pfx + "tbl"); tbl[o]->setTooltip ("OSC " + juce::String (o + 1) + " table");
            shapeA[o] = combo ("waveA"); shapeB[o] = combo ("waveB");
            shapeA[o]->setTooltip ("GHOST A/B: the shape of frame A (POS 0 %). Picking one switches this oscillator to GHOST A/B: POS then morphs A -> B");
            shapeB[o]->setTooltip ("GHOST A/B: the shape of frame B (POS 100 %). Picking one switches this oscillator to GHOST A/B: POS then morphs A -> B");
            for (auto* c : { shapeA[o], shapeB[o] }) c->addMouseListener (this, false);
            for (auto* b : { &drawA[o], &drawB[o], &dice[o], &importBtn[o], &oscMore[o], &editBtn[o] }) addAndMakeVisible (b);
            editBtn[o].setTooltip ("WAVETABLE EDITOR for OSC " + juce::String (o + 1) + ": frames, drawing, harmonic + phase bars, morph, generate, save to the user bank (its own window)");
            editBtn[o].onClick = [this, o] { openEditor (o); };
            drawA[o].setToggleState (true, juce::dontSendNotification);
            drawA[o].onClick = [this, o] { setDraw (o, false); };
            drawB[o].onClick = [this, o] { setDraw (o, true); };
            drawA[o].setTooltip ("Draw frame A of GHOST A/B (POS at 0 %) on this display; right-click the display for undo / smooth / stepped / mirror");
            drawB[o].setTooltip ("Draw frame B of GHOST A/B (POS at 100 %) on this display; right-click the display for undo / smooth / stepped / mirror");
            dice[o].onClick = [this, o] { wave[o].randomise(); };
            dice[o].setTooltip ("Random wave into the GHOST A/B frame this oscillator draws");
            importBtn[o].onClick = [this, o] { chooseFile (o); };
            importBtn[o].setTooltip ("Turn a sample into a wavetable for OSC " + juce::String (o + 1) + " (or drop a WAV on its display)");
            for (auto& [id, cap] : std::initializer_list<std::pair<const char*, const char*>> { { "pos", "POS" }, { "lvl", "LEVEL" }, { "oct", "OCT" }, { "semi", "SEMI" }, { "fine", "FINE" },
                                                                                                 { "uni", "UNISON" }, { "det", "DETUNE" }, { "spr", "SPREAD" }, { "bln", "BLEND" } })
                oscRow[o].push_back (knob (pfx + id, cap));
            onBtn[o] = toggle (pfx + "on", "ON");
            onBtn[o]->setTooltip ("OSC " + juce::String (o + 1) + " on / off");
            oscMore[o].setTooltip ("More for this oscillator: UNISON (detune mode, stack, law), two WARP slots, SPECTRAL and the ENGINE row (HARMONICS / TIMBRE / MORPH, SOURCE, TEXTURE / RESONATOR settings)");
            oscMore[o].onClick = [this, o, pfx] { MorePanel::show (&oscMore[o], proc.apvts, oscMoreTitle (proc, pfx), oscMoreItems (proc, pfx)); };   // (= CLASSIC's, ENGINE row included)
        }
        subK = knob ("sub_lvl", "SUB"); nzK = knob ("nz_lvl", "NOISE"); nzColK = knob ("nz_col", "COLOUR"); xamtK = knob ("xamt", "AMOUNT");
        subOct = combo ("sub_oct"); subShp = combo ("sub_shp"); xmodBox = combo ("xmod");
        xmodBox->setTooltip ("How OSC 1 bends OSC 2: FM, hard Sync or Ring");
        subOct->setTooltip ("SUB octave: one or two octaves under the note"); subShp->setTooltip ("SUB shape: sine, triangle or square");
        addAndMakeVisible (nzMore);
        nzMore.setTooltip ("More noise settings: TYPE and STEREO (with a type other than Classic the COLOUR knob is a FILTER)");
        nzMore.onClick = [this] { MorePanel::show (&nzMore, proc.apvts, "NOISE", { { "nz_type", "TYPE", MorePanel::Item::combo }, { "nz_stereo", "STEREO", MorePanel::Item::knob } }); };

        addAndMakeVisible (filterCurve);
        fType = combo ("f_type"); fType->setTooltip ("FILTER type (the curve shows its shape)");
        addAndMakeVisible (filtMore);
        filtMore.setTooltip ("More filter settings: DRIVE law and CLEAN MODE");
        filtMore.onClick = [this] { MorePanel::show (&filtMore, proc.apvts, "FILTER DRIVE", { { "f_dlaw", "DRIVE LAW", MorePanel::Item::combo }, { "f_clean", "CLEAN MODE", MorePanel::Item::toggle } }); };
        for (auto& [id, cap] : std::initializer_list<std::pair<const char*, const char*>> { { "cutoff", "CUTOFF" }, { "res", "RES" }, { "f_drive", "DRIVE" }, { "fenv", "ENV" }, { "f_key", "KEY" }, { "vel_f", "VEL" } })
            filtRow.push_back (knob (id, cap));

        addAndMakeVisible (envCurve);
        for (int e = 0; e < 4; ++e)
        {
            const auto ids = EnvCurve::idsFor (e);
            const char* caps[] { "ATTACK", "DECAY", "SUSTAIN", "RELEASE" };
            for (int k = 0; k < 4; ++k) envRow[e].push_back (knob (ids[k], caps[k]));
        }
        curveK = knob ("curve", "CURVE");
        curveK->slider.setTooltip ("Envelope Curve (every envelope: straight .. strongly exponential decay / release)");

        for (int k = 0; k < 2; ++k)
        {
            const juce::String pfx = "l" + juce::String (k + 1) + "_";
            lfo[k].rate = knob (pfx + "rate", "RATE"); lfo[k].fade = knob (pfx + "fade", "FADE IN");
            lfo[k].shape = combo (pfx + "shape"); lfo[k].div = combo (pfx + "div");
            lfo[k].sync = toggle (pfx + "sync", "SYNC"); lfo[k].trig = toggle (pfx + "trig", "RETRIG");
            lfo[k].shape->setTooltip ("LFO " + juce::String (k + 1) + " shape"); lfo[k].div->setTooltip ("LFO " + juce::String (k + 1) + " division (with SYNC)");
            lfo[k].sync->setTooltip ("SYNC: the rate follows the host tempo (the division box)");
            lfo[k].trig->setTooltip ("On: restarts with every note. Off: one free-running LFO for all voices");
            auto& v = lfoView[k]; addAndMakeVisible (v);
            v.colour = k == 0 ? C().slime : C().gold; v.look = k == 0 ? 5 : 12;
            const int src = k == 0 ? srcLfo1 : srcLfo2;
            v.source = [this, src] { return proc.srcLive[src].load(); };
            v.proc = &proc; v.lfoIndex = k;
        }

        buildPad();
        const char* padNames[] { "OSC 1", "OSC 2", "BOTH" };
        for (int k = 0; k < 3; ++k)
        {
            auto& b = padOsc[k]; b.setButtonText (padNames[k]); addAndMakeVisible (b);
            b.setRadioGroupId (0x7a11); b.setClickingTogglesState (true);
            b.setTooltip (juce::String ("The MORPH ghost moves ") + (k == 0 ? "OSC 1's" : k == 1 ? "OSC 2's" : "both oscillators'") + " POS (x) and DETUNE (y)");
            b.onClick = [this, k] { if (padOsc[k].getToggleState() && padTarget != k) { padTarget = k; buildPad(); } };
        }
        padOsc[0].setToggleState (true, juce::dontSendNotification);
        addAndMakeVisible (autoBtn); autoBtn.setClickingTogglesState (true);
        autoBtn.setTooltip ("AUTO-MOVE: the ghosts drift slowly around where you left them (they move their knobs; they are not LFOs or envelopes). "
                            "Click a ghost (without dragging) to see which two knobs it moves");
        autoBtn.onClick = [this] { setAuto (autoBtn.getToggleState()); };

        for (auto* b : { &morphBtn, &boardBtn, &snapPrev, &snapNameBtn, &snapNext, &rackBtn, &matrixBtn }) addAndMakeVisible (b);
        morphBtn.setTooltip ("MORPH ON / OFF: the board's puck blends the ghosts' sounds (the board itself: BOARD or the MORPH tab)");
        morphBtn.onClick = [this] { if (onMorphToggle) onMorphToggle(); };
        boardBtn.setTooltip ("Open the MORPH board (place ghosts, move the puck, orbit)"); boardBtn.onClick = [this] { if (onTab) onTab (6); };
        snapPrev.setTooltip ("Previous snapshot (newer)"); snapPrev.onClick = [this] { if (onSnapStep) onSnapStep (-1); };
        snapNext.setTooltip ("Next snapshot (older)"); snapNext.onClick = [this] { if (onSnapStep) onSnapStep (1); };
        snapNameBtn.setTooltip ("Your snapshots: load, star, rename, delete (SNAPSHOT in the header saves one)"); snapNameBtn.onClick = [this] { if (onSnapList) onSnapList(); };
        rackBtn.setTooltip ("Open the Ghost FX rack (every unit, its own page)"); rackBtn.onClick = [this] { if (onTab) onTab (2); };
        matrixBtn.setTooltip ("Open the MOD page: the matrix (amounts of what you dragged onto knobs), every source, the macros"); matrixBtn.onClick = [this] { if (onTab) onTab (1); };

        for (auto& [id, cap] : std::initializer_list<std::pair<const char*, const char*>> { { "drive", "DRIVE" }, { "crush", "CRUSH" }, { "chorus", "CHORUS" }, { "phaser", "PHASER" },
                                                                                             { "echo", "ECHO" }, { "verb", "REVERB" }, { "haunt", "HAUNT" }, { "gain", "VOLUME" } })
            fxRow.push_back (knob (id, cap));
        addAndMakeVisible (fxMore);
        fxMore.setTooltip ("More reverb settings: TYPE, SIZE, DECAY, PRE-DELAY, LO CUT, HI CUT, DUCK, WIDTH");
        fxMore.onClick = [this] { MorePanel::show (&fxMore, proc.apvts, "REVERB", { { "verb_type", "TYPE", MorePanel::Item::combo }, { "verb_size", "SIZE", MorePanel::Item::knob }, { "verb_decay", "DECAY", MorePanel::Item::knob }, { "verb_pre", "PRE-DELAY", MorePanel::Item::knob },
                                                                                   { "verb_lo", "LO CUT", MorePanel::Item::knob }, { "verb_hi", "HI CUT", MorePanel::Item::knob }, { "verb_duck", "DUCK", MorePanel::Item::knob }, { "verb_width", "WIDTH", MorePanel::Item::knob } }); };

        for (auto& [id, cap] : std::initializer_list<std::pair<const char*, const char*>> { { "vel_amp", "VEL" }, { "glide", "GLIDE" }, { "poly", "VOICES" }, { "bend", "BEND" } })
            voiceRow.push_back (knob (id, cap));
        monoBtn = toggle ("mono", "MONO"); legatoBtn = toggle ("legato", "LEGATO");
        monoBtn->setTooltip ("MONO: one note at a time"); legatoBtn->setTooltip ("LEGATO (with MONO): a note played over another glides on without restarting the envelopes");
        addAndMakeVisible (ampMore);
        ampMore.setTooltip ("QUALITY: Draft = 1x, High = 2x, Ultra = 4x engine oversampling; ULTRA WHEN RENDERING; LOCK keeps the quality when a sound loads");
        ampMore.onClick = [this] { MorePanel::show (&ampMore, proc.apvts, "QUALITY", { { "quality", "QUALITY", MorePanel::Item::combo }, { "q_render", "ULTRA ON RENDER", MorePanel::Item::toggle }, { "q_lock", "LOCK", MorePanel::Item::toggle } }); };

        for (int m = 0; m < 4; ++m)
        {
            macroRow.push_back (knob ("mac" + juce::String (m + 1), "MACRO " + juce::String (m + 1)));
        }
        showEnv (0);
        startTimerHz (30);
    }

    void resized() override
    {
        cards.clear();
        // ---- left: OSC 1, OSC 2, SUB / NOISE
        for (int o = 0; o < 2; ++o)
        {
            const int y = o * 278;
            auto r = card (0, y, LW, 270, o == 0 ? "OSC 1" : "OSC 2", o == 0 ? C().pink : C().ice);
            tbl[o]->setBounds (r.getX() + 64, y + 3, 150, 20);
            editBtn[o].setBounds (tbl[o]->getRight() + 6, y + 4, 64, 18);
            onBtn[o]->setBounds (r.getRight() - 12 - 46, y + 4, 46, 18);
            oscMore[o].setBounds (onBtn[o]->getX() - 6 - 54, y + 4, 54, 18);
            wave[o].setBounds (r.getX() + 10, y + 26, LW - 20, 76);
            // tools: A / B shapes, DRAW A / B, DICE, WAV
            auto t = juce::Rectangle<int> (r.getX() + 10, y + 105, LW - 20, 24);
            shapeA[o]->setBounds (t.removeFromLeft (88)); t.removeFromLeft (4);
            shapeB[o]->setBounds (t.removeFromLeft (88)); t.removeFromLeft (8);
            drawA[o].setBounds (t.removeFromLeft (70)); t.removeFromLeft (4);
            drawB[o].setBounds (t.removeFromLeft (70)); t.removeFromLeft (8);
            importBtn[o].setBounds (t.removeFromRight (62)); t.removeFromRight (4);
            dice[o].setBounds (t);
            palette[o].setBounds (r.getX() + 10, y + 132, LW - 20, 54);       // the WAVE PALETTE: 20 tables + 13 ENGINES + the dice
            placeRow (oscRow[o], juce::Rectangle<int> (r.getX() + 6, y + 188, LW - 12, 80));
        }
        {
            const int y = 556;
            auto r = card (0, y, LW, getHeight() - y, "SUB  /  NOISE  /  OSC MOD", C().gold);
            auto a = knobArea (r);
            const int kw = 58;
            subK->setBounds (a.getX(), a.getY(), kw, a.getHeight());
            subOct->setBounds (a.getX() + kw + 4, a.getY() + 10, 84, 24); subShp->setBounds (a.getX() + kw + 4, a.getY() + 42, 84, 24);
            nzK->setBounds (a.getX() + kw + 94, a.getY(), kw, a.getHeight());
            nzColK->setBounds (a.getX() + 2 * kw + 94, a.getY(), kw, a.getHeight());
            xmodBox->setBounds (a.getX() + 3 * kw + 106, a.getY() + 26, 96, 26);
            xamtK->setBounds (a.getX() + 3 * kw + 206, a.getY(), kw, a.getHeight());
            nzMore.setBounds (r.getRight() - 12 - 54, y + 4, 54, 18);
        }
        // ---- centre: FILTER, ENVELOPES, LFOs
        {
            auto f = card (CX, 0, CW, 228, "FILTER", C().ice);
            fType->setBounds (f.getRight() - 12 - 108, 3, 108, 20);
            filtMore.setBounds (fType->getX() - 6 - 54, 3, 54, 20);
            filterCurve.setBounds (CX + 10, 26, CW - 20, 106);
            placeRow (filtRow, juce::Rectangle<int> (CX + 6, 136, CW - 12, 88));
        }
        {
            const int y = 236;
            auto e = card (CX, y, CW, 224, "ENVELOPES", C().slime);
            int tx = CX + 98;
            for (int k = 0; k < 4; ++k) { if (envTiles[k] != nullptr) envTiles[k]->setBounds (tx, y + 3, 66, 20); tx += 70; }
            envCurve.setBounds (CX + 10, y + 26, CW - 20, 104);
            for (int k = 0; k < 4; ++k) placeRow ({ envRow[k][0], envRow[k][1], envRow[k][2], envRow[k][3], curveK }, juce::Rectangle<int> (CX + 6, y + 132, CW - 12, 88));
            juce::ignoreUnused (e);
        }
        {
            const int y = 468;
            card (CX, y, CW, getHeight() - y, "LFO 1  /  LFO 2", C().gold);
            for (int k = 0; k < 2; ++k)
            {
                const int x = CX + 10 + k * ((CW - 20) / 2 + 0), hw = (CW - 20) / 2 - (k == 0 ? 6 : 0);
                const int xx = k == 0 ? x : x + 6;
                if (lfoTiles[k] != nullptr) lfoTiles[k]->setBounds (k == 0 ? CX + 128 : CX + 128 + 70, y + 3, 66, 20);
                lfoView[k].setBounds (xx, y + 26, hw, 66);
                lfo[k].shape->setBounds (xx, y + 96, hw / 2 - 2, 22); lfo[k].div->setBounds (xx + hw / 2 + 2, y + 96, hw / 2 - 2, 22);
                lfo[k].sync->setBounds (xx, y + 128, 68, 24); lfo[k].trig->setBounds (xx, y + 158, 68, 24);
                const int kw = (hw - 72) / 2;
                lfo[k].rate->setBounds (xx + 72, y + 122, kw, getHeight() - y - 126);
                lfo[k].fade->setBounds (xx + 72 + kw, y + 122, kw, getHeight() - y - 126);
            }
        }
        // ---- right: XY pad, MORPH / SNAPSHOTS, GHOST FX, VOICE, MACROS
        pad->setBounds (RX, 0, RW, 216);
        for (int k = 0; k < 3; ++k) padOsc[k].setBounds (RX + 8 + k * 52, 6, 50, 20);
        autoBtn.setBounds (RX + 8 + 3 * 52 + 8, 6, 58, 20);
        {
            const int y = 224;
            morphCard = card (RX, y, RW, 40, "", C().pink);
            auto s = morphCard.reduced (8, 7);
            morphBtn.setBounds (s.removeFromLeft (92)); s.removeFromLeft (4);
            boardBtn.setBounds (s.removeFromLeft (64)); s.removeFromLeft (14);
            snapPrev.setBounds (s.removeFromLeft (24)); s.removeFromLeft (2);
            snapNext.setBounds (s.removeFromRight (24)); s.removeFromRight (2);
            snapNameBtn.setBounds (s);
        }
        {
            const int y = 272;
            auto x = card (RX, y, RW, 176, "GHOST FX  /  OUTPUT", C().ecto);
            fxMore.setBounds (x.getRight() - 12 - 54, y + 4, 54, 18);
            rackBtn.setBounds (fxMore.getX() - 6 - 78, y + 4, 78, 18);
            placeRow ({ fxRow[0], fxRow[1], fxRow[2], fxRow[3] }, juce::Rectangle<int> (RX + 6, y + 24, RW - 12, 74));
            placeRow ({ fxRow[4], fxRow[5], fxRow[6], fxRow[7] }, juce::Rectangle<int> (RX + 6, y + 98, RW - 12, 74));
        }
        {
            const int y = 456;
            auto v = card (RX, y, RW, 100, "VOICE", C().slime);
            legatoBtn->setBounds (v.getRight() - 12 - 64, y + 4, 64, 18); monoBtn->setBounds (legatoBtn->getX() - 6 - 54, y + 4, 54, 18);
            ampMore.setBounds (monoBtn->getX() - 6 - 54, y + 4, 54, 18);
            placeRow (voiceRow, knobArea (v));
        }
        {
            const int y = 564;
            auto m = card (RX, y, RW, getHeight() - y, "MACROS", C().pink);
            int tx = RX + 74;
            for (int k = 0; k < 4; ++k) { if (macroTiles[k] != nullptr) macroTiles[k]->setBounds (tx, y + 3, 40, 20); tx += 44; }
            matrixBtn.setBounds (m.getRight() - 12 - 84, y + 4, 84, 18);
            placeRow (macroRow, knobArea (m));
        }
    }

    void paint (juce::Graphics& g) override
    {
        Page::paint (g);
        // the MORPH / SNAPSHOTS strip: a small ghost + the morph state
        const bool on = proc.morph.enabled.load();
        drawGhost (g, lookFor (9), (float) snapPrev.getX() - 8, (float) morphCard.getCentreY(), 14, 0.0f, false, on ? 1.0f : 0.5f);
    }

    // a click on an A/B shape box while the oscillator plays another table: GHOST A/B with A = shape A, B = shape B (as CLASSIC)
    void mouseUp (const juce::MouseEvent& e) override
    {
        for (int k = 0; k < 4; ++k) if (e.eventComponent == envTiles[k] && e.getDistanceFromDragStart() < 4 && ! e.mods.isPopupMenu()) { showEnv (k); return; }
        if (pad != nullptr && e.eventComponent == pad.get() && e.getDistanceFromDragStart() < 4 && ! e.mods.isPopupMenu()) ghostClicked (e.position);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (pad != nullptr && e.eventComponent == pad.get()) return;
        for (int o = 0; o < 2; ++o)
        {
            if ((e.eventComponent != shapeA[o] && e.eventComponent != shapeB[o]) || wave[o].ghostMode()) continue;
            proc.frameA = ghosty::makeShape ((int) paramValue (proc, "waveA")); proc.frameB = ghosty::makeShape ((int) paramValue (proc, "waveB"));
            ghosty::alignFundamental (proc.frameB, proc.frameA);
            proc.setParam (wave[o].tblId(), (float) tGhost); proc.rebuildWaves(); wave[o].repaint();
            say ("OSC " + juce::String (o + 1) + " now plays GHOST A/B: " + shapeA[o]->getText() + " -> " + shapeB[o]->getText() + "  (POS morphs A -> B)");
        }
    }

    const juce::OwnedArray<GhostKnob>& allKnobs() const { return knobs; }
    GhostKnob* knobFor (const juce::String& id) const { for (auto* k : knobs) if (k->isVisible() && k->slider.getProperties()["paramId"].toString() == id) return k; return nullptr; }

    void showEnv (int e)
    {
        shownEnv = e; envCurve.env = e;
        for (int k = 0; k < 4; ++k) for (auto* kn : envRow[k]) kn->setVisible (k == e);
        envCurve.repaint(); repaint();
    }
    int envShown() const { return shownEnv; }
    juce::Component* envDisplay() { return &envCurve; }
    juce::Component* filterDisplay() { return &filterCurve; }
    std::vector<juce::Component*> tiles() const { std::vector<juce::Component*> v; for (auto& t : tileList) v.push_back (t.get()); return v; }

    // MODULATION is ProModUI's (the same tiles, rings and knob menu as CLASSIC's MOD page): its tiles placed in STUDIO's cards
    // (the ENVELOPES tiles also pick the envelope shown when clicked)
    void attachMod (gpro::ModUi* m)
    {
        if (m == nullptr || ! tileList.empty()) return;
        namespace md = gpro::mod;
        const int envSrc[] { md::env1, md::menv, md::env2, md::env3 };
        const char* envLabel[] { "AMP", "FILTER", "ENV 2", "ENV 3" };
        for (int e = 0; e < 4; ++e)
        {
            envTiles[e] = tile (*m, envSrc[e], envLabel[e]);
            if (auto* tt = dynamic_cast<juce::SettableTooltipClient*> (envTiles[e]))
                tt->setTooltip (juce::String ("Click: show ") + EnvCurve::nameFor (e) + " here.  Drag onto any knob: " + EnvCurve::nameFor (e) + " modulates it (drag its ring to set the amount)");
            envTiles[e]->addMouseListener (this, false);
        }
        lfoTiles[0] = tile (*m, md::lfo1, "LFO 1"); lfoTiles[1] = tile (*m, md::lfo2, "LFO 2");
        for (int k = 0; k < 4; ++k) macroTiles[k] = tile (*m, md::mac1 + k, "M" + juce::String (k + 1));
        resized();
    }
    void paintOverChildren (juce::Graphics& g) override
    {
        if (auto* t = envTiles[shownEnv])
        {   // the envelope shown: its tile underlined in the envelope's colour
            const auto r = t->getBounds().toFloat();
            g.setColour (EnvCurve::colourFor (shownEnv)); g.fillRoundedRectangle (r.getX() + 6, r.getBottom() + 1, r.getWidth() - 12, 2.5f, 1.2f);
        }
        // knobs lit for a moment (click a ghost: the two knobs it moves)
        const auto now = juce::Time::getMillisecondCounter();
        for (auto& [k, until] : flashes)
        {
            if (k == nullptr || now > until || ! k->isVisible()) continue;
            const auto sb = k->slider.getLookAndFeel().getSliderLayout (k->slider).sliderBounds;
            const auto r = getLocalArea (&k->slider, sb).toFloat().reduced (4.0f);
            const float rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f + 6.0f;
            g.setColour (C().gold.withAlpha (0.25f)); g.fillEllipse (r.getCentreX() - rad, r.getCentreY() - rad, rad * 2, rad * 2);
            g.setColour (C().gold); g.drawEllipse (r.getCentreX() - rad, r.getCentreY() - rad, rad * 2, rad * 2, 2.2f);
        }
    }
    void flashKnob (GhostKnob* k, int ms = 1500) { if (k != nullptr) { flashes.push_back ({ k, juce::Time::getMillisecondCounter() + (juce::uint32) ms }); getProperties().set ("flashing", flashing()); repaint(); } }
    int flashing() const { const auto now = juce::Time::getMillisecondCounter(); int n = 0; for (auto& f : flashes) n += f.first != nullptr && now <= f.second; return n; }

private:
    StudioWave wave[2];
    WavePalette palette[2];
    FilterCurve filterCurve;
    EnvCurve envCurve;
    LfoView lfoView[2];
    std::unique_ptr<XYGhostPad> pad;
    juce::TextButton padOsc[3], autoBtn { "AUTO" };
    int padTarget = 0;                       // 0 OSC 1, 1 OSC 2, 2 BOTH (the MORPH ghost)
    bool autoOn = false; float autoPhase = 0;
    struct Axis { juce::String id; float anchor = 0, written = -1; };
    std::vector<Axis> autoAxes;
    float bothLast[2] { -1, -1 };
    juce::Point<float> padDown;
    juce::StringArray padIds() const
    {
        const juce::String o = padTarget == 1 ? "o2_" : "o1_";
        return { "cutoff", "res", o + "pos", o + "det", "echo", "verb", "drive", "sub_lvl" };
    }
    void buildPad()
    {   // (re)made when the OSC switch changes; the ghosts' colours are the BLACK palette's, themed like every stored colour
        const bool wasAuto = autoOn; if (wasAuto) setAuto (false);
        if (pad != nullptr) { pad->removeMouseListener (this); removeChildComponent (pad.get()); }
        const auto bounds = pad != nullptr ? pad->getBounds() : juce::Rectangle<int>();
        pad = std::make_unique<XYGhostPad>();
        const auto& d = pt::darkPal(); const auto ids = padIds();
        auto P = [this] (const juce::String& id) { return proc.apvts.getParameter (id); };
        pad->addGhost ("TONE",  d.ice,  P (ids[0]), P (ids[1]), 4);
        pad->addGhost (padTarget == 2 ? "MORPH 1+2" : padTarget == 1 ? "MORPH 2" : "MORPH", d.pink, P (ids[2]), P (ids[3]), 9);
        pad->addGhost ("SPACE", d.ecto, P (ids[4]), P (ids[5]), 17);
        pad->addGhost ("HEAT",  d.gold, P (ids[6]), P (ids[7]), 22);
        pad->levelSource = [this] { return proc.level.load(); };
        pad->ghostPainter = proghost::padPainter(); pad->painterDrawsNames = true;     // GHOST ART (round 3): the same characters as CLASSIC's pad
        progl::themePad (*pad);      // GLASS (round 3): the pad backdrop follows the theme
        addAndMakeVisible (*pad); pad->toBack();
        pad->addMouseListener (this, false);
        pad->setBounds (bounds);
        bothLast[0] = bothLast[1] = -1;
        if (wasAuto) setAuto (true);
    }
    void setAuto (bool on)
    {
        if (on == autoOn) return;
        autoOn = on;
        if (on) { autoAxes.clear(); for (auto& id : padIds()) { auto* p = proc.apvts.getParameter (id); autoAxes.push_back ({ id, p->getValue(), -1 }); p->beginChangeGesture(); } }
        else { for (auto& a : autoAxes) if (auto* p = proc.apvts.getParameter (a.id)) p->endChangeGesture(); autoAxes.clear(); }
        autoBtn.setToggleState (on, juce::dontSendNotification);
    }
    void stepAuto()
    {   // a slow Lissajous per ghost around its anchor; a hand on the ghost (the value moved by someone else) re-anchors it
        autoPhase += 1.0f / 30.0f;
        for (size_t i = 0; i < autoAxes.size(); ++i)
        {
            auto& a = autoAxes[i]; auto* p = proc.apvts.getParameter (a.id);
            if (p == nullptr) continue;
            if (a.written >= 0 && std::abs (p->getValue() - a.written) > 1.0e-4f) a.anchor = p->getValue();
            const float rate = 0.11f + 0.037f * (float) (i / 2), ph = (i % 2 == 0) ? 0.0f : 1.3f + (float) i;
            const float v = juce::jlimit (0.0f, 1.0f, a.anchor + 0.07f * std::sin (juce::MathConstants<float>::twoPi * rate * autoPhase * ((i % 2 == 0) ? 1.0f : 1.5f) + ph));
            p->setValueNotifyingHost (v); a.written = p->getValue();
        }
    }
    void mirrorBoth()
    {   // BOTH: OSC 2's POS / DETUNE follow OSC 1's (message thread)
        const char* from[] { "o1_pos", "o1_det" }; const char* to[] { "o2_pos", "o2_det" };
        for (int k = 0; k < 2; ++k)
        {
            auto* a = proc.apvts.getParameter (from[k]); auto* b = proc.apvts.getParameter (to[k]);
            if (a->getValue() != bothLast[k]) { bothLast[k] = a->getValue(); if (std::abs (b->getValue() - bothLast[k]) > 1.0e-6f) b->setValueNotifyingHost (bothLast[k]); }
        }
    }
    void ghostClicked (juce::Point<float> at)
    {   // click a ghost: the two knobs it moves light up
        static const char* names[] { "TONE", "MORPH", "SPACE", "HEAT" };
        const auto ids = padIds(); const float m = 30.0f; int best = -1; float bd = 40.0f;
        for (int g = 0; g < 4; ++g)
        {
            auto* px = proc.apvts.getParameter (ids[g * 2]); auto* py = proc.apvts.getParameter (ids[g * 2 + 1]);
            const juce::Point<float> gp (m + px->getValue() * ((float) pad->getWidth() - 2 * m), m + (1.0f - py->getValue()) * ((float) pad->getHeight() - 2 * m));
            if (const float dd = gp.getDistanceFrom (at); dd < bd) { bd = dd; best = g; }
        }
        if (best < 0) return;
        auto* px = proc.apvts.getParameter (ids[best * 2]); auto* py = proc.apvts.getParameter (ids[best * 2 + 1]);
        flashKnob (knobFor (ids[best * 2])); flashKnob (knobFor (ids[best * 2 + 1]));
        say (juce::String (names[best]) + " ghost:  x = " + px->getName (30).toUpperCase() + ",  y = " + py->getName (30).toUpperCase() + "  (lit; drag the ghost to move both)");
    }
    std::vector<std::unique_ptr<juce::Component>> tileList;
    juce::Component* envTiles[4] {}; juce::Component* lfoTiles[2] {}; juce::Component* macroTiles[4] {};
    std::vector<std::pair<juce::Component::SafePointer<GhostKnob>, juce::uint32>> flashes;
    juce::TextButton drawA[2] { juce::TextButton ("DRAW A"), juce::TextButton ("DRAW A") }, drawB[2] { juce::TextButton ("DRAW B"), juce::TextButton ("DRAW B") };
    juce::TextButton dice[2] { juce::TextButton ("DICE"), juce::TextButton ("DICE") }, importBtn[2] { juce::TextButton ("WAV"), juce::TextButton ("WAV") };
    juce::TextButton oscMore[2] { juce::TextButton ("MORE"), juce::TextButton ("MORE") };
    juce::TextButton editBtn[2] { juce::TextButton ("EDITOR"), juce::TextButton ("EDITOR") };
    juce::TextButton nzMore { "MORE" }, filtMore { "MORE" }, fxMore { "MORE" }, ampMore { "MORE" };
    juce::TextButton morphBtn { "MORPH OFF" }, boardBtn { "BOARD" }, snapPrev { "<" }, snapNameBtn { "no snapshots" }, snapNext { ">" }, rackBtn { "FX RACK" }, matrixBtn { "MOD MATRIX" };
    juce::ComboBox* tbl[2] {}; juce::ComboBox* shapeA[2] {}; juce::ComboBox* shapeB[2] {};
    juce::ComboBox* subOct = nullptr, *subShp = nullptr, *xmodBox = nullptr, *fType = nullptr;
    std::vector<GhostKnob*> oscRow[2], filtRow, envRow[4], fxRow, voiceRow, macroRow;
    GhostKnob* subK = nullptr, *nzK = nullptr, *nzColK = nullptr, *xamtK = nullptr, *curveK = nullptr;
    juce::TextButton* onBtn[2] {}, *monoBtn = nullptr, *legatoBtn = nullptr;
    struct Lfo { GhostKnob* rate = nullptr; GhostKnob* fade = nullptr; juce::ComboBox* shape = nullptr; juce::ComboBox* div = nullptr; juce::TextButton* sync = nullptr; juce::TextButton* trig = nullptr; } lfo[2];
    juce::Rectangle<int> morphCard;
    int shownEnv = 0, tick = 0;
    std::unique_ptr<juce::FileChooser> chooser;

    void say (const juce::String& m) { if (onMessage) onMessage (m); }
    // the WAVETABLE EDITOR (ProWavetableEditor.h) in its own window, for one oscillator (STUDIO's own: nothing from CLASSIC's page)
    struct WtWindow : public juce::DocumentWindow { using DocumentWindow::DocumentWindow; void closeButtonPressed() override { setVisible (false); } };
    std::unique_ptr<WtWindow> wtWin;
public:
    ~StudioBody() override { if (wtWin != nullptr) wtWin->setLookAndFeel (nullptr); }
    juce::DocumentWindow* editorWindow() const { return wtWin.get(); }
    void openEditor (int o)
    {
        if (const char* shot = std::getenv ("GHOSTY_WTSHOT"))
        {   // tests (no window manager under Xvfb): snapshot the editor to a PNG instead
            WavetableEditor ed (proc, o); ed.setLookAndFeel (&getLookAndFeel()); ed.setSize (WavetableEditor::W, WavetableEditor::H);
            auto img = ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.0f);
            juce::PNGImageFormat png; juce::File f (shot); f.deleteFile();
            if (auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream())) png.writeImageToStream (img, *os);
            ed.setLookAndFeel (nullptr);
            return;
        }
        if (wtWin == nullptr)
        {
            wtWin = std::make_unique<WtWindow> ("Ghosty Pro  -  WAVETABLE EDITOR", C().bg, juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton);
            wtWin->setLookAndFeel (&getLookAndFeel());
            wtWin->setUsingNativeTitleBar (false); wtWin->setResizable (false, false); wtWin->setTitleBarHeight (26);
            wtWin->setContentOwned (new WavetableEditor (proc, o), true);
            if (auto* top = getTopLevelComponent()) wtWin->centreAroundComponent (top, wtWin->getWidth(), wtWin->getHeight());
        }
        else if (auto* ed = dynamic_cast<WavetableEditor*> (wtWin->getContentComponent()); ed != nullptr && ed->oscIndex() != o)
            wtWin->setContentOwned (new WavetableEditor (proc, o), true);
        wtWin->setBackgroundColour (C().bg);
        wtWin->setVisible (true); wtWin->toFront (true);
    }
private:
    juce::Component* tile (gpro::ModUi& m, int src, const juce::String& label)
    {
        tileList.push_back (m.makeTile (src, true, label));
        addAndMakeVisible (*tileList.back());
        return tileList.back().get();
    }
    void setDraw (int o, bool b)
    {
        wave[o].editB = b; wave[o].ensureGhost();
        drawA[o].setToggleState (! b, juce::dontSendNotification); drawB[o].setToggleState (b, juce::dontSendNotification); wave[o].repaint();
    }
    void chooseFile (int o)
    {
        chooser = std::make_unique<juce::FileChooser> ("Sample -> wavetable", juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.ogg");
        juce::Component::SafePointer<StudioBody> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe, o] (const juce::FileChooser& fc)
        {
            if (safe == nullptr) return;
            auto f = fc.getResult();
            if (f.existsAsFile()) safe->wave[o].importFile (f);
        });
    }
    void timerCallback() override
    {
        for (auto& v : lfoView) v.push();
        if (autoOn) stepAuto();
        if (! flashes.empty()) { const auto now = juce::Time::getMillisecondCounter(); flashes.erase (std::remove_if (flashes.begin(), flashes.end(), [now] (const auto& f) { return f.first == nullptr || now > f.second; }), flashes.end()); repaint(); }
        if (padTarget == 2) mirrorBoth();
        if (++tick % 3 != 0) return;       // the rest at 10 Hz
        L2::wantTrace (&proc, isShowing());
        if (isShowing()) proc.traceOn.store (true);
        // greyed = no effect right now (as CLASSIC)
        const bool mono = paramValue (proc, "mono") > 0.5f;
        legatoBtn->setEnabled (mono); legatoBtn->setAlpha (mono ? 1.0f : kGreyed);
        for (int o = 0; o < 2; ++o)
        {
            const bool on = paramValue (proc, o == 0 ? "o1_on" : "o2_on") > 0.5f;
            for (auto* k : oscRow[o]) k->setAlpha (on ? 1.0f : kGreyed);
            const bool uni = paramValue (proc, o == 0 ? "o1_uni" : "o2_uni") > 1.5f;
            for (int i = 6; i < 9; ++i) oscRow[o][(size_t) i]->setAlpha (on && uni ? 1.0f : kGreyed);
        }
        xamtK->setAlpha (paramValue (proc, "xmod") > 0.5f ? 1.0f : kGreyed);
        for (int k = 0; k < 2; ++k)
        {
            const bool sync = paramValue (proc, "l" + juce::String (k + 1) + "_sync") > 0.5f;
            lfo[k].rate->setAlpha (sync ? kGreyed : 1.0f); lfo[k].rate->setEnabled (! sync);
            lfo[k].div->setAlpha (sync ? 1.0f : kGreyed); lfo[k].div->setEnabled (sync);
        }
        for (int m = 0; m < (int) macroRow.size(); ++m)
        {
            auto nm = proc.macros.name (m); if (nm.isEmpty()) nm = "MACRO " + juce::String (m + 1);
            if (macroRow[(size_t) m]->label.getText() != nm) macroRow[(size_t) m]->label.setText (nm, juce::dontSendNotification);
            macroRow[(size_t) m]->slider.setDoubleClickReturnValue (true, proc.macros.bipolar (m) ? 0.5 : 0.0);
        }
        const bool mOn = proc.morph.enabled.load();
        morphBtn.setButtonText (mOn ? "MORPH ON" : "MORPH OFF"); morphBtn.setToggleState (mOn, juce::dontSendNotification);
        if (snapName) { const auto n = snapName(); if (snapNameBtn.getButtonText() != n) snapNameBtn.setButtonText (n); }
        repaint (morphCard);
    }
};

// ================================================================== the STUDIO window (defined in ProLayout2Studio.inc)
class ProStudio : public juce::Component, private juce::Timer
{
public:
    static constexpr int W = 1420, H = 940, TY = 112, BODY_H = 668;
    ProStudio (ProContent& content, ProProcessor& p);
    ~ProStudio() override;
    bool isOn() const { return on; }
    void setOn (bool shouldBeOn);
    void layoutContent();                   // ProContent::resized() in STUDIO: the header, tabs, pages and play row at STUDIO size
    void tabShown (int tab);                // ProContent::showTab(): the front page replaces the SYNTH page
    juce::Rectangle<float> toastRect() const;
    void paint (juce::Graphics&) override;
    void resized() override;
    void showLayoutMenu (juce::Component* target);

    StudioBody body;
    StudioKeys keys;
    juce::TextButton keysBtn { "FLOW" }, layoutBtn { "STUDIO" };
    juce::Rectangle<int> bodyArea() const { return bodyRect; }
    // AMBIENT: a gentle neon rain behind the panels (settings file, default on; off by itself while hidden or when the
    // computer can't keep up). Fixed pool, no allocation while painting, at the window's 24 fps.
    void setAmbient (bool shouldRain);
    bool ambientRaining() const { return ambient && ! ambientTooSlow; }

private:
    ProContent& c;
    ProProcessor& proc;
    bool on = false, flowMode = false;
    juce::Rectangle<int> bodyRect, playCard;
    struct Drop { float x = 0, y = -1000, vy = 0, len = 0, a = 0; int col = 0; };
    std::array<Drop, 26> rain {};
    std::vector<juce::Rectangle<int>> covered;      // opaque panels: a drop behind one needs no repaint
    juce::Random rnd { 0x9a1 };
    bool ambient = true, ambientTooSlow = false;
    double lastTickMs = 0; int slowTicks = 0;
    void stepRain();
    void paintRain (juce::Graphics&);
    void timerCallback() override;
};
} // namespace studio2
using studio2::ProStudio;
