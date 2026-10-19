#include "SpecEditor.h"

namespace {
const juce::Colour BG { 0xff05040a }, CARD { 0xff0b0a13 }, CARD2 { 0xff110f1c }, INK { 0xfff5f3ff }, GREY { 0xff9ca3af }, DIM { 0xff4b5563 },
                   PURPLE { 0xffa855f7 }, VIOLET { 0xff8b5cf6 }, GREEN { 0xff00ff88 }, PINK { 0xfff472b6 }, AMBER { 0xfffbbf24 }, CYAN { 0xff22d3ee };
constexpr int W = 1200, H = 790;
juce::Colour layerColour(int i) { static const juce::Colour C[] = { GREEN, PINK, CYAN, AMBER, PURPLE, juce::Colour(0xfff87171), juce::Colour(0xffa3e635), juce::Colour(0xff60a5fa) }; return C[(size_t) (i % 8)]; }

struct LnF : juce::LookAndFeel_V4 {
    LnF() {
        setColour(juce::ComboBox::backgroundColourId, CARD2); setColour(juce::ComboBox::textColourId, INK); setColour(juce::ComboBox::arrowColourId, PURPLE);
        setColour(juce::PopupMenu::backgroundColourId, CARD2); setColour(juce::PopupMenu::textColourId, INK); setColour(juce::PopupMenu::highlightedBackgroundColourId, PURPLE.withAlpha(0.35f));
        setColour(juce::TextButton::textColourOffId, INK); setColour(juce::TextButton::textColourOnId, BG); setColour(juce::Label::textColourId, INK);
    }
    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override {
        auto r = b.getLocalBounds().toFloat().reduced(1); const bool on = b.getToggleState() || (bool) b.getProperties()["accent"];
        g.setColour(on ? PURPLE.withAlpha(0.88f) : CARD2); g.fillRoundedRectangle(r, 8);
        g.setColour((on ? GREEN : PURPLE).withAlpha(over ? 0.95f : 0.45f)); g.drawRoundedRectangle(r, 8, down ? 2.0f : 1.1f);
    }
    juce::Font getTextButtonFont(juce::TextButton&, int h) override { return juce::FontOptions(juce::jmin(13.0f, h * 0.48f), juce::Font::bold); }
    void drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&) override {
        auto r = juce::Rectangle<float>(0, 0, (float) w, (float) h).reduced(1); g.setColour(CARD2); g.fillRoundedRectangle(r, 8); g.setColour(PURPLE.withAlpha(0.5f)); g.drawRoundedRectangle(r, 8, 1.1f);
        juce::Path a; a.addTriangle((float) w - 16, h * 0.5f - 2, (float) w - 8, h * 0.5f - 2, (float) w - 12, h * 0.5f + 3); g.setColour(PURPLE); g.fillPath(a);
    }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return juce::FontOptions(12.5f, juce::Font::bold); }
    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override {
        auto r = juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h).reduced(3); const float sz = juce::jmin(r.getWidth(), r.getHeight()); auto c = r.withSizeKeepingCentre(sz, sz);
        const float th = 4, ang = a0 + pos * (a1 - a0); g.setColour(CARD2); g.fillEllipse(c.reduced(th + 1));
        juce::Path tr; tr.addCentredArc(c.getCentreX(), c.getCentreY(), sz / 2 - th / 2, sz / 2 - th / 2, 0, a0, a1, true); g.setColour(juce::Colours::white.withAlpha(0.08f)); g.strokePath(tr, juce::PathStrokeType(th));
        juce::Path ar; ar.addCentredArc(c.getCentreX(), c.getCentreY(), sz / 2 - th / 2, sz / 2 - th / 2, 0, a0, ang, true);
        g.setGradientFill(juce::ColourGradient(PURPLE, c.getX(), c.getBottom(), GREEN, c.getRight(), c.getY(), false)); g.strokePath(ar, juce::PathStrokeType(th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(INK); g.drawLine({ c.getCentre().getPointOnCircumference(sz * 0.12f, ang), c.getCentre().getPointOnCircumference(sz / 2 - th - 4, ang) }, 2.0f);
    }
};
struct Knob : juce::Component {
    juce::Slider s { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox }; juce::Label l;
    explicit Knob(const juce::String& n) { s.setPopupDisplayEnabled(true, true, nullptr); addAndMakeVisible(s); l.setText(n, juce::dontSendNotification); l.setJustificationType(juce::Justification::centred);
        l.setFont(juce::FontOptions(10.5f, juce::Font::bold)); l.setColour(juce::Label::textColourId, GREY); addAndMakeVisible(l); }
    void resized() override { auto r = getLocalBounds(); l.setBounds(r.removeFromBottom(14)); s.setBounds(r); }
};
} // namespace

