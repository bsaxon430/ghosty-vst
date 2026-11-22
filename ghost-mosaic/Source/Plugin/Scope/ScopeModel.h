// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
//
// The tap's data model, as the new mode's page sees it: mosaic::tap::Model (Source/Engine/TapModel.h, the tap
// engineer's; studio/TAP.md is its guide). The page uses three calls, all on the message thread:
//     explicit Model (Engine&)      made with the page
//     void setActive (bool)         true when the new mode comes on screen (attach the tap), false when it leaves
//     void tick (double dtSeconds)  once per frame, before the view's frame (it drains the tap's queue)
// and, in the harness only, waitUntilExplained() so that every pick is explained before a picture is taken.
#pragma once
#include "TapModel.h"
