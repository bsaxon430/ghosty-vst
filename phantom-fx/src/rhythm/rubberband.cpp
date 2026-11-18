// SPDX-License-Identifier: AGPL-3.0-or-later
// WARP challenger engine: Rubber Band Library (GPL-2.0-or-later), compiled unchanged from its single-file build (single/RubberBandSingle.cpp,
// built-in FFT and resampler) in this file. Real-time mode, R3 ("finer") engine, pitch shift at time ratio 1. Fixed latency, as the
// library's own real-time guidance says: at a reset it is fed getPreferredStartPad() samples of silence and the first getStartDelay()
// output samples are dropped; the output then sits in a FIFO primed with a fixed safety margin, so every block hands back exactly n
// samples, `latency()` samples late.
#include "../master/Engines.h"
#include "single/RubberBandSingle.cpp"
#include <vector>
#include <algorithm>

namespace phantom
{
static constexpr int kHopMargin = 1024;
struct RubberBandBox::Impl
{
    std::unique_ptr<RubberBand::RubberBandStretcher> rb;
    std::vector<float> fifo[2], tmp[2], zeros; size_t rd = 0, wr = 0, cap = 0; int lat = 0, margin = 0, maxBlock = 0, toDrop = 0;
    double ratio = 1.0; bool formants = false;
    size_t fill() const { return wr - rd; }
    void push (float* const* src, size_t n)
    {
        for (size_t i = 0; i < n; ++i) { for (int c = 0; c < 2; ++c) fifo[c][(wr + i) % cap] = src[c][i]; }
        wr += n;
    }
    void pull()
    {
        int avail;
        while ((avail = rb->available()) > 0)
        {
            const size_t m = std::min ((size_t) avail, tmp[0].size());
            float* t[2] { tmp[0].data(), tmp[1].data() };
            rb->retrieve (t, m);
            size_t skip = std::min ((size_t) toDrop, m); toDrop -= (int) skip;
            float* s[2] { t[0] + skip, t[1] + skip };
            if (m > skip && fill() + (m - skip) <= cap) push (s, m - skip);
        }
    }
};

RubberBandBox::RubberBandBox() : d (std::make_unique<Impl>()) {}
RubberBandBox::~RubberBandBox() = default;
void RubberBandBox::prepare (double sampleRate, int maxBlock)
{
    using S = RubberBand::RubberBandStretcher;
    d->rb = std::make_unique<S> ((size_t) sampleRate, 2, S::OptionProcessRealTime | S::OptionEngineFiner | S::OptionPitchHighConsistency | S::OptionChannelsTogether);
    d->rb->setMaxProcessSize ((size_t) std::max (maxBlock, 1024));
    d->maxBlock = maxBlock;
    // the output is available once the library has seen getStartDelay() more input, handed back in hops: the margin covers both
    d->margin = (int) d->rb->getStartDelay() + kHopMargin;
    d->cap = (size_t) (d->margin * 4 + maxBlock * 4 + 65536);
    for (int c = 0; c < 2; ++c) { d->fifo[c].assign (d->cap, 0.0f); d->tmp[c].assign (8192, 0.0f); }
    d->zeros.assign ((size_t) std::max (8192, d->margin), 0.0f);
    reset();
}
void RubberBandBox::reset()
{
    auto& r = *d->rb;
    r.reset(); r.setPitchScale (d->ratio); r.setFormantOption (d->formants ? RubberBand::RubberBandStretcher::OptionFormantPreserved : RubberBand::RubberBandStretcher::OptionFormantShifted);
    d->rd = d->wr = 0;
    // prime the FIFO with the safety margin (silence), then the library's own start pad
    float* zz[2] { d->zeros.data(), d->zeros.data() }; d->push (zz, (size_t) d->margin);
    size_t pad = r.getPreferredStartPad();
    while (pad > 0) { const size_t m = std::min (pad, d->zeros.size()); const float* in[2] { d->zeros.data(), d->zeros.data() }; r.process (in, m, false); pad -= m; }
    d->toDrop = (int) r.getStartDelay();
    d->lat = d->margin;
    d->pull();
}
int RubberBandBox::latency() const noexcept { return d->lat; }
int RubberBandBox::inputLatency() const noexcept { return d->margin - kHopMargin; }
void RubberBandBox::setPitch (double ratio, bool keepFormants) noexcept
{
    if (ratio != d->ratio) { d->ratio = ratio; d->rb->setPitchScale (ratio); }
    if (keepFormants != d->formants) { d->formants = keepFormants; d->rb->setFormantOption (keepFormants ? RubberBand::RubberBandStretcher::OptionFormantPreserved : RubberBand::RubberBandStretcher::OptionFormantShifted); }
}
void RubberBandBox::process (const float* const* in, float* const* out, int n) noexcept
{
    d->rb->process (in, (size_t) n, false);
    d->pull();
    for (int i = 0; i < n; ++i)
    {
        const bool has = d->fill() > 0;
        for (int c = 0; c < 2; ++c) out[c][i] = has ? d->fifo[c][d->rd % d->cap] : 0.0f;
        if (has) ++d->rd;
    }
}
} // namespace phantom
