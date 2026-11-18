#include "SliceEditor.h"

using namespace gslice;
using namespace gslice::ui;
namespace gg = ghosty::glass;
using R = juce::Rectangle<float>;

namespace
{
// ---------------------------------------------------------------- the design (the picture's own pixels, 1477 x 1065)
const R kHeader { 20, 20, 1438, 88 }, kLooksBox { 860, 30, 360, 66 };
const juce::Rectangle<int> kKeyBtn { 463, 35, 178, 53 }, kTempoBtn { 667, 35, 168, 53 }, kLooksR { 864, 30, 344, 66 }, kGear { 1245, 36, 54, 53 }, kUnlock { 1318, 36, 124, 53 };
const juce::Rectangle<int> kStage { 20, 114, 1437, 348 };
const R kTypePanel { 20, 477, 667, 172 }, kCharPanel { 702, 477, 394, 160 }, kMusicalPanel { 20, 662, 690, 83 };
const juce::Rectangle<int> kTypeTiles { 32, 505, 662, 143 }, kCharTiles { 712, 505, 384, 125 };
const juce::Rectangle<int> kGenerate { 1110, 487, 348, 152 }, kVary { 1140, 642, 295, 58 };
// build 6: three knobs under CHARACTER (TONE, the type's own knob, SPACE): TONE and SPACE moved out a little to make room
const juce::Rectangle<int> kMusical { 124, 674, 488, 28 }, kTone { 728, 642, 100, 100 }, kTypeKnob { 858, 648, 88, 88 }, kSpace { 990, 642, 100, 100 };
// the TAKE TYPE title row (like CLEAR's): < STYLE > | LENGTH | UNDER THE HOOD
const juce::Rectangle<int> kStylePrev { 168, 480, 30, 30 }, kMelodyPill { 196, 478, 172, 34 }, kStyleNext { 366, 480, 30, 30 }, kLengthPill { 400, 478, 144, 34 }, kHoodPill { 546, 478, 140, 34 };
const juce::Rectangle<int> kHood { 20, 477, 1076, 423 };     // UNDER THE HOOD: over TAKE TYPE .. TAKES (GENERATE / VARY / PLAY stay)
const R kTakesPanel { 20, 760, 880, 140 }, kTilesPanel { 902, 780, 556, 120 }, kKeysPanel { 20, 910, 1438, 123 };
const juce::Rectangle<int> kPrev { 30, 813, 36, 72 }, kStrip { 80, 790, 770, 107 }, kNext { 852, 813, 36, 72 };
const juce::Rectangle<int> kPlay { 913, 800, 102, 92 }, kDragWav { 1031, 800, 135, 92 }, kDragMidi { 1180, 800, 129, 92 }, kSave { 1323, 800, 123, 92 };
const juce::Rectangle<int> kKeys { 28, 922, 1194, 98 }, kVelocity { 1247, 945, 53, 73 }, kScale { 1322, 945, 124, 73 };
const juce::Rectangle<int> kToast { 440, 140, 640, 34 }, kKnobLabels { 700, 732, 540, 27 };
const juce::Rectangle<int> kClearPill { 997, 478, 88, 34 };    // CLEAR: the right end of the CHARACTER title row (centred on its caption's line; 7 px of room for the glow)
constexpr int kLowKey = 12, kHighKey = 88;   // C1 .. E7 in FL Studio's names (middle C = C5)

// text with its left edge at x, vertically centred on cy
void textAt (juce::Graphics& g, const juce::String& s, const juce::Font& f, float x, float cy, juce::Justification j = juce::Justification::centredLeft, float w = 600.0f)
{
    g.setFont (f);
    float x0 = x;
    if (j.testFlags (juce::Justification::horizontallyCentred)) x0 = x - w * 0.5f;
    else if (j.testFlags (juce::Justification::right)) x0 = x - w;
    g.drawText (s, R (x0, cy - f.getHeight() * 0.5f, w, f.getHeight()), j, false);
}
float widthOf (const juce::Font& f, const juce::String& s) { return ghosty::textWidth (f, s); }

// a soft glow: layered strokes around a rounded rectangle
void glow (juce::Graphics& g, R r, float rad, juce::Colour c, float spread, int layers = 6, float alpha = 0.08f)
{
    for (int i = layers; i >= 1; --i)
    {
        const float e = spread * (float) i / (float) layers;
        g.setColour (c.withAlpha (alpha)); g.drawRoundedRectangle (r.expanded (e), rad + e, spread / (float) layers + 1.0f);
    }
}

// a dark-glass panel of the window (the skin's colours)
void glassPanel (juce::Graphics& g, R r, float rad, const Skin& S, bool shadow = true)
{
    juce::Path p; p.addRoundedRectangle (r, rad);
    if (shadow)
    {
        if (S.glass) gg::shadow (g, r, rad, 12.0f, 0.8f, 6);
        else juce::DropShadow (juce::Colours::black.withAlpha (S.light ? 0.10f : 0.45f), 22, { 0, 8 }).drawForPath (g, p);
    }
    g.setGradientFill (juce::ColourGradient (S.panelTop, r.getX(), r.getY(), S.panelBot, r.getX(), r.getBottom(), false)); g.fillPath (p);
    // the top highlight
    { juce::ColourGradient hi (S.panelHi, r.getX(), r.getY(), S.panelHi.withAlpha (0.0f), r.getX(), r.getY() + juce::jmin (40.0f, r.getHeight() * 0.5f), false);
      g.setGradientFill (hi); g.fillPath (p); }
    g.setColour (S.panelLine); g.strokePath (p, juce::PathStrokeType (1.0f));
}

void tileFill (juce::Graphics& g, R r, float rad, const Skin& S, bool sel, bool over, bool enabled = true)
{
    if (sel)
    {
        glow (g, r, rad, S.a, 9.0f, 6, S.light ? 0.06f : 0.10f);
        g.setGradientFill (juce::ColourGradient (S.selTop, r.getX(), r.getY(), S.selBot, r.getX(), r.getBottom(), false)); g.fillRoundedRectangle (r, rad);
        { juce::ColourGradient hi (juce::Colours::white.withAlpha (S.light ? 0.4f : 0.14f), r.getX(), r.getY(), juce::Colours::white.withAlpha (0.0f), r.getX(), r.getY() + r.getHeight() * 0.5f, false);
          g.setGradientFill (hi); g.fillRoundedRectangle (r, rad); }
        g.setColour (S.selLine); g.drawRoundedRectangle (r.reduced (0.8f), rad, 1.6f);
        return;
    }
    g.setColour (S.tile.brighter (over && enabled ? (S.light ? 0.0f : 0.25f) : 0.0f)); g.fillRoundedRectangle (r, rad);
    if (over && enabled && S.light) { g.setColour (S.a.withAlpha (0.06f)); g.fillRoundedRectangle (r, rad); }
    { juce::ColourGradient hi (juce::Colours::white.withAlpha (S.light ? 0.5f : 0.06f), r.getX(), r.getY(), juce::Colours::white.withAlpha (0.0f), r.getX(), r.getY() + r.getHeight() * 0.45f, false);
      g.setGradientFill (hi); g.fillRoundedRectangle (r, rad); }
    g.setColour (over && enabled ? S.a2.withAlpha (0.7f) : S.tileLine); g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f);
}

juce::ColourGradient hGrad (const Skin& S, float x0, float x1, float y, float alpha = 1.0f)
{
    juce::ColourGradient gr (S.a.withAlpha (alpha), x0, y, S.b.withAlpha (alpha), x1, y, false);
    gr.addColour (0.45, S.a2.interpolatedWith (S.b2, 0.35f).withAlpha (alpha));
    gr.addColour (0.72, S.b2.withAlpha (alpha));
    return gr;
}

const char* kTypeTips[] {
    "AUTO: every GENERATE makes a different kind of take (instrument, pad, chops, texture, in turn).",
    "INSTRUMENT: the most singable part of your sound becomes one playable sound across the whole keyboard. Hold a key and it keeps going.",
    "PAD: lots of tiny pieces of your sound, tuned to chords in the key, frozen into long glassy tones. Wide and slowly moving.",
    "CHOPS: bigger pieces of your sound rearranged into a catchy phrase in the key and the tempo, with repeats, reverses and stutters.",
    "TEXTURE: clouds and drones: your sound frozen and spread out into an atmosphere." };
const char* kCharTips[] {
    "NATURAL: pitched, but the voice keeps its own size and colour.",
    "CHIPMUNK: the voice changes size with the pitch, the small, childlike sound (and it plays an octave up).",
    "DEEP: a bigger, darker throat (and it plays an octave down)." };
const char* kInputNames[] { "AUTO: the track's input, else the Mac's microphone", "TRACK: only what the DAW sends into Ghost Slice", "MAC MIC: the Mac's own microphone (works in FL Studio's channel rack)" };
juce::String keyName (int tonic, bool minor) { return analysis::pitchClassName (tonic) + (minor ? " MIN" : " MAJ"); }
juce::String secondsText (double s) { return juce::String (s, s < 10.0 ? 1 : 1) + "s"; }
}

// ---------------------------------------------------------------- icons
void gslice::ui::drawIcon (juce::Graphics& g, Icon ic, R r, const Skin& S, juce::Colour c, bool gradient)
{
    if (gradient) g.setGradientFill (juce::ColourGradient (S.a2, r.getX(), r.getY(), S.b, r.getRight(), r.getBottom(), false));
    else g.setColour (c);
    const float lw = juce::jmax (1.3f, r.getWidth() * 0.085f);
    switch (ic)
    {
        case Icon::Folder:   icons::folder (g, r); break;
        case Icon::Record:   { const auto d = icons::square (r).reduced (r.getWidth() * 0.12f);
                               for (int i = 3; i >= 1; --i) { g.setColour (S.bad.withAlpha (0.10f)); g.fillEllipse (d.expanded (2.5f * (float) i)); }
                               g.setGradientFill (juce::ColourGradient (S.bad.brighter (0.35f), d.getX(), d.getY(), S.bad, d.getRight(), d.getBottom(), false)); g.fillEllipse (d); break; }
        case Icon::Play:     icons::play (g, r); break;
        case Icon::Stop:     icons::stopSquare (g, r); break;
        case Icon::Scissors: icons::scissors (g, r, lw); break;
        case Icon::ZoomIn:   icons::magnifier (g, r, lw, 1); break;
        case Icon::ZoomOut:  icons::magnifier (g, r, lw, -1); break;
        case Icon::Fit:      icons::fit (g, r, lw); break;
        case Icon::Gear:     icons::gear (g, r, lw); break;
        case Icon::Dice:     icons::dice (g, r, c); break;
        case Icon::WaveBars: icons::waveBars (g, r); break;
        case Icon::Notes:    icons::notes (g, r, lw); break;
        case Icon::Download: icons::download (g, r, lw); break;
        case Icon::ChevL:    icons::chevron (g, r, -1, lw); break;
        case Icon::ChevR:    icons::chevron (g, r, 1, lw); break;
        case Icon::Lock:     icons::lock (g, r, true, lw); break;
        case Icon::Unlock:   icons::lock (g, r, false, lw); break;
        case Icon::Sparkle:  icons::sparkle (g, r); break;
        case Icon::Cloud:    icons::cloud (g, r, lw); break;
        case Icon::Chops:    icons::chopBars (g, r, lw); break;
        case Icon::Waves:    icons::waves (g, r, lw); break;
        case Icon::None:     break;
    }
}

// ---------------------------------------------------------------- the look-and-feel
void GenLnF::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&)
{
    const auto b = R ((float) x, (float) y, (float) w, (float) h);
    const auto c = b.getCentre(); const float Rr = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f - 1.0f;
    const float aNow = a0 + pos * (a1 - a0);
    // the dark ring
    { const auto ring = R (Rr * 2.0f, Rr * 2.0f).withCentre (c);
      if (! S.light) juce::DropShadow (juce::Colours::black.withAlpha (0.55f), 14, { 0, 5 }).drawForRectangle (g, ring.reduced (Rr * 0.3f).toNearestInt());
      g.setGradientFill (juce::ColourGradient (S.knobRing.brighter (0.15f), c.x, c.y - Rr, S.knobRing.darker (0.4f), c.x, c.y + Rr, false)); g.fillEllipse (ring);
      g.setColour (S.panelLine.withAlpha (0.5f)); g.drawEllipse (ring.reduced (0.5f), 1.0f); }
    const float ar = Rr - 7.5f;
    juce::Path track; track.addCentredArc (c.x, c.y, ar, ar, 0.0f, a0, a1, true);
    g.setColour (S.light ? juce::Colours::black.withAlpha (0.10f) : S.dim.withAlpha (0.45f)); g.strokePath (track, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Path val; val.addCentredArc (c.x, c.y, ar, ar, 0.0f, a0, aNow, true);
    juce::ColourGradient gr (S.a, c.x - Rr, c.y + Rr * 0.4f, S.b, c.x + Rr, c.y - Rr * 0.2f, false); gr.addColour (0.5, S.a2);
    g.setGradientFill (gr);
    g.setOpacity (0.25f); g.strokePath (val, juce::PathStrokeType (11.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setOpacity (1.0f); g.strokePath (val, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // the face
    const float fr = Rr - 14.0f; const auto face = R (fr * 2.0f, fr * 2.0f).withCentre (c);
    g.setGradientFill (juce::ColourGradient (S.knobFace, c.x - fr * 0.6f, c.y - fr * 0.8f, S.knobFace2, c.x + fr * 0.5f, c.y + fr, true)); g.fillEllipse (face);
    g.setColour ((S.light ? juce::Colours::black : juce::Colours::white).withAlpha (S.light ? 0.10f : 0.07f)); g.drawEllipse (face, 1.0f);
    const auto tip = c.getPointOnCircumference (fr * 0.86f, aNow), base = c.getPointOnCircumference (fr * 0.30f, aNow);
    g.setColour (S.light ? S.ink : juce::Colours::white); g.drawLine ({ base, tip }, 3.4f);
}

void GenLnF::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider& s)
{
    const float cy = (float) y + (float) h * 0.5f;
    const auto tr = R ((float) x, cy - 3.5f, (float) w, 7.0f);
    g.setGradientFill (hGrad (S, tr.getX(), tr.getRight(), cy, S.light ? 0.25f : 0.30f)); g.fillRoundedRectangle (tr, 3.5f);
    const auto fill = tr.withRight (juce::jlimit (tr.getX(), tr.getRight(), pos));
    juce::ColourGradient fg (S.a, tr.getX(), cy, S.a2.interpolatedWith (S.b2, 0.3f), tr.getRight(), cy, false);
    g.setGradientFill (fg);
    g.setOpacity (0.25f); g.fillRoundedRectangle (fill.expanded (0.0f, 3.0f), 6.0f);
    g.setOpacity (s.isEnabled() ? 1.0f : 0.4f); g.fillRoundedRectangle (fill, 3.5f);
    g.setOpacity (1.0f);
    // the thumb: a glowing cyan capsule
    const auto th = R (15.0f, 27.0f).withCentre ({ juce::jlimit ((float) x + 7.5f, (float) (x + w) - 7.5f, pos), cy });
    glow (g, th, 6.0f, S.b, 9.0f, 5, 0.10f);
    g.setGradientFill (juce::ColourGradient (S.b.brighter (0.6f), th.getX(), th.getY(), S.b.darker (S.light ? 0.2f : 0.05f), th.getX(), th.getBottom(), false)); g.fillRoundedRectangle (th, 6.0f);
    g.setColour (juce::Colours::white.withAlpha (0.85f)); g.drawRoundedRectangle (th.reduced (0.6f), 6.0f, 1.2f);
}

void GenLnF::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& c, bool over, bool down)
{
    if (dynamic_cast<ghosty::UnlockButton*> (&b) == nullptr) { LnF::drawButtonBackground (g, b, c, over, down); return; }
    const auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool lic = b.getButtonText() == "LICENSED";
    if (! lic) glow (g, r, 14.0f, S.star, 6.0f, 4, 0.08f);
    g.setGradientFill (juce::ColourGradient (S.tile.brighter (over ? 0.2f : 0.0f), r.getX(), r.getY(), S.tile.darker (0.2f), r.getX(), r.getBottom(), false)); g.fillRoundedRectangle (r, 14.0f);
    g.setColour (lic ? S.panelLine.brighter (0.2f) : S.star.withAlpha (0.7f)); g.drawRoundedRectangle (r.reduced (0.5f), 14.0f, 1.2f);
}

void GenLnF::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool over, bool down)
{
    if (dynamic_cast<ghosty::UnlockButton*> (&b) == nullptr) { LnF::drawButtonText (g, b, over, down); return; }
    const auto r = b.getLocalBounds().toFloat();
    const bool lic = b.getButtonText() == "LICENSED", checking = ! b.isEnabled();
    const auto dot = checking ? S.dim : lic ? S.good : S.star;
    const auto f = capFont (10.0f, Weight::SemiBold, 0.06f);
    const float tw = widthOf (f, b.getButtonText()), total = 8.0f + 12.0f + tw;
    const float x0 = r.getCentreX() - total * 0.5f;
    for (int i = 3; i >= 1; --i) { g.setColour (dot.withAlpha (0.12f)); g.fillEllipse (R (8.0f + 3.0f * (float) i, 8.0f + 3.0f * (float) i).withCentre ({ x0 + 4.0f, r.getCentreY() })); }
    g.setColour (dot); g.fillEllipse (R (8.0f, 8.0f).withCentre ({ x0 + 4.0f, r.getCentreY() }));
    g.setColour (S.ink); textAt (g, b.getButtonText(), f, x0 + 20.0f, r.getCentreY(), juce::Justification::centredLeft, tw + 10.0f);
}

void GenLnF::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (P.card);
    g.setColour (S.a2.withAlpha (0.45f)); g.drawRect (0, 0, w, h, 1);
}

void GenLnF::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    const auto r = R (0, 0, (float) w, (float) h);
    g.fillAll (P.card);
    g.setColour (S.a2.withAlpha (0.55f)); g.drawRect (r, 1.0f);
    juce::AttributedString s; s.setJustification (juce::Justification::centredLeft);
    s.append (text, capFont (9.0f, Weight::Medium), P.ink);
    juce::TextLayout tl; tl.createLayoutWithBalancedLineLengths (s, 360.0f);
    tl.draw (g, r.reduced (10.0f, 6.0f));
}

juce::Rectangle<int> GenLnF::getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent)
{
    juce::AttributedString s; s.append (text, capFont (9.0f, Weight::Medium), P.ink);
    juce::TextLayout tl; tl.createLayoutWithBalancedLineLengths (s, 360.0f);
    const int w = (int) std::ceil (tl.getWidth()) + 22, h = (int) std::ceil (tl.getHeight()) + 14;
    return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 12, pos.y > parent.getCentreY() ? pos.y - (h + 6) : pos.y + 6, w, h).constrainedWithin (parent);
}

// ---------------------------------------------------------------- GButton
void GButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    if (S == nullptr) return;
    const auto& K = *S;
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool en = isEnabled();
    if (! en) g.beginTransparencyLayer (0.4f);
    if (style == Style::Pill)
    {   // KEY  A MIN ⌄
        const float rad = 12.0f;
        g.setGradientFill (juce::ColourGradient (K.tile.brighter (over ? 0.25f : 0.05f), r.getX(), r.getY(), K.tile.darker (0.25f), r.getX(), r.getBottom(), false)); g.fillRoundedRectangle (r, rad);
        g.setColour (over ? K.a2.withAlpha (0.8f) : K.panelLine.brighter (0.15f)); g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.2f);
        const auto lf = capFont (10.5f, Weight::Medium, 0.08f), vf = capFont (11.0f, Weight::SemiBold, 0.05f);
        g.setColour (K.dim); textAt (g, label, lf, r.getX() + 17.0f, r.getCentreY());
        const float vx = r.getX() + (label == "KEY" ? 76.0f : 89.0f), vw = r.getRight() - 34.0f - vx;
        g.setColour (K.ink); g.setFont (vf);
        g.drawFittedText (value, R (vx, r.getY(), vw, r.getHeight()).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
        g.setColour (K.dim); icons::chevron (g, R (r.getRight() - 30.0f, r.getCentreY() - 3.5f, 11.0f, 7.0f), 0, 1.6f);
        if (! en) g.endTransparencyLayer();
        return;
    }
    if (style == Style::Icon)
    {
        if (lit || over) { g.setColour ((K.light ? K.a : juce::Colours::white).withAlpha (lit ? 0.16f : 0.07f)); g.fillRoundedRectangle (r, 8.0f); }
        if (lit) { g.setColour (K.a2.withAlpha (0.7f)); g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f); }
        const auto ir = R (iconSize, iconSize).withCentre (r.getCentre());
        drawIcon (g, icon, ir, K, lit ? K.a2 : (over ? K.ink : K.ink.interpolatedWith (K.dim, 0.35f)), false);
        if (! en) g.endTransparencyLayer();
        return;
    }
    // a glass button
    const float rad = juce::jmin (12.0f, r.getHeight() * 0.28f);
    if (lit) glow (g, r, rad, red ? K.bad : K.a, 8.0f, 5, 0.10f);
    g.setGradientFill (juce::ColourGradient (K.tile.brighter (over && en ? 0.25f : 0.08f), r.getX(), r.getY(), K.tile.darker (0.3f), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, rad);
    if (lit) { g.setColour ((red ? K.bad : K.a).withAlpha (0.18f)); g.fillRoundedRectangle (r, rad); }
    { juce::ColourGradient hi (juce::Colours::white.withAlpha (K.light ? 0.5f : 0.07f), r.getX(), r.getY(), juce::Colours::white.withAlpha (0.0f), r.getX(), r.getCentreY(), false);
      g.setGradientFill (hi); g.fillRoundedRectangle (r, rad); }
    if (down) { g.setColour (juce::Colours::black.withAlpha (0.12f)); g.fillRoundedRectangle (r, rad); }
    g.setColour (lit ? (red ? K.bad : K.selLine) : (over && en ? K.a2.withAlpha (0.75f) : K.panelLine.brighter (0.15f)));
    g.drawRoundedRectangle (r.reduced (0.5f), rad, lit ? 1.5f : 1.1f);
    const auto text = getButtonText();
    const auto f = capFont (textCap, Weight::SemiBold, 0.07f);
    const float tw = text.isEmpty() ? 0.0f : widthOf (f, text), gap = (icon != Icon::None && text.isNotEmpty()) ? iconSize * 0.75f : 0.0f;
    const float iw = icon != Icon::None ? iconSize : 0.0f, total = iw + gap + tw;
    float x = r.getCentreX() - total * 0.5f;
    if (icon != Icon::None)
    {
        const auto ir = R (x, r.getCentreY() - iconSize * 0.5f, iconSize, iconSize);
        juce::Colour ic = K.ink; bool gr = false;
        if (icon == Icon::Folder) ic = K.light ? K.a : juce::Colour (0xffc4b5fd);
        else if (icon == Icon::Dice) ic = K.a2.interpolatedWith (K.a, 0.3f);
        else if (icon == Icon::Play || icon == Icon::Stop) ic = K.light ? K.ink : juce::Colours::white;
        else if (icon == Icon::ChevL || icon == Icon::ChevR) ic = K.ink.interpolatedWith (K.dim, 0.2f);
        drawIcon (g, icon, ir, K, ic, gr);
        x += iw + gap;
    }
    if (text.isNotEmpty()) { g.setColour (K.ink); textAt (g, text, f, x, r.getCentreY() + 0.5f, juce::Justification::centredLeft, tw + 12.0f); }
    if (! en) g.endTransparencyLayer();
}

