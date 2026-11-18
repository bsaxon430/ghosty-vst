#include "GhostVoicePage.h"
#include "ExportGate.h"

using namespace fusion;
namespace {
// ---- palette (the house tokens, applied at the start of every paint so two pages with different looks never recolour each other)
juce::Colour BG, CARD, CARD2, INK, GREY, DIM, PURPLE, GREEN, PINK, AMBER, CYAN, RED, BLUE, GLOW;
bool darkTheme = true; ghosty::themes::Theme curLook = ghosty::themes::Theme::Dark; bool paletteInit = false;
void applyTheme(ghosty::themes::Theme look) {
    const auto& T = ghosty::themes::get(look); curLook = look; darkTheme = !T.isLight; paletteInit = true;
    auto rd = [&](juce::Colour c) { return ghosty::themes::readableOn(ghosty::themes::readableOn(c, T.panel, 3.0), T.card, 3.0); };
    BG = T.window; CARD = T.panel; CARD2 = T.cell; INK = T.ink; GREY = T.dim; DIM = T.line;
    PURPLE = rd(T.violet); GREEN = rd(T.accent3); PINK = rd(T.accent1); AMBER = rd(T.warn); CYAN = rd(T.accent2); RED = rd(T.bad);
    BLUE = rd(T.isLight ? juce::Colour(0xff2456c8) : juce::Colour(0xff6aa8ff)); GLOW = T.isLight ? T.violet : T.accent2;
}
juce::Colour onFill(juce::Colour a) { const auto& T = ghosty::themes::get(curLook); const juce::Colour cand[] = { T.ink, T.onAccent, juce::Colour(0xff0b0814), juce::Colour(0xfff4eefc) }; juce::Colour best = cand[0]; double bk = -1; for (auto c : cand) { const double k = ghosty::themes::contrast(c, a); if (k > bk) { bk = k; best = c; } } return best; }
juce::Font font(float h, bool bold = true) { return juce::FontOptions(h, bold ? juce::Font::bold : juce::Font::plain); }
const juce::String DOT = juce::String::fromUTF8("  \xc2\xb7  ");
juce::String noteName(int m) { static const char* N[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return juce::String(N[((m % 12) + 12) % 12]) + juce::String(m / 12 - 1); }
juce::String timeText(double s) { const int m = (int) (s / 60); return m > 0 ? juce::String(m) + ":" + juce::String(s - m * 60, 1).paddedLeft('0', 4) : juce::String(s, 2) + " s"; }
// a soft glow around a rounded rectangle (a few strokes at falling alpha: the Ghost Hum glass idea in the house colours, no blur)
void glowRect(juce::Graphics& g, juce::Rectangle<float> r, float rad, juce::Colour c, float strength = 1.0f, int rings = 3, float spread = 2.0f) {
    for (int i = rings; i >= 1; --i) { g.setColour(c.withAlpha(juce::jlimit(0.0f, 1.0f, strength * 0.14f / (float) i))); g.drawRoundedRectangle(r.expanded(spread * (float) i), rad + spread * (float) i, spread * 1.2f); }
}
void card(juce::Graphics& g, juce::Rectangle<int> r, juce::Colour edge, float glow = 0.6f) {
    const auto f = r.toFloat(); if (darkTheme) glowRect(g, f, 12.0f, edge, glow);
    g.setColour(CARD); g.fillRoundedRectangle(f, 12.0f);
    { juce::ColourGradient sheen(juce::Colours::white.withAlpha(darkTheme ? 0.04f : 0.25f), f.getX(), f.getY(), juce::Colours::white.withAlpha(0.0f), f.getX(), f.getBottom(), false); g.setGradientFill(sheen); g.fillRoundedRectangle(f, 12.0f); }
    g.setColour(edge.withAlpha(darkTheme ? 0.45f : 0.3f)); g.drawRoundedRectangle(f.reduced(0.5f), 12.0f, 1.0f);
}
void caption(juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r, juce::Colour c) { g.setColour(c); g.setFont(font(10.5f)); g.drawText(s, r, juce::Justification::centredLeft); }
void tint(juce::Component& b, const juce::Colour& paletteEntry) { b.getProperties().set("tint", (juce::int64) reinterpret_cast<std::intptr_t>(&paletteEntry)); }
juce::Colour tintOf(const juce::Component& b, juce::Colour def) { if (!b.getProperties().contains("tint")) return def; return *reinterpret_cast<const juce::Colour*>(static_cast<std::intptr_t>((juce::int64) b.getProperties()["tint"])); }

struct LnF : juce::LookAndFeel_V4 {
    void colours() {
        setColour(juce::ComboBox::backgroundColourId, CARD2); setColour(juce::ComboBox::textColourId, INK); setColour(juce::ComboBox::arrowColourId, PURPLE); setColour(juce::ComboBox::outlineColourId, PURPLE.withAlpha(0.4f));
        setColour(juce::PopupMenu::backgroundColourId, CARD); setColour(juce::PopupMenu::textColourId, INK); setColour(juce::PopupMenu::highlightedBackgroundColourId, PURPLE.withAlpha(0.3f)); setColour(juce::PopupMenu::highlightedTextColourId, INK);
        setColour(juce::PopupMenu::headerTextColourId, GREY);
        setColour(juce::TextButton::textColourOffId, INK); setColour(juce::TextButton::textColourOnId, INK); setColour(juce::Label::textColourId, INK);
        setColour(juce::TooltipWindow::backgroundColourId, CARD); setColour(juce::TooltipWindow::textColourId, INK); setColour(juce::TooltipWindow::outlineColourId, PURPLE);
        setColour(juce::Slider::textBoxTextColourId, INK); setColour(juce::TextEditor::backgroundColourId, CARD2); setColour(juce::TextEditor::textColourId, INK); setColour(juce::TextEditor::outlineColourId, PURPLE.withAlpha(0.4f));
        setColour(juce::TextEditor::focusedOutlineColourId, PURPLE); setColour(juce::CaretComponent::caretColourId, INK); setColour(juce::TextEditor::highlightColourId, PURPLE.withAlpha(0.4f));
        setColour(juce::Slider::backgroundColourId, CARD2); setColour(juce::Slider::trackColourId, CYAN.withAlpha(0.6f));
    }
    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override {
        auto r = b.getLocalBounds().toFloat().reduced(1); const bool on = b.getToggleState(); const auto t = tintOf(b, PURPLE);
        if (on) { if (darkTheme) glowRect(g, r, 8, t, 0.9f); g.setGradientFill(juce::ColourGradient(t.brighter(0.1f), r.getX(), r.getY(), t.darker(0.15f), r.getX(), r.getBottom(), false)); g.fillRoundedRectangle(r, 8); }
        else { g.setColour(CARD2.withAlpha(b.isEnabled() ? 1.0f : 0.5f)); g.fillRoundedRectangle(r, 8); }
        g.setColour((on ? t.brighter(0.3f) : t).withAlpha(b.isEnabled() ? (over ? 0.95f : (on ? 0.7f : 0.42f)) : 0.15f)); g.drawRoundedRectangle(r, 8, down ? 2.0f : 1.1f);
    }
    void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) override {
        g.setFont(getTextButtonFont(b, b.getHeight())); const auto t = tintOf(b, PURPLE);
        g.setColour(b.getToggleState() ? onFill(t) : INK.withAlpha(b.isEnabled() ? 0.92f : 0.3f));
        g.drawFittedText(b.getButtonText(), b.getLocalBounds().reduced(4, 2), juce::Justification::centred, 1, 0.75f);
    }
    juce::Font getTextButtonFont(juce::TextButton&, int h) override { return font(juce::jmin(12.0f, h * 0.48f)); }
    void drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& c) override {
        auto r = juce::Rectangle<float>(0, 0, (float) w, (float) h).reduced(1); g.setColour(CARD2.withAlpha(c.isEnabled() ? 1.0f : 0.5f)); g.fillRoundedRectangle(r, 8); g.setColour(tintOf(c, PURPLE).withAlpha(c.isEnabled() ? 0.45f : 0.15f)); g.drawRoundedRectangle(r, 8, 1.1f);
        juce::Path a; a.addTriangle((float) w - 15, h * 0.5f - 2, (float) w - 7, h * 0.5f - 2, (float) w - 11, h * 0.5f + 3); g.setColour(tintOf(c, PURPLE)); g.fillPath(a);
    }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return font(11.5f); }
    void positionComboBoxText(juce::ComboBox& b, juce::Label& l) override { l.setBounds(6, 1, b.getWidth() - 22, b.getHeight() - 2); l.setFont(getComboBoxFont(b)); }
    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override {
        auto r = juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h).reduced(3); const float sz = juce::jmin(r.getWidth(), r.getHeight()); auto c = r.withSizeKeepingCentre(sz, sz);
        const auto t = tintOf(s, GREEN).withAlpha(s.isEnabled() ? 1.0f : 0.3f); const float th = 4, ang = a0 + pos * (a1 - a0);
        g.setColour(CARD2); g.fillEllipse(c.reduced(th + 1));
        juce::Path tr; tr.addCentredArc(c.getCentreX(), c.getCentreY(), sz / 2 - th / 2, sz / 2 - th / 2, 0, a0, a1, true); g.setColour(DIM); g.strokePath(tr, juce::PathStrokeType(th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const bool bip = s.getMinimum() < 0 && s.getMaximum() > 0; const float from = bip ? a0 + (float) ((0 - s.getMinimum()) / (s.getMaximum() - s.getMinimum())) * (a1 - a0) : a0;
        juce::Path ar; ar.addCentredArc(c.getCentreX(), c.getCentreY(), sz / 2 - th / 2, sz / 2 - th / 2, 0, std::min(from, ang), std::max(from, ang), true);
        if (darkTheme && s.isEnabled()) { g.setColour(t.withAlpha(0.25f)); g.strokePath(ar, juce::PathStrokeType(th + 4, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }
        g.setColour(t); g.strokePath(ar, juce::PathStrokeType(th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const auto tip = c.getCentre().getPointOnCircumference(sz / 2 - th - 5, ang); g.setColour(INK.withAlpha(s.isEnabled() ? 1.0f : 0.3f)); g.drawLine({ c.getCentre().getPointOnCircumference(sz * 0.12f, ang), tip }, 2.0f);
    }
    void drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider& s) override {
        auto r = juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h).reduced(0, 1); g.setColour(CARD2); g.fillRoundedRectangle(r, 6);
        const auto t = tintOf(s, CYAN); g.setColour(t.withAlpha(s.isEnabled() ? 0.55f : 0.2f)); g.fillRoundedRectangle(r.withWidth(pos - x), 6);
        g.setColour(INK.withAlpha(s.isEnabled() ? 1.0f : 0.4f)); g.setFont(font(10.5f)); g.drawText(s.getName() + "  " + s.getTextFromValue(s.getValue()), r.reduced(8, 0), juce::Justification::centredLeft);
    }
};

// a rotary knob with its label + value; right-click types a value, double-click = default
struct Knob : juce::Component {
    juce::Slider s { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox }; juce::String name, unit; int decimals = 1;
    std::function<void(double)> onChange; std::function<void()> onStart, onEnd; std::function<juce::String(double)> textFn;
    Knob(const juce::String& n, const juce::Colour& c, double lo, double hi, double def, double step, const juce::String& u, int dec, const juce::String& tip) : name(n), unit(u), decimals(dec) {
        s.setRange(lo, hi, step); s.setValue(def, juce::dontSendNotification); s.setDoubleClickReturnValue(true, def); s.setPopupDisplayEnabled(true, true, nullptr);
        s.textFromValueFunction = [this](double v) { return text(v); }; tint(s, c); s.setTooltip(tip + "  (double-click: default, right-click: type a value)");
        s.onValueChange = [this] { if (onChange) onChange(s.getValue()); repaint(); }; s.onDragStart = [this] { if (onStart) onStart(); }; s.onDragEnd = [this] { if (onEnd) onEnd(); };
        s.addMouseListener(this, false); addAndMakeVisible(s);
    }
    juce::String text(double v) const { return textFn ? textFn(v) : juce::String(v, decimals) + unit; }
    void mouseDown(const juce::MouseEvent& e) override {
        if (!e.mods.isPopupMenu()) return;
        auto ed = std::make_unique<juce::TextEditor>(); ed->setSize(110, 26); ed->setText(juce::String(s.getValue(), decimals), false); ed->selectAll(); ed->setJustification(juce::Justification::centred);
        auto* raw = ed.get(); juce::Component::SafePointer<Knob> safe(this);
        raw->onReturnKey = [raw, safe] { if (safe) safe->s.setValue(raw->getText().getDoubleValue(), juce::sendNotificationSync); if (auto* box = raw->findParentComponentOfClass<juce::CallOutBox>()) box->dismiss(); };
        juce::CallOutBox::launchAsynchronously(std::move(ed), s.getScreenBounds(), nullptr); juce::Timer::callAfterDelay(50, [raw] { raw->grabKeyboardFocus(); });
    }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds(); auto lab = r.removeFromBottom(24);
        g.setColour(GREY.withAlpha(isEnabled() ? 1.0f : 0.4f)); g.setFont(font(9.0f)); g.drawFittedText(name, lab.removeFromTop(11), juce::Justification::centred, 1, 0.7f);
        g.setColour(INK.withAlpha(isEnabled() ? 1.0f : 0.4f)); g.setFont(font(10.0f, false)); g.drawFittedText(text(s.getValue()), lab, juce::Justification::centred, 1, 0.7f);
    }
    void resized() override { auto r = getLocalBounds(); r.removeFromBottom(24); const int d = std::min(r.getWidth(), r.getHeight()); s.setBounds(r.withSizeKeepingCentre(d, d)); }
    void set(double v) { s.setValue(v, juce::dontSendNotification); repaint(); }
};

struct DragOut : juce::Component, juce::SettableTooltipClient {
    juce::String text; std::function<juce::StringArray()> make; bool started = false;
    DragOut(juce::String t, std::function<juce::StringArray()> m) : text(std::move(t)), make(std::move(m)) { setMouseCursor(juce::MouseCursor::DraggingHandCursor); }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat().reduced(1); const bool en = isEnabled();
        if (darkTheme && en) glowRect(g, r, 8, GREEN, isMouseOver() ? 1.0f : 0.5f);
        g.setColour(GREEN.withAlpha(en ? (isMouseOver() ? 0.24f : 0.13f) : 0.05f)); g.fillRoundedRectangle(r, 8); g.setColour(GREEN.withAlpha(en ? 0.9f : 0.3f)); g.drawRoundedRectangle(r, 8, 1.3f);
        juce::Path arrow; const float cx = r.getX() + 16, cy = r.getCentreY(); arrow.startNewSubPath(cx, cy - 6); arrow.lineTo(cx, cy + 5); arrow.startNewSubPath(cx - 4, cy + 1); arrow.lineTo(cx, cy + 5); arrow.lineTo(cx + 4, cy + 1); g.strokePath(arrow, juce::PathStrokeType(1.6f));
        g.setColour(INK.withAlpha(en ? 1.0f : 0.4f)); g.setFont(font(11.0f)); g.drawFittedText(text, r.withTrimmedLeft(24).toNearestInt(), juce::Justification::centred, 1, 0.8f);
    }
    void mouseEnter(const juce::MouseEvent&) override { repaint(); } void mouseExit(const juce::MouseEvent&) override { repaint(); }
    void mouseDown(const juce::MouseEvent&) override { started = false; }
    void mouseDrag(const juce::MouseEvent& e) override { if (started || e.getDistanceFromDragStart() < 6 || !isEnabled()) return; started = true; auto f = make(); f.removeEmptyStrings(); if (!f.isEmpty()) ghosty::exportgate::dragFiles(f, this); }   // make() asked the gate
};

