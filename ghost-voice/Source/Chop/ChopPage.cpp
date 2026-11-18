// Ghost Voice - AGPL-3.0-only (see LICENSE, NOTICE.md).
#include "ChopPage.h"
#include "Processor.h"
#include "Look.h"
#include "SplashArt.h"
#include "src/PluginProcessor.h"   // INTERSECT: its per-slice fields
#include <cmath>

namespace gv
{
namespace
{
// SPLASH (build 9): the playhead / loop in the painting's deep cyan, the selection and tools in its orchid; chops cycle its paints
// build 12: from the look in use (LIGHT / DARK): the playhead + loop colour, the selection + tools colour, the chops' colours
#define bsGreen (splash::art().play)
#define bsPurple (splash::art().select)
juce::Colour chopColour (int i) { static const int order[] { 3, 6, 1, 5, 0, 7, 2, 4 }; const auto& a = splash::art(); return (splash::isDark() ? a.inks : a.paints)[order[((i % 8) + 8) % 8]]; }
juce::String noteName (int n) { return juce::MidiMessage::getMidiNoteName (n, true, true, 3); }   // C3 = 60, as in FL Studio
juce::String secs (double s) { return s >= 60 ? juce::String ((int) (s / 60)) + ":" + juce::String (std::fmod (s, 60.0), 2).paddedLeft ('0', 5) : juce::String (s, 2) + " s"; }
}

// ================================================================================================ tool buttons (Block Step's)
struct ChopPage::ToolButton : public juce::Button
{
    ToolButton (ChopPage& p, Tool t) : juce::Button (toolName (t)), page (p), tool (t)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip (toolName (t) + " (" + juce::String::charToString (toolKey (t)).toUpperCase() + "): " + toolTip (t));
        onClick = [this] { page.setTool (tool); };
    }
    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const auto& P = page.pal();
        auto r = getLocalBounds().toFloat().reduced (1.5f);
        const bool on = page.tool() == tool;
        splash::handButton (g, P, getLocalBounds().toFloat(), on, true, over, 300 + (int) tool);   // hand-drawn (build 10)
        g.setColour (on ? juce::Colours::white : P.ink);
        g.setFont (gfont (11.0f)); g.drawText (toolName (tool), r.withTrimmedRight (18), juce::Justification::centred);
        auto k = r.removeFromRight (18).reduced (2, 6);
        g.setColour ((on ? juce::Colours::white : P.dim).withAlpha (0.25f)); g.fillRoundedRectangle (k, 3);
        g.setColour (on ? juce::Colours::white : P.dim); g.setFont (gfont (10.0f));
        g.drawText (juce::String::charToString (toolKey (tool)).toUpperCase(), k, juce::Justification::centred);
    }
    ChopPage& page; Tool tool;
};

// ================================================================================================ DRAG WAV (out to FL Studio / Finder)
struct ChopPage::DragOut : public juce::Component, public juce::SettableTooltipClient
{
    explicit DragOut (ChopPage& p) : page (p)
    { setMouseCursor (juce::MouseCursor::DraggingHandCursor); setTooltip ("Drag the selection (or the whole clip) out as a WAV, into FL Studio's playlist or a folder"); }
    void paint (juce::Graphics& g) override
    {
        const auto& P = page.pal(); auto r = getLocalBounds().toFloat().reduced (1.5f);
        const bool ok = page.engine().hasClip();
        splash::handButton (g, P, getLocalBounds().toFloat(), false, ok, false, 404);   // hand-drawn (build 10)
        g.setColour (ok ? P.ink : P.dim.withAlpha (0.5f)); g.setFont (gfont (11.5f)); g.drawText ("DRAG WAV", r, juce::Justification::centred);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging || e.getDistanceFromDragStart() < 6 || ! page.engine().hasClip()) return;
        dragging = true;
        const auto f = page.engine().writeRegion (page.selection, "ghostvoice");
        if (f.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this, [this] { dragging = false; });
        else dragging = false;
    }
    void mouseUp (const juce::MouseEvent&) override { dragging = false; }
    ChopPage& page; bool dragging = false;
};

// ================================================================================================ the wave
struct ChopPage::Wave : public juce::Component
{
    explicit Wave (ChopPage& p) : page (p) { setWantsKeyboardFocus (false); }
    ChopPage& page;
    static constexpr int rulerH = 24, overviewH = 16, bin = 256;
    std::vector<float> peaksMin, peaksMax; const void* peaksFor = nullptr;
    enum class Drag { none, select, loopRuler, overview, playback } drag = Drag::none;
    juce::int64 dragFrom = 0;

    juce::Rectangle<int> waveArea() const { return getLocalBounds().withTrimmedTop (rulerH).withTrimmedBottom (overviewH + 2); }
    juce::Rectangle<int> rulerArea() const { return getLocalBounds().removeFromTop (rulerH); }
    juce::Rectangle<int> overviewArea() const { return getLocalBounds().removeFromBottom (overviewH); }
    double spp() const { return page.viewFrames / juce::jmax (1, getWidth()); }
    juce::int64 frameAt (float x) const { return (juce::int64) std::llround (page.viewStart + (double) x * spp()); }
    float xOf (double frame) const { return (float) ((frame - page.viewStart) / juce::jmax (1.0e-9, spp())); }

    void buildPeaks (const chop::Clip& c)
    {
        peaksFor = &c;
        const auto nb = (size_t) (c.frames() / bin + 1);
        peaksMin.assign (nb, 0.0f); peaksMax.assign (nb, 0.0f);
        for (size_t b = 0; b < nb; ++b)
        {
            float mn = 0, mx = 0;
            const auto a = (int) (b * bin), e = (int) juce::jmin<juce::int64> (c.frames(), (juce::int64) a + bin);
            for (int ch = 0; ch < c.audio.getNumChannels(); ++ch)
            { const float* s = c.audio.getReadPointer (ch); for (int i = a; i < e; ++i) { mn = juce::jmin (mn, s[i]); mx = juce::jmax (mx, s[i]); } }
            peaksMin[b] = mn; peaksMax[b] = mx;
        }
    }
    void minMax (const chop::Clip& c, double f0, double f1, float& mn, float& mx) const
    {
        mn = 0; mx = 0;
        const auto a = (juce::int64) juce::jmax (0.0, f0), e = juce::jmin (c.frames(), (juce::int64) std::ceil (f1));
        if (e <= a) return;
        if (e - a >= bin * 2)
        {
            for (auto b = a / bin; b <= (e - 1) / bin && b < (juce::int64) peaksMin.size(); ++b) { mn = juce::jmin (mn, peaksMin[(size_t) b]); mx = juce::jmax (mx, peaksMax[(size_t) b]); }
            return;
        }
        for (int ch = 0; ch < c.audio.getNumChannels(); ++ch)
        { const float* s = c.audio.getReadPointer (ch); for (auto i = a; i < e; ++i) { mn = juce::jmin (mn, s[i]); mx = juce::jmax (mx, s[i]); } }
    }