// ---------------------------------------------------------------- CLEAR

// ---------------------------------------------------------------- build 6: the title-row pills and UNDER THE HOOD
void TitlePill::paintButton (juce::Graphics& g, bool over, bool down)
{
    if (S == nullptr) return;
    const auto& K = *S;
    const auto r = getLocalBounds().toFloat().reduced (7.0f);
    const float rad = r.getHeight() * 0.5f;
    g.setColour (K.tile.withMultipliedAlpha (0.8f)); g.fillRoundedRectangle (r, rad);
    if (over) { g.setColour (K.a.withAlpha (0.10f)); g.fillRoundedRectangle (r, rad); }
    g.setColour (over ? K.a2.withAlpha (0.8f) : K.panelLine.brighter (0.15f)); g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f);
    if (down) { g.setColour (juce::Colours::black.withAlpha (0.10f)); g.fillRoundedRectangle (r, rad); }
    const auto fl = capFont (7.6f, Weight::Medium, 0.10f), fv = capFont (8.6f, Weight::SemiBold, 0.10f);
    float x = r.getX() + 11.0f;
    const float right = r.getRight() - (chevron || menu ? 20.0f : 10.0f);
    if (label.isNotEmpty()) { g.setColour (K.dim); textAt (g, label, fl, x, r.getCentreY()); x += widthOf (fl, label) + 7.0f; }
    g.setColour (K.light ? K.ink : K.ink.interpolatedWith (K.a2, 0.15f)); textAt (g, value, fv, x, r.getCentreY(), juce::Justification::centredLeft, juce::jmax (10.0f, right - x));
    juce::Path ch; const float cx = r.getRight() - 13.0f, cy = r.getCentreY();
    if (menu) { ch.startNewSubPath (cx - 3.5f, cy - 1.8f); ch.lineTo (cx, cy + 1.8f); ch.lineTo (cx + 3.5f, cy - 1.8f); }
    else if (chevron) { ch.startNewSubPath (cx - 1.8f, cy - 3.5f); ch.lineTo (cx + 1.8f, cy); ch.lineTo (cx - 1.8f, cy + 3.5f); }
    g.setColour (K.b.withAlpha (0.9f)); g.strokePath (ch, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

namespace
{
// the notes a take plays (build 6 keeps them; an older take: read back from its MIDI)
std::vector<gen::NoteEvent> notesOfTake (const gen::Take& t)
{
    if (! t.notes.empty()) return t.notes;
    std::vector<gen::NoteEvent> out;
    const double tpq = t.midi.getTimeFormat() > 0 ? (double) t.midi.getTimeFormat() : 960.0;
    for (int tr = 0; tr < t.midi.getNumTracks(); ++tr)
        if (auto* seq = t.midi.getTrack (tr))
            for (int i = 0; i < seq->getNumEvents(); ++i)
            {
                auto* e = seq->getEventPointer (i);
                if (! e->message.isNoteOn()) continue;
                gen::NoteEvent n; n.note = e->message.getNoteNumber(); n.beat = e->message.getTimeStamp() / tpq; n.velocity = e->message.getFloatVelocity();
                n.length = e->noteOffObject != nullptr ? juce::jmax (0.05, (e->noteOffObject->message.getTimeStamp() - e->message.getTimeStamp()) / tpq) : 0.5;
                out.push_back (n);
            }
    return out;
}
juce::StringArray chainOf (const gen::Take& t)
{
    if (! t.chain.isEmpty()) return t.chain;
    switch (t.type)
    {   // (a take from before build 6)
        case gen::Type::Pad: return { "VOWEL", "PSOLA TONE", "FREEZE", "GRAINS", "SPACE" };
        case gen::Type::Chops: return { "CHOPS", "PITCH (STRETCH)", "PHRASE", "SPACE" };
        case gen::Type::Texture: return { "VOWELS", "FREEZE", "DRONE", "GRAINS", "SPACE" };
        default: return { "HELD VOWEL", "PSOLA", "PITCH", "LOOP", "SPACE" };
    }
}
juce::String chainTip (const juce::String& step)
{
    static const std::pair<const char*, const char*> tips[] {
        { "VOICE PARTS", "The parts of your sound it uses (the boxes on the waveform above)." },
        { "HELD VOWEL", "The steadiest vowel of your sound, the one the instrument holds." },
        { "VOWEL", "The steadiest vowel of your sound." }, { "VOWELS", "The best vowels of your sound, joined." },
        { "RESYNTH (WORLD)", "Your voice measured every 5 ms (pitch, the shape of its sound, how breathy it is) and played back as clean harmonics + air: no grain buzz." },
        { "PSOLA", "The older engine: little grains of the voice, one per pitch period." }, { "PSOLA TONE", "The older engine: little grains of the voice, one per pitch period." },
        { "PITCH", "Each note's pitch (the melody's), with a gentle vibrato and glides when you ask for them." },
        { "PITCH (STRETCH)", "Each chop moved to its note with the formants kept (Signalsmith Stretch)." },
        { "FORMANT", "The size of the throat: kept where your voice put it at any pitch (or moved by FORMANT / CHARACTER)." },
        { "MORPH", "VOICE: from your words, syllable by syllable, to the pure held instrument tone." },
        { "WORDS", "VOICE all the way left: it sings your actual syllables in order." },
        { "LOOP", "The held tone loops seamlessly while a key is down." },
        { "CHOIR", "Your vowel as a few gently detuned voices, slowly drifting through its own colours." },
        { "SHIMMER", "The voice an octave up, softly, above the pad." },
        { "FREEZE", "A spectral freeze: the sound held still like glass (long windows, fresh phases: no ringing)." },
        { "FREEZE (PHASE-LOCKED)", "The sound held still by a phase-locked vocoder: clear and glassy; DREAM adds the dreamy random-phase smear toward the top." },
        { "GRAINS", "Little pieces of your sound sprinkled over the top (granular synthesis)." },
        { "DRONE", "The frozen vowels as a long, slowly moving drone." }, { "SUB", "A soft octave below." },
        { "CHOPS", "Short pieces of your sound: syllables and words." }, { "PHRASE", "The rhythm and notes the chops play (the melody style)." },
        { "STUTTER", "Ratchets, repeats and reversed chops (the STUTTER knob)." },
        { "SPACE", "The FX: TONE, the chorus, the Ghost Verb reverb (SPACE, more with DREAM) and the width." } };
    for (auto& t : tips) if (step == t.first) return t.second;
    return step;
}
} // namespace

HoodPage::HoodPage (SliceProcessor& p) : proc (p)
{
    addAndMakeVisible (backBtn); backBtn.setTooltip ("BACK: the main page again.");
    backBtn.textCap = 10.0f; backBtn.iconSize = 12.0f;
    backBtn.onClick = [this] { if (onBack) onBack(); };
    addAndMakeVisible (styleBtn); styleBtn.label = "STYLE"; styleBtn.menu = true;
    styleBtn.onClick = [this] { if (onStyleMenu) onStyleMenu(); };
    for (int i = 0; i < kDeepShown; ++i)
    {
        auto& k = deep[i];
        addAndMakeVisible (k);
        k.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag); k.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        k.setRange (0.0, 1.0, 0.01); k.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true); k.setMouseDragSensitivity (180);
        k.onValueChange = [this, i] { if (! refreshing) proc.setDeep (i, deep[i].getValue(), ! deep[i].isMouseButtonDown()); repaint(); };
        k.onDragEnd = [this, i] { proc.setDeep (i, deep[i].getValue(), true); };
    }
    // build 8: the editable roll + its tools, SHAPE IT | PLAY IT
    addAndMakeVisible (roll); addAndMakeVisible (gridSeg); addAndMakeVisible (histSeg); addAndMakeVisible (tabs); addChildComponent (playPanel);
    gridSeg.setTooltip ("GRID: where notes land in time when you move, add or resize them (1/16 = a sixteenth note).");
    gridSeg.onChange = [this] (int g) { proc.setPlay ([g] (gen::NoteEdit& e) { e.grid = g; }); roll.repaint(); };
    histSeg.momentary = true; histSeg.setTooltip ("UNDO / REDO your edits (Cmd+Z, Cmd+Shift+Z). RESET NOTES: your notes gone, the take's own melody back.");
    histSeg.onChange = [this] (int i) { if (i == 0) proc.undoEdit(); else if (i == 1) proc.redoEdit(); else roll.resetNotes(); };
    tabs.setTooltip ("SHAPE IT: this type's deeper knobs. PLAY IT: MASTER, CHORDS, the ARP and the lines between notes (ArpEZator's controls).");
    tabs.onChange = [this] (int t) { setTab (t); };
    roll.onSelectionChanged = [this] { playPanel.refresh(); };
}

void HoodPage::setTab (int t)
{
    tab = juce::jlimit (0, 1, t); tabs.sel = tab;
    refresh();
}
void HoodPage::setSkin (const Skin* s)
{
    S = s; backBtn.S = s; styleBtn.S = s; roll.S = s; gridSeg.S = s; histSeg.S = s; tabs.S = s; playPanel.setSkin (s);
}
juce::Rectangle<float> HoodPage::toolsArea() const { return { 16.0f, 103.0f, 600.0f, 22.0f }; }

juce::Rectangle<float> HoodPage::chainArea() const { return { 16.0f, 52.0f, (float) getWidth() - 32.0f, 44.0f }; }
juce::Rectangle<float> HoodPage::rollArea() const { return { 16.0f, 131.0f, 600.0f, (float) getHeight() - 145.0f }; }
juce::Rectangle<float> HoodPage::meterArea() const { return { 630.0f, 131.0f, 62.0f, (float) getHeight() - 145.0f }; }
juce::Rectangle<float> HoodPage::partsArea() const { return { 708.0f, 112.0f, (float) getWidth() - 724.0f, 70.0f }; }
juce::Rectangle<float> HoodPage::partChip (int i) const
{
    const auto a = partsArea(); const float w = (a.getWidth() - 7.0f * 6.0f) / 8.0f;
    return { a.getX() + (float) i * (w + 6.0f), a.getY() + 24.0f, w, 36.0f };
}

void HoodPage::resized()
{
    backBtn.setBounds (getWidth() - 132, 6, 120, 40);
    styleBtn.setBounds (getWidth() - 340, 8, 200, 36);
    const auto a = partsArea();
    const float kw = (a.getWidth()) / 4.0f;
    // two rows of five (the type's knobs; the unused ones hidden)
    const float kw5 = a.getWidth() / 5.0f;
    for (int i = 0; i < kDeepShown; ++i) deep[i].setBounds (juce::Rectangle<float> (a.getX() + kw5 * (float) (i % 5) + (kw5 - 52.0f) * 0.5f, 234.0f + (float) (i / 5) * 86.0f, 52.0f, 52.0f).toNearestInt());
    juce::ignoreUnused (kw);
    // build 8
    roll.setBounds (rollArea().toNearestInt());
    const auto ta = toolsArea();
    gridSeg.setBounds (R (ta.getX() + 92.0f, ta.getY(), 176.0f, ta.getHeight()).toNearestInt());
    histSeg.setBounds (R (ta.getRight() - 236.0f, ta.getY(), 236.0f, ta.getHeight()).toNearestInt());
    tabs.setBounds (R (a.getX(), 192.0f, 210.0f, 24.0f).toNearestInt());
    playPanel.setBounds (R (a.getX(), 222.0f, a.getWidth(), (float) getHeight() - 232.0f).toNearestInt());
}

void HoodPage::refresh()
{
    refreshing = true;
    const auto ty = proc.knobType();
    tabs.sel = tab; gridSeg.sel = proc.takeEdit().grid; playPanel.setVisible (tab == 1); playPanel.refresh(); roll.sync();
    for (int i = 0; i < kDeepShown; ++i)
    {
        deep[i].setVisible (tab == 0 && i < gen::Recipe::deepCount (ty));
        deep[i].setValue (proc.deepValue (i), juce::dontSendNotification);
        deep[i].setTooltip (gen::Recipe::deepTip (ty, i));
        gen::Recipe r; r.clear = proc.clear; deep[i].setDoubleClickReturnValue (true, gen::Recipe::deepDefault (ty, i, proc.clear));
    }
    auto t = proc.currentTake();
    styleBtn.value = proc.style == gen::Style::Auto && t != nullptr && t->style != gen::Style::Auto ? juce::String ("AUTO: ") + gen::styleName (t->style) : juce::String (gen::styleName (proc.style));
    styleBtn.setTooltip (juce::String ("MELODY: the style of the melody, chords or phrase. ") + gen::styleTip (proc.style == gen::Style::Auto && t != nullptr ? t->style : proc.style));
    refreshing = false;
    repaint();
}

void HoodPage::tick()
{
    const float l = proc.takeLevel (0), r = proc.takeLevel (1);
    meterL = juce::jmax (l, meterL * 0.86f); meterR = juce::jmax (r, meterR * 0.86f);
    flow += 1.0f / 30.0f;
    roll.sync();
    { const int g = proc.takeEdit().grid; if (gridSeg.sel != g) { gridSeg.sel = g; gridSeg.repaint(); } }
    repaint();
}

juce::String HoodPage::getTooltip()
{
    const auto m = getMouseXYRelative().toFloat();
    if (hoverPart >= 0) return "Part " + juce::String (hoverPart + 1) + " of your sound (the box with the same number on the waveform). Click: hear it.";
    for (size_t i = 0; i < chainBoxes.size(); ++i)
        if (chainBoxes[i].contains (m)) if (auto t = proc.currentTake()) { const auto ch = chainOf (*t); if ((int) i < ch.size()) return ch[(int) i] + ": " + chainTip (ch[(int) i]); }

    if (meterArea().contains (m)) return "LEVEL: what the take sounds like right now, left and right (after TONE, the chorus and SPACE).";
    if (chainArea().contains (m)) return "What happens to your sound, step by step, from left to right.";
    return "UNDER THE HOOD: what this take type is doing with your sound. BACK returns to the main page.";
}

void HoodPage::mouseMove (const juce::MouseEvent& e)
{
    int h = -1; const int n = (int) proc.takeParts().size();
    for (int i = 0; i < juce::jmin (8, n); ++i) if (partChip (i).contains (e.position)) h = i;
    if (h != hoverPart) { hoverPart = h; repaint(); }
}

void HoodPage::mouseDown (const juce::MouseEvent& e)
{
    const int n = (int) proc.takeParts().size();
    for (int i = 0; i < juce::jmin (8, n); ++i) if (partChip (i).contains (e.position)) { if (onHearPart) onHearPart (i); return; }
}

void HoodPage::paint (juce::Graphics& g)
{
    if (S == nullptr) return;
    const auto& K = *S;
    const auto all = getLocalBounds().toFloat();
    // an opaque bed (the main page's panels must not show through), then the glass
    g.setColour (K.bg); g.fillRoundedRectangle (all, 16.0f);
    glassPanel (g, all, 16.0f, K, false);
    { juce::ColourGradient bd (K.a2.withAlpha (0.7f), 0.0f, 0.0f, K.b.withAlpha (0.6f), all.getRight(), all.getBottom(), false); g.setGradientFill (bd); g.drawRoundedRectangle (all.reduced (0.8f), 16.0f, 1.4f); }
    auto t = proc.currentTake();
    const auto ty = t != nullptr ? t->type : proc.knobType();
    // the title: UNDER THE HOOD · INSTRUMENT · Take 3b
    const auto title = capFont (12.0f, Weight::SemiBold, 0.10f), small = capFont (8.2f, Weight::Medium, 0.06f);
    g.setColour (K.ink); textAt (g, "UNDER THE HOOD", title, 18.0f, 27.0f);
    float x = 18.0f + widthOf (title, "UNDER THE HOOD") + 14.0f;
    juce::String sub = gen::typeName (ty);
    if (t != nullptr) { sub << "  \xc2\xb7  " << t->recipe.name.upToFirstOccurrenceOf (" - ", false, false); if (t->form.isNotEmpty()) sub << "  \xc2\xb7  " << t->form; }
    g.setColour (K.light ? K.a : K.a2); textAt (g, juce::String (juce::CharPointer_UTF8 (sub.toRawUTF8())), capFont (9.0f, Weight::SemiBold, 0.08f), x, 27.0f, juce::Justification::centredLeft, (float) getWidth() - 360.0f - x);
    if (t == nullptr)
    {
        g.setColour (K.dim); textAt (g, "Press GENERATE: this page shows what the take is made of, step by step.", capFont (10.0f, Weight::Medium), all.getCentreX(), all.getCentreY(), juce::Justification::centred, 800.0f);
        return;
    }
    // ---- the chain, as a flow (a soft light runs along it while the take plays)
    chainBoxes.clear();
    {
        const auto ch = chainOf (*t); const int n = ch.size(); const auto a = chainArea();
        const float gap = 22.0f, w = (a.getWidth() - gap * (float) (n - 1)) / (float) juce::jmax (1, n);
        const bool playing = proc.previewPlaying() || proc.activeVoices() > 0;
        const float lit = playing ? std::fmod (flow * 1.6f, (float) n + 1.0f) : -10.0f;
        for (int i = 0; i < n; ++i)
        {
            const auto b = R (a.getX() + (float) i * (w + gap), a.getY(), w, a.getHeight());
            chainBoxes.push_back (b);
            const float on = juce::jlimit (0.0f, 1.0f, 1.0f - std::abs (lit - (float) i - 0.5f));
            const auto c = K.at ((float) i / (float) juce::jmax (1, n - 1));
            if (on > 0.01f) glow (g, b, 12.0f, c, 8.0f, 4, 0.10f * on);
            g.setGradientFill (juce::ColourGradient (c.withAlpha (K.light ? 0.16f : 0.20f + 0.15f * on), b.getX(), b.getY(), c.withAlpha (0.05f), b.getX(), b.getBottom(), false)); g.fillRoundedRectangle (b, 12.0f);
            g.setColour (c.withAlpha (0.55f + 0.4f * on)); g.drawRoundedRectangle (b.reduced (0.5f), 12.0f, 1.2f);
            g.setColour (K.ink); g.drawFittedText (ch[i], b.reduced (6.0f, 2.0f).toNearestInt(), juce::Justification::centred, 2, 0.7f);
            if (i + 1 < n)
            {   // the arrow
                const float ax = b.getRight() + 4.0f, ay = b.getCentreY();
                juce::Path p; p.startNewSubPath (ax, ay); p.lineTo (ax + gap - 8.0f, ay);
                p.startNewSubPath (ax + gap - 13.0f, ay - 4.5f); p.lineTo (ax + gap - 8.0f, ay); p.lineTo (ax + gap - 13.0f, ay + 4.5f);
                g.setColour (K.dim.withAlpha (0.8f)); g.strokePath (p, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }
        g.setFont (capFont (9.0f, Weight::SemiBold, 0.06f));
    }
    // ---- the roll is its own component (NoteRoll, build 8: editable); its title + tools row
    {
        const auto ta = toolsArea();
        g.setColour (K.dim); textAt (g, t->type == gen::Type::Chops ? "PHRASE" : t->type == gen::Type::Pad || t->type == gen::Type::Texture ? "CHORDS" : "MELODY", capFont (7.8f, Weight::SemiBold, 0.08f), ta.getX() + 2.0f, ta.getCentreY());
        g.setColour (K.dim.withAlpha (0.8f)); textAt (g, "GRID", capFont (7.0f, Weight::Medium, 0.08f), ta.getX() + 60.0f, ta.getCentreY());
        const juce::String hint = roll.selectedCount() > 0 ? juce::String (roll.selectedCount()) + " SELECTED" : "";
        g.setColour (K.light ? K.a : K.a2); textAt (g, hint, capFont (7.0f, Weight::Medium, 0.06f), ta.getX() + 278.0f, ta.getCentreY(), juce::Justification::centredLeft, ta.getRight() - 240.0f - ta.getX() - 280.0f);
    }
    // ---- the meters (L / R, -48 .. 0 dB)
    {
        const auto a = meterArea();
        g.setColour (K.dim); textAt (g, "LEVEL", capFont (7.4f, Weight::Medium, 0.08f), a.getCentreX(), a.getY() - 7.0f, juce::Justification::centred, 80.0f);
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto b = R (a.getX() + 10.0f + (float) ch * 24.0f, a.getY(), 16.0f, a.getHeight());
            g.setColour (K.well); g.fillRoundedRectangle (b, 5.0f);
            const float v = ch == 0 ? meterL : meterR;
            const float db = v > 1e-5f ? 20.0f * std::log10 (v) : -100.0f;
            const float f = juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 48.0f);
            const auto fill = b.reduced (2.0f).withTop (b.reduced (2.0f).getBottom() - f * b.reduced (2.0f).getHeight());
            g.setGradientFill (juce::ColourGradient (K.b, 0.0f, b.getY(), K.a, 0.0f, b.getBottom(), false)); g.fillRoundedRectangle (fill, 3.0f);
        }
    }
    // ---- the source parts (numbered like the boxes on the waveform; a click hears one)
    {
        const auto a = partsArea(); const auto parts = proc.takeParts();
        g.setColour (K.dim); textAt (g, "YOUR SOUND: " + juce::String ((int) parts.size()) + ((int) parts.size() == 1 ? " PART" : " PARTS") + (proc.partsEdited() ? " (YOURS)" : ""), capFont (7.4f, Weight::Medium, 0.08f), a.getX(), a.getY() + 8.0f);
        for (int i = 0; i < juce::jmin (8, (int) parts.size()); ++i)
        {
            const auto r = partChip (i); const bool hov = i == hoverPart;
            const auto c = K.at ((float) i / 7.0f);
            g.setColour (c.withAlpha (hov ? 0.32f : 0.18f)); g.fillRoundedRectangle (r, 8.0f);
            g.setColour (c.withAlpha (hov ? 1.0f : 0.7f)); g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.1f);
            g.setColour (K.ink); g.setFont (capFont (9.0f, Weight::Bold)); g.drawText (juce::String (i + 1), r.withHeight (19.0f).translated (0.0f, 2.0f), juce::Justification::centred, false);
            g.setColour (K.dim); g.setFont (capFont (6.4f, Weight::Medium)); g.drawText (juce::String (parts[(size_t) i].first, 1) + "s", r.withTrimmedTop (19.0f), juce::Justification::centred, false);
        }
        if (parts.size() > 8) { g.setColour (K.dim); textAt (g, "+" + juce::String ((int) parts.size() - 8), capFont (7.4f, Weight::Medium), a.getRight() - 20.0f, a.getY() + 8.0f); }
    }
    // ---- the deeper knobs' names + values, the progression
    {
        const auto ty2 = proc.knobType();
        if (tab == 0) { g.setColour (K.dim); textAt (g, gen::typeName (ty2), capFont (7.4f, Weight::Medium, 0.08f), partsArea().getX() + 222.0f, 204.0f); }
        for (int i = 0; i < kDeepShown; ++i)
        {
            if (! deep[i].isVisible()) continue;
            const auto b = deep[i].getBounds().toFloat();
            g.setColour (K.ink); textAt (g, gen::Recipe::deepName (ty2, i), capFont (7.6f, Weight::SemiBold, 0.05f), b.getCentreX(), b.getBottom() + 8.0f, juce::Justification::centred, 100.0f);
            g.setColour (K.dim); textAt (g, juce::String (juce::roundToInt (deep[i].getValue() * 100.0)) + "%", capFont (7.0f, Weight::Medium), b.getCentreX(), b.getBottom() + 20.0f, juce::Justification::centred, 80.0f);
        }

    }
}

