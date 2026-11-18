# GHOSTY PRO

**GHOSTY PRO** is a big, friendly wavetable synth with ghost pads, an on-screen keyboard, a full modulation system and a built-in rack of 21 effects.

- Version: 3.1.0
- Formats: VST3, AU (Mac), CLAP, Standalone
- Type: instrument (it plays MIDI notes and makes sound; it has no MIDI output)

This manual is written for a producer who has just installed the plugin in FL Studio. It also works for Ableton Live, Logic, Reaper and other hosts.

> **No factory sounds yet.** This version comes without factory sound presets. A new hand-made sound set is coming in a later update (free for everyone who owns Ghosty Pro). Until then every new instance starts at **INIT**, a warm, plain starting sound, and BROWSE lists your own saved sounds and snapshots. Older projects keep their sounds: everything is stored in the project. The effect presets and scenes in the FX tab are still there.

## Contents at a glance

1. QUICK START
2. THE SCREEN (header, tabs, STUDIO front page, every tab)
3. WORKFLOWS
4. FL STUDIO TIPS
5. THE LOOKS
6. DEMO VS LICENSED
7. SHORTCUTS, TROUBLESHOOTING, FAQ
8. GLOSSARY

---

# QUICK START

## Make your first sound in 2 minutes

1. Add **Ghosty Pro** to a channel in FL Studio (Add > Ghosty Pro). The window opens in the **STUDIO** layout: both oscillators, filter, envelopes, LFOs and effects on one page.
2. A new Ghosty Pro starts at **INIT** (the sound name box shows "Init"). There are no factory sounds yet, so you build the sound yourself: the next steps show how.
3. Play. Use your MIDI keyboard, or the on-screen keys at the bottom of the window.
4. Open the sound up. Turn **CUTOFF** in the **FILTER** card, or drag in the filter picture (left and right = CUTOFF, up and down = RES).
5. Drag one of the four ghosts on the big XY pad: **TONE**, **MORPH**, **SPACE** or **HEAT**. Each ghost moves two knobs at once.
6. Turn the four **MACRO** knobs at the bottom right, or wire them to knobs of your own (see the MACROS card).
7. Pick a **KEY** and a **SCALE** in the **PLAY** row, so every key you press lands on a good note. Turn **ARP** on for an instant pattern.
8. Like it? Click **SAVE**, type a name, and press Save. It is now one of your sounds: in BROWSE, in LOAD, on the **<** **>** arrows and in your host's program list. For a quick copy while you tweak, click **SNAPSHOT** in the top bar.
9. Click the **look button** (top right) to change the colours: BLACK, PURPLE, LIGHT or GHOST.

> Tip: lost? Click **INIT** in the top bar. It gives you a plain, warm starting sound.

---

# THE SCREEN

## The top bar (every page)

| Control | What it does |
|---|---|
| Wordmark and **PRO** badge, version number | The plugin name. The small number under it is the version. |
| **<** and **>** | Previous / next of your saved sounds. If a BROWSE filter is set, they walk inside that filtered list. They are greyed out until you have saved a sound. |
| **Sound name box** | Shows the current sound and its category. Click it to open BROWSE. Click the **star** at its left to make the sound a favourite. Hover for its tags. |
| **SAVE** | Saves the sound under a name you type (every knob, both drawn waves, a sample and the effects). It asks before replacing a sound with the same name. |
| **LOAD** | Opens a list of **YOUR SOUNDS**, plus **Load a sound file...** and **Open sounds folder**. |
| **INIT** | A plain starting sound (a new instance starts here). It also clears modulation routes and the bend lane. |
| **SNAPSHOT** (with a small list arrow, and **<** name **>** in STUDIO) | One click saves the sound as it is right now into the SNAPSHOTS bank. Keep tweaking and click again. The arrow opens the list: load, star, rename, delete, undo delete, show the folder. |
| **UNLOCK** / **LICENSED** | Only appears in the demo build and after you unlock. See DEMO VS LICENSED. |
| **CLEAN** (with a small arrow, a level bar and an LED) | Keeps layers, unison and effects clear and never clipping. See the CLEAN card below. |
| **LAYOUT** (STUDIO only) | Opens the LOOK and LAYOUT menu. |
| **Look button** | Cycles the look. It shows the name of the NEXT look. Right-click for the LOOK and LAYOUT menu. |
| **BYPASS** | Mutes the plugin with a fade. (This is the same button in every GhostBlock plugin.) |
| **VOICES n  a / b UNI** | Notes sounding now, and unison voices playing / the most there can be. Hover for the full explanation. |
| Small level bar | The output level. |
| **MORPH ON - knobs follow the board** | Appears while the MORPH board is on, to tell you why knobs move by themselves. |

### The tabs

Under the top bar are seven tabs, in this order: **SYNTH**, **MOD**, **BEND**, **TRAP**, **MORPH**, **FX**, **BROWSE**. In the STUDIO layout the **SYNTH** tab is the big front page described next.

## The STUDIO front page (the SYNTH tab)

STUDIO is the default layout. Everything you need for a sound is on one page, in three columns.

### Left column: OSC 1 and OSC 2

Each oscillator has a panel with the same controls.

| Control | What it does | Range / default |
|---|---|---|
| **OSC 1 / OSC 2 table menu** | Which wavetable or engine the oscillator plays. 20 wavetables, then 13 engines (listed below). | OSC 1 default: Saw. OSC 2 default: Classic. |
| **EDITOR** | Opens the WAVETABLE EDITOR in its own window. | |
| **MORE** | Opens the pop-down: UNISON, WARP, SPECTRAL and ENGINE settings (see below). | |
| **ON** | Turns the oscillator on or off. | OSC 1 on, OSC 2 off |
| Wave display | Shows the table as a 3D stack. For GHOST A/B it shows a flat curve you can draw on. | |
| **A / B shape menus** | The shapes used by GHOST A/B: frame A (POS 0 %) and frame B (POS 100 %). Picking one switches the oscillator to GHOST A/B. | A = Saw, B = Square |
| **DRAW A / DRAW B** | Choose which frame you draw on the display. Drag to draw; hold **SHIFT** for a straight line; right-click for undo, redo, smooth strokes, free drawing, stepped (16 or 32 steps), mirror, reset, copy A to B / B to A. | |
| **DICE** | A random wave into the frame you are drawing. | |
| **WAV** | Turn a sample into a wavetable. You can also drop a WAV on the display. WAV, AIFF, FLAC and OGG are accepted. | |
| **WAVE PALETTE** | One small picture per table and engine, plus a dice tile. Click one and the oscillator plays it. Hover to preview it on the display. | |
| **POS** | Position in the wavetable (which frame you hear). With GHOST A/B it morphs frame A into frame B. | 0 to 100 %, default 0 |
| **LEVEL** | Oscillator volume. | 0 to 100 %, default 80 % |
| **OCT** | Octave shift. | -3 to +3, default 0 |
| **SEMI** | Semitone shift. | -12 to +12, default 0 |
| **FINE** | Fine tune. | -100 to +100 cents, default 0 |
| **UNISON** | Number of stacked voices per note. | 1 to 8, default 1 |
| **DETUNE** | How far apart the unison voices are tuned. | 0 to 100 %, default 25 % |
| **SPREAD** | How wide the unison voices sit in the stereo field. | 0 to 100 %, default 60 % |
| **BLEND** | Level of the side voices compared with the centre voice. | 0 to 100 %, default 75 % |

> Tip: touching any knob of an oscillator, or clicking anywhere in its panel, shows that oscillator in the display.

**The table menu.** The 20 wavetables are: Ghost A/B, Ghosty Shapes, Classic, Saw, Pulse, Harmonics, Hollow, Formant, Choir, Organ, Glass, FM Growl, FM Metal, Fold, Sync, PD Res, Dirty, Digital, Sample, User. "User" is the table you make in the WAVETABLE EDITOR. The 13 engines are listed in the ENGINES section.

**The A / B shapes.** Sine, Triangle, Saw, Square, Pulse, Organ, Glass, Growl, Digital, Vox, Ahh, Ooh, Sync, Fold, Hollow, Bright.

#### MORE: UNISON, WARP, SPECTRAL, ENGINE

Click **MORE** on an oscillator. The pop-down title ends with the name of the engine if one is playing.

