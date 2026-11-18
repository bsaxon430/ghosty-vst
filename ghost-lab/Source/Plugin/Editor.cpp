#include "Editor.h"
#include "TerrainSerial.h"
#include "HoodPage.h"

namespace glab
{
using namespace theme;

// each action's neon (the cards, their icons, their glow)
static juce::Colour opColour (Op op)
{
    switch (op)
    {
        case Op::Pull: return purple;
        case Op::Twist: return pink;
        case Op::Freeze: return cyan;
        case Op::Smear: return green;
        case Op::Shatter: return yellow;
        case Op::Mutate: return mint;
        case Op::GrainCloud: return purple;
        case Op::SpectralMelt: return cyan;
        case Op::TimeWarp: return pink;
        case Op::VoiceFusion: return green;
        case Op::Pinch: return blue;             // (no card: the name plate under the tube, while the body is squeezed)
        default: return purple;
    }
}

// ================================================================================================ small parts
class Pill : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    enum Icon { None, Folder, Layers, Hood, Terrain, Deep, Back, Rec, Info };
    juce::String label;
    juce::Colour accent { 0xfff2f2f3 };
    Icon icon = None;
    bool filled = false, dashed = false, dragSource = false, acceptsFiles = false, hover = false;
    std::function<void()> onClick;
    std::function<void (const juce::StringArray&)> onFiles;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const float rad = r.getHeight() * 0.5f;
        if (icon == Info)
        {
            g.setColour (night0.withAlpha (0.6f)); g.fillEllipse (r);
            g.setColour (hover ? text : text.withAlpha (0.75f)); g.drawEllipse (r, 1.4f);
            g.setFont (font (r.getHeight() * 0.5f, 2)); g.drawText ("i", r, juce::Justification::centred);
            return;
        }
        g.setColour (night0.withAlpha (0.85f)); g.fillRoundedRectangle (r, rad);
        if (filled || hover) { g.setColour (accent.withAlpha (filled ? 0.22f : 0.08f)); g.fillRoundedRectangle (r, rad); }
        if (filled) { juce::Path p; p.addRoundedRectangle (r, rad); art::glow (g, p, accent.withAlpha (0.45f), 10); }
        g.setColour (hover ? accent : (dashed ? text.withAlpha (0.35f) : accent.withAlpha (filled ? 1.0f : 0.6f)));
        if (dashed)
        {
            juce::Path p; p.addRoundedRectangle (r, rad);
            const float dl[] = { 4.0f, 3.0f };
            juce::Path d; juce::PathStrokeType (1.1f).createDashedStroke (d, p, dl, 2);
            g.fillPath (d);
        }
        else g.drawRoundedRectangle (r, rad, filled ? 1.5f : 1.1f);
        const auto f = font (juce::jmin (12.5f, r.getHeight() * 0.42f), 1, 0.1f);
        const float tw = juce::GlyphArrangement::getStringWidth (f, label);
        const float isz = r.getHeight() * 0.5f;
        const bool withIcon = icon != None && tw + isz + 26.0f <= r.getWidth();
        const float x0 = r.getCentreX() - (tw + (withIcon ? isz + 8.0f : 0.0f)) * 0.5f;
        const auto ic = dashed ? text.withAlpha (0.8f) : accent.interpolatedWith (text, 0.35f);
        if (withIcon)
        {
            auto ir = juce::Rectangle<float> (isz, isz).withPosition (x0, r.getCentreY() - isz * 0.5f);
            switch (icon)
            {
                case Folder: icon::folder (g, ir, ic); break;
                case Layers: icon::layers (g, ir, ic); break;
                case Hood: icon::gearOpen (g, ir, ic); break;
                case Terrain: icon::mountain (g, ir, ic); break;
                case Deep: icon::deepDown (g, ir, ic); break;
                case Back: icon::arrowHead (g, { ir.getX() + ir.getWidth() * 0.3f, ir.getCentreY() }, juce::MathConstants<float>::pi, ir.getWidth() * 0.45f, ir.getWidth() * 0.13f); break;
                case Rec: g.setColour (red); g.fillEllipse (ir.reduced (ir.getWidth() * 0.22f)); break;
                default: break;
            }
        }
        g.setColour (dashed ? text.withAlpha (0.85f) : theme::text);
        g.setFont (f);
        g.drawText (label, juce::Rectangle<float> (x0 + (withIcon ? isz + 8.0f : 0.0f), r.getY(), tw + 4.0f, r.getHeight()), juce::Justification::centredLeft);
    }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragSource || e.getDistanceFromDragStart() < 5) return;
        if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor (this))
            if (! c->isDragAndDropActive())
            {
                juce::Image im (juce::Image::ARGB, getWidth(), getHeight(), true);
                { juce::Graphics g (im); paint (g); }
                c->startDragging ("specimen-002", this, juce::ScaledImage (im));
            }
    }
    void mouseUp (const juce::MouseEvent& e) override { if (e.getDistanceFromDragStart() < 5 && onClick) onClick(); }
    bool isInterestedInFileDrag (const juce::StringArray& f) override { if (! acceptsFiles) return false; for (auto& s : f) if (canLoad (juce::File (s))) return true; return false; }
    void fileDragEnter (const juce::StringArray&, int, int) override { hover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { hover = false; repaint(); }
    void filesDropped (const juce::StringArray& f, int, int) override { hover = false; repaint(); if (onFiles) onFiles (f); }
};

class Card : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    Card (Op o, bool tableCard) : op (o), table (tableCard) {}
    Op op;
    bool table, inChain = false, selected = false, hover = false, busyGlow = false, acceptsFiles = false, fileHover = false;
    std::function<void()> onClick, onToggle;
    std::function<void (int dy)> onReorder;          // table cards: dragged up / down by this many slots
    std::function<void (const juce::StringArray&)> onFiles;
    int dragSlots = 0;

    // the icon with a soft neon glow behind it (rendered once per size / screen scale, then reused)
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
    {
        const float sc = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        const float pad = 12.0f;
        const auto key = juce::String (r.getWidth(), 1) + "|" + juce::String (r.getHeight(), 1) + "|" + juce::String (sc, 2) + "|" + juce::String ((int) op) + c.toString();
        if (key != iconKey || ! iconImage.isValid())
        {
            iconKey = key;
            const int w = juce::jmax (1, juce::roundToInt ((r.getWidth() + 2 * pad) * sc)), h = juce::jmax (1, juce::roundToInt ((r.getHeight() + 2 * pad) * sc));
            juce::Image raw (juce::Image::ARGB, w, h, true);
            { juce::Graphics gg (raw); gg.addTransform (juce::AffineTransform::scale (sc)); drawIconRaw (gg, op, { pad, pad, r.getWidth(), r.getHeight() }, c); }
            iconImage = juce::Image (juce::Image::ARGB, w, h, true);
            juce::Graphics gg (iconImage);
            juce::DropShadow (c.withAlpha (0.8f), juce::roundToInt (7.0f * sc), {}).drawForImage (gg, raw);
            gg.drawImageAt (raw, 0, 0);
        }
        g.setOpacity (1.0f);
        g.drawImageTransformed (iconImage, juce::AffineTransform::scale (1.0f / sc).translated (r.getX() - pad, r.getY() - pad));
    }
    juce::Image iconImage;
    juce::String iconKey;
    static void drawIconRaw (juce::Graphics& g, Op op, juce::Rectangle<float> r, juce::Colour c)
    {
        switch (op)
        {
            case Op::Pull: icon::pull (g, r, c); break;
            case Op::Twist: icon::spiral (g, r, c); break;
            case Op::Freeze: icon::snowflake (g, r, c); break;
            case Op::Smear: icon::smear (g, r, c); break;
            case Op::Shatter: icon::burst (g, r, c); break;
            case Op::Mutate: icon::nodes (g, r, c); break;
            case Op::GrainCloud: icon::grains (g, r, c); break;
            case Op::SpectralMelt: icon::wave (g, r, c); break;
            case Op::TimeWarp: icon::warp (g, r, c); break;
            case Op::VoiceFusion: icon::rings (g, r, c); break;
            default: break;
        }
    }
    juce::Rectangle<float> chainDot() const { return { (float) getWidth() - 26.0f, (float) getHeight() * 0.5f - 8.0f, 16.0f, 16.0f }; }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const auto c = opColour (op);
        art::glassPanel (g, r, c, 10.0f, hover || fileHover ? 1.6f : 1.0f, selected || fileHover, true);
        const auto& inf = info (op);
        if (! table)
        {
            // the little status light in the corner
            g.setColour (c); g.fillRect (r.getX() + 8, r.getY() + 8, 5.0f, 5.0f);
            const float isz = juce::jmin (44.0f, r.getHeight() * 0.34f);
            drawIcon (g, juce::Rectangle<float> (isz, isz).withCentre ({ r.getCentreX(), r.getY() + r.getHeight() * 0.32f }), c);
            g.setColour (text); g.setFont (font (16.0f, 2, 0.1f));
            g.drawFittedText (inf.title, r.withTop (r.getY() + r.getHeight() * 0.56f).withHeight (24).toNearestInt(), juce::Justification::centred, 1, 0.8f);
            g.setColour (sub.brighter (0.25f)); g.setFont (font (12.5f));
            g.drawFittedText (inf.subtitle, r.withTop (r.getY() + r.getHeight() * 0.76f).withHeight (20).toNearestInt(), juce::Justification::centred, 1, 0.8f);
        }
        else
        {
            auto tile = juce::Rectangle<float> (r.getX() + 8, r.getY() + 6, juce::jmin (64.0f, r.getHeight() - 12), r.getHeight() - 12);
            drawIcon (g, tile.withSizeKeepingCentre (juce::jmin (tile.getWidth(), 50.0f), juce::jmin (tile.getHeight(), 50.0f)), c);
            auto tx = r.withLeft (tile.getRight() + 13).withRight (r.getRight() - 34);
            g.setColour (text); g.setFont (font (15.0f, 2, 0.08f));
            g.drawFittedText (inf.title, tx.withY (r.getCentreY() - 21).withHeight (22).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
            g.setColour (sub.brighter (0.25f)); g.setFont (font (12.0f));
            g.drawFittedText (inf.subtitle, tx.withY (r.getCentreY() + 2).withHeight (18).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
            // the chain node: filled = this module runs when you press GENERATE MUTATION
            auto d = chainDot();
            if (inChain) { juce::Path p; p.addEllipse (d); art::glow (g, p, green.withAlpha (0.8f), 8); }
            g.setColour (inChain ? green : night0); g.fillEllipse (d);
            g.setColour (inChain ? green : text.withAlpha (0.55f)); g.drawEllipse (d, 1.4f);
        }
        if (busyGlow) { g.setColour (green.withAlpha (0.08f)); g.fillRoundedRectangle (r, 10.0f); }
    }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! table || ! onReorder) return;
        dragSlots = juce::roundToInt ((float) e.getDistanceFromDragStartY() / (float) (getHeight() + 10));
        setAlpha (dragSlots != 0 ? 0.7f : 1.0f);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        setAlpha (1.0f);
        if (table && dragSlots != 0 && onReorder) { const int d = dragSlots; dragSlots = 0; onReorder (d); return; }
        dragSlots = 0;
        if (e.getDistanceFromDragStart() > 6) return;
        if (table && chainDot().expanded (8).contains (e.position)) { if (onToggle) onToggle(); return; }
        if (onClick) onClick();
    }
    bool isInterestedInFileDrag (const juce::StringArray& f) override { if (! acceptsFiles) return false; for (auto& s : f) if (canLoad (juce::File (s))) return true; return false; }
    void fileDragEnter (const juce::StringArray&, int, int) override { fileHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { fileHover = false; repaint(); }
    void filesDropped (const juce::StringArray& f, int, int) override { fileHover = false; repaint(); if (onFiles) onFiles (f); }
};

