#pragma once
// GHOST HUM - the roll (build 2).
//   MELODY   an FL-style piano roll: thick clear grid (bar lines strongest, then beat, then step), black-key rows darker, C rows
//            highlighted and labelled, notes as glowing green bars with an outline and a velocity tint. Drag a note (pitch + time), drag
//            its right edge to resize, double-click an empty spot to add, right-click for the menu, wheel = velocity.
//   BEATBOX  an FL-style step sequencer: one square pad per step (16 per bar at 1/16; the QUANTIZE setting picks the step; more bars
//            scroll), grouped in 4s with alternating beat shading and thick bar lines. A hit is a lit glowing pad whose brightness is its
//            velocity (with a small level bar); a "not sure" hit is faint with a dotted ring. Click a pad to add or remove a hit, drag a
//            hit to another pad or lane. QUANTIZE OFF (or ORIGINAL) shows the exact timing as an offset glow inside the pad.
//   Both     drag-box selection, Shift / Cmd-click to add to the selection, Alt-drag to copy, Cmd+A / C / V / D, Delete, arrows (Shift =
//            an octave / a bigger step). Every edit is one UNDO. The roll shows what you hear: ON GRID = notes where QUANTIZE / SWING put
//            them, ORIGINAL = exactly as performed. Takes are seconds + MIDI underneath; the grid is a view.
#include "HumProcessor.h"
#include "HumLook.h"
#include <algorithm>
#include <set>

namespace hum::ui {

struct Roll : juce::Component, juce::SettableTooltipClient, private juce::Timer {
    HumProcessor& proc; std::function<void()> onEdit; std::function<bool(const juce::KeyPress&)> onKey; juce::String hint;
    std::vector<int> selected; int sel = -1;                       // the selection (indices into the take) and the primary one (last clicked)
    enum Drag { None, Move, Resize, Velocity, Box, Scroll }; Drag drag = None; bool dragged = false, pushed = false; Take start; juce::Point<float> boxA, boxB; int scrollStart = 0;
    double cursorT = -1;                                           // where you last clicked: Cmd+V pastes there
    int lo = 48, hi = 84; double secs = 4; int firstStep = 0;      // melody range; the take's length; the step sequencer's scroll
    static constexpr int kKeyW = 40, kLaneLabelW = 84, kHeadH = 22, kScrollH = 9; static constexpr float kMinPad = 22.0f;
    struct Clip { std::vector<Note> notes; std::vector<Hit> hits; } clip;
    bool lastPlaying = false;

    explicit Roll(HumProcessor& p) : proc(p) { startTimerHz(30); setWantsKeyboardFocus(true); setBufferedToImage(true); }
    void timerCallback() override { const bool pl = proc.isPlaying(); if (pl || lastPlaying) { if (pl && !melody()) followPlayhead(); repaint(); } lastPlaying = pl; }

