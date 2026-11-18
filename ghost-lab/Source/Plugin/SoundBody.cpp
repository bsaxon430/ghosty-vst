#include "SoundBody.h"
#include "../Core/AudioTools.h"

namespace glab
{
using namespace theme;
static constexpr int C = Profile::kColumns, B = Profile::kBands;
static constexpr float kPi = juce::MathConstants<float>::pi, kTwoPi = juce::MathConstants<float>::twoPi;

namespace
{
inline float fastExp (float x) { x = juce::jlimit (-20.0f, 0.0f, x); x = 1.0f + x / 64.0f; x *= x; x *= x; x *= x; x *= x; x *= x; x *= x; return x; }
inline float smooth (float e0, float e1, float x) { const float t = juce::jlimit (0.0f, 1.0f, (x - e0) / (e1 - e0)); return t * t * (3.0f - 2.0f * t); }
inline float sq (float x) { return x * x; }
}

SoundBody::SoundBody()
{
    dispE.assign (C, 0.15f); targetE = dispE;
    dispB.assign ((size_t) C * B, 0.2f); targetB = dispB;
    dispF.assign (C, 0.0f); targetF = dispF;
    setRepaintsOnMouseActivity (false);
    setOpaque (true);   // (the long tube: see setNarrowStyle)
    updateCursor();
    startTimerHz (60);
}

// A sound's sudden starts (a piano note, a consonant) are steps in its profile; drawn as they are they cut the body
// with a hard upright edge. Rounded off along the sound (a bell 2 columns wide) they are steep shoulders instead.
static void roundOff (std::vector<float>& v, int stride)
{
    static const float k[] = { 0.2066f, 0.1823f, 0.1253f, 0.0670f, 0.0279f, 0.0090f };   // sums to 1 with both sides
    const int n = (int) v.size() / stride;
    std::vector<float> out (v.size());
    for (int s = 0; s < stride; ++s)
        for (int i = 0; i < n; ++i)
        {
            float acc = 0;
            for (int j = -5; j <= 5; ++j) acc += k[std::abs (j)] * v[(size_t) (juce::jlimit (0, n - 1, i + j) * stride + s)];
            out[(size_t) (i * stride + s)] = acc;
        }
    v.swap (out);
}

void SoundBody::setProfile (const Profile& p)
{
    if ((int) p.energy.size() != C || (int) p.bands.size() != C * B) return;
    targetE = p.energy;
    targetB = p.bands;
    if ((int) p.flux.size() == C) targetF = p.flux; else targetF.assign (C, 0.0f);
    roundOff (targetE, 1); roundOff (targetB, B); roundOff (targetF, 1);
    if (empty) { empty = false; frameValid = softValid = false; updateCursor(); }
}

void SoundBody::setNarrowStyle (bool n)
{
    if (narrow != n) { narrow = n; staticDirty = true; }
    setOpaque (! narrow);   // in the tube it fills its whole box (the page under it is never drawn); the phone-width window has round corners
    repaint();
}

void SoundBody::settle (double atTime)
{
    dispE = targetE; dispB = targetB; dispF = targetF;
    t = atTime; playShown = playhead; touchGlow = pressing ? 1.0f : 0.0f; hoverGlow = 0.0f;
    frameValid = softValid = false;
}

void SoundBody::setDeformation (float pull, float pinch, float twist, float shake, float bend, float drag, float where)
{
    defPull = tPull = pull; defPinch = tPinch = pinch; defTwist = tTwist = twist;
    defShake = tShake = shake; defBend = tBend = bend; defDrag = tDrag = drag;
    vPull = vPinch = vTwist = vShake = vBend = vDrag = 0.0f;
    defPullC = grabU = where;
    frameValid = softValid = false;
}

juce::Rectangle<float> SoundBody::bodyRect() const
{
    const auto r = getLocalBounds().toFloat();
    return narrow ? r.reduced (r.getWidth() * 0.06f, r.getHeight() * 0.1f) : r.reduced (r.getWidth() * 0.028f, r.getHeight() * 0.07f);
}

float SoundBody::xToU (float x) const
{
    const auto b = bodyRect();
    return juce::jlimit (0.0f, 1.0f, (x - b.getX()) / juce::jmax (1.0f, b.getWidth()));
}

void SoundBody::updateCursor()
{
    setMouseCursor (empty ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::DraggingHandCursor);   // empty: click to choose a sound; alive: a hand, grab it
}

bool SoundBody::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files) if (canLoad (juce::File (f))) return true;
    return false;
}

void SoundBody::timerCallback()
{
    const bool frozen = freezeGlow > 0.3f;
    t += (frozen ? 0.15 : 1.0) * (busy ? 2.2 : 1.0) * (1.0 + defShake * 6.0) / 60.0;
    for (size_t i = 0; i < dispE.size(); ++i) dispE[i] += 0.062f * (targetE[i] - dispE[i]);
    for (size_t i = 0; i < dispB.size(); ++i) dispB[i] += 0.062f * (targetB[i] - dispB[i]);
    for (size_t i = 0; i < dispF.size(); ++i) dispF[i] += 0.062f * (targetF[i] - dispF[i]);
    // a shake dies down when the hand stops shaking
    shakeNow *= 0.94f;
    if (pressing) tShake = juce::jmin (tShake, juce::jmax (shakeNow, 0.0f));
    else { tPull = tTwist = tBend = tDrag = 0; tShake = 0; if (magnifyMs <= 0) tPinch = 0; }
    // the shown shape goes with the hand: while it is held, straight to where the hand is (a touch of smoothing, no
    // lag you can feel); once let go, through a damped spring, so it wobbles back like jelly
    auto follow = [this] (float& x, float& v, float target, float k, float damp)
    {
        if (pressing || magnifyMs > 0) { v = (target - x) * 0.36f; x += v; }
        else { v = (v + (target - x) * k) * damp; x += v; }
        if (std::abs (x - target) < 1.0e-5f && std::abs (v) < 1.0e-5f) { x = target; v = 0.0f; }
    };
    follow (defPull, vPull, tPull, 0.11f, 0.84f);
    follow (defPinch, vPinch, tPinch, 0.11f, 0.84f);
    follow (defTwist, vTwist, tTwist, 0.07f, 0.88f);
    follow (defBend, vBend, tBend, 0.13f, 0.85f);
    follow (defDrag, vDrag, tDrag, 0.12f, 0.84f);
    follow (defShake, vShake, tShake, 0.3f, 0.6f);
    defPull = juce::jmax (-0.3f, defPull); defShake = juce::jmax (0.0f, defShake);   // (a pull may wobble a little past rest, the other way)
    touchGlow += ((pressing ? 1.0f : 0.0f) - touchGlow) * 0.22f;
    hoverGlow += ((hovering && ! pressing && ! empty ? 1.0f : 0.0f) - hoverGlow) * 0.12f;
    // the playhead arrives from the editor 20 times a second: glide between its steps (a jump back to the start snaps)
    if (playhead < 0.0f || playShown < 0.0f || std::abs (playhead - playShown) > 0.2f) playShown = playhead;
    else playShown += (playhead - playShown) * 0.3f;
    if (pressing && moved && ! held) sendFeel (true, false);
    if (pressing && ! held && stroke.pts.size() > 0 && juce::Time::getMillisecondCounterHiRes() - pressMs > 600.0
             && stroke.pts.front().getDistanceFrom (stroke.pts.back()) < 6.0f)
    {
        held = true;   // a long press: FREEZE here
        freezeAt (stroke.pts.front().x);
    }
    freezeGlow *= 0.9925f;
    mergeFlash *= 0.964f;
    if (magnifyMs > 0 && juce::Time::getMillisecondCounterHiRes() - magnifyMs > 450.0)
    {
        magnifyMs = 0;
        const float amt = juce::jlimit (-1.0f, 1.0f, (1.0f - magnify) * 2.0f);
        magnify = 1.0f;
        tPinch = 0;
        if (std::abs (amt) > 0.05f && onGesture) onGesture ({ Op::Pinch, { { "amount", amt } }, 1 });
    }
    if (wantsFrame()) repaint();
}

// Calm (nobody touching it, nothing processing, the jelly at rest): its slow life is drawn 30 times a second, and on
// the frames between nothing is drawn at all. Any touch / gesture / change draws every frame (60 a second).
bool SoundBody::wantsFrame()
{
    ++stamp;
    const bool calm = ! pressing && ! busy && touchGlow < 0.02f && freezeGlow < 0.02f && mergeFlash < 0.05f && ! mergeHover
                      && std::abs (defPull) + std::abs (defPinch) + std::abs (defTwist) + std::abs (defBend) + std::abs (defDrag) + defShake < 0.004f;
    return ! calm || (stamp & 1);
}

void SoundBody::freezeAt (float x)
{
    freezeX = x; freezeGlow = 1.0f;
    if (onGesture && ! empty) onGesture ({ Op::Freeze, { { "where", xToU (x) } }, 1 });
}

