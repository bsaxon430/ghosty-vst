/*
 * Ghost Bass - BOARD SETS: ready-made blend boards (owner, 2026-11-16: "morph presets that blend sounds well").
 * Copyright 2026 the Ghost Bass authors. GPL-3.
 *
 * Each set puts four of Surge XT's own factory basses (unchanged) in the BLEND BOARD's corners, with each corner's VOLUME set
 * so all four play at the same loudness. The four were picked from the sustained factory basses so that every point
 * between them stays within about 2.5 dB of a corner: the mixer adds the real sounds with equal power, and two sounds whose
 * waves run against each other (most pure subs) thin out in the middle, so those were kept apart and each set's corners
 * were ordered for the evenest board. Measured offline: one held C2, each patch rendered by the engine alone, then the
 * board's mix at a 7 x 7 grid of puck positions. CLEAN BLEND on, puck in the middle.
 * 2026-11-16: no sound that clicks at its own note starts / ends on fast notes (FM Bass 6, Attacky, Sub 3, Deep End,
 * Smoothie...): SUB FLOOR, FM FOUNDRY, EVIL TWINS, WIDE AND WEIRD and SMOOTH OPERATORS were rebuilt from click-free
 * sounds; a test keeps it that way.
 */
#ifndef GHOSTBASS_SRC_GHOST_GHOSTBOARDSETS_H
#define GHOSTBASS_SRC_GHOST_GHOSTBOARDSETS_H

#include <array>
#include <vector>

namespace ghost
{
struct BoardSetCorner
{
    const char *cat, *name; // patches_factory/<cat>/<name>.fxp
    float vol;              // the corner's VOLUME (linear, 0..1.5)
};
struct BoardSet
{
    const char *name, *hint;
    std::array<BoardSetCorner, 4> corners; // A top-left, B top-right, C bottom-left, D bottom-right
};
inline const std::vector<BoardSet> &boardSets()
{
    static const std::vector<BoardSet> v = {
        {"SUB FLOOR", "deep, clean subs", {{{"Basses", "Mellow", 0.941f}, {"Basses", "Doomsday", 1.063f}, {"Basses", "Stable", 0.719f}, {"Basses", "Sub 1", 1.073f}}}},
        {"FM FOUNDRY", "FM basses, tight", {{{"Basses", "FM Bass 4", 1.500f}, {"Basses", "FM Bass 1", 0.296f}, {"Basses", "E-Bass", 0.753f}, {"Basses", "Distorted FM", 0.416f}}}},
        {"GRIT GARAGE", "distorted basses", {{{"Basses", "Crush Bass", 1.500f}, {"Basses", "Distorted FM", 0.650f}, {"Basses", "Dist Bass 1", 0.727f}, {"Basses", "Distorted Bass", 0.953f}}}},
        {"ANALOG ROOM", "classic analog basses", {{{"Basses", "Bass 1", 0.898f}, {"Basses", "Bass 3", 1.500f}, {"Basses", "Bass 2", 0.301f}, {"Basses", "Rubber Bass", 0.452f}}}},
        {"SAW AND STATIC", "saws and buzz", {{{"Basses", "Static 1", 1.287f}, {"Basses", "Static 2", 0.678f}, {"Basses", "Rumble", 1.003f}, {"Basses", "Digibass", 0.997f}}}},
        {"EVIL TWINS", "dark and menacing", {{{"Basses", "Behemoth", 0.396f}, {"Basses", "Ring Mayhem", 1.191f}, {"Basses", "Squared", 1.356f}, {"Basses", "Doomsday", 0.839f}}}},
        {"WIDE AND WEIRD", "wide, keys, odd", {{{"Basses", "Wide Bassline", 1.075f}, {"Basses", "Metal Square", 0.877f}, {"Basses", "WT Bass", 1.112f}, {"Basses", "Piano Bass", 0.930f}}}},
        {"SMOOTH OPERATORS", "round and warm", {{{"Basses", "Piano Bass", 1.297f}, {"Basses", "Mellow", 0.585f}, {"Basses", "Rumble", 0.374f}, {"Basses", "Wide Bassline", 1.500f}}}}
    };
    return v;
}
} // namespace ghost

#endif
