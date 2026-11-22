// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
//
// The plain reference view of the new mode: what the page shows until a design replaces it, and the working example
// of reading REAL engine data through the tap's model (studio/TAP.md is the guide to every call used here).
// It proves the whole path, engine -> tap -> model -> page -> harness, with honest readouts and nothing else.
//
//   on screen                              the field that drives it (ctx.model = mosaic::tap::Model)
//   1 the newest pick: target piece        model.pick (0).targetPiece, target().pieces()
//     played piece, its sound and tint     pick.libraryPiece, library().soundOf / soundName, look::sourceTint
//     algorithm                            pick.style
//     scored, winner's score               pick.why.candidates / unscored, why.winner.total
//     the three-part bar and its numbers   why.winner.toneShare + timbreShare + usedShare (they add up to total)
//     runner-ups                           why.runnerUp[0..2].piece / total
//     stickyness, slide, the target after  why.stickyTested / stickyPlayed / stickyFollowing / stickyLimit,
//                                          why.slideTested / slideHeld / slideError / slideLimit, pick.targetMovedOn / heldByRepeat
//     exact or approximate                 why.state, why.approximate, why.note
//   2 two strips of 100 bars               model.targetPieceOf (pick).bands above library().bands (pick.libraryPiece);
//                                          bars outside pick.settings.lowestBand .. highestBand are dimmed (they did not count)
//     sounding now, one row per piece      model.voice (i).libraryPiece / progress
//   3 the seven controls                   the plug-in's own parameters (ParamRow)
//     in force now                         model.settings(): the engine's own values, after TERRAIN
//   4 output level and its hold mark       model.meters().outputPeak / outputPeakHold
//     the words beside it                  model.transport().playing / whyNot
//     the honesty line                     model.counters().cellsLost / picksSeen / picksExplained / picksWaiting / picksSkipped, usedValid()
// Where there is nothing true to show, that part is greyed and says why in one plain line.
#include "ScopeView.h"

namespace mosaic::scope
{
namespace
{
constexpr int kGap = 14, kPad = 16, kHead = 46, kOutH = 92, kLine = 18, kLabelW = 96;
const char* const kControls[] = { "algo", "novelty", "boredom", "stickyness", "stretch", "synapses", "slide_error" };
const char* const kAlgo[] = { "basic", "reversed", "synaptic", "synaptic slide" };

// scores have no fixed scale (0.001 .. 10,000): enough digits to tell them apart at any size
juce::String num (double v)
{
    const double a = std::abs (v);
    return a >= 100 ? juce::String (juce::roundToInt (v)) : a >= 1 ? juce::String (v, 2) : juce::String (v, 4);
}

class BaselineView : public View
{
public:
    explicit BaselineView (Context& c) : View (c)
    {
        setOpaque (true);                // it paints every pixel of its area itself
        for (auto* id : kControls) m_rows.push_back (addControl (id));
    }

    void pageShown() override
    {
        m_serial = 0; m_state = -1; m_tick = 0;
        repaint();
    }