// ------------------------------------------------------------------------------------------------ the hand
// What a stroke is, in numbers: its length and the straight line from its start to its end, how often it turned back
// sideways (a shake), how far round it turned in all and how much of a circle it is (a twist).
SoundBody::Shape SoundBody::measure (const Stroke& s)
{
    Shape m;
    if (s.pts.size() < 2) return m;
    const auto a = s.pts.front(), b = s.pts.back();
    m.dx = b.x - a.x; m.dy = b.y - a.y; m.chord = a.getDistanceFrom (b);
    m.dur = s.ms.back() - s.ms.front();
    int dir = 0;
    float runX = 0;
    double area = 0;
    // the turning is read from points at least 6 px apart (a slow hand's jitter points nowhere)
    juce::Point<float> from = a, heading;
    bool haveHeading = false;
    for (size_t i = 1; i < s.pts.size(); ++i)
    {
        const auto d = s.pts[i] - s.pts[i - 1];
        m.len += d.getDistanceFromOrigin();
        area += 0.5 * ((double) (s.pts[i - 1].x - a.x) * (double) (s.pts[i].y - a.y) - (double) (s.pts[i].x - a.x) * (double) (s.pts[i - 1].y - a.y));
        runX += d.x;
        if (std::abs (runX) > 8.0f)
        {
            const int nd = runX > 0 ? 1 : -1;
            if (dir != 0 && nd != dir) ++m.reversals;
            dir = nd; runX = 0;
        }
        const auto step = s.pts[i] - from;
        if (step.getDistanceFromOrigin() >= 6.0f)
        {
            if (haveHeading)
            {
                const float turn = std::atan2 (heading.x * step.y - heading.y * step.x, heading.x * step.x + heading.y * step.y);
                if (std::abs (turn) < 2.4f) m.turn += turn;   // (a sharp turn back is a shake, not going round)
            }
            heading = step; haveHeading = true; from = s.pts[i];
        }
    }
    // how much of a circle: the area the path encloses against its length (1 = a circle, about 0 = back and forth on a line)
    const float loops = juce::jmax (1.0f, std::abs (m.turn) / kTwoPi);
    m.round = m.len > 1.0f ? (float) (4.0 * (double) kPi * std::abs (area) * (double) loops / ((double) m.len * (double) m.len)) : 0.0f;
    return m;
}

bool SoundBody::classify (const Stroke& s, Step& out) const
{
    if (s.pts.size() < 2) return false;
    return decide (measure (s), s, out);
}

bool SoundBody::decide (const Shape& m, const Stroke& s, Step& out) const
{
    // going round: a curve of 140 degrees or more, anywhere on the body, any size (build 11: it used to need more than
    // half a turn around the body's very centre). A straight drag turns nothing; a hook at the end of one is too little.
    const bool round = std::abs (m.turn) > 2.5f && m.round > 0.36f && m.len > 70.0f && m.chord < 0.78f * m.len;
    if (m.reversals >= 4 && m.dur < 2500.0 && ! round)
    {
        out = { Op::Mutate, { { "wild", juce::jlimit (0.15f, 1.0f, (float) m.reversals / 12.0f) } }, (juce::uint32) (juce::Time::getMillisecondCounter() | 1) };
        return true;
    }
    if (round)
    {
        out = { Op::Twist, { { "bend", juce::jlimit (-1.0f, 1.0f, m.turn / (1.5f * kPi)) } }, 1 };
        return true;
    }
    const auto a = s.pts.front(), b = s.pts.back();
    if (std::abs (m.dx) >= std::abs (m.dy) && std::abs (m.dx) > 20.0f)
    {
        const float from = xToU (juce::jmin (a.x, b.x)), to = xToU (juce::jmax (a.x, b.x));
        const float span = to - from;
        Step st { Op::Pull, { { "amount", juce::jlimit (1.2f, 8.0f, 1.3f + 5.5f * span) } }, 1 };
        if (span < 0.85f) { st.values["from"] = from; st.values["to"] = to; }
        out = st;
        return true;
    }
    if (std::abs (m.dy) > 20.0f)
    {
        out = { Op::Pinch, { { "amount", juce::jlimit (-1.0f, 1.0f, m.dy / ((float) juce::jmax (1, getHeight()) * 0.45f)) } }, 1 };
        return true;
    }
    return false;
}

void SoundBody::sendFeel (bool active, bool commit)
{
    if (! onFeel) return;
    Feel f;
    f.active = active; f.commit = commit; f.where = grabU;
    if (active)
    {
        // what the hand is doing now (not the jelly's lag): the sound answers at once
        f.pull = juce::jlimit (0.0f, 1.0f, tPull); f.pinch = juce::jlimit (-1.0f, 1.0f, tPinch); f.twist = juce::jlimit (-1.0f, 1.0f, tTwist);
        f.shake = juce::jlimit (0.0f, 1.0f, tShake); f.bend = juce::jlimit (-1.0f, 1.0f, tBend);
    }
    onFeel (f);
}

void SoundBody::press (juce::Point<float> at)
{
    if (empty) return;
    pressing = true; held = false; moved = false;
    reversalsSeen = 0; shakeNow = 0; liveGesture = Op::Count;
    pressMs = juce::Time::getMillisecondCounterHiRes();
    grabU = xToU (at.x); handAt = at;
    stroke = {};
    stroke.pts.push_back (at); stroke.ms.push_back (pressMs);
    updateCursor();
}

void SoundBody::dragTo (juce::Point<float> at)
{
    if (empty || held || ! pressing) return;
    stroke.pts.push_back (at); stroke.ms.push_back (juce::Time::getMillisecondCounterHiRes());
    const auto a = stroke.pts.front();
    if (! moved && at.getDistanceFrom (a) > 4.0f) moved = true;
    handAt = at;
    const auto body = bodyRect();
    const auto m = measure (stroke);
    Step s;
    const bool known = decide (m, stroke, s);
    if (m.reversals > reversalsSeen) { shakeNow = juce::jmin (1.0f, shakeNow + 0.22f * (float) (m.reversals - reversalsSeen)); reversalsSeen = m.reversals; }
    // The body goes with the hand at once, before any gesture is recognised, and nothing jumps when one is: every
    // amount below grows smoothly out of the hand's own movement.
    const float dx = at.x - a.x, dy = at.y - a.y, adx = std::abs (dx), ady = std::abs (dy);
    const float upDown = smooth (0.8f, 1.25f, ady / (adx + 0.001f));          // 1 = an up / down drag, 0 = a sideways one
    const float round = smooth (0.6f, 2.2f, std::abs (m.turn));               // 1 = the hand is going round
    const float shook = juce::jmin (1.0f, shakeNow * 1.4f);                   // 1 = the hand is shaking it
    // the part you grabbed goes up / down and slides sideways with the pointer (less so once the drag has become a
    // squeeze, a turn or a shake)
    const float bend = juce::jlimit (-1.0f, 1.0f, dy / (body.getHeight() * 0.5f)) * (1.0f - 0.6f * upDown * smooth (12.0f, 50.0f, ady)) * (1.0f - 0.7f * round) * (1.0f - 0.7f * shook);
    const float drag = juce::jlimit (-0.6f, 0.6f, dx / juce::jmax (1.0f, body.getWidth())) * (1.0f - 0.7f * round) * (1.0f - 0.5f * shook);
    // sideways: that part of the sound stretches (PULL); up / down: it squeezes or fattens (PINCH), from the first pixels
    const float from = xToU (juce::jmin (a.x, at.x)), to = xToU (juce::jmax (a.x, at.x));
    const float pull = (0.3f + 5.5f * (to - from)) / 7.0f * smooth (8.0f, 24.0f, adx) * (1.0f - upDown) * (1.0f - round) * (1.0f - shook);
    const float pinch = juce::jlimit (-1.0f, 1.0f, dy / ((float) juce::jmax (1, getHeight()) * 0.45f)) * smooth (6.0f, 26.0f, ady) * upDown * (1.0f - round) * (1.0f - shook);
    // going round winds it up as you turn, before it counts as a TWIST (a straight drag turns nothing)
    const float twist = round * juce::jlimit (-1.0f, 1.0f, m.turn / (1.5f * kPi));
    const float shake = juce::jmax (shakeNow, known && s.op == Op::Mutate ? s.get ("wild") : 0.0f);
    if (pull > 0.001f) defPullC = (from + to) * 0.5f;
    liveGesture = known ? s.op : Op::Count;
    tPull = pull; tPinch = pinch; tTwist = twist; tBend = bend; tDrag = drag; tShake = shake; shakeNow = shake;
    if (onLive && moved)
    {
        // what letting go now will do, in plain words
        juce::String hint ("Sideways to PULL   |   up or down to PINCH   |   round and round to TWIST   |   shake to MUTATE");
        if (known && s.op == Op::Pull) hint = s.values.count ("from") > 0 ? "Let go to stretch this part of the sound." : "Let go to stretch the whole sound.";
        else if (known && s.op == Op::Pinch) hint = s.get ("amount") > 0 ? "Let go to squeeze its spectrum." : "Let go to widen its spectrum.";
        else if (known && s.op == Op::Twist) hint = "Let go to twist its character.";
        else if (known && s.op == Op::Mutate) hint = "Let go to mutate it. Shake harder for wilder.";
        onLive (known ? s.describe() : juce::String(), hint);
    }
    if (moved) sendFeel (true, false);
}

void SoundBody::release (juce::Point<float> at, int clicks)
{
    if (empty) { if (onClickEmpty) onClickEmpty(); return; }
    if (! pressing) return;
    pressing = false;
    const bool wasMoved = moved, wasHeld = held;
    moved = false; held = false; liveGesture = Op::Count;
    updateCursor();
    if (wasHeld) { if (onLive) onLive ({}, {}); return; }   // (the long press already froze it)
    stroke.pts.push_back (at); stroke.ms.push_back (juce::Time::getMillisecondCounterHiRes());
    Step s;
    const bool known = classify (stroke, s);
    if (wasMoved) sendFeel (false, known);              // the hand is off: the real process runs now (or nothing does)
    if (known) { if (onGesture) onGesture (s); }
    else if (! wasMoved && clicks < 2 && onClick) onClick();
    if (onLive) onLive ({}, {});
}

void SoundBody::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! empty) freezeAt (e.position.x);
}

void SoundBody::mouseMagnify (const juce::MouseEvent&, float scale)
{
    if (empty) return;
    magnify *= scale;
    tPinch = juce::jlimit (-1.0f, 1.0f, (1.0f - magnify) * 2.0f);
    magnifyMs = juce::Time::getMillisecondCounterHiRes();
}

