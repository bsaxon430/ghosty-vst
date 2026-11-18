#include "VoicePage.h"
#include "WaveRegion.h"   // Shared: START / END / LOOP + the GhostBlock sample gestures

using namespace voice;
namespace {
// ---- palette: LIGHT is the default, DARK is the Spectrum night palette. Each page applies its own palette at the start
//      of every paint (usePalette), so two instances with different themes never recolour each other.
juce::Colour BG, CARD, CARD2, INK, GREY, DIM, PURPLE, GREEN, PINK, AMBER, CYAN, RED, BLUE, WAVE;
bool darkTheme = false;
ghosty::themes::Theme curLook = ghosty::themes::Theme::Light;
// the house looks (Shared/GhostThemes.h): surfaces from the tokens, accents readable on the cards
void applyTheme(ghosty::themes::Theme look) {
    const auto& T = ghosty::themes::get(look); curLook = look; darkTheme = !T.isLight;
    auto rd = [&](juce::Colour c) { return ghosty::themes::readableOn(ghosty::themes::readableOn(c, T.panel, 3.0), T.card, 3.0); };
    BG = T.window; CARD = T.panel; CARD2 = T.card; INK = T.ink; GREY = T.dim; DIM = T.line;
    PURPLE = rd(T.violet); GREEN = rd(T.accent3); PINK = rd(T.accent1); AMBER = rd(T.warn); CYAN = rd(T.accent2); RED = rd(T.bad);
    BLUE = rd(T.isLight ? juce::Colour(0xff2456c8) : juce::Colour(0xff6aa8ff)); WAVE = T.line.interpolatedWith(T.violet, 0.45f);
}
void applyTheme(bool dark) { applyTheme(dark ? ghosty::themes::Theme::Dark : ghosty::themes::Theme::Light); }
juce::Colour onFill(juce::Colour a, juce::Colour b) {   // the most readable label colour on a fill going a -> b
    const auto& T = ghosty::themes::get(curLook); const juce::Colour cand[] = { T.ink, T.onAccent, juce::Colour(0xff0b0814), juce::Colour(0xfff4eefc) };
    juce::Colour best = cand[0]; double bk = -1; for (auto c : cand) { const double k = juce::jmin(ghosty::themes::contrast(c, a), ghosty::themes::contrast(c, b)); if (k > bk) { bk = k; best = c; } } return best;
}
constexpr int W = 1200, H = 790;
const char* kModeNames[] = { "PITCH", "BORROW", "CONTOUR", "VOWEL", "TUNE" };
const char* kModeTips[] = { "Change the pitch and keep the formants (no chipmunk), or let them move",
                            "Borrow the formants of slot B: the sound in A takes on B's vowels and consonants (make a synth speak). Chords / pads use the VOCODE carrier",
                            "Draw a pitch curve onto the sound (snap to a key with a retune glide, or trace the detected pitch and edit it)",
                            "Impose vowels A-E-I-O-U along a drawn lane (morph between them)",
                            "Correct the detected pitch to a key: retune speed, flatten, humanize" };
const char* kModeShort[] = { "Shift the pitch, keep (or move) the formants", "A takes on the formants of B: make a synth speak", "Draw the pitch onto the sound",
                             "Morph vowels A-E-I-O-U along the lane", "Pitch-correct to a key" };
const char* kLaneNames[] = { "VOWEL", "PITCH", "FORMANT", "BREATH" };
const juce::String DOT = juce::String::fromUTF8("  \xc2\xb7  ");
juce::String noteName(int m) { static const char* N[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return juce::String(N[((m % 12) + 12) % 12]) + juce::String(m / 12 - 1); }
juce::String noteNameF(double midi) { const int n = (int) std::lround(midi); const int c = (int) std::lround((midi - n) * 100); return noteName(n) + (c == 0 ? juce::String() : (c > 0 ? " +" : " ") + juce::String(c) + "c"); }
juce::String hzText(double hz) { return hz >= 1000 ? juce::String(hz / 1000.0, 2) + " kHz" : juce::String((int) std::lround(hz)) + " Hz"; }
juce::String timeText(double s) { const int m = (int) (s / 60); return m > 0 ? juce::String(m) + ":" + juce::String(s - m * 60, 2).paddedLeft('0', 5) : juce::String(s, 2) + " s"; }
juce::Font font(float h, bool bold = true) { return juce::FontOptions(h, bold ? juce::Font::bold : juce::Font::plain); }
void card(juce::Graphics& g, juce::Rectangle<int> r, juce::Colour edge) {
    g.setColour(CARD); g.fillRoundedRectangle(r.toFloat(), 12.0f); g.setColour(edge.withAlpha(darkTheme ? 0.3f : 0.22f)); g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 12.0f, 1.0f);
}
void caption(juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r, juce::Colour c) { g.setColour(c); g.setFont(font(10.5f)); g.drawText(s, r, juce::Justification::centredLeft); }
// a tint remembers WHICH palette entry it is (not its value), so a theme switch re-colours every control
void tint(juce::Component& b, const juce::Colour& paletteEntry) { b.getProperties().set("tint", (juce::int64) reinterpret_cast<std::intptr_t>(&paletteEntry)); }
juce::Colour tintOf(const juce::Component& b, juce::Colour def) {
    if (!b.getProperties().contains("tint")) return def;
    return *reinterpret_cast<const juce::Colour*>(static_cast<std::intptr_t>((juce::int64) b.getProperties()["tint"]));
}

struct LnF : juce::LookAndFeel_V4 {
    void colours() {
        setColour(juce::ComboBox::backgroundColourId, CARD2); setColour(juce::ComboBox::textColourId, INK); setColour(juce::ComboBox::arrowColourId, PURPLE); setColour(juce::ComboBox::outlineColourId, PURPLE.withAlpha(0.4f));
        setColour(juce::PopupMenu::backgroundColourId, CARD); setColour(juce::PopupMenu::textColourId, INK); setColour(juce::PopupMenu::highlightedBackgroundColourId, PURPLE.withAlpha(0.3f)); setColour(juce::PopupMenu::highlightedTextColourId, INK);
        setColour(juce::TextButton::textColourOffId, INK); setColour(juce::TextButton::textColourOnId, darkTheme ? INK : juce::Colours::white); setColour(juce::Label::textColourId, INK);
        setColour(juce::TooltipWindow::backgroundColourId, CARD); setColour(juce::TooltipWindow::textColourId, INK); setColour(juce::TooltipWindow::outlineColourId, PURPLE);
        setColour(juce::BubbleComponent::backgroundColourId, CARD); setColour(juce::BubbleComponent::outlineColourId, PURPLE);
        setColour(juce::Slider::textBoxTextColourId, INK); setColour(juce::TextEditor::backgroundColourId, CARD2); setColour(juce::TextEditor::textColourId, INK); setColour(juce::TextEditor::outlineColourId, PURPLE.withAlpha(0.4f));
        setColour(juce::TextEditor::focusedOutlineColourId, PURPLE); setColour(juce::CaretComponent::caretColourId, INK); setColour(juce::ScrollBar::thumbColourId, PURPLE.withAlpha(0.45f));
        setColour(juce::Slider::backgroundColourId, CARD2); setColour(juce::Slider::trackColourId, CYAN.withAlpha(0.6f));
    }
    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override {
        auto r = b.getLocalBounds().toFloat().reduced(1); const bool on = b.getToggleState(); const auto t = tintOf(b, PURPLE);
        if (on) { g.setGradientFill(juce::ColourGradient(t.withAlpha(0.95f), r.getX(), r.getY(), t.interpolatedWith(PINK, 0.35f).withAlpha(0.88f), r.getRight(), r.getBottom(), false)); g.fillRoundedRectangle(r, 8); }
        else { g.setColour(CARD2.withAlpha(b.isEnabled() ? 1.0f : 0.5f)); g.fillRoundedRectangle(r, 8); }
        g.setColour((on ? t.brighter(0.3f) : t).withAlpha(b.isEnabled() ? (over ? 0.95f : (on ? 0.6f : 0.42f)) : 0.15f)); g.drawRoundedRectangle(r, 8, down ? 2.0f : 1.1f);
    }
    void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) override {
        g.setFont(getTextButtonFont(b, b.getHeight()));
        const auto t = tintOf(b, PURPLE);   // on a filled button: the label colour that reads on that fill (house tokens)
        g.setColour(b.getToggleState() ? onFill(t, t.interpolatedWith(PINK, 0.35f)) : INK.withAlpha(b.isEnabled() ? 0.92f : 0.3f));
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
        g.setColour(t); g.strokePath(ar, juce::PathStrokeType(th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const auto tip = c.getCentre().getPointOnCircumference(sz / 2 - th - 5, ang); g.setColour(INK.withAlpha(s.isEnabled() ? 1.0f : 0.3f)); g.drawLine({ c.getCentre().getPointOnCircumference(sz * 0.12f, ang), tip }, 2.0f);
    }
    void drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider& s) override {
        auto r = juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h).reduced(0, 1); g.setColour(CARD2); g.fillRoundedRectangle(r, 6);
        g.setColour(tintOf(s, CYAN).withAlpha(0.55f)); g.fillRoundedRectangle(r.withWidth(pos - x), 6);
        g.setColour(INK); g.setFont(font(10.5f)); g.drawText(s.getName() + "  " + s.getTextFromValue(s.getValue()), r.reduced(8, 0), juce::Justification::centredLeft);
    }
};

// a rotary knob with its label + value; SNAP knobs round to whole semitones unless Shift / Alt is held;
// right-click types a value
struct Knob : juce::Component {
    juce::Slider s { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox }; juce::String name, unit; int decimals = 1; bool snap = false, testFine = false, small = false;
    std::function<void(double)> onChange; std::function<void()> onStart, onEnd; std::function<juce::String(double)> textFn;
    Knob(const juce::String& n, const juce::Colour& c, double lo, double hi, double def, double step, const juce::String& u, int dec, const juce::String& tip, bool snapSemis = false)
        : name(n), unit(u), decimals(dec), snap(snapSemis) {
        s.setRange(lo, hi, step); s.setValue(def, juce::dontSendNotification); s.setDoubleClickReturnValue(true, def); s.setPopupDisplayEnabled(true, true, nullptr);
        s.textFromValueFunction = [this](double v) { return text(v); };
        tint(s, c); s.setTooltip(tip + (snap ? "  (snaps to semitones; Shift / Alt = fine)" : "") + "  (double-click: default, right-click: type a value)");
        s.onValueChange = [this] {
            double v = s.getValue();
            const auto mods = juce::ModifierKeys::currentModifiers; const bool fine = testFine || mods.isShiftDown() || mods.isAltDown();
            if (snap && !fine && std::abs(v - std::round(v)) > 1e-9) { v = std::round(v); s.setValue(v, juce::dontSendNotification); }
            if (onChange) onChange(v);
            repaint(); };
        s.onDragStart = [this] { if (onStart) onStart(); }; s.onDragEnd = [this] { if (onEnd) onEnd(); };
        s.addMouseListener(this, false);
        addAndMakeVisible(s);
    }
    juce::String text(double v) const { return textFn ? textFn(v) : juce::String(v, decimals) + unit; }
    void mouseDown(const juce::MouseEvent& e) override {
        if (!e.mods.isPopupMenu()) return;
        auto ed = std::make_unique<juce::TextEditor>(); ed->setSize(110, 26); ed->setText(juce::String(s.getValue(), decimals), false); ed->selectAll(); ed->setJustification(juce::Justification::centred);
        auto* raw = ed.get(); juce::Component::SafePointer<Knob> safe(this);
        raw->onReturnKey = [raw, safe] { if (safe) { safe->testFine = true; safe->s.setValue(raw->getText().getDoubleValue(), juce::sendNotificationSync); safe->testFine = false; }
                                         if (auto* box = raw->findParentComponentOfClass<juce::CallOutBox>()) box->dismiss(); };
        juce::CallOutBox::launchAsynchronously(std::move(ed), s.getScreenBounds(), nullptr);
        juce::Timer::callAfterDelay(50, [raw] { raw->grabKeyboardFocus(); });
    }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds(); auto lab = r.removeFromBottom(small ? 22 : 26);
        g.setColour(GREY.withAlpha(isEnabled() ? 1.0f : 0.4f)); g.setFont(font(9.0f)); g.drawText(name, lab.removeFromTop(11), juce::Justification::centred);
        g.setColour(INK.withAlpha(isEnabled() ? 1.0f : 0.4f)); g.setFont(font(10.0f, false)); g.drawFittedText(text(s.getValue()), lab, juce::Justification::centred, 1, 0.7f);
    }
    void resized() override { auto r = getLocalBounds(); r.removeFromBottom(small ? 22 : 26); const int d = std::min(r.getWidth(), r.getHeight()); s.setBounds(r.withSizeKeepingCentre(d, d)); }
    void set(double v) { s.setValue(v, juce::dontSendNotification); repaint(); }
};

struct DragOut : juce::Component, juce::SettableTooltipClient {
    juce::String text; std::function<juce::StringArray()> make; bool started = false;
    DragOut(juce::String t, std::function<juce::StringArray()> m) : text(std::move(t)), make(std::move(m)) { setMouseCursor(juce::MouseCursor::DraggingHandCursor); }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat().reduced(1); const bool en = isEnabled();
        g.setColour(GREEN.withAlpha(en ? (isMouseOver() ? 0.24f : 0.13f) : 0.05f)); g.fillRoundedRectangle(r, 8);
        g.setColour(GREEN.withAlpha(en ? 0.9f : 0.3f)); g.drawRoundedRectangle(r, 8, 1.3f);
        juce::Path arrow; const float cx = r.getX() + 16, cy = r.getCentreY(); arrow.startNewSubPath(cx, cy - 6); arrow.lineTo(cx, cy + 5); arrow.startNewSubPath(cx - 4, cy + 1); arrow.lineTo(cx, cy + 5); arrow.lineTo(cx + 4, cy + 1);
        g.strokePath(arrow, juce::PathStrokeType(1.6f));
        g.setColour(INK.withAlpha(en ? 1.0f : 0.4f)); g.setFont(font(11.0f)); g.drawFittedText(text, r.withTrimmedLeft(24).toNearestInt(), juce::Justification::centred, 1, 0.8f);
    }
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    void mouseDown(const juce::MouseEvent&) override { started = false; }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (started || e.getDistanceFromDragStart() < 6 || !isEnabled()) return;
        started = true;
        auto f = make(); f.removeEmptyStrings(); if (!f.isEmpty()) ghosty::exportgate::dragFiles(f, this);   // make() asked the gate
    }
};

// min / max per column of a sound over a time window
void peaksOf(const float* x, int n, int fs, double t0, double t1, int w, std::vector<std::pair<float, float>>& v) {
    v.assign((size_t) std::max(1, w), { 0.0f, 0.0f }); if (n <= 0 || fs <= 0) return;
    for (int c = 0; c < w; ++c) {
        const long a = (long) ((t0 + (t1 - t0) * c / w) * fs), b = std::min((long) n, (long) ((t0 + (t1 - t0) * (c + 1) / w) * fs) + 1); float lo = 0, hi = 0;
        for (long i = std::max(0L, a); i < b; ++i) { lo = std::min(lo, x[i]); hi = std::max(hi, x[i]); } v[(size_t) c] = { lo, hi };
    }
}
} // namespace

// =====================================================================================================================
struct VoicePage::Impl {
    VoicePage& page; VoiceHost& H; LnF lnf; bool dark = false; ghosty::themes::Theme look = ghosty::themes::Theme::Light;
    RenderParams P; AnalysisOptions O;
    SourcePtr A, B; std::shared_ptr<const Analysis> aA, aB; ResultPtr R; bool stale = false;
    int seenParams = -1, seenSource = -1, seenResult = -1;
    double inspectT = -1; int dropSlot = -1; bool wasMoving = false; bool follow = false;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::TooltipWindow> tips;   // only when the page owns its tooltips (see VoicePage::setOwnsTooltips)
    void usePalette() { if (curLook != look) { applyTheme(look); } }