void peaksOf(const float* x, int n, int fs, double t0, double t1, int w, std::vector<std::pair<float, float>>& v) {
    v.assign((size_t) std::max(1, w), { 0.0f, 0.0f }); if (n <= 0 || fs <= 0) return;
    for (int c = 0; c < w; ++c) { const long a = (long) ((t0 + (t1 - t0) * c / w) * fs), b = std::min((long) n, (long) ((t0 + (t1 - t0) * (c + 1) / w) * fs) + 1); float lo = 0, hi = 0;
        for (long i = std::max(0L, a); i < b; ++i) { lo = std::min(lo, x[i]); hi = std::max(hi, x[i]); } v[(size_t) c] = { lo, hi }; }
}
const char* kSlotTitles[3] = { "1  YOUR VOICE", "2  VOICE B", "VOICE C (optional)" };
} // namespace

// =====================================================================================================================
struct GhostVoicePage::Impl {
    GhostVoicePage& page; FusionHost& H; LnF lnf; ghosty::themes::Theme look = ghosty::themes::Theme::Dark;
    Params P; SlotPtr S[3]; std::shared_ptr<const voice::Analysis> aA; ResultPtr R; bool stale = false;
    int seenParams = -1, seenSource = -1, seenResult = -1; int dropSlot = -1; bool wasMoving = false;
    std::unique_ptr<juce::FileChooser> chooser; std::unique_ptr<juce::TooltipWindow> tips;
    juce::String flashText; juce::uint32 flashUntil = 0;
    void usePalette() { if (!paletteInit || curLook != look) applyTheme(look); }
    void flash(const juce::String& s) { flashText = s; flashUntil = juce::Time::getMillisecondCounter() + 3500; page.repaint(); }
    bool exportAllowed(juce::Component* ui, const char* id) { if (!page.allowExport || !page.allowExport(ui != nullptr ? ui : &page, id)) { flash("Exporting is part of the full version"); return false; } return true; }

    // ---------------------------------------------------------------------------------------------- the record button
    struct RecordButton : juce::Component, juce::SettableTooltipClient {
        Impl& I; int slot; float phase = 0;
        RecordButton(Impl& i, int s) : I(i), slot(s) { setMouseCursor(juce::MouseCursor::PointingHandCursor); }
        bool armed() const { return (I.H.isRecording() || I.H.isCountingIn()) && I.H.recordSlot() == slot; }
        void paint(juce::Graphics& g) override {
            I.usePalette(); auto r = getLocalBounds().toFloat(); const float sz = juce::jmin(r.getWidth(), r.getHeight()) - 6; auto c = r.withSizeKeepingCentre(sz, sz);
            const bool rec = armed() && I.H.isRecording(), cnt = armed() && I.H.isCountingIn(); const auto col = rec ? RED : cnt ? AMBER : PINK;
            const float lvl = armed() ? juce::jlimit(0.0f, 1.0f, I.H.inputLevel() * 2.5f) : 0.0f;
            if (darkTheme || rec) { for (int i = 4; i >= 1; --i) { g.setColour(col.withAlpha(0.12f / i * (rec ? 1.0f + lvl : 0.7f))); g.drawEllipse(c.expanded(2.5f * i + lvl * 6), 2.5f); } }
            g.setColour(CARD2); g.fillEllipse(c); g.setColour(col.withAlpha(isMouseOver() ? 1.0f : 0.8f)); g.drawEllipse(c.reduced(1), 2.2f);
            if (rec) { const float s2 = sz * 0.34f; g.setColour(col); g.fillRoundedRectangle(c.withSizeKeepingCentre(s2, s2), 4); }
            else if (cnt) { g.setColour(col); g.setFont(font(sz * 0.42f)); g.drawText(juce::String(I.H.countInLeft()), c, juce::Justification::centred); }
            else { g.setColour(col); g.fillEllipse(c.withSizeKeepingCentre(sz * 0.42f, sz * 0.42f)); }
            g.setColour(GREY); g.setFont(font(8.5f)); g.drawText(rec ? timeText(I.H.recordedSeconds()) : cnt ? "count-in" : "REC", getLocalBounds().removeFromBottom(11), juce::Justification::centred);
        }
        void mouseEnter(const juce::MouseEvent&) override { repaint(); } void mouseExit(const juce::MouseEvent&) override { repaint(); }
        void mouseUp(const juce::MouseEvent& e) override { if (!getLocalBounds().contains(e.getPosition())) return; if (armed()) I.H.stopRecording(); else if (!I.H.isRecording() && !I.H.isCountingIn()) I.H.startRecording(slot, I.countIn ? 4 : 0, I.page.tempoFn ? I.page.tempoFn() : 100.0); I.pull(); }
    };

