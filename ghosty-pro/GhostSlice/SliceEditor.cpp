#include "SliceEditor.h"
#include "GhostSliceArt.h"

using namespace gslice;
using namespace gslice::ui;
using R = juce::Rectangle<float>;
using RI = juce::Rectangle<int>;

namespace
{
// ---------------------------------------------------------------- the design (the picture's own pixels, 1536 x 1024)
const RI kKeyBtn { 677, 18, 148, 35 }, kTempoBtn { 953, 18, 128, 35 }, kTempoSw { 1143, 17, 272, 36 }, kGear { 1472, 15, 44, 44 };
const RI kStage { 350, 80, 1186, 500 };                       // the landscape + the waveform strip along its foot
const RI kLoad { 772, 243, 153, 37 }, kRecord { 960, 243, 160, 37 }, kPlaySrc { 1239, 537, 136, 35 }, kTrim { 1391, 537, 132, 35 };
const RI kTypes { 30, 147, 300, 270 }, kChars { 27, 485, 307, 108 }, kStyle { 161, 627, 159, 33 }, kLength { 161, 678, 159, 34 };
const RI kMusical { 482, 616, 356, 40 }, kTone { 1020, 586, 92, 92 }, kTypeKnob { 1186, 586, 92, 92 }, kSpace { 1359, 586, 92, 92 };
const RI kGenerate { 356, 693, 674, 86 }, kVary { 1060, 713, 273, 53 };
const R kTakesBox { 25, 821, 743, 66 };
const RI kPrev { 36, 832, 32, 44 }, kStrip { 76, 826, 642, 56 }, kNext { 726, 832, 32, 44 };
const RI kPlay { 831, 816, 126, 70 }, kDragWav { 969, 816, 129, 70 }, kDragMidi { 1110, 816, 129, 70 }, kDragChords { 1251, 816, 131, 70 }, kSave { 1393, 816, 130, 70 };
const RI kKeys { 22, 910, 1493, 104 }, kToast { 637, 92, 600, 34 };

const char* kTypeNames[] { "AUTO", "INSTRUMENT", "PAD", "CHOPS", "TEXTURE" };
const char* kTypeTips[] {
    "AUTO: every GENERATE makes a different kind of take (instrument, pad, chops, texture, in turn).",
    "INSTRUMENT: the most singable part of your sound becomes one playable sound across the whole keyboard. Hold a key and it keeps going.",
    "PAD: lots of tiny pieces of your sound, tuned to chords in the key, frozen into long glassy tones. Wide and slowly moving.",
    "CHOPS: bigger pieces of your sound rearranged into a catchy phrase in the key and the tempo, with repeats, reverses and stutters.",
    "TEXTURE: clouds and drones: your sound frozen and spread out into an atmosphere." };
const char* kCharNames[] { "NATURAL", "CHIPMUNK", "DEEP" };
const char* kCharTips[] {
    "NATURAL: pitched, but the voice keeps its own size and colour.",
    "CHIPMUNK: the voice changes size with the pitch, the small, childlike sound (and it plays an octave up).",
    "DEEP: a bigger, darker throat (and it plays an octave down)." };
const char* kInputNames[] { "AUTO: the track's input, else the Mac's microphone", "TRACK: only what the DAW sends into Ghost Slice", "MAC MIC: the Mac's own microphone (works in FL Studio's channel rack)" };
juce::String keyName (int tonic, bool minor) { return analysis::pitchClassName (tonic) + (minor ? " MIN" : " MAJ"); }
juce::String dot() { return juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")); }

float en (const juce::Component& c, float off = 0.35f) { return c.isEnabled() ? 1.0f : off; }

// a soft glow: the shape stroked wide and faint a few times
void glow (juce::Graphics& g, const juce::Path& p, juce::Colour c, float maxW = 14.0f, int steps = 5, float a = 0.06f)
{
    for (int i = steps; i >= 1; --i) { g.setColour (c.withAlpha (a)); g.strokePath (p, juce::PathStrokeType (maxW * (float) i / (float) steps, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }
}

// a crystal shard: six points, its left facets lit and its right ones in shade
// the paper's grain: a fixed speckle, tiled very faintly over the white (printed, not flat)
const juce::Image& grain()
{
    auto make = [] (juce::Colour c)
    {
        juce::Image i (juce::Image::ARGB, 192, 192, true); juce::Random r (42);
        for (int y = 0; y < 192; ++y) for (int x = 0; x < 192; ++x)
        { const float v = r.nextFloat(); if (v > 0.55f) i.setPixelAt (x, y, c.withAlpha ((v - 0.55f) * 0.5f)); }
        return i;
    };
    static juce::Image light = make (juce::Colour (0xff111215)), night = make (juce::Colour (0xffe8e4d8));
    return Look::dark ? night : light;
}
// a gem shard in ink: six points, the lit facets (luster when `lit`), the shaded ones hatched. kind 0 clear, 1 grey, 2 black.
void crystal (juce::Graphics& g, juce::Point<float> c, float w, float h, float angle, int kind, bool lit)
{
    const auto tr = juce::AffineTransform::rotation (angle).translated (c);
    const juce::Point<float> top (0.0f, -h * 0.5f), ru (w * 0.5f, -h * 0.16f), rl (w * 0.38f, h * 0.30f), bot (0.0f, h * 0.5f), ll (-w * 0.44f, h * 0.24f), lu (-w * 0.5f, -h * 0.2f),
                             mid (w * 0.04f, -h * 0.02f);
    auto poly = [&] (std::initializer_list<juce::Point<float>> pts) { juce::Path p; bool first = true; for (auto q : pts) { if (first) p.startNewSubPath (q); else p.lineTo (q); first = false; } p.closeSubPath(); p.applyTransform (tr); return p; };
    const auto whole = poly ({ top, ru, rl, bot, ll, lu });
    const auto A = poly ({ top, mid, lu }), B = poly ({ lu, mid, ll }), C = poly ({ top, ru, mid }), D = poly ({ ru, rl, mid }), E = poly ({ mid, rl, bot, ll });
    const auto bb = whole.getBounds();
    // the stones keep their own colours in both looks (clear, grey, black); only a black stone's outline follows the look
    const auto ink = Look::black(), paper = Look::stone();
    g.setColour (kind == 2 ? ink : kind == 1 ? juce::Colour (0xffd9d6cc) : paper); g.fillPath (whole);
    if (lit) { g.setGradientFill (Look::luster (bb, kind == 2 ? 0.75f : 1.0f)); g.fillPath (A); g.fillPath (B); g.setGradientFill (Look::luster (bb, 0.55f)); g.fillPath (C); }
    if (kind == 2)
    {
        if (! lit) { g.setColour (paper.withAlpha (0.16f)); g.fillPath (A); }
        Look::hatch (g, E, 60.0f, 2.6f, paper.withAlpha (0.25f), 0.5f);
    }
    else
    {
        Look::hatch (g, D, 62.0f, kind == 1 ? 1.9f : 2.4f, ink.withAlpha (0.85f), 0.6f);
        Look::hatch (g, E, 62.0f, kind == 1 ? 2.4f : 3.6f, ink.withAlpha (0.6f), 0.5f);
        if (kind == 1) Look::hatch (g, C, 62.0f, 3.4f, ink.withAlpha (0.45f), 0.5f);
    }
    juce::Path f; f.startNewSubPath (top); f.lineTo (mid); f.lineTo (bot); f.startNewSubPath (lu); f.lineTo (mid); f.lineTo (rl); f.startNewSubPath (ru); f.lineTo (mid); f.lineTo (ll); f.applyTransform (tr);
    g.setColour ((kind == 2 ? paper : ink).withAlpha (0.55f)); g.strokePath (f, juce::PathStrokeType (0.7f));
    g.setColour (kind == 2 ? Look::ink() : ink); g.strokePath (whole, juce::PathStrokeType (1.2f, juce::PathStrokeType::mitered));
    if (Look::dark && kind != 2) { g.setColour (Look::ink().withAlpha (0.35f)); g.strokePath (whole, juce::PathStrokeType (3.0f)); g.setColour (ink); g.strokePath (whole, juce::PathStrokeType (1.2f)); }
}

// the elongated crystal of GENERATE / VARY: a hexagon with pointed ends
juce::Path gem (R r, float tip)
{
    juce::Path p;
    p.startNewSubPath (r.getX(), r.getCentreY());
    p.lineTo (r.getX() + tip, r.getY()); p.lineTo (r.getRight() - tip, r.getY()); p.lineTo (r.getRight(), r.getCentreY());
    p.lineTo (r.getRight() - tip, r.getBottom()); p.lineTo (r.getX() + tip, r.getBottom()); p.closeSubPath();
    return p;
}
} // namespace

// ================================================================ the look
static juce::Typeface::Ptr typeface (const char* data, int size) { return juce::Typeface::createSystemTypefaceFor (data, (size_t) size); }

juce::Font Look::caps (float h, bool semi, float track)
{
    static juce::Typeface::Ptr reg = typeface (GhostSliceArt::CinzelRegular_ttf, GhostSliceArt::CinzelRegular_ttfSize);
    static juce::Typeface::Ptr sb = typeface (GhostSliceArt::CinzelSemiBold_ttf, GhostSliceArt::CinzelSemiBold_ttfSize);
    return juce::Font (juce::FontOptions (semi ? sb : reg).withHeight (h * 1.22f)).withExtraKerningFactor (track);   // (Cinzel's caps sit low in its height: 1.22 matches the picture's letters)
}

juce::Font Look::italic (float h)
{
    static juce::Typeface::Ptr it = typeface (GhostSliceArt::CormorantGaramondMediumItalic_ttf, GhostSliceArt::CormorantGaramondMediumItalic_ttfSize);
    return juce::Font (juce::FontOptions (it).withHeight (h));
}

float Look::width (const juce::Font& f, const juce::String& s) { return juce::GlyphArrangement::getStringWidth (f, s); }

void Look::text (juce::Graphics& g, const juce::String& s, const juce::Font& f, R r, juce::Justification j)
{
    g.setFont (f); g.drawText (s, r, j, false);
}

juce::ColourGradient Look::luster (R r, float alpha)
{
    juce::ColourGradient g (juce::Colour (0xffbdf0e1).withMultipliedAlpha (alpha), r.getX(), r.getY(), juce::Colour (0xffd2f3ea).withMultipliedAlpha (alpha), r.getRight(), r.getBottom(), false);
    g.addColour (0.22, juce::Colour (0xffe6dcff).withMultipliedAlpha (alpha));
    g.addColour (0.45, juce::Colour (0xfffff5da).withMultipliedAlpha (alpha));
    g.addColour (0.62, juce::Colour (0xffffffff).withMultipliedAlpha (alpha));
    g.addColour (0.78, juce::Colour (0xffc3e8ff).withMultipliedAlpha (alpha));
    return g;
}

void Look::hatch (juce::Graphics& g, const juce::Path& clip, float angleDeg, float spacing, juce::Colour c, float thick)
{
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (clip);
    const auto b = clip.getBounds().expanded (2.0f);
    const float a = juce::degreesToRadians (angleDeg), d = std::hypot (b.getWidth(), b.getHeight());
    const juce::Point<float> c0 = b.getCentre(), dir (std::cos (a), std::sin (a)), nrm (-dir.y, dir.x);
    g.setColour (c);
    for (float o = -d * 0.5f; o <= d * 0.5f; o += spacing)
    {
        const auto p = c0 + nrm * o;
        g.drawLine ({ p - dir * d * 0.5f, p + dir * d * 0.5f }, thick);
    }
}

void Look::sparkle (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour col)
{
    juce::Path p;
    p.startNewSubPath (c.x, c.y - r); p.quadraticTo (c.x, c.y, c.x + r * 0.32f, c.y); p.quadraticTo (c.x, c.y, c.x, c.y + r);
    p.quadraticTo (c.x, c.y, c.x - r * 0.32f, c.y); p.quadraticTo (c.x, c.y, c.x, c.y - r); p.closeSubPath();
    juce::Path q; q.startNewSubPath (c.x - r, c.y); q.quadraticTo (c.x, c.y, c.x, c.y - r * 0.32f); q.quadraticTo (c.x, c.y, c.x + r, c.y);
    q.quadraticTo (c.x, c.y, c.x, c.y + r * 0.32f); q.quadraticTo (c.x, c.y, c.x - r, c.y); q.closeSubPath();
    g.setColour (col); g.fillPath (p); g.fillPath (q);
}

// ================================================================ icons
void gslice::ui::drawIcon (juce::Graphics& g, Icon ic, R r, juce::Colour c)
{
    const float s = juce::jmin (r.getWidth(), r.getHeight());
    r = r.withSizeKeepingCentre (s, s);
    const float x = r.getX(), y = r.getY(), cx = r.getCentreX(), cy = r.getCentreY();
    const float lw = juce::jmax (1.2f, s * 0.075f);
    const juce::PathStrokeType st (lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour (c);
    juce::Path p;
    switch (ic)
    {
        case Icon::None: break;
        case Icon::Folder:
            p.startNewSubPath (x + s * 0.08f, y + s * 0.82f); p.lineTo (x + s * 0.08f, y + s * 0.2f); p.lineTo (x + s * 0.38f, y + s * 0.2f); p.lineTo (x + s * 0.48f, y + s * 0.32f);
            p.lineTo (x + s * 0.92f, y + s * 0.32f); p.lineTo (x + s * 0.92f, y + s * 0.82f); p.closeSubPath(); g.strokePath (p, st); break;
        case Icon::Record: g.setColour (juce::Colour (0xffe5534b)); g.fillEllipse (r.reduced (s * 0.22f)); break;
        case Icon::Stop: g.fillRoundedRectangle (r.reduced (s * 0.24f), s * 0.05f); break;
        case Icon::Play: p.addTriangle (x + s * 0.2f, y + s * 0.12f, x + s * 0.2f, y + s * 0.88f, x + s * 0.9f, cy); g.fillPath (p); break;
        case Icon::Scissors:
            g.drawEllipse (x + s * 0.06f, y + s * 0.58f, s * 0.3f, s * 0.3f, lw); g.drawEllipse (x + s * 0.64f, y + s * 0.58f, s * 0.3f, s * 0.3f, lw);
            p.startNewSubPath (x + s * 0.3f, y + s * 0.62f); p.lineTo (x + s * 0.82f, y + s * 0.08f); p.startNewSubPath (x + s * 0.7f, y + s * 0.62f); p.lineTo (x + s * 0.18f, y + s * 0.08f);
            g.strokePath (p, st); break;
        case Icon::Gear:
        {
            const int teeth = 8; const float ro = s * 0.48f, ri = s * 0.37f;
            for (int i = 0; i < teeth * 2; ++i)
            {
                const float a0 = juce::MathConstants<float>::twoPi * (float) i / (float) (teeth * 2) - 0.18f, a1 = a0 + juce::MathConstants<float>::twoPi / (float) (teeth * 2);
                const float rr = (i % 2 == 0) ? ro : ri;
                const juce::Point<float> q0 (cx + rr * std::sin (a0), cy - rr * std::cos (a0)), q1 (cx + rr * std::sin (a1), cy - rr * std::cos (a1));
                if (i == 0) p.startNewSubPath (q0); else p.lineTo (q0);
                p.lineTo (q1);
            }
            p.closeSubPath(); g.strokePath (p, juce::PathStrokeType (lw * 0.9f, juce::PathStrokeType::mitered));
            g.drawEllipse (r.withSizeKeepingCentre (s * 0.34f, s * 0.34f), lw * 0.9f);
            break;
        }
        case Icon::ChevL: p.startNewSubPath (x + s * 0.62f, y + s * 0.2f); p.lineTo (x + s * 0.34f, cy); p.lineTo (x + s * 0.62f, y + s * 0.8f); g.strokePath (p, st); break;
        case Icon::ChevR: p.startNewSubPath (x + s * 0.38f, y + s * 0.2f); p.lineTo (x + s * 0.66f, cy); p.lineTo (x + s * 0.38f, y + s * 0.8f); g.strokePath (p, st); break;
        case Icon::ChevDown: p.startNewSubPath (x + s * 0.18f, y + s * 0.36f); p.lineTo (cx, y + s * 0.66f); p.lineTo (x + s * 0.82f, y + s * 0.36f); g.strokePath (p, st); break;
        case Icon::Wave:
        case Icon::WaveFile:
        {
            static const float hs[] { 0.18f, 0.4f, 0.7f, 0.5f, 1.0f, 0.62f, 0.85f, 0.42f, 0.66f, 0.3f, 0.16f };
            const int n = (int) std::size (hs); const float step = s / (float) n;
            for (int i = 0; i < n; ++i) { const float hh = s * 0.9f * hs[i]; g.fillRoundedRectangle (x + step * ((float) i + 0.3f), cy - hh * 0.5f, juce::jmax (1.0f, step * 0.42f), hh, step * 0.2f); }
            break;
        }
        case Icon::Bars:
            for (int i = 0; i < 5; ++i) { const float hh = i == 2 ? s * 0.55f : s * 0.8f; g.fillRect (x + s * (0.1f + 0.19f * (float) i), i == 2 ? y + s * 0.1f : y + s * 0.1f, juce::jmax (1.5f, s * 0.07f), hh); }
            break;
        case Icon::Waves:
            for (int k = 0; k < 3; ++k)
            {
                juce::Path w; const float yy = y + s * (0.3f + 0.2f * (float) k);
                for (int i = 0; i <= 24; ++i) { const float t = (float) i / 24.0f; const float px = x + s * (0.06f + 0.88f * t), py = yy + std::sin (t * juce::MathConstants<float>::twoPi * 1.2f) * s * 0.06f;
                                                if (i == 0) w.startNewSubPath (px, py); else w.lineTo (px, py); }
                g.strokePath (w, juce::PathStrokeType (lw * 0.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            break;
        case Icon::Chops:
            g.fillRect (x + s * 0.06f, cy - s * 0.12f - lw * 0.5f, s * 0.3f, lw); g.fillRect (x + s * 0.06f, cy + s * 0.12f - lw * 0.5f, s * 0.3f, lw);
            g.fillRect (x + s * 0.5f, cy - s * 0.24f - lw * 0.5f, s * 0.44f, lw); g.fillRect (x + s * 0.5f, cy - lw * 0.5f, s * 0.2f, lw); g.fillRect (x + s * 0.5f, cy + s * 0.24f - lw * 0.5f, s * 0.44f, lw);
            break;
        case Icon::Dots:
            for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j)
            {
                const float a = 0.35f + 0.65f * (float) ((i * 7 + j * 3) % 5) / 4.0f, d = s * 0.08f;
                g.setColour (c.withMultipliedAlpha (a)); g.fillEllipse (x + s * (0.12f + 0.25f * (float) i) - d * 0.5f, y + s * (0.12f + 0.25f * (float) j) - d * 0.5f, d, d);
            }
            break;
        case Icon::Infinity:
        {
            for (int i = 0; i <= 60; ++i)
            {
                const float t = juce::MathConstants<float>::twoPi * (float) i / 60.0f, d = 1.0f + std::sin (t) * std::sin (t);
                const float px = cx + s * 0.48f * std::cos (t) / d, py = cy + s * 0.48f * std::sin (t) * std::cos (t) / d;
                if (i == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
            }
            g.strokePath (p, juce::PathStrokeType (lw * 0.85f));
            juce::Path p2; p2.addPath (p, juce::AffineTransform::translation (s * 0.06f, 0.0f)); g.setColour (c.withMultipliedAlpha (0.5f)); g.strokePath (p2, juce::PathStrokeType (lw * 0.6f));
            break;
        }
        case Icon::Note:
            g.fillEllipse (x + s * 0.12f, y + s * 0.66f, s * 0.26f, s * 0.2f); g.fillEllipse (x + s * 0.58f, y + s * 0.58f, s * 0.26f, s * 0.2f);
            g.fillRect (x + s * 0.34f, y + s * 0.16f, lw, s * 0.6f); g.fillRect (x + s * 0.8f, y + s * 0.08f, lw, s * 0.6f);
            p.startNewSubPath (x + s * 0.34f + lw * 0.5f, y + s * 0.18f); p.lineTo (x + s * 0.8f + lw * 0.5f, y + s * 0.1f); g.strokePath (p, juce::PathStrokeType (lw * 1.6f)); break;
        case Icon::Layers:
            for (int k = 2; k >= 0; --k)
            {
                juce::Path d; const float yy = y + s * (0.32f + 0.17f * (float) k);
                d.startNewSubPath (cx, yy - s * 0.2f); d.lineTo (x + s * 0.94f, yy); d.lineTo (cx, yy + s * 0.2f); d.lineTo (x + s * 0.06f, yy); d.closeSubPath();
                if (k > 0) { g.setColour (c.withMultipliedAlpha (0.55f)); g.strokePath (d, juce::PathStrokeType (lw * 0.8f)); }
                else { g.setColour (c); g.strokePath (d, juce::PathStrokeType (lw * 0.9f)); }
            }
            break;
        case Icon::Download:
            p.startNewSubPath (cx, y + s * 0.08f); p.lineTo (cx, y + s * 0.6f); p.startNewSubPath (x + s * 0.3f, y + s * 0.4f); p.lineTo (cx, y + s * 0.62f); p.lineTo (x + s * 0.7f, y + s * 0.4f);
            p.startNewSubPath (x + s * 0.08f, y + s * 0.66f); p.lineTo (x + s * 0.08f, y + s * 0.88f); p.lineTo (x + s * 0.92f, y + s * 0.88f); p.lineTo (x + s * 0.92f, y + s * 0.66f);
            g.strokePath (p, st); break;
        case Icon::Moon:
        {
            juce::Path m; m.addEllipse (r.reduced (s * 0.1f)); juce::Path cut; cut.addEllipse (r.reduced (s * 0.1f).translated (s * 0.28f, -s * 0.18f));
            juce::Graphics::ScopedSaveState ss (g); g.excludeClipRegion (juce::Rectangle<int>()); 
            juce::Path c2; c2.addEllipse (r.reduced (s * 0.1f)); c2.addEllipse (r.reduced (s * 0.1f).translated (s * 0.28f, -s * 0.18f)); c2.setUsingNonZeroWinding (false);
            g.reduceClipRegion (m); g.fillPath (c2);
            break;
        }
        case Icon::Sun:
            g.fillEllipse (r.withSizeKeepingCentre (s * 0.38f, s * 0.38f));
            for (int i = 0; i < 8; ++i) { const float a = juce::MathConstants<float>::twoPi * (float) i / 8.0f; g.drawLine (cx + s * 0.3f * std::sin (a), cy - s * 0.3f * std::cos (a), cx + s * 0.46f * std::sin (a), cy - s * 0.46f * std::cos (a), lw); }
            break;
        case Icon::Star:
            p.addStar ({ cx, cy }, 5, s * 0.2f, s * 0.48f, 0.0f); g.fillPath (p); break;
    }
}

// ================================================================ LookAndFeel
LnF::LnF() { applyLook(); }

void LnF::applyLook()
{
    setColour (juce::PopupMenu::backgroundColourId, Look::paper());
    setColour (juce::PopupMenu::textColourId, Look::ink());
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Look::ink());
    setColour (juce::PopupMenu::highlightedTextColourId, Look::paper());
    setColour (juce::PopupMenu::headerTextColourId, Look::soft());
    setColour (juce::AlertWindow::backgroundColourId, Look::paper());
    setColour (juce::AlertWindow::textColourId, Look::ink());
    setColour (juce::AlertWindow::outlineColourId, Look::ink());
    setColour (juce::TextButton::buttonColourId, Look::paper());
    setColour (juce::TextButton::textColourOffId, Look::ink());
    setColour (juce::TextEditor::backgroundColourId, Look::paper2());
    setColour (juce::TextEditor::textColourId, Look::ink());
    setColour (juce::TextEditor::outlineColourId, Look::ink());
}

void LnF::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    const auto ink = s.isEnabled() ? Look::ink() : Look::grey();
    const R b ((float) x, (float) y, (float) w, (float) h);
    const float rad = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f - 2.0f;
    const auto c = b.getCentre();
    const float ang = a0 + pos * (a1 - a0);
    // the scale: fine ticks, the ones up to the value drawn long
    for (int i = 0; i <= 20; ++i)
    {
        const float t = (float) i / 20.0f, a = a0 + t * (a1 - a0), on = t <= pos + 1e-4f ? 1.0f : 0.0f;
        const float r0 = rad - (on > 0 ? 8.0f : 5.0f), r1 = rad;
        g.setColour (ink.withAlpha (on > 0 ? 1.0f : 0.35f));
        g.drawLine (c.x + r0 * std::sin (a), c.y - r0 * std::cos (a), c.x + r1 * std::sin (a), c.y - r1 * std::cos (a), on > 0 ? 1.3f : 0.8f);
    }
    // the disc: paper, an ink ring, a luster crescent where the light hits it
    const float kr = rad * 0.7f;
    const R kb (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f);
    g.setColour (Look::paper()); g.fillEllipse (kb);
    if (s.isEnabled())
    {
        juce::Path cres; cres.addEllipse (kb.reduced (2.5f)); juce::Path cut; cut.addEllipse (kb.reduced (2.5f).translated (kr * 0.22f, kr * 0.22f));
        juce::Graphics::ScopedSaveState ss (g); g.reduceClipRegion (cres); g.excludeClipRegion (cut.getBounds().toNearestInt().reduced (0));
        g.setGradientFill (Look::luster (kb, 0.9f)); g.fillPath (cres);
    }
    { juce::Path sh; sh.addEllipse (kb.reduced (2.5f)); juce::Path half; half.addPieSegment (kb.reduced (2.5f), juce::MathConstants<float>::pi * 0.25f, juce::MathConstants<float>::pi * 1.25f, 0.0f);
      Look::hatch (g, half, 45.0f, 3.0f, ink.withAlpha (0.18f), 0.5f); }
    g.setColour (ink); g.drawEllipse (kb, 1.4f);
    g.setColour (ink.withAlpha (0.3f)); g.drawEllipse (kb.reduced (5.0f), 0.7f);
    // the pointer
    const juce::Point<float> p0 (c.x + kr * 0.1f * std::sin (ang), c.y - kr * 0.1f * std::cos (ang)), p1 (c.x + kr * 0.95f * std::sin (ang), c.y - kr * 0.95f * std::cos (ang));
    g.setColour (ink); g.drawLine ({ p0, p1 }, 2.2f);
    g.fillEllipse (c.x - 2.5f, c.y - 2.5f, 5.0f, 5.0f);
}

void LnF::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider& s)
{
    const auto ink = s.isEnabled() ? Look::ink() : Look::grey();
    const float cy = (float) y + (float) h * 0.5f;
    g.setColour (ink.withAlpha (0.35f)); g.fillRect ((float) x, cy - 0.5f, (float) w, 1.0f);
    for (int i = 0; i <= 10; ++i) { const float tx = (float) x + (float) w * (float) i / 10.0f; g.fillRect (tx - 0.4f, cy + 5.0f, 0.8f, i % 5 == 0 ? 6.0f : 3.0f); }
    g.setColour (ink); g.fillRect ((float) x, cy - 1.1f, pos - (float) x, 2.2f);
    // the gem thumb
    const float tw = 12.0f, th = 17.0f;
    juce::Path d; d.startNewSubPath (pos, cy - th); d.lineTo (pos + tw, cy); d.lineTo (pos, cy + th); d.lineTo (pos - tw, cy); d.closeSubPath();
    g.setColour (Look::paper()); g.fillPath (d);
    if (s.isEnabled()) { g.setGradientFill (Look::luster (d.getBounds())); g.fillPath (d); }
    juce::Path lo; lo.addTriangle (pos, cy, pos + tw, cy, pos, cy + th); lo.addTriangle (pos, cy, pos - tw, cy, pos, cy + th);
    Look::hatch (g, lo, 60.0f, 2.2f, (s.isEnabled() ? Look::black() : ink).withAlpha (0.5f), 0.5f);
    juce::Path f; f.startNewSubPath (pos, cy - th); f.lineTo (pos, cy + th); f.startNewSubPath (pos - tw, cy); f.lineTo (pos + tw, cy);
    g.setColour ((s.isEnabled() ? Look::black() : ink).withAlpha (0.6f)); g.strokePath (f, juce::PathStrokeType (0.7f));
    g.setColour (ink); g.strokePath (d, juce::PathStrokeType (1.3f, juce::PathStrokeType::mitered));
}

void LnF::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (Look::paper());
    g.setColour (Look::ink()); g.drawRect (0, 0, w, h, 1);
}

void LnF::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    g.fillAll (Look::paper());
    g.setColour (Look::ink()); g.drawRect (0, 0, w, h, 1);
    g.setFont (juce::FontOptions (13.5f));
    g.drawFittedText (text, juce::Rectangle<int> (0, 0, w, h).reduced (10, 6), juce::Justification::centredLeft, 6);
}

juce::Rectangle<int> LnF::getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent)
{
    const juce::Font f (juce::FontOptions (13.5f));
    const int maxW = 340;
    juce::AttributedString as; as.append (text, f); juce::TextLayout tl; tl.createLayout (as, (float) maxW);
    const int w = juce::jmin (maxW, (int) std::ceil (tl.getWidth())) + 22, h = (int) std::ceil (tl.getHeight()) + 14;
    return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 14, pos.y > parent.getCentreY() ? pos.y - (h + 8) : pos.y + 18, w, h).constrainedWithin (parent);
}

