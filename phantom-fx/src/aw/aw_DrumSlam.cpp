// SPDX-License-Identifier: AGPL-3.0-or-later
// Airwindows DrumSlam (MIT, Chris Johnson), compiled unchanged from third_party/airwindows/DrumSlam inside its own namespace.
#include <math.h>
#include <set>
#include <string>
#include <cstdint>
#include "audioeffectx.h"
#include "Airwindows.h"
#if defined(__clang__)
 #pragma clang diagnostic push
 #pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wall"
 #pragma GCC diagnostic ignored "-Wextra"
 #pragma GCC diagnostic ignored "-Wconversion"
 #pragma GCC diagnostic ignored "-Wsign-conversion"
 #pragma GCC diagnostic ignored "-Wshadow"
 #pragma GCC diagnostic ignored "-Wfloat-conversion"
 #pragma GCC diagnostic ignored "-Wunused-parameter"
 #pragma GCC diagnostic ignored "-Wfloat-equal"
#endif
namespace aw_DrumSlam
{
#include "../../third_party/airwindows/DrumSlam/DrumSlam.h"
#include "../../third_party/airwindows/DrumSlam/DrumSlam.cpp"
#include "../../third_party/airwindows/DrumSlam/DrumSlamProc.cpp"
}
#if defined(__clang__)
 #pragma clang diagnostic pop
#elif defined(__GNUC__)
 #pragma GCC diagnostic pop
#endif
namespace phantom::aw
{
namespace
{
struct W final : Plugin
{
    std::unique_ptr<aw_DrumSlam::DrumSlam> p { new aw_DrumSlam::DrumSlam (nullptr) };
    void setSampleRate (double sr) override { p->phantomSetSampleRate (sr); }
    void setParameter (int i, float v) override { p->setParameter (i, v); }
    void process (float* l, float* r, int n) override { float* io[2] { l, r }; p->processReplacing (io, io, n); }
};
}
std::unique_ptr<Plugin> makeDrumSlam() { return std::make_unique<W>(); }
}
