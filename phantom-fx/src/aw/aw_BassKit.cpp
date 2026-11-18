// SPDX-License-Identifier: AGPL-3.0-or-later
// Airwindows BassKit (MIT, Chris Johnson), compiled unchanged from third_party/airwindows/BassKit inside its own namespace.
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
namespace aw_BassKit
{
#include "../../third_party/airwindows/BassKit/BassKit.h"
#include "../../third_party/airwindows/BassKit/BassKit.cpp"
#include "../../third_party/airwindows/BassKit/BassKitProc.cpp"
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
    std::unique_ptr<aw_BassKit::BassKit> p { new aw_BassKit::BassKit (nullptr) };
    void setSampleRate (double sr) override { p->phantomSetSampleRate (sr); }
    void setParameter (int i, float v) override { p->setParameter (i, v); }
    void process (float* l, float* r, int n) override { float* io[2] { l, r }; p->processReplacing (io, io, n); }
};
}
std::unique_ptr<Plugin> makeBassKit() { return std::make_unique<W>(); }
}