    // ---- the take and the view
    Take& T() { return proc.take(); } const Take& T() const { return proc.take(); }
    bool melody() const { return proc.mode() == Melody; }
    Grid G() const { return proc.grid(); }
    bool original() const { return proc.playOriginal(); }
    double place(double t) const { return original() ? t : G().place(t, T()); }
    double bpm() const { return T().bpm > 0 ? T().bpm : 120.0; }
    double stepBeats() const { const int q = proc.quant(); return q == QOff ? 0.25 : quantBeats(q); }
    double stepLen() const { return stepBeats() * 60.0 / bpm(); }
    int stepsPerBeat() const { return juce::jmax(1, (int) std::lround(1.0 / stepBeats())); }
    int k0() const { return (int) std::floor((0.0 - T().origin) / stepLen() + 1e-9); }                       // the first pad's step (the recording's start)
    int stepCount() const { const auto& t = T(); int kMax = (int) std::ceil((juce::jmax(2.0, t.seconds) - t.origin) / stepLen()) - 1;
        for (auto& h : t.hits) kMax = juce::jmax(kMax, stepOf(h.t)); const int n = kMax - k0() + 1; return juce::jmax(stepsPerBeat() * 4, ((n + stepsPerBeat() * 4 - 1) / (stepsPerBeat() * 4)) * stepsPerBeat() * 4); }   // whole bars
    int stepOf(double t) const { return (int) std::lround((place(t) - T().origin) / stepLen()); }
    double timeOfStep(int k) const { return T().origin + k * stepLen(); }
    juce::Rectangle<int> area() const { auto a = getLocalBounds().withTrimmedLeft(melody() ? kKeyW : kLaneLabelW).withTrimmedTop(kHeadH).reduced(0, 3); if (!melody() && scrollable()) a.removeFromBottom(kScrollH + 2); return a; }
    float cellW() const { const auto a = area(); const int n = stepCount(); const float full = a.getWidth() / (float) juce::jmax(1, n); return full >= kMinPad ? full : kMinPad; }
    int visibleSteps() const { return juce::jmax(1, (int) std::floor(area().getWidth() / cellW())); }
    bool scrollable() const { const auto a = getLocalBounds().withTrimmedLeft(kLaneLabelW).withTrimmedTop(kHeadH); return a.getWidth() / (float) juce::jmax(1, stepCount()) < kMinPad; }
    int maxFirst() const { return juce::jmax(0, stepCount() - visibleSteps()); }
    void clampScroll() { firstStep = juce::jlimit(0, maxFirst(), firstStep); }
    void followPlayhead() { if (!scrollable()) return; const int k = stepOf(proc.playPosition()) - k0(); if (k < firstStep || k >= firstStep + visibleSteps()) { firstStep = (k / visibleSteps()) * visibleSteps(); clampScroll(); } }
    void fit() {   // the visible range: the notes' range padded to at least 2 octaves; time = the take (at least 2 s)
        const auto& t = T(); secs = juce::jmax(2.0, t.seconds); for (auto& n : t.notes) secs = juce::jmax(secs, n.t + n.len + 0.1);
        if (melody()) { int a = 127, b = 0; for (auto& n : t.notes) { a = juce::jmin(a, n.midi); b = juce::jmax(b, n.midi); } if (a > b) { a = 57; b = 76; }
            while (b - a < 24) { if (a > 0) --a; if (b - a < 24 && b < 127) ++b; } lo = juce::jmax(0, a - 2); hi = juce::jmin(127, b + 2); } else clampScroll(); }
    // time <-> x (melody: linear over the take; beatbox: steps)
    float xOf(double t) const { const auto a = area(); if (melody()) return a.getX() + (float) (t / secs) * a.getWidth(); return a.getX() + (float) ((t - T().origin) / stepLen() - k0() - firstStep + 0.5) * cellW(); }
    double tOf(float x) const { const auto a = area(); if (melody()) return juce::jlimit(0.0, secs, (x - a.getX()) / juce::jmax(1.0f, (float) a.getWidth()) * secs); return juce::jmax(0.0, T().origin + ((x - a.getX()) / cellW() - 0.5 + k0() + firstStep) * stepLen()); }
    float rowH() const { return melody() ? area().getHeight() / (float) (hi - lo + 1) : area().getHeight() / (float) kDrums; }
    float yOfMidi(int m) const { return area().getBottom() - (m - lo + 1) * rowH(); }
    int midiOfY(float y) const { return juce::jlimit(0, 127, lo + (int) std::floor((area().getBottom() - y) / rowH())); }
    float yOfDrum(int d) const { return area().getY() + d * rowH(); }
    int drumOfY(float y) const { return juce::jlimit(0, kDrums - 1, (int) std::floor((y - area().getY()) / rowH())); }
    int colOfX(float x) const { return (int) std::floor((x - area().getX()) / cellW()) + firstStep; }           // pad column (0-based over the whole take)
    juce::Rectangle<float> padRect(int col, int drum) const { const float w = cellW(), h = rowH(); const float x = area().getX() + (col - firstStep) * w, y = yOfDrum(drum); const float in = juce::jlimit(1.5f, 4.0f, juce::jmin(w, h) * 0.12f); return juce::Rectangle<float>(x, y, w, h).reduced(in); }
    juce::Rectangle<float> noteRect(const Note& n) const { const double t0 = place(n.t), t1 = juce::jmax(t0 + 0.03, place(n.t + n.len)); return { xOf(t0), yOfMidi(n.midi) + 1.0f, juce::jmax(4.0f, xOf(t1) - xOf(t0)), juce::jmax(2.0f, rowH() - 2.0f) }; }
    juce::Rectangle<float> hitRect(const Hit& h) const { return padRect(stepOf(h.t) - k0(), h.drum); }
    int indexAt(juce::Point<float> p) const {
        const auto& t = T(); if (melody()) { for (int i = (int) t.notes.size() - 1; i >= 0; --i) if (noteRect(t.notes[(size_t) i]).expanded(2.0f, 0.0f).contains(p)) return i; }
        else { if (!area().toFloat().contains(p)) return -1; const int col = colOfX(p.x), d = drumOfY(p.y); for (int i = (int) t.hits.size() - 1; i >= 0; --i) if (t.hits[(size_t) i].drum == d && stepOf(t.hits[(size_t) i].t) - k0() == col) return i; }
        return -1; }
    bool isSelected(int i) const { return std::find(selected.begin(), selected.end(), i) != selected.end(); }
    void select(int i, bool add = false) { if (!add) selected.clear(); if (i >= 0 && !isSelected(i)) selected.push_back(i); sel = i; repaint(); }
    void clearSelection() { selected.clear(); sel = -1; repaint(); }