class WaveView : public juce::Component
{
public:
    WaveView (bool isProcessed) : processed (isProcessed) {}
    bool processed, playing = false, hasSound = false;
    float playhead = -1.0f;
    double seconds = 0.0;        // the sound's length (the trim read-out)
    const Profile* profile = nullptr;
    juce::String info;
    std::function<void()> onClick, onDragOut;
    // TRIM (build 16): a handle at each end; drag them in, let go and the part between them is kept
    std::function<void (float, float)> onTrim;
    float trimA = 0.0f, trimB = 1.0f;
    int grabbing = -1, hoverHandle = -1;   // 0 the start handle, 1 the end handle

    juce::Rectangle<float> waveArea() const { return getLocalBounds().toFloat().reduced (16, 0).withTrimmedTop (32).withTrimmedBottom (10); }
    float handleX (int h) const { const auto wr = waveArea(); return wr.getX() + wr.getWidth() * (h == 0 ? trimA : trimB); }
    int handleAt (juce::Point<float> p) const
    {
        if (! hasSound || profile == nullptr || p.y < waveArea().getY() - 14.0f) return -1;
        const float d0 = std::abs (p.x - handleX (0)), d1 = std::abs (p.x - handleX (1));
        if (juce::jmin (d0, d1) > 12.0f) return -1;
        return d0 < d1 || (d0 == d1 && p.x < handleX (0)) ? 0 : 1;
    }
    static juce::String secs (double s) { return juce::String (s, s < 10.0 ? 2 : 1) + " s"; }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const auto c = processed ? green : purple;
        art::glassPanel (g, r.reduced (1.0f), c, 10.0f, 0.8f, playing, true);
        g.setColour (c); g.setFont (font (14.0f, 2, 0.12f));
        auto top = r.reduced (16, 8).removeFromTop (22);
        g.drawText (processed ? "PROCESSED" : "ORIGINAL", top, juce::Justification::centredLeft);
        if (grabbing >= 0)
        {
            // while trimming: what will be kept
            g.setColour (text); g.setFont (font (11.5f, 2, 0.06f));
            g.drawFittedText ("KEEP " + secs (trimA * seconds) + " TO " + secs (trimB * seconds) + "   (" + secs ((trimB - trimA) * seconds) + ")",
                              top.withTrimmedLeft (110).toNearestInt(), juce::Justification::centredRight, 1, 0.8f);
        }
        else
        {
            g.setColour (sub.brighter (0.2f)); g.setFont (font (11.5f, 0, 0.06f));
            g.drawFittedText (hasSound ? (juce::String (playing ? "PLAYING  " : "CLICK TO HEAR  ") + info + (processed ? "  |  DRAG INTO FL" : "")) : juce::String(),
                              top.withTrimmedLeft (110).toNearestInt(), juce::Justification::centredRight, 1, 0.8f);
        }
        auto wr = waveArea();
        if (profile == nullptr || ! hasSound)
        {
            g.setColour (c.withAlpha (0.35f)); g.drawHorizontalLine ((int) wr.getCentreY(), wr.getX(), wr.getRight());
            return;
        }
        // the bars and their glow: drawn once per sound / size / screen scale, then reused (the playhead moves over them)
        const float sc = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        const auto key = juce::String::toHexString ((juce::pointer_sized_int) profile) + ":" + juce::String (profile->waveMax[0]) + ":" + juce::String (profile->waveMax[(size_t) Profile::kWaveBins / 2])
                         + ":" + wr.toString() + ":" + juce::String (sc, 2);
        if (key != waveKey || ! waveImage.isValid())
        {
            waveKey = key;
            const float pad = 8.0f;
            waveImage = juce::Image (juce::Image::ARGB, juce::jmax (1, juce::roundToInt ((wr.getWidth() + 2 * pad) * sc)), juce::jmax (1, juce::roundToInt ((wr.getHeight() + 2 * pad) * sc)), true);
            juce::Graphics wg (waveImage);
            wg.addTransform (juce::AffineTransform::translation (pad - wr.getX(), pad - wr.getY()).scaled (sc));
            const int bins = Profile::kWaveBins;
            const int bars = juce::jmax (20, (int) (wr.getWidth() / 2.0f));
            float peakAll = 1e-6f;
            for (int i = 0; i < bins; ++i) peakAll = juce::jmax (peakAll, profile->waveMax[(size_t) i], -profile->waveMin[(size_t) i]);
            juce::Path wave;
            for (int b = 0; b < bars; ++b)
            {
                const int i0 = b * bins / bars, i1 = juce::jmax (i0 + 1, (b + 1) * bins / bars);
                float mm = 0; for (int i = i0; i < i1; ++i) mm = juce::jmax (mm, profile->waveMax[(size_t) i], -profile->waveMin[(size_t) i]);
                const float hgt = juce::jmax (0.5f, mm / peakAll * wr.getHeight() * 0.5f);
                const float x = wr.getX() + wr.getWidth() * (float) b / (float) bars;
                wave.addRectangle (x, wr.getCentreY() - hgt, 1.3f, hgt * 2);
            }
            art::glow (wg, wave, c.withAlpha (0.5f), 6);
            wg.setColour (c.withAlpha (0.92f)); wg.fillPath (wave);
        }
        g.setOpacity (1.0f);
        g.drawImageTransformed (waveImage, juce::AffineTransform::scale (1.0f / sc).translated (wr.getX() - 8.0f, wr.getY() - 8.0f));
        if (playing && playhead >= 0)
        {
            g.setColour (text.withAlpha (0.8f));
            g.drawVerticalLine ((int) (wr.getX() + wr.getWidth() * playhead), wr.getY(), wr.getBottom());
        }
        // the parts that will be cut: dimmed
        const float xa = handleX (0), xb = handleX (1);
        g.setColour (juce::Colour (0xff07060f).withAlpha (0.78f));
        if (xa > wr.getX() + 0.5f) g.fillRect (juce::Rectangle<float> (wr.getX() - 2.0f, wr.getY() - 4.0f, xa - wr.getX() + 2.0f, wr.getHeight() + 8.0f));
        if (xb < wr.getRight() - 0.5f) g.fillRect (juce::Rectangle<float> (xb, wr.getY() - 4.0f, wr.getRight() - xb + 2.0f, wr.getHeight() + 8.0f));
        // the two handles: a line the height of the wave with a grip at the top
        for (int h = 0; h < 2; ++h)
        {
            const float x = h == 0 ? xa : xb;
            const bool hot = grabbing == h || hoverHandle == h;
            const auto hc = hot ? text : c.brighter (0.2f);
            auto grip = juce::Rectangle<float> (10.0f, 18.0f).withCentre ({ x, wr.getCentreY() });
            if (hot) { juce::Path p; p.addRoundedRectangle (grip, 3.0f); art::glow (g, p, c.withAlpha (0.9f), 8); }
            g.setColour (hc.withAlpha (hot ? 1.0f : 0.75f));
            g.fillRect (juce::Rectangle<float> (x - (hot ? 1.0f : 0.75f), wr.getY(), hot ? 2.0f : 1.5f, wr.getHeight()));
            g.fillRoundedRectangle (grip, 3.0f);
            g.setColour (juce::Colour (0xff0d0b18));
            for (int k = -1; k <= 1; ++k) g.fillRect (grip.getCentreX() - 2.5f, grip.getCentreY() + (float) k * 3.5f - 0.5f, 5.0f, 1.0f);
        }
    }
    void mouseMove (const juce::MouseEvent& e) override
    {
        const int h = handleAt (e.position);
        if (h != hoverHandle) { hoverHandle = h; repaint(); }
        setMouseCursor (h >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    }
    void mouseExit (const juce::MouseEvent&) override { if (hoverHandle >= 0 && grabbing < 0) { hoverHandle = -1; repaint(); } }
    void mouseDown (const juce::MouseEvent& e) override { grabbing = handleAt (e.position); if (grabbing >= 0) repaint(); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (grabbing >= 0)
        {
            const auto wr = waveArea();
            const float t = juce::jlimit (0.0f, 1.0f, (e.position.x - wr.getX()) / juce::jmax (1.0f, wr.getWidth()));
            // the two ends never cross, and keep at least the shortest sound worth keeping between them
            const float gap = seconds > 0.0 ? juce::jlimit (0.002f, 0.5f, (float) (Engine::kMinTrimSeconds / seconds)) : 0.01f;
            if (grabbing == 0) trimA = juce::jmin (t, trimB - gap); else trimB = juce::jmax (t, trimA + gap);
            repaint();
            return;
        }
        if (processed && hasSound && e.getDistanceFromDragStart() > 8 && ! dragging) { dragging = true; if (onDragOut) onDragOut(); }
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (grabbing >= 0)
        {
            grabbing = -1;
            const float a = trimA, b = trimB;
            trimA = 0.0f; trimB = 1.0f;   // (the trimmed sound fills the strip again)
            repaint();
            if ((a > 0.0005f || b < 0.9995f) && onTrim) onTrim (a, b);
            return;
        }
        const bool wasDrag = dragging; dragging = false;
        if (! wasDrag && e.getDistanceFromDragStart() < 6 && onClick) onClick();
    }
    bool dragging = false;
    juce::Image waveImage;
    juce::String waveKey;
};

class FooterButton : public juce::Component
{
public:
    enum Kind { Undo, Redo, Generate, Save } kind;
    explicit FooterButton (Kind k) : kind (k) {}
    std::function<void()> onClick;
    bool hover = false, down = false, busy = false;
    float phase = 0;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        const bool on = isEnabled();
        if (kind == Generate)
        {
            auto c = green.withAlpha (on ? 1.0f : 0.35f);
            if (busy) c = green.withAlpha (0.55f + 0.25f * std::sin (phase));
            juce::Path p; p.addRoundedRectangle (r, 12.0f);
            if (on) art::glow (g, p, green.withAlpha (hover ? 0.9f : 0.6f), 22);
            g.setGradientFill (juce::ColourGradient (c.brighter (hover && on ? 0.35f : 0.18f), 0, r.getY(), c.darker (0.15f), 0, r.getBottom(), false));
            g.fillPath (p);
            g.setColour (juce::Colours::white.withAlpha (0.35f)); g.drawLine (r.getX() + 14, r.getY() + 2.5f, r.getRight() - 14, r.getY() + 2.5f, 1.2f);
            const auto ink = juce::Colour (0xff0e1408);
            g.setColour (ink);
            g.setFont (font (juce::jmin (19.0f, r.getHeight() * 0.34f), 2, 0.16f));
            const bool small = r.getWidth() < 300;
            g.drawFittedText (busy ? "WORKING..." : (small ? "GENERATE\nMUTATION" : "GENERATE MUTATION"), r.reduced (small ? 10 : 56, 2).toNearestInt(),
                              juce::Justification::centred, 2, 0.9f);
            if (! small && ! busy)
            {
                icon::dna (g, juce::Rectangle<float> (30, 20).withCentre ({ r.getX() + 38, r.getCentreY() }), ink);
                icon::chevronRight (g, juce::Rectangle<float> (16, 18).withCentre ({ r.getRight() - 30, r.getCentreY() }), ink);
            }
            return;
        }
        const auto c = on ? text : dim;
        g.setColour (night0.withAlpha (0.7f)); g.fillRoundedRectangle (r, 10.0f);
        if (hover && on) { g.setColour (purple.withAlpha (0.12f)); g.fillRoundedRectangle (r, 10.0f); }
        g.setColour ((hover && on ? purple : text).withAlpha (on ? 0.55f : 0.2f)); g.drawRoundedRectangle (r, 10.0f, 1.2f);
        const bool tiny = r.getWidth() < 100;
        const bool compact = r.getWidth() < 140;
        auto ir = juce::Rectangle<float> (20, 20).withCentre ({ r.getX() + (tiny ? r.getWidth() * 0.5f : (compact ? 24.0f : 34.0f)), r.getCentreY() });
        if (! tiny)
        {
            if (kind == Undo) icon::undo (g, ir, c);
            else if (kind == Redo) { juce::Graphics::ScopedSaveState ss (g); g.addTransform (juce::AffineTransform::scale (-1.0f, 1.0f, ir.getCentreX(), ir.getCentreY())); icon::undo (g, ir, c); }
            else icon::save (g, ir, c);
        }
        g.setColour (c); g.setFont (font (compact ? 14.0f : 16.0f, 1, compact ? 0.1f : 0.14f));
        const juce::String word = kind == Undo ? "UNDO" : kind == Redo ? "REDO" : "SAVE";
        g.drawFittedText (word, (tiny ? r : r.withTrimmedLeft (compact ? 42.0f : 58.0f).withTrimmedRight (6)).toNearestInt(),
                          tiny ? juce::Justification::centred : juce::Justification::centredLeft, 1, 0.9f);
    }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override { if (isEnabled() && getLocalBounds().contains (e.getPosition()) && onClick) onClick(); }
};

