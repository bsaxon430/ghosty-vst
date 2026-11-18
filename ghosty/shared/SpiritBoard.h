#pragma once
// SPIRIT BOARD: a 2-D board of ghosts (A, B, C ...), each holding whatever the host plugin decides (a scene, a snapshot, a preset), and a
// glowing PUCK the user drags (or an ORBIT moves): the sound is the blend of every ghost, weighted by its distance to the puck.
// Header-only, plugin-agnostic, shared by every GhostBlock plugin (Ghost Box scenes; Poltergeist / Ghosty Pro snapshots).
//
// Layers
//   1. maths (any thread, no allocation):  computeWeights(), orbitOffset()
//   2. model (message thread):             Board  (ghosts, puck, weight + orbit settings, juce::ValueTree save / load)
//   3. audio handoff (lock-free):          SharedBoard  (message thread publish(Board) -> audio thread read(): positions + settings)
//   4. UI (message thread):                BoardView  (themed with ghosty::themes, resizable; callbacks into the host)
//
// Guarantees (Shared/tests/SpiritBoardTests.cpp): weights are finite, >= 0 and sum to 1; the puck exactly on ghost k gives weight 1 to k and
// 0 to the others; weights change smoothly as the puck moves; no NaN with 0, 1 or kMaxGhosts ghosts, coincident ghosts or a puck far away.
#include <juce_gui_basics/juce_gui_basics.h>
#include "GhostArt.h"
#include "GhostThemes.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <functional>
#include <vector>

namespace ghosty::spirit {

constexpr int kMaxGhosts = 12;

// ---------------------------------------------------------------- 1. maths
enum class WeightMode { InverseDistance = 0, Gaussian = 1, NearestN = 2 };
struct WeightParams
{
    WeightMode mode = WeightMode::InverseDistance;
    float power = 2.0f;      // InverseDistance: w = 1 / d^power  (higher = sharper islands around each ghost)
    float radius = 0.25f;    // Gaussian: w = exp(-d^2 / (2 r^2))  (board units, the board is 0..1 x 0..1)
    int nearest = 3;         // NearestN: only the N closest ghosts count (inverse-distance among them)
};

// Normalised weights for n ghosts at (xs, ys) with the puck at (px, py) into out[0..n-1]. Exact 1 / 0 when the puck sits on a ghost
// (closer than 1e-5). n == 0: nothing written. Never NaN. No allocation: safe on the audio thread.
inline void computeWeights (const float* xs, const float* ys, int n, float px, float py, const WeightParams& wp, float* out) noexcept
{
    if (n <= 0) return;
    n = juce::jmin (n, kMaxGhosts);
    std::array<float, kMaxGhosts> d {};
    int onGhost = -1;
    for (int k = 0; k < n; ++k)
    {
        const float dx = xs[k] - px, dy = ys[k] - py;
        d[(size_t) k] = std::sqrt (dx * dx + dy * dy);
        if (d[(size_t) k] < 1.0e-5f && onGhost < 0) onGhost = k;
    }
    if (onGhost >= 0) { for (int k = 0; k < n; ++k) out[k] = k == onGhost ? 1.0f : 0.0f; return; }
    std::array<bool, kMaxGhosts> use {}; use.fill (true);
    if (wp.mode == WeightMode::NearestN && n > wp.nearest)
    {
        const int keep = juce::jmax (1, wp.nearest);
        for (int k = 0; k < n; ++k)
        {   // ghost k counts if fewer than 'keep' ghosts are strictly closer (ties by index)
            int closer = 0;
            for (int j = 0; j < n; ++j) if (j != k && (d[(size_t) j] < d[(size_t) k] || (! (d[(size_t) j] > d[(size_t) k]) && ! (d[(size_t) j] < d[(size_t) k]) && j < k))) ++closer;
            use[(size_t) k] = closer < keep;
        }
    }
    double sum = 0;
    for (int k = 0; k < n; ++k)
    {
        double w = 0;
        if (use[(size_t) k])
        {
            if (wp.mode == WeightMode::Gaussian) { const double r = juce::jmax (0.02f, wp.radius); w = std::exp (-(double) d[(size_t) k] * d[(size_t) k] / (2.0 * r * r)); }
            else w = 1.0 / std::pow ((double) d[(size_t) k], (double) juce::jlimit (0.5f, 8.0f, wp.power));
        }
        out[k] = (float) w; sum += w;
    }
    if (! (sum > 1.0e-30) || ! std::isfinite (sum))
    {   // everything vanished (a gaussian far from all ghosts): the nearest ghost takes it all
        int best = 0; for (int k = 1; k < n; ++k) if (d[(size_t) k] < d[(size_t) best]) best = k;
        for (int k = 0; k < n; ++k) out[k] = k == best ? 1.0f : 0.0f;
        return;
    }
    for (int k = 0; k < n; ++k) out[k] = (float) (out[k] / sum);
}

// ORBIT: the puck moves on its own around its set position. Deterministic: the same time gives the same offset (bounce == playback).
// Custom = a DRAWN ORBIT (one of the board's kMaxPaths user paths, OrbitParams::custom); saved as orbit 0 + "ocustom", so an older build
// reads it as Off
enum class Orbit { Off = 0, Circle = 1, Figure8 = 2, RandomWalk = 3, Custom = 4 };
struct OrbitParams
{
    Orbit mode = Orbit::Off;
    float rate = 0.25f;      // cycles per beat when synced, cycles per second when free
    bool sync = true;
    float depth = 0.25f;     // radius of the orbit in board units (0..0.5)
    uint32_t seed = 1;       // RandomWalk
    int custom = 0;          // Custom: which user path (0 .. kMaxPaths - 1)
};
inline float hash01 (uint32_t x) noexcept { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return (float) (x & 0xffffff) / 16777215.0f; }
// time = beats (sync) or seconds (free); returns the puck offset
inline juce::Point<float> orbitOffset (const OrbitParams& o, double time) noexcept
{
    if (o.mode == Orbit::Off || o.depth <= 0.0f) return {};
    const double ph = time * (double) o.rate;
    const double a = 2.0 * juce::MathConstants<double>::pi * (ph - std::floor (ph));
    const float r = juce::jlimit (0.0f, 0.5f, o.depth);
    switch (o.mode)
    {
        case Orbit::Off:     return {};
        case Orbit::Custom:  return {};   // (a DRAWN ORBIT is a path: orbitPuck)
        case Orbit::Circle:  return { r * (float) std::cos (a), r * (float) std::sin (a) };
        case Orbit::Figure8: return { r * (float) std::sin (a), r * 0.6f * (float) std::sin (2.0 * a) };
        case Orbit::RandomWalk:
        {   // smooth value noise: a random point per step, cosine-interpolated (one step per cycle)
            const double s = ph; const auto i = (int64_t) std::floor (s); const float f = (float) (s - (double) i);
            const float t = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * f);
            auto pt = [&o] (int64_t k, uint32_t salt) { return hash01 ((uint32_t) k * 2654435761u ^ (o.seed * 40503u + salt)) * 2.0f - 1.0f; };
            return { r * (pt (i, 1) + t * (pt (i + 1, 1) - pt (i, 1))), r * (pt (i, 7) + t * (pt (i + 1, 7) - pt (i, 7))) };
        }
        default: return {};
    }
}
inline juce::Point<float> clampToBoard (juce::Point<float> p) noexcept { return { juce::jlimit (0.0f, 1.0f, p.x), juce::jlimit (0.0f, 1.0f, p.y) }; }

