#include "LabPanel.h"
#include "WaveRegion.h"   // Shared: START / END / LOOP + the GhostBlock sample gestures
#include "GhostArt.h"
#include "SpecTheme.h"
#include <unordered_map>

using namespace lab;
namespace {
const char* const kDemoExport = "Exporting (drag-out WAV, SEND TO BEATS, stems, MIDI) is part of the full version - click UNLOCK to enter your key.";
// the palette (SpecTheme.h): every surface is opaque, the values follow the DARK / LIGHT theme at paint time
juce::Colour &BG = sp::pal().bg, &CARD = sp::pal().panel, &CARD2 = sp::pal().panel2, &INK = sp::pal().ink, &GREY = sp::pal().grey, &DIM = sp::pal().dim,
             &PURPLE = sp::pal().purple, &GREEN = sp::pal().green, &PINK = sp::pal().pink, &AMBER = sp::pal().amber, &CYAN = sp::pal().cyan, &RED = sp::pal().red, &BLUE = sp::pal().blue;
constexpr int W = 1200, H = 790;
const juce::Rectangle<int> kView { 244, 90, 940, 470 }, kPage { 244, 568, 940, 214 }, kSide { 16, 54, 218, 728 };
const juce::Rectangle<int> kMeter { 1088, 14, 96, 24 };
const int kPageOutput[] = { OutCanvas, OutCanvas, OutGhost, OutSplit, OutCanvas, OutMorph };   // what you hear follows the page
juce::Colour bandColour(int i) { const juce::Colour C[] = { PINK, AMBER, GREEN, CYAN, PURPLE, RED }; return C[(size_t) (i % 6)]; }
void tint(juce::Component& b, juce::Colour c) { b.getProperties().set("tint", (juce::int64) c.getARGB()); }
juce::String noteName(int m) { static const char* N[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return juce::String(N[((m % 12) + 12) % 12]) + juce::String(m / 12 - 1); }
void card(juce::Graphics& g, juce::Rectangle<int> r) {   // an opaque panel with a thin border (no translucency over the window)
    g.setColour(CARD); g.fillRoundedRectangle(r.toFloat(), 12.0f); g.setColour(sp::pal().line); g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 12.0f, 1.0f);
}
void caption(juce::Graphics& g, const juce::String& s, int x, int y, int w = 200, juce::Colour c = GREY) { g.setColour(c); g.setFont(juce::FontOptions(10.5f, juce::Font::bold)); g.drawText(s, x, y, w, 14, juce::Justification::centredLeft); }
juce::String hzText(double hz) { return hz >= 1000 ? juce::String(hz / 1000.0, 1) + "k" : juce::String((int) std::round(hz)); }

struct Knob : juce::Component {
    juce::Slider s { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox }; juce::Label l;
    std::function<void()> onStart, onEnd;
    Knob(const juce::String& n, juce::Colour c, double lo, double hi, double def, std::function<void(double)> f, const juce::String& tip) {
        s.setRange(lo, hi); s.setValue(def, juce::dontSendNotification); s.setDoubleClickReturnValue(true, def); s.setPopupDisplayEnabled(true, true, nullptr);
        s.getProperties().set("tint", (juce::int64) c.getARGB()); s.onValueChange = [this, f] { if (f) f(s.getValue()); }; s.setTooltip(tip + "  (double-click: default)");
        s.onDragStart = [this] { if (onStart) onStart(); }; s.onDragEnd = [this] { if (onEnd) onEnd(); };
        addAndMakeVisible(s);
        l.setText(n, juce::dontSendNotification); l.setJustificationType(juce::Justification::centred); l.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        l.setColour(juce::Label::textColourId, GREY); l.getProperties().set("role", "grey"); l.setInterceptsMouseClicks(false, false); addAndMakeVisible(l);
    }
    void resized() override { auto r = getLocalBounds(); l.setBounds(r.removeFromBottom(14)); s.setBounds(r); }
};
struct DragOut : juce::Component, juce::SettableTooltipClient {
    juce::String text; std::function<juce::StringArray()> make; bool started = false;
    DragOut(juce::String t, juce::Colour col, std::function<juce::StringArray()> m) : text(std::move(t)), make(std::move(m)) { tint(*this, col); }
    juce::Colour accent() const { return juce::Colour((juce::uint32) (juce::int64) getProperties()["tint"]); }
    void paint(juce::Graphics& g) override { auto r = getLocalBounds().toFloat().reduced(1); const auto c = accent();
        g.setColour(CARD2.interpolatedWith(c, isMouseOver() ? 0.22f : 0.12f)); g.fillRoundedRectangle(r, 8); g.setColour(c); g.drawRoundedRectangle(r, 8, 1.3f);
        g.setColour(INK); g.setFont(juce::FontOptions(11.5f, juce::Font::bold)); g.drawText(text, r, juce::Justification::centred); }
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    void mouseDown(const juce::MouseEvent&) override { started = false; }
    void mouseDrag(const juce::MouseEvent& e) override { if (started || e.getDistanceFromDragStart() < 6) return; started = true; auto f = make(); f.removeEmptyStrings(); if (!f.isEmpty()) ghosty::exportgate::dragFiles(f, this); }   // make() asked the gate
};
} // namespace

