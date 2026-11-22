// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
//
// Ported to JUCE from Bencho's "Particles" (lab/Particles.tsx), MIT licence (bencho.dev/licence,
// full text in NOTICE.md / Licenses). Bencho's comments are kept as written (marked "Bencho:");
// ours say what changed for Ghost Mosaic.
#include "Swarm.h"
#include "HeatWord.h"
#include "Look.h"

namespace mosaic
{
/* Bencho:
   ══ Particles ════════════════════════════════════════════
   A shape made of dots. Move over it and the dots are pushed
   away from the pointer and spring home behind it; press and a
   shockwave throws them out, and they gather again into the
   NEXT shape — circle, square, flower, round again.

   ── ONE SET OF DOTS, MANY SHAPES ────────────────────────
   The dots never change, only where home is. Every shape is
   sampled to the same number of points, and a change of shape
   pairs each dot with a new home by angle round the middle, so
   the swarm sweeps round into the new outline rather than
   crossing itself.

   ── STRETCHED BY SPEED ──────────────────────────────────
   A dot at rest is round; a moving one is drawn a little long
   along the way it is going, which is what makes a burst read
   as a splash rather than a scatter of points. */

/* Bencho (inlined from lab/spring):
   Frames, not milliseconds. `dt` is expressed in sixtieths of
   a second and the damping is RAISED to it rather than
   multiplied by it, so a dropped frame decays the same amount
   of energy as the two frames it replaced. Multiplying is the
   version that makes a spring behave differently on a busy
   page, which is the hardest kind of bug to see.

   The loop parks itself the moment the value has settled. */
// Ghost Mosaic: the editor's 60 Hz clock calls tick(); a settled swarm with nothing lit does no
// work and asks for no repaint (the "parks itself" rule). There is no reduced-motion setting
// to read inside a plug-in, so that part of the original is left out.

static constexpr float W = 480, H = 400, CX = W / 2, CY = H / 2;
// Ghost Mosaic: Bencho samples every shape to 720 dots. Here a dot is a library piece, so the
// count follows the library, up to kMaxDots; past that one dot stands for a run of neighbouring
// pieces (same sound, next to each other in time).
static constexpr int kEmptyDots = 720, kMaxDots = 2400;
// Bencho: SHAPES = ["Circle", "Square", "Flower"]. Ghost Mosaic adds a fourth, the GHOST (our own
// drawing: a dome head, two eye holes and a little "o" mouth left as gaps, a scalloped hem). It is
// the shape the swarm forms on first open, and the cycle comes back round to it.
static constexpr int kShapes = 4;
static constexpr int kGhost = 3;

static float clampf (float v, float a, float b) { return juce::jmin (b, juce::jmax (a, v)); }

static juce::Path outline (int s)
{
    juce::Path p;
    if (s == 0) p.addEllipse (CX - 150, CY - 150, 300, 300);
    else if (s == 1) p.addRoundedRectangle (CX - 140, CY - 140, 280, 280, 36);
    else
    {
        /* Bencho: five round petals round a smaller middle — circles drawn
           the same way round, so the fill is their union. Petals
           just touching their neighbours, so the notches between
           them read at the dots' own grain */
        for (int i = 0; i < 5; ++i)
        {
            const float a = -juce::MathConstants<float>::halfPi + (float) i * juce::MathConstants<float>::twoPi / 5;
            const float x = CX + std::cos (a) * 104, y = CY + 8 + std::sin (a) * 104;
            p.addEllipse (x - 60, y - 60, 120, 120);
        }
        p.addEllipse (CX - 58, CY + 8 - 58, 116, 116);
    }
    p.setUsingNonZeroWinding (true);
    return p;
}

// Ghost Mosaic: the ghost. One outline (head, sides, the wavy hem) plus the eyes and the mouth
// as separate loops inside it; filled even-odd, so those loops are holes in the dot field.
static juce::Path ghostOutline()
{
    juce::Path p;
    const float halfW = 128, headR = 128, top = CY - 170, shoulder = top + headR, hem = CY + 150;
    const float l = CX - halfW, r = CX + halfW;
    p.startNewSubPath (l, shoulder);
    // the dome: a half circle over the top, left to right
    p.addCentredArc (CX, shoulder, headR, headR, 0, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi);
    p.lineTo (r, hem);
    // the hem: four round scallops hanging down, right to left (the notches between them point up)
    constexpr int lobes = 4;
    const float lw = (r - l) / lobes, depth = 38;
    for (int i = 0; i < lobes; ++i)
    {
        const float x1 = r - lw * (float) i, mx = x1 - lw * 0.5f;
        // from the lobe's right end, down round its bottom, up to its left end (JUCE: 0 = 12 o'clock, clockwise)
        p.addCentredArc (mx, hem, lw * 0.5f, depth, 0, juce::MathConstants<float>::halfPi, juce::MathConstants<float>::pi * 1.5f);
    }
    p.closeSubPath();
    // the eyes (tall ovals) and the mouth (a small round "o")
    p.addEllipse (CX - 74, shoulder - 34, 46, 64);
    p.addEllipse (CX + 28, shoulder - 34, 46, 64);
    p.addEllipse (CX - 17, shoulder + 58, 34, 38);
    p.setUsingNonZeroWinding (false);
    return p;
}

/* Bencho: ── N points, spread evenly over the inside of a shape ──
   A hex grid spaced so the shape holds about N of them — its
   area over N, which is the spacing that fills it evenly — then
   trimmed or topped up to exactly N. It was a fixed fine grid
   thinned by a stride, and the stride beat against the grid:
   the circle came out in zigzag bands. */
static std::vector<juce::Point<float>> homes (int s, int N, float& spacing)
{
    const auto path = s == kGhost ? ghostOutline() : outline (s);
    float area = 0;
    for (int y = 1; y < (int) H; y += 2) for (int x = 1; x < (int) W; x += 2) if (path.contains ((float) x, (float) y)) area += 4;
    const float sp = std::sqrt ((area / (float) N) * (2 / std::sqrt (3.0f)));
    spacing = sp;
    const float rowH = sp * std::sqrt (3.0f) / 2;
    std::vector<juce::Point<float>> pts;
    int row = 0;
    for (float y = rowH / 2; y < H; y += rowH, ++row)
        for (float x = sp / 2 + (float) (row % 2) * (sp / 2); x < W; x += sp)
            if (path.contains (x, y)) pts.push_back ({ x, y });
    /* Bencho: within a few of N either way; drop evenly or repeat evenly */
    std::vector<juce::Point<float>> out ((size_t) N);
    for (int i = 0; i < N; ++i) out[(size_t) i] = pts[(size_t) ((juce::int64) i * (juce::int64) pts.size() / N)];
    return out;
}

static float angleOf (float x, float y) { return std::atan2 (y - CY, x - CX); }

Swarm::Swarm()
{
    setOpaque (false);
    setLibrary (nullptr);
}

void Swarm::setLibrary (std::shared_ptr<const LibraryPicture> pic)
{
    m_pic = pic;
    const int blocks = pic != nullptr ? pic->numBlocks : 0;
    const int n = blocks > 0 ? juce::jmin (blocks, kMaxDots) : kEmptyDots;
    m_blocksPerDot = blocks > 0 ? (blocks + n - 1) / n : 1;
    const int dots = blocks > 0 ? (blocks + m_blocksPerDot - 1) / m_blocksPerDot : kEmptyDots;
    const bool fresh = (int) m_dots.size() != dots;
    if (fresh) m_dots.assign ((size_t) dots, Dot { CX, CY, CX, CY, 0, 0 });
    for (int i = 0; i < dots; ++i)
    {
        auto& d = m_dots[(size_t) i];
        if (blocks > 0)
        {
            const auto& pd = pic->dots[(size_t) juce::jmin (blocks - 1, i * m_blocksPerDot)];
            d.sound = pd.sound;
            // a piece's tint is its sound's; quiet pieces are a little darker
            d.tint = look::sourceTint (pd.sound).withMultipliedBrightness (0.55f + 0.45f * pd.level);
        }
        else { d.sound = -1; d.tint = look::inkFaint; }
        d.flash = 0; d.heat = 0;
    }
    m_links.clear();
    become (m_shape, fresh);
    m_busy = true;
    repaint();
}

/* Bencho: ── give every dot a home in a shape ────────────────── */
void Swarm::become (int s, bool fresh)
{
    m_shape = s;
    float sp = 0;
    const auto next = homes (s, (int) m_dots.size(), sp);
    m_spacing = sp;
    std::vector<std::pair<float, int>> b;
    for (size_t i = 0; i < next.size(); ++i) b.push_back ({ angleOf (next[i].x, next[i].y), (int) i });
    std::sort (b.begin(), b.end());
    if (fresh)
    {
        // Ghost Mosaic: the first homes are dealt in library order round the middle, so every
        // sound is a wedge of its own colour (and keeps it: the pairing below is by angle)
        for (size_t k = 0; k < m_dots.size(); ++k)
        {
            const auto h = next[(size_t) b[k].second];
            auto& d = m_dots[k];
            d.hx = h.x; d.hy = h.y;
            d.x = CX + (h.x - CX) * 0.2f; d.y = CY + (h.y - CY) * 0.2f;   // and they bloom out from the middle
            d.vx = d.vy = 0;
        }
        return;
    }
    /* Bencho: pair by angle round the middle, so the swarm sweeps */
    std::vector<std::pair<float, int>> a;
    for (size_t i = 0; i < m_dots.size(); ++i) a.push_back ({ angleOf (m_dots[i].x, m_dots[i].y), (int) i });
    std::sort (a.begin(), a.end());
    for (size_t k = 0; k < a.size(); ++k)
    {
        const auto h = next[(size_t) b[k].second];
        m_dots[(size_t) a[k].second].hx = h.x;
        m_dots[(size_t) a[k].second].hy = h.y;
    }
}

void Swarm::burst (juce::Point<float> p, float strength)
{
    /* Bencho: the shockwave, then the next shape */
    // (force 60 of 100, as Bencho's default)
    const float S = (0.4f + 0.6f * 1.2f) * strength;
    for (auto& d : m_dots)
    {
        const float dx = d.x - p.x, dy = d.y - p.y;
        const float dd = std::hypot (dx, dy);
        if (dd > 200 || dd < 0.01f) continue;
        const float f = (1 - dd / 200) * 30 * S;
        d.vx += dx / dd * f;
        d.vy += dy / dd * f;
    }
    become ((m_shape + 1) % kShapes, false);
    m_busy = true;
}

void Swarm::shockwave()
{
    // MUTATE: from just off the middle (a burst from the exact centre has no direction for the middle dot)
    burst ({ CX + 0.5f, CY + 0.5f }, 1.0f);
}

void Swarm::played (int block, int)
{
    if (m_pic == nullptr || block < 0) return;
    const int i = block / juce::jmax (1, m_blocksPerDot);
    if (i >= (int) m_dots.size()) return;
    auto& d = m_dots[(size_t) i];
    d.flash = 1.0f;
    d.heat = juce::jmin (1.0f, d.heat + 0.3f);
    if (m_links.size() > 14) m_links.erase (m_links.begin());
    m_links.push_back ({ i, 1.0f });
    m_busy = true;
}

void Swarm::tick()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = m_lastTick > 0 ? clampf ((float) (now - m_lastTick) / 16.67f, 0, 2.5f) : 1;
    m_lastTick = now;
    if (! m_busy && ! m_hand) return;
    // Bencho's knobs at their defaults: reach 80 px, force 60
    const float R = 80, S = 0.6f * 11;
    bool busy = m_hand.has_value();
    const float damp = std::pow (0.84f, dt), flashDecay = std::pow (0.86f, dt), heatDecay = std::pow (0.985f, dt);
    for (auto& d : m_dots)
    {
        if (m_hand)
        {
            const float dx = d.x - m_hand->x, dy = d.y - m_hand->y;
            const float dd = std::hypot (dx, dy);
            if (dd < R && dd > 0.01f)
            {
                const float f = std::pow (1 - dd / R, 2.0f) * S;
                d.vx += dx / dd * f * dt;
                d.vy += dy / dd * f * dt;
            }
        }
        d.vx = (d.vx + (d.hx - d.x) * 0.05f * dt) * damp;
        d.vy = (d.vy + (d.hy - d.y) * 0.05f * dt) * damp;
        d.x += d.vx * dt;
        d.y += d.vy * dt;
        if (std::abs (d.hx - d.x) < 0.05f && std::abs (d.hy - d.y) < 0.05f && std::abs (d.vx) < 0.02f && std::abs (d.vy) < 0.02f)
        { d.x = d.hx; d.y = d.hy; d.vx = 0; d.vy = 0; }
        else busy = true;
        if (d.flash > 0.004f) { d.flash *= flashDecay; busy = true; } else d.flash = 0;
        if (d.heat > 0.004f) { d.heat *= heatDecay; busy = true; } else d.heat = 0;
    }
    for (auto& l : m_links) l.life *= std::pow (0.9f, dt);
    m_links.erase (std::remove_if (m_links.begin(), m_links.end(), [] (const Lit& l) { return l.life < 0.02f; }), m_links.end());
    m_busy = busy || ! m_links.empty();
    repaint();
}

