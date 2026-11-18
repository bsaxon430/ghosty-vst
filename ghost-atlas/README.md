# Ghost Atlas

A sample-based instrument with twelve layers, a preset browser that comes first, and a MUTATE button that makes a new
version of whatever is loaded. VST3, CLAP and a standalone app for the Mac. A personal instrument: it is not sold or
passed on.

Load a sound, play a note. Everything else is optional.

## The screen

Four pages over eight painted worlds (each kind of sound lives in one of them, and the picture follows the sound):

- **Play**: search, the eight worlds, favourites, recents, your own sounds; the sound's layers as lanterns (drag one
  to change its level, double-click to mute it); four macros; master level; a keyboard.
  The sound is seen as well as heard: light stands on the water above the keyboard, a note's own light over its key
  and its overtones to the right of it; every note drops a ring; a key that is down is lit, and a layer's lantern
  sends a ring out when the layer starts to sound. Keys no layer plays are dimmed.
  The button at the top right of the picture says how much the world moves: **Film** (the world's own moving
  picture, from the recording the paintings came from), **Alive** (the painting with its weather and mist) or
  **Still**. Film paces itself by how long the window takes to draw, so a slow computer gets a slower film, not a
  slow plug-in.
  The worlds are the places alone: the boat and its boatman that the recording follows have been painted out of
  every painting and every frame of the film (`tools/boat/`), and each picture is framed as it was with them in it.
- **Layers**: up to twelve layers. Eight slots are shown, as there always were, until the eighth is in use; then one
  more than the last in use, down to lower rows and smaller lanterns for all twelve. For each layer: instrument,
  level, pan, pitch, key and velocity range (with crossfades),
  attack and release, sample start, reverse, loop, filter, width, an LFO (level, pan, filter, pitch), three sends and
  two insert effects. Mute and solo.
- **Effects**: the two inserts of a layer (chosen on this page or on Layers), the three sends and the three master
  slots. Any of 26 units in any slot. Whatever cannot be heard says so and is greyed out, with the reason as its
  tooltip: a control that does nothing in the unit's present mode, a send no layer is sending to, an effect on a layer
  with no instrument. A send shows how much of each layer goes to it, as a row of bars that can be dragged, so a send
  can be fed without leaving the page. Every unit arrives doing something that is heard (the gate with a pattern that
  cuts, the stutter with a pad held); the equaliser arrives as five bands, flat, each with a gain and a frequency.
  One unit is a rack of its own: **SPECTRAL FX**, Poltergeist 2's FX page. A chain of four places, each taking one of
  Spectral Corruptor's 38 effects (in its eight folders) with that effect's own controls, then Warp Core's spectral
  inversion, and the window the chain works on (its size, shape and overlap). One part is shown at a time, with the
  spectrum going into the window and coming out of it drawn live underneath. An effect chosen for a place is heard at
  once, a little; in Poltergeist 2 the same effect starts at nothing and is turned up. Eleven factory sounds are made
  round the rack, at least one in every world: typing `spectral` in the Play page's search box lists them.
- **Library**: every instrument, what was heard in it and where it came from. Drop a folder anywhere on this page to
  add it.

The keyboard stays with the host, whose typing keys play notes: nothing on the screen takes it except a text box
that has been clicked into (and the name box of Save, which is asked for). Return, Escape or a click anywhere else
gives it back.

An effect that takes time to work (VOICE SHIFT: 60 or 120 ms; SPECTRAL FX with an effect that works on its window:
that window, 43 ms as it arrives, from 3 ms to a third of a second) makes its layer speak late. While the host's timeline
runs, or it exports, the other layers are held back to meet it and the host is told of that much latency, so what was
written lands together and on the beat. With the host stopped nothing is held back: keys played by hand answer at
once, and only that layer trails. On a send the sound itself is never held back: only what the send gives back comes
that much later, which is why the factory sounds that are struck and played by hand carry their rack on a send. On
the master the whole sound is that much late, and the host is told.

**MUTATE** (top right) mutates straight away in the mode last used:

- *Subtle*: one layer swapped for a close relative, or one added.
- *Evolve*: the main sound stays; everything around it is chosen again.
- *Wild*: a different main sound from far away, with layers chosen for it.
- *Reimagine* (experimental): the sound is rebuilt out of small pieces of other instruments and added to the library
  as a new instrument. About half a minute.

What comes back is played against the original at the same loudness. Keep it, try again, or put it back as it was.

