// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
//
// THE RIVER (designer 5): the new mode as one flow from left to right. Everything on the page drifts to the right
// as the engine's own clocks move, and nothing else moves. studio/STUDIO_NOTES.md has the full table; in short:
//
//   on screen                                  the field that drives it (ctx.model = mosaic::tap::Model)
//   1 the target stream and its lens shapes    target().peak / envelope / pieceLength / hop, playhead (0).targetPiece
//   2 100 bars, 12 bars, the tinted line       targetPieceOf (pick).bands / timbre, library().bands / timbre (pick.libraryPiece)
//   3 the tributaries, their three-part bars   pick.why.winner / runnerUp[] (width: total against the winner's total), toneShare / timbreShare / usedShare
//   4 the winner, the three gates              why.played / candidates / stickyTested / stickyPlayed / slideTested / slideHeld, pick.settings
//   5 the tinted lens shapes, the green stream pick.startSample against playhead (0).renderSample, voice (i).fade, meters().outMin / outMax
//   the trail under the river                  pick (i): tint, why.played.total, playedFollowing, targetMovedOn; usedUnit (pick.libraryPiece)
// The pick "in focus" is the newest explained pick (an explanation arrives a moment after its pick): the page never
// blinks while one is being worked out. Where there is nothing true to show, that part is greyed and says why.
#include "RiverView.h"

namespace mosaic::scope
{
namespace
{
constexpr int kGap = 14, kPad = 16, kColGap = 16, kRanks = 7, kOut = 160, kMaxLens = 24, kStairs = 24, kHold = 18;
const char* const kControls[] = { "algo", "novelty", "boredom", "stickyness", "stretch", "synapses", "slide_error", "ratio" };
const int kRowOfRank[9] = { 2, 1, 3, 0, 4, 5, 6, 7, 8 };            // the winner's row carries the river, the others lie around it

// scores have no fixed scale (0.001 .. millions): short enough to read, enough digits to tell them apart
juce::String num (double v)
{
    const double a = std::abs (v);
    if (a >= 1.0e9) return juce::String (v / 1.0e9, 2) + " bn";
    if (a >= 1.0e6) return juce::String (v / 1.0e6, 2) + "M";
    if (a >= 1.0e4) return juce::String (v / 1.0e3, 1) + "k";
    return a >= 100 ? juce::String (juce::roundToInt (v)) : a >= 1 ? juce::String (v, 2) : juce::String (v, 4);
}

// how much further from the target a score is than the best one: "+4.4%" beside a candidate, "4.4% further" in a sentence
juce::String gapOf (double score, double best, bool inWords)
{
    if (! (score > 0) || ! (best > 0)) return {};
    const double r = score / best;
    if (r < 1.0005) return inWords ? "a near tie" : "a tie";
    if (r < 2.0)
    {
        const double p = (r - 1.0) * 100.0;
        const juce::String s = p < 10.0 ? juce::String (p, 1) : juce::String (juce::roundToInt (p));
        return inWords ? s + "% further" : "+" + s + "%";
    }
    const juce::String s = r < 10.0 ? juce::String (r, 1) : juce::String (juce::roundToInt (r));
    return inWords ? s + " times further" : "x " + s;
}

juce::String millis (double samples, double rate)
{
    return rate > 0 ? juce::String (juce::roundToInt (samples * 1000.0 / rate)) + " ms" : juce::String ("?");
}

juce::String percent (double share) { return juce::String (juce::roundToInt (100.0 * share)) + "%"; }

juce::Colour mix (juce::Colour a, juce::Colour b, float t) { return a.interpolatedWith (b, t); }

float midY (juce::Rectangle<int> z) { return (float) z.getY() + (float) z.getHeight() * 0.5f; }


class RiverView : public View
{
public:
    explicit RiverView (Context& c) : View (c)
    {
        setOpaque (true);                // it paints every pixel of its area itself
        for (auto* id : kControls) m_rows.push_back (addControl (id));
    }

    juce::String buttonName() const override { return "THE RIVER"; }
    juce::String buttonTip() const override { return "Watch the sound flow: the target is cut into pieces, each one is measured, your sounds answer, one piece wins, and the winners are glued back together."; }

    void pageShown() override
    {
        m_sig = Sig();
        m_outN = 0; m_outHead = 0; m_lastRender = 0;
        m_focusSerial = 0; m_focusFrames = 0; m_midKey = -1; m_midAt = {}; m_buildStep = 0;
        repaint();
    }

    // After the model's tick. The only thing kept from frame to frame is the output's real shape (one column per
    // frame in which the engine's render clock moved) and a handful of numbers to know whether anything changed.
    void frame (double) override
    {
        const auto& m = ctx.model;
        const tap::Playhead& ph = m.playhead (0);
        const tap::Meters& mt = m.meters();
        const bool playing = m.active() && m.transport().playing;
        if (m.active() && ph.valid)
        {
            if (ph.renderSample < m_lastRender) m_outN = 0;              // the render clock began again: a restart
            if (playing && mt.shapeValid && ph.renderSample != m_lastRender)
            {
                m_outHead = (m_outHead + 1) % kOut;
                m_out[(size_t) m_outHead] = { ph.renderSample, mt.outMin[0], mt.outMax[0] };
                m_outN = juce::jmin (kOut, m_outN + 1);
            }
            m_lastRender = ph.renderSample;
        }
        {
            const tap::Pick* newest = newestFocus();
            const tap::Pick* held = m_focusSerial != 0 ? m.findPick (m_focusSerial) : nullptr;
            ++m_focusFrames;
            if (held == nullptr || ! playing || m_focusFrames >= kHold || (held->why.state != tap::Why::ready && newest != nullptr && newest->why.state == tap::Why::ready))
            {
                m_focusSerial = newest != nullptr ? newest->serial : 0;
                m_focusFrames = 0;
            }
        }
        Sig s;
        const tap::Pick* f = focusPick();
        const tap::Settings& st = m.settings();
        s.newest = m.pick (0).serial;
        s.focus = f != nullptr ? f->serial : 0;
        s.state = f != nullptr ? (int) f->why.state + (f->why.approximate ? 8 : 0) : -1;
        s.target = (juce::int64) std::floor (ph.targetPiece * 1024.0);
        s.render = ph.renderSample;
        s.voices = m.numVoices();
        s.flags = (m.active() ? 1 : 0) + (playing ? 2 : 0) + (m.usedValid() ? 4 : 0) + (m.library().ok() ? 8 : 0) + (m.target().ok() ? 16 : 0);
        s.libGen = m.library().generation();
        s.tgtGen = m.target().generation();
        s.level = juce::roundToInt (mt.outputPeak * 1000.0f);
        s.settings = st.style + 8 * juce::roundToInt (st.novelty * 100.0f) + 1024 * juce::roundToInt (st.smooth * 100.0f) + 131072 * st.repeat;
        s.more = juce::roundToInt (st.boredom * 100.0f) + 128 * st.connections;
        if (! (s == m_sig)) { m_sig = s; repaint(); }
        if (m.active()) stepMid();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        m_s = juce::jlimit (1.0f, 1.6f, juce::jmin ((float) r.getWidth() / 1152.0f, (float) r.getHeight() / 560.0f));
        // the existing controls at the page's own scale (the frame draws a taller row larger), in one line. (A second
        // line of four in a tall window was tried: eight large rows took the 2000 x 1400 repaint to the 20 ms mark.)
        const int rowH = px (ParamRow::kHeight), lines = 1;
        m_ctrl = r.removeFromBottom (lines * rowH + (lines - 1) * 8 + 20);
        r.removeFromBottom (kGap);
        m_river = r;
        auto in = m_river.reduced (kPad, 12);
        // five equal columns on one grid: the headings, the river's five stages, the readouts and the two strips share their edges
        const double step = (in.getWidth() + kColGap) / 5.0;
        for (int i = 0; i < 5; ++i)
        {
            m_colX[i] = in.getX() + juce::roundToInt (step * i);
            m_colW[i] = in.getX() + juce::roundToInt (step * (i + 1)) - kColGap - m_colX[i];
        }
        m_head = in.removeFromTop (px (48));
        in.removeFromTop (6);
        const auto strip = in.removeFromBottom (px (64));
        m_trail = { m_colX[0], strip.getY(), m_colX[1] + m_colW[1] - m_colX[0], strip.getHeight() };     // under stages 1 and 2
        m_bank = { m_colX[2], strip.getY(), m_colX[4] + m_colW[4] - m_colX[2], strip.getHeight() };      // under stages 3 to 5
        in.removeFromBottom (8);
        m_read = in.removeFromBottom (px (18));
        in.removeFromBottom (4);
        m_pic = in;
        m_ranks = m_pic.getHeight() >= 620 ? juce::jmin (9, 1 + tap::kRunnerUps) : kRanks;
        auto c = m_ctrl.reduced (kPad, 10);
        const int fit = juce::jlimit (1, 4, (c.getWidth() + kColGap) / (px (260) + kColGap));
        const double cs = (c.getWidth() + kColGap) / (double) fit;
        int shown = 0;
        for (auto* row : m_rows)
        {
            if (row == nullptr) continue;
            const bool on = shown < fit * lines;
            row->setVisible (on);
            if (! on) continue;
            const int col = shown % fit, line = shown / fit;
            const int x0 = c.getX() + juce::roundToInt (cs * col), x1 = c.getX() + juce::roundToInt (cs * (col + 1)) - kColGap;
            row->setScale (m_s);
            row->setBounds (x0, c.getY() + line * (rowH + 8), x1 - x0, rowH);
            ++shown;
        }
    }

    void paint (juce::Graphics& g) override
    {
        // Paint cost: flat opaque colours mixed beforehand, every pixel painted about once, text shaped once and kept.
        m_slot = 0;
        m_px = 1.0f / juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        const auto bed = m_pic.expanded (8, 0);                          // the river's bed, carved into the card
        const auto headArea = juce::Rectangle<int> (m_river.getX() + kPad, m_head.getY(), m_river.getWidth() - 2 * kPad, m_head.getHeight());
        const auto midArea = midRect();
        const bool midKept = ! m_midImg.isNull() && m_midAt == midArea && ctx.model.active();
        // the kept pictures are opaque and cover their areas: nothing is painted under them (it would be painted over at once)
        g.saveState();
        if (! m_headImg.isNull() && m_headAt == headArea) g.excludeClipRegion (headArea);
        if (midKept) g.excludeClipRegion (midArea);
        {
            // the ground is also the bed's colour: one coat for both, and the card is not painted under the bed
            juce::Graphics::ScopedSaveState state (g);
            g.excludeClipRegion (m_river.reduced (12).withBottom (bed.getY()));
            g.excludeClipRegion (m_river.reduced (12).withTop (bed.getBottom()));
            g.excludeClipRegion (m_ctrl.reduced (12));
            g.fillAll (look::ground);
        }
        {
            juce::Graphics::ScopedSaveState state (g);
            g.excludeClipRegion (bed);
            card (g, m_river.toFloat());
            g.setColour (look::ground);
            g.fillRect (bed);
        }
        card (g, m_ctrl.toFloat());
        g.restoreState();
        cached (g, m_headImg, m_headAt, m_headKey, headArea, 1, look::panel,
                [this] (juce::Graphics& ig) { paintHeads (ig); });

        const auto& m = ctx.model;
        juce::Rectangle<int> z[5];
        for (int i = 0; i < 5; ++i) z[i] = { m_colX[i], m_pic.getY() + 10, m_colW[i], m_pic.getHeight() - 20 };
        if (! m.active())
        {
            greyed (g, m_pic.reduced (8, 10), "NOT READING THE ENGINE", m.counters().note);
            paintTrail (g);
            paintBank (g, nullptr);
            return;
        }
        const tap::Pick* focus = focusPick();
        // one clock for both ends of the river: pixels per sample, so that a piece is the same length left and right
        m_k = (double) m_colW[0] / (2.3 * (double) juce::jmax (1, m.target().pieceLength(), m.library().pieceLength()));
        paintCut (g, z[0], focus);
        paintMid (g, focus);
        paintGlue (g, z[4], focus != nullptr ? focus->serial : 0);
        // the flow goes on between the first three stages
        for (int i = 0; i < (focus != nullptr ? 2 : 1); ++i)
        {
            const float x = (float) (z[i].getRight() + kColGap / 2), y = midY (z[i]);
            juce::Path p;
            p.addTriangle (x - 3.0f, y - 6.0f, x - 3.0f, y + 6.0f, x + 4.0f, y);
            g.setColour (look::inkFaint);
            g.fillPath (p);
        }
        paintReadouts (g, focus);
        paintTrail (g);
        paintBank (g, focus);
    }

private:
    struct Sig
    {
        juce::uint64 newest = 0, focus = 0;
        juce::int64 target = 0;
        juce::uint32 render = 0, libGen = 0, tgtGen = 0;
        int state = -2, voices = -1, flags = -1, level = 0, settings = 0, more = 0;
        bool operator== (const Sig&) const = default;
    };
    struct Out { juce::uint32 sample = 0; float lo = 0, hi = 0; };
    struct Slot { juce::String text; juce::Rectangle<int> r; int key = 0; juce::GlyphArrangement ga;
                  juce::Image img; juce::Point<float> at; juce::uint32 argb = 0; int pxOf = 0; };