    // After the model's tick: look at what changed and repaint only that. paint() reads the model itself; what the
    // model gives stays valid until the next tick, so nothing of it is kept here but a pick's serial number.
    void frame (double) override
    {
        const auto& m = ctx.model;
        const tap::Pick& p = m.pick (0);                     // 0 = the newest pick (serial 0: there is none)
        const int state = (int) p.why.state + (p.why.approximate ? 8 : 0) + (m.active() ? 16 : 0) + (m.transport().playing ? 32 : 0);
        if (p.serial != m_serial || state != m_state)
        {
            m_serial = p.serial; m_state = state;
            repaint (m_card[0]);
            repaint (m_compare);
            repaint (m_out);
        }
        const int voices = m.numVoices();                    // the pieces sounding now move while there are any
        if (voices > 0 || voices != m_voices) { m_voices = voices; repaint (m_sounding); }
        const float peak = m.meters().outputPeak, hold = m.meters().outputPeakHold;
        if (std::abs (peak - m_peak) > 0.002f || std::abs (hold - m_hold) > 0.002f) { m_peak = peak; m_hold = hold; repaint (m_meter); }
        if (const auto t = honestyLine(); t != m_honesty) { m_honesty = t; repaint (m_honestyArea); }
        if (m_tick++ % 6 == 0)
            if (const auto t = inForce(); t != m_inForceNow) { m_inForceNow = t; repaint (m_inForce); }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        m_out = r.removeFromBottom (kOutH);
        r.removeFromBottom (kGap);
        // three equal columns on one grid; the output strip below uses the same column edges
        const double step = (r.getWidth() + kGap) / 3.0;
        for (int i = 0; i < 3; ++i)
        {
            const int x0 = r.getX() + juce::roundToInt (step * i), x1 = r.getX() + juce::roundToInt (step * (i + 1)) - kGap;
            m_card[i] = { x0, r.getY(), x1 - x0, r.getHeight() };
        }
        m_pickArea = m_card[0].reduced (kPad, 0).withTrimmedTop (kHead).withTrimmedBottom (12);
        m_lineH = juce::jlimit (kLine, 30, (m_pickArea.getHeight() - 12) / 14);   // (the card's 14 readouts share its height)
        {
            // 2: the two strips of bands, then the pieces sounding now
            auto in = m_card[1].reduced (kPad, 0).withTrimmedTop (kHead).withTrimmedBottom (12);
            const int stripH = juce::jlimit (30, 110, (in.getHeight() - 2 * kLine - 20 - 28 - 5 * 20) / 2);
            m_compare = in.removeFromTop (2 * (kLine + stripH) + 10);
            m_stripA = m_compare.withHeight (kLine + stripH);
            m_stripB = m_stripA.translated (0, kLine + stripH + 10);
            in.removeFromTop (10);
            m_sounding = in;
        }
        {
            // 3: the existing controls, then the engine's own values
            auto in = m_card[2].reduced (kPad, 0).withTrimmedTop (kHead).withTrimmedBottom (12);
            for (auto* row : m_rows) if (row != nullptr) row->setBounds (in.removeFromTop (ParamRow::kHeight));
            in.removeFromTop (6);
            m_inForce = in;
        }
        {
            // 4: the output
            m_outWords = juce::Rectangle<int> (m_card[0].getX(), m_out.getY(), m_card[0].getWidth(), m_out.getHeight()).reduced (kPad, 0);
            auto right = juce::Rectangle<int> (m_card[1].getX(), m_out.getY(), m_card[2].getRight() - m_card[1].getX(), m_out.getHeight()).reduced (kPad, 0);
            m_meter = right.withTrimmedTop (12).withHeight (40);
            m_honestyArea = right.withTrimmedTop (56).withHeight (22);
        }
    }

    void paint (juce::Graphics& g) override
    {
        // Paint cost (studio/README.md, "What things cost"): nearly every pixel is painted once, with flat opaque colours.
        // The ground only shows between the cards and behind their round corners, so it is not filled underneath them;
        // nothing large is see-through; and thin bars sit on whole screen pixels (a bar with soft edges costs several times more).
        {
            juce::Graphics::ScopedSaveState state (g);
            for (auto& c : m_card) g.excludeClipRegion (c.reduced (12));
            g.excludeClipRegion (m_out.reduced (12));
            g.fillAll (look::ground);
        }
        // (a repaint of one part clips to it: the cards it does not touch are skipped)
        if (g.clipRegionIntersects (m_card[0])) paintPick (g);
        if (g.clipRegionIntersects (m_card[1])) { cardWithTitle (g, m_card[1], "2   WHAT WAS COMPARED, WHAT SOUNDS NOW"); paintCompare (g); paintSounding (g); }
        if (g.clipRegionIntersects (m_card[2])) paintSettings (g);
        if (g.clipRegionIntersects (m_out)) paintOutput (g);
    }

private:
    // ---- words that are compared from frame to frame to know when to repaint
    juce::String honestyLine() const
    {
        const auto& c = ctx.model.counters();
        if (! ctx.model.active()) return c.note;
        return "cells lost " + juce::String ((juce::int64) c.cellsLost) + "    picks seen " + juce::String ((juce::int64) c.picksSeen) + ", explained " + juce::String ((juce::int64) c.picksExplained)
               + ", waiting " + juce::String (c.picksWaiting) + ", let go " + juce::String ((juce::int64) c.picksSkipped)
               + "    \"used\" memory: " + (ctx.model.usedValid() ? juce::String ("the engine's own") : "being read back (" + juce::String (c.usedChecked) + " pieces so far)");
    }