std::vector<juce::Point<float>> Swarm::homePoints() const
{
    std::vector<juce::Point<float>> v;
    for (auto& d : m_dots) v.push_back ({ d.hx, d.hy });
    return v;
}

void Swarm::resized()
{
    const auto b = getLocalBounds().toFloat();
    m_scale = juce::jmin (b.getWidth() / W, b.getHeight() / H);
    // the cloud is stretched to its panel (at most 2.8 times wider than tall; the last 20 px are the key's)
    const float sy = juce::jmax (0.1f, (b.getHeight() - 20.0f) / 360.0f), sx = juce::jmax (sy, juce::jmin (b.getWidth() / 300.0f, sy * 2.8f));
    m_toView = juce::AffineTransform::scale (sx, sy).translated (b.getCentreX() - CX * sx, b.getCentreY() - 10.0f - CY * sy);
}

juce::Point<float> Swarm::toLocal (juce::Point<float> p) const
{
    return p.transformedBy (m_toView.inverted());
}

void Swarm::mouseMove (const juce::MouseEvent& e) { m_hand = toLocal (e.position); m_busy = true; }
void Swarm::mouseDrag (const juce::MouseEvent& e) { m_hand = toLocal (e.position); m_busy = true; }
void Swarm::mouseExit (const juce::MouseEvent&) { m_hand.reset(); m_busy = true; }
void Swarm::mouseDown (const juce::MouseEvent& e)
{
    burst (toLocal (e.position), 1.0f);
    if (onPress) onPress();
}