// ------------------------------------------------------------------------------------------------ drawing
// (build 11) The living sound is liquid light, after the tube on Brandon's banner: five translucent ribbons wound round
// a glowing core, each with two bright edges and fine lines along it, wisps trailing outside, sparks drifting along it,
// bubbles rising past. Everything is light added over the tube's dark liquid, so where ribbons cross they burn whiter.
//   the sound    loud moments are thick and bright, quiet ones a thin thread; the lows lift its top edge, the highs its
//                bottom; each ribbon is lit by its own part of the spectrum; rough, busy moments throw more sparks
//   the hand     the part you grab goes with the pointer (up / down and sideways); PULL spreads that part out, PINCH
//                squeezes / fattens it, TWIST winds the ribbons round, a shake rattles every line
// A frame is made in three steps, to stay cheap (the test holds it under 10% of one core at 60 fps):
//   1. per column of a coarse grid (one sample every 4 px): the outline, the colour, where every line runs
//   2. a soft light field on that grid (glow, haze, the ribbons' veils), scaled up smoothly over our kept copy of the
//      tube's liquid (plain byte loops the compiler turns into vector code). It is soft, so nobody can see it move
//      30 times a second instead of 60: with a hand on the body it is redone every other frame
//   3. the fine lines, sparks and bubbles, drawn sharp over a copy of that, every frame, at the screen's own pixels
//      (up to 2x on a Retina screen)
namespace
{
// value noise in a small tile (it stays in the cache); the tile repeats every 64 units both ways
struct Noise
{
    enum { N = 64, M = N - 1 };
    float v[N * N];
    Noise() { juce::Random r (0x6a05); for (auto& x : v) x = r.nextFloat(); }
    float at (float x, float y) const   // x, y >= 0
    {
        const int xi = (int) x, yi = (int) y;
        float tx = x - (float) xi, ty = y - (float) yi;
        tx = tx * tx * (3.0f - 2.0f * tx); ty = ty * ty * (3.0f - 2.0f * ty);
        const int x0 = xi & M, x1 = (xi + 1) & M, y0 = (yi & M) * N, y1 = ((yi + 1) & M) * N;
        const float a = v[y0 + x0], b = v[y0 + x1], c = v[y1 + x0], d = v[y1 + x1];
        const float top = a + tx * (b - a), bot = c + tx * (d - c);
        return top + ty * (bot - top);
    }
};
const Noise& noise() { static const Noise n; return n; }

// the living sound's colour along its length: purple -> magenta -> blue -> cyan -> acid green, a yellow tip
struct Ramp
{
    enum { N = 256 };
    float c[N + 1][3];   // r, g, b (0..1)
    Ramp()
    {
        struct Stop { float x, r, g, b; };
        const Stop st[] = { { 0.00f, 0.50f, 0.27f, 1.00f }, { 0.15f, 0.68f, 0.40f, 1.00f }, { 0.30f, 1.00f, 0.33f, 0.84f }, { 0.44f, 0.48f, 0.45f, 1.00f },
                            { 0.58f, 0.25f, 0.84f, 1.00f }, { 0.72f, 0.24f, 1.00f, 0.66f }, { 0.87f, 0.65f, 0.95f, 0.23f }, { 1.00f, 0.90f, 1.00f, 0.26f } };
        const int n = (int) (sizeof (st) / sizeof (st[0]));
        for (int i = 0; i <= N; ++i)
        {
            const float x = (float) i / (float) N;
            int k = 0;
            while (k < n - 2 && x > st[k + 1].x) ++k;
            const float f = smooth (st[k].x, st[k + 1].x, x);
            c[i][0] = st[k].r + f * (st[k + 1].r - st[k].r); c[i][1] = st[k].g + f * (st[k + 1].g - st[k].g); c[i][2] = st[k].b + f * (st[k + 1].b - st[k].b);
        }
    }
    const float* at (float x) const { return c[(int) (juce::jlimit (0.0f, 1.0f, x) * (float) N)]; }
};
const Ramp& ramp() { static const Ramp r; return r; }

// light (0 = none, 1 = a full colour, up to 4) to pixel values, by its strongest channel: the colour keeps its hue as
// it gets brighter (it rolls off, never clips), and only the very brightest parts burn toward white
struct Tone
{
    float gain[1024], white[1024];
    Tone()
    {
        for (int i = 0; i < 1024; ++i)
        {
            const float m = juce::jmax (1.0e-3f, (float) i / 256.0f);
            gain[i] = 255.0f * (1.0f - std::exp (-1.3f * m)) / m;
            white[i] = 255.0f * 0.8f * smooth (0.9f, 3.4f, m);
        }
    }
};
const Tone& tone() { static const Tone tn; return tn; }

// sin and cos of any angle, fast (the worst error is 0.001: nothing you can see)
inline void fastSinCos (float x, float& s, float& c)
{
    const float turns = x * (1.0f / kTwoPi);
    x -= kTwoPi * (float) (int) (turns + (turns >= 0.0f ? 0.5f : -0.5f));   // -pi .. pi
    auto wave = [] (float a) { const float y = 1.27323954f * a - 0.405284735f * a * std::abs (a); return 0.225f * (y * std::abs (y) - y) + y; };
    s = wave (x);
    float xc = x + 1.57079633f; if (xc > kPi) xc -= kTwoPi;
    c = wave (xc);
}
inline float fastSin (float x) { float s, c; fastSinCos (x, s, c); return s; }

// the five ribbons: where each starts round the body, how wide it is (an angle), how far out it sits, how fast it winds
// along the body and turns in time, how far its colour is shifted along the length's colours, and which bands of the
// spectrum light it (lows first)
struct RibbonDef { float theta, width, rho, wind, hue, gain, turn; int b0, b1; };
const RibbonDef kRib[] = { { 0.00f, 1.50f, 0.96f, 1.00f, -0.05f, 1.00f, 0.21f, 0, 2 },
                           { 1.85f, 1.10f, 0.78f, 1.22f,  0.09f, 0.90f, 0.29f, 2, 4 },
                           { 3.30f, 1.70f, 1.00f, 0.84f, -0.11f, 1.00f, 0.17f, 4, 7 },
                           { 4.55f, 1.00f, 0.62f, 1.38f,  0.13f, 0.85f, 0.34f, 6, 9 },
                           { 5.60f, 1.30f, 0.88f, 0.70f,  0.03f, 0.95f, 0.25f, 9, 11 } };
struct WispDef { float theta, rho, wind, hue, turn; };
const WispDef kWisp[] = { { 0.9f, 1.16f, 1.55f, 0.06f, 0.37f }, { 2.9f, 1.26f, 1.15f, -0.08f, 0.23f }, { 4.9f, 1.20f, 1.80f, 0.02f, 0.31f } };

// sparks that drift along the body and twinkle; bubbles that rise through the liquid
struct Spark { float u0, speed, theta, rho, rate, phase, size; bool star; };
struct Bubble { float x0, vx, y0, vy, rad, wobble, phase; };
struct Drifters
{
    std::vector<Spark> sparks; std::vector<Bubble> bubbles;
    Drifters()
    {
        juce::Random r (0x51ab);
        for (int i = 0; i < 64; ++i)
            sparks.push_back ({ r.nextFloat(), (0.006f + 0.02f * r.nextFloat()) * (r.nextBool() ? 1.0f : -0.6f), kTwoPi * r.nextFloat(), 0.15f + 1.05f * r.nextFloat(),
                                1.3f + 2.6f * r.nextFloat(), kTwoPi * r.nextFloat(), 0.6f + 0.9f * r.nextFloat(), i % 4 == 0 });
        for (int i = 0; i < 16; ++i)
            bubbles.push_back ({ r.nextFloat(), (r.nextFloat() - 0.5f) * 0.004f, r.nextFloat(), 0.018f + 0.035f * r.nextFloat(), 1.8f + 4.4f * r.nextFloat() * r.nextFloat(),
                                 0.5f + 0.8f * r.nextFloat(), kTwoPi * r.nextFloat() });
    }
};
const Drifters& drifters() { static const Drifters d; return d; }

static float sampleCol (const std::vector<float>& v, float u)
{
    const float x = juce::jlimit (0.0f, (float) (C - 1), u * (float) (C - 1));
    const int i = (int) x; const float f = x - (float) i;
    const float a = v[(size_t) i], b = v[(size_t) juce::jmin (C - 1, i + 1)];
    return a + f * (b - a);
}
}