// ---------------------------------------------------------------- 1b. DRAWN ORBIT: a looping path with its own timing
// A path is a list of (x, y, t) points on the board, t = where in the loop (0..1), sorted; the loop closes from the last point back to
// the first. Playback at a host time (beats): phase = frac (beats / loop beats + PHASE); REVERSE runs it backwards; the SPEED curve
// (5 points, monotone, 0 -> 0 and 1 -> 1) remaps the phase; the position is a time-indexed cubic through the points (Hermite with
// Catmull-Rom tangents taken over t: slow where the points are dense in time, fast where they are sparse, exactly as drawn / recorded;
// smooth through the wrap); SIZE scales it around its (time-weighted) centroid. The same host time always gives the same puck.
constexpr int kMaxPaths = 8;
constexpr int kMaxPathPoints = 512;
constexpr int kPathTable = 1024;   // the audio thread plays a baked table of one loop (linear between entries)
struct PathPoint { float x = 0.5f, y = 0.5f, t = 0.0f; };
inline int pathBars (int b) noexcept { return b >= 8 ? 8 : b >= 4 ? 4 : b >= 2 ? 2 : 1; }   // 1 / 2 / 4 / 8 bars

struct OrbitPath
{
    std::vector<PathPoint> pts;    // call normalise() after editing pts directly
    int bars = 1;                  // loop length (1 / 2 / 4 / 8 bars of 4 beats)
    float phase = 0.0f;            // PHASE offset (0..1 of the loop)
    float scale = 1.0f;            // SIZE around the centroid (0.1 .. 2)
    bool reverse = false;
    std::array<float, 5> speed { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };   // SPEED: the phase at 0, 1/4, 1/2, 3/4, 1 of the loop
    juce::Point<float> cen { 0.5f, 0.5f };                            // (cached by normalise)

    bool empty() const noexcept { return pts.size() < 2; }
    double loopBeats() const noexcept { return 4.0 * pathBars (bars); }
    bool speedIsLinear() const noexcept { return speed == std::array<float, 5> { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }; }
    void clear() { *this = OrbitPath(); }

    // finite, on the board, t in [0, 1) strictly increasing, at most kMaxPathPoints (more: resampled evenly in time); the centroid
    void normalise()
    {
        std::vector<PathPoint> v; v.reserve (pts.size());
        for (auto p : pts)
        {
            if (! std::isfinite (p.x) || ! std::isfinite (p.y) || ! std::isfinite (p.t)) continue;
            p.x = juce::jlimit (0.0f, 1.0f, p.x); p.y = juce::jlimit (0.0f, 1.0f, p.y);
            p.t = (float) (p.t - std::floor ((double) p.t)); if (p.t >= 1.0f) p.t = 0.0f;
            v.push_back (p);
        }
        std::stable_sort (v.begin(), v.end(), [] (const PathPoint& a, const PathPoint& b) { return a.t < b.t; });
        pts.clear();
        for (auto& p : v) if (pts.empty() || p.t - pts.back().t > 1.0e-6f) pts.push_back (p);
        if (pts.size() >= 2 && pts.back().t - pts.front().t > 1.0f - 1.0e-6f) pts.pop_back();
        bars = pathBars (bars);
        phase = std::isfinite (phase) ? juce::jlimit (0.0f, 1.0f, phase) : 0.0f;
        scale = std::isfinite (scale) ? juce::jlimit (0.1f, 2.0f, scale) : 1.0f;
        setSpeed (speed);
        if ((int) pts.size() > kMaxPathPoints) resample (kMaxPathPoints);
        updateCentroid();
    }
    // the curve at loop time t (0..1): no SPEED / REVERSE / SIZE
    juce::Point<float> rawAt (double t) const noexcept
    {
        const int n = (int) pts.size();
        if (n == 0) return { 0.5f, 0.5f };
        if (n == 1) return { pts[0].x, pts[0].y };
        t -= std::floor (t);
        // segment i: T(i) <= t < T(i + 1), T unwrapped over the loop (k beyond 0..n-1 wraps with +-1)
        int i;
        if (t < (double) pts[0].t) { i = n - 1; t += 1.0; }
        else { int lo = 0, hi = n - 1; while (lo < hi) { const int mid = (lo + hi + 1) / 2; if ((double) pts[(size_t) mid].t <= t) lo = mid; else hi = mid - 1; } i = lo; }
        auto T = [this, n] (int k) { const int q = (int) std::floor ((double) k / n); return (double) pts[(size_t) (k - q * n)].t + q; };
        auto P = [this, n] (int k) { const int q = (int) std::floor ((double) k / n); const auto& p = pts[(size_t) (k - q * n)]; return juce::Point<double> (p.x, p.y); };
        const double t0 = T (i), t1 = T (i + 1), h = t1 - t0;
        if (! (h > 1.0e-9)) return { pts[(size_t) i].x, pts[(size_t) i].y };
        auto tangent = [&] (int k) { const double dt = T (k + 1) - T (k - 1); return dt > 1.0e-9 ? (P (k + 1) - P (k - 1)) / dt : juce::Point<double>(); };
        const double u = juce::jlimit (0.0, 1.0, (t - t0) / h), u2 = u * u, u3 = u2 * u;
        const auto p = P (i) * (2 * u3 - 3 * u2 + 1) + tangent (i) * (h * (u3 - 2 * u2 + u)) + P (i + 1) * (-2 * u3 + 3 * u2) + tangent (i + 1) * (h * (u3 - u2));
        return { (float) p.x, (float) p.y };
    }
    // SPEED: a monotone cubic (Fritsch-Carlson) through (k / 4, speed[k]); the straight line = no change
    float speedMap (float u) const noexcept
    {
        if (speedIsLinear()) return u;
        u = juce::jlimit (0.0f, 1.0f, u);
        std::array<float, 4> d {}; std::array<float, 5> m {};
        for (int k = 0; k < 4; ++k) d[(size_t) k] = (speed[(size_t) k + 1] - speed[(size_t) k]) * 4.0f;
        m[0] = d[0]; m[4] = d[3];
        for (int k = 1; k < 4; ++k) m[(size_t) k] = d[(size_t) k - 1] * d[(size_t) k] <= 0.0f ? 0.0f : 0.5f * (d[(size_t) k - 1] + d[(size_t) k]);
        for (int k = 0; k < 4; ++k)
        {
            if (d[(size_t) k] == 0.0f) { m[(size_t) k] = m[(size_t) k + 1] = 0.0f; continue; }
            const float a = m[(size_t) k] / d[(size_t) k], b = m[(size_t) k + 1] / d[(size_t) k], r = a * a + b * b;
            if (r > 9.0f) { const float tau = 3.0f / std::sqrt (r); m[(size_t) k] = tau * a * d[(size_t) k]; m[(size_t) k + 1] = tau * b * d[(size_t) k]; }
        }
        const int k = juce::jmin (3, (int) (u * 4.0f));
        const float s = u * 4.0f - (float) k, s2 = s * s, s3 = s2 * s, h = 0.25f;
        const float y = speed[(size_t) k] * (2 * s3 - 3 * s2 + 1) + m[(size_t) k] * h * (s3 - 2 * s2 + s) + speed[(size_t) k + 1] * (-2 * s3 + 3 * s2) + m[(size_t) k + 1] * h * (s3 - s2);
        return juce::jlimit (0.0f, 1.0f, y);
    }
    void setSpeed (const std::array<float, 5>& s)
    {   // ends 0 / 1, finite, never going back
        speed[0] = 0.0f; speed[4] = 1.0f;
        for (int k = 1; k < 4; ++k) { const float v = std::isfinite (s[(size_t) k]) ? s[(size_t) k] : 0.25f * (float) k; speed[(size_t) k] = juce::jlimit (speed[(size_t) k - 1], 1.0f, v); }
    }
    // the puck at loop phase ph (0..1, PHASE not included): REVERSE, SPEED, SIZE
    juce::Point<float> shapeAt (double ph) const noexcept
    {
        ph -= std::floor (ph);
        if (reverse) { ph = 1.0 - ph; if (ph >= 1.0) ph -= 1.0; }
        auto p = rawAt (speedMap ((float) ph));
        if (scale != 1.0f) p = cen + (p - cen) * scale;
        return clampToBoard (p);
    }
    juce::Point<float> at (double loopPhase) const noexcept { return shapeAt (loopPhase + (double) phase); }   // PHASE included
    juce::Point<float> atBeats (double beats) const noexcept { return at (beats / loopBeats()); }
    void bake (float* xy, int n = kPathTable) const noexcept { for (int k = 0; k < n; ++k) { const auto p = shapeAt ((double) k / n); xy[2 * k] = p.x; xy[2 * k + 1] = p.y; } }
    void updateCentroid()
    {
        if (pts.empty()) { cen = { 0.5f, 0.5f }; return; }
        double sx = 0, sy = 0; const int N = 256;
        for (int k = 0; k < N; ++k) { const auto p = rawAt ((double) k / N); sx += p.x; sy += p.y; }
        cen = { (float) (sx / N), (float) (sy / N) };
    }

