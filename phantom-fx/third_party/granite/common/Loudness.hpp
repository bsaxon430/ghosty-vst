// epsilon-common — loudness metering after ITU-R BS.1770-4 / EBU R128: K-weighting, momentary (400 ms),
// short-term (3 s), integrated (gated: -70 LUFS absolute, -10 LU relative), loudness range (EBU Tech 3342:
// short-term values, -70 absolute and -20 LU relative gates, 10th to 95th percentile) and true peak (4x
// oversampled). Stereo; both channels weighted 1. Written in the audio thread, read by the UI (plain floats:
// a torn read only shows a value from one block earlier). License: MIT.
#pragma once
#include "Halfband.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace epsilon {

class LoudnessMeter {
public:
    static constexpr float kSilence = -144.f;

    void setSampleRate(float sr) {
        fs = sr;
        // K-weighting: a high shelf (about +4 dB above 2 kHz) and a high-pass (RLB) at about 38 Hz, designed for any rate
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan(3.141592653589793 * f0 / fs), Vh = std::pow(10.0, G / 20.0), Vb = std::pow(Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            s1b[0] = (Vh + Vb * K / Q + K * K) / a0; s1b[1] = 2.0 * (K * K - Vh) / a0; s1b[2] = (Vh - Vb * K / Q + K * K) / a0;
            s1a[0] = 2.0 * (K * K - 1.0) / a0; s1a[1] = (1.0 - K / Q + K * K) / a0;
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan(3.141592653589793 * f0 / fs), a0 = 1.0 + K / Q + K * K;
            s2a[0] = 2.0 * (K * K - 1.0) / a0; s2a[1] = (1.0 - K / Q + K * K) / a0;
        }
        blockLen = std::max(1, (int)std::lround(0.1 * fs));
        reset();
    }
    void reset() {
        std::memset(z1, 0, sizeof z1); std::memset(z2, 0, sizeof z2);
        acc = 0; accN = 0; std::fill(blocks, blocks + kBlocks, 0.0); nBlocks = 0; head = 0;
        std::fill(gHist, gHist + kBins, 0.0); std::fill(gCount, gCount + kBins, 0u);
        std::fill(sHist, sHist + kBins, 0u);
        mom = shortT = integ = kSilence; lraV = 0; tp = 0; sp = 0;
        for (int c = 0; c < 2; ++c) os[c].reset();
    }
    // reset only the integrated value and loudness range (what a "reset" button does)
    void resetIntegrated() { std::fill(gHist, gHist + kBins, 0.0); std::fill(gCount, gCount + kBins, 0u); std::fill(sHist, sHist + kBins, 0u); integ = kSilence; lraV = 0; tp = 0; sp = 0; }

    void process(const float* l, const float* r, uint32_t n) {
        for (uint32_t i = 0; i < n; ++i) {
            const float x[2] = { l[i], r[i] };
            double ms = 0;
            for (int c = 0; c < 2; ++c) {
                // stage 1 (shelf), stage 2 (high-pass), transposed direct form II in double
                const double v = x[c];
                const double y1 = s1b[0] * v + z1[c][0];
                z1[c][0] = s1b[1] * v - s1a[0] * y1 + z1[c][1];
                z1[c][1] = s1b[2] * v - s1a[1] * y1;
                const double y2 = y1 + z2[c][0];
                z2[c][0] = -2.0 * y1 - s2a[0] * y2 + z2[c][1];
                z2[c][1] = y1 - s2a[1] * y2;
                ms += y2 * y2;
                // peaks
                sp = std::max(sp, std::fabs(x[c]));
                float up[4]; os[c].up(x[c], 4, up);
                for (float u : up) tp = std::max(tp, std::fabs(u));
            }
            acc += ms;
            if (++accN >= blockLen) { pushBlock(acc / accN); acc = 0; accN = 0; }
        }
    }

