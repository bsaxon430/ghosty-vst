# Ghost Voice: CHOP (build 8)

Ghost Voice is an **instrument** now: it loads in FL Studio's **channel rack**, not the mixer. It opens on **CHOP**.

1. **Drag a WAV in** (or ADD WAV, or REC to record from the input). It goes on the keys at once: **WHOLE** mode, C3 plays it
   as it is, other keys pitch it (the length and the voice's character are kept).
2. **Edit it like FL Studio**: drag with SELECT, then Cmd+C / Cmd+X / Cmd+V / Delete / Cmd+B (duplicate), TRIM keeps the
   selection, Cmd+Z / Cmd+Shift+Z undo / redo.
3. **Loop and play like Block Step**: SPACE plays / stops; drag along the ruler to draw a loop, LOOP turns it on (Cmd+L loops the
   selection).
4. **AUTO CHOP** (Cmd+U) chops it and puts the chops on the keys from **C1** (CHOPS mode). Modes: RANDOM (random beats, 1 or 2
   beats long, snapped to the hits; press again for a new set), HITS, EVERY BEAT, EVERY 2 BEATS, EVERY BAR, EVEN. `-` / `+` set
   how many. The tools SLICE (C) and DELETE (D) cut or remove chops by hand.
5. **CONVERT VOICE** sings the selection (or the whole clip) with your Voice C (set up in IDENTITY LAB) and puts it back in the
   same place. Cmd+Z puts the original back.
6. **DRAG WAV** drags the selection (or the clip) out into FL Studio's playlist or a folder.

Tools (Block Step's letters = FL Studio's): **E** SELECT, **C** SLICE, **D** DELETE, **Z** ZOOM, **Y** PLAYBACK.
PgUp / PgDn or Cmd+scroll zoom, scroll moves along, the strip under the wave shows where you are.

## Look (build 10)
Everything is drawn by hand: every button, box, slider, knob, switch, panel and ghost is inked with a slightly wobbly pen line, shaded with pen hatching and painted in the colours of Brandon's painting (`Source/UI/HandDraw.h`, `SplashArt.h`). Text is never cut off: the tests check every tab and button at three window sizes.

## Under the hood
- The sound engine is **INTERSECT** (GPL-3.0), an untouched copy in `third_party/intersect`; Ghost Voice only sends it commands
  (load this clip, make these slices, these notes) through its own API, the way INTERSECT's own editor does.
- Tempo, key, hits and the HITS / BEATS / BAR / EVEN chop points are **Flayr Chop's** analysis (AGPL-3.0), untouched, in
  `third_party/flayr-chop`.
- RANDOM, the clip edits, the keys modes and CONVERT VOICE are ours (`Source/Chop`).
- Build 9: **PHRASES** (OpenVPI's audio-slicer, MIT) cuts a vocal at its silences, and **WORDS** (WhisperX, BSD-2) puts every
  word on its own key, named. Each runs as a separate Python helper (`helper/chop_tools.py`); PHRASES needs a small private
  Python (INSTALL PHRASE FINDER, ~60 MB, or the voice engine's), WORDS its own install (INSTALL WORD FINDER, ~1.5 GB).
- Build 9: the **SOUND** panel edits the chop you click (or the key you press): PITCH, FINE, VOLUME, ATTACK, RELEASE, REPITCH /
  STRETCH / SMOOTH, NO LOOP / LOOP / PING-PONG, the filter (CUTOFF, RESO), REVERSE, ONE-SHOT, KEEP VOICE. These are INTERSECT's
  own per-slice settings, sent through its API.
- Not yet: stem separation (INTERSECT's needs ONNX Runtime, which has no Intel Mac build), the robot voice.