// the bar along the bottom: the lab's floor (the buttons sit on it)
class FooterBar : public juce::Component
{
public:
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setGradientFill (juce::ColourGradient (night0, 0, r.getY(), juce::Colour (0xff07060f), 0, r.getBottom(), false));
        g.fillRect (r);
        // the floor's glow: purple from the left, green from the right
        g.setGradientFill (juce::ColourGradient (purple.withAlpha (0.16f), r.getX(), r.getBottom(), purple.withAlpha (0.0f), r.getX() + r.getWidth() * 0.4f, r.getY(), true));
        g.fillRect (r);
        g.setGradientFill (juce::ColourGradient (green.withAlpha (0.12f), r.getRight(), r.getBottom(), green.withAlpha (0.0f), r.getRight() - r.getWidth() * 0.4f, r.getY(), true));
        g.fillRect (r);
        juce::ColourGradient line (purple.withAlpha (0.0f), r.getX(), 0, purple.withAlpha (0.0f), r.getRight(), 0, false);
        line.addColour (0.3, purple.withAlpha (0.6f)); line.addColour (0.7, green.withAlpha (0.6f));
        g.setGradientFill (line); g.fillRect (r.withHeight (1.0f));
        // the hazard stripes either side of the big button
        if (stripesL.getWidth() > 0)
            for (auto s : { stripesL, stripesR })
            {
                g.setColour (green.withAlpha (0.35f));
                for (int k = 0; k < 4; ++k)
                {
                    juce::Path p; const float x = s.getX() + (float) k * 9.0f;
                    p.addQuadrilateral (x, s.getBottom(), x + 5, s.getBottom(), x + 9, s.getY(), x + 4, s.getY());
                    g.fillPath (p);
                }
            }
    }
    juce::Rectangle<float> stripesL, stripesR;
};

class Toast : public juce::Component, private juce::Timer
{
public:
    void show (const juce::String& m, bool isBad) { msg = m; bad = isBad; alpha = 1.0f; setVisible (true); startTimerHz (30); ticks = 0; repaint(); }
    void hide() { stopTimer(); msg = {}; setVisible (false); }
    void paint (juce::Graphics& g) override
    {
        if (msg.isEmpty()) return;
        g.setFont (font (13.5f, 0, 0.02f));
        const float w = juce::jmin ((float) getWidth(), juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), msg) + 40.0f);
        auto r = juce::Rectangle<float> (w, (float) getHeight()).withCentre (getLocalBounds().toFloat().getCentre());
        g.setColour (juce::Colour (0xff0b0918).withAlpha (0.94f * alpha)); g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour ((bad ? red : green).withAlpha (0.8f * alpha)); g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f, 1.2f);
        g.setColour (text.withAlpha (alpha)); g.drawFittedText (msg, r.reduced (16, 0).toNearestInt(), juce::Justification::centred, 1, 0.8f);
    }
    juce::String msg;
private:
    void timerCallback() override
    {
        if (++ticks > 120) alpha -= 0.05f;
        if (alpha <= 0) { stopTimer(); msg = {}; setVisible (false); }
        repaint();
    }
    bool bad = false; float alpha = 0; int ticks = 0;
};

// the DEEP view: every engine control of the selected action, the programs it runs, APPLY
class DeepPanel : public juce::Component
{
public:
    explicit DeepPanel (Editor& e) : ed (e)
    {
        addAndMakeVisible (apply); apply.setButtonText ("APPLY");
        apply.onClick = [this] { ed.run (ed.settingsFor (op)); };
        for (auto* b : { &apply }) { b->setColour (juce::TextButton::buttonColourId, green); b->setColour (juce::TextButton::textColourOffId, juce::Colour (0xff111311)); }
    }
    void show (Op o)
    {
        op = o;
        sliders.clear(); labels.clear();
        for (auto& c : info (op).controls)
        {
            auto* s = sliders.add (new juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight));
            s->setRange (c.min, c.max, c.id == "detail" ? 0.0 : (c.max - c.min) / 1000.0);
            if (c.id == "detail") s->setRange (256, 4096, 1);
            s->setValue (ed.settingsFor (op).get (c.id), juce::dontSendNotification);
            s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
            s->setNumDecimalPlacesToDisplay (c.id == "detail" ? 0 : 2);
            s->setTextValueSuffix (c.unit.isEmpty() ? juce::String() : " " + c.unit);
            s->setColour (juce::Slider::trackColourId, c.deep ? purple : green);
            s->setColour (juce::Slider::backgroundColourId, border);
            s->setColour (juce::Slider::thumbColourId, text);
            s->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            s->setColour (juce::Slider::textBoxTextColourId, sub);
            const auto id = c.id;
            s->onValueChange = [this, s, id] { ed.settingsFor (op).values[id] = (float) s->getValue(); };
            addAndMakeVisible (s);
            auto* l = labels.add (new juce::Label ({}, c.label + (c.deep ? "  (deep)" : "")));
            l->setFont (font (13.0f)); l->setColour (juce::Label::textColourId, c.deep ? purple.brighter (0.3f) : text);
            addAndMakeVisible (l);
        }
        resized(); repaint();
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (night0.withAlpha (0.95f)); g.fillRoundedRectangle (r, 12.0f);
        g.setColour (opColour (op).withAlpha (0.7f)); g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.2f);
        g.setColour (text); g.setFont (font (15.0f, 2, 0.12f));
        g.drawText ("DEEP  |  " + info (op).title, r.reduced (16, 10).removeFromTop (20), juce::Justification::centredLeft);
        g.setColour (dim.brighter (0.3f)); g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain)));
        g.drawFittedText ("ENGINE: " + info (op).cdp, r.reduced (16, 10).removeFromBottom (18).withTrimmedRight (120).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (16, 10);
        r.removeFromTop (26);
        apply.setBounds (getWidth() - 112, getHeight() - 38, 96, 28);
        r.removeFromBottom (32);
        const int n = sliders.size(), cols = getWidth() > 600 ? 2 : 1;
        const int rows = (n + cols - 1) / juce::jmax (1, cols);
        const int rowH = juce::jmin (30, r.getHeight() / juce::jmax (1, rows));
        for (int i = 0; i < n; ++i)
        {
            const int col = i / juce::jmax (1, rows), row = i % juce::jmax (1, rows);
            auto cell = juce::Rectangle<int> (r.getX() + col * (r.getWidth() / cols), r.getY() + row * rowH, r.getWidth() / cols - 10, rowH);
            labels[i]->setBounds (cell.removeFromLeft (juce::jmin (150, cell.getWidth() / 3)));
            sliders[i]->setBounds (cell);
        }
    }
    Op op = Op::Pull;
private:
    Editor& ed;
    juce::OwnedArray<juce::Slider> sliders;
    juce::OwnedArray<juce::Label> labels;
    juce::TextButton apply;
};

