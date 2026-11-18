#pragma once
// TRACKER VIEW: the same steps as the grid, as a vertical list (rows = steps, columns = tracks, 8 at a time).
// Keys: arrows move, Tab = VEL / FX column, digits type a velocity, a letter starts an effect then digits
// (R hits, T retrig per beat, A accel, V vel ramp, P pitch, F cents, G pitch ramp, D delay, L flam, E decay,
//  O start, C cut, S slice, X destroy, Y chance, B reverse, Z freeze x10ms, W band-reverse mask, N pan, H filter), Enter commits, Delete clears,
// shift+arrows select a block, Cmd/Ctrl+C / V copy and paste it. Double-click = every field of the step.
#include "EditorParts.h"

namespace bx {

struct TrackerView : juce::Component {
    Ed& ed; int bank = 0, row = 0, col = 0, field = 0, scroll = 0; int selR0 = -1, selC0 = -1;
    juce::String entry; char cmd = 0;
    int cols = 8; bool followTrack = false;   // the mini tracker beside the lanes shows 4 tracks around the selected one
    std::vector<std::vector<gb::Step>> clip;   // copied block [cols][rows]
    int previewBar = -1; std::shared_ptr<gb::Snapshot> previewSnap;   // >= 0: show what EVOLVE plays in that bar (read-only)
    static constexpr int rowH = 15, headH = 20, numW = 34, detailH = 46;
    explicit TrackerView(Ed& e) : ed(e) { setWantsKeyboardFocus(true); }
    int colW() const { return (getWidth() - numW) / cols; }
    void mouseEnter(const juce::MouseEvent&) override { grabKeyboardFocus(); repaint(); }
    void mouseExit(const juce::MouseEvent&) override { if (hasKeyboardFocus(true)) { commit(); giveAwayKeyboardFocus(); } repaint(); }
    int visRows() const { return juce::jmax(1, (getHeight() - headH - detailH) / rowH); }
    int track(int c) const { return juce::jlimit(0, gb::kTracks - 1, (followTrack ? juce::jmin(gb::kTracks - cols, (ed.track / cols) * cols) : bank * 8) + c); }

    gb::Step shown(int t, int s) const {
        if (previewBar >= 0 && previewSnap) {
            const auto& p = previewSnap->patterns[(size_t) ed.patIdx()]; const gb::EngineParams P = ed.proc.readParams(ed.proc.liveBpm());
            gb::EventCtx C(*previewSnap, P, p, 0.0, P.bpm); const long spb = (long) std::llround(4.0 / C.sb);
            return C.played(t, previewBar * spb + s);
        }
        return ed.pat().steps[(size_t) t][(size_t) s];
    }
    int rowsShown() const { if (previewBar >= 0) { const double sb = gb::stepBeats(ed.pat().res); return juce::jmin(gb::kMaxSteps, (int) std::lround(4.0 / sb)); } return ed.pat().length; }

