# GHOST BOX - the FX rack for any track

Ghost Box is one effect with 24 effect units (25 tiles with the always-on limiter) you can switch on, drag into any order and tweak, plus tools that build sounds for you: scenes, morphing, a motion timeline, a spirit board and more. Put it on any track, a drum bus or your master.

- Version 3.1.0. Formats: VST3, AU, CLAP, Standalone.
- It is an **effect** (it processes the audio of the track it sits on). It also reads MIDI notes, but only to switch scenes (see GHOST SCENES). The AU version is a plain effect and does not take MIDI in Logic.
- Slug: ghost-box.

---

## Quick start

> Make your first fat, moving sound in 2 minutes

1. Put **Ghost Box** on an effect slot (a mixer insert in FL Studio). Play a loop through it.
2. Look at the **strip** of tiles in the middle of the window. Click a tile (for example **FATTEN**) to open its page. Click the little light in the tile's corner (or double-click the tile) to switch it **on**.
3. Turn the unit's big knob. Turn its **MIX** down if it is too much.
4. Use the preset box on the rack page to pick one of the unit's own presets, or pick a whole-rack **SCENE** from the scene box.
5. Drag a tile left or right in the strip to change the order of the effects.
6. Try the **BRAIN** tab: choose a **SOURCE** (BASS, DRUMS, VOCAL...) and a **GOAL** (HUGE, LO-FI...), then press **AUDITION A / B / C**. Press **APPLY** to keep one.
7. Click **SNAPSHOT** (top right) to save what you hear. Press it again later after more tweaks.
8. At the bottom, click an empty scene slot to **store** what you hear there. Click a filled slot to jump to it.
9. Press **BYPASS** to compare with the dry sound. Use **A/B** to compare two full states.

---

## The screen

The default window is 1180 x 824. You can resize it from 960 x 764 up to 1900 x 1244. Top to bottom:

1. The **header** (logo, IN / OUT / MIX, SNAPSHOT, A/B, UNLOCK, look button, BYPASS).
2. The **rack panel** with a **tab row**, the **unit strip**, the page for the selected tab, and the **macro strip**.
3. The **GHOST SCENES bar** along the bottom.

### The header

| Control | What it does | Range / default |
|---|---|---|
| **IN** | Input gain for the whole box. | -24 to +24 dB, 0 |
| **OUT** | Output gain for the whole box. | -24 to +24 dB, 0 |
| **MIX** (DRY/WET) | Dry/wet for the whole box. | 0-100%, 100% |
| Meters | Input and output levels (red above -1 dB). | |
| **SNAPSHOT** | Saves the sound right now into the SNAPSHOTS bank. Shift+Alt+S does the same. The button reads "SAVED" for a moment. | |
| Snapshot list (the small arrow) | Load, star, rename, delete a snapshot; show the folder; undo a delete. In this compact bar there are no previous/next arrows; pick from the list. | |
| **A/B** and **A>B** | Two complete box states. The first click makes B a copy of A. **A>B** copies what you hear into the other slot. | |
| **UNLOCK / LICENSED** | Licence key entry (see DEMO VS LICENSED). | |
| Look button | BLACK / PURPLE / LIGHT / GHOST. See THE LOOKS. | |
| **BYPASS** | Dry signal only. | |

A line under MIX says "dry <-> wet (whole box)", or "BYPASSED: dry signal only".

### The tab row

**RACK**, **MORPH**, **MOTION**, **BRAIN**, **PERFORM**, **BOARD** are always there. After them come **CAPTURE**, **DNA**, **SPLITS**, **FOCUS**, **PRINT** and **G-SPACE**. In a narrow window some of these tabs are hidden because there is no room. Three ways always work:

- the **...** button on the GHOST SCENES bar has "Open ..." entries for CAPTURE, DNA, SPLITS, FOCUS, PRINT and G-SPACE;
- four small shortcut buttons sit in the empty cell of the strip: **SPLITS**, **FOCUS** (left column) and **PRINT**, **G-SPACE** (right column);
- make the window wider.

### The unit strip and its tiles

The strip shows one tile for each effect. Below 1300 px wide it folds into two rows. The last tile, **LIMIT** (GHOST LIMITER), always stays last.

| Do this | Result |
|---|---|
| Click a tile | Opens that unit's page. |
| Click the light in the tile corner, or double-click the tile | Switches the unit on or off. |
| Drag a tile | Moves it in the chain (the order matters: the sound goes through the tiles from left to right). A short tip may appear about the order you made. |
| Right-click a tile | Put it in **Lane A** or **Lane B** (PARALLEL LANES), or in a **GHOST SPLIT** branch. |
| Drag a tile onto the MOTION timeline | Adds a lane for that unit's MIX with an event where you drop it. |

### The macro strip

Eight **MACROS** (default names DARKNESS, CHAOS, SIZE, IMPACT, MOVEMENT, WIDTH, DIRT, AIR). Right-click **any knob** in the rack and choose a macro to put the knob under it. Right-click a macro to see what it moves, set each knob's depth and curve (linear, ease in, ease out, S-curve), rename it, clear it, or return all macros to the factory set. The strip also has **PARALLEL** (turn lane B on) and **BLEND A|B**.

### RACK page: the rack header

| Control | What it does |
|---|---|
| Preset box (for the selected unit) | Presets for this unit only: Init, factory presets, your own. Save preset... and Delete preset are in the same menu. |
| Scene box | Whole-rack scenes (35 factory scenes, starting with "Dry (all off)") and your own. |
| **SAVE SCENE** | Saves the whole rack (order, patterns, every effect) as your own scene. |
| **UNDO / REDO** | Takes back the last scene, preset, scene change, DNA result etc. |
| **BROWSE** | Search every scene and the unit's presets; star favourites. In the list: type to filter, Up/Down to hear each, Enter to keep, Esc to close. |
| Unit page | The controls of the selected unit (tables below). |

### The 24 effect units

Every unit has an on switch and a **MIX** (0-100%). GHOST SQUEEZE calls it **DEPTH**. The GHOST LIMITER has no mix and is always on by default. Ranges below are the real parameter ranges.

#### Rhythm

**GHOST GATE** - 16 drawable volume patterns plus a pump.