// ================================================================================================ the scrolling content
class Content : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    explicit Content (Editor& e) : ed (e), deepPanel (e)
    {
        addAndMakeVisible (body);
        addAndMakeVisible (pill1); pill1.accent = purple; pill1.icon = Pill::Folder;
        addAndMakeVisible (pill2); pill2.accent = green; pill2.acceptsFiles = true; pill2.dragSource = true; pill2.icon = Pill::Layers;
        addAndMakeVisible (deepPill); deepPill.accent = purple; deepPill.icon = Pill::Deep;
        addAndMakeVisible (terrainPill); terrainPill.accent = green; terrainPill.icon = Pill::Terrain;
        if (Processor::isFx) { addAndMakeVisible (capturePill); capturePill.accent = green; capturePill.icon = Pill::Rec; }
        // the TERRAIN page: the shared landscape control surface in Ghost Lab's colours and type
        terrain = std::make_unique<ghost::terrain::TerrainView> (ed.proc.terrain);
        auto pal = ghost::terrain::Palette::ghostLab();
        pal.font = [] (float h, bool bold) { return font (h / 1.2f, bold ? 1 : 0, 0.02f); };
        terrain->setPalette (pal);
        terrain->setTimeLabel ("TIME");
        terrain->onListen = [this] { ed.terrainListen(); };
        terrain->isListening = [this] { return ed.proc.terrainListening(); };
        addChildComponent (*terrain);
        addAndMakeVisible (hoodPill); hoodPill.accent = purple; hoodPill.label = "UNDER THE HOOD"; hoodPill.icon = Pill::Hood;
        // the one About / Credits item: who made the engine, the licences, where the source is
        addAndMakeVisible (aboutPill); aboutPill.accent = purple; aboutPill.label = "i"; aboutPill.icon = Pill::Info;
        aboutPill.setTitle ("About / Credits");
        aboutPill.onClick = [this] { ed.showAbout(); };
        hoodPill.onClick = [this] { ed.setHoodPage (! ed.hoodPageOn()); };
        hoodPage = std::make_unique<HoodPage> (ed.engine);
        hoodPage->onRun = [this] (const Step& s) { ed.runHood (s); };
        hoodPage->onUndo = [this] { ed.undo(); };
        addChildComponent (*hoodPage);
        // THE ROOMS: the strip under the banner, and the one page they share
        addAndMakeVisible (route);
        route.onPick = [this] (rooms::Room r) { ed.setRoom (r); };
        roomPage = std::make_unique<RoomPage> (ed);
        addChildComponent (*roomPage);
        cardView.setScrollBarsShown (false, false, false, true);
        cardView.setViewedComponent (&strip, false);
        addAndMakeVisible (cardView);
        for (auto op : { Op::Pull, Op::Twist, Op::Freeze, Op::Smear, Op::Shatter, Op::Mutate })
        {
            auto* c = actionCards.add (new Card (op, false));
            c->onClick = [this, op] { ed.card (op); };
            strip.addAndMakeVisible (c);
        }
        for (int i = 0; i < 4; ++i)
        {
            auto* c = tableCards.add (new Card (Op::GrainCloud, true));
            c->onClick = [this, c] { ed.card (c->op); };
            c->onToggle = [this, i] { auto& slot = ed.engine.chain[(size_t) i]; slot.on = ! slot.on; ed.refresh(); };
            c->onReorder = [this, i] (int d) { ed.engine.moveSlot (i, juce::jlimit (0, 3, i + d)); ed.refresh(); };
            c->onFiles = [this] (const juce::StringArray& f) { ed.loadFiles (f, true); };
            addAndMakeVisible (c);
        }
        addAndMakeVisible (original); addAndMakeVisible (processed);
        addChildComponent (deepPanel);
        addChildComponent (toast);
        toast.setInterceptsMouseClicks (false, false);

        body.onGesture = [this] (const Step& s) { ed.run (s, true); };
        body.onFeel = [this] (const SoundBody::Feel& f) { ed.bodyFeel (f); };
        body.onLive = [this] (const juce::String& gesture, const juce::String& hint)
        {
            if (gesture == live && hint == liveHint) return;
            live = gesture; liveHint = hint; liveOp = body.liveOp();
            repaint (livingArea());
        };
        body.onBreed = [this] { ed.breed(); };
        body.onClick = [this] { ed.playWave (Player::Processed); };
        body.onClickEmpty = [this] { ed.chooseFile (false); };
        body.onFiles = [this] (const juce::StringArray& f) { ed.loadFiles (f, false); };
        pill1.onClick = [this] { ed.chooseFile (false); };
        pill2.onClick = [this] { ed.chooseFile (true); };
        pill2.onFiles = [this] (const juce::StringArray& f) { ed.loadFiles (f, true); };
        deepPill.onClick = [this] { ed.setDeep (! ed.deepOn()); };
        terrainPill.onClick = [this] { ed.setTerrainPage (! ed.terrainPageOn()); };
        capturePill.onClick = [this]
        {
            if (ed.proc.capturing())
            {
                if (auto a = ed.proc.stopCapture()) { ed.engine.setSpecimen (a, "Captured"); ed.engine.specimenNumber++; ed.toast ("Captured " + juce::String (a->seconds(), 1) + " s. It's your new specimen."); }
                else ed.toast ("Nothing was captured. Press play in FL while CAPTURE is on.", true);
            }
            else { ed.proc.startCapture(); ed.toast ("Capturing what plays through this track (up to 60 s). Press again to stop."); }
            ed.refresh();
        };
        original.onClick = [this] { ed.playWave (Player::Original); };
        processed.onClick = [this] { ed.playWave (Player::Processed); };
        processed.onDragOut = [this] { ed.dragOut(); };
        original.onTrim = [this] (float a, float b) { ed.trimWave (false, a, b); };
        processed.onTrim = [this] (float a, float b) { ed.trimWave (true, a, b); };
    }

    bool isInterestedInFileDrag (const juce::StringArray& f) override { for (auto& s : f) if (canLoad (juce::File (s))) return true; return false; }
    void filesDropped (const juce::StringArray& f, int, int) override { ed.loadFiles (f, false); }

    // the room, the window, the title, the ghost: drawn once per size (and screen scale) into an image
    void paintScene (juce::Graphics& g)
    {
        const bool narrow = ed.isNarrow();
        auto all = getLocalBounds().toFloat();
        const auto clear = juce::Rectangle<float> (m - 8, panelTop - 8, all.getWidth() - 2 * (m - 8), all.getHeight() - panelTop);
        const bool hood = ed.hoodPageOn();
        art::labRoom (g, all, clear, {}, 0.0f);
        // the top: Brandon's banner (its own title, the ghost, the lab)
        art::banner (g, bannerR, bannerRows);
        // the tube's dark glass (the living sound is drawn over it)
        const bool lab = ed.roomNow() == rooms::Room::Lab;
        if (! narrow && lab && ! ed.terrainPageOn() && ! ed.hoodPageOn()) art::tubeBack (g, tubeR);
        // the right column's frame (the EXPERIMENT TABLE)
        if (! narrow && lab && ! ed.terrainPageOn() && ! ed.hoodPageOn())
        {
            art::glassPanel (g, tableFrame, purple, 14.0f, 0.6f);
            // a beaker and a test tube sitting on the table's frame
        }
    }

    void paint (juce::Graphics& g) override
    {
        const float sc = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        const auto key = juce::String (getWidth()) + "x" + juce::String (getHeight()) + "@" + juce::String (sc, 2) + (ed.isNarrow() ? "n" : "w")
                         + (ed.terrainPageOn() ? "t" : "") + (ed.hoodPageOn() ? "h" : "") + juce::String ((int) ed.roomNow());
        if (key != sceneKey || ! scene.isValid())
        {
            sceneKey = key;
            scene = juce::Image (juce::Image::RGB, juce::jmax (1, juce::roundToInt ((float) getWidth() * sc)), juce::jmax (1, juce::roundToInt ((float) getHeight() * sc)), false);
            juce::Graphics sg (scene);
            sg.addTransform (juce::AffineTransform::scale (sc));
            paintScene (sg);
        }
        g.setOpacity (1.0f);
        g.drawImageTransformed (scene, juce::AffineTransform::scale (1.0f / sc));

        // status
        const auto st = ed.statusText();
        const bool working = ed.engine.busy(), ready = ed.engine.hasSpecimen();
        const auto dotC = working ? purple : (ready ? green : purple.brighter (0.2f));
        g.setFont (font (statusSize, 1, 0.16f));
        const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), st);
        const float dx = statusR.getRight() - tw - 20;
        if (! ed.isNarrow())
        {
            auto back = juce::Rectangle<float> (dx - 16, statusR.getY() - 2, statusR.getRight() - dx + 24, statusR.getHeight() + 4);
            g.setColour (night0.withAlpha (0.78f)); g.fillRoundedRectangle (back, back.getHeight() * 0.5f);
            g.setColour (dotC.withAlpha (0.35f)); g.drawRoundedRectangle (back, back.getHeight() * 0.5f, 1.0f);
        }
        {
            juce::Path dot; dot.addEllipse (juce::Rectangle<float> (11, 11).withCentre ({ dx, statusR.getCentreY() }));
            art::glow (g, dot, dotC.withAlpha (0.8f), 8);
            g.setColour (dotC.withAlpha (working ? 0.5f + 0.5f * std::sin (phase) : 1.0f)); g.fillPath (dot);
        }
        g.setColour (working ? purple.brighter (0.4f) : text.withAlpha (0.92f));
        g.drawText (st, statusR.withLeft (dx + 13), juce::Justification::centredLeft);

        g.setColour (text); g.setFont (font (ed.isNarrow() ? 13.0f : 15.0f, 2, 0.12f));
        g.drawFittedText ("YOUR SPECIMEN", specR.toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
        if (ed.roomNow() != rooms::Room::Lab) {}   // (a room: its page says everything)
        else if (ed.terrainPageOn())
        {
            g.setColour (sub.brighter (0.2f)); g.setFont (font (ed.isNarrow() ? 12.0f : 12.5f, 0, 0.04f));
            g.drawFittedText (ed.isNarrow() ? "TERRAIN  |  tap the land for a mountain. Spires SHATTER, plateaus FREEZE, rough land GRAIN CLOUD, merging BREEDs. It plays while you drag; the full render fades in."
                                            : "TERRAIN  |  the land drives the lab: across = frequency, back = time, height = how much. Spires SHATTER, plateaus FREEZE + SMEAR, rough land a GRAIN CLOUD, "
                                              "the land's lean TWISTs, merging mountains BREED with SPECIMEN 002. It plays while you drag; the full render fades in when it lands. SHAPES: whole landscapes.",
                              terrainHintR.toNearestInt(), juce::Justification::centredLeft, 3, 0.9f);
        }
        else if (ed.hoodPageOn())
        {
            if (ed.isNarrow())
            {
                g.setColour (sub.brighter (0.2f)); g.setFont (font (12.0f, 0, 0.04f));
                g.drawFittedText ("UNDER THE HOOD  |  every engine process in plain words. Pick one, set it, press RUN. UNDO takes it back.",
                                  terrainHintR.toNearestInt(), juce::Justification::centredLeft, 3, 0.85f);
            }
        }
        else
        {
            // the EXPERIMENT TABLE title
            g.setColour (purple.brighter (0.2f)); g.fillRect (tableR.getX(), tableR.getCentreY() - 3, 6.0f, 6.0f);
            g.setColour (text); g.setFont (font (15.0f, 2, 0.14f));
            g.drawText ("EXPERIMENT TABLE", tableR.withTrimmedLeft (14), juce::Justification::centredLeft);
            if (! ed.isNarrow())
            {
                g.setColour (purple.withAlpha (0.6f));
                for (int k = 0; k < 4; ++k)
                {
                    juce::Path p; const float x = tableR.getRight() - 40 + (float) k * 8.0f, y = tableR.getCentreY();
                    p.addQuadrilateral (x, y + 5, x + 4, y + 5, x + 8, y - 5, x + 4, y - 5);
                    g.fillPath (p);
                }
            }

            paintLiving (g);

            g.setColour (sub.brighter (0.15f)); g.setFont (font (11.5f, 0, 0.02f));
            g.drawFittedText ("Tap a card to use it now. Green dot = part of your mutation chain (drag cards to reorder).", hintR.toNearestInt(), juce::Justification::topLeft, 2, 0.9f);
        }

        // the chain: patch cords between the modules that run on GENERATE MUTATION, in order
        Card* prev = nullptr;
        for (auto* c : tableCards)
        {
            if (! c->inChain || ed.terrainPageOn() || ed.hoodPageOn() || ed.roomNow() != rooms::Room::Lab) continue;
            if (prev != nullptr)
            {
                auto a = prev->getBounds().toFloat().getTopLeft() + prev->chainDot().getCentre();
                auto b = c->getBounds().toFloat().getTopLeft() + c->chainDot().getCentre();
                juce::Path p; p.startNewSubPath (a); p.cubicTo (a.translated (20, 0), b.translated (20, 0), b);
                g.setColour (green.withAlpha (0.8f)); g.strokePath (p, juce::PathStrokeType (1.8f));
            }
            prev = c;
        }
    }

    // THE LIVING SOUND's name plate under the tube: the name in the tube's own light (purple to green), a striped mark
    // and a fine rule either side, and a line saying what you can do. With a hand on the body it names the gesture
    // being made (in that gesture's colour) and says what letting go will do. With no sound in the tube nothing here
    // does anything yet, so it is greyed.
    juce::Rectangle<int> livingArea() const { return livingR.getUnion (livingSubR).expanded (0.0f, 8.0f).toNearestInt(); }
    void paintLiving (juce::Graphics& g)
    {
        if (! g.clipRegionIntersects (livingArea())) return;
        const bool alive = ed.engine.hasSpecimen(), gesture = alive && live.isNotEmpty();
        const float on = alive ? 1.0f : 0.4f;
        const auto inkL = gesture ? opColour (liveOp) : purple, inkR = gesture ? opColour (liveOp) : green;
        const auto f = font (18.0f, 2, 0.32f);
        const auto ls = gesture ? live : juce::String ("THE LIVING SOUND");
        const float cx = livingR.getCentreX(), cy = livingR.getCentreY();
        juce::GlyphArrangement ga;
        ga.addLineOfText (f, ls, 0.0f, 0.0f);
        juce::Path name; ga.createPath (name);
        const auto nb = name.getBounds();
        name.applyTransform (juce::AffineTransform::translation (cx - nb.getCentreX(), cy - nb.getCentreY()));
        const float lw = nb.getWidth();
        if (alive)
        {
            // its glow: the tube's colours, left and right
            for (int side = 0; side < 2; ++side)
            {
                juce::Graphics::ScopedSaveState ss (g);
                const auto half = livingR.expanded (0.0f, 10.0f);
                g.reduceClipRegion ((side == 0 ? half.withRight (cx) : half.withLeft (cx)).toNearestInt());
                art::glow (g, name, (side == 0 ? inkL : inkR).withAlpha (side == 0 ? 0.55f : 0.45f), 10);
            }
        }
        juce::ColourGradient ink (text.interpolatedWith (inkL, gesture ? 0.55f : 0.3f).withAlpha (on), cx - lw * 0.5f, 0, text.interpolatedWith (inkR, gesture ? 0.55f : 0.3f).withAlpha (on), cx + lw * 0.5f, 0, false);
        ink.addColour (0.5, text.interpolatedWith (inkL, gesture ? 0.3f : 0.0f).withAlpha (on));
        g.setGradientFill (ink); g.fillPath (name);
        // the marks either side: four stripes, then a fine rule running out and fading
        const float room = livingR.getWidth() * 0.5f - lw * 0.5f - 22.0f - 44.0f - 6.0f;
        for (int side = 0; side < 2; ++side)
        {
            const float dir = side == 0 ? -1.0f : 1.0f, x0 = cx + dir * (lw * 0.5f + 22.0f);
            const auto c = (side == 0 ? inkL : inkR).withAlpha (0.9f * on);
            for (int k = 0; k < 4; ++k)
            {
                const float x = side == 0 ? x0 - 9.0f - (float) k * 10.0f : x0 + (float) k * 10.0f;
                juce::Path p; p.addQuadrilateral (x, cy + 4.5f, x + 5, cy + 4.5f, x + 9, cy - 4.5f, x + 4, cy - 4.5f);
                g.setColour (c.withMultipliedAlpha (1.0f - 0.17f * (float) k)); g.fillPath (p);
            }
            if (room > 24.0f)
            {
                const float r0 = x0 + dir * 50.0f, r1 = r0 + dir * juce::jmin (170.0f, room);
                g.setGradientFill (juce::ColourGradient (c.withMultipliedAlpha (0.75f), r0, 0, c.withAlpha (0.0f), r1, 0, false));
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (r0, r1), cy - 0.6f, juce::jmax (r0, r1), cy + 0.6f));
                juce::Path d; d.addQuadrilateral (r0 - 3.0f, cy, r0, cy - 3.0f, r0 + 3.0f, cy, r0, cy + 3.0f);
                g.setColour (c); g.fillPath (d);
            }
        }
        // the line under it
        juce::String say;
        if (! alive) say = "Drop a sound in to wake it up.";
        else if (liveHint.isNotEmpty()) say = liveHint;
        else if (ed.engine.busy()) say = "Making the real sound...";
        else say = ed.engine.hasPartner() ? "Drag, pull, stretch and twist your audio. Drag SPECIMEN " + juce::String (juce::jmax (1, ed.engine.specimenNumber) + 1).paddedLeft ('0', 3) + " onto it to breed."
                                          : juce::String ("Drag, pull, stretch and twist your audio.");
        g.setColour (! alive ? dim.brighter (0.25f) : (liveHint.isNotEmpty() ? text.withAlpha (0.86f) : sub.brighter (0.3f)));
        g.setFont (font (14.0f));
        g.drawFittedText (say, livingSubR.toNearestInt(), juce::Justification::centredTop, 2, 0.85f);
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        // the glass over the living sound: its walls, rim light, reflections, bubbles (drawn once per size / screen scale).
        // The phone-width page has a rounded window instead of the long tube.
        if (! body.isVisible() || ed.deepOn()) return;   // (DEEP's panel sits over the tube)
        const float sc = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        const auto key = tubeR.toString() + "@" + juce::String (sc, 2) + (ed.isNarrow() ? "n" : "w");
        const float pad = 12.0f;
        if (key != frontKey || ! front.isValid())
        {
            frontKey = key;
            front = juce::Image (juce::Image::ARGB, juce::roundToInt ((tubeR.getWidth() + 2 * pad) * sc), juce::roundToInt ((tubeR.getHeight() + 2 * pad) * sc), true);
            juce::Graphics fg (front);
            fg.addTransform (juce::AffineTransform::translation (pad - tubeR.getX(), pad - tubeR.getY()).scaled (sc));
            if (ed.isNarrow()) art::tankFront (fg, tubeR); else art::tubeFront (fg, tubeR);
        }
        g.setOpacity (1.0f);
        g.drawImageTransformed (front, juce::AffineTransform::scale (1.0f / sc).translated (tubeR.getX() - pad, tubeR.getY() - pad));
    }

    // wide: laid out on a virtual canvas width x height (at least 1200 x 668; the editor scales it to fill the window).
    // narrow: one scrolling column `width` wide.
    void layout (bool narrow, int width, int height = Editor::kH - Editor::kFooterH)
    {
        body.setNarrowStyle (narrow);
        const float W = (float) width;
        if (! narrow)
        {
            const float H = (float) juce::jmax (Editor::kH - Editor::kFooterH, height);
            m = 24.0f;   // the panels run edge to edge at any window size
            const float rightW = juce::jlimit (252.0f, 330.0f, (W - 2 * m) * 0.22f), gap = 22.0f;
            const float leftW = W - 2 * m - rightW - gap, rightX = W - m - rightW, leftR = m + leftW;
            // the header is Brandon's banner (the title, the ghost, the lab), across the whole width; the specimen buttons
            // sit on its lower left, the page buttons on its lower right (clear of the ghost), the status top right
            const bool roomOn = ed.roomNow() != rooms::Room::Lab;
            const bool hood = ed.hoodPageOn(), terr = ed.terrainPageOn();
            const bool fx = Processor::isFx;
            const bool page = hood || roomOn;
            const float srcRows = page ? 190.0f : 228.0f;                          // UNDER THE HOOD and the rooms: a little less banner, more page
            const float hk = juce::jmin (W / 1206.0f, (page ? 196.0f : 240.0f) / srcRows);
            bannerR = { 0, 0, W, srcRows * hk };
            bannerRows = srcRows;
            statusSize = 15;
            statusR = { W - m - 300, 14, 300, 30 };
            aboutPill.setBounds ((int) (W - m - 32), 52, 32, 32);
            const float ph = 32, py = bannerR.getBottom() - ph - 10;
            // the strip of rooms, under the banner on every page
            route.setBounds ((int) m, (int) bannerR.getBottom() + 5, (int) (W - 2 * m), 28);
            top = bannerR.getBottom() + 5 + 28 + 7;
            panelTop = top;
            specR = {};
            pill1.setBounds ((int) m, (int) py, 138, (int) ph);
            pill2.setBounds ((int) (m + 146), (int) py, 156, (int) ph);
            capturePill.setBounds ((int) (m + 310), (int) py, 112, (int) ph);
            if (hood)
            {
                hoodPill.setBounds ((int) (W - m - 190), (int) py, 190, (int) ph);
                deepPill.setBounds (0, 0, 0, 0); terrainPill.setBounds (0, 0, 0, 0);
            }
            else if (terr)
            {
                terrainPill.setBounds ((int) (W - m - 170), (int) py, 170, (int) ph);
                hoodPill.setBounds ((int) (W - m - 170 - 8 - 172), (int) py, 172, (int) ph);
                deepPill.setBounds (0, 0, 0, 0);
            }
            else
            {
                deepPill.setBounds ((int) (W - m - 88), (int) py, 88, (int) ph);
                terrainPill.setBounds ((int) (W - m - 88 - 8 - 116), (int) py, 116, (int) ph);
                hoodPill.setBounds ((int) (W - m - 88 - 8 - 116 - 8 - 172), (int) py, 172, (int) ph);
            }
            // the bottom: the waveforms, then the cards, THE LIVING SOUND and the tube above them
            const float extra = juce::jmax (0.0f, H - (float) (Editor::kH - Editor::kFooterH));   // a tall window: the cards and waves grow too, not just the tube
            const float wavesH = page ? 66.0f : 86.0f + juce::jmin (extra * 0.3f, 50.0f);
            wavesR = { m, H - 16 - wavesH, W - 2 * m, wavesH };
            const float halfW = (wavesR.getWidth() - 14) * 0.5f;
            original.setBounds (wavesR.withWidth (halfW).toNearestInt()); processed.setBounds (wavesR.withTrimmedLeft (halfW + 14).toNearestInt());
            const float ch = 106 + juce::jmin (extra * 0.3f, 50.0f), cardsY = wavesR.getY() - 14 - ch;
            livingR = { m, cardsY - 46, leftW, 24 }; livingSubR = { m, cardsY - 22, leftW, 20 };
            tubeR = { m, top + 4, leftW, livingR.getY() - 4 - (top + 4) };
            body.setBounds (art::tubeGlass (tubeR).toNearestInt());   // the living sound fills the liquid, wall to wall
            body.setTube (tubeR.translated ((float) -body.getX(), (float) -body.getY()));
            cardView.setBounds ((int) m, (int) cardsY, (int) leftW + 2, (int) ch);
            strip.setSize ((int) leftW + 2, (int) ch);
            const float cw = (leftW - 5 * 12) / 6.0f;
            for (int i = 0; i < actionCards.size(); ++i) actionCards[i]->setBounds (juce::roundToInt ((float) i * (cw + 12)), 0, juce::roundToInt (cw), (int) ch);
            // the right column: the EXPERIMENT TABLE
            tableFrame = { rightX - 6, top - 2, rightW + 12, cardsY + ch - top + 8 };
            tableR = { rightX + 8, top + 2, rightW - 16, 26 };
            hintR = { rightX + 8, cardsY + ch - 30, rightW - 16, 28 };
            const float tTop = tableR.getBottom() + 6, tBottom = hintR.getY() - 6;
            const float th = juce::jlimit (50.0f, 124.0f, (tBottom - tTop - 3 * 8) / 4.0f);
            for (int i = 0; i < 4; ++i) tableCards[i]->setBounds ((int) (rightX + 4), (int) (tTop + (float) i * (th + 8)), (int) (rightW - 8), (int) th);
            deepPanel.setBounds (body.getBounds().expanded (0, 6));
            toast.setBounds ((int) (m + 100), (int) (tubeR.getBottom() - 34), (int) (leftW - 200), 30);
            // TERRAIN: the land over the tube + cards area
            terrain->setBounds ((int) m, (int) top, (int) (W - 2 * m), (int) (wavesR.getY() - 32 - top));
            terrainHintR = { m, wavesR.getY() - 26, W - 2 * m, 22 };
            // UNDER THE HOOD: everything between the pills and the waveforms
            hoodPage->setBounds ((int) m, (int) top, (int) (W - 2 * m), (int) (wavesR.getY() - 10 - top));
            roomPage->setBounds (hoodPage->getBounds());
            showPage();
            setSize (width, (int) H);
            return;
        }
        m = 20; panelTop = 0;
        statusSize = 12.5f;
        // the banner across the top (the whole banner: on a phone it's not tall)
        bannerRows = 402.0f;
        bannerR = { 0, 0, W, W * 402.0f / 1206.0f };
        statusR = { W * 0.4f, bannerR.getBottom() + 4, W * 0.6f - m, 22 };
        aboutPill.setBounds ((int) (W - m - 28), 8, 28, 28);
        float y = statusR.getBottom() + 14;
        specR = { m, y, 130, 30 };
        pill1.setBounds ((int) (m + 128), (int) y, 120, 30);
        pill2.setBounds ((int) (m + 256), (int) y, 140, 30);
        if (W < 480) pill2.setBounds ((int) m, (int) y + 38, 140, 30);
        deepPill.setBounds ((int) (W - m - 70), (int) y, 70, 30);
        terrainPill.setBounds ((int) (W - m - 70 - 108), (int) y, 100, 30);
        hoodPill.setBounds ((int) m, (int) y + 38, 160, 30);
        if (W < 480) { terrainPill.setBounds ((int) m + 148, (int) y + 38, 100, 30); hoodPill.setBounds ((int) m + 256, (int) y + 38, 140, 30); deepPill.setBounds ((int) (W - m - 70), (int) y, 70, 30); }
        if (ed.hoodPageOn())
        {
            deepPill.setBounds (0, 0, 0, 0); terrainPill.setBounds (0, 0, 0, 0);
            if (W < 480) hoodPill.setBounds ((int) m + 150, (int) y + 38, 170, 30);
            else hoodPill.setBounds ((int) (W - m - 170), (int) y, 170, 30);
        }
        y += 76;
        route.setBounds ((int) m, (int) y, (int) (W - 2 * m), 28); y += 38;
        if (Processor::isFx) { capturePill.setBounds ((int) m, (int) y, 120, 30); y += 40; }
        const float bh = (W - 2 * m) * 0.52f;
        body.setBounds ((int) m, (int) y, (int) (W - 2 * m), (int) bh);
        body.setTube ({});
        tubeR = body.getBounds().toFloat();
        deepPanel.setBounds (body.getBounds().withHeight (juce::jmax ((int) bh, 220)));
        toast.setBounds ((int) m, (int) (y + bh - 34), (int) (W - 2 * m), 28);
        if (ed.roomNow() != rooms::Room::Lab)
        {
            const int th = 940;
            roomPage->setBounds ((int) m, (int) y, (int) (W - 2 * m), th);
            y += (float) th + 16;
            wavesR = { m, y, W - 2 * m, 230 };
            original.setBounds (wavesR.withHeight (110).toNearestInt());
            processed.setBounds (wavesR.withTrimmedTop (120).toNearestInt());
            y += 250;
            showPage();
            setSize (width, (int) y);
            return;
        }
        if (ed.hoodPageOn())
        {
            const int th = 980;
            hoodPage->setBounds ((int) m, (int) y, (int) (W - 2 * m), th);
            terrainHintR = { m, y + (float) th + 6, W - 2 * m, 52 };
            y += (float) th + 66;
            wavesR = { m, y, W - 2 * m, 230 };
            original.setBounds (wavesR.withHeight (110).toNearestInt());
            processed.setBounds (wavesR.withTrimmedTop (120).toNearestInt());
            y += 250;
            showPage();
            setSize (width, (int) y);
            return;
        }
        if (ed.terrainPageOn())
        {
            const int th = juce::jmax (380, (int) ((W - 2 * m) * 0.9f));
            terrain->setBounds ((int) m, (int) y, (int) (W - 2 * m), th);
            terrainHintR = { m, y + (float) th + 6, W - 2 * m, 52 };
            y += (float) th + 66;
            wavesR = { m, y, W - 2 * m, 230 };
            original.setBounds (wavesR.withHeight (110).toNearestInt());
            processed.setBounds (wavesR.withTrimmedTop (120).toNearestInt());
            y += 250;
            showPage();
            setSize (width, (int) y);
            return;
        }
        showPage();
        y += bh + 18;
        livingR = { m, y, W - 2 * m, 26 }; livingSubR = { m, y + 26, W - 2 * m, 42 };   // two lines: the breeding hint wraps, never "..."
        y += 78;
        const int cw = 118, ch = 128, gap = 12;
        cardView.setBounds ((int) m, (int) y, (int) (W - m), ch);
        strip.setSize (actionCards.size() * (cw + gap) + (int) m, ch);
        for (int i = 0; i < actionCards.size(); ++i) actionCards[i]->setBounds (i * (cw + gap), 0, cw, ch);
        y += ch + 24;
        tableR = { m, y, W - 2 * m, 24 };
        tableFrame = {};
        y += 32;
        const bool two = W >= 520;
        const float tw = two ? (W - 2 * m - 12) / 2 : W - 2 * m;
        for (int i = 0; i < 4; ++i)
        {
            const int col = two ? i % 2 : 0, row = two ? i / 2 : i;
            tableCards[i]->setBounds ((int) (m + col * (tw + 12)), (int) (y + row * 88), (int) tw, 76);
        }
        y += (two ? 2 : 4) * 88;
        hintR = { m, y, W - 2 * m, 30 };
        y += 40;
        wavesR = { m, y, W - 2 * m, 230 };
        original.setBounds (wavesR.withHeight (110).toNearestInt());
        processed.setBounds (wavesR.withTrimmedTop (120).toNearestInt());
        y += 250;
        setSize (width, (int) y);
    }

    void showPage()
    {
        const bool rm = ed.roomNow() != rooms::Room::Lab;
        const bool h = ed.hoodPageOn() && ! rm;
        const bool t = ed.terrainPageOn() && ! h && ! rm;
        hoodPage->setVisible (h);
        terrain->setVisible (t);
        roomPage->setVisible (rm);
        route.current = ed.roomNow(); route.repaint();
        body.setVisible (! t && ! h && ! rm);
        cardView.setVisible (! t && ! h && ! rm);
        for (auto* c : tableCards) c->setVisible (! t && ! h && ! rm);
        if (t || h || rm) deepPanel.setVisible (false);
        hoodPill.label = h ? "BACK TO THE LAB" : "UNDER THE HOOD";
        hoodPill.icon = h ? Pill::Back : Pill::Hood;
        hoodPill.filled = h;
        terrainPill.setVisible (! h);
        deepPill.setVisible (! h && ! ed.terrainPageOn() && ! rm);
        terrainPill.label = t ? "BACK TO THE LAB" : "TERRAIN";
        terrainPill.icon = t ? Pill::Back : Pill::Terrain;
        terrainPill.filled = t;
        sceneKey.clear();
    }

    Editor& ed;
    SoundBody body;
    Pill pill1, pill2, deepPill, capturePill, terrainPill, hoodPill, aboutPill;
    std::unique_ptr<HoodPage> hoodPage;
    std::unique_ptr<RoomPage> roomPage;
    RouteBar route;
    std::unique_ptr<ghost::terrain::TerrainView> terrain;
    juce::Rectangle<float> terrainHintR;
    juce::Viewport cardView;
    juce::Component strip;
    juce::OwnedArray<Card> actionCards, tableCards;
    WaveView original { false }, processed { true };
    DeepPanel deepPanel;
    Toast toast;
    juce::String live, liveHint;   // the gesture in hand on the living sound, and what letting go will do
    Op liveOp = Op::Count;
    float phase = 0;
    float titleSize = 56, subSize = 15, statusSize = 15, m = 24, panelTop = 104;
    juce::Rectangle<float> statusR, specR, tableR, livingR, livingSubR, hintR, wavesR, tubeR, tableFrame, bannerR;
    float bannerRows = 228.0f, top = 0;
    juce::Image scene, front;
    juce::String sceneKey, frontKey;
};

