#include "ArrangePage.h"
#include "TypeIn.h"
#include "ExportGate.h"   // Shared: the demo export gate (drags)
#include <cmath>
#include <iostream>

namespace arr {
static const char* const kRerenderTip = "Ask the page that made this sound to render it again (with its saved settings); every copy of it follows";

// ================================================================== theme (Spectrum's palette; light by default)
Theme Theme::make(ghosty::themes::Theme look) {
    const auto& T = ghosty::themes::get(look); Theme t; t.look = look; t.dark = !T.isLight;
    auto rd = [&](juce::Colour c) { return ghosty::themes::readableOn(ghosty::themes::readableOn(c, T.panel, 3.0), T.waveBg, 3.0); };
    t.bg = T.window; t.panel = T.panel; t.panel2 = T.card; t.lane = T.waveBg; t.laneAlt = T.waveBg.interpolatedWith(T.cell, 0.35f);
    t.grid = T.waveBg.interpolatedWith(T.line, 0.45f); t.gridBar = T.line; t.ink = T.ink; t.grey = T.dim; t.dim = T.dim;
    t.accent = rd(T.accent1); t.accent2 = rd(T.accent2); t.loop = rd(T.accent2); t.playhead = rd(T.accent3); t.sel = T.ink; t.meter = rd(T.good); t.hot = rd(T.bad);
    return t;
}
juce::Colour Theme::clip(int i) const {
    static const juce::uint32 night[] = { 0xff3cff8f, 0xffff4fd8, 0xff45e6ff, 0xffffc35a, 0xffa855f7, 0xfff87171, 0xffa3e635, 0xff60a5fa };
    static const juce::uint32 day[] = { 0xff22c475, 0xffe23cbf, 0xff1ab3d6, 0xfff0a627, 0xff9346e8, 0xffec5a5a, 0xff7fbf1d, 0xff3f86f0 };
    return ghosty::themes::readableOn(juce::Colour((dark ? night : day)[(size_t) ((i % 8) + 8) % 8]), lane, 2.0);   // clip colours keep their hue, readable on the lanes
}

namespace {
juce::String posText(double b, int bpb, double bpm) {   // 9.3.1  0:17.250
    b = clampF(b, 0.0, kMaxBeats, 0.0); bpb = std::max(1, bpb);
    const int bar = (int) std::floor(b / bpb) + 1; const double inBar = b - (bar - 1) * (double) bpb; const int beat = (int) std::floor(inBar) + 1; const int six = juce::jlimit(1, 4, (int) std::floor((inBar - (beat - 1)) * 4) + 1);
    const double sec = b * 60.0 / clampF(bpm, 20.0, 400.0, 120.0); const int m = (int) (sec / 60); const double s = sec - m * 60;
    return juce::String(bar) + "." + juce::String(beat) + "." + juce::String(six) + "   " + juce::String(m) + ":" + juce::String(s, 3).paddedLeft('0', 6);
}
const double kSnapVals[] = { -1, 1, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0, 0 };   // -1 = one bar, 0 = off
const char* kSnapNames[] = { "BAR", "1/4", "1/8", "1/16", "1/32", "1/8 T", "1/16 T", "OFF" };
void styleButton(juce::TextButton& b, const Theme& t, juce::Colour on) {
    b.setColour(juce::TextButton::buttonColourId, t.panel2); b.setColour(juce::TextButton::buttonOnColourId, on);
    b.setColour(juce::TextButton::textColourOffId, t.ink); b.setColour(juce::TextButton::textColourOnId, t.dark ? juce::Colour(0xff07051a) : juce::Colours::white);
}
void styleSlider(juce::Slider& s, const Theme& t, juce::Colour c) {
    s.setColour(juce::Slider::trackColourId, c); s.setColour(juce::Slider::backgroundColourId, t.panel2); s.setColour(juce::Slider::thumbColourId, c.brighter(0.2f));
    s.setColour(juce::Slider::textBoxTextColourId, t.ink); s.setColour(juce::Slider::textBoxBackgroundColourId, t.panel); s.setColour(juce::Slider::textBoxOutlineColourId, t.grid);
}
void styleCombo(juce::ComboBox& cb, const Theme& t) { cb.setColour(juce::ComboBox::backgroundColourId, t.panel2); cb.setColour(juce::ComboBox::textColourId, t.ink); cb.setColour(juce::ComboBox::outlineColourId, t.grid); cb.setColour(juce::ComboBox::arrowColourId, t.grey); }
} // namespace
juce::String positionString(double b, int bpb, double bpm) { return posText(b, bpb, bpm); }
namespace {
constexpr const char* kAudioExts = "*.wav;*.aif;*.aiff;*.flac;*.ogg";   // what juce's basic formats read here
// value <-> lane height (0 bottom .. 1 top)
inline double dbToU(double db) { return juce::jlimit(0.0, 1.0, (db + 60.0) / 72.0); }
inline double uToDb(double u) { return juce::jlimit(-60.0, 12.0, u * 72.0 - 60.0); }
struct ClipBoard { std::vector<std::pair<int, Clip>> clips; double base = 0; };
ClipBoard& clipboard() { static ClipBoard c; return c; }
}

// ================================================================== drag-out chips
struct ArrangePage::Chip : juce::Component {
    ArrangePage& page; int what; juce::String text; bool dragging = false; int state = 0;   // 0 stale, 1 rendering, 2 ready
    Chip(ArrangePage& p, int w, const juce::String& t) : page(p), what(w), text(t) { setMouseCursor(juce::MouseCursor::DraggingHandCursor); }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat().reduced(1.5f); const auto& t = page.theme; const auto c = what == 2 ? t.accent2 : t.accent;
        g.setColour(c.withAlpha(isMouseOver() ? 0.28f : 0.14f)); g.fillRoundedRectangle(r, r.getHeight() / 2); g.setColour(c); g.drawRoundedRectangle(r, r.getHeight() / 2, 1.2f);
        const auto dot = juce::Rectangle<float>(6, 6).withCentre({ r.getX() + 10, r.getCentreY() });
        g.setColour(state == 2 ? t.meter : state == 1 ? juce::Colour(0xffffb020) : t.dim); g.fillEllipse(dot);
        g.setColour(t.ink); g.setFont(juce::FontOptions(11.5f, juce::Font::bold)); g.drawText(text, getLocalBounds().withTrimmedLeft(12), juce::Justification::centred);
    }
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    void mouseDown(const juce::MouseEvent&) override { dragging = false; }
    void mouseDrag(const juce::MouseEvent& e) override { if (!dragging && e.getDistanceFromDragStart() > 4) { dragging = true; page.dragOut(what); } }
    void mouseUp(const juce::MouseEvent&) override { if (!dragging) page.say(what == 2 ? ArrangePage::midiChipHelp() : what == 1 ? "Drag STEMS into your DAW: one WAV per track (rendered when you first drag)." : "Drag MIX into your DAW: the arrangement as one WAV (kept rendered in the background; green dot = ready). With LOOP on: the loop region."); }
};

// ================================================================== bar sliders (Spectrum's convention, see SpecSliders.h)
// mouse-down = the value under the mouse, drag scrubs, Shift = fine (1/10), wheel = one step, double-click = default,
// right-click = type a value (the page's TypeIn, or the host editor's), hover = the value in real units. One gesture = one undo step.
namespace {
class ArrBar : public juce::Slider {
public:
    double wheelStep = 0;   // 0 = the interval
    ArrBar() { setSliderStyle(juce::Slider::LinearHorizontal); }
    juce::String hint;   // what the bar is (hover shows it with the value in real units)
    juce::String getTooltip() override { return (hint.isNotEmpty() ? hint + ": " : juce::String()) + getTextFromValue(getValue()) + "   (click / drag, Shift = fine, wheel = step, double-click = default, right-click = type)"; }
    void mouseDown(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu() || !isEnabled()) return;                                  // right-click: TypeIn
        if (e.getNumberOfClicks() >= 2 && isDoubleClickReturnEnabled()) { dragging = false; return; }   // the double-click resets (its own gesture)
        const double keep = getValue();
        juce::Slider::mouseDown(e);                                                          // opens the gesture (onDragStart)
        dragging = true; x0 = e.position.x; fine = e.mods.isShiftDown();
        setValue(fine ? keep : valueAt(e.position.x), juce::sendNotificationSync);          // absolute: the value under the mouse (Shift: stay)
        v0 = getValue();
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (!dragging) return;
        if (e.mods.isShiftDown() != fine) { fine = e.mods.isShiftDown(); x0 = e.position.x; v0 = getValue(); }   // switching keeps the value (no jump)
        const double w = juce::jmax(1.0, (double) barWidth());
        const double p = valueToProportionOfLength(v0) + (e.position.x - x0) / w * (fine ? 0.1 : 1.0);
        setValue(proportionOfLengthToValue(juce::jlimit(0.0, 1.0, p)), juce::sendNotificationSync);
    }
    void mouseUp(const juce::MouseEvent& e) override { if (!dragging) return; dragging = false; juce::Slider::mouseUp(e); }   // closes the gesture (onDragEnd)
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override {
        if (!isEnabled()) return;
        const double d = std::abs(w.deltaX) > std::abs(w.deltaY) ? (w.isReversed ? -w.deltaX : w.deltaX) : (w.isReversed ? -w.deltaY : w.deltaY);
        if (d == 0) return;
        const double step = wheelStep > 0 ? wheelStep : (getInterval() > 0 ? getInterval() : (getMaximum() - getMinimum()) / 100.0);
        setValue(getValue() + (d > 0 ? step : -step), juce::sendNotificationSync);
    }
    double valueAt(float x) { return proportionOfLengthToValue(juce::jlimit(0.0, 1.0, (double) x / juce::jmax(1, barWidth()))); }
    int barWidth() const { return getTextBoxPosition() == juce::Slider::TextBoxRight || getTextBoxPosition() == juce::Slider::TextBoxLeft ? getWidth() - getTextBoxWidth() : getWidth(); }
private:
    bool dragging = false, fine = false; float x0 = 0; double v0 = 0;
};
} // namespace
juce::String panText(double v) { const int p = juce::roundToInt(v * 100); return p == 0 ? juce::String("C") : juce::String(std::abs(p)) + (p < 0 ? " L" : " R"); }
double parsePanText(const juce::String& t) {
    const auto u = t.trim(); if (u.equalsIgnoreCase("c") || u.equalsIgnoreCase("centre") || u.equalsIgnoreCase("center")) return 0.0;
    double v = u.retainCharacters("0123456789.").getDoubleValue() / 100.0; if (u.containsIgnoreCase("l") || u.startsWithChar('-')) v = -v;
    return juce::jlimit(-1.0, 1.0, v);
}
double parseDbText(const juce::String& t, double lo, double hi) {
    if (t.containsIgnoreCase("inf")) return lo;
    return juce::jlimit(lo, hi, t.retainCharacters("0123456789.-").getDoubleValue());
}
namespace {
struct ValueBox : juce::Component {   // the TypeIn look for painted bars: type, Enter applies, Esc cancels
    juce::TextEditor ed; std::function<void(juce::String)> apply;
    ValueBox(const juce::String& text, std::function<void(juce::String)> fn) : apply(std::move(fn)) {
        ed.setText(text, false); ed.setJustification(juce::Justification::centred); ed.setFont(juce::FontOptions(16.0f, juce::Font::bold)); ed.setSelectAllWhenFocused(true);
        ed.onReturnKey = [this] { const auto t = ed.getText().trim(); auto fn2 = apply; close(); if (fn2 && t.isNotEmpty()) fn2(t); }; ed.onEscapeKey = [this] { close(); };
        addAndMakeVisible(ed); setSize(120, 34);
    }
    void resized() override { ed.setBounds(getLocalBounds()); }
    void visibilityChanged() override { if (isShowing()) ed.grabKeyboardFocus(); }
    void close() { if (auto* cb = findParentComponentOfClass<juce::CallOutBox>()) cb->dismiss(); }
};
inline double gainToU(double db) { const double x = juce::jlimit(0.0, 1.0, (db + 60.0) / 72.0); return x * x; }   // the header gain bar (square-law width)
inline double uToGain(double u) { return juce::jmap(std::sqrt(juce::jlimit(0.0, 1.0, u)), 0.0, 1.0, -60.0, 12.0); }
}

// ================================================================== timeline
class Timeline : public juce::Component, public juce::TooltipClient, private juce::ScrollBar::Listener {
public:
    explicit Timeline(ArrangePage& p) : page(p), s(p.session) { addAndMakeVisible(hbar); addAndMakeVisible(vbar); hbar.addListener(this); vbar.addListener(this); hbar.setAutoHide(false); vbar.setAutoHide(true); setWantsKeyboardFocus(false); }
    static constexpr int headerW = 178, rulerH = 42, markerH = 13, barH = 12, autoH = 48, collapsedH = 26;
    int trackH = 64;
    double ppb = 24, first = 0; int vscroll = 0;   // pixels per beat, first visible beat, vertical scroll in px
    juce::uint32 hoverClip = 0;

    Arrangement& A() { return s.arrangement; }
    double bpm() const { return s.effectiveBpm(); }
    double xToBeat(float x) const { return first + (x - headerW) / ppb; }
    float beatToX(double b) const { return (float) (headerW + (b - first) * ppb); }
    int rowH(int i) { return A().tracks[(size_t) i].collapsed ? collapsedH : trackH; }
    int blockH(int i) { const auto& t = A().tracks[(size_t) i]; return rowH(i) + (t.showAuto && !t.collapsed ? autoH : 0); }
    int yOfTrack(int i) { int y = rulerH - vscroll; for (int j = 0; j < i && j < (int) A().tracks.size(); ++j) y += blockH(j); return y; }
    int contentH() { int h = 0; for (int j = 0; j < (int) A().tracks.size(); ++j) h += blockH(j); return h + trackH; }
    struct RowHit { int track = -1; bool autoLane = false; bool below = false; };
    RowHit rowAt(int y) {
        RowHit h; int yy = rulerH - vscroll;
        for (int i = 0; i < (int) A().tracks.size(); ++i) { const int r = rowH(i), b = blockH(i); if (y >= yy && y < yy + b) { h.track = i; h.autoLane = y >= yy + r; return h; } yy += b; }
        h.below = y >= yy; h.track = h.below ? (int) A().tracks.size() : -1; return h;
    }
    int trackAt(int y) { auto h = rowAt(y); return h.track; }
    int lanesBottom() const { return getHeight() - barH; }

    void updateBars() {
        const double total = std::max(A().lengthBeats() + 16.0, 64.0), vis = (getWidth() - headerW) / ppb;
        hbar.setRangeLimits(0, std::max(total, first + vis)); hbar.setCurrentRange(first, vis, juce::dontSendNotification);
        const int content = contentH(), viewH = lanesBottom() - rulerH;
        vscroll = juce::jlimit(0, std::max(0, content - viewH), vscroll);
        vbar.setRangeLimits(0, std::max(content, viewH)); vbar.setCurrentRange(vscroll, viewH, juce::dontSendNotification);
    }
    void resized() override { hbar.setBounds(headerW, getHeight() - barH, getWidth() - headerW - barH, barH); vbar.setBounds(getWidth() - barH, rulerH, barH, lanesBottom() - rulerH); updateBars(); }
    void scrollBarMoved(juce::ScrollBar* b, double v) override { if (b == &hbar) first = std::max(0.0, v); else vscroll = std::max(0, (int) v); repaint(); }
    void zoomAround(double factor, float x) { const double b = xToBeat(x); ppb = juce::jlimit(1.5, 400.0, ppb * factor); first = std::max(0.0, b - (x - headerW) / ppb); updateBars(); repaint(); }
    void fit() { const double len = std::max(16.0, A().lengthBeats() + 2.0); ppb = juce::jlimit(1.5, 400.0, (getWidth() - headerW - 20) / len); first = 0; vscroll = 0; updateBars(); repaint(); }
    void fitRange(double b0, double b1) { b1 = std::max(b1, b0 + 1); ppb = juce::jlimit(1.5, 400.0, (getWidth() - headerW - 40) / (b1 - b0)); first = std::max(0.0, b0 - 20 / ppb); updateBars(); repaint(); }

    // ---- waveform peaks (processed sound when the builder has made it, else the source mapped through the edit)
    struct Peaks { std::vector<float> lo, hi; int step = 256; bool exact = false; };
    std::map<juce::String, Peaks> peaks;
    const Peaks& peaksFor(const Clip& c) {
        const auto key = c.processKey(bpm()) + "@" + juce::String(s.sampleRate(), 1);
        auto it = peaks.find(key); if (it != peaks.end() && it->second.exact) return it->second;
        Peaks pk; const int step = pk.step;
        if (auto b = s.cache.find(key)) {
            const int n = b->getNumSamples(); const int m = (n + step - 1) / step; pk.lo.assign((size_t) m, 0); pk.hi.assign((size_t) m, 0); pk.exact = true;
            for (int i = 0; i < n; ++i) { const float v = 0.5f * (b->getSample(0, i) + b->getSample(1, i)); auto& lo = pk.lo[(size_t) (i / step)]; auto& hi = pk.hi[(size_t) (i / step)]; lo = std::min(lo, v); hi = std::max(hi, v); }
        } else if (it != peaks.end()) return it->second;
        else if (c.source && c.source->valid()) {   // quick approximation: map processed time to source time
            const auto& src = c.source->audio; const double k = c.processedSeconds(bpm()) * s.sampleRate(); const int m = std::max(1, std::min(1 << 22, (int) (k / step))); pk.lo.assign((size_t) m, 0); pk.hi.assign((size_t) m, 0);
            const double per = (double) src.getNumSamples() / m;
            for (int j = 0; j < m; ++j) { const int jj = c.reverse ? m - 1 - j : j; const int a = (int) (jj * per), e = std::min(src.getNumSamples(), (int) ((jj + 1) * per) + 1);
                for (int i = a; i < e; i += 2) { const float v = src.getSample(0, i); pk.lo[(size_t) j] = std::min(pk.lo[(size_t) j], v); pk.hi[(size_t) j] = std::max(pk.hi[(size_t) j], v); } }
        }
        if (peaks.size() > 400) peaks.clear();
        return peaks[key] = std::move(pk);
    }

