// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX Rhythm group: the upstream foundations, compiled unchanged, each in its own namespace (their names are global upstream:
// `Pattern`, `Delay`, `Hold`, `Curve` ... would collide with other engines). THIRD_PARTY.md lists the files and the commits.
//   gate12::Pattern / Transient   GATE-12 (tiagolr, GPL-3.0): drawn curves (point types + tension), audio transient trigger
//   time12::Delay                 TIME-12 (tiagolr, GPL-3.0): the buffer every time manipulation reads (cubic interpolation)
//   mng::*Effect                  Mango (odoare, AGPL-3.0-or-later): GHOST GRID's lane effects (see MangoLanes.h)
#pragma once
#include <vector>
#include <deque>
#include <mutex>
#include <atomic>
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace gate12 {
#include "../../third_party/gate12/src/dsp/Pattern.h"
#include "../../third_party/gate12/src/dsp/Transient.h"
}
namespace time12 {
#include "../../third_party/time12/src/dsp/Delay.h"
}
