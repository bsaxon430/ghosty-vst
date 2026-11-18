/*
 * Ghost Bass - a bass instrument built on Surge XT.
 * Copyright 2026 the Ghost Bass authors. Based on Surge XT by the Surge Synth Team.
 * Released under the GNU General Public Licence v3 or later (see LICENSE).
 *
 * The few small things upstream Surge structs need to know about Ghost Bass. Kept tiny on purpose,
 * so SurgeStorage.h only has to include this one header.
 */
#ifndef GHOSTBASS_SRC_GHOST_GHOSTSTORAGEHOOKS_H
#define GHOSTBASS_SRC_GHOST_GHOSTSTORAGEHOOKS_H

#include <functional>

class TiXmlElement;

namespace ghost
{
// patch XML hooks, set by GhostEngine; called on whatever thread saves/loads the patch
struct PatchHooks
{
    std::function<void(TiXmlElement &patch)> onSave; // add <ghostbass>, write base values
    std::function<void(TiXmlElement *patch)> onLoad; // read <ghostbass> (nullptr-safe)
};
} // namespace ghost

#endif
