// PROCESS LAB panel (see ProcessLab.h for the integration note).
#include "ProcessLabPanel.h"
#include "GhostArt.h"
#include "ExportGate.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace plab {

using namespace ghosty;

static juce::String fmtParam (const ParamInfo& pi, float v)
{
    if (pi.kind == Choice) { auto c = choiceNames (pi); const int i = juce::jlimit (0, juce::jmax (0, c.size() - 1), (int) std::round (v)); return c.isEmpty() ? juce::String (i) : c[i]; }
    if (pi.kind == Toggle) return v >= 0.5f ? "ON" : "OFF";
    if (juce::String (pi.unit) == "2^n") return juce::String (1 << (int) std::round (v)) + " @48k";
    juce::String s = pi.integer ? juce::String ((int) std::round (v)) : juce::String (v, std::abs (v) < 10 ? 2 : 1);
    if (juce::String (pi.name) == "bpm" && v <= 0) return "host";
    return juce::String (pi.unit).isEmpty() ? s : s + " " + pi.unit;
}
// ================================================================ palettes
Palette Palette::dark()
{
    Palette p; const juce::Colour screen (0xff07080c);
    p.bg = col::bg; p.card = col::card; p.cell = screen;
    p.text = col::white; p.text2 = col::dim; p.dim = col::dim;
    p.accent = col::slime; p.accent2 = col::ecto;
    p.waveBg = screen; p.wave = col::ecto; p.waveSel = col::slime; p.waveText = col::white; p.waveDim = col::dim; p.waveGrid = col::white;
    p.selection = col::slime; p.regionEdge = col::slime; p.cursor = col::slime; p.play = col::white; p.loop = col::ice; p.preview = col::pink; p.bar = col::gold;
    p.envLine = col::gold; p.envPoint = col::gold; p.envPointActive = col::white;
    p.opOn = col::slime; p.opOff = col::dim; p.opSel = col::ecto; p.opText = col::white; p.opTextOff = col::dim;
    p.seed = col::gold; p.info = col::ice; p.warn = juce::Colour (0xffff5555); p.progress = col::slime;
    p.button = col::card2; p.buttonOn = col::slime; p.buttonText = col::white; p.buttonTextOn = col::bg;
    p.comboBg = juce::Colour (0xff06070a); p.comboText = col::white; p.comboOutline = col::slime.withAlpha (0.35f);
    p.sliderTrack = col::ecto.withAlpha (0.6f); p.sliderBg = screen; p.labelBg = screen; p.labelText = col::white;
    p.popupBg = col::card; p.popupHighlight = col::ecto; p.popupText = col::white; p.popupHighlightText = col::white;
    p.textEditBg = screen; p.textEditText = col::white; p.textEditHighlight = col::ecto.withAlpha (0.4f); p.toggleTick = col::slime;
    return p;
}
Palette Palette::light()
{
    Palette p;
    const juce::Colour window (0xffDCD6E6), panel (0xffE6E1EE), cell (0xffD3CDDF), body (0xff1B1830), second (0xff5A556C), dimC (0xffB5AFC4),
                       purple (0xff6D3FF0), purple2 (0xff7C5CE0), green (0xff0B7F46), pink (0xffB83383), amber (0xff9A6700), blue (0xff2358D6),
                       red (0xffC92222), cyan (0xff0A6E82), screen (0xffF4F1F9);
    p.bg = window; p.card = panel; p.cell = cell;
    p.text = body; p.text2 = second; p.dim = dimC;
    p.accent = green; p.accent2 = purple;
    p.waveBg = screen; p.wave = purple2; p.waveSel = green; p.waveText = body; p.waveDim = second; p.waveGrid = body;
    p.selection = green; p.regionEdge = green; p.cursor = red; p.play = body; p.loop = cyan; p.preview = pink; p.bar = amber;
    p.envLine = amber; p.envPoint = amber; p.envPointActive = body;
    p.opOn = green; p.opOff = second; p.opSel = purple2; p.opText = body; p.opTextOff = second;
    p.seed = amber; p.info = blue; p.warn = red; p.progress = green;
    p.button = cell; p.buttonOn = purple; p.buttonText = body; p.buttonTextOn = panel;
    p.comboBg = cell; p.comboText = body; p.comboOutline = purple2;
    p.sliderTrack = purple.withAlpha (0.35f); p.sliderBg = cell; p.labelBg = cell; p.labelText = body;
    p.popupBg = panel; p.popupHighlight = purple; p.popupText = body; p.popupHighlightText = panel;
    p.textEditBg = panel; p.textEditText = body; p.textEditHighlight = purple2.withAlpha (0.3f); p.toggleTick = purple;
    return p;
}
float contrastRatio (juce::Colour a, juce::Colour b)
{
    auto lum = [] (juce::Colour c)
    {
        auto ch = [] (juce::uint8 v) { const double x = v / 255.0; return x <= 0.03928 ? x / 12.92 : std::pow ((x + 0.055) / 1.055, 2.4); };
        return 0.2126 * ch (c.getRed()) + 0.7152 * ch (c.getGreen()) + 0.0722 * ch (c.getBlue());
    };
    const double la = lum (a), lb = lum (b);
    return (float) ((juce::jmax (la, lb) + 0.05) / (juce::jmin (la, lb) + 0.05));
}

static float shapeT (float t, float curve) { return curve != 0.0f ? std::pow (juce::jlimit (0.0f, 1.0f, t), std::pow (4.0f, -curve)) : t; }
static double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }

