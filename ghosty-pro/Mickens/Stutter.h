#pragma once
// Mr. Mickens 2: performance STUTTER. Tempo-synced and sample-accurate: it engages on the next 16th line (or at the
// exact sample of a MIDI note), grabs one RATE-long piece of the breaks bus and repeats it.
//   RATE 1/8 .. 1/64, 1/16T   GATE how much of each repeat sounds   DECAY quieter each repeat
//   PITCH semitones per repeat   MOVEMENT repeats swing across the stereo field + the filter wanders
//   ROLL  plain repeats       RAMP  repeats get shorter (a build-up)       GLITCH  each repeat picks its own length,
//   spot and direction        BREAKDOWN  repeats get longer and slower, darker and quieter (a tape-stop breakdown;
//   it stops by itself when it has faded below -60 dB, repeats never grow past 2 beats)
// Release is a 5 ms crossfade from the still-running repeat into the live break (no held sample, no DC step).
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <vector>

namespace mk {

enum StutterMode { kRoll = 0, kRamp = 1, kGlitchSt = 2, kBreakdown = 3 };
struct StutterParams { int mode = kRoll, div = 1; float gate = 1, decay = 0, pitch = 0, move = 0, cut = 0.75f, env = 0.5f, envDecay = 0.35f; };

class Stutter {
public:
    void prepare(double rate) { sr = rate; for (auto& b : buf) b.assign((size_t) (rate * 2.2), 0.0f); active = false; midiHeld = false; clock = 0; log.n = 0; k = 0; rel = 0; relLen = juce::jmax(16, (int) (0.005 * rate)); filt = SVF(); }
    // log of repeat starts (sample positions counted from prepare); tests read it
    struct Log { long long at[256]; int n = 0; } log;
    long long clock = 0;   // samples since prepare
    bool isActive() const { return active; }
    void reset() { active = false; midiHeld = false; spent = false; rel = 0; k = 0; filt = SVF(); }   // BYPASS engaged: drop the repeat (no allocation)

    static double divBeats(int d) { static const double D[] = { 0.5, 0.25, 0.125, 0.0625, 1.0 / 6.0 }; return D[juce::jlimit(0, 4, d)]; }

