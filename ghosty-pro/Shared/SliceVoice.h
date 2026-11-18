#pragma once
// SliceVoice: the shared slice voice pool + the retrigger maths (GHOST-SLICE-STUDY.md 7.3 / 7.4).
// Written for Ghost Slice PLAY / REPEAT; meant to be shared later by Ghosty Beats (slice retrig, Qxy) and Mr. Mickens
// (break bays with ratchets). Neither of those uses it yet (their engines and tests stay as they are).
//
//   retrig::    trigger times: constant (t_n = n P), RAMP (exponential frequency glide, closed form), RATCHET curve (a drawn
//               log-rate curve integrated in 1 ms steps), SWING, fade fitting for very short periods.
//   Adsr        linear attack / decay / release, sustain level; sample-exact, no allocation.
//   VoicePool   N voices + a few GHOST slots. A voice plays a SampleView (two channel pointers, owned elsewhere and alive while
//               the voice sounds) at a pitch ratio (cubic Hermite read), forward or reversed, from a start frame, through an
//               ADSR, a velocity low-pass (2-pole TPT state variable filter), gain and equal-power pan.
//               Click-free by construction:
//                 * voice stealing (pool full: the oldest releasing voice, else the quietest, else the oldest): the stolen voice
//                   is MOVED into a ghost slot and fades out there over stealFadeMs (3 ms) while the new note starts at once;
//                 * the same note again / a choke group: the old voice fades the same way (ghost slot);
//                 * a retrigger (per-voice REPEAT): the old position fades out in a ghost slot (1 ms) while the voice restarts
//                   with a 1 ms fade in;
//                 * the slice end: a 1 ms tail guard; a start inside the audio (start offset) gets at least a 0.5 ms attack.
//               Zero latency: a note-on renders from the very next sample the caller asks for.
// Nothing here allocates or locks after prepare(); render() is noexcept and real-time safe.
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <cstdint>
#include <vector>
#include <array>
#include <algorithm>
#include <limits>

