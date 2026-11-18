// GHOST ORBIT: Ghosty Pro's physical neon-ring knobs (build 4).
//
//   - grab lifts and brightens the ring
//   - a flick keeps the knob turning with momentum and it settles
//   - stepped controls snap to magnetic detents (the spring you see is drawing only)
//   - the ring shows the base value; modulation shows on its own ring
//
// Audio stays precise:
//   - momentum moves the value along one smooth path that only ever goes toward the landing value and ends
//     exactly on it (Flick::at never passes `landing`); any overshoot or bounce is drawing only (Spring)
//   - the whole thing (drag + momentum) is one gesture: the owner opens it on grab and Orbit calls endGesture once,
//     when the knob has settled (or is grabbed again / changed by something else)
//   - only a user's mouse release can start momentum; values set by the host, presets or automation never do
//
// The physics is our own code (exponential friction flywheel, critically-under-damped detent spring).
// The drawing (drawOrbitKnob) follows BoratoVHSStyle's VHSKnobNeonRing / VHSKnobDigital (MIT, (c) 2026 Filipe Borato,
// https://github.com/FilipeBorato/BoratoVHSStyle commit 721b9826): pulse halo, dark body with a rim, dim full-sweep
// ring, lit segments with a glow pass and a bright core, an accented leading segment, a pointer with a tip dot.
#pragma once
#include <JuceHeader.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include "Watercolor.h"

namespace ghostorbit {

// ---------------------------------------------------------------- physics (no JUCE, testable)
// The flywheel: after a flick with velocity v (proportion of travel per second) the knob moves
//   p(t) = from + (landing - from) * (1 - e^(-k t)),   landing = clamp(from + v / k)
// which is what a wheel with friction proportional to its speed does. p only ever moves toward `landing`.
// NaN / inf check that survives -ffast-math (Vital builds with it, where std::isfinite may be folded to true)
inline bool finiteBits(double x) {
  uint64_t b;
  std::memcpy(&b, &x, sizeof b);
  return ((b >> 52) & 0x7ff) != 0x7ff;
}

struct Flick {
  static constexpr double kFriction = 5.0;        // 1/s: a 1.0/s flick travels 0.2 of the range
  static constexpr double kMinVelocity = 0.25;    // slower releases are a plain let-go, no momentum
  static constexpr double kMaxVelocity = 4.0;     // so one flick travels at most 0.8 of the range
  static constexpr double kSettle = 2.0e-4;       // closer than this to landing: settled, value = landing exactly

  double from = 0.0, landing = 0.0, t = 0.0;
  bool active = false;

  // snap (optional) maps a proportion to the nearest detent: the landing value is always a detent
  bool start(double fromPos, double velocity, const std::function<double(double)>& snap = nullptr) {
    active = false;
    if (!finiteBits(velocity) || std::abs(velocity) < kMinVelocity)
      return false;
    velocity = std::max(-kMaxVelocity, std::min(kMaxVelocity, velocity));
    from = fromPos;
    landing = std::max(0.0, std::min(1.0, fromPos + velocity / kFriction));
    if (snap)
      landing = snap(landing);
    if (std::abs(landing - from) < 1.0e-9)
      return false;
    t = 0.0;
    active = true;
    return true;
  }
  double at(double secs) const {
    const double p = from + (landing - from) * (1.0 - std::exp(-kFriction * secs));
    return std::abs(landing - p) < kSettle ? landing : p;
  }
  // advance; returns the new position (exactly `landing` on the last step)
  double step(double dt) {
    t += dt;
    const double p = at(t);
    if (p == landing)
      active = false;
    return p;
  }
};

// The detent spring: what you SEE on a stepped knob. It chases the (already snapped) value, overshoots a little and settles.
struct Spring {
  static constexpr double kStiffness = 900.0;    // 1/s^2
  static constexpr double kDamping = 24.0;       // 1/s (zeta ~ 0.4: one small visible overshoot)
  double x = 0.0, v = 0.0;
  bool settled(double target) const { return std::abs(x - target) < 1.0e-4 && std::abs(v) < 1.0e-3; }
  void step(double target, double dt) {
    // a few sub-steps so a slow frame never blows up
    const int n = std::max(1, (int) std::ceil(dt / 0.004));
    const double h = dt / n;
    for (int i = 0; i < n; ++i) {
      const double a = kStiffness * (target - x) - kDamping * v;
      v += a * h;
      x += v * h;
    }
    if (settled(target)) { x = target; v = 0.0; }
  }
};

// Release velocity from the last ~80 ms of the drag. Holding still before letting go means no flick.
struct VelocityTracker {
  static constexpr int kSize = 16;
  double pos[kSize] = {}, secs[kSize] = {};
  int count = 0, head = 0;
  void reset() { count = 0; head = 0; }
  void add(double p, double s) {
    pos[head] = p; secs[head] = s;
    head = (head + 1) % kSize;
    count = std::min(count + 1, kSize);
  }
  double velocity(double now) const {
    if (count < 2)
      return 0.0;
    const int last = (head + kSize - 1) % kSize;
    if (now - secs[last] > 0.06)
      return 0.0;
    int first = last;
    for (int i = 1; i < count; ++i) {
      const int idx = (last + kSize - i) % kSize;
      if (secs[last] - secs[idx] > 0.08)
        break;
      first = idx;
    }
    const double dt = secs[last] - secs[first];
    return dt > 0.008 ? (pos[last] - pos[first]) / dt : 0.0;
  }
};

// ---------------------------------------------------------------- the driver a knob owns
class Orbit : private juce::Timer {
  public:
    std::function<double()> getProportion;                 // the control's value now, as 0..1 of travel
    std::function<void(double)> setProportion;             // move the control (a normal user edit, inside the open gesture)
    std::function<double(double)> snap;                    // stepped controls: nearest detent (else empty)
    std::function<void()> endGesture;                      // called exactly once per gesture that release() kept open
    std::function<void()> redraw;

