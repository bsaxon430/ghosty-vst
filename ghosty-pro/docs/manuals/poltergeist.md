# POLTERGEIST

Poltergeist is an FM bass and spirit synth. Six "ghosts" (FM operators) plus a NOISE ghost and a sample slot haunt each other to make growls, 808s, wobbles, reeses, bells and leads.

Version 3.1.0. Formats: VST3, AU, CLAP, Standalone. It is a stereo instrument (it takes MIDI in, audio out).

---

## Quick start

### Make your first growl in 2 minutes

1. Load Poltergeist as an instrument on a track and play some low notes (the on-screen keyboard at the bottom works too).
2. Click the preset name at the top (it shows the current sound). Open a folder such as **WOBBLE** or **GROWL / SCREECH** and pick a sound. Use the **<** and **>** buttons to step through the list.
3. Or type words in the box at the top right, for example `dark growl`, and press **SUMMON**. Press **SUMMON** again for another take.
4. Turn the big **HAUNT** knob (bottom row). It sets how hard the ghosts hit each other, so the growl gets angrier.
5. Open the **WOBBLE** tab and drag the points on the drawn shape. The wobble follows your tempo.
6. Open the **BASS** tab. Turn on **SUB** to add a clean sine under the growl, and set **LOW MONO** to about 120 Hz to keep the low end mono.
7. Like it? Press **SNAPSHOT** (next to the **<** **>** buttons) to keep the sound, or **SAVE** to name it.
8. Lower **MASTER** (on the GHOSTS tab, VOICE card) if it clips. The default is -6 dB.

> Tip: **RANDOM** (top right) summons a surprise sound. Great for ideas.

---

## The screen

The window is 1040 x 744 and you can resize it (from 75% to 200%, the shape stays the same). Everything works with the mouse.

- **Knobs.** Drag up or down. Hover to see a tooltip. Double-click to go back to the default. Right-click to type a value (for example `20 Hz` or `50%`).
- **Bipolar sliders** (in the MOD MATRIX): click to jump to a spot, **Shift** = fine, double-click = 0, right-click = type.

### The header (always visible)

From left to right:

| Control | What it does |
|---|---|
| POLTERGEIST logo and version | Shows the plugin version. |
| **<** / **>** | Previous / next preset. Shift-click jumps to the previous / next folder. When a snapshot is loaded, they step through your snapshots instead. |
| Preset name | Click for the preset menu (see Presets below). |
| Star | Mark the current preset as a favourite. Favourites show at the top of the preset list. |
| **SAVE** | Save this sound under a name (saved in Documents/Ghosty/Poltergeist). |
| **LOAD** | Load a sound you saved with SAVE. |
| **SNAPSHOT** and **v** | **SNAPSHOT** saves the sound right now into the SNAPSHOTS bank (Shift+Alt+S). **v** lists your snapshots: load, star, rename, delete, show the folder. |
| Text box + **SUMMON** | Type what you want (`dark trap bell`, `wobble growl`, `808 slide`). SUMMON makes a sound from your words. With an empty box it asks for words. |
| **RANDOM** | Summons a random sound and clears the text box. |
| **BYPASS** | Mutes the whole plugin. Click again to turn it back on. |

Under that there is a row of **style chips**: **808, Bell, Growl, Wobble, Reese, Pluck, Keys, Pad, Choir, Lead, Brass, Riser, Arp**. Click one to summon that type. Next to them are modifier chips you can switch on: **dark, bright, haunted, dirty, wide, glitch, lofi, metallic**. Turn some on, then click a style chip to add those words.

Under the chips is the tab row: **GHOSTS, MOVE, FX, PLAY, BASS, WOBBLE, MORPH**. At the right end of the tab row are **UNLOCK** (demo only) and the look button (it names the next look). A status line between the tabs and those buttons tells you what just happened.

At the bottom of every tab:

- The six big **macro knobs**: **HAUNT, SPECTRE, POSSESS, DRIFT, VEIL, CURSE**.
- The **keyboard** (C1 to C6, with C3 as middle C). Click keys to play.

### The macro knobs

All run from 0 to 100%.

| Macro | What it does | Default |
|---|---|---|
| **HAUNT** | FM growl: how hard the ghosts hit each other. | 0% |
| **SPECTRE** | Unison: more ghosts, wider and thicker. | 0% |
| **POSSESS** | Drive: warm grit up to full possession. | 0% |
| **DRIFT** | Movement: wobble, wow and vibrato. | 0% |
| **VEIL** | Space: reverb and echo. | 15% |
| **CURSE** | Glitch: crush, decimate and stutter. | 0% |

The mod wheel (CC1) adds to HAUNT and also opens the vowel of the TALK section.