    int px (int v) const { return juce::roundToInt ((float) v * m_s); }
    float fs (float v) const { return v * m_s; }
    float snap (float v) const { return std::round (v / m_px) * m_px; }

    // The pick the page explains: the newest one whose explanation is here (an explanation arrives a moment after
    // its pick), or the newest pick itself while none is. Channel 0: with STEREO off it is the only one that searches.
    // ...held for kHold frames (0.3 s), so that its numbers can be read: the engine picks about 32 times
    // a second. Every pick held is a real one; stages 1 and 5 and the two strips still draw every pick at once.
    const tap::Pick* focusPick() const
    {
        if (m_focusSerial != 0)
            if (const tap::Pick* held = ctx.model.findPick (m_focusSerial)) return held;
        return newestFocus();
    }

    const tap::Pick* newestFocus() const
    {
        const auto& m = ctx.model;
        const tap::Pick* first = nullptr;
        const int n = juce::jmin (12, m.numPicks());
        for (int i = 0; i < n; ++i)
        {
            const tap::Pick& p = m.pick (i);
            if (p.channel != 0) continue;
            if (first == nullptr) first = &p;
            if (p.why.state == tap::Why::ready) return &p;
        }
        return first;
    }

    static void card (juce::Graphics& g, juce::Rectangle<float> r0)
    {
        const auto r = r0.reduced (0.5f);
        const float cut = juce::jlimit (0.0f, juce::jmin (r.getWidth(), r.getHeight()) * 0.5f, 14.0f);
        g.setColour (look::panel);
        const float split = std::round (r.getY() + cut);           // one whole-pixel join: no seam between the two fills
        g.fillRect (r.withTop (split));
        g.fillRect (r.withBottom (split).withTrimmedRight (cut));
        juce::Path corner;
        corner.addTriangle (r.getRight() - cut, r.getY(), r.getRight(), r.getY() + cut, r.getRight() - cut, r.getY() + cut);
        g.fillPath (corner);
        g.setColour (look::line);
        g.strokePath (look::cutRect (r, 14.0f), juce::PathStrokeType (1.0f));
    }

    juce::Rectangle<int> midRect() const
    {
        const int l = m_colX[0] + m_colW[0] + 3, r = m_colX[4];
        return { l, m_pic.getY(), juce::jmax (0, r - l), m_pic.getHeight() };
    }

    // Stages 2 to 4 change only when the pick in focus does. They are kept as one picture at the screen's own pixel size
    // and copied in between. A NEW picture is drawn over three frames, one stage per frame, into a second image, while
    // the last finished picture stays on screen; then the two swap. So no frame pays for all three stages at once (on
    // Brandon's Mac that one dear frame was a dropped frame), and the picture on screen is always one whole, real pick:
    // it is at most two frames (33 ms) behind the newest pick held for explaining. Only a new window size (no picture
    // of the right size yet) is drawn in one go.
    void midStage (juce::Image& img, int which, const tap::Pick* p, juce::Rectangle<int> area)
    {
        juce::Rectangle<int> z[5];
        for (int i = 0; i < 5; ++i) z[i] = { m_colX[i], m_pic.getY() + 10, m_colW[i], m_pic.getHeight() - 20 };
        juce::Graphics ig (img);
        ig.addTransform (juce::AffineTransform::translation ((float) -area.getX(), (float) -area.getY()).scaled (1.0f / m_px));
        const size_t keep = m_slot;
        m_slot = (size_t) m_midSlot[which];
        if (which == 0) paintMeasure (ig, z[1], p);
        if (which == 1) paintAnswers (ig, z[2], p);
        if (which == 2) paintWhy (ig, z[3], p);
        m_midSlot[which + 1] = (int) m_slot;
        m_slot = keep;
    }

    void midBlank (juce::Image& img, int w, int h)
    {
        if (img.isNull() || img.getWidth() != w || img.getHeight() != h) img = juce::Image (juce::Image::RGB, w, h, false);
        juce::Graphics (img).fillAll (look::ground);
    }

    // On the 60 Hz tick (not in paint, so the picture never falls behind when the host paints less often): one stage of
    // a new picture of stages 2 to 4 per frame, into a second image, while the last finished one stays on screen.
    void stepMid()
    {
        const auto area = midRect();
        if (area.isEmpty() || m_midImg.isNull() || m_midAt != area) return;          // paint makes the first one, at its size
        const tap::Pick* focus = focusPick();
        const int key = midKey (focus);
        if (m_buildStep == 0 && key != m_midKey)
        {
            midBlank (m_midNext, m_midImg.getWidth(), m_midImg.getHeight());
            m_buildKey = key; m_buildSerial = focus != nullptr ? focus->serial : 0; m_buildStep = 1;
        }
        if (m_buildStep == 0) return;
        const tap::Pick* p = m_buildSerial != 0 ? ctx.model.findPick (m_buildSerial) : nullptr;
        if (m_buildSerial != 0 && p == nullptr)
        {
            // the pick left the ring before its picture was done (a restart, a new library): begin again from the focus
            midBlank (m_midNext, m_midImg.getWidth(), m_midImg.getHeight());
            m_buildKey = key; m_buildSerial = focus != nullptr ? focus->serial : 0; m_buildStep = 1;
            p = focus;
        }
        m_k = (double) m_colW[0] / (2.3 * (double) juce::jmax (1, ctx.model.target().pieceLength(), ctx.model.library().pieceLength()));
        m_midSlot[0] = 64;
        midStage (m_midNext, m_buildStep - 1, p, area);
        if (++m_buildStep > 3)
        {
            std::swap (m_midImg, m_midNext);
            m_midKey = m_buildKey; m_buildStep = 0;
        }
        repaint();
    }

    // Stages 2 to 4 are kept as one picture at the screen's own pixel size and copied each frame. A new one is drawn over
    // three ticks (stepMid), one stage per tick, so no frame pays for all three at once (on Brandon's Mac that one dear
    // frame was a dropped frame); the picture on screen is always one whole, real pick, at most two frames (33 ms) behind
    // the pick held for explaining. Only a new window size (no picture of the right size yet) is drawn here in one go.
    void paintMid (juce::Graphics& g, const tap::Pick* focus)
    {
        const auto area = midRect();
        if (area.isEmpty()) return;
        const float scale = 1.0f / m_px;
        const int w = juce::jmax (1, juce::roundToInt ((float) area.getWidth() * scale)), h = juce::jmax (1, juce::roundToInt ((float) area.getHeight() * scale));
        if (m_midImg.isNull() || m_midImg.getWidth() != w || m_midImg.getHeight() != h || m_midAt != area)
        {
            midBlank (m_midImg, w, h);
            m_midSlot[0] = 64;
            for (int i = 0; i < 3; ++i) midStage (m_midImg, i, focus, area);
            m_midAt = area; m_midKey = midKey (focus); m_buildStep = 0;
        }
        g.drawImageTransformed (m_midImg, juce::AffineTransform::scale (m_px).translated ((float) area.getX(), (float) area.getY()));
    }

    // What stages 2 to 4 are drawn from, as one number: when it changes, their kept picture is drawn again.
    int midKey (const tap::Pick* focus) const
    {
        const auto& m = ctx.model;
        juce::uint64 h = 1469598103934665603ULL;
        auto add = [&h] (juce::uint64 v) { h = (h ^ v) * 1099511628211ULL; };
        add ((juce::uint64) m.library().generation());
        add ((juce::uint64) m.target().generation());
        add ((juce::uint64) ((m.library().ok() ? 1 : 0) + (m.target().ok() ? 2 : 0) + (m.transport().playing ? 4 : 0) + (m.target().live() ? 8 : 0)));
        if (focus != nullptr)
        {
            add ((juce::uint64) focus->serial);
            add ((juce::uint64) ((int) focus->why.state + (focus->why.approximate ? 8 : 0) + (focus->current ? 16 : 0) + 32 * (int) focus->playedFollowing));
        }
        else
        {
            add ((juce::uint64) (juce::int64) m.playhead (0).targetPiece);
            add ((juce::uint64) (m.settings().lowestBand + 128 * m.settings().highestBand));
            add ((juce::uint64) juce::String (m.transport().whyNot).hashCode64());
            add ((juce::uint64) m_sig.settings);
            add ((juce::uint64) m_sig.more);
            for (int k = 0; k < juce::jmin (12, m.library().sounds()); ++k)
                add ((juce::uint64) ((m.library().soundOn (k) ? 1 : 0) + (m.library().soundRemoved (k) ? 2 : 0)));
        }
        return (int) (juce::uint32) (h ^ (h >> 32));
    }

    juce::String whyNotText() const
    {
        const juce::String why (ctx.model.transport().whyNot);
        return why.isNotEmpty() ? why : juce::String ("The engine has not picked a piece since this page opened.");
    }

    // ---- text: shaped once per place and kept until its words, place or size change (shaping is the costly part)
    void txt (juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r, juce::Colour c, float size, int weight = 0,
              juce::Justification j = juce::Justification::centredLeft, int lines = 1, float tracking = 0.0f)
    {
        if (s.isEmpty() || r.getWidth() < 12 || r.getHeight() < 8) return;
        if (m_slot >= m_slots.size()) m_slots.resize (m_slot + 1);
        Slot& sl = m_slots[m_slot++];
        const int key = juce::roundToInt (size * 8.0f) + 4096 * weight + 16384 * lines + 131072 * j.getFlags() + (tracking > 0.0f ? (1 << 28) : 0);
        const bool reshape = sl.key != key || sl.r != r || sl.text != s;
        if (reshape)
        {
            sl.key = key; sl.r = r; sl.text = s;
            sl.ga.clear();
            sl.ga.addFittedText (tracking > 0.0f ? look::spaced (size, weight, tracking) : look::font (size, weight), s,
                                 (float) r.getX(), (float) r.getY(), (float) r.getWidth(), (float) r.getHeight(), j, lines, lines > 1 ? 0.95f : 0.8f);
        }
        // the shaped words are also kept as a small picture at the screen's own pixel size, drawn again only when the
        // words, their place, size or colour change: copying it costs far less than drawing every glyph every frame
        const int pxKey = juce::roundToInt (64.0f / m_px);
        if (reshape || sl.img.isNull() || sl.argb != c.getARGB() || sl.pxOf != pxKey)
        {
            sl.argb = c.getARGB(); sl.pxOf = pxKey;
            const auto bb = sl.ga.getBoundingBox (0, -1, true).expanded (1.0f);
            const float x0 = std::floor (bb.getX() / m_px) * m_px, y0 = std::floor (bb.getY() / m_px) * m_px;
            const int w = juce::jmax (1, (int) std::ceil ((bb.getRight() - x0) / m_px)), h = juce::jmax (1, (int) std::ceil ((bb.getBottom() - y0) / m_px));
            if (sl.img.isNull() || sl.img.getWidth() != w || sl.img.getHeight() != h) sl.img = juce::Image (juce::Image::ARGB, w, h, true);
            else sl.img.clear (sl.img.getBounds());
            sl.at = { x0, y0 };
            juce::Graphics ig (sl.img);
            ig.addTransform (juce::AffineTransform::translation (-x0, -y0).scaled (1.0f / m_px));
            ig.setColour (c);
            sl.ga.draw (ig);
        }
        g.drawImageTransformed (sl.img, juce::AffineTransform::scale (m_px).translated (sl.at.x, sl.at.y));
    }

    // words that do not change (the headings, the trail's legend): drawn once into a picture at the screen's own pixel
    // size and copied from then on (studio/README.md: such a copy is the cheapest way to show a still layer)
    static void words (juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r, juce::Colour c, float size, int weight, juce::Justification j, int lines, float tracking = 0.0f)
    {
        g.setColour (c);
        g.setFont (tracking > 0.0f ? look::spaced (size, weight, tracking) : look::font (size, weight));
        g.drawFittedText (s, r, j, lines, 1.0f);       // never squashed: a caption too long for one line takes two
    }