    // ---- painting
    void paint(juce::Graphics& g) override {
        const auto& t = page.theme; auto& a = A(); const int W = getWidth(), bottom = lanesBottom();
        g.fillAll(t.bg);
        const double last = xToBeat((float) W); const int bpb = std::max(1, a.beatsPerBar);
        g.saveState(); g.reduceClipRegion(headerW, rulerH, W - headerW, bottom - rulerH);
        for (int i = 0; i <= (int) a.tracks.size(); ++i) {
            const int y = yOfTrack(i);
            if (i == (int) a.tracks.size()) { if (y > bottom) continue; g.setColour(t.dim.withAlpha(0.35f)); juce::Path p; p.addRectangle((float) headerW + 2, (float) y + 6, (float) (W - headerW - 4), (float) trackH - 12); const float d[] = { 5, 5 }; juce::PathStrokeType(1).createDashedStroke(p, p, d, 2); g.fillPath(p);
                g.setColour(t.dim); g.setFont(juce::FontOptions(12.0f)); g.drawText("drop a sound here for a new track", headerW + 12, y, 400, trackH, juce::Justification::centredLeft); continue; }
            const int h = blockH(i); if (y > bottom || y + h < rulerH) continue;
            g.setColour(i % 2 ? t.laneAlt : t.lane); g.fillRect(headerW, y, W - headerW, rowH(i));
            if (h > rowH(i)) { g.setColour(t.panel2.withAlpha(0.6f)); g.fillRect(headerW, y + rowH(i), W - headerW, autoH); }
            if (!a.trackAudible(i)) { g.setColour(t.bg.withAlpha(0.5f)); g.fillRect(headerW, y, W - headerW, h); }
        }
        // grid: beats / bars (finer lines at the snap value when zoomed in)
        const double fine = a.snapOn && a.snapBeats > 0 && a.snapBeats * ppb >= 7 ? a.snapBeats : (ppb >= 7 ? 1.0 : (double) bpb);
        for (double b = std::floor(first / fine) * fine; b <= last; b += fine) {
            const float x = beatToX(b); const bool isBar = std::abs(std::fmod(b + 1e-9, (double) bpb)) < 1e-6, isBeat = std::abs(b - std::round(b)) < 1e-9;
            if (!isBar && ppb * bpb < 30 && fine >= bpb) continue;
            g.setColour(isBar ? t.gridBar : isBeat ? t.grid : t.grid.withAlpha(0.5f)); g.drawVerticalLine((int) x, (float) rulerH, (float) bottom);
        }
        for (auto& m : a.markers) { g.setColour(t.accent.withAlpha(0.25f)); g.drawVerticalLine((int) beatToX(m.beat), (float) rulerH, (float) bottom); }
        if (a.loopOn) { g.setColour(t.loop.withAlpha(0.07f)); g.fillRect(juce::Rectangle<float>(beatToX(a.loopStart), (float) rulerH, (float) ((a.loopEnd - a.loopStart) * ppb), (float) (bottom - rulerH))); }
        for (int i = 0; i < (int) a.tracks.size(); ++i) {
            const int y = yOfTrack(i), h = blockH(i); if (y > bottom || y + h < rulerH) continue;
            const auto& tr = a.tracks[(size_t) i];
            for (size_t k = 0; k < tr.clips.size(); ++k) drawClip(g, tr.clips[k], k + 1 < tr.clips.size() ? &tr.clips[k + 1] : nullptr, y, i);
            if (h > rowH(i)) drawLane(g, tr, y + rowH(i));
        }
        if (band) { g.setColour(t.accent.withAlpha(0.12f)); g.fillRect(bandRect); g.setColour(t.accent); g.drawRect(bandRect, 1); }
        g.restoreState();
        drawRuler(g, last, bpb);
        const float px = beatToX(s.playheadBeats());
        if (px >= headerW) { g.setColour(t.playhead); g.drawVerticalLine((int) px, (float) markerH, (float) bottom); juce::Path tri; tri.addTriangle(px - 6, (float) markerH, px + 6, (float) markerH, px, markerH + 9.0f); g.fillPath(tri); }
        // headers
        g.setColour(t.panel); g.fillRect(0, 0, headerW, bottom);
        g.setColour(t.ink); g.setFont(juce::FontOptions(12.5f, juce::Font::bold)); g.drawText(page.positionText().upToFirstOccurrenceOf("   ", false, false), 10, 4, headerW - 16, 18, juce::Justification::centredLeft);
        g.setColour(t.grey); g.setFont(juce::FontOptions(10.5f)); g.drawText(page.positionText().fromFirstOccurrenceOf("   ", false, false) + "    " + juce::String(a.beatsPerBar) + "/4", 10, 22, headerW - 16, 16, juce::Justification::centredLeft);
        g.saveState(); g.reduceClipRegion(0, rulerH, headerW, bottom - rulerH);
        for (int i = 0; i < (int) a.tracks.size(); ++i) { const int y = yOfTrack(i); if (y > bottom || y + blockH(i) < rulerH) continue; drawHeader(g, a.tracks[(size_t) i], i, y); }
        g.restoreState();
        g.setColour(t.grid); g.drawVerticalLine(headerW - 1, 0.0f, (float) bottom);
    }
    void drawRuler(juce::Graphics& g, double last, int bpb) {
        const auto& t = page.theme; auto& a = A(); const int W = getWidth();
        g.setColour(t.panel); g.fillRect(headerW, 0, W - headerW, rulerH); g.setColour(t.panel2); g.fillRect(headerW, 0, W - headerW, markerH);
        g.setColour(t.grid); g.drawHorizontalLine(rulerH - 1, (float) headerW, (float) W);
        if (hasLoop()) { auto r = braceRect();
            g.setColour(t.loop.withAlpha(a.loopOn ? 0.75f : 0.30f)); g.fillRoundedRectangle(r, 3);
            g.setColour(t.loop); g.fillRect(r.getX(), r.getY() - 2, 2.5f, r.getHeight() + 4); g.fillRect(r.getRight() - 2.5f, r.getY() - 2, 2.5f, r.getHeight() + 4);   // end handles
        }
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        const int barEvery = std::max(1, (int) std::ceil(38.0 / (ppb * bpb)));
        for (int bar = (int) std::floor(first / bpb); bar * bpb <= last; ++bar) {
            const float x = beatToX(bar * (double) bpb);
            if (bar % barEvery == 0) { g.setColour(t.grey); g.drawVerticalLine((int) x, (float) markerH, (float) rulerH - 2); g.setColour(t.ink); g.drawText(juce::String(bar + 1), (int) x + 3, markerH + 1, 40, 14, juce::Justification::left); }
            else if (ppb * bpb > 10) { g.setColour(t.dim); g.drawVerticalLine((int) x, markerH + 10.0f, (float) rulerH - 4); }
            if (ppb >= 10) for (int bt = 1; bt < bpb; ++bt) { g.setColour(t.dim.withAlpha(0.6f)); g.drawVerticalLine((int) beatToX(bar * (double) bpb + bt), markerH + 16.0f, (float) rulerH - 4); }
        }
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        for (size_t i = 0; i < a.markers.size(); ++i) {
            const float x = beatToX(a.markers[i].beat); if (x < headerW - 80 || x > W) continue;
            const int w = std::min(120, juce::GlyphArrangement::getStringWidthInt(juce::FontOptions(10.0f, juce::Font::bold), a.markers[i].name) + 12);
            g.setColour(t.accent); g.fillRoundedRectangle(x, 1.0f, (float) w, (float) markerH - 2, 3.0f);
            g.setColour(t.dark ? juce::Colour(0xff07051a) : juce::Colours::white); g.drawText(a.markers[i].name, (int) x + 5, 0, w - 6, markerH, juce::Justification::centredLeft, true);
        }
        if (!hasLoop() || braceRect().getRight() < W - 420 || braceRect().getX() > headerW + 420) {   // the gesture hint, where the brace isn't
            const bool right = !hasLoop() || braceRect().getRight() < W - 420; auto hr = juce::Rectangle<float>(right ? (float) W - 414 : (float) headerW + 6, rulerH - 14.0f, 408.0f, 12.0f);
            g.setColour(t.panel); g.fillRoundedRectangle(hr, 3); g.setColour(t.dim); g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
            g.drawText(ArrangePage::rulerHint(), hr.toNearestInt(), juce::Justification::centred, true);
        }
        if (hasLoop()) for (int k = 0; k < 2; ++k) {   // the grips: drag the loop out as WAV / MIDI
            const auto gr = gripRect(k == 1); if (gr.getRight() < headerW || gr.getX() > W) continue;
            g.setColour(t.dark ? juce::Colour(0xff07051a) : juce::Colours::white); g.fillRoundedRectangle(gr, 4); g.setColour(k ? t.accent2 : t.accent); g.drawRoundedRectangle(gr.reduced(0.5f), 4, 1.2f);
            g.setFont(juce::FontOptions(8.5f, juce::Font::bold)); g.drawText(juce::String::fromUTF8("\xe2\x87\xa1 ") + (k ? "MIDI" : "WAV"), gr.toNearestInt(), juce::Justification::centred);
        }
    }
    bool hasLoop() { return A().loopEnd > A().loopStart + 1e-9; }
    juce::Rectangle<float> braceRect() { auto& a = A(); return { beatToX(a.loopStart), rulerH - 13.0f, (float) std::max(2.0, (a.loopEnd - a.loopStart) * ppb), 10.0f }; }
    juce::Rectangle<float> gripRect(bool midi) {   // two grips at the brace's right end (after it when the loop is short)
        const auto r = braceRect(); const float gw = 36, gh = 12; const bool inside = r.getWidth() >= 2 * gw + 30;
        const float x = inside ? r.getRight() - 2 * gw - 6 : r.getRight() + 4;
        return { x + (midi ? gw + 2 : 0), rulerH - 14.0f, gw, gh };
    }
    juce::Rectangle<int> collapseBox(int y) const { return { 4, y + 8, 12, 16 }; }
    juce::Rectangle<int> autoBox(int y) const { return { headerW - 86, y + 8, 22, 18 }; }
    juce::Rectangle<int> muteBox(int y) const { return { headerW - 60, y + 8, 22, 18 }; }
    juce::Rectangle<int> soloBox(int y) const { return { headerW - 34, y + 8, 22, 18 }; }
    juce::Rectangle<int> gainBar(int y) const { return { 14, y + 34, 86, 9 }; }
    juce::Rectangle<int> panBar(int y) const { return { 108, y + 34, 50, 9 }; }
    juce::Rectangle<int> laneSwitch(int y) const { return { 14, y + 8, 120, 16 }; }
    void drawHeader(juce::Graphics& g, const Track& tr, int i, int y) {
        const auto& t = page.theme; const auto col = t.clip(tr.colour); const int rh = rowH(i);
        g.setColour(i % 2 ? t.panel : t.panel.interpolatedWith(t.panel2, 0.4f)); g.fillRect(0, y, headerW, blockH(i));
        g.setColour(col); g.fillRect(0, y + 4, 3, rh - 8);
        { juce::Path tri; const auto cb = collapseBox(y).toFloat(); if (tr.collapsed) tri.addTriangle(cb.getX() + 3, cb.getY() + 3, cb.getX() + 3, cb.getBottom() - 3, cb.getRight() - 1, cb.getCentreY()); else tri.addTriangle(cb.getX() + 1, cb.getY() + 5, cb.getRight() - 1, cb.getY() + 5, cb.getCentreX(), cb.getBottom() - 3); g.setColour(t.grey); g.fillPath(tri); }
        g.setColour(t.ink); g.setFont(juce::FontOptions(12.5f, juce::Font::bold)); g.drawText(tr.name, 18, y + 6, headerW - 108, 20, juce::Justification::centredLeft, true);
        auto box = [&](juce::Rectangle<int> r, const char* txt, bool on, juce::Colour c) { g.setColour(on ? c : t.panel2); g.fillRoundedRectangle(r.toFloat(), 4); g.setColour(on ? (t.dark ? juce::Colour(0xff07051a) : juce::Colours::white) : t.grey); g.setFont(juce::FontOptions(11.0f, juce::Font::bold)); g.drawText(txt, r, juce::Justification::centred); };
        box(autoBox(y), "A", tr.showAuto || !tr.volEnv.empty() || !tr.panEnv.empty(), t.accent); box(muteBox(y), "M", tr.mute, juce::Colour(0xffff9f43)); box(soloBox(y), "S", tr.solo, t.accent2);
        if (!tr.collapsed) {
            auto gb = gainBar(y).toFloat(); g.setColour(t.panel2); g.fillRoundedRectangle(gb, 3);
            const float gx = (float) juce::jmap((double) tr.gainDb, -60.0, 12.0, 0.0, 1.0); g.setColour(col.withAlpha(tr.volEnv.empty() ? 0.85f : 0.35f)); g.fillRoundedRectangle(gb.withWidth(std::max(3.0f, gb.getWidth() * std::pow(gx, 2.0f))), 3);
            auto pb = panBar(y).toFloat(); g.setColour(t.panel2); g.fillRoundedRectangle(pb, 3);
            const float cx = pb.getCentreX(), px = cx + tr.pan * pb.getWidth() / 2; g.setColour(col.withAlpha(tr.panEnv.empty() ? 0.85f : 0.35f)); g.fillRect(juce::Rectangle<float>(std::min(cx, px), pb.getY(), std::max(2.0f, std::abs(px - cx)), pb.getHeight()));
            g.setColour(t.dim); g.setFont(juce::FontOptions(9.5f));
            g.drawText(!tr.volEnv.empty() ? juce::String("AUTO") : (tr.gainDb <= -59.9f ? juce::String("-inf") : juce::String(tr.gainDb, 1)) + " dB", 14, y + 44, 86, 14, juce::Justification::left);
            g.drawText(!tr.panEnv.empty() ? juce::String("AUTO") : std::abs(tr.pan) < 0.005f ? juce::String("C") : (tr.pan < 0 ? "L" : "R") + juce::String((int) std::round(std::abs(tr.pan) * 100)), 108, y + 44, 50, 14, juce::Justification::left);
            // meter
            const float m = page.meters[(size_t) std::min(i, kMaxTracks - 1)]; auto mr = juce::Rectangle<float>((float) headerW - 9, (float) y + 32, 4, (float) rh - 40);
            g.setColour(t.panel2); g.fillRect(mr); const float u = (float) juce::jlimit(0.0, 1.0, (juce::Decibels::gainToDecibels(m, -60.0f) + 60.0) / 66.0);
            g.setColour(m >= 1.0f ? t.hot : t.meter); g.fillRect(mr.withTop(mr.getBottom() - mr.getHeight() * u));
        }
        if (blockH(i) > rh) { const int ly = y + rh; g.setColour(t.panel2); g.fillRect(0, ly, headerW, autoH); g.setColour(t.ink); g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
            g.drawText(juce::String(tr.autoLane == 0 ? "VOLUME" : "PAN") + juce::String::fromUTF8("  \xe2\x96\xbe"), laneSwitch(ly), juce::Justification::centredLeft);
            g.setColour(t.dim); g.setFont(juce::FontOptions(9.5f)); g.drawText("click adds, drag moves, double-click deletes", 14, ly + 24, headerW - 18, 20, juce::Justification::topLeft, true); }
        g.setColour(t.grid); g.drawHorizontalLine(y + blockH(i) - 1, 0.0f, (float) headerW);
    }
    // ---- envelopes
    float laneY(int ly, double u) const { return (float) (ly + 4 + (1.0 - u) * (autoH - 8)); }
    void drawEnv(juce::Graphics& g, const Envelope& e, std::function<float(double)> xOf, std::function<float(float)> yOf, float x0, float x1, juce::Colour c, bool handles) {
        if (e.empty()) return; juce::Path p; p.startNewSubPath(x0, yOf(e.front().value));
        for (auto& q : e) p.lineTo(juce::jlimit(x0, x1, xOf(q.beat)), yOf(q.value)); p.lineTo(x1, yOf(e.back().value));
        g.setColour(c); g.strokePath(p, juce::PathStrokeType(1.6f));
        if (handles) for (auto& q : e) { const float x = xOf(q.beat); if (x < x0 - 4 || x > x1 + 4) continue; g.setColour(page.theme.panel); g.fillEllipse(x - 3.5f, yOf(q.value) - 3.5f, 7, 7); g.setColour(c); g.drawEllipse(x - 3.5f, yOf(q.value) - 3.5f, 7, 7, 1.4f); }
    }
    void drawLane(juce::Graphics& g, const Track& tr, int ly) {
        const auto& t = page.theme; const auto col = t.clip(tr.colour); const auto& e = tr.autoLane == 0 ? tr.volEnv : tr.panEnv;
        auto yOf = [&](float v) { return laneY(ly, tr.autoLane == 0 ? dbToU(v) : (v + 1.0) / 2.0); };
        if (e.empty()) { g.setColour(col.withAlpha(0.5f)); const float y = yOf(tr.autoLane == 0 ? tr.gainDb : tr.pan); const float d[] = { 4, 4 }; g.drawDashedLine(juce::Line<float>((float) headerW, y, (float) getWidth(), y), d, 2, 1.0f);
            g.setColour(t.dim); g.setFont(juce::FontOptions(10.0f)); g.drawText("click to automate " + juce::String(tr.autoLane == 0 ? "volume" : "pan"), headerW + 8, ly + 2, 300, 14, juce::Justification::left); return; }
        drawEnv(g, e, [this](double b) { return beatToX(b); }, yOf, (float) headerW, (float) getWidth(), col, true);
    }
    juce::Rectangle<float> clipRect(const Clip& c, int y, int rh) const { return { beatToX(c.start), (float) y + 3, (float) std::max(3.0, c.length * ppb), (float) rh - 6 }; }
    juce::Rectangle<float> clipRect(const Clip& c, int y) { int ti = -1; A().findClip(c.id, &ti); return clipRect(c, y, ti >= 0 ? rowH(ti) : trackH); }
    juce::Rectangle<float> bodyRect(juce::Rectangle<float> r) const { return r.withTrimmedTop(15.0f).reduced(0, 2); }
    void drawClip(juce::Graphics& g, const Clip& c, const Clip* next, int y, int trackIdx) {
        const auto& t = page.theme; const int rh = rowH(trackIdx); auto r = clipRect(c, y, rh); if (r.getRight() < headerW || r.getX() > getWidth()) return;
        const auto col = t.clip(c.colour); const bool sel = s.selection.count(c.id) > 0 || c.id == s.selected; const bool audible = !c.mute && A().trackAudible(trackIdx);
        const bool collapsed = A().tracks[(size_t) trackIdx].collapsed;
        g.setColour(col.withAlpha(audible ? (t.dark ? 0.30f : 0.22f) : 0.08f)); g.fillRoundedRectangle(r, 5);
        const float titleH = collapsed ? r.getHeight() : 15.0f;
        g.setColour(col.withAlpha(audible ? 0.95f : 0.4f)); g.fillRoundedRectangle(r.withHeight(titleH), 5); if (!collapsed) g.fillRect(r.withHeight(15.0f).withTrimmedTop(8));
        if (!collapsed) {
            const auto& pk = peaksFor(c);
            if (!pk.lo.empty()) {
                const double spb = s.sampleRate() * 60.0 / bpm(), offS = c.offset * s.sampleRate(); const float midY = r.getY() + 15 + (r.getHeight() - 15) / 2, amp = (r.getHeight() - 19) / 2;
                const int len = (int) pk.lo.size();
                g.setColour((t.dark ? col.brighter(0.4f) : col.darker(0.35f)).withAlpha(audible ? 0.9f : 0.35f));
                const int x0 = std::max((int) r.getX(), headerW), x1 = std::min((int) r.getRight(), getWidth());
                for (int x = x0; x < x1; ++x) {
                    const double b0 = xToBeat((float) x) - c.start, b1 = b0 + 1.0 / ppb; int i0 = (int) ((offS + b0 * spb) / pk.step), i1 = std::max(i0 + 1, (int) ((offS + b1 * spb) / pk.step));
                    if (c.loop && len > 0) { const int sh = (i0 / len) * len; i0 -= sh; i1 -= sh; }
                    float lo = 0, hi = 0; for (int i = std::max(0, i0); i < std::min(len, i1); ++i) { lo = std::min(lo, pk.lo[(size_t) i]); hi = std::max(hi, pk.hi[(size_t) i]); }
                    if (hi > lo) g.drawVerticalLine(x, midY - std::min(1.0f, hi * 1.4f) * amp, midY - std::max(-1.0f, lo * 1.4f) * amp + 1);
                }
                if (c.loop && c.source) {   // loop wrap marks
                    const double P = c.naturalBeats(bpm()); if (P > 0) for (double w = c.start - std::fmod(s.arrangement.secondsToBeats(c.offset), P) + P; w < c.end(); w += P) { const float wx = beatToX(w); g.setColour(t.ink.withAlpha(0.35f)); g.drawVerticalLine((int) wx, r.getY() + 15, r.getBottom()); }
                }
            }
            auto fadePath = [&](double fb, Curve cv, bool in) {
                if (fb <= 0) return;
                juce::Path p; const float w = (float) (fb * ppb), top = r.getY() + 15, h = r.getBottom() - top;
                for (int k = 0; k <= 24; ++k) { const double x = k / 24.0; const float gy = fadeInGain(cv, in ? x : 1 - x); const float px = (in ? r.getX() : r.getRight() - w) + (float) x * w, py = top + h * (1 - gy); if (k == 0) p.startNewSubPath(px, py); else p.lineTo(px, py); }
                juce::Path fill = p; if (in) { fill.lineTo(r.getX(), top); } else { fill.lineTo(r.getRight(), top); } fill.closeSubPath();
                g.setColour(t.bg.withAlpha(0.45f)); g.fillPath(fill); g.setColour(t.ink.withAlpha(0.7f)); g.strokePath(p, juce::PathStrokeType(1.3f));
            };
            const bool morphIn = c.morph && next && next->start < c.end();
            fadePath(c.fadeIn, c.fadeInCurve, true); if (!morphIn) fadePath(c.fadeOut, c.fadeOutCurve, false);
            if (!c.gainEnv.empty() || (page.envMode && c.id == s.selected)) {
                const auto br = bodyRect(r); auto yOf = [&](float v) { return (float) (br.getBottom() - dbToU(v) * br.getHeight()); };
                if (c.gainEnv.empty()) { g.setColour(t.accent.withAlpha(0.6f)); g.drawHorizontalLine((int) yOf(0), br.getX(), br.getRight()); }
                else drawEnv(g, c.gainEnv, [this, &c](double b) { return beatToX(c.start + b); }, yOf, br.getX(), br.getRight(), t.accent, page.envMode && c.id == s.selected);
            }
            if ((sel || c.id == hoverClip) && !page.envMode) {   // fade handles only where you are working
                g.setColour(t.panel); const auto hIn = fadeHandle(c, y, true), hOut = fadeHandle(c, y, false);
                g.fillEllipse(hIn); g.fillEllipse(hOut); g.setColour(t.ink.withAlpha(0.8f)); g.drawEllipse(hIn, 1.2f); g.drawEllipse(hOut, 1.2f);
            }
            if (morphIn) {
                const float mx0 = beatToX(next->start), mx1 = beatToX(std::min(c.end(), next->end())); auto m = juce::Rectangle<float>(mx0, r.getY() + 15, mx1 - mx0, r.getHeight() - 15);
                juce::ColourGradient gr(col.withAlpha(0.35f), mx0, 0, t.clip(next->colour).withAlpha(0.35f), mx1, 0, false); g.setGradientFill(gr); g.fillRect(m);
            }
        }
        // title + edit flags
        juce::String tags; if (std::abs(c.pitch) > 0.005f) tags << (c.pitch > 0 ? "+" : "") << juce::String(c.pitch, std::abs(c.pitch - std::round(c.pitch)) < 0.005f ? 0 : 2) << "st" << (c.keepLength ? "" : " tape") << "  ";
        if (std::abs(c.stretch - 1) > 0.001f) tags << "x" << juce::String(c.stretch, 2) << "  ";
        if (c.warp && c.source && c.source->bpm > 0) tags << "WARP  ";
        if (c.loop) tags << "LOOP  ";
        if (c.reverse) tags << "REV  ";
        if (c.noteBased()) tags << "MIDI  ";
        if (c.mute) tags << "MUTED";
        int badgeW = 0;
        if (c.morph && next && next->start < c.end() && r.getWidth() > 90) {   // morph badge on the title bar
            badgeW = 52; const float bx = std::min(r.getRight(), beatToX(next->start)) - badgeW - 3;   // just before the next clip covers the title
            auto b = juce::Rectangle<float>(std::max(r.getX() + 4, bx), r.getY() + 2, (float) badgeW, 11.0f);
            badgeW = (int) (r.getRight() - b.getX());
            g.setColour(t.dark ? juce::Colour(0xff07051a) : juce::Colours::white); g.fillRoundedRectangle(b, 5.5f); g.setColour(col.darker(0.3f)); g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
            g.drawText(juce::String::fromUTF8("MORPH \xe2\x86\x92"), b.toNearestInt(), juce::Justification::centred);
        }
        g.setColour(t.dark ? juce::Colour(0xff07051a) : juce::Colours::white); g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(c.name + (tags.isNotEmpty() ? "   " + tags.trim() : juce::String()), r.withHeight(15.0f).reduced(6, 0).withTrimmedRight((float) badgeW).toNearestInt(), juce::Justification::centredLeft, true);
        if (sel) { g.setColour(t.sel); g.drawRoundedRectangle(r.reduced(0.5f), 5, c.id == s.selected ? 2.0f : 1.4f); }
        else { g.setColour(col.withAlpha(0.8f)); g.drawRoundedRectangle(r.reduced(0.5f), 5, 1.0f); }
    }
    juce::Rectangle<float> fadeHandle(const Clip& c, int y, bool in) {
        auto r = clipRect(c, y); const float x = in ? (float) (r.getX() + c.fadeIn * ppb) : (float) (r.getRight() - c.fadeOut * ppb);
        return juce::Rectangle<float>(10, 10).withCentre({ juce::jlimit(r.getX() + 5, r.getRight() - 5, x), r.getY() + 21 });
    }