// sizes, our copy of the liquid behind, the scaling tables: once per size / screen scale / page style
void SoundBody::prepare (float rs) const
{
    const int W = juce::jmax (1, getWidth()), H = juce::jmax (1, getHeight());
    const int w = (int) std::ceil ((float) W * rs), h = (int) std::ceil ((float) H * rs);
    if (frame.isValid() && w == ow && h == oh && std::abs (rs - scaleNow) < 1.0e-4f && narrow == narrowNow && tubeBox == tubeNow && ! staticDirty) return;
    ow = w; oh = h; scaleNow = rs; narrowNow = narrow; tubeNow = tubeBox; staticDirty = false; frameValid = softValid = false;
    frame = juce::Image (juce::Image::RGB, ow, oh, false);
    {
        // the liquid behind the living sound, exactly as the page draws it (so nothing shows where our picture ends)
        juce::Image back (juce::Image::RGB, ow, oh, false);
        {
            juce::Graphics bg (back);
            bg.fillAll (tubeInside);
            bg.addTransform (juce::AffineTransform::scale (rs));
            if (narrow || tubeBox.isEmpty()) art::tankLiquid (bg, juce::Rectangle<float> (0.0f, 0.0f, (float) W, (float) H));
            else art::tubeLiquid (bg, tubeBox);
        }
        juce::Image::BitmapData bd (back, juce::Image::BitmapData::readOnly);
        ps = bd.pixelStride; rowBytes = ow * ps;
        backPix.resize ((size_t) oh * (size_t) rowBytes);
        for (int y = 0; y < oh; ++y) std::memcpy (backPix.data() + (size_t) y * (size_t) rowBytes, bd.getLinePointer (y), (size_t) rowBytes);
    }
    // the light field's grid: sample f sits at (f - 0.5) * kCell px, with one sample of border all round
    iw = (int) std::ceil ((float) W / kCell) + 2; ih = (int) std::ceil ((float) H / kCell) + 2;
    light.assign ((size_t) (iw * ih * 3), 0.0f);
    coarse.assign ((size_t) (iw * ih * 3), 0);
    wide.assign ((size_t) ih * (size_t) rowBytes, 0);
    softPix.assign ((size_t) oh * (size_t) rowBytes, 0);
    cols.assign ((size_t) iw, Col());
    lineY.assign ((size_t) (kLines * iw), 0.0f); lineC.assign ((size_t) (kLines * iw * 3), 0.0f);
    veilLo.assign ((size_t) (kRibbons * iw), 0.0f); veilHi = veilLo; veilI.assign ((size_t) (kRibbons * iw * 4), 0.0f);
    xi.resize ((size_t) ow); xw.resize ((size_t) ow); yi.resize ((size_t) oh); yw.resize ((size_t) oh);
    auto grid = [rs] (int X, int limit, int& i0, int& wgt)
    {
        const int fp = (int) (((((float) X + 0.5f) / rs) / kCell + 0.5f) * 256.0f);
        i0 = juce::jlimit (0, limit - 2, fp >> 8); wgt = fp & 255;
    };
    for (int X = 0; X < ow; ++X) grid (X, iw, xi[(size_t) X], xw[(size_t) X]);
    for (int Y = 0; Y < oh; ++Y) grid (Y, ih, yi[(size_t) Y], yw[(size_t) Y]);
}

// step 1: per column of the grid - which moment of the sound is there, the outline, the colour, where every line runs
void SoundBody::buildColumns (const std::vector<float>& e, const std::vector<float>& bands, const std::vector<float>& flux, float alpha) const
{
    const auto bodyR = bodyRect();
    const float W = juce::jmax (1.0f, bodyR.getWidth()), H = (float) getHeight(), Hh = bodyR.getHeight() * 0.5f, cyc = bodyR.getCentreY();
    const float halfMax = juce::jmin (Hh * 0.78f, W * 0.22f), margin = 3.0f;
    // time, wrapped so it stays exact however long the window stays open
    auto phase = [this] (double rate) { return (float) std::fmod (t * rate, (double) kTwoPi); };
    const float p1 = phase (0.21), p2 = phase (0.33), p3 = phase (0.27), p4 = phase (0.30), p5 = phase (0.16), pBreath = phase (0.55), pShake = phase (40.0);
    const float tDrift = (float) std::fmod (t * 0.05, (double) Noise::N), tCore = (float) std::fmod (t * 0.3, (double) Noise::N), pFlow = phase (2.6);
    float ribTurn[kRibbons], wispTurn[kWisps];
    for (int r = 0; r < kRibbons; ++r) ribTurn[r] = phase ((double) kRib[r].turn);
    for (int k = 0; k < kWisps; ++k) wispTurn[k] = phase ((double) kWisp[k].turn);
    const float pinchScale = 1.0f - 0.55f * defPinch;                 // squeezed .. fattened
    const float twistTight = 1.0f - 0.28f * std::abs (defTwist);      // wrung out: thinner
    const float handU = grabU + defDrag;
    const float mergeK = mergeHover ? 1.0f : mergeFlash;
    const float hoverU = hoverGlow > 0.01f ? (hoverAt.x - bodyR.getX()) / W : -9.0f;
    const auto& rp = ramp(); const auto& nz = noise();
    // each line's place across its ribbon: the two edges and the fine lines between them
    float stepS[kRibbons], stepC[kRibbons];
    for (int r = 0; r < kRibbons; ++r) fastSinCos (kRib[r].width / (float) (kPerRibbon - 1), stepS[r], stepC[r]);
    const int coreLine = kLines - 1, wisp0 = kRibbons * kPerRibbon;
    for (int l = 0; l < kLines; ++l)
        lineW[l] = l == coreLine ? 1.5f : (l >= wisp0 ? 0.8f : ((l % kPerRibbon == 0 || l % kPerRibbon == kPerRibbon - 1) ? 1.25f : 0.65f));

    for (int i = 0; i < iw; ++i)
    {
        auto& c = cols[(size_t) i];
        const float px = ((float) i - 0.5f) * kCell;
        const float uu = (px - bodyR.getX()) / W;
        c.uu = uu; c.on = uu > 0.0f && uu < 1.0f;
        if (! c.on)
        {
            c.glow = c.top = c.bot = c.e = 0.0f; c.cy = cyc;
            for (int l = 0; l < kLines; ++l) { lineY[(size_t) (l * iw + i)] = cyc; auto* q = &lineC[(size_t) ((l * iw + i) * 3)]; q[0] = q[1] = q[2] = 0.0f; }
            for (int r = 0; r < kRibbons; ++r) veilI[(size_t) ((r * iw + i) * 4)] = 0.0f;
            continue;
        }
        // which moment of the sound is here: the hand drags the body's material sideways (what was under it when it
        // grabbed stays under it), and a PULL spreads the pulled part out
        float u = uu;
        if (std::abs (defDrag) > 1.0e-4f) { const float sg = 0.16f + 1.1f * std::abs (defDrag); u -= defDrag * fastExp (-sq ((uu - handU) / sg)); }
        if (std::abs (defPull) > 0.001f) { const float d = u - defPullC; u = defPullC + d / (1.0f + defPull * 1.6f * fastExp (-d * d * 18.0f)); }
        u = juce::jlimit (0.0f, 1.0f, u);
        c.u = u;
        // how loud, the spectrum's balance, how rough
        const float loud = juce::jlimit (0.0f, 1.0f, sampleCol (e, u));
        c.e = loud; c.flux = sampleCol (flux, u);
        const float cx = juce::jlimit (0.0f, (float) (C - 1), u * (float) (C - 1));
        const int c0 = (int) cx, c1 = juce::jmin (C - 1, c0 + 1);
        const float cf = cx - (float) c0;
        float band[B], lo = 0, hi = 0;
        for (int bb = 0; bb < B; ++bb)
        {
            band[bb] = bands[(size_t) (c0 * B + bb)] + cf * (bands[(size_t) (c1 * B + bb)] - bands[(size_t) (c0 * B + bb)]);
            (bb < B / 2 ? lo : hi) += band[bb];
        }
        lo /= (float) (B / 2); hi /= (float) (B / 2);
        // the outline: thick where the sound is loud, a thread where it is quiet, breathing; thin tails at both ends
        const float endPx = juce::jmin (uu, 1.0f - uu);
        const float taper = 0.3f + 0.7f * smooth (0.0f, 0.1f, endPx), fade = smooth (0.0f, 0.04f, endPx);
        const float thick = (0.2f + 0.8f * std::pow (loud, 0.65f)) * (1.0f + 0.07f * fastSin (kTwoPi * u * 2.6f + pBreath)) * taper;
        // wrung out, it is thinner, with a waist at every half turn
        const float waist = std::abs (defTwist) > 0.001f ? 1.0f - 0.34f * juce::jmin (1.0f, std::abs (defTwist) * 1.5f) * (0.5f - 0.5f * fastSin (defTwist * 4.0f * kTwoPi * (uu - 0.5f) + 1.5708f)) : 1.0f;
        const float half = halfMax * thick * pinchScale * twistTight * waist;
        c.top = half * (0.78f + 0.36f * lo + 0.08f * fastSin (kTwoPi * u * 2.3f + p3));
        c.bot = half * (0.78f + 0.36f * hi + 0.08f * fastSin (kTwoPi * u * 2.0f - p4 + 1.3f));
        // the spine: a slow travelling wave; the part you grabbed goes up / down with the hand; a shake rattles it
        float cy = cyc + Hh * (0.34f * fastSin (kTwoPi * u * 1.12f + p1) + 0.1f * fastSin (kTwoPi * u * 2.7f - p2 + 1.0f)) * (1.0f - 0.6f * thick) * taper;
        if (std::abs (defBend) > 1.0e-4f) cy += defBend * Hh * 0.85f * fastExp (-sq ((uu - handU) / 0.2f));
        if (defShake > 0.001f) cy += defShake * Hh * 0.2f * fastSin (pShake + uu * 23.0f);
        // pressed against the glass, it flattens (it never leaves the tube)
        cy = juce::jlimit (margin + 2.0f, H - margin - 2.0f, cy);
        c.top = juce::jmax (1.0f, juce::jmin (c.top, cy - margin)); c.bot = juce::jmax (1.0f, juce::jmin (c.bot, H - margin - cy));
        c.cy = cy;
        // how far round the ribbons have wound here: their own slow winding, plus two whole turns end to end at a full TWIST
        c.phi = kTwoPi * 0.85f * u + 0.45f * fastSin (kTwoPi * 1.3f * u + p5) + defTwist * 2.0f * kTwoPi * (uu - 0.5f);
        // brightness: the ends fade out; a pulse where the sound is playing; a lift under the hand
        float glow = alpha * fade;
        if (playShown >= 0.0f) glow *= 1.0f + 0.9f * fastExp (-sq ((uu - playShown) / 0.02f));
        if (touchGlow > 0.01f) glow *= 1.0f + 0.5f * touchGlow * fastExp (-sq ((uu - handU) / 0.1f));
        if (hoverU > -1.0f) glow *= 1.0f + 0.3f * hoverGlow * fastExp (-sq ((uu - hoverU) / 0.07f));
        // colour: along the sound; frozen parts go ice-blue; breeding washes it green
        const float iceK = freezeGlow > 0.02f && freezeX >= 0.0f ? 0.85f * freezeGlow * fastExp (-sq ((px - freezeX) / 34.0f)) : 0.0f;
        const float breedK = mergeK > 0.02f ? 0.45f * mergeK : 0.0f;
        glow *= (1.0f + 0.6f * iceK) * (1.0f + 1.1f * breedK);
        struct Rgb { float r, g, b; };
        auto colourAt = [&rp, iceK, breedK] (float where)
        {
            const float* k = rp.at (where);
            Rgb o { k[0], k[1], k[2] };
            o.r += (0.81f - o.r) * iceK; o.g += (0.91f - o.g) * iceK; o.b += (1.0f - o.b) * iceK;
            o.r += (0.65f - o.r) * breedK; o.g += (0.95f - o.g) * breedK; o.b += (0.23f - o.b) * breedK;
            return o;
        };
        const auto own = colourAt (u);
        const float cr = own.r, cg = own.g, cb = own.b;
        c.r = cr; c.g = cg; c.b = cb; c.glow = glow;

        // ---- the lines: each ribbon's two edges and the fine lines between them, wound round the body
        const float bright = glow * (0.36f + 0.64f * loud);
        const bool shaking = defShake > 0.001f;
        for (int r = 0; r < kRibbons; ++r)
        {
            const auto& rb = kRib[r];
            float grp = 0; for (int bb = rb.b0; bb <= rb.b1; ++bb) grp += band[bb];
            grp /= (float) (rb.b1 - rb.b0 + 1);
            float lit = bright * rb.gain * (0.32f + 0.85f * grp);                                  // its part of the spectrum lights it
            lit *= 0.8f + 0.3f * fastSin (kTwoPi * (u * 4.0f + (float) r * 0.37f) - pFlow);         // light flows along it, toward the sound's end
            const float a = rb.theta + rb.wind * c.phi + ribTurn[r] + 1.1f * (nz.at (u * 2.3f + (float) r * 7.1f, tDrift + (float) r * 3.3f) - 0.5f);
            const auto rc = colourAt (u + rb.hue);
            float s, z; fastSinCos (a - rb.width * 0.5f, s, z);
            const float sA = s, zA = z;
            for (int k = 0; k < kPerRibbon; ++k)
            {
                const int l = r * kPerRibbon + k;
                const bool edge = k == 0 || k == kPerRibbon - 1;
                float y = cy + rb.rho * (s < 0.0f ? c.top : c.bot) * s;
                if (shaking) y += defShake * Hh * 0.13f * fastSin (pShake * (1.0f + 0.13f * (float) l) + uu * 29.0f + (float) l * 1.7f);
                lineY[(size_t) (l * iw + i)] = juce::jlimit (margin, H - margin, y);
                const float depth = 0.5f + 0.5f * z;                                                 // 1 = the near side, 0 = the far side
                const float inten = 255.0f * lit * (0.26f + 0.74f * depth) * (edge ? 1.0f : 0.36f), white = (edge ? 0.34f : 0.1f) * depth;
                auto* q = &lineC[(size_t) ((l * iw + i) * 3)];
                q[0] = inten * (rc.b + (1.0f - rc.b) * white); q[1] = inten * (rc.g + (1.0f - rc.g) * white); q[2] = inten * (rc.r + (1.0f - rc.r) * white);
                const float s2 = s * stepC[r] + z * stepS[r]; z = z * stepC[r] - s * stepS[r]; s = s2;   // on to the next line across the ribbon
            }
            // the veil between its edges (as seen from here: over the body's top / bottom when the ribbon passes there)
            float sB, zB; fastSinCos (a + rb.width * 0.5f, sB, zB);
            const float sMax = (zA > 0.0f && zB < 0.0f) ? 1.0f : juce::jmax (sA, sB), sMin = (zA < 0.0f && zB > 0.0f) ? -1.0f : juce::jmin (sA, sB);
            const float yLo = cy + rb.rho * (sMin < 0.0f ? c.top : c.bot) * sMin, yHi = cy + rb.rho * (sMax < 0.0f ? c.top : c.bot) * sMax;
            veilLo[(size_t) (r * iw + i)] = yLo; veilHi[(size_t) (r * iw + i)] = yHi;
            const float full = rb.rho * 0.5f * (c.top + c.bot) * rb.width;                           // the ribbon's real width
            const float edgeOn = juce::jlimit (1.0f, 2.2f, full / juce::jmax (1.0f, yHi - yLo));       // seen edge-on, the same light in less room
            const float v = lit * 0.95f * (0.5f + 0.25f * (zA + zB + 2.0f) * 0.5f) * edgeOn;
            auto* vq = &veilI[(size_t) ((r * iw + i) * 4)];
            vq[0] = v; vq[1] = rc.b; vq[2] = rc.g; vq[3] = rc.r;
        }
        for (int k = 0; k < kWisps; ++k)
        {
            const auto& wd = kWisp[k];
            const int l = wisp0 + k;
            float s, z; fastSinCos (wd.theta + wd.wind * c.phi + wispTurn[k], s, z);
            float y = cy + (wd.rho + 0.14f * fastSin (kTwoPi * u * 3.1f + wispTurn[k])) * (s < 0.0f ? c.top : c.bot) * s;
            if (shaking) y += defShake * Hh * 0.2f * fastSin (pShake * (1.0f + 0.13f * (float) l) + uu * 31.0f + (float) l);
            lineY[(size_t) (l * iw + i)] = juce::jlimit (margin, H - margin, y);
            const auto wc = colourAt (u + wd.hue);
            const float inten = 255.0f * bright * 0.5f * (0.2f + 0.8f * (0.5f + 0.5f * z));
            auto* q = &lineC[(size_t) ((l * iw + i) * 3)];
            q[0] = inten * (wc.b + (1.0f - wc.b) * 0.2f); q[1] = inten * (wc.g + (1.0f - wc.g) * 0.2f); q[2] = inten * (wc.r + (1.0f - wc.r) * 0.2f);
        }
        {
            // the core: white-hot where the sound is loud
            lineY[(size_t) (coreLine * iw + i)] = cy;
            const float beads = 0.5f + 1.0f * nz.at (u * 9.0f + 64.0f - tCore, 3.5f);                    // brighter knots travelling along it
            const float inten = 255.0f * glow * (0.1f + 1.0f * loud * loud) * beads;
            auto* q = &lineC[(size_t) ((coreLine * iw + i) * 3)];
            q[0] = inten * (cb + (1.0f - cb) * 0.6f); q[1] = inten * (cg + (1.0f - cg) * 0.6f); q[2] = inten * (cr + (1.0f - cr) * 0.6f);
        }
    }
}