namespace ghosty::slicevoice
{
// ================================================================ retrigger maths
namespace retrig
{
    // the period of a note value: 1/div note (div = 4 .. 128); mod 0 = straight, 1 = triplet (2/3), 2 = dotted (3/2)
    inline double periodSeconds (double bpm, int div, int mod)
    {
        const double m = mod == 1 ? 2.0 / 3.0 : mod == 2 ? 1.5 : 1.0;
        return 240.0 / juce::jlimit (20.0, 999.0, bpm) / (double) juce::jlimit (1, 1024, div) * m;
    }
    // constant rate: t_n = n P, every t_n < D
    inline void constantTimes (double P, double D, std::vector<double>& out)
    {
        out.clear(); if (! (P > 0) || ! (D > 0)) return;
        for (int n = 0; ; ++n) { const double t = n * P; if (t >= D - 1e-12) break; out.push_back (t); if (out.size() > 100000) break; }
    }
    // RAMP: the trigger frequency glides exponentially from f0 = 1/P0 to f1 = k f0 over D.
    //   phase  Phi(t) = f0 D ((k)^(t/D) - 1) / ln k ;  trigger n where Phi(t_n) = n  ->  t_n = D ln (1 + n ln k / (f0 D)) / ln k
    inline double rampTime (int n, double f0, double k, double D)
    {
        if (std::abs (k - 1.0) < 1e-12) return n / f0;
        const double lk = std::log (k), arg = 1.0 + n * lk / (f0 * D);
        if (arg <= 0) return std::numeric_limits<double>::infinity();   // (a deceleration that never reaches the n-th trigger)
        return D * std::log (arg) / lk;
    }
    inline void rampTimes (double P0, double k, double D, std::vector<double>& out)
    {
        out.clear(); if (! (P0 > 0) || ! (D > 0) || ! (k > 0)) return;
        for (int n = 0; ; ++n) { const double t = rampTime (n, 1.0 / P0, k, D); if (! (t < D - 1e-12)) break; out.push_back (t); if (out.size() > 100000) break; }
    }
    // the RATCHET curve: npts points (x evenly over the span, values = log2 of the rate multiplier), linear between points
    inline double curveLog2 (const float* pts, int npts, double x)
    {
        if (npts <= 0) return 0.0;
        if (npts == 1) return pts[0];
        const double fx = juce::jlimit (0.0, 1.0, x) * (npts - 1);
        const int i = juce::jmin (npts - 2, (int) std::floor (fx));
        const double f = fx - i;
        return pts[i] + (pts[i + 1] - pts[i]) * f;
    }
    // rate f(x) = 2^curve(x) / P0, integrated numerically (dt steps, the phase inside a step linear), triggers where the phase
    // crosses integers (t_0 = 0)
    inline void curveTimes (double P0, const float* pts, int npts, double D, std::vector<double>& out, double dt = 0.001)
    {
        out.clear(); if (! (P0 > 0) || ! (D > 0)) return;
        out.push_back (0.0);
        double phase = 0, t = 0; int next = 1;
        while (t < D && out.size() < 100000)
        {
            const double h = std::min (dt, D - t);
            const double fa = std::exp2 (curveLog2 (pts, npts, t / D)) / P0, fb = std::exp2 (curveLog2 (pts, npts, (t + h) / D)) / P0;
            const double dphi = 0.5 * (fa + fb) * h;   // trapezoid
            while (phase + dphi >= (double) next && dphi > 0)
            {
                // inside the step the rate is ~linear: solve phase + fa s + (fb - fa) s^2 / (2h) = next for s
                const double need = (double) next - phase, a = 0.5 * (fb - fa) / h, b = fa;
                double s = std::abs (a) < 1e-15 ? need / b : (-b + std::sqrt (std::max (0.0, b * b + 4 * a * need))) / (2 * a);
                s = juce::jlimit (0.0, h, s);
                if (t + s < D - 1e-12) out.push_back (t + s);
                ++next;
            }
            phase += dphi; t += h;
        }
    }
    // SWING s in [0.5, 0.75]: odd triggers late by (s - 0.5) * 2 * P
    inline double swingOffset (int n, double swing, double P) { return (n & 1) ? (juce::jlimit (0.5, 0.75, swing) - 0.5) * 2.0 * P : 0.0; }
    // FADE fit: a period shorter than attack + release + 1 ms scales both down (each >= 0.25 ms)
    inline void fitFades (double P, double& a, double& r)
    {
        if (P >= a + r + 0.001) return;
        const double room = std::max (0.0005, P - 0.001), k = room / std::max (1e-9, a + r);
        a = std::max (0.00025, a * k); r = std::max (0.00025, r * k);
    }
}

// ================================================================ ADSR (times in samples)
struct Adsr
{
    enum Stage { Idle, Attack, Decay, Sustain, Release };
    Stage stage = Idle; float level = 0, sustain = 1; double aInc = 1, dDec = 0, rDec = 0;
    void start (double attackS, double decayS, float sustainLevel)
    {
        sustain = juce::jlimit (0.0f, 1.0f, sustainLevel);
        aInc = attackS > 0.5 ? 1.0 / attackS : 2.0; dDec = decayS > 0.5 ? (1.0 - sustain) / decayS : 2.0;
        stage = Attack; level = 0; if (aInc >= 1.0) { level = 1.0f; stage = sustain < 1.0f ? Decay : Sustain; }
    }
    void release (double releaseS) { if (stage == Idle) return; rDec = releaseS > 0.5 ? (double) level / releaseS : 2.0; if (rDec <= 0) rDec = 2.0; stage = Release; }
    inline float next() noexcept
    {
        const float out = level;
        switch (stage)
        {
            case Attack:  level = (float) std::min (1.0, (double) level + aInc); if (level >= 1.0f) { level = 1.0f; stage = sustain < 1.0f ? Decay : Sustain; } break;
            case Decay:   level = (float) std::max ((double) sustain, (double) level - dDec); if (level <= sustain) { level = sustain; stage = Sustain; } break;
            case Release: level = (float) std::max (0.0, (double) level - rDec); if (level <= 0.0f) { level = 0; stage = Idle; } break;
            case Idle: case Sustain: break;
        }
        return out;
    }
};

// ================================================================ the voice pool
struct SampleView
{
    const float* ch[2] { nullptr, nullptr };
    int64_t length = 0;
    bool valid() const { return ch[0] != nullptr && length > 1; }
};

struct VoiceStart
{
    int note = 60; float velocity = 1.0f;
    SampleView sample;
    double startFrame = 0;          // from the slice's start (forward) or its end (reversed)
    double pitchSemis = 0;          // the key's own pitch (bend comes on top)
    bool reverse = false, oneShot = true;
    double attackS = 0, decayS = 0, releaseS = 0.02; float sustain = 1.0f;
    float gain = 1.0f, pan = 0.0f;  // pan -1 .. 1 (equal power)
    int choke = 0;                  // 0 = none
    double cutoffHz = 0;            // 0 / >= 0.45 sr = open
    double retrigS = 0;             // > 0: restart every retrigS while it sounds (a REPEAT on the key)
    uint64_t tag = 0;               // the caller's tag (e.g. the table generation it reads from)
};

template <int kVoices, int kGhosts = 8>
class VoicePool
{
public:
    double stealFadeMs = 3.0, retrigFadeMs = 1.0, tailGuardMs = 1.0;