// =====================================================================================================
// THE CANVAS: x = time in the loop, y = frequency (20 Hz at the bottom, 20 kHz at the top)
struct Canvas : juce::Component, juce::DragAndDropTarget {
    SpecProcessor& proc;
    enum Tool { Draw, Drop, Connect, Move, Erase } tool = Draw;
    int currentLayer = -1;                              // the sound DRAW uses
    std::function<int()> ensureLayer;                    // make a layer for the selected bank sound if needed
    std::function<void(int layer, double beat, float pitch)> onDropBank;
    std::function<void()> onEdit, onSelectLayer, onNeedSound;
    juce::Image spectro; double lastBeat = -1;
    float fft[1024] {}; juce::dsp::FFT fftObj { 11 }; std::vector<float> fftBuf = std::vector<float>(4096);
    std::vector<juce::Point<float>> stroke; int moveNote = -1, connectFrom = -1; bool moveLen = false; sp::Note orig; juce::Point<float> down;
    static constexpr float kLo = 16, kHi = 135;   // midi range shown (20 Hz .. 20 kHz)

    explicit Canvas(SpecProcessor& p) : proc(p) {}
    double beats() const { return proc.model.bars * 4.0; }
    float xOf(double b) const { return (float) (b / beats() * getWidth()); }
    double beatOf(float x) const { return juce::jlimit(0.0, beats(), (double) x / getWidth() * beats()); }
    float yOf(float midi) const { return (float) getHeight() * (1.0f - (midi - kLo) / (kHi - kLo)); }
    float midiOf(float y) const { return kLo + (1.0f - y / (float) getHeight()) * (kHi - kLo); }
    double grid() const { return 0.25; }
    double snapB(double b) const { return std::floor(b / grid() + 1e-6) * grid(); }
    float pitchFor(float y, int layer) const {
        float m = midiOf(y);
        if (layer >= 0 && layer < (int) proc.model.layers.size() && proc.model.layers[(size_t) layer].kind == 1) return std::round(m);   // drums: tune by height
        return proc.model.snap ? sp::snapPitch(m, proc.model.root, proc.model.scale) : std::round(m);
    }
    void resized() override { spectro = juce::Image(juce::Image::ARGB, juce::jmax(1, getWidth()), juce::jmax(1, getHeight()), true); }