    template <typename Draw>
    void cached (juce::Graphics& g, juce::Image& img, juce::Rectangle<int>& at, int& held, juce::Rectangle<int> area, int key, juce::Colour fill, Draw&& draw)
    {
        if (area.isEmpty()) return;
        const float scale = 1.0f / m_px;
        const int w = juce::jmax (1, juce::roundToInt ((float) area.getWidth() * scale)), h = juce::jmax (1, juce::roundToInt ((float) area.getHeight() * scale));
        const bool fresh = img.isNull() || img.getWidth() != w || img.getHeight() != h;
        if (fresh || at != area || held != key)
        {
            if (fresh) img = juce::Image (juce::Image::RGB, w, h, false);
            at = area; held = key;
            juce::Graphics ig (img);
            ig.fillAll (fill);
            ig.addTransform (juce::AffineTransform::translation ((float) -area.getX(), (float) -area.getY()).scaled (scale));
            draw (ig);
        }
        g.drawImageTransformed (img, juce::AffineTransform::scale (m_px).translated ((float) area.getX(), (float) area.getY()));
    }

    // nothing true to show here: a greyed, dashed box that says why in one plain line
    void greyed (juce::Graphics& g, juce::Rectangle<int> box, const juce::String& head, const juce::String& why)
    {
        if (box.getHeight() < 30 || box.getWidth() < 60) return;
        const float dash[] = { 6, 5 };
        juce::Path p, d;
        p.addRectangle (box.toFloat().reduced (0.5f));
        juce::PathStrokeType (1.2f).createDashedStroke (d, p, dash, 2);
        g.setColour (look::line);
        g.fillPath (d);
        auto in = box.reduced (10, 6);
        in = in.withSizeKeepingCentre (in.getWidth(), juce::jmin (in.getHeight(), px (84)));
        txt (g, head, in.removeFromTop (px (22)), look::inkDim, fs (12.0f), 1, juce::Justification::centred, 1, 0.1f);
        txt (g, why, in, look::inkDim, fs (11.5f), 0, juce::Justification::centredTop, 4);
    }

    void paintHeads (juce::Graphics& g)
    {
        static const char* const titles[5] = { "1   CUT INTO PIECES", "2   EACH PIECE MEASURED", "3   THE LIBRARY ANSWERS", "4   ONE WINS, AND WHY", "5   GLUED BACK TOGETHER" };
        static const char* const lines[5] = { "The target, and its overlapping pieces.",
                                              "The bands with energy, and 12 timbre numbers.",
                                              "The closest pieces flow in. Widest = closest, thinnest = furthest behind.",
                                              "Its score, and three gates that can change it.",
                                              "The pieces that play, and their sum below." };
        for (int i = 0; i < 5; ++i)
        {
            juce::Rectangle<int> r (m_colX[i], m_head.getY(), m_colW[i], m_head.getHeight());
            words (g, titles[i], r.removeFromTop (px (18)), look::ink, fs (12.5f), 1, juce::Justification::centredLeft, 1, 0.1f);
            words (g, lines[i], r, look::inkDim, fs (12.5f), 0, juce::Justification::topLeft, 2);
        }
    }

    // One piece as a lens: its real outline (32 slices, lowest and highest sample), its start on the right. `amp` is
    // the height its largest slice gets. A target piece is drawn by the size of its swing (the same above and below
    // the line); a library piece keeps both sides of its outline, since that faded audio is exactly what plays.
    void lens (juce::Path& p, float xs, float wpx, float cy, float amp, int piece, bool fromTarget) const
    {
        const auto& m = ctx.model;
        float lo[32], hi[32], most = 0;
        for (int s = 0; s < 32; ++s)
        {
            if (fromTarget) m.target().envelope (0, piece, s, lo[s], hi[s]); else m.library().envelope (piece, s, lo[s], hi[s]);
            if (fromTarget) { hi[s] = juce::jmax (hi[s], -lo[s]); lo[s] = -hi[s]; }
            most = juce::jmax (most, hi[s], -lo[s]);
        }
        const float k = most > 0 ? amp / most : 0.0f;
        p.clear();
        p.startNewSubPath (xs, cy);
        const int step = fromTarget ? 2 : 1;                 // (a target lens is a smooth swing: 16 points a side are plenty)
        const float half = 0.5f * (float) step;
        for (int s = 0; s < 32; s += step) p.lineTo (xs - wpx * ((float) s + half) / 32.0f, cy - juce::jmax (1.0f, k * juce::jmax (hi[s], hi[s + step - 1])));
        p.lineTo (xs - wpx, cy);
        for (int s = 32 - step; s >= 0; s -= step) p.lineTo (xs - wpx * ((float) s + half) / 32.0f, cy - juce::jmin (-1.0f, k * juce::jmin (lo[s], lo[s + step - 1])));
        p.closeSubPath();
    }

    static float loud (float peak, float loudest) { return loudest > 0 ? std::pow (juce::jlimit (0.0f, 1.0f, peak / loudest), 0.4f) : 0.0f; }

    // the loudest piece of the target and of the library (looked up once per target / library): it fills its row
    float targetTop()
    {
        const auto& t = ctx.model.target();
        if (! m_tgtTopKnown || m_tgtTopGen != t.generation())
        {
            m_tgtTop = 0;
            for (int j = 0; j < t.pieces(); ++j) m_tgtTop = juce::jmax (m_tgtTop, t.peak (0, j));
            m_tgtTopGen = t.generation(); m_tgtTopKnown = true;
        }
        return m_tgtTop;
    }
    // the largest swing of any slice of the target (looked up once per target): the stream's full height
    float targetEdgeTop()
    {
        const auto& t = ctx.model.target();
        if (! m_edgeTopKnown || m_edgeTopGen != t.generation())
        {
            m_edgeTop = 0;
            const int n = t.pieces();
            for (int j = 0; j < n; j += juce::jmax (1, n / 400))
                for (int sl = 8; sl < 24; ++sl)
                {
                    float lo = 0, hi = 0;
                    t.envelope (0, j, sl, lo, hi);
                    m_edgeTop = juce::jmax (m_edgeTop, hi, -lo);
                }
            m_edgeTopGen = t.generation(); m_edgeTopKnown = true;
        }
        return m_edgeTop;
    }
    float libraryTop()
    {
        const auto& l = ctx.model.library();
        if (! m_libTopKnown || m_libTopGen != l.generation())
        {
            m_libTop = 0;
            for (int j = 0; j < l.pieces(); ++j) m_libTop = juce::jmax (m_libTop, l.peak (j));
            m_libTopGen = l.generation(); m_libTopKnown = true;
        }
        return m_libTop;
    }

    // ---- 1: the target flows in from the left and is cut into overlapping pieces
    void paintCut (juce::Graphics& g, juce::Rectangle<int> z, const tap::Pick* focus)
    {
        const auto& m = ctx.model;
        const auto& t = m.target();
        if (! t.ok())
        {
            greyed (g, z, t.live() ? "THE LIVE INPUT" : "NO TARGET", t.live() ? juce::String ("The target is the live input: there is nothing to look ahead at.") : juce::String (t.note()));
            return;
        }
        const int len = juce::jmax (1, t.pieceLength()), hop = juce::jmax (1, t.hop());
        const double at = m.playhead (0).valid ? m.playhead (0).targetPiece * (double) hop : 0.0;   // the playhead, in samples
        const float xCut = (float) z.getRight(), wpx = (float) ((double) len * m_k);
        const int lanes = juce::jlimit (1, 6, (len + hop - 1) / hop);
        const int jLo = juce::jmax (0, (int) std::floor ((at - (double) len) / (double) hop));
        const int jHi = juce::jmin (t.pieces() - 1, jLo + kMaxLens, (int) std::ceil ((at + (double) z.getWidth() / m_k) / (double) hop));
        const float loudest = targetTop();
        const int focusPiece = focus != nullptr && focus->current && ! focus->liveTarget ? focus->targetPiece : -1;
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (z);
        auto stream = z.removeFromTop (z.getHeight() * 26 / 100);
        z.removeFromTop (8);
        if (jHi >= jLo)
        {
            // The target as one stream: its real outline. Each piece gives the slices around its middle (where its
            // fade is fully open), one hop's worth, so the pieces' middles laid end to end are the target itself.
            const float cy = midY (stream), hh = (float) stream.getHeight() * 0.5f;
            const int ns = juce::jlimit (1, 32, juce::roundToInt (32.0 * (double) hop / (double) len)), s0 = juce::jlimit (0, 32 - ns, 16 - ns / 2);
            const double slice = (double) len / 32.0;
            const float edgeTop = targetEdgeTop();
            m_edgeX.clear(); m_edgeA.clear(); m_edgeB.clear();
            for (int j = jLo; j <= jHi; ++j)
            {
                const int sA = j == 0 ? 0 : s0, sB = j == t.pieces() - 1 ? 32 : s0 + ns;      // (the target's two ends: the whole first and last piece)
                for (int sl = sA; sl < sB; ++sl)
                {
                    float lo = 0, hi = 0;
                    t.envelope (0, j, sl, lo, hi);
                    m_edgeX.push_back (xCut - (float) (((double) j * (double) hop + ((double) sl + 0.5) * slice - at) * m_k));
                    m_edgeA.push_back (juce::jmax (1.0f, hh * juce::jlimit (0.0f, 1.0f, edgeTop > 0 ? hi / edgeTop : 0.0f)));
                    m_edgeB.push_back (juce::jmax (1.0f, hh * juce::jlimit (0.0f, 1.0f, edgeTop > 0 ? -lo / edgeTop : 0.0f)));
                }
            }
            const size_t np = m_edgeX.size();
            if (np > 1)
            {
                juce::Path s;
                s.startNewSubPath (m_edgeX[0], cy - m_edgeA[0]);
                for (size_t i = 1; i < np; ++i) s.lineTo (m_edgeX[i], cy - m_edgeA[i]);
                for (size_t i = np; i-- > 0;) s.lineTo (m_edgeX[i], cy + m_edgeB[i]);
                s.closeSubPath();
                g.setColour (mix (look::ground, look::ink, 0.55f));
                g.fillPath (s);
            }
            // the cuts: a new piece starts at every one of these lines
            g.setColour (look::ground);
            for (int j = jLo; j <= jHi; ++j)
                g.fillRect (snap (xCut - (float) (((double) j * (double) hop - at) * m_k)) - m_px, (float) stream.getY(), 2.0f * m_px, (float) stream.getHeight());
        }
        // the pieces, one row per overlap: a new piece starts every hop, so each row holds every "lanes"-th piece
        const float laneH = (float) z.getHeight() / (float) lanes;
        juce::Path p;
        for (int j = jLo; j <= jHi; ++j)
        {
            const float xs = xCut - (float) (((double) j * (double) hop - at) * m_k);
            const float cy = (float) z.getY() + laneH * ((float) (j % lanes) + 0.5f);
            const float amp = (laneH * 0.5f - 2.0f) * loud (t.peak (0, j), loudest);
            g.setColour (j == focusPiece ? look::live : look::inkDim);           // where each piece starts: one tick per hop
            g.fillRect (snap (xs) - m_px, (float) z.getBottom() - 6.0f, 2.0f * m_px, 6.0f);
            if (j == focusPiece) continue;                                       // (drawn whole below)
            g.setColour (mix (look::ground, look::ink, j < focusPiece ? 0.22f : 0.4f));
            if (amp < 1.6f) g.fillRect (xs - wpx, cy - 1.0f, wpx, 2.0f);         // (a nearly silent piece: its outline is a line)
            else { lens (p, xs, wpx, cy, amp, j, true); g.fillPath (p); }
        }
        if (focusPiece >= 0 && focusPiece < t.pieces())
        {
            // The piece being matched now: whole, held at the cut, in the look's "live" colour. (Its own place in the
            // stream has moved on past the cut: this is the same piece, kept in sight while its numbers are read.)
            const float cy = (float) z.getY() + laneH * ((float) (focusPiece % lanes) + 0.5f);
            const float amp = juce::jmax (1.6f, (laneH * 0.5f - 2.0f) * loud (t.peak (0, focusPiece), loudest));
            g.setColour (look::ground);
            g.fillRect (xCut - wpx - 6.0f, cy - laneH * 0.5f, wpx + 6.0f, laneH);
            lens (p, xCut - 3.0f, wpx, cy, amp, focusPiece, true);
            g.setColour (look::live);
            g.fillPath (p);
        }
        g.setColour (look::inkDim);                                              // the cut: pieces are matched as they reach it
        g.fillRect (xCut - 2.0f, (float) z.getY(), 2.0f, (float) z.getHeight());
    }

