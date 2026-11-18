#include "PluginEditor.h"
#include "WaveRegion.h"

using namespace gsui;
static constexpr int W = 1040, H = 744;
// every panel is opaque (it fills the surface under its rounded corners first: the page or the card it sits on)
static void paintBase(juce::Graphics& g, juce::Component& c) { g.fillAll((bool) c.getProperties()["onCard"] ? CARD : BG); }
static juce::String versionText() {
#ifdef JucePlugin_VersionString
    return "v1 beta";   // (CMake VERSION -> the plugin's version define)
#else
    return "v-dev";
#endif
}

// ---------------------------------------------------------------------------------------------------------
// look and feel (reads the palette at paint time; a forceDark instance keeps the BLACK look in any theme). One opaque, token-driven
// look for all three themes (BLACK / PURPLE / LIGHT).
LookAndFeel::LookAndFeel(bool alwaysDark) : forceDark(alwaysDark) { applyColours(); }
void LookAndFeel::applyColours() {
    const auto p = pal();
    setColourScheme(getDarkColourScheme());   // (resets every id; ours follow)
    setColour(juce::Label::textColourId, p.ink);
    setColour(juce::TextEditor::backgroundColourId, p.card2);
    setColour(juce::TextEditor::textColourId, p.ink);
    setColour(juce::TextEditor::outlineColourId, p.purple);
    setColour(juce::TextEditor::focusedOutlineColourId, p.purple);
    setColour(juce::TextEditor::highlightColourId, p.purple.interpolatedWith(p.card2, 0.6f));
    setColour(juce::TextEditor::highlightedTextColourId, p.ink);
    setColour(juce::CaretComponent::caretColourId, p.green);
    setColour(juce::ComboBox::backgroundColourId, p.card2);
    setColour(juce::ComboBox::textColourId, p.ink);
    setColour(juce::ComboBox::arrowColourId, p.purple);
    setColour(juce::PopupMenu::backgroundColourId, p.card2);
    setColour(juce::PopupMenu::textColourId, p.ink);
    setColour(juce::PopupMenu::headerTextColourId, p.grey);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, p.purple);
    setColour(juce::PopupMenu::highlightedTextColourId, p.onAccent);
    setColour(juce::TextButton::textColourOffId, p.ink);
    setColour(juce::TextButton::textColourOnId, p.onAccent);
    setColour(juce::Slider::textBoxTextColourId, p.ink);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::ToggleButton::textColourId, p.ink);
    setColour(juce::TooltipWindow::backgroundColourId, p.card2);
    setColour(juce::TooltipWindow::textColourId, p.ink);
    setColour(juce::TooltipWindow::outlineColourId, p.purple);
    setColour(juce::BubbleComponent::backgroundColourId, p.card2);
    setColour(juce::BubbleComponent::outlineColourId, p.purple);
    setColour(juce::AlertWindow::backgroundColourId, p.card);
    setColour(juce::AlertWindow::textColourId, p.ink);
    setColour(juce::AlertWindow::outlineColourId, p.purple);
    setColour(juce::ResizableWindow::backgroundColourId, p.bg);
    setColour(juce::ScrollBar::thumbColourId, p.purple);
    setColour(juce::ListBox::backgroundColourId, p.card2);
    setColour(juce::ListBox::textColourId, p.ink);
    setColour(juce::Slider::trackColourId, p.purple);
    setColour(juce::Slider::thumbColourId, p.green);
    setColour(juce::Slider::backgroundColourId, p.line);
    setColour(juce::Slider::rotarySliderFillColourId, p.purple);
    setColour(juce::Label::textWhenEditingColourId, p.ink);
    setColour(juce::Label::backgroundWhenEditingColourId, p.card2);
}
void LookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int w, int h) {
    const auto p = pal();
    g.fillAll(p.card2);
    g.setColour(p.purple); g.drawRect(0, 0, w, h, 1);
}
void LookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) {
    const auto p = pal();
    const bool on = b.getToggleState() || (bool) b.getProperties()["accent"];
    g.setFont(getTextButtonFont(b, b.getHeight()));
    auto c = b.findColour(on ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId);
    if (!b.isColourSpecified(juce::TextButton::textColourOffId) && !b.isColourSpecified(juce::TextButton::textColourOnId)) {
        c = on ? p.onAccent : p.ink;
        if (on && b.getToggleState() && b.isColourSpecified(juce::TextButton::buttonOnColourId))   // a button with its own "on" colour: the text that reads on it
            c = ghosty::themes::contrast(p.onAccent, b.findColour(juce::TextButton::buttonOnColourId)) >= ghosty::themes::contrast(p.ink, b.findColour(juce::TextButton::buttonOnColourId)) ? p.onAccent : p.ink;
    }
    if (!b.isEnabled()) c = p.grey;   // (disabled: the secondary text colour, still readable)
    g.setColour(c);
    g.drawFittedText(b.getButtonText(), b.getLocalBounds().reduced(4, 2), juce::Justification::centred, 2, 0.8f);
}

void LookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) {
    const auto p = pal();
    auto r = juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h).reduced(4);
    const float size = juce::jmin(r.getWidth(), r.getHeight());
    auto c = r.withSizeKeepingCentre(size, size);
    const bool big = s.getProperties()["big"];
    const float thick = big ? 7.0f : 4.0f;
    const float ang = a0 + pos * (a1 - a0);
    if (pos > 0.01f) {
        g.setColour(p.purple.withAlpha((0.10f + 0.18f * pos) * 0.6f));
        g.fillEllipse(c.expanded(big ? 6.0f : 3.0f));
    }
    g.setColour(p.card2);
    g.fillEllipse(c.reduced(thick + 2));
    juce::Path track; track.addCentredArc(c.getCentreX(), c.getCentreY(), size / 2 - thick / 2, size / 2 - thick / 2, 0, a0, a1, true);
    g.setColour(p.line);
    g.strokePath(track, juce::PathStrokeType(thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Path arc; arc.addCentredArc(c.getCentreX(), c.getCentreY(), size / 2 - thick / 2, size / 2 - thick / 2, 0, a0, ang, true);
    juce::ColourGradient grad(p.purple, c.getX(), c.getBottom(), p.green, c.getRight(), c.getY(), false);
    g.setGradientFill(grad);
    g.strokePath(arc, juce::PathStrokeType(thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Point<float> ctr = c.getCentre();
    auto tip = ctr.getPointOnCircumference(size / 2 - thick - 5, ang);
    g.setColour(p.ink);
    g.drawLine({ ctr.getPointOnCircumference(size * 0.12f, ang), tip }, big ? 3.0f : 2.0f);
    if (big) {   // little ghost eyes in the middle of the macro knobs
        g.setColour(p.ink);
        g.fillRoundedRectangle(ctr.x - 9, ctr.y + 4, 6, 8, 1.5f);
        g.fillRoundedRectangle(ctr.x + 3, ctr.y + 4, 6, 8, 1.5f);
    }
}

void LookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) {
    const auto p = pal();
    auto r = b.getLocalBounds().toFloat().reduced(1);
    const bool on = b.getToggleState() || (bool) b.getProperties()["accent"];
    const auto onCol = b.isColourSpecified(juce::TextButton::buttonOnColourId) ? b.findColour(juce::TextButton::buttonOnColourId) : p.purple;
    const auto hover = p.card2.interpolatedWith(p.purple, 0.18f);
    g.setColour(on ? (b.getToggleState() ? onCol : p.purple) : (over ? hover : p.card2));
    g.fillRoundedRectangle(r, r.getHeight() * 0.35f);
    g.setColour(on ? p.violet : (over ? p.purple : p.line));
    g.drawRoundedRectangle(r, r.getHeight() * 0.35f, down ? 2.0f : 1.2f);
}

void LookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool over, bool) {
    const auto p = pal();
    auto r = b.getLocalBounds().toFloat().reduced(2);
    const bool on = b.getToggleState();
    g.setColour(on ? p.purple : p.card2);
    g.fillRoundedRectangle(r, 8);
    g.setColour(on ? p.violet : (over ? p.purple : p.line));
    g.drawRoundedRectangle(r, 8, 1.2f);
    g.setColour(on ? p.onAccent : p.ink);
    g.setFont(juce::FontOptions(12.5f, juce::Font::bold));
    g.drawText(b.getButtonText(), r, juce::Justification::centred);
}

void LookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float minP, float maxP, juce::Slider::SliderStyle st, juce::Slider& s) {
    const auto p = pal();
    if (!(bool) s.getProperties()["bipolar"]) { juce::LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, pos, minP, maxP, st, s); return; }
    // a mod amount: the fill grows from the centre (0) to the left (negative) or right (positive)
    auto r = juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h);
    const float cy = r.getCentreY(), cx = r.getCentreX();
    g.setColour(p.line); g.fillRoundedRectangle(r.getX(), cy - 3, r.getWidth(), 6, 3);
    g.setColour(p.grey); g.fillRect(cx - 0.5f, cy - 6, 1.0f, 12.0f);
    g.setColour(pos >= cx ? p.green : p.red); g.fillRoundedRectangle(juce::jmin(cx, pos), cy - 3, std::abs(pos - cx), 6, 3);
    g.setColour(p.ink); g.fillEllipse(juce::Rectangle<float>(10, 10).withCentre({ pos, cy }));
}

