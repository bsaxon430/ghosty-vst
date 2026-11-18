// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
#include "Brightness.h"
#include <juce_dsp/juce_dsp.h>
#include <memory>
#include <vector>

namespace mosaic
{
float spectralCentroidHz (const float* x, int n, double rate)
{
    int order = 6;
    const int m = std::min (n, 4096);
    while ((1 << order) < m && order < 12) ++order;
    const int N = 1 << order;
    thread_local std::unique_ptr<juce::dsp::FFT> fft;
    thread_local std::vector<float> buf;
    if (fft == nullptr || fft->getSize() != N) fft = std::make_unique<juce::dsp::FFT> (order);
    buf.assign ((size_t) N * 2, 0.0f);
    for (int k = 0; k < std::min (m, N); ++k) buf[(size_t) k] = x[k];
    fft->performFrequencyOnlyForwardTransform (buf.data(), true);
    double num = 0, den = 0;
    for (int k = 1; k < N / 2; ++k) { const double p = (double) buf[(size_t) k] * buf[(size_t) k]; num += p * k * rate / N; den += p; }
    return den > 0 ? (float) (num / den) : 0.0f;
}
}