    float momentary() const { return mom; }        // LUFS, 400 ms
    float shortTerm() const { return shortT; }     // LUFS, 3 s
    float integrated() const { return integ; }     // LUFS, gated, since reset
    float range() const { return lraV; }           // LU
    float truePeakDb() const { return 20.f * std::log10(std::max(tp, 1e-9f)); }   // dBTP (4x oversampled, linear-phase interpolation)
    float samplePeakDb() const { return 20.f * std::log10(std::max(sp, 1e-9f)); }
    // latency of the true-peak path (base-rate samples); the loudness values have none beyond their windows
    static int truePeakLatency() { return OversamplerN::upLatency(4); }

private:
    static constexpr int kBlocks = 30, kBins = 1000;       // 3 s of 100 ms blocks; histogram: -70 .. +30 LUFS in 0.1 LU
    static float lufs(double meanSquare) { return meanSquare > 1e-20 ? (float)(-0.691 + 10.0 * std::log10(meanSquare)) : kSilence; }
    static int bin(float l) { return std::min(kBins - 1, std::max(0, (int)((l + 70.f) * 10.f))); }
    double mean(int count) const {
        double s = 0; const int k = std::min(count, nBlocks);
        for (int i = 0; i < k; ++i) s += blocks[(head - 1 - i + kBlocks) % kBlocks];
        return k ? s / k : 0.0;
    }
    void pushBlock(double ms) {
        blocks[head] = ms; head = (head + 1) % kBlocks; nBlocks = std::min(nBlocks + 1, kBlocks);
        if (nBlocks >= 4) {
            const double m = mean(4); mom = lufs(m);
            if (mom > -70.f) { gHist[bin(mom)] += m; gCount[bin(mom)]++; }       // gating block (400 ms, 75% overlap)
            updateIntegrated();
        }
        if (nBlocks >= kBlocks) {
            shortT = lufs(mean(kBlocks));
            if (shortT > -70.f) { sHist[bin(shortT)]++; updateRange(); }
        } else shortT = lufs(mean(nBlocks));
    }
    void updateIntegrated() {
        double s = 0; uint64_t n = 0;
        for (int b = 0; b < kBins; ++b) { s += gHist[b]; n += gCount[b]; }
        if (!n) { integ = kSilence; return; }
        const float rel = lufs(s / n) - 10.f;
        double s2 = 0; uint64_t n2 = 0;
        for (int b = bin(rel); b < kBins; ++b) { s2 += gHist[b]; n2 += gCount[b]; }
        integ = n2 ? lufs(s2 / n2) : kSilence;
    }
    void updateRange() {
        // relative gate: -20 LU below the mean (in the energy domain) of the short-term values above -70
        double e = 0; uint64_t n = 0;
        for (int b = 0; b < kBins; ++b) if (sHist[b]) { e += sHist[b] * std::pow(10.0, (-70.0 + (b + 0.5) / 10.0) / 10.0); n += sHist[b]; }
        if (!n) { lraV = 0; return; }
        const float gate = (float)(10.0 * std::log10(e / n)) - 20.f;
        uint64_t total = 0; for (int b = bin(gate); b < kBins; ++b) total += sHist[b];
        if (!total) { lraV = 0; return; }
        float lo = 0, hi = 0; uint64_t c = 0; bool gotLo = false;
        for (int b = bin(gate); b < kBins; ++b) {
            c += sHist[b];
            if (!gotLo && c >= (uint64_t)std::ceil(0.10 * total)) { lo = -70.f + (b + 0.5f) / 10.f; gotLo = true; }
            if (c >= (uint64_t)std::ceil(0.95 * total)) { hi = -70.f + (b + 0.5f) / 10.f; break; }
        }
        lraV = std::max(0.f, hi - lo);
    }

    float fs = 48000.f;
    double s1b[3] = {}, s1a[2] = {}, s2a[2] = {};
    double z1[2][2] = {}, z2[2][2] = {};
    double acc = 0; int accN = 0, blockLen = 4800;
    double blocks[kBlocks] = {}; int nBlocks = 0, head = 0;
    double gHist[kBins] = {}; uint32_t gCount[kBins] = {}; uint32_t sHist[kBins] = {};
    float mom = kSilence, shortT = kSilence, integ = kSilence, lraV = 0, tp = 0, sp = 0;
    OversamplerN os[2];
};

} // namespace epsilon
