// Ghost Graph -- EXPERIMENT: songs made from famous equations.
//
// GENERATE writes songs the way the videos do. EXPERIMENT starts from a famous equation instead -- y = mx + b, a
// parabola, the bell curve, Euler's formula, a rose curve, the heart curve, Fourier's square wave, the golden ratio ...
// -- written as it appears in a textbook, and lets it drive the music:
//   * MUSICAL: the equation draws the SHAPE of the melody; every 16th note reads it, scales it to about an octave and a
//     half, and snaps it to a key (S(v): step v of a 7-note scale). A bass reads it once a beat, a harmony a third above,
//     drums underneath. Curves drawn on the Re / Im plane (Euler, roses, the heart ...) sing their picture as glides,
//     with a snapped melody and drums on top.
//   * WILD: the equation as it is, sung as one continuous line (no key, no steps) -- the honest sound of the maths.
// Four scenes: it starts bare and builds; the last one plays the same equation with new numbers.
// Same seed -> same song.
#pragma once
#include <algorithm>
#include <cstdio>
#include <map>
#include <random>
#include <string>
#include <vector>
#include "GraphExpr.h"
#include "GraphGen.h"

namespace gg {

struct FamousEquation
{
    const char* name;     // "Slope-intercept form"
    const char* shows;    // "y = mx + b"
    const char* about;    // what it is, without maths
    bool plane;           // drawn on the Re / Im plane (X and Y)
    bool wildByNature;    // never quite musical (still fun)
};

class Experiment
{
public:
    static const std::vector<FamousEquation>& list()
    {
        static const std::vector<FamousEquation> L {
            { "Slope-intercept form", "y = mx + b", "a straight line: slope m, starting height b. It climbs (or falls) the scale in runs", false, false },
            { "Quadratic", "y = ax\xc2\xb2 + bx + c", "a parabola: up, turn, back down. Each bar is one arch", false, false },
            { "Sine wave", "y = A sin(\xcf\x89t + \xcf\x86)", "the smoothest wave there is: every vibration is built from these", false, false },
            { "Bell curve", "y = e^(-t\xc2\xb2/2\xcf\x83\xc2\xb2)", "the normal distribution: most things pile up in the middle", false, false },
            { "Logistic curve", "y = 1/(1 + e^(-kt))", "slow, then all at once, then levels off (how things grow): a riser", false, false },
            { "Damped oscillator", "y = e^(-\xce\xb3t) cos(\xcf\x89t)", "a spring or a plucked string: it wobbles and dies away", false, false },
            { "Fourier square wave", "y = sin t + sin 3t/3 + sin 5t/5 + ...", "sine waves stacked up until they make a square", false, false },
            { "Harmonic series", "f\xe2\x82\x99 = n f\xe2\x82\x81", "the notes hiding inside every note: 1, 2, 3, 4 ... times the pitch (a bugle's notes)", false, false },
            { "Golden ratio", "\xcf\x86 = (1 + \xe2\x88\x9a" "5)/2", "1.618...: the steps of floor(\xcf\x86n) never quite repeat", false, false },
            { "Fibonacci numbers", "F\xe2\x82\x99 = (\xcf\x86\xe2\x81\xbf - (1 - \xcf\x86)\xe2\x81\xbf)/\xe2\x88\x9a" "5", "1, 1, 2, 3, 5, 8 ...: each one the sum of the two before, read round a clock", false, false },
            { "Pythagorean theorem", "a\xc2\xb2 + b\xc2\xb2 = c\xc2\xb2", "right triangles with whole sides (3, 4, 5 ...): their sides sung as a chord", false, false },
            { "Inverse-square law", "y = 1/r\xc2\xb2", "how light and gravity fade with distance: a fast fall", false, false },
            { "Exponential growth", "y = e^(kt)", "compound interest: it doubles, and doubles again", false, false },
            { "Weierstrass function", "y = \xce\xa3 a\xe2\x81\xbf cos(b\xe2\x81\xbf \xcf\x80t)", "a curve that is jagged however close you zoom in", false, true },
            { "Tangent", "y = tan t", "flies off to infinity and comes back from the other side", false, true },
            { "Euler's formula", "e^(i\xce\xb8) = cos \xce\xb8 + i sin \xce\xb8", "the most famous equation: a point going round a circle", true, false },
            { "Lissajous curve", "x = sin(at + \xce\xb4), y = sin(bt)", "two vibrations at right angles: the old oscilloscope knot", true, false },
            { "Rose curve", "r = cos(k\xce\xb8)", "a flower with k petals (or 2k)", true, false },
            { "Cardioid", "r = 1 - cos \xce\xb8", "a heart-ish loop: the shape of a microphone's pickup", true, false },
            { "Archimedean spiral", "r = a\xce\xb8", "a spiral that winds out evenly, like a record groove", true, false },
            { "Heart curve", "x = 16 sin\xc2\xb3t, y = 13 cos t - 5 cos 2t - ...", "a heart", true, false },
            { "Butterfly curve", "r = e^(sin \xce\xb8) - 2 cos 4\xce\xb8 + sin\xe2\x81\xb5((2\xce\xb8 - \xcf\x80)/24)", "Temple Fay's butterfly", true, true },
            { "Riemann zeta function", "\xce\xb6(1/2 + it)", "the most famous unsolved problem in maths, on the Re / Im plane", true, true },
        };
        return L;
    }
    static int count() { return (int) list().size(); }

