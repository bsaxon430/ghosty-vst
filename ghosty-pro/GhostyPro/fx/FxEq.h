#pragma once
// SPIRIT EQ: the rack's precision EQ. Interface between the rack (ProFx.cpp / FxPanel.cpp) and the EQ files (FxEq*.cpp).
// The rack adds fx_eq_on and fx_eq_mix itself and handles on/off fades + dry/wet (using UnitDsp::delayDry for latency).
// Everything else under fx_eq_* is added by eq::addParameters.
//
// Parameter IDs the rack's scenes rely on (k = 1..12):
//   fx_eq_b<k>_on (bool), fx_eq_b<k>_type (choice: Bell, Low Shelf, High Shelf, Low Cut, High Cut, Notch, Band Pass, Tilt Shelf, Flat Tilt),
//   fx_eq_b<k>_slope (choice: 6,12,18,24,36,48,72,96 dB/oct), fx_eq_b<k>_freq (Hz), fx_eq_b<k>_gain (dB), fx_eq_b<k>_q
//   fx_eq_out (dB)
#include "FxCore.h"
#include "FxUi.h"

namespace gpfx {
namespace eq {

constexpr int kBands = 12;
enum BandType { btBell, btLowShelf, btHighShelf, btLowCut, btHighCut, btNotch, btBandPass, btTiltShelf, btFlatTilt, kNumBandTypes };

void addParameters (Layout& layout);
std::unique_ptr<UnitDsp> createUnit();
juce::ValueTree saveState (const UnitDsp& unit);                 // non-parameter EQ state (A/B slots etc.), child of the rack state
void loadState (UnitDsp& unit, const juce::ValueTree& tree);
std::unique_ptr<ui::Page> createPage (juce::AudioProcessorValueTreeState& apvts, UnitDsp& unit);

} // namespace eq
} // namespace gpfx
