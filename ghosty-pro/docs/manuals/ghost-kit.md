# GHOST KIT

**A 16-track drum machine and beat lab with a tracker, a melody tracker and a loop-to-MIDI converter.** Version 3.1.0. Formats: VST3, AU, CLAP, Standalone. (GHOST KIT was called Ghosty Beats; some hosts and folders still show the old name.)

You build beats on a step grid, vary them with generators and EVOLVE, polish every track, and drag the result into your DAW as MIDI or audio.

## Contents

- Quick start
- The screen
- Workflows
- FL Studio tips
- The looks
- Demo vs licensed
- Shortcuts
- Troubleshooting
- FAQ
- Glossary

---

## 1. QUICK START: make your first beat in 2 minutes

1. Insert GHOST KIT on a new instrument channel (see "FL Studio tips" below).
2. At the top, open the kit box (it reads "<name> kit") and pick **Trap kit**. Patterns **A** and **B** are loaded as a starter beat.
3. Press **PLAY** (or press play in your DAW: in **SYNC: DAW** mode the DAW runs GHOST KIT).
4. On the **SEQUENCER** page, click empty cells in the step grid to add hits. Click a lit cell to remove it. Hold **Shift** while clicking for an accent.
5. Click a track name on the left to hear it and select it. Use the track knobs (**PITCH**, **DECAY**, **VOLUME**, **PAN**...) to shape it.
6. Open the **RHYTHM LAB** page and press **GENERATE TRAP CLASSIC** (or INTRICATE, MACHINE GUN, DRILL, REGGAETON) for a new pattern.
7. Turn **EVOLVE** up a little so the beat changes bar by bar.
8. Drag **DRAG MIDI** into your DAW's piano roll or playlist to keep the notes. Drag **DRAG AUDIO** to keep a rendered WAV.
9. Press **SAVE BEAT** to keep everything (kit, samples, all patterns, the song).

> Tip: in the demo, step 8 is locked and the UNLOCK button blinks. See "Demo vs licensed".

---

## 2. THE SCREEN

The window has three zones: a **header** (kit, transport, drag-out), a **page area** with ten pages, and a **bottom strip** (master knobs, macros, performance). The name of the page you are on is lit in the page row.

### 2.1 The header