    void paint (juce::Graphics& g) override
    {
        const auto& P = page.pal();
        auto& eng = page.engine();
        auto all = getLocalBounds().toFloat();
        { juce::Path b; b.addRoundedRectangle (all.reduced (1), 14); g.setColour (P.waveBg); g.fillPath (b); splash::sketch (g, b, 501, 5, 0.75f, 0.45f); }
        auto c = eng.clip();
        if (c == nullptr || c->frames() == 0)
        {
            g.setColour (P.ink); g.setFont (gfont (22.0f)); g.drawText ("Drag a WAV in here", all.withTrimmedBottom (all.getHeight() * 0.5f - 6), juce::Justification::centredBottom);
            g.setColour (P.dim); g.setFont (gfont (13.5f, false));
            g.drawText ("or press ADD WAV, or REC to record.  Then AUTO CHOP, and play it on the keys.", all.withTrimmedTop (all.getHeight() * 0.5f + 8), juce::Justification::centredTop);
            return;
        }
        if (peaksFor != c.get()) buildPeaks (*c);
        const auto wa = waveArea().toFloat(), ra = rulerArea().toFloat();
        const auto det = eng.detected();
        // the beat grid (from the analysis) or seconds
        {
            g.setColour (P.cell); g.fillRect (ra.withTrimmedLeft (8).withTrimmedRight (8));
            const double beat = det.ready && det.bpm > 0 ? 60.0 / det.bpm * c->rate : 0.0;
            g.setFont (gfont (10.0f, false));
            if (beat > 0)
            {
                const double first = det.firstBeat * c->rate;
                int every = 1; while (beat * every / spp() < 18) every *= 2;
                const auto k0 = (juce::int64) std::floor ((page.viewStart - first) / beat), k1 = (juce::int64) std::ceil ((page.viewStart + page.viewFrames - first) / beat);
                for (auto k = k0; k <= k1; ++k)
                {
                    if (k % every != 0) continue;
                    const float x = xOf (first + (double) k * beat); if (x < 0 || x > (float) getWidth()) continue;
                    const bool bar = ((k % 4) + 4) % 4 == 0;
                    g.setColour (P.overlay.withAlpha (bar ? 0.16f : 0.06f)); g.fillRect (x, wa.getY(), 1.0f, wa.getHeight());
                    if (bar && k >= 0) { g.setColour (P.dim); g.drawText (juce::String (k / 4 + 1), juce::Rectangle<float> (x + 3, ra.getY(), 40, ra.getHeight()), juce::Justification::centredLeft); }
                }
            }
            else
            {
                double step = 0.1; while (step * c->rate / spp() < 60) step *= 2;
                for (double t = std::floor (page.viewStart / c->rate / step) * step; t * c->rate < page.viewStart + page.viewFrames; t += step)
                {
                    const float x = xOf (t * c->rate); if (x < 0) continue;
                    g.setColour (P.overlay.withAlpha (0.08f)); g.fillRect (x, wa.getY(), 1.0f, wa.getHeight());
                    g.setColour (P.dim); g.drawText (juce::String (t, step < 1 ? 1 : 0) + "s", juce::Rectangle<float> (x + 3, ra.getY(), 50, ra.getHeight()), juce::Justification::centredLeft);
                }
            }
        }
        // the loop (on the ruler, like Block Step's step numbers)
        if (! page.loop.empty())
        {
            const float x0 = juce::jmax (0.0f, xOf ((double) page.loop.a)), x1 = juce::jmin ((float) getWidth(), xOf ((double) page.loop.b));
            if (x1 > x0)
            {
                const auto lc = page.loopOn ? bsGreen : P.dim;
                g.setColour (lc.withAlpha (page.loopOn ? 0.35f : 0.18f)); g.fillRect (x0, ra.getY() + 3, x1 - x0, ra.getHeight() - 6);
                g.setColour (lc); g.fillRect (x0, ra.getY() + 3, 2.0f, ra.getHeight() - 6); g.fillRect (x1 - 2, ra.getY() + 3, 2.0f, ra.getHeight() - 6);
                if (page.loopOn) { g.setColour (bsGreen.withAlpha (0.05f)); g.fillRect (x0, wa.getY(), x1 - x0, wa.getHeight()); }
            }
        }
        // the chops (what INTERSECT holds), one band per key
        const auto sl = eng.slices();
        if (eng.keys == chop::KeysMode::chops)
        {
            for (size_t i = 0; i < sl.size(); ++i)
            {
                const auto& s = sl[i];
                const float x0 = xOf ((double) s.r.a), x1 = xOf ((double) s.r.b);
                if (x1 < 0 || x0 > (float) getWidth()) continue;
                const auto cc = chopColour ((int) i);
                const bool on = eng.noteSounding (s.note);
                g.setColour (cc.withAlpha (on ? 0.32f : 0.13f)); g.fillRect (x0, wa.getY(), juce::jmax (1.0f, x1 - x0), wa.getHeight());
                g.setColour (cc.withAlpha (0.9f)); g.fillRect (x0, wa.getY(), 2.0f, wa.getHeight());
                juce::String txt = noteName (s.note); if (eng.labelFor (i).isNotEmpty()) txt << " " << eng.labelFor (i);
                const float tw = juce::jmin (ghosty::textWidth (gfont (10.0f), txt) + 10.0f, juce::jmax (0.0f, x1 - x0 - 4));
                auto tag = juce::Rectangle<float> (x0 + 3, wa.getY() + 3 + (float) (i % 2) * 17.0f, tw, 15);
                if (x1 - x0 > 14) { g.setColour (cc.withAlpha (0.85f)); g.fillRoundedRectangle (tag, 4);
                                    g.setColour (juce::Colours::black); g.setFont (gfont (10.0f)); g.drawText (txt, tag, juce::Justification::centred, true); }
                if (s.index == page.selectedChop) { g.setColour (cc); g.drawRect (juce::Rectangle<float> (x0, wa.getY(), juce::jmax (2.0f, x1 - x0), wa.getHeight()), 2.0f); }
            }
        }
        // the waveform
        {
            const float mid = wa.getCentreY(), h = wa.getHeight() * 0.46f;
            juce::Path p; bool started = false;
            std::vector<float> lo ((size_t) getWidth()), hi ((size_t) getWidth());
            for (int x = 0; x < getWidth(); ++x)
            {
                float mn, mx; minMax (*c, page.viewStart + x * spp(), page.viewStart + (x + 1) * spp(), mn, mx);
                lo[(size_t) x] = mid - mx * h; hi[(size_t) x] = mid - mn * h;
            }
            for (int x = 0; x < getWidth(); ++x) { if (! started) { p.startNewSubPath ((float) x, lo[(size_t) x]); started = true; } else p.lineTo ((float) x, lo[(size_t) x]); }
            for (int x = getWidth() - 1; x >= 0; --x) p.lineTo ((float) x, juce::jmax (hi[(size_t) x], lo[(size_t) x] + 1.0f));
            p.closeSubPath();
            juce::ColourGradient gr (splash::art().wave0, 0, wa.getY(), splash::art().wave2, 0, wa.getBottom(), false);
            gr.addColour (0.5, splash::art().wave1);
            g.setGradientFill (gr); g.fillPath (p);
            splash::sketch (g, p, 77, 2, 0.5f, 0.35f);
            g.setColour (P.overlay.withAlpha (0.12f)); g.fillRect (wa.getX(), mid, wa.getWidth(), 1.0f);
        }
        // the selection
        if (! page.selection.empty())
        {
            const float x0 = xOf ((double) page.selection.a), x1 = xOf ((double) page.selection.b);
            g.setColour (bsPurple.withAlpha (0.14f)); g.fillRect (x0, wa.getY(), x1 - x0, wa.getHeight());
            g.setColour (bsPurple.withAlpha (0.85f)); g.fillRect (x0, wa.getY(), 1.5f, wa.getHeight()); g.fillRect (x1 - 1.5f, wa.getY(), 1.5f, wa.getHeight());
            g.setFont (gfont (10.5f, false)); g.setColour (P.ink);
            g.drawText (secs ((double) page.selection.length() / c->rate), juce::Rectangle<float> (x0 + 4, wa.getBottom() - 18, 90, 16), juce::Justification::centredLeft);
        }
        // the cursor + the playhead
        { const float x = xOf ((double) page.cursor); g.setColour (bsPurple.withAlpha (0.9f)); g.fillRect (x, ra.getY(), 1.0f, wa.getBottom() - ra.getY()); }
        if (eng.isPlaying()) { const float x = xOf ((double) eng.playhead()); g.setColour (bsGreen); g.fillRect (x - 0.5f, ra.getY(), 2.0f, wa.getBottom() - ra.getY()); }
        // the overview (what part of the clip is on screen)
        {
            const auto o = overviewArea().toFloat().reduced (8, 2);
            g.setColour (P.cell); g.fillRoundedRectangle (o, 4);
            const float a = o.getX() + o.getWidth() * (float) (page.viewStart / (double) c->frames()), w = o.getWidth() * (float) (page.viewFrames / (double) c->frames());
            g.setColour (bsPurple.withAlpha (0.55f)); g.fillRoundedRectangle (a, o.getY(), juce::jmax (6.0f, w), o.getHeight(), 4);
        }
    }

