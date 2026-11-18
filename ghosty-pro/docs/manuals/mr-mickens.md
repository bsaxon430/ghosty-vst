# MR. MICKENS

**A jungle and drum-and-bass break machine.** Four break bays, a drum machine, remix and groove-lock tools, fills, stutter, filters, and a break search. Version 3.1.0. Formats: VST3, AU, CLAP, Standalone.

It ships with a library of 100 breaks that it plays and processes itself (20 break rhythms, 5 variations each, 32 slices per break, one per 16th). You can add your own break WAVs. Everything runs in time with your song.

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

## 1. QUICK START: make your first break in 2 minutes

1. Load Mr. Mickens as an instrument (see "FL Studio tips").
2. Open the **PRESETS** box at the top and pick a factory preset, for example from the **JUNGLE** lens. It loads breaks, drums and settings.
3. Press **PLAY** (in SYNC: DAW mode, press play in your DAW). You hear the breaks and the drum machine.
4. In **BAY 1**, press the **>** arrow next to the break name to try the next break. You can also pick one from the list.
5. Turn **REMIX** up in the bay. The break stays recognisable but gets accents, swaps and ratchets. Press **DICE** in the bay for a different remix.
6. Press and hold **STUTTER (F2)** (in the stutter section) for a roll. Press **FILL NOW (E2)** for a fill into the next bar.
7. Click the **16th** cells in the **DRUM MACHINE** to add kicks, snares and hats.
8. Drag **DRAG WAV OUT** into the DAW to keep the mix. Drag a bay's **MIDI** grip to keep its sequence as MIDI.
9. Press **SAVE** to keep the whole setup as a `.mickens` file.

> Tip: in the demo, step 8 is locked (the UNLOCK button blinks). See "Demo vs licensed".

---

## 2. THE SCREEN

### 2.1 Header

