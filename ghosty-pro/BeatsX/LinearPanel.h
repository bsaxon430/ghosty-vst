#pragma once
// LINEAR DRUMMING panel (RHYTHM LAB > LINEAR tab): the switch, RULE, MOVE / DROP, the collision WINDOW (as a note value + ms),
// VARIATION < > with its own seed (NEW SEED rerolls), the PRIORITY order as a drag-to-reorder list with a LOCK chip per track,
// and a two-bar preview (the beat as written vs what LINEAR plays: moved hits outlined, dropped hits as ghosts).
#include "EditorParts.h"

namespace bx {

// two bars: one lane per track that plays, the hits as written (faint) and what LINEAR plays (solid; moved = outlined + a line from
// where it came from; dropped = hollow ghost)
struct LinearPreview : juce::Component {
    Ed& ed; BeatsXProcessor::LinearView v; bool on = false, showOrig = true, showDropped = true;
    explicit LinearPreview(Ed& e) : ed(e) {}
    void paint(juce::Graphics& g) override {
        auto R = getLocalBounds().toFloat(); g.setColour(CARD); g.fillRoundedRectangle(R, 8); g.setColour(PURPLE.withAlpha(0.25f)); g.drawRoundedRectangle(R.reduced(0.5f), 8, 1.0f);
        auto body = R.reduced(8, 6); const float head = 16; auto top = body.removeFromTop(head);
        std::vector<int> lanes; { int cnt[gb::kTracks] {}; for (auto& e : v.orig) ++cnt[e.track]; for (int t = 0; t < gb::kTracks; ++t) if (cnt[t]) lanes.push_back(t);
            const int fit = juce::jmax(1, (int) (body.getHeight() / 12.0f)); if ((int) lanes.size() > fit) { std::stable_sort(lanes.begin(), lanes.end(), [&](int a, int b) { return cnt[a] > cnt[b]; }); lanes.resize((size_t) fit); std::sort(lanes.begin(), lanes.end()); } }
        g.setColour(INK); g.setFont(font(10.5f));
        juce::String title = on ? "WHAT PLAYS" : "PREVIEW (LINEAR is off: this is what it would do)";
        title << "   beats " << juce::String(v.from + 1, 0) << "-" << juce::String(v.from + v.beats, 0) << (v.song ? " of the song" : v.periodic ? "  (the loop repeats every " + juce::String(v.span, v.span == std::floor(v.span) ? 0 : 2) + " beats)" : "  (a fixed " + juce::String((int) std::lround(v.span / 4)) + "-bar performance)");
        g.drawText(title, top, juce::Justification::centredLeft, true);
        if (lanes.empty()) { g.setColour(GREY); g.setFont(font(11.0f, false)); g.drawText("No hits in these beats.", body, juce::Justification::centred); return; }
        const float nameW = 74, laneH = juce::jmin(18.0f, body.getHeight() / (float) lanes.size()), x0 = body.getX() + nameW, wB = (body.getWidth() - nameW) / (float) juce::jmax(1.0, v.beats);
        for (size_t li = 0; li < lanes.size(); ++li) {
            const int t = lanes[li]; const float y = body.getY() + li * laneH;
            g.setColour(INK.withAlpha(li % 2 ? 0.03f : 0.06f)); g.fillRect(x0, y, body.getRight() - x0, laneH);
            g.setColour((ed.proc.model.linear.lock >> t & 1) ? AMBER : GREY); g.setFont(font(9.5f));
            g.drawText(juce::String(t + 1) + " " + ed.proc.model.tracks[(size_t) t].sound, juce::Rectangle<float>(body.getX(), y, nameW - 4, laneH), juce::Justification::centredLeft, true);
        }
        for (int s = 0; s <= (int) std::lround(v.beats * 4); ++s) { const float x = x0 + s * 0.25f * wB; g.setColour(INK.withAlpha(s % 16 == 0 ? 0.35f : s % 4 == 0 ? 0.18f : 0.06f)); g.drawVerticalLine((int) x, body.getY(), body.getY() + laneH * lanes.size()); }
        auto laneOf = [&](int t) { for (size_t i = 0; i < lanes.size(); ++i) if (lanes[i] == t) return (int) i; return -1; };
        auto xOf = [&](double b) { return x0 + (float) (b - v.from) * wB; };
        if (showOrig) for (auto& e : v.orig) { const int li = laneOf(e.track); if (li < 0) continue; const float y = body.getY() + li * laneH; g.setColour(trackColour(e.track).withAlpha(0.22f)); g.fillRect(xOf(e.beat) - 1.5f, y + 2, 3.0f, laneH - 4); }
        for (auto& e : v.lin) {
            const int li = laneOf(e.track); if (li < 0) continue; const float y = body.getY() + li * laneH, x = xOf(e.flags & gb::linear::FlagDropped ? e.orig : e.beat);
            if (e.flags & gb::linear::FlagDropped) { if (showDropped) { g.setColour(trackColour(e.track).withAlpha(0.55f)); g.drawRect(x - 2.5f, y + 3, 5.0f, laneH - 6, 1.0f); } continue; }
            g.setColour(trackColour(e.track)); g.fillRoundedRectangle(x - 2.5f, y + 2, 5.0f, laneH - 4, 1.5f);
            if (e.flags & gb::linear::FlagMoved) { g.setColour(INK); g.drawRoundedRectangle(x - 3.5f, y + 1, 7.0f, laneH - 2, 2, 1.2f); g.setColour(INK.withAlpha(0.35f)); g.drawLine(xOf(e.orig), y + laneH * 0.5f, x, y + laneH * 0.5f, 1.0f); }
            if (e.flags & gb::linear::FlagLocked) { g.setColour(AMBER); g.fillRect(x - 2.5f, y + 1, 5.0f, 2.0f); }
        }
    }
};

// the PRIORITY order (first wins a collision) as two columns of 8 rows; drag a row to move it; each row has its LOCK chip
struct LinearOrder : juce::Component {
    Ed& ed; int dragFrom = -1, dragTo = -1; std::function<void()> changed;
    explicit LinearOrder(Ed& e) : ed(e) {}
    std::array<int, 16> order() const { return gb::linear::resolvedOrder(ed.proc.model.linear, ed.proc.roles()); }
    juce::Rectangle<float> row(int r) const { const float w = (getWidth() - 18) / 4.0f, h = (getHeight() - 2) / 4.0f; return { (r / 4) * (w + 6), 1 + (r % 4) * h, w, h - 2 }; }
    juce::Rectangle<float> lockChip(int r) const { auto rr = row(r); return rr.removeFromRight(44).reduced(3, 2); }
    int rowAt(juce::Point<float> p) const { for (int r = 0; r < 16; ++r) if (row(r).contains(p)) return r; return -1; }
    void paint(juce::Graphics& g) override {
        const auto o = order(); const auto& L = ed.proc.model.linear; bool any = false; for (auto v : L.order) if (v) any = true;
        for (int r = 0; r < 16; ++r) {
            const int t = o[(size_t) r]; auto rr = row(r); const bool locked = L.lock >> t & 1;
            g.setColour(r == dragTo ? PURPLE.withAlpha(0.3f) : CARD2); g.fillRoundedRectangle(rr, 4);
            g.setColour(trackColour(t)); g.fillRoundedRectangle(rr.withWidth(4), 2);
            g.setColour(GREY); g.setFont(font(9.0f)); g.drawText("#" + juce::String(r + 1), rr.withTrimmedLeft(7).withWidth(22), juce::Justification::centredLeft);
            g.setColour(INK); g.setFont(font(9.5f)); g.drawText(juce::String(t + 1) + " " + ed.proc.model.tracks[(size_t) t].sound, rr.withTrimmedLeft(28).withTrimmedRight(46), juce::Justification::centredLeft, true);
            auto c = lockChip(r); g.setColour(locked ? AMBER.withAlpha(0.9f) : CARD); g.fillRoundedRectangle(c, 4); g.setColour(locked ? AMBER : DIM); g.drawRoundedRectangle(c, 4, 1.0f);
            g.setColour(locked ? ON : GREY); g.setFont(font(8.5f)); g.drawText(locked ? "LOCKED" : "lock", c, juce::Justification::centred);
        }
        if (!any) { g.setColour(GREY.withAlpha(0.8f)); g.setFont(font(9.0f, false)); g.drawText("(automatic order)", getLocalBounds().toFloat().removeFromBottom(10), juce::Justification::centredRight); }
    }
    void mouseDown(const juce::MouseEvent& e) override {
        const int r = rowAt(e.position); if (r < 0) return; const int t = order()[(size_t) r];
        if (lockChip(r).contains(e.position)) { ed.edit([&] { ed.proc.model.linear.lock ^= (uint16_t) (1u << t); }); if (changed) changed(); repaint(); return; }
        dragFrom = r; dragTo = r; repaint();
    }
    void mouseDrag(const juce::MouseEvent& e) override { if (dragFrom < 0) return; const int r = rowAt(e.position); if (r >= 0 && r != dragTo) { dragTo = r; repaint(); } }
    void mouseUp(const juce::MouseEvent&) override {
        if (dragFrom >= 0 && dragTo >= 0 && dragTo != dragFrom) { const int t = order()[(size_t) dragFrom]; ed.edit([&] { gb::linear::moveInOrder(ed.proc.model.linear, ed.proc.roles(), t, dragTo); }); if (changed) changed(); }
        dragFrom = dragTo = -1; repaint();
    }
};

struct LinearPanel : juce::Component {
    Ed& ed; LinearPreview preview { ed }; LinearOrder orderList { ed };
    juce::TextButton onBtn { "LINEAR OFF" }, moveBtn { "MOVE losers" }, dropBtn { "DROP losers" }, varPrev { "<" }, varNext { ">" }, newSeed { "NEW SEED" }, autoOrder { "AUTOMATIC ORDER" }, showOrig { "SHOW AS WRITTEN" }, showDrop { "SHOW DROPPED" };
    juce::ComboBox rule; Bar strict { juce::Slider::LinearBar, juce::Slider::NoTextBox };
    juce::Label lRule, lWin, winTxt, lVar, varTxt, lOrder, stats, help;
    double shownFrom = -1;
    explicit LinearPanel(Ed& e) : ed(e) {
        auto lab = [this](juce::Label& l, const juce::String& t, bool bold = true) { l.setText(t, juce::dontSendNotification); l.setFont(font(11.0f, bold)); l.setColour(juce::Label::textColourId, bold ? GREY : INK); addAndMakeVisible(l); };
        lab(lRule, "RULE (who wins a collision)"); lab(lWin, "COLLISION WINDOW"); lab(lVar, "VARIATION"); lab(lOrder, "PRIORITY ORDER (drag to reorder; first wins)  |  LOCK = that track always plays"); lab(winTxt, "", false); lab(varTxt, "", false); lab(stats, "", false);
        lab(help, "LINEAR makes the beat one-voice-at-a-time, like a drummer who never hits two things at once. Hits closer than the window collide: the RULE picks one, the others MOVE to the nearest free step (flams, rolls and retrig bursts move together) or DROP. "
                  "The beat itself is never edited (switch off = exactly as written) and every change is one UNDO step. A loop resolves the same way every time, and DRAG MIDI / AUDIO hold exactly what plays. "
                  "Live pads, MIDI drum notes and NOTE REPEAT are played as you play them (LINEAR shapes the pattern / song).", false);
        help.setJustificationType(juce::Justification::topLeft); help.setFont(font(10.5f, false)); help.setColour(juce::Label::textColourId, GREY);
        onBtn.setClickingTogglesState(false); onBtn.getProperties().set("tint", (juce::int64) GREEN.getARGB());
        onBtn.onClick = [this] { auto& L = ed.proc.model.linear; ed.edit([&] { gb::linear::setOn(L, !L.on, ed.proc.model.toolSeed); });
                                 if (ed.say) ed.say(L.on ? "LINEAR on: one voice per instant; VARIATION < > for other versions (each keeps its seed)." : "LINEAR off: the beat exactly as written."); };
        for (auto* b : { &moveBtn, &dropBtn }) { b->setClickingTogglesState(false); b->setRadioGroupId(7311); }
        moveBtn.onClick = [this] { if (!ed.proc.model.linear.move) ed.edit([&] { ed.proc.model.linear.move = true; }); };
        dropBtn.onClick = [this] { if (ed.proc.model.linear.move) ed.edit([&] { ed.proc.model.linear.move = false; }); };
        varPrev.onClick = [this] { auto L = ed.proc.model.linear; if (gb::linear::stepVariation(L, -1)) ed.edit([&] { ed.proc.model.linear = L; }); };
        varNext.onClick = [this] { auto L = ed.proc.model.linear; if (gb::linear::stepVariation(L, 1)) ed.edit([&] { ed.proc.model.linear = L; }); };
        newSeed.onClick = [this] { ed.edit([&] { ed.proc.model.linear.seed = 1 + juce::Random::getSystemRandom().nextInt(99998); }); if (ed.say) ed.say("New seed: every VARIATION is a new set of versions (UNDO brings the old seed back)."); };
        autoOrder.onClick = [this] { bool any = false; for (auto v : ed.proc.model.linear.order) if (v) any = true; if (any) ed.edit([&] { ed.proc.model.linear.order.fill(0); }); };
        rule.addItemList(gb::linear::ruleNames(), 1); rule.onChange = [this] { const int r = rule.getSelectedItemIndex(); if (r >= 0 && r != ed.proc.model.linear.rule) ed.edit([&] { ed.proc.model.linear.rule = (uint8_t) r; }); };
        strict.setRange(0, 100, 1); strict.setColour(juce::Slider::trackColourId, GREEN.withAlpha(0.45f)); strict.setDoubleClickReturnValue(true, 25);
        strict.onDragStart = [this] { ed.proc.pushUndo(); };   // (Bar: once per gesture: drag, double-click, a burst of wheel steps, a typed value)
        strict.onValueChange = [this] { const float v = (float) strict.getValue() / 100.0f; auto& L = ed.proc.model.linear; if (std::abs(v - L.strictness) < 1e-6f) return;
                                        L.strictness = v; ed.live(); syncText(); };
        for (auto* b : { &showOrig, &showDrop }) { b->setClickingTogglesState(true); b->setToggleState(true, juce::dontSendNotification); }
        showOrig.onClick = [this] { preview.showOrig = showOrig.getToggleState(); preview.repaint(); };
        showDrop.onClick = [this] { preview.showDropped = showDrop.getToggleState(); preview.repaint(); };
        orderList.changed = [this] { sync(); };
        for (juce::Component* c : std::initializer_list<juce::Component*> { &onBtn, &moveBtn, &dropBtn, &varPrev, &varNext, &newSeed, &autoOrder, &showOrig, &showDrop, &rule, &strict, &preview, &orderList }) addAndMakeVisible(c);
        sync();
    }
    void syncText() {
        const auto& L = ed.proc.model.linear;
        winTxt.setText(juce::String(juce::roundToInt(L.strictness * 100)) + "% of a 1/16  =  " + gb::linear::windowText(L, ed.proc.readParams(ed.proc.liveBpm()).bpm), juce::dontSendNotification);
        varTxt.setText(L.variation == 0 ? juce::String("VAR 0  (the rule only)") : "VAR " + juce::String(L.variation) + "  (seed " + juce::String(juce::jmax(1, L.seed)) + ")", juce::dontSendNotification);
    }
    void sync() {
        const auto& L = ed.proc.model.linear;
        onBtn.setButtonText(L.on ? "LINEAR ON" : "LINEAR OFF"); onBtn.setToggleState(L.on, juce::dontSendNotification);
        moveBtn.setToggleState(L.move, juce::dontSendNotification); dropBtn.setToggleState(!L.move, juce::dontSendNotification);
        rule.setSelectedItemIndex(L.rule, juce::dontSendNotification); if (!strict.isMouseButtonDown()) strict.setValue(L.strictness * 100.0, juce::dontSendNotification);
        varPrev.setEnabled(L.variation > 0); varNext.setEnabled(L.variation < 99);
        syncText(); shownFrom = -2; tick(); orderList.repaint();
    }
    void tick() {   // editor timer: follow the playhead two bars at a time (the preview is cheap: it reads the resolved performance)
        if (!isVisible()) return;
        const double b = ed.proc.engine.uiBeat.load(); const double beat = b >= 0 ? b : 0.0;
        auto v = ed.proc.linearView(beat);
        if (v.from == shownFrom && shownFrom >= 0 && v.lin.size() == preview.v.lin.size() && v.orig.size() == preview.v.orig.size() && v.st.kept == preview.v.st.kept && v.st.dropped == preview.v.st.dropped) return;
        shownFrom = v.from; preview.v = std::move(v); preview.on = ed.proc.model.linear.on;
        const auto& st = preview.v.st;
        stats.setText((preview.on ? "" : "(off)  ") + juce::String("whole ") + (preview.v.song ? "song" : preview.v.periodic ? "loop" : "performance") + ": " + juce::String(st.in) + " hits  ->  kept " + juce::String(st.kept) + "  |  moved " + juce::String(st.moved) + "  |  dropped " + juce::String(st.dropped)
                      + (st.overflow ? "  |  " + juce::String(st.overflow) + " NOT RESOLVED" : juce::String()), juce::dontSendNotification);
        preview.repaint();
    }
    void paint(juce::Graphics& g) override { g.setColour(CARD2.withAlpha(0.35f)); g.fillRoundedRectangle(getLocalBounds().toFloat(), 8); }
    void resized() override {
        auto r = getLocalBounds().reduced(8, 6); const int w = r.getWidth();
        auto row = [&](int h) { auto x = r.removeFromTop(h); r.removeFromTop(4); return x; };
        { auto a = row(30); onBtn.setBounds(a.removeFromLeft(130)); a.removeFromLeft(10); stats.setBounds(a); }
        { auto a = row(16); lRule.setBounds(a.removeFromLeft(w / 2)); lWin.setBounds(a); }
        { auto a = row(26); auto lft = a.removeFromLeft(w / 2 - 6); rule.setBounds(lft.removeFromLeft(lft.getWidth() - 0)); a.removeFromLeft(6); strict.setBounds(a); }
        { auto a = row(26); auto lft = a.removeFromLeft(w / 2 - 6); moveBtn.setBounds(lft.removeFromLeft(lft.getWidth() / 2 - 2)); lft.removeFromLeft(4); dropBtn.setBounds(lft); a.removeFromLeft(6); winTxt.setBounds(a); }
        { auto a = row(26); lVar.setBounds(a.removeFromLeft(76)); varPrev.setBounds(a.removeFromLeft(28)); a.removeFromLeft(4); varNext.setBounds(a.removeFromRight(110).removeFromLeft(0)); newSeed.setBounds(a.removeFromRight(100)); a.removeFromRight(6);
          varNext.setBounds(a.removeFromRight(28)); a.removeFromRight(4); varTxt.setBounds(a); }
        const int helpH = 58, orderH = 4 * 22 + 4;
        auto helpR = r.removeFromBottom(helpH); help.setBounds(helpR);
        auto ord = r.removeFromBottom(orderH); r.removeFromBottom(2); auto ordHead = r.removeFromBottom(20);
        lOrder.setBounds(ordHead.removeFromLeft(ordHead.getWidth() - 132)); autoOrder.setBounds(ordHead.withTrimmedLeft(4)); orderList.setBounds(ord);
        { auto a = r.removeFromBottom(22); r.removeFromBottom(4); showOrig.setBounds(a.removeFromLeft(140)); a.removeFromLeft(4); showDrop.setBounds(a.removeFromLeft(120)); }
        preview.setBounds(r);
    }
};

} // namespace bx