### Tab: GHOSTS

This is the FM synth. The left side is the **ghost field**. The right side is the panel for the ghost you selected. Under them are the MAIN FILTER, PITCH DROP, DRIFT (LFO) and VOICE cards.

#### The ghost field

You see ghosts 1 to 6, a **NOISE** ghost, and an **ADD SAMPLE** ghost (bottom right). A ghost labelled "you hear it" is sent to the output. A ghost labelled "haunts others" is only used to modulate other ghosts.

- **Click a ghost** to select it. Its controls show in the panel on the right.
- **Drag one ghost onto another** to make it HAUNT it (FM). A link appears with a number in a small circle (the depth, 0 to 100).
- **Drag the link's circle up or down** to change how hard.
- **Double-click the circle** to break the link.
- A ring over a ghost shows it has FEEDBACK.
- Ghost 1 starts as the one you hear; ghost 2 haunts ghost 1 at 35%.
- You can link any ghost (1 to 6, NOISE, or the SAMPLE) onto ghosts 1 to 6.

#### The ghost panel (GHOST 1 to 6)

| Control | What it does | Range / default |
|---|---|---|
| Waveform menu | The ghost's waveform. **Classic Morph** is the original (WAVE morphs sine, triangle, saw, square). The other 31 are band-limited tables and WAVE bends them. | Classic Morph |
| Envelope picture | Shows the ghost's envelope. Click it to switch between **AMP** (the ghost's own) and **FILTER** (the filter envelope). A dot traces the notes you play. | |
| **RATIO** | The ghost's pitch relative to the note. Snaps to musical ratios; hold **Shift** to move freely. | 0.25 to 16. Defaults: 1, 2, 1, 3, 0.5, 4 for ghosts 1 to 6 |
| **FINE** | Fine tune. | -50 to +50 cents, 0 |
| **OUT** | How loud you hear this ghost. At 0 it is only heard through the ghosts it haunts. | 0 to 100%. Ghost 1: 80%, others 0 |
| **WAVE / BEND** | Waveform morph (Classic Morph) or phase bend (the tables). | 0 to 3, default 0 |
| **FEEDBACK** | The ghost haunts itself. | 0 to 100%, 0 |
| **VELOCITY** | How much velocity changes this ghost. | 0 to 100%, 50% |
| **DELAY** | Time before the envelope starts. | 0 to 4 s, 0 |
| **ATTACK** | Fade-in. | 1 ms to 10 s, 3 ms |
| **HOLD** | How long it stays at full level after the attack. | 0 to 4 s, 0 |
| **DECAY** | Fall to the sustain level. | 5 ms to 10 s, 0.6 s |
| **SUSTAIN** | Level while the key is held. | 0 to 100%. Ghost 1: 80%, others 40% |
| **RELEASE** | Fade-out after you let go. | 5 ms to 10 s, 0.35 s |
| **LOOP ENV** | Loops the envelope (attack, hold, decay) while the key is held: growls that talk by themselves. | off |
| **TO FILTER 2** | With the filters on **SPLIT** routing, this ghost goes through FILTER 2 instead of FILTER 1. | off |
| Scope | A small live picture of the output wave. | |

#### The NOISE ghost panel

Select the **NOISE** ghost. Drag it onto a ghost to add noise FM: grit, breath and crackle inside the growl.

| Control | What it does | Range / default |
|---|---|---|
| Type menu | **White, Pink, Crackle**. | White |
| **OUT** | How loud you hear the noise. | 0 to 100%, 0 |
| **CUTOFF** | Filter for the noise. | 20 Hz to 20 kHz, 20 kHz |
| **RESO** | Filter resonance. | 0 to 100%, 10% |
| Filter menu | **Low, Band, High**. | Low |
| **ATTACK / DECAY / SUSTAIN / RELEASE** | The noise envelope. | 1 ms, 0.3 s, 100%, 0.2 s |
| **TO FILTER 2** | Send the noise through FILTER 2 (SPLIT routing). | off |

#### The ADD SAMPLE ghost

This is the one sample feature. A sample becomes an FM source. It can haunt a ghost, be heard itself, or be used as a ghost's waveform.

1. Click the **ADD SAMPLE** ghost, or press **ADD SAMPLE** in its panel, or just drop a WAV, AIFF, FLAC or MP3 file anywhere on Poltergeist. (The file chooser lists wav, aif, aiff, flac and mp3. Dropping also accepts ogg.)
2. Poltergeist looks for the pitch of the sample and sets **ROOT** to it. If it cannot find a clear pitch, ROOT is C3.
3. If the sample is still silent (LEVEL and HAUNT at 0), it is set to LEVEL 50% and HAUNT 30% so you hear something at once. It loops by default.

