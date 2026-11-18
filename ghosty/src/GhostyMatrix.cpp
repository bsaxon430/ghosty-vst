#include "GhostyMatrix.h"
#include "GhostyMatrixData.h"
#include "GhostyLook.h"
#include "GhostyProLogo.h"
#include "Watercolor.h"

namespace ghosty {
namespace {
  constexpr int kLevels = 32;
  // Brandon's 7x7 letters (G H O S T Y)
  const char* kGlyphs[6][7] = {
    { "0111110", "1100001", "1000000", "1011110", "1000001", "1100001", "0111110" },
    { "1100011", "1100011", "1100011", "1111111", "1100011", "1100011", "1100011" },
    { "0111110", "1100011", "1100011", "1100011", "1100011", "1100011", "0111110" },
    { "0111110", "1100000", "1100000", "0111110", "0000011", "0000011", "0111110" },
    { "1111111", "1111111", "0011100", "0011100", "0011100", "0011100", "0011100" },
    { "1100011", "1100011", "0110110", "0011100", "0001000", "0001000", "0001000" } };
  static_assert(matrixdata::kFrames > 8, "clip data");
  constexpr double kHoverSpeed = 1.6;   // a hover replay runs a little faster than the opening

  // the clip, unpacked once: kFrames x 6 x 7 x 7 levels 0..15
  const std::vector<uint8_t>& clip() {
    static const std::vector<uint8_t> data = [] {
      MemoryOutputStream out;
      Base64::convertFromBase64(out, String(matrixdata::kClipB64));
      const auto* p = (const uint8_t*) out.getData();
      const size_t n = (size_t) matrixdata::kFrames * 294;
      std::vector<uint8_t> v(n, 0);
      for (size_t i = 0; i < n && i / 2 < out.getDataSize(); ++i)
        v[i] = (i & 1) ? (uint8_t) (p[i / 2] & 15) : (uint8_t) (p[i / 2] >> 4);
      return v;
    }();
    return data;
  }
}

MatrixRow::MatrixRow() {
  setOpaque(false);
  setInterceptsMouseClicks(true, true);   // hover or click replays a letter
  setMouseCursor(MouseCursor::PointingHandCursor);
  setTooltip("GHOSTY: move over or click a letter to replay it");
}

bool MatrixRow::glyphBit(int tile, int row, int col) { return kGlyphs[jlimit(0, 5, tile)][jlimit(0, 6, row)][jlimit(0, 6, col)] == '1'; }

bool MatrixRow::idle() const {
  if (intro_) return false;
  for (auto& s : state_) if (s.start >= 0.0) return false;
  return true;
}

bool MatrixRow::tick(double nowMs) {
  now_ = nowMs;
  if (intro_) {   // the opening play: all tiles together, from the first tick
    intro_ = false;
    for (auto& s : state_) { s.start = nowMs; s.speed = 1.0; }
  }
  bool any = false;
  for (auto& s : state_) {
    if (s.start < 0.0) continue;
    if ((nowMs - s.start) * 0.001 * s.speed * matrixdata::kFps >= (double) (matrixdata::kFrames - 1)) s.start = -1.0;   // settled on its letter
    else any = true;
  }
  return any;
}

void MatrixRow::hoverTile(int tile, double nowMs) {
  if (tile < 0 || tile >= kTiles) return;
  intro_ = false;
  state_[(size_t) tile].start = nowMs;
  state_[(size_t) tile].speed = kHoverSpeed;
  now_ = nowMs;
}

Rectangle<float> MatrixRow::tileRect(int t) const {
  const float gap = (float) getHeight() * 0.09f;
  const float side = (float) getHeight();
  const float total = side * (float) kTiles + gap * (float) (kTiles - 1);
  const float x0 = ((float) getWidth() - total) * 0.5f;
  return { x0 + (float) t * (side + gap), 0.0f, side, side };
}

int MatrixRow::tileAt(Point<float> p) const {
  for (int t = 0; t < kTiles; ++t) if (tileRect(t).contains(p)) return t;
  return -1;
}

void MatrixRow::mouseMove(const MouseEvent& e) {
  const int t = tileAt(e.position);
  if (t != hover_) {
    hover_ = t;
    if (t >= 0) { hoverTile(t, Time::getMillisecondCounterHiRes()); repaint(); }
  }
}

void MatrixRow::mouseDown(const MouseEvent& e) {
  const double now = Time::getMillisecondCounterHiRes();
  const int t = tileAt(e.position);
  if (t >= 0) hoverTile(t, now);
  else for (int i = 0; i < kTiles; ++i) hoverTile(i, now);   // a click between letters replays the whole word
  repaint();
}

void MatrixRow::mouseExit(const MouseEvent&) { hover_ = -1; }

float MatrixRow::dotLevel(int tile, int row, int col) const {
  const auto& s = state_[(size_t) tile];
  if (s.start < 0.0 && !intro_) return glyphBit(tile, row, col) ? 1.0f : 0.0f;
  double f = s.start < 0.0 ? 0.0 : (now_ - s.start) * 0.001 * s.speed * matrixdata::kFps;   // before the first tick: the clip's first frame
  f = jlimit(0.0, (double) (matrixdata::kFrames - 1), f);
  const int i0 = (int) f, i1 = jmin(i0 + 1, matrixdata::kFrames - 1);
  const float a = (float) (f - i0);
  const auto& c = clip();
  const size_t k = (size_t) (tile * 49 + row * 7 + col);
  return (c[(size_t) i0 * 294 + k] * (1.0f - a) + c[(size_t) i1 * 294 + k] * a) / 15.0f;
}

void MatrixRow::resized() { spriteScale_ = 0.0f; }

// the dot at brightness b: lime-green core with a soft glow when lit, a small faint purple dot when off, purple-green in between
void MatrixRow::rebuildSprites(float scale) {
  const auto& p = pal();
  spriteGreen_ = p.green; spritePurple_ = p.purple; spriteScale_ = scale; spriteTile_ = getHeight(); spriteSketch_ = p.sketch;
  const float pitch = (float) getHeight() * 0.1f;
  const int px = jmax(4, roundToInt(pitch * 3.0f * scale));
  sprites_.clear();
  for (int l = 0; l < kLevels; ++l) {
    const float b = (float) l / (float) (kLevels - 1);
    Image im(Image::ARGB, px, px, true);
    Graphics g(im);
    const float c = (float) px * 0.5f, u = pitch * scale;
    const float on = jlimit(0.0f, 1.0f, (b - 0.12f) / 0.6f);
    if (p.sketch) {   // ink dots on paper: a faint pencil dot when off, a sage wash with a graphite centre when lit
      const float u2 = u;
      if (b > 0.06f) {
        const float gr = u2 * (0.45f + 0.7f * b);
        g.setGradientFill(ColourGradient(p.green.withAlpha(0.45f * b), c, c, p.green.withAlpha(0.0f), c + gr, c, true));
        g.fillEllipse(c - gr, c - gr, gr * 2.0f, gr * 2.0f);
      }
      const float r = u2 * (0.10f + 0.30f * b);
      g.setColour(ghosty::watercolor::wc::graphite.withAlpha(0.18f + 0.72f * jmin(1.0f, b * 1.6f)));
      g.fillEllipse(c - r, c - r, r * 2.0f, r * 2.0f);
      sprites_.push_back(im);
      continue;
    }
    const Colour body = spritePurple_.interpolatedWith(spriteGreen_, on).interpolatedWith(Colours::white, jlimit(0.0f, 0.55f, (b - 0.8f) * 2.7f));
    if (b > 0.06f) {   // glow
      const float gr = u * (0.5f + 0.95f * b);
      g.setGradientFill(ColourGradient(body.withAlpha(0.55f * b), c, c, body.withAlpha(0.0f), c + gr, c, true));
      g.fillEllipse(c - gr, c - gr, gr * 2.0f, gr * 2.0f);
    }
    const float r = u * (0.10f + 0.36f * b);
    g.setColour(body.withAlpha(0.35f + 0.65f * jmin(1.0f, b * 2.0f)));
    g.fillEllipse(c - r, c - r, r * 2.0f, r * 2.0f);
    sprites_.push_back(im);
  }
}

void MatrixRow::paint(Graphics& g) {
  const float scale = jmax(1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
  const auto& p = pal();
  if (sprites_.empty() || scale != spriteScale_ || spriteGreen_ != p.green || spritePurple_ != p.purple || spriteTile_ != getHeight() || spriteSketch_ != p.sketch)
    rebuildSprites(scale);
  const float pitch = (float) getHeight() * 0.1f, sz = (float) sprites_[0].getWidth() / scale;
  for (int t = 0; t < kTiles; ++t) {
    const auto r = tileRect(t);
    if (p.sketch) {   // a little square of paper with a pencil frame
      namespace wcol = ghosty::watercolor;
      auto sq = wcol::wobblyRoundRect(r, r.getWidth() * 0.08f, (uint32_t) (t * 7919 + 13), 0.5f);
      g.setColour(wcol::wc::paperLite.withAlpha(0.8f)); g.fillPath(sq);
      wcol::sketchPath(g, sq, wcol::wc::graphite.withAlpha(0.55f), (uint32_t) (t * 104729 + 7), 0.8f);
    }
    else {
      g.setColour(Colour(0xff12171c));
      g.fillRoundedRectangle(r, r.getWidth() * 0.085f);
      g.setColour(Colours::white.withAlpha(0.07f));
      g.drawRoundedRectangle(r.reduced(0.5f), r.getWidth() * 0.085f, 1.0f);
    }
    Graphics::ScopedSaveState ss(g);
    Path clipP; clipP.addRoundedRectangle(r, r.getWidth() * 0.085f);
    g.reduceClipRegion(clipP);
    g.setColour(Colours::black);   // (drawImage paints with the current colour's opacity: opaque)
    for (int y = 0; y < kDots; ++y)
      for (int x = 0; x < kDots; ++x) {
        const float b = dotLevel(t, y, x);
        const int lv = jlimit(0, kLevels - 1, roundToInt(b * (float) (kLevels - 1)));
        const float cx = r.getX() + r.getWidth() * 0.5f + ((float) x - 3.0f) * pitch;
        const float cy = r.getY() + r.getHeight() * 0.5f + ((float) y - 3.0f) * pitch;
        // (placed on whole physical pixels so the dots stay sharp at 1x and 2x)
        const float ox = std::round((cx - sz * 0.5f) * scale) / scale, oy = std::round((cy - sz * 0.5f) * scale) / scale;
        g.drawImage(sprites_[(size_t) lv], Rectangle<float>(ox, oy, sz, sz));
      }
  }
}


HeaderLogoLayout layoutHeaderLogo(Rectangle<int> header, int logoWidth) {
  HeaderLogoLayout l;
  l.ghost = Rectangle<int>(header.getX() + 8, header.getY() + 4, 54, header.getHeight() - 8);
  auto row = Rectangle<int>(l.ghost.getRight() + 6, header.getY() + 4, jmax(60, header.getX() + 8 + logoWidth - 4 - (l.ghost.getRight() + 6)), header.getHeight() - 8);
  const int tagH = 12;
  l.tag = row.removeFromBottom(tagH);
  row.removeFromBottom(1);
  const float span = 6.0f + 5.0f * 0.09f;   // six square tiles with a gap of 9% of a tile: the biggest that fits
  const int side = jmax(8, jmin(row.getHeight(), roundToInt((float) row.getWidth() / span)));
  l.row = Rectangle<int>(row.getX(), row.getY(), roundToInt((float) side * span), side);
  l.tag = l.tag.withX(l.row.getX()).withWidth(l.row.getWidth());
  return l;
}

// SKETCH: the ghost as a quick watercolor study: a pale blue-grey wash for the body that bleeds past a pencil outline
// drawn twice, two ink eyes, a touch of rose on the cheek. The wash and lines stay still; only the float moves it.
void paintSketchGhost(Graphics& g, Rectangle<float> box, float t, float bobPx) {
  namespace wcol = ghosty::watercolor;
  const float R = jmax(1.0f, jmin(box.getWidth() / 1.55f, (box.getHeight() - 2.0f * (std::abs(bobPx) + 1.0f)) / 2.7f));
  const float cx = box.getCentreX(), cy = box.getCentreY() + bobPx - 0.07f * R;
  Graphics::ScopedSaveState ss(g);
  g.reduceClipRegion(box.getSmallestIntegerContainer());
  // the same silhouette as the neon ghost (its body path), still: the hand-drawn one does not sway
  Path body;
  body.startNewSubPath(-0.34f, -1.04f);
  body.cubicTo(0.10f, -1.0f, 0.50f, -0.70f, 0.52f, -0.20f);
  body.cubicTo(0.54f, 0.20f, 0.34f, 0.42f, 0.22f, 0.62f);
  body.cubicTo(0.12f, 0.80f, 0.14f, 0.96f, 0.02f, 1.18f);
  body.cubicTo(-0.10f, 0.96f, -0.30f, 0.84f, -0.38f, 0.56f);
  body.cubicTo(-0.52f, 0.24f, -0.52f, -0.12f, -0.50f, -0.38f);
  body.cubicTo(-0.48f, -0.68f, -0.42f, -0.88f, -0.34f, -1.04f);
  body.closeSubPath();
  body.applyTransform(AffineTransform::scale(R).translated(cx, cy));
  g.setColour(wcol::wc::paperLite);
  g.fillPath(body);
  wcol::wash(g, body, wcol::wc::indigo, 4242, 1.1f, jmax(1.0f, R * 0.08f));
  wcol::sketchPath(g, body, wcol::wc::graphite, 4243, jmax(0.8f, R * 0.045f));
  // eyes: two ink drops (a slow blink every few seconds)
  const float blink = std::fmod(t, 5.3f) < 0.12f ? 0.25f : 1.0f;
  g.setColour(wcol::wc::graphite.withAlpha(0.9f));
  for (float ex : { -0.22f, 0.12f })
    g.fillEllipse(cx + ex * R - 0.055f * R, cy - 0.38f * R - 0.08f * R * blink, 0.11f * R, 0.16f * R * blink);
  // a blush of rose
  g.setGradientFill(ColourGradient(wcol::wc::rose.withAlpha(0.45f), cx + 0.25f * R, cy - 0.14f * R, wcol::wc::rose.withAlpha(0.0f),
                                   cx + 0.25f * R + 0.14f * R, cy - 0.14f * R, true));
  g.fillEllipse(cx + 0.11f * R, cy - 0.26f * R, 0.28f * R, 0.24f * R);
}

void paintHeaderLogo(Graphics& g, const HeaderLogoLayout& l, float t) {
  const auto& p = pal();
  // the ghost: a slow float (about 2.6 s, a few pixels) and a soft glow pulse (about 3.1 s), always inside its box
  const float bob = 2.5f * std::sin(t * MathConstants<float>::twoPi / 2.6f);
  const float glow = 0.85f + 0.25f * std::sin(t * MathConstants<float>::twoPi / 3.1f);
  if (p.sketch)
    paintSketchGhost(g, l.ghost.toFloat(), t, bob);
  else
    ghosty::prologo::drawMascotFit(g, l.ghost.toFloat(), p.card, t, bob, glow);
  // PRO 2 (small, under the row): the product name with a green-boxed VST tag
  if (!l.tag.isEmpty()) {
    const bool light = ghosty::logo::isLight(p.card);
    const auto col = light ? p.green.darker(0.3f) : p.green;
    auto r = l.tag.toFloat();
    g.setFont(Font((float) l.tag.getHeight() * 0.82f, Font::bold | Font::italic));
    g.setColour(light ? p.ink : Colours::white);
    g.drawText("PRO 2", r.removeFromLeft(r.getHeight() * 3.2f), Justification::centredLeft, false);
    auto tag = r.removeFromLeft(r.getHeight() * 2.2f).reduced(0.0f, 1.0f);
    g.setColour(col);
    g.drawRect(tag, 1.0f);
    g.setFont(Font(tag.getHeight() * 0.72f, Font::bold));
    g.drawText("VST", tag, Justification::centred, false);
    r.removeFromLeft(5.0f);
    g.setColour(col.withAlpha(0.55f));
    g.drawLine(r.getX(), r.getCentreY(), r.getRight(), r.getCentreY(), 1.0f);
  }
}

}