    // paint the live spectrum into the column under the playhead (the loop becomes a picture)
    void feedSpectrum() {
        const float beat = proc.engine.uiBeat.load(); if (beat < 0 || !spectro.isValid()) { lastBeat = -1; return; }
        const int end = proc.engine.ringPos.load(), N = 2048;
        for (int i = 0; i < N; ++i) { const float w = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * i / (N - 1)); fftBuf[(size_t) i] = proc.engine.ring[(end - N + i + sp::SpecEngine::kRing) % sp::SpecEngine::kRing] * w; }
        std::fill(fftBuf.begin() + N, fftBuf.end(), 0.0f);
        fftObj.performFrequencyOnlyForwardTransform(fftBuf.data());
        const double sr = 48000.0;
        const int x0 = (int) xOf(lastBeat < 0 || beat < lastBeat ? beat : lastBeat), x1 = juce::jmax(x0 + 1, (int) xOf(beat));
        juce::Image::BitmapData bd(spectro, juce::Image::BitmapData::readWrite);
        for (int y = 0; y < spectro.getHeight(); ++y) {
            const float hz = sp::midiToHz(midiOf((float) y)); const int bin = juce::jlimit(1, N / 2 - 1, (int) (hz / sr * N));
            const float mag = juce::jlimit(0.0f, 1.0f, (juce::Decibels::gainToDecibels(fftBuf[(size_t) bin] / (N * 0.25f) + 1e-6f) + 72.0f) / 66.0f);
            const auto col = juce::Colour::fromHSV(0.78f - mag * 0.45f, 0.85f, 1.0f, mag * mag * 0.85f);
            for (int x = x0; x < x1 && x < spectro.getWidth(); ++x) bd.setPixelColour(x, y, col);
        }
        lastBeat = beat;
    }

    void paint(juce::Graphics& g) override {
        auto R = getLocalBounds().toFloat();
        juce::ColourGradient bgGrad(juce::Colour(0xff0d0820), 0, 0, juce::Colour(0xff04030a), 0, R.getBottom(), false); g.setGradientFill(bgGrad); g.fillRoundedRectangle(R, 14);
        g.drawImageAt(spectro, 0, 0);
        // frequency lines
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        for (float hz : { 30.f, 60.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f, 16000.f }) {
            const float y = yOf(sp::hzToMidi(hz)); g.setColour(juce::Colours::white.withAlpha(0.06f)); g.drawHorizontalLine((int) y, 0, R.getRight());
            g.setColour(GREY.withAlpha(0.6f)); g.drawText(hz >= 1000 ? juce::String(hz / 1000, 0) + "k" : juce::String((int) hz), (int) R.getRight() - 34, (int) y - 12, 30, 11, juce::Justification::right);
        }
        g.setColour(GREY.withAlpha(0.5f)); g.drawText("BASS", 6, (int) yOf(sp::hzToMidi(60)) - 12, 60, 11, juce::Justification::left);
        g.drawText("MIDS", 6, (int) yOf(sp::hzToMidi(800)) - 12, 60, 11, juce::Justification::left); g.drawText("AIR", 6, (int) yOf(sp::hzToMidi(9000)) - 12, 60, 11, juce::Justification::left);
        // beats
        for (int b = 0; b <= (int) beats(); ++b) { g.setColour(juce::Colours::white.withAlpha(b % 4 == 0 ? 0.16f : 0.05f)); g.drawVerticalLine((int) xOf(b), 0, R.getBottom()); }
        // glides (connections) then notes
        auto& N = proc.model.notes;
        for (size_t i = 0; i < N.size(); ++i) {
            if (!N[i].glide) continue;
            int prev = -1; double best = -1; for (size_t j = 0; j < N.size(); ++j) if (N[j].layer == N[i].layer && N[j].start < N[i].start && N[j].start > best) { best = N[j].start; prev = (int) j; }
            if (prev < 0) continue;
            const auto& a = N[(size_t) prev]; juce::Path p; const float ax = xOf(a.start + a.len), ay = yOf(a.pitch), bx = xOf(N[i].start), by = yOf(N[i].pitch);
            p.startNewSubPath(ax, ay); p.cubicTo(ax + (bx - ax) * 0.5f, ay, ax + (bx - ax) * 0.5f, by, bx, by);
            g.setColour(layerColour(a.layer).withAlpha(0.35f)); g.strokePath(p, juce::PathStrokeType(7.0f)); g.setColour(layerColour(a.layer)); g.strokePath(p, juce::PathStrokeType(2.0f));
        }
        for (size_t i = 0; i < N.size(); ++i) {
            const auto& n = N[i]; if (n.layer >= (int) proc.model.layers.size()) continue;
            const auto col = layerColour(n.layer); const bool drum = proc.model.layers[(size_t) n.layer].kind == 1;
            auto r = juce::Rectangle<float>(xOf(n.start), yOf(n.pitch) - 6, juce::jmax(8.0f, xOf(n.start + n.len) - xOf(n.start) - 2), 12);
            if (drum) r = juce::Rectangle<float>(16, 16).withCentre({ xOf(n.start) + 6, yOf(n.pitch) });
            g.setColour(col.withAlpha(0.22f)); g.fillRoundedRectangle(r.expanded(4), 8);
            g.setColour(col.withAlpha(0.45f + 0.5f * n.vel)); if (drum) g.fillEllipse(r); else g.fillRoundedRectangle(r, 6);
            if ((int) i == connectFrom) { g.setColour(INK); g.drawRoundedRectangle(r.expanded(3), 8, 2); }
        }
        if (stroke.size() > 1) { juce::Path p; p.startNewSubPath(stroke[0]); for (auto& q : stroke) p.lineTo(q); g.setColour(layerColour(juce::jmax(0, currentLayer)).withAlpha(0.8f)); g.strokePath(p, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }
        const float beat = proc.engine.uiBeat.load();
        if (beat >= 0) { const float x = xOf(beat); g.setColour(INK.withAlpha(0.8f)); g.fillRect(x - 1, 0.0f, 2.0f, R.getHeight()); }
        if (N.empty()) {
            g.setColour(INK.withAlpha(0.75f)); g.setFont(juce::FontOptions(17.0f, juce::Font::bold));
            g.drawFittedText("Pick a sound on the left, then DRAW a line here.\nHigher = higher pitch. Left to right = time.\nOr drag any sound straight onto the canvas.", getLocalBounds().reduced(60), juce::Justification::centred, 4);
        }
    }

    int noteAt(juce::Point<float> p, bool& onEdge) const {
        auto& N = proc.model.notes; onEdge = false;
        for (int i = (int) N.size() - 1; i >= 0; --i) {
            const auto& n = N[(size_t) i]; const float x0 = xOf(n.start), x1 = juce::jmax(x0 + 12, xOf(n.start + n.len)), y = yOf(n.pitch);
            if (p.x >= x0 - 4 && p.x <= x1 + 2 && std::abs(p.y - y) < 10) { onEdge = x1 - p.x < 8; return i; }
        }
        return -1;
    }
    void mouseDown(const juce::MouseEvent& e) override {
        down = e.position; bool edge = false; const int hit = noteAt(e.position, edge);
        if (tool == Draw || tool == Drop) {
            if (tool == Drop && hit < 0) { const int l = ensureLayer ? ensureLayer() : currentLayer; if (l < 0) return; addNote(l, snapB(beatOf(e.position.x)), pitchFor(e.position.y, l)); return; }
            if (hit >= 0 && tool == Drop) { moveNote = hit; orig = proc.model.notes[(size_t) hit]; moveLen = edge; return; }
            stroke = { e.position }; return;
        }
        if (tool == Erase) { if (hit >= 0) { proc.model.notes.erase(proc.model.notes.begin() + hit); edited(); } return; }
        if (tool == Move) { moveNote = hit; if (hit >= 0) { orig = proc.model.notes[(size_t) hit]; moveLen = edge; } return; }
        if (tool == Connect) {
            if (hit < 0) { connectFrom = -1; repaint(); return; }
            if (connectFrom < 0) { connectFrom = hit; repaint(); return; }
            auto& N = proc.model.notes; auto a = N[(size_t) connectFrom]; auto& b = N[(size_t) hit];
            if (a.layer == b.layer && hit != connectFrom) {
                if (b.start < a.start) { std::swap(N[(size_t) connectFrom], b); }
                auto& first = N[(size_t) (N[(size_t) connectFrom].start < N[(size_t) hit].start ? connectFrom : hit)]; auto& second = N[(size_t) (&first == &N[(size_t) connectFrom] ? hit : connectFrom)];
                second.glide = true; first.len = juce::jmax(0.125, second.start - first.start);
                edited();
            }
            connectFrom = -1; repaint();
        }
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (!stroke.empty()) { stroke.push_back(e.position); repaint(); return; }
        if (moveNote >= 0) {
            auto& n = proc.model.notes[(size_t) moveNote];
            if (moveLen) n.len = juce::jmax(grid(), snapB(beatOf(e.position.x) + grid()) - n.start);
            else { n.start = juce::jlimit(0.0, beats() - grid(), snapB(orig.start + beatOf(e.position.x) - beatOf(down.x))); n.pitch = pitchFor(yOf(orig.pitch) + e.position.y - down.y, n.layer); }
            repaint();
        }
        if (tool == Erase) { bool edge; const int hit = noteAt(e.position, edge); if (hit >= 0) { proc.model.notes.erase(proc.model.notes.begin() + hit); edited(); } }
    }
    void mouseUp(const juce::MouseEvent&) override {
        if (!stroke.empty()) { commitStroke(); stroke.clear(); repaint(); }
        if (moveNote >= 0) { moveNote = -1; edited(); }
    }
    // a drawn line -> notes: one per 1/16 the line crosses; same pitch in a row = one longer note
    void commitStroke() {
        const int l = ensureLayer ? ensureLayer() : currentLayer;
        if (l < 0) { if (onNeedSound) onNeedSound(); return; }
        const bool drum = proc.model.layers[(size_t) l].kind == 1;
        if (stroke.size() < 3) { addNote(l, snapB(beatOf(stroke[0].x)), pitchFor(stroke[0].y, l)); return; }
        std::map<int, float> cells;   // grid cell -> y
        for (size_t i = 1; i < stroke.size(); ++i) {
            const auto a = stroke[i - 1], b = stroke[i];
            for (int k = 0; k <= 8; ++k) { const auto q = a + (b - a) * (k / 8.0f); cells[(int) (beatOf(q.x) / grid())] = q.y; }
        }
        sp::Note cur; bool open = false;
        for (auto& [c, y] : cells) {
            const double st = c * grid(); if (st >= beats()) continue;
            const float pitch = pitchFor(y, l);
            if (!drum && open && std::abs(pitch - cur.pitch) < 0.01f && std::abs(cur.start + cur.len - st) < 1e-6) { cur.len += grid(); continue; }
            if (open) proc.model.notes.push_back(cur);
            cur = { l, st, drum ? grid() : grid(), pitch, 0.85f, false }; open = true;
        }
        if (open) proc.model.notes.push_back(cur);
        proc.audition(l, (int) std::round(cur.pitch)); edited();
    }
    void addNote(int l, double beat, float pitch) { proc.model.notes.push_back({ l, beat, proc.model.layers[(size_t) l].kind == 1 ? grid() : grid() * 2, pitch, 0.85f, false }); proc.audition(l, (int) std::round(pitch)); edited(); }
    void edited() { repaint(); proc.publish(); if (onEdit) onEdit(); }

    bool isInterestedInDragSource(const SourceDetails& d) override { return d.description.toString().startsWith("bank:"); }
    void itemDropped(const SourceDetails& d) override { if (onDropBank) onDropBank(d.description.toString().fromFirstOccurrenceOf("bank:", false, false).getIntValue(), beatOf((float) d.localPosition.x), midiOf((float) d.localPosition.y)); }
};