// ================================================================ waveform + region + ruler + preview lane
class ProcessLabPanel::Wave : public juce::Component {
public:
    explicit Wave (ProcessLabPanel& o) : owner (o) {}
    uint32_t version = 1;
    void rebuild() { ++version; coarseLen = -1; repaint(); }
    static constexpr int rulerH = 16, kCoarse = 256;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const auto& P = owner.palette;
        g.setColour (P.waveBg); g.fillRoundedRectangle (r, 8);
        const Audio& a = owner.session.audio; const int n = a.length();
        if (n == 0) { g.setColour (P.waveDim); g.setFont (font (14)); g.drawText ("no sound: load or record one in the host", getLocalBounds(), juce::Justification::centred); return; }
        ensureCoarse (a);
        const int W = juce::jmax (1, getWidth());
        drawRuler (g, a);
        const Region reg = owner.session.region;
        const bool hasPrev = owner.session.previewValid();
        const float top = (float) rulerH, H = r.getHeight() - top;
        const float waveH = hasPrev ? H * 0.58f : H;
        const float x0 = xOf (reg.start), x1 = xOf (reg.end());
        g.setColour (P.selection.withAlpha (0.10f)); g.fillRect (juce::Rectangle<float> (x0, top, juce::jmax (1.0f, x1 - x0), waveH));
        const int nCh = juce::jmax (1, a.channels()); const float laneH = waveH / (float) nCh;
        for (int c = 0; c < a.channels(); ++c)
        {
            const float mid = top + laneH * ((float) c + 0.5f), half = laneH * 0.45f;
            g.setColour (P.waveGrid.withAlpha (0.05f)); g.drawHorizontalLine ((int) mid, 0, (float) W);
            for (int x = 0; x < W; ++x)
            {
                float lo, hi; minMax (a, c, sampleAtX ((float) x), sampleAtX ((float) x + 1), lo, hi);
                const bool in = x >= (int) x0 && x <= (int) x1;
                g.setColour (in ? P.waveSel : P.wave.withAlpha (0.75f));
                g.drawVerticalLine (x, mid - juce::jmax (hi, 0.004f) * half, mid - juce::jmin (lo, -0.004f) * half);
            }
        }
        if (hasPrev) drawPreview (g, a, top + waveH + 3, r.getBottom() - 2, reg);
        // region edges + fade handles
        g.setColour (reg.empty() ? P.cursor : P.regionEdge); g.drawVerticalLine ((int) x0, top, top + waveH); g.drawVerticalLine ((int) juce::jmax (x0, x1 - 1), top, top + waveH);
        if (auto* op = selectedFade(); op != nullptr && ! reg.empty())
        {
            const bool in = op->type == OpType::FadeIn;
            const int L = juce::jlimit (1, reg.length, (int) std::round (op->p[0] * 0.001 * a.rate));
            const float hx = in ? xOf (reg.start + L) : xOf (reg.end() - L);
            juce::Path p; const int steps = 48;
            for (int i = 0; i <= steps; ++i)
            {
                const float t = (float) i / steps; float gval;
                { const float tt = t; const int type = (int) op->p[2]; const float sh = op->p[1];
                  if (type == 1) gval = tt * tt * (3 - 2 * tt); else if (type == 2) gval = std::sin (tt * juce::MathConstants<float>::halfPi);
                  else gval = std::abs (sh) < 1e-6f ? tt : sh > 0 ? std::pow (tt, 1.0f / (1 + 3 * sh)) : std::pow (tt, 1 - 3 * sh); }
                const float sx = in ? xOf (reg.start + (int) (t * L)) : xOf (reg.end() - (int) (t * L));
                const float sy = top + waveH - gval * (waveH - 4);
                if (i == 0) p.startNewSubPath (sx, sy); else p.lineTo (sx, sy);
            }
            g.setColour (P.envLine); g.strokePath (p, juce::PathStrokeType (1.6f));
            g.fillEllipse (hx - 5, top + 2, 10, 10);
        }
        // playhead
        const float ph = owner.playhead01();
        if (ph >= 0.0f)
        {
            float px;
            if (owner.playIsPreview && hasPrev) px = xOf (reg.start + (int) (ph * (float) owner.session.previewAudio->length()));
            else px = xOf (reg.start + (int) (ph * (float) reg.length));
            g.setColour (P.play); g.drawVerticalLine ((int) px, top, r.getBottom());
        }
        g.setFont (font (10)); g.setColour (P.waveText.withAlpha (0.8f));
        g.drawText ((reg.empty() ? "CURSOR " + juce::String (reg.start / a.rate, 3) + " s" : "REGION " + juce::String (reg.start / a.rate, 3) + " s  +" + juce::String (reg.length / a.rate, 3) + " s")
                    + "   of " + juce::String (n / a.rate, 2) + " s   " + juce::String ((int) a.rate) + " Hz  " + juce::String (a.channels()) + " ch",
                    getLocalBounds().withTrimmedTop (rulerH).reduced (8, 3), juce::Justification::topRight);
        g.setColour (P.regionEdge.withAlpha (0.3f)); g.drawRoundedRectangle (r.reduced (0.5f), 8, 1);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        owner.grabKeyboardFocus(); dragged = false; fadeDrag = false;
        const Region reg = owner.session.region;
        if (auto* op = selectedFade(); op != nullptr && ! reg.empty())
        {
            const bool in = op->type == OpType::FadeIn; const int L = (int) std::round (op->p[0] * 0.001 * owner.session.audio.rate);
            const float hx = in ? xOf (reg.start + L) : xOf (reg.end() - L);
            if (std::abs (e.position.x - hx) < 7 && e.position.y < rulerH + 18) { fadeDrag = true; return; }
        }
        if (e.mods.isShiftDown() && ! reg.empty())
        {
            const int s = sampleAtX (e.position.x);
            anchor = std::abs (s - reg.start) < std::abs (s - reg.end()) ? reg.end() : reg.start;
            drag (e); return;
        }
        anchor = owner.snapPosition (sampleAtX (e.position.x));
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (fadeDrag)
        {
            auto& ops = owner.session.chain.ops; const Region reg = owner.session.region; auto& op = ops[(size_t) owner.selOp];
            const int s = sampleAtX (e.position.x);
            const int L = op.type == OpType::FadeIn ? s - reg.start : reg.end() - s;
            op.p[0] = (float) juce::jlimit (1.0, 10000.0, 1000.0 * juce::jlimit (1, reg.length, L) / owner.session.audio.rate);
            owner.session.chainChanged(); owner.repaint(); return;
        }
        if (e.getDistanceFromDragStart() > 2) dragged = true;
        if (dragged) drag (e);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! dragged && ! fadeDrag && ! e.mods.isShiftDown()) { owner.session.setRegion ({ anchor, 0 }); repaint(); }   // click = cursor
        fadeDrag = false;
    }
    void mouseDoubleClick (const juce::MouseEvent&) override { owner.session.setRegion (Region::all (owner.session.audio)); repaint(); }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const int n = owner.session.audio.length(); if (n == 0) return;
        if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
        {
            const float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
            owner.setView (owner.viewStart - (int) (d * 0.5f * (float) owner.viewLen), owner.viewLen); return;
        }
        const int at = sampleAtX (e.position.x);
        const double k = w.deltaY > 0 ? 0.8 : 1.25;
        const int newLen = (int) juce::jlimit (32.0, (double) n, owner.viewLen * k);
        const double frac = (double) (at - owner.viewStart) / juce::jmax (1, owner.viewLen);
        owner.setView (at - (int) (frac * newLen), newLen);
    }
    float xOf (int sample) const { return owner.viewLen > 0 ? (float) (sample - owner.viewStart) * (float) getWidth() / (float) owner.viewLen : 0.0f; }
    int sampleAtX (float x) const
    {
        const int n = owner.session.audio.length();
        return juce::jlimit (0, n, owner.viewStart + (int) std::llround ((double) x / juce::jmax (1, getWidth()) * owner.viewLen));
    }
private:
    Op* selectedFade() const
    {
        auto& ops = owner.session.chain.ops;
        if (owner.selOp < 0 || owner.selOp >= (int) ops.size()) return nullptr;
        auto& op = ops[(size_t) owner.selOp];
        return op.type == OpType::FadeIn || op.type == OpType::FadeOut ? &op : nullptr;
    }
    void drag (const juce::MouseEvent& e)
    {
        const int s = owner.snapPosition (sampleAtX (e.position.x));
        owner.session.setRegion ({ juce::jmin (anchor, s), std::abs (s - anchor) }); repaint();
    }
    void ensureCoarse (const Audio& a)
    {
        if (coarseLen == a.length() && (int) coarse.size() == a.channels()) return;
        coarseLen = a.length(); coarse.assign ((size_t) a.channels(), {});
        const int nb = (a.length() + kCoarse - 1) / kCoarse;
        for (int c = 0; c < a.channels(); ++c)
        {
            auto& v = coarse[(size_t) c]; v.resize ((size_t) nb);
            for (int b = 0; b < nb; ++b)
            {
                float lo = 0, hi = 0; const int e = juce::jmin (a.length(), (b + 1) * kCoarse);
                for (int i = b * kCoarse; i < e; ++i) { const float x = a.ch[(size_t) c][(size_t) i]; lo = juce::jmin (lo, x); hi = juce::jmax (hi, x); }
                v[(size_t) b] = { lo, hi };
            }
        }
    }
    void minMax (const Audio& a, int c, int s0, int s1, float& lo, float& hi) const
    {
        lo = 0; hi = 0; s1 = juce::jmax (s0 + 1, s1); const int n = a.length();
        if (s1 - s0 >= kCoarse * 2 && c < (int) coarse.size())
        {
            for (int b = s0 / kCoarse; b <= (s1 - 1) / kCoarse && b < (int) coarse[(size_t) c].size(); ++b) { lo = juce::jmin (lo, coarse[(size_t) c][(size_t) b].first); hi = juce::jmax (hi, coarse[(size_t) c][(size_t) b].second); }
            return;
        }
        for (int i = juce::jmax (0, s0); i < s1 && i < n; ++i) { const float x = a.ch[(size_t) c][(size_t) i]; lo = juce::jmin (lo, x); hi = juce::jmax (hi, x); }
    }
    void drawRuler (juce::Graphics& g, const Audio& a)
    {
        const double secsPerPx = owner.viewLen / a.rate / juce::jmax (1, getWidth());
        static const double steps[] = { 0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300 };
        double step = steps[0]; for (double s : steps) { step = s; if (s / secsPerPx >= 70) break; }
        const double t0 = owner.viewStart / a.rate, t1 = (owner.viewStart + owner.viewLen) / a.rate;
        g.setFont (font (9, false));
        const double bpm = owner.host.tempoBpm();
        if (bpm > 0)
        {
            const double bar = 4 * 60.0 / bpm; double bstep = bar; while (bstep / secsPerPx < 40) bstep *= 2;
            for (double t = std::ceil (t0 / bstep) * bstep; t <= t1; t += bstep)
            {
                const float x = xOf ((int) std::llround (t * a.rate));
                g.setColour (owner.palette.bar.withAlpha (0.5f)); g.drawVerticalLine ((int) x, (float) rulerH - 6, (float) getHeight());
                g.setColour (owner.palette.bar); g.drawText ("bar " + juce::String ((int) std::llround (t / bar) + 1), (int) x + 2, rulerH - 8, 60, 8, juce::Justification::left);
            }
        }
        for (double t = std::ceil (t0 / step) * step; t <= t1; t += step)
        {
            const float x = xOf ((int) std::llround (t * a.rate));
            g.setColour (owner.palette.waveDim); g.drawVerticalLine ((int) x, 2, (float) rulerH);
            g.drawText (juce::String (t, step < 0.01 ? 3 : step < 1 ? 2 : 0) + "s", (int) x + 2, 0, 60, 9, juce::Justification::left);
        }
    }
    void drawPreview (juce::Graphics& g, const Audio& a, float top, float bottom, Region reg)
    {
        const Audio& p = *owner.session.previewAudio; const int pn = p.length(), W = getWidth();
        const auto& P = owner.palette;
        g.setColour (P.preview.withAlpha (0.06f)); g.fillRect (juce::Rectangle<float> (0, top, (float) W, bottom - top));
        const int nCh = juce::jmax (1, a.channels()); const float laneH = (bottom - top) / (float) nCh;
        // the result spliced in context on the SAME time scale: original before | preview | original after (shifted)
        for (int c = 0; c < a.channels(); ++c)
        {
            const float mid = top + laneH * ((float) c + 0.5f), half = laneH * 0.45f;
            const auto& pc = p.ch[(size_t) juce::jmin (c, p.channels() - 1)];
            for (int x = 0; x < W; ++x)
            {
                const int s0 = sampleAtXRaw ((float) x), s1 = juce::jmax (s0 + 1, sampleAtXRaw ((float) x + 1));
                float lo = 0, hi = 0; bool inPrev = false;
                if (s1 <= reg.start) minMax (a, c, s0, s1, lo, hi);
                else if (s0 >= reg.start + pn) minMax (a, c, s0 - pn + reg.length, s1 - pn + reg.length, lo, hi);
                else
                {
                    inPrev = true; const int st = juce::jmax (1, (s1 - s0) / 512);
                    for (int i = juce::jmax (s0, reg.start); i < s1 && i < reg.start + pn; i += st) { const float v = pc[(size_t) (i - reg.start)]; lo = juce::jmin (lo, v); hi = juce::jmax (hi, v); }
                }
                g.setColour (inPrev ? P.preview : P.waveDim.withAlpha (0.45f));
                g.drawVerticalLine (x, mid - juce::jmax (hi, 0.004f) * half, mid - juce::jmin (lo, -0.004f) * half);
            }
        }
        g.setColour (P.preview); g.drawVerticalLine ((int) xOf (reg.start), top, bottom); g.drawVerticalLine ((int) xOf (reg.start + pn), top, bottom);
        g.setFont (font (10));
        g.drawText ("PREVIEW  " + juce::String (pn / p.rate, 2) + " s  (in context)", juce::Rectangle<float> (6, top + 1, 260, 12), juce::Justification::left);
    }
    int sampleAtXRaw (float x) const { return owner.viewStart + (int) std::llround ((double) x / juce::jmax (1, getWidth()) * owner.viewLen); }
    ProcessLabPanel& owner;
    std::vector<std::vector<std::pair<float, float>>> coarse; int coarseLen = -1;
    int anchor = 0; bool dragged = false, fadeDrag = false;
};