void LookAndFeel::drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&) {
    const auto p = pal();
    auto r = juce::Rectangle<float>(0, 0, (float) w, (float) h).reduced(1);
    g.setColour(p.card2); g.fillRoundedRectangle(r, 8);
    g.setColour(p.line); g.drawRoundedRectangle(r, 8, 1.2f);
    juce::Path arrow; const float ax = (float) w - 14, ay = (float) h * 0.5f;
    arrow.addTriangle(ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
    g.setColour(p.purple); g.fillPath(arrow);
}

// ---------------------------------------------------------------------------------------------------------
Knob::Knob(const juce::String& name, bool b) : big(b) {
    slider = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);
    slider->getProperties().set("big", big);
    slider->setPopupDisplayEnabled(true, true, nullptr);
    slider->setMouseDragSensitivity(big ? 260 : 200);
    addAndMakeVisible(*slider);
    label.setText(name, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setFont(juce::FontOptions(big ? 15.0f : 11.0f, juce::Font::bold));
    label.setColour(juce::Label::textColourId, big ? INK : GREY);
    label.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(label);
}
void Knob::attach(juce::AudioProcessorValueTreeState& s, const juce::String& id) {
    att.reset();
    att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(s, id, *slider);
    getProperties().set("pid", id);
    slider->setDoubleClickReturnValue(true, s.getParameter(id)->convertFrom0to1(s.getParameter(id)->getDefaultValue()));
}
void Knob::resized() {
    auto r = getLocalBounds();
    label.setBounds(r.removeFromBottom(big ? 20 : 15));
    slider->setBounds(r);
}
void Knob::paintOverChildren(juce::Graphics& g) {
    if (ticks.isEmpty() || (int) tickValues.size() != ticks.size()) return;
    const auto rp = slider->getRotaryParameters();
    auto r = slider->getBounds().toFloat().reduced(4);
    const float size = juce::jmin(r.getWidth(), r.getHeight());
    const auto c = r.getCentre(); const float rad = size / 2 - 13.0f;
    g.setFont(juce::FontOptions(8.5f, juce::Font::bold));
    const double now = slider->getValue();
    for (int i = 0; i < ticks.size(); ++i) {
        const float ang = rp.startAngleRadians + (float) slider->valueToProportionOfLength(tickValues[(size_t) i]) * (rp.endAngleRadians - rp.startAngleRadians);
        const auto pt = c.getPointOnCircumference(rad, ang);
        const bool near = std::abs(slider->valueToProportionOfLength(tickValues[(size_t) i]) - slider->valueToProportionOfLength(now)) < 0.06;
        g.setColour(near ? GREEN : GREY.withAlpha(0.85f));
        g.drawText(ticks[i], juce::Rectangle<float>(10, 10).withCentre(pt), juce::Justification::centred);
    }
}

// Ratio knob snaps to musical ratios (hold shift for free movement)
struct RatioSlider : juce::Slider {
    double snapValue(double v, DragMode) override {
        if (juce::ModifierKeys::currentModifiers.isShiftDown()) return v;
        static const double R[] = { 0.25, 0.5, 0.75, 1, 1.4142, 1.5, 2, 2.5, 2.76, 3, 3.5, 4, 5, 6, 7, 7.07, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
        double best = R[0];
        for (double r : R) if (std::abs(std::log(r / v)) < std::abs(std::log(best / v))) best = r;
        return best;
    }
};

// ---------------------------------------------------------------------------------------------------------
// The ghost field: 6 FM ghosts + the NOISE ghost; drag one onto another to make it haunt (modulate) it. The SAMPLE ghost (ADD SAMPLE,
// bottom right) is the sample slot: click it to see its panel, drop a file on the plugin, drag it onto a ghost to make the sample haunt it.
static constexpr int kFieldGhosts = 7;
static constexpr int kSampleGhost = 7;           // the SAMPLE ghost: its one link = smp_fm onto ghost smp_fm_to
static constexpr int kAllGhosts = 8;
class gsui::GhostField : public juce::Component, private juce::AudioProcessorValueTreeState::Listener {
public:
    GhostField(PoltergeistProcessor& p, std::function<void(int)> onSelect) : proc(p), select(std::move(onSelect)) {
        body = juce::Drawable::parseSVGPath("M16 31 C16 13 48 13 48 31 V48 Q45 54 42 49 Q39 54 36 49 Q33 54 30 49 Q27 54 24 49 Q21 54 18 49 Q16 52 16 48 Z");
        for (auto& id : ids()) proc.apvts.addParameterListener(id, this);
    }
    ~GhostField() override { for (auto& id : ids()) proc.apvts.removeParameterListener(id, this); }
    static juce::StringArray ids() {
        juce::StringArray a;
        for (int f = 0; f < kFieldGhosts; ++f) for (int t = 0; t < gs::kOps; ++t) if (f != t) a.add(gs::modId(f, t));
        for (int o = 0; o < gs::kOps; ++o) { a.add(gs::opId(o, "level")); a.add(gs::opId(o, "fb")); }
        a.add("nz_level");
        for (auto* id : { "smp_fm", "smp_fm_to", "smp_level", "smp_wave_to" }) a.add(id);
        return a;
    }
    static bool canLink(int f, int t) { return f != t && t < gs::kOps && (f < kFieldGhosts || f == kSampleGhost); }
    // the parameter behind a link: ghost / noise -> ghost = its mod amount; SAMPLE -> its target ghost = HAUNT (smp_fm)
    static juce::String linkId(int f, int t) { return f == kSampleGhost ? juce::String("smp_fm") : gs::modId(f, t); }
    int sampleTarget() const { return juce::jlimit(0, gs::kOps - 1, (int) val("smp_fm_to")); }
    float linkVal(int f, int t) const { if (f == kSampleGhost) return t == sampleTarget() ? val("smp_fm") : 0.0f; return val(gs::modId(f, t)); }
    bool hasSample() const { return proc.rawSample != nullptr || proc.engine.getSample() != nullptr; }
    juce::String outId(int o) const { return o == gs::kNoise ? juce::String("nz_level") : gs::opId(o, "level"); }
    int selected = 0;

    void resized() override {
        // a hexagon of FM ghosts (1 at the bottom, 4 on top, as before) and the noise ghost at the right
        const float w = (float) getWidth(), h = (float) getHeight();
        static const float P[kAllGhosts][2] = { { 230, 248 }, { 82, 208 }, { 378, 208 }, { 230, 62 }, { 82, 114 }, { 378, 114 }, { 484, 160 }, { 484, 250 } };
        for (int o = 0; o < kAllGhosts; ++o) pos[o] = { P[o][0] / 540.0f * w, P[o][1] / 310.0f * h };
    }

    float val(const juce::String& id) const { return proc.apvts.getRawParameterValue(id)->load(); }

    juce::Point<float> ghostCentre(int o) const { return pos[juce::jlimit(0, kAllGhosts - 1, o)]; }
    juce::Point<float> handleFor(int f, int t) const {
        auto a = pos[f], b = pos[t];
        auto mid = (a + b) * 0.5f;
        auto dir = b - a; const float len = juce::jmax(1.0f, dir.getDistanceFromOrigin());
        juce::Point<float> perp(-dir.y / len, dir.x / len);
        return mid + perp * 22.0f;   // A->B and B->A bow to opposite sides
    }

    void paint(juce::Graphics& g) override {
        paintBase(g, *this);
        auto bounds = getLocalBounds().toFloat();
        g.setColour(CARD.withAlpha(0.85f));
        g.fillRoundedRectangle(bounds, 18);
        g.setColour(PURPLE.withAlpha(0.25f));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 18, 1);
        g.setColour(GREY);
        g.setFont(juce::FontOptions(11.0f));
        g.drawFittedText("Drag a ghost onto another to make it HAUNT it (FM). Drag a link's dot up/down for how hard. Double-click a dot to break it.",
                         bounds.reduced(16, 8).removeFromTop(16).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);

        if (const int wt = (int) val("smp_wave_to"); wt > 0 && hasSample()) {   // AS WAVE: a dashed line to the ghost that plays the sample as its wave
            const auto a = pos[kSampleGhost], b = pos[juce::jmin(gs::kOps, wt) - 1];
            juce::Path ln; ln.startNewSubPath(a); ln.lineTo(b); juce::Path d; const float dash[] { 5, 5 };
            juce::PathStrokeType(1.6f).createDashedStroke(d, ln, dash, 2); g.setColour(BLUE.withAlpha(0.8f)); g.fillPath(d);
            const auto m = (a + b) * 0.5f; auto tag = juce::Rectangle<float>(40, 14).withCentre(m);
            g.setColour(BG); g.fillRoundedRectangle(tag, 4); g.setColour(BLUE); g.drawRoundedRectangle(tag, 4, 1.2f);
            g.setFont(juce::FontOptions(9.0f, juce::Font::bold)); g.drawText("WAVE", tag, juce::Justification::centred);
        }
        for (int f = 0; f < kAllGhosts; ++f)
            for (int t = 0; t < gs::kOps; ++t) {
                if (!canLink(f, t)) continue;
                const float a = linkVal(f, t);
                if (a < 0.001f) continue;
                auto h = handleFor(f, t);
                juce::Path p; p.startNewSubPath(pos[f]); p.quadraticTo(h * 2.0f - (pos[f] + pos[t]) * 0.5f, pos[t]);
                const float pulse = 0.6f + 0.4f * std::sin((float) phase * 3.0f + (float) (f * 4 + t));
                const auto fc = colOf(f);
                g.setColour(fc.withAlpha(0.18f * pulse));
                g.strokePath(p, juce::PathStrokeType(4.0f + a * 16.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour(fc.withAlpha(0.85f));
                g.strokePath(p, juce::PathStrokeType(1.5f + a * 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour(BG); g.fillEllipse(juce::Rectangle<float>(20, 20).withCentre(h));
                g.setColour(hoverLink == f * 8 + t ? GREEN : fc);
                g.drawEllipse(juce::Rectangle<float>(20, 20).withCentre(h), 2);
                g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
                g.drawText(juce::String(juce::roundToInt(a * 100)), juce::Rectangle<float>(26, 12).withCentre(h), juce::Justification::centred);
            }
        if (dragFrom >= 0 && dragging) {
            juce::Path p; p.startNewSubPath(pos[dragFrom]); p.lineTo(dragPt);
            g.setColour(GREEN.withAlpha(0.5f));
            g.strokePath(p, juce::PathStrokeType(3, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        for (int o = 0; o < kAllGhosts; ++o) drawGhost(g, o);
    }
    static juce::Colour colOf(int o) { return o == kSampleGhost ? BLUE : GHOST_COL[o]; }

    void drawGhost(juce::Graphics& g, int o) {
        if (o == kSampleGhost && !hasSample()) { drawEmptySample(g); return; }
        const bool smpG = o == kSampleGhost;
        const float lvl = smpG ? (proc.engine.smpPlayhead.load() >= 0 ? 0.5f : 0.0f) : proc.engine.meter[(size_t) o].load();
        const float out = smpG ? (val("mute_sample") > 0.5f ? 0.0f : val("smp_level")) : val(outId(o));
        const float fb = (o == gs::kNoise || smpG) ? 0.0f : val(gs::opId(o, "fb"));
        const bool noise = o == gs::kNoise;
        const float size = 56.0f + lvl * 7.0f;
        const float bob = std::sin((float) phase * 2.0f + (float) o * 1.7f) * 4.0f;
        auto c = pos[o].translated(0, bob);
        auto area = juce::Rectangle<float>(size, size).withCentre(c);
        const auto col = colOf(o);
        g.setColour(col.withAlpha(0.10f + lvl * 0.35f));
        g.fillEllipse(area.expanded(10 + lvl * 14));
        if (o == selected) { g.setColour(GREEN.withAlpha(0.8f)); g.drawEllipse(area.expanded(6), 2.0f); }
        juce::Path p = body;
        p.applyTransform(juce::AffineTransform::fromTargetPoints(16.0f, 13.0f, area.getX() + area.getWidth() * 0.18f, area.getY() + area.getHeight() * 0.12f,
                                                                 48.0f, 13.0f, area.getRight() - area.getWidth() * 0.18f, area.getY() + area.getHeight() * 0.12f,
                                                                 16.0f, 54.0f, area.getX() + area.getWidth() * 0.18f, area.getBottom() - area.getHeight() * 0.05f));
        g.setColour(col);
        g.fillPath(p);
        if (noise) {   // the noise ghost is speckled
            juce::Random rr(7 + (int) (phase * 6));
            g.setColour(BG.withAlpha(0.55f));
            for (int k = 0; k < 26; ++k) g.fillEllipse(juce::Rectangle<float>(2.2f, 2.2f).withCentre({ area.getX() + area.getWidth() * (0.25f + 0.5f * rr.nextFloat()), area.getY() + area.getHeight() * (0.2f + 0.7f * rr.nextFloat()) }));
        }
        if (smpG) {   // the SAMPLE ghost wears a little waveform across its middle
            juce::Path w; const float y0 = c.y + size * 0.16f, x0 = area.getX() + area.getWidth() * 0.26f, x1 = area.getRight() - area.getWidth() * 0.26f;
            for (int k = 0; k <= 16; ++k) { const float x = x0 + (x1 - x0) * (float) k / 16.0f, y = y0 - std::sin((float) k * 1.3f) * size * (k % 3 == 0 ? 0.07f : 0.04f); if (k) w.lineTo(x, y); else w.startNewSubPath(x, y); }
            g.setColour(BG.withAlpha(0.8f)); g.strokePath(w, juce::PathStrokeType(1.6f));
        }
        g.setColour(BG);
        auto eye = [&](float ex) { g.fillRoundedRectangle(juce::Rectangle<float>(size * 0.09f, size * 0.15f).withCentre({ ex, c.y - size * 0.04f }), 2); };
        eye(c.x - size * 0.11f); eye(c.x + size * 0.11f);
        if (lvl > 0.6f) { g.setColour(BG); g.fillEllipse(juce::Rectangle<float>(size * 0.1f, size * 0.08f * lvl).withCentre({ c.x, c.y + size * 0.12f })); }
        g.setColour(INK); g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
        g.drawText(smpG ? juce::String("SAMPLE") : noise ? juce::String("NOISE") : "GHOST " + juce::String(o + 1), juce::Rectangle<float>(110, 14).withCentre({ c.x, area.getBottom() + 8 }), juce::Justification::centred);
        g.setFont(juce::FontOptions(10.0f));
        g.setColour(out > 0.01f ? GREEN : GREY);
        g.drawText(out > 0.01f ? "you hear it" : "haunts others", juce::Rectangle<float>(110, 12).withCentre({ c.x, area.getBottom() + 20 }), juce::Justification::centred);
        if (fb > 0.01f) {
            g.setColour(col.withAlpha(0.7f));
            g.drawEllipse(juce::Rectangle<float>(26, 12).withCentre({ c.x, area.getY() - 2 }), 1.5f + fb * 2);
        }
    }

    // the empty slot: a dashed ghost outline that says ADD SAMPLE
    void drawEmptySample(juce::Graphics& g) {
        const auto c = pos[kSampleGhost]; auto area = juce::Rectangle<float>(56.0f, 56.0f).withCentre(c);
        if (selected == kSampleGhost) { g.setColour(GREEN.withAlpha(0.8f)); g.drawEllipse(area.expanded(6), 2.0f); }
        juce::Path p = body;
        p.applyTransform(juce::AffineTransform::fromTargetPoints(16.0f, 13.0f, area.getX() + area.getWidth() * 0.18f, area.getY() + area.getHeight() * 0.12f,
                                                                 48.0f, 13.0f, area.getRight() - area.getWidth() * 0.18f, area.getY() + area.getHeight() * 0.12f,
                                                                 16.0f, 54.0f, area.getX() + area.getWidth() * 0.18f, area.getBottom() - area.getHeight() * 0.05f));
        juce::Path d; const float dash[] { 4, 4 }; juce::PathStrokeType(1.6f).createDashedStroke(d, p, dash, 2);
        g.setColour(BLUE); g.fillPath(d);
        g.setFont(juce::FontOptions(20.0f, juce::Font::bold)); g.drawText("+", area.translated(0, 2), juce::Justification::centred);
        g.setColour(INK); g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
        g.drawText("ADD SAMPLE", juce::Rectangle<float>(110, 14).withCentre({ c.x, area.getBottom() + 8 }), juce::Justification::centred);
        g.setFont(juce::FontOptions(10.0f)); g.setColour(GREY);
        g.drawText("drop or click", juce::Rectangle<float>(110, 12).withCentre({ c.x, area.getBottom() + 20 }), juce::Justification::centred);
    }

    int ghostAt(juce::Point<float> p) const { for (int o = 0; o < kAllGhosts; ++o) if (p.getDistanceFrom(pos[o]) < 34) return o; return -1; }
    int linkAt(juce::Point<float> p) const {
        for (int f = 0; f < kAllGhosts; ++f) for (int t = 0; t < gs::kOps; ++t)
            if (canLink(f, t) && linkVal(f, t) > 0.001f && p.getDistanceFrom(handleFor(f, t)) < 13) return f * 8 + t;
        return -1;
    }
    std::function<void()> onAddSample;   // a click on the empty SAMPLE slot (the editor opens the file chooser)

    void mouseMove(const juce::MouseEvent& e) override { int l = linkAt(e.position); if (l != hoverLink) { hoverLink = l; repaint(); } }
    void mouseDown(const juce::MouseEvent& e) override {
        dragging = false;
        dragLink = linkAt(e.position);
        if (dragLink >= 0) {
            auto* prm = proc.apvts.getParameter(linkId(dragLink / 8, dragLink % 8));
            prm->beginChangeGesture(); dragStartVal = prm->getValue(); return;
        }
        dragFrom = ghostAt(e.position);
        dragPt = e.position;
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (dragLink >= 0) {
            auto* prm = proc.apvts.getParameter(linkId(dragLink / 8, dragLink % 8));
            prm->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, dragStartVal - (float) e.getDistanceFromDragStartY() / 160.0f));
            repaint(); return;
        }
        if (dragFrom >= 0) { dragging = e.getDistanceFromDragStart() > 6; dragPt = e.position; repaint(); }
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (dragLink >= 0) { proc.apvts.getParameter(linkId(dragLink / 8, dragLink % 8))->endChangeGesture(); dragLink = -1; return; }
        if (dragFrom >= 0) {
            const int to = ghostAt(e.position);
            if (dragging && to >= 0 && canLink(dragFrom, to)) {
                if (dragFrom == kSampleGhost)   // the SAMPLE haunts the ghost it was dropped on (one target)
                    if (auto* tp = proc.apvts.getParameter("smp_fm_to")) { tp->beginChangeGesture(); tp->setValueNotifyingHost(tp->convertTo0to1((float) to)); tp->endChangeGesture(); }
                auto* prm = proc.apvts.getParameter(linkId(dragFrom, to));
                if (prm->getValue() < 0.01f) { prm->beginChangeGesture(); prm->setValueNotifyingHost(0.5f); prm->endChangeGesture(); }
            } else if (!dragging) {
                selected = dragFrom; if (select) select(dragFrom);
                if (dragFrom == kSampleGhost && !hasSample() && onAddSample) onAddSample();
            }
        }
        dragFrom = -1; dragging = false; repaint();
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override {
        const int l = linkAt(e.position);
        if (l >= 0) { auto* prm = proc.apvts.getParameter(linkId(l / 8, l % 8)); prm->beginChangeGesture(); prm->setValueNotifyingHost(0); prm->endChangeGesture(); repaint(); }
    }
    void tick() { phase += 1.0 / 30.0; repaint(); }

private:
    void parameterChanged(const juce::String&, float) override { juce::MessageManager::callAsync([sp = juce::Component::SafePointer<GhostField>(this)] { if (sp) sp->repaint(); }); }
    PoltergeistProcessor& proc;
    std::function<void(int)> select;
    juce::Path body;
    juce::Point<float> pos[kAllGhosts], dragPt;
    int dragFrom = -1, dragLink = -1, hoverLink = -1; bool dragging = false; float dragStartVal = 0;
    double phase = 0;
};

// ---------------------------------------------------------------------------------------------------------
struct Scope : juce::Component {
    explicit Scope(gs::Engine& e) : eng(e) {}
    void paint(juce::Graphics& g) override {
        paintBase(g, *this);
        auto r = getLocalBounds().toFloat();
        g.setColour(CARD2); g.fillRoundedRectangle(r, 12);
        juce::Path p; const int N = gs::Engine::kScope; const int start = eng.scopePos.load();
        for (int i = 0; i < N; ++i) {
            const float x = r.getX() + r.getWidth() * (float) i / (float) (N - 1);
            const float y = r.getCentreY() - juce::jlimit(-1.0f, 1.0f, eng.scope[(start + i) % N]) * r.getHeight() * 0.45f;
            if (i) p.lineTo(x, y); else p.startNewSubPath(x, y);
        }
        g.setColour(GREEN.withAlpha(0.25f)); g.strokePath(p, juce::PathStrokeType(4));
        g.setColour(GREEN); g.strokePath(p, juce::PathStrokeType(1.4f));
    }
    gs::Engine& eng;
};

// RHYTHM: 16 bars you draw with a finger; the playing step lights up.
struct SeqEditor : juce::Component {
    explicit SeqEditor(PoltergeistProcessor& p) : proc(p) {}
    PoltergeistProcessor& proc; bool touched[16] {};
    void paint(juce::Graphics& g) override {
        paintBase(g, *this);
        auto r = getLocalBounds().toFloat();
        g.setColour(CARD2); g.fillRoundedRectangle(r, 12);
        const int steps = (int) proc.apvts.getRawParameterValue("seq_steps")->load(), now = proc.engine.seqStepNow.load();
        const float w = r.getWidth() / 16.0f;
        for (int i = 0; i < 16; ++i) {
            const float v = proc.apvts.getRawParameterValue("seq_" + juce::String(i + 1))->load();
            auto bar = juce::Rectangle<float>(r.getX() + i * w + 3, r.getY() + 6, w - 6, r.getHeight() - 12);
            g.setColour(FG(0.05f)); g.fillRoundedRectangle(bar, 4);
            auto fill = bar.withTrimmedTop(bar.getHeight() * (1 - v));
            const bool on = i < steps;
            juce::Colour c = i == now ? GREEN : (on ? PURPLE : DIM);
            g.setColour(c.withAlpha(on ? 0.9f : 0.35f)); g.fillRoundedRectangle(fill, 4);
            if (i % 4 == 0) { g.setColour(GREY.withAlpha(0.6f)); g.fillRect(bar.getX(), r.getBottom() - 4, bar.getWidth(), 2.0f); }
        }
    }
    void set(const juce::MouseEvent& e) {
        const int i = juce::jlimit(0, 15, (int) (e.position.x / (getWidth() / 16.0f)));
        const float v = juce::jlimit(0.0f, 1.0f, 1.0f - (e.position.y - 6) / (getHeight() - 12.0f));
        auto* p = proc.apvts.getParameter("seq_" + juce::String(i + 1));
        if (!touched[i]) { touched[i] = true; p->beginChangeGesture(); }
        p->setValueNotifyingHost(v); repaint();
    }
    void mouseDown(const juce::MouseEvent& e) override { set(e); }
    void mouseDrag(const juce::MouseEvent& e) override { set(e); }
    void mouseUp(const juce::MouseEvent&) override { for (int i = 0; i < 16; ++i) if (touched[i]) { touched[i] = false; proc.apvts.getParameter("seq_" + juce::String(i + 1))->endChangeGesture(); } }
};


// =========================================================================================================
static juce::Colour sliceColour(int i) { const juce::Colour C[] = { GREEN, PURPLE, BLUE, PINK, AMBER, CYAN, RED, ghosty::themes::readableOn(juce::Colour(0xffa3e635), CARD2, 3.0) }; return C[(size_t) (((i % 8) + 8) % 8)]; }
static juce::String keyName(int n) { return juce::isPositiveAndBelow(n, 128) ? juce::MidiMessage::getMidiNoteName(n, true, true, 3) : juce::String("-"); }

// The keyboard. An old project's sample keys still play (single keys from the old SAMPLE LAB, the old CHOP slices): they stay
// marked on the keys, so you can see where they are.
struct SampleKeysKeyboard : juce::MidiKeyboardComponent {
    SampleKeysKeyboard(PoltergeistProcessor& p, juce::MidiKeyboardState& st) : juce::MidiKeyboardComponent(st, horizontalKeyboard), proc(p) {}
    PoltergeistProcessor& proc;
    int chopSlot(int note) const {   // CHOP mode (old projects): the slice on this key (its home octave)
        if ((int) proc.apvts.getRawParameterValue("smp_mode")->load() != 4) return -1;
        auto k = proc.chopKitUi; if (!k || !juce::isPositiveAndBelow(note, 128) || k->octOf[note] != 0) return -1; return k->slotOf[note];
    }
    void drawWhiteNote(int note, juce::Graphics& g, juce::Rectangle<float> a, bool down, bool over, juce::Colour line, juce::Colour text) override {
        juce::MidiKeyboardComponent::drawWhiteNote(note, g, a, down, over, line, text);
        if (proc.keyNames[note].isNotEmpty()) { g.setColour(GREEN.withAlpha(0.55f)); g.fillRect(a.withTrimmedTop(a.getHeight() * 0.55f).reduced(2, 2)); }
        else if (const int sl = chopSlot(note); sl >= 0) { g.setColour(sliceColour(sl).withAlpha(0.75f)); g.fillRect(a.withTrimmedTop(a.getHeight() * 0.62f).reduced(2, 2)); }
    }
    void drawBlackNote(int note, juce::Graphics& g, juce::Rectangle<float> a, bool down, bool over, juce::Colour fill) override {
        juce::MidiKeyboardComponent::drawBlackNote(note, g, a, down, over, fill);
        if (proc.keyNames[note].isNotEmpty()) { g.setColour(GREEN.withAlpha(0.8f)); g.fillRect(a.withTrimmedTop(a.getHeight() * 0.6f).reduced(2, 1)); }
        else if (const int sl = chopSlot(note); sl >= 0) { g.setColour(sliceColour(sl).withAlpha(0.9f)); g.fillRect(a.withTrimmedTop(a.getHeight() * 0.62f).reduced(2, 1)); }
    }
};

// ADD SAMPLE (owner 2026-10-26): the FM page's one sample feature. The sample is an FM SOURCE: it HAUNTS a ghost (modulates it),
// LEVEL lets you hear it (it carries the voice), AS WAVE makes one cycle of it a ghost's waveform. This is its small waveform:
// START / END / LOOP with the GhostBlock gestures (Shared/WaveRegion.h, as everywhere), LOOP ON / OFF = looped or one-shot.
// No drag-out (sample exporting lives in Ghost Slice): the WAV grip is off and no export gate is wired.
struct SampleWave : juce::Component {
    explicit SampleWave(PoltergeistProcessor& p) : proc(p) {
        addAndMakeVisible(region); region.showExportGrip = false;
        region.emptyText = "drop a WAV / AIFF / FLAC / MP3 here";
        region.hint = "drag START / END  |  right-click: START here  |  right-drag: LOOP";
        region.setTooltip("The sample. Drag START / END; right-click = START here, right-drag = LOOP (two fingers on a trackpad). LOOP ON = it repeats while you hold a key, OFF = one-shot. Click the wave to hear it.");
        region.palette = [] { ghosty::WaveRegion::Palette c = isDark() ? ghosty::WaveRegion::Palette() : ghosty::WaveRegion::Palette::light();
                              c.bg = CARD2; c.card = CARD; c.ink = INK; c.dim = GREY; c.wave = BLUE; c.loop = AMBER; c.start = GREEN; c.end = PINK; c.label = ON_ACCENT; c.line = LINE; c.waveOut = DIM.interpolatedWith(GREY, 0.5f); return c; };
        region.onChange = [this](const ghosty::WaveRegion::Region& r) { writeParams(r); };
        region.onAudition = [this](bool down, float) { const int n = proc.sampleRoot(); if (down) proc.keyboard.noteOn(1, n, 0.85f); else proc.keyboard.noteOff(1, n, 0.0f); };   // hear it at its own pitch
        region.addMouseListener(this, false);   // (mouse-up ends the parameter gesture)
    }
    PoltergeistProcessor& proc;
    ghosty::WaveRegion region;
    const gs::SampleData* cached = nullptr;
    bool inGesture = false;
    float val(const char* id) const { return proc.apvts.getRawParameterValue(id)->load(); }
    void setRaw(const char* id, float v) { if (auto* p = proc.apvts.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(v)); }
    static constexpr const char* kIds[5] = { "smp_start", "smp_len", "smp_loop", "smp_lst", "smp_lend" };
    // params -> region: END = START + LENGTH x (what is left after START), the loop clipped into START..END
    ghosty::WaveRegion::Region readParams() const {
        ghosty::WaveRegion::Region r; r.start = val("smp_start"); r.end = r.start + val("smp_len") * (1 - r.start); r.loopOn = val("smp_loop") > 0.5f;
        r.loopStart = juce::jlimit(r.start, r.end, val("smp_lst")); r.loopEnd = juce::jlimit(r.start, r.end, val("smp_lend"));
        if (r.loopEnd <= r.loopStart) { r.loopStart = r.start; r.loopEnd = r.end; }
        return r;
    }
    // region -> params (one host gesture per mouse drag)
    void writeParams(const ghosty::WaveRegion::Region& r) {
        if (!inGesture) { for (auto* id : kIds) if (auto* p = proc.apvts.getParameter(id)) p->beginChangeGesture(); inGesture = true; }
        const float st = juce::jlimit(0.0f, 0.99f, r.start);
        setRaw("smp_start", st);
        setRaw("smp_len", juce::jlimit(0.01f, 1.0f, (r.end - st) / juce::jmax(0.001f, 1.0f - st)));
        setRaw("smp_loop", r.loopOn ? 1.0f : 0.0f);
        const bool whole = std::abs(r.loopStart - r.start) < 1e-5f && std::abs(r.loopEnd - r.end) < 1e-5f;   // (the whole window = the old default)
        setRaw("smp_lst", whole ? 0.0f : r.loopStart); setRaw("smp_lend", whole ? 1.0f : r.loopEnd);
        if (!region.isMouseButtonDown()) endGesture();   // (pill clicks, right-click START: one change)
    }
    void endGesture() { if (inGesture) { for (auto* id : kIds) if (auto* p = proc.apvts.getParameter(id)) p->endChangeGesture(); inGesture = false; } }
    std::function<void()> onEmptyClick;   // a click on the empty waveform (the editor opens the file chooser)
    void mouseUp(const juce::MouseEvent& e) override { endGesture(); if (!region.hasWave() && onEmptyClick && !e.mouseWasDraggedSinceMouseDown()) onEmptyClick(); }
    void refreshFromProcessor() {
        auto smp = proc.rawSample ? std::shared_ptr<const gs::SampleData>(proc.rawSample) : proc.engine.getSample();
        if (cached != smp.get()) {   // new peaks once per sample
            cached = smp.get();
            std::vector<float> pk;
            if (smp) { const int bins = 600, n = smp->size(); pk.assign((size_t) bins, 0.0f);
                       for (int x = 0; x < bins; ++x) { const int a = (int) ((double) x / bins * n), b = juce::jmax(a + 1, (int) ((double) (x + 1) / bins * n)); float m = 0; for (int i = a; i < b && i < n; ++i) m = juce::jmax(m, std::abs(smp->data[(size_t) i])); pk[(size_t) x] = juce::jmin(1.0f, m); } }
            region.setPeaks(pk); region.setLengthSeconds(smp ? smp->size() / smp->rate : 0.0);
        }
        if (!region.isMouseButtonDown()) region.setRegion(readParams());
    }
    void resized() override { region.setBounds(getLocalBounds()); }
    void paint(juce::Graphics& g) override { paintBase(g, *this); refreshFromProcessor(); }
    void paintOverChildren(juce::Graphics& g) override {   // the playhead while a note plays the sample
        const float ph = proc.engine.smpPlayhead.load(); if (ph < 0 || !region.hasWave()) return;
        auto w = region.waveArea(); g.setColour(INK); g.drawVerticalLine((int) region.xOf(ph), w.getY() + 2, w.getBottom() - 2);
    }
};
// ---------------------------------------------------------------------------------------------------------
// BASS update UI pieces
static void setParam(PoltergeistProcessor& p, const juce::String& id, float value) {
    if (auto* prm = p.apvts.getParameter(id)) { prm->beginChangeGesture(); prm->setValueNotifyingHost(prm->convertTo0to1(value)); prm->endChangeGesture(); }
}
static float rawParam(PoltergeistProcessor& p, const juce::String& id) { if (auto* a = p.apvts.getRawParameterValue(id)) return a->load(); return 0.0f; }

// the selected ghost's envelope: delay, attack, hold, decay, sustain, release (+ loop)
struct EnvView : juce::Component, juce::SettableTooltipClient {
    explicit EnvView(PoltergeistProcessor& p) : proc(p) { setTooltip("The envelope of the selected ghost. Click: AMP (the ghost's own) / FILTER (the filter envelope). The dot traces the notes you play."); }
    PoltergeistProcessor& proc; int ghost = 0; bool filterMode = false;
    void mouseDown(const juce::MouseEvent&) override { filterMode = !filterMode; repaint(); }
    // ---- the layout: 6 segments (delay, attack, hold, decay, sustain, release); the curve AND the tracer dots use it
    struct Layout { float x0 = 0, sc = 1, top = 0, bot = 1, wd[6] {}, S = 0.5f; bool loop = false;
        float segX(int k) const { float x = x0; for (int i = 0; i < k; ++i) x += wd[i] * sc; return x; }
        // the envelope's own laws: attack linear, decay / release exp(-6.9 t / time) (as Engine.h's Env runs them)
        float level(int k, float u) const { switch (k) { case 0: return 0.0f; case 1: return u; case 2: return 1.0f; case 3: return S + (1 - S) * std::exp(-6.9f * u); case 4: return S; default: return S * std::exp(-6.9f * u); } }
        juce::Point<float> at(int k, float u) const { return { segX(k) + u * wd[k] * sc, bot - level(k, u) * (bot - top) }; }
    };
    Layout layout() const {
        Layout L; auto r = getLocalBounds().toFloat();
        const bool nz = ghost == gs::kNoise;
        auto v = [&](const char* k) {
            if (filterMode) return rawParam(proc, juce::String("flt_") + k);
            return rawParam(proc, nz ? juce::String("nz_") + k : gs::opId(ghost, k)); };
        const float dly = (nz || filterMode) ? 0.0f : v("dly"), A = v("a"), hold = (nz || filterMode) ? 0.0f : v("hold"), D = v("d"), Rl = v("r");
        L.S = v("s"); L.loop = !nz && !filterMode && v("loop") > 0.5f;
        auto tw = [](float t) { return std::sqrt(juce::jmax(0.0f, t)) + 0.04f; };
        float wd[6] = { dly > 0 ? tw(dly) : 0.0f, tw(A), hold > 0 ? tw(hold) : 0.0f, tw(D), 0.5f, tw(Rl) };
        auto in = r.reduced(8, 7); in.removeFromBottom(10);
        for (int pass = 0; pass < 3; ++pass) {   // every stage that exists gets room for its label (a 1 ms attack still shows its "A")
            float total = 0; for (float w : wd) total += w;
            const float scNow = in.getWidth() / total; const float minW[6] = { 26, 12, 30, 12, 12, 12 };
            for (int k = 0; k < 6; ++k) if (wd[k] > 0 && wd[k] * scNow < minW[k]) wd[k] = minW[k] / scNow;
        }
        float total = 0; for (float w : wd) total += w;
        for (int k = 0; k < 6; ++k) L.wd[k] = wd[k];
        L.x0 = in.getX(); L.sc = in.getWidth() / total; L.top = in.getY(); L.bot = in.getBottom();
        return L;
    }
    // a tracer position: engine stage (1 delay .. 6 release) + position in it -> the point on the curve
    static juce::Point<float> dotAt(const Layout& L, int stage, float frac) {
        static const int seg[7] = { 0, 0, 1, 2, 3, 4, 5 };
        const int k = seg[juce::jlimit(0, 6, stage)];
        return L.at(k, stage == 5 ? 0.5f : juce::jlimit(0.0f, 1.0f, frac));   // (resting on sustain: the middle of its flat)
    }
    struct Trail { int id = -1; juce::Point<float> pts[6]; int n = 0; float alpha = 0; int rank = 0; };
    Trail trails[8];
    void paint(juce::Graphics& g) override {
        paintBase(g, *this);
        auto r = getLocalBounds().toFloat();
        g.setColour(CARD2); g.fillRoundedRectangle(r, 10);
        const auto L = layout();
        const float bot = L.bot, top = L.top;
        juce::Path p; p.startNewSubPath(L.segX(0), bot);
        for (int k = 0; k < 6; ++k) for (int i = 1; i <= 16; ++i) { const auto pt = L.at(k, (float) i / 16); p.lineTo(pt); }
        const auto col = filterMode ? CYAN : GHOST_COL[juce::jlimit(0, 6, ghost)];
        juce::Path fill = p; fill.lineTo(L.segX(6), bot); fill.closeSubPath();
        g.setColour(col.withAlpha(0.15f)); g.fillPath(fill);
        g.setColour(col); g.strokePath(p, juce::PathStrokeType(1.8f));
        g.setColour(GREY); g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        const char* nm[6] = { "DLY", "A", "HOLD", "D", "S", "R" };
        for (int k = 0; k < 6; ++k) if (L.wd[k] > 0) g.drawText(nm[k], juce::Rectangle<float>(L.segX(k), bot + 1, L.wd[k] * L.sc, 10), juce::Justification::centred);
        g.drawText(filterMode ? "FILTER" : "AMP", juce::Rectangle<float>(r.getRight() - 52, r.getY() + 3, 46, 10), juce::Justification::centredRight);
        if (L.loop) {   // the loop arrow: back from the end of the decay to the attack
            const float xs = L.segX(4), xa = L.segX(1);
            g.setColour(GREEN); juce::Path a; a.startNewSubPath(xs, top + 2); a.quadraticTo((xs + xa) * 0.5f, top - 6, xa, top + 2);
            g.strokePath(a, juce::PathStrokeType(1.4f)); g.fillEllipse(xa - 2.5f, top, 5, 5);
            g.drawText("LOOP", juce::Rectangle<float>((xs + xa) * 0.5f - 20, top + 1, 40, 10), juce::Justification::centred);
        }
        // ---- the tracer: up to 4 dots (newest brightest), each with a short fading trail; ended voices fade out
        gs::Engine::TraceSnap ts;
        const int env = filterMode ? 7 : juce::jlimit(0, 6, ghost);
        if (proc.engine.traceOn.load() && proc.engine.readTrace(ts)) {
            for (auto& t : trails) t.alpha *= 0.7f;   // anything not refreshed this frame fades
            for (int i = 0; i < ts.count; ++i) {
                const auto& v = ts.v[i]; if (v.id < 0 || v.stage[env] == 0) continue;
                Trail* t = nullptr; for (auto& c : trails) if (c.id == v.id) t = &c;
                if (!t) { t = &trails[0]; for (auto& c : trails) if (c.alpha < t->alpha) t = &c; t->id = v.id; t->n = 0; }
                const auto pt = dotAt(L, v.stage[env], v.frac[env]);
                for (int k = juce::jmin(t->n, 5); k > 0; --k) t->pts[k] = t->pts[k - 1];
                t->pts[0] = pt; t->n = juce::jmin(6, t->n + 1); t->alpha = 1.0f; t->rank = i;
            }
        } else for (auto& t : trails) t.alpha *= 0.7f;
        const auto dotCol = GREEN;
        for (auto& t : trails) {
            if (t.alpha < 0.05f || t.n == 0) continue;
            const float a = t.alpha * (t.rank == 0 ? 1.0f : 0.55f - 0.1f * (float) t.rank);
            for (int k = t.n - 1; k >= 1; --k) { g.setColour(dotCol.withAlpha(a * 0.25f * (1.0f - (float) k / 6.0f))); g.fillEllipse(juce::Rectangle<float>(5, 5).withCentre(t.pts[k])); }
            g.setColour(dotCol.withAlpha(a * 0.3f)); g.fillEllipse(juce::Rectangle<float>(12, 12).withCentre(t.pts[0]));   // glow
            g.setColour(dotCol.withAlpha(a)); g.fillEllipse(juce::Rectangle<float>(7, 7).withCentre(t.pts[0]));
            g.setColour(INK.withAlpha(a)); g.drawEllipse(juce::Rectangle<float>(7, 7).withCentre(t.pts[0]), 1.0f);
        }
    }
};

// WOBBLE: draw the shape (up to 8 points), tempo-synced; click to add, drag to move, double-click to remove
struct WobbleEditor : juce::Component {
    explicit WobbleEditor(PoltergeistProcessor& p) : proc(p) {}
    PoltergeistProcessor& proc;
    float xs[gs::kWobPts] {}, ys[gs::kWobPts] {}; int n = 2, drag = -1;
    std::function<void()> onFirstDraw;   // a shape edit: make sure the wobble moves something you can hear
    std::vector<juce::RangedAudioParameter*> held;   // parameters inside the current mouse gesture
    void beginAll() { endAll(); for (int i = 0; i < gs::kWobPts; ++i) for (auto* k : { "wob_x", "wob_y" }) if (auto* p = proc.apvts.getParameter(k + juce::String(i + 1))) { p->beginChangeGesture(); held.push_back(p); }
                      if (auto* p = proc.apvts.getParameter("wob_n")) { p->beginChangeGesture(); held.push_back(p); } }
    void endAll() { for (auto* p : held) p->endChangeGesture(); held.clear(); }
    void setRaw(const juce::String& id, float v) { if (auto* p = proc.apvts.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(v)); }
    ~WobbleEditor() override { endAll(); }
    void read() {
        n = juce::jlimit(2, gs::kWobPts, (int) rawParam(proc, "wob_n"));
        for (int i = 0; i < n; ++i) { xs[i] = i == 0 ? 0.0f : rawParam(proc, "wob_x" + juce::String(i + 1)); ys[i] = rawParam(proc, "wob_y" + juce::String(i + 1)); }
        for (int i = 2; i < n; ++i) for (int j = i; j > 1 && xs[j] < xs[j - 1]; --j) { std::swap(xs[j], xs[j - 1]); std::swap(ys[j], ys[j - 1]); }
    }
    void write() {   // inside a gesture (mouse down .. up)
        setRaw("wob_n", (float) n);
        for (int i = 0; i < n; ++i) { setRaw("wob_x" + juce::String(i + 1), xs[i]); setRaw("wob_y" + juce::String(i + 1), ys[i]); }
    }
    juce::Rectangle<float> area() const { return getLocalBounds().toFloat().reduced(10, 10); }
    juce::Point<float> toScreen(float x, float y) const { auto a = area(); return { a.getX() + x * a.getWidth(), a.getBottom() - y * a.getHeight() }; }
    void paint(juce::Graphics& g) override {
        paintBase(g, *this);
        read();
        auto r = getLocalBounds().toFloat(); auto a = area();
        g.setColour(CARD2); g.fillRoundedRectangle(r, 12);
        for (int k = 0; k <= 16; ++k) { g.setColour(FG(k % 4 == 0 ? 0.12f : 0.04f)); const float x = a.getX() + a.getWidth() * k / 16.0f; g.drawVerticalLine((int) x, a.getY(), a.getBottom()); }
        g.setColour(FG(0.06f)); g.drawHorizontalLine((int) a.getCentreY(), a.getX(), a.getRight());
        const float sm = rawParam(proc, "wob_smooth");
        juce::Path p; const int N = 240;
        for (int i = 0; i <= N; ++i) {
            const float ph = (float) i / N * 0.99999f;
            const auto pt = toScreen(ph, gs::Engine::wobbleShape(xs, ys, n, sm, ph));
            if (i == 0) p.startNewSubPath(pt); else p.lineTo(pt);
        }
        juce::Path fill = p; fill.lineTo(a.getRight(), a.getBottom()); fill.lineTo(a.getX(), a.getBottom()); fill.closeSubPath();
        g.setGradientFill(juce::ColourGradient(GREEN.withAlpha(0.25f), 0, a.getY(), PURPLE.withAlpha(0.05f), 0, a.getBottom(), false)); g.fillPath(fill);
        g.setColour(GREEN); g.strokePath(p, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        for (int i = 0; i < n; ++i) {
            const auto pt = toScreen(xs[i], ys[i]);
            g.setColour(i == drag ? GREEN : INK); g.fillEllipse(juce::Rectangle<float>(11, 11).withCentre(pt));
            g.setColour(BG); g.fillEllipse(juce::Rectangle<float>(5, 5).withCentre(pt));
        }
        const float ph = proc.engine.wobUi.load();
        if (ph >= 0) { g.setColour(PURPLE.withAlpha(0.9f)); g.drawVerticalLine((int) (a.getX() + ph * a.getWidth()), a.getY(), a.getBottom()); }
        g.setColour(GREY); g.setFont(juce::FontOptions(10.5f));
        g.drawText("one cycle = " + gs::kWobDivs[juce::jlimit(0, gs::kWobDivs.size() - 1, (int) rawParam(proc, "wob_div"))], a.reduced(4).removeFromTop(14), juce::Justification::topRight);
    }
    int hit(juce::Point<float> m) const { for (int i = 0; i < n; ++i) if (toScreen(xs[i], ys[i]).getDistanceFrom(m) < 9) return i; return -1; }
    void mouseDown(const juce::MouseEvent& e) override {
        read(); drag = hit(e.position); beginAll();
        if (drag < 0 && n < gs::kWobPts) {
            auto a = area(); const float x = juce::jlimit(0.001f, 0.999f, (e.position.x - a.getX()) / a.getWidth()), y = juce::jlimit(0.0f, 1.0f, (a.getBottom() - e.position.y) / a.getHeight());
            int at = n; for (int i = 1; i < n; ++i) if (xs[i] > x) { at = i; break; }
            for (int i = n; i > at; --i) { xs[i] = xs[i - 1]; ys[i] = ys[i - 1]; }
            xs[at] = x; ys[at] = y; ++n; drag = at; write();
        }
        repaint();
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (drag < 0) return;
        auto a = area();
        float x = (e.position.x - a.getX()) / a.getWidth(); const float y = juce::jlimit(0.0f, 1.0f, (a.getBottom() - e.position.y) / a.getHeight());
        const float lo = drag == 0 ? 0.0f : xs[drag - 1] + 0.002f, hi = drag + 1 < n ? xs[drag + 1] - 0.002f : 0.999f;
        x = drag == 0 ? 0.0f : juce::jlimit(lo, juce::jmax(lo, hi), x);
        xs[drag] = x; ys[drag] = y;
        setRaw("wob_x" + juce::String(drag + 1), x); setRaw("wob_y" + juce::String(drag + 1), y);
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override { endAll(); if (drag >= 0 && onFirstDraw) onFirstDraw(); drag = -1; repaint(); }
    void mouseDoubleClick(const juce::MouseEvent& e) override {
        read(); const int h = hit(e.position);
        if (h > 0 && n > 2) { beginAll(); for (int i = h; i < n - 1; ++i) { xs[i] = xs[i + 1]; ys[i] = ys[i + 1]; } --n; write(); endAll(); }
        drag = -1; repaint();
    }
};

// a favourite star (drawn, so it looks the same on every system)
struct StarButton : juce::Button {
    StarButton() : juce::Button("fav") {}
    void paintButton(juce::Graphics& g, bool over, bool) override {
        auto r = getLocalBounds().toFloat().reduced(3); const auto c = r.getCentre(); const float R = juce::jmin(r.getWidth(), r.getHeight()) / 2, ri = R * 0.45f;
        juce::Path p;
        for (int k = 0; k < 10; ++k) { const float a = juce::MathConstants<float>::pi * (float) k / 5.0f; const float rr = (k % 2 == 0) ? R : ri; const juce::Point<float> pt(c.x + rr * std::sin(a), c.y - rr * std::cos(a)); if (k == 0) p.startNewSubPath(pt); else p.lineTo(pt); }
        p.closeSubPath();
        if (getToggleState()) { g.setColour(AMBER); g.fillPath(p); }
        g.setColour(getToggleState() ? AMBER : (over ? INK : GREY)); g.strokePath(p, juce::PathStrokeType(1.4f));
    }
};

// the two BASS filters' response (as set: no envelope, keyed at C4), redrawn when a filter setting changes
struct FilterResponse : juce::Component {
    explicit FilterResponse(PoltergeistProcessor& p) : proc(p) { setInterceptsMouseClicks(false, false); }
    PoltergeistProcessor& proc;
    static constexpr int kOrder = 15, kN = 1 << kOrder;   // 2.7 Hz bins: smooth down to 20 Hz
    std::vector<float> curve[3];   // filter 1 alone, filter 2 alone, the routed result (dB per display column)
    juce::String key; bool any = false, split = false;
    void update() {
        juce::String k;
        for (auto* id : { "f1_on", "f1_type", "f1_cut", "f1_res", "f1_drive", "f2_on", "f2_type", "f2_cut", "f2_res", "f2_drive", "flt_route" }) k << juce::String(rawParam(proc, id), 3) << ",";
        if (k == key && getWidth() > 0 && (int) curve[2].size() == getWidth()) return;
        key = k;
        const double fs = 88200.0; const int cols = juce::jmax(1, getWidth());
        const bool on[2] = { rawParam(proc, "f1_on") > 0.5f, rawParam(proc, "f2_on") > 0.5f };
        const int route = (int) rawParam(proc, "flt_route");
        any = on[0] || on[1]; split = route == 2;
        std::vector<float> combMem((size_t) 4 * 5000, 0.0f);
        auto impulse = [&](int which) {   // which: 0 / 1 = one filter, 2 = routed
            gs::BassFilter F[2];
            for (int f = 0; f < 2; ++f) {
                F[f].combN = 5000; F[f].comb[0] = combMem.data() + (size_t) (f * 2) * 5000; F[f].comb[1] = combMem.data() + (size_t) (f * 2 + 1) * 5000; F[f].reset(true);
                const juce::String q = "f" + juce::String(f + 1) + "_";
                const float cut = rawParam(proc, q + "cut");
                F[f].setup((int) rawParam(proc, q + "type"), cut, rawParam(proc, q + "res"), rawParam(proc, q + "drive"), std::log2(juce::jmax(20.0f, cut) / 20.0f) / 9.9658f, (float) fs);
            }
            std::vector<float> buf((size_t) kN * 2, 0.0f);
            for (int i = 0; i < kN; ++i) {
                const float x = i == 0 ? 0.01f : 0.0f; float y = x;   // a small impulse (the drive stays in its linear range)
                if (which < 2) y = on[which] ? F[which].tick(0, x) : x;
                else if (route == 1 && on[0] && on[1]) y = (F[0].tick(0, x) + F[1].tick(0, x)) * 0.6f;
                else { if (on[0]) y = F[0].tick(0, y); if (on[1]) y = F[1].tick(0, y); }
                buf[(size_t) i] = y * 100.0f;
            }
            juce::dsp::FFT fft(kOrder); fft.performFrequencyOnlyForwardTransform(buf.data());
            std::vector<float> c((size_t) cols);
            for (int x = 0; x < cols; ++x) {
                const double f = 20.0 * std::pow(1000.0, (double) x / juce::jmax(1, cols - 1));
                const double bf = f / fs * kN; const int b = juce::jlimit(1, kN / 2 - 2, (int) bf); const float t = (float) (bf - b);
                c[(size_t) x] = juce::Decibels::gainToDecibels(buf[(size_t) b] + (buf[(size_t) b + 1] - buf[(size_t) b]) * t, -60.0f);
            }
            return c;
        };
        for (int w = 0; w < 3; ++w) curve[w] = impulse(w);
        repaint();
    }
    void paint(juce::Graphics& g) override {
        paintBase(g, *this);
        auto r = getLocalBounds().toFloat();
        g.setColour(CARD2); g.fillRoundedRectangle(r, 8);
        const float top = 18.0f, bot = -30.0f;
        auto Y = [&](float dB) { return r.getY() + 3 + (top - juce::jlimit(bot, top, dB)) / (top - bot) * (r.getHeight() - 6); };
        g.setColour(FG(0.06f));
        for (double f : { 100.0, 1000.0, 10000.0 }) { const float x = r.getX() + (float) (std::log10(f / 20.0) / 3.0) * r.getWidth(); g.drawVerticalLine((int) x, r.getY(), r.getBottom()); }
        g.drawHorizontalLine((int) Y(0), r.getX(), r.getRight());
        g.setFont(juce::FontOptions(9.0f, juce::Font::bold)); g.setColour(GREY);
        g.drawText("100", juce::Rectangle<float>(r.getX() + (float) (std::log10(5.0) / 3.0) * r.getWidth() + 2, r.getBottom() - 11, 30, 10), juce::Justification::left);
        g.drawText("1k", juce::Rectangle<float>(r.getX() + (float) (std::log10(50.0) / 3.0) * r.getWidth() + 2, r.getBottom() - 11, 30, 10), juce::Justification::left);
        g.drawText("10k", juce::Rectangle<float>(r.getX() + (float) (std::log10(500.0) / 3.0) * r.getWidth() + 2, r.getBottom() - 11, 30, 10), juce::Justification::left);
        if (!any) { g.drawText("FILTERS OFF", r, juce::Justification::centred); return; }
        auto draw = [&](const std::vector<float>& c, juce::Colour col, float th) {
            if (c.empty()) return;
            juce::Path p;
            for (size_t x = 0; x < c.size(); ++x) { const float px = r.getX() + (float) x, py = Y(c[x]); if (x == 0) p.startNewSubPath(px, py); else p.lineTo(px, py); }
            g.setColour(col); g.strokePath(p, juce::PathStrokeType(th));
        };
        if (split) { draw(curve[0], PURPLE, 1.6f); draw(curve[1], GREEN, 1.6f); g.setColour(GREY); g.drawText("SPLIT: 1 purple, 2 green", r.reduced(6, 2), juce::Justification::topRight); }
        else draw(curve[2], GREEN, 1.8f);
    }
};

struct PoltergeistEditor::Content : juce::Component, juce::DragAndDropContainer {
    PoltergeistEditor& ed; PoltergeistProcessor& proc;
    ghosty::UnlockButton unlock { proc.license };
    struct FocusGuard : juce::MouseListener { std::function<void(juce::Component*)> onDown; void mouseDown(const juce::MouseEvent& e) override { if (onDown) onDown(e.eventComponent); } } focusGuard;
    juce::TextEditor search; juce::TextButton summonBtn { "SUMMON" }, diceBtn { "RANDOM" }, prevBtn { "<" }, nextBtn { ">" }, presetBtn, saveSnd { "SAVE" }, loadSnd { "LOAD" };
    ghosty::SnapshotBar snapBar { proc.snapshots(), true };   // [SNAPSHOT][v]: < > of the preset bar cycle the snapshots while one is loaded
    juce::OwnedArray<juce::TextButton> chips;
    std::unique_ptr<GhostField> field;
    Scope scope;
    juce::Label panelTitle, panelRole, status;
    juce::OwnedArray<Knob> opKnobs, filterKnobs, macroKnobs, miscKnobs;
    juce::ComboBox fltType, lfoDiv, lfoShape;
    juce::ToggleButton monoBtn { "MONO" }, syncBtn { "SYNC" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> fltTypeA, lfoDivA, lfoShapeA;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> monoA, syncA;
    SampleKeysKeyboard keys;
    juce::OwnedArray<juce::Label> macroHints;
    juce::Rectangle<int> panelArea, filterArea, pitchArea, driftArea, voiceArea, macroArea;
    // v0.2 pages
    int page = 0;
    std::unique_ptr<juce::TextButton> pageBtns[10];   // (the page numbers stay as they were: 1 SAMPLE, 5 SAMPLE LAB and 6 CHOP left in 2026-10; no tab)
    static constexpr int kPages = 10;
    static bool pageExists(int pg) { return pg == 0 || pg == 2 || pg == 3 || pg == 4 || pg == 7 || pg == 8 || pg == 9; }
    std::vector<juce::Component*> pageComps[kPages];
    juce::OwnedArray<Knob> moveKnobs, fxKnobs, playKnobs; juce::OwnedArray<Knob> morphKnobs;
    juce::OwnedArray<juce::ComboBox> combos;
    juce::OwnedArray<juce::ToggleButton> toggles;
    juce::OwnedArray<juce::Label> helps;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAtts;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> toggleAtts;
    SeqEditor seqEd { proc };
    std::map<juce::String, juce::Component*> byId;
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> cards[kPages];
    // ADD SAMPLE: the SAMPLE ghost's panel (in the ghost panel's place while the SAMPLE ghost is selected)
    SampleWave smpWave { proc };
    juce::TextButton smpAddBtn { "ADD SAMPLE" }, smpClearBtn { "CLEAR" };
    juce::ComboBox smpRoot;
    juce::Label smpName, smpTag, smpRootL, smpHauntL, smpWaveL;
    juce::OwnedArray<Knob> smpKnobs;
    std::vector<juce::Component*> sampleComps;   // the SAMPLE ghost selected: these show, the ghost / noise panels hide
    // BASS update: ghost panel extras, the NOISE ghost panel, pages 7 (BASS) and 8 (WOBBLE)
    EnvView envView { proc }; WobbleEditor wobEd { proc };
    juce::ComboBox waveBox; juce::ToggleButton loopBtn { "LOOP ENV" }, f2Btn { "TO FILTER 2" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> waveA;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> loopA, f2A;
    juce::OwnedArray<Knob> noiseKnobs, bassKnobs, wobKnobs;
    std::vector<juce::Component*> noiseComps, ghostOnly;   // NOISE selected: noiseComps show, ghostOnly hide
    juce::Label* noiseFltLabel = nullptr; juce::Label* noiseHint = nullptr;
    juce::OwnedArray<juce::Slider> mmSliders; std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> mmAtts;
    juce::OwnedArray<juce::Label> mmNums, mmVals;
    juce::TextButton wobShapeBtn { "SHAPES" };
    FilterResponse fltResp { proc };
    int wobHelp = 0;

    Content(PoltergeistEditor& e, PoltergeistProcessor& p)
        : ed(e), proc(p), scope(p.engine), keys(p, p.keyboard) {
        search.setTextToShowWhenEmpty("Type a sound... dark trap bell, wobble growl, 808 slide", GREY);
        search.setFont(juce::FontOptions(15.0f));
        search.setIndents(12, 9);
        search.onReturnKey = [this] { summonBtn.onClick(); };
        // clicking anywhere else (knobs, pads, the sample view) takes the text cursor out of the SUMMON box
        setWantsKeyboardFocus(true); search.setExplicitFocusOrder(1000); addMouseListener(&focusGuard, true);
        focusGuard.onDown = [this](juce::Component* c) { if (c != &search && ! search.isParentOf(c) && search.hasKeyboardFocus(true)) grabKeyboardFocus(); };
        addAndMakeVisible(search);
        summonBtn.getProperties().set("accent", true);
        // SUMMON uses your words; RANDOM ignores them. With an empty box SUMMON asks for words instead of acting like RANDOM.
        summonBtn.onClick = [this] {
            if (search.getText().trim().isEmpty()) { status.setText("Type a sound first (e.g. dark bell, dirty bass), then SUMMON. Or press RANDOM for a surprise.", juce::dontSendNotification); search.grabKeyboardFocus(); return; }
            ed.doSummon(search.getText()); };
        summonBtn.setTooltip("Makes a sound from the words you typed");
        addAndMakeVisible(summonBtn);
        diceBtn.setTooltip("Random sound: summons a surprise sound (clears the text box)");
        diceBtn.onClick = [this] { search.clear(); ed.doSummon(""); };
        addAndMakeVisible(diceBtn);
        for (auto* b : { &prevBtn, &nextBtn, &presetBtn, &saveSnd, &loadSnd }) addAndMakeVisible(*b);
        addChildComponent(unlock); unlock.refresh();
        saveSnd.onClick = [this] {
            auto* w = new juce::AlertWindow("Save this sound", "Name it (saved in Documents/Ghosty/Poltergeist):", juce::MessageBoxIconType::NoIcon);
            w->addTextEditor("name", proc.patchName); w->addButton("SAVE", 1, juce::KeyPress(juce::KeyPress::returnKey)); w->addButton("Cancel", 0);
            w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int r) {
                if (r == 1) { const auto n = w->getTextEditorContents("name").trim(); if (n.isNotEmpty()) status.setText(proc.saveUserSound(n) ? "Saved \"" + n + "\". LOAD brings it back in any project." : "Couldn't save.", juce::dontSendNotification); ed.refreshPresetName(); }
            }), true);
        };
        loadSnd.onClick = [this] {
            juce::PopupMenu m; auto files = proc.userDir().findChildFiles(juce::File::findFiles, false, "*.ghostpatch"); files.sort();
            for (int i = 0; i < files.size(); ++i) m.addItem(i + 1, files[i].getFileNameWithoutExtension());
            if (files.isEmpty()) m.addItem(-1, "No saved sounds yet (use SAVE)", false);
            m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&loadSnd), [this, files](int r) { if (r > 0 && proc.loadUserSound(files[r - 1])) { ed.refreshPresetName(); snapBar.refresh(); smpWave.cached = nullptr; ed.selectGhost(field->selected); status.setText("Loaded \"" + files[r - 1].getFileNameWithoutExtension() + "\".", juce::dontSendNotification); } });
        };
        // SNAPSHOT: one click saves the sound into the SNAPSHOTS bank (Shift+Alt+S too); [v] lists them
        snapBar.onMessage = [this](const juce::String& m) { status.setText(m, juce::dontSendNotification); };
        snapBar.onLoaded = [this] { afterSoundLoaded(); };
        proc.snapshots().onChanged = [sp = juce::Component::SafePointer<Content>(this)] { if (sp) sp->snapBar.refresh(); };
        addAndMakeVisible(snapBar);
        // < > step through the presets; shift-click jumps to the next / previous folder
        prevBtn.setTooltip("Previous preset (shift-click: previous folder)"); nextBtn.setTooltip("Next preset (shift-click: next folder)");
        prevBtn.onClick = [this] { if (proc.snapshots().current() >= 0) { snapBar.step(-1); return; } stepPreset(-1, juce::ModifierKeys::currentModifiers.isShiftDown()); };   // (in the SNAPSHOTS bank: the snapshots)
        nextBtn.onClick = [this] { if (proc.snapshots().current() >= 0) { snapBar.step(1); return; } stepPreset(1, juce::ModifierKeys::currentModifiers.isShiftDown()); };
        presetBtn.setTooltip("All presets by folder, your favourites and a search");
        presetBtn.onClick = [this] { showPresetMenu({}); };
        favBtn.setTooltip("Favourite this preset (shows at the top of the preset list)");
        favBtn.onClick = [this] { toggleFavourite(); };
        addAndMakeVisible(favBtn); loadFavourites();
        bypassBtn.setDark(true); addAndMakeVisible(bypassBtn);
        themeBtn.setTooltip("LIGHT / DARK theme (saved with the project; not part of any sound)");
        themeBtn.onClick = [this] { applyTheme(ghosty::themes::next(currentTheme())); };
        themeBtn.setTooltip("Switch the look: BLACK -> PURPLE -> LIGHT (the button names the next look)");
        addAndMakeVisible(themeBtn);
        const juce::StringArray cats { "808", "Bell", "Growl", "Wobble", "Reese", "Pluck", "Keys", "Pad", "Choir", "Lead", "Brass", "Riser", "Arp" };
        const juce::StringArray mods { "dark", "bright", "haunted", "dirty", "wide", "glitch", "lofi", "metallic" };
        for (auto& c : cats) { auto* b = chips.add(new juce::TextButton(c)); b->onClick = [this, c] { search.setText(withMods(c.toLowerCase())); ed.doSummon(search.getText()); }; addAndMakeVisible(b); }
        for (auto& m : mods) {
            auto* b = chips.add(new juce::TextButton(m)); b->setClickingTogglesState(true);
            b->onClick = [this] {
                auto words = gs::Summoner::tokenize(search.getText()); juce::String cat;
                for (auto& w : words) if (gs::Summoner::categoryFor(juce::StringArray(w)).isNotEmpty()) { cat = w; break; }
                search.setText(withMods(cat)); if (cat.isNotEmpty()) ed.doSummon(search.getText());
            };
            addAndMakeVisible(b);
        }

        field = std::make_unique<GhostField>(proc, [this](int o) { ed.selectGhost(o); });
        addAndMakeVisible(*field);
        addAndMakeVisible(scope);

        panelTitle.setFont(juce::FontOptions(20.0f, juce::Font::bold)); addAndMakeVisible(panelTitle);
        panelRole.setFont(juce::FontOptions(11.5f)); panelRole.setColour(juce::Label::textColourId, GREY); addAndMakeVisible(panelRole);
        status.setFont(juce::FontOptions(12.0f)); status.setColour(juce::Label::textColourId, GREEN); addAndMakeVisible(status);

        const char* opNames[] = { "RATIO", "FINE", "OUT", "WAVE / BEND", "FEEDBACK", "VELOCITY", "DELAY", "ATTACK", "HOLD", "DECAY", "SUSTAIN", "RELEASE" };
        for (int i = 0; i < 12; ++i) {
            auto* k = opKnobs.add(new Knob(opNames[i]));
            if (i == 0) {
                k->removeChildComponent(k->slider.get());
                k->slider.reset(new RatioSlider());
                k->slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
                k->slider->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
                k->slider->setPopupDisplayEnabled(true, true, nullptr);
                k->addAndMakeVisible(*k->slider);
            }
            addAndMakeVisible(k);
        }
        const std::pair<const char*, const char*> fk[] = { { "CUTOFF", "flt_cut" }, { "RESO", "flt_res" }, { "ENV", "flt_env" }, { "KEY", "flt_key" },
                                                           { "AMOUNT", "penv_amt" }, { "DECAY", "penv_dec" }, { "RATE", "lfo_rate" } };
        for (auto& [n, id] : fk) { auto* k = filterKnobs.add(new Knob(n)); k->attach(proc.apvts, id); addAndMakeVisible(k); }
        const std::pair<const char*, const char*> mk[] = { { "GLIDE", "glide" }, { "OCTAVE", "octave" }, { "VOICES", "voices" }, { "MASTER", "master" } };
        for (auto& [n, id] : mk) { auto* k = miscKnobs.add(new Knob(n)); k->attach(proc.apvts, id); addAndMakeVisible(k); }
        for (int m = 0; m < 6; ++m) {
            auto* k = macroKnobs.add(new Knob(gs::kMacroNames[m], true)); k->attach(proc.apvts, "macro_" + gs::kMacroIds[m]); addAndMakeVisible(k);
            auto* h = macroHints.add(new juce::Label({}, gs::kMacroHints[m]));
            h->setFont(juce::FontOptions(10.5f)); h->setColour(juce::Label::textColourId, GREY); h->setJustificationType(juce::Justification::centredLeft);
            h->setMinimumHorizontalScale(0.8f); addAndMakeVisible(h);
        }
        fltType.addItemList({ "Low", "Band", "High" }, 1); fltTypeA = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.apvts, "flt_type", fltType);
        lfoDiv.addItemList(gs::kLfoDivs, 1); lfoDivA = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.apvts, "lfo_div", lfoDiv);
        lfoShape.addItemList({ "Sine", "Triangle", "Saw", "Square", "Random" }, 1); lfoShapeA = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.apvts, "lfo_shape", lfoShape);
        monoA = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "mono", monoBtn);
        syncA = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "lfo_sync", syncBtn);
        addAndMakeVisible(fltType); addAndMakeVisible(lfoDiv); addAndMakeVisible(lfoShape); addAndMakeVisible(monoBtn); addAndMakeVisible(syncBtn);

        keys.setColour(juce::MidiKeyboardComponent::whiteNoteColourId, KEYWHITE);
        keys.setColour(juce::MidiKeyboardComponent::blackNoteColourId, CARD2);
        keys.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, PURPLE.withAlpha(0.7f));
        keys.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, GREEN.withAlpha(0.3f));
        keys.setAvailableRange(36, 96); keys.setLowestVisibleKey(36);
        addAndMakeVisible(keys);
        buildPages();
        for (juce::Component* c : { (juce::Component*) &scope, (juce::Component*) &seqEd, (juce::Component*) &envView, (juce::Component*) &wobEd, (juce::Component*) &fltResp,
                                    (juce::Component*) &smpWave }) c->getProperties().set("onCard", true);
    }

    // attachments go first: MUTE GHOSTS / MUTE SAMPLE are declared after toggleAtts, so the default order freed them before their attachments
    ~Content() override {
        if (proc.snaps) proc.snaps->onChanged = nullptr;
        proc.engine.wobView.store(false); proc.onChopRendered = nullptr; proc.onLabRendered = nullptr; mmAtts.clear(); waveA.reset(); loopA.reset(); f2A.reset(); toggleAtts.clear(); comboAtts.clear(); }

    Knob* knob(juce::OwnedArray<Knob>& arr, int pg, const char* name, const char* id) {
        auto* k = arr.add(new Knob(name)); k->attach(proc.apvts, id); addAndMakeVisible(k); pageComps[pg].push_back(k); byId[id] = k; return k;
    }
    juce::ComboBox* combo(int pg, const char* id) {
        auto* c = combos.add(new juce::ComboBox());
        if (auto* ch = dynamic_cast<juce::AudioParameterChoice*>(proc.apvts.getParameter(id))) c->addItemList(ch->choices, 1);
        comboAtts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.apvts, id, *c));
        addAndMakeVisible(c); pageComps[pg].push_back(c); byId[id] = c; return c;
    }
    juce::ToggleButton* toggle(int pg, const char* text, const char* id) {
        auto* t = toggles.add(new juce::ToggleButton(text));
        toggleAtts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, id, *t));
        addAndMakeVisible(t); pageComps[pg].push_back(t); byId[id] = t; return t;
    }
    juce::Label* help(int pg, const juce::String& text) {
        auto* l = helps.add(new juce::Label({}, text)); l->setFont(juce::FontOptions(12.0f)); l->setColour(juce::Label::textColourId, GREY);
        l->setJustificationType(juce::Justification::topLeft); l->setMinimumHorizontalScale(0.9f); addAndMakeVisible(l); pageComps[pg].push_back(l); return l;
    }

    void buildPages() {
        static const char* names[kPages] = { "GHOSTS", nullptr, "MOVE", "FX", "PLAY", nullptr, nullptr, "BASS", "WOBBLE", "MORPH" };
        static const char* roles[kPages] = { "GHOSTS: the FM synth (the six ghosts + noise + ADD SAMPLE)", nullptr, "MOVE: rhythm, talk (vowel) and bubbles", "FX: comb, sync, squeeze",
                                             "PLAY: scale lock, chords, arp", nullptr, nullptr, "BASS: filters, sub, drive", "WOBBLE: the drawn wobble, LFOs and mod matrix", "MORPH: put sounds on ghosts, drag the puck to blend them" };
        for (int i = 0; i < kPages; ++i) {
            if (!pageExists(i)) continue;
            pageBtns[i] = std::make_unique<juce::TextButton>(names[i]); auto* b = pageBtns[i].get(); b->setClickingTogglesState(false);
            b->onClick = [this, i] { setPage(i); }; addAndMakeVisible(b);
            b->setTooltip(roles[i]);
        }
        // page 0: the ghosts (existing controls)
        for (juce::Component* c : { (juce::Component*) field.get(), (juce::Component*) &scope, (juce::Component*) &panelTitle, (juce::Component*) &panelRole,
                                    (juce::Component*) &fltType, (juce::Component*) &lfoDiv, (juce::Component*) &lfoShape, (juce::Component*) &monoBtn, (juce::Component*) &syncBtn }) pageComps[0].push_back(c);
        for (auto* k : opKnobs) pageComps[0].push_back(k);
        for (auto* k : filterKnobs) pageComps[0].push_back(k);
        for (auto* k : miscKnobs) pageComps[0].push_back(k);
        // page 2: MOVE (rhythm, talk, bubbles)
        addAndMakeVisible(seqEd); pageComps[2].push_back(&seqEd);
        toggle(2, "RHYTHM ON", "seq_on"); combo(2, "seq_div"); combo(2, "seq_dest");
        knob(moveKnobs, 2, "AMOUNT", "seq_amt"); knob(moveKnobs, 2, "SMOOTH", "seq_glide"); knob(moveKnobs, 2, "STEPS", "seq_steps");
        knob(moveKnobs, 2, "TALK", "vow_amt"); knob(moveKnobs, 2, "VOWEL", "vow_pos"); knob(moveKnobs, 2, "MOVE", "vow_mod"); knob(moveKnobs, 2, "RESO", "vow_res");
        knob(moveKnobs, 2, "BUBBLES", "bub_amt"); knob(moveKnobs, 2, "POP", "bub_pitch"); knob(moveKnobs, 2, "TONE", "bub_tone"); combo(2, "bub_div");
        help(2, "Draw the 16 steps with your finger. TARGET picks what the rhythm moves: GATE (stutter), FILTER, HAUNT (FM growl), TALK (vowels) or PITCH (jumps).");
        help(2, "TALK makes the bass speak (a-e-i-o-u, the 'yoi'). MOVE lets the DRIFT LFO or the rhythm (target TALK) work the mouth. The mod wheel opens it too.");
        help(2, "BUBBLES pops a pitch-dropping blip on every step: bubbly, poppy dubstep sparkle. POP = how far each blip falls.");
        // page 3: FX (comb, sync, squeeze)
        knob(fxKnobs, 3, "COMB", "cmb_amt"); knob(fxKnobs, 3, "FEEDBACK", "cmb_fb"); knob(fxKnobs, 3, "TUNE", "cmb_tune"); toggle(3, "FOLLOW KEY", "cmb_key");
        knob(fxKnobs, 3, "SYNC", "sync_amt"); knob(fxKnobs, 3, "SQUEEZE", "ott_amt");
        help(3, "COMB = a tuned metal resonance that tears through growls (follows the note you play). SYNC = ghost 1 hard-sync screech: turn it with HAUNT for laser leads. SQUEEZE = three-band squash (down above, up below), the loud, bright, in-your-face dubstep finish. Drive, crush and space stay on the big macros below.");
        // page 4: PLAY (scale, chords, arp)
        toggle(4, "SCALE LOCK", "scale_on"); combo(4, "scale_root"); combo(4, "scale_type"); combo(4, "chord");
        toggle(4, "ARP", "arp_on"); combo(4, "arp_div"); combo(4, "arp_pat"); knob(playKnobs, 4, "OCTAVES", "arp_oct"); knob(playKnobs, 4, "GATE", "arp_gate");
        help(4, "SCALE LOCK snaps every key you play into the key and scale, so nothing sounds wrong. CHORD makes one finger play a full chord (in key when Scale Lock is on): perfect for pop progressions. ARP plays the notes you hold one at a time in time with your song.");
        buildGhostExtras();
        buildSampleSlot();
        buildBass();
        buildWobble();
        buildMorph();
        setPage(0);
    }

    // page 9: MORPH BOARD (Shared/SpiritBoard.h + Morph.h)
    ghosty::spirit::BoardView boardView { proc.board };
    juce::TextButton mAddBtn { "ADD THIS SOUND" }, mRandBtn { "RANDOM BOARD" }, mCircleBtn { "ARRANGE IN CIRCLE" }, mApplyBtn { "BLEND TO KNOBS" },
                     mSaveBtn { "SAVE BOARD" }, mLoadBtn { "LOAD BOARD" }, mClearBtn { "CLEAR" };
    juce::ComboBox mOrbit, mWeights; juce::Slider mRate, mDepth; juce::ToggleButton mSync { "SYNC" }; juce::Label mOrbitL, mRateL, mDepthL, mWeightL, mHelp;
    bool puckDragging = false;
    void morphSetXY(float x, float y) {
        for (auto [id, v] : { std::pair<const char*, float> { "morph_x", x }, { "morph_y", y } }) if (auto* p = proc.apvts.getParameter(id)) { p->beginChangeGesture(); p->setValueNotifyingHost(v); p->endChangeGesture(); }
    }
    void morphSourceMenu(std::function<void(const juce::String&)> done) {
        juce::PopupMenu m; m.addItem(1, "This sound (what you hear now)");
        juce::PopupMenu sn; auto& b = proc.snapshots(); for (int i = 0; i < b.size(); ++i) sn.addItem(1000 + i, b.info(i).name);
        m.addSubMenu("A snapshot", sn, b.size() > 0);
        juce::PopupMenu pr; const int N = proc.getNumPrograms(), L = PoltergeistProcessor::numLegacyPresets();
        juce::PopupMenu orig; for (int i = 0; i < L; ++i) orig.addItem(100000 + i, presetName(i)); pr.addSubMenu("ORIGINAL", orig);
        for (auto& folder : gs::kBassFolders) { juce::PopupMenu sub; for (int i = L; i < N; ++i) if (folderOf(i) == folder) sub.addItem(100000 + i, presetName(i)); pr.addSubMenu("BASS: " + folder, sub); }
        m.addSubMenu("A preset", pr);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&boardView).withMousePosition(), [this, done](int r) {
            if (r == 1) done("current");
            else if (r >= 100000) done("preset:" + juce::String(r - 100000));
            else if (r >= 1000) { auto& b = proc.snapshots(); if (r - 1000 < b.size()) done("snap:" + b.info(r - 1000).file.getFileName()); }
        });
    }
    void refreshMorphControls() {
        auto& B = proc.board;
        mOrbit.setSelectedId((int) B.orbit.mode + 1, juce::dontSendNotification); mWeights.setSelectedId((int) B.weights.mode + 1, juce::dontSendNotification);
        mRate.setValue(B.orbit.rate, juce::dontSendNotification); mDepth.setValue(B.orbit.depth, juce::dontSendNotification); mSync.setToggleState(B.orbit.sync, juce::dontSendNotification);
        boardView.refresh();
    }
    void buildMorph() {
        const int pg = 9;
        addAndMakeVisible(boardView); pageComps[pg].push_back(&boardView);
        boardView.palette = []() -> const ghosty::themes::Palette& { return ghosty::themes::get(currentTheme()); };
        boardView.hint = "click = add a ghost with THIS sound   -   drag ghosts / the puck   -   double-click a ghost = hear it alone   -   right-click a ghost: set it from a snapshot / preset";
        boardView.onAddGhost = [this](float x, float y) { if (proc.morphAddGhost(x, y, "current") >= 0) status.setText("A ghost holds this sound now. Change the sound, add another ghost, then drag the puck between them (MORPH ON).", juce::dontSendNotification); };
        boardView.onGhostMoved = [this](int, float, float) { proc.morphPublish(); };
        boardView.onRemoveGhost = [this](int gi) { proc.morphRemoveGhost(gi); };
        boardView.onPuckMoved = [this](float x, float y) { morphSetXY(x, y); };
        boardView.requestScene = [this](int gi) { morphSourceMenu([this, gi](const juce::String& src) { if (proc.morphSetGhost(gi, src)) boardView.refresh(); }); };
        boardView.livePuck = [this] { if (proc.morphActive()) return juce::Point<float>(proc.morph.shared.livePx.load(), proc.morph.shared.livePy.load());
                                      return juce::Point<float>(proc.apvts.getRawParameterValue("morph_x")->load(), proc.apvts.getRawParameterValue("morph_y")->load()); };
        auto* on = toggle(pg, "MORPH ON", "morph_on"); on->setTooltip("MORPH ON: the board plays the blend of its ghosts (off = the knobs' sound)");
        knob(morphKnobs, pg, "X", "morph_x"); knob(morphKnobs, pg, "Y", "morph_y");
        for (auto* b : { &mAddBtn, &mRandBtn, &mCircleBtn, &mApplyBtn, &mSaveBtn, &mLoadBtn, &mClearBtn }) { addAndMakeVisible(b); pageComps[pg].push_back(b); }
        mAddBtn.setTooltip("Add a ghost holding the sound you hear now (or click the board)");
        mAddBtn.onClick = [this] { const float a = juce::Random::getSystemRandom().nextFloat() * 6.283f; if (proc.morphAddGhost(0.5f + 0.32f * std::cos(a), 0.5f + 0.32f * std::sin(a), "current") >= 0) boardView.refresh(); };
        mRandBtn.setTooltip("RANDOM BOARD: scatter the ghosts"); mRandBtn.onClick = [this] { proc.board.randomise((uint32_t) juce::Random::getSystemRandom().nextInt()); proc.morphPublish(); boardView.refresh(); };
        mCircleBtn.setTooltip("ARRANGE IN CIRCLE: the ghosts evenly round the middle"); mCircleBtn.onClick = [this] { proc.board.arrangeInCircle(); proc.morphPublish(); boardView.refresh(); };
        mApplyBtn.setTooltip("BLEND TO KNOBS: the blend at the puck becomes the knobs' sound (MORPH turns off; SAVE / SNAPSHOT it)");
        mApplyBtn.onClick = [this] { proc.morphApplyBlend(); ed.selectGhost(field->selected); smpWave.cached = nullptr; status.setText("The blend is on the knobs now (MORPH is off). SNAPSHOT keeps it.", juce::dontSendNotification); };
        mClearBtn.setTooltip("Remove every ghost"); mClearBtn.onClick = [this] { while (!proc.board.ghosts.empty()) proc.morphRemoveGhost(0); boardView.refresh(); };
        mSaveBtn.setTooltip("Save this board (ghosts + their sounds) under a name"); mLoadBtn.setTooltip("Load a saved board");
        mSaveBtn.onClick = [this] {
            auto* w = new juce::AlertWindow("Save board", "Name it:", juce::MessageBoxIconType::NoIcon); w->addTextEditor("n", "My board"); w->addButton("SAVE", 1, juce::KeyPress(juce::KeyPress::returnKey)); w->addButton("Cancel", 0);
            w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int r) { if (r == 1) status.setText(proc.saveBoard(w->getTextEditorContents("n")) ? "Board saved." : "Couldn't save the board.", juce::dontSendNotification); }), true);
        };
        mLoadBtn.onClick = [this] {
            juce::PopupMenu m; auto files = proc.boardsDir().findChildFiles(juce::File::findFiles, false, "*.ghostboard"); files.sort();
            for (int i = 0; i < files.size(); ++i) m.addItem(i + 1, files[i].getFileNameWithoutExtension());
            if (files.isEmpty()) m.addItem(-1, "No saved boards yet", false);
            m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&mLoadBtn), [this, files](int r) { if (r > 0 && proc.loadBoard(files[r - 1])) { refreshMorphControls(); status.setText("Board loaded.", juce::dontSendNotification); } });
        };
        mOrbit.addItemList({ "Off", "Circle", "Figure 8", "Random walk" }, 1); mWeights.addItemList({ "Smooth", "Islands", "Nearest 3" }, 1);
        mOrbit.setTooltip("ORBIT: the puck moves on its own around where you put it"); mWeights.setTooltip("How the ghosts mix: Smooth (all, by distance), Islands (only near ones), Nearest 3");
        mOrbit.onChange = [this] { proc.board.orbit.mode = (ghosty::spirit::Orbit) (mOrbit.getSelectedId() - 1); proc.morphPublish(); };
        mWeights.onChange = [this] { proc.board.weights.mode = (ghosty::spirit::WeightMode) (mWeights.getSelectedId() - 1); proc.morphPublish(); boardView.refresh(); };
        for (auto* sl : { &mRate, &mDepth }) { sl->setSliderStyle(juce::Slider::LinearHorizontal); sl->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0); sl->setPopupDisplayEnabled(false, false, nullptr); }
        mRate.setRange(0.01, 4.0); mRate.setSkewFactorFromMidPoint(0.5); mDepth.setRange(0.0, 0.5);
        mRate.setTooltip("ORBIT RATE: cycles per beat (SYNC) or per second"); mDepth.setTooltip("ORBIT SIZE");
        mRate.onValueChange = [this] { proc.board.orbit.rate = (float) mRate.getValue(); proc.morphPublish(); };
        mDepth.onValueChange = [this] { proc.board.orbit.depth = (float) mDepth.getValue(); proc.morphPublish(); };
        mSync.setTooltip("SYNC: the orbit follows the song's tempo"); mSync.onClick = [this] { proc.board.orbit.sync = mSync.getToggleState(); proc.morphPublish(); };
        mOrbitL.setText("ORBIT", juce::dontSendNotification); mRateL.setText("RATE", juce::dontSendNotification); mDepthL.setText("SIZE", juce::dontSendNotification); mWeightL.setText("MIX", juce::dontSendNotification);
        mHelp.setText("Each ghost holds a whole sound. MORPH ON plays the blend: knob values glide between the ghosts, switches and the sample follow the closest ghost. X / Y can be automated in your DAW.", juce::dontSendNotification);
        mHelp.setFont(juce::FontOptions(11.5f)); mHelp.setColour(juce::Label::textColourId, GREY); mHelp.setJustificationType(juce::Justification::topLeft);
        for (auto* l : { &mOrbitL, &mRateL, &mDepthL, &mWeightL }) { l->setFont(juce::FontOptions(11.0f, juce::Font::bold)); l->setColour(juce::Label::textColourId, GREY); }
        for (juce::Component* c : { (juce::Component*) &mOrbit, (juce::Component*) &mWeights, (juce::Component*) &mRate, (juce::Component*) &mDepth, (juce::Component*) &mSync,
                                    (juce::Component*) &mOrbitL, (juce::Component*) &mRateL, (juce::Component*) &mDepthL, (juce::Component*) &mWeightL, (juce::Component*) &mHelp }) { addAndMakeVisible(c); pageComps[pg].push_back(c); }
        refreshMorphControls();
    }
    void layoutMorph() {
        boardView.setBounds(18, 134, 680, 444);
        const int x = 708, w = 314;
        if (byId.count("morph_on")) byId["morph_on"]->setBounds(x, 136, 140, 30);
        if (morphKnobs.size() == 2) { morphKnobs[0]->setBounds(x + 150, 132, 76, 84); morphKnobs[1]->setBounds(x + 232, 132, 76, 84); }
        mAddBtn.setBounds(x, 174, 140, 30);
        mRandBtn.setBounds(x, 224, 152, 28); mCircleBtn.setBounds(x + 158, 224, 156, 28);
        mWeightL.setBounds(x, 262, 44, 26); mWeights.setBounds(x + 46, 262, w - 46, 26);
        mOrbitL.setBounds(x, 296, 44, 26); mOrbit.setBounds(x + 46, 296, 170, 26); mSync.setBounds(x + 222, 296, w - 222, 26);
        mRateL.setBounds(x, 328, 44, 22); mRate.setBounds(x + 46, 328, w - 46, 22);
        mDepthL.setBounds(x, 354, 44, 22); mDepth.setBounds(x + 46, 354, w - 46, 22);
        mApplyBtn.setBounds(x, 388, w, 30);
        mSaveBtn.setBounds(x, 426, 104, 28); mLoadBtn.setBounds(x + 108, 426, 104, 28); mClearBtn.setBounds(x + 216, 426, w - 216, 28);
        mHelp.setBounds(x, 464, w, 110);
    }

    void setPage(int pg) {
        if (!pageExists(pg)) pg = 0;   // (old page numbers: the sample pages are gone)
        page = pg;
        proc.engine.wobView.store(pg == 8);   // the wobble's playhead runs while its page is open
        if (pg == 7) { refreshFilterKnobs(); refreshSubKnobs(); fltResp.update(); }
        for (int i = 0; i < kPages; ++i) { if (pageBtns[i]) pageBtns[i]->setToggleState(i == pg, juce::dontSendNotification); for (auto* c : pageComps[i]) c->setVisible(i == pg); }
        if (pg == 0) refreshGhostPanel();
        repaint();
    }
    bool keyPressed(const juce::KeyPress& k) override { return snapBar.handleKey(k); }   // SNAPSHOT (S, Ctrl/Cmd+Z = undo a snapshot delete)
    int statusX = 680, bassHelp = 0;

    // ---- BASS update pages
    void buildGhostExtras() {
        waveBox.addItemList(gs::kWaveNames, 1); waveBox.setTooltip("The ghost's waveform. Classic Morph = the original (WAVE morphs sine > saw > square); the others are band-limited tables and WAVE bends them.");
        loopBtn.setTooltip("Loop the envelope (attack > hold > decay) while the key is held: growls that talk by themselves");
        f2Btn.setTooltip("With the filters on SPLIT, this ghost goes through FILTER 2 instead of FILTER 1");
        for (juce::Component* c : { (juce::Component*) &waveBox, (juce::Component*) &envView, (juce::Component*) &loopBtn, (juce::Component*) &f2Btn }) { addAndMakeVisible(c); pageComps[0].push_back(c); }
        ghostOnly = { &waveBox, &loopBtn, &f2Btn };   // the envelope view stays (it shows the noise envelope too)
        for (auto* k : opKnobs) ghostOnly.push_back(k);
        noiseComps.push_back(combo(0, "nz_type")); noiseComps.push_back(combo(0, "nz_ftype")); noiseComps.push_back(toggle(0, "TO FILTER 2", "nz_f2"));
        for (auto [n, id] : std::vector<std::pair<const char*, const char*>> { { "OUT", "nz_level" }, { "CUTOFF", "nz_cut" }, { "RESO", "nz_res" }, { "ATTACK", "nz_a" }, { "DECAY", "nz_d" }, { "SUSTAIN", "nz_s" }, { "RELEASE", "nz_r" } })
            noiseComps.push_back(knob(noiseKnobs, 0, n, id));
        noiseFltLabel = help(0, "FILTER"); noiseFltLabel->setFont(juce::FontOptions(10.5f, juce::Font::bold)); noiseComps.push_back(noiseFltLabel);
        noiseHint = help(0, "Drag NOISE onto a ghost on the left to make it haunt that ghost (noise FM: grit, breath, crackle inside the growl).");
        noiseHint->setFont(juce::FontOptions(11.0f)); noiseComps.push_back(noiseHint);
        toggle(0, "RETRIG", "mono_retrig")->setTooltip("Mono: every new note restarts the envelopes, the sub and a retriggered wobble (off = overlapping notes glide legato)");
    }
    // ---- ADD SAMPLE: the SAMPLE ghost's panel (the ghost panel's place, while the SAMPLE ghost is selected)
    std::unique_ptr<juce::FileChooser> chooser;
    void chooseSample() {
        chooser = std::make_unique<juce::FileChooser>("ADD SAMPLE: a sound to use as an FM source", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
            if (fc.getResult().existsAsFile()) ed.loadSample(fc.getResult()); });
    }
    void buildSampleSlot() {
        field->onAddSample = [this] { chooseSample(); };
        smpWave.onEmptyClick = [this] { chooseSample(); };
        smpAddBtn.getProperties().set("accent", true);
        smpAddBtn.setTooltip("ADD SAMPLE: pick a WAV / AIFF / FLAC / MP3 (or drop one anywhere on Poltergeist). It becomes an FM source: it HAUNTS a ghost, LEVEL lets you hear it, AS WAVE makes it a ghost's waveform.");
        smpAddBtn.onClick = [this] { chooseSample(); };
        smpClearBtn.setTooltip("Remove the sample (the ghosts keep playing)");
        smpClearBtn.onClick = [this] { proc.clearSample(); proc.detectedRoot = -1; smpWave.cached = nullptr; refreshSampleSlot(); field->repaint(); status.setText("Sample removed.", juce::dontSendNotification); };
        smpName.setFont(juce::FontOptions(12.5f, juce::Font::bold)); smpName.setMinimumHorizontalScale(0.75f);
        smpTag.setFont(juce::FontOptions(10.5f)); smpTag.setColour(juce::Label::textColourId, GREY); smpTag.setMinimumHorizontalScale(0.75f);
        for (auto* l : { &smpRootL, &smpHauntL, &smpWaveL }) { l->setFont(juce::FontOptions(10.5f, juce::Font::bold)); l->setColour(juce::Label::textColourId, GREY); }
        smpRootL.setText("ROOT", juce::dontSendNotification); smpHauntL.setText("HAUNTS", juce::dontSendNotification); smpWaveL.setText("AS WAVE OF", juce::dontSendNotification);
        // ROOT: the key that plays the sample at its own pitch (detected on ADD; pick another to override). Sets PITCH = C3 - ROOT.
        smpRoot.setTooltip("ROOT: the note the sample is in (found when you add it). The key you play then sounds that note. Pick another to override; AUTO finds it again.");
        smpRoot.onChange = [this] {
            const int id = smpRoot.getSelectedId(); if (id <= 0) return;
            if (id == 1000) { if (auto smp = proc.rawSample ? proc.rawSample : std::const_pointer_cast<gs::SampleData>(proc.engine.getSample())) proc.detectedRoot = PoltergeistProcessor::detectRoot(*smp);
                              proc.setSampleRoot(proc.detectedRoot >= 0 ? proc.detectedRoot : 60);
                              status.setText(proc.detectedRoot >= 0 ? "ROOT found: " + keyName(proc.detectedRoot) + "." : "No clear pitch in this sample: ROOT = C3 (it plays as recorded on C3).", juce::dontSendNotification); }
            else proc.setSampleRoot(id - 1);
            refreshSampleSlot(); };
        for (juce::Component* c : { (juce::Component*) &smpWave, (juce::Component*) &smpAddBtn, (juce::Component*) &smpClearBtn, (juce::Component*) &smpRoot, (juce::Component*) &smpName,
                                    (juce::Component*) &smpTag, (juce::Component*) &smpRootL, (juce::Component*) &smpHauntL, (juce::Component*) &smpWaveL }) { addAndMakeVisible(c); pageComps[0].push_back(c); sampleComps.push_back(c); }
        for (auto [n, id, tip] : std::vector<std::tuple<const char*, const char*, const char*>> {
                 { "LEVEL", "smp_level", "LEVEL: how loud you hear the sample itself (it carries the voice). 0 = you only hear what it does to the ghost it haunts." },
                 { "HAUNT", "smp_fm", "HAUNT: how hard the sample modulates its ghost (FM). A vocal or growl here makes the FM voice talk." },
                 { "ATTACK", "smp_a", "The sample's fade-in for each note" },
                 { "RELEASE", "smp_r", "The sample's fade-out after you let go" } }) {
            auto* k = knob(smpKnobs, 0, n, id); k->slider->setTooltip(tip); sampleComps.push_back(k);
        }
        auto* to = combo(0, "smp_fm_to"); to->setTooltip("HAUNTS: the ghost the sample modulates (or drag the SAMPLE ghost onto a ghost). That ghost must be heard (OUT) or haunt one that is.");
        auto* wv = combo(0, "smp_wave_to"); wv->setTooltip("AS WAVE OF: that ghost plays one cycle of the sample as its waveform (START picks which moment; DRIFT x SCAN moves it). Off = its own waveform.");
        wv->onChange = [this] { field->repaint(); };
        sampleComps.push_back(to); sampleComps.push_back(wv);
        for (auto* c : sampleComps) c->setVisible(false);
    }
    void refreshSampleSlot() {
        auto smp = proc.rawSample ? std::shared_ptr<const gs::SampleData>(proc.rawSample) : proc.engine.getSample();
        const bool has = smp != nullptr;
        smpClearBtn.setEnabled(has); smpRoot.setEnabled(has);
        smpAddBtn.setButtonText(has ? "REPLACE" : "ADD SAMPLE");
        smpName.setText(has ? smp->name + "  -  " + juce::String(smp->size() / smp->rate, 2) + " s" : juce::String("No sample yet"), juce::dontSendNotification);
        smpName.setColour(juce::Label::textColourId, has ? INK : GREY);
        juce::StringArray notes;   // old sounds may use what the removed pages set: say so (the sound plays as saved)
        const int mode = (int) rawParam(proc, "smp_mode");
        if (has && mode != 0) notes.add(gs::kSampleModes[juce::jlimit(0, gs::kSampleModes.size() - 1, mode)].toUpperCase() + " mode (saved with this sound)");
        if (rawParam(proc, "mute_sample") > 0.5f) notes.add("sample MUTED (saved with this sound)");
        if (rawParam(proc, "mute_ghosts") > 0.5f) notes.add("ghosts MUTED (saved with this sound)");
        if (rawParam(proc, "smp_rev") > 0.5f && has) notes.add("REVERSED");
        smpTag.setText(notes.isEmpty() ? juce::String("an FM source: HAUNT a ghost (drag the SAMPLE ghost onto it), LEVEL to hear it, AS WAVE for a ghost's waveform")
                                       : notes.joinIntoString("  |  "), juce::dontSendNotification);
        smpTag.setColour(juce::Label::textColourId, notes.isEmpty() ? GREY : textOn(AMBER, CARD));
        smpRoot.clear(juce::dontSendNotification);
        smpRoot.addItem(proc.detectedRoot >= 0 ? "AUTO (found " + keyName(proc.detectedRoot) + ")" : juce::String("AUTO (find it)"), 1000);
        for (int n = 36; n <= 84; ++n) smpRoot.addItem(keyName(n), n + 1);
        const int root = proc.sampleRoot();
        smpRoot.setText(keyName(root) + (root == proc.detectedRoot ? "  (auto)" : ""), juce::dontSendNotification);
        smpWave.cached = nullptr; smpWave.repaint();
    }
    // the panel title in the selected ghost's colour, made readable as text on the panel (>= 4.5:1 in every look)
    void refreshTitleColour() { const int o = field->selected; panelTitle.setColour(juce::Label::textColourId, textOn(o == kSampleGhost ? BLUE : GHOST_COL[juce::jlimit(0, 6, o)], CARD)); }
    void refreshGhostPanel() {
        if (page != 0) return;
        const bool smp = field->selected == kSampleGhost, nz = field->selected == gs::kNoise;
        for (auto* c : ghostOnly) c->setVisible(!nz && !smp);
        for (auto* c : noiseComps) c->setVisible(nz);
        for (auto* c : sampleComps) c->setVisible(smp);
        envView.setVisible(!smp); scope.setVisible(!smp);
        if (smp) refreshSampleSlot();
        else {   // a ghost that plays the sample as its wave: its WAVEFORM menu is not what it plays now
            const bool sw = !nz && (int) rawParam(proc, "smp_wave_to") == field->selected + 1 && (proc.rawSample || proc.engine.getSample());
            waveBox.setEnabled(!sw);
            waveBox.setTooltip(sw ? "This ghost plays one cycle of the SAMPLE as its wave (the SAMPLE ghost's AS WAVE). Set AS WAVE to Off to use a waveform again."
                                  : "The ghost's waveform. Classic Morph = the original (WAVE morphs sine > saw > square); the others are band-limited tables and WAVE bends them.");
        }
        envView.repaint();
    }
    void buildBass() {
        const int pg = 7;
        toggle(pg, "FILTER 1", "f1_on"); combo(pg, "f1_type"); toggle(pg, "FILTER 2", "f2_on"); combo(pg, "f2_type"); combo(pg, "flt_route");
        for (int f = 1; f <= 2; ++f) {
            const juce::String q = "f" + juce::String(f) + "_";
            for (auto [n, k] : std::vector<std::pair<const char*, const char*>> { { "CUTOFF", "cut" }, { "RESO", "res" }, { "DRIVE", "drive" }, { "KEY", "key" }, { "ENV", "env" } })
                knob(bassKnobs, pg, n, (q + k).toRawUTF8());
            if (auto* cb = dynamic_cast<juce::ComboBox*>(byId[(q + "type").toRawUTF8()])) {
                cb->setTooltip("FORMANT: the first knob picks the vowel (a-e-i-o-u); move it with the WOBBLE for the talking growl. COMB: the first knob is the pitch; KEY at 100% makes it follow the note.");
                cb->onChange = [this] { refreshFilterKnobs(); };
            }
        }
        if (auto* cb = dynamic_cast<juce::ComboBox*>(byId["flt_route"])) cb->setTooltip("SERIES: filter 1 into filter 2. PARALLEL: side by side. SPLIT: ghosts marked TO FILTER 2 go through filter 2, the rest through filter 1.");
        addAndMakeVisible(fltResp); pageComps[pg].push_back(&fltResp);
        toggle(pg, "SUB", "sub_on"); toggle(pg, "CLEAN SUB", "sub_clean"); toggle(pg, "SUB SOLO", "sub_solo"); combo(pg, "sub_wave"); combo(pg, "sub_oct"); combo(pg, "sub_env");
        if (auto* cb = dynamic_cast<juce::ComboBox*>(byId["sub_env"])) { cb->setTooltip("SUB FOLLOWS: which envelope shapes the sub. GHOST 1 (as before), LONGEST GHOST (a long sub under a short pluck), or OWN (its ATTACK / RELEASE knobs)."); cb->onChange = [this] { refreshSubKnobs(); }; }
        knob(bassKnobs, pg, "LEVEL", "sub_level"); knob(bassKnobs, pg, "LOW-PASS", "sub_cut"); knob(bassKnobs, pg, "LOW MONO", "mono_below");
        knob(bassKnobs, pg, "ATTACK", "sub_a"); knob(bassKnobs, pg, "RELEASE", "sub_r");
        toggle(pg, "DIST", "dist_on"); combo(pg, "dist_type");
        knob(bassKnobs, pg, "DRIVE", "dist_drive"); knob(bassKnobs, pg, "TONE", "dist_tone"); knob(bassKnobs, pg, "MIX", "dist_mix");
        toggle(pg, "MULTIBAND", "mb_on");
        for (auto [n, id] : std::vector<std::pair<const char*, const char*>> { { "LOW", "mb_lo" }, { "MID", "mb_mid" }, { "HIGH", "mb_hi" }, { "LOW X", "mb_xlo" }, { "HIGH X", "mb_xhi" }, { "MIX", "mb_mix" } })
            knob(bassKnobs, pg, n, id);
        combo(pg, "quality"); combo(pg, "render_q");
        if (auto* cb = dynamic_cast<juce::ComboBox*>(byId["quality"])) cb->setTooltip("How many times faster than your project the voices, FM and drive run while you play. Higher = cleaner highs on hard FM, sync and drive, more CPU.");
        if (auto* cb = dynamic_cast<juce::ComboBox*>(byId["render_q"])) cb->setTooltip("The quality used when your DAW renders / bounces offline (CPU doesn't matter there).");
        bassHelp = helps.size();
        help(pg, "CLEAN SUB skips the filters and every effect. SUB SOLO plays only the sub, to check it. LOW MONO: everything below that frequency is mono. FOLLOWS: which envelope shapes the sub.");
        help(pg, "Order: ghosts > filters > POSSESS > DIST > MULTIBAND > comb > SQUEEZE > crush > space. QUALITY: how many times faster the voices and drive run (cleaner highs, more CPU). RENDER: the quality for bounces.");
        help(pg, "Each band gets its own drive. Leave LOW at 0 to keep the sub clean while the mids scream.");
        help(pg, "QUALITY"); help(pg, "RENDER");
        refreshFilterKnobs(); refreshSubKnobs();
    }
    // the filters' first knob follows the type: CUTOFF (Hz), VOWEL (a-e-i-o-u) for FORMANT, PITCH (note) for the combs
    int shownType[2] { -1, -1 };
    void refreshFilterKnobs() {
        for (int f = 0; f < 2; ++f) {
            const juce::String id = "f" + juce::String(f + 1) + "_cut";
            const int t = (int) rawParam(proc, "f" + juce::String(f + 1) + "_type");
            if (t == shownType[f] || bassKnobs.size() < 10) continue;
            shownType[f] = t;
            auto* k = bassKnobs[f * 5]; auto* prm = proc.apvts.getParameter(id);
            k->ticks.clear(); k->tickValues.clear();
            if (t == gs::BassFilter::Formant) {
                k->label.setText("VOWEL", juce::dontSendNotification);
                k->slider->textFromValueFunction = [](double v) {
                    const double pos = juce::jlimit(0.0, 1.0, std::log2(juce::jmax(20.0, v) / 20.0) / 9.9658) * 4.0;
                    const int i = juce::jmin(3, (int) pos); const double fr = pos - i; const char* V = "AEIOU";
                    if (fr < 0.15) return juce::String::charToString(V[i]);
                    if (fr > 0.85) return juce::String::charToString(V[i + 1]);
                    return juce::String::charToString(V[i]) + " > " + juce::String::charToString(V[i + 1]) + "  " + juce::String(juce::roundToInt(fr * 100)) + " %";
                };
                const char* V[] = { "A", "E", "I", "O", "U" };
                for (int v = 0; v < 5; ++v) { k->ticks.add(V[v]); k->tickValues.push_back(20.0 * std::pow(2.0, v / 4.0 * 9.9658)); }
            } else if (gs::BassFilter::isComb(t)) {
                k->label.setText("PITCH", juce::dontSendNotification);
                k->slider->textFromValueFunction = [](double v) {
                    const int note = (int) std::lround(69.0 + 12.0 * std::log2(juce::jmax(1.0, v) / 440.0));
                    return gs::kNoteNames[((note % 12) + 12) % 12] + juce::String(note / 12 - 1) + "  (" + juce::String(v, v < 100 ? 1 : 0) + " Hz)";
                };
            } else {
                k->label.setText("CUTOFF", juce::dontSendNotification);
                k->slider->textFromValueFunction = [prm](double v) { return prm->getText(prm->convertTo0to1((float) v), 0); };
            }
            k->repaint();
        }
    }
    void refreshSubKnobs() {
        if (bassKnobs.size() < 15) return;
        const bool own = (int) rawParam(proc, "sub_env") == 2;
        bassKnobs[13]->setEnabled(own); bassKnobs[14]->setEnabled(own);
        bassKnobs[13]->setAlpha(own ? 1.0f : 0.35f); bassKnobs[14]->setAlpha(own ? 1.0f : 0.35f);
    }
    void buildWobble() {
        const int pg = 8;
        addAndMakeVisible(wobEd); pageComps[pg].push_back(&wobEd);
        wobEd.onFirstDraw = [this] { autoAssignWobble(); };
        combo(pg, "wob_div"); toggle(pg, "RETRIG", "wob_retrig"); knob(wobKnobs, pg, "SMOOTH", "wob_smooth");
        addAndMakeVisible(wobShapeBtn); pageComps[pg].push_back(&wobShapeBtn);
        wobShapeBtn.onClick = [this] {
            juce::PopupMenu m; const auto& sh = gs::wobShapes();
            for (int i = 0; i < (int) sh.size(); ++i) m.addItem(i + 1, sh[(size_t) i].name);
            m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&wobShapeBtn), [this](int r) {
                if (r <= 0) return; const auto& s = gs::wobShapes()[(size_t) r - 1];
                // one gesture for the whole shape (one undo step in the host)
                std::vector<juce::String> ids { "wob_n", "wob_smooth" };
                for (size_t i = 0; i < s.pts.size(); ++i) { ids.push_back("wob_x" + juce::String((int) i + 1)); ids.push_back("wob_y" + juce::String((int) i + 1)); }
                for (auto& id : ids) if (auto* p = proc.apvts.getParameter(id)) p->beginChangeGesture();
                auto set = [this](const juce::String& id, float v) { if (auto* p = proc.apvts.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(v)); };
                set("wob_n", (float) s.pts.size()); set("wob_smooth", s.smooth);
                for (size_t i = 0; i < s.pts.size(); ++i) { set("wob_x" + juce::String((int) i + 1), s.pts[i].first); set("wob_y" + juce::String((int) i + 1), s.pts[i].second); }
                for (auto& id : ids) if (auto* p = proc.apvts.getParameter(id)) p->endChangeGesture();
                autoAssignWobble();
                wobEd.repaint(); });
        };
        combo(pg, "wob_dst"); knob(wobKnobs, pg, "DEPTH", "wob_amt");
        if (auto* cb = dynamic_cast<juce::ComboBox*>(byId["wob_dst"])) cb->setTooltip("TARGET: what the drawn wobble moves (VOLUME = the shape is the volume). DEPTH: how far. More targets in the MOD MATRIX below.");
        for (int l = 1; l <= 2; ++l) {
            const juce::String q = "lfo" + juce::String(l) + "_";
            knob(wobKnobs, pg, "RATE", (q + "rate").toRawUTF8()); toggle(pg, "SYNC", (q + "sync").toRawUTF8()); combo(pg, (q + "div").toRawUTF8()); combo(pg, (q + "shape").toRawUTF8());
        }
        for (int m = 1; m <= gs::kModSlots; ++m) {
            const juce::String q = "mm" + juce::String(m) + "_";
            combo(pg, (q + "src").toRawUTF8()); combo(pg, (q + "dst").toRawUTF8())->setTooltip("What this slot moves. Cutoffs move up to 6 octaves, Pitch 24 semitones. Ghost Levels scale the ghost's OUT and its FM depth (relative: a ghost at OUT 0 is only heard through the ghosts it haunts). VOLUME: the source is the volume.");
            auto* sl = mmSliders.add(new juce::Slider(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox));
            sl->setColour(juce::Slider::trackColourId, PURPLE); sl->setColour(juce::Slider::thumbColourId, GREEN); sl->setColour(juce::Slider::backgroundColourId, CARD2);
            sl->setPopupDisplayEnabled(true, true, nullptr); sl->setDoubleClickReturnValue(true, 0.0); sl->getProperties().set("bipolar", true);
            sl->setVelocityModeParameters(0.12, 1, 0.0, true, juce::ModifierKeys::shiftModifier);   // Shift = fine (click still jumps to the spot)
            sl->setTooltip("How much: right = positive, left = negative. Click = jump there, Shift = fine, double-click = 0, right-click = type it");
            mmAtts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, q + "amt", *sl));
            addAndMakeVisible(sl); pageComps[pg].push_back(sl);
            auto* nl = mmNums.add(new juce::Label({}, juce::String(m))); nl->setFont(juce::FontOptions(12.0f, juce::Font::bold)); nl->setColour(juce::Label::textColourId, PURPLE);
            addAndMakeVisible(nl); pageComps[pg].push_back(nl);
            auto* vl = mmVals.add(new juce::Label()); vl->setFont(juce::FontOptions(11.0f, juce::Font::bold)); vl->setColour(juce::Label::textColourId, INK);
            vl->setJustificationType(juce::Justification::centredRight); vl->setInterceptsMouseClicks(false, false);
            addAndMakeVisible(vl); pageComps[pg].push_back(vl);
            sl->onValueChange = [sl, vl] { const int pc = juce::roundToInt(sl->getValue() * 100); vl->setText((pc > 0 ? "+" : "") + juce::String(pc) + "%", juce::dontSendNotification); };
            sl->onValueChange();
        }
        wobHelp = helps.size();
        help(pg, "Click to add a point, drag to move it, double-click to remove it. TARGET + DEPTH: what the wobble moves (more in the MOD MATRIX).");
        help(pg, "TARGET");
    }
    // drawing a shape that drives nothing yet: point it somewhere you can hear (the filter cutoff if a filter is on, else the volume)
    bool wobbleUsed() const {
        if (rawParam(proc, "wob_dst") > 0.5f && std::abs(rawParam(proc, "wob_amt")) > 0.001f) return true;
        for (int m = 1; m <= gs::kModSlots; ++m) { const juce::String q = "mm" + juce::String(m) + "_"; if ((int) rawParam(proc, q + "src") == 1 && (int) rawParam(proc, q + "dst") > 0 && std::abs(rawParam(proc, q + "amt")) > 0.001f) return true; }
        return false;
    }
    void autoAssignWobble() {
        if (wobbleUsed()) return;
        const bool f1 = rawParam(proc, "f1_on") > 0.5f, f2 = rawParam(proc, "f2_on") > 0.5f;
        const int dst = f1 && f2 ? gs::mdBothCut : f1 ? gs::mdF1Cut : f2 ? gs::mdF2Cut : gs::mdVolume;
        setParam(proc, "wob_dst", (float) dst); setParam(proc, "wob_amt", dst == gs::mdVolume ? 1.0f : 0.5f);
        status.setText("The wobble now moves " + gs::kModDsts[dst] + " (DEPTH " + (dst == gs::mdVolume ? "100" : "50") + "%). Change TARGET / DEPTH under the shape.", juce::dontSendNotification);
    }
    void layoutBass() {
        auto put = [&](const char* id, int x, int y, int w, int h) { if (byId.count(id)) byId[id]->setBounds(x, y, w, h); };
        const int hb = bassHelp;   // first BASS help label
        // FILTERS
        cards[7].push_back({ { 18, 134, 500, 236 }, "FILTERS" });
        put("flt_route", 404, 138, 104, 22);
        for (int f = 0; f < 2; ++f) {
            const int y = 152 + f * 78;
            put(f == 0 ? "f1_on" : "f2_on", 28, y + 8, 106, 26); put(f == 0 ? "f1_type" : "f2_type", 28, y + 40, 106, 24);
            for (int k = 0; k < 5; ++k) bassKnobs[f * 5 + k]->setBounds(142 + k * 74, y, 72, 78);
        }
        fltResp.setBounds(28, 312, 482, 52);
        // SUB
        cards[7].push_back({ { 528, 134, 494, 236 }, "SUB + LOW END" });
        put("sub_on", 540, 160, 70, 26); put("sub_clean", 616, 160, 108, 26); put("sub_solo", 730, 160, 100, 26);
        put("sub_wave", 540, 194, 106, 24); put("sub_oct", 652, 194, 84, 24); put("sub_env", 742, 194, 124, 24);
        for (int k = 0; k < 5; ++k) bassKnobs[10 + k]->setBounds(538 + k * 72, 226, 70, 90);
        helps[hb]->setBounds(890, 156, 126, 208); helps[hb]->setFont(juce::FontOptions(11.0f));
        // DIST
        cards[7].push_back({ { 18, 378, 500, 200 }, "DIST" });
        put("dist_on", 28, 404, 92, 26); put("dist_type", 28, 436, 120, 24);
        for (int k = 0; k < 3; ++k) bassKnobs[15 + k]->setBounds(158 + k * 80, 398, 76, 92);
        helps[hb + 3]->setBounds(412, 396, 96, 14); helps[hb + 3]->setFont(juce::FontOptions(10.0f, juce::Font::bold)); put("quality", 412, 410, 96, 24);
        helps[hb + 4]->setBounds(412, 440, 96, 14); helps[hb + 4]->setFont(juce::FontOptions(10.0f, juce::Font::bold)); put("render_q", 412, 454, 96, 24);
        helps[hb + 1]->setBounds(28, 500, 480, 72); helps[hb + 1]->setFont(juce::FontOptions(11.5f));
        // MULTIBAND
        cards[7].push_back({ { 528, 378, 494, 200 }, "MULTIBAND DRIVE" });
        put("mb_on", 540, 404, 120, 28);
        for (int k = 0; k < 6; ++k) bassKnobs[18 + k]->setBounds(538 + k * 80, 438, 76, 92);
        helps[hb + 2]->setBounds(668, 400, 346, 34);
    }
    void layoutWobble() {
        auto put = [&](const char* id, int x, int y, int w, int h) { if (byId.count(id)) byId[id]->setBounds(x, y, w, h); };
        cards[8].push_back({ { 18, 134, 640, 280 }, "WOBBLE (TEMPO-SYNCED, DRAWN)" });
        wobEd.setBounds(30, 156, 616, 180);
        put("wob_div", 30, 344, 80, 26); put("wob_retrig", 116, 344, 78, 26); wobShapeBtn.setBounds(200, 344, 74, 26);
        helps[wobHelp + 1]->setBounds(282, 350, 50, 16); helps[wobHelp + 1]->setFont(juce::FontOptions(10.5f, juce::Font::bold)); put("wob_dst", 334, 344, 172, 26);
        wobKnobs[0]->setBounds(522, 336, 62, 76); wobKnobs[1]->setBounds(588, 336, 62, 76);
        helps[wobHelp]->setBounds(30, 374, 486, 36); helps[wobHelp]->setFont(juce::FontOptions(11.5f));
        cards[8].push_back({ { 668, 134, 354, 280 }, "LFO 1 / LFO 2" });
        for (int l = 0; l < 2; ++l) {
            const int y = 158 + l * 126; const juce::String q = "lfo" + juce::String(l + 1) + "_";
            wobKnobs[2 + l]->setBounds(680, y, 80, 100);
            put((q + "sync").toRawUTF8(), 770, y + 10, 70, 28); put((q + "div").toRawUTF8(), 846, y + 10, 84, 28); put((q + "shape").toRawUTF8(), 770, y + 46, 160, 28);
        }
        cards[8].push_back({ { 18, 422, 1004, 156 }, "MOD MATRIX" });
        for (int m = 0; m < gs::kModSlots; ++m) {
            const int col = m % 4, row = m / 4, x = 30 + col * 248, y = 446 + row * 64;
            const juce::String q = "mm" + juce::String(m + 1) + "_";
            mmNums[m]->setBounds(x, y, 16, 26);
            put((q + "src").toRawUTF8(), x + 16, y, 96, 26); put((q + "dst").toRawUTF8(), x + 114, y, 122, 26);
            mmSliders[m]->setBounds(x + 16, y + 28, 176, 22); mmVals[m]->setBounds(x + 192, y + 28, 44, 22);
        }
    }

    // ---- preset browser: folders, favourites, search
    StarButton favBtn;
    ghosty::BypassButton bypassBtn { *proc.bypassParam };   // BYPASS: top right (mutes the whole plugin)
    juce::TextButton themeBtn { "LIGHT" };                  // LIGHT / DARK, next to BYPASS
    std::vector<juce::Component*> panels() {                // the panels that must be opaque in the light theme
        return { field.get(), &scope, &smpWave, &seqEd, &envView, &wobEd, &fltResp };
    }
    // a colour a component was given (setColour) in the old theme -> the same role in the new one (alpha kept)
    static juce::Colour remapColour(juce::Colour c, const Palette& from, const Palette& to) {
        const juce::Colour a[] = { from.bg, from.card, from.card2, from.ink, from.grey, from.dim, from.purple, from.violet, from.green, from.red, from.blue, from.amber, from.pink, from.cyan, from.keyWhite,
                                   from.ghost[0], from.ghost[1], from.ghost[2], from.ghost[3], from.ghost[4], from.ghost[5], from.ghost[6] };
        const juce::Colour b[] = { to.bg, to.card, to.card2, to.ink, to.grey, to.dim, to.purple, to.violet, to.green, to.red, to.blue, to.amber, to.pink, to.cyan, to.keyWhite,
                                   to.ghost[0], to.ghost[1], to.ghost[2], to.ghost[3], to.ghost[4], to.ghost[5], to.ghost[6] };
        for (size_t i = 0; i < std::size(a); ++i) if ((a[i].getARGB() & 0xffffff) == (c.getARGB() & 0xffffff)) return b[i].withAlpha(c.getAlpha());
        return c;
    }
    static void remapTree(juce::Component& c, const Palette& from, const Palette& to, juce::Component* skip) {
        if (&c == skip) return;
        auto& props = c.getProperties(); std::vector<std::pair<int, juce::Colour>> ch;
        for (int i = 0; i < props.size(); ++i) {
            const auto name = props.getName(i).toString();
            if (name.startsWith("jcclr_")) ch.push_back({ name.substring(6).getHexValue32(), juce::Colour((juce::uint32) (int) props.getValueAt(i)) });
        }
        for (auto& [id, col] : ch) c.setColour(id, remapColour(col, from, to));
        for (auto* k : c.getChildren()) remapTree(*k, from, to, skip);
    }
    void applyTheme(ghosty::themes::Theme t) {
        const Palette from = currentPalette();
        setTheme(t); proc.uiTheme = (int) t;
        ed.lnf.applyColours();
        if (from.theme != t) remapTree(ed, from, currentPalette(), nullptr);
        keys.setColour(juce::MidiKeyboardComponent::whiteNoteColourId, KEYWHITE);
        keys.setColour(juce::MidiKeyboardComponent::blackNoteColourId, KEYBLACK);
        keys.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, PURPLE.withAlpha(0.7f));
        keys.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, GREEN.withAlpha(0.3f));
        keys.setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, LINE);
        keys.setColour(juce::MidiKeyboardComponent::textLabelColourId, KEYBLACK);
        for (auto* c : panels()) if (c) c->setOpaque(true);
        search.setTextToShowWhenEmpty("Type a sound... dark trap bell, wobble growl, 808 slide", GREY);   // (the hint colour isn't a colour id: set it again)
        refreshSampleSlot();   // (its note colour is made from the theme's amber)
        refreshTitleColour();
        bypassBtn.setTheme(t);   // (house colours in all three looks)
        themeBtn.setButtonText(ghosty::themes::displayName(ghosty::themes::next(t)));
        snapBar.setTheme(t);
        boardView.repaint();   // names the NEXT look
        ed.repaint(); repaint();
        for (auto* c : getChildren()) c->repaint();
    }
    juce::StringArray favourites;
    juce::File favFile() const { return proc.userDir().getChildFile("favourites.txt"); }
    void loadFavourites() { favourites.clear(); favourites.addLines(favFile().loadFileAsString()); favourites.removeEmptyStrings(); refreshFavButton(); }
    void saveFavourites() { favFile().replaceWithText(favourites.joinIntoString("\n")); }
    juce::String presetName(int i) const { return proc.getProgramName(i); }
    juce::String folderOf(int i) const { const int L = PoltergeistProcessor::numLegacyPresets(); return i < L ? juce::String("ORIGINAL") : juce::String(gs::bassPresets()[(size_t) (i - L)].folder); }
    void refreshFavButton() { favBtn.setToggleState(favourites.contains(proc.patchName), juce::dontSendNotification); favBtn.repaint(); }
    void toggleFavourite() {
        const auto n = proc.patchName; if (n.isEmpty()) return;
        if (favourites.contains(n)) favourites.removeString(n); else favourites.add(n);
        saveFavourites(); refreshFavButton();
        status.setText(favourites.contains(n) ? "\"" + n + "\" is a favourite (top of the preset list)." : "Removed \"" + n + "\" from the favourites.", juce::dontSendNotification);
    }
    void stepPreset(int dir, bool folder) {
        const int N = proc.getNumPrograms(); int i = proc.currentPreset;
        if (!folder) i = (i + dir + N) % N;
        else {   // the first preset of the next / previous folder
            const auto cur = folderOf(i);
            int j = i; do { j = (j + dir + N) % N; } while (folderOf(j) == cur && j != i);
            if (dir < 0) { const auto f = folderOf(j); while (folderOf((j - 1 + N) % N) == f && (j - 1 + N) % N != i) j = (j - 1 + N) % N; }
            i = j;
        }
        proc.loadPreset(i); ed.refreshPresetName();
    }
    static constexpr int kSnapId = 300000;   // preset-menu ids of the SNAPSHOTS bank: kSnapId + index
    juce::PopupMenu snapshotItems(const juce::String& query) {
        juce::PopupMenu sm; auto& b = proc.snapshots(); const auto q = query.toLowerCase();
        for (int i = 0; i < b.size(); ++i) {
            const auto& in = b.info(i);
            if (q.isNotEmpty()) { const auto hay = (in.name + " snapshots snap " + in.note).toLowerCase(); bool all = true; for (auto& w : juce::StringArray::fromTokens(q, " ", "")) if (w.isNotEmpty() && !hay.contains(w)) all = false; if (!all) continue; }
            sm.addItem(kSnapId + i, (in.starred || favourites.contains(in.name) ? "* " : "") + in.name + "    " + juce::Time(in.timeMs).formatted("%d %b %H:%M"), true, i == b.current());
        }
        return sm;
    }
    juce::PopupMenu buildPresetMenu(const juce::String& query) {
        juce::PopupMenu m; const int N = proc.getNumPrograms();
        auto item = [&](juce::PopupMenu& menu, int i) { menu.addItem(i + 1, (favourites.contains(presetName(i)) ? "* " : "") + presetName(i), true, i == proc.currentPreset); };
        if (query.isNotEmpty()) {
            const auto q = query.toLowerCase(); int hits = 0; const int L = PoltergeistProcessor::numLegacyPresets();
            for (int i = 0; i < N; ++i) {
                juce::String hay = presetName(i) + " " + folderOf(i);
                if (i >= L) hay << " " << gs::bassPresets()[(size_t) (i - L)].tags;
                bool all = true; for (auto& w : juce::StringArray::fromTokens(q, " ", "")) if (w.isNotEmpty() && !hay.toLowerCase().contains(w)) all = false;
                if (all) { item(m, i); ++hits; }
            }
            { auto sm = snapshotItems(query); if (sm.getNumItems() > 0) { m.addSeparator(); m.addSectionHeader("SNAPSHOTS"); for (juce::PopupMenu::MenuItemIterator it(sm); it.next();) m.addItem(it.getItem()); hits += sm.getNumItems(); } }
            if (hits == 0) m.addItem(-1, "No preset matches \"" + query + "\"", false);
        } else {
            m.addItem(100000, "SEARCH...");
            { auto sm = snapshotItems({}); if (sm.getNumItems() > 0) m.addSubMenu("SNAPSHOTS (" + juce::String(sm.getNumItems()) + ")", sm); else m.addItem(-3, "SNAPSHOTS (press SNAPSHOT while you tweak)", false); }
            juce::PopupMenu fav; for (int i = 0; i < N; ++i) if (favourites.contains(presetName(i))) item(fav, i);
            if (fav.getNumItems() > 0) m.addSubMenu("FAVOURITES", fav); else m.addItem(-2, "FAVOURITES (click the star to add)", false);
            m.addSeparator();
            juce::PopupMenu orig; for (int i = 0; i < PoltergeistProcessor::numLegacyPresets(); ++i) item(orig, i);
            m.addSubMenu("ORIGINAL", orig);
            for (auto& folder : gs::kBassFolders) { juce::PopupMenu sub; for (int i = PoltergeistProcessor::numLegacyPresets(); i < N; ++i) if (folderOf(i) == folder) item(sub, i); m.addSubMenu("BASS: " + folder, sub); }
        }
        return m;
    }
    void afterSoundLoaded() { ed.refreshPresetName(); smpWave.cached = nullptr; ed.selectGhost(field->selected); snapBar.refresh(); refreshFavButton(); }
    void presetMenuResult(int r) {
        if (r >= kSnapId) { if (snapBar.loadIndex(r - kSnapId)) afterSoundLoaded(); return; }
        if (r > 0 && r < 100000) { proc.loadPreset(r - 1); ed.refreshPresetName(); snapBar.refresh(); }
    }
    void showPresetMenu(const juce::String& query) {
        auto m = buildPresetMenu(query);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presetBtn), [this](int r) {
            if (r == 100000) {
                auto* w = new juce::AlertWindow("Find a preset", "Words from the name, folder or style (e.g. growl, 808, reese dark):", juce::MessageBoxIconType::NoIcon);
                w->addTextEditor("q", {}); w->addButton("FIND", 1, juce::KeyPress(juce::KeyPress::returnKey)); w->addButton("Cancel", 0);
                w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int res) { if (res == 1) { const auto q = w->getTextEditorContents("q").trim(); if (q.isNotEmpty()) showPresetMenu(q); } }), true);
                return;
            }
            presetMenuResult(r);
        });
    }

    juce::String withMods(const juce::String& cat) {
        juce::String s = cat;
        for (auto* b : chips) if (b->getClickingTogglesState() && b->getToggleState()) s += " " + b->getButtonText();
        return s.trim();
    }

    void paint(juce::Graphics& g) override {
        {   // the theme's opaque window gradient (no translucent glows)
            const auto& T = ghosty::themes::get(currentTheme());
            g.setGradientFill(juce::ColourGradient(T.windowTop, 0, 0, T.windowBottom, 0, (float) H, false)); g.fillRect(getLocalBounds());
        }
        auto logo = juce::Rectangle<float>(18, 12, 46, 46);
        g.setColour(GREEN); g.drawEllipse(logo, 2.5f);
        g.setColour(INK); g.fillRoundedRectangle(logo.reduced(12, 10).withTrimmedBottom(4), 6);
        g.setColour(BG); g.fillRect(logo.getCentreX() - 6, logo.getCentreY() - 3, 4.0f, 6.0f); g.fillRect(logo.getCentreX() + 2, logo.getCentreY() - 3, 4.0f, 6.0f);
        g.setColour(INK); g.setFont(juce::FontOptions(28.0f, juce::Font::bold));
        g.drawText("POLTERGEIST", 74, 8, 220, 32, juce::Justification::centredLeft);
        g.setColour(PURPLE); g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
        g.drawText("FM SPIRIT SYNTH - GHOSTBLOCK", 76, 38, 220, 14, juce::Justification::centredLeft);
        g.setColour(GREY); g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText(versionText(), 246, 38, 52, 14, juce::Justification::centredLeft);   // the plugin's version (from the build)
        auto card = [&](juce::Rectangle<int> r, const char* title) {
            g.setColour(CARD); g.fillRoundedRectangle(r.toFloat(), 16);
            g.setColour(LINE); g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 16, 1);
            if (title != nullptr) { g.setColour(GREY); g.setFont(juce::FontOptions(10.5f, juce::Font::bold)); g.drawText(title, r.reduced(14, 6).removeFromTop(14), juce::Justification::centredLeft); }
        };
        if (page == 0) {
            card(panelArea, nullptr);
            card(filterArea, "MAIN FILTER");
            card(pitchArea, "PITCH DROP");
            card(driftArea, "DRIFT (LFO)");
            card(voiceArea, "VOICE");
        }
        for (auto& [r, t] : cards[page]) card(r, t.toRawUTF8());
        card(macroArea, nullptr);
    }

    void resized() override {
        prevBtn.setBounds(300, 16, 28, 34); presetBtn.setBounds(330, 16, 118, 34); favBtn.setBounds(450, 18, 22, 30); nextBtn.setBounds(474, 16, 28, 34);
        saveSnd.setBounds(506, 16, 44, 34); loadSnd.setBounds(552, 16, 44, 34); snapBar.setBounds(600, 16, 104, 34);
        search.setBounds(708, 14, 92, 38); summonBtn.setBounds(804, 14, 62, 38); diceBtn.setBounds(870, 14, 64, 38);
        bypassBtn.setBounds(1022 - 84, 20, 84, 26);                       // BYPASS: header, top right (as in every GhostBlock plugin)
        int x = 18; const int cy = 64;
        for (auto* b : chips) {
            const int w = (int) juce::GlyphArrangement::getStringWidth(juce::FontOptions(12.0f, juce::Font::bold), b->getButtonText()) + 14;
            b->setBounds(x, cy, w, 26); x += w + 3;
        }
        {   // page tabs, then the status line
            static const int TW[kPages] = { 70, 0, 56, 42, 52, 0, 0, 54, 72, 62 };
            int tx = 18; for (int i = 0; i < kPages; ++i) if (pageBtns[i]) { pageBtns[i]->setBounds(tx, 98, TW[i], 28); tx += TW[i] + 4; }
            statusX = tx + 6;
        }
        field->setBounds(18, 134, 540, 310);
        panelArea = { 570, 134, 452, 310 };
        panelTitle.setBounds(panelArea.getX() + 16, panelArea.getY() + 10, 176, 26);
        panelRole.setBounds(panelArea.getX() + 16, panelArea.getY() + 36, 260, 30);
        panelRole.setMinimumHorizontalScale(0.9f);
        // ghost panel: title + waveform, envelope view, 2 rows of 6 knobs, LOOP / TO FILTER 2, scope
        const int px = panelArea.getX(), py = panelArea.getY();
        panelRole.setBounds(0, 0, 0, 0);
        waveBox.setBounds(px + 16, py + 38, 170, 24);
        envView.setBounds(px + 196, py + 8, 244, 58);
        for (int i = 0; i < 12; ++i) opKnobs[i]->setBounds(px + 8 + (i % 6) * 73, py + 70 + (i / 6) * 100, 72, 94);
        loopBtn.setBounds(px + 14, py + 274, 92, 26); f2Btn.setBounds(px + 112, py + 274, 112, 26);
        scope.setBounds(px + 296, py + 270, 144, 32);
        // NOISE panel (same area)
        if (byId.count("nz_type")) { byId["nz_type"]->setBounds(px + 16, py + 38, 170, 24); byId["nz_ftype"]->setBounds(px + 8 + 3 * 73 + 4, py + 106, 138, 26); byId["nz_f2"]->setBounds(px + 112, py + 274, 112, 26); }
        if (noiseFltLabel) noiseFltLabel->setBounds(px + 8 + 3 * 73 + 4, py + 88, 138, 16);
        if (noiseHint) noiseHint->setBounds(px + 8 + 4 * 73 + 4, py + 176, 140, 84);
        for (int i = 0; i < noiseKnobs.size(); ++i) noiseKnobs[i]->setBounds(px + 8 + (i < 3 ? i : i - 3) * 73, py + 70 + (i < 3 ? 0 : 100), 72, 94);
        // ADD SAMPLE panel (same area): ADD / CLEAR + name + ROOT, the waveform, LEVEL / HAUNT / ATTACK / RELEASE, HAUNTS + AS WAVE OF
        smpAddBtn.setBounds(px + 16, py + 38, 112, 24); smpClearBtn.setBounds(px + 132, py + 38, 60, 24);
        smpName.setBounds(px + 196, py + 12, 244, 20);
        smpRootL.setBounds(px + 196, py + 39, 40, 22); smpRoot.setBounds(px + 238, py + 38, 202, 24);
        smpWave.setBounds(px + 16, py + 70, 424, 112);
        smpTag.setBounds(px + 16, py + 184, 424, 16);
        for (int i = 0; i < smpKnobs.size(); ++i) smpKnobs[i]->setBounds(px + 8 + i * 73, py + 204, 72, 94);
        smpHauntL.setBounds(px + 304, py + 206, 136, 14); if (byId.count("smp_fm_to")) byId["smp_fm_to"]->setBounds(px + 304, py + 220, 136, 24);
        smpWaveL.setBounds(px + 304, py + 252, 136, 14); if (byId.count("smp_wave_to")) byId["smp_wave_to"]->setBounds(px + 304, py + 266, 136, 24);
        themeBtn.setBounds(1022 - 62, 99, 62, 26);                        // LIGHT / DARK at the end of the tab row
        unlock.setBounds(1022 - 62 - 6 - 80, 99, 80, 26);                 // UNLOCK (demo only) beside it
        status.setBounds(statusX, 102, 1022 - 62 - 6 - 86 - statusX, 20); status.setMinimumHorizontalScale(0.7f);   // right of the page tabs
        const int sy = 452, sh = 104;
        filterArea = { 18, sy, 330, sh }; pitchArea = { 356, sy, 150, sh }; driftArea = { 514, sy, 238, sh }; voiceArea = { 760, sy, 262, sh };
        fltType.setBounds(filterArea.getRight() - 84, sy + 4, 72, 22);
        for (int i = 0; i < 4; ++i) filterKnobs[i]->setBounds(filterArea.getX() + 10 + i * 78, sy + 26, 72, 74);
        filterKnobs[4]->setBounds(pitchArea.getX() + 4, sy + 26, 72, 74); filterKnobs[5]->setBounds(pitchArea.getX() + 74, sy + 26, 72, 74);
        filterKnobs[6]->setBounds(driftArea.getX() + 6, sy + 26, 72, 74);
        syncBtn.setBounds(driftArea.getX() + 84, sy + 30, 64, 26); lfoDiv.setBounds(driftArea.getX() + 152, sy + 30, 76, 26);
        lfoShape.setBounds(driftArea.getX() + 84, sy + 64, 144, 26);
        monoBtn.setBounds(voiceArea.getRight() - 72, sy + 4, 62, 22);
        if (byId.count("mono_retrig")) byId["mono_retrig"]->setBounds(voiceArea.getRight() - 144, sy + 4, 68, 22);
        for (int i = 0; i < 4; ++i) miscKnobs[i]->setBounds(voiceArea.getX() + 6 + i * 64, sy + 26, 62, 74);
        layoutPages();
        macroArea = { 18, 586, 1004, 92 };
        for (int m = 0; m < 6; ++m) {
            const int mx = macroArea.getX() + 8 + m * 166;
            macroKnobs[m]->setBounds(mx + 4, macroArea.getY() + 4, 70, 84);
            macroHints[m]->setBounds(mx + 76, macroArea.getY() + 20, 86, 52);
        }
        keys.setBounds(18, 684, 1004, 54); keys.setKeyWidth(1004.0f / 36.0f);
    }

    void layoutPages() {
        for (auto& c : cards) c.clear();
        auto put = [&](const char* id, int x, int y, int w, int h) { if (byId.count(id)) byId[id]->setBounds(x, y, w, h); };
        // MOVE
        cards[2].push_back({ { 18, 134, 640, 300 }, "RHYTHM" }); cards[2].push_back({ { 668, 134, 354, 300 }, "TALK (VOWEL FILTER)" }); cards[2].push_back({ { 18, 442, 1004, 136 }, "BUBBLES" });
        seqEd.setBounds(30, 156, 616, 150);
        put("seq_on", 30, 314, 120, 28); put("seq_div", 158, 314, 84, 28); put("seq_dest", 250, 314, 104, 28);
        moveKnobs[0]->setBounds(366, 310, 86, 96); moveKnobs[1]->setBounds(456, 310, 86, 96); moveKnobs[2]->setBounds(546, 310, 86, 96);
        helps[0]->setBounds(30, 350, 330, 80);
        for (int i = 3; i < 7; ++i) moveKnobs[i]->setBounds(680 + (i - 3) * 84, 160, 80, 100);
        helps[1]->setBounds(680, 280, 330, 140);
        for (int i = 7; i < 10; ++i) moveKnobs[i]->setBounds(30 + (i - 7) * 96, 466, 90, 100);
        put("bub_div", 330, 498, 90, 30); helps[2]->setBounds(440, 470, 570, 90);
        // FX
        cards[3].push_back({ { 18, 134, 500, 220 }, "COMB" }); cards[3].push_back({ { 528, 134, 240, 220 }, "SYNC" }); cards[3].push_back({ { 778, 134, 244, 220 }, "SQUEEZE" }); cards[3].push_back({ { 18, 364, 1004, 214 }, "WHAT THEY DO" });
        fxKnobs[0]->setBounds(30, 166, 104, 120); fxKnobs[1]->setBounds(140, 166, 104, 120); fxKnobs[2]->setBounds(250, 166, 104, 120); put("cmb_key", 370, 206, 130, 32);
        fxKnobs[3]->setBounds(590, 160, 120, 140); fxKnobs[4]->setBounds(840, 160, 120, 140);
        helps[3]->setBounds(32, 392, 976, 170);
        // PLAY
        cards[4].push_back({ { 18, 134, 500, 200 }, "SCALE LOCK" }); cards[4].push_back({ { 528, 134, 494, 200 }, "CHORD (ONE FINGER)" }); cards[4].push_back({ { 18, 344, 1004, 234 }, "ARPEGGIATOR" });
        put("scale_on", 32, 170, 150, 34); put("scale_root", 192, 170, 90, 34); put("scale_type", 290, 170, 210, 34);
        put("chord", 544, 170, 200, 34);
        put("arp_on", 32, 380, 100, 34); put("arp_div", 142, 380, 100, 34); put("arp_pat", 252, 380, 150, 34);
        playKnobs[0]->setBounds(420, 366, 96, 104); playKnobs[1]->setBounds(522, 366, 96, 104);
        helps[4]->setBounds(32, 480, 976, 90);
        layoutBass();
        layoutWobble();
        layoutMorph();
    }
};