// ================================================================ buttons
void Btn::paintButton (juce::Graphics& g, bool over, bool down)
{
    const bool on = isEnabled();
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    const auto txt = getButtonText();
    const auto front = inverse ? Look::bandFg() : Look::ink(), back = inverse ? Look::band() : Look::paper();
    const auto fg = on ? front : (inverse ? Look::bandFg().withAlpha (0.35f) : Look::grey());
    const bool solid = on && (lit || (down && style != Style::Plain));
    switch (style)
    {
        case Style::Plain:
        {
            if (over && on) { g.setGradientFill (Look::luster (b, 0.35f)); g.fillEllipse (b.reduced (2.0f)); }
            drawIcon (g, icon, b.withSizeKeepingCentre (iconSize, iconSize), fg);
            return;
        }
        case Style::Drop:
        {
            g.setColour (fg.withMultipliedAlpha (over && on ? 1.0f : 0.7f)); g.drawRect (b, over && on ? 1.5f : 1.0f);
            const R chev (b.getRight() - 30.0f, b.getCentreY() - 8.0f, 16.0f, 16.0f);
            if (on) drawIcon (g, Icon::ChevDown, chev, fg);
            g.setColour (on ? front : (inverse ? Look::bandFg().withAlpha (0.55f) : Look::soft()));
            g.setFont (Look::caps (cap, false, 0.16f));
            g.drawFittedText (txt, b.withTrimmedRight (on ? 26.0f : 0.0f).reduced (6.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.55f);   // (long style names shrink to fit)
            return;
        }
        case Style::Box:
        {
            g.setColour (solid ? front : back.withAlpha (inverse ? 1.0f : 0.92f)); g.fillRect (b);
            g.setColour (fg); g.drawRect (b, over && on ? 1.6f : 1.0f);
            const auto tc = solid ? back : fg;
            const auto f = Look::caps (cap, false, 0.2f);
            const float tw = txt.isEmpty() ? 0.0f : Look::width (f, txt), gap = txt.isEmpty() ? 0.0f : 14.0f, isz = icon == Icon::None ? 0.0f : iconSize;
            const float x0 = b.getCentreX() - (isz + gap + tw) * 0.5f;
            if (isz > 0.0f) drawIcon (g, icon, R (x0, b.getCentreY() - isz * 0.5f, isz, isz), (red && on && icon == Icon::Stop) ? juce::Colour (0xffc8322b) : tc);
            g.setColour (tc); g.setFont (f);
            g.drawText (txt, R (x0 + isz + gap, b.getY(), tw + 4.0f, b.getHeight()), juce::Justification::centredLeft, false);
            return;
        }
        case Style::Tile:
        {
            const bool inv = on && (lit || over || down);
            g.setColour (inv ? Look::ink() : Look::paper()); g.fillRect (b);
            g.setColour (on ? Look::ink() : Look::grey()); g.drawRect (b, 1.0f);
            if (inv) { g.setGradientFill (Look::luster (b)); g.fillRect (b.getX(), b.getBottom() - 3.0f, b.getWidth(), 3.0f); }
            const auto tc = inv ? Look::paper() : (on ? Look::ink() : Look::grey());
            drawIcon (g, icon, R (b.getCentreX() - iconSize * 0.5f, b.getY() + b.getHeight() * 0.36f - iconSize * 0.5f, iconSize, iconSize), tc);
            g.setColour (tc);
            Look::text (g, txt, Look::caps (cap, false, 0.12f), R (b.getX(), b.getY() + b.getHeight() * 0.62f, b.getWidth(), b.getHeight() * 0.3f));
            return;
        }
    }
}

// ================================================================ TAKE TYPE
juce::String TypeList::getTooltip() { const int i = hover >= 0 ? hover : sel; return i >= 0 && i < 5 ? juce::String (kTypeTips[i]) : juce::String(); }

void TypeList::paint (juce::Graphics& g)
{
    static const Icon icons[] { Icon::Wave, Icon::Bars, Icon::Waves, Icon::Chops, Icon::Dots };
    for (int i = 0; i < 5; ++i)
    {
        const auto r = row (i).reduced (0.5f);
        const bool s = i == sel, h = i == hover;
        if (s)
        {
            g.setColour (Look::ink()); g.fillRect (r);
            // a cut gem at the right end, catching the light
            const juce::Point<float> c (r.getRight() - 28.0f, r.getCentreY());
            juce::Path d; d.startNewSubPath (c.x, c.y - 15.0f); d.lineTo (c.x + 10.0f, c.y - 4.0f); d.lineTo (c.x + 7.0f, c.y + 15.0f); d.lineTo (c.x - 7.0f, c.y + 15.0f); d.lineTo (c.x - 10.0f, c.y - 4.0f); d.closeSubPath();
            g.setGradientFill (Look::luster (d.getBounds())); g.fillPath (d);
            juce::Path lo; lo.addTriangle (c.x, c.y - 4.0f, c.x + 10.0f, c.y - 4.0f, c.x + 7.0f, c.y + 15.0f); Look::hatch (g, lo, 60.0f, 2.0f, Look::black().withAlpha (0.45f), 0.5f);
            juce::Path f; f.startNewSubPath (c.x - 10.0f, c.y - 4.0f); f.lineTo (c.x + 10.0f, c.y - 4.0f); f.startNewSubPath (c.x, c.y - 15.0f); f.lineTo (c.x - 3.0f, c.y - 4.0f); f.lineTo (c.x, c.y + 15.0f);
            g.setColour (Look::black().withAlpha (0.55f)); g.strokePath (f, juce::PathStrokeType (0.7f));
            g.setColour (Look::paper()); g.strokePath (d, juce::PathStrokeType (1.0f));
            Look::sparkle (g, { c.x - 16.0f, c.y - 11.0f }, 5.0f, Look::paper());
            g.setGradientFill (Look::luster (r, 0.9f)); g.fillRect (r.getX(), r.getBottom() - 2.0f, r.getWidth() - 56.0f, 2.0f);
        }
        else
        {
            g.setColour (Look::paper()); g.fillRect (r);
            g.setColour (Look::ink().withAlpha (h ? 1.0f : 0.75f)); g.drawRect (r, h ? 1.5f : 1.0f);
        }
        const auto c = s ? Look::paper() : Look::ink();
        drawIcon (g, icons[i], R (r.getX() + 24.0f, r.getCentreY() - 15.0f, 30.0f, 30.0f), c);
        g.setColour (c); Look::text (g, kTypeNames[i], Look::caps (17.5f, s, 0.24f), R (r.getX() + 86.0f, r.getY(), r.getWidth() - 120.0f, r.getHeight()), juce::Justification::centredLeft);
    }
}

// ================================================================ CHARACTER
R CharTiles::tile (int i) const
{
    static const float xs[] { 0.0f, 107.0f, 211.0f }, ws[] { 100.0f, 96.0f, 96.0f };
    return { xs[i] + 1.0f, 1.0f, ws[i] - 2.0f, (float) getHeight() - 2.0f };
}

juce::String CharTiles::getTooltip() { const int i = hover >= 0 ? hover : sel; return i >= 0 && i < 3 ? juce::String (kCharTips[i]) : juce::String(); }

void CharTiles::paint (juce::Graphics& g)
{
    for (int i = 0; i < 3; ++i)
    {
        const auto r = tile (i);
        const bool s = i == sel, h = i == hover;
        g.setColour (Look::paper()); g.fillRect (r);
        if (s)
        {
            g.setColour (Look::ink()); g.drawRect (r, 2.0f); g.drawRect (r.reduced (4.0f), 0.7f);
            g.setGradientFill (Look::luster (r, 0.35f)); g.fillRect (r.reduced (5.0f));
        }
        else { g.setColour (Look::ink().withAlpha (h ? 1.0f : 0.6f)); g.drawRect (r, 1.0f); }
        const juce::Point<float> c (r.getCentreX(), r.getY() + 38.0f);
        crystal (g, c, 24.0f, 50.0f, i == 0 ? 0.3f : i == 1 ? 0.26f : 0.36f, i, s);
        if (s) { Look::sparkle (g, { c.x + 15.0f, c.y - 18.0f }, 5.5f, Look::ink()); Look::sparkle (g, { c.x - 17.0f, c.y + 10.0f }, 3.5f, Look::ink()); }
        g.setColour (Look::ink());
        Look::text (g, kCharNames[i], Look::caps (13.0f, s, 0.06f), R (r.getX(), r.getBottom() - 32.0f, r.getWidth(), 22.0f));
    }
}

// ================================================================ ½x | 1x | 2x
void TempoSeg::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (0.5f);
    const float w = b.getWidth() / 3.0f;
    const juce::String names[] { juce::String::fromUTF8 ("\xc2\xbd" "x"), "1x", "2x" };
    for (int i = 0; i < 3; ++i)
    {
        const R c (b.getX() + w * (float) i, b.getY(), w, b.getHeight());
        if (i == sel) { g.setColour (Look::bandFg()); g.fillRect (c); g.setGradientFill (Look::luster (c)); g.fillRect (c.getX(), c.getBottom() - 3.0f, c.getWidth(), 3.0f); }
        g.setColour (i == sel ? Look::band() : Look::bandFg());
        Look::text (g, names[i], Look::caps (19.0f, i == sel, 0.04f), c);
    }
    g.setColour (Look::bandFg().withAlpha (0.7f)); g.drawRect (b, 1.0f);
    g.drawLine (b.getX() + w, b.getY(), b.getX() + w, b.getBottom(), 1.0f); g.drawLine (b.getX() + 2.0f * w, b.getY(), b.getX() + 2.0f * w, b.getBottom(), 1.0f);
}

