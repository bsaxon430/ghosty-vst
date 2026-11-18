// Ghost Matter: copied unchanged from Ghost Bass (GhostBlock's own code, GPL-3.0-or-later there; AGPL-3.0 inside Ghost Matter).
// In Ghost Matter it moves the SPACE pad's position (where the sound sits) instead of the bass territory.
/*
 * Ghost Bass - a bass instrument built on Surge XT.
 * Copyright 2026 the Ghost Bass authors. Based on Surge XT by the Surge Synth Team.
 * Released under the GNU General Public Licence v3 or later (see LICENSE).
 *
 * PIXEL TRAIL motion for BASS TERRITORY (our own implementation; the look was inspired by an
 * unlicensed web toy, no code of which is used here).
 *
 * Moves the territory position by itself, on the audio thread, so it runs with the editor closed
 * and follows the host's tempo:
 *  - WANDER: a slow, smooth drift around where you left the card (amount 0..1 = how far and how
 *    fast; host-automatable as "Territory Wander").
 *  - PATH: a recorded drag, kPathSteps points per bar (1/32 notes), 1, 2 or 4 bars long, looped in
 *    sync with the song position. Recording happens on the UI thread (it writes the points as
 *    the playhead passes them); the audio thread only reads.
 * All state is atomics and fixed arrays: no locks, no allocation.
 */
#ifndef GHOSTBASS_SRC_GHOST_GHOSTTRAIL_H
#define GHOSTBASS_SRC_GHOST_GHOSTTRAIL_H

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <string>

namespace ghost
{

class Trail
{
  public:
    static constexpr int kPathSteps = 32; // per bar
    static constexpr int kMaxBars = 4;
    static constexpr int kMaxPoints = kPathSteps * kMaxBars;

    std::atomic<float> wander{0.f};    // 0 = off
    std::atomic<int> bars{2};          // path length: 1, 2 or 4 bars
    std::atomic<bool> loop{false};     // play the recorded path
    std::atomic<bool> recording{false};
    std::atomic<int> recorded{0};      // how many points the path holds (0 = none)
    std::atomic<float> anchorX{0.5f}, anchorY{0.5f}; // where the card was left
    std::atomic<float> phase{0.f};     // 0..1 through the path (for the UI)
    std::atomic<bool> moving{false};   // the trail drives the position right now
    std::atomic<int> curStep{0};       // the path step the song position is on (for recording)

    Trail()
    {
        for (auto &p : px)
            p.store(0.5f);
        for (auto &p : py)
            p.store(0.5f);
    }

    int length() const { return std::clamp(bars.load(std::memory_order_relaxed), 1, kMaxBars) * kPathSteps; }
    int stepAt(double ppq) const
    {
        const double barsLen = 4.0 * std::clamp(bars.load(std::memory_order_relaxed), 1, kMaxBars);
        double ph = std::fmod(std::max(0.0, ppq), barsLen) / barsLen;
        return std::clamp((int)(ph * length()), 0, length() - 1);
    }
    // UI thread: write the point for the step the playhead is on
    void recordPoint(int step, float x, float y)
    {
        if (step < 0 || step >= kMaxPoints)
            return;
        px[step].store(std::clamp(x, 0.f, 1.f), std::memory_order_relaxed);
        py[step].store(std::clamp(y, 0.f, 1.f), std::memory_order_relaxed);
        recorded.store(std::max(recorded.load(), step + 1), std::memory_order_release);
    }
    float pointX(int i) const { return px[std::clamp(i, 0, kMaxPoints - 1)].load(std::memory_order_relaxed); }
    float pointY(int i) const { return py[std::clamp(i, 0, kMaxPoints - 1)].load(std::memory_order_relaxed); }
    void clearPath()
    {
        loop.store(false);
        recording.store(false);
        recorded.store(0);
    }