    // ---------------------------------------------------------------------------------------------- slot cards
    struct SlotCard : juce::Component {
        Impl& I; int slot; std::vector<std::pair<float, float>> pk; const void* pkKey = nullptr; int pkW = 0;
        RecordButton rec; juce::TextButton load { "LOAD" }, play { "PLAY" }, clear { "CLEAR" }, voiceBtn { "PICK A VOICE" };
        SlotCard(Impl& i, int s) : I(i), slot(s), rec(i, s) {
            addAndMakeVisible(rec); const auto& c = col();
            for (auto* b : { &load, &play, &clear }) { addAndMakeVisible(*b); tint(*b, c); }
            rec.setTooltip(slot == 0 ? "Record your performance from the input (count-in when COUNT-IN is on). Click again to stop" : "Record this voice from the input. Click again to stop");
            load.setTooltip(slot == 0 ? "Load your take from a file (or drop it on this card). Up to 3 minutes" : "Load a recording of this voice (or drop it here). Use recordings you have the rights to");
            play.setTooltip("Hear this voice on its own"); clear.setTooltip("Empty this slot");
            load.onClick = [this] { I.choose(slot); }; clear.onClick = [this] { I.H.clearSlot(slot); I.pull(); };
            play.onClick = [this] { const int w = slot == 0 ? PlayA : slot == 1 ? PlayB : PlayC; if (I.H.playing.load() == w) I.H.stop(); else I.H.play(w, 0.0); };
            if (slot > 0) { addAndMakeVisible(voiceBtn); tint(voiceBtn, c); voiceBtn.setTooltip("Pick a designed voice (Glass Angel, Midnight...) or one of your saved voices"); voiceBtn.onClick = [this] { I.voiceMenu(slot, voiceBtn); }; }
        }
        const juce::Colour& col() const { return slot == 0 ? CYAN : slot == 1 ? AMBER : PINK; }
        void paint(juce::Graphics& g) override {
            I.usePalette(); const auto& c = col(); card(g, getLocalBounds(), c);
            if (I.dropSlot == slot) { g.setColour(c.withAlpha(0.14f)); g.fillRoundedRectangle(getLocalBounds().toFloat(), 12); g.setColour(c); g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1), 12, 2.0f); }
            auto r = getLocalBounds().reduced(12, 7); caption(g, kSlotTitles[slot], r.removeFromTop(15), c);
            const auto s = I.S[slot]; r.removeFromLeft(rec.getWidth() + 8);
            auto wv = r.removeFromTop(30).toFloat(); r.removeFromTop(3); g.setColour(CARD2); g.fillRoundedRectangle(wv, 5);
            if (s && s->take) {
                if (pkKey != s.get() || pkW != (int) wv.getWidth()) { peaksOf(s->take->mono.data(), s->take->length(), s->take->fs, 0, s->seconds(), (int) wv.getWidth(), pk); pkKey = s.get(); pkW = (int) wv.getWidth(); }
                g.setColour(c.withAlpha(0.7f)); for (int i = 0; i < (int) pk.size(); ++i) { const float mid = wv.getCentreY(), hh = wv.getHeight() * 0.46f; g.fillRect(wv.getX() + i, mid - pk[(size_t) i].second * hh, 1.0f, std::max(1.0f, (pk[(size_t) i].second - pk[(size_t) i].first) * hh)); }
                const int pw = I.H.playing.load(); const double pp = I.H.playPos.load(); const int mine = slot == 0 ? PlayA : slot == 1 ? PlayB : PlayC;
                if (pp >= 0 && pw == mine && s->seconds() > 0) { g.setColour(GREEN); g.drawVerticalLine((int) (wv.getX() + pp / s->seconds() * wv.getWidth()), wv.getY(), wv.getBottom()); }
            } else if (s && s->identity()) { g.setColour(c.withAlpha(0.8f)); g.setFont(font(10.5f)); g.drawText("designed voice", wv, juce::Justification::centred); }
            else { g.setColour(GREY); g.setFont(font(10.5f)); g.drawText(slot == 0 ? "sing, or drop a WAV here" : "record, drop a WAV, or pick a voice", wv, juce::Justification::centred); }
            g.setFont(font(11.0f)); g.setColour(INK);
            if (s && !s->empty()) {
                g.drawText(s->name(), r.removeFromTop(13), juce::Justification::centredLeft, true); g.setColour(GREY); g.setFont(font(9.5f, false));
                juce::String info; if (s->take) info = timeText(s->seconds()) + DOT + juce::String(s->take->fs / 1000.0, 1) + " kHz";
                if (auto id = s->identity()) info = (info.isEmpty() ? juce::String() : info + DOT) + describe(*id); else if (s->take) info << DOT << (s->failed.load() ? "not analysed" : "listening... " + juce::String(std::max(0, I.H.progress.load())) + " %");
                g.drawFittedText(info, r.removeFromTop(24), juce::Justification::topLeft, 2, 0.8f);
            } else { g.setColour(GREY); g.setFont(font(9.5f, false)); g.drawFittedText(slot == 0 ? "The performance: your pitch, timing, words and vibrato stay exactly as sung." : slot == 1 ? "The voice to blend in: a singer, a pad, a choir, an instrument - or a designed voice." : "A third voice for 3-way blends (the pad becomes a triangle).", r.removeFromTop(26), juce::Justification::topLeft, 2); }
            if (slot > 0) { g.setColour(GREY.withAlpha(0.9f)); g.setFont(font(8.5f, false)); g.drawText("Use recordings you have the rights to.", getLocalBounds().reduced(12, 6).removeFromBottom(32).removeFromTop(10), juce::Justification::centredLeft); }
        }
        void resized() override {
            auto r = getLocalBounds().reduced(12, 7); r.removeFromTop(15); auto row = r.removeFromBottom(22); if (slot > 0) r.removeFromBottom(12);
            rec.setBounds(r.removeFromLeft(54).withHeight(56));
            const int n = slot > 0 ? 4 : 3; const int w = (row.getWidth() - (n - 1) * 4) / n;
            load.setBounds(row.removeFromLeft(w)); row.removeFromLeft(4); play.setBounds(row.removeFromLeft(w)); row.removeFromLeft(4);
            if (slot > 0) { voiceBtn.setBounds(row.removeFromLeft(w)); row.removeFromLeft(4); } clear.setBounds(row);
        }
    } slotA { *this, 0 }, slotB { *this, 1 }, slotC { *this, 2 };

    // ---------------------------------------------------------------------------------------------- the main waveform (A + pitch, START / END)
    struct Wave : juce::Component {
        Impl& I; juce::Image cache; juce::String cacheKey; int drag = 0; double lo = 45, hi = 75;   // drag: 1 START, 2 END
        explicit Wave(Impl& i) : I(i) { setMouseCursor(juce::MouseCursor::CrosshairCursor); }
        double T() const { return I.S[0] && I.S[0]->take ? std::max(0.2, I.S[0]->seconds()) : 1.0; }
        juce::Rectangle<float> plot() const { auto r = getLocalBounds().toFloat().reduced(10, 8); r.removeFromTop(16); r.removeFromBottom(14); r.removeFromLeft(36); return r; }
        float xOf(double t) const { const auto p = plot(); return p.getX() + (float) (t / T()) * p.getWidth(); }
        double tOf(float x) const { const auto p = plot(); return juce::jlimit(0.0, T(), (x - p.getX()) / p.getWidth() * T()); }
        float yOf(double m) const { const auto p = plot(); return p.getY() + (float) ((hi - m) / (hi - lo)) * p.getHeight(); }
        double t0() const { return I.P.trimStart > 0 ? I.P.trimStart : 0.0; } double t1() const { return I.P.trimEnd > 0 ? std::min(T(), I.P.trimEnd) : T(); }
        void fitRange() { double a = 1e9, b = -1e9; if (I.aA) for (double f : I.aA->f0) if (f > 0) { a = std::min(a, voice::hzMidi(f)); b = std::max(b, voice::hzMidi(f)); }
            if (I.R) for (double f : I.R->info.f0) if (f > 0) { a = std::min(a, voice::hzMidi(f)); b = std::max(b, voice::hzMidi(f)); }
            if (b > a) { lo = std::floor(a - 3); hi = std::ceil(b + 3); if (hi - lo < 14) { const double c = 0.5 * (lo + hi); lo = c - 7; hi = c + 7; } } else { lo = 45; hi = 75; } }
        juce::String keyNow() const { juce::String k; k << getWidth() << "x" << getHeight() << "|" << (juce::int64) (std::intptr_t) I.S[0].get() << "|" << (juce::int64) (std::intptr_t) I.aA.get() << "|" << (I.R ? (int) I.R->serial : -1) << "|" << (int) curLook << "|" << lo << "-" << hi; return k; }
        void renderCache() {
            cache = juce::Image(juce::Image::ARGB, std::max(1, getWidth()), std::max(1, getHeight()), true); juce::Graphics g(cache); card(g, getLocalBounds(), CYAN, 0.5f);
            const auto p = plot(); caption(g, "YOUR TAKE  -  the pitch you sang (cyan) and the new voice's pitch (pink)  -  drag START / END to use a part", getLocalBounds().reduced(10, 6).removeFromTop(14), GREY);
            g.setColour(CARD2); g.fillRoundedRectangle(p, 6);
            if (I.S[0] && I.S[0]->take) { std::vector<std::pair<float, float>> pk; const auto& t = *I.S[0]->take; peaksOf(t.mono.data(), t.length(), t.fs, 0, T(), (int) p.getWidth(), pk);
                juce::Path path; const float mid = p.getCentreY(), hh = p.getHeight() * 0.46f; for (int i = 0; i < (int) pk.size(); ++i) path.addRectangle(p.getX() + i, mid - pk[(size_t) i].second * hh, 1.0f, std::max(1.0f, (pk[(size_t) i].second - pk[(size_t) i].first) * hh));
                g.setColour(DIM.interpolatedWith(CYAN, 0.35f).withAlpha(darkTheme ? 0.45f : 0.55f)); g.fillPath(path); }
            g.setFont(font(9.0f)); for (int m = (int) std::ceil(lo); m <= (int) std::floor(hi); ++m) { const bool c = ((m % 12) + 12) % 12 == 0; const float y = yOf(m); g.setColour(INK.withAlpha(c ? 0.16f : 0.05f)); g.drawHorizontalLine((int) y, p.getX(), p.getRight());
                if (c || hi - lo < 20) { g.setColour(c ? INK.withAlpha(0.8f) : GREY.withAlpha(0.7f)); g.drawText(noteName(m), (int) p.getX() - 34, (int) y - 6, 30, 12, juce::Justification::centredRight); } }
            g.saveState(); g.reduceClipRegion(p.toNearestInt());
            if (I.aA) { const auto& a = *I.aA; const int stride = std::max(1, (int) (a.frames() / (p.getWidth() * 1.5f))); juce::Path dots; for (int i = 0; i < a.frames(); i += stride) if (a.f0[(size_t) i] > 0) dots.addEllipse(xOf(a.frameTime(i)) - 1.3f, yOf(voice::hzMidi(a.f0[(size_t) i])) - 1.3f, 2.6f, 2.6f); g.setColour(CYAN.withAlpha(0.7f)); g.fillPath(dots); }
            if (I.R && !I.R->info.f0.empty()) { const auto& in = I.R->info; juce::Path path; bool pen = false; for (int j = 0; j < in.frames(); ++j) { const double f = in.f0[(size_t) j]; if (f <= 0) { pen = false; continue; } const float x = xOf(in.t0 + j * in.framePeriod / 1000.0), y = yOf(voice::hzMidi(f)); if (!pen) path.startNewSubPath(x, y); else path.lineTo(x, y); pen = true; }
                g.setColour(PINK.withAlpha(0.9f)); g.strokePath(path, juce::PathStrokeType(1.8f)); }
            g.restoreState();
        }
        void paint(juce::Graphics& g) override {
            I.usePalette(); const auto k = keyNow(); if (k != cacheKey || !cache.isValid()) { renderCache(); cacheKey = k; } g.drawImageAt(cache, 0, 0);
            const auto p = plot();
            if (I.S[0] && I.S[0]->take) {   // START / END shade + handles
                const float x0 = xOf(t0()), x1 = xOf(t1()); g.setColour(BG.withAlpha(0.55f)); g.fillRect(p.getX(), p.getY(), x0 - p.getX(), p.getHeight()); g.fillRect(x1, p.getY(), p.getRight() - x1, p.getHeight());
                for (int h = 0; h < 2; ++h) { const float x = h == 0 ? x0 : x1; g.setColour(AMBER); g.drawLine(x, p.getY(), x, p.getBottom(), 1.6f); g.fillRoundedRectangle(x - 5, p.getBottom() - 10, 10, 10, 2);
                    g.setFont(font(8.5f)); g.drawText(h == 0 ? "START" : "END", (int) x + (h == 0 ? 4 : -40), (int) p.getY() + 2, 36, 10, h == 0 ? juce::Justification::centredLeft : juce::Justification::centredRight); }
            }
            const double pp = I.H.playPos.load(); const int pw = I.H.playing.load();
            if (pp >= 0 && (pw == PlayResult || pw == PlayA)) { const double ts = pw == PlayResult && I.R ? pp + I.R->info.t0 : pp; const float x = xOf(ts); if (x >= p.getX() && x <= p.getRight()) { g.setColour(GREEN); g.drawLine(x, p.getY(), x, p.getBottom(), 1.6f); } }
            if (!I.S[0] || !I.S[0]->take) { g.setColour(INK.withAlpha(0.75f)); g.setFont(font(15.0f)); g.drawText("Press REC and sing - or drop your take here", p.toNearestInt().translated(0, -10), juce::Justification::centred);
                g.setColour(GREY); g.setFont(font(11.5f, false)); g.drawText("Your performance stays exactly as sung; only the voice changes.", p.toNearestInt().translated(0, 12), juce::Justification::centred); }
            else if (!I.aA) { g.setColour(GREY); g.setFont(font(12.0f)); g.drawText("Listening to your take...", p.toNearestInt().translated(0, 20), juce::Justification::centredTop); }
            g.setColour(GREY); g.setFont(font(9.0f, false)); g.drawText(I.hint(), getLocalBounds().reduced(10, 4).removeFromBottom(11), juce::Justification::centredLeft);
        }
        int handleAt(float x) const { if (!I.S[0] || !I.S[0]->take) return 0; if (std::abs(x - xOf(t0())) < 8) return 1; if (std::abs(x - xOf(t1())) < 8) return 2; return 0; }
        void mouseMove(const juce::MouseEvent& e) override { setMouseCursor(handleAt(e.position.x) ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::CrosshairCursor); }
        void mouseDown(const juce::MouseEvent& e) override { drag = handleAt(e.position.x); if (drag) I.H.beginGesture(); }
        void mouseDrag(const juce::MouseEvent& e) override {
            if (!drag) return; auto p = I.H.params(); const double t = tOf(e.position.x), Tt = T();
            if (drag == 1) p.trimStart = t < 0.01 ? -1 : std::min(t, t1() - 0.1); else p.trimEnd = t > Tt - 0.01 ? -1 : std::max(t, t0() + 0.1);
            if (p.trimStart < 0 && p.trimEnd < 0) { p.trimStart = p.trimEnd = -1; } I.H.setParams(p, true, "TRIM"); I.P = I.H.params(); I.seenParams = I.H.paramsVersion.load(); repaint();
        }
        void mouseUp(const juce::MouseEvent& e) override { if (drag) { I.H.endGesture(); drag = 0; return; } if (!plot().contains(e.position)) return; const double t = tOf(e.position.x); if (I.R) I.H.play(PlayResult, std::max(0.0, t - I.R->info.t0)); else if (I.S[0] && I.S[0]->take) I.H.play(PlayA, t); }
        void mouseDoubleClick(const juce::MouseEvent&) override { auto p = I.H.params(); p.trimStart = p.trimEnd = -1; I.H.setParams(p); I.pull(); }
    } wave { *this };

    // ---------------------------------------------------------------------------------------------- the blend pad (A - B line, or A B C triangle)
    struct Pad : juce::Component, juce::SettableTooltipClient {
        Impl& I; bool dragging = false; explicit Pad(Impl& i) : I(i) { setTooltip("Drag the dot: how much of each voice is in the new voice. Without VOICE C it is a line from YOU to B; with C, a triangle"); }
        bool tri() const { return I.S[2] && !I.S[2]->empty(); }
        juce::Point<float> A() const { auto r = getLocalBounds().toFloat().reduced(16, 14); return { r.getX(), r.getBottom() }; }
        juce::Point<float> B() const { auto r = getLocalBounds().toFloat().reduced(16, 14); return { r.getRight(), r.getBottom() }; }
        juce::Point<float> C() const { auto r = getLocalBounds().toFloat().reduced(16, 14); return { r.getCentreX(), r.getY() }; }
        juce::Point<float> dot() const { const float wb = I.P.wB, wc = tri() ? I.P.wC : 0.0f, wa = 1 - wb - wc; auto a = A(), b = B(), c = C(); if (!tri()) { a.y = b.y = getLocalBounds().toFloat().getCentreY(); } return { wa * a.x + wb * b.x + wc * c.x, wa * a.y + wb * b.y + wc * c.y }; }
        void paint(juce::Graphics& g) override {
            I.usePalette(); g.setColour(CARD2); g.fillRoundedRectangle(getLocalBounds().toFloat(), 8);
            auto a = A(), b = B(), c = C(); const bool t = tri(); if (!t) { a.y = b.y = getLocalBounds().toFloat().getCentreY(); }
            juce::Path edges; edges.startNewSubPath(a); edges.lineTo(b); if (t) { edges.lineTo(c); edges.closeSubPath(); }
            g.setColour(INK.withAlpha(0.18f)); g.strokePath(edges, juce::PathStrokeType(1.2f));
            for (int k = 1; k < 4; ++k) { const float f = k / 4.0f; g.setColour(INK.withAlpha(0.07f)); g.drawLine(a.x + (b.x - a.x) * f, a.y - 4, a.x + (b.x - a.x) * f, a.y + 4, 1.0f); }
            auto label = [&](juce::Point<float> p, const juce::String& s, juce::Colour col, juce::Justification j, int dx, int dy) { g.setColour(col); g.setFont(font(10.0f)); g.drawText(s, (int) p.x + dx, (int) p.y + dy, 60, 12, j); };
            label(a, "YOU", CYAN, juce::Justification::centredLeft, -2, 2); label(b, "B", AMBER, juce::Justification::centredRight, -58, 2); if (t) label(c, "C", PINK, juce::Justification::centred, -30, -14);
            const auto d = dot(); const auto mix = CYAN.interpolatedWith(AMBER, I.P.wB).interpolatedWith(PINK, t ? I.P.wC : 0.0f);
            if (darkTheme) { for (int i = 3; i >= 1; --i) { g.setColour(mix.withAlpha(0.12f / i)); g.fillEllipse(d.x - 7 - 3 * i, d.y - 7 - 3 * i, 14 + 6 * i, 14 + 6 * i); } }
            g.setColour(mix); g.fillEllipse(d.x - 7, d.y - 7, 14, 14); g.setColour(INK); g.drawEllipse(d.x - 7, d.y - 7, 14, 14, 1.2f);
        }
        void set(juce::Point<float> p) {
            auto a = A(), b = B(), c = C(); const bool t = tri(); auto q = I.H.params();
            if (!t) { q.wB = juce::jlimit(0.0f, 1.0f, (p.x - a.x) / std::max(1.0f, b.x - a.x)); q.wC = 0; }
            else {   // barycentric, clamped into the triangle
                const float det = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
                float wa = ((b.y - c.y) * (p.x - c.x) + (c.x - b.x) * (p.y - c.y)) / det, wb = ((c.y - a.y) * (p.x - c.x) + (a.x - c.x) * (p.y - c.y)) / det; float wc = 1 - wa - wb;
                wa = std::max(0.0f, wa); wb = std::max(0.0f, wb); wc = std::max(0.0f, wc); const float s = wa + wb + wc; q.wB = wb / s; q.wC = wc / s;
            }
            I.H.setParams(q, true, "PAD"); I.P = I.H.params(); I.seenParams = I.H.paramsVersion.load(); I.syncBlend(); repaint();
        }
        void mouseDown(const juce::MouseEvent& e) override { I.H.beginGesture(); dragging = true; set(e.position); }
        void mouseDrag(const juce::MouseEvent& e) override { if (dragging) set(e.position); }
        void mouseUp(const juce::MouseEvent&) override { if (dragging) { I.H.endGesture(); dragging = false; } }
    } pad { *this };

    // ---------------------------------------------------------------------------------------------- about
    struct About : juce::Component {
        Impl& I; juce::TextEditor text; juce::TextButton close { "CLOSE" }; int shownLook = -1;
        explicit About(Impl& i) : I(i) { text.setMultiLine(true); text.setReadOnly(true); text.setScrollbarsShown(true); text.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
            text.setText(juce::String("GHOST VOICE blends the fingerprints of two or three voices (what each vocal tract does for every vowel) and sings your performance with the blend. Classic signal processing, no models.\n\n") + voice::thirdPartyNotice(), false);
            addAndMakeVisible(text); addAndMakeVisible(close); tint(close, PURPLE); close.onClick = [this] { setVisible(false); }; close.setTooltip("Close this panel"); }
        void paint(juce::Graphics& g) override { I.usePalette(); g.fillAll(BG.withAlpha(0.75f)); auto r = getLocalBounds().withSizeKeepingCentre(640, 480); card(g, r, PURPLE); if (shownLook != (int) curLook) { shownLook = (int) curLook; text.applyColourToAllText(INK, true); } caption(g, "ABOUT GHOST VOICE" + DOT + "third-party licences", r.reduced(16, 12).removeFromTop(16), INK); }
        void resized() override { auto r = getLocalBounds().withSizeKeepingCentre(640, 480).reduced(16, 12); r.removeFromTop(22); close.setBounds(r.removeFromBottom(26).removeFromRight(90)); r.removeFromBottom(8); text.setBounds(r); }
    } about { *this };

    // ---------------------------------------------------------------------------------------------- controls
    juce::TextButton undoBtn { "UNDO" }, redoBtn { "REDO" }, infoBtn { "i" }, countBtn { "COUNT-IN" };
    juce::Slider sB { juce::Slider::LinearBar, juce::Slider::NoTextBox }, sC { juce::Slider::LinearBar, juce::Slider::NoTextBox };
    juce::TextButton consBtn { "KEEP MY CONSONANTS" };
    Knob kClarity { "CLARITY", CYAN, 0, 1, 0.5, 0.01, "", 2, "With KEEP MY CONSONANTS: how much of the fine detail above 5 kHz (s, f, breath) stays yours" };
    Knob kFormant { "FORMANT", PURPLE, -12, 12, 0, 0.1, " st", 1, "Size of the new voice: - = bigger (deeper vowels), + = smaller. The pitch does not move" };
    Knob kBright { "BRIGHTNESS", PURPLE, -1, 1, 0, 0.01, "", 2, "Tilt above 1 kHz: darker (left) or brighter (right)" };
    Knob kWarmth { "WARMTH", PURPLE, -1, 1, 0, 0.01, "", 2, "The low-mid body around 250 Hz: thinner or warmer" };
    Knob kBreath { "BREATH", PURPLE, -1, 1, 0, 0.01, "", 2, "Less or more breath (air) in the voice" };
    Knob kGrit { "GRIT", PURPLE, 0, 1, 0, 0.01, "", 2, "Texture: a rough, unsteady edge (tiny pitch and level wobble, noise in the mids)" };
    Knob kReso { "RESONANCE", PURPLE, -1, 1, 0, 0.01, "", 2, "Peaks vs valleys of the vowel resonances: softer (left) or more ringing (right)" };
    Knob kWeight { "WEIGHT", PURPLE, -1, 1, 0, 0.01, "", 2, "Chest and body: lighter (left) or heavier (right)" };
    Knob kGender { "GENDER / AGE", PURPLE, -1, 1, 0, 0.01, "", 2, "One knob for size + pitch together: left = bigger and lower, right = smaller and higher" };
    juce::TextButton tuneBtn { "TUNE" }, natBtn { "NATURAL" }, hardBtn { "HARD" }, vibBtn { "KEEP VIBRATO" }; juce::ComboBox rootBox, scaleBox, octBox;
    Knob kRetune { "RETUNE", BLUE, 0, 400, 60, 1, " ms", 0, "How fast a note is pulled to the key (NATURAL)" };
    Knob kHumanize { "HUMANIZE", BLUE, 0, 1, 0.3, 0.01, "", 2, "Slower correction on long notes, so sustains stay alive (NATURAL)" };
    Knob kSemis { "SEMITONES", BLUE, -12, 12, 0, 1, " st", 0, "Transpose by semitones (the voice's size stays where FORMANT puts it)" };
    juce::ComboBox voicesBox; Knob kDetune { "DETUNE", GREEN, 0, 50, 10, 0.5, " c", 1, "STACK: pitch spread of the doubled voices (cents)" };
    Knob kSpread { "SPREAD", GREEN, 0, 60, 14, 0.5, " ms", 1, "STACK: timing spread of the doubled voices" }; Knob kWidth { "WIDTH", GREEN, 0, 1, 0.8, 0.01, "", 2, "STACK: stereo width of the choir" };
    Knob kOctDown { "OCT DOWN", GREEN, 0, 1, 0, 0.01, "", 2, "A layer one octave below (level)" }; Knob kOctUp { "OCT UP", GREEN, 0, 1, 0, 0.01, "", 2, "A layer one octave above (level)" };
    juce::TextButton playBtn { "PLAY NEW VOICE" }, stopBtn { "STOP" }, abBtn { "A/B  MINE / NEW" }, renderBtn { "RENDER" }, autoBtn { "AUTO" }, cancelBtn { "CANCEL" }, saveBtn { "SAVE VOICE" }, exportBtn { "EXPORT WAV" }, arrangeBtn { "SEND TO ARRANGE" };
    Knob kGain { "GAIN", GREEN, -24, 12, 0, 0.1, " dB", 1, "Output level (after the automatic normalise)" };
    DragOut dragOut { "DRAG WAV OUT", [this] { if (!exportAllowed(&dragOut, "spec.ghostvoice.wav-drag")) return juce::StringArray(); juce::String why; auto f = H.writeResultWav(why, true); pull(); if (!f.existsAsFile()) { flash(why); return juce::StringArray(); } return juce::StringArray { f.getFullPathName() }; } };
    juce::Rectangle<int> blendArea, designArea, pitchArea, stackArea, outputArea, progressArea, staleArea, titleArea;
    bool countIn = true;
    std::vector<Knob*> allKnobs() { return { &kClarity, &kFormant, &kBright, &kWarmth, &kBreath, &kGrit, &kReso, &kWeight, &kGender, &kRetune, &kHumanize, &kSemis, &kDetune, &kSpread, &kWidth, &kOctDown, &kOctUp, &kGain }; }

    Impl(GhostVoicePage& pg, FusionHost& h) : page(pg), H(h) {
        { const auto pr = H.uiPrefs(); look = pr.contains("look=") ? ghosty::themes::fromString(pr.fromFirstOccurrenceOf("look=", false, false).upToFirstOccurrenceOf(";", false, false), ghosty::themes::Theme::Dark) : ghosty::themes::Theme::Dark; countIn = !pr.contains("countin=0"); applyTheme(look); }
        lnf.colours(); page.setLookAndFeel(&lnf);
        for (auto* c : std::initializer_list<juce::Component*> { &slotA, &slotB, &slotC, &wave, &pad, &undoBtn, &redoBtn, &infoBtn, &countBtn, &sB, &sC, &consBtn, &kClarity, &kFormant, &kBright, &kWarmth, &kBreath, &kGrit, &kReso, &kWeight, &kGender,
                                                                 &tuneBtn, &natBtn, &hardBtn, &vibBtn, &rootBox, &scaleBox, &octBox, &kRetune, &kHumanize, &kSemis, &voicesBox, &kDetune, &kSpread, &kWidth, &kOctDown, &kOctUp,
                                                                 &playBtn, &stopBtn, &abBtn, &renderBtn, &autoBtn, &cancelBtn, &saveBtn, &exportBtn, &arrangeBtn, &kGain, &dragOut }) page.addAndMakeVisible(*c);
        page.addChildComponent(about);
        undoBtn.onClick = [this] { H.undo(); pull(); }; redoBtn.onClick = [this] { H.redo(); pull(); }; undoBtn.setTooltip("Undo (Ctrl/Cmd+Z)"); redoBtn.setTooltip("Redo (Ctrl/Cmd+Shift+Z)");
        infoBtn.setTooltip("About GHOST VOICE + third-party licences"); infoBtn.onClick = [this] { about.setVisible(true); about.toFront(true); };
        countBtn.setClickingTogglesState(true); countBtn.setToggleState(countIn, juce::dontSendNotification); tint(countBtn, PINK); countBtn.setTooltip("Four clicks before a recording starts (at Spectrum's tempo)");
        countBtn.onClick = [this] { countIn = countBtn.getToggleState(); savePrefs(); };
        // blend
        auto bar = [this](juce::Slider& s, const char* name, const juce::Colour& c, const juce::String& tip, std::function<void(Params&, double)> f) {
            s.setName(name); s.setRange(0, 100, 1); tint(s, c); s.setTooltip(tip); s.textFromValueFunction = [](double v) { return juce::String((int) v) + " %"; };
            s.onValueChange = [this, &s, f] { auto p = H.params(); f(p, s.getValue() / 100.0); H.setParams(p, true, s.getName()); P = H.params(); seenParams = H.paramsVersion.load(); syncBlend(); pad.repaint(); };
            s.onDragStart = [this] { H.beginGesture(); }; s.onDragEnd = [this] { H.endGesture(); }; };
        bar(sB, "VOICE B", AMBER, "How much of VOICE B is in the new voice (the rest is you, and C)", [](Params& p, double v) { p.wB = (float) v; if (p.wB + p.wC > 1) p.wC = 1 - p.wB; });
        bar(sC, "VOICE C", PINK, "How much of VOICE C is in the new voice", [](Params& p, double v) { p.wC = (float) v; if (p.wB + p.wC > 1) p.wB = 1 - p.wC; });
        auto toggle = [this](juce::TextButton& b, const juce::Colour& c, const juce::String& tip, std::function<void(Params&, bool)> f) { b.setClickingTogglesState(true); tint(b, c); b.setTooltip(tip); b.onClick = [this, &b, f] { auto p = H.params(); f(p, b.getToggleState()); H.setParams(p); pull(); }; };
        toggle(consBtn, CYAN, "Your consonants and breaths stay yours (s, t, k, f...); only the sung parts take the new voice. Off: everything is blended", [](Params& p, bool v) { p.keepConsonants = v; });
        auto bind = [this](Knob& k, std::function<void(Params&, double)> f) { k.onChange = [this, f, &k](double v) { auto p = H.params(); f(p, v); H.setParams(p, true, k.name); P = p; seenParams = H.paramsVersion.load(); }; k.onStart = [this] { H.beginGesture(); }; k.onEnd = [this] { H.endGesture(); }; };
        bind(kClarity, [](Params& p, double v) { p.clarity = (float) v; }); bind(kFormant, [](Params& p, double v) { p.formant = (float) v; }); bind(kBright, [](Params& p, double v) { p.brightness = (float) v; }); bind(kWarmth, [](Params& p, double v) { p.warmth = (float) v; });
        bind(kBreath, [](Params& p, double v) { p.breath = (float) v; }); bind(kGrit, [](Params& p, double v) { p.grit = (float) v; }); bind(kReso, [](Params& p, double v) { p.resonance = (float) v; }); bind(kWeight, [](Params& p, double v) { p.weight = (float) v; }); bind(kGender, [](Params& p, double v) { p.genderAge = (float) v; });
        bind(kRetune, [](Params& p, double v) { p.retuneMs = (float) v; }); bind(kHumanize, [](Params& p, double v) { p.humanize = (float) v; }); bind(kSemis, [](Params& p, double v) { p.semis = (float) v; });
        bind(kDetune, [](Params& p, double v) { p.detuneCents = (float) v; }); bind(kSpread, [](Params& p, double v) { p.spreadMs = (float) v; }); bind(kWidth, [](Params& p, double v) { p.width = (float) v; });
        bind(kOctDown, [](Params& p, double v) { p.octDown = (float) v; }); bind(kOctUp, [](Params& p, double v) { p.octUp = (float) v; }); bind(kGain, [](Params& p, double v) { p.gainDb = (float) v; });
        kGender.textFn = [](double v) { return v < -0.05 ? "bigger " + juce::String((int) std::round(-v * 100)) : v > 0.05 ? "smaller " + juce::String((int) std::round(v * 100)) : juce::String("as is"); };
        kFormant.textFn = [](double v) { return (v > 0 ? "+" : "") + juce::String(v, 1) + " st"; }; kSemis.textFn = [](double v) { return (v > 0 ? "+" : "") + juce::String((int) v) + " st"; };
        // pitch
        toggle(tuneBtn, BLUE, "Pull the sung notes to a key (ROOT / SCALE). Off = the pitch exactly as sung", [](Params& p, bool v) { p.tune = v; });
        for (auto* b : { &natBtn, &hardBtn }) { tint(*b, BLUE); b->setRadioGroupId(71); b->setClickingTogglesState(true); }
        natBtn.setTooltip("NATURAL: glides to each note (RETUNE), vibrato and drift kept"); hardBtn.setTooltip("HARD: every note snapped flat and instantly (the robot effect)");
        natBtn.onClick = [this] { auto p = H.params(); p.tuneMode = 0; H.setParams(p); pull(); }; hardBtn.onClick = [this] { auto p = H.params(); p.tuneMode = 1; H.setParams(p); pull(); };
        toggle(vibBtn, BLUE, "NATURAL tuning keeps your vibrato (on) or calms it (off)", [](Params& p, bool v) { p.keepVibrato = v; });
        auto combo = [this](juce::ComboBox& c, const juce::Colour& col, const juce::String& tip, std::function<void(Params&, int)> f, std::function<int(const Params&)> get) { tint(c, col); c.setTooltip(tip);
            c.onChange = [this, &c, f, get] { auto p = H.params(); if (get(p) == c.getSelectedItemIndex() || c.getSelectedItemIndex() < 0) return; f(p, c.getSelectedItemIndex()); H.setParams(p); pull(); }; };
        { static const char* N[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; for (int i = 0; i < 12; ++i) rootBox.addItem(N[i], i + 1); }
        combo(rootBox, BLUE, "Key root (TUNE)", [](Params& p, int i) { p.root = i; }, [](const Params& p) { return p.root; });
        scaleBox.addItemList(voice::scaleNames(), 1); combo(scaleBox, BLUE, "Scale (TUNE)", [](Params& p, int i) { p.scale = i; }, [](const Params& p) { return p.scale; });
        octBox.addItemList({ "-2 oct", "-1 oct", "octave: as sung", "+1 oct", "+2 oct" }, 1); combo(octBox, BLUE, "Transpose by octaves (the voice's size stays)", [](Params& p, int i) { p.octave = i - 2; }, [](const Params& p) { return p.octave + 2; });
        voicesBox.addItemList({ "1 voice", "2 voices", "3 voices", "4 voices" }, 1); combo(voicesBox, GREEN, "STACK: how many doubled voices sing (2-4 = a ghost choir, stereo)", [](Params& p, int i) { p.voices = i + 1; }, [](const Params& p) { return p.voices - 1; });
        // output
        tint(playBtn, GREEN); playBtn.setTooltip("Hear the new voice (Space). Click the take to play from there"); playBtn.onClick = [this] { if (H.playing.load() == PlayResult) H.stop(); else H.play(PlayResult); };
        tint(stopBtn, RED); stopBtn.onClick = [this] { H.stop(); }; stopBtn.setTooltip("Stop");
        tint(abBtn, CYAN); abBtn.setTooltip("Switch between your dry voice and the new voice at the same spot, loudness-matched (Tab)"); abBtn.onClick = [this] { H.toggleAB(); };
        tint(renderBtn, PURPLE); renderBtn.setTooltip("Render now (on a background thread)"); renderBtn.onClick = [this] { H.requestRender(); };
        autoBtn.setClickingTogglesState(true); tint(autoBtn, PURPLE); autoBtn.setTooltip("Render again after every change"); autoBtn.onClick = [this] { H.autoRender = autoBtn.getToggleState(); if (H.autoRender) H.requestRender(); };
        tint(cancelBtn, RED); cancelBtn.setTooltip("Stop the running analysis / render"); cancelBtn.onClick = [this] { H.cancel(); flash("Cancelled"); };
        tint(saveBtn, AMBER); saveBtn.setTooltip("Save the blended voice (its fingerprint + these settings) as a preset in Documents/Ghosty/Ghosty Spectrum/Voices. PICK A VOICE finds it again"); saveBtn.onClick = [this] { askSaveName(); };
        tint(exportBtn, GREEN); exportBtn.setTooltip("Save the new voice as a 24-bit WAV file"); exportBtn.onClick = [this] { exportWav(); };
        tint(arrangeBtn, BLUE); arrangeBtn.setTooltip("Put the new voice on the ARRANGE timeline as a clip"); arrangeBtn.onClick = [this] { if (page.onSendToArrange) page.onSendToArrange(); };
        dragOut.setTooltip("Drag the new voice into your DAW / a sampler / the desktop (24-bit WAV). Renders first if it's out of date"); dragOut.setComponentID("ghostVoiceWavOut"); exportBtn.setComponentID("ghostVoiceExport");
        pull();
    }
    ~Impl() { tips.reset(); page.setLookAndFeel(nullptr); }
    void savePrefs() { H.setUiPrefs("look=" + ghosty::themes::toString(look) + ";countin=" + juce::String(countIn ? 1 : 0)); }
    void updateTooltipWindow() { const bool want = ownsTips && (page.getParentComponent() != nullptr || page.isOnDesktop()); if (want && tips == nullptr) tips = std::make_unique<juce::TooltipWindow>(&page, 600); if (!want && tips != nullptr) tips.reset(); }
    bool ownsTips = true;

    // ---- host -> UI
    void pull() {
        P = H.params(); for (int i = 0; i < 3; ++i) S[i] = H.slot(i); aA = S[0] ? S[0]->analysis() : nullptr; R = H.result(); stale = H.resultStale();
        seenParams = H.paramsVersion.load(); seenSource = H.sourceVersion.load(); seenResult = H.resultVersion.load();
        syncBlend(); consBtn.setToggleState(P.keepConsonants, juce::dontSendNotification); kClarity.set(P.clarity);
        kFormant.set(P.formant); kBright.set(P.brightness); kWarmth.set(P.warmth); kBreath.set(P.breath); kGrit.set(P.grit); kReso.set(P.resonance); kWeight.set(P.weight); kGender.set(P.genderAge);
        tuneBtn.setToggleState(P.tune, juce::dontSendNotification); natBtn.setToggleState(P.tuneMode == 0, juce::dontSendNotification); hardBtn.setToggleState(P.tuneMode == 1, juce::dontSendNotification); vibBtn.setToggleState(P.keepVibrato, juce::dontSendNotification);
        rootBox.setSelectedItemIndex(P.root, juce::dontSendNotification); scaleBox.setSelectedItemIndex(P.scale, juce::dontSendNotification); octBox.setSelectedItemIndex(P.octave + 2, juce::dontSendNotification);
        kRetune.set(P.retuneMs); kHumanize.set(P.humanize); kSemis.set(P.semis); voicesBox.setSelectedItemIndex(P.voices - 1, juce::dontSendNotification);
        kDetune.set(P.detuneCents); kSpread.set(P.spreadMs); kWidth.set(P.width); kOctDown.set(P.octDown); kOctUp.set(P.octUp); kGain.set(P.gainDb);
        autoBtn.setToggleState(H.autoRender.load(), juce::dontSendNotification); undoBtn.setEnabled(H.canUndo()); redoBtn.setEnabled(H.canRedo());
        const bool tune = P.tune; for (auto* c : std::initializer_list<juce::Component*> { &natBtn, &hardBtn, &rootBox, &scaleBox }) c->setEnabled(tune);
        kRetune.setEnabled(tune && P.tuneMode == 0); kHumanize.setEnabled(tune && P.tuneMode == 0); vibBtn.setEnabled(tune && P.tuneMode == 0);
        const bool stack = P.voices >= 2; kDetune.setEnabled(stack); kSpread.setEnabled(stack); kWidth.setEnabled(stack || P.octDown > 0 || P.octUp > 0);
        wave.fitRange(); page.repaint();
    }
    void syncBlend() { sB.setValue(std::round(P.wB * 100), juce::dontSendNotification); sC.setValue(std::round(P.wC * 100), juce::dontSendNotification); const bool c = S[2] && !S[2]->empty(); sC.setEnabled(c); sC.setVisible(true); pad.repaint(); }
    juce::String hint() const {
        if (!S[0] || !S[0]->take) return "1. REC (or drop a WAV) - sing a line. 2. VOICE B: record, drop, or PICK A VOICE. 3. Drag the blend dot. PLAY NEW VOICE.";
        if (!S[1] || S[1]->empty()) return "Now VOICE B: record someone (with their permission), drop a file, or PICK A VOICE - then move the blend dot.";
        return "Click the take to hear from there" + DOT + "drag START / END to use a part (double-click = whole take)" + DOT + "A/B = your dry voice vs the new one" + DOT + "SAVE VOICE keeps the blend as a preset";
    }
    void choose(int slot) {
        chooser = std::make_unique<juce::FileChooser>(slot == 0 ? "Load your take" : slot == 1 ? "Load VOICE B" : "Load VOICE C", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, slot](const juce::FileChooser& fc) { const auto f = fc.getResult(); if (f.existsAsFile()) { H.loadFileAsync(slot, f); flash("Loading " + f.getFileName()); } });
    }
    void voiceMenu(int slot, juce::Component& anchor) {
        juce::PopupMenu m; m.addSectionHeader("DESIGNED VOICES"); const auto& D = factoryDesigns(); for (int i = 0; i < (int) D.size(); ++i) m.addItem(100 + i, juce::String(D[(size_t) i].name) + "  -  " + D[(size_t) i].blurb);
        const auto saved = H.presetFiles(); if (!saved.isEmpty()) { m.addSeparator(); m.addSectionHeader("MY VOICES  (Documents/Ghosty/Ghosty Spectrum/Voices)"); for (int i = 0; i < saved.size(); ++i) m.addItem(1000 + i, saved[i]); }
        m.addSeparator(); m.addItem(1, "Open the Voices folder...");
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&anchor), [this, slot, saved](int r) {
            if (r >= 100 && r < 1000) { page.pickFactoryVoice(slot, r - 100); }
            else if (r >= 1000) { juce::String err; if (!H.loadVoice(H.voicesDir().getChildFile(saved[r - 1000] + ".ghostvoice"), slot, false, err)) flash(err); else flash(saved[r - 1000] + " -> " + (slot == 1 ? "VOICE B" : "VOICE C")); pull(); }
            else if (r == 1) H.voicesDir().revealToUser(); });
    }
    void askSaveName() {
        auto ed = std::make_unique<juce::TextEditor>(); ed->setSize(220, 26); auto b = H.slot(SlotB); ed->setText(b && !b->empty() ? "Me + " + b->name() : "My ghost voice", false); ed->selectAll(); ed->setTextToShowWhenEmpty("name this voice", GREY);
        auto* raw = ed.get(); raw->onReturnKey = [this, raw] { juce::String err; if (!H.saveVoice(raw->getText(), err)) flash(err); else flash("Saved \"" + raw->getText().trim() + "\" to Documents/Ghosty/Ghosty Spectrum/Voices"); if (auto* box = raw->findParentComponentOfClass<juce::CallOutBox>()) box->dismiss(); };
        juce::CallOutBox::launchAsynchronously(std::move(ed), saveBtn.getScreenBounds(), nullptr); juce::Timer::callAfterDelay(50, [raw] { raw->grabKeyboardFocus(); });
    }
    void exportWav() {
        if (!exportAllowed(&exportBtn, "spec.ghostvoice.wav-save")) return;
        auto a = H.slot(SlotA); const juce::String base = juce::File::createLegalFileName((a ? a->name() : juce::String("voice")) + " ghost voice").trim();
        ghosty::exportgate::saveDialog(chooser, "Save the new voice as WAV", juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile(base + ".wav"), "*.wav",
            [this](const juce::File& f) { juce::String why; if (H.exportResultTo(f.withFileExtension("wav"), why)) flash("Saved " + f.getFileName()); else flash(why); });
    }
    void paintSections(juce::Graphics& g) {
        card(g, blendArea, AMBER); caption(g, "3  BLEND  -  you vs B (vs C)", blendArea.reduced(12, 7).removeFromTop(15), AMBER);
        card(g, designArea, PURPLE); caption(g, "4  VOICE DESIGN  -  shape the new voice", designArea.reduced(12, 7).removeFromTop(15), PURPLE);
        card(g, pitchArea, BLUE); caption(g, "5  PITCH  -  tune and transpose (the voice's size stays)", pitchArea.reduced(12, 7).removeFromTop(15), BLUE);
        card(g, stackArea, GREEN); caption(g, "6  STACK  -  a ghost choir", stackArea.reduced(12, 7).removeFromTop(15), GREEN);
        card(g, outputArea, GREEN); caption(g, "7  THE NEW VOICE", outputArea.reduced(12, 7).removeFromTop(15), GREEN);
        // progress + status
        const auto pa = progressArea.toFloat(); const int pr = H.progress.load(), job = H.job.load(); g.setColour(CARD2); g.fillRoundedRectangle(pa, 4); juce::String st;
        if (job != 0 && pr >= 0) { g.setColour(PURPLE); g.fillRoundedRectangle(pa.withWidth(pa.getWidth() * pr / 100.0f), 4); static const char* J[] = { "", "listening", "rendering", "", "decoding" }; st = juce::String(J[juce::jlimit(0, 4, job)]) + " " + juce::String(pr) + " %";
            const double left = H.secondsLeft(); if (left > 0) st << DOT << (left < 60 ? juce::String((int) std::ceil(left)) + " s left" : juce::String(left / 60.0, 1) + " min left"); }
        else if (job == 4) st = "decoding...";
        else if (H.isRecording()) st = "recording " + timeText(H.recordedSeconds()); else if (H.isCountingIn()) st = "count-in " + juce::String(H.countInLeft());
        else if (R) st = "new voice " + timeText(R->seconds()) + (R->audio.stereo() ? DOT + "stereo" : juce::String()); else st = S[0] && S[0]->take ? "not rendered yet" : "no take yet";
        if (juce::Time::getMillisecondCounter() < flashUntil) st = flashText; else if (job == 0) { const auto s = H.status(); if (s.isNotEmpty() && juce::Time::getMillisecondCounter() - H.statusTime() < 12000) st = s; }
        g.setColour(job != 0 && pr > 45 ? onFill(PURPLE) : INK); g.setFont(font(9.5f)); g.drawFittedText(st, pa.reduced(6, 0).toNearestInt(), juce::Justification::centredLeft, 1, 0.75f);
        if (stale && S[0] && S[0]->take) { auto s = staleArea.toFloat(); g.setColour(AMBER.withAlpha(0.18f)); g.fillRoundedRectangle(s, 4); g.setColour(AMBER); g.setFont(font(9.5f)); g.drawText(job == 2 || H.autoRender.load() ? "OUT OF DATE - RENDERING..." : "OUT OF DATE - press RENDER", s.toNearestInt(), juce::Justification::centred); }
        // the blend numbers
        { auto r = blendArea.reduced(12, 7); r.removeFromTop(15); g.setColour(INK); g.setFont(font(10.0f)); const int you = (int) std::round(P.wA() * 100), b = (int) std::round(P.wB * 100), c = (int) std::round(P.wC * 100);
          g.drawText("YOU " + juce::String(you) + " %   B " + juce::String(b) + " %" + (S[2] && !S[2]->empty() ? "   C " + juce::String(c) + " %" : juce::String()), pad.getBounds().withY(pad.getBottom() + 2).withHeight(12), juce::Justification::centred); }
    }
};