    // which: an equation from list() (-1: any); wild: 1 the raw maths, 0 musical, -1 decide (mostly musical)
    Experiment (uint32_t seed, int which = -1, int wild = -1, int keyRoot = -1, int keyScale = -1) : rng (seed)
    {
        idx = which >= 0 && which < count() ? which : pick (0, count() - 1);
        wildMode = wild >= 0 ? wild == 1 : chance (list()[(size_t) idx].wildByNature ? 45 : 20);
        modeA = oneOf (std::vector<int> { 2, 2, 3, 3, 1, 4, 5 });
        tempo = wildMode ? 76 + 4 * pick (0, 8) : 92 + 4 * pick (0, 9);
        waveIdx = oneOf (wildMode ? std::vector<int> { 0, 1, 4 } : std::vector<int> { 1, 3, 4, 5, 5 });
        tuneSt = oneOf (std::vector<int> { -7, -5, -3, 0, 2 });
        if (keyScale >= 0) { modeA = modeAForScale (keyScale); forcedScale = keyScale; }
        if (keyRoot >= 0) tuneSt = tuneForRoot (keyRoot, tuneSt);
    }
    int scale() const { return forcedScale >= 0 ? forcedScale : scaleForModeA (modeA); }

    const FamousEquation& equation() const { return list()[(size_t) idx]; }
    bool wild() const { return wildMode; }
    double bpm() const { return tempo; }
    float wave() const { return (float) waveIdx; }
    float tune() const { return (float) tuneSt; }

    std::vector<std::string> track()
    {
        std::vector<std::string> out;
        const Parts first = parts (0), last = parts (1);   // the same numbers for three scenes, new ones for the last
        for (int s = 0; s < 4; ++s) out.push_back (scene (s, s == 3 ? last : first));
        return out;
    }

private:
    std::mt19937 rng;
    int idx = 0, modeA = 2, waveIdx = 4, tuneSt = 0, forcedScale = -1;
    bool wildMode = false;
    double tempo = 110;

    int pick (int lo, int hi) { return std::uniform_int_distribution<int> (lo, hi) (rng); }
    bool chance (int pct) { return pick (1, 100) <= pct; }
    template <typename T> T oneOf (const std::vector<T>& v) { return v[(size_t) pick (0, (int) v.size() - 1)]; }
    static std::string num (double v)
    {
        if (std::abs (v - std::round (v)) < 1e-9) return std::to_string ((long long) std::llround (v));
        char b[32]; std::snprintf (b, sizeof b, "%.3g", v); return b;
    }

    // ---- the equations: their definitions (with this scene's numbers), what the song calls, and the time they run on
    struct Parts { std::string defs, f, X, Y, t; bool natural = false; };   // natural: already in tune (no key needed)

