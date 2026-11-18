# Phantom FX — phase reports (plain words)

## Phase 0 — Audit (2026-11-08)
- AUDIT.md: all 24 effects' controls (ids, ranges, defaults, choices) pulled from the Ghost Box code and checked against the manual.
- 27 foundations cloned, LICENSE read in each clone, trees listed (third_party/audit), licence texts kept (third_party/licenses).
- Findings: Mango is odoare/Mango (AGPL-3); CloudSeed moved to CloudSeedCore (MIT); SpectralSuite is public domain; ChowMatrix is BSD-3;
  FILT-R and JUCE 8 are AGPL-3; chowdsp's DSP modules are GPL-3 (not BSD); vitOTTx exists after all (Sakhnovkrg/vitOTTx, GPL-3, corrected the same day);
  sst-effects has a Chorus but no ensemble. Everything is AGPL-3-compatible. Ghost Box's 25th tile GHOST SPLIT is not among the brief's 24 (flagged).

## Phase 1 — Skeleton (2026-11-08), build 2, Mac run 42473553113
- Built: JUCE 8.0.4 project (VST3 + CLAP + Standalone; AU behind a switch), the PhantomEffect interface, the shared DSP layer
  (second-order parameter smoothing, oversampling policy Off/2x/4x/8x/Render, host-locked LFO / envelope follower / step sequencer,
  delay lines, Linkwitz-Riley crossover), the effect slot (latency-matched dry path, 10 ms click-free on/off, per-effect trim, mix),
  the host shell (stable parameter ids fx_<unit>_<param>, bit-exact state, latency reporting, 1200 x 760 window, About / Credits item),
  the test harness, the Mac workflow with pluginval 10 and the install step.
- Tests on Brandon's Mac (15 of 15 green): null −150.5 dBFS; latency honesty exact at 0 / 1 / 64 / 1000 samples; gain staging 0.00 dB;
  stereo L−R −240 dBFS; bypass click none (−3.10 in / −3.10 out); zipper −131.5 dBFS above 6 kHz; aliasing (linear) −148.5 dBFS;
  10 min of silence 3.7 µs/block vs 3.5 µs with signal, all finite; state round trip bit-exact and byte-identical; 9 sample-rate / buffer
  combinations clean; fresh start (two instances) clean; window 1200 x 760; CPU 0.13 % of one core; LFO phase error 0.000 samples over 10 min
  with a sweeping tempo. pluginval 1.0.4 strictness 10: SUCCESS.
- Installed: ~/Library/Audio/Plug-Ins/VST3/Phantom FX.vst3 (0.1.2), ~/Library/Audio/Plug-Ins/CLAP/Phantom FX.clap, /Applications/Phantom FX.app.
  INSTALLED.txt: "2026-11-08 04:17  Phantom FX 0.1.2  12c1961  INSTALLED". Ghost Box untouched.
- Not done yet: the 24 effects and the Ghost Box UI port (Phase 2). AU is not built (no Xcode app on the Mac).

## Phase 2 — Rack (2026-11-08), build 3, Mac run 42536416336
- Built: all 24 effects in the rack with their real names, groups, colours and every control (807 host parameters, stable ids
  fx_<unit>_<control>; pass-through engines until their phase), the rack engine (serial main chain + 3 parallel lanes with gain / pan /
  mute, latency-aligned summing, the limiter locked last, order changes through a 20 ms dry crossfade), 8 scenes per preset
  (store / recall / rename, morph time CUT … 4 BARS, quantise OFF … 2 BARS, MIDI keys C1–G1, the Scene host parameter), A/B for the whole
  rack and per unit, snapshots, human-readable JSON presets (whole rack and per unit, backwards-compatible loader), undo / redo, the
  modulator pool (4 LFOs, 2 step sequencers with drawable steps, 2 envelope followers, 8 macros) routed to any knob by drag or right-click,
  the global oversampling policy, input / output trims with peak + true-peak meters, and the Ghost Box UI ported as it is (header, tab row,
  unit strip with drag-reorder / lights / lane badges, unit page, macro strip, GHOST SCENES bar; BLACK / PURPLE / LIGHT / GHOST looks).
- Tests on Brandon's Mac (237 of 237 green): the full check table for the pass-through and each of the 24 units; lanes sum phase-aligned
  (residual −101 dBFS); order change while playing: no click (peak unchanged), never below −6.7 dBFS; scenes recall exactly; preset JSON
  round trip 0 differences with unknown keys ignored; state round trip byte-identical; fresh start clean; window 1200 × 760; CPU 1.83 %
  of one core with all 24 slots running; pluginval 1.0.4 strictness 10 SUCCESS. Screenshots: branch phantomfx-build (01–05 png).
- Installed: Phantom FX 0.1.3 (VST3 + CLAP + app). INSTALLED.txt: "2026-11-08 16:58  Phantom FX 0.1.3  65b9f8a  INSTALLED".
- Decisions to know about (DECISIONS.md 9–12): the topology crossfade dips to dry rather than running two racks; scene morph writes
  host parameters from a 60 Hz timer; SPILL is not in this phase; extra controls the brief calls for were declared now.
- Not done yet: the engines (Phases 3–6), drawable pattern / lane editors for GATE, GRID, HAUNT, WARP, FORMANT, SPECTRA mask (with Phase 5 / 4),
  the SPIRIT EQ analyser display (Phase 3), the factory preset bank (Phase 7).

## Phase 3 — Colour (2026-11-09), build 4 (0.1.4), Mac run 42577510816 — DONE and installed
- Built: all ten Colour effects on real open-source engines: CRACKLE / WRECKER (Surge saturators + chowdsp anti-aliased clippers, 8x),
  TUBE GLOW (Airwindows Tube2 + Console7Channel), SOUL CRUSH (chowdsp compressor + grit), RETRO (ChowTapeModel hysteresis, wow, flutter,
  degrade + noise beds, digital, space), FATTEN (Airwindows Density / FathomFive / DrumSlam / Tube2), BASS LAB (exact 3-band split, mono
  lows, Airwindows BassKit sub), GHOST FORMANT (level-matched vowel bank), SPIRIT EQ (24 bands, dynamics, mid / side, cuts to 96 dB/oct),
  PHANTOM FILTER (31 Surge filter models). New faster oversampler, exact latency at every factor, up to 16x in Render.