| Control | What it does | Range / default |
|---|---|---|
| **ADD SAMPLE** / **REPLACE** | Pick a file. The button says REPLACE when a sample is loaded. | |
| **CLEAR** | Remove the sample (the ghosts keep playing). | |
| **ROOT** | The note the sample is in. The key you play then sounds that note. Pick **AUTO** to find it again. | C1 to C5 |
| Waveform | Drag **START** and **END**. Right-click = START here. Right-drag = LOOP (two fingers on a trackpad). Click the wave to hear it. | |
| **LOOP ON / LOOP OFF** (button on the waveform) | ON: it repeats while you hold a key. OFF: one-shot. | ON after adding |
| **LEVEL** | How loud you hear the sample itself. 0 = you only hear what it does to the ghost it haunts. | 0 to 100%, 0 |
| **HAUNT** | How hard the sample modulates its ghost (FM). A vocal or a growl here makes the FM voice talk. | 0 to 100%, 0 |
| **ATTACK** | Fade-in for each note. | 2 ms |
| **RELEASE** | Fade-out after you let go. | 0.2 s |
| **HAUNTS** | Which ghost (1 to 6) the sample modulates. You can also drag the SAMPLE ghost onto a ghost. That ghost must be heard (OUT) or haunt one that is. | Ghost 1 |
| **AS WAVE OF** | That ghost plays one cycle of the sample as its waveform. START picks which moment. Off = its own waveform. A dashed **WAVE** line shows the link on the field. | Off |

The sample's waveform has no export grip. Poltergeist reads files in only; it does not write them out.

> Old projects and sounds that used the removed sample pages keep working. If a sound carries an old sample mode, mute or reverse setting, a note under the waveform says so. It still plays as saved.

#### Cards under the field

**MAIN FILTER** (the simple filter, always on)

| Control | What it does | Range / default |
|---|---|---|
| Type menu | **Low, Band, High**. | Low |
| **CUTOFF** | Filter frequency. | 20 Hz to 20 kHz, 20 kHz (open) |
| **RESO** | Resonance. | 0 to 100%, 10% |
| **ENV** | How much the filter envelope opens the filter (negative closes). | -100% to +100%, 0 |
| **KEY** | How much the cutoff follows the note you play. | 0 to 100%, 30% |

**PITCH DROP** (a quick pitch fall at the start of each note: the 808 "boom")

| Control | What it does | Range / default |
|---|---|---|
| **AMOUNT** | How far the pitch starts above (or below) the note. | -48 to +48 semitones, 0 |
| **DECAY** | How fast it falls. | 5 ms to 2 s, 60 ms |

**DRIFT (LFO)** (a slow movement source used by the macros, TALK and the MOD MATRIX)

| Control | What it does | Range / default |
|---|---|---|
| **RATE** | Speed. | 0.02 to 30 Hz, 2 Hz |
| **SYNC** | Lock the speed to the tempo. | off |
| Division menu | Used when SYNC is on: 1/1, 1/2, 1/4, 1/4T, 1/8, 1/8T, 1/8D, 1/16, 1/16T, 1/32. | 1/8 |
| Shape menu | Sine, Triangle, Saw, Square, Random. | Sine |

**VOICE**

| Control | What it does | Range / default |
|---|---|---|
| **MONO** | One note at a time. | off |
| **RETRIG** | In mono: every new note restarts the envelopes, the sub and a retriggered wobble. Off = overlapping notes glide legato. | off |
| **GLIDE** | Slide time between notes. | 0 to 1 s, 0 |
| **OCTAVE** | Shift the whole synth. | -3 to +3, 0 |
| **VOICES** | How many notes at once. | 1 to 16, 8 |
| **MASTER** | Output level. | -24 to +6 dB, -6 dB |

The pitch wheel bends notes. The bend range is a saved setting (**Bend Range**, 0 to 24 semitones, default 2) that shows up as a parameter in your DAW's automation list, but there is no knob for it on screen. The sustain pedal holds notes (on sounds that use the newer engine revision; older presets keep the original engine, which ignores the pedal).

### Tab: MOVE

Three cards: RHYTHM, TALK (VOWEL FILTER), BUBBLES.

**RHYTHM.** Draw 16 steps with your mouse (click or drag across the bars). The playing step lights up.

| Control | What it does | Range / default |
|---|---|---|
| **RHYTHM ON** | Turns the rhythm on. | off |
| Rate menu | Step length (same list as the DRIFT divisions). | 1/16 |
| Target menu | What the rhythm moves: **Gate** (stutter), **Filter**, **Haunt** (FM growl), **Talk** (vowels) or **Pitch** (jumps). | Gate |
| **AMOUNT** | How strongly. | 0 to 100%, 100% |
| **SMOOTH** | Smooths the jumps between steps. | 0 to 100%, 10% |
| **STEPS** | How many steps are used. | 2 to 16, 16 |