    juce::int64 snapFrame (juce::int64 f) const { return juce::jlimit ((juce::int64) 0, page.engine().frames(), f); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        page.grabKeyboardFocus();
        auto& eng = page.engine(); if (! eng.hasClip()) { page.mouseDown (e.getEventRelativeTo (&page)); return; }
        const auto f = snapFrame (frameAt ((float) e.x));
        drag = Drag::none; dragFrom = f;
        if (overviewArea().contains (e.getPosition())) { drag = Drag::overview; mouseDrag (e); return; }
        if (rulerArea().contains (e.getPosition()))
        {   // drag along the ruler = the loop (left or right button, like Block Step's step numbers); a click moves the cursor
            drag = Drag::loopRuler; page.cursor = f; repaint(); return;
        }
        switch (page.tool())
        {
            case Tool::select:
                if (e.mods.isShiftDown() && ! page.selection.empty())
                { dragFrom = std::abs (f - page.selection.a) > std::abs (f - page.selection.b) ? page.selection.a : page.selection.b; }
                drag = Drag::select; page.cursor = f;
                if (! e.mods.isShiftDown()) page.selection = {};
                break;
            case Tool::slice: eng.splitAt (f); break;
            case Tool::erase: if (! eng.removeChopAt (f)) page.say ("No chop there to delete (DELETE removes chops; Delete key removes the selected audio)"); break;
            case Tool::zoom: page.zoomBy (e.mods.isPopupMenu() ? 2.0 : 0.5, (double) f); break;
            case Tool::playback: drag = Drag::playback; eng.play (f, {}, false); break;
            default: break;
        }
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto& eng = page.engine(); if (! eng.hasClip()) return;
        const auto f = snapFrame (frameAt ((float) e.x));
        if (drag == Drag::overview)
        {
            const auto o = overviewArea().toFloat().reduced (8, 2);
            const double centre = (double) ((float) e.x - o.getX()) / juce::jmax (1.0f, o.getWidth()) * (double) eng.frames();
            page.viewStart = juce::jlimit (0.0, juce::jmax (0.0, (double) eng.frames() - page.viewFrames), centre - page.viewFrames * 0.5);
        }
        else if (drag == Drag::select) { page.selection = { juce::jmin (dragFrom, f), juce::jmax (dragFrom, f) }; page.cursor = page.selection.a; }
        else if (drag == Drag::loopRuler && std::abs (e.getDistanceFromDragStartX()) > 3) { page.loop = { juce::jmin (dragFrom, f), juce::jmax (dragFrom, f) }; page.loopOn = true; }
        else if (drag == Drag::playback) eng.play (f, {}, false);
        page.repaint();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (drag == Drag::playback) page.engine().stop();
        if (drag == Drag::select && page.selection.length() < 32)
        {   // a click (no drag) on a chop picks it for the SOUND panel
            page.selection = {};
            if (page.engine().keys == chop::KeysMode::chops)
                for (const auto& s : page.engine().slices()) if (page.cursor >= s.r.a && page.cursor < s.r.b) { page.selectChop (s.index); break; }
        }
        if (drag == Drag::loopRuler && page.loopOn && page.loop.length() > 64 && page.engine().isPlaying())
            page.engine().play (page.loop.a, page.loop, true);
        drag = Drag::none; page.repaint();
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {   // double-click a chop with SELECT: select it
        if (page.tool() != Tool::select) return;
        const auto f = frameAt ((float) e.x);
        for (const auto& s : page.engine().slices()) if (f >= s.r.a && f < s.r.b && page.engine().keys == chop::KeysMode::chops) { page.selection = s.r; page.cursor = s.r.a; page.repaint(); return; }
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        auto& eng = page.engine(); if (! eng.hasClip()) return;
        if (e.mods.isCommandDown() || e.mods.isCtrlDown()) { page.zoomBy (w.deltaY > 0 ? 0.8 : 1.25, (double) frameAt ((float) e.x)); return; }
        const float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
        page.viewStart = juce::jlimit (0.0, juce::jmax (0.0, (double) eng.frames() - page.viewFrames), page.viewStart - (double) d * page.viewFrames * 0.25);
        page.repaint();
    }
    void mouseMove (const juce::MouseEvent& e) override
    {
        static const juce::MouseCursor::StandardCursorType cur[] { juce::MouseCursor::IBeamCursor, juce::MouseCursor::CrosshairCursor, juce::MouseCursor::NoCursor,
                                                                   juce::MouseCursor::CrosshairCursor, juce::MouseCursor::PointingHandCursor };
        if (rulerArea().contains (e.getPosition()) || overviewArea().contains (e.getPosition())) setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        else setMouseCursor (page.tool() == Tool::erase ? juce::MouseCursor::NormalCursor : juce::MouseCursor (cur[(int) page.tool()]));
    }
};

// ================================================================================================ the keyboard
struct ChopPage::Keys : public juce::MidiKeyboardComponent
{
    Keys (ChopPage& p, juce::MidiKeyboardState& s) : juce::MidiKeyboardComponent (s, horizontalKeyboard), page (p)
    {
        setAvailableRange (0, 127); setLowestVisibleKey (24); setScrollButtonsVisible (true);
        setOctaveForMiddleC (3);   // C3 = 60, as in FL Studio
        setWantsKeyboardFocus (false); setKeyPressBaseOctave (4);
        clearKeyMappings();
    }
    ChopPage& page;
    int chopIndexFor (int note) const
    {
        const auto sl = page.engine().slices();
        for (size_t i = 0; i < sl.size(); ++i) if (note >= sl[i].note && note <= sl[i].highNote) return (int) i;
        return -1;
    }
    void drawWhiteNote (int note, juce::Graphics& g, juce::Rectangle<float> r, bool down, bool over, juce::Colour, juce::Colour) override
    {
        const auto& P = page.pal();
        const int ci = chopIndexFor (note);
        const bool chops = page.engine().keys == chop::KeysMode::chops;
        auto fill = P.keyWhite;
        if (ci >= 0 && chops) fill = fill.interpolatedWith (chopColour (ci), 0.55f);
        if (down || page.engine().noteSounding (note)) fill = bsGreen;
        else if (over) fill = fill.brighter (0.1f);
        const auto& kb = splash::handBox (r.reduced (0.8f, 0.5f), 3.0f, note * 17, 0.5f);   // each key inked by hand (build 10)
        g.setColour (fill); g.fillPath (kb);
        g.setColour (P.ink.withAlpha (0.55f)); g.strokePath (kb, juce::PathStrokeType (0.9f));
        if (note % 12 == 0 || (ci >= 0 && chops))
        {
            g.setColour (juce::Colours::black.withAlpha (0.7f)); g.setFont (gfont (9.5f, note % 12 == 0));
            g.drawText (noteName (note), r.withTrimmedTop (r.getHeight() - 16), juce::Justification::centred);
        }
    }
    void drawBlackNote (int note, juce::Graphics& g, juce::Rectangle<float> r, bool down, bool over, juce::Colour) override
    {
        const auto& P = page.pal();
        const int ci = chopIndexFor (note);
        auto fill = P.keyBlack;
        if (ci >= 0 && page.engine().keys == chop::KeysMode::chops) fill = fill.interpolatedWith (chopColour (ci), 0.6f);
        if (down || page.engine().noteSounding (note)) fill = bsGreen.darker (0.2f);
        else if (over) fill = fill.brighter (0.2f);
        const auto& kb = splash::handBox (r.reduced (0.5f, 0).withTrimmedTop (-3), 2.5f, note * 17, 0.4f);
        g.setColour (fill); g.fillPath (kb); g.setColour (P.ink.withAlpha (0.8f)); g.strokePath (kb, juce::PathStrokeType (0.8f));
    }
};


// ================================================================================================ SOUND: one chop's own settings (INTERSECT's per-slice controls)
struct ChopPage::Settings : public juce::Component
{
    using P = IntersectProcessor;
    struct Row { juce::String name; int field; juce::Slider slider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox }; std::function<juce::String (double)> text;
                 std::function<float (double)> toEngine; std::function<double (float)> fromEngine; };
    explicit Settings (ChopPage& p) : page (p)
    {
        auto add = [this] (juce::String n, int f, double lo, double hi, double step, std::function<juce::String (double)> t,
                           std::function<float (double)> to = nullptr, std::function<double (float)> from = nullptr, double skewMid = 0)
        {
            auto r = std::make_unique<Row>(); r->name = std::move (n); r->field = f; r->text = std::move (t);
            r->toEngine = to ? to : [] (double v) { return (float) v; }; r->fromEngine = from ? from : [] (float v) { return (double) v; };
            r->slider.setRange (lo, hi, step); if (skewMid > 0) r->slider.setSkewFactorFromMidPoint (skewMid);
            r->slider.setWantsKeyboardFocus (false);
            auto* row = r.get();
            r->slider.onValueChange = [this, row] { if (! syncing) page.engine().setSliceParam (page.selectedChop, row->field, row->toEngine (row->slider.getValue())); repaint(); };
            r->slider.onDragEnd = [this] { page.refreshControls(); };
            addAndMakeVisible (r->slider); rows.push_back (std::move (r));
        };
        add ("PITCH", P::FieldPitch, -24, 24, 1, [] (double v) { return (v > 0 ? "+" : "") + juce::String ((int) v) + " st"; });
        add ("FINE", P::FieldCentsDetune, -100, 100, 1, [] (double v) { return (v > 0 ? "+" : "") + juce::String ((int) v) + " c"; });
        add ("VOLUME", P::FieldVolume, -36, 12, 0.1, [] (double v) { return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; });
        add ("ATTACK", P::FieldAttack, 0, 2000, 1, [] (double v) { return juce::String ((int) v) + " ms"; },
             [] (double v) { return (float) (v / 1000.0); }, [] (float v) { return (double) v * 1000.0; }, 100);
        add ("RELEASE", P::FieldRelease, 0, 5000, 1, [] (double v) { return juce::String ((int) v) + " ms"; },
             [] (double v) { return (float) (v / 1000.0); }, [] (float v) { return (double) v * 1000.0; }, 300);
        add ("CUTOFF", P::FieldFilterCutoff, 20, 20000, 1, [] (double v) { return v >= 1000 ? juce::String (v / 1000.0, 1) + " kHz" : juce::String ((int) v) + " Hz"; }, nullptr, nullptr, 1000);
        add ("RESO", P::FieldFilterReso, 0, 1, 0.01, [] (double v) { return juce::String (juce::roundToInt (v * 100)) + " %"; });
        auto combo = [this] (juce::ComboBox& c, juce::StringArray items, int field, juce::String tip)
        {
            c.addItemList (items, 1); c.setWantsKeyboardFocus (false); c.setTooltip (tip); addAndMakeVisible (c);
            c.onChange = [this, &c, field] { if (! syncing) page.engine().setSliceParam (page.selectedChop, field, (float) (c.getSelectedId() - 1)); page.refreshControls(); };
        };
        combo (stretch, { "REPITCH", "STRETCH", "SMOOTH" }, P::FieldAlgorithm,
               "How the chop is pitched: REPITCH speeds it up / slows it down like a sampler; STRETCH and SMOOTH keep its length");
        combo (loopMode, { "NO LOOP", "LOOP", "PING-PONG" }, P::FieldLoop, "Loop the chop while the key is held");
        combo (filter, { "NO FILTER", "LOW-PASS", "HIGH-PASS", "BAND-PASS", "NOTCH" }, P::FieldFilterEnabled, "The chop's filter");
        filter.onChange = [this]
        {
            if (syncing) return;
            const int id = filter.getSelectedId();
            page.engine().setSliceParam (page.selectedChop, P::FieldFilterEnabled, id > 1 ? 1.0f : 0.0f);
            if (id > 1) page.engine().setSliceParam (page.selectedChop, P::FieldFilterType, (float) (id - 2));
            page.refreshControls();
        };
        auto toggle = [this] (juce::ToggleButton& t, int field, juce::String tip)
        {
            t.setWantsKeyboardFocus (false); t.setTooltip (tip); addAndMakeVisible (t);
            t.onClick = [this, &t, field] { if (! syncing) page.engine().setSliceParam (page.selectedChop, field, t.getToggleState() ? 1.0f : 0.0f); page.refreshControls(); };
        };
        toggle (reverse, P::FieldReverse, "Play the chop backwards");
        toggle (oneShot, P::FieldOneShot, "ONE-SHOT: the chop plays to its end even when you let go of the key");
        toggle (keepVoice, P::FieldFormantComp, "Keep the voice's character when it's pitched (only with STRETCH or SMOOTH)");
    }
    ChopPage& page;
    std::vector<std::unique_ptr<Row>> rows;
    juce::ComboBox stretch, loopMode, filter;
    juce::ToggleButton reverse { "REVERSE" }, oneShot { "ONE-SHOT" }, keepVoice { "KEEP VOICE" };
    bool syncing = false;
    juce::Rectangle<int> titleArea;