    // ---- 2: the piece in focus as the engine measured it, and the piece that plays for it laid over it band by band.
    // Only the bands that hold energy are drawn, large; the white bars are the gaps between the two: the mismatch.
    void paintMeasure (juce::Graphics& g, juce::Rectangle<int> z, const tap::Pick* focus)
    {
        const auto& m = ctx.model;
        tap::Floats tb, tt, lb, lt;
        juce::Colour tint = look::inkDim;
        int lo = m.settings().lowestBand, hi = m.settings().highestBand, tgtPiece = -1, libPiece = -1;
        juce::String note (m.target().note());
        bool waiting = false;
        m_bandsShown = 0; m_bandsOf = 0;
        if (focus != nullptr)
        {
            const tap::TargetPiece t = m.targetPieceOf (*focus);
            if (t.valid) { tb = t.bands; tt = t.timbre; } else note = t.note;
            lo = focus->settings.lowestBand; hi = focus->settings.highestBand;
            tgtPiece = focus->targetPiece;
            if (focus->current && m.library().ok())
            {
                libPiece = focus->libraryPiece;
                lb = m.library().bands (libPiece); lt = m.library().timbre (libPiece);
                tint = look::sourceTint (m.library().soundOf (libPiece));
            }
        }
        else if (m.target().ok())
        {
            // nothing picked yet: the piece waiting at the cut, as it was measured when the target was loaded
            tgtPiece = juce::jlimit (0, m.target().pieces() - 1, (int) m.playhead (0).targetPiece);
            tb = m.target().bands (0, tgtPiece); tt = m.target().timbre (0, tgtPiece);
            waiting = true;
            if (tb.empty()) note = "This target is too large to keep its measurements.";
        }
        if (tb.empty() || tt.empty()) { greyed (g, z, "NOTHING MEASURED", note); return; }

        const int n = (int) tb.size();
        const bool over = (int) lb.size() == n;
        float top = 0;
        for (float v : tb) top = juce::jmax (top, v);
        for (float v : lb) top = juce::jmax (top, v);
        // a log scale (band strengths span about six decades): six decades under the strongest band shown
        auto unit = [top] (float v) { return top > 0 && v > 0 ? juce::jlimit (0.0f, 1.0f, 1.0f + std::log10 (v / top) / 6.0f) : 0.0f; };
        int shown = juce::jmin (n, 6);                                           // the bands that hold energy in either piece
        for (int i = 0; i < n; ++i)
            if (unit (tb[(size_t) i]) > 0.08f || (over && unit (lb[(size_t) i]) > 0.08f)) shown = juce::jmax (shown, i + 1);
        if (m.target().ok()) shown = juce::jmin (n, steadyBandRows (n));         // the rows stay put from pick to pick
        m_bandsShown = shown; m_bandsOf = n;
        // their shares of the score of the piece that plays (-1: not known yet): a measurement that does not count is dimmed
        float toneFrac = -1.0f, timbreFrac = -1.0f;
        if (focus != nullptr && focus->why.state == tap::Why::ready)
        {
            const tap::Score& sp = focus->why.played.piece >= 0 ? focus->why.played : focus->why.winner;
            const double sum = std::abs (sp.toneShare) + std::abs (sp.timbreShare) + std::abs (sp.usedShare);
            if (sum > 0) { toneFrac = (float) (std::abs (sp.toneShare) / sum); timbreFrac = (float) (std::abs (sp.timbreShare) / sum); }
        }
        const bool toneOut = toneFrac >= 0.0f && toneFrac < 0.005f, timbreOut = timbreFrac >= 0.0f && timbreFrac < 0.005f;

        auto heads = z.removeFromTop (px (16));
        z.removeFromTop (3);
        auto legend = z.removeFromBottom (px (15));
        z.removeFromBottom (3);
        auto ends = z.removeFromBottom (px (15));                                // what the lower end of each chart is
        z.removeFromBottom (2);
        auto a = z.removeFromLeft (z.getWidth() * 60 / 100);
        z.removeFromLeft (10);
        auto b = z;
        txt (g, "high notes", heads.withWidth (a.getWidth()), look::ink, fs (12.0f), 1);
        txt (g, toneOut ? "low notes: not counted now" : "low notes", ends.withWidth (a.getWidth()), toneOut ? look::inkDim : look::ink, fs (12.0f), 1);
        txt (g, timbreFrac >= 0.0f ? "timbre " + percent (timbreFrac) : juce::String ("timbre"), heads.withLeft (b.getX()), timbreOut ? look::inkDim : look::green, fs (12.0f), 1);
        txt (g, timbreOut ? "not counted" : "less  0  more", ends.withLeft (b.getX()), look::inkDim, fs (11.5f), 0, juce::Justification::centred);
        const juce::Colour lit = mix (tint, look::ink, 0.32f);
        const float rh = (float) a.getHeight() / (float) shown, bottom = (float) a.getBottom(), x0 = (float) a.getX(), w = (float) a.getWidth();
        // (the piece being matched is the live colour here as at the cut in stage 1)
        const juce::Colour in = mix (look::ground, look::live, toneOut ? 0.24f : waiting ? 0.34f : 0.5f), out = mix (look::ground, look::live, 0.2f), track = mix (look::ground, look::live, 0.07f);
        for (int i = 0; i < shown; ++i)                                          // low bands at the bottom
        {
            const float y1 = snap (bottom - rh * (float) i), y0 = snap (bottom - rh * (float) (i + 1));
            const float bh = juce::jmax (m_px, y1 - y0 - juce::jmax (2.0f * m_px, snap (rh * 0.16f)));
            const float bl = juce::jmax (2.0f, snap (unit (tb[(size_t) i]) * w));
            g.setColour (i >= lo && i < hi ? in : out);
            g.fillRect (x0, y0, bl, bh);
            g.setColour (track);
            g.fillRect (x0 + bl, y0, w - bl, bh);
            if (! over) continue;
            // the piece that plays, laid over the target's bar; the white bar is the gap between the two
            const float ll = juce::jmax (2.0f, snap (unit (lb[(size_t) i]) * w)), sh = juce::jmax (2.0f, snap (bh * 0.42f)), sy = snap (y0 + (bh - sh) * 0.5f);
            g.setColour (look::ground);
            g.fillRect (x0, sy - 1.0f, ll + 1.0f, sh + 2.0f);
            g.setColour (lit);
            g.fillRect (x0, sy, ll, sh);
            if (std::abs (ll - bl) >= 2.0f)
            {
                const float gt = juce::jmax (2.0f, snap (sh * 0.34f));
                g.setColour (look::ink);
                g.fillRect (x0 + juce::jmin (ll, bl), snap (y0 + (bh - gt) * 0.5f), std::abs (ll - bl), gt);
            }
        }
        // the 12 timbre numbers, any sign, from a centre line (square root of their size, so the small ones show)
        float most = 0;
        for (float v : tt) most = juce::jmax (most, std::abs (v));
        for (float v : lt) most = juce::jmax (most, std::abs (v));
        auto bend = [most] (float v) { const float u = most > 0 ? std::sqrt (std::abs (v) / most) : 0.0f; return v < 0 ? -u : u; };
        const int nt = (int) tt.size();
        const float th = (float) b.getHeight() / (float) nt, cx = snap ((float) b.getCentreX()), hw = (float) b.getWidth() * 0.5f - 2.0f;
        const float greenT = waiting ? 0.4f : timbreFrac < 0.0f ? 0.8f : 0.26f + 0.54f * std::sqrt (juce::jlimit (0.0f, 1.0f, timbreFrac));
        for (int i = 0; i < nt; ++i)
        {
            const float y0 = snap ((float) b.getY() + th * (float) i) + 2.0f, h = juce::jmax (2.0f, snap (th) - 4.0f);
            const float s = bend (tt[(size_t) i]) * hw;
            g.setColour (mix (look::ground, look::green, greenT));
            g.fillRect (s >= 0 ? cx : cx + s, y0, juce::jmax (m_px, std::abs (s)), h);
            if (i < (int) lt.size())
            {
                const float q = snap (cx + bend (lt[(size_t) i]) * hw), e = cx + s;
                if (std::abs (q - e) >= 3.0f)
                {
                    g.setColour (look::ink);
                    g.fillRect (juce::jmin (q, e), snap (y0 + h * 0.5f) - 1.0f, std::abs (q - e), 2.0f);
                }
                g.setColour (look::ground);
                g.fillRect (q - 4.0f, y0 - 1.0f, 8.0f, h + 2.0f);
                g.setColour (lit);
                g.fillRect (q - 2.5f, y0 - 1.0f, 5.0f, h + 2.0f);
            }
        }
        g.setColour (look::inkDim);                                              // the centre line: zero
        g.fillRect (cx - 1.0f, (float) b.getY(), 2.0f, (float) b.getHeight());
        // which is which
        const int cw = legend.getWidth() / 3;
        const juce::String label[3] = { "target " + juce::String (tgtPiece), "piece " + juce::String (libPiece), "the gap" };
        for (int i = 0; i < (over ? 3 : 1); ++i)
        {
            const juce::Rectangle<int> cell (legend.getX() + cw * i, legend.getY(), cw - 4, legend.getHeight());
            const float cy = snap (midY (cell)), x = (float) cell.getX();
            if (i == 0)
            {
                g.setColour (mix (look::ground, look::live, 0.5f)); g.fillRect (x, cy - 4.0f, 5.0f, 8.0f);
                g.setColour (look::green); g.fillRect (x + 5.0f, cy - 4.0f, 5.0f, 8.0f);
            }
            else
            {
                g.setColour (i == 1 ? lit : look::ink);
                g.fillRect (x, cy - (i == 1 ? 2.0f : 1.0f), 10.0f, i == 1 ? 4.0f : 2.0f);
            }
            txt (g, label[i], cell.withTrimmedLeft (14), i == 0 ? look::inkDim : i == 1 ? lit : look::ink, fs (12.0f), 0);
        }
    }

    // The output on ONE fixed, straight scale: the lane's edge is a quarter of full scale (every piece is mixed in at
    // a fifth, so that is about where a busy mix reaches). Quiet output is thin, and the readout says so in words.
    static constexpr float kOutEdge = 0.25f;
    static float level (float v) { return juce::jlimit (-1.0f, 1.0f, v / kOutEdge); }

    // a tributary's width: the row's own height sets the widest (so the river grows with the window)
    float ribbon (float closeness, float rowH) const { return 2.0f + juce::jlimit (0.0f, 1.0f, closeness) * juce::jlimit (4.0f, juce::jmax (24.0f * m_s, rowH * 0.62f), rowH - 8.0f); }

    // How close a candidate is, among the candidates shown: 1 = the winner, 0 = as far behind the winner as the
    // furthest one shown. (The scores of the closest few are often within a few percent of each other, so their
    // closeness on the whole score scale looks the same for all of them: the gap to the winner is what tells them
    // apart.) A field of near ties stays a field of near ties: the range is never taken smaller than 2%.
    static float closenessAmongShown (const tap::Why& w, const tap::Score& sc, bool winner, bool reversed, int shown)
    {
        if (winner) return 1.0f;
        if (! sc.finite || ! w.winner.finite || ! (w.winner.total > 0)) return 0.0f;
        auto gap = [&] (const tap::Score& c) { return c.finite ? juce::jmax (0.0, reversed ? 1.0 - c.total / w.winner.total : c.total / w.winner.total - 1.0) : 0.0; };
        double far = 0.02;
        for (int i = 0; i < juce::jmin (shown - 1, w.runnerUps); ++i) far = juce::jmax (far, gap (w.runnerUp[i]));
        return (float) juce::jlimit (0.0, 1.0, 1.0 - gap (sc) / far);
    }

    // nothing searched yet: the sounds the tributaries will come from (their names, tints and sizes are real and still)
    void paintSounds (juce::Graphics& g, juce::Rectangle<int> z)
    {
        const auto& lib = ctx.model.library();
        const int n = juce::jmin (12, lib.sounds());
        const float rowH = (float) z.getHeight() / (float) juce::jmax (kRanks, n);
        const int lw = z.getWidth() * 40 / 100, cw = px (26);
        int first[12];                                                           // where each sound's pieces begin
        for (int k = 0; k < 12; ++k) first[k] = -1;
        for (int p = 0; p < lib.pieces(); ++p) { const int k = lib.soundOf (p); if (k >= 0 && k < 12 && first[k] < 0) first[k] = p; }
        const float loudest = libraryTop();
        const float x0 = (float) (z.getX() + lw), wAll = (float) (z.getWidth() - lw - cw - 4);
        const int cap = juce::jlimit (4, 24, (int) (wAll / 9.0f));
        const float step = wAll / (float) cap;
        juce::Path path;
        for (int k = 0; k < n; ++k)
        {
            const float y = (float) z.getY() + rowH * ((float) k + 0.5f);
            const bool on = lib.soundOn (k) && ! lib.soundRemoved (k);
            const juce::Colour tint = look::sourceTint (k);
            const juce::Rectangle<int> row (z.getX(), juce::roundToInt (y - rowH * 0.5f), z.getWidth(), juce::roundToInt (rowH));
            const int count = lib.soundPieces (k);
            txt (g, lib.soundName (k), row.withWidth (lw - 6), on ? tint : mix (look::inkFaint, look::inkDim, 0.6f), fs (12.0f), 1);
            txt (g, juce::String (count), row.withLeft (row.getRight() - cw), look::inkDim, fs (11.5f), 0, juce::Justification::centredRight);
            // the sound's own pieces, first to last from left to right, each as its real outline (a long sound: every few)
            const int show = juce::jmin (count, cap);
            if (first[k] < 0 || show < 1) continue;
            g.setColour (on ? mix (look::ground, tint, 0.8f) : look::line);
            for (int i = 0; i < show; ++i)
            {
                const int piece = first[k] + (int) ((juce::int64) i * count / show);
                const float amp = juce::jmax (1.0f, (rowH * 0.5f - 3.0f) * loud (lib.peak (piece), loudest));
                lens (path, x0 + step * (float) i, -(step - 1.0f), y, amp, piece, false);
                g.fillPath (path);
            }
        }
    }