    juce::StringArray inForce() const
    {
        const auto& s = ctx.model.settings();
        return { "algorithm", kAlgo[juce::jlimit (0, 3, s.style)], "fft / mfcc", juce::String (s.toneTimbre, 2),
                 "novelty", juce::String (s.novelty, 2), "boredom", juce::String (s.boredom, 2),
                 "stickyness", juce::String (s.smooth, 2), "search stretch", juce::String (s.repeat),
                 "num synpases", juce::String (s.connections), "slide error", juce::String (juce::roundToInt (s.waitUntilClose)) };
    }

    juce::String pieceWords (int piece) const
    {
        const auto& lib = ctx.model.library();
        const int k = lib.soundOf (piece);
        return "piece " + juce::String (piece) + (k >= 0 ? " of " + lib.soundName (k) : juce::String());
    }

    // ---- drawing helpers
    static void cardWithTitle (juce::Graphics& g, juce::Rectangle<int> card, const juce::String& title)
    {
        look::card (g, card.toFloat());
        look::title (g, title, card.reduced (kPad, 0).withHeight (40).translated (0, 4), look::ink, 13.0f);
    }

    // nothing true to show here: a greyed, dashed box that says why in one plain line
    static void greyed (juce::Graphics& g, juce::Rectangle<int> box, const juce::String& head, const juce::String& why)
    {
        if (box.getHeight() < 30 || box.getWidth() < 60) return;
        const float dash[] = { 6, 5 };
        juce::Path p, d;
        p.addRoundedRectangle (box.toFloat().reduced (0.5f), 10);
        juce::PathStrokeType (1.2f).createDashedStroke (d, p, dash, 2);
        g.setColour (look::line);
        g.fillPath (d);
        auto in = box.reduced (12, 6);
        in = in.withSizeKeepingCentre (in.getWidth(), juce::jmin (in.getHeight(), 60));
        g.setColour (look::inkDim);
        g.setFont (look::spaced (11.5f, 1, 0.1f));
        g.drawFittedText (head, in.removeFromTop (juce::jmin (22, in.getHeight() / 2)), juce::Justification::centred, 1, 0.8f);
        g.setColour (look::inkFaint);
        g.setFont (look::font (12.0f, 0));
        g.drawFittedText (why, in, juce::Justification::centredTop, juce::jmax (1, in.getHeight() / 16), 0.85f);
    }

    // one readout: its name, then its value
    juce::Rectangle<int> line (juce::Graphics& g, juce::Rectangle<int>& area, const juce::String& label, const juce::String& value, juce::Colour colour = look::ink, int lines = 1, int indent = 0) const
    {
        auto r = area.removeFromTop (m_lineH * lines);
        g.setColour (look::inkDim);
        g.setFont (look::font (11.5f, 0));
        g.drawText (label, r.removeFromLeft (kLabelW).withHeight (m_lineH), juce::Justification::centredLeft, false);
        g.setColour (colour);
        g.setFont (look::font (12.0f, 1));
        g.drawFittedText (value, r.withTrimmedLeft (indent), lines > 1 ? juce::Justification::topLeft : juce::Justification::centredLeft, lines, 0.8f);
        return r;
    }