| Control | What it does | Default |
|---|---|---|
| PATTERN | 1-16, each a drawable gate pattern over the loop. Draw on the pattern grid. | 1 |
| LENGTH | 1/2 BAR, 1 BAR, 2 BARS. | 1 BAR |
| SMOOTH | Softens the gate edges. | 20% |
| TIME AMOUNT | How much the time part of the pattern applies. | 100% |
| PUMP MODE | OFF, 4 ON FLOOR, HALF-TIME, FAST PULSE, DRAWN, SIDECHAIN. | OFF |
| PUMP DEPTH / SHAPE | How deep and how curved the duck is. | 70% / 50% |
| PUMP LENGTH | 1/2 BEAT, 1 BEAT, 2 BEATS, 1 BAR. | 1 BEAT |
| PUMP RELEASE | 10-1000 ms. | 180 ms |
| PUMP ATTACK | 0.5-50 ms. | 4 ms |
| PUMP HOLD | 0-200 ms at the deepest point. | 0 |
| SIDECHAIN THRESHOLD | -60 to 0 dB (SIDECHAIN mode). | -24 dB |
| PUMP ONLY | On: only the pump works and the pattern gate is bypassed. | on |
| SIDECHAIN LOOKAHEAD | The audio runs 5 ms behind the key so the duck is down when the kick lands (adds 5 ms latency, reported to the host). | off |

**GHOST GRID** - paint effects onto a one-bar step grid.

| Control | What it does | Default |
|---|---|---|
| PATTERN | 1-8. | 1 |
| STEPS | 16 STEPS or 32 STEPS. | 16 |
| Ten rows (paint steps on each) | LOOP (LENGTH), STUTTER (RATE), SCRATCH (DEPTH), REVERSE (AMOUNT), TAPE STOP (SPEED), FILTER (DEPTH), CRUSH (AMOUNT), DELAY (FEEDBACK), VERB (SIZE), TONAL (PITCH). Each row has one knob. | see knob |

**HAUNT EDIT** - hold a pad for a tempo-synced stutter gesture.

| Control | What it does |
|---|---|
| 16 pads | Hold a pad to play its gesture. Select a pad to set its own TYPE (STUTTER, BUILD, GATE, GLITCH, REVERSE, TAPE STOP, LP SWEEP, HP SWEEP, CRUSH, PITCH DROP), its SIZE (1/64 up to 2 BARS) and its MOTION knob. |
| SYNC START | On: gestures start on the beat grid (default on). |
| LATCH | Keeps a pad running after you let go. |

**WARP** - draw a tempo-synced pitch bend.

| Control | What it does | Default |
|---|---|---|
| MODE | PITCH (keeps time) or TAPE (bends time too). | PITCH |
| AMOUNT | How much of the drawn bend applies. | 100% |
| LENGTH | Length of the drawn lane: 1/2 BAR, 1 BAR, 2 BARS, 4 BARS. | 1 BAR |
| RANGE | How far the bend can go: +-2, +-7, +-12 or +-24 semitones. | +-12 st |
| KEEP FORMANTS | Keeps the voice character in PITCH mode. | off |
| Bend lane | Draw the curve. The lane editor also offers a snap (SNAP OFF, SEMITONE, 5TH + OCT, OCTAVE) and a grid (1/4 to 1/32). | |

#### Colour

**CRACKLE** - multiband destroyer.

| Control | What it does | Default |
|---|---|---|
| BANDS | 1, 2 or 3 bands. | 1 |
| LOW / HIGH CROSSOVER | Band split frequencies. | 220 Hz / 2800 Hz |
| Per band: CURVE | TUBE, TAPE, DIODE, HARD CLIP, FUZZ, RECTIFY, HALF RECT, FOLD, SINE FOLD, BITCRUSH, DOWNSAMPLE, BROKEN SPEAKER, CRACKED, DYING BATTERY. | |
| Per band: DRIVE, MIX, LEVEL | Drive 0-100% (40%), band mix (100%), level -24 to +12 dB. | |
| PRE LOW CUT / PRE HIGH CUT | Filter before the destroyer (10 Hz-2 kHz, 1-22 kHz). | 10 Hz / 22 kHz |
| POST LOW CUT / POST HIGH CUT | Filter after it. | 10 Hz / 22 kHz |
| BOX | OFF, SMALL SPEAKER, 4x12 CAB, TELEPHONE, AM RADIO, TIN CAN, CARDBOARD BOX, MEGAPHONE, LAPTOP, WALKIE-TALKIE. | OFF |
| BOX MIX | How much of the speaker box you hear. | 100% |
| AUTO GAIN | Keeps the level steady. | on |
| OUTPUT | -24 to +12 dB. | 0 |

**WRECKER** - analog saturation from warm to slammed.

| Control | What it does | Default |
|---|---|---|
| STYLE | TAPE, TUBE, CONSOLE, TRANSISTOR, PENTODE, FUZZ, FOLD, CRUSH, DECIMATE. | TAPE |
| DRIVE | 0-100%. | 30% |
| TONE | -1 (dark) to +1 (bright). | 0 |
| LOW CUT / HIGH CUT | 10-1000 Hz / 1-22 kHz. | 10 Hz / 22 kHz |
| THUMP, STEEP, PUNISH | Three extra character switches on the unit page. | off |
| AUTO GAIN | Level compensation. | on |
| OUTPUT | -24 to +12 dB. | 0 |

**TUBE GLOW** - vintage tube console warmth.

| Control | What it does | Default |
|---|---|---|
| DRIVE | 0-100%. | 35% |
| CHARACTER | LINE or MIC. | LINE |
| BASS / TREBLE | -12 to +12 dB. | 0 |
| OUTPUT | -24 to +12 dB. | 0 |

**SOUL CRUSH** - level-smashing grit compressor.

| Control | What it does | Default |
|---|---|---|
| CRUSH | How hard it smashes. | 50% |
| DARKNESS | Darkens the grit. | 30% |
| RELEASE | 5-1000 ms. | 120 ms |
| OUTPUT | -24 to +12 dB. | 0 |

**RETRO** - vintage colour: noise, wobble, dirt, dust and age.