    // ---- mouse
    enum class Drag { None, Move, TrimL, TrimR, FadeIn, FadeOut, Playhead, Loop, Gain, Pan, Band, ClipEnv, LaneEnv, Marker, RightRuler, LoopL, LoopR, LoopMove, Grip };
    Drag drag = Drag::None; Arrangement before; int origTrack = 0; double downBeat = 0; bool changedInDrag = false; int dragTrack = -1; float origVal = 0;
    std::map<juce::uint32, std::pair<int, Clip>> origs; bool altCopy = false, copied = false;
    bool band = false; juce::Rectangle<int> bandRect; std::set<juce::uint32> bandBase;
    int envIndex = -1; int markerIndex = -1; bool gripMidi = false, gripStarted = false;
    struct Hit { juce::uint32 id = 0; int track = -1; Drag zone = Drag::None; };
    Hit hitTest(juce::Point<int> p) {
        Hit h; if (p.x < headerW || p.y < rulerH || p.y >= lanesBottom()) return h;
        const auto rh = rowAt(p.y); if (rh.track < 0 || rh.track >= (int) A().tracks.size() || rh.autoLane) { h.track = rh.track; return h; }
        const int ti = rh.track; h.track = ti; const int y = yOfTrack(ti);
        const auto& clips = A().tracks[(size_t) ti].clips; const bool coll = A().tracks[(size_t) ti].collapsed;
        for (int k = (int) clips.size() - 1; k >= 0; --k) {   // topmost (later) first
            const auto& c = clips[(size_t) k]; auto r = clipRect(c, y, rowH(ti)); if (!r.expanded(3, 0).contains(p.toFloat())) continue;
            h.id = c.id;
            if (page.envMode && c.id == s.selected && !coll) h.zone = Drag::ClipEnv;
            else if (!coll && fadeHandle(c, y, true).expanded(4).contains(p.toFloat())) h.zone = Drag::FadeIn;
            else if (!coll && fadeHandle(c, y, false).expanded(4).contains(p.toFloat())) h.zone = Drag::FadeOut;
            else if (p.x < r.getX() + std::min(7.0f, r.getWidth() / 4)) h.zone = Drag::TrimL;
            else if (p.x > r.getRight() - std::min(7.0f, r.getWidth() / 4)) h.zone = Drag::TrimR;
            else h.zone = Drag::Move;
            return h;
        }
        return h;
    }
    void mouseMove(const juce::MouseEvent& e) override {
        const auto h = hitTest(e.getPosition());
        if (h.id != hoverClip) { hoverClip = h.id; repaint(); }
        setMouseCursor(h.zone == Drag::TrimL || h.zone == Drag::TrimR ? juce::MouseCursor::LeftRightResizeCursor : h.zone == Drag::FadeIn || h.zone == Drag::FadeOut || h.zone == Drag::ClipEnv ? juce::MouseCursor::PointingHandCursor
                       : h.zone == Drag::Move ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    }
    void mouseExit(const juce::MouseEvent&) override { if (hoverClip) { hoverClip = 0; repaint(); } }
    int markerAt(int x) { auto& a = A(); for (int i = (int) a.markers.size() - 1; i >= 0; --i) { const float mx = beatToX(a.markers[(size_t) i].beat); if (x >= mx - 3 && x <= mx + 60) return i; } return -1; }
    // envelope helpers
    Envelope* laneEnv(int ti) { auto& t = A().tracks[(size_t) ti]; return t.autoLane == 0 ? &t.volEnv : &t.panEnv; }
    double laneValue(int ti, int y) { const int ly = yOfTrack(ti) + rowH(ti); const double u = juce::jlimit(0.0, 1.0, 1.0 - (y - ly - 4) / (double) (autoH - 8)); return A().tracks[(size_t) ti].autoLane == 0 ? uToDb(u) : u * 2.0 - 1.0; }
    int envPointAt(const Envelope& e, std::function<juce::Point<float>(const EnvPoint&)> pos, juce::Point<int> p) { for (int i = 0; i < (int) e.size(); ++i) if (pos(e[(size_t) i]).getDistanceFrom(p.toFloat()) < 7) return i; return -1; }
    double snapIf(double b, const juce::ModifierKeys& m) { return m.isShiftDown() ? b : A().snap(b); }

    void mouseDown(const juce::MouseEvent& e) override {
        auto& a = A(); before = a; changedInDrag = false; drag = Drag::None; downBeat = xToBeat((float) e.x); copied = false; altCopy = false;
        if (e.y < rulerH && e.x >= headerW) {
            if (e.y < markerH) {
                markerIndex = markerAt(e.x);
                if (markerIndex >= 0) { if (e.mods.isPopupMenu()) { markerMenu(markerIndex); return; } drag = Drag::Marker; origVal = (float) a.markers[(size_t) markerIndex].beat; s.setPlayheadBeats(a.markers[(size_t) markerIndex].beat); repaint(); return; }
                if (e.mods.isPopupMenu()) { juce::PopupMenu m; m.addItem(1, "Add a marker here"); juce::Component::SafePointer<Timeline> sp(this); const double at = a.snap(std::max(0.0, downBeat));
                    m.showMenuAsync(juce::PopupMenu::Options(), [sp, at](int r) { if (sp && r == 1) sp->page.addMarkerAt(at); }); }
                return;
            }
            // the ruler gestures: right-click = move the playhead here, right-drag = loop; left only scrubs the playhead or
            // moves / resizes the loop that is there; the WAV / MIDI grips drag the loop out
            const auto pf = e.position;
            if (hasLoop() && (gripRect(false).contains(pf) || gripRect(true).contains(pf))) { drag = Drag::Grip; gripMidi = gripRect(true).contains(pf); return; }
            if (e.mods.isPopupMenu()) { drag = Drag::RightRuler; return; }   // decided on release / after 6 px
            if (hasLoop() && pf.y >= rulerH - 16) {
                const auto r = braceRect();
                if (std::abs(pf.x - r.getX()) <= 6) { drag = Drag::LoopL; return; }
                if (std::abs(pf.x - r.getRight()) <= 6) { drag = Drag::LoopR; return; }
                if (r.expanded(0, 3).contains(pf)) { drag = Drag::LoopMove; origVal = (float) a.loopStart; return; }
            }
            drag = Drag::Playhead; s.setPlayheadBeats(std::max(0.0, downBeat)); repaint();
            return;
        }
        if (e.x < headerW && e.y >= rulerH) { headerDown(e); return; }
        const auto rh = rowAt(e.y);
        if (rh.autoLane && rh.track >= 0 && e.x >= headerW) {   // automation lane
            auto* env = laneEnv(rh.track); const int ly = yOfTrack(rh.track) + rowH(rh.track); const bool vol = a.tracks[(size_t) rh.track].autoLane == 0;
            auto pos = [&](const EnvPoint& q) { return juce::Point<float>(beatToX(q.beat), laneY(ly, vol ? dbToU(q.value) : (q.value + 1) / 2)); };
            if (e.mods.isPopupMenu()) { juce::PopupMenu m; m.addItem(1, "Clear this lane"); m.addItem(2, "Hide the lane"); juce::Component::SafePointer<Timeline> sp(this); const int ti = rh.track;
                m.showMenuAsync(juce::PopupMenu::Options(), [sp, ti](int r) { if (!sp || ti >= (int) sp->A().tracks.size()) return; auto b = sp->A(); if (r == 1) sp->laneEnv(ti)->clear(); if (r == 2) sp->A().tracks[(size_t) ti].showAuto = false; sp->page.edited(b); }); return; }
            envIndex = envPointAt(*env, pos, e.getPosition());
            if (envIndex < 0) { env->push_back({ snapIf(std::max(0.0, downBeat), e.mods), (float) laneValue(rh.track, e.y) }); sortEnvelope(*env);
                for (int i = 0; i < (int) env->size(); ++i) if (pos((*env)[(size_t) i]).getDistanceFrom(e.position) < 9) envIndex = i; changedInDrag = true; s.changed(); }
            drag = Drag::LaneEnv; dragTrack = rh.track; repaint(); return;
        }
        const auto h = hitTest(e.getPosition());
        if (h.id == 0) {
            if (e.mods.isPopupMenu() && h.track >= 0) { laneMenu(h.track, downBeat); return; }
            if (h.track >= 0 && !rh.below) { drag = Drag::Band; band = true; bandRect = { e.x, e.y, 0, 0 }; bandBase = e.mods.isShiftDown() || e.mods.isCommandDown() ? s.selection : std::set<juce::uint32>(); if (bandBase.empty()) { s.selection.clear(); s.selected = 0; } page.refresh(); }
            else { s.selection.clear(); s.selected = 0; page.refresh(); }
            return;
        }
        // selection: Shift / Cmd toggles, a click on an unselected clip selects only it
        if (e.mods.isShiftDown() || e.mods.isCommandDown()) { if (s.selection.count(h.id) && !e.mods.isPopupMenu()) { s.selection.erase(h.id); if (s.selected == h.id) s.selected = s.selection.empty() ? 0 : *s.selection.begin(); page.refresh(); return; } s.selection.insert(h.id); s.selected = h.id; }
        else if (!s.selection.count(h.id)) { s.selection = { h.id }; s.selected = h.id; }
        else s.selected = h.id;
        int ti = 0; auto* c = a.findClip(h.id, &ti);
        if (e.mods.isPopupMenu()) { page.refresh(); clipMenu(h.id, downBeat); return; }
        if (h.zone == Drag::ClipEnv) {
            const int y = yOfTrack(ti); const auto br = bodyRect(clipRect(*c, y));
            auto pos = [&](const EnvPoint& q) { return juce::Point<float>(beatToX(c->start + q.beat), (float) (br.getBottom() - dbToU(q.value) * br.getHeight())); };
            envIndex = envPointAt(c->gainEnv, pos, e.getPosition());
            if (envIndex < 0) { const double b = juce::jlimit(0.0, c->length, snapIf(downBeat, e.mods) - c->start); const double u = juce::jlimit(0.0, 1.0, (double) ((br.getBottom() - (float) e.y) / br.getHeight()));
                c->gainEnv.push_back({ b, (float) uToDb(u) }); sortEnvelope(c->gainEnv); for (int i = 0; i < (int) c->gainEnv.size(); ++i) if (pos(c->gainEnv[(size_t) i]).getDistanceFrom(e.position) < 9) envIndex = i; changedInDrag = true; s.changed(); }
            drag = Drag::ClipEnv; origTrack = ti; page.refresh(); return;
        }
        origs.clear(); for (auto id : s.selection) { int t2 = 0; if (auto* x = a.findClip(id, &t2)) origs[id] = { t2, *x }; }
        origTrack = ti; drag = h.zone; altCopy = h.zone == Drag::Move && e.mods.isAltDown(); page.refresh();
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        auto& a = A(); const double b = xToBeat((float) e.x), d = b - downBeat;
        switch (drag) {
            case Drag::Playhead: s.setPlayheadBeats(std::max(0.0, b)); repaint(); return;
            case Drag::Marker: { if (markerIndex < 0 || markerIndex >= (int) a.markers.size() || std::abs(e.getDistanceFromDragStartX()) < 3) return; a.markers[(size_t) markerIndex].beat = std::max(0.0, snapIf(origVal + d, e.mods)); changedInDrag = true; repaint(); return; }
            case Drag::Grip: if (e.getDistanceFromDragStart() > 4 && !gripStarted) { gripStarted = true; page.dragLoop(gripMidi); } return;
            case Drag::RightRuler: if (std::abs(e.getDistanceFromDragStartX()) < 6) return; drag = Drag::Loop; [[fallthrough]];
            case Drag::Loop: { if (std::abs(e.getDistanceFromDragStartX()) < 6) return; const double x0 = a.snap(std::max(0.0, std::min(downBeat, b))), x1 = a.snap(std::max(downBeat, b)); a.loopStart = x0; a.loopEnd = std::max(x0 + (a.snapOn && a.snapBeats > 0 ? a.snapBeats : kMinClipBeats), x1); a.loopOn = true; changedInDrag = true; s.changed(); page.refresh(); return; }
            case Drag::LoopL: case Drag::LoopR: case Drag::LoopMove: {
                const double minLen = a.snapOn && a.snapBeats > 0 ? a.snapBeats : kMinClipBeats;
                if (drag == Drag::LoopL) a.loopStart = juce::jlimit(0.0, a.loopEnd - minLen, snapIf(b, e.mods));
                else if (drag == Drag::LoopR) a.loopEnd = juce::jlimit(a.loopStart + minLen, kMaxBeats, snapIf(b, e.mods));
                else { const double len = a.loopEnd - a.loopStart; const double ns = std::max(0.0, snapIf(origVal + d, e.mods)); a.loopStart = ns; a.loopEnd = ns + len; }
                changedInDrag = true; s.changed(); page.refresh(); return; }
            case Drag::Band: {
                bandRect = juce::Rectangle<int>(e.getMouseDownPosition(), e.getPosition()); s.selection = bandBase;
                for (int ti = 0; ti < (int) a.tracks.size(); ++ti) { const int y = yOfTrack(ti); for (auto& c : a.tracks[(size_t) ti].clips) if (clipRect(c, y, rowH(ti)).intersects(bandRect.toFloat())) s.selection.insert(c.id); }
                s.selected = s.selection.empty() ? 0 : *s.selection.rbegin(); page.refresh(); return; }
            case Drag::Gain: case Drag::Pan: { if (dragTrack < 0 || dragTrack >= (int) a.tracks.size()) return; auto& tr = a.tracks[(size_t) dragTrack];
                const bool g = drag == Drag::Gain; const auto bar = g ? gainBar(0) : panBar(0);
                if (e.mods.isShiftDown() != barFine) { barFine = e.mods.isShiftDown(); barX0 = (float) e.x; barU0 = g ? gainToU(tr.gainDb) : (tr.pan + 1.0) / 2.0; }   // switching keeps the value
                const double u = juce::jlimit(0.0, 1.0, barU0 + (e.x - barX0) / (double) bar.getWidth() * (barFine ? 0.1 : 1.0));
                if (g) tr.gainDb = (float) uToGain(u); else tr.pan = (float) (u * 2.0 - 1.0);
                changedInDrag = true; s.changed(); repaint(); return; }
            case Drag::LaneEnv: { if (dragTrack < 0 || dragTrack >= (int) a.tracks.size()) return; auto* env = laneEnv(dragTrack); if (envIndex < 0 || envIndex >= (int) env->size()) return;
                auto& q = (*env)[(size_t) envIndex]; q.beat = std::max(0.0, snapIf(b, e.mods)); q.value = (float) laneValue(dragTrack, e.y);
                const double keep = q.beat; const float kv = q.value; sortEnvelope(*env); for (int i = 0; i < (int) env->size(); ++i) if ((*env)[(size_t) i].beat == keep && (*env)[(size_t) i].value == kv) envIndex = i;
                changedInDrag = true; s.changed(); repaint(); return; }
            case Drag::ClipEnv: { auto* c = a.findClip(s.selected); if (!c || envIndex < 0 || envIndex >= (int) c->gainEnv.size()) return; const auto br = bodyRect(clipRect(*c, yOfTrack(origTrack)));
                auto& q = c->gainEnv[(size_t) envIndex]; q.beat = juce::jlimit(0.0, c->length, snapIf(b, e.mods) - c->start); q.value = (float) uToDb(juce::jlimit(0.0, 1.0, (double) ((br.getBottom() - (float) e.y) / br.getHeight())));
                const double keep = q.beat; const float kv = q.value; sortEnvelope(c->gainEnv); for (int i = 0; i < (int) c->gainEnv.size(); ++i) if (c->gainEnv[(size_t) i].beat == keep && c->gainEnv[(size_t) i].value == kv) envIndex = i;
                changedInDrag = true; s.changed(); page.refresh(); return; }
            case Drag::None: return;
            case Drag::Move: case Drag::TrimL: case Drag::TrimR: case Drag::FadeIn: case Drag::FadeOut: default: break;
        }
        if (e.getDistanceFromDragStart() < 3 && !changedInDrag) return;   // a click is not a drag (no stacked Alt copies)
        const double spb = 60.0 / bpm();
        if (drag == Drag::Move) {
            if (altCopy && !copied) {   // Alt-drag: copies stay behind, the dragged clips are new
                std::set<juce::uint32> fresh; std::map<juce::uint32, std::pair<int, Clip>> no;
                for (auto& [id, tc] : origs) if (auto* dd = a.duplicateClip(id, tc.second.start)) { fresh.insert(dd->id); no[dd->id] = { tc.first, *dd }; if (id == s.selected) s.selected = dd->id; }
                origs = no; s.selection = fresh; copied = true;
            }
            auto pit = origs.find(s.selected); if (pit == origs.end()) return;
            const double ns = std::max(0.0, e.mods.isShiftDown() ? pit->second.second.start + d : a.snap(pit->second.second.start + d)); const double delta = ns - pit->second.second.start;
            int dt = juce::jlimit(0, (int) a.tracks.size() - 1, std::max(0, trackAt(e.y))) - pit->second.first;
            for (auto& [id, tc] : origs) { if (tc.first + dt < 0) dt = -tc.first; if (tc.first + dt >= (int) a.tracks.size()) dt = (int) a.tracks.size() - 1 - tc.first; }
            for (auto& [id, tc] : origs) a.moveClip(id, tc.first + dt, std::max(0.0, tc.second.start + delta));
        } else {
            int ti = 0; auto* c = a.findClip(s.selected, &ti); auto oit = origs.find(s.selected); if (!c || oit == origs.end()) return; const Clip& orig = oit->second.second;
            switch (drag) {
                case Drag::TrimL: {
                    double ns = e.mods.isShiftDown() ? orig.start + d : a.snap(orig.start + d);
                    ns = juce::jlimit(c->loop ? 0.0 : std::max(0.0, orig.start - orig.offset / spb), orig.end() - kMinClipBeats, ns);
                    c->start = ns; c->length = orig.end() - ns; c->offset = std::max(0.0, orig.offset + (ns - orig.start) * spb);
                    if (c->loop && orig.processedSeconds(bpm()) > 0) { const double P = orig.processedSeconds(bpm()); c->offset = std::fmod(std::fmod(orig.offset + (ns - orig.start) * spb, P) + P, P); }
                    c->fadeIn = std::min(orig.fadeIn, c->length);
                    if (!orig.gainEnv.empty()) { c->gainEnv.clear(); const double sh = ns - orig.start; for (auto q : orig.gainEnv) { q.beat -= sh; if (q.beat >= 0) c->gainEnv.push_back(q); } }
                    a.sortClips(); break; }
                case Drag::TrimR: {
                    const double maxLen = c->loop ? kMaxBeats : std::max(kMinClipBeats, (orig.processedSeconds(bpm()) - orig.offset) / spb);
                    double ne = e.mods.isShiftDown() ? orig.end() + d : a.snap(orig.end() + d); c->length = juce::jlimit(kMinClipBeats, std::max(maxLen, orig.length), ne - orig.start); c->fadeOut = std::min(orig.fadeOut, c->length); break; }
                case Drag::FadeIn: c->fadeIn = juce::jlimit(0.0, c->length - c->fadeOut, orig.fadeIn + d); break;
                case Drag::FadeOut: c->fadeOut = juce::jlimit(0.0, c->length - c->fadeIn, orig.fadeOut - d); break;
                case Drag::None: case Drag::Move: case Drag::Playhead: case Drag::Loop: case Drag::Gain: case Drag::Pan: case Drag::Band: case Drag::ClipEnv: case Drag::LaneEnv: case Drag::Marker: case Drag::RightRuler: case Drag::LoopL: case Drag::LoopR: case Drag::LoopMove: case Drag::Grip: default: break;
            }
        }
        changedInDrag = true; s.changed(); page.refresh();
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (drag == Drag::RightRuler) { s.setPlayheadBeats(std::max(0.0, A().snapOn ? A().snap(downBeat) : downBeat)); page.say("Playhead moved to " + page.positionText().upToFirstOccurrenceOf(" ", false, false) + "."); repaint(); }
        if (drag == Drag::Grip && !gripStarted) page.say("Drag the WAV / MIDI grip out of the plugin to export the loop.");
        gripStarted = false; juce::ignoreUnused(e);
        if (drag == Drag::Marker && changedInDrag) { auto& ms = A().markers; std::stable_sort(ms.begin(), ms.end(), [](const Marker& x, const Marker& y) { return x.beat < y.beat; }); }
        if (changedInDrag) { s.history.push(before); s.changed(); page.refresh(); }
        if (band) { band = false; repaint(); }
        drag = Drag::None; dragTrack = -1;
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override {
        auto& a = A();
        if (e.y < markerH && e.x >= headerW) { const int mi = markerAt(e.x); if (mi >= 0) renameMarker(mi); else page.addMarkerAt(a.snap(std::max(0.0, xToBeat((float) e.x)))); return; }
        if (e.x < headerW && e.y >= rulerH) { const auto rh = rowAt(e.y); const int ti = rh.track; if (ti >= 0 && ti < (int) a.tracks.size() && !rh.autoLane) {
            const int y = yOfTrack(ti);
            if (gainBar(y).expanded(2, 4).contains(e.getPosition())) { auto b = a; a.tracks[(size_t) ti].gainDb = 0; page.edited(b); }
            else if (panBar(y).expanded(2, 4).contains(e.getPosition())) { auto b = a; a.tracks[(size_t) ti].pan = 0; page.edited(b); }
            else if (e.x > 16 && e.y < y + 28 && e.x < headerW - 90) renameTrack(ti); } return; }
        const auto rh = rowAt(e.y);
        if (rh.autoLane && rh.track >= 0) {   // delete a lane point
            auto* env = laneEnv(rh.track); const int ly = yOfTrack(rh.track) + rowH(rh.track); const bool vol = a.tracks[(size_t) rh.track].autoLane == 0;
            const int i = envPointAt(*env, [&](const EnvPoint& q) { return juce::Point<float>(beatToX(q.beat), laneY(ly, vol ? dbToU(q.value) : (q.value + 1) / 2)); }, e.getPosition());
            if (i >= 0) { auto b = before; env->erase(env->begin() + i); page.edited(b); drag = Drag::None; changedInDrag = false; } return; }
        const auto h = hitTest(e.getPosition());
        if (h.zone == Drag::ClipEnv) { if (auto* c = a.findClip(h.id)) { const auto br = bodyRect(clipRect(*c, yOfTrack(h.track)));
            const int i = envPointAt(c->gainEnv, [&](const EnvPoint& q) { return juce::Point<float>(beatToX(c->start + q.beat), (float) (br.getBottom() - dbToU(q.value) * br.getHeight())); }, e.getPosition());
            if (i >= 0) { auto b = before; c->gainEnv.erase(c->gainEnv.begin() + i); page.edited(b); drag = Drag::None; changedInDrag = false; } } return; }
        if (h.zone == Drag::FadeIn || h.zone == Drag::FadeOut) { auto b = a; if (auto* c = a.findClip(h.id)) { (h.zone == Drag::FadeIn ? c->fadeIn : c->fadeOut) = 0; page.edited(b); } }
        else if (h.id) page.say("Clip: drag to move (Alt = copy, Shift = off grid), edges trim, round handles fade, E edits its gain envelope, S splits at the playhead, right-click for more.");
    }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override {
        if (e.mods.isCommandDown() || e.mods.isCtrlDown()) { zoomAround(std::pow(1.0015, (w.deltaY + w.deltaX) * 400.0), (float) std::max(e.x, headerW)); return; }
        if (e.mods.isAltDown()) { trackH = juce::jlimit(36, 160, trackH + (int) std::round((w.deltaY + w.deltaX) * 60)); updateBars(); repaint(); return; }   // track height
        if (e.x < headerW && e.y >= rulerH) {   // wheel over a header bar = one step
            const auto rh = rowAt(e.y); if (rh.track >= 0 && rh.track < (int) A().tracks.size() && !rh.autoLane && !A().tracks[(size_t) rh.track].collapsed) {
                const int y = yOfTrack(rh.track); const double d = std::abs(w.deltaX) > std::abs(w.deltaY) ? w.deltaX : w.deltaY; const double dir = (w.isReversed ? -d : d) > 0 ? 1.0 : -1.0;
                auto& tr = A().tracks[(size_t) rh.track];
                if (d != 0 && gainBar(y).expanded(2, 5).contains(e.getPosition())) { auto b = A(); tr.gainDb = (float) juce::jlimit(-60.0, 12.0, tr.gainDb + 0.5 * dir); page.edited(b); return; }
                if (d != 0 && panBar(y).expanded(2, 5).contains(e.getPosition())) { auto b = A(); tr.pan = (float) juce::jlimit(-1.0, 1.0, tr.pan + 0.01 * dir); page.edited(b); return; }
            }
        }
        double dx = w.deltaX, dy = w.deltaY; if (e.mods.isShiftDown()) std::swap(dx, dy);
        const int content = contentH(), viewH = lanesBottom() - rulerH;
        if (std::abs(dy) > 0 && content > viewH && !e.mods.isShiftDown() && e.y >= rulerH) vscroll = juce::jlimit(0, content - viewH, vscroll - (int) (dy * 220));
        else first = std::max(0.0, first - (dx != 0 ? dx : dy) * 300.0 / ppb);
        updateBars(); repaint();
    }
    void headerDown(const juce::MouseEvent& e) {
        auto& a = A(); const auto rh = rowAt(e.y); const int ti = rh.track;
        if (ti < 0 || ti >= (int) a.tracks.size()) { if (ti == (int) a.tracks.size() && a.tracks.size() < (size_t) kMaxTracks) { a.addTrack(); page.edited(before); } return; }
        const int y = yOfTrack(ti); auto& tr = a.tracks[(size_t) ti];
        if (rh.autoLane) { if (laneSwitch(y + rowH(ti)).contains(e.getPosition())) { tr.autoLane = 1 - tr.autoLane; page.edited(before); } return; }
        if (e.mods.isPopupMenu() && !tr.collapsed && (gainBar(y).expanded(2, 5).contains(e.getPosition()) || panBar(y).expanded(2, 5).contains(e.getPosition()))) { typeBar(ti, gainBar(y).expanded(2, 5).contains(e.getPosition())); return; }
        if (e.mods.isPopupMenu()) { trackMenu(ti); return; }
        if (collapseBox(y).expanded(2).contains(e.getPosition())) { tr.collapsed = !tr.collapsed; page.edited(before); return; }
        if (autoBox(y).contains(e.getPosition())) { tr.showAuto = !tr.showAuto; if (tr.showAuto) tr.collapsed = false; page.edited(before); return; }
        if (muteBox(y).contains(e.getPosition())) { tr.mute = !tr.mute; page.edited(before); return; }
        if (soloBox(y).contains(e.getPosition())) { if (e.mods.isCommandDown()) { const bool on = !tr.solo; for (auto& x : a.tracks) x.solo = false; tr.solo = on; } else tr.solo = !tr.solo; page.edited(before); return; }
        if (tr.collapsed) return;
        const bool onGain = gainBar(y).expanded(2, 5).contains(e.getPosition()), onPan = panBar(y).expanded(2, 5).contains(e.getPosition());
        if ((onGain || onPan) && e.getNumberOfClicks() < 2) {   // bar: the value under the mouse, then scrub (Shift = fine)
            drag = onGain ? Drag::Gain : Drag::Pan; dragTrack = ti; barFine = e.mods.isShiftDown(); barX0 = (float) e.x;
            const auto bar = onGain ? gainBar(y) : panBar(y); const double u = juce::jlimit(0.0, 1.0, (e.x - bar.getX()) / (double) bar.getWidth());
            if (!barFine) { if (onGain) tr.gainDb = (float) uToGain(u); else tr.pan = (float) (u * 2.0 - 1.0); changedInDrag = true; s.changed(); repaint(); }
            barU0 = onGain ? gainToU(tr.gainDb) : (tr.pan + 1.0) / 2.0; return;
        }
    }
    bool barFine = false; float barX0 = 0; double barU0 = 0;
    void typeBar(int ti, bool gain) {   // right-click: type an exact value
        auto& tr = A().tracks[(size_t) ti]; const int y = yOfTrack(ti); juce::Component::SafePointer<Timeline> sp(this);
        auto box = std::make_unique<ValueBox>(gain ? juce::String(tr.gainDb, 1) + " dB" : panText(tr.pan), [sp, ti, gain](juce::String t) { if (!sp) return; sp->applyBarText(ti, gain, t); });
        juce::CallOutBox::launchAsynchronously(std::move(box), localAreaToGlobal(gain ? gainBar(y) : panBar(y)), nullptr);
    }
    bool applyBarText(int ti, bool gain, const juce::String& t) {
        if (ti < 0 || ti >= (int) A().tracks.size() || !t.containsAnyOf("0123456789cCiI")) return false;
        auto b = A(); auto& tr = A().tracks[(size_t) ti]; if (gain) tr.gainDb = (float) parseDbText(t, -60.0, 12.0); else tr.pan = (float) parsePanText(t); page.edited(b); return true;
    }
    juce::String getTooltip() override {   // hover: the bar's value in real units
        const auto p = getMouseXYRelative();
        if (p.x >= headerW && p.y >= markerH && p.y < rulerH) {
            if (hasLoop() && gripRect(false).contains(p.toFloat())) return "Drag the loop out as a WAV";
            if (hasLoop() && gripRect(true).contains(p.toFloat())) return "Drag the loop out as MIDI (note-based clips)";
            return "Ruler: click / drag = playhead  |  right-click: move the playhead here  |  right-drag: loop (drag its ends to resize, its middle to move)  |  drag the loop out to export";
        }
        if (p.x >= headerW || p.y < rulerH) return {};
        const auto rh = rowAt(p.y); if (rh.track < 0 || rh.track >= (int) A().tracks.size() || rh.autoLane || A().tracks[(size_t) rh.track].collapsed) return {};
        const auto& tr = A().tracks[(size_t) rh.track]; const int y = yOfTrack(rh.track);
        if (gainBar(y).expanded(2, 5).contains(p)) return "Track gain " + (tr.gainDb <= -59.9f ? juce::String("-inf") : juce::String(tr.gainDb, 1)) + " dB  (click / drag, Shift = fine, wheel = 0.5 dB, double-click = 0 dB, right-click = type)";
        if (panBar(y).expanded(2, 5).contains(p)) return "Track pan " + panText(tr.pan) + "  (click / drag, Shift = fine, wheel = 1 %, double-click = centre, right-click = type)";
        return {};
    }
    void renameTrack(int ti) {
        auto* w = new juce::AlertWindow("Rename track", {}, juce::MessageBoxIconType::NoIcon); w->addTextEditor("name", A().tracks[(size_t) ti].name); w->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey)); w->addButton("Cancel", 0);
        juce::Component::SafePointer<Timeline> sp(this);
        w->enterModalState(true, juce::ModalCallbackFunction::create([sp, w, ti](int r) { if (r == 1 && sp && ti < (int) sp->A().tracks.size()) { auto b = sp->A(); sp->A().tracks[(size_t) ti].name = w->getTextEditorContents("name").toUpperCase().substring(0, 24); sp->page.edited(b); } }), true);
    }
    void renameMarker(int mi) {
        auto* w = new juce::AlertWindow("Rename marker", {}, juce::MessageBoxIconType::NoIcon); w->addTextEditor("name", A().markers[(size_t) mi].name); w->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey)); w->addButton("Cancel", 0);
        juce::Component::SafePointer<Timeline> sp(this);
        w->enterModalState(true, juce::ModalCallbackFunction::create([sp, w, mi](int r) { if (r == 1 && sp && mi < (int) sp->A().markers.size()) { auto b = sp->A(); sp->A().markers[(size_t) mi].name = w->getTextEditorContents("name").toUpperCase().substring(0, 24); sp->page.edited(b); } }), true);
    }
    void markerMenu(int mi) {
        juce::PopupMenu m; m.addItem(1, "Rename..."); m.addItem(2, "Delete"); m.addItem(3, "Loop this section (to the next marker)"); m.addItem(4, "Render this section");
        juce::Component::SafePointer<Timeline> sp(this);
        m.showMenuAsync(juce::PopupMenu::Options(), [sp, mi](int r) { if (!sp || mi >= (int) sp->A().markers.size()) return; auto& a = sp->A(); auto b = a;
            const double s0 = a.markers[(size_t) mi].beat, s1 = mi + 1 < (int) a.markers.size() ? a.markers[(size_t) mi + 1].beat : std::max(s0 + a.beatsPerBar, a.lengthBeats());
            if (r == 1) sp->renameMarker(mi);
            else if (r == 2) { a.markers.erase(a.markers.begin() + mi); sp->page.edited(b); }
            else if (r == 3 || r == 4) { a.loopStart = s0; a.loopEnd = s1; a.loopOn = true; sp->page.edited(b); if (r == 4) sp->page.startUserRender(false); } });
    }
    void trackMenu(int ti) {
        juce::PopupMenu m; m.addItem(1, "Rename..."); m.addItem(2, "Add track below", A().tracks.size() < (size_t) kMaxTracks); m.addItem(3, "Delete track", A().tracks.size() > 1);
        m.addItem(4, A().tracks[(size_t) ti].collapsed ? "Expand" : "Collapse"); m.addItem(5, "Show automation lane", true, A().tracks[(size_t) ti].showAuto); m.addItem(6, "Select all clips on this track");
        juce::PopupMenu cols; for (int i = 0; i < 8; ++i) cols.addColouredItem(10 + i, "Colour " + juce::String(i + 1), page.theme.clip(i)); m.addSubMenu("Colour", cols);
        juce::Component::SafePointer<Timeline> sp(this);
        m.showMenuAsync(juce::PopupMenu::Options(), [sp, ti](int r) { if (!sp || ti >= (int) sp->A().tracks.size()) return; auto& a = sp->A(); auto b = a;
            if (r == 1) sp->renameTrack(ti);
            else if (r == 2) { a.addTrack(); auto t = a.tracks.back(); a.tracks.pop_back(); a.tracks.insert(a.tracks.begin() + ti + 1, t); sp->page.edited(b); }
            else if (r == 3) { a.removeTrack(ti); sp->page.edited(b); }
            else if (r == 4) { a.tracks[(size_t) ti].collapsed = !a.tracks[(size_t) ti].collapsed; sp->page.edited(b); }
            else if (r == 5) { a.tracks[(size_t) ti].showAuto = !a.tracks[(size_t) ti].showAuto; sp->page.edited(b); }
            else if (r == 6) { sp->s.selection.clear(); for (auto& c : a.tracks[(size_t) ti].clips) sp->s.selection.insert(c.id); sp->s.selected = sp->s.selection.empty() ? 0 : *sp->s.selection.begin(); sp->page.refresh(); }
            else if (r >= 10) { a.tracks[(size_t) ti].colour = r - 10; for (auto& c : a.tracks[(size_t) ti].clips) c.colour = r - 10; sp->page.edited(b); } });
    }
    void laneMenu(int ti, double beat) {
        juce::PopupMenu m; m.addItem(1, "Import a sound here..."); m.addItem(2, "Add a sound from Shared Sounds (Ghosty Beats)..."); m.addItem(3, "Set the playhead here"); m.addItem(4, "Paste here", !clipboard().clips.empty()); m.addItem(5, "Add a marker here");
        juce::Component::SafePointer<Timeline> sp(this); const double at = A().snapDown(std::max(0.0, beat));
        m.showMenuAsync(juce::PopupMenu::Options(), [sp, ti, at](int r) { if (!sp) return;
            if (r == 1) { sp->page.chooser = std::make_unique<juce::FileChooser>("Import a sound", juce::File(), kAudioExts);
                sp->page.chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems, [sp, ti, at](const juce::FileChooser& fc) { if (!sp) return; juce::StringArray f; for (auto& x : fc.getResults()) f.add(x.getFullPathName()); sp->page.importFiles(f, ti, at); }); }
            else if (r == 2) { sp->s.selected = 0; sp->s.selection.clear(); sp->s.setPlayheadBeats(at); sp->page.fromBeatsMenu(); }
            else if (r == 3) { sp->s.setPlayheadBeats(at); sp->repaint(); }
            else if (r == 4) { sp->s.selection.clear(); sp->s.selected = 0; sp->s.setPlayheadBeats(at); pasteTrack() = ti; sp->page.paste(); }
            else if (r == 5) sp->page.addMarkerAt(at); });
    }
    static int& pasteTrack() { static int t = -1; return t; }
    void clipMenu(juce::uint32 id, double beat) {
        auto* c = A().findClip(id); if (!c) return;
        const bool many = s.selection.size() > 1;
        juce::PopupMenu m; m.addItem(1, "Split here  (S splits at the playhead)", beat > c->start && beat < c->end()); m.addItem(2, "Duplicate after  (Ctrl/Cmd + D)"); m.addItem(3, many ? "Delete selected  (Del)" : "Delete  (Del)");
        m.addItem(10, "Copy  (Ctrl/Cmd + C)"); m.addItem(11, "Cut  (Ctrl/Cmd + X)"); m.addItem(12, many ? "Bounce selection to one clip  (Ctrl/Cmd + J)" : "Bounce in place  (Ctrl/Cmd + J)");
        m.addSeparator(); m.addItem(4, "Reverse", true, c->reverse); m.addItem(5, "Mute clip  (M)", true, c->mute); m.addItem(6, "Spectral morph into the next clip (overlap them)", true, c->morph);
        m.addItem(13, "Loop the sound inside the clip", true, c->loop); m.addItem(14, "Warp to the song tempo", c->source && c->source->bpm > 0, c->warp); m.addItem(15, "Edit gain envelope  (E)", true, page.envMode);
        m.addItem(7, "Reset length to the whole sound"); m.addSeparator(); m.addItem(8, "Send this clip to Ghosty Beats"); m.addItem(9, "Re-render from its page", s.canRerender(c->id));
        juce::PopupMenu cols; for (int i = 0; i < 8; ++i) cols.addColouredItem(20 + i, "Colour " + juce::String(i + 1), page.theme.clip(i)); m.addSubMenu("Colour", cols);
        juce::Component::SafePointer<Timeline> sp(this); const double at = A().snap(beat);
        m.showMenuAsync(juce::PopupMenu::Options(), [sp, id, at](int r) { if (!sp) return; auto& a = sp->A(); auto b = a; auto* k = a.findClip(id); if (!k) return;
            switch (r) {
                case 1: sp->page.splitSelectedAt(at); break;
                case 2: sp->page.duplicateSelected(); break;
                case 3: sp->page.deleteSelected(); break;
                case 10: sp->page.copySelected(); break;
                case 11: sp->page.cutSelected(); break;
                case 12: sp->page.bounceSelected(); break;
                case 4: k->reverse = !k->reverse; sp->page.edited(b); break;
                case 5: sp->page.muteSelected(); break;
                case 6: k->morph = !k->morph; sp->page.edited(b); if (k->morph) sp->page.say("MORPH: overlap this clip with the next one on the track - the overlap becomes a spectral blend from this sound into the next (its fades rest while MORPH is on)."); break;
                case 13: k->loop = !k->loop; sp->page.edited(b); break;
                case 14: k->warp = !k->warp; sp->page.edited(b); break;
                case 15: sp->page.setEnvelopeMode(!sp->page.envMode); break;
                case 7: k->offset = 0; k->length = std::max(kMinClipBeats, k->naturalBeats(sp->bpm())); sp->page.edited(b); break;
                case 8: sp->page.toBeatsBtn.triggerClick(); break;
                case 9: sp->page.say(sp->s.rerenderClip(id) ? "Re-rendered from " + k->source->source.page + "." : "Open the " + k->source->source.page + " page to re-render this sound (it isn't loaded right now, or the file moved - the audio itself is saved with the project)."); sp->page.refresh(); break;
                default: if (r >= 20) { for (auto cid : sp->s.selection) if (auto* x = a.findClip(cid)) x->colour = r - 20; k->colour = r - 20; sp->page.edited(b); } break;
            } });
    }

    ArrangePage& page; Session& s;
    juce::ScrollBar hbar { false }, vbar { true };
};