    void sync()
    {   // read the chop's settings back from the engine (unless a control is being dragged)
        auto& e = page.engine();
        const bool have = e.sliceCount() > 0 && page.selectedChop < e.sliceCount();
        juce::ScopedValueSetter<bool> sv (syncing, true);
        for (auto& r : rows) if (have && ! r->slider.isMouseButtonDown()) r->slider.setValue (r->fromEngine (e.sliceParam (page.selectedChop, r->field)), juce::dontSendNotification);
        const int algo = have ? (int) e.sliceParam (page.selectedChop, P::FieldAlgorithm) : 0;
        const bool filt = have && e.sliceParam (page.selectedChop, P::FieldFilterEnabled) > 0.5f;
        if (have)
        {
            stretch.setSelectedId (algo + 1, juce::dontSendNotification);
            loopMode.setSelectedId ((int) e.sliceParam (page.selectedChop, P::FieldLoop) + 1, juce::dontSendNotification);
            filter.setSelectedId (filt ? (int) e.sliceParam (page.selectedChop, P::FieldFilterType) + 2 : 1, juce::dontSendNotification);
            reverse.setToggleState (e.sliceParam (page.selectedChop, P::FieldReverse) > 0.5f, juce::dontSendNotification);
            oneShot.setToggleState (e.sliceParam (page.selectedChop, P::FieldOneShot) > 0.5f, juce::dontSendNotification);
            keepVoice.setToggleState (e.sliceParam (page.selectedChop, P::FieldFormantComp) > 0.5f, juce::dontSendNotification);
        }
        // greyed out when they'd do nothing (Brandon's rule)
        for (auto& r : rows) r->slider.setEnabled (have && ((r->field != P::FieldFilterCutoff && r->field != P::FieldFilterReso) || filt));
        for (juce::Component* c : { (juce::Component*) &stretch, (juce::Component*) &loopMode, (juce::Component*) &filter, (juce::Component*) &reverse, (juce::Component*) &oneShot })
            c->setEnabled (have);
        keepVoice.setEnabled (have && algo != 0);
        repaint();
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 8);
        titleArea = r.removeFromTop (34);
        for (auto& row : rows) { auto l = r.removeFromTop (24); l.removeFromLeft (64); l.removeFromRight (56); row->slider.setBounds (l); r.removeFromTop (2); }
        r.removeFromTop (4);
        auto c1 = r.removeFromTop (24); stretch.setBounds (c1.removeFromLeft (c1.getWidth() / 2 - 2)); c1.removeFromLeft (4); loopMode.setBounds (c1);
        r.removeFromTop (4); filter.setBounds (r.removeFromTop (24));
        r.removeFromTop (4);
        auto t1 = r.removeFromTop (22); reverse.setBounds (t1.removeFromLeft (t1.getWidth() / 2)); oneShot.setBounds (t1);
        keepVoice.setBounds (r.removeFromTop (22));
    }
    void paint (juce::Graphics& g) override
    {
        const auto& P_ = page.pal();
        panel (g, P_, getLocalBounds().toFloat(), 10);
        auto& e = page.engine();
        const auto sl = e.slices();
        juce::String title = "SOUND", sub;
        for (size_t i = 0; i < sl.size(); ++i)
            if (sl[i].index == page.selectedChop)
            {
                if (e.keys == chop::KeysMode::whole) { title = "SOUND - WHOLE CLIP"; sub = "every key, C3 = as it is"; }
                else { title = "SOUND - CHOP " + juce::String ((int) i + 1); sub = noteName (sl[i].note); if (e.labelFor (i).isNotEmpty()) sub << "  \"" << e.labelFor (i) << "\""; }
            }
        if (sl.empty()) sub = "load a clip";
        g.setColour (P_.ink); g.setFont (gfont (13.0f)); g.drawText (title, titleArea.withTrimmedBottom (16), juce::Justification::centredLeft, true);
        g.setColour (P_.dim); g.setFont (gfont (11.0f, false)); g.drawText (sub, titleArea.withTrimmedTop (18), juce::Justification::centredLeft, true);
        for (auto& r : rows)
        {
            const auto b = r->slider.getBounds();
            const float a = r->slider.isEnabled() ? 1.0f : 0.35f;
            g.setColour (P_.dim.withAlpha (a)); g.setFont (gfont (10.5f));
            g.drawText (r->name, juce::Rectangle<int> (10, b.getY(), 62, b.getHeight()), juce::Justification::centredLeft);
            g.setColour (P_.ink.withAlpha (a)); g.setFont (gfont (10.5f, false));
            g.drawText (r->text (r->slider.getValue()), juce::Rectangle<int> (b.getRight() + 2, b.getY(), 54, b.getHeight()), juce::Justification::centredRight);
        }
    }
};