    // ---------------------------------------------------------------------------------------------- waveform + pitch + lanes (source time)
    struct Wave : juce::Component, juce::ScrollBar::Listener {
        Impl& I; double lo = 45, hi = 75; bool userRange = false; double vt0 = 0, vt1 = 1; bool fitTime = true;
        double sel0 = -1, sel1 = -1; int laneKind = 0;
        juce::Point<float> hover { -1, -1 }; enum Drag { None, Draw, LaneDraw, Select, Click } drag = None; bool erasing = false, lineMode = false;
        Curve work; juce::Point<float> downPos, lastPos; double selAnchor = 0;
        juce::Image cache; juce::String cacheKey; juce::ScrollBar bar { false };
        static constexpr int kGutter = 52, kRuler = 18, kLane = 64, kBar = 8;
        explicit Wave(Impl& i) : I(i) { setMouseCursor(juce::MouseCursor::CrosshairCursor); bar.setAutoHide(false); bar.addListener(this); addAndMakeVisible(bar); }
        juce::Rectangle<float> inner() const { auto r = getLocalBounds().toFloat().reduced(8, 6); r.removeFromLeft((float) kGutter); return r; }
        juce::Rectangle<float> ruler() const { return inner().removeFromTop((float) kRuler); }
        juce::Rectangle<float> plot() const { auto r = inner(); r.removeFromTop((float) kRuler); r.removeFromBottom((float) (kLane + kBar + 10)); return r; }
        juce::Rectangle<float> lane() const { auto r = inner(); r.removeFromBottom((float) (kBar + 4)); return r.removeFromBottom((float) kLane); }
        double T() const { return I.A ? std::max(0.2, I.A->seconds()) : 1.0; }
        void clampView() { const double span = juce::jlimit(std::min(0.05, T()), T(), vt1 - vt0); vt0 = juce::jlimit(0.0, T() - span, vt0); vt1 = vt0 + span; fitTime = vt0 <= 1e-9 && vt1 >= T() - 1e-9; syncBar(); }
        void fitAll() { vt0 = 0; vt1 = T(); fitTime = true; syncBar(); }
        void syncBar() { bar.setRangeLimits(0.0, T(), juce::dontSendNotification); bar.setCurrentRange(vt0, vt1 - vt0, juce::dontSendNotification); }
        void scrollBarMoved(juce::ScrollBar*, double start) override { const double span = vt1 - vt0; vt0 = start; vt1 = start + span; clampView(); repaint(); }
        void zoom(double factor, double centre) { const double span = juce::jlimit(std::min(0.05, T()), T(), (vt1 - vt0) * factor); const double f = (centre - vt0) / std::max(1e-9, vt1 - vt0); vt0 = centre - f * span; vt1 = vt0 + span; clampView(); repaint(); }
        void scroll(double dt) { vt0 += dt; vt1 += dt; clampView(); repaint(); }
        float xOf(double t) const { const auto p = plot(); return p.getX() + (float) ((t - vt0) / (vt1 - vt0)) * p.getWidth(); }
        double tOf(float x) const { const auto p = plot(); return juce::jlimit(0.0, T(), vt0 + (x - p.getX()) / p.getWidth() * (vt1 - vt0)); }
        float yOf(double m) const { const auto p = plot(); return p.getY() + (float) ((hi - m) / (hi - lo)) * p.getHeight(); }
        double mOf(float y) const { const auto p = plot(); return hi - (y - p.getY()) / p.getHeight() * (hi - lo); }
        // lane value ranges
        double laneLo() const { return laneKind == 0 ? 0 : laneKind == 3 ? -1 : -12; }
        double laneHi() const { return laneKind == 0 ? (I.P.hasCustomVowel ? 5 : 4) : laneKind == 3 ? 1 : 12; }
        float laneY(double v) const { const auto l = lane(); return l.getY() + 6 + (float) ((laneKind == 0 ? (v - laneLo()) : (laneHi() - v)) / (laneHi() - laneLo())) * (l.getHeight() - 12); }
        double laneV(float y) const { const auto l = lane(); const double f = juce::jlimit(0.0, 1.0, (double) ((y - l.getY() - 6) / (l.getHeight() - 12))); return laneKind == 0 ? laneLo() + f * (laneHi() - laneLo()) : laneHi() - f * (laneHi() - laneLo()); }
        Curve& laneOf(RenderParams& p) const { return laneKind == 0 ? p.lane : laneKind == 1 ? p.pitchLane : laneKind == 2 ? p.formantLane : p.breathLane; }
        const Curve& laneOf(const RenderParams& p) const { return laneKind == 0 ? p.lane : laneKind == 1 ? p.pitchLane : laneKind == 2 ? p.formantLane : p.breathLane; }
        bool resultOnAxis() const { return I.R && std::abs(I.R->params.speed - 1.0f) < 1e-3f && I.R->params.mode != RenderParams::Borrow; }
        void fitRange() {
            if (userRange) return;
            double med = 0;
            if (I.aA) med = I.aA->medianF0();
            const double c = med > 0 ? hzMidi(med) : 57.0; lo = std::floor(c - 13); hi = lo + 26;
            if (I.R) { double a = 1e9, b = -1e9; for (double f : I.R->info.f0) if (f > 0) { a = std::min(a, hzMidi(f)); b = std::max(b, hzMidi(f)); }
                       if (b > a) { lo = std::min(lo, std::floor(a - 2.0)); hi = std::max(hi, std::ceil(b + 2.0)); } }
            if (!I.P.contour.empty()) { float a = 1e9f, b = -1e9f; for (auto& q : I.P.contour.pts) { a = std::min(a, q.v); b = std::max(b, q.v); } lo = std::min(lo, std::floor(a - 3.0)); hi = std::max(hi, std::ceil(b + 3.0)); }
            lo = juce::jlimit(12.0, 110.0, lo); hi = juce::jlimit(lo + 12, 124.0, hi);
        }
        void resized() override { auto r = inner(); bar.setBounds(r.removeFromBottom((float) kBar).toNearestInt()); syncBar(); cacheKey = {}; }
        // ---- the static layer (card, waveforms, grid, ruler, detected + result pitch), redrawn only when something it shows changes
        juce::String keyNow() const {
            juce::String k; k << getWidth() << "x" << getHeight() << "|" << vt0 << "-" << vt1 << "|" << lo << "-" << hi << "|" << (juce::int64) (std::intptr_t) I.A.get() << "|" << (juce::int64) (std::intptr_t) I.aA.get()
              << "|" << (I.R ? (int) I.R->serial : -1) << "|" << (int) darkTheme << "|" << I.P.mode << (int) I.P.contourSnap << I.P.root << I.P.scale << "|" << juce::String(I.P.voicing, 3) << "|" << (int) I.P.fixes.size();
            for (auto& f : I.P.fixes) k << juce::String(f.t0, 3) << f.semis << f.voicing;
            return k;
        }
        void renderCache() {
            const float sc = 1.0f; cache = juce::Image(juce::Image::ARGB, std::max(1, getWidth()), std::max(1, getHeight()), true); juce::Graphics g(cache); g.addTransform(juce::AffineTransform::scale(sc));
            card(g, getLocalBounds(), PURPLE);
            const auto p = plot(); const int w = std::max(1, (int) p.getWidth());
            std::vector<std::pair<float, float>> pk;
            auto drawPeaks = [&](juce::Colour c) { juce::Path path; const float mid = p.getCentreY(), hh = p.getHeight() * 0.48f;
                for (int i = 0; i < (int) pk.size(); ++i) path.addRectangle(p.getX() + i, mid - pk[(size_t) i].second * hh, 1.0f, std::max(1.0f, (pk[(size_t) i].second - pk[(size_t) i].first) * hh));
                g.setColour(c); g.fillPath(path); };
            if (I.A) { peaksOf(I.A->pcm->mono.data(), I.A->pcm->length(), I.A->fs(), vt0, vt1, w, pk); drawPeaks(WAVE.withAlpha(darkTheme ? 0.35f : 0.45f)); }
            if (I.R && resultOnAxis()) { const auto& L = I.R->audio.L; peaksOf(L.data(), (int) L.size(), I.R->audio.fs, vt0, vt1, w, pk); drawPeaks(PURPLE.withAlpha(darkTheme ? 0.22f : 0.16f)); }
            const bool snapRows = (I.P.mode == RenderParams::Contour && I.P.contourSnap) || I.P.mode == RenderParams::Tune;
            g.setFont(font(9.5f));
            for (int m = (int) std::ceil(lo); m <= (int) std::floor(hi); ++m) {
                const float y = yOf(m); const bool c = ((m % 12) + 12) % 12 == 0;
                const bool inKey = std::abs(snapMidi((float) m, I.P.root, I.P.scale) - (float) m) < 0.01f;
                if (snapRows && inKey) { g.setColour(AMBER.withAlpha(0.07f)); g.fillRect(p.getX(), y - p.getHeight() / (float) (hi - lo) * 0.5f, p.getWidth(), p.getHeight() / (float) (hi - lo)); }
                g.setColour(INK.withAlpha(c ? 0.16f : (snapRows && !inKey ? 0.02f : 0.06f))); g.drawHorizontalLine((int) y, p.getX(), p.getRight());
                if (c || (hi - lo) < 30) { g.setColour(c ? INK.withAlpha(0.85f) : GREY.withAlpha(snapRows && !inKey ? 0.35f : 0.8f)); g.drawText(noteName(m), (int) p.getX() - kGutter, (int) y - 6, kGutter - 6, 12, juce::Justification::centredRight); }
            }
            // ruler
            const auto ru = ruler(); g.setColour(CARD2.withAlpha(0.6f)); g.fillRect(ru);
            g.setColour(GREY); g.setFont(font(9.5f, false)); const double span = vt1 - vt0;
            const double step = span > 120 ? 20 : span > 30 ? 5 : span > 12 ? 2 : span > 4 ? 0.5 : span > 1 ? 0.25 : span > 0.3 ? 0.05 : 0.01;
            for (double t = std::ceil(vt0 / step) * step; t <= vt1 + 1e-9; t += step) { const float x = xOf(t); if (x > p.getRight() - 40) break; g.drawVerticalLine((int) x, ru.getBottom() - 4, ru.getBottom()); g.drawText(timeText(t), (int) x + 2, (int) ru.getY() + 1, 60, 14, juce::Justification::centredLeft); }
            g.saveState(); g.reduceClipRegion(p.toNearestInt());
            if (I.aA) {   // A's detected pitch, with the voicing + repairs applied
                const auto ef = effectiveF0(*I.aA, I.P); g.setColour(CYAN.withAlpha(0.75f));
                const int i0 = std::max(0, I.aA->frameAt(vt0) - 1), i1 = std::min(I.aA->frames() - 1, I.aA->frameAt(vt1) + 1); const int stride = std::max(1, (int) ((i1 - i0) / (p.getWidth() * 1.5f)));
                juce::Path dots; for (int i = i0; i <= i1; i += stride) if (ef[(size_t) i] > 0) dots.addEllipse(xOf(I.aA->frameTime(i)) - 1.3f, yOf(hzMidi(ef[(size_t) i])) - 1.3f, 2.6f, 2.6f);
                g.fillPath(dots);
            }
            if (I.R && !I.R->info.f0.empty()) {   // the result's pitch, drawn where it sits in the source
                const auto& f0 = I.R->info.f0; const auto& ts = I.R->info.tSrc; juce::Path path; bool pen = false; float lastX = -1e9f;
                for (size_t i = 0; i < f0.size(); ++i) {
                    if (f0[i] <= 0 || ts[i] < vt0 - 0.1 || ts[i] > vt1 + 0.1) { pen = false; continue; }
                    const float x = xOf(ts[i]), y = yOf(hzMidi(f0[i])); if (pen && (x < lastX - 2 || std::abs(x - lastX) > 40)) pen = false;
                    if (!pen) path.startNewSubPath(x, y); else path.lineTo(x, y); pen = true; lastX = x;
                }
                g.setColour(PINK.withAlpha(0.9f)); g.strokePath(path, juce::PathStrokeType(1.8f));
            }
            g.restoreState();
        }
        void paint(juce::Graphics& g) override {
            I.usePalette();
            if (fitTime && std::abs(vt1 - T()) > 1e-6) fitAll();
            const auto k = keyNow(); if (k != cacheKey || !cache.isValid()) { renderCache(); cacheKey = k; }
            g.drawImageAt(cache, 0, 0);
            const auto p = plot(); const double Tt = T();
            g.saveState(); g.reduceClipRegion(p.toNearestInt());
            // fixes (repairs) as tinted bands
            for (auto& f : I.P.fixes) { const float x0 = xOf(std::min(f.t0, f.t1)), x1 = xOf(std::max(f.t0, f.t1)); if (x1 < p.getX() || x0 > p.getRight()) continue;
                g.setColour((f.voicing < 0 ? RED : f.voicing > 0 ? GREEN : BLUE).withAlpha(0.08f)); g.fillRect(x0, p.getY(), x1 - x0, p.getHeight()); }
            // selection
            if (sel1 > sel0) { const float x0 = xOf(sel0), x1 = xOf(sel1); g.setColour(CYAN.withAlpha(0.10f)); g.fillRect(x0, p.getY(), x1 - x0, p.getHeight()); g.setColour(CYAN.withAlpha(0.8f)); g.drawVerticalLine((int) x0, p.getY(), p.getBottom()); g.drawVerticalLine((int) x1, p.getY(), p.getBottom());
                if (I.P.regionStart >= 0) { g.setColour(AMBER); g.setFont(font(10.0f)); g.drawText("PROCESSED REGION", (int) x0 + 4, (int) p.getY() + 2, 140, 12, juce::Justification::centredLeft); } }
            // the drawn contour
            if (I.P.mode == RenderParams::Contour) {
                const Curve& c = drag == Draw ? work : I.P.contour;
                if (!c.empty()) {
                    juce::Path raw, snapped; const int n = std::max(2, (int) p.getWidth() / 2);
                    for (int i = 0; i <= n; ++i) { const double t = vt0 + (vt1 - vt0) * i / n; const float v = c.at(t); const float x = xOf(t);
                        if (i == 0) { raw.startNewSubPath(x, yOf(v)); snapped.startNewSubPath(x, yOf(snapMidi(v, I.P.root, I.P.scale))); }
                        else { raw.lineTo(x, yOf(v)); snapped.lineTo(x, yOf(snapMidi(v, I.P.root, I.P.scale))); } }
                    g.setColour(AMBER.withAlpha(I.P.contourSnap ? 0.45f : 0.95f)); g.strokePath(raw, juce::PathStrokeType(I.P.contourSnap ? 1.2f : 2.2f));
                    if (I.P.contourSnap) { g.setColour(AMBER); g.strokePath(snapped, juce::PathStrokeType(2.2f)); }
                    int shown = 0; for (auto& q : c.pts) if (q.t >= vt0 && q.t <= vt1) ++shown;
                    if (shown < 300) { g.setColour(AMBER); for (auto& q : c.pts) if (q.t >= vt0 && q.t <= vt1) g.fillEllipse(xOf(q.t) - 2.5f, yOf(q.v) - 2.5f, 5, 5); }
                } else if (drag != Draw && I.A) {
                    g.setColour(AMBER.withAlpha(0.8f)); g.setFont(font(13.0f)); g.drawText("Draw a pitch curve here (TRACE copies the detected pitch to edit it)", p.toNearestInt(), juce::Justification::centred);
                }
            }
            if (hover.x >= p.getX() && hover.x <= p.getRight() && hover.y >= p.getY() && hover.y <= p.getBottom()) {
                g.setColour(INK.withAlpha(0.25f)); g.drawVerticalLine((int) hover.x, p.getY(), p.getBottom()); g.drawHorizontalLine((int) hover.y, p.getX(), p.getRight());
                const double t = tOf(hover.x), m = mOf(hover.y);
                juce::String s = timeText(t) + "   " + noteNameF(m) + "  " + hzText(midiHz(m));
                if (I.aA) { const auto ef = effectiveF0(*I.aA, I.P); const int f = I.aA->frameAt(t); if (ef[(size_t) f] > 0) s << "   A: " << noteNameF(hzMidi(ef[(size_t) f])); else s << "   A: unvoiced"; }
                auto box = juce::Rectangle<float>(hover.x + 10, hover.y - 22, 300, 17); if (box.getRight() > p.getRight()) box.setX(hover.x - 310);
                g.setColour(CARD.withAlpha(0.9f)); g.fillRoundedRectangle(box, 4); g.setColour(INK); g.setFont(font(10.5f)); g.drawText(s, box.reduced(5, 0), juce::Justification::centredLeft);
            }
            g.restoreState();
            // playhead (source time)
            const double pp = I.H.playPos.load(); const int pw = I.H.playing.load();
            if (pp >= 0 && pw != PlayB) { const double ts = (pw == PlayResult && I.R) ? I.R->info.outputToSource(pp) : pp; const float x = xOf(ts); if (x >= p.getX() && x <= p.getRight()) { g.setColour(GREEN); g.drawLine(x, p.getY(), x, p.getBottom(), 1.6f); } }
            // lane strip
            { const auto l = lane(); g.setColour(CARD2); g.fillRoundedRectangle(l, 6);
              if (laneKind == 0) { const auto& V = vowelTable(I.P.vowelSet); const int rows = I.P.hasCustomVowel ? 6 : 5;
                  for (int v = 0; v < rows; ++v) { const float y = laneY(v); g.setColour(INK.withAlpha(0.08f)); g.drawHorizontalLine((int) y, l.getX(), l.getRight()); g.setColour(GREY); g.setFont(font(9.5f));
                      g.drawText(v < 5 ? juce::String(V[(size_t) v].name) : juce::String("X"), (int) l.getX() - kGutter, (int) y - 6, kGutter - 6, 12, juce::Justification::centredRight); } }
              else { g.setColour(INK.withAlpha(0.12f)); g.drawHorizontalLine((int) laneY(0), l.getX(), l.getRight()); g.setColour(GREY); g.setFont(font(9.0f));
                     g.drawText(juce::String(laneHi(), 0), (int) l.getX() - kGutter, (int) l.getY() + 1, kGutter - 6, 11, juce::Justification::centredRight);
                     g.drawText(juce::String(laneLo(), 0), (int) l.getX() - kGutter, (int) l.getBottom() - 12, kGutter - 6, 11, juce::Justification::centredRight); }
              const Curve& c = drag == LaneDraw ? work : laneOf(I.P);
              if (!c.empty()) { juce::Path path; const int n = std::max(2, (int) l.getWidth() / 2);
                  for (int i = 0; i <= n; ++i) { const double t = vt0 + (vt1 - vt0) * i / n; const float x = l.getX() + (float) i / n * l.getWidth(), y = laneY(c.at(t)); if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y); }
                  g.setColour(laneKind == 0 ? GREEN : laneKind == 1 ? PINK : laneKind == 2 ? CYAN : GREY); g.strokePath(path, juce::PathStrokeType(2.0f)); }
              else { g.setColour(GREY); g.setFont(font(10.5f)); g.drawText(laneKind == 0 ? juce::String("Vowel lane: draw A E I O U over time (empty = A)") : juce::String(kLaneNames[laneKind]) + " lane: draw automation (empty = the knob only)", l.toNearestInt(), juce::Justification::centred); } }
            // banner / empty state
            const auto note = I.H.trimmedNote(SlotA);
            if (note.isNotEmpty()) { auto b = p.withHeight(18.0f).reduced(80, 0).translated(0, 2); g.setColour(AMBER.withAlpha(0.9f)); g.fillRoundedRectangle(b, 5); g.setColour(juce::Colours::black); g.setFont(font(10.5f)); g.drawText("A " + note, b, juce::Justification::centred); }
            if (!I.A) {
                g.setColour(INK.withAlpha(0.75f)); g.setFont(font(16.0f)); g.drawText("Drop a voice, a synth, a chord or any sound here", p.toNearestInt().translated(0, -12), juce::Justification::centred);
                g.setColour(GREY); g.setFont(font(12.0f, false)); g.drawText("slot A: the sound to change (its pitch and timing). Up to 5 minutes.", p.toNearestInt().translated(0, 14), juce::Justification::centred);
            } else if (!I.aA) {
                g.setColour(GREY); g.setFont(font(13.0f)); g.drawText(I.A->failed.load() ? "Analysis stopped: press ANALYSE" : "Analysing pitch + formants...", p.toNearestInt().translated(0, 24), juce::Justification::centredTop);
            }
            { // legend
                juce::StringArray names; juce::Array<juce::Colour> cols;
                names.add("detected pitch (A)"); cols.add(CYAN); names.add("result pitch"); cols.add(PINK);
                if (I.P.mode == RenderParams::Contour) { names.add(I.P.contourSnap ? "drawn curve (snapped)" : "drawn curve"); cols.add(AMBER); }
                g.setFont(font(9.5f)); float wsum = 0; for (auto& n : names) wsum += juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), n) + 22;
                auto box = juce::Rectangle<float>(p.getRight() - wsum - 10, p.getBottom() - 20, wsum + 6, 16);
                g.setColour(CARD.withAlpha(0.85f)); g.fillRoundedRectangle(box, 4); float x = box.getX() + 6;
                for (int i = 0; i < names.size(); ++i) { g.setColour(cols[i]); g.fillEllipse(x, box.getCentreY() - 3, 6, 6); x += 10; const float tw = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), names[i]);
                    g.setColour(GREY); g.drawText(names[i], juce::Rectangle<float>(x, box.getY(), tw + 4, box.getHeight()), juce::Justification::centredLeft); x += tw + 12; }
            }
            (void) Tt;
        }
        // ---- mouse: ruler drag = select (click = audition); plot: CONTOUR draws, otherwise drag = select / click = audition;
        //      lane: draw. Right / alt = erase, shift = straight line, double-click = delete the nearest point.
        void pencil(juce::Point<float> a, juce::Point<float> b, bool inLane) {
            double t0 = tOf(a.x), t1 = tOf(b.x); if (t0 > t1) { std::swap(t0, t1); std::swap(a, b); }
            auto val = [&](juce::Point<float> q) { return inLane ? (float) laneV(q.y) : (float) mOf(q.y); };
            work.erase(t0 - 1e-6, t1 + 1e-6);
            if (erasing) return;
            const double res = (vt1 - vt0) / std::max(1.0f, plot().getWidth()) * 3.0;
            const int n = std::max(1, (int) ((t1 - t0) / res));
            for (int i = 0; i <= n; ++i) { const float f = (float) i / n; work.add(t0 + (t1 - t0) * f, val(a) + (val(b) - val(a)) * f, res * 0.4); }
        }
        void mouseMove(const juce::MouseEvent& e) override { hover = e.position; if (plot().contains(e.position)) I.inspect(tOf(e.position.x)); repaint(); }
        void mouseExit(const juce::MouseEvent&) override { hover = { -1, -1 }; repaint(); }
        void mouseDown(const juce::MouseEvent& e) override {
            downPos = lastPos = e.position; erasing = e.mods.isRightButtonDown() || e.mods.isAltDown(); lineMode = e.mods.isShiftDown();
            if (lane().expanded(0, 3).contains(e.position)) { drag = LaneDraw; work = laneOf(I.P); if (!lineMode) pencil(e.position, e.position, true); repaint(); return; }
            if (plot().contains(e.position) && I.P.mode == RenderParams::Contour && !ruler().contains(e.position)) { drag = Draw; work = I.P.contour; if (!lineMode) pencil(e.position, e.position, false); repaint(); return; }
            if (ruler().contains(e.position) || plot().contains(e.position)) { drag = Click; selAnchor = tOf(e.position.x); return; }
            drag = None;
        }
        void mouseDrag(const juce::MouseEvent& e) override {
            hover = e.position;
            if (drag == Click && e.getDistanceFromDragStart() > 4) drag = Select;
            if (drag == Select) { const double t = tOf(e.position.x); sel0 = std::min(selAnchor, t); sel1 = std::max(selAnchor, t); I.selectionChanged(); }
            else if (drag == Draw || drag == LaneDraw) { const bool inLane = drag == LaneDraw;
                if (lineMode) { work = inLane ? laneOf(I.P) : I.P.contour; pencil(downPos, e.position, inLane); } else { pencil(lastPos, e.position, inLane); lastPos = e.position; } }
            repaint();
        }
        void mouseUp(const juce::MouseEvent& e) override {
            if (drag == Click) { const double t = tOf(e.position.x); if (I.R) I.H.play(PlayResult, I.R->info.sourceToOutput(t)); else if (I.A) I.H.play(PlayA, t); }
            else if (drag == Draw || drag == LaneDraw) { auto p = I.H.params(); (drag == LaneDraw ? laneOf(p) : p.contour) = work; drag = None; I.H.setParams(p); I.pull(); }
            drag = None; repaint();
        }
        void mouseDoubleClick(const juce::MouseEvent& e) override {
            if (ruler().contains(e.position)) { fitAll(); repaint(); return; }
            const bool inLane = lane().contains(e.position); if (!inLane && I.P.mode != RenderParams::Contour) return;
            auto p = I.H.params(); Curve& c = inLane ? laneOf(p) : p.contour; int best = -1; float bd = 12;
            for (int i = 0; i < (int) c.pts.size(); ++i) { const float d = std::hypot(xOf(c.pts[(size_t) i].t) - e.position.x, (inLane ? laneY(c.pts[(size_t) i].v) : yOf(c.pts[(size_t) i].v)) - e.position.y); if (d < bd) { bd = d; best = i; } }
            if (best >= 0) { c.pts.erase(c.pts.begin() + best); I.H.setParams(p); I.pull(); }
        }
        void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override {
            const double d = (std::abs(w.deltaY) > 1e-9f ? w.deltaY : w.deltaX) > 0 ? 1 : -1;
            if (std::abs(w.deltaX) > std::abs(w.deltaY) || e.mods.isShiftDown()) { scroll(-d * 0.1 * (vt1 - vt0)); return; }
            if (e.mods.isCommandDown() || e.mods.isCtrlDown() || ruler().contains(e.position)) { zoom(d > 0 ? 0.8 : 1.25, tOf(e.position.x)); return; }
            userRange = true;
            if (e.mods.isAltDown()) { const double c = mOf(e.position.y), span = juce::jlimit(12.0, 72.0, (hi - lo) * (d > 0 ? 0.85 : 1.18)); lo = c - (c - lo) / (hi - lo) * span; hi = lo + span; }
            else { lo += d * 2; hi += d * 2; }
            lo = juce::jlimit(12.0, 110.0, lo); hi = juce::jlimit(lo + 12, 124.0, hi); repaint();
        }
    } wave { *this };

    // ---------------------------------------------------------------------------------------------- formant view + vowel chart
    struct Formant : juce::Component {
        Impl& I; std::vector<juce::Point<float>> trajA, trajR; const void* keyA = nullptr; const void* keyR = nullptr;
        explicit Formant(Impl& i) : I(i) {}
        const void* contrastKey = nullptr; float contrast = 0;
        bool voiceLike() { if (contrastKey != I.aA.get()) { contrastKey = I.aA.get(); contrast = I.aA ? envelopeContrast(*I.aA) : 0; } return I.aA && I.aA->voicedFraction() >= 0.3f && contrast >= 14.0f; }
        juce::String label() { return !I.aA || voiceLike() ? "FORMANTS" : "ENVELOPE PEAKS (A is not a voice)"; }
        void rebuild() {
            if (keyA != I.aA.get()) { keyA = I.aA.get(); trajA.clear(); if (I.aA) { const auto& an = *I.aA; for (int i = 0; i < an.frames(); i += 2) if (an.voiced(i)) trajA.push_back({ an.formants[(size_t) i][1].hz, an.formants[(size_t) i][0].hz }); } }
            if (keyR != I.R.get()) { keyR = I.R.get(); trajR.clear(); if (I.R) { const auto& in = I.R->info; for (size_t i = 0; i < in.formants.size() && i < in.f0.size(); i += 2) if (in.f0[i] > 0) trajR.push_back({ in.formants[i][1].hz, in.formants[i][0].hz }); } }
        }
        void paint(juce::Graphics& g) override {
            I.usePalette(); card(g, getLocalBounds(), CYAN); rebuild();
            auto r = getLocalBounds().reduced(12, 8);
            caption(g, label(), r.removeFromTop(16), GREY);
            auto env = r.removeFromTop((int) (r.getHeight() * 0.42f)); r.removeFromTop(26);
            auto chartCap = r.removeFromTop(13); auto chart = r;
            const double t = I.inspectTime(); const double fMax = 5000;
            auto xF = [&](double f) { return env.getX() + (float) (f / fMax) * env.getWidth(); };
            auto yD = [&](double dbv) { return env.getY() + 12 + (float) (-dbv / 70.0) * (env.getHeight() - 24); };
            g.setColour(CARD2); g.fillRoundedRectangle(env.toFloat(), 5);
            g.setFont(font(8.5f, false));
            for (int f = 1000; f < fMax; f += 1000) { g.setColour(INK.withAlpha(0.07f)); g.drawVerticalLine((int) xF(f), (float) env.getY(), (float) env.getBottom()); }
            auto drawEnv = [&](const std::vector<double>& spec, int bins, int fs, juce::Colour c, float th) {
                double peak = 1e-30; const double binHz = fs / (2.0 * (bins - 1)); const int kMax = std::min(bins - 1, (int) (fMax / binHz));
                for (int k = 0; k <= kMax; ++k) peak = std::max(peak, spec[(size_t) k]);
                juce::Path path; for (int k = 0; k <= kMax; ++k) { const double dbv = juce::jlimit(-70.0, 0.0, 10.0 * std::log10(std::max(1e-30, spec[(size_t) k] / peak))); const float x = xF(k * binHz), y = yD(dbv); if (k == 0) path.startNewSubPath(x, y); else path.lineTo(x, y); }
                g.setColour(c); g.strokePath(path, juce::PathStrokeType(th));
            };
            const bool vl = voiceLike();
        auto marks = [&](const Formants& F, juce::Colour c, bool top) {
                for (int k = 0; k < kFormants; ++k) if (F[(size_t) k].hz > 0 && F[(size_t) k].hz < fMax) {
                    const float x = xF(F[(size_t) k].hz); g.setColour(c.withAlpha(0.5f)); g.drawVerticalLine((int) x, (float) env.getY() + 12, (float) env.getBottom() - 12);
                    g.setColour(c); g.setFont(font(9.0f)); g.drawText((vl ? "F" : "P") + juce::String(k + 1), (int) x - 12, top ? env.getY() + 1 : env.getBottom() - 12, 24, 11, juce::Justification::centred);
                }
            };
            juce::String readout;
            if (I.aA && I.aA->frames() > 0) {
                const auto& an = *I.aA; const int f = an.frameAt(t);
                drawEnv(an.specFrame(f), an.bins(), an.fs, CYAN, 1.5f); marks(an.formants[(size_t) f], CYAN, true);
                readout << "A " << timeText(t) << ": "; for (int k = 0; k < 2; ++k) if (an.formants[(size_t) f][(size_t) k].hz > 0) readout << (vl ? "F" : "P") << (k + 1) << " " << (int) an.formants[(size_t) f][(size_t) k].hz << "  ";
                if (I.P.mode == RenderParams::Borrow && I.aB && I.aB->frames() > 0) { const int fb = I.aB->frameAt(t); drawEnv(I.aB->specFrame(fb), I.aB->bins(), I.aB->fs, AMBER.withAlpha(0.85f), 1.2f); }
            } else { g.setColour(GREY); g.setFont(font(11.0f)); g.drawText("Spectral envelope at the cursor", env, juce::Justification::centred); }
            if (I.R && !I.R->info.formants.empty()) {
                const auto& in = I.R->info; const double to = in.sourceToOutput(t); const int fr = juce::jlimit(0, (int) in.formants.size() - 1, (int) std::lround(to * 1000.0 / in.framePeriod)); marks(in.formants[(size_t) fr], PINK, false);
                readout << " out: "; for (int k = 0; k < 2; ++k) if (in.formants[(size_t) fr][(size_t) k].hz > 0) readout << "F" << (k + 1) << " " << (int) in.formants[(size_t) fr][(size_t) k].hz << "  ";
            }
            g.setColour(GREY); g.setFont(font(8.5f, false)); for (int f = 1000; f < fMax; f += 1000) g.drawText(juce::String(f / 1000) + "k", (int) xF(f) - 12, env.getBottom() + 1, 24, 10, juce::Justification::centred);
            g.setColour(INK); g.setFont(font(9.0f)); g.drawFittedText(readout, juce::Rectangle<int>(env.getX(), env.getBottom() + 12, env.getWidth(), 12), juce::Justification::centredLeft, 1, 0.7f);
            // vowel chart: F2 (log, right to left) across, F1 (down)
            g.setColour(GREY); g.setFont(font(8.5f, false)); g.drawText("vowel chart: A (cyan) / result (pink) / targets (green)", chartCap, juce::Justification::centredLeft);
            g.setColour(CARD2); g.fillRoundedRectangle(chart.toFloat(), 5);
            auto cx = [&](double f2) { return chart.getRight() - 14 - (float) (std::log(f2 / 600.0) / std::log(3300.0 / 600.0)) * (chart.getWidth() - 28); };
            auto cy = [&](double f1) { return chart.getY() + 12 + (float) ((f1 - 200.0) / 900.0) * (chart.getHeight() - 24); };
            auto in = [&](juce::Point<float> q) { return q.x > 0 && q.y > 0; };
            g.saveState(); g.reduceClipRegion(chart);
            g.setColour(CYAN.withAlpha(0.35f)); for (auto& q : trajA) if (in(q)) g.fillEllipse(cx(q.x) - 1.5f, cy(q.y) - 1.5f, 3, 3);
            g.setColour(PINK.withAlpha(0.45f)); for (auto& q : trajR) if (in(q)) g.fillEllipse(cx(q.x) - 1.5f, cy(q.y) - 1.5f, 3, 3);
            for (const auto& v : vowelTable(I.P.vowelSet)) {
                const float x = cx(v.F[1]), y = cy(v.F[0]); g.setColour(CARD.withAlpha(0.8f)); g.fillEllipse(x - 10, y - 10, 20, 20); g.setColour(GREEN); g.drawEllipse(x - 10, y - 10, 20, 20, 1.4f);
                g.setFont(font(10.5f)); g.drawText(v.name, (int) x - 10, (int) y - 10, 20, 20, juce::Justification::centred);
            }
            if (I.P.hasCustomVowel) { const float x = cx(I.P.customF[1]), y = cy(I.P.customF[0]); g.setColour(AMBER); g.drawEllipse(x - 10, y - 10, 20, 20, 1.4f); g.drawText("X", (int) x - 10, (int) y - 10, 20, 20, juce::Justification::centred); }
            if (I.P.mode == RenderParams::VowelMorph) { Vowel cv {}; cv.name = "X"; for (int k = 0; k < kFormants; ++k) { cv.F[k] = I.P.customF[(size_t) k]; cv.BW[k] = 60; cv.dB[k] = 0; }
                const auto v = vowelAt(I.P.lane.empty() ? 0.0f : I.P.lane.at(t), I.P.vowelScale, I.P.vowelSet, I.P.hasCustomVowel ? &cv : nullptr); g.setColour(AMBER); g.drawEllipse(cx(v.F[1]) - 6, cy(v.F[0]) - 6, 12, 12, 2.0f); }
            g.restoreState();
            g.setColour(GREY); g.setFont(font(8.5f, false));
            g.drawText("<- F2", chart.getX() + 6, chart.getBottom() - 12, 60, 10, juce::Justification::centredLeft); g.drawText("F1 v", chart.getRight() - 40, chart.getBottom() - 12, 34, 10, juce::Justification::centredRight);
        }
    } formant { *this };

    // ---------------------------------------------------------------------------------------------- source slots
    struct SlotCard : juce::Component {
        Impl& I; int slot; std::vector<std::pair<float, float>> pk; const void* pkKey = nullptr; int pkW = 0;
        juce::TextButton load { "LOAD" }, play { "PLAY" }, clear { "CLEAR" }, swap { "SWAP" };
        SlotCard(Impl& i, int s) : I(i), slot(s) {
            for (auto* b : { &load, &play, &clear }) { addAndMakeVisible(*b); tint(*b, s == 0 ? CYAN : AMBER); }
            load.setTooltip(s == 0 ? "Load the sound to change (or drop a file on the page). Up to 5 minutes" : "Load the formant source: a voice whose vowels / consonants the sound in A should take on");
            play.setTooltip("Hear this slot (loudness-matched to the result)"); clear.setTooltip("Empty this slot");
            load.onClick = [this] { I.choose(slot); };
            play.onClick = [this] { if (I.H.playing.load() == (slot == 0 ? PlayA : PlayB)) I.H.stop(); else I.H.play(slot == 0 ? PlayA : PlayB, slot == 0 ? std::max(0.0, I.P.trimStart) : 0.0); };
            clear.onClick = [this] { I.H.clearSlot(slot); I.pull(); };
            if (s == 1) { addAndMakeVisible(swap); tint(swap, AMBER); swap.setTooltip("Swap the two slots"); swap.onClick = [this] { I.H.swapSlots(); I.pull(); }; }
        }
        SourcePtr src() const { return slot == 0 ? I.A : I.B; }
        void paint(juce::Graphics& g) override {
            I.usePalette();
            const auto c = slot == 0 ? CYAN : AMBER; card(g, getLocalBounds(), c);
            if (I.dropSlot == slot) { g.setColour(c.withAlpha(0.14f)); g.fillRoundedRectangle(getLocalBounds().toFloat(), 12); g.setColour(c); g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1), 12, 2.0f); }
            auto r = getLocalBounds().reduced(12, 7);
            caption(g, slot == 0 ? "A  SOUND (pitch + timing)" : "B  FORMANT SOURCE", r.removeFromTop(15), c);
            auto s = src();
            auto wv = r.removeFromTop(34).toFloat(); r.removeFromTop(3);
            g.setColour(CARD2); g.fillRoundedRectangle(wv, 5);
            if (s) {
                if (pkKey != s.get() || pkW != (int) wv.getWidth()) { peaksOf(s->pcm->mono.data(), s->pcm->length(), s->fs(), 0, s->seconds(), (int) wv.getWidth(), pk); pkKey = s.get(); pkW = (int) wv.getWidth(); }
                g.setColour(c.withAlpha(0.7f)); for (int i = 0; i < (int) pk.size(); ++i) { const float mid = wv.getCentreY(), hh = wv.getHeight() * 0.46f; g.fillRect(wv.getX() + i, mid - pk[(size_t) i].second * hh, 1.0f, std::max(1.0f, (pk[(size_t) i].second - pk[(size_t) i].first) * hh)); }
                const int pw = I.H.playing.load(); const double pp = I.H.playPos.load();
                if (pp >= 0 && pw == (slot == 0 ? PlayA : PlayB) && s->seconds() > 0) { g.setColour(GREEN); g.drawVerticalLine((int) (wv.getX() + pp / s->seconds() * wv.getWidth()), wv.getY(), wv.getBottom()); }
                g.setColour(INK); g.setFont(font(11.5f)); g.drawText(s->name().isEmpty() ? juce::String("(untitled)") : s->name(), r.removeFromTop(14), juce::Justification::centredLeft, true);
                g.setColour(GREY); g.setFont(font(10.0f, false));
                juce::String info = timeText(s->seconds()) + DOT + juce::String(s->fs() / 1000.0, 1) + " kHz" + DOT + (s->pcm->channels() == 2 ? "stereo" : "mono");
                g.drawText(info, r.removeFromTop(13), juce::Justification::centredLeft);
                juce::String an; auto a = s->analysis();
                if (a) { const double m = a->medianF0(); an = m > 0 ? "pitch " + noteNameF(hzMidi(m)) + DOT + "voiced " + juce::String((int) std::lround(a->voicedFraction() * 100)) + " %" : "no single pitch (chord / noise)"; }
                else an = s->failed.load() ? "not analysed (press ANALYSE)" : "analysing " + juce::String(std::max(0, I.H.progress.load())) + " %";
                g.setColour(a ? INK.withAlpha(0.8f) : AMBER); g.drawText(an, r.removeFromTop(13), juce::Justification::centredLeft);
            } else {
                g.setColour(GREY); g.setFont(font(11.0f)); g.drawText("drop audio here", wv, juce::Justification::centred);
                g.setFont(font(10.0f, false));
                g.drawFittedText(slot == 0 ? "A voice, a synth line, a chord or pad, a bass, drums..." : "A voice (speech or singing). In BORROW, A takes on its vowels + consonants.", r.removeFromTop(28), juce::Justification::centredLeft, 2);
            }
        }
        void resized() override {
            auto r = getLocalBounds().reduced(12, 7); auto row = r.removeFromBottom(22);
            const int n = slot == 1 ? 4 : 3; const int w = (row.getWidth() - (n - 1) * 4) / n;
            load.setBounds(row.removeFromLeft(w)); row.removeFromLeft(4); play.setBounds(row.removeFromLeft(w)); row.removeFromLeft(4); clear.setBounds(row.removeFromLeft(w));
            if (slot == 1) { row.removeFromLeft(4); swap.setBounds(row); }
        }
    } slotA { *this, 0 }, slotB { *this, 1 };

    // ---------------------------------------------------------------------------------------------- about panel
    struct About : juce::Component {
        Impl& I; juce::TextEditor text; juce::TextButton close { "CLOSE" }; int shownDark = -1;
        explicit About(Impl& i) : I(i) {
            text.setMultiLine(true); text.setReadOnly(true); text.setScrollbarsShown(true); text.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
            text.setText(juce::String(
                "ABOUT / CREDITS  -  Ghosty Spectrum by GhostBlock\n\n"
                "Open-source parts inside Ghosty Spectrum, their authors and licences (full texts ship in the installer):\n"
                "- JUCE 8.0.4 (Raw Material Software), the plug-in framework. https://github.com/juce-framework/JUCE\n"
                "- Signalsmith Stretch 1.4.0 + signalsmith-linear 0.6.4 (Signalsmith Audio, MIT): time-stretch / pitch-shift.\n"
                "  https://github.com/Signalsmith-Audio/signalsmith-stretch\n"
                "- WORLD v1.0.1 (M. Morise, modified BSD): the VOICE page's analysis and synthesis. Its notice follows.\n\n")
                + thirdPartyNotice(), false); addAndMakeVisible(text); addAndMakeVisible(close); tint(close, PURPLE);
            close.onClick = [this] { setVisible(false); }; close.setTooltip("Close this panel");
        }
        void paint(juce::Graphics& g) override {
            I.usePalette(); g.fillAll(BG.withAlpha(0.75f)); auto r = getLocalBounds().withSizeKeepingCentre(640, 480); card(g, r, PURPLE);
            if (shownDark != (int) darkTheme) { shownDark = (int) darkTheme; text.applyColourToAllText(INK, true); }
            caption(g, "ABOUT / CREDITS  " + DOT + "  third-party licences", r.reduced(16, 12).removeFromTop(16), INK);
        }
        void resized() override { auto r = getLocalBounds().withSizeKeepingCentre(640, 480).reduced(16, 12); r.removeFromTop(22); close.setBounds(r.removeFromBottom(26).removeFromRight(90)); r.removeFromBottom(8); text.setBounds(r); }
    } about { *this };

    // ---------------------------------------------------------------------------------------------- controls
    juce::TextButton modeBtn[5], undoBtn { "UNDO" }, redoBtn { "REDO" }, themeBtn { "DARK" }, infoBtn { "i" }, backBtn { "<" };
    juce::TextButton fastBtn { "FAST" }, analyseBtn { "ANALYSE" }; juce::ComboBox rangeBox; juce::Slider voicing { juce::Slider::LinearBar, juce::Slider::NoTextBox };
    juce::TextButton keepBtn { "KEEP FORMANTS" }, normBtn { "NORMALISE" }, transBtn { "TRANSIENTS" }, consBtn { "CONSONANTS" }, snapBtn { "SNAP" }, forceBtn { "WHISPER -> NOTE" }, traceBtn { "TRACE" }, clearCurveBtn { "CLEAR" };
    juce::TextButton vowelBtn[5], sweepBtn { "A-E-I-O-U" }, autoSizeBtn { "AUTO SIZE" };
    juce::ComboBox timeBox, carrierBox, rootBox, scaleBox, vsetBox, doubleBox, harmBox[3], laneBox;
    juce::TextButton playBtn { "PLAY RESULT" }, stopBtn { "STOP" }, abBtn { "A/B" }, loopBtn { "LOOP" }, followBtn { "FOLLOW" }, renderBtn { "RENDER" }, autoBtn { "AUTO" }, cancelBtn { "CANCEL" };
    juce::TextButton toABtn { "-> A" }, toBBtn { "-> B" }, setBtn { "SET..." };
    juce::TextButton regionBtn { "PROCESS ONLY THIS" }, zoomSelBtn { "ZOOM" }, octUpBtn { "+12" }, octDnBtn { "-12" }, voicedBtn { "VOICED" }, unvoicedBtn { "UNVOICED" }, resetFixBtn { "RESET" }, grabBtn { "GRAB VOWEL" }, clearSelBtn { "X" }, clearLaneBtn { "CLEAR LANE" };
    // Demo gate - every export asks the host's licence first
    bool exportAllowed(juce::Component* ui, const char* id) { if (!page.allowExport || !page.allowExport(ui != nullptr ? ui : &page, id)) { flash("Exporting is part of the full version"); return false; } return true; }
    DragOut dragOut { "DRAG WAV OUT", [this] { if (!exportAllowed(&dragOut, "spec.voice.wav-drag")) return juce::StringArray(); juce::String why; auto f = H.writeResultWav(why, true); pull(); if (!f.existsAsFile()) { flash(why); return juce::StringArray(); } return juce::StringArray { f.getFullPathName() }; } };
    DragOut dragSet { "DRAG SET", [this] { if (!exportAllowed(&dragSet, "spec.voice.set-drag")) return juce::StringArray(); auto d = H.lastSetFolder(); if (!d.isDirectory()) { flash("Render a SET first"); return juce::StringArray(); } return juce::StringArray { d.getFullPathName() }; } };
    // ---- the source waveform with START / END / LOOP (the GhostBlock sample gestures): START..END = the part of A that is
    //      processed (untouched = the whole file), LOOP = what the previews loop over, the WAV grip = drag that part out
    struct SrcWave : ghosty::WaveRegion {   // one gesture on the source waveform = one undo step
        VoiceHost& H; explicit SrcWave(VoiceHost& h) : H(h) {}
        void mouseDown(const juce::MouseEvent& e) override { H.beginGesture(); WaveRegion::mouseDown(e); }
        void mouseUp(const juce::MouseEvent& e) override { WaveRegion::mouseUp(e); H.endGesture(); }
    } srcWave { H };
    const void* srcPeaksKey = nullptr; juce::Rectangle<int> srcWaveArea;
    static ghosty::WaveRegion::Palette srcPalette() {
        auto P = darkTheme ? ghosty::WaveRegion::Palette() : ghosty::WaveRegion::Palette::light();
        P.bg = CARD; P.line = PURPLE.withAlpha(darkTheme ? 0.3f : 0.22f); P.ink = INK; P.dim = GREY; P.card = CARD2; P.wave = CYAN; P.waveOut = DIM.interpolatedWith(GREY, 0.4f); return P; }
    double srcSeconds() const { return A ? A->seconds() : 0.0; }
    void setupSrcWave() {
        srcWave.setComponentID("voiceSourceWave"); page.addAndMakeVisible(srcWave);
        srcWave.palette = [this] { usePalette(); return srcPalette(); };
        srcWave.emptyText = "drop a sound here (slot A)";
        srcWave.hint = "right-click: START here   |   right-drag: LOOP   |   drag START / END   |   drag WAV out";
        srcWave.setTooltip("Slot A. Drag START / END: only that part is processed (untouched = the whole file). Right-click = START here, right-drag = LOOP "
                           "(the previews loop over it; LOOP pill = on / off). Click = hear from there. Drag the WAV grip out to export the highlighted part.");
        srcWave.onChange = [this](const ghosty::WaveRegion::Region& r) {
            const double T = srcSeconds(); if (T <= 0) return;
            auto p = H.params(); const bool whole = r.start <= 0.0005f && r.end >= 0.9995f;
            const double t0 = whole ? -1.0 : r.start * T, t1 = whole ? -1.0 : r.end * T;
            if (std::abs(t0 - p.trimStart) > 1e-6 || std::abs(t1 - p.trimEnd) > 1e-6) { p.trimStart = t0; p.trimEnd = t1; H.setParams(p, true); P = H.params(); seenParams = H.paramsVersion.load(); wave.repaint(); }
            if (r.loopEnd > r.loopStart) H.setLoop(r.loopOn, r.loopStart * T, r.loopEnd * T); else H.setLoop(false, -1, -1);
            loopBtn.setToggleState(r.loopOn, juce::dontSendNotification);
        };
        srcWave.onAudition = [this](bool down, float f) { if (!down) return; const double t = f * srcSeconds(); if (R) H.play(PlayResult, R->info.sourceToOutput(t)); else if (A) H.play(PlayA, t); };
        srcWave.canExport = [this] { return page.allowExport && page.allowExport(&srcWave, "spec.voice.region-drag"); };
        srcWave.exportRegion = [this](float a, float b) { juce::String why; const double T = srcSeconds(); auto f = H.writeRangeWav(a * T, b * T, why); if (!f.existsAsFile() && why.isNotEmpty()) flash(why); return f; };
        srcWave.onMessage = [this](const juce::String& m) { flash(m.replace("(UNLOCK)", "").trim() == "Drag-out is part of the full version" ? juce::String("Exporting is part of the full version") : m); };
    }
    void syncSrcWave() {   // host state -> the view
        if (srcPeaksKey != A.get()) {
            srcPeaksKey = A.get(); std::vector<float> pk;
            if (A && A->pcm) { const auto& m = A->pcm->mono; const size_t n = m.size(), bins = 1500; pk.assign(bins, 0.0f);
                for (size_t i = 0; i < n; ++i) { auto& q = pk[i * bins / n]; q = std::max(q, std::abs(m[i])); }
                const float top = *std::max_element(pk.begin(), pk.end()); if (top > 0) for (auto& q : pk) q = std::min(1.0f, q / top); }
            srcWave.setLengthSeconds(srcSeconds()); srcWave.setPeaks(std::move(pk));
        }
        const double T = srcSeconds(); ghosty::WaveRegion::Region r;
        if (T > 0) {
            r.start = P.trimStart > 0 ? (float) juce::jlimit(0.0, 1.0, P.trimStart / T) : 0.0f; r.end = P.trimEnd > 0 ? (float) juce::jlimit(0.0, 1.0, P.trimEnd / T) : 1.0f;
            const auto lp = H.loopRange(); if (lp.second > lp.first && lp.first >= 0) { r.loopStart = (float) juce::jlimit(0.0, 1.0, lp.first / T); r.loopEnd = (float) juce::jlimit(0.0, 1.0, lp.second / T); r.loopOn = H.loopEnabled(); }
            else { r.loopStart = r.loopEnd = 0.0f; r.loopOn = false; }
        }
        srcWave.setRegion(r); loopBtn.setToggleState(r.loopOn, juce::dontSendNotification);
    }
    Knob kPitch { "PITCH", PINK, -24, 24, 0, 0.01, " st", 2, "Pitch shift", true };
    Knob kFormant { "FORMANT", CYAN, -12, 12, 0, 0.01, " st", 2, "Move the formants up (smaller) or down (bigger, darker) without changing the pitch", true };
    Knob kSpeed { "SPEED", BLUE, 0.25, 4, 1, 0.01, "x", 2, "Play faster or slower without changing the pitch (lanes stay on their syllables)" };
    Knob kBreath { "BREATH", GREY, -1, 1, 0, 0.01, "", 2, "Less (left) or more (right) breath than the natural, calibrated amount" };
    Knob kGain { "GAIN", GREEN, -24, 12, 0, 0.1, " dB", 1, "Output gain, after NORMALISE (soft-limited at 0 dBFS)" };
    Knob kMix { "MIX", AMBER, 0, 1, 1, 0.01, "", 2, "0 = A's own formants .. 1 = all of B's" };
    Knob kDyn { "DYNAMICS", AMBER, 0, 1, 0.7, 0.01, "", 2, "0 = A's loudness .. 1 = B's (speech rhythm, incl. its pauses)" };
    Knob kBreathB { "B BREATH", AMBER, 0, 1, 0.6, 0.01, "", 2, "How much of B's breath / noisiness is used (RESYNTH)" };
    Knob kAmount { "AMOUNT", AMBER, 0, 1, 1, 0.01, "", 2, "0 = the original pitch .. 1 = the drawn curve" };
    Knob kRetune { "RETUNE", AMBER, 0, 400, 40, 1, " ms", 0, "Glide time to a new note (0 = instant, robotic)" };
    Knob kFlatten { "FLATTEN", AMBER, 0, 1, 0, 0.01, "", 2, "0 = keep vibrato and drift around each note .. 1 = dead flat" };
    Knob kHumanize { "HUMANIZE", AMBER, 0, 1, 0, 0.01, "", 2, "Slower correction on long notes (natural sustain)" };
    Knob kVAmount { "AMOUNT", GREEN, 0, 1, 1, 0.01, "", 2, "0 = A's own vowels .. 1 = the vowel lane" };
    Knob kVSize { "SIZE", GREEN, 0.7, 1.4, 1, 0.01, "x", 2, "Vocal tract size: < 1 = bigger voice, > 1 = smaller voice" };
    Knob kDetune { "DETUNE", PURPLE, 0, 50, 12, 0.5, " c", 1, "DOUBLE: pitch spread of the copies" };
    Knob kSpread { "SPREAD", PURPLE, 0, 60, 15, 0.5, " ms", 1, "DOUBLE: timing spread of the copies" };
    Knob kWidth { "WIDTH", PURPLE, 0, 1, 0.8, 0.01, "", 2, "DOUBLE: stereo width" };
    Knob kHarmLvl[3] { { "H1 LEVEL", PINK, 0, 1, 0.7, 0.01, "", 2, "Level of harmony voice 1" }, { "H2 LEVEL", PINK, 0, 1, 0.7, 0.01, "", 2, "Level of harmony voice 2" }, { "H3 LEVEL", PINK, 0, 1, 0.7, 0.01, "", 2, "Level of harmony voice 3" } };
    juce::String flashText; juce::uint32 flashUntil = 0;
    juce::Rectangle<int> controlsArea, outputArea, progressArea, staleArea, analysisArea, actionsArea;
    std::vector<Knob*> allKnobs() { std::vector<Knob*> k { &kPitch, &kFormant, &kSpeed, &kBreath, &kGain, &kMix, &kDyn, &kBreathB, &kAmount, &kRetune, &kFlatten, &kHumanize, &kVAmount, &kVSize, &kDetune, &kSpread, &kWidth }; for (auto& h : kHarmLvl) k.push_back(&h); return k; }

    Impl(VoicePage& pg, VoiceHost& h) : page(pg), H(h) {
        { const auto pr = H.uiPrefs(); look = pr.contains("look=") ? ghosty::themes::fromString(pr.fromFirstOccurrenceOf("look=", false, false).upToFirstOccurrenceOf(";", false, false), ghosty::themes::Theme::Light)
                                                             : (pr.contains("dark=1") ? ghosty::themes::Theme::Dark : ghosty::themes::Theme::Light);
          dark = look != ghosty::themes::Theme::Light; applyTheme(look); }
        lnf.colours(); page.setLookAndFeel(&lnf); themeBtn.setButtonText(dark ? "LIGHT" : "DARK");
        for (int i = 0; i < 5; ++i) { auto& b = modeBtn[i]; b.setButtonText(kModeNames[i]); b.setTooltip(kModeTips[i] + juce::String("  (key ") + juce::String(i + 1) + ")"); tint(b, i == 0 ? PINK : i == 3 ? GREEN : i == 4 ? BLUE : AMBER);
            b.onClick = [this, i] { setMode(i); }; page.addAndMakeVisible(b); }
        for (auto* c : std::initializer_list<juce::Component*> { &wave, &formant, &slotA, &slotB, &undoBtn, &redoBtn, &themeBtn, &infoBtn, &fastBtn, &analyseBtn, &rangeBox, &voicing,
                                                                 &kPitch, &kFormant, &kSpeed, &kBreath, &kGain, &keepBtn, &normBtn, &transBtn, &kMix, &kDyn, &kBreathB, &timeBox, &carrierBox, &consBtn,
                                                                 &kAmount, &kRetune, &kFlatten, &kHumanize, &snapBtn, &rootBox, &scaleBox, &forceBtn, &traceBtn, &clearCurveBtn, &kVAmount, &kVSize, &sweepBtn, &vsetBox, &autoSizeBtn,
                                                                 &doubleBox, &kDetune, &kSpread, &kWidth, &laneBox, &clearLaneBtn,
                                                                 &regionBtn, &zoomSelBtn, &octUpBtn, &octDnBtn, &voicedBtn, &unvoicedBtn, &resetFixBtn, &grabBtn, &clearSelBtn,
                                                                 &playBtn, &stopBtn, &abBtn, &loopBtn, &followBtn, &renderBtn, &autoBtn, &cancelBtn, &dragOut, &toABtn, &toBBtn, &setBtn, &dragSet })
            page.addAndMakeVisible(*c);
        for (int i = 0; i < 3; ++i) { page.addAndMakeVisible(harmBox[i]); page.addAndMakeVisible(kHarmLvl[i]); }
        page.addChildComponent(about); page.addChildComponent(backBtn);
        for (auto* k : allKnobs()) k->small = true;
        backBtn.onClick = [this] { if (page.onLeave) page.onLeave(); }; backBtn.setTooltip("Back to the other Spectrum pages");
        undoBtn.onClick = [this] { H.undo(); pull(); }; redoBtn.onClick = [this] { H.redo(); pull(); };
        undoBtn.setTooltip("Undo (Ctrl/Cmd+Z)"); redoBtn.setTooltip("Redo (Ctrl/Cmd+Shift+Z)");
        themeBtn.setTooltip("Switch between the light and the dark theme"); themeBtn.onClick = [this] { setDark(!dark); };
        infoBtn.setTooltip("About / credits: the open-source parts inside Ghosty Spectrum and their licences"); infoBtn.onClick = [this] { about.setVisible(true); about.toFront(true); };
        // analysis
        fastBtn.setClickingTogglesState(true); fastBtn.setTooltip("FAST pitch tracking (DIO): quicker on long files; off = QUALITY (Harvest), better on breathy / low voices");
        fastBtn.onClick = [this] { auto o = H.analysisOptions(); o.method = fastBtn.getToggleState() ? AnalysisOptions::Fast : AnalysisOptions::Quality; H.setAnalysisOptions(o); pull(); };
        rangeBox.addItemList({ "Sub / 808  25-400 Hz", "Deep voice / bass  40-400 Hz", "Voice  71-800 Hz", "High voice  120-1200 Hz", "Instrument  40-1600 Hz" }, 1); tint(rangeBox, CYAN);
        rangeBox.setTooltip("The pitch range to look for (also sets how finely the formants are resolved)");
        rangeBox.onChange = [this] { static const double RANGES[5][2] = { { 25, 400 }, { 40, 400 }, { 71, 800 }, { 120, 1200 }, { 40, 1600 } }; const int i = juce::jlimit(0, 4, rangeBox.getSelectedItemIndex());
                                     auto o = H.analysisOptions(); if (std::abs(o.f0Floor - RANGES[i][0]) < 1e-6 && std::abs(o.f0Ceil - RANGES[i][1]) < 1e-6) return; o.f0Floor = RANGES[i][0]; o.f0Ceil = RANGES[i][1]; H.setAnalysisOptions(o); pull(); };
        analyseBtn.setTooltip("Analyse the slots again (after a cancel)"); analyseBtn.onClick = [this] { H.reanalyse(); };
        voicing.setName("VOICING"); voicing.setRange(0.05, 0.95, 0.01); voicing.setValue(0.45, juce::dontSendNotification); tint(voicing, CYAN); voicing.setDoubleClickReturnValue(true, 0.45);
        voicing.setTooltip("How periodic a frame must be to count as voiced (left = more voiced, right = stricter). Instant, no re-analysis");
        voicing.textFromValueFunction = [](double v) { return juce::String(v, 2); };
        voicing.onValueChange = [this] { auto p = H.params(); p.voicing = (float) voicing.getValue(); H.setParams(p, true, "VOICING"); P = p; seenParams = H.paramsVersion.load(); wave.repaint(); };
        voicing.onDragStart = [this] { H.beginGesture(); }; voicing.onDragEnd = [this] { H.endGesture(); };
        // knobs -> params
        auto bind = [this](Knob& k, std::function<void(RenderParams&, double)> f) {
            k.onChange = [this, f, &k](double v) { auto p = H.params(); f(p, v); H.setParams(p, true, k.name); P = p; seenParams = H.paramsVersion.load(); wave.repaint(); formant.repaint(); };
            k.onStart = [this] { H.beginGesture(); }; k.onEnd = [this] { H.endGesture(); };
        };
        bind(kPitch, [](RenderParams& p, double v) { p.pitchSemis = (float) v; }); bind(kFormant, [](RenderParams& p, double v) { p.formantSemis = (float) v; });
        bind(kSpeed, [](RenderParams& p, double v) { p.speed = (float) v; }); bind(kBreath, [](RenderParams& p, double v) { p.breath = (float) v; });
        bind(kGain, [](RenderParams& p, double v) { p.gainDb = (float) v; }); bind(kMix, [](RenderParams& p, double v) { p.borrowMix = (float) v; });
        bind(kDyn, [](RenderParams& p, double v) { p.dynamics = (float) v; }); bind(kBreathB, [](RenderParams& p, double v) { p.breathFromB = (float) v; });
        bind(kAmount, [](RenderParams& p, double v) { p.contourAmount = (float) v; }); bind(kRetune, [](RenderParams& p, double v) { p.retuneMs = (float) v; });
        bind(kFlatten, [](RenderParams& p, double v) { p.flatten = (float) v; }); bind(kHumanize, [](RenderParams& p, double v) { p.humanize = (float) v; });
        bind(kVAmount, [](RenderParams& p, double v) { p.vowelAmount = (float) v; }); bind(kVSize, [](RenderParams& p, double v) { p.vowelScale = (float) v; });
        bind(kDetune, [](RenderParams& p, double v) { p.doubleCents = (float) v; }); bind(kSpread, [](RenderParams& p, double v) { p.doubleMs = (float) v; }); bind(kWidth, [](RenderParams& p, double v) { p.width = (float) v; });
        for (int i = 0; i < 3; ++i) bind(kHarmLvl[i], [i](RenderParams& p, double v) { p.harmony[(size_t) i].level = (float) v; });
        kSpeed.s.setSkewFactorFromMidPoint(1.0);
        auto semisText = [this](double v) { juce::String s = (v > 0 ? "+" : "") + juce::String(v, std::abs(v - std::round(v)) > 1e-3 ? 2 : 0) + " st";
                                            if (aA && aA->medianF0() > 0) s << DOT << noteName((int) std::lround(hzMidi(aA->medianF0()) + v));
                                            return s; };
        kPitch.textFn = semisText; kFormant.textFn = [](double v) { return (v > 0 ? "+" : "") + juce::String(v, std::abs(v - std::round(v)) > 1e-3 ? 2 : 0) + " st"; };
        auto toggle = [this](juce::TextButton& b, const juce::Colour& c, const juce::String& tip, std::function<void(RenderParams&, bool)> f) {
            b.setClickingTogglesState(true); tint(b, c); b.setTooltip(tip);
            b.onClick = [this, &b, f] { auto p = H.params(); f(p, b.getToggleState()); H.setParams(p); pull(); };
        };
        toggle(keepBtn, CYAN, "On: the formants stay where they are (natural pitch shift). Off: they move with the pitch (chipmunk / giant)", [](RenderParams& p, bool v) { p.keepFormants = v; });
        toggle(normBtn, GREEN, "Normalise the result to -1 dBFS peak (then GAIN). Off when a region is processed", [](RenderParams& p, bool v) { p.normalise = v; });
        toggle(transBtn, GREEN, "Keep percussive attacks: the original audio around each onset (drums stay sharp, no pre-echo)", [](RenderParams& p, bool v) { p.transients = v; });
        toggle(consBtn, AMBER, "Where B is unvoiced but audible (s, f, t, k...) the result turns into noise too: consonants come through", [](RenderParams& p, bool v) { p.consonantsFromB = v; });
        toggle(snapBtn, AMBER, "Snap the drawn curve to the notes of the key (with the RETUNE glide)", [](RenderParams& p, bool v) { p.contourSnap = v; });
        toggle(forceBtn, AMBER, "Unvoiced parts (whispers, breaths) follow the curve too and become notes", [](RenderParams& p, bool v) { p.forceVoiced = v; });
        auto combo = [this](juce::ComboBox& c, const juce::Colour& col, const juce::String& tip, std::function<void(RenderParams&, int)> f, std::function<int(const RenderParams&)> get) {
            tint(c, col); c.setTooltip(tip); c.onChange = [this, &c, f, get] { auto p = H.params(); if (get(p) == c.getSelectedItemIndex() || c.getSelectedItemIndex() < 0) return; f(p, c.getSelectedItemIndex()); H.setParams(p); pull(); };
        };
        timeBox.addItemList({ "Timing of B", "Timing of B, A loops", "Stretch B over A" }, 1);
        combo(timeBox, AMBER, "Whose timing the result follows", [](RenderParams& p, int i) { p.timeMode = i; }, [](const RenderParams& p) { return p.timeMode; });
        carrierBox.addItemList({ "Carrier: AUTO", "Carrier: RESYNTH", "Carrier: VOCODE" }, 1);
        combo(carrierBox, AMBER, "RESYNTH rebuilds A from its pitch (best for one voice / line). VOCODE filters A's real audio (keeps chords, pads, detune, distortion). AUTO picks by how voiced A is",
              [](RenderParams& p, int i) { p.carrier = i; }, [](const RenderParams& p) { return p.carrier; });
        { static const char* N[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; for (int i = 0; i < 12; ++i) rootBox.addItem(N[i], i + 1); }
        combo(rootBox, AMBER, "Key root (SNAP, TUNE, HARMONY)", [](RenderParams& p, int i) { p.root = i; }, [](const RenderParams& p) { return p.root; });
        scaleBox.addItemList(scaleNames(), 1);
        combo(scaleBox, AMBER, "Scale (SNAP, TUNE, HARMONY)", [](RenderParams& p, int i) { p.scale = i; }, [](const RenderParams& p) { return p.scale; });
        vsetBox.addItemList({ "Adult male", "Adult female", "Child" }, 1);
        combo(vsetBox, GREEN, "Vowel targets for a male, female or child voice", [](RenderParams& p, int i) { p.vowelSet = i; }, [](const RenderParams& p) { return p.vowelSet; });
        doubleBox.addItem("DOUBLE off", 1); for (int n = 2; n <= 8; ++n) doubleBox.addItem("DOUBLE x" + juce::String(n), n);
        combo(doubleBox, PURPLE, "Stack detuned, delayed copies (stereo)", [](RenderParams& p, int i) { p.doubles = i == 0 ? 0 : i + 1; }, [](const RenderParams& p) { return p.doubles < 2 ? 0 : p.doubles - 1; });
        static const int kSteps[] = { 0, 2, 4, 5, 7, -2, -3, -5, -7 }; static const char* kStepNames[] = { "off", "3rd up", "5th up", "6th up", "octave up", "3rd down", "4th down", "6th down", "octave down" };
        for (int i = 0; i < 3; ++i) { for (int s = 0; s < 9; ++s) harmBox[i].addItem("H" + juce::String(i + 1) + ": " + kStepNames[s], s + 1);
            combo(harmBox[i], PINK, "Harmony voice in the key (ROOT / SCALE)", [i](RenderParams& p, int s) { p.harmony[(size_t) i].steps = kSteps[s]; if (std::abs(p.harmony[(size_t) i].pan) < 1e-6f) p.harmony[(size_t) i].pan = i == 0 ? -0.5f : i == 1 ? 0.5f : 0.0f; },
                  [i](const RenderParams& p) { for (int s = 0; s < 9; ++s) if (kSteps[s] == p.harmony[(size_t) i].steps) return s; return 0; }); }
        laneBox.addItemList({ "Lane: VOWEL", "Lane: PITCH", "Lane: FORMANT", "Lane: BREATH" }, 1); tint(laneBox, GREEN); laneBox.setSelectedItemIndex(0, juce::dontSendNotification);
        laneBox.setTooltip("Which automation lane the strip under the waveform edits"); laneBox.onChange = [this] { wave.laneKind = juce::jmax(0, laneBox.getSelectedItemIndex()); wave.repaint(); };
        tint(clearLaneBtn, GREEN); clearLaneBtn.setTooltip("Empty the selected lane"); clearLaneBtn.onClick = [this] { auto p = H.params(); wave.laneOf(p).clear(); H.setParams(p); pull(); };
        tint(traceBtn, AMBER); traceBtn.setTooltip("Copy the detected pitch of A into the curve, then edit it (re-sing the melody)");
        traceBtn.onClick = [this] { auto c = H.traceContour(); if (c.empty()) { flash("No pitch found in A to trace"); return; } auto p = H.params(); p.contour = c; H.setParams(p); pull(); };
        tint(clearCurveBtn, AMBER); clearCurveBtn.setTooltip("Remove the drawn curve"); clearCurveBtn.onClick = [this] { auto p = H.params(); p.contour.clear(); H.setParams(p); pull(); };
        for (int v = 0; v < 5; ++v) { auto& b = vowelBtn[v]; b.setButtonText(vowelTable()[(size_t) v].name); tint(b, GREEN); b.setTooltip(juce::String("The whole sound on ") + vowelTable()[(size_t) v].name);
            b.onClick = [this, v] { auto p = H.params(); p.lane.clear(); p.lane.add(0, (float) v); H.setParams(p); pull(); }; page.addAndMakeVisible(b); }
        tint(sweepBtn, GREEN); sweepBtn.setTooltip("A lane that walks A-E-I-O-U across the sound");
        sweepBtn.onClick = [this] { auto p = H.params(); p.lane.clear(); const double T = A ? std::max(0.5, A->seconds()) : 2.0; for (int v = 0; v < 5; ++v) { p.lane.add(T * (v + 0.15) / 5.0, (float) v); p.lane.add(T * (v + 0.85) / 5.0, (float) v); } H.setParams(p); pull(); };
        tint(autoSizeBtn, GREEN); autoSizeBtn.setTooltip("Set SIZE from A's own formants (keeps its voice size)");
        autoSizeBtn.onClick = [this] { if (!aA) return; auto p = H.params(); p.vowelScale = fitVowelScale(*aA, p.vowelSet); H.setParams(p); pull(); flash("SIZE " + juce::String(p.vowelScale, 2)); };
        // selection actions
        for (auto* b : { &regionBtn, &zoomSelBtn, &octUpBtn, &octDnBtn, &voicedBtn, &unvoicedBtn, &resetFixBtn, &grabBtn, &clearSelBtn }) tint(*b, CYAN);
        regionBtn.setClickingTogglesState(true); regionBtn.setTooltip("Process only the selection (10 ms crossfades); the rest of A stays untouched");
        regionBtn.onClick = [this] { auto p = H.params(); if (regionBtn.getToggleState() && wave.sel1 > wave.sel0) { p.regionStart = wave.sel0; p.regionEnd = wave.sel1; } else { p.regionStart = p.regionEnd = -1; } H.setParams(p); pull(); };
        zoomSelBtn.setTooltip("Zoom to the selection (Z)"); zoomSelBtn.onClick = [this] { zoomToSelection(); };
        octUpBtn.setTooltip("Repair: move the detected pitch in the selection up an octave"); octUpBtn.onClick = [this] { addFix(12, 0); };
        octDnBtn.setTooltip("Repair: move the detected pitch in the selection down an octave"); octDnBtn.onClick = [this] { addFix(-12, 0); };
        voicedBtn.setTooltip("Repair: treat the selection as voiced (sung)"); voicedBtn.onClick = [this] { addFix(0, 1); };
        unvoicedBtn.setTooltip("Repair: treat the selection as unvoiced (breath / noise)"); unvoicedBtn.onClick = [this] { addFix(0, -1); };
        resetFixBtn.setTooltip("Remove the repairs inside the selection"); resetFixBtn.onClick = [this] { auto p = H.params(); const double a = wave.sel0, b = wave.sel1;
            p.fixes.erase(std::remove_if(p.fixes.begin(), p.fixes.end(), [a, b](const Fix& f) { return f.t1 >= a && f.t0 <= b; }), p.fixes.end()); H.setParams(p); pull(); };
        grabBtn.setTooltip("Capture the vowel of A at the selection's middle as lane position X (6th row)");
        grabBtn.onClick = [this] { if (!aA) return; const auto& F = aA->formants[(size_t) aA->frameAt(0.5 * (wave.sel0 + wave.sel1))]; if (F[0].hz <= 0 || F[1].hz <= 0) { flash("No vowel found there"); return; }
            auto p = H.params(); for (int k = 0; k < kFormants; ++k) p.customF[(size_t) k] = F[(size_t) k].hz > 0 ? F[(size_t) k].hz : p.customF[(size_t) k]; p.hasCustomVowel = true; H.setParams(p); pull(); flash("Vowel X captured: F1 " + juce::String((int) F[0].hz) + ", F2 " + juce::String((int) F[1].hz)); };
        clearSelBtn.setTooltip("Clear the selection (Esc)"); clearSelBtn.onClick = [this] { clearSelection(); };
        // output
        tint(playBtn, GREEN); playBtn.setTooltip("Hear the result (Space). Click the waveform / ruler to play from there");
        playBtn.onClick = [this] { if (H.playing.load() == PlayResult) H.stop(); else H.play(PlayResult); };
        tint(stopBtn, RED); stopBtn.onClick = [this] { H.stop(); }; stopBtn.setTooltip("Stop playing");
        tint(abBtn, CYAN); abBtn.setTooltip("Switch between the result and A at the same spot, loudness-matched (Tab)"); abBtn.onClick = [this] { H.toggleAB(); };
        loopBtn.setClickingTogglesState(true); tint(loopBtn, CYAN); loopBtn.setTooltip("LOOP on / off: the previews loop over the LOOP span (right-drag on the source waveform) (L)"); loopBtn.onClick = [this] { srcWave.toggleLoop(); };
        followBtn.setClickingTogglesState(true); tint(followBtn, CYAN); followBtn.setTooltip("Scroll the view with the playhead"); followBtn.onClick = [this] { follow = followBtn.getToggleState(); };
        tint(renderBtn, PURPLE); renderBtn.setTooltip("Render now"); renderBtn.onClick = [this] { H.requestRender(); };
        autoBtn.setClickingTogglesState(true); tint(autoBtn, PURPLE); autoBtn.setTooltip("Render automatically after every change");
        autoBtn.onClick = [this] { H.autoRender = autoBtn.getToggleState(); if (H.autoRender) H.requestRender(); };
        tint(cancelBtn, RED); cancelBtn.setTooltip("Stop the running analysis / render"); cancelBtn.onClick = [this] { H.cancel(); flash("Cancelled"); };
        tint(toABtn, CYAN); toABtn.setTooltip("Bounce: the result becomes A (chain another process; undoable)"); toABtn.onClick = [this] { if (H.useResultAs(SlotA)) flash("Result -> A"); };
        tint(toBBtn, AMBER); toBBtn.setTooltip("Bounce: the result becomes B (undoable)"); toBBtn.onClick = [this] { if (H.useResultAs(SlotB)) flash("Result -> B"); };
        tint(setBtn, PURPLE); setBtn.setTooltip("RENDER SET: one file per pitch, for samplers / chops");
        setBtn.onClick = [this] {
            juce::PopupMenu m; m.addItem(1, "Chromatic -12 .. +12 (25 files)"); m.addItem(2, "Octaves -24 / -12 / 0 / +12 / +24"); m.addItem(3, "Scale notes, one octave up (ROOT / SCALE)");
            m.addSeparator(); m.addItem(11, "Chromatic -12 .. +12 -> Ghosty shared folder (Beats / Arrange)"); m.addItem(13, "Scale notes -> Ghosty shared folder");
            m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&setBtn), [this](int r) { startRenderSet(r); });
        };
        setupSrcWave();
        dragOut.setTooltip("Drag the result into your DAW / a sampler / the desktop (24-bit WAV). Renders first if it's out of date");
        dragSet.setTooltip("Drag the folder of the last RENDER SET"); dragOut.setComponentID("voiceWavOut"); dragSet.setComponentID("voiceSetOut"); setBtn.setComponentID("voiceSetButton");
        pull();
    }
    ~Impl() { tips.reset(); page.setLookAndFeel(nullptr); }
    // RENDER SET menu choice (1 chromatic, 2 octaves, 3 scale notes; +10 = to the Ghosty shared folder). False = refused / nothing to do
    bool startRenderSet(int r) {
        if (r <= 0 || !exportAllowed(&setBtn, r > 10 ? "spec.voice.render-set-shared" : "spec.voice.render-set")) return false;
        std::vector<int> s; const int kind = r % 10;
        if (kind == 1) for (int i = -12; i <= 12; ++i) s.push_back(i);
        if (kind == 2) s = { -24, -12, 0, 12, 24 };
        if (kind == 3) { const double base = aA && aA->medianF0() > 0 ? hzMidi(aA->medianF0()) + P.pitchSemis : 60; const int b = (int) std::lround(base);
                         for (int i = 0; i <= 12; ++i) if (std::abs(snapMidi((float) (b + i), P.root, P.scale) - (float) (b + i)) < 0.01f) s.push_back(i); }
        if (s.empty()) return false;
        H.renderSet(s, r > 10); flash("Rendering " + juce::String((int) s.size()) + " files..."); return true;
    }
    void updateTooltipWindow() {
        const bool want = page.ownsTooltips && (page.getParentComponent() != nullptr || page.isOnDesktop());
        if (want && tips == nullptr) tips = std::make_unique<juce::TooltipWindow>(&page, 600);
        if (!want && tips != nullptr) tips.reset();
    }

    // ---- host -> UI
    void pull() {
        P = H.params(); O = H.analysisOptions(); A = H.source(SlotA); B = H.source(SlotB); aA = A ? A->analysis() : nullptr; aB = B ? B->analysis() : nullptr; R = H.result(); stale = H.resultStale();
        seenParams = H.paramsVersion.load(); seenSource = H.sourceVersion.load(); seenResult = H.resultVersion.load();
        for (int i = 0; i < 5; ++i) modeBtn[i].setToggleState(P.mode == i, juce::dontSendNotification);
        kPitch.set(P.pitchSemis); kFormant.set(P.formantSemis); kSpeed.set(P.speed); kBreath.set(P.breath); kGain.set(P.gainDb);
        kMix.set(P.borrowMix); kDyn.set(P.dynamics); kBreathB.set(P.breathFromB); kAmount.set(P.contourAmount); kRetune.set(P.retuneMs); kFlatten.set(P.flatten); kHumanize.set(P.humanize);
        kVAmount.set(P.vowelAmount); kVSize.set(P.vowelScale); kDetune.set(P.doubleCents); kSpread.set(P.doubleMs); kWidth.set(P.width);
        for (int i = 0; i < 3; ++i) kHarmLvl[i].set(P.harmony[(size_t) i].level);
        voicing.setValue(P.voicing, juce::dontSendNotification);
        keepBtn.setToggleState(P.keepFormants, juce::dontSendNotification); consBtn.setToggleState(P.consonantsFromB, juce::dontSendNotification); transBtn.setToggleState(P.transients, juce::dontSendNotification);
        snapBtn.setToggleState(P.contourSnap, juce::dontSendNotification); forceBtn.setToggleState(P.forceVoiced, juce::dontSendNotification); normBtn.setToggleState(P.normalise, juce::dontSendNotification);
        timeBox.setSelectedItemIndex(P.timeMode, juce::dontSendNotification); carrierBox.setSelectedItemIndex(P.carrier, juce::dontSendNotification); rootBox.setSelectedItemIndex(P.root, juce::dontSendNotification);
        scaleBox.setSelectedItemIndex(P.scale, juce::dontSendNotification); vsetBox.setSelectedItemIndex(P.vowelSet, juce::dontSendNotification);
        doubleBox.setSelectedItemIndex(P.doubles < 2 ? 0 : P.doubles - 1, juce::dontSendNotification);
        { static const int kSteps[] = { 0, 2, 4, 5, 7, -2, -3, -5, -7 }; for (int i = 0; i < 3; ++i) { int idx = 0; for (int s = 0; s < 9; ++s) if (kSteps[s] == P.harmony[(size_t) i].steps) idx = s; harmBox[i].setSelectedItemIndex(idx, juce::dontSendNotification); } }
        fastBtn.setToggleState(O.method == AnalysisOptions::Fast, juce::dontSendNotification);
        const int ri = O.f0Floor <= 30 ? 0 : O.f0Ceil >= 1500 ? 4 : O.f0Floor >= 110 ? 3 : O.f0Floor <= 45 ? 1 : 2; rangeBox.setSelectedItemIndex(ri, juce::dontSendNotification);
        autoBtn.setToggleState(H.autoRender.load(), juce::dontSendNotification);
        regionBtn.setToggleState(P.regionStart >= 0, juce::dontSendNotification);
        undoBtn.setEnabled(H.canUndo()); redoBtn.setEnabled(H.canRedo());
        updateCarrier(); syncSrcWave();
        if (wave.fitTime) wave.fitAll(); else wave.clampView();
        wave.fitRange(); layoutMode(); page.repaint();
    }
    void setMode(int m) { auto p = H.params(); if (p.mode == m) return; p.mode = m; if (m == RenderParams::VowelMorph) { wave.laneKind = 0; laneBox.setSelectedItemIndex(0, juce::dontSendNotification); } H.setParams(p); pull(); }
    void setDark(bool d) { setLook(d ? ghosty::themes::Theme::Dark : ghosty::themes::Theme::Light); }
    void setLook(ghosty::themes::Theme lk) { look = lk; dark = lk != ghosty::themes::Theme::Light; applyTheme(lk); lnf.colours(); H.setUiPrefs(juce::String(dark ? "dark=1" : "dark=0") + ";look=" + ghosty::themes::toString(lk)); themeBtn.setButtonText(dark ? "LIGHT" : "DARK"); page.sendLookAndFeelChange(); page.repaint(); }
    void inspect(double t) { inspectT = t; formant.repaint(); }
    double cachedInspect = 0; const void* cachedInspectKey = nullptr;
    double inspectTime() {
        if (inspectT >= 0) return inspectT;
        if (aA.get() != cachedInspectKey) { cachedInspectKey = aA.get(); cachedInspect = 0; if (aA) { int c = 0, mid = 0; for (int i = 0; i < aA->frames(); ++i) c += aA->voiced(i); int k = 0; for (int i = 0; i < aA->frames(); ++i) if (aA->voiced(i) && ++k == c / 2) { mid = i; break; } cachedInspect = aA->frameTime(mid); } }
        return cachedInspect;
    }
    void flash(const juce::String& s) { flashText = s; flashUntil = juce::Time::getMillisecondCounter() + 3500; page.repaint(outputArea); }
    void choose(int slot) {
        chooser = std::make_unique<juce::FileChooser>(slot == 0 ? "Load the sound (slot A)" : "Load the formant source (slot B)", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, slot](const juce::FileChooser& fc) {
            const auto f = fc.getResult(); if (f.existsAsFile()) { H.loadFileAsync(slot, f); flash("Loading " + f.getFileName()); } });
    }
    // ---- selection
    void selectionChanged() { const bool has = wave.sel1 > wave.sel0 + 0.005; for (auto* b : { &regionBtn, &zoomSelBtn, &octUpBtn, &octDnBtn, &voicedBtn, &unvoicedBtn, &resetFixBtn, &grabBtn, &clearSelBtn }) b->setEnabled(has);
                              page.repaint(actionsArea); }
    void clearSelection() { wave.sel0 = wave.sel1 = -1; if (P.regionStart >= 0) { auto p = H.params(); p.regionStart = p.regionEnd = -1; H.setParams(p); pull(); } selectionChanged(); wave.repaint(); }
    void zoomToSelection() { if (wave.sel1 <= wave.sel0) return; const double pad = 0.05 * (wave.sel1 - wave.sel0); wave.vt0 = wave.sel0 - pad; wave.vt1 = wave.sel1 + pad; wave.clampView(); wave.repaint(); }
    void addFix(float semis, int v) { if (wave.sel1 <= wave.sel0) return; auto p = H.params(); p.fixes.push_back({ wave.sel0, wave.sel1, semis, v }); H.setParams(p); pull(); }
    // ---- layout
    bool autoVocode = false; const void* autoKey = nullptr; float autoVoicing = -1;
    void updateCarrier() { if (aA.get() != autoKey || std::abs(autoVoicing - P.voicing) > 1e-6f) { autoKey = aA.get(); autoVoicing = P.voicing; autoVocode = aA && A && preferVocode(*aA, A->pcm->ref(), P.voicing); } }
    bool vocodeActive() const { return P.mode == RenderParams::Borrow && (P.carrier == RenderParams::CarrierVocode || (P.carrier == RenderParams::CarrierAuto && autoVocode)); }
    void layoutMode() {
        const int m = P.mode;
        for (auto* c : std::initializer_list<juce::Component*> { &kMix, &kDyn, &kBreathB, &timeBox, &carrierBox, &consBtn }) c->setVisible(m == RenderParams::Borrow);
        for (auto* c : std::initializer_list<juce::Component*> { &kAmount, &snapBtn, &forceBtn, &traceBtn, &clearCurveBtn }) c->setVisible(m == RenderParams::Contour);
        for (auto* c : std::initializer_list<juce::Component*> { &kFlatten, &kHumanize }) c->setVisible(m == RenderParams::Tune);
        kRetune.setVisible(m == RenderParams::Tune || m == RenderParams::Contour);
        for (auto* c : std::initializer_list<juce::Component*> { &rootBox, &scaleBox }) c->setVisible(m == RenderParams::Contour || m == RenderParams::Tune);
        for (auto* c : std::initializer_list<juce::Component*> { &kVAmount, &kVSize, &sweepBtn, &vsetBox, &autoSizeBtn }) c->setVisible(m == RenderParams::VowelMorph);
        for (auto& b : vowelBtn) b.setVisible(m == RenderParams::VowelMorph);
        const bool voc = vocodeActive();
        for (auto* c : std::initializer_list<juce::Component*> { &doubleBox, &kDetune, &kSpread, &kWidth, &harmBox[0], &harmBox[1], &harmBox[2], &kHarmLvl[0], &kHarmLvl[1], &kHarmLvl[2], &kSpeed, &kBreath, &keepBtn, &transBtn }) c->setEnabled(!voc);
        kBreathB.setEnabled(!voc);
        const bool dbl = P.doubles >= 2; kDetune.setEnabled(!voc && dbl); kSpread.setEnabled(!voc && dbl); kWidth.setEnabled(!voc && dbl);
        for (int i = 0; i < 3; ++i) kHarmLvl[i].setEnabled(!voc && P.harmony[(size_t) i].steps != 0);
        rootBox.setEnabled(P.contourSnap || m == RenderParams::Tune); scaleBox.setEnabled(P.contourSnap || m == RenderParams::Tune);
        selectionChanged(); layoutControls();
    }
    void layoutControls() {
        if (controlsArea.isEmpty()) return;
        auto r = controlsArea.reduced(14, 8); r.removeFromTop(16);
        auto knobs = r.removeFromTop(74); const int kw = 66;
        auto place = [&](Knob& k, juce::Rectangle<int>& row, int w) { if (k.isVisible()) { k.setBounds(row.removeFromLeft(w)); row.removeFromLeft(2); } };
        place(kPitch, knobs, kw); place(kFormant, knobs, kw); place(kSpeed, knobs, kw); place(kBreath, knobs, kw); place(kGain, knobs, kw);
        knobs.removeFromLeft(10);
        place(kMix, knobs, kw); place(kDyn, knobs, kw); place(kBreathB, knobs, kw); place(kAmount, knobs, kw); place(kRetune, knobs, kw); place(kFlatten, knobs, kw); place(kHumanize, knobs, kw); place(kVAmount, knobs, kw); place(kVSize, knobs, kw);
        r.removeFromTop(5);
        auto row = r.removeFromTop(24);   // mode row
        if (P.mode == RenderParams::Borrow) { timeBox.setBounds(row.removeFromLeft(160)); row.removeFromLeft(6); carrierBox.setBounds(row.removeFromLeft(150)); row.removeFromLeft(6); consBtn.setBounds(row.removeFromLeft(108)); }
        if (P.mode == RenderParams::Contour) { traceBtn.setBounds(row.removeFromLeft(62)); row.removeFromLeft(4); clearCurveBtn.setBounds(row.removeFromLeft(56)); row.removeFromLeft(4); snapBtn.setBounds(row.removeFromLeft(54)); row.removeFromLeft(4);
                                               rootBox.setBounds(row.removeFromLeft(56)); row.removeFromLeft(4); scaleBox.setBounds(row.removeFromLeft(118)); row.removeFromLeft(4); forceBtn.setBounds(row.removeFromLeft(128)); }
        if (P.mode == RenderParams::Tune) { rootBox.setBounds(row.removeFromLeft(56)); row.removeFromLeft(4); scaleBox.setBounds(row.removeFromLeft(118)); }
        if (P.mode == RenderParams::VowelMorph) { for (auto& b : vowelBtn) { b.setBounds(row.removeFromLeft(30)); row.removeFromLeft(3); } row.removeFromLeft(5); sweepBtn.setBounds(row.removeFromLeft(86)); row.removeFromLeft(5);
                                                  vsetBox.setBounds(row.removeFromLeft(112)); row.removeFromLeft(5); autoSizeBtn.setBounds(row.removeFromLeft(84)); }
        r.removeFromTop(5);
        auto row2 = r.removeFromTop(24);
        keepBtn.setBounds(row2.removeFromLeft(118)); row2.removeFromLeft(4); normBtn.setBounds(row2.removeFromLeft(92)); row2.removeFromLeft(4); transBtn.setBounds(row2.removeFromLeft(96)); row2.removeFromLeft(10);
        doubleBox.setBounds(row2.removeFromLeft(110)); row2.removeFromLeft(4);
        r.removeFromTop(5);
        auto row3 = r.removeFromTop(24); const int hw = (row3.getWidth() - 8) / 3;
        for (int i = 0; i < 3; ++i) { harmBox[i].setBounds(row3.removeFromLeft(hw)); row3.removeFromLeft(4); }
        r.removeFromTop(3);
        auto knobs2 = r.removeFromTop(62); const int k2 = 62;
        place(kDetune, knobs2, k2); place(kSpread, knobs2, k2); place(kWidth, knobs2, k2); knobs2.removeFromLeft(12);
        for (auto& k : kHarmLvl) place(k, knobs2, k2);
    }
    juce::String hint() const {
        if (P.mode == RenderParams::Contour) return "Draw: left-drag" + DOT + "line: shift-drag" + DOT + "erase: right-drag" + DOT + "delete point: double-click" + DOT + "zoom: ctrl+wheel";
        if (P.mode == RenderParams::VowelMorph) return "Draw the vowel lane under the waveform" + DOT + "GRAB VOWEL captures one from A";
        if (P.mode == RenderParams::Borrow) return vocodeActive() ? juce::String("VOCODE: A's own audio (chords, detune) filtered by B's formants") : (B ? juce::String("RESYNTH: A takes on B's formants, B's rhythm by default") : juce::String("Drop a voice into slot B (left) to borrow its formants"));
        if (P.mode == RenderParams::Tune) return "TUNE corrects A's detected pitch to the key" + DOT + "FLATTEN 0 keeps vibrato";
        return "Drag on the ruler to select" + DOT + "click to hear from there" + DOT + "ctrl+wheel zooms";
    }
    void paintHelp(juce::Graphics& g) {
        auto r = controlsArea.reduced(14, 8); caption(g, juce::String(kModeNames[P.mode]) + DOT + kModeShort[P.mode], r.removeFromTop(16), P.mode == 0 ? PINK : P.mode == 3 ? GREEN : P.mode == 4 ? BLUE : AMBER);
        g.setColour(GREY); g.setFont(font(9.5f, false)); g.drawFittedText(hint(), controlsArea.reduced(14, 6).removeFromBottom(13), juce::Justification::centredLeft, 1, 0.9f);
        // the voices section caption
        g.setColour(GREY); g.setFont(font(8.5f)); if (!doubleBox.getBounds().isEmpty()) g.drawText(vocodeActive() ? "voices: RESYNTH only" : "", doubleBox.getRight() + 8, doubleBox.getY(), 140, doubleBox.getHeight(), juce::Justification::centredLeft);
    }
    void paintActions(juce::Graphics& g) {
        const bool has = wave.sel1 > wave.sel0 + 0.005; g.setColour(GREY); g.setFont(font(9.5f));
        const juce::String t = has ? "SEL " + timeText(wave.sel0) + " - " + timeText(wave.sel1) : juce::String("drag to select");
        g.drawFittedText(t, juce::Rectangle<int>(actionsArea.getX() + 246, actionsArea.getY(), 118, actionsArea.getHeight()), juce::Justification::centredLeft, 1, 0.8f);   // (the X sits right of it)
    }
};