    // ---- edits (timing is kept)
    void smooth (float amount = 0.5f, int passes = 2)
    {   // closed-loop Laplacian on the positions
        const int n = (int) pts.size(); if (n < 3) return;
        for (int pass = 0; pass < passes; ++pass)
        {
            auto v = pts;
            for (int i = 0; i < n; ++i)
            {
                const auto& a = v[(size_t) ((i + n - 1) % n)]; const auto& b = v[(size_t) ((i + 1) % n)]; auto& p = pts[(size_t) i];
                p.x += amount * (0.5f * (a.x + b.x) - p.x); p.y += amount * (0.5f * (a.y + b.y) - p.y);
            }
        }
        updateCentroid();
    }
    void resample (int n)
    {   // n points evenly spaced in time, on the curve (the timing stays what it was)
        n = juce::jlimit (3, kMaxPathPoints, n);
        if (empty()) return;
        std::vector<PathPoint> v ((size_t) n);
        for (int k = 0; k < n; ++k) { const auto p = rawAt ((double) k / n); v[(size_t) k] = { p.x, p.y, (float) k / (float) n }; }
        pts = std::move (v);
        updateCentroid();
    }
    // DRAW: the stroke's points with the time each was drawn (seconds); drawing slowly = the puck moves slowly there. The loop closes
    // back to the first point in the time the average drawing speed takes for that gap (2 .. 25 % of the loop).
    static OrbitPath fromStroke (const std::vector<juce::Point<float>>& p, const std::vector<double>& secs, int bars)
    {
        OrbitPath o; o.bars = pathBars (bars);
        const size_t n = juce::jmin (p.size(), secs.size());
        if (n < 2) return o;
        const double D = secs[n - 1] - secs[0];
        double len = 0; for (size_t i = 1; i < n; ++i) len += p[i].getDistanceFrom (p[i - 1]);
        const double gap = p[n - 1].getDistanceFrom (p[0]);
        const bool timed = D > 1.0e-3;
        const double dur = timed ? D : (double) (n - 1);
        double join = len > 1.0e-6 ? gap / (len / dur) : dur * 0.1;
        join = juce::jlimit (dur * 0.02, dur * 0.25, join);
        const double total = dur + join;
        for (size_t i = 0; i < n; ++i) o.pts.push_back ({ p[i].x, p[i].y, (float) ((timed ? secs[i] - secs[0] : (double) i) / total) });
        o.normalise();
        return o;
    }

    // ---- save / load (part of the board, never of a sound)
    juce::ValueTree toTree (int index) const
    {
        juce::ValueTree t ("OPATH");
        t.setProperty ("i", index, nullptr); t.setProperty ("bars", pathBars (bars), nullptr); t.setProperty ("phase", phase, nullptr);
        t.setProperty ("scale", scale, nullptr); t.setProperty ("rev", reverse, nullptr);
        juce::String sp; for (int k = 0; k < 5; ++k) sp << juce::String (speed[(size_t) k], 6) << (k < 4 ? " " : "");
        t.setProperty ("speed", sp, nullptr);
        juce::String s; s.preallocateBytes (pts.size() * 30);
        for (auto& q : pts) s << juce::String (q.x, 6) << "," << juce::String (q.y, 6) << "," << juce::String (q.t, 7) << ";";
        t.setProperty ("pts", s, nullptr);
        return t;
    }
    static OrbitPath fromTree (const juce::ValueTree& t)
    {
        OrbitPath o;
        if (! t.isValid()) return o;
        o.bars = (int) t.getProperty ("bars", 1); o.phase = (float) t.getProperty ("phase", 0.0f); o.scale = (float) t.getProperty ("scale", 1.0f);
        o.reverse = (bool) t.getProperty ("rev", false);
        const auto sp = juce::StringArray::fromTokens (t.getProperty ("speed").toString(), " ", "");
        if (sp.size() == 5) { std::array<float, 5> v {}; for (int k = 0; k < 5; ++k) v[(size_t) k] = sp[k].getFloatValue(); o.setSpeed (v); }
        for (auto& tok : juce::StringArray::fromTokens (t.getProperty ("pts").toString(), ";", ""))
        {
            const auto f = juce::StringArray::fromTokens (tok, ",", "");
            if (f.size() == 3) o.pts.push_back ({ f[0].getFloatValue(), f[1].getFloatValue(), f[2].getFloatValue() });
            if ((int) o.pts.size() >= kMaxPathPoints * 4) break;
        }
        o.normalise();
        return o;
    }
};