- Tests on Brandon's Mac: 243 of 243 green. Null -111 dBFS; latency exact; default level change 0.0-0.5 dB; aliasing -84 to -102 dBFS at full
  drive; zipper below -90 dBFS; SPIRIT EQ bell 6.00 dB for +6, 24 dB/oct cut -24.1 dB, dynamic band -6.00 dB; pluginval 10 SUCCESS.
  CPU with all 24 units on: 21.55 % of one core (limit 25; it was 25.3 % before the speed work). Switched-off units now cost nothing.
- Installed: "2026-11-09 00:14  Phantom FX 0.1.4  2b9689b  INSTALLED".
- Not done yet: SPIRIT EQ's linear-phase modes, Spectral switch and analyser display. RETRO at 100 % tape bias aliases at -74 dBFS.
  Decisions: DECISIONS.md 13-20.

## Planned: Phase 6b — SPATIAL (added 2026-11-09, Brandon's request)
- A SPATIAL page: each switched-on effect is a tile on a 3D sphere around the listener's head (top and side views), dragged to place its wet
  sound left/right, front/back, above/below, near/far (binaural on headphones, a speaker mode too); drag movements recorded in sync with the
  song and played back ("memory"), plus normal host automation. Full spec: SPATIAL.md. Built after Phase 6, before release (DECISIONS.md 26).

## Phase 4 — Space (2026-11-09), build 5 (0.1.5) — DONE and installed (Mac run 42626602043, merge f689e86)
- Built: all eight Space effects on real open-source engines. CHORUS (Surge's chorus voices stacked; ENSEMBLE on Chow's bucket-brigade
  model), CRYSTAL (the eurorack granular engine as Surge hosts it, plus our reversed splices), ECTO ECHO (a delay tree of up to 8 nodes on
  Chow's tape / analog / BBD / clean delay lines, tempo-locked), SPECTRAL SPACE (CloudSeed), SHIMMER (CloudSeed + an octave shifter in the
  loop, freeze / infinite hold), HAUNTED HALLS (Dragonfly's hall / room / plate engines, 11 modes), GHOST SPECTRA (our own spectral engine:
  freeze, smear, scatter, shift, pitch, phase, gate, motion, GHOST keeps the hits), DOORWAY (convolution through 5 generated spaces that
  morph: weighted, crossfade or spectral).
- Tests on Brandon's Mac: 247 of 248 green. Every Space unit: null at MIX 0 -240 dBFS; level change on pink noise 0.0-0.9 dB; reverb tails
  fall smoothly to -120 dBFS (worst rise 0.5 dB, worst ringing band 3.8 dB, limits 1 / 6); GHOST SPECTRA rebuilds the input to -132 dBFS,
  pre-echo -159 dB, latency 2048 samples exactly; ECTO ECHO stays within 0.27 samples of the beat over 10 minutes of tempo changes.
- First run: the one failure was CPU (30.73 % with all 24 on, limit then 25 %); options in DECISIONS.md 27.
- Brandon's answer (2026-11-09): A, raise the limit to 40 % (DECISIONS.md 28). Final: all 24 on = 32.45 % of one core; 244 of 244
  checks green; pluginval strictness 10 SUCCESS. Installed: "2026-11-09 06:40  Phantom FX 0.1.5  f689e86  INSTALLED".
- The earlier Phase 3 merge run (42578246194) failed only because pluginval crashed (segfault) inside its own "Parameter thread safety"
  test; the same code had passed that test in run 42577510816, and it passed in all three Phase 4 runs on the Mac and locally on Linux,
  so it is a rare crash that did not come back. Watch for it in later runs.
- (Asked before:) "With every one of the 24 effects switched on at the same time, the new reverbs bring Phantom FX to about 31 % of
  one core on your Mac, and the Rhythm and Master effects still to come will add more. Should I raise the limit for the whole rack (to
  about 40 % with everything on, the sound stays as it is), or keep 25 % and make the reverbs lighter (less dense, a bit thinner tails)?"
- Not done yet: DOORWAY's IR import (needs a file picker in the UI; the 5 generated spaces are in), the drawable editors (SPECTRA mask),
  per-unit pages' new graphics for the Space units (they use the existing unit page), and Phases 5, 6 and 6b.

## Phase 5 — Rhythm (2026-11-09), build 6 (0.1.6) — phase-branch run 42633358852 green on Brandon's Mac
- Built: the four Rhythm effects on their real open-source foundations.
  - GHOST GATE: 16 drawn gate patterns on GATE-12's curve engine (9 segment shapes, tension). Also a 16-step time lane (repeat, stutter,
    half, reverse, scratch, stop) on TIME-12's buffer, swing, HOST SYNC or AUDIO trigger (GATE-12's transient detector, 5 ms lookahead),
    and the pump (4 on floor, half-time, fast, drawn, input-keyed).
  - GHOST GRID: a 12-row painted grid (16/32 steps, 8 patterns, seed). The rows are Mango's own effects (gate, loop and stutter grain
    repeat, reverse, freeze, crush, filter, delay) plus FxmeTools' reverb and pitch shifter; scratch and tape stop use TIME-12's buffer.
  - HAUNT EDIT: 16 pads (10 gesture types), played by mouse, by automation, or by MIDI notes 48-63 at the exact sample.
  - WARP: a drawn bend lane. PITCH uses Signalsmith Stretch (keeps time; KEEP FORMANTS option). TAPE uses varispeed on TIME-12's buffer.
- Drawn editors on each unit page: the gate curve plus time lane plus drawn pump, the grid painter with each row's control at its end,
  the pad grid with a type / size / motion editor, and the bend lane with snap, grid and factory shapes. All drawn data is saved with
  the project.