// =====================================================================================================================
VoicePage::VoicePage(VoiceHost& h) : host(h) {
    impl = std::make_unique<Impl>(*this, h);
    setSize(W, H); setWantsKeyboardFocus(true); startTimerHz(30);
}
VoicePage::~VoicePage() { stopTimer(); impl.reset(); }

void VoicePage::setMode(int mode) { impl->setMode(juce::jlimit(0, 4, mode)); }
void VoicePage::setDark(bool dark) { impl->setDark(dark); }
void VoicePage::setLook(ghosty::themes::Theme t) { impl->setLook(t); }
bool VoicePage::isDark() const { return impl->dark; }
void VoicePage::inspectAt(double s) { impl->inspect(s); }
void VoicePage::refreshNow() { impl->pull(); }
void VoicePage::showAbout(bool show) { impl->about.setVisible(show); if (show) impl->about.toFront(false); }
juce::String VoicePage::aboutText() const { return impl->about.text.getText(); }
void VoicePage::setKnob(const juce::String& name, double value, bool fine) {
    for (auto* k : impl->allKnobs()) if (k->name == name) { k->testFine = fine; k->s.setValue(value, juce::sendNotificationSync); k->testFine = false; return; }
}
juce::String VoicePage::knobText(const juce::String& name) const { for (auto* k : impl->allKnobs()) if (k->name == name) return k->text(k->s.getValue()); return {}; }
juce::Range<double> VoicePage::visibleTime() const { return { impl->wave.vt0, impl->wave.vt1 }; }
void VoicePage::zoomTime(double f, double c) { impl->wave.zoom(f, c); }
void VoicePage::scrollTime(double s) { impl->wave.scroll(s); }
void VoicePage::selectForTest(double t0, double t1) { impl->wave.sel0 = t0; impl->wave.sel1 = t1; impl->selectionChanged(); }
juce::Rectangle<int> VoicePage::boundsOf(const juce::String& n) const {
    auto& I = *impl;
    if (n == "scale") return I.scaleBox.getBounds();
    if (n == "root") return I.rootBox.getBounds();
    if (n == "wave") return I.wave.getBounds();
    if (n == "controls") return I.controlsArea;
    return {};
}
bool VoicePage::hintFits() const { return juce::GlyphArrangement::getStringWidth(font(9.5f, false), impl->hint()) < impl->controlsArea.getWidth() - 28; }
juce::String VoicePage::formantLabel() const { return impl->formant.label(); }
void VoicePage::setOwnsTooltips(bool owns) { ownsTooltips = owns; if (impl) impl->updateTooltipWindow(); }
bool VoicePage::hasOwnTooltipWindow() const { return impl->tips != nullptr; }
void VoicePage::parentHierarchyChanged() { if (impl) impl->updateTooltipWindow(); }
void VoicePage::setThemeButtonVisible(bool v) { impl->themeBtn.setVisible(v); resized(); }
bool VoicePage::themeButtonVisible() const { return impl->themeBtn.isVisible(); }
juce::StringArray VoicePage::exportForTest(int which) {
    if (which == 0) return impl->dragOut.make();
    if (which == 1) return impl->dragSet.make();
    if (which == 3) return impl->startRenderSet(12) ? juce::StringArray { "started" } : juce::StringArray();   // RENDER SET to Shared Sounds
    return impl->startRenderSet(2) ? juce::StringArray { "started" } : juce::StringArray();
}
juce::Component& VoicePage::sourceWave() { return impl->srcWave; }
juce::String VoicePage::lastMessage() const { return juce::Time::getMillisecondCounter() < impl->flashUntil ? impl->flashText : juce::String(); }
double VoicePage::wavePaintMs(int paints) {
    auto& w = impl->wave; juce::Image img(juce::Image::ARGB, std::max(1, w.getWidth()), std::max(1, w.getHeight()), true);
    { juce::Graphics g(img); w.paintEntireComponent(g, false); }   // warm the cache
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    for (int i = 0; i < paints; ++i) { juce::Graphics g(img); w.paintEntireComponent(g, false); }
    return (juce::Time::getMillisecondCounterHiRes() - t0) / std::max(1, paints);
}