// REC: hold / toggle REC and move the puck while the host plays; the position + the host time are captured for one loop length, then
// the path plays. Feed it the host time (beats) + whether it plays + the puck, as often as the UI can (a relocate / loop jump of the
// host is not counted as recorded time).
struct PathRecorder
{
    bool armed = false, running = false;
    double loopBeats = 4.0, elapsed = 0.0, last = 0.0;
    std::vector<PathPoint> pts;
    void arm (double beats) { armed = true; running = false; loopBeats = juce::jmax (0.25, beats); elapsed = 0; pts.clear(); }
    void cancel() { armed = running = false; pts.clear(); }
    // true when one loop has been captured (then result() is the path)
    bool feed (double beats, bool playing, float x, float y)
    {
        if (! armed || ! playing || ! std::isfinite (beats)) return false;
        if (! running) { running = true; elapsed = 0; last = beats; }
        else { const double d = beats - last; if (d > 0.0 && d < 1.0) elapsed += d; last = beats; }
        if (elapsed >= loopBeats) { armed = running = false; return true; }
        double ph = beats / loopBeats; ph -= std::floor (ph);
        pts.push_back ({ x, y, (float) ph });
        return false;
    }
    double progress() const noexcept { return armed ? juce::jlimit (0.0, 1.0, elapsed / loopBeats) : 0.0; }
    OrbitPath result (int bars) const { OrbitPath p; p.pts = pts; p.bars = pathBars (bars); p.normalise(); return p; }
};

// the puck of any orbit: a DRAWN ORBIT plays its path (where it was drawn); the others move around the base (X / Y)
inline juce::Point<float> orbitPuck (const OrbitParams& o, const OrbitPath* path, juce::Point<float> base, double time) noexcept
{
    if (o.mode == Orbit::Custom) return path != nullptr && ! path->empty() ? path->atBeats (time) : clampToBoard (base);
    return clampToBoard (base + orbitOffset (o, time));
}

// ---------------------------------------------------------------- 2. model
struct Ghost
{
    juce::String id;          // stable id (survives renames / reorders); the host keys its content by it
    juce::String label;       // "A", "B" ... or a user name
    juce::Colour colour;      // the ghost's tint
    float x = 0.5f, y = 0.5f; // 0..1
    int icon = 0;             // ghosty::lookFor index
    juce::String userData;    // free text for the host (e.g. which scene it came from)
};

inline juce::Colour ghostColour (int i)
{
    static const juce::uint32 c[kMaxGhosts] { 0xffff2bd6, 0xff22e6ff, 0xffa6ff2b, 0xffffd23f, 0xffa77bff, 0xffff8a3d,
                                              0xff3dffc8, 0xffff5a8a, 0xff7fb2ff, 0xffe0ff4f, 0xffd08bff, 0xffffb3e6 };
    return juce::Colour (c[(size_t) juce::jlimit (0, kMaxGhosts - 1, i)]);
}
inline juce::String ghostLetter (int i) { return juce::String::charToString ((juce::juce_wchar) ('A' + juce::jlimit (0, 25, i))); }

struct Board
{
    std::vector<Ghost> ghosts;
    float puckX = 0.5f, puckY = 0.5f;
    WeightParams weights;
    OrbitParams orbit;
    bool snapToGhost = false;  // the puck jumps to the nearest ghost when released
    std::array<OrbitPath, kMaxPaths> paths;   // DRAWN ORBIT user paths ('Custom 1..8'), saved with the board
    bool pathHold = false;                    // (not saved) REC is capturing a new path: the old one is not played meanwhile
    const OrbitPath* activePath() const { return orbit.mode == Orbit::Custom && juce::isPositiveAndBelow (orbit.custom, kMaxPaths) && ! pathHold && ! paths[(size_t) orbit.custom].empty() ? &paths[(size_t) orbit.custom] : nullptr; }

    bool full() const { return (int) ghosts.size() >= kMaxGhosts; }
    int indexOf (const juce::String& id) const { for (int i = 0; i < (int) ghosts.size(); ++i) if (ghosts[(size_t) i].id == id) return i; return -1; }
    // a new ghost: the next free letter, its colour, a fresh id. Returns its index, or -1 when the board is full.
    int add (float x, float y)
    {
        if (full()) return -1;
        int letter = 0;
        for (;; ++letter) { bool used = false; for (auto& g : ghosts) used = used || g.label == ghostLetter (letter); if (! used) break; }
        Ghost g; g.id = juce::Uuid().toString(); g.label = ghostLetter (letter); g.colour = ghostColour (letter); g.icon = letter % 6;
        g.x = juce::jlimit (0.0f, 1.0f, x); g.y = juce::jlimit (0.0f, 1.0f, y);
        ghosts.push_back (g);
        return (int) ghosts.size() - 1;
    }
    void remove (int i) { if (juce::isPositiveAndBelow (i, (int) ghosts.size())) ghosts.erase (ghosts.begin() + i); }
    void weightsAt (float px, float py, std::vector<float>& out) const
    {
        out.assign (ghosts.size(), 0.0f);
        std::array<float, kMaxGhosts> xs {}, ys {};
        const int n = juce::jmin ((int) ghosts.size(), kMaxGhosts);
        for (int k = 0; k < n; ++k) { xs[(size_t) k] = ghosts[(size_t) k].x; ys[(size_t) k] = ghosts[(size_t) k].y; }
        computeWeights (xs.data(), ys.data(), n, px, py, weights, out.data());
    }
    void arrangeInCircle (float radius = 0.35f)
    {
        const int n = (int) ghosts.size();
        for (int k = 0; k < n; ++k) { const float a = juce::MathConstants<float>::twoPi * (float) k / (float) juce::jmax (1, n) - juce::MathConstants<float>::halfPi;
                                      ghosts[(size_t) k].x = 0.5f + radius * std::cos (a); ghosts[(size_t) k].y = 0.5f + radius * std::sin (a); }
    }
    void randomise (uint32_t seed)
    {
        for (int k = 0; k < (int) ghosts.size(); ++k) { ghosts[(size_t) k].x = 0.08f + 0.84f * hash01 (seed * 977u + (uint32_t) k * 2u); ghosts[(size_t) k].y = 0.08f + 0.84f * hash01 (seed * 977u + (uint32_t) k * 2u + 1u); }
    }

