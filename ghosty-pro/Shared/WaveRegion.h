// WaveRegion: a waveform with START / END handles, a LOOP region and the GhostBlock gestures (the same everywhere):
//   left-drag a handle ............ move START / END / the loop's ends (a time readout follows)
//   left-drag inside the loop ..... move the loop
//   right-click (two-finger tap) .. START here (the play-from point)
//   right-drag (two-finger drag) .. make a loop over that span (turns the loop ON)
//   the WAV grip .................. drag the highlighted region (the loop when ON, else START..END) out of the plugin
//   the LOOP pill / double-click the loop ... loop on / off
//   a plain left-click elsewhere .. audition (onAudition down / up); it never moves anything or makes a loop
// Header-only (JUCE gui_basics + the export gate, ExportGate.h). Colours come from the host's theme through `palette` (light + dark).
// Positions are fractions 0..1 of the sample; the host owns the truth: call setRegion() when its state changes and
// apply onChange() when the user edits.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "ExportGate.h"
#include <functional>
#include <vector>

namespace ghosty
{
class WaveRegion : public juce::Component, public juce::SettableTooltipClient
{
public:
    struct Region
    {
        float start = 0.0f, end = 1.0f, loopStart = 0.0f, loopEnd = 1.0f; bool loopOn = false;
        bool operator== (const Region& o) const { using juce::exactlyEqual; return exactlyEqual (start, o.start) && exactlyEqual (end, o.end) && exactlyEqual (loopStart, o.loopStart) && exactlyEqual (loopEnd, o.loopEnd) && loopOn == o.loopOn; }
        bool operator!= (const Region& o) const { return ! (*this == o); }
    };
    struct Palette
    {
        juce::Colour bg { 0xff17121f }, line { 0xff3a3150 }, wave { 0xff46d9ff }, waveOut { 0xff6e6585 }, ink { 0xffeee9f8 }, dim { 0xff9a91b0 },
                     start { 0xff4fe08a }, end { 0xffff6fa8 }, loop { 0xffffc94d }, label { 0xff120c1a }, card { 0xff231b30 };
        static Palette light()
        { Palette p; p.bg = juce::Colour (0xfff6f2fb); p.line = juce::Colour (0xffd6cde6); p.wave = juce::Colour (0xff1f8fc0); p.waveOut = juce::Colour (0xffb2a9c4); p.ink = juce::Colour (0xff2c2540);
          p.dim = juce::Colour (0xff7a7090); p.start = juce::Colour (0xff1f9d57); p.end = juce::Colour (0xffd1427e); p.loop = juce::Colour (0xffc48a00); p.label = juce::Colour (0xffffffff); p.card = juce::Colour (0xffffffff); return p; }
    };

    // ---- the host's hooks
    std::function<Palette()> palette;                               // light / dark: return the current theme's colours
    std::function<void (const Region&)> onChange;                   // the user moved something (during the drag too)
    bool clickAnywhereAuditions = false;                            // true: a plain click on a flag / handle / the strip under the wave plays it too
    std::function<void (bool down, float frac)> onAudition;         // a plain left-click on the wave: hear it (down, then up)
    std::function<bool()> canExport;                                // the export gate: wire it to ghosty::exportAllowed (licence, this, id); unset or false = no drag-out
    std::function<juce::File (float a, float b)> exportRegion;      // write fractions a..b as a WAV, return it (empty = failed)
    std::function<void (const juce::String&)> onMessage;            // short user messages (a toast)
    std::function<void (const juce::StringArray&)> performDrag;     // default: exportgate::dragFiles (tests swap it)
    float minGap = 0.002f;                                          // the smallest START..END / loop, as a fraction
    bool showExportGrip = true;                                     // false: no WAV grip at all (a host with no drag-out, e.g. Poltergeist's ADD SAMPLE)
    juce::String hint { "right-click: start here   |   right-drag: loop   |   drag the handles   |   drag WAV out" };

    WaveRegion() { setRepaintsOnMouseActivity (false); setTooltip ("Drag START / END. Right-click = start here, right-drag = loop (two fingers on a trackpad). Drag the WAV grip out to export the highlighted part."); }

    void setPeaks (std::vector<float> p) { peaks = std::move (p); repaint(); }   // |peak| per bin, 0..1, any count
    void setLengthSeconds (double s) { seconds = juce::jmax (0.0, s); repaint(); }
    void setRegion (const Region& r) { if (r != reg) { reg = r; repaint(); } }
    const Region& region() const { return reg; }
    bool hasWave() const { return ! peaks.empty() && seconds > 0.0; }

