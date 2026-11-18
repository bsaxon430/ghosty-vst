#pragma once
// MIX page: 16 channel strips (meter, fader, pan, mute / solo) + the master strip. TRACK LEVELS come from the engine's
// per-track peak meters (written on the audio thread as atomics, read here at 30 Hz; nothing is allocated or locked).
#include "EditorParts.h"

namespace bx {

struct MixPage : juce::Component {
    Ed& ed; int dragT = -1, dragKind = 0; float dragStart = 0; juce::Point<float> downPos;
    float hold[gb::kTracks + 2] {}; int holdAge[gb::kTracks + 2] {};
    std::function<void(float)> setMaster; std::function<float()> getMaster;   // the VOLUME parameter
    static constexpr int kMasterW = 92, kGap = 4, kTopH = 34, kPanH = 18, kBotH = 26;
    explicit MixPage(Ed& e) : ed(e) {}

    juce::Rectangle<int> strip(int t) const {   // t == kTracks: master
        const int sw = (getWidth() - kMasterW - kGap * (gb::kTracks + 1)) / gb::kTracks;
        if (t >= gb::kTracks) return { getWidth() - kMasterW, 0, kMasterW, getHeight() };
        return { t * (sw + kGap), 0, sw, getHeight() };
    }
    juce::Rectangle<int> faderArea(int t) const { auto r = strip(t); r.removeFromTop(kTopH + kPanH + 4); r.removeFromBottom(kBotH + 4); return r.reduced(2, 0); }
    juce::Rectangle<int> panArea(int t) const { auto r = strip(t); r.removeFromTop(kTopH); return r.removeFromTop(kPanH).reduced(6, 2); }
    juce::Rectangle<int> muteBox(int t) const { auto r = strip(t).removeFromBottom(kBotH).reduced(3, 2); return r.removeFromLeft(r.getWidth() / 2 - 1); }
    juce::Rectangle<int> soloBox(int t) const { auto r = strip(t).removeFromBottom(kBotH).reduced(3, 2); return r.removeFromRight(r.getWidth() / 2 - 1); }

    // fader law: 0 .. 2.0 gain over the strip, with unity (1.0) at 75 % so the useful range is wide
    static float gainToPos(float g) { return g <= 1.0f ? 0.75f * std::sqrt(juce::jlimit(0.0f, 1.0f, g)) : 0.75f + 0.25f * juce::jlimit(0.0f, 1.0f, (g - 1.0f)); }
    static float posToGain(float p) { p = juce::jlimit(0.0f, 1.0f, p); return p <= 0.75f ? (p / 0.75f) * (p / 0.75f) : 1.0f + (p - 0.75f) * 4.0f; }
    static float meterPos(float peak) { const float db = juce::Decibels::gainToDecibels(peak, -60.0f); return juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 66.0f); }   // -60 .. +6 dB