// ================================================================================================ the editor
Editor::Editor (Processor& p) : juce::AudioProcessorEditor (p), proc (p), engine (p.engine)
{
    const auto start = proc.editorSize;   // (read before anything below resizes the editor)
    setOpaque (true);
    setLookAndFeel (&look.get());   // our colours on every menu, list, box and dialog too (not JUCE's own)
    content = std::make_unique<Content> (*this);
    viewport = std::make_unique<juce::Viewport>();
    viewport->setViewedComponent (content.get(), false);
    viewport->setScrollBarThickness (6);
    viewport->getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, borderHi);
    addAndMakeVisible (*viewport);
    footer = std::make_unique<FooterBar>();
    addAndMakeVisible (*footer);
    footerLine = std::make_unique<juce::Component>();   // (the bar draws its own top line)
    footer->addChildComponent (*footerLine);
    undoBtn = std::make_unique<FooterButton> (FooterButton::Undo);
    redoBtn = std::make_unique<FooterButton> (FooterButton::Redo);
    genBtn = std::make_unique<FooterButton> (FooterButton::Generate);
    saveBtn = std::make_unique<FooterButton> (FooterButton::Save);
    for (auto* b : { undoBtn.get(), redoBtn.get(), genBtn.get(), saveBtn.get() }) footer->addAndMakeVisible (b);
    undoBtn->onClick = [this] { undo(); };
    redoBtn->onClick = [this] { redo(); };
    genBtn->onClick = [this] { generate(); };
    saveBtn->onClick = [this] { openSaveMenu(); };

    proc.onTerrainCommit = [this] { renderTerrain(); };

    setResizable (true, true);
    setResizeLimits (360, 560, 2000, 1400);
    setWantsKeyboardFocus (true);
    setSize (start.x > 0 ? start.x : kW, start.y > 0 ? start.y : kH);
    refresh();
    startTimerHz (20);
    if (! engine.cdp->available())
        toast ("Ghost Lab's sound engine wasn't found. Reinstall Ghost Lab.", true);
}

