How the BOARD SETS (src/ghost/GhostBoardSets.h) were picked. Offline only.
Each factory bass was rendered alone by the engine (one held C2, 2 s, then release; a small headless program on
surge-common: SurgeSynthesizer + loadPatchByPath + playNote + process). an.py measures loudness / envelope / pitch,
search.py tries every set of four (and every corner order) and keeps the ones whose equal-power mix stays even over
the whole board (two waves that run against each other thin out in the middle), sets.py is the result.