#include "SliceRoll.inl"

// ---------------------------------------------------------------- build 6: the STYLE browser
juce::StringArray StyleBrowser::groups() { return { "ALL", "GHOST SLICE", "PIANO", "PARLOUR PIANO", "REVERIE", "SYNTH", "LEARNED", "ARPEZATOR", "HOOK LAB", "TRAP", "HIP HOP" }; }

StyleBrowser::StyleBrowser (SliceProcessor& p) : proc (p)
{
    addAndMakeVisible (search);
    search.setTextToShowWhenEmpty ("search: a name, a group, a word", juce::Colours::grey);
    search.setTooltip ("Type to find a style (a name, a group or a word from what it does: \"piano\", \"sad\", \"303\").");
    search.onTextChange = [this] { rebuild(); };
    addAndMakeVisible (doneBtn); doneBtn.setTooltip ("DONE: back to the main page (the take on screen is remade in the style you picked).");
    doneBtn.onClick = [this] { if (onDone) onDone(); };
    rebuild();
}

void StyleBrowser::rebuild()
{
    shown.clear();
    const auto q = search.getText().trim();
    for (auto st : SliceProcessor::styleOrder())
    {
        const juce::String g = gslice::gen::styleGroup (st);
        if (group != "ALL" && g != group && st != gslice::gen::Style::Auto) continue;
        if (q.isNotEmpty() && ! (juce::String (gslice::gen::styleName (st)).containsIgnoreCase (q) || g.containsIgnoreCase (q) || juce::String (gslice::gen::styleTip (st)).containsIgnoreCase (q))) continue;
        shown.push_back (st);
    }
    resized(); repaint();
}

void StyleBrowser::resized()
{
    search.setBounds (190, 10, 300, 32);
    doneBtn.setBounds (getWidth() - 132, 6, 120, 40);
    // the group chips, then the tiles (8 across)
    chips.clear(); float x = 16.0f;
    for (auto& g : groups()) { const float w = juce::jmax (52.0f, widthOf (capFont (8.0f, Weight::SemiBold, 0.06f), g) + 24.0f); chips.push_back ({ x, 52.0f, w, 26.0f }); x += w + 6.0f; }
    tiles.clear();
    const int cols = 8; const float gap = 5.0f, tw = ((float) getWidth() - 32.0f - gap * (cols - 1)) / cols, th = 29.0f;
    for (size_t i = 0; i < shown.size(); ++i) tiles.push_back ({ 16.0f + (float) (i % cols) * (tw + gap), 88.0f + (float) (i / cols) * (th + gap), tw, th });
}

juce::String StyleBrowser::getTooltip()
{
    if (hover >= 0 && hover < (int) shown.size()) return juce::String (gslice::gen::styleName (shown[(size_t) hover])) + ": " + gslice::gen::styleTip (shown[(size_t) hover]) + " Click: pick it and hear it.";
    const auto m = getMouseXYRelative().toFloat();
    for (size_t i = 0; i < chips.size(); ++i) if (chips[i].contains (m)) return "Show " + (groups()[(int) i] == "ALL" ? juce::String ("every style") : "the " + groups()[(int) i] + " styles") + ".";
    return "STYLE: every melody, chord and arp style the house has made. Click one to hear it and use it.";
}

void StyleBrowser::mouseMove (const juce::MouseEvent& e)
{
    int h = -1; for (size_t i = 0; i < tiles.size(); ++i) if (tiles[i].contains (e.position)) h = (int) i;
    if (h != hover) { hover = h; repaint(); }
}

void StyleBrowser::mouseDown (const juce::MouseEvent& e)
{
    for (size_t i = 0; i < chips.size(); ++i) if (chips[i].contains (e.position)) { group = groups()[(int) i]; rebuild(); return; }
    for (size_t i = 0; i < tiles.size() && i < shown.size(); ++i) if (tiles[i].contains (e.position)) { if (onPick) onPick (shown[i]); repaint(); return; }
}

void StyleBrowser::paint (juce::Graphics& g)
{
    if (S == nullptr) return;
    const auto& K = *S;
    const auto all = getLocalBounds().toFloat();
    g.setColour (K.bg); g.fillRoundedRectangle (all, 16.0f);
    glassPanel (g, all, 16.0f, K, false);
    { juce::ColourGradient bd (K.a2.withAlpha (0.7f), 0.0f, 0.0f, K.b.withAlpha (0.6f), all.getRight(), all.getBottom(), false); g.setGradientFill (bd); g.drawRoundedRectangle (all.reduced (0.8f), 16.0f, 1.4f); }
    g.setColour (K.ink); textAt (g, "STYLE", capFont (12.0f, Weight::SemiBold, 0.10f), 18.0f, 27.0f);
    g.setColour (K.dim); textAt (g, juce::String ((int) shown.size()) + " shown", capFont (8.0f, Weight::Medium, 0.06f), 504.0f, 27.0f);
    const auto gl = groups();
    for (size_t i = 0; i < chips.size(); ++i)
    {
        const bool on = gl[(int) i] == group; const auto c = K.at ((float) i / (float) juce::jmax (1, gl.size() - 1));
        g.setColour (on ? c.withAlpha (0.30f) : K.tile.withMultipliedAlpha (0.8f)); g.fillRoundedRectangle (chips[i], 13.0f);
        g.setColour (on ? c : K.panelLine.brighter (0.1f)); g.drawRoundedRectangle (chips[i].reduced (0.5f), 13.0f, 1.0f);
        g.setColour (on ? K.ink : K.dim); g.setFont (capFont (8.0f, Weight::SemiBold, 0.06f)); g.drawText (gl[(int) i], chips[i], juce::Justification::centred, false);
    }
    for (size_t i = 0; i < tiles.size() && i < shown.size(); ++i)
    {
        const auto st = shown[i]; const auto r = tiles[i]; const bool sel = st == proc.style, hov = (int) i == hover;
        int gi = gl.indexOf (gslice::gen::styleGroup (st)); if (gi < 0) gi = 0;
        const auto c = K.at ((float) gi / (float) juce::jmax (1, gl.size() - 1));
        if (sel) glow (g, r, 8.0f, c, 6.0f, 4, 0.12f);
        g.setColour (sel ? c.withAlpha (0.32f) : hov ? c.withAlpha (0.16f) : K.tile.withMultipliedAlpha (0.85f)); g.fillRoundedRectangle (r, 8.0f);
        g.setColour (sel ? c : hov ? c.withAlpha (0.8f) : K.panelLine.brighter (0.1f)); g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, sel ? 1.5f : 1.0f);
        g.setColour (c); g.fillRoundedRectangle (R (r.getX() + 7.0f, r.getCentreY() - 6.0f, 3.0f, 12.0f), 1.5f);
        g.setColour (K.ink); g.setFont (capFont (8.2f, Weight::SemiBold, 0.04f));
        g.drawFittedText (gslice::gen::styleName (st), r.withTrimmedLeft (16.0f).reduced (4.0f, 2.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
    }
    // the line about the style under the pointer (or the chosen one)
    const auto st = hover >= 0 && hover < (int) shown.size() ? shown[(size_t) hover] : proc.style;
    g.setColour (K.light ? K.a : K.a2); textAt (g, juce::String (gslice::gen::styleName (st)) + "  \xc2\xb7  " + gslice::gen::styleGroup (st), capFont (8.6f, Weight::SemiBold, 0.06f), 18.0f, all.getBottom() - 34.0f);
    g.setColour (K.ink.interpolatedWith (K.dim, 0.2f)); g.setFont (capFont (9.0f, Weight::Medium));
    g.drawFittedText (gslice::gen::styleTip (st), R (18.0f, all.getBottom() - 26.0f, all.getWidth() - 36.0f, 20.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
}

void ClearPill::paintButton (juce::Graphics& g, bool over, bool down)
{
    if (S == nullptr) return;
    const auto& K = *S;
    const auto r = getLocalBounds().toFloat().reduced (7.0f);
    const float rad = r.getHeight() * 0.5f;
    const auto f = capFont (8.6f, Weight::SemiBold, 0.12f);
    const auto dot = R (6.0f, 6.0f).withCentre ({ r.getX() + 12.0f, r.getCentreY() });
    if (on)
    {
        glow (g, r, rad, K.b, 6.0f, 4, K.light ? 0.07f : 0.13f);
        g.setGradientFill (juce::ColourGradient (K.b.withAlpha (K.light ? 0.20f : 0.26f), r.getX(), r.getY(), K.b.withAlpha (K.light ? 0.08f : 0.10f), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, rad);
        if (over) { g.setColour (K.b.withAlpha (0.08f)); g.fillRoundedRectangle (r, rad); }
        g.setColour (K.b.withAlpha (0.95f)); g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.3f);
        for (int i = 3; i >= 1; --i) { g.setColour (K.b.withAlpha (0.16f)); g.fillEllipse (dot.expanded (1.6f * (float) i)); }
        g.setColour (K.light ? K.b : K.b.brighter (0.4f)); g.fillEllipse (dot);
    }
    else
    {
        g.setColour (K.tile.withMultipliedAlpha (0.8f)); g.fillRoundedRectangle (r, rad);
        g.setColour (over ? K.a2.withAlpha (0.7f) : K.panelLine.brighter (0.1f)); g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f);
        g.setColour (K.faint); g.drawEllipse (dot.reduced (0.5f), 1.2f);
    }
    if (down) { g.setColour (juce::Colours::black.withAlpha (0.10f)); g.fillRoundedRectangle (r, rad); }
    g.setColour (on ? (K.light ? K.ink : juce::Colour (0xffe6fdff)) : (over ? K.ink.interpolatedWith (K.dim, 0.4f) : K.dim));
    textAt (g, "CLEAR", f, dot.getRight() + 6.0f, r.getCentreY() + 0.5f, juce::Justification::centredLeft, r.getRight() - dot.getRight() - 6.0f);
}

// ---------------------------------------------------------------- Stage
Stage::Stage (SliceProcessor& p) : proc (p)
{
    for (auto* b : { &loadBtn, &recordBtn, &zoomInBtn, &zoomOutBtn, &fitBtn, &playSrcBtn, &trimBtn }) addAndMakeVisible (b);
    loadBtn.iconSize = 22.0f; recordBtn.iconSize = 18.0f; playSrcBtn.iconSize = 15.0f; trimBtn.iconSize = 19.0f;
    zoomInBtn.iconSize = zoomOutBtn.iconSize = 21.0f; fitBtn.iconSize = 20.0f;
    loadBtn.setTooltip ("Pick a sound file (WAV, AIFF, FLAC, MP3). You can also drop one anywhere on this panel.");
    recordBtn.setTooltip ("Record your voice or any sound (up to 60 s). Press again to stop: the recording becomes the sound. Where it records from: the gear menu (INPUT).");
    zoomInBtn.setTooltip ("Zoom in on the waveform (or Ctrl / Cmd + mouse wheel).");
    zoomOutBtn.setTooltip ("Zoom out.");
    fitBtn.setTooltip ("Show the whole sound.");
    playSrcBtn.setTooltip ("Hear the sound itself (the trimmed part). Press again to stop.");
    trimBtn.setTooltip ("TRIM: drag the two handles on the waveform to choose the part of your sound GENERATE uses. Right-click: use the whole sound again.");
    zoomInBtn.onClick = [this] { zoomBy (0.5); };
    zoomOutBtn.onClick = [this] { zoomBy (2.0); };
    fitBtn.onClick = [this] { fitAll(); };
    trimBtn.onClick = [this]
    {
        if (juce::ModifierKeys::currentModifiers.isPopupMenu()) return;
        trimMode = ! trimMode; trimBtn.lit = trimMode; dragT0 = proc.trimStart(); dragT1 = proc.trimEnd(); repaint();
    };
}

juce::Rectangle<float> Stage::waveArea() const { return { 25.0f, 91.0f, 1340.0f, 190.0f }; }
juce::Rectangle<float> Stage::overviewArea() const { return { 27.0f, 291.0f, 1137.0f, 35.0f }; }

void Stage::resized()
{
    loadBtn.setBounds (1118, 29, 130, 50); recordBtn.setBounds (1263, 29, 145, 50);
    zoomInBtn.setBounds (1370, 115, 38, 36); zoomOutBtn.setBounds (1370, 155, 38, 36); fitBtn.setBounds (1370, 211, 38, 38);
    playSrcBtn.setBounds (1182, 288, 62, 38); trimBtn.setBounds (1261, 288, 147, 38);
}

double Stage::xToFrac (float x) const { const auto w = waveArea(); return v0 + (double) ((x - w.getX()) / w.getWidth()) * (v1 - v0); }
float Stage::fracToX (double f) const { const auto w = waveArea(); return w.getX() + (float) ((f - v0) / juce::jmax (1e-9, v1 - v0)) * w.getWidth(); }

void Stage::zoomBy (double factor)
{
    const auto src = proc.getSource(); if (src == nullptr) return;
    const double minW = juce::jlimit (0.0005, 1.0, 0.05 / juce::jmax (0.05, src->seconds()));
    const double c = (v0 + v1) * 0.5, w = juce::jlimit (minW, 1.0, (v1 - v0) * factor);
    v0 = juce::jlimit (0.0, 1.0 - w, c - w * 0.5); v1 = v0 + w;
    repaint();
}

bool Stage::isInterestedInFileDrag (const juce::StringArray& files) { for (auto& f : files) if (source::canLoad (juce::File (f))) return true; return false; }
void Stage::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false; repaint();
    for (auto& f : files) if (source::canLoad (juce::File (f))) { if (onFile) onFile (juce::File (f)); return; }
}
bool Stage::isInterestedInTextDrag (const juce::String& t)
{   // (some hosts hand a dragged file over as its path in text)
    const auto s = t.trim().upToFirstOccurrenceOf ("\n", false, false).trim().unquoted();
    return juce::File::isAbsolutePath (s) && source::canLoad (juce::File (s));
}
void Stage::textDropped (const juce::String& t, int, int)
{
    dragOver = false; repaint();
    const auto s = t.trim().upToFirstOccurrenceOf ("\n", false, false).trim().unquoted();
    if (juce::File::isAbsolutePath (s) && onFile) onFile (juce::File (s));
}

void Stage::mouseMove (const juce::MouseEvent& e)
{
    const auto p = e.position; auto cur = juce::MouseCursor::NormalCursor;
    if (proc.getSource() != nullptr)
    {
        const auto o = overviewArea();
        if (trimMode && waveArea().expanded (0, 6).contains (p) && (std::abs (p.x - fracToX (dragT0)) < 9.0f || std::abs (p.x - fracToX (dragT1)) < 9.0f)) cur = juce::MouseCursor::LeftRightResizeCursor;
        else if (o.contains (p))
        {
            const float x0 = o.getX() + (float) v0 * o.getWidth(), x1 = o.getX() + (float) v1 * o.getWidth();
            cur = std::abs (p.x - x0) < 8.0f || std::abs (p.x - x1) < 8.0f ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor;
        }
        else
        {
            const auto h = hitBox (p);
            if (h.what == 1 || h.what == 2) cur = juce::MouseCursor::LeftRightResizeCursor;
            else if (h.what == 3) cur = e.mods.isAltDown() ? juce::MouseCursor::CopyingCursor : juce::MouseCursor::DraggingHandCursor;
            if (h.box != hoverBox) { hoverBox = h.box; repaint(); }
        }
    }
    setMouseCursor (cur);
}

void Stage::mouseDown (const juce::MouseEvent& e)
{
    dragWhat = 0;
    if (proc.getSource() == nullptr || proc.recording()) return;
    const auto p = e.position; const auto o = overviewArea();
    dragV0 = v0; dragV1 = v1;
    if (trimMode && waveArea().expanded (0, 6).contains (p))
    {
        const float a = fracToX (dragT0), b = fracToX (dragT1);
        if (std::abs (p.x - a) < 9.0f && std::abs (p.x - a) <= std::abs (p.x - b)) { dragWhat = 1; return; }
        if (std::abs (p.x - b) < 9.0f) { dragWhat = 2; return; }
    }
    if (o.contains (p))
    {
        const float x0 = o.getX() + (float) v0 * o.getWidth(), x1 = o.getX() + (float) v1 * o.getWidth();
        const double f = (double) ((p.x - o.getX()) / o.getWidth());
        if (std::abs (p.x - x0) < 8.0f) dragWhat = 4;
        else if (std::abs (p.x - x1) < 8.0f) dragWhat = 5;
        else
        {
            if (p.x < x0 || p.x > x1) { const double w = v1 - v0; v0 = juce::jlimit (0.0, 1.0 - w, f - w * 0.5); v1 = v0 + w; dragV0 = v0; dragV1 = v1; repaint(); }
            dragWhat = 3;
        }
        dragAnchor = f;
        return;
    }
    if (waveArea().contains (p))
        if (const auto h = hitBox (p); h.box >= 0)
        {
            const double secs = proc.getSource()->seconds();
            if (e.mods.isPopupMenu()) { boxMenu (h.box, xToFrac (p.x) * secs); return; }
            editBoxes = proc.takeParts(); editBox = h.box; editWhat = h.what; editDup = false; dragMoved = false;
            grabSec = xToFrac (p.x) * secs; grabBox = editBoxes[(size_t) h.box];
            if (h.what == 3 && e.mods.isAltDown() && (int) editBoxes.size() < proc.maxParts())
            {   // Alt / Option-drag: a copy of the box is dragged (it lands in the nearest free space)
                editBoxes.insert (editBoxes.begin() + h.box + 1, grabBox); editBox = h.box + 1; editDup = true;
            }
            dragWhat = 7;
            return;
        }
    if (waveArea().contains (p) && v1 - v0 < 0.999) { dragWhat = 6; dragAnchor = (double) p.x; }
}

Stage::BoxHit Stage::hitBox (juce::Point<float> p) const
{
    BoxHit h;
    const auto src = proc.getSource();
    if (src == nullptr || proc.currentTake() == nullptr || ! waveArea().contains (p)) return h;
    const double secs = juce::jmax (1e-6, src->seconds());
    const auto parts = dragWhat == 7 ? editBoxes : proc.takeParts();
    float bestD = 1e9f;
    for (int i = 0; i < (int) parts.size(); ++i)
    {   // the edges first (the nearest within a few pixels), then the middle
        const float x0 = fracToX (parts[(size_t) i].first / secs), x1 = fracToX (parts[(size_t) i].second / secs);
        const float edge = juce::jlimit (3.0f, 7.0f, (x1 - x0) / 3.0f);
        if (std::abs (p.x - x0) <= edge && std::abs (p.x - x0) < bestD) { bestD = std::abs (p.x - x0); h = { i, 1 }; }
        if (std::abs (p.x - x1) <= edge && std::abs (p.x - x1) < bestD) { bestD = std::abs (p.x - x1); h = { i, 2 }; }
    }
    if (h.box >= 0) return h;
    for (int i = 0; i < (int) parts.size(); ++i)
    {
        const float x0 = fracToX (parts[(size_t) i].first / secs), x1 = fracToX (parts[(size_t) i].second / secs);
        if (p.x > x0 && p.x < x1) return { i, 3 };
    }
    return h;
}

double Stage::snapSec (double sec, bool free, double pxTol) const
{
    const auto src = proc.getSource();
    if (src == nullptr || free) return sec;
    const double sr = src->sampleRate, secs = juce::jmax (1e-6, src->seconds());
    const double secPerPx = (v1 - v0) * secs / juce::jmax (1.0f, waveArea().getWidth());
    double best = sec, bd = pxTol * secPerPx;
    auto consider = [&] (double t) { if (std::abs (t - sec) < bd) { bd = std::abs (t - sec); best = t; } };
    for (size_t i = 0; i < src->onsets.size(); ++i) consider ((double) src->onsets[i] / sr);
    if (auto P = proc.getPrepared(); P != nullptr && P->analysis != nullptr && P->src != nullptr && P->src->frames == src->frames)
        for (auto& sy : P->analysis->syllables) { consider ((double) sy.start / sr); consider ((double) sy.end / sr); }
    // then the nearest zero crossing (2.5 ms either side): the part starts and ends without a click
    const int64 c = (int64) std::llround (best * sr), R = (int64) (0.0025 * sr);
    auto at = [&] (int64 k) { if (k < 0 || k >= src->frames) return 0.0f; float v = 0; for (int ch = 0; ch < src->channels; ++ch) v += src->audio.getSample (ch, (int) k); return v; };
    for (int64 d = 0; d <= R; ++d)
        for (int64 k : { c - d, c + d })
            if (k > 0 && k < src->frames && ((at (k - 1) <= 0.0f && at (k) > 0.0f) || (at (k - 1) >= 0.0f && at (k) < 0.0f) || at (k) == 0.0f)) return (double) k / sr;
    return (double) c / sr;
}

void Stage::hearBox (int i)
{
    const auto parts = proc.takeParts();
    if (i < 0 || i >= (int) parts.size()) return;
    proc.playRegion (parts[(size_t) i].first, parts[(size_t) i].second);
    if (onHear) onHear (i);
    repaint();
}

bool Stage::addBoxAt (double sec)
{
    if (proc.currentTake() == nullptr || proc.getSource() == nullptr) return false;
    auto parts = proc.takeParts();
    const auto lim = proc.partLimits();
    if ((int) parts.size() >= proc.maxParts())
    {
        const auto ty = proc.slotType (proc.currentIndex());
        proc.lastMessage = juce::String (gen::typeName (ty)) + " uses " + (proc.maxParts() == 1 ? juce::String ("one part") : "up to " + juce::String (proc.maxParts()) + " parts") + ": move or resize the ones there.";
        if (proc.onToast) proc.onToast (proc.lastMessage);
        return false;
    }
    if (sec < lim.getStart() || sec > lim.getEnd()) return false;
    double lo = lim.getStart(), hi = lim.getEnd();
    for (auto& q : parts) { if (sec >= q.first && sec <= q.second) return false; if (q.second <= sec) lo = std::max (lo, q.second); if (q.first >= sec) hi = std::min (hi, q.first); }
    const double len = std::min (proc.slotType (proc.currentIndex()) == gen::Type::Chops ? 0.25 : 0.4, hi - lo);
    if (len < kMinPart) { if (proc.onToast) proc.onToast ("No room for a new part there."); return false; }
    double a = juce::jlimit (lo, hi - len, sec - len * 0.5);
    double sa = snapSec (a, false), sb = snapSec (a + len, false);
    if (sa < lo) sa = lo; if (sb > hi) sb = hi; if (sb - sa < kMinPart) { sa = a; sb = a + len; }
    parts.push_back ({ sa, sb });
    return proc.editParts (parts);
}

void Stage::boxMenu (int i, double atSec)
{
    const auto parts = proc.takeParts();
    if (i < 0 || i >= (int) parts.size()) return;
    const auto q = parts[(size_t) i]; const int n = (int) parts.size(), cap = proc.maxParts();
    const double cut = snapSec (atSec, false);
    juce::PopupMenu m;
    m.addItem (1, "Hear this part");
    m.addItem (2, "Split here", n < cap && cut - q.first >= kMinPart && q.second - cut >= kMinPart);
    m.addItem (3, "Duplicate", n < cap);
    m.addSeparator();
    m.addItem (4, "Delete", n > 1);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (juce::Rectangle<int> (getMouseXYRelative().x, getMouseXYRelative().y, 1, 1) + getScreenPosition()),
                     [sp = juce::Component::SafePointer<Stage> (this), i, cut] (int r)
    {
        if (sp == nullptr || r <= 0) return;
        auto ps = sp->proc.takeParts();
        if (i >= (int) ps.size()) return;
        const auto b = ps[(size_t) i];
        if (r == 1) { sp->hearBox (i); return; }
        if (r == 2) { ps[(size_t) i].second = cut; ps.insert (ps.begin() + i + 1, { cut, b.second }); }
        else if (r == 3)
        {   // the copy right after the part (or right before it) where there is room
            const auto lim = sp->proc.partLimits(); const double len = b.second - b.first;
            const double nextStart = i + 1 < (int) ps.size() ? ps[(size_t) i + 1].first : lim.getEnd(), prevEnd = i > 0 ? ps[(size_t) i - 1].second : lim.getStart();
            if (nextStart - b.second >= len) ps.insert (ps.begin() + i + 1, { b.second, b.second + len });
            else if (b.first - prevEnd >= len) ps.insert (ps.begin() + i, { b.first - len, b.first });
            else { if (sp->proc.onToast) sp->proc.onToast ("No room next to that part for a copy."); return; }
        }
        else if (r == 4) ps.erase (ps.begin() + i);
        sp->proc.editParts (ps);
        sp->repaint();
    });
}

