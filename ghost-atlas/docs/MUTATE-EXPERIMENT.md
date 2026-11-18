# MUTATE against random layering

The brief asked for this to be measured: are the sounds MUTATE makes actually better than layers put together at
random?

**Short answer**

- Against plain random layering, yes, clearly: by MUTATE's own listening checks and by two measures it does not steer
  by.
- Because of what it *chooses*, no, not that these figures can show. When the layers were drawn blind and then set
  up, listened to and put right exactly as MUTATE does it, the results measured the same.
- So what earns the difference is the setting-up and the listening. Whether the chosen layers sound better together
  than blind ones is a question for ears; a listening page with the first twelve sounds was made for that.

## What was compared

40 instruments that can carry a sound on their own (pitched, at least three octaves, clean loops, at most six from
any one category), each played four ways:

| | what it is |
|---|---|
| alone | the instrument by itself |
| mutate | MUTATE in its Evolve mode, as the instrument does it: layers shortlisted by likeness, ranked by fit, set up for their parts, played, put right, and tried again (up to four times) if they do not hold up |
| blind | the same in every way, except that the layers are drawn blind from the whole library instead of chosen; the same parts in the same order, the same setting-up, listening and putting right, the same four attempts; three draws each |
| random | the same number of layers drawn at random, each set to the same measured loudness, nothing else done; three draws each |

Every sound went through the engine over the same notes (low, middle, high, soft, hard and a five-note chord) with
the master effects off, and was measured the same way.

## The figures

| | sounds | first set of layers held up | pass all nine checks | main sound masked | pitch pulled (cents) | level the layers add (dB) | under 150 Hz | mono loss (dB) | sharpest ring (dB) | off pitch (cents) |
|---|---|---|---|---|---|---|---|---|---|---|
| alone | 40 | - | 35 (88 %) | 1 % | 0.2 | 0.0 | 6 % | 1.47 | 10.2 | 4.1 |
| mutate | 40 | 38 (95 %) | 40 (100 %) | 30 % | 2.5 | 2.7 | 4 % | 1.68 | 10.2 | 2.9 |
| blind | 120 | 114 (95 %) | 119 (99 %) | 27 % | 3.0 | 2.1 | 5 % | 1.35 | 9.7 | 3.3 |
| random | 120 | - | 73 (61 %) | 41 % | 5.0 | 4.5 | 9 % | 1.25 | 10.7 | 4.7 |

The nine checks are MUTATE's own (it sounds; nothing clips; the low end is not piling up; the main sound is not
buried; no harsh ring; in tune; holds up in mono; no loop seams; even across the keys). MUTATE and the blind control
both work towards those checks and give up on what fails them, so a full pass rate there only says the refinement
does what it sets out to do. Two columns are things MUTATE does not steer by: *main sound masked* is the share of
the starting sound's energy, over the whole held chord, that lies in bands where the added layers are louder than it
is; *pitch pulled* is how far the layers move the measured pitch from where the starting sound sits alone. Lower is
better in both.

## Start by start

The same starting sound, MUTATE against the average of the other arm's three draws. "Better" means lower.

| measure | against random: better / same / worse | against blind: better / same / worse |
|---|---|---|
| main sound masked | 27 / 2 / 11 (sign test p = 0.014) | 19 / 0 / 21 (p = 0.88) |
| pitch pulled | 28 / 1 / 11 (p = 0.009) | 22 / 3 / 15 (p = 0.32) |
| share of energy under 150 Hz | 30 / 2 / 8 (p < 0.001) | 22 / 2 / 16 (p = 0.42) |
| mono loss | 8 / 0 / 32 (p < 0.001) | 11 / 0 / 29 (p = 0.006) |
| sharpest ring | 22 / 1 / 17 (p = 0.52) | 15 / 2 / 23 (p = 0.26) |

- **Against random**, MUTATE covers the starting sound less, pulls its pitch about half as far and keeps the low end
  clearer. Random layers buried the starting sound in 33 of 120 tries, left 8 out of tune, piled up the low end in 5
  and failed in mono in 5.
- **Against blind**, nothing separates them on the measures that matter here. The first set of layers held up 95 % of
  the time either way (38 of 40 and 114 of 120).
- **Mono** is the one place MUTATE is measurably worse than both: its layers lose about 0.3 to 0.4 dB more when
  folded to mono. All of them are inside the check's limit.

## Held sounds and struck sounds

| starting sound | | main sound masked | pitch pulled (cents) | level the layers add (dB) |
|---|---|---|---|---|
| holds (27) | mutate | 3.5 % | 1.7 | 1.3 |
| | blind | 5.3 % | 2.5 | 1.2 |
| | random | 24.3 % | 4.5 | 2.9 |
| struck or plucked (13) | mutate | 85.7 % | 4.1 | 5.7 |
| | blind | 71.9 % | 3.9 | 3.8 |
| | random | 75.4 % | 5.9 | 7.8 |

Under a sound that holds, MUTATE's layers stay out of its way (and slightly more so than blind ones, 18 starts to 9,
which is not enough to call). Under a struck sound MUTATE likes a held layer that outlasts the pluck. The pluck is
still heard at its attack, which is what MUTATE's own check listens to for struck sounds, but over the whole chord
the layers carry most of the energy, and more than a blind draw's do (in 12 of the 13 struck starts). Whether a
pluck over a pad is what was wanted is a matter of taste; it is the clearest place where the choosing could be made
better.

## What this shows, and what it does not

- It shows that setting layers up for their parts and then listening and putting right does real work: it is the
  whole of the difference between the blind arm and the random arm.
- It does not show that the fit ranking picks better layers than chance. That may be because the checks leave little
  room (whatever survives them measures alike), because masking and pitch are blunt (they do not hear whether two
  timbres suit each other), or because the ranking does not help yet. These figures cannot tell which.
- It cannot say whether a sound is more beautiful.

Nothing in the instrument was changed because of this: the blind draw exists only inside the experiment.

## Running it again

    GhostAtlasTests experiment <folder> [count]

writes `report.md`, `results.tsv`, the sounds it made (`.atlas`) and, for the first twelve starting instruments, the
five-note chord of each arm as WAV, all brought to the loudness of the starting sound. The rows behind this page are
in `mutate-experiment-results.tsv` beside it. The audio is not kept in the repository. A few instruments choose among
their recordings at random, so a second run can differ by a sound or two (one run counted 36 of the 40 passing alone,
the next 35).