// ================================================================== inspector
class Inspector : public juce::Component, private juce::Timer {
public:
    explicit Inspector(ArrangePage& p) : page(p), s(p.session) {
        auto slider = [this](ArrBar& sl, double lo, double hi, double step, double def, const juce::String& suffix, bool heavy, std::function<void(Clip&, double)> set) {
            sl.setTextBoxStyle(juce::Slider::TextBoxRight, false, 58, 18); sl.setRange(lo, hi, step); sl.setDoubleClickReturnValue(true, def); sl.setTextValueSuffix(suffix);
            sl.onDragStart = [this] { before = s.arrangement; dragging = true; };
            sl.onValueChange = [this, &sl, set, heavy] { if (updating) return; auto* c = s.arrangement.findClip(s.selected); if (!c) return; if (!dragging) before = s.arrangement;
                const double bpm = s.effectiveBpm(); const double natBefore = c->naturalBeats(bpm), lenBefore = c->length; set(*c, sl.getValue());
                if (!c->loop && std::abs(lenBefore - (natBefore - c->offset * bpm / 60.0)) < 1e-3) c->length = std::max(kMinClipBeats, c->naturalBeats(bpm) - c->offset * bpm / 60.0);   // a whole-sound clip follows its sound
                if (dragging) {   // pitch / stretch re-process the whole sound: at most every 150 ms while dragging
                    if (!heavy || juce::Time::getMillisecondCounterHiRes() - lastHeavy > 150) { s.changed(); lastHeavy = juce::Time::getMillisecondCounterHiRes(); flushPending = false; } else { flushPending = true; startTimer(160); }
                    page.timeline->repaint();
                } else page.edited(before); };
            sl.onDragEnd = [this] { dragging = false; flushPending = false;
                const auto* now = s.arrangement.findClip(s.selected); const auto* was = before.findClip(s.selected);
                const bool same = now && was && now->pitch == was->pitch && now->stretch == was->stretch && now->gainDb == was->gainDb && now->pan == was->pan && now->length == was->length && now->morphAmount == was->morphAmount && now->morphSmear == was->morphSmear;
                if (!same) s.history.push(before); s.changed(); page.refresh(); };
            addAndMakeVisible(sl);
        };
        slider(pitch, -24, 24, 0.01, 0, " st", true, [](Clip& c, double v) { c.pitch = (float) v; });
        slider(stretch, 0.25, 4, 0.001, 1, "x", true, [](Clip& c, double v) { c.stretch = (float) v; }); stretch.setSkewFactorFromMidPoint(1.0);
        slider(gain, -60, 24, 0.1, 0, " dB", false, [](Clip& c, double v) { c.gainDb = (float) v; }); gain.setSkewFactorFromMidPoint(-6.0);
        slider(pan, -1, 1, 0.01, 0, "", false, [](Clip& c, double v) { c.pan = (float) v; });
        pitch.hint = "Pitch"; stretch.hint = "Stretch"; gain.hint = "Clip gain"; pan.hint = "Pan"; morphAmt.hint = "Morph amount"; smear.hint = "Morph smear";
        pan.textFromValueFunction = [](double v) { return panText(v); }; pan.valueFromTextFunction = [](const juce::String& t) { return parsePanText(t); };
        gain.setTextValueSuffix({}); gain.textFromValueFunction = [](double v) { return v <= -59.95 ? juce::String("-inf dB") : juce::String(v, 1) + " dB"; }; gain.valueFromTextFunction = [](const juce::String& t) { return parseDbText(t, -60.0, 24.0); };
        slider(morphAmt, 0, 1, 0.01, 1, "", false, [](Clip& c, double v) { c.morphAmount = (float) v; });
        slider(smear, 0, 1, 0.01, 0, "", false, [](Clip& c, double v) { c.morphSmear = (float) v; });
        for (auto* pc : { &morphAmt, &smear }) { pc->textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v * 100)) + " %"; };
            pc->valueFromTextFunction = [](const juce::String& t) { return juce::jlimit(0.0, 1.0, t.retainCharacters("0123456789.").getDoubleValue() / 100.0); }; }
        auto toggle = [this](juce::TextButton& b, std::function<bool&(Clip&)> field) {
            b.setClickingTogglesState(true); addAndMakeVisible(b);
            b.onClick = [this, &b, field] { if (updating) return; auto* c = s.arrangement.findClip(s.selected); if (!c) return; auto bf = s.arrangement; const double bpm = s.effectiveBpm(); const double natBefore = c->naturalBeats(bpm), lenBefore = c->length;
                const bool v = b.getToggleState(); for (auto id : s.selection) if (auto* x = s.arrangement.findClip(id)) if (x != c) field(*x) = v;   // the whole selection
                field(*c) = v; if (!c->loop && std::abs(lenBefore - (natBefore - c->offset * bpm / 60.0)) < 1e-3) c->length = std::max(kMinClipBeats, c->naturalBeats(bpm) - c->offset * bpm / 60.0); page.edited(bf); };
        };
        toggle(keepLen, [](Clip& c) -> bool& { return c.keepLength; }); toggle(reverse, [](Clip& c) -> bool& { return c.reverse; }); toggle(morph, [](Clip& c) -> bool& { return c.morph; }); toggle(muteClip, [](Clip& c) -> bool& { return c.mute; });
        toggle(warp, [](Clip& c) -> bool& { return c.warp; }); toggle(loopClip, [](Clip& c) -> bool& { return c.loop; });
        keepLen.setTooltip("On: pitch keeps the length (time-stretch). Off: tape style - higher is shorter.");
        morph.setTooltip("Spectral morph over the overlap with the next clip on this track: the overlap becomes a blend from this sound's spectrum into the next one's.");
        reverse.setTooltip("Play the clip backwards"); muteClip.setTooltip("Mute this clip (M)");
        warp.setTooltip("Follow the song tempo: a loop made at another tempo is stretched to stay on the grid (tempo-tagged sounds only).");
        loopClip.setTooltip("Repeat the sound to fill the clip: drag the clip's right edge out to loop it.");
        envBtn.setClickingTogglesState(true); addAndMakeVisible(envBtn); envBtn.onClick = [this] { page.setEnvelopeMode(envBtn.getToggleState()); };
        envBtn.setTooltip("Draw this clip's gain envelope (E): click adds a point, drag moves, double-click deletes");
        for (auto* cb : { &fadeInCurve, &fadeOutCurve }) { for (int i = 0; i < (int) Curve::Count; ++i) cb->addItem(curveName((Curve) i), i + 1); addAndMakeVisible(*cb); }
        for (int i = 0; i < (int) MorphShape::Count; ++i) morphShape.addItem(juce::String("MORPH ") + morphShapeName((MorphShape) i), i + 1); addAndMakeVisible(morphShape);
        fadeInCurve.onChange = [this] { if (updating) return; if (auto* c = s.arrangement.findClip(s.selected)) { auto b = s.arrangement; c->fadeInCurve = (Curve) (fadeInCurve.getSelectedId() - 1); page.edited(b); } };
        fadeOutCurve.onChange = [this] { if (updating) return; if (auto* c = s.arrangement.findClip(s.selected)) { auto b = s.arrangement; c->fadeOutCurve = (Curve) (fadeOutCurve.getSelectedId() - 1); page.edited(b); } };
        morphShape.onChange = [this] { if (updating) return; if (auto* c = s.arrangement.findClip(s.selected)) { auto b = s.arrangement; c->morphShape = (MorphShape) (morphShape.getSelectedId() - 1); page.edited(b); } };
        addAndMakeVisible(rerenderBtn); rerenderBtn.onClick = [this] { if (auto* c = s.arrangement.findClip(s.selected)) { const auto pg = c->source ? c->source->source.page : juce::String(); page.say(s.rerenderClip(s.selected) ? "Re-rendered from " + pg + "." : "Nothing to re-render from: " + (pg.isEmpty() ? juce::String("the sound has no source page") : "the " + pg + " page isn't open (or the file moved; the audio itself is saved with the project)") + "."); page.refresh(); } };
        rerenderBtn.setTooltip(kRerenderTip);
        update();
    }
    void timerCallback() override { stopTimer(); if (flushPending) { flushPending = false; s.changed(); lastHeavy = juce::Time::getMillisecondCounterHiRes(); } }
    void update() {
        updating = true; auto* c = s.arrangement.findClip(s.selected); const bool on = c != nullptr;
        for (juce::Component* k : std::initializer_list<juce::Component*> { &pitch, &stretch, &gain, &pan, &keepLen, &reverse, &morph, &muteClip, &warp, &loopClip, &envBtn, &fadeInCurve, &fadeOutCurve, &morphShape, &morphAmt, &smear, &rerenderBtn }) k->setVisible(on);
        if (c) {
            pitch.setValue(c->pitch, juce::dontSendNotification); stretch.setValue(c->stretch, juce::dontSendNotification); gain.setValue(c->gainDb, juce::dontSendNotification); pan.setValue(c->pan, juce::dontSendNotification);
            morphAmt.setValue(c->morphAmount, juce::dontSendNotification); smear.setValue(c->morphSmear, juce::dontSendNotification);
            keepLen.setToggleState(c->keepLength, juce::dontSendNotification); reverse.setToggleState(c->reverse, juce::dontSendNotification); morph.setToggleState(c->morph, juce::dontSendNotification); muteClip.setToggleState(c->mute, juce::dontSendNotification);
            warp.setToggleState(c->warp, juce::dontSendNotification); warp.setEnabled(c->source && c->source->bpm > 0); loopClip.setToggleState(c->loop, juce::dontSendNotification);
            envBtn.setToggleState(page.envMode, juce::dontSendNotification);
            fadeInCurve.setSelectedId((int) c->fadeInCurve + 1, juce::dontSendNotification); fadeOutCurve.setSelectedId((int) c->fadeOutCurve + 1, juce::dontSendNotification); morphShape.setSelectedId((int) c->morphShape + 1, juce::dontSendNotification);
            for (juce::Component* k : std::initializer_list<juce::Component*> { &morphShape, &morphAmt, &smear }) k->setEnabled(c->morph);
            const bool canRe = s.canRerender(c->id); rerenderBtn.setEnabled(canRe);
            rerenderBtn.setTooltip(canRe ? juce::String(kRerenderTip) : c->source && c->source->source.page == "Bounce" ? juce::String("RE-RENDER is off: a bounced clip has no page to render it again (UNDO the bounce to get the original clips back)")
                                                                    : juce::String("RE-RENDER is off: no page can render this sound again right now"));
        }
        updating = false; repaint();
    }
    void applyTheme() {
        const auto& t = page.theme;
        styleSlider(pitch, t, t.accent); styleSlider(stretch, t, t.accent2); styleSlider(gain, t, t.clip(0)); styleSlider(pan, t, t.clip(3)); styleSlider(morphAmt, t, t.accent); styleSlider(smear, t, t.accent);
        for (auto* b : { &keepLen, &reverse, &morph, &muteClip, &warp, &loopClip, &envBtn, &rerenderBtn }) styleButton(*b, t, b == &morph || b == &envBtn ? t.accent : b == &muteClip ? juce::Colour(0xffff9f43) : t.accent2);
        for (auto* cb : { &fadeInCurve, &fadeOutCurve, &morphShape }) styleCombo(*cb, t);
        repaint();
    }
    void resized() override {
        auto r = getLocalBounds().reduced(12, 6); r.removeFromLeft(240);
        auto col = [&](int w) { auto c = r.removeFromLeft(w); r.removeFromLeft(14); return c; };
        const int y1 = r.getY() + 16, y2 = r.getY() + 54, y3 = r.getY() + 92, b2 = r.getY() + 48, b3 = r.getY() + 82;   // slider rows (label above each) / button rows
        auto c1 = col(220); pitch.setBounds(c1.getX(), y1, c1.getWidth(), 22); keepLen.setBounds(c1.getX(), b2, 106, 24); reverse.setBounds(c1.getX() + 112, b2, 96, 24); warp.setBounds(c1.getX(), b3, 70, 24); loopClip.setBounds(c1.getX() + 76, b3, 60, 24); envBtn.setBounds(c1.getX() + 142, b3, 76, 24);
        auto c2 = col(220); stretch.setBounds(c2.getX(), y1, c2.getWidth(), 22); morph.setBounds(c2.getX(), b2, 80, 24); muteClip.setBounds(c2.getX() + 86, b2, 60, 24); morphShape.setBounds(c2.getX(), b3, 160, 24);
        auto c3 = col(196); gain.setBounds(c3.getX(), y1, c3.getWidth(), 22); pan.setBounds(c3.getX(), y2, c3.getWidth(), 22); morphAmt.setBounds(c3.getX(), y3, c3.getWidth(), 22);
        auto c4 = col(std::max(140, r.getWidth() + 14)); const int cw = std::min(170, c4.getWidth()); fadeInCurve.setBounds(c4.getX(), y1, cw, 22); fadeOutCurve.setBounds(c4.getX(), y2, cw, 22); smear.setBounds(c4.getX(), y3, cw, 22);
        rerenderBtn.setBounds(12, getHeight() - 30, 110, 24);
    }
    void paint(juce::Graphics& g) override {
        const auto& t = page.theme; g.fillAll(t.panel); g.setColour(t.grid); g.drawHorizontalLine(0, 0.0f, (float) getWidth());
        auto* c = s.arrangement.findClip(s.selected); int ti = -1; s.arrangement.findClip(s.selected, &ti);
        if (!c) {
            g.setColour(t.grey); g.setFont(juce::FontOptions(13.0f));
            g.drawFittedText("Every sound you make in Spectrum can become a clip: send sounds here from the other pages, drop a file, IMPORT, or take one FROM BEATS.\n"
                             "Click a clip to edit it (Shift-click or drag a box to select more). Right-click the ruler to play from there, right-drag it for a loop (drag the loop out to export), double-click the top strip for a marker. Space plays.", getLocalBounds().reduced(16, 10), juce::Justification::centredLeft, 3);
            return;
        }
        g.setColour(t.clip(c->colour)); g.fillRoundedRectangle(12.0f, 10.0f, 6.0f, 46.0f, 3.0f);
        g.setColour(t.ink); g.setFont(juce::FontOptions(16.0f, juce::Font::bold)); g.drawText(c->name + (s.selection.size() > 1 ? "  +" + juce::String((int) s.selection.size() - 1) : juce::String()), 26, 6, 210, 22, juce::Justification::centredLeft, true);
        g.setColour(t.grey); g.setFont(juce::FontOptions(11.5f));
        const auto from = c->source ? c->source->source.page : juce::String(); const int nn = c->source ? (int) c->source->notes.size() : 0;
        g.drawText((from.isNotEmpty() ? "from " + from + "  -  " : juce::String()) + juce::String(c->source ? c->source->seconds() : 0.0, 2) + " s" + (c->source && c->source->bpm > 0 ? " @ " + juce::String(c->source->bpm, 1) + " BPM" : juce::String())
                   + (nn > 0 ? ", " + juce::String(nn) + (nn == 1 ? " note" : " notes") : juce::String()), 26, 28, 214, 16, juce::Justification::centredLeft, true);
        g.drawText("at " + posText(c->start, s.arrangement.beatsPerBar, s.effectiveBpm()).upToFirstOccurrenceOf(" ", false, false) + "  -  " + juce::String(c->length, 2) + " beats" + (ti >= 0 ? "  -  " + s.arrangement.tracks[(size_t) ti].name : juce::String()), 26, 44, 214, 16, juce::Justification::centredLeft, true);
        g.setColour(t.dim); g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
        auto lab = [&](juce::Component& k, const juce::String& txt, int dy = -14) { g.drawText(txt, k.getX(), k.getY() + dy, 220, 13, juce::Justification::left); };
        lab(pitch, "PITCH"); lab(stretch, "STRETCH"); lab(gain, "CLIP GAIN"); lab(pan, "PAN"); lab(morphAmt, "MORPH AMOUNT (0 = CROSSFADE)"); lab(smear, "MORPH SMEAR (CENTRE BLUR)");
        const bool morphing = c->morph; const juce::String rest = morphing ? "  (MORPH)" : juce::String();
        lab(fadeInCurve, "FADE IN  " + juce::String(c->fadeIn, 2) + " BEATS"); lab(fadeOutCurve, "FADE OUT  " + juce::String(c->fadeOut, 2) + " BEATS" + rest);
    }
    ArrBar pitch, stretch, gain, pan, morphAmt, smear;
    juce::TextButton keepLen { "KEEP LENGTH" }, reverse { "REVERSE" }, morph { "MORPH" }, muteClip { "MUTE" }, warp { "WARP" }, loopClip { "LOOP" }, envBtn { "GAIN ENV" }, rerenderBtn { "RE-RENDER" };
    juce::ComboBox fadeInCurve, fadeOutCurve, morphShape;