// =====================================================================================================================
GhostVoicePage::GhostVoicePage(FusionHost& h) : host(h) { impl = std::make_unique<Impl>(*this, h); setSize(1200, 750); setWantsKeyboardFocus(true); startTimerHz(30); }
GhostVoicePage::~GhostVoicePage() { stopTimer(); impl.reset(); }
void GhostVoicePage::setOwnsTooltips(bool owns) { impl->ownsTips = owns; impl->updateTooltipWindow(); }
void GhostVoicePage::setLook(ghosty::themes::Theme t) { impl->look = t; applyTheme(t); impl->lnf.colours(); impl->savePrefs(); impl->wave.cacheKey = {}; sendLookAndFeelChange(); repaint(); }
ghosty::themes::Theme GhostVoicePage::look() const { return impl->look; }
void GhostVoicePage::parentHierarchyChanged() { if (impl) impl->updateTooltipWindow(); }
void GhostVoicePage::refreshNow() { impl->pull(); }
void GhostVoicePage::setKnob(const juce::String& name, double value) { for (auto* k : impl->allKnobs()) if (k->name == name) { k->s.setValue(value, juce::sendNotificationSync); return; } }
juce::String GhostVoicePage::knobText(const juce::String& name) const { for (auto* k : impl->allKnobs()) if (k->name == name) return k->text(k->s.getValue()); return {}; }
juce::StringArray GhostVoicePage::exportForTest(int which) { if (which == 0) return impl->dragOut.make(); impl->exportWav(); return { "started" }; }
bool GhostVoicePage::saveVoiceForTest(const juce::String& name) { juce::String err; const bool ok = host.saveVoice(name, err); if (!ok) impl->flash(err); return ok; }
void GhostVoicePage::pickFactoryVoice(int slot, int index) { const auto& F = factoryIdentities(); if (index < 0 || index >= (int) F.size()) return; host.setIdentity(slot, F[(size_t) index], F[(size_t) index].name); impl->flash(F[(size_t) index].name + " -> " + (slot == 1 ? "VOICE B" : "VOICE C")); impl->pull(); }
juce::String GhostVoicePage::lastMessage() const { return juce::Time::getMillisecondCounter() < impl->flashUntil ? impl->flashText : juce::String(); }
juce::Rectangle<int> GhostVoicePage::boundsOf(const juce::String& n) const { auto& I = *impl; if (n == "wave") return I.wave.getBounds(); if (n == "pad") return I.pad.getBounds(); if (n == "blend") return I.blendArea; if (n == "design") return I.designArea; if (n == "pitch") return I.pitchArea; if (n == "stack") return I.stackArea; if (n == "output") return I.outputArea; if (n == "slotA") return I.slotA.getBounds(); return {}; }
void GhostVoicePage::showAbout(bool show) { impl->about.setVisible(show); if (show) impl->about.toFront(false); }