| Control | What it does | Default |
|---|---|---|
| MAGIC | One-knob overall amount. | 50% |
| Six sections: NOISE, WOBBLE, DISTORT, DIGITAL, SPACE, MAGNETIC | Each has its own on switch and amount. |
| NOISE | Level of added noise. TYPE: VINYL, DUST, TAPE HISS, WHITE, PINK, HUM 50, HUM 60, RADIO. TONE -1 to +1. FOLLOWS INPUT makes the noise follow your signal's level. | 30%, on |
| WOBBLE (+ on) and FLUTTER | Slow pitch wobble and fast flutter. | 30%, 30% |
| DISTORT (+ on) | Amount 30%. TYPE: TAPE, TUBE, CONSOLE, TRANSISTOR, FUZZ. | on |
| DIGITAL (+ on) | Digital grit. | off |
| SPACE (+ on) | The SPACE section (amount 30%). | off |
| MAGNETIC (+ on) | The MAGNETIC section (amount 30%). | off |
| TONE | -1 to +1. | 0 |
| OUTPUT | -24 to +12 dB. | 0 |

**FATTEN** - one big knob: turn it and it gets fatter.

| Control | What it does | Default |
|---|---|---|
| FAT | The big knob. | 40% |
| MODE | CLASSIC (the original: low shelf, glue and drive), SUB (octave-down sub layer), BODY (a 140 Hz thump), SMASH (heavy parallel squash), SATURATE (tape / tube drive), STACK (two micro-delayed copies: wide and thick), TRANSIENT (softer attack, longer tail), SAUSAGE (everything at once). A one-line description shows under the mode. | CLASSIC |
| COLOR | -1 (dark) to +1 (bright). | 0 |
| CRUNCH | Adds bite. | 20% |
| INPUT | 0-24 dB drive into the unit. | 0 |
| PUNCH | Keeps the hit of drums: the attack passes clean, the fat goes on the body. | off |
| LOW CLEAN | The deep sub (below about 90 Hz) skips the drive and stays clean and mono. | off |
| OUTPUT | -12 to +12 dB. | 0 |

**GHOST SPLIT (unit)** - split each hit into ATTACK and BODY and treat each its own way. (Not the same as the SPLITS tab.)

| Control | What it does | Default |
|---|---|---|
| SENSITIVITY | How easily a hit counts as ATTACK. | 50% |
| ATTACK LENGTH | 5-300 ms. | 30 ms |
| ATTACK / SUSTAIN | -1 to +1: boost or cut the attack and the body. | 0 / 0 |
| BLEND | Balance of the two parts. | 50% |
| LISTEN | BOTH, ATTACK or BODY. | BOTH |
| Per part: GAIN (-24..+12 dB), DRIVE, TILT (-1..+1), WIDTH (0-200%), REVERB SEND, ECHO SEND | A mini chain for each part. | |
| REVERB SIZE / DECAY, ECHO DIVISION / FEEDBACK | Settings of the two sends. | |
| OUTPUT | -24 to +12 dB. | 0 |

**BASS LAB** - low / mid / high bass tool.

| Control | What it does | Default |
|---|---|---|
| LOW SPLIT / HIGH SPLIT | Crossovers: 40-400 Hz and 700-8000 Hz. | 120 Hz / 2200 Hz |
| LOW WIDTH | 0-100%: below 100% makes the sub mono. | 100% |
| SUB | Adds sub. | 0 |
| GROWL | Midrange growl; GROWL FREQUENCY 300-1500 Hz (750), GROWL MOVE, GROWL SYNC division. | 0 |
| EDGE | Adds bite on top. | 0 |
| TRANSLATE | Makes the bass heard on small speakers. | 0 |
| LOW / MID / HIGH LEVEL | -18 to +12 dB each. | 0 |
| GAIN COMPENSATION | Level compensation. | on |
| OUTPUT | -24 to +12 dB. | 0 |

**GHOST FORMANT** - talking vowel filter (A-E-I-O-U vowel shapes with a mouth size and drawn or synced movement).

| Control | What it does | Default |
|---|---|---|
| VOWEL | Slides between the five vowel shapes A, E, I, O, U. | A |
| MOUTH SIZE | 0-100%. | 50% |
| RESONANCE | | 50% |
| FORMANT SHIFT | -12 to +12 semitones. | 0 |
| DRIVE | | 20% |
| MOVEMENT | STILL, LFO or DRAWN. | STILL |
| MOVEMENT DIVISION / DEPTH | Tempo division and depth for the movement. | 50% |
| DRAWN LENGTH | Length of the drawn movement: 1/2 BAR to 4 BARS. | 1 BAR |
| BODY | Keeps some of the unfiltered body. | 30% |
| LFO SHAPE | SINE, TRIANGLE, SAW, SQUARE, S&H, ENV. | SINE |

**SPIRIT EQ** - precision dynamic EQ over a live analyser. Each band has: on, bypass, TYPE (Bell, Low Shelf, High Shelf, Low Cut, High Cut, Notch, Band Pass, Tilt Shelf, Flat Tilt), SLOPE (6-96 dB), FREQ (10 Hz-30 kHz), GAIN (-30 to +30 dB), Q (0.025-40), STEREO (Stereo, Left, Right, Mid, Side) and a DYNAMIC section (threshold, range, ratio, attack, release, auto threshold, detect Band/Wide, spectral). Global: OUTPUT (-24..+24 dB), MODE (Zero Latency, Linear Low, Linear Medium, Linear High), CHARACTER (Clean, Subtle, Warm), AUTO GAIN, PHASE INVERT. The Linear modes add latency.

**PHANTOM FILTER** - resonant analog multimode filter with rhythm LFO.

| Control | What it does | Default |
|---|---|---|
| TYPE | LOW PASS, BAND PASS, HIGH PASS, NOTCH. | LOW PASS |
| SLOPE | 12 dB or 24 dB. | 24 dB |
| ROUTING | SINGLE, SERIES, PARALLEL. | SINGLE |
| CUTOFF | 20 Hz-20 kHz. | 1200 Hz |
| RESONANCE | | 40% |
| SPREAD | -4 to +4 octaves between the two filters (SERIES / PARALLEL). | 1 |
| DRIVE | | 20% |
| LFO AMOUNT | -1 to +1. | 0 |
| LFO SHAPE | SINE, TRI, SAW UP, SAW DOWN, SQUARE, S&H, SMOOTH. | SINE |
| LFO SYNC / DIVISION / RATE | Tempo sync (on, division 1/4) or free rate 0.05-20 Hz. | sync on |
| ENV AMOUNT / ENV RELEASE | Input-driven envelope on the cutoff: -1..+1, 10-1000 ms. | 0 / 200 ms |

#### Space

**CHORUS** - three-voice ghost chorus: RATE (0.05-5 Hz, 0.4), DEPTH (50%), DELAY (2-30 ms, 12), FEEDBACK (0), WIDTH (100%).

