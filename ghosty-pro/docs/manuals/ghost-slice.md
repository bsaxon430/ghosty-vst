# GHOST SLICE - the audio slicer, stretcher and pitch editor

Load a loop, a vocal or a sample. Cut it, line it up to the beat, change its notes, and turn the slices into keys you can play. Your original audio is never changed.

- Version: 0.1.0 (first release, shipped in the 3.1.0 GhostBlock installer). Formats: VST3, AU, CLAP, Standalone.
- It loads as an **instrument** (it makes sound and takes MIDI).

> **Please read first: this is a FIRST RELEASE.** Ghost Slice is being rebuilt to be simpler. The pitch and key features (the PITCH tab, KEY MODE, MATCH KEY, ENGINE) are still being improved, so results on some sounds can be rough. This manual describes the current version, not the future one. The basics (load, cut, line up, play slices on keys) are the solid part. If something here changes in a later update, the in-plugin help (press **?**) is always the latest.

---

## Quick start

> Chop a loop and play it on your keyboard in 2 minutes

1. Add **Ghost Slice** as an instrument in your DAW (see FL STUDIO TIPS).
2. Drop an audio file (WAV, AIFF, FLAC or MP3) onto the big waveform, or press **LOAD**. Ghost Slice listens to it in the background for beats, tempo, notes and key. Wait a moment until the **BPM** box fills in.
3. Press **PLAY** (or the space bar) to hear it. Right-click the waveform to move the play cursor.
4. In the second tool row, open the **CHOP** box and pick how to cut: **TRANSIENT** for drums, **BEAT** for a loop, **SYLLABLE** for vocals. Dashed green lines show the cuts. Use **SENS** (or **PARTS**, or the beat division) to get more or fewer.
5. Click the **PLAY** tab (or press **4**).
6. Press **TURN INTO INSTRUMENT**. Slice 1 lands on key C1, slice 2 on C#1 and so on.
7. Click the pads to hear each slice, or play the keys from C1 upward on your MIDI keyboard.
8. Want a pad as audio? **Drag it out** of the pad grid into your DAW (full version only).
9. Save your work with **PROJECT > Save project as...** (your DAW project also keeps it).

---

## The screen

The window opens at 1280 x 780. You can drag the corner to enlarge it (minimum 1160 x 700).

### The header

From left to right:

| Control | What it does |
|---|---|
| Ghost + **GHOST SLICE** logo | Just the logo. |
| **EDIT / FLEX / PITCH / PLAY** | The four workspaces. Keys **1 2 3 4** switch. |
| **BPM** | Project tempo. Double-click to type it. Under it: DETECTED, or SET (and the detected value). |
| **KEY** | The source key: detected from its notes (with a confidence %), or set by hand. Click it to pick a target key, a KEY MODE, or MATCH PROJECT KEY. |
| **GRID** | The snap grid you set with SNAP. |
| **PLAY / STOP** | Play the arrangement from the cursor. |
| **LOOP** | Loop the selection (or right-drag span). |
| **LOAD** | Load audio. |
| **PROJECT** | Open, Save, Save as, Relink audio. Projects use the `.ghostslice` file. |
| **RELINK** | Shows only if the project's audio file was moved: pick the original file. |
| **SYNC: DAW / SYNC: OWN** | DAW: follow the DAW's play button, tempo and position. OWN: Ghost Slice runs by itself with its own tempo and play button. |
| Look button | Cycles BLACK / PURPLE / LIGHT / GHOST. |
| **UNLOCK / LICENSED** | Enter your licence key (demo only). |

At the bottom is the **info strip**: file name, length, a short fingerprint; on the right the piece count, LIVE or STUDIO state (with a progress bar while rendering), and latency in samples. A **?** button there opens the full shortcut list.

### The tool rows

Row 1 is the same in every tab:

