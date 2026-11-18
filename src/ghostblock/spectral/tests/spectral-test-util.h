// Poltergeist 2 (GhostBlock) - small helpers for the SPECTRAL FX checks
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace p2t
{
inline int fails = 0;
#define CHECK(c, msg)                                                                                                   \
    do                                                                                                                  \
    {                                                                                                                   \
        if (!(c)) { ++p2t::fails; std::printf("FAIL: %s\n", msg); }                                                     \
        else std::printf("ok:   %s\n", msg);                                                                            \
    } while (0)
inline void section(const char *s) { std::printf("---- %s\n", s); }
inline int finish()
{
    std::printf(fails ? "FAILURES: %d\n" : "ALL PASS\n", fails);
    return fails ? 1 : 0;
}
struct Stereo
{
    std::vector<float> l, r;
    int n() const { return (int)l.size(); }
    explicit Stereo(int len = 0) : l((size_t)len, 0.f), r((size_t)len, 0.f) {}
};
// a bass-like test tone: a saw-ish 55 Hz note with a fifth, a filter-ish decay of the highs, slightly different left / right
inline Stereo bassSignal(double sr, double seconds)
{
    Stereo s((int)(sr * seconds));
    for (int i = 0; i < s.n(); ++i)
    {
        const double t = i / sr, env = std::min(1.0, t * 200.0) * (0.35 + 0.65 * std::exp(-t * 1.5));
        double a = 0, b = 0;
        for (int h = 1; h <= 40; ++h)
        {
            const double g = std::exp(-h * (0.05 + 0.25 * t)) / h;
            a += g * std::sin(2 * M_PI * 55.0 * h * t);
            b += g * std::sin(2 * M_PI * 55.0 * h * t + 0.3 * h);
            if (h <= 12) { a += 0.4 * g * std::sin(2 * M_PI * 82.41 * h * t); b += 0.4 * g * std::sin(2 * M_PI * 82.41 * h * t + 0.2); }
        }
        s.l[(size_t)i] = (float)(0.5 * env * a);
        s.r[(size_t)i] = (float)(0.5 * env * b);
    }
    return s;
}
struct Health
{
    bool finite{true}, denormal{false};
    float peak{0};
};
inline Health health(const Stereo &s)
{
    Health h;
    for (auto *v : {&s.l, &s.r})
        for (float x : *v)
        {
            if (!std::isfinite(x)) { h.finite = false; continue; }
            const float a = std::fabs(x);
            if (a != 0.f && a < 1e-30f) h.denormal = true;
            h.peak = std::max(h.peak, a);
        }
    return h;
}
inline double rmsOf(const Stereo &s, int from, int to)
{
    double e = 0;
    for (int i = from; i < to; ++i) e += (double)s.l[(size_t)i] * s.l[(size_t)i] + (double)s.r[(size_t)i] * s.r[(size_t)i];
    return std::sqrt(e / (2.0 * std::max(1, to - from)));
}
// rms of (y delayed-compensated by `lag`) - x over [from, to)
inline double rmsDiff(const Stereo &x, const Stereo &y, int lag, int from, int to)
{
    double e = 0;
    for (int i = from; i < to; ++i)
    {
        const double dl = (double)y.l[(size_t)(i + lag)] - x.l[(size_t)i], dr = (double)y.r[(size_t)(i + lag)] - x.r[(size_t)i];
        e += dl * dl + dr * dr;
    }
    return std::sqrt(e / (2.0 * std::max(1, to - from)));
}
inline float maxDiff(const Stereo &x, const Stereo &y, int lag, int from, int to)
{
    float d = 0;
    for (int i = from; i < to; ++i)
        d = std::max(d, std::max(std::fabs(y.l[(size_t)(i + lag)] - x.l[(size_t)i]), std::fabs(y.r[(size_t)(i + lag)] - x.r[(size_t)i])));
    return d;
}
} // namespace p2t
