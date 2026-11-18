// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Theme.h"
#include "GhostAtlasArt.h"

namespace atlas::ui
{
// ============================================================================================ type
static juce::Typeface::Ptr face (const char* resource)
{
    int size = 0;
    if (const char* d = GhostAtlasArt::getNamedResource (resource, size)) return juce::Typeface::createSystemTypefaceFor (d, (size_t) size);
    return {};
}
// (see Kept in Theme.h for why none of this is a static; all of it is touched from the message thread only)
static Kept* kept = nullptr;
Kept::Kept() { kept = this; }
Kept::~Kept() { if (kept == this) kept = nullptr; }

static juce::Font made (int which, const char* resource, float height, float kerning = 0.0f)
{
    juce::Typeface::Ptr tf;
    if (kept != nullptr)
    {
        auto& slot = kept->faces[(size_t) which];
        if (slot == nullptr) slot = face (resource);
        tf = slot;
    }
    juce::Font f = tf != nullptr ? juce::Font (juce::FontOptions (tf).withHeight (height)) : juce::Font (juce::FontOptions (height));
    if (kerning != 0.0f) f.setExtraKerningFactor (kerning);
    return f;
}
juce::Font fellItalic (float h) { return made (0, "IMFeENit28P_ttf", h); }
juce::Font fellRoman (float h)  { return made (1, "IMFeENrm28P_ttf", h); }
juce::Font fellCaps (float h)   { return made (2, "IMFeENsc28P_ttf", h, 0.05f); }
juce::Font sans (float h)       { return made (3, "AtkinsonHyperlegibleRegular_ttf", h); }
juce::Font sansBold (float h)   { return made (4, "AtkinsonHyperlegibleBold_ttf", h); }

// ============================================================================================ colour
Look lookFor (int w)
{
    Look l;
    const auto& d = world (w);
    l.accent = juce::Colour (d.accent); l.accent2 = juce::Colour (d.accent2); l.deep = juce::Colour (d.deep);
    return l;
}
Look& look() { static Look l = lookFor (7); return l; }

// ============================================================================================ words
static bool keepsCapitals (const juce::String& w)
{
    static const juce::StringArray keep { "LFO", "EQ", "FX", "OTT", "FM", "MIDI", "CC", "DC", "IR", "HP", "LP", "BP", "LR", "MS", "A", "B", "C", "X", "Y", "II", "III", "SFZ", "SF2", "CC0", "MB", "GB" };
    return keep.contains (w) || w.containsAnyOf ("0123456789/#");
}
static juce::String cased (const juce::String& s, bool everyWord)
{
    if (s.isEmpty() || s != s.toUpperCase()) return s;          // already written as words
    juce::StringArray words; words.addTokens (s, " ", "");
    bool first = true;
    for (auto& w : words)
    {
        if (w.isEmpty()) continue;
        if (! keepsCapitals (w)) w = (first || everyWord) ? w.substring (0, 1) + w.substring (1).toLowerCase() : w.toLowerCase();
        first = false;
    }
    return words.joinIntoString (" ");
}
juce::String worded (const juce::String& s) { return cased (s, false); }
juce::String named (const juce::String& s) { return cased (s, true); }

// ============================================================================================ drawing
void drawFitted (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, juce::Justification j, const juce::Font& font, float minScale)
{
    juce::Font f = font;
    const float w = juce::GlyphArrangement::getStringWidth (f, text);
    if (w > r.getWidth() && w > 0.0f) f.setHorizontalScale (juce::jmax (minScale, r.getWidth() / w));
    g.setFont (f);
    g.drawText (text, r, j, true);
}
void softShadowText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, juce::Justification j, const juce::Font& font, juce::Colour colour, juce::Colour shadow)
{
    g.setFont (font);
    g.setColour (shadow);
    for (auto d : { juce::Point<float> (0.0f, 1.5f), juce::Point<float> (0.0f, -0.5f), juce::Point<float> (1.0f, 1.0f), juce::Point<float> (-1.0f, 1.0f) }) g.drawText (text, r.translated (d.x, d.y), j, false);
    g.setColour (colour);
    g.drawText (text, r, j, false);
}
void glow (juce::Graphics& g, juce::Point<float> c, float radius, juce::Colour colour, float strength)
{
    if (radius <= 0.5f || strength <= 0.003f) return;
    juce::ColourGradient grad (colour.withAlpha (juce::jlimit (0.0f, 1.0f, strength)), c.x, c.y, colour.withAlpha (0.0f), c.x + radius, c.y, true);
    grad.addColour (0.35, colour.withAlpha (juce::jlimit (0.0f, 1.0f, strength * 0.42f)));
    grad.addColour (0.7, colour.withAlpha (juce::jlimit (0.0f, 1.0f, strength * 0.1f)));
    g.setGradientFill (grad);
    g.fillEllipse (c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f);
}
void heart (juce::Graphics& g, juce::Rectangle<float> r, bool filled, juce::Colour colour)
{
    const float s = juce::jmin (r.getWidth(), r.getHeight()), x = r.getCentreX() - s * 0.5f, y = r.getCentreY() - s * 0.5f;
    juce::Path p;
    p.startNewSubPath (x + s * 0.5f, y + s * 0.9f);
    p.cubicTo (x - s * 0.12f, y + s * 0.48f, x + s * 0.1f, y + s * 0.02f, x + s * 0.5f, y + s * 0.3f);
    p.cubicTo (x + s * 0.9f, y + s * 0.02f, x + s * 1.12f, y + s * 0.48f, x + s * 0.5f, y + s * 0.9f);
    p.closeSubPath();
    g.setColour (colour);
    if (filled) g.fillPath (p); else g.strokePath (p, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved));
}
void chevron (juce::Graphics& g, juce::Rectangle<float> r, int direction, juce::Colour colour, float thickness)
{
    const float cx = r.getCentreX(), cy = r.getCentreY(), a = juce::jmin (r.getWidth(), r.getHeight()) * 0.26f;
    juce::Path p;
    if (direction == 0)      { p.startNewSubPath (cx + a * 0.6f, cy - a); p.lineTo (cx - a * 0.6f, cy); p.lineTo (cx + a * 0.6f, cy + a); }
    else if (direction == 1) { p.startNewSubPath (cx - a * 0.6f, cy - a); p.lineTo (cx + a * 0.6f, cy); p.lineTo (cx - a * 0.6f, cy + a); }
    else if (direction == 2) { p.startNewSubPath (cx - a, cy - a * 0.6f); p.lineTo (cx, cy + a * 0.6f); p.lineTo (cx + a, cy - a * 0.6f); }
    else                     { p.startNewSubPath (cx - a, cy + a * 0.6f); p.lineTo (cx, cy - a * 0.6f); p.lineTo (cx + a, cy + a * 0.6f); }
    g.setColour (colour);
    g.strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// ============================================================================================ pictures
juce::Image loadPicture (const char* resource)
{
    int size = 0;
    if (const char* d = GhostAtlasArt::getNamedResource (resource, size)) return juce::ImageFileFormat::loadFrom (d, (size_t) size);
    return {};
}
juce::Image worldThumb (int w)
{
    w = juce::jlimit (0, kWorlds, w);
    const auto resource = "t" + juce::String (w) + "_jpg";
    if (kept == nullptr) return loadPicture (resource.toRawUTF8());
    auto& slot = kept->thumbs[(size_t) w];
    if (slot.isNull()) slot = loadPicture (resource.toRawUTF8());
    return slot;
}

// box blur, run a few times (which comes close to a Gaussian), on a small copy that is scaled back up
juce::Image blurred (const juce::Image& source, int downscale, int radius, int passes)
{
    if (source.isNull()) return {};
    const int w = juce::jmax (8, source.getWidth() / juce::jmax (1, downscale)), h = juce::jmax (8, source.getHeight() / juce::jmax (1, downscale));
    juce::Image small = source.rescaled (w, h, juce::Graphics::mediumResamplingQuality).convertedToFormat (juce::Image::ARGB);
    {
        juce::Image::BitmapData bd (small, juce::Image::BitmapData::readWrite);
        std::vector<juce::uint8> line ((size_t) juce::jmax (w, h) * 4);
        const int r = juce::jmax (1, radius);
        auto run = [&] (juce::uint8* first, int count, int stride) {
            for (int c = 0; c < 4; ++c)
            {
                int sum = 0;
                for (int i = -r; i <= r; ++i) sum += first[(size_t) (juce::jlimit (0, count - 1, i) * stride + c)];
                for (int i = 0; i < count; ++i)
                {
                    line[(size_t) (i * 4 + c)] = (juce::uint8) (sum / (2 * r + 1));
                    sum += first[(size_t) (juce::jmin (count - 1, i + r + 1) * stride + c)] - first[(size_t) (juce::jmax (0, i - r) * stride + c)];
                }
            }
            for (int i = 0; i < count; ++i) for (int c = 0; c < 4; ++c) first[(size_t) (i * stride + c)] = line[(size_t) (i * 4 + c)];
        };
        for (int p = 0; p < passes; ++p)
        {
            for (int y = 0; y < h; ++y) run (bd.getLinePointer (y), w, bd.pixelStride);
            for (int x = 0; x < w; ++x) run (bd.getPixelPointer (x, 0), h, bd.lineStride);
        }
    }
    return small.rescaled (source.getWidth(), source.getHeight(), juce::Graphics::highResamplingQuality);
}

// ============================================================================================ menus, scroll bars, text boxes
namespace
{
class ChartLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    ChartLookAndFeel()
    {
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff10131c));
        setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::TextEditor::textColourId, juce::Colour (0xfff6eee2));
        setColour (juce::TextEditor::highlightColourId, juce::Colour (0x66ffc56b));
        setColour (juce::TextEditor::highlightedTextColourId, juce::Colour (0xfff6eee2));
        setColour (juce::CaretComponent::caretColourId, juce::Colour (0xffffc56b));
        setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::ScrollBar::thumbColourId, juce::Colour (0x55f6eee2));
        setColour (juce::TooltipWindow::textColourId, juce::Colour (0xfff6eee2));
    }
    juce::Font getPopupMenuFont() override { return sans (16.0f); }
    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        const auto& l = look();
        g.fillAll (l.deep.darker (0.72f));
        g.setColour (l.text (0.14f));
        g.drawRect (0, 0, w, h, 1);
    }
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int, int& idealWidth, int& idealHeight) override
    {
        if (isSeparator) { idealWidth = 60; idealHeight = 9; return; }
        idealHeight = 30;
        idealWidth = (int) juce::GlyphArrangement::getStringWidth (sans (16.0f), text) + 58;
    }
    void drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                            const juce::String& text, const juce::String&, const juce::Drawable*, const juce::Colour*) override
    {
        const auto& l = look();
        if (isSeparator) { g.setColour (l.text (0.12f)); g.fillRect (area.reduced (12, 0).withSizeKeepingCentre (area.getWidth() - 24, 1)); return; }
        auto r = area.toFloat();
        if (isHighlighted && isActive) { g.setColour (l.lantern.withAlpha (0.16f)); g.fillRoundedRectangle (r.reduced (4.0f, 1.0f), 6.0f); }
        if (isTicked) { g.setColour (l.lantern); g.fillEllipse (r.getX() + 13.0f, r.getCentreY() - 3.0f, 6.0f, 6.0f); }
        g.setColour (isTicked ? l.lantern : l.text (isActive ? 0.92f : 0.35f));
        g.setFont (sans (16.0f));
        g.drawText (text, r.withTrimmedLeft (28.0f).withTrimmedRight (18.0f), juce::Justification::centredLeft, true);
        if (hasSubMenu) chevron (g, r.removeFromRight (22.0f), 1, l.text (0.6f));
    }
    void drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& name) override
    {
        g.setColour (look().accent.withAlpha (0.85f));
        g.setFont (fellCaps (15.0f));
        g.drawText (name, area.toFloat().withTrimmedLeft (14.0f), juce::Justification::centredLeft, true);
    }
    void drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart, int thumbSize, bool over, bool down) override
    {
        if (thumbSize <= 0) return;
        const float alpha = down ? 0.5f : (over ? 0.38f : 0.22f);
        g.setColour (look().text (alpha));
        if (vertical) g.fillRoundedRectangle ((float) x + (float) w * 0.3f, (float) thumbStart + 2.0f, (float) w * 0.4f, (float) thumbSize - 4.0f, (float) w * 0.2f);
        else g.fillRoundedRectangle ((float) thumbStart + 2.0f, (float) y + (float) h * 0.3f, (float) thumbSize - 4.0f, (float) h * 0.4f, (float) h * 0.2f);
    }
    int getDefaultScrollbarWidth() override { return 12; }
    void fillTextEditorBackground (juce::Graphics&, int, int, juce::TextEditor&) override {}
    void drawTextEditorOutline (juce::Graphics&, int, int, juce::TextEditor&) override {}
    juce::Rectangle<int> getTooltipBounds (const juce::String& tip, juce::Point<int> pos, juce::Rectangle<int> parent) override
    {
        const int w = (int) juce::GlyphArrangement::getStringWidth (sans (14.0f), tip) + 22, h = 28;
        return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 18, pos.y > parent.getCentreY() ? pos.y - (h + 8) : pos.y + 20, w, h).constrainedWithin (parent);
    }
    void drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h) override
    {
        const auto& l = look();
        g.setColour (l.deep.darker (0.75f)); g.fillRoundedRectangle (0.5f, 0.5f, (float) w - 1.0f, (float) h - 1.0f, 6.0f);
        g.setColour (l.text (0.18f)); g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1.0f, (float) h - 1.0f, 6.0f, 1.0f);
        g.setColour (l.text (0.9f)); g.setFont (sans (14.0f));
        g.drawText (text, 0, 0, w, h, juce::Justification::centred, true);
    }
};
} // namespace

juce::LookAndFeel& chartLookAndFeel()
{
    if (kept == nullptr) return juce::LookAndFeel::getDefaultLookAndFeel();
    if (kept->menus == nullptr) kept->menus = std::make_unique<ChartLookAndFeel>();
    return *kept->menus;
}
} // namespace atlas::ui