- Tests on Brandon's Mac: 253 of 253 green, pluginval strictness 10 SUCCESS.
  - Tempo lock, 10 minutes at 70-174 BPM with tempo changes and 84 loop jumps:
    - GATE: all 610 drawn edges land at the same offset from the exact host sample (spread 0).
    - GRID: all 1911 painted blocks start on their exact sample.
    - HAUNT: all 240 MIDI presses start on the note's own sample, with slices on the host grid (0 samples off).
  - Audio trigger: the first sample of every hit passes, latency 240 samples (reported).
  - WARP: +7 semitones measured at 662.4 Hz (PITCH) and 659.3 Hz (TAPE); 659.26 Hz wanted.
  - An empty grid nulls (-240 dBFS). Drawn data survives save / load byte-exact.
  - CPU with all 24 on: 35.41 % of one core (limit 40).
- Side finding (pluginval state restoration): could not be reproduced. pluginval's own procedure, run on the plugin, restores all 812
  parameters and reads the state back identical, and pluginval 10 passes on the Mac. Guard added: a restore now cancels any pending
  scene request / quantised trigger / scene morph. Also fixed: three lane parameters showed in hosts as "Lane 65/66/67 ..." instead of
  "Lane A/B/C ...".
- Decisions: DECISIONS.md 29-34 (Mango's sequencer engine itself is not used, only its block model and effects; GRID gained GATE and
  FREEZE rows; HAUNT's old Pad Type / Pad Size automation ids are kept but unused; GATE's new trigger controls).