    juce::ValueTree toTree() const
    {
        juce::ValueTree t ("SPIRITBOARD");
        t.setProperty ("puckX", puckX, nullptr); t.setProperty ("puckY", puckY, nullptr);
        t.setProperty ("wmode", (int) weights.mode, nullptr); t.setProperty ("power", weights.power, nullptr);
        t.setProperty ("radius", weights.radius, nullptr); t.setProperty ("nearest", weights.nearest, nullptr);
        t.setProperty ("orbit", orbit.mode == Orbit::Custom ? 0 : (int) orbit.mode, nullptr); t.setProperty ("orate", orbit.rate, nullptr); t.setProperty ("osync", orbit.sync, nullptr);
        if (orbit.mode == Orbit::Custom) t.setProperty ("ocustom", juce::jlimit (0, kMaxPaths - 1, orbit.custom), nullptr);   // (an older build: Off)
        t.setProperty ("odepth", orbit.depth, nullptr); t.setProperty ("oseed", (int) orbit.seed, nullptr); t.setProperty ("snap", snapToGhost, nullptr);
        for (auto& g : ghosts)
        {
            juce::ValueTree c ("GHOST");
            c.setProperty ("id", g.id, nullptr); c.setProperty ("label", g.label, nullptr); c.setProperty ("colour", (juce::int64) g.colour.getARGB(), nullptr);
            c.setProperty ("x", g.x, nullptr); c.setProperty ("y", g.y, nullptr); c.setProperty ("icon", g.icon, nullptr); c.setProperty ("data", g.userData, nullptr);
            t.appendChild (c, nullptr);
        }
        for (int k = 0; k < kMaxPaths; ++k) if (! paths[(size_t) k].empty()) t.appendChild (paths[(size_t) k].toTree (k), nullptr);   // (older builds skip them)
        return t;
    }
    static Board fromTree (const juce::ValueTree& t)
    {
        Board b;
        if (! t.isValid()) return b;
        auto f = [&t] (const char* k, float d) { const float v = (float) t.getProperty (k, d); return std::isfinite (v) ? v : d; };
        b.puckX = juce::jlimit (0.0f, 1.0f, f ("puckX", 0.5f)); b.puckY = juce::jlimit (0.0f, 1.0f, f ("puckY", 0.5f));
        b.weights.mode = (WeightMode) juce::jlimit (0, 2, (int) t.getProperty ("wmode", 0)); b.weights.power = juce::jlimit (0.5f, 8.0f, f ("power", 2.0f));
        b.weights.radius = juce::jlimit (0.02f, 1.0f, f ("radius", 0.25f)); b.weights.nearest = juce::jlimit (1, kMaxGhosts, (int) t.getProperty ("nearest", 3));
        { const int om = (int) t.getProperty ("orbit", 0); b.orbit.mode = (Orbit) (om < 0 ? 0 : om > 3 ? 0 : om); }   // unknown (newer) = Off
        if (t.hasProperty ("ocustom")) { const int k = (int) t.getProperty ("ocustom"); if (juce::isPositiveAndBelow (k, kMaxPaths)) { b.orbit.mode = Orbit::Custom; b.orbit.custom = k; } }
        b.orbit.rate = juce::jlimit (0.01f, 16.0f, f ("orate", 0.25f));
        b.orbit.sync = (bool) t.getProperty ("osync", true); b.orbit.depth = juce::jlimit (0.0f, 0.5f, f ("odepth", 0.25f)); b.orbit.seed = (uint32_t) (int) t.getProperty ("oseed", 1);
        b.snapToGhost = (bool) t.getProperty ("snap", false);
        for (auto c : t)
        {
            if (! c.hasType ("GHOST") || b.full()) continue;
            Ghost g; g.id = c.getProperty ("id").toString(); if (g.id.isEmpty()) g.id = juce::Uuid().toString();
            g.label = c.getProperty ("label", ghostLetter ((int) b.ghosts.size())).toString(); g.colour = juce::Colour ((juce::uint32) (juce::int64) c.getProperty ("colour", (juce::int64) ghostColour ((int) b.ghosts.size()).getARGB()));
            const float x = (float) c.getProperty ("x", 0.5f), y = (float) c.getProperty ("y", 0.5f);
            g.x = std::isfinite (x) ? juce::jlimit (0.0f, 1.0f, x) : 0.5f; g.y = std::isfinite (y) ? juce::jlimit (0.0f, 1.0f, y) : 0.5f;
            g.icon = (int) c.getProperty ("icon", 0); g.userData = c.getProperty ("data").toString();
            b.ghosts.push_back (g);
        }
        for (auto c : t)
            if (c.hasType ("OPATH")) { const int k = (int) c.getProperty ("i", -1); if (juce::isPositiveAndBelow (k, kMaxPaths)) b.paths[(size_t) k] = OrbitPath::fromTree (c); }
        return b;
    }
};