    static juce::String fx(const gb::Step& s) {
        juce::String r;
        if (s.roll > 1) r << "R" << (int) s.roll; if (s.rate) r << "T" << (int) s.rate; if (s.accel) r << "A" << (int) s.accel; if (s.velRamp) r << "V" << (int) s.velRamp;
        if (s.pitch) r << "P" << (int) s.pitch; if (s.cents) r << "F" << (int) s.cents; if (s.pitchRamp) r << "G" << (int) s.pitchRamp; if (s.nudge) r << "D" << (int) s.nudge;
        if (s.flam) r << "L" << (int) s.flam; if (s.decay != 100) r << "E" << (int) s.decay; if (s.offset) r << "O" << (int) s.offset; if (s.cut) r << "C" << (int) s.cut;
        if (s.slice) r << "S" << (int) s.slice; if (s.destroy) r << "X" << (int) s.destroy; if (s.freeze) r << "Z" << (int) s.freeze; if (s.frev) r << "W" << (int) s.frev; if (s.pan) r << "N" << (int) s.pan; if (s.filt) r << "H" << (int) s.filt; if (s.prob < 100) r << "Y" << (int) s.prob; if (s.reversed()) r << "B";
        return r;
    }
    void paint(juce::Graphics& g) override {
        auto& P = ed.pat(); const int cw = colW(), vr = visRows(), n = rowsShown();
        g.setColour(CARD2.withAlpha(0.6f)); g.fillRoundedRectangle(getLocalBounds().toFloat(), 8);
        for (int c = 0; c < cols; ++c) {
            const int t = track(c); auto h = juce::Rectangle<int>(numW + c * cw, 0, cw, headH);
            g.setColour(trackColour(t).withAlpha(t == ed.track ? 0.5f : 0.22f)); g.fillRect(h.reduced(1));
            g.setColour(INK); g.setFont(font(cw < 110 ? 9.0f : 10.5f)); g.drawText(juce::String(t + 1) + " " + ed.proc.model.tracks[(size_t) t].sound, h.reduced(3, 0), juce::Justification::centredLeft, true);
        }
        const int now = ed.playStep(0);
        for (int i = 0; i < vr; ++i) {
            const int s = scroll + i; if (s >= n) break; const int y = headH + i * rowH;
            const bool beat = (int) std::floor(s * gb::stepBeats(P.res) + 1e-9) != (int) std::floor((s - 1) * gb::stepBeats(P.res) + 1e-9) || s == 0;
            if (beat) { g.setColour(INK.withAlpha(0.05f)); g.fillRect(0, y, getWidth(), rowH); }
            if (s == now && previewBar < 0) { g.setColour(GREEN.withAlpha(0.15f)); g.fillRect(0, y, getWidth(), rowH); }
            g.setColour(beat ? AMBER : GREY); g.setFont(mono(11.5f)); g.drawText(juce::String(s + 1).paddedLeft('0', 2), 0, y, numW - 4, rowH, juce::Justification::centredRight);   // 1-based, like the grid
            for (int c = 0; c < cols; ++c) {
                const int t = track(c); const int x = numW + c * cw;
                const bool sel = selR0 >= 0 && juce::jmin(selR0, row) <= s && s <= juce::jmax(selR0, row) && juce::jmin(selC0, col) <= c && c <= juce::jmax(selC0, col);
                if (sel) { g.setColour(PURPLE.withAlpha(0.3f)); g.fillRect(x, y, cw, rowH); }
                if (s >= P.lenOf(t) && previewBar < 0) { g.setColour(DIM); g.setFont(mono(11.5f)); g.drawText("  .  ", x, y, cw, rowH, juce::Justification::centredLeft); continue; }
                const auto st = shown(t, s);
                g.setFont(mono(11.5f));
                if (st.vel) { g.setColour(trackColour(t)); g.drawText(juce::String(st.vel).paddedLeft('0', 3), x + 4, y, 30, rowH, juce::Justification::centredLeft);
                              g.setColour(st.locked() ? AMBER : INK.withAlpha(0.85f)); g.drawText(fx(st), x + 34, y, cw - 36, rowH, juce::Justification::centredLeft, true); }
                else { g.setColour(DIM); g.drawText("---", x + 4, y, 30, rowH, juce::Justification::centredLeft); }
                if (s == row && c == col && hasKeyboardFocus(false)) { g.setColour(GREEN); g.drawRect(field == 0 ? juce::Rectangle<int>(x + 2, y, 32, rowH) : juce::Rectangle<int>(x + 34, y, cw - 36, rowH), 1); }
                g.setColour(INK.withAlpha(0.06f)); g.drawVerticalLine(x, (float) y, (float) (y + rowH));
            }
        }
        // detail strip: every field of the cursor step + what you're typing
        auto d = getLocalBounds().removeFromBottom(detailH).reduced(6, 4); const auto st = shown(track(col), row);
        g.setColour(CARD.withAlpha(0.9f)); g.fillRoundedRectangle(d.toFloat(), 6);
        juce::String a; a << "TRACK " << track(col) + 1 << "  STEP " << row + 1 << "   VEL " << (int) st.vel << "  CHANCE " << (int) st.prob << "%  HITS " << (int) st.roll << "  RETRIG " << (int) st.rate << "/beat  ACCEL " << (int) st.accel
                          << "  VEL RAMP " << (int) st.velRamp << "  PITCH " << (int) st.pitch << " " << (int) st.cents << "c  RAMP " << (int) st.pitchRamp;
        juce::String b; b << "DELAY " << (int) st.nudge << "%  FLAM " << (int) st.flam << "ms  DECAY " << (int) st.decay << "%  START " << (int) st.offset << "%  CUT " << (int) st.cut << "%  SLICE " << (int) st.slice
                          << "  DESTROY " << (int) st.destroy << "%  FREEZE " << (int) st.freeze * 10 << "ms  BAND REV " << (int) st.frev << "  PAN " << (int) st.pan << "  FILTER " << (int) st.filt << (st.reversed() ? "  REVERSE" : "") << (st.locked() ? "  LOCKED" : "")
                          << (cmd || entry.isNotEmpty() ? "      typing: " + juce::String::charToString((juce::juce_wchar) (cmd ? cmd : ' ')) + entry : juce::String())
                          << (previewBar >= 0 ? "      PREVIEW of evolved bar " + juce::String(previewBar + 1) + " (read-only)" : juce::String());
        g.setColour(INK); g.setFont(mono(11.0f)); g.drawText(a, d.removeFromTop(d.getHeight() / 2), juce::Justification::centredLeft, true); g.setColour(GREY); g.drawText(b, d, juce::Justification::centredLeft, true);
    }
    void follow() { const int vr = visRows(); if (row < scroll) scroll = row; if (row >= scroll + vr) scroll = row - vr + 1; scroll = juce::jlimit(0, juce::jmax(0, rowsShown() - vr), scroll); }
    void mouseDown(const juce::MouseEvent& e) override {
        grabKeyboardFocus(); commit();
        const int c = (e.x - numW) / juce::jmax(1, colW()), r = scroll + (e.y - headH) / rowH;
        if (c < 0 || c >= cols || e.y < headH || r >= rowsShown()) return;
        if (e.mods.isShiftDown()) { if (selR0 < 0) { selR0 = row; selC0 = col; } } else selR0 = -1;
        row = r; col = c; field = (e.x - numW - c * colW()) > 34 ? 1 : 0; ed.track = track(c); if (ed.refresh) ed.refresh(); repaint();
    }
    void mouseDoubleClick(const juce::MouseEvent&) override {
        if (previewBar >= 0) return;
        std::vector<std::pair<int, int>> sel;   // a selected block: the step editor writes into every step of it
        if (selR0 >= 0) for (int c = juce::jmin(selC0, col); c <= juce::jmax(selC0, col); ++c) for (int r = juce::jmin(selR0, row); r <= juce::jmax(selR0, row); ++r) sel.push_back({ track(c), r });
        if (!sel.empty()) { auto it = std::find(sel.begin(), sel.end(), std::make_pair(track(col), row)); if (it != sel.end()) std::iter_swap(sel.begin(), it); }
        StepEditor::open(ed, track(col), row, *this, localAreaToGlobal(juce::Rectangle<int>(numW + col * colW(), headH + (row - scroll) * rowH, colW(), rowH)), sel);
    }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { scroll = juce::jlimit(0, juce::jmax(0, rowsShown() - visRows()), scroll - (int) std::round(w.deltaY * 12)); repaint(); }