    // ---- geometry (public for tests and for hosts that place things around it)
    juce::Rectangle<float> waveArea() const { return getLocalBounds().toFloat().withTrimmedTop (kTop).withTrimmedBottom (kBottom); }
    float xOf (float f) const { auto w = waveArea(); return w.getX() + juce::jlimit (0.0f, 1.0f, f) * w.getWidth(); }
    float fracOf (float x) const { auto w = waveArea(); return juce::jlimit (0.0f, 1.0f, (x - w.getX()) / juce::jmax (1.0f, w.getWidth())); }
    juce::Rectangle<float> handleFlag (int which) const   // 0 START, 1 END (the labelled flags on the top row)
    {
        const float x = xOf (which == 0 ? reg.start : reg.end), w = 76;
        return { which == 0 ? juce::jmin (x, (float) getWidth() - w) : juce::jmax (0.0f, x - w), 1.0f, w, kTop - 2.0f };
    }
    juce::Rectangle<float> loopPill() const { return { (float) getWidth() - 86.0f, (float) getHeight() - kBottom + 2.0f, 84.0f, kBottom - 4.0f }; }
    juce::Rectangle<float> gripRect() const   // the WAV drag-out grip, on the highlighted region
    {
        if (! hasWave() || ! showExportGrip) return {};
        auto w = waveArea(); const auto [a, b] = highlighted();
        const float x1 = xOf (b), gw = 40, gh = 18; float gx = x1 - gw - 4; if (gx < xOf (a) + 4) gx = juce::jmin (w.getRight() - gw - 2, x1 + 4);
        return { gx, w.getY() + 4, gw, gh };
    }
    std::pair<float, float> highlighted() const { return reg.loopOn ? std::make_pair (reg.loopStart, reg.loopEnd) : std::make_pair (reg.start, reg.end); }
    juce::String timeAt (float f) const { const double t = f * seconds; return t < 10.0 ? juce::String (t, 3) + " s" : juce::String (t, 2) + " s"; }

    // ---- painting
    void paint (juce::Graphics& g) override
    {
        const Palette P = palette ? palette() : Palette();
        auto W = getLocalBounds().toFloat(), w = waveArea();
        g.setColour (P.bg); g.fillRoundedRectangle (W, 8); g.setColour (P.line); g.drawRoundedRectangle (W.reduced (0.5f), 8, 1.0f);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        if (! hasWave())
        {
            g.setColour (P.dim); const float dash[] { 6, 5 }; juce::Path box; box.addRoundedRectangle (w.reduced (14), 10); juce::Path d;
            juce::PathStrokeType (1.5f).createDashedStroke (d, box, dash, 2); g.fillPath (d);
            g.setFont (juce::FontOptions (15.0f, juce::Font::bold)); g.drawText (emptyText, w, juce::Justification::centred); return;
        }
        const float s0 = xOf (reg.start), e0 = xOf (reg.end), mid = w.getCentreY(), hh = w.getHeight() * 0.44f;
        const int cols = juce::jmax (1, (int) w.getWidth()), np = (int) peaks.size();
        for (int x = 0; x < cols; ++x)
        {
            const int a = x * np / cols, b = juce::jmax (a + 1, (x + 1) * np / cols); float pk = 0; for (int i = a; i < b && i < np; ++i) pk = juce::jmax (pk, peaks[(size_t) i]);
            const float px = w.getX() + (float) x; const bool in = px >= s0 && px <= e0;
            g.setColour (in ? P.wave.withAlpha (0.9f) : P.waveOut.withAlpha (0.55f)); g.fillRect (px, mid - pk * hh, 1.25f, juce::jmax (1.0f, 2.0f * pk * hh));
        }
        g.setColour (P.ink.withAlpha (0.10f)); g.fillRect (w.getX(), w.getY(), s0 - w.getX(), w.getHeight()); g.fillRect (e0, w.getY(), w.getRight() - e0, w.getHeight());
        if (reg.loopEnd > reg.loopStart)
        {   // the loop: a band over the wave + a bar on the bottom row; dim when off
            const float l0 = xOf (reg.loopStart), l1 = xOf (reg.loopEnd);
            g.setColour (P.loop.withAlpha (reg.loopOn ? 0.18f : 0.06f)); g.fillRect (l0, w.getY(), l1 - l0, w.getHeight());
            g.setColour (P.loop.withAlpha (reg.loopOn ? 1.0f : 0.4f)); for (float x : { l0, l1 }) g.fillRect (x - 1.0f, w.getY(), 2.0f, w.getHeight());
            auto bar = juce::Rectangle<float> (l0, w.getBottom() + 2, l1 - l0, 5); g.fillRoundedRectangle (bar, 2);
        }
        // START / END: lines + labelled flags with the time
        for (int k = 0; k < 2; ++k)
        {
            const auto c = k == 0 ? P.start : P.end; const float x = k == 0 ? s0 : e0;
            g.setColour (c); g.fillRect (x - 1.5f, 1.0f, 3.0f, w.getBottom() - 1.0f);
            auto f = handleFlag (k); g.fillRoundedRectangle (f, 4); g.setColour (P.label);
            g.drawText ((k == 0 ? "START " : "END ") + timeAt (k == 0 ? reg.start : reg.end), f, juce::Justification::centred, true);
            // a grab knob halfway down the line
            auto knob = juce::Rectangle<float> (x - 5, w.getCentreY() - 9, 10, 18); g.setColour (c); g.fillRoundedRectangle (knob, 3);
            g.setColour (P.label.withAlpha (0.8f)); for (int i = -1; i <= 1; ++i) g.fillRect (x - 2.5f, w.getCentreY() + (float) i * 4 - 0.5f, 5.0f, 1.0f);
        }
        if (drag >= 0 && drag <= 3)
        {   // the readout for the handle being dragged
            const float f = drag == 0 ? reg.start : drag == 1 ? reg.end : drag == 2 ? reg.loopStart : reg.loopEnd;
            auto r = juce::Rectangle<float> (juce::jlimit (w.getX(), w.getRight() - 70, xOf (f) - 35), w.getBottom() - 20, 70, 16);
            g.setColour (P.card); g.fillRoundedRectangle (r, 3); g.setColour (P.ink); g.drawText (timeAt (f), r, juce::Justification::centred);
        }
        if (showExportGrip)
        {   // the WAV grip
            auto gr = gripRect(); g.setColour (P.card); g.fillRoundedRectangle (gr, 3); g.setColour (reg.loopOn ? P.loop : P.wave); g.drawRoundedRectangle (gr, 3, 1.2f);
            g.setColour (P.ink); g.drawText ("WAV", gr, juce::Justification::centred);
        }
        // the bottom row: the hint + LOOP ON / OFF
        auto pill = loopPill(); g.setColour (reg.loopOn ? P.loop : P.card); g.fillRoundedRectangle (pill, 4); g.setColour (reg.loopOn ? P.label : P.dim);
        g.drawRoundedRectangle (pill, 4, 1.0f); g.drawText (reg.loopOn ? "LOOP ON" : "LOOP OFF", pill, juce::Justification::centred);
        g.setColour (P.dim); g.setFont (juce::FontOptions (9.5f));
        g.drawText (hint, juce::Rectangle<float> (8, (float) getHeight() - kBottom, pill.getX() - 12, kBottom), juce::Justification::centredLeft, true);
    }