| Control | What it does |
|---|---|
| **Wordmark** (GHOST KIT) | Hover for the version. Click for the about card (click or Esc closes it). |
| **Kit box** | Pick one of 43 kits. Choosing a kit loads its 16 sounds and starter patterns A and B. See "Kits". |
| **PLAY** | Starts and stops GHOST KIT. Only active in **SYNC: OWN** or in the Standalone app. In **SYNC: DAW** the DAW's play button drives it. |
| **SYNC: DAW / OWN** | DAW = the DAW's transport and tempo. OWN = GHOST KIT's own tempo and PLAY (the DAW's transport and tempo are ignored). |
| **BPM box** | In DAW mode it shows the DAW tempo (read only). In OWN mode: drag, or double-click to type 20 to 999. In the Standalone app with no host: 60 to 200. |
| **TAP** | Tap the tempo (four taps or more). A pause of 2 s starts a new count. Works in OWN mode and Standalone. |
| **GROOVE box** | Timing feel: Straight, Swing 54%, Swing 58%, Swing 62%, Swing 66%, Triplet Shuffle, House Push. |
| **UNDO / REDO** | Every edit is one undo step. Generators, EVOLVE changes and RE-CREATE are each one step. |
| **Pattern letters** | Sixteen patterns, **A** to **P**. Click a letter to switch. The DAW can automate the "Pattern" parameter. |
| **Length box** | Pattern length from 1 to 128 steps. |
| **Resolution box** | Step size: 1/8, 1/16, 1/32, 1/16T, 1/8T, 1/64. |
| **COPY / PASTE / CLEAR** | Copy the current pattern, paste it over another letter, or clear the current one (locked tracks and locked steps are kept). |
| **PATTERN / SONG** | What the MIDI drag-out and the audio drag-out contain: the selected pattern, or the whole song. |
| **MIX / STEMS** | Audio drag-out as one mix, or as separate stems. |
| **CH 1 / CH 10** | The MIDI channel written into dragged MIDI files. Click to flip between 1 and 10. |
| **DRAG MIDI** | Grab and drag into the DAW: a .mid file of the pattern or song. Each track becomes a note (see "MIDI note map"). |
| **DRAG AUDIO** | Grab and drag into the DAW: a 24-bit WAV (mix or stems). It renders while you hover, with a progress fill. |
| **PLAY: ALWAYS / ON NOTES** | ALWAYS: the beat runs whenever the host (or PLAY) runs, and MIDI notes play the pads. ON NOTES: the beat runs only while a MIDI note is held (draw one long note per bar). Muting the channel in the DAW stops it. In ON NOTES, notes do not play pads or record. BYPASS wins over both. |
| **BYPASS** | Top right. Passes the plugin through. |
| **SONG MODE** | On: the beat follows the SONG page arrangement. Off: the selected pattern loops. |
| **Look button** | Switches the look. The button names the *next* one. See "The looks". |
| **FOCUS** | Shows only the tracks, grid and lanes, as big as the window. Press FOCUS again for everything. Remembered between sessions. |
| **UNLOCK / LICENSED** | License entry. Only visible in builds that need a license. |
| **Status line** | Green text under the header: short messages about what just happened. |

### 2.2 Page 1: SEQUENCER

The main page. Tracks are rows (16 tracks), steps run left to right, 16 steps per screen page.

**Track list (left).** Each row shows the track number, its sound and a level meter. At the right of each row:

- **MIDI** and **WAV** handles: drag one into the DAW for that track's MIDI or audio only.
- **M** and **S**: mute and solo.
- **L**: the LINEAR lock lane (shown when LINEAR is on, see "LINEAR DRUMMING").
- A small dot means the track holds your own sample (amber) or slices (cyan).

Click a row to select the track and hear it. Right-click a row for a menu: **Load sample...**, **Chop as a break (slices + rhythm)**, **Clear the sound**, **Lock / Unlock this track (generators skip it)**, and the MIDI note this track exports as. Drag a sound from the SOUNDS page (or an audio file from your DAW or Finder) onto a row to load it.

**Step grid.**

| Gesture | Result |
|---|---|
| Click an empty cell | Adds a hit (velocity 100). Click a lit cell to remove it. |
| Drag along a row | Paints (or erases) several steps. |
| Shift + click | Adds an accent (velocity 127). |
| Right-click or right-drag | Erases. |
| Double-click, or Alt/Cmd + click | Opens the **step editor** for that step. |
| Arrow keys, Enter, Delete | Move the cursor, toggle a hit, clear a step. |

If a pattern or a track is longer than 16 steps, **<** and **>** and the "steps 1-16" label page through it.

**Step editor.** Click anywhere on a bar to set it, drag to scrub (Shift = fine), right-click to type, double-click to reset.

| Field | Range | Default | What it does |
|---|---|---|---|
| VELOCITY | 0 to 127 | 100 | How hard the hit plays. |
| CHANCE % | 1 to 100 | 100 | Probability that the hit plays. |
| HITS (ROLL) | 1 to 16 | 1 | Splits the step into this many rapid hits. |
| RETRIG /BEAT | 0 to 64 | 0 | Retrigger rate per beat. |
| ACCEL | -100 to 100 | 0 | Speeds up or slows down the retriggers. |
| VEL RAMP | -100 to 100 | 0 | Velocity rises or falls across the retriggers. |
| PITCH | -24 to 24 | 0 | Semitones. |
| CENTS | -99 to 99 | 0 | Fine tune. |
| PITCH RAMP | -24 to 24 | 0 | Pitch moves across the roll. |
| NUDGE % | -50 to 50 | 0 | Moves the hit earlier or later than the grid. |
| FLAM ms | 0 to 40 | 0 | A quick grace hit before the main one. |
| DECAY % | 5 to 100 | 100 | Shortens this hit. |
| START % | 0 to 99 | 0 | Starts the sample part-way in. |
| CUT % | 0 to 100 | 0 | Cuts the hit short. |
| SLICE | 0 to 64 | 0 | Plays slice number N of the track's chopped sound. |
| DESTROY % | 0 to 100 | 0 | Per-hit destruction (see below). |
| FREEZE x10ms | 0 to 255 | 0 | Holds a frozen snapshot of the hit's spectrum as a tail (up to 2.5 s). |
| BAND REV 1L 2M 4H | 0 to 7 | 0 | Plays selected frequency bands backwards (1 = low, 2 = mid, 4 = high; add them up). |
| PAN | -50 to 50 | 0 | Pan for this hit. |
| FILTER 1-63 LP, 65-127 HP | 0 to 127 | 0 | A low-pass (1 to 63) or high-pass (65 to 127) on this hit. 0 = off. |
| reverse / lock / timing lock | toggles | off | Reverse the hit. Lock keeps generators and EVOLVE from changing it. Timing lock keeps its timing. |

Select several steps in the TRACKER first and the editor changes all of them at once.

**Lanes (below the grid).** One strip per property of the selected track, showing one bar per step. The buttons choose the lane: **VEL, NUDGE, PITCH, CHANCE, HITS, FLAM, DECAY, RETRIG, DESTR, FREEZE, PAN, FILTER**. Click or drag a bar to set it. Shift = fine. Right-click to type. Double-click resets the step.

**Track tools.** **FILL**, **EVERY 2**, **EVERY 4**, **RANDOM** (uses the **density** slider, 5 to 100, default 40), **<<** and **>>** (rotate), **REVERSE**, **DOUBLE**, **HALF**, **COPY**, **PASTE**, **CLEAR**. Locked tracks and locked steps are left alone.

**Track panel.** Sound name with **<** and **>** to step through sounds, **BROWSE** (goes to SOUNDS), **LOAD SAMPLE**, **REVERSE**, **LOCK** (generators skip this track), **CHOP**, **REARRANGE**, a track length box ("Length = pattern" or 1 to 128, so tracks can run in polymeter), a choke box (No choke, Choke 1 to 4), and the **sens** slider for CHOP (0 to 100, default 50).

**Track knobs.**

| Knob | Range | Default |
|---|---|---|
| PITCH | -24 to 24 semitones | 0 |
| DECAY | 0.05 to 1 | 1 |
| VOLUME | 0 to 2 | 0.8 |
| PAN | -1 to 1 | centre |
| TONE | 0 to 1 | 0.5 (centred) |
| DRIVE | 0 to 1 | 0 |
| SWING | 0 to 1 | 0 |
| ACCENT | 0 to 1 | 0 |
| HUMANIZE | 0 to 1 | 0 |

Double-click any knob to reset it.

**REGION / SLICES tabs.** Under the knobs, two tabs show the selected track's sound.

- **REGION:** set **START**, **END** and a **LOOP** by dragging the flags. Right-click sets START, right-drag sets LOOP. A WAV grip lets you drag the region out as audio.
- **SLICES** (BREAK CHOP): click to add a slice line, drag to move it, right-click to remove it. Drag a slice out as a WAV.

On large windows a velocity lane and a 4-track mini tracker sit beside the grid.

**LOOP -> MIDI...** (button on this page) opens the RE-CREATE AS MIDI panel for an audio file. See section 2.9.

### 2.3 Page 2: SOUNDS

The sound browser. The folder list on the left: **KICKS, SNARES, CLAPS, HATS, PERC, CYMBALS, FX, 808s, GLITCH, BREAKS, SHARED, FAVOURITES**. The search box finds sounds across all folders ("kick, crack, 808, riser...").

- Click a sound to hear it. The info box shows its name, folder, length, and whether it is a loop that chops automatically.
- Double-click (or drag it onto a track row) to load it. **LOAD ON SELECTED TRACK** does the same for the selected track.
- **LOAD A FILE...** loads one of your own files on the selected track (WAV, AIFF, FLAC, MP3, OGG, M4A).
- **NORMALISE DROPPED SAMPLES** makes your dropped samples a consistent level.
- A star marks a favourite.
- **SHARED** lists WAV files in `Documents/Ghosty/Shared Sounds`.

**Dropping your own samples.** Drag any WAV, AIFF, FLAC or MP3 from Finder/Explorer, or straight from FL's browser, onto a track row or a pad. Several files fill the next tracks. Drop on an empty spot to use a free track. Long loops are chopped into slices automatically. Silence is trimmed. Your samples are saved inside the project.

### 2.4 Page 3: PROCESS

Per-track processing for the selected track, plus the bus.

| Group | Controls |
|---|---|
| Track processing | **TRANSIENT** (-1 to 1), **SUSTAIN** (-1 to 1), **SATURATE** (0 to 1), **PUNCH** (0 to 1), **LOW CUT** (0 to 1), **TILT** (-1 to 1), **HIGH CUT** (0 to 1, default 1 = open), **SC DUCK** (0 to 1) |
| Envelopes | **AMP ATT ms** (0 to 200), **GLIDE ms** (0 to 500, the 808 legato glide), **PITCH ENV** (-24 to 24), **ENV ms** (1 to 1000, default 60), **VEL>LEVEL** (0 to 1, default 1) |
| Sidechain box | "Sidechain: first kick", or any track 1 to 16 as the ducking source |
| Layers | **LAYER 1** and **LAYER 2**: stack up to two more sounds on the track (a kick with a punch layer and a sub layer). Each has **LEVEL**, **PITCH** (-24 to 24), **OFFSET ms** (0 to 50). |
| Character | A "Character" box plus **AMOUNT** and **DRIVE** (0 to 1): play the track through an old-sampler colour. |
| Bus (all tracks) | **GLUE**, **CLIPPER**, **ROOM**, **FILTER RESO**, **CRUSH**, **CEILING dB**, and a limiter box: CLEAN (look-ahead), SOFT, or OFF (mix in the DAW). |
| Conditional rules | Four rule rows per track, each with a type, a source (track or bar), N and a percent. Types: "If [track] played the step before -> ghost note", "Every Nth bar -> more retriggers", "Silent for N steps -> hit", "After a loud hit -> next one quieter", "Every Nth bar -> drop hits", "Play only on bar [k] of N", "If [track] plays here -> skip", "Every Nth step -> accent". Rules are seeded, so the same beat gives the same result. |

### 2.5 Page 4: TRACKER

A tracker-style view of the pattern: steps as rows, tracks as columns (**TRACKS 1-8** and **TRACKS 9-16** buttons switch the bank). Each cell has a velocity field and a command field.

- Move with arrows, Tab switches field. Type digits for velocity.
- Command keys (listed on the page): **R** hits, **T** retrig per beat, **A** accel, **V** velocity ramp, **P** pitch, **F** cents, **G** pitch ramp, **D** delay, **L** flam, **E** decay, **O** start, **C** cut, **S** slice, **X** destroy, **Y** chance, **B** reverse. **Z** freeze, **W** band reverse, **N** pan, **H** filter.
- **Enter** commits and moves down. **Del** clears. **Shift + arrows** select a block. **Cmd/Ctrl + C / V** copy and paste the block.
- The **preview box** ("Edit the pattern" or "Preview evolved bar N") shows what EVOLVE will play in bar N (up to 64). While previewing you cannot edit.
- Double-click a step to open the step editor.

### 2.6 Page 5: RHYTHM LAB

This page has two tabs: **RHYTHM TOOLS** and **LINEAR DRUMMING**. On the right is the BREED area.

**Rhythm tools (left).**

| Control | What it does |
|---|---|
| **EVOLVE** (0 to 100 %) | Bar-by-bar variation while the pattern plays. The kick on 1 and the backbeat snares stay. |
| **INTRICATE / MACHINE GUN** | The style EVOLVE follows (see "Styles"). |
| **NEW SEED** | Rolls a new random seed for this pattern. The seed is shown next to FILLS. |
| **FILLS** | No fills, or a fill every 2, 4 or 8 bars. |
| **PHRASE arc** | A 16-bar intensity drawing. Draw it with the mouse. Sections are labelled ESTABLISH, VARY, BUILD, RESOLVE. EVOLVE follows it. |
| **CONVERGENCE** | For polymeter tracks: never realign, or realign every 1, 2, 4, 8 or 16 bars. |
| **GENERATE INTRICATE / MACHINE GUN / DRILL / REGGAETON / TRAP CLASSIC** | Make a new pattern in that style (see below). One undo step each. |
| **DRILL GRIT** (0 to 100 %, default 70) | Used by the next GENERATE DRILL. 0 = the clean drill, higher = tougher and dirtier. Saved with the beat. |
| **KEEP x% IDENTITY** + **KEEP + NEW HITS** | Keeps that percent (10 to 100, default 80) of the pattern's identity and replaces the rest with new hits. |
| **BLEND** | Pick pattern A and pattern B and a weight (0 to 100 % B, default 50). It blends their *structure* beat by beat, not a crossfade. |
| **PERFORMANCE** + **BAKE TO SONG** | Choose 8, 16, 32 or 64 bars. BAKE turns the evolving performance into real patterns after the current one, plus a song (SONG MODE on). |
| **PHRASE LOCKS** | Four lock rows: pick a start bar (1 to 16) and a length (off, 1, 2, 4, 8 bars) and which tracks (ALL or one). Locked bars play exactly as written. |
| **MOTIF** | **LOCK MOTIF** captures a figure of the selected track (from a step, 1 to 16 steps long) so every bar keeps it. Up to 4 motifs, on a 1/16 grid. **CLEAR MOTIFS** frees them. |
| DNA display | Shows the pattern's hits per bar position, with accents in amber, and numbers for syncopation, velocity, retrig and silence. |

**BREED (right).** **NEW BREED** makes 16 variations of the current pattern as tiles. Click a tile to hear it (it is placed in the current pattern, and UNDO goes back). Click the star to mark favourites. **NEXT GENERATION** breeds a new set that leans toward what your starred tiles share.

**Locks.** Double-click a step and choose "lock", or use **LOCK** on a whole track. Generators, EVOLVE, KEEP, BLEND and BREED never touch locked hits.

#### Styles

| Style | What GENERATE gives you |
|---|---|
| **INTRICATE** | A busy, detailed pattern. (Also an EVOLVE style.) |
| **MACHINE GUN** | Dense rolling retriggers. (Also an EVOLVE style.) |
| **DRILL** | 138 to 146 BPM in half time. Snare on beat 3, displaced in bar 2. 3-3-2 hats. A sliding 808. Each press rotates the 808 pattern, snare spot and hat cell. Two bars. Set the **DRILL GRIT** slider first. |
| **REGGAETON** | 88 to 100 BPM. Kick on 1 and 3, the dembow snare/rim figure, straight hats, shaker 16ths, clave, congas and bongos, cowbell, timbale fills every 4 or 8 bars. Each press rotates the pattern family. |
| **TRAP CLASSIC** | 140 to 160 BPM in half time. Rolling hats (1/32, triplet, accelerating, pitch-rising, 1/64 rolls with velocity ramps), open-hat off-beats, a long 808 on the kicks with slides, snare and clap on 3, crash and brass on the phrase start, a snare roll into the drop. Four bars. Each press rotates the pattern family and the roll order. |

The generated tempo is a suggestion shown in the status line. The pattern is in beats, so it plays at your project tempo.

**LINEAR DRUMMING tab.** Makes the beat one-voice-at-a-time, like a drummer who never hits two things at once. Hits closer than the collision window collide. The rule picks one, and the others move to the nearest free step or are dropped.

| Control | What it does |
|---|---|
| **LINEAR OFF / ON** | The switch. Off = the beat exactly as written. |
| **RULE** | Who wins a collision: Priority order, Keep accents + downbeats, Alternate, Keep the groove, Random (seeded). |
| **MOVE losers / DROP losers** | Losers move to a free step (flams, rolls and retrigs move together), or are dropped. |
| **COLLISION WINDOW** | A slider, shown as a percent of a 1/16 and in milliseconds. Double-click resets (25 %). |
| **VARIATION < >**, **NEW SEED** | Other versions of the resolution. Each version keeps its own seed. |
| **PRIORITY ORDER** | A drag-to-reorder list of the 16 tracks (first wins). Each row has a **lock** chip: a locked track always plays. **AUTOMATIC ORDER** resets it. |
| **SHOW AS WRITTEN / SHOW DROPPED** | Preview display options. |
| Preview | Two bars: the hits as written (faint), what LINEAR plays (solid), moved hits (outlined), dropped hits (hollow). |

The beat itself is never edited. DRAG MIDI and DRAG AUDIO contain exactly what plays. Live pads, MIDI drum notes and NOTE REPEAT are played as you play them.

### 2.7 Page 6: PADS

A 4x4 grid of the 16 pads (pad 1 is bottom left). Tap pads to play (higher on a pad = harder).

- **REC**: with PLAY running, writes what you tap into the current pattern.
- **NOTE REPEAT** with a rate box (1/8, 1/16, 1/32, 1/16T): hold a pad for rolls.
- MIDI notes **C1 to D#2** play pads 1 to 16 (when PLAY is ALWAYS).

### 2.8 Page 7: SONG

Arrange patterns into a song. The lane shows blocks; pattern chips A to P add blocks. Buttons: **DELETE BLOCK**, **DUPLICATE**, **CLEAR SONG**. Turn on **SONG MODE** (header) and the beat follows the arrangement bar by bar while the DAW (or PLAY) runs.

### 2.9 RE-CREATE AS MIDI

Turns an audio loop (drums, a break, a bassline) into an editable pattern or melody. Open it three ways: drag an audio file onto the step grid, the lanes or the melody tracker; or press **LOOP -> MIDI...** and pick a file; or (when dragging) watch the overlay: "DROP: RE-CREATE AS MIDI" on the grid and "DROP: USE AS SOUND" on a track row.

The loop is analysed in the background, with a progress bar and **CANCEL**. The result is applied at once as one undo step, and the panel stays open so you can fine-tune. Each change replaces the last result.

| Control | What it does |
|---|---|
| **DRUMS / PITCHED** | DRUMS writes a drum pattern. PITCHED writes a bassline or melody onto the MELODY tracker. The panel picks one automatically; you can switch. |
| **BPM** (20 to 999) | The loop's tempo, found from the file name, the loop length or its hits. Right-click to type. **x2** and **/2** fix a double or half tempo. |
| **grid** | 1/8, 1/16 or 1/32. |
| **% quantize** (0 to 100, default 100) | 100 puts every hit on the grid. Less keeps that much of the loop's own timing as step NUDGE. |
| **also use the loop's own hits as the sounds** | Off: only MIDI (the kit's own sounds play the new pattern). On: the loop's strongest hit of each kind becomes that track's sound. |
| **RE-CREATE AS PATTERN** | Apply again with the current settings. |
| **USE AS SOUND ON THIS TRACK** / **CHOP TO SLICES** | The old drop behaviour: load the file as a sound, or chop it into slices. |
| **CLOSE** | Closes the panel (Esc also works). |

