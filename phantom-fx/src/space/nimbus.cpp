// SPDX-License-Identifier: AGPL-3.0-or-later
// CRYSTAL's engine host: sst-effects' Nimbus (GPL-3.0) over the eurorack granular processor (MIT), instantiated for sst's ConcreteConfig.
#include <juce_dsp/juce_dsp.h>
#include "../effects/SpaceGrain.h"
#include "sst/effects/NimbusImpl.h"

template struct sst::effects::nimbus::Nimbus<sst::effects::core::ConcreteConfig>;

namespace phantom
{
NimbusBox::NimbusBox (double sr)
{
    gs = std::make_unique<sst::effects::core::ConcreteConfig::GS> (sr);
    fx = std::make_unique<NimbusHost> (gs.get(), nullptr, nullptr);
    for (auto& v : fx->paramStorage) v = 0.0f;
    fx->paramStorage[NimbusHost::nmb_mode] = 0.0f;      // granular
    fx->paramStorage[NimbusHost::nmb_quality] = 0.0f;   // 16-bit stereo (the cleanest buffer)
    fx->paramStorage[NimbusHost::nmb_mix] = 1.0f;       // all wet: the slot's MIX blends the dry
    fx->paramStorage[NimbusHost::nmb_reverb] = 0.0f;
    fx->initialize();
}
NimbusBox::~NimbusBox() = default;
void NimbusBox::setParam (int idx, float v) noexcept { fx->paramStorage[idx] = v; }
void NimbusBox::process (float* l, float* r) noexcept { fx->processBlock (l, r); }
} // namespace phantom
