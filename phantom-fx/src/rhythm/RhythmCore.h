// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX Rhythm group: shared pieces of our own glue.
//   Published<T>  lock-free hand-over of drawn data (message thread -> audio thread), a triple buffer: never blocks, never allocates
//   TimeHead      a read head on TIME-12's buffer (time12::Delay, cubic read) that crossfades over 4 ms whenever the read point jumps
//   timeModeDelay the time gestures (repeat, stutter, half speed, reverse, scratch, tape stop) as a read delay over a run of steps
//   HostClock     sample-exact song position inside a block, step boundaries, swing
#pragma once
#include "Foundations.h"
#include "../core/PhantomEffect.h"
#include <array>

namespace phantom::rhythm
{
constexpr double kPi = 3.14159265358979323846;
inline double positiveMod (double x, double m) noexcept { const double r = std::fmod (x, m); return r < 0 ? r + m : r; }
inline uint32_t hash32 (uint32_t x) noexcept { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
inline float hashFloat (uint32_t x) noexcept { return (float) (hash32 (x) & 0xffffff) / 16777216.0f; }

// ---------------------------------------------------------------- triple buffer
template <typename T>
class Published
{
public:
    // message thread: fill edit() completely, then publish()
    T& edit() noexcept { return buf[(size_t) back]; }
    void publish() noexcept { back = middle.exchange (back | kNew, std::memory_order_acq_rel) & 3; }
    void publishAll (const T& v) { for (auto& b : buf) b = v; }   // prepare / construction only (no reader running)
    // audio thread: take the newest published copy (if any), then read()
    bool fetch() noexcept
    {
        if ((middle.load (std::memory_order_acquire) & kNew) == 0) return false;
        front = middle.exchange (front, std::memory_order_acq_rel) & 3;
        return true;
    }
    const T& read() const noexcept { return buf[(size_t) front]; }
private:
    static constexpr int kNew = 4;
    std::array<T, 3> buf {};
    int front = 0, back = 2;
    std::atomic<int> middle { 1 };
};

// ---------------------------------------------------------------- the song position inside a block (one host clock)
struct HostClock
{
    double ppq0 = 0.0, spb = 24000.0;   // song position at sample 0, samples per beat
    void set (const TempoInfo& t) noexcept { ppq0 = t.ppqPosition; spb = t.samplesPerBeat(); }
    double ppqAt (double sample) const noexcept { return ppq0 + sample / spb; }
    // the first sample (>= from) at which the song position reaches the next multiple of `unitBeats`; n if none in this block
    int nextBoundary (int from, int n, double unitBeats) const noexcept
    {
        const double p = ppqAt (from) / unitBeats;
        double k = std::floor (p + 1.0e-9) + 1.0;
        const double s = (k * unitBeats - ppq0) * spb;
        int si = juce::jlimit (from + 1, n, (int) std::ceil (s - 1.0e-6));
        // settle on the exact definition: the first sample whose own position lies in the next unit (unitIndex), whatever the rounding
        const long long cur = unitIndex (from, unitBeats);
        while (si > from + 1 && unitIndex (si - 1, unitBeats) != cur) --si;
        while (si < n && unitIndex (si, unitBeats) == cur) ++si;
        return si;
    }
    // index of the unit that sample `i` lies in (consistent with nextBoundary)
    long long unitIndex (int i, double unitBeats) const noexcept { return (long long) std::floor (ppqAt (i) / unitBeats + 1.0e-9); }
};

// swing: the first 16th of each pair lasts (1 + a) steps and the second (1 - a), a = swing * 1/2 (100 % = a 3:1 feel)
inline double swingSteps (double pSteps, float swing) noexcept
{
    const double a = 0.5 * juce::jlimit (0.0f, 1.0f, swing);
    if (a <= 1.0e-6) return pSteps;
    const double pair = std::floor (pSteps * 0.5), f = pSteps - 2.0 * pair;
    const double fs = f < 1.0 + a ? f / (1.0 + a) : 1.0 + (f - (1.0 + a)) / (1.0 - a);
    return 2.0 * pair + fs;
}

// ---------------------------------------------------------------- a stereo buffer on TIME-12's Delay
struct TapeBuffer
{
    std::array<time12::Delay, 2> d;
    void prepare (int samples) { for (auto& x : d) { x.resize (samples, true); x.clear(); } }
    void clear() { for (auto& x : d) x.clear(); }
    inline void write (float l, float r) noexcept { d[0].write (l); d[1].write (r); }
    int size() const noexcept { return d[0].size; }
    // D = samples back from the newest written sample (0 = the sample just written); TIME-12 reads `1 + D`
    inline float read (int ch, double D) noexcept { return (float) d[(size_t) ch].read3 (1.0 + juce::jlimit (0.0, (double) d[0].size - 4.0, D)); }
};

// a read head that glides while the read point moves smoothly and crossfades (raised cosine, 4 ms) when it jumps
struct TimeHead
{
    double prevD = 0, dD = 0, oldD = 0, oldStep = 0, curD = 0; int xf = 0, xfLen = 192; float wNew = 1, wOld = 0;
    void prepare (double sr) { xfLen = std::max (16, (int) (sr * 0.004)); reset(); }
    void reset() noexcept { prevD = dD = oldD = oldStep = curD = 0; xf = 0; wNew = 1; wOld = 0; }
    bool idle() const noexcept { return curD < 1.0e-3 && wOld <= 0.0f; }
    inline void advance (double D) noexcept
    {
        const double pred = prevD + dD;
        if (std::abs (D - pred) > 2.0) { oldD = pred; oldStep = juce::jlimit (-1.0, 3.0, dD); xf = xfLen; dD = 0; }
        else dD = D - prevD;
        prevD = D; curD = D;
        if (xf > 0)
        {
            oldD = std::max (0.0, oldD + oldStep);
            const float a = (float) xf / (float) xfLen;   // 1 -> 0
            wOld = 0.5f - 0.5f * (float) std::cos (kPi * a); wNew = 1.0f - wOld; --xf;
        }
        else { wNew = 1; wOld = 0; }
    }
    inline float tap (TapeBuffer& b, int ch, float live) noexcept
    {
        float y = wNew * (curD < 1.0e-3 ? live : b.read (ch, curD));
        if (wOld > 0) y += wOld * (oldD < 1.0e-3 ? live : b.read (ch, oldD));
        return y;
    }
};

// ---------------------------------------------------------------- time gestures as a read delay over a run of steps
enum TimeMode { tmNormal, tmRepeat, tmStutter, tmHalf, tmReverse, tmScratch, tmStop, kNumTimeModes };
inline const char* timeModeName (int m) { static const char* n[] { "-", "REP", "STUT", "HALF", "REV", "SCR", "STOP" }; return n[juce::jlimit (0, kNumTimeModes - 1, m)]; }
inline juce::uint32 timeModeColour (int m)
{
    static const juce::uint32 c[] { 0xff2a2e3e, 0xff45E6FF, 0xffFF4FD8, 0xffA06BFF, 0xffFFC35A, 0xff3CFF8F, 0xffFF5A6E };
    return c[juce::jlimit (0, kNumTimeModes - 1, m)];
}
// oRun = samples since the run started, runLen = run length, stepLen = one step, oStep = samples since the step started
inline double timeModeDelay (int mode, double oRun, double runLen, double stepLen, double oStep, float amount = 0.5f) noexcept
{
    switch (mode)
    {
        case tmRepeat:  { const double L = std::max (16.0, stepLen); return oRun - std::fmod (oRun, L); }
        case tmStutter: { const double L = std::max (16.0, stepLen * (0.5 - 0.375 * amount)); return oRun - std::fmod (oRun, L); }
        case tmHalf:    return oRun * 0.5;
        case tmReverse: return 2.0 * oRun;
        case tmScratch: return stepLen * (0.08 + 0.14 * amount) * (1.0 - std::cos (4.0 * kPi * oStep / std::max (1.0, stepLen)));
        case tmStop:    { const double e = 1.0 + 1.5 * amount; const double u = std::min (1.0, oRun / std::max (1.0, runLen));
                          return oRun - runLen * (1.0 - std::pow (1.0 - u, e + 1.0)) / (e + 1.0); }   // speed (1-u)^e
        default:        return 0.0;
    }
}

// a gain that never clicks: an attack / release slew in two one-pole stages
struct GainSlew
{
    float a = 1, b = 1, kUp = 0.1f, kDown = 0.1f;
    void set (double sr, float upMs, float downMs) noexcept
    { kUp = 1.0f - (float) std::exp (-1.0 / (sr * 0.001 * std::max (0.05f, upMs) * 0.5)); kDown = 1.0f - (float) std::exp (-1.0 / (sr * 0.001 * std::max (0.05f, downMs) * 0.5)); }
    void snap (float v) noexcept { a = b = v; }
    inline float next (float target) noexcept { const float k = target > a ? kUp : kDown; a += k * (target - a); b += k * (a - b); return b; }
};
} // namespace phantom::rhythm