    // ---- gestures
    void mouseDown (const juce::MouseEvent& e) override
    {
        drag = -1; moved = false; down0 = e.position; reg0 = reg; auditioning = false;
        if (! hasWave()) return;
        const auto p = e.position;
        if (loopPill().contains (p)) { if (! e.mods.isPopupMenu()) toggleLoop(); return; }
        if (gripRect().contains (p) && ! e.mods.isPopupMenu()) { drag = kGrip; return; }
        if (e.mods.isPopupMenu()) { drag = kRight; return; }   // a click = START here, a drag = a loop (decided on the way)
        if (handleFlag (0).contains (p)) { drag = 0; return; }
        if (handleFlag (1).contains (p)) { drag = 1; return; }
        const int h = handleAt (p.x); if (h >= 0) { drag = h; return; }
        const float f = fracOf (p.x);
        if (reg.loopOn && f > reg.loopStart && f < reg.loopEnd && p.y > waveArea().getBottom() - 4) { drag = kMoveLoop; return; }   // the loop bar
        if (waveArea().contains (p)) { auditioning = true; if (onAudition) onAudition (true, f); }
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag < 0) return;
        if (! moved && e.getDistanceFromDragStart() < kDragPx) return;   // a click (or a trackpad wobble) never moves anything
        const bool first = ! moved; moved = true;
        const float f = fracOf (e.position.x), f0 = fracOf (down0.x);
        Region r = reg;
        switch (drag)
        {
            // a handle keeps its offset from the mouse (grabbed by its flag or its line: no jump)
            case 0: r.start = juce::jlimit (0.0f, r.end - minGap, reg0.start + f - f0); break;
            case 1: r.end = juce::jlimit (r.start + minGap, 1.0f, reg0.end + f - f0); break;
            case 2: r.loopStart = juce::jlimit (r.start, r.loopEnd - minGap, reg0.loopStart + f - f0); break;
            case 3: r.loopEnd = juce::jlimit (r.loopStart + minGap, r.end, reg0.loopEnd + f - f0); break;
            case kMoveLoop: { const float len = reg0.loopEnd - reg0.loopStart; const float a = juce::jlimit (reg0.start, juce::jmax (reg0.start, reg0.end - len), reg0.loopStart + f - f0); r.loopStart = a; r.loopEnd = a + len; break; }
            case kRight:
            {   // a right-drag: the loop over the span (START / END widen to hold it)
                const float a = juce::jmin (f, f0), b = juce::jmax (juce::jmax (f, f0), juce::jmin (f, f0) + minGap);
                r.loopStart = a; r.loopEnd = juce::jmin (1.0f, b); r.loopOn = true; r.start = juce::jmin (r.start, a); r.end = juce::jmax (r.end, r.loopEnd); break;
            }
            case kGrip: if (first) startExport(); return;
            default: return;
        }
        commit (r);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (auditioning) { auditioning = false; if (onAudition) onAudition (false, fracOf (e.position.x)); }
        else if (clickAnywhereAuditions && ! moved && ! e.mods.isPopupMenu() && hasWave() && drag != kGrip && ! loopPill().contains (down0) && e.getDistanceFromDragStart() < kDragPx && onAudition)
        {   // (2026-11-07) a plain click anywhere else on the sound (its START / END flags, a handle, the strip under it) plays it too:
            // the wave strip is thin in a small window, and a click that lands on a flag used to do nothing
            onAudition (true, fracOf (down0.x)); onAudition (false, fracOf (down0.x));
        }
        if (drag == kRight && ! moved)
        {   // right-click: START here
            Region r = reg; r.start = juce::jlimit (0.0f, r.end - minGap, fracOf (down0.x));
            if (r.loopOn && r.loopStart < r.start) { r.loopStart = r.start; r.loopEnd = juce::jmax (r.loopEnd, r.start + minGap); }
            commit (r); if (onMessage) onMessage ("START at " + timeAt (r.start));
        }
        drag = -1; moved = false; repaint();
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const float f = fracOf (e.position.x);
        if (! e.mods.isPopupMenu() && reg.loopEnd > reg.loopStart && f > reg.loopStart && f < reg.loopEnd && handleAt (e.position.x) < 0 && ! loopPill().contains (e.position)) toggleLoop();
    }
    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto p = e.position;
        setMouseCursor (gripRect().contains (p) ? juce::MouseCursor::DraggingHandCursor
                        : handleFlag (0).contains (p) || handleFlag (1).contains (p) || handleAt (p.x) >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    }

