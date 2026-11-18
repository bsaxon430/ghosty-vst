#pragma once
// The Ghost Slice mascot (owner direction 2026-10-26, docs/design/ghost-slice/README.md): the GhostBlock ghost (the shared cute
// body, Shared/GhostArt.h) with attitude - a purple dripping hem, narrowed mischievous eyes with green pupils, a mitten hand holding
// a dark cleaver that drips neon-green slime, green splats around. Pure juce::Path + gradients (no image assets), readable at
// 24-32 px (header) and detailed at 120 px (the empty screen / the ? overlay).
#include <juce_gui_basics/juce_gui_basics.h>
#include "GhostArt.h"

namespace gslice::ui
{
struct MascotColours
{
    juce::Colour body { 0xfffbfaff }, shade { 0xffd9d3ea }, outline { 0x00000000 };   // outline: drawn when not transparent (light themes)
    juce::Colour hem { 0xff8a5cff }, eye { 0xff15131c }, pupil { 0xff3dff6e }, slime { 0xff3dff6e };
    juce::Colour blade { 0xff2e2d36 }, bladeHi { 0xff5a5866 }, handle { 0xff101014 };
};

// the drawing box in design units: x 62 .. 240, y 18 .. 182 (the ghost's own body sits at x 70 .. 188 like every GhostBlock ghost)
inline void drawSliceGhost (juce::Graphics& g, const MascotColours& C, float cx, float cy, float size)
{
    const float s = size / 182.0f;
    const auto t = juce::AffineTransform::translation (-156.0f, -102.0f).scaled (s).translated (cx, cy);
    const bool detail = size >= 60.0f;
    auto P = [&] (juce::Path p) { p.applyTransform (t); return p; };
    auto pt = [&] (float x, float y) { juce::Point<float> q (x, y); return q.transformedBy (t); };
    const float stroke = juce::jmax (0.8f, 2.2f * s);

    // ---- the cleaver's geometry: the handle runs up-right from the hand, the blade hangs off its spine (edge down-right)
    const juce::Point<float> d (0.643f, -0.766f), n (0.766f, 0.643f);
    const juce::Point<float> H0 (172, 134), H1 = H0 + d * 22.0f, B2 = H1 + d * 58.0f, B3 = B2 + n * 31.0f, B4 = H1 + n * 31.0f;

    // splats behind
    {
        juce::Path sp;
        const float spl[][3] { { 238, 44, 3.4f }, { 256, 118, 2.6f }, { 66, 154, 2.8f }, { 222, 182, 3.0f }, { 120, 26, 2.2f }, { 262, 70, 1.8f } };
        for (int i = 0; i < (detail ? 6 : 3); ++i) sp.addEllipse (spl[i][0] - spl[i][2], spl[i][1] - spl[i][2], spl[i][2] * 2, spl[i][2] * 2);
        g.setColour (C.slime.withAlpha (0.9f)); g.fillPath (P (sp));
    }
    // ---- body: hem drips (purple + two green), the body with a soft shade, the purple hem
    {
        g.setColour (C.hem); g.fillPath (P (ghosty::cuteDrips()));
        juce::Path gd; gd.addRoundedRectangle (114, 146, 7, 30, 3.5f); gd.addEllipse (112.5f, 170, 10, 11);
        gd.addRoundedRectangle (160, 144, 6, 20, 3.0f); gd.addEllipse (158.5f, 159, 9, 10);
        g.setColour (C.slime); g.fillPath (P (gd));
        const auto body = P (ghosty::cuteBody());
        juce::ColourGradient grad (C.body, pt (150, 50), C.shade, pt (84, 146), false);
        g.setGradientFill (grad); g.fillPath (body);
        if (! C.outline.isTransparent()) { g.setColour (C.outline); g.strokePath (body, juce::PathStrokeType (stroke)); }
        g.setColour (C.hem); g.fillPath (P (ghosty::cuteSlime()));
        if (detail)
        {   // purple speckles + a green splash on the body, a highlight on the head
            juce::Path sk; sk.addEllipse (96, 112, 4, 6); sk.addEllipse (104, 124, 3, 3); sk.addEllipse (90, 128, 3, 4);
            g.setColour (C.hem.withAlpha (0.75f)); g.fillPath (P (sk));
            juce::Path gs; gs.addEllipse (113, 98, 12, 7); gs.addRoundedRectangle (117, 101, 3.2f, 15, 1.6f); gs.addEllipse (116, 113, 5.2f, 6);
            g.setColour (C.slime); g.fillPath (P (gs));
            juce::Path hl; hl.addEllipse (112, 46, 26, 14); hl.applyTransform (juce::AffineTransform::rotation (-0.5f, 125, 53));
            g.setColour (juce::Colours::white.withAlpha (0.55f)); g.fillPath (P (hl));
        }
    }
    // ---- eyes: dark ovals, lids cut at a slant toward the middle (mischief), green pupils low and inward
    {
        juce::Path eyes; eyes.addEllipse (142, 68, 17, 23); eyes.addEllipse (166, 69, 15, 21);
        g.setColour (C.eye); g.fillPath (P (eyes));
        juce::Path lids;
        lids.startNewSubPath (138, 60); lids.lineTo (162, 60); lids.lineTo (162, 79); lids.lineTo (138, 70); lids.closeSubPath();
        lids.startNewSubPath (163, 60); lids.lineTo (186, 60); lids.lineTo (186, 69); lids.lineTo (163, 78); lids.closeSubPath();
        juce::ColourGradient lg (C.body, pt (150, 56), C.body.interpolatedWith (C.shade, 0.25f), pt (150, 80), false);
        g.setGradientFill (lg); g.fillPath (P (lids));
        if (! C.outline.isTransparent() || detail)
        {   // the lid line reads as a frown
            juce::Path ll; ll.startNewSubPath (140, 70.5f); ll.lineTo (161, 78.5f); ll.startNewSubPath (164, 77.5f); ll.lineTo (183, 70);
            g.setColour (C.eye.withAlpha (0.85f)); g.strokePath (P (ll), juce::PathStrokeType (juce::jmax (0.7f, 1.6f * s), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        juce::Path pu; pu.addEllipse (152, 80, 6, 7); pu.addEllipse (166.5f, 80, 5.4f, 6.4f);
        g.setColour (C.pupil); g.fillPath (P (pu));
        if (detail) { juce::Path gl; gl.addEllipse (154.5f, 81, 1.8f, 1.8f); gl.addEllipse (168.5f, 81, 1.6f, 1.6f); g.setColour (juce::Colours::white.withAlpha (0.8f)); g.fillPath (P (gl)); }
    }
    // ---- the cleaver
    {
        juce::Path blade; blade.startNewSubPath (H1); blade.lineTo (B2); blade.lineTo (B3); blade.lineTo (B4); blade.closeSubPath();
        blade = blade.createPathWithRoundedCorners (3.0f);
        juce::Path hole; hole.addEllipse (B2.x + n.x * 6.5f - d.x * 7.0f - 3.2f, B2.y + n.y * 6.5f - d.y * 7.0f - 3.2f, 6.4f, 6.4f);
        juce::Path bp = blade; bp.setUsingNonZeroWinding (false); bp.addPath (hole);
        juce::ColourGradient bg (C.bladeHi, pt (H1.x, H1.y), C.blade, pt (B3.x, B3.y), false);
        g.setGradientFill (bg); g.fillPath (P (bp));
        if (! C.outline.isTransparent()) { g.setColour (C.outline.withAlpha (0.6f)); g.strokePath (P (blade), juce::PathStrokeType (stroke * 0.7f)); }
        // neon slime along the edge (a wavy coat) + drips falling straight down
        juce::Path coat; coat.startNewSubPath (B4); coat.lineTo (B3);
        const int waves = 6;
        for (int k = waves; k >= 0; --k)
        {
            const float u = (float) k / waves;
            const auto e = B4 + (B3 - B4) * u;
            const float depth = (k % 2 == 0 ? 9.0f : 5.0f) + (detail ? 2.0f * std::sin ((float) k * 1.7f) : 0.0f);
            coat.lineTo (e - n * depth);
        }
        coat.closeSubPath();
        g.setColour (C.slime); g.fillPath (P (coat.createPathWithRoundedCorners (2.0f)));
        juce::Path drips;
        const float du[] { 0.12f, 0.38f, 0.62f, 0.86f }, dl[] { 22, 13, 28, 16 };
        for (int k = 0; k < 4; ++k)
        {
            const auto e = B4 + (B3 - B4) * du[k];
            const float w = k == 2 ? 4.2f : 3.4f;
            drips.addRoundedRectangle (e.x - w * 0.5f, e.y - 3, w, dl[k], w * 0.5f);
            drips.addEllipse (e.x - w * 0.75f, e.y + dl[k] - 4, w * 1.5f, w * 1.6f);
        }
        if (detail) { drips.addEllipse (B3.x + 2, B3.y + 38, 4, 5); drips.addEllipse (B4.x + 9, B4.y + 36, 3.4f, 4); }
        g.fillPath (P (drips));
        // handle (with a rivet) and the mitten hand around it
        juce::Path handle; handle.startNewSubPath (H0 - d * 4.0f); handle.lineTo (H1 + d * 2.0f);
        g.setColour (C.handle); g.strokePath (P (handle), juce::PathStrokeType (7.5f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (detail) { juce::Path rv; const auto r0 = H0 + d * 15.0f; rv.addEllipse (r0.x - 1.3f, r0.y - 1.3f, 2.6f, 2.6f); g.setColour (C.bladeHi); g.fillPath (P (rv)); }
        juce::Path hand; hand.addEllipse (164, 120, 22, 19);
        juce::ColourGradient hg (C.body, pt (170, 120), C.shade, pt (180, 140), false);
        g.setGradientFill (hg); g.fillPath (P (hand));
        g.setColour (C.outline.isTransparent() ? C.shade.darker (0.25f) : C.outline);
        juce::Path fingers; fingers.startNewSubPath (171, 124); fingers.quadraticTo (169, 130, 172, 136); fingers.startNewSubPath (177, 123); fingers.quadraticTo (175, 129, 178, 135);
        g.strokePath (P (fingers), juce::PathStrokeType (juce::jmax (0.6f, 1.3f * s), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (! C.outline.isTransparent()) g.strokePath (P (hand), juce::PathStrokeType (stroke * 0.8f));
    }
}
// the mascot in a Ghost Slice look (the hem = the waveform purple, the slime = the green accent; light looks get an outline)
template <class Pal> inline MascotColours mascotColours (const Pal& P)
{
    MascotColours c;
    c.hem = P.light ? P.wave : P.theme == ghosty::themes::Theme::Dark ? juce::Colour (0xff8f5cff) : P.wave;
    c.slime = P.slime; c.pupil = P.light ? P.slime.darker (0.1f) : P.slime;
    if (P.light) { c.outline = juce::Colour (0xff2a2438); c.shade = juce::Colour (0xffe4def0); }
    return c;
}
} // namespace gslice::ui