// ---------------------------------------------------------------- 3. audio handoff: positions + settings, lock-free
// Message thread: publish(board) after every edit. Audio thread: read(xs, ys, wp, op) copies a consistent set (seqlock; never blocks, no alloc).
struct SharedBoard
{
    void publish (const Board& b) noexcept
    {
        seq.fetch_add (1, std::memory_order_acq_rel);   // odd = writing
        const int n = juce::jmin ((int) b.ghosts.size(), kMaxGhosts);
        for (int k = 0; k < n; ++k) { x[(size_t) k].store (b.ghosts[(size_t) k].x, std::memory_order_relaxed); y[(size_t) k].store (b.ghosts[(size_t) k].y, std::memory_order_relaxed); }
        count.store (n, std::memory_order_relaxed);
        wmode.store ((int) b.weights.mode, std::memory_order_relaxed); power.store (b.weights.power, std::memory_order_relaxed);
        radius.store (b.weights.radius, std::memory_order_relaxed); nearest.store (b.weights.nearest, std::memory_order_relaxed);
        omode.store ((int) b.orbit.mode, std::memory_order_relaxed); orate.store (b.orbit.rate, std::memory_order_relaxed);
        osync.store (b.orbit.sync, std::memory_order_relaxed); odepth.store (b.orbit.depth, std::memory_order_relaxed); oseed.store (b.orbit.seed, std::memory_order_relaxed);
        seq.fetch_add (1, std::memory_order_acq_rel);   // even = done
        publishPath (b.activePath());
    }
    // DRAWN ORBIT: the playing path, baked (message thread; nullptr / empty = none). The audio thread reads two table entries per call.
    void publishPath (const OrbitPath* p)
    {
        const bool on = p != nullptr && ! p->empty();
        std::vector<float> xy; if (on) { xy.resize ((size_t) kPathTable * 2); p->bake (xy.data()); }
        pseq.fetch_add (1, std::memory_order_acq_rel);
        if (on)
        {
            for (int k = 0; k < kPathTable; ++k) { tx[(size_t) k].store (xy[(size_t) (2 * k)], std::memory_order_relaxed); ty[(size_t) k].store (xy[(size_t) (2 * k + 1)], std::memory_order_relaxed); }
            pBeats.store (p->loopBeats(), std::memory_order_relaxed); pPhase.store ((double) p->phase, std::memory_order_relaxed);
        }
        pOn.store (on, std::memory_order_relaxed);
        pseq.fetch_add (1, std::memory_order_acq_rel);
    }
    bool hasPath() const noexcept { return pOn.load (std::memory_order_relaxed); }
    // the path's puck at a host time (beats); false = no path playing. Deterministic: the same time, the same puck. No alloc, no lock.
    bool pathPuck (double beats, juce::Point<float>& out) const noexcept
    {
        bool on = false; juce::Point<float> r;
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            const auto s0 = pseq.load (std::memory_order_acquire);
            if (s0 & 1u) continue;
            on = pOn.load (std::memory_order_relaxed);
            if (on)
            {
                const double lb = pBeats.load (std::memory_order_relaxed);
                double ph = (std::isfinite (beats) ? beats : 0.0) / (lb > 0.0 ? lb : 4.0) + pPhase.load (std::memory_order_relaxed);
                ph -= std::floor (ph);
                const double f = ph * kPathTable; int i = (int) f; const float a = (float) (f - (double) i);
                i = juce::jlimit (0, kPathTable - 1, i); const int j = (i + 1) % kPathTable;
                const float x0 = tx[(size_t) i].load (std::memory_order_relaxed), x1 = tx[(size_t) j].load (std::memory_order_relaxed);
                const float y0 = ty[(size_t) i].load (std::memory_order_relaxed), y1 = ty[(size_t) j].load (std::memory_order_relaxed);
                r = { x0 + a * (x1 - x0), y0 + a * (y1 - y0) };
            }
            if (pseq.load (std::memory_order_acquire) == s0) break;
        }
        if (on) out = clampToBoard (r);
        return on;
    }
    // returns the ghost count; false copies are retried a few times, then the last values are used (a torn read only happens mid-edit)
    int read (float* xs, float* ys, WeightParams& wp, OrbitParams& op) const noexcept
    {
        int n = 0;
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            const auto s0 = seq.load (std::memory_order_acquire);
            if (s0 & 1u) continue;
            n = count.load (std::memory_order_relaxed);
            for (int k = 0; k < n; ++k) { xs[k] = x[(size_t) k].load (std::memory_order_relaxed); ys[k] = y[(size_t) k].load (std::memory_order_relaxed); }
            wp.mode = (WeightMode) wmode.load (std::memory_order_relaxed); wp.power = power.load (std::memory_order_relaxed);
            wp.radius = radius.load (std::memory_order_relaxed); wp.nearest = nearest.load (std::memory_order_relaxed);
            op.mode = (Orbit) omode.load (std::memory_order_relaxed); op.rate = orate.load (std::memory_order_relaxed); op.sync = osync.load (std::memory_order_relaxed);
            op.depth = odepth.load (std::memory_order_relaxed); op.seed = oseed.load (std::memory_order_relaxed);
            if (seq.load (std::memory_order_acquire) == s0) break;
        }
        return n;
    }
    // the audio thread posts what it uses (the moving puck and the weights) for the UI
    void postLive (float px, float py, const float* w, int n) noexcept
    {
        livePx.store (px, std::memory_order_relaxed); livePy.store (py, std::memory_order_relaxed);
        for (int k = 0; k < juce::jmin (n, kMaxGhosts); ++k) liveW[(size_t) k].store (w[k], std::memory_order_relaxed);
        liveN.store (n, std::memory_order_relaxed);
    }
    std::atomic<float> livePx { 0.5f }, livePy { 0.5f }; std::array<std::atomic<float>, kMaxGhosts> liveW {}; std::atomic<int> liveN { 0 };
    // the audio thread also posts the time it used (beats) + the wall clock of that block, so the UI can tell whether the audio is
    // running (fresh) and estimate the host time between blocks (recording a path, showing the orbit while the host is suspended)
    void postTime (double beats, double bpm, bool playing) noexcept
    {
        liveBeats.store (beats, std::memory_order_relaxed); liveBpm.store (bpm > 1.0 && std::isfinite (bpm) ? bpm : 120.0, std::memory_order_relaxed);
        livePlaying.store (playing, std::memory_order_relaxed); liveMs.store (juce::Time::getMillisecondCounterHiRes(), std::memory_order_release);
    }
    bool audioFresh (double maxAgeMs = 250.0) const noexcept { const double m = liveMs.load (std::memory_order_acquire); return m > 0.0 && juce::Time::getMillisecondCounterHiRes() - m < maxAgeMs; }
    double beatsNow() const noexcept
    {   // the last block's time + the wall-clock time since (only while the audio is fresh; else the last time as it was)
        const double m = liveMs.load (std::memory_order_acquire), b = liveBeats.load (std::memory_order_relaxed);
        if (! (m > 0.0)) return b;
        const double age = juce::Time::getMillisecondCounterHiRes() - m;
        return age >= 0.0 && age < 250.0 ? b + age / 60000.0 * liveBpm.load (std::memory_order_relaxed) : b;
    }
    std::atomic<double> liveBeats { 0.0 }, liveBpm { 120.0 }, liveMs { 0.0 }; std::atomic<bool> livePlaying { false };
private:
    std::atomic<uint32_t> seq { 0 };
    std::atomic<int> count { 0 }, wmode { 0 }, nearest { 3 }, omode { 0 };
    std::array<std::atomic<float>, kMaxGhosts> x {}, y {};
    std::atomic<float> power { 2.0f }, radius { 0.25f }, orate { 0.25f }, odepth { 0.25f };
    std::atomic<bool> osync { true }; std::atomic<uint32_t> oseed { 1 };
    std::atomic<uint32_t> pseq { 0 }; std::atomic<bool> pOn { false }; std::atomic<double> pBeats { 4.0 }, pPhase { 0.0 };
    std::array<std::atomic<float>, kPathTable> tx {}, ty {};
};

// ---------------------------------------------------------------- 4. UI
// The host owns the Board and reacts to the callbacks; BoardView never changes the board's CONTENT by itself (adding asks onAddGhost).
// Drag ghost: onGhostMoved(i, x, y) while dragging (the view moves board.ghosts[i] itself), onEditStart() once before (undo).
// Drag puck: onPuckMoved(x, y) (the host sets its X / Y parameters); the puck the audio uses comes back via livePuck().
class BoardView : public juce::Component
{
public:
    explicit BoardView (Board& b) : board (b) { setOpaque (true); }
    // ---- host hooks
    std::function<void (float x, float y)> onAddGhost;              // click on empty board (the host adds a ghost + gives it content)
    std::function<void (int ghost)> requestScene;                    // right-click "Set from..." (the host shows its own menu)
    std::function<void (int ghost)> onRemoveGhost, onRenamed, onSolo;
    std::function<void (int ghost)> onSetPercent;                    // optional: a "Set weight %..." item in the ghost menu (custom weights)
    std::function<void (int ghost, float x, float y)> onGhostMoved;
    std::function<void (float x, float y)> onPuckMoved;
    std::function<void()> onEditStart;                               // before an edit (undo snapshot)
    std::function<void (const std::vector<float>&)> onWeightsChanged; // the weights at the displayed puck, after any change
    std::function<juce::Point<float>()> livePuck;                    // where the audio's puck is (orbit / automation); default = board puck
    std::function<std::vector<float>()> liveWeights;                 // the audio's weights; default = computed from the displayed puck
    std::function<const themes::Palette&()> palette = [] () -> const themes::Palette& { return themes::get (themes::Theme::Dark); };
    std::function<void (const char*)> trail;                         // optional: breadcrumbs (paint begin / end) for a host's crash diary
    std::function<void (const juce::String&)> onPaintError;          // optional: paint threw (the view shows a plain board instead)
    std::function<void (juce::Graphics&)> paintPath;                 // optional: drawn over the grid, under the ghosts (DRAWN ORBIT)
    std::function<bool (const juce::MouseEvent&, int phase)> onBoardMouse;   // optional: takes the left mouse first (0 down, 1 drag, 2 up); true = taken
    juce::String hint { "click = add a ghost   -   drag a ghost to move it   -   drag the glowing puck to morph   -   right-click a ghost for its menu" };