    gb::Step& cur() { return ed.pat().steps[(size_t) track(col)][(size_t) row]; }
    void commit() {
        if (previewBar >= 0 || (entry.isEmpty() && !cmd)) { entry.clear(); cmd = 0; return; }
        const int v = entry.getIntValue();
        ed.edit([&] {
            auto& st = cur();
            if (!cmd) { st.vel = (uint8_t) juce::jlimit(0, 127, v); if (!st.vel) st = gb::Step(); }
            else {
                if (!st.vel) st.vel = 100;
                switch (cmd) {
                    case 'R': st.roll = (uint8_t) juce::jlimit(1, 16, v); break;           case 'T': st.rate = (uint8_t) juce::jlimit(0, 64, v); break;
                    case 'A': st.accel = (int8_t) juce::jlimit(-100, 100, v); break;       case 'V': st.velRamp = (int8_t) juce::jlimit(-100, 100, v); break;
                    case 'P': st.pitch = (int8_t) juce::jlimit(-24, 24, v); break;         case 'F': st.cents = (int8_t) juce::jlimit(-99, 99, v); break;
                    case 'G': st.pitchRamp = (int8_t) juce::jlimit(-24, 24, v); break;     case 'D': st.nudge = (int8_t) juce::jlimit(-50, 50, v); break;
                    case 'L': st.flam = (uint8_t) juce::jlimit(0, 40, v); break;           case 'E': st.decay = (uint8_t) juce::jlimit(5, 100, v); break;
                    case 'O': st.offset = (uint8_t) juce::jlimit(0, 99, v); break;         case 'C': st.cut = (uint8_t) juce::jlimit(0, 100, v); break;
                    case 'S': st.slice = (uint8_t) juce::jlimit(0, 64, v); break;          case 'X': st.destroy = (uint8_t) juce::jlimit(0, 100, v); break;
                    case 'Y': st.prob = (uint8_t) juce::jlimit(1, 100, v); break;          case 'B': st.flags ^= 1; break;
                    case 'Z': st.freeze = (uint8_t) juce::jlimit(0, 255, v); break;        case 'W': st.frev = (uint8_t) juce::jlimit(0, 7, v); break;
                    case 'N': st.pan = (int8_t) juce::jlimit(-50, 50, v); break;            case 'H': st.filt = (uint8_t) juce::jlimit(0, 127, v); break;
                    default: break;
                }
            }
        });
        entry.clear(); cmd = 0;
    }
    bool keyPressed(const juce::KeyPress& k) override {
        const bool shift = k.getModifiers().isShiftDown(), cmdKey = k.getModifiers().isCommandDown();
        const int n = rowsShown();
        auto move = [&](int dr, int dc) { commit(); if (shift) { if (selR0 < 0) { selR0 = row; selC0 = col; } } else selR0 = -1;
                                          row = juce::jlimit(0, n - 1, row + dr); col = juce::jlimit(0, cols - 1, col + dc); ed.track = track(col); follow(); if (ed.refresh) ed.refresh(); repaint(); return true; };
        if (k.isKeyCode(juce::KeyPress::upKey)) return move(-1, 0);
        if (k.isKeyCode(juce::KeyPress::downKey)) return move(1, 0);
        if (k.isKeyCode(juce::KeyPress::leftKey)) { if (!shift && field == 1) { commit(); field = 0; repaint(); return true; } return move(0, -1); }
        if (k.isKeyCode(juce::KeyPress::rightKey)) { if (!shift && field == 0) { commit(); field = 1; repaint(); return true; } return move(0, 1); }
        if (k.isKeyCode(juce::KeyPress::tabKey)) { commit(); field = 1 - field; repaint(); return true; }
        if (k.isKeyCode(juce::KeyPress::pageDownKey)) return move(visRows(), 0);
        if (k.isKeyCode(juce::KeyPress::pageUpKey)) return move(-visRows(), 0);
        if (previewBar >= 0) return false;
        if (k.isKeyCode(juce::KeyPress::returnKey)) { commit(); return move(1, 0); }
        if (k.isKeyCode(juce::KeyPress::deleteKey) || k.isKeyCode(juce::KeyPress::backspaceKey)) {
            entry.clear(); cmd = 0;
            ed.edit([&] { forBlock([&](gb::Step& st) { const auto fl = st.flags & 2; st = gb::Step(); st.flags = (uint8_t) fl; }); }); return move(selR0 < 0 ? 1 : 0, 0);
        }
        if (cmdKey && k.getKeyCode() == 'C') { copyBlock(); if (ed.say) ed.say("Copied the block."); return true; }
        if (cmdKey && k.getKeyCode() == 'V') { pasteBlock(); return true; }
        if (cmdKey) return false;
        const auto ch = k.getTextCharacter();
        if (ch >= '0' && ch <= '9') { if (field == 0 && !cmd && entry.length() >= 3) entry.clear(); entry << juce::String::charToString(ch); if (field == 0 && !cmd && entry.length() == 3) { commit(); return move(1, 0); } repaint(); return true; }
        if (ch == '-' && entry.isEmpty()) { entry = "-"; repaint(); return true; }
        const auto up = juce::CharacterFunctions::toUpperCase(ch);
        if (juce::String("RTAVPFGDLEOCSXYBZWNH").containsChar(up)) { commit(); field = 1; cmd = (char) up; if (cmd == 'B') commit(); repaint(); return true; }
        return false;
    }
    void forBlock(const std::function<void(gb::Step&)>& f) {
        const int r0 = selR0 < 0 ? row : juce::jmin(selR0, row), r1 = selR0 < 0 ? row : juce::jmax(selR0, row), c0 = selR0 < 0 ? col : juce::jmin(selC0, col), c1 = selR0 < 0 ? col : juce::jmax(selC0, col);
        for (int c = c0; c <= c1; ++c) for (int r = r0; r <= r1; ++r) f(ed.pat().steps[(size_t) track(c)][(size_t) r]);
    }
    void copyBlock() {
        const int r0 = selR0 < 0 ? row : juce::jmin(selR0, row), r1 = selR0 < 0 ? row : juce::jmax(selR0, row), c0 = selR0 < 0 ? col : juce::jmin(selC0, col), c1 = selR0 < 0 ? col : juce::jmax(selC0, col);
        clip.assign((size_t) (c1 - c0 + 1), {}); for (int c = c0; c <= c1; ++c) for (int r = r0; r <= r1; ++r) clip[(size_t) (c - c0)].push_back(ed.pat().steps[(size_t) track(c)][(size_t) r]);
    }
    void pasteBlock() {
        if (clip.empty()) return;
        ed.edit([&] { for (size_t c = 0; c < clip.size(); ++c) for (size_t r = 0; r < clip[c].size(); ++r) { const int t = track(col + (int) c), s = row + (int) r; if (t < gb::kTracks && col + (int) c < cols && s < gb::kMaxSteps) ed.pat().steps[(size_t) t][(size_t) s] = clip[c][r]; } });
    }
};

} // namespace bx