    void toggleLoop()
    {
        Region r = reg; r.loopOn = ! r.loopOn;
        if (r.loopOn && ! (r.loopEnd > r.loopStart)) { r.loopStart = r.start; r.loopEnd = r.end; }
        commit (r); if (onMessage) onMessage (r.loopOn ? "LOOP ON: it holds inside the loop" : "LOOP OFF: plays START to END");
    }
    void startExport()
    {
        if (! canExport || ! canExport()) { if (onMessage) onMessage ("Drag-out is part of the full version (UNLOCK)"); return; }   // fail closed: no gate wired = no export
        if (! exportRegion) return;
        const auto [a, b] = highlighted(); const auto f = exportRegion (a, b);
        if (! f.existsAsFile()) { if (onMessage) onMessage ("Couldn't write the WAV"); return; }
        const juce::StringArray files { f.getFullPathName() };
        if (performDrag) performDrag (files); else exportgate::dragFiles (files, this);
    }
    int handleAt (float x) const   // the nearest handle within reach: 0 START, 1 END, 2 / 3 the loop's ends (only when it shows)
    {
        int best = -1; float bd = kReach;
        const float xs[4] { xOf (reg.start), xOf (reg.end), xOf (reg.loopStart), xOf (reg.loopEnd) };
        for (int k = 0; k < 4; ++k) { if (k >= 2 && ! reg.loopOn) continue; const float d = std::abs (x - xs[k]); if (d < bd || (k < 2 && d <= bd && best >= 2)) { bd = d; best = k; } }
        return best;
    }

    juce::String emptyText { "drop a sample here" };
    static constexpr float kTop = 18, kBottom = 20, kReach = 8, kDragPx = 5;
    static constexpr int kMoveLoop = 4, kRight = 5, kGrip = 6;
    int dragging() const { return drag; }

private:
    void commit (const Region& r) { if (r == reg) return; reg = r; repaint(); if (onChange) onChange (reg); }
    std::vector<float> peaks; double seconds = 0.0; Region reg, reg0;
    int drag = -1; bool moved = false, auditioning = false; juce::Point<float> down0;
};
} // namespace ghosty