// ================================================================ the landscape
R Stage::strip() const { return { 0.0f, (float) getHeight() - 45.0f, (float) getWidth(), 45.0f }; }
R Stage::wave() const { const auto s = strip(); return { 30.0f, s.getY() + 5.0f, (float) getWidth() - 336.0f, s.getHeight() - 10.0f }; }
double Stage::xToFrac (float x) const { const auto w = wave(); return juce::jlimit (0.0, 1.0, (double) ((x - w.getX()) / w.getWidth())); }
float Stage::fracToX (double f) const { const auto w = wave(); return w.getX() + (float) f * w.getWidth(); }

// the drawing: the shore at the end of the world. Paper sky, the moon, a colonnade receding into the white, the sea in hatched lines,
// and one tall cut crystal standing at the water with a crack through it, shards lifting off, sparkles. Our own drawing.
void Stage::paintArt (juce::Graphics& g, float w, float h)
{
    const auto ink = Look::ink(), paper = Look::paper();
    g.fillAll (paper);
    g.setTiledImageFill (grain(), 0, 0, 0.2f); g.fillRect (0.0f, 0.0f, w, h);
    juce::Random rnd (20261113);
    const float y0 = h * 0.70f;
    if (Look::dark)
    {   // the night: a field of fine stars, a few of them sparkling
        juce::Random st (7);
        for (int k = 0; k < 220; ++k)
        {
            const float x = st.nextFloat() * w, y = std::pow (st.nextFloat(), 1.4f) * (y0 - 6.0f), r = 0.4f + st.nextFloat() * st.nextFloat() * 1.3f;
            g.setColour (ink.withAlpha (0.25f + 0.6f * st.nextFloat())); g.fillEllipse (x - r, y - r, r * 2.0f, r * 2.0f);
        }
        for (int k = 0; k < 9; ++k) Look::sparkle (g, { st.nextFloat() * w, st.nextFloat() * (y0 * 0.8f) }, 3.0f + st.nextFloat() * 4.0f, ink.withAlpha (0.85f));
    }
    // the moon: a fine ring, its dark side in hatching (at night: lit, with a halo)
    {
        const juce::Point<float> mc (w * 0.905f, h * 0.24f); const float mr = h * 0.17f;
        const R mb (mc.x - mr, mc.y - mr, mr * 2.0f, mr * 2.0f);
        juce::Path moon; moon.addEllipse (mb);
        if (Look::dark)
        {
            for (int k = 6; k >= 1; --k) { g.setColour (ink.withAlpha (0.035f)); g.fillEllipse (mb.expanded (mr * 0.12f * (float) k)); }
            juce::ColourGradient mg (juce::Colour (0xfff6f2e6), mc.x - mr * 0.4f, mc.y - mr * 0.4f, juce::Colour (0xffc9c4b6), mc.x + mr, mc.y + mr, true);
            g.setGradientFill (mg); g.fillPath (moon);
            g.setGradientFill (Look::luster (mb, 0.35f)); g.fillPath (moon);
        }
        juce::Path cres; cres.addEllipse (mb); cres.addEllipse (mb.translated (mr * 0.38f, -mr * 0.12f)); cres.setUsingNonZeroWinding (false);
        { juce::Graphics::ScopedSaveState ss (g); g.reduceClipRegion (moon); Look::hatch (g, cres, 58.0f, 3.0f, (Look::dark ? Look::black() : ink).withAlpha (0.55f), 0.6f); }
        g.setColour (ink); g.strokePath (moon, juce::PathStrokeType (1.1f));
        g.setColour (ink.withAlpha (0.22f)); g.drawEllipse (mb.expanded (mr * 0.12f), 0.6f); g.drawEllipse (mb.expanded (mr * 0.26f), 0.5f);
    }
    // the sea: hatched lines, closer together toward the horizon, broken where the light lies on the water
    g.setColour (ink); g.fillRect (0.0f, y0 - 0.6f, w, 1.2f);
    {
        int n = 0;
        for (float y = y0 + 4.0f, gap = 3.2f; y < h; y += gap, gap *= 1.09f, ++n)
        {
            float x = -rnd.nextFloat() * 40.0f;
            while (x < w)
            {
                const float len = 18.0f + rnd.nextFloat() * (60.0f + (float) n * 9.0f), sp = 5.0f + rnd.nextFloat() * (14.0f + (float) n * 2.0f);
                const float cx = x + len * 0.5f, glint = std::abs (cx - w * 0.83f) < w * 0.07f ? 0.25f : 1.0f;   // the crystal's light on the water
                g.setColour (ink.withAlpha ((0.25f + 0.45f * rnd.nextFloat()) * glint));
                g.fillRect (x, y, len, 0.8f);
                x += len + sp;
            }
        }
    }
    // the colonnade: slim pillars receding toward the horizon under one thin black lintel, a floor of fine perspective lines
    {
        const juce::Point<float> vp (w * 0.47f, y0 - h * 0.03f);
        const float x0 = w * 0.012f, top0 = h * 0.035f, base0 = h * 0.97f, pw0 = w * 0.024f, lintel0 = h * 0.034f;
        struct Pil { float x, top, base, pw, s; };
        std::vector<Pil> ps;
        for (float s = 1.0f; s > 0.05f; s *= 0.8f)
        {
            Pil p { vp.x + (x0 - vp.x) * s, vp.y + (top0 - vp.y) * s, vp.y + (base0 - vp.y) * s, pw0 * s, s };
            if (p.x > w * 0.4f || p.pw < 2.0f) break;
            ps.push_back (p);
        }
        if (! ps.empty())
        {
            const auto& L = ps.back();
            // the floor: paper over the sea, fine lines running to the vanishing point
            juce::Path fl; fl.startNewSubPath (0.0f, base0 + (0.0f - x0) * (L.base - base0) / (L.x - x0)); fl.lineTo (L.x + L.pw, L.base); fl.lineTo (L.x + L.pw + w * 0.09f, h); fl.lineTo (0.0f, h); fl.closeSubPath();
            g.setColour (paper); g.fillPath (fl);
            { juce::Graphics::ScopedSaveState ss (g); g.reduceClipRegion (fl); g.setColour (ink.withAlpha (0.28f));
              for (int k = 0; k <= 14; ++k) g.drawLine (vp.x, vp.y, -w * 0.9f + (float) k * w * 0.11f, h + 10.0f, 0.5f);
              for (const auto& p : ps) g.drawLine (0.0f, p.base + 0.5f, p.x + p.pw * 2.0f, p.base + 0.5f, 0.5f); }
            g.setColour (ink); g.drawLine (L.x + L.pw, L.base, L.x + L.pw + w * 0.09f, h, 1.0f);
            for (int k = (int) ps.size() - 1; k >= 0; --k)
            {
                const auto& p = ps[(size_t) k];
                const R pr (p.x, p.top, p.pw, p.base - p.top);
                g.setColour (paper); g.fillRect (pr);
                juce::Path shade; shade.addRectangle (pr.withTrimmedLeft (p.pw * 0.6f)); Look::hatch (g, shade, 90.0f, juce::jmax (1.4f, 2.4f * p.s), ink.withAlpha (0.75f), 0.55f);
                g.setColour (ink.withAlpha (0.3f)); g.fillRect (p.x + p.pw * 0.28f, p.top, 0.5f, p.base - p.top);
                g.setColour (ink); g.drawRect (pr, 0.9f);
                g.fillRect (p.x - p.pw * 0.18f, p.top, p.pw * 1.36f, juce::jmax (1.5f, p.pw * 0.18f));    // the capital
                g.fillRect (p.x - p.pw * 0.18f, p.base - juce::jmax (1.5f, p.pw * 0.22f), p.pw * 1.36f, juce::jmax (1.5f, p.pw * 0.22f));   // the plinth
            }
            juce::Path lin; lin.startNewSubPath (0.0f, top0 - 1.0f); lin.lineTo (L.x + L.pw * 1.2f, L.top - 1.0f);
            lin.lineTo (L.x + L.pw * 1.2f, L.top - lintel0 * L.s); lin.lineTo (0.0f, top0 - lintel0 * 1.05f); lin.closeSubPath();
            g.setColour (ink); g.fillPath (lin);
            g.drawLine (0.0f, top0 - lintel0 * 1.05f - 5.0f, L.x + L.pw * 1.2f, L.top - lintel0 * L.s - 5.0f * L.s, 0.8f);
        }
    }
    // the crystal: a tall cut prism standing at the water, leaning a little, cracked through
    {
        const float H = h * 0.68f, W = h * 0.19f;
        const juce::Point<float> base (w * 0.83f, y0 + h * 0.025f);
        const auto tr = juce::AffineTransform::rotation (-0.07f).translated (base);
        auto P = [&] (float x, float y) { return juce::Point<float> (x, y).transformedBy (tr); };
        const auto T = P (0.0f, -H), RS = P (W * 0.5f, -H + W * 0.62f), RB = P (W * 0.46f, -W * 0.12f), B = P (W * 0.05f, 0.0f), LB = P (-W * 0.44f, -W * 0.09f), LS = P (-W * 0.5f, -H + W * 0.56f);
        const auto CT = P (-W * 0.07f, -H + W * 0.7f), CB = P (-W * 0.05f, -W * 0.03f);
        auto poly = [] (std::initializer_list<juce::Point<float>> pts) { juce::Path p; bool f = true; for (auto q : pts) { if (f) p.startNewSubPath (q); else p.lineTo (q); f = false; } p.closeSubPath(); return p; };
        const auto whole = poly ({ T, RS, RB, B, LB, LS }), left = poly ({ LS, CT, CB, B, LB }), right = poly ({ CT, RS, RB, B, CB }), tipL = poly ({ T, CT, LS }), tipR = poly ({ T, RS, CT });
        // its light: fine rays from the tip (at night: it glows)
        if (Look::dark)
        {
            for (int k = 7; k >= 1; --k) { g.setGradientFill (Look::luster (whole.getBounds(), 0.045f)); g.strokePath (whole, juce::PathStrokeType (8.0f * (float) k, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }
            juce::ColourGradient pool (juce::Colour (0xffc3e8ff).withAlpha (0.16f), base.x, y0 + 6.0f, juce::Colour (0xffc3e8ff).withAlpha (0.0f), base.x + W * 2.4f, y0 + 6.0f, true);
            g.setGradientFill (pool); g.fillEllipse (base.x - W * 2.4f, y0 - h * 0.05f, W * 4.8f, h * 0.2f);
        }
        for (int k = 0; k < 9; ++k) { const float a = -2.5f + 0.32f * (float) k; g.setColour (ink.withAlpha (0.1f)); g.drawLine ({ T, T + juce::Point<float> (std::cos (a), std::sin (a)) * (h * 0.6f) }, 0.6f); }
        // its reflection in the sea
        for (float y = y0 + 6.0f; y < h; y += 5.0f) { const float half = W * (0.25f + 0.25f * rnd.nextFloat()); g.setColour (ink.withAlpha (0.35f)); g.fillRect (base.x - half * 0.6f + rnd.nextFloat() * 8.0f, y, half, 1.2f); }
        g.setColour (paper); g.fillPath (whole);
        g.setGradientFill (Look::luster (whole.getBounds())); g.fillPath (left); g.fillPath (tipL);
        { juce::Graphics::ScopedSaveState ss (g); g.reduceClipRegion (left);   // refraction inside the lit face
          g.setColour (juce::Colours::white.withAlpha (0.75f)); g.fillPath (poly ({ LS, CT, P (-W * 0.3f, -H * 0.55f) }));
          g.setColour (Look::black().withAlpha (0.4f)); for (int k = 0; k < 6; ++k) { const float yy = -H * (0.15f + 0.12f * (float) k); g.drawLine ({ P (-W * 0.5f, yy), P (-W * 0.05f, yy - W * 0.35f) }, 0.5f); } }
        Look::hatch (g, right, 64.0f, 2.3f, ink.withAlpha (0.85f), 0.65f);
        Look::hatch (g, tipR, 64.0f, 1.6f, ink.withAlpha (0.9f), 0.65f);
        g.setColour (ink.withAlpha (0.7f)); g.drawLine ({ CT, CB }, 0.9f); g.drawLine ({ T, CT }, 0.9f); g.drawLine ({ LS, CT }, 0.7f); g.drawLine ({ CT, RS }, 0.7f);
        // the crack, and the shards lifting off it
        juce::Path crack; crack.startNewSubPath (P (-W * 0.5f, -H * 0.42f)); crack.lineTo (P (-W * 0.24f, -H * 0.45f)); crack.lineTo (P (-W * 0.12f, -H * 0.38f)); crack.lineTo (P (W * 0.06f, -H * 0.44f));
        crack.lineTo (P (W * 0.22f, -H * 0.37f)); crack.lineTo (P (W * 0.48f, -H * 0.41f));
        g.setColour (paper); g.strokePath (crack, juce::PathStrokeType (3.2f)); g.setColour (ink); g.strokePath (crack, juce::PathStrokeType (1.3f));
        juce::Path br; br.startNewSubPath (P (-W * 0.12f, -H * 0.38f)); br.lineTo (P (-W * 0.18f, -H * 0.31f)); br.lineTo (P (-W * 0.14f, -H * 0.27f));
        br.startNewSubPath (P (W * 0.22f, -H * 0.37f)); br.lineTo (P (W * 0.3f, -H * 0.47f)); g.strokePath (br, juce::PathStrokeType (0.8f));
        g.setColour (ink); g.strokePath (whole, juce::PathStrokeType (1.6f, juce::PathStrokeType::mitered));
        const juce::Point<float> sh[] { P (W * 0.85f, -H * 0.46f), P (W * 1.15f, -H * 0.55f), P (W * 0.72f, -H * 0.6f), P (W * 1.35f, -H * 0.42f) };
        const float ss[] { 11.0f, 7.0f, 5.0f, 4.0f };
        for (int k = 0; k < 4; ++k)
        {
            const auto c = sh[k]; const float s = ss[k];
            auto d = poly ({ { c.x, c.y - s }, { c.x + s * 0.7f, c.y - s * 0.1f }, { c.x + s * 0.2f, c.y + s * 0.8f }, { c.x - s * 0.6f, c.y + s * 0.2f } });
            g.setGradientFill (Look::luster (d.getBounds())); g.fillPath (d);
            g.setColour (ink); g.strokePath (d, juce::PathStrokeType (0.9f));
        }
        Look::sparkle (g, T.translated (-2.0f, -14.0f), 13.0f, ink);
        Look::sparkle (g, P (W * 0.62f, -H * 0.75f), 7.0f, ink);
        Look::sparkle (g, P (-W * 0.7f, -H * 0.3f), 5.0f, ink);
        Look::sparkle (g, { base.x - W * 1.6f, y0 + h * 0.08f }, 6.0f, ink);
        Look::sparkle (g, { base.x + W * 1.4f, y0 + h * 0.15f }, 4.5f, ink);
        // two small stones at its foot
        crystal (g, P (-W * 0.95f, -W * 0.18f), 11.0f, 22.0f, -0.5f, 0, true);
        crystal (g, P (W * 0.9f, -W * 0.12f), 9.0f, 17.0f, 0.6f, 2, false);
    }
    // the frame's hairline
    g.setColour (ink); g.drawRect (0.0f, 0.0f, w, h, 1.0f);
}

void Stage::paint (juce::Graphics& g)
{
    const float aw = (float) getWidth(), ah = (float) getHeight() - 45.0f;
    if (art.isNull() || artW != getWidth() || artH != getHeight() || artDark != Look::dark)
    {
        artW = getWidth(); artH = getHeight(); artDark = Look::dark;
        art = juce::Image (juce::Image::RGB, juce::jmax (1, (int) aw * 2), juce::jmax (1, (int) ah * 2), true);
        juce::Graphics ag (art); ag.addTransform (juce::AffineTransform::scale (2.0f));
        paintArt (ag, aw, ah);
    }
    const R artR (0.0f, 0.0f, aw, ah);
    g.drawImage (art, artR);
    // the line above the words: — NO SOUND LOADED —
    {
        const auto f = Look::caps (13.0f, false, 0.22f);
        const float tw = Look::width (f, status), cx = aw * 0.5f, cy = 73.0f;
        g.setColour (Look::paper().withAlpha (0.85f)); g.fillRect (cx - tw * 0.5f - 64.0f, cy - 11.0f, tw + 128.0f, 22.0f);
        g.setColour (Look::soft());
        Look::text (g, status, f, R (cx - tw * 0.5f - 4.0f, cy - 10.0f, tw + 8.0f, 20.0f));
        g.setColour (Look::ink());
        g.fillRect (cx - tw * 0.5f - 58.0f, cy - 0.5f, 40.0f, 1.0f); g.fillRect (cx + tw * 0.5f + 18.0f, cy - 0.5f, 40.0f, 1.0f);
    }
    {   // the big words
        auto f = Look::caps (28.5f, false, 0.2f);
        float tw = Look::width (f, headline);
        if (tw > 900.0f) { f = Look::caps (28.5f * 900.0f / tw, false, 0.2f); tw = 900.0f; }
        const R r (aw * 0.5f - 470.0f, 101.0f, 940.0f, 42.0f);
        g.setColour (Look::paper().withAlpha (0.85f)); g.fillRect (r.withSizeKeepingCentre (tw + 40.0f, 40.0f));
        g.setColour (Look::ink()); Look::text (g, headline, f, r);
    }
    if (dragOver)
    {
        g.setGradientFill (Look::luster (artR, 0.3f)); g.fillRect (artR);
        g.setColour (Look::ink()); g.drawRect (artR.reduced (4.0f), 2.0f); g.drawRect (artR.reduced (9.0f), 0.7f);
    }
    // the waveform strip: a solid black band, the sound in white, its TRIM part in the gem's luster
    const auto s = strip();
    g.setColour (Look::band()); g.fillRect (s);
    const auto w = wave();
    const float cy = w.getCentreY();
    auto src = proc.getSource();
    const double t0 = trimMode ? dragT0 : proc.trimStart(), t1 = trimMode ? dragT1 : proc.trimEnd();
    if (src == nullptr || src->frames <= 0)
    {
        g.setColour (Look::bandFg().withAlpha (0.4f)); g.fillRect (w.getX(), cy - 0.5f, w.getWidth(), 1.0f);
    }
    else
    {
        const int W = juce::jmax (1, (int) w.getWidth());
        if (pathFor != src.get() || pathW != W)
        {
            pathFor = src.get(); pathW = W; wavePath.clear();
            for (int px = 0; px < W; ++px)
            {
                const int64 a = (int64) ((double) px / W * (double) src->frames), b = juce::jmax (a + 1, (int64) ((double) (px + 1) / W * (double) src->frames));
                float m = 0.0f;
                for (int ch = 0; ch < src->channels; ++ch) { const auto q = src->peaks.query (src->audio, ch, a, juce::jmin (b, src->frames)); m = juce::jmax (m, std::abs (q.mn), std::abs (q.mx)); }
                const float hh = juce::jmax (0.5f, juce::jmin (1.0f, m) * w.getHeight() * 0.5f);
                wavePath.addRectangle ((float) px, -hh, 0.8f, hh * 2.0f);
            }
        }
        const auto tr = juce::AffineTransform::translation (w.getX(), cy);
        {
            juce::Graphics::ScopedSaveState ss (g);
            g.setColour (Look::bandFg().withAlpha (0.28f)); g.fillPath (wavePath, tr);
            g.reduceClipRegion (juce::Rectangle<float> (fracToX (t0), w.getY() - 4.0f, fracToX (t1) - fracToX (t0), w.getHeight() + 8.0f).toNearestInt());
            g.setGradientFill (Look::luster (w)); g.fillPath (wavePath, tr);
        }
        if (trimMode)
            for (double f : { t0, t1 })
            {
                const float x = fracToX (f);
                g.setColour (Look::bandFg()); g.fillRect (x - 1.0f, w.getY() - 3.0f, 2.0f, w.getHeight() + 6.0f);
                juce::Path d; d.addTriangle (x - 5.0f, w.getY() - 6.0f, x + 5.0f, w.getY() - 6.0f, x, w.getY()); g.fillPath (d);
            }
    }
    float px = fracToX (t0);
    if (src != nullptr && proc.sourcePlaying()) { const double pos = proc.sourcePosition(); if (pos >= 0.0 && src->seconds() > 0.0) px = fracToX (pos / src->seconds()); }
    g.setColour (Look::bandFg()); g.fillRect (px - 0.75f, w.getY() + 2.0f, 1.5f, w.getHeight() - 4.0f);
    Look::sparkle (g, { px, cy }, 6.0f, Look::bandFg());
}

void Stage::mouseDown (const juce::MouseEvent& e)
{
    if (! strip().contains (e.position) || proc.getSource() == nullptr || proc.recording()) return;
    if (! trimMode)
    {   // a click on the waveform: hear the sound (its TRIM part) / stop
        if (wave().expanded (6.0f, 0.0f).contains (e.position)) { proc.playSource (! proc.sourcePlaying()); repaint(); }
        return;
    }
    const float x0 = fracToX (dragT0), x1 = fracToX (dragT1);
    if (std::abs (e.position.x - x0) < 9.0f) trimDrag = 1;
    else if (std::abs (e.position.x - x1) < 9.0f) trimDrag = 2;
    else { trimDrag = 3; trimAnchor = xToFrac (e.position.x); dragT0 = dragT1 = trimAnchor; }
    repaint();
}

void Stage::mouseDrag (const juce::MouseEvent& e)
{
    if (! trimMode || trimDrag == 0) return;
    const double f = xToFrac (e.position.x);
    if (trimDrag == 1) dragT0 = juce::jmin (f, dragT1 - 0.01);
    else if (trimDrag == 2) dragT1 = juce::jmax (f, dragT0 + 0.01);
    else { dragT0 = juce::jmin (trimAnchor, f); dragT1 = juce::jmax (trimAnchor, f); }
    repaint();
}

void Stage::mouseUp (const juce::MouseEvent&)
{
    if (! trimMode || trimDrag == 0) return;
    trimDrag = 0;
    if (dragT1 - dragT0 < 0.01) { dragT0 = proc.trimStart(); dragT1 = proc.trimEnd(); repaint(); return; }
    proc.setTrim (dragT0, dragT1);
    repaint();
}

bool Stage::isInterestedInFileDrag (const juce::StringArray& files) { for (auto& f : files) if (source::canLoad (juce::File (f))) return true; return false; }
void Stage::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false; repaint();
    for (auto& f : files) if (source::canLoad (juce::File (f))) { if (onFile) onFile (juce::File (f)); return; }
}
bool Stage::isInterestedInTextDrag (const juce::String& t) { const auto s = t.trim().upToFirstOccurrenceOf ("\n", false, false); return juce::File::isAbsolutePath (s) && source::canLoad (juce::File (s)); }
void Stage::textDropped (const juce::String& t, int, int)
{
    dragOver = false; repaint();
    const auto s = t.trim().upToFirstOccurrenceOf ("\n", false, false);
    if (juce::File::isAbsolutePath (s) && onFile) onFile (juce::File (s));
}

// ================================================================ GENERATE / VARY
void GenerateButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    const bool on = isEnabled();
    const auto ink = on ? Look::ink() : Look::grey(), onGem = on ? Look::black() : Look::grey();   // (lines on the luster stay black in both looks)
    const auto r = getLocalBounds().toFloat().reduced (8.0f, 6.0f);
    const float tip = r.getHeight() * 0.52f;
    const auto shape = gem (r, tip);
    // the cut gem: the luster body, its facets lit and hatched, ink lines (at night: a halo of its own light)
    if (on && Look::dark) for (int k = 5; k >= 1; --k) { g.setGradientFill (Look::luster (r, 0.05f)); g.strokePath (shape, juce::PathStrokeType (7.0f * (float) k, juce::PathStrokeType::mitered)); }
    g.setColour (Look::paper()); g.fillPath (shape);
    if (on) { g.setGradientFill (Look::luster (r)); g.fillPath (shape); }
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (shape);
        const float x = r.getX(), y = r.getY(), w = r.getWidth(), h = r.getHeight(), my = r.getCentreY();
        juce::Path facets, lo;
        const int n = 8;
        for (int i = 0; i <= n; ++i)
        {
            const float fx = x + tip + (w - 2.0f * tip) * (float) i / (float) n;
            const float kx = fx + (i % 2 == 0 ? 1.0f : -1.0f) * 22.0f;
            facets.startNewSubPath (fx, y); facets.lineTo (kx, my - h * 0.12f); facets.lineTo (fx + (i % 2 == 0 ? -1.0f : 1.0f) * 10.0f, y + h);
            if (i < n) { const float nx = x + tip + (w - 2.0f * tip) * (float) (i + 1) / (float) n; if (i % 2 == 1) lo.addTriangle (fx, y + h, nx, y + h, (fx + nx) * 0.5f, my + h * 0.08f); }
        }
        facets.startNewSubPath (x, my); facets.lineTo (x + w, my);
        facets.startNewSubPath (x + tip, y); facets.lineTo (x + tip * 1.7f, my); facets.lineTo (x + tip, y + h);
        facets.startNewSubPath (x + w - tip, y); facets.lineTo (x + w - tip * 1.7f, my); facets.lineTo (x + w - tip, y + h);
        juce::Path lt; lt.addTriangle (x, my, x + tip, y + h, x + tip * 1.7f, my); lt.addTriangle (x + w, my, x + w - tip, y + h, x + w - tip * 1.7f, my); lo.addPath (lt);
        Look::hatch (g, lo, 62.0f, 2.6f, onGem.withAlpha (on ? 0.4f : 0.25f), 0.6f);
        if (on) { juce::Path hi; hi.addTriangle (x, my, x + tip, y, x + tip * 1.7f, my); g.setColour (juce::Colours::white.withAlpha (0.7f)); g.fillPath (hi);
                  g.setColour (juce::Colours::white.withAlpha (0.5f)); g.fillRect (x, y, w, h * 0.06f); }
        g.setColour (onGem.withAlpha (0.45f)); g.strokePath (facets, juce::PathStrokeType (0.8f));
        if (proc.busy())
        {
            const float pw = w * juce::jlimit (0.0f, 1.0f, proc.progress());
            g.setColour (Look::black().withAlpha (0.12f)); g.fillRect (x, y, pw, h);
            const float sx = x + std::fmod (phase * 300.0f, w + 200.0f) - 100.0f;
            juce::ColourGradient sh (juce::Colours::white.withAlpha (0.0f), sx - 70.0f, y, juce::Colours::white.withAlpha (0.0f), sx + 70.0f, y, false); sh.addColour (0.5, juce::Colours::white.withAlpha (0.8f));
            g.setGradientFill (sh); g.fillRect (sx - 70.0f, y, 140.0f, h);
        }
        if (on && (over || down)) { g.setColour (Look::black().withAlpha (down ? 0.12f : 0.05f)); g.fillRect (r); }
    }
    g.setColour (ink); g.strokePath (shape, juce::PathStrokeType (over && on ? 2.4f : 1.7f, juce::PathStrokeType::mitered));
    g.setColour (onGem.withAlpha (0.35f)); g.strokePath (gem (r.reduced (8.0f, 7.0f), tip - 5.0f), juce::PathStrokeType (0.8f));
    if (on) { Look::sparkle (g, { r.getX() + tip * 0.75f, r.getY() + 4.0f }, 10.0f, Look::ink()); Look::sparkle (g, { r.getRight() - tip * 0.6f, r.getBottom() - 6.0f }, 7.0f, Look::ink()); }
    const auto label = proc.busy() ? juce::String ("GENERATING") : juce::String ("GENERATE");
    const auto f = Look::caps (42.0f, true, 0.34f);
    g.setColour (onGem);
    Look::text (g, label, f, r.translated (0.0f, 1.0f));
}

void VaryButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    const bool on = isEnabled();
    const auto ink = on ? Look::ink() : Look::grey();
    const auto r = getLocalBounds().toFloat().reduced (2.0f);
    const auto shape = gem (r, 24.0f);
    g.setColour (on && down ? Look::ink() : Look::paper()); g.fillPath (shape);
    if (on && over && ! down) { g.setGradientFill (Look::luster (r, 0.6f)); g.fillPath (shape); }
    g.setColour (ink); g.strokePath (shape, juce::PathStrokeType (over && on ? 1.8f : 1.2f, juce::PathStrokeType::mitered));
    g.setColour (ink.withAlpha (0.35f)); g.strokePath (gem (r.reduced (6.0f, 5.0f), 20.0f), juce::PathStrokeType (0.7f));
    const auto c = on && down ? Look::paper() : ink;
    drawIcon (g, Icon::Infinity, R (r.getX() + 72.0f, r.getCentreY() - 14.0f, 30.0f, 28.0f), c);
    g.setColour (c);
    Look::text (g, "VARY", Look::caps (18.0f, false, 0.48f), R (r.getX() + 122.0f, r.getY(), 120.0f, r.getHeight()), juce::Justification::centredLeft);
}

// ================================================================ TAKES
void TakesStrip::showCurrent()
{
    const int c = proc.currentIndex(), n = (int) proc.takes().size();
    if (c < first) first = c;
    if (c >= first + kShown) first = c - kShown + 1;
    first = juce::jlimit (0, juce::jmax (0, n - kShown), first);
    repaint();
}