**CRYSTAL** - reverse and pitched granular splices: PITCH (-24 to +24 st, +12), SPLICE (20-600 ms, 180), DELAY (0-1500 ms, 250), FEEDBACK (40%), REVERSE (50%), SPREAD (60%).

**ECTO ECHO** - character delay.

| Control | What it does | Default |
|---|---|---|
| MODE | TAPE, ANALOG, BBD, DIGITAL. | TAPE |
| SYNC | Tempo sync on/off. | on |
| TIME | 1-2000 ms (when not synced). | 375 ms |
| DIVISION | 1/64 to 2/1, with triplets and dotted values. | 1/8 dotted |
| FEEDBACK | | 45% |
| PING-PONG | | on |
| WIDTH | | 100% |
| LOW CUT / HIGH CUT | 20 Hz-2 kHz, 1-20 kHz. | 120 Hz / 6.5 kHz |
| CHARACTER | | 50% |
| DUCKING | Echo gets quieter while you play. | 0 |

**SPECTRAL SPACE** - blendable ambience with glimmering tails: SIZE (50%), BLEND (60%), DECAY (0.3-12 s, 2.5), TONE (-1..+1), MODULATION (40%), GLIMMER (20%), WIDTH (100%), PRE-DELAY (0-250 ms, 20), DUCKING (0).

**SHIMMER** - octave-shimmer reverb with freeze: SIZE (70%), DECAY (0.5-30 s, 5), DAMPING (40%), SHIMMER (50%), PRE-DELAY (0-250 ms, 30), MODULATION (50%), WIDTH (100%), FREEZE (switch).

**HAUNTED HALLS** - 16-line reverb, 11 rooms.

| Control | What it does | Default |
|---|---|---|
| MODE | ROOM, HALL, PLATE, CHAMBER, CATHEDRAL, CLOUD, GALAXY, REVERSE, GATED, NONLINEAR, DARK TAPE. | HALL |
| SIZE | | 60% |
| DECAY | 0.1-60 s. | 3 s |
| PRE-DELAY | 0-500 ms. | 20 ms |
| DIFFUSION / DAMPING | | 70% / 40% |
| LOW CUT | 20-1000 Hz. | 80 Hz |
| MODULATION / WIDTH | | 40% / 100% |
| EARLY | Early reflections. | 50% |
| SHIMMER | | 0 |
| FREEZE | Holds the tail forever. | off |

**GHOST SPECTRA** - spectral freeze, smear, scatter and shift: FREEZE (switch), SMEAR (30%), SCATTER (0), SHIFT (-24..+24 st), GHOST (50%; keeps the hits), MOTION (0) with MOTION DIVISION, TILT (-1..+1), HQ BOUNCE (switch), FREEZE SYNC (OFF, 1/16 up to 2 BARS), THRESHOLD (-90..0 dB). A 64-band painted mask lets you choose which frequencies are affected. This unit adds latency.

**DOORWAY** - morphing-spaces reverb: SIZE (60%), SHAPE (METALLIC, ORGANIC, HOLLOW, DIFFUSE, CRYSTALLINE), MOVEMENT (STATIC, DRIFT, ROTATE, EXPAND), DENSITY (70%), DECAY (0.3-30 s, 4), TRANSFORM (0), FREEZE, PRE-DELAY (0-250 ms), TONE (-1..+1), EARLY REFLECTIONS (50%).

#### Master

**GHOST SQUEEZE** - 3-band upward and downward compression: TIME (50%), UPWARD (60%), DOWNWARD (70%), INPUT (-12..+12 dB), OUTPUT (-12..+12 dB), DEPTH (the mix).

**GHOST LIMITER** - transparent lookahead limiter, no overs: CEILING (-12 to 0 dB, -0.3), GAIN (0 to +18 dB), RELEASE (1-500 ms, 80). It is on by default and always last. GHOST DNA never mutates it.

### MORPH tab

Capture two whole-rack states and glide between them with one knob.

| Control | What it does |
|---|---|
| **MORPH** knob | 0% = A, 100% = B. |
| **SET A / SET B** | Capture the rack as it sounds now. (SET B switches MORPH on.) |
| **A FROM SCENE... / B FROM SCENE...** | Load a scene and capture it as A or B. |
| **SWAP A/B**, **CLEAR** | Swap or forget (UNDO brings back). |
| **MORPH ON** | Enabled when both states exist. |
| Curve box | LINEAR, SMOOTH, EASE IN, EASE OUT: how the knob travels from A to B. |

Settings that cannot glide (switches, choices) flip part of the way. Everything else glides.

### MOTION tab

Five drawable, tempo-synced lanes: **VOLUME, FILTER, PITCH, SPACE, DAMAGE**. Each lane has:

| Control | What it does | Default |
|---|---|---|
| ON | Lane on/off (off lanes do nothing). | off |
| DEPTH | 0-100%. | 100% |
| LENGTH | 1 BEAT, 2 BEATS, 1 BAR, 2 BARS, 4 BARS, 8 BARS, 16 BARS. | 1 BAR |
| FEEL | STRAIGHT, TRIPLET, DOTTED. | STRAIGHT |
| MODE | CURVE (draw a curve) or STEPS. | CURVE |
| STEPS | 8, 16, 32, 64. In STEPS mode: drag = value, right-click = tie, double-click = 0. | 16 |
| PHASE | Shifts the lane against the song (50% starts half way). | 0 |

Other buttons: **RESET ON START** (lanes count from the moment the transport starts; off: they follow the song position), **MOTIONS...** (factory motions and your saved MOTION DNA files), **SAVE MOTION...** (all 5 lanes as a file), **CLEAR ALL**.

#### GHOST MOTION timeline

The **GHOST MOTION TIMELINE** switch on the right swaps the 5 lanes' editor for one shared timeline for the whole rack. The 5 classic lanes keep running. Switch it again to get them back.