    // ---- 1: the newest pick, and why
    void paintPick (juce::Graphics& g)
    {
        cardWithTitle (g, m_card[0], "1   THE NEWEST PICK");
        const auto& m = ctx.model;
        auto in = m_pickArea;
        if (! m.active()) { greyed (g, in, "NOT READING THE ENGINE", m.counters().note); return; }
        const tap::Pick& p = m.pick (0);
        if (p.serial == 0)
        {
            const juce::String why (m.transport().whyNot);
            greyed (g, in, "NO PICK YET", why.isNotEmpty() ? why : juce::String ("The engine has not picked a piece since this page opened."));
            return;
        }
        line (g, in, "target piece", juce::String (p.targetPiece) + (m.target().ok() ? " of " + juce::String (m.target().pieces()) : juce::String (p.liveTarget ? " of the live input" : "")));
        {
            // the library piece that plays, with its sound's own tint
            auto r = line (g, in, "played", p.current ? pieceWords (p.libraryPiece) : "piece " + juce::String (p.libraryPiece) + " of an older library", look::ink, 1, 16);
            g.setColour (p.current ? look::sourceTint (m.library().soundOf (p.libraryPiece)) : look::inkFaint);
            g.fillEllipse ((float) r.getX() + 1, (float) r.getCentreY() - 4.5f, 9, 9);
        }
        line (g, in, "algorithm", kAlgo[juce::jlimit (0, 3, p.style)]);
        line (g, in, "after it", p.targetMovedOn ? "the target moves on" : p.heldByRepeat ? "the target stays: search stretch repeats it" : p.heldBySlide ? "the target stays: waiting for a closer match" : "the target stays");
        const tap::Why& w = p.why;
        if (w.state != tap::Why::ready)
        {
            // waiting, let go, or not available: the model says which, in plain words
            line (g, in, "why", w.note, look::inkDim, 3);
            return;
        }
        line (g, in, "scored", juce::String (w.candidates) + (w.candidates == 1 ? " piece" : " pieces") + (w.unscored > 0 ? ", " + juce::String (w.unscored) + " could not be" : juce::String()));
        line (g, in, "winner's score", num (w.winner.total) + (w.winner.piece >= 0 && w.winner.piece != p.libraryPiece ? "  (piece " + juce::String (w.winner.piece) + ")" : juce::String()));
        {
            // the score in the engine's three parts: tone + timbre + "used" (they add up to the score)
            const double parts[3] = { std::abs (w.winner.toneShare), std::abs (w.winner.timbreShare), std::abs (w.winner.usedShare) };
            const juce::Colour tints[3] = { look::purple, look::green, look::live };
            const char* names[3] = { "tone ", "timbre ", "used " };
            const double shares[3] = { w.winner.toneShare, w.winner.timbreShare, w.winner.usedShare };
            const double sum = parts[0] + parts[1] + parts[2];
            auto bar = in.removeFromTop (12).withTrimmedTop (2).toFloat();
            g.setColour (look::line);
            g.fillRect (bar);
            float x = bar.getX();
            if (sum > 0)
                for (int i = 0; i < 3; ++i)
                {
                    const float wdt = bar.getWidth() * (float) (parts[i] / sum);
                    g.setColour (tints[i]);
                    g.fillRect (x, bar.getY(), wdt, bar.getHeight());
                    x += wdt;
                }
            auto legend = in.removeFromTop (m_lineH);
            const int third = legend.getWidth() / 3;
            g.setFont (look::font (11.5f, 1));
            for (int i = 0; i < 3; ++i)
            {
                g.setColour (tints[i]);
                g.drawFittedText (names[i] + num (shares[i]), legend.removeFromLeft (third), juce::Justification::centredLeft, 1, 0.8f);
            }
        }
        for (int i = 0; i < juce::jmin (3, w.runnerUps) && in.getHeight() >= 4 * m_lineH; ++i)
            line (g, in, i == 0 ? "runner-ups" : "", pieceWords (w.runnerUp[i].piece) + "  " + num (w.runnerUp[i].total), look::inkDim);
        if (w.runnerUps == 0) line (g, in, "runner-ups", "none", look::inkFaint);
        line (g, in, "stickyness", ! w.stickyTested ? juce::String ("not in play")
                                    : w.stickyPlayed ? "played the following piece " + juce::String (w.stickyPiece) + " instead"
                                                     : "kept the winner (" + num (w.stickyFollowing) + " against " + num (w.stickyLimit) + ")",
              w.stickyTested ? look::ink : look::inkFaint);
        line (g, in, "slide", ! w.slideTested ? juce::String ("not in play")
                               : (w.slideHeld ? "the target waited (" : "close enough (") + num (w.slideError) + " against " + num (w.slideLimit) + ")",
              w.slideTested ? look::ink : look::inkFaint);
        // never hidden: whether these are the engine's own numbers
        if (w.approximate) line (g, in, "~ approximate", w.note, look::live, juce::jlimit (1, 3, in.getHeight() / m_lineH));
        else line (g, in, "exact", "the engine's own numbers", look::green);
    }

