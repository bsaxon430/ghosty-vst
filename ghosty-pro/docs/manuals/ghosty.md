# GHOSTY - the free wavetable synth

A friendly wavetable synth with 81 sounds, two waves you can draw yourself, and four ghosts to play with. **Ghosty is free and open source** (AGPL-3.0). Nothing is locked, there is no demo mode and there is no licence key.

- Version: 1.2.0 (the free Ghosty build, as numbered in its own source).
- Formats: VST3 and Standalone on Windows. VST3, AU and Standalone on Mac. (The free build does not include a CLAP file.)
- Source code: the public Ghosty repository on GitHub (Synth and Shared folders).
- Part of the GhostBlock family. Its big brother is Ghosty Pro. Ghosty never nags you: the small GET PRO button only opens an info card when you click it.

---

## Quick start

> Make your first sound in 2 minutes

1. Load **Ghosty** as an instrument on a MIDI track (see the FL STUDIO TIPS section).
2. Click the preset box at the top (it shows a sound name). Pick anything in **PADS**, **BASS**, **KEYS**, **LEADS**, **PLUCKS**, **BELLS** or **FX**. The arrow buttons **<** and **>** step through the list.
3. Play some notes on your keyboard, or slide your mouse across the **FLOW** strip at the bottom. Higher on the strip is louder.
4. Turn **CUTOFF** (in the FILTER panel) down to make it darker and up to make it brighter.
5. Turn **MORPH** (OSCILLATOR panel) to blend from wave A to wave B.
6. Click and drag the **TONE** ghost on the big pad to move cutoff and resonance together.
7. In the **WAVE** screen, press and drag with the mouse to draw your own shape. Choose **DRAW A** or **DRAW B** first. Press **DICE** for a random shape.
8. Press **SAVE**, type a name, and press Save. Your sound is stored as a file you can load any time with **LOAD**.

---

## The screen

The whole window scales as one piece. Drag the window corner to make it bigger or smaller (from 60% to 160% of normal size). The shape stays the same.

The window has these areas from top to bottom: the header, the WAVE screen and GHOSTS pad, then the panels OSCILLATOR, AMP ENVELOPE, FILTER, FILTER ENVELOPE, LFO, EFFECTS, MOD WHEEL / VELOCITY, OUTPUT, and finally the PLAY card with the FLOW strip.

Knobs: drag up/down to turn. The small number under each knob shows its value. You can also click the number under the knob and type a value. Hosts can automate every knob.

### Header

| Control | What it does |
|---|---|
| Logo (ghost + "ghosty" + SYNTH tag) | Just the logo. It glitches when you change sound. |
| **<** and **>** | Previous / next factory sound (wraps around). |
| Preset box | Click to pick a sound. Sounds are grouped by heading: KEYS, PADS, BASS, LEADS, PLUCKS, BELLS, FX. Your own saved sounds appear under the YOUR SOUNDS heading. The last line of the list is a teaser about Ghosty Pro. |
| **SAVE** | Saves a sound: every knob and both drawn waves. Asks for a name. If the name exists it asks "Replace?". |
| **LOAD** | Menu of your saved sounds, plus "Open sounds folder". |
| **INIT** | Resets to a clean saw sound. |
| **GET PRO** | Opens an info card about Ghosty Pro. Buttons: See Ghosty Pro (opens the store page in your browser) and Not now. Esc or a click outside also closes it. |

### Preset list (81 sounds)

| Group | Count | Examples |
|---|---|---|
| KEYS | 14 | Ghost Piano, Dream EP, Glass Keys, Music Box, Church Organ |
| PADS | 16 | Vapor Pad, Haunted Halls, Trance Pad, Choir Aahs, Frozen Lake |
| BASS | 14 | Growl Bass, Sub Bass, Grime Bass, Fold Bass, Pluck Bass |
| LEADS | 15 | Neon Supersaw, Vox Lead, Chip Lead, Screamer, Brass Stab |
| PLUCKS | 14 | Pluck Fever, Kalimba, Marimba, Harp, Future Pluck |
| BELLS | 4 | Crystal Bell, Tubular, Gamelan, Ghost Chimes |
| FX | 4 | Riser Saw, Dark Drone, Bitcrush Zap, Ghost Wind |

