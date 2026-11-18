#pragma once
// GHOST HUM - the LIVING GLASS SLIDER (build 7, 2026-11-13: the owner's "Living Glass Slider" brief, Material Study No. 065).
// Replaces the little STRENGTH / SMOOTH / SWING pills in the bottom bar. Same parameters, same ranges, same wiring: only the look
// and the feel changed.
//
//   the track   a long pill of dark glass with a tiny pixel-art world inside, drawn at a low internal resolution (one art pixel =
//               1.5 points, so 3 x 3 real pixels on a Retina screen) and enlarged with hard nearest-neighbour edges: far circuit
//               towers with little windows, a waveform, drifting fireflies, circuit traces with pads in front, sparks, and a tiny
//               ghost. Everything is drawn back to front into one buffer, colour AND light together, so a thing in front always
//               hides the light of what is behind it; the bloom is made from that buffer afterwards.
//   the light   the world wakes up behind the knob: left of it is lit, right of it is dark, the edge is a 4 x 4 Bayer dither with
//               a fine bright rim. The higher the value, the more windows / pads switch on (each one flickers when it first
//               lights), the faster things move, the brighter the waves, the stronger the glass reflections.
//   the knob    a capsule of thick pale-aqua glass (a ramune marble), rendered per pixel at the screen's real resolution by its own
//               little "fragment shader" on the CPU: it magnifies and refracts the world under it, splits R / G / B near the curved
//               rim, has a darker band around its waist (which marks the exact value), Fresnel light on the edges, a long soft
//               curved highlight, one sharp specular dot, bubbles, a faint liquid line inside, a glow under it and a soft shadow.
//   the feel    springs: the knob trails the exact value a little while you drag (elastic), stretches sideways when fast, wobbles
//               like jelly when it stops, overshoots a hair and settles; bubbles drift against the motion, light streaks trail a
//               fast move, a ring of light pulses when you let go; past the ends it gives a little like rubber.
//   the value   is ALWAYS the exact parameter value: the animation only moves the picture, never the number. Mouse / touch drag
//               (Shift = fine), click on the track = jump there, double-click = the default, wheel, arrows, Home / End, Page Up /
//               Down. Begin / end of a drag are reported (onBegin / onEnd) so a DAW records automation as one gesture.
//   styles      Electric (STRENGTH): sharp pulses running along the traces, a spiky waveform, green; Liquid (SMOOTH): soft waves,
//               a little sloshing pool, round bubbles rising, calm aqua; Groove (SWING): equaliser bars that bounce in a swung
//               rhythm - the off-beat bars land later the more swing you dial in.
//   the ghost   a 5 x 6 pixel friend of the logo: asleep at low values (with a tiny z), wakes up when the knob comes near, floats
//               after the knob while you drag, glows when the value is high, and does a little hop with sparkles at 100 %.
//   cost        the world is ~130 x 14 art pixels, the frame ~25 000 real pixels: a small fraction of a millisecond per frame.
//               Its own timer: 60 fps only while something moves (a drag, a spring settling, a pulse), 30 fps of idle life while
//               GHOST ANIMATION is on, nothing at all when it is off (then it is a still, crisp picture that snaps to the value -
//               the reduced-motion setting) or when the window is hidden. Message thread only; never touches the audio thread.
//   no sound    the slider makes no sound.
#include "HumLook.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace hum::ui {

struct GlassSlider : juce::Component, juce::SettableTooltipClient, private juce::Timer {
    enum Style { Electric, Liquid, Groove };

    // ---- the Dial's interface (the editor wires it the same way)
    juce::String name, unit; double lo = 0, hi = 100, val = 0, def = 0, step = 1; int decimals = 0;
    std::function<void(double)> onChange; std::function<void()> onBegin, onEnd; std::function<juce::String(double)> format;
    Style style = Electric;

    GlassSlider() { setWantsKeyboardFocus(true); setMouseClickGrabsKeyboardFocus(true); setMouseCursor(juce::MouseCursor::PointingHandCursor); setRepaintsOnMouseActivity(false); }
    ~GlassSlider() override { stopTimer(); }

    void set(double v, bool notify) {
        v = juce::jlimit(lo, hi, std::round(v / step) * step);
        if (v != val) { val = v; if (!lively) pos = frac(); kick(); repaint(); if (notify && onChange) onChange(val); }
    }
    juce::String text() const { return format ? format(val) : juce::String(val, decimals) + unit; }
    double frac() const { return juce::jlimit(0.0, 1.0, (val - lo) / juce::jmax(1e-9, hi - lo)); }
    // GHOST ANIMATION: ON = alive (idle life, springs, wobble); OFF = reduced motion: a still picture that snaps to the value
    void setLively(bool on) { if (lively == on) return; lively = on; if (!on) { pos = frac(); vel = 0; stretch = stretchV = 0; pulse = 0; celebrate = 0; } kick(); repaint(); }
    bool isLively() const { return lively; }
    float shownFraction() const { return (float) pos; }   // (tests: where the picture of the knob is)
    void advanceForTests(float dt) { simulate(dt); }       // (tests: step the springs / the world by hand)
    bool keepPictureForTests = false;                      // (tests: paint the springs' state even without the timer)