void Stage::mouseDrag (const juce::MouseEvent& e)
{
    const auto p = e.position; const auto o = overviewArea();
    if (dragWhat == 7 && proc.getSource() == nullptr) { dragWhat = 0; return; }
    const double f = (double) ((p.x - o.getX()) / o.getWidth());
    const double minW = 0.002;
    switch (dragWhat)
    {
        case 1: dragT0 = juce::jlimit (0.0, dragT1 - minW, xToFrac (p.x)); break;
        case 2: dragT1 = juce::jlimit (dragT0 + minW, 1.0, xToFrac (p.x)); break;
        case 3: { const double w = dragV1 - dragV0; v0 = juce::jlimit (0.0, 1.0 - w, dragV0 + (f - dragAnchor)); v1 = v0 + w; break; }
        case 4: v0 = juce::jlimit (0.0, v1 - minW, f); break;
        case 5: v1 = juce::jlimit (v0 + minW, 1.0, f); break;
        case 6: { const double w = v1 - v0, d = -(double) (p.x - (float) dragAnchor) / (double) waveArea().getWidth() * w;
                  v0 = juce::jlimit (0.0, 1.0 - w, dragV0 + d); v1 = v0 + w; break; }
        case 7:
        {   // a part: move (the middle) or resize (an edge), snapped, inside its neighbours and the take's TRIM part
            if (! dragMoved && std::abs (p.x - e.mouseDownPosition.x) < 3.0f) return;
            dragMoved = true;
            const double secs = proc.getSource()->seconds(), d = xToFrac (p.x) * secs - grabSec;
            const auto lim = proc.partLimits(); const bool free = e.mods.isShiftDown();
            double lo = lim.getStart(), hi = lim.getEnd();
            if (! editDup)
            {
                if (editBox > 0) lo = std::max (lo, editBoxes[(size_t) editBox - 1].second);
                if (editBox + 1 < (int) editBoxes.size()) hi = std::min (hi, editBoxes[(size_t) editBox + 1].first);
            }
            auto& b = editBoxes[(size_t) editBox];
            const double len = grabBox.second - grabBox.first;
            if (editWhat == 3)
            {
                double a = grabBox.first + d;
                const double sa = snapSec (a, free), sb = snapSec (a + len, free);
                a = std::abs (sb - (a + len)) < std::abs (sa - a) ? sb - len : sa;
                a = juce::jlimit (lo, std::max (lo, hi - len), a);
                b = { a, std::min (hi, a + len) };
            }
            else if (editWhat == 1) b.first = juce::jlimit (lo, b.second - kMinPart, snapSec (grabBox.first + d, free));
            else b.second = juce::jlimit (b.first + kMinPart, hi, snapSec (grabBox.second + d, free));
            break;
        }
        default: return;
    }
    repaint();
}

void Stage::mouseUp (const juce::MouseEvent&)
{
    if (dragWhat == 1 || dragWhat == 2) { proc.setTrim (dragT0, dragT1); dragT0 = proc.trimStart(); dragT1 = proc.trimEnd(); }
    if (dragWhat == 7)
    {
        if (! dragMoved) { if (! editDup) hearBox (editBox); }   // a click: hear the part
        else
        {
            auto ps = editBoxes; bool ok = true;
            if (editDup)
            {   // the copy lands in the free space nearest where it was let go
                const auto c = ps[(size_t) editBox]; ps.erase (ps.begin() + editBox);
                const auto lim = proc.partLimits(); const double len = c.second - c.first;
                std::vector<std::pair<double, double>> gaps; double prev = lim.getStart();
                for (auto& q : ps) { gaps.push_back ({ prev, q.first }); prev = q.second; }
                gaps.push_back ({ prev, lim.getEnd() });
                double bestA = -1, bestD = 1e18;
                for (auto& g : gaps) if (g.second - g.first >= len) { const double a = juce::jlimit (g.first, g.second - len, c.first); if (std::abs (a - c.first) < bestD) { bestD = std::abs (a - c.first); bestA = a; } }
                if (bestA < 0) { ok = false; if (proc.onToast) proc.onToast ("No room for a copy of that part."); }
                else ps.push_back ({ bestA, bestA + len });
            }
            if (ok) proc.editParts (ps);
        }
        editBox = -1; editBoxes.clear(); editDup = false;
    }
    dragWhat = 0; repaint();
}

void Stage::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (waveArea().contains (e.position) && proc.currentTake() != nullptr && proc.getSource() != nullptr && ! (trimMode && (std::abs (e.position.x - fracToX (dragT0)) < 9.0f || std::abs (e.position.x - fracToX (dragT1)) < 9.0f)))
    {
        if (hitBox (e.position).box < 0) addBoxAt (xToFrac (e.position.x) * proc.getSource()->seconds());
        return;
    }
    if (waveArea().contains (e.position) || overviewArea().contains (e.position)) fitAll();
}

void Stage::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (proc.getSource() == nullptr) return;
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        const double at = xToFrac (e.position.x), f = w.deltaY > 0 ? 0.8 : 1.25;
        const double minW = 0.002, nw = juce::jlimit (minW, 1.0, (v1 - v0) * f), k = (at - v0) / juce::jmax (1e-9, v1 - v0);
        v0 = juce::jlimit (0.0, 1.0 - nw, at - k * nw); v1 = v0 + nw;
    }
    else
    {
        const double d = (std::abs (w.deltaX) > std::abs (w.deltaY) ? -w.deltaX : -w.deltaY) * (v1 - v0) * 0.5, wv = v1 - v0;
        v0 = juce::jlimit (0.0, 1.0 - wv, v0 + d); v1 = v0 + wv;
    }
    repaint();
}