    // ---- 3: the candidates the search found closest, as tributaries: the winner is the widest and the furthest one
    // shown the thinnest (each by its gap to the winner's score); only the winner reaches the river.
    // Each carries its score and how much further from the target it is than the winner.
    void paintAnswers (juce::Graphics& g, juce::Rectangle<int> z, const tap::Pick* focus)
    {
        const auto& m = ctx.model;
        const auto& lib = m.library();
        if (! lib.ok()) { greyed (g, z, "NO LIBRARY YET", lib.note()); return; }
        if (focus == nullptr) { paintSounds (g, z); return; }
        if (! focus->current) { greyed (g, z, "AN OLDER LIBRARY", "The library was replaced since this pick."); return; }
        const tap::Why& w = focus->why;
        const bool ready = w.state == tap::Why::ready, rev = focus->style == 1;
        const float rowH = (float) z.getHeight() / (float) m_ranks, yWin = (float) z.getY() + rowH * ((float) kRowOfRank[0] + 0.5f);
        const int lw = z.getWidth() / 2;
        const float x0 = (float) (z.getX() + lw), xEnd = (float) z.getRight();
        const juce::Colour parts[3] = { look::purple, look::green, look::live };
        const int count = ready ? 1 + juce::jmin (m_ranks - 1, w.runnerUps) : 1;
        for (int r = count - 1; r >= 0; --r)                                     // the winner last: it lies on top
        {
            tap::Score sc;
            if (ready) sc = r == 0 ? w.winner : w.runnerUp[r - 1]; else sc.piece = focus->libraryPiece;
            if (sc.piece < 0) continue;                                          // (nothing could be scored)
            const float y = (float) z.getY() + rowH * ((float) kRowOfRank[r] + 0.5f);
            const juce::Colour tint = look::sourceTint (lib.soundOf (sc.piece));
            const bool win = r == 0;
            // wider = closer: the winner is the widest, the furthest candidate shown the thinnest
            const float close = ready ? 0.14f + 0.86f * closenessAmongShown (w, sc, win, rev, m_ranks) : 0.5f;
            const float t = ribbon (close, rowH);
            const int barH = juce::jmax (3, px (3));
            const juce::Rectangle<int> cell (z.getX(), juce::roundToInt (y - rowH * 0.5f) + 1, lw - 8, juce::roundToInt (rowH) - 2);
            const bool two = ready && cell.getHeight() >= 27 + barH;             // name, numbers and the bar as one block, centred on the row
            const int labH = (two ? px (15) + px (13) : px (16)) + 3 + barH;
            const int waveH = ready && cell.getHeight() - labH >= px (20) ? juce::jmin (px (34), cell.getHeight() - labH - 6) : 0;   // a tall row: its own outline too
            auto lab = cell.withSizeKeepingCentre (cell.getWidth(), juce::jmin (cell.getHeight(), labH + (waveH > 0 ? waveH + 4 : 0)));
            if (waveH > 0)
            {
                const auto wave = lab.removeFromBottom (waveH);
                lab.removeFromBottom (4);
                juce::Path wp;
                lens (wp, (float) wave.getX(), -juce::jmin ((float) wave.getWidth(), (float) waveH * 3.4f), midY (wave), (float) waveH * 0.5f - 1.0f, sc.piece, false);
                g.setColour (win ? tint : mix (look::ground, tint, 0.7f));
                g.fillPath (wp);
            }
            auto text = lab.withTrimmedBottom (barH + 3);
            const auto nameAt = two ? text.removeFromTop (text.getHeight() * 54 / 100) : text;
            txt (g, (ready ? juce::String (r + 1) + "  " : juce::String()) + lib.soundName (lib.soundOf (sc.piece)) + " " + juce::String (sc.piece), nameAt,
                 win ? tint : mix (look::ground, tint, 0.8f), fs (12.0f), win ? 2 : 0);
            if (two)                                                             // its score, and how much further than the winner
                txt (g, ! sc.finite ? juce::String ("could not be scored")
                        : num (sc.total) + "   " + (win ? juce::String (w.stickyPlayed ? "found, not played" : rev ? "the furthest" : "the closest") : rev ? juce::String() : gapOf (sc.total, w.winner.total, false)),
                     text, win ? look::ink : look::inkDim, fs (11.5f), 0);
            if (ready)
            {
                // what this score is made of: bands + timbre + "used lately" (they add up to the score)
                const double share[3] = { std::abs (sc.toneShare), std::abs (sc.timbreShare), std::abs (sc.usedShare) };
                const double sum = share[0] + share[1] + share[2];
                float bx = (float) lab.getX();
                const float by = snap ((float) lab.getBottom() - (float) barH - 1.0f);
                for (int i = 0; i < 3 && sum > 0; ++i)
                {
                    const float bw = (float) lab.getWidth() * close * (float) (share[i] / sum);     // as long as it is close
                    g.setColour (win ? parts[i] : mix (look::ground, parts[i], 0.6f));
                    g.fillRect (bx, by, bw, (float) barH);
                    bx += bw;
                }
            }
            const float x1 = win ? xEnd : x0 + (xEnd - x0) * 0.92f, y1 = win ? y : y + (yWin - y) * 0.7f, te = win ? t : 0.0f, xm = (x0 + x1) * 0.5f;
            juce::Path p;
            p.startNewSubPath (x0, y);
            p.lineTo (x0 + 10.0f, y - t * 0.5f);
            p.cubicTo (xm, y - t * 0.5f, xm, y1 - te * 0.5f, x1, y1 - te * 0.5f);
            p.lineTo (x1, y1 + te * 0.5f);
            p.cubicTo (xm, y1 + te * 0.5f, xm, y + t * 0.5f, x0 + 10.0f, y + t * 0.5f);
            p.closeSubPath();
            g.setColour (win ? tint : mix (look::ground, tint, 0.55f));
            g.fillPath (p);
        }
        if (! ready)
            txt (g, w.note, z.withTrimmedTop (z.getHeight() * 2 / 3).reduced (4, 0), look::inkDim, fs (12.0f), 0, juce::Justification::centred, 3);
    }

    // the "used" memory, as it is now, of the pieces of the last picks (newest on the left): a pick fills a piece's
    // memory to 1000 and every later pick multiplies it by boredom, so the steps show how fast the engine forgets
    void stairs (juce::Graphics& g, juce::Rectangle<int> gz, bool counted)
    {
        const auto& m = ctx.model;
        if (gz.getWidth() < 24 || gz.getHeight() < 4) return;
        const float bw = (float) gz.getWidth() / (float) kStairs, base = (float) gz.getBottom(), full = (float) gz.getHeight();
        g.setColour (counted ? look::live : look::inkFaint);
        int c = 0;
        for (int k = 0; k < m.numPicks() && c < kStairs; ++k)
        {
            const tap::Pick& p = m.pick (k);
            if (p.channel != 0) continue;
            const float u = p.current ? juce::jlimit (0.0f, 1.0f, m.usedUnit (p.libraryPiece)) : 0.0f;
            const float x0 = snap ((float) gz.getX() + bw * (float) c), x1 = snap ((float) gz.getX() + bw * (float) (c + 1)) - juce::jmax (m_px, 1.0f);
            const float h = juce::jmax (m_px, snap (u * full));
            g.fillRect (x0, base - h, juce::jmax (m_px, x1 - x0), h);
            ++c;
        }
    }

    // a gate's test as a lane: scores on a log track, closer on the left. The white line is the limit and the lighter
    // water left of it is where a score passes. Square mark: the search's winner. Round mark: the piece being tested.
    void lane (juce::Graphics& g, juce::Rectangle<int> gz, bool on, double limit, double a, juce::Colour ca, double b, juce::Colour cb, bool bPlays)
    {
        if (gz.getWidth() < 24 || gz.getHeight() < 4) return;
        const float cy = snap (midY (gz)), x0 = (float) gz.getX(), wd = (float) gz.getWidth(), h = (float) gz.getHeight();
        g.setColour (look::line);
        g.drawRect (gz, 1);
        g.fillRect (x0, cy - m_px, wd, 2.0f * m_px);
        const double far = juce::jmax (a, b);
        if (! on || ! (far > 0)) return;
        if (! (limit < 1.0e30)) limit = far * 8.0;                               // (no limit at all: every score passes)
        if (! (limit > 0)) return;
        double lo = limit, hi = limit;
        if (a > 0) { lo = juce::jmin (lo, a); hi = juce::jmax (hi, a); }
        if (b > 0) { lo = juce::jmin (lo, b); hi = juce::jmax (hi, b); }
        lo *= 0.5; hi *= 2.0;
        auto xOf = [&] (double v) { return x0 + wd * tap::ScoreScale::logUnit (v, lo, hi); };
        const float xl = snap (xOf (limit)), r = juce::jmin (h * 0.38f, fs (5.0f));
        g.setColour (mix (look::ground, look::ink, 0.17f));
        g.fillRect (x0, (float) gz.getY(), xl - x0, h);
        g.setColour (look::ink);
        g.fillRect (xl - 1.0f, (float) gz.getY(), 2.0f, h);
        if (a > 0)
        {
            g.setColour (ca);
            g.fillRect (xOf (a) - r * 0.7f, cy - r, r * 1.4f, 2.0f * r);
        }
        if (b > 0)
        {
            const float xb = xOf (b);
            if (bPlays)
            {
                g.setColour (look::ink);
                g.fillEllipse (xb - r - 1.5f, cy - r - 1.5f, 2.0f * r + 3.0f, 2.0f * r + 3.0f);
            }
            g.setColour (cb);
            g.fillEllipse (xb - r, cy - r, 2.0f * r, 2.0f * r);
        }
    }

