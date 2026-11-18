// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST SQUEEZE engine host: vitOTTx's Source/vital_dsp (GPL-3.0, Vital's MultibandCompressor) compiled unchanged; this file drives it
// exactly as vitOTTx's own PluginProcessor does (one SmoothValue per input, plugged in input order; ratios = band ratio x depth x
// upward / downward; vitOTTx's band thresholds and ratios at their own defaults; at most kMaxBufferSize samples per call).
#include "Engines.h"
#include "compressor.h"
#include "utilities/smooth_value.h"
#include <array>
#include <algorithm>

namespace phantom
{
struct SqueezeBox::Impl
{
    using MC = vital::MultibandCompressor;
    std::unique_ptr<MC> comp; std::unique_ptr<vital::Output> in;
    std::array<std::unique_ptr<vital::SmoothValue>, MC::kNumInputs - 1> vals;
    Impl()
    {
        comp = std::make_unique<MC>(); in = std::make_unique<vital::Output>();
        comp->plug (in.get(), MC::kAudio);
        for (size_t i = 0; i < vals.size(); ++i) { vals[i] = std::make_unique<vital::SmoothValue> (0.0f); comp->plug (vals[i].get(), (int) i + 1); }
        set (SqueezeSettings{});
    }
    void v (int input, float x) { vals[(size_t) input - 1]->set (x); }
    void set (const SqueezeSettings& s)
    {
        // vitOTTx's defaults (its PluginProcessor parameter list)
        v (MC::kLowUpperThreshold, -28.0f); v (MC::kBandUpperThreshold, -25.0f); v (MC::kHighUpperThreshold, -30.0f);
        v (MC::kLowLowerThreshold, -35.0f); v (MC::kBandLowerThreshold, -36.0f); v (MC::kHighLowerThreshold, -35.0f);
        const float up = s.depth * s.upward, dn = s.depth * s.downward;
        v (MC::kLowLowerRatio, 0.8f * up); v (MC::kBandLowerRatio, 0.8f * up); v (MC::kHighLowerRatio, 0.8f * up);
        v (MC::kLowUpperRatio, 0.9f * dn); v (MC::kBandUpperRatio, 0.857f * dn); v (MC::kHighUpperRatio, 1.0f * dn);
        v (MC::kAttack, s.attack); v (MC::kRelease, s.release);
        v (MC::kLowOutputGain, s.lowGainDb); v (MC::kBandOutputGain, s.midGainDb); v (MC::kHighOutputGain, s.highGainDb);
        v (MC::kLMFrequency, s.lowHz); v (MC::kMHFrequency, s.highHz);
        v (MC::kMix, 1.0f);   // the rack slot's DEPTH blends the dry
        v (MC::kEnabledBands, (float) MC::kMultiband);
    }
};

SqueezeBox::SqueezeBox() : d (std::make_unique<Impl>()) {}
SqueezeBox::~SqueezeBox() = default;
void SqueezeBox::prepare (double sampleRate) { d->comp->setSampleRate ((int) sampleRate); reset(); }
void SqueezeBox::reset() { d->comp->reset (vital::constants::kFullMask); }
void SqueezeBox::set (const SqueezeSettings& s) noexcept { d->set (s); }
void SqueezeBox::process (float* l, float* r, int n) noexcept
{
    for (int off = 0; off < n;)
    {
        const int m = std::min (n - off, vital::kMaxBufferSize);
        for (auto& x : d->vals) x->process (m);
        auto* buf = (vital::mono_float*) d->in->buffer;
        for (int i = 0; i < m; ++i) { buf[vital::poly_float::kSize * i] = l[off + i]; buf[vital::poly_float::kSize * i + 1] = r[off + i]; }
        d->comp->process (m);
        const auto* o = (const vital::mono_float*) d->comp->output (Impl::MC::kAudioOut)->buffer;
        for (int i = 0; i < m; ++i) { l[off + i] = o[vital::poly_float::kSize * i]; r[off + i] = o[vital::poly_float::kSize * i + 1]; }
        off += m;
    }
}
} // namespace phantom