void GhostVoicePage::resized() {
    auto& I = *impl; auto r = getLocalBounds();
    auto top = r.removeFromTop(44).reduced(16, 8); I.titleArea = top;
    I.infoBtn.setBounds(top.removeFromRight(30)); top.removeFromRight(8); I.redoBtn.setBounds(top.removeFromRight(60)); top.removeFromRight(5); I.undoBtn.setBounds(top.removeFromRight(60)); top.removeFromRight(12); I.countBtn.setBounds(top.removeFromRight(86));
    r = r.reduced(16, 0).withTrimmedBottom(10);
    auto left = r.removeFromLeft(296); r.removeFromLeft(10);
    I.slotA.setBounds(left.removeFromTop(126)); left.removeFromTop(6); I.slotB.setBounds(left.removeFromTop(138)); left.removeFromTop(6); I.slotC.setBounds(left.removeFromTop(138)); left.removeFromTop(6);
    I.outputArea = left;
    { auto o = I.outputArea.reduced(12, 7); o.removeFromTop(16); auto row = o.removeFromTop(26); I.playBtn.setBounds(row.removeFromLeft(row.getWidth() - 60)); row.removeFromLeft(4); I.stopBtn.setBounds(row);
      o.removeFromTop(4); row = o.removeFromTop(24); const int w3 = (row.getWidth() - 8) / 3; I.abBtn.setBounds(row.removeFromLeft(w3 * 2 + 4)); row.removeFromLeft(4); I.cancelBtn.setBounds(row);
      o.removeFromTop(4); row = o.removeFromTop(24); I.renderBtn.setBounds(row.removeFromLeft(w3)); row.removeFromLeft(4); I.autoBtn.setBounds(row.removeFromLeft(w3)); row.removeFromLeft(4); I.kGain.setBounds(row.withHeight(50).translated(0, -6));
      o.removeFromTop(22); I.progressArea = o.removeFromTop(15); o.removeFromTop(3); I.staleArea = o.removeFromTop(15); o.removeFromTop(5);
      I.dragOut.setBounds(o.removeFromTop(32)); o.removeFromTop(5); row = o.removeFromTop(24); I.saveBtn.setBounds(row.removeFromLeft(row.getWidth() / 2 - 2)); row.removeFromLeft(4); I.exportBtn.setBounds(row); o.removeFromTop(4); I.arrangeBtn.setBounds(o.removeFromTop(24)); }
    auto right = r.removeFromRight(300); r.removeFromRight(10);
    I.pitchArea = right.removeFromTop(258); right.removeFromTop(6); I.stackArea = right;
    { auto p = I.pitchArea.reduced(12, 7); p.removeFromTop(18); auto row = p.removeFromTop(24); I.tuneBtn.setBounds(row.removeFromLeft(60)); row.removeFromLeft(6); I.natBtn.setBounds(row.removeFromLeft(72)); row.removeFromLeft(4); I.hardBtn.setBounds(row.removeFromLeft(60)); row.removeFromLeft(6); I.vibBtn.setBounds(row);
      p.removeFromTop(6); row = p.removeFromTop(24); I.rootBox.setBounds(row.removeFromLeft(62)); row.removeFromLeft(6); I.scaleBox.setBounds(row);
      p.removeFromTop(6); auto kn = p.removeFromTop(78); const int kw = kn.getWidth() / 2; I.kRetune.setBounds(kn.removeFromLeft(kw)); I.kHumanize.setBounds(kn);
      p.removeFromTop(4); row = p.removeFromTop(78); I.octBox.setBounds(row.removeFromLeft(130).withHeight(24).translated(0, 24)); row.removeFromLeft(8); I.kSemis.setBounds(row); }
    { auto s = I.stackArea.reduced(12, 7); s.removeFromTop(18); I.voicesBox.setBounds(s.removeFromTop(24).removeFromLeft(130)); s.removeFromTop(6); auto kn = s.removeFromTop(78); const int kw = kn.getWidth() / 3;
      I.kDetune.setBounds(kn.removeFromLeft(kw)); I.kSpread.setBounds(kn.removeFromLeft(kw)); I.kWidth.setBounds(kn); s.removeFromTop(4); kn = s.removeFromTop(78); I.kOctDown.setBounds(kn.removeFromLeft(kw)); I.kOctUp.setBounds(kn.removeFromLeft(kw)); }
    I.designArea = r.removeFromBottom(130); r.removeFromBottom(8);
    { auto d = I.designArea.reduced(12, 7); d.removeFromTop(18); const int kw = d.getWidth() / 8; for (auto* k : { &I.kFormant, &I.kBright, &I.kWarmth, &I.kBreath, &I.kGrit, &I.kReso, &I.kWeight, &I.kGender }) k->setBounds(d.removeFromLeft(kw)); }
    I.blendArea = r.removeFromBottom(176); r.removeFromBottom(8);
    { auto b = I.blendArea.reduced(12, 7); b.removeFromTop(18); auto padR = b.removeFromLeft(250); I.pad.setBounds(padR.withTrimmedBottom(14)); b.removeFromLeft(14);
      auto sl = b.removeFromLeft(168); I.sB.setBounds(sl.removeFromTop(24)); sl.removeFromTop(8); I.sC.setBounds(sl.removeFromTop(24)); sl.removeFromTop(10); I.consBtn.setBounds(sl.removeFromTop(24)); b.removeFromLeft(10); I.kClarity.setBounds(b.removeFromLeft(76).withHeight(78)); }
    I.wave.setBounds(r);
    I.about.setBounds(getLocalBounds());
}
void GhostVoicePage::paint(juce::Graphics& g) {
    auto& I = *impl; I.usePalette(); const auto& T = ghosty::themes::get(I.look);
    if (T.windowTop != T.windowBottom) { g.setGradientFill(juce::ColourGradient(T.windowTop, 0, 0, T.windowBottom, 0, (float) getHeight(), false)); g.fillAll(); } else g.fillAll(BG);
    auto t = I.titleArea; g.setColour(INK); g.setFont(font(22.0f)); g.drawText("GHOST VOICE", t.removeFromLeft(150), juce::Justification::centredLeft);
    if (darkTheme) { g.setColour(GLOW.withAlpha(0.35f)); g.fillEllipse((float) t.getX() - 160, (float) t.getCentreY() - 3, 6, 6); }
    g.setColour(GREY); g.setFont(font(11.0f, false)); g.drawText("sing it - blend the voices - a singer nobody has heard before", t.withTrimmedLeft(6), juce::Justification::centredLeft);
    I.paintSections(g);
    if (I.dropSlot >= 0) { g.setColour((I.dropSlot == 0 ? CYAN : I.dropSlot == 1 ? AMBER : PINK).withAlpha(0.06f)); g.fillAll(); }
}
void GhostVoicePage::timerCallback() {
    auto& I = *impl; host.pollRecording();
    if (host.paramsVersion.load() != I.seenParams || host.sourceVersion.load() != I.seenSource || host.resultVersion.load() != I.seenResult) I.pull();
    const bool st = host.resultStale(); if (st != I.stale) { I.stale = st; repaint(I.outputArea); }
    I.cancelBtn.setEnabled(host.job.load() != 0); I.dragOut.setEnabled(I.S[0] && I.S[0]->take && (I.R != nullptr || I.aA != nullptr)); I.exportBtn.setEnabled(I.dragOut.isEnabled()); I.arrangeBtn.setEnabled(I.R != nullptr && onSendToArrange != nullptr);
    I.playBtn.setButtonText(host.playing.load() == PlayResult ? "STOP NEW VOICE" : "PLAY NEW VOICE"); I.undoBtn.setEnabled(host.canUndo()); I.redoBtn.setEnabled(host.canRedo());
    const bool moving = host.playPos.load() >= 0 || host.job.load() != 0 || host.isRecording() || host.isCountingIn();
    if (moving || I.wasMoving) { I.wave.repaint(); I.slotA.repaint(); I.slotB.repaint(); I.slotC.repaint(); repaint(I.outputArea); } I.wasMoving = moving;
    if (juce::Time::getMillisecondCounter() < I.flashUntil + 100) repaint(I.outputArea);
}
bool GhostVoicePage::keyPressed(const juce::KeyPress& k) {
    auto& I = *impl; const auto mods = k.getModifiers();
    if (mods.isCommandDown() && k.getKeyCode() == 'Z') { if (mods.isShiftDown()) host.redo(); else host.undo(); I.pull(); return true; }
    if (k == juce::KeyPress::spaceKey) { if (host.playing.load() != PlayNone) host.stop(); else host.play(I.R ? PlayResult : PlayA); return true; }
    if (k == juce::KeyPress::tabKey) { host.toggleAB(); return true; }
    if (k == juce::KeyPress::escapeKey) { if (I.about.isVisible()) I.about.setVisible(false); else if (host.isRecording() || host.isCountingIn()) host.stopRecording(); return true; }
    if (!mods.isAnyModifierKeyDown() && (k.getTextCharacter() == 'r' || k.getTextCharacter() == 'R')) { I.slotA.rec.mouseUp(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), { 1, 1 }, {}, 0, 0, 0, 0, 0, &I.slotA.rec, &I.slotA.rec, juce::Time::getCurrentTime(), { 1, 1 }, juce::Time::getCurrentTime(), 1, false)); return true; }
    return false;
}
bool GhostVoicePage::isInterestedInFileDrag(const juce::StringArray& files) { for (auto& f : files) for (auto& e : FusionHost::audioExtensions()) if (f.endsWithIgnoreCase(e) || f.endsWithIgnoreCase(".ghostvoice")) return true; return false; }
void GhostVoicePage::fileDragEnter(const juce::StringArray& f, int x, int y) { fileDragMove(f, x, y); }
void GhostVoicePage::fileDragMove(const juce::StringArray&, int x, int y) { const int s = impl->slotC.getBounds().contains(x, y) ? 2 : impl->slotB.getBounds().contains(x, y) ? 1 : 0; if (s != impl->dropSlot) { impl->dropSlot = s; repaint(); } }
void GhostVoicePage::fileDragExit(const juce::StringArray&) { impl->dropSlot = -1; repaint(); }
void GhostVoicePage::filesDropped(const juce::StringArray& files, int x, int y) {
    const int s = impl->slotC.getBounds().contains(x, y) ? 2 : impl->slotB.getBounds().contains(x, y) ? 1 : 0; impl->dropSlot = -1;
    for (auto& f : files) { if (f.endsWithIgnoreCase(".ghostvoice")) { juce::String err; if (!host.loadVoice(juce::File(f), std::max(1, s), false, err)) impl->flash(err); continue; } if (isInterestedInFileDrag({ f })) { host.loadFileAsync(s, juce::File(f)); impl->flash("Loading " + juce::File(f).getFileName()); break; } }
    repaint();
}