**TALK (VOWEL FILTER).** Makes the bass speak (a, e, i, o, u).

| Control | What it does | Range / default |
|---|---|---|
| **TALK** | How much of the vowel filter you hear. | 0 to 100%, 0 |
| **VOWEL** | Which vowel. | 0 to 100%, 0 |
| **MOVE** | Lets the DRIFT LFO (or the rhythm when its target is Talk) work the mouth. | 0 to 100%, 50% |
| **RESO** | Sharpness of the vowel. | 0 to 100%, 60% |

**BUBBLES.** A pitch-dropping blip on every step: bubbly, poppy sparkle.

| Control | What it does | Range / default |
|---|---|---|
| **BUBBLES** | Amount. | 0 to 100%, 0 |
| **POP** | How far each blip falls. | 0 to 48 semitones, 24 |
| **TONE** | Brightness. | 0 to 100%, 40% |
| Rate menu | Blip rate. | 1/16 |

### Tab: FX

| Card | Control | What it does | Range / default |
|---|---|---|---|
| COMB | **COMB** | A tuned metal resonance that tears through growls. | 0 to 100%, 0 |
| | **FEEDBACK** | How long it rings. | 0 to 97%, 75% |
| | **TUNE** | Pitch offset of the comb. | -24 to +24 semitones, 0 |
| | **FOLLOW KEY** | The comb follows the note you play. | on |
| SYNC | **SYNC** | Ghost 1 hard-sync screech. Turn it with HAUNT for laser leads. | 0 to 100%, 0 |
| SQUEEZE | **SQUEEZE** | Three-band squash (down above, up below): the loud, bright, in-your-face finish. | 0 to 100%, 0 |

Drive, crush and space stay on the big macros (POSSESS, CURSE, VEIL).

### Tab: PLAY

Play helpers that change the notes you play before the synth hears them.

| Card | Control | What it does | Default |
|---|---|---|---|
| SCALE LOCK | **SCALE LOCK** | Snaps every key into the key and scale, so nothing sounds wrong. | off |
| | Key menu | C to B. | C |
| | Scale menu | Major, Minor, Harmonic minor, Dorian, Phrygian, Pentatonic minor, Pentatonic major, Blues. | Minor |
| CHORD (ONE FINGER) | Chord menu | One finger plays a chord: **Off, Power, Triad, 7th, 9th, Sus2, Sus4, Octaves**. In key when SCALE LOCK is on. | Off |
| ARPEGGIATOR | **ARP** | Plays the notes you hold one at a time, in time with your song. | off |
| | Rate menu | Step length (1/1 to 1/32, with triplets and dotted 1/8). | 1/16 |
| | Pattern menu | **Up, Down, Up-Down, Random, As played**. | Up |
| | **OCTAVES** | Repeat over this many octaves. | 1 to 3, 1 |
| | **GATE** | Note length as a part of the step. | 10% to 100%, 60% |

### Tab: BASS

Four cards: FILTERS, SUB + LOW END, DIST, MULTIBAND DRIVE.

The signal order is: ghosts, filters, POSSESS, DIST, MULTIBAND, comb, SQUEEZE, crush, space.

**FILTERS.** Two extra filters (separate from the MAIN FILTER on the GHOSTS tab). A small picture shows their combined response (**FILTERS OFF** when both are off).

| Control | What it does | Range / default |
|---|---|---|
| **FILTER 1** / **FILTER 2** | Switch each on. | off |
| Type menu (each) | Low 12, Low 24 Ladder, Band, High 12, High 24, Notch, Comb +, Comb -, Formant, Dirty. | Filter 1: Low 24 Ladder. Filter 2: Formant |
| Routing menu (top right of the card) | **Series** (1 into 2), **Parallel**, **Split** (ghosts marked TO FILTER 2 go through filter 2, the rest through filter 1). | Series |
| **CUTOFF** (each) | Frequency. With the **Formant** type this knob is called **VOWEL** (A, E, I, O, U). With a **Comb** type it is called **PITCH** (a note; set KEY to 100% to follow the note you play). | 20 Hz to 20 kHz. Filter 1: 800 Hz, filter 2: 1 kHz |
| **RESO** | Resonance. | 0 to 100%, 30% |
| **DRIVE** | Grit into the filter. | 0 to 100%, 0 |
| **KEY** | Follows the note. | 0 to 100%, 0 |
| **ENV** | Filter envelope amount. | -100% to +100%, 0 |

