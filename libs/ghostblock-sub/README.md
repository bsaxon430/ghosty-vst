# libs/ghostblock-sub - the upstream DSP behind Poltergeist 2's SUB

Source files of two open-source projects, byte-identical to the upstream commits below (`PROVENANCE.sha256` lists every
file's SHA-256; `verify.sh` re-checks them, and with `--clone` against fresh clones). Our own code that uses them lives
in `src/ghostblock/sub/`.

| folder | upstream | commit | licence |
| --- | --- | --- | --- |
| `bombaz/` | https://github.com/hollance/bombaz - "Monophonic bass synth based on Window Function synthesis", M.I. Hollemans | `434d8b6eaf228338b0b2d2f56ad470c1f8929ddd` | GPL-3.0-or-later (`bombaz/LICENSE.txt`) |
| `subhoofer/` | https://github.com/ardura/Subhoofer - v2.2.3, Ardura | `19ea26b500fe0f3fc0516bde02b565af80501ae6` | GPL-3.0-or-later (stated in `subhoofer/Cargo.toml`; upstream has no licence file) |
| `reference/src/util.rs` | two small functions (`db_to_gain` ...) as in https://github.com/ardura/nih-plug `src/util.rs`, the fork Subhoofer builds against - a stand-in so the reference needs no plugin framework | `fc2ae2da8e8aef414f53a51c24ecf378448c93e1` | ISC |

## Bombaz: compiled as it is

`bombaz/src/Oscillator.h`, `DCBlockFilter.h` and `DSP.h` are compiled into the plugin unmodified
(`src/ghostblock/sub/bombaz-voice.h` includes them). They are the synthesis: the Blackman-Harris pulse, its width, the two
drive waveshapers, the loudness compensation, parameter changes at the start of a cycle, the DC killer.

`Voice.h`, `Synth.h`, `Synth.cpp`, `Parameters.h`, `Parameters.cpp` need JUCE's plugin classes, so the plugin does not
compile them; `bombaz-voice.h` is an adaptation of them (the glide, what WIDTH / DRIVE / GLIDE / TUNE mean), marked line
by line. They are vendored because the test program compiles them, exactly as they are, against real JUCE as the
REFERENCE: `p2-sub-tests bombaz` plays the same notes through upstream's `Synth` and through our voice and requires the
two to be sample-for-sample identical.

NOT taken: upstream's plugin shell (`PluginProcessor`, `PluginEditor`), interface, Projucer project and presets.

## Subhoofer: a port, checked against the original

Subhoofer is written in Rust (nih-plug). The plugin does not link Rust; `src/ghostblock/sub/subhoofer-port.h` is a C++
PORT of the harmonic path of `subhoofer/src/lib.rs` and `subhoofer/src/SweetenX.rs`: the algorithms "A Bass 3",
"A Bass 2", "8 Harmonic Stack", "Duro Console", "TanH Transfer" and "Custom" (functions `chebyshev_tape`,
`tape_saturation`, `b_bass_saturation`, `c_bass_saturation`, `custom_sincos_saturation`, `leaf_saturation`,
`SweetenX::process`), the hardness stage and the DC high-pass of `Subhoofer::process`. NOT ported: the subharmonic
generator (the sub's pitch comes from Bombaz), the interface, the parameter smoothing.

`reference/` proves the port: `make-golden.py` checks the two Rust files against their pinned SHA-256, cuts the functions
and the harmonic part of `process()` out of them VERBATIM, compiles that Rust text with rustc, runs 72 cases x 160 stereo
samples plus 216 function values through it and writes `src/ghostblock/sub/tests/subhoofer-golden.h`.
`p2-sub-tests port` runs the same inputs through the C++ port: bit-identical on Linux (same maths library as the
reference run); on macOS within rounding of sin / cos / tanh / atan / pow.

    P2SUB_REF_TARGET=/tmp/p2sub-ref python3 -I libs/ghostblock-sub/reference/make-golden.py           # regenerate
    P2SUB_REF_TARGET=/tmp/p2sub-ref python3 -I libs/ghostblock-sub/reference/make-golden.py --check   # compare

## Licence

Both projects are GPL-3.0-or-later; Poltergeist 2 as a whole is GPL-3, so the terms match. The copyright notices stay in
the files; `ghostblock/packaging/THIRD-PARTY-LICENSES.txt` and `CREDITS.txt` name both. Anyone who receives the plugin
must be able to get this source (the address is in CREDITS.txt).