    // Audio thread, once per Surge block. ppq: song position at the block start;
    // dq: quarter notes in this block; dt: seconds in this block. Returns true and sets x, y
    // when the trail moves the territory.
    bool tick(double ppq, double dq, double dt, float &x, float &y)
    {
        (void)dq;
        const int len = length();
        curStep.store(stepAt(ppq), std::memory_order_relaxed);
        const bool havePath = recorded.load(std::memory_order_acquire) >= len;
        if (loop.load(std::memory_order_relaxed) && havePath && !recording.load(std::memory_order_relaxed))
        {
            // the recorded path, interpolated between its 1/32 points (no steps in the sound)
            const double barsLen = 4.0 * std::clamp(bars.load(std::memory_order_relaxed), 1, kMaxBars);
            const double ph = std::fmod(std::max(0.0, ppq), barsLen) / barsLen;
            const double fpos = ph * len;
            const int i0 = (int)fpos % len, i1 = (i0 + 1) % len;
            const float fr = (float)(fpos - std::floor(fpos));
            x = pointX(i0) + (pointX(i1) - pointX(i0)) * fr;
            y = pointY(i0) + (pointY(i1) - pointY(i0)) * fr;
            phase.store((float)ph, std::memory_order_relaxed);
            moving.store(true, std::memory_order_relaxed);
            return true;
        }
        const float w = std::clamp(wander.load(std::memory_order_relaxed), 0.f, 1.f);
        if (w > 0.001f)
        {
            // two slow random walks, each low-passed twice: a smooth drift, never a jump.
            // reach: up to 0.35 of the map around the anchor; speed: 0.05..0.6 Hz
            const double rate = 0.05 + 0.55 * w;
            const double a = 1.0 - std::exp(-2.0 * M_PI * rate * dt);
            if ((t += dt) > nextKick)
            {
                nextKick = t + 0.5 / rate; // a new place to drift towards, twice per cycle
                tgt[0] = 2.f * urand() - 1.f;
                tgt[1] = 2.f * urand() - 1.f;
            }
            for (int k = 0; k < 2; ++k)
            {
                s1[k] += (float)a * (tgt[k] - s1[k]);
                s2[k] += (float)a * (s1[k] - s2[k]);
            }
            const float reach = 0.1f + 0.25f * w;
            x = std::clamp(anchorX.load(std::memory_order_relaxed) + reach * s2[0], 0.f, 1.f);
            y = std::clamp(anchorY.load(std::memory_order_relaxed) + reach * s2[1], 0.f, 1.f);
            moving.store(true, std::memory_order_relaxed);
            return true;
        }
        // nothing drives it: let the drift settle back for next time
        s1[0] = s1[1] = s2[0] = s2[1] = 0.f;
        moving.store(false, std::memory_order_relaxed);
        return false;
    }

    std::string serialise() const
    {
        std::string o = "t1;" + std::to_string((int)std::lround(wander.load() * 1000)) + ";" + std::to_string(bars.load()) +
                        ";" + (loop.load() ? "1" : "0") + ";" + std::to_string(recorded.load()) + ";";
        const int n = std::min(recorded.load(), kMaxPoints);
        for (int i = 0; i < n; ++i)
            o += std::to_string((int)std::lround(pointX(i) * 1000)) + "," +
                 std::to_string((int)std::lround(pointY(i) * 1000)) + (i + 1 < n ? " " : "");
        return o;
    }
    bool deserialise(const std::string &t)
    {
        if (t.compare(0, 3, "t1;") != 0)
            return false;
        int v[4];
        size_t a = 3;
        for (int k = 0; k < 4; ++k)
        {
            size_t b = t.find(';', a);
            if (b == std::string::npos)
                return false;
            v[k] = std::atoi(t.substr(a, b - a).c_str());
            a = b + 1;
        }
        wander.store(std::clamp(v[0] / 1000.f, 0.f, 1.f));
        bars.store(std::clamp(v[1], 1, kMaxBars));
        const int n = std::clamp(v[3], 0, kMaxPoints);
        int got = 0;
        while (got < n && a < t.size())
        {
            size_t c = t.find(',', a);
            if (c == std::string::npos)
                break;
            size_t e = t.find(' ', c);
            float x = std::atoi(t.substr(a, c - a).c_str()) / 1000.f;
            float y = std::atoi(t.substr(c + 1, e == std::string::npos ? std::string::npos : e - c - 1).c_str()) / 1000.f;
            px[got].store(std::clamp(x, 0.f, 1.f));
            py[got].store(std::clamp(y, 0.f, 1.f));
            ++got;
            if (e == std::string::npos)
                break;
            a = e + 1;
        }
        recorded.store(got);
        loop.store(v[2] != 0 && got >= length());
        recording.store(false);
        return true;
    }

  private:
    std::array<std::atomic<float>, kMaxPoints> px, py;
    float s1[2]{0, 0}, s2[2]{0, 0}, tgt[2]{0, 0};
    double t{0}, nextKick{0};
    uint32_t rng{0x51ED270Bu};
    float urand()
    {
        rng = rng * 1664525u + 1013904223u;
        return (float)(rng >> 8) * (1.f / 16777216.f);
    }
};

} // namespace ghost

#endif