void VoicePage::resized() {
    auto& I = *impl; auto r = getLocalBounds();
    auto top = r.removeFromTop(48).reduced(16, 9);
    if (onLeave) { I.backBtn.setVisible(true); I.backBtn.setBounds(top.removeFromLeft(32)); top.removeFromLeft(8); }
    top.removeFromLeft(100);
    for (auto& b : I.modeBtn) { b.setBounds(top.removeFromLeft(88)); top.removeFromLeft(5); }
    I.infoBtn.setBounds(top.removeFromRight(30)); top.removeFromRight(8); if (I.themeBtn.isVisible()) { I.themeBtn.setBounds(top.removeFromRight(66)); top.removeFromRight(12); }
    I.redoBtn.setBounds(top.removeFromRight(60)); top.removeFromRight(5); I.undoBtn.setBounds(top.removeFromRight(60));
    r = r.reduced(16, 0).withTrimmedBottom(8);
    auto side = r.removeFromLeft(228); r.removeFromLeft(10);
    I.slotA.setBounds(side.removeFromTop(132)); side.removeFromTop(6); I.slotB.setBounds(side.removeFromTop(132)); side.removeFromTop(6);
    I.analysisArea = side.removeFromTop(108);
    { auto a = I.analysisArea.reduced(12, 7); a.removeFromTop(16); I.rangeBox.setBounds(a.removeFromTop(24)); a.removeFromTop(5); auto row = a.removeFromTop(24);
      I.fastBtn.setBounds(row.removeFromLeft(row.getWidth() / 2 - 3)); row.removeFromLeft(6); I.analyseBtn.setBounds(row); a.removeFromTop(5); I.voicing.setBounds(a.removeFromTop(20)); }
    side.removeFromTop(6); I.outputArea = side;
    { auto o = I.outputArea.reduced(12, 7); o.removeFromTop(16);
      auto row = o.removeFromTop(26); I.playBtn.setBounds(row.removeFromLeft(row.getWidth() - 60)); row.removeFromLeft(4); I.stopBtn.setBounds(row);
      o.removeFromTop(4); row = o.removeFromTop(24); int w3 = (row.getWidth() - 8) / 3; I.abBtn.setBounds(row.removeFromLeft(w3)); row.removeFromLeft(4); I.loopBtn.setBounds(row.removeFromLeft(w3)); row.removeFromLeft(4); I.followBtn.setBounds(row);
      o.removeFromTop(4); row = o.removeFromTop(24); I.renderBtn.setBounds(row.removeFromLeft(w3)); row.removeFromLeft(4); I.autoBtn.setBounds(row.removeFromLeft(w3)); row.removeFromLeft(4); I.cancelBtn.setBounds(row);
      o.removeFromTop(5); I.progressArea = o.removeFromTop(15); o.removeFromTop(3); I.staleArea = o.removeFromTop(15); o.removeFromTop(4);
      I.dragOut.setBounds(o.removeFromTop(34)); o.removeFromTop(5);
      row = o.removeFromTop(24); I.toABtn.setBounds(row.removeFromLeft(w3)); row.removeFromLeft(4); I.toBBtn.setBounds(row.removeFromLeft(w3)); row.removeFromLeft(4); I.setBtn.setBounds(row);
      o.removeFromTop(4); I.dragSet.setBounds(o.removeFromTop(28)); }
    auto bottom = r.removeFromBottom(272); r.removeFromBottom(4);
    I.srcWaveArea = r.removeFromTop(78); r.removeFromTop(5); I.srcWave.setBounds(I.srcWaveArea);
    I.actionsArea = r.removeFromBottom(26); r.removeFromBottom(4);
    I.wave.setBounds(r);
    { auto a = I.actionsArea; I.laneBox.setBounds(a.removeFromLeft(140)); a.removeFromLeft(4); I.clearLaneBtn.setBounds(a.removeFromLeft(96));
      auto gap = a.removeFromLeft(156); I.clearSelBtn.setBounds(gap.removeFromRight(30).withTrimmedRight(4));   // X: the right end of the gap, after the SEL text (it had no room at the row's end)
      for (auto* b : { &I.regionBtn, &I.zoomSelBtn, &I.octUpBtn, &I.octDnBtn, &I.voicedBtn, &I.unvoicedBtn, &I.resetFixBtn, &I.grabBtn }) {
          const int w = b == &I.regionBtn ? 124 : b == &I.grabBtn ? 88 : b == &I.unvoicedBtn ? 76 : b == &I.voicedBtn ? 62 : b == &I.resetFixBtn ? 52 : 44;
          b->setBounds(a.removeFromLeft(w)); a.removeFromLeft(3); } }
    I.formant.setBounds(bottom.removeFromRight(316)); bottom.removeFromRight(10);
    I.controlsArea = bottom;
    I.about.setBounds(getLocalBounds());
    I.layoutMode();
}