**How the drum separation works (as it is in the code).** The analysis finds onsets, then guesses what drum each one is from the energy in a low, a mid and a high band during its first 40 ms:

- KICK: strong low energy.
- SNARE: strong, noisy mid energy. CLAP: a snare with several quick bursts.
- TOM: strong, tonal mid energy.
- HAT: strong high energy, or a hat on the loop's regular hat grid (even if hidden under a snare). OPEN HAT: a hat that takes a long time to fade.
- PERC: anything else that is loud enough.

One onset can count as more than one drum. Hits are then written onto the kit's tracks by role. If a role has no track, it falls back to the closest one. Locked tracks are left alone. Velocity comes from each role's energy.

> Tip: **This part of GHOST KIT is being improved right now. Results vary with the loop.** Clean, dry, single-bar drum loops work best. Dense or heavily layered loops, loops with long reverb, and loops where a kick and a snare hit together can come out with wrong or missing hits. Always listen, then fix by hand.

Only the first 2 minutes of a file are read. Pitched mode follows notes with a pitch tracker and needs a clearly pitched loop.

### 2.10 Page 8: SCRIPTS

Small rule "modules" that act on every step, in plain text. Up to 8 modules. Buttons: **NEW**, **APPLY**, **DELETE**, **SAVE MODULE**, **LOAD MODULE** (with built-in examples and your saved ones).

