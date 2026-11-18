#include "TerrainView.h"
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <numeric>
#include <set>

namespace ghost::terrain
{
namespace
{
inline float segDist (juce::Point<float> a, juce::Point<float> b, juce::Point<float> p)
{
    juce::Point<float> near;
    return juce::Line<float> (a, b).getDistanceFromPoint (p, near);
}
// ---- the reference's numbers (topographically.vercel.app, measured from what the page does)
constexpr float kTilt = 50.0f, kTurn = -20.0f;     // the camera: 50 degrees down, turned 20
constexpr float kHeight = 0.64f;                     // a full mountain (height 1) stands 0.64 island radii tall ("height: 32")
constexpr int kLines = 25;                           // rings to a full height
constexpr float kStep = 1.0f / (float) kLines;
constexpr float kFade = 0.81f;                       // rings fade towards the shore from 0.19 of the radius out
constexpr float kWobble = 0.22f;                     // a mountain's radius breathes +-22 %
constexpr float kReachSize = 1.24f, kReachFalloff = 1.05f, kReachSat = 0.15f;   // the lit region: the mountain's own shape, wider
constexpr float kDim = 0.7f;                         // a chosen mountain dims the rest to 30 %
constexpr double kSpringSeconds = 0.35, kGrowInMs = 700, kGrowOutMs = 500, kTintMs = 180;
constexpr double kOpenDelayMs = 300, kOpenMs = 1800;
constexpr float kStemPx = 150.0f;                    // the dot stands this far above the summit
constexpr float kDotR = 3.0f;
constexpr float kTagSize = 14.0f, kTagPad = 5.0f, kTagRadius = 7.5f;
constexpr float kArrowGap = 2.0f, kHeldArrowGap = 6.0f, kArrowSize = 6.0f, kLineGap = 7.0f;
constexpr float kLabelSize = 8.0f;
constexpr float kCssToJuce = 1.2f;                  // a CSS font size as a JUCE font height (JUCE's counts ascent + descent)                   // the height numbers in the rings
constexpr float kSizeLead = 32.0f;                   // the size tag hangs this far off the outline

inline double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }
inline float smoothstep (float t) { return t <= 0.0f ? 0.0f : (t >= 1.0f ? 1.0f : t * t * (3.0f - 2.0f * t)); }
// the reference's curves
inline float easeOut (float t) { return 1.0f - std::pow (1.0f - juce::jlimit (0.0f, 1.0f, t), 3.0f); }
inline float easeInOut (float t)
{
    t = juce::jlimit (0.0f, 1.0f, t);
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow (-2.0f * t + 2.0f, 3.0f) * 0.5f;
}
float bezierEase (float x, float x1, float y1, float x2, float y2)
{
    // a CSS cubic-bezier(x1, y1, x2, y2) at x
    float lo = 0, hi = 1, t = x;
    for (int i = 0; i < 24; ++i)
    {
        const float bx = 3 * (1 - t) * (1 - t) * t * x1 + 3 * (1 - t) * t * t * x2 + t * t * t;
        if (bx < x) lo = t; else hi = t;
        t = 0.5f * (lo + hi);
    }
    return 3 * (1 - t) * (1 - t) * t * y1 + 3 * (1 - t) * t * t * y2 + t * t * t;
}
inline float rimAt (float u, float v)
{
    const float t = std::sqrt (u * u + v * v);
    return 1.0f - smoothstep ((t - (1.0f - kFade)) / kFade);
}
// the lit region of one mountain at island point (u, v): its shape, a quarter wider, saturating early
inline float reachAt (const Peak& p, float u, float v, float wob)
{
    const float pu = 2.0f * p.x - 1.0f, pv = 1.0f - 2.0f * p.z;
    const float ru = juce::jmax (1e-4f, 4.0f * p.width * kReachSize), rv = juce::jmax (1e-4f, 4.0f * p.depth * kReachSize);
    const float du = (u - pu) / ru, dv = (v - pv) / rv;
    const float s = std::sqrt (du * du + dv * dv) * (1.0f + kWobble * wob);
    if (s >= 1.6f) return 0.0f;
    float c = std::exp (-3.0f * s * s) * (s > 1.2f ? 1.0f - smoothstep ((s - 1.2f) / 0.4f) : 1.0f);
    return c > 0.0f ? std::pow (c, kReachFalloff) : 0.0f;
}

// the reference's curve through a polyline (each piece a cubic whose handles follow the neighbours, a third of its length)
void addCurve (juce::Path& path, const std::vector<juce::Point<float>>& e, bool closed, float smoothness = 1.0f)
{
    const int n = (int) e.size();
    if (n < 2) return;
    path.startNewSubPath (e[0]);
    const int last = n - 1;
    auto unit = [] (juce::Point<float> a, juce::Point<float> b, float len)
    {
        const float d = a.getDistanceFrom (b);
        return d > 1e-6f ? (b - a) * (len / d) : juce::Point<float>();
    };
    for (int o = 0; o < last; ++o)
    {
        const auto s = e[(size_t) (o == 0 ? (closed ? last - 1 : 0) : o - 1)];
        const auto c = e[(size_t) o], l = e[(size_t) o + 1];
        const auto u = e[(size_t) (o + 2 > last ? (closed ? 1 : last) : o + 2)];
        const float d = c.getDistanceFrom (l) / 3.0f * smoothness;
        const auto f = unit (s, l, d), p = unit (c, u, d);
        path.cubicTo (c + f, l - p, l);
    }
    if (closed) path.closeSubPath();
}

// Douglas-Peucker
void simplify (std::vector<juce::Point<float>>& pts, float tol)
{
    if (pts.size() <= 2) return;
    std::vector<uint8_t> keep (pts.size(), 0);
    keep.front() = keep.back() = 1;
    std::vector<std::pair<int, int>> stack { { 0, (int) pts.size() - 1 } };
    while (! stack.empty())
    {
        auto [a, b] = stack.back(); stack.pop_back();
        if (b - a < 2) continue;
        const auto A = pts[(size_t) a], B = pts[(size_t) b];
        const float dx = B.x - A.x, dy = B.y - A.y, L2 = dx * dx + dy * dy;
        float best = 0; int bi = -1;
        for (int i = a + 1; i < b; ++i)
        {
            const auto P = pts[(size_t) i];
            float d;
            if (L2 < 1e-12f) d = P.getDistanceFrom (A);
            else
            {
                const float t = ((P.x - A.x) * dx + (P.y - A.y) * dy) / L2;
                d = P.getDistanceFrom ({ A.x + t * dx, A.y + t * dy });
            }
            if (d > best) { best = d; bi = i; }
        }
        if (best > tol && bi > 0) { keep[(size_t) bi] = 1; stack.push_back ({ a, bi }); stack.push_back ({ bi, b }); }
    }
    std::vector<juce::Point<float>> out;
    for (size_t i = 0; i < pts.size(); ++i) if (keep[i]) out.push_back (pts[i]);
    pts.swap (out);
}

// the shuffle / tag / x icons: the reference's 14 px glyphs
const char* kShufflePath = "M10.9122 7.75444C10.6843 7.52656 10.3149 7.52656 10.087 7.75444C9.85911 7.98233 9.85911 8.35178 10.087 8.57967L11.2303 9.723H9.289C8.99733 9.723 8.72356 9.59389 8.53844 9.36911L7.56389 8.18611C7.35856 7.93722 6.99144 7.90222 6.74256 8.106C6.49367 8.31133 6.45789 8.67844 6.66244 8.92733L7.637 10.1103C8.04456 10.6058 8.64656 10.8897 9.28822 10.8897H11.2296L10.0862 12.033C9.85833 12.2609 9.85833 12.6303 10.0862 12.8582C10.1998 12.9718 10.3491 13.0293 10.4984 13.0293C10.6478 13.0293 10.7971 12.9726 10.9107 12.8582L13.0496 10.7193C13.2774 10.4914 13.2774 10.122 13.0496 9.89411L10.9107 7.75522L10.9122 7.75444Z"
                           "M1.36111 4.27778H2.37689C2.66856 4.27778 2.94233 4.40689 3.12744 4.63167L4.102 5.81467C4.21711 5.95467 4.38433 6.027 4.55311 6.027C4.68378 6.027 4.81444 5.98344 4.92333 5.894C5.17222 5.68867 5.208 5.32156 5.00344 5.07267L4.02889 3.88967C3.62133 3.39422 3.01933 3.11033 2.37767 3.11033H1.36111C1.03911 3.11033 0.777778 3.37167 0.777778 3.69367C0.777778 4.01567 1.03911 4.27778 1.36111 4.27778Z"
                           "M9.28978 4.27778H11.2311L10.0878 5.42111C9.85989 5.649 9.85989 6.01844 10.0878 6.24633C10.2013 6.35989 10.3507 6.41744 10.5 6.41744C10.6493 6.41744 10.7987 6.36067 10.9122 6.24633L13.0511 4.10744C13.279 3.87956 13.279 3.51011 13.0511 3.28222L10.9122 1.14333C10.6843 0.915444 10.3149 0.915444 10.087 1.14333C9.85911 1.37122 9.85911 1.74067 10.087 1.96856L11.2303 3.11189H9.289C8.64733 3.11189 8.04533 3.39578 7.63778 3.89122L3.12667 9.36911C2.94156 9.59389 2.66778 9.723 2.37611 9.723H1.36111C1.03911 9.723 0.777778 9.98433 0.777778 10.3063C0.777778 10.6283 1.03911 10.8897 1.36111 10.8897H2.37689C3.01856 10.8897 3.62056 10.6058 4.02811 10.1103L8.53922 4.63244C8.72433 4.40767 8.99811 4.27778 9.28978 4.27778Z";
const char* kTagPath = "M10.7979 2.83733C10.5381 2.51689 10.1523 2.33333 9.74011 2.33333H3.69444C2.51456 2.33333 1.55556 3.29233 1.55556 4.47222V9.52778C1.55556 10.7077 2.51456 11.6667 3.69444 11.6667H9.74011C10.1523 11.6667 10.5381 11.4831 10.7979 11.1619L13.8701 7.36711C14.0436 7.15322 14.0436 6.84756 13.8701 6.63367L10.7979 2.83733Z";
const char* kClearPath = "M3.11092 11.4722C2.96158 11.4722 2.81225 11.4154 2.69869 11.3011C2.47081 11.0732 2.47081 10.7038 2.69869 10.4759L10.4765 2.69888C10.7044 2.471 11.0738 2.471 11.3017 2.69888C11.5296 2.92677 11.5296 3.29622 11.3017 3.52411L3.52314 11.3011C3.40958 11.4147 3.26025 11.4722 3.11092 11.4722Z"
                         "M10.8887 11.4722C10.7394 11.4722 10.59 11.4155 10.4765 11.3011L2.69869 3.52335C2.47081 3.29546 2.47081 2.92601 2.69869 2.69812C2.92658 2.47024 3.29603 2.47024 3.52392 2.69812L11.3009 10.4767C11.5288 10.7046 11.5288 11.074 11.3009 11.3019C11.1874 11.4155 11.038 11.473 10.8887 11.473V11.4722Z";
const juce::Path& iconPath (const char* d)
{
    static std::map<const char*, juce::Path> cache;
    auto it = cache.find (d);
    if (it == cache.end()) it = cache.emplace (d, juce::Drawable::parseSVGPath (d)).first;
    return it->second;
}
}

void placedStoreErase (const void* v);
double TerrainView::profileMs[8] = { 1e9, 1e9, 1e9, 1e9, 1e9, 1e9, 1e9, 1e9 };

// ================================================================================================ construction
TerrainView::TerrainView (TerrainEditor& e, Range r) : range (r), interaction (e), ed (e)
{
    interaction.hit = [this] (juce::Point<float> p) { return hitAt (p); };
    interaction.groundAt = [this] (juce::Point<float> p, float lift, float& x, float& z) { return groundAtLift (p, lift, x, z); };
    interaction.pick = [this] (juce::Point<float> p, float& x, float& z) { return landAt (p, x, z); };
    interaction.bodyAt = [this] (juce::Point<float> p) { return bodyAt (p); };
    watchId = ed.watch ([this] { markDirty(); });
    setOpaque (true);
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (true);
    startTimerHz (60);
}

TerrainView::~TerrainView()
{
    ed.unwatch (watchId);
    stopTimer();
    placedStoreErase (this);
}

juce::Font TerrainView::fontOf (float h, bool bold) const
{
    const int key = juce::roundToInt (h * 4.0f) * 2 + (bold ? 1 : 0);
    auto it = fontCache.find (key);
    if (it != fontCache.end()) return it->second;
    auto f = palette.font ? palette.font (h, bold) : juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
    fontCache[key] = f;
    return f;
}

void TerrainView::setPalette (const Palette& p)
{
    palette = p;
    fontCache.clear();
    cacheValid = false;
    markDirty();
    repaint();
}

void TerrainView::setMeters (std::vector<Meter> m)
{
    meters = std::move (m);
    repaint (juce::Rectangle<int> (0, 0, 260, 120));
}

void TerrainView::status (const juce::String& s) { if (onStatus) onStatus (s); }

void TerrainView::setSpectrum (std::vector<float> dbAcrossX)
{
    spectrum = std::move (dbAcrossX);
    if (analyzer) repaint();
}

void TerrainView::setPlayhead (float z)
{
    if (std::abs (z - playZ) > 1e-4f || (z < 0) != (playZ < 0)) { playZ = z; repaint(); }
}

void TerrainView::setDisplayPhase (float phase)
{
    if (std::abs (phase - displayPhase) < 1e-5f) return;
    displayPhase = phase;
    if (ed.terrain().evolve > 0.0f) markDirty();
}

void TerrainView::playOpening()
{
    openT = 0.0f; openStart = nowMs();
    offsetValid = false;
    updateCamera();
    markDirty();
}

void TerrainView::visibilityChanged()
{
    if (isShowing() && openStart <= 0.0) playOpening();
}

Terrain TerrainView::drawn() const
{
    Terrain t = ed.terrain();
    if (shownCount == t.size() && ! shown.empty())
    {
        const double now = nowMs();
        Terrain d = t;
        d.clear();
        int n = 0;
        for (auto& sh : shown)
        {
            if (n >= Terrain::kMaxPeaks) break;
            Peak p = sh.p;
            p.height *= growOf (sh, now);
            if (! sh.ghost && n < t.size()) d.add (p);
            ++n;
        }
        for (auto& sh : shown)
            if (sh.ghost && d.size() < Terrain::kMaxPeaks) { Peak p = sh.p; p.height *= growOf (sh, now); d.add (p); }
        t = d;
    }
    if (displayPhase >= 0.0f) t.phase = displayPhase;
    return t;
}