void Stage::paintWave (juce::Graphics& g, const SourceAudio& src)
{
    const auto wr = waveArea().reduced (0.0f, 6.0f);
    const int W = (int) wr.getWidth() * 2;
    const Key k { &src, v0, v1, W };
    if (! (k == cacheKey))
    {
        cacheKey = k; wavePath.clear(); corePath.clear();
        float pk = 1e-6f; for (int ch = 0; ch < src.channels; ++ch) pk = std::max (pk, src.audio.getMagnitude (ch, 0, (int) src.frames));
        const float scale = 0.98f / pk, mid = wr.getCentreY(), half = wr.getHeight() * 0.5f;
        std::vector<float> top ((size_t) W), bot ((size_t) W), ct ((size_t) W);
        const double f0 = v0 * (double) src.frames, f1 = v1 * (double) src.frames;
        for (int i = 0; i < W; ++i)
        {
            const int64 a = (int64) (f0 + (double) i / W * (f1 - f0)), b = juce::jmax (a + 1, (int64) (f0 + (double) (i + 1) / W * (f1 - f0)));
            float mn = 0, mx = 0, ms = 0;
            for (int ch = 0; ch < src.channels; ++ch) { const auto q = src.peaks.query (src.audio, ch, a, juce::jmin (b, src.frames)); mn = std::min (mn, q.mn); mx = std::max (mx, q.mx); ms = std::max (ms, q.ms); }
            top[(size_t) i] = mid - juce::jmax (0.004f, juce::jmin (1.0f, mx * scale)) * half; bot[(size_t) i] = mid - juce::jmin (-0.004f, juce::jmax (-1.0f, mn * scale)) * half;
            ct[(size_t) i] = juce::jmin (1.0f, std::sqrt (ms) * scale * 1.15f) * half;
        }
        const float dx = wr.getWidth() / (float) W;
        wavePath.startNewSubPath (wr.getX(), top[0]);
        for (int i = 1; i < W; ++i) wavePath.lineTo (wr.getX() + (float) i * dx, top[(size_t) i]);
        for (int i = W; i-- > 0;) wavePath.lineTo (wr.getX() + (float) i * dx, bot[(size_t) i]);
        wavePath.closeSubPath();
        corePath.startNewSubPath (wr.getX(), mid - ct[0]);
        for (int i = 1; i < W; ++i) corePath.lineTo (wr.getX() + (float) i * dx, mid - ct[(size_t) i]);
        for (int i = W; i-- > 0;) corePath.lineTo (wr.getX() + (float) i * dx, mid + ct[(size_t) i]);
        corePath.closeSubPath();
    }
    const auto& K = *S;
    // the glow, the body, the bright core
    g.setGradientFill (hGrad (K, wr.getX(), wr.getRight(), wr.getCentreY(), 0.22f));
    g.strokePath (wavePath, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setGradientFill (hGrad (K, wr.getX(), wr.getRight(), wr.getCentreY(), 0.95f)); g.fillPath (wavePath);
    { juce::ColourGradient cg (juce::Colours::white.withAlpha (K.light ? 0.0f : 0.28f), wr.getX(), wr.getCentreY(), juce::Colours::white.withAlpha (K.light ? 0.0f : 0.22f), wr.getRight(), wr.getCentreY(), false);
      if (K.light) { cg = juce::ColourGradient (K.a.darker (0.3f).withAlpha (0.35f), wr.getX(), 0, K.b.darker (0.3f).withAlpha (0.35f), wr.getRight(), 0, false); }
      g.setGradientFill (cg); g.fillPath (corePath); }
    g.setGradientFill (hGrad (K, wr.getX(), wr.getRight(), wr.getCentreY(), 0.65f)); g.fillRect (wr.getX(), wr.getCentreY() - 0.5f, wr.getWidth(), 1.0f);
}

void Stage::paintOverview (juce::Graphics& g, const SourceAudio& src)
{
    const auto& K = *S;
    const auto o = overviewArea();
    if (overKey != &src)
    {
        overKey = &src; overPath.clear();
        const auto ir = o.reduced (8.0f, 6.0f); const int W = (int) ir.getWidth();
        float pk = 1e-6f; for (int ch = 0; ch < src.channels; ++ch) pk = std::max (pk, src.audio.getMagnitude (ch, 0, (int) src.frames));
        std::vector<float> hh ((size_t) W);
        for (int i = 0; i < W; ++i)
        {
            const int64 a = (int64) ((double) i / W * (double) src.frames), b = juce::jmax (a + 1, (int64) ((double) (i + 1) / W * (double) src.frames));
            float m = 0; for (int ch = 0; ch < src.channels; ++ch) { const auto q = src.peaks.query (src.audio, ch, a, juce::jmin (b, src.frames)); m = std::max (m, std::max (q.mx, -q.mn)); }
            hh[(size_t) i] = juce::jmax (0.5f, m / pk * ir.getHeight() * 0.5f);
        }
        overPath.startNewSubPath (ir.getX(), ir.getCentreY() - hh[0]);
        for (int i = 1; i < W; ++i) overPath.lineTo (ir.getX() + (float) i, ir.getCentreY() - hh[(size_t) i]);
        for (int i = W; i-- > 0;) overPath.lineTo (ir.getX() + (float) i, ir.getCentreY() + hh[(size_t) i]);
        overPath.closeSubPath();
    }
    const auto ir = o.reduced (8.0f, 6.0f);
    g.setColour (K.dim.withAlpha (K.light ? 0.45f : 0.42f)); g.fillPath (overPath);
    auto fx = [&] (double f) { return ir.getX() + (float) f * ir.getWidth(); };
    // the trimmed-off parts
    const double t0 = trimMode ? dragT0 : proc.trimStart(), t1 = trimMode ? dragT1 : proc.trimEnd();
    if (t0 > 0.0) { g.setColour (K.bg.withAlpha (0.55f)); g.fillRect (R::leftTopRightBottom (o.getX() + 2.0f, o.getY() + 2.0f, fx (t0), o.getBottom() - 2.0f)); }
    if (t1 < 1.0) { g.setColour (K.bg.withAlpha (0.55f)); g.fillRect (R::leftTopRightBottom (fx (t1), o.getY() + 2.0f, o.getRight() - 2.0f, o.getBottom() - 2.0f)); }
    // the parts the current take used: bright bars on the centre line, a grip at each end
    const double secs = juce::jmax (1e-6, src.seconds());
    if (auto take = proc.currentTake())
        for (auto& u : take->used)
        {
            const float x0 = fx (u.first / secs), x1 = juce::jmax (x0 + 3.0f, fx (u.second / secs));
            const auto c = K.at ((x0 + x1) * 0.5f / o.getRight());
            g.setColour (c.withAlpha (0.3f)); g.fillRoundedRectangle (x0, ir.getCentreY() - 3.5f, x1 - x0, 7.0f, 3.5f);
            g.setColour (c); g.fillRoundedRectangle (x0, ir.getCentreY() - 1.5f, x1 - x0, 3.0f, 1.5f);
        }
    // the view window
    const float vx0 = fx (v0), vx1 = fx (v1);
    if (v1 - v0 < 0.999)
    {
        const auto vr = R::leftTopRightBottom (vx0, o.getY() + 3.0f, vx1, o.getBottom() - 3.0f);
        g.setColour (K.a2.withAlpha (0.10f)); g.fillRoundedRectangle (vr, 4.0f);
        g.setColour (K.a2.withAlpha (0.55f)); g.drawRoundedRectangle (vr, 4.0f, 1.0f);
    }
    // the grips: the view's ends when zoomed, else the span the take used (first part's start, last part's end)
    float gx0 = vx0, gx1 = vx1;
    if (v1 - v0 >= 0.999)
        if (auto take = proc.currentTake(); take != nullptr && ! take->used.empty())
        {
            double lo = 1e9, hi = -1e9; for (auto& u : take->used) { lo = std::min (lo, u.first); hi = std::max (hi, u.second); }
            gx0 = fx (lo / secs); gx1 = fx (hi / secs);
        }
    for (int k = 0; k < 2; ++k)
    {
        const float x = k == 0 ? gx0 : gx1; const auto c = k == 0 ? K.a : K.b;
        const auto hr = R (8.0f, 16.0f).withCentre ({ juce::jlimit (o.getX() + 5.0f, o.getRight() - 5.0f, x), o.getCentreY() });
        g.setColour (c.withAlpha (0.25f)); g.fillRoundedRectangle (hr.expanded (3.0f), 5.0f);
        g.setGradientFill (juce::ColourGradient (c.brighter (0.4f), hr.getX(), hr.getY(), c, hr.getX(), hr.getBottom(), false)); g.fillRoundedRectangle (hr, 3.5f);
    }
    if (proc.sourcePlaying())
    {
        const double pf = proc.sourcePosition() / secs; g.setColour (K.ink); g.fillRect (fx (pf) - 0.75f, o.getY() + 3.0f, 1.5f, o.getHeight() - 6.0f);
    }
}

void Stage::paint (juce::Graphics& g)
{
    if (S == nullptr) return;
    const auto& K = *S;
    const auto src = proc.getSource();
    const bool rec = proc.recording();
    const auto wr = waveArea(), o = overviewArea();
    // the wells: the waveform and the overview
    g.setColour (K.well); g.fillRoundedRectangle (wr.expanded (4.0f, 4.0f), 10.0f);
    g.setColour (K.well); g.fillRoundedRectangle (o, 8.0f);
    g.setColour (K.panelLine.withAlpha (0.5f)); g.drawRoundedRectangle (o.reduced (0.5f), 8.0f, 1.0f);
    // the tools column
    { const auto tc = R (1366.0f, 103.0f, 46.0f, 160.0f); g.setColour (K.well); g.fillRoundedRectangle (tc, 10.0f); g.setColour (K.panelLine.withAlpha (0.45f)); g.drawRoundedRectangle (tc.reduced (0.5f), 10.0f, 1.0f);
      g.setColour (K.panelLine.withAlpha (0.6f)); g.drawRoundedRectangle (R (1370.0f, 211.0f, 38.0f, 38.0f), 8.0f, 1.0f); }
    // faint vertical grid (beats when the tempo is known)
    {
        g.setColour ((K.light ? K.ink : juce::Colours::white).withAlpha (K.light ? 0.05f : 0.035f));
        const int lines = 54;
        for (int i = 1; i < lines; ++i) g.fillRect (wr.getX() + wr.getWidth() * (float) i / (float) lines, wr.getY(), 1.0f, wr.getHeight());
    }
    const auto capName = capFont (11.5f, Weight::SemiBold), capInfo = capFont (9.6f, Weight::Medium, 0.02f);
    if (rec)
    {   // RECORDING: a breathing ring, the seconds, the level
        const auto c = wr.getCentre().translated (0.0f, -14.0f); const float lv = juce::jlimit (0.0f, 1.0f, std::sqrt (proc.inputLevel()) * 1.6f);
        const float t = (float) juce::Time::getMillisecondCounterHiRes() * 0.001f;
        const float d = 96.0f * (1.0f + 0.05f * std::sin (t * 3.0f) + 0.18f * lv);
        for (int i = 4; i >= 1; --i) { g.setColour (K.bad.withAlpha (0.06f * (float) i)); g.fillEllipse (R (d + 14.0f * (float) i, d + 14.0f * (float) i).withCentre (c)); }
        g.setColour (K.bad); g.drawEllipse (R (d, d).withCentre (c), 5.0f);
        g.setColour (K.bad); g.fillEllipse (R (26.0f, 26.0f).withCentre (c));
        g.setColour (K.ink); textAt (g, "RECORDING  " + juce::String (proc.recordedSeconds(), 1) + " s", capFont (14.0f, Weight::Bold, 0.14f), wr.getCentreX(), c.y + d * 0.5f + 26.0f, juce::Justification::centred, 700.0f);
        g.setColour (K.dim); textAt (g, "from " + proc.recordSourceText() + ".  Press STOP when you are done.", capFont (9.0f, Weight::Medium), wr.getCentreX(), c.y + d * 0.5f + 52.0f, juce::Justification::centred, 900.0f);
        g.setColour (K.ink); textAt (g, "Recording...", capName, 41.0f, 41.0f);
        g.setColour (K.dim); textAt (g, juce::String (proc.recordedSeconds(), 1) + "s of 60s", capInfo, 41.0f, 66.0f);
        return;
    }
    if (src == nullptr)
    {   // the drop zone
        auto dz = wr.reduced (6.0f, 2.0f);
        juce::Path dash; dash.addRoundedRectangle (dz, 12.0f);
        juce::Path out; const float pat[] { 10.0f, 7.0f };
        juce::PathStrokeType (dragOver ? 2.6f : 1.6f).createDashedStroke (out, dash, pat, 2);
        g.setColour (dragOver ? K.b : K.panelLine); g.fillPath (out);
        if (dragOver) { g.setColour (K.b.withAlpha (0.07f)); g.fillRoundedRectangle (dz, 12.0f); }
        const float cy = wr.getCentreY() - 40.0f;
        drawSliceGhost (g, MascotColours(), wr.getCentreX() + 8.0f, cy - 2.0f, 70.0f);
        g.setColour (K.ink); textAt (g, proc.isLoading() ? "LOADING..." : "DROP A VOCAL OR ANY SOUND", capFont (17.0f, Weight::Bold, 0.12f), wr.getCentreX(), cy + 58.0f, juce::Justification::centred, 900.0f);
        g.setColour (K.dim); textAt (g, "Drag a file here from Finder or FL Studio's browser, or press LOAD... or RECORD.", capFont (9.0f, Weight::Medium), wr.getCentreX(), cy + 88.0f, juce::Justification::centred, 900.0f);
        g.setColour (K.ink); textAt (g, "No sound yet", capName, 41.0f, 41.0f);
        g.setColour (K.dim); textAt (g, "a vocal, a sentence, any sound", capInfo, 41.0f, 66.0f);
        return;
    }
    const double secs = src->seconds();
    // the file and what it is
    g.setColour (K.ink); textAt (g, src->file.getFileName().isNotEmpty() ? src->file.getFileName() : juce::String ("your sound"), capName, 41.0f, 41.0f, juce::Justification::centredLeft, 900.0f);
    {
        const auto k = proc.detectedKey();
        juce::StringArray parts;
        parts.add (proc.keyTonic >= 0 ? keyName (proc.keyTonic, proc.keyMinor) : proc.keyKnown() ? keyName (k.tonic, k.minor) : juce::String ("LISTENING..."));
        parts.add (juce::String (juce::roundToInt (proc.tempo())) + " BPM");
        parts.add (secondsText (secs));
        if (proc.isTrimmed()) parts.add ("TRIM " + juce::String (proc.trimStart() * secs, 1) + "-" + juce::String (proc.trimEnd() * secs, 1) + "s");
        float x = 41.0f;
        for (int i = 0; i < parts.size(); ++i)
        {
            if (i > 0) { g.setColour (K.dim); textAt (g, "|", capInfo, x + 10.0f, 66.0f); x += 26.0f; }
            g.setColour (i == 3 ? K.b : K.ink.interpolatedWith (K.dim, 0.15f)); textAt (g, parts[i], capInfo, x, 66.0f);
            x += widthOf (capInfo, parts[i]);
        }
    }
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (wr.expanded (4.0f, 4.0f).toNearestInt());
        paintWave (g, *src);
        // the parts the take used: translucent boxes with handle dots (the generator's merged when near; the user's exactly as drawn)
        auto take = proc.currentTake();
        std::vector<std::pair<double, double>> used;
        const bool editing = dragWhat == 7 && dragMoved;
        if (editing) used = editBoxes;
        else if (proc.partsEdited()) used = proc.takeParts();
        else if (take != nullptr)
        {   // neighbouring parts read as one box
            used = take->used; std::sort (used.begin(), used.end());
            std::vector<std::pair<double, double>> m;
            for (auto& u : used) { if (! m.empty() && u.first - m.back().second < secs * 0.02) m.back().second = std::max (m.back().second, u.second); else m.push_back (u); }
            used = m;
        }
        auto drawBox = [&] (std::pair<double, double> u, bool hot)
        {
            const float x0 = fracToX (u.first / secs), x1 = juce::jmax (x0 + 3.0f, fracToX (u.second / secs));
            if (x1 < wr.getX() || x0 > wr.getRight()) return;
            const auto c = K.at (juce::jlimit (0.0f, 1.0f, ((x0 + x1) * 0.5f - wr.getX()) / wr.getWidth()));
            const auto box = R::leftTopRightBottom (x0, wr.getY() + 2.0f, x1, wr.getBottom() - 2.0f);
            g.setColour (c.withAlpha (hot ? 0.30f : 0.16f)); g.fillRect (box);
            g.setColour (hot ? c.brighter (0.35f) : c.withAlpha (0.75f)); g.drawRect (box, hot ? 2.0f : 1.2f);
            const float k = hot ? 1.45f : 1.0f;
            for (auto pt : { box.getTopLeft(), box.getTopRight() }) { g.setColour (c.brighter (0.3f)); g.fillEllipse (R (7.0f * k, 7.0f * k).withCentre (pt)); }
            for (auto pt : { box.getBottomLeft(), box.getBottomRight() }) { g.setColour (c.withAlpha (0.3f)); g.fillEllipse (R (14.0f * k, 14.0f * k).withCentre (pt)); g.setColour (c.brighter (0.25f)); g.fillEllipse (R (9.0f * k, 9.0f * k).withCentre (pt)); }
        };
        const int hotI = editing ? editBox : (dragWhat == 7 ? editBox : hoverBox);
        const auto hotList = editing ? editBoxes : proc.takeParts();
        for (auto& u : used) drawBox (u, false);
        if (hotI >= 0 && hotI < (int) hotList.size()) drawBox (hotList[(size_t) hotI], true);
        if (proc.busy() && proc.partsEdited())
        {   // re-rendering from the user's parts: a light sweeps through the boxes
            const float ph = (float) std::fmod (juce::Time::getMillisecondCounterHiRes() * 0.0011, 1.0);
            for (auto& u : used)
            {
                const float x0 = fracToX (u.first / secs), x1 = juce::jmax (x0 + 3.0f, fracToX (u.second / secs)), w = x1 - x0, cx = x0 + ph * w;
                juce::ColourGradient sh (juce::Colours::white.withAlpha (0.0f), cx - 0.25f * w - 6.0f, 0, juce::Colours::white.withAlpha (0.0f), cx + 0.25f * w + 6.0f, 0, false);
                sh.addColour (0.5, juce::Colours::white.withAlpha (K.light ? 0.35f : 0.22f));
                g.setGradientFill (sh); g.fillRect (R::leftTopRightBottom (x0, wr.getY() + 2.0f, x1, wr.getBottom() - 2.0f));
            }
        }
        if (editing && editBox >= 0 && editBox < (int) editBoxes.size())
        {   // the time readout while dragging
            const auto u = editBoxes[(size_t) editBox];
            const float x0 = fracToX (u.first / secs), x1 = fracToX (u.second / secs);
            const auto txt = juce::String (u.first, 2) + juce::String (juce::CharPointer_UTF8 (" \xe2\x80\x93 ")) + juce::String (u.second, 2) + "s  (" + juce::String ((u.second - u.first) * 1000.0, 0) + " ms)";
            const auto f = capFont (8.2f, Weight::SemiBold, 0.03f); const float tw = widthOf (f, txt) + 16.0f;
            const auto tr = R (tw, 20.0f).withCentre ({ juce::jlimit (wr.getX() + tw * 0.5f, wr.getRight() - tw * 0.5f, (x0 + x1) * 0.5f), wr.getY() + 16.0f });
            g.setColour (K.well.withAlpha (1.0f).withMultipliedAlpha (0.92f)); g.fillRoundedRectangle (tr, 6.0f);
            g.setColour (K.b.withAlpha (0.8f)); g.drawRoundedRectangle (tr, 6.0f, 1.0f);
            g.setColour (K.ink); textAt (g, txt, f, tr.getCentreX(), tr.getCentreY(), juce::Justification::centred, tw);
        }
        // TRIM: the parts left out are dimmed; in TRIM mode the two handles
        const double t0 = trimMode ? dragT0 : proc.trimStart(), t1 = trimMode ? dragT1 : proc.trimEnd();
        const float tx0 = fracToX (t0), tx1 = fracToX (t1);
        g.setColour (K.bg.withAlpha (0.6f));
        if (tx0 > wr.getX()) g.fillRect (R::leftTopRightBottom (wr.getX() - 4.0f, wr.getY() - 4.0f, tx0, wr.getBottom() + 4.0f));
        if (tx1 < wr.getRight()) g.fillRect (R::leftTopRightBottom (tx1, wr.getY() - 4.0f, wr.getRight() + 4.0f, wr.getBottom() + 4.0f));
        if (trimMode)
            for (int k = 0; k < 2; ++k)
            {
                const float x = k == 0 ? tx0 : tx1; const auto c = k == 0 ? K.a2 : K.b;
                g.setColour (c); g.fillRect (x - 1.0f, wr.getY(), 2.0f, wr.getHeight());
                const auto kn = R (14.0f, 30.0f).withCentre ({ x, wr.getCentreY() });
                g.setColour (c.withAlpha (0.3f)); g.fillRoundedRectangle (kn.expanded (3.0f), 7.0f);
                g.setColour (c); g.fillRoundedRectangle (kn, 6.0f);
                g.setColour (K.bg); g.fillRect (kn.getCentreX() - 2.5f, kn.getCentreY() - 7.0f, 1.2f, 14.0f); g.fillRect (kn.getCentreX() + 1.3f, kn.getCentreY() - 7.0f, 1.2f, 14.0f);
                g.setColour (K.ink); textAt (g, juce::String ((k == 0 ? t0 : t1) * secs, 2) + "s", capFont (8.0f, Weight::SemiBold), x + (k == 0 ? 10.0f : -10.0f), wr.getY() + 12.0f, k == 0 ? juce::Justification::centredLeft : juce::Justification::centredRight, 80.0f);
            }
        if (proc.sourcePlaying())
        {
            const float x = fracToX (proc.sourcePosition() / secs);
            g.setColour (K.ink); g.fillRect (x - 1.0f, wr.getY(), 2.0f, wr.getHeight());
        }
    }
    paintOverview (g, *src);
}

// ---------------------------------------------------------------- tiles
void TileGroup::paint (juce::Graphics& g)
{
    if (S == nullptr) return;
    const auto& K = *S;
    for (int i = 0; i < (int) items.size(); ++i)
    {
        const auto r = tile (i); const bool on = i == sel, ov = i == hover;
        tileFill (g, r, 12.0f, K, on, ov);
        const auto& it = items[(size_t) i];
        const bool ghostTile = it.ghost >= 0;
        const float iconCy = r.getY() + r.getHeight() * (ghostTile ? 0.43f : 0.30f);
        if (ghostTile) icons::ghost (g, R (58.0f, 58.0f).withCentre ({ r.getCentreX(), iconCy }), it.ghost, K.a2, K.b);
        else
        {
            const float sz = it.icon == Icon::WaveBars ? 34.0f : 32.0f;
            const auto ir = R (sz, sz * (it.icon == Icon::WaveBars ? 0.72f : 1.0f)).withCentre ({ r.getCentreX(), iconCy });
            const auto c = on ? (K.light && ! K.glass ? K.a : juce::Colours::white) : K.a2.interpolatedWith (K.ink, 0.55f);
            if (on && ! K.light) { g.setColour (K.a2.withAlpha (0.35f)); drawIcon (g, it.icon, ir.expanded (2.0f), K, K.a2.withAlpha (0.25f), false); }
            drawIcon (g, it.icon, ir, K, c, false);
        }
        g.setColour (on ? (K.light ? K.ink : juce::Colours::white) : K.ink);
        textAt (g, it.name, capFont (nameCap, Weight::SemiBold, 0.03f), r.getCentreX(), r.getY() + r.getHeight() * (ghostTile ? 0.82f : 0.63f), juce::Justification::centred, r.getWidth() + 10.0f);
        if (it.caption.isNotEmpty())
        {
            g.setColour (on ? (K.light ? K.a : K.a2.brighter (0.4f)) : K.dim);
            textAt (g, it.caption, capFont (capCap, Weight::Medium, 0.06f), r.getCentreX(), r.getY() + r.getHeight() * 0.83f, juce::Justification::centred, r.getWidth() + 10.0f);
        }
    }
}

// ---------------------------------------------------------------- the looks
juce::String LookChips::getTooltip()
{
    static const char* t[] { "BLACK: the graphite look.", "PURPLE: the deep indigo look with the violet -> cyan glow (the default).", "LIGHT: the soft lilac look.", "GHOST: the liquid glass look." };
    return juce::String ("The look: ") + t[juce::jlimit (0, 3, hover >= 0 ? hover : sel)];
}

void LookChips::paint (juce::Graphics& g)
{
    if (S == nullptr) return;
    const auto& K = *S;
    static const char* names[] { "BLACK", "PURPLE", "LIGHT", "GHOST" };
    for (int i = 0; i < 4; ++i)
    {
        const auto r = chip (i); const bool on = i == sel;
        if (on) glow (g, r, 9.0f, K.a, 8.0f, 5, 0.12f);
        juce::ColourGradient gr;
        switch (i)
        {
            case 0: gr = juce::ColourGradient (juce::Colour (0xff3a3a44), r.getX(), r.getY(), juce::Colour (0xff050507), r.getX(), r.getBottom(), false); break;
            case 1: gr = juce::ColourGradient (juce::Colour (0xffc8a6ff), r.getX(), r.getY(), juce::Colour (0xff6a3fe0), r.getRight(), r.getBottom(), false); break;
            case 2: gr = juce::ColourGradient (juce::Colour (0xffffffff), r.getX(), r.getY(), juce::Colour (0xffd6cfe6), r.getX(), r.getBottom(), false); break;
            default: gr = juce::ColourGradient (juce::Colour (0xff7dd3fc), r.getX(), r.getY(), juce::Colour (0xff5eead4), r.getRight(), r.getBottom(), false); gr.addColour (0.5, juce::Colour (0xffa78bfa)); break;
        }
        g.setGradientFill (gr); g.fillRoundedRectangle (r, 9.0f);
        { juce::ColourGradient hi (juce::Colours::white.withAlpha (0.35f), r.getX(), r.getY(), juce::Colours::white.withAlpha (0.0f), r.getX(), r.getCentreY(), false); g.setGradientFill (hi); g.fillRoundedRectangle (r.reduced (2.0f), 7.0f); }
        g.setColour (on ? K.selLine : (i == hover ? K.a2.withAlpha (0.7f) : (K.light ? juce::Colours::black : juce::Colours::white).withAlpha (0.18f)));
        g.drawRoundedRectangle (r.reduced (0.5f), 9.0f, on ? 2.0f : 1.0f);
        g.setColour (on ? (K.light ? K.a : K.a2.brighter (0.3f)) : K.ink.interpolatedWith (K.dim, 0.3f));
        textAt (g, names[i], capFont (7.6f, Weight::SemiBold, 0.06f), r.getCentreX(), 54.0f, juce::Justification::centred, 90.0f);
    }
}

// ---------------------------------------------------------------- GENERATE
void GenerateButton::paintButton (juce::Graphics& g, bool over, bool)
{
    if (S == nullptr) return;
    const auto& K = *S;
    const auto pill = getLocalBounds().toFloat().reduced (15.0f, 15.0f).withTrimmedBottom (0.0f);
    const float rad = pill.getHeight() * 0.5f;
    const bool busy = proc.busy(); const float p = proc.progress();
    if (! isEnabled()) g.beginTransparencyLayer (0.45f);
    // the outer ring and glow
    g.setColour (K.a2.withAlpha (0.22f)); g.drawRoundedRectangle (pill.expanded (9.0f), rad + 9.0f, 1.2f);
    for (int i = 6; i >= 1; --i)
    {
        const float e = 1.6f * (float) i;
        juce::ColourGradient gr (K.genA.withAlpha (0.07f), pill.getX(), 0, K.genB.withAlpha (0.07f), pill.getRight(), 0, false);
        g.setGradientFill (gr); g.fillRoundedRectangle (pill.expanded (e), rad + e);
    }
    juce::ColourGradient fill (K.genA.brighter (over ? 0.08f : 0.0f), pill.getX(), pill.getY(), K.genB.brighter (over ? 0.08f : 0.0f), pill.getRight(), pill.getBottom(), false);
    fill.addColour (0.5, K.genM.brighter (over ? 0.08f : 0.0f));
    if (busy)
    {
        g.setColour (K.tile.darker (0.2f)); g.fillRoundedRectangle (pill, rad);
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (pill.withWidth (juce::jmax (rad * 2.0f, pill.getWidth() * juce::jlimit (0.0f, 1.0f, p))).toNearestInt());
        g.setGradientFill (fill); g.fillRoundedRectangle (pill, rad);
    }
    else { g.setGradientFill (fill); g.fillRoundedRectangle (pill, rad); }
    // gloss + shimmer
    { juce::ColourGradient hi (juce::Colours::white.withAlpha (0.42f), pill.getX(), pill.getY(), juce::Colours::white.withAlpha (0.0f), pill.getX(), pill.getCentreY() + 4.0f, false);
      g.setGradientFill (hi); g.fillRoundedRectangle (pill.reduced (4.0f, 3.0f), rad - 3.0f); }
    if (! busy)
    {
        const float x = pill.getX() + std::fmod (phase * 0.12f, 1.6f) * pill.getWidth() - pill.getWidth() * 0.3f;
        juce::ColourGradient sh (juce::Colours::white.withAlpha (0.0f), x, pill.getY(), juce::Colours::white.withAlpha (0.0f), x + pill.getWidth() * 0.3f, pill.getY(), false); sh.addColour (0.5, juce::Colours::white.withAlpha (0.14f));
        juce::Graphics::ScopedSaveState ss (g); juce::Path clip; clip.addRoundedRectangle (pill, rad); g.reduceClipRegion (clip); g.setGradientFill (sh); g.fillRect (pill);
    }
    g.setColour (juce::Colours::white.withAlpha (0.65f)); g.drawRoundedRectangle (pill.reduced (0.6f), rad, 1.4f);
    if (busy)
    {
        g.setColour (K.ink);
        g.setFont (capFont (10.5f, Weight::Bold, 0.08f));
        g.drawFittedText (proc.busyText() + "  " + juce::String (juce::roundToInt (p * 100.0f)) + " %", pill.toNearestInt().reduced (24, 0), juce::Justification::centred, 2);
        if (! isEnabled()) g.endTransparencyLayer();
        return;
    }
    // the play triangle with sparkles, the words
    const auto tri = R (44.0f, 44.0f).withCentre ({ pill.getX() + 62.0f, pill.getCentreY() - 1.0f });
    {
        juce::Path t; t.addTriangle (tri.getX() + 6.0f, tri.getY(), tri.getX() + 6.0f, tri.getBottom(), tri.getRight(), tri.getCentreY());
        t = t.createPathWithRoundedCorners (5.0f);
        g.setColour (juce::Colours::white.withAlpha (0.5f)); g.strokePath (t, juce::PathStrokeType (3.0f));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3b4dff), tri.getX(), tri.getY(), juce::Colour (0xff1e2a9a), tri.getRight(), tri.getBottom(), false)); g.fillPath (t);
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.fillPath (icons::star4 ({ tri.getX() - 14.0f, tri.getY() + 2.0f }, 6.0f));
        g.fillPath (icons::star4 ({ tri.getX() - 6.0f, tri.getBottom() + 4.0f }, 4.5f));
        g.fillPath (icons::star4 ({ tri.getRight() + 2.0f, tri.getY() - 6.0f }, 3.5f));
    }
    g.setColour (K.genInk);
    textAt (g, "GENERATE", capFont (22.5f, Weight::Bold, 0.07f), pill.getX() + 100.0f, pill.getCentreY() - 7.0f, juce::Justification::centredLeft, 240.0f);
    g.setColour (K.genInk.withAlpha (0.82f));
    textAt (g, proc.getSource() == nullptr ? "DROP A SOUND FIRST" : "CREATE A NEW TAKE", capFont (8.0f, Weight::SemiBold, 0.17f), pill.getX() + 101.0f, pill.getCentreY() + 22.0f, juce::Justification::centredLeft, 240.0f);
    if (! isEnabled()) g.endTransparencyLayer();
}

