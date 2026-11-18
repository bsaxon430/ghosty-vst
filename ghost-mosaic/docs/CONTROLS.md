# Ghost Mosaic: every Samplebrain control, and where it is now

Ghost Mosaic runs Samplebrain's own engine (`samplebrain/brain/src`, unmodified apart from
one thread-safety patch in `block.cpp`, see NOTICE.md), so every setting below reaches the
engine with the same value and the same scaling the Qt app sent it (`app/MainWindow.h`).
The null tests (`tests/MosaicTests.cpp`) prove the sound is bit-identical.

Pages: **PLAY** (the main screen) and **UNDER THE HOOD** (everything). On screen the
original's name is printed small under ours (with "brain" written as "library").

## Opening it, and the status line (build 14)

- A new Ghost Mosaic opens **ready to play**: a few built-in sounds (synthesised drums) and the built-in
  target (sung vowels) are already loaded. Press GENERATE (or PLAY) to hear them; drop your own to replace them.
  Only the starting sounds are ours: the settings and the sound are Samplebrain's own. A saved session replaces all of it.
- The **status line** (top of the screen) always says what you hear, and why not when it's silent:
  "Building library... 40% - about 25 s left, then it plays", "Playing", "Silent: no target loaded",
  "Silent: PLAY BY is DAW play. Press play in your DAW", "Silent: PLAY BY is MIDI keys. Hold a note to play".
  While the library builds, THE LIVE VIEW shows a big progress bar with the time left.