| Item | What it does | Choices / default |
|---|---|---|
| **DETUNE MODE** | How the unison voices are spread in pitch. | Linear, Super, Exp, Inv, Random. Default Super. |
| **STACK** | Adds extra layers of the note. | Off, 12, 12+7, Center -12, Center -24. Default Off. |
| **UNISON LAW** | Classic is the older behaviour. V2 makes side voices lose their sub and adds a slow drift. | Classic, V2. Default V2. (Shared by both oscillators.) |
| **WARP 1**, **AMOUNT 1**, **WARP 2**, **AMOUNT 2** | Two warps in series, each with an amount. | Default Off, amount 0 |
| **SPECTRAL**, **AMOUNT** | Reshapes the spectrum of the table. | Default Off, amount 0 |
| Engine knobs | Three knobs, named for the engine (see below). | 50 % |
| **SOURCE** | For TEXTURE and RESONATOR: what feeds them. | Other OSC, or any wavetable |
| TEXTURE only: **SIZE**, **PITCH**, **FEEDBACK**, **FREEZE** | Settings of the granular cloud. | SIZE 50 %, PITCH -24 to +24 st, FEEDBACK 0, FREEZE off |
| RESONATOR only: **POSITION**, **MODEL** | Where the resonator is excited, and its type. | POSITION 50 %, MODEL Modal or String |

**The warp modes:** Off, Sync, Bend +, Bend -, Squeeze, Asym, Mirror, Quantize, Formant, Pulse Width, FM Osc, FM Sub, FM Noise, PD Osc, PD Sub, PD Noise, AM Osc, AM Sub, AM Noise, RM Osc, RM Sub, RM Noise, Tube, Zero-Square. The "Osc / Sub / Noise" modes use the other oscillator, the sub or the noise as the modulator.

**The spectral modes:** Off, Smear, Harmonic Scale, Inharmonic, Phase Disperse, Skew, Random Amps, Low Pass, High Pass, Shepard.

#### The 13 engines

Engines sit in the table menu after the wavetables. Every engine has three controls, named for what they do on that engine. TEXTURE and RESONATOR process another signal (the SOURCE); RESONATOR plucks itself when it has no source.

| Engine | Its three controls |
|---|---|
| VA | DETUNE, SQUARE, SAW |
| WAVESHAPE | SHAPE, FOLD, ASYMMETRY |
| FM 2-OP | RATIO, INDEX, FEEDBACK |
| ADDITIVE | BUMPS, CENTRE, SLOPE |
| GRAIN FORMANT | FORMANT 2, FORMANT, WIDTH |
| SWARM | SPREAD, DENSITY, LENGTH |
| STRING | INHARM, BRIGHT, DECAY |
| MODAL | MATERIAL, BRIGHT, DECAY |
| CHORD | CHORD, INVERSION, WAVE |
| SPEECH | VOICE, SPECIES, VOWEL |
| PARTICLE | SCATTER, DENSITY, FILTER |
| TEXTURE | POSITION, TEXTURE, DENSITY |
| RESONATOR | STRUCTURE, BRIGHT, DAMPING |

For other tables the three controls are called HARMONICS, TIMBRE and MORPH.

### SUB / NOISE / OSC MOD card (bottom left)

| Control | What it does | Range / default |
|---|---|---|
| **SUB** | Level of the sub oscillator. | 0 to 100 %, default 0 |
| Sub octave menu | One or two octaves under the note. | -1 oct, -2 oct (default -1 oct) |
| Sub shape menu | Shape of the sub. | Sine, Triangle, Square (default Sine) |
| **NOISE** | Level of the noise. | 0 to 100 %, default 0 |
| **COLOUR** | Tone of the noise. With a noise type other than Classic it becomes a filter: left = low pass, right = high pass. | 0 to 100 %, default 60 % |
| **MORE** (noise) | **TYPE**: Classic, White, Pink, Brown, Blue, Violet, Geiger (clicks), Breath (follows the note). **STEREO**: mono to fully wide. | TYPE default White, STEREO 100 % |
| OSC MOD menu | How OSC 1 bends OSC 2. | Off, FM, Sync, Ring (default Off) |
| **AMOUNT** | How strong that bend is. | 0 to 100 %, default 0 |

### Centre column: FILTER, ENVELOPES, LFOs

**FILTER card**

| Control | What it does | Range / default |
|---|---|---|
| Type menu | The filter type. | LP 12, BP, HP 12, Notch, LP 24, HP 24, Comb +, Comb -, Formant, Off. Default LP 12. |
| Filter picture | Drag left and right = **CUTOFF**, up and down = **RES** (SHIFT = fine), mouse wheel = **DRIVE**, double-click = reset. With keyboard focus the arrow keys do the same. | |
| **CUTOFF** | Filter frequency. | About 30 Hz to 20 kHz, default fully open |
| **RES** | Resonance. | 0 to 100 %, default 0 |
| **DRIVE** | Saturation in the filter. | 0 to 100 %, default 0 |
| **ENV** | How much the FILTER envelope moves the cutoff. | 0 to 100 %, default 0 |
| **KEY** | Key tracking: higher notes open the filter more. | 0 to 100 %, default 0 |
| **VEL** | Velocity to filter. | 0 to 100 %, default 0 |
| **MORE** | **DRIVE LAW**: Classic, or Inside (the drive happens inside the filter). **CLEAN MODE**: shows the filter curve without the saturation. | Default Inside, CLEAN MODE off |

**ENVELOPES card.** Four small tiles choose which envelope the graph shows: **AMP**, **FILTER**, **ENV 2**, **ENV 3**. Drag the handles: ATTACK (the peak), DECAY and SUSTAIN (the corner), RELEASE (the end). Drag a slope up or down for **CURVE**. Double-click a handle to reset it. You can also drag a tile onto any knob, so that envelope modulates the knob (drag its ring to set the amount).

| Envelope knob | Range / default |
|---|---|
| **ATTACK** (AMP) | 1 ms to 8 s, default 3 ms |
| **DECAY** (AMP) | 5 ms to 8 s, default 400 ms |
| **SUSTAIN** (AMP) | 0 to 100 %, default 100 % |
| **RELEASE** (AMP) | 5 ms to 10 s, default 200 ms |
| **F.ATK, F.DEC, F.SUS, F.REL** (FILTER envelope) | 5 ms, 400 ms, 0 %, 400 ms |
| ENV 2 and ENV 3 attack / decay / sustain / release | 5 ms, 600 ms, 0 %, 400 ms |
| **CURVE** (one knob for every envelope) | 0 to 100 %, default 50 %. Straight to strongly exponential decay and release. |

**LFO 1 / LFO 2 card.** Each LFO has a small picture: drag up and down for **RATE** (with SYNC on, for the division), double-click for the next **SHAPE**. With keyboard focus, up and down = rate and space = next shape.

| Control | What it does | Range / default |
|---|---|---|
| Shape menu | Sine, Triangle, Saw Up, Saw Down, Square, S&H, Smooth | Sine |
| Division menu | Used when SYNC is on: 4/1, 2/1, 1/1, 1/2, 1/4, 1/8, 1/16, 1/32, 1/2 T, 1/4 T, 1/8 T, 1/16 T, 1/4 D, 1/8 D | 1/4 |
| **SYNC** | The rate follows the host tempo. | Off |
| **RETRIG** | On: restarts with every note. Off: one free-running LFO for all voices. | Off |
| **RATE** | Speed (greyed out while SYNC is on). | 0.02 to 30 Hz, default 2 Hz (LFO 1), 0.5 Hz (LFO 2) |
| **FADE IN** | Time for the LFO to fade in after a note starts. | 0 to 4 s, default 0 |

Two more LFOs (LFO 3 and LFO 4) live on the MOD tab's ROUTES page.

### Right column: the XY pad, MORPH strip, GHOST FX, VOICE, MACROS

**The XY pad.** Four ghosts, each moving two knobs when you drag it:

| Ghost | Left / right | Up / down |
|---|---|---|
| **TONE** | CUTOFF | RES |
| **MORPH** | OSC POS | OSC DETUNE |
| **SPACE** | ECHO | REVERB |
| **HEAT** | DRIVE | SUB level |

Three buttons above the pad, **OSC 1 / OSC 2 / BOTH**, choose which oscillator the MORPH ghost moves. **AUTO** makes the ghosts drift slowly around where you left them (they move knobs; they are not LFOs or envelopes). Click a ghost without dragging to see which two knobs it moves.

**MORPH strip.** **MORPH ON / MORPH OFF** turns the MORPH board on or off. **BOARD** opens the MORPH tab. The small **<** name **>** buttons step through your snapshots; the name button opens the snapshot list.

**GHOST FX / OUTPUT card.** Eight knobs for the effects built into the synth. All start at 0 except VOLUME.

| Knob | What it does | Default |
|---|---|---|
| **DRIVE** | Warm saturation | 0 % |
| **CRUSH** | Bit-crush grit | 0 % |
| **CHORUS** | Chorus | 0 % |
| **PHASER** | Phaser | 0 % |
| **ECHO** | A dotted-eighth ping-pong echo | 0 % |
| **REVERB** | Reverb amount. With the Ghost type, 100 % is about 35 % wet, so it sits behind the sound. | 0 % |
| **HAUNT** | A dark, wobbly long echo | 0 % |
| **VOLUME** | Output level. | 0 dB (range from silence to +12 dB) |