// ================================================================ op chain list
class ProcessLabPanel::OpList : public juce::Component {
public:
    explicit OpList (ProcessLabPanel& o) : owner (o) {}
    static constexpr int rowH = 22;
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const auto& P = owner.palette;
        g.setColour (P.card); g.fillRoundedRectangle (r, 8);
        const auto& ops = owner.session.chain.ops;
        if (ops.empty())
        {
            g.setColour (P.text2); g.setFont (font (12, false));
            g.drawFittedText ("empty chain\n+ ADD a process", getLocalBounds().reduced (8), juce::Justification::centred, 3);
        }
        for (int i = scroll; i < (int) ops.size(); ++i)
        {
            const auto row = rowRect (i); if (row.getY() > getHeight()) break;
            const bool sel = i == owner.selOp, on = ops[(size_t) i].enabled;
            if (sel) { g.setColour (P.opSel.withAlpha (0.35f)); g.fillRoundedRectangle (row.toFloat().reduced (2, 1), 5); }
            g.setColour (on ? P.opOn : P.opOff.withAlpha (0.5f)); g.fillEllipse (juce::Rectangle<float> ((float) row.getX() + 8, (float) row.getCentreY() - 5, 10, 10));
            g.setFont (font (12)); g.setColour (on ? P.opText : P.opTextOff);
            const auto& I = info (ops[(size_t) i].type);
            g.drawText (juce::String (i + 1) + "  " + I.name, row.withTrimmedLeft (26), juce::Justification::centredLeft);
            g.setFont (font (9, false)); g.setColour (P.text2);
            g.drawText (I.group, row.withTrimmedRight (8), juce::Justification::centredRight);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        owner.grabKeyboardFocus();
        const int i = scroll + (e.y - 4) / rowH;
        auto& ops = owner.session.chain.ops;
        if (i < 0 || i >= (int) ops.size()) return;
        if (e.x < 24) { ops[(size_t) i].enabled = ! ops[(size_t) i].enabled; owner.session.chainChanged(); }
        owner.selectOp (i);
    }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        const int maxScroll = juce::jmax (0, (int) owner.session.chain.ops.size() - getHeight() / rowH);
        scroll = juce::jlimit (0, maxScroll, scroll + (w.deltaY < 0 ? 1 : -1)); repaint();
    }
    void ensureVisible (int i)
    {
        const int vis = juce::jmax (1, (getHeight() - 8) / rowH);
        if (i < scroll) scroll = i; else if (i >= scroll + vis) scroll = i - vis + 1;
        scroll = juce::jmax (0, scroll);
    }
private:
    juce::Rectangle<int> rowRect (int i) const { return { 4, 4 + (i - scroll) * rowH, getWidth() - 8, rowH }; }
    ProcessLabPanel& owner; int scroll = 0;
};

// ================================================================ breakpoint envelope editor
class ProcessLabPanel::EnvEditor : public juce::Component {
public:
    explicit EnvEditor (ProcessLabPanel& o) : owner (o) {}
    std::function<void()> onChange;
    std::vector<EnvPoint>* env = nullptr;
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const auto& P = owner.palette;
        g.setColour (P.waveBg); g.fillRoundedRectangle (r, 6);
        drawRegionWave (g);
        g.setColour (P.waveGrid.withAlpha (0.06f));
        for (int i = 1; i < 8; ++i) g.drawHorizontalLine ((int) (r.getHeight() * (float) i / 8.0f), 0, r.getWidth());
        for (int i = 1; i < 16; ++i) g.drawVerticalLine ((int) (4 + (r.getWidth() - 8) * (float) i / 16.0f), 0, r.getHeight());
        if (env == nullptr || env->empty()) return;
        juce::Path p; bool first = true;
        for (size_t i = 0; i < env->size(); ++i)
        {
            const auto& a = (*env)[i];
            if (first) { p.startNewSubPath (toPx (a)); first = false; }
            if (i + 1 < env->size())
            {
                const auto& b = (*env)[i + 1];
                for (int k = 1; k <= 24; ++k) { const float t = (float) k / 24.0f; p.lineTo (toPx ({ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * shapeT (t, a.curve), 0 })); }
            }
        }
        g.setColour (P.envLine); g.strokePath (p, juce::PathStrokeType (2.0f));
        for (size_t i = 0; i < env->size(); ++i) { const auto q = toPx ((*env)[i]); g.setColour ((int) i == dragIdx ? P.envPointActive : P.envPoint); g.fillEllipse (q.x - 4, q.y - 4, 8, 8); }
        g.setColour (P.waveDim); g.setFont (font (9, false));
        g.drawText ("click add | drag move (shift: grid) | ctrl-drag: curve | right-click delete", getLocalBounds().reduced (6, 2), juce::Justification::topRight);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (env == nullptr) return;
        dragIdx = nearest (e.position); startY = e.position.y; startCurve = dragIdx >= 0 ? (*env)[(size_t) dragIdx].curve : 0.0f;
        if (e.mods.isPopupMenu())
        {
            if (dragIdx >= 0 && env->size() > 2) { env->erase (env->begin() + dragIdx); changed(); }
            dragIdx = -1; return;
        }
        if (dragIdx < 0 && (int) env->size() < kMaxEnvPoints)
        {
            auto v = fromPx (e.position, e.mods.isShiftDown());
            auto it = std::lower_bound (env->begin(), env->end(), v.x, [] (const EnvPoint& a, float x) { return a.x < x; });
            dragIdx = (int) (env->insert (it, v) - env->begin()); changed();
        }
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (env == nullptr || dragIdx < 0 || dragIdx >= (int) env->size()) return;
        auto& pt = (*env)[(size_t) dragIdx];
        if (e.mods.isCommandDown() || e.mods.isCtrlDown()) { pt.curve = juce::jlimit (-1.0f, 1.0f, startCurve + (startY - e.position.y) / 60.0f); changed(); return; }
        auto v = fromPx (e.position, e.mods.isShiftDown());
        const float lo = dragIdx > 0 ? (*env)[(size_t) dragIdx - 1].x : 0.0f, hi = dragIdx + 1 < (int) env->size() ? (*env)[(size_t) dragIdx + 1].x : 1.0f;
        pt.x = juce::jlimit (lo, hi, v.x); pt.y = v.y; changed();
    }
    void mouseUp (const juce::MouseEvent&) override { dragIdx = -1; repaint(); }
private:
    void drawRegionWave (juce::Graphics& g)
    {
        const Audio& a = owner.session.audio; const Region reg = owner.session.region;
        if (a.channels() == 0 || reg.empty()) return;
        const int W = juce::jmax (1, getWidth() - 8); const float mid = (float) getHeight() * 0.5f, half = (float) getHeight() * 0.45f;
        const auto& x = a.ch[0];
        g.setColour (owner.palette.wave.withAlpha (0.25f));
        for (int px = 0; px < W; ++px)
        {
            const int s0 = reg.start + (int) ((int64_t) px * reg.length / W), s1 = juce::jmax (s0 + 1, reg.start + (int) ((int64_t) (px + 1) * reg.length / W));
            const int st = juce::jmax (1, (s1 - s0) / 256); float lo = 0, hi = 0;
            for (int i = s0; i < s1; i += st) { lo = juce::jmin (lo, x[(size_t) i]); hi = juce::jmax (hi, x[(size_t) i]); }
            g.drawVerticalLine (px + 4, mid - hi * half, mid - lo * half);
        }
    }
    void changed() { repaint(); if (onChange) onChange(); }
    juce::Point<float> toPx (EnvPoint v) const { return { 4 + v.x * ((float) getWidth() - 8), 4 + (1 - v.y) * ((float) getHeight() - 8) }; }
    EnvPoint fromPx (juce::Point<float> p, bool snap) const
    {
        EnvPoint e { juce::jlimit (0.0f, 1.0f, (p.x - 4) / juce::jmax (1.0f, (float) getWidth() - 8)), juce::jlimit (0.0f, 1.0f, 1 - (p.y - 4) / juce::jmax (1.0f, (float) getHeight() - 8)), 0.0f };
        if (snap) { e.x = std::round (e.x * 16) / 16; e.y = std::round (e.y * 8) / 8; }
        return e;
    }
    int nearest (juce::Point<float> p) const
    {
        int best = -1; float bd = 9.0f;
        for (size_t i = 0; i < env->size(); ++i) { const float d = toPx ((*env)[i]).getDistanceFrom (p); if (d < bd) { bd = d; best = (int) i; } }
        return best;
    }
    ProcessLabPanel& owner;
    int dragIdx = -1; float startY = 0, startCurve = 0;
};

