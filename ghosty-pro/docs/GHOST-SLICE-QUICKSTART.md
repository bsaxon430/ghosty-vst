# Ghost Slice - quickstart

Ghost Slice is a nondestructive audio editor: your file is never changed. Every cut, stretch, pitch edit and slice is saved
as a project (JSON + a reference to the audio) and rendered from the original.

## 1. Load
Drop a WAV / AIFF / FLAC / MP3 on the waveform (or LOAD). The audio is analysed in the background: onsets, tempo, the pitch
track, notes, syllables and the key. Pick the MATERIAL (AUTO / DRUMS / VOCAL / MELODIC) in FLEX if the guess is wrong.
A file at another sample rate than your session is converted with a clean (anti-aliased) converter; the original is never touched.
Slow loops (70-90 BPM) are no longer read at double speed; if a reading is still off, use TAP BPM.
A clip that comes with a `.ghostclip.json` next to it (`take.wav.ghostclip.json` or `take.ghostclip.json`: bpm, bars, offset,
key) loads with that tempo, downbeat and key.

## 2. EDIT - cut and arrange (key 1)
- Right-click = the cursor; right-drag = a loop. Left-drag = a selection. S = cut. R = reverse. Cmd/Ctrl+D = duplicate.
- Drag a region's top bar to move it (snaps to the grid), its edges to trim (Alt: stretch), the corner squares to fade.
- PITCH / CENTS / FORMANT (row 2): with a region selected they shift that region only (accent captions; its header shows
  "+3 st"); click empty space to set the global ones for the whole project.
- CHOP (row 3): choose where slices come from - TRANSIENT (attacks), SYLLABLE (one slice per syllable: its consonant stays with its
  vowel), NOTE (pitched notes),
  BEAT (the tempo grid), EQUAL parts. SENS sets how many cuts. The dashed green lines show them; SPLIT @ SLICES turns them
  into regions, and the ARRANGE strip under the waveform lets you drag the blocks into a new order.
- REPEAT and HOLD work on a selection (stutters, held vowels). The waveform under a REPEAT shows what it plays. HOLD finds the
  vowel of the syllable you selected and holds only that part: the consonants keep their speed. ELASTIC holds now meet the
  original without a pitch wobble at either end.

## 3. FLEX - timing (key 2)
Double-click to add a warp marker, drag it to move audio in time (MOVE / STRETCH / SLIP). A = anchor a marker. AUTO ALIGN
proposes moving the detected hits to the grid (amber ghosts); APPLY keeps them. TAP BPM / USE BPM set the tempo.

## 4. PITCH - notes (key 3)
- The notes are violet blobs on a piano roll. Click to select (Cmd/Ctrl-click to add, drag on empty space for a band).
- Drag a blob up / down to change its pitch (semitone snap; Shift = free cents). Up / Down keys do the same.
- Cmd/Ctrl-drag a blob = move it in time; drag its edge = stretch its start / end. Double-click = split it.
- Note row: FINE, FORMANT, DRIFT (flatten / exaggerate the slow wander), VIBRATO (0-200 %), TRANS (transition time), GLIDE,
  SPLIT, MERGE, RESET.
- DRAW (or D): draw the pitch freehand over the notes (violet line); each stroke is one undo step.
- SCALE + SNAP / SNAP ALL: put notes on a scale. Row 2 has the global PITCH / CENTS / FORMANT / TONALITY.
- KEY: set the PROJECT key, choose a KEY MODE and press MATCH KEY (or click the KEY readout in the header):
  CHROMATIC (shift the whole source into the key), SCALE SNAP (snap each note to the key's scale), MELODY PRESERVE (move the
  melody tonic to tonic, intervals exact), MANUAL (edit the notes yourself), USE CHORDS (shift by the detected chords' roots
  onto the key's chords: best for pads, keys and guitar).
- CHORDS (row 4, also in EDIT): a read-only chord lane at the top of the view (C, Am, G7, Bdim, Dsus; "?" = root only,
  N.C. = no chord), detected in the background and shown as heard after PITCH; the detected KEY also uses it (+CHORDS).

## 5. PLAY - slices on keys (key 4)
Pick the CHOP mode, then TURN INTO INSTRUMENT: slice 1 is C1 and up. Click pads to audition, set per-key pitch / envelope /
reverse / repeat, play them from MIDI. Drag a pad out for its WAV (full version). HOLD VOWEL (a key setting, makes the key GATE):
hold the key and the slice's vowel sustains as long as you hold it (up to 8 s), then RELEASE fades it.

## 6. Quality and export
LIVE stretches in real time; STUDIO renders high-quality versions in the background (the info strip shows the progress)
and switches to them at safe points. An offline bounce in your DAW waits for the STUDIO renders. EXPORT WAV renders the
arrangement (full version); EXPORT MIDI writes the slice positions.

Press ? any time for every shortcut and gesture. Undo / redo: Cmd/Ctrl+Z / Shift+Cmd/Ctrl+Z.