Rules are one per line: `if <condition> then <actions>` and an optional `else`. Values include bar, step, pos, beat, track, role, hit, vel, pitch, density, len, seed. Actions include hit(vel), mute(), vel(v), accent(x), pitch(semis), retrig(n, rate, accel, ramp, vramp), reverse(), offset(%), cut(%), nudge(%), prob(%), flam(ms), decay(%), slice(n), destroy(%), freeze(ms), frev(mask). Scripts are safe: no loops, no files, seeded, so the same beat gives the same result. The help text on the page lists every value and action. Errors show under the editor.

Saved modules live in `Documents/Ghosty/Ghosty Beats/Scripts` as `.ghostscript` files.

### 2.11 Page 9: MIX

Sixteen channel strips plus the master. Each strip has a level meter, a fader, a pan bar and **M** / **S**. Drag faders (Shift = fine, double-click = reset). Right-click a fader to type dB (or "-inf"). Red on a meter means over -0.5 dBFS before the limiter.

### 2.12 Page 10: MELODY

A tracker for melody and bass. The 16 **instruments** each own a sound and play through a **ROUTE** track. The song has 8 channels, each with NOTE, INSTRUMENT, VOLUME and EFFECT fields.

- **MELODY ON / OFF** plays the tracker together with the drums. **WITH DRUMS** uses pattern A with drum pattern A. **OWN ORDER** gives the melody its own pattern order.
- Instrument settings: sound, route track, slice, root, tune, vol, pan, envelope, start/end, loop, filter. You can load your own sample for an instrument, or have an instrument take over a track's sound.
- **MIDI REC** records notes you play.
- **DRAG MELODY MIDI** drags the melody as a .mid. **DRAG BEAT + MELODY MIDI** drags both together.
- **XM / IT...** menu: import an XM or IT file into the melody tracker, or export the melody as XM or IT, with patterns, instruments and samples.
- Press **?** on the page to see every effect and its help.