// ================================================================================================ the page
juce::String ChopPage::toolName (Tool t)
{
    static const char* n[] { "SELECT", "SLICE", "DELETE", "ZOOM", "PLAYBACK" };
    return n[juce::jlimit (0, 4, (int) t)];
}
juce::String ChopPage::toolTip (Tool t)
{
    auto u = [] (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); };
    switch (t)
    {
        case Tool::select:   return u ("Drag to select part of the wave  ·  Shift+drag changes the selection  ·  double-click a chop to select it  ·  drag along the ruler to set the loop");
        case Tool::slice:    return u ("Click to cut a chop in two at that point (or to start chopping the clip there)");
        case Tool::erase:    return u ("Click a chop to delete it (the audio stays)");
        case Tool::zoom:     return u ("Click zooms in  ·  right-click zooms out  ·  Cmd+scroll or PgUp / PgDn zoom too");
        case Tool::playback: return u ("Click or drag over the wave to hear it from there");
        default: break;
    }
    return {};
}
juce::juce_wchar ChopPage::toolKey (Tool t) { static const char k[] { 'e', 'c', 'd', 'z', 'y' }; return k[juce::jlimit (0, 4, (int) t)]; }

chop::ChopEngine& ChopPage::engine() { return *proc.chopper; }

ChopPage::ChopPage (Processor& p, std::function<const ghosty::themes::Palette&()> pl, std::function<void()> lab)
    : proc (p), palette (std::move (pl)), openLab (std::move (lab))
{
    setWantsKeyboardFocus (true);
    setOpaque (true);
    wave = std::make_unique<Wave> (*this); addAndMakeVisible (*wave);
    keys = std::make_unique<Keys> (*this, keyState); addAndMakeVisible (*keys);
    settings = std::make_unique<Settings> (*this); addAndMakeVisible (*settings);
    keyState.addListener (this);
    for (int i = 0; i < (int) Tool::kTools; ++i) { tools.push_back (std::make_unique<ToolButton> (*this, (Tool) i)); addAndMakeVisible (*tools.back()); }
    dragOut = std::make_unique<DragOut> (*this); addAndMakeVisible (*dragOut);
    for (auto* b : { &playB, &loopB, &autoB, &minus, &plus, &keysChops, &keysWhole, &convertB, &addB, &recB, &installB, &undoB, &redoB, &cutB, &copyB, &pasteB, &delB, &trimB, &dupB, &clearB })
    { addAndMakeVisible (*b); b->setMouseCursor (juce::MouseCursor::PointingHandCursor); b->setWantsKeyboardFocus (false); }
    addAndMakeVisible (autoMode); autoMode.setWantsKeyboardFocus (false);
    for (int m = 0; m < (int) chop::AutoMode::kModes; ++m) autoMode.addItem (chop::autoModeName ((chop::AutoMode) m), m + 1);
    autoMode.setSelectedId ((int) engine().autoMode + 1, juce::dontSendNotification);
    autoMode.onChange = [this] { engine().autoMode = (chop::AutoMode) (autoMode.getSelectedId() - 1); autoMode.setTooltip (chop::autoModeTip (engine().autoMode)); refreshControls(); };
    installB.onClick = [this] { engine().installTool(); refreshControls(); };
    autoMode.setTooltip (chop::autoModeTip (engine().autoMode));

    playB.setTooltip ("Play / stop (SPACE). Plays the loop when LOOP is on, otherwise from the cursor");
    playB.onClick = [this] { togglePlay(); };
    loopB.setTooltip ("Loop on / off. Drag along the ruler to draw a loop  ·  Cmd+L loops the selection");
    loopB.onClick = [this]
    {
        if (loop.empty() && ! selection.empty()) loop = selection;
        if (loop.empty()) { say ("Drag along the ruler (or select some of the wave) to make a loop first"); return; }
        loopOn = ! loopOn;
        if (engine().isPlaying()) engine().play (loopOn ? loop.a : engine().playhead(), loop, loopOn);
        refreshControls(); repaint();
    };
    autoB.setTooltip ("AUTO CHOP (Cmd+U): chops the clip and puts the chops on the keys from C1. Press again for a new set");
    autoB.onClick = [this] { engine().autoChop(); refreshControls(); };
    minus.setTooltip ("Fewer chops"); plus.setTooltip ("More chops");
    minus.onClick = [this] { engine().chopCount = juce::jmax (1, engine().chopCount - (engine().chopCount > 16 ? 4 : 1)); repaint(); };
    plus.onClick = [this] { engine().chopCount = juce::jmin (64, engine().chopCount + (engine().chopCount >= 16 ? 4 : 1)); repaint(); };
    keysChops.setTooltip ("CHOPS: each chop on its own key, from C1");
    keysWhole.setTooltip ("WHOLE: the whole clip across the keyboard. C3 plays it as it is, other keys pitch it (the length and the voice's character are kept)");
    keysChops.onClick = [this] { engine().setKeys (chop::KeysMode::chops); refreshControls(); };
    keysWhole.onClick = [this] { engine().setKeys (chop::KeysMode::whole); refreshControls(); };
    convertB.setTooltip ("Sings the selection (or the whole clip) with your Voice C, then puts it back in the same place. Set up the voices in IDENTITY LAB");
    convertB.onClick = [this] { convertVoice(); };
    addB.setTooltip ("Load a WAV (or AIFF, FLAC, OGG, MP3). You can also drag one onto the page");
    addB.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Load a sound", juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [sp = juce::Component::SafePointer<ChopPage> (this)] (const juce::FileChooser& fc)
                              { if (sp != nullptr && fc.getResult().existsAsFile()) sp->loadFile (fc.getResult()); });
    };
    recB.setTooltip ("Record from the input into the clip. Press again to stop");
    recB.onClick = [this]
    {
        if (! recording) { proc.startRecording (slotPerf); recording = true; say ("Recording... press REC again to stop"); }
        else
        {
            const auto before = proc.takes[slotPerf];
            proc.stopRecording(); recording = false;
            if (proc.takes[slotPerf] != before && ! proc.takes[slotPerf].isEmpty()) loadFile (juce::File (proc.takes[slotPerf][proc.takes[slotPerf].size() - 1]));
            else say ("Nothing recorded (record at least half a second)");
        }
        refreshControls();
    };
    undoB.setTooltip ("Undo (Cmd+Z)"); redoB.setTooltip ("Redo (Cmd+Shift+Z)");
    cutB.setTooltip ("Cut the selection (Cmd+X)"); copyB.setTooltip ("Copy the selection (Cmd+C)");
    pasteB.setTooltip ("Paste at the cursor, or over the selection (Cmd+V)"); delB.setTooltip ("Delete the selected audio (Delete)");
    trimB.setTooltip ("Keep only the selection"); dupB.setTooltip ("Duplicate the selection right after it (Cmd+B)");
    clearB.setTooltip ("Remove all chops (the whole clip goes back on the keys)");
    undoB.onClick = [this] { engine().undo(); selection = {}; showAll(); };
    redoB.onClick = [this] { engine().redo(); selection = {}; showAll(); };
    cutB.onClick = [this] { if (engine().cut (selection)) { cursor = selection.a; selection = {}; showAll(); } };
    copyB.onClick = [this] { engine().copy (selection); repaint(); };
    pasteB.onClick = [this] { if (engine().paste (cursor, selection)) { selection = {}; showAll(); } };
    delB.onClick = [this] { if (engine().erase (selection)) { cursor = selection.a; selection = {}; showAll(); } };
    trimB.onClick = [this] { if (engine().trim (selection)) { selection = {}; loop = {}; loopOn = false; cursor = 0; showAll(); } };
    dupB.onClick = [this] { if (engine().duplicate (selection)) { const auto L = selection.length(); selection = { selection.b, selection.b + L }; showAll(); } };
    clearB.onClick = [this] { engine().clearChops(); refreshControls(); };

    engine().onChange = [sp = juce::Component::SafePointer<ChopPage> (this)] { if (sp != nullptr) { sp->refreshControls(); sp->repaint(); } };
    showAll();
    refreshControls();
    startTimerHz (30);
}