    Parts parts (int variant)
    {
        Parts P;
        const bool alt = variant == 1;   // the last scene: new numbers
        switch (idx)
        {
            case 0: { const int m = oneOf (std::vector<int> { 2, 3, 5, -2, -3 }) * (alt ? -1 : 1); const int b = pick (-1, 1);
                      P.defs = "f(t) = m t + b\nm = " + num (m) + "\nb = " + num (b) + "\n"; P.f = "f(t)"; P.t = alt ? "frac(T/4)" : "frac(T/2)"; break; }
            case 1: { const int a = oneOf (std::vector<int> { 1, -1, 2, -2 }) * (alt ? -1 : 1); const int b = pick (-1, 1);
                      P.defs = "f(t) = a t^2 + b t + c\na = " + num (a) + "\nb = " + num (b) + "\nc = 0\n"; P.f = "f(t)"; P.t = alt ? "mod(T, 2) - 1" : "mod(T, 4) - 2"; break; }
            case 2: { const std::string w = oneOf (std::vector<std::string> { "pi/2", "pi/4", "3pi/4", "pi/3" });
                      P.defs = "f(t) = A sin(omega t + phi)\nA = 1\nomega = " + (alt ? std::string ("pi/8") : w) + "\nphi = " + num (pick (0, 3)) + "\n"; P.f = "f(t)"; P.t = "T"; break; }
            case 3: { const std::string sg = alt ? "0.4" : oneOf (std::vector<std::string> { "0.6", "0.8", "1" });
                      P.defs = "f(t) = e^(-t^2/(2sigma^2))\nsigma = " + sg + "\n"; P.f = "f(t)"; P.t = alt ? "mod(T, 2) - 1" : "mod(T, 4) - 2"; break; }
            case 4: { P.defs = "f(t) = 1/(1 + e^(-k t))\nk = " + num (alt ? 8 : oneOf (std::vector<int> { 2, 3, 4 })) + "\n"; P.f = "f(t)"; P.t = alt ? "mod(T, 4) - 2" : "mod(T, 8) - 4"; break; }
            case 5: { P.defs = "f(t) = e^(-gamma t)cos(omega t)\ngamma = " + num (alt ? 2 : oneOf (std::vector<double> { 0.5, 1 })) + "\nomega = " + (alt ? std::string ("4pi") : oneOf (std::vector<std::string> { "2pi", "3pi" })) + "\n";
                      P.f = "f(t)"; P.t = alt ? "mod(T, 2)" : "mod(T, 4)"; break; }
            case 6: { P.defs = alt ? "f(t) = sin(t) + sin(3t)/3 + sin(5t)/5 + sin(7t)/7 + sin(9t)/9\n" : "f(t) = sin(t) + sin(3t)/3 + sin(5t)/5\n"; P.f = "f(t)"; P.t = alt ? "pi T/4" : "pi T/2"; break; }
            case 7: { const int N = alt ? 16 : oneOf (std::vector<int> { 6, 8, 10 });
                      P.defs = "f(n) = ln(n)/ln(2)\nn = mod(floor(4T), " + num (N) + ") + 1\n"; P.f = "f(n)"; P.t = "T"; P.natural = true; break; }
            case 8: { P.defs = "f(t) = mod(floor(phi t), " + num (alt ? 5 : 8) + ")\nphi = (1 + sqrt(5))/2\n"; P.f = "f(t)"; P.t = alt ? "4T + 8" : "4T"; break; }
            case 9: { P.defs = "F(n) = (phi^n - (1 - phi)^n)/sqrt(5)\nf(t) = mod(round(F(floor(t))), " + num (alt ? 5 : 7) + ")\nphi = (1 + sqrt(5))/2\n";
                      P.f = "f(t)"; P.t = "mod(4T + 32, 16)"; break; }
            case 10: { P.defs = "c(a, b) = sqrt(a^2 + b^2)\n"
                                "a = {k = 0: 3, k = 1: 5, k = 2: 8, 7}\n"
                                "b = {k = 0: 4, k = 1: 12, k = 2: 15, 24}\n"
                                "k = mod(floor(T/" + std::string (alt ? "2" : "4") + "), 4)\n";
                       P.f = "c(a, b)"; P.t = "T"; P.natural = true; break; }
            case 11: { P.defs = "f(r) = 1/r^2\n"; P.f = "f(t)"; P.t = alt ? "1 + mod(T, 1)" : "1 + mod(T, 2)"; break; }
            case 12: { P.defs = "f(t) = e^(k t)\nk = " + num (alt ? 4 : oneOf (std::vector<int> { 2, 3 })) + "\n"; P.f = "f(t)"; P.t = alt ? "mod(T, 2)/2" : "mod(T, 4)/4"; break; }
            case 13: { P.defs = "f(t) = cos(pi t) + cos(3pi t)/2 + cos(9pi t)/4 + cos(27pi t)/8\n"; P.f = "f(t)"; P.t = alt ? "T/2" : "T/4"; break; }
            case 14: { P.defs = "f(t) = tan(t)\n"; P.f = "f(t)"; P.t = alt ? "pi T/2" : "pi T/4"; break; }
            case 15: { P.defs = "theta = pi t/" + std::string (alt ? "2" : "4") + "\n"; P.X = "cos(theta)"; P.Y = "sin(theta)"; P.t = "x"; break; }
            case 16: { const std::vector<std::vector<int>> ab { { 3, 2 }, { 5, 4 }, { 3, 4 }, { 5, 6 } }; const auto r = alt ? std::vector<int> { 1, 2 } : oneOf (ab);
                       P.defs = "a = " + num (r[0]) + "\nb = " + num (r[1]) + "\ndelta = pi/" + num (oneOf (std::vector<int> { 2, 4, 6 })) + "\n";
                       P.X = "sin(a t + delta)"; P.Y = "sin(b t)"; P.t = "pi x/8"; break; }
            case 17: { P.defs = "r(theta) = cos(k theta)\nk = " + num (alt ? 4 : oneOf (std::vector<int> { 2, 3, 5 })) + "\n"; P.X = "r(t)cos(t)"; P.Y = "r(t)sin(t)"; P.t = "pi x/8"; break; }
            case 18: { P.defs = "r(theta) = 1 - cos(theta)\n"; P.X = "r(t)cos(t)"; P.Y = "r(t)sin(t)"; P.t = alt ? "pi x/4" : "pi x/8"; break; }
            case 19: { P.defs = "r(theta) = a theta\na = 1\n"; P.X = "r(t)cos(t)"; P.Y = "r(t)sin(t)"; P.t = alt ? "pi (x + 8)/2" : "pi (x + 8)/4"; break; }
            case 20: { P.defs = ""; P.X = "16sin(t)^3"; P.Y = "13cos(t) - 5cos(2t) - 2cos(3t) - cos(4t)"; P.t = alt ? "pi x/4" : "pi x/8"; break; }
            case 21: { P.defs = "r(theta) = e^(sin(theta)) - 2cos(4theta) + sin((2theta - pi)/24)^5\n"; P.X = "r(t)cos(t)"; P.Y = "r(t)sin(t)"; P.t = alt ? "pi (x + 8)/2" : "pi (x + 8)/4"; break; }
            default: { P.defs = "u = " + std::string (alt ? "2x + 16" : "4x + 32") + "\n"; P.X = "zre(u)"; P.Y = "zim(u)"; P.t = "x"; break; }
        }
        return P;
    }