// ---------------------------------------------------------------- TAKES
void TakesStrip::showCurrent()
{
    const int c = proc.currentIndex(), v = visibleCount();
    if (c < first) first = c; else if (c >= first + v) first = c - v + 1;
    first = juce::jlimit (0, juce::jmax (0, (int) proc.takes().size() - v), first);
}

const std::vector<float>& TakesStrip::miniOf (const gen::Take& t)
{
    auto it = minis.find (&t);
    if (it != minis.end()) return it->second;
    if (minis.size() > 40) minis.clear();
    std::vector<float> v (64, 0.0f);
    const auto& b = t.preview; const int n = b.getNumSamples();
    float pk = 1e-6f;
    if (n > 0)
    {
        // the first few seconds (or all of it): 64 bars
        const int span = juce::jmin (n, (int) (t.rate * 6.0));
        for (int i = 0; i < 64; ++i)
        {
            const int a = i * span / 64, e = juce::jmax (a + 1, (i + 1) * span / 64);
            float m = 0; for (int c = 0; c < b.getNumChannels(); ++c) m = std::max (m, b.getMagnitude (c, a, e - a));
            v[(size_t) i] = m; pk = std::max (pk, m);
        }
        for (int i = 0; i < 64; ++i)
        {   // drawn like a waveform: sharp, uneven spikes under the take's own loudness
            const float t = (float) i / 63.0f, env = std::pow (std::sin (t * juce::MathConstants<float>::pi), 0.6f);
            const uint32_t hsh = (uint32_t) i * 2654435761u; const float jit = 0.35f + 0.65f * (float) ((hsh >> 16) & 255) / 255.0f;
            v[(size_t) i] = juce::jlimit (0.04f, 1.0f, (0.35f + 0.65f * v[(size_t) i] / pk) * env * jit);
        }
    }
    return minis[&t] = v;
}

juce::String TakesStrip::getTooltip()
{
    for (auto& h : hits)
    {
        if (h.star.contains (mouse)) return "Star it: keep track of the takes you like.";
        if (h.del.contains (mouse)) return "Delete this take.";
        if (h.chip.contains (mouse))
        {
            const auto& sl = proc.takes()[(size_t) h.index];
            juce::String s = "Click to hear it and make it the take your keys play. Right-click: star or delete.";
            if (sl.take != nullptr && sl.take->info.isNotEmpty()) s = sl.take->info + "\n" + s;
            return s;
        }
    }
    return "Your takes: every GENERATE adds one. Click one to hear it and play it.";
}

void TakesStrip::paint (juce::Graphics& g)
{
    hits.clear();
    if (S == nullptr) return;
    const auto& K = *S;
    const auto& T = proc.takes();
    if (T.empty())
    {
        g.setColour (K.dim);
        textAt (g, proc.getSource() == nullptr ? "Your takes appear here." : "Your takes appear here. Press GENERATE.", capFont (9.5f, Weight::Medium), (float) getWidth() * 0.5f, (float) getHeight() * 0.5f, juce::Justification::centred, (float) getWidth());
        return;
    }
    first = juce::jlimit (0, juce::jmax (0, (int) T.size() - visibleCount()), first);
    for (int k = 0; k < visibleCount() && first + k < (int) T.size(); ++k)
    {
        const int i = first + k;
        const auto chip = juce::Rectangle<int> (5 + juce::roundToInt ((float) k * 190.5f), 5, 177, 97);
        const auto cf = chip.toFloat();
        const bool cur = i == proc.currentIndex(), hov = chip.contains (mouse);
        if (cur)
        {
            glow (g, cf, 12.0f, K.a, 8.0f, 5, 0.10f);
            g.setGradientFill (juce::ColourGradient (K.selTop.withMultipliedAlpha (0.55f), cf.getX(), cf.getY(), K.selBot.withMultipliedAlpha (0.45f), cf.getX(), cf.getBottom(), false)); g.fillRoundedRectangle (cf, 12.0f);
            g.setColour (K.tile.withMultipliedAlpha (0.5f)); g.fillRoundedRectangle (cf, 12.0f);
            g.setColour (K.selLine); g.drawRoundedRectangle (cf.reduced (0.8f), 12.0f, 1.6f);
        }
        else tileFill (g, cf, 12.0f, K, false, hov);
        const auto& s = T[(size_t) i];
        const auto type = proc.slotType (i);
        const juce::String nm = "Take " + juce::String (s.index) + (s.recipe.variation > 0 ? juce::String::charToString ((juce::juce_wchar) ('a' + s.recipe.variation)) : juce::String());
        g.setColour (K.ink); textAt (g, nm, capFont (11.0f, Weight::SemiBold), cf.getX() + 18.0f, cf.getY() + 20.0f, juce::Justification::centredLeft, 120.0f);
        g.setColour (K.ink.interpolatedWith (K.a2, 0.25f)); textAt (g, gen::typeName (type), capFont (9.8f, Weight::Medium, 0.03f), cf.getX() + 18.0f, cf.getY() + 44.0f, juce::Justification::centredLeft, 150.0f);
        g.setColour (K.dim); textAt (g, proc.slotKey (i), capFont (9.8f, Weight::Medium, 0.03f), cf.getX() + 18.0f, cf.getY() + 68.0f, juce::Justification::centredLeft, 80.0f);
        {   // build 6: the melody style (the take's own, AUTO's pick when it has rendered)
            const auto st = s.take != nullptr && s.take->style != gen::Style::Auto ? s.take->style : s.recipe.style;
            if (st != gen::Style::Auto || s.take != nullptr)
            {
                const float x0 = cf.getX() + 18.0f + widthOf (capFont (11.0f, Weight::SemiBold), nm) + 8.0f;
                g.setColour (K.light ? K.a : K.a2.interpolatedWith (K.dim, 0.3f));
                g.setFont (capFont (7.0f, Weight::SemiBold, 0.04f));
                g.drawFittedText (gen::styleName (st), R (x0, cf.getY() + 13.0f, juce::jmax (10.0f, cf.getRight() - 34.0f - x0), 14.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
            }
        }
        // the mini waveform
        const auto wr = R (cf.getX() + 72.0f, cf.getY() + 50.0f, 96.0f, 40.0f);
        juce::Colour c0 = K.a2, c1 = K.a;
        switch (type) { case gen::Type::Instrument: c0 = K.b; c1 = K.b2; break; case gen::Type::Texture: c0 = K.b2; c1 = K.b; break; case gen::Type::Chops: c0 = K.a; c1 = K.a2; break; default: break; }
        if (s.take != nullptr)
        {
            const auto& v = miniOf (*s.take);
            juce::Path p; const int n = (int) v.size(); const float dx = wr.getWidth() / (float) n;
            p.startNewSubPath (wr.getX(), wr.getCentreY());
            for (int j = 0; j < n; ++j) { const float h = juce::jmax (0.6f, v[(size_t) j] * wr.getHeight() * 0.5f); p.lineTo (wr.getX() + (float) j * dx + dx * 0.5f, wr.getCentreY() - h); p.lineTo (wr.getX() + (float) j * dx + dx, wr.getCentreY() - h * 0.25f); }
            p.lineTo (wr.getRight(), wr.getCentreY());
            for (int j = n; j-- > 0;) { const float h = juce::jmax (0.6f, v[(size_t) j] * wr.getHeight() * 0.5f * 0.85f); p.lineTo (wr.getX() + (float) j * dx + dx, wr.getCentreY() + h * 0.25f); p.lineTo (wr.getX() + (float) j * dx + dx * 0.5f, wr.getCentreY() + h); }
            p.closeSubPath();
            g.setGradientFill (juce::ColourGradient (c0.withAlpha (0.3f), wr.getX(), 0, c1.withAlpha (0.3f), wr.getRight(), 0, false)); g.strokePath (p, juce::PathStrokeType (2.5f));
            g.setGradientFill (juce::ColourGradient (c0, wr.getX(), 0, c1.brighter (0.2f), wr.getRight(), 0, false)); g.fillPath (p);
            g.setColour (c1.withAlpha (0.7f)); g.fillRect (wr.getX(), wr.getCentreY() - 0.5f, wr.getWidth(), 1.0f);
        }
        else
        {
            g.setColour (K.dim); textAt (g, proc.busy() && cur ? "making..." : "click to remake", capFont (8.0f, Weight::Medium), wr.getCentreX() + 6.0f, wr.getCentreY(), juce::Justification::centred, 110.0f);
        }
        // star, delete (on hover)
        const auto star = juce::Rectangle<int> (chip.getRight() - 34, chip.getY() + 8, 26, 26);
        juce::Path st; st.addStar (star.toFloat().getCentre(), 5, 4.2f, 10.0f, 0.0f);
        if (s.recipe.favourite) { g.setColour (K.star.withAlpha (0.3f)); g.strokePath (st, juce::PathStrokeType (4.0f)); g.setColour (K.star); g.fillPath (st); }
        else { g.setColour (star.contains (mouse) ? K.ink : K.ink.interpolatedWith (K.dim, 0.3f)); g.strokePath (st, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }
        const auto del = juce::Rectangle<int> (chip.getRight() - 60, chip.getY() + 11, 20, 20);
        if (hov)
        {
            g.setColour (del.contains (mouse) ? K.bad : K.dim);
            icons::cross (g, del.toFloat().reduced (5.5f), 1.6f);
        }
        if (cur && proc.previewPlaying())
        {
            const float pp = proc.previewPosition();
            g.setColour (K.b); g.fillRoundedRectangle (cf.getX() + 12.0f, cf.getBottom() - 6.0f, (cf.getWidth() - 24.0f) * pp, 2.5f, 1.2f);
        }
        hits.push_back ({ chip, star, hov ? del : juce::Rectangle<int>(), i });
    }
}

void TakesStrip::mouseDown (const juce::MouseEvent& e)
{
    for (auto& h : hits)
    {
        if (! h.chip.contains (e.getPosition())) continue;
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu m;
            const bool fav = proc.takes()[(size_t) h.index].recipe.favourite;
            m.addItem (1, fav ? "Unstar" : "Star"); m.addItem (2, "Delete this take");
            const int idx = h.index;
            m.showMenuAsync (juce::PopupMenu::Options(), [sp = juce::Component::SafePointer<TakesStrip> (this), idx] (int r)
            {
                if (sp == nullptr) return;
                if (r == 1) sp->proc.toggleFavourite (idx); else if (r == 2) sp->proc.deleteTake (idx);
            });
            return;
        }
        if (h.star.contains (e.getPosition())) { proc.toggleFavourite (h.index); return; }
        if (h.del.contains (e.getPosition())) { proc.deleteTake (h.index); return; }
        if (h.index == proc.currentIndex() && proc.currentTake() != nullptr) proc.playPreview (! proc.previewPlaying()); else proc.selectTake (h.index);
        return;
    }
}

// ---------------------------------------------------------------- the big tiles
static void paintBigTile (juce::Graphics& g, R r, const Skin& K, bool accent, bool over, bool down, bool en, Icon icon, const juce::String& text)
{
    if (! en) g.beginTransparencyLayer (0.42f);
    if (accent)
    {
        glow (g, r, 12.0f, K.a, 10.0f, 6, 0.10f);
        g.setGradientFill (juce::ColourGradient (K.selTop.brighter (over ? 0.15f : 0.0f).withMultipliedAlpha (0.75f), r.getX(), r.getY(), K.selBot.brighter (over ? 0.15f : 0.0f).withMultipliedAlpha (0.7f), r.getX(), r.getBottom(), false)); g.fillRoundedRectangle (r, 12.0f);
        { juce::ColourGradient hi (juce::Colours::white.withAlpha (0.22f), r.getX(), r.getY(), juce::Colours::white.withAlpha (0.0f), r.getX(), r.getCentreY(), false); g.setGradientFill (hi); g.fillRoundedRectangle (r, 12.0f); }
        g.setColour (K.selLine); g.drawRoundedRectangle (r.reduced (0.8f), 12.0f, 1.6f);
    }
    else tileFill (g, r, 12.0f, K, false, over, en);
    if (down) { g.setColour (juce::Colours::black.withAlpha (0.12f)); g.fillRoundedRectangle (r, 12.0f); }
    const float isz = icon == Icon::Play || icon == Icon::Stop ? 22.0f : 28.0f;
    const auto ir = R (isz, isz).withCentre ({ r.getCentreX() + (icon == Icon::Play ? 2.0f : 0.0f), r.getY() + r.getHeight() * 0.38f });
    const auto accInk = K.light ? K.ink : juce::Colours::white;
    if (accent) drawIcon (g, icon, ir, K, K.light ? K.a : juce::Colours::white, false);
    else if (icon == Icon::Download) drawIcon (g, icon, ir, K, K.ink, false);
    else
    {
        g.setGradientFill (juce::ColourGradient (icon == Icon::Notes ? K.b : K.a2, ir.getX(), ir.getY(), icon == Icon::Notes ? K.a2 : K.b2, ir.getRight(), ir.getBottom(), false));
        if (icon == Icon::WaveBars) icons::waveBars (g, ir.withSizeKeepingCentre (ir.getWidth(), ir.getHeight() * 0.9f), 9, 0.42f);
        else if (icon == Icon::Notes) icons::notes (g, ir.reduced (2.0f), 2.2f);
        else drawIcon (g, icon, ir, K, K.ink, false);
    }
    g.setColour (accent ? accInk : K.ink);
    textAt (g, text, capFont (10.5f, Weight::SemiBold, 0.04f), r.getCentreX(), r.getY() + r.getHeight() * 0.75f, juce::Justification::centred, r.getWidth() + 10.0f);
    if (! en) g.endTransparencyLayer();
}

void IconTile::paintButton (juce::Graphics& g, bool over, bool down)
{
    if (S == nullptr) return;
    paintBigTile (g, getLocalBounds().toFloat().reduced (1.0f), *S, accent, over, down, isEnabled(), icon, getButtonText());
}

void DragPill::paint (juce::Graphics& g)
{
    if (S == nullptr) return;
    paintBigTile (g, getLocalBounds().toFloat().reduced (1.0f), *S, false, over && isEnabled(), false, isEnabled(), icon, text);
}

// ---------------------------------------------------------------- the keys
juce::Colour GhostKeys::litColour (int note) const
{
    const float t = juce::jlimit (0.0f, 1.0f, (float) (note - kLowKey) / (float) (kHighKey - kLowKey) * 1.7f - 0.2f);
    return S->at (t);
}

void GhostKeys::drawWhiteNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool down, bool over, juce::Colour, juce::Colour)
{
    if (S == nullptr) return;
    const auto& K = *S;
    auto r = area.reduced (1.0f, 0.0f).withTrimmedBottom (1.0f);
    juce::Path p; p.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), 3.5f, 3.5f, false, false, true, true);
    const bool on = lit[(size_t) note];
    if (note == flash) down = true;   // (a heard CHOPS part: its key flashes)
    if (down || on)
    {
        const auto c = down ? (note == flash ? K.b : K.a) : litColour (note);
        g.setGradientFill (juce::ColourGradient (c, r.getX(), r.getY(), c.brighter (0.5f).withMultipliedSaturation (0.7f), r.getX(), r.getBottom(), false)); g.fillPath (p);
        g.setColour (juce::Colours::white.withAlpha (0.25f)); g.fillRect (r.getX() + 2.0f, r.getBottom() - 10.0f, r.getWidth() - 4.0f, 2.0f);
    }
    else
    {
        g.setGradientFill (juce::ColourGradient (K.keyW1, r.getX(), r.getY(), K.keyW2, r.getX(), r.getBottom(), false)); g.fillPath (p);
        if (over) { g.setColour (K.a2.withAlpha (0.25f)); g.fillPath (p); }
    }
    if (note % 12 == 0)
    {
        g.setColour (on || down ? juce::Colours::white : juce::Colour (0xff3b3556));
        textAt (g, getWhiteNoteText (note), capFont (7.0f, Weight::Medium), r.getX() + 5.0f, r.getBottom() - 11.0f, juce::Justification::centredLeft, 40.0f);
    }
}

void GhostKeys::drawBlackNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool down, bool over, juce::Colour)
{
    if (S == nullptr) return;
    const auto& K = *S;
    auto r = area.reduced (1.0f, 0.0f);
    juce::Path p; p.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), 3.0f, 3.0f, false, false, true, true);
    const bool on = lit[(size_t) note];
    if (note == flash) down = true;
    if (down || on)
    {
        const auto c = down ? (note == flash ? K.b : K.a) : litColour (note);
        g.setGradientFill (juce::ColourGradient (c.darker (0.2f), r.getX(), r.getY(), c, r.getX(), r.getBottom(), false)); g.fillPath (p);
    }
    else
    {
        g.setGradientFill (juce::ColourGradient (K.keyB1, r.getX(), r.getY(), K.keyB2, r.getX(), r.getBottom(), false)); g.fillPath (p);
        g.setColour (juce::Colours::white.withAlpha (0.10f)); g.fillRoundedRectangle (r.getX() + 2.5f, r.getBottom() - 9.0f, r.getWidth() - 5.0f, 5.0f, 2.0f);
        if (over) { g.setColour (K.a2.withAlpha (0.35f)); g.fillPath (p); }
    }
}

// ---------------------------------------------------------------- VELOCITY / SCALE / toast
void VelocityMeter::paint (juce::Graphics& g)
{
    if (S == nullptr) return;
    const auto& K = *S;
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (K.well); g.fillRoundedRectangle (r, 8.0f);
    g.setColour (K.panelLine.withAlpha (0.6f)); g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    const auto bar = R (r.getCentreX() - 9.0f, r.getY() + 6.0f, 18.0f, r.getHeight() - 12.0f);
    const int n = 14; const float sh = bar.getHeight() / (float) n;
    for (int i = 0; i < n; ++i)
    {
        const float t = (float) (i + 1) / (float) n;
        const auto seg = R (bar.getX(), bar.getBottom() - sh * (float) (i + 1) + 1.0f, bar.getWidth(), sh - 2.0f);
        const auto c = K.a.interpolatedWith (K.a2, t * 0.6f);
        if (t <= value + 1e-3f) { g.setColour (c); g.fillRoundedRectangle (seg, 1.0f); }
        else { g.setColour (c.withAlpha (0.28f)); g.fillRoundedRectangle (seg, 1.0f); }
    }
    g.setColour (K.panelLine.withAlpha (0.5f)); g.fillRect (bar.getRight() + 4.0f, bar.getY(), 1.0f, bar.getHeight());
}

ScaleBox::ScaleBox()
{
    for (auto* b : { &lockBtn, &prevBtn, &nextBtn }) addAndMakeVisible (b);
    lockBtn.iconSize = 14.0f; prevBtn.iconSize = nextBtn.iconSize = 10.0f;
    lockBtn.setTooltip ("Lock the key: it stays when you load a new sound. Unlocked, a new sound starts on its own key (AUTO).");
    prevBtn.setTooltip ("The previous key (one semitone down, same major / minor).");
    nextBtn.setTooltip ("The next key (one semitone up, same major / minor).");
}
void ScaleBox::resized() { lockBtn.setBounds (94, 10, 24, 24); prevBtn.setBounds (6, 42, 24, 24); nextBtn.setBounds (94, 42, 24, 24); }
void ScaleBox::paint (juce::Graphics& g)
{
    if (S == nullptr) return;
    const auto& K = *S;
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (K.well); g.fillRoundedRectangle (r, 8.0f);
    g.setColour (K.panelLine.withAlpha (0.6f)); g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    g.setColour (K.light ? K.a : K.a2.brighter (0.2f)); textAt (g, "KEY", capFont (8.2f, Weight::SemiBold, 0.06f), 18.0f, 22.0f);
    g.setColour (K.ink); textAt (g, keyText, capFont (8.0f, Weight::Medium, 0.04f), r.getCentreX(), 54.0f, juce::Justification::centred, 70.0f);
}

void Toast::paint (juce::Graphics& g)
{
    if (S == nullptr || text.isEmpty()) return;
    const auto& K = *S;
    const auto f = capFont (9.0f, Weight::Medium);
    const float w = juce::jmin ((float) getWidth() - 4.0f, widthOf (f, text) + 40.0f);
    const auto r = R (w, (float) getHeight() - 4.0f).withCentre (getLocalBounds().toFloat().getCentre());
    g.setColour (K.light ? juce::Colour (0xf2ffffff) : juce::Colour (0xf0120f2c)); g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (K.b.withAlpha (0.7f)); g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f, 1.2f);
    g.setColour (K.ink); g.setFont (f); g.drawFittedText (text, r.toNearestInt().reduced (16, 0), juce::Justification::centred, 1, 0.7f);
}

