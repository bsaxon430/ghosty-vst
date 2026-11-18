# GhostBlock plugin roadmap (owner's ideas, agreed 2026-10-20/21)

Rules for every build: original designs only (no artist/brand names in UI, presets or code, no third-party samples);
builds go through `ghostybuild <dir> --target X` (one build at a time, -j2, -j1 for ASan; the server OOM'd on 2026-10-21);
never kill processes by name.

## The bar (owner, 2026-10-21)
Every plugin must be good enough that the most demanding producers would pick it up and love it: an experimental IDM producer
(tracker-level rhythmic control, spectral weirdness), a bass-music producer (huge, nasty, fast sound design and chops), a progressive
house producer (clean hypnotic arps, precise drums, polished sound). No cutting corners.

FINAL GATE before any plugin ships: (1) all tests + ASan clean; (2) a separate code-review helper hunts bugs;
(3) a "PRO REVIEW" helper evaluates the plugin through each of those three producer lenses (based on their publicly known workflows,
never naming them in the product): workflow speed, sound quality (measured), missing pro features, anything that feels amateur,
then writes a punch list; (4) fix the punch list; (5) owner's ears in FL.

## No "later" (owner, 2026-10-21)
Every item marked "later"/"phase 2" anywhere in this file or in chat is IN SCOPE NOW: Beats spectral freeze, frequency-dependent reverse,
user scripting, phrase locks, large layout; Mickens filter routing (parallel/serial/morph, comb, formant, rhythm-triggered sweeps) and
break search by feel + BREAK MATCH; Ghost Box PARALLEL lanes, SPECTRA, BASS LAB, PORTAL, FORMANT, sidechain pump upgrade.

## Shipped (installer 2026-10-21)
- ArpEZator VST (SUNRISE/DUSK/MIDNIGHT, recipe engine, lanes + drag MIDI/AUDIO mix/stems, SIMPLE|RICH chords,
  piano roll pinch/scroll/audition, glide/fatten handles, undo/redo, lane volumes, per-mode memory, save/load, muted-lane banner)
- Ghosty Pro poly stuck-note fix; Poltergeist cursor fix + MUTE GHOSTS/SAMPLE; ArpEZator Gumroad license.

## Done, waiting for the next installer
- Ghosty Pro BEND tab (drawable tempo-synced pitch lane, 10 shapes) + Ghost Box WARP unit (PITCH / TAPE).
- Poltergeist CHOP mode (auto-chop vocals, drag slices to pitch, tune to key, chop melody, bounce/crunch, Lab warp-section pitch).

## In progress
1. Ghosty Beats revamp: 300+ original drums, 20 kits, sound browser, drop-a-sample, per-step lanes (vel/nudge/prob/roll/flam/pitch/decay),
   polymeter per track + CONVERGENCE, swing/grooves, per-track processing + layers + sidechain, drag MIDI/AUDIO (pattern|song, mix|stems);
   unified rhythm engine: TRACKER view (1/64, retrig/pitch/reverse/offset/cut), BREAK CHOP, RHYTHM DNA (keep x%, blend A/B), BREED (star favourites),
   phrase arc, conditional probability rules, 8 macros (CHAOS DENSITY MUTATION FRACTURE GRAVITY MEMORY TENSION RESOLVE), per-hit destruction v1,
   GENERATE styles MACHINE GUN / INTRICATE. Later: spectral freeze, scripting.
2. Poltergeist BASS: 6 ghosts + noise ghost, ~30 waveforms, feedback, multi-stage envelopes, 2 filters (LP12/24 ladder, BP, HP, notch, comb, formant, dirty),
   SUB section (+ sub solo, low-end mono), bass FX (dist + multiband), drawable WOBBLE LFO + mod matrix, 50+ bass presets, SUMMON bass words.
3. Ghost Box 3: GHOST MORPH (A/B states, one knob), GHOST MOTION (5 drawable lanes, Motion DNA), GHOST SPLIT (attack vs body chains),
   8 macros/performance, CHAIN BRAIN (goal -> 3 chains A/B), chain arrows + position numbers + order tips, later PARALLEL lanes,
   later SPECTRA (share Spectrum engine), BASS LAB (TRANSLATE), PORTAL, FORMANT, sidechain pump upgrade.

## Queued
4. ArpEZator HOOK LAB: generate 16 hooks / find the magic / make it a song / combine parts; EAR CANDY (subtle/hook/euphoric/unhinged);
   COMPOSITION STYLE (hypnotic/anthemic/minimal/emotional/playful) independent of mood; MELODIC TENSION; GENERATE RESPONSE + similarity;
   arp DIRECTION/CONTOUR/ACCENT; musical EVOLVE (grow/wave/journey, keep strongest motif, lock motifs); motif harvester that learns; piano test.
5. Ghosty Spectrum SPECTRAL LAB tab (keep existing tabs as they are): spectral paint + image-to-sound, SPECTRAL GHOST (evolve/memory/decay),
   SPECTRAL SPLIT (per-band rhythms), CHAOS PAINT, SPECTRAL DNA morph, SEND TO BEATS shared folder.
6. Mr. Mickens 2: BREAK DNA + GROOVE LOCK + REMIX knob (preserve/rewire/mutate, freeze DNA), GENERATE FILL (5 styles), INTERLOCK bays + bay ducking,
   performance STUTTER (rate/gate/decay/pitch/movement; roll/ramp/glitch/breakdown), SNAPSHOT DECK + morph; later filter routing, break search + BREAK MATCH.
7. Ghosty Pro TRAP: synth brass engine + presets, STACK (3 layers, reverse), SAMPLE layer (formant-keep pitch), CHARACTER knob, ~80 trap presets, tape/slowed mode.

## Business
- New prices from Oct 28 (after LAUNCH30): Poltergeist $49, Ghost Box $49, Ghosty Pro $39, ArpEZator $29, Beats $39 alone, Spectrum $19 alone, Mickens $25, bundle $99.
- After features land: new descriptions, web + PDF manuals per plugin, "What's new" notes.

## 8. LEADS pack (owner, 2026-10-21; after the current gate)
Ghosty Pro: ~40 new original lead presets in folders HYPNOTIC (warm saws, filter-moving evolving plucks), EUPHORIC (huge unison supersaw
stacks using STACK layers, emotional plucks), AGGRESSIVE (metallic, distorted, screaming). Each lead preset gets 4 macros: BRIGHTNESS, WIDTH,
MOVEMENT, SIZE (one-knob build-up automation). Level-matched, tested like other presets. Poltergeist: ~10 FM screech/metal LEAD presets using the
BASS-update engine. ArpEZator: preview sounds tuned per mood (SUNRISE euphoric supersaw/piano, DUSK pluck, MIDNIGHT hypnotic saw with filter LFO).
Principle: simple clean big sounds; the melody is the magic. No artist/preset names from other products.

## 9. LINEAR DRUMMING (owner, 2026-10-22; Ghosty Beats)
A LINEAR button that converts the current pattern/performance so only ONE hit sounds at any instant (no simultaneous kick+hat etc.).
At each collision choose one voice by a selectable rule set (priority order, keep accents/downbeats, alternate, groove-preserving, random-seeded)
and optionally displace the losers to the nearest free slot instead of deleting them (MOVE vs DROP), incl. retrigs/rolls/flams counted as hits.
VARIATION < > buttons cycle through different seeded linear versions (show a small preview strip), STRICTNESS (sub-step tolerance window),
LOCK lanes that must survive. Non-destructive (toggle back to the original), undoable, works on SONG/evolved performances too,
and the result is exactly what DRAG MIDI / DRAG AUDIO export. Tests: zero overlaps within the tolerance window; seeded variations differ; anchors kept.

## 10. UPIC page (owner, 2026-10-22; Ghosty Spectrum, new tab, existing tabs unchanged)
Inspired by the historic line-drawing composition system (original implementation, no names in product): draw ARCS (x=time, y=pitch, log),
each arc an oscillator voice gliding along the drawn line; per-arc hand-drawn WAVEFORM, drawn amplitude ENVELOPE and drawn PAN; page
LENGTH (1 beat .. minutes, tempo-sync), play backwards / jump / loop region, playhead speed lane; stamp tools (copy, mirror, invert,
stretch, rotate groups); arc-to-arc FM (one arc modulates another); quantise-to-key option; band-limited oscillators, no zipper;
drag out MIDI (one note per arc with pitch bend/MPE option) and audio; save/load pages; undo. Tests: drawn line at 440 Hz -> 440 Hz;
glides follow the line; reverse = time-reversed output; FM depth works; no NaN; screenshots + 2 MP3 demos.

## 11. PROCESS LAB (owner, 2026-10-22): destructive/experimental offline processes (inspired by classic composer-desktop processing suites
and destructive wave editors; original implementation, no product names). Shared engine (e.g. Shared/ProcessLab.h) used by Poltergeist SAMPLE LAB
and Spectrum SPECTRAL LAB: select a region -> apply: wavecycle shuffle / repeat / omit / reverse / interleave, brassage (granular reassembly),
iterate (repeat with pitch/time drift), spectral blur / trace (keep N loudest bins) / freeze / stretch / shift, envelope impose (draw a breakpoint
envelope and apply), extend/loop, normalise, reverse, fade; chainable; BOUNCE and process again; seeded; undo; preview before apply.
## 12. VOICE page (Spectrum): pitch + formant analysis/resynthesis (speech-analysis style): change pitch keep formants, swap/borrow formants
from another sound (make a synth "speak"), draw a pitch contour onto a sound, vowel morph; drag out audio.
## 12b. GHOST VOICE page (Spectrum; owner 2026-10-31, built 2026-11-01): sing a take, blend your voice's fingerprint with another voice's (a recording,
a pad, or a designed identity: Glass Angel, Midnight, Electric Soul, Dream Machine, Velvet, Tiny, Giant, Alien, Choir Of One, Static Ghost) and the
blend sings your performance (pitch, timing, words, vibrato kept). Classic DSP on WORLD (Shared/VoiceFusion*, reusable by a standalone Ghost Voice
plugin later). 2- or 3-way blends, KEEP MY CONSONANTS, VOICE DESIGN knobs, TUNE / transpose, STACK choir, SAVE VOICE presets, demo-gated exports.
Done: ghosty-pro/docs/GHOST-VOICE.md. Next (not started): a standalone Ghost Voice plugin on the same Shared engine + host; real-time preview.