private:
    ArrangePage& page; Session& s; Arrangement before; bool updating = false, dragging = false, flushPending = false; double lastHeavy = 0;
};

// ================================================================== page
ArrangePage::ArrangePage(Session& s) : session(s), theme(Theme::make(false)) {
    timeline = std::make_unique<Timeline>(*this); inspector = std::make_unique<Inspector>(*this);
    addAndMakeVisible(*timeline); addAndMakeVisible(*inspector);
    mixChip = std::make_unique<Chip>(*this, 0, "MIX"); stemChip = std::make_unique<Chip>(*this, 1, "STEMS"); midiChip = std::make_unique<Chip>(*this, 2, "MIDI"); mixChip->setComponentID("arrangeMixChip"); stemChip->setComponentID("arrangeStemsChip"); midiChip->setComponentID("arrangeMidiChip");
    for (auto* c : { mixChip.get(), stemChip.get(), midiChip.get() }) addAndMakeVisible(*c);
    for (auto* b : { &playBtn, &loopBtn, &undoBtn, &redoBtn, &addTrackBtn, &importBtn, &fromBeatsBtn, &toBeatsBtn, &renderBtn, &themeBtn, &fitBtn }) addAndMakeVisible(*b);
    loopBtn.setClickingTogglesState(true); themeBtn.setClickingTogglesState(true);
    playBtn.onClick = [this] { togglePlay(); };
    playBtn.setTooltip("Play the arrangement here (Space). While your DAW plays, Arrange follows its timeline.");
    loopBtn.onClick = [this] { auto b = session.arrangement; session.arrangement.loopOn = loopBtn.getToggleState(); if (session.arrangement.loopEnd <= session.arrangement.loopStart) { session.arrangement.loopStart = 0; session.arrangement.loopEnd = 4.0 * session.arrangement.beatsPerBar; } edited(b); };
    loopBtn.setTooltip("Loop region (L) - internal PLAY only (your DAW keeps its own timeline). Right-drag the ruler to make one, drag its ends / middle to change it, drag its WAV / MIDI grip out to export it. With LOOP on, RENDER and MIX use the loop.");
    undoBtn.onClick = [this] { if (session.undo()) refresh(); }; redoBtn.onClick = [this] { if (session.redo()) refresh(); };
    addTrackBtn.onClick = [this] { if (session.arrangement.tracks.size() >= (size_t) kMaxTracks) { say("16 tracks is the limit."); return; } auto b = session.arrangement; session.arrangement.addTrack(); edited(b); };
    importBtn.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser>("Import sounds", juce::File(), kAudioExts);
        juce::Component::SafePointer<ArrangePage> sp(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems, [sp](const juce::FileChooser& fc) { if (!sp) return; juce::StringArray f; for (auto& x : fc.getResults()) f.add(x.getFullPathName()); sp->importFiles(f, -1, -1); });
    };
    fromBeatsBtn.onClick = [this] { fromBeatsMenu(); }; fromBeatsBtn.setTooltip("Take a sound from Documents/Ghosty/Shared Sounds (what Ghosty Beats and Spectrum share)");
    toBeatsBtn.onClick = [this] { if (!exportAllowed("spec.arrange.send-beats")) return; juce::Component::SafePointer<ArrangePage> sp(this); say(session.selected ? "Sending the clip to Shared Sounds..." : "Rendering the mix for Shared Sounds...");
        session.sendToBeatsAsync(session.selected, [sp](juce::Array<juce::File> f, juce::String err) { if (sp) sp->say(f.isEmpty() ? err : "Sent \"" + f[0].getFileName() + "\" to Shared Sounds - Ghosty Beats lists it in SHARED."); }); };
    toBeatsBtn.setTooltip("Send the selected clip (as you hear it) - or the whole mix when nothing is selected - to Ghosty Beats via Shared Sounds");
    renderBtn.onClick = [this] {
        if (session.rendering() && !autoRendering) { session.cancelRender(); say("Render cancelled."); return; }
        startUserRender(false);
    };
    renderBtn.setTooltip("Render the arrangement (or the loop region when LOOP is on) to Documents/Ghosty/Spectrum/Arrange Bounces; click again to cancel");
    themeBtn.onClick = [this] { setDark(themeBtn.getToggleState()); };
    undoBtn.setTooltip("Undo the last edit (Ctrl/Cmd + Z)"); redoBtn.setTooltip("Redo (Ctrl/Cmd + Shift + Z)"); addTrackBtn.setTooltip("Add an empty track");
    importBtn.setTooltip("Import audio files as clips (or drop files on a track)"); themeBtn.setTooltip("Light / dark look of this page");
    fitBtn.onClick = [this] { zoomToFit(); }; fitBtn.setTooltip("Zoom to show the whole arrangement (Z zooms to the selection; Ctrl/Cmd + wheel zooms, Alt + wheel sets the track height)");
    for (int i = 0; i < 8; ++i) snapBox.addItem(juce::String("GRID ") + kSnapNames[i], i + 1);
    snapBox.onChange = [this] { const int i = snapBox.getSelectedId() - 1; if (i < 0) return; auto b = session.arrangement; auto& a = session.arrangement;
        a.snapOn = kSnapVals[i] != 0; if (a.snapOn) a.snapBeats = kSnapVals[i] < 0 ? (double) a.beatsPerBar : kSnapVals[i]; edited(b); };
    addAndMakeVisible(snapBox); addAndMakeVisible(tempoLbl); addAndMakeVisible(status);
    tempoLbl.setJustificationType(juce::Justification::centred); tempoLbl.setEditable(false, true, false);
    tempoLbl.onTextChange = [this] { const double v = tempoLbl.getText().retainCharacters("0123456789.").getDoubleValue();
        if (session.hostTempoKnown() && session.followHostTempo.load()) { say("The tempo follows your DAW."); return; }
        if (v >= 20 && v <= 400) { auto b = session.arrangement; session.arrangement.bpm = v; edited(b); } };
    status.setJustificationType(juce::Justification::centredLeft);
    setWantsKeyboardFocus(true);
    applyTheme(); refresh(); startTimerHz(30);
}
ArrangePage::~ArrangePage() { stopTimer(); if (session.rendering() && autoRendering) session.cancelRender(); }