Two small buttons: **FX RACK** opens the FX tab. **MORE** opens the reverb settings: **TYPE** (Classic is the old reverb, Ghost is the new one; default Ghost), **SIZE** (60 %), **DECAY** (0.2 to 12 s, default 1.6 s), **PRE-DELAY** (0 to 200 ms, default 12 ms), **LO CUT** (20 to 1000 Hz, default 180 Hz), **HI CUT** (1 to 20 kHz, default 9 kHz), **DUCK** (the sound pushes the tail down while it plays, default 0) and **WIDTH** (default 100 %).

**VOICE card**

| Control | What it does | Range / default |
|---|---|---|
| **VEL** | Velocity to volume | 0 to 100 %, default 50 % |
| **GLIDE** | Slide between notes | 0 to 2 s, default 0 |
| **VOICES** | Most notes at once | 1 to 16, default 16 |
| **BEND** | Pitch-wheel range | 1 to 24 semitones, default 2 |
| **MONO** | One note at a time | Off |
| **LEGATO** | With MONO: a note played over another glides on without restarting the envelopes | Off |
| **MORE** | **QUALITY**: Draft (1x), High (2x), Ultra (4x) oversampling, default High. **ULTRA ON RENDER**: lifts an offline render to Ultra (default on). **LOCK**: keeps the quality when a sound loads. | |

In the CLASSIC layout the same **MORE** also holds **1 PER NOTE** (playing a key again takes over its own voice instead of stacking a second one) and **STEAL** (which voice a new note takes when all VOICES are busy: Newest, Oldest, Highest, Lowest, Quietest; default Oldest; released voices always go first).

**MACROS card.** Four knobs, **MACRO 1** to **MACRO 4** (0 to 100 %, default 0). A sound can rename them for what they do (for example BRIGHTNESS) and wire each one to several knobs at once. Small tiles above them (M1 to M4) can be dragged onto any knob. **MOD MATRIX** opens the MOD tab.

### The play row (bottom of every page)

| Control | What it does | Choices / default |
|---|---|---|
| **KEY** | The key of your music. | C to B, default A |
| **SCALE** | When a scale is chosen, every key you press is moved to the nearest note in the scale. | Off (all notes), Major, Minor, Penta, Minor Penta, Blues, Dorian, Mixolydian, Lydian, Phrygian, Harmonic Minor, Japanese, Arabic. Default Off. |
| **CHORD** | One key plays a chord. | Single notes, Triads, 7th chords, Power chords. Default Single notes. |
| **ARP** | Arpeggiator on / off. A small light flashes on every step. | Off |
| Arp rate | Step length. | 1/4, 1/8, 1/16, 1/32, 1/8 T, 1/16 T. Default 1/16. |
| Arp order | | Up, Down, Up + Down, Random, As played. Default Up. |
| Arp range | | 1 octave, 2 octaves, 3 octaves. Default 1 octave. |
| **ARP GATE** | How long each arp note is held. | 0 to 100 %, default 60 % |
| **HOLD** | Notes you play on the FLOW strip keep sounding after you let go (they feed the ARP too). Play a held note again to let it go; switch HOLD off to let them all go. Not saved in sounds. | Off |
| **OCT - / +** | The FLOW strip plays an octave lower or higher. Not saved. | 0 |
| **FLOW** (STUDIO) | Switches the bottom strip between the on-screen **KEYS** and the **FLOW** strip. | Off (KEYS shown) |

In the CLASSIC layout the play row shows only the FLOW strip.

**KEYS** is a piano keyboard: five octaves plus a C, starting at C2 (OCT moves it). Keys glow while a note sounds, whether it came from you, your MIDI keyboard or the arp.

**FLOW strip.** A strip of coloured bars, one per note of your scale. Click and drag across it to slide through the scale. The bar for the root note has a bright top edge. If a layer uses KEY SPLIT, a marker shows where the split is.

## The CLASSIC layout (SYNTH tab)

If you pick CLASSIC in the LOOK and LAYOUT menu, the SYNTH tab is laid out differently but has the same controls. Left side: the oscillator display with **OSC 1 / OSC 2** buttons (choose which one it shows), the table menu, **IMPORT WAV**, **EDITOR** and **DICE**; the A / B shape menus with **DRAW A / DRAW B**; the **OSC 1** and **OSC 2** cards (with **MORE** and **ON**); and the **SUB / NOISE / OSC MOD** card. Right side: the XY pad, the **FILTER** card (with the ten filter knobs CUTOFF, RES, DRIVE, ENV, KEY, VEL, F.ATK, F.DEC, F.SUS, F.REL), the **AMP / VOICE** card (ATTACK, DECAY, SUSTAIN, RELEASE, CURVE, VEL, GLIDE, VOICES, BEND, MONO, LEGATO) and the **GHOST FX / OUTPUT** card. The play row has only the FLOW strip (no on-screen piano keys). The MOD tab then holds the LFOs and envelopes, and the FX tab is the rack. The old window is 1140 by 866 points; STUDIO is 1420 by 940.

## MORE panels in short

Every **MORE** button opens a small pop-down panel next to it: oscillators (UNISON, WARP, SPECTRAL, ENGINE and the WAVE PALETTE), noise, filter drive, reverb, and the voice card's QUALITY and VOICING. Close it by clicking away.

---

## The MOD tab

Everything that moves by itself or by you: LFOs, envelopes, the mod matrix, macros, and a router of up to 64 routes.

**The two ways to modulate.**

- **Drag a source onto a knob.** The source tiles on the MOD tab header (LFO 1, LFO 2, ENV 2, ENV 3, ENV 1, M1 to M4) can be dragged onto any knob in any tab. Hold the tile over a tab name and that tab opens. A ring appears around the knob. **Drag the ring up or down to set the amount** (double-click the ring for 0).
- **Right-click a knob.** The knob menu has **Type a Value...**, **Mod Source** (pick a source), and, for a knob with routes: auxiliary source, curve, **Edit Custom Curve...**, **Bipolar**, **Bypass Modulator**, **Remove Modulator**, **Remove All Modulators**, **Reset Control** and **MIDI Learn** (move a controller on your keyboard; a learned CC moves that knob).

**Sources:** ENV 1 (AMP), ENV 2, ENV 3, MOD ENV (FILTER), LFO 1 to LFO 4, MACRO 1 to MACRO 4, VELOCITY, NOTE, AFTERTOUCH, MOD WHEEL (MIDI CC 1), PITCH BEND, RANDOM. In the older eight-slot matrix, "Bend Lane" is also a source.

**How a route sounds.** The knob moves by AMOUNT times the source, as a share of the knob's whole travel (like turning it by hand). A route can have a **CURVE** (Linear, Exp, Log, S, Custom with 5 drawn points), an **AUX** source that multiplies it (for example macro times velocity), **BIPOLAR** (swings both ways around the knob), and **BYPASS** (kept but not heard).

### The cards of the MOD tab

| Card | Contents |
|---|---|
| **LFO 1, LFO 2** | A live scope, shape, division, SYNC, RETRIG, RATE, FADE IN (same as on STUDIO). |
| **ENV 2, ENV 3** | A graph and ATTACK, DECAY, SUSTAIN, RELEASE. |
| **MOD MATRIX  source > target / amount** | Eight slots. Each has a source menu, a target menu and an amount (-100 % to +100 %, double-click for 0). A small ghost carries the signal between source and target and a bar shows the live value. |
| **ENV 1 / AMP** | A picture of the amp envelope. Edit it on the SYNTH tab. |
| **MACROS** | The four MACRO knobs, named by the sound. Double-click returns a knob to its default (the middle for a two-way macro). |
| **ROUTES >** button | Opens the ROUTES page. |

The matrix **targets** are: Pitch, Fine Pitch, Osc 1 / 2 Pitch, Osc 1 / 2 Pos, Osc 1 / 2 Level, Osc 1 / 2 Detune, Unison Spread, X-Mod, Sub Level, Noise Level, Cutoff, Resonance, Filter Drive, Volume, Pan, LFO 1 / 2 Rate, Growl, Character, Tape Amount, Layer 2 / 3 Level, Sample Start, Osc 1 / 2 Warp 1 / 2, and Osc 1 / 2 Engine H / T / M.

### The ROUTES page

Click **ROUTES >**. You get every source as a drag tile, **LFO 3** and **LFO 4** (shape, division, SYNC, RETRIG, RATE, FADE IN, DEPTH), and a list headed **MOD ROUTES n / 64**. Each row shows SOURCE, TARGET (drag its handle onto another knob to move the route), AMOUNT, CURVE, AUX, a bipolar toggle, a bypass toggle and a remove button. **CLEAR ALL** removes every route of the sound. **< MOD PAGE** goes back.