| Part | What it does |
|---|---|
| **LOOP** | 1 to 8 bars, in time with the host. |
| **+ LANE** | Add a lane for any continuous rack setting (up to 12 lanes). Also: right-click any knob > "Add to GHOST MOTION timeline", or drag an effect tile into the loop. Switches and choices are not listed. |
| Lane chips | Click a chip to edit that lane; right-click to remove. |
| **ON / REMOVE** | Lane on/off, remove lane. |
| **CURVE / STEPS** | The lane's base: a drawn curve or steps (8/16/32/64). |
| **EVENTS / BASE** | What your mouse edits. EVENTS: drag on an empty place to make a new event (its height is its amount, -1 to +1), drag an event to move it, drag its right end to change length, double-click or right-click to delete. |
| **DEPTH** | -100 to +100% per lane. |
| **DETECT HITS** | Finds the hits in the last complete loop of Ghost Box's input. Play the loop once with the transport running first. |
| **RULE** | EVERY 2ND HIT, LAST HIT OF BAR, QUIET HITS, LOUD HITS, EVERY HIT. |
| Event length box | 1/32, 1/16, 1/8, 1/4, 1/2. |
| **PLACE ON HITS** | Puts an event on every hit the RULE picks, in the selected lane. It replaces events placed this way before; your own events stay. |

The timeline is part of the sound: presets, sessions, snapshots, scenes and A/B all carry it. The detected hits are not saved.

### BRAIN tab (CHAIN BRAIN)

Pick a **SOURCE** (BASS, DRUMS, VOCAL, SYNTH LEAD, PAD, FULL MIX) and a **GOAL** (AGGRESSIVE, HUGE, CINEMATIC, NOSTALGIC, HIT HARDER, MOVE, DISAPPEAR INTO SPACE, LO-FI). Three generated chains appear.

| Control | What it does |
|---|---|
| **AUDITION A / B / C** | Hear one chain. |
| **NEW IDEAS** | Three new chains for the same source and goal. |
| **SEED** | The same number always gives the same three chains. Type one and press Enter. |
| **MATCH LOUDNESS** | While auditioning, every chain plays at the loudness of your dry sound, so louder never fools you. |
| **ORIGINAL** | Back to the rack you had before auditioning. |
| **APPLY** | Keep the chain you hear. Its loudness match becomes make-up gain. |

### PERFORM tab

One screen for playing: the 8 big **MACRO** knobs, the **MORPH** knob, and the 5 **MOTION LANES** on/off buttons.

### BOARD tab (SPIRIT BOARD)

A board of up to 12 **ghosts**. Each ghost holds a scene. A glowing **puck** blends all of them: the closer a ghost is to the puck, the more you hear it.

- **Click an empty place** on the board to add a ghost. Pick what it holds: what you hear now, a snapshot, a factory scene or one of your scenes.
- **Drag a ghost** to move it. **Drag the puck** to morph. **Right-click a ghost** for its menu (what it holds, set weight %...).
- The first ghost needs a click on the board ("click anywhere on the board to place your first ghost").

| Control | What it does |
|---|---|
| **BOARD ON** | The board drives the rack (MORPH lets go). |
| Weights box | DISTANCE, SOFT (GAUSSIAN) or NEAREST 3: how the ghosts share the sound. |
| **SHARPNESS** | Low = wide, creamy blends. High = each ghost owns its own island. |
| **SNAP** | Let go of the puck and it jumps onto the nearest ghost. |
| **RANDOM**, **CIRCLE**, **CLEAR** | Throw the ghosts to random places, arrange them in a circle, remove all. |
| **SAVE BOARD... / LOAD BOARD...** | Save or load a board (also in BROWSE). |
| BLEND strip | **SMOOTH**: one rack glides between the ghosts' settings. **LAYER**: every ghost with weight runs its own rack on the same sound at the same time (different effects play together). Then WEIGHT LAW (POWER or LINEAR), WEIGHTS (DISTANCE, EVEN, CUSTOM) and LAYERS (how many racks may run at once; more = more CPU). |
| **ORBIT** | The puck moves by itself: OFF, CIRCLE, FIGURE 8, RANDOM WALK, or CUSTOM 1-8 (your drawn or recorded path). RATE: 1 BEAT to 8 BARS. DEPTH: how far it travels. |
| **PATH** | Opens the drawn-orbit strip: **DRAW** (drag a loop; draw slowly where the puck should linger), **REC** (move the puck by hand while the song plays), **SPEED...**, **SMOOTH**, **REVERSE**, **RESAMPLE...**, **CLEAR**, LOOP (1, 2, 4 or 8 bars), PHASE, SIZE. |

The puck position is also two host parameters, Spirit Board X and Y, so you can automate it.

### GHOST SCENES bar (bottom)

Eight scene slots, named CLEAN, FAT, DREAM, BROKEN, SPACE, FREEZE, CHAOS, DROP. At first they are empty (just names). A scene holds a whole sound: every unit and setting, the chain order, patterns, curves, MORPH, MOTION and MACROS. It never holds the look, BYPASS, the SPIRIT BOARD, IN / OUT gain or FIXED LATENCY.

| Do this | Result |
|---|---|
| Click a filled slot | That scene, and it stays (latch). |
| Press and hold a slot (more than 0.35 s) | The scene only while you hold; letting go returns to exactly what played before. |
| Click an empty slot | Stores what you hear there. |
| Right-click a slot | Store what you hear here / Rename... / Clear / Copy to... / Undo the last scene edit. |
| MIDI keys C1 to G1 (MIDI 36-43) | Scenes 1-8 (default). HOLD: the scene while the key is down. LATCH: a key switches. |
| Automate **Ghost Scene** (host parameter) | Follows and triggers scenes. **Ghost Scene Morph** (default 100%) is how far the box sits between the previous sound and the scene. |

Controls on the bar:

| Control | What it does |
|---|---|
| **CUT / MORPH / SPILL** | The transition. CUT: settings glide 10 ms at the grid so nothing clicks. MORPH: every setting glides to the scene over the MORPH TIME. SPILL: like CUT, but echoes and reverbs that the new scene turns off keep ringing out their tails. |
| Quantise box | OFF, 1/16, 1/8, BEAT (default), BAR, 2 BARS: scenes land on the next grid line. With the transport stopped or OFF, they land at once. |
| MORPH TIME box | 1/4 BEAT, 1/2 BEAT, 1 BEAT, 2 BEATS, 1 BAR, 2 BARS, 4 BARS. |
| **MIDI** button | Turns MIDI-key scene triggering on or off (shows the base key). |
| **...** menu | MIDI keys on/off; HOLD or LATCH; the key range (C1-G1, C2-G2, ...); fill empty slots with starter scenes; undo the last scene edit; Open GHOST CAPTURE / DNA / SPLIT / FOCUS / GHOSTPRINT / GHOST SPACE. |