juce::String TakesStrip::getTooltip()
{
    for (auto& h : hits)
    {
        if (h.star.contains (mouse)) return "Star this take (keep your favourites easy to find).";
        if (h.card.contains (mouse)) return "Click to play this take on the keys. Right-click: star or delete it.";
    }
    return proc.takes().empty() ? juce::String ("Every GENERATE and VARY adds a take here.") : juce::String ("Your takes: click one to play it on the keys, right-click to star or delete it. Scroll for more.");
}

void TakesStrip::paint (juce::Graphics& g)
{
    hits.clear();
    const auto& takes = proc.takes();
    const auto b = getLocalBounds().toFloat();
    if (takes.empty())
    {
        g.setColour (Look::soft()); Look::text (g, "Your takes appear here.", Look::italic (22.0f), b);
        return;
    }
    first = juce::jlimit (0, juce::jmax (0, (int) takes.size() - kShown), first);
    const float gap = 8.0f, cw = (b.getWidth() - gap * (float) (kShown - 1)) / (float) kShown;
    for (int k = 0; k < kShown; ++k)
    {
        const int i = first + k;
        if (i >= (int) takes.size()) break;
        const auto& s = takes[(size_t) i];
        const R c (b.getX() + (cw + gap) * (float) k, b.getY() + 2.0f, cw, b.getHeight() - 4.0f);
        const bool cur = i == proc.currentIndex(), hov = c.contains (mouse.toFloat());
        g.setColour (cur ? Look::ink() : Look::paper()); g.fillRect (c);
        g.setColour (Look::ink().withAlpha (cur || hov ? 1.0f : 0.5f)); g.drawRect (c, hov && ! cur ? 1.4f : 1.0f);
        const auto fg = cur ? Look::paper() : Look::ink();
        const R star (c.getRight() - 28.0f, c.getY() + 6.0f, 20.0f, 20.0f);
        g.setColour (fg);
        Look::text (g, "TAKE " + juce::String (s.index), Look::caps (12.5f, cur, 0.14f), R (c.getX() + 12.0f, c.getY() + 4.0f, c.getWidth() - 44.0f, 22.0f), juce::Justification::centredLeft);
        g.setColour (cur ? Look::paper().withAlpha (0.75f) : Look::soft());
        Look::text (g, juce::String (typeName (proc.slotType (i))) + dot() + proc.slotKey (i), Look::italic (19.0f), R (c.getX() + 12.0f, c.getY() + 25.0f, c.getWidth() - 20.0f, 22.0f), juce::Justification::centredLeft);
        juce::Path p; p.addStar (star.getCentre(), 4, 2.6f, 9.0f, 0.0f);
        if (s.recipe.favourite) { g.setGradientFill (Look::luster (star)); g.fillPath (p); g.setColour (fg); g.strokePath (p, juce::PathStrokeType (0.9f)); }
        else { g.setColour (fg.withAlpha (hov || cur ? 0.7f : 0.3f)); g.strokePath (p, juce::PathStrokeType (0.9f)); }
        if (cur && s.take == nullptr) { g.setGradientFill (Look::luster (c)); g.fillRect (c.getX() + 1.0f, c.getBottom() - 3.0f, (c.getWidth() - 2.0f) * proc.progress(), 2.0f); }
        else if (cur && proc.previewPlaying()) { g.setGradientFill (Look::luster (c)); g.fillRect (c.getX() + 1.0f, c.getBottom() - 3.0f, (c.getWidth() - 2.0f) * proc.previewPosition(), 2.0f); }
        hits.push_back ({ c.toNearestInt(), star.toNearestInt(), i });
    }
}