---

## The BEND tab

**BEND LANE:** draw a line that bends the pitch of every voice, in time with the beat. Up an octave, down an octave, or anywhere between. It repeats every 1/2, 1, 2 or 4 bars.

| Control | What it does | Choices / default |
|---|---|---|
| Lane picture | Click = add a point. Drag = move it. Right-click or double-click = delete. Drag a diamond = curve the line. Double-click a diamond = step (a jump). Hold **ALT** = no snap. | |
| **ON** | Bend lane on / off | Off |
| **SHAPES...** | Factory shapes: Up & back, Up-up-down, Octave flick, Drop & return, Wobble 1/8, Rise 4 bars, Dive end of bar, Trill, Slide down, Stair up. Picking one also sets length and range and turns the lane on. | |
| Length menu | | 1/2 BAR, 1 BAR, 2 BARS, 4 BARS. Default 1 BAR. |
| Range menu | How far the top and bottom of the lane bend. | +/-2 ST, +/-7 ST, +/-12 ST, +/-24 ST. Default +/-12 ST. |
| Mode menu | **SYNC**: locked to the bar. **RETRIGGER**: the lane starts over on every new note. | SYNC |
| **SNAP** menu | What new or moved points snap to. | SNAP OFF, SEMITONE, 5TH + OCT, OCTAVE |
| **GRID** menu | The time grid for the points. | GRID 1/4, 1/8, 1/16, 1/32 |
| **AMOUNT** | Scales the whole lane. | 0 to 100 %, default 100 % |
| **PITCH NOW** | A ghost floats up and down with the live bend and shows it in semitones. | |

The lane follows the song's bars; when the host is stopped it free-runs at the song tempo.

---

## The TRAP tab

Three tools for big modern sounds: synth brass, saturation, tape, and a three-layer stack with a sample.

### SYNTH BRASS

A scoop into every note, growl, a horn body and a fall. The **BRASS OFF / BRASS ON** button is a quick start: click once and it sets sensible values (your oscillators stay); click again and the brass knobs go back to zero.

| Knob | What it does | Range / default |
|---|---|---|
| **SCOOP** | Pitch scoops up into every note | 0 to 100 %, default 0 |
| **SCOOP TIME** | How long the scoop takes | 10 to 500 ms, default 80 ms |
| **GROWL** | Fast pitch and filter growl; velocity makes it angrier | 0 to 100 %, default 0 |
| **FORMANT** | A resonant body peak (the horn bell) | 0 to 100 %, default 0 |
| **FORM FREQ** | Where the body peak sits, 400 Hz to 4 kHz | 0 to 100 %, default 45 % |
| **FALL** | Pitch falls away after the key is let go, over the release | 0 to 100 %, default 0 |

### CHARACTER

One knob: saturation that turns into clipping, with the low mids cleaned up and the level held steady. 0 to 100 %, default 0.

### TAPE / SLOWED

The **TAPE** switch adds wow and flutter, tape saturation, a high-end roll-off and a pitch-down on everything.

| Knob | What it does | Range / default |
|---|---|---|
| **AMOUNT** | 0 = a warm tape, 1 = a worn one | 0 to 100 %, default 50 % |
| **SLOW** | The slowed edit's pitch-down | 0 to -12 semitones, default -3 |

### STACK: three layers

Layer 1 is the sound on the SYNTH and MOD pages (called MAIN). Layers 2 and 3 can each play the SAMPLE. Factory layer sounds come back with the new sound set; an older project or saved sound that plays a factory sound as a layer still plays it (the menu shows it ticked).

| Control (per layer) | What it does | Range / default |
|---|---|---|
| Source menu (layers 2, 3) | What the layer plays | Off, Sound, Sample. Default Off. |
| Sound button | "pick a sound..." opens a menu by category | |
| **OCT** | Octave | -3 to +3, default 0 |
| **DETUNE** | Detune | -100 to +100 cents, default 0 |
| **LEVEL** | Level | 0 to 100 %, default 100 % (layer 1) or 80 % (layers 2, 3) |
| **PAN** | Stereo position | -100 to +100 %, default 0 |
| **DELAY** | The layer comes in late | 0 to 1 s, default 0 |
| **REV** | A sample plays backwards; a sound swells in over its decay and is cut off | Off |
| Play menu | Which notes the layer plays | All, Vel Low, Vel High, Key Low, Key High |
| **S** / **M** | Solo / mute the layer (not saved with the sound) | |
| **FX** (layers 2, 3) | The layer keeps its own Ghost FX and CHARACTER. Off = the layer is dry and the FX tab shapes the sum. | Off |
| A readout | voices and CPU use of the layer | |

Below the layers: **VEL SPLIT** (velocity below this is VEL LOW, from here up VEL HIGH; 1 to 127, default 80) and **KEY SPLIT** (keys below this are KEY LOW; 0 to 127, default 60).

### SAMPLE

A one-shot sample played across the keys, pitched with its formants kept. Drop a WAV, AIFF, FLAC, MP3 or OGG anywhere on the page, or press **IMPORT**. **CLEAR** removes it. Drag the two markers on the picture to set START and END; click the picture to hear the sample at its root.

| Control | What it does | Range / default |
|---|---|---|
| **ROOT** | The note the sample is at (found automatically on import; set it by ear if it had no clear pitch) | 0 to 127, default 60 |
| **FINE** | Fine tune | -100 to +100 cents |
| **START**, **END** | The part that plays | 0 to 100 %, defaults 0 and 100 % |
| **ATTACK**, **DECAY**, **SUSTAIN**, **RELEASE** | The sample's own envelope | 2 ms, 1 s, 100 %, 300 ms |
| **REV VEL** | Notes played at or above this velocity play the sample backwards (0 = off) | default 0 |
| **KEEP FORMANTS** | Pitch the sample without the chipmunk or giant effect | On |
| **LOOP** | Loop the START to END region while the key is held | Off |

> Tip: a Random to Sample Start route in the mod matrix chops the sample differently on every note.

---

## The MORPH tab

The MORPH board blends whole sounds. Ghosts sit on a 2D board; each one holds a complete sound. A glowing **puck** blends the sounds of the ghosts it is near.

- **Click an empty spot** on the board: a new ghost appears holding the CURRENT SOUND (up to 12 ghosts). The board turns on by itself.
- **Right-click a ghost:** Set from... (CURRENT SOUND or a snapshot), Rename..., Solo (puck onto it), Remove.
- **Drag a ghost** to move it. **Drag the puck** to morph. **Double-click a ghost** to hear it alone.