### 2.13 The bottom strip

| Group | Controls |
|---|---|
| Master | **SWING, HUMAN, GLITCH, DJ FILTER, ECHO, REVERB, DRIVE, VOLUME** |
| Macros | **CHAOS, DENSITY, MUTATION, FRACTURE, GRAVITY, MEMORY, TENSION, RESOLVE** (0 to 1; DENSITY, GRAVITY and MEMORY rest at 0.5, the rest at 0). They steer how the pattern changes while it plays. With all macros at rest the beat plays as written. All are automatable. |
| Performance | **STUTTER**: press and hold 1/4, 1/8, 1/16 or 1/32, drag across to change rate, release to stop. **TAPE STOP**: hold it. |
| Files | **SAVE BEAT**, **LOAD BEAT** (see FL Studio tips) |

### 2.14 Kits and the drum library

GHOST KIT makes all its drum sounds itself. There are no sample packs to install. Sounds are generated on first start and cached.

**The 43 kits:** Trap, Drill, Boom Bap, Lofi, House, Dubstep, Afrobeats, Phonk, Reggaeton, Jersey Club, Club Progressive, Big Room, Dubstep Heavy, Riddim, Brostep, Electro House, Tech House, Melodic Techno, Future Bass, Drum & Bass, Trap Hard, Drill Dark, Phonk Drift, Lo-fi Tape, Boom Bap Dusty, Afro House, Garage, Hardstyle, Pop, IDM Glitch, Cinematic, DRILL, LATIN, ANALOG BOOM, ANALOG PUNCH, DIGITAL EIGHTIES, CRUNCH TWELVE, SWING TWELVE, TRAP GOLD, TEXAS BOUNCE, BOOM BAP CLASSIC, POP SHINE and TRAP CLASSIC.