- **Long sounds:** every piece of the library is compared with every other piece, so a long song takes much
  longer to build (4 times as long for twice the length). When you drop one, a note says how long it will take
  on your computer. Tip: shorter sounds build faster. The build runs in the background; the screen and the audio
  keep going, and GENERATE pressed during it waits for that build (it doesn't start a second one) and then plays.
- **GENERATE with no target** uses the built-in target, and says so.

Section and control names follow the manual (`docs/manual.md`) and `gui/samplebrain.ui`.

## UNDER THE HOOD is the original's window (build 16)

UNDER THE HOOD is the original Samplebrain window rebuilt in the GhostBlock colours: the same two tabs
(**search | net**), the same three columns in the same order, the same lowercase labels (the original's own
spelling, "num synpases" included; our plain-words name is printed small under each one), and the same bottom row.
The only difference on purpose: the menus list the original's own item names (dodgy, bartlett ..., basic, reversed ...), same order,
same values. Our additions sit below in their own strip, **GHOST MOSAIC (added)**, so the original part looks untouched.
At the default window size (1180 x 800) the whole original part is on screen without scrolling; only the added strip scrolls.

Every control here is the SAME control as the one on PLAY / in the host (one parameter, one engine call; nothing is
duplicated), and the MOVING NOW markers (build 15) still show on them while TERRAIN moves them.
Screens say "brain" here because the original does (Brandon, 2026-11-06 15:28); everywhere else it is the library.

| Original control (Qt name) | Where it is now (UNDER THE HOOD) | Engine call |
|---|---|---|
| tab "search" / "net" (tabWidget) | tabs `search` / `net` | the net tab is `OscBridge` (see net tab below) |
| column "brain tweaks": fft / mfcc | `fft / mfcc` slider | `ratio` > `search_params::m_ratio` |
| freq & dynamics / freq only | `freq & dynamics / freq only` slider | `n_ratio` |
| fft subsection Start / End | `fft subsection  Start [ ]  End [ ]` number boxes | `fft_start` / `fft_end` |
| novelty, boredom, stickyness | `novelty`, `boredom`, `stickyness` sliders | `novelty`, `boredom`, `stickyness` |
| search stretch | `search stretch` slider | `stretch` |
| algorithm | `algorithm` menu: basic, reversed, synaptic, synaptic slide | `algo` |
| num synpases (sic) | `num synpases` slider | `synapses` |
| synaptic slide error | `synaptic slide error` slider | `slide_error` |
| column "target sound": load target | `load target` button | `Engine::setTargetFile` |
| block size / block overlap / window shape | `block size` `block overlap` `window shape` (target) | `tgt_block`, `tgt_overlap`, `tgt_window` |
| (re)generate blocks | `(re)generate blocks` button | `Engine::generateTarget` |
| use mic input | `use mic input` switch (the FX version; greyed in the instrument, which has no input) | `live_input` |
| "mix": autotune, normalised, brain / target | the same names, sliders | `autotune`, `n_mix`, `target_mix` |
| stereo mode | `stereo mode` switch | `stereo` |
| column "brain contents": the sound list | the loaded-sounds list (switch + x per sound, drop files on it) | `setSoundEnabled` / `deleteSound` |
| all / none | `all` / `none` | `Engine::setAllEnabled` |
| load sound / directory / clear | `load sound` / `directory` / `clear` | `Engine::addFiles`, `clearSounds` |
| block size / block overlap / window shape | the same three (library) | `src_block`, `src_overlap`, `src_window` |
| (re)generate brain | `(re)generate brain` | `Engine::generateLibrary` |
| load brain / save brain | `load brain` / `save brain` | `Engine::loadLibraryFile` / `saveLibraryFile` |
| play (green) / pause (blue) | the round play / pause buttons | `Engine::play` / `pause` |
| record (red) / stop (black) | the round record / stop buttons (stop ends the recording and pauses, as the original's /stop) | `Engine::startRecording` / `stopRecording` + `pause` |
| the volume dial | the volume dial (same parameter as the VOLUME knob on PLAY) | `volume` |
| load session / save session | `load session` / `save session` | `Engine::loadSessionFile` / `saveSessionFile` |
| (ours) PLAY BY, KEYS CHANGE PITCH, DRY, SAFETY LIMITER, SOFT START/STOP, MOVEMENT, MOVEMENT RATE, AUTO BUILD, BACK TO START | strip "GHOST MOSAIC (added)" below | `play_mode`, `key_pitch`, `dry`, `limiter`, `declick`, `move_amount`, `move_rate`, `auto_build`, `Engine::restart` |

Fresh-start defaults (Brandon 2026-11-07): target block size 7237, overlap 0.81, window Hamming; library block size 5304, overlap 0.82, window Hamming (tested). The editor opens at 1200 x 760; every new instance starts empty at factory defaults (tested).

The tables below say where each control lives in the older (build 15 and before) words; the control names there
(TONE / TIMBRE, TARGET PIECE SIZE ...) are the small names printed under the originals on UNDER THE HOOD.

## search tab: "brain tweaks"

| Samplebrain (Qt widget) | Ghost Mosaic | Where | Parameter id |
|---|---|---|---|
| fft / mfcc (sliderRatio + doubleSpinBoxRatio) | TONE / TIMBRE | PLAY macro + UNDER THE HOOD > SEARCH | `ratio` |
| freq & dynamics / freq only (sliderNRatio + doubleSpinBoxNRatio) | SHAPE / PITCH ONLY | UNDER THE HOOD > SEARCH | `n_ratio` |
| fft subsection: Start (spinBoxFFT1Start) | LOWEST BAND | UNDER THE HOOD > SEARCH | `fft_start` |
| fft subsection: End (spinBoxFFT1End) | HIGHEST BAND | UNDER THE HOOD > SEARCH | `fft_end` |
| novelty (sliderNovelty + doubleSpinBoxNovelty) | NOVELTY | PLAY macro + UNDER THE HOOD > SEARCH | `novelty` |
| boredom (sliderBoredom + doubleSpinBoxBoredom) | BOREDOM | UNDER THE HOOD > SEARCH | `boredom` |
| stickyness (sliderStickyness + doubleSpinBoxStickyness) | SMOOTH | PLAY macro + UNDER THE HOOD > SEARCH | `stickyness` |
| search stretch (sliderSearchStretch + spinBoxSearchStretch) | REPEAT | PLAY macro + UNDER THE HOOD > SEARCH | `stretch` |
| algorithm (comboBoxAlgorithm: basic / reversed / synaptic / synaptic slide) | SEARCH STYLE: Closest / Most different / Network / Network + wait | PLAY (macros card) + UNDER THE HOOD > SEARCH | `algo` |
| num synapses (sliderSynapses + spinBoxSynapses) | CONNECTIONS | UNDER THE HOOD > SEARCH | `synapses` |
| synaptic slide error (sliderSlideError + spinBoxSlideError) | WAIT UNTIL CLOSE | UNDER THE HOOD > SEARCH | `slide_error` |

## search tab: "target sound"

| Samplebrain | Ghost Mosaic | Where | |
|---|---|---|---|
| load target (pushButtonLoadTarget) + "no target sound loaded" label | drop on TARGET, or LOAD / BUILT-IN; the name shows on the card | PLAY > TARGET; UNDER THE HOOD > LOAD TARGET | `Engine::setTargetFile` |
| block size (spinBoxBlockSizeTarget) | TARGET PIECE SIZE (and the PIECE SIZE macro, which sets both sizes) | UNDER THE HOOD > TARGET; PLAY macro | `tgt_block` |
| block overlap (doubleSpinBoxBlockOverlapTarget) | TARGET OVERLAP | UNDER THE HOOD > TARGET | `tgt_overlap` |
| window shape (comboBoxTargetShape: dodgy, bartlett, blackman, flat top, gaussian, hamming, hann, rectangle) | TARGET FADE SHAPE (same 8, same order) | UNDER THE HOOD > TARGET | `tgt_window` |
| (re)generate blocks (pushButtonGenerateTarget) | BUILD TARGET; GENERATE does it too; AUTO BUILD does it when a size changes | UNDER THE HOOD; PLAY > GENERATE | `Engine::generateTarget` |
| use mic input (mic) | TARGET = LIVE INPUT ("LIVE INPUT" on the TARGET card) - the effect version: the track's audio is the target | Ghost Mosaic FX: PLAY > TARGET; UNDER THE HOOD > GHOST MOSAIC | `live_input` |

## search tab: "mix"

| Samplebrain | Ghost Mosaic | Where | |
|---|---|---|---|
| autotune (sliderAutotune + doubleSpinBoxAutotune) | AUTOTUNE | PLAY macro + UNDER THE HOOD > MIX | `autotune` |
| normalised (sliderNMix + doubleSpinBoxNMix) | FLATTEN | UNDER THE HOOD > MIX | `n_mix` |
| brain / target (sliderTargetMix + doubleSpinBoxTargetMix) | ORIGINAL MIX | PLAY macro + UNDER THE HOOD > MIX | `target_mix` |
| stereo mode (checkBoxStereo) | STEREO | UNDER THE HOOD > MIX | `stereo` |

## search tab: "brain contents"

| Samplebrain | Ghost Mosaic | Where | |
|---|---|---|---|
| the sound list (sound_items: one enable checkbox + delete per sound) | SOURCE SOUNDS list: a switch and an x per sound | PLAY > SOURCE SOUNDS | `Engine::setSoundEnabled` / `deleteSound` |
| all (toolButtonAll) / none (toolButtonNone) | ALL / NONE | PLAY > SOURCE SOUNDS; UNDER THE HOOD > ALL ON / ALL OFF | `Engine::setAllEnabled` |
| load sound (pushButtonLoadSound) | ADD (or drop files on the card) | PLAY > SOURCE SOUNDS; UNDER THE HOOD > ADD SOUNDS | `Engine::addFiles` |
| directory (pushButtonLoadSounds) | FOLDER (or drop a folder) - recursive, as the original | PLAY > SOURCE SOUNDS; UNDER THE HOOD > ADD FOLDER | `Engine::addFiles` |
| clear (pushButtonClearBrain) | CLEAR | PLAY > SOURCE SOUNDS; UNDER THE HOOD > CLEAR | `Engine::clearSounds` |
| block size (spinBoxBlockSize) | PIECE SIZE (and the PIECE SIZE macro) | UNDER THE HOOD > LIBRARY; PLAY macro | `src_block` |
| block overlap (doubleSpinBoxBlockOverlap) | PIECE OVERLAP | UNDER THE HOOD > LIBRARY | `src_overlap` |
| window shape (comboBoxBrainShape) | PIECE FADE SHAPE | UNDER THE HOOD > LIBRARY | `src_window` |
| (re)generate brain (pushButtonGenerate) | BUILD LIBRARY; GENERATE (library + target, then plays from the top) | UNDER THE HOOD; PLAY > GENERATE | `Engine::generateLibrary` |
| load brain / save brain (pushButtonLoadBrain / SaveBrain) | OPEN LIBRARY / SAVE LIBRARY - the same `.brain` file format, byte for byte | UNDER THE HOOD; PLAY > SESSION menu | `Engine::loadLibraryFile` / `saveLibraryFile` |

## lower bar

| Samplebrain | Ghost Mosaic | Where | |
|---|---|---|---|
| play (pushButtonPlay) / pause (pushButtonStop) | PLAY / STOP; plus PLAY BY: play button, MIDI keys (hold a note) or DAW play | PLAY > TARGET; UNDER THE HOOD > GHOST MOSAIC | `Engine::play` / `pause`, `play_mode` |
| record (pushButtonRecord) / stop (pushButtonStopRecord) | RECORD / STOP REC (WAV in Documents/GhostBlock/Ghost Mosaic/Recordings, then DRAG WAV drags it) | PLAY > TARGET; UNDER THE HOOD | `Engine::startRecording` / `stopRecording` |
| volume (dialVolume, 0-99, x10 gain) | VOLUME (same range and gain) | PLAY macro + UNDER THE HOOD > the volume dial | `volume` |
| load session / save session (pushButtonLoadSession / SaveSession) | OPEN SESSION / SAVE SESSION - the same `.samplebrain` format, byte for byte | UNDER THE HOOD; PLAY > SESSION menu | `Engine::loadSessionFile` / `saveSessionFile` |
| settings (pushButton, the cog) | see "settings dialog" below | | |
| the restart message (/restart_audio) | BACK TO START (UNDER THE HOOD), or click the target's waveform | | `Engine::restart` |
| status bar | the status line in the header (the engine's messages, in plain words) | header | `Engine::status` |

## net tab

| Samplebrain | Ghost Mosaic | Where |
|---|---|---|
| 10 destinations (osc.udp:// address + enable checkbox each) - every setting change is sent to them | the same 10 rows | UNDER THE HOOD > NETWORK (OSC) |
| (the Qt app always listened) | NETWORK ON switch, off by default so two copies in one project don't fight over the ports | UNDER THE HOOD > NETWORK (OSC) |

Every OSC message of the Qt app's audio and process threads is understood
(`Engine::handleOsc`): /start /pause /ratio /n_ratio /fft1_start /fft1_end /novelty
/stickyness /autotune /volume /search_algo /n_mix /target_mix /boredom /synapses
/search-stretch /slide-error /stereo /mic /record /stop /restart_audio /load_sample
/delete_sample /activate_sound /deactivate_sound /source_block_size /source_overlap
/generate_brain /load_target /target_block_size /target_overlap /generate_target
/window_type /target_window_type /load_brain /save_brain /load_session /save_session.
Status goes out as /report and /sound-item, as the engine sends them.

## settings dialog (settings.ui)

| Samplebrain | Ghost Mosaic |
|---|---|
| audio device, sample rate, buffer size | the DAW's audio settings (plug-in), or the standalone app's Options > Audio settings |
| gui / process / audio OSC ports | STATUS OUT / BUILD IN / SOUND IN ports, UNDER THE HOOD > NETWORK (OSC) |

## file formats and behaviour kept

- `.brain` and `.samplebrain` files are read and written by the engine's own stream code: a
  session saved here opens in Samplebrain and the other way round (tested: round trip byte
  for byte, and the upstream demo session plays bit-identical).
- Sound files: WAV, AIFF, FLAC, MP3, OGG (the Qt app used libsndfile: WAV, AIFF, FLAC, OGG).
  Files are resampled to the plug-in's rate when they differ (the Qt app played them at
  the device rate as they were).
- The library network (synapses) is built exactly as `build_synapses_fixed` builds it, on
  all cores (tested identical).

## added in Ghost Mosaic (not in the original)

PLAY BY (button / MIDI keys / DAW play), KEYS CHANGE PITCH, DRY (effect), SAFETY LIMITER,
SOFT START/STOP, AUTO BUILD, MUTATE,
A / B, UNDO / REDO, presets, built-in sounds and targets, DRAG WAV, the live view (DOTS /
HEAT), the plug-in state (sounds, target and settings saved in your DAW project).

Open-source engine, kept pure (build 13): by default the output is Samplebrain's own, sample
for sample (tested against the original engine). Nothing of ours sits on the output unless you
switch it on. SAFETY LIMITER (keeps the true peak under -1 dBTP; adds a 71-sample look-ahead
delay, reported to the DAW, only while on) and SOFT START/STOP (6 ms fades when playback starts
or stops) are extras that start OFF. A session saved with them on keeps them on, and presets
leave them as you set them. DRY (effect) starts at 0. In PLAY BY MIDI keys, the note's velocity
sets the level, as part of that mode.

## TERRAIN page (build 9, new)

A landscape (GhostBlock's shared TERRAIN component) that steers the matching. Nothing on it
replaces a Samplebrain control: every original setting keeps its own control, and a flat
landscape (the default) leaves the engine exactly as upstream (null-tested).

TERRAIN adds no sound processing of its own (build 12): every move below is one of Samplebrain's
own settings (the matching, NOVELTY, BOREDOM, SMOOTH, REPEAT, ORIGINAL MIX, which sounds are on),
moved for you. The STEREO SPREAD of builds 9-11 crossfaded the right channel towards a second
search, which was a widener of ours, so it was removed. With STEREO off both channels are the
left search, exactly as in Samplebrain; STEREO stays the original switch under the hood.

| On the landscape | What it does in the engine |
|---|---|
| left -> right (30 Hz .. 16 kHz, log) | a weight per FFT band in the matching distance (`block::_compare`, engine patch 2): a full mountain makes those bands count as if they were 18 dB louder, a full valley 18 dB quieter (the distance sums squared power differences, so that is a weight of up to 4096x); the weights average 1. MFCCs are left alone (no MFCC coefficient belongs to one frequency), so it acts on the TONE side of TONE / TIMBRE. The engine's bands are 1/100 of half the sample rate (~220 Hz each at 44.1 kHz), so everything under 220 Hz is one band. |
| front -> back (the target's start .. end; the effect: an 8-second loop) | how wild the matching gets there: tall or rough land raises NOVELTY (to at most 0.9) and BOREDOM (to at most 0.99) at that moment; a valley lowers NOVELTY towards 0. Flat land leaves the knobs' values. |
| a ridge the whole way front to back | a band that always counts (taken out of the time curve) |
| a ridge right across | a moment that gets wild (taken out of the weights) |
| SMOOTH / ROUGHEN, grit | roughness adds to the wildness (more random) |
| where the land stands across (build 10) | BRIGHTNESS: raised lows pick darker pieces, raised highs brighter ones (valleys the other way). Every library piece's spectral centroid is ranked; a piece's distance is scaled by up to 30x for or against it (engine patch 3), so this works at any TONE / TIMBRE setting |
| DRAG (build 10): how fast you pull | a burst of wildness (NOVELTY / BOREDOM up) and stutter (REPEAT up), heard within ~20 ms |
| DRAG: pulling up / down | up pushes it wilder, down pulls it back to the target |
| DRAG: pulling left / right (moving or widening) | sweeps the brightness preference that way (darker pieces to the left, brighter to the right) |
| spires (tall, very thin mountains) | STUTTER: REPEAT +1..+3 and SMOOTH down |
| plateaus (wide raised tables) | SMOOTH up towards 0.9: long smooth runs of one sound |
| mountains merging into each other | BLEND: ORIGINAL MIX up by as much as 0.4 |
| how many mountains (1..3) | how many of your sounds are in play (1 mountain = about half; 4 or more = all), picked by brightness: the sounds whose pitch sits under a mountain. Your own switches are kept (and saved) as you set them. |
| SHAPES menu | ready-made landscapes: GHOST, SPIRE FOREST, VOLCANO, PLATEAU, ARCHIPELAGO, TWIN PEAKS, BASS HUNTER, HAT HUNTER, SLOW BURN |
| ON | TERRAIN on / off (off: the knobs exactly as set; the land is kept) |
| WHAT THE SHAPE DOES | the four strongest moves the shape makes, with bars |
| LISTEN | play / pause |
| RANDOMIZE, FREEZE, EVOLVE, RESET, A/B, UNDO / REDO | the component's own (FREEZE: one moment's land for the whole target, so no time curve) |
| the readout under it | the band that counts most / least, where it gets wildest / closest, and the NOVELTY / BOREDOM in use right now |

Changes glide over ~50 ms on the audio thread (no jumps while dragging); nothing allocates
there. The land is saved in the plug-in state and beside a saved `.samplebrain` file as
`<name>.terrain.json` (the session file itself stays upstream's format). Older sessions load
flat. PIECE SIZE and the search style stay normal controls (a piece size change rebuilds the
library, which can't follow a drag). The drag's effects die away (0.35 s time constant, fully off
within ~2 s) once you stop pulling; they are never saved.

## TERRAIN moves every live control (build 15)

TERRAIN now reaches every Samplebrain control that can change live without a rebuild or a click: 14 of 14 (a test
asserts 100%: `testTerrainCoverage`). The land moves each one away from the value you set; the handle stays at your value
and a glowing second marker (UNDER THE HOOD and PLAY) follows the value in use. The TERRAIN page's MOVING NOW strip lists
what is moving, as "set > in use". TERRAIN off, flat land, or MOVEMENT 0 with a shape that has nothing to wander = no
change at all (null-tested, bit for bit as build 14). Only Samplebrain's own settings move: no DSP of ours is added.

"Zones" are the lows, mids and highs of the land under the target's playing position (front to back = target time).

| Control (id) | What moves it |
|---|---|
| NOVELTY (`novelty`), BOREDOM (`boredom`) | tall / rough land at the playhead; valleys lower NOVELTY; the drag burst; MOVEMENT |
| SMOOTH (`stickyness`) | plateaus and raised mids up; spires and the drag's stutter down; MOVEMENT |
| REPEAT (`stretch`) | spires, raised highs, drag stutter; MOVEMENT |
| ORIGINAL MIX (`target_mix`) | merging mountains, raised lows (never more than +0.4 over your value: level-safe); MOVEMENT |
| TONE / TIMBRE (`ratio`) | land that weights frequencies leans to TONE; rough land and raised highs to TIMBRE; MOVEMENT |
| SHAPE / PITCH ONLY (`n_ratio`) | plateaus, merging mountains, raised lows; MOVEMENT |
| LOWEST BAND (`fft_start`) | a valley in the lows stops the matching counting them; MOVEMENT |
| HIGHEST BAND (`fft_end`) | a valley in the highs does the same for the top; MOVEMENT |
| CONNECTIONS (`synapses`) | plateaus narrow the neighbourhood, spires widen it; MOVEMENT |
| WAIT UNTIL CLOSE (`slide_error`) | raised lows relax it, raised highs tighten it; MOVEMENT |
| AUTOTUNE (`autotune`) | raised highs pull pieces to the target's pitch; MOVEMENT. Only when you have turned AUTOTUNE up (any amount from a standing start switches the whole pitch path on and adds clicks and peaks). |
| FLATTEN (`n_mix`) | a valley in the mids flattens loudness; MOVEMENT. Only when you have turned FLATTEN up (same reason). |
| SEARCH STYLE (`algo`) | wide plateaus take Closest > Network (needs the library's links; otherwise it stays as set) |
| which SOURCE SOUNDS are in play | how many mountains, picked by pitch. Your own switches are kept; a sound TERRAIN holds out dims and its switch goes hollow, the ones in play glow. |

**MOVEMENT** (0 to 1, default 0.6) adds random wandering around where the land puts each control above, scaled by how much land
there is (flat land has none). **MOVEMENT RATE**: FREE (each control picks new places at its own random times) or on the beat
(1 beat, 2 beats, 1 bar, 2 bars, 4 bars; your DAW's position when it gives one, else a clock at its tempo). Values glide to
each new place, so there are no steps.

**Not moved (listed on the TERRAIN page):** needs a rebuild (PIECE SIZE, PIECE OVERLAP, PIECE FADE SHAPE, and the three
TARGET ones), restarts the sound (STEREO), or level (VOLUME). Our own extras (PLAY BY, DRY and so on) are not Samplebrain
controls and stay as set.

**Drag:** what you hear while pulling a mountain is the same landscape the engine settles on when you let go, with the same
glide. The one deliberate drag-only gesture is the burst of wildness and stutter from pulling fast; it is labelled on the page
("DRAG FX ... fades out when you let go") and is cleared when you leave the page. There are no preview notes in Ghost Mosaic
(LISTEN is the plain play / pause), so nothing can stay stuck.
