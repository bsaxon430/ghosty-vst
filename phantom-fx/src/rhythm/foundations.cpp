// SPDX-License-Identifier: AGPL-3.0-or-later
// Compiles the GATE-12 and TIME-12 sources (unchanged apart from the one marked include in Pattern.cpp) inside their namespaces.
#include "Foundations.h"
namespace gate12 {
#include "../../third_party/gate12/src/dsp/Pattern.cpp"
#include "../../third_party/gate12/src/dsp/Transient.cpp"
}
namespace time12 {
#include "../../third_party/time12/src/dsp/Delay.cpp"
}