    void drawMeter(juce::Graphics& g, juce::Rectangle<float> r, float peak, int slot, juce::Colour col) {
        if (GLASS) ghosty::glass::recessed(g, r, juce::jmin(4.0f, r.getWidth() * 0.5f)); else { g.setColour(CARD2); g.fillRoundedRectangle(r, 3); }   // GHOST: a glass scope well
        const float p = meterPos(peak); if (hold[slot] <= p || ++holdAge[slot] > 24) { hold[slot] = p; holdAge[slot] = 0; }
        if (p > 0.001f) { auto f = r.withTrimmedTop(r.getHeight() * (1.0f - p)); g.setColour(peak > 0.95f ? RED : (peak > 0.7f ? AMBER : col).withAlpha(0.9f)); g.fillRoundedRectangle(f, 3); }
        if (hold[slot] > 0.001f) { const float y = r.getBottom() - r.getHeight() * hold[slot]; g.setColour(INK.withAlpha(0.7f)); g.fillRect(r.getX(), y - 1, r.getWidth(), 2.0f); }
        for (float db : { 0.0f, -6.0f, -12.0f, -24.0f, -42.0f }) { const float y = r.getBottom() - r.getHeight() * (db + 60.0f) / 66.0f; g.setColour(INK.withAlpha(db == 0.0f ? 0.35f : 0.15f)); g.fillRect(r.getX(), y, r.getWidth(), 1.0f); }
    }
    void paint(juce::Graphics& g) override {
        const auto& E = ed.proc.engine;
        for (int t = 0; t < gb::kTracks; ++t) {
            const auto& tr = ed.proc.model.tracks[(size_t) t]; auto s = strip(t).toFloat(); const auto col = trackColour(t);
            if (GLASS) { glassui::sprites().draw(g, s, 10.0f, glassui::Strip); if (t == ed.track) { g.setColour(PURPLE.withAlpha(0.12f)); g.fillRoundedRectangle(s, 10.0f); } }
            else { g.setColour(t == ed.track ? PURPLE.withAlpha(0.14f) : CARD2.withAlpha(0.45f)); g.fillRoundedRectangle(s, 8); }
            // name
            g.setColour(col); g.fillRoundedRectangle(s.getX() + 6, 5, s.getWidth() - 12, 3, 1.5f);
            g.setColour(tr.mute ? DIM : INK); g.setFont(font(10.0f)); g.drawFittedText(juce::String(t + 1) + " " + (tr.sound.isEmpty() ? juce::String("-") : tr.sound), strip(t).withTrimmedTop(9).withHeight(kTopH - 10).reduced(3, 0), juce::Justification::centredTop, 2, 0.8f);
            // pan
            auto pa = panArea(t).toFloat(); g.setColour(CARD2); g.fillRoundedRectangle(pa, 3); g.setColour(INK.withAlpha(0.2f)); g.fillRect(pa.getCentreX() - 0.5f, pa.getY(), 1.0f, pa.getHeight());
            const float px = pa.getX() + (tr.pan + 1) * 0.5f * pa.getWidth(); g.setColour(col); g.fillRoundedRectangle(px - 3, pa.getY() + 1, 6, pa.getHeight() - 2, 2);
            // meter + fader
            auto fa = faderArea(t).toFloat(); const float mw = juce::jmin(10.0f, fa.getWidth() * 0.3f);
            drawMeter(g, fa.removeFromLeft(mw).reduced(0, 2), E.trackPeak[t].load(), t, col);
            fa.removeFromLeft(3);
            g.setColour(CARD2); g.fillRoundedRectangle(fa.withSizeKeepingCentre(6, fa.getHeight()), 3);
            const float u = fa.getBottom() - 10 - (fa.getHeight() - 20) * 0.75f; g.setColour(INK.withAlpha(0.35f)); g.fillRect(fa.getX(), u, fa.getWidth(), 1.0f);   // unity mark
            const float y = fa.getBottom() - 10 - (fa.getHeight() - 20) * gainToPos(tr.vol);
            auto knob = juce::Rectangle<float>(fa.getCentreX() - juce::jmin(14.0f, fa.getWidth() * 0.5f), y - 9, juce::jmin(28.0f, fa.getWidth()), 18);
            if (GLASS) { ghosty::glass::shadow(g, knob, 5.0f, 3.0f, 1.0f, 3); ghosty::glass::body(g, knob, 5.0f, t == dragT && dragKind == 1 ? col : juce::Colours::white, t == dragT && dragKind == 1 ? 0.9f : 0.75f); }
            else { g.setColour(t == dragT && dragKind == 1 ? col : CARD); g.fillRoundedRectangle(knob, 4); } g.setColour(col.withAlpha(0.9f)); g.drawRoundedRectangle(knob, 4, 1.2f);
            g.setColour(INK.withAlpha(0.6f)); g.fillRect(knob.getX() + 5, knob.getCentreY() - 0.5f, knob.getWidth() - 10, 1.0f);
            g.setColour(GREY); g.setFont(font(9.0f)); g.drawText(tr.vol <= 0.0005f ? juce::String("-inf") : juce::String(juce::Decibels::gainToDecibels(tr.vol), 1), fa.withY(fa.getBottom() - 10).withHeight(10), juce::Justification::centred);
            // mute / solo
            g.setColour(tr.mute ? RED : DIM.withAlpha(0.5f)); g.fillRoundedRectangle(muteBox(t).toFloat(), 3); g.setColour(tr.solo ? AMBER : DIM.withAlpha(0.5f)); g.fillRoundedRectangle(soloBox(t).toFloat(), 3);
            g.setFont(font(9.5f)); g.setColour(tr.mute ? ON : GREY); g.drawText("M", muteBox(t), juce::Justification::centred); g.setColour(tr.solo ? ON : GREY); g.drawText("S", soloBox(t), juce::Justification::centred);
        }
        // master
        auto s = strip(gb::kTracks).toFloat(); if (GLASS) glassui::sprites().draw(g, s, 10.0f, glassui::Strip); else { g.setColour(CARD2.withAlpha(0.7f)); g.fillRoundedRectangle(s, 8); }
        g.setColour(INK); g.setFont(font(11.0f)); g.drawText("MASTER", strip(gb::kTracks).withHeight(kTopH), juce::Justification::centred);
        auto fa = faderArea(gb::kTracks).toFloat(); auto meters = fa.removeFromLeft(fa.getWidth() * 0.45f);
        const float mw = (meters.getWidth() - 4) / 2;
        drawMeter(g, meters.removeFromLeft(mw).reduced(0, 2), E.masterPeak[0].load(), gb::kTracks, GREEN); meters.removeFromLeft(4); drawMeter(g, meters.removeFromLeft(mw).reduced(0, 2), E.masterPeak[1].load(), gb::kTracks + 1, GREEN);
        fa.removeFromLeft(4);
        const float v = getMaster ? getMaster() : 0.8f;
        g.setColour(CARD2); g.fillRoundedRectangle(fa.withSizeKeepingCentre(6, fa.getHeight()), 3);
        const float y = fa.getBottom() - 10 - (fa.getHeight() - 20) * v;
        auto knob = juce::Rectangle<float>(fa.getCentreX() - 14, y - 9, 28, 18); if (GLASS) { ghosty::glass::shadow(g, knob, 5.0f, 3.0f, 1.0f, 3); ghosty::glass::body(g, knob, 5.0f, dragT == gb::kTracks ? GREEN : juce::Colours::white, dragT == gb::kTracks ? 0.9f : 0.75f); } else { g.setColour(dragT == gb::kTracks ? GREEN : CARD); g.fillRoundedRectangle(knob, 4); } g.setColour(GREEN); g.drawRoundedRectangle(knob, 4, 1.2f);
        g.setColour(GREY); g.setFont(font(9.0f)); g.drawText(v <= 0.001f ? juce::String("-inf dB") : juce::String(juce::Decibels::gainToDecibels(v * 1.25f), 1) + " dB", strip(gb::kTracks).removeFromBottom(kBotH), juce::Justification::centred);   // unity at the default
    }
    int stripAt(juce::Point<float> p) const { for (int t = 0; t <= gb::kTracks; ++t) if (strip(t).toFloat().contains(p)) return t; return -1; }
    // the GhostBlock slider convention on the strips: MOUSE-DOWN on a fader / pan sets it to that spot, DRAG scrubs, SHIFT = fine (1/8),
    // WHEEL = one step (0.5 dB / 1 % pan), DOUBLE-CLICK = unity (fader) / centre (pan), RIGHT-CLICK = type a value; one undo step per gesture
    bool fine = false; float fineRef = 0, fineVal = 0; juce::TextEditor typer; int typeT = -1, typeKind = 0;
    float faderPosAt(int t, float y) const { auto fa = faderArea(t).toFloat(); return juce::jlimit(0.0f, 1.0f, (fa.getBottom() - 10 - y) / juce::jmax(1.0f, fa.getHeight() - 20)); }
    float panAt(int t, float x) const { auto pa = panArea(t).toFloat(); return juce::jlimit(-1.0f, 1.0f, (x - pa.getX()) / juce::jmax(1.0f, pa.getWidth()) * 2.0f - 1.0f); }
    float getVal(int t, int kind) const { if (kind == 3) return getMaster ? getMaster() : 0.8f; const auto& tr = ed.proc.model.tracks[(size_t) t]; return kind == 1 ? gainToPos(tr.vol) : tr.pan; }
    void setVal(int t, int kind, float v) {   // kind 1: fader position 0..1, 2: pan -1..1, 3: master 0..1
        if (kind == 3) { if (setMaster) setMaster(juce::jlimit(0.0f, 1.0f, v)); repaint(); return; }
        auto& tr = ed.proc.model.tracks[(size_t) t];
        if (kind == 1) tr.vol = posToGain(v); else tr.pan = juce::jlimit(-1.0f, 1.0f, v);
        ed.live(); repaint();
    }
    void undoStep(int t, int kind) { if (kind == 1) ed.gestureUndo(&ed.proc.model.tracks[(size_t) t].vol); else if (kind == 2) ed.gestureUndo(&ed.proc.model.tracks[(size_t) t].pan); }   // (master = a host parameter)
    int kindAt(int t, juce::Point<float> p) const {
        if (t == gb::kTracks) return faderArea(t).toFloat().contains(p) ? 3 : 0;
        return panArea(t).toFloat().contains(p) ? 2 : faderArea(t).toFloat().contains(p) ? 1 : 0;
    }
    void mouseDown(const juce::MouseEvent& e) override {
        const int t = stripAt(e.position); if (t < 0) return; downPos = e.position; dragKind = 0; dragT = t;
        if (t < gb::kTracks) {
            auto& tr = ed.proc.model.tracks[(size_t) t];
            if (muteBox(t).toFloat().contains(e.position)) { ed.edit([&] { tr.mute = !tr.mute; }); dragT = -1; return; }
            if (soloBox(t).toFloat().contains(e.position)) { ed.edit([&] { tr.solo = !tr.solo; }); dragT = -1; return; }
            ed.track = t;
        }
        const int kind = kindAt(t, e.position);
        if (kind == 0) { if (t < gb::kTracks) ed.proc.auditionTrack(t); dragT = -1; if (ed.refresh) ed.refresh(); return; }
        if (e.mods.isPopupMenu()) { startTyping(t, kind); dragT = -1; return; }
        undoStep(t, kind); dragKind = kind;
        fine = e.mods.isShiftDown(); fineRef = kind == 2 ? e.position.x : e.position.y; fineVal = getVal(t, kind);
        if (!fine) setVal(t, kind, kind == 2 ? panAt(t, e.position.x) : faderPosAt(t, e.position.y));   // absolute: jumps to the click
        if (ed.refresh) ed.refresh();
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (dragT < 0 || dragKind == 0) return;
        const float pos = dragKind == 2 ? e.position.x : e.position.y;
        if (e.mods.isShiftDown() != fine) { fine = e.mods.isShiftDown(); fineRef = pos; fineVal = getVal(dragT, dragKind); }
        if (fine) {
            const float len = dragKind == 2 ? panArea(dragT).getWidth() * 0.5f : (float) (faderArea(dragT).getHeight() - 20);
            setVal(dragT, dragKind, fineVal + (dragKind == 2 ? pos - fineRef : fineRef - pos) / juce::jmax(1.0f, len) / 8.0f);
        } else setVal(dragT, dragKind, dragKind == 2 ? panAt(dragT, pos) : faderPosAt(dragT, pos));
    }
    void mouseUp(const juce::MouseEvent&) override { if (dragT >= 0 && dragT < gb::kTracks && dragKind == 1) ed.proc.auditionTrack(dragT); dragT = -1; dragKind = 0; fine = false; repaint(); }
    void mouseDoubleClick(const juce::MouseEvent& e) override {
        const int t = stripAt(e.position); if (t < 0 || e.mods.isPopupMenu()) return; const int kind = kindAt(t, e.position); if (kind == 0) return;
        undoStep(t, kind);   // (the same undo step as the click before it)
        setVal(t, kind, kind == 1 ? gainToPos(0.8f) : kind == 2 ? 0.0f : 0.8f);
    }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override {
        const int t = stripAt(e.position); if (t < 0) return; const int kind = kindAt(t, e.position); if (kind == 0) return;
        const float amt = std::abs(w.deltaX) > std::abs(w.deltaY) ? -w.deltaX : w.deltaY; if (amt == 0.0f) return; const float d = (amt > 0) != w.isReversed ? 1.0f : -1.0f;
        undoStep(t, kind);
        if (kind == 2) setVal(t, kind, getVal(t, kind) + d * 0.02f);
        else if (kind == 1) { const float g = ed.proc.model.tracks[(size_t) t].vol; const float ng = g <= 0.0005f ? (d > 0 ? juce::Decibels::decibelsToGain(-60.0f) : 0.0f) : juce::jlimit(0.0f, 2.0f, g * juce::Decibels::decibelsToGain(0.5f * d)); setVal(t, kind, gainToPos(ng)); }
        else setVal(t, kind, getVal(t, kind) + d * 0.01f);
    }
    // right-click: type the value (fader / master in dB, "-inf" for off; pan -100 (left) .. 100 (right))
    void startTyping(int t, int kind) {
        if (typer.getParentComponent() != this) { addChildComponent(typer); typer.setJustification(juce::Justification::centred); typer.setFont(font(13.0f));
            typer.onReturnKey = [this] { commitTyped(); }; typer.onFocusLost = [this] { commitTyped(); }; typer.onEscapeKey = [this] { typer.setVisible(false); }; }
        typeT = t; typeKind = kind; const float v = getVal(t, kind);
        const float gain = kind == 1 ? posToGain(v) : v * 1.25f;
        typer.setText(kind == 2 ? juce::String(juce::roundToInt(v * 100.0f)) : (gain <= 0.0005f ? juce::String("-inf") : juce::String(juce::Decibels::gainToDecibels(gain), 1)), false);
        auto r = (kind == 2 ? panArea(t) : faderArea(t)).withSizeKeepingCentre(juce::jmax(64, strip(t).getWidth()), 26); typer.setBounds(r); typer.setVisible(true); typer.grabKeyboardFocus(); typer.selectAll();
    }
    void commitTyped() {
        if (!typer.isVisible()) return; typer.setVisible(false); const auto txt = typer.getText().trim().toLowerCase(); if (typeT < 0) return;
        const bool off = txt.contains("inf"); if (!off && !txt.containsAnyOf("0123456789")) return;
        const float x = (float) txt.retainCharacters("-0123456789.").getDoubleValue();
        undoStep(typeT, typeKind);
        if (typeKind == 2) setVal(typeT, 2, x / 100.0f);
        else { const float g = off ? 0.0f : juce::Decibels::decibelsToGain(juce::jlimit(-60.0f, 6.0f, x)); setVal(typeT, typeKind, typeKind == 1 ? gainToPos(juce::jmin(2.0f, g)) : g / 1.25f); }
        if (typeKind == 1 && typeT < gb::kTracks) ed.proc.auditionTrack(typeT);
    }
};

} // namespace bx
