#include "BeatsXEditor.h"

namespace {
const juce::Colour BG { 0xff05040a }, CARD { 0xff0b0a13 }, CARD2 { 0xff110f1c }, INK { 0xfff5f3ff }, GREY { 0xff9ca3af }, DIM { 0xff4b5563 },
                   PURPLE { 0xffa855f7 }, VIOLET { 0xff8b5cf6 }, GREEN { 0xff00ff88 }, PINK { 0xfff472b6 }, AMBER { 0xfffbbf24 }, BLUE { 0xff60a5fa };
constexpr int W = 1180, H = 790;
juce::Colour trackColour(int t) { static const juce::Colour C[] = { GREEN, PURPLE, PINK, AMBER, BLUE, juce::Colour(0xff22d3ee), juce::Colour(0xfff87171), juce::Colour(0xffa3e635) }; return C[(size_t) (t % 8)]; }
juce::Colour patColour(int p) { return trackColour(p * 3 + 1); }
juce::String patName(int p) { return juce::String::charToString((juce::juce_wchar) ('A' + p)); }

struct LnF : juce::LookAndFeel_V4 {
    LnF() {
        setColour(juce::ComboBox::backgroundColourId, CARD2); setColour(juce::ComboBox::textColourId, INK); setColour(juce::ComboBox::arrowColourId, PURPLE);
        setColour(juce::PopupMenu::backgroundColourId, CARD2); setColour(juce::PopupMenu::textColourId, INK); setColour(juce::PopupMenu::highlightedBackgroundColourId, PURPLE.withAlpha(0.35f));
        setColour(juce::TextButton::textColourOffId, INK); setColour(juce::TextButton::textColourOnId, BG); setColour(juce::Label::textColourId, INK);
        setColour(juce::TextEditor::backgroundColourId, CARD2); setColour(juce::TextEditor::textColourId, INK); setColour(juce::TextEditor::outlineColourId, PURPLE.withAlpha(0.4f));
    }
    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override {
        auto r = b.getLocalBounds().toFloat().reduced(1); const bool on = b.getToggleState() || (bool) b.getProperties()["accent"];
        const juce::Colour tint = b.getProperties().contains("tint") ? juce::Colour((juce::uint32) (juce::int64) b.getProperties()["tint"]) : PURPLE;
        g.setColour(on ? tint.withAlpha(0.85f) : CARD2); g.fillRoundedRectangle(r, 7);
        g.setColour((on ? tint.brighter(0.3f) : tint).withAlpha(over ? 0.95f : 0.45f)); g.drawRoundedRectangle(r, 7, down ? 2.0f : 1.1f);
    }
    juce::Font getTextButtonFont(juce::TextButton&, int h) override { return juce::FontOptions(juce::jmin(13.0f, h * 0.48f), juce::Font::bold); }
    void drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&) override {
        auto r = juce::Rectangle<float>(0, 0, (float) w, (float) h).reduced(1); g.setColour(CARD2); g.fillRoundedRectangle(r, 7);
        g.setColour(PURPLE.withAlpha(0.5f)); g.drawRoundedRectangle(r, 7, 1.1f);
        juce::Path a; a.addTriangle((float) w - 16, h * 0.5f - 2, (float) w - 8, h * 0.5f - 2, (float) w - 12, h * 0.5f + 3); g.setColour(PURPLE); g.fillPath(a);
    }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return juce::FontOptions(12.5f, juce::Font::bold); }
    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override {
        auto r = juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h).reduced(3); const float sz = juce::jmin(r.getWidth(), r.getHeight());
        auto c = r.withSizeKeepingCentre(sz, sz); const float th = 4, ang = a0 + pos * (a1 - a0);
        g.setColour(CARD2); g.fillEllipse(c.reduced(th + 1));
        juce::Path tr; tr.addCentredArc(c.getCentreX(), c.getCentreY(), sz / 2 - th / 2, sz / 2 - th / 2, 0, a0, a1, true);
        g.setColour(juce::Colours::white.withAlpha(0.08f)); g.strokePath(tr, juce::PathStrokeType(th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        juce::Path ar; ar.addCentredArc(c.getCentreX(), c.getCentreY(), sz / 2 - th / 2, sz / 2 - th / 2, 0, a0, ang, true);
        g.setGradientFill(juce::ColourGradient(PURPLE, c.getX(), c.getBottom(), GREEN, c.getRight(), c.getY(), false));
        g.strokePath(ar, juce::PathStrokeType(th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(INK); g.drawLine({ c.getCentre().getPointOnCircumference(sz * 0.12f, ang), c.getCentre().getPointOnCircumference(sz / 2 - th - 4, ang) }, 2.0f);
    }
};

// knob + label; either bound to an APVTS param, or to a getter/setter on the model
struct Knob : juce::Component {
    juce::Slider s { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox }; juce::Label l;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    explicit Knob(const juce::String& name) {
        s.setPopupDisplayEnabled(true, true, nullptr); addAndMakeVisible(s);
        l.setText(name, juce::dontSendNotification); l.setJustificationType(juce::Justification::centred); l.setFont(juce::FontOptions(10.5f, juce::Font::bold));
        l.setColour(juce::Label::textColourId, GREY); l.setInterceptsMouseClicks(false, false); addAndMakeVisible(l);
    }
    void resized() override { auto r = getLocalBounds(); l.setBounds(r.removeFromBottom(14)); s.setBounds(r); }
};
} // namespace

// =====================================================================================================
// SEQUENCER GRID: tracks are rows, time runs left -> right
struct StepGrid : juce::Component {
    BeatsXProcessor& proc; std::function<void(int)> onSelectTrack; std::function<void()> onEdit;
    int selected = 0, page = 0, mode = 0;   // mode: 0 STEP, 1 VELOCITY, 2 ROLL, 3 CHANCE, 4 NUDGE
    bool painting = false; uint8_t paintVel = 0; int dragT = -1, dragS = -1; float dragY = 0; gb::Step dragOrig;
    explicit StepGrid(BeatsXProcessor& p) : proc(p) {}
    int patIdx() const { return (int) proc.apvts.getRawParameterValue("pattern")->load() - 1; }
    gb::Pattern& pat() { return proc.model.patterns[(size_t) juce::jlimit(0, gb::kPatterns - 1, patIdx())]; }
    float rowH() const { return getHeight() / (float) gb::kTracks; }
    float colW() const { return getWidth() / 16.0f; }
    void paint(juce::Graphics& g) override {
        auto& P = pat(); const int now = proc.engine.uiStep.load(), nowPat = proc.engine.uiPattern.load();
        const float rh = rowH(), cw = colW();
        for (int t = 0; t < gb::kTracks; ++t) {
            if (t == selected) { g.setColour(PURPLE.withAlpha(0.08f)); g.fillRect(0.0f, t * rh, (float) getWidth(), rh); }
            for (int c = 0; c < 16; ++c) {
                const int s = page * 16 + c; if (s >= P.length) continue;
                auto r = juce::Rectangle<float>(c * cw + 2, t * rh + 2, cw - 4, rh - 4);
                const bool beat = (s / 4) % 2 == 0;
                g.setColour(juce::Colours::white.withAlpha(beat ? 0.07f : 0.04f)); g.fillRoundedRectangle(r, 4);
                const auto& st = P.steps[(size_t) t][(size_t) s];
                if (st.vel) {
                    const auto col = trackColour(t); const float v = st.vel / 127.0f;
                    g.setColour(col.withAlpha(0.35f + 0.6f * v)); g.fillRoundedRectangle(r.reduced(1), 4);
                    if (mode == 1) { g.setColour(BG.withAlpha(0.55f)); g.fillRoundedRectangle(r.withTrimmedBottom(r.getHeight() * v).reduced(1), 3); }
                    if (st.roll > 1) { g.setColour(BG); const float dw = juce::jmin(5.0f, (r.getWidth() - 6) / st.roll);   // roll dots
                        for (int k = 0; k < st.roll; ++k) g.fillEllipse(r.getX() + 3 + k * (r.getWidth() - 6) / st.roll, r.getBottom() - dw - 2, dw, dw); }
                    if (st.prob < 100) { g.setColour(INK.withAlpha(0.9f)); g.setFont(juce::FontOptions(9.5f, juce::Font::bold)); g.drawText(juce::String(st.prob) + "%", r, juce::Justification::centredTop); }
                    if (st.nudge != 0) { g.setColour(BG); const float nx = r.getCentreX() + st.nudge / 100.0f * r.getWidth(); g.fillRect(nx - 1, r.getY() + 2, 2.0f, r.getHeight() - 4); }
                } else if (mode == 0 && t == selected) { g.setColour(PURPLE.withAlpha(0.12f)); g.drawRoundedRectangle(r, 4, 1); }
                if (s == now && nowPat == patIdx()) { g.setColour(INK.withAlpha(0.5f)); g.drawRoundedRectangle(r, 4, 1.6f); }
            }
        }
        for (int c = 0; c <= 16; c += 4) { g.setColour(juce::Colours::white.withAlpha(0.15f)); g.drawVerticalLine((int) (c * cw), 0, (float) getHeight()); }
    }
    bool cellAt(juce::Point<float> p, int& t, int& s) { t = (int) (p.y / rowH()); const int c = (int) (p.x / colW()); s = page * 16 + c; return t >= 0 && t < gb::kTracks && c >= 0 && c < 16 && s < pat().length; }
    void mouseDown(const juce::MouseEvent& e) override {
        int t, s; if (!cellAt(e.position, t, s)) return;
        if (t != selected) { selected = t; if (onSelectTrack) onSelectTrack(t); }
        auto& st = pat().steps[(size_t) t][(size_t) s];
        dragT = t; dragS = s; dragY = e.position.y; dragOrig = st;
        if (mode == 0 || (!st.vel && mode != 0)) {
            painting = true; paintVel = st.vel ? 0 : 105;
            if (e.mods.isShiftDown() && !st.vel) paintVel = 127;   // shift = accent
            st.vel = paintVel; if (!paintVel) { st.roll = 1; st.prob = 100; st.nudge = 0; }
            if (paintVel) proc.auditionTrack(t, paintVel / 127.0f);
            edited();
        } else if (mode == 2 && st.vel) {   // ROLL: click cycles 1 -> 2 -> 3 -> 4 -> 6 -> 8
            static const int R[] = { 1, 2, 3, 4, 6, 8 }; int i = 0; while (i < 5 && R[i] != st.roll) ++i; st.roll = (uint8_t) R[(i + 1) % 6]; edited();
        }
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (painting) { int t, s; if (cellAt(e.position, t, s) && t == dragT) { auto& st = pat().steps[(size_t) t][(size_t) s]; if (st.vel != paintVel) { st.vel = paintVel; if (!paintVel) { st.roll = 1; st.prob = 100; st.nudge = 0; } edited(); } } return; }
        if (dragT < 0) return;
        auto& st = pat().steps[(size_t) dragT][(size_t) dragS]; const float dy = (dragY - e.position.y);
        if (mode == 1) st.vel = (uint8_t) juce::jlimit(1, 127, dragOrig.vel + (int) (dy * 1.2f));
        else if (mode == 3) st.prob = (uint8_t) juce::jlimit(5, 100, dragOrig.prob + (int) (dy * 0.8f));
        else if (mode == 4) st.nudge = (int8_t) juce::jlimit(-50, 50, dragOrig.nudge + (int) ((e.position.x - e.mouseDownPosition.x) * 1.0f));
        edited();
    }
    void mouseUp(const juce::MouseEvent&) override { painting = false; dragT = -1; }
    void edited() { repaint(); proc.publish(); if (onEdit) onEdit(); }
};

// the track names on the left (click = select + hear it; drop audio on a row = that track uses it)
struct TrackList : juce::Component {
    BeatsXProcessor& proc; std::function<void(int)> onSelect; int selected = 0;
    explicit TrackList(BeatsXProcessor& p) : proc(p) {}
    float rowH() const { return getHeight() / (float) gb::kTracks; }
    void paint(juce::Graphics& g) override {
        const float rh = rowH();
        for (int t = 0; t < gb::kTracks; ++t) {
            const auto& tr = proc.model.tracks[(size_t) t];
            auto r = juce::Rectangle<float>(0, t * rh + 2, (float) getWidth(), rh - 4);
            const uint32_t tc = proc.engine.trig[t].load(); const bool flash = tc != last[t]; last[t] = tc; if (flash) glow[t] = 1.0f; glow[t] *= 0.82f;
            g.setColour(t == selected ? PURPLE.withAlpha(0.25f) : CARD2); g.fillRoundedRectangle(r, 5);
            g.setColour(trackColour(t).withAlpha(0.35f + 0.65f * glow[t])); g.fillRoundedRectangle(r.withWidth(5), 2);
            g.setColour(tr.mute ? DIM : INK); g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
            g.drawText(juce::String(t + 1) + "  " + tr.sound + (tr.sample ? "  *" : ""), r.withTrimmedLeft(10).withTrimmedRight(46), juce::Justification::centredLeft);
            auto m = juce::Rectangle<float>(r.getRight() - 42, r.getY() + 2, 19, r.getHeight() - 4), so = m.translated(21, 0);
            g.setColour(tr.mute ? juce::Colour(0xfff87171) : DIM.withAlpha(0.5f)); g.fillRoundedRectangle(m, 4);
            g.setColour(tr.solo ? AMBER : DIM.withAlpha(0.5f)); g.fillRoundedRectangle(so, 4);
            g.setColour(BG); g.setFont(juce::FontOptions(10.0f, juce::Font::bold)); g.drawText("M", m, juce::Justification::centred); g.drawText("S", so, juce::Justification::centred);
        }
    }
    void mouseDown(const juce::MouseEvent& e) override {
        const int t = (int) (e.position.y / rowH()); if (!juce::isPositiveAndBelow(t, gb::kTracks)) return;
        auto& tr = proc.model.tracks[(size_t) t];
        if (e.position.x > getWidth() - 44 && e.position.x < getWidth() - 23) { tr.mute = !tr.mute; proc.publish(); repaint(); return; }
        if (e.position.x >= getWidth() - 23) { tr.solo = !tr.solo; proc.publish(); repaint(); return; }
        selected = t; proc.auditionTrack(t); if (onSelect) onSelect(t); repaint();
    }
    uint32_t last[gb::kTracks] {}; float glow[gb::kTracks] {};
};

// MPC pads
struct Pads : juce::Component {
    BeatsXProcessor& proc; uint32_t last[16] {}; float glow[16] {}; int downPad = -1;
    explicit Pads(BeatsXProcessor& p) : proc(p) {}
    juce::Rectangle<float> padRect(int i) const { const int col = i % 4, row = 3 - i / 4; const float w = getWidth() / 4.0f, h = getHeight() / 4.0f; return { col * w + 6, row * h + 6, w - 12, h - 12 }; }
    void paint(juce::Graphics& g) override {
        for (int i = 0; i < 16; ++i) {
            const uint32_t tc = proc.engine.trig[i].load(); if (tc != last[i]) { glow[i] = 1; last[i] = tc; } glow[i] *= 0.85f;
            auto r = padRect(i); const auto col = trackColour(i);
            g.setColour(CARD2); g.fillRoundedRectangle(r, 12);
            g.setColour(col.withAlpha(0.18f + 0.7f * glow[i])); g.fillRoundedRectangle(r.reduced(3), 10);
            g.setColour(col.withAlpha(0.8f)); g.drawRoundedRectangle(r, 12, 1.6f);
            g.setColour(INK); g.setFont(juce::FontOptions(14.0f, juce::Font::bold)); g.drawText(proc.model.tracks[(size_t) i].sound, r.reduced(8), juce::Justification::centred);
            g.setColour(GREY); g.setFont(juce::FontOptions(10.5f, juce::Font::bold)); g.drawText("PAD " + juce::String(i + 1), r.reduced(10, 8), juce::Justification::topLeft);
        }
    }
    int padAt(juce::Point<float> p) const { for (int i = 0; i < 16; ++i) if (padRect(i).contains(p)) return i; return -1; }
    void mouseDown(const juce::MouseEvent& e) override {
        downPad = padAt(e.position); if (downPad < 0) return;
        const float vel = juce::jlimit(0.35f, 1.0f, 1.0f - (e.position.y - padRect(downPad).getY()) / padRect(downPad).getHeight() * 0.65f);   // higher on the pad = harder
        proc.engine.pushPad(downPad, vel); proc.engine.heldPads.fetch_or(1u << downPad);
    }
    void mouseUp(const juce::MouseEvent&) override { if (downPad >= 0) proc.engine.heldPads.fetch_and(~(1u << downPad)); downPad = -1; }
};

// SONG arranger: drag a pattern chip onto the bars, slide blocks, drag the right edge to repeat
struct SongLane : juce::Component, juce::DragAndDropTarget {
    BeatsXProcessor& proc; int selected = -1, drag = 0; gb::SongBlock orig; float downX = 0; std::function<void()> onEdit;
    static constexpr int kBars = 32;
    explicit SongLane(BeatsXProcessor& p) : proc(p) {}
    float barW() const { return getWidth() / (float) kBars; }
    void paint(juce::Graphics& g) override {
        auto R = getLocalBounds().toFloat(); g.setColour(CARD2); g.fillRoundedRectangle(R, 10);
        for (int b = 0; b <= kBars; ++b) { g.setColour(juce::Colours::white.withAlpha(b % 4 == 0 ? 0.2f : 0.07f)); g.drawVerticalLine((int) (b * barW()), 0, R.getBottom());
            if (b % 4 == 0 && b < kBars) { g.setColour(GREY); g.setFont(juce::FontOptions(10.0f, juce::Font::bold)); g.drawText(juce::String(b + 1), (int) (b * barW()) + 3, 2, 30, 12, juce::Justification::left); } }
        auto& song = proc.model.song;
        for (int i = 0; i < (int) song.size(); ++i) {
            const auto& b = song[(size_t) i];
            auto r = juce::Rectangle<float>(b.startBar * barW() + 2, 18, b.bars * barW() - 4, R.getHeight() - 26);
            const auto col = patColour(b.pattern);
            g.setColour(col.withAlpha(0.35f)); g.fillRoundedRectangle(r, 7); g.setColour(i == selected ? INK : col); g.drawRoundedRectangle(r, 7, i == selected ? 2.2f : 1.2f);
            const int plen = proc.model.patterns[(size_t) b.pattern].length / 16;
            for (int k = plen; k < b.bars; k += juce::jmax(1, plen)) { g.setColour(col.withAlpha(0.5f)); g.drawVerticalLine((int) (r.getX() + k * barW()), r.getY() + 6, r.getBottom() - 6); }
            g.setColour(INK); g.setFont(juce::FontOptions(18.0f, juce::Font::bold)); g.drawText(patName(b.pattern), r.reduced(8, 4), juce::Justification::centredLeft);
        }
        if (proc.apvts.getRawParameterValue("songmode")->load() > 0.5f) { const int bar = proc.engine.uiBar.load(); g.setColour(GREEN); g.fillRect(bar * barW(), 14.0f, 2.0f, R.getHeight() - 16); }
        if (song.empty()) { g.setColour(GREY); g.setFont(juce::FontOptions(13.5f)); g.drawFittedText("Drag a pattern letter from the left onto the bars. Slide blocks to rearrange, drag a block's right edge to repeat it.", getLocalBounds().reduced(20), juce::Justification::centred, 2); }
    }
    int blockAt(float x) const { const int bar = (int) (x / barW()); auto& s = proc.model.song; for (int i = (int) s.size() - 1; i >= 0; --i) if (bar >= s[(size_t) i].startBar && bar < s[(size_t) i].startBar + s[(size_t) i].bars) return i; return -1; }
    void mouseDown(const juce::MouseEvent& e) override {
        selected = blockAt(e.position.x); downX = e.position.x; drag = 0;
        if (selected >= 0) { orig = proc.model.song[(size_t) selected]; const float right = (orig.startBar + orig.bars) * barW(); drag = right - e.position.x < 12 ? 2 : 1; }
        repaint();
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (selected < 0 || drag == 0) return;
        auto& b = proc.model.song[(size_t) selected]; const int d = (int) std::round((e.position.x - downX) / barW());
        if (drag == 1) b.startBar = juce::jlimit(0, kBars - 1, orig.startBar + d); else b.bars = juce::jlimit(1, kBars, orig.bars + d);
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override { if (drag) { proc.publish(); if (onEdit) onEdit(); } drag = 0; }
    bool isInterestedInDragSource(const SourceDetails& d) override { return d.description.toString().startsWith("pattern:"); }
    void itemDropped(const SourceDetails& d) override {
        const int p = d.description.toString().fromFirstOccurrenceOf(":", false, false).getIntValue();
        const int bar = juce::jlimit(0, kBars - 1, (int) (d.localPosition.x / barW()));
        proc.model.song.push_back({ p, bar, juce::jmax(1, proc.model.patterns[(size_t) p].length / 16) });
        selected = (int) proc.model.song.size() - 1; proc.publish(); repaint(); if (onEdit) onEdit();
    }
};

struct PatternChip : juce::Component {
    int p; explicit PatternChip(int i) : p(i) {}
    void paint(juce::Graphics& g) override { auto r = getLocalBounds().toFloat().reduced(2); g.setColour(patColour(p).withAlpha(0.4f)); g.fillRoundedRectangle(r, 7); g.setColour(patColour(p)); g.drawRoundedRectangle(r, 7, 1.4f);
        g.setColour(INK); g.setFont(juce::FontOptions(16.0f, juce::Font::bold)); g.drawText(patName(p), r, juce::Justification::centred); }
    void mouseDrag(const juce::MouseEvent& e) override { if (e.getDistanceFromDragStart() > 6) if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor(this)) c->startDragging("pattern:" + juce::String(p), this); }
};

// drag a freshly rendered file out to the DAW
struct DragOut : juce::Component {
    juce::String text; std::function<juce::File()> make; std::function<juce::StringArray()> makeMany; bool started = false;
    DragOut(const juce::String& t, std::function<juce::File()> f) : text(t), make(std::move(f)) {}
    DragOut(const juce::String& t, std::function<juce::StringArray()> f, int) : text(t), makeMany(std::move(f)) {}
    void paint(juce::Graphics& g) override { auto r = getLocalBounds().toFloat().reduced(1); g.setColour(GREEN.withAlpha(0.15f)); g.fillRoundedRectangle(r, 7); g.setColour(GREEN.withAlpha(0.85f)); g.drawRoundedRectangle(r, 7, 1.3f);
        g.setFont(juce::FontOptions(11.5f, juce::Font::bold)); g.drawText(text, r, juce::Justification::centred); }
    void mouseDown(const juce::MouseEvent&) override { started = false; }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (started || e.getDistanceFromDragStart() < 6) return; started = true;
        if (makeMany) { auto files = makeMany(); if (!files.isEmpty()) juce::DragAndDropContainer::performExternalDragDropOfFiles(files, false, this); return; }
        auto f = make(); if (f.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles({ f.getFullPathName() }, false, this);
    }
};

// hold-to-use performance button
struct HoldButton : juce::TextButton {
    std::function<void(bool)> onHold; using juce::TextButton::TextButton;
    void mouseDown(const juce::MouseEvent& e) override { juce::TextButton::mouseDown(e); setToggleState(true, juce::dontSendNotification); if (onHold) onHold(true); }
    void mouseUp(const juce::MouseEvent& e) override { juce::TextButton::mouseUp(e); setToggleState(false, juce::dontSendNotification); if (onHold) onHold(false); }
};

// =====================================================================================================
struct BeatsXEditor::Content : juce::Component {
    BeatsXProcessor& proc; BeatsXEditor& ed;
    int page = 0, copiedFrom = -1;
    juce::OwnedArray<juce::TextButton> pageBtns, patBtns, modeBtns, gridPageBtns;
    juce::ComboBox kitBox, lenBox, catBox, soundBox, chokeBox, repeatDiv;
    juce::TextButton playBtn { "PLAY" }, copyBtn { "COPY" }, pasteBtn { "PASTE" }, clearBtn { "CLEAR" }, saveBtn { "SAVE" }, loadBtn { "LOAD" };
    juce::TextButton prevSnd { "<" }, nextSnd { ">" }, loadSmp { "LOAD SAMPLE" }, revBtn { "REVERSE" }, clrRow { "CLEAR ROW" }, f4 { "1/4" }, f8 { "1/8" }, f16 { "1/16" }, shL { "<<" }, shR { ">>" }, rnd { "RANDOM" };
    juce::TextButton saveBeatBtn { "SAVE BEAT" }, loadBeatBtn { "LOAD BEAT" };
    juce::TextButton recBtn { "REC" }, repeatBtn { "NOTE REPEAT" }, songModeBtn { "SONG MODE" }, delBlock { "DELETE BLOCK" }, dupBlock { "DUPLICATE" }, clrSong { "CLEAR SONG" };
    juce::OwnedArray<HoldButton> perf;
    juce::OwnedArray<Knob> masterKnobs, trackKnobs;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> btnAtts;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> repeatAtt;
    StepGrid grid; TrackList tracks; Pads pads; SongLane lane;
    juce::OwnedArray<PatternChip> chips;
    DragOut wavOut, midiOut, stemsOut;
    juce::Label trackTitle, status, padHelp, songHelp;
    juce::Slider bpm { juce::Slider::LinearBar, juce::Slider::TextBoxRight };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bpmAtt;
    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<juce::Component*> pageComps[3];

    Content(BeatsXProcessor& p, BeatsXEditor& e) : proc(p), ed(e), grid(p), tracks(p), pads(p), lane(p),
        wavOut("DRAG WAV OUT", [this] { const bool song = proc.apvts.getRawParameterValue("songmode")->load() > 0.5f && !proc.model.song.empty();
                                        return proc.bounce(song, song ? proc.songBars() : juce::jmax(1, grid.pat().length / 16)); }),
        midiOut("DRAG MIDI OUT", [this] { return proc.exportMidi(proc.apvts.getRawParameterValue("songmode")->load() > 0.5f); }),
        stemsOut("DRAG STEMS OUT", [this] { const bool song = proc.apvts.getRawParameterValue("songmode")->load() > 0.5f && !proc.model.song.empty();
                                            return proc.bounceStems(song, song ? proc.songBars() : juce::jmax(1, grid.pat().length / 16)); }, 0) {
        const char* pages[] = { "SEQUENCER", "PADS", "SONG" };
        for (int i = 0; i < 3; ++i) { auto* b = pageBtns.add(new juce::TextButton(pages[i])); b->onClick = [this, i] { setPage(i); }; addAndMakeVisible(b); }
        for (int i = 0; i < gb::kPatterns; ++i) {
            auto* b = patBtns.add(new juce::TextButton(patName(i))); b->getProperties().set("tint", (juce::int64) patColour(i).getARGB());
            b->onClick = [this, i] { if (auto* pp = proc.apvts.getParameter("pattern")) pp->setValueNotifyingHost(pp->convertTo0to1((float) (i + 1))); refreshPattern(); };
            addAndMakeVisible(b);
        }
        for (auto& k : gb::kits()) kitBox.addItem(juce::String(k.name) + " kit", kitBox.getNumItems() + 1);
        kitBox.setSelectedItemIndex(proc.currentKit, juce::dontSendNotification);
        kitBox.onChange = [this] { proc.applyKit(kitBox.getSelectedItemIndex(), true); refreshAll(); say("Loaded the " + kitBox.getText() + ": patterns A and B are a starter beat."); };
        addAndMakeVisible(kitBox);
        playBtn.setClickingTogglesState(true); playBtn.onClick = [this] { proc.engine.internalPlay = playBtn.getToggleState(); };
        addAndMakeVisible(playBtn);
        bpm.setRange(60, 200, 1); bpmAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, "bpm", bpm);
        bpm.setNumDecimalPlacesToDisplay(0); bpm.setColour(juce::Slider::trackColourId, PURPLE.withAlpha(0.5f)); bpm.setColour(juce::Slider::textBoxTextColourId, INK); bpm.textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v)) + " bpm"; };   // whole BPM (the attachment would show 7 decimals)
        bpm.valueFromTextFunction = [](const juce::String& t) { return t.retainCharacters("0123456789.").getDoubleValue(); }; bpm.updateText(); addAndMakeVisible(bpm);
        lenBox.addItemList({ "16 steps", "32 steps", "64 steps" }, 1);
        lenBox.onChange = [this] { static const int L[] = { 16, 32, 64 }; grid.pat().length = L[juce::jmax(0, lenBox.getSelectedItemIndex())]; grid.page = 0; proc.publish(); refreshPattern(); };
        copyBtn.onClick = [this] { copiedFrom = grid.patIdx(); say("Copied pattern " + patName(copiedFrom) + ". Pick another letter and PASTE."); };
        pasteBtn.onClick = [this] { if (copiedFrom < 0) return; proc.model.patterns[(size_t) grid.patIdx()] = proc.model.patterns[(size_t) copiedFrom]; proc.publish(); refreshPattern(); say("Pasted into " + patName(grid.patIdx()) + "."); };
        clearBtn.onClick = [this] { grid.pat().clear(); proc.publish(); refreshPattern(); };
        saveBtn.onClick = [this] { const auto name = kitBox.getText().upToFirstOccurrenceOf(" ", false, false) + " " + patName(grid.patIdx()) + " " + juce::Time::getCurrentTime().formatted("%m-%d %H%M");
                                   say(proc.savePattern(grid.patIdx(), name) ? "Saved \"" + name + "\" to Documents/Ghosty/Patterns." : "Couldn't save the pattern."); };
        loadBtn.onClick = [this] {
            juce::PopupMenu m; auto files = proc.userDir().findChildFiles(juce::File::findFiles, false, "*.ghostpat"); files.sort();
            for (int i = 0; i < files.size(); ++i) m.addItem(i + 1, files[i].getFileNameWithoutExtension());
            if (files.isEmpty()) m.addItem(-1, "No saved patterns yet (use SAVE)", false);
            m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&loadBtn), [this, files](int r) { if (r > 0 && proc.loadPattern(files[r - 1], grid.patIdx())) { refreshPattern(); say("Loaded into " + patName(grid.patIdx()) + "."); } });
        };
        for (auto* c : { &lenBox }) addAndMakeVisible(c);
        for (auto* b : { &copyBtn, &pasteBtn, &clearBtn, &saveBtn, &loadBtn }) addAndMakeVisible(b);

        // ---- SEQUENCER page
        addAndMakeVisible(grid); addAndMakeVisible(tracks); pageComps[0] = { &grid, &tracks };
        grid.onSelectTrack = [this](int t) { selectTrack(t); };
        tracks.onSelect = [this](int t) { selectTrack(t); };
        const char* modes[] = { "STEP", "VELOCITY", "ROLL / TRIPLET", "CHANCE", "NUDGE" };
        for (int i = 0; i < 5; ++i) { auto* b = modeBtns.add(new juce::TextButton(modes[i])); b->onClick = [this, i] { grid.mode = i; refreshModes(); grid.repaint(); }; addAndMakeVisible(b); pageComps[0].push_back(b); }
        for (int i = 0; i < 4; ++i) { auto* b = gridPageBtns.add(new juce::TextButton(juce::String(i * 16 + 1) + "-" + juce::String(i * 16 + 16))); b->onClick = [this, i] { grid.page = i; refreshPattern(); }; addAndMakeVisible(b); pageComps[0].push_back(b); }
        trackTitle.setFont(juce::FontOptions(16.0f, juce::Font::bold)); addAndMakeVisible(trackTitle); pageComps[0].push_back(&trackTitle);
        for (auto& c : gb::DrumLibrary::categories()) catBox.addItem(c, catBox.getNumItems() + 1);
        catBox.onChange = [this] { fillSounds(); }; soundBox.onChange = [this] { if (soundBox.getSelectedId() > 0) { proc.setTrackSound(grid.selected, soundBox.getText()); proc.auditionTrack(grid.selected); tracks.repaint(); } };
        prevSnd.onClick = [this] { stepSound(-1); }; nextSnd.onClick = [this] { stepSound(1); };
        loadSmp.getProperties().set("accent", true);
        loadSmp.onClick = [this] { chooser = std::make_unique<juce::FileChooser>("Pick a sound for this track", juce::File(), "*.wav;*.aif;*.aiff;*.mp3;*.flac;*.ogg;*.m4a");
            chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) loadOnto(grid.selected, fc.getResult()); }); };
        revBtn.setClickingTogglesState(true); revBtn.onClick = [this] { proc.model.tracks[(size_t) grid.selected].reverse = revBtn.getToggleState(); proc.publish(); };
        chokeBox.addItemList({ "No choke", "Choke 1", "Choke 2", "Choke 3", "Choke 4" }, 1);
        chokeBox.onChange = [this] { proc.model.tracks[(size_t) grid.selected].choke = chokeBox.getSelectedItemIndex(); proc.publish(); };
        auto row = [this] { return std::ref(grid.pat().steps[(size_t) grid.selected]); };
        clrRow.onClick = [this, row] { for (auto& s : row().get()) s = gb::Step(); grid.edited(); };
        auto fill = [this, row](int every) { auto& r = row().get(); for (int s = 0; s < grid.pat().length; ++s) { r[(size_t) s] = gb::Step(); if (s % every == 0) r[(size_t) s].vel = 105; } grid.edited(); };
        f4.onClick = [fill] { fill(4); }; f8.onClick = [fill] { fill(2); }; f16.onClick = [fill] { fill(1); };
        shL.onClick = [this, row] { auto& r = row().get(); const int L = grid.pat().length; std::rotate(r.begin(), r.begin() + 1, r.begin() + L); grid.edited(); };
        shR.onClick = [this, row] { auto& r = row().get(); const int L = grid.pat().length; std::rotate(r.begin(), r.begin() + L - 1, r.begin() + L); grid.edited(); };
        rnd.onClick = [this, row] { auto& r = row().get(); juce::Random R; for (int s = 0; s < grid.pat().length; ++s) { r[(size_t) s] = gb::Step(); if (R.nextFloat() < (s % 4 == 0 ? 0.6f : 0.25f)) { r[(size_t) s].vel = (uint8_t) (60 + R.nextInt(67)); if (R.nextFloat() < 0.15f) r[(size_t) s].roll = (uint8_t) (R.nextBool() ? 3 : 2); } } grid.edited(); };
        for (juce::Component* c : { (juce::Component*) &catBox, (juce::Component*) &soundBox, (juce::Component*) &prevSnd, (juce::Component*) &nextSnd, (juce::Component*) &loadSmp, (juce::Component*) &revBtn,
                                    (juce::Component*) &chokeBox, (juce::Component*) &clrRow, (juce::Component*) &f4, (juce::Component*) &f8, (juce::Component*) &f16, (juce::Component*) &shL, (juce::Component*) &shR, (juce::Component*) &rnd }) { addAndMakeVisible(c); pageComps[0].push_back(c); }
        const char* tk[] = { "PITCH", "DECAY", "VOLUME", "PAN", "TONE", "DRIVE" };
        for (int i = 0; i < 6; ++i) {
            auto* k = trackKnobs.add(new Knob(tk[i])); addAndMakeVisible(k); pageComps[0].push_back(k);
            static const double lo[] = { -24, 0.05, 0, -1, 0, 0 }, hi[] = { 24, 1, 1.2, 1, 1, 1 }, def[] = { 0, 1, 0.8, 0, 0.5, 0 };
            k->s.setRange(lo[i], hi[i], i == 0 ? 1.0 : 0.0); k->s.setDoubleClickReturnValue(true, def[i]);
            k->s.onValueChange = [this, i, k] { auto& tr = proc.model.tracks[(size_t) grid.selected]; const float v = (float) k->s.getValue();
                switch (i) { case 0: tr.pitch = v; break; case 1: tr.decay = v; break; case 2: tr.vol = v; break; case 3: tr.pan = v; break; case 4: tr.tone = v; break; default: tr.drive = v; } proc.publish(); };
            k->s.onDragEnd = [this] { proc.auditionTrack(grid.selected); };
        }
        // ---- PADS page
        addAndMakeVisible(pads); pageComps[1] = { &pads };
        recBtn.setClickingTogglesState(true); recBtn.getProperties().set("tint", (juce::int64) juce::Colour(0xfff87171).getARGB());
        recBtn.onClick = [this] { proc.recording.store(recBtn.getToggleState()); if (recBtn.getToggleState() && !proc.engine.internalPlay) { playBtn.setToggleState(true, juce::sendNotification); } };
        btnAtts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "repeat", repeatBtn)); repeatBtn.setClickingTogglesState(true);
        repeatDiv.addItemList({ "1/8", "1/16", "1/32", "1/16T" }, 1); repeatAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.apvts, "repeatdiv", repeatDiv);
        padHelp.setText("Tap pads to play (higher on a pad = harder). REC + PLAY writes what you tap into the current pattern, snapped to 1/16. Hold a pad with NOTE REPEAT on for hi-hat rolls. MIDI notes C1-D#2 play pads 1-16.", juce::dontSendNotification);
        padHelp.setFont(juce::FontOptions(12.5f)); padHelp.setColour(juce::Label::textColourId, GREY); padHelp.setJustificationType(juce::Justification::topLeft);
        for (juce::Component* c : { (juce::Component*) &recBtn, (juce::Component*) &repeatBtn, (juce::Component*) &repeatDiv, (juce::Component*) &padHelp }) { addAndMakeVisible(c); pageComps[1].push_back(c); }
        // ---- SONG page
        addAndMakeVisible(lane); pageComps[2] = { &lane }; lane.onEdit = [this] { lane.repaint(); };
        for (int i = 0; i < gb::kPatterns; ++i) { auto* c = chips.add(new PatternChip(i)); addAndMakeVisible(c); pageComps[2].push_back(c); }
        delBlock.onClick = [this] { if (lane.selected >= 0 && lane.selected < (int) proc.model.song.size()) { proc.model.song.erase(proc.model.song.begin() + lane.selected); lane.selected = -1; proc.publish(); lane.repaint(); } };
        dupBlock.onClick = [this] { if (lane.selected >= 0 && lane.selected < (int) proc.model.song.size()) { auto b = proc.model.song[(size_t) lane.selected]; b.startBar += b.bars; proc.model.song.push_back(b); lane.selected = (int) proc.model.song.size() - 1; proc.publish(); lane.repaint(); } };
        clrSong.onClick = [this] { proc.model.song.clear(); lane.selected = -1; proc.publish(); lane.repaint(); };
        btnAtts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "songmode", songModeBtn)); songModeBtn.setClickingTogglesState(true);
        songHelp.setText("SONG MODE on: when FL (or PLAY) runs, the beat follows this arrangement bar by bar. Off: the selected pattern loops. Switch patterns live with the letters at the top (FL can automate \"Pattern\").", juce::dontSendNotification);
        songHelp.setFont(juce::FontOptions(12.5f)); songHelp.setColour(juce::Label::textColourId, GREY); songHelp.setJustificationType(juce::Justification::topLeft);
        for (juce::Component* c : { (juce::Component*) &delBlock, (juce::Component*) &dupBlock, (juce::Component*) &clrSong, (juce::Component*) &songHelp }) { addAndMakeVisible(c); pageComps[2].push_back(c); }
        addAndMakeVisible(songModeBtn);
        // ---- bottom: master + performance + drag out
        const char* mk[] = { "SWING", "HUMAN", "GLITCH", "DJ FILTER", "RESO", "ECHO", "REVERB", "CRUSH", "DRIVE", "GLUE", "VOLUME" };
        const char* mid[] = { "swing", "human", "glitch", "filter", "res", "echo", "verb", "crush", "drive", "glue", "volume" };
        for (int i = 0; i < 11; ++i) { auto* k = masterKnobs.add(new Knob(mk[i])); k->att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, mid[i], k->s); addAndMakeVisible(k); }
        const char* pf[] = { "STUTTER 1/4", "1/8", "1/16", "1/32", "TAPE STOP" };
        for (int i = 0; i < 5; ++i) { auto* b = perf.add(new HoldButton(pf[i])); b->getProperties().set("tint", (juce::int64) PINK.getARGB());
            b->onHold = [this, i](bool on) { if (i < 4) proc.stutter.store(on ? i + 1 : 0); else proc.tape.store(on); }; addAndMakeVisible(b); }
        addAndMakeVisible(wavOut); addAndMakeVisible(midiOut); addAndMakeVisible(stemsOut);
        addAndMakeVisible(saveBeatBtn); addAndMakeVisible(loadBeatBtn); saveBeatBtn.getProperties().set("accent", true);
        saveBeatBtn.onClick = [this] {
            auto* w = new juce::AlertWindow("Save this beat", "Name it (kit, samples, all patterns and the song are saved):", juce::MessageBoxIconType::NoIcon);
            w->addTextEditor("name", kitBox.getText().upToFirstOccurrenceOf(" ", false, false) + " beat"); w->addButton("SAVE", 1, juce::KeyPress(juce::KeyPress::returnKey)); w->addButton("Cancel", 0);
            w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int r) { if (r == 1) { const auto n = w->getTextEditorContents("name").trim(); if (n.isNotEmpty()) say(proc.saveBeat(n) ? "Saved \"" + n + "\" in Documents/Ghosty/Beats." : "Couldn't save."); } }), true);
        };
        loadBeatBtn.onClick = [this] {
            juce::PopupMenu m; auto files = proc.beatsDir().findChildFiles(juce::File::findFiles, false, "*.ghostbeat"); files.sort();
            for (int i = 0; i < files.size(); ++i) m.addItem(i + 1, files[i].getFileNameWithoutExtension());
            if (files.isEmpty()) m.addItem(-1, "No saved beats yet (use SAVE BEAT)", false);
            m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&loadBeatBtn), [this, files](int r) { if (r > 0 && proc.loadBeat(files[r - 1])) { refreshAll(); say("Loaded \"" + files[r - 1].getFileNameWithoutExtension() + "\"."); } });
        };
        status.setFont(juce::FontOptions(12.0f)); status.setColour(juce::Label::textColourId, GREEN); addAndMakeVisible(status);
        setPage(0); selectTrack(0); refreshPattern(); refreshModes();
    }

    void say(const juce::String& s) { status.setText(s, juce::dontSendNotification); }
    void setPage(int p) { page = p; for (int i = 0; i < 3; ++i) { pageBtns[i]->setToggleState(i == p, juce::dontSendNotification); for (auto* c : pageComps[i]) c->setVisible(i == p); } repaint(); }
    void refreshModes() { for (int i = 0; i < 5; ++i) modeBtns[i]->setToggleState(grid.mode == i, juce::dontSendNotification); }
    void refreshPattern() {
        const int pi = grid.patIdx(); for (int i = 0; i < gb::kPatterns; ++i) patBtns[i]->setToggleState(i == pi, juce::dontSendNotification);
        const int L = grid.pat().length; lenBox.setSelectedItemIndex(L == 64 ? 2 : (L == 32 ? 1 : 0), juce::dontSendNotification);
        for (int i = 0; i < 4; ++i) { gridPageBtns[i]->setVisible(page == 0 && i * 16 < L); gridPageBtns[i]->setToggleState(grid.page == i, juce::dontSendNotification); }
        grid.page = juce::jmin(grid.page, L / 16 - 1); grid.repaint(); tracks.repaint();
    }
    void refreshAll() { refreshPattern(); selectTrack(grid.selected); pads.repaint(); lane.repaint(); }
    void selectTrack(int t) {
        grid.selected = t; tracks.selected = t; const auto& tr = proc.model.tracks[(size_t) t];
        trackTitle.setText("TRACK " + juce::String(t + 1) + "  -  " + tr.sound, juce::dontSendNotification); trackTitle.setColour(juce::Label::textColourId, trackColour(t));
        const auto snd = proc.lib->byName(tr.sound);
        catBox.setText(tr.sample ? "" : (snd ? snd->category : ""), juce::dontSendNotification); if (snd && !tr.sample) catBox.setSelectedItemIndex(gb::DrumLibrary::categories().indexOf(snd->category), juce::dontSendNotification);
        fillSounds(); soundBox.setText(tr.sound, juce::dontSendNotification);
        const double v[] = { tr.pitch, tr.decay, tr.vol, tr.pan, tr.tone, tr.drive };
        for (int i = 0; i < 6; ++i) trackKnobs[i]->s.setValue(v[i], juce::dontSendNotification);
        revBtn.setToggleState(tr.reverse, juce::dontSendNotification); chokeBox.setSelectedItemIndex(tr.choke, juce::dontSendNotification);
        grid.repaint(); tracks.repaint();
    }
    void fillSounds() {
        soundBox.clear(juce::dontSendNotification); const auto cat = catBox.getText(); int id = 1;
        for (auto& s : proc.lib->sounds()) if (s->category == cat) soundBox.addItem(s->name, id++);
    }
    void stepSound(int d) {
        auto& all = proc.lib->sounds(); const auto& tr = proc.model.tracks[(size_t) grid.selected];
        int i = proc.lib->indexOf(tr.sound); i = ((i < 0 ? 0 : i) + d + (int) all.size()) % (int) all.size();
        proc.setTrackSound(grid.selected, all[(size_t) i]->name); proc.auditionTrack(grid.selected); selectTrack(grid.selected);
    }
    void loadOnto(int t, const juce::File& f) {
        juce::String err;
        if (proc.loadSampleToTrack(t, f, err)) { selectTrack(t); proc.auditionTrack(t); say("Track " + juce::String(t + 1) + " now plays \"" + f.getFileNameWithoutExtension() + "\"."); }
        else say(err);
    }
    int trackAtScreen(juce::Point<int> local) { auto in = [&](juce::Component& c) { return c.isVisible() && c.getBounds().contains(local); };
        if (in(tracks)) return (int) ((local.y - tracks.getY()) / tracks.rowH()); if (in(grid)) return (int) ((local.y - grid.getY()) / grid.rowH());
        if (in(pads)) return pads.padAt((local - pads.getPosition()).toFloat()); return grid.selected; }

    void paint(juce::Graphics& g) override {
        g.fillAll(BG);
        g.setGradientFill(juce::ColourGradient(VIOLET.withAlpha(0.2f), W * 0.85f, -40.0f, VIOLET.withAlpha(0.0f), W * 0.85f, 360.0f, true)); g.fillRect(getLocalBounds());
        g.setColour(INK); g.setFont(juce::FontOptions(26.0f, juce::Font::bold)); g.drawText("GHOSTY BEATS", 18, 10, 240, 30, juce::Justification::centredLeft);
        g.setColour(PURPLE); g.setFont(juce::FontOptions(10.0f, juce::Font::bold)); g.drawText("BY GHOSTBLOCK", 20, 38, 200, 12, juce::Justification::centredLeft);
        auto card = [&](juce::Rectangle<int> r) { g.setColour(CARD.withAlpha(0.9f)); g.fillRoundedRectangle(r.toFloat(), 14); g.setColour(PURPLE.withAlpha(0.22f)); g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 14, 1); };
        card({ 12, 132, W - 24, 500 });
        card({ 12, 640, W - 24, 140 });
        if (page == 0) { g.setColour(CARD2); g.fillRoundedRectangle(918.0f, 140.0f, 250.0f, 484.0f, 12.0f); }
    }

    void resized() override {
        kitBox.setBounds(232, 14, 150, 30); playBtn.setBounds(390, 14, 70, 30); bpm.setBounds(466, 16, 150, 26);
        lenBox.setBounds(624, 14, 104, 30);
        int x = 736; for (auto* b : { &copyBtn, &pasteBtn, &clearBtn, &saveBtn, &loadBtn }) { b->setBounds(x, 14, 64, 30); x += 68; }

        for (int i = 0; i < gb::kPatterns; ++i) patBtns[i]->setBounds(18 + i * 40, 56, 36, 30);
        for (int i = 0; i < 3; ++i) pageBtns[i]->setBounds(18 + i * 118, 94, 112, 30);
        songModeBtn.setBounds(374, 94, 110, 30);
        // SEQUENCER
        for (int i = 0; i < 5; ++i) modeBtns[i]->setBounds(500 + i * 96 - (i > 2 ? 0 : 0), 94, i == 2 ? 118 : 90, 30);
        int mx = 500; for (int i = 0; i < 5; ++i) { const int w = i == 2 ? 120 : 88; modeBtns[i]->setBounds(mx, 94, w, 30); mx += w + 4; }
        for (int i = 0; i < 4; ++i) gridPageBtns[i]->setBounds(mx + 8 + i * 60, 94, 56, 30);
        tracks.setBounds(20, 142, 210, 480); grid.setBounds(236, 142, 672, 480);
        trackTitle.setBounds(928, 146, 232, 22);
        catBox.setBounds(928, 174, 110, 28); soundBox.setBounds(1042, 174, 118, 28);
        prevSnd.setBounds(928, 206, 34, 28); loadSmp.setBounds(966, 206, 156, 28); nextSnd.setBounds(1126, 206, 34, 28);
        for (int i = 0; i < 6; ++i) trackKnobs[i]->setBounds(930 + (i % 3) * 76, 242 + (i / 3) * 92, 72, 88);
        revBtn.setBounds(928, 432, 112, 28); chokeBox.setBounds(1046, 432, 114, 28);
        clrRow.setBounds(928, 470, 112, 28); rnd.setBounds(1046, 470, 114, 28);
        f4.setBounds(928, 506, 54, 28); f8.setBounds(986, 506, 54, 28); f16.setBounds(1044, 506, 56, 28);
        shL.setBounds(1104, 506, 26, 28); shR.setBounds(1134, 506, 26, 28);
        // PADS
        pads.setBounds(260, 142, 600, 480);
        recBtn.setBounds(880, 150, 130, 40); repeatBtn.setBounds(880, 200, 130, 34); repeatDiv.setBounds(1016, 200, 140, 34);
        padHelp.setBounds(880, 250, 280, 200);
        // SONG
        for (int i = 0; i < gb::kPatterns; ++i) chips[i]->setBounds(24 + (i % 4) * 46, 152 + (i / 4) * 46, 44, 44);
        lane.setBounds(220, 150, 940, 170);
        delBlock.setBounds(220, 332, 130, 30); dupBlock.setBounds(356, 332, 110, 30); clrSong.setBounds(472, 332, 110, 30);
        songHelp.setBounds(220, 372, 940, 60);
        // bottom
        for (int i = 0; i < 11; ++i) masterKnobs[i]->setBounds(22 + i * 72, 650, 68, 80);
        for (int i = 0; i < 5; ++i) perf[i]->setBounds(22 + i * (i == 0 ? 0 : 1) * 0 + (i == 0 ? 0 : 0), 0, 0, 0);
        int px = 22; for (int i = 0; i < 5; ++i) { const int w = i == 0 ? 104 : (i == 4 ? 104 : 56); perf[i]->setBounds(px, 738, w, 32); px += w + 4; }
        wavOut.setBounds(820, 652, 160, 36); midiOut.setBounds(990, 652, 166, 36); stemsOut.setBounds(820, 694, 336, 36);
        saveBeatBtn.setBounds(440, 738, 110, 32); loadBeatBtn.setBounds(556, 738, 110, 32);
        status.setBounds(668, 60, 500, 22);
    }
};