- Not done yet:
  - Rubber Band (WARP's challenger, Phase 6).
  - Turning WARP on is silent for about 0.1 s before it fades in (no click; Ghost Box pre-rolled its stretcher, this one waits for it).
  - Drawn data is not in scenes / unit A/B / unit presets.
  - Undo for drawn edits.

## Phase 6 — Master (2026-11-09), build 7 (0.1.7) — phase-branch run 42652353748 green on Brandon's Mac
- Built: the two Master effects on real open-source engines, plus the brief's challenger procedure for the limiter and WARP.
  - GHOST SQUEEZE: the 3-band OTT compressor of vitOTTx (Vital's own multiband compressor code), driven the way vitOTTx drives it. Its
    knobs at 0 dB / 50 % give vitOTTx's own sound; DEPTH 0 % = dry.
  - GHOST LIMITER: ZL Limiter (true-peak lookahead limiter with its own oversampling, 1x-32x, default 2x). The LSP limiter (the brief's
    baseline) is built and tested too and stays in the repo behind one build switch.
- Challenger results (Brandon's Mac):
  - Limiter, +12 dB pushed into a -0.3 dB ceiling, 4x inter-sample peaks: ZL at most 0.04-0.07 dB over at every oversampling (the rule is
    +0.1); LSP 0.22 dB over at 4-32x and 1-2.4 dB over at 1-2x. CPU per 256-sample block: ZL ~120 us, LSP ~41 us. Latency at 2x: ZL 746
    samples, LSP 542. ZL passes, so ZL ships (the brief: "ships whichever passes the limiter tests").
  - WARP: Signalsmith 50 ms latency, ~81 us per block, +7 st measured 662.4 Hz; Rubber Band 64 ms, ~528 us per block (about 6.5x the CPU),
    659.26 Hz exact. Signalsmith stays.
  - Blind listening (page with 4 unlabelled, level-matched pairs; Brandon picked 15:52-15:54 UTC): limiter 1-1 split (ZL in one pair, LSP
    in the other), so ZL stays; WARP: Signalsmith both times. DECISIONS.md 42. The same clips and the answer key go to
    Music/GhostBlock Builds/Phantom FX blind clips on install.
- WARP no longer goes silent for ~0.1 s when switched on: the dry plays while the stretcher fills, then WARP fades in.
- Tests on Brandon's Mac: 278 of 278 green, pluginval strictness 10 SUCCESS. All 24 on: 37.35 % of one core (limit 40).
- Decisions: DECISIONS.md 35-42.
- Not done yet: Phase 6b SPATIAL (own agent); a gain-reduction meter on the limiter page (the value is computed, not drawn yet); LSP on the
  Mac runs without its AVX2 / AVX-512 code (Apple's compiler cannot build those files; it is not the shipped engine); the drawn-data gaps
  from Phase 5 (scenes / unit A/B / unit presets / undo).

## Phase 6b — SPATIAL (2026-11-09), build 8 (0.1.8)
- What it is: a new SPATIAL tab. A sphere around your head, seen from the top (front / back / left / right) and from the side
  (above / below), with distance rings at 1, 5, 10 and 20 m. Every unit you switch on in the rack shows up as its own ghost tile
  (the same tile as the strip). Drag a tile to put that effect's sound somewhere around you: reverb below, delay above, distortion on
  the right, crystal on the left. Further from the head = further away (quieter, a little darker). The readout shows the selected
  tile's angle, elevation and distance. Tiles on the head (the row under the views) are not placed: they sound exactly as in the rack.
  Double-click a tile or press CENTRE to put it back on the head. Mouse wheel on a tile = nearer / further.
- HEADPHONES (binaural, the real 3D) or SPEAKERS (no ear delays; the up / down colour is kept, panned between the speakers).
- How it sounds: the dry stays in the middle; each placed unit's wet sound is moved. A placed unit becomes a parallel send, so a rack
  with several placed units sounds fuller and louder than the same units in a chain (its MIX knob is its wet level).
- Memory (automation): OFF / READ / TOUCH / WRITE and ARM (one tile or all). Arm a tile, press play in FL, drag while the song plays:
  the move is recorded against FL's bars and beats and plays back every time the song gets there. TOUCH replaces only what you touched.
  CLEAR TILE, CLEAR ALL, or clear a bar range. Per tile: QUANTISE (1/16, 1/8, 1/4: the tile steps on the beat) and LOOP (1, 2, 4 bars).
  Recorded paths show as faint trails; the tile rides its path on playback. Saved with the project. Every tile's angle / elevation /
  distance are also normal FL automation parameters (Spatial <unit> Azimuth / Elevation / Distance).
- Engine: Steam Audio (Valve, Apache-2.0), vendored unchanged; its built-in HRTF (CIPIC). Searched first: Steam Audio, SAF, libmysofa.
- Checks: 294 of 294 green + pluginval 10 on Brandon's Mac (17 new): SPATIAL off = 0.1.7 to the last sample; SPATIAL on with every tile on the head = 0.1.7 + 128 samples
  of latency (reported to FL); right vs left: 13.5 / 15.2 dB louder in the near ear, 29 / 27 samples earlier; above vs below: 19 dB
  difference at 12 kHz; front vs back 6 dB colour difference; 1 m vs 8 m: 18.7 dB; speakers mode works; a fast drag on a tone: nothing
  above -74 dBFS (a small click reads -58); record / play back exact; touch replaces only its bar; quantise and loop; save / load exact;
  a new instance starts empty; the page has no overlaps at 1200 x 760.
- CPU on Brandon's Mac (all 24 units on, 48 kHz, 256 samples): SPATIAL off 37.3 %, SPATIAL's own cost with 4 tiles placed +4.1 %,
  8 placed +7.3 %, all 22 placed +19.0 % (56.3 % in total; run 42667583022). So with EVERY unit on AND every unit placed it does not fit the 40 % rack
  budget. The 40 % check (SPATIAL off, the default) still passes: 38.0 %. Normal use (a handful of units, a few placed) is well under.
- Not done: SPATIAL works at 44.1 and 48 kHz only (Steam Audio's built-in HRTF has no data for 88.2 / 96 kHz; it stays off there and the
  page says so). No room send (the spec's "optional room send"). Below -45 degrees sounds like -45 (the HRTF set stops there). GHOST SQUEEZE
  and GHOST LIMITER have no tile (they work on the whole mix).

## Looks (0.1.9, build 9), 2026-11-09: DAWN, CEL and NEON beside BLACK / PURPLE / LIGHT / GHOST. NOT merged, NOT installed yet
Branch `phantom-fx-looks`. Brandon's Mac still has 0.1.8.

**What is built.** The look button now goes BLACK -> PURPLE -> LIGHT -> GHOST -> DAWN -> CEL -> NEON and round again. A painted look is
a folder of pictures plus one settings file (`assets/looks/<look>/look.json`); NEON went in that way while this was being built, twice
(round 1, then its round 2 art). Every page of the plugin works in every look: RACK with all 24 units, LANES, MODS, PERFORM, SPATIAL.
The look is remembered the way looks already were (the next window opens in the last look chosen; nothing about the sound or the
project is remembered).

**Checks here (Linux, the same test program the Mac runs): 316 of 316 pass** = the 294 from 0.1.8 + 22 new ones:
every page of all 7 looks lays out with nothing overlapping, nothing outside the window and every control clickable at 1200 x 760, at the
smallest window and at 1500 wide (196 pages each time); the painted looks keep all 1229 controls, no dial smaller and no button / menu
lower than on BLACK (about 890 of 1229 have the same or a bigger area; the rest are narrower because CEL and NEON keep the right third
for the picture, the narrowest 65 %); the look button's round trip; a new instance in a painted look is at factory defaults; the sound
is bit-identical with a painted look animating and with no window; the animation costs 4 to 7 % of one core here with software drawing
(DAWN 7.0, CEL 4.6, NEON 5.7), repainting on average 2 to 14 % of the window per tick, nothing on the audio thread. The audio CPU check
is unchanged.

**On Brandon's Mac: not green.** Three runs. Run 42708381023 was replaced by the next push. Run 42711162148: the first 311 checks passed
on the Mac (all layout / hit-area / round-trip / fresh-start checks), then (a) the sound check read 0.0000018 difference between "no
window" and "a painted look animating" (0 on Linux; the check now first measures the engines' own run-to-run repeatability on the
machine, which has not been run there yet), and (b) the animation-cost check did not finish in 15 minutes: drawing a painted look on
the Mac is far slower than here, cause not yet known. I cancelled it. Run 42715762833 (every step under a time limit, the cost check
traced): the runner on the Mac lost its connection about 35 minutes in and has not come back; no log survived. Run 42721237164 (a
2-minute probe that times one paint of each look and publishes that before anything long runs) has been waiting for the runner since
23:32 UTC. Until the probe's numbers exist and the slow drawing is fixed, this must not be merged.

**Bundle:** 36.5 MB of art in the plugin (CEL 5.7, DAWN 8.1, NEON 22.7) + 0.1 MB of typefaces, in each of the three formats.
Backgrounds are JPEG, sprites @2x only, glows / blurred backgrounds / light overlays at 1x.

**Screenshots** (made here, 1200 x 760, every page in the three painted looks, 84 files): `/home/claude/work/phantom-anime/plugin-shots/`
(`dawn-10-rack-fatten.png`, `cel-10-rack-fatten.png`, `neon-10-rack-fatten.png`, `*-28-spatial.png`, ...). The Mac build publishes its
own as JPEG on the `phantomfx-build` branch under `looks/` once a run completes.

**Matches the pictures:** header, tabs, tile strip (3 x 8 in CEL / NEON, 2 x 12 in DAWN), the FATTEN page, macro row, scenes bar and the
whole SPATIAL page sit on LAYOUT.md's rectangles; filmstrip dials with the value ring in each unit's colour; ghosts awake / asleep with
on / off; the machine's lamp, hatch ghost, shoulder ghost (it is the selected unit's ghost), leaves, mist; NEON's signs, flicker, train,
hologram, rain, rings, steam, smoked glass, dial pods, kerb boxes.

**Does not match yet / choices made where the export has nothing:**
- DAWN's FATTEN dials are a little smaller than drawn (108 px cell instead of 116) so the value labels do not touch the row below.
- DAWN's picture shows two small ghosts by the brass window; the export has no anchors for them, so they are not there.
- The warm grade / grain (DAWN, CEL) and the bloom (NEON) lie over the controls in the pictures; in the plugin the controls are a touch
  cleaner. NEON: the lit keys' bloom pictures are not used, rain stays on the alley, the whole-picture "louder = brighter" is only on
  the hologram.
- A unit page with a drawn editor (GATE, GRID, HAUNT, WARP), RETRO (13 dials) and MODS take the wide slab in CEL / NEON, which covers the
  machine's body / the alley's lower half. In CEL those four pages have no blurb line.
- Unit ghosts have one drawing each: their bounce is that drawing squashed and stretched in held steps (NEON's signs dip for two ticks
  instead, the selected one buzzes).
- Hover = a touch brighter (dials, menus, lit buttons); pressed = one pixel down and darker (NEON has its own pressed key); disabled =
  faded. Unit colours with no lit-button / bead art use the nearest that exists.
- SPATIAL: tiles follow the art's distance rings (30 / 80 / 126 / 180 px), so "5 m" on the ring is 5 m.
- Text inside the drawn editors, the step editors, lane chips and vertical bars is still drawn by code in the look's colours on a slab.
- Dial captions with long names ("SIDECHAIN THRESHOLD") are squeezed, as on the drawn looks.

**Art requests for the designer:** lit-button and bead sprites for every accent (CEL / DAWN have 6 and 3); a value paper for DAWN; hover
art for dials / tiles / menus; per-unit bounce frames for the tile ghosts; anchors (or sprites) for DAWN's window ghosts; a slab or
calmer background variant for CEL's wide editor pages; NEON dial strips in the other accents (only pink exists; the neutral strip is
used elsewhere).

**Also fixed in the drawn looks** (found by the new every-page check): RETRO's and SPIRIT EQ's last dial row ran under their menus at
1200 x 760; at the smallest window GATE / GRID / CRACKLE dials and the MODS step-length menus overlapped.

## Looks, round 3 art + the Mac preparation (0.1.9, build 9), 2026-11-09 evening. NOT merged, NOT installed yet
Branch `phantom-fx-looks`. Brandon's Mac still has 0.1.8; its build runner has been offline since the afternoon, so nothing below has run on the Mac yet.

**What is in.**
- Round 3 art in all three looks: every tile ghost and scene ghost moves through six drawn poses (idle, idle, squat, hit, settle,
  asleep) on held frames at 12 fps, driven by each unit's own level; the squash / stretch trick is gone (kept only as a fallback).
  NEON: tube ghosts on the lit signs, painted sleeping ghost on the unlit ones, one still dial body per colour with the pointer
  drawn by code, buttons and slider beads in every accent, the 20 Japanese sign lines. DAWN: value labels on paper.
- Wide pages (GATE, GRID, HAUNT, WARP, RETRO, MODS, and CRACKLE / SPIRIT EQ, which need the room) in CEL and NEON: their own
  background, the agreed slab 12,384,1176,256, the unit header row where it was, the scene ghost on the slab's edge. The blurb
  line is back on those pages in both looks.
- Control width: no switch or menu is under 85 % of its width on BLACK any more (the narrowest is 86 %; it was 65 %). The check
  now fails below 85 %. Dials are the same size or bigger on every page except ECTO ECHO on DAWN (107 -> 93 px).
- Size: 20.0 MB of art in the plugin (DAWN 6.3, CEL 5.5, NEON 8.2 with its typeface), down from 36.5. Full sharpness kept.

**Checks here (Linux): 318 of 319 pass.** The 316 from before + 3 new paint-time budgets (one per painted look). The one that
fails is the audio CPU budget (44 % of one core with all 24 on, limit 40): it is a speed measurement and this container was moved
to a slower machine in the middle of the work; no audio code was touched (only the UI files changed), and the Mac measured 38 %
with the same audio code. It needs the Mac run to confirm. The sound check is exact: 0 difference between no window, a drawn
look, a painted look standing still and a painted look animating.
Drawing here: opening a painted look 180 to 280 ms once, a whole-window repaint 12 to 21 ms (BLACK: 10), the animation 5 to 9 %
of one core.

**Mac drawing speed: prepared blind, unproven.** What changed: every picture is scaled once by JUCE's own software code (the
same on both systems) to the exact size it is drawn at and kept, so painting is plain copies; backgrounds are opaque and kept
per page; nothing is rescaled, blurred or gradient-filled per frame; only the rectangles that changed are repainted. Every
drawing step in the test program now has a hard time limit that names the slow step and stops (so a slow Mac fails in seconds,
it cannot hang), and the short look probe is still the first thing a Mac run does after building. Whether this is enough on the
Mac is not known until the runner is back.

**The 0.0000018 on the Mac: cause not found yet.** The level meter was re-read line by line: it only reads the finished block.
The check now runs five controls so the next Mac run says whether it is the engines themselves (no window involved), any
window, or the painted looks.

**Does not match the art yet.** DAWN's small window ghosts are not drawn (the designer's round 4 brings them as strips; the
plugin is ready for them). NEON's whole-picture bloom and "louder = brighter" are still only on the hologram. Hover is still a
code brightening (round 4 brings hover pictures; dials and menus pick them up automatically, tiles not yet). The lit signs'
flicker cross-fades to the unlit sign instead of a dim frame. On CEL / NEON pages with many switches the switches stand in a
column right of the dials instead of rows underneath. Long blurbs on wide pages can run behind the scene ghost.