    void refresh() { notifyWeights(); repaint(); }
    // the puck the view shows (set by every paint and by syncLive); a host timer calls syncLive() to follow the audio's puck: it repaints
    // only when the puck or the weights moved, and works whether or not the host reports the window as showing
    juce::Point<float> shownPuck { 0.5f, 0.5f };
    bool syncLive()
    {
        auto pk = livePuck ? livePuck() : juce::Point<float> { board.puckX, board.puckY };
        pk = { safeUnit (pk.x, 0.5f), safeUnit (pk.y, 0.5f) };
        std::vector<float> w; if (liveWeights) w = liveWeights();
        const bool moved = pk.getDistanceFrom (shownPuck) > 1.0e-5f || w != shownW;
        shownPuck = pk; shownW = std::move (w);
        if (moved) repaint();
        return moved;
    }
    juce::Rectangle<float> area() const { return getLocalBounds().toFloat().reduced (14.0f).withTrimmedBottom (16.0f); }
    juce::Point<float> toScreen (float x, float y) const { auto a = area(); return { a.getX() + x * a.getWidth(), a.getY() + y * a.getHeight() }; }
    juce::Point<float> toBoard (juce::Point<float> p) const { auto a = area(); return clampToBoard ({ (p.x - a.getX()) / juce::jmax (1.0f, a.getWidth()), (p.y - a.getY()) / juce::jmax (1.0f, a.getHeight()) }); }
    int ghostAt (juce::Point<float> p) const
    {
        for (int i = (int) board.ghosts.size() - 1; i >= 0; --i) if (toScreen (board.ghosts[(size_t) i].x, board.ghosts[(size_t) i].y).getDistanceFrom (p) < ghostRadius() + 4) return i;
        return -1;
    }
    float ghostRadius() const { return juce::jlimit (14.0f, 30.0f, area().getWidth() * 0.028f); }

