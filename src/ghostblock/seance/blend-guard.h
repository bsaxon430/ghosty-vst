/*
 * SEANCE (GhostBlock) - the crossroads' phase guard: what it measures and what it is allowed to do.
 *
 * The crossroads adds the real outputs of up to five engines (the sound on the table + four corners). Two corners
 * that play the same note with similar waves can line up against each other and cancel: the blend goes thin, the
 * low end first. The plugin measures, every buffer, how loud the blend is against how loud its parts are. When one
 * part keeps pulling the others down (its signal runs opposite to the rest), the guard turns that part upside down
 * (a smooth fade, 30 ms), which makes it add instead of subtract. A level follower then keeps the blend as loud as its
 * parts would be, so moving the flame does not dip or bump the volume. Off = the plain mix, measured only.
 * The values here are for the screen (written by the audio thread, read anywhere).
 */
#pragma once
#include <atomic>

namespace ghostblock::seance
{
struct BlendGuard
{
    std::atomic<bool> on{true};
    std::atomic<float> cancelDb{0.f}; // how much the plain mix loses to cancelling right now (0 = none, -6 = half the power gone)
    std::atomic<float> afterDb{0.f};  // the same after the guard's fixes
    std::atomic<int> flipped{0};      // bit per corner (0-3) the guard has turned upside down; bit 4 = the sound on the table
    std::atomic<bool> active{false};  // the crossroads is mixing two or more parts at the moment
};
} // namespace ghostblock::seance