    ~Orbit() override { stopTimer(); }

    std::function<double()> clock;                          // tests: a hand-driven clock (default: real time)
    double now() const { return clock ? clock() : juce::Time::getMillisecondCounterHiRes() * 0.001; }
    bool animating() const { return isTimerRunning(); }

    // mouse down. true = it caught a spinning knob: its gesture is still open, so the caller must NOT open another one
    bool grab() {
      const bool caught = flick_.active;
      flick_.active = false;
      held_ = true;
      tracker_.reset();
      tracker_.add(getProportion(), now());
      wake();
      return caught;
    }
    void drag() { tracker_.add(getProportion(), now()); wake(); }
    // mouse up. true = momentum started and the gesture stays open until it settles (endGesture is called then)
    bool release() {
      held_ = false;
      const double t = now();
      const double v = tracker_.velocity(t);
      wake();
      if (!flick_.start(getProportion(), v, snap))
        return false;
      lastSent_ = getProportion();
      return true;
    }
    // stop now (re-grab handled by grab(); this is for external changes, wheel, double-click, teardown)
    void cancel() {
      if (!flick_.active)
        return;
      flick_.active = false;
      if (endGesture)
        endGesture();
    }

    bool spinning() const { return flick_.active; }
    bool held() const { return held_; }
    float lift() const { return (float) lift_; }
    double landing() const { return flick_.landing; }
    // the proportion to DRAW: the value itself, or the detent spring's position on stepped controls
    double display(double actual) const { return snap && springLive_ ? spring_.x : actual; }

    // tests drive time by hand
    void tick(double dt) { advance(dt); }

  private:
    void wake() {
      lastTick_ = now();
      if (!isTimerRunning())
        startTimerHz(60);
    }
    void timerCallback() override {
      const double t = now();
      const double dt = std::min(0.05, std::max(0.0, t - lastTick_));
      lastTick_ = t;
      advance(dt);
    }
    void advance(double dt) {
      // lift: eases up while held or spinning, back down after
      const double liftTarget = (held_ || flick_.active) ? 1.0 : 0.0;
      lift_ += (liftTarget - lift_) * (1.0 - std::exp(-dt * (liftTarget > lift_ ? 22.0 : 9.0)));
      if (std::abs(lift_ - liftTarget) < 0.002)
        lift_ = liftTarget;

      if (flick_.active) {
        const double current = getProportion();
        if (std::abs(current - lastSent_) > 1.0e-6)
          cancel();                                        // something else moved it (host, preset): stop, no fight
        else {
          double p = flick_.step(dt);
          if (snap)
            p = snap(p);                                   // the value only ever sits on detents
          if (p != lastSent_) {
            setProportion(p);
            lastSent_ = getProportion();
          }
          if (!flick_.active && endGesture)
            endGesture();                                  // settled exactly on landing: one gesture ends here
        }
      }

      const double actual = getProportion();
      if (snap && (held_ || flick_.active || springLive_)) {
        if (!springLive_) { spring_.x = actual; spring_.v = 0.0; springLive_ = true; }
        spring_.step(actual, dt);
        if (!held_ && !flick_.active && spring_.settled(actual))
          springLive_ = false;
      }
      if (redraw)
        redraw();
      if (!held_ && !flick_.active && !springLive_ && lift_ == 0.0)
        stopTimer();
    }

