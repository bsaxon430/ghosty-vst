// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX unit visuals (0.1.11): a small living picture on a unit's page that shows what its knobs and its sound are doing.
//
//   A UnitVisual is one unit's picture. It names the controls it reads (mapped()), and paints itself from a Feed: those
//   controls' values (0..1, read on the UI thread from the parameter tree, so a knob changes the picture with no sound playing),
//   the unit's level and a decaying "hit" (from one atomic the audio thread writes per block, only while a visual shows), and
//   the time. Nothing here runs on the audio thread and nothing here can change the sound.
//   A Style is the look's hand: its colours, whether motion is held on twos, how rough a line is. The drawn looks (BLACK,
//   PURPLE, LIGHT, GHOST) are clean vector styles; a painted look gets its own style and, when its art lands, its own sprites.
//   visual::make (unit) returns the unit's visual, or nullptr while a unit has none yet.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "GhostArt.h"
#include "FxTheme.h"
#include "../effects/Catalog.h"

namespace phantom::visual
{
using RF = juce::Rectangle<float>;
constexpr int kMaxMapped = 24;

struct Feed
{
    std::array<float, kMaxMapped> v {};   // the mapped controls, each 0..1 in the order of mapped()
    float level = 0.0f;                   // the unit's output level, 0..1 (fast up, slow down)
    float hit = 0.0f;                     // 1 at a transient, falling to 0 in about a quarter second
    double t = 0.0;                       // seconds since the page opened
    bool on = true;                       // the unit is switched on
    // time held on twos at 12 pictures a second (character motion never slides)
    double held() const noexcept { return std::floor (t * 12.0) / 12.0; }
    int tick() const noexcept { return (int) std::floor (t * 12.0); }
    // a menu's position (0 .. n - 1) and a switch, from their 0..1 value (a menu is index / (n - 1), a switch 0 or 1)
    int choice (int i, int n) const noexcept { return juce::jlimit (0, n - 1, juce::roundToInt (v[(size_t) i] * (float) (n - 1))); }
    bool sw (int i) const noexcept { return v[(size_t) i] > 0.5f; }
};
struct Style
{
    juce::Colour back, ink, dim, accent, glow; bool light = false, glass = false; float line = 1.6f; int look = 0;
};
// a repeatable "random" number for element i (the same every frame: nothing flickers unless a visual asks for it)
inline float rnd (int i, int salt = 0) noexcept { juce::uint32 x = (juce::uint32) i * 374761393u + (juce::uint32) salt * 668265263u; x = (x ^ (x >> 13)) * 1274126177u; return (float) ((x ^ (x >> 16)) & 0xffffff) / (float) 0x1000000; }

class UnitVisual
{
public:
    virtual ~UnitVisual() = default;
    virtual std::vector<const char*> mapped() const = 0;   // the unit's control ids this picture reads, at most kMaxMapped
    virtual void paint (juce::Graphics&, RF area, const Feed&, const Style&) = 0;
    int unit = 0;
protected:
    // our ghost, on model, centred, kx / ky = how wide / tall it is drawn
    static void ghost (juce::Graphics& g, int look, float cx, float cy, float size, float kx = 1.0f, float ky = 1.0f, float alpha = 1.0f, juce::Colour tint = {}, float tintAmount = 0.0f)
    {
        auto L = ghosty::lookFor (look); if (tintAmount > 0.0f) { L.body = L.body.interpolatedWith (tint, tintAmount); L.slime = L.slime.interpolatedWith (tint, tintAmount * 0.6f); }
        juce::Graphics::ScopedSaveState ss (g); g.addTransform (juce::AffineTransform::scale (kx, ky, cx, cy));
        ghosty::drawGhost (g, L, cx, cy, size, 0.0f, false, alpha);
    }
};

// ---------------------------------------------------------------- FATTEN: the ghost gets fat
// FAT = how wide and heavy it is; COLOR = the tint of its glow (cool to warm); CRUNCH = a rough, spiky edge; DRIVE = the heat
// around it; MODE = what it stands on (eight floors); PUNCH = impact lines on a hit; LOW CLEAN = a clean ring under it; OUTPUT =
// how bright the whole picture is. The sound makes it jiggle: louder = a heavier wobble, a hit = one squash.
class FattenVisual final : public UnitVisual
{
public:
    std::vector<const char*> mapped() const override { return { "fat", "color", "crunch", "drive", "mode", "punch", "lowclean", "out" }; }
    void paint (juce::Graphics& g, RF a, const Feed& f, const Style& s) override
    {
        const float fat = f.v[0], colour = f.v[1] * 2.0f - 1.0f, crunch = f.v[2], drive = f.v[3], punch = f.v[5], clean = f.v[6], out = 0.55f + 0.45f * f.v[7]; const int mode = juce::roundToInt (f.v[4] * 7.0f);
        const float cx = a.getCentreX(), floorY = a.getBottom() - a.getHeight() * 0.16f, base = juce::jmin (a.getHeight() * 0.62f, a.getWidth() * 0.34f);
        const auto warm = juce::Colour (0xffff8a3c), cool = juce::Colour (0xff45c8ff), tint = colour >= 0.0f ? warm : cool;
        // the weight wobbles on twos: more fat and more level = a slower, bigger jiggle; a hit squashes it once
        const float wob = std::sin ((float) f.held() * (9.0f - 4.0f * fat)) * (0.012f + 0.05f * fat) * (0.25f + f.level), squash = f.hit * (0.05f + 0.10f * fat);
        const float kx = (1.0f + 0.95f * fat) * (1.0f + wob + squash), ky = (1.0f + 0.16f * fat) * (1.0f - wob * 0.7f - squash * 0.8f);
        const float size = base * (0.78f + 0.10f * fat), cy = floorY - size * 0.50f * ky;
        // the heat around it (DRIVE), in the COLOR's tint
        if (drive > 0.01f || std::abs (colour) > 0.02f)
        {
            const float rr = size * (0.75f + 0.55f * drive) * juce::jmax (kx, 1.0f); const auto c = tint.withAlpha (out * (0.10f + 0.35f * drive + 0.12f * std::abs (colour)));
            g.setGradientFill (juce::ColourGradient (c, cx, cy, c.withAlpha (0.0f), cx + rr, cy, true)); g.fillEllipse (cx - rr, cy - rr * 0.8f, rr * 2.0f, rr * 1.6f);
        }
        // what it stands on: the MODE's floor (0 a line ... 7 a stack of plates), bending under the weight
        const float sag = size * 0.05f * fat * (1.0f + f.level), fw = size * (0.9f + 0.9f * fat);
        g.setColour (s.dim.withAlpha (out));
        for (int k = 0; k <= mode; ++k)
        {
            juce::Path p; const float y = floorY + (float) k * 5.0f; p.startNewSubPath (cx - fw, y); p.quadraticTo (cx, y + sag * 2.0f, cx + fw, y);
            g.strokePath (p, juce::PathStrokeType (k == 0 ? s.line * 1.4f : s.line * 0.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        if (clean > 0.5f) { g.setColour (s.accent.withAlpha (0.8f * out)); g.drawEllipse (cx - fw * 0.8f, floorY - 4.0f + sag, fw * 1.6f, 10.0f, s.line); }   // LOW CLEAN: a clean ring under it
        // CRUNCH: a rough edge, spikes that grow out of the outline (redrawn on twos so they crackle)
        if (crunch > 0.01f)
        {
            juce::Path sp; const int n = 26; const float rx = size * 0.50f * kx, ry = size * 0.56f * ky;
            for (int i = 0; i < n; ++i)
            {
                const float an = juce::MathConstants<float>::twoPi * (float) i / (float) n, j = rnd (i, f.tick() / 2), len = crunch * size * (0.06f + 0.22f * j);
                const float x0 = cx + std::cos (an) * rx, y0 = cy + std::sin (an) * ry * 0.96f, x1 = cx + std::cos (an) * (rx + len), y1 = cy + std::sin (an) * (ry + len);
                sp.startNewSubPath (x0, y0); sp.lineTo (x1, y1);
            }
            g.setColour (tint.interpolatedWith (s.ink, 0.4f).withAlpha (out * (0.35f + 0.6f * crunch))); g.strokePath (sp, juce::PathStrokeType (s.line * (0.8f + crunch)));
        }
        ghost (g, unitDef (unit).look, cx, cy, size, kx, ky, f.on ? out : out * 0.45f, tint, 0.35f * std::abs (colour));
        // PUNCH: impact lines that jump out on a hit
        if (punch > 0.5f)
        {
            g.setColour (s.ink.withAlpha (out * (0.25f + 0.75f * f.hit)));
            for (int i = 0; i < 6; ++i) { const float side = i < 3 ? -1.0f : 1.0f, yy = cy + ((float) (i % 3) - 1.0f) * size * 0.28f, x0 = cx + side * size * (0.62f * kx + 0.08f), len = size * (0.10f + 0.30f * f.hit); g.drawLine (x0, yy, x0 + side * len, yy + ((float) (i % 3) - 1.0f) * len * 0.3f, s.line * 1.3f); }
        }
    }
};

// ---------------------------------------------------------------- CRYSTAL: shards grow, split and refract
// PITCH = which way and how far the shards lean and reach (up for up, down for down); SPLICE = how long each shard is; DELAY =
// how far apart the generations stand; FEEDBACK = how many generations grow out of each other; REVERSE = how many shards hang
// from above instead; SPREAD = how wide the cluster fans; POSITION = where along the shard its colour sits; DENSITY = how many
// shards; TEXTURE = facets cut into them; FREEZE = frost: everything stops and whitens. The sound makes them glint.
class CrystalVisual final : public UnitVisual
{
public:
    std::vector<const char*> mapped() const override { return { "pitch", "splice", "delay", "fb", "reverse", "spread", "position", "density", "texture", "freeze" }; }
    void paint (juce::Graphics& g, RF a, const Feed& f, const Style& s) override
    {
        const float pitch = f.v[0] * 2.0f - 1.0f, splice = f.v[1], delay = f.v[2], fb = f.v[3], reverse = f.v[4], spread = f.v[5], position = f.v[6], density = f.v[7], texture = f.v[8]; const bool frozen = f.v[9] > 0.5f;
        const int n = 3 + juce::roundToInt (density * 9.0f), gens = 1 + juce::roundToInt (fb * 3.0f);
        const float time = frozen ? 0.0f : (float) f.held(), glint = frozen ? 0.2f : f.level;
        const auto c0 = frozen ? juce::Colours::white.interpolatedWith (s.accent, 0.25f) : s.accent, c1 = s.accent.withRotatedHue (0.12f + 0.30f * position), c2 = s.accent.withRotatedHue (-0.18f);
        for (int gen = gens - 1; gen >= 0; --gen)
        {
            const float gk = std::pow (0.62f, (float) gen), off = (float) gen * (0.04f + 0.16f * delay) * a.getWidth();
            for (int i = 0; i < n; ++i)
            {
                const float u = n > 1 ? (float) i / (float) (n - 1) - 0.5f : 0.0f, r = rnd (i, gen * 7 + 1);
                const bool down = rnd (i, 91 + gen) < reverse;
                const float bx = a.getCentreX() + u * a.getWidth() * (0.25f + 0.65f * spread) + (gen & 1 ? off : -off) * (0.5f + r);
                const float by = down ? a.getY() + 2.0f : a.getBottom() - 2.0f;
                const float len = a.getHeight() * (0.22f + 0.55f * splice) * (0.55f + 0.45f * r) * gk * (0.6f + 0.4f * std::abs (pitch) + 0.15f);
                const float lean = pitch * 0.9f + u * (0.5f + spread) + std::sin (time * 0.7f + (float) i) * 0.03f, w = len * (0.10f + 0.07f * r);
                const float dir = down ? 1.0f : -1.0f, tx = bx + std::sin (lean) * len, ty = by + dir * std::cos (lean) * len;
                const float nx = std::cos (lean), ny = -dir * std::sin (lean) * -1.0f;
                juce::Path p; p.startNewSubPath (bx - nx * w, by + ny * w * 0.2f); p.lineTo (bx + (tx - bx) * 0.72f - nx * w * 0.9f, by + (ty - by) * 0.72f); p.lineTo (tx, ty); p.lineTo (bx + (tx - bx) * 0.72f + nx * w * 0.9f, by + (ty - by) * 0.72f); p.lineTo (bx + nx * w, by - ny * w * 0.2f); p.closeSubPath();
                const float al = (0.30f + 0.5f * gk) * (0.75f + 0.25f * glint);
                g.setGradientFill (juce::ColourGradient (c0.withAlpha (al), bx, by, (i & 1 ? c1 : c2).withAlpha (al * 0.55f), tx, ty, false)); g.fillPath (p);
                g.setColour ((frozen ? juce::Colours::white : s.ink).withAlpha (0.35f + 0.45f * gk)); g.strokePath (p, juce::PathStrokeType (s.line * (0.5f + 0.5f * gk)));
                // TEXTURE: facets cut across the shard
                const int facets = juce::roundToInt (texture * 5.0f);
                for (int k = 1; k <= facets; ++k) { const float q = (float) k / (float) (facets + 1) * 0.72f; g.drawLine (bx + (tx - bx) * q - nx * w * 0.8f, by + (ty - by) * q, bx + (tx - bx) * (q + 0.08f) + nx * w * 0.8f, by + (ty - by) * (q + 0.08f), s.line * 0.5f); }
                // the centre ridge, and a glint that the sound lights (a different shard each held frame)
                g.setColour (juce::Colours::white.withAlpha (0.25f + 0.5f * glint)); g.drawLine (bx, by, tx, ty, s.line * 0.5f);
                if (! frozen && glint > 0.05f && ((i + gen * 3 + f.tick() / 2) % 4) == 0) { const float gs = 2.0f + 7.0f * glint * gk; g.setColour (juce::Colours::white.withAlpha (juce::jmin (1.0f, 0.3f + glint))); g.drawLine (tx - gs, ty, tx + gs, ty, 1.2f); g.drawLine (tx, ty - gs, tx, ty + gs, 1.2f); }
            }
        }
        if (frozen) { g.setColour (juce::Colours::white.withAlpha (0.5f)); for (int i = 0; i < 24; ++i) { const float x = a.getX() + rnd (i, 5) * a.getWidth(), y = a.getY() + rnd (i, 6) * a.getHeight(); g.fillEllipse (x, y, 2.0f, 2.0f); } }   // frost
    }
};

// ---------------------------------------------------------------- ECHO: ghost copies trail away
// TIME = how far apart the copies stand; FEEDBACK = how many there are and how slowly they fade; PING-PONG = they hop from
// side to side (up and down in the picture); WIDTH = how far they hop; LOW CUT = each copy smaller; HIGH CUT = each copy duller;
// CHARACTER = how much each copy wavers; DUCK = the copies shrink away while the sound is loud; NODES = extra small copies at
// each echo; SPREAD = how far those scatter; SATURATION = a hot outline; PAN = the trail tilts; MODE = the kind of trace they
// leave (tape = soft dots, analog = a line, BBD = steps, digital = ticks); SYNC = beat marks under the trail.
class EchoVisual final : public UnitVisual
{
public:
    std::vector<const char*> mapped() const override { return { "time", "fb", "ping", "width", "lowcut", "highcut", "char", "duck", "nodes", "spread", "sat", "pan", "mode", "sync" }; }
    void paint (juce::Graphics& g, RF a, const Feed& f, const Style& s) override
    {
        const float time = f.v[0], fb = f.v[1], width = f.v[3], lowcut = f.v[4], highcut = f.v[5], chr = f.v[6], duck = f.v[7], spread = f.v[9], sat = f.v[10], pan = f.v[11] * 2.0f - 1.0f;
        const bool ping = f.v[2] > 0.5f, sync = f.v[13] > 0.5f; const int nodes = 1 + juce::roundToInt (f.v[8] * 7.0f), mode = juce::roundToInt (f.v[12] * 3.0f);
        const float size = juce::jmin (a.getHeight() * 0.50f, a.getWidth() * 0.20f), x0 = a.getX() + size * 0.62f, midY = a.getCentreY();
        const float step = (a.getWidth() - size * 1.3f) * (0.10f + 0.34f * std::sqrt (time)), th = (float) f.held();
        const float ducked = 1.0f - duck * juce::jmin (1.0f, f.level * 1.5f);
        const auto hot = juce::Colour (0xffff7a3c);
        if (sync) { g.setColour (s.dim.withAlpha (0.6f)); for (float x = x0; x < a.getRight() - 4.0f; x += step * 0.5f) { const bool half = ((int) ((x - x0) / (step * 0.5f) + 0.5f) & 1) != 0; g.fillRect (x - 1.0f, a.getBottom() - (half ? 7.0f : 13.0f), 2.0f, half ? 7.0f : 13.0f); g.fillRect (x - 1.0f, a.getY(), 2.0f, half ? 4.0f : 8.0f); } }
        // DUCK: a shade the caller holds over its copies (how far it reaches = how much they are pushed down while it sounds)
        if (duck > 0.01f) { juce::Path sh; sh.addCentredArc (x0, midY, size * 0.9f + step * 2.5f * duck, size * (0.75f + 0.25f * duck), 0.0f, 0.15f, 1.45f, true); g.setColour (s.ink.withAlpha (0.25f + 0.55f * duck)); g.strokePath (sh, juce::PathStrokeType (s.line * (1.0f + 2.5f * duck), juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }
        // the trace between the copies, in the MODE's hand
        {
            g.setColour (s.accent.withAlpha (0.5f)); const float yT = midY + size * 0.62f; const float x1 = juce::jmin (a.getRight() - 4.0f, x0 + step * 8.0f);
            if (mode == 1) g.drawLine (x0, yT, x1, yT + pan * 14.0f, s.line);
            else for (float x = x0; x < x1; x += (mode == 0 ? 7.0f : mode == 2 ? 12.0f : 5.0f))
            { const float y = yT + pan * 14.0f * (x - x0) / juce::jmax (1.0f, x1 - x0); if (mode == 0) g.fillEllipse (x, y - 1.5f, 3.0f, 3.0f); else if (mode == 2) g.fillRect (x, y - ((int) (x / 12.0f) & 1 ? 4.0f : 0.0f), 12.0f, 2.0f); else g.fillRect (x, y - 4.0f, 1.2f, 8.0f); }
        }
        // the copies, far ones first
        int count = 0; { float al = 1.0f; while (count < 8 && al > 0.06f) { ++count; al *= 0.25f + 0.72f * fb; } }
        for (int k = count; k >= 1; --k)
        {
            const float al = std::pow (0.25f + 0.72f * fb, (float) k), x = x0 + step * (float) k; if (x > a.getRight() - size * 0.3f) continue;
            const float sz = size * (0.86f - 0.05f * (float) k) * (1.0f - 0.45f * lowcut * (float) k / 8.0f) * (0.45f + 0.55f * ducked);
            const float hop = ping ? ((k & 1) ? -1.0f : 1.0f) * size * (0.10f + 0.42f * width) : 0.0f, waver = std::sin (th * 3.0f + (float) k * 1.7f) * size * 0.10f * chr;
            const float y = midY + hop + waver + pan * size * 0.5f * (float) k / 8.0f; const float dull = (1.0f - highcut) * juce::jmin (1.0f, (float) k / 4.0f);
            for (int nd = nodes - 1; nd >= 1; --nd)
            {   // the extra taps: small copies scattered around this echo
                const float an = juce::MathConstants<float>::twoPi * rnd (nd, k), rr = size * (0.25f + 0.9f * spread) * (0.4f + 0.6f * rnd (nd, k + 40));
                ghost (g, unitDef (unit).look, x + std::cos (an) * rr * 0.6f, y + std::sin (an) * rr, sz * 0.34f, 1.0f, 1.0f, al * 0.55f, s.dim, 0.5f + 0.5f * dull);
            }
            if (sat > 0.02f) { g.setColour (hot.withAlpha (al * sat * 0.7f)); g.drawEllipse (x - sz * 0.42f, y - sz * 0.50f, sz * 0.84f, sz * 0.96f, s.line * (1.0f + 2.0f * sat)); }
            ghost (g, unitDef (unit).look, x, y, sz, 1.0f, 1.0f, al, s.dim, dull * 0.85f);
        }
        // the one that calls: it bounces with the sound
        const float b = f.hit * 0.10f; ghost (g, unitDef (unit).look, x0, midY - f.level * size * 0.06f, size, 1.0f + b, 1.0f - b, f.on ? 1.0f : 0.5f);
        if (f.hit > 0.05f) { g.setColour (s.accent.withAlpha (f.hit * 0.7f)); const float rr = size * (0.55f + 0.5f * (1.0f - f.hit)); g.drawEllipse (x0 - rr, midY - rr, rr * 2.0f, rr * 2.0f, s.line); }
    }
};

inline std::unique_ptr<UnitVisual> make (int unit)
{
    std::unique_ptr<UnitVisual> v;
    switch (unit)
    {
        case uFatten:  v = std::make_unique<FattenVisual>(); break;
        case uCrystal: v = std::make_unique<CrystalVisual>(); break;
        case uEcho:    v = std::make_unique<EchoVisual>(); break;
        default: break;
    }
    if (v) v->unit = unit;
    return v;
}
// the look's hand for the drawn looks (a painted look fills its own from its look.json when its art lands)
inline Style styleFor (int unit)
{
    const auto& p = fxtheme::pal(); Style s; s.light = p.isLight; s.look = fxtheme::look();
    s.back = p.cell; s.ink = p.ink; s.dim = p.dim; s.accent = fxtheme::T (juce::Colour (unitDef (unit).accent)); s.glow = s.accent; s.line = 1.6f;
    return s;
}
} // namespace phantom::visual
