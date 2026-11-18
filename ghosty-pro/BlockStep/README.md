# Block Step (plugin)

The Block Step drum tracker (web app) as a VST3 / AU / Standalone instrument.

- `data/index.html` is the same page as the web app (Ghost Money UI). Inside the plugin it detects JUCE
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