void TakesStrip::mouseDown (const juce::MouseEvent& e)
{
    for (auto& h : hits)
    {
        if (! h.card.contains (e.getPosition())) continue;
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu m;
            m.addItem (1, proc.takes()[(size_t) h.index].recipe.favourite ? "Unstar" : "Star");
            m.addItem (2, "Delete this take");
            const int idx = h.index;
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [sp = juce::Component::SafePointer<TakesStrip> (this), idx] (int r)
            {
                if (sp == nullptr) return;
                if (r == 1) sp->proc.toggleFavourite (idx);
                else if (r == 2) sp->proc.deleteTake (idx);
                sp->repaint();
            });
            return;
        }
        if (h.star.contains (e.getPosition())) proc.toggleFavourite (h.index);
        else proc.selectTake (h.index);
        repaint();
        return;
    }
}

// ================================================================ the keys
void Keys::drawWhiteNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool down, bool over, juce::Colour, juce::Colour)
{
    const auto r = area;
    g.setColour (Look::keyWhite()); g.fillRect (r);
    if (lit[(size_t) note] || down)
    {
        g.setGradientFill (Look::luster (r.withTrimmedTop (r.getHeight() * 0.3f), down ? 1.0f : 0.85f)); g.fillRect (r.withTrimmedTop (r.getHeight() * (down ? 0.0f : 0.45f)));
        if (down) Look::sparkle (g, { r.getCentreX(), r.getBottom() - 30.0f }, 6.0f, Look::black());
    }
    else if (over) { juce::Path p; p.addRectangle (r); Look::hatch (g, p, 60.0f, 4.0f, Look::black().withAlpha (0.15f), 0.5f); }
    g.setColour (Look::black()); g.fillRect (r.getRight() - 0.5f, r.getY(), 1.0f, r.getHeight());
    if (note % 12 == 0)
    {
        g.setColour (juce::Colour (0xff5c5a55));
        Look::text (g, "C" + juce::String (note / 12), Look::caps (12.5f, false, 0.0f), R (r.getX() + 5.0f, r.getBottom() - 22.0f, r.getWidth() - 6.0f, 16.0f), juce::Justification::centredLeft);
    }
}

void Keys::drawBlackNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool down, bool over, juce::Colour)
{
    const auto r = area.reduced (0.5f, 0.0f);
    g.setColour (Look::black()); g.fillRect (r);
    g.setColour (juce::Colours::white.withAlpha (0.18f)); g.fillRect (r.getX() + r.getWidth() * 0.2f, r.getY(), 0.8f, r.getHeight() * 0.85f);
    if (lit[(size_t) note] || down) { g.setGradientFill (Look::luster (r, 0.95f)); g.fillRect (r.reduced (2.0f, 0.0f).withTrimmedTop (r.getHeight() * (down ? 0.05f : 0.55f)).withTrimmedBottom (2.0f)); }
    else if (over) { g.setColour (juce::Colours::white.withAlpha (0.2f)); g.fillRect (r.reduced (2.0f, 0.0f).withTrimmedBottom (2.0f)); }
}

void Toast::paint (juce::Graphics& g)
{
    const auto f = Look::caps (12.5f, false, 0.08f);
    const float w = juce::jmin ((float) getWidth(), Look::width (f, text) + 40.0f);
    const R b (((float) getWidth() - w) * 0.5f, 1.0f, w, (float) getHeight() - 2.0f);
    g.setColour (Look::ink()); g.fillRect (b);
    g.setGradientFill (Look::luster (b)); g.fillRect (b.getX(), b.getBottom() - 2.0f, b.getWidth(), 2.0f);
    g.setColour (Look::paper()); g.setFont (f); g.drawFittedText (text, b.reduced (16.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.8f);
}

// ================================================================ the editor
SliceEditor::SliceEditor (SliceProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    Look::dark = proc.uiTheme.load() == 0;   // this session's look (0 = DARK; any other = the LIGHT one)
    lnf.applyLook();
    setLookAndFeel (&lnf);
    addAndMakeVisible (canvas);
    canvas.paintFn = [this] (juce::Graphics& g) { paintCanvas (g); };
    auto add = [this] (juce::Component& c, const juce::String& tip)
    {
        canvas.addAndMakeVisible (c);
        if (auto* t = dynamic_cast<juce::SettableTooltipClient*> (&c)) if (tip.isNotEmpty()) t->setTooltip (tip);
    };
    // header
    keyBtn.cap = 15.0f; tempoBtn.cap = 17.0f;
    for (auto* b : { &keyBtn, &tempoBtn, &gearBtn, &playSrcBtn, &trimBtn }) b->inverse = true;   // (on the black header / the black strip)
    add (keyBtn, "KEY: AUTO uses the key found in your sound. Click to pick another one.");
    keyBtn.onClick = [this] { keyMenu(); };
    add (tempoBtn, {});
    tempoBtn.onClick = [this] { tempoMenu(); };
    add (tempoSw, "TEMPO: make the takes at half, the same or double the tempo (65 or 260 when FL is at 130). DRAG MIDI lands in FL exactly as you hear it.");
    tempoSw.onChange = [this] (int c)
    {
        proc.setTempoMul (c); proc.updateHostDisplay (juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged (true)); refresh();
        toast ("TEMPO " + ghosty::tempomul::multName (c) + ": takes at " + ghosty::tempomul::bpmText (proc.effectiveTempo()) + " BPM");
    };
    gearBtn.iconSize = 30.0f;
    add (gearBtn, "Settings: where RECORD records from, CLEAR, the key lock, TRIM, how Ghost Slice works.");
    gearBtn.onClick = [this] { settingsMenu(); };
    lookBtn.iconSize = 24.0f; lookBtn.inverse = true;
    add (lookBtn, {});
    lookBtn.onClick = [this] { setDark (! gslice::ui::Look::dark); };
    canvas.addChildComponent (unlock); unlock.setBounds (0, 0, 0, 0);
    unlock.onChange = [this] { proc.unlockedNow(); refresh(); };
    // the landscape
    add (stage, "Drop a vocal, a sentence or any sound here. Ghost Slice listens to it and turns it into instruments, pads, chops and textures.");
    stage.onFile = [this] (const juce::File& f) { proc.loadAudioAsync (f); };
    add (loadBtn, "LOAD...: pick a sound from your Mac (WAV, AIFF, FLAC, MP3). Or drop one anywhere on the picture.");
    loadBtn.onClick = [this] { loadClicked(); };
    add (recordBtn, "RECORD: sing or play into Ghost Slice (from the track or the Mac's mic; the gear menu picks). Press again to stop.");
    recordBtn.onClick = [this] { recordClicked(); };
    playSrcBtn.cap = 15.0f; trimBtn.cap = 15.0f; playSrcBtn.iconSize = 16.0f;
    add (playSrcBtn, "PLAY: hear your sound (the part TRIM chose).");
    playSrcBtn.onClick = [this] { proc.playSource (! proc.sourcePlaying()); stage.repaint(); };
    add (trimBtn, "TRIM: drag on the waveform to choose the part of your sound GENERATE uses. Right-click: the whole sound again.");
    trimBtn.onClick = [this]
    {
        if (juce::ModifierKeys::currentModifiers.isPopupMenu()) return;
        stage.trimMode = ! stage.trimMode; trimBtn.lit = stage.trimMode;
        stage.dragT0 = proc.trimStart(); stage.dragT1 = proc.trimEnd();
        if (stage.trimMode) toast ("TRIM: drag on the waveform to pick the part GENERATE uses");
        stage.repaint(); trimBtn.repaint();
    };
    trimBtn.onStateChange = [this]
    {
        if (trimBtn.isDown() && juce::ModifierKeys::currentModifiers.isPopupMenu())
        {
            juce::PopupMenu m; m.addItem (1, "Use the whole sound (reset TRIM)", proc.isTrimmed());
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&trimBtn), [sp = juce::Component::SafePointer<SliceEditor> (this)] (int r)
            { if (sp != nullptr && r == 1) { sp->proc.setTrim (0.0, 1.0); sp->stage.dragT0 = 0.0; sp->stage.dragT1 = 1.0; sp->stage.repaint(); } });
        }
    };
    // TAKE TYPE / CHARACTER / STYLE / LENGTH
    add (typeTiles, {});
    typeTiles.onChange = [this] (int i) { proc.type = (gen::Type) i; refresh(); };
    add (charTiles, {});
    charTiles.onChange = [this] (int i) { proc.character = (gen::Character) i; };
    styleBtn.cap = 15.0f; lengthBtn.cap = 15.0f;
    add (styleBtn, {});
    styleBtn.onClick = [this] { styleMenu(); };
    add (lengthBtn, "LENGTH: how many bars the melody, chords or phrase last (1, 2, 4, 8 or 16). Longer ones develop, they never just loop.");
    lengthBtn.onClick = [this] { lengthMenu(); };
    // NATURAL <-> MUSICAL, TONE / the type's knob / SPACE
    add (musicalSl, "NATURAL <-> MUSICAL. Left: the voice keeps its own pitches and character. Right: everything snaps to your key and chords, more processed.");
    musicalSl.setRange (0.0, 1.0, 0.01); musicalSl.onValueChange = [this] { if (! refreshing) proc.musical = musicalSl.getValue(); };
    musicalSl.setDoubleClickReturnValue (true, 0.7);
    musicalSl.setSliderSnapsToMousePosition (false);
    add (toneKnob, "TONE: dark (left) to bright (right). Double-click: the middle.");
    toneKnob.setRange (-1.0, 1.0, 0.01); toneKnob.setDoubleClickReturnValue (true, 0.0); toneKnob.onValueChange = [this] { if (! refreshing) proc.setTone ((float) toneKnob.getValue()); };
    add (spaceKnob, "SPACE: how much of the lush reverb, from dry to huge. Double-click: the default.");
    spaceKnob.setRange (0.0, 1.0, 0.01); spaceKnob.setDoubleClickReturnValue (true, 0.4);
    spaceKnob.onValueChange = [this] { if (! refreshing) proc.setSpace ((float) spaceKnob.getValue()); };
    add (typeKnob, {});
    typeKnob.setRange (0.0, 1.0, 0.01);
    typeKnob.onValueChange = [this] { if (! refreshing) proc.setKnob (typeKnob.getValue(), ! typeKnob.isMouseButtonDown()); };
    typeKnob.onDragEnd = [this] { proc.setKnob (typeKnob.getValue(), true); };
    for (auto* k : { &toneKnob, &spaceKnob, &typeKnob }) { k->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true); k->setMouseDragSensitivity (180); }
    // GENERATE / VARY
    add (generateBtn, "GENERATE: make a new take from your sound. Every press is different. Each take is kept under TAKES.");
    generateBtn.onClick = [this] { proc.generate(); strip.showCurrent(); };
    add (varyBtn, "VARY: a variation of the take you are on (the same idea, new details).");
    varyBtn.onClick = [this] { proc.vary(); strip.showCurrent(); };
    // TAKES
    add (strip, {});
    prevBtn.style = nextBtn.style = Btn::Style::Box; prevBtn.setButtonText ({}); nextBtn.setButtonText ({});
    prevBtn.iconSize = nextBtn.iconSize = 18.0f;
    add (prevBtn, "The previous take."); add (nextBtn, "The next take.");
    prevBtn.onClick = [this] { proc.stepTake (-1); strip.showCurrent(); refresh(); };
    nextBtn.onClick = [this] { proc.stepTake (1); strip.showCurrent(); refresh(); };
    for (auto* b : { (Btn*) &playBtn, (Btn*) &dragWav, (Btn*) &dragMidi, (Btn*) &dragChords, (Btn*) &saveBtn }) { b->cap = 12.5f; b->iconSize = 24.0f; }
    add (playBtn, "PLAY: hear the take (its melody, chords or phrase). Your MIDI keys play it too.");
    playBtn.onClick = [this] { proc.playPreview (! proc.previewPlaying()); };
    add (dragWav, "Drag the take into FL Studio (or Finder) as a WAV, with the TONE and SPACE you hear.");
    dragWav.onDragOut = [this] { dragWavOut(); };
    add (dragMidi, "Drag the melody (or the PAD's chords, or the CHOPS phrase) of this take as MIDI: one track.");
    dragMidi.onDragOut = [this] { dragMidiOut(); };
    add (dragChords, "Drag the chords of this take as MIDI: one track.");
    dragChords.onDragOut = [this] { dragChordsOut(); };
    add (saveBtn, "SAVE TAKE: the take (WAV + its MIDI + a CHORDS MIDI) into Documents/Ghosty/Ghost Slice/Takes.");
    saveBtn.onClick = [this] { saveTakeClicked(); };
    // the keys
    add (keys, "Play the take here, or from your MIDI keyboard / FL Studio's piano roll.");
    keys.setAvailableRange (kLowKey, kHighKey); keys.setLowestVisibleKey (kLowKey); keys.setOctaveForMiddleC (5);   // (FL Studio names: middle C = C5)
    keys.setScrollButtonsVisible (false); keys.setVelocity (0.8f, true);
    keys.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colours::transparentBlack);
    keys.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
    canvas.addChildComponent (toastView);
    setWantsKeyboardFocus (true);
    proc.onChanged = [this] { refresh(); };
    proc.onToast = [this] (const juce::String& s) { toast (s); };
    setResizable (true, true);
    setResizeLimits (760, 520, 3840, 2400);   // any shape: the screen fills the window (resized())
    // Brandon 2026-11-06: open at the size that fits his 1440 x 900 MacBook Pro inside FL's plug-in window (never wider than 1240 or
    // taller than 780): 1140 x 760. Always this size on open; drag it to any size or shape and the screen fills it.
    setSize (kDefaultW, kDefaultH);
    refresh();
    startTimerHz (30);
}