// SOUND BANK: tabs (SYNTHS / DRUMS / MY SAMPLES) -> groups -> sounds. Click = hear + pick for DRAW. Drag = onto the canvas.
struct Bank : juce::Component {
    SpecProcessor& proc; int tab = 0, group = 0, selected = -1; float scroll = 0;
    std::vector<int> items;   // indexes into the current list
    std::function<void(int tab, int index)> onPick;
    explicit Bank(SpecProcessor& p) : proc(p) {}
    juce::StringArray groups() const { if (tab == 0) return sp::synthGroups(); if (tab == 1) return gb::DrumLibrary::categories(); return { "My samples" }; }
    void rebuild() {
        items.clear(); const auto gname = groups()[juce::jlimit(0, groups().size() - 1, group)];
        if (tab == 0) { const auto& b = sp::synthBank(); for (int i = 0; i < (int) b.size(); ++i) if (b[(size_t) i].group == gname) items.push_back(i); }
        else if (tab == 1) { const auto& s = proc.drums->sounds(); for (int i = 0; i < (int) s.size(); ++i) if (s[(size_t) i]->category == gname) items.push_back(i); }
        else for (int i = 0; i < (int) proc.userSamples.size(); ++i) items.push_back(i);
        scroll = 0; repaint();
    }
    juce::String nameOf(int idx) const { if (tab == 0) return sp::synthBank()[(size_t) idx].name; if (tab == 1) return proc.drums->sounds()[(size_t) idx]->name; return proc.userSamples[(size_t) idx]->name; }
    static constexpr int kTabH = 30, kGroupRowH = 24, kItemH = 26;
    int groupRows() const { return (groups().size() + 2) / 3; }
    int listTop() const { return kTabH + 6 + groupRows() * kGroupRowH + 8; }
    void paint(juce::Graphics& g) override {
        auto R = getLocalBounds().toFloat(); g.setColour(CARD); g.fillRoundedRectangle(R, 12);
        const char* tabs[] = { "SYNTHS", "DRUMS", "MY SAMPLES" };
        for (int t = 0; t < 3; ++t) { auto r = juce::Rectangle<float>(4 + t * (R.getWidth() - 8) / 3, 4, (R.getWidth() - 8) / 3 - 4, (float) kTabH - 4);
            g.setColour(t == tab ? PURPLE.withAlpha(0.85f) : CARD2); g.fillRoundedRectangle(r, 7); g.setColour(t == tab ? BG : INK); g.setFont(juce::FontOptions(11.5f, juce::Font::bold)); g.drawText(tabs[t], r, juce::Justification::centred); }
        auto gs = groups();
        for (int i = 0; i < gs.size(); ++i) { auto r = juce::Rectangle<float>(4 + (i % 3) * (R.getWidth() - 8) / 3, (float) kTabH + 6 + (i / 3) * kGroupRowH, (R.getWidth() - 8) / 3 - 4, (float) kGroupRowH - 4);
            g.setColour(i == group ? GREEN.withAlpha(0.25f) : CARD2); g.fillRoundedRectangle(r, 6); g.setColour(i == group ? GREEN : GREY); g.setFont(juce::FontOptions(11.0f, juce::Font::bold)); g.drawText(gs[i], r, juce::Justification::centred); }
        g.saveState(); g.reduceClipRegion(0, listTop(), getWidth(), getHeight() - listTop());
        for (int i = 0; i < (int) items.size(); ++i) {
            auto r = juce::Rectangle<float>(6, listTop() + i * kItemH - scroll, R.getWidth() - 12, (float) kItemH - 3);
            g.setColour(items[(size_t) i] == selected ? PURPLE.withAlpha(0.35f) : CARD2); g.fillRoundedRectangle(r, 6);
            g.setColour(INK); g.setFont(juce::FontOptions(12.5f, juce::Font::bold)); g.drawText(nameOf(items[(size_t) i]), r.reduced(10, 0), juce::Justification::centredLeft);
            g.setColour(GREY); g.setFont(juce::FontOptions(10.0f)); g.drawText("drag", r.reduced(8, 0), juce::Justification::centredRight);
        }
        g.restoreState();
        if (tab == 2 && items.empty()) { g.setColour(GREY); g.setFont(juce::FontOptions(12.5f)); g.drawFittedText("Drop audio files anywhere on Spectrum and they show up here.", getLocalBounds().withTrimmedTop(listTop()).reduced(14), juce::Justification::centredTop, 3); }
    }
    void mouseDown(const juce::MouseEvent& e) override {
        const auto p = e.position;
        if (p.y < kTabH) { tab = juce::jlimit(0, 2, (int) (p.x / (getWidth() / 3.0f))); group = 0; rebuild(); return; }
        if (p.y < listTop()) { const int i = (int) ((p.y - kTabH - 6) / kGroupRowH) * 3 + (int) (p.x / (getWidth() / 3.0f)); if (i < groups().size()) { group = i; rebuild(); } return; }
        const int i = (int) ((p.y - listTop() + scroll) / kItemH); if (!juce::isPositiveAndBelow(i, (int) items.size())) return;
        selected = items[(size_t) i]; repaint(); if (onPick) onPick(tab, selected);
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (selected < 0 || e.mouseDownPosition.y < listTop() || e.getDistanceFromDragStart() < 8) return;
        if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor(this)) if (!c->isDragAndDropActive()) c->startDragging("bank:" + juce::String(tab * 10000 + selected), this);
    }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { scroll = juce::jlimit(0.0f, juce::jmax(0.0f, items.size() * (float) kItemH - (getHeight() - listTop())), scroll - w.deltaY * 120); repaint(); }
};

