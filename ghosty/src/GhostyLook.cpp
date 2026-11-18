#include "GhostyLook.h"
#include "GhostyProcessor.h"
#include "synth_parameters.h"
#include "Watercolor.h"

namespace ghosty {

namespace {
  Look g_look = Look::Ghost;
  namespace wcol = ghosty::watercolor;

  Pal makePal(Look l) {
    Pal p;
    if (l == Look::Light) {   // LIGHT: rice paper, graphite, watercolor washes
      p.sketch = true;
      p.bgTop = p.bgBottom = wcol::wc::paper;
      p.card = wcol::wc::paperLite; p.cardEdge = wcol::wc::graphite; p.cardShade = Colours::transparentBlack;
      p.ink = wcol::wc::graphite; p.dim = Colour(0xff6a6055);
      p.accent = wcol::wc::indigo; p.accent2 = wcol::wc::rose; p.accent3 = wcol::wc::sage; p.accent4 = wcol::wc::ochre;
      p.knobFace = wcol::wc::paperLite; p.knobTrack = wcol::wc::paperDeep;
      p.pill = wcol::wc::paperLite; p.pillOn = wcol::wc::sage; p.pillInk = wcol::wc::graphite; p.pillOnInk = wcol::wc::graphite;
      p.screen = wcol::wc::paperDeep; p.wave1 = wcol::wc::rose; p.wave2 = wcol::wc::indigo;
      p.keyWhite = Colour(0xfff8f3e8); p.keyBlack = Colour(0xff3a332d);
      p.good = wcol::wc::sage; p.warn = wcol::wc::ochre;
      p.green = wcol::wc::sage; p.purple = wcol::wc::plum;
      return p;
    }
    if (l == Look::Ghost) {   // GHOST: Brandon's target look (neon green + purple on near-black)
      p.neon = true;
      p.green = Colour(0xff6cff2a); p.purple = Colour(0xffc04dff);
      p.bgTop = Colour(0xff050706); p.bgBottom = Colour(0xff030404);
      p.card = Colour(0xff0b0d0f); p.cardEdge = Colour(0xff6cff2a).withAlpha(0.45f); p.cardShade = Colour(0x99000000);
      p.ink = Colour(0xfff1f6ee); p.dim = Colour(0xffa3ad9f);
      p.accent = p.green; p.accent2 = p.purple; p.accent3 = p.green; p.accent4 = Colour(0xffffc93d);
      p.knobFace = Colour(0xff15181b); p.knobTrack = Colour(0xff1f281b);
      p.pill = Colour(0xff0e1113); p.pillOn = p.green; p.pillInk = Colour(0xfff1f6ee); p.pillOnInk = Colour(0xff061004);
      p.screen = Colour(0xff07090a); p.wave1 = p.purple; p.wave2 = p.green;
      p.keyWhite = Colour(0xfffbf8ff); p.keyBlack = Colour(0xff0b0d0f);
      p.good = p.green; p.warn = Colour(0xffffc93d);
      return p;
    }
    const auto& q = themes::get(themes::Theme::Dark);   // BLACK
    p.bgTop = q.windowTop; p.bgBottom = q.windowBottom; p.card = q.panel; p.cardEdge = q.line; p.cardShade = q.shadow;
    p.ink = q.ink; p.dim = q.dim; p.accent = q.violet; p.accent2 = q.accent2; p.accent3 = q.accent3; p.accent4 = q.warn;
    p.knobFace = q.isLight ? q.card : q.cell; p.knobTrack = q.line;
    p.pill = q.cell; p.pillOn = q.violet; p.pillInk = q.ink; p.pillOnInk = q.onAccent;
    p.screen = q.waveBg; p.wave1 = q.waveHi; p.wave2 = q.waveLo; p.keyWhite = q.keyWhite; p.keyBlack = q.keyBlack;
    p.good = q.good; p.warn = q.warn;
    return p;
  }