ChopPage::~ChopPage()
{
    stopTimer();
    keyState.removeListener (this);
    if (proc.chopper != nullptr) proc.chopper->onChange = nullptr;
}

void ChopPage::setTool (Tool t) { currentTool = t; for (auto& b : tools) b->repaint(); repaint (tipArea); }

void ChopPage::zoomBy (double factor, double around)
{
    const auto total = (double) juce::jmax ((juce::int64) 1, engine().frames());
    const double minFrames = juce::jmax (64.0, (double) getWidth() * 0.5);
    const double nv = juce::jlimit (minFrames, total, viewFrames * factor);
    const double rel = viewFrames > 0 ? (around - viewStart) / viewFrames : 0.5;
    viewStart = juce::jlimit (0.0, juce::jmax (0.0, total - nv), around - rel * nv);
    viewFrames = nv;
    repaint();
}

void ChopPage::showAll()
{
    viewStart = 0; viewFrames = (double) juce::jmax ((juce::int64) 1, engine().frames());
    if (selection.b > engine().frames()) selection = {};
    if (loop.b > engine().frames()) { loop = {}; loopOn = false; }
    cursor = juce::jlimit ((juce::int64) 0, engine().frames(), cursor);
    refreshControls(); repaint();
}

bool ChopPage::loadFile (const juce::File& f)
{
    juce::String why;
    if (! engine().loadFile (f, why)) { say (why); return false; }
    selection = {}; loop = {}; loopOn = false; cursor = 0;
    showAll();
    return true;
}

void ChopPage::togglePlay()
{
    auto& e = engine();
    if (! e.hasClip()) { say ("Drag a WAV in first"); return; }
    if (e.isPlaying()) e.stop();
    else if (loopOn && ! loop.empty()) e.play (loop.a, loop, true);
    else e.play (! selection.empty() ? selection.a : cursor, {}, false);
    refreshControls(); repaint();
}