// ================================================================================================ camera
float TerrainView::uiScale() const { return juce::jlimit (0.72f, 1.0f, (float) getHeight() / 745.0f); }

void TerrainView::updateCamera()
{
    const float W = (float) juce::jmax (40, getWidth()), H = (float) juce::jmax (40, getHeight());
    tiltNow = kTilt * openT;
    const float s = juce::degreesToRadians (tiltNow), c = juce::degreesToRadians (kTurn);
    const float cl = std::cos (s), sl = std::sin (s), d = std::cos (c), f = std::sin (c);
    // the reference's fit: the ground (a disc of radius min(w, h) / 2) and a full mountain fit inside a margin
    const float re = juce::jmin (W, H) * 0.5f * groundScale;
    const float p = re * (std::abs (d) + std::abs (f));
    const float n = kHeight * re;
    const float g = -juce::jmax (p * cl, n * sl), under = p * cl;
    const float bottomRoom = toolbarOn ? 0.0f : 0.0f;
    const float v = juce::jmax (48.0f * uiScale(), 0.1f * juce::jmin (W, H));
    auto fit = [&] (float e, float nn) { return juce::jmin ((W - 2 * v) / e, (H - 2 * v - bottomRoom) / nn); };
    const float b = juce::jmin (1.0f, fit (2 * p, under - g) / fit (2 * re, 2 * re));
    K = b * re;
    m11 = d; m12 = -f; m21 = f * cl; m22 = d * cl;   // per island radius, times K
    rise = K * sl;
    heightUnits = kHeight;
    ax = W * 0.5f - viewOffX * openT;
    ay = H * 0.5f - viewOffY * openT - (g + under) * 0.5f * b;
    interaction.pixelsPerUnitHeight = juce::jmax (20.0f, liftPx (1.0f));
    interaction.pixelsPerUnitSize = juce::jmax (20.0f, K * std::hypot (m11, m12));
}

juce::Point<float> TerrainView::toScreen (float u, float v, float h) const
{
    return { ax + K * (m11 * u + m12 * v), ay + K * (m21 * u + m22 * v) - liftPx (h) };
}

bool TerrainView::toGround (juce::Point<float> p, float h, float& u, float& v) const
{
    const float det = K * K * (m11 * m22 - m12 * m21);
    if (std::abs (det) < 1e-9f) return false;
    const float sx = p.x - ax, sy = p.y - ay + liftPx (h);
    u = K * (m22 * sx - m12 * sy) / det;
    v = K * (m11 * sy - m21 * sx) / det;
    return true;
}

void TerrainView::fitOffset()
{
    // the reference: the land's visible rings and the dots, placed so their bottom sits above the toolbar's room
    const float W = (float) getWidth(), H = (float) getHeight();
    if (W < 10 || H < 10) return;
    const float keepT = openT; const float keepX = viewOffX, keepY = viewOffY;
    openT = 1.0f; viewOffX = viewOffY = 0.0f;
    updateCamera();
    const Terrain t = ed.terrain();
    // the land's extent: its rings where they show (a quarter bright or more), and the dots
    buildRings (t);
    buildHandles (t);
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (auto& r : rings)
        for (size_t k = 0; k < r.screen.size(); ++k)
            if (rimAt (r.ground[k].x, r.ground[k].y) >= 0.25f)
            {
                const auto& q = r.screen[k];
                x0 = juce::jmin (x0, q.x); x1 = juce::jmax (x1, q.x); y0 = juce::jmin (y0, q.y); y1 = juce::jmax (y1, q.y);
            }
    for (auto& H : handles) y0 = juce::jmin (y0, H.dot.y);
    if (x0 > x1) { x0 = W * 0.4f; x1 = W * 0.6f; y0 = H * 0.4f; y1 = H * 0.6f; }
    const float reserve = toolbarOn ? juce::jmax (168.0f * uiScale(), 0.2f * H) : juce::jmax (40.0f, 0.08f * H);
    const float o = H - reserve, top = 40.0f * uiScale();
    const float sCentre = (y1 - y0 <= o - top) ? o - (y1 - y0) * 0.5f : (top + o) * 0.5f;
    viewOffX = (x0 + x1) * 0.5f - W * 0.5f;
    viewOffY = (y0 + y1) * 0.5f - sCentre;
    juce::ignoreUnused (keepX, keepY);
    openT = keepT;
    offsetValid = true;
    updateCamera();
}

void TerrainView::resized()
{
    offsetValid = false;
    fitOffset();
    cacheValid = false;
    dirty = true;
}

juce::Point<float> TerrainView::project (float x, float z, float h) const { return toScreen (2.0f * x - 1.0f, 1.0f - 2.0f * z, h); }

bool TerrainView::pickGround (juce::Point<float> p, float& x, float& z) const { return groundAtLift (p, 0.0f, x, z); }

bool TerrainView::groundAtLift (juce::Point<float> p, float lift, float& x, float& z) const
{
    float u = 0, v = 0;
    if (! toGround (p, lift, u, v)) return false;
    x = 0.5f + 0.5f * u; z = 0.5f - 0.5f * v;
    const bool inside = u * u + v * v <= 1.0f;
    x = juce::jlimit (0.0f, 1.0f, x); z = juce::jlimit (0.0f, 1.0f, z);
    return inside;
}

bool TerrainView::landAt (juce::Point<float> p, float& x, float& z) const
{
    // the ground point whose land is under the pointer: start on the ground, climb to the land there, eight times
    float u = 0, v = 0;
    if (! toGround (p, 0.0f, u, v)) return false;
    for (int i = 0; i < 8; ++i)
    {
        const float h = landStep (u, v);
        if (! toGround (p, juce::jmax (0.0f, h), u, v)) return false;
    }
    x = juce::jlimit (0.0f, 1.0f, 0.5f + 0.5f * u); z = juce::jlimit (0.0f, 1.0f, 0.5f - 0.5f * v);
    return u * u + v * v <= 1.0f;
}

// ================================================================================================ the sampled land
float TerrainView::landRaw (float u, float v) const
{
    if (N < 2 || field.empty()) return 0.0f;
    const float gi = (u + 1.0f) * 0.5f * (float) (N - 1), gj = (1.0f - v) * 0.5f * (float) (N - 1);
    if (gi < 0 || gj < 0 || gi >= (float) (N - 1) || gj >= (float) (N - 1)) return -1e9f;
    const int i = (int) gi, j = (int) gj;
    const float fx = gi - (float) i, fz = gj - (float) j;
    const float* r0 = field.data() + (size_t) j * (size_t) N + (size_t) i;
    const float* r1 = r0 + N;
    return (r0[0] * (1 - fx) + r0[1] * fx) * (1 - fz) + (r1[0] * (1 - fx) + r1[1] * fx) * fz;
}

float TerrainView::landStep (float u, float v) const
{
    const float h = landRaw (u, v);
    if (h < -1e8f) return h;
    return juce::jmin (1.0f, std::floor (h / kStep + 1e-6f) * kStep);
}

// ================================================================================================ motion
float TerrainView::growOf (const Shown& sh, double now) const
{
    if (sh.growMs <= 0.0f) return sh.growTo;
    const float u = juce::jlimit (0.0f, 1.0f, (float) ((now - sh.growT0) / sh.growMs));
    return sh.growFrom + (sh.growTo - sh.growFrom) * bezierEase (u, 0.77f, 0.0f, 0.175f, 1.0f);
}

void TerrainView::setHeld (int i, float to, double now)
{
    if (i < 0 || i >= Terrain::kMaxPeaks || std::abs (heldTo[(size_t) i] - to) < 1e-4f) return;
    heldFrom[(size_t) i] = heldNow[(size_t) i]; heldTo[(size_t) i] = to; heldT0[(size_t) i] = now;
    motionActive = true;
}

void TerrainView::setHover (int i, float to, double now)
{
    if (i < 0 || i >= Terrain::kMaxPeaks || std::abs (hoverTo[(size_t) i] - to) < 1e-4f) return;
    hoverFrom[(size_t) i] = hoverNow[(size_t) i]; hoverTo[(size_t) i] = to; hoverT0[(size_t) i] = now;
    motionActive = true;
}

void TerrainView::refreshLights (double now)
{
    const int n = ed.terrain().size();
    for (int i = 0; i < Terrain::kMaxPeaks; ++i)
    {
        setHeld (i, i < n && i == ed.selected ? 1.0f : 0.0f, now);
        setHover (i, i < n && i == hoverPeak && i != ed.selected && ! interaction.active() ? 0.85f : 0.0f, now);
    }
}

void TerrainView::stepMotion (double now)
{
    const Terrain target = ed.terrain();
    if ((int) shown.size() != shownCount || shownCount != target.size() || [&] { int k = 0; for (auto& s : shown) if (! s.ghost) ++k; return k != target.size(); }())
    {
        std::vector<Shown> next;
        std::vector<bool> used (shown.size(), false);
        for (int i = 0; i < target.size(); ++i)
        {
            const Peak& tp = target[i];
            int bestK = -1; float bd = 0.05f;
            for (size_t k = 0; k < shown.size(); ++k)
                if (! used[k] && ! shown[k].ghost) { const float d = std::hypot (shown[k].p.x - tp.x, shown[k].p.z - tp.z) + std::abs (shown[k].p.width - tp.width); if (d < bd) { bd = d; bestK = (int) k; } }
            if (bestK < 0 && target.size() == shownCount && i < (int) shown.size() && ! used[(size_t) i] && ! shown[(size_t) i].ghost) bestK = i;
            Shown sh;
            if (bestK >= 0) { sh = shown[(size_t) bestK]; used[(size_t) bestK] = true; }
            else { sh.p = tp; sh.growFrom = 0.0f; sh.growTo = 1.0f; sh.growT0 = now; sh.growMs = (float) kGrowInMs; }
            next.push_back (sh);
        }
        for (size_t k = 0; k < shown.size(); ++k)
            if (! used[k] && (int) next.size() < Terrain::kMaxPeaks)
            {
                Shown g = shown[k];
                if (! g.ghost) { g.ghost = true; g.growFrom = growOf (g, now); g.growTo = 0.0f; g.growT0 = now; g.growMs = (float) kGrowOutMs; }
                next.push_back (g);
            }
        shown = std::move (next);
        shownCount = target.size();
        motionActive = true;
    }
    const double dt = juce::jlimit (0.0, 0.1, lastStep > 0 ? (now - lastStep) / 1000.0 : 1.0 / 60.0);
    lastStep = now;
    const float w = (float) (juce::MathConstants<double>::twoPi / kSpringSeconds), k = w * w, c = 2.0f * w;   // critically damped
    const int sub = juce::jmax (1, (int) std::ceil (dt / (1.0 / 240.0)));
    const float h = (float) dt / (float) sub;
    bool moving = false;
    int ti = 0;
    for (auto& sh : shown)
    {
        if (! sh.ghost && ti < target.size())
        {
            const Peak& tp = target[ti];
            float* cur[5] = { &sh.p.x, &sh.p.z, &sh.p.height, &sh.p.width, &sh.p.depth };
            const float goal[5] = { tp.x, tp.z, tp.height, tp.width, tp.depth };
            for (int a = 0; a < 5; ++a)
            {
                for (int st = 0; st < sub; ++st) { sh.v[a] += (k * (goal[a] - *cur[a]) - c * sh.v[a]) * h; *cur[a] += sh.v[a] * h; }
                if (std::abs (goal[a] - *cur[a]) > 1e-4f || std::abs (sh.v[a]) > 1e-3f) moving = true;
                else { *cur[a] = goal[a]; sh.v[a] = 0.0f; }
            }
            sh.p.rough = tp.rough;
            ++ti;
        }
        if (sh.growMs > 0.0f && now - sh.growT0 < sh.growMs) moving = true;
    }
    shown.erase (std::remove_if (shown.begin(), shown.end(), [&] (const Shown& g) { return g.ghost && growOf (g, now) <= 0.001f; }), shown.end());
    for (size_t i = 0; i < heldNow.size(); ++i)
    {
        auto ease = [&] (float& cur, float from, float to, double t0)
        {
            if (std::abs (cur - to) < 1e-4f) { cur = to; return; }
            const float u = juce::jlimit (0.0f, 1.0f, (float) ((now - t0) / kTintMs));
            cur = from + (to - from) * bezierEase (u, 0.23f, 1.0f, 0.32f, 1.0f);   // the reference's "snappy"
            if (u < 1.0f) moving = true; else cur = to;
        };
        ease (heldNow[i], heldFrom[i], heldTo[i], heldT0[i]);
        ease (hoverNow[i], hoverFrom[i], hoverTo[i], hoverT0[i]);
    }
    motionActive = moving;
    markDirty();
}

void TerrainView::settle()
{
    const Terrain t = ed.terrain();
    shown.clear();
    for (int i = 0; i < t.size(); ++i) { Shown sh; sh.p = t[i]; shown.push_back (sh); }
    shownCount = t.size();
    for (size_t i = 0; i < heldNow.size(); ++i)
    {
        heldTo[i] = heldNow[i] = (int) i == ed.selected && (int) i < t.size() ? 1.0f : 0.0f;
        hoverTo[i] = hoverNow[i] = 0.0f;
    }
    motionActive = false;
    if (! offsetValid) fitOffset();
    openT = stillOpening >= 0.0f ? stillOpening : 1.0f; openStart = juce::jmax (openStart, 1.0);
    updateCamera();
    labelsQuiet = false; labelsShownAt = 0; landChangedAt = 0;
    dirty = true; cacheValid = false;
}