// ================================================================ parameter editor for the selected op
class ProcessLabPanel::Params : public juce::Component {
public:
    explicit Params (ProcessLabPanel& o) : owner (o), env (o)
    {
        for (int i = 0; i < kMaxParams; ++i)
        {
            auto& s = sliders[(size_t) i];
            s.setSliderStyle (juce::Slider::LinearBar); s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            s.onValueChange = [this, i] { changed (i, (float) sliders[(size_t) i].getValue()); };
            addChildComponent (s);
            auto& t = toggles[(size_t) i]; t.setClickingTogglesState (true); t.setTooltip ("on / off for this process");
            t.onClick = [this, i] { changed (i, toggles[(size_t) i].getToggleState() ? 1.0f : 0.0f); };
            addChildComponent (t);
            auto& c = combos[(size_t) i];
            c.onChange = [this, i] { if (combos[(size_t) i].getSelectedId() > 0) changed (i, (float) (combos[(size_t) i].getSelectedId() - 1)); };
            addChildComponent (c);
        }
        env.onChange = [this] { owner.session.chainChanged(); };
        addChildComponent (env);
        presetB.onClick = [this] { presets(); }; presetB.setTooltip ("ready-made shapes for this envelope");
        addChildComponent (presetB);
    }
    void show (int opIndex)
    {
        shown = opIndex;
        auto& ops = owner.session.chain.ops;
        const bool valid = opIndex >= 0 && opIndex < (int) ops.size();
        const OpInfo* I = valid ? &info (ops[(size_t) opIndex].type) : nullptr;
        for (int i = 0; i < kMaxParams; ++i)
        {
            auto& s = sliders[(size_t) i]; auto& t = toggles[(size_t) i]; auto& c = combos[(size_t) i];
            const bool vis = I != nullptr && i < I->numParams;
            const int kind = vis ? I->params[i].kind : -1;
            s.setVisible (kind == Continuous); t.setVisible (kind == Toggle); c.setVisible (kind == Choice);
            if (! vis) continue;
            const auto& pi = I->params[i]; const float v = ops[(size_t) opIndex].p[(size_t) i];
            if (kind == Continuous)
            {
                s.setRange (pi.min, pi.max, pi.integer ? 1.0 : 0.0);
                if (pi.max / juce::jmax (1.0e-3f, pi.min) > 100.0f && pi.min > 0) s.setSkewFactorFromMidPoint (std::sqrt (pi.min * pi.max)); else s.setSkewFactor (1.0);
                s.setDoubleClickReturnValue (true, pi.def);
                s.setValue (v, juce::dontSendNotification);
            }
            else if (kind == Toggle) { t.setButtonText (juce::String (pi.name).toUpperCase()); t.setTooltip (juce::String (pi.name) + ": on / off for this process"); t.setToggleState (v >= 0.5f, juce::dontSendNotification); }
            else
            {
                c.clear (juce::dontSendNotification); auto names = choiceNames (pi);
                for (int k = 0; k < names.size(); ++k) c.addItem (names[k], k + 1);
                c.setSelectedId ((int) std::round (v) + 1, juce::dontSendNotification);
            }
        }
        env.env = I != nullptr && I->hasEnvelope ? &ops[(size_t) opIndex].env : nullptr;
        env.setVisible (env.env != nullptr); presetB.setVisible (env.env != nullptr);
        resized(); repaint();
    }
    void paint (juce::Graphics& g) override
    {
        const auto& P = owner.palette;
        g.setColour (P.card); g.fillRoundedRectangle (getLocalBounds().toFloat(), 8);
        auto& ops = owner.session.chain.ops;
        if (shown < 0 || shown >= (int) ops.size())
        {
            g.setColour (P.text2); g.setFont (font (12, false));
            g.drawFittedText ("select a process in the chain to edit it", getLocalBounds().reduced (8), juce::Justification::centred, 2);
            return;
        }
        const auto& op = ops[(size_t) shown]; const auto& I = info (op.type);
        g.setColour (P.accent); g.setFont (font (13)); g.drawText (I.name, 10, 5, getWidth() - 20, 16, juce::Justification::left);
        g.setColour (P.text2); g.setFont (font (10, false)); g.drawText (I.blurb, 10, 20, getWidth() - 20, 14, juce::Justification::left, true);
        for (int i = 0; i < I.numParams; ++i)
        {
            const auto& pi = I.params[i];
            const juce::Component* cmp = pi.kind == Toggle ? (const juce::Component*) &toggles[(size_t) i] : pi.kind == Choice ? (const juce::Component*) &combos[(size_t) i] : &sliders[(size_t) i];
            const auto b = cmp->getBounds();
            if (pi.kind != Toggle)
            {
                g.setColour (P.text); g.setFont (font (10));
                g.drawText (juce::String (pi.name).toUpperCase(), b.getX() - labelW, b.getY(), labelW - 6, b.getHeight(), juce::Justification::centredRight);
            }
            if (pi.kind == Continuous) { g.setColour (P.accent); g.drawText (fmtParam (pi, op.p[(size_t) i]), b.reduced (6, 0), juce::Justification::centredRight); }
        }
        if (I.usesSeed && lastBottom + 14 <= getHeight()) { g.setColour (P.text2); g.setFont (font (9, false)); g.drawText ("seeded: change SEED for another take", getLocalBounds().reduced (10, 2), juce::Justification::bottomLeft); }
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (10).withTrimmedTop (26);
        int nVis = 0; for (int i = 0; i < kMaxParams; ++i) if (sliders[(size_t) i].isVisible() || toggles[(size_t) i].isVisible() || combos[(size_t) i].isVisible()) ++nVis;
        const int avail = r.getHeight() - (env.isVisible() ? 60 : 0);
        const bool twoCols = (nVis > 5 && r.getWidth() > 420) || (nVis > 1 && nVis * 18 > avail && r.getWidth() > 340);
        const int rows = twoCols ? (nVis + 1) / 2 : nVis;
        const int h = juce::jmin (22, juce::jmax (14, (r.getHeight() - (env.isVisible() ? 60 : 8)) / juce::jmax (1, rows) - 3));
        int k = 0; const int colW = twoCols ? r.getWidth() / 2 : r.getWidth(); lastBottom = 0;
        for (int i = 0; i < kMaxParams; ++i)
        {
            juce::Component* c = sliders[(size_t) i].isVisible() ? (juce::Component*) &sliders[(size_t) i] : toggles[(size_t) i].isVisible() ? (juce::Component*) &toggles[(size_t) i] : combos[(size_t) i].isVisible() ? (juce::Component*) &combos[(size_t) i] : nullptr;
            if (c == nullptr) continue;
            const int col = twoCols ? k % 2 : 0, row = twoCols ? k / 2 : k; ++k;
            juce::Rectangle<int> cell (r.getX() + col * colW, r.getY() + row * (h + 4), colW - (twoCols ? 8 : 0), h);
            c->setBounds (c == &toggles[(size_t) i] ? cell.withTrimmedLeft (labelW).withWidth (juce::jmin (140, cell.getWidth() - labelW)) : cell.withTrimmedLeft (labelW));
            lastBottom = juce::jmax (lastBottom, cell.getBottom());
        }
        if (env.isVisible())
        {
            auto er = r.withTrimmedTop (rows * (h + 4)).withTrimmedBottom (12);
            presetB.setBounds (er.removeFromLeft (labelW - 6).removeFromTop (22));
            env.setBounds (er.withTrimmedLeft (6));
        }
    }
    void forceRefresh() { show (shown); }
    void applyPalette (const Palette& P)
    {
        for (auto& sl : sliders) { sl.setColour (juce::Slider::trackColourId, P.sliderTrack); sl.setColour (juce::Slider::backgroundColourId, P.sliderBg); }
        repaint(); env.repaint();
    }
    int shownOp() const { return shown; }
private:
    void changed (int i, float v)
    {
        auto& ops = owner.session.chain.ops; if (shown < 0 || shown >= (int) ops.size()) return;
        ops[(size_t) shown].p[(size_t) i] = v;
        owner.session.chainChanged(); repaint(); owner.wave->repaint();
    }
    void presets()
    {
        juce::PopupMenu m; const char* names[] = { "SWELL", "DECAY", "GATE x4", "GATE x8", "RAMP UP", "TREMOLO x8", "DUCK", "FLAT" };
        for (int i = 0; i < 8; ++i) m.addItem (i + 1, names[i]);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetB), [safe = juce::Component::SafePointer<Params> (this)] (int r)
        {
            if (safe == nullptr || r <= 0 || safe->env.env == nullptr) return;
            auto& e = *safe->env.env; e.clear();
            switch (r)
            {
                case 1: e = { { 0, 0, 0.6f }, { 0.85f, 1, 0 }, { 1, 0, 0 } }; break;
                case 2: e = { { 0, 1, -0.7f }, { 1, 0, 0 } }; break;
                case 3: case 4: { const int k = r == 3 ? 4 : 8; for (int i = 0; i < k; ++i) { const float a = (float) i / k, b = (float) (i + 1) / k; e.push_back ({ a, 1, 0 }); e.push_back ({ a + (b - a) * 0.5f, 1, 0 }); e.push_back ({ a + (b - a) * 0.55f, 0, 0 }); e.push_back ({ b - 0.001f, 0, 0 }); } break; }
                case 5: e = { { 0, 0, 0 }, { 1, 1, 0 } }; break;
                case 6: for (int i = 0; i <= 16; ++i) e.push_back ({ (float) i / 16, (i % 2) ? 0.35f : 1.0f, 0 }); break;
                case 7: e = { { 0, 0, 0.5f }, { 0.35f, 1, 0 }, { 1, 1, 0 } }; break;
                default: e = { { 0, 1, 0 }, { 1, 1, 0 } }; break;
            }
            safe->env.repaint(); safe->owner.session.chainChanged();
        });
    }
    static constexpr int labelW = 78;
    ProcessLabPanel& owner; int shown = -1, lastBottom = 0;
    std::array<juce::Slider, kMaxParams> sliders;
    std::array<juce::ToggleButton, kMaxParams> toggles;
    std::array<juce::ComboBox, kMaxParams> combos;
    EnvEditor env;
    juce::TextButton presetB { "PRESET" };
};