| Control | What it does |
|---|---|
| **MORPH ON** | The board plays (the puck blends the ghosts' sounds). Off: the sound stays as it is and the knobs are yours again. |
| **RANDOM BOARD** | Scatter the ghosts over the board |
| **ARRANGE IN CIRCLE** | Put the ghosts on a circle around the middle |
| Orbit menu | **ORBIT off**, Circle, Figure 8, Random walk: the puck circles around where you put it |
| **SYNC** | The orbit rate is in cycles per beat (off: per second) |
| **RATE** | Orbit rate, 0.03 to 4 |
| **DEPTH** | How far from the puck the orbit goes, 0 to 0.5 |
| **SAVE**, **LOAD** | Save the board (ghosts and their sounds) as a board file, or load one |

Smooth knobs blend as you move the puck; switches, tables and drawn waves come from the nearest ghost. The puck position (Morph X, Morph Y) can be automated by the host. Boards are saved in your own data folder (see FL STUDIO TIPS).

---

## The FX tab (the Ghost FX rack)

A rack of 21 effects in one chain. It runs after the synth's own GHOST FX knobs. Only the limiter is on at the start.

**The strip at the top.** One tile per effect, left to right in signal order. The tile shows its position in the chain when it is on, and is greyed out when it is off.

- **Click a tile** to open that effect's page.
- **Click the light** on a tile (or double-click the tile) to switch the effect on or off.
- **Drag a tile** to change the order (the limiter always stays last). You can also drag the page title onto the strip.
- **Right-click a tile:** CHAIN ORDER menu: Reset order, Move to the start, Move to the end (before the limiter).

**Top bar of the page:** a **PRESET** menu (presets for that effect only), a **SCENES** menu (whole-rack setups: every effect, its order and its patterns), **SAVE SCENE**, and **UNDO** (puts the rack back as it was before the last scene or preset). Each page has a **MIX** knob (named **DEPTH** on SQUEEZE).

| Tile | Effect | What it does |
|---|---|---|
| GATE | GHOST GATE | 16 drawable volume and time patterns per bar |
| GRID | GHOST GRID | Paint effects onto a one-bar step grid |
| HAUNT | HAUNT EDIT | Hold a pad for tempo-synced stutter gestures |
| WRECK | WRECKER | Analog saturation from warm to slammed |
| TUBE | TUBE GLOW | Vintage tube console warmth |
| CRUSH | SOUL CRUSH | Level-smashing grit compressor |
| EQ | SPIRIT EQ | Precision dynamic EQ over a live analyser |
| FILTER | PHANTOM FILTER | Resonant multimode filter with a rhythm LFO |
| CHORUS | CHORUS | Three-voice ghost chorus (or four taps) |
| CRYSTAL | CRYSTAL | Reverse and pitched granular splices |
| ECHO | ECTO ECHO | Tape, analog, BBD or digital delay |
| SPACE | SPECTRAL SPACE | Blendable ambience with glimmering tails |
| SHIMMER | SHIMMER | Lush octave-shimmer reverb with freeze |
| SQUEEZE | GHOST SQUEEZE | 3-band upward and downward compression |
| FATTEN | FATTEN | One big knob: turn it and it gets fatter |
| HYPER | HYPER | 1 to 7 micro-delay chorus voices, or DIMENSION (4 moving lines) |
| DIST | DISTORTION | 10 curves, 4x oversampled, filter before or after |
| COMP | COMPRESSOR | Single or 3-band compression; top RATIO is a limiter |
| SPLIT | SPLITTER | Split into bands and give each its own mini rack of up to 3 effects |
| TONE | TONE EQ | 3 quick bells and a tilt |
| LIMIT | GHOST LIMITER | Transparent lookahead limiter, no overs |

### Controls of each effect

Defaults are shown after each control. All effects start off, with the MIX shown (the limiter starts on).

**GHOST GATE** (MIX 100 %). A pattern editor: 16 patterns (PATTERN 1 to 16) of 16 steps. Click and drag to paint steps on or off. Brushes **ERASE**, **REP**, **STUT**, **HALF**, **REV**, **SCR**, **STOP** put time effects on steps. Buttons **SNAP**, **INVERT**, **CLEAR**, **RESET**. Knobs: **SMOOTH** (20 %), **TIME FX** (100 %), **LENGTH** (1/2 BAR, 1 BAR, 2 BARS; default 1 BAR).

**GHOST GRID** (MIX 100 %). Eight patterns on a 16 or 32 step grid (**STEPS**: 16 STEPS or 32 STEPS) with ten rows, each with a knob: **LOOP** (LENGTH), **STUTTER** (RATE), **SCRATCH** (DEPTH), **REVERSE** (AMOUNT), **TAPE STOP** (SPEED), **FILTER** (DEPTH), **CRUSH** (AMOUNT), **DELAY** (FEEDBACK), **VERB** (SIZE), **TONAL** (PITCH). **CLEAR** and **RESET** buttons.

**HAUNT EDIT** (MIX 100 %). Sixteen pads. Each pad has a gesture type: STUTTER, BUILD, GATE, GLITCH, REVERSE, TAPE STOP, LP SWEEP, HP SWEEP, CRUSH, PITCH DROP, and a SIZE (a tempo division). Hold a pad to play it. **MOTION**, **LATCH** and **SYNC START** (default on) change how pads start and hold.

**WRECKER** (MIX 100 %). Style menu: TAPE, TUBE, CONSOLE, TRANSISTOR, PENTODE, FUZZ, FOLD, CRUSH, DECIMATE. Knobs: **DRIVE** (30 %), **TONE** (-1 to +1), **LOW CUT** (10 to 1000 Hz), **HIGH CUT** (1 to 22 kHz), **OUTPUT** (-24 to +12 dB). Switches: **THUMP**, **STEEP**, **PUNISH**, **AUTO** (auto gain, default on).

**TUBE GLOW** (MIX 100 %). **DRIVE** (35 %), **BASS** and **TREBLE** (-12 to +12 dB), **OUTPUT** (-24 to +12 dB). Character: LINE or MIC.

**SOUL CRUSH** (MIX 60 %). **CRUSH** (50 %), **DARKNESS** (30 %), **RELEASE** (5 to 1000 ms, default 120 ms), **OUTPUT** (-24 to +12 dB).

**SPIRIT EQ** (MIX 100 %). Twelve bands, each with on, bypass, type (Bell, Low Shelf, High Shelf, Low Cut, High Cut, Notch, Band Pass, Tilt Shelf, Flat Tilt), slope (6 to 96 dB), frequency (10 Hz to 30 kHz), gain (-30 to +30 dB), Q, stereo placement (Stereo, Left, Right, Mid, Side), and a **Dynamic** mode with threshold, range, ratio, attack, release and detect (Band or Wide). Global: output (-24 to +24 dB), mode (Zero Latency, Linear Low, Linear Medium, Linear High), character (Clean, Subtle, Warm), gain scale, auto gain, phase invert. A live analyser sits behind the curve. Linear modes add latency, which the plugin reports to the host.

**PHANTOM FILTER** (MIX 100 %). Type: LOW PASS, BAND PASS, HIGH PASS, NOTCH. **SLOPE** 12 or 24 dB (24). **ROUTING**: SINGLE, SERIES, PARALLEL. **CUTOFF** (20 Hz to 20 kHz, default 1200 Hz), **RESO** (40 %), **DRIVE** (20 %), **SPREAD** (-4 to +4 octaves, default +1), **LFO AMT** (-1 to +1), **LFO SHAPE** (SINE, TRI, SAW UP, SAW DOWN, SQUARE, S&H, SMOOTH), **LFO SYNC** (on) with **LFO DIV**, **LFO HZ** (0.05 to 20), **ENV AMT** (-1 to +1), **ENV REL** (10 to 1000 ms, default 200 ms).

**CHORUS** (MIX 50 %). **RATE** (0.05 to 5 Hz, default 0.4), **DEPTH** (50 %), **DELAY** (2 to 30 ms, default 12), **FEEDBACK** (0), **WIDTH** (100 %). More: **TAPS** (3 VOICES or 4 TAPS), **DELAY 2** (2 to 30 ms, default 18), **WET LP** (1 to 20 kHz) and **WET HP** (20 Hz to 2 kHz).

**CRYSTAL** (MIX 40 %). **PITCH** (-24 to +24 st, default +12), **SPLICE** (20 to 600 ms, default 180), **DELAY** (0 to 1500 ms, default 250), **FEEDBACK** (40 %), **REVERSE** (50 %), **SPREAD** (60 %).

**ECTO ECHO** (MIX 30 %). Mode: TAPE, ANALOG, BBD, DIGITAL. **SYNC** (on) with **DIVISION**, or **TIME** (1 to 2000 ms, default 375). **FEEDBACK** (45 %), **CHARACTER** (50 %), **WIDTH** (100 %), **LOW CUT** (20 to 2000 Hz), **HIGH CUT** (1 to 20 kHz), **DUCKING** (0). **PING-PONG** (on).

**SPECTRAL SPACE** (MIX 30 %). **SIZE** (50 %), **BLEND** (60 %), **DECAY** (0.3 to 12 s, default 2.5), **TONE** (-1 to +1), **MOD** (40 %), **GLIMMER** (20 %), **PRE-DELAY** (0 to 250 ms, default 20), **WIDTH** (100 %), **DUCKING** (0).

**SHIMMER** (MIX 35 %). **SIZE** (70 %), **DECAY** (0.5 to 30 s, default 5), **DAMPING** (40 %), **SHIMMER** (50 %), **PRE-DELAY** (0 to 250 ms, default 30), **MOD** (50 %), **WIDTH** (100 %), **FREEZE** (off).

**GHOST SQUEEZE** (**DEPTH** 50 %). **TIME** (50 %), **UPWARD** (60 %), **DOWNWARD** (70 %), **INPUT** and **OUTPUT** (-12 to +12 dB).

**FATTEN** (MIX 100 %). The big knob **FAT** (40 %): 0 to 30 % glue and weight, 30 to 70 % warm drive, 70 to 100 % heavy clip. **COLOR** (-1 darker to +1 brighter), **CRUNCH** (20 %), **INPUT** (0 to 24 dB), **OUTPUT** (-12 to +12 dB), **PUNCH** (keeps the attack of hits clean), **LOW CLEAN** (the deep sub below about 90 Hz stays clean and mono). Mode: CLASSIC (the original), SUB (octave-down sub layer), BODY (a 140 Hz thump), SMASH (heavy parallel squash), SATURATE (warm tape-like drive), STACK (two micro-delayed copies, wide and thick), TRANSIENT (softer attack, longer tail), SAUSAGE (everything at once).

**HYPER** (MIX 50 %). Mode: HYPER or DIMENSION. **VOICES** (1 to 7, default 4), **DETUNE** (35 %), **RATE** (0.05 to 8 Hz, default 0.6), **WIDTH** (100 %), **SIZE** (40 %), **RETRIG** (on: restarts on each new note).

**DISTORTION** (MIX 100 %). Mode: TUBE, SOFT CLIP, HARD CLIP, DIODE, LIN FOLD, SIN FOLD, ZERO-SQUARE, DOWNSAMPLE, ASYM, X-SHAPER. Two modes are drawn by you: draw curve A (the sound at DRIVE 0) and curve B (the sound at full DRIVE); **SYMMETRIC** mirrors one half. **DRIVE** (0 to 36 dB, default 12), **LEVEL** (-24 to +12 dB), a filter (**OFF**, **PRE** or **POST**) with type, **FREQ** (20 Hz to 20 kHz), **Q** and key tracking. Every curve runs 4x oversampled with anti-aliasing, except DOWNSAMPLE which is a deliberate sample-and-hold.

**COMPRESSOR** (MIX 100 %). Mode: SINGLE or MULTIBAND (low / mid / high, each compressed down and / or up). **THRESH** (-60 to 0 dB, default -18), **RATIO** (1 to 20, default 4; the top shows LIMIT), **ATTACK** (0 to 100 ms, default 10), **RELEASE** (5 to 2000 ms, default 150), **MAKEUP** (0 to 36 dB), **KNEE** (0 to 24 dB, default 3), **KEY**: UNIT INPUT or RACK INPUT (the dry synth before any effect, so echoes and reverbs after it duck to your playing). Multiband has two crossovers (LOW 30 to 2000 Hz, default 150; HIGH 500 Hz to 16 kHz, default 2500).

**SPLITTER** (MIX 100 %). Mode: L / H, L / M / H, or M / S. Crossovers **LOW** (30 Hz to 2 kHz, default 200) and **HIGH** (500 Hz to 16 kHz, default 3000). Each band has a level (-24 to +12 dB) and three slots; each slot can hold an effect (with its own amount) that uses that effect's own settings. The splitter cannot hold itself, the limiter, GATE, GRID, HAUNT or SPIRIT EQ.

**TONE EQ** (MIX 100 %). Three bells: **LOW**, **MID**, **HIGH**, each with frequency, gain (-18 to +18 dB) and Q. **TILT** (-12 to +12 dB, tilts the whole sound around 1 kHz: right = brighter), **OUTPUT**.

**GHOST LIMITER** (always on by default). **CEILING** (-12 to 0 dB, default -0.3), **GAIN** (0 to 18 dB), **RELEASE** (1 to 500 ms, default 80). A gain-reduction readout shows its work.

---

## The BROWSE tab

All your sounds in one place: your own saved sounds and your snapshots. There are no factory sounds yet: a new hand-made sound set is coming.

- **Empty?** With nothing saved yet the list says "No factory sounds yet" with a **START FROM INIT** button. Make a sound from INIT and press **SAVE** (or **SAVE AS...** here); it shows up in the list.
- **Search box.** Type to search your sounds by name or tag (unison, fm, mono, reverb...). Several words must all match. Press **Enter** to load the first match.
- **CATEGORIES card.** All, Snapshots, Favourites, Your Sounds.
- **The list.** Click a sound to load it and hear it. The arrow keys walk the list. The star makes it a favourite.
- **SORT.** BANK ORDER, NAME, CATEGORY, RECENT.
- **TAGS.** Opens tag chips (derived from what each of your sounds does). Pick several to show only sounds that have all of them. **CLEAR** resets them; **DONE** closes. The button appears once your sounds have tags.
- **< and >** walk inside the filtered list; **RANDOM** picks a random sound from the list (never the one playing).
- **NOW LOADED card.** Shows the sound that is loaded.
- **Buttons:** **SAVE AS...**, **DELETE (your sound)**, **OPEN SOUNDS FOLDER**, **STAR / UNSTAR**.

Favourites and recently used are yours, not part of any sound. They are kept in `favourites.json` and `recent.json` next to your saved sounds.

---

## The WAVETABLE EDITOR

Press **EDITOR** on an oscillator. It opens in its own window titled "Ghosty Pro - WAVETABLE EDITOR". It makes the table called **User** that either oscillator can play.

- **Frame strip.** Click a frame to select it. **SHIFT-click** makes it a key frame (or not).
- **The big frame.** Drag to draw (SHIFT = straight line).
- **Harmonic bars** (drag = magnitude) and **phase bars** (drag = phase).

| Button / control | What it does |
|---|---|
| Name box | The table's name (the file name in the user bank) |
| **LOAD** | Load a table from the user bank, the sound's User table, what this oscillator plays, or import an audio file |
| **SAVE**, **SAVE AS** | Save into the user bank (overwrites / new name). Files end in `.ghostywt`. *Full version only.* |
| **IMPORT** | A WAV / AIFF / FLAC: a 2048-point wavetable file is taken as it is, anything else is cut into cycles (**CYCLE**, **FRAMES**) |
| **EXPORT WAV** | A float32 WAV with the frames one after another. *Full version only.* |
| **OSC PLAYS IT** | Switches the oscillator to the User table (off = back to the table it had) |
| **LIVE** | Pushes every edit into the synth as you work |
| **UNDO**, **REDO** | Ctrl or Cmd + Z, Ctrl or Cmd + Shift + Z |
| **NORMALISE**, **REMOVE DC**, **INVERT**, **REVERSE**, **SMOOTH**, **ALIGN** | Frame tools: peak to 1, remove offset, flip, play backwards, round the corners, rotate so the fundamental starts at 0 |
| **KEY FRAME** | Make the current frame a key frame (or not) |
| **MORPH: CROSSFADE**, **MORPH: SPECTRAL** | Fill the frames between key frames with a cross-fade, or in the spectral domain (harmonics glide) |
| **+ FRAME**, **- FRAME**, **CLEAR KEYS** | Insert a copy after the current frame; delete the current frame; forget the key frames |
| Recipe menus A and B | Sine, Triangle, Saw, Square, Noise, Odd harmonics, Even + 1, 1 / n, 1 / n squared, Bright |
| **GEN FRAME** | Recipe A into the current frame |
| **GEN TABLE** | A new table: recipe A morphing to recipe B over FRAMES frames |
| **FRAMES** | 1 to 256 (default 16): how many frames GEN TABLE and an import make |
| **HARMONICS** | 1 to 512 (default 64): how many partials the recipes use |
| **CYCLE** | The cycle length in samples a sample is cut at. 0 = detect the pitch. Press **RE-CUT** after changing it |
| **START FROM OSC** | Start from what this oscillator plays now |

---

## The CLEAN card

Click the small arrow next to **CLEAN** in the top bar. CLEAN keeps layers, unison and effects clear and never clipping: each layer gets its own place, the bass stays mono, and drives keep their loudness. It is on by default (and INIT turns it on).

| Knob | What it does | Range / default |
|---|---|---|
| **POCKET WIDTH** | How far layers 2 and 3 sit out to the sides (layer 2 leans left, layer 3 right). 0 = all centred. | 0 to 100 %, default 50 % |
| **LOW CUT** | Layers that don't carry the bass are cut below this, so only one layer owns the low end | 30 to 300 Hz, default 100 Hz |
| **MONO BELOW** | Everything below this is mono: a solid, club-safe bass. All the way down = off. | 0 to 300 Hz, default 120 Hz |
| **MUD** | Gently dips 150 to 400 Hz, only when layers pile up there | 0 to 100 %, default 50 % |
| **TRIM** | The sound's level trim: what a sound sets so sounds are equally loud. VOLUME is your own level on top. | -24 to +18 dB, default 0 |
| **MONO CHECK** | Listen in mono (not saved). With CLEAN on, the width backs off by itself if the mono sum would lose more than 3 dB. | Off |

**The output meter.** The bar next to CLEAN shows the peak level. The white line is the ceiling at -0.3 dB; nothing ever goes over it. The LED is green when fine, amber when hot, and red when the hidden ceiling caught a peak. Click the meter or the LED to reset it.

---

## Sound Engine V2 in short

Ghosty Pro's newer sound engine adds: the 13 ENGINES, the WARP and SPECTRAL modes, UNISON LAW V2 with DETUNE MODE and STACK, the Inside drive law and CLEAN MODE, the noise TYPES, the Ghost reverb, QUALITY oversampling, voicing controls, and the HYPER, DISTORTION, COMPRESSOR, SPLITTER and TONE EQ effects.

- Older sounds and projects load with the Classic laws, so they sound exactly as they did.
- New sounds and **INIT** use the V2 laws (Inside drive, V2 unison, Ghost reverb, High quality).

---

# WORKFLOWS

## Trap 808 slide

1. Press **INIT**. In the VOICE card turn **MONO** and **LEGATO** on.
2. Set **GLIDE** to about 150 to 300 ms so notes slide into each other.
3. Set OSC 1 to a sine-like table (for example the Classic table, or GHOST A/B with shape A = Sine). Turn **SUB** up to taste.
4. In the AMP envelope make **SUSTAIN** high and **RELEASE** long.
5. Add weight with **DRIVE** or the FATTEN effect in the FX tab (turn it on and pick the SUB or BODY mode).
6. Play overlapping notes in your piano roll. Legato notes glide on without restarting the envelopes.
7. Want a pitch dive on each hit? Use the BEND tab: pick **Drop & return** from **SHAPES...**.

## Dark trap brass

1. Open the **TRAP** tab and click **BRASS OFF** so it reads **BRASS ON**.
2. Shape it with **SCOOP**, **GROWL**, **FORMANT** and **FALL**.
3. Raise **CHARACTER** for saturation that turns into clipping.
4. Optionally add a second layer in the STACK: set layer 2 to **Sample** and drop in a pad or choir sample.

## Slowed and tape

1. TRAP tab, **TAPE / SLOWED**: turn **TAPE** on.
2. **AMOUNT** sets how worn it is. **SLOW** sets the pitch-down (try -3 to -5 semitones).
3. The effect applies to every layer of the sound.

## Moving pad with the BEND LANE

1. Make a pad from INIT (slow ATTACK and RELEASE, two detuned oscillators) or load one of your own from BROWSE.
2. BEND tab: click **SHAPES...** and pick **Wobble 1/8** or **Rise 4 bars**.
3. Choose **RETRIGGER** in the mode menu if you want the lane to start with each new note.
4. Lower **AMOUNT** for a subtler move.

## Sound design with a ghost

1. Pick OSC 1's table **Ghost A/B**.
2. Choose **DRAW A**, drag on the display to draw a wave. Choose **DRAW B** and draw another.
3. Turn **POS** (or drag the MORPH ghost on the XY pad) to morph from A to B.
4. Drag a source tile (for example LFO 1) from the MOD tab onto **POS** and set its amount with the ring.

## Build your own wavetable

1. Press **EDITOR** on an oscillator.
2. Pick recipe A (for example Saw) and recipe B (for example Bright), set **FRAMES** to 16 and press **GEN TABLE**.
3. Click frames, draw or change the harmonic bars. Mark key frames with **SHIFT-click** and use **MORPH: SPECTRAL**.
4. Press **OSC PLAYS IT**. Turn **LIVE** on to hear edits as you work.
5. **SAVE AS** gives it a name (full version).

## Blend two sounds with the MORPH board

1. Load your first sound and click an empty spot on the MORPH tab: a ghost holds it.
2. Load a second sound, click another spot.
3. Drag the puck between them and listen. Set **ORBIT** to Circle with **SYNC** on for movement in time.
4. Turn **MORPH ON** off to keep the blend: the sound you hear becomes the knob values.

## Chords and an arp in one key

1. In the play row set **KEY** and **SCALE** (for example A and Minor).
2. Set **CHORD** to Triads.
3. Press **ARP**, set the rate to 1/16 and the order to Up + Down.
4. Tap one key: the arp plays the whole chord in time with your host tempo.

## Fix timing or volume with an effect scene

1. FX tab. Click the **GATE** light to switch it on and pick a pattern.
2. Click steps to paint the rhythm. Use **SMOOTH** to soften the edges.
3. Add **HYPER** for width, and keep **LIMIT** on at the end of the chain.
4. Save the whole rack with **SAVE SCENE**.

---

# FL STUDIO TIPS

## Loading

- Ghosty Pro is an **instrument**. Add it from the plugin database (Add > Ghosty Pro), not as an effect.
- Windows installs VST3 files into `C:\Program Files\Common Files\VST3`. On Mac the installer puts VST3 in `/Library/Audio/Plug-Ins/VST3`, AU in `/Library/Audio/Plug-Ins/Components` and CLAP in `/Library/Audio/Plug-Ins/CLAP`. FL Studio on Mac loads VST3 and AU.
- If it does not show up: in FL open the plugin manager and run a new scan (Options > Manage plugins > Find installed plugins), or restart FL.

## MIDI

- Send MIDI notes to Ghosty Pro from the piano roll or a MIDI keyboard. It also reads pitch bend, the mod wheel (CC 1), aftertouch and any CC you learn.
- The KEY / SCALE / CHORD / ARP row works on the notes it receives, so it changes what your piano roll plays. Switch it off (SCALE Off, CHORD Single notes, ARP off) when you want the notes exactly as written.
- It has no MIDI output.

## Tempo sync

LFO SYNC, the BEND lane, the ARP, and the effects in the rack all follow the tempo of your host. The arp locks to the host's playhead while it plays and runs at the host tempo when stopped.

## Automation

Every knob is a normal automatable parameter, including the MORPH puck (Morph X and Morph Y). In FL, use the plugin wrapper's parameter list (the small arrow or gear menu at the top of the plugin window) and right-click a parameter to create an automation clip. Recording automation while you tweak a knob also works.

## Saving

- **SAVE** keeps the sound in your sounds folder as a `.ghostypro` file. Your saved sounds are also the plugin's programs in the host (program 0 is INIT).
- **SNAPSHOT** keeps quick copies in the SNAPSHOTS bank.
- Your FL project stores everything about the plugin, including the look. In the demo, a reopened project starts at the default sound (see DEMO VS LICENSED).
- There is no drag-out of MIDI or audio in Ghosty Pro. To get audio, render or freeze the channel in FL. The WAVETABLE EDITOR can also export a table as WAV (full version).

## Latency

The plugin tells the host how much latency it has, so FL compensates for it automatically. It changes with QUALITY, with CLEAN and with the effects you use (SPIRIT EQ in a Linear mode adds the most).

## Where things live

| What | Mac | Windows |
|---|---|---|
| Your saved sounds (`.ghostypro`), `favourites.json`, `recent.json` | `~/Documents/Ghosty/Ghosty Pro` | `Documents\Ghosty\Ghosty Pro` |
| User wavetables (`.ghostywt`) | `~/Documents/Ghosty/Ghosty Pro/Wavetables` | `Documents\Ghosty\Ghosty Pro\Wavetables` |
| FX presets and scenes | `~/Documents/Ghosty/Ghosty Pro/FX Presets` and `FX Scenes` | same folders under `Documents\Ghosty\Ghosty Pro` |
| MORPH boards (`.ghostyboard`) and snapshots | `~/Library/GhostBlock/Ghosty Pro/Boards` and `.../Snapshots` | `%APPDATA%\GhostBlock\Ghosty Pro\Boards` and `\Snapshots` |
| Licence file (one per plugin) | `~/Library/GhostBlock/Ghosty Pro.license` | `%APPDATA%\GhostBlock\Ghosty Pro.license` |
| Settings (layout, AMBIENT) | `~/Library/Application Support/GhostBlock/Ghosty Pro.settings` | `%APPDATA%\GhostBlock\Ghosty Pro.settings` |
| Engine log (for support) | `~/Library/Logs/Ghosty Pro/engines.log` | `%APPDATA%\Ghosty Pro\engines.log` |

> Tip: **OPEN SOUNDS FOLDER** (BROWSE tab, or LOAD menu) opens your sounds folder for you, and the snapshot list has **Show the folder**.

---

# THE LOOKS

Ghosty Pro has four looks: **BLACK**, **PURPLE**, **LIGHT** and **GHOST**. GHOST is the fourth look: liquid glass, with a pale aurora backdrop, frosted glass panels, pill buttons, glossy knobs and glass keys.

**How to switch.**

- Click the **look button** at the top right. It cycles BLACK, PURPLE, LIGHT, GHOST, and back to BLACK. The button shows the name of the NEXT look, with a small swatch.
- Right-click the look button for the **LOOK** menu: pick any of the four. In STUDIO the **LAYOUT** button opens the same menu.

The look is saved with your project, not with sounds. A new plugin starts as BLACK.

## The layouts

The same menu has a **LAYOUT** section:

- **STUDIO** (default): both oscillators, filter, envelopes, LFOs and effects on one page, with on-screen keys. It opens at a size that fits your screen.
- **CLASSIC**: the original pages, one tab each.
- **AMBIENT**: a gentle neon rain behind STUDIO's panels. Switch it off if you want a calmer screen; it also turns itself off when the computer is busy.

Layout and AMBIENT are saved for you, not in projects or sounds. The window can be resized (about 60 % to 160 %); the shape stays the same.

---

# DEMO VS LICENSED

Without a licence key the plugin runs in **demo mode**. Everything works, with these limits:

- **Drop-out:** the sound goes silent for 2 seconds every 20 seconds. The top bar reads "DEMO - sound drops out every 20 s".
- **Reopened projects:** a project you reopen starts at the default sound. Unlocking brings the saved sound back. (The look still comes back.)
- **Export is off:** in the WAVETABLE EDITOR, **SAVE**, **SAVE AS** and **EXPORT WAV** are refused. The **UNLOCK** button blinks and a small hint says "Export is part of the full version." Ghosty Pro has no other export path.
- **Saves leave out user wavetables** made in the WAVETABLE EDITOR (generated, drawn, harmonic or phase edited, or started from the oscillator). This applies to presets, snapshots, boards and the project. Tables you imported from your own file are kept.

**How to unlock.**

1. Click **UNLOCK** in the top bar.
2. Paste the licence key from your receipt email and press **Unlock**. (**Buy** opens the store page.)
3. The first unlock needs the internet. After that it works offline.

When the plugin is unlocked, the button reads **LICENSED**; click it to see which key is in use (only the start and end are shown). A key unlocks up to 5 computers. A bundle key unlocks every GhostBlock plugin on that computer. The licence is re-checked quietly every 14 days and only locks again if the purchase was refunded or charged back, never because the internet is down. The licence file is kept in the `GhostBlock` folder of your user app data.

---

# SHORTCUTS

## Keyboard

| Keys | What it does |
|---|---|
| **Shift + Alt + S** | Take a SNAPSHOT (when no text box has focus) |
| **Ctrl / Cmd + Z** | Undo the last snapshot delete (within 60 seconds). In the WAVETABLE EDITOR: undo |
| **Ctrl / Cmd + Shift + Z** | WAVETABLE EDITOR: redo |
| **Left / Right arrow** | BROWSE: previous / next sound in the list |
| **Enter** | BROWSE search box: load the first match |
| **Arrow keys, Space** | When the filter picture or an LFO picture has focus: move CUTOFF / RES, or change RATE / SHAPE |

## Mouse

| Gesture | What it does |
|---|---|
| **Drag a knob** | Change it (a rotary knob keeps its relative drag; a bar jumps to where you click and then follows) |
| **Hold Shift while dragging** | Fine control |
| **Hover a knob** | Shows its value in real units |
| **Double-click a knob** | Back to its default |
| **Right-click a knob** | The knob menu: Type a Value..., Mod Source, curves, MIDI Learn, Reset Control |
| **Drag a source tile onto a knob** | Make a modulation route (hold it over a tab name to switch tabs) |
| **Drag a modulation ring** | Set the route's amount. Double-click: 0 |
| **Drag in the oscillator display** | Draw (only in GHOST A/B, on the frame chosen by DRAW A / DRAW B). Hold Shift for a line |
| **Right-click the oscillator display** | Undo, redo, smooth strokes, free drawing, stepped, mirror, reset, copy |
| **Wheel on the filter picture** | Filter DRIVE |
| **Drag ghosts on the XY pad** | Two knobs at once |
| **BEND lane:** click, drag, right-click / double-click, diamond, ALT | Add, move, delete, curve, no snap |
| **FX strip:** click, light, drag, right-click | Open, switch on / off, reorder, order menu |

---

# TROUBLESHOOTING

**1. No sound.**
Check, in order: **BYPASS** is off; the DAW channel is not muted; FL is sending notes to this channel; OSC 1 **ON** is lit and its **LEVEL** is up; **VOLUME** is not at the bottom; **VOICES** (VOICE card) is at least 1; **CUTOFF** is not turned all the way down; the TRAP tab's STACK layers are not muted (**M**). Press **INIT** and play a key: if that works, the problem is in the sound you had.

**2. No MIDI notes arrive.**
Ghosty Pro is an instrument: it must be on an instrument channel, not an effect slot. In FL check the channel's MIDI input port, and check the **SCALE**, **CHORD** and **ARP** settings in the play row: a scale moves notes, an arp turns held notes into a pattern.

**3. The sound drops out every 20 seconds.**
That is the demo. Click **UNLOCK** and enter your licence key.

**4. Notes get cut off or stolen.**
The **VOICES** limit is reached (VOICE card). Raise **VOICES**, or lower **UNISON**. In the CLASSIC layout, **MORE** on the AMP / VOICE card also has **STEAL** (which voice a new note takes) and **1 PER NOTE**.

**5. The sound is too quiet or too loud after switching sounds.**
Each sound has its own **TRIM** (CLEAN card). **VOLUME** is your own level on top. Turn **CLEAN** on to keep levels steady.

**6. Mac says the plugin cannot be opened (Gatekeeper warning).**
Until the installer is notarized, right-click the installer and choose Open, then confirm. If a plugin file is blocked, right-click it in Finder and choose Open once, or allow it in System Settings > Privacy & Security.

**7. The plugin does not show up in FL Studio.**
Rescan: Options > Manage plugins > Find installed plugins. Make sure the VST3 is in the folders listed under FL STUDIO TIPS. On Mac use VST3 or AU. Restart FL after installing.

**8. The knobs move by themselves.**
The **MORPH ON** switch is on (the top bar says "MORPH ON - knobs follow the board"). Turn it off on the MORPH tab. Or a modulation route / macro is moving the knob (look for the ring around it), or **AUTO** is on in the XY pad.

**9. CPU is high.**
Lower **QUALITY** to Draft (VOICE card > MORE), lower **UNISON** or **VOICES**, switch off effects you do not use, and avoid the Linear modes of SPIRIT EQ. The STUDIO **AMBIENT** rain can be turned off in the LOOK and LAYOUT menu. The STACK readouts show CPU per layer.

---

# FAQ

**What is GHOST A/B?**
A table you draw yourself: frame A is the sound at POS 0 %, frame B is the sound at POS 100 %, and POS morphs between them. Pick it in the table menu, or pick a shape in the A or B menu.

**What is the difference between the GHOST FX knobs and the FX tab?**
The eight knobs on the SYNTH page (DRIVE, CRUSH, CHORUS, PHASER, ECHO, REVERB, HAUNT, VOLUME) are the effects built into the synth. The FX tab is a separate rack of 21 effects that runs after them, with presets and scenes.

**Do snapshots and saved sounds hold the effects and modulation?**
Saved sounds keep every knob, both drawn waves, an imported sample, the effects, the macros and the modulation routes. The look and the layout are not part of a sound.

**Can I use my own samples?**
Yes: as a wavetable (**WAV** on an oscillator, or the WAVETABLE EDITOR), or as a one-shot in the TRAP tab's SAMPLE section.

**Why do older sounds sound slightly different from new ones?**
They load with the Classic laws (older unison, filter drive, noise and reverb) so they sound as they always did. New sounds use the V2 laws.

**Can I move or resize the window?**
Yes, drag the window corner. The shape is kept. STUDIO opens at a size that fits the screen.

**Does MIDI learn work?**
Yes. Right-click a knob and choose **MIDI Learn**, then move a controller on your keyboard. Learned controllers are saved with your host project (not inside a sound).

**Is there a standalone version?**
Yes, the Standalone build runs the same plugin as its own program; the on-screen keys let you play it without a keyboard.

---

# GLOSSARY

| Term | Meaning |
|---|---|
| **GHOST** | The plugin's mascots. Ghosts move knobs on the XY pad, hold sounds on the MORPH board and float in the BEND tab. |
| **GHOST A/B** | A drawable table with two frames; POS morphs A into B |
| **WAVE PALETTE** | The tile picker for tables and engines |
| **ENGINE** | An oscillator type that is not a plain wavetable (VA, FM 2-OP, STRING, TEXTURE, ...) |
| **WARP** | A reshaping of the oscillator's phase or output (Sync, Bend, FM, AM, ...) |
| **SPECTRAL** | A reshaping of the spectrum of a table |
| **STACK** (oscillator) | Extra octaves or fifths added to a note |
| **STACK** (TRAP tab) | The three layers of a sound |
| **POCKET** | A layer's own place in the stereo and frequency picture (CLEAN) |
| **CLEAN** | The mode that keeps layers clear and the output under -0.3 dB |
| **ROUTE** | One modulation connection from a source to a knob |
| **MACRO** | A knob named by the sound that moves several knobs at once |
| **BEND LANE** | A drawn, tempo-synced pitch curve |
| **PUCK** | The glowing dot on the MORPH board that blends the ghosts' sounds |
| **ORBIT** | The puck circling around where you placed it |
| **SCENE** | A whole-rack FX setup (order, patterns and every effect) |
| **SNAPSHOT** | A quick one-click save of the current sound |
| **FLOW strip** | The strip of coloured bars you slide across to play a scale |
| **INIT** | The plain starting sound every new Ghosty Pro opens with |
| **V2** | The newer sound engine laws (unison, drive, reverb, noise) |
