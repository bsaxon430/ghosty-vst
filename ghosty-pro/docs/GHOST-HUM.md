# GHOST HUM 1.0 (build 3) — Sing it. Beatbox it. Make music.

Ghost Hum turns your voice into MIDI. Hum or sing a melody and it becomes notes on a piano roll, played back on a piano. Beatbox
("boom tss pah tss") and it becomes kick / snare / hi-hat / open hat / clap hits on drum lanes. Then drag the MIDI (or a rendered WAV)
into FL Studio or any DAW. No musical training needed. Mac only for now (VST3, AU, and a standalone app).

## How to use it

1. Open it one of three ways. All three record.
   - **The Ghost Hum app** (easiest): it listens through your Mac's microphone (the input chosen in Options).
   - **FL Studio's channel rack** (add Ghost Hum like an instrument): FL gives an instrument no audio, so Ghost Hum listens through your
     Mac's microphone by itself (INPUT = AUTO, the default). Its piano / drums come out of that channel and the notes go out as MIDI
     while it plays. The first time, your Mac may ask whether FL Studio may use the microphone: say yes.
   - **A mixer insert** on the track your microphone comes in on (FL: the mixer track with your mic as its input; any other DAW: the
     vocal track): Ghost Hum listens to that track, as before.
2. Choose MELODY or BEATBOX at the top.
3. Tap the big button. Sing / hum / beatbox. Tap again when you are done (it stops by itself after 90 seconds).
4. Ghost listens for a moment, shows what it heard in the roll, and plays it back right away.
5. If you recorded without a click (TEMPO = FREE), Ghost guesses the tempo and offers choices: KEEP ORIGINAL, the guess, and its double
   and half. Tap one and listen; ON GRID turns on 1/16 quantize so you hear it snapped.
6. Fix anything by hand in the roll. CLEAN tidies the take. UNDO brings anything back.
7. DRAG MIDI into your DAW's piano roll (or any instrument channel). DRAG WAV gives you the audio you hear.

## New in build 3: it records wherever you load it

- **The bug**: loaded in FL Studio's channel rack (as an instrument), the ring said TAP TO STOP but nothing was recorded. An instrument
  gets no audio from FL, and Ghost Hum only recorded the audio the host handed it.
- **The fix**: when the host gives Ghost Hum no usable audio, it listens through the Mac's default microphone itself (its own input-only
  audio device; macOS lets several apps read the mic at once). "No usable audio" means the host gives it no input channels at all, or
  feeds digital silence for the first half second of a take while the microphone has sound. Whenever the track carries real sound, the
  track is used, so a mixer insert on the mic track works exactly as before. The mic is opened on the first record tap that needs it,
  never on the audio thread, and closed again when the plugin goes away or after a minute unused.
- **INPUT AUTO | TRACK | MAC MIC** (under TEMPO; saved with the plugin state): AUTO as above, with a few words next to it saying what it is
  doing ("sound on this track" / "no track audio: your Mac's mic" / "now: your Mac's mic"). TRACK = only the audio coming into this track
  (in the app: the input chosen in Options). MAC MIC = always the Mac's microphone.
- **The status line says where it listens**: "Listening through your Mac's microphone..." or "Listening to this track...". If nothing at
  all arrives for 2 seconds: "No sound is coming in. Check your microphone (or put Ghost Hum on your mic's mixer track)." (the mic version
  names the macOS permission). A take that stayed at digital zero says so too, instead of "Ghost heard no notes".
- **The ring follows the real source**: its level and crackle come from whichever input is being recorded.
- **Clean in the channel rack**: with no input bus, the output is cleared before the piano / drums are added (a host may hand an instrument
  leftover data), the count-in and metronome still click out of the channel, and the click times (seconds of the take, from the host's
  clock) still mask the speaker leak in a microphone take. A microphone take is recorded and analysed at the microphone's own sample
  rate. The smallest window is now 760 x 600 (one more row in the settings panel).

## New in build 2