    void prepare (double sampleRate)
    {
        sr = sampleRate > 0 ? sampleRate : 48000.0;
        for (auto& v : voices) v = Voice();
        for (auto& v : ghosts) v = Voice();
        bendNow = bendTarget = 0; clock = 0;
        bendCoef = 1.0 - std::exp (-1.0 / (0.005 * sr));
    }
    void setPolyphony (int n) { poly = juce::jlimit (1, kVoices, n); }
    int polyphony() const { return poly; }
    void setBend (double semis) { bendTarget = semis; }
    double bend() const { return bendNow; }

    // a note-on: always sounds at once (stealing when full). Returns the voice index.
    int noteOn (const VoiceStart& s) noexcept
    {
        if (! s.sample.valid()) return -1;
        for (int i = 0; i < kVoices; ++i)
        {
            auto& v = voices[i];
            if (! v.on) continue;
            if (v.note == s.note || (s.choke > 0 && v.choke == s.choke)) { toGhost (v, stealFadeMs); v.on = false; }   // same key / choke group
        }
        int slot = -1, used = 0;
        for (int i = 0; i < kVoices; ++i) if (voices[i].on) ++used;
        if (used >= poly) slot = victim();
        else for (int i = 0; i < kVoices; ++i) if (! voices[i].on) { slot = i; break; }
        if (slot < 0) return -1;
        auto& v = voices[slot];
        if (v.on) { toGhost (v, stealFadeMs); ++steals; }
        v = Voice();
        v.on = true; v.note = s.note; v.choke = s.choke; v.oneShot = s.oneShot; v.reverse = s.reverse; v.tag = s.tag;
        v.sample = s.sample; v.age = ++clock; v.held = true;
        v.start = juce::jlimit (0.0, (double) s.sample.length - 1.0, s.startFrame);
        v.pos = v.reverse ? (double) s.sample.length - 1.0 - v.start : v.start;
        v.pitch = s.pitchSemis; v.ratio = std::exp2 ((v.pitch + bendNow) / 12.0); v.ratioBend = bendNow;
        const double minAttack = v.start > 0.5 ? 0.0005 * sr : 0.0;
        v.env.start (std::max (minAttack, s.attackS * sr), s.decayS * sr, s.sustain);
        v.releaseS = s.releaseS;
        const float vel = juce::jlimit (0.0f, 1.0f, s.velocity);
        juce::ignoreUnused (vel);
        const float p = juce::jlimit (-1.0f, 1.0f, s.pan);
        const float ang = (p + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
        v.gL = s.gain * std::cos (ang) * juce::MathConstants<float>::sqrt2; v.gR = s.gain * std::sin (ang) * juce::MathConstants<float>::sqrt2;
        if (s.cutoffHz > 0 && s.cutoffHz < 0.45 * sr)
        {
            v.filt = true; const double g = std::tan (juce::MathConstants<double>::pi * s.cutoffHz / sr), k = juce::MathConstants<double>::sqrt2;
            v.fa1 = 1.0 / (1.0 + g * (g + k)); v.fa2 = g * v.fa1; v.fa3 = g * v.fa2;
        }
        v.retrig = s.retrigS > 0 ? std::max (0.004 * sr, s.retrigS * sr) : 0.0; v.retrigAt = v.retrig;
        return slot;
    }
    void noteOff (int note) noexcept
    {
        for (auto& v : voices) if (v.on && v.held && v.note == note) { v.held = false; if (! v.oneShot) v.env.release (v.releaseS * sr); }
    }
    void allNotesOff (bool fast) noexcept
    {
        for (auto& v : voices) if (v.on) { v.held = false; if (fast) { toGhost (v, stealFadeMs); v.on = false; } else v.env.release (v.releaseS * sr); }
    }
    // ADDS n frames to L / R
    void render (float* L, float* R, int n) noexcept
    {
        int done = 0;
        while (done < n)
        {
            const int m = std::min (16, n - done);   // bend smoothing + the ratio update per 16 frames
            for (int i = 0; i < m; ++i) bendNow += (bendTarget - bendNow) * bendCoef;
            if (std::abs (bendNow - bendTarget) < 1e-6) bendNow = bendTarget;
            for (auto& v : voices) if (v.on) { if (v.ratioBend != bendNow) { v.ratio = std::exp2 ((v.pitch + bendNow) / 12.0); v.ratioBend = bendNow; } renderVoice (v, L + done, R + done, m, false); }
            for (auto& v : ghosts) if (v.on) { if (v.ratioBend != bendNow) { v.ratio = std::exp2 ((v.pitch + bendNow) / 12.0); v.ratioBend = bendNow; } renderVoice (v, L + done, R + done, m, true); }
            done += m;
        }
    }
    int active() const noexcept { int c = 0; for (auto& v : voices) c += v.on ? 1 : 0; return c; }
    int fading() const noexcept { int c = 0; for (auto& v : ghosts) c += v.on ? 1 : 0; return c; }
    bool sounding (int note) const noexcept { for (auto& v : voices) if (v.on && v.note == note) return true; return false; }
    // the smallest tag of any sounding voice (ghosts included); max() = none
    uint64_t minTag() const noexcept
    {
        uint64_t t = std::numeric_limits<uint64_t>::max();
        for (auto& v : voices) if (v.on) t = std::min (t, v.tag);
        for (auto& v : ghosts) if (v.on) t = std::min (t, v.tag);
        return t;
    }
    void retag (uint64_t from, uint64_t to) noexcept { for (auto& v : voices) if (v.on && v.tag == from) v.tag = to; for (auto& v : ghosts) if (v.on && v.tag == from) v.tag = to; }
    // fade out every voice whose tag is below t (its sample is going away)
    void fadeTagsBelow (uint64_t t) noexcept { for (auto& v : voices) if (v.on && v.tag < t) { toGhost (v, stealFadeMs); v.on = false; } }
    int steals = 0, ghostCuts = 0;

private:
    struct Voice
    {
        bool on = false, held = false, oneShot = true, reverse = false, filt = false;
        int note = -1, choke = 0; uint64_t age = 0, tag = 0;
        SampleView sample; double start = 0, pos = 0, ratio = 1, pitch = 0, ratioBend = 0;
        Adsr env; double releaseS = 0.02; float gL = 1, gR = 1;
        double fa1 = 0, fa2 = 0, fa3 = 0, ic1[2] {}, ic2[2] {};
        int fadePos = 0, fadeLen = 0;      // > 0: fading out (ghost) over fadeLen
        int inPos = 0, inLen = 0;          // > 0: a retrigger's fade in
        int skip = 0;                      // a ghost made in the middle of a chunk starts after this many frames
        double retrig = 0, retrigAt = 0;   // samples
    };
    Voice voices[kVoices] {};
    Voice ghosts[kGhosts] {};
    double sr = 48000, bendNow = 0, bendTarget = 0, bendCoef = 0.004; uint64_t clock = 0; int poly = kVoices;

    int victim() const noexcept
    {
        int best = -1; uint64_t bestAge = std::numeric_limits<uint64_t>::max();
        for (int i = 0; i < kVoices; ++i) if (voices[i].on && voices[i].env.stage == Adsr::Release && voices[i].age < bestAge) { best = i; bestAge = voices[i].age; }
        if (best >= 0) return best;
        float q = 1e30f;
        for (int i = 0; i < kVoices; ++i) if (voices[i].on) { const float lv = voices[i].env.level * std::max (voices[i].gL, voices[i].gR); if (lv < q - 1e-6f) { q = lv; best = i; } }
        if (best >= 0 && q < 0.05f) return best;   // clearly the quietest
        bestAge = std::numeric_limits<uint64_t>::max(); best = -1;
        for (int i = 0; i < kVoices; ++i) if (voices[i].on && voices[i].age < bestAge) { best = i; bestAge = voices[i].age; }
        return best;
    }
    void toGhost (const Voice& v, double fadeMs, int skip = 0) noexcept
    {
        int g = -1;
        for (int i = 0; i < kGhosts; ++i) if (! ghosts[i].on) { g = i; break; }
        if (g < 0)
        {   // all ghost slots busy: the one closest to silence makes room (its last few samples are cut; counted)
            int best = 0; double most = -1;
            for (int i = 0; i < kGhosts; ++i) { const double f = ghosts[i].fadeLen > 0 ? (double) ghosts[i].fadePos / ghosts[i].fadeLen : 0; if (f > most) { most = f; best = i; } }
            g = best; ++ghostCuts;
        }
        ghosts[g] = v; ghosts[g].on = true;
        ghosts[g].fadePos = 0; ghosts[g].fadeLen = std::max (1, (int) std::lround (fadeMs * 0.001 * sr));
        ghosts[g].retrig = 0; ghosts[g].skip = skip;
    }
    static inline float hermite (const float* x, int64_t len, double pos) noexcept
    {
        const int64_t i = (int64_t) std::floor (pos); const float f = (float) (pos - (double) i);
        auto at = [x, len] (int64_t k) { return k >= 0 && k < len ? x[k] : 0.0f; };
        const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }
    void renderVoice (Voice& v, float* L, float* R, int n, bool ghost) noexcept
    {
        const float* c0 = v.sample.ch[0]; const float* c1 = v.sample.ch[1] != nullptr ? v.sample.ch[1] : c0;
        const int64_t len = v.sample.length;
        const double guard = std::max (1.0, tailGuardMs * 0.001 * sr);
        for (int i = 0; i < n; ++i)
        {
            if (v.skip > 0) { --v.skip; continue; }
            const double remain = (v.reverse ? v.pos : (double) (len - 1) - v.pos) / std::max (1e-9, v.ratio);
            if (remain <= 0) { v.on = false; return; }
            float a = hermite (c0, len, v.pos), b = c1 == c0 ? a : hermite (c1, len, v.pos);
            if (v.filt)
            {
                double in[2] { a, b }; float o[2];
                for (int c = 0; c < 2; ++c)
                {
                    const double v3 = in[c] - v.ic2[c], v1 = v.fa1 * v.ic1[c] + v.fa2 * v3, v2 = v.ic2[c] + v.fa2 * v.ic1[c] + v.fa3 * v3;
                    v.ic1[c] = 2 * v1 - v.ic1[c]; v.ic2[c] = 2 * v2 - v.ic2[c]; o[c] = (float) v2;
                }
                a = o[0]; b = o[1];
            }
            float e = v.env.next();
            if (remain < guard) e *= (float) (remain / guard);
            if (v.inLen > 0) { e *= (float) (v.inPos + 1) / (float) v.inLen; if (++v.inPos >= v.inLen) v.inLen = 0; }
            if (ghost || v.fadeLen > 0)
            {
                const float f = 1.0f - (float) std::min (v.fadePos, v.fadeLen) / (float) std::max (1, v.fadeLen);
                e *= f * f * (3.0f - 2.0f * f);   // smoothstep to zero
                if (++v.fadePos >= v.fadeLen) { L[i] += a * e * v.gL; R[i] += b * e * v.gR; v.on = false; return; }
            }
            L[i] += a * e * v.gL; R[i] += b * e * v.gR;
            v.pos += v.reverse ? -v.ratio : v.ratio;
            if (v.env.stage == Adsr::Idle && ! ghost) { v.on = false; return; }
            if (! ghost && v.retrig > 0 && (v.held || v.oneShot) && --v.retrigAt <= 0)
            {   // REPEAT on the key: the old position fades out in a ghost slot, the voice restarts with a short fade in
                toGhost (v, retrigFadeMs, i + 1);
                v.pos = v.reverse ? (double) len - 1.0 - v.start : v.start;
                v.inPos = 0; v.inLen = std::max (1, (int) std::lround (retrigFadeMs * 0.001 * sr)); v.retrigAt = v.retrig;
            }
        }
    }
};
} // namespace ghosty::slicevoice