// =====================================================================================================
// THE SPECTROGRAM VIEW (log frequency up, time across, colour = level). Wheel = zoom time, shift + wheel = zoom pitch,
// horizontal wheel / scrollbars = scroll, ruler: click-hold = scrub, double-click = fit.
struct SpecView : juce::Component {
    LabHost& L; SpecProcessor& proc;
    enum Tool { Paint, Erase, Smear, Freeze, Shift, Stretch, Lock, Select } tool = Paint;
    static constexpr int kRuler = 18, kBar = 10;
    bool snap = false; int page = 0, gridDiv = 0;   // gridDiv 0 = off, else 4..64
    double vt0 = 0, vt1 = 1, vr0 = 0, vr1 = R - 1; bool fitTime = true;
    juce::Image img; bool imgDirty = true, blank = true; juce::uint32 lastImg = 0; int lastPlayX = -1;
    struct Saved { std::vector<Cell> cells; std::vector<float> a, f, n; };
    std::unordered_map<int, Saved> saved;   // frames as they were before this stroke, backed up as the brush first reaches them (no whole-canvas copy)
    int tmin = 1 << 30, tmax = -1, imgT0 = 1 << 30, imgT1 = -1; juce::Point<float> last; bool stroking = false, rightErase = false;
    Sel sel; bool haveSel = false, draggingSel = false, makingSel = false, fine = false; juce::Point<float> selStart; float dragDy = 0;
    int dragXover = -1, dragBar = 0; juce::Point<float> hover { -1, -1 }, barGrab; double barT0 = 0, barR0 = 0;
    std::function<void(const juce::String&)> say; std::function<void()> onEdit, onSelection, onSideEdit;
    SpecView(SpecProcessor& p) : L(p.lab), proc(p) { setMouseCursor(juce::MouseCursor::CrosshairCursor); setOpaque(true); }
    juce::Rectangle<float> area() const { return { 0.0f, (float) kRuler, (float) getWidth() - kBar, (float) getHeight() - kRuler - kBar }; }
    double T() const { return std::max(1, L.canvas.T); }
    float tOf(float x) const { const auto a = area(); return (float) (vt0 + (x - a.getX()) / a.getWidth() * (vt1 - vt0)); }
    float rOf(float y) const { const auto a = area(); return (float) (vr1 - (y - a.getY()) / a.getHeight() * (vr1 - vr0)); }
    float xOf(double t) const { const auto a = area(); return a.getX() + (float) ((t - vt0) / (vt1 - vt0)) * a.getWidth(); }
    float yOf(double r) const { const auto a = area(); return a.getY() + (float) ((vr1 - r) / (vr1 - vr0)) * a.getHeight(); }
    float timeScale() const { const auto a = area(); return (float) ((a.getHeight() / (vr1 - vr0)) / (a.getWidth() / (vt1 - vt0))); }
    void fit() { vt0 = 0; vt1 = T(); vr0 = 0; vr1 = R - 1; fitTime = true; imgDirty = true; }
    void clampView() {
        const double span = juce::jlimit(8.0, T(), vt1 - vt0); vt0 = juce::jlimit(0.0, T() - span, vt0); vt1 = vt0 + span;
        const double rs = juce::jlimit(24.0, (double) R - 1, vr1 - vr0); vr0 = juce::jlimit(0.0, (double) R - 1 - rs, vr0); vr1 = vr0 + rs;
        fitTime = vt0 <= 0.0 && vt1 >= T() - 0.5; imgDirty = true;
    }
    double beatFrames() const { return 60.0 / juce::jmax(20.0, proc.tempo()) * SR / HOP; }
    int snapT(double t) const { if (gridDiv <= 0) return (int) std::lround(t); const double st = beatFrames() * 4.0 / gridDiv; return (int) std::lround(std::round(t / st) * st); }
    void refreshImage(bool force = false) {
        const auto now = juce::Time::getMillisecondCounter();
        if (fitTime && std::abs(vt1 - T()) > 0.5) { vt0 = 0; vt1 = T(); imgDirty = true; }
        const auto a = area().toNearestInt();
        // during a stroke only the columns that changed are drawn again (a full redraw follows when the stroke ends)
        if (!imgDirty && imgT1 >= imgT0 && img.isValid() && img.getWidth() == a.getWidth() && img.getHeight() == a.getHeight() && (force || now - lastImg >= 30)) {
            const int x0 = juce::jlimit(0, a.getWidth(), (int) std::floor(xOf(imgT0) - a.getX()) - 1), x1 = juce::jlimit(0, a.getWidth(), (int) std::ceil(xOf(imgT1 + 1) - a.getX()) + 1);
            if (x1 > x0) {
                const double t0 = vt0 + (vt1 - vt0) * x0 / a.getWidth(), t1 = vt0 + (vt1 - vt0) * x1 / a.getWidth();
                auto part = canvasImage(L.canvas, x1 - x0, a.getHeight(), 72.0f, t0, t1, vr0, vr1);
                juce::Graphics g(img); g.drawImageAt(part, x0, 0);
            }
            imgT0 = 1 << 30; imgT1 = -1; blank = false; lastImg = now; repaint(); return;
        }
        if (!force && (!imgDirty || now - lastImg < 60)) return;
        img = canvasImage(L.canvas, std::max(1, a.getWidth()), std::max(1, a.getHeight()), 72.0f, vt0, vt1, vr0, vr1);
        blank = L.canvas.isBlank(); imgDirty = false; lastImg = now; repaint();
    }
    void changed() { imgDirty = true; imgT0 = 1 << 30; imgT1 = -1; L.edited(); if (onEdit) onEdit(); }
    void changedBox(const Sel& s) { imgDirty = true; L.editedRange(s.t0 - 1, s.t1 + 1); if (onEdit) onEdit(); }
    void changedRange(int t0, int t1) { if (!imgDirty) { imgT0 = std::min(imgT0, t0); imgT1 = std::max(imgT1, t1); } L.editedRange(t0, t1); if (onEdit) onEdit(); }
    void backup(int t0, int t1) {
        const auto& c = L.canvas; t0 = std::max(0, t0); t1 = std::min(c.T - 1, t1);
        for (int t = t0; t <= t1; ++t) if (saved.find(t) == saved.end()) {
            Saved sv; sv.cells.assign(c.col(t), c.col(t) + K); const auto a = (ptrdiff_t) t * R;
            sv.a.assign(c.amp.begin() + a, c.amp.begin() + a + R); sv.f.assign(c.fq.begin() + a, c.fq.begin() + a + R); sv.n.assign(c.noise.begin() + a, c.noise.begin() + a + R);
            saved.emplace(t, std::move(sv));
        }
    }
    void commitStroke() {   // one undo step for the stroke: the backed-up frames (untouched frames in the range are as they are now)
        if (tmax < 0 || saved.empty()) { saved.clear(); return; }
        const auto& c = L.canvas; const int t0 = std::max(0, tmin), t1 = std::min(c.T - 1, tmax); if (t1 < t0) { saved.clear(); return; }
        std::vector<Cell> cells; std::vector<float> a, f, n; cells.reserve((size_t) (t1 - t0 + 1) * K);
        for (int t = t0; t <= t1; ++t) {
            auto it = saved.find(t); const auto o = (ptrdiff_t) t * R;
            if (it != saved.end()) { cells.insert(cells.end(), it->second.cells.begin(), it->second.cells.end()); a.insert(a.end(), it->second.a.begin(), it->second.a.end()); f.insert(f.end(), it->second.f.begin(), it->second.f.end()); n.insert(n.end(), it->second.n.begin(), it->second.n.end()); }
            else { cells.insert(cells.end(), c.col(t), c.col(t) + K); a.insert(a.end(), c.amp.begin() + o, c.amp.begin() + o + R); f.insert(f.end(), c.fq.begin() + o, c.fq.begin() + o + R); n.insert(n.end(), c.noise.begin() + o, c.noise.begin() + o + R); }
        }
        L.undo.beginFrames(t0, t1, std::move(cells), std::move(a), std::move(f), std::move(n)); saved.clear();
    }
    void paint(juce::Graphics& g) override {
        const auto a = area(); auto r = getLocalBounds().toFloat();
        g.setColour(BG); g.fillRect(r);
        if (img.isValid()) g.drawImage(img, a);
        // octave lines + labels (readable over bright spectra)
        g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
        const int step = (vr1 - vr0) < 80 ? 1 : 12;   // zoomed in on pitch: every semitone
        for (int m = 24; m <= 120; m += step) {
            const float y = yOf(hzRow(midiHz((float) m))); if (y < a.getY() + 6 || y > a.getBottom() - 4) continue;
            const bool oct = m % 12 == 0; if (!oct && step == 1 && (vr1 - vr0) > 40 && m % 12 != 7) continue;
            g.setColour(INK.withAlpha(oct ? 0.08f : 0.04f)); g.drawHorizontalLine((int) y, 62.0f, a.getRight());
            g.setColour(BG.withAlpha(0.72f)); g.fillRoundedRectangle(2.0f, y - 7.0f, 58.0f, 14.0f, 4.0f);
            g.setColour(GREY.withAlpha(0.95f)); g.drawText(noteName(m) + "  " + hzText(midiHz((float) m)), 6, (int) y - 7, 70, 14, juce::Justification::centredLeft);
        }
        paintRuler(g);
        // locks (CHAOS keeps these) - only where they matter
        if (page == 4 || tool == Lock) for (auto& lk : L.locks) {
            auto b = juce::Rectangle<float>(xOf(lk.t0), yOf(lk.r1), xOf(lk.t1) - xOf(lk.t0), yOf(lk.r0) - yOf(lk.r1)).getIntersection(a);
            if (b.isEmpty()) continue;
            g.setColour(AMBER.withAlpha(0.12f)); g.fillRect(b); g.setColour(AMBER.withAlpha(0.85f)); g.drawRect(b, 1.5f);
            g.saveState(); g.reduceClipRegion(b.toNearestInt()); g.setColour(AMBER.withAlpha(0.25f)); for (float x = b.getX() - b.getHeight(); x < b.getRight(); x += 10) g.drawLine(x, b.getBottom(), x + b.getHeight(), b.getY(), 1.0f); g.restoreState();
            g.setFont(juce::FontOptions(9.5f, juce::Font::bold)); g.drawText("LOCKED", b.reduced(4).toNearestInt(), juce::Justification::topLeft);
        }
        // split crossovers
        if (page == 3) for (int i = 0; i < L.split.bands - 1; ++i) {
            const float y = yOf(hzRow(L.split.xover[(size_t) i])); if (y < a.getY() || y > a.getBottom()) continue;
            g.setColour(bandColour(i).withAlpha(0.9f)); g.drawLine(a.getX(), y, a.getRight(), y, dragXover == i ? 2.5f : 1.5f);
            g.setColour(BG.withAlpha(0.7f)); g.fillRoundedRectangle(a.getRight() - 74, y - 15, 70, 13, 3);
            g.setColour(bandColour(i)); g.setFont(juce::FontOptions(10.0f, juce::Font::bold)); g.drawText(juce::String((int) L.split.xover[(size_t) i]) + " Hz", (int) a.getRight() - 90, (int) y - 15, 84, 13, juce::Justification::centredRight);
        }
        // selection (with the pending SHIFT / STRETCH preview)
        if (haveSel || makingSel) {
            auto b = juce::Rectangle<float>(xOf(sel.t0), yOf(sel.r1), xOf(sel.t1) - xOf(sel.t0), yOf(sel.r0) - yOf(sel.r1));
            g.setColour(CYAN.withAlpha(0.08f)); g.fillRect(b); g.setColour(CYAN); g.drawRect(b, 1.4f);
            g.setFont(juce::FontOptions(10.0f, juce::Font::bold)); g.drawText(juce::String((sel.t1 - sel.t0) * HOP / SR, 2) + " s", b.toNearestInt().withHeight(14).translated(4, 2), juce::Justification::left);
            if (draggingSel) { g.setColour(PINK.withAlpha(0.9f)); g.drawRect(b.translated(0, dragDy), 1.4f); g.setFont(juce::FontOptions(12.0f, juce::Font::bold)); g.drawText(pendingText(), b.translated(0, dragDy).toNearestInt().withHeight(18).translated(4, -18), juce::Justification::centredLeft); }
        }
        // playhead
        const double pp = L.playPos.load(); if (pp >= 0) { const float x = xOf(pp * T()); if (x >= a.getX() && x <= a.getRight()) { g.setColour(GREEN.withAlpha(0.9f)); g.drawVerticalLine((int) x, a.getY(), a.getBottom()); } }
        // brush cursor
        if (hover.x >= 0 && a.contains(hover) && (tool == Paint || tool == Erase || tool == Smear)) {
            const float rr = L.brush.size * a.getHeight() / (float) (vr1 - vr0);
            g.setColour((tool == Erase ? RED : (tool == Smear ? CYAN : INK)).withAlpha(0.8f)); g.drawEllipse(hover.x - rr, hover.y - (snap && tool == Paint ? 1.5f : rr), rr * 2, snap && tool == Paint ? 3.0f : rr * 2, 1.2f);
            if (snap && tool == Paint) { const float m = snapPitch(hzMidi((float) rowHz(rOf(hover.y))), proc.model.root, proc.model.scale); g.setFont(juce::FontOptions(11.0f, juce::Font::bold)); g.drawText(noteName((int) m), (int) hover.x + 8, (int) hover.y - 16, 50, 14, juce::Justification::left); }
        }
        if (L.canvas.T > 0 && blank) {
            g.setColour(GREY.withAlpha(0.8f)); g.setFont(juce::FontOptions(15.0f, juce::Font::bold));
            g.drawFittedText("Paint with light: up = pitch, across = time, brighter = louder.\nDrop a sound or a picture here, or press GENERATE in CHAOS.", a.toNearestInt().reduced(60), juce::Justification::centred, 3);
        }
        // scrollbars (only when zoomed)
        g.setColour(CARD2); g.fillRect(juce::Rectangle<float>(0, a.getBottom(), a.getRight(), (float) kBar)); g.fillRect(juce::Rectangle<float>(a.getRight(), (float) kRuler, (float) kBar, a.getHeight()));
        g.setColour(PURPLE.withAlpha(fitTime ? 0.25f : 0.7f)); g.fillRoundedRectangle(timeThumb(), 3);
        g.setColour(PURPLE.withAlpha(vr1 - vr0 >= R - 1.5 ? 0.25f : 0.7f)); g.fillRoundedRectangle(pitchThumb(), 3);
        g.setColour(PURPLE.withAlpha(0.45f)); g.drawRect(r, 1.0f);
    }
    juce::Rectangle<float> timeThumb() const { const auto a = area(); const float x0 = a.getX() + (float) (vt0 / T()) * a.getWidth(), x1 = a.getX() + (float) (vt1 / T()) * a.getWidth(); return { x0, a.getBottom() + 2, std::max(8.0f, x1 - x0), (float) kBar - 4 }; }
    juce::Rectangle<float> pitchThumb() const { const auto a = area(); const float y0 = a.getY() + (float) ((R - 1 - vr1) / (R - 1)) * a.getHeight(), y1 = a.getY() + (float) ((R - 1 - vr0) / (R - 1)) * a.getHeight(); return { a.getRight() + 2, y0, (float) kBar - 4, std::max(8.0f, y1 - y0) }; }
    void paintRuler(juce::Graphics& g) {
        const auto a = area(); g.setColour(CARD2); g.fillRect(0.0f, 0.0f, (float) getWidth(), (float) kRuler);
        const double bf = beatFrames(), bar = bf * 4.0; const double pxPerBeat = a.getWidth() / (vt1 - vt0) * bf;
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        // grid ticks, beats, bars (bars are numbered)
        const double sub = gridDiv > 0 ? bf * 4.0 / gridDiv : bf / 4.0; const bool showSub = a.getWidth() / (vt1 - vt0) * sub >= 6;
        for (double t = std::floor(vt0 / sub) * sub; t <= vt1; t += sub) {
            const float x = xOf(t); if (x < a.getX() || x > a.getRight()) continue;
            const double beatIdx = t / bf; const bool isBeat = std::abs(beatIdx - std::round(beatIdx)) < 1e-6, isBar = isBeat && ((long long) std::llround(beatIdx)) % 4 == 0;
            if (!isBeat && !showSub) continue;
            if (isBeat && !isBar && pxPerBeat < 5) continue;
            g.setColour(isBar ? INK.withAlpha(0.8f) : (isBeat ? GREY.withAlpha(0.6f) : DIM)); g.drawVerticalLine((int) x, isBar ? 2.0f : (isBeat ? 8.0f : 12.0f), (float) kRuler);
            if (isBar && pxPerBeat * 4 >= 26) g.drawText(juce::String((long long) std::llround(beatIdx / 4) + 1), (int) x + 3, 1, 30, 12, juce::Justification::left);
            if (isBar || (isBeat && gridDiv > 0)) { g.setColour(INK.withAlpha(isBar ? 0.08f : 0.035f)); g.drawVerticalLine((int) x, a.getY(), a.getBottom()); }
        }
        (void) bar;
        // seconds along the bottom of the canvas
        const double secs = (vt1 - vt0) * HOP / SR; const double stp = secs > 16 ? 4 : (secs > 6 ? 1 : (secs > 1.5 ? 0.5 : 0.1));
        g.setColour(GREY.withAlpha(0.7f)); g.setFont(juce::FontOptions(9.5f));
        for (double s = std::ceil(vt0 * HOP / SR / stp) * stp; s * SR / HOP < vt1; s += stp) { const float x = xOf(s * SR / HOP); if (x < a.getX() + 20) continue; g.drawText(juce::String(s, stp < 1 ? (stp < 0.5 ? 1 : 1) : 0) + "s", (int) x - 20, (int) a.getBottom() - 13, 40, 12, juce::Justification::centred); }
        // right end of the ruler: the canvas length, the tempo, and a small activity bar while the worker renders (nothing else changes colour)
        g.setColour(CARD2); g.fillRect(getWidth() - 300, 0, 300 - kBar, kRuler);
        g.setColour(GREY); g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
        g.drawText(juce::String(L.canvas.seconds(), 2) + " s  -  " + juce::String(juce::roundToInt(proc.tempo())) + " BPM  -  hold to scrub", getWidth() - 236, 2, 222, 13, juce::Justification::right);
        if (L.busy.load()) { const float t = (float) std::fmod(juce::Time::getMillisecondCounter() / 900.0, 1.0); auto bar = juce::Rectangle<float>((float) getWidth() - 296, 6, 52, 5);
            g.setColour(sp::pal().track); g.fillRoundedRectangle(bar, 2.5f); g.setColour(GREEN); g.fillRoundedRectangle(bar.withWidth(16).withX(bar.getX() + (bar.getWidth() - 16) * t), 2.5f); }
    }
    juce::String pendingText() const {
        const float rows = -dragDy / area().getHeight() * (float) (vr1 - vr0);
        if (tool == Shift) { const double st = shiftAmount(rows); return (st >= 0 ? "+" : "") + juce::String(st, fine ? 2 : 0) + " semitones" + (fine ? " (fine)" : "  (hold ALT for cents)"); }
        return "stretch x" + juce::String(std::exp2(rows / 60.0f), 2);
    }
    double shiftAmount(float rows) const { const double st = rows / 3.0; return fine ? std::round(st * 100.0) / 100.0 : std::round(st); }
    Sel rectSel(juce::Point<float> a, juce::Point<float> b) const {
        Sel s; s.t0 = juce::jlimit(0, L.canvas.T, snapT(tOf(std::min(a.x, b.x)))); s.t1 = juce::jlimit(0, L.canvas.T, snapT(tOf(std::max(a.x, b.x))));
        if (s.t1 <= s.t0) s.t1 = std::min(L.canvas.T, s.t0 + 1);
        s.r0 = juce::jlimit(0.0f, (float) R - 1, rOf(std::max(a.y, b.y))); s.r1 = juce::jlimit(0.0f, (float) R - 1, rOf(std::min(a.y, b.y))); return s;
    }
    void brushAt(juce::Point<float> p) {
        Brush b = L.brush; b.timeScale = timeScale();
        const float t = tOf(p.x), r = rOf(p.y);
        const auto useTool = rightErase ? Erase : tool;
        { const float rt0 = std::max(0.5f, b.size * b.timeScale); backup((int) std::floor(t - rt0) - 1, (int) std::ceil(t + rt0) + 1); }
        if (useTool == Paint) { double hz = 0; if (snap) hz = midiHz(snapPitch(hzMidi((float) rowHz(r)), proc.model.root, proc.model.scale)); paintAt(L.canvas, t, r, b, hz); }
        else if (useTool == Erase) eraseAt(L.canvas, t, r, b); else smearAt(L.canvas, t, r, b);
        const float rt = b.size * b.timeScale + 2; tmin = std::min(tmin, (int) std::floor(t - rt)); tmax = std::max(tmax, (int) std::ceil(t + rt));
    }
    double sampleAt(float x) const { return juce::jmax(0.0, frameCentre((int) std::lround(tOf(x)))); }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override {
        const auto a = area();
        if (std::abs(w.deltaX) > std::abs(w.deltaY)) { const double d = -w.deltaX * (vt1 - vt0) * 0.5; vt0 += d; vt1 += d; clampView(); refreshImage(true); return; }
        const double f = std::exp(-w.deltaY * 1.4);
        if (e.mods.isAltDown()) { const double d = w.deltaY * (vr1 - vr0) * 0.4; vr0 += d; vr1 += d; }
        else if (e.mods.isShiftDown()) { const double at = rOf(juce::jlimit(a.getY(), a.getBottom(), e.position.y)); vr0 = at - (at - vr0) * f; vr1 = at + (vr1 - at) * f; }
        else { const double at = tOf(juce::jlimit(a.getX(), a.getRight(), e.position.x)); vt0 = at - (at - vt0) * f; vt1 = at + (vt1 - at) * f; }
        clampView(); refreshImage(true);
    }
    void mouseMove(const juce::MouseEvent& e) override { hover = e.position; repaint(); }
    void mouseExit(const juce::MouseEvent&) override { hover = { -1, -1 }; repaint(); }
    void mouseDoubleClick(const juce::MouseEvent& e) override { if (e.position.y < kRuler || e.position.y > area().getBottom()) { fit(); refreshImage(true); if (say) say("Zoomed to fit."); } }
    void mouseDown(const juce::MouseEvent& e) override {
        hover = e.position; const auto a = area(); fine = e.mods.isAltDown();
        if (e.position.y < kRuler) { L.scrubPos = sampleAt(e.position.x); dragBar = 3; return; }                                 // scrub
        if (e.position.y > a.getBottom()) { dragBar = 1; barGrab = e.position; barT0 = vt0; return; }                            // time scrollbar
        if (e.position.x > a.getRight()) { dragBar = 2; barGrab = e.position; barR0 = vr0; return; }                             // pitch scrollbar
        dragBar = 0;
        if (page == 3) for (int i = 0; i < L.split.bands - 1; ++i) if (std::abs(e.position.y - yOf(hzRow(L.split.xover[(size_t) i]))) < 6) { L.undo.beginSide(); dragXover = i; return; }
        if (tool == Lock && e.mods.isPopupMenu()) { const float t = tOf(e.position.x), r = rOf(e.position.y);
            for (size_t i = 0; i < L.locks.size(); ++i) { auto& s = L.locks[i]; if (t >= s.t0 && t < s.t1 && r >= s.r0 && r <= s.r1) { L.undo.beginSide(); L.locks.erase(L.locks.begin() + (ptrdiff_t) i); L.sideEdited(); repaint(); if (say) say("Lock removed."); return; } } return; }
        if (tool == Paint || tool == Erase || tool == Smear) {
            rightErase = e.mods.isPopupMenu(); saved.clear(); tmin = 1 << 30; tmax = -1; stroking = true; last = e.position; brushAt(e.position); changedRange(tmin, tmax); return;
        }
        if ((tool == Shift || tool == Stretch) && haveSel) {
            auto b = juce::Rectangle<float>(xOf(sel.t0), yOf(sel.r1), xOf(sel.t1) - xOf(sel.t0), yOf(sel.r0) - yOf(sel.r1));
            if (b.contains(e.position)) { draggingSel = true; dragDy = 0; return; }
        }
        makingSel = true; selStart = e.position; sel = rectSel(e.position, e.position);
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        hover = e.position; fine = e.mods.isAltDown(); const auto a = area();
        if (dragBar == 3) { L.scrubPos = sampleAt(juce::jlimit(a.getX(), a.getRight(), e.position.x)); return; }
        if (dragBar == 1) { const double d = (e.position.x - barGrab.x) / a.getWidth() * T(); vt1 += barT0 + d - vt0; vt0 = barT0 + d; clampView(); refreshImage(true); return; }
        if (dragBar == 2) { const double d = -(e.position.y - barGrab.y) / a.getHeight() * (R - 1); const double s = vr1 - vr0; vr0 = barR0 + d; vr1 = vr0 + s; clampView(); refreshImage(true); return; }
        if (dragXover >= 0) {
            auto& x = L.split.xover; const float lo = dragXover > 0 ? x[(size_t) dragXover - 1] * 1.19f : 40.0f, hi = dragXover < L.split.bands - 2 ? x[(size_t) dragXover + 1] / 1.19f : 18000.0f;
            x[(size_t) dragXover] = juce::jlimit(lo, hi, (float) rowHz(rOf(e.position.y))); repaint(); return;
        }
        if (stroking) {
            const float dist = e.position.getDistanceFrom(last), step = std::max(1.5f, L.brush.size * a.getHeight() / (float) (vr1 - vr0) * 0.4f);
            const int n = std::max(1, (int) (dist / step)); for (int i = 1; i <= n; ++i) brushAt(last + (e.position - last) * ((float) i / n));
            last = e.position; changedRange(tmin, tmax); return;
        }
        if (draggingSel) { dragDy = e.position.y - e.mouseDownPosition.y; repaint(); return; }
        if (makingSel) { sel = rectSel(selStart, e.position); repaint(); }
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (dragBar == 3) { L.scrubPos = -1.0; dragBar = 0; return; }
        if (dragBar != 0) { dragBar = 0; return; }
        if (dragXover >= 0) { dragXover = -1; L.sideEdited(); L.requestSplit(); if (say) say("Crossover moved: bands re-split."); return; }
        if (stroking) { stroking = false; commitStroke(); imgDirty = true; L.editedRange(tmin, tmax); if (onEdit) onEdit(); return; }
        if (draggingSel) {
            draggingSel = false; const float rows = -dragDy / area().getHeight() * (float) (vr1 - vr0);
            if (std::abs(rows) >= 0.05f) {
                if (tool == Shift) { const double st = shiftAmount(rows); if (st != 0) { L.undo.begin(L.canvas, sel.t0, sel.t1); const int lost = shift(L.canvas, sel, st, L.keepTransients.load()); sel.r0 = juce::jlimit(0.0f, (float) R - 1, sel.r0 + (float) st * 3.0f); sel.r1 = juce::jlimit(0.0f, (float) R - 1, sel.r1 + (float) st * 3.0f);
                    if (say) say("Shifted " + juce::String(st, fine ? 2 : 0) + " semitones." + (lost > 0 ? "  Some partials went above 23 kHz and were removed." : "")); changedBox(sel); } }
                else { L.undo.begin(L.canvas, sel.t0, sel.t1); stretchHarmonics(L.canvas, sel, std::exp2(rows / 60.0f), L.keepTransients.load()); if (say) say("Harmonics stretched x" + juce::String(std::exp2(rows / 60.0f), 2) + "."); changedBox(sel); }
            }
            dragDy = 0; repaint(); return;
        }
        if (makingSel) {
            makingSel = false; sel = rectSel(selStart, e.position);
            if (!sel.valid() || (sel.t1 - sel.t0) < 1 || e.getDistanceFromDragStart() < 3) { haveSel = false; if (onSelection) onSelection(); repaint(); return; }
            if (tool == Freeze) { L.undo.begin(L.canvas, sel.t0, sel.t1); freeze(L.canvas, sel); haveSel = false; changedBox(sel); if (say) say("Frozen: the first moment of the box now holds across it."); }
            else if (tool == Lock) { L.undo.beginSide(); L.locks.push_back(sel); L.sideEdited(); haveSel = false; if (say) say("Locked. CHAOS GENERATE keeps this box (right-click it to unlock)."); }
            else { haveSel = true; if (say) say(tool == Shift ? "Now drag the box up or down to move it in pitch (hold ALT for cents)." : (tool == Stretch ? "Now drag the box up to stretch the harmonics apart, down to squeeze them." : "Selected: HEAR plays it, LOOP SEL loops it, DEL cuts it.")); }
            if (onSelection) onSelection();
            repaint();
        }
    }
};