void ArrangePage::setDark(bool d) { setLook(d ? ghosty::themes::Theme::Dark : ghosty::themes::Theme::Light); }
void ArrangePage::setLook(ghosty::themes::Theme lk) { theme = Theme::make(lk); themeBtn.setToggleState(theme.dark, juce::dontSendNotification); themeBtn.setButtonText(theme.dark ? "LIGHT" : "DARK"); applyTheme(); }
void ArrangePage::setThemeButtonVisible(bool v) { themeBtn.setVisible(v); resized(); }
bool ArrangePage::exportAllowed(const char* id, bool tellUser) {
    if (allowExport && allowExport(this, id)) return true;
    if (tellUser) say("Exporting is part of the full version.");
    return false;
}
void ArrangePage::setOwnsTooltips(bool own) { ownsTooltips = own; if (!own) tips.reset(); else parentHierarchyChanged(); }
void ArrangePage::setOwnsTypeIn(bool own) { ownsTypeIn = own; if (!own) typeIn.reset(); else parentHierarchyChanged(); }
void ArrangePage::parentHierarchyChanged() {
    if (ownsTypeIn && !typeIn && getParentComponent() != nullptr) typeIn = std::make_unique<ghosty::TypeIn>(*this);   // right-click a slider = type a value
    if (!ownsTypeIn) typeIn.reset();
    if (ownsTooltips && !tips && getParentComponent() != nullptr) tips = std::make_unique<juce::TooltipWindow>(this, 600);   // only when shown, and only if the host has none
    if (!ownsTooltips) tips.reset();
}
void ArrangePage::togglePlay() {
    if (view.active && view.own && onOwnPlayToggle) { onOwnPlayToggle(); session.stop(); updateTransportUi(); return; }   // OWN: the shared clock starts / stops
    if (session.isPlaying()) session.stop(); else session.play();
    updateTransportUi();
}
void ArrangePage::setTransportView(const TransportView& v) {
    const bool changed = v.active != view.active || v.own != view.own || v.playing != view.playing || std::abs(v.bpm - view.bpm) > 1e-9;
    view = v; if (changed) updateTransportUi();
}
void ArrangePage::updateTransportUi() {
    const bool hostPlay = session.hostPlaying();
    const bool lit = session.isPlaying() || (view.active && view.playing);
    if (playBtn.getToggleState() != lit) playBtn.setToggleState(lit, juce::dontSendNotification);
    juce::String text, tip; bool editable;
    if (view.active && view.own) {
        const double b = view.bpm >= 20 ? view.bpm : session.effectiveBpm();
        text = "OWN " + juce::String(b, b == std::floor(b) ? 0 : 1) + " BPM"; editable = false;
        tip = "Tempo: Spectrum's own clock (SYNC: OWN) - set it in Spectrum's header. PLAY / Space start and stop that clock.";
        playBtn.setTooltip("Start / stop Spectrum's own clock (SYNC: OWN): Arrange and every other player run on it (Space)");
    } else if (view.active && (view.bpm >= 20 || session.hostTempoKnown())) {
        const double b = view.bpm >= 20 ? view.bpm : session.effectiveBpm();
        text = "DAW " + juce::String(b, b == std::floor(b) ? 0 : 1) + " BPM"; editable = false;
        tip = "Tempo: follows your DAW (SYNC: DAW)";
        playBtn.setTooltip("Play the arrangement here while your DAW is stopped (Space). While your DAW plays, Arrange follows its timeline.");
    } else {   // no view: as before
        const double b = session.effectiveBpm();
        text = juce::String(b, 1) + " BPM" + (hostPlay ? "  HOST" : ""); editable = !(session.followHostTempo.load() && session.hostTempoKnown());
        tip = session.hostTempoKnown() ? "Tempo: follows your DAW" : "Tempo: double-click to type one (no host tempo)";
        playBtn.setTooltip("Play the arrangement here (Space). While your DAW plays, Arrange follows its timeline.");
    }
    tempoLbl.setEditable(false, editable, false); tempoLbl.setTooltip(tip);
    if (!tempoLbl.isBeingEdited() && tempoLbl.getText() != text) tempoLbl.setText(text, juce::dontSendNotification);
}
void ArrangePage::applyTheme() {
    for (auto* b : { &playBtn, &loopBtn, &undoBtn, &redoBtn, &addTrackBtn, &importBtn, &fromBeatsBtn, &toBeatsBtn, &renderBtn, &themeBtn, &fitBtn }) styleButton(*b, theme, b == &playBtn ? theme.playhead : b == &loopBtn ? theme.loop : theme.accent);
    styleCombo(snapBox, theme);
    tempoLbl.setColour(juce::Label::textColourId, theme.ink); tempoLbl.setColour(juce::Label::backgroundColourId, theme.panel2); tempoLbl.setFont(juce::FontOptions(12.5f, juce::Font::bold));
    status.setColour(juce::Label::textColourId, theme.grey); status.setFont(juce::FontOptions(12.0f));
    inspector->applyTheme(); repaint(); timeline->repaint();
}
void ArrangePage::refresh() {
    auto& a = session.arrangement;
    undoBtn.setEnabled(session.history.canUndo()); redoBtn.setEnabled(session.history.canRedo()); loopBtn.setToggleState(a.loopOn, juce::dontSendNotification);
    int snapId = 8; if (a.snapOn) for (int i = 0; i < 7; ++i) { const double v = kSnapVals[i] < 0 ? (double) a.beatsPerBar : kSnapVals[i]; if (std::abs(v - a.snapBeats) < 1e-9) { snapId = i + 1; break; } }
    snapBox.setSelectedId(snapId, juce::dontSendNotification);
    for (auto it = session.selection.begin(); it != session.selection.end();) { if (!a.findClip(*it)) it = session.selection.erase(it); else ++it; }
    if (!a.findClip(session.selected)) session.selected = session.selection.empty() ? 0 : *session.selection.begin();
    if (session.selected) session.selection.insert(session.selected);
    renderBtn.setButtonText(a.loopOn ? "RENDER LOOP" : "RENDER");
    inspector->update(); timeline->updateBars(); timeline->repaint();
}
void ArrangePage::edited(const Arrangement& before) { session.commit(before); refresh(); }
void ArrangePage::zoomToFit() { timeline->fit(); }
void ArrangePage::setZoom(double ppb, double firstBeat) { timeline->ppb = juce::jlimit(1.5, 400.0, ppb); timeline->first = std::max(0.0, firstBeat); timeline->updateBars(); timeline->repaint(); }
void ArrangePage::say(const juce::String& msg) { statusText = msg; statusUntil = juce::Time::getMillisecondCounterHiRes() + 9000; status.setText(msg, juce::dontSendNotification); }
juce::String ArrangePage::midiChipHelp() { return "Drag MIDI into your DAW: the notes of note-based clips (for example clips sent from ARCS). VOICE clips and plain audio carry no notes."; }
juce::String ArrangePage::noMidiClipsText() { return "No note-based clips: MIDI comes from clips that carry notes (for example clips sent from ARCS)."; }
juce::String ArrangePage::rulerHint() { return "right-click: move the playhead here  |  right-drag: loop  |  drag the loop out to export"; }
juce::String ArrangePage::positionText() const { return posText(session.playheadBeats(), session.arrangement.beatsPerBar, session.effectiveBpm()); }
void ArrangePage::setEnvelopeMode(bool on) { envMode = on; if (on && !session.selected) say("Select a clip, then draw its gain envelope."); refresh(); }
void ArrangePage::addMarkerAt(double beat, const juce::String& name) {
    auto b = session.arrangement; auto& ms = session.arrangement.markers;
    ms.push_back({ clampF(beat, 0.0, kMaxBeats, 0.0), name.isNotEmpty() ? name : juce::String(std::initializer_list<const char*> { "INTRO", "BUILD", "DROP", "BREAK", "OUTRO" }.begin()[std::min<size_t>(ms.size(), 4)]) });
    std::stable_sort(ms.begin(), ms.end(), [](const Marker& x, const Marker& y) { return x.beat < y.beat; }); edited(b);
}

