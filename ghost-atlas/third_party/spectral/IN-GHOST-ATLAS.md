# third_party/spectral in Ghost Atlas

This folder is Poltergeist 2's spectral rack, copied whole from this repository's `poltergeist-2` branch at commit
`cae613d` and not changed in any way:

| here | there | what |
|---|---|---|
| `spectral-corruptor/`, `warpcore/`, `PROVENANCE.sha256`, `verify.sh`, `README.md` | `libs/ghostblock-spectral/` | the two upstream projects' sources (Spectral Corruptor v0.2.6, MPL-2.0; WarpCore v0.0.12, GPL-3.0), unmodified, with their checksums and the notes on what was taken and why |
| `rack/ghostblock/spectral/` | `src/ghostblock/spectral/` | Poltergeist 2's own hosting of them: `spectral-fx.*` (the rack), `sc-engine.*` and `sc-shadow.h` (Spectral Corruptor's modules), `warpcore-*` (one build of WarpCore per instruction set), and its `README.md` |

`README.md` and `rack/ghostblock/spectral/README.md` are Poltergeist 2's and speak of its paths (`libs/simde`,
`src/clap/...`). Not copied: Poltergeist 2's page for the rack (`spectral-panel.*`) and its checks.

How it is compiled here: `GhostAtlas.cmake` (the section headed SPECTRAL FX), which follows Poltergeist 2's
`spectral.cmake` file for file and flag for flag. Three header-only libraries are fetched at the commits
Poltergeist 2 pins: simde, sst-cpputils and the CLAP headers (all MIT).

What stands between the rack and a slot of Ghost Atlas: `src/engine/SpectralRack.h` and `.cpp`.

`./verify.sh` checks the upstream files against `PROVENANCE.sha256`; `./verify.sh --clone` also compares them with
fresh clones of the upstream repositories at the pinned commits (last run 2026-11-14: every file identical).

If Phantom FX gains these effects itself: Ghost Atlas compiles Phantom FX's sources, and two copies of the same
upstream code cannot be linked into one plug-in. This copy and `src/engine/SpectralRack.*` then give way to Phantom
FX's unit (or Phantom FX takes this one: it is already shaped as a `phantom::PhantomEffect`), and sounds saved with the
unit key `spectral` are read into whatever takes its place.

To take a newer rack from Poltergeist 2: copy the same paths again, run `./verify.sh --clone`, build, run the checks
(the rack has its own in `tests/Tests.cpp`, and `GhostAtlasTests spectral` tries every effect on a sound).
