#pragma once
// SKETCH look: hand-drawn watercolor on rice paper (Ghosty Pro 2, 2.0.15).
// The idea: drawings that feel sketched by hand, with the "imperfections" left in: stray pencil lines that run past the
// corners, soft colour washes that bleed a little past their outline and pool darker at the edges, warm fibrous paper.
//
// Header-only, message thread only, deterministic: every wobble comes from a seed made from the shape's position, so a
// card looks the same on every repaint (nothing shimmers) and only changes when it moves or resizes.
//
//   paper(g, r)                         the paper (one cached texture, made once, ~1400 x 900, drawn scaled to fill)
//   sketchRect(g, r, ink, seed, ...)    a pencil rectangle: two passes, slightly off, lines overshooting the corners
//   sketchPath(g, path, ink, seed, w)   a pencil line along any path (two passes, the second a hair off)
//   wash(g, path, colour, seed, ...)    a watercolor fill: uneven layers, a bleed past the outline, darker pooled edge
//   wobblyRoundRect(r, rad, seed, amp)  a rounded rectangle whose sides breathe a little (hand-drawn)
//   wobbly(path, seed, amp)             any path, re-drawn by a slightly unsteady hand
#include <juce_graphics/juce_graphics.h>
#include <cmath>
#include <cstdint>

namespace ghosty::watercolor {

// the palette: paper, graphite, sepia ink and the soft washes (muted, as if mixed with a lot of water)
namespace wc {
  const juce::Colour paper     { 0xffefe7d6 };   // warm rice paper
  const juce::Colour paperLite { 0xfff6f0e3 };   // cards: a lighter sheet
  const juce::Colour paperDeep { 0xffe4d9c2 };   // insets, value boxes
  const juce::Colour graphite  { 0xff2f2a26 };   // pencil / ink lines and body text
  const juce::Colour pencil    { 0xff8d8273 };   // light pencil, captions
  const juce::Colour sepia     { 0xff5a4636 };
  const juce::Colour sage      { 0xff6f9a64 };   // bamboo / leaf green
  const juce::Colour rose      { 0xffc8707e };   // cherry blossom
  const juce::Colour indigo    { 0xff5b76a3 };   // night sky
  const juce::Colour ochre     { 0xffc9a24a };   // moon
  const juce::Colour plum      { 0xff8b6a9c };
  const juce::Colour peach     { 0xffe0a07c };
}

// a small, fast, seedable random (the same seed always draws the same hand)
struct Rng {
  uint32_t s;
  explicit Rng(uint32_t seed) : s(seed * 2654435761u + 0x9e3779b9u) { if (s == 0) s = 1; }
  uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
  float unit() { return (float) (next() & 0xffffff) / (float) 0xffffff; }          // 0..1
  float bi() { return unit() * 2.0f - 1.0f; }                                         // -1..1
};

inline uint32_t seedOf(juce::Rectangle<float> r, uint32_t salt = 0) {
  return (uint32_t) ((int) r.getX() * 73856093) ^ (uint32_t) ((int) r.getY() * 19349663) ^ (uint32_t) ((int) r.getWidth() * 83492791)
       ^ (uint32_t) ((int) r.getHeight() * 2971215073u) ^ (salt * 668265263u);
}

// ------------------------------------------------------------------ paper
namespace detail {
  // value noise on a grid, smoothly interpolated (for the paper's soft blotches)
  inline float vnoise(const std::vector<float>& grid, int gw, int gh, float x, float y) {
    const int x0 = juce::jlimit(0, gw - 2, (int) x), y0 = juce::jlimit(0, gh - 2, (int) y);
    float fx = juce::jlimit(0.0f, 1.0f, x - (float) x0), fy = juce::jlimit(0.0f, 1.0f, y - (float) y0);
    fx = fx * fx * (3.0f - 2.0f * fx); fy = fy * fy * (3.0f - 2.0f * fy);
    const float a = grid[(size_t) (y0 * gw + x0)], b = grid[(size_t) (y0 * gw + x0 + 1)];
    const float c = grid[(size_t) ((y0 + 1) * gw + x0)], d = grid[(size_t) ((y0 + 1) * gw + x0 + 1)];
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
  }

