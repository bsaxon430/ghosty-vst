#pragma once
// MELODY page: the melodic tracker (see MelodyTracker.h). Left: the 16 INSTRUMENTS. Top: the selected instrument (sound, route track,
// slice, root, tune, vol, pan, envelope, start / end, loop, filter) and the song (MELODY on, LPB, SPEED, ROWS, octave, step, FOLLOW,
// MIDI REC, ORDER). Middle: 8 channels x NOTE / INSTRUMENT / VOLUME / EFFECT. Bottom: the cursor cell in words + effect help.
// Keys (classic tracker layout):
//   notes   Z S X D C V G B H N J M , L . ; /  = C..E of the current octave;  Q 2 W 3 E R 5 T 6 Y 7 U I 9 O 0 P = the octave above
//           (you hear the note while the key is held, at its pitch, on its instrument; never recorded into the drums)
//   1 = note off (===)    [ ] = octave down / up    { } = edit step    Enter = hear the cursor row    SPACE = the transport (not taken here)
//   arrows move, Tab / shift+Tab = channel, hex digits type INSTRUMENT / VOLUME / EFFECT (effect letter first, then two hex digits),
//   Delete clears the field, Insert inserts a row, shift+Delete deletes a row, shift+arrows select, Cmd/Ctrl+C / X / V copy / cut / paste,
//   Cmd/Ctrl+up / down transpose the selection (+shift = an octave), Cmd/Ctrl+I interpolates volume + effect values over the selection,
//   Cmd/Ctrl+shift+C / V copy / paste the whole pattern, ? = every effect with its help.
// Mouse: click a field; the wheel scrolls the rows (and turns FOLLOW off); wheel / click / right-click the strip's fields to change them.
#include "MelodyTracker.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace bx {

struct MelLook {   // defaults = Ghosty Beats' LIGHT palette (EditorParts.h); the editor passes its palette on every theme switch
    juce::Colour card { 0xffFFFFFF }, card2 { 0xffE7E9F1 }, ink { 0xff15172A }, grey { 0xff646B7C }, dim { 0xffC0C4D2 }, purple { 0xff6D3FF0 },
                 green { 0xff0F9D58 }, amber { 0xffC28400 }, pink { 0xffC93B8F }, cyan { 0xff0E8FA8 }, blue { 0xff2563EB }, red { 0xffDC2626 }, lime { 0xff5F9D0B }, on { 0xffFFFFFF };   // on = text on a filled accent
    juce::Colour track(int t) const { const juce::Colour C[] = { green, purple, pink, amber, blue, cyan, red, lime }; return C[(size_t) ((t % 8 + 8) % 8)]; }
};

struct MelodyTrackerView : juce::Component {
    gb::mel::Song* song = nullptr; int pat = 0;
    std::function<void(const std::function<void()>&)> edit = [](const std::function<void()>& f) { f(); };
    std::function<int()> playRow, playPat;                       // the row / pattern under the playhead (-1 = not playing)
    std::function<juce::String(int)> trackName;
    std::function<void(int, int)> preview;                        // (inst, note) audition; note < 0 = release
    std::function<void(const juce::String&)> say;
    std::function<void(juce::PopupMenu&)> librarySounds;          // fills a menu: item ids 1000 + n, see soundForId
    std::function<juce::String(int)> soundForId;                  // menu id -> library sound name
    std::function<void(int)> loadSample;                          // instrument i: open a file and make it the instrument's own sample
    std::function<void(int, int)> takeTrack;                      // instrument i <- track t's sound (the editor copies the reference)
    std::function<void(bool)> midiRec;                            // MIDI REC switched
    std::function<void()> orderChanged;                           // FOLLOW DRUMS <-> OWN ORDER (the editor re-syncs `pat`)
    MelLook look;
    int row = 0, chan = 0, field = 0, nib = 0, scroll = 0, octave = 4, step = 1, inst = 0, orderPos = 0; int selR0 = -1, selC0 = -1; bool help = false, follow = true, recArmed = false;
    int heldKey = 0, heldNote = -1;   // the note key held down (audition until released)
    std::vector<std::vector<gb::mel::Cell>> clip; std::unique_ptr<gb::mel::Pattern> patClip;
    juce::TextEditor nameEd;
    static constexpr int rowH = 15, stripRowH = 24, stripRows = 3, headH = 20, numW = 34, detailH = 46, listW = 158;
    int stripH() const { return stripRows * stripRowH + 10; }

