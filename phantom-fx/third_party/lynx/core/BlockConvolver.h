#pragma once

// A small uniformly partitioned overlap-save convolver (the textbook UPOLS scheme: the kernel cut into blocks of B
// samples, each block's spectrum multiplied with a frequency-domain delay line of the input's spectra, one complex
// FFT of 2B per block). Used for short measured kernels inside effects (round 148: Echoform's ping-pong tape voicing);
// the output is the convolution delayed by exactly B samples. Nothing allocates after setKernel().

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace lynx
{
class BlockConvolver
{
public:
    // `kernel` = the impulse response; `block` = B (a power of two)
    void setKernel (const std::vector<float>& kernel, int block = 256)
    {
        B = std::max (16, block);
        N = 2 * B;
        parts = std::max (1, (int) ((kernel.size() + (std::size_t) B - 1) / (std::size_t) B));
        bins = N; // full complex spectra (simple and safe; the kernels here are short)
        twiddle.resize ((std::size_t) N / 2);
        for (int k = 0; k < N / 2; ++k)
            twiddle[(std::size_t) k] = std::polar (1.0f, (float) (-2.0 * pi * k / N));
        bitrev.resize ((std::size_t) N);
        int bits = 0;
        while ((1 << bits) < N)
            ++bits;
        for (int i = 0; i < N; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                r |= ((i >> b) & 1) << (bits - 1 - b);
            bitrev[(std::size_t) i] = r;
        }
        H.assign ((std::size_t) (parts * bins), {});
        std::vector<std::complex<float>> z ((std::size_t) N);
        for (int p = 0; p < parts; ++p)
        {
            std::fill (z.begin(), z.end(), std::complex<float> {});
            for (int i = 0; i < B; ++i)
            {
                const auto t = (std::size_t) (p * B + i);
                if (t < kernel.size())
                    z[(std::size_t) i] = kernel[t];
            }
            fft (z.data(), false);
            std::copy (z.begin(), z.end(), H.begin() + (std::ptrdiff_t) (p * bins));
        }
        X.assign ((std::size_t) (parts * bins), {});
        work.assign ((std::size_t) N, {});
        acc.assign ((std::size_t) N, {});
        input.assign ((std::size_t) N, 0.0f);
        output.assign ((std::size_t) B, 0.0f);
        reset();
    }

    void reset() noexcept
    {
        std::fill (X.begin(), X.end(), std::complex<float> {});
        std::fill (input.begin(), input.end(), 0.0f);
        std::fill (output.begin(), output.end(), 0.0f);
        head = 0;
        fill = 0;
    }

    bool ready() const noexcept { return ! H.empty(); }
    int latency() const noexcept { return B; }

    // one input sample in, the output B samples late
    float push (float x) noexcept
    {
        const auto y = output[(std::size_t) fill];
        input[(std::size_t) (B + fill)] = x;
        if (++fill == B)
        {
            block();
            fill = 0;
        }
        return y;
    }

private:
    static constexpr double pi = 3.141592653589793;

    void block() noexcept
    {
        // the last 2B inputs -> the newest spectrum in the delay line
        head = (head + parts - 1) % parts;
        for (int i = 0; i < N; ++i)
            work[(std::size_t) i] = input[(std::size_t) i];
        fft (work.data(), false);
        std::copy (work.begin(), work.end(), X.begin() + (std::ptrdiff_t) (head * bins));
        std::fill (acc.begin(), acc.end(), std::complex<float> {});
        for (int p = 0; p < parts; ++p)
        {
            const auto* xs = X.data() + (std::size_t) (((head + p) % parts) * bins);
            const auto* hs = H.data() + (std::size_t) (p * bins);
            for (int k = 0; k < bins; ++k)
                acc[(std::size_t) k] += xs[k] * hs[k];
        }
        fft (acc.data(), true);
        const float scale = 1.0f / (float) N;
        for (int i = 0; i < B; ++i)
            output[(std::size_t) i] = acc[(std::size_t) (B + i)].real() * scale;
        std::copy (input.begin() + B, input.end(), input.begin()); // this block becomes the previous one
    }

    void fft (std::complex<float>* a, bool inverse) const noexcept
    {
        for (int i = 0; i < N; ++i)
            if (i < bitrev[(std::size_t) i])
                std::swap (a[i], a[bitrev[(std::size_t) i]]);
        for (int len = 2; len <= N; len <<= 1)
        {
            const int half = len / 2, step = N / len;
            for (int i = 0; i < N; i += len)
                for (int j = 0; j < half; ++j)
                {
                    auto w = twiddle[(std::size_t) (j * step)];
                    if (inverse)
                        w = std::conj (w);
                    const auto u = a[i + j], v = a[i + j + half] * w;
                    a[i + j] = u + v;
                    a[i + j + half] = u - v;
                }
        }
    }

    int B = 256, N = 512, parts = 1, bins = 512, head = 0, fill = 0;
    std::vector<std::complex<float>> twiddle, H, X, work, acc;
    std::vector<int> bitrev;
    std::vector<float> input, output;
};
} // namespace lynx
