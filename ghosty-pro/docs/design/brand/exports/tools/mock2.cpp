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

static void knob (juce::Graphics& g, float cx, float cy, float rad, float v, juce::String label, juce::String val)
{
    Rectangle<float> k (cx - rad, cy - rad, rad * 2, rad * 2);
    shadow (g, k.reduced (3), rad, 8, 1.0f);
    const float a0 = juce::MathConstants<float>::pi * 1.25f, a1 = juce::MathConstants<float>::pi * 2.75f, a = a0 + (a1 - a0) * v;
    juce::Path tr; tr.addCentredArc (cx, cy, rad + 5, rad + 5, 0, a0, a1, true); g.setColour (Colour (0x33443A7A)); g.strokePath (tr, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Path fl; fl.addCentredArc (cx, cy, rad + 5, rad + 5, 0, a0, a, true);
    g.setGradientFill (juce::ColourGradient (Colour (0xff8B5CF6), cx - rad, cy, Colour (0xff22D3B6), cx + rad, cy, false)); g.strokePath (fl, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setGradientFill (juce::ColourGradient (Colour (0xffFFFFFF), cx - rad * 0.4f, cy - rad * 0.7f, Colour (0xffD5CBF2), cx + rad * 0.5f, cy + rad, false)); g.fillEllipse (k);
    g.setGradientFill (juce::ColourGradient (Colour (0x00FFFFFF), cx, cy, Colour (0x66B79BFF), cx, cy + rad, true)); g.fillEllipse (k);
    g.setColour (Colour (0xB0FFFFFF)); g.drawEllipse (k.reduced (0.8f), 1.4f);
    g.setColour (Colour (0x70FFFFFF)); g.fillEllipse (cx - rad * 0.55f, cy - rad * 0.8f, rad * 1.1f, rad * 0.6f);
    juce::Path ind; ind.addRoundedRectangle (-1.6f, -rad + 3, 3.2f, rad * 0.45f, 1.6f); g.setColour (Colour (0xff6D3AE8)); g.fillPath (ind, juce::AffineTransform::rotation (a + juce::MathConstants<float>::halfPi).translated (cx, cy));
    text (g, label, { cx - 40, cy - rad - 20, 80, 12 }, 9.5f, Colour (0xff4A4270));
    auto vb = Rectangle<float> (cx - 30, cy + rad + 7, 60, 17); glass (g, vb, 8, Colour (0xffFFFFFF), 0.30, true); text (g, val, vb, 10, Colour (0xff2A2250));
}
static void panel (juce::Graphics& g, Rectangle<float> r, juce::String title, juce::Colour c)
{ glass (g, r, 22, Colour (0xffFFFFFF), 0.30f); text (g, title, { r.getX() + 16, r.getY() + 6, 200, 22 }, 11.5f, c, true, juce::Justification::centredLeft); }
static void screen (juce::Graphics& g, Rectangle<float> r, int kind, juce::Colour c1, juce::Colour c2)
{
    glass (g, r, 14, Colour (0xff6A5CC8), 0.12f, true);
    juce::Path p; for (int i = 0; i <= 200; ++i) { const float t = i / 200.0f; float y = 0.5f;
        if (kind == 0) y = 0.5f - 0.4f * std::sin (t * 6.2832f * 2); else if (kind == 1) y = 0.5f - 0.4f * (2 * std::fmod (t * 2, 1.0f) - 1); else if (kind == 2) y = (t < 0.5f ? 0.9f - t * 1.5f : 0.15f + (t - 0.5f) * 1.0f) ; else y = 0.88f - 0.7f * std::exp (-std::pow ((t - 0.4f) * 5.0f, 2.0f)) - 0.2f * t;
        const float x = r.getX() + 10 + (r.getWidth() - 20) * t, yy = r.getY() + 8 + (r.getHeight() - 16) * y; if (i == 0) p.startNewSubPath (x, yy); else p.lineTo (x, yy); }
    g.setColour (c1.withAlpha (0.25f)); g.strokePath (p, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setGradientFill (juce::ColourGradient (c1, r.getX(), 0, c2, r.getRight(), 0, false)); g.strokePath (p, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    const int W = 1420, H = 940;
    juce::Image img (juce::Image::ARGB, W, H, true, juce::SoftwareImageType()); juce::Graphics g (img);
    g.setGradientFill (juce::ColourGradient (Colour (0xffEFE9FF), 0, 0, Colour (0xffE3F6F1), W, H, false)); g.fillAll();
    struct Orb { float x, y, r; juce::uint32 c; } orbs[] { { 120, 110, 380, 0xffC9B2FF }, { 1300, 140, 340, 0xff9FE8D6 }, { 900, 800, 420, 0xffFFC9E6 }, { 200, 860, 340, 0xffB6D4FF }, { 700, 420, 280, 0xffD9C8FF } };
    for (auto& o : orbs) { g.setGradientFill (juce::ColourGradient (Colour (o.c).withAlpha (0.85f), o.x, o.y, Colour (o.c).withAlpha (0.0f), o.x + o.r, o.y, true)); g.fillRect (0, 0, W, H); }
    const Colour violet (0xff6D3AE8), teal (0xff0E9C86), pink (0xffD6317F), ink (0xff2A2250);
    // header
    glass (g, { 12, 8, W - 24, 56 }, 28);
    text (g, "ghosty", { 30, 8, 120, 56 }, 28, ink, true, juce::Justification::centredLeft); text (g, "PRO", { 138, 22, 48, 26 }, 12, violet);
    btn (g, { 250, 18, 36, 36 }, "<"); glass (g, { 294, 16, 210, 40 }, 20, Colour (0xffFFFFFF), 0.3f, true); text (g, "Trap Stacks   TRAP STACKS & FX", { 302, 16, 200, 40 }, 11, ink);
    btn (g, { 512, 18, 36, 36 }, ">"); btn (g, { 560, 18, 70, 36 }, "SAVE"); btn (g, { 638, 18, 70, 36 }, "LOAD"); btn (g, { 716, 18, 70, 36 }, "INIT"); btn (g, { 796, 18, 100, 36 }, "SNAPSHOT", true);
    btn (g, { W - 400.0f, 18, 90, 36 }, "STUDIO"); btn (g, { W - 300.0f, 18, 100, 36 }, "GHOST", true); btn (g, { W - 192.0f, 18, 90, 36 }, "BYPASS");
    // tabs
    const char* tabs[] { "SYNTH", "MOD", "BEND", "TRAP", "MORPH", "FX", "BROWSE" };
    for (int i = 0; i < 7; ++i) btn (g, { 20.0f + 124 * i, 74, 116, 34 }, tabs[i], i == 0, 12);
    // OSC panels
    for (int o = 0; o < 2; ++o)
    {
        const float y0 = 116 + o * 276; panel (g, { 12, y0, 462, 266 }, o == 0 ? "OSC 1" : "OSC 2", violet);
        btn (g, { 340, y0 + 6, 56, 22 }, "MORE", false, 9.5f); btn (g, { 402, y0 + 6, 56, 22 }, "ON", true, 9.5f);
        screen (g, { 26, y0 + 32, 434, 76 }, o == 0 ? 1 : 3, o == 0 ? pink : teal, violet);
        btn (g, { 26, y0 + 114, 110, 26 }, "Saw", false, 11); btn (g, { 142, y0 + 114, 110, 26 }, "Square", false, 11); btn (g, { 262, y0 + 114, 90, 26 }, "DRAW A", true, 10.5f); btn (g, { 358, y0 + 114, 100, 26 }, "WAV", false, 10.5f);
        const char* kl[] { "POS", "LEVEL", "OCT", "SEMI", "FINE", "UNISON", "DETUNE", "SPREAD", "BLEND" }; const char* kv[] { "25%", "80%", "0", "0", "0 ct", "4", "15%", "60%", "75%" }; const float kn[] { .25f, .8f, .5f, .5f, .5f, .3f, .15f, .6f, .75f };
        for (int i = 0; i < 9; ++i) knob (g, 52.0f + 48.5f * i, y0 + 186, 15, kn[i], kl[i], kv[i]);
    }
    // filter
    panel (g, { 486, 116, 458, 224 }, "FILTER", teal); btn (g, { 760, 122, 56, 22 }, "MORE", false, 9.5f); btn (g, { 822, 122, 110, 22 }, "LP 24", false, 10);
    screen (g, { 500, 148, 430, 84 }, 3, teal, violet);
    const char* fl[] { "CUTOFF", "RES", "DRIVE", "ENV", "KEY", "VEL" }; const char* fv[] { "1.8 kHz", "58%", "0%", "40%", "0%", "25%" }; const float fk[] { .55f, .58f, .1f, .4f, .05f, .25f };
    for (int i = 0; i < 6; ++i) knob (g, 526.0f + 74 * i, 286, 17, fk[i], fl[i], fv[i]);
    // envelopes
    panel (g, { 486, 348, 458, 232 }, "ENVELOPES", Colour (0xff3D7A0E)); btn (g, { 506, 372, 70, 22 }, "AMP", true, 9.5f); btn (g, { 582, 372, 70, 22 }, "FILTER", false, 9.5f);
    screen (g, { 500, 400, 430, 90 }, 2, Colour (0xff3D9A0E), teal);
    const char* el[] { "ATTACK", "DECAY", "SUSTAIN", "RELEASE", "CURVE" }; const char* ev[] { "358 ms", "600 ms", "75%", "300 ms", "77%" };
    for (int i = 0; i < 5; ++i) knob (g, 540.0f + 88 * i, 536, 17, .2f + .15f * i, el[i], ev[i]);
    // lfos
    panel (g, { 486, 588, 458, 192 }, "LFO 1  /  LFO 2", Colour (0xffB07A00)); screen (g, { 500, 616, 210, 64 }, 0, Colour (0xff3D9A0E), teal); screen (g, { 720, 616, 210, 64 }, 0, Colour (0xffD9A300), pink);
    btn (g, { 500, 688, 90, 26 }, "SYNC", false, 10.5f); btn (g, { 720, 688, 90, 26 }, "RETRIG", false, 10.5f);
    for (int i = 0; i < 4; ++i) knob (g, 620.0f + 74 * i, 750, 14, .3f + .1f * i, i % 2 ? "FADE IN" : "RATE", i % 2 ? "0 ms" : "6.9 Hz");
    // right: ghost field + FX
    panel (g, { 956, 116, 452, 268 }, "GHOSTS", violet);
    g.setColour (Colour (0xfffFFFFF)); for (auto p : { juce::Point<float> { 1050, 250 }, { 1140, 300 }, { 1290, 220 } }) { g.setColour (Colour (0xffFFFFFF)); g.fillEllipse (p.x - 24, p.y - 28, 48, 56); g.setColour (violet.withAlpha (0.6f)); g.drawEllipse (p.x - 24, p.y - 28, 48, 56, 1.5f); g.setColour (teal); g.fillEllipse (p.x - 9, p.y - 8, 5, 5); g.fillEllipse (p.x + 4, p.y - 8, 5, 5); }
    panel (g, { 956, 392, 452, 190 }, "GHOST FX / OUTPUT", teal); for (int i = 0; i < 4; ++i) { knob (g, 1010.0f + 108 * i, 458, 15, .1f + .2f * i, i == 0 ? "DRIVE" : i == 1 ? "CRUSH" : i == 2 ? "CHORUS" : "PHASER", "7%"); }
    for (int i = 0; i < 4; ++i) knob (g, 1010.0f + 108 * i, 538, 13, .3f, i == 0 ? "ECHO" : i == 1 ? "REVERB" : i == 2 ? "HAUNT" : "VOLUME", "37%");
    panel (g, { 956, 590, 452, 190 }, "VOICE  /  MACROS", pink); for (int i = 0; i < 4; ++i) knob (g, 1010.0f + 108 * i, 660, 15, .3f + .15f * i, i == 0 ? "VEL" : i == 1 ? "GLIDE" : i == 2 ? "VOICES" : "BEND", "50%");
    for (int i = 0; i < 4; ++i) knob (g, 1010.0f + 108 * i, 740, 13, .1f * i, "MACRO " + juce::String (i + 1), "0%");
    // keys
    glass (g, { 12, 790, W - 24, 140 }, 24, Colour (0xffFFFFFF), 0.30f);
    btn (g, { 30, 802, 90, 28 }, "ARP", false, 11); btn (g, { 130, 802, 90, 28 }, "1/16", false, 11); btn (g, { 230, 802, 90, 28 }, "HOLD", false, 11); btn (g, { 330, 802, 90, 28 }, "FLOW", true, 11);
    for (int i = 0; i < 49; ++i) { const float kx = 30 + i * 27.4f; Rectangle<float> kr (kx, 840, 26, 80); g.setGradientFill (juce::ColourGradient (Colour (0xE0FFFFFF), kx, 840, Colour (0x80E6DFFA), kx, 920, false)); g.fillRoundedRectangle (kr, 5); g.setColour (Colour (0xC0FFFFFF)); g.drawRoundedRectangle (kr, 5, 1.2f); }
    for (int i = 0; i < 49; ++i) { const int m = i % 7; if (m == 2 || m == 6) continue; const float kx = 30 + i * 27.4f + 18; Rectangle<float> kr (kx, 840, 17, 48); g.setGradientFill (juce::ColourGradient (Colour (0xff3B3470), kx, 840, Colour (0xff6A5CC8), kx, 888, false)); g.fillRoundedRectangle (kr, 4); g.setColour (Colour (0x60FFFFFF)); g.drawRoundedRectangle (kr.reduced (1), 4, 1.0f); }
    juce::File f ("/root/work/logos/pro-ghost-look-mock.png"); f.deleteFile(); juce::PNGImageFormat png; png.writeImageToStream (img, *std::unique_ptr<juce::FileOutputStream> (f.createOutputStream()));
}
