#include "core/ParametricEqLinear.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>

// bit-exactness needs every product and sum rounded on its own (no fused multiply-add)
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

// Parametric EQ 2 linear-phase mode (docs/analise/REA_rodada50.md §5). The float/double roundings of the magnitude
// table follow the plugin; the FFT convolution is computed here in double precision (the plugin's single-precision
// FFT rounding is not reproduced, §5.6).

namespace lynx::peqfl
{
namespace
{
constexpr double twoPi = 6.283185307179586;
constexpr double ln2 = 0.69314718055994529;
constexpr double freqStep = 0.00015206580024203624;
constexpr float smoothingSeconds[4] = { 0.1f, 0.25f, 0.01f, 0.02f };

// 2^y for |y| ≤ 100/6 with the plugin's cubic approximation of 2^f on [0, 1): integer part through the exponent
// bits, negative arguments as the reciprocal of the positive one (§5.2)
float pow2Approx (float y)
{
    auto power = [] (float v) {
        const int i = (int) (double) v;
        const double f = (double) (v - (float) i);
        std::uint32_t bits = (std::uint32_t) (i + 127) << 23;
        float scale;
        std::memcpy (&scale, &bits, 4);
        return (float) ((double) scale * (f * (f * (f * 0.079064658211 + 0.224328091473) + 0.69659850257) + 0.999862219597));
    };
    if (y > 0.0f)
        return power (y);
    if ((double) y < 0.0)
        return 1.0f / power (-y);
    return 1.0f;
}

// Twiddle factors of the radix-2 transform below for size n, stage by stage (len = 2, 4, … n: the len/2 factors
// e^(±2πik/len) start at len/2 − 1). Built once per size and shared; the values are the ones the transform computed
// per butterfly before (std::polar of the same angle), so the output is unchanged — only the audio thread no longer
// evaluates a sine and a cosine for every butterfly (REA round 87: this was most of a heavy project's CPU)
std::shared_ptr<const Twiddles> twiddlesFor (int n)
{
    static std::mutex lock;
    static std::map<int, std::shared_ptr<const Twiddles>> cache;
    const std::lock_guard<std::mutex> guard (lock);
    auto& entry = cache[n];
    if (entry == nullptr)
    {
        auto t = std::make_shared<Twiddles>();
        for (auto* table : { &t->forward, &t->inverse })
        {
            const bool inverse = table == &t->inverse;
            table->reserve ((std::size_t) std::max (0, n - 1));
            for (std::size_t len = 2; len <= (std::size_t) n; len <<= 1)
            {
                const double ang = (inverse ? twoPi : -twoPi) / (double) len;
                for (std::size_t k = 0; k < len / 2; ++k)
                    table->push_back (std::polar (1.0, ang * (double) k));
            }
        }
        entry = std::move (t);
    }
    return entry;
}

// (a + bi)(c + di) as std::complex computes it for finite values, without its NaN/infinity recovery branch
inline std::complex<double> mul (const std::complex<double>& z, const std::complex<double>& w) noexcept
{
    const double a = z.real(), b = z.imag(), c = w.real(), d = w.imag();
    const double ac = a * c;
    const double bd = b * d;
    const double ad = a * d;
    const double bc = b * c;
    const double x = ac - bd;
    const double y = ad + bc;
    if (std::isnan (x) && std::isnan (y))
        return z * w;
    return { x, y };
}

void fft (std::complex<double>* a, std::size_t n, const std::vector<std::complex<double>>& tw) noexcept
{
    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        auto bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1)
    {
        const auto* w = tw.data() + (len / 2 - 1);
        const auto half = len / 2;
        for (std::size_t i = 0; i < n; i += len)
            for (std::size_t k = 0; k < half; ++k)
            {
                const auto u = a[i + k], v = mul (a[i + k + half], w[k]);
                a[i + k] = u + v;
                a[i + k + half] = u - v;
            }
    }
}

// The plugin's main-level stage (§5.5, shared with its other modes): a change glides linearly over the block, at
// most maxStep per frame; the per-frame gains are built by repeated float additions, even and odd frames on
// separate running sums in groups of eight frames, the remainder one frame at a time
void applyMainGain (float* io, int n, float target, float& now, float maxStep, bool snap) noexcept
{
    if (snap)
        now = target;
    const float g0 = now;
    float step = target - g0;
    std::uint32_t stepBits;
    std::memcpy (&stepBits, &step, 4);
    if (stepBits != 0)
    {
        now = target;
        step = step / (float) n;
        if (std::abs (step) > maxStep)
        {
            step = std::copysign (maxStep, step);
            now = g0 + step * (float) n;
            std::uint32_t b;
            std::memcpy (&b, &now, 4);
            if ((b & 0x7FFFFFFFu) < 0x33800000u)
            {
                now = 0.0f;
                step = -g0 / (float) n;
            }
        }
    }
    else
    {
        if (g0 == 0.0f)
            std::fill (io, io + 2 * n, 0.0f);
        else if (g0 != 1.0f)
            for (int i = 0; i < 2 * n; ++i)
                io[i] *= g0;
        return;
    }
    float even = g0 + 0.0f, odd = g0 + step;
    const float step2 = step + step;
    int i = 0;
    for (int g = 0; g < (n >> 3); ++g)
        for (int m = 0; m < 4; ++m, i += 2)
        {
            io[2 * i] *= even, io[2 * i + 1] *= even;
            io[2 * i + 2] *= odd, io[2 * i + 3] *= odd;
            even += step2, odd += step2;
        }
    for (; i < n; ++i)
    {
        io[2 * i] *= even, io[2 * i + 1] *= even;
        even += step;
    }
}

std::int32_t i32At (const std::vector<std::uint8_t>& d, std::size_t offset)
{
    std::int32_t v = 0;
    if (offset + 4 <= d.size())
        std::memcpy (&v, d.data() + offset, 4);
    return v;
}
} // namespace