    // ---- 4: the winner, what its score is made of, and the three gates that can change what plays: each gate with
    // its own small picture and one plain sentence in the engine's own numbers
    void paintWhy (juce::Graphics& g, juce::Rectangle<int> z, const tap::Pick* focus)
    {
        const auto& m = ctx.model;
        const auto& lib = m.library();
        if (focus == nullptr) { paintGatesAsSet (g, z); return; }
        if (! focus->current || ! lib.ok()) { greyed (g, z, "AN OLDER LIBRARY", "The library was replaced since this pick."); return; }
        const tap::Why& w = focus->why;
        const tap::Settings& st = focus->settings;
        const bool ready = w.state == tap::Why::ready, rev = focus->style == 1;
        const float rowH = (float) z.getHeight() / (float) m_ranks, yMid = (float) z.getY() + rowH * ((float) kRowOfRank[0] + 0.5f);
        const int played = focus->libraryPiece, winner = ready && w.winner.piece >= 0 ? w.winner.piece : played;
        const juce::Colour tintPlayed = look::sourceTint (lib.soundOf (played)), tintWin = look::sourceTint (lib.soundOf (winner));
        const float t = ribbon (ready && w.winner.piece >= 0 ? 1.0f : 0.5f, rowH);     // the winner's tributary, at its full width
        const float third = (float) z.getWidth() / 3.0f;
        const float gx[3] = { (float) z.getX() + third * 0.5f, (float) z.getX() + third * 1.5f, (float) z.getX() + third * 2.5f };
        // the stream: the search's winner as far as the stickyness gate, the piece that really plays after it
        const float xa = (float) (z.getX() - kColGap), xb = (float) (z.getRight() + kColGap);
        g.setColour (tintWin);
        g.fillRect (xa, yMid - t * 0.5f, gx[1] - xa, t);
        g.setColour (tintPlayed);
        g.fillRect (gx[1], yMid - t * 0.5f, xb - gx[1], t);

        const bool sticky = ready && w.stickyTested, slide = ready && w.slideTested, novel = st.novelty > 0.0f;
        const juce::Colour gate[3] = { novel ? look::live : look::line,
                                       sticky ? (w.stickyPlayed ? look::ink : look::inkDim) : look::line,
                                       slide ? (w.slideHeld ? look::ink : look::inkDim) : look::line };
        const float gh = ribbon (1.0f, rowH) + 16.0f;                            // as tall as the widest river: the words never move
        for (int i = 0; i < 3; ++i)
        {
            g.setColour (look::ground);
            g.fillRect (gx[i] - 5.0f, yMid - gh * 0.5f, 10.0f, gh);
            g.setColour (gate[i]);
            g.fillRect (gx[i] - 2.5f, yMid - gh * 0.5f, 5.0f, gh);
        }
        const int gap = juce::roundToInt (gh * 0.5f) + 5;
        auto up = z.withBottom (juce::roundToInt (yMid) - gap), down = z.withTop (juce::roundToInt (yMid) + gap);
        const tap::Score& scp = w.played.piece >= 0 ? w.played : w.winner;

        // above the stream: who plays, why, and what its score is made of
        // (a tall window has room for the candidates' own lane under the sentences)
        const bool race = ready && ! rev && ! w.stickyPlayed && w.runnerUps > 0 && w.winner.total > 0 && up.getHeight() >= 7 * px (19);
        const int lines = race ? 7 : up.getHeight() >= 5 * 14 ? 5 : 4;
        const int lh = juce::jmax (12, up.getHeight() / lines);
        {
            auto r = up.removeFromTop (lh);
            g.setColour (tintPlayed);
            g.fillRect ((float) r.getX() + 1.0f, (float) r.getCentreY() - 4.5f, 9.0f, 9.0f);
            txt (g, "piece " + juce::String (played) + "  " + lib.soundName (lib.soundOf (played)), r.withTrimmedLeft (16), look::ink, fs (12.5f), 2);
        }
        juce::String verdict (w.note), second;
        if (ready)
        {
            if (w.stickyPlayed)
            {
                verdict = "stickyness plays the next piece, at " + num (scp.total);
                if (w.winner.piece >= 0) second = "the search found piece " + juce::String (w.winner.piece) + " closer: " + num (w.winner.total);
            }
            else
            {
                verdict = juce::String (rev ? "the furthest of " : "the closest of ") + juce::String (w.candidates) + (focus->style >= 2 ? " neighbours, at " : " pieces, at ") + num (scp.total);
                second = w.runnerUps < 1 || w.runnerUp[0].piece < 0 ? juce::String ("no other piece could be scored")
                       : rev ? "next: piece " + juce::String (w.runnerUp[0].piece) + ", at " + num (w.runnerUp[0].total)
                             : "next best: piece " + juce::String (w.runnerUp[0].piece) + ", " + gapOf (w.runnerUp[0].total, w.winner.total, true);
            }
        }
        txt (g, verdict, up.removeFromTop (lh), ready ? look::inkDim : look::inkDim, fs (12.0f), 0);
        if (ready)
        {
            const double share[3] = { std::abs (scp.toneShare), std::abs (scp.timbreShare), std::abs (scp.usedShare) };
            const double sum = share[0] + share[1] + share[2];
            const juce::Colour parts[3] = { look::purple, look::green, look::live };
            const char* const pnames[3] = { "bands ", "timbre ", "used " };
            auto bar = up.removeFromTop (lh).withSizeKeepingCentre (z.getWidth(), juce::jmax (6, px (8))).toFloat();
            juce::Rectangle<int> legend;
            if (lines >= 5) legend = up.removeFromTop (lh);
            g.setColour (look::line);
            g.fillRect (bar);
            float bx = bar.getX();
            for (int i = 0; i < 3; ++i)
            {
                const float bw = sum > 0 ? bar.getWidth() * (float) (share[i] / sum) : 0.0f;
                g.setColour (parts[i]);
                g.fillRect (bx, bar.getY(), bw, bar.getHeight());
                bx += bw;
                if (lines >= 5) txt (g, pnames[i] + percent (sum > 0 ? share[i] / sum : 0.0), legend.removeFromLeft (z.getWidth() / 3), parts[i], fs (12.0f), 1);
            }
            txt (g, second, up.removeFromTop (lh), look::inkDim, fs (12.0f), 0);
            if (race)
            {
                // the candidates of stage 3 on one lane by their scores (a log track from the best to the worst shown):
                // closer on the left, the winner ringed in white, so a near tie and a clear win look different
                const int nr = juce::jmin (m_ranks - 1, w.runnerUps);
                txt (g, "the " + juce::String (nr + 1) + " closest by score: closer on the left", up.removeFromTop (lh), look::inkDim, fs (12.0f), 0);
                const auto gz = up.removeFromTop (lh).reduced (0, 3);
                double far = w.winner.total;
                for (int i = 0; i < nr; ++i) if (w.runnerUp[i].finite) far = juce::jmax (far, w.runnerUp[i].total);
                const double lo = w.winner.total, hi = juce::jmax (far, lo * 1.0001);
                const float cy = snap (midY (gz)), r = juce::jmin ((float) gz.getHeight() * 0.36f, fs (5.0f));
                g.setColour (look::line);
                g.drawRect (gz, 1);
                g.fillRect ((float) gz.getX(), cy - m_px, (float) gz.getWidth(), 2.0f * m_px);
                float lastX = -1.0e6f;
                for (int i = 0; i <= nr; ++i)
                {
                    const tap::Score& c = i == 0 ? w.winner : w.runnerUp[i - 1];
                    if (c.piece < 0 || ! c.finite || ! (c.total > 0)) continue;
                    float x = (float) gz.getX() + r + 3.0f + ((float) gz.getWidth() - 2.0f * r - 6.0f) * tap::ScoreScale::logUnit (c.total, lo, hi);
                    x = juce::jmin ((float) gz.getRight() - r - 2.0f, juce::jmax (x, lastX + 2.0f * r + 1.0f));      // (ties stand side by side, best first)
                    lastX = x;
                    if (i == 0)
                    {
                        g.setColour (look::ink);
                        g.fillRect (x - r - 1.5f, cy - r - 1.5f, 2.0f * r + 3.0f, 2.0f * r + 3.0f);
                    }
                    g.setColour (look::sourceTint (lib.soundOf (c.piece)));
                    g.fillRect (x - r, cy - r, 2.0f * r, 2.0f * r);
                }
            }
        }

        // under the stream: the three gates in the river's order, each with its own picture and one plain sentence
        static const char* const names[3] = { "novelty", "stickyness", "slide error" };
        const bool usedCounts = novel && ready && scp.total > 0 && scp.usedShare / scp.total >= 0.005;
        juce::String says[3];
        says[0] = ! novel ? juce::String ("Off at 0: the used memory is kept, but does not count.")
                : (! ready ? juce::String ("Working it out. ") : usedCounts ? "+" + percent (scp.usedShare / scp.total) + " for being used. " : juce::String ("Fresh before this pick: +0%. "))
                  + "The mark fades " + juce::String (juce::roundToInt (100.0f * (1.0f - st.boredom))) + "% each pick (boredom " + juce::String (st.boredom, 2) + ").";
        says[1] = st.smooth <= 0.0f ? juce::String ("Off at 0: every piece is a fresh search.")
                : rev ? juce::String ("Off: the reversed style does not use it.")
                : ! ready ? juce::String ("Working it out.")
                : ! w.stickyTested ? juce::String ("The last piece has no next piece to play.")
                : "Next in line (dot) " + num (w.stickyFollowing) + (w.stickyPlayed ? " is under the limit (line) " : " is over the limit (line) ")
                  + num (w.stickyLimit) + (w.stickyPlayed ? ": it plays." : ": the found piece plays.") + " Found (square): " + num (w.stickyWinner) + ".";
        says[2] = focus->style != 3 ? juce::String ("Off: only synaptic slide makes the target wait.")
                : ! ready ? juce::String ("Working it out.")
                : ! w.slideTested ? juce::String ("This search was not tested against the limit.")
                : (w.slideErrorKnown ? "Match " + num (w.slideError) : juce::String ("Nothing could be scored")) + (w.slideHeld ? ": over the limit " : ": under the limit ")
                  + num (w.slideLimit) + (w.slideHeld ? ", the target waits." : ", the target moves on.");
        const bool acted[3] = { usedCounts, sticky && w.stickyPlayed, slide && w.slideHeld };
        if (down.getHeight() >= 3 * px (44) + px (13))
        {
            const auto nameRow = down.removeFromTop (px (15));
            if (ready && w.stickyPlayed)
            {
                // stickyness overruled the search: the piece that plays comes in here, at the stickyness gate, under its own name
                const float wy = yMid + t * 0.5f, wh = juce::jmax (6.0f, (float) nameRow.getY() - wy), ww = juce::jmax (12.0f, t * 0.8f), wx = gx[1] + 4.0f + ww * 0.5f;
                juce::Path in;
                in.addTriangle (wx - ww * 0.5f, wy + wh, wx + ww * 0.5f, wy + wh, wx, wy - 1.0f);
                g.setColour (tintPlayed);
                g.fillPath (in);
                const juce::String gapWords = gapOf (w.stickyFollowing, w.winner.total, false);
                txt (g, "next in line: " + juce::String (lib.soundName (lib.soundOf (played))) + " " + juce::String (played) + (gapWords.isNotEmpty() ? ", " + gapWords : juce::String()),
                     nameRow.withWidth (z.getWidth()), tintPlayed, fs (12.0f), 1, juce::Justification::centred);
            }
            else
                for (int i = 0; i < 3; ++i)
                    txt (g, names[i], { juce::roundToInt (gx[i] - third * 0.5f) + 1, nameRow.getY(), juce::roundToInt (third) - 2, nameRow.getHeight() },
                         look::inkDim, fs (12.0f), 0, juce::Justification::centred);
        }
        const int rh3 = down.getHeight() / 3;
        const float sf = rh3 < px (44) ? 11.0f : fs (12.0f);
        const int sentH = juce::roundToInt (sf * 2.8f), sentH3 = juce::roundToInt (sf * 4.1f), nameW = z.getWidth() * 46 / 100;
        // A gate that is off is ONE greyed line (its name and, in a few words, why): the gates in use take its room, and
        // with every gate off the room goes to the last picks.
        const bool inUse[3] = { novel, st.smooth > 0.0f && ! rev, focus->style == 3 };
        const int nUse = (inUse[0] ? 1 : 0) + (inUse[1] ? 1 : 0) + (inUse[2] ? 1 : 0);
        const int offH = px (19);
        const juce::String offWhy[3] = { "off: memory not counted", rev ? "off: unused by reversed" : "off at 0: fresh search", "off: synaptic slide only" };
        int rowY = down.getY();
        for (int i = 0; i < 3; ++i)
        {
            const juce::String gateName = i == 0 ? "novelty " + juce::String (st.novelty, 2) : i == 1 ? "stickyness " + juce::String (st.smooth, 2) : juce::String ("slide error");
            if (! inUse[i])
            {
                const juce::Rectangle<int> line (z.getX(), rowY, z.getWidth(), offH);
                rowY += offH;
                g.setColour (look::inkFaint);
                g.fillRect (line.getX(), line.getY() + 3, 3, line.getHeight() - 6);
                txt (g, gateName, line.withTrimmedLeft (8).withWidth (nameW - 8), look::inkDim, fs (12.0f), 1);
                txt (g, offWhy[i], line.withTrimmedLeft (nameW + 4), look::inkDim, fs (11.5f), 0);
                continue;
            }
            const int rowH3 = (down.getBottom() - down.getY() - (3 - nUse) * offH) / nUse;
            juce::Rectangle<int> row (z.getX(), rowY, z.getWidth(), rowH3);
            rowY += rowH3;
            const int sh = i == 1 && row.getHeight() >= px (16) + 24 + sentH3 ? sentH3 : sentH;      // (the stickyness test names its three marks: three lines)
            const bool on = gate[i] != look::line;
            const auto head = row.removeFromTop (juce::jmax (13, row.getHeight() - sh));
            const int nameH = juce::jmin (head.getHeight(), px (16));
            g.setColour (gate[i]);                                               // the gate's own mark, as on the stream
            g.fillRect (head.getX(), head.getY() + 2, 3, nameH - 4);
            txt (g, gateName, { head.getX() + 8, head.getY(), nameW - 8, nameH }, on ? look::ink : look::inkDim, fs (12.0f), 1);
            const bool tall = head.getHeight() >= px (16) + 22;                  // room for the picture under the name, at full width
            const auto gz = tall ? head.withTrimmedTop (nameH + 2).withTrimmedBottom (6) : head.withTrimmedLeft (nameW + 4).reduced (0, 2);
            if (i == 0) stairs (g, gz, novel);
            else if (i == 1) lane (g, gz, sticky, w.stickyLimit, w.stickyWinner, tintWin, w.stickyFollowing,
                                   sticky && w.stickyPiece >= 0 ? look::sourceTint (lib.soundOf (w.stickyPiece)) : look::inkDim, w.stickyPlayed);
            else lane (g, gz, slide && w.slideErrorKnown, w.slideLimit, w.slideError, tintWin, 0.0, look::inkDim, false);
            txt (g, says[i], row, acted[i] ? look::ink : look::inkDim, sf, 0, juce::Justification::topLeft, sh == sentH3 ? 3 : 2);
        }
        const juce::Rectangle<int> rest (z.getX(), rowY + px (6), z.getWidth(), down.getBottom() - rowY - px (6));
        if (rest.getHeight() >= px (56)) paintLastPicks (g, rest);
    }

