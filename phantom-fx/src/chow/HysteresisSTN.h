// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX: ChowTapeModel's hysteresis can also run a small neural "STN" solver (RTNeural + trained weights). Phantom FX uses the
// RK4 solver only, so this stands in for the STN class with the same interface; its process() is never called.
#pragma once
struct HysteresisSTN
{
    static constexpr double diffMakeup = 1.0 / 6.0e4;
    void prepare (double) {}
    void setParams (float, float) {}
    double process (const double*) noexcept { return 0.0; }
};
