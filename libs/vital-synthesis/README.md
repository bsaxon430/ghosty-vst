Vital's oscillator, unchanged.

From https://github.com/mtytel/vital at commit 636ca0ef517a4db087a6a08a6a8a5e704e21f836 (GPL-3, Matt Tytel):
`src/synthesis/framework`, `src/synthesis/lookups`, `src/synthesis/producers`, `src/common/fourier_transform.h`,
`src/common/synth_constants.h`, `src/common/wavetable` (the wavetable builder that reads .vitaltable files),
`src/common/line_generator.*`, `third_party/kissfft`, `third_party/json`. No file here is edited. Vital's presets and wavetables are
not included (they are not free to redistribute). SEANCE uses this through `src/ghostblock/shade`.
