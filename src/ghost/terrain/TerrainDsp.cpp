#include "TerrainDsp.h"
#include <cmath>
#include <complex>
#include <vector>
#include <cstring>

namespace ghost::terrain
{
// ================================================================================================ hand-off
TerrainShare::TerrainShare() {}

void TerrainShare::publish (const Terrain& t) noexcept
{
    slots[(size_t) back] = t;
    const int old = middle.exchange (back | 4, std::memory_order_acq_rel);
    back = old & 3;
}

bool TerrainShare::pull (Terrain& out) noexcept
{
    if ((middle.load (std::memory_order_acquire) & 4) == 0) return false;
    const int old = middle.exchange (front, std::memory_order_acq_rel);
    front = old & 3;
    out = slots[(size_t) front];
    return true;
}

// ================================================================================================ the bank
void TerrainBank::prepare (double sr, int)
{
    sampleRate = sr > 1000.0 ? sr : 48000.0;
    reset();
}

void TerrainBank::reset() noexcept
{
    for (auto& s : state)
    {
        for (int c = 0; c < kMaxChannels; ++c) { s.z1[c] = 0.0f; s.z2[c] = 0.0f; }
        s.on = false; s.mix = 0.0f; s.drive = 0.0f; s.dmix = 0.0f; s.ddrive = 0.0f;
    }
    amountNow = amount;
    active = 0;
}

void TerrainBank::targets (int numSamples) noexcept
{
    Band bands[kBands];
    const int n = haveTerrain ? bandsAt (terrain, depth, bands, range) : 0;
    const float inv = 1.0f / (float) tj::jmax (1, numSamples);
    const float amountTarget = tj::jlimit (0.0f, 1.0f, amount);
    const float amountStep = (amountTarget - amountNow) * inv;
    active = 0;
    for (int i = 0; i < kBands; ++i)
    {
        auto& s = state[(size_t) i];
        float gainDb = 0.0f, q = 1.0f, hz = 1000.0f, drive = 0.0f;
        bool want = i < n;
        if (want)
        {
            gainDb = bands[i].gainDb; q = bands[i].q; hz = bands[i].hz; drive = bands[i].drive;
            if (resonanceOnly) gainDb = tj::jmax (0.0f, gainDb) * 0.5f;
            want = std::abs (gainDb) > 0.02f || drive > 0.001f;
        }
        // coefficients: an RBJ bandpass with 0 dB peak gain at hz
        const float fHz = tj::jlimit (20.0f, (float) sampleRate * 0.45f, hz);
        const float w = tj::twoPi * fHz / (float) sampleRate;
        const float sn = std::sin (w), cs = std::cos (w);
        const float alpha = sn / (2.0f * tj::jlimit (0.3f, 60.0f, q));
        const float a0 = 1.0f + alpha;
        const float tb0 = alpha / a0, tb2 = -alpha / a0, ta1 = -2.0f * cs / a0, ta2 = (1.0f - alpha) / a0;
        const float gainLin = std::pow (10.0f, gainDb / 20.0f);
        const float tmix = want ? (gainLin - 1.0f) * amountTarget : 0.0f;
        const float tdrive = want ? drive * amountTarget : 0.0f;
        s.hz = fHz;
        if (! s.on)
        {
            if (! want) { s.dmix = 0.0f; s.ddrive = 0.0f; continue; }
            // a band comes alive: its filter starts at its target shape, only the mix fades in
            s.on = true;
            s.b0 = tb0; s.b2 = tb2; s.a1 = ta1; s.a2 = ta2;
            s.db0 = s.db2 = s.da1 = s.da2 = 0.0f;
            s.mix = 0.0f; s.drive = 0.0f;
            for (int c = 0; c < kMaxChannels; ++c) { s.z1[c] = 0.0f; s.z2[c] = 0.0f; }
        }
        else
        {
            s.db0 = (tb0 - s.b0) * inv; s.db2 = (tb2 - s.b2) * inv; s.da1 = (ta1 - s.a1) * inv; s.da2 = (ta2 - s.a2) * inv;
        }
        s.dmix = (tmix - s.mix) * inv;
        s.ddrive = (tdrive - s.drive) * inv;
        if (! want && std::abs (s.mix) < 1e-4f && s.drive < 1e-4f) { s.on = false; s.mix = 0.0f; s.drive = 0.0f; s.dmix = 0.0f; s.ddrive = 0.0f; continue; }
        ++active;
    }
    amountNow += amountStep * (float) numSamples;
    amountNow = tj::jlimit (0.0f, 1.0f, amountNow);
}

void TerrainBank::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    numChannels = tj::jmin (numChannels, kMaxChannels);
    if (numSamples <= 0 || numChannels <= 0) return;
    if (! begun) targets (numSamples);
    begun = false;
    if (active == 0) return;
    for (int n = 0; n < numSamples; ++n)
    {
        for (int i = 0; i < kBands; ++i)
        {
            auto& s = state[(size_t) i];
            if (! s.on) continue;
            const float k = 1.0f + 6.0f * s.drive;
            const float norm = s.drive > 1e-3f ? 0.25f / softLimit (k * 0.25f) : 1.0f;
            for (int c = 0; c < numChannels; ++c)
            {
                const float x = channels[c][n];
                const float y = s.b0 * x + s.z1[c];        // transposed direct form II (b1 = 0)
                s.z1[c] = s.z2[c] - s.a1 * y;
                s.z2[c] = s.b2 * x - s.a2 * y;
                float v = y;
                if (s.drive > 1e-3f) v = y + s.drive * (softLimit (k * y) * norm - y);
                channels[c][n] = x + s.mix * v;
            }
            s.b0 += s.db0; s.b2 += s.db2; s.a1 += s.da1; s.a2 += s.da2; s.mix += s.dmix; s.drive += s.ddrive;
        }
    }
    // keep the filters from drifting into denormals when idle
    for (auto& s : state)
        for (int c = 0; c < numChannels; ++c)
        {
            if (std::abs (s.z1[c]) < 1e-25f) s.z1[c] = 0.0f;
            if (std::abs (s.z2[c]) < 1e-25f) s.z2[c] = 0.0f;
        }
}