// ---------------------------------------------------------------------------------------------------------
PoltergeistEditor::PoltergeistEditor(PoltergeistProcessor& p) : AudioProcessorEditor(p), proc(p) {
    setLookAndFeel(&lnf);
    content = std::make_unique<Content>(*this, p);
    addAndMakeVisible(*content);
    content->setBounds(0, 0, W, H);
    // a demo save (user sound / snapshot / board) that left audio Poltergeist made out of the file: the UNLOCK hint says why
    proc.onRenderedLeftOut = [this](const char* id) { ghosty::exportgate::renderedAudioLeftOut(this, id); };
    setResizable(true, true);
    setResizeLimits(W * 3 / 4, H * 3 / 4, W * 2, H * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio((double) W / H);
    setSize(W, H);
    selectGhost(0);
    refreshPresetName();
    content->applyTheme(ghosty::themes::fromInt(proc.uiTheme));   // the project's theme (dark unless it was saved light)
    startTimerHz(30);
}
PoltergeistEditor::~PoltergeistEditor() { proc.onRenderedLeftOut = nullptr; stopTimer(); proc.engine.traceOn.store(false); content.reset(); setLookAndFeel(nullptr); }

void PoltergeistEditor::paint(juce::Graphics& g) { g.fillAll(gsui::BG); }
void PoltergeistEditor::resized() { if (content) content->setTransform(juce::AffineTransform::scale((float) getWidth() / W)); }

void PoltergeistEditor::selectGhost(int op) {
    auto& c = *content;
    op = juce::jlimit(0, kSampleGhost, op);
    c.field->selected = op; if (op <= gs::kNoise) c.envView.ghost = op;
    if (op < gs::kOps) {
        const char* ids[] = { "ratio", "fine", "level", "wave", "fb", "vel", "dly", "a", "hold", "d", "s", "r" };
        for (int i = 0; i < 12; ++i) c.opKnobs[i]->attach(proc.apvts, gs::opId(op, ids[i]));
        c.waveA.reset(); c.waveA = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.apvts, gs::opId(op, "wtype"), c.waveBox);
        c.loopA.reset(); c.loopA = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, gs::opId(op, "loop"), c.loopBtn);
        c.f2A.reset(); c.f2A = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, gs::opId(op, "f2"), c.f2Btn);
    }
    c.panelTitle.setText(op == kSampleGhost ? juce::String("ADD SAMPLE") : op == gs::kNoise ? juce::String("NOISE GHOST") : "GHOST " + juce::String(op + 1), juce::dontSendNotification);
    c.refreshTitleColour();
    c.panelRole.setText("OUT = how loud you hear it  -  RATIO = its pitch", juce::dontSendNotification);
    c.refreshGhostPanel();
    c.field->repaint();
}