void Swarm::paint (juce::Graphics& g)
{
    if (m_heat && m_pic != nullptr && m_pic->numBlocks > 0) paintHeat (g);
    else paintDots (g);
    if (m_pic == nullptr || m_pic->numBlocks == 0) paintEmpty (g);
}

void Swarm::paintEmpty (juce::Graphics& g)
{
    g.setColour (look::inkDim);
    g.setFont (look::spaced (13.0f, 1, 0.14f));
    g.drawText ("DROP SOUNDS ON SOURCE SOUNDS", getLocalBounds().removeFromBottom (28), juce::Justification::centred);
}

void Swarm::paintDots (juce::Graphics& g)
{
    // Bencho: grain "Fine" 1.9, "Coarse" 2.7. Ghost Mosaic: more dots than 720 are drawn finer,
    // so a big library fills the same shape at the same grain of light
    const float r = 1.9f * clampf (std::sqrt (720.0f / (float) juce::jmax (1, (int) m_dots.size())), 0.55f, 1.25f);
    g.saveState();
    g.addTransform (m_toView);
    // network links of the pieces just played (Network search styles walk these)
    if (m_pic != nullptr && ! m_links.empty())
    {
        for (auto& l : m_links)
        {
            const auto& a = m_dots[(size_t) l.dot];
            const int blk = l.dot * m_blocksPerDot;
            if (blk >= (int) m_pic->links.size()) continue;
            for (int nb : m_pic->links[(size_t) blk])
            {
                if (nb < 0) continue;
                const int j = nb / m_blocksPerDot;
                if (j >= (int) m_dots.size()) continue;
                const auto& b = m_dots[(size_t) j];
                g.setColour (a.tint.withAlpha (0.35f * l.life));
                g.drawLine (a.x, a.y, b.x, b.y, 0.8f);
            }
        }
    }
    g.restoreState();
    // The dots are placed by the panel's stretch but drawn round, in screen pixels, big enough to show their colour.
    // one path per tint for the dots at rest or moving (the many), one by one for the lit (the few)
    const float R = juce::jlimit (1.6f, 4.5f, r * std::sqrt (std::abs (m_toView.mat00 * m_toView.mat11)) * 1.3f);
    auto at = [this] (float x, float y) { return juce::Point<float> (x, y).transformedBy (m_toView); };
    std::map<juce::uint32, juce::Path> groups;
    std::vector<const Dot*> lit;
    for (auto& d : m_dots)
    {
        if (d.flash > 0.05f) { lit.push_back (&d); continue; }
        auto& p = groups[d.tint.getARGB()];
        const auto c = at (d.x, d.y);
        const float sp = std::hypot (d.vx, d.vy);
        if (sp < 0.3f) p.addEllipse (c.x - R, c.y - R, R * 2, R * 2);
        else
        {
            /* Bencho: long along its path, a little thin across it */
            const float s = juce::jmin (2.6f, 1 + sp * 0.12f);
            const float rot = std::atan2 (d.vy * m_toView.mat11, d.vx * m_toView.mat00);
            juce::Path e; e.addEllipse (-R * s, -R / std::sqrt (s), R * s * 2, R / std::sqrt (s) * 2);
            p.addPath (e, juce::AffineTransform::rotation (rot).translated (c.x, c.y));
        }
    }
    for (auto& [argb, p] : groups)
    {
        g.setColour (juce::Colour (argb).withAlpha (0.78f));
        g.fillPath (p);
    }
    for (auto* d : lit)
    {
        const auto c = at (d->x, d->y);
        const float rr = R * (1 + 1.4f * d->flash);
        g.setColour (d->tint.withAlpha (0.25f * d->flash));
        g.fillEllipse (c.x - rr * 2.6f, c.y - rr * 2.6f, rr * 5.2f, rr * 5.2f);
        g.setColour (d->tint.interpolatedWith (juce::Colours::white, 0.6f * d->flash));
        g.fillEllipse (c.x - rr, c.y - rr, rr * 2, rr * 2);
    }
    // the key: what a dot, its colour, a glowing dot and a line are
    if (m_pic != nullptr && m_pic->numBlocks > 0)
    {
        g.setColour (look::inkDim);
        g.setFont (look::font (11.0f, 0));
        g.drawFittedText ("dot = a piece of a sound    colour = which sound    glowing = playing now    line = a piece it is linked to (Network styles)",
                          getLocalBounds().removeFromBottom (20).reduced (10, 0), juce::Justification::centred, 1, 0.8f);
    }
}