// =====================================================================================================
struct SpecEditor::Content : juce::Component {
    SpecProcessor& proc; Canvas canvas; Bank bank;
    juce::TextButton playBtn { "PLAY" }, saveBtn { "SAVE" }, loadBtn { "LOAD" }, clearBtn { "CLEAR" }, snapBtn { "SNAP TO KEY" };
    juce::OwnedArray<juce::TextButton> toolBtns, layerBtns;
    juce::ComboBox barsBox, keyBox, scaleBox;
    juce::Slider bpm { juce::Slider::LinearBar, juce::Slider::TextBoxRight }; std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bpmAtt;
    juce::OwnedArray<Knob> knobs; juce::TextButton muteBtn { "MUTE" }, delLayer { "REMOVE SOUND" };
    juce::Label layerTitle, status, guide;
    int pickTab = 0, pickIdx = -1;
    struct DragOut : juce::Component {
        juce::String text; std::function<juce::StringArray()> make; bool started = false;
        DragOut(juce::String t, std::function<juce::StringArray()> m) : text(std::move(t)), make(std::move(m)) {}
        void paint(juce::Graphics& g) override { auto r = getLocalBounds().toFloat().reduced(1); g.setColour(GREEN.withAlpha(0.15f)); g.fillRoundedRectangle(r, 8); g.setColour(GREEN.withAlpha(0.9f)); g.drawRoundedRectangle(r, 8, 1.3f);
            g.setFont(juce::FontOptions(12.0f, juce::Font::bold)); g.drawText(text, r, juce::Justification::centred); }
        void mouseDown(const juce::MouseEvent&) override { started = false; }
        void mouseDrag(const juce::MouseEvent& e) override { if (started || e.getDistanceFromDragStart() < 6) return; started = true; auto f = make(); if (!f.isEmpty()) juce::DragAndDropContainer::performExternalDragDropOfFiles(f, false, this); }
    };
    DragOut wavOut { "DRAG LOOP OUT (WAV)", [this] { return juce::StringArray(proc.bounce().getFullPathName()); } };
    DragOut stemsOut { "DRAG STEMS OUT", [this] { return proc.bounceStems(); } };
    std::unique_ptr<juce::FileChooser> chooser;