## 13. VINTAGE SAMPLER CHARACTER: one-click modes for Poltergeist samples and Ghosty Beats tracks: 8-bit home computer, 12-bit classic sampler,
early-90s lo-fi (sample-rate/bit-depth/aliasing/filter/noise models, no copied hardware names).
## 14. MELODIC TRACKER (Ghosty Beats): tracker columns can play pitched sample instruments as melodies (note, instrument, volume, effect columns),
portamento/arpeggio/vibrato/retrig effects like classic trackers; drag out MIDI/audio.
## 15. ARRANGE page (Spectrum; "paint the sounds, then sequence them into a piece", owner 2026-10-22): multitrack timeline where anything made
in Spectrum (painted/spectral sounds, UPIC pages, VOICE pieces, PROCESS LAB results, loops) becomes a clip; per-clip pitch, stretch, reverse,
fades, gain, pan, spectral morph between adjacent clips; tempo-synced grid, loop regions, send to/receive from Ghosty Beats (shared sounds folder);
render the whole arrangement in the background; drag out audio (mix or stems) and MIDI for note-based clips; save/load; undo.

## 16. FL CHANNEL-MUTE FOLLOW (owner, 2026-10-22; Mickens, Beats, ArpEZator, Spectrum Arrange/ARCS/Lab)
In FL Studio the channel-rack mute light only stops the NOTES sent to a plugin; it tells the plugin nothing else. Plugins that play on their own from the host
transport (Mr. Mickens, Ghosty Beats, ArpEZator, Spectrum's ARRANGE / ARCS / Lab players) therefore keep playing when the channel is muted. Add a header switch
PLAY: ALWAYS | ON NOTES (next to BYPASS; ALWAYS = today's behaviour, default; saved per project, never in presets): in ON NOTES the plugin plays only while it
receives a MIDI note (any key; held notes gate it, short fade in/out, never stuck), so muting the channel stops it and unmuting starts it again at the next note,
in time with the song (the user draws one long note per bar/pattern like for any FL instrument). Mickens' existing HOLD TO PLAY (C2) stays; Beats' pad notes must
not collide with the gate (pick a clear rule and show it in the UI). Tests: no note -> silent, note held -> plays in time, note-off -> stops (fade, no click),
mute simulated by removing notes, no stuck state, state round trip, old sessions load ALWAYS.

## 17. SYNC TO DAW (owner, 2026-10-22; Mickens, Beats, ArpEZator; later Spectrum's ARRANGE/ARCS/Lab players for consistency)
Shared/SyncTransport.h (built + tested). Header switch SYNC: DAW | OWN. DAW (default) = today's behaviour (DAW play/tempo/position drive the plugin, SPACE not taken).
OWN = independent clock + own editable tempo + own PLAY and SPACE (focused plugin plays alone; DAW transport/tempo ignored; DAW play doesn't start it).

## Owner requests 2026-10-22 evening (queued for installer #4; weekly credits were at 93%)
18. SNAPSHOT button (Poltergeist + Ghosty Pro first, later Mickens/other synths): one click saves the current sound as a preset into an auto "Snapshots" bank (name = time/stamp, optional rename, newest first), quick cycle through snapshots (prev/next + list), delete/rename/star, saved on disk like user presets. Never lose the sound: also keep a rolling undo of the last N snapshots.
19. SNAPSHOT MORPH / SPIRIT BOARD for synths: same design as Ghost Box MORPH A/B and the planned multi-ghost SPIRIT BOARD (item 20): a graph where each little ghost is a snapshot OR any factory preset; drag a puck between them and the sound blends (continuous params interpolate, switches take highest weight with hysteresis, no zipper/clicks), puck X/Y automatable + orbit modes. Build the board once as a shared component (Shared/SpiritBoard.h) used by Ghost Box (scenes) and the synths (snapshots/presets).
20. Ghost Box SPIRIT BOARD (multi-scene morph, up to 8-12 ghosts). Not started. Name approved by owner.
21. Ghost Box FATTEN: modes are built (FxBox.h) but hidden (miss the fatness targets on kicks/808/drum loop). Redo SUB/BODY with a parallel low-band compressor + staged FAT curve (glue -> saturate -> heavy clip), input gain + clip LED, PUNCH + LOW CLEAN options. Original design (Sausage Fattener only studied as a public description: FATNESS/COLOR/GAIN).
22. Known open: ArpEZator 9 low-contrast text spots (ArpEZator report), Ghosty Pro all-knobs sweep 13 unverified entries, FX tab 32 faint labels (should be fixed by Ghost Box theme work: re-check), other plugins' Bypass/Sync buttons should call Bypass setTheme(Theme) for PURPLE.

## GHOSTY PRO 2 (owner decision 2026-10-23): a new plugin, not a patch of Ghosty Pro
Ghosty Pro (v3.x) stays as it is (additive fixes only). Ghosty Pro 2 gets:
- the Serum-style layout the owner likes: both oscillators visible with their wavetable displays (click a panel to select; OSC 1 = pink, OSC 2 = blue), envelope + filter curves you can see AND drag, LFO displays, macros, a mod matrix, a keyboard strip; drag a knob onto an LFO/envelope (or drag a source onto a knob) to connect modulation with an amount ring on the knob;
- the newest "Nexus"-style cleanup of the look (cleaner, fewer boxes), the 3 house looks (BLACK/PURPLE/LIGHT);
- FATTEN (modes, INPUT, PUNCH, LOW CLEAN) in its FX rack (Ghost Box already has it), SNAPSHOT bank + MORPH board (already in Ghosty Pro), SPIRIT BOARD from Ghost Box;
- amp ENV 1 display + LFO phase dots, FX-rack knobs blending in the morph board.
Plan it properly first (design pass + official-SDK/open-source check per CLAUDE.md), reuse Shared/ pieces (GhostThemes, SnapshotBank, SpiritBoard, WaveRegion).
- (owner 2026-10-23, revised later the same day) GHOSTY PRO 2 oscillator section: TWO wavetable oscillators (OSC 1 pink, OSC 2 blue) PLUS a SUB oscillator, done perfectly, NOT four. Serum-style: both oscillator displays visible at once, the LFO, envelope and filter curves visible and draggable, drag a knob onto an LFO/envelope/filter (or a source onto a knob) to modulate it with an amount ring. Always clean (the CLEAN SOUND system is part of the engine from day one, plus oversampling with a quality switch and per-sample modulation smoothing).

## CLEAN SOUND system (owner 2026-10-23: "use every trick in the book" so layered sounds never get muddy or clip)
Applies to: Ghost Box LAYER mode, Ghosty Pro + Poltergeist LAYER mode, Ghosty Pro TRAP page (stacks, brass, sample layers, tape), and plain synth patches with several oscillators/unison. Build ONE shared module (Shared/CleanStage.h) + per-plugin wiring, default ON for layers/stacks, switch + a few knobs:
- GAIN STAGING: automatic per-layer trim by layer count/weights (equal-power: ~-3 dB per doubling), keep summed loudness steady; peak-aware headroom meter (green/amber/red clip LED) on the output.
- LOW-END MANAGEMENT: ONE layer owns the sub (lowest-octave/ sine-sub layer); high-pass the other layers (adjustable crossover ~60-150 Hz, 12/24 dB, linear-phase option); MONO BELOW x Hz (default 120 Hz) on the sum (mid/side low mono); optional sub-harmonic cleaner (cut 20-30 Hz rumble, DC block).
- PHASE: per-layer phase alignment / polarity check (auto-flip a layer if the sum loses low-end energy), random vs reset start phases per voice, tiny detune between same-pitch layers to avoid comb filtering; all-pass phase-rotation option.
- UNISON: limit stereo spread and detune on low notes (frequency-dependent spread), keep the centre voice mono.
- SPECTRAL MASKING: dynamic/multiband EQ that ducks overlapping bands between layers (mud band 150-400 Hz), optional per-layer sidechain ducking of low-mids by the bass layer; auto 'mud' meter.
- OUTPUT: oversampled soft clipper/limiter (look-ahead true-peak, 2x-4x oversampling), gentle saturation for perceived thickness without low-end build-up, final DC blocker.
- TRAP page brass: BRASS button is now a real ON/OFF toggle (done in installer #11).
Numeric tests: summed loudness constant for 1..6 layers within 1 dB; low-band (<120 Hz) is mono; no clipping above -0.3 dBFS on stress patches (6 layers x unison 8 x full-key chords); mud-band energy lower than without CLEAN at equal loudness; old patches unchanged when CLEAN is off/new patches default per above.

- (owner 2026-10-23) GHOST BOX: the full CLEAN SOUND treatment applies to SPIRIT BOARD + LAYER mode: two ghosts blend great, three or more get messy -> gain law across active racks, mono lows, side budget between wide racks, mud control, final true-peak ceiling; every effect at max must stay crisp and intentional.

## (owner 2026-10-24) ONE GHOSTY PRO: Pro 2 is merged into Ghosty Pro
No separate Ghosty Pro 2 plugin. Ghosty Pro itself grows into it: everything it has now (the CLEAN SOUND / Oliver logic, the sounds, presets, Ghost FX, TRAP, MORPH, snapshots) PLUS the Serum-style features and UI: both oscillators (+ SUB) visible with their wavetable displays, draggable envelope / LFO / filter curves, drag-a-knob-to-modulator with amount rings, visible mod routing. Same plugin ID, so every existing project and preset keeps loading and sounding the same (state migration, old-preset compare stays green). Planned as its own design pass after the current sound-quality work (Ghost Box FX + Pro FX + Beats drums).

## (owner 2026-10-24) POLTERGEIST = pure FM bass engine; SAMPLE features become their own product
Poltergeist focuses on being an incredible, perfectly clean FM bass engine / FM sequencer (clean even when many FM ghosts are blended). Its SAMPLE / SAMPLE LAB / PROCESS LAB UI comes out (old sessions that used samples still load and play). The sample features (WaveRegion editor, Sample Lab timeline + chops, Process Lab, drag-out) become a NEW separate sample plugin once each piece is perfect. Later maybe a small sample piece back in Poltergeist for growls with a sample attached. Note for the owner (answered 2026-10-24): code size itself does not change the sound; only the stages a signal actually passes through do. Rule kept: features that are off must be bit-transparent (tested).
