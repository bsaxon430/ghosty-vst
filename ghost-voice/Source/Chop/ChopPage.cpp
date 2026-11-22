// Ghost Voice - AGPL-3.0-only (see LICENSE, NOTICE.md).
//
// CHOP in THE GHOST CHOIR look: the clip is a long stained-glass window (each chop a pane of coloured glass, the wave the light
// through it), the choir stalls stand over the keys (one hooded singer per chop, on its own key: it lights up and sings when its
// key sounds), and SOUND is the chop engine's own signal chain (TIME/PITCH, FILTER, AMP, PLAYBACK) under its own control names.
// Static art is cached in Images (the page's stone and frames, the window's glass and wave, the stalls); a repaint draws the cache
// and only what moves (the playhead, the selection, the singing voices).
#include "ChopPage.h"
#include "Processor.h"
#include "src/PluginProcessor.h"   // the chop engine's per-slice fields
#include <cmath>

namespace gv
{
namespace
{
juce::String noteName (int n) { return juce::MidiMessage::getMidiNoteName (n, true, true, 3); }   // C3 = 60, as in FL Studio
juce::String secs (double s) { return s >= 60 ? juce::String ((int) (s / 60)) + ":" + juce::String (std::fmod (s, 60.0), 2).paddedLeft ('0', 5) : juce::String (s, 2) + " s"; }
using choir::serif; using choir::display;
juce::String slicesKey (chop::ChopEngine& e)
{   // what the chops look like now (for the caches)
    juce::String k; k << (int) e.keys << ":";
    const auto sl = e.slices();
    for (size_t i = 0; i < sl.size(); ++i) k << (int) sl[i].r.a << "-" << (int) sl[i].r.b << "@" << sl[i].note << e.labelFor (i) << ";";
    return k;
}
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
        const auto& P = page.cp();
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        const bool on = page.tool() == tool;
        if (on) { juce::ColourGradient gr (P.glass[4], r.getX(), r.getY(), P.glass[1], r.getRight(), r.getBottom(), false); g.setGradientFill (gr); }
        else { juce::ColourGradient gr (P.card, 0, r.getY(), P.cell, 0, r.getBottom(), false); g.setGradientFill (gr); }
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (over ? P.goldHi : P.edge); g.drawRoundedRectangle (r, 4.0f, over ? 1.4f : 1.0f);
        auto k = r.removeFromRight (20).reduced (3, 8);
        g.setColour (on ? juce::Colours::white : P.ink);
        const auto n = toolName (tool);
        g.setFont (choir::fitted (choir::bold, n, r.getWidth() - 6, 13.5f, 9.0f)); g.drawText (n, r.withTrimmedLeft (2), juce::Justification::centred, false);
        g.setColour ((on ? juce::Colours::white : P.gold).withAlpha (on ? 0.3f : 0.22f)); g.fillRoundedRectangle (k, 3);
        g.setColour (on ? juce::Colours::white : P.gold); g.setFont (serif (12.0f, choir::bold));
        g.drawText (juce::String::charToString (toolKey (tool)).toUpperCase(), k, juce::Justification::centred, false);
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
        const auto& P = page.cp(); auto r = getLocalBounds().toFloat().reduced (0.5f);
        const bool ok = page.engine().hasClip(); const float a = ok ? 1.0f : 0.38f;
        juce::ColourGradient gr (P.card.withMultipliedAlpha (a), 0, r.getY(), P.cell.withMultipliedAlpha (a), 0, r.getBottom(), false);
        g.setGradientFill (gr); g.fillRoundedRectangle (r, 4.0f);
        g.setColour ((ok ? P.edge : P.faint).withMultipliedAlpha (a)); g.drawRoundedRectangle (r, 4.0f, 1.0f);
        // a little arrow leaving the box
        juce::Path ar; const float ax = r.getX() + 13, ay = r.getCentreY();
        ar.startNewSubPath (ax - 5, ay); ar.lineTo (ax + 5, ay); ar.startNewSubPath (ax + 1, ay - 4); ar.lineTo (ax + 5, ay); ar.lineTo (ax + 1, ay + 4);
        g.setColour (P.gold.withMultipliedAlpha (a)); g.strokePath (ar, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (choir::greyInk (P, P.ink, ok ? 1.0f : choir::greyA (P)));
        g.setFont (choir::fitted (choir::bold, "DRAG WAV", r.getWidth() - 28, 14.0f, 9.0f));
        g.drawText ("DRAG WAV", r.withTrimmedLeft (22), juce::Justification::centred, false);
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

// ================================================================================================ the wave: a stained-glass window
struct ChopPage::Wave : public juce::Component
{
    explicit Wave (ChopPage& p) : page (p) { setWantsKeyboardFocus (false); setOpaque (true); }
    ChopPage& page;
    static constexpr int rulerH = 24, overviewH = 16, bin = 256;
    std::vector<float> peaksMin, peaksMax; const void* peaksFor = nullptr;
    enum class Drag { none, select, loopRuler, overview, playback } drag = Drag::none;
    juce::int64 dragFrom = 0;
    choir::Cache glass;   // the frame, the ruler, the panes and the wave (everything that only changes with the view / the chops)

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

    void paintGlass (juce::Graphics& g, const chop::Clip& c)
    {   // the static window: ground, ruler + grid, the chops' panes with lead between them, and the wave as light
        const auto& P = page.cp();
        auto& eng = page.engine();
        const auto all = getLocalBounds().toFloat(), wa = waveArea().toFloat(), ra = rulerArea().toFloat();
        g.setColour (P.waveBg); g.fillRect (all);
        juce::ColourGradient gl (P.glass[1].withAlpha (P.dark ? 0.10f : 0.06f), 0, wa.getY(), P.glass[4].withAlpha (P.dark ? 0.06f : 0.04f), 0, wa.getBottom(), false);
        g.setGradientFill (gl); g.fillRect (wa);
        // the ruler: a gilded strip with bar numbers
        g.setColour (P.cell); g.fillRect (ra);
        g.setColour (P.edgeDim); g.fillRect (ra.getX(), ra.getBottom() - 1, ra.getWidth(), 1.0f);
        const auto det = eng.detected();
        const double beat = det.ready && det.bpm > 0 ? 60.0 / det.bpm * c.rate : 0.0;
        g.setFont (serif (13.0f, choir::bold));
        if (beat > 0)
        {
            const double first = det.firstBeat * c.rate;
            int every = 1; while (beat * every / spp() < 18) every *= 2;
            const auto k0 = (juce::int64) std::floor ((page.viewStart - first) / beat), k1 = (juce::int64) std::ceil ((page.viewStart + page.viewFrames - first) / beat);
            for (auto k = k0; k <= k1; ++k)
            {
                if (k % every != 0) continue;
                const float x = xOf (first + (double) k * beat); if (x < 0 || x > (float) getWidth()) continue;
                const bool bar = ((k % 4) + 4) % 4 == 0;
                g.setColour ((P.dark ? juce::Colours::white : P.lead).withAlpha (bar ? 0.13f : 0.05f)); g.fillRect (x, wa.getY(), 1.0f, wa.getHeight());
                g.setColour (P.edge.withAlpha (bar ? 0.9f : 0.4f)); g.fillRect (x, ra.getBottom() - (bar ? 8.0f : 4.0f), 1.0f, bar ? 8.0f : 4.0f);
                if (bar && k >= 0) { g.setColour (P.gold); g.drawText (juce::String (k / 4 + 1), juce::Rectangle<float> (x + 4, ra.getY(), 40, ra.getHeight() - 2), juce::Justification::centredLeft, false); }
            }
        }
        else
        {
            double step = 0.1; while (step * c.rate / spp() < 60) step *= 2;
            for (double t = std::floor (page.viewStart / c.rate / step) * step; t * c.rate < page.viewStart + page.viewFrames; t += step)
            {
                const float x = xOf (t * c.rate); if (x < 0) continue;
                g.setColour ((P.dark ? juce::Colours::white : P.lead).withAlpha (0.06f)); g.fillRect (x, wa.getY(), 1.0f, wa.getHeight());
                g.setColour (P.gold); g.drawText (juce::String (t, step < 1 ? 1 : 0) + "s", juce::Rectangle<float> (x + 4, ra.getY(), 50, ra.getHeight() - 2), juce::Justification::centredLeft, false);
            }
        }
        // the panes: one piece of coloured glass per chop (per key), leaded at its edges, with its note on a little plaque
        const auto sl = eng.slices();
        if (eng.keys == chop::KeysMode::chops)
        {
            for (size_t i = 0; i < sl.size(); ++i)
            {
                const auto& s = sl[i];
                const float x0 = xOf ((double) s.r.a), x1 = xOf ((double) s.r.b);
                if (x1 < 0 || x0 > (float) getWidth()) continue;
                const auto cc = P.chop ((int) i);
                juce::ColourGradient pg (cc.withAlpha (P.dark ? 0.34f : 0.26f), 0, wa.getY(), cc.withAlpha (P.dark ? 0.12f : 0.08f), 0, wa.getBottom(), false);
                g.setGradientFill (pg); g.fillRect (x0, wa.getY(), juce::jmax (1.0f, x1 - x0), wa.getHeight());
                g.setColour (cc.withAlpha (0.35f)); g.fillRect (x0 + 3, wa.getY() + 2, juce::jmax (0.0f, x1 - x0 - 6), 2.0f);   // the glint on the glass
                g.setColour (P.lead.withAlpha (P.dark ? 0.95f : 0.7f)); g.fillRect (x0 - 1.0f, wa.getY(), 3.0f, wa.getHeight());
            }
        }
        // the wave: the light through the glass
        {
            const float mid = wa.getCentreY() + 10.0f, h = (wa.getHeight() - 30.0f) * 0.44f;
            juce::Path p;
            const int W = getWidth();
            std::vector<float> lo ((size_t) W), hi ((size_t) W);
            for (int x = 0; x < W; ++x)
            {
                float mn, mx; minMax (c, page.viewStart + x * spp(), page.viewStart + (x + 1) * spp(), mn, mx);
                lo[(size_t) x] = mid - mx * h; hi[(size_t) x] = mid - mn * h;
            }
            p.startNewSubPath (0.0f, lo[0]);
            for (int x = 1; x < W; ++x) p.lineTo ((float) x, lo[(size_t) x]);
            for (int x = W - 1; x >= 0; --x) p.lineTo ((float) x, juce::jmax (hi[(size_t) x], lo[(size_t) x] + 1.0f));
            p.closeSubPath();
            if (P.dark) { g.setColour (P.glow.withAlpha (0.10f)); g.strokePath (p, juce::PathStrokeType (5.0f)); }
            juce::ColourGradient wg (P.waveInk, 0, mid - h, P.waveInk.withMultipliedAlpha (P.dark ? 0.75f : 0.85f), 0, mid, true);
            g.setGradientFill (wg); g.fillPath (p);
            g.setColour ((P.dark ? juce::Colours::white : P.lead).withAlpha (0.10f)); g.fillRect (wa.getX(), mid, wa.getWidth(), 1.0f);
        }
        // (polish 3) the beats the count left out: the last chop runs on over them, so that stretch is veiled and says so
        if (const auto cap = page.beatCap(); cap.on)
        {
            const float x0 = juce::jmax (0.0f, xOf ((double) cap.tailFrom)), x1 = juce::jmin ((float) getWidth(), xOf ((double) cap.tailTo));
            if (x1 - x0 > 2.0f)
            {
                const auto veil = juce::Rectangle<float> (x0, wa.getY(), x1 - x0, wa.getHeight());
                g.setColour (P.waveBg.withAlpha (P.dark ? 0.62f : 0.58f)); g.fillRect (veil);
                {   // fine diagonal hatching
                    juce::Graphics::ScopedSaveState ss (g);
                    g.reduceClipRegion (veil.toNearestInt());
                    g.setColour ((P.dark ? juce::Colours::white : P.lead).withAlpha (P.dark ? 0.07f : 0.10f));
                    for (float x = x0 - wa.getHeight(); x < x1; x += 9.0f) g.drawLine (x, wa.getBottom(), x + wa.getHeight(), wa.getY(), 1.0f);
                }
                g.setColour (P.edge.withAlpha (0.9f)); g.fillRect (x0, wa.getY(), 1.5f, wa.getHeight());
                const juce::String t1 = "not chopped", t2 = juce::String (cap.unit.substring (0, 1).toUpperCase() + cap.unit.substring (1)) + " " + juce::String (cap.shown + 1) + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")) + juce::String (cap.avail) + " ride on the last chop";
                auto lr = veil.reduced (10, 0).withTrimmedTop (34).withHeight (40);
                g.setColour (P.ink); g.setFont (choir::fitted (choir::bold, t1, lr.getWidth(), 15.0f, 10.0f)); g.drawText (t1, lr.removeFromTop (18), juce::Justification::centredLeft, false);
                g.setColour (P.dim); g.setFont (choir::fitted (choir::italic, t2, lr.getWidth(), 14.0f, 10.0f)); g.drawText (t2, lr.removeFromTop (18), juce::Justification::centredLeft, false);
            }
        }
        // (polish 2) the chops' name chips, drawn over the wave on one top row: a solid chip in the pane's glass with a 1 px outline.
        // A chop that starts off the left edge keeps its chip at the edge, so a zoomed view still says which chop it is
        if (eng.keys == chop::KeysMode::chops)
        {
            const auto tf = serif (13.0f, choir::bold);
            for (size_t i = 0; i < sl.size(); ++i)
            {
                const auto& s = sl[i];
                const float x0 = xOf ((double) s.r.a), x1 = juce::jmin ((float) getWidth() - 2.0f, xOf ((double) s.r.b));
                const float cx = juce::jmax (x0 + 4.0f, 4.0f);
                if (x1 - cx < 20.0f) continue;
                juce::String txt = noteName (s.note); if (eng.labelFor (i).isNotEmpty()) txt << "  " << eng.labelFor (i);
                const float tw = juce::jmin (choir::width (tf, txt) + 12.0f, x1 - cx - 4.0f);
                auto tag = juce::Rectangle<float> (cx, wa.getY() + 6, tw, 18);
                const auto cc = P.chop ((int) i);
                g.setColour (P.dark ? cc.darker (0.15f) : cc); g.fillRoundedRectangle (tag, 3);
                g.setColour (P.dark ? cc.brighter (0.6f) : P.lead); g.drawRoundedRectangle (tag.reduced (0.5f), 3, 1.0f);
                g.setColour (juce::Colours::white); g.setFont (tf); g.drawText (txt, tag.reduced (5, 0), juce::Justification::centredLeft, true);
            }
        }
        // the frame: a gilded lead border
        g.setColour (P.edge); g.drawRect (all, 1.0f);
        g.setColour (P.lead.withAlpha (0.6f)); g.drawRect (all.reduced (1), 1.0f);
    }

    void paintEmpty (juce::Graphics& g)
    {
        const auto& P = page.cp();
        const auto all = getLocalBounds().toFloat();
        g.setColour (P.waveBg); g.fillRect (all);
        // an empty window: tall lancets of dim glass, and one singer waiting with a shut book
        const int n = 9; const float lw = all.getWidth() / (float) n;
        for (int i = 0; i < n; ++i)
        {
            auto l = juce::Rectangle<float> (all.getX() + lw * (float) i + 8, all.getY() + 14, lw - 16, all.getHeight() - 28);
            auto lp = choir::lancet (l, 0.30f);
            g.setColour (P.glass[(i * 3) % 8].withAlpha (P.dark ? 0.07f : 0.06f)); g.fillPath (lp);
            g.setColour (P.edgeDim.withMultipliedAlpha (0.6f)); g.strokePath (lp, juce::PathStrokeType (1.0f));
        }
        // (fixes) the singer and the two lines as one block, centred in whatever height the window has
        const float sw = juce::jlimit (28.0f, 46.0f, (all.getHeight() - 16.0f - 62.0f) / 1.4f), block = sw * 1.4f + 62.0f;
        const float top = all.getY() + juce::jmax (4.0f, (all.getHeight() - block) * 0.5f);
        choir::singer (g, P, juce::Rectangle<float> (all.getCentreX() - sw * 0.5f, top, sw, sw * 1.4f), P.glass[4], 0.0f, 0.0f, 0.9f);
        g.setColour (P.ink); g.setFont (display (28.0f));
        g.drawText ("Drag a WAV in here", all.withTop (top + sw * 1.4f + 2.0f).withHeight (36), juce::Justification::centred, false);
        g.setColour (P.dim); g.setFont (serif (17.0f, choir::italic));
        g.drawText ("or press ADD WAV, or REC to record.  Then AUTO CHOP, and play it on the keys.", all.withTop (top + sw * 1.4f + 38.0f).withHeight (24), juce::Justification::centred, false);
        g.setColour (P.edge); g.drawRect (all, 1.0f);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& P = page.cp();
        auto& eng = page.engine();
        auto c = eng.clip();
        if (c == nullptr || c->frames() == 0) { glass.draw (g, "empty" + juce::String ((int) P.dark), getLocalBounds(), [this] (juce::Graphics& ig, juce::Rectangle<int>) { paintEmpty (ig); }, true); return; }
        if (peaksFor != c.get()) buildPeaks (*c);
        juce::String key; key << (juce::pointer_sized_int) c.get() << "|" << page.viewStart << "|" << page.viewFrames << "|" << (int) P.dark << "|" << (eng.detected().ready ? eng.detected().bpm : 0.0) << "|" << slicesKey (eng) << "|" << (int) eng.autoMode << "|" << eng.chopCount;
        glass.draw (g, key, getLocalBounds(), [this, &c] (juce::Graphics& ig, juce::Rectangle<int>) { paintGlass (ig, *c); }, true);
        const auto wa = waveArea().toFloat(), ra = rulerArea().toFloat();
        // the singing panes glow, the picked one is outlined in gold
        if (eng.keys == chop::KeysMode::chops)
        {
            const auto sl = eng.slices();
            for (size_t i = 0; i < sl.size(); ++i)
            {
                const auto& s = sl[i];
                const float x0 = xOf ((double) s.r.a), x1 = xOf ((double) s.r.b);
                if (x1 < 0 || x0 > (float) getWidth()) continue;
                if (eng.noteSounding (s.note) || s.note == page.playingNote()) { g.setColour (P.chop ((int) i).withAlpha (0.30f)); g.fillRect (x0, wa.getY(), juce::jmax (1.0f, x1 - x0), wa.getHeight()); }
                if (s.index == page.selectedChop) { g.setColour (P.gold); g.drawRect (juce::Rectangle<float> (x0 + 1, wa.getY() + 1, juce::jmax (3.0f, x1 - x0 - 1), wa.getHeight() - 2), 2.0f); }
            }
        }
        // the loop (drawn on the ruler, like Block Step's step numbers)
        if (! page.loop.empty())
        {
            const float x0 = juce::jmax (0.0f, xOf ((double) page.loop.a)), x1 = juce::jmin ((float) getWidth(), xOf ((double) page.loop.b));
            if (x1 > x0)
            {
                const auto lc = page.loopOn ? P.good : P.faint;
                g.setColour (lc.withAlpha (page.loopOn ? 0.35f : 0.2f)); g.fillRect (x0, ra.getY() + 3, x1 - x0, ra.getHeight() - 6);
                g.setColour (lc); g.fillRect (x0, ra.getY() + 3, 2.0f, ra.getHeight() - 6); g.fillRect (x1 - 2, ra.getY() + 3, 2.0f, ra.getHeight() - 6);
                if (page.loopOn) { g.setColour (P.good.withAlpha (0.06f)); g.fillRect (x0, wa.getY(), x1 - x0, wa.getHeight()); }
            }
        }
        // the selection
        if (! page.selection.empty())
        {
            const float x0 = xOf ((double) page.selection.a), x1 = xOf ((double) page.selection.b);
            g.setColour (P.select.withAlpha (0.18f)); g.fillRect (x0, wa.getY(), x1 - x0, wa.getHeight());
            g.setColour (P.select); g.fillRect (x0, wa.getY(), 1.5f, wa.getHeight()); g.fillRect (x1 - 1.5f, wa.getY(), 1.5f, wa.getHeight());
            const auto t = secs ((double) page.selection.length() / c->rate);
            auto lab = juce::Rectangle<float> (x0 + 4, wa.getBottom() - 22, choir::width (serif (13.0f, choir::bold), t) + 10, 18);
            g.setColour (P.waveBg.withAlpha (0.85f)); g.fillRoundedRectangle (lab, 3);
            g.setFont (serif (13.0f, choir::bold)); g.setColour (P.ink); g.drawText (t, lab, juce::Justification::centred, false);
        }
        // the cursor + the playhead (a candle flame on a thread of light)
        { const float x = xOf ((double) page.cursor); g.setColour (P.select); g.fillRect (x, ra.getY(), 1.0f, wa.getBottom() - ra.getY()); }
        if (eng.isPlaying())
        {
            const float x = xOf ((double) eng.playhead());
            g.setColour (P.play.withAlpha (0.25f)); g.fillRect (x - 3.0f, wa.getY(), 6.0f, wa.getHeight());
            g.setColour (P.play); g.fillRect (x - 0.75f, ra.getY(), 1.5f, wa.getBottom() - ra.getY());
            choir::diamond (g, { x, ra.getY() + 6 }, 5.0f, P.play);
        }
        // the overview (what part of the clip is on screen)
        {
            const auto o = overviewArea().toFloat().reduced (8, 3);
            g.setColour (P.cell); g.fillRoundedRectangle (o, 4);
            const float a = o.getX() + o.getWidth() * (float) (page.viewStart / (double) c->frames()), w = o.getWidth() * (float) (page.viewFrames / (double) c->frames());
            g.setColour (P.gold.withAlpha (0.28f)); g.fillRoundedRectangle (a, o.getY(), juce::jmax (6.0f, w), o.getHeight(), 4);
            g.setColour (P.gold.withAlpha (0.8f)); g.drawRoundedRectangle (a + 0.5f, o.getY() + 0.5f, juce::jmax (6.0f, w) - 1.0f, o.getHeight() - 1.0f, 4, 1.0f);
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
        setOpaque (true);
    }
    ChopPage& page;
    std::vector<chop::SliceView> cached;   // refreshed by the page's timer, not per key
    int chopIndexFor (int note) const
    {
        for (size_t i = 0; i < cached.size(); ++i) if (note >= cached[i].note && note <= cached[i].highNote) return (int) i;
        return -1;
    }
    choir::Cache art; bool anyDown = false;
    void paint (juce::Graphics& g) override
    {   // the keys at rest are cached; while any key sounds they are drawn live
        cached = page.engine().slices();
        anyDown = false;
        anyDown = page.playingNote() >= 0;
        for (int n = 0; n < 128 && ! anyDown; ++n) anyDown = page.engine().noteSounding (n) || page.keyState.isNoteOn (1, n);
        if (anyDown || isMouseOverOrDragging()) { g.fillAll (page.cp().lead); juce::MidiKeyboardComponent::paint (g); return; }
        juce::String k; k << (int) page.cp().dark << "|" << getLowestVisibleKey() << "|" << getKeyWidth() << "|" << getWidth() << "x" << getHeight() << "|" << (int) page.engine().keys << slicesKey (page.engine());
        art.draw (g, k, getLocalBounds(), [this] (juce::Graphics& ig, juce::Rectangle<int>) { ig.fillAll (page.cp().lead); juce::MidiKeyboardComponent::paint (ig); }, true);
    }
    void mouseExit (const juce::MouseEvent& e) override { juce::MidiKeyboardComponent::mouseExit (e); repaint(); }
    // (polish 3) JUCE's own scroll buttons lie over the end keys (a sliver of a key showed beside them): they stay hidden, and
    // the page's own arrows sit in gutters beside the keyboard, so it starts and ends on whole keys
    void resized() override
    {
        juce::MidiKeyboardComponent::resized();
        for (auto* c : getChildren()) c->setVisible (false);
    }
    void drawWhiteNote (int note, juce::Graphics& g, juce::Rectangle<float> r, bool down, bool over, juce::Colour, juce::Colour) override
    {
        const auto& P = page.cp();
        const int ci = chopIndexFor (note);
        const bool chops = page.engine().keys == chop::KeysMode::chops;
        const bool sounding = down || page.engine().noteSounding (note) || note == page.playingNote();
        auto k = r.reduced (0.6f, 0).withTrimmedBottom (1.0f);
        // (round 3, from the studio entry) a key that holds a chop wears its chop's glass colour, so key and pane match at a glance
        const auto base = ci >= 0 && chops ? P.keyWhite.interpolatedWith (P.chop (ci), P.dark ? 0.34f : 0.26f) : P.keyWhite;
        juce::ColourGradient gr (sounding ? P.play.brighter (0.3f) : base, 0, k.getY(), sounding ? P.play : base.darker (over ? 0.05f : 0.14f), 0, k.getBottom(), false);
        g.setGradientFill (gr); g.fillRoundedRectangle (k, 2.0f);
        if (ci >= 0 && chops) { g.setColour (P.chop (ci)); g.fillRect (k.getX(), k.getY(), k.getWidth(), 7.0f); }
        if (! chops && note == 60) { g.setColour (P.gold); g.fillRect (k.getX(), k.getY(), k.getWidth(), 6.0f); }
        if ((note % 12 == 0 || (ci >= 0 && chops)) && k.getX() >= -0.5f && k.getRight() <= (float) getWidth() + 0.5f)
        {
            // (polish 3) every named key in the same strong ink: a chop's D1 reads as clearly as C1 (C keeps the bold)
            g.setColour (juce::Colour (0xff2a2017).withAlpha (0.92f)); g.setFont (serif (11.5f, note % 12 == 0 || (ci >= 0 && chops) ? choir::bold : choir::regular));
            g.drawText (noteName (note), k.withTrimmedTop (k.getHeight() - 17), juce::Justification::centred, false);
        }
    }
    void drawBlackNote (int note, juce::Graphics& g, juce::Rectangle<float> r, bool down, bool over, juce::Colour) override
    {
        const auto& P = page.cp();
        const int ci = chopIndexFor (note);
        const bool sounding = down || page.engine().noteSounding (note) || note == page.playingNote();
        auto k = r.reduced (0.5f, 0);
        const bool chopKey = ci >= 0 && page.engine().keys == chop::KeysMode::chops;
        const auto base = chopKey ? P.keyBlack.interpolatedWith (P.chop (ci), 0.42f) : P.keyBlack;
        juce::ColourGradient gr (sounding ? P.play : base.brighter (over ? 0.25f : 0.12f), 0, k.getY(), sounding ? P.play.darker (0.3f) : base, 0, k.getBottom(), false);
        g.setGradientFill (gr); g.fillRoundedRectangle (k, 2.0f);
        if (ci >= 0 && page.engine().keys == chop::KeysMode::chops) { g.setColour (P.chop (ci)); g.fillRect (k.getX() + 1, k.getY(), k.getWidth() - 2, 5.0f); }
    }
    void drawUpDownButton (juce::Graphics& g, int w, int h, bool over, bool, bool movesOctavesUp) override
    {
        const auto& P = page.cp();
        g.fillAll (P.cell);
        juce::Path ar; const float cx = (float) w * 0.5f, cy = (float) h * 0.5f, d = movesOctavesUp ? 1.0f : -1.0f;
        ar.startNewSubPath (cx - 3 * d, cy - 6); ar.lineTo (cx + 3 * d, cy); ar.lineTo (cx - 3 * d, cy + 6);
        g.setColour (over ? P.goldHi : P.gold); g.strokePath (ar, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
};

// (polish 3) a scroll arrow for the keyboard, in its own gutter beside the keys: one octave per click
struct ChopPage::KeyArrow : public juce::Component, public juce::SettableTooltipClient
{
    KeyArrow (ChopPage& p, int d) : page (p), dir (d) { setTooltip (d < 0 ? "Show lower keys (one octave down)" : "Show higher keys (one octave up)"); }
    ChopPage& page; int dir;
    bool canMove() const
    {
        const int lo = page.keys->getLowestVisibleKey();
        return dir < 0 ? lo > 0 : lo + 12 <= 127 - 16;
    }
    void paint (juce::Graphics& g) override
    {
        const auto& P = page.cp();
        const bool on = canMove();
        g.fillAll (P.cell);
        g.setColour (P.edgeDim); g.drawRect (getLocalBounds().toFloat(), 1.0f);
        juce::Path ar; const float cx = (float) getWidth() * 0.5f, cy = (float) getHeight() * 0.5f, d = (float) dir;
        ar.startNewSubPath (cx - 3 * d, cy - 6); ar.lineTo (cx + 3 * d, cy); ar.lineTo (cx - 3 * d, cy + 6);
        g.setColour ((isMouseOver() && on ? P.goldHi : P.gold).withMultipliedAlpha (on ? 1.0f : 0.35f));
        g.strokePath (ar, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent&) override
    {
        if (! canMove()) return;
        page.keys->setLowestVisibleKey (juce::jlimit (0, 127, page.keys->getLowestVisibleKey() + 12 * dir));
        page.keyLeft->repaint(); page.keyRight->repaint();
        page.repaintStalls();
    }
};

// ================================================================================================ the choir stalls over the keys
// (polish 1) A MAP, not a pile: every key on the keyboard has its own pointed arch straight above it (the arches follow the keys'
// own centres, so a black key's arch sits over the black key), and a chop's singer stands alone in its key's arch, robed in its pane's
// glass colour, its note written over its head. All singers are one size, in one row. It opens its mouth and lights up while its key
// sounds. In WHOLE every arch is tinted with the clip's pale light and one gold singer stands at C3 (the key that plays it as it is);
// any other key that sounds gets a singer while it sings.
struct ChopPage::Stalls : public juce::Component
{
    explicit Stalls (ChopPage& p) : page (p) { setOpaque (true); setInterceptsMouseClicks (false, false); }
    ChopPage& page;
    choir::Cache art;
    std::array<bool, 128> was {};
    bool wasPlaying = false;
    float phase = 0;
    struct Arch { int note; juce::Rectangle<float> r; };
    struct Voice { int note; juce::Rectangle<float> r; juce::Colour robe; bool shown; int chop; };
    std::vector<Arch> arches;
    std::vector<Voice> voices;
    float singerW = 20, singerH = 28;

    float centreOf (int note) const { return (float) (page.keys->getX() - getX()) + page.keys->getRectangleForKey (juce::jlimit (0, 127, note)).getCentreX(); }
    void layoutVoices()
    {
        arches.clear(); voices.clear();
        auto& k = *page.keys;
        auto& e = page.engine();
        const auto& P = page.cp();
        const float H = (float) getHeight(), W = (float) getWidth(), kw = k.getKeyWidth();
        for (int n = 0; n < 128; ++n)
        {
            const float c = centreOf (n);
            const float l = n > 0 ? (centreOf (n - 1) + c) * 0.5f : c - kw * 0.35f, r = n < 127 ? (c + centreOf (n + 1)) * 0.5f : c + kw * 0.35f;
            // (polish 3) only over the keys on show: the arrows' gutters keep plain wood
            const float k0 = (float) (k.getX() - getX()), k1 = k0 + (float) k.getWidth();
            if (c < k0 || c > k1) continue;
            const float l2 = juce::jmax (l, k0), r2 = juce::jmin (r, k1);
            juce::ignoreUnused (W);
            arches.push_back ({ n, juce::Rectangle<float> (l2 + 1.5f, 5.0f, r2 - l2 - 3.0f, H - 9.0f) });
        }
        // one size for every singer: the narrowest arch (a black key's) sets it
        singerW = juce::jmax (12.0f, kw * 0.5f - 5.0f);
        singerH = juce::jmin (singerW * 1.4f, H * 0.46f); singerW = singerH / 1.4f;
        auto voiceIn = [&] (const Arch& a, juce::Colour robe, bool shown, int chopIndex)
        { voices.push_back ({ a.note, { a.r.getCentreX() - singerW * 0.5f, a.r.getBottom() - 3.0f - singerH, singerW, singerH }, robe, shown, chopIndex }); };
        const auto sl = e.slices();
        for (auto& a : arches)
        {
            if (e.keys == chop::KeysMode::chops)
            {
                for (size_t i = 0; i < sl.size(); ++i)
                    if (a.note >= sl[i].note && a.note <= sl[i].highNote) { voiceIn (a, P.chop ((int) i), true, (int) i); break; }
            }
            else if (e.hasClip()) voiceIn (a, a.note == 60 ? P.gold : P.ghost.interpolatedWith (P.glow, 0.4f), a.note == 60, -1);
        }
    }
    void paintStalls (juce::Graphics& g)
    {
        const auto& P = page.cp();
        auto& e = page.engine();
        const auto r = getLocalBounds().toFloat();
        juce::ColourGradient wood (P.dark ? juce::Colour (0xff221a27) : juce::Colour (0xffe2d2b0), 0, 0, P.dark ? juce::Colour (0xff120e17) : juce::Colour (0xffcfb98e), 0, r.getHeight(), false);
        g.setGradientFill (wood); g.fillRect (r);
        const bool chops = e.keys == chop::KeysMode::chops, whole = ! chops && e.hasClip();
        for (auto& a : arches)
        {
            const bool black = juce::MidiMessage::isMidiNoteBlack (a.note);
            auto lp = choir::lancet (a.r, 0.30f);
            const Voice* v = nullptr; for (auto& x : voices) if (x.note == a.note && (x.shown || ! whole)) { v = &x; break; }
            // the niche: darker over a black key; a chop's niche glows faintly in its glass, WHOLE's niches in the clip's light
            juce::Colour fill = (P.dark ? juce::Colours::black : P.lead).withAlpha (black ? (P.dark ? 0.55f : 0.22f) : (P.dark ? 0.30f : 0.08f));
            g.setColour (fill); g.fillPath (lp);
            if (v != nullptr && chops) { g.setColour (v->robe.withAlpha (P.dark ? 0.22f : 0.18f)); g.fillPath (lp); }
            if (whole) { g.setColour ((a.note == 60 ? P.gold : P.glow).withAlpha (a.note == 60 ? 0.26f : (P.dark ? 0.10f : 0.12f))); g.fillPath (lp); }
            g.setColour (v != nullptr && chops ? v->robe.withAlpha (0.85f) : P.edgeDim.withMultipliedAlpha (0.8f));
            g.strokePath (lp, juce::PathStrokeType (v != nullptr && chops ? 1.2f : 0.8f));
            // the note over the singer's head: every chop's key, and C3 in WHOLE
            if ((v != nullptr && chops) || (whole && a.note == 60))
            {
                const auto nm = noteName (a.note);
                auto lr = juce::Rectangle<float> (a.r.getX() - 2, a.r.getBottom() - 3.0f - singerH - 17.0f, a.r.getWidth() + 4, 15.0f);
                g.setColour (whole ? P.gold : (P.dark ? v->robe.brighter (0.45f) : v->robe.darker (0.25f)));
                g.setFont (choir::fitted (choir::bold, nm, lr.getWidth(), 13.0f, 8.0f));
                g.drawText (nm, lr, juce::Justification::centred, false);
            }
        }
        g.setColour (P.edge.withAlpha (0.8f)); g.fillRect (r.getX(), r.getBottom() - 3, r.getWidth(), 1.0f);
        g.setColour (P.edgeDim); g.fillRect (r.getX(), 0.0f, r.getWidth(), 1.0f);
        for (auto& v : voices) if (v.shown) choir::singer (g, P, v.r, v.robe, 0.0f, whole ? 0.4f : 0.0f, 1.0f, singerW >= 18);
        if (! e.hasClip())
        {   // no clip yet: the arches wait empty, and a plaque says what will stand in them
            const juce::String t1 = "The choir waits", t2 = "Load a clip and press AUTO CHOP: each chop gets its own singer, in the arch over the key that plays it.";
            const float pw = juce::jmin (r.getWidth() - 40, choir::width (choir::serif (15.0f), t2) + 60), ph = 60;
            auto pl = juce::Rectangle<float> (r.getCentreX() - pw * 0.5f, r.getCentreY() - ph * 0.5f, pw, ph);
            g.setColour (P.shadow); g.fillRoundedRectangle (pl.translated (0, 2), 6);
            g.setColour (P.panel); g.fillRoundedRectangle (pl, 6);
            g.setColour (P.edge); g.drawRoundedRectangle (pl.reduced (0.5f), 6, 1.0f);
            choir::diamond (g, { pl.getX(), pl.getCentreY() }, 4.0f, P.gold); choir::diamond (g, { pl.getRight(), pl.getCentreY() }, 4.0f, P.gold);
            g.setColour (P.ink); g.setFont (choir::display (19.0f)); g.drawText (t1, pl.withTrimmedBottom (ph * 0.46f).withTrimmedTop (6), juce::Justification::centred, false);
            g.setColour (P.dim); g.setFont (choir::fitted (choir::regular, t2, pw - 30, 15.0f, 10.0f)); g.drawText (t2, pl.withTrimmedTop (ph * 0.52f).withTrimmedBottom (6), juce::Justification::centred, false);
        }
    }
    void paint (juce::Graphics& g) override
    {
        auto& e = page.engine();
        const auto& P = page.cp();
        juce::String key; key << (int) P.dark << "|" << page.keys->getLowestVisibleKey() << "|" << page.keys->getKeyWidth() << "|" << getWidth() << "x" << getHeight() << "|" << (int) e.hasClip() << "|" << (int) e.keys << "|" << slicesKey (e);
        if (key != art.key) layoutVoices();
        art.draw (g, key, getLocalBounds(), [this] (juce::Graphics& ig, juce::Rectangle<int>) { paintStalls (ig); }, true);
        // the voices singing now: their key sounds, or the transport is playing through their chop
        const int underPlayhead = page.playingNote();
        for (auto& v : voices)
        {
            if (! e.noteSounding (v.note) && ! page.keyState.isNoteOn (1, v.note) && v.note != underPlayhead) continue;
            for (auto& a : arches)
                if (a.note == v.note)
                {   // (polish 2) the singing voice's arch fills with candle light, so you see which one sings from across the room
                    auto lp = choir::lancet (a.r, 0.30f);
                    g.setColour (P.play.withAlpha (P.dark ? 0.32f : 0.40f)); g.fillPath (lp);
                    g.setColour (P.play); g.strokePath (lp, juce::PathStrokeType (1.6f));
                    const auto nm = noteName (a.note);
                    auto lr = juce::Rectangle<float> (a.r.getX() - 2, a.r.getBottom() - 3.0f - singerH - 17.0f, a.r.getWidth() + 4, 15.0f);
                    g.setColour (P.dark ? P.goldHi : P.ink); g.setFont (choir::fitted (choir::bold, nm, lr.getWidth(), 13.0f, 8.0f)); g.drawText (nm, lr, juce::Justification::centred, false);
                }
            const float open = 0.65f + 0.35f * std::sin (phase * 7.0f + (float) v.note);
            choir::singer (g, P, v.r, v.robe, open, 1.0f, 1.0f, singerW >= 18);
        }
    }
    bool tick()
    {   // repaints only when a voice starts / stops, or while voices sing (their mouths move); true = something started / stopped
        phase += 1.0f / 30.0f;
        auto& e = page.engine();
        bool any = false, changed = false;
        for (int n = 0; n < 128; ++n)
        {
            const bool s = e.noteSounding (n) || page.keyState.isNoteOn (1, n);
            changed |= s != was[(size_t) n]; was[(size_t) n] = s; any |= s;
        }
        if (any || changed || e.isPlaying() || wasPlaying) repaint();
        wasPlaying = e.isPlaying();
        return changed;
    }
};

// ================================================================================================ SOUND: one chop's own settings, laid out as the chop engine's signal chain
// The engine's own modules, left to right: TIME/PITCH, FILTER, AMP, PLAYBACK, under its own control names and in its own order
// (only the controls Ghost Voice wires; each has a plain-words line under it). Above them the engine's context row: SAMPLE | SLICE n
// and the note it sits on.
struct ChopPage::Settings : public juce::Component
{
    using P = IntersectProcessor;
    struct Row { juce::String name; int field; juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox }; std::function<juce::String (double)> text;
                 std::function<float (double)> toEngine; std::function<double (float)> fromEngine; };
    // one cell of a module: a knob (row), a picker or a switch, with its engine name and its plain line
    struct Cell { juce::String name, plain; juce::Component* c = nullptr; Row* knob = nullptr; int module = 0; int row = -1; juce::Rectangle<int> r; };   // row: -1 = the module's full height, 0 / 1 = INTERSECT's upper / lower row
    explicit Settings (ChopPage& p) : page (p)
    {
        setOpaque (true);
        auto add = [this] (juce::String n, int f, double lo, double hi, double step, std::function<juce::String (double)> t,
                           std::function<float (double)> to = nullptr, std::function<double (float)> from = nullptr, double skewMid = 0)
        {
            auto r = std::make_unique<Row>(); r->name = std::move (n); r->field = f; r->text = std::move (t);
            r->toEngine = to ? to : [] (double v) { return (float) v; }; r->fromEngine = from ? from : [] (float v) { return (double) v; };
            r->slider.setRange (lo, hi, step); if (skewMid > 0) r->slider.setSkewFactorFromMidPoint (skewMid);
            r->slider.setWantsKeyboardFocus (false);
            r->slider.setMouseDragSensitivity (160);
            auto* row = r.get();
            r->slider.onValueChange = [this, row] { if (! syncing) page.engine().setSliceParam (page.selectedChop, row->field, row->toEngine (row->slider.getValue())); repaint(); };
            r->slider.onDragEnd = [this] { page.refreshControls(); };
            addAndMakeVisible (r->slider); rows.push_back (std::move (r));
        };
        add ("PITCH", P::FieldPitch, -24, 24, 1, [] (double v) { return (v > 0 ? "+" : "") + juce::String ((int) v) + " st"; });
        add ("TUNE", P::FieldCentsDetune, -100, 100, 1, [] (double v) { return (v > 0 ? "+" : "") + juce::String ((int) v) + " c"; });
        add ("GAIN", P::FieldVolume, -36, 12, 0.1, [] (double v) { return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; });
        add ("ATK", P::FieldAttack, 0, 2000, 1, [] (double v) { return juce::String ((int) v) + " ms"; },
             [] (double v) { return (float) (v / 1000.0); }, [] (float v) { return (double) v * 1000.0; }, 100);
        add ("REL", P::FieldRelease, 0, 5000, 1, [] (double v) { return juce::String ((int) v) + " ms"; },
             [] (double v) { return (float) (v / 1000.0); }, [] (float v) { return (double) v * 1000.0; }, 300);
        add ("CUT", P::FieldFilterCutoff, 20, 20000, 1, [] (double v) { return v >= 1000 ? juce::String (v / 1000.0, 1) + " kHz" : juce::String ((int) v) + " Hz"; }, nullptr, nullptr, 1000);
        add ("RESO", P::FieldFilterReso, 0, 100, 0.1, [] (double v) { return juce::String (juce::roundToInt (v)) + " %"; });   // (INTERSECT keeps RESO as 0..100 %)
        rows[0]->slider.setTooltip ("PITCH: transposes the chop, in semitones");
        rows[1]->slider.setTooltip ("TUNE: fine pitch, in cents");
        rows[2]->slider.setTooltip ("GAIN: the chop's level");
        rows[3]->slider.setTooltip ("ATK: the fade-in when the key goes down");
        rows[4]->slider.setTooltip ("REL: the fade-out after the key is let go");
        rows[5]->slider.setTooltip ("CUT: the filter's frequency (greyed with the filter OFF)");
        rows[6]->slider.setTooltip ("RESO: the filter's resonance, a ring at CUT (greyed with the filter OFF)");
        auto combo = [this] (juce::ComboBox& c, juce::StringArray items, int field, juce::String tip)
        {
            c.addItemList (items, 1); c.setWantsKeyboardFocus (false); c.setTooltip (tip); addAndMakeVisible (c);
        c.setTextWhenNothingSelected (juce::String::fromUTF8 ("\xe2\x80\x94"));
            c.onChange = [this, &c, field] { if (! syncing) page.engine().setSliceParam (page.selectedChop, field, (float) (c.getSelectedId() - 1)); page.refreshControls(); };
        };
        combo (stretch, { "REPITCH", "STRETCH", "SMOOTH" }, P::FieldAlgorithm,
               "ALGO: how the chop is pitched. REPITCH speeds it up / slows it down like a sampler; STRETCH and SMOOTH keep its length");
        combo (loopMode, { "OFF", "LOOP", "PP" }, P::FieldLoop, "LOOP: loops the chop while the key is held (PP = back and forth)");
        combo (filter, { "OFF", "LP", "HP", "BP", "NT" }, P::FieldFilterEnabled, "TYPE: the chop's filter. OFF, LP (cuts the highs), HP (cuts the lows), BP (keeps a band), NT (cuts a notch)");
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
        toggle (reverse, P::FieldReverse, "REV: plays the chop backwards");
        toggle (oneShot, P::FieldOneShot, "1SHOT: the chop plays to its end even when you let go of the key");
        toggle (keepVoice, P::FieldFormantComp, "FMNT C: keeps the voice's character when it's pitched (only with STRETCH: greyed with REPITCH and SMOOTH)");
        // the engine's order: TIME/PITCH (PITCH, TUNE, ALGO, FMNT C) | FILTER (TYPE, CUT, RESO) | AMP (ATK / REL, GAIN) | PLAYBACK (REV, LOOP / 1SHOT)
        // (fixes) AMP and PLAYBACK on INTERSECT's own two rows (its upper row: ATK DEC SUS and REV LOOP FADE; its lower row: REL TAIL GAIN
        // and MUTE 1SHOT OUT), with the controls Ghost Voice has
        cells = { { "PITCH", "semitones up / down", nullptr, rows[0].get(), 0 }, { "TUNE", "fine pitch, cents", nullptr, rows[1].get(), 0 },
                  { "ALGO", "how it is pitched", &stretch, nullptr, 0 }, { "FMNT C", "keeps the voice", &keepVoice, nullptr, 0 },
                  { "TYPE", "the filter's shape", &filter, nullptr, 1 }, { "CUT", "filter frequency", nullptr, rows[5].get(), 1 }, { "RESO", "ring at the cutoff", nullptr, rows[6].get(), 1 },
                  { "ATK", "fade-in when a key goes down", nullptr, rows[3].get(), 2, 0 }, { "REL", "fade on key-up", nullptr, rows[4].get(), 2, 1 }, { "GAIN", "the chop's level", nullptr, rows[2].get(), 2, 1 },
                  { "REV", "plays backwards", &reverse, nullptr, 3, 0 }, { "LOOP", "loops while held", &loopMode, nullptr, 3, 0 }, { "1SHOT", "plays to the end, even after the key is let go", &oneShot, nullptr, 3, 1 } };
        reverse.setButtonText ("REV"); oneShot.setButtonText ("1SHOT"); keepVoice.setButtonText ("FMNT C");
        for (auto* t : { &reverse, &oneShot, &keepVoice }) t->getProperties().set ("onOff", true);   // the switch shows ON / OFF (its name is over it)
    }
    ChopPage& page;
    std::vector<std::unique_ptr<Row>> rows;
    juce::ComboBox stretch, loopMode, filter;
    juce::ToggleButton reverse { "REVERSE" }, oneShot { "ONE-SHOT" }, keepVoice { "KEEP VOICE" };
    bool syncing = false;
    std::vector<Cell> cells;
    juce::Rectangle<int> contextArea, modArea[4];
    float plainSize = 13.0f;
    choir::Cache art;
    static constexpr const char* modName[4] { "TIME / PITCH", "FILTER", "AMP", "PLAYBACK" };

    void sync()
    {   // read the chop's settings back from the engine (unless a control is being dragged)
        auto& e = page.engine();
        const bool have = e.sliceCount() > 0 && page.selectedChop < e.sliceCount();
        juce::ScopedValueSetter<bool> sv (syncing, true);
        for (auto& r : rows) if (have && ! r->slider.isMouseButtonDown()) r->slider.setValue (r->fromEngine (e.sliceParam (page.selectedChop, r->field)), juce::dontSendNotification);
        const int algo = have ? (int) e.sliceParam (page.selectedChop, P::FieldAlgorithm) : 0;
        const bool filt = have && e.sliceParam (page.selectedChop, P::FieldFilterEnabled) > 0.5f;
        if (! have)
        {   // (polish 2) no clip: the pickers show a dash, not an empty box
            for (auto* c : { &stretch, &loopMode, &filter }) c->setSelectedId (0, juce::dontSendNotification);
        }
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
        keepVoice.setEnabled (have && algo == 1);   // FMNT C only works with the Signalsmith stretch (STRETCH), as in INTERSECT's own panel
        repaint();
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 6);
        contextArea = r.removeFromTop (24);
        r.removeFromTop (4);
        // (polish 1) a picker or a switch gets more room than a knob (its words must fit: REPITCH, LP low-pass); the modules follow.
        // (fixes) a module on two rows is as wide as its wider row; a cell on a row has its value beside the knob, so it is wider
        auto weight = [] (const Cell& c) { return c.row >= 0 ? 2.0f : (c.knob != nullptr ? 1.0f : 1.7f); };
        float wsum[4] {}, rowSum[4][2] {}, total = 0;
        for (auto& c : cells) { if (c.row >= 0) rowSum[c.module][c.row] += weight (c); else wsum[c.module] += weight (c); }
        for (int m = 0; m < 4; ++m) { twoRows[m] = rowSum[m][0] + rowSum[m][1] > 0; if (twoRows[m]) wsum[m] = juce::jmax (rowSum[m][0], rowSum[m][1]); total += wsum[m]; }
        const int gap = 8;
        const int pad = 8;   // each module's inner side margin
        const float unit = (float) (r.getWidth() - gap * 3 - pad * 2 * 4) / total;
        int x = r.getX();
        for (int m = 0; m < 4; ++m)
        {
            const int w = m == 3 ? r.getRight() - x : juce::roundToInt (wsum[m] * unit) + pad * 2;
            modArea[m] = juce::Rectangle<int> (x, r.getY(), w, r.getHeight());
            x += w + gap;
        }
        float at[4] {};
        int rowAt[4][2] {}, rowCount[4][2] {};
        for (auto& c : cells) if (c.row >= 0) ++rowCount[c.module][c.row];
        for (auto& c : cells)
        {
            auto ma = modArea[c.module].reduced (pad, 0).withTrimmedTop (20);
            if (c.row >= 0)
            {   // one of INTERSECT's two rows: the row's cells share its width evenly
                auto rr = rowRect (c.module, c.row);
                const int n = juce::jmax (1, rowCount[c.module][c.row]), i = rowAt[c.module][c.row]++;
                const int x0 = rr.getX() + rr.getWidth() * i / n, x1 = rr.getX() + rr.getWidth() * (i + 1) / n;
                c.r = juce::Rectangle<int> (x0, rr.getY(), x1 - x0, rr.getHeight());
                auto ctl = c.r.reduced (3, 0).withTrimmedTop (rowNameH).withHeight (rowCtlH);
                if (c.knob != nullptr)
                {   // the knob, its value beside it (the pair centred in the cell)
                    const auto vw = rowValueW (c);
                    const int pairW = rowCtlH + 4 + vw, x0k = ctl.getX() + juce::jmax (0, (ctl.getWidth() - pairW) / 2);
                    c.knob->slider.setBounds (x0k, ctl.getY(), rowCtlH, rowCtlH);
                }
                else c.c->setBounds (ctl.withSizeKeepingCentre (juce::jmin (ctl.getWidth() - 6, 96), 28));
                continue;
            }
            const float k = (float) ma.getWidth() / wsum[c.module];
            const int x0 = ma.getX() + juce::roundToInt (at[c.module] * k);
            at[c.module] += weight (c);
            const int x1 = ma.getX() + juce::roundToInt (at[c.module] * k);
            // (fixes) the cell's words and control as one block, centred in the module's height
            const int ch = ctlH (ma.getHeight()), need = 17 + ch + 17 + 30;
            c.r = juce::Rectangle<int> (x0, ma.getY() + juce::jmax (0, (ma.getHeight() - need) / 2), x1 - x0, juce::jmin (need, ma.getHeight()));
            auto inner = c.r.reduced (3, 0);
            // (polish 1) name 17 | control (knobs up to 48 px) | value 17 | plain 16
            auto ctl = inner.withTrimmedTop (17).withHeight (ctlH (inner.getHeight()));
            if (c.knob != nullptr) c.knob->slider.setBounds (ctl.withSizeKeepingCentre (ctl.getHeight(), ctl.getHeight()));
            else c.c->setBounds (ctl.withSizeKeepingCentre (juce::jmin (ctl.getWidth(), 104), 28));
        }
        plainSize = 13.5f;
        // the plain line on a row is one line: one size for every row cell, the largest at which all of them fit
        rowPlainSize = 13.0f;
        for (auto& c : cells)
            if (c.row >= 0)
                for (const auto& t : plainsFor (c)) rowPlainSize = juce::jmin (rowPlainSize, choir::fit (choir::regular, t, (float) c.r.getWidth() - 6.0f, 13.0f, 11.0f));
        art.clear();
    }
    bool twoRows[4] {};
    static constexpr int rowNameH = 16, rowCtlH = 34, rowGap = 4;
    float rowPlainSize = 12.0f;
    juce::Rectangle<int> rowRect (int m, int row) const
    {
        auto body = modArea[m].reduced (8, 0).withTrimmedTop (20).withTrimmedBottom (2);
        const int h = (body.getHeight() - rowGap) / 2;
        return row == 0 ? body.withHeight (h) : body.withTrimmedTop (h + rowGap);
    }
    int rowValueW (const Cell& c) const
    {   // the widest value this knob can show
        int w = 0;
        for (double v : { c.knob->slider.getMinimum(), c.knob->slider.getMaximum(), c.knob->slider.getValue() })
            w = juce::jmax (w, (int) std::ceil (choir::width (serif (14.0f, choir::bold), c.knob->text (v))));
        return w;
    }
    juce::StringArray plainsFor (const Cell& c) const
    {   // every plain line the cell can show (LOOP changes its words with the mode)
        juce::StringArray a { c.plain };
        if (c.c == &loopMode) a.add ("ping-pong: to and fro");
        return a;
    }
    static int ctlH (int cellH) { return juce::jlimit (26, 46, cellH - 17 - 17 - 30); }
    void paintArt (juce::Graphics& g)
    {
        const auto& C = page.cp();
        g.setColour (C.panel); g.fillAll();
        choir::page (g, C, getLocalBounds().toFloat().reduced (1.0f));
        for (int m = 0; m < 4; ++m)
        {
            auto a = modArea[m].toFloat();
            g.setColour (C.cell.withAlpha (C.dark ? 0.35f : 0.45f)); g.fillRoundedRectangle (a, 4.0f);
            g.setColour (C.edgeDim); g.drawRoundedRectangle (a.reduced (0.5f), 4.0f, 1.0f);
            auto t = a.removeFromTop (18);
            g.setColour (C.edgeDim.withMultipliedAlpha (0.6f)); g.fillRect (t.getX() + 6, t.getBottom(), t.getWidth() - 12, 1.0f);
            g.setColour (C.dark ? C.gold : C.edge.darker (0.2f)); g.setFont (display (14.0f)); g.drawText (modName[m], t, juce::Justification::centred, false);
        }
        // (fixes) INTERSECT's two rows: a faint rule between them, and between the cells of a row
        for (int m = 0; m < 4; ++m)
            if (twoRows[m])
            {
                const auto r1 = rowRect (m, 1).toFloat();
                g.setColour (C.edgeDim.withMultipliedAlpha (0.6f)); g.fillRect (r1.getX() + 4, r1.getY() - (float) rowGap * 0.5f, r1.getWidth() - 8, 1.0f);
            }
        for (size_t i = 1; i < cells.size(); ++i)
            if (cells[i].row >= 0 && cells[i].row == cells[i - 1].row && cells[i].module == cells[i - 1].module)
            { const auto& c = cells[i]; g.setColour (C.edgeDim.withMultipliedAlpha (0.7f)); g.fillRect ((float) c.r.getX() - 0.5f, (float) c.r.getY() + 4, 1.0f, (float) c.r.getHeight() - 8); }
    }
    void paint (juce::Graphics& g) override
    {   // the frames, the words and the values together, cached until a value, a state or the chop changes
        const auto& C = page.cp();
        auto& e = page.engine();
        juce::String k; k << (int) C.dark << getWidth() << "x" << getHeight() << "|" << page.selectedChop << (int) e.keys << slicesKey (e);
        k << filter.getSelectedId() << loopMode.getSelectedId() << stretch.getSelectedId();
        for (auto& c : cells) k << (int) (c.knob != nullptr ? c.knob->slider.isEnabled() : c.c->isEnabled()) << (c.knob != nullptr ? c.knob->text (c.knob->slider.getValue()) : juce::String());
        art.draw (g, k, getLocalBounds(), [this] (juce::Graphics& ig, juce::Rectangle<int>) { paintArt (ig); paintWords (ig); }, true);
    }
    void paintWords (juce::Graphics& g)
    {
        const auto& C = page.cp();
        auto& e = page.engine();
        const auto sl = e.slices();
        // the context row: SAMPLE | SLICE n, and the note it is on
        juce::String where, sub;
        const bool whole = e.keys == chop::KeysMode::whole;
        int shownIndex = -1;
        for (size_t i = 0; i < sl.size(); ++i)
            if (sl[i].index == page.selectedChop)
            {
                shownIndex = (int) i;
                if (whole) { where = "the whole clip"; sub = "every key plays it, C3 = as it is"; }
                else { where = "chop " + juce::String ((int) i + 1) + " of " + juce::String ((int) sl.size()); sub = "on " + noteName (sl[i].note); if (e.labelFor (i).isNotEmpty()) sub << "  \"" << e.labelFor (i) << "\""; }
            }
        // (polish 3) a loaded clip in WHOLE never reads "no clip yet"
        if (e.hasClip() && (whole || sl.empty())) { where = "whole clip"; sub = "chop it to give each chop its own sound"; }
        if (! e.hasClip()) { where = "no clip yet"; sub = "load a clip: SOUND is each chop's own sound"; }
        auto cr = contextArea.toFloat();
        g.setColour (C.ink); g.setFont (display (17.0f)); g.drawText ("SOUND", cr.removeFromLeft (70), juce::Justification::centredLeft, false);
        // (fixes) INTERSECT's SAMPLE / SLICE scope, drawn as a label (not a tab): Ghost Voice's SOUND always edits what is on the keys
        // (the whole clip in WHOLE, the chosen chop in CHOPS), so there is no scope to switch here
        {
            const juce::String scope = ! e.hasClip() ? juce::String ("SAMPLE") : (whole || shownIndex < 0) ? juce::String ("SAMPLE") : "SLICE " + juce::String (shownIndex + 1);
            const auto f = display (14.0f);
            auto b = cr.removeFromLeft (choir::width (f, scope) + 22);
            choir::diamond (g, { b.getX() + 4.0f, b.getCentreY() }, 3.0f, C.gold);
            g.setColour (choir::greyInk (C, C.dark ? C.gold : C.edge.darker (0.3f), e.hasClip() ? 1.0f : choir::greyA (C)));
            g.setFont (f); g.drawText (scope, b.withTrimmedLeft (12), juce::Justification::centredLeft, false);
        }
        cr.removeFromLeft (8);
        g.setColour (C.ink); g.setFont (serif (15.0f, choir::bold));
        g.drawText (where, cr.removeFromLeft (choir::width (serif (15.0f, choir::bold), where) + 12), juce::Justification::centredLeft, false);
        g.setColour (C.dim); g.setFont (serif (15.0f, choir::italic)); g.drawText (sub, cr, juce::Justification::centredLeft, true);
        // each control: the engine's name over it, its value, and what it does in plain words
        for (auto& c : cells)
        {
            const bool en = c.knob != nullptr ? c.knob->slider.isEnabled() : c.c->isEnabled();
            const float a = en ? 1.0f : choir::greyA (C);
            auto r = c.r.toFloat().reduced (1, 0);
            const auto valueInkRow = C.dark ? C.gold : C.edge.darker (0.35f);
            if (c.row >= 0)
            {   // (fixes) a cell on one of INTERSECT's rows: the name, the control (a knob with its value beside it), the plain line
                g.setColour (choir::greyInk (C, C.ink, a)); g.setFont (serif (14.5f, choir::bold)); g.drawText (c.name, r.removeFromTop ((float) rowNameH), juce::Justification::centred, false);
                auto ctl = r.removeFromTop ((float) rowCtlH);
                if (c.knob != nullptr)
                {
                    const auto kb = c.knob->slider.getBounds().toFloat();
                    g.setColour (choir::greyInk (C, valueInkRow, a)); g.setFont (serif (14.0f, choir::bold));
                    g.drawText (c.knob->text (c.knob->slider.getValue()), juce::Rectangle<float> (kb.getRight() + 4, ctl.getY(), ctl.getRight() - kb.getRight() - 4, ctl.getHeight()), juce::Justification::centredLeft, false);
                }
                auto plain = c.plain;
                if (c.c == &loopMode && loopMode.getSelectedId() == 3) plain = "ping-pong: to and fro";
                g.setColour (choir::greyInk (C, C.dim, a)); g.setFont (serif (rowPlainSize, choir::regular));
                g.drawText (plain, r.withTrimmedTop (1), juce::Justification::centredTop, false);
                continue;
            }
            g.setColour (choir::greyInk (C, C.ink, a)); g.setFont (serif (15.0f, choir::bold)); g.drawText (c.name, r.removeFromTop (17), juce::Justification::centred, false);
            r.removeFromTop ((float) ctlH (c.r.getHeight()));
            const auto valueInk = C.dark ? C.gold : C.edge.darker (0.35f);
            if (c.knob != nullptr) { g.setColour (choir::greyInk (C, valueInk, a)); g.setFont (serif (14.5f, choir::bold)); g.drawText (c.knob->text (c.knob->slider.getValue()), r.removeFromTop (17), juce::Justification::centred, false); }
            else r.removeFromTop (17);
            // the plain-words line: upright, full ink-dim, as big as it can be in its cell (one size per panel: the smallest that fits all)
            auto plain = c.plain;
            if (c.c == &filter && filter.getSelectedId() > 1) { static const char* w[] { "", "", "low-pass: cuts the highs", "high-pass: cuts the lows", "band-pass: keeps a band", "notch: cuts a notch" }; plain = w[juce::jlimit (0, 5, filter.getSelectedId())]; }
            if (c.c == &loopMode && loopMode.getSelectedId() == 3) plain = "ping-pong: back and forth";
            g.setColour (choir::greyInk (C, C.dim, a)); g.setFont (serif (plainSize, choir::regular));
            g.drawFittedText (plain, r.removeFromTop (30).toNearestInt(), juce::Justification::centredTop, 2, 0.9f);
        }
    }
};

// ================================================================================================ the finder's install card (polish 2)
// When PHRASES or WORDS is picked and its finder is not installed yet: the wave dims and a card in its middle says what is needed,
// with the INSTALL button in it. Only the card takes the mouse; the rest of the wave stays usable under the dim.
struct ChopPage::InstallVeil : public juce::Component
{
    explicit InstallVeil (ChopPage& p) : page (p) {}
    ChopPage& page;
    juce::Rectangle<float> card() const { return getLocalBounds().toFloat().withSizeKeepingCentre (juce::jmin (480.0f, (float) getWidth() - 40), 128.0f); }
    bool hitTest (int x, int y) override { return card().contains ((float) x, (float) y); }
    void paint (juce::Graphics& g) override
    {
        const auto& P = page.cp();
        const bool words = page.engine().autoMode == chop::AutoMode::words;
        g.setColour (P.waveBg.withAlpha (0.72f)); g.fillRect (getLocalBounds());
        auto c = card();
        g.setColour (P.shadow); g.fillRoundedRectangle (c.translated (0, 3), 7);
        choir::page (g, P, c);
        auto t = c.reduced (20, 14);
        g.setColour (P.ink); g.setFont (display (20.0f));
        g.drawText (words ? "WORDS needs its word finder" : "PHRASES needs its phrase finder", t.removeFromTop (26), juce::Justification::centred, false);
        g.setColour (P.dim); g.setFont (serif (15.0f));
        g.drawFittedText (words ? "Installed once into Ghost Voice's own folder (about 1.5 GB). Then AUTO CHOP puts one word on each key."
                                : "Installed once into Ghost Voice's own folder (about 60 MB). Then AUTO CHOP puts one phrase on each key.",
                          t.removeFromTop (40).toNearestInt(), juce::Justification::centred, 2, 1.0f);
    }
    void resized() override { auto c = card().toNearestInt(); page.installB.setBounds (c.withSizeKeepingCentre (290, 32).withY (c.getBottom() - 46)); }
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

void ChopPage::repaintStalls() { stalls->repaint(); }

ChopPage::ChopPage (Processor& p, std::function<const ghosty::themes::Palette&()> pl, std::function<void()> lab)
    : proc (p), palette (std::move (pl)), openLab (std::move (lab))
{
    setWantsKeyboardFocus (true);
    setOpaque (true);
    wave = std::make_unique<Wave> (*this); addAndMakeVisible (*wave);
    keys = std::make_unique<Keys> (*this, keyState); addAndMakeVisible (*keys);
    keyLeft = std::make_unique<KeyArrow> (*this, -1); addAndMakeVisible (*keyLeft);
    keyRight = std::make_unique<KeyArrow> (*this, 1); addAndMakeVisible (*keyRight);
    settings = std::make_unique<Settings> (*this); addAndMakeVisible (*settings);
    strip = std::make_unique<StatusStrip> (*this);   // (the Editor adds it to the window)
    stalls = std::make_unique<Stalls> (*this); addAndMakeVisible (*stalls);
    veil = std::make_unique<InstallVeil> (*this); addChildComponent (*veil);
    keyState.addListener (this);
    for (int i = 0; i < (int) Tool::kTools; ++i) { tools.push_back (std::make_unique<ToolButton> (*this, (Tool) i)); addAndMakeVisible (*tools.back()); }
    dragOut = std::make_unique<DragOut> (*this); addAndMakeVisible (*dragOut);
    for (auto* b : { &playB, &loopB, &autoB, &minus, &plus, &keysChops, &keysWhole, &convertB, &addB, &recB, &undoB, &redoB, &cutB, &copyB, &pasteB, &delB, &trimB, &dupB, &clearB })
    { addAndMakeVisible (*b); b->setMouseCursor (juce::MouseCursor::PointingHandCursor); b->setWantsKeyboardFocus (false); }
    addAndMakeVisible (autoMode); autoMode.setWantsKeyboardFocus (false);
    veil->addAndMakeVisible (installB); installB.setMouseCursor (juce::MouseCursor::PointingHandCursor); installB.setWantsKeyboardFocus (false);
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

// (fixes) the status line's strip: a child of the Editor (so the resize corner sits beside it, not on it); the page paints it
struct ChopPage::StatusStrip : public juce::Component
{
    explicit StatusStrip (ChopPage& p) : page (p) { setOpaque (true); }
    ChopPage& page;
    juce::String shown;
    void paint (juce::Graphics& g) override { page.paintStatus (g, getLocalBounds()); }
    void mouseDown (const juce::MouseEvent&) override { page.grabKeyboardFocus(); }
};

juce::Component* ChopPage::statusStrip() { return strip.get(); }

void ChopPage::paintStatus (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto& P = cp();
    g.setColour (P.panel); g.fillRect (area);
    g.setColour (P.edgeDim); g.fillRect (area.getX(), area.getY(), area.getWidth(), 1);
    auto sa = area.withTrimmedTop (1).reduced (16, 0);
    choir::diamond (g, { (float) sa.getX() + 4, (float) sa.getCentreY() }, 3.5f, P.gold);
    auto st = status();
    if (st.isEmpty()) st = stateLine();
    if (strip != nullptr) strip->shown = st;
    g.setColour (P.ink); g.setFont (choir::fitted (choir::regular, st, (float) sa.getWidth() - 16, 15.0f, 10.0f));
    g.drawText (st, sa.withTrimmedLeft (16), juce::Justification::centredLeft, false);
    if (conv.active)
    { g.setColour (P.play); g.fillRect (area.getX(), area.getBottom() - 3, (int) ((float) area.getWidth() * juce::jlimit (0.0f, 1.0f, proc.progressValue())), 3); }
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
    if (engine().isPlaying() || conv.active || recording || engine().analysing() || engine().toolBusy()) { wave->repaint(); repaint (tipArea); if (conv.active) strip->repaint(); }
    if (strip->isVisible())
    {   // the status line repaints only when its words change
        auto st = status(); if (st.isEmpty()) st = stateLine();
        if (st != strip->shown) strip->repaint();
    }
    if (toolWasBusy != engine().toolBusy()) { toolWasBusy = engine().toolBusy(); refreshControls(); }
    if ((ticks % 10) == 0 && settings != nullptr) settings->sync();
    ++ticks;
    if (stalls->tick()) { keys->repaint(); wave->repaint(); }
    if (const int pn = playingNote(); pn != lastPlayingNote) { lastPlayingNote = pn; keys->repaint(); stalls->repaint(); }
}

void ChopPage::refreshControls()
{
    auto& e = engine();
    const bool clip = e.hasClip(), sel = ! selection.empty();
    playB.setButtonText (e.isPlaying() ? "STOP" : "PLAY");
    loopB.setToggleState (loopOn, juce::dontSendNotification);
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
    installB.setVisible (true);
    veil->setVisible (needInstall && clip);
    installB.setEnabled (! e.toolBusy());
    installB.setButtonText (e.toolBusy() ? "INSTALLING..." : e.autoMode == chop::AutoMode::words ? "INSTALL WORD FINDER (1.5 GB)" : "INSTALL PHRASE FINDER");
    installB.setTooltip (e.autoMode == chop::AutoMode::words ? "Downloads the word finder and its models (about 1.5 GB, once) into Ghost Voice's own folder"
                                                             : "Downloads a small private Python (about 60 MB, once) into Ghost Voice's own folder");
    autoB.setEnabled (clip && ! needInstall && ! e.toolBusy());
    if (e.keys == chop::KeysMode::whole || selectedChop >= e.sliceCount()) selectedChop = 0;
    if (settings != nullptr) settings->sync();
    resized();
    autoMode.setEnabled (clip);
    autoMode.setSelectedId ((int) e.autoMode + 1, juce::dontSendNotification);   // (UNDER THE HOOD can change the mode too)
    convertB.setEnabled (clip && ! conv.active);
    convertB.setButtonText (conv.active ? "CONVERTING..." : "CONVERT VOICE");
    dragOut->repaint();
    repaint();
}

int ChopPage::playingNote() const
{
    auto& e = const_cast<ChopPage*> (this)->engine();
    if (! e.isPlaying() || e.keys != chop::KeysMode::chops) return -1;
    const auto ph = e.playhead();
    for (const auto& sl : e.slices()) if (ph >= sl.r.a && ph < sl.r.b) return sl.note;
    return -1;
}

std::vector<juce::Component*> ChopPage::sideControls() { return { &addB, &recB, dragOut.get(), &convertB }; }

// (polish 1) everything the page holds now, as one line: what is on the keys, what is picked, selected, looped or playing.
// Read from the engine and the page at paint time, so it is never left over from an earlier step.
juce::String ChopPage::stateSig() const
{
    auto& e = const_cast<ChopPage*> (this)->engine();
    juce::String k; k << (int) e.autoMode << "|" << (int) e.keys << "|" << (int) e.chops.size() << "|" << (int) e.labels.size() << "|" << (int) e.toolReady() << "|" << e.frames()
                      << "|" << selection.a << "-" << selection.b << "|" << loop.a << "-" << loop.b << (int) loopOn << "|" << (int) e.isPlaying() << "|" << (int) currentTool
                      << "|" << selectedChop << "|" << (int) juce::roundToInt (viewFrames);
    return k;
}

juce::String ChopPage::stateLine() const
{
    auto& e = const_cast<ChopPage*> (this)->engine();
    if (! e.hasClip()) return "Ready. Drag a WAV in, or press ADD WAV or REC.";
    const auto dot = juce::String::fromUTF8 ("  \xc2\xb7  ");
    juce::StringArray parts;
    if (e.isPlaying()) parts.add (loopOn && ! loop.empty() ? "Playing the loop (" + secs ((double) loop.length() / e.rate()) + ")" : juce::String ("Playing"));
    if (chop::usesTool (e.autoMode) && ! e.toolReady())
        parts.add (chop::autoModeName (e.autoMode) + " needs its finder: press " + installB.getButtonText() + " (once)");
    const auto sl = const_cast<ChopPage*> (this)->engine().slices();
    if (e.keys == chop::KeysMode::whole) parts.add ("The whole clip is on the keys: C3 plays it as it is");
    else if (! sl.empty())
    {
        const bool words = ! e.labels.empty();
        parts.add (juce::String ((int) sl.size()) + (words ? " words" : " chops") + " on the keys, " + noteName (sl.front().note) + " to " + noteName (sl.back().highNote));
        for (size_t i = 0; i < sl.size(); ++i)
            if (sl[i].index == selectedChop)
            { auto w = "SOUND: chop " + juce::String ((int) i + 1) + " (" + noteName (sl[i].note) + ")"; if (e.labelFor (i).isNotEmpty()) w << " \"" << e.labelFor (i) << "\""; parts.add (w); break; }
    }
    if (! selection.empty()) parts.add ("selected " + secs ((double) selection.length() / e.rate()));
    if (loopOn && ! loop.empty() && ! e.isPlaying()) parts.add ("LOOP on, " + secs ((double) loop.length() / e.rate()));
    if (viewFrames > 0 && viewFrames < (double) e.frames() * 0.98) parts.add ("zoomed in to " + juce::String (juce::roundToInt (100.0 * viewFrames / (double) e.frames())) + " %");
    if (parts.isEmpty()) parts.add ("Ready. Press AUTO CHOP, then play the chops on the keys.");
    return parts.joinIntoString (dot);
}

ChopPage::BeatCap ChopPage::beatCap() const
{
    BeatCap b;
    auto& e = const_cast<ChopPage*> (this)->engine();
    const auto m = e.autoMode;
    if (m != chop::AutoMode::beats && m != chop::AutoMode::twoBeats && m != chop::AutoMode::bar) return b;
    if (e.keys != chop::KeysMode::chops || e.chops.empty()) return b;
    const auto c = e.clip(); if (c == nullptr || c->frames() == 0) return b;
    const auto det = e.detected(); if (! det.ready || det.bpm <= 0) return b;
    const double per = m == chop::AutoMode::twoBeats ? 2.0 : m == chop::AutoMode::bar ? 4.0 : 1.0;
    const double step = 60.0 / det.bpm * per;
    const int avail = (int) std::floor ((c->seconds() - det.firstBeat) / step + 1e-6);
    const int shown = (int) e.chops.size();
    if (shown < juce::jlimit (1, 64, e.chopCount) || avail <= shown) return b;
    auto last = e.chops.front(); for (auto& r : e.chops) if (r.a > last.a) last = r;
    const auto tailFrom = last.a + (juce::int64) std::llround (step * c->rate);
    if (tailFrom >= last.b - (juce::int64) (0.05 * c->rate)) return b;
    b.on = true; b.shown = shown; b.avail = avail; b.tailFrom = tailFrom; b.tailTo = last.b;
    b.unit = m == chop::AutoMode::bar ? "bars" : m == chop::AutoMode::twoBeats ? "2-beat steps" : "beats";
    return b;
}

juce::String ChopPage::beatCapLine() const
{
    const auto b = beatCap();
    if (! b.on) return {};
    return juce::String (b.shown) + " of " + juce::String (b.avail) + " " + b.unit + " chopped " + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")) + " raise the count (+) for more";
}

juce::String ChopPage::status() const
{
    auto& e = const_cast<ChopPage*> (this)->engine();
    if (conv.active) return uiWords (proc.progressText());
    if (recording) return "Recording " + secs (proc.recordedSeconds());
    if (note.isNotEmpty() && juce::Time::getMillisecondCounterHiRes() - noteTime < 6000) return note;
    if (e.toolBusy()) return e.tools.progress().isNotEmpty() ? uiWords (e.tools.progress()) : juce::String ("Working...");
    if (e.analysing()) return "Listening to the clip (tempo, key, hits)...";
    // the engine's last message stays only while nothing on the page has changed since it was said; then the page says what it holds
    const auto sig = stateSig();
    if (e.message != seenMessage) { seenMessage = e.message; seenSig = sig; }
    const auto cap = beatCapLine();
    auto withCap = [&cap] (juce::String t) { return cap.isEmpty() ? t : t + "   ·   " + cap; };
    if (e.message.isNotEmpty() && sig == seenSig) return withCap (e.message);
    return withCap (stateLine());
}

void ChopPage::paint (juce::Graphics& g)
{
    const auto& P = cp();
    auto& e = engine();
    back.draw (g, juce::String ((int) P.dark) + "|" + juce::String (getWidth()) + "x" + juce::String (getHeight()) + "|" + deckArea.toString() + waveFrame.toString() + choirFrame.toString(), getLocalBounds(),
               [this, &P] (juce::Graphics& ig, juce::Rectangle<int> r)
               {
                   choir::nave (ig, P, r, getPosition());
                   // the lectern: a dark-oak (or limestone) deck under the two rows of tools and the tip line
                   auto d = deckArea.toFloat();
                   choir::page (ig, P, d);
                   ig.setColour (P.edgeDim.withMultipliedAlpha (0.6f)); ig.fillRect (d.getX() + 14, (float) tipArea.getY() - 3, d.getWidth() - 28, 1.0f);
                   // the frames round the window, the stalls and the keys
                   for (auto fr : { waveFrame, choirFrame })
                   {
                       auto f = fr.toFloat();
                       ig.setColour (P.shadow); ig.fillRect (f.translated (0, 3));
                       ig.setColour (P.edge); ig.drawRect (f, 1.0f);
                       for (auto c : { f.getTopLeft(), f.getTopRight(), f.getBottomLeft(), f.getBottomRight() }) choir::diamond (ig, c, 4.0f, P.gold);
                   }
               }, true);
    // the clip info (right of the tip line): its name, length, and the tempo and key that were found
    juce::String info;
    if (auto c = e.clip())
    {
        const auto det = e.detected();
        info = c->name + "   " + secs (c->seconds());
        if (det.ready) info << "   " << det.summary();
        else if (e.analysing()) info << "   listening...";
    }
    auto t = tipArea.toFloat();
    if (info.isNotEmpty())
    {
        const auto f = serif (15.0f, choir::bold);
        const float iw = juce::jmin (choir::width (f, info) + 8, t.getWidth() * 0.42f);
        auto ir = t.removeFromRight (iw);
        g.setColour (P.gold); g.setFont (choir::fitted (choir::bold, info, ir.getWidth(), 15.0f, 10.0f)); g.drawText (info, ir, juce::Justification::centredRight, false);
        t.removeFromRight (12);
    }
    // the tip line: the tool and what it does (Block Step's)
    {
        const auto n = toolName (currentTool);
        const auto nf = serif (15.0f, choir::bold);
        g.setColour (P.select); g.setFont (nf); g.drawText (n, t, juce::Justification::centredLeft, false);
        auto rest = t.withTrimmedLeft (choir::width (nf, n) + 10);
        const auto tip = toolTip (currentTool);
        g.setColour (P.dim); g.setFont (choir::fitted (choir::italic, tip, rest.getWidth(), 15.0f, 10.0f));
        g.drawText (tip, rest, juce::Justification::centredLeft, false);
    }
    // the chop count between - and +, and the KEYS word
    // (polish 1) PHRASES / WORDS take what they find: the count shows how many chops are on the keys, not a number nobody asked for
    const bool found = chop::usesTool (e.autoMode);
    const juce::String count = ! found ? juce::String (e.chopCount) : (e.keys == chop::KeysMode::chops && ! e.chops.empty() ? juce::String ((int) e.chops.size()) : juce::String ("-"));
    g.setColour (minus.isEnabled() || plus.isEnabled() ? P.ink : choir::greyInk (P, P.ink, choir::greyA (P))); g.setFont (display (18.0f)); g.drawText (count, countArea, juce::Justification::centred, false);
    g.setColour (P.gold); g.setFont (display (13.5f)); g.drawText ("KEYS", keysLabelArea, juce::Justification::centredRight, false);
}

void ChopPage::resized()
{
    auto r = getLocalBounds();   // (fixes) the status strip is under the page, in the Editor's hands
    r = r.reduced (14, 8);
    auto deck = r;
    {   // row 1: transport, the five tools, AUTO CHOP with its mode and count (ADD WAV / REC / DRAG WAV / CONVERT stand in the left column)
        auto row = r.removeFromTop (32).reduced (10, 0).translated (0, 8);
        const int fixed = 60 + 4 + 56 + 10 + 4 * 4 + 10 + 98 + 4 + 128 + 4 + 26 + 30 + 26;
        const int tw = juce::jlimit (70, 110, (row.getWidth() - fixed) / 5);
        playB.setBounds (row.removeFromLeft (60)); row.removeFromLeft (4);
        loopB.setBounds (row.removeFromLeft (56)); row.removeFromLeft (10);
        for (auto& t : tools) { t->setBounds (row.removeFromLeft (tw)); row.removeFromLeft (4); }
        row.removeFromLeft (6);
        plus.setBounds (row.removeFromRight (26)); countArea = row.removeFromRight (30); minus.setBounds (row.removeFromRight (26)); row.removeFromRight (4);
        autoMode.setBounds (row.removeFromRight (128)); row.removeFromRight (4);
        autoB.setBounds (row.removeFromRight (juce::jmin (110, row.getWidth())));
    }
    r.removeFromTop (14);
    {   // row 2: KEYS, then the edits: every button the same width and the same type size (polish 1)
        auto row = r.removeFromTop (30).reduced (10, 0);
        keysLabelArea = row.removeFromLeft (40); row.removeFromLeft (6);
        keysChops.setBounds (row.removeFromLeft (72)); row.removeFromLeft (4); keysWhole.setBounds (row.removeFromLeft (72)); row.removeFromLeft (12);
        // one type size for the whole row, the largest at which every word has the same room round it (14 px of air each side);
        // each button is its words plus that same air, plus an equal share of what is left, so the row ends flush at the right
        juce::TextButton* eds[] { &undoB, &redoB, &cutB, &copyB, &pasteB, &delB, &trimB, &dupB, &clearB };
        const int n = (int) std::size (eds), gap = 4, room = row.getWidth() - gap * (n - 1);
        float fh = 14.0f;
        auto wordsAt = [&] (float h) { float t = 0; for (auto* b : eds) t += choir::width (serif (h, choir::bold), b->getButtonText()); return t; };
        while (fh > 10.0f && wordsAt (fh) + (float) n * 28.0f > (float) room) fh -= 0.5f;
        for (auto* b : { &keysChops, &keysWhole }) fh = juce::jmin (fh, choir::fit (choir::bold, b->getButtonText(), 58.0f, 14.0f, 9.0f));
        const float share = ((float) room - wordsAt (fh)) / (float) n;
        float x = (float) row.getX();
        for (int i = 0; i < n; ++i)
        {
            const float w = choir::width (serif (fh, choir::bold), eds[i]->getButtonText()) + share;
            const int x0 = juce::roundToInt (x), x1 = i == n - 1 ? row.getRight() : juce::roundToInt (x + w);
            eds[i]->setBounds (x0, row.getY(), x1 - x0, row.getHeight()); x += w + (float) gap;
        }
        for (auto* b : std::initializer_list<juce::TextButton*> { &undoB, &redoB, &cutB, &copyB, &pasteB, &delB, &trimB, &dupB, &clearB, &keysChops, &keysWhole })
            b->getProperties().set ("fontH", fh);
        infoArea = {};
    }
    r.removeFromTop (6);
    tipArea = r.removeFromTop (22).reduced (12, 0);
    deckArea = deck.withBottom (tipArea.getBottom() + 4);
    r.removeFromTop (12);
    // under the lectern: the window (wave), SOUND, then the choir stalls on the keys
    auto stallsAndKeys = r.removeFromBottom (156);
    choirFrame = stallsAndKeys;
    r.removeFromBottom (8);
    soundFrame = r.removeFromBottom (196);   // (fixes) AMP and PLAYBACK on two rows, as INTERSECT has them
    settings->setBounds (soundFrame);
    r.removeFromBottom (10);
    waveFrame = r;
    wave->setBounds (r.reduced (1));
    // (polish 1) the finder's INSTALL button floats over the wave's top right corner, under the ruler: the edit row keeps its widths
    veil->setBounds (wave->getBounds()); veil->toFront (false);
    auto sk = stallsAndKeys.reduced (1);
    {   // (polish 3) the arrows in their own 18 px gutters; the keys between them show 17 whole white keys, C1 to E3 (no slivers)
        auto kr = sk.removeFromBottom (70);
        keyLeft->setBounds (kr.removeFromLeft (18)); keyRight->setBounds (kr.removeFromRight (18));
        keys->setBounds (kr);
        keys->setScrollButtonWidth (18);
        keys->setKeyWidth (juce::jmax (18.0f, (float) kr.getWidth() / 17.0f));
        if (! keysPlaced && keys->getWidth() > 0) { keys->setLowestVisibleKey (36); keysPlaced = true; }   // C1, where the chops start
    }
    stalls->setBounds (sk);
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