    // room under the gates: the last picks, newest first: which piece played, at what score, and what changed it
    void paintLastPicks (juce::Graphics& g, juce::Rectangle<int> r)
    {
        const auto& m = ctx.model;
        const auto& lib = m.library();
        txt (g, "THE LAST PICKS, NEWEST FIRST", r.removeFromTop (px (19)), look::inkDim, fs (11.5f), 1, juce::Justification::centredLeft, 1, 0.1f);
        const int lh = px (17), rows = r.getHeight() / lh;
        int c = 0;
        for (int i = 0; i < m.numPicks() && c < rows; ++i)
        {
            const tap::Pick& p = m.pick (i);
            if (p.channel != 0 || ! p.current || ! lib.ok()) continue;
            const juce::Rectangle<int> line (r.getX(), r.getY() + lh * c, r.getWidth(), lh);
            ++c;
            g.setColour (look::sourceTint (lib.soundOf (p.libraryPiece)));
            g.fillRect ((float) line.getX() + 1.0f, (float) line.getCentreY() - 4.0f, 8.0f, 8.0f);
            const tap::Score& sc = p.why.played.piece >= 0 ? p.why.played : p.why.winner;
            juce::String words = juce::String (lib.soundName (lib.soundOf (p.libraryPiece))) + " " + juce::String (p.libraryPiece);
            if (p.why.state == tap::Why::ready && sc.finite) words += "   " + num (sc.total);
            if (p.playedFollowing == tap::Pick::yes) words += "   next in line";
            else if (! p.targetMovedOn) words += "   target waited";
            txt (g, words, line.withTrimmedLeft (14), look::inkDim, fs (11.5f), 0);
        }
    }

    // nothing picked yet (stage 4): the three gates as they are SET now, each with its reason. Settings are real and
    // still: nothing here moves, and there is no empty box.
    void paintGatesAsSet (juce::Graphics& g, juce::Rectangle<int> z)
    {
        const tap::Settings& st = ctx.model.settings();
        txt (g, "NO WINNER YET", z.removeFromTop (px (20)), look::ink, fs (11.5f), 1, juce::Justification::centredLeft, 1, 0.1f);
        z.removeFromTop (px (6));
        txt (g, "THE THREE GATES, AS SET", z.removeFromTop (px (20)), look::inkDim, fs (12.0f), 1, juce::Justification::centredLeft, 1, 0.1f);
        const bool on[3] = { st.novelty > 0.0f, st.smooth > 0.0f && st.style != 1, st.style == 3 };
        const juce::String name[3] = { "novelty " + juce::String (st.novelty, 2), "stickyness " + juce::String (st.smooth, 2), juce::String ("slide error") };
        const juce::String says[3] = {
            on[0] ? "On: a used piece scores worse. The mark fades " + juce::String (juce::roundToInt (100.0f * (1.0f - st.boredom))) + "% each pick (boredom " + juce::String (st.boredom, 2) + ")."
                  : juce::String ("Off at 0: the used memory is kept, but does not count."),
            st.smooth <= 0.0f ? juce::String ("Off at 0: every piece is a fresh search.")
                  : st.style == 1 ? juce::String ("Off: the reversed style does not use it.")
                                  : juce::String ("On: the next piece of the same sound plays when its score is under the limit."),
            on[2] ? juce::String ("On: the target waits while the match is over the limit.") : juce::String ("Off: only synaptic slide makes the target wait.") };
        const juce::Colour mark[3] = { on[0] ? look::live : look::inkFaint, on[1] ? look::ink : look::inkFaint, on[2] ? look::ink : look::inkFaint };
        const int rh = z.getHeight() / 3;
        for (int i = 0; i < 3; ++i)
        {
            juce::Rectangle<int> row (z.getX(), z.getY() + rh * i, z.getWidth(), rh);
            row = row.withSizeKeepingCentre (row.getWidth(), juce::jmin (rh, px (16) + px (34)));
            const auto head = row.removeFromTop (px (16));
            g.setColour (mark[i]);
            g.fillRect (head.getX(), head.getY() + 2, 3, head.getHeight() - 4);
            txt (g, name[i], head.withTrimmedLeft (8), on[i] ? look::ink : look::inkDim, fs (12.0f), 1);
            txt (g, says[i], row, look::inkDim, fs (12.0f), 0, juce::Justification::topLeft, 2);
        }
    }

    // ---- 5: the pieces that play leave the "now" line and drift right, overlapping; under them their sum, the output
    void paintGlue (juce::Graphics& g, juce::Rectangle<int> z, juce::uint64 focusSerial)
    {
        const auto& m = ctx.model;
        const auto& lib = m.library();
        if (! lib.ok()) { greyed (g, z, "NO LIBRARY YET", lib.note()); return; }
        if (m.numPicks() == 0)
        {
            // nothing has played yet: the lanes the pieces will overlap in (their number comes from the piece and hop
            // sizes), the NOW line and the output's line, flat: that is the real output, silence. Nothing here moves.
            const int len0 = juce::jmax (1, lib.pieceLength()), hop0 = juce::jmax (1, m.target().hop() > 0 ? m.target().hop() : lib.hop());
            const int lanes0 = juce::jlimit (1, 6, (len0 + hop0 - 1) / hop0 + 1);
            auto zz = z;
            const auto sum = zz.removeFromBottom (zz.getHeight() * 26 / 100);
            zz.removeFromBottom (8);
            const auto axis = zz.removeFromTop (px (15));
            const float laneH = (float) zz.getHeight() / (float) lanes0;
            g.setColour (look::line);
            for (int i = 0; i < lanes0; ++i)
                g.fillRect ((float) zz.getX(), snap ((float) zz.getY() + laneH * ((float) i + 0.5f)), (float) zz.getWidth(), juce::jmax (m_px, 1.0f));
            g.setColour (mix (look::ground, look::green, 0.5f));
            g.fillRect ((float) z.getX(), (float) axis.getY(), 2.0f, (float) (sum.getBottom() - axis.getY()));
            g.fillRect ((float) z.getX(), midY (sum) - 1.0f, (float) sum.getWidth(), 2.0f);
            txt (g, "NOW", axis.withTrimmedLeft (6).withWidth (px (40)), look::green, fs (11.5f), 1);
            txt (g, "up to " + juce::String (lanes0 - 1) + " pieces overlap here", axis.withTrimmedLeft (px (46)), look::inkDim, fs (11.5f), 0, juce::Justification::centredRight);
            txt (g, "their sum: the output, silent", { sum.getX() + 8, sum.getY(), sum.getWidth() - 8, px (15) }, look::inkDim, fs (11.5f), 0);
            return;
        }
        const int len = juce::jmax (1, lib.pieceLength()), hop = juce::jmax (1, m.target().hop() > 0 ? m.target().hop() : lib.hop());
        // one lane more than the pieces that overlap: the engine still counts a piece as sounding while the next one
        // begins, so every piece it says is sounding has a lane of its own
        const int lanes = juce::jlimit (1, 6, (len + hop - 1) / hop + 1);
        const double now = (double) m.playhead (0).renderSample;
        const float loudest = libraryTop();
        const float xNow = (float) z.getX(), wpx = (float) ((double) len * m_k);
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (z.withLeft (z.getX() - 4));
        auto sum = z.removeFromBottom (z.getHeight() * 26 / 100);
        z.removeFromBottom (8);
        const auto axis = z.removeFromTop (px (15));                             // the lanes' time line: now on the left, earlier to the right
        const float laneH = (float) z.getHeight() / (float) lanes;
        juce::Path p;
        const int n = juce::jmin (64, m.numPicks());
        int drawn = 0;
        for (int i = 0; i < n && drawn < kMaxLens; ++i)
        {
            const tap::Pick& pk = m.pick (i);
            if (pk.channel != 0 || ! pk.current) continue;
            const float xs = xNow + (float) ((now - (double) pk.startSample) * m_k);      // its start: the leading edge
            if (xs < xNow || xs - wpx > (float) z.getRight()) continue;                   // not started yet, or drifted out of sight
            const float cy = (float) z.getY() + laneH * ((float) ((pk.startSample / (juce::uint32) hop) % (juce::uint32) lanes) + 0.5f);
            const juce::Colour tint = look::sourceTint (lib.soundOf (pk.libraryPiece));
            lens (p, xs, wpx, cy, (laneH * 0.5f - 2.0f) * loud (lib.peak (pk.libraryPiece), loudest), pk.libraryPiece, false);
            g.setColour (pk.sounding ? tint : mix (look::ground, tint, 0.5f));
            g.fillPath (p);
            if (pk.serial == focusSerial)                                                 // the winner of stage 4 is this piece
            {
                g.setColour (look::ink);
                g.strokePath (p, juce::PathStrokeType (1.4f));
            }
            ++drawn;
        }
        // the finished output's real shape, one column per frame in which the render clock moved
        const float cy = midY (sum), hh = (float) sum.getHeight() * 0.5f;
        juce::Path s;
        int used = 0;
        for (int i = 0; i < m_outN; ++i)
        {
            const Out& o = m_out[(size_t) ((m_outHead - i + kOut) % kOut)];
            const float x = xNow + (float) ((now - (double) o.sample) * m_k);
            const float y = juce::jmin (cy - 1.0f, cy - level (o.hi) * hh);
            if (used == 0) s.startNewSubPath (x, y); else s.lineTo (x, y);
            ++used;
            if (x > (float) sum.getRight()) break;
        }
        for (int i = used - 1; i >= 0; --i)
        {
            const Out& o = m_out[(size_t) ((m_outHead - i + kOut) % kOut)];
            s.lineTo (xNow + (float) ((now - (double) o.sample) * m_k), juce::jmax (cy + 1.0f, cy - level (o.lo) * hh));
        }
        g.setColour (used > 1 ? look::green : mix (look::ground, look::green, 0.35f));
        if (used > 1) { s.closeSubPath(); g.fillPath (s); }
        else g.fillRect (xNow, cy - 1.0f, (float) sum.getWidth(), 2.0f);
        // now: every piece that sounds crosses this line; the dot is its own fade at this instant
        g.setColour (look::green);
        g.fillRect (xNow, (float) axis.getY(), 2.0f, (float) (sum.getBottom() - axis.getY()));
        txt (g, "NOW", axis.withTrimmedLeft (6).withWidth (px (40)), look::green, fs (11.5f), 1);
        txt (g, "earlier >", axis.withTrimmedLeft (px (46)), look::inkDim, fs (11.5f), 0, juce::Justification::centredRight);
        txt (g, "their sum: the output (edge = 25% of full)", { sum.getX() + 8, sum.getY(), sum.getWidth() - 8, px (15) }, look::inkDim, fs (11.5f), 0);
        for (int i = 0; i < m.numVoices(); ++i)
        {
            const tap::Voice& v = m.voice (i);
            if (v.channel != 0) continue;
            const float vy = (float) z.getY() + laneH * ((float) ((v.startSample / (juce::uint32) hop) % (juce::uint32) lanes) + 0.5f);
            const float rad = 2.5f + 4.0f * juce::jlimit (0.0f, 1.0f, v.fade);
            g.setColour (look::ink);
            g.fillEllipse (xNow + 1.0f - rad, vy - rad, rad * 2.0f, rad * 2.0f);
        }
    }

    // ---- one plain line of facts under each stage
    void paintReadouts (juce::Graphics& g, const tap::Pick* focus)
    {
        const auto& m = ctx.model;
        const auto& t = m.target();
        juce::String line[5];
        juce::Colour col[5] = { look::inkDim, look::inkDim, look::inkDim, look::inkDim, look::inkDim };
        const int repeat = focus != nullptr ? focus->settings.repeat : m.settings().repeat;
        if (t.ok()) line[0] = millis (t.pieceLength(), t.rate()) + (repeat > 1 ? " pieces, each searched " + juce::String (repeat) + " times"
                                                                                : " pieces, a new one every " + millis (t.hop(), t.rate()));
        if (m_bandsOf > 0) line[1] = focus == nullptr ? juce::String ("the piece waiting at the cut")
                                   : m_bandsShown < m_bandsOf ? "bands 1 to " + juce::String (m_bandsShown) + " of " + juce::String (m_bandsOf) + " (the target's)"
                                                              : juce::String ("every band holds some energy here");
        if (focus != nullptr)
        {
            const tap::Why& w = focus->why;
            if (w.state == tap::Why::ready)
            {
                line[2] = focus->style >= 2 ? "only " + juce::String (w.allowedTotal) + " neighbours of piece " + juce::String (focus->previousPiece) + " allowed"
                                            : juce::String (w.candidates) + " pieces scored, the best " + juce::String (juce::jmin (m_ranks, 1 + w.runnerUps)) + " shown";
                line[3] = w.approximate ? "~ approximate: " + juce::String (w.note) : juce::String ("exact numbers from the engine");
                col[3] = w.approximate ? look::live : look::green;
            }
            else line[3] = w.note;
        }
        else
        {
            if (m.library().ok()) line[2] = juce::String (m.library().pieces()) + " pieces from " + juce::String (m.library().sounds()) + " sounds, waiting";
            line[3] = "the reasons show at the first pick";
            col[3] = look::inkDim;
        }
        const bool playing = m.transport().playing;
        const float peak = m.meters().outputPeak;
        line[4] = playing ? juce::String (m.numVoices()) + " pieces overlap now, " + (peak < 0.01f ? "very quiet: " : "output peak ") + juce::String (100.0f * peak, 1) + "%"
                : m.numPicks() == 0 ? juce::String ("silent: nothing has played yet") : "nothing is sounding: " + whyNotText();
        if (! playing) col[4] = look::inkDim;
        for (int i = 0; i < 5; ++i)
            txt (g, line[i], { m_colX[i], m_read.getY(), m_colW[i], m_read.getHeight() }, col[i], fs (12.0f), 0);
    }