**SUB + LOW END.**

| Control | What it does | Range / default |
|---|---|---|
| **SUB** | Adds a sub oscillator. | off |
| **CLEAN SUB** | The sub skips the filters and every effect. | on |
| **SUB SOLO** | Plays only the sub, to check it. | off |
| Wave menu | Sine, Triangle, Square, Rounded. | Sine |
| Octave menu | -1 oct or -2 oct. | -1 oct |
| **FOLLOWS** menu | Which envelope shapes the sub: **Ghost 1**, **Longest Ghost** (a long sub under a short pluck), or **Own**. | Ghost 1 |
| **LEVEL** | Sub volume. | 0 to 100%, 70% |
| **LOW-PASS** | Cuts the top of the sub. | 40 Hz to 2 kHz, 400 Hz |
| **LOW MONO** | Everything below this frequency is mono. Under 20 Hz shows "Off". | 0 to 300 Hz, Off |
| **ATTACK / RELEASE** | The sub's own envelope. Only active when FOLLOWS is **Own**. | 2 ms / 0.12 s |

**DIST.**

| Control | What it does | Range / default |
|---|---|---|
| **DIST** | Turns the distortion on. | off |
| Type menu | Overdrive, Tube, Hard Clip, Fold, Bitcrush, Downsample. | Overdrive |
| **DRIVE** | Amount. | 0 to 100%, 50% |
| **TONE** | Brightness. | 0 to 100%, 100% |
| **MIX** | Dry / wet. | 0 to 100%, 100% |
| **QUALITY** | How many times faster than your project the voices, FM and drive run while you play. Higher = cleaner highs on hard FM, sync and drive, more CPU. | 2x, 4x, 8x. Default 2x |
| **RENDER** | The quality used when your DAW bounces offline. | Same as live, 4x, 8x. Default Same as live |

**MULTIBAND DRIVE.** Each band gets its own drive. Leave LOW at 0 to keep the sub clean while the mids scream.

| Control | What it does | Range / default |
|---|---|---|
| **MULTIBAND** | Turns it on. | off |
| **LOW / MID / HIGH** | Drive per band. | 0 to 100%. 0, 50%, 40% |
| **LOW X** | Crossover between low and mid. | 60 to 400 Hz, 150 Hz |
| **HIGH X** | Crossover between mid and high. | 800 Hz to 6 kHz, 2.5 kHz |
| **MIX** | Dry / wet. | 0 to 100%, 100% |

### Tab: WOBBLE

Three cards: WOBBLE (TEMPO-SYNCED, DRAWN), LFO 1 / LFO 2, MOD MATRIX.

**WOBBLE.** Draw a shape that repeats in time with your tempo. A label in the corner says how long one cycle is.

- Click an empty spot to add a point (up to 8).
- Drag a point to move it.
- Double-click a point to remove it (at least 2 stay).
- The first time you draw and nothing is using the wobble yet, Poltergeist points it at something you can hear: the filter cutoff (if a filter is on), or the volume.

| Control | What it does | Range / default |
|---|---|---|
| Rate menu | Length of one cycle: 4/1, 2/1, 1/1, 1/2, 1/2T, 1/2D, 1/4, 1/4T, 1/4D, 1/8, 1/8T, 1/8D, 1/16, 1/16T, 1/16D, 1/32, 1/32T. | 1/8 |
| **RETRIG** | The wobble restarts on every note. | off |
| **SHAPES** | A menu of ready-made shapes. | |
| **TARGET** | What the wobble moves (the same list as the MOD MATRIX targets; **Volume** means the shape is the volume). | Off (set for you when you first draw) |
| **DEPTH** | How far it moves the target. | -100% to +100%, 0 |
| **SMOOTH** | Rounds the corners of the shape. | 0 to 100%, 100% |

**LFO 1 / LFO 2.** Two more LFOs for the MOD MATRIX.

| Control | What it does | Range / default |
|---|---|---|
| **RATE** | Speed. | 0.02 to 30 Hz. LFO 1: 1 Hz, LFO 2: 0.25 Hz |
| **SYNC** | Lock to tempo. | off |
| Division menu | Same list as the wobble rate. | 1/4 |
| Shape menu | Sine, Triangle, Saw Up, Saw Down, Square, Random, Smooth Random. | Sine |

**MOD MATRIX.** Eight slots numbered 1 to 8. Each slot has a **source** menu, a **target** menu and an amount slider.