Editor::~Editor()
{
    stopTimer();
    if (openAlert != nullptr) delete openAlert.getComponent();
    setLookAndFeel (nullptr);
    proc.onTerrainCommit = nullptr;
    proc.setTerrainListening (false);
    proc.endBodyTouch();   // (a live loop under a hand never outlives the window)
}

HoodPage& Editor::hood() { return *content->hoodPage; }

RoomPage& Editor::roomPage() { return *content->roomPage; }
RouteBar& Editor::routeBar() { return content->route; }

void Editor::setRoom (rooms::Room r)
{
    if (r == roomOn) return;
    if (r != rooms::Room::Lab)
    {
        if (hoodPage) setHoodPage (false);
        if (terrainPage) setTerrainPage (false);
        if (deep) setDeep (false);
        proc.endBodyTouch();   // (the body is off the page: a live loop under a hand ends)
    }
    proc.setSpaceListening (false);
    roomOn = r;
    if (r != rooms::Room::Lab) content->roomPage->setRoom (r);
    resized();
    refresh();
}

void Editor::runRoom (Op op)
{
    if (! engine.hasSpecimen()) { toast ("Load a sound first, or GROW one.", true); return; }
    if (engine.busy()) { toast ("Still working on the last one..."); return; }
    proc.setSpaceListening (false);
    Step s = settingsFor (op);
    s.op = op;
    // running the same room again replaces its last run (tweak and try again: nothing stacks up)
    const auto word = juce::String (rooms::info (rooms::roomOf (op)).name) + " ";
    const bool again = engine.historyCursor() > 0 && engine.current().label.startsWith (word);
    auto done = [safe = juce::Component::SafePointer<Editor> (this)] (bool ok, const juce::String& m)
    {
        if (safe == nullptr) return;   // (the window was closed before the render landed)
        safe->toast (ok ? m + ". Listen: this is the new sound." : m, ! ok);
        if (ok) { safe->proc.player.setMidiSlot (Player::Processed); safe->proc.player.play (Player::Processed, 0.0); }
        safe->refresh();
    };
    if (again) engine.applyReplacingLast (s, done); else engine.apply (s, done);
    refresh();
}

void Editor::growRoom (bool asSecond)
{
    if (engine.busy()) { toast ("Still working on the last one..."); return; }
    proc.setSpaceListening (false);
    Step s = settingsFor (Op::Grow);
    s.op = Op::Grow;
    engine.grow (s, asSecond, [safe = juce::Component::SafePointer<Editor> (this), asSecond] (bool ok, const juce::String& m)
    {
        if (safe == nullptr) return;
        safe->toast (ok ? (asSecond ? m + " is your second sound." : m + " is your new specimen. Take it to THE LAB.") : m, ! ok);
        if (ok && ! asSecond) { safe->proc.player.setMidiSlot (Player::Processed); safe->proc.player.play (Player::Processed, 0.0); }
        safe->refresh();
    });
    refresh();
}

void Editor::spaceListen()
{
    if (! engine.hasSpecimen()) { toast ("Load a sound first, or GROW one.", true); return; }
    if (proc.spaceListening()) { proc.setSpaceListening (false); return; }
    // the sound underneath: if SPACE already printed, the one before that print (so the room is never heard twice)
    const bool printed = engine.historyCursor() > 0 && engine.current().label.startsWith ("SPACE ");
    float v[rooms::kSpaceControls]; rooms::spacePins (settingsFor (Op::Space), v);
    proc.setSpaceLive (v);
    proc.setSpaceListening (true, printed ? engine.audioUnderLast() : engine.processedAudio());
}

void Editor::setHoodPage (bool on)
{
    if (hoodPage == on) return;
    if (on && roomOn != rooms::Room::Lab) setRoom (rooms::Room::Lab);
    if (on && terrainPage) setTerrainPage (false);
    if (on) proc.endBodyTouch();   // (the body is off the page: a live loop under a hand ends)
    hoodPage = on;
    if (on && deep) setDeep (false);
    resized();
    refresh();
}