| Control | What it does |
|---|---|
| **CUT** | Cut at the cursor, or at both edges of the selection (S). |
| **SPLIT @ MARKERS** | Split the selected region at every warp marker. |
| **DELETE** | Delete the selected range, region or (in FLEX) marker. |
| **DUPLICATE** | Copy the region right after itself (Cmd/Ctrl+D). |
| **REVERSE** | Reverse the region (R). |
| **UNDO / REDO** | Cmd/Ctrl+Z and Shift+Cmd/Ctrl+Z. |
| **SNAP** box | SNAP OFF, 1/4, 1/8, 1/16, 1/32, 1/64. |
| **T** | Triplet grid. |
| **A/B** | Hear the original (B: ORIG) against your edit (A). |
| **LIVE / STUDIO** | LIVE stretches in real time. STUDIO renders higher quality versions in the background and swaps them in at safe points. |
| **EXPORT WAV / EXPORT MIDI** | Full version only. See DEMO VS LICENSED. |

### EDIT tab (key 1) - cut and arrange

Left-drag selects a range. Right-click sets the cursor. Right-drag makes a loop. Drag a region's top bar to move it (it snaps to the grid). Drag a region edge to trim (hold Alt to stretch). The small squares at the top corners are the fades. Cmd/Ctrl-drag slides the audio inside a region.

Row 2:

| Control | Range / what it does |
|---|---|
| **GAIN** | -48 to +12 dB, for the selected region. |
| **FADE IN / FADE OUT** | 0-2000 ms, for the selected region. |
| **PITCH** | -24 to +24 semitones. With a region selected it shifts only that region (the caption turns accent colour). With nothing selected it is global. Formants are kept. |
| **CENTS** | Fine pitch. |
| **FORMANT** | Moves the voice character without changing the pitch. 0 keeps the voice as it is. |
| **REPEAT** | Retrigger the selected range. See below. |
| **HOLD** | Hold the stable part of the selected range (a vowel, a note). See below. |

Row 3 is **CHOP** (where slices come from):

| Mode | Slices come from |
|---|---|
| REGIONS | The regions you made in EDIT. |
| EQUAL | N equal parts (**PARTS** 2-64, default 16). Cuts snap to a zero crossing. |
| MANUAL | Your own cuts: click the strip to add one, drag to move, double-click to remove (set up in PLAY). |
| TRANSIENT | Attacks (drums, plucks). |
| SYLLABLE | One slice per syllable. A consonant stays with its vowel. |
| NOTE | Pitched notes. |
| BEAT | The tempo grid. Pick 1/4, 1/8, 1/16 or 1/32. |

**SENS** (0-100%, default 50%) is how many cuts for TRANSIENT, SYLLABLE and NOTE. A label shows how many slices you have. **SPLIT @ SLICES** turns the dashed cuts into real regions. Then drag the blocks in the **lane strip** under the waveform to rearrange them. **CHORDS** (when there is room) shows a read-only chord lane over the waveform.

#### REPEAT (stutter)

Select a range and press **REPEAT**. A strip appears. Drag the handle at the end of the repeat to set its length.

| Control | What it does |
|---|---|
| Rate box | 1/4 down to 1/128 (follows tempo). |
| STRAIGHT / TRIPLET / DOTTED | Rate feel. |
| REV OFF / ALT / ALL | Reverse every second repeat, or all. |
| **RETRIG** | On: each repeat restarts at the start (stutter). Off: the audio runs on and the repeats gate it. |
| **STEP / RAMP** | Pitch per repeat: STEP (each repeat shifts by PITCH) or RAMP (0 up to PITCH over the span). |
| **PITCH** | -24 to +24 semitones. |
| **FADE IN / FADE OUT** | 0-50 ms (defaults 1 and 4 ms). |
| **SWING** | 50-75%. |
| **RAMP** | 0.25x-4x speed ramp. |
| **DECAY** | 0-100%, how repeats fade. |
| **GATE** | 10-100%, how long each repeat lasts. |
| **RATCHET** + curve | Draw the rate over the span: drag the five points (up faster, down slower). Double-click the curve to flatten. |
| **REMOVE / X** | Remove the repeat, or close the strip. |