Choosing a factory sound first puts the LFO, glide and extra filter settings back to neutral, so every sound plays the way it was designed. Your host's own program-change list shows the same 81 sounds.

### WAVE screen

Ghosty has two waves, **A** and **B**. **MORPH** blends between them. The screen shows the wave you are editing in front, with the other one behind it.

| Control | What it does |
|---|---|
| **Wave A box / Wave B box** (two drop-downs above the screen) | Pick a starting shape for each wave: Sine, Triangle, Saw, Square, Pulse, Organ, Glass, Growl, Digital, Vox, Ahh, Ooh, Sync, Fold, Hollow, Bright (16 shapes). Defaults: A = Saw, B = Square. |
| **DRAW A / DRAW B** | Choose which wave your mouse draws on. |
| **DICE** | Replaces the chosen wave with a random smooth shape. |
| The screen itself | Press and drag to draw. Fast drags are joined with a straight line so there are no gaps. When you let go, the shape is lightly smoothed so it does not sound harsh. |

> Tip: picking a new shape in a Wave box replaces whatever you drew on that wave.

### GHOSTS pad

Four ghosts, each controlling two parameters at once (left-right and up-down). Drag a ghost, and a readout shows the two values.

| Ghost | Left-right | Up-down |
|---|---|---|
| **TONE** | CUTOFF | RES (resonance) |
| **MORPH** | MORPH | WIDTH |
| **SPACE** | ECHO | REVERB |
| **HEAT** | DRIVE | SUB |

- Click anywhere on the pad and the nearest ghost jumps there.
- Double-click a ghost to give it a new random face (just for fun).
- Moves are recorded by your host as automation, so you can automate the pad.

### OSCILLATOR

| Control | What it does | Range / default |
|---|---|---|
| **MORPH** | Blend from wave A (0%) to wave B (100%). | 0-100%, default 15% |
| **WIDTH** | Detune and stereo spread of the unison voices. | 0-100%, default 55% |
| **UNISON** | Number of stacked voices per note. | 1-7, default 7 |
| **SUB** | Adds a sine one octave below. | 0-100%, default 25% |
| **GLIDE** | Slides the pitch from the last note to the new one. Time to travel. | 0 ms-2 s, default 0 (off) |
| **MONO** | One note at a time. Playing a new note while another is held moves the voice to it. Letting go of the newest key falls back to a key still held. | off |
| **LEGATO** | Only works when MONO is on. Notes played while another is held glide without restarting the envelopes. | off |

### AMP ENVELOPE

How loudness changes over time.

| Control | What it does | Range / default |
|---|---|---|
| **ATTACK** | Time to fade in. | 1 ms-4 s, default 10 ms |
| **DECAY** | Time to fall to the sustain level. | 10 ms-4 s, default 400 ms |
| **SUSTAIN** | Level while the key is held. | 0-100%, default 80% |
| **RELEASE** | Time to fade out after you let go. | 10 ms-6 s, default 600 ms |

You can change these on a note that is already held and it follows.

### FILTER

| Control | What it does | Range / default |
|---|---|---|
| Type box | Low pass, Band pass, High pass, Notch. | default Low pass |
| **CUTOFF** | Filter frequency. | 50 Hz up to about 18 kHz, default 82% (about 6 kHz) |
| **RES** | Resonance (the ringing at the cutoff). | 0-100%, default 10% |
| **F.DRIVE** | Soft saturation before the filter. At 0 it is fully off. | 0-100%, default 0 |
| **ENV AMT** | How far the filter envelope opens the cutoff (up to 5 octaves). | 0-100%, default 20% |

### FILTER ENVELOPE