void PoltergeistEditor::doSummon(const juce::String& words) {
    const auto cat = proc.summon(words);
    content->status.setText("Summoned a " + cat + (words.trim().isNotEmpty() ? " from \"" + words.trim() + "\"" : "") + "  -  SUMMON again for another take", juce::dontSendNotification);
    refreshPresetName();
}

void PoltergeistEditor::refreshPresetName() { content->presetBtn.setButtonText(proc.patchName); content->refreshFavButton(); content->field->repaint(); }

void PoltergeistEditor::timerCallback() {
    if ((int) currentTheme() != proc.uiTheme) content->applyTheme(ghosty::themes::fromInt(proc.uiTheme));   // (the palette is shared: another instance may have switched it)
    content->field->tick(); content->scope.repaint();
    proc.engine.traceOn.store(content->page == 0 && content->envView.isVisible());   // the tracer runs only while its view is on screen
    if (content->page == 0) content->envView.repaint();
    if (content->page == 8) content->wobEd.repaint();
    if (content->page == 7) { content->refreshFilterKnobs(); content->refreshSubKnobs(); content->fltResp.update(); }
    if (content->page == 0 && content->smpWave.isVisible()) content->smpWave.repaint();   // (the playhead)
    if (content->page == 2) content->seqEd.repaint();
    if (content->page == 9) content->boardView.repaint();
}