// ================================================================================================ rings
void TerrainView::buildRings (const Terrain& t)
{
    // the grid over the island (one cell ~3 px; finer when there are spires), its sampled land
    const bool thin = [&] { for (auto& p : t) if (p.r() < 0.12f && std::abs (p.height) > 0.3f) return true; return false; }();
    const float cellPx = thin ? 2.4f : 3.2f;
    N = juce::jlimit (64, thin ? 260 : 200, juce::roundToInt (2.0f * K / cellPx) + 1);
    const size_t NN = (size_t) N * (size_t) N;
    field.resize (NN);
    if (shapeN != N || shapeSeed != t.seed || shapeVersion != t.version || shapeCache.size() != NN * 2)
    {
        shapeCache.resize (NN * 2);
        Terrain::shapeGrid (t.seed, N, N, 0.0f, 1.0f, 0.0f, 1.0f, shapeCache.data(), t.version);
        shapeN = N; shapeSeed = t.seed; shapeVersion = t.version;
    }
    double tm = nowMs();
    {
        // (sample() wants the grit pattern; no grit: none)
        static thread_local std::vector<float> gritNoise;
        bool anyRough = t.grit > 0.0f;
        for (auto& p : t) anyRough = anyRough || p.rough > 0.0f;
        const float* noise = nullptr;
        if (anyRough) { gritNoise.resize (NN); Terrain::noiseGrid (t.seed, N, N, 0.0f, 1.0f, 0.0f, 1.0f, t.frozen, t.freezeZ, gritNoise.data()); noise = gritNoise.data(); }
        // the view shows the land at one moment: the middle of time (FREEZE: its moment), every mountain at its own depth
        // reads as itself; time is the z axis, so the whole surface is drawn as it is
        t.sample (N, N, 0.0f, 1.0f, 0.0f, 1.0f, field.data(), nullptr, noise, nullptr, -1, nullptr, shapeCache.data(), true, true);
    }
    profileMs[0] = juce::jmin (profileMs[0], nowMs() - tm); tm = nowMs();
    // the levels present
    float hiH = 0.0f, loH = 0.0f;
    for (float h : field) { hiH = juce::jmax (hiH, h); loH = juce::jmin (loH, h); }
    const int kTop = juce::jmin (kLines, (int) std::floor (hiH / kStep + 1e-4f));
    const int kBottom = loH < -0.5f * kStep ? -juce::jmin (kLines, (int) std::floor (-loH / kStep + 1e-4f)) : 0;
    rings.clear();
    segmentCount = 0;
    // marching squares: edge ids (horizontal edges r * N + c, vertical edges N * N + r * N + c)
    const int E = 2 * N * N;
    static thread_local std::vector<int> segA, segB, slot0, slot1;
    static thread_local std::vector<uint8_t> usedSeg;
    slot0.assign ((size_t) E, -1); slot1.assign ((size_t) E, -1);
    const float su = 2.0f / (float) (N - 1);
    const float tolerance = 0.8f / juce::jmax (0.5f, K * su);   // 0.8 px in grid cells
    auto value = [&] (int r, int c) -> float
    {
        if (r <= 0 || c <= 0 || r >= N - 1 || c >= N - 1) return -2.0f;   // a border below everything: every ring closes
        return field[(size_t) r * (size_t) N + (size_t) c];
    };
    // one pass over the cells: which rings cross each (the reference's way: no pass per ring)
    static thread_local std::vector<std::vector<int>> cellsAt;
    const int nLevels = kTop - kBottom + 1;
    if ((int) cellsAt.size() < nLevels) cellsAt.resize ((size_t) nLevels);
    for (int q = 0; q < nLevels; ++q) cellsAt[(size_t) q].clear();
    for (int r = 0; r < N - 1; ++r)
        for (int c = 0; c < N - 1; ++c)
        {
            const float tl = value (r, c), tr = value (r, c + 1), bl = value (r + 1, c), br = value (r + 1, c + 1);
            const float mn = juce::jmin (juce::jmin (tl, tr), juce::jmin (bl, br)), mx = juce::jmax (juce::jmax (tl, tr), juce::jmax (bl, br));
            const int lo = juce::jmax (kBottom, (int) std::floor (mn / kStep) - 1), hi = juce::jmin (kTop, (int) std::floor (mx / kStep) + 1);
            for (int k = lo; k <= hi; ++k)
            {
                const float L = (float) k * kStep;
                if (k != 0 && mx >= L && mn < L) cellsAt[(size_t) (k - kBottom)].push_back (r * N + c);
            }
        }
    for (int k = kBottom; k <= kTop; ++k)
    {
        if (k == 0) continue;
        const float L = (float) k * kStep;
        segA.clear(); segB.clear();
        for (int cell : cellsAt[(size_t) (k - kBottom)])
            {
                const int r = cell / N, c = cell % N;
                const float tl = value (r, c), tr = value (r, c + 1), bl = value (r + 1, c), br = value (r + 1, c + 1);
                const float mn = juce::jmin (juce::jmin (tl, tr), juce::jmin (bl, br)), mx = juce::jmax (juce::jmax (tl, tr), juce::jmax (bl, br));
                if (mx < L || mn >= L) continue;
                const int code = (tl >= L ? 8 : 0) | (tr >= L ? 4 : 0) | (br >= L ? 2 : 0) | (bl >= L ? 1 : 0);
                const int top = r * N + c, bottom = (r + 1) * N + c, left = N * N + r * N + c, right = left + 1;
                auto seg = [&] (int a, int b) { segA.push_back (a); segB.push_back (b); };
                switch (code)
                {
                    case 1: seg (bottom, left); break;
                    case 2: seg (right, bottom); break;
                    case 3: seg (right, left); break;
                    case 4: seg (top, right); break;
                    case 5: seg (top, left); seg (right, bottom); break;
                    case 6: seg (top, bottom); break;
                    case 7: seg (top, left); break;
                    case 8: seg (left, top); break;
                    case 9: seg (bottom, top); break;
                    case 10: seg (left, bottom); seg (top, right); break;
                    case 11: seg (right, top); break;
                    case 12: seg (left, right); break;
                    case 13: seg (bottom, right); break;
                    case 14: seg (left, bottom); break;
                    default: break;
                }
            }
        const int S = (int) segA.size();
        if (S == 0) continue;
        segmentCount += S;
        auto attach = [&] (int edge, int ref) { if (slot0[(size_t) edge] < 0) slot0[(size_t) edge] = ref; else slot1[(size_t) edge] = ref; };
        for (int i = 0; i < S; ++i) { attach (segA[(size_t) i], i * 2); attach (segB[(size_t) i], i * 2 + 1); }
        usedSeg.assign ((size_t) S, 0);
        auto edgePoint = [&] (int edge) -> juce::Point<float>
        {
            auto frac = [&] (float a, float b) { const float d = b - a; return std::abs (d) < 1e-10f ? 0.5f : juce::jlimit (0.0f, 1.0f, (L - a) / d); };
            if (edge < N * N) { const int r = edge / N, c = edge % N; return { (float) c + frac (value (r, c), value (r, c + 1)), (float) r }; }
            const int e2 = edge - N * N, r = e2 / N, c = e2 % N;
            return { (float) c, (float) r + frac (value (r, c), value (r + 1, c)) };
        };
        auto nextRef = [&] (int edge) -> int
        {
            const int a = slot0[(size_t) edge];
            if (a >= 0 && ! usedSeg[(size_t) (a >> 1)]) return a;
            const int b = slot1[(size_t) edge];
            return b >= 0 && ! usedSeg[(size_t) (b >> 1)] ? b : -1;
        };
        for (int i = 0; i < S; ++i)
        {
            if (usedSeg[(size_t) i]) continue;
            usedSeg[(size_t) i] = 1;
            std::vector<int> fwd { segA[(size_t) i], segB[(size_t) i] };
            for (int e = segB[(size_t) i];;)
            {
                const int ref = nextRef (e);
                if (ref < 0) break;
                usedSeg[(size_t) (ref >> 1)] = 1;
                e = (ref & 1) ? segA[(size_t) (ref >> 1)] : segB[(size_t) (ref >> 1)];
                fwd.push_back (e);
            }
            std::vector<int> back;
            for (int e = segA[(size_t) i];;)
            {
                const int ref = nextRef (e);
                if (ref < 0) break;
                usedSeg[(size_t) (ref >> 1)] = 1;
                e = (ref & 1) ? segA[(size_t) (ref >> 1)] : segB[(size_t) (ref >> 1)];
                back.push_back (e);
            }
            std::reverse (back.begin(), back.end());
            back.insert (back.end(), fwd.begin(), fwd.end());
            if (back.size() < 3) continue;
            Ring ring;
            ring.level = k;
            std::vector<juce::Point<float>> g;
            g.reserve (back.size());
            for (int e : back) g.push_back (edgePoint (e));
            ring.closed = g.front().getDistanceFrom (g.back()) < 1e-3f;
            simplify (g, tolerance);
            if (g.size() < 3) continue;
            // to island coordinates; counter-clockwise on screen (the reference turns its rings one way)
            ring.ground.reserve (g.size());
            for (auto& q : g) ring.ground.push_back ({ -1.0f + q.x * su, 1.0f - q.y * su });
            float area = 0;
            for (size_t a = 0; a + 1 < ring.ground.size(); ++a) area += ring.ground[a].x * ring.ground[a + 1].y - ring.ground[a + 1].x * ring.ground[a].y;
            if (area > 0) std::reverse (ring.ground.begin(), ring.ground.end());
            rings.push_back (std::move (ring));
        }
        for (int i = 0; i < S; ++i)
        {
            slot0[(size_t) segA[(size_t) i]] = slot1[(size_t) segA[(size_t) i]] = -1;
            slot0[(size_t) segB[(size_t) i]] = slot1[(size_t) segB[(size_t) i]] = -1;
        }
    }
    // project
    for (auto& ring : rings)
    {
        const float h = (float) ring.level * kStep;
        ring.screen.resize (ring.ground.size());
        juce::Rectangle<float> box;
        float len = 0;
        for (size_t a = 0; a < ring.ground.size(); ++a)
        {
            ring.screen[a] = toScreen (ring.ground[a].x, ring.ground[a].y, h);
            if (a > 0) len += ring.screen[a].getDistanceFrom (ring.screen[a - 1]);
            box = a == 0 ? juce::Rectangle<float> (ring.screen[a], ring.screen[a]) : box.getUnion (juce::Rectangle<float> (ring.screen[a], ring.screen[a]));
        }
        ring.box = box; ring.length = len;
    }
    profileMs[1] = juce::jmin (profileMs[1], nowMs() - tm);
}