void mainLevel (float* io, int n, float target, float& now, float maxStep, bool snap) noexcept
{
    applyMainGain (io, n, target, now, maxStep, snap);
}

int linearFftSize (int quality, int hostRate) noexcept
{
    const int base = 512 << std::clamp (quality, 0, 7);
    if (hostRate < 44100)
        return base / 2;
    const int e = (int) (std::log ((double) hostRate * 2.2675736961451248e-05) * 1.4426950408889634 + 0.5);
    return std::min (base << std::max (e, 0), 65536);
}

LinearOptions parseLinearOptions (const std::vector<std::uint8_t>& d)
{
    LinearOptions o;
    if (i32At (d, 0) >= 4 && d.size() >= 0x13D)
    {
        o.quality = std::clamp ((int) i32At (d, 0x135), 0, 7);
        o.smoothing = std::clamp ((int) i32At (d, 0x139), 0, 3);
    }
    return o;
}

std::vector<float> linearMagnitudes (const Settings& s, int hostRate, int n)
{
    // the bands are designed at the plugin's processing rate (×2 with HQ oversampling on), but the grid spans the host
    // rate's 0…rate/2 (§5.1)
    const bool twice = s.processing == Processing::hq || s.processing == Processing::hqExternal;
    const double procRate = twice ? 2.0 * hostRate : (double) hostRate;
    const auto limit = (float) ((double) (int) procRate * 0.4999);
    const double wk = (1.0 / procRate) * twoPi;

    std::array<Band, 7> bands {};
    std::array<float, 7> freq {};
    std::array<bool, 7> snap {}, used {};
    for (int b = 0; b < 7; ++b)
    {
        designBand (s, b, procRate, bands[(std::size_t) b]);
        freq[(std::size_t) b] = (float) (std::exp ((double) (float) s.freq[(std::size_t) b] * freqStep * ln2) * 20.0);
        const int ui = s.type[(std::size_t) b];
        // narrow band-pass / notch / peaking bands (width < 1/8) pull the nearest grid point onto their centre
        snap[(std::size_t) b] = s.width[(std::size_t) b] < 0x2000 && ui >= 0 && ui < 8 && ((0x54 >> ui) & 1) != 0;
        used[(std::size_t) b] = (s.mask & (1u << b)) != 0 && bands[(std::size_t) b].type > 0;
    }

    std::vector<float> t ((std::size_t) (n / 2 + 1));
    const float spacing = (float) hostRate / (float) n;
    float previous = 0.0f;
    for (int k = 0; k <= n / 2; ++k)
    {
        float f = (float) k * spacing;
        if (0.1f > f)
            f = 0.1f;
        float level = 0.0f; // response in units of 18 dB
        if (! (f > limit))
        {
            for (int b = 0; b < 7; ++b)
                if (snap[(std::size_t) b] && freq[(std::size_t) b] > previous && f >= freq[(std::size_t) b])
                {
                    f = freq[(std::size_t) b];
                    break;
                }
            previous = f;
            const auto w = (float) ((double) f * wk);
            const double s1 = std::sin ((double) w), c1 = std::cos ((double) w);
            const auto w2 = w * 2.0f;
            const double s2 = std::sin ((double) w2), c2 = std::cos ((double) w2);
            float product = 1.0f;
            for (int b = 0; b < 7; ++b)
            {
                if (! used[(std::size_t) b])
                    continue;
                float g = 1.0f;
                const auto& band = bands[(std::size_t) b];
                for (int st = 0; st < band.stages; ++st)
                {
                    const auto& c = band.stage[(std::size_t) st];
                    const double nr = c.b0 * c2 + c.b1 * c1 + c.b2, ni = c.b1 * s1 + c.b0 * s2;
                    const double dr = c2 + c.a1 * c1 + c.a2, di = c.a1 * s1 + s2;
                    g = (float) (((double) g * (nr * nr + ni * ni)) / (dr * dr + di * di));
                }
                product *= g;
            }
            level = (float) (std::log10 ((double) product) * 0.5555555555555556);
            level = std::clamp (level, -1000.0f, 1000.0f);
        }
        const float db = level * 18.0f;
        float amp;
        if (db < -100.0f)
            amp = 0.0f;
        else if (db > 100.0f)
            amp = (float) std::pow (2.0, (double) (db * 0.166666672f)); // not reached by the plugin's ranges
        else
            amp = pow2Approx (db * 0.166666672f);
        t[(std::size_t) k] = amp;
    }
    t[(std::size_t) (n / 2)] = t[(std::size_t) (n / 2 - 1)];
    return t;
}