    // ---- 2: the 100 band energies of the target piece above those of the library piece that was played
    void strip (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& label, tap::Floats row, float top, juce::Colour tint, int lo, int hi, const juce::String& whyNot)
    {
        g.setColour (look::inkDim);
        g.setFont (look::font (11.5f, 0));
        g.drawFittedText (label, area.removeFromTop (kLine), juce::Justification::centredLeft, 1, 0.8f);
        g.setColour (look::line);
        g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 6, 1.0f);
        if (row.empty())
        {
            g.setColour (look::inkFaint);
            g.setFont (look::font (12.0f, 0));
            g.drawFittedText (whyNot, area.reduced (8, 2), juce::Justification::centred, 2, 0.85f);
            return;
        }
        const auto r = area.toFloat().reduced (4, 4);
        const float scale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        auto snap = [scale] (float v) { return std::round (v * scale) / scale; };
        const float bw = r.getWidth() / (float) row.size();
        for (size_t n = 0; n < row.size(); ++n)
        {
            // a log scale, as the guide says (band strengths have no fixed top): six decades under the strongest band shown
            const float u = top > 0 && row[n] > 0 ? juce::jlimit (0.0f, 1.0f, 1.0f + std::log10 (row[n] / top) / 6.0f) : 0.0f;
            const float h = juce::jmax (1.0f / scale, snap (u * r.getHeight()));
            const float x0 = snap (r.getX() + bw * (float) n), x1 = snap (r.getX() + bw * (float) (n + 1));
            g.setColour ((int) n >= lo && (int) n < hi ? tint : look::panel.interpolatedWith (tint, 0.3f));
            g.fillRect (x0, snap (r.getBottom()) - h, juce::jmax (1.0f / scale, x1 - x0 - 1.0f / scale), h);
        }
    }

    void paintCompare (juce::Graphics& g)
    {
        const auto& m = ctx.model;
        const tap::Pick& p = m.pick (0);
        if (! m.active() || p.serial == 0) { greyed (g, m_compare, "NOTHING COMPARED YET", "The bands of a target piece and of the piece picked for it show here once the engine picks."); return; }
        const tap::TargetPiece t = m.targetPieceOf (p);       // (it knows about a replaced target and about live input)
        const tap::Floats lib = p.current ? m.library().bands (p.libraryPiece) : tap::Floats();
        float top = 0;
        for (float v : t.bands) top = juce::jmax (top, v);
        for (float v : lib) top = juce::jmax (top, v);
        const int lo = p.settings.lowestBand, hi = p.settings.highestBand;
        strip (g, m_stripA, "target piece " + juce::String (p.targetPiece) + ": its 100 bands, low to high", t.valid ? t.bands : tap::Floats(), top, look::purple, lo, hi, t.note);
        strip (g, m_stripB, "library " + pieceWords (p.libraryPiece) + ": the same", lib, top, look::sourceTint (m.library().soundOf (p.libraryPiece)), lo, hi,
               "The library was replaced since this pick.");
    }