void VoicePage::paint(juce::Graphics& g) {
    auto& I = *impl; I.usePalette();
    g.fillAll(BG);
    if (darkTheme) { juce::ColourGradient bg(juce::Colour(0xff0d0628), 0, 0, juce::Colour(0xff04101a), 0, (float) getHeight(), false); g.setGradientFill(bg); g.fillAll(); }
    auto t = getLocalBounds().removeFromTop(48).reduced(16, 9); if (onLeave) t.removeFromLeft(40);
    g.setColour(INK); g.setFont(font(22.0f)); g.drawText("VOICE", t.removeFromLeft(80), juce::Justification::centredLeft);
    g.setColour(PURPLE); g.fillEllipse((float) t.getX() - 8, (float) t.getCentreY() - 3, 6, 6);
    card(g, I.analysisArea, CYAN); caption(g, "PITCH TRACKING", I.analysisArea.reduced(12, 7).removeFromTop(15), GREY);
    card(g, I.outputArea, GREEN); caption(g, "RESULT", I.outputArea.reduced(12, 7).removeFromTop(15), GREY);
    card(g, I.controlsArea, PURPLE); I.paintHelp(g);
    I.paintActions(g);
    // progress + time left + status
    const auto pa = I.progressArea.toFloat(); const int pr = host.progress.load(), job = host.job.load();
    g.setColour(CARD2); g.fillRoundedRectangle(pa, 4);
    juce::String st;
    if (job != 0 && pr >= 0) { g.setColour(PURPLE); g.fillRoundedRectangle(pa.withWidth(pa.getWidth() * pr / 100.0f), 4);
        static const char* J[] = { "", "analysing", "rendering", "render set", "decoding" }; st = juce::String(J[juce::jlimit(0, 4, job)]) + " " + juce::String(pr) + " %";
        const double left = host.secondsLeft(); if (left > 0) st << DOT << (left < 60 ? juce::String((int) std::ceil(left)) + " s left" : juce::String(left / 60.0, 1) + " min left"); }
    else if (job == 4) st = "decoding...";
    else if (I.R) st = "result " + timeText(I.R->seconds()) + DOT + juce::String(I.R->fs() / 1000.0, 1) + " kHz" + (I.R->audio.stereo() ? DOT + "stereo" : juce::String()) + (I.R->info.vocoded ? DOT + "vocoded" : juce::String());
    else st = I.A ? "not rendered yet" : "no sound";
    if (juce::Time::getMillisecondCounter() < I.flashUntil) st = I.flashText;
    else if (job == 0) { const auto s = host.status(); if (s.isNotEmpty() && juce::Time::getMillisecondCounter() - host.statusTime() < 12000) st = s; }
    g.setColour(job != 0 && pr > 45 ? (darkTheme ? INK : juce::Colours::white) : INK); g.setFont(font(9.5f)); g.drawFittedText(st, pa.reduced(6, 0).toNearestInt(), juce::Justification::centredLeft, 1, 0.75f);
    // OUT OF DATE badge
    if (I.stale && I.A) { auto s = I.staleArea.toFloat(); g.setColour(AMBER.withAlpha(0.18f)); g.fillRoundedRectangle(s, 4); g.setColour(AMBER); g.setFont(font(9.5f));
        g.drawText(job == 2 || host.autoRender.load() ? "OUT OF DATE - RENDERING..." : "OUT OF DATE - press RENDER (drag renders)", s.toNearestInt(), juce::Justification::centred); }
    if (I.dropSlot >= 0) { g.setColour((I.dropSlot == 0 ? CYAN : AMBER).withAlpha(0.06f)); g.fillAll(); }
}