// step 2: the soft light on the coarse grid - the glow round the body, the haze in it, its skin, its core, the
// ribbons' veils, a halo round every line, the light under the hand
void SoundBody::buildLight() const
{
    const float H = (float) getHeight();
    std::fill (light.begin(), light.end(), 0.0f);
    const auto& nz = noise();
    const float tA = (float) std::fmod (t * 0.07, (double) Noise::N), tB = (float) std::fmod (t * 0.03, (double) Noise::N);
    // the glow outside the outline falls off in two steps (a tight one, a wide faint one), reaching nothing at auraR
    const float R1 = juce::jmax (6.0f, H * 0.065f), R2 = juce::jmax (18.0f, H * 0.2f), auraR = 2.4f * R2, auraK = 63.0f / auraR;
    float auraLut[64], rimLut[64], coreLut[64];
    {
        const float end = 0.7f * std::exp (-auraR / R1) + 0.3f * std::exp (-auraR / R2);
        for (int k = 0; k < 64; ++k)
        {
            const float d = (float) k / auraK, ad = (float) k / 63.0f;
            auraLut[k] = (0.7f * std::exp (-d / R1) + 0.3f * std::exp (-d / R2) - end) / (1.0f - end);
            rimLut[k] = std::exp (-sq ((ad - 0.9f) / 0.1f));
            coreLut[k] = std::exp (-sq (ad / 0.17f));
        }
    }
    for (int i = 0; i < iw; ++i)
    {
        const auto& c = cols[(size_t) i];
        if (! c.on || c.glow < 0.004f) continue;
        const float invTop = 1.0f / c.top, invBot = 1.0f / c.bot;
        const float lift = c.glow * (0.3f + 0.7f * c.e);
        const float au = 0.24f * lift, hz = 0.42f * lift, rm = 0.2f * lift, core = 0.5f * c.glow * c.e * c.e;
        const int j0 = juce::jmax (0, (int) ((c.cy - c.top - auraR) / kCell + 0.5f)), j1 = juce::jmin (ih - 1, (int) ((c.cy + c.bot + auraR) / kCell + 1.5f));
        const float nx = c.u * 7.0f + 64.0f - tA;
        float* L = &light[(size_t) ((j0 * iw + i) * 3)];
        for (int j = j0; j <= j1; ++j, L += iw * 3)
        {
            const float dy = ((float) j - 0.5f) * kCell - c.cy;
            const float ady = std::abs (dy), h = dy < 0.0f ? c.top : c.bot, out = ady - h;
            float f, w = 0.0f;
            if (out >= 0.0f) f = au * auraLut[juce::jmin (63, (int) (out * auraK))];
            else
            {
                const float ad = ady * (dy < 0.0f ? invTop : invBot);
                const int k = (int) (ad * 63.0f);
                const float n = nz.at (nx, dy * (dy < 0.0f ? invTop : invBot) * 1.3f + 8.0f + tB);
                f = au + hz * (1.0f - ad * ad) * (0.4f + 1.2f * n) + rm * rimLut[k];
                w = core * coreLut[k];
            }
            L[0] += c.b * f + w; L[1] += c.g * f + w; L[2] += c.r * f + w;
        }
    }
    // the ribbons' veils: light between each ribbon's edges, brightest toward the edges (silk seen against the light)
    for (int r = 0; r < kRibbons; ++r)
        for (int i = 0; i < iw; ++i)
        {
            const float* vq = &veilI[(size_t) ((r * iw + i) * 4)];
            if (vq[0] < 0.004f) continue;
            const float rl = veilLo[(size_t) (r * iw + i)] / kCell + 0.5f, rh = veilHi[(size_t) (r * iw + i)] / kCell + 0.5f;
            const float span = juce::jmax (0.001f, rh - rl), inv = 1.0f / span;
            const int j0 = juce::jmax (0, (int) (rl - 0.5f)), j1 = juce::jmin (ih - 1, (int) (rh + 1.5f));
            for (int j = j0; j <= j1; ++j)
            {
                const float cov = juce::jmin ((float) j + 0.5f, rh) - juce::jmax ((float) j - 0.5f, rl);
                if (cov <= 0.0f) continue;
                const float sAcross = juce::jlimit (0.0f, 1.0f, ((float) j - rl) * inv);
                const float f = vq[0] * juce::jmin (1.0f, cov) * (0.34f + 0.66f * sq (2.0f * sAcross - 1.0f));
                float* L = &light[(size_t) ((j * iw + i) * 3)];
                L[0] += vq[1] * f; L[1] += vq[2] * f; L[2] += vq[3] * f;
            }
        }
    // a soft halo round every line (the sharp line itself is drawn later, at full resolution)
    for (int l = 0; l < kLines; ++l)
    {
        const float gain = (l == kLines - 1 ? 0.75f : 0.42f) / 255.0f;
        for (int i = 0; i < iw; ++i)
        {
            const float* q = &lineC[(size_t) ((l * iw + i) * 3)];
            if (q[0] + q[1] + q[2] < 8.0f) continue;
            const float row = lineY[(size_t) (l * iw + i)] / kCell + 0.5f;
            const int j = (int) row;
            if (j < 0 || j + 1 >= ih) continue;
            const float f1 = (row - (float) j) * gain, f0 = gain - f1;
            float* L = &light[(size_t) ((j * iw + i) * 3)];
            L[0] += q[0] * f0; L[1] += q[1] * f0; L[2] += q[2] * f0;
            L += iw * 3;
            L[0] += q[0] * f1; L[1] += q[1] * f1; L[2] += q[2] * f1;
        }
    }
    // the light under the hand (and a fainter one under a hovering pointer)
    auto spot = [this, H] (juce::Point<float> at, float amount, float size)
    {
        if (amount < 0.01f) return;
        const float sg = H * size, inv = 1.0f / (2.0f * sg * sg);
        const int i0 = juce::jmax (0, (int) ((at.x - 2.6f * sg) / kCell)), i1 = juce::jmin (iw - 1, (int) ((at.x + 2.6f * sg) / kCell) + 2);
        const int j0 = juce::jmax (0, (int) ((at.y - 2.6f * sg) / kCell)), j1 = juce::jmin (ih - 1, (int) ((at.y + 2.6f * sg) / kCell) + 2);
        for (int j = j0; j <= j1; ++j)
            for (int i = i0; i <= i1; ++i)
            {
                const float f = amount * fastExp (-(sq (((float) i - 0.5f) * kCell - at.x) + sq (((float) j - 0.5f) * kCell - at.y)) * inv);
                float* L = &light[(size_t) ((j * iw + i) * 3)];
                L[0] += f; L[1] += 0.9f * f; L[2] += 0.92f * f;
            }
    };
    spot (handAt, 0.32f * touchGlow, 0.2f);
    spot (hoverAt, 0.12f * hoverGlow, 0.16f);
    // to pixel values; the light dies away just before the glass above and below
    const auto& tn = tone();
    for (int j = 0; j < ih; ++j)
    {
        const float py = ((float) j - 0.5f) * kCell;
        const float fd = smooth (1.0f, 10.0f, py) * smooth (1.0f, 10.0f, H - py);
        const float* L = &light[(size_t) (j * iw * 3)];
        auto* q = &coarse[(size_t) (j * iw * 3)];
        for (int i = 0; i < iw; ++i, L += 3, q += 3)
        {
            const float m = juce::jmax (L[0], L[1], L[2]) * fd;
            if (m < 0.004f) { q[0] = q[1] = q[2] = 0; continue; }
            const int k = juce::jmin (1023, (int) (m * 256.0f));
            const float g = tn.gain[k] * fd, w = tn.white[k];
            const float b0 = L[0] * g, g0 = L[1] * g, r0 = L[2] * g;
            q[0] = (juce::uint8) juce::jmin (255.0f, b0 + w * (1.0f - b0 * (1.0f / 255.0f)));
            q[1] = (juce::uint8) juce::jmin (255.0f, g0 + w * (1.0f - g0 * (1.0f / 255.0f)));
            q[2] = (juce::uint8) juce::jmin (255.0f, r0 + w * (1.0f - r0 * (1.0f / 255.0f)));
        }
    }
}