// ================================================================================================ offline
namespace
{
// a small in-place radix-2 FFT (complex); n a power of two
void fft (std::complex<float>* a, int n, bool inverse)
{
    for (int i = 1, j = 0; i < n; ++i)
    {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (int len = 2; len <= n; len <<= 1)
    {
        const double ang = 2.0 * tj::pi / len * (inverse ? 1.0 : -1.0);
        const std::complex<float> wl ((float) std::cos (ang), (float) std::sin (ang));
        for (int i = 0; i < n; i += len)
        {
            std::complex<float> w (1.0f, 0.0f);
            for (int j = 0; j < len / 2; ++j)
            {
                const auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    if (inverse) for (int i = 0; i < n; ++i) a[i] /= (float) n;
}
}

float sculptGainDb (const Terrain& t, float hz, float z, const SculptOptions& o)
{
    return t.heightAt (hzToX (hz, o.range), z) * o.range.maxDb * o.strength;
}

void sculpt (const Terrain& t, const float* const* in, float* const* out, int channels, int numSamples, double sampleRate, const SculptOptions& o)
{
    if (numSamples <= 0 || channels <= 0) return;
    const int N = 1 << tj::jlimit (8, 14, o.fftOrder), hop = N / 4, bins = N / 2 + 1;
    const int padded = numSamples + 2 * N;
    const int frames = (padded - N) / hop + 1;
    // the landscape's slice per frame as gain per bin: sampled on a (frames x 512) grid, read with interpolation
    constexpr int kXs = 512;
    std::vector<float> surf ((size_t) frames * kXs);
    t.sample (kXs, frames, 0.0f, 1.0f, 0.0f, 1.0f, surf.data());   // row f = the moment of frame f (z = f / (frames - 1))
    std::vector<float> binX ((size_t) bins);
    for (int k = 0; k < bins; ++k) binX[(size_t) k] = hzToX ((float) k * (float) sampleRate / (float) N, o.range) * (float) (kXs - 1);
    // sqrt-Hann in and out: their product sums to a constant across the 75 % overlap (normalised numerically below)
    std::vector<float> win ((size_t) N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = std::sqrt (0.5f - 0.5f * std::cos (tj::twoPi * (float) i / (float) N));
    std::vector<float> norm ((size_t) padded, 0.0f);
    for (int f = 0; f < frames; ++f)
        for (int i = 0; i < N; ++i) norm[(size_t) (f * hop + i)] += win[(size_t) i] * win[(size_t) i];
    std::vector<std::complex<float>> spec ((size_t) N);
    std::vector<float> gain ((size_t) bins);
    std::vector<float> padIn ((size_t) padded), acc ((size_t) padded);
    for (int c = 0; c < channels; ++c)
    {
        std::fill (padIn.begin(), padIn.end(), 0.0f);
        std::fill (acc.begin(), acc.end(), 0.0f);
        std::memcpy (padIn.data() + N, in[c], sizeof (float) * (size_t) numSamples);
        for (int f = 0; f < frames; ++f)
        {
            // where this frame's centre falls in the sound (0..1) picks the landscape's slice; frames in the padding clamp
            const float centre = ((float) (f * hop) + 0.5f * (float) N - (float) N) / (float) tj::jmax (1, numSamples);
            const float row = tj::jlimit (0.0f, 1.0f, centre) * (float) (frames - 1);
            const int r0 = (int) row, r1 = tj::jmin (frames - 1, r0 + 1);
            const float fr = row - (float) r0;
            const float* s0 = surf.data() + (size_t) r0 * kXs;
            const float* s1 = surf.data() + (size_t) r1 * kXs;
            for (int k = 0; k < bins; ++k)
            {
                const float bx = binX[(size_t) k];
                const int i0 = (int) bx, i1 = tj::jmin (kXs - 1, i0 + 1);
                const float fx = bx - (float) i0;
                const float h0 = s0[i0] + fx * (s0[i1] - s0[i0]), h1 = s1[i0] + fx * (s1[i1] - s1[i0]);
                const float h = h0 + fr * (h1 - h0);
                gain[(size_t) k] = std::pow (10.0f, tj::jlimit (-1.0f, 1.0f, h) * o.range.maxDb * o.strength / 20.0f);
            }
            const float* src = padIn.data() + f * hop;
            for (int i = 0; i < N; ++i) spec[(size_t) i] = { src[i] * win[(size_t) i], 0.0f };
            fft (spec.data(), N, false);
            for (int k = 0; k < bins; ++k)
            {
                spec[(size_t) k] *= gain[(size_t) k];
                if (k > 0 && k < N / 2) spec[(size_t) (N - k)] *= gain[(size_t) k];
            }
            fft (spec.data(), N, true);
            float* dst = acc.data() + f * hop;
            for (int i = 0; i < N; ++i) dst[i] += spec[(size_t) i].real() * win[(size_t) i];
        }
        for (int i = 0; i < numSamples; ++i)
        {
            const float nrm = norm[(size_t) (i + N)];
            out[c][i] = nrm > 1e-6f ? acc[(size_t) (i + N)] / nrm : 0.0f;
        }
    }
    if (o.resonate)
    {
        // the ringing and the grit: the bank follows the same landscape across the sound, in short blocks
        TerrainBank bank;
        bank.prepare (sampleRate, channels);
        bank.setRange (o.range);
        bank.setResonanceOnly (true);
        bank.setAmount (tj::jlimit (0.0f, 1.0f, o.strength * o.resonanceDb * 2.0f));
        bank.setTerrain (t);
        bank.reset();
        const int block = 64;
        float* ptrs[TerrainBank::kMaxChannels];
        for (int pos = 0; pos < numSamples; pos += block)
        {
            const int n = tj::jmin (block, numSamples - pos);
            bank.setDepth ((float) pos / (float) numSamples);
            const int nc = tj::jmin (channels, TerrainBank::kMaxChannels);
            for (int c = 0; c < nc; ++c) ptrs[c] = out[c] + pos;
            bank.process (ptrs, nc, n);
        }
    }
}


// ================================================================================================ the live chain
bool LiveParams::operator== (const LiveParams& o) const
{
    return drive == o.drive && shatter == o.shatter && shatterSize == o.shatterSize && reverse == o.reverse && shimmer == o.shimmer && smear == o.smear
           && freeze == o.freeze && wobble == o.wobble && wobbleHz == o.wobbleHz && tilt == o.tilt && width == o.width && gainDb == o.gainDb;
}

bool LiveParams::isZero() const
{
    constexpr float e = 1e-5f;
    return std::abs (drive) < e && std::abs (shatter) < e && std::abs (shimmer) < e && std::abs (smear) < e && std::abs (wobble) < e
           && std::abs (tilt) < e && std::abs (width) < e && std::abs (gainDb) < e;
}

void TerrainLive::prepare (double sampleRate, int maxChannels)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    chans = tj::jlimit (1, kMaxCh, maxChannels);
    ringLen = (int) (sr * 2.0) + 1;
    for (auto& r : ring) r.assign ((size_t) ringLen, 0.0f);
    const double ms[4] = { 31.3, 37.1, 41.9, 47.7 };
    for (int k = 0; k < 4; ++k) { fdnLen[k] = (int) (sr * ms[k] / 1000.0); fdn[k].assign ((size_t) fdnLen[k] + 1, 0.0f); }
    reset();
}

void TerrainLive::reset() noexcept
{
    for (auto& r : ring) std::fill (r.begin(), r.end(), 0.0f);
    for (auto& d : fdn) std::fill (d.begin(), d.end(), 0.0f);
    ringPos = 0; for (auto& q : fdnP) q = 0; sliceLeft = 0; sliceRepeat = false; sliceGain = 0.0f;
    for (auto& v : fdnLp) v = 0.0f;
    for (auto& v : tiltLp) v = 0.0f;
    sideHp1 = sideHp2 = sideX1 = sideX2 = 0.0f;
    for (auto& v : decor) v = 0.0f;
    env = 0.0f; grainPhase = 0.0; wobPhase = 0.0;
    now = LiveParams(); now.wobbleHz = target.wobbleHz; now.shatterSize = target.shatterSize; now.freeze = target.freeze; now.reverse = target.reverse;
    idle = true;
}

void TerrainLive::process (float* const* ch, int numChannels, int numSamples) noexcept
{
    const int nc = tj::jmin (tj::jmin (numChannels, chans), (int) kMaxCh);
    if (nc <= 0 || ring[0].empty()) return;
    // the ring always listens (a stutter that starts has the last two seconds to repeat)
    auto writeRing = [&] (int n0, int n)
    {
        for (int c = 0; c < nc; ++c)
        {
            int w = (ringPos + n0) % ringLen;
            for (int i = 0; i < n; ++i) { ring[c][(size_t) w] = ch[c][n0 + i]; if (++w == ringLen) w = 0; }
        }
    };
    if (idle && target.isZero())
    {
        writeRing (0, numSamples);
        ringPos = (ringPos + numSamples) % ringLen;
        return;   // (untouched: bit for bit)
    }
    idle = false;
    const float k = 1.0f - std::exp (-1.0f / (float) (0.03 * sr));   // ~30 ms smoothing
    auto glide = [k] (float& v, float t) { v += (t - v) * k; };
    const float fadeLen = (float) (0.004 * sr);
    const float ceiling = 0.84f, release = std::exp (-1.0f / (float) (0.08 * sr));
    for (int i = 0; i < numSamples; ++i)
    {
        glide (now.drive, target.drive); glide (now.shatter, target.shatter); glide (now.shatterSize, target.shatterSize); glide (now.reverse, target.reverse);
        glide (now.shimmer, target.shimmer); glide (now.smear, target.smear); glide (now.freeze, target.freeze); glide (now.wobble, target.wobble);
        glide (now.wobbleHz, target.wobbleHz); glide (now.tilt, target.tilt); glide (now.width, target.width); glide (now.gainDb, target.gainDb);
        float x[kMaxCh] = {};
        for (int c = 0; c < nc; ++c) { x[c] = ch[c][i]; ring[c][(size_t) ringPos] = x[c]; }
        // the stutter: at each slice's end, maybe repeat the slice just heard (sometimes backwards), faded in and out
        if (sliceLeft <= 0)
        {
            sliceLen = tj::jmax (16, (int) (sr * (0.02 + 0.23 * now.shatterSize) * (0.75 + 0.5 * rand01())));
            sliceLeft = sliceLen;
            sliceRepeat = now.shatter > 1e-3f && rand01() < now.shatter;
            sliceBack = sliceRepeat && rand01() < now.reverse;
            sliceStart = ((ringPos - sliceLen) % ringLen + ringLen) % ringLen;
            sliceRead = 0;
        }
        const float pos = (float) (sliceLen - sliceLeft);
        const float edge = tj::jmin (1.0f, tj::jmin (pos, (float) sliceLeft) / tj::jmax (1.0f, fadeLen));
        const float wantGain = sliceRepeat ? edge : 0.0f;
        sliceGain += (wantGain - sliceGain) * 0.2f;
        if (sliceGain > 1e-4f)
        {
            const int off = sliceBack ? sliceLen - 1 - sliceRead : sliceRead;
            const int r = (sliceStart + off) % ringLen;
            for (int c = 0; c < nc; ++c) x[c] += (ring[c][(size_t) r] - x[c]) * sliceGain;
        }
        ++sliceRead; --sliceLeft;
        // the shimmer: two grains reading the ring an octave up
        if (now.shimmer > 1e-4f)
        {
            const double G = 0.06 * sr;
            grainPhase += 1.0 / G;
            if (grainPhase >= 1.0) grainPhase -= 1.0;
            for (int gI = 0; gI < 2; ++gI)
            {
                double p = grainPhase + 0.5 * gI; if (p >= 1.0) p -= 1.0;
                const float w = (float) std::sin (tj::pi * p); const float win = w * w;
                const double rp = (double) ringPos - 2.0 * G + 2.0 * p * G;
                int r0 = (int) std::floor (rp); const float fr = (float) (rp - r0);
                r0 = ((r0 % ringLen) + ringLen) % ringLen; const int r1 = (r0 + 1) % ringLen;
                for (int c = 0; c < nc; ++c) x[c] += 0.45f * now.shimmer * win * (ring[c][(size_t) r0] + (ring[c][(size_t) r1] - ring[c][(size_t) r0]) * fr);
            }
        }
        // drive
        if (now.drive > 1e-4f)
            for (int c = 0; c < nc; ++c) x[c] += now.drive * (0.85f * softLimit (x[c] * (1.0f + 6.0f * now.drive)) - x[c]);
        // tilt (around ~800 Hz)
        if (std::abs (now.tilt) > 1e-4f)
        {
            const float a = 1.0f - std::exp (-2.0f * 3.14159265f * 800.0f / (float) sr);
            for (int c = 0; c < nc; ++c)
            {
                tiltLp[c] += (x[c] - tiltLp[c]) * a;
                const float lo = tiltLp[c], hi = x[c] - lo;
                x[c] = lo * (1.0f - 0.5f * now.tilt) + hi * (1.0f + 0.6f * now.tilt);
            }
        }
        // the wash (smear) and its hold (freeze)
        if (now.smear > 1e-4f || now.freeze > 1e-4f)
        {
            const float in = (nc > 1 ? 0.5f * (x[0] + x[1]) : x[0]) * (1.0f - 0.9f * now.freeze);
            const float g = 0.55f + 0.44f * now.freeze, damp = 0.35f - 0.25f * now.freeze;
            float d[4], sum = 0.0f;
            for (int q = 0; q < 4; ++q) { const int r = (fdnP[q] + 1) % (fdnLen[q] + 1); d[q] = fdn[q][(size_t) r]; sum += d[q]; }
            for (int q = 0; q < 4; ++q)
            {
                float y = (d[q] - 0.5f * sum) * g + in;
                fdnLp[q] += (y - fdnLp[q]) * (1.0f - damp);
                fdn[q][(size_t) fdnP[q]] = fdnLp[q];
                if (++fdnP[q] > fdnLen[q]) fdnP[q] = 0;
            }
            const float wl = 0.6f * (d[0] + d[2]), wr = 0.6f * (d[1] + d[3]);
            const float s = now.smear;
            x[0] = x[0] * (1.0f - 0.35f * s) + wl * s;
            if (nc > 1) x[1] = x[1] * (1.0f - 0.35f * s) + wr * s;
        }
        // wobble: tremolo, and a gentle sway left / right
        if (now.wobble > 1e-4f)
        {
            wobPhase += now.wobbleHz / sr; if (wobPhase >= 1.0) wobPhase -= 1.0;
            const float sn = (float) std::sin (2.0 * tj::pi * wobPhase);
            const float trem = 1.0f - 0.45f * now.wobble * (0.5f + 0.5f * sn);
            if (nc > 1) { x[0] *= trem * (1.0f + 0.2f * now.wobble * sn); x[1] *= trem * (1.0f - 0.2f * now.wobble * sn); }
            else x[0] *= trem;
        }
        // width: more side, from above ~150 Hz only (the lows stay in the middle)
        if (nc > 1 && now.width > 1e-4f)
        {
            const float mid = 0.5f * (x[0] + x[1]), side = 0.5f * (x[0] - x[1]);
            const float a = std::exp (-2.0f * 3.14159265f * 150.0f / (float) sr);
            // two first-order highpasses
            const float h1 = a * (sideHp1 + mid - sideX1); sideX1 = mid; sideHp1 = h1;
            const float h2 = a * (sideHp2 + side - sideX2); sideX2 = side; sideHp2 = h2;
            // a decorrelating allpass on the mid's highs
            const float ap = -0.6f * h1 + decor[0] + 0.6f * decor[1]; decor[0] = h1; decor[1] = ap;
            const float s2 = side + now.width * (0.5f * h2 + 0.55f * ap);
            x[0] = mid + s2; x[1] = mid - s2;
        }
        // trim and the ceiling (-1.5 dBFS peaks)
        const float gain = std::abs (now.gainDb) > 1e-4f ? std::pow (10.0f, now.gainDb / 20.0f) : 1.0f;
        float peak = 0.0f;
        for (int c = 0; c < nc; ++c) { x[c] *= gain; peak = tj::jmax (peak, std::abs (x[c])); }
        env = peak > env ? peak : env * release + peak * (1.0f - release);
        const float gr = env > ceiling ? ceiling / env : 1.0f;
        for (int c = 0; c < nc; ++c) ch[c][i] = x[c] * gr;
        if (++ringPos == ringLen) ringPos = 0;
    }
    // back to untouched once everything has glided to nothing (and the wash has rung out)
    if (target.isZero() && now.drive < 1e-4f && now.shatter < 1e-4f && now.shimmer < 1e-4f && now.smear < 1e-4f && now.wobble < 1e-4f
        && std::abs (now.tilt) < 1e-4f && now.width < 1e-4f && std::abs (now.gainDb) < 1e-4f && sliceGain < 1e-4f && env <= ceiling)
    {
        const auto keep = target;
        now = LiveParams(); now.wobbleHz = keep.wobbleHz; now.shatterSize = keep.shatterSize; now.freeze = keep.freeze; now.reverse = keep.reverse;
        for (auto& dl : fdn) std::fill (dl.begin(), dl.end(), 0.0f);
        idle = true;
    }
}

void LiveShare::publish (const LiveParams& p) noexcept
{
    slots[(size_t) back] = p;
    const int old = middle.exchange (back | 4, std::memory_order_acq_rel);
    back = old & 3;
}

bool LiveShare::pull (LiveParams& out) noexcept
{
    if ((middle.load (std::memory_order_acquire) & 4) == 0) return false;
    const int old = middle.exchange (front, std::memory_order_acq_rel);
    front = old & 3;
    out = slots[(size_t) front];
    return true;
}

// ================================================================================================ analyzer
void SpectrumTap::push (const float* const* channels, int numChannels, int numSamples) noexcept
{
    if (numChannels <= 0) return;
    int w = writePos.load (std::memory_order_relaxed);
    const float g = 1.0f / (float) numChannels;
    for (int n = 0; n < numSamples; ++n)
    {
        float s = 0.0f;
        for (int c = 0; c < numChannels; ++c) s += channels[c][n];
        ring[(size_t) w] = s * g;
        w = (w + 1) & (int) (ring.size() - 1);
    }
    writePos.store (w, std::memory_order_release);
}

bool SpectrumTap::read (std::vector<float>& dbAcrossX, int points, const Range& r, double sampleRate, float fallDbPerCall)
{
    const int w = writePos.load (std::memory_order_acquire);
    work.resize ((size_t) kSize);
    std::vector<std::complex<float>> spec ((size_t) kSize);
    bool any = false;
    for (int i = 0; i < kSize; ++i)
    {
        const float s = ring[(size_t) ((w - kSize + i + (int) ring.size()) & (int) (ring.size() - 1))];
        any = any || std::abs (s) > 1e-9f;
        const float win = 0.5f - 0.5f * std::cos (tj::twoPi * (float) i / (float) kSize);
        spec[(size_t) i] = { s * win, 0.0f };
    }
    if (! any && held.empty()) return false;
    fft (spec.data(), kSize, false);
    mags.resize ((size_t) kSize / 2 + 1);
    for (int k = 0; k <= kSize / 2; ++k) mags[(size_t) k] = std::abs (spec[(size_t) k]) * (4.0f / (float) kSize);   // Hann: x2, one-sided: x2
    if ((int) held.size() != points) held.assign ((size_t) points, -120.0f);
    dbAcrossX.resize ((size_t) points);
    for (int p = 0; p < points; ++p)
    {
        const float x0 = ((float) p - 0.5f) / (float) (points - 1), x1 = ((float) p + 0.5f) / (float) (points - 1);
        const float b0 = xToHz (tj::jmax (0.0f, x0), r) * (float) kSize / (float) sampleRate, b1 = xToHz (tj::jmin (1.0f, x1), r) * (float) kSize / (float) sampleRate;
        int k0 = tj::jlimit (0, kSize / 2, (int) std::floor (b0)), k1 = tj::jlimit (0, kSize / 2, (int) std::ceil (b1));
        float m = 0.0f;
        if (k1 <= k0)
        {
            // between bins: interpolate the magnitude
            const float bf = tj::jlimit (0.0f, (float) (kSize / 2 - 1), 0.5f * (b0 + b1));
            const int kk = (int) bf; const float fr = bf - (float) kk;
            m = mags[(size_t) kk] + fr * (mags[(size_t) kk + 1] - mags[(size_t) kk]);
        }
        else for (int k = k0; k <= k1; ++k) m = tj::jmax (m, mags[(size_t) k]);
        const float db = 20.0f * std::log10 (tj::jmax (1e-7f, m));
        held[(size_t) p] = tj::jmax (db, held[(size_t) p] - fallDbPerCall);
        dbAcrossX[(size_t) p] = held[(size_t) p];
    }
    return true;
}
}
