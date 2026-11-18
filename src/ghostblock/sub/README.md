# SUB - the sub-bass layer of Poltergeist 2

    notes -> Six Sines engine (+ MORPH) -> SPECTRAL FX -> [MAKE ROOM: low cut] --(+)--> output
       |                                                                        ^
       +--> SUB: Bombaz voice (CLEAN | DIRTY = drive + Subhoofer harmonics) ----+   mono, as late as the FX rack

## What makes the sound (third-party, GPL-3.0-or-later, see libs/ghostblock-sub/README.md)

- **Bombaz** (M.I. Hollemans): its oscillator is compiled as it is (`Oscillator.h`, `DCBlockFilter.h`, `DSP.h`).
  `bombaz-voice.h` hosts two of them, locked cycle for cycle: CLEAN (never driven) and DIRTY (Bombaz's DRIVE). WIDTH,
  DRIVE, GLIDE, TUNE and OCTAVE mean what they mean in Bombaz. One thing is ours: at WIDTH 0 on notes below 66.7 Hz
  the pulse grows with the period, so the clean sub is the same pure window wave on every note.
- **Subhoofer** (Ardura): `subhoofer-port.h` is a C++ port of its harmonic algorithms (A Bass 3, A Bass 2, 8 Harmonic
  Stack, Duro Console, TanH Transfer), hardness stage and DC high-pass, checked bit for bit against upstream's Rust.
  HARMONIC MODE picks the algorithm, HARMONICS is its "Harmonics" amount. Its subharmonic generator is not used.

## Where it sits, and why it cannot clash

- The sub is added AFTER the engine and AFTER SPECTRAL FX, straight onto the output: the engine's saturation, bit
  crusher and FM and every spectral effect never see it. It waits exactly as long as the FX rack's reported latency.
- The harmonic layer is made from the DIRTY pulse itself, so it is phase-locked to the clean sub. Whatever part of an
  algorithm's answer is only a copy of the pulse is measured (a straight-line fit over each cycle) and taken out, so
  the layer is new overtones only and cannot thin or cancel the fundamental (checked: it never loses more than 1 dB).
- It follows the engine's own voice: the lowest key that is held, tuned exactly as the engine tunes it (MTS-ESP, pitch
  bend with the patch's range, MPE, the patch's glide, fine tune and its modulation), with the engine's volume envelope
  and velocity. Mono, identical left and right.
- MAKE ROOM is a 24 dB / octave Linkwitz-Riley high-pass on the engine + FX (not on the sub) that follows the sub's
  pitch: 0 = off, 40 (default) = 1.3 x the sub's pitch, 100 = 2.5 x.
- The voice runs at 4 x the host rate (2 x from 88.2 kHz, 1 x from 176.4 kHz) with a linear-phase decimator; the
  DC blockers are Bombaz's and Subhoofer's own.
- SUB off: `process()` returns before touching a sample; the output is bit-identical to a build without SUB.

## Host parameters (ids 920001 .. 920012, all automatable, after every existing parameter)

ON, LEVEL (-24 .. +6 dB), OCTAVE (-2 / -1 / 0), WIDTH, DRIVE, HARMONICS, HARMONIC MODE, CLEAN / DIRTY, GLIDE (ms),
TUNE (cents), PHASE (RESTART: every new note starts a new pulse; FREE: it runs on), MAKE ROOM.
State: a text section `P2SUB 1` after SPECTRAL FX's in the plugin state; a state without it = SUB off, defaults.
Presets (panel only, they set everything but LEVEL and turn SUB on): PURE, DEEP, 808, ANALOG, REESE BODY, SPEAKER,
RUMBLE, MONSTER.

Not built: DUCK (there is no kick inside the plugin to duck from, and an instrument plugin gets no side-chain it could
be tested with). With MORPH on the sub follows THIS SOUND's engine (its envelope), not the corner sounds'.

## Files

- `sub-bass.h / .cpp` - parameters, state, the audio path. `bombaz-voice.h`, `subhoofer-port.h` - see above.
- `src/clap/six-sines-clap.cpp` (marked `// SUB`) - reads the followed voice after each engine block (`subTrackT`), calls
  `sub.process` after `spectralProcess`, lists the parameters, saves / loads the state.
- `src/ghostblock/machine/sub-ui.cpp` - the SUB sign and panel; art by `scripts/ghostblock/make_sub_art.py`.
- `tests/sub-tests.cpp` - `p2-sub-tests checks`: port vs Rust, voice vs upstream Bombaz, pitch at 44.1 / 48 / 96 kHz,
  glide, clicks, aliasing, levels, presets, state, following the engine, SUB-off bit-identity for every factory preset,
  allocations, CPU.