// =====================================================================================================
BeatsXEditor::BeatsXEditor(BeatsXProcessor& p) : AudioProcessorEditor(p), proc(p) {
    lnf = std::make_unique<LnF>(); setLookAndFeel(lnf.get());
    content = std::make_unique<Content>(p, *this); addAndMakeVisible(*content); content->setBounds(0, 0, W, H);
    setResizable(true, true); setResizeLimits(W * 3 / 4, H * 3 / 4, W * 2, H * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio((double) W / H);
    setSize(W, H); startTimerHz(30);
}
BeatsXEditor::~BeatsXEditor() { stopTimer(); content.reset(); setLookAndFeel(nullptr); }
void BeatsXEditor::paint(juce::Graphics& g) { g.fillAll(BG); }
void BeatsXEditor::resized() { if (content) content->setTransform(juce::AffineTransform::scale((float) getWidth() / W)); }
void BeatsXEditor::timerCallback() {
    proc.drainRecorded();
    auto& c = *content;
    if (c.page == 0) { c.grid.repaint(); c.tracks.repaint(); } else if (c.page == 1) c.pads.repaint(); else c.lane.repaint();
    if (c.playBtn.getToggleState() != proc.engine.internalPlay) c.playBtn.setToggleState(proc.engine.internalPlay, juce::dontSendNotification);
    const int pi = c.grid.patIdx(); if (!c.patBtns[pi]->getToggleState()) c.refreshPattern();   // pattern changed by automation
}
static bool isAudioFile(const juce::String& f) { return juce::File(f).hasFileExtension("wav;aif;aiff;mp3;flac;ogg;m4a;caf"); }
bool BeatsXEditor::isInterestedInFileDrag(const juce::StringArray& files) { for (auto& f : files) if (isAudioFile(f)) return true; return false; }
void BeatsXEditor::filesDropped(const juce::StringArray& files, int x, int y) {
    const float s = (float) getWidth() / W; const auto local = juce::Point<int>((int) (x / s), (int) (y / s));
    int t = content->trackAtScreen(local);
    for (auto& f : files) if (isAudioFile(f)) { if (juce::isPositiveAndBelow(t, gb::kTracks)) content->loadOnto(t, juce::File(f)); ++t; }   // several files fill the next tracks
}