| Control | What it does |
|---|---|
| **PRESETS box** | 40 factory presets grouped by lens (JUNGLE, HALFTIME, IDM, BASS, PROG, FILTER) and your own saved presets under "YOUR PRESETS". |
| **< / >** (next to PRESETS) | Step through the factory presets. |
| **SAVE PRESET** | Save the current setup as a preset of your own. It shows up at the bottom of the list. |
| **SYNC: DAW / OWN** | DAW (default): Mr. Mickens follows the DAW's play button, tempo and song position. You can edit while the song plays, and SPACE stays with the DAW. OWN: it runs by itself with its own tempo, its own PLAY, and SPACE plays and stops only Mr. Mickens while its window has focus. The DAW's play button does not start it. PLAY: ON NOTES still gates it, BYPASS still wins. Saved with the project, never in presets. |
| **Tempo area** | DAW mode: shows the project tempo ("from DAW"), read only. OWN mode: a BPM box (click, type 20 to 999, Enter) and **TAP** (tap two times or more). |
| **PLAY** | OWN: starts the own clock from bar 1. In DAW mode with the DAW stopped, it starts a preview. |
| **PLAY: ALWAYS / ON NOTES** | ALWAYS: the beat runs with the transport. ON NOTES: runs only while a MIDI note is held, so muting the channel in your DAW stops it and the next note starts it again on the song's 16th grid. Draw one long note per bar or pattern. C0 (or any note below C1) only opens the gate. HOLD TO PLAY keys also keep the gate open while held. With no note held, everything is silent. BYPASS always wins. Saved with the project, never in presets or snapshots. |
| **MUTE** | Silences Mr. Mickens while your song plays. It keeps following the beat, so unmuting drops back in on time. |
| **BYPASS** | Passes the plugin through. |
| **DICE** | A new start point for every bay (on each bay's grid). Press again for a new combo. |
| **SAVE / LOAD** | Save the whole setup as a `.mickens` file, or load one. |
| **BREAKS FOLDER** | Opens `Documents/Ghosty/Mr Mickens/Breaks`. Put your own break WAVs there. Sub-folders become sections. They show up at the top of every bay's list. |
| **RESCAN** | Load the Breaks folder again after you add files. |
| **Look button** | Switch the look. It names the next one. See "The looks". |
| **UNLOCK / LICENSED** | License entry. |
| **Meter** | Output peak. The red light means the safety clipper (above 0.98) is working: turn VOLUME down. Click to reset. |
| **SEARCH BREAKS** | Opens the break search (see 2.9). |

### 2.2 The four break bays

Each of the four bays plays one break. Bay 1 plays by default. Bays 2, 3 and 4 start muted.

**Choosing a break.** A list (grouped by family), **<** and **>** arrows, and **FIND** (opens the search for this bay). Built-in breaks show their tempo. You can also drag an audio file onto the waveform ("pick a break, or drop one here"). A file with no found tempo loads with FIT TEMPO off.

**The waveform** shows the break as 32 slices. The slice that is playing is lit. A line marks every 16th, and a stronger line every beat.

| Control | Range / values | Default | What it does |
|---|---|---|---|
| **NUDGE** | -16 to 16 steps | 0 | Moves the start point by that many grid steps. |
| **grid box** | 1/16, 1/8, 1/4, 1/2, 1 bar | 1/16 | The size of one NUDGE step. |
| **PITCH** | -12 to 12 semitones | 0 | Pitch of the break. |
| **LEVEL** | 0 to 1 | 0.7 (bay 1), 0.5 (others) | Bay volume. |
| **CHOP LEN** | 0.05 to 1 | 1 | How long each slice plays. |
| **PAN** | -1 to 1 | 0 | Stereo position. |
| **RANDOM START** | on / off | off | Every bar the break starts from a new random spot (on the grid). |
| **RANDOM CHOPS** | on / off | off | Swaps 16ths for random slices. |
| **CHOP AMT** | 0 to 1 | 0.3 | How often RANDOM CHOPS swaps. |
| **FIT TEMPO** | on / off | on | On: the break re-pitches to the song tempo (the classic jungle sound). Off: it keeps its own speed. |
| **MUTE** | on / off | off for bay 1 | Mutes the bay (2 ms fade, no click). |
| **HOLD TO PLAY (C2, C#2, D2, D#2)** | button | | The bay plays its break from its start point, in time, until you let go. The MIDI note shown does the same. |
| **KEYS** | button | | Your MIDI keys from C3 up play this bay's slices, one slice per key. |
| **MIDI** (grip) | | | Drag 4 bars of this bay's sequence out as a MIDI clip. Note = C3 + slice (the KEYS mapping), ratchets as repeated notes, velocity = level. |

### 2.3 Sidechain (per bay)

Each bay has a small block labelled **SIDECHAIN (ONLY THIS BAY DUCKS)**.

| Control | Range | Default | What it does |
|---|---|---|---|
| **DUCK KICK** | 0 to 1 | 0.35 (bay 1), 0 (others) | How much the drum machine's kick ducks this bay. |
| **DUCK SNARE** | 0 to 1 | 0 | Same, from the snare. |
| **DUCK HATS** | 0 to 1 | 0 | Same, from the hats. |

The speed it recovers at is **DUCK REL** in the MIX section (30 to 600 ms, default 160 ms).

### 2.4 BREAK DNA, REMIX and GROOVE LOCK

Mr. Mickens reads each break's groove and calls it its **DNA**.

**DNA lane.** For every 16th it shows what hits there: kick, snare, ghost, hat, cymbal (colours as in the legend). Bar height = accent. Position = micro-timing. Ticks along the top show a frozen groove.

**SEQ strip (sequence).** Shows what the bay plays on each 16th *right now*, including REMIX, locks and interlock. Click a step to **lock** it as it is. Right-click to unlock. Arrow marks show a reversed slice. Vertical lines show ratchets. A small arrow shows a pitch change. A white outline marks a locked step.

| Control | What it does |
|---|---|
| **PRESERVE / REWIRE / MUTATE** (DNA mode) | PRESERVE keeps the break's order and only decorates it. REWIRE swaps hits for other hits of the same kind (the skeleton stays). MUTATE rewrites the skeleton itself. |
| **REMIX** (0 to 100) | 0 = the break as it is. Turning up stacks layers: accents, same-kind swaps (from 20), ratchets (40), reverses + pitch + swing (60), phrase edits (80). Turn LOCK down for bigger changes. |
| **LOCK** (0 to 100, default 70) | GROOVE LOCK: how strictly the ticked parts of the groove survive the remix (100 = kept exactly, 0 = anything goes). |
| **K/S, POS, SWING, ACCENT, GHOSTS** (ticks, all on by default) | What the groove lock protects: where kicks and snares land, hit positions (no new hits, no silences), the swing (swapped hits keep the groove's micro-timing), the accents, the ghost notes' dynamics and placement. |
| **FREEZE DNA** | Keeps this break's groove while you load other breaks into the bay. The new break plays the frozen groove with its own sounds. |
| **DICE** | A new remix seed. The remix pattern changes, the settings stay. |
| **UNDO** | Takes back the last DNA move (mode, remix, lock, dice, freeze). |
| **PRINT** | Locks all 32 steps exactly as they play now. Then edit single steps by clicking them. |
| **CLEAR** | Frees every step lock of this bay. |
| **ROLE** | The interlock role (see 2.6): FREE, MAIN, GHOST, TEXTURE, FILLS. |

### 2.5 The DRUM MACHINE

Four drum lanes of 16 steps: **KICK, SNARE, HAT, OPEN HAT**. The drums run next to the breaks and duck them.

- **Click** a step: on / off. **Right-click** or **Shift-click**: a ghost (soft) hit. Drag to paint.
- Each lane has a sound box. Choose a sound, or **drop your own sample** on the lane's steps.
- **M**: mute that drum (it also stops ducking the breaks).
- **HIT**: play that drum now (MIDI notes C1, D1, F#1, A#1).
- A level knob per drum (defaults: kick 1, snare 0.9, hat 0.6, open hat 0.55).
- **MUTE ALL**: mutes every drum (patterns stay).
- **TWO STEP / HALFTIME / ROLLER / CLEAR**: ready-made patterns.

### 2.6 GENERATE FILL and INTERLOCK

**GENERATE FILL.**

| Control | Values | What it does |
|---|---|---|
| **style** | CLASSIC, JUNGLE, ROLLING, GLITCH, CHAOS | CLASSIC: a snare run. JUNGLE: snare and ghost triplets with a kick drop. ROLLING: building ratchets. GLITCH: reversed, pitched, cut-up slices. CHAOS: anything goes. The downbeat after a fill always lands on a kick. |
| **every** | OFF, 4 BARS, 8 BARS, 16 BARS | Automatic fills at the end of every N bars. |
| **length** | 1 BEAT, 2 BEATS, 1 BAR | How long a fill is. It always ends on the bar line. |
| **INTENSITY** | 0 to 1 (default 0.6) | How wild the fill gets. |
| **FILL NOW (E2)** | button | Fill from the next 16th into the next bar line. MIDI note E2 does the same. Pressed in the last 16th of a bar it fills into the bar after. Needs the beat running. |

**INTERLOCK.** Lets the bays answer each other. Set one bay to **MAIN** with its ROLE box, and the others to GHOST, TEXTURE or FILLS.

| Control | What it does |
|---|---|
| **mode** | OFF, COMPLEMENT (they rest where the main bay hits), CALL & RESPONSE (they answer every other bar), UNISON (they double its hits), CROSS-RHYTHM (a 3-against-4 layer), COLLISION (they hit with it). |
| **BAY DUCK** (0 to 1, default 0.6) | The other bays dip around the main bay's hits (it opens 2 ms before each hit). |
| **ROLE** (in each bay) | MAIN leads. GHOST answers with ghost notes. TEXTURE lays long quiet hats. FILLS only plays in fills. FREE ignores interlock. |

### 2.7 SNAPSHOT DECK

Eight slots, numbered 1 to 8.

- **Click** a slot: recall it.
- **Shift-click**, **right-click**, or click while **STORE** is armed: store everything there (knobs, breaks, drum pattern, frozen DNA, step locks).
- **Alt-click**: clear the slot.
- **MORPH**: the **A > B** knob glides every knob from slot A to slot B. Breaks, patterns and modes switch on the bar line halfway. While MORPH is on, you hear the slots, not live edits. To edit a slot, set things up and STORE it, or switch MORPH off.
- Pick the two slots with the **A** and **B** boxes. Automate **A > B** for whole-setup transitions.

### 2.8 FX sections

| Section | Controls |
|---|---|
| **CRUNCH** | **CRUNCH** (default 0.25), **BITS** (default 0). |
| **SPACE** | **CHORUS** (default 0), **REVERB** (default 0.12), **SIZE** (default 0.6). |
| **MIX / OUT** | **BREAKS** (0.85), **DRUMS** (0.7), **DUCK REL** (30 to 600 ms, 160 ms), **VOLUME** (0.5), output meter. |
| **QUALITY** | **LO-FI / CLEAN / HQ** (default CLEAN). Sample reads: LO-FI = the classic 2-point reads (grit on pitched hats), CLEAN = 4-point, HQ = 8-point sinc. Drag-outs always use HQ unless LO-FI is chosen. |

#### Filters

Six filters: **LP** (low-pass), **HP** (high-pass), **BP** (band-pass), **NOTCH**, **COMB**, **FORMANT**. Each has a **CUTOFF** (COMB has a **PITCH**, FORMANT a **VOWEL**) and a **MIX**. All MIX start at 0. A shared **DRY** knob (default 1) and **RESO** (default 0.5).

| Control | What it does |
|---|---|
| **routing** | **PARALLEL**: every filter next to the dry break. **SERIAL**: LP > HP > BP > NOTCH > COMB > FORMANT, each MIX = how much of that stage. **MORPH**: crossfades through the filters that are on. Switching crossfades over 10 ms. In MORPH routing the filter MIX knobs read ON and switch a filter in or out of the chain. |
| **MORPH** knob | In MORPH routing: where in the chain of switched-on filters you are. Automate it. |
| **AUTO GAIN** (on by default) | Keeps the filtered part as loud as the input (peak follower, +/-6 dB). In PARALLEL only the wet sum is corrected; DRY is never touched. |
| **COMB: FEEDBACK** (-0.95 to 0.95, default 0.75) | Negative = hollow, odd harmonics. |
| **COMB tuning** | FREE (the PITCH knob), KEY (tuned to the NOTE), TEMPO (a power-of-two multiple of the beat, so it rings in time). |
| **NOTE** | The comb note for KEY tuning (C0 to C6, default A1). |

#### FILTER MOTION

| Control | What it does |
|---|---|
| **HIT SWEEP** source: OFF, KICK, SNARE, ANY HIT | Every kick, snare or any hit (from the main bay's DNA and the drum machine) fires a resonant sweep from the hit's own sample. |
| **SWEEP** (-4 to 4 octaves, default 2) | Sweep amount. Plus = up, minus = down. |
| **DECAY** (0.01 to 1 s, default 0.12 s) | How fast the sweep falls back. |
| **RESO** (default 0.3) | Extra resonance while the sweep is open. |
| **PHRASE SWEEP** length: OFF, 1, 2, 4, 8 BARS | A tempo-synced sweep over the phrase that snaps back at the phrase start. |
| **shape**: RISE, FALL, ARCH | The sweep shape. |
| **AMOUNT** (-5 to 5 octaves, default 3) | Phrase sweep amount. |

### 2.9 STUTTER

A tempo-synced, sample-exact stutter on the breaks.

| Control | Values | What it does |
|---|---|---|
| **STUTTER (F2)** (hold button) | | Hold to stutter. It engages on the next 16th. MIDI note F2 does the same, sample-exact. |
| **LATCH** | on / off | Off: hold to stutter, release to stop. On: tap on, tap off. |
| **size** | 1/8, 1/16 (default), 1/32, 1/64, 1/16T | Repeat size. |
| **mode** | ROLL, RAMP, GLITCH, BREAKDOWN | ROLL: plain repeats. RAMP: repeats get shorter (a build). GLITCH: every repeat picks its own length, spot and direction. BREAKDOWN: repeats get longer, slower, darker and quieter until it fades out by itself. |
| **GATE** | 0.05 to 1 (default 1) | How much of each repeat sounds. |
| **DECAY** | 0 to 1 (default 0) | Each repeat quieter than the last. |
| **PITCH** | -12 to 12 (default 0) | Semitones per repeat. |
| **MOVE** | 0 to 1 (default 0) | Repeats swing across the stereo field and the filter wanders. |
| **FILTER** (0 to 1, default 0.75), **ENV AMT** (0.5), **ENV DECAY** (0.35) | The stutter's filter and its envelope. |

### 2.10 The BAR ruler (sections and loops)

A 16-bar ruler under the main area.

- **Right-click** a bar: play from that bar (own clock, or the preview when the DAW is stopped).
- **Right-drag** (at least 6 pixels): pick bars as a **section**, shown as a brace.
- **Double-click**: clear the section.
- **LOOP** (or key **L**): play only the braced bars, over and over (own PLAY, the preview and the DAW's playback). Click again to play everything.
- The brace has **MIDI** and **WAV** grips: drag just these bars out as MIDI (a track per bay plus the drums) or as WAV (rendered from its own bar, with a 2 s tail).

### 2.11 SEARCH BREAK and BREAK MATCH

Press **SEARCH BREAKS** (top) or **FIND** in a bay.

Three modes: **SEARCH BY FEEL**, **MATCH: SIMILAR**, **MATCH: COMPLEMENT**.

- **Words** box: type words such as jungle, sparse, swung, bright or a family name.
- **Eight feel sliders**, each can be left on **ANY**: **TEMPO** (80 to 180 BPM), **GHOSTS**, **SNARE ACCENT**, **SWING**, **KICK SPARSE**, **HAT BUSY**, **BRIGHT**, **SYNCOPATION**. Click to set, drag to scrub, Shift = fine, wheel = one step, double-click = back to ANY, right-click = type a value. **ALL ANY** resets all of them.
- **BAY 1 to BAY 4** buttons choose the target bay.
- **MATCH: SIMILAR**: breaks closest to the target bay's break (feel, spectrum and kick/snare groove).
- **MATCH: COMPLEMENT**: breaks that sit well next to the target bay's break (different density and spectrum, few colliding hits, matching swing).
- The list shows results. Built-in families show as one row with their 5 variations.
- **AUDITION**: plays the selected break once, in time at the song tempo (the mix ducks while it plays).
- **LOAD INTO BAY**: puts the selected break in the target bay. **CLOSE** closes the panel.

### 2.12 Drag-out row

| Control | What it does |
|---|---|
| **bars box** | 1, 2, 4 or 8 BARS (default 4): the length of the mix drag-out. |
| **DRAG WAV OUT** | Drags a WAV of the mix (renders in the background, then the drag starts). |
| **DRAG STEMS OUT** | Drags the stems: breaks, kick, snare, hats. |

---

## 3. WORKFLOWS

### 3.1 Classic jungle roller

1. Pick a **JUNGLE** preset or load a roller break in bay 1. Keep **FIT TEMPO** on.
2. Set the DAW tempo near 170 to 174.
3. In the drum machine press **TWO STEP** or **ROLLER**.
4. Bay 1 **REMIX** about 30 to 40, **LOCK** 70.
5. Set **FILL every 4 BARS**, style **JUNGLE**.

### 3.2 Layer a second break (ghost notes)

1. Load a second break in bay 2 and unmute it (**MUTE** off).
2. Set bay 1 **ROLE: MAIN**, bay 2 **ROLE: GHOST**.
3. In INTERLOCK choose **COMPLEMENT** and raise **BAY DUCK**.
4. Pan the bays a little apart and lower bay 2 **LEVEL**.

### 3.3 Chop a break by hand

1. Pick a break and press **PRINT** to lock all 32 steps as they play.
2. Click single steps in the **SEQ** strip to unlock the ones you want to change.
3. Use **RANDOM CHOPS** and **CHOP AMT** for random swaps.
4. Turn on **KEYS** and play slices from C3 upward. Record the MIDI in your DAW.

### 3.4 Halftime and IDM textures

1. Pick a preset from the **HALFTIME** or **IDM** lens.
2. Turn on **FREEZE DNA** and load other breaks into the bay: they play the frozen groove with their own sounds.
3. Try **REWIRE** or **MUTATE** with **LOCK** lowered.

### 3.5 Filter build and drop

1. Set filter **routing: PARALLEL** and raise **LP MIX**.
2. Set a **PHRASE SWEEP** of 4 or 8 BARS with shape **RISE**.
3. Add the **STUTTER** with mode **RAMP** (hold) for the last bar.
4. Press **FILL NOW (E2)** to land on the kick.

### 3.6 Whole-setup transitions

1. Build a verse sound, store it in slot **1** (**STORE**, click 1).
2. Build a drop, store it in slot **2**.
3. Turn **MORPH** on, A = slot 1, B = slot 2, and automate **A > B** in the DAW.

### 3.7 Drag a loop into the DAW

1. Drag across the **BAR** ruler (right-drag) to brace 4 bars.
2. Drag the **MIDI** or **WAV** grip into the playlist.
3. Or use **DRAG WAV OUT** / **DRAG STEMS OUT** for the whole mix.

### 3.8 Use your own breaks

1. Press **BREAKS FOLDER** and copy WAV files into it (sub-folders become sections).
2. Press **RESCAN**.
3. Pick them at the top of any bay's list, or drop a file on a bay's waveform.

---

## 4. FL STUDIO TIPS

**Loading.** After install, rescan plugins (Plugin database > "Find installed plugins"). Add Mr. Mickens as an instrument (generator).

**Instrument or effect.** Instrument. It makes its own sound and listens to MIDI. It has no MIDI output.

**MIDI map.**

| Notes | What they do |
|---|---|
| C1, D1, F#1, A#1 | Kick, snare, closed hat, open hat |
| C2, C#2, D2, D#2 | HOLD TO PLAY for bay 1 to 4 |
| E2 | GENERATE FILL |
| F2 | Hold to STUTTER |
| C3 and up (64 keys) | Slices of the bay set by KEYS |

**Tempo sync.** In **SYNC: DAW** it follows FL's play, tempo and song position. **SYNC: OWN** gives it its own clock. The tempo knob of older versions is replaced by the transport area, and the drag-outs follow your project tempo.

**Automation.** Right-click a knob in FL and link a controller. Everything on the panel is a parameter, including REMIX, LOCK, the filters, the morph and per-bay settings. Automate **A > B** for whole-setup morphs.

**Dragging MIDI and audio out.** Use the grips and pills. Dragging needs a licensed copy.

**Saving.** The host saves the whole plugin state with the project (in the demo a reopened project starts at the default sound). **SAVE** writes a `.mickens` file with parameters and your own dropped samples. It works in the demo.

**Where files live.**

| What | Mac | Windows |
|---|---|---|
| Setups (`.mickens`) | `~/Documents/Ghosty/Mr Mickens` | `Documents\Ghosty\Mr Mickens` |
| Your presets | `.../Mr Mickens/Presets` | `...\Mr Mickens\Presets` |
| Your break WAVs | `.../Mr Mickens/Breaks` | `...\Mr Mickens\Breaks` |
| License file | `~/Library/Application Support/GhostBlock` | the Windows app-data folder, under `GhostBlock` |
| VST3 | `/Library/Audio/Plug-Ins/VST3` | `C:\Program Files\Common Files\VST3` |
| AU / CLAP (Mac) | `/Library/Audio/Plug-Ins/Components` and `/CLAP` | not applicable |

Renders for drag-outs go to the temporary folder, not your Breaks folder.

---

## 5. THE LOOKS

Mr. Mickens has four looks: **BLACK**, **PURPLE**, **LIGHT** and **GHOST** (the 4th look, liquid glass).

Use the look button in the header (tooltip: "Switch the look: BLACK -> PURPLE -> LIGHT -> GHOST (liquid glass); the button names the next one"). Each press moves to the next look, and after GHOST it returns to BLACK. The default look is PURPLE. The look is saved with the session. The window scales: resize it and the whole panel scales with it.

---

## 6. DEMO VS LICENSED

In demo mode (no license key):

- **The sound drops out for 2 seconds every 20 seconds.**
- **A reopened project starts at the default sound.**
- **Nothing leaves as MIDI or audio.** Blocked: **DRAG WAV OUT**, **DRAG STEMS OUT**, each bay's **MIDI** grip, and the BAR ruler's **MIDI** and **WAV** grips.

When you try, the **UNLOCK** button blinks and a hint appears. Nothing is written.

Still works in the demo: all playing and editing, the break library and search, presets, SAVE / LOAD of `.mickens` setups (they hold only parameters and your own dropped samples), and your own breaks.

**To unlock:** click **UNLOCK** (it becomes **LICENSED**). Paste the license key from your Gumroad receipt email and press **Unlock**. **Buy** opens the store page. Hover the button to read the demo rules.

---

## 7. SHORTCUTS

| Keys / mouse | Action |
|---|---|
| SPACE | In SYNC: OWN, play / stop Mr. Mickens (while its window has focus). In DAW mode SPACE stays with the DAW. |
| L | Toggle LOOP SECTION (when a section is braced). |
| Click a SEQ step | Lock it. Right-click unlocks. |
| Click / right-click / Shift-click a drum step | On-off / ghost. |
| Right-click a bar in the ruler | Play from that bar. |
| Right-drag in the ruler | Pick a section. |
| Double-click the ruler | Clear the section. |
| Slot click / Shift-click / Alt-click | Recall / store / clear a snapshot. |
| Drag on a knob | Change it. Double-click = default. Right-click = type a value. |
| Shift + drag, wheel | Fine / one step (search sliders). |
| Drop a WAV on a waveform | Load it as a break in that bay. |
| Drop a sample on a drum lane | Use it as that drum. |
| MIDI C1 D1 F#1 A#1 | Drums. |
| MIDI C2 to D#2 | HOLD TO PLAY, bays 1 to 4. |
| MIDI E2 / F2 | Fill / stutter. |
| MIDI C3 and up | Slices (KEYS). |

---

## 8. TROUBLESHOOTING

**1. No sound.** Check that **MUTE** (header) is off, **BYPASS** is off, and bay 1 is not muted. Bays 2 to 4 start muted. In SYNC: DAW press play in the DAW. In OWN press PLAY. If **PLAY: ON NOTES** is on, hold a MIDI note.

**2. No response to MIDI.** Mr. Mickens listens on the notes listed under "MIDI map". In **PLAY: ON NOTES**, with no note held, everything is silent, including the on-screen pads. Set it back to ALWAYS.

**3. The sound cuts out for 2 seconds.** That is the demo drop-out (every 20 seconds). Enter your license key.

**4. Drag-out does nothing and UNLOCK blinks.** Export needs a licensed copy.

**5. A break plays at the wrong speed.** Check **FIT TEMPO**. A file with no found tempo loads with FIT TEMPO off ("no tempo found - FIT TEMPO off" shows on the waveform). Use a file name or length that gives a clear tempo, or leave FIT TEMPO off and set the project tempo by hand.

**6. Mac says the plugin can't be opened (Gatekeeper).** Until the installer is notarized: right-click the installer and choose **Open**, then confirm. Or System Settings > Privacy & Security > "Open Anyway".

**7. The plugin is not showing in FL Studio.** Rescan plugins (Plugin database > "Find installed plugins"). The VST3 folder is `/Library/Audio/Plug-Ins/VST3` on Mac and `C:\Program Files\Common Files\VST3` on Windows.

**8. The output is distorted.** The red meter light means the safety clipper is working. Turn **VOLUME**, **BREAKS** or **CRUNCH** down.

**9. My own breaks do not show up.** Put WAV files in the Breaks folder and press **RESCAN**.

---

## 9. FAQ

**How many breaks come with it?** 100: 20 break rhythms with 5 variations each. They are re-played by the plugin, not sampled from records.

**Can I load my own breaks?** Yes: the BREAKS FOLDER, or drop a file on a bay. Breaks up to 20 seconds are read.

**Does REMIX change the audio?** It changes which slices play and how (accents, swaps, ratchets, reverses, pitch, swing). The sound of the break itself is the same.

**What is the difference between PRESERVE, REWIRE and MUTATE?** PRESERVE keeps the order and decorates. REWIRE swaps hits for hits of the same kind. MUTATE rewrites the skeleton.

**How do I keep a remix I like?** Press **PRINT** to lock all steps as they play, or STORE a snapshot, or SAVE the setup.

**Does it send MIDI?** No. It only receives it. The MIDI grips write clips you can drag into the DAW.

**Can it run without the DAW's transport?** Yes: SYNC: OWN.

**What happens to my settings when I load a preset?** A preset resets to defaults and then applies its own settings, bays and drum pattern.

---

## 10. GLOSSARY

- **Bay:** one of four break players.
- **BAR ruler:** the 16-bar strip for sections and loops.
- **Brace:** the marked section on the ruler.
- **DNA:** a break's groove, read as hits on each 16th.
- **FIT TEMPO:** re-pitch the break to the song tempo.
- **FREEZE DNA:** keep a groove while changing the break.
- **GROOVE LOCK:** how strictly the groove survives a remix.
- **INTERLOCK:** the bays answer each other (roles).
- **Lens:** a group of factory presets (JUNGLE, HALFTIME, IDM, BASS, PROG, FILTER).
- **MORPH:** glide the whole setup between two snapshot slots.
- **PRINT:** lock all 32 steps as they play.
- **REMIX:** how much the break is re-arranged.
- **SEQ strip:** shows what a bay plays now.
- **Snapshot deck:** eight stored setups.
- **Stutter:** repeats of a small slice in time.
