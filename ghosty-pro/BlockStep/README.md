# Block Step (plugin)

The Block Step drum tracker (web app) as a VST3 / AU / Standalone instrument.

- `data/index.html` is the same page as the web app. Its look is two painted scenes (build 113; see **Scenes** below).
  Inside the plugin it detects JUCE
  (`window.__JUCE__`) and hands sound, transport and saving to C++ (events `bs_state`, `bs_audition`,
  `bs_transport`, `bs_seek`, `bs_ready` in; `bs_restore`, `bs_pos` out).
- `BlockStepEngine.h`: sample-accurate sequencer + one-shot sampler, synced to host ppq (or its own clock when the
  host is stopped), all tracker FX, per-channel choke, background mp3 decoding (exact 1105-sample MP3 delay removed).
- `data/sounds`: Brandon's Block kit (167) + CC0 breaks library (340, Breakbeat Pattern Maker's VCSL / TR-808 /
  UDNB / Lo-Fi sets). `data/allman.json` is the page's KITMAN (same order: the page's sound indices = the engine's).
- Native strip: DRAG MIDI (GM drum notes), DAW sync status, UNLOCK (demo gate like every GhostBlock plugin).
- Off in the main build (`GHOSTY_BLOCKSTEP=OFF`); `.github/workflows/blockstep-mac.yml` builds it alone on the
  `blockstep` branch when `BlockStep/BUILD` changes. Engine tests: `tests/EngineTests.cpp` (all pass on Linux).
- Not yet: Windows (needs the WebView2 SDK on the runner), Gumroad product ID (`STORE_BLOCKSTEP`; until then the
  bundle key unlocks it).

## Looks (builds 113 and 114)
The SCENE button next to the name switches between three looks. It always opens on GHOST CITY (build 117); the choice is saved as (`gb_scene`,
a look setting like LINES; it is not part of a project's saved state):
- **GHOST CITY**: a haunted neon city at night, seen through the interface. Every panel is a sheet of glass in a
  hand-bent neon tube, every button a thick glass key (dark with a tube round it, or lit like a sign), every note a
  block of lit gel. The grid is a hologram display: an emblem etched in the glass, a beam where the song is playing,
  a ring and sparks off every hit. Rain, a hologram ghost drummer, twelve sign boards that flare with their drums.
- **SPIRIT HILLS**: a gouache-and-ink anime landscape. Inked paper panels with a red seal, painted wooden block keys,
  a wooden tool rail. A cel ghost drummer on a taiko, twelve little spirits that hop to their drums, a windmill that
  turns with the tempo, petals.
- **ORIGINAL**: the Ghost Money look (swirling ghosts, falling glyphs), drawn by the page's own untouched code
  (`draw0` / `drawCell0`, the old background on `<canvas id="bg">`).

Only the look changes: every control is where it was in build 112 (the page tests check this), and the sound, the kit
and the generators are untouched. HACKER MODE hides the looks (its SCENE button is greyed out). GHOST ANIMATION OFF
keeps the picture but stops everything that moves.

How it is built (all inside `data/index.html`, because the plugin serves only that one file):
- the scene paintings are `<script type="text/plain" id="art-...">` blocks (base64 JPEG / PNG) decoded on demand;
- the painted frames and keys are 9-slice pictures inside the style sheet (`--p-*`, `--k-*`, `--q-*`, `--b-*`,
  used with `border-image`: corners keep their shape, sides stretch or repeat, so no control changes size or place);
- GHOST CITY's sign lettering is Tilt Neon (SIL OFL 1.1), embedded like the page's other fonts;
- the scene code is at the end of the page's script (`var SCN = ...`, `THEMES`, `cityCell`, `cityBack`, `cityOver`,
  `animeCell`) and draws on `<canvas id="scn">`; the tracker reads its colours and painters from `TH` (the theme);
- the scene styles are at the end of the page's `<style>` (`body.sc-city`, `body.sc-anime`).

`scenes/` holds what made them: `paint/` (the painters: Node + Playwright + Chrome's canvas, seeded so a re-render
gives the same picture; `paint/ui.js` paints the frames and keys), `page/` (the scene code and styles, `pack.py`,
`swap_art.py`, and `assemble.py`, which builds the page from build 112's). See `scenes/README.md`.

## Piano
The 🎹 PIANO sound is the Salamander Grand Piano V2 (Yamaha C5) by Alexander Holm, licensed CC BY 3.0 (https://creativecommons.org/licenses/by/3.0/). 22 notes, trimmed and re-encoded (`data/sounds/pn-*.mp3`); every key plays from the nearest one.

## Piano sound bank (credits and licenses)
All free to use in a commercial plugin; every library's license was checked before use (2026-10-28).
- GRAND: Salamander Grand Piano V2 by Alexander Holm, CC BY 3.0
- UPRIGHT: Upright Piano KW (FreePats), CC0. HAUNTED, FROZEN and BACKWARDS are this piano run through effects.
- HONKY-TONK: Old Piano FB, a Francis Bacon player piano recorded by Piotr Barcz (FreePats), CC0. ON FIRE and BROKEN TAPE are this piano run through effects.
- WURLITZER, CP80, PIANET: Greg Sullivan E-Pianos, CC BY 3.0
- FM PIANO: FM Piano 2 (FreePats), CC0
- HARPSICHORD: Versilian Community Sample Library (VCSL), CC0
Notes are about one every minor third, trimmed and re-encoded as mp3 (`data/sounds/ps-*.mp3`); each key plays from the nearest one.
