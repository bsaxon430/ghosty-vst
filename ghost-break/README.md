# Ghost Break

Brandon's private jungle / drum & bass break instrument (stays on his Mac, like Phantom FX). AGPL-3.0-or-later, because it hosts Phantom FX's effect units.

## What it does
- **4 break bays.** Each plays a break as 16th slices from a 2-bar, 32-step sequence (play / hold / rest, repeats, reverse). Pick from the built-in library or any file in `Music/GhostBlock/Ghost Break/Breaks` (sub-folders become sections; a tempo in the file name, like `_174_`, is used). Drag a file onto a bay to load it.
- **NUDGE** per bay moves every slice start by whole steps of the bay's GRID (1/4 to 1/64), in time.
- **Drum machine:** six free lanes on 32 steps (starting as KICK, SNARE, CLOSED HAT, OPEN HAT, PERC 1, PERC 2; MIDI 36, 38, 42, 46, 39, 37). Any lane takes any sound and any name (right-click the lane name). Click a lane's name or sound for the sound window: YOUR MICKENS DRUMS (`Music/GhostBlock/Ghost Break/Drums`), CLASSIC BREAK KITS (`.../Classic Break Kits/<break>/...`, one-shots cut from the classic breaks) or BUILT-IN, then KICK / SNARE / CLOSED HAT / OPEN HAT / PERCS. Your Mickens kick, snare and hat load by default. The closed hat chokes the open hat (CHOKE, on by default).
- **Split sidechain** (LSP DSP Units crossover): per bay, the kick ducks only the break's lows, the snare + PERC 1 its mids, the hats + PERC 2 its highs; any lane can drive any band. PUNCH (exact hits) or SMART (the drums' real sound), attack / hold / release / shape per band, lookahead, mix, meters.
- **RANDOM START** gives every unlocked bay a new start on its grid. **RANDOM COMBO** writes a new arrangement across the unlocked bays (and unlocked drum lanes) inside the RULES: density, chaos, stutter hits, reverse, trade, slice length, new breaks. **LOCK** a bay, a single step, or a drum lane first. **UNDO** steps back.
- **Effects are Phantom FX's own units:** per bay WRECKER (crunch) and PHANTOM FILTER (parallel filters); on the breaks HAUNT EDIT (stutter, hold the button or F2) with its own PHANTOM FILTER and GHOST GATE; sends to CHORUS and HAUNTED HALLS; GHOST LIMITER on the output.
- **Separate outputs:** each bay and the drums can go to their own stereo output.

## Build
Built from `phantom-fx/CMakeLists.txt` with `-DPHANTOM_WITH_GHOST_BREAK=ON` (see `GhostBreak.cmake`), so it uses exactly the newest Phantom FX effect code. The Mac workflow is `.github/workflows/ghostbreak-mac.yml` (push to the `ghost-break` branch: build, tests, pluginval 10, install).
Tests: `GhostBreakTests` (all checks), `GhostBreakTests breaks <folder>` (every file of a break pack loads and plays), `GhostBreakTests shot out.png [fxSlot]` (paints the screen).

Built-in breaks and drums come from Mr. Mickens' own synthesised library (`ghosty-pro/Mickens/BreakLib.h`, `MkDrumKit.h`); Mr. Mickens itself is untouched.

## NUDGE LINK (0.1.30)
Every step, Ghost Break sends each bay's NUDGE move (in host quarter notes) to any Phantom FX in the same project
(src/NudgeLink.h, identical to Phantom FX's src/rhythm/NudgeLink.h). A Phantom FX whose HAUNT EDIT has FOLLOW GHOST BREAK on
moves its own audio earlier by the followed bay's move, with look-ahead FL Studio compensates. Live NUDGE moves and played-back
automation both go out (Ghost Break reads the parameter at each step). Nothing about Ghost Break's own sound changes.