// ---------------------------------------------------------------- the canvas
void Canvas::paint (juce::Graphics& g)
{
    const float sc = juce::jlimit (0.25f, 8.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (cache.isNull() || std::abs (cacheScale - sc) > 1e-3f || cacheLook != look)
    {
        cache = juce::Image (juce::Image::RGB, juce::jmax (1, juce::roundToInt ((float) kDesignW * sc)), juce::jmax (1, juce::roundToInt ((float) kDesignH * sc)), true);
        juce::Graphics ig (cache);
        ig.addTransform (juce::AffineTransform::scale ((float) cache.getWidth() / (float) kDesignW, (float) cache.getHeight() / (float) kDesignH));
        if (paintStatic) paintStatic (ig);
        cacheScale = sc; cacheLook = look;
    }
    g.drawImageTransformed (cache, juce::AffineTransform::scale ((float) kDesignW / (float) cache.getWidth(), (float) kDesignH / (float) cache.getHeight()));
    if (paintLive) paintLive (g);
}

// ================================================================ the editor
SliceEditor::SliceEditor (SliceProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (canvas);
    canvas.paintStatic = [this] (juce::Graphics& g) { paintStatic (g); };
    canvas.paintLive = [this] (juce::Graphics& g) { paintLive (g); };
    auto add = [this] (juce::Component& c, const juce::String& tip)
    {
        canvas.addAndMakeVisible (c);
        if (auto* t = dynamic_cast<juce::SettableTooltipClient*> (&c)) t->setTooltip (tip);
    };
    using namespace gslice::ui;
    // header
    keyBtn.label = "KEY"; tempoBtn.label = "TEMPO";
    add (keyBtn, "KEY: AUTO uses the key found in your sound. Click to pick another one.");
    keyBtn.onClick = [this] { keyMenu(); };
    add (tempoBtn, "TEMPO: the DAW's tempo when it plays; in the app, click to set it.");
    tempoBtn.onClick = [this] { tempoMenu(); };
    add (looks, {});
    looks.onChange = [this] (int l) { setLook (l); };
    add (gearBtn, "Settings: where RECORD records from (AUTO / TRACK / MAC MIC), CLEAR, the key lock, TRIM, about Ghost Slice.");
    gearBtn.iconSize = 24.0f;
    gearBtn.onClick = [this] { settingsMenu(); };
    add (unlock, "UNLOCK: enter your licence key (the demo works fully but cannot export, and drops out for 2 s every 20 s).");
    unlock.onChange = [this] { proc.unlockedNow(); refresh(); };
    // the SOUND panel
    add (stage, "Drop a vocal, a sentence or any sound here. Ghost Slice listens to it and turns it into instruments, pads, chops and textures.");
    stage.onFile = [this] (const juce::File& f) { proc.loadAudioAsync (f); };
    stage.setWantsKeyboardFocus (true); setWantsKeyboardFocus (true);
    stage.onHear = [this] (int i)
    {   // CHOPS: the heard part's key flashes (MIDI 36 up)
        if (proc.slotType (proc.currentIndex()) != gen::Type::Chops) return;
        keys.flash = gen::Take::kChopNote + i; flashUntil = juce::Time::getMillisecondCounterHiRes() + 450.0; keys.repaint();
    };
    stage.loadBtn.onClick = [this] { loadClicked(); };
    stage.recordBtn.onClick = [this] { recordClicked(); };
    stage.playSrcBtn.onClick = [this] { proc.playSource (! proc.sourcePlaying()); stage.repaint(); };
    stage.trimBtn.onStateChange = [this]
    {   // right-click: the whole sound again
        if (stage.trimBtn.isDown() && juce::ModifierKeys::currentModifiers.isPopupMenu())
        {
            juce::PopupMenu m; m.addItem (1, "Use the whole sound (reset TRIM)", proc.isTrimmed());
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&stage.trimBtn), [sp = juce::Component::SafePointer<SliceEditor> (this)] (int r)
            { if (sp != nullptr && r == 1) { sp->proc.setTrim (0.0, 1.0); sp->stage.dragT0 = 0.0; sp->stage.dragT1 = 1.0; sp->stage.repaint(); } });
        }
    };
    // TAKE TYPE / CHARACTER
    add (typeTiles, {});
    typeTiles.items = { { "AUTO", "ALL STYLES", kTypeTips[0], Icon::Sparkle }, { "INSTRUMENT", "ONE VOICE", kTypeTips[1], Icon::WaveBars },
                        { "PAD", "GLASSY", kTypeTips[2], Icon::Cloud }, { "CHOPS", "CALL & ANSWER", kTypeTips[3], Icon::Chops }, { "TEXTURE", "DRONE", kTypeTips[4], Icon::Waves } };
    typeTiles.tileW = 117.0f; typeTiles.gap = 12.0f;
    typeTiles.onChange = [this] (int i) { proc.type = (gen::Type) i; refresh(); };
    add (charTiles, {});
    charTiles.items = { { "NATURAL", "", kCharTips[0], Icon::None, 0 }, { "CHIPMUNK", "", kCharTips[1], Icon::None, 1 }, { "DEEP", "", kCharTips[2], Icon::None, 2 } };
    charTiles.tileW = 112.0f; charTiles.gap = 10.5f; charTiles.nameCap = 9.5f;
    charTiles.onChange = [this] (int i) { proc.character = (gen::Character) i; };
    add (clearPill, "CLEAR: keeps your sound as clean and true as possible: a real sample on every key, no wobble or chorus, no grit. Turn it off for the dreamier, blurrier sound.");
    clearPill.onClick = [this] { toggleClear(); };
    // NATURAL <-> MUSICAL, TONE, SPACE
    add (musicalSl, "NATURAL <-> MUSICAL. Left: the voice keeps its own pitches and character. Right: everything snaps to your key and chords, more processed.");
    musicalSl.setRange (0.0, 1.0, 0.01); musicalSl.onValueChange = [this] { if (! refreshing) proc.musical = musicalSl.getValue(); };
    musicalSl.setDoubleClickReturnValue (true, 0.7);
    add (toneKnob, "TONE: dark (left) to bright (right). Double-click: the middle.");
    toneKnob.setRange (-1.0, 1.0, 0.01); toneKnob.setDoubleClickReturnValue (true, 0.0); toneKnob.onValueChange = [this] { if (! refreshing) proc.setTone ((float) toneKnob.getValue()); };
    add (spaceKnob, "SPACE: how much of the lush reverb, from DRY to HUGE. Double-click: the default.");
    spaceKnob.setRange (0.0, 1.0, 0.01); spaceKnob.setDoubleClickReturnValue (true, 0.4);
    spaceKnob.onValueChange = [this] { if (! refreshing) proc.setSpace ((float) spaceKnob.getValue()); knobLabels.repaint(); };
    // build 6: the type's own knob (VOICE / DREAM / STUTTER / DRIFT): a drag's end re-renders the take on screen (same idea)
    add (typeKnob, {});
    typeKnob.setRange (0.0, 1.0, 0.01);
    typeKnob.onValueChange = [this] { if (! refreshing) proc.setKnob (typeKnob.getValue(), ! typeKnob.isMouseButtonDown()); knobLabels.repaint(); };
    typeKnob.onDragEnd = [this] { proc.setKnob (typeKnob.getValue(), true); };
    for (auto* k : { &toneKnob, &spaceKnob, &typeKnob }) { k->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true); k->setMouseDragSensitivity (180); }
    // build 6: MELODY (the style) and UNDER THE HOOD (the second page) in the TAKE TYPE title row
    melodyBtn.label = "STYLE"; melodyBtn.menu = true;
    add (melodyBtn, {});
    melodyBtn.onClick = [this] { showBrowser (true); };
    add (stylePrev, "The previous style (the take on screen is remade in it)."); add (styleNext, "The next style (the take on screen is remade in it).");
    stylePrev.iconSize = styleNext.iconSize = 11.0f;
    stylePrev.onClick = [this] { proc.stepStyle (-1); refresh(); };
    styleNext.onClick = [this] { proc.stepStyle (1); refresh(); };
    lengthBtn.label = "LENGTH"; lengthBtn.menu = true;
    add (lengthBtn, "LENGTH: how many bars the melody, chords or phrase last (1, 2, 4, 8 or 16). Longer ones develop, they never just loop.");
    lengthBtn.onClick = [this] { lengthMenu(); };
    canvas.addChildComponent (browser);
    browser.onDone = [this] { showBrowser (false); };
    browser.onPick = [this] (gen::Style st) { proc.setStyle (st, false); proc.auditionStyle (st); refresh(); };
    hoodBtn.label = "UNDER THE"; hoodBtn.value = "HOOD"; hoodBtn.chevron = true;
    add (hoodBtn, "UNDER THE HOOD: a second page that shows what this take type is actually doing (the parts of your sound, each step, the notes and chords) with its deeper knobs.");
    hoodBtn.onClick = [this] { showHood (true); };
    canvas.addChildComponent (hood);   // (UNDER THE HOOD; the STYLE browser goes on top of it)
    hood.onBack = [this] { showHood (false); };
    hood.onStyleMenu = [this] { showBrowser (true); };
    hood.onHearPart = [this] (int i) { stage.hoverBox = i; stage.hearBox (i); };
    // GENERATE / VARY
    add (generateBtn, "GENERATE: make a new take from your sound. Every press is different. Each take is kept under TAKES.");
    generateBtn.onClick = [this] { proc.generate(); strip.showCurrent(); };
    add (varyBtn, "VARY: a variation of the take you are on (the same idea, new details).");
    varyBtn.textCap = 14.0f; varyBtn.iconSize = 30.0f;
    varyBtn.onClick = [this] { proc.vary(); strip.showCurrent(); };
    // TAKES
    add (strip, {});
    add (prevBtn, "The previous take."); add (nextBtn, "The next take.");
    prevBtn.iconSize = nextBtn.iconSize = 12.0f;
    prevBtn.onClick = [this] { proc.stepTake (-1); strip.showCurrent(); };
    nextBtn.onClick = [this] { proc.stepTake (1); strip.showCurrent(); };
    add (playBtn, "PLAY: hear the take (its melody, chords or phrase). Your MIDI keys play it too.");
    playBtn.accent = true;
    playBtn.onClick = [this] { proc.playPreview (! proc.previewPlaying()); };
    dragWav.text = "DRAG WAV"; dragWav.icon = Icon::WaveBars;
    add (dragWav, "Drag the take into FL Studio (or Finder) as a WAV, with the TONE and SPACE you hear.");
    dragWav.onDragOut = [this] { dragWavOut(); };
    dragMidi.text = "DRAG MIDI"; dragMidi.icon = Icon::Notes;
    add (dragMidi, "Drag the melody, chords or phrase of this take as MIDI.");
    dragMidi.onDragOut = [this] { dragMidiOut(); };
    add (saveBtn, "SAVE TAKE: the take (WAV + MIDI) into Documents/Ghosty/Ghost Slice/Takes.");
    saveBtn.onClick = [this] { saveTakeClicked(); };
    // the keys, VELOCITY, SCALE
    add (keys, "Play the take here, or from your MIDI keyboard / FL Studio's piano roll.");
    keys.setAvailableRange (kLowKey, kHighKey); keys.setLowestVisibleKey (kLowKey); keys.setOctaveForMiddleC (5);   // (FL Studio names: middle C = C5)
    keys.setScrollButtonsVisible (false); keys.setVelocity (0.8f, true);
    keys.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour (0xff0c0a1c));
    keys.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colours::transparentBlack);
    keys.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
    add (velocity, "VELOCITY: how hard the last note was played (your MIDI keys, or where you click on a key here: lower = harder).");
    add (scaleBox, {});
    scaleBox.lockBtn.onClick = [this] { proc.keyLock = ! proc.keyLock; toast (proc.keyLock ? "KEY locked: it stays when you load a new sound." : "KEY unlocked: a new sound starts on its own key."); refresh(); };
    scaleBox.prevBtn.onClick = [this] { stepKey (-1); };
    scaleBox.nextBtn.onClick = [this] { stepKey (1); };
    canvas.addAndMakeVisible (knobLabels); knobLabels.setInterceptsMouseClicks (false, false);
    hood.toFront (false);
    knobLabels.paintFn = [this] (juce::Graphics& g) { paintKnobLabels (g); };
    canvas.addChildComponent (toastView);
    proc.onChanged = [this] { refresh(); };
    proc.onToast = [this] (const juce::String& s) { toast (s); };
    setLook (proc.uiTheme.load());
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) gslice::ui::kDesignW / (double) gslice::ui::kDesignH);
    setResizeLimits (812, 586, 2216, 1598);
    setSize (1108, 799);
    refresh();
    startTimerHz (30);
}

SliceEditor::~SliceEditor()
{
    stopTimer();
    proc.onChanged = nullptr; proc.onToast = nullptr;
    setLookAndFeel (nullptr);
}

void SliceEditor::setLook (int l)
{
    l = juce::jlimit (0, kLooks - 1, l);
    shownLook = l; proc.uiTheme.store (l);
    const bool ghost = l == kLookGhost;
    P = gslice::ui::palette (ghost ? ghosty::themes::Theme::Light : ghosty::themes::fromInt (l), ghost);
    S = gslice::ui::skinFor (l);
    lnf.set (P); lnf.S = S;
    lnf.setColour (juce::PopupMenu::backgroundColourId, P.card);
    for (auto* b : { &keyBtn, &tempoBtn, &gearBtn, &varyBtn, &prevBtn, &nextBtn, &stage.loadBtn, &stage.recordBtn, &stage.zoomInBtn, &stage.zoomOutBtn, &stage.fitBtn, &stage.playSrcBtn, &stage.trimBtn,
                     &scaleBox.lockBtn, &scaleBox.prevBtn, &scaleBox.nextBtn })
        b->S = &S;
    hood.setSkin (&S); melodyBtn.S = &S; hoodBtn.S = &S; lengthBtn.S = &S; stylePrev.S = &S; styleNext.S = &S;
    browser.S = &S; browser.doneBtn.S = &S;
    browser.search.setColour (juce::TextEditor::backgroundColourId, S.well); browser.search.setColour (juce::TextEditor::textColourId, S.ink);
    browser.search.setColour (juce::TextEditor::outlineColourId, S.panelLine); browser.search.setColour (juce::TextEditor::focusedOutlineColourId, S.a2);
    stage.P = P; stage.S = &S; typeTiles.S = &S; charTiles.S = &S; clearPill.S = &S; looks.S = &S; looks.sel = l;
    generateBtn.P = P; generateBtn.S = &S; strip.P = P; strip.S = &S; playBtn.S = &S; saveBtn.S = &S;
    dragWav.P = P; dragWav.S = &S; dragMidi.P = P; dragMidi.S = &S; keys.S = &S; velocity.S = &S; scaleBox.S = &S; toastView.S = &S;
    canvas.look = l; canvas.invalidate();
    sendLookAndFeelChange();
    repaint();
}

void SliceEditor::showHood (bool on)
{
    if (on) { hood.refresh(); hood.setVisible (true); hood.toFront (false); toastView.toFront (false); }
    else { hood.setVisible (false); stage.hoverBox = -1; stage.repaint(); }
    hoodBtn.repaint();
}

void SliceEditor::showBrowser (bool on)
{
    if (on) { styleOnOpen = proc.style; browser.rebuild(); browser.setVisible (true); browser.toFront (false); toastView.toFront (false); }
    else
    {
        browser.setVisible (false); proc.stopAudition();
        if (proc.style != styleOnOpen) { proc.setStyle (proc.style, true); toast (juce::String ("STYLE: ") + gen::styleName (proc.style) + (proc.currentTake() != nullptr ? ". The take is being remade in it." : ". Press GENERATE.")); }
    }
    refresh();
}

void SliceEditor::lengthMenu()
{
    juce::PopupMenu m;
    for (int b : { 1, 2, 4, 8, 16 }) m.addItem (b, juce::String (b) + (b == 1 ? " bar" : " bars") + (b == 4 ? "  (the default)" : b == 16 ? "  (a whole section: it develops)" : ""), true, proc.lengthBars == b);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&lengthBtn), [sp = juce::Component::SafePointer<SliceEditor> (this)] (int r)
    {
        if (sp == nullptr || r <= 0) return;
        sp->proc.setLength (r, true);
        sp->toast ("LENGTH: " + juce::String (r) + (r == 1 ? " bar" : " bars") + (sp->proc.currentTake() != nullptr ? ". The take is being remade." : ". Press GENERATE."));
        sp->refresh();
    });
}

void SliceEditor::styleMenu (juce::Component* target)
{
    juce::PopupMenu m;
    for (int i = 0; i < gen::kStyles; ++i)
    {
        const auto st = (gen::Style) i;
        if (st == gen::Style::Voice) continue;   // (AUTO picks it at NATURAL: the sound's own melody)
        m.addItem (i + 1, juce::String (gen::styleName (st)) + "  -  " + gen::styleTip (st), true, proc.style == st);
    }
    m.addItem ((int) gen::Style::Voice + 1, juce::String ("VOICE  -  ") + gen::styleTip (gen::Style::Voice), true, proc.style == gen::Style::Voice);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [sp = juce::Component::SafePointer<SliceEditor> (this)] (int r)
    {
        if (sp == nullptr || r <= 0) return;
        sp->proc.setStyle ((gen::Style) (r - 1), true);
        sp->toast (juce::String ("MELODY: ") + gen::styleName ((gen::Style) (r - 1)) + (sp->proc.currentTake() != nullptr ? ". The take is being remade in it." : ". Press GENERATE."));
        sp->refresh();
    });
}

void SliceEditor::toast (const juce::String& s)
{
    toastText = s; toastUntil = juce::Time::getMillisecondCounterHiRes() + 5000.0;
    toastView.text = s; toastView.setVisible (s.isNotEmpty()); toastView.toFront (false); toastView.repaint();
}

juce::String SliceEditor::keyText() const
{
    if (proc.keyTonic >= 0) return keyName (proc.keyTonic, proc.keyMinor);
    const auto k = proc.detectedKey();
    return proc.keyKnown() ? keyName (k.tonic, k.minor) : juce::String ("AUTO");
}

void SliceEditor::refresh()
{
    refreshing = true;
    const bool hasSrc = proc.getSource() != nullptr, rec = proc.recording();
    typeTiles.set ((int) proc.type); charTiles.set ((int) proc.character);
    if (clearPill.on != proc.clear) { clearPill.on = proc.clear; clearPill.repaint(); }
    musicalSl.setValue (proc.musical, juce::dontSendNotification);
    toneKnob.setValue (proc.tone(), juce::dontSendNotification); spaceKnob.setValue (proc.space(), juce::dontSendNotification);
    {   // build 6: the type's knob, the melody style
        const auto kt = proc.knobType();
        typeKnob.setValue (proc.knobValue(), juce::dontSendNotification);
        typeKnob.setDoubleClickReturnValue (true, gen::Recipe::knobDefault (kt));
        typeKnob.setTooltip (juce::String (gen::Recipe::knobTip (kt)) + " Double-click: the default.");
        auto t = proc.currentTake();
        melodyBtn.value = proc.style == gen::Style::Auto && t != nullptr && t->style != gen::Style::Auto && (proc.type == gen::Type::Auto || t->type == proc.type) ? juce::String ("AUTO") : juce::String (gen::styleName (proc.style));
        if (proc.style == gen::Style::Auto && t != nullptr && t->style != gen::Style::Auto) melodyBtn.value = gen::styleName (t->style);
        melodyBtn.setTooltip (juce::String ("STYLE: open the style browser (every melody, chord and arp style; click one to hear it). AUTO picks one that suits the take; VARY keeps it. Now: ")
                              + gen::styleName (proc.style) + ". " + gen::styleTip (proc.style));
        lengthBtn.value = juce::String (proc.lengthBars) + (proc.lengthBars == 1 ? " BAR" : " BARS");
        melodyBtn.repaint(); hoodBtn.repaint(); lengthBtn.repaint();
        if (browser.isVisible()) browser.repaint();
        hood.refresh();   // (its knobs' tooltips and which of them the type uses, also while it is hidden)
    }
    const bool hasTake = proc.currentTake() != nullptr;
    generateBtn.setEnabled (! rec); varyBtn.setEnabled (hasSrc && ! rec && ! proc.takes().empty());
    for (juce::Component* c : { (juce::Component*) &playBtn, (juce::Component*) &dragWav, (juce::Component*) &dragMidi, (juce::Component*) &saveBtn }) c->setEnabled (hasTake);
    prevBtn.setEnabled (! proc.takes().empty()); nextBtn.setEnabled (! proc.takes().empty());
    stage.recordBtn.setButtonText (rec ? "STOP" : "RECORD"); stage.recordBtn.icon = rec ? gslice::ui::Icon::Stop : gslice::ui::Icon::Record;
    stage.recordBtn.lit = rec; stage.recordBtn.red = rec;
    stage.loadBtn.setEnabled (! rec);
    for (auto* b : { &stage.zoomInBtn, &stage.zoomOutBtn, &stage.fitBtn, &stage.playSrcBtn, &stage.trimBtn }) b->setEnabled (hasSrc && ! rec);
    if (! hasSrc && stage.trimMode) { stage.trimMode = false; stage.trimBtn.lit = false; }
    if (! stage.trimMode) { stage.dragT0 = proc.trimStart(); stage.dragT1 = proc.trimEnd(); }
    if (stage.shownSrc != proc.getSource().get()) { stage.shownSrc = proc.getSource().get(); stage.sourceChanged(); stage.overKey = nullptr; }
    keyBtn.value = proc.keyTonic < 0 ? (proc.keyKnown() ? "AUTO " + keyText() : juce::String ("AUTO")) : keyText();
    tempoBtn.value = juce::String (juce::roundToInt (proc.tempo())) + (proc.tempoFromDaw() ? "  DAW" : "");
    unlock.refresh(); unlock.setVisible (true);
    scaleBox.keyText = keyText(); scaleBox.lockBtn.lit = proc.keyLock; scaleBox.lockBtn.icon = proc.keyLock ? gslice::ui::Icon::Lock : gslice::ui::Icon::Unlock;
    updateKeysLit();
    refreshing = false;
    for (auto* c : { (juce::Component*) &keyBtn, (juce::Component*) &tempoBtn, (juce::Component*) &stage, (juce::Component*) &scaleBox, (juce::Component*) &strip, (juce::Component*) &generateBtn })
        c->repaint();
    canvas.repaint();
}

