// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST BREAK: the split sidechain's crossover, on LSP DSP Units' Crossover (LGPL-3.0, pinned with Phantom FX's LSP build).
#include "Duck.h"
#include <lsp-plug.in/dsp/dsp.h>
#include <lsp-plug.in/dsp-units/util/Crossover.h>

namespace gbk
{
struct Split3::Impl
{
    lsp::dspu::Crossover x; float* const* dst = nullptr; float fl = -1, fh = -1; size_t fs = 999;
    static void take (void* object, void*, size_t band, const float* data, size_t first, size_t count)
    { auto* s = static_cast<Impl*> (object); juce::FloatVectorOperations::copy (s->dst[band] + first, data, (int) count); }
};
void Split3::initDsp() { static const bool once = [] { lsp::dsp::init(); return true; }(); (void) once; }
Split3::Split3() : d (new Impl()) { initDsp(); d->x.construct(); }
Split3::~Split3() { d->x.destroy(); delete d; }
void Split3::prepare (double sr)
{
    d->x.destroy(); d->x.construct(); d->x.init (kBands, kDuckChunk); d->x.set_sample_rate ((size_t) std::lround (sr));
    for (size_t k = 0; k < (size_t) kBands; ++k) d->x.set_handler (k, &Impl::take, d, nullptr);
    d->fl = d->fh = -1; d->fs = 999; set (160, 5000, 1); d->x.reconfigure(); d->x.reset();
}
void Split3::set (float lo, float hi, int slope)
{
    static const size_t s[] { lsp::dspu::CROSS_SLOPE_LR2, lsp::dspu::CROSS_SLOPE_LR4, lsp::dspu::CROSS_SLOPE_LR8 };
    lo = juce::jlimit (30.0f, 2000.0f, lo); hi = juce::jlimit (lo * 1.5f, 16000.0f, hi);
    const size_t sl = s[juce::jlimit (0, 2, slope)];
    if (lo != d->fl || hi != d->fh || sl != d->fs)
    {
        d->x.set_frequency (0, lo); d->x.set_frequency (1, hi);
        for (size_t i = 0; i < 2; ++i) { d->x.set_slope (i, sl); d->x.set_mode (i, lsp::dspu::CROSS_MODE_BT); }
        d->fl = lo; d->fh = hi; d->fs = sl;
    }
}
void Split3::reset() { d->x.reset(); }
void Split3::process (const float* in, float* const* out, int n) { d->dst = out; d->x.process (in, (size_t) n); }
} // namespace gbk
