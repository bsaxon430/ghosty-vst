#include "PluginEditor.h"

using namespace gsui;
static constexpr int W = 1040, H = 744;

// ---------------------------------------------------------------------------------------------------------
// look and feel
LookAndFeel::LookAndFeel() {
    setColour(juce::Label::textColourId, INK);
    setColour(juce::TextEditor::backgroundColourId, CARD2);
    setColour(juce::TextEditor::textColourId, INK);
    setColour(juce::TextEditor::outlineColourId, PURPLE.withAlpha(0.45f));
    setColour(juce::TextEditor::focusedOutlineColourId, PURPLE);
    setColour(juce::TextEditor::highlightColourId, PURPLE.withAlpha(0.4f));
    setColour(juce::CaretComponent::caretColourId, GREEN);
    setColour(juce::ComboBox::backgroundColourId, CARD2);
    setColour(juce::ComboBox::textColourId, INK);
    setColour(juce::ComboBox::arrowColourId, PURPLE);
    setColour(juce::PopupMenu::backgroundColourId, CARD2);
    setColour(juce::PopupMenu::textColourId, INK);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, PURPLE.withAlpha(0.35f));
    setColour(juce::TextButton::textColourOffId, INK);
    setColour(juce::TextButton::textColourOnId, BG);
    setColour(juce::Slider::textBoxTextColourId, INK);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::ToggleButton::textColourId, INK);
}

void LookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) {
    auto r = juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h).reduced(4);
    const float size = juce::jmin(r.getWidth(), r.getHeight());
    auto c = r.withSizeKeepingCentre(size, size);
    const bool big = s.getProperties()["big"];
    const float thick = big ? 7.0f : 4.0f;
    const float ang = a0 + pos * (a1 - a0);
    if (pos > 0.01f) {
        g.setColour(PURPLE.withAlpha(0.10f + 0.18f * pos));
        g.fillEllipse(c.expanded(big ? 6.0f : 3.0f));
    }
    g.setColour(CARD2);
    g.fillEllipse(c.reduced(thick + 2));
    juce::Path track; track.addCentredArc(c.getCentreX(), c.getCentreY(), size / 2 - thick / 2, size / 2 - thick / 2, 0, a0, a1, true);
    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.strokePath(track, juce::PathStrokeType(thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Path arc; arc.addCentredArc(c.getCentreX(), c.getCentreY(), size / 2 - thick / 2, size / 2 - thick / 2, 0, a0, ang, true);
    juce::ColourGradient grad(PURPLE, c.getX(), c.getBottom(), GREEN, c.getRight(), c.getY(), false);
    g.setGradientFill(grad);
    g.strokePath(arc, juce::PathStrokeType(thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Point<float> ctr = c.getCentre();
    auto tip = ctr.getPointOnCircumference(size / 2 - thick - 5, ang);
    g.setColour(INK);
    g.drawLine({ ctr.getPointOnCircumference(size * 0.12f, ang), tip }, big ? 3.0f : 2.0f);
    if (big) {   // little ghost eyes in the middle of the macro knobs
        g.setColour(BG);
        g.fillRoundedRectangle(ctr.x - 9, ctr.y + 4, 6, 8, 1.5f);
        g.fillRoundedRectangle(ctr.x + 3, ctr.y + 4, 6, 8, 1.5f);
    }
}

void LookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) {
    auto r = b.getLocalBounds().toFloat().reduced(1);
    const bool on = b.getToggleState() || (bool) b.getProperties()["accent"];
    juce::ColourGradient grad(on ? PURPLE.withAlpha(0.9f) : CARD2, r.getX(), r.getY(), on ? VIOLET.withAlpha(0.7f) : CARD, r.getX(), r.getBottom(), false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(r, r.getHeight() * 0.35f);
    g.setColour((on ? GREEN : PURPLE).withAlpha(over ? 0.9f : 0.45f));
    g.drawRoundedRectangle(r, r.getHeight() * 0.35f, down ? 2.0f : 1.2f);
}

void LookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool over, bool) {
    auto r = b.getLocalBounds().toFloat().reduced(2);
    const bool on = b.getToggleState();
    g.setColour(on ? PURPLE.withAlpha(0.85f) : CARD2);
    g.fillRoundedRectangle(r, 8);
    g.setColour((on ? GREEN : PURPLE).withAlpha(over ? 0.9f : 0.5f));
    g.drawRoundedRectangle(r, 8, 1.2f);
    g.setColour(on ? BG : INK);
    g.setFont(juce::FontOptions(12.5f, juce::Font::bold));
    g.drawText(b.getButtonText(), r, juce::Justification::centred);
}

void LookAndFeel::drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&) {
    auto r = juce::Rectangle<float>(0, 0, (float) w, (float) h).reduced(1);
    g.setColour(CARD2); g.fillRoundedRectangle(r, 8);
    g.setColour(PURPLE.withAlpha(0.5f)); g.drawRoundedRectangle(r, 8, 1.2f);
    juce::Path arrow; const float ax = (float) w - 14, ay = (float) h * 0.5f;
    arrow.addTriangle(ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
    g.setColour(PURPLE); g.fillPath(arrow);
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
    slider->setDoubleClickReturnValue(true, s.getParameter(id)->convertFrom0to1(s.getParameter(id)->getDefaultValue()));
}
void Knob::resized() {
    auto r = getLocalBounds();
    label.setBounds(r.removeFromBottom(big ? 20 : 15));
    slider->setBounds(r);
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
// The ghost field: 4 ghosts; drag one onto another to make it haunt (modulate) it.
class gsui::GhostField : public juce::Component, private juce::AudioProcessorValueTreeState::Listener {
public:
    GhostField(PoltergeistProcessor& p, std::function<void(int)> onSelect) : proc(p), select(std::move(onSelect)) {
        body = juce::Drawable::parseSVGPath("M16 31 C16 13 48 13 48 31 V48 Q45 54 42 49 Q39 54 36 49 Q33 54 30 49 Q27 54 24 49 Q21 54 18 49 Q16 52 16 48 Z");
        for (int f = 0; f < 4; ++f) for (int t = 0; t < 4; ++t) if (f != t) proc.apvts.addParameterListener(gs::modId(f, t), this);
        for (int o = 0; o < 4; ++o) { proc.apvts.addParameterListener(gs::opId(o, "level"), this); proc.apvts.addParameterListener(gs::opId(o, "fb"), this); }
    }
    ~GhostField() override {
        for (int f = 0; f < 4; ++f) for (int t = 0; t < 4; ++t) if (f != t) proc.apvts.removeParameterListener(gs::modId(f, t), this);
        for (int o = 0; o < 4; ++o) { proc.apvts.removeParameterListener(gs::opId(o, "level"), this); proc.apvts.removeParameterListener(gs::opId(o, "fb"), this); }
    }
    int selected = 0;

    void resized() override {
        auto r = getLocalBounds().toFloat().reduced(30, 34);
        const float cx = r.getCentreX(), cy = r.getCentreY() + 6;
        const float dx = r.getWidth() * 0.34f, dy = r.getHeight() * 0.36f;
        pos[0] = { cx, cy + dy };
        pos[1] = { cx - dx, cy };
        pos[2] = { cx + dx, cy };
        pos[3] = { cx, cy - dy };
    }

    float val(const juce::String& id) const { return proc.apvts.getRawParameterValue(id)->load(); }

    juce::Point<float> handleFor(int f, int t) const {
        auto a = pos[f], b = pos[t];
        auto mid = (a + b) * 0.5f;
        auto dir = b - a; const float len = juce::jmax(1.0f, dir.getDistanceFromOrigin());
        juce::Point<float> perp(-dir.y / len, dir.x / len);
        return mid + perp * 22.0f;   // A->B and B->A bow to opposite sides
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat();
        g.setColour(CARD.withAlpha(0.85f));
        g.fillRoundedRectangle(bounds, 18);
        g.setColour(PURPLE.withAlpha(0.25f));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 18, 1);
        g.setColour(GREY);
        g.setFont(juce::FontOptions(11.0f));
        g.drawFittedText("Drag a ghost onto another to make it HAUNT it (FM). Drag a link's dot up/down for how hard. Double-click a dot to break it.",
                         bounds.reduced(16, 8).removeFromTop(16).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);

        for (int f = 0; f < 4; ++f)
            for (int t = 0; t < 4; ++t) {
                if (f == t) continue;
                const float a = val(gs::modId(f, t));
                if (a < 0.001f) continue;
                auto h = handleFor(f, t);
                juce::Path p; p.startNewSubPath(pos[f]); p.quadraticTo(h * 2.0f - (pos[f] + pos[t]) * 0.5f, pos[t]);
                const float pulse = 0.6f + 0.4f * std::sin((float) phase * 3.0f + (float) (f * 4 + t));
                g.setColour(GHOST_COL[f].withAlpha(0.18f * pulse));
                g.strokePath(p, juce::PathStrokeType(4.0f + a * 16.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour(GHOST_COL[f].withAlpha(0.85f));
                g.strokePath(p, juce::PathStrokeType(1.5f + a * 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour(BG); g.fillEllipse(juce::Rectangle<float>(20, 20).withCentre(h));
                g.setColour(hoverLink == f * 4 + t ? GREEN : GHOST_COL[f]);
                g.drawEllipse(juce::Rectangle<float>(20, 20).withCentre(h), 2);
                g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
                g.drawText(juce::String(juce::roundToInt(a * 100)), juce::Rectangle<float>(26, 12).withCentre(h), juce::Justification::centred);
            }
        if (dragFrom >= 0 && dragging) {
            juce::Path p; p.startNewSubPath(pos[dragFrom]); p.lineTo(dragPt);
            g.setColour(GREEN.withAlpha(0.5f));
            g.strokePath(p, juce::PathStrokeType(3, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        for (int o = 0; o < 4; ++o) drawGhost(g, o);
    }

    void drawGhost(juce::Graphics& g, int o) {
        const float lvl = proc.engine.meter[(size_t) o].load();
        const float out = val(gs::opId(o, "level"));
        const float fb = val(gs::opId(o, "fb"));
        const float size = 74.0f + lvl * 8.0f;
        const float bob = std::sin((float) phase * 2.0f + (float) o * 1.7f) * 4.0f;
        auto c = pos[o].translated(0, bob);
        auto area = juce::Rectangle<float>(size, size).withCentre(c);
        const auto col = GHOST_COL[o];
        g.setColour(col.withAlpha(0.10f + lvl * 0.35f));
        g.fillEllipse(area.expanded(10 + lvl * 14));
        if (o == selected) { g.setColour(GREEN.withAlpha(0.8f)); g.drawEllipse(area.expanded(6), 2.0f); }
        juce::Path p = body;
        p.applyTransform(juce::AffineTransform::fromTargetPoints(16.0f, 13.0f, area.getX() + area.getWidth() * 0.18f, area.getY() + area.getHeight() * 0.12f,
                                                                 48.0f, 13.0f, area.getRight() - area.getWidth() * 0.18f, area.getY() + area.getHeight() * 0.12f,
                                                                 16.0f, 54.0f, area.getX() + area.getWidth() * 0.18f, area.getBottom() - area.getHeight() * 0.05f));
        g.setColour(col);
        g.fillPath(p);
        g.setColour(BG);
        auto eye = [&](float ex) { g.fillRoundedRectangle(juce::Rectangle<float>(size * 0.09f, size * 0.15f).withCentre({ ex, c.y - size * 0.04f }), 2); };
        eye(c.x - size * 0.11f); eye(c.x + size * 0.11f);
        if (lvl > 0.6f) { g.setColour(BG); g.fillEllipse(juce::Rectangle<float>(size * 0.1f, size * 0.08f * lvl).withCentre({ c.x, c.y + size * 0.12f })); }
        g.setColour(INK); g.setFont(juce::FontOptions(12.5f, juce::Font::bold));
        g.drawText("GHOST " + juce::String(o + 1), juce::Rectangle<float>(120, 16).withCentre({ c.x, area.getBottom() + 10 }), juce::Justification::centred);
        g.setFont(juce::FontOptions(10.5f));
        g.setColour(out > 0.01f ? GREEN : GREY);
        g.drawText(out > 0.01f ? "you hear it" : "haunts others", juce::Rectangle<float>(120, 14).withCentre({ c.x, area.getBottom() + 24 }), juce::Justification::centred);
        if (fb > 0.01f) {
            g.setColour(col.withAlpha(0.7f));
            g.drawEllipse(juce::Rectangle<float>(26, 12).withCentre({ c.x, area.getY() - 2 }), 1.5f + fb * 2);
        }
    }

    int ghostAt(juce::Point<float> p) const { for (int o = 0; o < 4; ++o) if (p.getDistanceFrom(pos[o]) < 42) return o; return -1; }
    int linkAt(juce::Point<float> p) const {
        for (int f = 0; f < 4; ++f) for (int t = 0; t < 4; ++t)
            if (f != t && val(gs::modId(f, t)) > 0.001f && p.getDistanceFrom(handleFor(f, t)) < 13) return f * 4 + t;
        return -1;
    }

    void mouseMove(const juce::MouseEvent& e) override { int l = linkAt(e.position); if (l != hoverLink) { hoverLink = l; repaint(); } }
    void mouseDown(const juce::MouseEvent& e) override {
        dragging = false;
        dragLink = linkAt(e.position);
        if (dragLink >= 0) {
            auto* prm = proc.apvts.getParameter(gs::modId(dragLink / 4, dragLink % 4));
            prm->beginChangeGesture(); dragStartVal = prm->getValue(); return;
        }
        dragFrom = ghostAt(e.position);
        dragPt = e.position;
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (dragLink >= 0) {
            auto* prm = proc.apvts.getParameter(gs::modId(dragLink / 4, dragLink % 4));
            prm->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, dragStartVal - (float) e.getDistanceFromDragStartY() / 160.0f));
            repaint(); return;
        }
        if (dragFrom >= 0) { dragging = e.getDistanceFromDragStart() > 6; dragPt = e.position; repaint(); }
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (dragLink >= 0) { proc.apvts.getParameter(gs::modId(dragLink / 4, dragLink % 4))->endChangeGesture(); dragLink = -1; return; }
        if (dragFrom >= 0) {
            const int to = ghostAt(e.position);
            if (dragging && to >= 0 && to != dragFrom) {
                auto* prm = proc.apvts.getParameter(gs::modId(dragFrom, to));
                if (prm->getValue() < 0.01f) { prm->beginChangeGesture(); prm->setValueNotifyingHost(0.5f); prm->endChangeGesture(); }
            } else if (!dragging) {
                selected = dragFrom; if (select) select(dragFrom);
            }
        }
        dragFrom = -1; dragging = false; repaint();
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override {
        const int l = linkAt(e.position);
        if (l >= 0) { auto* prm = proc.apvts.getParameter(gs::modId(l / 4, l % 4)); prm->beginChangeGesture(); prm->setValueNotifyingHost(0); prm->endChangeGesture(); repaint(); }
    }
    void tick() { phase += 1.0 / 30.0; repaint(); }

private:
    void parameterChanged(const juce::String&, float) override { juce::MessageManager::callAsync([sp = juce::Component::SafePointer<GhostField>(this)] { if (sp) sp->repaint(); }); }
    PoltergeistProcessor& proc;
    std::function<void(int)> select;
    juce::Path body;
    juce::Point<float> pos[4], dragPt;
    int dragFrom = -1, dragLink = -1, hoverLink = -1; bool dragging = false; float dragStartVal = 0;
    double phase = 0;
};

// ---------------------------------------------------------------------------------------------------------
struct Scope : juce::Component {
    explicit Scope(gs::Engine& e) : eng(e) {}
    void paint(juce::Graphics& g) override {
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

// ---------------------------------------------------------------------------------------------------------
// Sample Ghost waveform: shows the sample, the start/length window, the position, slices and the playhead.
// Drag across it to move POSITION (granular / wavetable) or START (classic / slice).
struct WaveView : juce::Component {
    explicit WaveView(PoltergeistProcessor& p) : proc(p) {}
    PoltergeistProcessor& proc;
    const gs::SampleData* cached = nullptr; std::vector<float> peaks;
    float val(const char* id) const { return proc.apvts.getRawParameterValue(id)->load(); }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        g.setColour(CARD2); g.fillRoundedRectangle(r, 14);
        g.setColour(PURPLE.withAlpha(0.35f)); g.drawRoundedRectangle(r.reduced(0.5f), 14, 1);
        auto smp = proc.engine.getSample();
        if (!smp) {
            g.setColour(GREY); g.setFont(juce::FontOptions(15.0f, juce::Font::bold));
            g.drawFittedText("Drop any audio file here (vocal, drum, noise, a clip...)\nor tap LOAD SAMPLE. It becomes a playable sound.", getLocalBounds().reduced(20), juce::Justification::centred, 3);
            return;
        }
        if (cached != smp.get()) {   // recompute the drawing peaks once per sample
            cached = smp.get(); const int W2 = juce::jmax(1, getWidth()); peaks.assign((size_t) W2, 0.0f);
            const int n = smp->size();
            for (int x = 0; x < W2; ++x) { const int a = (int) ((double) x / W2 * n), b = juce::jmax(a + 1, (int) ((double) (x + 1) / W2 * n)); float m = 0; for (int i = a; i < b && i < n; ++i) m = juce::jmax(m, std::abs(smp->data[(size_t) i])); peaks[(size_t) x] = m; }
        }
        const int mode = (int) val("smp_mode");
        const float st = val("smp_start"), ln = val("smp_len"), pos = val("smp_pos");
        const float x0 = r.getX() + st * r.getWidth(), x1 = x0 + ln * (r.getRight() - x0);
        g.setColour(PURPLE.withAlpha(0.12f)); g.fillRect(juce::Rectangle<float>(x0, r.getY(), juce::jmax(1.0f, x1 - x0), r.getHeight()));
        const float mid = r.getCentreY(), hh = r.getHeight() * 0.42f;
        for (int x = 0; x < (int) peaks.size(); ++x) {
            const float px = r.getX() + (float) x; const bool in = px >= x0 && px <= x1;
            g.setColour((in ? GREEN : GREY).withAlpha(in ? 0.85f : 0.35f));
            g.drawVerticalLine((int) px, mid - peaks[(size_t) x] * hh, mid + peaks[(size_t) x] * hh);
        }
        if (mode == 3) {   // slice lines
            const int sl = 4 << (int) val("smp_slices");
            g.setColour(VIOLET.withAlpha(0.7f));
            for (int i = 1; i < sl; ++i) g.drawVerticalLine((int) (r.getX() + r.getWidth() * i / sl), r.getY() + 6, r.getBottom() - 6);
        }
        if (mode == 1 || mode == 2) { const float px = x0 + pos * (x1 - x0); g.setColour(PURPLE); g.fillRect(px - 1.5f, r.getY() + 4, 3.0f, r.getHeight() - 8); }
        const float ph = proc.engine.smpPlayhead.load();
        if (ph >= 0) { g.setColour(INK); g.drawVerticalLine((int) (r.getX() + ph * r.getWidth()), r.getY() + 2, r.getBottom() - 2); }
        g.setColour(INK); g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawText(smp->name + "  -  " + juce::String(smp->size() / smp->rate, 2) + " s", r.reduced(12, 8).removeFromTop(16), juce::Justification::centredLeft);
    }
    void mouseDown(const juce::MouseEvent& e) override { const int mode = (int) val("smp_mode"); dragId = (mode == 1 || mode == 2) ? "smp_pos" : "smp_start"; if (auto* p = proc.apvts.getParameter(dragId)) p->beginChangeGesture(); mouseDrag(e); }
    void mouseDrag(const juce::MouseEvent& e) override {
        auto* p = proc.apvts.getParameter(dragId); if (!p) return;
        float v = juce::jlimit(0.0f, 1.0f, (float) e.x / (float) getWidth());
        if (dragId == juce::String("smp_pos")) { const float st = val("smp_start"), ln = val("smp_len"); const float x0 = st, x1 = st + ln * (1 - st); v = juce::jlimit(0.0f, 1.0f, (v - x0) / juce::jmax(0.001f, x1 - x0)); }
        p->setValueNotifyingHost(p->convertTo0to1(v)); repaint();
    }
    void mouseUp(const juce::MouseEvent&) override { if (auto* p = proc.apvts.getParameter(dragId)) p->endChangeGesture(); }
    juce::String dragId { "smp_start" };
};

// RHYTHM: 16 bars you draw with a finger; the playing step lights up.
struct SeqEditor : juce::Component {
    explicit SeqEditor(PoltergeistProcessor& p) : proc(p) {}
    PoltergeistProcessor& proc; bool touched[16] {};
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        g.setColour(CARD2); g.fillRoundedRectangle(r, 12);
        const int steps = (int) proc.apvts.getRawParameterValue("seq_steps")->load(), now = proc.engine.seqStepNow.load();
        const float w = r.getWidth() / 16.0f;
        for (int i = 0; i < 16; ++i) {
            const float v = proc.apvts.getRawParameterValue("seq_" + juce::String(i + 1))->load();
            auto bar = juce::Rectangle<float>(r.getX() + i * w + 3, r.getY() + 6, w - 6, r.getHeight() - 12);
            g.setColour(juce::Colours::white.withAlpha(0.05f)); g.fillRoundedRectangle(bar, 4);
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
// SAMPLE LAB widgets
static juce::Colour srcColour(int id) { static const juce::Colour C[] = { GREEN, PURPLE, BLUE, juce::Colour(0xfff472b6), juce::Colour(0xfffbbf24), juce::Colour(0xff22d3ee) }; return C[(size_t) (id % 6)]; }

// The source strip: highlight a section (drag), then drag the highlight onto a key, or add it to the timeline.
struct SourceStrip : juce::Component {
    explicit SourceStrip(PoltergeistProcessor& p) : proc(p) {}
    PoltergeistProcessor& proc; double selA = 0, selB = 0; bool dragSel = false; double downSec = 0;
    std::function<void()> onChange;
    const gs::SampleData* src() const { auto it = proc.lab.sources.find(proc.lab.current); return it == proc.lab.sources.end() ? nullptr : it->second.get(); }
    double secAt(float x) const { auto* s = src(); return s ? juce::jlimit(0.0, s->size() / s->rate, (double) x / getWidth() * s->size() / s->rate) : 0.0; }
    float xAt(double sec) const { auto* s = src(); return s ? (float) (sec / (s->size() / s->rate) * getWidth()) : 0.0f; }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        g.setColour(CARD2); g.fillRoundedRectangle(r, 12); g.setColour(PURPLE.withAlpha(0.35f)); g.drawRoundedRectangle(r.reduced(0.5f), 12, 1);
        auto* s = src();
        if (!s) { g.setColour(GREY); g.setFont(juce::FontOptions(14.0f, juce::Font::bold)); g.drawFittedText("Drop audio files here. Highlight a part, then drag it onto a key below, or ADD it to the timeline.", getLocalBounds().reduced(16), juce::Justification::centred, 2); return; }
        const int n = s->size(); const float mid = r.getCentreY(), hh = r.getHeight() * 0.42f;
        if (selB > selA) { g.setColour(GREEN.withAlpha(0.18f)); g.fillRect(juce::Rectangle<float>(xAt(selA), r.getY(), xAt(selB) - xAt(selA), r.getHeight())); }
        for (int x = 0; x < getWidth(); ++x) {
            const int a = (int) ((double) x / getWidth() * n), b = juce::jmax(a + 1, (int) ((double) (x + 1) / getWidth() * n));
            float m = 0; for (int i = a; i < b && i < n; i += juce::jmax(1, (b - a) / 64)) m = juce::jmax(m, std::abs(s->data[(size_t) i]));
            const double sec = (double) a / s->rate; const bool in = selB > selA && sec >= selA && sec <= selB;
            g.setColour(in ? GREEN : srcColour(proc.lab.current).withAlpha(0.7f)); g.drawVerticalLine(x, mid - m * hh, mid + m * hh);
        }
        g.setColour(INK); g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
        juce::String t = s->name + "  " + juce::String(n / s->rate, 2) + " s";
        if (selB > selA) t << "   |   highlight " << juce::String(selA, 2) << " - " << juce::String(selB, 2) << " s  (drag it onto a key)";
        g.drawText(t, r.reduced(10, 6).removeFromTop(14), juce::Justification::centredLeft);
    }
    void mouseDown(const juce::MouseEvent& e) override { downSec = secAt(e.position.x); dragSel = selB > selA && downSec >= selA && downSec <= selB && !e.mods.isShiftDown(); if (!dragSel) { selA = selB = downSec; } }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (dragSel) { if (e.getDistanceFromDragStart() > 8) if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor(this)) { c->startDragging("labsel", this); dragSel = false; } return; }
        const double t = secAt(e.position.x); selA = juce::jmin(downSec, t); selB = juce::jmax(downSec, t); repaint();
    }
    void mouseUp(const juce::MouseEvent&) override { if (onChange) onChange(); }
};

// The keyboard: shows which keys hold Sample Lab sections; drop a highlight onto a key to put it there.
struct DropKeyboard : juce::MidiKeyboardComponent, juce::DragAndDropTarget {
    DropKeyboard(PoltergeistProcessor& p, juce::MidiKeyboardState& st) : juce::MidiKeyboardComponent(st, horizontalKeyboard), proc(p) {}
    PoltergeistProcessor& proc; int hoverNote = -1;
    std::function<void(int)> onDropNote;
    void drawWhiteNote(int note, juce::Graphics& g, juce::Rectangle<float> a, bool down, bool over, juce::Colour line, juce::Colour text) override {
        juce::MidiKeyboardComponent::drawWhiteNote(note, g, a, down, over, line, text);
        if (proc.keyNames[note].isNotEmpty()) { g.setColour(GREEN.withAlpha(0.55f)); g.fillRect(a.withTrimmedTop(a.getHeight() * 0.55f).reduced(2, 2)); }
        if (note == hoverNote) { g.setColour(PURPLE.withAlpha(0.6f)); g.fillRect(a); }
    }
    void drawBlackNote(int note, juce::Graphics& g, juce::Rectangle<float> a, bool down, bool over, juce::Colour fill) override {
        juce::MidiKeyboardComponent::drawBlackNote(note, g, a, down, over, fill);
        if (proc.keyNames[note].isNotEmpty()) { g.setColour(GREEN.withAlpha(0.8f)); g.fillRect(a.withTrimmedTop(a.getHeight() * 0.6f).reduced(2, 1)); }
        if (note == hoverNote) { g.setColour(PURPLE.withAlpha(0.7f)); g.fillRect(a); }
    }
    bool isInterestedInDragSource(const SourceDetails& d) override { return d.description == juce::var("labsel"); }
    void itemDragMove(const SourceDetails& d) override { const int n = getNoteAndVelocityAtPosition(d.localPosition.toFloat()).note; if (n != hoverNote) { hoverNote = n; repaint(); } }
    void itemDragExit(const SourceDetails&) override { hoverNote = -1; repaint(); }
    void itemDropped(const SourceDetails& d) override { const int n = getNoteAndVelocityAtPosition(d.localPosition.toFloat()).note; hoverNote = -1; if (n >= 0 && onDropNote) onDropNote(n); repaint(); }
};

// The timeline: bars of the host tempo; clips you move, trim, stretch, cut and flex.
struct Timeline : juce::Component {
    explicit Timeline(PoltergeistProcessor& p) : proc(p) {}
    PoltergeistProcessor& proc;
    enum Tool { Select, Cut, Stretch, Flex } tool = Select;
    int selected = 0, snapIdx = 4;
    std::function<void()> onEdit;
    enum Drag { None, Move, TrimL, TrimR, StrL, StrR, Mark } drag = None;
    gs::LabClip orig; int markIdx = -1; float downX = 0;
    gs::Lab& lab() { return proc.lab; }
    double snapBeats() const { static const double S[] = { 0, 4, 1, 0.5, 0.25, 1.0 / 3.0 }; return S[juce::jlimit(0, 5, snapIdx)]; }
    double snap(double b) const { const double q = snapBeats(); return q > 0 ? std::round(b / q) * q : b; }
    juce::Rectangle<float> area() const { return getLocalBounds().toFloat().reduced(6, 18).withTrimmedBottom(-12); }
    float xOf(double beat) const { auto r = area(); return r.getX() + (float) (beat / proc.lab.beatsTotal()) * r.getWidth(); }
    double beatOf(float x) const { auto r = area(); return (x - r.getX()) / r.getWidth() * proc.lab.beatsTotal(); }
    gs::LabClip* hit(float x) { for (auto it = lab().clips.rbegin(); it != lab().clips.rend(); ++it) if (x >= xOf(it->start) && x <= xOf(it->start + it->len)) return &*it; return nullptr; }

    void paint(juce::Graphics& g) override {
        auto R = getLocalBounds().toFloat();
        g.setColour(CARD2); g.fillRoundedRectangle(R, 12); g.setColour(PURPLE.withAlpha(0.35f)); g.drawRoundedRectangle(R.reduced(0.5f), 12, 1);
        auto r = area(); const double B = lab().beatsTotal();
        for (int b = 0; b <= (int) B; ++b) {
            const float x = xOf(b); const bool bar = b % 4 == 0;
            g.setColour(juce::Colours::white.withAlpha(bar ? 0.22f : 0.07f)); g.drawVerticalLine((int) x, R.getY() + 14, R.getBottom() - 4);
            if (bar && b < (int) B) { g.setColour(GREY); g.setFont(juce::FontOptions(10.5f, juce::Font::bold)); g.drawText(juce::String(b / 4 + 1), (int) x + 4, (int) R.getY() + 2, 30, 12, juce::Justification::centredLeft); }
        }
        const double bs = lab().beatSec();
        for (auto& c : lab().clips) {
            auto it = lab().sources.find(c.source); if (it == lab().sources.end()) continue;
            const auto& S = *it->second;
            auto cr = juce::Rectangle<float>(xOf(c.start), r.getY(), xOf(c.start + c.len) - xOf(c.start), r.getHeight());
            const auto col = srcColour(c.source);
            g.setColour(col.withAlpha(0.16f)); g.fillRoundedRectangle(cr, 6);
            g.setColour(col.withAlpha(0.85f));
            const float mid = cr.getCentreY(), hh = cr.getHeight() * 0.4f;
            for (int x = (int) cr.getX(); x < (int) cr.getRight(); ++x) {   // waveform through the flex map
                const double outSec = (beatOf((float) x) - c.start) * bs, src = lab().outToSrc(c, outSec);
                const double sec = c.reverse ? c.srcEnd - src : c.srcStart + src;
                const int i = (int) (sec * S.rate); float m = 0;
                for (int k = i; k < i + 220 && k < S.size(); k += 4) if (k >= 0) m = juce::jmax(m, std::abs(S.data[(size_t) k]));
                g.drawVerticalLine(x, mid - m * hh, mid + m * hh);
            }
            for (auto& m : c.marks) { const float mx = xOf(c.start + m.out / bs); g.setColour(juce::Colour(0xfffbbf24)); g.fillRect(mx - 1.0f, cr.getY(), 2.0f, cr.getHeight()); g.fillEllipse(mx - 4, cr.getY() - 4, 8, 8); }
            g.setColour(c.id == selected ? GREEN : col.withAlpha(0.6f)); g.drawRoundedRectangle(cr, 6, c.id == selected ? 2.2f : 1.0f);
            const double ratio = c.len * bs / c.srcLen();
            juce::String tag = S.name; if (std::abs(ratio - 1) > 0.01) tag << "  x" << juce::String(ratio, 2); if (c.pitch != 0) tag << "  " << (c.pitch > 0 ? "+" : "") << juce::String(c.pitch, 0) << " st"; if (c.reverse) tag << "  REV";
            g.setColour(INK); g.setFont(juce::FontOptions(10.5f, juce::Font::bold)); g.drawText(tag, cr.reduced(6, 3).removeFromTop(13), juce::Justification::centredLeft);
        }
        if (lab().clips.empty()) { g.setColour(GREY); g.setFont(juce::FontOptions(13.5f)); g.drawFittedText("Highlight part of a sample above and tap ADD. Then move, trim, STRETCH to fit the bars, CUT, FLEX (drop a line, drag it), DUPLICATE. Overlaps crossfade.", getLocalBounds().reduced(30), juce::Justification::centred, 3); }
        if (proc.previewOn.load() && proc.labRendered) { const float ph = (float) (proc.previewPosUi() / juce::jmax(1, proc.labRendered->size())); g.setColour(INK); g.drawVerticalLine((int) (r.getX() + ph * r.getWidth()), R.getY() + 2, R.getBottom() - 2); }
    }
    void mouseDown(const juce::MouseEvent& e) override {
        downX = e.position.x; drag = None;
        auto* c = hit(e.position.x);
        if (!c) { selected = 0; repaint(); return; }
        selected = c->id; orig = *c;
        const float l = xOf(c->start), rr = xOf(c->start + c->len); const bool nearL = e.position.x - l < 8, nearR = rr - e.position.x < 8;
        const double bs = lab().beatSec();
        if (tool == Cut) {
            const double split = snap(beatOf(e.position.x));
            if (split > c->start + 0.05 && split < c->start + c->len - 0.05) {
                gs::LabClip b = *c; b.id = lab().nextId++;
                const double outCut = (split - c->start) * bs, srcCut = lab().outToSrc(*c, outCut);
                std::vector<gs::LabMarker> ma, mb;
                for (auto& m : c->marks) { if (m.out < outCut) ma.push_back(m); else mb.push_back({ m.src - srcCut, m.out - outCut }); }
                if (!c->reverse) { b.srcStart = c->srcStart + srcCut; c->srcEnd = c->srcStart + srcCut; }
                else { b.srcEnd = c->srcEnd - srcCut; c->srcStart = c->srcEnd - srcCut; }
                b.start = split; b.len = c->start + c->len - split; c->len = split - c->start; c->marks = ma; b.marks = mb;
                lab().clips.push_back(b); selected = b.id; edited();
            }
            return;
        }
        if (tool == Flex) {
            for (size_t i = 0; i < c->marks.size(); ++i) if (std::abs(xOf(c->start + c->marks[i].out / bs) - e.position.x) < 7) { markIdx = (int) i; drag = Mark; return; }
            const double out = (beatOf(e.position.x) - c->start) * bs; c->marks.push_back({ lab().outToSrc(*c, out), out });
            markIdx = (int) c->marks.size() - 1; drag = Mark; orig = *c; repaint(); return;
        }
        drag = (tool == Stretch) ? (nearL ? StrL : nearR ? StrR : Move) : (nearL ? TrimL : nearR ? TrimR : Move);
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        auto* c = lab().clip(selected); if (!c || drag == None) return;
        const double d = beatOf(e.position.x) - beatOf(downX), bs = lab().beatSec(), minLen = 0.0625;
        auto srcDur = [&] { auto it = lab().sources.find(c->source); return it == lab().sources.end() ? 0.0 : it->second->size() / it->second->rate; };
        const double ratio = orig.srcLen() / (orig.len * bs);   // source seconds per timeline second
        switch (drag) {
            case Move: c->start = juce::jmax(0.0, snap(orig.start + d)); break;
            case TrimR: { double ne = juce::jmax(orig.start + minLen, snap(orig.start + orig.len + d)); double nl = ne - orig.start;
                          c->srcEnd = juce::jmin(srcDur(), orig.srcStart + nl * bs * ratio); c->len = (c->srcEnd - c->srcStart) / (bs * ratio);
                          c->marks.clear(); for (auto& m : orig.marks) if (m.out < c->len * bs) c->marks.push_back(m); break; }
            case TrimL: { double ns = juce::jmin(orig.start + orig.len - minLen, snap(orig.start + d)); double delta = ns - orig.start;
                          double nsrc = orig.srcStart + delta * bs * ratio; if (nsrc < 0) { delta -= nsrc / (bs * ratio); nsrc = 0; ns = orig.start + delta; }
                          c->srcStart = nsrc; c->start = ns; c->len = orig.len - delta; c->marks.clear();
                          for (auto& m : orig.marks) if (m.out > delta * bs) c->marks.push_back({ m.src - delta * bs * ratio, m.out - delta * bs }); break; }
            case StrR: { double nl = juce::jmax(minLen, snap(orig.start + orig.len + d) - orig.start); const double k = nl / orig.len; c->len = nl;
                         c->marks = orig.marks; for (auto& m : c->marks) m.out *= k; break; }
            case StrL: { double ns = juce::jmin(orig.start + orig.len - minLen, snap(orig.start + d)); double nl = orig.start + orig.len - ns; const double k = nl / orig.len;
                         c->start = ns; c->len = nl; c->marks = orig.marks; for (auto& m : c->marks) m.out *= k; break; }
            case Mark: { if (markIdx < 0 || markIdx >= (int) c->marks.size()) break;
                         auto pts = c->marks; std::sort(pts.begin(), pts.end(), [](auto& a, auto& b) { return a.src < b.src; });
                         const double src = c->marks[(size_t) markIdx].src; double lo = 0.01, hi = c->len * bs - 0.01;
                         for (auto& m : pts) { if (m.src < src) lo = juce::jmax(lo, m.out + 0.01); if (m.src > src) hi = juce::jmin(hi, m.out - 0.01); }
                         const double grid = snapBeats() > 0 ? snap(c->start + (beatOf(e.position.x) - c->start)) : beatOf(e.position.x);
                         c->marks[(size_t) markIdx].out = juce::jlimit(lo, hi, (grid - c->start) * bs); break; }
            default: break;
        }
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override { if (drag != None) edited(); drag = None; }
    void mouseDoubleClick(const juce::MouseEvent& e) override {   // double-click a flex line to remove it
        if (tool != Flex) return; auto* c = hit(e.position.x); if (!c) return; const double bs = lab().beatSec();
        for (size_t i = 0; i < c->marks.size(); ++i) if (std::abs(xOf(c->start + c->marks[i].out / bs) - e.position.x) < 7) { c->marks.erase(c->marks.begin() + (long) i); edited(); return; }
    }
    void edited() { repaint(); if (onEdit) onEdit(); }
};

// Drag the bounced timeline straight out into FL Studio / any DAW.
struct DragOut : juce::Component {
    explicit DragOut(PoltergeistProcessor& p) : proc(p) {}
    PoltergeistProcessor& proc; bool started = false;
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat().reduced(1);
        g.setColour(GREEN.withAlpha(proc.labRendered ? 0.22f : 0.06f)); g.fillRoundedRectangle(r, 8);
        g.setColour(GREEN.withAlpha(proc.labRendered ? 0.9f : 0.3f)); g.drawRoundedRectangle(r, 8, 1.4f);
        g.setFont(juce::FontOptions(11.5f, juce::Font::bold)); g.drawText("DRAG OUT", r, juce::Justification::centred);
    }
    void mouseDown(const juce::MouseEvent&) override { started = false; }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (started || e.getDistanceFromDragStart() < 6 || !proc.labRendered) return;
        started = true;
        auto f = proc.writeTempWav(*proc.labRendered, "Poltergeist Lab " + juce::Time::getCurrentTime().formatted("%H%M%S"));
        juce::DragAndDropContainer::performExternalDragDropOfFiles({ f.getFullPathName() }, false, this);
    }
};

struct PoltergeistEditor::Content : juce::Component, juce::DragAndDropContainer {
    PoltergeistEditor& ed; PoltergeistProcessor& proc;
    juce::TextEditor search; juce::TextButton summonBtn { "SUMMON" }, diceBtn { "?" }, prevBtn { "<" }, nextBtn { ">" }, presetBtn;
    juce::OwnedArray<juce::TextButton> chips;
    std::unique_ptr<GhostField> field;
    Scope scope;
    juce::Label panelTitle, panelRole, status;
    juce::OwnedArray<Knob> opKnobs, filterKnobs, macroKnobs, miscKnobs;
    juce::ComboBox fltType, lfoDiv, lfoShape;
    juce::ToggleButton monoBtn { "MONO" }, syncBtn { "SYNC" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> fltTypeA, lfoDivA, lfoShapeA;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> monoA, syncA;
    DropKeyboard keys;
    juce::OwnedArray<juce::Label> macroHints;
    juce::Rectangle<int> panelArea, filterArea, pitchArea, driftArea, voiceArea, macroArea;
    // v0.2 pages
    int page = 0;
    juce::OwnedArray<juce::TextButton> pageBtns;
    std::vector<juce::Component*> pageComps[6];
    juce::OwnedArray<Knob> smpKnobs, moveKnobs, fxKnobs, playKnobs;
    juce::OwnedArray<juce::ComboBox> combos;
    juce::OwnedArray<juce::ToggleButton> toggles;
    juce::OwnedArray<juce::Label> helps;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAtts;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> toggleAtts;
    juce::TextButton loadBtn { "LOAD SAMPLE" }, resampleBtn { "RESAMPLE LAST 4 s" }, clearBtn { "CLEAR" };
    WaveView wave { proc }; SeqEditor seqEd { proc };
    std::map<juce::String, juce::Component*> byId;
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> cards[6];
    SourceStrip strip { proc }; Timeline timeline { proc }; DragOut dragOut { proc };
    juce::TextButton prevSrc { "<" }, nextSrc { ">" }, addClipBtn { "ADD TO TIMELINE" }, toKbdBtn { "SET AS KEYBOARD SAMPLE" };
    juce::TextButton toolBtns[4] { juce::TextButton("SELECT"), juce::TextButton("CUT"), juce::TextButton("STRETCH"), juce::TextButton("FLEX") };
    juce::TextButton dupBtn { "DUPLICATE" }, delBtn { "DELETE" }, revBtn { "REV" }, pDown { "-1" }, pUp { "+1" }, toGhostBtn { "TO GHOST" }, clearKeysBtn { "CLEAR KEYS" };
    juce::ToggleButton previewBtn { "PREVIEW" };
    juce::ComboBox snapBox, barsBox;
    juce::Label srcName, keysInfo, labHint;

    Content(PoltergeistEditor& e, PoltergeistProcessor& p)
        : ed(e), proc(p), scope(p.engine), keys(p, p.keyboard) {
        search.setTextToShowWhenEmpty("Type a sound... dark trap bell, wobble growl, 808 slide", GREY);
        search.setFont(juce::FontOptions(15.0f));
        search.setIndents(12, 9);
        search.onReturnKey = [this] { ed.doSummon(search.getText()); };
        addAndMakeVisible(search);
        summonBtn.getProperties().set("accent", true);
        summonBtn.onClick = [this] { ed.doSummon(search.getText()); };
        addAndMakeVisible(summonBtn);
        diceBtn.setTooltip("Surprise me");
        diceBtn.onClick = [this] { search.clear(); ed.doSummon(""); };
        addAndMakeVisible(diceBtn);
        for (auto* b : { &prevBtn, &nextBtn, &presetBtn }) addAndMakeVisible(*b);
        prevBtn.onClick = [this] { proc.loadPreset((proc.currentPreset + proc.getNumPrograms() - 1) % proc.getNumPrograms()); ed.refreshPresetName(); };
        nextBtn.onClick = [this] { proc.loadPreset((proc.currentPreset + 1) % proc.getNumPrograms()); ed.refreshPresetName(); };
        presetBtn.onClick = [this] {
            juce::PopupMenu m; const auto& v = gs::factoryPresets();
            for (int i = 0; i < (int) v.size(); ++i) m.addItem(i + 1, v[(size_t) i].name, true, i == proc.currentPreset);
            m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presetBtn), [this](int r) { if (r > 0) { proc.loadPreset(r - 1); ed.refreshPresetName(); } });
        };
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

        const char* opNames[] = { "RATIO", "FINE", "OUT", "WAVE", "FEEDBACK", "VELOCITY", "ATTACK", "DECAY", "SUSTAIN", "RELEASE" };
        for (int i = 0; i < 10; ++i) {
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

        keys.setColour(juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour(0xffe9e5f5));
        keys.setColour(juce::MidiKeyboardComponent::blackNoteColourId, CARD2);
        keys.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, PURPLE.withAlpha(0.7f));
        keys.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, GREEN.withAlpha(0.3f));
        keys.setAvailableRange(36, 96); keys.setLowestVisibleKey(36);
        addAndMakeVisible(keys);
        buildPages();
    }

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
        const char* names[] = { "GHOSTS", "SAMPLE", "MOVE", "FX", "PLAY", "SAMPLE LAB" };
        for (int i = 0; i < 6; ++i) {
            auto* b = pageBtns.add(new juce::TextButton(names[i])); b->setClickingTogglesState(false);
            b->onClick = [this, i] { setPage(i); }; addAndMakeVisible(b);
        }
        // page 0: the ghosts (existing controls)
        for (juce::Component* c : { (juce::Component*) field.get(), (juce::Component*) &scope, (juce::Component*) &panelTitle, (juce::Component*) &panelRole,
                                    (juce::Component*) &fltType, (juce::Component*) &lfoDiv, (juce::Component*) &lfoShape, (juce::Component*) &monoBtn, (juce::Component*) &syncBtn }) pageComps[0].push_back(c);
        for (auto* k : opKnobs) pageComps[0].push_back(k);
        for (auto* k : filterKnobs) pageComps[0].push_back(k);
        for (auto* k : miscKnobs) pageComps[0].push_back(k);
        // page 1: SAMPLE GHOST
        addAndMakeVisible(wave); pageComps[1].push_back(&wave);
        for (auto* b : { &loadBtn, &resampleBtn, &clearBtn }) { addAndMakeVisible(*b); pageComps[1].push_back(b); }
        loadBtn.getProperties().set("accent", true);
        loadBtn.onClick = [this] {
            chooser = std::make_unique<juce::FileChooser>("Pick a sound for the Sample Ghost", juce::File(), "*.wav;*.aif;*.aiff;*.mp3;*.flac;*.ogg;*.m4a;*.caf");
            chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
                if (fc.getResult().existsAsFile()) ed.loadSample(fc.getResult()); });
        };
        resampleBtn.onClick = [this] { ed.resample(); };
        clearBtn.onClick = [this] { proc.clearSample(); wave.cached = nullptr; wave.repaint(); };
        combo(1, "smp_mode"); combo(1, "smp_slices");
        toggle(1, "KEY TRACK", "smp_track"); toggle(1, "LOOP", "smp_loop"); toggle(1, "REVERSE", "smp_rev");
        for (auto [n, id] : std::vector<std::pair<const char*, const char*>> { { "OUT", "smp_level" }, { "PITCH", "smp_pitch" }, { "START", "smp_start" }, { "LENGTH", "smp_len" },
                 { "POSITION", "smp_pos" }, { "SCAN", "smp_scan" }, { "GRAIN", "smp_grain" }, { "SPRAY", "smp_spray" }, { "DENSITY", "smp_dens" }, { "HAUNT G1", "smp_fm" },
                 { "ATTACK", "smp_a" }, { "DECAY", "smp_d" }, { "SUSTAIN", "smp_s" }, { "RELEASE", "smp_r" } }) knob(smpKnobs, 1, n, id);
        help(1, "CLASSIC plays it on the keys. GRANULAR smears it into clouds (POSITION, GRAIN, SPRAY). WAVETABLE turns it into a growl you sweep with POSITION / SCAN (SCAN moves with the DRIFT LFO). SLICE chops it across the keys for vocal chops. HAUNT G1 lets the sample FM ghost 1. RESAMPLE grabs the last 4 seconds Poltergeist played and loads it back in.");
        // page 2: MOVE (rhythm, talk, bubbles)
        addAndMakeVisible(seqEd); pageComps[2].push_back(&seqEd);
        toggle(2, "RHYTHM ON", "seq_on"); combo(2, "seq_div"); combo(2, "seq_dest");
        knob(moveKnobs, 2, "AMOUNT", "seq_amt"); knob(moveKnobs, 2, "SMOOTH", "seq_glide"); knob(moveKnobs, 2, "STEPS", "seq_steps");
        knob(moveKnobs, 2, "TALK", "vow_amt"); knob(moveKnobs, 2, "VOWEL", "vow_pos"); knob(moveKnobs, 2, "MOVE", "vow_mod"); knob(moveKnobs, 2, "RESO", "vow_res");
        knob(moveKnobs, 2, "BUBBLES", "bub_amt"); knob(moveKnobs, 2, "POP", "bub_pitch"); knob(moveKnobs, 2, "TONE", "bub_tone"); combo(2, "bub_div");
        help(2, "Draw the 16 steps with your finger. TARGET picks what the rhythm moves: GATE (stutter), FILTER, HAUNT (FM growl), TALK (vowels) or PITCH (jumps).");
        help(2, "TALK makes the bass speak (a-e-i-o-u, the 'yoi'). MOVE lets the DRIFT LFO or the rhythm (target TALK) work the mouth. The mod wheel opens it too.");
        help(2, "BUBBLES pops a pitch-dropping blip on every step: bubbly, poppy dubstep sparkle. POP = how far each blip falls.");
        // page 3: FX (comb, sync, OTT)
        knob(fxKnobs, 3, "COMB", "cmb_amt"); knob(fxKnobs, 3, "FEEDBACK", "cmb_fb"); knob(fxKnobs, 3, "TUNE", "cmb_tune"); toggle(3, "FOLLOW KEY", "cmb_key");
        knob(fxKnobs, 3, "SYNC", "sync_amt"); knob(fxKnobs, 3, "OTT", "ott_amt");
        help(3, "COMB = a tuned metal resonance that tears through growls (follows the note you play). SYNC = ghost 1 hard-sync screech: turn it with HAUNT for laser leads. OTT = three-band squash, the loud, bright, in-your-face dubstep finish. Drive, crush and space stay on the big macros below.");
        // page 4: PLAY (scale, chords, arp)
        toggle(4, "SCALE LOCK", "scale_on"); combo(4, "scale_root"); combo(4, "scale_type"); combo(4, "chord");
        toggle(4, "ARP", "arp_on"); combo(4, "arp_div"); combo(4, "arp_pat"); knob(playKnobs, 4, "OCTAVES", "arp_oct"); knob(playKnobs, 4, "GATE", "arp_gate");
        help(4, "SCALE LOCK snaps every key you play into the key and scale, so nothing sounds wrong. CHORD makes one finger play a full chord (in key when Scale Lock is on): perfect for pop progressions. ARP plays the notes you hold one at a time in time with FL.");
        toggle(1, "KEEP TEMPO", "smp_keep");
        buildLab();
        setPage(0);
    }

    // page 5: SAMPLE LAB
    void buildLab() {
        const int pg = 5;
        for (juce::Component* c : { (juce::Component*) &strip, (juce::Component*) &timeline, (juce::Component*) &dragOut, (juce::Component*) &prevSrc, (juce::Component*) &nextSrc,
                                    (juce::Component*) &addClipBtn, (juce::Component*) &toKbdBtn, (juce::Component*) &dupBtn, (juce::Component*) &delBtn, (juce::Component*) &revBtn,
                                    (juce::Component*) &pDown, (juce::Component*) &pUp, (juce::Component*) &toGhostBtn, (juce::Component*) &clearKeysBtn, (juce::Component*) &previewBtn,
                                    (juce::Component*) &snapBox, (juce::Component*) &barsBox, (juce::Component*) &srcName, (juce::Component*) &keysInfo, (juce::Component*) &labHint }) { addAndMakeVisible(c); pageComps[pg].push_back(c); }
        for (int i = 0; i < 4; ++i) { addAndMakeVisible(toolBtns[i]); pageComps[pg].push_back(&toolBtns[i]); toolBtns[i].onClick = [this, i] { timeline.tool = (Timeline::Tool) i; refreshTools(); }; }
        toggle(pg, "ONE-SHOT KEYS", "lab_oneshot");
        srcName.setFont(juce::FontOptions(12.5f, juce::Font::bold)); srcName.setJustificationType(juce::Justification::centred);
        labHint.setFont(juce::FontOptions(11.0f)); labHint.setColour(juce::Label::textColourId, GREY); labHint.setText("drag a highlight onto a key below", juce::dontSendNotification); labHint.setJustificationType(juce::Justification::centred);
        keysInfo.setFont(juce::FontOptions(11.5f)); keysInfo.setColour(juce::Label::textColourId, GREY); keysInfo.setJustificationType(juce::Justification::topLeft); keysInfo.setMinimumHorizontalScale(0.85f);
        snapBox.addItemList({ "Snap off", "Snap bar", "Snap beat", "Snap 1/8", "Snap 1/16", "Snap 1/8T" }, 1); snapBox.setSelectedItemIndex(4, juce::dontSendNotification);
        snapBox.onChange = [this] { timeline.snapIdx = snapBox.getSelectedItemIndex(); };
        barsBox.addItemList({ "1 bar", "2 bars", "4 bars", "8 bars" }, 1);
        barsBox.onChange = [this] { static const int B[] = { 1, 2, 4, 8 }; proc.lab.bars = B[juce::jmax(0, barsBox.getSelectedItemIndex())]; labEdited(); };
        prevSrc.onClick = [this] { stepSource(-1); }; nextSrc.onClick = [this] { stepSource(1); };
        addClipBtn.getProperties().set("accent", true);
        addClipBtn.onClick = [this] {
            if (!(strip.selB > strip.selA + 0.02)) { status.setText("Highlight part of the sample first (drag across it).", juce::dontSendNotification); return; }
            double at = 0; for (auto& c : proc.lab.clips) at = juce::jmax(at, c.start + c.len);
            if (at >= proc.lab.beatsTotal()) at = 0;
            timeline.selected = proc.lab.addClip(proc.lab.current, strip.selA, strip.selB, timeline.snap(at)); labEdited();
        };
        toKbdBtn.onClick = [this] {
            auto s = proc.lab.section(proc.lab.current, strip.selA, strip.selB, srcName.getText() + " cut");
            if (!s) { status.setText("Highlight part of the sample first.", juce::dontSendNotification); return; }
            proc.setSampleGhost(s);
            if (auto* p = proc.apvts.getParameter("smp_level"); p && p->getValue() < 0.01f) p->setValueNotifyingHost(0.9f);
            if (auto* p = proc.apvts.getParameter("smp_mode")) p->setValueNotifyingHost(0.0f);
            status.setText("That highlight now plays across the keyboard. Turn on KEEP TEMPO (SAMPLE page) to pitch it without speeding up.", juce::dontSendNotification);
        };
        dupBtn.onClick = [this] { if (auto* c = proc.lab.clip(timeline.selected)) { auto b = *c; b.id = proc.lab.nextId++; b.start = c->start + c->len; proc.lab.clips.push_back(b); timeline.selected = b.id; labEdited(); } };
        delBtn.onClick = [this] { auto& v = proc.lab.clips; v.erase(std::remove_if(v.begin(), v.end(), [this](auto& c) { return c.id == timeline.selected; }), v.end()); timeline.selected = 0; labEdited(); };
        revBtn.onClick = [this] { if (auto* c = proc.lab.clip(timeline.selected)) { c->reverse = !c->reverse; c->marks.clear(); labEdited(); } };
        pDown.onClick = [this] { if (auto* c = proc.lab.clip(timeline.selected)) { c->pitch = juce::jmax(-24.0f, c->pitch - 1); labEdited(); } };
        pUp.onClick = [this] { if (auto* c = proc.lab.clip(timeline.selected)) { c->pitch = juce::jmin(24.0f, c->pitch + 1); labEdited(); } };
        previewBtn.onClick = [this] { proc.previewOn.store(previewBtn.getToggleState()); if (previewBtn.getToggleState()) proc.labRenderPreview(); };
        toGhostBtn.onClick = [this] {
            if (!proc.labRendered) { status.setText("Build something on the timeline first.", juce::dontSendNotification); return; }
            proc.setSampleGhost(std::make_shared<gs::SampleData>(*proc.labRendered));
            if (auto* p = proc.apvts.getParameter("smp_level"); p && p->getValue() < 0.01f) p->setValueNotifyingHost(0.9f);
            status.setText("Your Lab loop is now the Sample Ghost (SAMPLE page): slice it, granulate it, play it.", juce::dontSendNotification);
        };
        clearKeysBtn.onClick = [this] { proc.labClearKeys(); refreshKeys(); };
        strip.onChange = [this] { };
        timeline.onEdit = [this] { labEdited(); };
        keys.onDropNote = [this](int note) {
            auto s = proc.lab.section(proc.lab.current, strip.selA, strip.selB, srcName.getText() + " " + juce::String(strip.selA, 2) + "s");
            if (!s) return;
            proc.labMapKey(note, s); refreshKeys();
            status.setText("Put on " + juce::MidiMessage::getMidiNoteName(note, true, true, 3) + ". Load another file and keep building - this key stays.", juce::dontSendNotification);
        };
        proc.onLabRendered = [sp = juce::Component::SafePointer<Content>(this)] { if (sp) { sp->dragOut.repaint(); sp->timeline.repaint(); } };
        refreshTools(); refreshLab();
    }
    void refreshTools() { for (int i = 0; i < 4; ++i) toolBtns[i].setToggleState((int) timeline.tool == i, juce::dontSendNotification); }
    void stepSource(int d) {
        auto& S = proc.lab.sources; if (S.empty()) return;
        std::vector<int> ids; for (auto& [id, s] : S) ids.push_back(id);
        auto it = std::find(ids.begin(), ids.end(), proc.lab.current); int i = it == ids.end() ? 0 : (int) (it - ids.begin());
        proc.lab.current = ids[(size_t) ((i + d + (int) ids.size()) % (int) ids.size())]; strip.selA = strip.selB = 0; refreshLab();
    }
    void refreshLab() {
        auto it = proc.lab.sources.find(proc.lab.current);
        srcName.setText(it == proc.lab.sources.end() ? "no sample yet" : it->second->name + "  (" + juce::String(std::distance(proc.lab.sources.begin(), it) + 1) + "/" + juce::String(proc.lab.sources.size()) + ")", juce::dontSendNotification);
        static const int B[] = { 1, 2, 4, 8 }; for (int i = 0; i < 4; ++i) if (B[i] == proc.lab.bars) barsBox.setSelectedItemIndex(i, juce::dontSendNotification);
        refreshKeys(); strip.repaint(); timeline.repaint();
    }
    void refreshKeys() {
        juce::String t; int n = 0;
        for (int i = 0; i < 128; ++i) if (proc.keyNames[i].isNotEmpty()) { if (n++) t << "   "; t << juce::MidiMessage::getMidiNoteName(i, true, true, 3) << " = " << proc.keyNames[i]; }
        keysInfo.setText(n ? "KEYS: " + t : "KEYS: none yet. Highlight part of a sample and drag it onto a key on the keyboard below.", juce::dontSendNotification);
        keys.repaint();
    }
    void labEdited() { timeline.repaint(); proc.labRenderPreview(); }

    void setPage(int pg) {
        page = pg;
        for (int i = 0; i < 6; ++i) { pageBtns[i]->setToggleState(i == pg, juce::dontSendNotification); for (auto* c : pageComps[i]) c->setVisible(i == pg); }
        if (pg == 5) refreshLab();
        repaint();
    }
    std::unique_ptr<juce::FileChooser> chooser;

    juce::String withMods(const juce::String& cat) {
        juce::String s = cat;
        for (auto* b : chips) if (b->getClickingTogglesState() && b->getToggleState()) s += " " + b->getButtonText();
        return s.trim();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(BG);
        g.setGradientFill(juce::ColourGradient(VIOLET.withAlpha(0.22f), W * 0.9f, -40.0f, VIOLET.withAlpha(0.0f), W * 0.9f, 380.0f, true)); g.fillRect(getLocalBounds());
        g.setGradientFill(juce::ColourGradient(GREEN.withAlpha(0.08f), -60.0f, 120.0f, GREEN.withAlpha(0.0f), 360.0f, 120.0f, true)); g.fillRect(getLocalBounds());
        auto logo = juce::Rectangle<float>(18, 12, 46, 46);
        g.setColour(GREEN.withAlpha(0.8f)); g.drawEllipse(logo, 2.5f);
        g.setColour(INK); g.fillRoundedRectangle(logo.reduced(12, 10).withTrimmedBottom(4), 6);
        g.setColour(BG); g.fillRect(logo.getCentreX() - 6, logo.getCentreY() - 3, 4.0f, 6.0f); g.fillRect(logo.getCentreX() + 2, logo.getCentreY() - 3, 4.0f, 6.0f);
        g.setColour(INK); g.setFont(juce::FontOptions(28.0f, juce::Font::bold));
        g.drawText("POLTERGEIST", 74, 8, 270, 32, juce::Justification::centredLeft);
        g.setColour(PURPLE); g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
        g.drawText("FM SPIRIT SYNTH  -  BY GHOSTBLOCK", 76, 38, 270, 14, juce::Justification::centredLeft);
        auto card = [&](juce::Rectangle<int> r, const char* title) {
            g.setColour(CARD.withAlpha(0.85f)); g.fillRoundedRectangle(r.toFloat(), 16);
            g.setColour(PURPLE.withAlpha(0.22f)); g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 16, 1);
            if (title != nullptr) { g.setColour(GREY); g.setFont(juce::FontOptions(10.5f, juce::Font::bold)); g.drawText(title, r.reduced(14, 6).removeFromTop(14), juce::Justification::centredLeft); }
        };
        if (page == 0) {
            card(panelArea, nullptr);
            card(filterArea, "FILTER");
            card(pitchArea, "PITCH DROP");
            card(driftArea, "DRIFT (LFO)");
            card(voiceArea, "VOICE");
        }
        for (auto& [r, t] : cards[page]) card(r, t.toRawUTF8());
        card(macroArea, nullptr);
    }

    void resized() override {
        prevBtn.setBounds(352, 16, 30, 34); presetBtn.setBounds(386, 16, 200, 34); nextBtn.setBounds(590, 16, 30, 34);
        search.setBounds(636, 14, 268, 38); summonBtn.setBounds(910, 14, 82, 38); diceBtn.setBounds(996, 14, 30, 38);
        int x = 18; const int cy = 64;
        for (auto* b : chips) {
            const int w = (int) juce::GlyphArrangement::getStringWidth(juce::FontOptions(12.0f, juce::Font::bold), b->getButtonText()) + 14;
            b->setBounds(x, cy, w, 26); x += w + 3;
        }
        for (int i = 0; i < 6; ++i) pageBtns[i]->setBounds(18 + i * 104, 98, i == 5 ? 118 : 98, 28);
        field->setBounds(18, 134, 540, 310);
        panelArea = { 570, 134, 452, 310 };
        panelTitle.setBounds(panelArea.getX() + 16, panelArea.getY() + 10, 220, 26);
        panelRole.setBounds(panelArea.getX() + 16, panelArea.getY() + 36, 260, 30);
        panelRole.setMinimumHorizontalScale(0.9f);
        scope.setBounds(panelArea.getRight() - 160, panelArea.getY() + 12, 144, 42);
        for (int i = 0; i < 10; ++i) {
            const int row = i / 5, col = i % 5;
            opKnobs[i]->setBounds(panelArea.getX() + 14 + col * 86, panelArea.getY() + 66 + row * 116, 80, 104);
        }
        status.setBounds(560, 102, 462, 20);
        const int sy = 452, sh = 104;
        filterArea = { 18, sy, 330, sh }; pitchArea = { 356, sy, 150, sh }; driftArea = { 514, sy, 238, sh }; voiceArea = { 760, sy, 262, sh };
        fltType.setBounds(filterArea.getRight() - 84, sy + 4, 72, 22);
        for (int i = 0; i < 4; ++i) filterKnobs[i]->setBounds(filterArea.getX() + 10 + i * 78, sy + 26, 72, 74);
        filterKnobs[4]->setBounds(pitchArea.getX() + 4, sy + 26, 72, 74); filterKnobs[5]->setBounds(pitchArea.getX() + 74, sy + 26, 72, 74);
        filterKnobs[6]->setBounds(driftArea.getX() + 6, sy + 26, 72, 74);
        syncBtn.setBounds(driftArea.getX() + 84, sy + 30, 64, 26); lfoDiv.setBounds(driftArea.getX() + 152, sy + 30, 76, 26);
        lfoShape.setBounds(driftArea.getX() + 84, sy + 64, 144, 26);
        monoBtn.setBounds(voiceArea.getRight() - 72, sy + 4, 62, 22);
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
        // SAMPLE
        wave.setBounds(18, 134, 700, 196);
        loadBtn.setBounds(730, 134, 292, 40); resampleBtn.setBounds(730, 180, 292, 34); clearBtn.setBounds(730, 220, 92, 30);
        put("smp_mode", 830, 220, 110, 30); put("smp_slices", 946, 220, 76, 30);
        put("smp_track", 730, 258, 104, 30); put("smp_loop", 840, 258, 80, 30); put("smp_rev", 926, 258, 96, 30); put("smp_keep", 730, 296, 292, 30);
        cards[1].push_back({ { 18, 338, 1004, 240 }, "SOUND" });
        for (int i = 0; i < 10; ++i) smpKnobs[i]->setBounds(26 + i * 99, 356, 92, 96);
        for (int i = 10; i < 14; ++i) smpKnobs[i]->setBounds(26 + (i - 10) * 99, 462, 92, 96);
        helps[0]->setBounds(430, 464, 584, 106);
        // MOVE
        cards[2].push_back({ { 18, 134, 640, 300 }, "RHYTHM" }); cards[2].push_back({ { 668, 134, 354, 300 }, "TALK (VOWEL FILTER)" }); cards[2].push_back({ { 18, 442, 1004, 136 }, "BUBBLES" });
        seqEd.setBounds(30, 156, 616, 150);
        put("seq_on", 30, 314, 120, 28); put("seq_div", 158, 314, 84, 28); put("seq_dest", 250, 314, 104, 28);
        moveKnobs[0]->setBounds(366, 310, 86, 96); moveKnobs[1]->setBounds(456, 310, 86, 96); moveKnobs[2]->setBounds(546, 310, 86, 96);
        helps[1]->setBounds(30, 350, 330, 80);
        for (int i = 3; i < 7; ++i) moveKnobs[i]->setBounds(680 + (i - 3) * 84, 160, 80, 100);
        helps[2]->setBounds(680, 280, 330, 140);
        for (int i = 7; i < 10; ++i) moveKnobs[i]->setBounds(30 + (i - 7) * 96, 466, 90, 100);
        put("bub_div", 330, 498, 90, 30); helps[3]->setBounds(440, 470, 570, 90);
        // FX
        cards[3].push_back({ { 18, 134, 500, 220 }, "COMB" }); cards[3].push_back({ { 528, 134, 240, 220 }, "SYNC" }); cards[3].push_back({ { 778, 134, 244, 220 }, "OTT" }); cards[3].push_back({ { 18, 364, 1004, 214 }, "WHAT THEY DO" });
        fxKnobs[0]->setBounds(30, 166, 104, 120); fxKnobs[1]->setBounds(140, 166, 104, 120); fxKnobs[2]->setBounds(250, 166, 104, 120); put("cmb_key", 370, 206, 130, 32);
        fxKnobs[3]->setBounds(590, 160, 120, 140); fxKnobs[4]->setBounds(840, 160, 120, 140);
        helps[4]->setBounds(32, 392, 976, 170);
        // PLAY
        cards[4].push_back({ { 18, 134, 500, 200 }, "SCALE LOCK" }); cards[4].push_back({ { 528, 134, 494, 200 }, "CHORD (ONE FINGER)" }); cards[4].push_back({ { 18, 344, 1004, 234 }, "ARPEGGIATOR" });
        put("scale_on", 32, 170, 150, 34); put("scale_root", 192, 170, 90, 34); put("scale_type", 290, 170, 210, 34);
        put("chord", 544, 170, 200, 34);
        put("arp_on", 32, 380, 100, 34); put("arp_div", 142, 380, 100, 34); put("arp_pat", 252, 380, 150, 34);
        playKnobs[0]->setBounds(420, 366, 96, 104); playKnobs[1]->setBounds(522, 366, 96, 104);
        helps[5]->setBounds(32, 480, 976, 90);
        // SAMPLE LAB
        strip.setBounds(18, 134, 760, 104);
        srcName.setBounds(786, 134, 236, 20); prevSrc.setBounds(786, 158, 34, 28); addClipBtn.setBounds(824, 158, 160, 28); nextSrc.setBounds(988, 158, 34, 28);
        toKbdBtn.setBounds(786, 190, 236, 24); labHint.setBounds(0, 0, 0, 0);
        previewBtn.setBounds(786, 216, 78, 24); toGhostBtn.setBounds(868, 216, 74, 24); dragOut.setBounds(946, 216, 76, 24);
        int tx = 18; auto tb = [&](juce::Component& c, int w) { c.setBounds(tx, 246, w, 30); tx += w + 4; };
        for (int i = 0; i < 4; ++i) tb(toolBtns[i], i == 2 ? 82 : 66);
        tx += 8; tb(dupBtn, 92); tb(delBtn, 70); tb(revBtn, 48); tb(pDown, 38); tb(pUp, 38);
        tx += 8; tb(snapBox, 120); tb(barsBox, 96);
        timeline.setBounds(18, 284, 1004, 240);
        keysInfo.setBounds(18, 530, 680, 46); clearKeysBtn.setBounds(706, 536, 110, 30); put("lab_oneshot", 822, 536, 200, 30);
    }
};

// ---------------------------------------------------------------------------------------------------------
PoltergeistEditor::PoltergeistEditor(PoltergeistProcessor& p) : AudioProcessorEditor(p), proc(p) {
    setLookAndFeel(&lnf);
    content = std::make_unique<Content>(*this, p);
    addAndMakeVisible(*content);
    content->setBounds(0, 0, W, H);
    setResizable(true, true);
    setResizeLimits(W * 3 / 4, H * 3 / 4, W * 2, H * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio((double) W / H);
    setSize(W, H);
    selectGhost(0);
    refreshPresetName();
    startTimerHz(30);
}
PoltergeistEditor::~PoltergeistEditor() { stopTimer(); content.reset(); setLookAndFeel(nullptr); }

void PoltergeistEditor::paint(juce::Graphics& g) { g.fillAll(gsui::BG); }
void PoltergeistEditor::resized() { if (content) content->setTransform(juce::AffineTransform::scale((float) getWidth() / W)); }

void PoltergeistEditor::selectGhost(int op) {
    auto& c = *content;
    c.field->selected = op;
    const char* ids[] = { "ratio", "fine", "level", "wave", "fb", "vel", "a", "d", "s", "r" };
    for (int i = 0; i < 10; ++i) c.opKnobs[i]->attach(proc.apvts, gs::opId(op, ids[i]));
    c.panelTitle.setText("GHOST " + juce::String(op + 1), juce::dontSendNotification);
    c.panelTitle.setColour(juce::Label::textColourId, gsui::GHOST_COL[op]);
    c.panelRole.setText("OUT = how loud you hear it  -  RATIO = its pitch", juce::dontSendNotification);
    c.field->repaint();
}

void PoltergeistEditor::doSummon(const juce::String& words) {
    const auto cat = proc.summon(words);
    content->status.setText("Summoned a " + cat + (words.trim().isNotEmpty() ? " from \"" + words.trim() + "\"" : "") + "  -  SUMMON again for another take", juce::dontSendNotification);
    refreshPresetName();
}

void PoltergeistEditor::refreshPresetName() { content->presetBtn.setButtonText(proc.patchName); content->field->repaint(); }

void PoltergeistEditor::timerCallback() {
    content->field->tick(); content->scope.repaint();
    if (content->page == 1) content->wave.repaint();
    if (content->page == 2) content->seqEd.repaint();
}

// (owner 2026-10-18) drop any audio file on the plugin -> Sample Ghost
static bool isAudio(const juce::String& f) { return juce::File(f).hasFileExtension("wav;aif;aiff;mp3;flac;ogg;m4a;caf;aac"); }
bool PoltergeistEditor::isInterestedInFileDrag(const juce::StringArray& files) { for (auto& f : files) if (isAudio(f)) return true; return false; }
void PoltergeistEditor::filesDropped(const juce::StringArray& files, int, int) { for (auto& f : files) if (isAudio(f)) { loadSample(juce::File(f)); return; } }

void PoltergeistEditor::loadSample(const juce::File& f) {
    juce::String err;
    if (!proc.loadSampleFile(f, err)) { content->status.setText(err, juce::dontSendNotification); return; }
    if (auto* p = proc.apvts.getParameter("smp_level"); p && p->getValue() < 0.01f) { p->beginChangeGesture(); p->setValueNotifyingHost(0.9f); p->endChangeGesture(); }
    content->wave.cached = nullptr;
    content->setPage(1);
    content->status.setText("Loaded \"" + f.getFileNameWithoutExtension() + "\". Play it! Try GRANULAR, WAVETABLE or SLICE, or type \"chop\" / \"granular\" in Summon.", juce::dontSendNotification);
}

void PoltergeistEditor::resample() {
    if (!proc.resampleRecent(4.0)) { content->status.setText("Play something first: RESAMPLE grabs the last 4 seconds Poltergeist played.", juce::dontSendNotification); return; }
    if (auto* p = proc.apvts.getParameter("smp_level"); p && p->getValue() < 0.01f) { p->beginChangeGesture(); p->setValueNotifyingHost(0.9f); p->endChangeGesture(); }
    content->wave.cached = nullptr; content->setPage(1);
    content->status.setText("Resampled! Your last 4 seconds are now the Sample Ghost. Mangle it again.", juce::dontSendNotification);
}