void VoicePage::timerCallback() {
    auto& I = *impl;
    if (host.paramsVersion.load() != I.seenParams || host.sourceVersion.load() != I.seenSource || host.resultVersion.load() != I.seenResult) {
        const bool src = host.sourceVersion.load() != I.seenSource; const auto oldA = I.A; I.pull(); if (src && oldA != I.A) { I.wave.userRange = false; I.wave.fitAll(); I.wave.fitRange(); }
    }
    const bool st = host.resultStale(); if (st != I.stale) { I.stale = st; repaint(I.outputArea); }
    I.cancelBtn.setEnabled(host.job.load() != 0);
    I.dragOut.setEnabled(I.A != nullptr && (I.R != nullptr || I.aA != nullptr));
    I.dragSet.setEnabled(host.lastSetFolder().isDirectory());
    I.toABtn.setEnabled(I.R != nullptr); I.toBBtn.setEnabled(I.R != nullptr);
    I.playBtn.setButtonText(host.playing.load() == PlayResult ? "STOP RESULT" : "PLAY RESULT");
    I.undoBtn.setEnabled(host.canUndo()); I.redoBtn.setEnabled(host.canRedo());
    const double pp = host.playPos.load(); const bool moving = pp >= 0 || host.job.load() != 0;
    if (I.follow && pp >= 0 && host.playing.load() != PlayB) {
        const double ts = host.playing.load() == PlayResult && I.R ? I.R->info.outputToSource(pp) : pp; auto& w = I.wave;
        if (ts > w.vt1 - 0.05 * (w.vt1 - w.vt0) || ts < w.vt0) w.scroll(ts - w.vt0 - 0.1 * (w.vt1 - w.vt0));
    }
    if (moving || I.wasMoving) { I.wave.repaint(); I.slotA.repaint(); I.slotB.repaint(); repaint(I.outputArea); }
    I.wasMoving = moving;
    if (juce::Time::getMillisecondCounter() < I.flashUntil + 100) repaint(I.outputArea);
}

