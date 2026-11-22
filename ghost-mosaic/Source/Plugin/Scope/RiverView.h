// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
//
// THE RIVER: the new mode drawn as one flow from left to right (studio/STUDIO_NOTES.md has the idea and the
// table element -> field). RiverView.cpp holds the view; ActiveView.cpp returns it.
#pragma once
#include "ScopeView.h"

namespace mosaic::scope
{
std::unique_ptr<View> createRiverView (Context&);
}