// ---- selection actions
void ArrangePage::duplicateSelected() {   // the selection again, right after itself
    auto& a = session.arrangement; if (session.selection.empty()) return; auto b = a;
    double s0 = 1e300, s1 = -1; for (auto id : session.selection) if (auto* c = a.findClip(id)) { s0 = std::min(s0, c->start); s1 = std::max(s1, c->end()); }
    if (s1 < 0) return; const double shift = a.snap(s1) - s0 > 0 ? a.snap(s1) - s0 : s1 - s0;
    std::set<juce::uint32> fresh; juce::uint32 prim = 0;
    for (auto id : std::set<juce::uint32>(session.selection)) if (auto* c = a.findClip(id)) { const double st = c->start; if (auto* d = a.duplicateClip(id, st + shift)) { fresh.insert(d->id); if (id == session.selected) prim = d->id; } }
    session.selection = fresh; session.selected = prim ? prim : (fresh.empty() ? 0 : *fresh.begin()); edited(b);
}
void ArrangePage::repeatSelected() { duplicateSelected(); }
void ArrangePage::deleteSelected() { auto& a = session.arrangement; auto b = a; bool any = false; for (auto id : session.selection) any |= a.removeClip(id); any |= a.removeClip(session.selected); if (any) { session.selection.clear(); session.selected = 0; edited(b); } }
void ArrangePage::splitSelectedAt(double beats) {
    auto& a = session.arrangement; auto b = a; std::set<juce::uint32> right; juce::uint32 prim = 0;
    std::set<juce::uint32> ids = session.selection; ids.insert(session.selected);
    for (auto id : ids) if (auto* c = a.findClip(id)) if (beats > c->start && beats < c->end()) if (auto* r = a.splitClip(id, beats)) { right.insert(r->id); if (id == session.selected) prim = r->id; }
    if (right.empty()) { say("Put the playhead inside the selected clip to split it."); return; }
    session.selection = right; session.selected = prim ? prim : *right.begin(); edited(b);
}
void ArrangePage::copySelected() {
    auto& a = session.arrangement; auto& cb = clipboard(); cb.clips.clear(); int topTrack = kMaxTracks; cb.base = 1e300;
    for (auto id : session.selection) { int ti = 0; if (auto* c = a.findClip(id, &ti)) { topTrack = std::min(topTrack, ti); cb.base = std::min(cb.base, c->start); } }
    for (auto id : session.selection) { int ti = 0; if (auto* c = a.findClip(id, &ti)) cb.clips.push_back({ ti - topTrack, *c }); }
    if (!cb.clips.empty()) say("Copied " + juce::String((int) cb.clips.size()) + " clip" + (cb.clips.size() > 1 ? "s" : "") + " - Ctrl/Cmd + V pastes at the playhead.");
}
void ArrangePage::cutSelected() { copySelected(); deleteSelected(); }
void ArrangePage::paste() {
    auto& cb = clipboard(); if (cb.clips.empty()) return; auto& a = session.arrangement; auto b = a;
    int track = Timeline::pasteTrack(); Timeline::pasteTrack() = -1; if (track < 0) { int ti = -1; if (a.findClip(session.selected, &ti)) track = ti; else track = 0; }
    const double at = a.snapDown(session.playheadBeats()); std::set<juce::uint32> fresh;
    for (auto& [rel, c] : cb.clips) {
        const int t = juce::jlimit(0, kMaxTracks - 1, track + rel); while (t >= (int) a.tracks.size()) a.addTrack();
        Clip k = c; k.id = a.nextId++; k.start = clampF(at + (c.start - cb.base), 0.0, kMaxBeats, 0.0); a.tracks[(size_t) t].clips.push_back(k); fresh.insert(k.id);
    }
    a.sortClips(); session.selection = fresh; session.selected = fresh.empty() ? 0 : *fresh.begin(); edited(b);
}
void ArrangePage::selectAll() { session.selection.clear(); for (auto& t : session.arrangement.tracks) for (auto& c : t.clips) session.selection.insert(c.id); if (!session.selection.count(session.selected)) session.selected = session.selection.empty() ? 0 : *session.selection.begin(); refresh(); }
void ArrangePage::nudge(double beats, int tracks) {
    auto& a = session.arrangement; if (session.selection.empty()) return; auto b = a;
    std::vector<std::pair<juce::uint32, int>> ids; int lo = 0, hi = (int) a.tracks.size() - 1; int dt = tracks;
    for (auto id : session.selection) { int ti = 0; if (a.findClip(id, &ti)) { ids.push_back({ id, ti }); dt = juce::jlimit(lo - ti, hi - ti, dt); } }
    double minStart = 1e300; for (auto& [id, ti] : ids) minStart = std::min(minStart, a.findClip(id)->start); if (minStart + beats < 0) beats = -minStart;
    for (auto& [id, ti] : ids) { auto* c = a.findClip(id); a.moveClip(id, ti + dt, c->start + beats); }
    edited(b);
}
void ArrangePage::quantiseSelected() { auto& a = session.arrangement; auto b = a; const double g = a.snapBeats > 0 ? a.snapBeats : 1.0; for (auto id : session.selection) if (auto* c = a.findClip(id)) c->start = std::max(0.0, std::round(c->start / g) * g); a.sortClips(); edited(b); }
void ArrangePage::muteSelected() { auto& a = session.arrangement; auto b = a; bool anyOn = false; for (auto id : session.selection) if (auto* c = a.findClip(id)) anyOn |= !c->mute; for (auto id : session.selection) if (auto* c = a.findClip(id)) c->mute = anyOn; edited(b); }
void ArrangePage::bounceSelected() {
    std::set<juce::uint32> ids = session.selection; if (ids.empty() && session.selected) ids.insert(session.selected); if (ids.empty()) return;
    say("Bouncing..."); const auto id = session.bounce(ids, true);
    say(id ? "Bounced into one clip (Ctrl/Cmd + Z undoes it)." : "Nothing to bounce."); refresh();
}
void ArrangePage::zoomToSelection() { double s0 = 1e300, s1 = -1; for (auto id : session.selection) if (auto* c = session.arrangement.findClip(id)) { s0 = std::min(s0, c->start); s1 = std::max(s1, c->end()); } if (s1 > 0) timeline->fitRange(s0, s1); else zoomToFit(); }
void ArrangePage::importFiles(const juce::StringArray& files, int track, double beats) {
    if (files.isEmpty()) return;
    juce::Component::SafePointer<ArrangePage> sp(this); say("Loading " + juce::String(files.size()) + " sound" + (files.size() > 1 ? "s" : "") + "...");
    session.loadSoundsAsync(files, "File", [sp, track, beats](std::vector<PayloadPtr> ps, juce::StringArray notes) { if (!sp) return;
        int n = 0, t = track; double at = beats;
        for (auto& p : ps) { const auto id = sp->session.receive(p, t, at); if (!id) continue; ++n; if (auto* c = sp->session.arrangement.findClip(id, &t)) at = sp->session.arrangement.snap(c->end()); }
        sp->say(notes.isEmpty() ? "Added " + juce::String(n) + " sound" + (n == 1 ? "" : "s") + "." : notes.joinIntoString("  ")); sp->refresh(); });
}
void ArrangePage::fromBeatsMenu() {
    const auto files = Session::listShared();
    if (files.isEmpty()) { say("Shared Sounds is empty - in Ghosty Beats or Spectrum use SEND TO BEATS / SHARE, they land in Documents/Ghosty/Shared Sounds."); return; }
    juce::PopupMenu m; for (int i = 0; i < std::min(40, files.size()); ++i) m.addItem(i + 1, files[i].getFileNameWithoutExtension());
    juce::Component::SafePointer<ArrangePage> sp(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&fromBeatsBtn), [sp, files](int r) { if (!sp || r <= 0) return;
        sp->say("Loading...");
        sp->session.loadSoundsAsync({ files[r - 1].getFullPathName() }, "Shared", [sp](std::vector<PayloadPtr> ps, juce::StringArray notes) { if (!sp) return;
            if (ps.empty()) { sp->say(notes.isEmpty() ? juce::String("Can't read that sound.") : notes[0]); return; }
            sp->session.receive(ps[0]); sp->say("Added \"" + ps[0]->name + "\" from Shared Sounds." + (notes.isEmpty() ? juce::String() : "  " + notes[0])); sp->refresh(); }); });
}
void ArrangePage::startUserRender(bool stems) {
    if (!exportAllowed(stems ? "spec.arrange.stems-drag" : "spec.arrange.render")) { pendingUser = -1; return; }
    if (session.rendering()) { session.cancelRender(); pendingUser = stems ? 1 : 0; return; }   // an automatic MIX render gives way
    RenderRequest rq; rq.stems = stems; rq.mix = !stems; rq.loopOnly = session.arrangement.loopOn;
    if (!stems) { rq.dir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Ghosty").getChildFile("Spectrum").getChildFile("Arrange Bounces"); rq.base = "Arrange " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H%M%S"); }
    autoRendering = false; juce::Component::SafePointer<ArrangePage> sp(this);
    session.startRender(rq, [sp, stems](std::shared_ptr<RenderResult> r) { if (!sp) return;
        if (r->cancelled) { sp->say("Render cancelled."); return; }
        if (r->failed) { sp->say(r->error); return; }
        const double sec = (r->toBeat - r->fromBeat) * 60.0 / std::max(20.0, r->bpm); const int secs = (int) std::lround(sec);
        const juce::String over = r->peak > 1.0f ? "  Peak " + juce::String(juce::Decibels::gainToDecibels(r->peak), 1) + " dBFS (float WAV, not clipped)." : juce::String();
        if (stems) sp->say("Stems ready (" + juce::String(r->stemFiles.size()) + " tracks) - drag STEMS out now." + over);
        else sp->say("Rendered " + juce::String(secs / 60) + ":" + juce::String(secs % 60).paddedLeft('0', 2) + (r->loopOnly ? " (loop)" : "") + " - saved " + r->mixFile.getFileName() + " in Documents/Ghosty/Spectrum/Arrange Bounces." + over); });
    say(stems ? "Rendering stems in the background..." : "Rendering in the background...");
}
juce::File ArrangePage::writeLoopMidi(const juce::File& dir) const {
    Arrangement a = session.arrangement; a.bpm = session.effectiveBpm(); auto m = arrangementMidi(a);
    const double ppq = m.getTimeFormat() > 0 ? m.getTimeFormat() : 960, t0 = a.loopStart * ppq, t1 = a.loopEnd * ppq;
    juce::MidiFile out; out.setTicksPerQuarterNote((int) ppq); int notes = 0;
    for (int ti = 0; ti < m.getNumTracks(); ++ti) {
        const auto& src = *m.getTrack(ti); juce::MidiMessageSequence seq;
        for (auto* ev : src) {
            const auto& msg = ev->message;
            if (msg.isMetaEvent()) { if (msg.getTimeStamp() <= t0 || ti == 0) { auto x = msg; x.setTimeStamp(0); seq.addEvent(x); } continue; }
            if (!msg.isNoteOn()) continue;
            const double on = msg.getTimeStamp(), off = ev->noteOffObject ? ev->noteOffObject->message.getTimeStamp() : on + 1;
            if (on >= t1 || off <= t0) continue;
            auto a1 = msg; a1.setTimeStamp(std::max(on, t0) - t0); auto b1 = juce::MidiMessage::noteOff(msg.getChannel(), msg.getNoteNumber()); b1.setTimeStamp(std::min(off, t1) - t0);
            seq.addEvent(a1); seq.addEvent(b1); ++notes;
        }
        seq.updateMatchedPairs(); out.addTrack(seq);
    }
    if (notes == 0) return {};
    dir.createDirectory(); auto f = Session::uniqueFile(dir, "Arrange loop notes", ".mid"); f.deleteFile(); juce::FileOutputStream os(f); if (!os.openedOk()) return {};
    out.writeTo(os); os.flush(); return f;   // GATE: via-caller (dragLoop asked spec.arrange.loop-midi-drag)
}
bool ArrangePage::dragLoop(bool midi) {   // the loop's grip: the highlighted loop section out as WAV / MIDI (reuses the render / MIDI export)
    if (!exportAllowed(midi ? "spec.arrange.loop-midi-drag" : "spec.arrange.loop-wav-drag")) return false;
    if (midi) {
        auto f = writeLoopMidi(Session::dragDir().getChildFile(juce::String(juce::Time::currentTimeMillis())));
        if (!f.existsAsFile()) { say("No notes inside the loop: MIDI comes from note-based clips."); return false; }
        ghosty::exportgate::dragFiles({ f.getFullPathName() }, this); return true;
    }
    const auto hash = session.audioHashNow(true);
    if (!loopRender || loopRender->revision != hash) {
        if (!session.rendering()) { RenderRequest rq; rq.loopOnly = true; juce::Component::SafePointer<ArrangePage> sp(this);
            session.startRender(rq, [sp](std::shared_ptr<RenderResult> r) { if (sp && r && !r->cancelled && !r->failed) { sp->loopRender = r; sp->say("The loop is rendered - drag its WAV grip out now."); } }); }
        say("Rendering the loop... drag its WAV grip again in a moment."); return false;
    }
    auto files = session.writeAudio(*loopRender, Session::dragDir().getChildFile(juce::String(juce::Time::currentTimeMillis())), "Arrange loop", false);
    if (files.isEmpty()) { say("Nothing to drag - the loop is silent."); return false; }
    ghosty::exportgate::dragFiles({ files[0].getFullPathName() }, this); return true;
}
void ArrangePage::dragOut(int what) {
    if (!exportAllowed(what == 2 ? "spec.arrange.midi-drag" : what == 1 ? "spec.arrange.stems-drag" : "spec.arrange.mix-drag")) return;
    if (what == 2) {
        auto f = session.writeMidi(Session::dragDir(), "Arrange notes");
        if (!f.existsAsFile()) { say(ArrangePage::noMidiClipsText()); return; }
        ghosty::exportgate::dragFiles({ f.getFullPathName() }, this); return;
    }
    const bool loopOnly = session.arrangement.loopOn; const auto hash = session.audioHashNow(loopOnly);
    auto r = what == 1 ? session.lastStems : session.lastRender;
    if (!r || r->revision != hash) {
        if (what == 1) startUserRender(true);
        else if (!session.rendering()) { autoRendering = true; RenderRequest rq; rq.loopOnly = loopOnly; session.startRender(rq, nullptr); }
        say(what == 1 ? "Rendering the stems... drag STEMS again when its dot is green." : "Rendering the mix... drag MIX again when its dot is green."); return;
    }
    auto dir = Session::dragDir().getChildFile(juce::String(juce::Time::currentTimeMillis())); auto files = session.writeAudio(*r, dir, what == 1 ? "Arrange stem" : (r->loopOnly ? "Arrange loop" : "Arrange mix"), what == 1);
    juce::StringArray paths; for (auto& f : files) paths.add(f.getFullPathName());
    if (paths.isEmpty()) { say("Nothing to drag - the arrangement is silent."); return; }
    ghosty::exportgate::dragFiles(paths, this);
}