    explicit Content(SpecProcessor& p) : proc(p), canvas(p), bank(p) {
        addAndMakeVisible(canvas); addAndMakeVisible(bank);
        bank.onPick = [this](int tab, int idx) { pick(tab, idx); };
        bank.rebuild();
        canvas.ensureLayer = [this] { return ensureLayer(); };
        canvas.onDropBank = [this](int code, double beat, float midi) { dropBank(code / 10000, code % 10000, beat, midi); };
        canvas.onEdit = [this] { refreshLayers(); };
        canvas.onNeedSound = [this] { say("Pick a sound in the bank first (left), then draw."); };
        const char* tools[] = { "DRAW", "DROP", "CONNECT", "MOVE", "ERASE" };
        for (int i = 0; i < 5; ++i) { auto* b = toolBtns.add(new juce::TextButton(tools[i])); b->onClick = [this, i] { canvas.tool = (Canvas::Tool) i; refreshTools(); }; addAndMakeVisible(b); }
        playBtn.setClickingTogglesState(true); playBtn.onClick = [this] { proc.engine.playing = playBtn.getToggleState(); };
        for (auto* b : { &playBtn, &saveBtn, &loadBtn, &clearBtn, &snapBtn, &muteBtn, &delLayer }) addAndMakeVisible(b);
        bpm.setRange(60, 200, 1); bpm.setNumDecimalPlacesToDisplay(0); bpm.setTextValueSuffix(" bpm"); bpmAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, "bpm", bpm);
        bpm.setColour(juce::Slider::trackColourId, PURPLE.withAlpha(0.5f)); bpm.setColour(juce::Slider::textBoxTextColourId, INK); addAndMakeVisible(bpm);
        barsBox.addItemList({ "1 bar", "2 bars", "4 bars" }, 1); barsBox.onChange = [this] { static const int B[] = { 1, 2, 4 }; proc.model.bars = B[juce::jmax(0, barsBox.getSelectedItemIndex())]; proc.publish(); canvas.resized(); canvas.repaint(); };
        keyBox.addItemList({ "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 1); keyBox.onChange = [this] { proc.model.root = keyBox.getSelectedItemIndex(); };
        scaleBox.addItemList(sp::scaleNames(), 1); scaleBox.onChange = [this] { proc.model.scale = scaleBox.getSelectedItemIndex(); };
        for (auto* c : { &barsBox, &keyBox, &scaleBox }) addAndMakeVisible(c);
        snapBtn.setClickingTogglesState(true); snapBtn.onClick = [this] { proc.model.snap = snapBtn.getToggleState(); };
        clearBtn.onClick = [this] { proc.model.notes.clear(); proc.publish(); canvas.resized(); canvas.repaint(); say("Canvas cleared."); };
        saveBtn.onClick = [this] { const auto n = "Spectrum " + juce::Time::getCurrentTime().formatted("%m-%d %H%M"); say(proc.saveCanvas(n) ? "Saved \"" + n + "\" in Documents/Ghosty/Spectrum." : "Couldn't save."); };
        loadBtn.onClick = [this] { juce::PopupMenu m; auto files = proc.userDir().findChildFiles(juce::File::findFiles, false, "*.ghostspec"); files.sort();
            for (int i = 0; i < files.size(); ++i) m.addItem(i + 1, files[i].getFileNameWithoutExtension()); if (files.isEmpty()) m.addItem(-1, "Nothing saved yet (use SAVE)", false);
            m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&loadBtn), [this, files](int r) { if (r > 0 && proc.loadCanvas(files[r - 1])) { syncFromModel(); say("Loaded."); } }); };
        const char* kn[] = { "VOLUME", "TONE", "SPACE", "PAN" };
        for (int i = 0; i < 4; ++i) {
            auto* k = knobs.add(new Knob(kn[i])); addAndMakeVisible(k);
            k->s.setRange(i == 3 ? -1.0 : 0.0, 1.0); k->s.setDoubleClickReturnValue(true, i == 0 ? 0.8 : (i == 1 ? 0.5 : (i == 2 ? 0.2 : 0.0)));
            k->s.onValueChange = [this, i, k] { if (canvas.currentLayer < 0 || canvas.currentLayer >= (int) proc.model.layers.size()) return; auto& L = proc.model.layers[(size_t) canvas.currentLayer];
                const float v = (float) k->s.getValue(); if (i == 0) L.vol = v; else if (i == 1) L.tone = v; else if (i == 2) L.space = v; else L.pan = v; proc.publish(); };
        }
        muteBtn.setClickingTogglesState(true); muteBtn.onClick = [this] { if (canvas.currentLayer >= 0) { proc.model.layers[(size_t) canvas.currentLayer].mute = muteBtn.getToggleState(); proc.publish(); refreshLayers(); } };
        delLayer.onClick = [this] { if (canvas.currentLayer >= 0) { proc.removeLayer(canvas.currentLayer); canvas.currentLayer = -1; refreshLayers(); canvas.repaint(); } };
        for (int i = 0; i < sp::kLayers; ++i) { auto* b = layerBtns.add(new juce::TextButton()); b->onClick = [this, i] { selectLayer(i); }; addAndMakeVisible(b); }
        addAndMakeVisible(wavOut); addAndMakeVisible(stemsOut);
        layerTitle.setFont(juce::FontOptions(14.0f, juce::Font::bold)); addAndMakeVisible(layerTitle);
        status.setFont(juce::FontOptions(12.0f)); status.setColour(juce::Label::textColourId, GREEN); addAndMakeVisible(status);
        guide.setFont(juce::FontOptions(13.0f, juce::Font::bold)); guide.setJustificationType(juce::Justification::centredLeft); addAndMakeVisible(guide);
        syncFromModel();
    }
    void say(const juce::String& s) { status.setText(s, juce::dontSendNotification); }
    void syncFromModel() {
        static const int B[] = { 1, 2, 4 }; for (int i = 0; i < 3; ++i) if (B[i] == proc.model.bars) barsBox.setSelectedItemIndex(i, juce::dontSendNotification);
        keyBox.setSelectedItemIndex(proc.model.root, juce::dontSendNotification); scaleBox.setSelectedItemIndex(proc.model.scale, juce::dontSendNotification); snapBtn.setToggleState(proc.model.snap, juce::dontSendNotification);
        canvas.currentLayer = proc.model.layers.empty() ? -1 : 0; refreshTools(); refreshLayers(); canvas.resized(); canvas.repaint();
    }
    void refreshTools() { for (int i = 0; i < 5; ++i) toolBtns[i]->setToggleState((int) canvas.tool == i, juce::dontSendNotification); }
    // step-by-step guide that lights up the next thing to do
    void refreshGuide() {
        bool hasDrum = false; for (auto& L : proc.model.layers) if (L.kind == 1) hasDrum = true;
        juce::String t;
        if (proc.model.layers.empty() && pickIdx < 0) t = "STEP 1  -  pick a sound in the bank on the left (click to hear it)";
        else if (proc.model.notes.empty()) t = "STEP 2  -  DRAW a line on the canvas (or drag the sound onto it). Then press PLAY.";
        else if (!hasDrum) t = "STEP 3  -  open DRUMS in the bank and paint a beat (low = kick, high = hats)";
        else t = "STEP 4  -  tweak, CONNECT notes into slides, then DRAG LOOP OUT into FL (or DRAG STEMS OUT)";
        guide.setText(t, juce::dontSendNotification);
    }
    void refreshLayers() {
        for (int i = 0; i < sp::kLayers; ++i) {
            auto* b = layerBtns[i]; const bool has = i < (int) proc.model.layers.size(); b->setVisible(has);
            if (has) { const auto& L = proc.model.layers[(size_t) i]; b->setButtonText((L.mute ? "(muted) " : "") + L.name); b->setToggleState(i == canvas.currentLayer, juce::dontSendNotification); }
        }
        const bool sel = canvas.currentLayer >= 0 && canvas.currentLayer < (int) proc.model.layers.size();
        for (auto* k : knobs) k->setVisible(sel); muteBtn.setVisible(sel); delLayer.setVisible(sel);
        if (sel) {
            const auto& L = proc.model.layers[(size_t) canvas.currentLayer];
            layerTitle.setText("SOUND " + juce::String(canvas.currentLayer + 1) + ":  " + L.name, juce::dontSendNotification); layerTitle.setColour(juce::Label::textColourId, layerColour(canvas.currentLayer));
            const double v[] = { L.vol, L.tone, L.space, L.pan }; for (int i = 0; i < 4; ++i) knobs[i]->s.setValue(v[i], juce::dontSendNotification);
            muteBtn.setToggleState(L.mute, juce::dontSendNotification);
        } else layerTitle.setText("Sounds on the canvas show here", juce::dontSendNotification);
        refreshGuide();
    }
    void selectLayer(int i) { canvas.currentLayer = i; pickIdx = -1; refreshLayers(); proc.audition(i, 60); }
    // a bank click: hear it, and make it the sound DRAW will use
    void pick(int tab, int idx) {
        pickTab = tab; pickIdx = idx;
        const int kind = tab == 0 ? 0 : (tab == 1 ? 1 : 2);
        const juce::String name = tab == 0 ? sp::synthBank()[(size_t) idx].name : (tab == 1 ? proc.drums->sounds()[(size_t) idx]->name : proc.userSamples[(size_t) idx]->name);
        const int existing = proc.layerFor(kind, name);
        if (existing >= 0) { selectLayer(existing); return; }
        if (tab == 1) proc.auditionDrum(name); else { const int l = ensureLayer(); if (l >= 0) { proc.audition(l, 60); selectLayer(l); } }
        say("\"" + name + "\" picked. DRAW on the canvas to use it."); refreshGuide();
    }
    int ensureLayer() {
        if (pickIdx < 0) return canvas.currentLayer;
        const int kind = pickTab == 0 ? 0 : (pickTab == 1 ? 1 : 2);
        juce::String name, words; uint32_t seed = 1; std::shared_ptr<gs::SampleData> smp;
        if (pickTab == 0) { const auto& b = sp::synthBank()[(size_t) pickIdx]; name = b.name; words = b.words; seed = b.seed; }
        else if (pickTab == 1) name = proc.drums->sounds()[(size_t) pickIdx]->name;
        else { smp = proc.userSamples[(size_t) pickIdx]; name = smp->name; }
        int l = proc.layerFor(kind, name);
        if (l < 0) l = proc.addLayer(kind, name, words, seed, smp);
        if (l < 0) { say("8 sounds is the max - remove one first (REMOVE SOUND)."); return -1; }
        pickIdx = -1; canvas.currentLayer = l; refreshLayers(); return l;
    }
    void dropBank(int tab, int idx, double beat, float midi) {
        pick(tab, idx); const int l = ensureLayer(); if (l < 0) return;
        canvas.addNote(l, canvas.snapB(beat), canvas.pitchFor(canvas.yOf(midi), l)); refreshLayers();
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(BG);
        g.setGradientFill(juce::ColourGradient(VIOLET.withAlpha(0.22f), W * 0.6f, -60.0f, VIOLET.withAlpha(0.0f), W * 0.6f, 300.0f, true)); g.fillRect(getLocalBounds());
        g.setColour(INK); g.setFont(juce::FontOptions(26.0f, juce::Font::bold)); g.drawText("GHOSTY SPECTRUM", 16, 10, 300, 30, juce::Justification::centredLeft);
        g.setColour(PURPLE); g.setFont(juce::FontOptions(10.0f, juce::Font::bold)); g.drawText("PAINT A LOOP ON THE SOUND SPECTRUM", 18, 38, 300, 12, juce::Justification::centredLeft);
        g.setColour(CARD.withAlpha(0.9f)); g.fillRoundedRectangle(16.0f, 690.0f, (float) W - 32, 92.0f, 12.0f);
    }
    void resized() override {
        playBtn.setBounds(330, 14, 76, 32); bpm.setBounds(412, 17, 150, 26); barsBox.setBounds(570, 14, 90, 32); keyBox.setBounds(666, 14, 60, 32); scaleBox.setBounds(732, 14, 150, 32); snapBtn.setBounds(888, 14, 110, 32);
        saveBtn.setBounds(1004, 14, 58, 32); loadBtn.setBounds(1066, 14, 58, 32); clearBtn.setBounds(1128, 14, 58, 32);
        guide.setBounds(330, 52, 856, 22);
        bank.setBounds(16, 84, 270, 600);
        for (int i = 0; i < 5; ++i) toolBtns[i]->setBounds(296 + i * 92, 84, 88, 32);
        status.setBounds(760, 92, 426, 18);
        canvas.setBounds(296, 124, 890, 560);
        for (int i = 0; i < sp::kLayers; ++i) layerBtns[i]->setBounds(28 + (i % 4) * 150, 700 + (i / 4) * 38, 146, 32);
        layerTitle.setBounds(640, 696, 300, 20);
        for (int i = 0; i < 4; ++i) knobs[i]->setBounds(640 + i * 62, 716, 58, 62);
        muteBtn.setBounds(892, 722, 68, 28); delLayer.setBounds(892, 752, 68 + 0, 26);
        wavOut.setBounds(968, 700, 218, 36); stemsOut.setBounds(968, 742, 218, 34);
    }
};