  inline juce::Image makePaper(int W, int H) {
    juce::Image im(juce::Image::RGB, W, H, false);
    Rng rng(20261111);
    auto grid = [&rng](int gw, int gh) { std::vector<float> v((size_t) (gw * gh)); for (auto& x : v) x = rng.unit(); return v; };
    const int g1w = W / 180 + 3, g1h = H / 180 + 3, g2w = W / 40 + 3, g2h = H / 40 + 3;
    const auto n1 = grid(g1w, g1h), n2 = grid(g2w, g2h);
    const juce::Colour base = wc::paper;
    juce::Image::BitmapData bd(im, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x) {
        const float big = vnoise(n1, g1w, g1h, (float) x / 180.0f, (float) y / 180.0f);   // soft tide marks
        const float mid = vnoise(n2, g2w, g2h, (float) x / 40.0f, (float) y / 40.0f);     // the sheet's cloudiness
        const float grain = rng.unit();                                                   // the tooth
        const float v = (big - 0.5f) * 0.045f + (mid - 0.5f) * 0.03f + (grain - 0.5f) * 0.035f;
        const float warm = (big - 0.5f) * 0.02f;
        auto ch = [v](float c, float k) { return (juce::uint8) juce::jlimit(0, 255, juce::roundToInt((c + v * k) * 255.0f)); };
        bd.setPixelColour(x, y, juce::Colour(ch(base.getFloatRed() + warm, 1.0f), ch(base.getFloatGreen(), 1.0f), ch(base.getFloatBlue() - warm, 1.1f)));
      }
    // fibres: short, faint, curved strands in the sheet
    juce::Graphics g(im);
    for (int i = 0; i < (W * H) / 900; ++i) {
      const float x = rng.unit() * (float) W, y = rng.unit() * (float) H, len = 6.0f + rng.unit() * 22.0f, ang = rng.unit() * juce::MathConstants<float>::twoPi;
      juce::Path f; f.startNewSubPath(x, y);
      f.quadraticTo(x + std::cos(ang + 0.6f) * len * 0.5f, y + std::sin(ang + 0.6f) * len * 0.5f, x + std::cos(ang) * len, y + std::sin(ang) * len);
      g.setColour((rng.unit() < 0.5f ? wc::sepia : juce::Colours::white).withAlpha(0.05f + rng.unit() * 0.07f));
      g.strokePath(f, juce::PathStrokeType(0.6f + rng.unit() * 0.5f));
    }
    // a few pale washes left on the sheet by an earlier painting (the colours of the look, very faint)
    const juce::Colour stains[] { wc::rose, wc::sage, wc::indigo, wc::ochre, wc::peach };
    for (int i = 0; i < 7; ++i) {
      const float cx = rng.unit() * (float) W, cy = rng.unit() * (float) H, rad = 120.0f + rng.unit() * 260.0f;
      const auto c = stains[(size_t) i % 5];
      g.setGradientFill(juce::ColourGradient(c.withAlpha(0.07f), cx, cy, c.withAlpha(0.0f), cx + rad, cy, true));
      g.fillEllipse(cx - rad, cy - rad * 0.8f, rad * 2.0f, rad * 1.6f);
    }
    return im;
  }
}

inline const juce::Image& paperImage() {
  static const juce::Image im = detail::makePaper(1400, 900);
  return im;
}

inline void paper(juce::Graphics& g, juce::Rectangle<int> r) {
  g.drawImage(paperImage(), r.toFloat(), juce::RectanglePlacement::fillDestination);
}

// ------------------------------------------------------------------ an unsteady hand
// re-draws a path in short segments, each point pushed a little sideways by a slow random wave (never jittery)
inline juce::Path wobbly(const juce::Path& src, uint32_t seed, float amp, float step = 7.0f) {
  juce::Path out;
  Rng rng(seed);
  const float ph1 = rng.unit() * 6.28f, ph2 = rng.unit() * 6.28f, f1 = 0.035f + rng.unit() * 0.03f, f2 = 0.11f + rng.unit() * 0.05f;
  juce::PathFlatteningIterator fi(src, {}, 0.6f);
  float dist = 0.0f;
  bool needStart = true;
  auto push = [&](juce::Point<float> p, juce::Point<float> dir, bool start) {
    const float off = amp * (0.7f * std::sin(dist * f1 + ph1) + 0.3f * std::sin(dist * f2 + ph2));
    const juce::Point<float> n(-dir.y, dir.x);
    const auto q = p + n * off;
    if (start) out.startNewSubPath(q); else out.lineTo(q);
  };
  while (fi.next()) {
    const juce::Point<float> a(fi.x1, fi.y1), b(fi.x2, fi.y2);
    const float len = a.getDistanceFrom(b);
    if (fi.subPathIndex == 0) needStart = true;
    if (len > 0.0001f) {
      const auto dir = (b - a) / len;
      if (needStart) { push(a, dir, true); needStart = false; }
      const int n = juce::jmax(1, (int) std::ceil(len / step));
      for (int k = 1; k <= n; ++k) { dist += len / (float) n; push(a + (b - a) * ((float) k / (float) n), dir, false); }
    }
    if (fi.closesSubPath && !needStart) { out.closeSubPath(); needStart = true; }
  }
  return out;
}

inline juce::Path wobblyRoundRect(juce::Rectangle<float> r, float rad, uint32_t seed, float amp = 1.2f) {
  juce::Path p; p.addRoundedRectangle(r, rad);
  return wobbly(p, seed, amp);
}

// a pencil line: graphite laid down twice, the second pass a hair off and lighter, a little darker where the hand pressed
inline void sketchPath(juce::Graphics& g, const juce::Path& p, juce::Colour ink, uint32_t seed, float width = 1.1f) {
  g.setColour(ink.withMultipliedAlpha(0.78f));
  g.strokePath(wobbly(p, seed, 0.8f), juce::PathStrokeType(width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
  g.setColour(ink.withMultipliedAlpha(0.32f));
  g.strokePath(wobbly(p, seed + 77, 1.3f).createPathWithRoundedCorners(2.0f), juce::PathStrokeType(width * 0.75f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// a pencil rectangle as an artist sketches a frame: four separate strokes that run a little past the corners (the stray
// lines are left in), drawn twice; 'over' = how far a line overshoots
inline void sketchRect(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour ink, uint32_t seed, float width = 1.1f, float over = 6.0f) {
  Rng rng(seed);
  const juce::Point<float> c[4] { r.getTopLeft(), r.getTopRight(), r.getBottomRight(), r.getBottomLeft() };
  for (int pass = 0; pass < 2; ++pass) {
    for (int s = 0; s < 4; ++s) {
      auto a = c[s], b = c[(s + 1) % 4];
      const auto d = (b - a) / juce::jmax(0.001f, a.getDistanceFrom(b));
      const float o1 = over * (0.2f + rng.unit()) * (pass == 0 ? 1.0f : 0.5f), o2 = over * (0.1f + rng.unit()) * (pass == 0 ? 1.0f : 0.5f);
      a -= d * o1; b += d * o2;
      const juce::Point<float> n(-d.y, d.x);
      a += n * rng.bi() * 1.2f; b += n * rng.bi() * 1.2f;
      juce::Path l; l.startNewSubPath(a); l.lineTo(b);
      g.setColour(ink.withMultipliedAlpha(pass == 0 ? 0.75f : 0.3f));
      g.strokePath(wobbly(l, seed + (uint32_t) (s * 31 + pass * 7), pass == 0 ? 0.7f : 1.1f, 9.0f),
                   juce::PathStrokeType(pass == 0 ? width : width * 0.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
  }
}

// a watercolor wash: three uneven, slightly shifted layers of thin colour; the water carries a little of it past the
// outline (the bleed), and pigment pools darker along the edge where it dried
inline void wash(juce::Graphics& g, const juce::Path& shape, juce::Colour c, uint32_t seed, float strength = 1.0f, float bleed = 3.0f) {
  Rng rng(seed);
  const auto b = shape.getBounds();
  for (int i = 0; i < 4; ++i) {
    const float dx = rng.bi() * bleed * 0.7f, dy = rng.bi() * bleed * 0.7f;
    const float sx = 1.0f - rng.unit() * 0.035f, sy = 1.0f - rng.unit() * 0.035f;
    auto layer = wobbly(shape, seed + (uint32_t) i * 101, bleed * (0.6f + 0.6f * rng.unit()), 12.0f);
    layer.applyTransform(juce::AffineTransform::scale(sx, sy, b.getCentreX(), b.getCentreY()).translated(dx, dy));
    // lighter where the brush lifted: a gradient across the layer in a random direction
    const float ang = rng.unit() * 6.28f, rr = juce::jmax(b.getWidth(), b.getHeight()) * 0.55f;
    const auto from = b.getCentre() + juce::Point<float>(std::cos(ang), std::sin(ang)) * rr;
    const auto to = b.getCentre() - juce::Point<float>(std::cos(ang), std::sin(ang)) * rr;
    g.setGradientFill(juce::ColourGradient(c.withMultipliedAlpha(0.17f * strength), from.x, from.y,
                                           c.withMultipliedAlpha(0.03f * strength), to.x, to.y, false));
    g.fillPath(layer);
  }
  // the dried edge: pigment pools where the water stopped
  g.setColour(c.withMultipliedAlpha(0.30f * strength));
  g.strokePath(wobbly(shape, seed + 999, bleed * 0.5f, 8.0f), juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
  g.setColour(c.withMultipliedAlpha(0.10f * strength));
  g.strokePath(wobbly(shape, seed + 998, bleed * 0.9f, 10.0f), juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
  // a couple of blooms (back-runs): pale rings where a drop of water landed in the drying wash
  for (int i = 0; i < 2; ++i) {
    if (b.getWidth() < 40.0f || b.getHeight() < 24.0f) break;
    const float bx = b.getX() + b.getWidth() * (0.15f + 0.7f * rng.unit()), by = b.getY() + b.getHeight() * (0.2f + 0.6f * rng.unit());
    const float br = juce::jmin(b.getWidth(), b.getHeight()) * (0.12f + 0.12f * rng.unit());
    juce::Graphics::ScopedSaveState ss(g);
    g.reduceClipRegion(shape);
    juce::Path ring; ring.addEllipse(bx - br, by - br * 0.8f, br * 2.0f, br * 1.6f);
    g.setColour(c.withMultipliedAlpha(0.12f * strength));
    g.strokePath(wobbly(ring, seed + 555 + (uint32_t) i, br * 0.08f, 6.0f), juce::PathStrokeType(1.2f));
  }
}

inline void washRect(juce::Graphics& g, juce::Rectangle<float> r, float rad, juce::Colour c, uint32_t seed, float strength = 1.0f, float bleed = 3.0f) {
  juce::Path p; p.addRoundedRectangle(r, rad);
  wash(g, p, c, seed, strength, bleed);
}

} // namespace ghosty::watercolor