Each kit has 16 sounds, a starter pattern A and a variation B. Kits also set a starting tempo, hat swing and per-track processing. Loading a kit loads its 16 sounds and starter patterns.

**Your own samples** can go on any track (SOUNDS page, track menu, or drop). They are saved inside the project. A "Shared" folder in `Documents/Ghosty/Shared Sounds` appears under SHARED.

**DESTRUCTION.** The per-hit DESTROY, FREEZE and BAND REV fields are rendered offline and cached per sound. FREEZE holds a spectral snapshot of the hit as a tail. BAND REV plays chosen bands backwards.

**Rhythm lab and user samples together.** Locked tracks (LOCK) are never touched by generators, so you can keep your own sampled kick and let GENERATE write the rest.

### 2.15 MIDI note map

Each track is exported as a note by role (for example a kick as note 36, i.e. C1), so the MIDI sounds right on a General MIDI drum kit as well as in GHOST KIT. Other tracks fall back to `36 + track number`. The right-click menu on a track row shows its exact note. The channel is set by **CH**.

---

## 3. WORKFLOWS

### 3.1 Trap beat with rolling hats and a sliding 808

1. Load the **TRAP CLASSIC** kit.
2. RHYTHM LAB: **GENERATE TRAP CLASSIC**. Press again to rotate to another pattern family.
3. SEQUENCER: select the 808 track. On the PROCESS page, raise **GLIDE ms** for the slide.
4. Edit a hat step with the step editor: **HITS (ROLL)** 3 to 8, **VEL RAMP** for a swell.
5. Switch on **LOCK** for the 808 track once you like it.
6. Press **GENERATE TRAP CLASSIC** again. Everything except the locked track changes.

### 3.2 Drill pattern

1. Load the **DRILL** kit.
2. RHYTHM LAB: set **DRILL GRIT** (0 clean, 100 dirty), then **GENERATE DRILL**.
3. Each press rotates the 808 pattern, snare spot and hat cell.

### 3.3 Make it breathe: EVOLVE + PHRASE

1. Turn **EVOLVE** to 20 to 40 %.
2. Draw a PHRASE arc that rises to BUILD and falls at RESOLVE.
3. Add **FILLS** every 4 bars.
4. Use the TRACKER page preview box ("Preview evolved bar N") to inspect a bar.
5. Like a stretch? Choose 16 or 32 bars and press **BAKE TO SONG**.

### 3.4 Fix timing

1. Select a track and open its lane **NUDGE** to move single hits.
2. Use **HUMANIZE** (track knob) or the master **HUMAN** for a gentle feel, and the **GROOVE** box for swing.
3. For a loop you imported: in RE-CREATE AS MIDI lower **% quantize** to keep some of the loop's own timing.

### 3.5 Re-create a drum loop as MIDI

1. Drag your loop onto the step grid (or press **LOOP -> MIDI...**).
2. Wait for the progress bar. The pattern appears.
3. Check **BPM**: use **x2** or **/2** if the loop was found at double or half.
4. Choose a **grid**, adjust **% quantize**.
5. Turn on "also use the loop's own hits as the sounds" if you want its sound.
6. Listen, then clean up the grid by hand. Results vary with the loop.

### 3.6 Chop a break

1. Drop a break loop on a track row (use the "DROP: USE AS SOUND" zone).
2. Press **CHOP** (adjust the **sens** slider) or use "Chop as a break" from the track menu.
3. Open the **SLICES** tab to add, move or remove slice lines.
4. Press **REARRANGE** for a new slice order with the same groove.

### 3.7 Drag MIDI to FL Studio

1. Choose **PATTERN** or **SONG**, and the **CH** you want.
2. Drag **DRAG MIDI** into the piano roll or playlist.
3. For one track only, drag the **MIDI** handle on its row. For audio of one track, drag the **WAV** handle.

### 3.8 Melody from a bassline loop

1. Drop a bass loop on the **MELODY** page (the melody tracker is a drop zone), or use **LOOP -> MIDI...**.
2. Pick **PITCHED** if it was not picked automatically.
3. Press **DRAG MELODY MIDI**, or **DRAG BEAT + MELODY MIDI** for drums and notes together.