// (owner 2026-10-18 / 2026-10-26) drop a WAV / AIFF / FLAC / MP3 anywhere on the plugin -> ADD SAMPLE (the SAMPLE ghost)
static bool isAudio(const juce::String& f) { return juce::File(f).hasFileExtension("wav;aif;aiff;flac;mp3;ogg"); }
bool PoltergeistEditor::isInterestedInFileDrag(const juce::StringArray& files) { for (auto& f : files) if (isAudio(f)) return true; return false; }
void PoltergeistEditor::filesDropped(const juce::StringArray& files, int, int) { for (auto& f : files) if (isAudio(f)) { loadSample(juce::File(f)); return; } }

void PoltergeistEditor::loadSample(const juce::File& f) {
    juce::String err;
    if (!proc.addSampleFile(f, err)) { content->status.setText(err, juce::dontSendNotification); return; }
    sampleAdded();
}
void PoltergeistEditor::sampleAdded() {
    content->smpWave.cached = nullptr;
    content->setPage(0); selectGhost(kSampleGhost);
    const int r = proc.detectedRoot; auto smp = proc.engine.getSample();
    content->status.setText("Added \"" + (smp ? smp->name : juce::String()) + "\"" + (r >= 0 ? " (ROOT " + keyName(r) + ")" : juce::String(" (no clear pitch: ROOT C3)"))
                            + ". It haunts GHOST " + juce::String(proc.apvts.getRawParameterValue("smp_fm_to")->load() + 1.0f, 0) + ": drag the SAMPLE ghost onto another to move it.", juce::dontSendNotification);
}