    // want: the stutter switch/param; midiAt: sample of a MIDI note-on in this block (-1 none), midiOffAt likewise.
    // b0: beat position at sample 0, spb: samples per beat
    void process(float* L, float* R, int n, const StutterParams& P, bool want, int midiAt, int midiOffAt, double b0, double spb, bool clockRunning) {
        // when does it switch on / off inside this block?
        int onAt = -1, offAt = -1;
        if (midiAt >= 0) { midiHeld = true; if (!active) onAt = midiAt; }
        if (midiOffAt >= 0) { midiHeld = false; if (!want) offAt = midiOffAt; }
        if (!want && !midiHeld) spent = false;
        if (want && !active && !spent && onAt < 0) onAt = nextGrid(b0, spb, n, clockRunning);
        if (!want && !midiHeld && active && offAt < 0 && midiOffAt < 0) offAt = nextGrid(b0, spb, n, clockRunning);
        const float envK = std::exp(-1.0f / (juce::jmax(0.01f, P.envDecay * 0.3f) * (float) sr));
        for (int i = 0; i < n; ++i, ++clock) {
            if (i == onAt && !active) engage(P, spb);
            if (i == offAt && active) release();
            float a = L[i], b = R[i];
            if (active || rel > 0) {
                if (posInRep >= repLen) { if (active) nextRepeat(P, spb); else posInRep = 0; }
                float wa, wb;
                if (k == 0) { if (rec < (int) buf[0].size()) { buf[0][(size_t) rec] = a; buf[1][(size_t) rec] = b; ++rec; } wa = a; wb = b; }
                else {
                    const double rp = repOffset + (repRev ? (repLen - 1 - posInRep) : posInRep) * repRate; const int len = juce::jmax(2, rec);
                    double q = std::fmod(rp, (double) len); if (q < 0) q += len; const int i0 = (int) q; const float t = (float) (q - i0); const int i1 = (i0 + 1) % len;
                    wa = buf[0][(size_t) i0] + (buf[0][(size_t) i1] - buf[0][(size_t) i0]) * t; wb = buf[1][(size_t) i0] + (buf[1][(size_t) i1] - buf[1][(size_t) i0]) * t;
                }
                // gate + edge fades so repeats click-free
                const double gl = juce::jmax(64.0, repLen * juce::jlimit(0.05f, 1.0f, P.gate));
                float g = repGain;
                const double edge = juce::jmin(48.0, repLen * 0.25);
                if (k > 0 && posInRep < edge) g *= (float) (posInRep / edge);
                if (posInRep > gl - edge) g *= (float) juce::jmax(0.0, (gl - posInRep) / edge);
                wa *= g * panL; wb *= g * panR;
                envNow *= envK;
                if ((i & 15) == 0 || posInRep == 0) filt.setup(cutHz(juce::jlimit(0.0f, 1.0f, P.cut + P.env * envNow * 0.5f + repCut)), 0.6f, (float) sr);
                const float fa = filt.tick(0, wa), fb = filt.tick(1, wb);
                posInRep += 1;
                if (active) { a = fa; b = fb; }
                else { const float t = rel / (float) relLen; a = a * (1 - t) + fa * t; b = b * (1 - t) + fb * t; --rel; }   // release: the repeat keeps running under the fade
            }
            L[i] = a; R[i] = b;
        }
    }

private:
    struct SVF { float s1[2] {}, s2[2] {}, a1 = 0, a2 = 0, a3 = 0, k = 1;
        void setup(float f, float q, float rate) { const float g = std::tan(juce::MathConstants<float>::pi * juce::jlimit(20.0f, rate * 0.45f, f) / rate); k = 2.0f - 1.9f * q; a1 = 1 / (1 + g * (g + k)); a2 = g * a1; a3 = g * a2; }
        float tick(int c, float x) { const float v3 = x - s2[c], v1 = a1 * s1[c] + a2 * v3, v2 = s2[c] + a2 * s1[c] + a3 * v3; s1[c] = 2 * v1 - s1[c]; s2[c] = 2 * v2 - s2[c]; return v2; } } filt;
    static float cutHz(float v) { return 30.0f * std::pow(600.0f, juce::jlimit(0.0f, 1.0f, v)); }
    static float hash(uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return (float) (x & 0xffffffU) / 16777216.0f; }
    int nextGrid(double b0, double spb, int n, bool running) const {
        if (!running || !std::isfinite(b0)) return 0;   // nothing to sync to: start right away
        const double g = std::ceil(b0 * 4.0 - 1e-9) / 4.0; const int at = (int) std::llround((g - b0) * spb);
        return at < n ? juce::jmax(0, at) : -1;
    }
    void engage(const StutterParams& P, double spb) {
        active = true; rel = 0; k = 0; rec = 0; posInRep = 0; envNow = 1; ++engageCount; elapsed = 0;
        len0 = spb * divBeats(P.div); repLen = std::round(len0); repRate = 1; repOffset = 0; repRev = false; repGain = 1; panL = panR = 1; repCut = 0;
        if (log.n < 256) log.at[log.n++] = clock;
    }
    void release() { active = false; rel = k > 0 ? relLen : 0; }
    void nextRepeat(const StutterParams& P, double spb) {
        elapsed += repLen; ++k; posInRep = 0; envNow = 1;
        double len = len0; repRate = std::exp2(juce::jlimit(-24.0f, 24.0f, P.pitch * (float) k) / 12.0f); repOffset = 0; repRev = false; repCut = 0;
        const float hk = hash((uint32_t) k * 7919u + (uint32_t) engageCount * 104729u);
        switch (P.mode) {
            case kRamp: len = juce::jmax(spb / 32.0, len0 * std::pow(0.8, (double) k)); break;
            case kGlitchSt: { static const double D[] = { 0.125, 0.25, 0.5, 1.0 / 6.0 }; len = spb * D[(int) (hk * 4) & 3]; repRev = hash((uint32_t) k * 31u + 5u + (uint32_t) engageCount) < 0.3f;
                              repOffset = std::floor(hash((uint32_t) k * 131u + 9u + (uint32_t) engageCount) * 4) * len0 * 0.25; break; }
            case kBreakdown: len = juce::jmin(spb * 2.0, len0 * std::pow(1.25, (double) k)); repRate *= std::exp2(-(double) k * (1.0 + std::abs(P.pitch) * 0.25) / 12.0); repCut = -0.08f * (float) k; break;
            default: break;
        }
        repRate = juce::jlimit(0.125, 4.0, repRate);
        // repeat starts on exact (rounded) multiples so ROLL never drifts from the grid
        const double target = (P.mode == kRoll) ? std::round(len0 * (k + 1)) : std::round(elapsed + len);
        repLen = juce::jmax(16.0, target - elapsed);
        repGain = (float) std::pow(1.0 - 0.45 * juce::jlimit(0.0f, 1.0f, P.decay), (double) k);
        if (P.mode == kBreakdown) repGain *= (float) std::pow(0.88, (double) k);
        const float side = (k & 1) ? 1.0f : -1.0f, m = juce::jlimit(0.0f, 1.0f, P.move) * 0.85f * side;
        panL = std::sqrt(juce::jlimit(0.0f, 2.0f, 1.0f - m)); panR = std::sqrt(juce::jlimit(0.0f, 2.0f, 1.0f + m));
        repCut += (hk - 0.5f) * 0.5f * P.move;
        if (log.n < 256) log.at[log.n++] = clock;
        if (P.mode == kBreakdown && repGain < 0.001f) { release(); spent = true; }   // faded below -60 dB: the breakdown is over (it won't restart until the switch is let go)
    }
    double sr = 48000, len0 = 1000, repLen = 1000, repRate = 1, repOffset = 0, elapsed = 0;
    double posInRep = 0; int k = 0, rec = 0; uint32_t engageCount = 0; bool active = false, midiHeld = false, repRev = false, spent = false; int rel = 0, relLen = 240;
    float repGain = 1, panL = 1, panR = 1, repCut = 0, envNow = 0;
    std::vector<float> buf[2];
};

} // namespace mk