// =====================================================================================================
SpecEditor::SpecEditor(SpecProcessor& p) : AudioProcessorEditor(p), proc(p) {
    lnf = std::make_unique<LnF>(); setLookAndFeel(lnf.get());
    content = std::make_unique<Content>(p); addAndMakeVisible(*content); content->setBounds(0, 0, W, H);
    setResizable(true, true); setResizeLimits(W * 3 / 4, H * 3 / 4, W * 2, H * 2); if (auto* c = getConstrainer()) c->setFixedAspectRatio((double) W / H);
    setSize(W, H); startTimerHz(30);
}
SpecEditor::~SpecEditor() { stopTimer(); content.reset(); setLookAndFeel(nullptr); }
void SpecEditor::paint(juce::Graphics& g) { g.fillAll(BG); }
void SpecEditor::resized() { if (content) content->setTransform(juce::AffineTransform::scale((float) getWidth() / W)); }
void SpecEditor::timerCallback() {
    content->canvas.feedSpectrum(); content->canvas.repaint();
    if (content->playBtn.getToggleState() != proc.engine.playing) content->playBtn.setToggleState(proc.engine.playing, juce::dontSendNotification);
}
static bool isAudio(const juce::String& f) { return juce::File(f).hasFileExtension("wav;aif;aiff;mp3;flac;ogg;m4a;caf"); }
bool SpecEditor::isInterestedInFileDrag(const juce::StringArray& f) { for (auto& x : f) if (isAudio(x)) return true; return false; }
void SpecEditor::filesDropped(const juce::StringArray& files, int, int) {
    for (auto& f : files) if (isAudio(f)) { juce::String err; if (auto s = gs::SampleData::fromFile(juce::File(f), err)) proc.userSamples.push_back(s); }
    content->bank.tab = 2; content->bank.rebuild(); content->say("Your sounds are in MY SAMPLES. Click one, then DRAW.");
}