SliceEditor::~SliceEditor()
{
    stopTimer();
    proc.onChanged = nullptr; proc.onToast = nullptr;
    setLookAndFeel (nullptr);
}

void SliceEditor::toast (const juce::String& s)
{
    toastText = s; toastUntil = juce::Time::getMillisecondCounterHiRes() + 4500.0;
    toastView.text = s; toastView.setVisible (s.isNotEmpty()); toastView.toFront (false); toastView.repaint();
}

juce::String SliceEditor::keyText() const
{
    if (proc.keyTonic >= 0) return keyName (proc.keyTonic, proc.keyMinor);
    const auto k = proc.detectedKey();
    return proc.keyKnown() ? keyName (k.tonic, k.minor) : juce::String ("AUTO");
}

juce::String SliceEditor::headline() const
{
    if (proc.recording()) return "RECORDING  " + juce::String (proc.recordedSeconds(), 1) + " S";
    if (proc.isLoading()) return "LISTENING...";
    if (auto s = proc.getSource())
    {
        auto n = s->file.getFileNameWithoutExtension();
        if (n.isEmpty()) n = "YOUR RECORDING";
        return n.toUpperCase();
    }
    return "DROP A VOCAL OR ANY SOUND";
}

juce::String SliceEditor::statusText() const
{
    if (proc.recording()) return "RECORDING FROM " + proc.recordSourceText().toUpperCase();
    if (proc.isLoading()) return "LOADING";
    if (proc.busy()) { auto t = proc.busyText().toUpperCase(); return t.isNotEmpty() ? t : juce::String ("WORKING"); }
    if (auto s = proc.getSource())
    {
        juce::String t = keyText() + dot() + juce::String (s->seconds(), 1) + " S";
        if (proc.isTrimmed()) t << dot() << "TRIMMED";
        if (proc.takes().empty()) t << dot() << "PRESS GENERATE";
        return t;
    }
    return "NO SOUND LOADED";
}

void SliceEditor::refresh()
{
    refreshing = true;
    const bool hasSrc = proc.getSource() != nullptr, rec = proc.recording();
    typeTiles.set ((int) proc.type); charTiles.set ((int) proc.character);
    musicalSl.setValue (proc.musical, juce::dontSendNotification);
    toneKnob.setValue (proc.tone(), juce::dontSendNotification); spaceKnob.setValue (proc.space(), juce::dontSendNotification);
    const auto kt = proc.knobType();
    typeKnob.setValue (proc.knobValue(), juce::dontSendNotification);
    typeKnob.setDoubleClickReturnValue (true, gen::Recipe::knobDefault (kt));
    typeKnob.setTooltip (juce::String (gen::Recipe::knobTip (kt)) + " Double-click: the default.");
    {   // STYLE: the melody style (TEXTURE has no melody: greyed out); LENGTH
        auto t = proc.currentTake();
        juce::String sv = gen::styleName (proc.style);
        if (proc.style == gen::Style::Auto && t != nullptr && t->style != gen::Style::Auto && (proc.type == gen::Type::Auto || t->type == proc.type)) sv = gen::styleName (t->style);
        styleBtn.setButtonText (sv.toUpperCase());
        styleBtn.setEnabled (kt != gen::Type::Texture);
        styleBtn.setTooltip (kt == gen::Type::Texture ? juce::String ("STYLE: TEXTURE has no melody, so it has no style.")
                                                      : "STYLE: the melody, chord and arp style. AUTO picks one that suits the take; VARY keeps it. Now: " + juce::String (gen::styleName (proc.style)) + ". " + gen::styleTip (proc.style));
        lengthBtn.setButtonText (juce::String (proc.lengthBars) + (proc.lengthBars == 1 ? " BAR" : " BAR"));
    }
    const bool hasTake = proc.currentTake() != nullptr, anyTakes = ! proc.takes().empty();
    generateBtn.setEnabled (hasSrc && ! rec);
    varyBtn.setEnabled (hasSrc && ! rec && anyTakes);
    for (juce::Component* c : { (juce::Component*) &playBtn, (juce::Component*) &dragWav, (juce::Component*) &dragMidi, (juce::Component*) &saveBtn }) c->setEnabled (hasTake);
    { auto tk = proc.currentTake(); dragChords.setEnabled (tk != nullptr && proc.chordsMidiOf (*tk).getNumTracks() > 0); }
    prevBtn.setEnabled (anyTakes && proc.currentIndex() > 0); nextBtn.setEnabled (anyTakes && proc.currentIndex() < (int) proc.takes().size() - 1);
    recordBtn.setButtonText (rec ? "STOP" : "RECORD"); recordBtn.icon = rec ? Icon::Stop : Icon::Record; recordBtn.lit = rec; recordBtn.red = rec;
    loadBtn.setEnabled (! rec);
    playSrcBtn.setEnabled (hasSrc && ! rec); trimBtn.setEnabled (hasSrc && ! rec);
    if (! hasSrc && stage.trimMode) { stage.trimMode = false; trimBtn.lit = false; }
    if (! stage.trimMode) { stage.dragT0 = proc.trimStart(); stage.dragT1 = proc.trimEnd(); }
    keyBtn.setButtonText (proc.keyTonic < 0 && proc.keyKnown() ? "AUTO " + keyText() : keyText());
    keyBtn.cap = keyBtn.getButtonText().length() > 7 ? 13.0f : 16.0f;
    tempoBtn.setButtonText (juce::String (juce::roundToInt (proc.tempo())));
    tempoBtn.setEnabled (! proc.tempoFromDaw());
    tempoBtn.setTooltip (proc.tempoFromDaw() ? juce::String ("BPM: follows FL Studio's tempo.") : juce::String ("BPM: the tempo of the takes (in the app). Click to set it."));
    if (tempoSw.sel != proc.tempoMul) { tempoSw.sel = proc.tempoMul; tempoSw.repaint(); }
    unlock.refresh(); unlock.setBounds (0, 0, 0, 0);
    lookBtn.icon = Look::dark ? Icon::Sun : Icon::Moon;
    lookBtn.setTooltip (Look::dark ? "LIGHT: back to the paper-white look." : "DARK: the same Ghost Slice at night (black, bone-white lines, the gem glowing).");
    stage.headline = headline(); stage.status = statusText();
    updateKeysLit();
    strip.showCurrent();
    refreshing = false;
    canvas.repaint();
    for (juce::Component* c : { (juce::Component*) &keyBtn, (juce::Component*) &tempoBtn, (juce::Component*) &stage, (juce::Component*) &strip, (juce::Component*) &generateBtn,
                                (juce::Component*) &styleBtn, (juce::Component*) &lengthBtn, (juce::Component*) &recordBtn })
        c->repaint();
}

void SliceEditor::updateKeysLit()
{
    std::bitset<128> lit;
    if (auto t = proc.currentTake())
    {
        for (int tr = 0; tr < t->midi.getNumTracks(); ++tr)
            if (auto* seq = t->midi.getTrack (tr))
                for (auto* ev : *seq) if (ev->message.isNoteOn()) { const int n = ev->message.getNoteNumber(); if (n >= 0 && n < 128) lit[(size_t) n] = true; }
    }
    else if (proc.getSource() != nullptr && proc.keyKnown())
    {   // no take yet: the key's tonic in every octave
        const int tonic = proc.keyTonic >= 0 ? proc.keyTonic : proc.detectedKey().tonic;
        for (int n = kLowKey; n <= kHighKey; ++n) if (((n - tonic) % 12 + 12) % 12 == 0) lit[(size_t) n] = true;
    }
    if (lit != keys.lit) { keys.lit = lit; keys.repaint(); }
}

void SliceEditor::resized()
{
    // Brandon 2026-11-13: the screen always fills the whole window, any shape (no empty space at the sides). One uniform scale (the
    // letters and the picture never stretch); the extra width / height goes to the picture, the TAKES box and the keys, and the
    // controls on the right move with the edge.
    const float w = (float) getWidth(), h = (float) getHeight();
    scale = juce::jmin (w / (float) kDesignW, h / (float) kDesignH);
    const float Wd = w / scale, Hd = h / scale;
    dx = juce::jmax (0.0f, Wd - (float) kDesignW); dy = juce::jmax (0.0f, Hd - (float) kDesignH);
    canvas.setBounds (0, 0, (int) std::floor (Wd + 0.01f), (int) std::floor (Hd + 0.01f));
    canvas.setTransform (juce::AffineTransform::scale (scale));
    const int ix = juce::roundToInt (dx), iy = juce::roundToInt (dy), mx = juce::roundToInt (dx * 0.5f);
    auto at = [] (RI r, int ox, int oy, int ow = 0, int oh = 0) { return RI (r.getX() + ox, r.getY() + oy, r.getWidth() + ow, r.getHeight() + oh); };
    lookBtn.setBounds (497, 19, 40, 40);
    keyBtn.setBounds (at (kKeyBtn, ix, 0)); tempoBtn.setBounds (at (kTempoBtn, ix, 0)); tempoSw.setBounds (at (kTempoSw, ix, 0)); gearBtn.setBounds (at (kGear, ix, 0));
    stage.setBounds (at (kStage, 0, 0, ix, iy));
    loadBtn.setBounds (at (kLoad, mx, 0)); recordBtn.setBounds (at (kRecord, mx, 0)); playSrcBtn.setBounds (at (kPlaySrc, ix, iy)); trimBtn.setBounds (at (kTrim, ix, iy));
    typeTiles.setBounds (kTypes); charTiles.setBounds (kChars); styleBtn.setBounds (kStyle); lengthBtn.setBounds (kLength);
    musicalSl.setBounds (at (kMusical, mx, iy)); toneKnob.setBounds (at (kTone, mx, iy)); typeKnob.setBounds (at (kTypeKnob, mx, iy)); spaceKnob.setBounds (at (kSpace, mx, iy));
    generateBtn.setBounds (at (kGenerate, mx, iy)); varyBtn.setBounds (at (kVary, mx, iy));
    prevBtn.setBounds (at (kPrev, 0, iy)); strip.setBounds (at (kStrip, 0, iy, ix)); nextBtn.setBounds (at (kNext, ix, iy));
    playBtn.setBounds (at (kPlay, ix, iy)); dragWav.setBounds (at (kDragWav, ix, iy)); dragMidi.setBounds (at (kDragMidi, ix, iy)); dragChords.setBounds (at (kDragChords, ix, iy)); saveBtn.setBounds (at (kSave, ix, iy));
    const auto kb = at (kKeys, 0, iy, ix);
    keys.setBounds (kb);
    { int whites = 0; for (int n = kLowKey; n <= kHighKey; ++n) if (! juce::MidiMessage::isMidiNoteBlack (n)) ++whites;
      keys.setKeyWidth ((float) kb.getWidth() / (float) whites); keys.setBlackNoteWidthProportion (0.6f); keys.setBlackNoteLengthProportion (0.6f); }
    toastView.setBounds (at (kToast, mx, 0));
}

void SliceEditor::paint (juce::Graphics& g) { g.fillAll (gslice::ui::Look::bg()); }