    // ------------------------------------------------------------ input
    void mouseDown(const juce::MouseEvent& e) override {
        if (!isEnabled()) return;
        if (e.getNumberOfClicks() >= 2) { begin(); set(def, true); end(); pulse = lively ? 1.0f : 0.0f; dragging = false; return; }
        const auto G = geo(); const float kx = knobX(G);
        grab = std::abs(e.position.x - kx) <= G.half + 3.0f ? e.position.x - kx : 0.0f;   // on the knob: keep where you held it; on the track: jump there
        dragging = true; fine = false; begin(); dragTo(e); kick();
    }
    void mouseDrag(const juce::MouseEvent& e) override { if (dragging) dragTo(e); }
    void mouseUp(const juce::MouseEvent&) override { if (!dragging) return; dragging = false; over = 0.0f; pulse = lively ? 1.0f : 0.0f; end(); kick(); }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override {
        if (!isEnabled() || w.deltaY == 0) return; begin(); set(val + (w.deltaY > 0 ? step : -step) * (std::abs(w.deltaY) > 0.3f ? 4 : 1), true); end(); }
    bool keyPressed(const juce::KeyPress& k) override {
        if (!isEnabled()) return false;
        const int c = k.getKeyCode(); const bool big = k.getModifiers().isShiftDown(); double to = val;
        if (c == juce::KeyPress::leftKey || c == juce::KeyPress::downKey) to = val - step * (big ? 5 : 1);
        else if (c == juce::KeyPress::rightKey || c == juce::KeyPress::upKey) to = val + step * (big ? 5 : 1);
        else if (c == juce::KeyPress::pageDownKey) to = val - step * 10; else if (c == juce::KeyPress::pageUpKey) to = val + step * 10;
        else if (c == juce::KeyPress::homeKey) to = lo; else if (c == juce::KeyPress::endKey) to = hi;
        else return false;   // (Space, R, Q ... go on to the window)
        begin(); set(to, true); end(); pulse = lively ? 0.6f : 0.0f; kick(); return true;
    }
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }
    void visibilityChanged() override { kick(); }
    void parentHierarchyChanged() override { kick(); }
    void enablementChanged() override { if (!isEnabled() && dragging) { dragging = false; end(); } kick(); repaint(); }
    void resized() override { worldDirty = true; }

    // ------------------------------------------------------------ paint
    void paint(juce::Graphics& g) override {
        if (!started) { started = true; pos = frac(); simulate(0.0f); }
        if (!isTimerRunning() && !dragging && !keepPictureForTests) { pos = frac(); vel = 0; stretch = stretchV = 0; }   // nothing is animating it: show the truth
        const auto G = geo(); if (G.track.getWidth() < 24.0f || G.track.getHeight() < 8.0f) return;
        const float s = juce::jlimit(1.0f, 3.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        rasterWorld(G); render(G, s);
        g.setOpacity(1.0f); g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
        g.drawImage(frame, frameArea, juce::RectanglePlacement::stretchToFit, false);
        // the label and the value, on a line above the track
        const bool en = isEnabled(); auto top = getLocalBounds().withHeight((int) G.track.getY() - 1).reduced(6, 0);
        g.setFont(look::small(9.0f)); g.setColour(en ? look::dim : look::faint); g.drawText(name, top, juce::Justification::centredLeft);
        g.setFont(look::font(11.5f, true)); glowText(g, text(), top, juce::Justification::centredRight, en ? look::ink : look::faint, accentColour(), en ? 0.6f : 0.0f);
        if (hasKeyboardFocus(false) && en) { g.setColour(look::greenGlow.withAlpha(0.45f)); g.drawRoundedRectangle(G.track.expanded(1.5f), G.track.getHeight() * 0.5f + 1.5f, 1.0f); }
    }

private:
    // ------------------------------------------------------------ geometry
    struct Geo { juce::Rectangle<float> track; float kh = 0, half = 0, r = 0, minX = 0, maxX = 0; };
    static constexpr float kArt = 1.5f;   // one art pixel, in points
    Geo geo() const {
        Geo G; const auto b = getLocalBounds().toFloat(); const float labelH = juce::jlimit(0.0f, 14.0f, b.getHeight() - 18.0f);
        G.track = juce::Rectangle<float>(b.getX() + 1.0f, b.getY() + labelH + 1.0f, b.getWidth() - 2.0f, b.getHeight() - labelH - 2.0f);
        G.kh = G.track.getHeight() + 2.0f; G.r = G.kh * 0.5f; G.half = juce::jmin(G.kh * 0.86f, G.track.getWidth() * 0.22f);
        G.minX = G.track.getX() + G.half + 1.0f; G.maxX = juce::jmax(G.minX + 1.0f, G.track.getRight() - G.half - 1.0f); return G; }
    float knobX(const Geo& G) const { return G.minX + (float) pos * (G.maxX - G.minX) + rubber(); }
    float rubber() const { return over == 0.0f ? 0.0f : (over > 0 ? 1.0f : -1.0f) * 7.0f * (1.0f - std::exp(-std::abs(over) / 24.0f)); }
    juce::Colour accentColour() const { return style == Liquid ? look::teal : look::green; }

    void dragTo(const juce::MouseEvent& e) {
        const auto G = geo(); const float travel = G.maxX - G.minX; double f;
        if (e.mods.isShiftDown()) { if (!fine) { fine = true; fineX = e.position.x; fineF = frac(); } f = fineF + (e.position.x - fineX) / travel * 0.2; }
        else { fine = false; f = (e.position.x - grab - G.minX) / travel; }
        over = f < 0 ? (float) (f * travel) : f > 1 ? (float) ((f - 1) * travel) : 0.0f; if (!lively) over = 0.0f;
        set(lo + juce::jlimit(0.0, 1.0, f) * (hi - lo), true); kick();
    }
    void begin() { if (!gesture) { gesture = true; if (onBegin) onBegin(); } }
    void end()   { if (gesture) { gesture = false; if (onEnd) onEnd(); } }

    // ------------------------------------------------------------ timing: 60 fps while moving, 30 fps idle life, off when still
    bool settled() const { return std::abs(frac() - pos) < 1e-4 && std::abs(vel) < 1e-3 && std::abs(stretch) < 2e-3 && std::abs(stretchV) < 2e-3 && pulse <= 0 && celebrate <= 0 && over == 0; }
    void kick() {
        int hz = 0; if (isShowing() && getWidth() > 0) { if (dragging || (lively && !settled())) hz = 60; else if (lively && isEnabled()) hz = 30; }
        if (hz == 0) { stopTimer(); curHz = 0; return; }
        if (hz != curHz || !isTimerRunning()) { curHz = hz; lastT = juce::Time::getMillisecondCounterHiRes() * 0.001; startTimerHz(hz); }
    }
    void timerCallback() override {
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001; const float dt = (float) juce::jlimit(0.0, 0.05, now - lastT); lastT = now;
        simulate(dt); repaint(); kick(); }

    // ------------------------------------------------------------ the simulation
    struct Mote { float x, y, vx, vy, ph; };
    struct Spark { std::vector<juce::Point<int>> px; float life; };
    struct Bubble { float u, v, rad, ph; };
    struct Fleck { float x, y, vx, vy, life; };
    double pos = 0, vel = 0; float stretch = 0, stretchV = 0, pulse = 0, over = 0, grab = 0, t = 0, slosh = 0, velPx = 0, lastDir = 1;
    bool dragging = false, lively = true, gesture = false, fine = false, started = false, worldDirty = true, wasFull = false; float fineX = 0; double fineF = 0;
    int curHz = 0; double lastT = 0; juce::Random rng { 1234 };
    std::vector<Mote> motes; std::vector<Spark> sparks; std::vector<Fleck> flecks, zees; std::array<Bubble, 6> bubbles {}; std::vector<float> litSince, pulses;
    float ghostX = -1, ghostVis = 0.9f, awake = 0, sleepy = 0, celebrate = 0, zTimer = 0, blink = 3.0f; std::vector<int> litPads;

    void simulate(float dt) {
        const auto G = geo(); const float travel = juce::jmax(1.0f, G.maxX - G.minX); const double target = frac();
        t += dt;
        if (!lively) { pos = target; vel = 0; stretch = stretchV = 0; }
        else if (dt > 0) {
            const int n = juce::jmax(1, (int) std::ceil(dt * 240.0f)); const double h = dt / n;
            const double k = dragging ? 1300.0 : 430.0, c = 2.0 * (dragging ? 0.82 : 0.62) * std::sqrt(k);   // elastic while held; a hair of overshoot after
            const float ks = 320.0f, cs = 2.0f * 0.17f * std::sqrt(ks);                                     // jelly: a loose, ringing stretch spring
            for (int i = 0; i < n; ++i) {
                vel += (k * (target - pos) - c * vel) * h; pos += vel * h;
                const float sp = (float) std::abs(vel) * travel; const float sT = juce::jlimit(0.0f, 0.30f, sp * 0.00075f);
                stretchV += (ks * (sT - stretch) - cs * stretchV) * (float) h; stretch += stretchV * (float) h; }
            stretch = juce::jlimit(-0.18f, 0.38f, stretch);
        }
        velPx = (float) vel * travel; if (std::abs(velPx) > 20.0f) lastDir = velPx > 0 ? 1.0f : -1.0f;
        pulse = juce::jmax(0.0f, pulse - dt * 2.2f); slosh += (-velPx * 0.0005f - slosh) * juce::jmin(1.0f, dt * 5.0f) + std::sin(t * 7.0f) * 0.0f;
        if (over != 0 && !dragging) over = 0;
        // bubbles: drift against the motion, rise slowly, wrap inside the glass
        if (bubbles[0].rad == 0) for (auto& b : bubbles) b = { rng.nextFloat() * 1.6f - 0.8f, rng.nextFloat() * 1.2f - 0.6f, 0.35f + rng.nextFloat() * 0.6f, rng.nextFloat() * 6.28f };
        for (auto& b : bubbles) { b.u -= velPx * dt * 0.0045f; b.v -= dt * 0.05f * (lively ? 1.0f : 0.0f); b.ph += dt;
            if (b.u < -0.85f) b.u += 1.7f; if (b.u > 0.85f) b.u -= 1.7f; if (b.v < -0.62f) { b.v = 0.6f; b.u = rng.nextFloat() * 1.6f - 0.8f; } }
        // 100 %: a tiny celebration (once each time it gets there)
        const bool full = target >= 0.999; if (full && !wasFull && lively && started && dt > 0) { celebrate = 1.4f; for (int i = 0; i < 10; ++i) flecks.push_back({ ghostX + 2.5f, 3.0f, (rng.nextFloat() - 0.5f) * 14.0f, -4.0f - rng.nextFloat() * 8.0f, 0.6f + rng.nextFloat() * 0.5f }); }
        wasFull = full; celebrate = juce::jmax(0.0f, celebrate - dt);
        stepWorld(G, dt);
    }

    // the world grid
    int AW = 0, AH = 0; std::vector<float> col, emi, blm;   // rgb per art pixel; light per art pixel; bloom
    float energy() const { return (float) juce::jlimit(0.0, 1.0, pos); }
    static float hash(int n) { unsigned int x = (unsigned int) n * 747796405u + 2891336453u; x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u; x = (x >> 22u) ^ x; return (float) (x & 0xffffff) / 16777216.0f; }
    static float bayer(int x, int y) { static const int m[4][4] { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } }; return ((float) m[y & 3][x & 3] + 0.5f) / 16.0f; }
    float litEdge(const Geo& G) const { return (knobX(G) + G.half * juce::jmax(0.55f, 1.0f + stretch) - G.track.getX()) / kArt + 4.0f; }   // in art pixels
    bool lit(float edge, int x, int y) const { const float u = (edge - (float) x) / 6.0f; return u >= 1.0f || (u > 0.0f && u > bayer(x, y)); }

    void sizeWorld(const Geo& G) {
        const int w = juce::jmax(8, (int) std::ceil(G.track.getWidth() / kArt)), h = juce::jmax(6, (int) std::ceil(G.track.getHeight() / kArt));
        if (w == AW && h == AH && !worldDirty) return; AW = w; AH = h; worldDirty = false;
        col.assign((size_t) (AW * AH * 3), 0.0f); emi.assign(col.size(), 0.0f); blm.assign(col.size(), 0.0f); litSince.assign((size_t) AW, -100.0f);
        motes.clear(); const int n = juce::jmax(6, AW / 6); for (int i = 0; i < n; ++i) motes.push_back({ rng.nextFloat() * (float) AW, 1.0f + rng.nextFloat() * (float) (AH - 3), 0, 0, rng.nextFloat() * 6.28f });
        if (ghostX < 0 || ghostX > (float) AW) ghostX = (float) AW * 0.62f;
    }
    void stepWorld(const Geo& G, float dt) {
        sizeWorld(G); const float e = energy(), edge = litEdge(G); const bool moving = lively && dt > 0;
        for (int x = 0; x < AW; ++x) { const bool on = (float) x < edge - 1.0f; if (on && litSince[(size_t) x] < -50.0f) litSince[(size_t) x] = t; if (!on) litSince[(size_t) x] = -100.0f; }
        if (!moving) return;
        // motes
        for (auto& m : motes) { const bool on = m.x < edge; const float go = on ? 0.25f + 1.6f * e : 0.08f; m.ph += dt * (1.0f + 3.0f * e);
            if (style == Liquid) { m.y -= dt * go * 1.6f; m.x += std::sin(m.ph) * dt * 0.8f; if (m.y < 0.5f) { m.y = (float) AH - 1.5f; m.x = rng.nextFloat() * (float) AW; } }
            else if (style == Electric) { m.x += dt * go * 4.0f; m.y += std::sin(m.ph * 1.7f) * dt * 1.2f; if (m.x > (float) AW) { m.x = 0; m.y = 1.0f + rng.nextFloat() * (float) (AH - 3); } }
            else { m.x += dt * go * 1.5f; if (m.x > (float) AW) m.x = 0; }
            m.y = juce::jlimit(0.5f, (float) AH - 1.5f, m.y); }
        // trace pulses (Electric): bright dots running right along the bottom trace inside the lit part
        if (style == Electric) { for (auto& p : pulses) p += dt * (10.0f + 30.0f * e); pulses.erase(std::remove_if(pulses.begin(), pulses.end(), [&](float p) { return p > edge || p > (float) AW; }), pulses.end());
            if (e > 0.05f && rng.nextFloat() < dt * (0.6f + 5.0f * e)) pulses.push_back(-2.0f); }
        // sparks between lit pads
        for (auto& s : sparks) s.life -= dt; sparks.erase(std::remove_if(sparks.begin(), sparks.end(), [](const Spark& s) { return s.life <= 0; }), sparks.end());
        const float rate = (style == Electric ? 2.6f : style == Groove ? 1.0f : 0.5f) * juce::jmax(0.0f, e - 0.2f) * 1.4f;
        if (litPads.size() >= 2 && rng.nextFloat() < dt * rate) { const int a = litPads[(size_t) rng.nextInt((int) litPads.size())]; int b = -1;
            for (int p : litPads) if (p != a && std::abs(p - a) >= 5 && std::abs(p - a) <= 16 && (b < 0 || rng.nextBool())) b = p;
            if (b >= 0) { Spark s; s.life = 0.13f; const int x0 = juce::jmin(a, b), x1 = juce::jmax(a, b), yb = AH - 3; int y = yb;
                for (int x = x0; x <= x1; ++x) { const int want = yb - 1 - (int) (std::sin((float) (x - x0) / (float) juce::jmax(1, x1 - x0) * 3.14159f) * (2.0f + rng.nextFloat() * 1.5f)); y += juce::jlimit(-1, 1, want - y + (rng.nextInt(3) - 1)); s.px.push_back({ x, y }); }
                sparks.push_back(std::move(s)); } }
        // the ghost: asleep low, wakes when the knob comes near, follows the knob while you drag, excited high
        const float kArtX = (knobX(G) - G.track.getX()) / kArt, kHalfArt = G.half / kArt;
        const bool near = std::abs(ghostX + 2.5f - kArtX) < kHalfArt + 16.0f;
        const float wantAwake = (dragging || near || e > 0.38f) ? 1.0f : 0.0f;
        if (wantAwake > 0) { awake = juce::jmin(1.0f, awake + dt * 3.0f); sleepy = 0; } else { sleepy += dt; if (sleepy > 2.0f) awake = juce::jmax(0.0f, awake - dt * 0.8f); }
        float targetX = ghostX;
        if (dragging) { targetX = kArtX - lastDir * (kHalfArt + 6.0f) - 2.5f; if (targetX < 1.0f || targetX > (float) AW - 6.0f) targetX = kArtX + lastDir * (kHalfArt + 3.0f) - 2.5f; }
        else if (std::abs(ghostX + 2.5f - kArtX) < kHalfArt + 3.0f) targetX = kArtX - kHalfArt - 8.0f < 1.0f ? kArtX + kHalfArt + 3.0f : kArtX - kHalfArt - 8.0f;   // never hide under the knob
        else if (awake > 0.5f) targetX = ghostX + std::sin(t * 0.7f) * 0.6f;
        ghostX += (juce::jlimit(1.0f, (float) AW - 6.0f, targetX) - ghostX) * juce::jmin(1.0f, dt * (dragging ? 5.0f : 2.0f));
        ghostVis += ((awake > 0.3f ? 1.0f : juce::jmin(1.0f, 0.72f + 0.38f * std::sin(t * 0.45f))) - ghostVis) * juce::jmin(1.0f, dt * 2.0f);   // asleep it fades in and out between the towers
        blink -= dt; if (blink < -0.12f) blink = 2.0f + rng.nextFloat() * 3.5f;
        zTimer -= dt; if (awake < 0.2f && zTimer <= 0) { zTimer = 1.6f; zees.push_back({ ghostX + 5.0f, (float) AH * 0.5f - 1.0f, 2.0f, -2.2f, 1.3f }); }
        for (auto& z : zees) { z.x += z.vx * dt; z.y += z.vy * dt; z.life -= dt; } zees.erase(std::remove_if(zees.begin(), zees.end(), [](const Fleck& f) { return f.life <= 0 || f.y < 0; }), zees.end());
        for (auto& f : flecks) { f.x += f.vx * dt; f.y += f.vy * dt; f.vy += 22.0f * dt; f.life -= dt; } flecks.erase(std::remove_if(flecks.begin(), flecks.end(), [](const Fleck& f) { return f.life <= 0; }), flecks.end());
    }

    // ------------------------------------------------------------ the world, drawn back to front (colour + light together)
    struct RGB { float r, g, b; };
    static RGB rgb(juce::uint32 c, float k = 1.0f) { return { (float) ((c >> 16) & 255) / 255.0f * k, (float) ((c >> 8) & 255) / 255.0f * k, (float) (c & 255) / 255.0f * k }; }
    static RGB mix(RGB a, RGB b, float k) { return { a.r + (b.r - a.r) * k, a.g + (b.g - a.g) * k, a.b + (b.b - a.b) * k }; }
    void put(int x, int y, RGB c, float light) { if (x < 0 || y < 0 || x >= AW || y >= AH) return; const size_t i = (size_t) ((y * AW + x) * 3); col[i] = c.r; col[i + 1] = c.g; col[i + 2] = c.b; emi[i] = c.r * light; emi[i + 1] = c.g * light; emi[i + 2] = c.b * light; }
    void blend(int x, int y, RGB c, float a, float light) { if (x < 0 || y < 0 || x >= AW || y >= AH) return; const size_t i = (size_t) ((y * AW + x) * 3);
        col[i] += (c.r - col[i]) * a; col[i + 1] += (c.g - col[i + 1]) * a; col[i + 2] += (c.b - col[i + 2]) * a; emi[i] += (c.r * light - emi[i]) * a; emi[i + 1] += (c.g * light - emi[i + 1]) * a; emi[i + 2] += (c.b * light - emi[i + 2]) * a; }
    bool flick(int x) const { const float since = t - litSince[(size_t) juce::jlimit(0, AW - 1, x)]; return since < 0.35f && hash((int) (t * 30.0f) * 131 + x) < 0.55f; }   // first light: a little flicker

    void rasterWorld(const Geo& G) {
        sizeWorld(G);
        const float e = energy(), edge = litEdge(G), kArtX = (knobX(G) - G.track.getX()) / kArt;
        const RGB acc = style == Liquid ? rgb(0x2ee6d6) : rgb(0x3dff8c), accHi = style == Liquid ? rgb(0x8df7ff) : rgb(0x8cffc0), aqua = rgb(0x2ee6d6), cyan = rgb(0x8df7ff), mint = rgb(0x8cffc0), white = rgb(0xffffff);
        const int far = (int) std::floor(-(float) pos * 6.0f), nearS = (int) std::floor(-(float) pos * 16.0f);   // parallax: the near layer slides more than the far one
        // 0. the dark (a breath of green where it is lit)
        for (int y = 0; y < AH; ++y) { const float v = 1.0f - std::abs((float) y / (float) juce::jmax(1, AH - 1) - 0.55f) * 1.4f;
            for (int x = 0; x < AW; ++x) { RGB c = mix(rgb(0x020405), rgb(0x061210), juce::jmax(0.0f, v)); if (lit(edge, x, y)) c = mix(c, style == Liquid ? rgb(0x06302c) : rgb(0x082c1c), 0.35f + 0.65f * e); put(x, y, c, 0.0f); } }
        // 1. far towers (circuit blocks) with little windows
        for (int x = 0; x < AW; ++x) { const int wx = x - far + 4000, blk = wx / 5; if (hash(blk * 7 + 3) < 0.22f) continue;
            const int hgt = 3 + (int) (hash(blk) * (float) juce::jmax(1, AH - 6)), top = AH - 1 - hgt;
            for (int y = top; y < AH; ++y) { const bool on = lit(edge, x, y); RGB c = on ? mix(rgb(0x0b1d16), rgb(0x11402c), e) : rgb(0x08120e); if (y == top) c = on ? mix(rgb(0x14402c), rgb(0x1f6b48), e) : rgb(0x0c1a15);
                float light = 0.0f; const int wy = AH - 1 - y;
                if (y > top && (wx % 5 == 1 || wx % 5 == 3) && wy % 2 == 1 && hash(wx * 31 + y) < 0.55f) {   // a window
                    if (on && hash(wx * 13 + y * 7) < 0.22f + 0.72f * e && !flick(x)) { c = mix(acc, accHi, hash(wx + y) * 0.5f); light = 0.5f + 0.4f * e;
                        if (lively && hash((int) (t * 3.0f) * 17 + wx * 5 + y) < 0.02f) { c = mix(c, rgb(0x0b1d16), 0.7f); light = 0.1f; } }   // the odd window blinks
                    else c = rgb(0x0e1f19); }
                put(x, y, c, light); } }
        // 2. the waveform (and Groove's bars)
        auto waveY = [&](float x, int which) -> float {
            const float mid = (float) AH * 0.42f;
            if (style == Electric) { float spikes = 0; for (int k = 0; k < 3; ++k) { const float p = std::fmod(t * (16.0f + 22.0f * e) + (float) k * (float) AW / 3.0f, (float) AW + 20.0f) - 10.0f; spikes += std::exp(-(x - p) * (x - p) * 0.8f) * (k % 2 ? -1.0f : 1.0f); }
                return mid + (0.8f + 2.2f * e) * (0.45f * std::sin(x * 0.42f - t * 5.0f) * (0.4f + 0.6f * e) + 1.4f * e * spikes + 0.15f * std::sin(x * 1.3f + t * 9.0f) * e); }
            if (style == Liquid) { if (which == 0) return mid + (0.7f + 1.9f * e) * (0.6f * std::sin(x * 0.17f - t * 1.6f) + 0.4f * std::sin(x * 0.07f + t * 0.9f));
                return mid + 2.0f + (0.5f + 1.3f * e) * std::sin(x * 0.12f - t * 1.1f + 1.3f); }
            return mid - 1.0f + 0.6f * std::sin(x * 0.3f - t * 2.0f) * (0.3f + 0.7f * e); };
        for (int which = 0; which < (style == Liquid ? 2 : 1); ++which)
            for (int x = 0; x < AW; ++x) { const float y0 = waveY((float) x, which), y1 = waveY((float) x + 1.0f, which); const int a = (int) std::round(juce::jmin(y0, y1)), b = (int) std::round(juce::jmax(y0, y1));
                for (int y = juce::jmax(0, a); y <= juce::jmin(AH - 1, b); ++y) { const bool on = lit(edge, x, y);
                    const RGB c = on ? mix(acc, which ? aqua : accHi, 0.3f + 0.5f * e) : mix(rgb(0x0f3a38), aqua, 0.18f);   // dark: a faint cyan wave
                    if (on) put(x, y, mix(rgb(0x0b1d16), c, 0.45f + 0.55f * e), (0.35f + 0.65f * e) * (which ? 0.6f : 1.0f)); else blend(x, y, c, 0.8f, 0.10f); } }
        if (style == Groove) {   // equaliser bars in a swung rhythm: even steps on the beat, odd ones late by the swing
            const float slot = 0.32f, sw = (float) pos * 0.5f, cyc = 2.0f * slot;
            for (int i = 0; i * 4 + 1 < AW; ++i) { const int x = i * 4 + 1; const float ph = std::fmod(t + (float) i * 0.018f, cyc), hitAt = (i % 2 == 0) ? 0.0f : slot * (1.0f + sw);
                float since = ph - hitAt; if (since < 0) since += cyc; const float env = std::exp(-since * 7.0f) * (lively ? 1.0f : 0.5f);
                const bool on = lit(edge, x, AH - 4); const int hgt = 1 + (int) std::round(env * (on ? 1.5f + 4.0f * e : 1.0f));
                for (int k = 0; k < hgt; ++k) { const int y = AH - 4 - k; for (int dx = 0; dx < 2; ++dx) { if (on) put(x + dx, y, mix(acc, white, k == hgt - 1 ? 0.5f : 0.0f), 0.5f + 0.5f * e); else blend(x + dx, y, aqua, 0.22f, 0.05f); } } } }
        // 3. motes: fireflies (Electric / Groove: hard 1-pixel squares that blink) or bubbles (Liquid: round, rising)
        for (auto& m : motes) { const int x = (int) m.x, y = (int) m.y; const bool on = lit(edge, x, y); const float tw = 0.5f + 0.5f * std::sin(m.ph * 2.3f);
            if (!on) { blend(x, y, rgb(0x1a4a40), 0.5f * tw, 0.04f); continue; }
            const RGB c = mix(style == Liquid ? aqua : acc, mix(cyan, white, 0.3f), tw); const float L = (0.4f + 0.6f * e) * (0.5f + 0.5f * tw);
            if (style == Groove) { const float since = std::fmod(t + m.ph, 0.64f); if (since > 0.3f) continue; }
            put(x, y, c, L); if (style == Liquid && e > 0.25f) { for (auto d : { juce::Point<int>(1, 0), juce::Point<int>(-1, 0), juce::Point<int>(0, 1), juce::Point<int>(0, -1) }) blend(x + d.x, y + d.y, c, 0.35f, L * 0.4f); } }
        // 4. the ghost (between the towers and the traces in front)
        drawGhost(e, kArtX);
        // 5. the traces in front: a bottom trace with pads and little vias, a sparse top trace. These hide what is behind them.
        litPads.clear(); const int yb = AH - 2;
        for (int x = 0; x < AW; ++x) { const int wx = x - nearS + 4000; const bool gap = (wx % 9 == 8) && hash(wx / 9 * 5 + 1) < 0.4f;
            if (!gap) { const bool on = lit(edge, x, yb); const bool hot = on && hash(wx / 9) < 0.3f + 0.7f * e && !flick(x);
                put(x, yb, on ? (hot ? mix(rgb(0x17a85a), acc, e) : rgb(0x12402a)) : rgb(0x0d1d17), hot ? 0.25f + 0.35f * e : 0.0f); }
            if (wx % 9 == 4 && hash(wx * 3) < 0.75f) {   // a pad (2 x 2) and a via up to a node
                const bool on = lit(edge, x, yb - 1), hot = on && hash(wx * 11) < 0.25f + 0.75f * e && !flick(x);
                const RGB pc = hot ? mix(acc, mint, 0.6f) : on ? rgb(0x1a5a3c) : rgb(0x112620); const float pl = hot ? 0.8f : 0.0f;
                for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) put(x + dx, yb - dy, pc, pl);
                if (hot) litPads.push_back(x);
                const int via = 1 + (int) (hash(wx * 17) * 3.0f); for (int k = 0; k < via; ++k) put(x, yb - 2 - k, on ? (hot ? acc : rgb(0x16503a)) : rgb(0x0f221b), hot ? 0.3f : 0.0f);
                put(x, yb - 2 - via, hot ? white : on ? rgb(0x2a7a58) : rgb(0x13291f), hot ? 0.9f : 0.0f); }
            if (hash((x - nearS / 2 + 4000) / 6 * 3 + 7) < 0.45f && AH > 9) { const bool on = lit(edge, x, 1); put(x, 1, on ? mix(rgb(0x123d29), acc, 0.4f * e) : rgb(0x0b1813), on ? 0.12f * e : 0.0f); } }
        if (style == Electric) for (float p : pulses) { const int x = (int) p; if (x >= 0 && x < AW) { put(x, yb, white, 1.0f); put(x - 1, yb, mint, 0.8f); put(x - 2, yb, acc, 0.5f); } }
        for (auto& s : sparks) { const float a = s.life / 0.13f; for (auto& p : s.px) put(p.x, p.y, mix(acc, white, 0.5f + 0.5f * a), 0.7f + 0.6f * a); }
        // 6. Liquid: a sloshing pool in front (translucent, so the traces glow through it)
        if (style == Liquid) for (int x = 0; x < AW; ++x) { if (!lit(edge, x, AH - 3) && (float) x > edge) continue;
            const float lvl = (float) AH - 3.2f - 1.4f * e + 0.7f * std::sin((float) x * 0.21f - t * 2.1f) + 0.4f * std::sin((float) x * 0.05f + t * 0.8f);
            for (int y = juce::jmax(0, (int) std::ceil(lvl)); y < AH; ++y) { if (!lit(edge, x, y)) continue; const bool surf = y == (int) std::ceil(lvl);
                blend(x, y, surf ? cyan : aqua, surf ? 0.75f : 0.28f, surf ? 0.6f : 0.25f); } }
        // 7. the fine bright rim where light meets dark
        for (int y = 0; y < AH; ++y) for (int x = juce::jmax(0, (int) edge - 7); x < juce::jmin(AW - 1, (int) edge + 1); ++x)
            if (lit(edge, x, y) && !lit(edge, x + 1, y) && edge - (float) x < 2.5f) blend(x, y, mix(accHi, white, 0.3f), 0.45f, 0.6f);
        // 8. flecks (the 100 % sparkles) on top
        for (auto& f : flecks) put((int) f.x, (int) f.y, mix(mint, white, f.life), 1.0f);
        // the bloom: the light buffer, blurred (three box passes each way)
        blm = emi; std::vector<float> tmp(blm.size());
        for (int pass = 0; pass < 3; ++pass) {
            for (int y = 0; y < AH; ++y) for (int x = 0; x < AW; ++x) for (int ch = 0; ch < 3; ++ch) { float s = 0; int n = 0; for (int d = -1; d <= 1; ++d) { const int xx = x + d; if (xx < 0 || xx >= AW) continue; s += blm[(size_t) ((y * AW + xx) * 3 + ch)]; ++n; } tmp[(size_t) ((y * AW + x) * 3 + ch)] = s / (float) n; }
            for (int y = 0; y < AH; ++y) for (int x = 0; x < AW; ++x) for (int ch = 0; ch < 3; ++ch) { float s = 0; int n = 0; for (int d = -1; d <= 1; ++d) { const int yy = y + d; if (yy < 0 || yy >= AH) continue; s += tmp[(size_t) ((yy * AW + x) * 3 + ch)]; ++n; } blm[(size_t) ((y * AW + x) * 3 + ch)] = s / (float) n; } }
    }
    void drawGhost(float e, float kArtX) {
        static const char* body[6] { ".###.", "#####", "#o#o#", "#####", "#####", "#.#.#" };
        const bool excited = e > 0.72f && awake > 0.5f; const float bobAmp = awake > 0.5f ? (excited ? 1.0f : 0.6f) : 0.0f, bobSp = excited ? 5.0f : 2.2f;
        float hop = 0; if (celebrate > 0) hop = -std::abs(std::sin((1.4f - celebrate) * 3.14159f / 0.35f)) * 3.0f;
        const int gx = (int) std::round(ghostX), gy = (int) std::round((float) AH * 0.5f - 3.5f + (awake < 0.5f ? 1.5f : 0.0f) + std::sin(t * bobSp) * bobAmp + hop);
        const bool wig = awake > 0.5f && std::fmod(t * (excited ? 8.0f : 3.0f), 2.0f) < 1.0f;
        const RGB bodyC = awake > 0.5f ? (excited ? rgb(0xe8fff4) : rgb(0xd6f8f2)) : rgb(0x6f8c86);
        const float L = awake > 0.5f ? (excited ? 0.75f : 0.35f) : 0.0f; const bool eyesOpen = awake > 0.5f && blink > 0;
        if (excited) for (int y = -1; y <= 6; ++y) for (int x = -1; x <= 5; ++x) {   // a glow outline
            const bool in = y >= 0 && y < 6 && x >= 0 && x < 5 && body[y][x] != '.'; if (in) continue; bool nb = false;
            for (int dy = -1; dy <= 1 && !nb; ++dy) for (int dx = -1; dx <= 1; ++dx) { const int yy = y + dy, xx = x + dx; if (yy >= 0 && yy < 6 && xx >= 0 && xx < 5 && body[yy][xx] != '.') { nb = true; break; } }
            if (nb && bayer(gx + x, gy + y) < ghostVis * 0.6f) blend(gx + x, gy + y, rgb(0x3dff8c), 0.45f, 0.6f); }
        for (int y = 0; y < 6; ++y) for (int x = 0; x < 5; ++x) { char ch = body[y][x]; if (y == 5 && wig) ch = ".#.#."[x]; if (ch == '.') continue;
            if (bayer(gx + x, gy + y) > ghostVis) continue;   // fades in / out with the dither
            if (ch == 'o') put(gx + x, gy + y, eyesOpen ? rgb(0x0a1418) : mix(bodyC, rgb(0x0a1418), 0.45f), 0.0f); else put(gx + x, gy + y, bodyC, L); }
        for (auto& z : zees) if (z.life > 0.2f) put((int) z.x, (int) z.y, rgb(0x5fb8b0, z.life), 0.15f);
        juce::ignoreUnused(kArtX);
    }

    // ------------------------------------------------------------ the frame: glass tube + the glass knob, per real pixel
    juce::Image frame; juce::Rectangle<float> frameArea;
    RGB world(float lx, float ly, const Geo& G) const {   // the world at a point (nearest art pixel) + its bloom (smooth)
        const float ax = (lx - G.track.getX()) / kArt, ay = (ly - G.track.getY()) / kArt;
        const int ix = juce::jlimit(0, AW - 1, (int) std::floor(ax)), iy = juce::jlimit(0, AH - 1, (int) std::floor(ay)); const size_t i = (size_t) ((iy * AW + ix) * 3);
        RGB c { col[i], col[i + 1], col[i + 2] };
        const float bx = juce::jlimit(0.0f, (float) AW - 1.001f, ax - 0.5f), by = juce::jlimit(0.0f, (float) AH - 1.001f, ay - 0.5f); const int x0 = (int) bx, y0 = (int) by; const float fx = bx - (float) x0, fy = by - (float) y0;
        const int x1 = juce::jmin(AW - 1, x0 + 1), y1 = juce::jmin(AH - 1, y0 + 1);
        auto B = [&](int x, int y, int ch) { return blm[(size_t) ((y * AW + x) * 3 + ch)]; };
        const float k = 1.25f;
        c.r += k * ((B(x0, y0, 0) * (1 - fx) + B(x1, y0, 0) * fx) * (1 - fy) + (B(x0, y1, 0) * (1 - fx) + B(x1, y1, 0) * fx) * fy);
        c.g += k * ((B(x0, y0, 1) * (1 - fx) + B(x1, y0, 1) * fx) * (1 - fy) + (B(x0, y1, 1) * (1 - fx) + B(x1, y1, 1) * fx) * fy);
        c.b += k * ((B(x0, y0, 2) * (1 - fx) + B(x1, y0, 2) * fx) * (1 - fy) + (B(x0, y1, 2) * (1 - fx) + B(x1, y1, 2) * fx) * fy);
        return c; }
    static float sstep(float a, float b, float x) { const float t = juce::jlimit(0.0f, 1.0f, (x - a) / (b - a)); return t * t * (3.0f - 2.0f * t); }

    void render(const Geo& G, float s) {
        const auto b = getLocalBounds().toFloat(); frameArea = G.track.expanded(4.0f, 4.0f).getIntersection(b);
        const int FW = juce::jmax(1, (int) std::ceil(frameArea.getWidth() * s)), FH = juce::jmax(1, (int) std::ceil(frameArea.getHeight() * s));
        if (frame.isNull() || frame.getWidth() != FW || frame.getHeight() != FH) frame = juce::Image(juce::Image::ARGB, FW, FH, true, juce::SoftwareImageType());
        frameArea.setSize((float) FW / s, (float) FH / s);
        const float e = energy(), en = isEnabled() ? 1.0f : 0.0f, focus = hasKeyboardFocus(false) ? 1.0f : 0.0f;
        const float tcx = G.track.getCentreX(), tcy = G.track.getCentreY(), rt = G.track.getHeight() * 0.5f, ta = juce::jmax(0.0f, G.track.getWidth() * 0.5f - rt);
        // the knob
        const float kx = knobX(G), ky = tcy, sx = 1.0f + stretch, sy = 1.0f / (1.0f + stretch * 0.55f), r = G.r, L = G.half, ka = juce::jmax(0.0f, L - r);
        const RGB aqua = rgb(0x2ee6d6), cyan = rgb(0x8df7ff), mint = rgb(0x8cffc0), glowC = style == Liquid ? rgb(0x54f0e4) : rgb(0x5dffb0);
        const float speed = std::abs(velPx), streak = lively ? sstep(160.0f, 700.0f, speed) : 0.0f, streakLen = juce::jmin(70.0f, speed * 0.07f), dir = velPx >= 0 ? 1.0f : -1.0f;
        const float glowK = (0.22f + 0.40f * e + 0.45f * pulse + 0.15f * focus) * (0.4f + 0.6f * en);
        juce::Image::BitmapData bd(frame, juce::Image::BitmapData::writeOnly);
        for (int py = 0; py < FH; ++py) {
            const float ly = frameArea.getY() + ((float) py + 0.5f) / s;
            for (int px = 0; px < FW; ++px) {
                const float lx = frameArea.getX() + ((float) px + 0.5f) / s;
                // ---- the knob's distance (in its own stretched space)
                const float kxL = (lx - kx) / sx, kyL = (ly - ky) / sy; const float qx = juce::jmax(std::abs(kxL) - ka, 0.0f), qy = kyL; const float ql = std::sqrt(qx * qx + qy * qy); const float dk = ql - r;
                // ---- outside: the halo under / around the knob (glow, shadow, the release ring) - normal alpha over the window
                float outR = 0, outG = 0, outB = 0, outA = 0;
                {   const float qsy = kyL - 1.6f, dsh = std::sqrt(qx * qx + qsy * qsy) - r;
                    const float shadow = dk > 0 ? 0.5f * std::exp(-juce::jmax(0.0f, dsh) * 0.9f) : 0.0f;
                    const float under = 0.6f + 0.4f * juce::jlimit(-1.0f, 1.0f, kyL / r);
                    float gl = dk > 0 ? glowK * std::exp(-dk * 0.55f) * under : 0.0f;
                    if (pulse > 0 && dk > 0) { const float R = (1.0f - pulse) * 12.0f; gl += pulse * 0.9f * std::exp(-(dk - R) * (dk - R) * 0.35f); }
                    gl = juce::jmin(1.0f, gl); outA = gl + shadow * 0.45f * (1.0f - gl); outR = glowC.r * gl; outG = glowC.g * gl; outB = glowC.b * gl; }
                // ---- the glass tube with the world inside
                const float tqx = juce::jmax(std::abs(lx - tcx) - ta, 0.0f), tqy = ly - tcy; const float dt = std::sqrt(tqx * tqx + tqy * tqy) - rt; const float tcov = juce::jlimit(0.0f, 1.0f, 0.5f - dt * s);
                float cr = outR, cg = outG, cb = outB, ca = outA;
                if (tcov > 0) {
                    RGB c = world(lx, ly, G); const float wall = -dt;
                    const float dim = 0.55f + 0.45f * sstep(0.0f, 4.0f, wall); c.r *= dim; c.g *= dim; c.b *= dim;
                    const float rim = std::exp(-wall * 1.4f) * (0.22f + 0.30f * e); c.r += aqua.r * rim; c.g += aqua.g * rim; c.b += aqua.b * rim;
                    const float ends = sstep(rt * 0.6f, rt * 1.8f, juce::jmin(lx - G.track.getX(), G.track.getRight() - lx));
                    const float hl = std::exp(-(ly - (G.track.getY() + 2.4f)) * (ly - (G.track.getY() + 2.4f)) * 1.2f) * ends * (0.06f + 0.14f * e); c.r += hl; c.g += hl; c.b += hl;   // the tube catches the light
                    const float lowRef = std::exp(-(G.track.getBottom() - 2.2f - ly) * (G.track.getBottom() - 2.2f - ly) * 1.5f) * ends * 0.05f; c.g += lowRef; c.b += lowRef * 0.8f;
                    if (dk > 0) {   // around the knob, on the glass: the shadow, the glow, the streaks of a fast move
                        const float qsy = kyL - 1.6f, dsh = std::sqrt(qx * qx + qsy * qsy) - r; const float sh = 1.0f - 0.55f * std::exp(-juce::jmax(0.0f, dsh) * 0.9f); c.r *= sh; c.g *= sh; c.b *= sh;
                        const float gl = glowK * std::exp(-dk * 0.45f) * (0.6f + 0.4f * juce::jlimit(-1.0f, 1.0f, kyL / r)); c.r += glowC.r * gl; c.g += glowC.g * gl; c.b += glowC.b * gl;
                        if (streak > 0) { const float behind = (kx - lx) * dir - L * sx; if (behind > 0 && behind < streakLen) { const float f = (1.0f - behind / streakLen); float rows = 0;
                                for (float yk : { -0.42f, 0.0f, 0.38f }) rows += std::exp(-(ly - (ky + yk * r)) * (ly - (ky + yk * r)) * 2.2f);
                                const float add = streak * f * f * rows * 0.75f; c.r += mint.r * add; c.g += mint.g * add; c.b += mint.b * add; } }
                        if (pulse > 0) { const float R = (1.0f - pulse) * 12.0f; const float ring = pulse * 0.7f * std::exp(-(dk - R) * (dk - R) * 0.35f); c.r += glowC.r * ring; c.g += glowC.g * ring; c.b += glowC.b * ring; } }
                    cr = c.r * tcov + cr * (1.0f - tcov); cg = c.g * tcov + cg * (1.0f - tcov); cb = c.b * tcov + cb * (1.0f - tcov); ca = tcov + ca * (1.0f - tcov); }
                // ---- the glass knob
                const float kcov = juce::jlimit(0.0f, 1.0f, 0.5f - dk * s * juce::jmin(sx, sy));
                if (kcov > 0) {
                    const float depth = juce::jlimit(0.0f, 1.0f, -dk / r), t1 = 1.0f - depth, nz = std::sqrt(juce::jmax(0.0f, 1.0f - t1 * t1));
                    float dxn = 0, dyn = 0; if (ql > 1e-4f) { dxn = (kxL < 0 ? -qx : qx) / ql; dyn = qy / ql; } const float nx = dxn * t1, ny = dyn * t1;
                    // refraction: magnify the world under it and bend it near the curved rim; split R / G / B a little at the rim
                    const float bend = r * 0.55f, mag = 0.72f, disp = 0.55f * t1 * t1;
                    const float bx0 = kx + (lx - kx) * mag - nx * bend, by0 = ky + (ly - ky) * mag - ny * bend;
                    const RGB sr = world(bx0 + nx * disp, by0 + ny * disp, G), sg = world(bx0, by0, G), sb = world(bx0 - nx * disp, by0 - ny * disp, G);
                    RGB c { sr.r, sg.g, sb.b };
                    const float path = 1.0f / juce::jmax(0.28f, nz), tintK = juce::jmin(1.0f, 0.30f + (path - 1.0f) * 0.45f);   // thick glass: more colour where the path is long
                    c.r *= 1.0f + (0.80f - 1.0f) * tintK; c.b *= 1.0f + (0.92f - 1.0f) * tintK; c.r *= 1.14f; c.g *= 1.14f; c.b *= 1.14f;
                    const float bodyK = 0.05f + 0.11f * t1; c.r += 0.50f * bodyK; c.g += 0.92f * bodyK; c.b += 0.86f * bodyK;          // the pale-aqua glass itself
                    const float bandW = r * 0.20f, band = 1.0f - sstep(bandW - 0.6f, bandW + 0.6f, std::abs(kxL));                       // the darker band round its waist (= the value)
                    c.r *= 1.0f - 0.42f * band; c.g *= 1.0f - 0.36f * band; c.b *= 1.0f - 0.34f * band;
                    const float bandEdge = std::exp(-(std::abs(kxL) - bandW) * (std::abs(kxL) - bandW) * 2.5f) * 0.16f * (0.6f + 0.4f * nz); c.r += aqua.r * bandEdge; c.g += aqua.g * bandEdge; c.b += aqua.b * bandEdge;
                    const float lvl = r * 0.36f + std::sin(kxL * 0.45f + t * 2.0f) * r * 0.05f + slosh * kxL * 0.4f;                 // a faint liquid line inside
                    if (kyL > lvl) { c.r += 0.01f; c.g += 0.04f; c.b += 0.04f; } { const float ll = std::exp(-(kyL - lvl) * (kyL - lvl) * 3.0f) * 0.08f * nz; c.r += cyan.r * ll; c.g += cyan.g * ll; c.b += cyan.b * ll; }
                    for (auto& bu : bubbles) { const float bxp = bu.u * (ka + r * 0.5f), byp = bu.v * r * 0.75f, br = bu.rad + 0.15f * std::sin(bu.ph * 3.0f); const float d = std::sqrt((kxL - bxp) * (kxL - bxp) + (kyL - byp) * (kyL - byp));
                        if (d < br + 1.2f) { const float ring = std::exp(-(d - br) * (d - br) * 7.0f) * 0.32f * nz; c.r += 0.75f * ring; c.g += ring; c.b += ring;
                            const float dot = std::exp(-((kxL - bxp + br * 0.35f) * (kxL - bxp + br * 0.35f) + (kyL - byp + br * 0.35f) * (kyL - byp + br * 0.35f)) * 9.0f) * 0.45f; c.r += dot; c.g += dot; c.b += dot; } }
                    const float F = 0.03f + 0.97f * std::pow(juce::jlimit(0.0f, 1.0f, 1.0f - nz), 3.0f), fres = F * (0.55f + 0.45f * e + 0.4f * pulse + 0.25f * focus) * (0.8f + 0.45f * (-dyn * t1));
                    c.r += 0.74f * fres; c.g += 1.0f * fres; c.b += 0.90f * fres;   // bright Fresnel edges
                    c.r += 0.10f * F * juce::jmax(0.0f, -dxn); c.b += 0.12f * F * juce::jmax(0.0f, dxn);                              // the colour fringe on the curved rim
                    const float ends = 1.0f - sstep(ka * 0.5f, L * 0.98f, std::abs(kxL)) * 0.85f;
                    const float hl = std::exp(-(t1 - 0.60f) * (t1 - 0.60f) * 90.0f) * std::pow(juce::jmax(0.0f, -dyn), 2.0f) * ends * (0.50f + 0.22f * e); c.r += hl; c.g += hl; c.b += hl;   // the long curved highlight
                    const float spx = kxL + ka * 0.78f, spy = kyL + r * 0.46f; const float spec = std::exp(-(spx * spx + spy * spy) * 7.0f) * 1.6f; c.r += spec; c.g += spec; c.b += spec;   // the one sharp glint
                    const float bounce = std::exp(-(t1 - 0.78f) * (t1 - 0.78f) * 30.0f) * std::pow(juce::jmax(0.0f, dyn), 2.0f) * (0.08f + 0.12f * e); c.r += mint.r * bounce; c.g += mint.g * bounce; c.b += mint.b * bounce;
                    const float edgeDark = sstep(0.0f, 0.6f, -dk); c.r *= 0.62f + 0.38f * edgeDark; c.g *= 0.62f + 0.38f * edgeDark; c.b *= 0.62f + 0.38f * edgeDark;
                    cr = c.r * kcov + cr * (1.0f - kcov); cg = c.g * kcov + cg * (1.0f - kcov); cb = c.b * kcov + cb * (1.0f - kcov); ca = kcov + ca * (1.0f - kcov); }
                // greyed out when it does nothing (STRENGTH / SWING with QUANTIZE OFF)
                if (en == 0.0f) { const float l = (cr * 0.3f + cg * 0.59f + cb * 0.11f); cr = (l + (cr - l) * 0.2f) * 0.55f; cg = (l + (cg - l) * 0.2f) * 0.55f; cb = (l + (cb - l) * 0.2f) * 0.55f; }
                // write premultiplied (colour never above alpha)
                ca = juce::jlimit(0.0f, 1.0f, ca); const float A = ca;
                auto q = [A](float v) { return (juce::uint8) juce::jlimit(0, 255, (int) (juce::jlimit(0.0f, A, v) * 255.0f + 0.5f)); };
                // (inside the tube the colour is already premultiplied by coverage; outside it is glow * alpha)
                reinterpret_cast<juce::PixelARGB*>(bd.getPixelPointer(px, py))->setARGB((juce::uint8) (A * 255.0f + 0.5f), q(cr), q(cg), q(cb));
            }
        }
    }
};

} // namespace hum::ui