Every scene change is one UNDO step. A saved session remembers which scene was active but does not trigger it when loaded.

### CAPTURE tab (GHOST CAPTURE)

Ghost Box keeps a rolling history of what it played (its real processed output). CAPTURE saves the last 2, 4, 8 or 16 bars, ending on the latest bar line, onto a **shelf** of 8 clips.

| Control | What it does |
|---|---|
| **CAPTURE** (Shift+Alt+C) | Take a capture now. |
| Bars box | 2, 4, 8 or 16 BARS. |
| **HISTORY ON / OFF** | Off frees the memory (about 64 s of audio). Saved with the session. |
| Clip tile | The waveform. **Drag it into your DAW** to drop a 32-bit float stereo WAV. |
| **LOOP** | Hear the clip looping through Ghost Box's output. |
| **REVERSE** | Flip the clip (the drag, SAVE AS and SLICE use it too). |
| **SAVE AS...** | Save as a WAV file. |
| **SLICE** | Writes the WAV and a `.ghostclip.json` (tempo and bars) next to it and drags both. Drop them on Ghost Slice and it loads with that tempo. |
| **x** | Remove from the shelf. |

If you ask for more bars than the history holds it says "Not enough history yet". The shelf is in memory only and is not saved with the project. Files go to `Documents/Ghosty/Ghost Box/Captures`. If you stop the transport, or jump or loop inside the window, the clip is the last N bars of samples at the current tempo and is marked "not bar-aligned".

### DNA tab (GHOST DNA)

Makes new chains from whole sounds. Every result is a normal chain that stays editable and is one UNDO step. All results are loudness-matched.

| Control | What it does |
|---|---|
| **A** and **B** boxes | Parents for BREED. Each can be WHAT YOU HEAR, a filled scene or one of the 12 newest snapshots. |
| **BREED** | A x B. Every unit comes from A or B. **BLEND** = how much of B. |
| **MUTATE** | Seeded random changes to units that are on, by **STRENGTH**. |
| **EVOLVE** | 4 variations as tiles. Click a tile to hear it. |
| **KEEP** | What you hear stays; the next action starts from it. |
| **UNDO TO ORIGINAL** | Back to exactly what played before the first DNA result. |
| **SEED / NEW** | Editable number. The same sound + seed + strength + locks give the same result. Each press moves the seed on. |
| **LOCKS** | A chip for each unit that is on. Click locks the unit; right-click locks single settings. Locked things never change. |

The GHOST LIMITER is never changed and is always on in a result, so a mutation cannot run away. Feedback settings stay below 90%.

### SPLITS tab (GHOST SPLIT routing)

Split the signal into 2 or 3 **branches**. Each branch runs its own units; the branches are summed back, time-aligned. The tab is called **SPLITS** so you do not mix it up with the GHOST SPLIT unit tile.

| Mode | Branches | How it splits |
|---|---|---|
| FREQUENCY | LOW / HIGH (2 BANDS) or LOW / MID / HIGH (3 BANDS) | Crossovers at X LOW (120 Hz) and X HIGH (3000 Hz). **KEEP IN BAND** (on): what a branch changes stays in its own band, so a distorted HIGH never adds to the sub. |
| TRANSIENT / BODY | ATTACK / BODY | **SENSITIVITY** and **ATTACK LENGTH**. |
| MID / SIDE | MID / SIDE | |
| LEVEL | LOUD / QUIET | **THRESHOLD** (-60..0 dB, -18) and **KNEE** (0-24 dB, 6). |
| PARALLEL | A / B | **BLEND** 0% = only A, 100% = only B. |

Controls: **ON**, the MODE row, **BANDS**, the mode's own settings, and for each branch a **LEVEL** (-24 to +12 dB), a list of unit chips (click a chip to move it or switch it on/off), and **+ UNIT**. Routed tiles show a small teal badge (L / M / H, A / B, M / S, L / Q). You can also right-click a tile and pick its branch. A unit can be in one branch only.

While SPLIT routes units, the PARALLEL lanes are ignored. Changing mode or moving units fades the chain out and back in (about 5 ms), click-free. The host is told the latency of the slowest branch.

### FOCUS tab (GHOST FOCUS)

Say what you want and hear four starting chains. Pick **FAT, WIDER, DARKER, DREAMY, DIRTY, MOVE, WEIRD** or **PUNCH**. No AI and no randomness: the same intention always gives the same four chains.

| Control | What it does |
|---|---|
| Intention buttons | Show 4 chains as tiles (name, what it does, units and presets, gain match in dB). |
| Tile | Click to hear it, gain-matched to what played before. |
| **AMOUNT** | How much of the chain's wet sound you hear (100% = as designed). |
| **KEEP** | The chain becomes your rack. |
| **BACK TO ORIGINAL** | Exactly the sound you had before the first chain. |

Nothing from FOCUS is saved. After KEEP it is a normal sound.

### PRINT tab (Ghostprint)

Drop a short audio reference and take its **gesture**, not its sound. Ghost Box measures how the reference moves (volume, hits, brightness, stereo) and writes that as editable GHOST MOTION timeline lanes. The reference's audio is never used or saved.

| Control | What it does |
|---|---|
| **LOAD FILE...** | WAV, AIFF, FLAC, OGG or MP3, up to 60 s. You can also drop a file anywhere on the page. |
| **FROM CAPTURE** | Use a CAPTURE clip. Its tempo is known. |
| **BPM** | AUTO detects the tempo; type one if you know it. |
| **BARS** | AUTO, 1, 2, 4 or 8 BARS: how many bars the reference is. |
| Four curves | ENVELOPE (to VOLUME, FILTER CUTOFF or SPACE DISTANCE), ACCENTS (to ECHO THROWS, REVERB THROWS or FILTER CUTOFF; default off), BRIGHTNESS (to TONE or FILTER CUTOFF), STEREO (to WIDTH or SPACE PAN). Choose each curve's destination (or OFF). |
| **INTENSITY** | How much of the gesture applies (100% = full). |
| **SMOOTH** | Rounds the curves. |
| **RHYTHM-ONLY** | VOLUME becomes a gate that opens on the accents. |
| **INVERT** | Flips every curve (accents duck instead of push). |
| **APPLY** | Writes the lanes into the GHOST MOTION timeline (one UNDO step). Lanes on the same settings are overwritten. |