void Editor::runHood (const Step& s)
{
    if (! engine.hasSpecimen()) { toast ("Drop a sound first.", true); return; }
    if (engine.busy()) { toast ("Still working on the last one..."); return; }
    engine.apply (s, [safe = juce::Component::SafePointer<Editor> (this)] (bool ok, const juce::String& m)
    {
        if (safe == nullptr) return;   // (the window was closed before the render landed)
        safe->toast (ok ? m : m, ! ok);
        safe->content->hoodPage->showRan (safe->engine.lastCommands, ok);
        safe->refresh();
    });
    refresh();
}

ghost::terrain::TerrainView& Editor::terrainView() { return *content->terrain; }

void Editor::setTerrainPage (bool on)
{
    if (terrainPage == on) return;
    if (on && roomOn != rooms::Room::Lab) setRoom (rooms::Room::Lab);
    if (on && hoodPage) setHoodPage (false);
    if (on) proc.endBodyTouch();   // (the body is off the page: a live loop under a hand ends)
    terrainPage = on;
    proc.setTerrainPage (on);
    if (! on) proc.setTerrainListening (false);
    else if (deep) setDeep (false);
    resized();
    refresh();
    if (on && engine.hasSpecimen() && engine.historyCursor() == 0 && ! proc.terrain.terrain().empty()) renderTerrain();
}

void Editor::renderTerrain()
{
    if (! engine.hasSpecimen()) return;
    if (engine.busy()) { terrainPending = true; return; }
    Step s; s.op = Op::Terrain; s.seed = 1;
    s.values["strength"] = 1.0f;
    s.data = ghost::terrain::toString (proc.terrain.terrain());
    const bool topIsTerrain = engine.historyCursor() > 0 && engine.current().label.startsWith ("TERRAIN");
    const auto rendering = proc.terrain.terrain();
    auto done = [this, safe = juce::Component::SafePointer<Editor> (this), rendering] (bool ok, const juce::String& m)
    {
        if (safe == nullptr) return;   // (the window was closed before the render landed)
        if (! ok) toast (m, true);
        // build 4: the exact render cross-fades in under the live path (only if the land is still the one it rendered)
        if (ok && ! terrainPending && proc.terrain.terrain().sameLand (rendering)) proc.setExactPreview (engine.processedAudio());
        refresh();
        if (terrainPending) { terrainPending = false; renderTerrain(); }
    };
    if (topIsTerrain) engine.applyReplacingLast (s, done); else engine.apply (s, done);
    refresh();
}

void Editor::terrainListen()
{
    if (! engine.hasSpecimen()) { toast ("Drop a sound first.", true); return; }
    proc.setTerrainListening (! proc.terrainListening());
    content->terrain->repaint();
}

juce::Component& Editor::contentComponent() { return *content; }
SoundBody& Editor::body() { return content->body; }

void Editor::paint (juce::Graphics& g)
{
    g.fillAll (night0);
}

void Editor::resized()
{
    proc.editorSize = { getWidth(), getHeight() };
    narrow = getWidth() < kNarrowBelow;
    if (narrow) layoutNarrow(); else layoutWide();
}

// wide: the page is laid out on a canvas the window's shape (at least 1200 x 760) and scaled to fill the whole window:
// no empty bands at the sides or the top and bottom, whatever size FL gives the plug-in.
void Editor::layoutWide()
{
    const float w = (float) juce::jmax (1, getWidth()), h = (float) juce::jmax (1, getHeight());
    float s; int VW, VH;
    if (w / (float) kW >= h / (float) kH) { s = h / (float) kH; VH = kH; VW = juce::jmax (kW, (int) std::floor (w / s)); }
    else { s = w / (float) kW; VW = kW; VH = juce::jmax (kH, (int) std::floor (h / s)); }
    content->layout (false, VW, VH - kFooterH);
    const auto tr = juce::AffineTransform::scale (s);
    viewport->setTransform (tr);
    viewport->setBounds (0, 0, VW, VH - kFooterH);
    viewport->setScrollBarsShown (false, false);
    footer->setTransform (tr);
    footer->setBounds (0, VH - kFooterH, VW, kFooterH);
    const int gw = 400, gx = (VW - gw) / 2, fm = 24;
    undoBtn->setBounds (fm, 18, 150, 54);
    redoBtn->setBounds (fm + 160, 18, 132, 54);
    genBtn->setBounds (gx, 14, gw, 62);
    saveBtn->setBounds (VW - fm - 160, 18, 160, 54);
    footerLine->setBounds (0, 0, VW, 1);
    if (auto* fb = dynamic_cast<FooterBar*> (footer.get())) { fb->stripesL = { (float) gx - 58, 38, 40, 14 }; fb->stripesR = { (float) gx + gw + 18, 38, 40, 14 }; fb->repaint(); }
}

void Editor::layoutNarrow()
{
    viewport->setTransform ({});
    footer->setTransform ({});
    const int fh = 96;
    viewport->setBounds (0, 0, getWidth(), getHeight() - fh);
    viewport->setScrollBarsShown (true, false);
    content->layout (true, getWidth() - 6);
    footer->setBounds (0, getHeight() - fh, getWidth(), fh);
    footerLine->setBounds (0, 0, getWidth(), 1);
    if (auto* fb = dynamic_cast<FooterBar*> (footer.get())) { fb->stripesL = {}; fb->stripesR = {}; }
    const int m = 16, W = getWidth() - 2 * m;
    const int uw = (int) (W * 0.24f), gw = (int) (W * 0.44f);
    undoBtn->setBounds (m, 22, uw, 56);
    redoBtn->setBounds (0, 0, 0, 0);
    genBtn->setBounds (m + uw + 8, 18, gw, 64);
    saveBtn->setBounds (m + uw + gw + 16, 22, W - uw - gw - 16, 56);
}

Step& Editor::settingsFor (Op op)
{
    for (auto& slot : engine.chain) if (slot.op == op) return slot.step;   // the experiment table keeps its own settings
    auto it = settings.find (op);
    if (it == settings.end()) it = settings.emplace (op, Step { op, {}, 1 }).first;
    return it->second;
}

juce::String Editor::statusText() const
{
    if (engine.busy()) return "WORKING";
    if (proc.capturing()) return "CAPTURING " + juce::String (proc.capturedSeconds(), 1) + "s";
    if (! engine.cdp->available()) return "ENGINE MISSING";
    return engine.hasSpecimen() ? "AUDIO READY" : "DROP OR GROW A SOUND";
}

void Editor::refresh()
{
    auto& c = *content;
    const bool has = engine.hasSpecimen();
    c.body.setEmpty (! has);
    if (has) c.body.setProfile (engine.current().profile);
    c.body.setBusy (engine.busy());
    if (roomOn != rooms::Room::Lab) c.roomPage->soundChanged();
    if (hoodPage && ! engine.busy()) c.hoodPage->soundChanged();   // the ORIGINAL page's ranges follow the sound (asked once per sound)
    c.pill1.label = has ? "SPECIMEN " + juce::String (juce::jmax (1, engine.specimenNumber)).paddedLeft ('0', 3) : juce::String ("+ LOAD SOUND");
    c.pill1.dashed = ! has;
    c.pill2.label = engine.hasPartner() ? "SPECIMEN " + juce::String (juce::jmax (1, engine.specimenNumber) + 1).paddedLeft ('0', 3) : juce::String ("+ SECOND SOUND");
    c.pill2.dashed = ! engine.hasPartner();
    c.pill2.filled = engine.hasPartner();
    c.pill2.dragSource = engine.hasPartner();
    c.deepPill.label = deep ? "DEEP ON" : "DEEP";
    c.deepPill.filled = deep;
    c.capturePill.label = proc.capturing() ? "STOP CAPTURE" : "CAPTURE";
    c.capturePill.filled = proc.capturing();
    for (auto* card : c.actionCards) { card->selected = deep && card->op == selected; card->repaint(); }
    for (int i = 0; i < 4; ++i)
    {
        auto* card = c.tableCards[i];
        card->op = engine.chain[(size_t) i].op;
        card->inChain = engine.chain[(size_t) i].on;
        card->selected = deep && card->op == selected;
        card->acceptsFiles = card->op == Op::VoiceFusion;
        card->repaint();
    }
    c.original.profile = &engine.originalProfile();
    c.original.hasSound = has;
    c.original.info = has ? juce::String (engine.originalAudio()->seconds(), 1) + " s" : juce::String();
    c.original.seconds = has ? engine.originalAudio()->seconds() : 0.0;
    c.processed.seconds = has ? engine.processedAudio()->seconds() : 0.0;
    c.processed.profile = has ? &engine.current().profile : nullptr;
    c.processed.hasSound = has;
    c.processed.info = has ? juce::String (engine.processedAudio()->seconds(), 1) + " s  " + (engine.historyCursor() == 0 ? juce::String ("(untouched)") : engine.current().label) : juce::String();
    undoBtn->setEnabled (terrainPage ? (proc.terrain.canUndo() || engine.canUndo()) && ! engine.busy() : engine.canUndo() && ! engine.busy());
    redoBtn->setEnabled (terrainPage ? (proc.terrain.canRedo() || engine.canRedo()) && ! engine.busy() : engine.canRedo() && ! engine.busy());
    c.terrainPill.repaint();
    genBtn->setEnabled (has);
    genBtn->busy = engine.busy();
    saveBtn->setEnabled (true);
    for (auto* b : { undoBtn.get(), redoBtn.get(), genBtn.get(), saveBtn.get() }) b->repaint();
    c.repaint();
}

void Editor::timerCallback()
{
    auto& c = *content;
    c.phase += 0.25f;
    const int slot = proc.player.playingSlot();
    const float head = proc.player.playhead();
    // (a hand on the living sound plays the processed sound too: through the live chain)
    const bool processedPlays = slot == Player::Processed || slot == Player::Touch;
    c.original.playing = slot == Player::Original; c.original.playhead = head;
    c.processed.playing = processedPlays; c.processed.playhead = head;
    c.body.setPlayhead (processedPlays ? head : -1.0f);
    if (terrainPage)
    {
        c.terrain->setPlayhead (proc.terrainPlayhead());
        c.terrain->setMeters (proc.labLand().meters);   // build 4: what the land is doing to the sound
        if (c.terrain->analyzerVisible())
        {
            std::vector<float> db;
            if (proc.terrainTap.read (db, 160, c.terrain->range, proc.getSampleRate() > 0 ? proc.getSampleRate() : 44100.0)) c.terrain->setSpectrum (std::move (db));
        }
    }
    c.original.repaint(); c.processed.repaint();
    c.body.setBusy (engine.busy());
    if (engine.busy() || proc.capturing())
    {
        genBtn->phase = c.phase; genBtn->repaint();
        c.repaint (c.statusR.toNearestInt().expanded (30, 4));
    }
}

bool Editor::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)) { undo(); return true; }
    if (k == juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)) { redo(); return true; }
    if (k == juce::KeyPress::spaceKey) { if (terrainPage) terrainListen(); else playWave (Player::Processed); return true; }
    return false;
}

void Editor::toast (const juce::String& msg, bool bad) { content->toast.show (msg, bad); content->toast.toFront (false); }
void Editor::hideToast() { content->toast.hide(); }

void Editor::setDeep (bool on)
{
    deep = on;
    content->deepPanel.setVisible (on);
    if (on) { content->deepPanel.show (selected); content->deepPanel.toFront (false); }
    refresh();
}

void Editor::card (Op op)
{
    if (! engine.hasSpecimen()) { toast ("Drop a sound first.", true); return; }
    if (deep) { selected = op; content->deepPanel.show (op); refresh(); return; }   // DEEP: tap selects, APPLY runs
    run (settingsFor (op));
}