- Sources: Off, Wobble, LFO 1, LFO 2, Drift LFO, Filter Env, Ghost 1 Env, Velocity, Mod Wheel, Key, Random.
- Targets: Off, Filter 1 Cutoff, Filter 2 Cutoff, Filters 1+2 Cutoff, Filter 1 Reso, Filter 2 Reso, Vowel, FM Amount, Ghost 1 Level to Ghost 6 Level, All Ghost Levels, Pitch, Noise Level, Sub Level, Dist Drive, Wave Bend, Main Filter Cutoff, Volume.
- Amount: -100% to +100% (default 0). Right = positive, left = negative.
- Cutoffs move up to 6 octaves. Pitch moves 24 semitones. Ghost Levels scale the ghost's OUT and its FM depth, so a ghost at OUT 0 is only heard through the ghosts it haunts.

### Tab: MORPH

The MORPH board blends whole sounds. Each ghost on the board holds a complete sound. A glowing puck mixes them by distance.

- **Click the board** to add a ghost that holds the sound you hear now.
- Drag ghosts and the puck. Double-click a ghost to hear it alone.
- Right-click a ghost to set it from this sound, a snapshot or a preset.
- Up to 12 ghosts.

| Control | What it does |
|---|---|
| **MORPH ON** | The board plays the blend. Off = the knobs' sound. |
| **X / Y** | The puck position (you can automate these in your DAW). Default 0.5 and 0.5. |
| **ADD THIS SOUND** | Adds a ghost holding the sound you hear now. |
| **RANDOM BOARD** | Scatters the ghosts. |
| **ARRANGE IN CIRCLE** | Spaces the ghosts evenly round the middle. |
| **MIX** menu | How the ghosts mix: **Smooth** (all, by distance), **Islands** (only near ones), **Nearest 3**. |
| **ORBIT** menu | The puck moves by itself: **Off, Circle, Figure 8, Random walk**. |
| **SYNC** | The orbit follows the tempo. |
| **RATE** | Orbit speed: cycles per beat (SYNC) or per second. |
| **SIZE** | Orbit size. |
| **BLEND TO KNOBS** | The blend at the puck becomes the knobs' sound (MORPH turns off). Then SAVE or SNAPSHOT it. |
| **SAVE BOARD / LOAD BOARD** | Save or load the board (ghosts and their sounds) under a name. |
| **CLEAR** | Removes every ghost. |

Knob values glide between the ghosts. Switches and the sample follow the closest ghost.

### Presets, favourites, snapshots

Click the preset name to open the menu:

- **SEARCH...** finds presets by words from the name, folder or style.
- **SNAPSHOTS** lists the sounds you took with SNAPSHOT.
- **FAVOURITES** lists the sounds you starred.
- **ORIGINAL** holds the first set of presets.
- **BASS: ...** folders: GROWL / SCREECH, RIDDIM, WOBBLE, REESE / NEURO, ROLLING / CLEAN, SUB, 808, MID-BASS PLUCK, LEADS.

---

## Workflows

### Trap 808 slide

1. Press the **808** style chip and play low notes.
2. On the GHOSTS tab, set **MONO** on, and raise **GLIDE** a little (50 to 120 ms) so notes slide.
3. In **PITCH DROP**, set **AMOUNT** to about +24 and **DECAY** to about 50 ms for the "boom".
4. On the BASS tab, turn **SUB** on, keep **CLEAN SUB** on.
5. Overlap two notes in your piano roll. With **RETRIG** off, overlapping notes glide legato (a slide).

### Dubstep wobble that follows the tempo

1. Pick a preset from the **WOBBLE** folder.
2. On the WOBBLE tab, set the rate menu to 1/8 (or 1/4 for slower).
3. Draw a shape. Use **SHAPES** for ready-made ones.
4. Check **TARGET** and **DEPTH**. Push DEPTH up for a stronger wobble.
5. Turn on **RETRIG** if each note should start the wobble from the top.

### Talking growl

1. Start with a **GROWL / SCREECH** preset.
2. Go to MOVE. Raise **TALK** and move **VOWEL**.
3. Raise **MOVE** so the DRIFT LFO moves the vowel on its own, or set the RHYTHM **target** to **Talk** and draw steps.
4. Or on the BASS tab set a filter type to **Formant** and move its **VOWEL** knob with the wobble (MOD MATRIX).

### Reese with a clean sub

1. Pick a **REESE / NEURO** preset.
2. BASS tab: turn **SUB** on, **CLEAN SUB** on.
3. Set **LOW MONO** to about 120 Hz.
4. Turn **MULTIBAND** on and keep **LOW** at 0, so the sub stays clean while mids and highs get dirty.

### Put a vocal into the FM