    MelodyTrackerView() {
        setWantsKeyboardFocus(true);
        nameEd.setFont(font(11.0f)); nameEd.setJustification(juce::Justification::centredLeft); addChildComponent(nameEd);
        nameEd.onReturnKey = [this] { commitName(); }; nameEd.onFocusLost = [this] { commitName(); }; nameEd.onEscapeKey = [this] { nameEd.setVisible(false); grabKeyboardFocus(); };
    }
    static juce::Font mono(float h) { return juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), h, juce::Font::plain); }
    static juce::Font font(float h, bool bold = true) { return juce::FontOptions(h, bold ? juce::Font::bold : juce::Font::plain); }
    gb::mel::Pattern& P() { return song->pat(pat); }
    const gb::mel::Pattern& P() const { return song->pat(pat); }
    gb::mel::Instrument& I() { return song->inst[(size_t) inst]; }
    const gb::mel::Instrument& I() const { return song->inst[(size_t) inst]; }
    int rows() const { return song ? juce::jlimit(1, gb::mel::kRows, song->pat(pat).rows) : 1; }
    int gridX() const { return listW + 6; }
    int colW() const { return (getWidth() - gridX() - numW) / gb::mel::kChannels; }
    int gridY() const { return stripH(); }
    int visRows() const { return juce::jmax(1, (getHeight() - gridY() - headH - detailH) / rowH); }
    int charW() const { return juce::jmax(6, juce::roundToInt(juce::GlyphArrangement::getStringWidth(mono(11.5f), "0000000000") / 10.0f)); }
    static int fieldChar(int f) { static const int x[] { 0, 4, 7, 10 }; return x[f]; }
    static int fieldLen(int f) { static const int l[] { 3, 2, 2, 3 }; return l[f]; }
    static juce::String hex2(int v) { return juce::String::toHexString(v).toUpperCase().paddedLeft('0', 2); }
    juce::String tname(int t) const { return trackName ? trackName(t) : juce::String("track"); }
    bool instUsedInSong(int i) const { for (int p = 0; p < gb::mel::kPats; ++p) { const auto& pt = song->pat(p); for (auto& ch : pt.cells) for (int r = 0; r < pt.rows; ++r) if (ch[(size_t) r].inst == i + 1 && ch[(size_t) r].note >= 1 && ch[(size_t) r].note <= 128) return true; } return false; }
    juce::String problem(int i) const { const auto& in = song->inst[(size_t) i]; if (!in.hasSound()) return "no sound"; if (in.route < 0) return "no route track"; return {}; }

    // ---- the strip: fields you click / right-click / wheel
    enum StripField { SName = 0, SSound, SRoute, SSlice, SRoot, STune, SVol, SPan, SFollowPitch,
                      SAtt, SDec, SSus, SRel, SStart, SEnd, SLoop, SCut, SRes,
                      SOn, SLpb, SSpeed, SRows, SOct, SStep, SFollow, SRec, SOrderMode, SPat, SCount };
    juce::Rectangle<int> sr[SCount]; std::vector<juce::Rectangle<int>> orderCells; juce::Rectangle<int> orderAdd;
    static juce::String pct(float v) { return juce::String(juce::roundToInt(v * 100)) + "%"; }
    static juce::String ms(float s) { return s < 1.0f ? juce::String(juce::roundToInt(s * 1000)) + "ms" : juce::String(s, 2) + "s"; }
    juce::String stripText(int f) const {
        const auto& in = I();
        switch (f) {
            case SName: return "INS " + hex2(inst + 1) + "  " + (in.label().isNotEmpty() ? in.label() : juce::String("(empty)"));
            case SSound: return "SOUND  " + (in.sample ? in.sample->name + " (own sample)" : in.sound.isNotEmpty() ? in.sound : juce::String("- pick -"));
            case SRoute: return in.route >= 0 ? "TRACK " + juce::String(in.route + 1) + " " + tname(in.route) : juce::String("TRACK - pick -");
            case SSlice: return in.slices.empty() ? juce::String("SLICE -") : "SLICE " + (in.slice ? juce::String(in.slice) : juce::String("all"));
            case SRoot: return "ROOT " + gb::mel::noteName(in.root + 1);
            case STune: return "TUNE " + juce::String(juce::roundToInt(in.tune)) + "c";
            case SVol: return "VOL " + juce::String(juce::roundToInt(in.vol * 100));
            case SPan: return "PAN " + juce::String(juce::roundToInt(in.pan * 50));
            case SFollowPitch: return in.followPitch ? "TRACK PITCH on" : "TRACK PITCH off";
            case SAtt: return "ATTACK " + ms(in.attack);
            case SDec: return "DECAY " + ms(in.decay);
            case SSus: return "SUSTAIN " + pct(in.sustain);
            case SRel: return "RELEASE " + ms(in.release);
            case SStart: return "START " + pct(in.start);
            case SEnd: return "END " + pct(in.end);
            case SLoop: return in.loop ? "LOOP " + pct(in.loopStart) + "-" + pct(in.loopEnd) : juce::String("LOOP off");
            case SCut: return in.cutoff >= 0.999f ? juce::String("CUTOFF open") : "CUTOFF " + pct(in.cutoff);
            case SRes: return "RESO " + pct(in.reso);
            case SOn: return song->on ? "MELODY ON" : "MELODY OFF";
            case SLpb: return "LPB " + juce::String(song->lpb);
            case SSpeed: return "SPEED " + juce::String(song->speed);
            case SRows: return "ROWS " + juce::String(P().rows);
            case SOct: return "OCT " + juce::String(octave);
            case SStep: return "STEP " + juce::String(step);
            case SFollow: return follow ? "FOLLOW on" : "FOLLOW off";
            case SRec: return recArmed ? "MIDI REC on" : "MIDI REC";
            case SOrderMode: return song->ownOrder ? "OWN ORDER" : "WITH DRUMS";
            case SPat: return "PAT " + juce::String::charToString((juce::juce_wchar) ('A' + pat));
            default: return {};
        }
    }
    void layoutStrip() {
        auto r = juce::Rectangle<int>(6, 4, getWidth() - 12, stripRows * stripRowH);
        auto row = [&](int k) { return r.withY(r.getY() + k * stripRowH).withHeight(stripRowH - 3); };
        auto put = [&](juce::Rectangle<int>& line, int f, int w) { sr[f] = line.removeFromLeft(w); line.removeFromLeft(4); };
        { auto a = row(0); put(a, SName, 190); put(a, SSound, 230); put(a, SRoute, 150); put(a, SSlice, 70); put(a, SRoot, 76); put(a, STune, 70); put(a, SVol, 62); put(a, SPan, 62); put(a, SFollowPitch, juce::jlimit(90, 150, a.getWidth())); }
        { auto a = row(1); put(a, SAtt, 104); put(a, SDec, 100); put(a, SSus, 96); put(a, SRel, 108); put(a, SStart, 80); put(a, SEnd, 76); put(a, SLoop, 120); put(a, SCut, 110); put(a, SRes, juce::jlimit(70, 110, a.getWidth())); }
        { auto a = row(2); put(a, SOn, 100); put(a, SLpb, 56); put(a, SSpeed, 70); put(a, SRows, 74); put(a, SOct, 56); put(a, SStep, 60); put(a, SFollow, 90); put(a, SRec, 90); put(a, SOrderMode, 96); put(a, SPat, 56);
          orderCells.clear(); if (song && song->ownOrder) { for (size_t k = 0; k < song->order.size() && a.getWidth() > 46; ++k) { orderCells.push_back(a.removeFromLeft(22)); a.removeFromLeft(2); } orderAdd = a.removeFromLeft(24); } else orderAdd = {}; }
        nameEd.setBounds(sr[SName].withTrimmedLeft(52));
    }
    void resized() override { layoutStrip(); }
    void changeStrip(int f, int d) {
        if (!song) return;
        if (f == SOct) { octave = juce::jlimit(0, 8, octave + d); repaint(); return; }
        if (f == SStep) { step = juce::jlimit(0, 16, step + d); repaint(); return; }
        if (f == SFollow) { follow = !follow; repaint(); return; }
        if (f == SRec) { recArmed = !recArmed; if (midiRec) midiRec(recArmed); if (say) say(recArmed ? "MIDI REC: your MIDI keyboard plays instrument " + hex2(inst + 1) + " and writes into channel " + juce::String(chan + 1) + " (at the playhead while playing, else at the cursor)." : "MIDI REC off: MIDI notes play the drum pads again."); repaint(); return; }
        if (f == SPat) { if (song->ownOrder) { pat = juce::jlimit(0, gb::mel::kPats - 1, pat + d); row = juce::jmin(row, rows() - 1); repaint(); } return; }
        if (f == SName) { nameEd.setText(I().name, false); nameEd.setVisible(true); nameEd.grabKeyboardFocus(); nameEd.selectAll(); return; }
        if (f == SSound) { soundMenu(); return; }
        if (f == SRoute) { routeMenu(); return; }
        edit([&] {
            auto& in = I();
            auto addF = [&](float& v, float step2, float lo, float hi) { v = juce::jlimit(lo, hi, v + d * step2); };
            auto mulT = [&](float& v, float lo, float hi) { v = v < 0.0005f ? (d > 0 ? lo : 0.0f) : juce::jlimit(0.0f, hi, v * (d > 0 ? 1.3f : 1.0f / 1.3f)); if (v < lo * 0.99f) v = 0.0f; };
            switch (f) {
                case SSlice: if (!in.slices.empty()) in.slice = juce::jlimit(0, (int) in.slices.size(), in.slice + d); break;
                case SRoot: in.root = juce::jlimit(12, 127, in.root + d); break;
                case STune: addF(in.tune, 1, -100, 100); break;
                case SVol: addF(in.vol, 0.01f, 0, 2); break;
                case SPan: addF(in.pan, 0.02f, -1, 1); break;
                case SFollowPitch: in.followPitch = !in.followPitch; break;
                case SAtt: mulT(in.attack, 0.001f, 4.0f); break;
                case SDec: mulT(in.decay, 0.005f, 8.0f); break;
                case SSus: addF(in.sustain, 0.05f, 0, 1); break;
                case SRel: in.release = juce::jlimit(0.002f, 8.0f, in.release * (d > 0 ? 1.3f : 1.0f / 1.3f)); break;
                case SStart: addF(in.start, 0.01f, 0, juce::jmax(0.0f, in.end - 0.02f)); break;
                case SEnd: addF(in.end, 0.01f, juce::jmin(1.0f, in.start + 0.02f), 1); break;
                case SLoop: if (!in.loop && d > 0) in.loop = true; else if (in.loop) { in.loopStart = juce::jlimit(0.02f, in.loopEnd - 0.02f, in.loopStart + d * 0.01f); if (d < 0 && in.loopStart <= 0.021f) in.loop = false; } break;
                case SCut: addF(in.cutoff, 0.02f, 0, 1); break;
                case SRes: addF(in.reso, 0.02f, 0, 1); break;
                case SOn: song->on = !song->on; break;
                case SLpb: song->lpb = juce::jlimit(1, 16, song->lpb + d); break;
                case SSpeed: song->speed = juce::jlimit(1, 32, song->speed + d); break;
                case SRows: P().rows = juce::jlimit(1, gb::mel::kRows, P().rows + d * (P().rows >= 16 ? 16 : 1)); break;
                case SOrderMode: song->ownOrder = !song->ownOrder; if (song->order.empty()) song->order.push_back((uint8_t) pat); break;
                default: break;
            }
        });
        if (f == SOrderMode && orderChanged) orderChanged();
        row = juce::jmin(row, rows() - 1); layoutStrip(); repaint();
    }
    void soundMenu() {
        juce::PopupMenu m, tracks, lib;
        for (int t = 0; t < gb::kTracks; ++t) tracks.addItem(100 + t, juce::String(t + 1) + "  " + tname(t));
        m.addSubMenu("Copy a track's sound (the instrument keeps it: kit changes don't touch it)", tracks);
        if (librarySounds) { librarySounds(lib); m.addSubMenu("A library sound", lib); }
        if (loadSample) m.addItem(2, "Load a sample file...");
        m.addSeparator(); m.addItem(3, "No sound");
        juce::Component::SafePointer<MelodyTrackerView> sp(this); const int ii = inst;
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withTargetScreenArea(localAreaToGlobal(sr[SSound])), [sp, ii](int r) {
            if (!sp || r <= 0) return; auto& v = *sp;
            if (r >= 100 && r < 100 + gb::kTracks) { if (v.takeTrack) v.takeTrack(ii, r - 100); }
            else if (r == 2) { if (v.loadSample) v.loadSample(ii); }
            else if (r == 3) v.edit([&] { auto& in = v.song->inst[(size_t) ii]; in.sample = nullptr; in.sound = {}; in.slices.clear(); in.slice = 0; });
            else if (r >= 1000 && v.soundForId) { const auto nm = v.soundForId(r); if (nm.isNotEmpty()) v.edit([&] { auto& in = v.song->inst[(size_t) ii]; in.sample = nullptr; in.sound = nm; in.slices.clear(); in.slice = 0; if (in.route < 0) in.route = 0; }); }
            v.repaint(); });
    }
    void routeMenu() {
        juce::PopupMenu m; for (int t = 0; t < gb::kTracks; ++t) m.addItem(t + 1, juce::String(t + 1) + "  " + tname(t), true, I().route == t);
        juce::Component::SafePointer<MelodyTrackerView> sp(this); const int ii = inst;
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withTargetScreenArea(localAreaToGlobal(sr[SRoute])), [sp, ii](int r) { if (sp && r > 0) { sp->edit([&] { sp->song->inst[(size_t) ii].route = r - 1; }); sp->repaint(); } });
    }
    void commitName() { if (!nameEd.isVisible()) return; nameEd.setVisible(false); const auto t = nameEd.getText().trim().substring(0, 24); if (song && t != I().name) edit([&] { I().name = t; }); grabKeyboardFocus(); repaint(); }
    int stripAt(juce::Point<int> p) const { for (int f = 0; f < SCount; ++f) if (sr[f].contains(p)) return f; return -1; }

    // ---- painting (the same look as the drum tracker)
    void paint(juce::Graphics& g) override {
        if (!song) return;
        const auto& L = look; auto& p = P(); const int cw = colW(), vr = visRows(), n = rows(), ch = charW(), gx = gridX(), y0 = gridY();
        g.setColour(L.card2.withAlpha(0.6f)); g.fillRoundedRectangle(getLocalBounds().toFloat(), 8);
        for (int f = 0; f < SCount; ++f) {
            if (f == SPat && !song->ownOrder) continue;
            const auto r = sr[f].toFloat(); const auto& in = I();
            const bool on = (f == SOn && song->on) || (f == SLoop && in.loop) || (f == SFollow && follow) || (f == SRec && recArmed) || (f == SOrderMode && song->ownOrder) || (f == SFollowPitch && in.followPitch);
            const bool warn = (f == SSound && !in.hasSound() && instUsedInSong(inst)) || (f == SRoute && in.route < 0 && in.hasSound());
            g.setColour(on ? (f == SRec ? L.red : L.purple).withAlpha(0.85f) : L.card2); g.fillRoundedRectangle(r, 5);
            g.setColour((warn ? L.red : L.purple).withAlpha(f == SName || warn ? 0.9f : 0.4f)); g.drawRoundedRectangle(r.reduced(0.5f), 5, warn ? 1.6f : 1.0f);
            g.setColour(on ? L.on : warn ? L.red : (f == SRoute && in.route >= 0 ? L.track(in.route) : L.ink)); g.setFont(font(10.5f));
            if (!(f == SName && nameEd.isVisible())) g.drawText(stripText(f), sr[f].reduced(5, 0), juce::Justification::centredLeft, true);
        }
        for (size_t k = 0; k < orderCells.size(); ++k) {   // OWN ORDER: the sequence (click = edit that pattern, wheel = change it, right-click = remove)
            const auto r = orderCells[k].toFloat(); const bool cur2 = (int) k == orderPos; const bool playing = playPat && playPat() == song->order[k] && playRow && playRow() >= 0;
            g.setColour(cur2 ? L.purple.withAlpha(0.8f) : L.card); g.fillRoundedRectangle(r, 4); g.setColour(playing ? L.green : L.purple.withAlpha(0.4f)); g.drawRoundedRectangle(r.reduced(0.5f), 4, playing ? 1.8f : 1.0f);
            g.setColour(cur2 ? L.on : L.ink); g.setFont(font(10.5f)); g.drawText(juce::String::charToString((juce::juce_wchar) ('A' + song->order[k])), r, juce::Justification::centred);
        }
        if (!orderAdd.isEmpty()) { g.setColour(L.card); g.fillRoundedRectangle(orderAdd.toFloat(), 4); g.setColour(L.purple); g.drawText("+", orderAdd, juce::Justification::centred); }
        // instrument list
        { auto lr = juce::Rectangle<int>(4, y0, listW - 2, getHeight() - y0 - detailH - 2); const int ih = juce::jmin(22, lr.getHeight() / gb::mel::kInstruments);
          g.setColour(L.grey); g.setFont(font(9.5f)); g.drawText("INSTRUMENTS", lr.removeFromTop(headH), juce::Justification::centredLeft);
          for (int i = 0; i < gb::mel::kInstruments; ++i) { auto r = lr.removeFromTop(ih).reduced(0, 1); const auto& in = song->inst[(size_t) i]; const auto prob = problem(i); const bool bad = prob.isNotEmpty() && instUsedInSong(i);
              g.setColour(i == inst ? L.purple.withAlpha(0.3f) : L.card.withAlpha(0.6f)); g.fillRoundedRectangle(r.toFloat(), 4);
              if (in.route >= 0) { g.setColour(L.track(in.route)); g.fillRect(r.getX(), r.getY() + 2, 3, r.getHeight() - 4); }
              g.setColour(L.grey); g.setFont(mono(10.5f)); g.drawText(hex2(i + 1), r.withTrimmedLeft(6).withWidth(20), juce::Justification::centredLeft);
              g.setColour(bad ? L.red : in.hasSound() ? L.ink : L.dim); g.setFont(font(10.0f, in.hasSound())); g.drawText(bad ? in.label() + " (" + prob + ")" : (in.hasSound() ? in.label() : juce::String("-")), r.withTrimmedLeft(28), juce::Justification::centredLeft, true); } }
        for (int c = 0; c < gb::mel::kChannels; ++c) {
            auto h = juce::Rectangle<int>(gx + numW + c * cw, y0, cw, headH);
            g.setColour(L.track(c).withAlpha(c == chan ? 0.5f : 0.22f)); g.fillRect(h.reduced(1));
            g.setColour(L.ink); g.setFont(font(10.5f)); g.drawText("CH " + juce::String(c + 1), h.reduced(4, 0), juce::Justification::centredLeft, true);
        }
        const int now = playRow && (!playPat || playPat() == pat) ? playRow() : -1;
        for (int i = 0; i < vr; ++i) {
            const int r = scroll + i; if (r >= n) break; const int y = y0 + headH + i * rowH;
            const bool beat = r % juce::jmax(1, song->lpb) == 0;
            if (beat) { g.setColour(L.ink.withAlpha(0.05f)); g.fillRect(gx, y, getWidth() - gx, rowH); }
            if (r == now) { g.setColour(L.green.withAlpha(0.18f)); g.fillRect(gx, y, getWidth() - gx, rowH); }
            g.setColour(beat ? L.amber : L.grey); g.setFont(mono(11.5f)); g.drawText(hex2(r), gx, y, numW - 4, rowH, juce::Justification::centredRight);
            for (int c = 0; c < gb::mel::kChannels; ++c) {
                const int x = gx + numW + c * cw; const auto& cell = p.cells[(size_t) c][(size_t) r];
                const bool sel = selR0 >= 0 && juce::jmin(selR0, row) <= r && r <= juce::jmax(selR0, row) && juce::jmin(selC0, chan) <= c && c <= juce::jmax(selC0, chan);
                if (sel) { g.setColour(L.purple.withAlpha(0.3f)); g.fillRect(x, y, cw, rowH); }
                const int rt = cell.inst ? song->inst[(size_t) (cell.inst - 1)].route : -1; const bool badInst = cell.inst && cell.note >= 1 && cell.note <= 128 && !song->inst[(size_t) (cell.inst - 1)].used();
                auto put = [&](int f, const juce::String& s, juce::Colour col) { g.setColour(col); g.drawText(s, x + 4 + fieldChar(f) * ch, y, fieldLen(f) * ch + 2, rowH, juce::Justification::centredLeft, false); };
                g.setFont(mono(11.5f));
                put(0, gb::mel::noteName(cell.note), cell.note == gb::mel::kNoteOff ? L.pink : (cell.note ? (badInst ? L.red : L.track(juce::jmax(0, rt))) : L.dim));
                put(1, cell.inst ? hex2(cell.inst) : juce::String(".."), cell.inst ? (badInst ? L.red : L.ink.withAlpha(0.85f)) : L.dim);
                put(2, cell.vol != gb::mel::kNoVol ? hex2(cell.vol) : juce::String(".."), cell.vol != gb::mel::kNoVol ? L.cyan : L.dim);
                put(3, cell.fx ? juce::String::charToString((juce::juce_wchar) gb::mel::fxChar(cell.fx)) + hex2(cell.param) : juce::String("..."), cell.fx ? L.amber : L.dim);
                if (r == row && c == chan) { g.setColour(hasKeyboardFocus(false) ? L.green : L.green.withAlpha(0.45f)); g.drawRect(x + 2 + fieldChar(field) * ch, y, fieldLen(field) * ch + 4, rowH, 1); }
                g.setColour(L.ink.withAlpha(0.06f)); g.drawVerticalLine(x, (float) y, (float) (y + rowH));
            }
        }
        // detail strip
        auto d = getLocalBounds().removeFromBottom(detailH).reduced(6, 4); const auto& cell = p.cells[(size_t) chan][(size_t) row];
        g.setColour(L.card.withAlpha(0.95f)); g.fillRoundedRectangle(d.toFloat(), 6);
        juce::String a; a << "CH " << chan + 1 << "  ROW " << hex2(row) << "   NOTE " << gb::mel::noteName(cell.note) << "  INS " << (cell.inst ? hex2(cell.inst) + " " + song->inst[(size_t) (cell.inst - 1)].label() : juce::String("--"))
                          << "  VOL " << (cell.vol != gb::mel::kNoVol ? juce::String((int) cell.vol) : juce::String("--")) << "   OCT " << octave << "  STEP " << step;
        { const auto pr = problem(inst); if (pr.isNotEmpty() && instUsedInSong(inst)) a << "      INS " << hex2(inst + 1) << ": " << pr << " (it plays nothing)"; }
        const int fx = cell.fx; juce::String b = fx ? juce::String(gb::mel::fxName(fx)) + ": " + gb::mel::fxHelp(fx) : juce::String("EFFECT: type 0 1 2 3 4 7 8 9 A B C D E F P Q R Y Z, then two hex digits.   ? = all effects   Enter = hear the row");
        g.setColour(L.ink); g.setFont(mono(11.0f)); g.drawText(a, d.removeFromTop(d.getHeight() / 2), juce::Justification::centredLeft, true);
        g.setColour(L.grey); g.drawText(b, d, juce::Justification::centredLeft, true);
        if (help) paintHelp(g);
    }
    void paintHelp(juce::Graphics& g) {
        const auto& L = look; auto hb = getLocalBounds().reduced(24, 30).toFloat();
        g.setColour(L.card); g.fillRoundedRectangle(hb, 10); g.setColour(L.purple.withAlpha(0.6f)); g.drawRoundedRectangle(hb, 10, 1.4f);
        auto t = hb.reduced(16, 10).toNearestInt(); g.setColour(L.ink); g.setFont(font(13.0f)); g.drawText("EFFECTS  (00 = the last value of that effect on the channel)", t.removeFromTop(20), juce::Justification::centredLeft);
        const int colWd = t.getWidth() / 2; auto left = t.removeFromLeft(colWd), right = t; const float lh = juce::jmin(18.0f, (float) (t.getHeight() - 60) / 10.0f);
        g.setFont(mono(11.0f));
        for (int k = 1; k < gb::mel::FxCount; ++k) { auto& col = k <= 10 ? left : right; auto rr = col.removeFromTop((int) lh); g.setColour(L.amber); g.drawText(gb::mel::fxName(k), rr.removeFromLeft(118), juce::Justification::centredLeft); g.setColour(L.ink); g.drawFittedText(gb::mel::fxHelp(k), rr, juce::Justification::centredLeft, 1, 0.8f); }
        auto foot = getLocalBounds().reduced(40, 30).removeFromBottom(54); g.setColour(L.grey); g.setFont(font(10.5f, false));
        g.drawFittedText("NOTES: Z S X D C V G B H N J M = C..B, Q 2 W 3 E R 5 T 6 Y 7 U = the octave above, 1 = note off, [ ] octave, { } step.  INSERT / shift+DELETE = insert / delete a row.  "
                         "Cmd+up/down = transpose (+shift = octave), Cmd+I = interpolate, Cmd+X/C/V = cut/copy/paste, Cmd+shift+C/V = the whole pattern.  ? closes this.", foot, juce::Justification::topLeft, 3);
    }

    // ---- editing
    gb::mel::Cell& cur() { return P().cells[(size_t) chan][(size_t) row]; }
    void follow1() { const int vr = visRows(); if (row < scroll) scroll = row; if (row >= scroll + vr) scroll = row - vr + 1; scroll = juce::jlimit(0, juce::jmax(0, rows() - vr), scroll); }
    void moveTo(int r, int c, bool shift) { if (shift) { if (selR0 < 0) { selR0 = row; selC0 = chan; } } else selR0 = -1; row = juce::jlimit(0, rows() - 1, r); chan = juce::jlimit(0, gb::mel::kChannels - 1, c); nib = 0; follow1(); repaint(); }
    void advance() { moveTo(row + step, chan, false); }
    void tick() {   // editor timer: FOLLOW the playhead (the edit cursor stays where it is)
        if (!song || !follow) return; const int r = playRow ? playRow() : -1; if (r < 0) return;
        if (song->ownOrder && playPat) { const int pp = playPat(); if (pp >= 0 && pp != pat) { pat = pp; row = juce::jmin(row, rows() - 1); } }
        else if (playPat && playPat() != pat) return;
        const int vr = visRows(); const int want = juce::jlimit(0, juce::jmax(0, rows() - vr), r - vr / 2); if (want != scroll) { scroll = want; repaint(); }
    }
    static int pianoKey(juce::juce_wchar ch) {
        static const char* lo = "zsxdcvgbhnjm,l.;/"; static const char* hi = "q2w3er5t6y7ui9o0p";
        const auto c = juce::CharacterFunctions::toLowerCase(ch);
        for (int i = 0; lo[i]; ++i) if (lo[i] == (char) c) return i;
        for (int i = 0; hi[i]; ++i) if (hi[i] == (char) c) return 12 + i;
        return -1;
    }
    static int hexVal(juce::juce_wchar ch) { if (ch >= '0' && ch <= '9') return ch - '0'; const auto u = juce::CharacterFunctions::toUpperCase(ch); if (u >= 'A' && u <= 'F') return u - 'A' + 10; return -1; }
    void audition(int note, int keyCode) { if (heldNote >= 0 && preview) preview(inst, -1); heldNote = note; heldKey = keyCode; if (preview) preview(inst, note); }
    bool keyStateChanged(bool) override {
        if (heldNote >= 0 && heldKey != 0 && !juce::KeyPress::isKeyCurrentlyDown(heldKey)) { if (preview) preview(inst, -1); heldNote = -1; heldKey = 0; }
        return false;
    }
    void typeNote(int semis, int keyCode) {
        const int midi = juce::jlimit(12, 127, (octave + 1) * 12 + semis);
        edit([&] { auto& c = cur(); c.note = (uint8_t) (midi + 1); if (!c.inst) c.inst = (uint8_t) (inst + 1); });
        audition(midi, keyCode);
        advance();
    }
    // MIDI REC: a note from the keyboard (beat >= 0 while playing = the row under the playhead; else the cursor, then STEP)
    void recordNote(int note, int vel, int atPat, int atRow) {
        if (!song) return;
        const int p = atPat >= 0 ? atPat : pat, r = atRow >= 0 ? atRow : row;
        edit([&] { auto& c = song->pat(p).cells[(size_t) chan][(size_t) juce::jlimit(0, gb::mel::kRows - 1, r)]; c.note = (uint8_t) (juce::jlimit(0, 127, note) + 1); c.inst = (uint8_t) (inst + 1); c.vol = (uint8_t) juce::jlimit(1, 64, (vel * 64 + 63) / 127); });
        if (atRow < 0) advance(); else repaint();
    }
    int typedHi = -1;   // the first hex digit of a two-digit field (applied with the second; minor 28)
    void typeHex(int v) {
        bool done = false;
        if ((field == 1 || field == 2) && nib == 0) { typedHi = v; nib = 1; repaint(); return; }
        edit([&] {
            auto& c = cur();
            if (field == 1) { const int x = typedHi * 16 + v; c.inst = (uint8_t) juce::jlimit(0, gb::mel::kInstruments, x); done = true; }
            else if (field == 2) { const int x = typedHi * 16 + v; c.vol = (uint8_t) juce::jlimit(0, 64, x); done = true; }
            else if (field == 3) { if (nib == 1) c.param = (uint8_t) ((v << 4) | (c.param & 15)); else if (nib == 2) { c.param = (uint8_t) ((c.param & 0xf0) | v); done = true; } }
        });
        if (field == 1 && cur().inst) inst = cur().inst - 1;
        if (done) { typedHi = -1; advance(); } else { ++nib; repaint(); }
    }
    void blockBounds(int& r0, int& r1, int& c0, int& c1) const { r0 = selR0 < 0 ? row : juce::jmin(selR0, row); r1 = selR0 < 0 ? row : juce::jmax(selR0, row); c0 = selR0 < 0 ? chan : juce::jmin(selC0, chan); c1 = selR0 < 0 ? chan : juce::jmax(selC0, chan); }
    void insertRow(bool del) {
        int r0, r1, c0, c1; blockBounds(r0, r1, c0, c1); const int nr = rows();
        edit([&] { for (int c = c0; c <= c1; ++c) { auto& col = P().cells[(size_t) c];
            if (!del) { for (int r = nr - 1; r > row; --r) col[(size_t) r] = col[(size_t) r - 1]; col[(size_t) row] = {}; }
            else { for (int r = row; r < nr - 1; ++r) col[(size_t) r] = col[(size_t) r + 1]; col[(size_t) nr - 1] = {}; } } });
        repaint();
    }
    void transpose(int d) {
        edit([&] { forBlock([&](gb::mel::Cell& c) { if (c.note >= 1 && c.note <= 128) c.note = (uint8_t) juce::jlimit(13, 128, (int) c.note + d); }); });
        if (say) say("Transposed " + juce::String(d > 0 ? "+" : "") + juce::String(d) + " semitone" + (std::abs(d) == 1 ? "" : "s") + "."); repaint();
    }
    void interpolate() {
        int r0, r1, c0, c1; blockBounds(r0, r1, c0, c1); if (r1 - r0 < 2) { if (say) say("Select at least 3 rows to interpolate."); return; }
        edit([&] { for (int c = c0; c <= c1; ++c) { auto& col = P().cells[(size_t) c]; const auto a = col[(size_t) r0], b = col[(size_t) r1];
            for (int r = r0 + 1; r < r1; ++r) { const float u = (float) (r - r0) / (float) (r1 - r0); auto& x = col[(size_t) r];
                if (a.vol != gb::mel::kNoVol && b.vol != gb::mel::kNoVol) x.vol = (uint8_t) std::lround(a.vol + (b.vol - a.vol) * u);
                if (a.fx && a.fx == b.fx) { x.fx = a.fx; x.param = (uint8_t) std::lround(a.param + (b.param - a.param) * u); } } } });
        if (say) say("Interpolated volume and effect values over the selection."); repaint();
    }
    bool keyPressed(const juce::KeyPress& k) override {
        if (!song) return false;
        const auto m = k.getModifiers(); const bool shift = m.isShiftDown(), cmdKey = m.isCommandDown();
        if (k.getKeyCode() == juce::KeyPress::spaceKey) return false;
        const int kc = k.getKeyCode() >= 'a' && k.getKeyCode() <= 'z' ? k.getKeyCode() - 32 : k.getKeyCode();   // letters: either case   // SPACE is the transport (SYNC OWN play / stop; the DAW's in DAW mode)
        if (cmdKey && k.isKeyCode(juce::KeyPress::upKey)) { transpose(shift ? 12 : 1); return true; }
        if (cmdKey && k.isKeyCode(juce::KeyPress::downKey)) { transpose(shift ? -12 : -1); return true; }
        if (k.isKeyCode(juce::KeyPress::upKey)) { moveTo(row - 1, chan, shift); return true; }
        if (k.isKeyCode(juce::KeyPress::downKey)) { moveTo(row + 1, chan, shift); return true; }
        if (k.isKeyCode(juce::KeyPress::leftKey)) { if (!shift && field > 0) { --field; nib = 0; repaint(); } else if (chan > 0) { field = shift ? field : 3; moveTo(row, chan - 1, shift); } return true; }
        if (k.isKeyCode(juce::KeyPress::rightKey)) { if (!shift && field < 3) { ++field; nib = 0; repaint(); } else if (chan < gb::mel::kChannels - 1) { field = shift ? field : 0; moveTo(row, chan + 1, shift); } return true; }
        if (k.isKeyCode(juce::KeyPress::tabKey)) { field = 0; moveTo(row, (chan + (shift ? gb::mel::kChannels - 1 : 1)) % gb::mel::kChannels, false); return true; }
        if (k.isKeyCode(juce::KeyPress::pageDownKey)) { moveTo(row + visRows(), chan, shift); return true; }
        if (k.isKeyCode(juce::KeyPress::pageUpKey)) { moveTo(row - visRows(), chan, shift); return true; }
        if (k.isKeyCode(juce::KeyPress::homeKey)) { moveTo(0, chan, shift); return true; }
        if (k.isKeyCode(juce::KeyPress::endKey)) { moveTo(rows() - 1, chan, shift); return true; }
        if (k.isKeyCode(juce::KeyPress::insertKey)) { insertRow(false); return true; }
        if (k.isKeyCode(juce::KeyPress::returnKey)) { auditionRow(); return true; }
        if (k.isKeyCode(juce::KeyPress::deleteKey) || k.isKeyCode(juce::KeyPress::backspaceKey)) {
            if (shift) { insertRow(true); return true; }
            edit([&] { forBlock([&](gb::mel::Cell& c) { if (selR0 >= 0) { c = {}; return; } switch (field) { case 0: c.note = 0; c.inst = 0; break; case 1: c.inst = 0; break; case 2: c.vol = gb::mel::kNoVol; break; default: c.fx = 0; c.param = 0; } }); });
            if (selR0 < 0) advance(); else repaint(); return true;
        }
        if (cmdKey && shift && kc == 'C') { patClip = std::make_unique<gb::mel::Pattern>(P()); if (say) say("Copied pattern " + juce::String::charToString((juce::juce_wchar) ('A' + pat)) + "."); return true; }
        if (cmdKey && shift && kc == 'V') { if (patClip) { edit([&] { P() = *patClip; }); if (say) say("Pasted the whole pattern."); repaint(); } return true; }
        if (cmdKey && kc == 'C') { copyBlock(); if (say) say("Copied the block."); return true; }
        if (cmdKey && kc == 'X') { copyBlock(); edit([&] { forBlock([](gb::mel::Cell& c) { c = {}; }); }); repaint(); return true; }
        if (cmdKey && kc == 'V') { pasteBlock(); return true; }
        if (cmdKey && kc == 'I') { interpolate(); return true; }
        if (cmdKey) return false;
        const auto ch = k.getTextCharacter();
        if (ch == '?') { help = !help; repaint(); return true; }
        if (ch == '[') { octave = juce::jmax(0, octave - 1); repaint(); return true; }
        if (ch == ']') { octave = juce::jmin(8, octave + 1); repaint(); return true; }
        if (ch == '{') { step = juce::jmax(0, step - 1); repaint(); return true; }
        if (ch == '}') { step = juce::jmin(16, step + 1); repaint(); return true; }
        if (field == 0) {
            if (ch == '1') { edit([&] { cur().note = gb::mel::kNoteOff; }); advance(); return true; }
            const int s = pianoKey(ch); if (s >= 0) { typeNote(s, k.getKeyCode()); return true; }
            return false;
        }
        if (field == 3 && nib == 0) { const int fx = gb::mel::fxFromChar(ch); if (fx > 0) { edit([&] { cur().fx = (uint8_t) fx; }); nib = 1; repaint(); return true; } return false; }
        const int v = hexVal(ch); if (v >= 0) { typeHex(v); return true; }
        return false;
    }
    void auditionRow() {   // Enter: hear the cursor row's note on the cursor channel (or the row's first note)
        const auto& c = cur(); int note = c.note >= 1 && c.note <= 128 ? c.note - 1 : -1, ii = c.inst ? c.inst - 1 : inst;
        if (note < 0) for (int cc = 0; cc < gb::mel::kChannels && note < 0; ++cc) { const auto& x = P().cells[(size_t) cc][(size_t) row]; if (x.note >= 1 && x.note <= 128) { note = x.note - 1; ii = x.inst ? x.inst - 1 : inst; } }
        if (note >= 0 && preview) { preview(ii, note); juce::Timer::callAfterDelay(300, [this, safe = juce::Component::SafePointer<MelodyTrackerView>(this)] { if (safe && preview) preview(inst, -1); }); }
    }
    void forBlock(const std::function<void(gb::mel::Cell&)>& f) { int r0, r1, c0, c1; blockBounds(r0, r1, c0, c1); for (int c = c0; c <= c1; ++c) for (int r = r0; r <= r1; ++r) f(P().cells[(size_t) c][(size_t) r]); }
    void copyBlock() { int r0, r1, c0, c1; blockBounds(r0, r1, c0, c1); clip.assign((size_t) (c1 - c0 + 1), {}); for (int c = c0; c <= c1; ++c) for (int r = r0; r <= r1; ++r) clip[(size_t) (c - c0)].push_back(P().cells[(size_t) c][(size_t) r]); }
    void pasteBlock() {
        if (clip.empty()) return;
        edit([&] { for (size_t c = 0; c < clip.size(); ++c) for (size_t r = 0; r < clip[c].size(); ++r) { const int cc = chan + (int) c, rr = row + (int) r; if (cc < gb::mel::kChannels && rr < gb::mel::kRows) P().cells[(size_t) cc][(size_t) rr] = clip[c][r]; } });
        repaint();
    }
    void mouseDown(const juce::MouseEvent& e) override {
        grabKeyboardFocus(); if (!song) return;
        if (help) { help = false; repaint(); return; }
        const int f = stripAt(e.getPosition());
        if (f >= 0) { changeStrip(f, e.mods.isRightButtonDown() || e.mods.isShiftDown() ? -1 : 1); return; }
        for (size_t k = 0; k < orderCells.size(); ++k) if (orderCells[k].contains(e.getPosition())) {
            if (e.mods.isRightButtonDown()) { if (song->order.size() > 1) edit([&] { song->order.erase(song->order.begin() + (long) k); }); orderPos = juce::jmin(orderPos, (int) song->order.size() - 1); }
            else { orderPos = (int) k; pat = song->order[k]; row = juce::jmin(row, rows() - 1); }
            layoutStrip(); repaint(); return; }
        if (orderAdd.contains(e.getPosition())) { if ((int) song->order.size() < gb::mel::kOrderMax) edit([&] { song->order.push_back((uint8_t) pat); }); orderPos = (int) song->order.size() - 1; layoutStrip(); repaint(); return; }
        if (e.x < listW) {   // instrument list
            const int y0 = gridY() + headH, ih = juce::jmin(22, (getHeight() - gridY() - detailH - 2 - headH) / gb::mel::kInstruments); const int i = (e.y - y0) / juce::jmax(1, ih);
            if (juce::isPositiveAndBelow(i, gb::mel::kInstruments)) { inst = i; if (e.mods.isPopupMenu()) soundMenu(); repaint(); } return;
        }
        const int c = (e.x - gridX() - numW) / juce::jmax(1, colW()), r = scroll + (e.y - gridY() - headH) / rowH;
        if (c < 0 || c >= gb::mel::kChannels || e.y < gridY() + headH || r >= rows()) return;
        const int cx = (e.x - gridX() - numW - c * colW() - 4) / charW(); int fl = 0; for (int q = 0; q < 4; ++q) if (cx >= fieldChar(q)) fl = q;
        field = fl; moveTo(r, c, e.mods.isShiftDown());
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override { if (song && e.x >= gridX()) auditionRow(); }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override {
        if (!song) return;
        const int f = stripAt(e.getPosition()); if (f >= 0) { if (f != SName && f != SSound && f != SRoute && f != SFollow && f != SRec && f != SOrderMode && f != SOn && f != SFollowPitch) changeStrip(f, w.deltaY > 0 ? 1 : -1); return; }
        for (size_t k = 0; k < orderCells.size(); ++k) if (orderCells[k].contains(e.getPosition())) { edit([&] { song->order[k] = (uint8_t) juce::jlimit(0, gb::mel::kPats - 1, song->order[k] + (w.deltaY > 0 ? 1 : -1)); }); if ((int) k == orderPos) pat = song->order[k]; repaint(); return; }
        if (e.x < listW) { inst = juce::jlimit(0, gb::mel::kInstruments - 1, inst + (w.deltaY > 0 ? -1 : 1)); repaint(); return; }
        follow = false; scroll = juce::jlimit(0, juce::jmax(0, rows() - visRows()), scroll - (int) std::round(w.deltaY * 12)); repaint();
    }
    void focusLost(FocusChangeType) override { if (heldNote >= 0 && preview) preview(inst, -1); heldNote = -1; heldKey = 0; repaint(); }
    void focusGained(FocusChangeType) override { repaint(); }
};

} // namespace bx