    // ---- painting
    void paint(juce::Graphics& g) override {
        fit(); const auto& t = T(); const auto a = area(); const bool mel = melody(); const auto acc = look::accent(mel), glow = look::accentGlow(mel);
        glassPanel(g, getLocalBounds().toFloat().reduced(1.0f), 14.0f, acc, 0.9f, 0.70f);
        // title + hint
        g.setFont(look::label(10.5f)); glowText(g, mel ? "YOUR MELODY" : "YOUR BEAT", juce::Rectangle<int>(14, 3, 200, kHeadH - 3), juce::Justification::centredLeft, look::ink, acc, 0.8f);
        g.setColour(look::dim); g.setFont(look::font(10.5f)); g.drawFittedText(hint, juce::Rectangle<int>(120, 3, getWidth() - 134, kHeadH - 3), juce::Justification::centredRight, 1);
        g.saveState(); g.reduceClipRegion(getLocalBounds().withTrimmedTop(kHeadH));
        if (mel) paintPianoRoll(g, t, a, look::green, look::greenGlow); else paintSequencer(g, t, a, acc, glow);   // (notes are green in both modes; the teal is the melody ring)
        // the selection box
        if (drag == Box) { auto b = juce::Rectangle<float>(boxA, boxB); g.setColour(acc.withAlpha(0.12f)); g.fillRect(b); g.setColour(acc.withAlpha(0.8f)); g.drawRect(b, 1.0f); }
        // playhead
        if (proc.isPlaying()) { const float x = xOf(proc.playPosition()); if (x >= a.getX() - 1 && x <= a.getRight() + 1) { g.setColour(glow.withAlpha(0.25f)); g.fillRect(x - 2.0f, (float) a.getY(), 5.0f, (float) a.getHeight()); g.setColour(look::white.withAlpha(0.85f)); g.fillRect(x - 0.75f, (float) a.getY(), 1.5f, (float) a.getHeight()); } }
        g.restoreState();
    }
    void paintPianoRoll(juce::Graphics& g, const Take& t, juce::Rectangle<int> a, juce::Colour acc, juce::Colour glow) {
        const float rh = rowH(); const bool bigRows = rh >= 7.0f;
        // rows: black-key rows darker, C rows highlighted; the key strip on the left
        for (int m = lo; m <= hi; ++m) { const int pc = m % 12; const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; const float y = yOfMidi(m);
            g.setColour(black ? look::bg.withAlpha(0.55f) : look::white.withAlpha(0.035f)); g.fillRect((float) a.getX(), y, (float) a.getWidth(), rh);
            if (pc == 0) { g.setColour(acc.withAlpha(0.10f)); g.fillRect((float) a.getX(), y, (float) a.getWidth(), rh); g.setColour(acc.withAlpha(0.35f)); g.fillRect((float) a.getX(), y + rh - 1.0f, (float) a.getWidth(), 1.0f); }
            g.setColour(black ? juce::Colour(0xff0a100d) : look::ink.withAlpha(0.88f)); g.fillRect(4.0f, y + 0.5f, (float) kKeyW - 8.0f, rh - 1.0f);
            if (pc == 0 && bigRows) { g.setColour(look::bg); g.setFont(look::font(juce::jmin(10.0f, rh - 1.0f), true)); g.drawText("C" + juce::String(m / 12 - 1), 4, (int) y, kKeyW - 10, (int) rh, juce::Justification::centredRight); } }
        if (!bigRows) for (int m = lo; m <= hi; ++m) if (m % 12 == 0) { const float y = yOfMidi(m); g.setColour(acc.withAlpha(0.9f)); g.setFont(look::small(9.0f)); g.drawText("C" + juce::String(m / 12 - 1), a.getX() + 3, (int) (y - 6), 30, 12, juce::Justification::centredLeft); }
        // the grid: bar lines strongest, then beats, then steps (the QUANTIZE step; 1/16 when off)
        if (t.bpm > 0) { const double beat = 60.0 / t.bpm, step = stepBeats() * beat;
            for (double b = t.origin - std::floor(t.origin / step) * step; b <= secs; b += step) { const double k = std::round((b - t.origin) / step); const double kb = k * stepBeats(); const bool bar = std::abs(kb / 4.0 - std::round(kb / 4.0)) < 1e-6, isBeat = std::abs(kb - std::round(kb)) < 1e-6;
                g.setColour(bar ? look::ink.withAlpha(0.32f) : isBeat ? acc.withAlpha(0.22f) : look::ink.withAlpha(0.07f)); g.fillRect(xOf(b) - (bar ? 1.0f : 0.5f), (float) a.getY(), bar ? 2.0f : 1.0f, (float) a.getHeight()); }
            // bar numbers along the top of the area
            g.setFont(look::small(9.0f)); for (double b = t.origin - std::floor(t.origin / (4 * beat)) * 4 * beat, n = 1; b <= secs; b += 4 * beat, ++n) { if (b < 0) continue; g.setColour(look::dim.withAlpha(0.8f)); g.drawText(juce::String((int) n), (int) xOf(b) + 3, a.getY() + 1, 24, 10, juce::Justification::centredLeft); } }
        // notes: glowing bars with an outline and a velocity tint
        int i = 0; for (auto& n : t.notes) { auto r = noteRect(n); const bool s = isSelected(i++); const auto fill = acc.interpolatedWith(look::greenDeep, 0.55f * (1.0f - n.vel)).interpolatedWith(look::white, s ? 0.25f : 0.0f);
            for (int k = 2; k >= 1; --k) { g.setColour(glow.withAlpha((s ? 0.22f : 0.10f + 0.10f * n.vel) / (float) k)); g.fillRoundedRectangle(r.expanded(2.0f * (float) k, 1.5f * (float) k), 3.0f + (float) k); }
            g.setColour(fill.withAlpha(0.85f + 0.15f * n.vel)); g.fillRoundedRectangle(r, 3.0f); g.setColour((s ? look::white : glow).withAlpha(s ? 0.95f : 0.8f)); g.drawRoundedRectangle(r.reduced(0.5f), 3.0f, s ? 1.4f : 1.0f);
            if (r.getWidth() > 10 && r.getHeight() > 5) { g.setColour(look::white.withAlpha(0.35f)); g.fillRect(r.getX() + 2.0f, r.getY() + 1.5f, r.getWidth() - 4.0f, 1.0f); }
            if (std::abs(n.cents) > 0.5f && r.getWidth() > 18 && r.getHeight() > 8) { g.setColour(look::onGreen.withAlpha(0.8f)); g.setFont(look::font(9.0f)); g.drawText((n.cents > 0 ? "+" : "") + juce::String((int) n.cents), r.toNearestInt(), juce::Justification::centred); } }
    }
    void paintSequencer(juce::Graphics& g, const Take& t, juce::Rectangle<int> a, juce::Colour acc, juce::Colour glow) {
        const float rh = rowH(), cw = cellW(); const int n = stepCount(), spb = stepsPerBeat(), vis = visibleSteps(), k0_ = k0();
        // lane labels
        for (int d = 0; d < kDrums; ++d) { const float y = yOfDrum(d); g.setColour(d % 2 ? look::white.withAlpha(0.025f) : look::bg.withAlpha(0.35f)); g.fillRect((float) a.getX(), y, (float) a.getWidth(), rh);
            g.setFont(look::label(10.0f)); glowText(g, drumName(d), juce::Rectangle<int>(12, (int) y, kLaneLabelW - 16, (int) rh), juce::Justification::centredLeft, look::ink.withAlpha(0.9f), acc, 0.4f); }
        // pads: alternating beat shading, thick bar lines, beat lines
        const bool exact = proc.quant() == QOff || original() || proc.quantStrength() < 0.999f;
        for (int col = firstStep; col < juce::jmin(n, firstStep + vis + 1); ++col) { const int k = col + k0_; const int beatIx = (int) std::floor((double) k / spb + 1e-9); const bool altBeat = ((beatIx % 2) + 2) % 2 == 1; const bool barStart = ((k % (spb * 4)) + spb * 4) % (spb * 4) == 0, beatStart = ((k % spb) + spb) % spb == 0;
            const float x = a.getX() + (col - firstStep) * cw;
            if (altBeat) { g.setColour(look::white.withAlpha(0.03f)); g.fillRect(x, (float) a.getY(), cw, (float) a.getHeight()); }
            for (int d = 0; d < kDrums; ++d) { const auto pr = padRect(col, d); g.setColour(look::cell.withAlpha(0.75f)); g.fillRoundedRectangle(pr, 3.0f); g.setColour(look::white.withAlpha(0.05f)); g.drawRoundedRectangle(pr.reduced(0.5f), 3.0f, 1.0f); }
            if (barStart) { g.setColour(look::ink.withAlpha(0.35f)); g.fillRect(x - 1.0f, (float) a.getY(), 2.0f, (float) a.getHeight()); g.setColour(look::dim.withAlpha(0.9f)); g.setFont(look::small(9.0f)); g.drawText(juce::String(k / (spb * 4) + 1), (int) x + 4, a.getY() - kHeadH + 10, 24, 10, juce::Justification::centredLeft); }
            else if (beatStart) { g.setColour(acc.withAlpha(0.18f)); g.fillRect(x - 0.5f, (float) a.getY(), 1.0f, (float) a.getHeight()); } }
        // hits: lit glowing pads, brightness = velocity, a level bar; unsure = faint with a dotted ring; exact timing = an offset marker
        int i = 0; for (auto& h : t.hits) { const int idx = i++; const int col = stepOf(h.t) - k0_; if (col < firstStep || col >= firstStep + vis + 1) continue; const auto pr = padRect(col, h.drum); const bool s = isSelected(idx), unsure = h.conf < 0.5f;
            const float bright = unsure ? 0.30f : 0.45f + 0.55f * h.vel;
            if (!unsure || s) for (int k = 3; k >= 1; --k) { g.setColour(glow.withAlpha((s ? 0.28f : 0.08f + 0.12f * h.vel) / (float) k)); g.fillRoundedRectangle(pr.expanded(2.5f * (float) k), 3.0f + 2.0f * (float) k); }
            g.setColour(acc.interpolatedWith(look::greenDeep, 0.5f * (1.0f - h.vel)).withAlpha(bright)); g.fillRoundedRectangle(pr, 3.0f);
            { juce::ColourGradient sheen(look::white.withAlpha(0.25f * bright), pr.getX(), pr.getY(), look::white.withAlpha(0.0f), pr.getX(), pr.getBottom(), false); g.setGradientFill(sheen); g.fillRoundedRectangle(pr, 3.0f); }
            if (unsure) { juce::Path p; p.addRoundedRectangle(pr.expanded(1.5f), 4.0f); juce::PathStrokeType st(1.2f); const float dash[] { 3.0f, 3.0f }; juce::Path dp; st.createDashedStroke(dp, p, dash, 2); g.setColour(acc.withAlpha(0.85f)); g.fillPath(dp); }
            else { g.setColour((s ? look::white : glow).withAlpha(s ? 0.95f : 0.7f)); g.drawRoundedRectangle(pr.reduced(0.5f), 3.0f, s ? 1.5f : 1.0f); }
            if (pr.getHeight() > 12) { const float bw = juce::jmax(3.0f, (pr.getWidth() - 8.0f) * h.vel); g.setColour(look::bg.withAlpha(0.45f)); g.fillRoundedRectangle(pr.getX() + 4.0f, pr.getBottom() - 5.0f, pr.getWidth() - 8.0f, 2.5f, 1.0f); g.setColour(look::white.withAlpha(0.85f)); g.fillRoundedRectangle(pr.getX() + 4.0f, pr.getBottom() - 5.0f, bw, 2.5f, 1.0f); }
            if (exact) { const double off = (place(h.t) - timeOfStep(col + k0_)) / stepLen(); if (std::abs(off) > 0.02) { const float mx = pr.getCentreX() + (float) off * cw; g.setColour(glow.withAlpha(0.35f)); g.fillRect(juce::jmin(mx, pr.getCentreX()), pr.getY() + 2.0f, std::abs(mx - pr.getCentreX()), pr.getHeight() - 4.0f); g.setColour(look::white.withAlpha(0.9f)); g.fillRect(mx - 1.0f, pr.getY() + 1.0f, 2.0f, pr.getHeight() - 2.0f); } } }
        // the scrollbar (more bars than fit)
        if (scrollable()) { const auto sb = scrollBar(); g.setColour(look::cell.withAlpha(0.8f)); g.fillRoundedRectangle(sb.toFloat(), 4.0f); const auto th = thumb(); g.setColour(acc.withAlpha(0.7f)); g.fillRoundedRectangle(th, 4.0f); }
    }
    juce::Rectangle<int> scrollBar() const { return getLocalBounds().withTrimmedLeft(kLaneLabelW).reduced(0, 3).removeFromBottom(kScrollH); }
    juce::Rectangle<float> thumb() const { const auto sb = scrollBar().toFloat(); const int n = stepCount(), vis = visibleSteps(); const float w = juce::jmax(24.0f, sb.getWidth() * vis / (float) juce::jmax(1, n)); return sb.withX(sb.getX() + (sb.getWidth() - w) * firstStep / (float) juce::jmax(1, maxFirst())).withWidth(w); }