void SliceEditor::updateKeysLit()
{
    std::bitset<128> lit;
    if (auto t = proc.currentTake())
    {
        for (int tr = 0; tr < t->midi.getNumTracks(); ++tr)
            if (auto* seq = t->midi.getTrack (tr))
                for (auto* ev : *seq) if (ev->message.isNoteOn()) { int n = ev->message.getNoteNumber(); if (n >= 0 && n < 128) lit[(size_t) n] = true; }
    }
    else if (proc.getSource() != nullptr && proc.keyKnown())
    {   // no take yet: the key's tonic in every octave
        const int tonic = proc.keyTonic >= 0 ? proc.keyTonic : proc.detectedKey().tonic;
        for (int n = kLowKey; n <= kHighKey; ++n) if (((n - tonic) % 12 + 12) % 12 == 0) lit[(size_t) n] = true;
    }
    if (lit != keys.lit) { keys.lit = lit; keys.repaint(); }
}

juce::String SliceEditor::statusText() const
{
    if (proc.recording()) return "RECORDING";
    if (proc.isLoading()) return "LOADING";
    if (proc.busy()) return proc.busyText();
    if (proc.getSource() == nullptr) return "DROP A SOUND TO START";
    if (proc.takes().empty()) return "READY: PRESS GENERATE";
    return juce::String ((int) proc.takes().size()) + ((int) proc.takes().size() == 1 ? " TAKE" : " TAKES");
}

juce::String SliceEditor::infoText() const
{
    if (toastUntil > juce::Time::getMillisecondCounterHiRes()) return toastText;
    if (auto t = proc.currentTake()) return t->recipe.name + ":  " + t->info;
    if (proc.getSource() != nullptr && proc.takes().empty()) return "Pick a TAKE TYPE (or leave it on AUTO), then press GENERATE. Every press makes a new take.";
    return proc.lastMessage;
}

void SliceEditor::resized()
{
    const float w = (float) getWidth(), h = (float) getHeight();
    scale = juce::jmin (w / (float) gslice::ui::kDesignW, h / (float) gslice::ui::kDesignH);
    const float ox = (w - (float) gslice::ui::kDesignW * scale) * 0.5f, oy = (h - (float) gslice::ui::kDesignH * scale) * 0.5f;
    canvas.setBounds (0, 0, gslice::ui::kDesignW, gslice::ui::kDesignH);
    canvas.setTransform (juce::AffineTransform::scale (scale).translated (ox, oy));
    keyBtn.setBounds (kKeyBtn); tempoBtn.setBounds (kTempoBtn); looks.setBounds (kLooksR); gearBtn.setBounds (kGear); unlock.setBounds (kUnlock);
    stage.setBounds (kStage);
    typeTiles.setBounds (kTypeTiles); charTiles.setBounds (kCharTiles); clearPill.setBounds (kClearPill);
    musicalSl.setBounds (kMusical); toneKnob.setBounds (kTone); spaceKnob.setBounds (kSpace); typeKnob.setBounds (kTypeKnob);
    melodyBtn.setBounds (kMelodyPill); hoodBtn.setBounds (kHoodPill); hood.setBounds (kHood); browser.setBounds (kHood);
    stylePrev.setBounds (kStylePrev); styleNext.setBounds (kStyleNext); lengthBtn.setBounds (kLengthPill);
    generateBtn.setBounds (kGenerate); varyBtn.setBounds (kVary);
    prevBtn.setBounds (kPrev); strip.setBounds (kStrip); nextBtn.setBounds (kNext);
    playBtn.setBounds (kPlay); dragWav.setBounds (kDragWav); dragMidi.setBounds (kDragMidi); saveBtn.setBounds (kSave);
    keys.setBounds (kKeys);
    { int whites = 0; for (int n = kLowKey; n <= kHighKey; ++n) if (! juce::MidiMessage::isMidiNoteBlack (n)) ++whites;
      keys.setKeyWidth ((float) kKeys.getWidth() / (float) whites); keys.setBlackNoteWidthProportion (0.62f); keys.setBlackNoteLengthProportion (0.6f); }
    velocity.setBounds (kVelocity); scaleBox.setBounds (kScale);
    toastView.setBounds (kToast); knobLabels.setBounds (kKnobLabels);
}

void SliceEditor::paintStatic (juce::Graphics& g)
{
    const float W = (float) gslice::ui::kDesignW, H = (float) gslice::ui::kDesignH;
    // the window: deep navy with soft violet / blue glows (GHOST: the aurora)
    if (S.glass) gg::paintAurora (g, W, H);
    else
    {
        g.fillAll (S.bg);
        auto orb = [&] (float x, float y, float r, juce::Colour c) { g.setGradientFill (juce::ColourGradient (c, x, y, c.withAlpha (0.0f), x + r, y, true)); g.fillRect (0.0f, 0.0f, W, H); };
        orb (60.0f, 1000.0f, 760.0f, S.glow1);
        orb (240.0f, 60.0f, 720.0f, S.glow2);
        orb (1460.0f, 460.0f, 800.0f, S.glow3);
        orb (760.0f, 300.0f, 520.0f, S.glow2.withMultipliedAlpha (0.5f));
        // the vignette
        juce::ColourGradient v (juce::Colours::transparentBlack, W * 0.5f, H * 0.45f, juce::Colours::black.withAlpha (S.light ? 0.06f : 0.45f), 0.0f, -80.0f, true);
        g.setGradientFill (v); g.fillRect (0.0f, 0.0f, W, H);
    }
    using gslice::ui::Weight;
    // panels
    glassPanel (g, kHeader, 16.0f, S);
    {   // the SOUND panel: glass with the glowing violet border
        const auto r = kStage.toFloat();
        glow (g, r, 24.0f, S.a, 16.0f, 8, S.light ? 0.035f : 0.075f);
        glassPanel (g, r, 24.0f, S);
        juce::ColourGradient bd (S.a2, r.getX(), r.getY(), S.a, r.getRight(), r.getBottom(), false); bd.addColour (0.6, S.a.interpolatedWith (S.b2, 0.25f));
        g.setGradientFill (bd); g.drawRoundedRectangle (r.reduced (0.8f), 24.0f, 2.0f);
        g.setColour (S.a2.withAlpha (0.25f)); g.drawRoundedRectangle (r.reduced (4.0f), 21.0f, 1.0f);
    }
    glassPanel (g, kTypePanel, 16.0f, S);
    glassPanel (g, kCharPanel, 16.0f, S);
    { g.setColour (S.panelLine.withMultipliedAlpha (0.6f)); g.drawRoundedRectangle (R (1133.0f, 635.0f, 309.0f, 72.0f), 24.0f, 1.0f); }
    glassPanel (g, kMusicalPanel, 14.0f, S);
    glassPanel (g, kTakesPanel, 16.0f, S);
    glassPanel (g, kTilesPanel, 16.0f, S);
    glassPanel (g, kKeysPanel, 14.0f, S);
    { g.setColour (S.well); g.fillRoundedRectangle (kLooksBox, 12.0f); g.setColour (S.panelLine); g.drawRoundedRectangle (kLooksBox.reduced (0.5f), 12.0f, 1.0f); }
    // the ghost + the wordmark
    {
        { auto mc = mascotColours (P); if (! S.light) mc.hem = juce::Colour (0xffcdbdff); drawSliceGhost (g, mc, 88.0f, 66.0f, 86.0f); }
        const auto f = gslice::ui::capFont (21.0f, Weight::Bold, 0.165f);
        g.setColour (S.ink); textAt (g, "GHOST", f, 133.0f, 56.0f);
        const float x = 295.0f, w = widthOf (f, "SLICE");
        juce::ColourGradient gr (S.light ? S.a : juce::Colour (0xffe56cf2), x, 0.0f, S.b2, x + w, 0.0f, false); gr.addColour (0.45, S.a2.interpolatedWith (S.a, 0.3f));
        g.setGradientFill (gr); textAt (g, "SLICE", f, x, 56.0f);
        g.setColour (S.dim); textAt (g, juce::CharPointer_UTF8 ("VOCALS  \xe2\x80\xa2  SOUNDS  \xe2\x80\xa2  INSPIRATION"), gslice::ui::capFont (7.0f, Weight::Medium, 0.19f), 143.0f, 84.0f);
    }
    // panel titles and captions
    const auto title = gslice::ui::capFont (10.5f, Weight::SemiBold, 0.08f);
    g.setColour (S.ink); textAt (g, "TAKE TYPE", title, 43.0f, 495.0f); textAt (g, "CHARACTER", title, 719.0f, 495.0f);
    const auto lab = gslice::ui::capFont (10.0f, Weight::SemiBold, 0.06f);
    g.setColour (S.ink); textAt (g, "NATURAL", lab, 42.0f, 688.0f); textAt (g, "MUSICAL", lab, 692.0f, 688.0f, juce::Justification::centredRight, 200.0f);
    g.setColour (S.dim); textAt (g, juce::CharPointer_UTF8 ("the voice\xe2\x80\x99s own pitches   \xe2\x86\x94   snapped to your key and chords"), gslice::ui::capFont (8.6f, Weight::Medium), 363.0f, 723.0f, juce::Justification::centred, 600.0f);
    g.setColour (S.dim); textAt (g, "VELOCITY", gslice::ui::capFont (7.2f, Weight::Medium, 0.06f), 1274.0f, 930.0f, juce::Justification::centred, 90.0f);
    textAt (g, "SCALE", gslice::ui::capFont (9.0f, Weight::Medium, 0.06f), 1354.0f, 930.0f, juce::Justification::centred, 90.0f);
    // the keyboard's dark bed
    g.setColour (S.light ? juce::Colour (0xff2a2540) : juce::Colour (0xff07061a)); g.fillRoundedRectangle (kKeys.toFloat().expanded (2.0f, 2.0f), 5.0f);
}

void SliceEditor::paintLive (juce::Graphics& g)
{
    using gslice::ui::Weight;
    const auto title = gslice::ui::capFont (10.0f, Weight::SemiBold, 0.06f);
    g.setColour (S.ink); textAt (g, "TAKES", title, 41.0f, 775.0f);
    g.setColour (S.light ? S.a : S.a2.interpolatedWith (S.ink, 0.35f)); textAt (g, juce::String ((int) proc.takes().size()), title, 103.0f, 775.0f);
}

void SliceEditor::paintKnobLabels (juce::Graphics& g)
{   // (on top of the knobs: the words sit over the rings' lower edge, as in the picture)
    using gslice::ui::Weight;
    g.addTransform (juce::AffineTransform::translation (-(float) kKnobLabels.getX(), -(float) kKnobLabels.getY()));
    const auto small = gslice::ui::capFont (7.6f, Weight::Medium, 0.06f), knobName = gslice::ui::capFont (10.0f, Weight::SemiBold, 0.06f);
    const float tc = (float) kTone.getCentreX(), kc = (float) kTypeKnob.getCentreX(), sc = (float) kSpace.getCentreX();
    g.setColour (S.dim); textAt (g, "DARK", small, tc - 50.0f, 742.0f, juce::Justification::centred, 80.0f); textAt (g, "BRIGHT", small, tc + 54.0f, 742.0f, juce::Justification::centred, 80.0f);
    textAt (g, "DRY", small, sc - 50.0f, 742.0f, juce::Justification::centred, 80.0f); textAt (g, "HUGE", small, 1174.0f, 742.0f, juce::Justification::centred, 80.0f);
    g.setColour (S.ink); textAt (g, "TONE", knobName, tc, 741.0f, juce::Justification::centred, 80.0f); textAt (g, "SPACE", knobName, sc, 741.0f, juce::Justification::centred, 80.0f);
    g.setColour (S.ink.interpolatedWith (S.dim, 0.15f)); textAt (g, juce::String (juce::roundToInt (proc.space() * 100.0f)) + "%", gslice::ui::capFont (7.8f, Weight::Medium, 0.04f), 1117.0f, 742.0f, juce::Justification::centred, 80.0f);
    // build 6: the type's knob: its name, and the words at its two ends under it
    const auto kt = proc.knobType();
    g.setColour (S.light ? S.a : S.a2.interpolatedWith (S.ink, 0.45f)); textAt (g, gen::Recipe::knobName (kt), knobName, kc, 741.0f, juce::Justification::centred, 110.0f);
    g.setColour (S.dim); textAt (g, juce::String (gen::Recipe::knobLeft (kt)) + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + gen::Recipe::knobRight (kt),
                                 gslice::ui::capFont (6.4f, Weight::Medium, 0.04f), kc, 753.5f, juce::Justification::centred, 130.0f);
}

void SliceEditor::paint (juce::Graphics& g) { g.fillAll (S.bg); }

void SliceEditor::timerCallback()
{
    generateBtn.phase += 1.0f / 30.0f;
    if (hood.isVisible()) hood.tick();
    const bool busy = proc.busy(), rec = proc.recording(), sp = proc.sourcePlaying();
    if (busy || generateBtn.isMouseOver()) generateBtn.repaint();
    if (rec || sp || sp != wasSrcPlaying || (busy && proc.partsEdited())) stage.repaint();
    if (flashUntil > 0 && juce::Time::getMillisecondCounterHiRes() > flashUntil) { flashUntil = 0; keys.flash = -1; keys.repaint(); }
    if (sp != wasSrcPlaying) { wasSrcPlaying = sp; stage.playSrcBtn.icon = sp ? gslice::ui::Icon::Stop : gslice::ui::Icon::Play; stage.playSrcBtn.lit = sp; stage.playSrcBtn.repaint(); }
    if (busy || proc.previewPlaying()) strip.repaint();
    const bool pl = proc.previewPlaying();
    if ((playBtn.icon == gslice::ui::Icon::Stop) != pl) { playBtn.icon = pl ? gslice::ui::Icon::Stop : gslice::ui::Icon::Play; playBtn.setButtonText (pl ? "STOP" : "PLAY"); playBtn.repaint(); }
    if (wasBusy != busy || wasRec != rec) { wasBusy = busy; wasRec = rec; refresh(); }
    if (toastUntil > 0 && juce::Time::getMillisecondCounterHiRes() > toastUntil) { toastUntil = 0; toastView.setVisible (false); }
    const auto tv = juce::String (juce::roundToInt (proc.tempo())) + (proc.tempoFromDaw() ? "  DAW" : "");
    if (tempoBtn.value != tv) { tempoBtn.value = tv; tempoBtn.repaint(); }
    // VELOCITY: the last note-on, held while notes sound, then it falls
    const int n = proc.noteOnCount();
    float v = velShown;
    if (n != lastNoteOns) { lastNoteOns = n; v = proc.lastVelocity(); }
    else if (proc.activeVoices() == 0) v *= 0.93f;
    if (v < 0.01f) v = 0.0f;
    if (std::abs (v - velocity.value) > 0.004f) { velocity.value = v; velocity.repaint(); }
    velShown = v;
}

// ---------------------------------------------------------------- actions
void SliceEditor::loadClicked()
{
    chooser = std::make_unique<juce::FileChooser> ("Pick a sound", juce::File::getSpecialLocation (juce::File::userMusicDirectory), source::wildcard());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc) { const auto f = fc.getResult(); if (f.existsAsFile()) proc.loadAudioAsync (f); });
}

void SliceEditor::recordClicked()
{
    if (proc.recording()) proc.stopRecording();
    else proc.startRecording();
    refresh();
}

void SliceEditor::keyMenu()
{
    juce::PopupMenu m, maj, mn;
    const auto k = proc.detectedKey();
    m.addItem (1, "AUTO" + (proc.keyKnown() ? juce::String (" (the sound's: ") + keyName (k.tonic, k.minor) + ")" : juce::String()), true, proc.keyTonic < 0);
    for (int i = 0; i < 12; ++i) { maj.addItem (100 + i, keyName (i, false), true, proc.keyTonic == i && ! proc.keyMinor); mn.addItem (200 + i, keyName (i, true), true, proc.keyTonic == i && proc.keyMinor); }
    m.addSubMenu ("MAJOR (happier)", maj); m.addSubMenu ("MINOR (moodier)", mn);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&keyBtn), [sp = juce::Component::SafePointer<SliceEditor> (this)] (int r)
    {
        if (sp == nullptr || r <= 0) return;
        if (r == 1) sp->proc.keyTonic = -1;
        else { sp->proc.keyTonic = r % 100; sp->proc.keyMinor = r >= 200; }
        sp->toast ("The next takes are in " + (sp->proc.keyTonic < 0 ? juce::String ("the sound's own key") : keyName (sp->proc.keyTonic, sp->proc.keyMinor)) + ".");
        sp->refresh();
    });
}

void SliceEditor::tempoMenu()
{
    if (proc.tempoFromDaw()) { toast ("The tempo follows your DAW."); return; }
    juce::PopupMenu m; for (int b = 60; b <= 180; b += 5) m.addItem (b, juce::String (b) + " BPM", true, std::abs (proc.ownBpm - b) < 0.5);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&tempoBtn), [sp = juce::Component::SafePointer<SliceEditor> (this)] (int r) { if (sp != nullptr && r > 0) { sp->proc.ownBpm = r; sp->refresh(); } });
}

void SliceEditor::stepKey (int d)
{
    int t = proc.keyTonic; bool mi = proc.keyMinor;
    if (t < 0) { const auto k = proc.detectedKey(); t = k.tonic; mi = k.minor; }
    proc.keyTonic = ((t + d) % 12 + 12) % 12; proc.keyMinor = mi;
    toast ("The next takes are in " + keyName (proc.keyTonic, proc.keyMinor) + ".");
    refresh();
}

bool SliceEditor::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0))
    {
        if (! proc.undoEdit()) toast ("Nothing to undo.");
        stage.repaint();
        return true;
    }
    if (k == juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0))
    {   // build 8: REDO (the boxes, the roll, PLAY IT)
        if (! proc.redoEdit()) toast ("Nothing to redo.");
        stage.repaint();
        return true;
    }
    return false;
}

void SliceEditor::toggleClear()
{
    proc.clear = ! proc.clear;
    toast (proc.clear ? "CLEAR on: press GENERATE for a clearer take" : "CLEAR off: press GENERATE for a dreamier take");
    refresh();
}

void SliceEditor::settingsMenu()
{
    juce::PopupMenu m, in;
    for (int i = 0; i < 3; ++i) in.addItem (10 + i, kInputNames[i], true, (int) proc.input == i);
    m.addSectionHeader ("GHOST SLICE " GSLICE_VERSION);
    m.addSubMenu ("RECORD INPUT", in);
    m.addItem (23, "CLEAR: keep the sound clean and true (the next takes)", true, proc.clear);
    m.addItem (20, "Keep the key when a new sound loads (SCALE lock)", true, proc.keyLock);
    m.addItem (21, "TRIM: use the whole sound again", proc.isTrimmed());
    m.addItem (22, "Show the whole sound (fit the waveform)", proc.getSource() != nullptr);
    m.addSeparator();
    m.addItem (30, "How Ghost Slice works...");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&gearBtn), [sp = juce::Component::SafePointer<SliceEditor> (this)] (int r)
    {
        if (sp == nullptr || r <= 0) return;
        auto& p = sp->proc;
        if (r >= 10 && r <= 12) { p.input = (SliceProcessor::Input) (r - 10); sp->toast ("RECORD records from " + p.recordSourceText() + "."); }
        else if (r == 20) p.keyLock = ! p.keyLock;
        else if (r == 23) { sp->toggleClear(); return; }
        else if (r == 21) { p.setTrim (0.0, 1.0); sp->stage.dragT0 = 0.0; sp->stage.dragT1 = 1.0; }
        else if (r == 22) sp->stage.fitAll();
        else if (r == 30)
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Ghost Slice",
                "1. Drop a vocal or any sound on the big panel (or LOAD... / RECORD).\n"
                "2. Pick a TAKE TYPE and a CHARACTER (or leave AUTO), set NATURAL <-> MUSICAL.\n"
                "3. Press GENERATE: every press makes a new take. VARY makes a variation of the one you are on.\n"
                "4. Play it on the keys (or your MIDI keyboard), then DRAG WAV / DRAG MIDI into your DAW or SAVE TAKE.\n\n"
                "CLEAR (next to CHARACTER) keeps the next takes clean and true to your sound; off gives the dreamier, blurrier sound.\n"
                "TRIM chooses the part of the sound GENERATE uses. Hover any control to see what it does.");
        sp->refresh();
    });
}

void SliceEditor::dragWavOut()
{
    if (proc.currentTake() == nullptr) { toast ("No take yet: press GENERATE."); return; }
    if (! ghosty::exportAllowed (proc.license, &dragWav, "slice.drag.wav")) return;
    juce::String err; const auto f = proc.writeTakeWav (SliceProcessor::dragFolder(), err);
    if (f == juce::File()) { toast (err); return; }
    ghosty::exportgate::dragFiles (proc.license, { f.getFullPathName() }, &dragWav);
}

void SliceEditor::dragMidiOut()
{
    if (proc.currentTake() == nullptr) { toast ("No take yet: press GENERATE."); return; }
    if (! ghosty::exportAllowed (proc.license, &dragMidi, "slice.drag.midi")) return;
    juce::String err; const auto f = proc.writeTakeMidi (SliceProcessor::dragFolder(), err);
    if (f == juce::File()) { toast (err); return; }
    ghosty::exportgate::dragFiles (proc.license, { f.getFullPathName() }, &dragMidi);
}

void SliceEditor::saveTakeClicked()
{
    if (proc.currentTake() == nullptr) { toast ("No take yet: press GENERATE."); return; }
    if (! ghosty::exportAllowed (proc.license, &saveBtn, "slice.take.save")) return;
    juce::String err; const auto f = proc.saveTake (err);
    toast (f == juce::File() ? err : "Saved " + f.getFileName() + " (+ .mid) in Documents/Ghosty/Ghost Slice/Takes.");
}
