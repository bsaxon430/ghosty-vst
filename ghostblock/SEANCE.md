# SÉANCE (GhostBlock) - build brief

Working name SÉANCE (files / ids: "Seance"). A new plugin; Poltergeist 2 is not touched (this branch forked from
`poltergeist-2` build 16 so it starts with that engine, MORPH, SPECTRAL FX, SUB and the build / check / install setup).

## What it is
Twelve spirits sit round a table. Each spirit is an operator. Any spirit can possess (audio-rate modulate) another.
Each seat picks its SPECIES = which real sound engine makes its tone:
- SINE  - Six Sines' operator (baconpaul/six-sines, MIT, the code already in this tree)
- WAVE  - Vital's wavetable oscillator with its spectral warps and unison (mtytel/vital, GPL-3, real code)
- BEAST - Surge XT's oscillators: Classic, Modern, String, Twist, Window, Alias, S&H (surge-synthesizer/surge, GPL-3, real code)
Both Vital's oscillator (its `kPhase` input) and Surge's oscillators (`assign_fm` + FM depth) take audio-rate
modulation in upstream's own code, so cross-species possession uses the real engines, not rewrites.

## The screen (never a grid of knobs)
- THE TABLE: a round table seen from above, twelve seats on the rim, a spirit in each. Drag from one spirit to another =
  a thread of ectoplasm across the table = that link. Thickness = depth; the thread shakes with the real signal.
  Pull a spirit toward the middle = louder in YOU HEAR (the planchette in the centre is the output).
  A spirit curling round on itself = feedback.
- Two HANDS: seats 1-6 = LEFT HAND, 7-12 = RIGHT HAND, each with its own level / octave / mute, so one hand can be the
  bass and the other the movement.
- Click a spirit = its page (species, ratio, wave, envelope, LFO). Anything that does nothing for that species is greyed.
- Owner rules that apply: open at 1440 x 900 without overlap, no upstream names in the visible UI, grey out dead controls,
  install on his Mac when a build passes.

## Build order
1. DONE (this commit): the engine runs twelve operators - 66 forward links, 12 feedbacks. Link ids 1-15 are upstream's,
   so every Six Sines / Poltergeist 2 patch still loads into seats 1-6. Proof: `ghostblock/headless` (engine-only build,
   `engine-checks`: every operator in tune, all 66 links and 12 feedbacks act, worst case stays finite).
2. DONE (build 1): its own names and ids (product "Seance", CLAP id io.ghostblock.seance, AU subtype Senc, user folder
   Documents/GhostBlock/Seance, workflow `seance-mac.yml`, installs Seance.vst3 + Seance.clap), THE TABLE editor
   (`src/ghostblock/seance/seance-editor.*`, drawn by code, 1180 x 760 fixed), checks (`seance-tests`: shots + audio).
   Poltergeist 2's editors (p2-editor, machine/) and the upstream ui/ are still in the tree but not compiled.
   MORPH / SPECTRAL FX / SUB are compiled into the plugin and off (no pages yet): off = bit-identical.
   Not done from the screen idea above: the two HANDS, pulling a spirit toward the middle for volume (scroll on it or
   use VOLUME instead), a spirit page for LFOs / macros / modulation slots.
   Linux can build and run everything except the Mac bundle: `cmake -S . -B b -G Ninja -DCOPY_AFTER_BUILD=FALSE`,
   target `seance-tests`, run under `xvfb-run` (needs libx11 / freetype / alsa / xrandr / xinerama / xcursor dev packages).
3. WAVE species. Two routes, in this order:
   a. Six Sines' own operator already has a wavetable mode upstream (`src/dsp/wavetable*.h`, `wavetable_io`,
      `WavetableHandoff`, the `wavetableMorph` / band-limit parameters). The table editor does not expose it yet:
      a wavetable picker + MORPH knob on the spirit panel gives a real wavetable spirit with no new engine code.
   b. Vital's oscillator (spectral warps, unison) inside a seat. Biggest risk: Vital runs four voices per SIMD lane in
      its own processor graph; it has to be driven per seat without changing its sound.
