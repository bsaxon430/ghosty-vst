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
flavours, HIP HOP's 5, the HOOK LAB styles). The Satie piano brain is left out: one of its Mutopia sources is CC BY-SA. Everything is in
the take's key; LENGTH 1-16 bars, long patterns develop (A A' A B, then the theme lifted and answered anew).