// one glowing line into the finished frame: light added, anti-aliased, a grid cell at a time. `y` in px (before the
// screen scale), `c` its colour x brightness (b, g, r, 0..255 and over) at each grid column. Inside a cell everything
// is whole-number arithmetic (16.16 fixed point), stepped a column at a time: this is the hottest loop of the picture.
namespace
{
inline void addLight (juce::uint8* q, int b, int g, int r, int cov)   // cov 0..256
{
    int v = q[0] + ((b * cov) >> 8); q[0] = (juce::uint8) (v > 255 ? 255 : v);
    v = q[1] + ((g * cov) >> 8); q[1] = (juce::uint8) (v > 255 ? 255 : v);
    v = q[2] + ((r * cov) >> 8); q[2] = (juce::uint8) (v > 255 ? 255 : v);
}
}

void SoundBody::drawLine (juce::uint8* pix, int stride, const float* y, const float* c, float width) const
{
    const float rs = scaleNow, cell = kCell * rs, dt = 1.0f / cell, wpx = width * rs;
    const int lastRow = oh - 1;
    for (int i = 0; i + 1 < iw; ++i)
    {
        const float* ca = c + i * 3; const float* cb = ca + 3;
        if (ca[0] + ca[1] + ca[2] + cb[0] + cb[1] + cb[2] < 56.0f) continue;   // too dim to see over the glow it runs through
        const float xa = ((float) i - 0.5f) * cell;
        const int X0 = juce::jmax (0, (int) std::ceil (xa - 0.5f)), X1 = juce::jmin (ow, (int) std::ceil (xa + cell - 0.5f));
        if (X1 <= X0) continue;
        const float ya = y[i] * rs, yb = y[i + 1] * rs, slope = (yb - ya) * dt;
        // a column of the line covers `span` rows (its thickness seen upright, plus how far it climbs in one column);
        // its light is shared over them, so a steep stretch is as bright as a flat one
        const float thick = wpx * std::sqrt (1.0f + slope * slope), span = std::abs (slope) + thick;
        const int share = (int) (256.0f * thick / span);
        const float f0 = ((float) X0 + 0.5f - xa) * dt;
        int top = (int) ((ya + (yb - ya) * f0 - 0.5f * span) * 65536.0f);
        const int dTop = (int) (slope * 65536.0f), spanF = (int) (span * 65536.0f);
        int b = (int) ((ca[0] + (cb[0] - ca[0]) * f0) * 65536.0f), g = (int) ((ca[1] + (cb[1] - ca[1]) * f0) * 65536.0f), r = (int) ((ca[2] + (cb[2] - ca[2]) * f0) * 65536.0f);
        const int db = (int) ((cb[0] - ca[0]) * dt * 65536.0f), dg = (int) ((cb[1] - ca[1]) * dt * 65536.0f), dr = (int) ((cb[2] - ca[2]) * dt * 65536.0f);
        auto* col = pix + (size_t) (X0 * ps);
        for (int X = X0; X < X1; ++X, top += dTop, b += db, g += dg, r += dr, col += ps)
        {
            const int bot = top + spanF, jt = top >> 16, jb = bot >> 16;
            if (jb < 0 || jt > lastRow) continue;
            const int cb8 = b >> 16, cg8 = g >> 16, cr8 = r >> 16;
            if (jt == jb) { addLight (col + (size_t) jt * (size_t) stride, cb8, cg8, cr8, ((spanF >> 8) * share) >> 8); continue; }
            auto* q = col + (size_t) juce::jmax (0, jt) * (size_t) stride;
            if (jt >= 0) { addLight (q, cb8, cg8, cr8, (((((jt + 1) << 16) - top) >> 8) * share) >> 8); q += stride; }
            for (int j = juce::jmax (0, jt + 1); j < juce::jmin (jb, oh); ++j, q += stride) addLight (q, cb8, cg8, cr8, share);
            if (jb <= lastRow) addLight (q, cb8, cg8, cr8, (((bot - (jb << 16)) >> 8) * share) >> 8);
        }
    }
}

// a soft dot of light (x, y, rad in frame pixels; colour x brightness 0..255 and over)
void SoundBody::addDot (juce::uint8* pix, int stride, float x, float y, float rad, float b, float g, float r) const
{
    const int x0 = juce::jmax (0, (int) (x - rad)), x1 = juce::jmin (ow - 1, (int) (x + rad) + 1);
    const int y0 = juce::jmax (0, (int) (y - rad)), y1 = juce::jmin (oh - 1, (int) (y + rad) + 1);
    const float inv = 1.0f / (rad * rad);
    const int ib = (int) b, ig = (int) g, ir = (int) r;
    for (int j = y0; j <= y1; ++j)
        for (int i = x0; i <= x1; ++i)
        {
            const float d = 1.0f - (sq ((float) i + 0.5f - x) + sq ((float) j + 0.5f - y)) * inv;
            if (d <= 0.0f) continue;
            const int cov = (int) (256.0f * d * d);
            auto* q = pix + (size_t) j * (size_t) stride + (size_t) (i * ps);
            int v = q[0] + ((ib * cov) >> 8); q[0] = (juce::uint8) (v > 255 ? 255 : v);
            v = q[1] + ((ig * cov) >> 8); q[1] = (juce::uint8) (v > 255 ? 255 : v);
            v = q[2] + ((ir * cov) >> 8); q[2] = (juce::uint8) (v > 255 ? 255 : v);
        }
}