// =====================================================================================================
// SPLIT: one row per band (its own length 1..32, rate, gate, retrig, mute); edit on/off, chance or pitch per step
struct SplitGrid : juce::Component {
    LabHost& L; SpecProcessor& proc; std::function<void()> onChange; int dragGate = -1, paintVal = -1, dragBand = -1, dragStep = -1, mode = 0; float dragY0 = 0; int dragV0 = 0;
    SplitGrid(LabHost& l, SpecProcessor& p) : L(l), proc(p) {}
    static constexpr int kName = 56, kStepsW = 496, kLen = 556, kRate = 596, kGate = 640, kRetrig = 706, kMute = 760;
    int rowH() const { return std::max(16, (getHeight() - 4) / std::max(2, L.split.bands)); }
    bool muted(int b) const { auto* p = proc.labMuteP[b]; return p && p->load() > 0.5f; }
    float stepW(const BandSeq& s) const { return (float) kStepsW / (float) juce::jlimit(1, kSteps, s.length); }
    void paint(juce::Graphics& g) override {
        const int nb = L.split.bands, rh = rowH();
        for (int b = 0; b < nb; ++b) {
            auto& s = L.split.seq[(size_t) b]; const int y = b * rh; const auto c = bandColour(b); const bool mu = muted(b) || s.mute; const float sw = stepW(s);
            g.setColour(c.withAlpha(0.9f)); g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
            const float lo = b == 0 ? 0 : L.split.xover[(size_t) b - 1], hi = b == nb - 1 ? 24000 : L.split.xover[(size_t) b];
            g.drawText(b == 0 ? "< " + hzText(hi) : (b == nb - 1 ? "> " + hzText(lo) : hzText(lo) + "-" + hzText(hi)), 0, y, kName - 4, rh - 2, juce::Justification::centredLeft);
            for (int k = 0; k < s.length; ++k) {
                auto r = juce::Rectangle<float>((float) kName + k * sw + 1, (float) y + 2, sw - 2.5f, (float) rh - 5);
                const bool on = s.steps[(size_t) k] != 0;
                g.setColour(on ? c.withAlpha(mu ? 0.25f : 0.85f) : (k % 4 == 0 ? CARD2.brighter(0.15f) : CARD2)); g.fillRoundedRectangle(r, 3);
                if (on && mode == 1) { g.setColour(juce::Colours::black.withAlpha(0.45f)); g.fillRect(r.withHeight(r.getHeight() * (1.0f - s.prob[(size_t) k] / 100.0f))); }
                else if (on && s.prob[(size_t) k] < 100) { g.setColour(juce::Colours::black.withAlpha(0.5f)); g.fillEllipse(r.getRight() - 6, r.getY() + 2, 4, 4); }
                if (on && s.gate < 0.99f && mode == 0) { g.setColour(juce::Colours::black.withAlpha(0.3f)); g.fillRect(r.withTrimmedLeft(r.getWidth() * s.gate)); }
                if (on && (mode == 2 || s.pitch[(size_t) k] != 0) && sw >= 13) { g.setColour(juce::Colours::black.withAlpha(0.85f)); g.setFont(juce::FontOptions(9.0f, juce::Font::bold)); const int pv = s.pitch[(size_t) k]; g.drawText((pv > 0 ? "+" : "") + juce::String(pv), r.toNearestInt(), juce::Justification::centred); }
            }
            auto box = [&](int bx, int bw, const juce::String& t, bool hot) { auto r = juce::Rectangle<float>((float) bx, (float) y + 2, (float) bw, (float) rh - 5);
                g.setColour(hot ? c.withAlpha(0.8f) : CARD2); g.fillRoundedRectangle(r, 5); g.setColour(hot ? juce::Colours::black : INK); g.setFont(juce::FontOptions(10.0f, juce::Font::bold)); g.drawText(t, r, juce::Justification::centred); };
            box(kLen, 36, juce::String(s.length), false); box(kRate, 40, "1/" + juce::String(s.rateDiv), false);
            auto gr = juce::Rectangle<float>((float) kGate, (float) y + 2, 62, (float) rh - 5); g.setColour(CARD2); g.fillRoundedRectangle(gr, 5); g.setColour(c.withAlpha(0.6f)); g.fillRoundedRectangle(gr.withWidth(62 * s.gate), 5);
            g.setColour(INK); g.drawText("GATE " + juce::String((int) (s.gate * 100)), gr, juce::Justification::centred);
            box(kRetrig, 50, "RETRIG", s.retrig); box(kMute, 44, "MUTE", mu);
        }
        g.setColour(DIM); g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        if (nb * rh + 14 <= getHeight()) g.drawText("STEPS                                                                                                         LEN    RATE", kName, nb * rh, 600, 12, juce::Justification::left);
    }
    void mouseDown(const juce::MouseEvent& e) override {
        const int rh = rowH(), b = (int) e.position.y / rh; if (b < 0 || b >= L.split.bands) return; auto& s = L.split.seq[(size_t) b]; const int x = (int) e.position.x;
        if (x >= kMute && x < kMute + 44) {   // band mutes are host parameters (automatable)
            proc.setLabParam(("labMute" + juce::String(b + 1)).toRawUTF8(), muted(b) ? 0.0f : 1.0f); s.mute = false; repaint(); if (onChange) onChange(); return; }
        L.undo.beginSide();
        if (x >= kName && x < kName + kStepsW) {
            const int k = juce::jlimit(0, s.length - 1, (int) ((x - kName) / stepW(s)));
            if (mode == 0) { paintVal = s.steps[(size_t) k] ? 0 : 1; s.steps[(size_t) k] = (uint8_t) paintVal; }
            else { dragBand = b; dragStep = k; dragY0 = e.position.y; if (mode == 1 && !s.steps[(size_t) k]) s.steps[(size_t) k] = 1;
                   dragFine = e.mods.isShiftDown(); if (!dragFine) setStepAbs(s, k, b, e.position.y);   // the bar value under the mouse (Shift: fine, from where it is)
                   dragV0 = mode == 1 ? s.prob[(size_t) k] : s.pitch[(size_t) k]; dragF0 = (float) dragV0; }
        }
        else if (x >= kLen && x < kLen + 36) { dragBand = b; dragStep = -2; dragY0 = e.position.y; dragV0 = s.length; s.length = juce::jlimit(1, kSteps, s.length + (e.mods.isPopupMenu() ? -1 : 1)); }
        else if (x >= kRate && x < kRate + 40) { static const int rates[] = { 4, 8, 16, 32, 64 }; int i = 0; while (i < 5 && rates[i] != s.rateDiv) ++i; s.rateDiv = rates[(i + (e.mods.isPopupMenu() ? 4 : 1)) % 5]; }
        else if (x >= kGate && x < kGate + 62) { dragGate = b; dragFine = e.mods.isShiftDown(); dragY0 = e.position.x; if (!dragFine) s.gate = juce::jlimit(0.05f, 1.0f, (x - kGate) / 62.0f); dragF0 = s.gate; }
        else if (x >= kRetrig && x < kRetrig + 50) s.retrig = !s.retrig;
        repaint(); if (onChange) onChange();
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        const int rh = rowH(), x = (int) e.position.x;
        if (dragGate >= 0) { auto& s = L.split.seq[(size_t) dragGate];
            if (e.mods.isShiftDown() != dragFine) { dragFine = e.mods.isShiftDown(); dragY0 = e.position.x; dragF0 = s.gate; }
            s.gate = juce::jlimit(0.05f, 1.0f, dragFine ? dragF0 + (e.position.x - dragY0) / 620.0f : (x - kGate) / 62.0f); repaint(); if (onChange) onChange(); return; }
        if (dragBand >= 0) {
            auto& s = L.split.seq[(size_t) dragBand]; const float dy = dragY0 - e.position.y;
            if (dragStep == -2) s.length = juce::jlimit(1, kSteps, dragV0 + (int) std::lround(dy / 8.0f));
            else {
                if (e.mods.isShiftDown() != dragFine) { dragFine = e.mods.isShiftDown(); dragY0 = e.position.y; dragF0 = (float) (mode == 1 ? s.prob[(size_t) dragStep] : s.pitch[(size_t) dragStep]); }
                if (!dragFine) setStepAbs(s, dragStep, dragBand, e.position.y);   // scrub: follows the mouse
                else { const float d = (dragY0 - e.position.y) * (mode == 1 ? 0.15f : 0.025f);   // fine: 1/10 of the bar's travel
                       if (mode == 1) s.prob[(size_t) dragStep] = (uint8_t) juce::jlimit(0, 100, (int) std::lround(dragF0 + d)); else s.pitch[(size_t) dragStep] = (int8_t) juce::jlimit(-12, 12, (int) std::lround(dragF0 + d)); }
            }
            repaint(); if (onChange) onChange(); return;
        }
        const int b = (int) e.position.y / rh;
        if (paintVal >= 0 && b >= 0 && b < L.split.bands && x >= kName && x < kName + kStepsW) { auto& s = L.split.seq[(size_t) b]; s.steps[(size_t) juce::jlimit(0, s.length - 1, (int) ((x - kName) / stepW(s)))] = (uint8_t) paintVal; repaint(); if (onChange) onChange(); }
    }
    void mouseUp(const juce::MouseEvent&) override { dragGate = -1; paintVal = -1; dragBand = -1; dragStep = -1; L.sideEdited(); }
    // the step bar's value at height y (PROB 0..100 %, PITCH -12..+12): top of the row = max
    void setStepAbs(BandSeq& s, int k, int b, float y) {
        const int rh = rowH(); const float f = juce::jlimit(0.0f, 1.0f, 1.0f - (y - (float) (b * rh + 2)) / (float) juce::jmax(1, rh - 5));
        if (mode == 1) s.prob[(size_t) k] = (uint8_t) juce::roundToInt(f * 100.0f); else if (mode == 2) s.pitch[(size_t) k] = (int8_t) juce::roundToInt(-12.0f + f * 24.0f);
    }
    struct Hit { int band = -1, step = -1, what = 0; };   // what: 1 step bar, 2 gate, 3 length
    Hit hit(juce::Point<float> p) const { Hit h; const int rh = rowH(), b = (int) p.y / rh; if (b < 0 || b >= L.split.bands) return h; const int x = (int) p.x; h.band = b; const auto& s = L.split.seq[(size_t) b];
        if (x >= kName && x < kName + kStepsW) { h.what = 1; h.step = juce::jlimit(0, s.length - 1, (int) ((x - kName) / stepW(s))); }
        else if (x >= kGate && x < kGate + 62) h.what = 2; else if (x >= kLen && x < kLen + 36) h.what = 3; return h; }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override {   // one step per wheel notch
        const auto h = hit(e.position); const float d = std::abs(w.deltaX) > std::abs(w.deltaY) ? w.deltaX : w.deltaY; if (h.band < 0 || d == 0 || h.what == 0) return;
        const int dir = (d > 0) != w.isReversed ? 1 : -1; auto& s = L.split.seq[(size_t) h.band];
        if (h.what == 1 && mode == 0) return;
        L.undo.beginSide();
        if (h.what == 1 && mode == 1) s.prob[(size_t) h.step] = (uint8_t) juce::jlimit(0, 100, (int) s.prob[(size_t) h.step] + dir);
        else if (h.what == 1 && mode == 2) s.pitch[(size_t) h.step] = (int8_t) juce::jlimit(-12, 12, (int) s.pitch[(size_t) h.step] + dir);
        else if (h.what == 2) s.gate = juce::jlimit(0.05f, 1.0f, s.gate + 0.01f * (float) dir);
        else if (h.what == 3) s.length = juce::jlimit(1, kSteps, s.length + dir);
        L.sideEdited(); repaint(); if (onChange) onChange();
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override {   // back to the default (PROB 100 %, PITCH 0, GATE 100 %)
        const auto h = hit(e.position); if (h.band < 0 || (h.what == 1 && mode == 0) || h.what == 0 || h.what == 3) return; auto& s = L.split.seq[(size_t) h.band];
        L.undo.beginSide();
        if (h.what == 1 && mode == 1) s.prob[(size_t) h.step] = 100; else if (h.what == 1 && mode == 2) s.pitch[(size_t) h.step] = 0; else if (h.what == 2) s.gate = 1.0f;
        L.sideEdited(); repaint(); if (onChange) onChange();
    }
    bool dragFine = false; float dragF0 = 0;
};

// MORPH LANE: 8 bars of morph amounts
struct Lane : juce::Component {
    LabHost& L; explicit Lane(LabHost& l) : L(l) {}
    void paint(juce::Graphics& g) override {
        const float w = getWidth() / 8.0f;
        for (int i = 0; i < 8; ++i) { auto r = juce::Rectangle<float>(i * w + 2, 0, w - 4, (float) getHeight()); g.setColour(CARD2); g.fillRoundedRectangle(r, 4);
            const float v = L.morphLane[(size_t) i].load(); g.setColour(PINK.interpolatedWith(CYAN, v).withAlpha(L.laneOn.load() ? 0.9f : 0.35f)); g.fillRoundedRectangle(r.withTop(r.getBottom() - r.getHeight() * v), 4);
            g.setColour(INK.withAlpha(0.8f)); g.setFont(juce::FontOptions(9.5f, juce::Font::bold)); g.drawText(juce::String(i + 1), r.withHeight(14), juce::Justification::centred); }
    }
    // bars: click = the value under the mouse, drag scrubs (across bars too), Shift = fine, wheel = 1 %, double-click = the default ramp
    int barAt(float x) const { return juce::jlimit(0, 7, (int) (x / (getWidth() / 8.0f))); }
    void set(const juce::MouseEvent& e) {
        if (e.mods.isShiftDown()) { if (!fine) { fine = true; fineBar = barAt(e.position.x); y0 = e.position.y; v0 = L.morphLane[(size_t) fineBar].load(); }
            L.morphLane[(size_t) fineBar].store(juce::jlimit(0.0f, 1.0f, v0 + (y0 - e.position.y) / (10.0f * getHeight()))); repaint(); return; }
        fine = false; L.morphLane[(size_t) barAt(e.position.x)].store(juce::jlimit(0.0f, 1.0f, 1.0f - e.position.y / getHeight())); repaint(); }
    void mouseDown(const juce::MouseEvent& e) override { L.undo.beginSide(); fine = false; set(e); }
    void mouseDrag(const juce::MouseEvent& e) override { set(e); }
    void mouseUp(const juce::MouseEvent&) override { L.sideEdited(); L.requestLane(); }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override {
        const float d = std::abs(w.deltaX) > std::abs(w.deltaY) ? w.deltaX : w.deltaY; if (d == 0) return; const int i = barAt(e.position.x);
        L.undo.beginSide(); L.morphLane[(size_t) i].store(juce::jlimit(0.0f, 1.0f, L.morphLane[(size_t) i].load() + ((d > 0) != w.isReversed ? 0.01f : -0.01f))); L.sideEdited(); L.requestLane(); repaint(); }
    void mouseDoubleClick(const juce::MouseEvent& e) override { static const float def[] = { 0, 0.15f, 0.3f, 0.45f, 0.6f, 0.75f, 0.9f, 1.0f }; const int i = barAt(e.position.x);
        L.undo.beginSide(); L.morphLane[(size_t) i].store(def[i]); L.sideEdited(); L.requestLane(); repaint(); }
    bool fine = false; int fineBar = 0; float y0 = 0, v0 = 0;
};

// GHOST thumbnails: one spectrogram per variation with its stage (click = hear it)
struct GhostStrip : juce::Component {
    LabHost& L; std::vector<juce::Image> thumbs; std::vector<juce::String> stages; int version = -1; explicit GhostStrip(LabHost& l) : L(l) {}
    void rebuild() {
        auto b = L.latestBank(); if (!b || b->version == version) return; version = b->version; thumbs = b->ghostThumbs; stages = b->ghostStages; repaint();
    }
    void paint(juce::Graphics& g) override {
        if (thumbs.empty()) { g.setColour(GREY); g.setFont(juce::FontOptions(12.0f)); g.drawFittedText("Press GENERATE: every hit becomes a new ghost of the sound.", getLocalBounds(), juce::Justification::centred, 2); return; }
        const int n = (int) thumbs.size(); const float w = (float) getWidth() / n; const int playing = L.playingGhost.load();
        for (int i = 0; i < n; ++i) { auto r = juce::Rectangle<float>(i * w + 2, 0, w - 4, (float) getHeight() - 26); g.drawImage(thumbs[(size_t) i], r);
            g.setColour(i == playing ? GREEN : PURPLE.withAlpha(0.5f)); g.drawRect(r, i == playing ? 2.0f : 1.0f);
            g.setColour(i == playing ? GREEN : INK); g.setFont(juce::FontOptions(10.0f, juce::Font::bold)); g.drawText(juce::String(i + 1), juce::Rectangle<float>(i * w, (float) getHeight() - 25, w, 12), juce::Justification::centred);
            if (i < (int) stages.size() && (i == 0 || stages[(size_t) i] != stages[(size_t) i - 1] || w >= 52)) { g.setColour(GREY); g.setFont(juce::FontOptions(8.5f, juce::Font::bold)); g.drawText(stages[(size_t) i], juce::Rectangle<float>(i * w - 10, (float) getHeight() - 13, w + 20, 12), juce::Justification::centred); }
        }
    }
    void mouseDown(const juce::MouseEvent& e) override { if (thumbs.empty()) return; L.audition.store(juce::jlimit(0, (int) thumbs.size() - 1, (int) (e.position.x / ((float) getWidth() / thumbs.size())))); }
};

// =====================================================================================================
struct LabPanel::Impl {
    LabPanel& owner; SpecProcessor& proc; LabHost& L;
    SpecView view; SplitGrid grid; Lane lane; GhostStrip strip;
    // the heard sound as a waveform with START / END (the Lab's PLAY range) and LOOP (= the SELECT box's time span + LOOP SEL), PAINT page
    ghosty::WaveRegion wave;
    juce::TextButton tabPads { "PAINT PADS" }, tabLoop { "PAINT LOOP" }, tabLab { "SPECTRAL LAB" }, play { "PLAY" }, hearBtn { "HEARING: CANVAS" }, keysBtn { "KEYS" }, loopBtn { "LOOP" }, shotBtn { "ONE-SHOT" };
    juce::OwnedArray<juce::TextButton> pageBtns, toolBtns;
    juce::TextButton snapBtn { "SNAP TO KEY" }, undoBtn { "UNDO" }, redoBtn { "REDO" }, clearBtn { "CLEAR" }, newBtn { "NEW" }, importBtn { "IMPORT SOUND" }, showBtn { "PNG" }, beatsBtn { "SEND TO BEATS" }, arrangeBtn { "SEND TO ARRANGE" },
                     fitBtn { "FIT" }, transBtn { "KEEP TRANSIENTS" }, autoBtn { "AUTO LEVEL" };
    juce::ComboBox keyBox, scaleBox, rootBox, lenBox, gridBox, pitchBox, inBox;
    juce::OwnedArray<Knob> brushKnobs; std::unique_ptr<Knob> levelKnob, widthKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> levelAtt, widthAtt, morphAtt;
    juce::OwnedArray<juce::Component> pageItems[6];
    juce::Label status;
    juce::Slider* morphSlider = nullptr;
    int page = 0, lastBank = -1, hearPinned = -1, lastSideRev = -1, dropZone = -1, lastClip = 0; juce::uint32 clipUntil = 0, remapAt = 0; bool remapPending = false; float lastMeter = -1;
    bool wasBusy = false;
    std::unique_ptr<juce::FileChooser> chooser;
    DragOut wavOut { "DRAG WAV OUT", GREEN, [this] { if (!proc.allowExport(&wavOut, "spec.lab.wav-drag")) { say(kDemoExport); return juce::StringArray(); } juce::String why; auto f = L.dragFile(proc.tempo(), why); if (!f.existsAsFile()) { say(why); return juce::StringArray(); } say("Dragging " + f.getFileName()); return juce::StringArray(f.getFullPathName()); } };
    juce::Component::SafePointer<LabPanel> safe { &owner };

