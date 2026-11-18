# Block Step scenes

The painted looks of Block Step (builds 113 and 114): **GHOST CITY** and **SPIRIT HILLS** (the third look, ORIGINAL, is the page's own Ghost Money look). Everything here is our own code;
the pictures are painted by a program, not traced or copied from anything.

What ships is only `../data/index.html`. This folder is how the pictures and the scene code in it were made.

## What is where
- `paint/lib.js`: the painting toolkit (`PT`): seeded random numbers, colour mixing, noise, hand-inked lines, gouache
  dabs, neon tubes and lettering, glow, paper and grain textures, bloom, and `PT.post` (turns soft shading into the
  flat tones of gouache).
- `paint/city.js`: GHOST CITY (landscape for the plugin and wide screens, portrait for phones) + the twelve drum
  signs' lit versions.
- `paint/holo.js`: the hologram ghost drummer (body, two arms, drum, flash).
- `paint/anime.js`: SPIRIT HILLS (landscape and portrait).
- `paint/animeSpr.js`: the cel ghost drummer with its taiko, the twelve little spirits, the windmill sails.
- `paint/ui.js`: the controls as paintings: GHOST CITY's glass panels in hand-bent neon tube, its glass keys (dark, rimmed, lit),
  SPIRIT HILLS' inked paper panels, wooden rail and painted block keys. Each is a 9-slice picture the style sheet lays over a box.
- `paint/gh.js`: the GhostBlock ghost drawings, copied from the page so the painters can use them.
- `paint/render.js` + `paint/harness.html`: runs a painter in Chrome and saves its canvases to `out/`.
- `page/scene.js`, `page/scene.css`: the scene code and styles as they were put into the page.
- `page/pack.py`: turns the painter's output into the pictures and numbers the page carries.
- `page/swap_art.py`: puts repainted pictures into an existing page (use this from now on).
- `page/assemble.py`: builds the page from build 112's + this folder (the painted frames, keys and the neon typeface go
  into the style sheet as `url(ui:NAME)` / `url(font:NAME)`). It starts again from build 112, so do not run it once
  the page has been edited by hand after it; from then on edit the page, and use `swap_art.py` for the paintings.
- `shot.js`: screenshots of the page (plugin layout or phone, Chromium or WebKit).

## Changing things
- **Scene code or styles**: edit `../data/index.html` directly (the scene code is at the end of its script, the scene
  styles at the end of its `<style>`). Keep `page/scene.js` and `page/scene.css` in step if you can; the page is
  what counts.
- **Repainting a picture**:

      npm install                                  # the sign fonts (pinned; all SIL OFL 1.1)
      export PLAYWRIGHT=/path/to/node_modules/playwright CHROME=/path/to/chrome   # only if they are not found by default
      node paint/render.js paint/city.js '{}'
      node paint/render.js paint/city.js '{"portrait":1}' city-p
      node paint/render.js paint/holo.js
      node paint/render.js paint/anime.js '{}'
      node paint/render.js paint/anime.js '{"portrait":1}' anime-p
      node paint/render.js paint/animeSpr.js
      node paint/render.js paint/ui.js '{}' ui     # the frames and keys (they go into the style sheet, see below)
      python3 page/pack.py                         # needs Pillow
      python3 page/swap_art.py                     # writes into ../data/index.html
      node ../tests/page_tests.js                  # must say "all page tests passed"

  Made with Playwright 1.63.0 and Chrome 153. The painters are seeded, so the same code gives the same picture.

## Rules the scenes keep
- A frame or key never changes a box: it is a `border-image` (9-slice) with the box's own border left at its width.
- The layout does not change: every control sits where it sat in build 112, in both scenes. The scene styles never
  change a size, a padding or a position. `../tests/page_tests.js` checks this.
- The plugin shows the page at 1370 x 835 and zooms it. The free window between the transport and the engine box
  (`611,10 403x219`) is where each scene puts its drummer, so the paintings are composed around that slot.
- Nothing here touches the sound, the kit or the generators. The page only tells the scene which drum just played
  (`SCN.hit`).
- The plugin serves only `index.html`, so every picture is inside the page.
- On a phone the page scrolls and the top rows wrap (build 113). Before that the phone layout squeezed everything
  into one screen and the rows overlapped.