void PoltergeistEditor::testPresetMenu(const juce::String& query) { content->showPresetMenu(query); }
void PoltergeistEditor::testBypassDark(bool dark) { content->bypassBtn.setDark(dark); }
void PoltergeistEditor::testTheme(ghosty::themes::Theme t) { content->applyTheme(t); }
juce::Button* PoltergeistEditor::testThemeButton() { return &content->themeBtn; }
ghosty::SnapshotBar* PoltergeistEditor::testSnapBar() { return &content->snapBar; }
int PoltergeistEditor::testSnapshotMenuItems(const juce::String& q) {
    int n = 0; auto m = content->buildPresetMenu(q);
    std::function<void(const juce::PopupMenu&)> walk = [&](const juce::PopupMenu& pm) { for (juce::PopupMenu::MenuItemIterator it(pm); it.next();) { auto& i = it.getItem(); if (i.itemID >= Content::kSnapId) ++n; if (i.subMenu) walk(*i.subMenu); } };
    walk(m); return n;
}
void PoltergeistEditor::testPresetMenuResult(int r) { content->presetMenuResult(r); }
bool PoltergeistEditor::testKey(const juce::KeyPress& k) { return content->keyPressed(k); }
juce::String PoltergeistEditor::testPresetName() { return content->presetBtn.getButtonText(); }
void PoltergeistEditor::testPresetStep(int dir) { (dir > 0 ? content->nextBtn : content->prevBtn).onClick(); }
void PoltergeistEditor::testEnvMode(int ghost, bool filter) { selectGhost(ghost); content->envView.filterMode = filter; content->envView.repaint(); }
juce::Slider* PoltergeistEditor::testKnobSlider(const juce::String& paramId) {
    for (auto* k : content->getChildren()) if (auto* kn = dynamic_cast<gsui::Knob*>(k)) if (kn->att && kn->slider && kn->getProperties()["pid"].toString() == paramId) return kn->slider.get();
    return nullptr;
}
std::vector<juce::Component*> PoltergeistEditor::testPanels() { return content->panels(); }
std::vector<juce::String> PoltergeistEditor::testTabs() { std::vector<juce::String> t; for (auto& b : content->pageBtns) if (b) t.push_back(b->getButtonText()); return t; }
juce::Component* PoltergeistEditor::testField() { return content->field.get(); }
juce::Point<float> PoltergeistEditor::testGhostPos(int g) { return content->field->ghostCentre(g); }
juce::Point<float> PoltergeistEditor::testLinkHandle(int from, int to) { return content->field->handleFor(from, to); }
ghosty::WaveRegion* PoltergeistEditor::testSampleWave() { return &content->smpWave.region; }
juce::ComboBox* PoltergeistEditor::testSampleRoot() { return &content->smpRoot; }
juce::Button* PoltergeistEditor::testSampleButton(bool clear) { return clear ? (juce::Button*) &content->smpClearBtn : (juce::Button*) &content->smpAddBtn; }
std::vector<juce::Component*> PoltergeistEditor::testSampleComps() { return content->sampleComps; }
void PoltergeistEditor::testRefresh() { content->refreshSampleSlot(); content->refreshGhostPanel(); }

void PoltergeistEditor::testShow(int page, int, int) { content->setPage(juce::jlimit(0, Content::kPages - 1, page)); }