void Editor::run (const Step& sIn, bool fromBody)
{
    if (! engine.hasSpecimen()) { toast ("Drop a sound first.", true); if (fromBody) proc.endBodyTouch(); return; }
    if (engine.busy()) { toast ("Still working on the last one..."); if (fromBody) proc.endBodyTouch(); return; }
    // a gesture brings its own amount; everything else comes from the card's settings
    Step s = settingsFor (sIn.op);
    for (auto& kv : sIn.values) s.values[kv.first] = kv.second;
    s.op = sIn.op;
    s.seed = sIn.op == Op::Mutate || sIn.op == Op::GrainCloud || sIn.op == Op::Shatter ? ++seed * 2654435761u : sIn.seed;
    // (the window may be closed before the render lands: then there is nobody to tell)
    engine.apply (s, [safe = juce::Component::SafePointer<Editor> (this), fromBody] (bool ok, const juce::String& m)
    {
        if (safe == nullptr) return;
        safe->toast (ok ? m : m, ! ok);
        // a gesture on the living sound: the live sketch gives way to the real thing, played from its start
        if (fromBody) { if (ok) safe->proc.bodyResultLanded(); else safe->proc.endBodyTouch(); }
        safe->refresh();
    });
    refresh();
}

void Editor::bodyFeel (const SoundBody::Feel& f)
{
    if (f.active)
    {
        // a hand is on the living sound and has moved: the sound plays, round and round, through the live chain, and
        // changes as the hand does
        proc.beginBodyTouch (f.where);
        Processor::BodyTouch b;
        b.pull = f.pull; b.pinch = f.pinch; b.twist = f.twist; b.shake = f.shake; b.bend = f.bend;
        proc.setBodyTouch (b);
    }
    else if (! f.commit) proc.endBodyTouch();   // it let go without making a gesture: nothing more is coming
    // (it made one: the live sound holds as it was let go until the real render lands - see run())
}

void Editor::generate()
{
    if (! engine.hasSpecimen()) { toast ("Drop a sound first.", true); chooseFile (false); return; }
    if (engine.busy()) { toast ("Still working on the last one..."); return; }
    engine.runChain (++seed * 2246822519u, [safe = juce::Component::SafePointer<Editor> (this)] (bool ok, const juce::String& m)
                     { if (safe != nullptr) { safe->toast (ok ? "Mutation: " + m : m, ! ok); safe->refresh(); } });
    refresh();
}

void Editor::breed()
{
    if (! engine.hasPartner()) { toast ("Drop a second sound on the + SECOND SOUND pill first.", true); return; }
    if (engine.busy()) { toast ("Still working on the last one..."); return; }
    engine.breed (settingsFor (Op::Breed).get ("lean"), [safe = juce::Component::SafePointer<Editor> (this)] (bool ok, const juce::String& m)
                  { if (safe != nullptr) { safe->toast (ok ? "Bred a new specimen from the two." : m, ! ok); safe->refresh(); } });
    refresh();
}

void Editor::undo()
{
    if (terrainPage && proc.terrain.canUndo()) { proc.terrain.undo(); toast ("Undone: the landscape"); refresh(); return; }
    if (engine.undo()) toast ("Undone: back to " + (engine.historyCursor() == 0 ? juce::String ("the original") : engine.current().label));
    refresh();
}

void Editor::redo()
{
    if (terrainPage && proc.terrain.canRedo()) { proc.terrain.redo(); toast ("Redone: the landscape"); refresh(); return; }
    if (engine.redo()) toast ("Redone: " + engine.current().label);
    refresh();
}

void Editor::loadFiles (const juce::StringArray& files, bool asPartner)
{
    juce::StringArray ok;
    for (auto& f : files) if (canLoad (juce::File (f))) ok.add (f);
    if (ok.isEmpty()) { toast ("Ghost Lab can't open that file. Try a WAV, AIFF, FLAC or MP3.", true); return; }
    if (engine.busy()) { toast ("Still working on the last one..."); return; }
    juce::String err;
    if (asPartner)
    {
        if (! engine.loadPartner (juce::File (ok[0]), err)) { toast (err, true); return; }
        toast (err.isNotEmpty() ? err : "Second specimen ready. Drag it onto the body to breed.", err.isNotEmpty());
    }
    else
    {
        if (! engine.loadSpecimen (juce::File (ok[0]), err)) { toast (err, true); return; }
        if (ok.size() > 1) engine.loadPartner (juce::File (ok[1]), err);
        toast (err.isNotEmpty() ? err : (ok.size() > 1 ? juce::String ("Two specimens loaded. Drag the second onto the body to breed them.") : "Specimen loaded. Touch it."), err.isNotEmpty());
    }
    refresh();
}

void Editor::chooseFile (bool asPartner)
{
    chooser = std::make_unique<juce::FileChooser> (asPartner ? "Choose a second sound" : "Choose a sound", juce::File(), formats().getWildcardForAllFormats());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, asPartner] (const juce::FileChooser& fc)
    {
        if (fc.getResult().existsAsFile()) loadFiles ({ fc.getResult().getFullPathName() }, asPartner);
    });
}

void Editor::playWave (int slot)
{
    if (! engine.hasSpecimen()) return;
    const int now = proc.player.playingSlot();
    proc.forgetBodyTouch();                            // (you chose what to listen to: a waiting live loop lets go)
    if (now == slot) { proc.player.stop(); return; }
    proc.player.setMidiSlot (slot);                    // the keyboard plays what you last listened to
    proc.player.play (slot, now >= 0 ? -1.0 : 0.0);    // switching A/B keeps your place
}

juce::Component& Editor::waveComponent (bool processedWave) { return processedWave ? (juce::Component&) content->processed : (juce::Component&) content->original; }

void Editor::trimWave (bool processedWave, float from, float to)
{
    if (! engine.hasSpecimen()) return;
    if (engine.busy()) { toast ("Still working on the last one..."); return; }
    proc.forgetBodyTouch();
    proc.setSpaceListening (false);
    proc.player.stop();
    juce::String err;
    const bool ok = processedWave ? engine.trimProcessed (from, to, err) : engine.trimOriginal (from, to, err);
    if (! ok) { toast (err, true); refresh(); return; }
    const auto len = juce::String ((processedWave ? engine.processedAudio() : engine.originalAudio())->seconds(), 1) + " s";
    toast (processedWave ? "Trimmed to " + len + ". UNDO brings the rest back."
                         : "The original is now the " + len + " you kept: everything works on this. UNDO brings the whole sound back.");
    const int slot = processedWave ? Player::Processed : Player::Original;
    proc.player.setMidiSlot (slot);
    proc.player.play (slot, 0.0);
    refresh();
}

void Editor::dragOut()
{
    juce::String err;
    auto f = engine.exportProcessed (err);
    if (f == juce::File()) { toast (err, true); return; }
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, &content->processed);
}

void Editor::openSaveMenu()
{
    juce::PopupMenu m, load;
    m.addItem (1, "Save the sound (24-bit WAV)", engine.hasSpecimen());
    m.addItem (2, "Save this chain as a favourite...");
    auto files = Engine::chainsFolder().findChildFiles (juce::File::findFiles, false, "*.ghostchain");
    files.sort();
    for (int i = 0; i < files.size(); ++i) load.addItem (100 + i, files[i].getFileNameWithoutExtension());
    m.addSubMenu ("Load a favourite chain", load, ! files.isEmpty());
    m.addSeparator();
    m.addItem (3, "Back to the original sound", engine.hasSpecimen() && engine.historyCursor() != 0);
    m.addItem (4, "Show my saved sounds");
    m.addItem (5, "Remove the second specimen", engine.hasPartner());

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (saveBtn.get()), [this, files] (int r)
    {
        juce::String err;
        if (r == 1)
        {
            auto f = engine.exportProcessed (err, Engine::exportsFolder());
            toast (f != juce::File() ? "Saved " + f.getFileName() + " in Documents/GhostBlock/Ghost Lab" : err, f == juce::File());
        }
        else if (r == 2)
        {
            auto* w = new juce::AlertWindow ("Save this chain", "Give it a name:", juce::MessageBoxIconType::NoIcon);
            w->setLookAndFeel (&look.get()); openAlert = w;
            w->addTextEditor ("name", "My chain");
            w->addButton ("SAVE", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int res)
            {
                juce::String e;
                if (res == 1) { if (engine.saveChain (w->getTextEditorContents ("name"), e)) toast ("Chain saved."); else toast (e, true); }
            }), true);
        }
        else if (r == 3) { engine.backToOriginal(); refresh(); }
        else if (r == 4) { Engine::exportsFolder().createDirectory(); Engine::exportsFolder().startAsProcess(); }
        else if (r == 5) { engine.clearPartner(); refresh(); }
        else if (r >= 100 && r - 100 < files.size())
        {
            if (engine.loadChain (files[r - 100], err)) toast ("Loaded the chain " + files[r - 100].getFileNameWithoutExtension()); else toast (err, true);
            refresh();
        }
    });
}

// ------------------------------------------------------------------------------------------------ About / Credits
// The only place the open-source projects inside Ghost Lab are named (their licences require this notice: who made them,
// the licence, where the source is). Everywhere else the UI says ENGINE / UNDER THE HOOD.
juce::String Editor::creditsText()
{
    return juce::String ("GHOST LAB ") + GHOSTLAB_VERSION + "  by GhostBlock\n"
        "Free and open-source software: GNU Affero General Public License, version 3 only (AGPL-3.0). The complete source of\n"
        "this build is in the download (Licenses/Ghost Lab source.zip). The full notices are in NOTICE.md there.\n\n"
        "THE LAB'S ENGINE\n"
        "The CDP System, Release 8 (Composers Desktop Project). Copyright (c) 2022 Composers Desktop Project; programs by\n"
        "Trevor Wishart and the CDP authors. Licence: LGPL-2.1 or later. Source: https://github.com/ComposersDesktop/CDP8\n"
        "(commit 28bc42c72c1a7cb0fab933acd1c433be958a787b). Its programs ship unmodified as separate programs inside the\n"
        "plug-in (Contents/Helpers/cdp), with the licence, the notice and the full source (Licenses/CDP8-source.zip).\n\n"
        "GROW\n"
        "Flues (PM-Synth, Chatterbox, Disyn) by Danny Ayers. MIT License. https://github.com/danja/flues (commit 2eae800).\n\n"
        "STRETCH\n"
        "PaulXStretch by Nasca Octavian Paul, Xenakios and Jesse Chappell. GPL-3.0. https://github.com/essej/paulxstretch\n"
        "(commit b6175e9). Its FFT: PFFFT by Julien Pommier (BSD-like) with FFTPACK (UCAR).\n\n"
        "REBORN\n"
        "DDSP-VST by Google / the Magenta team, with its instrument models. Apache-2.0. https://github.com/magenta/ddsp-vst\n"
        "(commit f2996e9). It runs on TensorFlow Lite (the TensorFlow authors, Apache-2.0).\n\n"
        "SPACE\n"
        "Pertylizer (Spatial Panner, Reverb) by Per Jonsson. MIT License. https://github.com/pertyjons/pertylizer (commit 3726360).\n\n"
        "UNDER THE HOOD, IN PLAIN WORDS\n"
        "Some of the longer descriptions of the lab engine's processes come from SoundThread (https://github.com/j-p-higgins/SoundThread),\n"
        "Copyright (c) 2025 Jonathan Higgins, MIT License. The rest of the names and words are ours.\n\n"
        "TYPE: Poppins by the Indian Type Foundry (SIL Open Font License 1.1).   FRAMEWORK: JUCE 8 (https://juce.com), under its AGPLv3 option.\n\n"
        "Every engine runs as its authors wrote it. The licence files are in the Licenses folder of the download and inside the\n"
        "plug-in (Contents/Resources/CREDITS.txt).";
}

void Editor::showAbout()
{
    auto* w = new juce::AlertWindow ("About / Credits", creditsText(), juce::MessageBoxIconType::NoIcon, this);
    w->setLookAndFeel (&look.get());
    if (openAlert != nullptr) delete openAlert.getComponent();
    openAlert = w;
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey), juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, nullptr, true);
}
}