// sparks: they ride the ribbons' winding, drift along the body and twinkle; loud, rough moments and a shake throw more
void SoundBody::drawSparks (juce::uint8* pix, int stride) const
{
    const auto bodyR = bodyRect();
    const float rs = scaleNow;
    const auto& rp = ramp();
    const auto& all = drifters().sparks;
    const int n = juce::jmin (kSparks, (int) all.size());
    for (int k = 0; k < n; ++k)
    {
        const auto& sp = all[(size_t) k];
        const float uu = (float) std::fmod ((double) sp.u0 + t * (double) sp.speed + 1000.0, 1.0);
        const float fx = (bodyR.getX() + uu * bodyR.getWidth()) / kCell + 0.5f;
        const int i = juce::jlimit (0, iw - 2, (int) fx);
        const float fr = juce::jlimit (0.0f, 1.0f, fx - (float) i);
        const auto& a = cols[(size_t) i]; const auto& b = cols[(size_t) i + 1];
        if (! a.on || ! b.on) continue;
        const float glow = a.glow + (b.glow - a.glow) * fr, loud = a.e + (b.e - a.e) * fr, rough = a.flux + (b.flux - a.flux) * fr;
        float tw = fastSin (sp.phase + (float) std::fmod (t * (double) sp.rate, (double) kTwoPi));
        tw = tw > 0.0f ? tw * tw * tw : 0.0f;
        float s, z; fastSinCos (sp.theta + (a.phi + (b.phi - a.phi) * fr) * 1.1f, s, z);
        const float lit = glow * (0.22f + 0.78f * loud) * (0.35f + 0.65f * (0.5f + 0.5f * z)) * tw * (1.0f + 1.6f * rough + 2.0f * defShake);
        if (lit < 0.03f) continue;
        const float cy = a.cy + (b.cy - a.cy) * fr, h = s < 0.0f ? a.top + (b.top - a.top) * fr : a.bot + (b.bot - a.bot) * fr;
        const float x = (bodyR.getX() + uu * bodyR.getWidth()) * rs, y = (cy + sp.rho * h * s) * rs;
        const float* c = rp.at (a.u + (b.u - a.u) * fr + 0.1f * (sp.rho - 0.6f));
        const float v = 255.0f * juce::jmin (1.6f, lit * 1.5f);
        const float cb = v * (c[2] + (1.0f - c[2]) * 0.6f), cg = v * (c[1] + (1.0f - c[1]) * 0.6f), cr = v * (c[0] + (1.0f - c[0]) * 0.6f);
        addDot (pix, stride, x, y, (1.0f + sp.size) * rs, cb, cg, cr);
        if (sp.star && tw > 0.25f)
        {
            // a glint: four fine rays
            const float len = (2.5f + 3.5f * sp.size) * rs * tw;
            for (float d = 1.0f; d < len; d += 1.0f)
            {
                const float k2 = 0.55f * (1.0f - d / len);
                for (auto o : { juce::Point<float> (d, 0.0f), juce::Point<float> (-d, 0.0f), juce::Point<float> (0.0f, d), juce::Point<float> (0.0f, -d) })
                    addDot (pix, stride, x + o.x, y + o.y, 0.75f * rs, cb * k2, cg * k2, cr * k2);
            }
        }
    }
}

// bubbles rising through the liquid: a thin bright ring (brightest low on the right), a glint top left
void SoundBody::drawBubbles (juce::uint8* pix, int stride) const
{
    const float rs = scaleNow, W = (float) getWidth(), H = (float) getHeight();
    const auto& rp = ramp();
    const auto& all = drifters().bubbles;
    const int n = juce::jmin (narrow ? 6 : kBubbles, (int) all.size());
    for (int k = 0; k < n; ++k)
    {
        const auto& bb = all[(size_t) k];
        const float rise = (float) std::fmod ((double) bb.y0 + t * (double) bb.vy, 1.0);
        const float xf = (float) std::fmod ((double) bb.x0 + t * (double) bb.vx + 1000.0, 1.0);
        const float rad = bb.rad * rs * (0.8f + 0.2f * rise);
        const float x = (0.03f * W + 0.94f * W * xf + 3.0f * fastSin (bb.phase + (float) std::fmod (t * (double) bb.wobble, (double) kTwoPi))) * rs;
        // (it forms just inside the glass below and is gone just inside the glass above: never cut by the wall)
        const float y = (H - bb.rad - 4.0f) * rs - (H - 2.0f * bb.rad - 8.0f) * rs * rise;
        const float alpha = smooth (0.0f, 0.14f, rise) * smooth (0.0f, 0.14f, 1.0f - rise) * 0.62f;
        if (alpha < 0.02f) continue;
        const float* c = rp.at (xf);
        const float cb = 255.0f * alpha * (c[2] + (1.0f - c[2]) * 0.55f), cg = 255.0f * alpha * (c[1] + (1.0f - c[1]) * 0.55f), cr = 255.0f * alpha * (c[0] + (1.0f - c[0]) * 0.55f);
        const int x0 = juce::jmax (0, (int) (x - rad - 2.0f)), x1 = juce::jmin (ow - 1, (int) (x + rad + 2.0f));
        const int y0 = juce::jmax (0, (int) (y - rad - 2.0f)), y1 = juce::jmin (oh - 1, (int) (y + rad + 2.0f));
        const float lineHalf = juce::jmax (0.7f, 0.11f * rad + 0.35f * rs);
        for (int j = y0; j <= y1; ++j)
            for (int i = x0; i <= x1; ++i)
            {
                const float dx = (float) i + 0.5f - x, dy = (float) j + 0.5f - y, d = std::sqrt (dx * dx + dy * dy);
                float f = juce::jlimit (0.0f, 1.0f, 1.0f - (std::abs (d - rad) - lineHalf)) * (0.5f + 0.5f * juce::jlimit (0.0f, 1.0f, 0.5f + (dx + dy) / (2.83f * rad)));
                if (d < rad) f += 0.07f + 0.16f * sq (d / rad);                       // its faint body, a little brighter toward the rim
                if (f <= 0.0f) continue;
                const int cov = (int) (256.0f * f);
                auto* q = pix + (size_t) j * (size_t) stride + (size_t) (i * ps);
                int v = q[0] + (((int) cb * cov) >> 8); q[0] = (juce::uint8) (v > 255 ? 255 : v);
                v = q[1] + (((int) cg * cov) >> 8); q[1] = (juce::uint8) (v > 255 ? 255 : v);
                v = q[2] + (((int) cr * cov) >> 8); q[2] = (juce::uint8) (v > 255 ? 255 : v);
            }
        addDot (pix, stride, x - 0.42f * rad, y - 0.45f * rad, juce::jmax (0.9f * rs, 0.34f * rad), 300.0f * alpha, 300.0f * alpha, 300.0f * alpha);
    }
}

// step 2 (the rest) and step 3: the coarse light scaled up over the liquid, then the sharp parts
namespace
{
// one finished row: the liquid behind plus the light (two coarse rows blended). Plain bytes, so it compiles to vector code.
inline void lightRow (juce::uint8* dst, const juce::uint8* back, const juce::uint8* a, const juce::uint8* b, int wgt, int n)
{
    const juce::uint16 wb = (juce::uint16) wgt, wa = (juce::uint16) (256 - wgt);
    for (int k = 0; k < n; ++k)
    {
        const juce::uint16 l = (juce::uint16) ((juce::uint16) (a[k] * wa + b[k] * wb) >> 8);
        const juce::uint16 v = (juce::uint16) (back[k] + l);
        dst[k] = (juce::uint8) (v > 255 ? 255 : v);
    }
}
}

void SoundBody::composeSoft() const
{
    // each coarse row widened to the frame's width once ...
    for (int j = 0; j < ih; ++j)
    {
        const auto* src = &coarse[(size_t) (j * iw * 3)];
        auto* out = &wide[(size_t) j * (size_t) rowBytes];
        bool any = false;
        for (int k = 0; k < iw * 3 && ! any; ++k) any = src[k] != 0;
        if (! any) { std::memset (out, 0, (size_t) rowBytes); continue; }
        // (a run of pixels between the same two samples shares them: only the weight changes)
        int at = -1, b0 = 0, g0 = 0, r0 = 0, db = 0, dg = 0, dr = 0;
        for (int X = 0; X < ow; ++X, out += ps)
        {
            if (xi[(size_t) X] != at)
            {
                at = xi[(size_t) X];
                const auto* p = src + at * 3;
                b0 = p[0]; g0 = p[1]; r0 = p[2]; db = p[3] - b0; dg = p[4] - g0; dr = p[5] - r0;
            }
            const int w1 = xw[(size_t) X];
            out[0] = (juce::uint8) (b0 + ((db * w1) >> 8));
            out[1] = (juce::uint8) (g0 + ((dg * w1) >> 8));
            out[2] = (juce::uint8) (r0 + ((dr * w1) >> 8));
        }
    }
    // ... then every row of the picture is the liquid plus two of them blended
    for (int Y = 0; Y < oh; ++Y)
        lightRow (softPix.data() + (size_t) Y * (size_t) rowBytes, backPix.data() + (size_t) Y * (size_t) rowBytes, &wide[(size_t) yi[(size_t) Y] * (size_t) rowBytes],
                  &wide[(size_t) (yi[(size_t) Y] + 1) * (size_t) rowBytes], yw[(size_t) Y], rowBytes);
}