    void paintSounding (juce::Graphics& g)
    {
        const auto& m = ctx.model;
        auto in = m_sounding;
        const int n = m.numVoices();
        g.setColour (n > 0 ? look::inkDim : look::inkFaint);
        g.setFont (look::spaced (11.0f, 1, 0.14f));
        g.drawText ("SOUNDING NOW", in.removeFromTop (20), juce::Justification::centredLeft, false);
        if (n == 0)
        {
            const juce::String why (m.transport().whyNot);
            g.setColour (look::inkFaint);
            g.setFont (look::font (12.0f, 0));
            g.drawFittedText (why.isNotEmpty() ? "No piece is sounding. " + why : juce::String ("No piece is sounding."), in.removeFromTop (juce::jmin (in.getHeight(), 34)), juce::Justification::topLeft, 2, 0.85f);
            return;
        }
        const int fit = juce::jmax (1, in.getHeight() / 20), shown = n > fit ? fit - 1 : n;
        for (int i = 0; i < shown; ++i)
        {
            const tap::Voice& v = m.voice (i);                 // oldest first
            auto row = in.removeFromTop (20);
            const auto tint = look::sourceTint (m.library().soundOf (v.libraryPiece));
            g.setColour (tint);
            g.fillEllipse ((float) row.getX() + 1, (float) row.getCentreY() - 4.5f, 9, 9);
            g.setColour (look::ink);
            g.setFont (look::font (11.5f, 0));
            g.drawFittedText (pieceWords (v.libraryPiece), row.removeFromLeft (row.getWidth() / 2).withTrimmedLeft (16), juce::Justification::centredLeft, 1, 0.8f);
            g.setColour (look::inkDim);
            g.drawText (juce::String (juce::roundToInt (100.0f * juce::jlimit (0.0f, 1.0f, v.progress))) + "%", row.removeFromRight (38), juce::Justification::centredRight, false);
            // where the engine is reading inside the piece
            const auto bar = row.withSizeKeepingCentre (row.getWidth() - 8, 6).toFloat();
            g.setColour (look::line);
            g.fillRect (bar);
            g.setColour (tint);
            g.fillRect (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, v.progress)));
        }
        if (shown < n)
        {
            g.setColour (look::inkDim);
            g.setFont (look::font (11.5f, 0));
            g.drawText ("and " + juce::String (n - shown) + " more", in.removeFromTop (20).withTrimmedLeft (16), juce::Justification::centredLeft, false);
        }
    }

    // ---- 3: the existing controls (child components), and under them the values the engine itself is using
    void paintSettings (juce::Graphics& g)
    {
        cardWithTitle (g, m_card[2], "3   THE SETTINGS");
        auto in = m_inForce;
        if (in.getHeight() < 3 * 14) return;
        g.setColour (look::inkDim);
        g.setFont (look::spaced (11.0f, 1, 0.14f));
        g.drawFittedText ("IN FORCE NOW, AS THE ENGINE HOLDS THEM", in.removeFromTop (18), juce::Justification::centredLeft, 1, 0.75f);
        if (! ctx.model.active()) { g.setColour (look::inkFaint); g.setFont (look::font (12.0f, 0)); g.drawFittedText (ctx.model.counters().note, in, juce::Justification::topLeft, 2, 0.85f); return; }
        const auto words = inForce();
        const int rowH = juce::jlimit (14, kLine, in.getHeight() / 4), half = in.getWidth() / 2;
        for (int i = 0; i + 1 < words.size(); i += 2)
        {
            auto cell = juce::Rectangle<int> (in.getX() + ((i / 2) % 2) * half, in.getY() + (i / 4) * rowH, half - 6, rowH);
            if (cell.getBottom() > in.getBottom()) break;
            g.setColour (look::inkDim);
            g.setFont (look::font (11.5f, 0));
            g.drawFittedText (words[i], cell.removeFromLeft (cell.getWidth() * 3 / 5), juce::Justification::centredLeft, 1, 0.8f);
            g.setColour (look::ink);
            g.setFont (look::font (11.5f, 1));
            g.drawFittedText (words[i + 1], cell, juce::Justification::centredRight, 1, 0.8f);
        }
    }

    // ---- 4: the output meter, why it is silent when it is, and the honesty counters
    void paintOutput (juce::Graphics& g)
    {
        look::card (g, m_out.toFloat());
        const auto& m = ctx.model;
        const bool playing = m.active() && m.transport().playing;
        auto words = m_outWords;
        look::title (g, "4   THE OUTPUT", words.withHeight (40).translated (0, 4), look::ink, 13.0f);
        g.setColour (playing ? look::ink : look::inkDim);
        g.setFont (look::font (12.0f, 0));
        g.drawFittedText (playing ? juce::String ("Playing") : juce::String (m.active() ? m.transport().whyNot : m.counters().note), words.withTrimmedTop (40).withTrimmedBottom (10), juce::Justification::topLeft, 2, 0.85f);
        auto mt = m_meter;
        auto top = mt.removeFromTop (20);
        g.setColour (playing ? look::inkDim : look::inkFaint);
        g.setFont (look::spaced (11.0f, 1, 0.14f));
        g.drawText ("OUTPUT LEVEL", top, juce::Justification::centredLeft, false);
        g.setFont (look::font (12.0f, 1));
        g.setColour (playing ? look::ink : look::inkFaint);
        g.drawText (playing ? juce::String (m_peak, 2) + " of 1.00" : juce::String ("nothing is playing"), top, juce::Justification::centredRight, false);
        const auto bar = mt.removeFromTop (16).withSizeKeepingCentre (mt.getWidth(), 10).toFloat();
        g.setColour (look::line);
        g.fillRoundedRectangle (bar, 5);
        if (m_peak > 0.0f)
        {
            g.setColour (look::purple.interpolatedWith (look::green, juce::jlimit (0.0f, 1.0f, m_peak)));
            g.fillRoundedRectangle (bar.withWidth (juce::jmax (10.0f, bar.getWidth() * juce::jmin (1.0f, m_peak))), 5);
        }
        if (m_hold > 0.0f)
        {
            g.setColour (look::ink);
            g.fillRect (bar.getX() + (bar.getWidth() - 2) * juce::jmin (1.0f, m_hold), bar.getY() - 2, 2.0f, bar.getHeight() + 4);
        }
        g.setColour (m.active() && m.counters().cellsLost == 0 ? look::inkDim : look::live);
        g.setFont (look::font (11.5f, 0));
        g.drawFittedText (m_honesty, m_honestyArea, juce::Justification::centredLeft, 1, 0.75f);
    }

    std::vector<ParamRow*> m_rows;
    juce::uint64 m_serial = 0;
    int m_state = -1, m_voices = -1, m_tick = 0, m_lineH = kLine;
    float m_peak = 0, m_hold = 0;
    juce::String m_honesty;
    juce::StringArray m_inForceNow;
    juce::Rectangle<int> m_card[3], m_out, m_pickArea, m_compare, m_stripA, m_stripB, m_sounding, m_inForce, m_outWords, m_meter, m_honestyArea;
};
}

std::unique_ptr<View> createBaselineView (Context& c) { return std::make_unique<BaselineView> (c); }
}