#### HOLD (sustain)

Select a range inside one region (for example a vowel) and press **HOLD**. Ghost Slice finds the stable part for you. Drag the right edge to set how long it is held. Consonants keep their speed.

| Control | What it does |
|---|---|
| Mode box | **ELASTIC** (stretch it), **SUSTAIN** (a grain loop that follows the pitch: vowels, notes), **TEXTURE** (a spectral hold: pads), **FREEZE** (an endless smear). |
| **AUTO** | Find the stable region again. |
| **DRIFT** (SUSTAIN) | 0-25 cents of slow random pitch drift. Default 5. |
| **VARIATION** (SUSTAIN) | 0.25x-2x: how far the grain walk roams. Default 1x. |
| **SMEAR** (TEXTURE) | 5-100%: low = a held tone, high = a wash. Default 60%. |
| **REMOVE / X** | Remove the hold, or close the strip. |

### FLEX tab (key 2) - timing

Double-click the waveform to add a **warp marker** (it snaps to a beat hit nearby). Drag a marker to move the audio in time.

| Control | What it does |
|---|---|
| **MOVE** | Drag a marker: the neighbours stretch, the total length stays. |
| **STRETCH** | Drag a marker: the part before it stretches and everything after moves with it. |
| **SLIP** | Drag inside the region: the audio slides under fixed markers. |
| **ADD MARKER / REMOVE** | Add a marker at the cursor (M) or remove the selected one. |
| **ANCHOR** | The selected marker never moves (A). |
| **SNAP %** | How hard markers snap to the grid (Cmd/Ctrl = full, Alt = none). |
| **TAP BPM** | Sets the project tempo from the selected marker and the next one. |
| **T-LOCK** | Transient lock: attacks play at normal speed inside stretched parts. |

Row 3 is **auto align** (the "ghost align" row):

| Control | What it does |
|---|---|
| **MATERIAL** box | AUTO, DRUMS, VOCAL or MELODIC: how the audio is analysed. Change it if the guess is wrong. |
| **AUTO ALIGN** | Proposes moving the detected hits onto the grid. They show as amber ghosts. Nothing changes yet. |
| **TIGHT / GROOVE / MANUAL** | TIGHT: onto the grid. GROOVE: the grid plus the groove of the strong hits (keeps a shared swing). MANUAL: no automatic moves. |
| **STRENGTH** | 0-100%, default 100%. How far hits move toward their targets (50% is half way). |
| **CONF** | 0-100%, default 45%. Weaker hits (and breaths) stay where they are. |
| **FEEL** | 0-100%, default 100%. How much groove the targets keep (GROOVE mode only). |
| **APPLY** | Keeps the alignment as AUTO markers (one undo step). Your own markers never move. |
| **CLEAR AUTO** | Removes the AUTO markers. |
| **USE BPM** | The detected tempo and downbeat become the project tempo and grid. |

> Tip: if the tempo reading is wrong, use TAP BPM. Slow loops (70-90 BPM) are no longer read at double speed.

### PITCH tab (key 3) - notes

> This tab is the part still being improved. Try it on clean, solo sounds first (a single voice or a simple melody). Busy mixes can give rough results.

The notes appear as violet blobs on a piano roll.

- Click a blob to select it. Cmd/Ctrl-click adds to the selection. Drag on empty space to select several.
- Drag a blob up or down to change its pitch (semitone steps; hold Shift for free cents). **Up / Down** arrows do the same (Shift = 10 cents).
- Cmd/Ctrl-drag a blob moves it in time. Drag its edge to stretch its start or end. Double-click splits it.
- **DELETE** resets the selected notes.

Row 2 (global): **PITCH**, **CENTS**, **FORMANT**, **TONALITY** (1000-20000 Hz, default 8000: above this frequency the pitch shift stops keeping tonal detail; lower it for noisy or breathy sounds), and an **ENGINE** box:

