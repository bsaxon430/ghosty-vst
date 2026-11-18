# third_party/spectral in Phantom FX

Ghost Atlas's `third_party/spectral` (ghost-atlas branch, commit `0c712c2`), copied byte for byte: Poltergeist 2's spectral rack as it is in
the `poltergeist-2` branch at `cae613d`. Spectral Corruptor v0.2.6 (MPL-2.0) and WarpCore v0.0.12 (GPL-3.0) unmodified
(`PROVENANCE.sha256`; `./verify.sh --clone` compares every file with fresh clones of the pinned upstream commits), and Poltergeist 2's own
hosting of them in `rack/ghostblock/spectral/` (SpectralFx), unchanged. Its `README.md` files speak of Poltergeist 2's paths.

Phantom FX uses it for the CORRUPTOR and WARPCORE engines (`src/engines/SpectralEngines.*`, DECISIONS.md 95): each Spectral Corruptor
effect is a voice of an ENGINE menu in the Phantom FX effect it fits, and WarpCore is an engine of GHOST SPECTRA.

For Ghost Atlas (it compiles Phantom FX's sources, and two copies of this code cannot be linked into one plug-in): this is now the one
copy. `PHANTOM_SFX_LIB_SOURCES` is the rack itself (already in `PHANTOM_SOURCES`), `PHANTOM_SFX_INC` its include path, so Ghost Atlas's
`src/engine/SpectralRack.*` can stay and its own `third_party/spectral` and SPECTRAL FX section of GhostAtlas.cmake give way.
