#pragma once
// TEMPO  ½x | 1x | 2x  (Brandon 2026-11-07: "if the DAW's at 130 you gotta be able to put it at 130 or half of that or double that"),
// shared by every GhostBlock plugin that plays or exports anything in time (Ghosty Beats, ArpEZator, Ghost Slice, Ghost Hum).
//
//   1x  (default, a fresh start): the DAW's tempo, exactly as before.
//   2x  the plugin runs at double the DAW's tempo, still locked to the DAW's bars (two of its bars in every DAW bar).
//   ½x  half the DAW's tempo (one of its bars spans two DAW bars, starting on an odd DAW bar).
//
// The plugin multiplies the host's tempo AND position by factor() before its own clock sees them, so everything downstream (pattern position,
// swing, echo / delay sync, recording) runs at the effective tempo and stays locked to the DAW's grid.
// Exports: audio is rendered at the effective tempo (exactly what you hear; the file name says the effective BPM). MIDI is written at the
// DAW's tempo with every time scaled (scaleMidiToHost), so dropped on the DAW's timeline it plays back exactly as heard (a 2x one-bar pattern
// fills half a DAW bar; ½x stretches over two bars).
// Saved with the project (stateKey, written only when not 1x). Nothing here allocates on the audio thread.
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "GhostThemes.h"
#include <cmath>
#include <functional>