1. Drop a WAV of a voice on Poltergeist.
2. Select the **HAUNTS** target you want (for example Ghost 1). Raise **HAUNT** on the sample panel.
3. Make sure Ghost 1 is heard (OUT up) or haunts a ghost that is.
4. Play notes. The voice shapes the FM, so the bass "talks".
5. Raise **LEVEL** if you also want to hear the sample itself.
6. Use **AS WAVE OF** to turn one cycle of the sample into a ghost's waveform.

### Blend two sounds with the MORPH board

1. Make a sound you like. On MORPH, press **ADD THIS SOUND**.
2. Load or build another sound. Press **ADD THIS SOUND** again (or right-click a ghost and set it from a preset).
3. Turn **MORPH ON** and drag the puck. Try **ORBIT: Circle** with **SYNC** for a moving blend.
4. Press **BLEND TO KNOBS** to keep the blend as a normal sound, then **SNAPSHOT** it.

### Play a bass line in key

1. PLAY tab: turn **SCALE LOCK** on and set the key and scale.
2. Optional: pick a **CHORD**, or turn on **ARP** to get a pattern from held notes.
3. Play any keys. They all land in the scale.

---

## FL Studio tips

- **Loading.** Add Poltergeist as an instrument (a "generator") from the plugin database. If you cannot find it, rescan plugins (see Troubleshooting).
- **MIDI.** Poltergeist is an instrument. It needs MIDI notes from a piano roll or a keyboard. It does not output MIDI. There is no MIDI or audio drag-out in Poltergeist. To get audio, bounce or render in FL Studio as usual.
- **Tempo sync.** The wobble, the RHYTHM, the ARP, the LFO divisions and the MORPH orbit follow your project tempo when they are in sync mode. When the host is not playing, the ARP uses its own clock.
- **Automation.** All the knobs, menus and switches show up as parameters. In FL Studio, right-click a knob and choose "Create automation clip". The **MORPH X / Y** puck position can be automated too.
- **Presets and sessions.** Your sound is saved with the project. **SAVE** writes a named sound file you can load in any project. **SNAPSHOT** keeps versions as you tweak. **SAVE BOARD** saves a MORPH board.
- **Latency.** The plugin reports its oversampling latency to the host. A higher **QUALITY** (4x, 8x) costs more CPU. Use **RENDER** to set a different quality for bounces.
- **Polyphony and CPU.** Lower **VOICES** and **QUALITY** if your CPU is busy.
- **Window size.** Drag the corner to resize the window.

### Where files live

| What | Mac | Windows |
|---|---|---|
| Saved sounds (`.ghostpatch`), `favourites.txt` | `~/Documents/Ghosty/Poltergeist` | `Documents\Ghosty\Poltergeist` |
| Saved boards (`.ghostboard`) | `~/Documents/Ghosty/Poltergeist/Boards` | `Documents\Ghosty\Poltergeist\Boards` |
| Snapshots (`.xml`) | `~/Library/Application Support/GhostBlock/Poltergeist/Snapshots` | `%APPDATA%\GhostBlock\Poltergeist\Snapshots` |
| Licence file | `~/Library/Application Support/GhostBlock` | `%APPDATA%\GhostBlock` |

In the SNAPSHOT list (**v**), "Show the folder" opens the snapshots folder for you. I did not find a log file location in the code.

---

## The looks

Poltergeist has three looks: **BLACK**, **PURPLE** and **LIGHT**. It does not have the fourth, glass-style GHOST look.

The look button is at the right end of the tab row (next to UNLOCK). Its text is the name of the next look. Tooltip: "Switch the look: BLACK -> PURPLE -> LIGHT (the button names the next look)". Click it to go to the next one. The look is saved with the project and is not part of any sound.

---

## Demo vs licensed

In demo mode (no licence key entered):

- The sound drops out for 2 seconds every 20 seconds.
- A project you reopen starts at the default sound. Unlocking brings the saved sound back.
- Saves (your sounds, snapshots, boards, the project) leave out audio that the plugin made itself. Your own imported samples are still saved. A hint near **UNLOCK** tells you when something was left out.
- Poltergeist has no audio or MIDI drag-out, so there is no export limit to hit.

Everything else works in the demo.

**To unlock:** click the **UNLOCK** button in the top bar (right end of the tab row). A box asks for your licence key (the one from your receipt email). Paste it and press **Unlock**. **Buy** opens the store page. After unlocking, the button reads **LICENSED**. Click it to see which key is in use. The licence is re-checked quietly in the background about every 14 days.

---

## Shortcuts

