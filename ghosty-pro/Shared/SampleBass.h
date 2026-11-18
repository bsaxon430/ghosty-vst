#pragma once
// SampleBass (owner 2026-11-03): the ACID page's BASS SAMPLES sound. One of the 17 Block Step basses plays the acid line, pitched
// to every note: mono legato like a 303 (a note that starts while the last one's gate is still open = a pitch glide, no new attack;
// otherwise a new attack, the old note fading out in 4 ms), the gate cut by the note length with a 12 ms release, accents louder
// with a little drive. Pitch: 2^((note - root) / 12) times the sample's speed (the root measured offline, Shared/BassRoots.h),
// read with a windowed-sinc interpolator (Blackman-Harris, 8 zero crossings each side) whose cutoff follows the speed, so notes
// above the root don't alias.
// Real-time: no allocation, no locks; note() and render() run on the audio thread (the engine's scheduler calls note()).
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>
#include <cstdint>

namespace ghosty::samplebass {

struct Kernel {   // the windowed sinc, tabulated (x in zero crossings, 0 .. kHalf)
    static constexpr int kHalf = 8, kRes = 1024;
    std::array<float, kHalf * kRes + 2> t {};
    Kernel() {
        for (int i = 0; i <= kHalf * kRes + 1; ++i) {
            const double x = (double) i / kRes; if (x >= kHalf) { t[(size_t) i] = 0.0f; continue; }
            const double s = x < 1.0e-9 ? 1.0 : std::sin(juce::MathConstants<double>::pi * x) / (juce::MathConstants<double>::pi * x);
            const double u = 0.5 + 0.5 * x / kHalf, tp = juce::MathConstants<double>::twoPi;   // Blackman-Harris over [-kHalf, kHalf]
            const double w = 0.35875 - 0.48829 * std::cos(tp * u) + 0.14128 * std::cos(2 * tp * u) - 0.01168 * std::cos(3 * tp * u);
            t[(size_t) i] = (float) (s * w); }
    }
    float at(float x) const { x = std::abs(x); if (x >= (float) kHalf) return 0.0f; const float p = x * kRes; const int i = (int) p; const float f = p - (float) i; return t[(size_t) i] + (t[(size_t) i + 1] - t[(size_t) i]) * f; }
};
inline const Kernel& kernel() { static const Kernel k; return k; }

// the sample at fractional position pos, band-limited to fc (1 = the sample's own Nyquist; < 1 when it is read faster)
inline float readSinc(const float* s, int len, double pos, float fc) {
    const auto& K = kernel(); const int i0 = (int) std::floor(pos); const float fr = (float) (pos - i0);
    const int W = (int) std::ceil((float) Kernel::kHalf / fc); float acc = 0.0f;
    const int a = juce::jmax(0, i0 - W + 1), b = juce::jmin(len - 1, i0 + W);
    for (int n = a; n <= b; ++n) acc += s[n] * K.at(((float) (n - i0) - fr) * fc);
    return acc * fc;
}

struct Note {
    int64_t t = 0, off = 0;          // start and gate end (absolute samples, the engine's clock)
    float midi = 36, vel = 1, gain = 1, pan = 0; bool accent = false;
    const juce::AudioBuffer<float>* buf = nullptr; double srcRate = 44100; float root = 36;
};

class Voice {
public:
    static constexpr int kQueue = 64;
    void prepare(double rate) { sr = rate > 0 ? rate : 44100.0; (void) kernel(); reset();
        glideK = (float) (1.0 - std::exp(-1.0 / (0.022 * sr)));   // the slide: ~22 ms time constant (a 303 slide is ~60 ms to arrive)
        gainK = (float) (1.0 - std::exp(-1.0 / (0.004 * sr))); }
    void reset() { nq = 0; cur = {}; old = {}; }
    bool active() const { return cur.on || old.on || nq > 0; }
    // schedule a note (audio thread). Two notes starting together (a chord): the lowest is the bass, the rest are dropped.
    void note(const Note& n) {
        if (n.buf == nullptr || n.buf->getNumSamples() < 4 || n.off <= n.t) return;
        for (int k = 0; k < nq; ++k) if (q[(size_t) k].t == n.t) { if (n.midi < q[(size_t) k].midi) q[(size_t) k] = n; return; }
        if (nq >= kQueue) return;
        int k = nq++; while (k > 0 && q[(size_t) k - 1].t > n.t) { q[(size_t) k] = q[(size_t) k - 1]; --k; } q[(size_t) k] = n;
    }
    // adds n samples starting at absolute sample `now` into L (and R; R == nullptr: mono, L gets the sum)
    void render(float* L, float* R, int n, int64_t now) {
        if (! cur.on && ! old.on && nq == 0) return;
        for (int i = 0; i < n; ++i) {
            const int64_t at = now + i;
            while (nq > 0 && q[0].t <= at) { if (q[0].t >= at - 4096) start(q[0], at); for (int k = 1; k < nq; ++k) q[(size_t) k - 1] = q[(size_t) k]; --nq; }   // (a note far in the past is dropped)
            float l = 0, r = 0; tick(cur, at, l, r); tick(old, at, l, r);
            if (R != nullptr) { L[i] += l; R[i] += r; } else L[i] += 0.5f * (l + r);
        }
    }
    int pending() const { return nq; }
private:
    struct Play {
        bool on = false; const juce::AudioBuffer<float>* buf = nullptr; double srcRate = 44100, pos = 0; float root = 36, semis = 36, target = 36;
        float g = 0, gT = 0, pan = 0; bool accent = false, accT = false; int64_t off = 0;
        float env = 0; int stage = 0; float step = 0;   // stage 0 attack, 1 hold, 2 release, 3 quick fade (a newer note took over)
    };
    void start(const Note& n, int64_t at) {
        const float lvl = n.vel * n.gain * (n.accent ? 1.3f : 1.0f);
        if (cur.on && cur.stage < 2 && cur.off > n.t && cur.buf == n.buf) {   // legato: the gate is still open = glide there
            cur.target = n.midi; cur.off = n.off; cur.gT = lvl; cur.pan = n.pan; cur.accT = n.accent; return; }
        if (cur.on) { old = cur; old.stage = 3; old.step = old.env / (float) (0.004 * sr); }
        cur = {}; cur.on = true; cur.buf = n.buf; cur.srcRate = n.srcRate; cur.root = n.root; cur.semis = cur.target = n.midi;
        cur.g = cur.gT = lvl; cur.pan = n.pan; cur.accent = cur.accT = n.accent; cur.off = n.off; cur.stage = 0; cur.env = 0; cur.step = 1.0f / (float) juce::jmax(1.0, 0.0015 * sr);
        cur.pos = (double) (at - n.t) * std::exp2((n.midi - n.root) / 12.0) * n.srcRate / sr;   // (a note that should have started a few samples ago)
    }
    void tick(Play& v, int64_t at, float& l, float& r) {
        if (! v.on) return;
        if (v.stage < 2 && at >= v.off) { v.stage = 2; v.step = v.env / (float) (0.012 * sr); }
        if (v.stage == 0) { v.env += v.step; if (v.env >= 1.0f) { v.env = 1.0f; v.stage = 1; } }
        else if (v.stage >= 2) { v.env -= v.step; if (v.env <= 0.0f) { v.on = false; return; } }
        v.semis += (v.target - v.semis) * glideK; v.g += (v.gT - v.g) * gainK; v.accent = v.accT;
        const double ratio = std::exp2((v.semis - v.root) / 12.0) * v.srcRate / sr; const int len = v.buf->getNumSamples();
        if (v.pos >= len - 1) { v.on = false; return; }
        const float fc = ratio > 1.0 ? (float) (0.97 / juce::jmin(ratio, 8.0)) : 0.97f;
        float a = readSinc(v.buf->getReadPointer(0), len, v.pos, fc), b = v.buf->getNumChannels() > 1 ? readSinc(v.buf->getReadPointer(1), len, v.pos, fc) : a;
        if (v.accent) { a = std::tanh(a * 1.8f) * 0.72f; b = std::tanh(b * 1.8f) * 0.72f; }   // ACCENT: a little drive (about the same level for quiet parts)
        const float e = v.env * v.g, pl = v.pan > 0 ? 1.0f - v.pan : 1.0f, pr = v.pan < 0 ? 1.0f + v.pan : 1.0f;
        l += a * e * pl; r += b * e * pr; v.pos += ratio;
    }
    double sr = 44100; float glideK = 0.001f, gainK = 0.005f;
    std::array<Note, kQueue> q {}; int nq = 0; Play cur, old;
};

} // namespace ghosty::samplebass