    // ---- the trail (under stages 1 and 2): the last picks, newest on the left
    void paintTrail (juce::Graphics& g)
    {
        const auto& m = ctx.model;
        const auto& lib = m.library();
        auto r = m_trail;
        g.setColour (look::line);
        g.fillRect (r.getX(), r.getY(), r.getWidth(), 1);
        r.removeFromTop (6);
        auto bottom = r.removeFromBottom (px (15));
        const int most = juce::jlimit (8, 200, r.getWidth() / px (15));
        const int n = m.active() ? m.numPicks() : 0;
        int have = 0;
        for (int i = 0; i < n && have < most; ++i) if (m.pick (i).channel == 0) ++have;
        const int cells = have > 0 ? juce::jmax (juce::jmin (12, most), have) : most;    // as many places as there are picks: the strip is full
        txt (g, have > 0 ? "LAST " + juce::String (have) + " PICKS, NEWEST LEFT" : juce::String ("NO PICKS YET"), bottom.removeFromLeft (bottom.getWidth() * 40 / 100),
             look::inkDim, fs (12.0f), 1);
        txt (g, "tall = close   dot = sticky   line = target stayed", bottom, look::inkDim, fs (12.0f), 0, juce::Justification::centredRight, 1);
        r.removeFromBottom (5);
        r.removeFromTop (7);
        const double pitch = (double) r.getWidth() / (double) cells;
        const auto& scale = m.scoreScale();
        const float base = (float) r.getBottom(), full = (float) r.getHeight();
        int c = 0;
        for (int i = 0; i < n && c < cells; ++i)
        {
            const tap::Pick& p = m.pick (i);
            if (p.channel != 0) continue;
            const float x0 = snap ((float) r.getX() + (float) (pitch * c)), x1 = snap ((float) r.getX() + (float) (pitch * (c + 1))) - 2.0f;
            ++c;
            const bool known = p.current && lib.ok();
            const juce::Colour tint = known ? look::sourceTint (lib.soundOf (p.libraryPiece)) : look::inkFaint;
            const bool ready = p.why.state == tap::Why::ready;
            // height = how close, only once the engine's score is here; until then a flat stub (no closeness is claimed)
            const float h = ready ? 5.0f + (1.0f - scale.unit (p.why.played.piece >= 0 ? p.why.played.total : p.why.winner.total)) * (full - 5.0f) : 3.0f;
            g.setColour (ready ? tint : mix (look::ground, tint, 0.5f));
            g.fillRect (x0, snap (base - h), x1 - x0, snap (h));
            if (p.playedFollowing == tap::Pick::yes)
            {
                g.setColour (look::ink);
                g.fillEllipse ((x0 + x1) * 0.5f - 2.5f, (float) r.getY() - 6.0f, 5.0f, 5.0f);
            }
            if (! p.targetMovedOn)
            {
                g.setColour (look::ink);
                g.fillRect (x0, base + 1.0f, x1 - x0, 2.0f * m_px + 1.0f);
            }
        }
        g.setColour (look::line);                                                // the places no pick has reached yet
        for (; c < cells; ++c)
        {
            const float x0 = snap ((float) r.getX() + (float) (pitch * c)), x1 = snap ((float) r.getX() + (float) (pitch * (c + 1))) - 2.0f;
            g.fillRect (x0, base - 2.0f, x1 - x0, 2.0f);
        }
    }

    // ---- the bank (under stages 3 to 5): the whole library the tributaries come from, every piece by its sound's
    // tint. Lit = the pieces this search could pick: all of them, or in a synaptic style only the neighbours it was
    // allowed to visit. White = the candidates shown above and the piece that plays. Under each piece its "used"
    // memory as it is now: a pick fills it, boredom drains it.
    void paintBank (juce::Graphics& g, const tap::Pick* focus)
    {
        const auto& m = ctx.model;
        const auto& lib = m.library();
        auto r = m_bank;
        g.setColour (look::line);
        g.fillRect (r.getX(), r.getY(), r.getWidth(), 1);
        r.removeFromTop (6);
        auto bottom = r.removeFromBottom (px (15));
        const int pieces = m.active() && lib.ok() ? lib.pieces() : 0;
        if (pieces < 1)
        {
            txt (g, "THE LIBRARY", bottom, look::inkDim, fs (11.5f), 1);
            txt (g, m.active() ? juce::String (lib.note()) : juce::String ("Not reading the engine."), r, look::inkDim, fs (12.0f), 0, juce::Justification::centred);
            return;
        }
        const bool ready = focus != nullptr && focus->current && focus->why.state == tap::Why::ready;
        const bool net = ready && focus->style >= 2, novel = m.settings().novelty > 0.0f;
        txt (g, "THE LIBRARY: ALL " + juce::String (pieces) + " PIECES", bottom.removeFromLeft (bottom.getWidth() * 30 / 100), look::inkDim, fs (12.0f), 1);
        txt (g, (net ? "lit = the " + juce::String (focus->why.allowedTotal) + " neighbours it may visit" : juce::String ("bar = one piece, by its sound"))
                 + "   white = picked   " + (novel ? "cyan = used lately, draining" : "grey = used lately (not counted)"),
             bottom, look::inkDim, fs (12.0f), 0, juce::Justification::centredRight, 1);
        const int usedH = px (12);
        const auto usedRow = r.removeFromBottom (usedH);
        r.removeFromBottom (2);
        r.removeFromTop (px (6));                                                // room for the white marks
        const int cols = juce::jlimit (1, juce::jmax (1, r.getWidth() / 3), pieces);
        m_lit.assign ((size_t) cols, 0);                                         // per bar: 1 allowed, 2 a candidate shown, 4 the piece that plays
        auto colOf = [cols, pieces] (int piece) { return (size_t) juce::jlimit (0, cols - 1, (int) ((juce::int64) piece * cols / pieces)); };
        if (net)
            for (int i = 0; i < focus->why.allowedListed; ++i)
                if (focus->why.allowedOn[i]) m_lit[colOf (focus->why.allowed[i])] |= 1;
        if (ready)
        {
            if (focus->why.winner.piece >= 0) m_lit[colOf (focus->why.winner.piece)] |= 2;
            for (int i = 0; i < juce::jmin (m_ranks - 1, focus->why.runnerUps); ++i)
                if (focus->why.runnerUp[i].piece >= 0) m_lit[colOf (focus->why.runnerUp[i].piece)] |= 2;
        }
        if (focus != nullptr && focus->current) m_lit[colOf (focus->libraryPiece)] |= 4;
        const float loudest = libraryTop();
        const double pitch = (double) r.getWidth() / (double) cols;
        const float base = (float) r.getBottom(), full = (float) r.getHeight(), gapW = pitch >= 4.0 ? juce::jmax (m_px, 1.0f) : 0.0f;
        for (int c = 0; c < cols; ++c)
        {
            const int p0 = (int) ((juce::int64) c * pieces / cols), p1 = juce::jmax (p0 + 1, (int) ((juce::int64) (c + 1) * pieces / cols));
            const int stride = juce::jmax (1, (p1 - p0) / 4);                    // (a huge library: a few pieces stand for each bar)
            float pk = 0, us = 0;
            for (int p = p0; p < p1; p += stride) { pk = juce::jmax (pk, lib.peak (p)); us = juce::jmax (us, m.usedUnit (p)); }
            const int snd = lib.soundOf (p0);
            const bool on = lib.soundOn (snd) && ! lib.soundRemoved (snd);
            const juce::Colour tint = look::sourceTint (snd);
            const int f = m_lit[(size_t) c];
            const bool may = on && (! net || (f & 1) != 0);                      // this search could pick it
            const float x0 = snap ((float) r.getX() + (float) (pitch * c)), x1 = juce::jmax (x0 + m_px, snap ((float) r.getX() + (float) (pitch * (c + 1))) - gapW);
            const float h = juce::jmax (2.0f, snap (full * (0.22f + 0.78f * loud (pk, loudest))));
            g.setColour ((f & 4) != 0 ? mix (tint, look::ink, 0.3f) : may ? mix (look::ground, tint, net ? 1.0f : 0.62f) : mix (look::ground, tint, on ? 0.26f : 0.12f));
            g.fillRect (x0, base - h, x1 - x0, h);
            if ((f & 6) != 0)
            {
                g.setColour ((f & 4) != 0 ? look::ink : look::inkDim);
                g.fillRect (x0, base - h - ((f & 4) != 0 ? 5.0f : 4.0f), x1 - x0, (f & 4) != 0 ? 3.0f : 2.0f);
            }
            if (us > 0.004f)
            {
                g.setColour (novel ? look::live : look::inkFaint);
                g.fillRect (x0, (float) usedRow.getY() + 1.0f, x1 - x0, juce::jmax (m_px, snap (juce::jmin (1.0f, us) * (float) (usedH - 1))));
            }
        }
    }

    // The band rows stay put from pick to pick: as many as the highest band that holds energy in any piece of the
    // target (looked up once per target; a huge one: every few pieces). What a library piece holds above them is not drawn.
    int steadyBandRows (int n)
    {
        const auto& m = ctx.model;
        if (m_bandRowsKnown && m_bandRowsOf == n && m_bandRowsTgt == m.target().generation() && m_bandRowsLib == m.library().generation()) return m_bandRows;
        int rows = juce::jmin (n, 6);
        auto scan = [&rows, n] (tap::Floats row)
        {
            if ((int) row.size() != n) return;
            float top = 0;
            for (float v : row) top = juce::jmax (top, v);
            if (! (top > 0)) return;
            const float least = top * 3.0e-6f;                                   // (the six decades the chart shows)
            for (int i = n - 1; i >= rows; --i) if (row[(size_t) i] > least) { rows = i + 1; break; }
        };
        const int tp = m.target().ok() ? m.target().pieces() : 0;
        for (int j = 0; j < tp; j += juce::jmax (1, tp / 256)) scan (m.target().bands (0, j));
        m_bandRows = rows; m_bandRowsOf = n; m_bandRowsTgt = m.target().generation(); m_bandRowsLib = m.library().generation(); m_bandRowsKnown = true;
        return rows;
    }

    std::vector<ParamRow*> m_rows;
    std::vector<float> m_edgeX, m_edgeA, m_edgeB;
    float m_edgeTop = 0; juce::uint32 m_edgeTopGen = 0; bool m_edgeTopKnown = false;
    int m_ranks = kRanks;                    // candidates shown: 7, or all 9 the engine reports in a tall window
    int m_bandRows = 6, m_bandRowsOf = 0;
    juce::uint32 m_bandRowsTgt = 0, m_bandRowsLib = 0;
    bool m_bandRowsKnown = false;
    float m_tgtTop = 0, m_libTop = 0;
    juce::uint32 m_tgtTopGen = 0, m_libTopGen = 0;
    bool m_tgtTopKnown = false, m_libTopKnown = false;
    juce::Image m_headImg, m_midImg, m_midNext;
    int m_buildStep = 0, m_buildKey = -1, m_midSlot[4] = { 64, 64, 64, 64 };
    juce::uint64 m_buildSerial = 0;
    juce::Rectangle<int> m_headAt, m_midAt;
    int m_headKey = -1, m_midKey = -1;
    std::vector<Slot> m_slots;
    size_t m_slot = 0;
    std::array<Out, kOut> m_out {};
    int m_outN = 0, m_outHead = 0;
    juce::uint32 m_lastRender = 0;
    Sig m_sig;
    juce::uint64 m_focusSerial = 0;
    int m_focusFrames = 0;
    float m_s = 1.0f, m_px = 1.0f;
    double m_k = 0.01;
    int m_colX[5] = {}, m_colW[5] = {};
    juce::Rectangle<int> m_river, m_ctrl, m_head, m_pic, m_read, m_trail, m_bank;
    int m_bandsShown = 0, m_bandsOf = 0;
    std::vector<juce::uint8> m_lit;
};
}

std::unique_ptr<View> createRiverView (Context& c) { return std::make_unique<RiverView> (c); }
}