---

## 4. FL STUDIO TIPS

**Loading.** After install, rescan plugins (Plugin database > "Find installed plugins"). Add GHOST KIT from the plugin picker. It appears under the name "Ghosty Beats" in some hosts, because the plugin ID is kept for compatibility.

**Instrument or effect.** GHOST KIT is an instrument: it makes sound by itself and listens to MIDI. Load it as a generator (instrument) and play it from the piano roll, or let it run by itself.

**Tempo sync.** In **SYNC: DAW**, tempo and play position come from FL. Switch to **SYNC: OWN** for GHOST KIT's own tempo and PLAY. In Standalone with no host, tempo is a 60 to 200 BPM parameter.

**Playing the beat from MIDI.**

- **PLAY: ALWAYS:** the beat runs whenever FL runs. Notes **C1 to D#2** play pads 1 to 16.
- **PLAY: ON NOTES:** draw one long note per bar in FL's piano roll. The beat plays only while the note is held.

**Automation.** Right-click a knob and choose "Link to controller" (or the host's equivalent). The DAW can automate the master knobs, the macros, tempo, groove, pattern, and the bus controls (GLUE, CLIPPER, ROOM, FILTER RESO, CRUSH, CEILING dB, limiter).

**Dragging MIDI and audio out.** Drag **DRAG MIDI** or **DRAG AUDIO** into the playlist or a channel. The row handles drag one track. Dragging needs a licensed copy (see Demo vs licensed).

**Saving.** The host saves the whole plugin state with the project. In the demo, a reopened project starts at the default sound. **SAVE BEAT** writes a `.ghostbeat` file with the kit, your samples, all 16 patterns and the song. It also works in the demo.

**Latency.** The CLEAN limiter (PROCESS page) looks ahead, and the plugin reports that delay to the host so the DAW can compensate. Switch the limiter box to SOFT or OFF if you want none.

**Where files live.**

| What | Mac | Windows |
|---|---|---|
| Saved beats (`.ghostbeat`) | `~/Documents/Ghosty/Beats` | `Documents\Ghosty\Beats` |
| Saved patterns (`.ghostpat`) | `~/Documents/Ghosty/Patterns` | `Documents\Ghosty\Patterns` |
| Script modules | `~/Documents/Ghosty/Ghosty Beats/Scripts` | `Documents\Ghosty\Ghosty Beats\Scripts` |
| Shared sounds | `~/Documents/Ghosty/Shared Sounds` | `Documents\Ghosty\Shared Sounds` |
| Drum cache (rebuilt automatically) | `~/Documents/Ghosty/Ghosty Beats/Cache` | `Documents\Ghosty\Ghosty Beats\Cache` |
| Settings (look, FOCUS, channel) | `~/Library/Application Support/Ghosty` | the Windows app-data folder, under `Ghosty` |
| License file | `~/Library/Application Support/GhostBlock` | the Windows app-data folder, under `GhostBlock` |
| VST3 | `/Library/Audio/Plug-Ins/VST3` | `C:\Program Files\Common Files\VST3` |
| AU / CLAP (Mac) | `/Library/Audio/Plug-Ins/Components` and `/CLAP` | not applicable |

Deleting the Cache folder is safe. It is rebuilt on the next start.

---

## 5. THE LOOKS

GHOST KIT has four looks: **BLACK**, **PURPLE**, **LIGHT** and **GHOST** (the 4th look, liquid glass).

Use the look button in the header (tooltip: "Switch the look: BLACK -> PURPLE -> LIGHT -> GHOST (liquid glass)"). The button is labelled with the *next* look, so it reads "PURPLE" while you are on BLACK. Click to move on; after GHOST it returns to BLACK. The choice is remembered. If you have several GHOST KIT windows open, the others follow.

---

## 6. DEMO VS LICENSED

In demo mode (no license key) everything works except:

- **The sound drops out for 2 seconds every 20 seconds.**
- **Export is off.** A reopened project starts at the default sound.
- **Nothing leaves the plugin as MIDI or audio.** These are blocked: DRAG MIDI, DRAG AUDIO (mix and stems), DRAG MELODY MIDI, DRAG BEAT + MELODY MIDI, the MIDI and WAV handles on track rows, the WAV grip on REGION, a slice dragged out as WAV, and Export as XM and Export as IT.

When you try one, the **UNLOCK** button blinks and a short hint appears: "Export is part of the full version." Nothing is written.

Still allowed in the demo: SAVE BEAT and LOAD BEAT (the plugin's own format), saving patterns, using the whole sequencer, and internal drags (sounds onto tracks).

**To unlock:** click the **UNLOCK** button (it turns into **LICENSED**). Paste the license key from your Gumroad receipt email into the box and press **Unlock**. Press **Buy** if you do not have one. The key is checked online. Hover the button to see the demo rules or the first and last characters of your key.

---

## 7. SHORTCUTS

| Keys / mouse | Where | Action |
|---|---|---|
| SPACE | In SYNC: OWN (or Standalone) | Play / stop. In DAW mode SPACE goes to the DAW. |
| Cmd/Ctrl + Z | Anywhere | Undo. Add Shift to redo. |
| Cmd/Ctrl + Y | Anywhere | Redo. |
| Cmd/Ctrl + C / V | SEQUENCER | Copy / paste the selected track's steps. |
| Click / drag / Shift + click | Step grid | Add, paint, accent. |
| Right-click / right-drag | Step grid | Erase. |
| Double-click, Alt/Cmd + click | Step grid | Open the step editor. |
| Arrows, Enter, Delete | Step grid | Move, toggle, clear. |
| Right-click | Track row | Track menu. |
| Right-click | Slice line | Remove it. |
| Right-click | Any knob or bar | Type a value. |
| Double-click | Knob or bar | Reset to default. |
| Shift + drag | Bars and faders | Fine control. |
| Esc | RE-CREATE panel, about card | Close. |
| `?` | MELODY page | Show all tracker effects. |
| Z S X D C V G B H N J M | MELODY page | Notes (bottom row). Q 2 W 3 E R 5 T 6 Y 7 U I 9 O 0 P = the octave above. |
| `[` `]` | MELODY page | Octave down / up. |
| `{` `}` | MELODY page | Edit step down / up. |
| Insert, Shift + Delete | MELODY page | Insert / delete a row. |
| Cmd/Ctrl + Up / Down | MELODY page | Transpose the selection (+Shift = an octave). |
| Cmd/Ctrl + I | MELODY page | Interpolate volume and effects over a selection. |

---

## 8. TROUBLESHOOTING

**1. No sound.** Check that the plugin is loaded as an instrument and its mixer track is not muted. In SYNC: DAW press play in the DAW. In SYNC: OWN press PLAY. Check **BYPASS** is off and that no track is on solo. If PLAY is **ON NOTES**, the beat only runs while a MIDI note is held. Switch it to ALWAYS.

**2. No MIDI response.** In **PLAY: ON NOTES** the notes only start and stop the beat and do not play pads. Switch to **ALWAYS**. In ALWAYS, notes C1 to D#2 play pads 1 to 16. Make sure the FL channel is routed to the plugin.

**3. The sound cuts out for 2 seconds.** That is the demo drop-out (every 20 seconds). Enter your license key to remove it.

**4. Drag-out does nothing and UNLOCK blinks.** Export is part of the full version. Unlock first.

**5. A reopened project starts at the default sound.** Also a demo limit. Use SAVE BEAT (works in the demo) for your own saves, or unlock.

**6. Mac says the plugin "can't be opened" (Gatekeeper).** Until the installer is notarized, right-click the installer and choose **Open**, then confirm. If the plugin itself is blocked, go to System Settings > Privacy & Security and choose "Open Anyway".

**7. The plugin is not showing in FL Studio.** Rescan plugins (Plugin database > "Find installed plugins"). Make sure the VST3 folder is in FL's plugin search paths. It lives in `/Library/Audio/Plug-Ins/VST3` on Mac and `C:\Program Files\Common Files\VST3` on Windows.

**8. RE-CREATE AS MIDI gets the wrong hits or the wrong tempo.** Use **x2** or **/2** for a double or half tempo, or type the BPM. Put the tempo in the file name ("loop 140bpm.wav"). Switch between DRUMS and PITCHED. Remember the drum separation is being improved and results vary with the loop. Dense or reverberant loops can confuse it. Fix hits by hand in the grid.

**9. Very first start is slow.** GHOST KIT builds its drum library on first start and caches it. The next starts are quick.

---

## 9. FAQ

**Does it come with sample packs?** No. All built-in drum sounds are made by GHOST KIT itself. Use your own samples any time.

**Is GHOST KIT the same as Ghosty Beats?** Yes. It was renamed. Hosts may still list the old plugin name.

**How many tracks and patterns?** 16 tracks, 16 patterns (A to P), up to 128 steps per pattern, and each track can have its own length for polymeter.

**Can tracks have different lengths?** Yes: the track length box under the track panel. CONVERGENCE sets when they realign.

**Will the generators overwrite my work?** Not locked tracks or locked steps. Every generator action is one undo step.

**Are random results repeatable?** Yes. Generators, EVOLVE and scripts are seeded. The same beat gives the same result.

**Can I use my own drums with RE-CREATE?** Yes. Turn on "also use the loop's own hits as the sounds".

**Does it work in the Standalone app?** Yes. PLAY and the BPM parameter (60 to 200) run it with no host.

---

## 10. GLOSSARY

- **BAKE TO SONG:** turns the evolving performance into real patterns plus a song.
- **BREED:** 16 variations of a pattern as tiles; star favourites and breed again.
- **CHOP:** slices a long sound at its hits.
- **CONVERGENCE:** when polymeter tracks realign.
- **DNA:** the pattern's rhythm fingerprint (hits per bar position, syncopation, velocity).
- **EVOLVE:** bar-by-bar variation while the beat plays.
- **FOCUS:** a big view with only tracks, grid and lanes.
- **KEEP x% IDENTITY:** keep part of a pattern, write new hits elsewhere.
- **LINEAR DRUMMING:** one voice at a time; collisions resolved by a rule.
- **LOCK:** protects a track or a step from generators.
- **MOTIF:** a locked figure that every bar keeps.
- **PHRASE arc:** the 16-bar intensity curve EVOLVE follows.
- **PLAY: ALWAYS / ON NOTES:** whether the beat runs freely or only while a MIDI note is held.
- **RE-CREATE AS MIDI:** audio loop to pattern or melody.
- **SAVE BEAT:** a `.ghostbeat` file with everything.
- **SYNC: DAW / OWN:** whose clock runs the beat.
- **TRACKER:** the steps-as-rows view.