    // how the values of expr (with the definitions) spread over the page: the 10th and 90th percentiles
    static bool spread (const std::string& expr, const std::string& defs, double& lo, double& hi)
    {
        auto p = Program::compile ("y = [" + expr + "]\n" + defs);
        if (! p->ok()) return false;
        std::vector<double> v;
        for (int i = 0; i < 512; ++i) { p->setX (-8.0 + 16.0 * i / 512); const double y = p->voice (0); if (std::isfinite (y)) v.push_back (y); }
        if (v.size() < 8) return false;
        std::sort (v.begin(), v.end());
        lo = v[v.size() / 10]; hi = v[v.size() * 9 / 10];
        if (hi - lo < 1e-6) { lo -= 0.5; hi += 0.5; }
        return true;
    }
    // a "nice" number near v (2 significant figures)
    static double nice (double v)
    {
        if (v == 0 || ! std::isfinite (v)) return 1;
        const double e = std::pow (10.0, std::floor (std::log10 (std::abs (v))) - 1);
        return std::round (v / e) * e;
    }

    std::string scene (int s, const Parts& P)
    {
        const auto& E = equation();
        std::string head = "# " + std::string (E.name) + ": " + E.shows + (s == 3 ? "   (new numbers)" : "") + "\n";
        // the clock: T steps in 16ths for the musical songs, glides for the wild ones
        const std::string clock = std::string (wildMode ? "T = x\n" : "T = floor(4x)/4\n") + "t = " + P.t + "\n";
        const std::string defs = P.defs + clock;
        const std::string drums =
            "kick = {h < 0.25: 1.2e^(-16h) - 2}\n"
            "hats = {frac(2x - 1/2) < 0.12: 1.4 - 2frac(2x - 1/2)}\n"
            "clap = {mod(x, 2) >= 1 and h < 0.3: 1.3 - h}\n"
            "h = frac(x)\n";
        const std::string scale = "S(v) = floor((12floor(v) + " + num (modeA) + ")/7)/12\n";

        if (E.plane)
        {
            // the picture: X across, Y up, sung as glides about half an octave each way
            double lx = 0, hx = 0, ly = 0, hy = 0;
            spread (P.X, defs, lx, hx); spread (P.Y, defs, ly, hy);
            const double m = std::max ({ std::abs (lx), std::abs (hx), std::abs (ly), std::abs (hy), 1e-3 });
            const std::string R = num (nice (0.6 / m));
            std::string y = "y = plane(" + R + "X, " + R + "Y";
            std::string extra;
            if (! wildMode && s >= 1) { y += ", kick"; if (s >= 2) y += ", hats"; if (s >= 3) y += ", clap"; extra += drums; }
            if (! wildMode && s >= 2)
            {
                // a melody and a bass that read the picture in steps, in a key
                const double K = nice (10.0 / std::max (1e-6, hx - lx)), O = std::round (-K * lx - 3);
                y += ", mel, bass";
                extra += "mel = {frac(4x) < 0.7: S(clamp(at(" + num (K) + "X + " + num (O) + ", floor(4x)/4), -7, 14))}\n"
                         "bass = {frac(x) < 0.85: S(clamp(at(" + num (nice (6.0 / std::max (1e-6, hy - ly))) + "Y, floor(x)), -7, 7)) - 1}\n" + scale;
            }
            if (wildMode && s >= 2) { y += ", kick"; extra += "kick = {h < 0.25: 1.2e^(-16h) - 2}\nh = frac(x)\n"; }
            y += ")\n";
            return head + y + "X = " + P.X + "\nY = " + P.Y + "\n" + P.defs + "t = " + P.t + "\n" + extra;
        }

        // a line: the equation draws the melody's shape
        double lo = 0, hi = 1;
        spread (P.f, defs, lo, hi);
        std::string y, extra;
        if (wildMode)
        {
            // the maths as it is: one continuous line, scaled to about an octave and a half
            const double K = nice (1.5 / std::max (1e-6, hi - lo)), O = nice (-K * (lo + hi) / 2);
            const std::string line = num (K) + P.f + (O >= 0 ? " + " : " - ") + num (std::abs (O));
            y = "y = [clamp(" + line + ", -2, 2)";
            if (s >= 1) { y += ", kick"; extra += "kick = {h < 0.25: 1.2e^(-16h) - 2}\nh = frac(x)\n"; }
            if (s >= 2) { y += ", echo"; extra += "echo = clamp(at(" + line + ", x - 1/2), -2, 2) - 1\n"; }
            if (s >= 3) { y += ", hats"; extra += "hats = {frac(2x - 1/2) < 0.12: 1.4 - 2frac(2x - 1/2)}\n"; }
            return head + y + "]\n" + P.defs + clock + extra;
        }
        std::string mel;
        if (P.natural)   // already in tune: the harmonic series, the Pythagorean chord
        {
            if (idx == 10)
            {
                y = "y = [ln(a)/ln(2) - 2, ln(b)/ln(2) - 2, ln(c(a, b))/ln(2) - 2";   // the triangle's three sides, as a chord
                if (s >= 1) y += ", kick";
                if (s >= 2) y += ", hats, bass";
                if (s >= 3) y += ", clap";
                extra = drums + "bass = {frac(x) < 0.85: ln(a)/ln(2) - 4}\n";
                return head + y + "]\n" + P.defs + clock + extra;
            }
            mel = "mel = {frac(4x) < 0.7: " + P.f + " - 1}\n";
            extra = "bass = {frac(x) < 0.85: -2}\n";
        }
        else
        {
            const double K = nice (10.0 / std::max (1e-6, hi - lo)), O = std::round (-K * lo - 3);
            const std::string v = num (K) + P.f + (O >= 0 ? " + " : " - ") + num (std::abs (O));
            mel = "mel = {frac(4x) < 0.7: S(clamp(" + v + ", -7, 14))}\n";
            extra = "bass = {frac(x) < 0.85: S(clamp(at(" + v + ", floor(x)), -7, 14)) - 1}\n"
                    "har = {frac(4x) < 0.7: S(clamp(" + v + " + 2, -5, 16))}\n" + scale;
        }
        y = "y = [mel, kick";
        if (s >= 1) y += ", hats, bass";
        if (s >= 2 && ! P.natural) y += ", har";
        if (s >= 2) y += ", clap";
        return head + y + "]\n" + P.defs + mel + clock + extra + drums;
    }
};

} // namespace gg