## Where things are on the computer

    ~/Music/GhostBlock/Ghost Atlas/
        Library/        every sound library, one folder each, with ATLAS-SOURCE.json (where it came from, its licence)
        Library/reimagined/   instruments REIMAGINE made
        Presets/        your own sounds (.atlas)
        Film/           the eight worlds' moving pictures (one .gafilm each; made by tools/make_film.py)
        library.json    the list of instruments and what was heard in each
        Cache/          analysis that can be made again
        Reports/        the last import's report, the licence manifest as text, and screen.txt (how long the
                        window takes to draw and how fast the film runs, written while the window is open)

Libraries are found by paths relative to that folder. If a library you linked from elsewhere moves, the Library page
offers "Find its files".

## The sounds

- `library/manifest.json` lists the starter libraries: name, source, author, licence, restrictions, and the checksum
  or commit that was fetched. `library/fetch_library.py` downloads them (direct public downloads only).
- The importer (`src/core/Import.cpp`) reads SFZ instruments as they are, converts SoundFonts and Decent Sampler
  packs to SFZ beside the originals, maps loose folders of recordings from their file names (or by listening, when
  the names say nothing), and makes wave-synth instruments from chosen single-cycle waveforms. It never changes a
  recording. What it cannot use it reports, including SFZ settings the engine does not play.
  A library's record can also say which of a SoundFont's presets to list (`keep`), and that each of its recordings
  is an instrument of its own (`singles`: a drone, a texture, one sung chord, played from C2 to C6 and tuned by the
  note it is heard to be).
- Every instrument is then played and measured (`src/core/Analysis.cpp`): loudness, attack, how long it holds,
  brightness, tuning, width, movement, loop seams. Those numbers are what the picker's matched levels and MUTATE use.
- The factory sounds are written by hand in `tools/make_presets.py` and made ready by
  `GhostAtlasTests polish`: each layer's level comes from its instrument's measured loudness and the part it plays,
  pitched layers are brought onto their key, and every sound is played through the engine, checked, and brought to
  one loudness. Sounds that fail a check do not ship. `presets/factory-report.tsv` has the measurements. A later batch
  is polished on its own and set in after the rest by `tools/add_factory.py`, so the sounds already there are not
  played again. A sound's SPECTRAL FX is written with upstream's own names for its effects and controls, in their own
  units: `presets/spectral-catalogue.json` (from `GhostAtlasTests spectral list`) says how each is kept. A rack is held
  to its own ring: the listening forgives a sound the ring its instruments have by themselves, and for a factory sound
  that is measured with every rack switched off, so a rack that leaves a whistle does not ship.

- The fourth voyage (0.1.13), ten sounds in the manner of the big commercial ROMplers, all tagged `edm`: a supersaw
  lead, a trance pluck, a cinematic pad, an FM electric piano with a real piano under it, FM bells, an analog bass, an
  evolving atmosphere, an orchestral hybrid, a digital texture and a trance-gated stack. Most of their layers are Surge XT
  factory patches played offline into multisampled instruments (`surge-xt-renders`, made once on the Mac); Meta Open
  Sound's piano and guitars (CC0) and the Stargate sample pack's soundscapes and noise (CC0) came in with them, and VSCO 2
  now brings its brass, woodwinds and tuned percussion too. They are polished on the Mac itself, against its library:
  while `presets/POLISH-NEW` is in the tree the Mac build polishes `presets/factory-drafts-new.json`, sets the sounds that
  pass into the bank, builds the plug-in again with them and renders a chord, a line and an arpeggio of each (published
  to the `ghostatlas-polish` branch).

- The fifth voyage (0.1.14), ten more tagged `edm`, made from a second set of synth renders (`synth-renders-2`: 73
  factory patches of OB-Xd 2, Dexed, Vaporizer2 and Odin 2, hosted with DawDreamer and played into multisampled
  instruments by `tools/factory/render_plugins.py`; the lists it was made from are in `tools/factory/specs/`) and Caveman
  Cosmonaut's own presets. `presets/factory-drafts-new.json` now holds every sound the bank does not hold yet, so a sound
  that did not pass is tried again with the next batch.

## How MUTATE chooses

1. **How alike** two instruments sound is measured with Samplebrain's own analysis and distance. That only decides how
   far from the present sound to look.
