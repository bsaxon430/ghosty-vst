#pragma once
// Where Vital draws a control, in the Ghosty Pro window's own units (tests and the hit-test tool).
// Vital draws through OpenGL (OpenGlComponent::setViewPort): a control's pixel box is its bounds in FullInterface units times
// gl_scale, where gl_scale is 1 when Vital's display scale is not 1 (its layout is then in pixels) and the rendering scale
// when it is 1; the window shows `rendering` pixels per point.
#include "JuceHeader.h"
#include "full_interface.h"
#include "GhostyEditor.h"

inline Point<float> ghostyDrawnCentre(GhostyEditor& ed, FullInterface& gui, Component& c) {
  const float ds = gui.getPixelScaling();
  const float rendering = Desktop::getInstance().getDisplays().getPrimaryDisplay()->scale;   // pixels per point
  const float glScale = ds == 1.0f ? rendering : 1.0f;
  // the control's centre in FullInterface units, as Vital's viewport maths sees it (it ignores transforms above FullInterface)
  Point<float> centre = c.getLocalBounds().toFloat().getCentre();
  for (Component* k = &c; k != nullptr && k != &gui; k = k->getParentComponent())
    centre += k->getPosition().toFloat();
  const Point<float> origin = ed.getLocalPoint(ed.vitalHolder(), Point<float>());
  return origin + centre * (glScale / rendering);
}

// A Retina Mac as JUCE sees it: window coordinates are points, every display reports scale 2 (backingScaleFactor), and the
// editor has NO transform (Desktop::setGlobalScaleFactor is a different thing: it scales the window itself).
inline void ghostyPretendDisplayScale(float scale) {
  if (scale <= 0.0f) return;   // 0: leave the displays as the system reports them (an X server started with -dpi 192 reports 2)
  auto& d = const_cast<Displays&>(Desktop::getInstance().getDisplays());
  for (auto& display : d.displays) display.scale = scale;
}

// keeps the pretended scale while a window is made / resized (the X server re-reads the displays on each configure event)
struct GhostyScaleKeeper : private Timer {
  explicit GhostyScaleKeeper(float s) : scale(s) { ghostyPretendDisplayScale(s); startTimer(1); }
  ~GhostyScaleKeeper() override { stopTimer(); }
  void timerCallback() override { ghostyPretendDisplayScale(scale); }
  float scale;
};
