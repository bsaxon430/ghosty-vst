# Poltergeist 2 (GhostBlock)

Poltergeist's look on the **Six Sines** engine (https://github.com/baconpaul/six-sines, Paul Walker, upstream commit d6f9b15).

Rule (owner, Nov 2026): the upstream engine stays pure. Our work is only the UI and plumbing on top.

What is ours (everything else is upstream, unchanged):
- `src/ghostblock/p2-editor.*` - the editor: ghost field (drag a ghost onto one below it = it haunts it / FM),
  ghost and link panels, TONE / PITCH DROP / DRIFT / VOICE cards, MAIN page, six macros, keyboard, BLACK / PURPLE / LIGHT.
- `src/ghostblock/machine/*` + `resources/ghostblock/machine/*` - the ghost-machine editor (the default since the
  `poltergeist-2-machine` branch): the same functions as `p2-editor`, drawn from the hand-painted asset pack
  (pages GHOSTS / MAIN / MORPH, listed in `ui-layout.json` "page_order"; a page added there with `assets/<key>/` is
  picked up without code). Hoses between OUT and IN sockets are the FM links, the preset ledger is the browser, SKY is
  the look (STORM = BLACK, DUSK = PURPLE, CLEAR = LIGHT), the CALM chain stops lightning and slows the clouds.
  Its own software compositor redraws only the 16 px cells that changed. No engine, DSP or preset code in it: it
  sends the same BEGIN_EDIT / SET_PARAM / END_EDIT messages. The first editor stays in the tree and is built instead
  with `-DP2_CLASSIC_EDITOR=ON`. The design's brass piano roll is NOT built (the plugin has no sequencer).
  Art: made for GhostBlock (this project's own). Live text: Poppins (SIL Open Font License 1.1,
  `resources/ghostblock/machine/fonts/Poppins-OFL.txt`).
- `src/ghostblock/morph.h` + `processMorph` in `src/clap/six-sines-clap.cpp` - MORPH, the blend board: four corners, each
  THIS SOUND or its own sound on its own (unchanged) engine instance; the puck mixes the real sounds (equal power,
  volume + pan per corner, ~20 ms gain glide). Saved after the patch's terminating zero in the plugin state.
- `src/ghostblock/spectral/*` - SPECTRAL FX, the effect rack after the engine (see "SPECTRAL FX" below): our rack /
  plumbing / page around third-party DSP that is NOT ours (`libs/ghostblock-spectral/`, unmodified).
- `src/ghostblock/sub/*` - SUB, the sub-bass layer under the engine (the SUB sign under the VOICES plate; see
  `src/ghostblock/sub/README.md`): Bombaz's oscillator (compiled as it is) + a checked C++ port of Subhoofer's harmonic
  algorithms (`libs/ghostblock-sub/`), added to the output after SPECTRAL FX. Off = the output is untouched.
- `src/ghostblock/machine/backroom.cpp` + `resources/ghostblock/machine/common/backroom/*` - the BACK ROOM: the ghost's eyes
  and mouth (GHOSTS / MAIN / MORPH) are doors to a full-window room with every FM link as plumbing (rows = the ghost that
  haunts, columns = the ghost it haunts, the diagonal = feedback, YOU HEAR = the mix). Click a window to plug it in, drag
  for how much, right-click to unplug; FRONT PANEL or Esc goes back. Same parameters and messages as the front panel's
  hoses; the front panel is unchanged. Art cut from the machine's own by `scripts/ghostblock/make_backroom_art.py`.
- `src/ui/spectrum-analyzer.{h,cpp}` (upstream, COLOUR / LOOK ONLY, owner-approved Nov 2026; every edit marked
  `// GHOSTBLOCK: colour only`): an optional `Look` (`setLook`) for the SPYGLASS - scope-trace colour, a glow under the
  spectrum line, the plot labels' typeface, the right-click menu's LookAndFeel. Defaults = upstream's look; analysis,
  FFT, threads, sizes, layout and menu items / actions are unchanged. Set from `src/ghostblock/machine/spyglass.cpp`.
- `src/ghostblock/ui-note-queue.h` - the on-screen keyboard's notes into the voice manager (`src/clap/six-sines-clap.cpp`).
- `src/clap/six-sines-clap.cpp` - `createEditor()` makes our editor; fixed window size.
- names / ids: product "Poltergeist 2", CLAP id `io.ghostblock.poltergeist2`, AU `aumu Plt2 Ghbk`, user folder
  Documents/GhostBlock/Poltergeist 2 (`CMakeLists.txt`, `src/clap/*entry-impl.cpp`, `src/clap/preset-discovery-impl.cpp`,
  `src/synth/synth.cpp` userDocumentsPath only).
- `src/ghostblock/tests/p2-tests.cpp` - offline checks (screenshots of both editors, hose drag -> link, every layout
  control exists / is at least 34 px / overlaps nothing, every parameter of the first editor is reachable in the
  machine, all factory presets load, frame cost, CLAP render).
- `src/ghostblock/spectral/tests/*` - the rack's checks (`p2-spectral-tests checks`) and `p2-render-hash`.
- `src/ghostblock/sub/tests/*` - SUB's checks (`p2-sub-tests checks`).
- `.github/workflows/poltergeist2-mac.yml` - builds on Brandon's Mac (self-hosted runner) and installs it.

The patch format id stays `org.baconpaul.six-sines`, so every Six Sines patch (.sxsnp) loads in Poltergeist 2.

License: the combined work is GPL-3 (Six Sines' source is MIT with GPL-3 dependencies). See LICENSE.md and resources/LICENSE_GPL3.
SPECTRAL FX adds MPL-2.0 files (Spectral Corruptor, file-level copyleft, compatible with GPL-3) and GPL-3 files (WarpCore):
`ghostblock/packaging/THIRD-PARTY-LICENSES.txt` ships in the package; details in `libs/ghostblock-spectral/README.md`.
SUB adds GPL-3.0-or-later code (Bombaz, and a port of Subhoofer): same licence family as the plugin. The obligation that
comes with it: whoever is given the plugin must be given, or be able to get, the complete source of that build under the
GPL (CREDITS.txt names the address); the copyright notices stay. Details in `libs/ghostblock-sub/README.md`.

## SPECTRAL FX

Six Sines engine (+ MORPH) -> **SPECTRAL FX** -> output. An insert on the plugin's stereo output, in
`SixSinesClap::process` after the engine has rendered (`spectralProcess`). The engine, its presets and MIDI are untouched;
with the rack off the output is bit-identical to a build without it and the plugin reports no latency.

- CHAIN: four slots, each any of the 38 effects of Spectral Corruptor (its real modules and FFT engine), slot on / mix,
  the effect's real parameters; FFT size 128..16384, window, overlap, frequency limit, chain mix.
- WARPCORE: WarpCore's spectral inversion (zero latency), its nine real parameters.
- Master on, master mix (dry is delay-compensated), order CHAIN > WARPCORE or WARPCORE > CHAIN.
- Latency: FFT size - 1 samples while the rack is on and a slot holds a spectral effect, else 0. Reported through
  CLAP latency (so VST3 / AU too); a change asks the host to restart the plugin, as CLAP requires.
- Saved in the plugin state after MORPH's section (`P2SPECTRAL 1`); older projects load with the rack off.
  Not in .sxsnp preset files (same as MORPH).
- Everything else (parameter ids, editor API, threads, decisions): `src/ghostblock/spectral/README.md`.
- No Spectral Corruptor licensing / activation / update / interface code is in the tree (`libs/ghostblock-spectral/verify.sh`).