void ChopPage::convertVoice()
{
    auto& e = engine();
    if (! e.hasClip()) { say ("Drag a WAV in first"); return; }
    if (conv.active) { say ("Already converting..."); return; }
    const chop::Region r = selection.empty() ? chop::Region { 0, e.frames() } : selection;
    const auto f = e.writeRegion (r, "convert");
    if (! f.existsAsFile()) { say ("Select a longer part (at least a few milliseconds)"); return; }
    proc.takes[slotPerf] = juce::StringArray { f.getFullPathName() };
    if (const auto why = proc.whyNotSong(); why.isNotEmpty())
    { say ("Can't convert yet: " + uiWords (why) + "  Set up your voices in IDENTITY LAB."); if (openLab && proc.takes[slotA].isEmpty()) openLab(); return; }
    conv = Conversion(); conv.before = proc.result; conv.r = r;
    if (! proc.renderSong()) { say ("Couldn't start the conversion: " + uiWords (proc.lastError)); return; }
    conv.active = true;
    say ("Converting " + secs ((double) r.length() / e.rate()) + " with " + proc.voiceName + "...");
    refreshControls();
}

void ChopPage::timerCallback()
{
    if (conv.active && ! proc.busy())
    {
        conv.active = false;
        if (proc.result.existsAsFile() && proc.result != conv.before)
        {
            std::vector<float> x; int fs = 0; juce::String err;
            if (readMono (proc.result, x, fs, err))
            {
                juce::AudioBuffer<float> b (1, (int) x.size()); b.copyFrom (0, 0, x.data(), (int) x.size());
                engine().replaceRegion (conv.r, b, fs);
                selection = { conv.r.a, juce::jmin (engine().frames(), conv.r.a + (juce::int64) std::llround ((double) x.size() * engine().rate() / fs)) };
                say ("Converted with " + proc.voiceName + ". Cmd+Z puts the original back");
                showAll();
            }
            else say ("The conversion came back unreadable: " + err);
        }
        else say ("The conversion didn't finish: " + uiWords (proc.lastError.isNotEmpty() ? proc.lastError : juce::String ("cancelled")));
        refreshControls();
    }
    if (recording && proc.recordingSlot() < 0) { recording = false; refreshControls(); }
    if (engine().isPlaying() || conv.active || recording || engine().analysing() || engine().toolBusy()) { wave->repaint(); repaint (statusArea); repaint (infoArea); }
    if (toolWasBusy != engine().toolBusy()) { toolWasBusy = engine().toolBusy(); refreshControls(); }
    if ((ticks % 10) == 0 && settings != nullptr) settings->sync();
    if ((++ticks % 4) == 0) keys->repaint();
}

void ChopPage::refreshControls()
{
    auto& e = engine();
    const bool clip = e.hasClip(), sel = ! selection.empty();
    playB.setButtonText (e.isPlaying() ? "STOP" : "PLAY");
    loopB.setToggleState (loopOn, juce::dontSendNotification);
    loopB.setColour (juce::TextButton::buttonOnColourId, bsGreen.darker (0.3f));
    loopB.setClickingTogglesState (false);
    keysChops.setToggleState (e.keys == chop::KeysMode::chops, juce::dontSendNotification);
    keysWhole.setToggleState (e.keys == chop::KeysMode::whole, juce::dontSendNotification);
    recB.setButtonText (recording ? "STOP REC" : "REC");
    recB.setToggleState (recording, juce::dontSendNotification);
    // greyed out when they'd do nothing (Brandon's rule for every GhostBlock plug-in)
    for (auto* b : { &playB, &autoB, &keysChops, &keysWhole, &convertB }) b->setEnabled (clip);
    for (auto* b : { &cutB, &copyB, &delB, &trimB, &dupB }) b->setEnabled (clip && sel);
    pasteB.setEnabled (clip && e.hasClipboard());
    undoB.setEnabled (e.canUndo()); redoB.setEnabled (e.canRedo());
    clearB.setEnabled (! e.chops.empty());
    loopB.setEnabled (clip && (! loop.empty() || sel));
    const bool counted = ! chop::usesTool (e.autoMode);   // PHRASES / WORDS take what they find: no count
    minus.setEnabled (clip && counted && e.chopCount > 1); plus.setEnabled (clip && counted && e.chopCount < 64);
    const bool needInstall = ! e.toolReady();
    installB.setVisible (needInstall);
    installB.setEnabled (! e.toolBusy());
    installB.setButtonText (e.toolBusy() ? "INSTALLING..." : e.autoMode == chop::AutoMode::words ? "INSTALL WORD FINDER (1.5 GB)" : "INSTALL PHRASE FINDER");
    installB.setTooltip (e.autoMode == chop::AutoMode::words ? "Downloads the word finder and its models (about 1.5 GB, once) into Ghost Voice's own folder"
                                                             : "Downloads a small private Python (about 60 MB, once) into Ghost Voice's own folder");
    autoB.setEnabled (clip && ! needInstall && ! e.toolBusy());
    if (e.keys == chop::KeysMode::whole || selectedChop >= e.sliceCount()) selectedChop = 0;
    if (settings != nullptr) settings->sync();
    resized();
    autoMode.setEnabled (clip);
    convertB.setEnabled (clip && ! conv.active);
    convertB.setButtonText (conv.active ? "CONVERTING..." : "CONVERT VOICE");
    dragOut->repaint();
    repaint();
}

juce::String ChopPage::status() const
{
    auto& e = const_cast<ChopPage*> (this)->engine();
    if (conv.active) return uiWords (proc.progressText());
    if (recording) return "Recording " + secs (proc.recordedSeconds());
    if (note.isNotEmpty() && juce::Time::getMillisecondCounterHiRes() - noteTime < 6000) return note;
    if (e.toolBusy()) return e.tools.progress().isNotEmpty() ? uiWords (e.tools.progress()) : juce::String ("Working...");
    if (e.analysing()) return "Listening to the clip (tempo, key, hits)...";
    return e.message;
}

void ChopPage::paint (juce::Graphics& g)
{
    const auto& P = pal();
    splash::backdrop (g, getLocalBounds(), getPosition());   // its part of the window's painting
    {   // a milky deck under the two tool rows and the tip line, so every word on them reads on a calm ground
        juce::Path deck; deck.addRoundedRectangle (deckArea.toFloat(), 16.0f);
        g.setColour (P.shadow); g.fillPath (deck, juce::AffineTransform::translation (0, 3));
        g.setColour (P.panel.withAlpha (0.94f)); g.fillPath (deck);
        splash::sketch (g, deck, 1313, 6, 0.75f, 0.42f);
    }
    auto& e = engine();
    // info: the clip, what the analysis found
    g.setFont (gfont (12.5f, false));
    if (auto c = e.clip())
    {
        g.setColour (P.ink);
        const auto det = e.detected();
        juce::String s = c->name + "   " + secs (c->seconds());
        if (det.ready) s << "   " << det.summary();
        else if (e.analysing()) s << "   listening...";
        g.drawText (s, infoArea, juce::Justification::centredRight, true);
    }
    // the chop count between - and +
    g.setColour (P.ink); g.setFont (gfont (14.0f)); g.drawText (juce::String (e.chopCount), countArea, juce::Justification::centred);
    g.setColour (P.dim); g.setFont (gfont (10.0f)); g.drawText ("KEYS", keysLabelArea, juce::Justification::centredRight);
    // Block Step's tip line: the tool and what it does
    {
        auto t = tipArea.toFloat();
        g.setColour (bsPurple); g.setFont (gfont (12.0f)); const auto n = toolName (currentTool);
        g.drawText (n, t, juce::Justification::centredLeft);
        g.setColour (P.dim); g.setFont (gfont (12.0f, false));
        g.drawText (toolTip (currentTool), t.withTrimmedLeft (ghosty::textWidth (gfont (12.0f), n) + 8), juce::Justification::centredLeft, true);
    }
    // the status line
    g.setColour (P.panel); g.fillRect (statusArea);
    { juce::Path rule; rule.startNewSubPath ((float) statusArea.getX(), statusArea.getY() + 0.5f); rule.lineTo ((float) statusArea.getRight(), statusArea.getY() + 0.5f); splash::sketch (g, rule, 9, 3, 0.7f, 0.5f); }
    statusArea.removeFromTop (1);
    g.setColour (P.ink); g.setFont (gfont (12.5f, false));
    g.drawText (status(), statusArea.reduced (16, 0), juce::Justification::centredLeft, true);
    if (conv.active)
    { g.setColour (bsGreen); g.fillRect (statusArea.getX(), statusArea.getBottom() - 3, (int) ((float) statusArea.getWidth() * juce::jlimit (0.0f, 1.0f, proc.progressValue())), 3); }
}

