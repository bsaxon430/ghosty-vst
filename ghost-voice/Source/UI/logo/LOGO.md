# Ghost Voice logo — polish 3 "the hand, haunted"

Own drawing (AGPL-3.0 with the plug-in). Made by `work/gen.py` (numpy + shapely, PNGs by rsvg-convert); `python3 work/gen.py`
rewrites every file here and `../LogoData.h` (the header lockup embedded in the plug-in as PNG, decoded once into an Image).

- **One hand**: every line is drawn with the same pointed pen (`pen()`): hairline on up- and side-strokes, swelling only where the
  pen pulls down, tapering in and out.
- **Wordmark**: thin, flowing, slightly leaning hand-drawn capitals GHOST VOICE, a little condensed for the header. VOICE's O is a
  small singing face. E's foot does not stop: it trails off as a curl of smoke, thinning to nothing.
- **Mark**: a hooded ghost drawn with the same pen (outline only over a pale body that fades toward the hem), hollow eyes, a singing
  "O", and its hem trailing off as one wisp. The 32 px mark uses one even, heavier line and a filled mouth so it holds at that size.
- **Polish 3, the haunt**: a trailing wisp of ectoplasm (two strands, swelling in the middle, thinning to nothing at both ends)
  drifts low through the word behind the letters and runs into E's curl of smoke.
- **DARK**: bone letters fading to lilac, a wide soft ectoplasm glow round the letters and the ghost, a tighter lilac haze, and the
  wisp lit with its own blur. **LIGHT**: plum ink kept crisp (a whisper of haze, no glow), the wisp a pale violet under the ink.
- The header candles beside the lockup are drawn in the plug-in (`headerCandle`, Editor.cpp): an amber flame with a pale core, an
  inked rim in LIGHT and a warm halo in DARK, so they read in both looks.
- Files: `mark-*`, `wordmark-*`, `lockup-*` (.svg, .png 2048 transparent, `-on-dark/-on-light` on grounds), `mark-32-*`, `header-*.png`.
