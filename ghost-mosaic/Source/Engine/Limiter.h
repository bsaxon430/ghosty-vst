// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
//
// Safety limiter (ours, not part of Samplebrain): keeps the output's true peak (the peaks
// between samples, estimated 8x oversampled with a 32-tap windowed sinc) under the ceiling. Look-ahead min-hold + box
// smoothing, so the gain is already down when a peak arrives (no overshoot, no clicks).
// Below the ceiling it does nothing at all (gain exactly 1, output = input delayed).
#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

namespace mosaic
{
class TruePeakLimiter
{
public:
    static constexpr int K = 16;       // interpolation reach (samples ahead)
    static constexpr int P = 8, T = 32; // oversampling, taps per phase
    static constexpr int M = 56;       // min-hold / smoothing length
    static constexpr int kLatency = M - 1 + K;

    void prepare (double sampleRate, int channels, float ceilingDb = -1.3f)
    {
        m_ch = channels;
        m_ceiling = std::pow (10.0f, ceilingDb / 20.0f);
        m_release = (float) (1.0 - std::exp (-1.0 / (0.12 * sampleRate)));
        const int len = 256;
        m_mask = len - 1;
        m_x.assign ((size_t) (channels * len), 0.0f);
        m_g.assign ((size_t) len, 1.0f);
        m_h.assign ((size_t) len, 1.0f);
        m_pos = 0; m_h2 = 1; m_sum = (double) M; m_sumCount = 0;
        // 8x polyphase windowed sinc, 32 taps per phase (a 4x / 16-tap estimate missed up to
        // 0.7 dB on bright drums: the test's independent 8x / 64-tap meter caught it)
        for (int ph = 1; ph < P; ++ph)
            for (int j = 0; j < T; ++j)
            {
                const double f = ph / (double) P, t = f - (j - (T / 2 - 1));
                const double sinc = std::abs (t) < 1e-9 ? 1.0 : std::sin (M_PI * t) / (M_PI * t);
                const double w = 0.5 + 0.5 * std::cos (M_PI * t / (T / 2 + 0.5));
                m_coef[ph - 1][j] = (float) (sinc * w);
            }
        m_minGain = 1;
    }
    void reset() { std::fill (m_x.begin(), m_x.end(), 0.0f); std::fill (m_g.begin(), m_g.end(), 1.0f); std::fill (m_h.begin(), m_h.end(), 1.0f); m_h2 = 1; m_sum = (double) M; }

    // in place; ch[c] for c < channels
    void process (float* const* ch, int n)
    {
        const int len = m_mask + 1;
        for (int i = 0; i < n; ++i)
        {
            // write the new sample
            for (int c = 0; c < m_ch; ++c) m_x[(size_t) (c * len + (m_pos & m_mask))] = ch[c][i];
            // true peak of the sample K behind (now that its future neighbours are known)
            const int p = m_pos - K;
            float tp = 0;
            for (int c = 0; c < m_ch; ++c)
            {
                const float* x = m_x.data() + c * len;
                tp = std::max (tp, std::abs (x[p & m_mask]));
                for (int ph = 0; ph < P - 1; ++ph)
                {
                    float acc = 0;
                    for (int j = 0; j < T; ++j) acc += m_coef[ph][j] * x[(p + j - (T / 2 - 1)) & m_mask];
                    tp = std::max (tp, std::abs (acc));
                }
            }
            const float greq = tp > m_ceiling ? m_ceiling / tp : 1.0f;
            m_g[(size_t) (p & m_mask)] = greq;
            // min over the last M required gains
            float h = 1.0f;
            for (int k = 0; k < M; ++k) h = std::min (h, m_g[(size_t) ((p - k) & m_mask)]);
            if (h < m_h2) m_h2 = h; else m_h2 += (h - m_h2) * m_release;
            // box average of the last M held gains
            m_sum += m_h2 - m_h[(size_t) ((p - M) & m_mask)];
            m_h[(size_t) (p & m_mask)] = m_h2;
            if (++m_sumCount >= 4096) { m_sumCount = 0; double s = 0; for (int k = 0; k < M; ++k) s += m_h[(size_t) ((p - k) & m_mask)]; m_sum = s; }
            float gain = (float) (m_sum / M);
            if (gain > 0.999999f) gain = 1.0f;
            m_minGain = std::min (m_minGain, gain);
            // output: the sample (M-1) behind p, i.e. kLatency behind the input
            const int o = p - (M - 1);
            for (int c = 0; c < m_ch; ++c)
            {
                const float v = m_x[(size_t) (c * len + (o & m_mask))];
                ch[c][i] = gain == 1.0f ? v : v * gain;
            }
            ++m_pos;
        }
    }
    float takeMinGain() { float g = m_minGain; m_minGain = 1; return g; }

private:
    int m_ch = 2, m_mask = 255, m_pos = 0, m_sumCount = 0;
    float m_ceiling = 0.86f, m_release = 0.001f, m_h2 = 1, m_minGain = 1;
    double m_sum = 0;
    std::vector<float> m_x, m_g, m_h;
    float m_coef[P - 1][T] {};
};
}