void ChopPage::resized()
{
    auto r = getLocalBounds();
    statusArea = r.removeFromBottom (26);
    r = r.reduced (16, 10);
    {   // row 1: transport, tools, AUTO CHOP, KEYS, CONVERT VOICE, load / record / drag out
        auto row = r.removeFromTop (34);
        playB.setBounds (row.removeFromLeft (70)); row.removeFromLeft (6);
        loopB.setBounds (row.removeFromLeft (62)); row.removeFromLeft (14);
        for (auto& t : tools) { t->setBounds (row.removeFromLeft (86)); row.removeFromLeft (3); }
        row.removeFromLeft (11);
        autoB.setBounds (row.removeFromLeft (96)); row.removeFromLeft (4);
        autoMode.setBounds (row.removeFromLeft (128).reduced (0, 2)); row.removeFromLeft (4);
        minus.setBounds (row.removeFromLeft (26)); countArea = row.removeFromLeft (30); plus.setBounds (row.removeFromLeft (26));
        dragOut->setBounds (row.removeFromRight (92)); row.removeFromRight (6);
        recB.setBounds (row.removeFromRight (78)); row.removeFromRight (6);
        addB.setBounds (row.removeFromRight (82));
    }
    r.removeFromTop (8);
    {   // row 2: KEYS, CONVERT VOICE, the edits, the clip info
        auto row = r.removeFromTop (30);
        keysLabelArea = row.removeFromLeft (34); row.removeFromLeft (6);
        keysChops.setBounds (row.removeFromLeft (70)); row.removeFromLeft (3); keysWhole.setBounds (row.removeFromLeft (70)); row.removeFromLeft (14);
        convertB.setBounds (row.removeFromLeft (132)); row.removeFromLeft (14);
        for (auto* b : { &undoB, &redoB, &cutB, &copyB, &pasteB, &delB, &trimB, &dupB, &clearB })
        { b->setBounds (row.removeFromLeft (b == &clearB ? 96 : b == &dupB ? 84 : 58)); row.removeFromLeft (3); }
        if (installB.isVisible()) { installB.setBounds (row.removeFromLeft (juce::jmin (232, row.getWidth()))); row.removeFromLeft (6); }
        infoArea = row.withTrimmedLeft (8);
    }
    r.removeFromTop (4);
    tipArea = r.removeFromTop (22);
    deckArea = getLocalBounds().reduced (6, 0).withTop (2).withBottom (tipArea.getBottom() + 4);
    r.removeFromTop (4);
    keys->setBounds (r.removeFromBottom (92));
    keys->setKeyWidth (juce::jmax (14.0f, (float) keys->getWidth() / 52.0f));
    r.removeFromBottom (8);
    settings->setBounds (r.removeFromRight (250)); r.removeFromRight (8);
    wave->setBounds (r);
}

bool ChopPage::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files) if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3")) return true;
    return false;
}

void ChopPage::filesDropped (const juce::StringArray& files, int, int)
{
    for (auto& f : files) if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3")) { loadFile (juce::File (f)); return; }
}

void ChopPage::visibilityChanged() { if (isShowing()) { grabKeyboardFocus(); refreshControls(); } }

void ChopPage::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (! engine().hasClip() && wave->getBounds().contains (e.getPosition())) addB.triggerClick();
}

void ChopPage::handleNoteOn (juce::MidiKeyboardState*, int, int n, float v)
{
    engine().noteOn (n, v);
    juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<ChopPage> (this), n]
    { if (sp != nullptr) for (auto& s : sp->engine().slices()) if (n >= s.note && n <= s.highNote) { sp->selectChop (s.index); break; } });
}

void ChopPage::selectChop (int i)
{
    selectedChop = juce::jmax (0, i);
    if (settings != nullptr) settings->sync();
    repaint();
}
void ChopPage::handleNoteOff (juce::MidiKeyboardState*, int, int n, float) { engine().noteOff (n); }

bool ChopPage::keyPressed (const juce::KeyPress& k)
{
    auto& e = engine();
    const auto mods = k.getModifiers();
    const bool cmd = mods.isCommandDown() || mods.isCtrlDown(), sh = mods.isShiftDown();
    const int code = k.getKeyCode();
    const auto ch = juce::CharacterFunctions::toLowerCase (k.getTextCharacter() != 0 ? k.getTextCharacter() : (juce::juce_wchar) code);
    if (code == juce::KeyPress::spaceKey) { togglePlay(); return true; }
    if (code == juce::KeyPress::escapeKey) { selection = {}; repaint(); return true; }
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) { delB.triggerClick(); return true; }
    if (code == juce::KeyPress::pageUpKey) { zoomBy (0.8, (double) cursor); return true; }
    if (code == juce::KeyPress::pageDownKey) { zoomBy (1.25, (double) cursor); return true; }
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
    {   // nudge the cursor (Shift: by a beat when the tempo is known)
        const auto det = e.detected();
        const double step = sh && det.ready && det.bpm > 0 ? 60.0 / det.bpm * e.rate() : juce::jmax (1.0, viewFrames / 100.0);
        cursor = juce::jlimit ((juce::int64) 0, e.frames(), cursor + (juce::int64) ((code == juce::KeyPress::leftKey ? -1 : 1) * step));
        repaint(); return true;
    }
    if (cmd)
    {
        switch (ch)
        {
            case 'z': (sh || mods.isAltDown()) ? redoB.triggerClick() : undoB.triggerClick(); return true;
            case 'y': redoB.triggerClick(); return true;
            case 'a': if (e.hasClip()) { selection = { 0, e.frames() }; refreshControls(); } return true;
            case 'd': selection = {}; refreshControls(); return true;
            case 'c': copyB.triggerClick(); return true;
            case 'x': cutB.triggerClick(); return true;
            case 'v': pasteB.triggerClick(); return true;
            case 'b': dupB.triggerClick(); return true;
            case 'u': autoB.triggerClick(); return true;
            case 'l': if (! selection.empty()) { loop = selection; loopOn = true; if (e.isPlaying()) e.play (loop.a, loop, true); refreshControls(); } return true;
            default: break;
        }
        return false;
    }
    for (int t = 0; t < (int) Tool::kTools; ++t) if (ch == toolKey ((Tool) t) && ! mods.isAnyModifierKeyDown()) { setTool ((Tool) t); return true; }
    return false;
}
}