bool VoicePage::keyPressed(const juce::KeyPress& k) {
    auto& I = *impl; const auto mods = k.getModifiers();
    if (mods.isCommandDown() && k.getKeyCode() == 'Z') { if (mods.isShiftDown()) host.redo(); else host.undo(); I.pull(); return true; }
    if (mods.isCommandDown() && k.getKeyCode() == 'Y') { host.redo(); I.pull(); return true; }
    if (k == juce::KeyPress::spaceKey) { if (host.playing.load() != PlayNone) host.stop(); else host.play(I.R ? PlayResult : PlayA); return true; }
    if (k == juce::KeyPress::tabKey) { host.toggleAB(); return true; }
    if (k == juce::KeyPress::escapeKey) { if (I.about.isVisible()) I.about.setVisible(false); else I.clearSelection(); return true; }
    if (!mods.isAnyModifierKeyDown()) {
        const auto c = k.getTextCharacter();
        if (c >= '1' && c <= '5') { setMode(c - '1'); return true; }
        if (c == 'l' || c == 'L') { I.srcWave.toggleLoop(); return true; }
        if (c == 'z' || c == 'Z') { I.zoomToSelection(); return true; }
    }
    return false;
}

bool VoicePage::isInterestedInFileDrag(const juce::StringArray& files) {
    for (auto& f : files) for (auto& e : VoiceHost::audioExtensions()) if (f.endsWithIgnoreCase(e)) return true;
    return false;
}
void VoicePage::fileDragEnter(const juce::StringArray& f, int x, int y) { fileDragMove(f, x, y); }
void VoicePage::fileDragMove(const juce::StringArray&, int x, int y) {
    const int s = impl->slotB.getBounds().contains(x, y) ? 1 : 0; if (s != impl->dropSlot) { impl->dropSlot = s; repaint(); impl->slotA.repaint(); impl->slotB.repaint(); }
}
void VoicePage::fileDragExit(const juce::StringArray&) { impl->dropSlot = -1; repaint(); impl->slotA.repaint(); impl->slotB.repaint(); }
void VoicePage::filesDropped(const juce::StringArray& files, int x, int y) {
    const int s = impl->slotB.getBounds().contains(x, y) ? 1 : 0; impl->dropSlot = -1;
    juce::StringArray audio; for (auto& f : files) if (isInterestedInFileDrag({ f })) audio.add(f);
    if (audio.size() >= 2 && s == 0) { host.loadFileAsync(0, juce::File(audio[0])); host.loadFileAsync(1, juce::File(audio[1])); impl->flash("Loading A + B"); }
    else if (!audio.isEmpty()) { host.loadFileAsync(s, juce::File(audio[0])); impl->flash("Loading " + juce::File(audio[0]).getFileName()); }
    repaint(); impl->slotA.repaint(); impl->slotB.repaint();
}