void ArrangePage::timerCallback() {
    session.housekeeping();
    if (session.revision() != seenRevision) { seenRevision = session.revision(); refresh(); }   // a state set from a host thread, a page sending a sound...
    const bool hostPlay = session.hostPlaying();
    // the document keeps its own tempo; when the host has one, edits use the host's (no undo entries, no rebuilds: the engine follows by itself)
    if (session.followHostTempo.load() && session.hostTempoKnown()) session.arrangement.bpm = session.effectiveBpm();
    const double bpm = session.effectiveBpm();
    juce::ignoreUnused(bpm); updateTransportUi();
    // render state: keep a MIX ready for drag-out (background, ~1 s after edits settle)
    const double now = juce::Time::getMillisecondCounterHiRes();
    const bool loopOnly = session.arrangement.loopOn; const auto hash = session.audioHashNow(loopOnly);
    if (hash != seenHash) { seenHash = hash; hashSince = now; }
    if (!session.rendering()) {
        if (pendingUser >= 0) { const int w = pendingUser; pendingUser = -1; startUserRender(w == 1); }
        else if ((!session.lastRender || session.lastRender->revision != hash) && now - hashSince > 1000 && session.upToDate() && session.arrangement.clipCount() > 0
                 && !(hash == autoRefusedHash && !(exportOpen && exportOpen()))) {
            if (!allowExport || !allowExport(nullptr, "spec.arrange.auto-mix")) autoRefusedHash = hash;   // demo: no MIX on disk (silent: no user action)
            else { autoRefusedHash = 0;
            if (autoDir.isDirectory() && (!session.lastRender || session.lastRender->mixFile.getParentDirectory() != autoDir)) autoDir.deleteRecursively();
            autoRendering = true; RenderRequest rq; rq.loopOnly = loopOnly; rq.dir = Session::dragDir().getChildFile("auto-" + juce::String((juce::int64) (hash & 0xffffffff)));
            const auto prevDir = session.lastRender ? session.lastRender->mixFile.getParentDirectory() : juce::File();
            juce::Component::SafePointer<ArrangePage> sp(this);
            session.startRender(rq, [sp, prevDir](std::shared_ptr<RenderResult> r) { if (!sp) return; sp->autoRendering = false;
                if (!r->cancelled && !r->failed) { if (prevDir.isDirectory() && prevDir.getFileName().startsWith("auto-") && prevDir != r->mixFile.getParentDirectory()) prevDir.deleteRecursively(); sp->autoDir = r->mixFile.getParentDirectory(); } });
        } }   // (else: the demo gate)
    }
    mixChip->state = session.lastRender && session.lastRender->revision == hash ? 2 : session.rendering() ? 1 : 0;
    stemChip->state = session.lastStems && session.lastStems->revision == session.audioHashNow(loopOnly) ? 2 : (session.rendering() && !autoRendering) ? 1 : 0;
    midiChip->state = 2; mixChip->repaint(); stemChip->repaint();
    renderBtn.setButtonText(session.rendering() && !autoRendering ? "CANCEL " + juce::String((int) (session.renderProgress() * 100)) + "%" : (session.arrangement.loopOn ? "RENDER LOOP" : "RENDER"));
    // meters (peak, decaying)
    for (int i = 0; i < kMaxTracks; ++i) meters[(size_t) i] = std::max(session.takeTrackPeak(i), meters[(size_t) i] * 0.82f);
    masterMeter = std::max(session.takeMasterPeak(), masterMeter * 0.82f);
    if (statusText.isNotEmpty() && now > statusUntil) { statusText = {}; status.setText({}, juce::dontSendNotification); }
    if (statusText.isEmpty()) status.setText(session.upToDate() ? juce::String(session.arrangement.clipCount()) + " clips on " + juce::String((int) session.arrangement.tracks.size()) + " tracks" + (session.selection.size() > 1 ? "  -  " + juce::String((int) session.selection.size()) + " selected" : juce::String())
                                                                : juce::String("Processing sounds (pitch / stretch / warp)..."), juce::dontSendNotification);
    if (session.isPlaying() || hostPlay) { const float x = timeline->beatToX(session.playheadBeats()); if (x > timeline->getWidth() - 40 || x < Timeline::headerW) { timeline->first = std::max(0.0, session.playheadBeats() - 2.0); timeline->updateBars(); } }
    timeline->repaint(); repaint(getWidth() - 360, getHeight() - 28, 360, 28);
}

void ArrangePage::paint(juce::Graphics& g) {
    g.fillAll(theme.bg); g.setColour(theme.panel); g.fillRect(0, 0, getWidth(), 44); g.setColour(theme.grid); g.drawHorizontalLine(43, 0.0f, (float) getWidth());
    g.setColour(theme.accent); g.setFont(juce::FontOptions(15.0f, juce::Font::bold)); g.drawText("ARRANGE", 12, 0, 90, 44, juce::Justification::centredLeft);
    g.setColour(theme.dim); g.setFont(juce::FontOptions(10.5f, juce::Font::bold)); g.drawText("DRAG OUT", dragLabelX, getHeight() - 28, 80, 28, juce::Justification::centredRight);
    // master meter (left of DRAG OUT)
    auto mr = juce::Rectangle<float>((float) dragLabelX - 96, (float) getHeight() - 18, 70, 8);
    g.setColour(theme.panel2); g.fillRoundedRectangle(mr, 3); const float u = (float) juce::jlimit(0.0, 1.0, (juce::Decibels::gainToDecibels(masterMeter, -60.0f) + 60.0) / 66.0);
    g.setColour(masterMeter >= 1.0f ? theme.hot : theme.meter); g.fillRoundedRectangle(mr.withWidth(mr.getWidth() * u), 3);
    g.setColour(theme.dim); g.setFont(juce::FontOptions(9.5f, juce::Font::bold)); g.drawText("OUT", (int) mr.getX() - 26, getHeight() - 28, 24, 28, juce::Justification::centredRight);
    if (session.rendering() && !autoRendering) { auto r = renderBtn.getBounds().toFloat(); g.setColour(theme.accent); g.fillRect(r.getX(), r.getBottom() + 2, r.getWidth() * session.renderProgress(), 2.5f); }
}
void ArrangePage::resized() {
    auto r = getLocalBounds(); auto top = r.removeFromTop(44).reduced(6, 8); top.removeFromLeft(92);
    auto put = [&](juce::Component& c, int w) { c.setBounds(top.removeFromLeft(w)); top.removeFromLeft(5); };
    put(playBtn, 58); put(loopBtn, 52); put(tempoLbl, 96); put(snapBox, 104); put(fitBtn, 40); put(undoBtn, 52); put(redoBtn, 52); top.removeFromLeft(6);
    put(addTrackBtn, 72); put(importBtn, 62); put(fromBeatsBtn, 92); put(toBeatsBtn, 108); top.removeFromLeft(6); put(renderBtn, 104);
    if (themeBtn.isVisible()) themeBtn.setBounds(top.removeFromRight(60));
    auto bottom = r.removeFromBottom(28).reduced(10, 3);   // status line + meter + the drag-out chips
    midiChip->setBounds(bottom.removeFromRight(66)); bottom.removeFromRight(6); stemChip->setBounds(bottom.removeFromRight(76)); bottom.removeFromRight(6); mixChip->setBounds(bottom.removeFromRight(62));
    dragLabelX = bottom.getRight() - 84; bottom.removeFromRight(90 + 130); status.setBounds(bottom);
    inspector->setBounds(r.removeFromBottom(130));
    timeline->setBounds(r);
}
bool ArrangePage::keyPressed(const juce::KeyPress& k) {
    const auto mods = k.getModifiers(); const int code = k.getKeyCode(); const bool cmd = mods.isCommandDown(); const bool plain = !mods.isAnyModifierKeyDown();
    auto is = [&](char c) { return code == c || code == juce::CharacterFunctions::toLowerCase((juce::juce_wchar) c); };
    if (code == juce::KeyPress::spaceKey) { togglePlay(); return true; }
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) { deleteSelected(); return true; }
    if (code == juce::KeyPress::escapeKey) { session.selection.clear(); session.selected = 0; envMode = false; refresh(); return true; }
    if (cmd && is('Z')) { if (mods.isShiftDown() ? session.redo() : session.undo()) refresh(); return true; }
    if (cmd && is('Y')) { if (session.redo()) refresh(); return true; }
    if (cmd && is('D')) { duplicateSelected(); return true; }
    if (cmd && is('B')) { repeatSelected(); return true; }
    if (cmd && is('C')) { copySelected(); return true; }
    if (cmd && is('X')) { cutSelected(); return true; }
    if (cmd && is('V')) { paste(); return true; }
    if (cmd && is('A')) { selectAll(); return true; }
    if (cmd && is('J')) { bounceSelected(); return true; }
    if (plain && is('S')) { splitSelectedAt(session.playheadBeats()); return true; }
    if (plain && is('L')) { loopBtn.setToggleState(!loopBtn.getToggleState(), juce::sendNotification); return true; }
    if (plain && is('Q')) { quantiseSelected(); return true; }
    if (plain && is('M')) { muteSelected(); return true; }
    if (plain && is('E')) { setEnvelopeMode(!envMode); return true; }
    if (plain && is('Z')) { zoomToSelection(); return true; }
    const double step = mods.isAltDown() ? kMinClipBeats : (session.arrangement.snapOn && session.arrangement.snapBeats > 0 ? session.arrangement.snapBeats : 1.0);
    if (code == juce::KeyPress::leftKey) { nudge(-step, 0); return true; }
    if (code == juce::KeyPress::rightKey) { nudge(step, 0); return true; }
    if (code == juce::KeyPress::upKey) { nudge(0, -1); return true; }
    if (code == juce::KeyPress::downKey) { nudge(0, 1); return true; }
    if (code == juce::KeyPress::homeKey) { session.setPlayheadBeats(0); timeline->first = 0; timeline->updateBars(); return true; }
    if (code == '=' || code == '+') { timeline->zoomAround(1.25, (float) Timeline::headerW); return true; }
    if (code == '-') { timeline->zoomAround(0.8, (float) Timeline::headerW); return true; }
    return false;
}
bool ArrangePage::isInterestedInFileDrag(const juce::StringArray& files) { for (auto& f : files) if (juce::File(f).hasFileExtension("wav;aif;aiff;flac;ogg")) return true; return false; }
void ArrangePage::filesDropped(const juce::StringArray& files, int x, int y) {
    const auto p = timeline->getLocalPoint(this, juce::Point<int>(x, y)); int ti = timeline->trackAt(p.y);
    const double at = p.x >= Timeline::headerW ? session.arrangement.snap(std::max(0.0, timeline->xToBeat((float) p.x))) : -1.0;
    if (ti < 0 || p.y < Timeline::rulerH) ti = -1; else ti = std::min(ti, (int) session.arrangement.tracks.size());
    importFiles(files, ti, at);
}

} // namespace arr