- **The look** (the owner's pictures): near-black with soft green / teal light wisps and streaks, drifting particles and a few faint ghosts
  behind glass panels with thin glowing green edges; dark-glass pill buttons with green rims (the chosen one is bright green with a bloom);
  the record button is a thick neon ring with an electric crackle (teal, with concentric rings, in MELODY; stronger while recording); the
  glowing white / cyan ghost logo with a green glow and trailing wisps. Flat, every label real and readable.
- **GHOST ANIMATION: ON / OFF** (top right, next to BYPASS; also the G key): OFF stops every animation (ghosts, wisps, particles, ring
  crackle, logo pulse) and its timer completely; the last frame stands. Kept in the plugin state and as an app setting. Default ON.
  The animation also pauses while the window is hidden or minimised.
- **YOUR BEAT is a step sequencer** (FL-style): one square pad per step (16 per bar at 1/16; QUANTIZE picks the step; more bars scroll with a
  bar at the bottom, the wheel or Shift-wheel), grouped in 4s with alternating beat shading and thick bar lines. A hit is a lit glowing pad:
  brightness = velocity, with a small level bar. A "not sure" hit is faint with a dotted ring. Click an empty pad to add a hit, click a lit
  pad to remove it, drag a hit to another pad or lane. QUANTIZE OFF (or ORIGINAL) shows the exact timing as an offset glow inside the pad.
- **YOUR MELODY is an FL-style piano roll**: bar lines strongest, then beats, then steps; black-key rows darker; C rows highlighted and
  labelled; notes are glowing green bars with an outline and a velocity tint; bar numbers along the top.
- **Selection**: drag an empty spot for a box, Shift / Cmd-click adds, Cmd+A all. Alt-drag drags a copy. Everything below works on the
  whole selection.
- **Key commands** (the ? button lists them): Space play / stop · R record · Q next quantize grid · G ghost animation · T tap tempo ·
  Cmd+Z / Shift+Cmd+Z undo / redo · Cmd+A select all · Delete / Backspace · arrows move (a step sideways, a semitone or lane up / down;
  Shift = a beat / an octave) · Cmd+D duplicate after itself (on the next bar line) · Cmd+C / Cmd+V copy / paste (paste lands where you
  last clicked) · Esc closes a card. Every control has a tooltip.
- **TAP** next to the BPM number: tap the beat (or press T), the BPM follows (the last 8 taps; a 2 s pause starts over).
- **HOST**: inside a DAW that is playing, a HOST 128 pill shows the DAW's tempo; tap it to use it. Ghost never changes your BPM by itself.
- **COUNT-IN ON / OFF** next to CLICK: in BPM mode, 4 3 2 1 on the big button, then the take starts on the downbeat. A tap cancels.
- **Speaker click masking**: when the metronome leaks from the speakers into the mic, Ghost knows exactly when it clicked. It finds the
  clicks that stand clear, learns the speaker-to-mic delay and level from them, subtracts every click at that delay, notches the click
  tones, and treats any onset left inside a click's window as a click unless it is clearly a drum / a real note. -6 dB leaks: extra hits
  5.2% -> 1.4% (synthesised beatboxers); through the plugin itself 16 / 16 hits, 0 extra.
- **Hints**: the roll's header says what to do next (sing; pick a tempo; PLAY, fix, DRAG MIDI). The export buttons wait for a take.
- **Hardening**: a take keeps the mode it was started in (switching MELODY / BEATBOX mid-take does not change what it is); the 90 s cap
  finishes the take properly; hearing hardened for real mics and mouths (build 2a: high-pass, auto level, NOT A DRUM class, octave twins,
  murmurs, plosive pops - see the numbers); no memory is freed on the audio thread (the playback sequence is parked for the message thread).

## Every button

- **MELODY | BEATBOX** — what the next recording is. Each mode keeps its own take, so switching never loses anything.
- **TAP TO RECORD / TAP TO BEATBOX** — start; tap again to stop. The ring shows how much of the 90 seconds is used.
- **TEMPO FREE | BPM** — FREE: record without a click, Ghost estimates the tempo afterwards. BPM: set the tempo first (drag the number),
  CLICK ON gives you a metronome while recording; the grid then starts at your first beat.
- **PITCH ORIGINAL | NEAREST NOTE | SNAP TO KEY** (melody) — ORIGINAL plays exactly the pitch you sang (bends to it, and the MIDI file
  carries pitch bend); NEAREST NOTE (default) rounds each note to the nearest semitone; SNAP TO KEY moves notes onto the key: the
  detected key by default, or pick one with KEY. Pitch correction and key snapping are separate steps, and the as-heard notes are always
  kept underneath.
- **KEEP ORIGINAL / 94 BPM / 188 BPM** — after a FREE take: keep the timing exactly as performed, or try the guessed tempo / its double /
  its half.
- **YOUR MELODY** roll — drag a note up or down to change its pitch, sideways to move it, drag its right edge to make it longer or
  shorter, double-click an empty spot to add a note, right-click (or Backspace) to delete, mouse wheel = louder / softer. Up / down arrow
  keys move the selected note a semitone (Shift = an octave).
- **YOUR BEAT** lanes (KICK, SNARE, HI-HAT, OPEN HAT, CLAP) — drag a hit to another lane to change what it is, sideways to move it,
  right-click for "Make it a ..." / Soft / Medium / Hard / Delete, mouse wheel or Shift-drag = velocity, double-click an empty spot to add.
  A faint hit with a dotted ring means Ghost was not sure which drum it was: drag it where it belongs.
- **PLAY / STOP** (also the space bar) — plays the take on the built-in sounds and sends the notes to your DAW as MIDI while it plays.
- **CLEAN** — melody: removes accidental blips, joins shaky notes, fixes a short wrong-key note between right ones, evens the velocities,
  keeps the melody. Drums: removes double hits, tightens timing a little (keeps the groove), evens velocities per drum. One UNDO away.
- **QUANTIZE OFF | 1/4 | 1/8 | 1/16 | 1/32** + **STRENGTH** — detection and quantize are separate: Ghost detects what you did first; the grid
  is a view on top. STRENGTH 50% moves notes half way to the grid. **SWING** (beats) delays the off-beats.
- **ON GRID | ORIGINAL** — what you hear and drag out: your notes on the grid (QUANTIZE / SWING applied), or your notes exactly where you
  performed them. The roll shows the same thing. (PITCH = ORIGINAL is the separate switch for hearing the exact pitch you sang.)
- **UNDO / REDO** — every edit, CLEAN, pitch change, tempo choice and new take is undoable (Cmd-Z / Shift-Cmd-Z too).
- **SOUND** — PIANO (first), SOFT KEYS, BELL, PLUCK for melodies. Beats play on built-in drums.
- **DRAG MIDI** — a standard MIDI file (type 1, 960 ticks per beat, the take's tempo, real note lengths and velocities; drums on channel 10
  with the General MIDI notes 36 kick, 38 snare, 42 closed hat, 46 open hat, 39 clap). Also saved in Documents/Ghosty/Ghost Hum/MIDI.
- **DRAG WAV** — 24-bit 48 kHz stereo of what you hear. Also saved in Documents/Ghosty/Ghost Hum/Audio.
- **TEACH GHOST YOUR SOUNDS** (beats) — make your kick 3 times, snare 3 times, hi-hat 3 times, open hat 3 times, clap 3 times (or SKIP).
  Ghost keeps them on this computer and uses them every time. FORGET WHAT I TAUGHT goes back to the built-in ears.
- **INPUT AUTO | TRACK | MAC MIC** — where Ghost listens (build 3). AUTO: this track when sound comes into it, otherwise your Mac's
  microphone (FL's channel rack). TRACK: only this track's audio. MAC MIC: always the Mac's microphone.
- **NEW** — clears the take (undoable). **BYPASS** — mutes the plugin. **GHOST ANIMATION** — the moving background on / off (G).
  **?** — the key commands. **TAP** / **HOST** / **COUNT-IN** — see "New in build 2".

Never destructive: the original recording's notes (ORIGINAL) are kept beside the pitch-corrected working notes, and quantize is a
separate view; nothing overwrites anything. Everything runs offline, on your computer.

## How it hears (short)

- **Melody**: the recording is brought to 16 kHz, pitch is tracked every 8 ms with YIN (several candidate periods per frame, so an octave
  slip never wins over a steady line), a note path is found with a Viterbi search over "silence + every note" (vibrato of +-50 cents
  stays one note; a real semitone change wins after ~40 ms), "da da da" re-attacks split repeated notes, scoops / fall-offs / octave
  flickers at a note's edges are folded into the note, blips under 50 ms and whispers are dropped, each note's pitch is the median past
  the scoop, the key is found with Krumhansl-Kessler profiles. Nothing is invented: a note needs pitched sound to exist.
- **Beatbox**: the five-band onset analysis shared with Ghosty Beats' RE-CREATE AS MIDI (Shared/HearCore.h), one hit per 50 ms (one mouth),
  12 features per hit (band shares, ring, top decay, brightness, mid noisiness, a voiced "boom", clap bursts, top drop, low pitch), a
  softmax classifier fitted on hundreds of synthesised mouths (GhostHum/tests/HumVoices.h; no recordings, no datasets), confidence from
  the gap between the best and second drum. TEACH adds your own examples as weighted nearest neighbours in the same space.
- **Tempo**: a comb search 55..220 BPM over the onsets (Gaussian fit, swung off-beats allowed), offered with its double and half.
- **Clicks** (GhostHum/HumClicks.h): the plugin keeps every metronome click's time with the take. Before the analysis, pass 1 matches the
  first 8 ms of the click template (2000 Hz accent / 1400 Hz beat) within 40 ms after each click time; the clicks that correlate > 0.6
  give the median delay and gain; pass 2 subtracts the template at that delay from every click (gain capped at the median, least squares
  below it, so a kick on the click is never carved); then 2000 / 1400 Hz notches; then a -5..+80 ms window per click in which an onset
  must be clearly a drum (or a note longer than 120 ms) to survive.

## Numbers (HumTests, synthesised voices; run `HumTests` on Linux / Mac; build 3: 384 checks, 0 failed)

Real-voice conditions (build 2; 30 singers / 30 beatboxers each, through a phone / laptop mic model):
melody 97-100% right under DC + rumble, reverb, clipping, -40 dB takes, mouth clicks, fan + hum, "la la" / "na na", closed-mouth humming,
low / high voices, phone low cut, metronome leaks -10 and -4 dB (vocal fry 91%, high child voice 93%, plosive pops 95%); beatbox 93-99%
found and 96-99% the right drum under breath / grunts / smacks, mouth clicks, DC + hum + fan, -40 dB, clipping, reverb, a -12 and -6 dB
metronome leak (1.2-1.4% extra), the phone stack.

- Melody, 80 random singers / 640 notes: 99.7% of the notes exactly right (pitch and within 80 ms), 0.8% extra notes, onsets off by 20 ms
  (the scoop into a note counts from its start), note ends by 12 ms on average. In a noisy room (hiss 36 dB under the voice): 97.5%.
- Beatbox, 60 held-out mouths / 1017 hits: 99.4% of the hits found (1.3 ms timing), 2.9% extra, the right drum 98.1% (kick 100%, snare 98%,
  hi-hat 95%, open hat 100%, clap 95%). Odd mouths the model never saw: 85% right built-in, 94.5% after TEACH.
- FREE tempo, 40 two-bar beats: the right tempo among the offers in 38, the best guess within 3% in 37.
- Also tested: quantize grid / strength / swing, CLEAN + UNDO + REDO, MIDI and WAV files, plugin state round trip, the plugin path through
  processBlock (record -> notes -> playback -> MIDI out), the export gate, and the window in both modes.
- Input sources (build 3): the FL generator layout (no input bus) records a synthesised voice through the fake microphone device at
  44.1 kHz (4 / 4 notes, the right rate, playback heard, 4 MIDI note-ons, count-in clicks heard and the click times kept); silence on both
  gives "No sound is coming in" after 2 s and at the stop; no mic and no input refuses the tap with a word (AUTO, TRACK, MAC MIC); a track
  with sound is used with the mic never armed; a track feeding digital zeros hands over to the mic within 0.5 s with the whole take kept
  from the tap, and the other way round the track wins; INPUT = MAC MIC on a live track takes the mic and survives the state round trip.

## Room left in the design

Takes are kept per mode in the processor (`takes[]`), notes are plain seconds + MIDI, and the roll, player and MIDI writer take a `Take`:
multi-track (melody + bassline + beatbox together, three MIDI drags) and "+ CHORDS" can be added without changing the file format.

## Files

GhostHum/HumModel.h (data, grid, undo, JSON), HumMelody.h (singing -> notes), HumBeat.h (beatbox -> hits, the classifier), HumClicks.h
(speaker click masking), HumTempo.h (tempo, swing, CLEAN), HumSounds.h (piano / drums / player / render), HumFiles.h (MIDI / WAV),
HumMic.h (the Mac's microphone as an input-only device, build 3), HumProcessor.h/.cpp (the plugin), HumLook.h (the look: tokens, glass, backdrop, controls, the record ring), HumRoll.h (piano roll + step
sequencer, selection, clipboard), HumEditor.h (the window, TEACH and ? cards, key commands), tests/HumVoices.h (our synthesised singers
and beatboxers + mic / room / noise models), tests/HumTests.cpp (the harness: `test`, `rw`, `plugin`, `beattrain [apply]`, `shots <dir>`),
Shared/HearCore.h (the analysis core shared with Ghosty Beats, moved there unchanged), GhostHum.cmake, packaging/, BUILD,
.github/workflows/ghosthum-mac.yml.

### Building the Mac mic in, for the next developer

The host side (processBlock) and the microphone side (HumProcessor::micCallback, on the device's thread) each write their own buffer
(`rec` at the host rate, `micRec` at the device rate), sized on the message thread before the device is armed; nothing allocates or locks
on either audio thread. `src` says who owns the take (SrcHost / SrcMic / SrcNone while AUTO decides); the 10 Hz timer (pollSources) decides
and the stop copies the chosen buffer and its rate into the take. The host's sample clock (`recClock`) keeps running whoever owns the
samples, so the metronome and the click times are the same in both cases. The harness fakes the device (`fakeMic`, `micFeed`) because
the Linux box has no audio device; the Mac build opens the real one (juce::AudioDeviceManager, input only).
