# GHOST

GhostBlock's fast song maker: one VST3 that opens five of the owner's own plug-ins inside itself and wires them together.

| part | what it is |
| --- | --- |
| **ARPEZ** page | ArpEZator itself (its own window, inside GHOST's screen). It writes the song: CHORDS, ARP, MELODY, BASS. |
| **4 lanes** (right side) | each lane plays ArpEZator's notes for that part through **one sound of Ghosty Pro 2, Poltergeist 2 or Ghost Atlas** (the real plug-ins). `<` `>` step through sounds that suit the lane, **NEXT GOOD** picks a random one from any of the three, clicking the amber display opens every sound (search, synth, folder). |
| **FX** (per lane) | Poltergeist 2's SPECTRAL rack: 4 bays (Spectral Corruptor's 38 effects) + WARP (WarpCore). Its code is compiled into GHOST, so it works on every sound, not only Poltergeist 2's. |
| **EDIT** (per lane) | opens the sound in its own synth's window (Vital's editor for Ghosty Pro 2, Poltergeist 2's machine, Ghost Atlas's worlds). Whatever is changed there is saved with the song. |
| **BEATS** page | Ghosty Beats itself: QUICK page, sequencer and pattern generators, every drum sound. |
| **GRAPH** | greyed for now: Ghost Graph's music through these sounds comes next. |

- When FL plays, everything follows FL. When FL is stopped, GHOST's **PLAY** runs one clock that ArpEZator and Ghosty Beats both follow.
- **KEYS** on a lane: your MIDI keyboard plays that lane (to try a sound).
- **TONE**: left = darker, right = thinner, middle = untouched.
- The FX can add delay (a spectral bay = its FFT size); GHOST delays the other parts by the same amount and tells FL, so everything stays in time.
- A safety limiter sits last, so GHOST never clips.

## Where the sounds come from
- The three synths' factory sounds are packed into GHOST (`presets/*.pack`, `presets/atlas/factory.json`; `tools/pack_presets.py`).
- Your own: the Vital / Vial folders Ghosty Pro 2 searches (and Ghosty Pro 2's own list of what it found, which includes your packs),
  `Documents/GhostBlock/Poltergeist 2/Patches`, `Music/GhostBlock/Ghost Atlas/Presets`. **LOOK AGAIN** in the sound list searches again.
- A sound is loaded by handing its synth the same state the synth itself saves, so a sound in GHOST is exactly the sound in the synth.

## What must be installed
GHOST opens `ArpEZator.vst3`, `Ghosty Beats.vst3`, `Ghosty Pro 2.vst3`, `Poltergeist 2.vst3`, `Ghost Atlas.vst3` from
`~/Library/Audio/Plug-Ins/VST3` (or `/Library/Audio/Plug-Ins/VST3`). A missing one is shown as missing (its synth's button greyed), the rest work.

## Build
`cmake -S ghost -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target Ghost_VST3` (Mac: universal, macOS 11+).
Checks (Linux): `ghost-tests` (every factory sound becomes a valid state), `ghost-host-test` (with Linux builds of ArpEZator /
Ghosty Beats in `GHOST_VST3_DIR`: notes reach the lanes, FX line up, a song saves and comes back, the window draws).

## Licences
GHOST's own code: GPL-3 (it carries WarpCore, GPL-3). `libs/ghostblock-spectral`: Spectral Corruptor (MPL-2.0) and WarpCore
(GPL-3), unmodified (see its README). `libs/clap` (MIT), `libs/sst-cpputils` (MIT), `libs/simde` (MIT). JUCE 8 (AGPL / commercial).