VOLUME, WIDTH and TONE are three clean settings in a small stage after GHOST SPACE; at their defaults it does not touch the audio. A bad file shows a message and writes nothing. The loop is at most 4 bars, and 4/4 is assumed.

### G-SPACE tab (Ghost Space)

Move the sound through a space. You are in the middle of a 2-D field, facing up. The sound is a dot. Where the dot is sets how far away it sounds and from which side.

| Control | What it does | Default |
|---|---|---|
| **SPACE ON** | Turns it on (20 ms crossfade). Off, the rack sounds as without it. | off |
| The field | Drag the dot to place the sound. Farther = quieter, darker, more room. The line under it says where the sound is. | |
| **PATH** | STILL, ORBIT, FALL, GHOST, BLACK HOLE, DRAWN. | STILL |
| **RATE** | 1/2 BAR, 1 BAR, 2 BARS, 4 BARS, 8 BARS. | 1 BAR |
| **DEPTH** | Orbit size, how far a fall goes, how deep the black hole pulls. | 60% |
| **ROOM** | Size of the space. | 50% |
| **AIR** | How much distance darkens the sound. | 50% |
| **REVERB** | How much room you hear. | 50% |
| **GHOST PITCH** | Pitch of the GHOST copy, -24 to +24 st. | -12 |
| **TRAIL** | How far behind the copy follows: 1/16, 1/8, 1/8 D, 1/4, 1/2. | 1/8 D |
| **DRAW** | Click-drag a looping path in the field. | |
| Loop-length box | 1, 2, 4 or 8 bars for the drawn path. | 2 |
| **CLEAR PATH** | Removes the drawn path. | |

PATH meanings: ORBIT circles you; FALL falls away into a bigger room and back; GHOST orbits and leaves a delayed, pitched copy behind; BLACK HOLE spirals in, freezes, then returns; DRAWN plays your drawn loop. It is a creative space, not a binaural renderer: left/right and near/far read well on speakers and headphones. The 11 settings are host parameters (fx_gspace_...), so you can automate them, and they are rack settings that scenes, MORPH and snapshots carry.

---

## Workflows

### Fat bass without a muddy sub (GHOST SPLIT)

1. Open the **SPLITS** tab (or the shortcut in the strip).
2. MODE: FREQUENCY, BANDS: 2 BANDS, X LOW about 120 Hz, KEEP IN BAND on, **ON**.
3. Click **+ UNIT** under HIGH and pick **WRECKER**. Set it to FUZZ and push DRIVE.
4. The sub stays clean while the top gets dirty.

### Make a section "drop" with scenes

1. Build a clean sound. Click an empty slot (for example **CLEAN**) to store it.
2. Add effects until it is big. Click the **DROP** slot to store that.
3. Set the transition to **CUT**, quantise to **BAR**.
4. Click DROP on the bar line you want. Or hold the slot to preview and release to go back.
5. Switch to **SPILL** if the old reverb should ring out under the drop.

### Play scenes from the piano roll

1. Make sure the **MIDI** button on the bar is on.
2. Route a MIDI track or clip to Ghost Box the way your host sends MIDI to an effect.
3. Notes C1 to G1 (MIDI 36 to 43) now trigger scenes 1 to 8. In HOLD mode the scene lasts while the note is held.

### Steal a rhythm (Ghostprint)

1. **PRINT** tab. Drop a short loop with the movement you like (a pumping pad, a gated synth).
2. Choose destinations (default: VOLUME, TONE, WIDTH), set INTENSITY.
3. **APPLY**. Open MOTION > GHOST MOTION TIMELINE to see and edit the lanes.

### Capture a loop and send it to Ghost Slice

1. Play with your effects over a loop. Choose 4 BARS on the **CAPTURE** tab.
2. Press **CAPTURE** (or Shift+Alt+C) on a bar line.
3. Press **SLICE** on the clip tile. Drop both dragged files onto Ghost Slice. (Full version only.)

### Let it surprise you (DNA)

1. Set a sound you like. On the **DNA** tab, press **EVOLVE**.
2. Click each tile to hear it. Lock the units you want to keep untouched first.
3. **KEEP** the best one. **UNDO TO ORIGINAL** goes back.

### Hits-only effects (AUDIO-AWARE motion)

1. MOTION > **GHOST MOTION TIMELINE**. Add a lane for ECHO MIX with **+ LANE**.
2. Play your drum loop once with the transport running. Press **DETECT HITS**.
3. RULE: LAST HIT OF BAR. Press **PLACE ON HITS**. The echo now throws on each bar's last hit.

### Quick starting points (FOCUS or BRAIN)

1. **FOCUS**: pick WIDER, audition the four tiles, **KEEP** one.
2. Or **BRAIN**: SOURCE VOCAL, GOAL DISAPPEAR INTO SPACE, **AUDITION A**.

---

## FL Studio tips

- **Loading:** Rescan plugins (Options > Manage plugins > Find more plugins). Load Ghost Box in a mixer insert slot. It is an effect, so you do not put it in the Channel Rack as an instrument.
- **MIDI scenes:** Ghost Box accepts MIDI on its input in the VST3 and CLAP builds. To use it in FL Studio, route MIDI to the effect plugin the way FL allows for effects. If that is awkward, use the scene slots on the bar or automate **Ghost Scene** instead.
- **Tempo sync:** Everything in Ghost Box that is "in time" (patterns, scenes, MOTION, G-SPACE paths, echo divisions) follows the host tempo and song position. If the host gives no time signature it assumes 4/4.
- **Automation:** Every knob is a host parameter. In FL, right-click a knob > Create automation clip. Special parameters you can automate: Ghost Scene, Ghost Scene Morph, MORPH, Macro 1-8, the five motion lanes' on/depth/length/..., Spirit Board X/Y, ORBIT settings, GHOST SPLIT and GHOST SPACE settings. New parameters are appended at the end of the list so older automation does not move.
- **Saving:** Your whole Ghost Box state is saved in the FL project. Scenes (the slots), the timeline and DNA settings are saved with it. Snapshots, unit presets, whole-rack scenes, spirit boards and motion files are saved as files (below).
- **Dragging audio out:** Drag a CAPTURE clip tile into the Playlist or a channel. (Full version.) There is no MIDI drag-out.
- **Latency:** Ghost Box reports a fixed latency (on by default; the host parameter **Fixed Latency**) that covers any one latent unit, so switching GHOST SPECTRA, WARP or the pump lookahead on and off never changes the host's delay compensation. Some units (for example the SPIRIT EQ linear modes and GHOST SPECTRA) add their own latency.
- **Tail:** Ghost Box reports a 12 second tail.
- **Where files live:**
  - Presets and scenes: `Documents/Ghosty/Ghost Box/` in subfolders `FX Presets` (one folder per unit), `FX Scenes`, `Spirit Boards`, `Motion`, `Captures`. On Windows that is `Documents\Ghosty\Ghost Box\...`; on Mac `~/Documents/Ghosty/Ghost Box/...`.
  - Snapshots: Windows `%APPDATA%\GhostBlock\Ghost Box\Snapshots`; Mac `~/Library/Application Support/GhostBlock/Ghost Box/Snapshots`. The list menu has "Show the folder".
  - SPIRIT BOARD log: Mac `~/Library/Logs/Ghost Box/ghostbox-board.log`; Windows `%APPDATA%\Ghost Box\ghostbox-board.log`. It is kept small and only for troubleshooting.
  - Licence file: the `GhostBlock` folder in the same Application Support / AppData place.