    Flick flick_;
    Spring spring_;
    VelocityTracker tracker_;
    bool held_ = false, springLive_ = false;
    double lift_ = 0.0, lastSent_ = 0.0, lastTick_ = 0.0;
};

// ---------------------------------------------------------------- drawing (after BoratoVHSStyle, MIT)
struct OrbitColours {
  juce::Colour body { 0xff0b0d0f }, rim { 0xff3a2452 }, base { 0xffc04dff }, lead { 0xff6cff2a }, unlit { 0xff2a2433 },
               mod { 0xff6cff2a }, pointer { 0xff6cff2a };
  bool sketch = false;   // SKETCH look: drawn in pencil and watercolor (Watercolor.h) instead of the neon segments
};

// pos: 0..1 drawn position; lift: 0..1 grab; bipolarCentre: arcs start in the middle; modLo/modHi: a modulation range (or <0)
inline void drawOrbitKnob(juce::Graphics& g, juce::Rectangle<float> area, float pos, float a0, float a1, float lift,
                          bool bipolarCentre, const OrbitColours& c, bool enabled = true, float modLo = -1.0f, float modHi = -1.0f) {
  using namespace juce;
  const float size = jmin(area.getWidth(), area.getHeight());
  const float radius = size * 0.5f * (0.86f + 0.06f * lift);            // lift: the ring grows a little
  const auto centre = area.getCentre();
  const float ring = jmax(2.0f, radius * 0.16f) * (1.0f + 0.25f * lift);
  const float rr = radius - ring;                                      // ring centre radius
  const Colour base = enabled ? c.base.interpolatedWith(Colours::white, 0.22f * lift) : c.unlit.brighter(0.4f);
  const Colour lead = enabled ? c.lead.interpolatedWith(Colours::white, 0.18f * lift) : c.unlit.brighter(0.6f);
  pos = jlimit(0.0f, 1.0f, pos);

  if (c.sketch) {   // SKETCH: a pencil circle on paper, the value as watercolor dabs around it, a graphite pointer
    namespace wcol = ghosty::watercolor;
    const uint32_t seed = wcol::seedOf(area, 11);
    const float br = rr - ring * 0.9f;
    Path face; face.addEllipse(centre.x - br, centre.y - br, br * 2.0f, br * 2.0f);
    g.setColour(c.body.withAlpha(0.92f)); g.fillPath(face);
    if (lift > 0.01f) wcol::wash(g, face, c.base, seed + 5, 0.8f * lift, 1.5f);
    wcol::sketchPath(g, face, c.pointer, seed, 1.0f);
    const int count = jlimit(12, 32, (int) std::round((a1 - a0) * rr / jmax(4.0f, ring * 1.9f)));
    const float segAngle = (a1 - a0) / (float) count;
    const float startPos = bipolarCentre ? 0.5f : 0.0f;
    const float lo = jmin(startPos, pos), hi = jmax(startPos, pos);
    wcol::Rng rng(seed + 1);
    for (int i = 0; i < count; ++i) {
      const float mid = (i + 0.5f) / (float) count, ang = a0 + mid * (a1 - a0) + rng.bi() * segAngle * 0.08f;
      const bool lit = enabled && mid >= lo && mid <= hi && std::abs(hi - lo) > 1.0e-4f;
      const float r0 = rr - ring * (0.45f + 0.1f * rng.unit()), r1 = rr + ring * (0.35f + 0.15f * rng.unit());
      Path tick;
      tick.startNewSubPath(centre.x + std::sin(ang) * r0, centre.y - std::cos(ang) * r0);
      tick.lineTo(centre.x + std::sin(ang) * r1, centre.y - std::cos(ang) * r1);
      if (lit) {   // a dab of colour: wide and pale, then a stronger core
        g.setColour(c.base.withAlpha(0.3f)); g.strokePath(tick, PathStrokeType(ring * 1.5f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour(c.base.withAlpha(0.75f)); g.strokePath(tick, PathStrokeType(ring * 0.55f, PathStrokeType::curved, PathStrokeType::rounded));
      }
      else {
        g.setColour(c.rim.withAlpha(0.55f)); g.strokePath(tick, PathStrokeType(0.9f, PathStrokeType::curved, PathStrokeType::rounded));
      }
    }
    if (modLo >= 0.0f && modHi >= 0.0f && std::abs(modHi - modLo) > 1.0e-4f) {
      const float mr = rr - ring * 1.1f;
      Path m; m.addCentredArc(centre.x, centre.y, mr, mr, 0.0f, a0 + jmin(modLo, modHi) * (a1 - a0), a0 + jmax(modLo, modHi) * (a1 - a0), true);
      g.setColour(c.mod.withAlpha(0.6f)); g.strokePath(m, PathStrokeType(jmax(1.2f, ring * 0.4f), PathStrokeType::curved, PathStrokeType::rounded));
    }
    const float angle = a0 + pos * (a1 - a0);
    Path line;
    line.startNewSubPath(centre.x + std::sin(angle) * br * 0.25f, centre.y - std::cos(angle) * br * 0.25f);
    line.lineTo(centre.x + std::sin(angle) * br * 0.86f, centre.y - std::cos(angle) * br * 0.86f);
    g.setColour((enabled ? c.pointer : c.rim).withAlpha(0.9f));
    g.strokePath(line, PathStrokeType(2.0f + 0.4f * lift, PathStrokeType::curved, PathStrokeType::rounded));
    return;
  }

  // pulse halo (VHSKnobNeonRing::drawPulseHalo): only while lifted
  if (lift > 0.01f) {
    const float haloR = radius * (1.05f + 0.12f * lift);
    ColourGradient halo(base.withAlpha(0.20f * lift), centre.x, centre.y, Colours::transparentBlack, centre.x + haloR, centre.y, true);
    g.setGradientFill(halo);
    g.fillEllipse(centre.x - haloR, centre.y - haloR, haloR * 2.0f, haloR * 2.0f);
  }
  // dark body with a rim (drawBackground)
  const float br = rr - ring * 0.9f;
  g.setColour(Colours::black.withAlpha(0.6f));
  g.fillEllipse(centre.x - br - 1.5f, centre.y - br - 0.5f, br * 2.0f + 3.0f, br * 2.0f + 3.0f);
  ColourGradient body(c.body.brighter(0.12f), centre.x, centre.y - br * 0.35f, c.body.darker(0.5f), centre.x, centre.y + br, true);
  g.setGradientFill(body);
  g.fillEllipse(centre.x - br, centre.y - br, br * 2.0f, br * 2.0f);
  g.setColour(c.rim.withAlpha(0.9f));
  g.drawEllipse(centre.x - br, centre.y - br, br * 2.0f, br * 2.0f, 1.0f);
  g.setColour(Colours::white.withAlpha(0.05f));
  g.drawEllipse(centre.x - br + 2.0f, centre.y - br + 2.0f, br * 2.0f - 4.0f, br * 2.0f - 4.0f, 0.7f);

  // segments (VHSKnobDigital::drawSegments + KnobGeometry::buildSegmentPaths)
  const int count = jlimit(12, 40, (int) std::round((a1 - a0) * rr / jmax(4.0f, ring * 1.7f)));
  const float segAngle = (a1 - a0) / (float) count;
  const float gap = jmin(segAngle * 0.34f, 0.12f);
  const float startPos = bipolarCentre ? 0.5f : 0.0f;
  const float lo = jmin(startPos, pos), hi = jmax(startPos, pos);
  const int leadIndex = jlimit(0, count - 1, (int) std::floor(pos * count - (pos > startPos ? 1.0e-4f : -1.0e-4f)));
  const PathStrokeType stroke(ring, PathStrokeType::curved, PathStrokeType::butt);
  for (int i = 0; i < count; ++i) {
    const float s0 = a0 + i * segAngle + gap * 0.5f, s1 = s0 + segAngle - gap;
    Path p;
    p.addCentredArc(centre.x, centre.y, rr, rr, 0.0f, s0, s1, true);
    const float mid = (i + 0.5f) / (float) count;
    const bool lit = mid >= lo && mid <= hi && std::abs(hi - lo) > 1.0e-4f;
    const bool leading = enabled && i == leadIndex && std::abs(pos - startPos) > 0.5f / count;
    if (leading || lit) {
      const Colour col = leading ? lead : base;
      g.setColour(col.withAlpha(leading ? 0.24f : 0.14f + 0.08f * lift));
      g.strokePath(p, PathStrokeType(ring * 1.65f, PathStrokeType::curved, PathStrokeType::butt));
      g.setColour(col.withAlpha(leading ? 0.95f : 0.82f));
      g.strokePath(p, stroke);
    }
    else {
      g.setColour(c.unlit.withAlpha(0.55f));
      g.strokePath(p, stroke);
    }
  }
  // modulation ring: a thin green arc just inside the segments
  if (modLo >= 0.0f && modHi >= 0.0f && std::abs(modHi - modLo) > 1.0e-4f) {
    const float mr = rr - ring * 0.95f;
    Path m;
    m.addCentredArc(centre.x, centre.y, mr, mr, 0.0f, a0 + jmin(modLo, modHi) * (a1 - a0), a0 + jmax(modLo, modHi) * (a1 - a0), true);
    g.setColour(c.mod.withAlpha(0.25f));
    g.strokePath(m, PathStrokeType(ring * 0.9f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour(c.mod.withAlpha(0.9f));
    g.strokePath(m, PathStrokeType(jmax(1.2f, ring * 0.35f), PathStrokeType::curved, PathStrokeType::rounded));
  }
  // pointer (drawChromaticPointer): from 35% to 85% of the body, tip dot
  const float angle = a0 + pos * (a1 - a0);
  const Point<float> from(centre.x + std::sin(angle) * br * 0.30f, centre.y - std::cos(angle) * br * 0.30f);
  const Point<float> tip(centre.x + std::sin(angle) * br * 0.86f, centre.y - std::cos(angle) * br * 0.86f);
  Path line;
  line.startNewSubPath(from);
  line.lineTo(tip);
  g.setColour(Colours::black.withAlpha(0.5f));
  g.strokePath(line, PathStrokeType(4.0f, PathStrokeType::curved, PathStrokeType::rounded));
  g.setColour((enabled ? c.pointer : c.unlit.brighter(0.6f)).withAlpha(0.95f));
  g.strokePath(line, PathStrokeType(2.0f + 0.4f * lift, PathStrokeType::curved, PathStrokeType::rounded));
  g.setColour(Colours::white.withAlpha(0.6f));
  g.fillEllipse(tip.x - 1.6f, tip.y - 1.6f, 3.2f, 3.2f);
}

// ---------------------------------------------------------------- a JUCE rotary slider with the orbit
// Gestures go through onGestureBegin / onGestureEnd (one pair per grab, momentum included); values set from code
// (setValue with or without notification) never start the physics.
class OrbitKnob : public juce::Slider {
  public:
    OrbitKnob() {
      setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
      setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
      setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
      orbit.getProportion = [this] { return valueToProportionOfLength(getValue()); };
      orbit.setProportion = [this](double p) { setValue(proportionOfLengthToValue(p), juce::sendNotificationSync); };
      orbit.endGesture = [this] { if (onGestureEnd) onGestureEnd(); };
      orbit.redraw = [this] { repaint(); };
      updateSnap();
    }
    ~OrbitKnob() override { orbit.cancel(); }

    std::function<void()> onGestureBegin, onGestureEnd;
    OrbitColours colours;
    bool bipolar = false;
    Orbit orbit;

    void updateSnap() {
      if (getInterval() > 0.0 && getMaximum() > getMinimum()) {
        orbit.snap = [this](double p) {
          const double v = getMinimum() + std::round((proportionOfLengthToValue(p) - getMinimum()) / getInterval()) * getInterval();
          return valueToProportionOfLength(juce::jlimit(getMinimum(), getMaximum(), v));
        };
      }
      else
        orbit.snap = nullptr;
    }

    void mouseDown(const juce::MouseEvent& e) override {
      if (e.mods.isPopupMenu()) { juce::Slider::mouseDown(e); return; }
      const bool caught = orbit.grab();
      if (!caught && onGestureBegin)
        onGestureBegin();
      juce::Slider::mouseDown(e);
    }
    void mouseDrag(const juce::MouseEvent& e) override {
      if (e.mods.isPopupMenu()) return;
      setVelocityBasedMode(false);
      setMouseDragSensitivity(e.mods.isShiftDown() || e.mods.isCommandDown() ? 1600 : 250);   // Shift: fine
      juce::Slider::mouseDrag(e);
      orbit.drag();
    }
    void mouseUp(const juce::MouseEvent& e) override {
      if (e.mods.isPopupMenu()) { juce::Slider::mouseUp(e); return; }
      juce::Slider::mouseUp(e);
      if (!orbit.release() && onGestureEnd)
        onGestureEnd();
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override {
      orbit.cancel();
      juce::Slider::mouseDoubleClick(e);
    }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override {
      orbit.cancel();
      juce::Slider::mouseWheelMove(e, w);
    }
    void paint(juce::Graphics& g) override {
      const auto rp = getRotaryParameters();
      const double actual = valueToProportionOfLength(getValue());
      drawOrbitKnob(g, getLocalBounds().toFloat().reduced(1.0f), (float) orbit.display(actual), rp.startAngleRadians, rp.endAngleRadians,
                    orbit.lift(), bipolar, colours, isEnabled());
    }
};

}  // namespace ghostorbit
