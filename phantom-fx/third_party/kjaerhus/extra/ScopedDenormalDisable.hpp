// PHANTOM FX SHIM: DPF's extra/ScopedDenormalDisable.hpp (the rack already runs with denormals off: juce::ScopedNoDenormals)
#pragma once
namespace DISTRHO { struct ScopedDenormalDisable { ScopedDenormalDisable() {} }; }