---

## The looks

Click the look button in the header. It shows the name of the look it will switch to. The order is **BLACK -> PURPLE -> LIGHT -> GHOST -> BLACK**. Tooltip: "Look: ... Click for ... (GHOST = liquid glass; saved with the project, not in presets)". GHOST is the liquid-glass look with floating panels and an aurora background. The look is saved with the project, not with presets, scenes or snapshots.

---

## Demo vs licensed

Without a licence key (demo):

- The sound drops out for 2 seconds every 20 seconds. The header caption reads "DEMO - drops out every 20 s".
- A reopened project starts at the default sound. Unlocking brings the saved sound back.
- **Nothing can leave the plugin as audio.** In demo, dragging a CAPTURE clip, **SAVE AS** and **SLICE** are refused. The UNLOCK button blinks and a short hint shows.

Everything else works, including scenes, DNA, FOCUS, PRINT, G-SPACE, snapshots, presets and the capture shelf itself.

To unlock: click **UNLOCK**, paste the key from your receipt email into "License key" and press **Unlock** (or **Buy** to open the store). The button becomes **LICENSED**.

---

## Shortcuts

| Key / gesture | Does |
|---|---|
| Shift+Alt+S | SNAPSHOT (plain S stays a piano key in hosts) |
| Shift+Alt+C | CAPTURE |
| Ctrl/Cmd+Z in the snapshot list | Undo a snapshot delete (within 60 s) |
| Double-click a tile / click its light | Unit on/off |
| Drag a tile | Reorder the chain |
| Right-click a tile | Lanes / GHOST SPLIT branch |
| Right-click a knob | Assign to a macro, add to the timeline; Type value / Reset |
| Click a scene slot / press and hold | Latch / hold |
| Right-click a scene slot | Store, rename, clear, copy |
| BROWSE: Up/Down, Enter, Esc | Step and hear, keep, close |
| Click the board | Add a ghost |
| Right-click a ghost | Its menu |
| Brain: type seed + Enter | Bring back a set of chains |

---

## Troubleshooting

1. **No sound / silence.** Check BYPASS, MIX and OUT. Check the limiter ceiling. If you hear it with BYPASS on, the box is not the problem.
2. **Sound drops out every 20 s.** Demo mode. Unlock to remove it.
3. **A scene does not trigger from MIDI.** Is the MIDI button on? Is the key in the C1-G1 range (or the range you chose)? The AU build does not receive MIDI (Logic does not send MIDI to effects). Some hosts do not send MIDI to effects.
4. **Changing a scene "waits".** Scenes land on the next grid line. Set the quantise box to OFF to switch at once.
5. **A new unit changed the loudness.** Use MATCH LOUDNESS in BRAIN, or the OUT knob. DNA and FOCUS results are gain-matched.
6. **Click when I switch something.** Most switches are click-free; very fast reordering can cause a tiny fade. Use MORPH or SPILL for scene changes.
7. **The tabs CAPTURE, DNA... are missing.** The window is too narrow. Use the ... menu on the scene bar, the shortcuts in the strip, or make the window wider.
8. **Mac "can't be opened" warning.** The installer is not notarized yet: right-click the installer and choose Open, then confirm.
9. **Plugin not showing in FL Studio.** Rescan plugins and restart FL.
10. **CAPTURE says "Not enough history yet".** Play longer, or pick fewer bars. HISTORY must be on.

---

## FAQ

**Is Ghost Box an instrument or an effect?** An effect. It reads MIDI only to switch scenes.

**What is the difference between SPLITS and the GHOST SPLIT tile?** The tile splits each hit into attack and body and treats each part. SPLITS is routing: it sends units into low/high, mid/side, loud/quiet or parallel branches.

**Do snapshots and scenes store the same thing?** Both store a whole sound. Snapshots are files in a bank (SNAPSHOT button). Scenes are the 8 slots on the bar and live in the project. Snapshots and A/B never carry the scene slots.

**Are results of DNA, FOCUS and BRAIN random?** They are seeded: the same seed gives the same result. FOCUS has no randomness at all.

**Can I use Ghost Box on my master?** Yes. The GHOST LIMITER at the end of the chain helps.

**Will older projects sound the same?** Yes. New features were added so an older session renders the same as before.

**Does Ghostprint copy the sound of my reference?** No. Only its movement is kept, and only while the plugin is open.

---

## Glossary

- **Unit / tile:** one effect in the strip.
- **Chain:** the order of the units; the sound flows left to right.
- **Scene:** a whole-rack sound. Eight slots live on the bar. Whole-rack scenes can also be saved as files.
- **Snapshot:** a saved copy of what you hear, kept in the SNAPSHOTS bank.
- **MORPH:** glide between rack states A and B.
- **MOTION:** tempo-synced lanes (the 5 classic ones, or the shared timeline).
- **Macro:** one knob that moves many.
- **Ghost / puck / orbit:** the spirit board's markers and the puck's automatic movement.
- **Capture:** a saved clip of Ghost Box's own output.
- **DNA:** breed / mutate / evolve whole chains.
- **Focus:** pick an intention, hear four starter chains.
- **Ghostprint:** extract the gesture of a reference as motion lanes.
- **G-SPACE:** the visual field that moves a source through a space.
- **Branch:** a path of units in GHOST SPLIT routing.
- **Fixed latency:** a constant reported delay so the host's delay compensation does not change.