    // ---- editing
    void push() { if (!pushed) { proc.beginEdit(); pushed = true; } }
    void resized() override { fit(); }
    void mouseDown(const juce::MouseEvent& e) override {
        grabKeyboardFocus(); fit(); auto& t = T(); const int i = indexAt(e.position); dragged = false; pushed = false; drag = None; cursorT = area().contains(e.getPosition()) ? tOf(e.position.x) : cursorT;
        if (!melody() && scrollable() && scrollBar().contains(e.getPosition())) { drag = Scroll; scrollStart = firstStep; if (!thumb().contains(e.position)) { firstStep = (int) ((e.position.x - scrollBar().getX()) / scrollBar().getWidth() * stepCount()) - visibleSteps() / 2; clampScroll(); scrollStart = firstStep; repaint(); } return; }
        if (e.mods.isPopupMenu()) { if (i >= 0) { if (!isSelected(i)) select(i); menu(i); } return; }
        const bool extend = e.mods.isShiftDown() || e.mods.isCommandDown();
        if (i < 0) { if (e.getNumberOfClicks() >= 2 && area().contains(e.getPosition()) && melody()) { add(e.position); return; }
            if (!melody() && area().contains(e.getPosition()) && !extend && !e.mods.isAltDown()) { addHitAt(colOfX(e.position.x), drumOfY(e.position.y)); return; }   // a pad: add a hit
            if (!extend) clearSelection(); drag = Box; boxA = boxB = e.position; repaint(); return; }
        if (extend) { if (isSelected(i)) { selected.erase(std::remove(selected.begin(), selected.end(), i), selected.end()); sel = selected.empty() ? -1 : selected.back(); repaint(); return; } select(i, true); }
        else if (!isSelected(i)) select(i); else { sel = i; repaint(); }
        start = t;
        if (melody()) { const auto& n = t.notes[(size_t) i]; const auto r = noteRect(n); drag = (e.position.x > r.getRight() - 7.0f && r.getWidth() > 14.0f && selected.size() == 1) ? Resize : Move; proc.auditionNote(n.midi); }
        else { drag = (e.mods.isShiftDown() && !extend) ? Velocity : Move; proc.auditionDrum(t.hits[(size_t) i].drum); }
        if (drag == Move && e.mods.isAltDown()) { duplicateSelectionInPlace(); start = t; }   // Alt-drag: a copy comes along, the originals stay
        setMouseCursor(drag == Resize ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor); repaint();
    }
    void mouseMove(const juce::MouseEvent& e) override { const int i = indexAt(e.position); bool edge = false; if (i >= 0 && melody()) { const auto r = noteRect(T().notes[(size_t) i]); edge = e.position.x > r.getRight() - 7.0f && r.getWidth() > 14.0f; }
        setMouseCursor(edge ? juce::MouseCursor::LeftRightResizeCursor : i >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor); }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (drag == None) return; auto& t = T();
        if (drag == Scroll) { firstStep = scrollStart + (int) std::lround(e.getDistanceFromDragStartX() / juce::jmax(1.0f, (float) scrollBar().getWidth()) * stepCount()); clampScroll(); repaint(); return; }
        if (drag == Box) { boxB = e.position; repaint(); return; }
        if (e.getDistanceFromDragStart() < 3 && !dragged) return; dragged = true; push();
        if (melody()) { const double dt = (e.getDistanceFromDragStartX() / juce::jmax(1.0f, (float) area().getWidth())) * secs; const int dm = (int) std::round(-e.getDistanceFromDragStartY() / rowH());
            if (drag == Resize && sel >= 0) { t.notes[(size_t) sel].len = juce::jmax(0.03, start.notes[(size_t) sel].len + dt); }
            else if (drag == Move) { double minT = 1e9; for (int i : selected) minT = juce::jmin(minT, start.notes[(size_t) i].t); const double ddt = juce::jmax(dt, -minT); int pitched = -1;
                for (int i : selected) { auto& n = t.notes[(size_t) i]; const auto& s0 = start.notes[(size_t) i]; n.t = s0.t + ddt; const int m = juce::jlimit(0, 127, s0.midi + dm); if (m != n.midi) { n.midi = m; if (i == sel) pitched = m; } }
                if (pitched >= 0) proc.auditionNote(pitched); } }
        else { if (drag == Velocity) { for (int i : selected) t.hits[(size_t) i].vel = juce::jlimit(0.1f, 1.0f, start.hits[(size_t) i].vel - e.getDistanceFromDragStartY() / 120.0f); }
            else if (drag == Move && sel >= 0) { const int col = juce::jlimit(0, stepCount() - 1, colOfX(e.position.x)), d = drumOfY(e.position.y); const auto& p0 = start.hits[(size_t) sel];
                const int dCol = col - (stepOf(p0.t) - k0()), dD = d - p0.drum; int newDrum = -1;
                for (int i : selected) { auto& h = t.hits[(size_t) i]; const auto& s0 = start.hits[(size_t) i]; const double within = place(s0.t) - timeOfStep(stepOf(s0.t));   // keeps the exact offset inside the step
                    h.t = juce::jmax(0.0, timeOfStep(stepOf(s0.t) + dCol) + within); const int nd = juce::jlimit(0, kDrums - 1, s0.drum + dD); if (nd != h.drum) { h.drum = nd; h.conf = 1; if (i == sel) newDrum = nd; } }
                if (newDrum >= 0) proc.auditionDrum(newDrum); } }
        repaint();
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (drag == Box) { auto b = juce::Rectangle<float>(boxA, boxB); const bool add = e.mods.isShiftDown() || e.mods.isCommandDown(); if (!add) selected.clear();
            const auto& t = T(); if (melody()) { for (int i = 0; i < (int) t.notes.size(); ++i) if (b.intersects(noteRect(t.notes[(size_t) i])) && !isSelected(i)) selected.push_back(i); }
            else { for (int i = 0; i < (int) t.hits.size(); ++i) if (b.intersects(hitRect(t.hits[(size_t) i])) && !isSelected(i)) selected.push_back(i); }
            sel = selected.empty() ? -1 : selected.back(); drag = None; repaint(); return; }
        if (drag == Scroll) { drag = None; return; }
        if (dragged) finish();
        else if (drag == Move && !melody() && sel >= 0 && !e.mods.isShiftDown() && !e.mods.isCommandDown() && !e.mods.isAltDown() && e.getNumberOfClicks() == 1) { removeSelected(); }   // a plain click on a lit pad: the hit goes
        drag = None;
    }
    // after an edit the take is sorted: find the selected notes / hits again by what they are
    void finish() {
        auto& t = T(); std::vector<Note> kn; std::vector<Hit> kh; if (melody()) { for (int i : selected) if (i >= 0 && i < (int) t.notes.size()) kn.push_back(t.notes[(size_t) i]); } else { for (int i : selected) if (i >= 0 && i < (int) t.hits.size()) kh.push_back(t.hits[(size_t) i]); }
        const int primary = sel; Note pn; Hit ph; if (primary >= 0 && primary < t.count()) { if (melody()) pn = t.notes[(size_t) primary]; else ph = t.hits[(size_t) primary]; }
        double last = 0; for (auto& n : t.notes) last = juce::jmax(last, n.t + n.len); for (auto& h : t.hits) last = juce::jmax(last, h.t + 0.3); t.seconds = juce::jmax(t.seconds, last + 0.1);
        proc.edited(); selected.clear(); sel = -1;
        auto sameN = [](const Note& a, const Note& b) { return a.t == b.t && a.midi == b.midi && a.len == b.len && a.vel == b.vel; }; auto sameH = [](const Hit& a, const Hit& b) { return a.t == b.t && a.drum == b.drum && a.vel == b.vel; };
        if (melody()) { for (auto& k : kn) for (int i = 0; i < (int) t.notes.size(); ++i) if (!isSelected(i) && sameN(t.notes[(size_t) i], k)) { selected.push_back(i); break; } if (primary >= 0) for (int i = 0; i < (int) t.notes.size(); ++i) if (sameN(t.notes[(size_t) i], pn)) { sel = i; break; } }
        else { for (auto& k : kh) for (int i = 0; i < (int) t.hits.size(); ++i) if (!isSelected(i) && sameH(t.hits[(size_t) i], k)) { selected.push_back(i); break; } if (primary >= 0) for (int i = 0; i < (int) t.hits.size(); ++i) if (sameH(t.hits[(size_t) i], ph)) { sel = i; break; } }
        if (sel < 0 && !selected.empty()) sel = selected.back(); pushed = false; if (onEdit) onEdit(); repaint();
    }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override {
        fit(); const int i = indexAt(e.position);
        if (i < 0 || std::abs(w.deltaX) > std::abs(w.deltaY) || (e.mods.isShiftDown() && !melody())) { if (!melody() && scrollable()) { const float d = std::abs(w.deltaX) > std::abs(w.deltaY) ? w.deltaX : w.deltaY; firstStep += d < 0 ? 4 : -4; clampScroll(); repaint(); } return; }
        auto& t = T(); proc.beginEdit(); const float d = w.deltaY > 0 ? 0.08f : -0.08f; if (!isSelected(i)) select(i);
        for (int k : selected) { if (melody()) t.notes[(size_t) k].vel = juce::jlimit(0.1f, 1.0f, t.notes[(size_t) k].vel + d); else t.hits[(size_t) k].vel = juce::jlimit(0.1f, 1.0f, t.hits[(size_t) k].vel + d); }
        proc.status = "Velocity"; proc.edited(); if (onEdit) onEdit(); repaint(); }
    void add(juce::Point<float> p) { auto& t = T(); proc.beginEdit(); const double at = tOf(p.x);
        Note n; n.t = at; n.len = t.bpm > 0 ? 60.0 / t.bpm * juce::jmax(0.25, quantBeats(proc.quant())) : 0.25; n.midi = midiOfY(p.y); n.vel = 0.8f; t.notes.push_back(n); proc.auditionNote(n.midi);
        selected.clear(); selected.push_back((int) t.notes.size() - 1); sel = selected.back(); proc.status = "Added a note"; finish(); }
    void addHitAt(int col, int drum) { auto& t = T(); proc.beginEdit(); Hit h; h.t = juce::jmax(0.0, timeOfStep(col + k0())); h.drum = drum; h.vel = 0.8f; h.conf = 1; t.hits.push_back(h); proc.auditionDrum(drum);
        selected.clear(); selected.push_back((int) t.hits.size() - 1); sel = selected.back(); proc.status = "Added a hit"; finish(); }
    void removeSelected() { auto& t = T(); if (selected.empty()) return; proc.beginEdit(); std::vector<int> idx = selected; std::sort(idx.begin(), idx.end()); idx.erase(std::unique(idx.begin(), idx.end()), idx.end());
        for (int k = (int) idx.size() - 1; k >= 0; --k) { const int i = idx[(size_t) k]; if (melody()) { if (i < (int) t.notes.size()) t.notes.erase(t.notes.begin() + i); } else if (i < (int) t.hits.size()) t.hits.erase(t.hits.begin() + i); }
        proc.status = idx.size() == 1 ? (melody() ? "Note removed" : "Hit removed") : juce::String((int) idx.size()) + " removed"; selected.clear(); sel = -1; pushed = false; proc.edited(); if (onEdit) onEdit(); repaint(); }
    void remove(int i) { select(i); removeSelected(); }
    void selectAll() { selected.clear(); for (int i = 0; i < T().count(); ++i) selected.push_back(i); sel = selected.empty() ? -1 : selected.back(); repaint(); }
    // Cmd+C / Cmd+V: the clipboard holds the selection relative to its first event; paste lands where you last clicked (or after the take)
    bool copy() { clip = {}; const auto& t = T(); if (selected.empty()) return false; double t0 = 1e9; for (int i : selected) t0 = juce::jmin(t0, melody() ? t.notes[(size_t) i].t : t.hits[(size_t) i].t);
        for (int i : selected) { if (melody()) { auto n = t.notes[(size_t) i]; n.t -= t0; clip.notes.push_back(n); } else { auto h = t.hits[(size_t) i]; h.t -= t0; clip.hits.push_back(h); } }
        proc.status = "Copied " + juce::String((int) selected.size()); if (onEdit) onEdit(); return true; }
    double snapT(double t) const { const double s = stepLen(); if (T().bpm <= 0 || s <= 0) return juce::jmax(0.0, t); return juce::jmax(0.0, T().origin + std::round((t - T().origin) / s) * s); }
    bool paste() { auto& t = T(); if ((melody() && clip.notes.empty()) || (!melody() && clip.hits.empty())) return false; proc.beginEdit();
        double at = cursorT >= 0 ? snapT(cursorT) : snapT(t.empty() ? 0.0 : endOfTake()); selected.clear();
        if (melody()) for (auto n : clip.notes) { n.t += at; t.notes.push_back(n); selected.push_back((int) t.notes.size() - 1); } else for (auto h : clip.hits) { h.t += at; t.hits.push_back(h); selected.push_back((int) t.hits.size() - 1); }
        sel = selected.back(); proc.status = "Pasted"; finish(); return true; }
    double endOfTake() const { const auto& t = T(); double e = 0; for (auto& n : t.notes) e = juce::jmax(e, n.t + n.len); for (auto& h : t.hits) e = juce::jmax(e, h.t + stepLen()); return e; }
    // Cmd+D: the selection (or the whole take) again right after itself; with a tempo the copy starts on the next bar line
    bool duplicate() { auto& t = T(); if (t.empty()) return false; if (selected.empty()) selectAll(); proc.beginEdit(); double a = 1e9, b = 0;
        for (int i : selected) { if (melody()) { a = juce::jmin(a, t.notes[(size_t) i].t); b = juce::jmax(b, t.notes[(size_t) i].t + t.notes[(size_t) i].len); } else { a = juce::jmin(a, t.hits[(size_t) i].t); b = juce::jmax(b, t.hits[(size_t) i].t + stepLen()); } }
        double shift = b - a; if (t.bpm > 0) { const double bar = 4 * 60.0 / t.bpm; shift = juce::jmax(bar, std::ceil((b - a) / bar - 1e-6) * bar); }
        std::vector<int> fresh; for (int i : selected) { if (melody()) { auto n = t.notes[(size_t) i]; n.t += shift; t.notes.push_back(n); fresh.push_back((int) t.notes.size() - 1); } else { auto h = t.hits[(size_t) i]; h.t += shift; t.hits.push_back(h); fresh.push_back((int) t.hits.size() - 1); } }
        selected = fresh; sel = selected.back(); proc.status = "Duplicated"; finish(); return true; }
    void duplicateSelectionInPlace() { auto& t = T(); push(); std::vector<int> fresh; for (int i : selected) { if (melody()) { t.notes.push_back(t.notes[(size_t) i]); fresh.push_back((int) t.notes.size() - 1); } else { t.hits.push_back(t.hits[(size_t) i]); fresh.push_back((int) t.hits.size() - 1); } }
        if (sel >= 0) { const auto it = std::find(selected.begin(), selected.end(), sel); sel = it != selected.end() ? fresh[(size_t) (it - selected.begin())] : fresh.back(); } selected = fresh; }
    // arrows: move the selection (left / right = one step, Shift = one beat; up / down = a semitone or a lane, Shift = an octave)
    bool nudge(int dx, int dy, bool big) { auto& t = T(); if (selected.empty()) return false; proc.beginEdit(); const double dt = dx * (big ? 60.0 / bpm() : stepLen()); double minT = 1e9;
        for (int i : selected) minT = juce::jmin(minT, melody() ? t.notes[(size_t) i].t : t.hits[(size_t) i].t); const double ddt = juce::jmax(dt, -minT); int heard = -1;
        for (int i : selected) { if (melody()) { auto& n = t.notes[(size_t) i]; n.t += ddt; n.midi = juce::jlimit(0, 127, n.midi + dy * (big ? 12 : 1)); heard = n.midi; } else { auto& h = t.hits[(size_t) i]; h.t = juce::jmax(0.0, h.t + ddt); if (dy != 0) { h.drum = juce::jlimit(0, kDrums - 1, h.drum - dy); h.conf = 1; heard = h.drum; } } }
        if (heard >= 0 && dy != 0) { if (melody()) proc.auditionNote(heard); else proc.auditionDrum(heard); } proc.status = "Moved"; finish(); return true; }
    bool keyPressed(const juce::KeyPress& k) override { return onKey ? onKey(k) : false; }
    void menu(int i) {
        juce::PopupMenu m; const bool mel = melody();
        if (!mel) { for (int d = 0; d < kDrums; ++d) m.addItem(100 + d, juce::String("Make it a ") + drumName(d), true, T().hits[(size_t) i].drum == d); m.addSeparator(); }
        m.addItem(201, "Soft"); m.addItem(202, "Medium"); m.addItem(203, "Hard"); m.addSeparator();
        if (mel) { m.addItem(301, "Up an octave"); m.addItem(302, "Down an octave"); m.addSeparator(); }
        m.addItem(401, "Copy  (Cmd+C)"); m.addItem(402, "Duplicate  (Cmd+D)"); m.addSeparator();
        m.addItem(1, selected.size() > 1 ? "Delete selected" : (mel ? "Delete note" : "Delete hit"));
        m.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(juce::Rectangle<int>(juce::Desktop::getMousePosition(), juce::Desktop::getMousePosition().translated(1, 1))),
            [sp = juce::Component::SafePointer<Roll>(this), i](int r) { if (!sp || r == 0) return; auto& t = sp->T(); if (i < 0 || i >= t.count()) return;
                if (r == 1) { sp->removeSelected(); return; } if (r == 401) { sp->copy(); return; } if (r == 402) { sp->duplicate(); return; } sp->proc.beginEdit();
                for (int k : sp->selected) { if (r >= 100 && r < 100 + kDrums) { t.hits[(size_t) k].drum = r - 100; t.hits[(size_t) k].conf = 1; }
                    else if (r >= 201 && r <= 203) { const float v = r == 201 ? 0.4f : r == 202 ? 0.7f : 1.0f; if (sp->melody()) t.notes[(size_t) k].vel = v; else t.hits[(size_t) k].vel = v; }
                    else if (r == 301 || r == 302) { t.notes[(size_t) k].midi = juce::jlimit(0, 127, t.notes[(size_t) k].midi + (r == 301 ? 12 : -12)); } }
                if (r >= 100 && r < 100 + kDrums) sp->proc.auditionDrum(r - 100); sp->finish(); });
    }
};

} // namespace hum::ui
