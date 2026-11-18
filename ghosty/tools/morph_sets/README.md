How the MORPH SETS (src/GhostyMorphSets.cpp) were made. Offline only, never shipped.
  sets.py    the sets: base preset + four ghosts as knob changes
  build.py / build2.py   apply them, render every ghost and every halfway point with Vital's own headless
             (UpstreamHeadless --headless -o out.wav -l 2 -m 48 preset.vital), match each ghost's VOLUME to the base,
             scale a ghost's changes down until every pair blends within ~1.5 dB; writes gfinal.json (-> the C++ table)