    Impl(LabPanel& o, SpecProcessor& p) : owner(o), proc(p), L(p.lab), view(p), grid(p.lab, p), lane(p.lab), strip(p.lab) {
        auto add = [&](juce::Component& c) { owner.addAndMakeVisible(c); };
        add(view); view.setComponentID("labView"); view.say = [this](const juce::String& s) { say(s); };
        view.onEdit = [this] { refreshUndo(); }; view.onSelection = [this] { updateRegion(); syncWave(); };
        setupWave();
        for (auto* b : { &tabPads, &tabLoop, &tabLab }) { owner.addChildComponent(*b); b->setClickingTogglesState(false); }   // the editor's tab bar replaces these
        tint(tabPads, PINK); tint(tabLoop, CYAN); tint(tabLab, GREEN); tabLab.setToggleState(true, juce::dontSendNotification);
        tabPads.onClick = [this] { if (owner.onLeave) owner.onLeave(0); }; tabLoop.onClick = [this] { if (owner.onLeave) owner.onLeave(1); };
        tabPads.setTooltip("Back to PAINT PADS"); tabLoop.setTooltip("Back to PAINT LOOP"); tabLab.setTooltip("SPECTRAL LAB (you are here)");
        add(play); tint(play, GREEN); play.setTooltip("Play / stop the lab sound as a loop (SPACE). With LOOP SEL on, only the selection loops.");
        play.onClick = [this] { L.previewPlaying = !L.previewPlaying.load(); refreshTop(); };
        add(hearBtn); tint(hearBtn, AMBER); hearBtn.setTooltip("What you hear follows the page: PAINT / IMAGE / CHAOS = the canvas, GHOST = the ghosts, SPLIT = the band rhythms, DNA = the morph. Click to pin one.");
        hearBtn.onClick = [this] { hearMenu(); };
        for (auto* b : { &keysBtn, &loopBtn }) { add(*b); tint(*b, CYAN); b->setRadioGroupId(4711); b->setClickingTogglesState(false); }
        keysBtn.setTooltip("KEYS: your MIDI keys play the sound (C4 = as painted; see KEYS PITCH on the left)"); loopBtn.setTooltip("LOOP: the sound loops in time with your DAW's transport");
        keysBtn.onClick = [this] { L.playKeys = true; refreshTop(); say("KEYS: play the sound on your MIDI keyboard."); };
        loopBtn.onClick = [this] { L.playKeys = false; refreshTop(); say("LOOP: the sound loops with your DAW's transport."); };
        add(shotBtn); shotBtn.setClickingTogglesState(true); tint(shotBtn, PINK); shotBtn.setTooltip("Keys play the whole sound even after you let go");
        shotBtn.onClick = [this] { L.oneShot = shotBtn.getToggleState(); };
        const char* pages[] = { "PAINT", "IMAGE", "GHOST", "SPLIT", "CHAOS", "DNA" };
        const juce::Colour pc[] = { GREEN, CYAN, PURPLE, AMBER, PINK, BLUE };
        static const char* pageTips[] = { "PAINT: paint and edit the sound with the tools on the left (+ START / END / LOOP under the canvas)",
                                          "IMAGE: turn a picture into sound (LOAD PICTURE), then shape how it is read", "GHOST: a set of variations of the sound, one per hit - click one to hear it",
                                          "SPLIT: cut the sound into frequency bands and give every band its own rhythm", "CHAOS: generate a new sound from a few controls (GENERATE)",
                                          "DNA: morph between two sounds (A and B) with MORPH or the 8-bar lane" };
        for (int i = 0; i < 6; ++i) { auto* b = pageBtns.add(new juce::TextButton(pages[i])); tint(*b, pc[i]); b->setTooltip(pageTips[i]); b->onClick = [this, i] { owner.showPage(i); }; add(*b); }
        const char* tools[] = { "PAINT", "ERASE", "SMEAR", "FREEZE", "SHIFT", "STRETCH", "LOCK", "SELECT" };
        const char* toolTips[] = { "Paint light = sound (right-click erases)  [B]", "Erase what's under the brush  [E]", "Blur in time: drag across a sound to smear it  [S]", "Drag a box: its first moment holds across the whole box  [F]",
                                   "Drag a box, then drag it up / down to move it in pitch (ALT = cents)  [T]", "Drag a box, then drag up / down to stretch / squeeze its harmonics  [X]", "Drag boxes CHAOS GENERATE must keep (right-click one to unlock)  [L]",
                                   "Drag a box to hear it (HEAR), loop it (LOOP SEL) or cut it (DEL)  [M]" };
        for (int i = 0; i < 8; ++i) { auto* b = toolBtns.add(new juce::TextButton(tools[i])); tint(*b, GREEN); b->setTooltip(toolTips[i]); b->onClick = [this, i] { owner.selectTool(i); }; add(*b); }
        add(snapBtn); snapBtn.setClickingTogglesState(true); tint(snapBtn, AMBER); snapBtn.setTooltip("Painted lines land exactly on the notes of the key (pure tones)");
        snapBtn.onClick = [this] { view.snap = snapBtn.getToggleState(); };
        keyBox.addItemList({ "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 1); keyBox.onChange = [this] { proc.model.root = keyBox.getSelectedItemIndex(); };
        scaleBox.addItemList(sp::scaleNames(), 1); scaleBox.onChange = [this] { proc.model.scale = scaleBox.getSelectedItemIndex(); };
        for (int m = 24; m <= 96; ++m) rootBox.addItem("plays at " + noteName(m), m);
        rootBox.onChange = [this] { L.rootNote = rootBox.getSelectedId(); L.requestPreview(); }; rootBox.setTooltip("The MIDI key that plays the sound at its own pitch (set automatically for imported sounds)");
        pitchBox.addItem("SPECTRAL", 1); pitchBox.addItem("TAPE", 2);
        pitchBox.setTooltip("KEYS PITCH - SPECTRAL: other keys re-render the sound at that pitch and keep its length (the first press of a new key plays TAPE while it renders). TAPE: faster = higher, like a sampler.");
        pitchBox.onChange = [this] { L.keysPitch = pitchBox.getSelectedId() == 2 ? PitchTape : PitchSpectral; };
        lenBox.addItemList({ "1 bar", "2 bars", "4 bars", "8 bars", "4 seconds", "10 seconds" }, 1); lenBox.setSelectedItemIndex(1, juce::dontSendNotification);
        lenBox.setTooltip("Length of a NEW canvas (bars follow the tempo)");
        gridBox.addItemList({ "GRID OFF", "GRID 1/4", "GRID 1/8", "GRID 1/16", "GRID 1/32", "GRID 1/64" }, 1); gridBox.setSelectedItemIndex(0, juce::dontSendNotification);
        gridBox.setTooltip("Boxes (select, freeze, lock, cut) snap to this grid at the project tempo");
        gridBox.onChange = [this] { static const int D[] = { 0, 4, 8, 16, 32, 64 }; view.gridDiv = D[juce::jlimit(0, 5, gridBox.getSelectedItemIndex())]; view.repaint(); };
        inBox.addItemList({ "MID", "LEFT", "RIGHT", "SIDE" }, 1); inBox.setTooltip("Which part of a stereo file is imported"); inBox.onChange = [this] { L.stereoIn = inBox.getSelectedItemIndex(); };
        for (auto* c : { &keyBox, &scaleBox, &rootBox, &lenBox, &gridBox, &pitchBox, &inBox }) add(*c);
        add(fitBtn); tint(fitBtn, PURPLE); fitBtn.setTooltip("Show the whole canvas (0). Wheel = zoom time, SHIFT + wheel = zoom pitch, drag the bars to scroll.");
        fitBtn.onClick = [this] { view.fit(); view.refreshImage(true); };
        add(transBtn); transBtn.setClickingTogglesState(true); tint(transBtn, CYAN); transBtn.setTooltip("SHIFT / STRETCH / GHOST keep the snap of drums and plucks (the attack keeps its own phases)");
        transBtn.onClick = [this] { L.keepTransients = transBtn.getToggleState(); L.edited(); L.requestPreview(); if (L.output.load() == OutGhost) L.requestGhost(); };
        struct BK { const char* n; double lo, hi; float* v; const char* tip; };
        const BK bk[] = { { "SIZE", 0.5, 24, &L.brush.size, "Brush size (rows of a third of a semitone)  [ and ]" }, { "HARD", 0, 0.95, &L.brush.hardness, "Soft edge (left) or hard edge (right)" },
                          { "BRIGHT", 0.01, 1, &L.brush.level, "Brightness = loudness of the stroke" }, { "NOISE", 0, 1, &L.brush.noise, "Pure tone (left) or breath / noise (right)" } };
        for (auto& d : bk) { float* v = d.v; auto* k = brushKnobs.add(new Knob(d.n, GREEN, d.lo, d.hi, *v, [v](double x) { *v = (float) x; }, d.tip)); add(*k); }
        levelKnob = std::make_unique<Knob>("LEVEL", INK, 0, 1, 0.8, nullptr, "Volume of the lab (automatable: Lab Level)"); add(*levelKnob);
        widthKnob = std::make_unique<Knob>("WIDTH", CYAN, 0, 1, 0.35, nullptr, "Stereo width: partials spread left / right, noise goes wide, the low end stays mono (automatable: Lab Width)"); add(*widthKnob);
        levelAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, "labLevel", levelKnob->s);
        widthAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, "labWidth", widthKnob->s);
        add(autoBtn); autoBtn.setClickingTogglesState(true); tint(autoBtn, GREEN); autoBtn.setTooltip("Keeps every page at about the same loudness (-16 dBFS RMS)");
        autoBtn.onClick = [this] { L.autoLevel = autoBtn.getToggleState(); L.edited(); L.requestPreview(); };
        for (auto* b : { &undoBtn, &redoBtn, &clearBtn, &newBtn, &importBtn, &showBtn, &beatsBtn }) add(*b);
        undoBtn.setTooltip("Undo (CMD+Z): strokes, edits, split / lock / lane changes"); redoBtn.setTooltip("Redo (SHIFT+CMD+Z)");
        undoBtn.onClick = [this] { if (L.undo.undo(L.canvas)) { afterUndo(); say("Undone."); } refreshUndo(); };
        redoBtn.onClick = [this] { if (L.undo.redo(L.canvas)) { afterUndo(); say("Redone."); } refreshUndo(); };
        clearBtn.setTooltip("Empty the canvas (same length; UNDO brings it back)");
        clearBtn.onClick = [this] { L.undo.beginAll(L.canvas); L.canvas.resize(L.canvas.T); view.changed(); say("Canvas cleared (UNDO brings it back)."); };
        newBtn.setTooltip("A fresh blank canvas of the chosen length"); newBtn.onClick = [this] { L.newCanvas(newLength()); view.fit(); view.changed(); say("New canvas: " + lenBox.getText() + "."); };
        importBtn.setTooltip("Load a sound into the canvas to repaint it (WAV / AIFF / FLAC / OGG; MP3 / M4A on macOS and Windows)");
        importBtn.onClick = [this] { choose("*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.m4a;*.caf;*.aac", [this](const juce::File& f) { importSound(f); }); };
        showBtn.setTooltip("Save the canvas as a picture (Documents/Ghosty/Spectrum/Lab Images)");
        showBtn.onClick = [this] { auto f = L.exportImage(); if (f.existsAsFile()) { say("Saved " + f.getFileName() + " in Lab Images."); f.revealToUser(); } else say("Couldn't save the picture."); };
        beatsBtn.setTooltip("Save what you hear (stereo WAV) into Documents/Ghosty/Shared Sounds - Ghosty Beats lists that folder"); tint(beatsBtn, PINK);
        beatsBtn.onClick = [this] { if (!proc.allowExport(&beatsBtn, "spec.lab.send-beats")) { say(kDemoExport); return; } auto sp = safe; say("Saving to Shared Sounds...");
            L.sendToBeats(proc.tempo(), [this, sp](juce::StringArray f, juce::String err) { if (!sp) return; say(f.isEmpty() ? (err.isNotEmpty() ? err : juce::String("Nothing to save.")) : "Sent " + juce::String(f.size()) + " sound" + (f.size() > 1 ? "s" : "") + " to Shared Sounds - Ghosty Beats will find them there." + (err.isNotEmpty() ? "  (" + err + ")" : "")); }); };
        add(arrangeBtn); tint(arrangeBtn, BLUE); arrangeBtn.setTooltip("Put what you hear on the ARRANGE timeline as a clip (stereo; snapped lines come along as MIDI notes; ARRANGE can re-render it later)");
        arrangeBtn.onClick = [this] { auto sp = safe; say("Rendering for ARRANGE...");
            L.renderExport(proc.tempo(), [this, sp](std::shared_ptr<LabHost::ExportSound> e) { if (!sp || !e) return;
                const auto id = proc.arrange.receive(SpecProcessor::toPayload(*e));
                say(id != 0 ? "\"" + e->name + "\" is on the ARRANGE timeline" + (e->notes.empty() ? juce::String(".") : " (with " + juce::String((int) e->notes.size()) + " MIDI notes).") : juce::String("Nothing to send (the sound is silent).")); }); };
        add(wavOut); wavOut.setComponentID("labWavOut"); beatsBtn.setComponentID("labToBeats"); wavOut.setTooltip("Drag the sound (what you hear, high quality, stereo) into your DAW");
        status.setFont(juce::FontOptions(12.0f, juce::Font::bold)); status.setColour(juce::Label::textColourId, GREEN); status.getProperties().set("role", "status");
        status.setJustificationType(juce::Justification::centredRight); status.setMinimumHorizontalScale(1.0f); status.setInterceptsMouseClicks(false, false); add(status);
        buildPages();
        syncAll();
    }
    ~Impl() { morphAtt.reset(); levelAtt.reset(); widthAtt.reset(); }   // the attachments go before the knobs they hold (the MORPH knob lives in pageItems)
    void afterUndo() { view.changed(); L.requestPreview(); if (L.output.load() == OutSplit) L.requestSplit(); grid.repaint(); lane.repaint(); }
    double newLength() const { const int i = lenBox.getSelectedItemIndex(); const double bar = 4 * 60.0 / proc.tempo(); static const double B[] = { 1, 2, 4, 8 }; return i < 4 ? B[i] * bar : (i == 4 ? 4.0 : 10.0); }
    void say(const juce::String& s) { status.setText(s, juce::dontSendNotification); }
    void choose(const juce::String& pattern, std::function<void(const juce::File&)> done) {
        chooser = std::make_unique<juce::FileChooser>("Choose a file", juce::File::getSpecialLocation(juce::File::userHomeDirectory), pattern);
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [done](const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) done(fc.getResult()); });
    }
    void importSound(const juce::File& f) {
        say("Loading \"" + f.getFileName() + "\"..."); auto sp = safe;
        L.importAudioAsync(f, [this, sp](bool ok, const juce::String& msg) { if (!sp) return; if (ok) { view.fit(); view.changed(); syncAll(); } say(msg); });
    }
    void importPicture(const juce::File& f) { auto img = juce::ImageFileFormat::loadFrom(f); if (!img.isValid()) { say("Can't read that picture."); return; } L.setImage(img); view.fit(); view.changed(); owner.showPage(1); say("Picture -> sound: bright = loud, up = higher. Change the length / range below."); }
    // ---- the WaveRegion (START / END / LOOP): wording and gestures as in every GhostBlock sample view
    static ghosty::WaveRegion::Palette wavePalette() {
        ghosty::WaveRegion::Palette P = sp::pal().dark ? ghosty::WaveRegion::Palette() : ghosty::WaveRegion::Palette::light();
        P.bg = sp::pal().field; P.line = sp::pal().line; P.ink = sp::pal().ink; P.dim = sp::pal().grey; P.card = sp::pal().panel2; P.wave = sp::pal().cyan; P.waveOut = sp::pal().dim; return P; }
    int waveFrames() const { return juce::jmax(1, L.canvas.T); }
    void setupWave() {
        wave.setComponentID("labWave"); owner.addChildComponent(wave);
        wave.palette = [] { return wavePalette(); };
        wave.emptyText = "the sound you hear shows here";
        wave.hint = "right-click: START here   |   right-drag: LOOP   |   drag START / END   |   drag WAV out";
        wave.setTooltip("What you hear. Drag START / END: the Lab's PLAY runs between them. Right-click = START here, right-drag = LOOP (the same span as a SELECT box; "
                        "LOOP ON = LOOP SEL). Drag the WAV grip out to export the highlighted part.");
        wave.onChange = [this](const ghosty::WaveRegion::Region& r) {
            L.playStart = r.start; L.playEnd = r.end;
            if (r.loopEnd > r.loopStart) {   // the loop is the box's time span (full height): HEAR / LOOP SEL / CUT BOX all use it
                const int T = waveFrames(); view.sel.t0 = juce::jlimit(0, T - 1, (int) std::floor(r.loopStart * T)); view.sel.t1 = juce::jlimit(view.sel.t0 + 1, T, (int) std::ceil(r.loopEnd * T));
                view.sel.r0 = 0; view.sel.r1 = (float) R - 1; view.haveSel = true; updateRegion(); view.repaint(); }
            L.loopRegion = r.loopOn; if (loopSelBtn) loopSelBtn->setToggleState(r.loopOn, juce::dontSendNotification); };
        wave.onAudition = [this](bool down, float f) { const auto b = L.latestBank(); L.scrubPos = down && b && b->main ? f * (double) b->main->size() : -1.0; };
        wave.canExport = [this] { return proc.allowExport(&wave, "spec.lab.region-drag"); };
        wave.exportRegion = [this](float a, float b) { return exportWaveRegion(a, b); };
        wave.onMessage = [this](const juce::String& m) { say(m); };
    }
    void syncWave() {   // the host state -> the view (box, LOOP SEL, START / END)
        ghosty::WaveRegion::Region r; r.start = L.playStart.load(); r.end = L.playEnd.load(); r.loopOn = L.loopRegion.load();
        if (view.haveSel) { const float T = (float) waveFrames(); r.loopStart = juce::jlimit(0.0f, 1.0f, view.sel.t0 / T); r.loopEnd = juce::jlimit(0.0f, 1.0f, view.sel.t1 / T); }
        else { r.loopStart = r.loopEnd = 0.0f; r.loopOn = false; }
        wave.setRegion(r);
    }
    void refreshWavePeaks() {
        const auto b = L.latestBank(); std::vector<float> pk;
        if (b && b->main && b->main->size() > 0) { const auto& m = b->main->m; const auto& sd = b->main->s; const size_t n = m.size(), bins = 1200; pk.assign(bins, 0.0f);
            for (size_t i = 0; i < n; ++i) { const float v = std::abs(m[i]) + (i < sd.size() ? std::abs(sd[i]) : 0.0f); auto& q = pk[i * bins / n]; q = std::max(q, v); }
            const float top = *std::max_element(pk.begin(), pk.end()); if (top > 0) for (auto& q : pk) q = std::min(1.0f, q / top); }
        wave.setLengthSeconds(b && b->main ? (double) b->main->size() / SR : 0.0); wave.setPeaks(std::move(pk));
    }
    juce::File exportWaveRegion(float a, float b) {   // the high-quality render of what you hear, only a..b
        juce::String why; const auto f = L.dragFileRegion(proc.tempo(), a, b, why); if (!f.existsAsFile() && why.isNotEmpty()) say(why); return f; }
    void updateRegion() {
        if (view.haveSel) { L.regionStart = std::max(0.0, frameCentre(view.sel.t0)); L.regionEnd = std::max(0.0, frameCentre(view.sel.t1)); }
        else { L.regionStart = -1.0; L.regionEnd = -1.0; }
    }
    void boxChanged(const Sel& s) { view.imgDirty = true; if (view.haveSel) L.editedRange(s.t0 - 1, s.t1 + 1); else L.edited(); refreshUndo(); }   // a box edit: only its frames go to the worker
    Sel selOrAll() const { Sel s; s.t0 = 0; s.t1 = L.canvas.T; s.r0 = 0; s.r1 = R - 1; return view.haveSel ? view.sel : s; }
    void doShift(double st) { auto s = selOrAll(); L.undo.begin(L.canvas, s.t0, s.t1); const int lost = shift(L.canvas, s, st, L.keepTransients.load()); boxChanged(s);
        say((st >= 0 ? "Up " : "Down ") + juce::String(std::abs(st), std::abs(st - std::round(st)) > 1e-6 ? 2 : 0) + " semitones" + (view.haveSel ? " (the box)." : ".") + (lost > 0 ? "  Some partials went above 23 kHz and were removed." : "")); }
    // ---------------------------------------------------------------- pages
    Knob* knob(int pg, const juce::String& n, juce::Colour c, double lo, double hi, double def, std::function<void(double)> f, const juce::String& tip) { auto* k = new Knob(n, c, lo, hi, def, std::move(f), tip); pageItems[pg].add(k); owner.addChildComponent(k); return k; }
    juce::TextButton* button(int pg, const juce::String& n, juce::Colour c, std::function<void()> f, const juce::String& tip) { auto* b = new juce::TextButton(n); tint(*b, c); b->onClick = std::move(f); b->setTooltip(tip); pageItems[pg].add(b); owner.addChildComponent(b); return b; }
    juce::Label* label(int pg, const juce::String& t) { auto* l = new juce::Label({}, t); l->setFont(juce::FontOptions(11.5f)); l->setColour(juce::Label::textColourId, GREY); l->getProperties().set("role", "grey"); l->setMinimumHorizontalScale(1.0f); l->setJustificationType(juce::Justification::topLeft); l->setInterceptsMouseClicks(false, false); pageItems[pg].add(l); owner.addChildComponent(l); return l; }
    juce::TextEditor* entry(int pg, const juce::String& t, const juce::String& tip) { auto* e = new juce::TextEditor(); e->setText(t, false); e->setJustification(juce::Justification::centred); e->setTooltip(tip); e->setFont(juce::FontOptions(12.0f, juce::Font::bold));
        e->setColour(juce::TextEditor::backgroundColourId, sp::pal().field); e->setColour(juce::TextEditor::textColourId, INK); e->setColour(juce::TextEditor::outlineColourId, sp::pal().line); pageItems[pg].add(e); owner.addChildComponent(e); return e; }
    juce::Label *nameA = nullptr, *nameB = nullptr, *morphState = nullptr, *ghostNote = nullptr; juce::TextButton *laneBtn = nullptr, *velBtn = nullptr, *loopSelBtn = nullptr; juce::ComboBox *bandsBox = nullptr, *hitBox = nullptr;
    juce::TextButton* modeBtns[3] {}; Knob* swingKnob = nullptr;
    // the strip under the canvas: row 1 = page title (left) + status (right), row 2 = one line of help, then the controls
    static constexpr int kStripTop = 46;
    void buildPages() {
        const int x0 = kPage.getX() + 14, y0 = kPage.getY() + kStripTop;
        // ---- PAINT
        { int x = x0; auto bt = [&](const juce::String& n, juce::Colour c, std::function<void()> f, const juce::String& tip, int w, int y) { auto* b = button(0, n, c, std::move(f), tip); b->setBounds(x, y, w, 28); x += w + 6; return b; };
          bt("OCTAVE UP", CYAN, [this] { doShift(12); }, "Shift the box (or everything) up 12 semitones", 96, y0);
          bt("OCTAVE DOWN", CYAN, [this] { doShift(-12); }, "Shift the box (or everything) down 12 semitones", 106, y0);
          bt("FIFTH UP", CYAN, [this] { doShift(7); }, "Shift the box (or everything) up 7 semitones", 84, y0);
          bt("FREEZE", PURPLE, [this] { auto s = selOrAll(); L.undo.begin(L.canvas, s.t0, s.t1); freeze(L.canvas, s); boxChanged(s); say("Frozen."); }, "Hold the first moment of the box (or everything) across it", 76, y0);
          bt("LOUDER", GREEN, [this] { auto s = selOrAll(); L.undo.begin(L.canvas, s.t0, s.t1); gainRegion(L.canvas, s, 1.41f); boxChanged(s); }, "+3 dB on the box (or everything)", 70, y0);
          bt("SOFTER", GREEN, [this] { auto s = selOrAll(); L.undo.begin(L.canvas, s.t0, s.t1); gainRegion(L.canvas, s, 0.71f); boxChanged(s); }, "-3 dB on the box (or everything)", 70, y0);
          bt("CUT BOX", RED, [this] { cutBox(); }, "Erase everything inside the box (DEL)", 76, y0);
          bt("HEAR", AMBER, [this] { hearOnce(); }, "Play the box once (or the whole canvas)  [ENTER]", 64, y0);
          loopSelBtn = bt("LOOP SEL", AMBER, [this] { L.loopRegion = loopSelBtn->getToggleState(); syncWave(); if (L.loopRegion.load() && !view.haveSel) say("Draw a box with SELECT first, then PLAY loops it."); }, "PLAY loops only the box", 84, y0);
          loopSelBtn->setClickingTogglesState(true);
          x = x0; const int ty = y0 + 38;   // typed moves
          auto* sl = label(0, "SHIFT BY"); sl->setBounds(x, ty + 2, 60, 24); sl->setJustificationType(juce::Justification::centredLeft); x += 62;
          auto* se = entry(0, "+0.50", "Semitones, with cents: +0.5, -12, 7.25 ..."); se->setBounds(x, ty, 66, 28); x += 72;
          bt("APPLY", CYAN, [this, se] { const double v = juce::jlimit(-48.0, 48.0, se->getText().retainCharacters("0123456789.-").getDoubleValue()); if (v != 0) doShift(v); }, "Shift the box (or everything) by the typed amount", 62, ty); x += 18;
          auto* tl = label(0, "STRETCH x"); tl->setBounds(x, ty + 2, 70, 24); tl->setJustificationType(juce::Justification::centredLeft); x += 72;
          auto* te = entry(0, "1.10", "Harmonic stretch: 1 = none, 1.1 = 10 % wider, 0.9 = squeezed"); te->setBounds(x, ty, 60, 28); x += 66;
          bt("APPLY", CYAN, [this, te] { const double v = juce::jlimit(0.25, 4.0, te->getText().retainCharacters("0123456789.").getDoubleValue()); auto s = selOrAll(); L.undo.begin(L.canvas, s.t0, s.t1); stretchHarmonics(L.canvas, s, v, L.keepTransients.load()); boxChanged(s); say("Harmonics stretched x" + juce::String(v, 2) + "."); }, "Stretch the harmonics of the box (or everything)", 62, ty);
          auto* hint = label(0, "Drop any sound or picture onto the canvas.  Keys: B E S F T X L M tools, [ ] brush, SPACE play, ENTER hear, DEL cut, ESC clear, 0 fit."); hint->setBounds(x0, y0 + 80, 912, 18);
          wave.setBounds(x0, y0 + 100, 912, kPage.getBottom() - (y0 + 100) - 6); }
        // ---- IMAGE
        { button(1, "LOAD PICTURE", CYAN, [this] { choose("*.png;*.jpg;*.jpeg;*.gif;*.bmp", [this](const juce::File& f) { importPicture(f); }); }, "Turn a picture into sound")->setBounds(x0, y0 + 4, 130, 32);
          button(1, "SAVE PICTURE", PINK, [this] { auto f = L.exportImage(); if (f.existsAsFile()) { say("Saved " + f.getFileName()); f.revealToUser(); } }, "Export the spectrogram as a picture")->setBounds(x0, y0 + 44, 130, 32);
          int x = x0 + 150;
          auto ik = [&](const char* n, double lo, double hi, double def, std::function<void(double)> f, const char* tip) { auto* k = knob(1, n, CYAN, lo, hi, def, [this, f](double v) { f(v); L.imageMap.sanitise(); remapPending = true; }, tip);
              k->onStart = [this] { if (L.image.isValid()) L.undo.beginAll(L.canvas); }; k->onEnd = [this] { syncPageKnobs(); }; k->setBounds(x, y0, 70, 78); x += 80; };
          ik("LENGTH s", 0.5, 30, L.imageMap.seconds, [this](double v) { L.imageMap.seconds = v; }, "How long the picture plays");
          ik("LOW Hz", 30, 5000, L.imageMap.loHz, [this](double v) { L.imageMap.loHz = v; }, "Pitch of the bottom of the picture (always at least a fifth below HIGH)");
          ik("HIGH Hz", 200, 20000, L.imageMap.hiHz, [this](double v) { L.imageMap.hiHz = v; }, "Pitch of the top of the picture");
          ik("CONTRAST", 0.3, 3, L.imageMap.gamma, [this](double v) { L.imageMap.gamma = (float) v; }, "Left: grey shades sing too; right: only the brightest parts");
          x += 10;
          button(1, "FIT TO 2 BARS", AMBER, [this] { if (L.image.isValid()) L.undo.beginAll(L.canvas); L.imageMap.seconds = 8 * 60.0 / proc.tempo(); syncPageKnobs(); L.remapImage(false); view.fit(); view.changed(); }, "Make the picture exactly 2 bars long at the tempo")->setBounds(x, y0 + 4, 130, 32); }
        // ---- GHOST
        { int x = x0;
          auto gk = [&](const char* n, double lo, double hi, double def, std::function<void(double)> f, const char* tip) { knob(2, n, PURPLE, lo, hi, def, std::move(f), tip)->setBounds(x, y0 - 2, 62, 64); x += 66; };
          gk("HITS", 2, 32, L.ghost.count, [this](double v) { L.ghost.count = (int) v; }, "How many variations (one per hit / loop)");
          gk("EVOLVE", 0, 1, L.ghost.evolve, [this](double v) { L.ghost.evolve = (float) v; }, "How far the ghost travels from the sound");
          gk("MEMORY", 0, 1, L.ghost.memory, [this](double v) { L.ghost.memory = (float) v; }, "Each ghost builds on the last one (right) or on the original (left)");
          gk("DECAY", 0, 1, L.ghost.decay, [this](double v) { L.ghost.decay = (float) v; }, "Each ghost a little quieter and darker");
          gk("SEED", 1, 999, L.ghost.seed, [this](double v) { L.ghost.seed = (uint32_t) v; }, "Same seed = same ghosts"); x += 4;
          button(2, "GENERATE", PURPLE, [this] { LabHost::sanitise(L.ghost); setOutput(OutGhost); L.requestGhost(); say("Summoning " + juce::String(L.ghost.count) + " ghosts: every hit plays the next one."); }, "Make the variations")->setBounds(x, y0, 104, 28);
          button(2, "NEW SEED", PURPLE, [this] { L.ghost.seed = (uint32_t) juce::Random::getSystemRandom().nextInt({ 1, 999 }); syncPageKnobs(); setOutput(OutGhost); L.requestGhost(); }, "Roll the dice and generate")->setBounds(x, y0 + 32, 104, 28); x += 112;
          hitBox = new juce::ComboBox(); hitBox->addItem("NEXT GHOST EACH HIT", 1); for (int i = 1; i <= kMaxGhosts; ++i) hitBox->addItem("ALWAYS GHOST " + juce::String(i), i + 1);
          hitBox->setTooltip("Which ghost plays: the next one on every hit / loop, or always the same one (automatable: Lab Ghost Hit)");
          hitBox->onChange = [this] { proc.setLabParam("labGhostHit", (float) (hitBox->getSelectedId() - 1)); };
          pageItems[2].add(hitBox); owner.addChildComponent(hitBox); hitBox->setBounds(x, y0, 190, 28);
          ghostNote = label(2, "Click a ghost to hear it. SEND TO BEATS saves the whole set (one WAV per hit)."); ghostNote->setBounds(x, y0 + 32, 912 - (x - x0), 28);
          pageItems[2].add(nullptr); owner.addChildComponent(strip); strip.setBounds(x0, y0 + 66, 912, 94); }
        // ---- SPLIT
        { owner.addChildComponent(grid); grid.setComponentID("splitGrid"); grid.setBounds(x0 + 108, y0 - 2, 806, 162); grid.onChange = [this] { L.publishSplit(); if (L.output.load() != OutSplit) setOutput(OutSplit); };
          bandsBox = new juce::ComboBox(); for (int b = 2; b <= kMaxBands; ++b) bandsBox->addItem(juce::String(b) + " bands", b); pageItems[3].add(bandsBox); owner.addChildComponent(bandsBox);
          bandsBox->setBounds(x0, y0 - 2, 104, 26); bandsBox->onChange = [this] { L.undo.beginSide(); L.split.bands = bandsBox->getSelectedId(); L.sideEdited(); grid.repaint(); view.repaint(); L.requestSplit(); setOutput(OutSplit); };
          const char* modes[] = { "EDIT STEPS", "EDIT CHANCE", "EDIT PITCH" }; const char* mtips[] = { "Click / drag: steps on and off", "Click a step at a height = that chance, drag to scrub (Shift = fine, wheel = 1 %, double-click = 100 %)", "Click a step at a height = -12..+12 semitones, drag to scrub (Shift = fine, wheel = 1 semitone, double-click = 0; re-read from the step start)" };
          for (int m = 0; m < 3; ++m) { auto* b = button(3, modes[m], AMBER, [this, m] { grid.mode = m; for (int k = 0; k < 3; ++k) modeBtns[k]->setToggleState(k == m, juce::dontSendNotification); grid.repaint(); }, mtips[m]);
              b->setBounds(x0, y0 + 28 + m * 26, 104, 23); b->setComponentID("splitMode" + juce::String(m)); modeBtns[m] = b; }
          modeBtns[0]->setToggleState(true, juce::dontSendNotification);
          swingKnob = knob(3, "SWING", AMBER, 0, 0.5, L.split.swing, [this](double v) { L.split.swing = (float) v; L.publishSplit(); }, "Every second step comes late (all bands)");
          swingKnob->onStart = [this] { L.undo.beginSide(); }; swingKnob->onEnd = [this] { L.sideEdited(); }; swingKnob->setBounds(x0, y0 + 108, 58, 54);
          button(3, "DICE", AMBER, [this] { L.undo.beginSide(); L.split.seed = (uint32_t) juce::Random::getSystemRandom().nextInt({ 1, 999999 }); L.sideEdited(); say("New dice for the chance steps."); }, "New random choices for the steps below 100 %")->setBounds(x0 + 62, y0 + 122, 42, 24); }
        // ---- CHAOS
        { int x = x0; auto ck = [&](const char* n, float* v, const char* tip) { knob(4, n, PINK, 0, 1, *v, [v](double d) { *v = (float) d; }, tip)->setBounds(x, y0, 66, 76); x += 72; };
          ck("CHAOS", &L.chaos.chaos, "Order (on the grid, clean lengths) to wild (anywhere, any length, noise)");
          ck("DENSITY", &L.chaos.density, "How many events and partials");
          ck("HARMONY", &L.chaos.harmony, "Free pitches (left), notes of the key (middle), the harmonic series of the key (right)");
          ck("MOTION", &L.chaos.motion, "Glides and vibrato");
          ck("CONSTRAINT", &L.chaos.constraint, "Follow the imported sound: events where it hits, pitches where it sings");
          knob(4, "SEED", PINK, 1, 999, L.chaos.seed, [this](double v) { L.chaos.seed = (uint32_t) v; }, "Same seed = same picture")->setBounds(x, y0, 66, 76); x += 80;
          auto gen = [this] { L.chaos.root = proc.model.root; L.chaos.scale = proc.model.scale; L.chaos.bpm = proc.tempo(); L.generateChaos(); view.changed(); say("Generated (seed " + juce::String((int) L.chaos.seed) + ")" + (L.locks.empty() ? "." : ", locked boxes kept.")); };
          button(4, "GENERATE", PINK, gen, "Paint a new picture (locked boxes stay)")->setBounds(x, y0 + 4, 110, 30);
          button(4, "NEW SEED", PINK, [this, gen] { L.chaos.seed = (uint32_t) juce::Random::getSystemRandom().nextInt({ 1, 999 }); syncPageKnobs(); gen(); }, "Roll the dice and generate")->setBounds(x, y0 + 40, 110, 30); x += 120;
          button(4, "LOCK TOOL", AMBER, [this] { owner.selectTool(SpecView::Lock); say("Drag boxes on the canvas to keep them."); }, "Drag boxes on the canvas that GENERATE keeps")->setBounds(x, y0 + 4, 110, 30);
          button(4, "UNLOCK ALL", AMBER, [this] { L.undo.beginSide(); L.locks.clear(); L.sideEdited(); view.repaint(); }, "Remove every lock")->setBounds(x, y0 + 40, 110, 30); }
        // ---- DNA
        { auto load = [this](int which) { choose("*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.m4a;*.caf;*.aac", [this, which](const juce::File& f) { loadMorph(which, f); }); };
          button(5, "LOAD A", BLUE, [load] { load(0); }, "Sound A (MORPH 0)")->setBounds(x0, y0 + 2, 80, 28);
          button(5, "CANVAS > A", BLUE, [this] { say("Rendering the canvas as A..."); auto sp = safe; L.useCanvasAs(0, [this, sp] { if (sp) { syncAll(); say("The canvas is A."); } }); }, "Use the canvas sound as A")->setBounds(x0 + 84, y0 + 2, 96, 28);
          button(5, "LOAD B", BLUE, [load] { load(1); }, "Sound B (MORPH 1)")->setBounds(x0, y0 + 36, 80, 28);
          button(5, "CANVAS > B", BLUE, [this] { say("Rendering the canvas as B..."); auto sp = safe; L.useCanvasAs(1, [this, sp] { if (sp) { syncAll(); say("The canvas is B."); } }); }, "Use the canvas sound as B")->setBounds(x0 + 84, y0 + 36, 96, 28);
          nameA = label(5, "A: -"); nameA->setBounds(x0 + 186, y0 + 6, 150, 20); nameB = label(5, "B: -"); nameB->setBounds(x0 + 186, y0 + 40, 150, 20);
          auto* mk = knob(5, "MORPH", BLUE, 0, 1, 0, nullptr, "A (left) to B (right). Automatable in your DAW (Lab Morph A-B)."); mk->setBounds(x0 + 340, y0 - 4, 80, 84);
          morphSlider = &mk->s; morphAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, "labMorph", mk->s);
          mk->s.onValueChange = [this] { L.morphNow = (float) morphSlider->getValue(); };
          laneBtn = button(5, "PER BAR", BLUE, [this] { L.laneOn = laneBtn->getToggleState(); lane.repaint(); L.requestLane(); }, "Use the 8-bar lane instead of the MORPH knob (each bar rendered exactly)"); laneBtn->setClickingTogglesState(true); laneBtn->setBounds(x0 + 434, y0 + 2, 90, 28);
          velBtn = button(5, "PER NOTE", BLUE, [this] { L.velMorph = velBtn->getToggleState(); }, "Key velocity sets the morph: soft = A, hard = B"); velBtn->setClickingTogglesState(true); velBtn->setBounds(x0 + 434, y0 + 36, 90, 28);
          pageItems[5].add(nullptr); owner.addChildComponent(lane); lane.setComponentID("morphLane"); lane.setBounds(x0 + 534, y0, 260, 64);
          button(5, "MORPH > CANVAS", BLUE, [this] { std::shared_ptr<MorphPrep> mp = L.getMorphPrep(); if (!mp) { say("Load A and B first."); return; } L.undo.beginAll(L.canvas); morph(*mp, L.morphNow.load(), L.canvas); L.sourceAudio.clear(); view.fit(); view.changed(); owner.showPage(0); say("The morph is on the canvas now - paint on it."); }, "Put the morph at the knob's amount on the canvas to edit it")->setBounds(x0 + 798, y0 + 2, 120, 28);
          button(5, "HEAR MORPH", BLUE, [this] { setOutput(OutMorph); L.audition.store(-1); }, "Play the morph once at the knob's amount")->setBounds(x0 + 798, y0 + 36, 120, 28);
          morphState = label(5, ""); morphState->setBounds(x0, y0 + 80, 900, 18); }
    }
    static const char* helpText(int page) {   // one line each, under the page title (the title is never repeated here)
        static const char* H[] = {
            "Up = pitch, across = time, brighter = louder. SNAP TO KEY draws pure notes. SHIFT / STRETCH: drag a box, then drag it up or down (ALT = cents).",
            "Drop a PNG / JPG on the canvas (or LOAD). Bright pixels sing, dark ones are silent; the top of the picture is the highest pitch.",
            "Each hit becomes the next ghost of the sound: metal rings, it rises, breaks into fragments, turns to dust, then returns.",
            "Each band has its own rhythm. Drag the lines on the canvas to move the crossovers. RATE: click (right-click = back). Drag GATE.",
            "Generate a spectral picture. LOCK boxes on the canvas to keep them, then generate the rest again. Same seed = same picture.",
            "Morph sound A into sound B: partials glide to their partners, tone colour blends, timing lines up. Drop a sound: left half = A, right half = B." };
        return H[juce::jlimit(0, 5, page)];
    }
    void applyTheme() {   // after DARK / LIGHT: everything that stored a colour at construction gets the new one
        std::function<void(juce::Component&)> walk = [&](juce::Component& c) {
            if (auto* l = dynamic_cast<juce::Label*>(&c)) { const auto role = l->getProperties()["role"].toString(); l->setColour(juce::Label::textColourId, role == "status" ? GREEN : (role == "ink" ? INK : GREY)); }
            if (auto* e = dynamic_cast<juce::TextEditor*>(&c)) { e->setColour(juce::TextEditor::backgroundColourId, sp::pal().field); e->setColour(juce::TextEditor::textColourId, INK); e->setColour(juce::TextEditor::outlineColourId, sp::pal().line); e->applyColourToAllText(INK); }
            for (auto* ch : c.getChildren()) walk(*ch);
        };
        walk(owner); view.imgDirty = true; owner.repaint();
    }
    void loadMorph(int which, const juce::File& f) { say("Loading " + f.getFileName() + "..."); auto sp = safe; L.loadMorphSoundAsync(which, f, [this, sp](bool, const juce::String& msg) { if (!sp) return; syncAll(); say(msg); }); }
    void cutBox() { if (!view.haveSel) { say("Draw a box first (SELECT, M)."); return; } L.undo.begin(L.canvas, view.sel.t0, view.sel.t1); clearRegion(L.canvas, view.sel); boxChanged(view.sel); say("Cut."); }
    void hearOnce() { updateRegion(); L.audition.store(view.haveSel && L.output.load() == OutCanvas ? -2 : -1); }
    void syncPageKnobs() {
        for (auto& items : pageItems) for (auto* c : items) if (auto* k = dynamic_cast<Knob*>(c)) {
            const auto n = k->l.getText(); double v = k->s.getValue();
            if (n == "LENGTH s") v = L.imageMap.seconds; else if (n == "LOW Hz") v = L.imageMap.loHz; else if (n == "HIGH Hz") v = L.imageMap.hiHz; else if (n == "CONTRAST") v = L.imageMap.gamma;
            else if (n == "HITS") v = L.ghost.count; else if (n == "EVOLVE") v = L.ghost.evolve; else if (n == "MEMORY") v = L.ghost.memory; else if (n == "DECAY") v = L.ghost.decay;
            else if (n == "CHAOS") v = L.chaos.chaos; else if (n == "DENSITY") v = L.chaos.density; else if (n == "HARMONY") v = L.chaos.harmony; else if (n == "MOTION") v = L.chaos.motion; else if (n == "CONSTRAINT") v = L.chaos.constraint;
            else if (n == "SEED") v = (&items == &pageItems[2]) ? L.ghost.seed : L.chaos.seed;
            else if (n == "SWING") v = L.split.swing;
            else continue;
            k->s.setValue(v, juce::dontSendNotification);
        }
    }
    void setOutput(int o) { L.output = o; proc.setLabParam("labOutput", (float) o); if (o == OutSplit) L.requestSplit(); if (o == OutGhost) L.requestGhost(); if (o == OutMorph) L.requestMorph(); refreshTop(); }
    void hearMenu() {
        juce::PopupMenu m; m.addSectionHeader("WHAT YOU HEAR");
        m.addItem(1, "Follow the page", true, hearPinned < 0);
        const char* n[] = { "Canvas", "Ghost", "Split", "Morph" }; for (int i = 0; i < 4; ++i) m.addItem(10 + i, juce::String("Always ") + n[i], true, hearPinned == i);
        auto sp = safe;
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&hearBtn), [this, sp](int r) { if (!sp || r == 0) return; hearPinned = r == 1 ? -1 : r - 10; setOutput(hearPinned >= 0 ? hearPinned : kPageOutput[page]); });
    }
    void refreshTop() {
        static const char* names[] = { "CANVAS", "GHOST", "SPLIT", "MORPH" };
        const int o = juce::jlimit(0, 3, L.output.load()); juce::String t = juce::String("HEARING: ") + names[o];
        if (o == OutGhost) { const int pg = L.playingGhost.load(); auto b = L.latestBank(); const int n = b ? (int) b->ghosts.size() : 0; if (n > 0) t << " " << (pg >= 0 ? juce::String(pg + 1) : juce::String("-")) << "/" << n; }
        else if (o == OutSplit) t << " " << L.split.bands << " BANDS";
        else if (o == OutMorph) t << " " << juce::roundToInt(L.morphNow.load() * 100) << "%";
        t << (hearPinned >= 0 ? "  (PINNED)" : "") << "  v";
        hearBtn.setButtonText(t);
        keysBtn.setToggleState(L.playKeys.load(), juce::dontSendNotification); loopBtn.setToggleState(!L.playKeys.load(), juce::dontSendNotification);
        shotBtn.setToggleState(L.oneShot.load(), juce::dontSendNotification); shotBtn.setVisible(L.playKeys.load());
        play.setButtonText(L.previewPlaying.load() ? "STOP" : "PLAY"); play.setToggleState(L.previewPlaying.load(), juce::dontSendNotification);
    }
    void refreshUndo() { undoBtn.setEnabled(L.undo.depth() > 0); redoBtn.setEnabled(L.undo.redoDepth() > 0); }
    void syncAll() {
        keyBox.setSelectedItemIndex(proc.model.root, juce::dontSendNotification); scaleBox.setSelectedItemIndex(proc.model.scale, juce::dontSendNotification);
        rootBox.setSelectedId(juce::jlimit(24, 96, L.rootNote.load()), juce::dontSendNotification);
        pitchBox.setSelectedId(L.keysPitch.load() == PitchTape ? 2 : 1, juce::dontSendNotification); inBox.setSelectedItemIndex(L.stereoIn.load(), juce::dontSendNotification);
        transBtn.setToggleState(L.keepTransients.load(), juce::dontSendNotification); autoBtn.setToggleState(L.autoLevel.load(), juce::dontSendNotification);
        brushKnobs[0]->s.setValue(L.brush.size, juce::dontSendNotification); brushKnobs[1]->s.setValue(L.brush.hardness, juce::dontSendNotification); brushKnobs[2]->s.setValue(L.brush.level, juce::dontSendNotification); brushKnobs[3]->s.setValue(L.brush.noise, juce::dontSendNotification);
        if (bandsBox) bandsBox->setSelectedId(L.split.bands, juce::dontSendNotification);
        if (hitBox) hitBox->setSelectedId((int) proc.labGhostP->load() + 1, juce::dontSendNotification);
        if (nameA) nameA->setText("A: " + (L.audioA.empty() ? juce::String("-") : L.nameA + " (" + juce::String(L.audioA.size() / SR, 1) + " s)"), juce::dontSendNotification);
        if (nameB) nameB->setText("B: " + (L.audioB.empty() ? juce::String("-") : L.nameB + " (" + juce::String(L.audioB.size() / SR, 1) + " s)"), juce::dontSendNotification);
        if (laneBtn) laneBtn->setToggleState(L.laneOn.load(), juce::dontSendNotification);
        if (velBtn) velBtn->setToggleState(L.velMorph.load(), juce::dontSendNotification);
        if (loopSelBtn) loopSelBtn->setToggleState(L.loopRegion.load(), juce::dontSendNotification);
        syncWave();
        syncPageKnobs(); refreshTop(); refreshUndo();
    }
    void tick() {
        const auto now = juce::Time::getMillisecondCounter();
        if (remapPending) { remapPending = false; remapAt = now; L.remapImageAsync(); }   // IMAGE knobs: re-mapped on the worker, ticks merged
        view.refreshImage();
        bool repaintView = false;
        if (L.bankVersion.load() != lastBank) { lastBank = L.bankVersion.load(); view.imgDirty = true; if (page == 2) strip.rebuild(); refreshWavePeaks(); }
        if (L.sideRev.load() != lastSideRev) { lastSideRev = L.sideRev.load(); grid.repaint(); lane.repaint(); syncPageKnobs(); if (bandsBox) bandsBox->setSelectedId(L.split.bands, juce::dontSendNotification); repaintView = true; }
        const double pp = L.playPos.load(); const int px = pp < 0 ? -1 : (int) view.xOf(pp * view.T());
        if (px != view.lastPlayX) { view.lastPlayX = px; repaintView = true; }
        if (repaintView) view.repaint();
        if (page == 2) strip.repaint();
        if (page == 3) grid.repaint();
        if (morphState) morphState->setText(L.audioA.empty() || L.audioB.empty() ? "Load or pick sounds for A and B." : (L.busy.load() ? "Analysing A and B (partials, tone colour, timing)..." : "Ready: turn MORPH, draw the per-bar lane, or play keys with PER NOTE on."), juce::dontSendNotification);
        if (ghostNote) ghostNote->setText(L.ghostTrimmed.load() ? "Long sound: the ghosts use its first 8 seconds. Click a ghost to hear it; SEND TO BEATS saves the set." : "Click a ghost to hear it. SEND TO BEATS saves the whole set (one WAV per hit).", juce::dontSendNotification);
        if (hitBox && !hitBox->isPopupActive()) hitBox->setSelectedId((int) proc.labGhostP->load() + 1, juce::dontSendNotification);
        if (const int o = (int) proc.labOutputP->load(); o != L.output.load()) L.output = o;   // automation of Lab Hear
        if (const int pr = L.progress.load(); pr >= 0) say("Saving... " + juce::String(pr) + "%");
        refreshTop();
        // the canvas length + the rendering bar live in the ruler's right end: repaint just that strip while the worker runs
        const bool busyNow = L.busy.load(); if (busyNow || busyNow != wasBusy) view.repaint(view.getWidth() - 300, 0, 300, SpecView::kRuler); wasBusy = busyNow;
        const int cc = L.clipCount.load(); if (cc != lastClip) { lastClip = cc; clipUntil = now + 1500; }
        const float m = L.meter.load(); if (std::abs(m - lastMeter) > 0.004f || now < clipUntil + 100) { lastMeter = m; owner.repaint(kMeter.expanded(2)); }
    }
};