**Screenshots** (84, every page of DAWN / CEL / NEON, 1200 x 760): `/home/claude/work/phantom-anime/plugin-shots/`.

## Looks, round 4 art + the three fixes (0.1.9, build 9), 2026-11-09 night. NOT merged, NOT installed yet
Still branch `phantom-fx-looks`; the Mac's runner is still offline, a run is queued for it.
- **Blurb and ghost:** on wide pages the blurb now stops before the scene ghost and wraps to two lines. New check: 252 page
  layouts (every page, three painted looks, three window sizes), none touching.
- **Round 4 art is in (final export):** DAWN's small ghosts by the brass window and on SPATIAL, as drawn pose strips that move
  with the sound; hover pictures for dials, menus and tiles; CEL's frosted wide pane with the machine's hands on its edge; NEON's
  new wide-page scene, richer city and stroke-drawn Japanese. The designer shipped no dim frame for signs, so flicker still fades
  to the unlit sign (the plugin will use a dim frame if one arrives).
- **ECTO ECHO on DAWN:** not restored (93 px, was 107). The only layout that gives 107 with full-width switches is the full-width
  plate, which removes the brass window on that page. Say the word and it is one line.
- **Size:** 19.6 MB of art (DAWN 5.6, CEL 5.4, NEON 8.6).
- **Checks here: 319 of 320.** The one failure is again the audio CPU budget. Run nine times tonight on this container with the
  same audio code: 43.5, 44.0, 44.6, 45.3, then 51.9, 52.6, 52.8, 54.0, 54.4 % (limit 40; the limit was not touched). A spread of
  11 points between sittings on an unchanged program is the shared machine, not the plugin; the Mac measured 38 % with this audio
  code and decides. Sound check exact (0). Animation 5 to 10 % of one core here.