| Keys | What it does |
|---|---|
| **Shift+Alt+S** (Option+Shift+S on a Mac) | Take a SNAPSHOT (when no text box has focus). |
| **Ctrl/Cmd+Z** | Undo the last snapshot delete (within 60 seconds). |
| Enter | In the SUMMON box: summon. |
| Shift-click **<** / **>** | Jump to the previous / next preset folder. |
| Shift while turning **RATIO** | Move freely instead of snapping to musical ratios. |
| Double-click a knob | Back to the default. |
| Right-click a knob | Type a value. |
| Shift while dragging a MOD MATRIX slider | Fine adjust. |
| Double-click a link circle | Break an FM link. |
| Double-click a wobble point | Remove it. |
| Mod wheel | Adds to HAUNT and to the vowel. |
| Pitch wheel | Bends the notes. |
| Sustain pedal | Holds notes (newer-engine sounds only). |

Plain letter keys are not used on purpose, because hosts play the computer keyboard as piano keys.

---

## Troubleshooting

1. **No sound.** Check that **BYPASS** is off. Check that a ghost is heard: **OUT** above 0 on a ghost (ghost 1 is 80% by default), or SUB on, or NOISE OUT up. Check **MASTER**. Check that notes reach the plugin: click a key on the on-screen keyboard to test the sound itself.
2. **Sound only through a haunted ghost.** A ghost with OUT at 0 is silent on its own. The ghost it haunts must be heard.
3. **No MIDI.** Make sure the track sends MIDI to Poltergeist. In FL Studio, route your controller to the channel that holds the plugin. Poltergeist is an instrument, not an effect.
4. **Demo dropout.** A 2-second silence every 20 seconds is the demo. Click **UNLOCK**.
5. **Sound resets when I reopen a project.** That is the demo behaviour. Unlock brings the saved sound back.
6. **Gatekeeper warning on a Mac.** Until the installer is notarized, right-click the installer or plugin and choose **Open**, then **Open** again.
7. **Plugin not showing in FL Studio.** Make sure you installed the format your host uses (VST3, AU, CLAP). In FL Studio open the plugin manager and run **Find installed plugins** (a rescan). Restart FL Studio if needed.
8. **Highs sound harsh or aliased on hard FM.** Raise **QUALITY** on the BASS tab (4x or 8x). It costs CPU.
9. **The sample does nothing.** Both **LEVEL** and **HAUNT** may be 0, or the **HAUNTS** ghost is not heard. Raise LEVEL to hear it, and make sure the haunted ghost is heard.
10. **A file does not load.** Use WAV, AIFF, FLAC or MP3. A file with no sound in it is refused, and the status line says so.

---

## FAQ

**Is there a drag-out for audio or MIDI?** No. Poltergeist does not export files. Use your DAW to bounce audio.

**Can I use my own samples?** Yes: one sample, with ADD SAMPLE. It becomes an FM source. Sample editing, chopping and slicing are not in Poltergeist.

**How many sounds does it come with?** An ORIGINAL set plus the BASS folders. The preset menu lists them all, and SEARCH finds them by name or style.

**What does SUMMON do?** It matches your words to a style (for example "808", "growl", "bell") and builds a sound from a recipe. A new tap gives a new variation. Modifier words such as dark, bright, haunted, dirty, wide, glitch, lofi and metallic change the recipe.

**How do I keep a sound I tweaked?** Use **SNAPSHOT** for a quick keep, or **SAVE** to name it. Both can be loaded in any project.

**Why does the bass change when I press a key higher on the keyboard?** The filter **KEY** knobs and the comb can follow the note. Set KEY to 0 for a fixed filter.

**Can I automate everything?** Yes. The parameters appear in your DAW's automation list, including the MORPH X / Y.

---

## Glossary

- **Ghost.** One FM operator (an oscillator with its own envelope). There are six.
- **Haunt.** One ghost frequency-modulating another. Also the name of the first macro (FM growl).
- **NOISE ghost.** A noise source that can be heard or can haunt the ghosts.
- **ADD SAMPLE / SAMPLE ghost.** The sample slot. The sample can haunt a ghost, be heard, or be a ghost's waveform.
- **Macro.** One of the six big knobs: HAUNT, SPECTRE, POSSESS, DRIFT, VEIL, CURSE.
- **SUMMON.** Makes a sound from your words.
- **Drift.** The main slow LFO of the synth, also a macro.
- **Wobble.** A drawn, tempo-synced shape that moves a target.
- **Squeeze.** A three-band squash for a loud, bright finish.
- **Comb.** A tuned, metallic resonance.
- **Snapshot.** A saved copy of the whole sound in the SNAPSHOTS bank.
- **Board / puck / orbit.** The MORPH tab: a board of sounds, the puck that blends them, and the automatic movement of the puck.
- **Low Mono.** Makes everything below a chosen frequency mono.
- **Clean Sub.** A sub that skips filters and effects.