  Pal& palStore() { static Pal p = makePal(Look::Ghost); return p; }
}

const Pal& pal() { return palStore(); }
void setLook(Look look) { g_look = look; palStore() = makePal(look); }
Look currentLook() { return g_look; }

ghostorbit::OrbitColours orbitColours() {
  ghostorbit::OrbitColours c;   // GHOST defaults
  switch (g_look) {
    case Look::Black:  c.base = Colour(0xffa77bff); c.rim = Colour(0xff3a2a55); c.unlit = Colour(0xff2a1d40); c.body = Colour(0xff110a1b); break;
    case Look::Light:  c.base = wcol::wc::indigo; c.rim = Colour(0xff9a8f80); c.unlit = Colour(0xffded3bd); c.body = wcol::wc::paperLite;
                       c.lead = c.mod = Colour(0xffb0596a); c.pointer = wcol::wc::graphite; c.sketch = true; break;
    default: break;
  }
  return c;
}

const themes::Palette& boardPalette() {
  static const themes::Palette sketch = [] {   // the SPIRIT BOARD on paper: a light palette in the watercolor colours
    themes::Palette q = themes::makePalette(themes::Theme::Light);
    q.window = q.windowTop = q.windowBottom = wcol::wc::paper;
    q.panel = wcol::wc::paperLite; q.card = wcol::wc::paperLite; q.cell = wcol::wc::paperDeep; q.line = Colour(0xff9a8f80);
    q.ink = wcol::wc::graphite; q.dim = Colour(0xff6a6055); q.onAccent = wcol::wc::paperLite; q.overlay = wcol::wc::sepia;
    q.accent1 = Colour(0xffa84a5c); q.accent2 = Colour(0xff3f5f8f); q.accent3 = Colour(0xff4f7a45); q.violet = Colour(0xff6c4f80);
    q.good = q.accent3; q.warn = Colour(0xff8c6a1c);
    q.keyWhite = Colour(0xfff8f3e8); q.keyBlack = Colour(0xff3a332d);
    q.waveBg = Colour(0xffe9dfca); q.waveLo = q.accent2; q.waveHi = q.accent1; q.shadow = Colour(0x22403020);
    return q;
  }();
  switch (g_look) {
    case Look::Light: return sketch;
    default: return themes::get(themes::Theme::Dark);
  }
}

void paintBackground(Graphics& g, Rectangle<int> r) {
  const auto& p = pal();
  if (p.sketch) {   // the sheet of paper, and a soft wash pooled in two corners
    wcol::paper(g, r);
    auto rf = r.toFloat();
    auto pool = [&g](float x, float y, float rad, Colour c) {
      g.setGradientFill(ColourGradient(c.withAlpha(0.10f), x, y, c.withAlpha(0.0f), x + rad, y, true));
      g.fillEllipse(x - rad, y - rad, rad * 2, rad * 2);
    };
    pool(rf.getRight() - 40.0f, rf.getY() + 30.0f, 320.0f, wcol::wc::peach);
    pool(rf.getX() + 60.0f, rf.getBottom() - 20.0f, 360.0f, wcol::wc::sage);
    return;
  }
  ColourGradient grad(p.bgTop, (float) r.getX(), (float) r.getY(), p.bgBottom, (float) r.getRight(), (float) r.getBottom(), false);
  if (p.glass) {
    grad.addColour(0.45, Colour(0xffe6e1f7));
    grad.addColour(0.8, Colour(0xffe9e0f3));
  }
  g.setGradientFill(grad);
  g.fillRect(r);
  if (p.neon) {   // the neon glow around the window's edges
    auto r2 = r.toFloat();
    auto edge = [&g](Rectangle<float> band, Point<float> from, Point<float> to, Colour c) {
      g.setGradientFill(ColourGradient(c, from.x, from.y, c.withAlpha(0.0f), to.x, to.y, false));
      g.fillRect(band);
    };
    const Colour c = p.green.withAlpha(0.28f);
    const float w = 26.0f;
    edge(r2.withWidth(w), { r2.getX(), 0 }, { r2.getX() + w, 0 }, c);
    edge(r2.withTrimmedLeft(r2.getWidth() - w), { r2.getRight(), 0 }, { r2.getRight() - w, 0 }, c);
    edge(r2.withHeight(w * 0.6f), { 0, r2.getY() }, { 0, r2.getY() + w * 0.6f }, c.withMultipliedAlpha(0.7f));
    edge(r2.withTrimmedTop(r2.getHeight() - w), { 0, r2.getBottom() }, { 0, r2.getBottom() - w }, c);
    auto bloom = [&g](float x, float y, float rad, Colour col) {
      g.setGradientFill(ColourGradient(col, x, y, col.withAlpha(0.0f), x + rad, y, true));
      g.fillEllipse(x - rad, y - rad, rad * 2, rad * 2);
    };
    bloom(r2.getX(), r2.getY() + r2.getHeight() * 0.35f, 150.0f, p.green.withAlpha(0.16f));
    bloom(r2.getRight(), r2.getY() + r2.getHeight() * 0.6f, 160.0f, p.green.withAlpha(0.14f));
    bloom(r2.getX() + 60.0f, r2.getY() + 20.0f, 110.0f, p.green.withAlpha(0.18f));
    g.setColour(p.green.withAlpha(0.5f));
    g.drawRoundedRectangle(r2.reduced(2.0f), 14.0f, 1.2f);
  }
  if (p.glass) {   // soft blooms behind the glass
    auto bloom = [&g](float x, float y, float rad, Colour c) {
      ColourGradient cg(c, x, y, c.withAlpha(0.0f), x + rad, y, true);
      g.setGradientFill(cg);
      g.fillEllipse(x - rad, y - rad, rad * 2, rad * 2);
    };
    bloom(r.getWidth() * 0.82f, r.getHeight() * 0.18f, 380.0f, Colour(0x40a5f0dc));
    bloom(r.getWidth() * 0.15f, r.getHeight() * 0.85f, 420.0f, Colour(0x30c8b8ff));
    bloom(r.getWidth() * 0.6f, r.getHeight() * 0.9f, 300.0f, Colour(0x30f5c0e8));
  }
}

void drawCard(Graphics& g, Rectangle<float> r, const String& title, Colour titleColour) {
  const auto& p = pal();
  if (p.sketch) {   // a lighter sheet laid on the paper, a pale wash in the title's colour, a pencil frame with stray lines
    const auto seed = wcol::seedOf(r, 1);
    auto sheet = wcol::wobblyRoundRect(r, 6.0f, seed, 0.9f);
    g.setColour(wcol::wc::paperLite.withAlpha(0.72f));
    g.fillPath(sheet);
    if (title.isNotEmpty())
      wcol::washRect(g, r.reduced(6.0f), 10.0f, titleColour, seed + 3, 0.38f, 5.0f);
    wcol::sketchRect(g, r, p.cardEdge, seed + 5, 1.0f, 7.0f);
    if (title.isNotEmpty()) {
      auto tr = r.reduced(16, 10).withHeight(16);
      // the title: graphite, with a quick brush of its colour under it
      const float tw = jmin(tr.getWidth(), LnF::font(12.5f).getStringWidthFloat(title) + 6.0f);
      juce::Path stroke; stroke.addRoundedRectangle(tr.getX() - 4.0f, tr.getCentreY() - 1.0f, tw + 8.0f, 8.0f, 4.0f);
      wcol::wash(g, stroke, titleColour, seed + 9, 1.6f, 1.5f);
      g.setColour(p.ink);
      g.setFont(LnF::font(12.5f));
      g.drawText(title, tr, Justification::centredLeft);
    }
    return;
  }
  const float rad = 16.0f;
  if (p.glass) {
    for (int i = 3; i >= 1; --i)
      g.setColour(p.cardShade.withMultipliedAlpha(0.35f)), g.fillRoundedRectangle(r.translated(0, (float) i * 1.5f).expanded((float) i), rad + (float) i);
    ColourGradient cg(p.card.brighter(0.04f), r.getX(), r.getY(), p.card.withAlpha(0.86f), r.getX(), r.getBottom(), false);
    g.setGradientFill(cg);
  }
  else
    g.setColour(p.card);
  g.fillRoundedRectangle(r, rad);
  g.setColour(p.cardEdge.withAlpha(p.glass ? 0.9f : 1.0f));
  g.drawRoundedRectangle(r.reduced(0.5f), rad, 1.0f);
  if (p.neon) {   // a faint neon bloom along the outline
    g.setColour(p.cardEdge.withMultipliedAlpha(0.25f));
    g.drawRoundedRectangle(r.expanded(1.0f), rad + 1.0f, 2.5f);
  }
  if (p.glass) {
    g.setColour(Colours::white.withAlpha(0.7f));
    g.drawLine(r.getX() + rad, r.getY() + 1.2f, r.getRight() - rad, r.getY() + 1.2f, 1.0f);
  }
  if (title.isNotEmpty()) {
    g.setColour(titleColour);
    g.setFont(LnF::font(12.5f));
    g.drawText(title, r.reduced(16, 10).withHeight(16), Justification::centredLeft);
  }
}

void drawScreen(Graphics& g, Rectangle<float> r) {
  const auto& p = pal();
  if (p.sketch) {   // a pale, deeper inset of paper with a single pencil line around it
    const auto seed = wcol::seedOf(r, 2);
    auto inset = wcol::wobblyRoundRect(r, 8.0f, seed, 0.8f);
    g.setColour(p.screen.withAlpha(0.85f));
    g.fillPath(inset);
    wcol::sketchPath(g, inset, p.cardEdge.withAlpha(0.6f), seed + 1, 0.9f);
    return;
  }
  g.setColour(p.screen);
  g.fillRoundedRectangle(r, 10.0f);
  if (p.glass) {
    ColourGradient cg(Colours::white.withAlpha(0.35f), r.getX(), r.getY(), Colours::transparentWhite, r.getX(), r.getY() + r.getHeight() * 0.6f, false);
    g.setGradientFill(cg);
    g.fillRoundedRectangle(r, 10.0f);
  }
  g.setColour(p.cardEdge);
  g.drawRoundedRectangle(r.reduced(0.5f), 10.0f, 1.0f);
}

void drawGlowLine(Graphics& g, const Path& path, Colour a, Colour b, Rectangle<float> r) {
  if (pal().sketch) {   // a wet brush line: a wide pale stroke of colour, then a fine ink line over it
    ColourGradient cg(a, r.getX(), r.getCentreY(), b, r.getRight(), r.getCentreY(), false);
    g.setGradientFill(cg);
    g.setOpacity(0.22f);
    g.strokePath(path, PathStrokeType(7.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setOpacity(1.0f);
    g.setColour(pal().ink.withAlpha(0.8f));
    g.strokePath(path, PathStrokeType(1.3f, PathStrokeType::curved, PathStrokeType::rounded));
    return;
  }
  ColourGradient cg(a, r.getX(), r.getCentreY(), b, r.getRight(), r.getCentreY(), false);
  g.setGradientFill(cg);
  g.setOpacity(0.25f);
  g.strokePath(path, PathStrokeType(6.0f, PathStrokeType::curved, PathStrokeType::rounded));
  g.setOpacity(1.0f);
  g.strokePath(path, PathStrokeType(2.2f, PathStrokeType::curved, PathStrokeType::rounded));
}

// ------------------------------------------------------------------ look and feel
LnF::LnF() { refresh(); }

Font LnF::font(float h, bool bold) {
  return Font(Font::getDefaultSansSerifFontName(), h, bold ? Font::bold : Font::plain);
}

void LnF::refresh() {
  const auto& p = pal();
  setColour(PopupMenu::backgroundColourId, p.card);
  setColour(PopupMenu::textColourId, p.ink);
  setColour(PopupMenu::highlightedBackgroundColourId, p.pillOn);
  setColour(PopupMenu::highlightedTextColourId, p.pillOnInk);
  setColour(ComboBox::textColourId, p.pillInk);
  setColour(ComboBox::arrowColourId, p.dim);
  setColour(Label::textColourId, p.ink);
  setColour(TextEditor::backgroundColourId, p.pill);
  setColour(TextEditor::textColourId, p.ink);
  setColour(TextEditor::outlineColourId, p.cardEdge);
  setColour(TextEditor::focusedOutlineColourId, p.accent);
  setColour(ListBox::backgroundColourId, Colours::transparentBlack);
  setColour(ScrollBar::thumbColourId, p.accent.withAlpha(0.5f));
  setColour(TooltipWindow::backgroundColourId, p.card);
  setColour(TooltipWindow::textColourId, p.ink);
  setColour(TooltipWindow::outlineColourId, p.cardEdge);
  setColour(AlertWindow::backgroundColourId, p.card);
  setColour(AlertWindow::textColourId, p.ink);
  setColour(TextButton::textColourOffId, p.pillInk);
  setColour(TextButton::textColourOnId, p.pillOnInk);
  setColour(MidiKeyboardComponent::whiteNoteColourId, p.keyWhite);
  setColour(MidiKeyboardComponent::blackNoteColourId, p.keyBlack);
  setColour(MidiKeyboardComponent::keySeparatorLineColourId, p.cardEdge);
  setColour(MidiKeyboardComponent::keyDownOverlayColourId, p.accent.withAlpha(0.55f));
  setColour(MidiKeyboardComponent::mouseOverKeyOverlayColourId, p.accent.withAlpha(0.2f));
  setColour(MidiKeyboardComponent::shadowColourId, Colours::transparentBlack);
  setColour(MidiKeyboardComponent::upDownButtonBackgroundColourId, p.pill);
  setColour(MidiKeyboardComponent::upDownButtonArrowColourId, p.dim);
}

void LnF::drawRotarySlider(Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, Slider& s) {
  const auto& p = pal();
  const float size = (float) jmin(w, h) - 4.0f;
  auto r = Rectangle<float>((float) x, (float) y, (float) w, (float) h).withSizeKeepingCentre(size, size);
  const float cx = r.getCentreX(), cy = r.getCentreY(), rad = size * 0.5f;
  if (p.sketch) {   // a pencil circle, the value as a brush stroke of colour around it, an ink pointer
    const auto seed = wcol::seedOf(r, 3);
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float from = bipolar ? (a0 + a1) * 0.5f : a0, to = a0 + pos * (a1 - a0);
    Path track; track.addCentredArc(cx, cy, rad - 3.0f, rad - 3.0f, 0.0f, a0, a1, true);
    wcol::sketchPath(g, track, p.dim.withAlpha(0.6f), seed, 0.8f);
    if (std::abs(to - from) > 0.001f) {
      Path arc; arc.addCentredArc(cx, cy, rad - 3.0f, rad - 3.0f, 0.0f, jmin(from, to), jmax(from, to), true);
      g.setColour(p.accent.withAlpha(0.35f));
      g.strokePath(wcol::wobbly(arc, seed + 1, 0.6f), PathStrokeType(6.0f, PathStrokeType::curved, PathStrokeType::rounded));
      g.setColour(p.accent.withAlpha(0.55f));
      g.strokePath(arc, PathStrokeType(2.2f, PathStrokeType::curved, PathStrokeType::rounded));
    }
    auto face = r.reduced(8.0f);
    Path fp; fp.addEllipse(face);
    g.setColour(p.knobFace.withAlpha(0.9f)); g.fillPath(fp);
    wcol::sketchPath(g, fp, p.ink, seed + 2, 1.0f);
    const float pr = face.getWidth() * 0.5f;
    Path ptr; ptr.startNewSubPath(cx + std::sin(to) * pr * 0.2f, cy - std::cos(to) * pr * 0.2f);
    ptr.lineTo(cx + std::sin(to) * pr * 0.85f, cy - std::cos(to) * pr * 0.85f);
    g.setColour((s.isEnabled() ? p.ink : p.dim).withAlpha(0.9f));
    g.strokePath(ptr, PathStrokeType(2.0f, PathStrokeType::curved, PathStrokeType::rounded));
    return;
  }
  // track
  Path track;
  track.addCentredArc(cx, cy, rad - 2.5f, rad - 2.5f, 0.0f, a0, a1, true);
  g.setColour(p.knobTrack);
  g.strokePath(track, PathStrokeType(4.0f, PathStrokeType::curved, PathStrokeType::rounded));
  // value arc (bipolar controls start from the middle)
  const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
  const float from = bipolar ? (a0 + a1) * 0.5f : a0;
  const float to = a0 + pos * (a1 - a0);
  if (std::abs(to - from) > 0.001f) {
    Path arc;
    arc.addCentredArc(cx, cy, rad - 2.5f, rad - 2.5f, 0.0f, jmin(from, to), jmax(from, to), true);
    ColourGradient cg(p.accent, cx - rad, cy, p.accent2, cx + rad, cy, false);
    g.setGradientFill(cg);
    g.strokePath(arc, PathStrokeType(4.0f, PathStrokeType::curved, PathStrokeType::rounded));
  }
  // face
  auto face = r.reduced(7.0f);
  if (p.glass) {
    g.setColour(p.cardShade);
    g.fillEllipse(face.translated(0, 2));
  }
  ColourGradient fg(p.knobFace.brighter(0.05f), face.getX(), face.getY(), p.knobFace.darker(0.06f), face.getX(), face.getBottom(), false);
  g.setGradientFill(fg);
  g.fillEllipse(face);
  g.setColour(p.cardEdge);
  g.drawEllipse(face, 1.0f);
  // pointer
  const float pr = face.getWidth() * 0.5f;
  Point<float> a(cx + std::sin(to) * pr * 0.35f, cy - std::cos(to) * pr * 0.35f);
  Point<float> b(cx + std::sin(to) * pr * 0.78f, cy - std::cos(to) * pr * 0.78f);
  g.setColour(s.isEnabled() ? p.accent : p.dim);
  g.drawLine({ a, b }, 2.6f);
}

void LnF::drawButtonBackground(Graphics& g, Button& b, const Colour&, bool over, bool down) {
  const auto& p = pal();
  auto r = b.getLocalBounds().toFloat().reduced(1.5f);
  const float rad = r.getHeight() * 0.5f;
  const bool on = b.getToggleState();
  if (p.sketch) {   // a pencil-drawn pill; on = a wash of colour inside it (tint: 1 sage, 2 rose, plain: indigo)
    const int tint = (int) b.getProperties().getWithDefault("tint", 0);
    const auto seed = wcol::seedOf(r, 4);
    auto shape = wcol::wobblyRoundRect(r, rad, seed, 0.7f);
    g.setColour(p.pill.withAlpha(over ? 0.95f : 0.75f));
    g.fillPath(shape);
    const Colour c = tint == 1 ? wcol::wc::sage : tint == 2 ? wcol::wc::rose : wcol::wc::indigo;
    if (on) wcol::wash(g, shape, c, seed + 1, down ? 2.4f : 2.0f, 2.0f);
    else if (over) wcol::wash(g, shape, c, seed + 1, 0.6f, 1.5f);
    wcol::sketchPath(g, shape, p.ink.withAlpha(on ? 0.85f : 0.6f), seed + 2, on ? 1.2f : 0.9f);
    return;
  }
  if (p.neon) {
    const int tint = (int) b.getProperties().getWithDefault("tint", 0);
    const Colour edge = tint == 1 ? p.green : tint == 2 ? p.purple : Colour(0xff3a4440);
    if (on) {   // filled neon with a glow
      g.setColour(p.green.withAlpha(0.25f)); g.fillRoundedRectangle(r.expanded(2.0f), rad + 2.0f);
      g.setGradientFill(ColourGradient(p.green.brighter(0.25f), r.getX(), r.getY(), p.green.darker(0.25f), r.getX(), r.getBottom(), false));
      g.fillRoundedRectangle(r, rad);
      return;
    }
    if (tint != 0) { g.setColour(edge.withAlpha(0.18f)); g.fillRoundedRectangle(r.expanded(2.0f), rad + 2.0f); }
    Colour fill = p.pill;
    if (tint == 2) fill = Colour(0xff1d0b26);
    if (over) fill = fill.brighter(0.15f);
    if (down) fill = fill.darker(0.2f);
    g.setColour(fill); g.fillRoundedRectangle(r, rad);
    g.setColour(edge.withAlpha(tint != 0 ? 0.95f : 0.8f)); g.drawRoundedRectangle(r, rad, tint != 0 ? 1.6f : 1.0f);
    return;
  }
  if (p.glass && !on) {
    g.setColour(p.cardShade.withMultipliedAlpha(0.6f));
    g.fillRoundedRectangle(r.translated(0, 1.5f), rad);
  }
  Colour fill = on ? p.pillOn : p.pill;
  if (over) fill = fill.brighter(0.05f).overlaidWith(p.accent.withAlpha(on ? 0.0f : 0.06f));
  if (down) fill = fill.darker(0.08f);
  if (on && p.glass) {
    ColourGradient cg(p.pillOn.brighter(0.15f), r.getX(), r.getY(), p.pillOn.darker(0.05f), r.getX(), r.getBottom(), false);
    g.setGradientFill(cg);
  }
  else
    g.setColour(fill);
  g.fillRoundedRectangle(r, rad);
  g.setColour(on ? p.pillOn.darker(0.2f) : p.cardEdge);
  g.drawRoundedRectangle(r, rad, 1.0f);
}

Font LnF::getTextButtonFont(TextButton& b, int h) {
  if ((bool) b.getProperties().getWithDefault("big", false)) return font(jmin(17.0f, h * 0.5f));
  return font(jmin(12.5f, h * 0.48f));
}

void LnF::drawButtonText(Graphics& g, TextButton& b, bool, bool) {
  const auto& p = pal();
  g.setFont(getTextButtonFont(b, b.getHeight()));
  Colour ink = b.getToggleState() ? p.pillOnInk : p.pillInk;
  g.setColour(ink.withMultipliedAlpha(b.isEnabled() ? 1.0f : 0.5f));
  auto tr = b.getLocalBounds().reduced(6, 2);
  if ((bool) b.getProperties().getWithDefault("arrow", false)) {   // a drop-down arrow (the preset name)
    auto ar = tr.removeFromRight(18).toFloat();
    Path arrow; const float ax = ar.getCentreX(), ay = ar.getCentreY();
    arrow.addTriangle(ax - 5, ay - 2.5f, ax + 5, ay - 2.5f, ax, ay + 3.5f);
    g.fillPath(arrow);
    tr.removeFromLeft(18);
  }
  g.drawFittedText(b.getButtonText(), tr, Justification::centred, 1, 0.8f);
}

void LnF::drawComboBox(Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& box) {
  const auto& p = pal();
  auto r = Rectangle<float>(0, 0, (float) w, (float) h).reduced(1.5f);
  if (p.sketch) {
    auto shape = wcol::wobblyRoundRect(r, r.getHeight() * 0.5f, wcol::seedOf(r, 5), 0.7f);
    g.setColour(p.pill.withAlpha(0.8f)); g.fillPath(shape);
    wcol::sketchPath(g, shape, p.ink.withAlpha(0.6f), wcol::seedOf(r, 6), 0.9f);
  }
  else {
    g.setColour(p.pill);
    g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
    g.setColour(p.cardEdge);
    g.drawRoundedRectangle(r, r.getHeight() * 0.5f, 1.0f);
  }
  Path arrow;
  const float ax = (float) w - 16.0f, ay = h * 0.5f;
  arrow.addTriangle(ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
  g.setColour(p.dim.withMultipliedAlpha(box.isEnabled() ? 1.0f : 0.4f));
  g.fillPath(arrow);
}

Font LnF::getComboBoxFont(ComboBox& b) { return font(jmin(12.5f, b.getHeight() * 0.48f)); }

void LnF::positionComboBoxText(ComboBox& box, Label& label) {
  label.setBounds(10, 1, box.getWidth() - 28, box.getHeight() - 2);
  label.setFont(getComboBoxFont(box));
  label.setJustificationType(Justification::centred);
}

void LnF::drawPopupMenuBackground(Graphics& g, int w, int h) {
  const auto& p = pal();
  if (p.sketch) {
    wcol::paper(g, { 0, 0, w, h });
    g.setColour(wcol::wc::paperLite.withAlpha(0.5f)); g.fillAll();
    wcol::sketchRect(g, Rectangle<float>(2.0f, 2.0f, (float) w - 4.0f, (float) h - 4.0f), p.ink, (uint32_t) (w * 31 + h), 1.0f, 0.0f);
    return;
  }
  g.fillAll(p.card);
  g.setColour(p.cardEdge);
  g.drawRect(0, 0, w, h);
}

void LnF::drawLinearSlider(Graphics& g, int x, int y, int w, int h, float pos, float, float, Slider::SliderStyle style, Slider& s) {
  const auto& p = pal();
  if (style != Slider::LinearHorizontal && style != Slider::LinearBar) {
    LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, pos, 0, 0, style, s);
    return;
  }
  auto r = Rectangle<float>((float) x, y + h * 0.5f - 3.0f, (float) w, 6.0f);
  if (p.sketch) {   // a pencil line, the value brushed along it, a little ink circle to hold
    const auto seed = wcol::seedOf(r, 7);
    Path line; line.startNewSubPath(r.getX(), r.getCentreY()); line.lineTo(r.getRight(), r.getCentreY());
    wcol::sketchPath(g, line, p.dim, seed, 0.9f);
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float mid = r.getX() + r.getWidth() * (float) ((0.0 - s.getMinimum()) / (s.getMaximum() - s.getMinimum()));
    const float from = bipolar ? mid : r.getX();
    Path val; val.startNewSubPath(jmin(from, pos), r.getCentreY()); val.lineTo(jmax(from, pos), r.getCentreY());
    g.setColour(p.accent.withAlpha(0.4f));
    g.strokePath(val, PathStrokeType(6.0f, PathStrokeType::curved, PathStrokeType::rounded));
    Path knob; knob.addEllipse(pos - 6.5f, r.getCentreY() - 6.5f, 13.0f, 13.0f);
    g.setColour(p.knobFace); g.fillPath(knob);
    wcol::sketchPath(g, knob, p.ink, seed + 1, 1.0f);
    return;
  }
  g.setColour(p.knobTrack);
  g.fillRoundedRectangle(r, 3.0f);
  const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
  const float mid = r.getX() + r.getWidth() * (float) ((0.0 - s.getMinimum()) / (s.getMaximum() - s.getMinimum()));
  const float from = bipolar ? mid : r.getX();
  ColourGradient cg(p.accent, r.getX(), 0, p.accent2, r.getRight(), 0, false);
  g.setGradientFill(cg);
  g.fillRoundedRectangle(Rectangle<float>(jmin(from, pos), r.getY(), std::abs(pos - from), r.getHeight()), 3.0f);
  g.setColour(p.knobFace);
  g.fillEllipse(pos - 7.0f, r.getCentreY() - 7.0f, 14.0f, 14.0f);
  g.setColour(p.accent);
  g.drawEllipse(pos - 7.0f, r.getCentreY() - 7.0f, 14.0f, 14.0f, 1.5f);
}

Font LnF::getLabelFont(Label& l) { return font(l.getFont().getHeight(), l.getFont().isBold()); }

// ------------------------------------------------------------------ VKnob
String VKnob::displayText(const std::string& control, float value) {
  const vital::ValueDetails& d = vital::Parameters::getDetails(control);
  if (d.value_scale == vital::ValueDetails::kIndexed) {
    if (d.string_lookup != nullptr) {
      int i = (int) std::round(value - d.min);
      return String(d.string_lookup[jlimit(0, (int) std::round(d.max - d.min), i)]);
    }
    return String((int) std::round(value));
  }
  // pitch-type controls (cutoffs, centres) are MIDI notes inside Vital: shown in Hz, as Vital shows them by default
  if (d.display_units == " semitones" && (control.find("cutoff") != std::string::npos || control.find("center") != std::string::npos)) {
    const double hz = 440.0 * std::pow(2.0, (value - 69.0) / 12.0);
    return hz >= 1000.0 ? String(hz / 1000.0, hz >= 10000.0 ? 1 : 2) + " kHz" : String(roundToInt(hz)) + " Hz";
  }
  double v = value;
  switch (d.value_scale) {   // as Vital's SynthSlider::getAdjustedValue
    case vital::ValueDetails::kQuadratic: v *= v; break;
    case vital::ValueDetails::kCubic: v = v * v * v; break;
    case vital::ValueDetails::kQuartic: v *= v; v *= v; break;
    case vital::ValueDetails::kExponential: v = std::pow(2.0, v); break;
    case vital::ValueDetails::kSquareRoot: v = std::sqrt(jmax(v, 0.0)); break;
    default: break;
  }
  v += d.post_offset;
  if (d.display_invert && v != 0.0)
    v = 1.0 / v;
  v *= d.display_multiply;
  String units = d.display_units;
  String num;
  const double a = std::abs(v);
  if (units == " Hz" && a >= 1000.0) { num = String(v / 1000.0, a >= 10000.0 ? 1 : 2); units = " kHz"; }
  else if (units == " secs" && a < 1.0) { num = String(roundToInt(v * 1000.0)); units = " ms"; }
  else if (units == " secs") { num = String(v, a >= 10.0 ? 1 : 2); units = " s"; }
  else if (a >= 100.0) num = String(roundToInt(v));
  else if (a >= 10.0) num = String(v, 1);
  else num = String(v, 2);
  if (num.contains(".")) {
    while (num.endsWithChar('0')) num = num.dropLastCharacters(1);
    if (num.endsWithChar('.')) num = num.dropLastCharacters(1);
  }
  if (num == "-0") num = "0";
  return num + units;
}

VKnob::VKnob(GhostyProcessor& p, std::string control, String caption) : proc_(p), control_(std::move(control)), caption_(std::move(caption)) {
  slider.addListener(this);
  slider.onGestureBegin = [this] { proc_.beginChangeGesture(control_); };
  slider.onGestureEnd = [this] { proc_.endChangeGesture(control_); };
  addAndMakeVisible(slider);
  value_.setJustificationType(Justification::centred);
  value_.setFont(LnF::font(11.0f));
  value_.setInterceptsMouseClicks(false, false);
  addAndMakeVisible(value_);
  setControl(control_);
}

void VKnob::setControl(std::string control) {
  control_ = std::move(control);
  if (!proc_.hasControl(control_)) {
    setEnabled(false);
    return;
  }
  const vital::ValueDetails& d = vital::Parameters::getDetails(control_);
  setting_ = true;
  slider.setRange(d.min, d.max, d.value_scale == vital::ValueDetails::kIndexed ? 1.0 : 0.0);
  slider.setDoubleClickReturnValue(true, d.default_value);
  slider.updateSnap();
  slider.bipolar = d.min < 0.0f && d.max > 0.0f;
  slider.setValue(proc_.getControl(control_), dontSendNotification);
  setting_ = false;
  String tip = String(d.display_name);
  if (d.local_description.size())
    tip = String(d.display_name);
  slider.setTooltip(tip + " (double-click: back to default)");
  sync();
}

void VKnob::resized() {
  auto r = getLocalBounds();
  r.removeFromTop(14);
  value_.setBounds(r.removeFromBottom(18).reduced(4, 1));
  slider.setBounds(r.reduced(2));
}

void VKnob::paint(Graphics& g) {
  const auto& p = pal();
  g.setColour(p.dim);
  g.setFont(LnF::font(10.5f));
  g.drawText(caption_, getLocalBounds().removeFromTop(14), Justification::centred);
  auto vb = value_.getBounds().toFloat();
  if (p.sketch) {
    auto shape = wcol::wobblyRoundRect(vb, vb.getHeight() * 0.5f, wcol::seedOf(vb, 8), 0.5f);
    g.setColour(p.screen.withAlpha(0.7f)); g.fillPath(shape);
    wcol::sketchPath(g, shape, p.ink.withAlpha(0.45f), wcol::seedOf(vb, 9), 0.8f);
    return;
  }
  g.setColour(p.pill);
  g.fillRoundedRectangle(vb, vb.getHeight() * 0.5f);
  g.setColour(p.cardEdge);
  g.drawRoundedRectangle(vb.reduced(0.5f), vb.getHeight() * 0.5f, 1.0f);
}

void VKnob::sync() {
  if (!proc_.hasControl(control_))
    return;
  const float v = proc_.getControl(control_);
  slider.colours = orbitColours();
  if (!slider.isMouseButtonDown() && !slider.orbit.spinning() && std::abs(slider.getValue() - v) > 1.0e-6) {
    setting_ = true;
    slider.setValue(v, dontSendNotification);
    setting_ = false;
  }
  value_.setColour(Label::textColourId, pal().ink);
  String t = displayText(control_, (float) slider.getValue());
  if (value_.getText() != t)
    value_.setText(t, dontSendNotification);
}

void VKnob::sliderValueChanged(Slider*) {
  if (setting_)
    return;
  proc_.setControl(control_, (float) slider.getValue());
  value_.setText(displayText(control_, (float) slider.getValue()), dontSendNotification);
  if (onUserChange)
    onUserChange((float) slider.getValue());
}


// ------------------------------------------------------------------ VSwitch / VChoice
VSwitch::VSwitch(GhostyProcessor& p, std::string control, String text) : Pill(text), proc_(p), control_(std::move(control)) {
  onClick = [this] {
    const bool on = proc_.getControl(control_) < 0.5f;
    proc_.setControl(control_, on ? 1.0f : 0.0f);
    setToggleState(on, dontSendNotification);
    if (onUserChange)
      onUserChange(on);
  };
  sync();
}

void VSwitch::sync() {
  const bool on = proc_.getControl(control_) >= 0.5f;
  if (getToggleState() != on)
    setToggleState(on, dontSendNotification);
}

VChoice::VChoice(GhostyProcessor& p, std::string control) : proc_(p), control_(std::move(control)) {
  onChange = [this] {
    if (setting_ || getSelectedId() <= 0)
      return;
    const vital::ValueDetails& d = vital::Parameters::getDetails(control_);
    proc_.setControl(control_, d.min + (float) (getSelectedId() - 1));
  };
  fill();
}

void VChoice::setControl(std::string control) { control_ = std::move(control); fill(); }

void VChoice::fill() {
  setting_ = true;
  clear(dontSendNotification);
  if (proc_.hasControl(control_)) {
    const vital::ValueDetails& d = vital::Parameters::getDetails(control_);
    const int n = (int) std::round(d.max - d.min) + 1;
    for (int i = 0; i < n; ++i)
      addItem(VKnob::displayText(control_, d.min + (float) i), i + 1);
  }
  setting_ = false;
  sync();
}

void VChoice::sync() {
  if (!proc_.hasControl(control_))
    return;
  const vital::ValueDetails& d = vital::Parameters::getDetails(control_);
  const int id = (int) std::round(proc_.getControl(control_) - d.min) + 1;
  if (getSelectedId() != id && !isPopupActive()) {
    setting_ = true;
    setSelectedId(id, dontSendNotification);
    setting_ = false;
  }
}

} // namespace ghosty