    void paint (juce::Graphics& g) override
    {
        if (trail) trail ("view paint begin");
        try { paintBoard (g); }
        catch (const std::exception& e) { g.fillAll (juce::Colours::black); if (onPaintError) onPaintError (e.what()); }
        catch (...) { g.fillAll (juce::Colours::black); if (onPaintError) onPaintError ("unknown exception"); }
        if (trail) trail ("view paint end");
    }
    // everything a paint uses is finite and on the board: a NaN / huge puck or weight (a bad automation value, a torn read) is clamped
    static float safeUnit (float v, float fallback) noexcept { return std::isfinite (v) ? juce::jlimit (0.0f, 1.0f, v) : fallback; }
    void paintBoard (juce::Graphics& g)
    {
        const auto& P = palette();
        g.fillAll (P.panel);
        const auto a = area();
        if (a.getWidth() < 4.0f || a.getHeight() < 4.0f) return;   // too small to draw a board (the host has not laid us out yet)
        g.setColour (P.waveBg); g.fillRoundedRectangle (a, 10.0f);
        g.setColour (P.line); g.drawRoundedRectangle (a, 10.0f, 1.0f);
        g.setColour (P.overlay.withAlpha (P.isLight ? 0.10f : 0.07f));
        for (int k = 1; k < 8; ++k) { const float x = a.getX() + a.getWidth() * (float) k / 8.0f, y = a.getY() + a.getHeight() * (float) k / 8.0f;
                                      g.drawVerticalLine ((int) x, a.getY(), a.getBottom()); g.drawHorizontalLine ((int) y, a.getX(), a.getRight()); }
        if (paintPath) paintPath (g);
        auto pk = livePuck ? livePuck() : juce::Point<float> { board.puckX, board.puckY };
        pk = { safeUnit (pk.x, 0.5f), safeUnit (pk.y, 0.5f) };
        shownPuck = pk;
        std::vector<float> w; if (liveWeights) w = liveWeights(); if (w.size() != board.ghosts.size()) board.weightsAt (pk.x, pk.y, w);
        for (auto& v : w) v = safeUnit (v, 0.0f);
        const auto pp = toScreen (pk.x, pk.y);
        const float gr = ghostRadius();
        // influence: a line from each ghost to the puck, thickness / alpha = weight
        for (size_t k = 0; k < board.ghosts.size(); ++k)
        {
            const auto& gh = board.ghosts[k]; const auto c = themes::readableOn (gh.colour, P.waveBg, 3.0); const auto gp = toScreen (safeUnit (gh.x, 0.5f), safeUnit (gh.y, 0.5f));
            const float wk = k < w.size() ? w[k] : 0.0f;
            if (wk > 0.002f) { g.setColour (c.withAlpha (0.25f + 0.6f * wk)); g.drawLine ({ gp, pp }, 1.0f + 5.0f * wk); }
        }
        for (size_t k = 0; k < board.ghosts.size(); ++k)
        {
            const auto& gh = board.ghosts[k]; const auto c = themes::readableOn (gh.colour, P.waveBg, 3.0); const auto gp = toScreen (safeUnit (gh.x, 0.5f), safeUnit (gh.y, 0.5f));
            const float wk = k < w.size() ? w[k] : 0.0f;
            // ring = weight
            g.setColour (c.withAlpha (0.35f + 0.65f * wk)); g.drawEllipse (gp.x - gr - 4 - 10 * wk, gp.y - gr - 4 - 10 * wk, 2 * (gr + 4 + 10 * wk), 2 * (gr + 4 + 10 * wk), 1.5f + 2.5f * wk);
            g.setColour (P.cell); g.fillEllipse (gp.x - gr, gp.y - gr, 2 * gr, 2 * gr);
            auto look = ghosty::lookFor (gh.icon); look.slime = c; look.pupil = c;
            ghosty::drawGhost (g, look, gp.x, gp.y - gr * 0.1f, gr * 1.5f, 0.0f, false, 0.45f + 0.55f * juce::jmax (0.15f, wk));
            g.setColour (c); g.setFont (ghosty::font (juce::jmax (11.0f, gr * 0.62f)));
            g.drawText (gh.label, juce::Rectangle<float> (gp.x - 40, gp.y + gr + 2, 80, 16), juce::Justification::centred);
            if (wk > 0.005f) { g.setColour (P.dim); g.setFont (ghosty::font (10.0f, false)); g.drawText (juce::String (juce::roundToInt (wk * 100)) + "%", juce::Rectangle<float> (gp.x - 40, gp.y + gr + 16, 80, 13), juce::Justification::centred); }
        }
        // the puck (glow + solid core)
        const auto pc = P.isLight ? P.violet : P.accent1;
        for (int k = 3; k >= 1; --k) { g.setColour (pc.withAlpha (0.12f * (float) k)); const float r = 8.0f + 5.0f * (float) (4 - k); g.fillEllipse (pp.x - r, pp.y - r, 2 * r, 2 * r); }
        g.setColour (pc); g.fillEllipse (pp.x - 8, pp.y - 8, 16, 16);
        g.setColour (P.onAccent); g.drawEllipse (pp.x - 8, pp.y - 8, 16, 16, 1.5f);
        if (board.ghosts.empty())
        {
            g.setColour (P.ink); g.setFont (ghosty::font (15.0f));
            g.drawText ("click anywhere on the board to place your first ghost", a.toNearestInt(), juce::Justification::centred);
        }
        g.setColour (P.dim); g.setFont (ghosty::font (10.5f, false));
        g.drawText (hint, getLocalBounds().toFloat().removeFromBottom (18.0f).reduced (14.0f, 0), juce::Justification::centredLeft);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        mouseTaken = false;
        if (! e.mods.isPopupMenu() && onBoardMouse && onBoardMouse (e, 0)) { mouseTaken = true; return; }
        const int gi = ghostAt (e.position);
        if (e.mods.isPopupMenu()) { if (gi >= 0) ghostMenu (gi); return; }
        dragGhost = -1; dragPuck = false;
        const auto pk = toScreen (board.puckX, board.puckY);
        if (pk.getDistanceFrom (e.position) < 14.0f) { dragPuck = true; return; }
        if (gi >= 0) { dragGhost = gi; if (onEditStart) onEditStart(); return; }
        const auto bp = toBoard (e.position);
        if (onAddGhost && ! board.full()) onAddGhost (bp.x, bp.y);
        refresh();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (mouseTaken) { if (onBoardMouse) onBoardMouse (e, 1); return; }
        if (e.mods.isPopupMenu()) return;
        const auto bp = toBoard (e.position);
        if (dragPuck) { board.puckX = bp.x; board.puckY = bp.y; if (onPuckMoved) onPuckMoved (bp.x, bp.y); refresh(); }
        else if (juce::isPositiveAndBelow (dragGhost, (int) board.ghosts.size()))
        { board.ghosts[(size_t) dragGhost].x = bp.x; board.ghosts[(size_t) dragGhost].y = bp.y; if (onGhostMoved) onGhostMoved (dragGhost, bp.x, bp.y); refresh(); }
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (mouseTaken) { mouseTaken = false; if (onBoardMouse) onBoardMouse (e, 2); return; }
        if (dragPuck && board.snapToGhost && ! board.ghosts.empty())
        {
            int best = 0; float bd = 9;
            for (int k = 0; k < (int) board.ghosts.size(); ++k) { const float d = std::hypot (board.ghosts[(size_t) k].x - board.puckX, board.ghosts[(size_t) k].y - board.puckY); if (d < bd) { bd = d; best = k; } }
            board.puckX = board.ghosts[(size_t) best].x; board.puckY = board.ghosts[(size_t) best].y; if (onPuckMoved) onPuckMoved (board.puckX, board.puckY); refresh();
        }
        dragPuck = false; dragGhost = -1;
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {   // double-click a ghost: the puck sits on it (hear that ghost alone)
        if (onBoardMouse && onBoardMouse (e, 3)) return;   // (3 = a double-click while the mouse is taken: ignored by the view)
        const int gi = ghostAt (e.position);
        if (gi >= 0) solo (gi);
    }
    void solo (int gi)
    {
        if (! juce::isPositiveAndBelow (gi, (int) board.ghosts.size())) return;
        board.puckX = board.ghosts[(size_t) gi].x; board.puckY = board.ghosts[(size_t) gi].y;
        if (onPuckMoved) onPuckMoved (board.puckX, board.puckY);
        if (onSolo) onSolo (gi);
        refresh();
    }
    void ghostMenu (int gi)
    {
        juce::PopupMenu m; m.addSectionHeader ("GHOST " + board.ghosts[(size_t) gi].label);
        m.addItem (1, "Set from...", (bool) requestScene); m.addItem (2, "Rename..."); m.addItem (3, "Solo (puck onto it)");
        if (onSetPercent) m.addItem (5, "Set weight %...");
        m.addSeparator(); m.addItem (4, "Remove");
        juce::Component::SafePointer<BoardView> sp (this);
        m.showMenuAsync (juce::PopupMenu::Options(), [sp, gi] (int r) mutable
        {
            if (sp == nullptr || ! juce::isPositiveAndBelow (gi, (int) sp->board.ghosts.size())) return;
            if (r == 1 && sp->requestScene) sp->requestScene (gi);
            else if (r == 2) sp->rename (gi);
            else if (r == 3) sp->solo (gi);
            else if (r == 5 && sp->onSetPercent) sp->onSetPercent (gi);
            else if (r == 4) { if (sp->onEditStart) sp->onEditStart(); if (sp->onRemoveGhost) sp->onRemoveGhost (gi); else sp->board.remove (gi); sp->refresh(); }
        });
    }
    void rename (int gi)
    {
        auto* w = new juce::AlertWindow ("Rename ghost", "", juce::AlertWindow::NoIcon, this);
        w->addTextEditor ("name", board.ghosts[(size_t) gi].label); w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey)); w->addButton ("Cancel", 0);
        juce::Component::SafePointer<BoardView> sp (this);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([sp, gi, w] (int r) mutable
        {
            if (r == 1 && sp != nullptr && juce::isPositiveAndBelow (gi, (int) sp->board.ghosts.size()))
            {
                const auto t = w->getTextEditorContents ("name").trim().substring (0, 12);
                if (t.isNotEmpty()) { if (sp->onEditStart) sp->onEditStart(); sp->board.ghosts[(size_t) gi].label = t; if (sp->onRenamed) sp->onRenamed (gi); sp->refresh(); }
            }
        }), true);
    }
    Board& board;
private:
    void notifyWeights() { if (! onWeightsChanged) return; std::vector<float> w; board.weightsAt (board.puckX, board.puckY, w); onWeightsChanged (w); }
    int dragGhost = -1; bool dragPuck = false, mouseTaken = false;
    std::vector<float> shownW;
};

} // namespace ghosty::spirit