| Control | What it does | Range / default |
|---|---|---|
| **LINK TO AMP** (button) | On: the filter envelope copies the amp envelope's attack, decay and release, with sustain at zero (the classic Ghosty behaviour). The four knobs below dim. Off: you set your own. | on |
| **F.ATTACK** | Filter envelope attack. | 1 ms-4 s, default 9 ms |
| **F.DECAY** | Filter envelope decay. | 10 ms-4 s, default 400 ms |
| **F.SUSTAIN** | Filter envelope sustain. | 0-100%, default 0 |
| **F.RELEASE** | Filter envelope release. | 10 ms-6 s, default 600 ms |

### LFO

One LFO that moves one thing.

| Control | What it does | Range / default |
|---|---|---|
| **RATE** | Speed in Hz. Dims when SYNC is on. | 0.05-20 Hz, default 2 Hz |
| **DEPTH** | How much it moves the target. | 0-100%, default 0 (LFO off) |
| **SHAPE** box | Sine, Triangle, Saw, Square, S&H (a new random value each cycle). | Sine |
| **TARGET** box | Cutoff, Morph, Pitch (vibrato, up to +/-2 semitones), Volume (tremolo), Pan. | Cutoff |
| **SYNC** (button) | Locks the LFO to your song tempo. While the host plays it follows the bar grid. | off |
| **SYNC DIVISION** box | Used when SYNC is on: 1/1, 1/2, 1/4, 1/8, 1/16, 1/32, and triplets 1/2 T, 1/4 T, 1/8 T, 1/16 T, 1/32 T. Dims when SYNC is off. | 1/4 |

### EFFECTS

The effects run on the whole sound after the voices. Each knob is 0-100%. At 0 the effect is off.

| Control | What it does | Default |
|---|---|---|
| **DRIVE** | Warm saturation. | 10% |
| **CRUSH** | Bit-crusher: lo-fi grit. | 0 |
| **CHORUS** | Thickens the sound with a moving doubled copy. | 0 |
| **PHASER** | Sweeping phase effect. | 0 |
| **ECHO** | Ping-pong echo (dotted eighth, follows tempo). | 25% |
| **REVERB** | Room / hall reverb. | 35% |
| **HAUNT** | A dark, wobbly, long echo. | 0 |

### MOD WHEEL / VELOCITY

| Control | What it does | Range / default |
|---|---|---|
| **MOD WHEEL** | How much the mod wheel (MIDI CC1) raises MORPH and CUTOFF together (up to half the knob range). | 0-100%, default 50% |
| **VEL > FILT** | How much harder key hits open the filter (up to 4 octaves). Velocity always also changes volume a little. | 0-100%, default 0 |

### OUTPUT

| Control | What it does | Range / default |
|---|---|---|
| **VOLUME** | Master volume. Ramped smoothly, so no zipper noise. | 0-100%, default 75% |

A safety limiter after the effects keeps the output below full scale, so Ghosty will not clip your master by itself.

### PLAY card

Helpers that make playing easier. They work on notes from your keyboard, your MIDI clip, and the FLOW strip.