2. **Whether they work together** is a separate question: the part a layer would play (a second body, an edge on each
   note, slow air behind, light on top), where its energy lies against what is already there, its register, its
   tuning, its width. Two to four layers that fit are preferred to many that resemble.
3. Each chosen layer is **set up for its part**: level from measured loudness, tuned to agree with the main sound,
   filtered out of the others' way, with the envelope, width and sends the part calls for.
4. The result is **played and listened to** (low, middle and high notes, soft and hard, a chord), with whatever
   stands on its master except the limiter and the squeeze: the master level is set in front of those two, by what
   goes into them. What can be put right is (a buried main sound, piled-up low end, a ring, width that cancels in
   mono, level); what cannot rejects the candidate and another is tried.
5. The survivor is brought to the **loudness of the original**, so the comparison is fair.

`docs/MUTATE-EXPERIMENT.md` measures it against layering at random. In short: its sounds come out clearly cleaner
than random layering (all 40 pass the nine checks against 61 %), and that is owed to steps 3 and 4. With the layers
drawn blind and then given the same setting-up and listening, the results measured the same, so step 2 is not yet
shown to help.

## Building and checking

The Mac build runs on the owner's own Mac (`.github/workflows/ghostatlas-mac.yml`, branch `ghost-atlas`): build, the
checks, pluginval at strictness 10, the starter library (fetched once, then listened to), a check that every factory
sound finds its instruments, and install (the old copy is backed up first).

    cmake -S phantom-fx -B build -DPHANTOM_WITH_GHOST_ATLAS=ON -DPHANTOM_BUILD_TESTS=ON
    cmake --build build --target GhostAtlasTests GhostAtlas_VST3
    GhostAtlasTests                     the checks (in a throw-away home); on Linux through tools/checks-headless.sh,
                                        which gives a headless display what a real window needs
    GhostAtlasTests index               list and listen to everything under Library/
    GhostAtlasTests import <folder>     add a folder
    GhostAtlasTests factory             does every factory sound find its instruments here?
    GhostAtlasTests mutate <sound> <subtle|evolve|wild> [seed]
    GhostAtlasTests reimagine <sound> [seed]
    GhostAtlasTests experiment <folder> [count]     MUTATE against random layering
    GhostAtlasTests polish <drafts.json> <out.json> [report.tsv]
    GhostAtlasTests shots <folder> [width]          pictures of every page
    GhostAtlasTests film <folder> [world] [seconds] [pictures a second] [width] [play] [still|alive|film] [sound=<name>]
                                                    the Play page in motion, as a run of pictures (with play: and its sound)
    GhostAtlasTests stress [sound ...] [seconds]    how hard a sound is to play: fast notes over a held chord, in real
                                                    time, at 44.1 / 48 / 96 kHz in blocks of 64 / 128 / 256
    GhostAtlasTests fxaudit [sound]                 how much each effect is heard on a factory sound: every send off,
                                                    every master unit on, every macro up, every unit on the first layer
    GhostAtlasTests spectral [sound]                how much each of the rack's 38 effects is heard, as it arrives
    GhostAtlasTests spectral list <file.json>       the rack's effects and their controls, for tools/make_presets.py

`-DGHOSTATLAS_SFIZZ=dusk` builds against dusk-fizz (sfizz 1.2.3 with portability fixes) in place of sfizz 1.2.3; see
`docs/FOUNDATION.md`.

    python3 tools/make_film.py extract|enlarge|pack ...     makes the worlds' film from the recording (see the script)

## What is where in the source

    src/sfz/       the bridge to sfizz: one engine per layer, layer controls written in as standard SFZ opcodes
    src/engine/    the twelve layers, sends, master chain, loader thread, click-free swaps, VOICE SHIFT, SPECTRAL FX
    src/core/      library, importer, analysis, timbre (Samplebrain), presets, MUTATE, REIMAGINE
    src/ui/        the stage (worlds, weather, film, glass), widgets, the four pages, the overlays
    film/          the worlds' film as it is installed (one .gafilm a world)
    src/plugin/    the processor (host parameters, state) and the editor
    third_party/   Samplebrain (as upstream wrote it), TinySoundFont, spectral/ (Spectral Corruptor, WarpCore and
                   Poltergeist 2's rack round them, all unmodified), licence texts
    docs/          FOUNDATION.md (what it is built on and why), OFFLINE-VOICE-TOOLS.md (RAVE and DiffSinger),
                   MUTATE-EXPERIMENT.md

What it is made of and under what licences: `NOTICE.md`.