| ENGINE | Meaning |
|---|---|
| AUTO | VOCAL on voiced notes within +-4 semitones, WIDE beyond. |
| VOCAL | Pitch-synchronous engine: the voice's formants stay. Up to +-12 semitones. |
| WIDE | The stretcher with formant correction. Any shift. |

Row 3 (selected notes): **FINE** (-50..+50 cents), **FORMANT** (-12..+12 st), **DRIFT** (0-200%, how much of the natural pitch wander stays), **VIBRATO** (0-200%), **TRANS** (transition time in ms into the note), **GLIDE** (straight-line transition), **SPLIT**, **MERGE**, **RESET**.

Row 4: the key tools.

| Control | What it does |
|---|---|
| Source key box | DETECTED (with the key shown) or a key you pick. |
| Project key box | The song key to move toward. |
| **KEY MODE** box | CHROMATIC (shift the whole source into the key), SCALE SNAP (snap each note to the key's scale), MELODY PRESERVE (move the melody tonic to tonic, intervals exact), MANUAL (you edit the notes), USE CHORDS (shift by the detected chord roots: best for pads, keys and guitar). |
| **MATCH KEY** | Runs the chosen KEY MODE. |
| SCALE box | The scale SNAP uses. |
| **SNAP / SNAP ALL** | Put the selected notes (or all notes) on the scale. Notes under 50% confidence stay. |
| **DRAW** (key D) | Draw the pitch freehand over the notes. One undo step per stroke. |
| **F0** | Show the detected pitch curve over the blobs. |
| **CHORDS** | Show the chord lane (read-only). "?" means only the root was found. N.C. means no chord. |

### PLAY tab (key 4) - slices on keys

Here the slices become an instrument you play.

| Control | What it does |
|---|---|
| **CHOP** box, **SENS / PARTS / beat box**, **SPLIT @ SLICES** | Same as in EDIT. |
| **TURN INTO INSTRUMENT** | Puts every slice on a key from C1 upward (up to 92 slices). Settings of keys that stay are kept. |
| **EXPORT MIDI MAP** | Full version only. Writes a MIDI file with each slice's key at its place in the arrangement. |
| **<  >** | Page through the pads, 16 at a time. |
| Pads | Click to hear a slice. Hold for GATE keys. Drag a pad out into your DAW to get its WAV (full version only). |
| Keyboard strip | Shows which keys play which slice, and what is sounding. |

Settings for the selected key:

| Control | Range / what it does |
|---|---|
| **PITCH** | -24 to +24 semitones, tape style (higher is shorter). |
| **START** | 0-2000 ms into the slice. |
| **ATTACK / DECAY / RELEASE** | 0-2000 ms. |
| **SUSTAIN** | Level held while the key is down. |
| **GAIN / PAN** | Level and stereo position. |
| **CHOKE** | A key cuts the others in its group (open and closed hats). 0 = none. |
| **ONE-SHOT / GATE** | ONE-SHOT: plays to its end. GATE: stops (RELEASE) when you let go. |
| **REVERSE** | Play the slice backwards. |
| **HOLD VOWEL** | Hold the key and the vowel sustains as long as you hold (up to 8 s). Makes the key GATE. |
| **REPEAT** box | OFF, 1/8, 1/16, 1/32, 1/16T, 1/64: retrigger the slice while it sounds. |

Global:

| Control | Range / what it does |
|---|---|
| **POLY** | 1-32 voices (default 16). Stolen voices fade in 3 ms, no clicks. |
| **BEND** | 2-24 semitones (default 2). |
| **VEL>GAIN** | How much velocity sets loudness (default 100%). |
| **VEL>FILTER** | How much a soft note closes a low-pass filter (default 0). |

---

## Workflows

### Chop a drum loop and play it

1. Drop the loop. Wait for BPM.
2. EDIT tab, CHOP: TRANSIENT. Raise SENS until every hit has a cut.
3. PLAY tab, TURN INTO INSTRUMENT. Play C1 upward.

### Fix the timing of a loop (auto align)

1. FLEX tab. Set MATERIAL to DRUMS (or leave AUTO).
2. Check the BPM. Use USE BPM or TAP BPM if wrong.
3. Press AUTO ALIGN. Amber ghosts show where hits will go.
4. Lower STRENGTH to keep some feel, or switch to GROOVE.
5. Press APPLY. Press A/B to compare with the original. UNDO if you do not like it.

### Rearrange a vocal phrase

1. EDIT tab. CHOP: SYLLABLE.
2. Press SPLIT @ SLICES.
3. Drag the blocks in the lane strip under the waveform into a new order.

### Hold a vowel

1. Select the vowel with left-drag, in one region.
2. Press HOLD. Drag the right edge to make it longer. Choose SUSTAIN mode.

### Stutter

1. Select a short range and press REPEAT.
2. Pick 1/16, turn RETRIG on, drag the handle to the length you want.
3. Draw a RATCHET curve for speed-ups.

### Move a sample to your song key

1. PITCH tab. Check the detected KEY (the box shows a % confidence).
2. Pick the project key in the second key box.
3. KEY MODE: CHROMATIC for the simplest result. Press MATCH KEY.
4. Listen. If it sounds rough, undo and use PITCH on the EDIT tab instead.

### Send slices to the DAW

1. PLAY tab, TURN INTO INSTRUMENT.
2. Full version: EXPORT MIDI MAP, then drag the MIDI file into your piano roll. Use the same sound on the instrument to play it back.
3. Or drag pads out one by one as WAV files.

---

## FL Studio tips

- **Loading:** Scan plugins (Options > Manage plugins > Find more plugins), then add **Ghost Slice** from the Channel Rack **+** button. It is an instrument.
- **MIDI:** In FL, play from the piano roll into the channel. Keys from C1 upward play slices (after TURN INTO INSTRUMENT).
- **Tempo and play:** With **SYNC: DAW** the plugin follows the DAW's play button, tempo and song position. With **SYNC: OWN** it ignores them. In the standalone app it uses its own clock.
- **Space bar:** With SYNC: DAW inside a DAW, the space bar belongs to the DAW. Use the PLAY button in the plugin.
- **Automation:** Ghost Slice's edits are inside the project, not DAW parameters.
- **Saving:** Your DAW project stores the whole Ghost Slice project (cuts, markers, notes, slices). For a separate file use PROJECT > Save project as... (`.ghostslice`). It holds your edits and a reference to the audio file, **not the audio itself**. Keep the original audio where it is, or use RELINK.
- **Dragging audio out:** Drag a pad from the PLAY tab (full version).
- **Latency:** The plugin reports a small fixed latency (shown in the info strip in samples) that comes from its output peak limiter. The DAW compensates.
- **Offline bounce:** When you render in the DAW, Ghost Slice waits for the STUDIO renders it needs.
- **Sample rate:** A file at another sample rate than your project is converted cleanly. The original file is not changed.
- **Where files live:**
  - Analysis cache (safe to delete): Windows `%APPDATA%\GhostBlock\Ghost Slice\cache`; Mac `~/Library/Application Support/GhostBlock/Ghost Slice/cache`.
  - Default export folder: `Music\Ghost Slice` (Windows) or `~/Music/Ghost Slice` (Mac).
  - Pad drags use a temporary folder named "Ghost Slice drag".
  - Licence file: `GhostBlock` folder in the same Application Support / AppData place.
  - A clip with `<name>.ghostclip.json` next to it (tempo, bars, offset, key) loads with that tempo and key.

---

## The looks

Click the look button in the header (it shows the name of the next look). It cycles **BLACK** -> **PURPLE** -> **LIGHT** -> **GHOST**. Tooltip: "The look: BLACK / PURPLE / LIGHT / GHOST (liquid glass). Click for the next one." GHOST is the liquid-glass look with floating glass panels. Your choice is saved with the project.

---

## Demo vs licensed

Without a licence key the plugin is in demo mode:

- The sound drops out for 2 seconds every 20 seconds.
- A reopened DAW project starts empty (the project data is kept and comes back when you unlock).
- Nothing can leave the plugin as audio or MIDI: **EXPORT WAV**, **EXPORT MIDI**, **EXPORT MIDI MAP** and dragging a pad out are off. When you try, the UNLOCK button blinks and a short hint shows.

Everything else works: loading, editing, playing, saving `.ghostslice` projects.

To unlock: click **UNLOCK** in the header, paste the key from your receipt email into "License key" and press **Unlock**. The button changes to **LICENSED**. Press **Buy** to open the store page.

---

## Shortcuts

| Key / gesture | Does |
|---|---|
| Space | Play / stop (not when SYNC: DAW inside a DAW) |
| 1 2 3 4 | EDIT / FLEX / PITCH / PLAY |
| Cmd/Ctrl+Z, Shift+Cmd/Ctrl+Z, Cmd/Ctrl+Y | Undo, redo |
| S | Cut |
| R | Reverse region |
| Cmd/Ctrl+D | Duplicate region |
| Delete | Delete (in PITCH: reset notes) |
| Left / Right | Nudge the region (Shift = fine) |
| L | Loop selection |
| Z | Zoom to selection |
| + / - | Zoom in / out |
| Home | Cursor to start |
| M, A (FLEX) | Add marker, anchor marker |
| D, Up/Down (PITCH) | Pitch draw, move notes +-1 semitone |
| ? | Shortcut list (Esc closes) |
| Right-click / right-drag | Cursor / loop |
| Wheel; Cmd/Ctrl+wheel | Scroll; zoom |

---

## Troubleshooting

1. **No sound.** Press PLAY in the plugin, or play the DAW if SYNC: DAW is on. Make sure audio is loaded (the info strip says "no audio loaded" if not). Keys only play slices after TURN INTO INSTRUMENT.
2. **Keys do nothing.** The channel must send MIDI to Ghost Slice. Keys start at C1. Check the PLAY tab has slices.
3. **Silence for 2 seconds now and then.** Demo mode. Unlock to remove it.
4. **Export buttons do nothing and UNLOCK blinks.** Demo mode.
5. **Project reopens empty.** Demo mode; also check that the audio file was not moved (use RELINK).
6. **Wrong tempo.** Use TAP BPM or type the tempo by double-clicking the BPM box.
7. **Pitch or key change sounds rough.** The pitch tools are still being improved. Try ENGINE: VOCAL or WIDE, a smaller shift, or use a cleaner source.
8. **Mac "can't be opened" warning.** The installer is not notarized yet: right-click the app or installer and choose Open, then confirm.
9. **Plugin not in FL Studio.** Rescan plugins and restart FL.

---

## FAQ

**Does it change my audio file?** No. Everything is stored as instructions and rendered from the original.

**Which files can I load?** WAV, AIFF, FLAC and MP3.

**What is GHOST MAP?** The name of the editing model. Your cuts, markers, pitch edits and slice keys all stay attached to each region when you move it.

**LIVE or STUDIO?** LIVE is instant while you edit. STUDIO renders better quality in the background and replaces LIVE when ready.

**Is this the final version?** No. It is a first release being rebuilt to be simpler.

**Can I use the slices in another sampler?** Yes with the full version: drag pads out as WAV, or export the MIDI map.

---

## Glossary

- **Region:** a piece of the arrangement you can move, trim and fade.
- **Slice:** one cut piece that can sit on a key.
- **Warp marker:** a point you pin to the grid in FLEX.
- **AUTO markers:** markers made by APPLY after AUTO ALIGN.
- **Anchor:** a marker that never moves.
- **Formant:** the voice's character; moving it changes the sound without changing the note.
- **GHOST MAP:** the non-destructive project model.
- **GHOST REPEAT / GHOST HOLD / GHOST RATCHET:** the stutter, sustain and speed-curve tools.
- **LIVE / STUDIO:** real-time stretching versus background high-quality renders.