4. DONE (build 3): BEAST species = Surge XT's own oscillators in a seat. `src/ghostblock/beast/`:
   - `fetch-surge.sh` gets Surge at a pinned commit with only the libraries its engine needs; `build-beast.sh` builds it
     the way Surge XT Rack does (no JUCE, no Lua, block size 8) as the top-level project, with `beast.cpp` injected
     (`-DCMAKE_PROJECT_INCLUDE`), runs `beast-check`, then links everything into ONE object in which only `beast_*`
     is visible (`ld -r` + an exported-symbols list on the Mac; `ld -r --force-group-allocation` + `objcopy` on Linux,
     compiled with `-fno-gnu-unique`). So Surge's copies of fmt / sst-* never meet this tree's copies.
   - Give that object to CMake with `-DSEANCE_BEAST_OBJ=...`; without it `beast-stub.cpp` is built (no beasts).
   - Patch: per operator `species` (id +200; 0 = sine operator, 1-8 = Classic, Modern, String, Twist, Alias, Noise,
     FM2, FM3), `beastP[7]` (+201..207, 0..1 = Surge's own parameter range), `beastSet` (+208: 0 = type defaults).
   - Engine: `OpSource::beastBlock` (op_source.h) replaces the sine inner loop: same pitch (ratio, env / LFO to ratio,
     unison), the seat's phase-modulation input goes to Surge's FM input (`assign_fm`, depth `beastFmDepth` = 4),
     feedback = its own previous block into the same input, ring-mod on the output. Surge makes 16 samples a call:
     engine block 8 = a call every second block. `Synth::process` copies type + knobs into the `BeastWorld` each block.
     One `BeastWorld` per plugin (two SurgeStorages = twelve oscillator setups); the editor has its own for names.
   - Not yet: Wavetable / Window oscillators (they need wavetables loaded), stereo, Surge's filters.
   - CPU here: twelve beasts x 8 voices = about half a core (Surge runs at the engine's oversampled rate).
   SHADE (build 5) = Vital's oscillator in a seat (species 9, "Wavetable"). `libs/vital-synthesis` is Vital's code at
   636ca0e, unedited; `src/ghostblock/shade/` is the door (`shade.h` C functions, `shade.cpp`, `shim/JuceHeader.h`
   so it builds without JUCE, `shade.cmake` = static lib `seance-shade`, Vital's folders as -iquote paths only).
   - Wired like Vital's own OscillatorModule. Transpose / tune / level / PHASE are audio-rate buffers; the seat's
     phase modulation (links + feedback) goes into PHASE in cycles, same block, no delay.
   - The seven BODY knob slots (`beastP`) are reused: TABLE, FRAME, MORPH TYPE, MORPH, WARP TYPE, WARP, UNISON.
     Unison detune and the other Vital controls sit at Vital's defaults (no knob yet).
   - Tables are generated in shade.cpp (8 x 16 frames). A table change reloads the seat's Wavetable on the audio
     thread (may click). User .wav wavetables: not yet.
   - Left out: Shepard Tone (needs its own table) and Inharmonic Stretch (output ran away to thousands x full scale
     in this standalone setup, cause not found). The door clamps output at +-6 and drops non-finite samples.
   - FFT inside Vital: kissfft on Linux, Accelerate on a Mac (Vital's own #if). The Mac path is only exercised by
     the "a shade plays in tune and in range on this machine" check in seance-tests.
   - Cost here: twelve shades x 8 voices = about 1.6 cores (one instance per voice, all four SIMD lanes run, at the
     engine's oversampled rate). To cut: pair two voices per instance, or run shades at a lower rate.
   SHADE table files (build 6): `shade_slot_load_vitaltable` builds a seat's table from a .vitaltable's JSON text
   with Vital's own `WavetableCreator` (libs/vital-synthesis/src/common/wavetable, unedited; the shim JuceHeader.h
   supplies String / Base64 / MemoryOutputStream / ProjectInfo, shim/load_save.h three helpers; `-include
   line_generator.h` because wave_line_source.h relies on it being seen first). Main thread only, as in Vital.
   - `Patch::shadeTables[seat] = {name, json, tag}` holds the file's text; streamed as <shadeTables> (deflate +
     base64), copied by `copyValuesFrom`, cleared on init. `reconcileShadeTables` (called at the top of
     `reconcileWavetables`, so by the engine and by the editor) makes the engine match the patch;
     `WavetableHandoff::shadeWorld` is how the main thread reaches the engine's shades.
   - A slot's table is written by one thread at a time (`busy` flag): the main thread for files, the audio thread
     for built-in tables (it skips a block if the main thread holds it).
   - The editor's TABLE plaque scans Music/Vital for *.vitaltable when clicked. Nothing of Vital's is in the repo
     or the plugin; a saved sound carries the text of the file it used, so do not publish such sounds.
   - Checks: engine-checks renders every file under $SEANCE_VITAL_TABLES when set; the built-in check uses a table
     written in the check itself.
   MOVE (build 8): `Patch::ThroatNode` also holds envToCutoff / lfoToCutoff (semitones) and bodyTarget (0 FRAME,
   1 MORPH, 2 WARP) + envToBody / lfoToBody (-1 .. 1). The modulators are the operator's own source envelope and
   LFO (`OpSource::moveEnv / moveLfo`, taken each block where upstream uses them for pitch); `checkLfoUsed` counts
   MOVE so the LFO runs. Cutoff: added in `Voice::renderBlock`. Body: `shade_osc_render(.., move3, ..)` - each
   ShadeOsc has its own frame / morph / warp values (the seat's knob + this voice's movement). Beasts' knobs do not
   move yet. Editor: sixth tab on the spirit panel (`MK`, `moveTarget`).
   THROAT (build 4): Surge's other strong point, its filters. `src/ghostblock/seance/throat.h` wraps sst-filters'
   filters++ (already in libs/, GPL-3, unchanged): a catalog of every model + configuration without a delay line
   (214; the combs are left out), `Patch::ThroatNode` per seat (ids 200000 + 20 * seat: kind, cutoff in semitones from
   440 Hz, resonance, keytrack), run in `Voice::renderBlock` right after the operator renders, so links and the mixer
   read the filtered signal. kind 0 = bit-identical. The catalog order is the library's: keep sst-filters pinned.
5. DONE (build 2): backward links. `matrixSize` is 132: 0-65 upstream's forward links, 66-131 their mirrors
   (`MatrixIndex`), applied before the target renders, so a mirror reads the modulator's previous engine block.
   "Which operators are in use" is a fixed point now (loops). Also in build 2: FX page (the existing spectral panel,
   restyled through a LookAndFeel) and SUB page, laid over the table (`PageBase` in seance-editor.cpp).
   The host sees ON and DEPTH only for links beyond upstream's fifteen (`paramsInfo`, CLAP_PARAM_IS_HIDDEN).
   DONE (build 9): .wav wavetables. `shade_wav_to_vitaltable` (shade.cpp) reads a RIFF wav (PCM 8/16/24/32, float32,
   first channel, cycle length 2048 or the file's "clm " chunk, any other length stretched to 2048, at most 257 cycles)
   and writes the text of a Vital "Wave Source" table, so the existing loader, saving and FRAME all apply unchanged.
   TABLE FILE > "Load a .wav wavetable..." (async FileChooser). FX page: `SeanceLook` (seance-editor.cpp) draws the
   spectral panel's JUCE sliders, tick boxes, buttons and menus like Seance's own controls.
   DONE (build 10): a sound carries its FX and SUB pages. `Patch::rackSpectral / rackSub` (their own state text,
   `<seanceRack>` in the sound file) are filled by the editor at SAVE (`writeSound`) and applied when any sound is
   loaded (`applyRack`); a sound without them (factory sounds, NEW) turns both off. The plugin state still keeps the
   live copies after the patch, and those win when a project opens.
   DONE (build 12): MOVE reaches Surge spirits. `bodyTarget` is 0-6 now: on a shade 0-2 (FRAME / MORPH / WARP, higher
   values clamp to WARP), on a beast the index of one of its seven knobs. `beast_osc_set_move` pushes that knob for one
   note inside the oscillator's local copy of the parameters (float knobs only: `beast_slot_param_movable`). The MOVES
   plaque lists the seat's own movable knob names (`Impl::moveName`, `Plaque::nameFor`). "Seance Twelve": ten sounds.
   DONE (build 12): CROSS page ("The crossroads", page 3) = the MORPH engine from Poltergeist 2 (`ghostblock/morph.h`,
   unchanged): four corners, each empty / the sound on the table / its own sound in its own engine instance; the flame
   (puck) mixes the real outputs in the CLAP wrapper (`processMorph`). `BlendPage` in seance-editor.cpp: corner pickers
   (own PresetManager, never null host - `sendEntirePatchToAudio` does nothing without one), VOLUME / PAN / OCTAVE per
   corner, ORBIT / EVERY / SIZE. Lives in the plugin state (project), not in a sound. Four engines = up to 4x the CPU.
   DONE (build 13): SUMMON (top bar). `src/ghostblock/seance/summon.h` `Summoner`: fills the patch by rule in one of
   six characters (bass, bell, pad, lead, keys, unholy), plays it on a scratch engine (every attack forced instant so a
   swell is judged at full level), trims the heard levels to a peak near 0.55, retries up to six times if silent /
   non-finite / still ringing 2 s after release. About 0.7 s per sound on the Linux box. Names from two word lists.
   DONE (build 14): FOLLOWS + MACROS, on the engine's own modulation slots (upstream `ModulationMixin`, slot 0 of each
   node, target DIRECT; pitch uses the source node's DIRECT_FINE). Spirit panel: 7th tab HANDS (`envMode` 6) with
   VOLUME / FEEDBACK / PITCH FOLLOWS + amounts; thread panel: DEPTH FOLLOWS + FOLLOW AMT; circle panel: MACROS toggle
   shows the six `macroNodes[i].level` knobs in the envelope's place. Sources offered: velocity, mod wheel (CC1),
   channel aftertouch, pitch bend, key, random per note, macro 1-6 (`followSources`). Slots 1 and 2 are untouched.
   Lighter shades: measured (SEANCE_BENCH=1 engine-checks): about 0.8-1.1 % of a core per voice whatever the block
   size, so there is no cheap win outside Vital's own code, which stays unedited. Dropped.
   DONE (build 15): SUMMON menu also has "Mutate it a little / a lot" (`Summoner::mutate`: depths, possessor ratios,
   wavetable positions, filter, feedback, pan, tails drift; a lot may add one thread; then `trim`) and "Bring back the
   sound before that" (`Impl::undoState` = the patch state incl. FX / SUB taken before each SUMMON / MUTATE; the item
   swaps). Summoned threads often follow velocity (modulation slot 0).
   NOT doing: Inharmonic Stretch + Shepard (Inharmonic ran away in the engine; adding menu entries would also shift
   the stored morph steps of saved sounds), comb filters (a delay line per voice per seat).
   DONE (build 16): STEPS page ("The rite", page 4) = a note sequencer in front of the engine.
   - `src/ghostblock/seance/steps.h` `StepSeq` (JUCE-free; audio thread `block()`): a pattern is a loop of notes at 96
     ticks a quarter, each an offset from the held key or (ladder patterns) an index into the held notes (+64 per
     octave lift). Host playing = position taken from the song position every buffer (eased, never double-fires);
     stopped = starts at the first key at host tempo. LATCH, SPEED (half / as written / double), LENGTH, SWING,
     OCTAVE, four lane switches. Pattern handoff = try-lock, no allocation in block().
   - Plugin (`six-sines-clap.cpp`): while STEPS is on (or still has notes to end) `buildStepEvents` rewrites the
     buffer's input events: played notes (host notes, MIDI note messages, the on-screen keyboard) go to the sequencer,
     its notes take their place, everything else passes through; `process_nonblocking` then runs unchanged, so SUB and
     CROSS hear the sequencer's notes. State: section "SEANCESTEPS 1" after SUB's. Not host-automatable.
   - `libs/arpezator-engine` = ArpEZator 3.2.2's own composing engine, unchanged (branch arpezator-mutant @ ef6b8e3),
     static lib `seance-arpez`; its `recipe.json` is embedded (cmrc `arpez/recipe.json`) and loaded with its own
     `RecipeJson.h`. `step-patterns.h`: `arpezIdea` (newIdea for MIDNIGHT / DUSK and a seed -> chords, arp, melody,
     bass as offsets from the key's tonic at or above MIDI 55; ARP + MELODY on by default), `arpezFigure` (the idea's
     arp Figure laid over the held notes: pedal = lowest held note, ArpEZator's velocities / ties / re-sync), plus
     Seance's own 14 one-key lines and 14 held-chord arps (token strings in that file).
   - Page: pattern menu, ANOTHER (new seed; only for the composed kinds), roll (click = add a sixteenth on the ARP
     lane, click a note = remove, drag = pitch), playhead. A sound saved with STEPS stores it (`Patch::rackSteps`);
     a sound with none leaves the sequencer as it is.
   DONE (build 17):
   - STEPS grid: drag a note = move (time snaps to sixteenths), drag its right end = length, wheel = loudness,
     right-click a note (loudness presets, octave, which part, remove) or a bar (copy onto next / every bar, clear).
     DRAW ON (the part new notes join), LOOP (1-16 bars, or repeat what is there). Part switches wake for an idea or
     when notes sit on more than one part.
   - DRAG MIDI OUT (`DragTile`, `StepsPage::midiBytes` via ArpEZator's `arp::writeMidi`): the pattern as it plays
     (parts on, LENGTH, SWING, SPEED, OCTAVE) from C4, ladder patterns over C minor; temp file, external drag.
   - STEPS menu: SUNRISE (idea + figure), TRAP and HIP HOP (ideas; `setNewIdeaMelVer(kMelVerCurrent)`, the idea's
     GROOVE applied as ArpEZator's own export does). Kinds in `step-builtins.h` (no ArpEZator / JUCE: the sound tool
     uses it); `step-patterns.h` keeps the ArpEZator part.
   - SUMMON: `Summoner::pickTable` (editor: a random .vitaltable from ~/Music/Vital, toggle in the menu) for
     wavetable spirits; `mutate(P, amount, what)` with What bits (DEPTHS TUNING TABLES FILTERS SHAPES) = "Change
     only..."; `revoice` = "New voices".
   - FOLLOWS second slot (indices 4-7 = the same four on modulation slot 1; plaques "AND < ...").
   - Macro names: `Patch::macroNames` (upstream) shown on the knobs and in FOLLOWS; NAME IT under each macro knob.
   - Factory folder "Seance Played" (10): velocity / mod wheel / random / macros, five with STEPS on.
   DONE (build 18):
   - `out-guard.h` `OutGuard`: meter + safety limiter, the last thing in `process()` (after SUB). Instant attack,
     0.12 s release, ceiling 0.97, bit-exact below it; non-finite samples become 0. On by default; its switch is saved
     as a "LIMITER n" line at the end of the SEANCESTEPS state section. Editor: `OutMeter` under VOICES (click = on/off).
   - `host-params.h` `HostParams`: ids 920001-920007 after SUB's = Steps On / Speed / Length / Swing / Octave / Latch,
     Output Limiter. Screen changes call `SeanceEditor::tellHost` -> values rescan on the main thread. CROSS (the
     Poltergeist 2 morph params On / X / Y / Orbit / Rate / Size) was already automatable.
   - CHORDS: YOURS (STEPS ideas): `arpezIdea(..., followChords)` files each note as chord tone j of the chord under
     it in ArpEZator's progression + octaves (`ladderOff`, may be negative) + a nudge in semitones (StepNote lane byte,
     high 4 bits); `StepSeq::pitchOf` plays tone j of the held chord. Rows in the roll: 4 per octave.
   - A note's "Set its loudness and length exactly..." (right-click).
   - SUMMON > "Summon around this sound..." (`Summoner::summonKeeping`, Keep bits WIRING / VOICES / SHAPES).
   - Sound list: FIND A SOUND..., FAVOURITES (file `Documents/GhostBlock/Seance/favourites.txt`, one key per line).
   - THROAT: the seven comb filters, appended AFTER the 214 older catalog entries (saved indices unchanged). Each
     Throat owns one delay line (heap, made with the voice); lanes 1-3 of the library's quad filter share one scratch
     line. Combs with a mix setting get extra = 0.5.
   DONE (build 19): a new instance opens on the factory sound Bass / "A Supporting Role" (owner's choice):
   `SixSinesClap::openingSound()` in the plugin's constructor reads it from the embedded factory sounds and sets both
   copies of the patch before any audio runs. A project's saved state still replaces it on load; NEW still gives INIT.
   DONE (build 20): CROSSROADS menu on the CROSS page. `crossroads.h`: 12 ready-made crossroads (four factory sounds +
   per-corner volume / pan / octave, flame place, orbit, rate, size). Volume trims measured with the new
   `seance-sounds levels <factory_patches> <Folder/File>...` (held C3 chord RMS, plus the register one note sits in) to
   bring each corner near -20 dB; the test re-measures and requires every recipe within 6 dB (all are within 3.1).
   "Save this crossroads..." writes `Morph::toState()` byte for byte to Documents/GhostBlock/Seance/Crossroads/*.crossroads
   (replaceWithText would turn \n into \r\n and break its byte counts); YOURS lists them.
   DONE (build 21): the crossroads' phase guard (`blend-guard.h`, mixing in `processMorph`). The mix was already equal
   power (sqrt of the flame's shares), so the thin sound is real cancelling between correlated corners. Every buffer:
   each mix slot's heard signal against the rest of the blend (energy, cross term, ~150 ms memory). With FIX
   CANCELLING on (default; "PHASEFIX n" line in the SEANCESTEPS state section) the part most opposed to the rest
   (correlation < -0.3) is turned over with a 30 ms fade, one at a time, then 0.4 s rest; signs reset when it is off
   or the part leaves. A level follower keeps the blend at the power of its parts side by side (gain 0.71-2.0, ramped
   per buffer). CROSS page: FIX CANCELLING switch and a readout (CLEAN / CANCELLING x dB / what was turned over).
   Test: a sine against the same sine half a cycle late cancels to -60 dB plain; with the guard it plays at the
   level of one corner.
6. Factory sounds that use all twelve seats. MORPH page. A spirit page for LFOs / macros / modulation slots.
   10,579 parameters in the patch (about 3,900 listed to the host): watch FL Studio's load time.

## Licence
With Vital or Surge code inside, the plugin is GPL-3: it can be given away with its source, not sold closed.
Vital's factory presets and wavetables are not free to redistribute and are not included.