// =====================================================================================================
LabPanel::LabPanel(SpecProcessor& p) : proc(p) {
    impl = std::make_unique<Impl>(*this, p);
    setOpaque(true);   // the panel paints every pixel of its area
    setWantsKeyboardFocus(true);
    showPage(0); selectTool(0);
    startTimerHz(30);
}
LabPanel::~LabPanel() { stopTimer(); impl.reset(); }
void LabPanel::timerCallback() { if (isVisible()) impl->tick(); }
void LabPanel::showPage(int p) {
    impl->page = juce::jlimit(0, 5, p); impl->view.page = impl->page;
    for (int i = 0; i < 6; ++i) { impl->pageBtns[i]->setToggleState(i == impl->page, juce::dontSendNotification); for (auto* c : impl->pageItems[i]) if (c) c->setVisible(i == impl->page); }
    impl->strip.setVisible(impl->page == 2); impl->grid.setVisible(impl->page == 3); impl->lane.setVisible(impl->page == 5); impl->wave.setVisible(impl->page == 0);
    if (const int want = kPageOutput[impl->page]; impl->hearPinned < 0 && (impl->L.output.load() != want || (int) proc.labOutputP->load() != want)) impl->setOutput(want);   // what you hear follows the page
    if (impl->page == 2) impl->strip.rebuild();
    impl->syncAll(); impl->view.imgDirty = true; impl->view.repaint(); repaint();
}
void LabPanel::canvasReplaced() { impl->view.fit(); impl->view.changed(); impl->syncAll(); repaint(); }
void LabPanel::selectTool(int t) {
    impl->view.tool = (SpecView::Tool) juce::jlimit(0, 7, t);
    if (t != SpecView::Select && t != SpecView::Shift && t != SpecView::Stretch) { impl->view.haveSel = false; impl->updateRegion(); impl->syncWave(); }
    for (int i = 0; i < impl->toolBtns.size(); ++i) impl->toolBtns[i]->setToggleState(i == t, juce::dontSendNotification);
    impl->view.repaint();
}
void LabPanel::paint(juce::Graphics& g) {
    // opaque, solid surfaces only: nothing translucent is layered over the window (no glows, no see-through cards)
    g.setColour(BG); g.fillRect(getLocalBounds());
    ghosty::drawGhost(g, ghosty::lookFor(3), 34, 30, 34);
    g.setColour(INK); g.setFont(juce::FontOptions(21.0f, juce::Font::bold)); g.drawText("SPECTRAL LAB", 60, 8, 220, 26, juce::Justification::centredLeft);
    g.setColour(GREY); g.setFont(juce::FontOptions(9.0f, juce::Font::bold)); g.drawText("GHOSTY SPECTRUM  -  PAINT THE SOUND ITSELF", 62, 33, 230, 12, juce::Justification::centredLeft);
    card(g, kSide); card(g, kPage);
    caption(g, "TOOLS", kSide.getX() + 12, kSide.getY() + 6); caption(g, "BRUSH", kSide.getX() + 12, 216); caption(g, "KEY + PITCH", kSide.getX() + 12, 386); caption(g, "OUT", kSide.getX() + 12, 568);
    // the strip: row 1 = title (the status label sits right of it), row 2 = one line of help (never the title again)
    static const char* titles[] = { "PAINT", "IMAGE TO SOUND", "SPECTRAL GHOST", "SPECTRAL SPLIT", "CHAOS PAINT", "SPECTRAL DNA" };
    g.setColour(INK); g.setFont(juce::FontOptions(12.0f, juce::Font::bold)); g.drawText(titles[impl->page], kPage.getX() + 14, kPage.getY() + 4, 180, 20, juce::Justification::centredLeft);
    g.setColour(GREY); g.setFont(juce::FontOptions(11.0f)); g.drawText(Impl::helpText(impl->page), kPage.getX() + 14, kPage.getY() + 24, kPage.getWidth() - 28, 16, juce::Justification::centredLeft, true);
    // output meter + clip light
    const auto m = kMeter.toFloat(); const float v = juce::jlimit(0.0f, 1.0f, impl->L.meter.load());
    g.setColour(GREY); g.setFont(juce::FontOptions(9.0f, juce::Font::bold)); g.drawText("OUT", m.withWidth(24), juce::Justification::centredLeft);
    auto bar = m.withTrimmedLeft(26).withTrimmedRight(16).withSizeKeepingCentre(m.getWidth() - 42, 8);
    g.setColour(CARD2); g.fillRoundedRectangle(bar, 3);
    const float db = v > 0 ? juce::jlimit(0.0f, 1.0f, (20.0f * std::log10(v) + 48.0f) / 48.0f) : 0.0f;
    g.setColour(v > 0.89f ? AMBER : GREEN); g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * db), 3);
    const bool clip = juce::Time::getMillisecondCounter() < impl->clipUntil;
    g.setColour(clip ? RED : RED.withAlpha(0.18f)); g.fillEllipse(m.getRight() - 12, m.getCentreY() - 5, 10, 10);
}
void LabPanel::paintOverChildren(juce::Graphics& g) {
    if (impl->dropZone < 0) return;   // DNA: where a dropped sound goes
    auto r = kPage.reduced(8).toFloat(); auto a = r.removeFromLeft(r.getWidth() / 2).reduced(4), b = r.reduced(4);
    for (int i = 0; i < 2; ++i) { auto z = i == 0 ? a : b; const bool hot = impl->dropZone == i;
        g.setColour(BLUE.withAlpha(hot ? 0.35f : 0.12f)); g.fillRoundedRectangle(z, 10); g.setColour(BLUE.withAlpha(hot ? 1.0f : 0.5f)); g.drawRoundedRectangle(z, 10, hot ? 2.5f : 1.2f);
        g.setColour(INK); g.setFont(juce::FontOptions(18.0f, juce::Font::bold)); g.drawText(i == 0 ? "DROP HERE = A" : "DROP HERE = B", z, juce::Justification::centred); }
}
void LabPanel::resized() {
    auto& I = *impl;
    I.tabPads.setBounds(290, 10, 98, 30); I.tabLoop.setBounds(392, 10, 98, 30); I.tabLab.setBounds(494, 10, 118, 30);
    I.play.setBounds(624, 10, 62, 30); I.hearBtn.setBounds(692, 10, 196, 30);
    I.keysBtn.setBounds(894, 10, 56, 30); I.loopBtn.setBounds(952, 10, 56, 30); I.shotBtn.setBounds(1012, 10, 70, 30);
    for (int i = 0; i < 6; ++i) I.pageBtns[i]->setBounds(244 + i * 92, 54, 88, 30);
    I.gridBox.setBounds(800, 56, 96, 26); I.fitBtn.setBounds(900, 55, 46, 28); I.transBtn.setBounds(950, 55, 130, 28); I.autoBtn.setBounds(1084, 55, 100, 28);
    I.status.setBounds(kPage.getX() + 200, kPage.getY() + 4, kPage.getWidth() - 212, 20);   // row 1, right of the page title
    I.view.setBounds(kView);
    const int sx = kSide.getX() + 10, sw = kSide.getWidth() - 20;
    for (int i = 0; i < 8; ++i) I.toolBtns[i]->setBounds(sx + (i % 2) * (sw / 2 + 2), kSide.getY() + 24 + (i / 2) * 34, sw / 2 - 2, 30);
    for (int i = 0; i < 4; ++i) I.brushKnobs[i]->setBounds(sx + (i % 2) * (sw / 2), 230 + (i / 2) * 76, sw / 2, 74);
    I.keyBox.setBounds(sx, 402, 60, 26); I.scaleBox.setBounds(sx + 64, 402, sw - 64, 26);
    I.snapBtn.setBounds(sx, 432, sw / 2 - 2, 26); I.pitchBox.setBounds(sx + sw / 2 + 2, 432, sw / 2 - 2, 26); I.rootBox.setBounds(sx, 462, sw, 26);
    I.undoBtn.setBounds(sx, 496, sw / 2 - 2, 28); I.redoBtn.setBounds(sx + sw / 2 + 2, 496, sw / 2 - 2, 28);
    I.lenBox.setBounds(sx, 530, sw / 2 - 2, 28); I.newBtn.setBounds(sx + sw / 2 + 2, 530, sw / 4 - 3, 28); I.clearBtn.setBounds(sx + sw / 2 + sw / 4 + 1, 530, sw / 4 - 1, 28);
    I.importBtn.setBounds(sx, 584, sw - 62, 28); I.inBox.setBounds(sx + sw - 58, 584, 58, 28);
    I.wavOut.setBounds(sx, 616, sw, 32); I.beatsBtn.setBounds(sx, 652, sw, 26); I.arrangeBtn.setBounds(sx, 682, sw, 26);
    I.levelKnob->setBounds(sx, 712, 64, 66); I.widthKnob->setBounds(sx + 66, 712, 64, 66);
    I.showBtn.setBounds(sx + 134, 728, sw - 134, 30);
}
void LabPanel::themeChanged() { impl->applyTheme(); }
juce::NamedValueSet LabPanel::probe() const {
    const auto& I = *impl; juce::NamedValueSet v;
    v.set("page", I.page); v.set("tool", (int) I.view.tool); v.set("vt0", I.view.vt0); v.set("vt1", I.view.vt1); v.set("vr0", I.view.vr0); v.set("vr1", I.view.vr1);
    v.set("T", I.L.canvas.T); v.set("haveSel", I.view.haveSel); v.set("selT0", I.view.sel.t0); v.set("selT1", I.view.sel.t1); v.set("selR0", I.view.sel.r0); v.set("selR1", I.view.sel.r1); v.set("dropZone", I.dropZone);
    v.set("brushSize", I.L.brush.size); v.set("gridDiv", I.view.gridDiv); v.set("hear", I.hearBtn.getButtonText()); v.set("status", I.status.getText());
    v.set("redoEnabled", I.redoBtn.isEnabled()); v.set("undoEnabled", I.undoBtn.isEnabled());
    return v;
}
bool LabPanel::isInterestedInFileDrag(const juce::StringArray& f) { for (auto& x : f) if (juce::File(x).hasFileExtension("wav;aif;aiff;mp3;flac;ogg;m4a;caf;aac;png;jpg;jpeg;gif;bmp")) return true; return false; }
void LabPanel::fileDragEnter(const juce::StringArray& f, int x, int y) { fileDragMove(f, x, y); }
void LabPanel::fileDragMove(const juce::StringArray& f, int x, int) {
    bool audio = false; for (auto& s : f) if (!juce::File(s).hasFileExtension("png;jpg;jpeg;gif;bmp")) audio = true;
    const int z = impl->page == 5 && audio ? (x < kPage.getCentreX() ? 0 : 1) : -1;
    if (z != impl->dropZone) { impl->dropZone = z; repaint(); }
}
void LabPanel::fileDragExit(const juce::StringArray&) { if (impl->dropZone >= 0) { impl->dropZone = -1; repaint(); } }
void LabPanel::filesDropped(const juce::StringArray& files, int x, int) {
    impl->dropZone = -1; repaint();
    for (auto& s : files) {
        const juce::File f(s);
        if (f.hasFileExtension("png;jpg;jpeg;gif;bmp")) { impl->importPicture(f); return; }
        if (impl->page == 5) { impl->loadMorph(x < kPage.getCentreX() ? 0 : 1, f); return; }
        impl->importSound(f); return;
    }
}
bool LabPanel::keyPressed(const juce::KeyPress& k) {
    auto& I = *impl; const auto mods = k.getModifiers(); const int code = k.getKeyCode();
    if (mods.isCommandDown() && (code == 'Z' || code == 'z')) { (mods.isShiftDown() ? I.redoBtn : I.undoBtn).triggerClick(); return true; }
    if (mods.isCommandDown() && (code == 'Y' || code == 'y')) { I.redoBtn.triggerClick(); return true; }
    if (mods.isCommandDown()) return false;
    if (code == juce::KeyPress::spaceKey) { I.play.triggerClick(); return true; }
    if (code == juce::KeyPress::returnKey) { I.hearOnce(); return true; }
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) { I.cutBox(); return true; }
    if (code == juce::KeyPress::escapeKey) { I.view.haveSel = false; I.updateRegion(); I.syncWave(); I.view.repaint(); return true; }
    const auto c = juce::CharacterFunctions::toUpperCase(k.getTextCharacter());
    const juce::String keys = "BESFTXLM"; const int t = keys.indexOfChar(c); if (t >= 0) { selectTool(t); return true; }
    if (c == '[' || c == ']') { auto& b = I.L.brush; b.size = juce::jlimit(0.5f, 24.0f, b.size * (c == ']' ? 1.25f : 0.8f)); I.brushKnobs[0]->s.setValue(b.size, juce::dontSendNotification); I.view.repaint(); return true; }
    if (c == '0') { I.fitBtn.triggerClick(); return true; }
    return false;
}
