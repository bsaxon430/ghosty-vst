#include <juce_gui_basics/juce_gui_basics.h>
using juce::Colour; using juce::Rectangle;
static void shadow (juce::Graphics& g, Rectangle<float> r, float rad, float spread, float a)
{ for (int i = 8; i >= 1; --i) { g.setColour (Colour (0xff3A2E6B).withAlpha (a * 0.035f)); g.fillRoundedRectangle (r.expanded (spread * i / 8.0f).translated (0, spread * 0.45f), rad + spread * i / 8.0f); } }
static void glass (juce::Graphics& g, Rectangle<float> r, float rad, juce::Colour tint = Colour (0xffFFFFFF), float body = 0.38f, bool pressed = false)
{
    shadow (g, r, rad, pressed ? 6.0f : 14.0f, 1.0f);
    g.setGradientFill (juce::ColourGradient (tint.withAlpha (body + 0.22f), r.getX(), r.getY(), tint.withAlpha (body - 0.10f), r.getX(), r.getBottom(), false)); g.fillRoundedRectangle (r, rad);
    // refraction-ish inner glow near the edges
    g.setGradientFill (juce::ColourGradient (Colour (0x00FFFFFF), r.getCentreX(), r.getCentreY(), Colour (0x55FFFFFF), r.getX(), r.getY(), true)); g.fillRoundedRectangle (r, rad);
    // specular rim: bright top-left, soft bottom-right, plus a colour fringe on the lower edge
    g.setGradientFill (juce::ColourGradient (Colour (0xE6FFFFFF), r.getX(), r.getY(), Colour (0x30FFFFFF), r.getRight(), r.getBottom(), false));
    g.drawRoundedRectangle (r.reduced (0.75f), rad, 1.5f);
    g.setColour (Colour (0x40A06BFF)); g.drawRoundedRectangle (r.reduced (2.2f).translated (0, 0.8f), rad - 2, 0.8f);
    g.setGradientFill (juce::ColourGradient (Colour (0x90FFFFFF), r.getX() + rad, r.getY() + 2, Colour (0x00FFFFFF), r.getX() + rad, r.getY() + r.getHeight() * 0.45f, false));
    juce::Path hl; hl.addRoundedRectangle (r.getX() + 3, r.getY() + 2.5f, r.getWidth() - 6, r.getHeight() * 0.42f, rad * 0.8f, rad * 0.8f, true, true, false, false); g.fillPath (hl);
}
static void text (juce::Graphics& g, juce::String t, Rectangle<float> r, float h, Colour c, bool bold = true, juce::Justification j = juce::Justification::centred)
{ g.setColour (c); g.setFont (juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain))); g.drawText (t, r, j); }
static void btn (juce::Graphics& g, Rectangle<float> r, juce::String t, bool on = false, float fs = 12.5f)
{
    if (on) { glass (g, r, r.getHeight() * 0.5f, Colour (0xff8B5CF6), 0.55f); text (g, t, r, fs, Colour (0xffFFFFFF)); }
    else { glass (g, r, r.getHeight() * 0.5f); text (g, t, r, fs, Colour (0xff2A2250)); }
}
static void slider (juce::Graphics& g, Rectangle<float> r, float v, juce::String label, juce::String val)
{
    text (g, label, { r.getX(), r.getY() - 2, 70, r.getHeight() }, 11.5f, Colour (0xff4A4270), true, juce::Justification::centredLeft);
    auto tr = Rectangle<float> (r.getX() + 72, r.getCentreY() - 3.5f, r.getWidth() - 160, 7);
    g.setColour (Colour (0x30443A7A)); g.fillRoundedRectangle (tr, 3.5f); g.setColour (Colour (0x55FFFFFF)); g.drawRoundedRectangle (tr, 3.5f, 1.0f);
    g.setGradientFill (juce::ColourGradient (Colour (0xff8B5CF6), tr.getX(), 0, Colour (0xff22D3B6), tr.getRight(), 0, false)); g.fillRoundedRectangle (tr.withWidth (tr.getWidth() * v), 3.5f);
    const float tx = tr.getX() + tr.getWidth() * v; const Rectangle<float> th (tx - 10, r.getCentreY() - 10, 20, 20);
    shadow (g, th, 10, 6, 1.0f);
    g.setGradientFill (juce::ColourGradient (Colour (0xffFFFFFF), tx - 4, th.getY() + 2, Colour (0xffDCD4F5), tx, th.getBottom(), false)); g.fillEllipse (th);
    g.setColour (Colour (0xA0FFFFFF)); g.drawEllipse (th.reduced (0.7f), 1.4f); g.setColour (Colour (0x60A06BFF)); g.drawEllipse (th.reduced (3.0f), 0.8f);
    auto vb = Rectangle<float> (r.getRight() - 78, r.getCentreY() - 12, 78, 24); glass (g, vb, 12, Colour (0xffFFFFFF), 0.30, true); text (g, val, vb, 12, Colour (0xff2A2250));
}
int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    const int W = 1280, H = 780;
    juce::Image img (juce::Image::ARGB, W, H, true, juce::SoftwareImageType());
    juce::Graphics g (img);
    // soft aurora background the glass sits on
    g.setGradientFill (juce::ColourGradient (Colour (0xffEFE9FF), 0, 0, Colour (0xffE3F6F1), W, H, false)); g.fillAll();
    struct Orb { float x, y, r; juce::uint32 c; } orbs[] { { 140, 90, 330, 0xffC9B2FF }, { 1120, 130, 300, 0xff9FE8D6 }, { 820, 640, 380, 0xffFFC9E6 }, { 260, 700, 300, 0xffB6D4FF }, { 640, 360, 240, 0xffD9C8FF } };
    for (auto& o : orbs) { g.setGradientFill (juce::ColourGradient (Colour (o.c).withAlpha (0.85f), o.x, o.y, Colour (o.c).withAlpha (0.0f), o.x + o.r, o.y, true)); g.fillRect (0, 0, W, H); }
    // header
    glass (g, { 14, 10, W - 28, 50 }, 25);
    text (g, "GHOST SLICE", { 70, 10, 160, 50 }, 17, Colour (0xff2A2250), true, juce::Justification::centredLeft);
    g.setColour (Colour (0xffFFFFFF)); g.fillEllipse (30, 20, 30, 30); g.setColour (Colour (0xff3CFF8F)); g.fillEllipse (40, 30, 5, 5); g.fillEllipse (48, 30, 5, 5);
    const char* tabs[] { "EDIT", "FLEX", "PITCH", "PLAY" }; for (int i = 0; i < 4; ++i) btn (g, { 240.0f + 74 * i, 18, 66, 34 }, tabs[i], i == 2);
    text (g, "120.00 BPM    KEY C MAJ    GRID 1/16", { 560, 10, 300, 50 }, 12.5f, Colour (0xff4A4270), true, juce::Justification::centredLeft);
    const char* r1[] { "PLAY", "LOOP", "LOAD", "PROJECT", "SYNC: DAW", "LIGHT", "LICENSED" }; float x = 880;
    for (int i = 0; i < 7; ++i) { const float w = i >= 3 ? 84.0f : 58.0f; btn (g, { x, 18, w, 34 }, r1[i], false, 11.0f); x += w + 6; if (x > W - 20) break; }
    // waveform panel
    Rectangle<float> wp { 14, 72, W - 28, 440 };
    glass (g, wp, 26, Colour (0xffFFFFFF), 0.30f);
    for (int i = 0; i < 12; ++i) if (i % 2) { g.setColour (Colour (0x14513FA8)); g.fillRect (wp.getX() + 24, wp.getY() + 24 + i * 32.0f, wp.getWidth() - 48, 32.0f); }
    for (int i = 0; i <= 13; ++i) { g.setColour (Colour (0x20513FA8)); g.fillRect (wp.getX() + 24 + i * 86.0f, wp.getY() + 24, 1.0f, wp.getHeight() - 48); }
    // waveform
    juce::Random rnd (4); juce::Path wf; const float cy = wp.getY() + 235;
    for (int px = 0; px < 1080; ++px) { const float t = px / 1080.0f; const float env = (px > 590 && px < 640) ? 0.0f : 52.0f * (0.75f + 0.25f * std::sin (t * 40)); const float a = env * (0.55f + 0.45f * rnd.nextFloat()); wf.addRectangle (wp.getX() + 40 + px, cy - a, 1.0f, a * 2.0f); }
    g.setGradientFill (juce::ColourGradient (Colour (0xcc7C5CE6), 0, cy - 55, Colour (0xcc22C7B0), 0, cy + 55, false)); g.fillPath (wf);
    // notes as glossy glass capsules
    struct N { float x, y, w; const char* n; bool sel; } notes[] { { 50, 380, 270, "C4", false }, { 330, 306, 270, "D4", true }, { 640, 190, 270, "F4", true }, { 920, 255, 270, "E4", false } };
    for (auto& n : notes) { Rectangle<float> r (wp.getX() + n.x, wp.getY() + n.y - 70, n.w, 28); glass (g, r, 14, n.sel ? Colour (0xff22D3B6) : Colour (0xff8B5CF6), 0.62f, true); text (g, n.n, r.withTrimmedLeft (10), 11, Colour (0xff1B1740), true, juce::Justification::centredLeft); }
    juce::Path curve; curve.startNewSubPath (wp.getX() + 920, wp.getY() + 190); curve.lineTo (wp.getX() + 1190, wp.getY() + 118); g.setColour (Colour (0xffFF4DA6)); g.strokePath (curve, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // arrange strip
    glass (g, { 14, 524, W - 28, 44 }, 22, Colour (0xffFFFFFF), 0.28f, true); text (g, "ARRANGE     1", { 14, 524, W - 28, 44 }, 12, Colour (0xff4A4270));
    // controls
    glass (g, { 14, 580, W - 28, 190 }, 26, Colour (0xffFFFFFF), 0.34f);
    const char* b1[] { "CUT", "SPLIT @ MARKERS", "DELETE", "DUPLICATE", "REVERSE", "UNDO", "REDO" }; x = 34;
    for (int i = 0; i < 7; ++i) { const float w = i == 1 ? 140.0f : 84.0f; btn (g, { x, 594, w, 34 }, b1[i], false, 11.5f); x += w + 8; }
    btn (g, { W - 250.0f, 594, 100, 34 }, "EXPORT WAV", false, 11.5f); btn (g, { W - 142.0f, 594, 110, 34 }, "EXPORT MIDI", false, 11.5f);
    slider (g, { 34, 640, 560, 28 }, 0.5f, "PITCH", "0 st"); slider (g, { 640, 640, 600, 28 }, 0.62f, "TONALITY", "8000 Hz");
    slider (g, { 34, 682, 560, 28 }, 0.38f, "VIBRATO", "100 %"); slider (g, { 640, 682, 600, 28 }, 0.24f, "TRANS", "40 ms");
    btn (g, { 34, 724, 200, 34 }, "DETECTED: C MAJ 40 %", false, 11.0f); btn (g, { 244, 724, 130, 34 }, "CHROMATIC", false, 11.0f); btn (g, { 384, 724, 130, 34 }, "MATCH KEY", false, 11.0f);
    btn (g, { W - 360.0f, 724, 80, 34 }, "SNAP", false); btn (g, { W - 270.0f, 724, 100, 34 }, "SNAP ALL", false); btn (g, { W - 160.0f, 724, 56, 34 }, "DRAW", true); btn (g, { W - 96.0f, 724, 72, 34 }, "F0 CURVE", true, 11.0f);
    juce::File f ("/root/work/logos/slice-liquid-light-mock.png"); f.deleteFile(); juce::PNGImageFormat png; png.writeImageToStream (img, *std::unique_ptr<juce::FileOutputStream> (f.createOutputStream()));
}