// ------------------------------------------------------------------------------------------------- convolver

void FftConvolver::init (int size)
{
    n = size;
    hop = size / 2;
    pos = 0;
    kernel.assign ((std::size_t) n, {});
    work.assign ((std::size_t) n, {});
    work2.assign ((std::size_t) n, {});
    twiddles = twiddlesFor (n);
    for (int c = 0; c < 2; ++c)
    {
        in[c].assign ((std::size_t) hop, 0.0f);
        out[c].assign ((std::size_t) n, 0.0f);
    }
}

void FftConvolver::clear() noexcept
{
    pos = 0;
    for (int c = 0; c < 2; ++c)
    {
        std::fill (in[c].begin(), in[c].end(), 0.0f);
        std::fill (out[c].begin(), out[c].end(), 0.0f);
    }
}

void FftConvolver::setKernel (const std::vector<float>& t)
{
    // zero-phase impulse centred at n/2: (1/n)·IFFT of the table times (−1)^k, mirrored; its middle half is kept,
    // weighted by a sine window, and becomes an n/2-tap FIR with its centre at tap n/4 (§5.3)
    // (in the preallocated buffers: a new response is loaded on the audio thread)
    auto& spec = work;
    std::fill (spec.begin(), spec.end(), std::complex<double> {});
    for (int k = 0; k <= n / 2; ++k)
        spec[(std::size_t) k] = (k & 1) ? -(double) t[(std::size_t) k] : (double) t[(std::size_t) k];
    for (int k = 1; k < n / 2; ++k)
        spec[(std::size_t) (n - k)] = spec[(std::size_t) k];
    fft (spec.data(), (std::size_t) n, twiddles->inverse);
    const float span = 1.0f / (float) (hop - 1);
    auto& taps = work2;
    std::fill (taps.begin(), taps.end(), std::complex<double> {});
    for (int i = 0; i < hop; ++i)
    {
        const auto h = (float) (spec[(std::size_t) (n / 4 + i)].real() / (double) n);
        const auto w = (float) std::sin ((double) ((float) i * span * 3.14159274f));
        taps[(std::size_t) i] = (double) (w * h);
    }
    fft (taps.data(), (std::size_t) n, twiddles->forward);
    std::copy (taps.begin(), taps.end(), kernel.begin());
}