- **Not seen with my own eyes:** the hover pictures (screenshots have no pointer). Not proven: anything on the Mac.
- **Screenshots** (84, new): `/home/claude/work/phantom-anime/plugin-shots/`.

## Looks on Brandon's Mac: the first real numbers (run 42740061431, second try, 2026-11-09 23:32 MST)
- **Drawing speed on the Mac is fixed.** The paint probe ran there in 3 seconds. Opening a look / repainting the whole window / a
  160 x 160 area, in milliseconds: BLACK 41 / 15 / 1, PURPLE 22 / 14 / 1, LIGHT 22 / 14 / 1, GHOST 49 / 46 / 1, **DAWN 447 / 13 / 1,
  CEL 268 / 16 / 2, NEON 322 / 15 / 3**. A painted look now repaints as fast as BLACK (before: a check did not finish in 15 minutes).
- **Why the run still stood still:** not the plugin and not its checks. Right after the probe, the step that publishes those numbers to
  the `phantomfx-probe` branch ran git with the checks' sandbox home folder; there the Mac's keychain helper has no keychain and waits
  for a click. It waited 18 minutes until I cancelled the run. (The first try of that run had died earlier for another reason: the
  runner was restarted one minute after it started.)
- **Fix (workflow only):** the publishing runs with the real home folder, with no keychain helper at all and under a 60-second limit,
  in the probe step and in the Status step. If a keychain window is still open on the Mac's screen, it can be closed with Cancel.
- Not measured yet on the Mac: the sound check, the audio CPU budget, the rest of the table (the run never got there).

## Looks: GREEN on Brandon's Mac (run 42771433292, 2026-11-10 00:04 MST), then merged
- **320 of 320 checks** (317 in the table + the 3 animation-cost checks) and **pluginval strictness 10: SUCCESS**. The whole run took
  11.5 minutes; the probe's numbers were published in 4 seconds this time.