void Swarm::paintHeat (juce::Graphics& g)
{
    // Bencho's Heat map: "The word is drawn once, blurred, into a grey field: bright in the thick
    // of a stroke, fading at its edge ... Every pixel's brightness is then looked up in a
    // 256-entry table of colours. Two strokes close together overlap in the field, so they fuse
    // where the heat pools." Ghost Mosaic: the field is made of the dots instead of a word - every
    // piece adds a soft round of grey, every piece being used adds heat - and it is splatted with a
    // gaussian here rather than blurred with a canvas shadow.
    const int fw = 160, fh = 134;               // a third of the view's own pixels; drawn smooth
    const float cell = W / (float) fw;
    m_field.assign ((size_t) fw * fh * 2, 0.0f);
    float* pres = m_field.data();
    float* hot = m_field.data() + (size_t) fw * fh;
    // a dot's weight so that a shape filled with dots reads about 1 inside
    const float spacing = juce::jmax (2.0f, m_spacing);
    const float sigma = juce::jmax (1.2f, spacing / cell * 0.9f);
    const float norm = (spacing * spacing * 0.866f) / (cell * cell) / (juce::MathConstants<float>::twoPi * sigma * sigma);
    const int k = (int) std::ceil (sigma * 2.5f);
    for (auto& d : m_dots)
    {
        const float fx = d.x / cell, fy = d.y / cell;
        const int x0 = (int) fx, y0 = (int) fy;
        const float h = d.heat * 2.2f + d.flash * 0.6f;
        for (int y = juce::jmax (0, y0 - k); y <= juce::jmin (fh - 1, y0 + k); ++y)
            for (int x = juce::jmax (0, x0 - k); x <= juce::jmin (fw - 1, x0 + k); ++x)
            {
                const float dx = (float) x + 0.5f - fx, dy = (float) y + 0.5f - fy;
                const float w = std::exp (-(dx * dx + dy * dy) / (2 * sigma * sigma)) * norm;
                pres[y * fw + x] += w;
                if (h > 0) hot[y * fw + x] += w * h;
            }
    }
    if (! m_heatImg.isValid() || m_heatImg.getWidth() != fw) m_heatImg = juce::Image (juce::Image::ARGB, fw, fh, true);
    const auto* LUT = heat::lut();
    const auto* GA = heat::greyAlpha();
    juce::Image::BitmapData bd (m_heatImg, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < fh; ++y)
        for (int x = 0; x < fw; ++x)
        {
            const float p = pres[y * fw + x], hh = hot[y * fw + x];
            const int fi = juce::jlimit (0, 255, (int) (p * 200.0f));
            // how much heat map this pixel shows: none where nothing is played, all where a lot is
            const float t = clampf (hh * 3.0f, 0, 1);
            const float m = t * t * (3 - 2 * t);
            // the palette index follows the heat: a little is the green rim, more the white band,
            // the most-used pieces electric purple (the deep core is kept for the wordmark)
            const int v = juce::jlimit (0, 255, heat::kFloor + (int) (clampf (hh * 1.2f, 0, 1) * 0.8f * (255 - heat::kFloor))) * 4;
            const float kk = 1 - m;
            const float a = ((float) GA[fi] * 0.4f * kk + (float) LUT[v + 3] * m) / 255.0f;
            const float rr = heat::kGrey[0] * kk + LUT[v] * m, gg = heat::kGrey[1] * kk + LUT[v + 1] * m, bb = heat::kGrey[2] * kk + LUT[v + 2] * m;
            bd.setPixelColour (x, y, juce::Colour::fromFloatRGBA (rr / 255.0f, gg / 255.0f, bb / 255.0f, a));
        }
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImageTransformed (m_heatImg, juce::AffineTransform::scale (cell).followedBy (m_toView));
}
}