// ================================================================================================ the ring cache
void TerrainView::renderCache (float scale)
{
    const int w = juce::jmax (1, juce::roundToInt ((float) getWidth() * scale)), h = juce::jmax (1, juce::roundToInt ((float) getHeight() * scale));
    if (cache.getWidth() != w || cache.getHeight() != h || ! cache.isValid()) cache = juce::Image (juce::Image::RGB, w, h, false);
    cacheScale = scale;
    updateCamera();
    const Terrain t = drawn();
    buildRings (t);
    double tm = nowMs();
    const double tColour = tm;
    // ---- the colour of a ring at each point of the island (an image the strokes are painted with): the ring colour fading
    // into the ground towards the shore; everything but the chosen mountain dimmed to a third; the chosen mountain's region
    // in the accent (its shape a quarter wider, saturating early), blending to accentTop with height
    float held = 0.0f;
    for (int i = 0; i < Terrain::kMaxPeaks; ++i) held += heldNow[(size_t) i];
    const float staticK = 1.0f - kDim * juce::jmin (1.0f, held);
    const size_t NN = (size_t) N * (size_t) N;
    litField.assign (NN, 0.0f);
    {
        struct Light { Peak p; float held, hover; };
        std::vector<Light> lights;
        for (int i = 0; i < t.size() && i < Terrain::kMaxPeaks; ++i)
            if (heldNow[(size_t) i] > 0.001f || hoverNow[(size_t) i] > 0.001f) lights.push_back ({ t.peakAt (i, t[i].z), heldNow[(size_t) i], hoverNow[(size_t) i] });
        if (! lights.empty())
            for (int j = 0; j < N; ++j)
            {
                const float v = 1.0f - 2.0f * (float) j / (float) (N - 1);
                for (int i = 0; i < N; ++i)
                {
                    const float u = -1.0f + 2.0f * (float) i / (float) (N - 1);
                    const float wob = shapeCache[((size_t) j * (size_t) N + (size_t) i) * 2];
                    float lit = 0.0f;
                    for (auto& l : lights)
                    {
                        const float e = reachAt (l.p, u, v, wob);
                        if (e > 0.0f) lit = juce::jmax (lit, juce::jmax (l.held * juce::jmin (1.0f, e / kReachSat), l.hover * e));
                    }
                    litField[(size_t) j * (size_t) N + (size_t) i] = lit;
                }
            }
    }
    if (colourImg.getWidth() != N || colourImg.getHeight() != N) colourImg = juce::Image (juce::Image::ARGB, N, N, false);
    {
        juce::Image::BitmapData bd (colourImg, juce::Image::BitmapData::writeOnly);
        const auto bg = palette.ground;
        for (int j = 0; j < N; ++j)
        {
            const float v = 1.0f - 2.0f * (float) j / (float) (N - 1);
            for (int i = 0; i < N; ++i)
            {
                const float u = -1.0f + 2.0f * (float) i / (float) (N - 1);
                const size_t idx = (size_t) j * (size_t) N + (size_t) i;
                const float rim = rimAt (u, v), hh = field[idx];
                const auto ink = hh < -0.5f * kStep ? palette.valley.withAlpha (1.0f) : palette.ring.withAlpha (1.0f);
                auto c = bg.interpolatedWith (ink, rim * staticK * palette.ring.getFloatAlpha());
                const float lit = litField[idx] * rim;
                if (lit > 0.0f) c = c.interpolatedWith (palette.accent.interpolatedWith (palette.accentTop, juce::jlimit (0.0f, 1.0f, hh)), lit);
                bd.setPixelColour (i, j, c);
            }
        }
    }
    profileMs[6] = juce::jmin (profileMs[6], nowMs() - tColour); tm = nowMs();
    // ---- paint: the ground, then ring by ring from the lowest: its land filled with the ground colour (its top, its cliff
    // down to the ring below, its foot) - this hides the rings behind it - then the ring itself
    juce::Graphics g (cache);
    g.addTransform (juce::AffineTransform::scale (scale));
    g.fillAll (palette.ground);
    g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
    const float su = 2.0f / (float) (N - 1), a0 = -1.0f - 0.5f * su, b0 = 1.0f + 0.5f * su;
    const juce::PathStrokeType line (1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    size_t r0 = 0;
    const int firstLevel = rings.empty() ? 0 : rings.front().level;
    while (r0 < rings.size())
    {
        const int k = rings[r0].level;
        size_t r1 = r0;
        while (r1 < rings.size() && rings[r1].level == k) ++r1;
        const float hNow = (float) k * kStep;
        // (the lowest ring has no cliff under it; the others drop to the ring below)
        const float drop = k == firstLevel ? 0.0f : liftPx (hNow) - liftPx ((float) (k - 1) * kStep);
        juce::Path faces, curves, walls;
        faces.setUsingNonZeroWinding (false);
        for (size_t r = r0; r < r1; ++r)
        {
            const auto& ring = rings[r];
            juce::Path c;
            addCurve (c, ring.screen, ring.closed);
            curves.addPath (c);
            if (ring.closed) faces.addPath (c);
            if (drop >= 0.5f)
                for (size_t a = 0; a + 1 < ring.screen.size(); ++a)
                {
                    auto p = ring.screen[a], q = ring.screen[a + 1];
                    if (q.x < p.x) std::swap (p, q);
                    walls.startNewSubPath (p); walls.lineTo (q); walls.lineTo (q.x, q.y + drop); walls.lineTo (p.x, p.y + drop); walls.closeSubPath();
                }
        }
        g.setColour (palette.ground);
        if (! faces.isEmpty())
        {
            g.fillPath (faces);
            if (! walls.isEmpty()) { g.fillPath (walls); g.fillPath (faces, juce::AffineTransform::translation (0.0f, drop)); }
        }
        // the ring: painted with the colour image, laid on the island and lifted to this ring's height
        const float l = liftPx (hNow);
        const juce::AffineTransform img (K * m11 * su, -K * m12 * su, ax + K * (m11 * a0 + m12 * b0),
                                         K * m21 * su, -K * m22 * su, ay + K * (m21 * a0 + m22 * b0) - l);
        g.setFillType (juce::FillType (colourImg, img));
        g.strokePath (curves, line);
        r0 = r1;
    }
    profileMs[2] = juce::jmin (profileMs[2], nowMs() - tm); tm = nowMs();
    buildHandles (t);
    profileMs[7] = juce::jmin (profileMs[7], nowMs() - tm);
    cacheValid = true; dirty = false;
}

// ================================================================================================ handles
// the land as drawn (mountains held in by the shore) without mountain `skip`: what that mountain stands on
static float heldWithout (const Terrain& t, float x, float z, int skip)
{
    const float zz = t.effectiveZ (z);
    const float floor = Terrain::floorAt (x, zz, t.reliefAt (x, zz));
    const float all = t.smoothHeightAt (x, z, skip);
    return floor + (t.version >= 2 ? Terrain::shoreAt (x, zz) : 1.0f) * (all - floor);
}

juce::Point<float> TerrainView::outlinePoint (const Terrain& t, int i, float angle, float& lift) const
{
    // the mountain's foot at this angle: where its normalised distance (with the wobble) reaches 1
    const Peak pk = t.peakAt (i, t[i].z);
    const float pu = 2.0f * pk.x - 1.0f, pv = 1.0f - 2.0f * pk.z;
    const float ca = std::cos (angle), sa = std::sin (angle);
    const float ru = juce::jmax (1e-4f, pk.r()), rv = juce::jmax (1e-4f, 4.0f * pk.depth);
    const float R = 1.0f / std::sqrt ((ca / ru) * (ca / ru) + (sa / rv) * (sa / rv));
    auto outside = [&] (float d)
    {
        const float u = pu + d * ca, v = pv + d * sa;
        return d * (1.0f + kWobble * t.wobbleAt (0.5f + 0.5f * u, 0.5f - 0.5f * v)) >= R;
    };
    float lo = R / (1.0f + kWobble), hi = R / (1.0f - kWobble), a = lo, b = hi;
    for (int e = 1; e <= 6; ++e) { const float d = lo + (hi - lo) * (float) e / 6.0f; if (outside (d)) { b = d; break; } a = d; }
    for (int e = 0; e < 7; ++e) { const float m = 0.5f * (a + b); if (outside (m)) b = m; else a = m; }
    const float d = 0.5f * (a + b);
    const float u = pu + d * ca, v = pv + d * sa;
    lift = juce::jlimit (0.0f, 1.0f, heldWithout (t, 0.5f + 0.5f * u, 0.5f - 0.5f * v, i));
    return toScreen (u, v, lift);
}

void TerrainView::buildHandles (const Terrain& t)
{
    handles.assign ((size_t) t.size(), {});
    const float ui = uiScale();
    const float perUnit = juce::jmax (1e-3f, liftPx (1.0f));
    // is the point at screen (x, y), standing at height `base`, hidden by land in front of it? (walk the line of sight)
    auto hidden = [&] (float x, float y, float base)
    {
        for (float s = base + kStep * 0.5f; s <= 1.0f + 1e-4f; s += kStep)
        {
            float u = 0, v = 0;
            if (! toGround ({ x, y }, s, u, v)) return false;
            const float land = landStep (u, v);
            if (land < -1e8f) return false;
            if (land >= s - 1e-5f) return true;
        }
        return false;
    };
    for (int i = 0; i < t.size(); ++i)
    {
        auto& H = handles[(size_t) i];
        const Peak pk = t.peakAt (i, t[i].z);
        const float u = 2.0f * pk.x - 1.0f, v = 1.0f - 2.0f * pk.z;
        float a = landRaw (u, v);
        if (a < -1e8f) a = 0.0f;
        H.landTop = juce::jmin (1.0f, a);
        H.top = a >= 0.0f ? juce::jmin (1.0f, std::floor (a / kStep + 1e-6f) * kStep) : juce::jmax (-1.0f, std::ceil (a / kStep - 1e-6f) * kStep);
        H.summitPt = toScreen (u, v, H.top);
        const auto d = toScreen (u, v, H.landTop);
        const float valueY = d.y - kStemPx * ui;
        const float p = kTagSize * 0.5f + kTagPad + kHeldArrowGap + kArrowSize + kLineGap;
        float vy = valueY, pin = valueY + p, dot = valueY - p - kDotR, stemTop = dot + kDotR + kLineGap;
        const float lowest = 12.0f;
        if (dot < lowest) { const float k = lowest - dot; vy += k; pin += k; dot += k; stemTop += k; }
        auto open = [&] (float y) { return H.summitPt.y + (y - H.summitPt.y) * openT; };
        H.valuePt = { H.summitPt.x, open (vy) }; H.pinTop = { H.summitPt.x, open (pin) };
        H.dot = { H.summitPt.x, open (dot) }; H.stemTop = { H.summitPt.x, open (stemTop) };
        // the stem rises from the mountain's foot (the land without it); the mountain itself, and anything in front, hides it
        const float foot = juce::jlimit (0.0f, 1.0f, std::floor (juce::jmax (0.0f, heldWithout (t, pk.x, t[i].z, i)) / kStep + 1e-6f) * kStep);
        H.foot = toScreen (u, v, foot);
        const float E = heldNow[(size_t) i];
        const float top = H.stemTop.y + (H.pinTop.y - H.stemTop.y) * E;
        H.stemVisible.clear();
        if (H.foot.y > top)
        {
            const int n = juce::jmax (1, (int) std::ceil ((H.foot.y - top) / 4.0f));
            std::vector<uint8_t> vis ((size_t) n + 1);
            for (int k = 0; k <= n; ++k)
            {
                const float y = H.foot.y - (H.foot.y - top) * (float) k / (float) n;
                const float s = foot + (H.foot.y - y) / perUnit;
                vis[(size_t) k] = hidden (H.foot.x, y, s) ? 0 : 1;
            }
            for (int k = 1; k < n; ++k) if (vis[(size_t) k - 1] == vis[(size_t) k + 1] && vis[(size_t) k] != vis[(size_t) k - 1]) vis[(size_t) k] = vis[(size_t) k - 1];
            for (int k = 0; k <= n; ++k)
            {
                if (! vis[(size_t) k]) continue;
                const float y = H.foot.y - (H.foot.y - top) * (float) k / (float) n;
                if (! H.stemVisible.empty() && k > 0 && vis[(size_t) k - 1]) H.stemVisible.back().second = y;
                else H.stemVisible.push_back ({ y, y });
            }
            H.stemVisible.erase (std::remove_if (H.stemVisible.begin(), H.stemVisible.end(), [] (auto& r) { return r.first - r.second <= 0.5f; }), H.stemVisible.end());
        }
        H.dotVisible = ! hidden (H.dot.x, H.dot.y, foot + (H.foot.y - H.dot.y) / perUnit);
        // the chosen one: its outline at the base (hidden parts left out) and where its size tag hangs
        if (E > 0.001f)
        {
            constexpr int kSamples = 144;
            H.outline.resize (kSamples + 1); H.outlineHidden.resize (kSamples + 1);
            std::vector<float> lifts (kSamples + 1);
            for (int k = 0; k <= kSamples; ++k)
            {
                float lift = 0;
                H.outline[(size_t) k] = outlinePoint (t, i, juce::MathConstants<float>::twoPi * (float) k / (float) kSamples, lift);
                lifts[(size_t) k] = lift;
                bool hid = false;
                for (float e = lift + kStep * 1.5f; e <= 1.0f && ! hid; e += kStep * 0.5f)
                {
                    float gu = 0, gv = 0;
                    if (! toGround (H.outline[(size_t) k], e, gu, gv)) break;
                    const float land = landStep (gu, gv);
                    if (land > -1e8f && land >= e) hid = true;
                }
                H.outlineHidden[(size_t) k] = hid;
            }
            const int last = kSamples;
            for (int k = 0; k < last; ++k)
            {
                if (H.outlineHidden[(size_t) k] == H.outlineHidden[(size_t) ((k + last - 1) % last)]) continue;
                int run = 1;
                while (run <= 2 && H.outlineHidden[(size_t) ((k + run) % last)] == H.outlineHidden[(size_t) k]) ++run;
                if (run <= 2) for (int q = 0; q < run; ++q) H.outlineHidden[(size_t) ((k + q) % last)] = ! H.outlineHidden[(size_t) k];
            }
            H.outlineHidden[(size_t) last] = H.outlineHidden[0];
            // the size tag: under the outline if it fits, else to its right, else its left
            const float halfH = kTagSize * 0.5f + kTagPad;
            const float tw = juce::GlyphArrangement::getStringWidth (fontOf (kTagSize * kCssToJuce), juce::String (pk.r(), 2));
            const float halfW = juce::jmax (tw * 0.5f + kTagPad, halfH);
            const float lead = (kHeldArrowGap + kArrowSize + kLineGap);
            const float reach = halfW + lead;
            auto place = [&] (int side, bool& fits)
            {
                const juce::Point<float> dir = side == 0 ? juce::Point<float> (0, 1) : (side == 1 ? juce::Point<float> (1, 0) : juce::Point<float> (-1, 0));
                float ang = std::atan2 (m12 * dir.x + m22 * dir.y, m11 * dir.x + m21 * dir.y);
                if (ang < 0) ang += juce::MathConstants<float>::twoPi;
                float lift = 0;
                const auto from = outlinePoint (t, i, ang, lift);
                const auto to = from + dir * (kSizeLead * ui);
                const auto anchor = side == 0 ? juce::Point<float> (to.x, to.y + kLineGap + halfH) : juce::Point<float> (to.x + dir.x * (lead + halfW), to.y);
                const int k = juce::roundToInt (ang / juce::MathConstants<float>::twoPi * (float) kSamples) % kSamples;
                fits = anchor.x - reach >= 12.0f && anchor.x + reach <= (float) getWidth() - 12.0f && anchor.y - halfH >= 12.0f
                       && anchor.y + halfH <= (float) getHeight() - 12.0f && ! H.outlineHidden[(size_t) k];
                H.sizeFrom = from; H.sizeTo = to; H.sizeAnchor = anchor; H.sizeSide = side;
            };
            bool fits = false;
            int chosen = -1;
            const int was = sizeSideOf[(size_t) i];
            const bool sizing = interaction.active() && interaction.currentPeak() == i && interaction.currentMode() == TerrainInteraction::Mode::width;
            place (was, fits);
            if (fits || sizing) chosen = was;   // (the reference keeps a tag where it is while it fits, and always while it is dragged)
            else for (int side : { 0, 1, 2 }) { place (side, fits); if (fits) { chosen = side; break; } }
            if (chosen < 0) place (0, fits);
            sizeSideOf[(size_t) i] = H.sizeSide;
            H.sizeBox = juce::Rectangle<float> (2.0f * (halfW + lead), 2.0f * halfH).withCentre (H.sizeAnchor);
            const float hw = juce::jmax (juce::GlyphArrangement::getStringWidth (fontOf (kTagSize * kCssToJuce), juce::String (pk.height, 2)) * 0.5f + kTagPad, halfH);
            H.heightBox = juce::Rectangle<float> (2.0f * hw, 2.0f * (halfH + kHeldArrowGap + kArrowSize)).withCentre (H.valuePt);
        }
    }
}

void TerrainView::ensureBuilt() const
{
    if (cacheValid && ! dirty) return;
    auto* self = const_cast<TerrainView*> (this);
    if (! self->offsetValid) self->fitOffset();
    self->renderCache (cacheScale);
}

juce::Point<float> TerrainView::summit (int i) const { ensureBuilt(); return i >= 0 && i < (int) handles.size() ? handles[(size_t) i].summitPt : juce::Point<float>(); }
juce::Point<float> TerrainView::handleDot (int i) const { ensureBuilt(); return i >= 0 && i < (int) handles.size() ? handles[(size_t) i].dot : juce::Point<float>(); }
juce::Point<float> TerrainView::heightTagCentre (int i) const { ensureBuilt(); return i >= 0 && i < (int) handles.size() ? handles[(size_t) i].valuePt : juce::Point<float>(); }
juce::Point<float> TerrainView::widthTagCentre (int i) const { ensureBuilt(); return i >= 0 && i < (int) handles.size() ? handles[(size_t) i].sizeAnchor : juce::Point<float>(); }

TerrainInteraction::Hit TerrainView::hitAt (juce::Point<float> p) const
{
    ensureBuilt();
    using K_ = TerrainInteraction::Kind;
    TerrainInteraction::Hit none;
    if (labelling) return none;
    const int sel = ed.selected;
    std::vector<int> order;
    if (sel >= 0 && sel < (int) handles.size()) order.push_back (sel);
    for (int i = 0; i < (int) handles.size(); ++i) if (i != sel) order.push_back (i);
    constexpr float reach = 6.0f;
    for (int i : order)
    {
        const auto& H = handles[(size_t) i];
        const bool isSel = i == sel && heldNow[(size_t) i] > 0.001f;
        if (isSel && H.heightBox.contains (p)) return { i, K_::height, false, H.top };
        const bool onDot = p.getDistanceFrom (H.dot) <= kDotR + reach;
        if (isSel && ! H.outline.empty())
        {
            const auto leadBox = juce::Rectangle<float> (H.sizeFrom, H.sizeTo).getUnion (H.sizeBox).expanded (16.0f);
            if (H.sizeBox.expanded (4.0f).contains (p) || (leadBox.contains (p) && segDist (H.sizeFrom, H.sizeTo, p) < 8.0f))
                return { i, K_::size, true, 0.0f };
        }
        if (onDot || (p.y >= H.stemTop.y - 2 && p.y <= H.summitPt.y + 2 && segDist (H.summitPt, H.stemTop, p) <= reach)) return { i, K_::move, false, H.top };
        if (isSel && ! H.outline.empty())
        {
            float best = 1e9f;
            for (size_t k = 0; k + 1 < H.outline.size(); ++k) best = juce::jmin (best, segDist (H.outline[k], H.outline[k + 1], p));
            if (best <= reach)
            {
                // the plane the outline lies on (its foot's height)
                float x = 0, z = 0, lift = 0;
                juce::ignoreUnused (x, z);
                const auto& t = ed.terrain();
                if (i < t.size()) lift = juce::jlimit (0.0f, 1.0f, heldWithout (t, t[i].x, t[i].z, i));
                return { i, K_::size, false, lift };
            }
        }
    }
    return none;
}

int TerrainView::handleAt (juce::Point<float> p) const
{
    ensureBuilt();
    for (int i = 0; i < (int) handles.size(); ++i) if (p.getDistanceFrom (handles[(size_t) i].dot) <= kDotR + 9.0f) return i;
    return -1;
}

int TerrainView::lineAt (juce::Point<float> p) const
{
    ensureBuilt();
    for (int i = 0; i < (int) handles.size(); ++i)
    {
        const auto& H = handles[(size_t) i];
        if (p.y >= H.stemTop.y && p.y <= H.summitPt.y && std::abs (p.x - H.summitPt.x) <= 6.0f) return i;
    }
    return -1;
}

int TerrainView::widthTagAt (juce::Point<float> p) const
{
    ensureBuilt();
    const int i = ed.selected;
    return i >= 0 && i < (int) handles.size() && ! handles[(size_t) i].outline.empty() && handles[(size_t) i].sizeBox.expanded (4.0f).contains (p) ? i : -1;
}

int TerrainView::bodyAt (juce::Point<float> p) const
{
    float x = 0, z = 0;
    if (! landAt (p, x, z)) return -1;
    const auto& t = ed.terrain();
    int best = -1; float bd = 1.0f;
    for (int i = 0; i < t.size(); ++i)
    {
        const float d = std::hypot ((x - t[i].x) / juce::jmax (0.005f, 2.0f * t[i].width), (z - t[i].z) / juce::jmax (0.005f, 2.0f * t[i].depth));
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

juce::Rectangle<int> TerrainView::changingBounds() const { return getLocalBounds(); }

void TerrainView::markDirty()
{
    dirty = true;
    repaint();
}

// ================================================================================================ labels
namespace
{
struct Flat
{
    std::vector<juce::Point<float>> pts;
    std::vector<float> cum;
    float total = 0;
    bool closed = false;
    juce::Point<float> at (float d, juce::Point<float>* tangent = nullptr) const
    {
        if (pts.size() < 2) return pts.empty() ? juce::Point<float>() : pts[0];
        if (closed && total > 0) { d = std::fmod (d, total); if (d < 0) d += total; }
        d = juce::jlimit (0.0f, total, d);
        const auto it = std::upper_bound (cum.begin(), cum.end(), d);
        size_t k = (size_t) juce::jlimit (1, (int) cum.size() - 1, (int) (it - cum.begin()));
        const float seg = cum[k] - cum[k - 1], f = seg > 1e-6f ? (d - cum[k - 1]) / seg : 0.0f;
        if (tangent != nullptr) { auto t = pts[k] - pts[k - 1]; const float l = t.getDistanceFromOrigin(); *tangent = l > 1e-6f ? t / l : juce::Point<float> (1, 0); }
        return pts[k - 1] + (pts[k] - pts[k - 1]) * f;
    }
};
Flat flatten (const std::vector<juce::Point<float>>& screen, bool closed)
{
    juce::Path p; addCurve (p, screen, closed);
    Flat f; f.closed = closed;
    for (juce::PathFlatteningIterator it (p, {}, 0.3f); it.next();)
    {
        if (f.pts.empty()) f.pts.push_back ({ it.x1, it.y1 });
        f.pts.push_back ({ it.x2, it.y2 });
    }
    f.cum.resize (f.pts.size());
    for (size_t k = 0; k < f.pts.size(); ++k) f.cum[k] = k == 0 ? 0.0f : f.cum[k - 1] + f.pts[k].getDistanceFrom (f.pts[k - 1]);
    f.total = f.cum.empty() ? 0.0f : f.cum.back();
    return f;
}
bool insideGround (const std::vector<juce::Point<float>>& poly, juce::Point<float> q)
{
    bool in = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
    {
        const auto a = poly[i], b = poly[j];
        if ((a.y > q.y) != (b.y > q.y) && q.x < (b.x - a.x) * (q.y - a.y) / (b.y - a.y) + a.x) in = ! in;
    }
    return in;
}
// what the auto labels and own labels keep: the ring's drawn curve
struct PlacedLabel { Flat path; float at = 0; juce::String text; int level = 0; bool own = false; int slot = -1; float rim = 1; };
std::map<const TerrainView*, std::vector<PlacedLabel>>& placedStore() { static std::map<const TerrainView*, std::vector<PlacedLabel>> m; return m; }
}
void placedStoreErase (const void* v) { placedStore().erase ((const TerrainView*) v); }

int TerrainView::labelCountShown() const
{
    auto it = placedStore().find (this);
    return it == placedStore().end() ? 0 : (int) it->second.size();
}

juce::String TerrainView::labelTextAt (int i) const
{
    auto it = placedStore().find (this);
    return it != placedStore().end() && i >= 0 && i < (int) it->second.size() ? it->second[(size_t) i].text : juce::String();
}

int TerrainView::ringUnder (juce::Point<float> p, float reach, float& at) const
{
    int best = -1; float bd = reach;
    for (int r = 0; r < (int) rings.size(); ++r)
    {
        const auto& ring = rings[(size_t) r];
        if (! ring.box.expanded (reach).contains (p) || ring.level <= 0) continue;
        float cum = 0;
        for (size_t k = 0; k + 1 < ring.screen.size(); ++k)
        {
            const juce::Line<float> seg (ring.screen[k], ring.screen[k + 1]);
            juce::Point<float> near;
            const float d = seg.getDistanceFromPoint (p, near);
            if (d < bd)
            {
                float u = 0, v = 0;
                if (toGround (near, (float) ring.level * kStep, u, v) && rimAt (u, v) >= 0.25f) { bd = d; best = r; at = cum + ring.screen[k].getDistanceFrom (near); }
            }
            cum += seg.getLength();
        }
    }
    return best;
}

void TerrainView::placeLabels (const Terrain& t)
{
    auto& out = placedStore()[this];
    out.clear();
    const auto font = fontOf (kLabelSize * kCssToJuce);
    const float pad = kTagPad * kLabelSize / kTagSize, halfH = kLabelSize * 0.5f + pad;
    std::vector<juce::Rectangle<float>> taken;
    if (toolbarOn) { taken.push_back (toolbarBounds().expanded (6.0f)); taken.push_back (shapesBounds().expanded (6.0f)); }
    for (auto& H : handles) taken.push_back (juce::Rectangle<float> (H.dot, H.summitPt).expanded (kDotR + 4.0f, 0.0f));
    if (! meters.empty()) taken.push_back ({ 0, 0, 220, 26.0f + 16.0f * (float) meters.size() });
    auto overlaps = [&] (juce::Rectangle<float> b, float m) { for (auto& r : taken) if (r.expanded (m).intersects (b)) return true; return false; };
    auto hiddenAt = [&] (juce::Point<float> sp, float h)
    {
        for (float e = h + kStep * 1.5f; e <= 1.0f; e += kStep * 0.5f)
        {
            float u = 0, v = 0;
            if (! toGround (sp, e, u, v)) return false;
            const float land = landStep (u, v);
            if (land > -1e8f && land >= e) return true;
        }
        return false;
    };
    auto rimOf = [&] (juce::Point<float> sp, float h) { float u = 0, v = 0; return toGround (sp, h, u, v) ? rimAt (u, v) : 0.0f; };
    // own labels first (the TAG tool): on the ring of their level nearest to where they were put
    for (int s = 0; s < Terrain::kMaxLabels; ++s)
    {
        const auto& L = t.labels[(size_t) s];
        if (L.level == 0) continue;
        const float lu = 2.0f * L.x - 1.0f, lv = 1.0f - 2.0f * L.z;
        int best = -1; float bd = 1e9f; size_t bk = 0;
        for (int r = 0; r < (int) rings.size(); ++r)
        {
            if (rings[(size_t) r].level != L.level) continue;
            const auto& gpts = rings[(size_t) r].ground;
            for (size_t k = 0; k < gpts.size(); ++k) { const float d = gpts[k].getDistanceFrom ({ lu, lv }); if (d < bd) { bd = d; best = r; bk = k; } }
        }
        if (best < 0) continue;
        PlacedLabel pl;
        pl.path = flatten (rings[(size_t) best].screen, rings[(size_t) best].closed);
        const auto target = rings[(size_t) best].screen[bk];
        float bestD = 1e9f;
        for (size_t k = 0; k < pl.path.pts.size(); ++k) { const float d = pl.path.pts[k].getDistanceFrom (target); if (d < bestD) { bestD = d; pl.at = pl.path.cum[k]; } }
        pl.text = juce::String::fromUTF8 (L.text); pl.level = L.level; pl.own = true; pl.slot = s;
        pl.rim = rimOf (target, (float) L.level * kStep);
        const float w = juce::GlyphArrangement::getStringWidth (font, pl.text);
        taken.push_back (juce::Rectangle<float> (w + 2 * pad, 2 * halfH).withCentre (pl.path.at (pl.at)));
        out.push_back (std::move (pl));
    }
    // auto heights: every 5th ring ("0", "50", "100" ...), one per height unless it rings a summit; the best spot on each:
    // clear of its neighbours, low on its ring (the front), near the middle, level, bright, not hidden, not on anything
    std::vector<int> cand;
    std::vector<uint8_t> holdsSummit (rings.size(), 0);
    for (int r = 0; r < (int) rings.size(); ++r)
    {
        const auto& ring = rings[(size_t) r];
        if (ring.level <= 0 || (ring.level - 1) % 5 != 0 || ! ring.closed) continue;
        for (auto& p : t) if (p.height > 0 && insideGround (ring.ground, { 2.0f * p.x - 1.0f, 1.0f - 2.0f * p.z })) { holdsSummit[(size_t) r] = 1; break; }
        cand.push_back (r);
    }
    std::sort (cand.begin(), cand.end(), [&] (int a, int b)
    {
        const auto& A = rings[(size_t) a]; const auto& B = rings[(size_t) b];
        if (A.level != B.level) return A.level < B.level;
        if (holdsSummit[(size_t) a] != holdsSummit[(size_t) b]) return holdsSummit[(size_t) a] > holdsSummit[(size_t) b];
        return A.length > B.length;
    });
    std::set<int> levelsWithSummit, levelsDone;
    for (int r : cand) if (holdsSummit[(size_t) r]) levelsWithSummit.insert (rings[(size_t) r].level);
    for (auto& o : out) levelsDone.insert (o.level);
    for (int r : cand)
    {
        const auto& ring = rings[(size_t) r];
        if (! (holdsSummit[(size_t) r] || (levelsWithSummit.count (ring.level) == 0 && levelsDone.count (ring.level) == 0))) continue;
        const juce::String text (((ring.level - 1) * 10));
        const float w = juce::GlyphArrangement::getStringWidth (font, text), half = w * 0.5f + pad;
        if (ring.length < 3.0f * 2.0f * half) continue;
        Flat f = flatten (ring.screen, ring.closed);
        const float h = (float) ring.level * kStep;
        float maxRim = 0;
        for (size_t k = 0; k < f.pts.size(); k += 4) maxRim = juce::jmax (maxRim, rimOf (f.pts[k], h));
        const float stepD = juce::jmax (6.0f, f.total / 72.0f);
        float bestScore = -1e9f, bestAt = -1;
        juce::Rectangle<float> bestBox;
        for (float d = 0; d < f.total; d += stepD)
        {
            juce::Point<float> tan;
            const auto c = f.at (d, &tan);
            const auto a = f.at (d - 8.0f), b = f.at (d + 8.0f);
            const float level = std::abs (b.x - a.x) / juce::jmax (1e-3f, a.getDistanceFrom (b));
            if (level < 0.64f) continue;
            juce::Rectangle<float> box;
            for (int q = 0; q <= 4; ++q) { const auto pt = f.at (d - half + 2.0f * half * (float) q / 4.0f); box = q == 0 ? juce::Rectangle<float> (pt, pt) : box.getUnion (juce::Rectangle<float> (pt, pt)); }
            box = box.expanded (halfH);
            if (box.getX() < 16 || box.getY() < 16 || box.getRight() > (float) getWidth() - 16 || box.getBottom() > (float) getHeight() - 16) continue;
            const float need = juce::jmin (0.55f, 0.6f * maxRim);
            bool bad = false;
            for (float dd : { d - half, d, d + half }) { const auto q = f.at (dd); if (hiddenAt (q, h) || rimOf (q, h) < need) { bad = true; break; } }
            if (bad || overlaps (box, 6.0f)) continue;
            float clear = 1e9f;
            for (const auto& o : rings)
                if (&o != &ring && std::abs (o.level - ring.level) <= 1 && o.box.expanded (2 * halfH).contains (c))
                    for (size_t k = 0; k + 1 < o.screen.size(); ++k) clear = juce::jmin (clear, segDist (o.screen[k], o.screen[k + 1], c));
            const float cl = juce::jmin (1.0f, clear / (2.0f * halfH));
            const float low = ring.box.getHeight() > 0 ? (c.y - ring.box.getY()) / ring.box.getHeight() : 0.5f;
            const float mid = 1.0f - juce::jmin (1.0f, std::abs (c.x - ring.box.getCentreX()) / juce::jmax (48.0f, ring.box.getWidth() * 0.5f));
            const float score = 1.5f * cl + low + 0.75f * mid + level + 0.5f * rimOf (c, h);
            if (score > bestScore) { bestScore = score; bestAt = d; bestBox = box; }
        }
        if (bestAt < 0) continue;
        PlacedLabel pl;
        pl.path = std::move (f); pl.at = bestAt; pl.text = text; pl.level = ring.level;
        pl.rim = rimOf (pl.path.at (bestAt), h);
        taken.push_back (bestBox);
        levelsDone.insert (ring.level);
        out.push_back (std::move (pl));
    }
}


namespace
{
void drawOnPath (juce::Graphics& g, const Flat& f, float at, const juce::String& text, const juce::Font& font, juce::Colour ink, juce::Colour paper, float alpha,
                 bool chip, juce::Colour chipFill, juce::Colour chipInk)
{
    if (f.pts.size() < 2 || alpha <= 0.01f) return;
    juce::GlyphArrangement ga;
    ga.addLineOfText (font, text, 0.0f, 0.0f);
    const float w = juce::jmax (1.0f, ga.getBoundingBox (0, -1, true).getWidth());
    const float size = font.getHeight() / kCssToJuce, pad = kTagPad * size / kTagSize, halfH = size * 0.5f + pad;
    juce::Point<float> tan;
    f.at (at, &tan);
    const float dir = tan.x < 0 ? -1.0f : 1.0f;   // read left to right
    // a sliver of ground cuts the ring under the words
    juce::Path cut;
    const int n = juce::jmax (2, (int) std::ceil ((w + 2 * pad) / 2.0f));
    for (int k = 0; k <= n; ++k)
    {
        const auto q = f.at (at + dir * (-(w * 0.5f + pad) + (w + 2 * pad) * (float) k / (float) n));
        if (k == 0) cut.startNewSubPath (q); else cut.lineTo (q);
    }
    g.setColour (paper.withMultipliedAlpha (alpha));
    if (chip)
    {
        g.strokePath (cut, juce::PathStrokeType (2.0f * halfH + 4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (chipFill.withMultipliedAlpha (alpha));
        g.strokePath (cut, juce::PathStrokeType (2.0f * halfH, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    else g.strokePath (cut, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    juce::Path glyphs;
    for (int k = 0; k < ga.getNumGlyphs(); ++k)
    {
        const auto& gl = ga.getGlyph (k);
        juce::Path one;
        gl.createPath (one);
        if (one.isEmpty()) continue;
        const float cx = gl.getLeft() + gl.getRight() * 0.0f + gl.getBounds().getWidth() * 0.5f;
        const float d = at + dir * (cx - w * 0.5f);
        juce::Point<float> tg;
        const auto pos = f.at (d, &tg);
        const float ang = std::atan2 (tg.y * dir, tg.x * dir);
        one.applyTransform (juce::AffineTransform::translation (-cx, size * 0.727f * 0.5f).rotated (ang).translated (pos));
        glyphs.addPath (one);
    }
    if (! chip)
    {
        g.setColour (paper.withMultipliedAlpha (alpha));
        g.strokePath (glyphs, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour ((chip ? chipInk : ink).withMultipliedAlpha (alpha));
    g.fillPath (glyphs);
}
}

// ================================================================================================ paint
void TerrainView::paint (juce::Graphics& g)
{
    const float scale = juce::jlimit (1.0f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (dirty || ! cacheValid || std::abs (scale - cacheScale) > 1e-3f) renderCache (scale);
    double tm = nowMs();
    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
    g.setOpacity (1.0f);
    if (cacheScale == 1.0f) g.drawImageAt (cache, 0, 0);
    else g.drawImageTransformed (cache, juce::AffineTransform::scale (1.0f / cacheScale));
    profileMs[3] = juce::jmin (profileMs[3], nowMs() - tm); tm = nowMs();
    drawOverlays (g);
    // the ring labels
    {
        const double now = nowMs();
        auto it = placedStore().find (this);
        if (it != placedStore().end())
        {
            int top = 0;
            for (auto& l : it->second) if (! l.own) top = juce::jmax (top, (l.level - 1) / 5);
            const float quiet = labelsQuiet ? juce::jlimit (0.0f, 1.0f, 1.0f - (float) ((now - landChangedAt) / 380.0)) : 1.0f;
            const float fadeT = labelsShownAt > 0 ? (float) ((now - labelsShownAt) / 1000.0) : 1.0f;
            for (auto& l : it->second)
            {
                float a = 1.0f;
                if (! l.own && openT < 1.0f) continue;
                if (! l.own)
                {
                    const int rung = (l.level - 1) / 5;
                    const float i0 = top > 0 ? (float) juce::jmin (rung, top) / (float) top * 0.65f : 0.0f;
                    const float e = 1.0f - std::pow (1.0f - juce::jlimit (0.0f, 1.0f, fadeT), 3.5f);
                    a = juce::jlimit (0.0f, 1.0f, (e - i0) / 0.35f) * quiet;
                }
                // its colour: the ring's (faded towards the shore), the accent where the chosen mountain lights it
                float lit = 0.0f;
                {
                    float u = 0, v = 0;
                    if (toGround (l.path.at (l.at), (float) l.level * kStep, u, v) && N > 1)
                    {
                        const int gi = juce::jlimit (0, N - 1, juce::roundToInt ((u + 1.0f) * 0.5f * (float) (N - 1)));
                        const int gj = juce::jlimit (0, N - 1, juce::roundToInt ((1.0f - v) * 0.5f * (float) (N - 1)));
                        if (! litField.empty()) lit = litField[(size_t) gj * (size_t) N + (size_t) gi];
                    }
                }
                float held = 0.0f;
                for (auto hv : heldNow) held += hv;
                const auto inkBase = palette.ground.interpolatedWith (palette.ring.withAlpha (1.0f), l.rim * (1.0f - kDim * juce::jmin (1.0f, held)));
                const auto ink = inkBase.interpolatedWith (palette.accent, lit * l.rim);
                const bool editing = l.own && l.slot == editLabel;
                drawOnPath (g, l.path, l.at, editing ? (typing + (typedAny ? "" : "")) : l.text, fontOf ((editing ? kTagSize : kLabelSize) * kCssToJuce), ink, palette.ground, a,
                            editing, palette.handle, palette.onHandle);
            }
        }
    }
    profileMs[4] = juce::jmin (profileMs[4], nowMs() - tm); tm = nowMs();
    drawHandles (g);
    profileMs[5] = juce::jmin (profileMs[5], nowMs() - tm); tm = nowMs();
    drawMeters (g);
    if (toolbarOn) drawToolbar (g);
}

void TerrainView::drawOverlays (juce::Graphics& g)
{
    if (playZ >= 0.0f && N > 1)
    {
        juce::Path p;
        const float v = 1.0f - 2.0f * playZ;
        bool on = false;
        for (int i = 0; i <= 120; ++i)
        {
            const float u = -1.0f + 2.0f * (float) i / 120.0f;
            if (u * u + v * v > 0.98f) { on = false; continue; }
            const auto s = toScreen (u, v, juce::jmax (0.0f, landStep (u, v)));
            if (! on) p.startNewSubPath (s); else p.lineTo (s);
            on = true;
        }
        g.setColour (palette.handle.withAlpha (0.45f));
        g.strokePath (p, juce::PathStrokeType (1.0f));
    }
    if (analyzer && spectrum.size() > 2)
    {
        juce::Path p;
        for (size_t i = 0; i < spectrum.size(); ++i)
        {
            const float x = (float) i / (float) (spectrum.size() - 1);
            const float hgt = juce::jlimit (0.0f, 1.0f, (spectrum[i] + 72.0f) / 72.0f) * 0.85f;
            const auto s = project (x, 0.02f, hgt);
            if (i == 0) p.startNewSubPath (s); else p.lineTo (s);
        }
        juce::Path dashed;
        const float dashes[] = { 3.0f, 3.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, p, dashes, 2);
        g.setColour (palette.text.withAlpha (0.45f));
        g.fillPath (dashed);
    }
}

juce::Rectangle<float> TerrainView::shapesBounds() const
{
    if (! toolbarOn) return {};
    return { (float) getWidth() - 16.0f - 84.0f, 16.0f, 84.0f, 24.0f };
}

void TerrainView::pickShape (const juce::String& name)
{
    ed.selected = -1;
    ed.set (makeShape ? makeShape (name) : shapePreset (name.toRawUTF8(), (uint32_t) ++randomCount));
    if (onShape) onShape (name);
    markDirty();
}

void TerrainView::drawMeters (juce::Graphics& g)
{
    // SHAPES
    if (const auto r = shapesBounds(); ! r.isEmpty())
    {
        g.setColour (hoverButton == 1000 ? palette.buttonHi : palette.button);
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (palette.handle);
        g.setFont (fontOf (11.0f, true));
        g.drawText ("SHAPES", r.withTrimmedRight (16.0f), juce::Justification::centred, false);
        juce::Path tri; tri.addTriangle (r.getRight() - 20.0f, r.getCentreY() - 2.0f, r.getRight() - 12.0f, r.getCentreY() - 2.0f, r.getRight() - 16.0f, r.getCentreY() + 2.5f);
        g.fillPath (tri);
    }
    if (meters.empty()) return;
    // "what this is doing": the parameters the shape is pushing, small, top left
    const float x = 16.0f, w = 74.0f;
    float y = 16.0f;
    const auto f = fontOf (10.0f);
    g.setFont (f);
    for (size_t i = 0; i < meters.size() && i < 5; ++i)
    {
        const auto& m = meters[i];
        g.setColour (palette.dim);
        g.drawText (juce::String (m.name), juce::Rectangle<float> (x, y, 120.0f, 12.0f), juce::Justification::centredLeft, false);
        const auto bar = juce::Rectangle<float> (x + 122.0f, y + 4.5f, w, 3.0f);
        g.setColour (palette.ring.withAlpha (0.14f));
        g.fillRoundedRectangle (bar, 1.5f);
        g.setColour (palette.handle.withAlpha (0.9f));
        g.fillRoundedRectangle (bar.withWidth (w * juce::jlimit (0.0f, 1.0f, m.value)), 1.5f);
        y += 16.0f;
    }
}

void TerrainView::drawTag (juce::Graphics& g, juce::Point<float> c, const juce::String& text, bool vertical, float chip, float arrows, juce::Colour accent, float alpha)
{
    if (alpha <= 0.01f) return;
    const auto font = fontOf (kTagSize * kCssToJuce);
    const float tw = juce::GlyphArrangement::getStringWidth (font, text);
    const float halfH = kTagSize * 0.5f + kTagPad, halfW = juce::jmax (tw * 0.5f + kTagPad, halfH);
    const auto paper = palette.ground;
    // arrows (the reference's: a little triangle each side, nudged out while held)
    if (arrows > 0.001f)
    {
        const float sz = kArrowSize * (0.5f + 0.5f * arrows), base = sz * (4.2f / 7.7f), gap = kArrowGap + (kHeldArrowGap - kArrowGap) * chip;
        juce::Path tri;
        if (vertical)
            for (float s : { -1.0f, 1.0f })
            {
                const float y = c.y + s * (halfH + gap);
                tri.addTriangle (c.x - base, y, c.x + base, y, c.x, y + s * sz);
            }
        else
        {
            const float ext = tw * 0.5f + kTagPad;
            for (float s : { -1.0f, 1.0f })
            {
                const float x = c.x + s * (ext + gap);
                tri.addTriangle (x, c.y - base, x, c.y + base, x + s * sz, c.y);
            }
        }
        g.setColour (paper.withMultipliedAlpha (alpha * juce::jmin (1.0f, arrows / 0.5f)));
        g.strokePath (tri, juce::PathStrokeType (8.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (accent.withMultipliedAlpha (alpha * juce::jmin (1.0f, arrows / 0.5f)));
        g.fillPath (tri);
        g.strokePath (tri, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    juce::GlyphArrangement ga;
    ga.addLineOfText (font, text, c.x - tw * 0.5f, c.y + kTagSize * 0.727f * 0.5f);
    juce::Path glyphs;
    ga.createPath (glyphs);
    if (chip < 1.0f)
    {
        g.setColour (paper.withMultipliedAlpha (alpha));
        g.strokePath (glyphs, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (accent.withMultipliedAlpha (alpha));
        g.fillPath (glyphs);
    }
    if (chip > 0.0f)
    {
        const auto box = juce::Rectangle<float> (2.0f * halfW, 2.0f * halfH).withCentre (c);
        g.setColour (paper.withMultipliedAlpha (alpha * chip));
        g.drawRoundedRectangle (box, kTagRadius, 4.0f);
        g.setColour (accent.withMultipliedAlpha (alpha * chip));
        g.fillRoundedRectangle (box, kTagRadius);
        g.setColour (palette.onHandle.withMultipliedAlpha (alpha * chip));
        g.fillPath (glyphs);
    }
}

void TerrainView::drawHandles (juce::Graphics& g)
{
    if (labelling) return;   // (the TAG tool: the land and its labels only, as on the reference)
    const Terrain t = drawn();
    const int n = juce::jmin (t.size(), (int) handles.size());
    const float ui = uiScale();
    const float lineW = juce::jmax (1.25f, 2.0f * ui);
    const auto mode = interaction.currentMode();
    const int grabbed = interaction.active() ? interaction.currentPeak() : -1;
    float heldSum = 0.0f;
    for (int i = 0; i < n; ++i) heldSum += heldNow[(size_t) i];
    // stems and dots first (all of them), then the chosen one's outline, tags
    for (int i = 0; i < n; ++i)
    {
        const auto& H = handles[(size_t) i];
        if (std::abs (t[i].height) < 0.005f) continue;
        const float others = heldSum - heldNow[(size_t) i];
        const float O = 1.0f - kDim * juce::jmin (1.0f, others);
        const auto c = palette.ground.interpolatedWith (palette.handle, O);
        g.setColour (c);
        for (auto& [y0, y1] : H.stemVisible) g.fillRect (H.foot.x - lineW * 0.5f, y1, lineW, y0 - y1);
        if (H.dotVisible)
        {
            const float r = kDotR + (grabbed == i && mode == TerrainInteraction::Mode::move ? 1.0f : 0.0f);
            g.setColour (palette.ground);
            g.fillEllipse (juce::Rectangle<float> (2 * (r + 3.0f), 2 * (r + 3.0f)).withCentre (H.dot));
            g.setColour (c);
            g.fillEllipse (juce::Rectangle<float> (2 * r, 2 * r).withCentre (H.dot));
        }
    }
    for (int i = 0; i < n; ++i)
    {
        const auto& H = handles[(size_t) i];
        const float E = heldNow[(size_t) i];
        if (E <= 0.001f) continue;
        const float others = heldSum - E;
        const float O = (1.0f - kDim * juce::jmin (1.0f, others)) * E;
        // the outline at its base, where it is not hidden, and the lead to the size tag
        if (! H.outline.empty())
        {
            juce::Path run;
            std::vector<juce::Point<float>> seg;
            auto flush = [&] { if (seg.size() > 1) addCurve (run, seg, false); seg.clear(); };
            for (size_t k = 0; k < H.outline.size(); ++k) { if (H.outlineHidden[k]) flush(); else seg.push_back (H.outline[k]); }
            flush();
            g.setColour (palette.accent.withMultipliedAlpha (E));
            g.strokePath (run, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (palette.handle.withMultipliedAlpha (E));
            g.drawLine ({ H.sizeFrom, H.sizeTo }, lineW);
        }
        const bool editingH = editPeak == i && editKind == TerrainInteraction::Kind::height;
        const bool editingS = editPeak == i && editKind == TerrainInteraction::Kind::size;
        const bool chipH = (grabbed == i && (mode == TerrainInteraction::Mode::height || mode == TerrainInteraction::Mode::create || mode == TerrainInteraction::Mode::grit))
                           || (hoverPeak == i && hoverKind == TerrainInteraction::Kind::height) || editingH;
        const bool chipS = (grabbed == i && mode == TerrainInteraction::Mode::width) || (hoverPeak == i && hoverKind == TerrainInteraction::Kind::size && hoverButton < 0) || editingS;
        const auto& live = ed.terrain();
        const Peak pk = i < live.size() ? live[i] : t[i];
        const juce::String hText = editingH ? editText : (mode == TerrainInteraction::Mode::grit && grabbed == i ? interaction.liveText : juce::String (pk.height, 2));
        drawTag (g, H.valuePt, hText, true, chipH ? 1.0f : 0.0f, E, palette.handle, O);
        drawTag (g, H.sizeAnchor, editingS ? editText : juce::String (pk.r(), 2), false, chipS ? 1.0f : 0.0f, E, palette.handle, O);
        if (readouts)
        {
            Band b;
            const Peak d = t.peakAt (i, t[i].z);
            b.hz = xToHz (d.x, range); b.gainDb = d.height * range.maxDb; b.q = qFromWidth (d.width, range);
            g.setColour (palette.dim.withMultipliedAlpha (E));
            g.setFont (fontOf (10.0f));
            g.drawText (juce::String (describeBand (b)), juce::Rectangle<float> (200.0f, 14.0f).withPosition (H.valuePt.translated (26.0f, -7.0f)), juce::Justification::centredLeft, false);
        }
    }
    juce::ignoreUnused (ui);
}

// ================================================================================================ toolbar
bool TerrainView::buttonShown (Button b) const
{
    if (! toolbarOn) return false;
    switch (b)
    {
        case Button::randomize: case Button::labels: case Button::reset: case Button::more: return true;
        default: return extrasOn;
    }
}

juce::Rectangle<float> TerrainView::toolbarBounds() const
{
    const float bottom = getHeight() >= 600 ? 40.0f : 20.0f;
    const float w = 3 * 32.0f + 2 * 6.0f;
    return { (float) getWidth() * 0.5f - w * 0.5f, (float) getHeight() - bottom - 32.0f, w, 32.0f };
}

juce::Rectangle<float> TerrainView::buttonBounds (Button b) const
{
    if (! buttonShown (b)) return {};
    const auto tb = toolbarBounds();
    switch (b)
    {
        case Button::randomize: return { tb.getX(), tb.getY(), 32, 32 };
        case Button::labels:    return { tb.getX() + 38, tb.getY(), 32, 32 };
        case Button::reset:     return { tb.getX() + 76, tb.getY(), 32, 32 };
        case Button::more:      return { (float) getWidth() - 16.0f - 32.0f, tb.getY(), 32, 32 };
        default: break;
    }
    // ours, a row above "..." at the right: LISTEN SMOOTH FREEZE EVOLVE A/B ANALYZER UNDO REDO
    static const Button order[] = { Button::listen, Button::smooth, Button::freeze, Button::evolve, Button::ab, Button::analyzer, Button::undo, Button::redo };
    int k = 0;
    for (int q = 0; q < 8; ++q) if (order[q] == b) k = q;
    const float right = (float) getWidth() - 16.0f;
    return { right - (float) (8 - k) * 38.0f + 6.0f, tb.getY() - 38.0f, 32, 32 };
}

juce::String TerrainView::buttonName (Button b) const
{
    switch (b)
    {
        case Button::randomize: return "Shuffle peaks";
        case Button::listen:    return isListening && isListening() ? "STOP" : "LISTEN";
        case Button::smooth:    return "SMOOTH  (hold Alt: ROUGHEN)";
        case Button::freeze:    return ed.terrain().frozen ? "FREEZE is on: one moment for all time" : "FREEZE";
        case Button::evolve:    return ed.terrain().evolve > 0 ? "EVOLVE is on: a slow drift" : "EVOLVE";
        case Button::ab:        return ed.activeSlot() == 0 ? "A / B  (this is A)" : "A / B  (this is B)";
        case Button::labels:    return "Label lines (L)";
        case Button::analyzer:  return analyzer ? "ANALYZER: hide" : "ANALYZER: show the real spectrum";
        case Button::undo:      return "UNDO";
        case Button::redo:      return "REDO";
        case Button::reset:     return "Clear peaks";
        case Button::more:      return extrasOn ? "Hide more" : "More: LISTEN, SMOOTH, FREEZE, EVOLVE, A/B, UNDO";
        case Button::count:     break;
    }
    return {};
}

void TerrainView::drawIcon (juce::Graphics& g, Button b, juce::Rectangle<float> r, juce::Colour c) const
{
    g.setColour (c);
    auto glyph = [&] (const char* d)
    {
        // the reference's 14 px glyph, centred
        g.fillPath (iconPath (d), juce::AffineTransform::translation (r.getCentreX() - 7.0f, r.getCentreY() - 7.0f));
    };
    const auto k = r.reduced (10.0f);
    const float x0 = k.getX(), y0 = k.getY(), x1 = k.getRight(), y1 = k.getBottom(), xm = k.getCentreX(), ym = k.getCentreY();
    juce::PathStrokeType st (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    juce::Path p;
    switch (b)
    {
        case Button::randomize: glyph (kShufflePath); return;
        case Button::labels:    glyph (kTagPath); return;
        case Button::reset:     glyph (kClearPath); return;
        case Button::more:
            for (int q = -1; q <= 1; ++q) g.fillEllipse (juce::Rectangle<float> (2.6f, 2.6f).withCentre ({ r.getCentreX() + 4.5f * (float) q, r.getCentreY() }));
            return;
        case Button::listen:
            if (isListening && isListening()) { g.fillRect (k.reduced (1.0f)); return; }
            p.addTriangle (x0 + 1.5f, y0, x1, ym, x0 + 1.5f, y1); g.fillPath (p); return;
        case Button::smooth:
            p.startNewSubPath (x0, ym);
            for (int i = 1; i <= 12; ++i) { const float u = (float) i / 12.0f; p.lineTo (x0 + u * k.getWidth(), ym - 3.0f * std::sin (u * 6.283f)); }
            g.strokePath (p, st); return;
        case Button::freeze:
            for (int a = 0; a < 3; ++a)
            {
                const float ang = (float) a * 1.0472f;
                p.startNewSubPath (xm - std::cos (ang) * 5.5f, ym - std::sin (ang) * 5.5f); p.lineTo (xm + std::cos (ang) * 5.5f, ym + std::sin (ang) * 5.5f);
            }
            g.strokePath (p, st); return;
        case Button::evolve:
            p.addCentredArc (xm - 2.5f, ym, 2.8f, 2.8f, 0.0f, 0.4f, 6.0f, true);
            p.addCentredArc (xm + 2.5f, ym, 2.8f, 2.8f, 0.0f, 3.5f, 9.2f, true);
            g.strokePath (p, st); return;
        case Button::ab:
            g.setFont (fontOf (11.0f, true));
            g.drawText (ed.activeSlot() == 0 ? "A" : "B", r, juce::Justification::centred);
            return;
        case Button::analyzer:
            for (int i = 0; i < 4; ++i) { const float hh = (float) (2 + (i * 7) % 9); g.fillRect (x0 + (float) i * 3.0f, y1 - hh, 1.6f, hh); }
            return;
        case Button::undo: case Button::redo:
        {
            const float d = b == Button::undo ? 1.0f : -1.0f;
            p.addCentredArc (xm, ym + 1.0f, 4.5f, 4.0f, 0.0f, d > 0 ? -1.6f : 1.6f, d > 0 ? 1.4f : -1.4f, true);
            g.strokePath (p, st);
            juce::Path hh;
            const float axx = xm - d * 0.5f, ayy = ym - 3.0f;
            hh.startNewSubPath (axx + d * 2.5f, ayy - 2.5f); hh.lineTo (axx, ayy); hh.lineTo (axx + d * 2.5f, ayy + 2.5f);
            g.strokePath (hh, st); return;
        }
        case Button::count: break;
    }
}

void TerrainView::drawToolbar (juce::Graphics& g)
{
    // the reference's toolbar: dark rounded squares (radius 10), accent glyphs; it rises in once the land has opened
    const double now = nowMs();
    const auto& t = ed.terrain();
    const auto bg = palette.button, bgHi = palette.buttonHi, ink = palette.handle;
    const float shownAge = openT >= 0.6f ? (float) (now - (openStart + kOpenDelayMs + 0.42 * kOpenMs)) : -1.0f;
    for (int i = 0; i < (int) Button::count; ++i)
    {
        const auto b = (Button) i;
        auto r = buttonBounds (b);
        if (r.isEmpty()) continue;
        const int order = b == Button::randomize ? 0 : (b == Button::labels ? 1 : (b == Button::reset ? 2 : 3));
        float a = 1.0f, dy = 0.0f;
        if (openStart > 1.0 && shownAge < 800.0f)
        {
            const float u = juce::jlimit (0.0f, 1.0f, (shownAge - 50.0f * (float) order) / 300.0f);
            a = u; dy = 6.0f * (1.0f - bezierEase (juce::jlimit (0.0f, 1.0f, (shownAge - 50.0f * (float) order) / 450.0f), 0.23f, 1.0f, 0.32f, 1.0f));
            if (a <= 0.0f) continue;
        }
        r = r.translated (0.0f, dy);
        const bool pressed = (b == Button::labels && labelling) || (b == Button::freeze && t.frozen) || (b == Button::evolve && t.evolve > 0)
                             || (b == Button::analyzer && analyzer) || (b == Button::listen && isListening && isListening()) || (b == Button::more && extrasOn);
        const bool disabled = (b == Button::randomize && labelling) || (b == Button::reset && (labelling || t.empty()))
                              || (b == Button::undo && ! undoAvailable()) || (b == Button::redo && ! redoAvailable());
        if (pressed)
        {
            g.setColour (ink.withMultipliedAlpha (a)); g.fillRoundedRectangle (r, 10.0f);
            drawIcon (g, b, r, bg.withAlpha (a));
        }
        else
        {
            g.setColour ((i == hoverButton && ! disabled ? bgHi : bg).withMultipliedAlpha (a));
            g.fillRoundedRectangle (r, 10.0f);
            drawIcon (g, b, r, (disabled ? bg.interpolatedWith (ink, 0.28f) : ink).withMultipliedAlpha (a));
        }
    }
}

void TerrainView::toggleEvolve() { ed.apply ([] (Terrain& t) { t.evolve = t.evolve > 0.0f ? 0.0f : 0.6f; }); }

void TerrainView::toggleFreeze()
{
    const float z = playZ >= 0.0f ? playZ : (ed.selected >= 0 && ed.selected < ed.terrain().size() ? ed.terrain()[ed.selected].z : 0.5f);
    ed.apply ([z] (Terrain& t) { t.frozen = ! t.frozen; t.freezeZ = z; });
}

bool TerrainView::undoAvailable() const { return canUndoHook ? canUndoHook() : ed.canUndo(); }
bool TerrainView::redoAvailable() const { return canRedoHook ? canRedoHook() : ed.canRedo(); }
bool TerrainView::doUndo() { const bool r = onUndo ? onUndo() : ed.undo(); markDirty(); return r; }
bool TerrainView::doRedo() { const bool r = onRedo ? onRedo() : ed.redo(); markDirty(); return r; }

void TerrainView::press (Button b, juce::ModifierKeys mods)
{
    switch (b)
    {
        case Button::randomize:
        {
            if (labelling) break;
            // the reference's shuffle: as many mountains as now plus two (up to six; Shift: up to ten), all new; ours turns
            // some into spires, plateaus and knolls
            const auto cur = ed.terrain();
            const uint32_t seed = (uint32_t) juce::Random::getSystemRandom().nextInt (0x7fffffff) ^ (uint32_t) (++randomCount * 2654435761u);
            ed.selected = -1;
            ed.set (shuffleTerrain (cur, seed, true, mods.isShiftDown() ? 10 : 6));
            break;
        }
        case Button::labels:   setLabelMode (! labelling); break;
        case Button::reset:    if (! labelling) { ed.selected = -1; ed.apply ([] (Terrain& t) { t.clear(); }); } break;
        case Button::more:     extrasOn = ! extrasOn; break;
        case Button::listen:   if (onListen) onListen(); break;
        case Button::smooth:   ed.apply ([alt = mods.isAltDown()] (Terrain& t) { smooth (t, alt ? -0.5f : 0.5f); }); break;
        case Button::freeze:   toggleFreeze(); break;
        case Button::evolve:   toggleEvolve(); break;
        case Button::ab:       ed.toggleAB(); break;
        case Button::analyzer: setAnalyzerVisible (! analyzer); break;
        case Button::undo:     doUndo(); break;
        case Button::redo:     doRedo(); break;
        case Button::count:    break;
    }
    markDirty();
}

// ================================================================================================ the TAG tool
void TerrainView::setLabelMode (bool on)
{
    if (on == labelling) return;
    if (editLabel >= 0 && ed.inGesture()) ed.endGesture();
    labelling = on; editLabel = -1; typing = {};
    ed.select (-1);
    markDirty();
}

// ================================================================================================ mouse + keys
static int buttonUnder (const TerrainView& v, juce::Point<float> p)
{
    for (int i = 0; i < (int) TerrainView::Button::count; ++i)
    {
        const auto r = v.buttonBounds ((TerrainView::Button) i);
        if (! r.isEmpty() && r.expanded (3.0f, 6.0f).contains (p)) return i;
    }
    return -1;
}

void TerrainView::mouseMove (const juce::MouseEvent& e)
{
    int hb = buttonUnder (*this, e.position);
    if (hb < 0 && shapesBounds().contains (e.position)) hb = 1000;
    const auto h = hitAt (e.position);
    if (hb != hoverButton || h.peak != hoverPeak || h.kind != hoverKind)
    {
        hoverButton = hb; hoverPeak = hb >= 0 ? -1 : h.peak; hoverKind = hb >= 0 ? TerrainInteraction::Kind::none : h.kind;
        refreshLights (nowMs());
        markDirty();
    }
    if (hb == 1000) setTooltip ("SHAPE PRESETS: a whole landscape of one character"); else if (hb >= 0) setTooltip (buttonName ((Button) hb)); else setTooltip ({});
    float x, z, at = 0;
    const bool onLand = landAt (e.position, x, z);
    using K_ = TerrainInteraction::Kind;
    setMouseCursor (hb >= 0 ? juce::MouseCursor::PointingHandCursor
                    : labelling ? (ringUnder (e.position, 18.0f, at) >= 0 ? juce::MouseCursor::IBeamCursor : juce::MouseCursor::NormalCursor)
                    : h.kind == K_::height ? juce::MouseCursor::UpDownResizeCursor
                    : h.kind == K_::size ? juce::MouseCursor::LeftRightResizeCursor
                    : h.kind == K_::move ? juce::MouseCursor::DraggingHandCursor
                    : (onLand && ed.selected < 0 ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor));
}

void TerrainView::mouseExit (const juce::MouseEvent&)
{
    hoverButton = -1; hoverPeak = -1; hoverKind = TerrainInteraction::Kind::none;
    refreshLights (nowMs());
    repaint();
}

void TerrainView::mouseDown (const juce::MouseEvent& e)
{
    if (const int b = buttonUnder (*this, e.position); b >= 0) { pressingButton = true; press ((Button) b, e.mods); return; }
    if (shapesBounds().contains (e.position))
    {
        pressingButton = true;
        juce::PopupMenu m;
        auto names = shapeNames;
        if (names.isEmpty()) for (int i = 0; i < shapePresetCount(); ++i) names.add (shapePresetName (i));
        for (int i = 0; i < names.size(); ++i) m.addItem (i + 1, names[i]);
        juce::Component::SafePointer<TerrainView> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (shapesBounds().toNearestInt())),
                         [safe, names] (int r) { if (safe != nullptr && r > 0) safe->pickShape (names[r - 1]); });
        return;
    }
    pressingButton = false;
    // a click elsewhere ends typing into a tag
    if (editPeak >= 0) { editPeak = -1; editKind = TerrainInteraction::Kind::none; }
    if (labelling)
    {
        // the TAG tool: a press on one of your labels edits it; on a ring writes a new one there; elsewhere stops
        if (editLabel >= 0 && ed.inGesture()) ed.endGesture();
        editLabel = -1;
        auto it = placedStore().find (this);
        if (it != placedStore().end())
            for (auto& l : it->second)
                if (l.own && l.path.at (l.at).getDistanceFrom (e.position) < 14.0f)
                {
                    editLabel = l.slot; typing = l.text; typedAny = false;
                    ed.beginGesture();
                    markDirty();
                    return;
                }
        float at = 0;
        const int r = ringUnder (e.position, e.source.isTouch() ? 28.0f : 18.0f, at);
        if (r >= 0 && ed.terrain().labelCount() < Terrain::kMaxLabels)
        {
            const auto& ring = rings[(size_t) r];
            float u = 0, v = 0;
            toGround (e.position, (float) ring.level * kStep, u, v);
            RingLabel L;
            L.x = 0.5f + 0.5f * u; L.z = 0.5f - 0.5f * v; L.level = (int8_t) ring.level;
            juce::String ((ring.level - 1) * 10).copyToUTF8 (L.text, sizeof (L.text));
            ed.beginGesture();
            ed.live().addLabel (L);
            for (int s = 0; s < Terrain::kMaxLabels; ++s) if (ed.terrain().labels[(size_t) s] == L) editLabel = s;
            typing = juce::String::fromUTF8 (L.text); typedAny = false;
            ed.changed();
        }
        markDirty();
        return;
    }
    interaction.dragThreshold = e.source.isTouch() ? 10.0f : 3.0f;
    interaction.mouseDown (e.position, e.mods);
    refreshLights (nowMs());
    markDirty();
}

void TerrainView::mouseDrag (const juce::MouseEvent& e)
{
    if (pressingButton || labelling) return;
    interaction.mouseDrag (e.position, e.mods);
    if (onStatus && interaction.liveText.isNotEmpty()) onStatus (interaction.liveText);
}

void TerrainView::mouseUp (const juce::MouseEvent& e)
{
    if (pressingButton) { pressingButton = false; return; }
    if (labelling) return;
    interaction.mouseUp (e.position, e.mods);
    // a click on a tag (desktop): type its number (Enter sets it, the arrow keys nudge it)
    if (interaction.clickedTag() != TerrainInteraction::Kind::none && ! e.source.isTouch())
    {
        editPeak = ed.selected; editKind = interaction.clickedTag(); editTyped = false;
        if (editPeak >= 0 && editPeak < ed.terrain().size())
            editText = juce::String (editKind == TerrainInteraction::Kind::size ? ed.terrain()[editPeak].r() : ed.terrain()[editPeak].height, 2);
        grabKeyboardFocus();
    }
    refreshLights (nowMs());
    markDirty();
}

void TerrainView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (buttonUnder (*this, e.position) >= 0 || labelling) return;
    const auto h = hitAt (e.position);
    if (h.peak >= 0 && h.kind == TerrainInteraction::Kind::move) interaction.doubleClick (e.position);
}

void TerrainView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) { interaction.wheel (e.position, w.deltaY); }
void TerrainView::mouseMagnify (const juce::MouseEvent& e, float scale) { interaction.magnify (e.position, scale); }

bool TerrainView::keyPressed (const juce::KeyPress& k)
{
    const auto ch = k.getTextCharacter();
    // typing a label
    if (labelling && editLabel >= 0)
    {
        auto commit = [&] (bool keep)
        {
            auto& L = ed.live().labels[(size_t) editLabel];
            if (! keep || typing.trim().isEmpty()) L = RingLabel();
            else typing.copyToUTF8 (L.text, sizeof (L.text));
            ed.changed();
            ed.endGesture();
            editLabel = -1; typing = {};
            markDirty();
        };
        if (k == juce::KeyPress::returnKey) { commit (true); return true; }
        if (k == juce::KeyPress::escapeKey) { commit (true); return true; }
        if (k == juce::KeyPress::deleteKey && ! typedAny) { commit (false); return true; }
        if (k == juce::KeyPress::backspaceKey) { typing = typedAny ? typing.dropLastCharacters (1) : juce::String(); typedAny = true; }
        else if (ch >= 32 && ch != 127 && ! k.getModifiers().isCommandDown()) { typing = (typedAny ? typing : juce::String()) + juce::String::charToString (ch); typedAny = true; }
        else return false;
        typing.copyToUTF8 (ed.live().labels[(size_t) editLabel].text, sizeof (RingLabel::text));
        ed.changed();
        markDirty();
        return true;
    }
    // typing a tag's number
    if (editPeak >= 0 && editPeak < ed.terrain().size())
    {
        const bool size = editKind == TerrainInteraction::Kind::size;
        auto setValue = [&] (float v)
        {
            const int i = editPeak;
            ed.apply ([i, v, size] (Terrain& t)
            {
                if (size) { const float r = juce::jlimit (4.0f * kMinWidth, 4.0f * kMaxWidth, v), k = r / juce::jmax (1e-4f, t[i].r()); t[i].width = r * 0.25f; t[i].depth *= k; }
                else t[i].height = juce::jlimit (kMinHeight, kMaxHeight, v);
            });
        };
        if (k == juce::KeyPress::returnKey) { if (editTyped) setValue (editText.getFloatValue()); editPeak = -1; markDirty(); return true; }
        if (k == juce::KeyPress::escapeKey) { editPeak = -1; markDirty(); return true; }
        if (k == juce::KeyPress::upKey || k == juce::KeyPress::downKey)
        {
            const float step = (k == juce::KeyPress::upKey ? 1.0f : -1.0f) * (k.getModifiers().isShiftDown() ? 0.1f : 0.01f);
            const float cur = size ? ed.terrain()[editPeak].r() : ed.terrain()[editPeak].height;
            setValue (cur + step);
            editText = juce::String (size ? ed.terrain()[editPeak].r() : ed.terrain()[editPeak].height, 2); editTyped = false;
            markDirty(); return true;
        }
        if (k == juce::KeyPress::backspaceKey) { editText = editTyped ? editText.dropLastCharacters (1) : juce::String(); editTyped = true; markDirty(); return true; }
        if ((ch >= '0' && ch <= '9') || ch == '.' || ch == '-') { editText = (editTyped ? editText : juce::String()) + juce::String::charToString (ch); editTyped = true; markDirty(); return true; }
    }
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) return interaction.removeSelected();
    if (k == juce::KeyPress::escapeKey) { if (labelling) setLabelMode (false); else ed.select (-1); return true; }
    if (k == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)) return doUndo();
    if (k == juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)) return doRedo();
    if (! k.getModifiers().isAnyModifierKeyDown() && (ch == 'l' || ch == 'L')) { setLabelMode (! labelling); return true; }
    if (! k.getModifiers().isAnyModifierKeyDown() && (ch == 's' || ch == 'S')) { setLabelMode (false); return true; }
    return false;
}

// ================================================================================================ animation + tests
void TerrainView::timerCallback()
{
    const double now = nowMs();
    // the opening: the land tilts up from a flat map (after 0.3 s, over 1.8 s, eased in and out)
    if (openT < 1.0f && openStart > 1.0)
    {
        const float u = juce::jlimit (0.0f, 1.0f, (float) ((now - openStart - kOpenDelayMs) / kOpenMs));
        openT = u >= 1.0f ? 1.0f : bezierEase (u, 0.65f, 0.0f, 0.35f, 1.0f);
        markDirty();
    }
    else if (openStart > 1.0 && now - openStart < kOpenDelayMs + kOpenMs + 1200.0) repaint (toolbarBounds().expanded (400.0f, 50.0f).toNearestInt());   // (the toolbar rising in)
    // the drawn land follows the model
    {
        const Terrain cur = ed.terrain();
        refreshLights (now);
        bool changed = shownCount != cur.size();
        if (! changed)
        {
            int k = 0;
            for (auto& s : shown) if (! s.ghost) { if (k < cur.size() && s.p != cur[k]) { changed = true; break; } ++k; }
        }
        if (motionActive || changed) stepMotion (now);
        else lastStep = now;
        // the ring labels hide while the land moves, and come back where they fit once it has been still for 180 ms
        if (! cur.sameLand (lastLand) || cur.labels != lastLand.labels)
        {
            const bool onlyLabels = cur.sameLand (lastLand);
            lastLand = cur;
            if (! onlyLabels)
            {
                if (! labelsQuiet) { labelsQuiet = true; landChangedAt = now; }
                else landChangedAt = juce::jmax (landChangedAt, now - 380.0);
            }
            labelsPending = true;
        }
        if (labelsPending && ! motionActive && ! interaction.active() && openT >= 1.0f && now - landChangedAt > 180.0 && cacheValid && ! dirty)
        {
            placeLabels (drawn());
            if (labelsQuiet) labelsShownAt = now;
            labelsQuiet = false; labelsPending = false;
            repaint();
        }
        if (labelsShownAt > 0 && now - labelsShownAt < 1100.0) repaint();
        if (labelsQuiet && now - landChangedAt < 420.0) repaint();
    }
    // EVOLVE: the drift moves on its own; not an undo step
    if (ed.terrain().evolve > 0.0f && displayPhase < 0.0f && isShowing() && ! interaction.active())
    {
        if (now - lastTick > 33.0)
        {
            const float dt = lastTick > 0 ? (float) juce::jmin (0.2, (now - lastTick) / 1000.0) : 0.033f;
            lastTick = now;
            Terrain t = ed.terrain();
            advance (t, dt);
            ed.setWithoutUndo (t);
        }
    }
    else lastTick = 0;
}

double TerrainView::benchmarkFrameMs (int frames, bool changing)
{
    juce::Image im (juce::Image::RGB, juce::jmax (1, getWidth()), juce::jmax (1, getHeight()), false);
    juce::Graphics g (im);
    settle();
    paint (g);
    auto cpuMs = []
    {
       #if defined (CLOCK_THREAD_CPUTIME_ID)
        timespec ts {}; if (clock_gettime (CLOCK_THREAD_CPUTIME_ID, &ts) == 0) return (double) ts.tv_sec * 1000.0 + (double) ts.tv_nsec / 1e6;
       #endif
        return nowMs();
    };
    const double t0 = cpuMs();
    for (int i = 0; i < frames; ++i)
    {
        if (changing)
        {
            Terrain t = ed.terrain();
            if (! t.empty()) t[0].height = 0.5f + 0.3f * std::sin ((float) i * 0.1f);
            ed.setWithoutUndo (t);
            settle();
        }
        paint (g);
    }
    return (cpuMs() - t0) / juce::jmax (1, frames);
}

juce::Image TerrainView::snapshot (float scale)
{
    juce::Image im (juce::Image::ARGB, juce::jmax (1, juce::roundToInt ((float) getWidth() * scale)), juce::jmax (1, juce::roundToInt ((float) getHeight() * scale)), true);
    juce::Graphics g (im);
    g.addTransform (juce::AffineTransform::scale (scale));
    settle();
    refreshLights (nowMs());
    heldNow = heldTo; hoverNow = hoverTo;
    dirty = true;
    renderCache (scale);
    // labels as they would settle (a still picture: no fades)
    if (! interaction.active())
    {
        placeLabels (drawn());
        labelsQuiet = false; labelsShownAt = 0; labelsPending = false; lastLand = ed.terrain();
    }
    paint (g);
    dirty = true;
    return im;
}
}
