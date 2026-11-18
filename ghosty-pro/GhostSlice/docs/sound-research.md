# Ghost Slice build 6: how it sounds, and why (plain words)

## What made INSTRUMENT sound distorted (measured first)
Measured on a public-domain spoken voice (LibriVox) and our synthetic sung voice, holding a key at its own note, +7, and an octave up / down,
through the real engine and FX (`GhostSliceTests diag`):

| | HNR | energy off the harmonics | period-to-period wobble |
|---|---|---|---|
| build 5 INSTRUMENT (PSOLA, real voice) | 15.5 dB | -13.6 dB | 4.4 % |
| build 6 INSTRUMENT end (real voice) | 22.0 dB | -22.9 dB | 2.6 % |

The cause was the old PSOLA tone: it held a vowel by re-ordering pitch-period grains with a random walk. Neighbouring grains came from
different places, so they did not line up (roughness, "buzz"), each grain was normalised separately (a level that pulses at the pitch),
and keys between zones were resampled (the formants moved). Clipping, the tanh warmth (-40 dB under the signal), aliasing in the reads and
loop clicks were all measured and were NOT the problem.

## What we chose
- **VOICE 0 = the real recording.** At VOICE 0 with no pitch change the take plays the syllables' own samples, bit for bit (test: null
  -147 dB). Any other setting plays one model of the syllable: its harmonics with their real amplitudes AND phases plus the residual
  (breath, consonants, the room) - Spectral Modeling Synthesis (Serra & Smith 1990) with McAulay-Quatieri phase. Played back unchanged
  this model gives the recording back (-142 dB). Re-pitching is "shape-invariant": each harmonic keeps its phase relative to the
  fundamental, so every period keeps the voice's own wave shape (that is what makes it sound like a person, not a vocoder). Formants are
  read off the voice's own harmonic peaks at the new pitch (a true-envelope estimate): an octave up or down the vowel's envelope moves
  less than 30 cents.
- **Turning VOICE up** moves the harmonic amplitudes (in log level) toward the smooth instrument envelope, the phases toward fixed calm
  ones, the vowel stretches to the note, the consonants and the residual fade, and the instrument's soft air comes in. It is ONE
  synthesis the whole way: we never cross-fade two different syntheses (that combs and goes phasey in the middle). Test: no comb notches
  anywhere in a 0 / 25 / 50 / 75 / 100 % sweep, no clicks.
- **The instrument end** is WORLD (M. Morise, modified BSD): CheapTrick's pitch-adaptive envelope and D4C's aperiodicity, played by our own
  harmonic + noise synthesis. Breath noise is kept out of the low bands (low-band noise was the "grit"; air above ~2.4 kHz sounds soft).
- **DREAM / DRIFT freeze** is an identity phase-locked vocoder (Laroche & Dolson 1999): peaks advance by their own frequency, the bins
  around a peak keep their phase to it, onsets reset the phase, stereo shares one phase advance. Clean and glassy at low DREAM; the
  random-phase Paulstretch smear (Paul Nasca's published algorithm, re-implemented from the description) only comes in toward the top of
  DREAM or with BLUR. Tests: a sine / chord stretched 4x stays within 0.1 cent with 0.2 dB ripple; a click train keeps its crest factor;
  stereo keeps its image.
- **Grains** (PAD / TEXTURE): windowed-sinc reads in CLEAR, level normalised by the expected overlap (DENSITY never pumps the level),
  at least 50 % overlap in CLEAR, window Tukey / Hann / Gaussian, ROUTE grains -> freeze or freeze -> grains.
- **Aphex Twin's methods, in plain terms:** documented interviews and gear lists describe hand-edited loops, extreme time-stretching
  into drones (the Ambient Works sound), detuned "player piano" melodies that are human but not quite, 303 acid lines with odd-length
  motifs against the bar, and FM bells. We built these as rules: Paulstretch-style freeze at high DREAM, the LULLABY / PREPARED piano
  styles, ACID / AFX ACID (an odd-length motif against 16) and FM BELLS. Nothing of his music is copied.

## Considered and not used
PSOLA kept only for older (build 5) takes; WORLD's own synthesis (pulse + noise) is buzzier than our harmonic synthesis; Rubber Band is
GPL (listening reference only); the C++ Paulstretch is GPL (we wrote ours from the description); Mutable Instruments Clouds / Beads (MIT)
was the design reference for the grain parameters, no code compiled in.

## The melody brain
Every melody / chord / arp generator the house has made, as C++: our own styles; Block Step / Ghost House's piano page (ANTHEM, PROG,
PIANO HOUSE, DEEP, EUPHORIC, LULLABY, RAIN, PREPARED, DRIFT, SKITTER, plus BUILD and BRIDGE from Ghost House's song sections) and synth
page (ACID, AFX ACID, 101 ROLL, FM BELLS, SUPERSAW, PLUCKS, STABS); REVERIE (6 emotions, 5 textures); the learned POP BRAIN (POP909, MIT)
and ACID BRAIN (Aicd, MIT); and Professor Bernstein's ArpEZator engine itself (SUNRISE / DUSK / MIDNIGHT arps and hooks, TRAP's 6
flavours, HIP HOP's 5, the HOOK LAB styles). Build 7 adds PARLOUR PIANO (WISTFUL, STILL NIGHT): Block Step's learned piano, n-grams from
12 Mutopia piano scores (11 public domain, 1 CC BY-SA 4.0, attributed in THIRD_PARTY_LICENSES.md). Everything is in
the take's key; LENGTH 1-16 bars, long patterns develop (A A' A B, then the theme lifted and answered anew).