void SliceEditor::paintCanvas (juce::Graphics& g)
{
    const float W = (float) canvas.getWidth(), H = (float) canvas.getHeight();
    const float mx = dx * 0.5f;   // the middle row stays centred under the picture
    const auto ink = Look::ink();
    g.fillAll (Look::paper());
    g.setTiledImageFill (grain(), 0, 0, 0.2f); g.fillRect (0.0f, 0.0f, W, H);
    // the header: one black band, the words in paper
    g.setColour (Look::band()); g.fillRect (0.0f, 0.0f, W, 79.0f);
    g.setGradientFill (Look::luster (R (0.0f, 76.0f, W, 3.0f), 0.9f)); g.fillRect (0.0f, 77.0f, W, 2.0f);
    drawIcon (g, Icon::Wave, R (42.0f, 12.0f, 70.0f, 60.0f), Look::bandFg());
    Look::sparkle (g, { 112.0f, 18.0f }, 7.0f, Look::bandFg());
    g.setColour (Look::bandFg()); Look::text (g, "GHOST SLICE", Look::caps (35.0f, false, 0.25f), R (131.0f, 10.0f, 420.0f, 42.0f), juce::Justification::centredLeft);
    g.setColour (Look::bandFg().withAlpha (0.75f));
    Look::text (g, "VOCALS" + dot() + "SOUNDS" + dot() + "INSPIRATION", Look::caps (12.0f, false, 0.3f), R (131.0f, 48.0f, 420.0f, 20.0f), juce::Justification::centredLeft);
    g.setColour (Look::bandFg().withAlpha (0.3f));
    g.fillRect (547.0f, 16.0f, 1.0f, 44.0f);
    for (float x : { 860.0f, 1112.0f, 1445.0f }) g.fillRect (x + dx, 16.0f, 1.0f, 44.0f);
    g.setColour (Look::bandFg());
    Look::text (g, "KEY", Look::caps (15.0f, false, 0.2f), R (590.0f + dx, 18.0f, 70.0f, 35.0f), juce::Justification::centredRight);
    Look::text (g, "BPM", Look::caps (15.0f, false, 0.2f), R (870.0f + dx, 18.0f, 70.0f, 35.0f), juce::Justification::centredRight);
    // the frame's hairlines
    g.setColour (ink.withAlpha (0.85f));
    g.fillRect (349.0f, 79.0f, 1.0f, 711.0f + dy); g.fillRect (0.0f, 790.0f + dy, W, 1.0f);
    // the left column
    const auto title = Look::caps (18.0f, false, 0.26f);
    g.setColour (ink);
    Look::text (g, "TAKE TYPE", title, R (32.0f, 98.0f, 300.0f, 28.0f), juce::Justification::centredLeft);
    Look::text (g, "CHARACTER", title, R (32.0f, 436.0f, 300.0f, 28.0f), juce::Justification::centredLeft);
    g.fillRect (30.0f, 128.0f, 300.0f, 1.0f); g.fillRect (30.0f, 467.0f, 300.0f, 1.0f);
    g.fillRect (30.0f, 131.0f, 60.0f, 2.0f); g.fillRect (30.0f, 470.0f, 60.0f, 2.0f);
    Look::sparkle (g, { 322.0f, 112.0f }, 6.0f, ink); Look::sparkle (g, { 322.0f, 450.0f }, 6.0f, ink);
    Look::text (g, "STYLE", Look::caps (14.0f, false, 0.14f), R (60.0f, 627.0f, 90.0f, 33.0f), juce::Justification::centredRight);
    Look::text (g, "LENGTH", Look::caps (14.0f, false, 0.14f), R (60.0f, 678.0f, 90.0f, 34.0f), juce::Justification::centredRight);
    {   // a cracked line in the white under the column (the shatterable world)
        juce::Path cr; cr.startNewSubPath (30.0f, 750.0f + dy); cr.lineTo (120.0f, 750.0f + dy); cr.lineTo (132.0f, 744.0f + dy); cr.lineTo (146.0f, 755.0f + dy); cr.lineTo (158.0f, 749.0f + dy); cr.lineTo (330.0f, 749.0f + dy);
        cr.startNewSubPath (146.0f, 755.0f + dy); cr.lineTo (150.0f, 764.0f + dy);
        g.setColour (ink.withAlpha (0.55f)); g.strokePath (cr, juce::PathStrokeType (0.8f));
        Look::sparkle (g, { 172.0f, 738.0f + dy }, 4.0f, ink);
    }
    // NATURAL <-> MUSICAL, the knobs' names
    const auto lab = Look::caps (15.0f, false, 0.16f);
    Look::text (g, "NATURAL", lab, R (380.0f + mx, 621.0f + dy, 100.0f, 30.0f), juce::Justification::centredLeft);
    Look::text (g, "MUSICAL", lab, R (850.0f + mx, 621.0f + dy, 100.0f, 30.0f), juce::Justification::centredLeft);
    g.setColour (ink.withAlpha (0.4f)); g.fillRect (970.0f + mx, 603.0f + dy, 1.0f, 68.0f);
    g.setColour (ink);
    Look::text (g, "TONE", lab, R ((float) kTone.getCentreX() - 70.0f + mx, 675.0f + dy, 140.0f, 26.0f));
    Look::text (g, gen::Recipe::knobName (proc.knobType()), lab, R ((float) kTypeKnob.getCentreX() - 70.0f + mx, 675.0f + dy, 140.0f, 26.0f));
    Look::text (g, "SPACE", lab, R ((float) kSpace.getCentreX() - 70.0f + mx, 675.0f + dy, 140.0f, 26.0f));
    // TAKES n, its box, the divider before the bottom row
    Look::text (g, "TAKES", Look::caps (15.0f, false, 0.28f), R (30.0f, 795.0f + dy, 120.0f, 24.0f), juce::Justification::centredLeft);
    Look::text (g, juce::String ((int) proc.takes().size()), Look::caps (15.0f, true, 0.1f), R (118.0f, 795.0f + dy, 60.0f, 24.0f), juce::Justification::centredLeft);
    g.setColour (ink.withAlpha (0.7f)); g.drawRect (kTakesBox.translated (0.0f, dy).withWidth (kTakesBox.getWidth() + dx), 1.0f);
    g.setColour (ink.withAlpha (0.4f)); g.fillRect (800.0f + dx, 812.0f + dy, 1.0f, 78.0f);
    // the keyboard's black bed
    g.setColour (Look::band()); g.fillRect (keys.getBounds().toFloat().expanded (4.0f, 4.0f));
}

void SliceEditor::timerCallback()
{
    generateBtn.phase += 1.0f / 30.0f;
    const bool busy = proc.busy(), rec = proc.recording(), sp = proc.sourcePlaying(), pl = proc.previewPlaying();
    if (busy) generateBtn.repaint();
    if (rec || sp || sp != wasSrcPlaying) { stage.status = statusText(); stage.headline = headline(); stage.repaint(); }
    if (sp != wasSrcPlaying) { wasSrcPlaying = sp; playSrcBtn.icon = sp ? Icon::Stop : Icon::Play; playSrcBtn.setButtonText (sp ? "STOP" : "PLAY"); playSrcBtn.lit = sp; playSrcBtn.repaint(); }
    if (busy || pl) strip.repaint();
    if (pl != wasPreview) { wasPreview = pl; playBtn.icon = pl ? Icon::Stop : Icon::Play; playBtn.setButtonText (pl ? "STOP" : "PLAY"); playBtn.lit = pl; playBtn.repaint(); }
    if (wasBusy != busy || wasRec != rec) { wasBusy = busy; wasRec = rec; refresh(); }
    else if (busy) { const auto st = statusText(); if (st != stage.status) { stage.status = st; stage.repaint(); } }
    if (toastUntil > 0 && juce::Time::getMillisecondCounterHiRes() > toastUntil) { toastUntil = 0; toastView.setVisible (false); }
    const auto tv = juce::String (juce::roundToInt (proc.tempo()));
    if (tempoBtn.getButtonText() != tv) { tempoBtn.setButtonText (tv); tempoBtn.repaint(); }
    if (tempoSw.sel != proc.tempoMul) { tempoSw.sel = proc.tempoMul; tempoSw.repaint(); }
    if (proc.isLoading() != (stage.headline == "LISTENING...")) { stage.headline = headline(); stage.status = statusText(); stage.repaint(); }
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
    m.addSeparator();
    m.addItem (2, "Keep this key when a new sound loads", true, proc.keyLock);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&keyBtn), [sp = juce::Component::SafePointer<SliceEditor> (this)] (int r)
    {
        if (sp == nullptr || r <= 0) return;
        if (r == 2) { sp->proc.keyLock = ! sp->proc.keyLock; sp->refresh(); return; }
        if (r == 1) sp->proc.keyTonic = -1;
        else { sp->proc.keyTonic = r % 100; sp->proc.keyMinor = r >= 200; }
        sp->toast ("The next takes are in " + (sp->proc.keyTonic < 0 ? juce::String ("the sound's own key") : keyName (sp->proc.keyTonic, sp->proc.keyMinor)));
        sp->refresh();
    });
}

void SliceEditor::tempoMenu()
{
    if (proc.tempoFromDaw()) { toast ("The tempo follows your DAW"); return; }
    juce::PopupMenu m; for (int b = 60; b <= 180; b += 5) m.addItem (b, juce::String (b) + " BPM", true, std::abs (proc.ownBpm - b) < 0.5);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&tempoBtn), [sp = juce::Component::SafePointer<SliceEditor> (this)] (int r) { if (sp != nullptr && r > 0) { sp->proc.ownBpm = r; sp->refresh(); } });
}

void SliceEditor::styleMenu()
{
    juce::PopupMenu m;
    const auto order = SliceProcessor::styleOrder();
    for (auto st : order)
        m.addItem ((int) st + 1, juce::String (gen::styleName (st)) + "  -  " + gen::styleTip (st), true, proc.style == st);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&styleBtn).withMaximumNumColumns (2), [sp = juce::Component::SafePointer<SliceEditor> (this)] (int r)
    {
        if (sp == nullptr || r <= 0) return;
        sp->proc.setStyle ((gen::Style) (r - 1), true);
        sp->toast (juce::String ("STYLE: ") + gen::styleName ((gen::Style) (r - 1)) + (sp->proc.currentTake() != nullptr ? ". The take is being remade in it" : ". Press GENERATE"));
        sp->refresh();
    });
}

void SliceEditor::lengthMenu()
{
    juce::PopupMenu m;
    for (int b : { 1, 2, 4, 8, 16 }) m.addItem (b, juce::String (b) + (b == 1 ? " bar" : " bars") + (b == 4 ? "  (the default)" : b == 16 ? "  (a whole section: it develops)" : ""), true, proc.lengthBars == b);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&lengthBtn), [sp = juce::Component::SafePointer<SliceEditor> (this)] (int r)
    {
        if (sp == nullptr || r <= 0) return;
        sp->proc.setLength (r, true);
        sp->toast ("LENGTH: " + juce::String (r) + (r == 1 ? " bar" : " bars") + (sp->proc.currentTake() != nullptr ? ". The take is being remade" : ". Press GENERATE"));
        sp->refresh();
    });
}

bool SliceEditor::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)) { if (! proc.undoEdit()) toast ("Nothing to undo"); stage.repaint(); return true; }
    if (k == juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)) { if (! proc.redoEdit()) toast ("Nothing to redo"); stage.repaint(); return true; }
    return false;
}

void SliceEditor::setDark (bool d)
{
    Look::dark = d;
    proc.uiTheme.store (d ? 0 : 1);
    proc.updateHostDisplay (juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged (true));
    lnf.applyLook();
    refresh();
    repaint();
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
    m.addItem (25, "DARK look (the night version)", true, Look::dark);
    m.addItem (23, "CLEAR: keep the sound clean and true (the next takes)", true, proc.clear);
    m.addItem (20, "Keep the key when a new sound loads", true, proc.keyLock);
    m.addItem (21, "TRIM: use the whole sound again", proc.isTrimmed());
    if (ghosty::License::required()) m.addItem (24, proc.license.isUnlocked() ? "Licence..." : "Unlock (enter your licence key)...");
    m.addSeparator();
    m.addItem (30, "How Ghost Slice works...");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&gearBtn), [sp = juce::Component::SafePointer<SliceEditor> (this)] (int r)
    {
        if (sp == nullptr || r <= 0) return;
        auto& p = sp->proc;
        if (r >= 10 && r <= 12) { p.input = (SliceProcessor::Input) (r - 10); sp->toast ("RECORD records from " + p.recordSourceText()); }
        else if (r == 20) p.keyLock = ! p.keyLock;
        else if (r == 23) { sp->toggleClear(); return; }
        else if (r == 25) { sp->setDark (! Look::dark); return; }
        else if (r == 21) { p.setTrim (0.0, 1.0); sp->stage.dragT0 = 0.0; sp->stage.dragT1 = 1.0; }
        else if (r == 24) sp->unlock.open();
        else if (r == 30)
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Ghost Slice",
                "1. Drop a vocal or any sound on the picture (or LOAD... / RECORD).\n"
                "2. Pick a TAKE TYPE and a CHARACTER (or leave AUTO), set NATURAL <-> MUSICAL.\n"
                "3. Press GENERATE: every press makes a new take. VARY makes a variation of the one you are on.\n"
                "4. Play it on the keys (or your MIDI keyboard), then DRAG WAV / DRAG MIDI / DRAG CHORDS into FL Studio, or SAVE TAKE.\n\n"
                "TRIM chooses the part of your sound GENERATE uses. Hover any control to see what it does.");
        sp->refresh();
    });
}

void SliceEditor::dragWavOut()
{
    if (proc.currentTake() == nullptr) { toast ("No take yet: press GENERATE"); return; }
    if (! ghosty::exportAllowed (proc.license, &dragWav, "slice.drag.wav")) return;
    juce::String err; const auto f = proc.writeTakeWav (SliceProcessor::dragFolder(), err);
    if (f == juce::File()) { toast (err); return; }
    ghosty::exportgate::dragFiles (proc.license, { f.getFullPathName() }, &dragWav);
}

void SliceEditor::dragMidiOut()
{
    if (proc.currentTake() == nullptr) { toast ("No take yet: press GENERATE"); return; }
    if (! ghosty::exportAllowed (proc.license, &dragMidi, "slice.drag.midi")) return;
    juce::String err; const auto f = proc.writeTakeMidi (SliceProcessor::dragFolder(), err);
    if (f == juce::File()) { toast (err); return; }
    ghosty::exportgate::dragFiles (proc.license, { f.getFullPathName() }, &dragMidi);
}

void SliceEditor::dragChordsOut()
{
    if (proc.currentTake() == nullptr) { toast ("No take yet: press GENERATE"); return; }
    if (! ghosty::exportAllowed (proc.license, &dragChords, "slice.drag.chords")) return;
    juce::String err; const auto f = proc.writeTakeChordsMidi (SliceProcessor::dragFolder(), err);
    if (f == juce::File()) { toast (err); return; }
    ghosty::exportgate::dragFiles (proc.license, { f.getFullPathName() }, &dragChords);
}

void SliceEditor::saveTakeClicked()
{
    if (proc.currentTake() == nullptr) { toast ("No take yet: press GENERATE"); return; }
    if (! ghosty::exportAllowed (proc.license, &saveBtn, "slice.take.save")) return;
    juce::String err; const auto f = proc.saveTake (err);
    toast (f == juce::File() ? err : "Saved " + f.getFileName() + " (+ .mid) in Documents/Ghosty/Ghost Slice/Takes");
}