// step 3: the sharp parts, over a fresh copy of the soft picture
void SoundBody::composeSharp() const
{
    juce::Image::BitmapData dst (frame, juce::Image::BitmapData::writeOnly);
    for (int Y = 0; Y < oh; ++Y) std::memcpy (dst.getLinePointer (Y), softPix.data() + (size_t) Y * (size_t) rowBytes, (size_t) rowBytes);
    auto* pix = dst.getLinePointer (0);
    drawBubbles (pix, dst.lineStride);
    for (int l = 0; l < kLines; ++l) drawLine (pix, dst.lineStride, &lineY[(size_t) (l * iw)], &lineC[(size_t) (l * iw * 3)], lineW[l]);
    drawSparks (pix, dst.lineStride);
}

void SoundBody::renderFrame (const std::vector<float>& e, const std::vector<float>& bands, const std::vector<float>& flux, float alpha, bool freshSoft) const
{
    buildColumns (e, bands, flux, alpha);
    if (freshSoft) { buildLight(); composeSoft(); }
    composeSharp();
}

// the drop zone, as on Brandon's mockup: a dashed box in the middle of the glass, the arrow in a tab on its top edge
void SoundBody::drawEmptyHint (juce::Graphics& g, juce::Rectangle<float> r) const
{
    const bool tab = r.getHeight() >= 124.0f;
    auto box = juce::Rectangle<float> (juce::jmin (r.getWidth() - 24.0f, 420.0f), juce::jmin (r.getHeight() - (tab ? 44.0f : 16.0f), 96.0f))
                   .withCentre ({ r.getCentreX(), r.getCentreY() + (tab ? 9.0f : 0.0f) });
    const auto line = fileHover ? green : text;
    g.setColour (night0.withAlpha (0.5f)); g.fillRoundedRectangle (box, 12.0f);
    auto tabR = juce::Rectangle<float> (46.0f, 34.0f).withCentre ({ box.getCentreX(), box.getY() });
    {
        juce::Graphics::ScopedSaveState ss (g);
        if (tab) g.excludeClipRegion (tabR.expanded (4.0f, 0.0f).toNearestInt());
        juce::Path p; p.addRoundedRectangle (box, 12.0f);
        const float dl[] = { 6.0f, 4.0f };
        juce::Path d; juce::PathStrokeType (1.3f).createDashedStroke (d, p, dl, 2);
        g.setColour (line.withAlpha (fileHover ? 0.95f : 0.5f)); g.fillPath (d);
    }
    const bool roomForIcon = tab || box.getHeight() >= 90.0f;
    if (tab)
    {
        g.setColour (night0.withAlpha (0.8f)); g.fillRoundedRectangle (tabR, 9.0f);
        g.setColour (line.withAlpha (fileHover ? 0.95f : 0.4f)); g.drawRoundedRectangle (tabR, 9.0f, 1.2f);
        icon::save (g, juce::Rectangle<float> (20, 20).withCentre (tabR.getCentre()), line.withAlpha (0.95f));
    }
    else if (roomForIcon) icon::save (g, juce::Rectangle<float> (22, 22).withCentre ({ box.getCentreX(), box.getY() + 22 }), line.withAlpha (0.9f));
    const float mid = box.getCentreY() + (tab ? 6.0f : 0.0f);
    g.setColour (text.withAlpha (0.97f)); g.setFont (font (17.0f, 1, 0.12f));
    g.drawText ("DROP A SOUND HERE", box.withHeight (24).withCentre ({ box.getCentreX(), mid + (tab ? -12.0f : (roomForIcon ? 8.0f : -8.0f)) }), juce::Justification::centred);
    g.setColour (sub.brighter (0.3f)); g.setFont (font (13.0f));
    g.drawFittedText ("or click to choose one. Drop two to breed them.",
                      box.withHeight (20).withCentre ({ box.getCentreX(), mid + (tab ? 13.0f : (roomForIcon ? 32.0f : 16.0f)) }).reduced (10.0f, 0.0f).toNearestInt(),
                      juce::Justification::centred, 1, 0.8f);
}

void SoundBody::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    // drawn at the screen's own pixels up to 2x (a Retina screen shows every fine line), never coarser than 1x - and
    // never more than about 900,000 pixels a frame, however big the window (then it is drawn a little smaller and scaled)
    const float room = std::sqrt (900000.0f / (float) juce::jmax (1, getWidth() * getHeight()));
    const float rs = juce::jmax (1.0f, std::floor (juce::jmin (2.0f, room, g.getInternalContext().getPhysicalPixelScaleFactor() + 0.06f) * 8.0f) / 8.0f);
    prepare (rs);
    // a new frame only when time has moved on (the timer decides how often: see wantsFrame); otherwise the finished
    // one is shown again. The soft light under the sharp lines is redone every other new frame.
    if (! frameValid || shownStamp != stamp)
    {
        const bool freshSoft = ! softValid || (++softNo & 1);
        softValid = true; shownStamp = stamp;
        if (empty)
        {
            // a sleeping body: gentle, slow, waiting for a sound
            static thread_local std::vector<float> e, b, f;
            e.resize (C); b.resize ((size_t) C * B); f.assign (C, 0.0f);
            for (int i = 0; i < C; ++i)
            {
                const float u = (float) i / (float) (C - 1);
                e[(size_t) i] = 0.5f + 0.24f * std::sin ((float) t * 0.4f + u * 9.0f) + 0.1f * std::sin ((float) t * 0.23f - u * 17.0f);
                for (int k = 0; k < B; ++k) b[(size_t) (i * B + k)] = 0.42f + 0.2f * std::sin (u * 7.0f + (float) k * 1.3f + (float) t * 0.2f);
            }
            renderFrame (e, b, f, 0.72f, freshSoft);
        }
        else renderFrame (dispE, dispB, dispF, busy ? 0.78f + 0.22f * std::sin ((float) std::fmod (t * 5.0, (double) kTwoPi)) : 1.0f, freshSoft);
        frameValid = true;
    }
    {
        juce::Graphics::ScopedSaveState ss (g);
        if (narrow) { juce::Path clip; clip.addRoundedRectangle (r.reduced (1.0f), 13.5f); g.reduceClipRegion (clip); }
        g.setOpacity (1.0f);
        if (rs < 1.001f) g.drawImageAt (frame, 0, 0);
        else g.drawImageTransformed (frame, juce::AffineTransform::scale (1.0f / rs));
    }
    if (fileHover)
    {
        g.setColour (green.withAlpha (0.08f)); g.fillRoundedRectangle (r.reduced (2), narrow ? 13.0f : 6.0f);
        g.setColour (green.withAlpha (0.6f)); g.drawRoundedRectangle (r.reduced (2), narrow ? 13.0f : 6.0f, 1.5f);
    }
    if (empty) { drawEmptyHint (g, r); return; }
    const auto bodyR = bodyRect();
    if (mergeHover || mergeFlash > 0.05f)
    {
        g.setColour (text.withAlpha (0.9f * (mergeHover ? 1.0f : mergeFlash))); g.setFont (font (14.0f, 1, 0.14f));
        g.drawText ("DROP TO BREED", r.withTop (r.getBottom() - 28.0f), juce::Justification::centred);
    }
    if (freezeGlow > 0.02f && freezeX >= 0)
    {
        juce::ColourGradient ig (ice.withAlpha (0.0f), freezeX - 30, 0, ice.withAlpha (0.0f), freezeX + 30, 0, false);
        ig.addColour (0.5, ice.withAlpha (0.22f * freezeGlow));
        g.setGradientFill (ig);
        g.fillRect (juce::Rectangle<float> (freezeX - 30, bodyR.getY(), 60, bodyR.getHeight()));
        icon::snowflake (g, juce::Rectangle<float> (22, 22).withCentre ({ freezeX, bodyR.getY() + 14 }), ice.withAlpha (freezeGlow));
    }
    if (playShown >= 0.0f && playShown <= 1.0f)
    {
        // where the sound is playing: a fine line through the body there (the body itself pulses as it passes)
        const float x = bodyR.getX() + bodyR.getWidth() * playShown;
        const auto& c = cols[(size_t) juce::jlimit (0, juce::jmax (0, iw - 1), (int) (x / kCell + 0.5f))];
        juce::ColourGradient pg (text.withAlpha (0.0f), x, c.cy - c.top - 12.0f, text.withAlpha (0.0f), x, c.cy + c.bot + 12.0f, false);
        pg.addColour (0.5, text.withAlpha (0.55f));
        g.setGradientFill (pg);
        g.fillRect (x - 0.6f, c.cy - c.top - 12.0f, 1.2f, c.top + c.bot + 24.0f);
    }
}

double SoundBody::benchmarkFrameMs (int n, bool touching, float scale)
{
    juce::Image im (juce::Image::ARGB, juce::jmax (1, juce::roundToInt ((float) getWidth() * scale)), juce::jmax (1, juce::roundToInt ((float) getHeight() * scale)), true);
    juce::Graphics g (im);
    g.addTransform (juce::AffineTransform::scale (scale));
    const bool wasPressing = pressing;
    pressing = touching;
    for (int i = 0; i < 10; ++i) { ++stamp; paint (g); }   // warm up (allocates the frame once)
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    for (int i = 0; i < n; ++i)
    {
        t += 1.0 / 60.0;
        if (wantsFrame()) paint (g);   // (as the timer does: calm, it draws every other frame)
    }
    const double ms = (juce::Time::getMillisecondCounterHiRes() - t0) / juce::jmax (1, n);
    pressing = wasPressing;
    frameValid = softValid = false;
    return ms;
}
}