## Build 8: PLAY IT (the roll is yours, ArpEZator's controls)
- **Your notes.** A note you move, resize, add or link is *locked* (ArpEZator's word): it is stored with the take's recipe and stays
  through MASTER, the ARP, a chord swap and VARY. The take's own notes you delete or move away are remembered as removed. The line step
  (`edit::editLine`) puts it together: removed notes out, yours in, one line for INSTRUMENT / CHOPS (what yours overlap is cut), every
  pitch snapped to the take's key, every time inside the LENGTH. The roll only lets a note land on a note of the key; the engine snaps
  again, so an out-of-key note cannot exist (tested with random out-of-key edits in 5 keys x 7 styles x 4 lengths).
- **ArpEZator's engine, reused (ArpEZator/, our own code, linked since build 6).** The ARP is its `composeArp` + `shapeArp` over the
  take's chords (turned into its `Harmony`); MASTER is its `toolSimpler` / `toolBusier` (locked notes are left alone, as in ArpEZator);
  the chord choices are its `altChord` alternatives (auto, sus, 7th, lush, over the 3rd, swap down / up) plus every other chord of the
  key, coloured by SIMPLE (triads) / RICH (7ths, 9ths, 11ths). Its "borrow" alternative is left out: it leaves the key. A swapped chord
  is stored as (degree, alternative), so SIMPLE / RICH still colour it. After a swap the melody re-fits: on-beat and long notes move to
  the nearest tone of the new chord, the rest to the key. GLIDE / FATTEN / STEP are ArpEZator's lines: GLIDE holds the note and slides
  the voice into the next one (the slide starts where the dot is), FATTEN lifts the next note's level (ArpEZator's `playVel`), and in
  the MIDI a GLIDE is an overlap (a DAW's legato / portamento).
- **Fast and exact.** An edit re-renders only the preview: the zones and the syllables' models of the take before the edit are reused
  when nothing but the edit changed (`generate (..., reuse)`). Measured on the 2-core test box: about 0.5 s instead of about 5 s, and
  bit for bit the same audio as a full render of the same recipe (tested). The old take keeps playing until the new one is swapped in;
  no allocation on the audio thread (tested).
- **Old sessions.** A recipe without an edit writes and reads exactly as before, and every PLAY IT step is skipped when there is no
  edit, so build 7 sessions render bit for bit the same (checked by rendering the same recipes with the build 7 and build 8 code).

## Build 9 (engine 7): VOICE 0 closer to the recording, and the roll's loose ends
- **In-order PSOLA at VOICE 0 (review 2A).** With VOICE at 0-5 % and a re-pitch of up to a fifth, each syllable is moved by TD-PSOLA
  (Moulines & Charpentier 1990) on its own periods, in order: one pitch mark per period along the analysis' f0 track, each placed where
  the waveform repeats the last period best (normalised correlation, +-15 %), 2-period Hann grains re-spaced at the new pitch; the
  consonants and breaths are the recording itself (6 ms ramps). A formant guard then lifts each frame's cepstrally smoothed spectrum
  to the recording's at the same moment (+-12 dB; a true-envelope style correction after Robel & Rodet 2005), so the vowel keeps its
  formants exactly while PSOLA keeps the periods, phases and breath. Measured (VOICE 0, MUSICAL, 6 syllables each, the same notes):

  | | build 8 (the model) | build 9 (PSOLA + guard) |
  |---|---|---|
  | synthetic voice, worst syllable-envelope correlation | 0.981 | 0.999 |
  | public-domain voice, worst syllable-envelope correlation | 0.922 | 0.963 |
  | public-domain voice, HNR off the recording (worst syllable) | 6.0 dB | 3.9 dB |

  Tried and not kept: PSOLA with the epoch marker of build 5 (it found too few marks in speech: 0.833 worst), plain PSOLA without the
  guard (0.838 at +4.5 semitones), a 2-semitone limit (nothing changed for most notes). VOICE 0 with no re-pitch is still the
  recording bit for bit; the VOICE sweep has no comb (tested). Takes from before (engine 6) render exactly as before.
- **TEXTURE edits** reuse the take's drone zones like INSTRUMENT / PAD: ~0.3 s instead of ~5.7 s, bit for bit a full render.
- **Older takes** (build 5) edit straight away: on the first edit the take is upgraded silently (its notes, read back from its MIDI,
  become yours so nothing you see moves; CHOPS: its phrase is made again, the old one has no chop numbers). One UNDO brings it back.
- **VARY remembers deletions**: deleting a note of a line leaves a rest (a time span), and a VARY keeps those spans empty.
- **Chop octaves**: a chop note you move is re-pitched by the formant-keeping phase vocoder to exactly where you put it, up to
  +-19 semitones (build 8 folded it into the nearest octave); a big move skips the old-pitch attack splice.
- **CHORD BED** (INSTRUMENT, off by default): the take's chords held on the same voice's held tone (its own zones), slow swell, darker
  (2.6 kHz), mixed at the slider's level under the melody.

## Build 10 (engine 8): big moves at VOICE 0, chords in the MIDI, older takes keep their sound
- **Big moves.** The in-order PSOLA + formant guard now covers re-pitches up to an octave (ratio 0.48-2.08); beyond that the model
  takes over. The guard gets +-18 dB of room, a second pass (as the true envelope iterates), and its ends now blend into the plain
  output by the frames' weight (build 9 switched at a threshold: that step was heard as a click on some speech syllables; engine 7
  takes keep build 9's exact output). Measured on the public-domain voice, VOICE 0, MUSICAL, 12 takes (4 keys x 3 seeds):

  | | build 9 (engine 7) | build 10 (engine 8) |
  |---|---|---|
  | syllables moved by more than a fifth (9): worst envelope correlation | 0.864 | 0.984 |
  | the same, mean | 0.938 | 0.990 |
  | every measured syllable of the 12 takes: worst | 0.864 | 0.955 |
  | clicks in the 12 previews (click detector) | 44 | 0 |
  | worst HNR off the recording (big moves) | 6.1 dB | 7.5 dB |

  HNR (periodicity) on the single worst big move is 1.4 dB further off than the model's; everything else is closer. On build 9's own
  six syllables the worst moves 0.963 -> 0.958 (the second guard pass; with one pass it is 0.965 but the 12-take worst is 0.928).
- **CHORDS in DRAG MIDI.** INSTRUMENT and CHOPS takes write a second MIDI track named CHORDS on channel 2: the CHORD BED's notes when it
  is on, otherwise the take's chords held for their spans. PAD's MIDI already is its chords. A build 8 take's MIDI is unchanged.
- **Older takes keep their sound.** The first edit of a build 5 take now also gives the upgraded take the parts of the sound the old
  one used (the same syllables / chops, in the same order), and a CHOPS take keeps its phrase: a take's notes now carry their chop
  numbers in every engine (no sound changes), so they become yours with their chops.