// ================================================================ drag-out button
class ProcessLabPanel::DragButton : public juce::TextButton {
public:
    explicit DragButton (ProcessLabPanel& o) : juce::TextButton ("DRAG"), owner (o) { setTooltip ("drag the preview (or the region) into your DAW"); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging || e.getDistanceFromDragStart() < 6) return;
        dragging = true;
        const auto f = owner.writeDragFile();
        if (f.existsAsFile()) ghosty::exportgate::dragFiles ({ f.getFullPathName() }, this);
    }
    void mouseUp (const juce::MouseEvent& e) override { dragging = false; juce::TextButton::mouseUp (e); }
private:
    ProcessLabPanel& owner; bool dragging = false;
};

// ================================================================ panel
ProcessLabPanel::ProcessLabPanel (Host& h) : host (h)
{
    setWantsKeyboardFocus (true);
    wave = std::make_unique<Wave> (*this); list = std::make_unique<OpList> (*this); params = std::make_unique<Params> (*this); dragB = std::make_unique<DragButton> (*this);
    addAndMakeVisible (*wave); addAndMakeVisible (*list); addAndMakeVisible (*params); addAndMakeVisible (*dragB);
    for (auto* b : { &addB, &delB, &upB, &downB, &dupB, &previewB, &autoB, &playSrcB, &playPrevB, &stopB, &loopB, &applyB, &bounceB, &bounceNewB, &againB, &sliceB,
                     &undoB, &redoB, &cancelB, &seedDown, &seedUp, &diceB, &allB, &editB, &zoomIn, &zoomOut, &zoomFit })
    { addAndMakeVisible (*b); b->setWantsKeyboardFocus (false); }
    loopB.setClickingTogglesState (true); autoB.setClickingTogglesState (true); autoB.setToggleState (true, juce::dontSendNotification);
    addB.setTooltip ("add a process to the chain"); delB.setTooltip ("remove the selected process"); upB.setTooltip ("move the selected process earlier in the chain");
    downB.setTooltip ("move the selected process later in the chain"); dupB.setTooltip ("duplicate the selected process"); playSrcB.setTooltip ("play the original (the region, or the whole sound)");
    playPrevB.setTooltip ("play the preview (the chain applied)"); stopB.setTooltip ("stop playing"); loopB.setTooltip ("loop what plays");
    cancelB.setTooltip ("stop the running job (Esc)"); seedDown.setTooltip ("previous seed (random processes)"); seedUp.setTooltip ("next seed (random processes)");
    diceB.setTooltip ("a random seed"); allB.setTooltip ("select the whole sound (A)"); editB.setTooltip ("edit commands: copy, cut, paste, delete, silence, ...");
    zoomIn.setTooltip ("zoom in (+)"); zoomOut.setTooltip ("zoom out (-)"); zoomFit.setTooltip ("show the whole sound (0)");
    previewB.setTooltip ("P"); applyB.setTooltip ("Enter"); bounceB.setTooltip ("Ctrl/Cmd+B: apply, then clear the chain to process again");
    againB.setTooltip ("Ctrl/Cmd+R: run the last applied chain on the current region"); autoB.setTooltip ("preview automatically after every change");
    bounceNewB.setTooltip ("process the region into a NEW sound, keep the original"); sliceB.setTooltip ("apply the chain to every slice of the sound separately");
    for (auto* l : { &seedLabel, &edgeLabel, &startLabel, &lenLabel }) { l->setJustificationType (juce::Justification::centred); l->setFont (font (12)); l->setEditable (false, true); addAndMakeVisible (*l); }
    seedLabel.setTooltip ("seed (double-click to type)"); edgeLabel.setTooltip ("edge crossfade where the result meets the file (applied only where it would click)");
    startLabel.setTooltip ("region start (s), double-click to type"); lenLabel.setTooltip ("region length (s), double-click to type");
    seedLabel.onTextChange = [this] { session.chain.seed = (uint32_t) juce::jlimit ((juce::int64) 1, (juce::int64) 4294967295LL, seedLabel.getText().getLargeIntValue()); session.chainChanged(); };
    edgeLabel.onTextChange = [this] { session.chain.edgeMs = (float) juce::jlimit (0.0, 50.0, edgeLabel.getText().getDoubleValue()); session.chainChanged(); };
    startLabel.onTextChange = [this] { const int s = (int) std::llround (startLabel.getText().getDoubleValue() * session.audio.rate); session.setRegion ({ s, session.region.length }); };
    lenLabel.onTextChange = [this] { const int l = (int) std::llround (lenLabel.getText().getDoubleValue() * session.audio.rate); session.setRegion ({ session.region.start, l }); };
    snapBox.addItemList ({ "NO SNAP", "ZERO", "HITS", "GRID" }, 1); snapBox.setTooltip ("snap the selection: zero crossings / transients / 1/16 grid"); snapBox.setSelectedId (snapMode + 1, juce::dontSendNotification);
    snapBox.onChange = [this] { snapMode = snapBox.getSelectedId() - 1; };
    snapBox.setWantsKeyboardFocus (false);
    addAndMakeVisible (snapBox);

    addB.onClick = [this] { addOpMenu(); };
    editB.onClick = [this] { editMenu(); };
    delB.onClick = [this] { auto& o = session.chain.ops; if (selOp >= 0 && selOp < (int) o.size()) { o.erase (o.begin() + selOp); selOp = juce::jmin (selOp, (int) o.size() - 1); session.chainChanged(); params->show (selOp); } };
    dupB.onClick = [this] { auto& o = session.chain.ops; if (selOp >= 0 && selOp < (int) o.size() && (int) o.size() < kMaxChainOps) { o.insert (o.begin() + selOp + 1, o[(size_t) selOp]); session.chainChanged(); selectOp (selOp + 1); } };
    upB.onClick = [this] { moveOp (-1); };
    downB.onClick = [this] { moveOp (1); };
    previewB.onClick = [this] { stopAudition(); session.preview(); };
    autoB.onClick = [this] { autoPreview = autoB.getToggleState(); };
    playSrcB.onClick = [this] { playSource(); };
    playPrevB.onClick = [this] { playPreview(); };
    stopB.onClick = [this] { stopAudition(); };
    applyB.onClick = [this] { stopAudition(); session.apply(); };
    bounceB.onClick = [this] { stopAudition(); session.bounce(); };
    bounceNewB.onClick = [this]
    {
        if (session.previewValid()) { if (host.bounceToNew (*session.previewAudio)) status = "bounced into a new sound (original kept)"; refresh(); }
        else { status = "PREVIEW first, then BOUNCE NEW"; session.preview(); refresh(); }
    };
    againB.onClick = [this] { stopAudition(); session.repeatLast(); };
    sliceB.onClick = [this] { auto s = host.slices(); if (s.empty()) { status = "the host has no slices for this sound"; refresh(); } else { stopAudition(); session.applyPerSlice (s); } };
    undoB.onClick = [this] { stopAudition(); session.undoStep(); };
    redoB.onClick = [this] { stopAudition(); session.redoStep(); };
    cancelB.onClick = [this] { session.cancel(); status = "cancelled"; refresh(); };
    seedDown.onClick = [this] { session.chain.seed = juce::jmax (1u, session.chain.seed - 1); session.chainChanged(); };
    seedUp.onClick = [this] { session.chain.seed = session.chain.seed + 1; session.chainChanged(); };
    diceB.onClick = [this] { session.chain.seed = (uint32_t) juce::Random::getSystemRandom().nextInt ({ 1, 99999 }); session.chainChanged(); };
    allB.onClick = [this] { session.setRegion (Region::all (session.audio)); };
    zoomIn.onClick = [this] { const Region r = session.region; if (! r.empty()) setView (r.start - r.length / 8, r.length + r.length / 4); else setView (viewStart + viewLen / 4, viewLen / 2); };
    zoomOut.onClick = [this] { setView (viewStart - viewLen / 2, viewLen * 2); };
    zoomFit.onClick = [this] { setView (0, session.audio.length()); };

    setOpaque (true);
    applyPaletteToChildren();
    session.onChanged = [this] { refresh(); };
    session.onMessage = [this] (const juce::String& m) { status = m; repaint (statusArea); };
    session.onCommitted = [this] (const Audio& a) { skipAuto = true; host.setBuffer (a); wave->rebuild(); if (viewStart + viewLen > a.length() || viewLen <= 0) setView (viewStart, viewLen); };
    reload();
    setSize (900, 520);
    startTimerHz (30);
}
ProcessLabPanel::~ProcessLabPanel()
{
    stopTimer();
    setLookAndFeel (nullptr);
    session.onChanged = nullptr; session.onMessage = nullptr; session.onCommitted = nullptr;
    session.cancel();
    if (auditioning) host.audition (nullptr, false);
}
void ProcessLabPanel::applyPaletteToChildren()
{
    const auto& P = palette;
    applyB.setColour (juce::TextButton::buttonColourId, P.accent.withAlpha (0.35f));
    seedLabel.setColour (juce::Label::textColourId, P.seed); edgeLabel.setColour (juce::Label::textColourId, P.info);
    startLabel.setColour (juce::Label::textColourId, P.text); lenLabel.setColour (juce::Label::textColourId, P.text);
    for (auto* l : { &seedLabel, &edgeLabel, &startLabel, &lenLabel }) l->setColour (juce::Label::backgroundColourId, P.cell);
    params->applyPalette (P);
}
void ProcessLabPanel::setPalette (const Palette& p)
{
    palette = p;
    if (ownLnf == nullptr) ownLnf = std::make_unique<juce::LookAndFeel_V4>();
    auto& L = *ownLnf;
    L.setColour (juce::ResizableWindow::backgroundColourId, p.bg);
    L.setColour (juce::TextButton::buttonColourId, p.button); L.setColour (juce::TextButton::buttonOnColourId, p.buttonOn);
    L.setColour (juce::TextButton::textColourOffId, p.buttonText); L.setColour (juce::TextButton::textColourOnId, p.buttonTextOn);
    L.setColour (juce::ComboBox::backgroundColourId, p.comboBg); L.setColour (juce::ComboBox::textColourId, p.comboText);
    L.setColour (juce::ComboBox::outlineColourId, p.comboOutline); L.setColour (juce::ComboBox::arrowColourId, p.comboText); L.setColour (juce::ComboBox::buttonColourId, p.comboBg);
    L.setColour (juce::ComboBox::focusedOutlineColourId, p.comboOutline);
    L.setColour (juce::Slider::trackColourId, p.sliderTrack); L.setColour (juce::Slider::backgroundColourId, p.sliderBg);
    L.setColour (juce::Slider::textBoxTextColourId, p.text); L.setColour (juce::Slider::thumbColourId, p.accent2);
    L.setColour (juce::Label::textColourId, p.labelText); L.setColour (juce::Label::backgroundColourId, p.labelBg);
    L.setColour (juce::Label::textWhenEditingColourId, p.textEditText); L.setColour (juce::Label::backgroundWhenEditingColourId, p.textEditBg);
    L.setColour (juce::Label::outlineWhenEditingColourId, p.accent2);
    L.setColour (juce::TextEditor::backgroundColourId, p.textEditBg); L.setColour (juce::TextEditor::textColourId, p.textEditText);
    L.setColour (juce::TextEditor::highlightColourId, p.textEditHighlight); L.setColour (juce::TextEditor::outlineColourId, p.comboOutline);
    L.setColour (juce::TextEditor::focusedOutlineColourId, p.accent2); L.setColour (juce::CaretComponent::caretColourId, p.text);
    L.setColour (juce::PopupMenu::backgroundColourId, p.popupBg); L.setColour (juce::PopupMenu::textColourId, p.popupText);
    L.setColour (juce::PopupMenu::highlightedBackgroundColourId, p.popupHighlight); L.setColour (juce::PopupMenu::highlightedTextColourId, p.popupHighlightText);
    L.setColour (juce::ToggleButton::textColourId, p.text); L.setColour (juce::ToggleButton::tickColourId, p.toggleTick);
    L.setColour (juce::ToggleButton::tickDisabledColourId, p.dim);
    L.setColour (juce::TooltipWindow::backgroundColourId, p.popupBg); L.setColour (juce::TooltipWindow::textColourId, p.popupText); L.setColour (juce::TooltipWindow::outlineColourId, p.comboOutline);
    setLookAndFeel (ownLnf.get());
    applyPaletteToChildren();
    sendLookAndFeelChange();
    repaint();
}
void ProcessLabPanel::reload()
{
    Audio a = host.getBuffer(); if (a.empty()) a.rate = host.sampleRate();
    session.tempoBpm = host.tempoBpm();
    session.setAudio (std::move (a)); wave->rebuild();
    viewStart = 0; viewLen = session.audio.length();
    seenGen = session.chainGeneration(); autoReqGen = seenGen;   // no auto-preview just for loading
    refresh();
}
juce::ValueTree ProcessLabPanel::getState() const
{
    juce::ValueTree v ("PLAB");
    v.setProperty ("undoMB", (double) session.undo.getBudget() / 1048576.0, nullptr);
    v.setProperty ("sel", selOp, nullptr);
    v.setProperty ("auto", autoPreview, nullptr);
    v.setProperty ("snap", snapMode, nullptr);
    v.appendChild (session.chain.toValueTree(), nullptr);
    return v;
}
void ProcessLabPanel::setState (const juce::ValueTree& v)
{
    if (! v.hasType ("PLAB")) return;
    session.cancel();
    session.chain = Chain::fromValueTree (v.getChildWithName ("PLCHAIN"));
    if (session.chain.seed == 0) session.chain.seed = 1;
    auto num = [&] (const char* id, double def) { const double d = (double) v.getProperty (id, def); return std::isfinite (d) ? d : def; };
    session.undo.setBudget ((size_t) (juce::jlimit (8.0, 4096.0, num ("undoMB", 192.0)) * 1048576.0));
    selOp = (int) juce::jlimit (-1.0, (double) session.chain.ops.size() - 1, num ("sel", -1.0));
    setAutoPreview (num ("auto", 1.0) >= 0.5);
    snapMode = (int) juce::jlimit (0.0, 3.0, num ("snap", (double) SnapZero)); snapBox.setSelectedId (snapMode + 1, juce::dontSendNotification);
    session.chainChanged(); params->show (selOp);
    seenGen = session.chainGeneration(); autoReqGen = seenGen;
}
void ProcessLabPanel::setAutoPreview (bool on) { autoPreview = on; autoB.setToggleState (on, juce::dontSendNotification); }
void ProcessLabPanel::selectOp (int i)
{
    selOp = juce::jlimit (-1, (int) session.chain.ops.size() - 1, i);
    list->ensureVisible (juce::jmax (0, selOp)); params->show (selOp); refresh();
}
void ProcessLabPanel::setView (int start, int len)
{
    const int n = session.audio.length();
    len = juce::jlimit (juce::jmin (n, 32), juce::jmax (1, n), len <= 0 ? n : len);
    viewLen = len; viewStart = juce::jlimit (0, juce::jmax (0, n - len), start);
    wave->repaint();
}
std::vector<int> ProcessLabPanel::transients() const
{
    const Audio& a = session.audio;
    if (onsetLen == a.length() && onsetGen == wave->version) return onsetCache;
    onsetLen = a.length(); onsetGen = wave->version; onsetCache.clear();
    if (a.length() < 1024 || a.channels() == 0) return onsetCache;
    const int hop = 256; const int F = a.length() / hop;
    std::vector<float> e ((size_t) F, 0.0f);
    for (int f = 0; f < F; ++f) { double s = 0; for (int c = 0; c < a.channels(); ++c) for (int i = f * hop; i < (f + 1) * hop; ++i) s += (double) a.ch[(size_t) c][(size_t) i] * a.ch[(size_t) c][(size_t) i]; e[(size_t) f] = (float) std::log1p (1000.0 * s / hop); }
    std::vector<float> flux ((size_t) F, 0.0f); for (int f = 1; f < F; ++f) flux[(size_t) f] = juce::jmax (0.0f, e[(size_t) f] - e[(size_t) f - 1]);
    const int minGap = juce::jmax (1, (int) (0.05 * a.rate / hop)); int last = -minGap;
    for (int f = 1; f + 1 < F; ++f)
    {
        float mean = 0; int cnt = 0; for (int k = juce::jmax (0, f - 16); k <= juce::jmin (F - 1, f + 16); ++k) { mean += flux[(size_t) k]; ++cnt; }
        mean /= (float) cnt;
        if (flux[(size_t) f] > mean * 2.5f + 0.05f && flux[(size_t) f] >= flux[(size_t) f - 1] && flux[(size_t) f] >= flux[(size_t) f + 1] && f - last >= minGap)
        {
            // refine: the first sample (within the frame before) above 10 % of the frame's peak
            int pos = (f - 1) * hop; float pk = 0;
            for (int i = (f - 1) * hop; i < (f + 1) * hop; ++i) for (auto& c : a.ch) pk = juce::jmax (pk, std::abs (c[(size_t) i]));
            for (int i = (f - 1) * hop; i < (f + 1) * hop; ++i) { float v = 0; for (auto& c : a.ch) v = juce::jmax (v, std::abs (c[(size_t) i])); if (v > 0.1f * pk) { pos = i; break; } }
            onsetCache.push_back (pos); last = f;
        }
    }
    return onsetCache;
}
int ProcessLabPanel::snapPosition (int sample) const
{
    const Audio& a = session.audio; const int n = a.length();
    sample = juce::jlimit (0, n, sample);
    if (snapMode == SnapOff || n == 0 || a.channels() == 0) return sample;
    const int pxSamples = juce::jmax (1, viewLen / juce::jmax (1, wave->getWidth()));
    if (snapMode == SnapZero)
    {
        const int range = juce::jlimit ((int) (0.01 * a.rate), 4096, pxSamples * 8);
        auto mono = [&] (int i) { float s = 0; for (auto& c : a.ch) s += c[(size_t) i]; return s; };
        for (int d = 0; d <= range; ++d)
            for (int sgn : { 1, -1 })
            {
                const int i = sample + sgn * d; if (i < 1 || i >= n) continue;
                if (mono (i - 1) < 0.0f && mono (i) >= 0.0f) return i;
            }
        return sample;
    }
    if (snapMode == SnapTransient)
    {
        const auto on = transients(); int best = sample, bd = pxSamples * 10;
        for (int t : on) if (std::abs (t - sample) < bd) { bd = std::abs (t - sample); best = t; }
        return best;
    }
    const double bpm = host.tempoBpm() > 0 ? host.tempoBpm() : 120.0;
    const double grid = 60.0 / bpm / 4.0 * a.rate;   // 1/16 notes
    return juce::jlimit (0, n, (int) std::llround (std::round (sample / grid) * grid));
}
void ProcessLabPanel::addOpMenu()
{
    juce::PopupMenu m; std::map<juce::String, juce::PopupMenu> groups; juce::StringArray order;
    for (int t = 0; t < (int) OpType::Count; ++t)
    {
        const auto& I = info ((OpType) t);
        if (! order.contains (I.group)) order.add (I.group);
        groups[I.group].addItem (t + 1, I.name);
    }
    for (auto& gname : order) m.addSubMenu (gname, groups[gname]);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addB), [safe = juce::Component::SafePointer<ProcessLabPanel> (this)] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        auto& o = safe->session.chain.ops; if ((int) o.size() >= kMaxChainOps) { safe->status = "chain is full (64 ops)"; safe->refresh(); return; }
        const int at = safe->selOp >= 0 ? safe->selOp + 1 : (int) o.size();
        Op op ((OpType) (r - 1));
        if (info (op.type).usesTempo && safe->host.tempoBpm() > 0) op.p[1] = (float) safe->host.tempoBpm();
        o.insert (o.begin() + at, op);
        safe->session.chainChanged(); safe->selectOp (at);
    });
}
void ProcessLabPanel::editMenu()
{
    juce::PopupMenu m; const bool hasR = ! session.region.empty(); const bool clip = session.clipboard != nullptr;
    m.addItem (1, "Crop to region", hasR); m.addItem (2, "Delete region  (Del)", hasR); m.addItem (3, "Silence region", hasR);
    m.addItem (4, "Insert 0.5 s silence"); m.addItem (5, "Insert 1 beat of silence");
    m.addSeparator();
    m.addItem (6, "Copy  (Ctrl+C)", hasR); m.addItem (7, "Cut  (Ctrl+X)", hasR); m.addItem (8, "Paste  (Ctrl+V)", clip); m.addItem (9, "Mix paste", clip);
    m.addSeparator();
    m.addItem (10, "Find loop point (moves the region end)", hasR);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&editB), [safe = juce::Component::SafePointer<ProcessLabPanel> (this)] (int r)
    {
        if (safe == nullptr) return;
        auto& L = safe->session; safe->stopAudition();
        switch (r)
        {
            case 1: L.crop(); break; case 2: L.deleteRegion(); break; case 3: L.silenceRegion(); break;
            case 4: L.insertSilence (0.5); break; case 5: L.insertSilence (60.0 / (safe->host.tempoBpm() > 0 ? safe->host.tempoBpm() : 120.0)); break;
            case 6: L.copyRegion(); break; case 7: L.cutRegion(); break; case 8: L.paste(); break; case 9: L.mixPaste(); break;
            case 10: L.findLoop(); break;
            default: break;
        }
    });
}
void ProcessLabPanel::moveOp (int d)
{
    auto& o = session.chain.ops; const int j = selOp + d;
    if (selOp < 0 || j < 0 || j >= (int) o.size()) return;
    std::swap (o[(size_t) selOp], o[(size_t) j]); session.chainChanged(); selectOp (j);
}
void ProcessLabPanel::startedAudition (std::shared_ptr<const Audio> clip, bool loop, bool isPreview)
{
    auditioning = true; playStartMs = nowMs(); playDurMs = clip ? 1000.0 * clip->length() / clip->rate : 0; playLoop = loop; playIsPreview = isPreview;
}
float ProcessLabPanel::playhead01() const
{
    if (! auditioning) return -1.0f;
    const float hp = host.auditionPosition(); if (hp >= 0.0f) return hp;
    if (playDurMs <= 0) return -1.0f;
    double t = (nowMs() - playStartMs) / playDurMs;
    if (playLoop) t = std::fmod (t, 1.0); else if (t > 1.0) return -1.0f;
    return (float) t;
}
void ProcessLabPanel::playSource()
{
    if (session.audio.empty()) return;
    auto clip = session.region.empty() ? std::make_shared<const Audio> (session.audio.slice (session.region.start, session.audio.length())) : session.regionAudio();
    host.audition (clip, loopB.getToggleState()); startedAudition (clip, loopB.getToggleState(), false); status = "playing the source region"; refresh();
}
void ProcessLabPanel::playPreview()
{
    if (! session.previewValid()) { status = "PREVIEW first (the chain or region changed)"; refresh(); return; }
    host.audition (session.previewAudio, loopB.getToggleState()); startedAudition (session.previewAudio, loopB.getToggleState(), true); status = "playing the preview"; refresh();
}
void ProcessLabPanel::stopAudition() { if (auditioning) host.audition (nullptr, false); auditioning = false; }
void ProcessLabPanel::togglePlay()
{
    if (auditioning) { stopAudition(); refresh(); return; }
    if (session.previewValid()) playPreview(); else playSource();
}
juce::File ProcessLabPanel::writeDragFile()
{
    if (! host.allowExport (this)) { status = "Exporting is part of the full version"; repaint (statusArea); return {}; }   // every file export goes through here (the demo gate)
    const Audio a = session.previewValid() ? *session.previewAudio : (session.region.empty() ? session.audio : session.audio.slice (session.region.start, session.region.length));
    if (a.empty()) return {};
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ProcessLab_" + juce::String (juce::Time::currentTimeMillis()) + ".wav");
    juce::WavAudioFormat wav;
    if (auto os = std::unique_ptr<juce::OutputStream> (f.createOutputStream()))
        if (auto* w = wav.createWriterFor (os.get(), a.rate, (unsigned) a.channels(), 24, {}, 0))   // GATE: via-caller (the host's <plugin>.plab.drag gate, asked above)
        {
            os.release(); std::unique_ptr<juce::AudioFormatWriter> W (w);
            auto b = a.toBuffer(); W->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        }
    return f;
}
bool ProcessLabPanel::keyPressed (const juce::KeyPress& k)
{
    const bool cmd = k.getModifiers().isCommandDown() || k.getModifiers().isCtrlDown();
    const int code = k.getKeyCode(); const auto ch = k.getTextCharacter();
    if (code == juce::KeyPress::spaceKey) { togglePlay(); return true; }
    if (code == juce::KeyPress::returnKey) { stopAudition(); session.apply(); return true; }
    if (code == juce::KeyPress::escapeKey) { session.cancel(); stopAudition(); refresh(); return true; }
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) { session.deleteRegion(); return true; }
    if (cmd)
    {
        const int c = juce::CharacterFunctions::toLowerCase (ch ? ch : (juce::juce_wchar) code);
        if (c == 'z' && k.getModifiers().isShiftDown()) { session.redoStep(); return true; }
        if (c == 'z') { stopAudition(); session.undoStep(); return true; }
        if (c == 'y') { session.redoStep(); return true; }
        if (c == 'b') { stopAudition(); session.bounce(); return true; }
        if (c == 'r') { stopAudition(); session.repeatLast(); return true; }
        if (c == 'c') { session.copyRegion(); return true; }
        if (c == 'x') { session.cutRegion(); return true; }
        if (c == 'v') { session.paste(); return true; }
        if (c == 'a') { session.setRegion (Region::all (session.audio)); return true; }
        return false;
    }
    if (ch == 'p' || ch == 'P') { stopAudition(); session.preview(); return true; }
    if (ch == 'a' || ch == 'A') { session.setRegion (Region::all (session.audio)); return true; }
    if (ch == '+' || ch == '=') { zoomIn.triggerClick(); return true; }
    if (ch == '-') { zoomOut.triggerClick(); return true; }
    if (ch == '0') { zoomFit.triggerClick(); return true; }
    return false;
}
void ProcessLabPanel::refresh()
{
    const bool busy = session.busy(), has = ! session.audio.empty();
    const bool hasOps = std::any_of (session.chain.ops.begin(), session.chain.ops.end(), [] (const Op& o) { return o.enabled; });
    if (selOp >= (int) session.chain.ops.size()) selOp = (int) session.chain.ops.size() - 1;   // e.g. BOUNCE cleared the chain
    const bool hasR = ! session.region.empty();
    previewB.setEnabled (has && hasOps && hasR); applyB.setEnabled (has && hasOps && hasR); bounceB.setEnabled (has && hasOps && hasR);
    bounceNewB.setVisible (host.canBounceToNew()); bounceNewB.setEnabled (has && hasOps && hasR);
    againB.setEnabled (has && hasR && session.hasLast()); sliceB.setEnabled (has && hasOps); sliceB.setVisible (host.showsPerSlice());
    playSrcB.setEnabled (has); playPrevB.setEnabled (session.previewValid()); cancelB.setEnabled (busy); dragB->setEnabled (has);
    undoB.setEnabled (session.undo.canUndo()); redoB.setEnabled (session.undo.canRedo());
    undoB.setTooltip ("undo" + (session.undo.undoName().isNotEmpty() ? ": " + session.undo.undoName() : juce::String())); redoB.setTooltip ("redo" + (session.undo.redoName().isNotEmpty() ? ": " + session.undo.redoName() : juce::String()));
    delB.setEnabled (selOp >= 0); dupB.setEnabled (selOp >= 0); upB.setEnabled (selOp > 0); downB.setEnabled (selOp >= 0 && selOp + 1 < (int) session.chain.ops.size());
    seedLabel.setText (juce::String ((juce::int64) session.chain.seed), juce::dontSendNotification);
    edgeLabel.setText (juce::String (session.chain.edgeMs, 1) + " ms", juce::dontSendNotification);
    if (has)
    {
        startLabel.setText (juce::String (session.region.start / session.audio.rate, 4), juce::dontSendNotification);
        lenLabel.setText (juce::String (session.region.length / session.audio.rate, 4), juce::dontSendNotification);
    }
    if (viewLen <= 0 || viewStart + viewLen > session.audio.length()) setView (viewStart, viewLen > 0 ? viewLen : session.audio.length());
    if (params->shownOp() != selOp || selOp >= (int) session.chain.ops.size()) params->show (selOp);
    list->repaint(); wave->repaint(); repaint();
}
void ProcessLabPanel::timerCallback()
{
    const bool busy = session.busy();
    if (busy) { repaint (progressArea); repaint (statusArea); }
    if (wasBusy && ! busy) refresh();
    wasBusy = busy;
    if (auditioning)
    {
        wave->repaint();
        if (! playLoop && host.auditionPosition() < 0.0f && nowMs() - playStartMs > playDurMs + 100) { auditioning = false; refresh(); }
    }
    // auto preview: 150 ms after the last chain / seed / region edit
    const uint32_t gen = session.chainGeneration();
    if (gen != seenGen) { seenGen = gen; genChangedAt = nowMs(); if (skipAuto) { skipAuto = false; autoReqGen = gen; } }
    if (autoPreview && autoReqGen != gen && nowMs() - genChangedAt > 150 && ! busy && ! session.previewValid() && ! session.region.empty()
        && std::any_of (session.chain.ops.begin(), session.chain.ops.end(), [] (const Op& o) { return o.enabled; }))
    { autoReqGen = gen; session.preview(); }
}