void FftConvolver::block() noexcept
{
    const auto scale = 1.0f / (float) n;
    auto& x = work;
    for (int c = 0; c < 2; ++c)
    {
        std::fill (x.begin(), x.end(), std::complex<double> {});
        for (int i = 0; i < hop; ++i)
            x[(std::size_t) i] = (double) in[c][(std::size_t) i];
        fft (x.data(), (std::size_t) n, twiddles->forward);
        for (int k = 0; k < n; ++k)
            x[(std::size_t) k] = mul (x[(std::size_t) k], kernel[(std::size_t) k]);
        fft (x.data(), (std::size_t) n, twiddles->inverse);
        auto& o = out[c];
        std::copy (o.begin() + hop, o.end(), o.begin());
        std::fill (o.begin() + hop, o.end(), 0.0f);
        for (int i = 0; i < n; ++i)
            o[(std::size_t) i] = scale * (float) x[(std::size_t) i].real() + o[(std::size_t) i];
    }
}

void FftConvolver::step (float& l, float& r) noexcept
{
    in[0][(std::size_t) pos] = l;
    in[1][(std::size_t) pos] = r;
    l = out[0][(std::size_t) pos];
    r = out[1][(std::size_t) pos];
    if (++pos == hop)
    {
        pos = 0;
        block();
    }
}

// ------------------------------------------------------------------------------------------------- engine

void LinearPhase::setOptions (LinearOptions o) noexcept
{
    quality = std::clamp (o.quality, 0, 7);
    smoothingIndex = std::clamp (o.smoothing, 0, 3);
}

void LinearPhase::prepare (int hostRate) noexcept
{
    rate = hostRate;
    size = linearFftSize (quality, rate);
    for (auto& c : conv)
        c.init (size);
    for (auto& h : history)
        h.assign ((std::size_t) size, 0.0f);
    reset();
}

void LinearPhase::reset() noexcept
{
    for (auto& c : conv)
        c.clear();
    for (auto& h : history)
        std::fill (h.begin(), h.end(), 0.0f);
    historyPos = 0;
    fadeCount = fadeLength = 0;
    gainSnap = true;
    active = 0;
    if (configured)
    {
        // the response is (re)loaded with the first sample, faded in from silence
        for (auto& c : conv)
            c.setKernel (std::vector<float> ((std::size_t) (size / 2 + 1), 0.0f));
        pending = true;
    }
}

void LinearPhase::set (const Settings& s)
{
    settings = s;
    mainGain = (float) std::exp ((double) ((float) s.main * 0.01f * 0.115129255f));
    // the glide limit follows the plugin's processing rate (×2 when HQ oversampling is on)
    const bool twice = s.processing == Processing::hq || s.processing == Processing::hqExternal;
    gainStepMax = (float) (44100.0 / (double) (twice ? 2 * rate : rate) * 0.001);
    if (size != linearFftSize (quality, rate) || conv[0].n != size)
        prepare (rate);
    table = linearMagnitudes (s, rate, size);
    pending = true;
    configured = true;
}

void LinearPhase::load() noexcept
{
    // the convolver not in use takes the new response, is primed with the last n input samples and becomes the
    // current one; the output then cross-fades from the old one over the smoothing time (§5.4)
    pending = false;
    fadeCount = 0;
    fadeLength = (int) ((float) rate * smoothingSeconds[smoothingIndex]);
    auto& c = conv[(std::size_t) active];
    c.clear();
    c.setKernel (table);
    for (int k = 0, p = historyPos; k < size; ++k)
    {
        float l = history[0][(std::size_t) p], r = history[1][(std::size_t) p];
        c.step (l, r);
        if (++p == size)
            p = 0;
    }
    active = active == 0 ? 1 : 0;
}

void LinearPhase::process (float* io, int n) noexcept
{
    if (! configured)
        return;
    applyMainGain (io, n, mainGain, gainNow, gainStepMax, gainSnap);
    gainSnap = false;

    for (int i = 0; i < n; ++i)
    {
        if (pending && fadeLength <= fadeCount)
            load();
        float& l = io[2 * i];
        float& r = io[2 * i + 1];
        history[0][(std::size_t) historyPos] = l;
        history[1][(std::size_t) historyPos] = r;
        if (++historyPos == size)
            historyPos = 0;
        if (fadeLength <= fadeCount)
        {
            conv[active == 0 ? 1u : 0u].step (l, r);
            continue;
        }
        float al = l, ar = r, bl = l, br = r;
        conv[0].step (al, ar);
        conv[1].step (bl, br);
        float f = (float) fadeCount / (float) fadeLength;
        if (active == 0)
            f = 1.0f - f;
        const float g = 1.0f - f;
        l = bl * g + al * f;
        r = br * g + ar * f;
        ++fadeCount;
    }
}
} // namespace lynx::peqfl
