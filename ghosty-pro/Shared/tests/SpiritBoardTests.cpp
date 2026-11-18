// Shared/SpiritBoard.h: weights, orbit, model save / load, audio handoff, the view (run under xvfb-run for the UI part).
#include "SpiritBoard.h"
#include <cstdio>

using namespace ghosty::spirit;
static int failures = 0;
static void check (bool ok, const char* what) { if (! ok) { ++failures; std::printf ("  FAIL: %s\n", what); } }

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    std::printf ("SPIRIT BOARD tests\n");
    // ---- weights
    {
        std::array<float, kMaxGhosts> xs {}, ys {}, w {};
        uint32_t rng = 1; auto r01 = [&rng] { rng = rng * 1664525u + 1013904223u; return (float) (rng >> 8) / 16777216.0f; };
        double worstSum = 0; bool finite = true, nonNeg = true, exact = true;
        for (int mode = 0; mode < 3; ++mode)
            for (int n = 1; n <= kMaxGhosts; ++n)
                for (int trial = 0; trial < 200; ++trial)
                {
                    for (int k = 0; k < n; ++k) { xs[(size_t) k] = r01(); ys[(size_t) k] = r01(); }
                    WeightParams wp; wp.mode = (WeightMode) mode; wp.power = 0.5f + 7.5f * r01(); wp.radius = 0.02f + r01(); wp.nearest = 1 + (int) (r01() * 5);
                    const float px = r01() * 1.4f - 0.2f, py = r01() * 1.4f - 0.2f;
                    computeWeights (xs.data(), ys.data(), n, px, py, wp, w.data());
                    double s = 0; for (int k = 0; k < n; ++k) { s += w[(size_t) k]; finite = finite && std::isfinite (w[(size_t) k]); nonNeg = nonNeg && w[(size_t) k] >= 0.0f; }
                    worstSum = std::max (worstSum, std::abs (s - 1.0));
                    const int on = (int) (r01() * n) % n;   // puck exactly on a ghost
                    computeWeights (xs.data(), ys.data(), n, xs[(size_t) on], ys[(size_t) on], wp, w.data());
                    for (int k = 0; k < n; ++k) exact = exact && w[(size_t) k] == (k == on ? 1.0f : 0.0f);
                }
        // coincident ghosts, a far puck with a tiny gaussian, smoothness along a path
        xs[0] = xs[1] = 0.3f; ys[0] = ys[1] = 0.3f; WeightParams g; g.mode = WeightMode::Gaussian; g.radius = 0.02f;
        computeWeights (xs.data(), ys.data(), 2, 5.0f, 5.0f, g, w.data());
        const bool far = std::isfinite (w[0]) && std::abs (w[0] + w[1] - 1.0f) < 1.0e-6f;
        xs[0] = 0.2f; ys[0] = 0.5f; xs[1] = 0.8f; ys[1] = 0.5f; xs[2] = 0.5f; ys[2] = 0.2f; WeightParams id;
        float maxJump = 0, prev = -1; for (int i = 1; i < 1000; ++i) { computeWeights (xs.data(), ys.data(), 3, 0.21f + 0.58f * (float) i / 1000.0f, 0.5f, id, w.data()); if (prev >= 0) maxJump = std::max (maxJump, std::abs (w[0] - prev)); prev = w[0]; }
        // centroid of 3 symmetric ghosts = equal weights
        xs[0] = 0.5f; ys[0] = 0.2f; xs[1] = 0.5f + 0.3f * 0.8660254f; ys[1] = 0.65f; xs[2] = 0.5f - 0.3f * 0.8660254f; ys[2] = 0.65f;
        computeWeights (xs.data(), ys.data(), 3, 0.5f, 0.5f, id, w.data());
        const bool centroid = std::abs (w[0] - 1.0f / 3.0f) < 1.0e-4f && std::abs (w[1] - 1.0f / 3.0f) < 1.0e-4f;
        std::printf ("  weights: worst |sum - 1| %.2e, finite %d, >= 0 %d, exact on a ghost %d, far gaussian %d, max step along a path %.4f, centroid equal %d\n",
                     worstSum, (int) finite, (int) nonNeg, (int) exact, (int) far, maxJump, (int) centroid);
        check (worstSum < 1.0e-5 && finite && nonNeg && exact && far && maxJump < 0.01f && centroid, "weights");
    }
    // ---- orbit
    {
        OrbitParams o; o.depth = 0.2f; bool ok = true;
        for (int m = 1; m <= 3; ++m) { o.mode = (Orbit) m; for (double t = 0; t < 20; t += 0.013) { const auto a = orbitOffset (o, t), b = orbitOffset (o, t); ok = ok && a == b && std::abs (a.x) <= 0.2001f && std::abs (a.y) <= 0.2001f && std::isfinite (a.x); } }
        o.mode = Orbit::Circle; o.rate = 1.0f; const auto p0 = orbitOffset (o, 0.0), p1 = orbitOffset (o, 1.0);
        std::printf ("  orbit: deterministic + bounded %d, one cycle per beat at rate 1 %d\n", (int) ok, (int) (p0.getDistanceFrom (p1) < 1.0e-4f));
        check (ok && p0.getDistanceFrom (p1) < 1.0e-4f, "orbit");
    }
    // ---- model
    {
        Board b; for (int k = 0; k < 14; ++k) b.add (0.1f + 0.06f * (float) k, 0.4f);
        const bool max = (int) b.ghosts.size() == kMaxGhosts && b.ghosts[2].label == "C";
        b.ghosts[3].label = "Wobble"; b.ghosts[5].userData = "scene:dub"; b.puckX = 0.33f; b.weights.mode = WeightMode::NearestN; b.orbit.mode = Orbit::Figure8; b.snapToGhost = true;
        const auto r = Board::fromTree (b.toTree());
        bool same = r.ghosts.size() == b.ghosts.size() && r.puckX == b.puckX && r.weights.mode == b.weights.mode && r.orbit.mode == b.orbit.mode && r.snapToGhost;
        for (size_t k = 0; k < b.ghosts.size() && same; ++k) same = r.ghosts[k].id == b.ghosts[k].id && r.ghosts[k].label == b.ghosts[k].label && r.ghosts[k].x == b.ghosts[k].x && r.ghosts[k].userData == b.ghosts[k].userData;
        b.remove (1); const int again = b.add (0.5f, 0.9f);
        const bool reuse = again >= 0 && b.ghosts[(size_t) again].label == "B";   // the freed letter comes back
        b.arrangeInCircle(); bool inside = true; for (auto& g : b.ghosts) inside = inside && g.x >= 0 && g.x <= 1 && g.y >= 0 && g.y <= 1;
        const auto bad = Board::fromTree (juce::ValueTree ("SPIRITBOARD"));
        std::printf ("  model: 12 max %d, save / load identical %d, freed letter reused %d, circle inside %d, empty tree ok %d\n", (int) max, (int) same, (int) reuse, (int) inside, (int) bad.ghosts.empty());
        check (max && same && reuse && inside && bad.ghosts.empty(), "model");
    }
    // ---- audio handoff
    {
        Board b; b.add (0.2f, 0.2f); b.add (0.8f, 0.7f); b.weights.power = 3.0f; SharedBoard sh; sh.publish (b);
        std::array<float, kMaxGhosts> xs {}, ys {}; WeightParams wp; OrbitParams op;
        const int n = sh.read (xs.data(), ys.data(), wp, op);
        std::printf ("  handoff: %d ghosts, positions %d, settings %d\n", n, (int) (xs[1] == 0.8f && ys[1] == 0.7f), (int) (wp.power == 3.0f));
        check (n == 2 && xs[1] == 0.8f && ys[1] == 0.7f && wp.power == 3.0f, "handoff");
    }
    // ---- view: click adds via the callback, drag moves, the puck drag reports; renders in all three themes with readable ink
    {
        Board b; BoardView v (b); v.setSize (600, 420);
        int added = 0; v.onAddGhost = [&] (float x, float y) { b.add (x, y); ++added; };
        float px = -1; v.onPuckMoved = [&] (float x, float) { px = x; };
        auto src = juce::Desktop::getInstance().getMainMouseSource();
        auto ev = [&] (juce::Point<float> p, juce::ModifierKeys m) { return juce::MouseEvent (src, p, m, 1.0f, 0, 0, 0, 0, &v, &v, juce::Time::getCurrentTime(), p, juce::Time::getCurrentTime(), 1, false); };
        const auto at = v.toScreen (0.2f, 0.3f);
        v.mouseDown (ev (at, juce::ModifierKeys::leftButtonModifier)); v.mouseUp (ev (at, {}));
        const auto g0 = v.toScreen (b.ghosts[0].x, b.ghosts[0].y), to = v.toScreen (0.7f, 0.6f);
        v.mouseDown (ev (g0, juce::ModifierKeys::leftButtonModifier)); v.mouseDrag (ev (to, juce::ModifierKeys::leftButtonModifier)); v.mouseUp (ev (to, {}));
        const auto pk = v.toScreen (b.puckX, b.puckY), pto = v.toScreen (0.1f, 0.9f);
        v.mouseDown (ev (pk, juce::ModifierKeys::leftButtonModifier)); v.mouseDrag (ev (pto, juce::ModifierKeys::leftButtonModifier)); v.mouseUp (ev (pto, {}));
        bool readable = true;
        for (auto t : { ghosty::themes::Theme::Dark, ghosty::themes::Theme::Purple, ghosty::themes::Theme::Light })
        {
            v.palette = [t] () -> const ghosty::themes::Palette& { return ghosty::themes::get (t); };
            auto img = v.createComponentSnapshot (v.getLocalBounds(), true, 1.0f);
            const auto& P = ghosty::themes::get (t);
            readable = readable && ghosty::themes::contrast (P.ink, P.waveBg) >= 4.5 && ghosty::themes::contrast (P.dim, P.panel) >= 4.5 && img.getPixelAt (3, 3).getAlpha() == 255;
        }
        std::printf ("  view: click added %d ghost, drag moved it to %.2f,%.2f, puck dragged to x %.2f, readable + opaque in 3 looks %d\n", added, b.ghosts[0].x, b.ghosts[0].y, px, (int) readable);
        check (added == 1 && std::abs (b.ghosts[0].x - 0.7f) < 0.02f && std::abs (px - 0.1f) < 0.02f && readable, "view");
    }
    std::printf ("\n%s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAILURES", failures);
    return failures;
}
