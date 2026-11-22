// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
//
// The design on screen in the new mode. This file holds exactly one function, and it is the one a design replaces:
// include your view's header, return your view here, and add your files to cmake/ScopeSources.cmake.
// Nothing else in the shared files changes for a new design (studio/README.md, "How a designer adds a view").
#include "RiverView.h"

namespace mosaic::scope
{
std::unique_ptr<View> createView (Context& c)
{
    return createRiverView (c);
}
}
