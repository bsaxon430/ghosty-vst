# What Ghost Atlas is built on, and why

The brief asked for an instrument built on proven open-source audio code rather than new basic DSP, and asked for
three projects to be looked at first. This is what was found and what was chosen.

## What plays the samples: sfizz

Every layer is played by its own [sfizz](https://sfz.tools/sfizz/) engine (version 1.2.3, BSD-2-Clause), used as a
library and not modified. sfizz reads SFZ instruments as their authors wrote them (velocity layers, round robins,
loops, release samples, key switches, filters and envelopes), so the importer's job is to turn everything else
(SoundFonts, Decent Sampler packs, loose folders of recordings, single-cycle waveforms) into SFZ and leave the
recordings alone. Ghost Atlas's own layer controls reach into each instrument through SFZ's standard controller
opcodes, written into a copy of the instrument's SFZ text when a layer loads (`src/sfz/SfzLayer.cpp`); nothing is
resampled or rewritten on disk.

The audio thread never loads, allocates, locks or waits: a loader thread builds instruments and effect units, and they
are swapped in under a 12 ms fade (`src/engine/Engine.cpp`).

There is one exception, and it is deliberate: an export. sfizz keeps the first 0.17 s of each recording in memory and
reads the rest in behind a note as it plays, which is fine in real time. A host that renders faster than real time
can outrun the disk, and the note then stops where the part in memory ends (with the checks rendering as fast as they
can, 86 of 400 notes did). So when the host says it is rendering offline, sfizz is put in its own mode for that, each
note waits until sfizz reports its recordings read, and the engine finishes any loading before the first block. sfizz
itself is not changed; only waiting is added. The host has to say that it is rendering: in FL Studio that is the
wrapper's "Notify about rendering mode" option (Processing tab), which
[tells the plug-in when FL Studio is rendering](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/wrapper.htm).

### sfizz is no longer maintained: what was looked at, and what was done (November 2026)

The sfizz repository was archived by its authors on 21 June 2026, at release 1.2.3. Its `develop` branch holds 39
commits that were never released, among them a fix for a note that is never let go (when its end falls on the same
sample as its start, anywhere but on a block's first sample), a use-after-free in the modulation matrix, and faster
note-ons.

One maintained continuation was found: [dusk-fizz](https://github.com/dusk-audio/dusk-fizz) (BSD-2-Clause), which is
the 1.2.3 release plus 19 commits, last changed on 19 October 2026. They are fixes for building and running on more
machines (ARM, newer compilers, Windows paths, the macOS deployment target), a loop-wrap fix, a bounds fix in one
filter, and safer shutdown of the file pool. It does not contain the unreleased fixes from `develop`.

It was built in place of sfizz and compared: every check passes against it, and of 48 renders of real instruments 46
are identical to the last bit; the other two (a saxophone with random round robins) differ by less than that sound
differs from itself between two runs.

So nothing is gained in sound and nothing is lost, and the choice is about what can change underneath the instrument.
Ghost Atlas stays on sfizz 1.2.3: an archived release can never change, and none of dusk-fizz's fixes touches
anything Ghost Atlas runs into on this Mac. dusk-fizz is kept as a proven second choice, one switch away
(`-DGHOSTATLAS_SFIZZ=dusk`, pinned to commit `abd338d`), for the day a compiler or a machine needs it.

The one fault of 1.2.3 that Ghost Atlas can meet, the note that is never let go, is guarded in Ghost Atlas's own
bridge and not in sfizz: a note-off that arrives on the very sample of its note-on is handed over one sample earlier,
which sfizz treats as a note that never sounded (`SfzLayer::noteOff`). A check plays such a note and listens for it
three seconds later.

### Layers that speak late

Each layer is its own sfizz engine followed by its own two effects, so a layer whose effect needs time to work
(VOICE SHIFT works on a window of sound: 120 ms, or 60 in its tight mode) comes out that much later than the others.
The engine adds up what each layer in use is held back by, and while the host's timeline runs or it exports, delays
every other layer to meet the slowest (a short delay line per layer, crossfaded over 30 ms when the amount changes)
and tells the host of that much more latency, so the host sends everything early by the same amount. With the host
stopped nothing is delayed: a key played by hand answers at once and only the slow layer trails, which is what a
player wants (`Engine::setOnTimeline`).

## The three projects in the brief

- **Quetzalcoatl** (built on HISE): not used. Building it needs the full Xcode, which this Mac does not have (the
  command-line tools only); its SFZ import drops settings it does not know rather than reporting them, which the brief
  rules out; and its own sample content carries no stated licence, so it could not go into a manifest.
- **HISE** itself: a framework for building one fixed instrument from a project file, with the same build requirement.
  Ghost Atlas needs to load any library the user drops on it, at run time.
- **Shortcircuit XT** (Surge Synth Team, GPL-3.0): a complete sampler plug-in, in public beta since January 2026. It is
  an application rather than an engine to build inside another instrument, and it is still changing quickly. It is a
  good second sampler to have beside Ghost Atlas, not a base for it.

## The rest

- **Effects**: the 24 units of Phantom FX (this repository's own effect rack, AGPL-3.0), on every layer (two inserts),
  on three sends and on the master. They include what the brief's voice engine asks for beyond pitch and formant:
  grains (CRYSTAL), freezing (SHIMMER, SPECTRA, DOORWAY), a talking vowel filter (FORMANT) and tempo-synced gating and
  stutter (GATE, GRID, HAUNT).
  The units are Phantom FX's own code, unchanged. Three of them are given different starting values here, because
  there each is opened on a page of its own (a pattern to draw, pads to hold, bands to switch on) and here it arrives
  in a slot with knobs only: the gate starts on a pattern that cuts (its first stands open), the stutter with a pad
  held, and the equaliser as five of its twenty-four bands, switched on and flat (`atlasDefaults` in
  `src/engine/Engine.cpp`). A sound that sets a value of its own keeps it.
- **Spectral effects**: SPECTRAL FX, the effect rack of Poltergeist 2's FX page, as a 26th unit. Its sound is two
  open-source projects, compiled as their authors wrote them: [Spectral Corruptor](https://github.com/Dirty-Octopus/spectral-corruptor-plugin)
  v0.2.6 (Dirty Octopus, MPL-2.0; its 38 effects on its own STFT engine) and [WarpCore](https://github.com/ManasWolrd/WarpCore)
  v0.0.12 (ManasWolrd, GPL-3.0; its spectral inversion). Round them is Poltergeist 2's own rack object, also unmodified
  (`third_party/spectral/rack/`): it builds effects off the audio thread, swaps them in between blocks with a fade, and
  holds the dry side back by the window. Ours is `src/engine/SpectralRack.*`, which stands between that object and a
  Phantom FX slot, so the rack sits wherever any other unit can: a layer, a send, the master. The engine tells it of
  each control from the message thread, where it builds at once; the loader thread looks in on every rack between its
  jobs; and a slot is asked for its latency again only when the rack's window comes into use, goes out of use or
  changes size. `NOTICE.md` says what was taken from each project and under what terms.
- **Pitch and formant shifting**: VOICE SHIFT, a thin unit around
  [Signalsmith Stretch](https://signalsmith-audio.co.uk/code/stretch/) 1.3.2 (MIT), through its documented calls
  (`setTransposeSemitones`, `setFormantSemitones`).
- **Analysis for MUTATE**: [Samplebrain](https://gitlab.com/then-try-this/samplebrain) (Then Try This,
  GPL-2.0-or-later): its block, FFT, MFCC and distance, exactly as written, for "how alike do these two sound".
  What makes two sounds work together is a separate judgement, in `src/core/Mutate.cpp`.
- **REIMAGINE**: Samplebrain's brain and renderer, again as written, run offline note by note (`src/core/Reimagine.cpp`).
- **SoundFonts**: [TinySoundFont](https://github.com/schellingb/TinySoundFont) (MIT) reads them; the importer writes SFZ.

- **The picture of the sound** on the Play page: JUCE's own FFT (`juce::dsp::FFT`), read at the pitch of each key, so
  the light stands over the notes that are sounding. The engine hands the screen its last 4096 samples through a
  lock-free tap; nothing on the audio thread waits for the screen.
- **The worlds' film**: no video library. `tools/make_film.py` takes a run of about thirty frames for each world from
  the owner's recording (starting at the frame its painting was made from), enlarges them the way the paintings were,
  and packs them as JPEGs in one `.gafilm` file a world. The plug-in decodes them with JUCE's JPEG reader on a thread
  of its own, blends neighbouring frames, and plays the run slowly out and back so that it always returns to the
  painting. It asks for a new picture at most fifteen times a second, and less often when the window is slow to draw.

Everything fetched at build time is pinned to a version or a commit; licences are in `NOTICE.md`.
