#include "PluginEditor.h"

using namespace gsui;
static constexpr int W = 1040, H = 712;

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
struct PoltergeistEditor::Content : juce::Component {
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
    juce::MidiKeyboardComponent keys;
    juce::OwnedArray<juce::Label> macroHints;
    juce::Rectangle<int> panelArea, filterArea, pitchArea, driftArea, voiceArea, macroArea;

    Content(PoltergeistEditor& e, PoltergeistProcessor& p)
        : ed(e), proc(p), scope(p.engine), keys(p.keyboard, juce::MidiKeyboardComponent::horizontalKeyboard) {
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
    }

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
        card(panelArea, nullptr);
        card(filterArea, "FILTER");
        card(pitchArea, "PITCH DROP");
        card(driftArea, "DRIFT (LFO)");
        card(voiceArea, "VOICE");
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
        field->setBounds(18, 100, 540, 332);
        panelArea = { 570, 100, 452, 332 };
        panelTitle.setBounds(panelArea.getX() + 16, panelArea.getY() + 10, 220, 26);
        panelRole.setBounds(panelArea.getX() + 16, panelArea.getY() + 36, 260, 30);
        panelRole.setMinimumHorizontalScale(0.9f);
        scope.setBounds(panelArea.getRight() - 160, panelArea.getY() + 12, 144, 42);
        for (int i = 0; i < 10; ++i) {
            const int row = i / 5, col = i % 5;
            opKnobs[i]->setBounds(panelArea.getX() + 14 + col * 86, panelArea.getY() + 72 + row * 122, 80, 108);
        }
        status.setBounds(panelArea.getX() + 16, panelArea.getBottom() - 22, 420, 16);
        const int sy = 442, sh = 104;
        filterArea = { 18, sy, 330, sh }; pitchArea = { 356, sy, 150, sh }; driftArea = { 514, sy, 238, sh }; voiceArea = { 760, sy, 262, sh };
        fltType.setBounds(filterArea.getRight() - 84, sy + 4, 72, 22);
        for (int i = 0; i < 4; ++i) filterKnobs[i]->setBounds(filterArea.getX() + 10 + i * 78, sy + 26, 72, 74);
        filterKnobs[4]->setBounds(pitchArea.getX() + 4, sy + 26, 72, 74); filterKnobs[5]->setBounds(pitchArea.getX() + 74, sy + 26, 72, 74);
        filterKnobs[6]->setBounds(driftArea.getX() + 6, sy + 26, 72, 74);
        syncBtn.setBounds(driftArea.getX() + 84, sy + 30, 64, 26); lfoDiv.setBounds(driftArea.getX() + 152, sy + 30, 76, 26);
        lfoShape.setBounds(driftArea.getX() + 84, sy + 64, 144, 26);
        monoBtn.setBounds(voiceArea.getRight() - 72, sy + 4, 62, 22);
        for (int i = 0; i < 4; ++i) miscKnobs[i]->setBounds(voiceArea.getX() + 6 + i * 64, sy + 26, 62, 74);
        macroArea = { 18, 554, 1004, 92 };
        for (int m = 0; m < 6; ++m) {
            const int mx = macroArea.getX() + 8 + m * 166;
            macroKnobs[m]->setBounds(mx + 4, macroArea.getY() + 4, 70, 84);
            macroHints[m]->setBounds(mx + 76, macroArea.getY() + 20, 86, 52);
        }
        keys.setBounds(18, 652, 1004, 52); keys.setKeyWidth(1004.0f / 36.0f);
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

void PoltergeistEditor::timerCallback() { content->field->tick(); content->scope.repaint(); }
