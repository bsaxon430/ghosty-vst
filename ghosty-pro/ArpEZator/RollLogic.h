#pragma once
// Piano-roll logic that the tests can reach without a window: the time view (zoom / pan clamps), the line
// (GLIDE / FATTEN / STEP) handle drags and the lock toggle. Message thread only; no JUCE.
#include "Idea.h"
#include <algorithm>
#include <cmath>

namespace arp
{
struct RollView { double start = 0, len = 4.0 * BAR; };

constexpr double kMinViewLen = BAR / 2.0;   // zoomed in: half a bar
inline double maxViewLen (int total) { return std::max (4.0 * BAR, (double) std::max (BAR, total)); }

inline RollView clampView (RollView v, int total)
{
    v.len = clampv (v.len, kMinViewLen, maxViewLen (total));
    v.start = clampv (v.start, 0.0, std::max (0.0, (double) total - v.len));
    return v;
}
// zoom by `factor` (< 1 = in) keeping the tick under the pointer (at fraction `frac` of the width) where it is
inline RollView zoomAt (RollView v, double frac, double factor, int total)
{
    const double t = v.start + frac * v.len;
    RollView o; o.len = clampv (v.len * factor, kMinViewLen, maxViewLen (total));
    o.start = t - frac * o.len;
    return clampView (o, total);
}
inline RollView panBy (RollView v, double ticks, int total) { v.start += ticks; return clampView (v, total); }

// line handles: a drag changes only the CURRENT type's amount, never the type
inline Link glideDrag (Link l0, double tickAt, int ta, int tb)   // GLIDE: where the glide starts, 0..97 % of the gap
{
    if (l0.type != 1) return l0;
    l0.w = (float) clampv ((tickAt - ta) / (double) std::max (1, tb - ta), 0.0, 0.97);
    return l0;
}
inline Link fattenDrag (Link l0, float startF, float dyPixels)   // FATTEN: up = fatter (70 px = the whole range)
{
    if (l0.type != 2) return l0;
    l0.f = clampv (startF - dyPixels / 70.0f, 0.0f, 1.0f);
    return l0;
}
inline bool linkHasAmount (const Link& l) { return l.type == 1 || l.type == 2; }

// LOCK SEL as a toggle: anything selected that is locked -> unlock all of it, else lock it. Returns true = now locked.
inline bool selectionLocked (const Idea& I, int lane, const std::vector<bool>& sel, int b0, int b1)
{
    const auto& L = I.lanes[lane]; bool anyNote = false;
    for (size_t i = 0; i < L.notes.size() && i < sel.size(); ++i) if (sel[i]) { anyNote = true; if (L.notes[i].lock) return true; }
    if (! anyNote && b0 >= 0) for (int b = b0; b <= b1 && b < (int) L.barLocks.size(); ++b) if (L.barLocks[(size_t) b]) return true;
    return false;
}
inline bool toggleLocks (Idea& I, int lane, const std::vector<bool>& sel, int b0, int b1)
{
    const bool lockIt = ! selectionLocked (I, lane, sel, b0, b1);
    auto& L = I.lanes[lane]; bool anyNote = false;
    for (size_t i = 0; i < L.notes.size() && i < sel.size(); ++i) if (sel[i]) { L.notes[i].lock = lockIt; anyNote = true; }
    if (b0 >= 0) for (int b = b0; b <= b1 && b < (int) L.barLocks.size(); ++b) if (! anyNote || ! lockIt) L.barLocks[(size_t) b] = lockIt;
    return lockIt;
}
inline void unlockAll (Idea& I, int lane)
{
    auto& L = I.lanes[lane];
    for (auto& n : L.notes) n.lock = false;
    std::fill (L.barLocks.begin(), L.barLocks.end(), false);
}
} // namespace arp