| Control | What it does | Range / default |
|---|---|---|
| **KEY** box | The root note of the scale: C to B. | A |
| **SCALE** box | Off (all notes), Major, Minor, Penta, Minor Penta, Blues, Dorian, Mixolydian, Lydian, Phrygian, Harmonic Minor, Japanese, Arabic. When on, every note you play is moved to the nearest note in the scale. | Off |
| **CHORDS** box | Single notes, Triads, 7th chords, Power chords. One key plays a whole chord. With a scale on, the chord is built from the scale's own notes. With the scale off, triads are major and 7th chords are dominant sevenths. Power chords are root, fifth and octave. | Single notes |
| **ARP** (button) | Turns the arpeggiator on. A small light blinks on each step. | off |
| Rate box | 1/4, 1/8, 1/16, 1/32, 1/8 T, 1/16 T. Follows your song tempo. | 1/16 |
| Mode box | Up, Down, Up + Down, Random, As played. | Up |
| Octaves box | 1, 2 or 3 octaves. | 2 octaves |
| **ARP GATE** | How long each arp note lasts inside its step. | 5-98% in effect, default 70% |
| **FLOW** strip | A row of notes that all fit the chosen KEY and SCALE (3 octaves up from the key's C). Press and slide to play. Higher up the strip means louder. Ripples and ghosts show what you play. | - |

> Tip: set KEY and SCALE first, then slide on FLOW. Every note is "right" by design.

When the arp is on and the host is playing, the arp stays in step with the host's beat position.

---

## Workflows

### Wobble bass

1. Pick **BASS > Wobble-ish** or start from **INIT**.
2. Turn **UNISON** to 1 and **SUB** up to about 50%.
3. In the LFO panel set **TARGET** to Cutoff, **SHAPE** to Sine or Square, **DEPTH** to about 60%.
4. Turn on **SYNC** and choose a **SYNC DIVISION** of 1/8 so the wobble follows your tempo.
5. Raise **RES** to about 40% and add **DRIVE** in EFFECTS.

### Gliding bass slide

1. Pick **BASS > Sub Bass** (or any bass).
2. Turn on **MONO** and **LEGATO**.
3. Set **GLIDE** to about 150 ms.
4. Play overlapping notes: the pitch slides from one to the next without a new attack.

### Lush pad

1. Pick **PADS > Vapor Pad**.
2. Set **UNISON** to 7 and **WIDTH** around 50%.
3. Slow the **ATTACK** (about 1 s) and lengthen **RELEASE** (about 3 s).
4. Add **REVERB** and a little **CHORUS**.
5. Use the **SPACE** ghost to ride echo and reverb while it plays.

### Chord pad from single notes

1. Set **KEY** and **SCALE** (for example C, Minor).
2. Set **CHORDS** to Triads (or 7th chords).
3. Play one key at a time: each makes a full chord from the scale.

### Instant arpeggio

1. Set **KEY**, **SCALE** and **CHORDS** as above.
2. Turn **ARP** on, rate 1/16, mode Up + Down, octaves 2.
3. Hold a key (or a chord). Lower **ARP GATE** for short plucky notes.

### Draw your own sound

1. Choose **DRAW A**. Draw a slow smooth curve.
2. Choose **DRAW B**. Draw something jagged.
3. Slowly turn **MORPH**, or set the LFO **TARGET** to Morph with some **DEPTH** to make the sound evolve.
4. Press **SAVE** so you do not lose it. The drawn waves are saved with the sound.

### Mod-wheel expression

1. Set **MOD WHEEL** to 70% or more.
2. Play and move your keyboard's mod wheel: the sound gets brighter and morphs toward wave B.

---

## FL Studio tips

- **Loading:** Make sure the plugin is scanned: **Options > Manage plugins > Find more plugins** (scan). Then click **+** in the Channel Rack, or look in Plugin database > Generators, and choose **Ghosty**. It is an **instrument** (it makes sound from MIDI), not an effect.
- **MIDI:** Play from the piano roll or a MIDI keyboard. Ghosty listens on all MIDI channels. Pitch bend works (+/-2 semitones). The mod wheel is CC1. Output is stereo only.
- **Tempo sync:** The arpeggiator and the synced LFO read the host tempo. The arp also follows the host beat position when the song plays. In the standalone app, tempo defaults to 120 BPM.
- **Automation:** All knobs and buttons are host parameters. In FL, right-click a knob and choose "Create automation clip", or use the plugin's wrapper menu to link a controller.
- **Saving sounds:** Press **SAVE** in the plugin. Files are named `<name>.ghostysound` (readable XML) and live in:
  - Windows: `Documents\Ghosty\Ghosty`
  - Mac: `~/Documents/Ghosty/Ghosty`
  - LOAD > "Open sounds folder" opens it. Copy these files to share sounds.
- **Projects:** Your whole current sound, including drawn waves, is saved inside the FL project, so projects reopen exactly as you left them.
- **Where the plugin files are:**
  - Windows: `C:\Program Files\Common Files\VST3`
  - Mac: `~/Library/Audio/Plug-Ins/VST3` and `~/Library/Audio/Plug-Ins/Components` (AU)
- **Latency:** Ghosty adds no processing delay of its own. Use a small buffer in your audio settings for live playing.
- **Dragging MIDI/audio out:** Ghosty has no drag-out of MIDI or audio. Record the notes in your host, or bounce the track.
- **Tail:** The plugin reports a 4 second tail so reverb and echo ring out when playback stops.

---

## The looks

Ghosty has one look: the dark purple ghost theme. It does not have the BLACK / PURPLE / LIGHT / GHOST look button that the paid plugins have. You can resize the window instead (see THE SCREEN).

---

## Demo vs licensed

There is no demo. Ghosty is free and open source, with every feature unlocked and no silence dropouts. There is no UNLOCK button and no licence key.

---

## Shortcuts and gestures

| Where | Gesture | Result |
|---|---|---|
| Knob | Drag up / down | Turn |
| Knob value | Click the number | Type a value |
| WAVE screen | Press and drag | Draw on the wave |
| GHOSTS pad | Drag a ghost | Move its two parameters |
| GHOSTS pad | Click on empty pad | Nearest ghost jumps there |
| GHOSTS pad | Double-click a ghost | New random face |
| FLOW strip | Press and slide | Play scale notes; higher is louder |
| GET PRO card | Esc or click outside | Close |
| Window corner | Drag | Resize (60-160%) |

---

## Troubleshooting

1. **No sound.** Ghosty is an instrument. Put it on a channel that receives MIDI, and make sure notes are played. Check VOLUME, and that CUTOFF is not at zero. Check that the mixer track is not muted.
2. **Plugin not showing in FL Studio.** Scan again: Options > Manage plugins > Find more plugins. Restart FL. On Windows check the file is in `C:\Program Files\Common Files\VST3`.
3. **Mac: "can't be opened" or "damaged".** The free build is not signed by Apple. Open System Settings > Privacy & Security, scroll down and click Open Anyway, or right-click the app and choose Open. Running the installer script (`Install Ghosty.command`) again also fixes it.
4. **Notes are wrong / out of key.** SCALE is on and moves notes to the scale. Set SCALE to "Off (all notes)".
5. **Single notes make chords.** CHORDS is not "Single notes".
6. **Notes keep repeating.** ARP is on. Turn it off.
7. **Chords play only one note.** MONO may be on. Turn it off to play more than one note at a time.
8. **Filter envelope knobs do nothing.** LINK TO AMP is on. Turn it off to use F.ATTACK to F.RELEASE.
9. **LFO RATE does nothing.** SYNC is on, so SYNC DIVISION is used instead.
10. **My saved sound is missing.** Check the sounds folder (see FL STUDIO TIPS) and that the file ends in `.ghostysound`.

---

## FAQ

**Is Ghosty really free?** Yes. It is free and open source under AGPL-3.0. Nothing is locked.

**What is the difference from Ghosty Pro?** Pro has more sounds, more unison, wavetable import, a modulation matrix, more filters and a built-in effects studio. The GET PRO card in the plugin lists them.

**How many voices can I play?** 16 notes at once.

**Can I load my own samples as waves?** Not in free Ghosty. You draw waves with the mouse or start from the 16 shapes.

**Are my drawn waves saved?** Yes, in your saved sounds and in the host project.

**Does it work offline?** Yes. The only thing that goes online is the GET PRO button, which opens the store page when you click it.

---

## Glossary

- **Wavetable:** a list of one-cycle wave shapes the synth plays through. Ghosty uses two (A and B).
- **MORPH:** the blend between wave A and wave B.
- **Unison:** several slightly detuned copies of the sound played together.
- **WIDTH:** how far apart those copies are in pitch and stereo.
- **SUB:** an extra sine one octave down.
- **Ghosts:** the four draggable faces on the pad (TONE, MORPH, SPACE, HEAT).
- **FLOW:** the strip that only plays notes that fit your scale.
- **HAUNT:** a dark, wobbly long echo effect.
- **Ghostysound:** the file type for saved sounds.