void ProcessLabPanel::paint (juce::Graphics& g)
{
    const auto& P = palette;
    g.fillAll (P.bg);
    g.setColour (P.accent); g.setFont (font (getWidth() < 760 ? 13.0f : 16.0f)); g.drawText ("PROCESS LAB", titleArea, juce::Justification::centredLeft);
    g.setColour (P.text2); g.setFont (font (9));
    g.drawText ("SEED", seedDown.getBounds().translated (-34, 0).withWidth (32), juce::Justification::centredRight);
    g.drawText ("EDGE", edgeTitle, juce::Justification::centredRight);
    g.drawText ("START", startLabel.getBounds().translated (-38, 0).withWidth (36), juce::Justification::centredRight);
    g.drawText ("LEN", lenLabel.getBounds().translated (-30, 0).withWidth (28), juce::Justification::centredRight);
    g.setColour (P.text.withAlpha (0.85f)); g.setFont (font (11, false));
    juce::String st = status;
    if (session.busy()) st = "working... " + juce::String (juce::roundToInt (session.progress() * 100)) + " %  (Esc cancels)";
    g.drawText (st, statusArea, juce::Justification::centredRight, true);
    auto pr = progressArea.toFloat();
    g.setColour (P.cell); g.fillRoundedRectangle (pr, 3);
    const float f = session.busy() ? session.progress() : 0.0f;
    if (f > 0) { g.setColour (P.progress); g.fillRoundedRectangle (pr.withWidth (pr.getWidth() * f), 3); }
}
void ProcessLabPanel::resized()
{
    auto r = getLocalBounds().reduced (8);
    const bool narrow = getWidth() < 800;
    auto top = r.removeFromTop (26); r.removeFromTop (4);
    titleArea = top.removeFromLeft (narrow ? 100 : 128);
    allB.setBounds (top.removeFromLeft (40).reduced (1)); editB.setBounds (top.removeFromLeft (46).reduced (1));
    snapBox.setBounds (top.removeFromLeft (narrow ? 82 : 96).reduced (1)); top.removeFromLeft (36);
    seedDown.setBounds (top.removeFromLeft (28).reduced (1)); seedLabel.setBounds (top.removeFromLeft (54).reduced (1)); seedUp.setBounds (top.removeFromLeft (28).reduced (1));
    diceB.setBounds (top.removeFromLeft (44).reduced (1));
    edgeTitle = top.removeFromLeft (34); edgeLabel.setBounds (top.removeFromLeft (58).reduced (1)); top.removeFromLeft (6);
    statusArea = top;
    // two button rows: audition / preview, then commit
    bounceNewB.setButtonText (narrow ? "NEW" : "BOUNCE NEW");
    auto rowB = r.removeFromBottom (28); r.removeFromBottom (3); auto rowA = r.removeFromBottom (28); r.removeFromBottom (5);
    {
        juce::Component* a[] = { &previewB, &autoB, &playSrcB, &playPrevB, &stopB, &loopB, dragB.get(), &cancelB };
        const float wts[] = { 1.3f, 0.8f, 1.1f, 1.1f, 0.9f, 0.9f, 0.9f, 1.0f };
        float tw = 0; for (float w : wts) tw += w; const int W = rowA.getWidth();
        for (int i = 0; i < 8; ++i) a[i]->setBounds (rowA.removeFromLeft (i == 7 ? rowA.getWidth() : (int) (W * wts[i] / tw)).reduced (2, 0));
    }
    {
        std::vector<juce::Component*> b { &undoB, &redoB, &againB, &sliceB };
        if (host.canBounceToNew()) b.push_back (&bounceNewB);
        b.push_back (&bounceB); b.push_back (&applyB);
        std::vector<float> wts; for (auto* c : b) wts.push_back (c == &applyB || c == &bounceB ? 1.5f : 1.0f);
        float tw = 0; for (float w : wts) tw += w; const int W = rowB.getWidth();
        for (size_t i = 0; i < b.size(); ++i) b[i]->setBounds (rowB.removeFromLeft (i + 1 == b.size() ? rowB.getWidth() : (int) (W * wts[i] / tw)).reduced (2, 0));
    }
    wave->setBounds (r.removeFromTop ((int) (r.getHeight() * (getHeight() < 440 ? 0.44f : 0.52f))));
    r.removeFromTop (3);
    auto num = r.removeFromTop (20); r.removeFromTop (4);
    zoomFit.setBounds (num.removeFromRight (40).reduced (1, 0)); zoomIn.setBounds (num.removeFromRight (26).reduced (1, 0)); zoomOut.setBounds (num.removeFromRight (26).reduced (1, 0));
    num.removeFromRight (6);
    lenLabel.setBounds (num.removeFromRight (70)); num.removeFromRight (32);
    startLabel.setBounds (num.removeFromRight (70)); num.removeFromRight (42);
    progressArea = num.reduced (0, 6);
    auto left = r.removeFromLeft ((int) (r.getWidth() * 0.34f)); r.removeFromLeft (6);
    auto lb = left.removeFromTop (24); left.removeFromTop (3);
    {
        juce::TextButton* bs[] = { &addB, &delB, &dupB, &upB, &downB };
        const int w = lb.getWidth() / 5;
        downB.setButtonText (w < 52 ? "DN" : "DOWN"); addB.setButtonText (w < 52 ? "+" : "+ ADD");
        for (auto* b : bs) b->setBounds (lb.removeFromLeft (w).reduced (1, 0));
    }
    list->setBounds (left);
    params->setBounds (r);
}

} // namespace plab