- **Drawing on the Mac** (milliseconds): opening a painted look 274 to 484 once (DAWN 484, CEL 274, NEON 340); a wide page the first
  time 146 to 235; a whole-window repaint after that 12.8 to 21.5 (NEON's wide page 64.2); a 160 x 160 repaint 1.0 to 2.4. BLACK
  repaints in 15. Limits were 5000 / 250 / 60. NEON's animation costs 9.6 % of one core in the test's software drawing (limit 25;
  DAWN's and CEL's figures passed too but were cut from the log by the trace lines).
- **Audio CPU on the Mac: 35.56 %** of one core with all 24 units on (limit 40; all off 2.02 %). So the 44 to 59 % read in the
  container was the container. SPATIAL (informational): off 34.5 %, 4 tiles placed 37.8 %, 8 placed 42.7 %, all 22 placed 52.2 %.
- **Sound check on the Mac: passes, and the 0.0000018 is explained.** Two runs with no window at all differ by 0.000001762, and
  every window case differs from the first run by exactly that. It is not the looks. Several of the open-source engines (the
  Airwindows stages in FATTEN first of all) take their dither's starting numbers from the computer's shared random-number source
  when they are created, so two instances never play bit-identical on a machine where anything else uses that source; it is noise
  at about -115 dBFS, far below hearing. On Linux nothing else uses it, hence 0 there. Left as the engines' authors made it.
- All looks checks green on the Mac: 7 looks, 28 pages each with nothing overlapping at 1200 x 760, smallest and 1500 wide; every
  control kept; none under 85 % of its BLACK width; the blurb clear of the ghosts; round trip; fresh start.

## 0.1.9 INSTALLED on Brandon's Mac (run 42773359869, 2026-11-10 00:20 MST)
- `phantom-fx-looks` was merged into `phantom-fx` (merge c1797138, the same files as the green run) and the install run went green
  in 11.5 minutes: 320 of 320 checks, pluginval strictness 10 SUCCESS, then the install step. Its line in
  `~/Music/GhostBlock Builds/INSTALLED.txt`: **"2026-11-10 00:20  Phantom FX 0.1.9  c179713  INSTALLED"**. VST3 0.1.9, CLAP and the
  app in /Applications; the old copies went to `~/GhostBlock-Backups/2026-11-10_0020 Phantom FX` (moved, not deleted).
- **Drawing in this run** (first full paint when the look opens / every repaint after / a 160 x 160 repaint, in milliseconds):
  BLACK 51 / 17 / 1, PURPLE 19 / 15 / 1, LIGHT 21 / 15 / 1, GHOST 53 / 48 / 2, DAWN 469 / 13 / 1, CEL 277 / 17 / 2, NEON 336 / 17 / 3.
  Wide pages: first time 147 / 216 / 172, then 21 / 23 / 69. NEON's animation: 10.2 % of one core in the test's software drawing.
- **Audio CPU: 36.27 %** of one core with all 24 units on (limit 40; all off 1.94 %). The run before read 35.56 %. Caveat: the
  separate SPATIAL measurement in this same run read 40.07 % for the same setup (34.49 % the run before), so the Mac's own
  spread is about 34 to 40 % and the 40 % check has little room there. The limit was not raised.
- **Sound check: passes; the cause of the Mac's difference is now shown, not guessed.** This run: 0.000001945 for all five
  comparisons (0.000001762 the run before). On Linux, with a test-only helper: the Airwindows engines in TUBE GLOW, FATTEN and BASS
  LAB take their dither's 36 starting numbers per instance from the computer's shared random-number source; one extra number
  drawn from it by anything else just before the first run gives 0.000001997 for all five comparisons, the Mac's pattern exactly.
  It is noise at about -114 dBFS, far below hearing, and it is how those engines behave in any host. (An earlier line in this
  file also named the Surge-team effects and freeverb: the helper saw no such calls, only Airwindows and CloudSeed.)
- What had held the earlier runs: a runner restart (re-run), then git calling the Mac's keychain helper under the tests' sandbox
  home folder and waiting 18 minutes for a click (fixed in the workflow: real home for git, no credential helper, 60 s limit).

## Phase 7 leftovers (0.1.10, build 10), 2026-11-09 night. Branch `phantom-fx-p7`. NOT merged, NOT installed, never run on the Mac
Built on top of the looks branch; pushes to this branch queue nothing on the Mac. Brandon's Mac still has 0.1.8.

**Checks here (Linux): 327 of 328 pass.** The one that fails is the audio CPU budget again (44 to 50 % tonight on this shared
container, limit 40, untouched): the same check read 43 to 59 % during the day on unchanged audio code, so it cannot tell a small
change from noise here. None of the six items adds work to the default all-on rack (MAGNETIC, the linear EQ modes and SPATIAL are
off by default; the spectrum copy only runs while its display shows), but only the Mac's number counts.

1. **Limiter meter: done.** GHOST LIMITER's page shows its gain reduction (bar to 24 dB, held peak, number) in all seven looks.
2. **Patterns in scenes / A-B / presets: done.** Scenes, snapshots, the rack's A/B, a unit's A/B, unit presets and presets now
   carry the drawn patterns. Old projects load as before. On a scene change the pattern switches at the switch point (it does
   not glide, whatever the morph time). An open GATE / GRID editor follows.
3. **SPIRIT EQ: done, with two things to know.** Linear phase Low / Medium / High (latency 1536 / 3072 / 6144 samples, reported):
   same curve as Zero Latency to within 0.07 dB, impulse response symmetric. (a) Dynamic bands do not move in the linear modes.
   (b) Sweeping a band in a linear mode moves in steps of one frame (21 to 85 ms). Spectrum: a SPECTRUM switch on the EQ page shows
   the output spectrum in place of the dials, in every look; on the four drawn looks it is a low strip.
4. **RETRO bias aliasing: fixed.** Before -73.8 dBFS at 100 % bias, after -85.8 (limit -80). Changes the sound only with MAGNETIC
   on; costs more CPU there (about 770 against roughly 470 microseconds per block here, the "before" estimated).
5. **SPATIAL at every common rate: done.** 88.2, 96, 176.4 and 192 kHz now work (the spatial stage runs at 44.1 / 48 behind
   rate changers; latency 330 to 802 samples, reported). 44.1 / 48 unchanged to the bit.
6. **Factory presets: a first bank of 34**, every unit used, in the PRESETS menu by tag. Tuned by numbers, not by ear: Brandon
   should audition them. No separate tag-filter browser window; no patterns / scenes / modulation inside the presets yet.

**Sound that changes on purpose:** RETRO with MAGNETIC on; SPIRIT EQ only when a Linear mode is chosen; SPATIAL only at the four
new rates (it was off there). Everything else nulls exactly as in 0.1.9 (SPIRIT EQ's own null -240 dBFS, RETRO's -112).

**Release checklist, what could be done without the Mac:** version 0.1.10, THIRD_PARTY.md (no new outside project: the new DSP
uses JUCE's FFT and filter designer and the engines already in), README status, DECISIONS 66-71. **Left:** a Mac run of this
branch (after the looks run is green and merged), pluginval on it, the manual, a signed / notarised build and a GitHub release
(the common rules say no public release without Brandon), auditioning the presets, and the smaller gaps named above.

## 0.1.10: three looks only + 17 painted unit visuals (branch phantom-fx-visuals, 2026-11-10)
- **Three looks only.** The look button cycles DAWN, CEL, NEON; a new window with no saved look opens in DAWN; a saved preference
  naming BLACK / PURPLE / LIGHT / GHOST (or anything unknown) opens DAWN. The old looks' code is kept, hidden behind a build flag.
- **17 painted unit visuals** in DAWN, CEL and NEON: FATTEN, CRYSTAL, ECHO, CHORUS, FORMANT, FILTER, SQUEEZE, LIMIT, CRUSH, TUBE,
  WRECK, BASS, SPACE, SHIMMER, HALLS, SPECTRA, DOOR. The designer's painter was ported line by line; each unit was rendered by both
  with the same settings and the same beat: 1 to 3 grey levels apart in every look (side by side:
  /home/claude/work/phantom-anime/plugin-shots/visuals/<unit>-compare*.jpg; in the window: <look>-<unit>-page.jpg).
  The seven wide-page units show no picture yet (batch 3 is being painted; DAWN's rectangles for it are in the designer's PROGRESS.md).
- **Checks here: 326 of 327.** The one that fails is the audio CPU budget (45 % on this shared container, limit 40, untouched; no
  audio code changed). New: every control of every painted visual changes its picture in every look (on a still in silence, by
  motion, or with sound; the check says which), the sound moves every picture, a picture paints in 1.3 to 2.9 ms on average here,
  and neither the stage nor its frame touches a control on any of the 51 pages. Look checks 24 of 24.
- **Paint cost here** (one picture of the stage): 1.2 to 3.1 ms at 1x, 2.9 to 9.3 ms at Retina 2x; above 6 ms a picture the visual
  drops itself to 12 pictures a second. The Mac run's probe now prints the same numbers for his Mac.
- **Art size:** 1.22 MB of visual parts in the three looks (budget about 6 MB).

## 0.1.10 INSTALLED on Brandon's Mac (run 42786156275, 2026-11-10 02:10 MST)
- `phantom-fx-visuals` merged into `phantom-fx` (merge ff50fda). Mac run green: **324 of 324 checks + 3 animation-cost checks, pluginval
  strictness 10 SUCCESS**, then the install. INSTALLED.txt: **"2026-11-10 02:10  Phantom FX 0.1.10  ff50fda  INSTALLED"**; the old
  0.1.9 moved to ~/GhostBlock-Backups/2026-11-10_0210 Phantom FX.
- **Audio CPU on the Mac: 35.75 %** of one core with all 24 units on (limit 40, untouched; all off 1.90 %).
- Sound check passes (the Mac's usual 0.0000016 from the Airwindows dither seeds, same in the no-window runs).
- **Unit visuals on the Mac** (one picture of the stage, 1x / Retina 2x): DAWN 2.5-3.1 / 4.6-8.6 ms, CEL 2.1-2.5 / 3.8-6.8 ms,
  NEON 3.5-5.8 / 9.3-11.3 ms. Above 6 ms a picture the visual runs at 12 pictures a second instead of 24: on his Retina screen
  that is NEON always and some DAWN / CEL units.

## 0.1.11: the seven wide-page units painted (branch phantom-fx-visuals, 2026-11-10)
- **GATE, GRID, HAUNT, WARP, CRACKLE, RETRO, EQ** now have their painted pictures in DAWN, CEL and NEON (batch 3, the designer's
  u_gate.js .. u_eq.js, ported line by line). Side by side with the painter: /home/claude/work/phantom-anime/plugin-shots/visuals/
  gate-default / gate-pump / gate-sidechain, grid-chaos / grid-tonal / grid-breakdown, haunt-idle / haunt-glitch / haunt-lpsweep,
  warp-default / warp-formant / warp-tape, crackle-default / crackle-3bands, retro-default / retro-busy, eq-default / eq-out-mode (.jpg).
- **Where:** CEL in a smaller show box on the wide slab (the robot holds it); NEON on its holo screen; the wide pages' info line
  is hidden while the picture shows. DAWN: a picture of each unit's own behind its editor (GATE, WARP, HAUNT), over it (GRID's
  light on its step grid), in a small pane of its own (RETRO, EQ), or the brass window (CRACKLE). DECISIONS 80-83.
- **Fed by the editors:** the playhead, pump, patterns, pads, the bend lane and SPIRIT EQ's own curve. The one audio-side
  addition: SPIRIT EQ stores each band's dynamic gain in a relaxed atomic for the picture (the sound is unchanged).
- **Checks here: 326 of 327** (the one: the audio CPU budget, 46 to 48 % on this container, limit 40, untouched; 0.1.10 read 45 %
  here and 35.75 % on the Mac). Look checks 24 of 24: every control of all 24 painted units changes its picture in every look,
  the sound moves every picture, 1.5 to 3.9 ms a picture at 1x here; no stage or frame meets a control; a DAWN picture under or
  over an editor takes no clicks and every control there is still reached.

## 0.1.11 INSTALLED on Brandon's Mac (run 42798652851, 2026-11-10 03:41 MST)
- `phantom-fx-visuals` merged into `phantom-fx` (merge 722b42a). Mac run green: **324 of 324 checks + 3 animation-cost checks,
  pluginval strictness 10 SUCCESS**, then the install. INSTALLED.txt: **"2026-11-10 03:41  Phantom FX 0.1.11  722b42a  INSTALLED"**;
  the old 0.1.10 moved to ~/GhostBlock-Backups/2026-11-10_0341 Phantom FX.
- **Audio CPU on the Mac: 36.05 %** of one core with all 24 units on (limit 40, untouched; all off 1.92 %; 0.1.10: 35.75 %).
- Sound check passes (the Mac's usual 0.0000019 from the Airwindows dither seeds, the same in the no-window runs).
- **The seven new pictures on the Mac** (one picture, 1x, the slowest look): GATE 2.43, GRID 2.39, HAUNT 2.90, WARP 1.67,
  CRACKLE 2.64, RETRO 2.23, EQ 2.07 ms. All 72 unit pages checked on the Mac too.
- The run's artifact upload was refused (GitHub's artifact storage quota for the account is full; it recalculates in 6-12 h). It
  does not affect the build or the install; the logs went to the phantomfx-build branch as usual.

## After 0.1.11 (on the branch, NOT installed)
- Two late painter changes from the designer, ported and compared (plugin-shots/visuals/warp-formant.jpg, warp-formant-2.jpg,
  haunt-gate-a.jpg, haunt-gate-shut.jpg): WARP's KEEP FORMANTS face placement and frame; HAUNT's GATE-gesture shutter. Look checks
  24 of 24 here. They go to Brandon's Mac with the next install.

## 0.1.12: the unit picture over SPATIAL (Brandon's bug, 2026-11-10 12:35)
- **Cause, plainly:** when you went from a unit page to SPATIAL, the code that lays out the SPATIAL page left early and never
  got to the line that hides the unit picture, so the picture stayed where it was, on top of SPATIAL's side panel. LANES, MODS
  and PERFORM did hide it. It "sometimes" happened because it needed a unit page to be open just before SPATIAL.
- **Fix:** SPATIAL hides the picture as the other tabs do (it also stops drawing, and the NEON hologram / train, the CEL hatch
  ghost and the DAWN scene ghosts come back). New check: 1,048 page changes in all three looks; it failed on 0.1.11's code and
  passes now. Also in this build: the designer's two late fixes (WARP KEEP FORMANTS face, HAUNT GATE shutter).

## 0.1.12 INSTALLED on Brandon's Mac (run 42890090540, 2026-11-10 13:15 MST)
- Merge 29f9fb6. Mac run green: **325 of 325 checks + 3 animation-cost checks, pluginval strictness 10 SUCCESS**, then the install.
  INSTALLED.txt: **"2026-11-10 13:15  Phantom FX 0.1.12  29f9fb6  INSTALLED"**; the old 0.1.11 moved to
  ~/GhostBlock-Backups/2026-11-10_1315 Phantom FX. The new page-walk check passes on the Mac (1,048 page changes).
- Audio CPU on the Mac: 37.03 % with all 24 units on (limit 40, untouched; all off 2.06 %).