namespace ghosty::tempomul {

enum { Half = 0, Normal = 1, Double = 2 };
inline constexpr const char* stateKey = "tempomul";
inline int clampCode(int c) { return c < Half || c > Double ? Normal : c; }
inline double factor(int code) { code = clampCode(code); return code == Half ? 0.5 : code == Double ? 2.0 : 1.0; }
inline juce::String multName(int code) { code = clampCode(code); return code == Half ? juce::String::fromUTF8("\xc2\xbd" "x") : code == Double ? "2x" : "1x"; }
inline juce::String bpmText(double bpm) { const double r = std::round(bpm * 10.0) / 10.0; return std::abs(r - std::round(r)) < 1e-6 ? juce::String((int) std::lround(r)) : juce::String(r, 1); }
// a file-name tag: "" at 1x, otherwise e.g. " 2x" (callers put the effective BPM in the name themselves)
inline juce::String fileTag(int code) { code = clampCode(code); return code == Normal ? juce::String() : code == Double ? juce::String(" 2x") : juce::String(" half-time"); }

// MIDI made in the plugin's own (effective) beats -> the DAW's beats: every time stamp / f, every tempo event * (1 / f)
// (so a file whose tempo said the effective BPM now says the DAW's BPM, and plays back exactly as heard on the DAW's timeline)
inline void scaleMidiToHost(juce::MidiFile& mf, double f) {
    if (std::abs(f - 1.0) < 1e-9 || f <= 0.0) return;
    juce::MidiFile out; out.setTicksPerQuarterNote(mf.getTimeFormat() > 0 ? mf.getTimeFormat() : 960);
    for (int t = 0; t < mf.getNumTracks(); ++t) {
        juce::MidiMessageSequence s;
        for (auto* e : *mf.getTrack(t)) {
            auto m = e->message; const double ts = m.getTimeStamp() / f;
            if (m.isTempoMetaEvent()) m = juce::MidiMessage::tempoMetaEvent((int) std::llround(m.getTempoSecondsPerQuarterNote() * 1.0e6 * f));
            m.setTimeStamp(ts); s.addEvent(m);
        }
        s.updateMatchedPairs(); out.addTrack(s);
    }
    mf = std::move(out);
}

// the header switch: TEMPO [ ½x 65 | 1x 130 | 2x 260 ]. Shows the effective BPM of each choice for the tempo it is given (the DAW's, or the
// plugin's own in SYNC: OWN). setTheme() follows the plugin's look like ghosty::SyncButton.
class Switch : public juce::Component, public juce::SettableTooltipClient {
public:
    Switch() {
        setTooltip(juce::String::fromUTF8("TEMPO: play at the DAW's tempo (1x), half of it (\xc2\xbd" "x) or double it (2x). It stays locked to the DAW's bars. "
                                          "DRAG MIDI lands in the DAW exactly as you hear it; DRAG AUDIO is rendered as you hear it (the file name says the BPM). "
                                          "Saved with the project."));
    }
    std::function<void(int)> onChange;
    int sel = Normal; double baseBpm = 120.0; bool showCaption = true;
    void set(int code, double bpm) { code = clampCode(code); if (code != sel || std::abs(bpm - baseBpm) > 1e-6) { sel = code; baseBpm = bpm; repaint(); } }
    void setTheme(themes::Theme t) { theme = t; themed = true; custom = false; repaint(); }   // (also ends setColours())
    void setColours(juce::Colour bg, juce::Colour ink, juce::Colour accent, juce::Colour accentInk) { cBg = bg; cInk = ink; cAcc = accent; cAccInk = accentInk; themed = false; custom = true; repaint(); }
    juce::Rectangle<float> segment(int i) const {
        auto r = getLocalBounds().toFloat().reduced(1.0f);
        if (showCaption && r.getWidth() >= 140.0f) r.removeFromLeft(captionW());
        const float w = r.getWidth() / 3.0f; return { r.getX() + w * (float) i, r.getY(), w, r.getHeight() };
    }
    void mouseDown(const juce::MouseEvent& e) override {
        for (int i = 0; i < 3; ++i) if (segment(i).contains(e.position)) { if (i != sel) { sel = i; repaint(); if (onChange) onChange(i); } return; }
    }
    void paint(juce::Graphics& g) override {
        const auto& pal = themes::get(theme);
        const auto bg = custom ? cBg : themed ? pal.cell : juce::Colour(0x33ffffff);
        const auto ink = custom ? cInk : themed ? pal.ink : juce::Colour(0xffe9e4f5);
        const auto acc = custom ? cAcc : themed ? themes::readableOn(pal.accent2, pal.window, 3.0) : juce::Colour(0xff4cd0ff);
        const auto accInk = custom ? cAccInk : juce::Colour(0xff001a24);
        auto r = getLocalBounds().toFloat().reduced(1.0f); const float rad = juce::jmin(10.0f, r.getHeight() * 0.5f);
        g.setColour(bg); g.fillRoundedRectangle(r, rad);
        g.setColour(ink.withAlpha(0.45f)); g.drawRoundedRectangle(r, rad, 1.2f);
        if (showCaption && r.getWidth() >= 140.0f) {
            g.setColour(ink.withAlpha(0.8f)); g.setFont(juce::Font(juce::FontOptions(juce::jmin(13.0f, r.getHeight() * 0.3f), juce::Font::bold)));
            g.drawText("TEMPO", r.withWidth(captionW()), juce::Justification::centred);
        }
        for (int i = 0; i < 3; ++i) {
            auto s = segment(i); const bool on = i == sel;
            if (on) { g.setColour(acc); g.fillRoundedRectangle(s.reduced(2.0f), juce::jmax(2.0f, rad - 2.0f)); }
            else if (i > 0 && sel != i - 1) { g.setColour(ink.withAlpha(0.2f)); g.drawVerticalLine((int) s.getX(), s.getY() + 5.0f, s.getBottom() - 5.0f); }
            const auto col = on ? accInk : ink;
            const bool twoLines = s.getHeight() >= 26.0f;
            if (twoLines) {
                auto top = s.withTrimmedBottom(s.getHeight() * 0.5f).withTrimmedTop(2.0f), bot = s.withTrimmedTop(s.getHeight() * 0.45f).withTrimmedBottom(2.0f);
                g.setColour(col.withAlpha(on ? 0.85f : 0.65f)); g.setFont(juce::Font(juce::FontOptions(juce::jmin(13.0f, s.getHeight() * 0.32f), juce::Font::bold)));
                g.drawText(multName(i), top.toNearestInt(), juce::Justification::centred);
                g.setColour(col); g.setFont(juce::Font(juce::FontOptions(juce::jmin(19.0f, s.getHeight() * 0.44f), juce::Font::bold)));
                g.drawFittedText(bpmText(baseBpm * factor(i)), bot.toNearestInt(), juce::Justification::centred, 1, 0.8f);
            } else {
                g.setColour(col); g.setFont(juce::Font(juce::FontOptions(juce::jmin(12.0f, s.getHeight() * 0.6f), juce::Font::bold)));
                g.drawFittedText(multName(i) + " " + bpmText(baseBpm * factor(i)), s.toNearestInt(), juce::Justification::centred, 1, 0.75f);
            }
        }
    }
private:
    float captionW() const { return 46.0f; }
    themes::Theme theme = themes::Theme::Dark; bool themed = false, custom = false;
    juce::Colour cBg, cInk, cAcc, cAccInk;
};

} // namespace ghosty::tempomul
